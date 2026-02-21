#include "App.h"

#include "DebugConsole.h"
#include "MainFrame.h"

#include <wx/config.h>
#include <wx/fileconf.h>

namespace confy {

bool App::OnInit() {
    // wxConfig::Set() takes ownership of the pointer and deletes it at exit.
    wxConfig::Set(new wxFileConfig(GetAppName()));
    InitializeDebugLogging();
    auto* mainFrame = new MainFrame();
    mainFrame->Show(true);
    return true;
}

int App::OnExit() {
    ShutdownDebugLogging();
    return wxApp::OnExit();
}

}  // namespace confy
