#include "MainFrame.h"

#include "AppSettings.h"
#include "AuthCredentials.h"
#include "BitbucketClient.h"
#include "ConfigLoader.h"
#include "ConfigWriter.h"
#include "DebugConsole.h"
#include "DownloadProgressDialog.h"
#include "GitClient.h"
#include "NexusClient.h"

#include <wx/clipbrd.h>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/dataobj.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace {

constexpr int kIdLoadConfig = wxID_HIGHEST + 1;
constexpr int kIdApply = wxID_HIGHEST + 2;
constexpr int kIdDeselectAll = wxID_HIGHEST + 3;
constexpr int kIdViewDebugConsole = wxID_HIGHEST + 4;
constexpr int kIdLoadLastConfig = wxID_HIGHEST + 5;
constexpr int kIdSaveAs = wxID_HIGHEST + 6;
constexpr int kIdCopyConfig = wxID_HIGHEST + 7;
constexpr int kIdLoadFromBitbucket = wxID_HIGHEST + 8;
constexpr int kSectionLabelWidth = 64;
constexpr int kFieldLabelWidth = 72;

bool HasSource(const confy::ComponentConfig& component) {
    return !component.source.url.empty() || !component.source.branchOrTag.empty() ||
        !component.source.script.empty();
}

bool HasArtifact(const confy::ComponentConfig& component) {
    return !component.artifact.url.empty() || !component.artifact.version.empty() ||
        !component.artifact.buildType.empty() || !component.artifact.script.empty();
}

std::string ResolveM2SettingsPath() {
#ifdef _WIN32
    std::string homeDir;
    if (const char* h = std::getenv("USERPROFILE")) {
        homeDir = h;
    } else {
        const char* drive = std::getenv("HOMEDRIVE");
        const char* homepath = std::getenv("HOMEPATH");
        if (drive && homepath) {
            homeDir = std::string(drive) + homepath;
        }
    }
#else
    const char* homeEnv = std::getenv("HOME");
    const std::string homeDir = homeEnv ? homeEnv : "";
#endif

    if (homeDir.empty()) {
        return {};
    }
    return (std::filesystem::path(homeDir) / ".m2" / "settings.xml").string();
}

class BitbucketLoadDialog final : public wxDialog {
public:
    BitbucketLoadDialog(wxWindow* parent,
                        confy::BitbucketClient& client,
                                                const std::string& repoUrl)
        : wxDialog(parent, wxID_ANY, "Load from Bitbucket", wxDefaultPosition, wxSize(680, 460)),
                    client_(client),
                    repoUrl_(repoUrl) {
        auto* root = new wxBoxSizer(wxVERTICAL);

                root->Add(new wxStaticText(this, wxID_ANY, wxString::Format("Repo: %s", repoUrl_)),
                                    0,
                                    wxLEFT | wxRIGHT | wxTOP | wxEXPAND,
                                    10);

        auto* branchRow = new wxBoxSizer(wxHORIZONTAL);
        branchRow->Add(new wxStaticText(this, wxID_ANY, "Branch"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        branchChoice_ = new wxChoice(this, wxID_ANY);
        branchRow->Add(branchChoice_, 1, wxEXPAND);
                refreshButton_ = new wxButton(this, wxID_ANY, "Refresh");
                branchRow->Add(refreshButton_, 0, wxLEFT, 8);
                root->Add(branchRow, 0, wxALL | wxEXPAND, 10);

        root->Add(new wxStaticText(this, wxID_ANY, "XML files"), 0, wxLEFT | wxRIGHT, 10);
        fileList_ = new wxListBox(this, wxID_ANY);
        root->Add(fileList_, 1, wxALL | wxEXPAND, 10);

        statusLabel_ = new wxStaticText(this, wxID_ANY, "Enter repo URL and click Fetch.");
        root->Add(statusLabel_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

        auto* buttons = new wxStdDialogButtonSizer();
        okButton_ = new wxButton(this, wxID_OK, "Load");
        okButton_->Disable();
        buttons->AddButton(okButton_);
        buttons->AddButton(new wxButton(this, wxID_CANCEL, "Cancel"));
        buttons->Realize();
        root->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 10);

        SetSizerAndFit(root);
        SetMinSize(wxSize(680, 460));

        refreshButton_->Bind(wxEVT_BUTTON, &BitbucketLoadDialog::OnRefresh, this);
        branchChoice_->Bind(wxEVT_CHOICE, &BitbucketLoadDialog::OnBranchChanged, this);
        fileList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdateOkEnabled(); });
        okButton_->Bind(wxEVT_BUTTON, &BitbucketLoadDialog::OnOk, this);

        StartLoadBranchesAndDefaultFiles();
    }

    ~BitbucketLoadDialog() override {
        aliveFlag_->store(false);
    }

    std::string SelectedBranch() const {
        return selectedBranch_;
    }

    std::string SelectedFile() const {
        return selectedFile_;
    }

private:
    void StartLoadBranchesAndDefaultFiles() {
        const auto requestId = requestId_.fetch_add(1) + 1;
        SetBusyState(true, "Loading branches from Bitbucket...");

        auto alive = aliveFlag_;
        std::thread([this, alive, requestId]() {
            std::vector<std::string> branches;
            std::string error;
            const auto ok = client_.ListBranches(repoUrl_, branches, error);

            wxTheApp->CallAfter([this, alive, requestId, ok, branches = std::move(branches), error = std::move(error)]() mutable {
                if (!alive->load() || requestId != requestId_.load()) {
                    return;
                }

                if (!ok) {
                    SetBusyState(false, "");
                    wxMessageBox(error, "Bitbucket error", wxOK | wxICON_ERROR, this);
                    return;
                }

                branchChoice_->Clear();
                for (const auto& branch : branches) {
                    branchChoice_->Append(branch);
                }

                int selectedIndex = wxNOT_FOUND;
                for (unsigned int i = 0; i < branchChoice_->GetCount(); ++i) {
                    if (branchChoice_->GetString(i) == "master") {
                        selectedIndex = static_cast<int>(i);
                        break;
                    }
                }
                if (selectedIndex == wxNOT_FOUND && branchChoice_->GetCount() > 0) {
                    selectedIndex = 0;
                }
                if (selectedIndex != wxNOT_FOUND) {
                    branchChoice_->SetSelection(selectedIndex);
                }

                SetBusyState(false, "");
                if (branchChoice_->GetSelection() != wxNOT_FOUND) {
                    StartRefreshFilesForSelectedBranch();
                    return;
                }
                statusLabel_->SetLabelText("No branches available for this repository.");
            });
        }).detach();
    }

