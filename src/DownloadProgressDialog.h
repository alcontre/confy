#pragma once

#include "DownloadWorkerQueue.h"
#include "JobTypes.h"

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
    struct ProgressRow {
        wxStaticText* nameLabel{nullptr};
        wxGauge* gauge{nullptr};
        wxStaticText* statusLabel{nullptr};
        bool finished{false};
    };

    void OnTimer(wxTimerEvent& event);
    void OnCancel(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void ConsumeWorkerEvents();
    void MarkFinishedAndCheckCompletion(std::size_t componentIndex, const wxString& status);

    std::vector<NexusDownloadJob> jobs_;
    std::vector<ProgressRow> rows_;
    std::unordered_map<std::size_t, std::size_t> rowIndexByComponent_;

    DownloadWorkerQueue worker_{4};
    wxTimer* timer_{nullptr};
    wxButton* cancelButton_{nullptr};
    bool finished_{false};
    bool cancelRequested_{false};
};

}  // namespace confy
