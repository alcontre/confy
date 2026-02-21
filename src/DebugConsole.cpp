#include "DebugConsole.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/frame.h>
#include <wx/log.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/thread.h>
#include <wx/weakref.h>
#include <wx/window.h>

#include <mutex>
#include <vector>

namespace confy {
namespace {

class DebugConsoleFrame final : public wxFrame {
public:
    explicit DebugConsoleFrame(wxWindow* parent)
        : wxFrame(parent, wxID_ANY, "Debug Console", wxDefaultPosition, wxSize(900, 360)) {
        auto* rootSizer = new wxBoxSizer(wxVERTICAL);

        logText_ = new wxTextCtrl(this,
                                  wxID_ANY,
                                  "",
                                  wxDefaultPosition,
                                  wxDefaultSize,
                                  wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
        rootSizer->Add(logText_, 1, wxALL | wxEXPAND, 8);

        auto* controlsSizer = new wxBoxSizer(wxHORIZONTAL);
        autoScrollCheck_ = new wxCheckBox(this, wxID_ANY, "Auto-scroll");
        autoScrollCheck_->SetValue(true);
        controlsSizer->Add(autoScrollCheck_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        auto* clearButton = new wxButton(this, wxID_CLEAR, "Clear");
        controlsSizer->Add(clearButton, 0, wxALIGN_CENTER_VERTICAL);
        controlsSizer->AddStretchSpacer();

        rootSizer->Add(controlsSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

        SetSizer(rootSizer);

        clearButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            logText_->Clear();
        });

        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (event.CanVeto()) {
                Hide();
                event.Veto();
                return;
            }
            event.Skip();
        });
    }

    void AppendLogLine(const wxString& line) {
        if (logText_ == nullptr) {
            return;
        }
        logText_->AppendText(line);
        logText_->AppendText("\n");

        if (autoScrollCheck_ != nullptr && autoScrollCheck_->GetValue()) {
            const auto lastPosition = logText_->GetLastPosition();
            logText_->ShowPosition(lastPosition);
        }
    }

    void SetLogLines(const std::vector<wxString>& lines) {
        if (logText_ == nullptr) {
            return;
        }
        logText_->Clear();
        for (const auto& line : lines) {
            logText_->AppendText(line);
            logText_->AppendText("\n");
        }

        if (autoScrollCheck_ != nullptr && autoScrollCheck_->GetValue()) {
            const auto lastPosition = logText_->GetLastPosition();
            logText_->ShowPosition(lastPosition);
        }
    }

private:
    wxTextCtrl* logText_{nullptr};
    wxCheckBox* autoScrollCheck_{nullptr};
};

class DebugLogTarget final : public wxLog {
public:
    void AttachConsole(DebugConsoleFrame* consoleFrame) {
        std::vector<wxString> snapshot;
        {
            std::scoped_lock lock(mutex_);
            consoleFrame_ = consoleFrame;
            snapshot = lines_;
        }

        if (consoleFrame != nullptr) {
            consoleFrame->SetLogLines(snapshot);
        }
    }

    bool IsConsoleVisible() const {
        std::scoped_lock lock(mutex_);
        return consoleFrame_ != nullptr && consoleFrame_->IsShown();
    }

protected:
    void DoLogText(const wxString& msg) override {
        wxWeakRef<DebugConsoleFrame> consoleFrame;
        {
            std::scoped_lock lock(mutex_);
            lines_.push_back(msg);
            consoleFrame = consoleFrame_;
        }

        if (consoleFrame == nullptr) {
            return;
        }

        if (wxIsMainThread()) {
            consoleFrame->AppendLogLine(msg);
            return;
        }

        if (wxTheApp == nullptr) {
            return;
        }

        wxTheApp->CallAfter([consoleFrame, msg]() {
            if (consoleFrame != nullptr) {
                consoleFrame->AppendLogLine(msg);
            }
        });
    }

private:
    mutable std::mutex mutex_;
    std::vector<wxString> lines_;
    DebugConsoleFrame* consoleFrame_{nullptr};
};

DebugLogTarget* g_debugLogTarget = nullptr;
wxLog* g_previousLogTarget = nullptr;
DebugConsoleFrame* g_consoleFrame = nullptr;

void EnsureConsoleFrame(wxWindow* parent) {
    if (g_consoleFrame == nullptr) {
        g_consoleFrame = new DebugConsoleFrame(parent);
        g_consoleFrame->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent& event) {
            if (event.GetEventObject() == g_consoleFrame) {
                g_consoleFrame = nullptr;
            }
            event.Skip();
        });
    }
    if (g_debugLogTarget != nullptr) {
        g_debugLogTarget->AttachConsole(g_consoleFrame);
    }
}

}  // namespace

void InitializeDebugLogging() {
    if (g_debugLogTarget != nullptr) {
        return;
    }

    g_debugLogTarget = new DebugLogTarget();
    g_previousLogTarget = wxLog::SetActiveTarget(g_debugLogTarget);
}

void ShutdownDebugLogging() {
    if (g_debugLogTarget == nullptr) {
        return;
    }

    wxLog::SetActiveTarget(g_previousLogTarget);
    g_previousLogTarget = nullptr;

    delete g_debugLogTarget;
    g_debugLogTarget = nullptr;

    if (g_consoleFrame != nullptr) {
        g_consoleFrame->Destroy();
        g_consoleFrame = nullptr;
    }
}

void ToggleDebugConsole(wxWindow* parent) {
    EnsureConsoleFrame(parent);
    if (g_consoleFrame == nullptr) {
        return;
    }

    if (g_consoleFrame->IsShown()) {
        g_consoleFrame->Hide();
    } else {
        g_consoleFrame->Show(true);
        g_consoleFrame->Raise();
    }
}

bool IsDebugConsoleVisible() {
    if (g_debugLogTarget != nullptr) {
        return g_debugLogTarget->IsConsoleVisible();
    }
    return false;
}

}  // namespace confy