    void StartRefreshFilesForSelectedBranch() {
        const auto branch = branchChoice_->GetStringSelection().ToStdString();
        if (branch.empty()) {
            fileList_->Clear();
            UpdateOkEnabled();
            return;
        }

        const auto requestId = requestId_.fetch_add(1) + 1;
        fileList_->Clear();
        SetBusyState(true, "Loading XML files from Bitbucket...");

        auto alive = aliveFlag_;
        std::thread([this, alive, requestId, branch]() {
            std::vector<std::string> files;
            std::string error;
            const auto ok = client_.ListTopLevelXmlFiles(repoUrl_, branch, files, error);

            wxTheApp->CallAfter([this,
                                 alive,
                                 requestId,
                                 branch,
                                 ok,
                                 files = std::move(files),
                                 error = std::move(error)]() mutable {
                if (!alive->load() || requestId != requestId_.load()) {
                    return;
                }

                if (!ok) {
                    SetBusyState(false, "");
                    wxMessageBox(error, "Bitbucket error", wxOK | wxICON_ERROR, this);
                    return;
                }

                fileList_->Clear();
                for (const auto& file : files) {
                    fileList_->Append(file);
                }
                if (!files.empty()) {
                    fileList_->SetSelection(0);
                }

                SetBusyState(false, "");
                UpdateOkEnabled();
                statusLabel_->SetLabelText(wxString::Format("Found %u XML file(s) in branch '%s'.",
                                                            fileList_->GetCount(),
                                                            branch));
            });
        }).detach();
    }

    void OnRefresh(wxCommandEvent&) {
        StartLoadBranchesAndDefaultFiles();
    }

    void OnBranchChanged(wxCommandEvent&) {
        StartRefreshFilesForSelectedBranch();
    }

    void OnOk(wxCommandEvent& event) {
        if (branchChoice_->GetSelection() == wxNOT_FOUND || fileList_->GetSelection() == wxNOT_FOUND) {
            event.Skip();
            return;
        }

        selectedBranch_ = branchChoice_->GetStringSelection().ToStdString();
        selectedFile_ = fileList_->GetStringSelection().ToStdString();
        event.Skip();
    }

    void SetBusyState(bool busy, const wxString& message) {
        branchChoice_->Enable(!busy);
        fileList_->Enable(!busy);
        refreshButton_->Enable(!busy);
        okButton_->Enable(!busy && fileList_->GetSelection() != wxNOT_FOUND &&
                          branchChoice_->GetSelection() != wxNOT_FOUND);
        if (busy) {
            statusLabel_->SetLabelText(message);
        }
    }

    void UpdateOkEnabled() {
        okButton_->Enable(fileList_->GetSelection() != wxNOT_FOUND && branchChoice_->GetSelection() != wxNOT_FOUND);
    }

    confy::BitbucketClient& client_;
    std::string repoUrl_;
    wxChoice* branchChoice_{nullptr};
    wxListBox* fileList_{nullptr};
    wxStaticText* statusLabel_{nullptr};
    wxButton* refreshButton_{nullptr};
    wxButton* okButton_{nullptr};
    std::string selectedBranch_;
    std::string selectedFile_;
    std::atomic<std::uint64_t> requestId_{0};
    std::shared_ptr<std::atomic<bool>> aliveFlag_{std::make_shared<std::atomic<bool>>(true)};
};

}  // namespace

namespace confy {

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Confy", wxDefaultPosition, wxSize(900, 600)) {
    CreateStatusBar(1);
    SetStatusText("Config: none");

    auto* fileMenu = new wxMenu();
    fileMenu->Append(kIdLoadConfig, "&Load Config...\tCtrl+O");
    fileMenu->Append(kIdSaveAs, "Save &As...\tCtrl+Shift+S");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit");

    auto* editMenu = new wxMenu();
    editMenu->Append(wxID_SELECTALL, "Select &All");
    editMenu->Append(kIdDeselectAll, "&Deselect All");
    editMenu->AppendSeparator();
    editMenu->Append(kIdCopyConfig, "&Copy Config");

    auto* viewMenu = new wxMenu();
    viewMenu->AppendCheckItem(kIdViewDebugConsole, "&Debug Console");

    auto* menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(editMenu, "&Edit");
    menuBar->Append(viewMenu, "&View");
    SetMenuBar(menuBar);

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    statusLabel_ = new wxStaticText(this, wxID_ANY, "");
    statusLabel_->Hide();
    rootSizer->Add(statusLabel_, 0, wxALL | wxEXPAND, 8);

    componentScroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    componentScroll_->SetScrollRate(0, 6);

    componentContentPanel_ = new wxPanel(componentScroll_);
    componentListSizer_ = new wxBoxSizer(wxVERTICAL);
    componentContentPanel_->SetSizer(componentListSizer_);

    auto* scrollSizer = new wxBoxSizer(wxVERTICAL);
    scrollSizer->Add(componentContentPanel_, 1, wxEXPAND);
    componentScroll_->SetSizer(scrollSizer);

    emptyStatePanel_ = new wxPanel(componentContentPanel_);
    auto* emptyStateSizer = new wxBoxSizer(wxVERTICAL);
    emptyStateSizer->AddStretchSpacer();

    loadConfigButton_ = new wxButton(emptyStatePanel_, kIdLoadConfig, "Load config XML");
    auto buttonFont = loadConfigButton_->GetFont();
    buttonFont.MakeLarger();
    loadConfigButton_->SetFont(buttonFont);
    loadConfigButton_->SetMinSize(wxSize(280, 72));
    emptyStateSizer->Add(loadConfigButton_, 0, wxALIGN_CENTER_HORIZONTAL);

