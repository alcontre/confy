#include "MainFrame.h"

#include "ConfigLoader.h"

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

namespace {

constexpr int kIdLoadConfig = wxID_HIGHEST + 1;
constexpr int kIdApply = wxID_HIGHEST + 2;

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
    std::size_t sourceEnabled = 0;
    std::size_t artifactEnabled = 0;
    for (const auto& component : config_.components) {
        if (HasSource(component) && component.source.enabled) {
            ++sourceEnabled;
        }
        if (HasArtifact(component) && component.artifact.enabled) {
            ++artifactEnabled;
        }
    }

    wxMessageBox(
        wxString::Format("Ready to apply:\n- Source downloads: %zu\n- Artifact downloads: %zu\n\nTarget: %s",
                         sourceEnabled,
                         artifactEnabled,
                         config_.rootPath),
        "Apply Preview",
        wxOK | wxICON_INFORMATION,
        this);
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

    auto* sourceRow = new wxBoxSizer(wxHORIZONTAL);
    sourceRow->Add(new wxStaticText(componentScroll_, wxID_ANY, "Source"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* sourceEnabled = new wxCheckBox(componentScroll_, wxID_ANY, "Enable");
    sourceRow->Add(sourceEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    sourceRow->Add(new wxStaticText(componentScroll_, wxID_ANY, "Branch/Tag"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* sourceBranch = new wxComboBox(componentScroll_, wxID_ANY);
    sourceRow->Add(sourceBranch, 1, wxALIGN_CENTER_VERTICAL);
    detailsSizer->Add(sourceRow, 0, wxEXPAND | wxBOTTOM, 6);

    auto* artifactRow = new wxBoxSizer(wxHORIZONTAL);
    artifactRow->Add(new wxStaticText(componentScroll_, wxID_ANY, "Artifact"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* artifactEnabled = new wxCheckBox(componentScroll_, wxID_ANY, "Enable");
    artifactRow->Add(artifactEnabled, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    artifactRow->Add(new wxStaticText(componentScroll_, wxID_ANY, "Version"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* artifactVersion = new wxComboBox(componentScroll_, wxID_ANY);
    artifactRow->Add(artifactVersion, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    artifactRow->Add(new wxStaticText(componentScroll_, wxID_ANY, "Build"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* artifactBuildType = new wxComboBox(componentScroll_, wxID_ANY);
    artifactRow->Add(artifactBuildType, 1, wxALIGN_CENTER_VERTICAL);
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
