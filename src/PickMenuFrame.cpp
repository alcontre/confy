#include "PickMenuFrame.h"

#include "AppInfo.h"
#include "AppSettings.h"
#include "AuthCredentials.h"
#include "BitbucketClient.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>

namespace {

constexpr int kIdLoadFile          = wxID_HIGHEST + 101;
constexpr int kIdLoadLast          = wxID_HIGHEST + 102;
constexpr int kIdLoadFromBitbucket = wxID_HIGHEST + 103;
constexpr int kPickerButtonWidth   = 280;
constexpr int kPickerButtonHeight  = 56;
constexpr int kPickerButtonGap     = 12;

std::string ResolveM2SettingsPath()
{
#ifdef _WIN32
   std::string homeDir;
   if (const char *h = std::getenv("USERPROFILE")) {
      homeDir = h;
   } else {
      const char *drive    = std::getenv("HOMEDRIVE");
      const char *homePath = std::getenv("HOMEPATH");
      if (drive && homePath) {
         homeDir = std::string(drive) + homePath;
      }
   }
#else
   const char *homeEnv       = std::getenv("HOME");
   const std::string homeDir = homeEnv ? homeEnv : "";
#endif

   if (homeDir.empty()) {
      return {};
   }
   return (std::filesystem::path(homeDir) / ".m2" / "settings.xml").string();
}

class BitbucketLoadDialog final : public wxDialog
{
 public:
   BitbucketLoadDialog(wxWindow *parent,
       confy::BitbucketClient &client,
       const std::string &repoUrl) :
       wxDialog(parent, wxID_ANY, "Load from Bitbucket", wxDefaultPosition, wxSize(680, 460)),
       client_(client),
       repoUrl_(repoUrl)
   {
      auto *root = new wxBoxSizer(wxVERTICAL);
      root->Add(new wxStaticText(this, wxID_ANY, wxString::Format("Repo: %s", repoUrl_)),
          0,
          wxLEFT | wxRIGHT | wxTOP | wxEXPAND,
          10);

      auto *branchRow = new wxBoxSizer(wxHORIZONTAL);
      branchRow->Add(new wxStaticText(this, wxID_ANY, "Branch"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      branchChoice_ = new wxChoice(this, wxID_ANY);
      branchRow->Add(branchChoice_, 1, wxEXPAND);
      refreshButton_ = new wxButton(this, wxID_ANY, "Refresh");
      branchRow->Add(refreshButton_, 0, wxLEFT, 8);
      root->Add(branchRow, 0, wxALL | wxEXPAND, 10);

      auto *filesRow = new wxBoxSizer(wxHORIZONTAL);
      filesRow->Add(new wxStaticText(this, wxID_ANY, "XML files"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      filesRow->AddStretchSpacer();
      sortButton_ = new wxButton(this, wxID_ANY, "");
      filesRow->Add(sortButton_, 0);
      root->Add(filesRow, 0, wxLEFT | wxRIGHT, 10);
      fileList_ = new wxListBox(this, wxID_ANY);
      root->Add(fileList_, 1, wxALL | wxEXPAND, 10);

      statusLabel_ = new wxStaticText(this, wxID_ANY, "Loading branches from Bitbucket...");
      root->Add(statusLabel_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

      auto *buttons = new wxStdDialogButtonSizer();
      okButton_     = new wxButton(this, wxID_OK, "Load");
      okButton_->Disable();
      buttons->AddButton(okButton_);
      buttons->AddButton(new wxButton(this, wxID_CANCEL, "Cancel"));
      buttons->Realize();
      root->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 10);

      SetSizerAndFit(root);
      SetMinSize(wxSize(680, 460));

      refreshButton_->Bind(wxEVT_BUTTON, &BitbucketLoadDialog::OnRefresh, this);
      branchChoice_->Bind(wxEVT_CHOICE, &BitbucketLoadDialog::OnBranchChanged, this);
      fileList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { UpdateOkEnabled(); });
      sortButton_->Bind(wxEVT_BUTTON, &BitbucketLoadDialog::OnToggleSort, this);
      okButton_->Bind(wxEVT_BUTTON, &BitbucketLoadDialog::OnOk, this);
      UpdateSortButtonLabel();

      StartLoadBranchesAndDefaultFiles();
   }

   ~BitbucketLoadDialog() override
   {
      aliveFlag_->store(false);
   }

   std::string SelectedBranch() const
   {
      return selectedBranch_;
   }

   std::string SelectedFile() const
   {
      return selectedFile_;
   }

 private:
   void StartLoadBranchesAndDefaultFiles()
   {
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
            for (const auto &branch : branches) {
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

   void StartRefreshFilesForSelectedBranch()
   {
      const auto branch = branchChoice_->GetStringSelection().ToStdString();
      if (branch.empty()) {
         currentFiles_.clear();
         fileList_->Clear();
         UpdateOkEnabled();
         return;
      }

      const auto requestId = requestId_.fetch_add(1) + 1;
      currentFiles_.clear();
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

            currentFiles_ = std::move(files);
            RenderFileList();

            SetBusyState(false, "");
            UpdateOkEnabled();
            statusLabel_->SetLabelText(wxString::Format("Found %u XML file(s) in branch '%s'.",
                static_cast<unsigned int>(currentFiles_.size()),
                branch));
         });
      }).detach();
   }

   void OnRefresh(wxCommandEvent &)
   {
      StartLoadBranchesAndDefaultFiles();
   }

   void OnBranchChanged(wxCommandEvent &)
   {
      StartRefreshFilesForSelectedBranch();
   }

   void OnToggleSort(wxCommandEvent &)
   {
      const auto selected = fileList_->GetSelection() != wxNOT_FOUND
                                ? fileList_->GetStringSelection().ToStdString()
                                : std::string();
      sortAscending_      = !sortAscending_;
      UpdateSortButtonLabel();
      RenderFileList(selected);
      UpdateOkEnabled();
   }

   void OnOk(wxCommandEvent &event)
   {
      if (branchChoice_->GetSelection() == wxNOT_FOUND || fileList_->GetSelection() == wxNOT_FOUND) {
         event.Skip();
         return;
      }

      selectedBranch_ = branchChoice_->GetStringSelection().ToStdString();
      selectedFile_   = fileList_->GetStringSelection().ToStdString();
      event.Skip();
   }

   void SetBusyState(bool busy, const wxString &message)
   {
      busy_ = busy;
      branchChoice_->Enable(!busy);
      fileList_->Enable(!busy);
      refreshButton_->Enable(!busy);
      sortButton_->Enable(!busy && !currentFiles_.empty());
      okButton_->Enable(!busy && fileList_->GetSelection() != wxNOT_FOUND &&
                        branchChoice_->GetSelection() != wxNOT_FOUND);
      if (busy) {
         statusLabel_->SetLabelText(message);
      }
   }

   void UpdateOkEnabled()
   {
      sortButton_->Enable(!busy_ && !currentFiles_.empty());
      okButton_->Enable(!busy_ && fileList_->GetSelection() != wxNOT_FOUND &&
                        branchChoice_->GetSelection() != wxNOT_FOUND);
   }

   void UpdateSortButtonLabel()
   {
      sortButton_->SetLabel(sortAscending_ ? "Sort Asc" : "Sort Desc");
      sortButton_->SetToolTip(sortAscending_ ? "Ascending order" : "Descending order");
   }

   void RenderFileList(const std::string &preferredSelection = {})
   {
      std::vector<std::string> sortedFiles = currentFiles_;
      std::sort(sortedFiles.begin(), sortedFiles.end());
      if (!sortAscending_) {
         std::reverse(sortedFiles.begin(), sortedFiles.end());
      }

      const auto selectionToPreserve = preferredSelection.empty() && fileList_->GetSelection() != wxNOT_FOUND
                                           ? fileList_->GetStringSelection().ToStdString()
                                           : preferredSelection;

      fileList_->Clear();
      int selectedIndex = wxNOT_FOUND;
      for (std::size_t index = 0; index < sortedFiles.size(); ++index) {
         fileList_->Append(sortedFiles[index]);
         if (sortedFiles[index] == selectionToPreserve) {
            selectedIndex = static_cast<int>(index);
         }
      }

      if (selectedIndex != wxNOT_FOUND) {
         fileList_->SetSelection(selectedIndex);
      } else if (!sortedFiles.empty()) {
         fileList_->SetSelection(0);
      }
   }

   confy::BitbucketClient &client_;
   std::string repoUrl_;
   wxChoice *branchChoice_{nullptr};
   wxListBox *fileList_{nullptr};
   wxStaticText *statusLabel_{nullptr};
   wxButton *refreshButton_{nullptr};
   wxButton *sortButton_{nullptr};
   wxButton *okButton_{nullptr};
   std::string selectedBranch_;
   std::string selectedFile_;
   bool busy_{false};
   bool sortAscending_{true};
   std::vector<std::string> currentFiles_;
   std::atomic<std::uint64_t> requestId_{0};
   std::shared_ptr<std::atomic<bool>> aliveFlag_{std::make_shared<std::atomic<bool>>(true)};
};

} // namespace

namespace confy {

PickMenuFrame::PickMenuFrame(std::function<void(const wxString &)> onConfigChosen,
    std::function<void()> onExitRequested) :
    wxFrame(nullptr,
        wxID_ANY,
        AppTitle(),
        wxDefaultPosition,
        wxDefaultSize,
        wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)),
    onConfigChosen_(std::move(onConfigChosen)),
    onExitRequested_(std::move(onExitRequested))
{
   auto *panel       = new wxPanel(this);
   auto *buttonSizer = new wxBoxSizer(wxVERTICAL);

   loadFromBitbucketButton_ = new wxButton(panel, kIdLoadFromBitbucket, "Load from Bitbucket");
   loadLastButton_          = new wxButton(panel, kIdLoadLast, "Load Last");
   auto *loadFileButton     = new wxButton(panel, kIdLoadFile, "Load from File");

   auto buttonFont = loadFileButton->GetFont();
   buttonFont.MakeLarger();
   for (wxButton *button : {loadFromBitbucketButton_, loadLastButton_, loadFileButton}) {
      button->SetFont(buttonFont);
      button->SetMinSize(wxSize(kPickerButtonWidth, kPickerButtonHeight));
   }

   buttonSizer->Add(loadFromBitbucketButton_, 0, wxALIGN_CENTER_HORIZONTAL);
   buttonSizer->Add(loadLastButton_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, kPickerButtonGap);
   buttonSizer->Add(loadFileButton, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, kPickerButtonGap);

   auto *panelSizer = new wxBoxSizer(wxVERTICAL);
   panelSizer->Add(buttonSizer, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 24);
   panel->SetSizerAndFit(panelSizer);

   auto *frameSizer = new wxBoxSizer(wxVERTICAL);
   frameSizer->Add(panel, 1, wxEXPAND);
   SetSizerAndFit(frameSizer);
   SetMinSize(GetSize());
   SetMaxSize(GetSize());
   CentreOnScreen();

   Bind(wxEVT_BUTTON, &PickMenuFrame::OnLoadFile, this, kIdLoadFile);
   Bind(wxEVT_BUTTON, &PickMenuFrame::OnLoadLast, this, kIdLoadLast);
   Bind(wxEVT_BUTTON, &PickMenuFrame::OnLoadFromBitbucket, this, kIdLoadFromBitbucket);
   Bind(wxEVT_CLOSE_WINDOW, &PickMenuFrame::OnCloseWindow, this);
   Bind(wxEVT_SHOW, [this](wxShowEvent &event) {
      if (event.IsShown()) {
         RefreshLastPathState();
      }
      event.Skip();
   });

   RefreshLastPathState();
}

void PickMenuFrame::OnLoadFile(wxCommandEvent &)
{
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

   if (onConfigChosen_) {
      onConfigChosen_(dialog.GetPath());
   }
}

void PickMenuFrame::OnLoadLast(wxCommandEvent &)
{
   const auto lastPath = AppSettings::Get().GetLastConfigPath();
   if (lastPath.empty()) {
      wxMessageBox("No previously loaded XML path was found.", "Load Last", wxOK | wxICON_INFORMATION, this);
      return;
   }

   if (onConfigChosen_) {
      onConfigChosen_(wxString(lastPath));
   }
}

void PickMenuFrame::OnLoadFromBitbucket(wxCommandEvent &)
{
   const auto repoUrl = AppSettings::Get().GetXmlRepoUrl();
   if (repoUrl.empty()) {
      wxMessageBox("Missing XML repo in confy.conf. Set /XmlRepoUrl before using Load from Bitbucket.",
          "Bitbucket",
          wxOK | wxICON_ERROR,
          this);
      return;
   }

   const auto settingsPath = ResolveM2SettingsPath();
   if (settingsPath.empty()) {
      wxMessageBox("Unable to resolve home directory for ~/.m2/settings.xml.",
          "Bitbucket auth",
          wxOK | wxICON_ERROR,
          this);
      return;
   }

   AuthCredentials credentials;
   std::string authError;
   if (!credentials.LoadFromM2SettingsXml(settingsPath, authError)) {
      wxMessageBox(wxString("Unable to load Maven credentials: ") + authError,
          "Bitbucket auth",
          wxOK | wxICON_ERROR,
          this);
      return;
   }

   BitbucketClient client(std::move(credentials));
   BitbucketLoadDialog dialog(this, client, repoUrl);
   if (dialog.ShowModal() != wxID_OK) {
      return;
   }

   const auto branch   = dialog.SelectedBranch();
   const auto filePath = dialog.SelectedFile();
   if (repoUrl.empty() || branch.empty() || filePath.empty()) {
      wxMessageBox("Missing repository, branch, or XML file selection.",
          "Bitbucket",
          wxOK | wxICON_ERROR,
          this);
      return;
   }

   const auto executablePath = wxStandardPaths::Get().GetExecutablePath();
   wxFileName executableFile(executablePath);
   wxString outputPath;
   if (executableFile.IsOk()) {
      outputPath = executableFile.GetPathWithSep() + "from_bitbucket.xml";
   } else {
      outputPath = wxFileName::GetCwd() + wxFILE_SEP_PATH + "from_bitbucket.xml";
   }

   wxBusyCursor busyCursor;
   wxYieldIfNeeded();

   std::string downloadError;
   if (!client.DownloadFile(repoUrl, branch, filePath, outputPath.ToStdString(), downloadError)) {
      wxMessageBox(downloadError, "Bitbucket download failed", wxOK | wxICON_ERROR, this);
      return;
   }

   if (onConfigChosen_) {
      onConfigChosen_(outputPath);
   }
}

void PickMenuFrame::OnCloseWindow(wxCloseEvent &event)
{
   if (onExitRequested_) {
      onExitRequested_();
   }
   event.Skip();
}

void PickMenuFrame::RefreshLastPathState()
{
   const wxString lastPath(AppSettings::Get().GetLastConfigPath());
   const bool hasLastPath = !lastPath.empty();

   loadLastButton_->Enable(hasLastPath);
   if (hasLastPath) {
      loadLastButton_->SetToolTip(lastPath);
   } else {
      loadLastButton_->SetToolTip("No previous XML has been loaded yet");
   }

   const auto xmlRepoUrl = AppSettings::Get().GetXmlRepoUrl();
   const bool hasRepo    = !xmlRepoUrl.empty();
   loadFromBitbucketButton_->Enable(hasRepo);
   if (hasRepo) {
      loadFromBitbucketButton_->UnsetToolTip();
   } else {
      loadFromBitbucketButton_->SetToolTip("No XmlRepoUrl configured");
   }
}

} // namespace confy
