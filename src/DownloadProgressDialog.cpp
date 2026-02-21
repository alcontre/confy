#include "DownloadProgressDialog.h"

#include <algorithm>
#include <cmath>

#include <wx/button.h>
#include <wx/control.h>
#include <wx/dcclient.h>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

namespace {

constexpr int kTimerId = wxID_HIGHEST + 250;
constexpr int kNameLabelWidth = 180;
constexpr int kGaugeWidth = 300;
constexpr int kStatusLabelWidth = 420;
constexpr int kDetailLabelWidth = 420;
constexpr int kScrollRateY = 14;
constexpr int kRowMinHeight = 74;
constexpr int kRowsPaneMinHeight = 120;
constexpr int kInitialDialogWidth = 760;
constexpr int kInitialDialogHeight = 420;
constexpr int kMinDialogWidth = 720;
constexpr int kMinDialogHeight = 320;
constexpr std::size_t kMaxEventsPerTick = 64;

std::string FormatDownloadedSize(std::uint64_t bytes) {
    constexpr std::uint64_t kKB = 1000ULL;
    constexpr std::uint64_t kMB = 1000ULL * 1000ULL;
    if (bytes < kMB) {
        auto roundedKB = static_cast<std::uint64_t>(std::llround(static_cast<double>(bytes) / kKB));
        if (bytes > 0 && roundedKB == 0) {
            roundedKB = 1;
        }
        return std::to_string(roundedKB) + " KB";
    }

    const auto roundedMB = static_cast<std::uint64_t>(std::llround(static_cast<double>(bytes) / kMB));
    return std::to_string(roundedMB) + " MB";
}

wxString EllipsizeText(wxStaticText* label, const wxString& text, int maxWidth, wxEllipsizeMode mode) {
    if (label == nullptr || maxWidth <= 0) {
        return text;
    }

    wxClientDC dc(label);
    dc.SetFont(label->GetFont());
    return wxControl::Ellipsize(text, dc, mode, maxWidth);
}

wxString BuildProgressStatus(wxStaticText* label,
                             int filePercent,
                             std::uint64_t downloadedBytes,
                             const wxString& activePath) {
    const wxString prefix =
        wxString::Format("Downloading (%d%%, %s) ",
                         std::clamp(filePercent, 0, 100),
                         wxString::FromUTF8(FormatDownloadedSize(downloadedBytes)));

    if (label == nullptr) {
        return prefix + activePath;
    }

    wxClientDC dc(label);
    dc.SetFont(label->GetFont());
    const int prefixWidth = dc.GetTextExtent(prefix).GetWidth();
    const int pathWidth = std::max(40, kStatusLabelWidth - prefixWidth);
    const wxString truncatedPath = wxControl::Ellipsize(activePath, dc, wxELLIPSIZE_START, pathWidth);
    return prefix + truncatedPath;
}

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

