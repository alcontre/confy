#include "App.h"

#include "AppSettings.h"
#include "DebugConsole.h"
#include "MainFrame.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace confy {

bool App::OnInit() {
    wxString executableDir;
    const auto executablePath = wxStandardPaths::Get().GetExecutablePath();
    if (!executablePath.empty()) {
        wxFileName executableFile(executablePath);
        if (executableFile.IsOk()) {
            executableDir = executableFile.GetPath();
        }
    }
    if (executableDir.empty()) {
        executableDir = wxFileName::GetCwd();
    }
    AppSettings::Initialize(executableDir.ToStdString());

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
