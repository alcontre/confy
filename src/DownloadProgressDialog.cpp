#include "DownloadProgressDialog.h"

#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

namespace {

constexpr int kTimerId = wxID_HIGHEST + 250;
constexpr int kStatusLabelWidth = 420;
constexpr std::size_t kMaxEventsPerTick = 64;

}  // namespace

namespace confy {

DownloadProgressDialog::DownloadProgressDialog(wxWindow* parent, std::vector<NexusDownloadJob> jobs)
    : wxDialog(parent,
               wxID_ANY,
               "Download Progress",
               wxDefaultPosition,
               wxSize(760, 420),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      jobs_(std::move(jobs)) {
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxStaticText(this, wxID_ANY, "Downloading selected components...");
    rootSizer->Add(header, 0, wxALL | wxEXPAND, 8);

    auto* listSizer = new wxBoxSizer(wxVERTICAL);

    rows_.reserve(jobs_.size());

    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        const auto& job = jobs_[i];

        auto* rowSizer = new wxBoxSizer(wxHORIZONTAL);
        auto* contentSizer = new wxBoxSizer(wxVERTICAL);
        auto* mainLineSizer = new wxBoxSizer(wxHORIZONTAL);

        auto* nameLabel = new wxStaticText(this, wxID_ANY, job.componentName);
        nameLabel->SetMinSize(wxSize(180, -1));

        auto* gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(320, -1));

        auto* statusLabel = new wxStaticText(this, wxID_ANY, "Queued");
        statusLabel->SetMinSize(wxSize(kStatusLabelWidth, -1));
        statusLabel->Wrap(kStatusLabelWidth);

        auto* detailLabel = new wxStaticText(this, wxID_ANY, "");
        detailLabel->SetMinSize(wxSize(320, -1));
        detailLabel->Wrap(420);
        detailLabel->Hide();

        auto* retryButton = new wxButton(this, wxID_ANY, "Retry");
        retryButton->Disable();

        mainLineSizer->Add(gauge, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        mainLineSizer->Add(retryButton, 0, wxALIGN_CENTER_VERTICAL);

        contentSizer->Add(mainLineSizer, 0, wxEXPAND);
        contentSizer->Add(statusLabel, 0, wxTOP | wxEXPAND, 4);
        contentSizer->Add(detailLabel, 0, wxTOP | wxEXPAND, 2);

        rowSizer->Add(nameLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        rowSizer->Add(contentSizer, 1, wxEXPAND);

        listSizer->Add(rowSizer, 0, wxEXPAND | wxBOTTOM, 6);

        ProgressRow row;
        row.nameLabel = nameLabel;
        row.gauge = gauge;
        row.statusLabel = statusLabel;
        row.detailLabel = detailLabel;
        row.retryButton = retryButton;

        rows_.push_back(row);
        rowIndexByComponent_[job.componentIndex] = i;

        retryButton->Bind(wxEVT_BUTTON,
                          [this, componentIndex = job.componentIndex](wxCommandEvent&) {
                              OnRetryComponent(componentIndex);
                          });
    }

    rootSizer->Add(listSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* actionSizer = new wxBoxSizer(wxHORIZONTAL);
    retryFailedButton_ = new wxButton(this, wxID_ANY, "Retry Failed");
    retryFailedButton_->Disable();
    actionSizer->Add(retryFailedButton_, 0, wxRIGHT, 8);
    actionSizer->AddStretchSpacer();

    cancelButton_ = new wxButton(this, wxID_CANCEL, "Cancel");
    actionSizer->Add(cancelButton_, 0);

    rootSizer->Add(actionSizer, 0, wxALL | wxEXPAND, 8);

    SetSizerAndFit(rootSizer);
    SetMinSize(wxSize(720, 320));

    Bind(wxEVT_BUTTON, &DownloadProgressDialog::OnCancel, this, wxID_CANCEL);
    retryFailedButton_->Bind(wxEVT_BUTTON, &DownloadProgressDialog::OnRetryFailed, this);
    Bind(wxEVT_CLOSE_WINDOW, &DownloadProgressDialog::OnClose, this);

    timer_ = new wxTimer(this, kTimerId);
    Bind(wxEVT_TIMER, &DownloadProgressDialog::OnTimer, this, kTimerId);

    worker_.Start();
    for (const auto& job : jobs_) {
        worker_.Submit(job);
    }

    UpdateDialogControls();
    timer_->Start(100);
}

DownloadProgressDialog::~DownloadProgressDialog() {
    if (timer_ != nullptr) {
        timer_->Stop();
    }
    worker_.Stop();
}

void DownloadProgressDialog::OnTimer(wxTimerEvent&) {
    ConsumeWorkerEvents();
}

void DownloadProgressDialog::OnCancel(wxCommandEvent&) {
    if (!HasActiveJobs()) {
        if (timer_ != nullptr) {
            timer_->Stop();
        }
        EndModal(wxID_OK);
        return;
    }

    if (!cancelRequested_) {
        cancelRequested_ = true;
        cancelButton_->Disable();
        worker_.RequestCancelAll();
    }

    UpdateDialogControls();
}

void DownloadProgressDialog::OnRetryFailed(wxCommandEvent&) {
    if (cancelRequested_ || HasActiveJobs()) {
        return;
    }

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].state == RowState::Failed) {
            QueueRetry(jobs_[i].componentIndex);
        }
    }

