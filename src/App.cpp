#include "App.h"

#include "DebugConsole.h"
#include "MainFrame.h"

namespace confy {

bool App::OnInit() {
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
