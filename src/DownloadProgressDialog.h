#pragma once

#include "DownloadWorkerQueue.h"
#include "JobTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <wx/dialog.h>

class wxButton;
class wxCommandEvent;
class wxGauge;
class wxStaticText;
class wxTimer;
class wxTimerEvent;

namespace confy {

class DownloadProgressDialog final : public wxDialog {
public:
    DownloadProgressDialog(wxWindow* parent, std::vector<NexusDownloadJob> jobs);
    ~DownloadProgressDialog() override;

private:
    enum class RowState {
        Queued,
        Running,
        Completed,
        Failed,
        Cancelled,
    };

    struct ProgressRow {
        wxWindow* container{nullptr};
        wxStaticText* nameLabel{nullptr};
        wxGauge* gauge{nullptr};
        wxStaticText* statusLabel{nullptr};
        wxStaticText* detailLabel{nullptr};
        wxButton* retryButton{nullptr};
        RowState state{RowState::Queued};
    };

    void OnTimer(wxTimerEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnRetryFailed(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnRetryComponent(std::size_t componentIndex);
    void ConsumeWorkerEvents();
    void SetRowState(std::size_t componentIndex,
                     RowState state,
                     const wxString& status,
                     int percent,
                     bool isProgressUpdate = false,
                     std::uint64_t downloadedBytes = 0,
                     const wxString& detail = wxString());
    void QueueRetry(std::size_t componentIndex);
    void UpdateDialogControls();
    bool HasActiveJobs() const;
    bool HasFailedJobs() const;

    std::vector<NexusDownloadJob> jobs_;
    std::vector<ProgressRow> rows_;
    std::unordered_map<std::size_t, std::size_t> rowIndexByComponent_;

    DownloadWorkerQueue worker_{6};
    wxTimer* timer_{nullptr};
    wxButton* cancelButton_{nullptr};
    wxButton* retryFailedButton_{nullptr};
    bool cancelRequested_{false};
};

}  // namespace confy