    UpdateDialogControls();
}

void DownloadProgressDialog::OnClose(wxCloseEvent& event) {
    if (!HasActiveJobs()) {
        if (timer_ != nullptr) {
            timer_->Stop();
        }
        event.Skip();
        return;
    }

    if (!cancelRequested_) {
        cancelRequested_ = true;
        cancelButton_->Disable();
        worker_.RequestCancelAll();
    }

    event.Veto();
}

void DownloadProgressDialog::OnRetryComponent(std::size_t componentIndex) {
    if (cancelRequested_ || HasActiveJobs()) {
        return;
    }

    QueueRetry(componentIndex);
    UpdateDialogControls();
}

void DownloadProgressDialog::ConsumeWorkerEvents() {
    DownloadEvent event;
    std::size_t processed = 0;
    while (processed < kMaxEventsPerTick && worker_.TryPopEvent(event)) {
        ++processed;
        switch (event.type) {
            case DownloadEventType::Started:
                SetRowState(event.componentIndex, RowState::Running, "Starting", 0);
                break;
            case DownloadEventType::Progress:
                if (!event.message.empty()) {
                    SetRowState(event.componentIndex,
                                RowState::Running,
                                wxString::FromUTF8(event.message),
                                event.percent);
                } else {
                    SetRowState(event.componentIndex, RowState::Running, "Downloading", event.percent);
                }
                break;
            case DownloadEventType::Completed:
                SetRowState(event.componentIndex, RowState::Completed, "Completed", 100);
                break;
            case DownloadEventType::Cancelled:
                SetRowState(event.componentIndex, RowState::Cancelled, "Cancelled", 0);
                break;
            case DownloadEventType::Failed:
                if (!event.message.empty()) {
                    SetRowState(event.componentIndex,
                                RowState::Failed,
                                "Failed",
                                0,
                                wxString::FromUTF8(event.message));
                } else {
                    SetRowState(event.componentIndex, RowState::Failed, "Failed", 0);
                }
                break;
        }
    }

    UpdateDialogControls();
}

void DownloadProgressDialog::SetRowState(std::size_t componentIndex,
                                         RowState state,
                                         const wxString& status,
                                         int percent,
                                         const wxString& detail) {
    const auto it = rowIndexByComponent_.find(componentIndex);
    if (it == rowIndexByComponent_.end()) {
        return;
    }

    if (it->second >= rows_.size()) {
        return;
    }

    auto& row = rows_[it->second];

    if (row.statusLabel == nullptr || row.gauge == nullptr || row.retryButton == nullptr ||
        row.detailLabel == nullptr) {
        return;
    }

    row.state = state;
    row.statusLabel->SetLabelText(status);
    row.statusLabel->Wrap(kStatusLabelWidth);
    row.gauge->SetValue(percent);
    const bool wasDetailVisible = row.detailLabel->IsShown();
    row.detailLabel->SetLabelText(detail);
    if (detail.empty()) {
        row.detailLabel->Hide();
    } else {
        row.detailLabel->Wrap(420);
        row.detailLabel->Show();
    }
    row.retryButton->Enable(state == RowState::Failed && !cancelRequested_);
    if (wasDetailVisible != row.detailLabel->IsShown()) {
        Layout();
    }
}

void DownloadProgressDialog::QueueRetry(std::size_t componentIndex) {
    const auto it = rowIndexByComponent_.find(componentIndex);
    if (it == rowIndexByComponent_.end()) {
        return;
    }

    if (it->second >= rows_.size() || it->second >= jobs_.size()) {
        return;
    }

    auto& row = rows_[it->second];
    if (row.state != RowState::Failed) {
        return;
    }

    SetRowState(componentIndex, RowState::Queued, "Queued", 0);
    worker_.Submit(jobs_[it->second]);
}

bool DownloadProgressDialog::HasActiveJobs() const {
    for (const auto& row : rows_) {
        if (row.state == RowState::Queued || row.state == RowState::Running) {
            return true;
        }
    }

    return false;
}

bool DownloadProgressDialog::HasFailedJobs() const {
    for (const auto& row : rows_) {
        if (row.state == RowState::Failed) {
            return true;
        }
    }

    return false;
}

void DownloadProgressDialog::UpdateDialogControls() {
    const bool active = HasActiveJobs();
    const bool failed = HasFailedJobs();

    for (auto& row : rows_) {
        if (row.retryButton != nullptr) {
            row.retryButton->Enable(!cancelRequested_ && !active && row.state == RowState::Failed);
        }
    }

    if (retryFailedButton_ != nullptr) {
        retryFailedButton_->Enable(!cancelRequested_ && !active && failed);
    }

    if (active) {
        if (cancelButton_ != nullptr) {
            cancelButton_->SetLabel("Cancel");
            cancelButton_->Enable(!cancelRequested_);
        }
        return;
    }

    if (cancelButton_ != nullptr) {
        cancelButton_->SetLabel("Close");
        cancelButton_->Enable(true);
    }
}

}  // namespace confy
