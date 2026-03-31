#pragma once

#include "ConfigModel.h"

#include <wx/frame.h>

#include <condition_variable>
#include <cstddef>
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

class MainFrame final : public wxFrame
{
 public:
   MainFrame(const wxString &initialConfigPath, std::function<void()> onReturnToPicker);
   ~MainFrame() override;

 private:
   struct ComponentRowWidgets
   {
      wxCheckBox *sourceEnabled{nullptr};
      wxCheckBox *sourceShallow{nullptr};
      wxComboBox *sourceBranch{nullptr};
      wxCheckBox *artifactEnabled{nullptr};
      wxComboBox *artifactVersion{nullptr};
      wxComboBox *artifactBuildType{nullptr};
   };

   struct ComponentRowBaseline
   {
      std::string sourceBranch;
      std::string artifactVersion;
      std::string artifactBuildType;
   };

   struct ComponentRowChangeState
   {
      wxPanel *modifiedIndicator{nullptr};
      bool sourceBranchTouched{false};
      bool artifactVersionTouched{false};
      bool artifactBuildTypeTouched{false};
   };

   void OnCloseConfig(wxCommandEvent &event);
   void OnReloadConfig(wxCommandEvent &event);
   void OnExit(wxCommandEvent &event);
   void OnCloseWindow(wxCloseEvent &event);
   void OnSaveAs(wxCommandEvent &event);
   void OnApply(wxCommandEvent &event);
   void OnSelectAll(wxCommandEvent &event);
   void OnDeselectAll(wxCommandEvent &event);
   void OnCopyConfig(wxCommandEvent &event);
   void OnToggleDebugConsole(wxCommandEvent &event);
   void OnUpdateSaveAs(wxUpdateUIEvent &event);
   void OnUpdateSelectAll(wxUpdateUIEvent &event);
   void OnUpdateDeselectAll(wxUpdateUIEvent &event);
   void OnUpdateCopyConfig(wxUpdateUIEvent &event);
   void OnUpdateDebugConsole(wxUpdateUIEvent &event);
   void OnFrameSize(wxSizeEvent &event);
   void RelayoutComponentArea();
   void RenderConfig();
   bool LoadConfigFromPath(const wxString &path);
   void AddComponentRow(std::size_t componentIndex);
   void UpdateComboTooltip(wxComboBox &comboBox);
   void UpdateRowTooltips(std::size_t componentIndex);
   void RefreshRowModifiedIndicator(std::size_t componentIndex);
   void RefreshRowEnabledState(std::size_t componentIndex);
   void RefreshArtifactBuildTypes(std::size_t componentIndex, const std::string &version);
   void StartMetadataWorkers();
   void StopMetadataWorkers();
   void EnqueueVersionFetch(std::size_t componentIndex, bool prioritize);
   void EnqueueBuildTypeFetch(std::size_t componentIndex, const std::string &version);
   void EnqueueSourceRefsFetch(std::size_t componentIndex, bool prioritize);
   void MetadataWorkerLoop();

   enum class MetadataTaskType
   {
      SourceRefs,
      Versions,
      BuildTypes
   };
   struct MetadataTask
   {
      MetadataTaskType type{MetadataTaskType::Versions};
      std::size_t componentIndex{0};
      std::string version;
   };
   struct ComponentMetadataState
   {
      bool sourceRefsLoading{false};
      bool sourceRefsLoaded{false};
      bool versionsLoading{false};
      bool versionsLoaded{false};
      std::unordered_map<std::string, std::vector<std::string>> buildTypesByVersion;
      std::unordered_set<std::string> buildTypesLoadingVersions;
   };

   ConfigModel config_;
   wxScrolledWindow *componentScroll_{nullptr};
   wxPanel *componentContentPanel_{nullptr};
   wxSizer *componentListSizer_{nullptr};
   wxButton *applyButton_{nullptr};
   std::vector<ComponentRowWidgets> rows_;
   std::vector<ComponentRowBaseline> rowBaselines_;
   std::vector<ComponentRowChangeState> rowChangeState_;
   std::unordered_set<const wxComboBox *> openComboDropdowns_;
   // GUI-thread state: workers never mutate these directly. Worker results are
   // marshaled back to the main event loop via CallAfter before touching them.
   std::vector<ComponentMetadataState> metadataState_;

   // Per-render request snapshots indexed by component. Workers read from these
   // after dequeuing a task; RenderConfig rebuilds them only between
   // StopMetadataWorkers()/StartMetadataWorkers() boundaries.
   std::vector<std::string> componentSourceRequests_;
   std::vector<std::pair<std::string, std::string>> componentArtifactRequests_;
   std::string loadedConfigPath_;
   bool uiUpdating_{false};

   // Cross-thread coordination for metadata workers:
   // - metadataTasks_/metadataTaskKeys_/stopMetadataWorkers_ are protected by metadataMutex_.
   // - metadataTaskKeys_ deduplicates queued work (not currently executing work).
   // - metadataCv_ wakes worker threads when new tasks arrive or shutdown begins.
   std::mutex metadataMutex_;
   std::condition_variable metadataCv_;
   std::deque<MetadataTask> metadataTasks_;
   std::unordered_set<std::string> metadataTaskKeys_;
   std::vector<std::thread> metadataWorkers_;
   bool stopMetadataWorkers_{false};
   bool exitRequested_{false};
   std::function<void()> onReturnToPicker_;
};

} // namespace confy
