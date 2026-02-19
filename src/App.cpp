#include "App.h"

#include "MainFrame.h"

namespace confy {

bool App::OnInit() {
    auto* mainFrame = new MainFrame();
    mainFrame->Show(true);
    return true;
}

}  // namespace confy
