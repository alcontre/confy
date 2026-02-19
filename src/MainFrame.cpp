#include "MainFrame.h"

#include "ConfigLoader.h"
#include "DownloadProgressDialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include <cstdint>

namespace {

constexpr int kIdLoadConfig = wxID_HIGHEST + 1;
constexpr int kIdApply = wxID_HIGHEST + 2;
constexpr int kSectionLabelWidth = 64;
constexpr int kFieldLabelWidth = 72;
constexpr int kSourceFieldWidth = 300;
constexpr int kArtifactFieldWidth = 170;

bool HasSource(const confy::ComponentConfig& component) {
    return component.source.enabled || !component.source.url.empty() ||
           !component.source.branchOrTag.empty() || !component.source.script.empty();
}

bool HasArtifact(const confy::ComponentConfig& component) {
    return component.artifact.enabled || !component.artifact.url.empty() ||
           !component.artifact.version.empty() || !component.artifact.buildType.empty() ||
           !component.artifact.script.empty();
}

}  // namespace

namespace confy {

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Confy", wxDefaultPosition, wxSize(900, 600)) {
    auto* fileMenu = new wxMenu();
    fileMenu->Append(kIdLoadConfig, "&Load Config...\tCtrl+O");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit");

    auto* menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, "&File");
    SetMenuBar(menuBar);

    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    statusLabel_ = new wxStaticText(this, wxID_ANY, "No config loaded.");
    rootSizer->Add(statusLabel_, 0, wxALL | wxEXPAND, 8);

    componentScroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    componentScroll_->SetScrollRate(0, 6);

    componentListSizer_ = new wxBoxSizer(wxVERTICAL);
    componentScroll_->SetSizer(componentListSizer_);

    rootSizer->Add(componentScroll_, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    applyButton_ = new wxButton(this, kIdApply, "Apply");
    applyButton_->Disable();
    rootSizer->Add(applyButton_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, 8);

    SetSizer(rootSizer);

    Bind(wxEVT_MENU, &MainFrame::OnLoadConfig, this, kIdLoadConfig);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
    Bind(wxEVT_BUTTON, &MainFrame::OnApply, this, kIdApply);
}

void MainFrame::OnLoadConfig(wxCommandEvent&) {
    wxFileDialog dialog(this,
                        "Open config XML",
                        "",
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
        job.componentName = component.displayName.empty() ? component.name : component.displayName;
        job.repositoryUrl = component.artifact.url;
        job.version = component.artifact.version;
        job.buildType = component.artifact.buildType;
        job.targetDirectory = config_.rootPath + "/" + component.path;

        jobs.push_back(std::move(job));
    }

    if (jobs.empty()) {
        wxMessageBox("No artifact download jobs are enabled.", "Nothing to download", wxOK | wxICON_INFORMATION, this);
        return;
    }

    DownloadProgressDialog dialog(this, std::move(jobs));
    dialog.ShowModal();
}

void MainFrame::RenderConfig() {
    uiUpdating_ = true;
    componentListSizer_->Clear(true);
    rows_.clear();
    rows_.reserve(config_.components.size());

    for (std::size_t i = 0; i < config_.components.size(); ++i) {
        AddComponentRow(i);
    }

    componentScroll_->Layout();
    componentScroll_->FitInside();
    uiUpdating_ = false;

    statusLabel_->SetLabelText(wxString::Format("Loaded %zu component(s)", config_.components.size()));
    applyButton_->Enable(!config_.components.empty());
}

void MainFrame::AddComponentRow(std::size_t componentIndex) {
    auto& component = config_.components[componentIndex];

    const auto displayName = component.displayName.empty() ? component.name : component.displayName;
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
    sourceBranch->SetMinSize(wxSize(kSourceFieldWidth, -1));
    sourceRow->Add(sourceBranch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 0);
    sourceRow->AddStretchSpacer();
    detailsSizer->Add(sourceRow, 0, wxEXPAND | wxBOTTOM, 6);

    auto* artifactRow = new wxBoxSizer(wxHORIZONTAL);
    artifactRow->Add(makeFixedLabel("Artifact", kSectionLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* artifactEnabled = new wxCheckBox(componentScroll_, wxID_ANY, "Enable");
    artifactRow->Add(artifactEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    artifactRow->Add(makeFixedLabel("Version", kFieldLabelWidth), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* artifactVersion = new wxComboBox(componentScroll_, wxID_ANY);
    artifactVersion->SetMinSize(wxSize(kArtifactFieldWidth, -1));
    artifactRow->Add(artifactVersion, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    artifactRow->Add(makeFixedLabel("Build", 44), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* artifactBuildType = new wxComboBox(componentScroll_, wxID_ANY);
    artifactBuildType->SetMinSize(wxSize(kArtifactFieldWidth, -1));
    artifactRow->Add(artifactBuildType, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 0);
    artifactRow->AddStretchSpacer();
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
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].source.branchOrTag =
            rows_[componentIndex].sourceBranch->GetValue().ToStdString();
    });

    row.artifactVersion->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent&) {
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].artifact.version =
            rows_[componentIndex].artifactVersion->GetValue().ToStdString();
    });

    row.artifactBuildType->Bind(wxEVT_TEXT, [this, componentIndex](wxCommandEvent&) {
        if (uiUpdating_) {
            return;
        }
        config_.components[componentIndex].artifact.buildType =
            rows_[componentIndex].artifactBuildType->GetValue().ToStdString();
    });

    RefreshRowEnabledState(componentIndex);
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

}  // namespace confy
