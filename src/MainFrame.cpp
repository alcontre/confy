#include "MainFrame.h"

#include "AuthCredentials.h"
#include "ConfigLoader.h"
#include "DebugConsole.h"
#include "DownloadProgressDialog.h"
#include "NexusClient.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>

namespace {

constexpr int kIdLoadConfig = wxID_HIGHEST + 1;
constexpr int kIdApply = wxID_HIGHEST + 2;
constexpr int kIdDeselectAll = wxID_HIGHEST + 3;
constexpr int kIdViewDebugConsole = wxID_HIGHEST + 4;
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

}  // namespace

namespace confy {

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Confy", wxDefaultPosition, wxSize(900, 600)) {
    CreateStatusBar(1);
    SetStatusText("Config: none");

    auto* fileMenu = new wxMenu();
    fileMenu->Append(kIdLoadConfig, "&Load Config...\tCtrl+O");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit");

    auto* editMenu = new wxMenu();
    editMenu->Append(wxID_SELECTALL, "Select &All");
    editMenu->Append(kIdDeselectAll, "&Deselect All");

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

    componentListSizer_ = new wxBoxSizer(wxVERTICAL);

    emptyStatePanel_ = new wxPanel(componentScroll_);
    auto* emptyStateSizer = new wxBoxSizer(wxVERTICAL);
    emptyStateSizer->AddStretchSpacer();

    loadConfigButton_ = new wxButton(emptyStatePanel_, kIdLoadConfig, "Load config XML");
    auto buttonFont = loadConfigButton_->GetFont();
    buttonFont.MakeLarger();
    loadConfigButton_->SetFont(buttonFont);
    loadConfigButton_->SetMinSize(wxSize(280, 72));
    emptyStateSizer->Add(loadConfigButton_, 0, wxALIGN_CENTER_HORIZONTAL);
    emptyStateSizer->AddStretchSpacer();
    emptyStatePanel_->SetSizer(emptyStateSizer);

    componentListSizer_->Add(emptyStatePanel_, 1, wxEXPAND);
    componentScroll_->SetSizer(componentListSizer_);

    rootSizer->Add(componentScroll_, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    applyButton_ = new wxButton(this, kIdApply, "Apply");
    applyButton_->Disable();
    rootSizer->Add(applyButton_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 8);

    SetSizer(rootSizer);

    Bind(wxEVT_MENU, &MainFrame::OnLoadConfig, this, kIdLoadConfig);
    Bind(wxEVT_BUTTON, &MainFrame::OnLoadConfig, this, kIdLoadConfig);
    Bind(wxEVT_MENU, &MainFrame::OnSelectAll, this, wxID_SELECTALL);
    Bind(wxEVT_MENU, &MainFrame::OnDeselectAll, this, kIdDeselectAll);
    Bind(wxEVT_MENU, &MainFrame::OnToggleDebugConsole, this, kIdViewDebugConsole);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateSelectAll, this, wxID_SELECTALL);
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateDeselectAll, this, kIdDeselectAll);
    Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateDebugConsole, this, kIdViewDebugConsole);
    Bind(wxEVT_BUTTON, &MainFrame::OnApply, this, kIdApply);
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

    ConfigLoader loader;
    auto result = loader.LoadFromFile(dialog.GetPath().ToStdString());
    if (!result.success) {
        wxMessageBox(result.errorMessage, "Config load failed", wxOK | wxICON_ERROR, this);
        return;
    }

    config_ = std::move(result.config);
    loadedConfigPath_ = dialog.GetPath().ToStdString();
    RenderConfig();
}

