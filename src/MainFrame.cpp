#include "MainFrame.h"

#include "AppInfo.h"
#include "AppSettings.h"
#include "AuthCredentials.h"
#include "ConfigLoader.h"
#include "ConfigWriter.h"
#include "DebugConsole.h"
#include "DownloadProgressDialog.h"
#include "GitClient.h"
#include "NexusClient.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/combobox.h>
#include <wx/dataobj.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
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
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace {

constexpr int kIdReloadConfig     = wxID_HIGHEST + 1;
constexpr int kIdApply            = wxID_HIGHEST + 2;
constexpr int kIdDeselectAll      = wxID_HIGHEST + 3;
constexpr int kIdViewDebugConsole = wxID_HIGHEST + 4;
constexpr int kIdCloseConfig      = wxID_HIGHEST + 5;
constexpr int kIdSaveAs           = wxID_HIGHEST + 6;
constexpr int kIdCopyConfig       = wxID_HIGHEST + 7;
constexpr int kSectionLabelWidth  = 64;
constexpr int kFieldLabelWidth    = 72;
const wxColour kModifiedIndicatorActiveColour(255, 140, 0);

bool HasSource(const confy::ComponentConfig &component)
{
   return component.sourcePresent;
}

bool HasArtifact(const confy::ComponentConfig &component)
{
   return component.artifactPresent;
}

} // namespace

namespace confy {

MainFrame::MainFrame(const wxString &initialConfigPath, std::function<void()> onReturnToPicker) :
    wxFrame(nullptr, wxID_ANY, AppTitle(), wxDefaultPosition, wxSize(900, 600)),
    onReturnToPicker_(std::move(onReturnToPicker))
{
   CreateStatusBar(1);
   SetStatusText("Config: none");

   auto *fileMenu = new wxMenu();
   fileMenu->Append(kIdCloseConfig, "&Close\tCtrl+W");
   fileMenu->Append(kIdReloadConfig, "&Reload\tCtrl+R");
   fileMenu->AppendSeparator();
   fileMenu->Append(kIdSaveAs, "Save &As...\tCtrl+Shift+S");
   fileMenu->AppendSeparator();
   fileMenu->Append(wxID_EXIT, "E&xit");

   auto *editMenu = new wxMenu();
   editMenu->Append(wxID_SELECTALL, "Select &All");
   editMenu->Append(kIdDeselectAll, "&Deselect All");
   editMenu->AppendSeparator();
   editMenu->Append(kIdCopyConfig, "&Copy Config");

   auto *viewMenu = new wxMenu();
   viewMenu->AppendCheckItem(kIdViewDebugConsole, "&Debug Console");

   auto *menuBar = new wxMenuBar();
   menuBar->Append(fileMenu, "&File");
   menuBar->Append(editMenu, "&Edit");
   menuBar->Append(viewMenu, "&View");
   SetMenuBar(menuBar);

   auto *rootSizer = new wxBoxSizer(wxVERTICAL);

   componentScroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
   componentScroll_->SetScrollRate(0, 6);

   componentContentPanel_ = new wxPanel(componentScroll_);
   componentListSizer_    = new wxBoxSizer(wxVERTICAL);
   componentContentPanel_->SetSizer(componentListSizer_);

   auto *scrollSizer = new wxBoxSizer(wxVERTICAL);
   scrollSizer->Add(componentContentPanel_, 1, wxEXPAND);
   componentScroll_->SetSizer(scrollSizer);

   rootSizer->Add(componentScroll_, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

   applyButton_ = new wxButton(this, kIdApply, "Apply");
   applyButton_->Disable();
   rootSizer->Add(applyButton_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 8);

   SetSizer(rootSizer);

   Bind(wxEVT_MENU, &MainFrame::OnCloseConfig, this, kIdCloseConfig);
   Bind(wxEVT_MENU, &MainFrame::OnReloadConfig, this, kIdReloadConfig);
   Bind(wxEVT_MENU, &MainFrame::OnSaveAs, this, kIdSaveAs);
   Bind(wxEVT_MENU, &MainFrame::OnSelectAll, this, wxID_SELECTALL);
   Bind(wxEVT_MENU, &MainFrame::OnDeselectAll, this, kIdDeselectAll);
   Bind(wxEVT_MENU, &MainFrame::OnCopyConfig, this, kIdCopyConfig);
   Bind(wxEVT_MENU, &MainFrame::OnToggleDebugConsole, this, kIdViewDebugConsole);
   Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
   Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnCloseWindow, this);
   Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateSaveAs, this, kIdSaveAs);
   Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateSelectAll, this, wxID_SELECTALL);
   Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateDeselectAll, this, kIdDeselectAll);
   Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateCopyConfig, this, kIdCopyConfig);
   Bind(wxEVT_UPDATE_UI, &MainFrame::OnUpdateDebugConsole, this, kIdViewDebugConsole);
   Bind(wxEVT_BUTTON, &MainFrame::OnApply, this, kIdApply);
   Bind(wxEVT_SIZE, &MainFrame::OnFrameSize, this);

   if (!LoadConfigFromPath(initialConfigPath)) {
      CallAfter([this]() { Close(); });
   }
}

