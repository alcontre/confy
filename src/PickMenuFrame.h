#pragma once

#include <wx/frame.h>

#include <functional>

class wxButton;
class wxCloseEvent;
class wxCommandEvent;

namespace confy {

class PickMenuFrame final : public wxFrame {
public:
    PickMenuFrame(std::function<void(const wxString&)> onConfigChosen,
                  std::function<void()> onExitRequested);

private:
    void OnLoadFile(wxCommandEvent& event);
    void OnLoadLast(wxCommandEvent& event);
    void OnLoadFromBitbucket(wxCommandEvent& event);
    void OnCloseWindow(wxCloseEvent& event);
    void RefreshLastPathState();

    wxButton* loadLastButton_{nullptr};
    wxButton* loadFromBitbucketButton_{nullptr};
    std::function<void(const wxString&)> onConfigChosen_;
    std::function<void()> onExitRequested_;
};

}  // namespace confy
