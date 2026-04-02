#include "App.h"

#include "AppInfo.h"
#include "AppSettings.h"
#include "DebugConsole.h"
#include "MainFrame.h"
#include "PickMenuFrame.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

namespace confy {

bool App::OnInit()
{
   SetAppName(APP_NAME);

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
   ShowPickMenu();
   return true;
}

int App::OnExit()
{
   ShutdownDebugLogging();
   return wxApp::OnExit();
}

void App::ShowPickMenu()
{
   if (pickMenuFrame_) {
      pickMenuFrame_->Show(true);
      pickMenuFrame_->Raise();
      SetTopWindow(pickMenuFrame_);
      return;
   }

   pickMenuFrame_ = new PickMenuFrame(
       [this](const wxString &configPath) { OpenMainFrame(configPath); },
       [this]() {
          if (!mainFrame_) {
             ExitMainLoop();
          }
       });

   pickMenuFrame_->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent &) {
      if (pickMenuFrame_) {
         pickMenuFrame_ = nullptr;
      }
   });

   pickMenuFrame_->Show(true);
   SetTopWindow(pickMenuFrame_);
}

void App::OpenMainFrame(const wxString &configPath)
{
   if (configPath.empty()) {
      return;
   }
   if (mainFrame_) {
      return;
   }

   mainFrame_ = new MainFrame(configPath, [this]() { ReturnToPickMenu(); });
   mainFrame_->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent &) {
      mainFrame_ = nullptr;
   });

   if (pickMenuFrame_) {
      pickMenuFrame_->Hide();
   }

   mainFrame_->Show(true);
   mainFrame_->Raise();
   SetTopWindow(mainFrame_);
}

void App::ReturnToPickMenu()
{
   mainFrame_ = nullptr;
   ShowPickMenu();
}

} // namespace confy