    auto* rowsScrollWindow =
        new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_THEME);
    rowsScrollWindow->SetScrollRate(0, kScrollRateY);
    rowsScrollWindow->SetMinSize(wxSize(-1, kRowsPaneMinHeight));
    auto* rowsPanel = new wxPanel(rowsScrollWindow);
    auto* listSizer = new wxBoxSizer(wxVERTICAL);
    rowsPanel->SetSizer(listSizer);
    auto* scrollSizer = new wxBoxSizer(wxVERTICAL);
    scrollSizer->Add(rowsPanel, 1, wxEXPAND);
    rowsScrollWindow->SetSizer(scrollSizer);

    rows_.reserve(jobs_.size());

    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        const auto& job = jobs_[i];

        auto* rowPanel = new wxPanel(rowsPanel);
        rowPanel->SetMinSize(wxSize(-1, kRowMinHeight));
        auto* rowSizer = new wxBoxSizer(wxHORIZONTAL);
        rowPanel->SetSizer(rowSizer);
        auto* contentSizer = new wxBoxSizer(wxVERTICAL);
        auto* mainLineSizer = new wxBoxSizer(wxHORIZONTAL);

        auto* nameLabel = new wxStaticText(rowPanel, wxID_ANY, job.componentDisplayName);
        nameLabel->SetMinSize(wxSize(kNameLabelWidth, -1));

        auto* gauge = new wxGauge(rowPanel, wxID_ANY, 100, wxDefaultPosition, wxSize(kGaugeWidth, -1));

        auto* statusLabel = new wxStaticText(rowPanel, wxID_ANY, "Queued");
        statusLabel->SetMinSize(wxSize(kStatusLabelWidth, -1));

        auto* detailLabel = new wxStaticText(rowPanel, wxID_ANY, " ");
        detailLabel->SetMinSize(wxSize(kDetailLabelWidth, -1));

        auto* retryButton = new wxButton(rowPanel, wxID_ANY, "Retry");
        retryButton->Disable();

        mainLineSizer->Add(gauge, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        mainLineSizer->Add(retryButton, 0, wxALIGN_CENTER_VERTICAL);

        contentSizer->Add(mainLineSizer, 0, wxEXPAND);
        contentSizer->Add(statusLabel, 0, wxTOP | wxEXPAND, 4);
        contentSizer->Add(detailLabel, 0, wxTOP | wxEXPAND, 2);

        rowSizer->Add(nameLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        rowSizer->Add(contentSizer, 1, wxEXPAND);

        listSizer->Add(rowPanel, 0, wxEXPAND | wxBOTTOM, 6);

        ProgressRow row;
        row.container = rowPanel;
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

    rowsPanel->Layout();
    rowsScrollWindow->FitInside();

    rootSizer->Add(rowsScrollWindow, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* actionSizer = new wxBoxSizer(wxHORIZONTAL);
    retryFailedButton_ = new wxButton(this, wxID_ANY, "Retry Failed");
    retryFailedButton_->Disable();
    actionSizer->Add(retryFailedButton_, 0, wxRIGHT, 8);
    actionSizer->AddStretchSpacer();

    cancelButton_ = new wxButton(this, wxID_CANCEL, "Cancel");
    actionSizer->Add(cancelButton_, 0);

    rootSizer->Add(actionSizer, 0, wxALL | wxEXPAND, 8);

    SetSizer(rootSizer);
    SetMinSize(wxSize(kMinDialogWidth, kMinDialogHeight));
    SetSize(wxSize(kInitialDialogWidth, kInitialDialogHeight));
    Layout();

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
                SetRowState(event.componentIndex,
                            RowState::Running,
                            wxString::FromUTF8(event.message),
                            event.percent,
                            true,
                            event.downloadedBytes);
                break;
            case DownloadEventType::Completed:
                SetRowState(event.componentIndex, RowState::Completed, "Completed", 100);
                break;
            case DownloadEventType::Cancelled:
                SetRowState(event.componentIndex, RowState::Failed, "Cancelled", 0);
                break;
            case DownloadEventType::Failed:
                SetRowState(event.componentIndex,
                            RowState::Failed,
                            "Failed",
                            0,
                            false,
                            0,
                            wxString::FromUTF8(event.message));
                break;
        }
    }

    UpdateDialogControls();
}

void DownloadProgressDialog::SetRowState(std::size_t componentIndex,
                                         RowState state,
                                         const wxString& status,
                                         int percent,
                                         bool isProgressUpdate,
                                         std::uint64_t downloadedBytes,
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
    row.gauge->SetValue(std::clamp(percent, 0, 100));

    if (isProgressUpdate) {
        const wxString fullPath = status;
        row.statusLabel->SetLabelText(BuildProgressStatus(row.statusLabel, percent, downloadedBytes, fullPath));
        row.statusLabel->SetToolTip(fullPath);
    } else {
        row.statusLabel->SetLabelText(EllipsizeText(row.statusLabel, status, kStatusLabelWidth, wxELLIPSIZE_END));
        row.statusLabel->UnsetToolTip();
    }

    if (detail.empty()) {
        row.detailLabel->SetLabelText(" ");
        row.detailLabel->UnsetToolTip();
    } else {
        row.detailLabel->SetLabelText(EllipsizeText(row.detailLabel, detail, kDetailLabelWidth, wxELLIPSIZE_END));
        row.detailLabel->SetToolTip(detail);
    }

    row.retryButton->Enable(state == RowState::Failed && !cancelRequested_);
    if (row.container != nullptr) {
        row.container->Layout();
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
    if (!active && cancelRequested_) {
        cancelRequested_ = false;
    }
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