void MainFrame::OnApply(wxCommandEvent&) {
    std::vector<NexusDownloadJob> jobs;
    jobs.reserve(config_.components.size());

    static std::uint64_t nextJobId = 1;

    for (std::size_t i = 0; i < config_.components.size(); ++i) {
        const auto& component = config_.components[i];
        if (!HasArtifact(component) || !component.artifact.enabled) {
            continue;
        }

        NexusDownloadJob job;
        job.jobId = nextJobId++;
        job.componentIndex = i;
        job.componentName = component.name;
        job.componentDisplayName = component.displayName;
        job.repositoryUrl = component.artifact.url;
        job.version = component.artifact.version;
        job.buildType = component.artifact.buildType;
        job.targetDirectory = (std::filesystem::path(config_.rootPath) / component.path).string();
        job.regexIncludes = component.artifact.regexIncludes;
        job.regexExcludes = component.artifact.regexExcludes;

        jobs.push_back(std::move(job));
    }

    if (jobs.empty()) {
        wxMessageBox("No artifact download jobs are enabled.", "Nothing to download", wxOK | wxICON_INFORMATION, this);
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

void MainFrame::OnToggleDebugConsole(wxCommandEvent&) {
    ToggleDebugConsole(this);
}

void MainFrame::OnUpdateDebugConsole(wxUpdateUIEvent& event) {
    event.Check(IsDebugConsoleVisible());
}

void MainFrame::RenderConfig() {
    StopMetadataWorkers();
    uiUpdating_ = true;
    componentListSizer_->Clear(true);
    emptyStatePanel_ = nullptr;
    loadConfigButton_ = nullptr;
    rows_.clear();
    rows_.reserve(config_.components.size());
    metadataState_.clear();
    metadataState_.resize(config_.components.size());
    componentArtifactRequests_.clear();
    componentArtifactRequests_.resize(config_.components.size());

    for (std::size_t i = 0; i < config_.components.size(); ++i) {
        componentArtifactRequests_[i] = {config_.components[i].artifact.url, config_.components[i].name};
        AddComponentRow(i);
    }

    componentScroll_->Layout();
    componentScroll_->FitInside();
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
        if (HasArtifact(config_.components[i]) && !config_.components[i].artifact.url.empty()) {
            EnqueueVersionFetch(i, false);
        }
    }
}

void MainFrame::AddComponentRow(std::size_t componentIndex) {
    auto& component = config_.components[componentIndex];

    const auto displayName = component.displayName;
    auto* rowBox = new wxStaticBoxSizer(wxVERTICAL,
                                        componentScroll_,
                                        wxString::Format("%s  (%s)", displayName, component.path));

    auto* detailsSizer = new wxBoxSizer(wxVERTICAL);

    auto makeFixedLabel = [this](const wxString& text, int width) {
        auto* label = new wxStaticText(componentScroll_, wxID_ANY, text);
        label->SetMinSize(wxSize(width, -1));
        return label;
    };

    auto* sourceRow = new wxBoxSizer(wxHORIZONTAL);
    sourceRow->Add(makeFixedLabel("Source", kSectionLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* sourceEnabled = new wxCheckBox(componentScroll_, wxID_ANY, "Enable");
    sourceRow->Add(sourceEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    sourceRow->Add(makeFixedLabel("Branch/Tag", kFieldLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* sourceBranch = new wxComboBox(componentScroll_, wxID_ANY);
    sourceRow->Add(sourceBranch, 1, wxRIGHT | wxEXPAND, 0);
    detailsSizer->Add(sourceRow, 0, wxEXPAND | wxBOTTOM, 6);

    auto* artifactRow = new wxBoxSizer(wxHORIZONTAL);
    artifactRow->Add(makeFixedLabel("Artifact", kSectionLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* artifactEnabled = new wxCheckBox(componentScroll_, wxID_ANY, "Enable");
    artifactRow->Add(artifactEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    artifactRow->Add(makeFixedLabel("Version", kFieldLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* artifactVersion = new wxComboBox(componentScroll_, wxID_ANY);
    artifactRow->Add(artifactVersion, 1, wxRIGHT | wxEXPAND, 10);
    artifactRow->Add(makeFixedLabel("Build", 44), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* artifactBuildType = new wxComboBox(componentScroll_, wxID_ANY);
    artifactRow->Add(artifactBuildType, 1, wxRIGHT | wxEXPAND, 0);
    detailsSizer->Add(artifactRow, 0, wxEXPAND);

    rowBox->Add(detailsSizer, 1, wxEXPAND);
    componentListSizer_->Add(rowBox, 0, wxBOTTOM | wxEXPAND, 6);

    ComponentRowWidgets row;
    row.sourceEnabled = sourceEnabled;
    row.sourceBranch = sourceBranch;
    row.artifactEnabled = artifactEnabled;
    row.artifactVersion = artifactVersion;
    row.artifactBuildType = artifactBuildType;
    rows_.push_back(row);

    if (!component.source.branchOrTag.empty()) {
        row.sourceBranch->Append(component.source.branchOrTag);
    }
    row.sourceBranch->SetValue(component.source.branchOrTag);

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

    row.artifactEnabled->Bind(wxEVT_CHECKBOX, [this, componentIndex](wxCommandEvent&) {
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].artifact.enabled = rows_[componentIndex].artifactEnabled->GetValue();
        RefreshRowEnabledState(componentIndex);
    });

    row.sourceBranch->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent&) {
        UpdateComboTooltip(rows_[componentIndex].sourceBranch);
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].source.branchOrTag =
            rows_[componentIndex].sourceBranch->GetValue().ToStdString();
    });

    row.artifactVersion->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent&) {
        UpdateComboTooltip(rows_[componentIndex].artifactVersion);
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].artifact.version =
            rows_[componentIndex].artifactVersion->GetValue().ToStdString();
    });
    row.artifactVersion->Bind(wxEVT_COMBOBOX, [this, componentIndex](wxCommandEvent&) {
        UpdateComboTooltip(rows_[componentIndex].artifactVersion);
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
        UpdateComboTooltip(rows_[componentIndex].artifactBuildType);
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].artifact.buildType =
            rows_[componentIndex].artifactBuildType->GetValue().ToStdString();
    });

    RefreshRowEnabledState(componentIndex);
}

void MainFrame::UpdateComboTooltip(wxComboBox* comboBox) {
    if (comboBox == nullptr) {
        return;
    }

    const auto value = comboBox->GetValue();
    if (value.empty()) {
        comboBox->UnsetToolTip();
        return;
    }

    comboBox->SetToolTip(value);
}

void MainFrame::UpdateRowTooltips(std::size_t componentIndex) {
    if (componentIndex >= rows_.size()) {
        return;
    }

    const auto& row = rows_[componentIndex];
    UpdateComboTooltip(row.sourceBranch);
    UpdateComboTooltip(row.artifactVersion);
    UpdateComboTooltip(row.artifactBuildType);
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
    row.sourceBranch->Enable(sourceExists && component.source.enabled);

    row.artifactEnabled->Enable(artifactExists);
    row.artifactEnabled->SetValue(artifactExists && component.artifact.enabled);
    row.artifactVersion->Enable(artifactExists && component.artifact.enabled);
    row.artifactBuildType->Enable(artifactExists && component.artifact.enabled);
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
            if (task.type == MetadataTaskType::Versions) {
                metadataTaskKeys_.erase("v:" + std::to_string(task.componentIndex));
            } else {
                metadataTaskKeys_.erase("b:" + std::to_string(task.componentIndex) + ":" + task.version);
            }
        }

        if (task.componentIndex >= componentArtifactRequests_.size()) {
            continue;
        }
        const auto request = componentArtifactRequests_[task.componentIndex];
        if (request.first.empty() || request.second.empty()) {
            continue;
        }

        if (task.componentIndex >= metadataState_.size()) {
            continue;
        }

        if (task.type == MetadataTaskType::Versions) {
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
            continue;
        }

        AuthCredentials credentials;
        std::string authError;
        if (!credentials.LoadFromM2SettingsXml(settingsPath, authError)) {
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
                UpdateComboTooltip(versionBox);

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
            UpdateComboTooltip(buildTypeBox);
        });
    }
}

}  // namespace confy