    loadLastConfigButton_ = new wxButton(emptyStatePanel_, kIdLoadLastConfig, "Load Last");
    loadLastConfigButton_->SetMinSize(wxSize(280, 36));
    loadLastConfigButton_->Hide();
    emptyStateSizer->Add(loadLastConfigButton_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 12);

    loadFromBitbucketButton_ = new wxButton(emptyStatePanel_, kIdLoadFromBitbucket, "Load from Bitbucket");
    loadFromBitbucketButton_->SetMinSize(wxSize(280, 36));
    emptyStateSizer->Add(loadFromBitbucketButton_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 8);

    wxString lastPath(AppSettings::Get().GetLastConfigPath());
    if (!lastPath.empty()) {
        loadLastConfigButton_->SetToolTip(lastPath);
        loadLastConfigButton_->Show();
    }

    emptyStateSizer->AddStretchSpacer();
    emptyStatePanel_->SetSizer(emptyStateSizer);

    componentListSizer_->Add(emptyStatePanel_, 1, wxEXPAND);

    rootSizer->Add(componentScroll_, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    applyButton_ = new wxButton(this, kIdApply, "Apply");
    applyButton_->Disable();
    rootSizer->Add(applyButton_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 8);

    SetSizer(rootSizer);

    Bind(wxEVT_MENU, &MainFrame::OnLoadConfig, this, kIdLoadConfig);
    Bind(wxEVT_BUTTON, &MainFrame::OnLoadConfig, this, kIdLoadConfig);
    Bind(wxEVT_BUTTON, &MainFrame::OnLoadLastConfig, this, kIdLoadLastConfig);
    Bind(wxEVT_BUTTON, &MainFrame::OnLoadFromBitbucket, this, kIdLoadFromBitbucket);
    Bind(wxEVT_MENU, &MainFrame::OnSaveAs, this, kIdSaveAs);
    Bind(wxEVT_MENU, &MainFrame::OnSelectAll, this, wxID_SELECTALL);
    Bind(wxEVT_MENU, &MainFrame::OnDeselectAll, this, kIdDeselectAll);
    Bind(wxEVT_MENU, &MainFrame::OnCopyConfig, this, kIdCopyConfig);
    Bind(wxEVT_MENU, &MainFrame::OnToggleDebugConsole, this, kIdViewDebugConsole);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateSaveAs, this, kIdSaveAs);
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateSelectAll, this, wxID_SELECTALL);
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateDeselectAll, this, kIdDeselectAll);
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateCopyConfig, this, kIdCopyConfig);
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateDebugConsole, this, kIdViewDebugConsole);
    Bind(wxEVT_BUTTON, &MainFrame::OnApply, this, kIdApply);
    Bind(wxEVT_SIZE, &MainFrame::OnFrameSize, this);
}

MainFrame::~MainFrame() {
    StopMetadataWorkers();
}

