#include "DownloadProgressDialog.h"

#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

namespace {

constexpr int kTimerId = wxID_HIGHEST + 250;

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

        auto* nameLabel = new wxStaticText(this, wxID_ANY, job.componentName);
        nameLabel->SetMinSize(wxSize(180, -1));

        auto* gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(320, -1));

        auto* statusLabel = new wxStaticText(this, wxID_ANY, "Queued");
        statusLabel->SetMinSize(wxSize(120, -1));

        rowSizer->Add(nameLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        rowSizer->Add(gauge, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        rowSizer->Add(statusLabel, 0, wxALIGN_CENTER_VERTICAL);

        listSizer->Add(rowSizer, 0, wxEXPAND | wxBOTTOM, 6);

        ProgressRow row;
        row.nameLabel = nameLabel;
        row.gauge = gauge;
        row.statusLabel = statusLabel;

        rows_.push_back(row);
        rowIndexByComponent_[job.componentIndex] = i;
    }

    rootSizer->Add(listSizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    cancelButton_ = new wxButton(this, wxID_CANCEL, "Cancel");
    rootSizer->Add(cancelButton_, 0, wxALL | wxALIGN_RIGHT, 8);

    SetSizerAndFit(rootSizer);
    SetMinSize(wxSize(720, 320));

    Bind(wxEVT_BUTTON, &DownloadProgressDialog::OnCancel, this, wxID_CANCEL);
    Bind(wxEVT_CLOSE_WINDOW, &DownloadProgressDialog::OnClose, this);

    timer_ = new wxTimer(this, kTimerId);
    Bind(wxEVT_TIMER, &DownloadProgressDialog::OnTimer, this, kTimerId);

    worker_.Start();
    for (const auto& job : jobs_) {
        worker_.Submit(job);
    }

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
    if (finished_) {
        EndModal(wxID_OK);
        return;
    }

    if (!cancelRequested_) {
        cancelRequested_ = true;
        cancelButton_->Disable();
        worker_.RequestCancelAll();
    }
}

void DownloadProgressDialog::OnClose(wxCloseEvent& event) {
    if (finished_) {
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

void DownloadProgressDialog::ConsumeWorkerEvents() {
    DownloadEvent event;
    while (worker_.TryPopEvent(event)) {
        const auto it = rowIndexByComponent_.find(event.componentIndex);
        if (it == rowIndexByComponent_.end()) {
            continue;
        }

        auto& row = rows_[it->second];

        switch (event.type) {
            case DownloadEventType::Started:
                row.statusLabel->SetLabelText("Starting");
                row.gauge->SetValue(0);
                break;
            case DownloadEventType::Progress:
                row.statusLabel->SetLabelText("Downloading");
                row.gauge->SetValue(event.percent);
                break;
            case DownloadEventType::Completed:
                row.gauge->SetValue(100);
                MarkFinishedAndCheckCompletion(event.componentIndex, "Completed");
                break;
            case DownloadEventType::Cancelled:
                MarkFinishedAndCheckCompletion(event.componentIndex, "Cancelled");
                break;
            case DownloadEventType::Failed:
                MarkFinishedAndCheckCompletion(event.componentIndex, "Failed");
                break;
        }
    }
}

void DownloadProgressDialog::MarkFinishedAndCheckCompletion(std::size_t componentIndex,
                                                            const wxString& status) {
    const auto it = rowIndexByComponent_.find(componentIndex);
    if (it == rowIndexByComponent_.end()) {
        return;
    }

    auto& row = rows_[it->second];
    row.statusLabel->SetLabelText(status);
    row.finished = true;

    bool allFinished = true;
    for (const auto& item : rows_) {
        if (!item.finished) {
            allFinished = false;
            break;
        }
    }

    if (!allFinished || finished_) {
        return;
    }

    finished_ = true;
    timer_->Stop();
    cancelButton_->Enable();
    cancelButton_->SetLabel("Close");
}

}  // namespace confy
