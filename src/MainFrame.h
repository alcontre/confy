#pragma once

#include "ConfigModel.h"

#include <wx/frame.h>

#include <cstddef>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class wxButton;
class wxCheckBox;
class wxComboBox;
class wxCommandEvent;
class wxCloseEvent;
class wxSizeEvent;
class wxUpdateUIEvent;
class wxPanel;
class wxScrolledWindow;
class wxSizer;
class wxStaticText;

namespace confy {

class MainFrame final : public wxFrame {
public:
    MainFrame(const wxString& initialConfigPath, std::function<void()> onReturnToPicker);
    ~MainFrame() override;

private:
    struct ComponentRowWidgets {
        wxCheckBox* sourceEnabled{nullptr};
        wxCheckBox* sourceShallow{nullptr};
        wxComboBox* sourceBranch{nullptr};
        wxCheckBox* artifactEnabled{nullptr};
        wxComboBox* artifactVersion{nullptr};
        wxComboBox* artifactBuildType{nullptr};
    };

    void OnCloseConfig(wxCommandEvent& event);
    void OnReloadConfig(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnCloseWindow(wxCloseEvent& event);
    void OnSaveAs(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent& event);
    void OnDeselectAll(wxCommandEvent& event);
    void OnCopyConfig(wxCommandEvent& event);
    void OnToggleDebugConsole(wxCommandEvent& event);
    void OnUpdateSaveAs(wxUpdateUIEvent& event);
    void OnUpdateSelectAll(wxUpdateUIEvent& event);
    void OnUpdateDeselectAll(wxUpdateUIEvent& event);
    void OnUpdateCopyConfig(wxUpdateUIEvent& event);
    void OnUpdateDebugConsole(wxUpdateUIEvent& event);
    void OnFrameSize(wxSizeEvent& event);
    void RelayoutComponentArea();
    void RenderConfig();
    bool LoadConfigFromPath(const wxString& path);
    void AddComponentRow(std::size_t componentIndex);
    void UpdateComboTooltip(wxComboBox& comboBox);
    void UpdateRowTooltips(std::size_t componentIndex);
    void RefreshRowEnabledState(std::size_t componentIndex);
    void StartMetadataWorkers();
    void StopMetadataWorkers();
    void EnqueueVersionFetch(std::size_t componentIndex, bool prioritize);
    void EnqueueBuildTypeFetch(std::size_t componentIndex, const std::string& version);
    void EnqueueSourceRefsFetch(std::size_t componentIndex, bool prioritize);
    void MetadataWorkerLoop();

    enum class MetadataTaskType { SourceRefs, Versions, BuildTypes };
    struct MetadataTask {
        MetadataTaskType type{MetadataTaskType::Versions};
        std::size_t componentIndex{0};
        std::string version;
    };
    struct ComponentMetadataState {
        bool sourceRefsLoading{false};
        bool sourceRefsLoaded{false};
        bool versionsLoading{false};
        bool versionsLoaded{false};
        std::unordered_map<std::string, std::vector<std::string>> buildTypesByVersion;
        std::unordered_set<std::string> buildTypesLoadingVersions;
    };

    ConfigModel config_;
    wxStaticText* statusLabel_{nullptr};
    wxScrolledWindow* componentScroll_{nullptr};
    wxPanel* componentContentPanel_{nullptr};
    wxSizer* componentListSizer_{nullptr};
    wxButton* applyButton_{nullptr};
    std::vector<ComponentRowWidgets> rows_;
    std::vector<ComponentMetadataState> metadataState_;
    std::vector<std::string> componentSourceRequests_;
    std::vector<std::pair<std::string, std::string>> componentArtifactRequests_;
    std::string loadedConfigPath_;
    bool uiUpdating_{false};

    std::mutex metadataMutex_;
    std::condition_variable metadataCv_;
    std::deque<MetadataTask> metadataTasks_;
    std::unordered_set<std::string> metadataTaskKeys_;
    std::vector<std::thread> metadataWorkers_;
    bool stopMetadataWorkers_{false};
    bool exitRequested_{false};
    std::function<void()> onReturnToPicker_;
};

}  // namespace confy