void MainFrame::OnLoadConfig(wxCommandEvent&) {
    wxString initialDirectory;
    const auto executablePath = wxStandardPaths::Get().GetExecutablePath();
    if (!executablePath.empty()) {
        wxFileName executableFile(executablePath);
        if (executableFile.IsOk()) {
            initialDirectory = executableFile.GetPath();
        }
    }

    wxFileDialog dialog(this,
                        "Open config XML",
                        initialDirectory,
                        "",
                        "XML files (*.xml)|*.xml|All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    LoadConfigFromPath(dialog.GetPath());
}

void MainFrame::OnLoadLastConfig(wxCommandEvent&) {
    const auto lastPath = AppSettings::Get().GetLastConfigPath();
    if (lastPath.empty()) {
        return;
    }
    LoadConfigFromPath(wxString(lastPath));
}

void MainFrame::OnLoadFromBitbucket(wxCommandEvent&) {
    wxLogMessage("[bitbucket] UI action: Load from Bitbucket clicked");
    const auto repoUrl = AppSettings::Get().GetXmlRepoUrl();
    if (repoUrl.empty()) {
        wxLogError("[bitbucket] Missing /XmlRepoUrl in confy.conf");
        wxMessageBox("Missing XML repo in confy.conf. Set /XmlRepoUrl before using Load from Bitbucket.",
                     "Bitbucket",
                     wxOK | wxICON_ERROR,
                     this);
        return;
    }
    wxLogMessage("[bitbucket] Using configured repo url=%s", repoUrl.c_str());

    const auto settingsPath = ResolveM2SettingsPath();
    if (settingsPath.empty()) {
        wxLogError("[bitbucket] Unable to resolve m2 settings path");
        wxMessageBox("Unable to resolve home directory for ~/.m2/settings.xml.",
                     "Bitbucket auth",
                     wxOK | wxICON_ERROR,
                     this);
        return;
    }
    wxLogMessage("[bitbucket] Loading credentials from %s", settingsPath.c_str());

    AuthCredentials credentials;
    std::string authError;
    if (!credentials.LoadFromM2SettingsXml(settingsPath, authError)) {
        wxLogError("[bitbucket] Failed loading credentials: %s", authError.c_str());
        wxMessageBox(wxString("Unable to load Maven credentials: ") + authError,
                     "Bitbucket auth",
                     wxOK | wxICON_ERROR,
                     this);
        return;
    }
    wxLogMessage("[bitbucket] Credentials loaded from m2 settings");

    BitbucketClient client(std::move(credentials));
    BitbucketLoadDialog dialog(this, client, repoUrl);
    if (dialog.ShowModal() != wxID_OK) {
        wxLogMessage("[bitbucket] User canceled Bitbucket file selection dialog");
        return;
    }

    const auto branch = dialog.SelectedBranch();
    const auto filePath = dialog.SelectedFile();
    if (repoUrl.empty() || branch.empty() || filePath.empty()) {
        wxLogError("[bitbucket] Missing selection after dialog repo=%s branch=%s file=%s",
                   repoUrl.c_str(),
                   branch.c_str(),
                   filePath.c_str());
        wxMessageBox("Missing repository, branch, or XML file selection.",
                     "Bitbucket",
                     wxOK | wxICON_ERROR,
                     this);
        return;
    }
    wxLogMessage("[bitbucket] Selected branch=%s file=%s", branch.c_str(), filePath.c_str());

    const auto executablePath = wxStandardPaths::Get().GetExecutablePath();
    wxFileName executableFile(executablePath);
    wxString outputPath;
    if (executableFile.IsOk()) {
        outputPath = executableFile.GetPathWithSep() + "from_bitbucket.xml";
    } else {
        outputPath = wxFileName::GetCwd() + wxFILE_SEP_PATH + "from_bitbucket.xml";
    }

    wxBusyCursor busyCursor;
    SetStatusText("Downloading XML from Bitbucket...");
    wxYieldIfNeeded();

    std::string downloadError;
    if (!client.DownloadFile(repoUrl, branch, filePath, outputPath.ToStdString(), downloadError)) {
        wxLogError("[bitbucket] Download failed: %s", downloadError.c_str());
        SetStatusText("Config: none");
        wxMessageBox(downloadError, "Bitbucket download failed", wxOK | wxICON_ERROR, this);
        return;
    }
    wxLogMessage("[bitbucket] Download completed to %s", outputPath.ToStdString().c_str());

    SetStatusText("Loading downloaded XML...");
    wxYieldIfNeeded();
    LoadConfigFromPath(outputPath);
}

void MainFrame::OnSaveAs(wxCommandEvent&) {
    wxString initialDirectory;
    wxString initialFileName;
    if (!loadedConfigPath_.empty()) {
        wxFileName loadedFile{wxString(loadedConfigPath_)};
        if (loadedFile.IsOk()) {
            initialDirectory = loadedFile.GetPath();
            initialFileName = loadedFile.GetFullName();
        }
    }

    if (initialDirectory.empty()) {
        const auto executablePath = wxStandardPaths::Get().GetExecutablePath();
        if (!executablePath.empty()) {
            wxFileName executableFile(executablePath);
            if (executableFile.IsOk()) {
                initialDirectory = executableFile.GetPath();
            }
        }
    }

    wxFileDialog dialog(this,
                        "Save config XML",
                        initialDirectory,
                        initialFileName,
                        "XML files (*.xml)|*.xml|All files (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }

    wxFileName outputPath(dialog.GetPath());
    if (!outputPath.HasExt()) {
        outputPath.SetExt("xml");
    }

    const auto saveResult = SaveConfigToFile(config_, outputPath.GetFullPath().ToStdString());
    if (!saveResult.success) {
        wxMessageBox(saveResult.errorMessage, "Save failed", wxOK | wxICON_ERROR, this);
        return;
    }

    loadedConfigPath_ = outputPath.GetFullPath().ToStdString();
    AppSettings::Get().SetLastConfigPath(loadedConfigPath_);
    SetStatusText(wxString::Format("Config: %s", loadedConfigPath_));
}

void MainFrame::LoadConfigFromPath(const wxString& path) {
    ConfigLoader loader;
    auto result = loader.LoadFromFile(path.ToStdString());
    if (!result.success) {
        wxMessageBox(result.errorMessage, "Config load failed", wxOK | wxICON_ERROR, this);
        return;
    }

    config_ = std::move(result.config);
    loadedConfigPath_ = path.ToStdString();
    AppSettings::Get().SetLastConfigPath(loadedConfigPath_);
    RenderConfig();
}

void MainFrame::OnApply(wxCommandEvent&) {
    std::vector<DownloadJob> jobs;
    jobs.reserve(config_.components.size());

    static std::uint64_t nextJobId = 1;

    for (std::size_t i = 0; i < config_.components.size(); ++i) {
        const auto& component = config_.components[i];

        if (HasSource(component) && component.source.enabled && !component.source.url.empty()) {
            GitCloneJob sourceJob;
            sourceJob.jobId = nextJobId++;
            sourceJob.componentIndex = i;
            sourceJob.componentName = component.name;
            sourceJob.componentDisplayName = component.displayName;
            sourceJob.repositoryUrl = component.source.url;
            sourceJob.branchOrTag = component.source.branchOrTag;
            sourceJob.targetDirectory = (std::filesystem::path(config_.rootPath) / component.path).string();
            sourceJob.postDownloadScript = component.source.script;
            sourceJob.shallow = component.source.shallow;
            jobs.push_back(DownloadJob::FromSource(std::move(sourceJob)));
        }

        if (HasArtifact(component) && component.artifact.enabled) {
            NexusDownloadJob artifactJob;
            artifactJob.jobId = nextJobId++;
            artifactJob.componentIndex = i;
            artifactJob.componentName = component.name;
            artifactJob.componentDisplayName = component.displayName;
            artifactJob.repositoryUrl = component.artifact.url;
            artifactJob.version = component.artifact.version;
            artifactJob.buildType = component.artifact.buildType;
            artifactJob.targetDirectory = (std::filesystem::path(config_.rootPath) / component.path).string();
            artifactJob.postDownloadScript = component.artifact.script;
            artifactJob.regexIncludes = component.artifact.regexIncludes;
            artifactJob.regexExcludes = component.artifact.regexExcludes;

            jobs.push_back(DownloadJob::FromArtifact(std::move(artifactJob)));
        }
    }

    if (jobs.empty()) {
        wxMessageBox("No source/artifact jobs are enabled.", "Nothing to do", wxOK | wxICON_INFORMATION, this);
        return;
    }

    DownloadProgressDialog dialog(this, std::move(jobs));
    dialog.ShowModal();
}

void MainFrame::OnSelectAll(wxCommandEvent&) {
    if (config_.components.empty()) {
        return;
    }

    uiUpdating_ = true;
    for (std::size_t i = 0; i < config_.components.size(); ++i) {
        auto& component = config_.components[i];
        component.source.enabled = HasSource(component);
        component.artifact.enabled = HasArtifact(component);
        RefreshRowEnabledState(i);
    }
    uiUpdating_ = false;
}

void MainFrame::OnDeselectAll(wxCommandEvent&) {
    if (config_.components.empty()) {
        return;
    }

    uiUpdating_ = true;
    for (std::size_t i = 0; i < config_.components.size(); ++i) {
        config_.components[i].source.enabled = false;
        config_.components[i].artifact.enabled = false;
        RefreshRowEnabledState(i);
    }
    uiUpdating_ = false;
}

void MainFrame::OnUpdateSelectAll(wxUpdateUIEvent& event) {
    event.Enable(!config_.components.empty());
}

void MainFrame::OnUpdateDeselectAll(wxUpdateUIEvent& event) {
    event.Enable(!config_.components.empty());
}

void MainFrame::OnUpdateSaveAs(wxUpdateUIEvent& event) {
    event.Enable(!config_.components.empty());
}

void MainFrame::OnCopyConfig(wxCommandEvent&) {
    const auto summary = BuildHumanReadableConfigSummary(config_);
    if (summary.empty()) {
        wxMessageBox("No enabled components with artifact selections to copy.",
                     "Copy Config",
                     wxOK | wxICON_INFORMATION,
                     this);
        return;
    }

    if (!wxTheClipboard || !wxTheClipboard->Open()) {
        wxMessageBox("Unable to access clipboard.", "Copy Config", wxOK | wxICON_ERROR, this);
        return;
    }

    wxTheClipboard->SetData(new wxTextDataObject(summary));
    wxTheClipboard->Flush();
    wxTheClipboard->Close();
    SetStatusText("Copied configuration summary to clipboard");
}

void MainFrame::OnUpdateCopyConfig(wxUpdateUIEvent& event) {
    event.Enable(!config_.components.empty());
}

void MainFrame::OnToggleDebugConsole(wxCommandEvent&) {
    ToggleDebugConsole(this);
}

void MainFrame::OnUpdateDebugConsole(wxUpdateUIEvent& event) {
    event.Check(IsDebugConsoleVisible());
}

void MainFrame::OnFrameSize(wxSizeEvent& event) {
    event.Skip();
    RelayoutComponentArea();
}

void MainFrame::RelayoutComponentArea() {
    componentContentPanel_->Layout();
    componentScroll_->Layout();
    componentScroll_->FitInside();
}

void MainFrame::RenderConfig() {
    StopMetadataWorkers();
    uiUpdating_ = true;
    componentListSizer_->Clear(true);
    emptyStatePanel_ = nullptr;
    loadConfigButton_ = nullptr;
    loadLastConfigButton_ = nullptr;
    loadFromBitbucketButton_ = nullptr;
    rows_.clear();
    rows_.reserve(config_.components.size());
    metadataState_.clear();
    metadataState_.resize(config_.components.size());
    componentSourceRequests_.clear();
    componentSourceRequests_.resize(config_.components.size());
    componentArtifactRequests_.clear();
    componentArtifactRequests_.resize(config_.components.size());

    for (std::size_t i = 0; i < config_.components.size(); ++i) {
        componentSourceRequests_[i] = config_.components[i].source.url;
        componentArtifactRequests_[i] = {config_.components[i].artifact.url, config_.components[i].name};
        AddComponentRow(i);
    }

    RelayoutComponentArea();
    uiUpdating_ = false;

    statusLabel_->SetLabelText(wxString::Format("Loaded %zu component(s)", config_.components.size()));
    statusLabel_->Show();
    if (loadedConfigPath_.empty()) {
        SetStatusText("Config: none");
    } else {
        SetStatusText(wxString::Format("Config: %s", loadedConfigPath_));
    }
    applyButton_->Enable(!config_.components.empty());
    Layout();

    StartMetadataWorkers();
    for (std::size_t i = 0; i < config_.components.size(); ++i) {
        if (HasSource(config_.components[i]) && !config_.components[i].source.url.empty()) {
            EnqueueSourceRefsFetch(i, false);
        }
        if (HasArtifact(config_.components[i]) && !config_.components[i].artifact.url.empty()) {
            EnqueueVersionFetch(i, false);
        }
    }
}

void MainFrame::AddComponentRow(std::size_t componentIndex) {
    auto& component = config_.components[componentIndex];

    const auto displayName = component.displayName;
    auto* rowBox = new wxStaticBoxSizer(wxVERTICAL,
                                        componentContentPanel_,
                                        wxString::Format("%s  (%s)", displayName, component.path));
    auto* rowParent = rowBox->GetStaticBox();

    auto* detailsSizer = new wxBoxSizer(wxVERTICAL);

    auto makeFixedLabel = [rowParent](const wxString& text, int width) {
        auto* label = new wxStaticText(rowParent, wxID_ANY, text);
        label->SetMinSize(wxSize(width, -1));
        return label;
    };

    auto* sourceRow = new wxBoxSizer(wxHORIZONTAL);
    sourceRow->Add(makeFixedLabel("Source", kSectionLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* sourceEnabled = new wxCheckBox(rowParent, wxID_ANY, "Enable");
    sourceRow->Add(sourceEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    auto* sourceShallow = new wxCheckBox(rowParent, wxID_ANY, "Shallow");
    sourceRow->Add(sourceShallow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    sourceRow->Add(makeFixedLabel("Branch/Tag", kFieldLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* sourceBranch = new wxComboBox(rowParent, wxID_ANY);
    sourceRow->Add(sourceBranch, 1, wxRIGHT | wxEXPAND, 0);
    detailsSizer->Add(sourceRow, 0, wxEXPAND | wxBOTTOM, 6);

    auto* artifactRow = new wxBoxSizer(wxHORIZONTAL);
    artifactRow->Add(makeFixedLabel("Artifact", kSectionLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* artifactEnabled = new wxCheckBox(rowParent, wxID_ANY, "Enable");
    artifactRow->Add(artifactEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    artifactRow->Add(makeFixedLabel("Version", kFieldLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* artifactVersion = new wxComboBox(rowParent, wxID_ANY);
    artifactRow->Add(artifactVersion, 1, wxRIGHT | wxEXPAND, 10);
    artifactRow->Add(makeFixedLabel("Build", 44), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* artifactBuildType = new wxComboBox(rowParent, wxID_ANY);
    artifactRow->Add(artifactBuildType, 1, wxRIGHT | wxEXPAND, 0);
    detailsSizer->Add(artifactRow, 0, wxEXPAND);

    rowBox->Add(detailsSizer, 1, wxEXPAND);
    componentListSizer_->Add(rowBox, 0, wxBOTTOM | wxEXPAND, 6);

    ComponentRowWidgets row;
    row.sourceEnabled = sourceEnabled;
    row.sourceShallow = sourceShallow;
    row.sourceBranch = sourceBranch;
    row.artifactEnabled = artifactEnabled;
    row.artifactVersion = artifactVersion;
    row.artifactBuildType = artifactBuildType;
    rows_.push_back(row);

    if (!component.source.branchOrTag.empty()) {
        row.sourceBranch->Append(component.source.branchOrTag);
    }
    row.sourceBranch->SetValue(component.source.branchOrTag);
    row.sourceShallow->SetValue(component.source.shallow);

    if (!component.artifact.version.empty()) {
        row.artifactVersion->Append(component.artifact.version);
    }
    row.artifactVersion->SetValue(component.artifact.version);

    if (!component.artifact.buildType.empty()) {
        row.artifactBuildType->Append(component.artifact.buildType);
    }
    row.artifactBuildType->SetValue(component.artifact.buildType);
    UpdateRowTooltips(componentIndex);

    row.sourceEnabled->Bind(wxEVT_CHECKBOX, [this, componentIndex](wxCommandEvent&) {
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].source.enabled = rows_[componentIndex].sourceEnabled->GetValue();
        RefreshRowEnabledState(componentIndex);
    });

    row.sourceShallow->Bind(wxEVT_CHECKBOX, [this, componentIndex](wxCommandEvent&) {
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].source.shallow = rows_[componentIndex].sourceShallow->GetValue();
    });

    row.artifactEnabled->Bind(wxEVT_CHECKBOX, [this, componentIndex](wxCommandEvent&) {
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].artifact.enabled = rows_[componentIndex].artifactEnabled->GetValue();
        RefreshRowEnabledState(componentIndex);
    });

    row.sourceBranch->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent&) {
        UpdateComboTooltip(*rows_[componentIndex].sourceBranch);
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].source.branchOrTag =
            rows_[componentIndex].sourceBranch->GetValue().ToStdString();
    });
    row.sourceBranch->Bind(wxEVT_COMBOBOX_DROPDOWN, [this, componentIndex](wxCommandEvent&) {
        if (uiUpdating_) {
            return;
        }
        EnqueueSourceRefsFetch(componentIndex, true);
    });

    row.artifactVersion->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent&) {
        UpdateComboTooltip(*rows_[componentIndex].artifactVersion);
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].artifact.version =
            rows_[componentIndex].artifactVersion->GetValue().ToStdString();
    });
    row.artifactVersion->Bind(wxEVT_COMBOBOX, [this, componentIndex](wxCommandEvent&) {
        UpdateComboTooltip(*rows_[componentIndex].artifactVersion);
        if (uiUpdating_) {
            return;
        }
        const auto version = rows_[componentIndex].artifactVersion->GetValue().ToStdString();
        config_.components[componentIndex].artifact.version = version;
        EnqueueBuildTypeFetch(componentIndex, version);
    });
    row.artifactVersion->Bind(wxEVT_COMBOBOX_DROPDOWN, [this, componentIndex](wxCommandEvent&) {
        if (uiUpdating_) {
            return;
        }
        EnqueueVersionFetch(componentIndex, true);
    });

    row.artifactBuildType->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent&) {
        UpdateComboTooltip(*rows_[componentIndex].artifactBuildType);
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].artifact.buildType =
            rows_[componentIndex].artifactBuildType->GetValue().ToStdString();
    });

    RefreshRowEnabledState(componentIndex);
}

void MainFrame::UpdateComboTooltip(wxComboBox& comboBox) {
    const auto value = comboBox.GetValue();
    if (value.empty()) {
        comboBox.UnsetToolTip();
        return;
    }

    comboBox.SetToolTip(value);
}

void MainFrame::UpdateRowTooltips(std::size_t componentIndex) {
    if (componentIndex >= rows_.size()) {
        return;
    }

    const auto& row = rows_[componentIndex];
    UpdateComboTooltip(*row.sourceBranch);
    UpdateComboTooltip(*row.artifactVersion);
    UpdateComboTooltip(*row.artifactBuildType);
}

void MainFrame::RefreshRowEnabledState(std::size_t componentIndex) {
    if (componentIndex >= config_.components.size() || componentIndex >= rows_.size()) {
        return;
    }

    const auto& component = config_.components[componentIndex];
    auto& row = rows_[componentIndex];

    const bool sourceExists = HasSource(component);
    const bool artifactExists = HasArtifact(component);

    row.sourceEnabled->Enable(sourceExists);
    row.sourceEnabled->SetValue(sourceExists && component.source.enabled);
    row.sourceShallow->Enable(sourceExists && component.source.enabled);
    row.sourceShallow->SetValue(component.source.shallow);
    row.sourceBranch->Enable(sourceExists && component.source.enabled);

    row.artifactEnabled->Enable(artifactExists);
    row.artifactEnabled->SetValue(artifactExists && component.artifact.enabled);
    row.artifactVersion->Enable(artifactExists && component.artifact.enabled);
    row.artifactBuildType->Enable(artifactExists && component.artifact.enabled);
}

void MainFrame::EnqueueSourceRefsFetch(std::size_t componentIndex, bool prioritize) {
    if (componentIndex >= config_.components.size() || componentIndex >= metadataState_.size()) {
        return;
    }
    if (!HasSource(config_.components[componentIndex]) || config_.components[componentIndex].source.url.empty()) {
        return;
    }
    if (metadataState_[componentIndex].sourceRefsLoaded && !prioritize) {
        return;
    }

    MetadataTask task;
    task.type = MetadataTaskType::SourceRefs;
    task.componentIndex = componentIndex;
    const auto key = "s:" + std::to_string(componentIndex);

    {
        std::scoped_lock lock(metadataMutex_);
        if (metadataTaskKeys_.find(key) != metadataTaskKeys_.end()) {
            if (!prioritize) {
                return;
            }
            for (auto it = metadataTasks_.begin(); it != metadataTasks_.end(); ++it) {
                if (it->type == MetadataTaskType::SourceRefs && it->componentIndex == componentIndex) {
                    std::rotate(metadataTasks_.begin(), it, it + 1);
                    break;
                }
            }
            metadataCv_.notify_one();
            return;
        }
        if (prioritize) {
            metadataTasks_.push_front(task);
        } else {
            metadataTasks_.push_back(task);
        }
        metadataTaskKeys_.insert(key);
    }
    metadataCv_.notify_one();
}

void MainFrame::StartMetadataWorkers() {
    std::scoped_lock lock(metadataMutex_);
    if (!metadataWorkers_.empty()) {
        return;
    }
    stopMetadataWorkers_ = false;
    metadataWorkers_.emplace_back(&MainFrame::MetadataWorkerLoop, this);
    metadataWorkers_.emplace_back(&MainFrame::MetadataWorkerLoop, this);
}

void MainFrame::StopMetadataWorkers() {
    {
        std::scoped_lock lock(metadataMutex_);
        stopMetadataWorkers_ = true;
        metadataTasks_.clear();
        metadataTaskKeys_.clear();
    }
    metadataCv_.notify_all();
    for (auto& worker : metadataWorkers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    metadataWorkers_.clear();
}

void MainFrame::EnqueueVersionFetch(std::size_t componentIndex, bool prioritize) {
    if (componentIndex >= config_.components.size() || componentIndex >= metadataState_.size()) {
        return;
    }
    if (!HasArtifact(config_.components[componentIndex]) || config_.components[componentIndex].artifact.url.empty()) {
        return;
    }
    if (metadataState_[componentIndex].versionsLoaded) {
        return;
    }

    MetadataTask task;
    task.type = MetadataTaskType::Versions;
    task.componentIndex = componentIndex;
    const auto key = "v:" + std::to_string(componentIndex);

    {
        std::scoped_lock lock(metadataMutex_);
        if (metadataTaskKeys_.find(key) != metadataTaskKeys_.end()) {
            if (!prioritize) {
                return;
            }
            for (auto it = metadataTasks_.begin(); it != metadataTasks_.end(); ++it) {
                if (it->type == MetadataTaskType::Versions && it->componentIndex == componentIndex) {
                    std::rotate(metadataTasks_.begin(), it, it + 1);
                    break;
                }
            }
            metadataCv_.notify_one();
            return;
        }
        if (prioritize) {
            metadataTasks_.push_front(task);
        } else {
            metadataTasks_.push_back(task);
        }
        metadataTaskKeys_.insert(key);
    }
    metadataCv_.notify_one();
}

void MainFrame::EnqueueBuildTypeFetch(std::size_t componentIndex, const std::string& version) {
    if (componentIndex >= config_.components.size() || componentIndex >= metadataState_.size() || version.empty()) {
        return;
    }
    if (!HasArtifact(config_.components[componentIndex]) || config_.components[componentIndex].artifact.url.empty()) {
        return;
    }
    auto& state = metadataState_[componentIndex];
    if (state.buildTypesByVersion.find(version) != state.buildTypesByVersion.end() ||
        state.buildTypesLoadingVersions.find(version) != state.buildTypesLoadingVersions.end()) {
        return;
    }

    MetadataTask task;
    task.type = MetadataTaskType::BuildTypes;
    task.componentIndex = componentIndex;
    task.version = version;
    const auto key = "b:" + std::to_string(componentIndex) + ":" + version;
    {
        std::scoped_lock lock(metadataMutex_);
        if (metadataTaskKeys_.find(key) != metadataTaskKeys_.end()) {
            return;
        }
        metadataTasks_.push_front(task);
        metadataTaskKeys_.insert(key);
    }
    metadataCv_.notify_one();
}

void MainFrame::MetadataWorkerLoop() {
#ifdef _WIN32
    std::string homeDir;
    if (const char* h = std::getenv("USERPROFILE")) {
        homeDir = h;
    } else {
        const char* drive = std::getenv("HOMEDRIVE");
        const char* homepath = std::getenv("HOMEPATH");
        if (drive && homepath) {
            homeDir = std::string(drive) + homepath;
        }
    }
#else
    const char* homeEnv = std::getenv("HOME");
    const std::string homeDir = homeEnv ? homeEnv : "";
#endif
    const std::string settingsPath = homeDir.empty() ? "" : (std::filesystem::path(homeDir) / ".m2" / "settings.xml").string();

    while (true) {
        MetadataTask task;
        {
            std::unique_lock lock(metadataMutex_);
            metadataCv_.wait(lock, [this]() { return stopMetadataWorkers_ || !metadataTasks_.empty(); });
            if (stopMetadataWorkers_) {
                return;
            }
            task = metadataTasks_.front();
            metadataTasks_.pop_front();
            if (task.type == MetadataTaskType::SourceRefs) {
                metadataTaskKeys_.erase("s:" + std::to_string(task.componentIndex));
            } else if (task.type == MetadataTaskType::Versions) {
                metadataTaskKeys_.erase("v:" + std::to_string(task.componentIndex));
            } else {
                metadataTaskKeys_.erase("b:" + std::to_string(task.componentIndex) + ":" + task.version);
            }
        }

        if (task.type == MetadataTaskType::SourceRefs && task.componentIndex >= componentSourceRequests_.size()) {
            continue;
        }
        std::pair<std::string, std::string> request;
        if (task.type != MetadataTaskType::SourceRefs) {
            if (task.componentIndex >= componentArtifactRequests_.size()) {
                continue;
            }
            request = componentArtifactRequests_[task.componentIndex];
            if (request.first.empty() || request.second.empty()) {
                continue;
            }
        }

        if (task.componentIndex >= metadataState_.size()) {
            continue;
        }

        if (task.type == MetadataTaskType::SourceRefs) {
            CallAfter([this, index = task.componentIndex]() {
                if (index < metadataState_.size()) {
                    metadataState_[index].sourceRefsLoading = true;
                }
            });
        } else if (task.type == MetadataTaskType::Versions) {
            CallAfter([this, index = task.componentIndex]() {
                if (index < metadataState_.size()) {
                    metadataState_[index].versionsLoading = true;
                }
            });
        } else {
            CallAfter([this, index = task.componentIndex, version = task.version]() {
                if (index < metadataState_.size()) {
                    metadataState_[index].buildTypesLoadingVersions.insert(version);
                }
            });
        }

        if (settingsPath.empty()) {
            if (task.type == MetadataTaskType::SourceRefs) {
                CallAfter([this, index = task.componentIndex]() {
                    if (index < metadataState_.size()) {
                        metadataState_[index].sourceRefsLoading = false;
                    }
                });
            } else if (task.type == MetadataTaskType::Versions) {
                CallAfter([this, index = task.componentIndex]() {
                    if (index < metadataState_.size()) {
                        metadataState_[index].versionsLoading = false;
                    }
                });
            } else {
                CallAfter([this, index = task.componentIndex, version = task.version]() {
                    if (index < metadataState_.size()) {
                        metadataState_[index].buildTypesLoadingVersions.erase(version);
                    }
                });
            }
            continue;
        }

        AuthCredentials credentials;
        std::string authError;
        if (!credentials.LoadFromM2SettingsXml(settingsPath, authError)) {
            if (task.type == MetadataTaskType::SourceRefs) {
                CallAfter([this, index = task.componentIndex]() {
                    if (index < metadataState_.size()) {
                        metadataState_[index].sourceRefsLoading = false;
                    }
                });
            }
            continue;
        }

        if (task.type == MetadataTaskType::SourceRefs) {
            std::vector<std::string> refs;
            std::string errorMessage;
            GitClient client(std::move(credentials));
            const auto ok = client.ListBranchesAndTags(componentSourceRequests_[task.componentIndex], refs, errorMessage);
            CallAfter([this, index = task.componentIndex, refs = std::move(refs), ok]() mutable {
                if (index >= metadataState_.size() || index >= rows_.size() || index >= config_.components.size()) {
                    return;
                }

                auto& state = metadataState_[index];
                state.sourceRefsLoading = false;
                state.sourceRefsLoaded = ok;
                if (!ok) {
                    return;
                }

                auto* sourceBranch = rows_[index].sourceBranch;
                const auto previousSelection = sourceBranch->GetValue().ToStdString();

                uiUpdating_ = true;
                sourceBranch->Clear();
                for (const auto& ref : refs) {
                    sourceBranch->Append(ref);
                }
                if (!previousSelection.empty()) {
                    sourceBranch->SetValue(previousSelection);
                } else if (!refs.empty()) {
                    sourceBranch->SetValue(refs.front());
                    config_.components[index].source.branchOrTag = refs.front();
                }
                uiUpdating_ = false;
                UpdateComboTooltip(*sourceBranch);
            });
            continue;
        }

        NexusClient client(std::move(credentials));
        std::string errorMessage;
        if (task.type == MetadataTaskType::Versions) {
            std::vector<std::string> versions;
            const auto ok = client.ListComponentVersions(
                request.first, request.second, versions, errorMessage);
            CallAfter([this, index = task.componentIndex, versions = std::move(versions), ok]() mutable {
                if (index >= metadataState_.size() || index >= rows_.size() || index >= config_.components.size()) {
                    return;
                }

                auto& state = metadataState_[index];
                state.versionsLoading = false;
                state.versionsLoaded = ok;
                if (!ok) {
                    return;
                }

                auto& versionBox = rows_[index].artifactVersion;
                const auto previousSelection = versionBox->GetValue().ToStdString();
                uiUpdating_ = true;
                versionBox->Clear();
                for (const auto& version : versions) {
                    versionBox->Append(version);
                }
                if (!previousSelection.empty()) {
                    versionBox->SetValue(previousSelection);
                } else if (!versions.empty()) {
                    versionBox->SetValue(versions.front());
                    config_.components[index].artifact.version = versions.front();
                }
                uiUpdating_ = false;
                UpdateComboTooltip(*versionBox);

                if (!config_.components[index].artifact.version.empty()) {
                    EnqueueBuildTypeFetch(index, config_.components[index].artifact.version);
                }
            });
            continue;
        }

        std::vector<std::string> buildTypes;
        const auto ok = client.ListBuildTypes(
            request.first, request.second, task.version, buildTypes, errorMessage);
        CallAfter([this,
                   index = task.componentIndex,
                   version = task.version,
                   buildTypes = std::move(buildTypes),
                   ok]() mutable {
            if (index >= metadataState_.size() || index >= rows_.size() || index >= config_.components.size()) {
                return;
            }

            auto& state = metadataState_[index];
            state.buildTypesLoadingVersions.erase(version);
            if (!ok) {
                return;
            }
            state.buildTypesByVersion[version] = buildTypes;

            if (config_.components[index].artifact.version != version) {
                return;
            }

            auto& buildTypeBox = rows_[index].artifactBuildType;
            const auto previousSelection = buildTypeBox->GetValue().ToStdString();
            uiUpdating_ = true;
            buildTypeBox->Clear();
            for (const auto& buildType : buildTypes) {
                buildTypeBox->Append(buildType);
            }
            if (!previousSelection.empty()) {
                buildTypeBox->SetValue(previousSelection);
            } else if (!buildTypes.empty()) {
                buildTypeBox->SetValue(buildTypes.front());
                config_.components[index].artifact.buildType = buildTypes.front();
            }
            uiUpdating_ = false;
            UpdateComboTooltip(*buildTypeBox);
        });
    }
}

}  // namespace confy
