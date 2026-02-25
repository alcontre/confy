#pragma once

#include <wx/app.h>

namespace confy {

class MainFrame;
class PickMenuFrame;

class App final : public wxApp
{
 public:
   bool OnInit() override;
   int OnExit() override;

 private:
   void ShowPickMenu();
   void OpenMainFrame(const wxString &configPath);
   void ReturnToPickMenu();

   MainFrame *mainFrame_{nullptr};
   PickMenuFrame *pickMenuFrame_{nullptr};
};

} // namespace confy
