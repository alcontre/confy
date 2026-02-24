#pragma once

#include "ConfigModel.h"

#include <wx/frame.h>

#include <cstddef>
#include <condition_variable>
#include <deque>
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
class wxSizeEvent;
class wxUpdateUIEvent;
class wxPanel;
class wxScrolledWindow;
class wxSizer;
class wxStaticText;

namespace confy {

class MainFrame final : public wxFrame {
public:
    MainFrame();
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

    void OnLoadConfig(wxCommandEvent& event);
    void OnLoadLastConfig(wxCommandEvent& event);
    void OnLoadFromBitbucket(wxCommandEvent& event);
    void OnSaveAs(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent& event);
    void OnDeselectAll(wxCommandEvent& event);
    void OnCopyConfig(wxCommandEvent& event);
    void OnToggleDebugConsole(wxCommandEvent& event);
    void OnFrameSize(wxSizeEvent& event);
    void OnUpdateSaveAs(wxUpdateUIEvent& event);
    void OnUpdateSelectAll(wxUpdateUIEvent& event);
    void OnUpdateDeselectAll(wxUpdateUIEvent& event);
    void OnUpdateCopyConfig(wxUpdateUIEvent& event);
    void OnUpdateDebugConsole(wxUpdateUIEvent& event);
    void RelayoutComponentArea();
    void RenderConfig();
    void LoadConfigFromPath(const wxString& path);
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
    wxPanel* emptyStatePanel_{nullptr};
    wxButton* loadConfigButton_{nullptr};
    wxButton* loadLastConfigButton_{nullptr};
    wxButton* loadFromBitbucketButton_{nullptr};
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
};

}  // namespace confy
