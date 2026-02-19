#pragma once

#include "ConfigModel.h"

#include <wx/frame.h>

#include <cstddef>
#include <vector>

class wxButton;
class wxCheckBox;
class wxComboBox;
class wxCommandEvent;
class wxScrolledWindow;
class wxSizer;
class wxStaticText;

namespace confy {

class MainFrame final : public wxFrame {
public:
    MainFrame();

private:
    struct ComponentRowWidgets {
        wxCheckBox* sourceEnabled{nullptr};
        wxComboBox* sourceBranch{nullptr};
        wxCheckBox* artifactEnabled{nullptr};
        wxComboBox* artifactVersion{nullptr};
        wxComboBox* artifactBuildType{nullptr};
    };

    void OnLoadConfig(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);
    void RenderConfig();
    void AddComponentRow(std::size_t componentIndex);
    void RefreshRowEnabledState(std::size_t componentIndex);

    ConfigModel config_;
    wxStaticText* statusLabel_{nullptr};
    wxScrolledWindow* componentScroll_{nullptr};
    wxSizer* componentListSizer_{nullptr};
    wxButton* applyButton_{nullptr};
    std::vector<ComponentRowWidgets> rows_;
    bool uiUpdating_{false};
};

}  // namespace confy