MainFrame::~MainFrame()
{
   StopMetadataWorkers();
}

void MainFrame::OnCloseConfig(wxCommandEvent &)
{
   Close();
}

void MainFrame::OnReloadConfig(wxCommandEvent &)
{
   if (loadedConfigPath_.empty()) {
      return;
   }
   LoadConfigFromPath(wxString(loadedConfigPath_));
}

void MainFrame::OnExit(wxCommandEvent &)
{
   exitRequested_ = true;
   if (wxTheApp) {
      wxTheApp->ExitMainLoop();
   }
   Close(true);
}

void MainFrame::OnCloseWindow(wxCloseEvent &event)
{
   if (!exitRequested_ && onReturnToPicker_) {
      onReturnToPicker_();
   }
   event.Skip();
}

void MainFrame::OnSaveAs(wxCommandEvent &)
{
   wxString initialDirectory;
   wxString initialFileName;
   if (!loadedConfigPath_.empty()) {
      wxFileName loadedFile{wxString(loadedConfigPath_)};
      if (loadedFile.IsOk()) {
         initialDirectory = loadedFile.GetPath();
         initialFileName  = loadedFile.GetFullName();
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

bool MainFrame::LoadConfigFromPath(const wxString &path)
{
   ConfigLoader loader;
   auto result = loader.LoadFromFile(path.ToStdString());
   if (!result.success) {
      wxMessageBox(result.errorMessage, "Config load failed", wxOK | wxICON_ERROR, this);
      return false;
   }

   config_           = std::move(result.config);
   loadedConfigPath_ = path.ToStdString();
   AppSettings::Get().SetLastConfigPath(loadedConfigPath_);
   RenderConfig();
   return true;
}

void MainFrame::OnApply(wxCommandEvent &)
{
   std::vector<DownloadJob> jobs;
   jobs.reserve(config_.components.size());

   static std::uint64_t nextJobId = 1;

   for (std::size_t i = 0; i < config_.components.size(); ++i) {
      const auto &component = config_.components[i];

      if (HasSource(component) && component.source.enabled && !component.source.url.empty()) {
         GitCloneJob sourceJob;
         sourceJob.jobId                = nextJobId++;
         sourceJob.componentIndex       = i;
         sourceJob.componentName        = component.name;
         sourceJob.componentDisplayName = component.displayName;
         sourceJob.repositoryUrl        = component.source.url;
         sourceJob.branchOrTag          = component.source.branchOrTag;
         sourceJob.targetDirectory      = (std::filesystem::path(config_.rootPath) / component.path).string();
         sourceJob.postDownloadScript   = component.source.script;
         sourceJob.shallow              = component.source.shallow;
         jobs.push_back(DownloadJob::FromSource(std::move(sourceJob)));
      }

      if (HasArtifact(component) && component.artifact.enabled) {
         NexusDownloadJob artifactJob;
         artifactJob.jobId                = nextJobId++;
         artifactJob.componentIndex       = i;
         artifactJob.componentName        = component.name;
         artifactJob.componentDisplayName = component.displayName;
         artifactJob.repositoryUrl        = component.artifact.url;
         artifactJob.artifactPath         = component.artifact.relativePath.empty()
                                                ? component.name
                                                : component.artifact.relativePath + "/" + component.name;
         artifactJob.version              = component.artifact.version;
         artifactJob.buildType            = component.artifact.buildType;
         artifactJob.targetDirectory      = (std::filesystem::path(config_.rootPath) / component.path).string();
         artifactJob.postDownloadScript   = component.artifact.script;
         artifactJob.regexIncludes        = component.artifact.regexIncludes;
         artifactJob.regexExcludes        = component.artifact.regexExcludes;

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

void MainFrame::OnSelectAll(wxCommandEvent &)
{
   if (config_.components.empty()) {
      return;
   }

   uiUpdating_ = true;
   for (std::size_t i = 0; i < config_.components.size(); ++i) {
      auto &component            = config_.components[i];
      component.source.enabled   = HasSource(component);
      component.artifact.enabled = HasArtifact(component);
      RefreshRowEnabledState(i);
   }
   uiUpdating_ = false;
}

void MainFrame::OnDeselectAll(wxCommandEvent &)
{
   if (config_.components.empty()) {
      return;
   }

   uiUpdating_ = true;
   for (std::size_t i = 0; i < config_.components.size(); ++i) {
      config_.components[i].source.enabled   = false;
      config_.components[i].artifact.enabled = false;
      RefreshRowEnabledState(i);
   }
   uiUpdating_ = false;
}

void MainFrame::OnUpdateSelectAll(wxUpdateUIEvent &event)
{
   event.Enable(!config_.components.empty());
}

void MainFrame::OnUpdateDeselectAll(wxUpdateUIEvent &event)
{
   event.Enable(!config_.components.empty());
}

void MainFrame::OnUpdateSaveAs(wxUpdateUIEvent &event)
{
   event.Enable(!config_.components.empty());
}

void MainFrame::OnCopyConfig(wxCommandEvent &)
{
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

void MainFrame::OnUpdateCopyConfig(wxUpdateUIEvent &event)
{
   event.Enable(!config_.components.empty());
}

void MainFrame::OnToggleDebugConsole(wxCommandEvent &)
{
   ToggleDebugConsole(this);
}

void MainFrame::OnUpdateDebugConsole(wxUpdateUIEvent &event)
{
   event.Check(IsDebugConsoleVisible());
}

void MainFrame::OnFrameSize(wxSizeEvent &event)
{
   event.Skip();
   RelayoutComponentArea();
}

void MainFrame::RelayoutComponentArea()
{
   componentContentPanel_->Layout();
   componentScroll_->Layout();
   componentScroll_->FitInside();
}

void MainFrame::RenderConfig()
{
   StopMetadataWorkers();
   uiUpdating_ = true;
   componentListSizer_->Clear(true);
   rows_.clear();
   rows_.reserve(config_.components.size());
   rowBaselines_.clear();
   rowBaselines_.resize(config_.components.size());
   rowChangeState_.clear();
   rowChangeState_.resize(config_.components.size());
   openComboDropdowns_.clear();
   metadataState_.clear();
   metadataState_.resize(config_.components.size());
   componentSourceRequests_.clear();
   componentSourceRequests_.resize(config_.components.size());
   componentArtifactRequests_.clear();
   componentArtifactRequests_.resize(config_.components.size());

   // Build per-component request snapshots consumed by worker threads. These
   // vectors are refreshed as a batch for the current config view and are not
   // mutated again until workers are stopped and the next RenderConfig() runs.
   for (std::size_t i = 0; i < config_.components.size(); ++i) {
      componentSourceRequests_[i]   = config_.components[i].source.url;
      componentArtifactRequests_[i] = {config_.components[i].artifact.url,
          config_.components[i].artifact.relativePath.empty()
              ? config_.components[i].name
              : config_.components[i].artifact.relativePath + "/" + config_.components[i].name};
      AddComponentRow(i);
   }

   RelayoutComponentArea();
   uiUpdating_ = false;

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

void MainFrame::AddComponentRow(std::size_t componentIndex)
{
   auto &component = config_.components[componentIndex];

   const auto displayName = component.displayName;
   auto *rowBox           = new wxStaticBoxSizer(wxVERTICAL,
                 componentContentPanel_,
                 wxString::Format("%s  (%s)", displayName, component.path));
   auto *rowParent        = rowBox->GetStaticBox();

   auto *detailsSizer = new wxBoxSizer(wxVERTICAL);

   auto *componentCardSizer = new wxBoxSizer(wxHORIZONTAL);
   auto *modifiedIndicator  = new wxPanel(componentContentPanel_, wxID_ANY, wxDefaultPosition, wxSize(6, -1));
   modifiedIndicator->SetMinSize(wxSize(6, -1));
   modifiedIndicator->SetBackgroundColour(componentContentPanel_->GetBackgroundColour());
   componentCardSizer->Add(modifiedIndicator, 0, wxEXPAND | wxRIGHT, 6);

   auto makeFixedLabel = [rowParent](const wxString &text, int width) {
      auto *label = new wxStaticText(rowParent, wxID_ANY, text);
      label->SetMinSize(wxSize(width, -1));
      return label;
   };

   auto *sourceRow = new wxBoxSizer(wxHORIZONTAL);
   sourceRow->Add(makeFixedLabel("Source", kSectionLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
   auto *sourceEnabled = new wxCheckBox(rowParent, wxID_ANY, "Enable");
   sourceRow->Add(sourceEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
   auto *sourceShallow = new wxCheckBox(rowParent, wxID_ANY, "Shallow");
   sourceRow->Add(sourceShallow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
   sourceRow->Add(makeFixedLabel("Branch/Tag", kFieldLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   auto *sourceBranch = new wxComboBox(rowParent, wxID_ANY);
   sourceRow->Add(sourceBranch, 1, wxRIGHT | wxEXPAND, 0);
   detailsSizer->Add(sourceRow, 0, wxEXPAND | wxBOTTOM, 6);

   auto *artifactRow = new wxBoxSizer(wxHORIZONTAL);
   artifactRow->Add(makeFixedLabel("Artifact", kSectionLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
   auto *artifactEnabled = new wxCheckBox(rowParent, wxID_ANY, "Enable");
   artifactRow->Add(artifactEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
   artifactRow->Add(makeFixedLabel("Version", kFieldLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   auto *artifactVersion = new wxComboBox(rowParent, wxID_ANY);
   artifactRow->Add(artifactVersion, 1, wxRIGHT | wxEXPAND, 10);
   artifactRow->Add(makeFixedLabel("Build", 44), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   auto *artifactBuildType = new wxComboBox(rowParent, wxID_ANY);
   artifactRow->Add(artifactBuildType, 1, wxRIGHT | wxEXPAND, 0);
   detailsSizer->Add(artifactRow, 0, wxEXPAND);

   rowBox->Add(detailsSizer, 1, wxEXPAND);
   componentCardSizer->Add(rowBox, 1, wxEXPAND);

   componentListSizer_->Add(componentCardSizer, 0, wxBOTTOM | wxEXPAND, 6);

   ComponentRowWidgets row;
   row.sourceEnabled     = sourceEnabled;
   row.sourceShallow     = sourceShallow;
   row.sourceBranch      = sourceBranch;
   row.artifactEnabled   = artifactEnabled;
   row.artifactVersion   = artifactVersion;
   row.artifactBuildType = artifactBuildType;
   rows_.push_back(row);

   rowBaselines_[componentIndex].sourceBranch        = component.source.branchOrTag;
   rowBaselines_[componentIndex].artifactVersion     = component.artifact.version;
   rowBaselines_[componentIndex].artifactBuildType   = component.artifact.buildType;
   rowChangeState_[componentIndex].modifiedIndicator = modifiedIndicator;
   metadataState_[componentIndex].pendingBuildTypeSelection = component.artifact.buildType;

   auto bindComboWheelProtection = [this](wxComboBox *combo) {
      combo->Bind(wxEVT_COMBOBOX_DROPDOWN, [this, combo](wxCommandEvent &) { openComboDropdowns_.insert(combo); });
      combo->Bind(wxEVT_COMBOBOX_CLOSEUP, [this, combo](wxCommandEvent &) { openComboDropdowns_.erase(combo); });
      combo->Bind(wxEVT_MOUSEWHEEL, [this, combo](wxMouseEvent &event) {
         if (openComboDropdowns_.find(combo) != openComboDropdowns_.end()) {
            event.Skip();
            return;
         }

         if (componentScroll_) {
            int wheelDelta = event.GetWheelDelta();
            if (wheelDelta == 0) {
               wheelDelta = 120;
            }
            int wheelSteps = event.GetWheelRotation() / wheelDelta;
            if (wheelSteps == 0) {
               wheelSteps = (event.GetWheelRotation() > 0) ? 1 : -1;
            }
            int linesPerAction = event.GetLinesPerAction();
            if (linesPerAction <= 0) {
               linesPerAction = 3;
            }
            componentScroll_->ScrollLines(-wheelSteps * linesPerAction);
         }
      });
   };

   bindComboWheelProtection(row.sourceBranch);
   bindComboWheelProtection(row.artifactVersion);
   bindComboWheelProtection(row.artifactBuildType);

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

   row.sourceEnabled->Bind(wxEVT_CHECKBOX, [this, componentIndex](wxCommandEvent &) {
      if (uiUpdating_) {
         return;
      }
      config_.components[componentIndex].source.enabled = rows_[componentIndex].sourceEnabled->GetValue();
      RefreshRowEnabledState(componentIndex);
   });

   row.sourceShallow->Bind(wxEVT_CHECKBOX, [this, componentIndex](wxCommandEvent &) {
      if (uiUpdating_) {
         return;
      }
      config_.components[componentIndex].source.shallow = rows_[componentIndex].sourceShallow->GetValue();
   });

   row.artifactEnabled->Bind(wxEVT_CHECKBOX, [this, componentIndex](wxCommandEvent &) {
      if (uiUpdating_) {
         return;
      }
      config_.components[componentIndex].artifact.enabled = rows_[componentIndex].artifactEnabled->GetValue();
      RefreshRowEnabledState(componentIndex);
   });

   row.sourceBranch->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent &) {
      UpdateComboTooltip(*rows_[componentIndex].sourceBranch);
      if (uiUpdating_) {
         return;
      }
      rowChangeState_[componentIndex].sourceBranchTouched = true;
      config_.components[componentIndex].source.branchOrTag =
          rows_[componentIndex].sourceBranch->GetValue().ToStdString();
      RefreshRowModifiedIndicator(componentIndex);
   });
   row.sourceBranch->Bind(wxEVT_COMBOBOX, [this, componentIndex](wxCommandEvent &) {
      UpdateComboTooltip(*rows_[componentIndex].sourceBranch);
      if (uiUpdating_) {
         return;
      }
      rowChangeState_[componentIndex].sourceBranchTouched = true;
      config_.components[componentIndex].source.branchOrTag =
          rows_[componentIndex].sourceBranch->GetValue().ToStdString();
      RefreshRowModifiedIndicator(componentIndex);
   });
   row.sourceBranch->Bind(wxEVT_COMBOBOX_DROPDOWN, [this, componentIndex](wxCommandEvent &) {
      if (uiUpdating_) {
         return;
      }
      EnqueueSourceRefsFetch(componentIndex, true);
   });

   row.artifactVersion->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent &) {
      UpdateComboTooltip(*rows_[componentIndex].artifactVersion);
      if (uiUpdating_) {
         return;
      }
      rowChangeState_[componentIndex].artifactVersionTouched = true;
      config_.components[componentIndex].artifact.version =
          rows_[componentIndex].artifactVersion->GetValue().ToStdString();
      RefreshRowModifiedIndicator(componentIndex);
   });
   row.artifactVersion->Bind(wxEVT_COMBOBOX, [this, componentIndex](wxCommandEvent &) {
      UpdateComboTooltip(*rows_[componentIndex].artifactVersion);
      if (uiUpdating_) {
         return;
      }
      rowChangeState_[componentIndex].artifactVersionTouched = true;
      const auto version                                     = rows_[componentIndex].artifactVersion->GetValue().ToStdString();
      config_.components[componentIndex].artifact.version    = version;
      RefreshRowModifiedIndicator(componentIndex);
      if (metadataState_[componentIndex].buildTypesByVersion.find(version) !=
          metadataState_[componentIndex].buildTypesByVersion.end()) {
         RefreshArtifactBuildTypes(componentIndex, version);
      } else {
         ClearArtifactBuildTypes(componentIndex);
      }
      EnqueueBuildTypeFetch(componentIndex, version);
   });
   row.artifactVersion->Bind(wxEVT_COMBOBOX_DROPDOWN, [this, componentIndex](wxCommandEvent &) {
      if (uiUpdating_) {
         return;
      }
      EnqueueVersionFetch(componentIndex, true);
   });

   row.artifactBuildType->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent &) {
      UpdateComboTooltip(*rows_[componentIndex].artifactBuildType);
      if (uiUpdating_) {
         return;
      }
      rowChangeState_[componentIndex].artifactBuildTypeTouched = true;
      config_.components[componentIndex].artifact.buildType =
          rows_[componentIndex].artifactBuildType->GetValue().ToStdString();
      metadataState_[componentIndex].pendingBuildTypeSelection = config_.components[componentIndex].artifact.buildType;
      RefreshRowModifiedIndicator(componentIndex);
   });
   row.artifactBuildType->Bind(wxEVT_COMBOBOX, [this, componentIndex](wxCommandEvent &) {
      UpdateComboTooltip(*rows_[componentIndex].artifactBuildType);
      if (uiUpdating_) {
         return;
      }
      rowChangeState_[componentIndex].artifactBuildTypeTouched = true;
      config_.components[componentIndex].artifact.buildType =
          rows_[componentIndex].artifactBuildType->GetValue().ToStdString();
      metadataState_[componentIndex].pendingBuildTypeSelection = config_.components[componentIndex].artifact.buildType;
      RefreshRowModifiedIndicator(componentIndex);
   });

   RefreshRowModifiedIndicator(componentIndex);
   RefreshRowEnabledState(componentIndex);
}

void MainFrame::UpdateComboTooltip(wxComboBox &comboBox)
{
   const auto value = comboBox.GetValue();
   if (value.empty()) {
      comboBox.UnsetToolTip();
      return;
   }

   comboBox.SetToolTip(value);
}

void MainFrame::UpdateRowTooltips(std::size_t componentIndex)
{
   if (componentIndex >= rows_.size()) {
      return;
   }

   const auto &row = rows_[componentIndex];
   UpdateComboTooltip(*row.sourceBranch);
   UpdateComboTooltip(*row.artifactVersion);
   UpdateComboTooltip(*row.artifactBuildType);
}

void MainFrame::RefreshRowModifiedIndicator(std::size_t componentIndex)
{
   if (componentIndex >= config_.components.size() || componentIndex >= rowBaselines_.size() ||
       componentIndex >= rowChangeState_.size()) {
      return;
   }

   const auto &component = config_.components[componentIndex];
   const auto &baseline  = rowBaselines_[componentIndex];
   auto &state           = rowChangeState_[componentIndex];

   const bool sourceModified =
       state.sourceBranchTouched && component.source.branchOrTag != baseline.sourceBranch;
   const bool versionModified =
       state.artifactVersionTouched && component.artifact.version != baseline.artifactVersion;
   const bool buildTypeModified =
       state.artifactBuildTypeTouched && component.artifact.buildType != baseline.artifactBuildType;

   const bool isModified = sourceModified || versionModified || buildTypeModified;

   if (!state.modifiedIndicator) {
      return;
   }

   const auto inactiveColour = componentContentPanel_ ? componentContentPanel_->GetBackgroundColour() : wxNullColour;
   state.modifiedIndicator->SetBackgroundColour(isModified ? kModifiedIndicatorActiveColour : inactiveColour);
   state.modifiedIndicator->Refresh();

   if (isModified) {
      state.modifiedIndicator->SetToolTip("Modified from XML");
   } else {
      state.modifiedIndicator->UnsetToolTip();
   }
}

void MainFrame::RefreshRowEnabledState(std::size_t componentIndex)
{
   if (componentIndex >= config_.components.size() || componentIndex >= rows_.size()) {
      return;
   }

   const auto &component = config_.components[componentIndex];
   auto &row             = rows_[componentIndex];

   const bool sourceExists   = HasSource(component);
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

void MainFrame::ClearArtifactBuildTypes(std::size_t componentIndex)
{
   if (componentIndex >= config_.components.size() || componentIndex >= rows_.size()) {
      return;
   }

   auto &buildTypeBox      = rows_[componentIndex].artifactBuildType;
   const bool hadBuildType = !config_.components[componentIndex].artifact.buildType.empty();

   uiUpdating_ = true;
   buildTypeBox->Clear();
   buildTypeBox->SetValue(wxString());
   uiUpdating_ = false;

   config_.components[componentIndex].artifact.buildType.clear();
   if (hadBuildType) {
      rowChangeState_[componentIndex].artifactBuildTypeTouched = true;
      RefreshRowModifiedIndicator(componentIndex);
   }
   UpdateComboTooltip(*buildTypeBox);
}

void MainFrame::RefreshArtifactBuildTypes(std::size_t componentIndex, const std::string &version)
{
   if (componentIndex >= config_.components.size() || componentIndex >= metadataState_.size() ||
       componentIndex >= rows_.size() || config_.components[componentIndex].artifact.version != version) {
      return;
   }

   const auto buildTypesIt = metadataState_[componentIndex].buildTypesByVersion.find(version);
   if (buildTypesIt == metadataState_[componentIndex].buildTypesByVersion.end()) {
      return;
   }

   auto &buildTypeBox     = rows_[componentIndex].artifactBuildType;
   auto &state            = metadataState_[componentIndex];
   const auto &buildTypes = buildTypesIt->second;
   const bool keepSelection =
       !state.pendingBuildTypeSelection.empty() &&
       std::find(buildTypes.begin(), buildTypes.end(), state.pendingBuildTypeSelection) != buildTypes.end();
   const auto nextBuildType   = keepSelection ? state.pendingBuildTypeSelection : std::string();
   const bool buildTypeChange = config_.components[componentIndex].artifact.buildType != nextBuildType;

   uiUpdating_ = true;
   buildTypeBox->Clear();
   for (const auto &buildType : buildTypes) {
      buildTypeBox->Append(buildType);
   }
   buildTypeBox->SetValue(nextBuildType);
   uiUpdating_ = false;

   config_.components[componentIndex].artifact.buildType = nextBuildType;
   state.pendingBuildTypeSelection = nextBuildType;
   if (buildTypeChange) {
      rowChangeState_[componentIndex].artifactBuildTypeTouched = true;
      RefreshRowModifiedIndicator(componentIndex);
   }
   UpdateComboTooltip(*buildTypeBox);
}

void MainFrame::EnqueueSourceRefsFetch(std::size_t componentIndex, bool prioritize)
{
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
   task.type           = MetadataTaskType::SourceRefs;
   task.componentIndex = componentIndex;
   const auto key      = "s:" + std::to_string(componentIndex);

   // Queue semantics:
   // - At most one queued source-ref task per component key.
   // - Prioritized requests move the existing queued task to the front.
   // - Deduplication applies to queued tasks; once dequeued, a new request can
   //   be queued even if an older one is still in-flight.
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

void MainFrame::StartMetadataWorkers()
{
   std::scoped_lock lock(metadataMutex_);
   if (!metadataWorkers_.empty()) {
      return;
   }
   // Two background workers process metadata in parallel. UI work remains on
   // the main thread and is posted via CallAfter from worker code.
   stopMetadataWorkers_ = false;
   metadataWorkers_.emplace_back(&MainFrame::MetadataWorkerLoop, this);
   metadataWorkers_.emplace_back(&MainFrame::MetadataWorkerLoop, this);
}

void MainFrame::StopMetadataWorkers()
{
   {
      std::scoped_lock lock(metadataMutex_);
      // Shutdown contract: stop accepting/processing queued metadata work,
      // clear pending tasks, then join workers so no background thread remains
      // active before config/UI state is rebuilt.
      stopMetadataWorkers_ = true;
      metadataTasks_.clear();
      metadataTaskKeys_.clear();
   }
   metadataCv_.notify_all();
   for (auto &worker : metadataWorkers_) {
      if (worker.joinable()) {
         worker.join();
      }
   }
   metadataWorkers_.clear();
}

void MainFrame::EnqueueVersionFetch(std::size_t componentIndex, bool prioritize)
{
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
   task.type           = MetadataTaskType::Versions;
   task.componentIndex = componentIndex;
   const auto key      = "v:" + std::to_string(componentIndex);

   // Same queueing contract as source refs: dedupe by key while queued and
   // optional promotion to the front when the user explicitly prioritizes.
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

void MainFrame::EnqueueBuildTypeFetch(std::size_t componentIndex, const std::string &version)
{
   if (componentIndex >= config_.components.size() || componentIndex >= metadataState_.size() || version.empty()) {
      return;
   }
   if (!HasArtifact(config_.components[componentIndex]) || config_.components[componentIndex].artifact.url.empty()) {
      return;
   }
   auto &state    = metadataState_[componentIndex];
   const auto hit = state.buildTypesByVersion.find(version);
   if (hit != state.buildTypesByVersion.end()) {
      // Cache hit; apply the previously-fetched build types to the dropdown
      auto &buildTypeBox           = rows_[componentIndex].artifactBuildType;
      const auto previousSelection = buildTypeBox->GetValue().ToStdString();
      uiUpdating_                  = true;
      buildTypeBox->Clear();
      for (const auto &buildType : hit->second) {
         buildTypeBox->Append(buildType);
      }
      if (!previousSelection.empty()) {
         buildTypeBox->SetValue(previousSelection);
      }
      uiUpdating_ = false;
      UpdateComboTooltip(*buildTypeBox);
      return;
   }
   // Already in-flight for this version; wait for the worker callback.
   if (state.buildTypesLoadingVersions.find(version) != state.buildTypesLoadingVersions.end()) {
      return;
   }

   MetadataTask task;
   task.type           = MetadataTaskType::BuildTypes;
   task.componentIndex = componentIndex;
   task.version        = version;
   const auto key      = "b:" + std::to_string(componentIndex) + ":" + version;
   // Build-type tasks are version-scoped and deduped per component+version key.
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

void MainFrame::MetadataWorkerLoop()
{
#ifdef _WIN32
   std::string homeDir;
   if (const char *h = std::getenv("USERPROFILE")) {
      homeDir = h;
   } else {
      const char *drive    = std::getenv("HOMEDRIVE");
      const char *homepath = std::getenv("HOMEPATH");
      if (drive && homepath) {
         homeDir = std::string(drive) + homepath;
      }
   }
#else
   const char *homeEnv       = std::getenv("HOME");
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
         // Copy one task out while holding the lock, then release the lock and
         // perform network/auth work without blocking other workers/queue ops.
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
         // Workers never update wx widgets or GUI-owned metadata state directly.
         // CallAfter marshals updates onto the main GUI event loop.
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
         // Move fetched data into the posted callback so the worker thread can
         // continue and UI mutation happens only on the event loop thread.
         CallAfter([this, index = task.componentIndex, refs = std::move(refs), ok]() mutable {
            if (index >= metadataState_.size() || index >= rows_.size() || index >= config_.components.size()) {
               return;
            }

            auto &state             = metadataState_[index];
            state.sourceRefsLoading = false;
            state.sourceRefsLoaded  = ok;
            if (!ok) {
               return;
            }

            auto *sourceBranch           = rows_[index].sourceBranch;
            const auto previousSelection = sourceBranch->GetValue().ToStdString();

            uiUpdating_ = true;
            sourceBranch->Clear();
            for (const auto &ref : refs) {
               sourceBranch->Append(ref);
            }
            if (!previousSelection.empty()) {
               sourceBranch->SetValue(previousSelection);
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
         // Result ownership is transferred to the GUI callback by move-capture.
         CallAfter([this, index = task.componentIndex, versions = std::move(versions), ok]() mutable {
            if (index >= metadataState_.size() || index >= rows_.size() || index >= config_.components.size()) {
               return;
            }

            auto &state           = metadataState_[index];
            state.versionsLoading = false;
            state.versionsLoaded  = ok;
            if (!ok) {
               return;
            }

            auto &versionBox             = rows_[index].artifactVersion;
            const auto previousSelection = versionBox->GetValue().ToStdString();
            uiUpdating_                  = true;
            versionBox->Clear();
            for (const auto &version : versions) {
               versionBox->Append(version);
            }
            if (!previousSelection.empty()) {
               versionBox->SetValue(previousSelection);
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
      // Version and payload are copied/moved into the callback to decouple UI
      // application from worker lifetime and stack storage.
      CallAfter([this,
                    index      = task.componentIndex,
                    version    = task.version,
                    buildTypes = std::move(buildTypes),
                    ok]() mutable {
         if (index >= metadataState_.size() || index >= rows_.size() || index >= config_.components.size()) {
            return;
         }

         auto &state = metadataState_[index];
         state.buildTypesLoadingVersions.erase(version);
         if (!ok) {
            return;
         }
         state.buildTypesByVersion[version] = buildTypes;
         RefreshArtifactBuildTypes(index, version);
      });
   }
}

} // namespace confy
