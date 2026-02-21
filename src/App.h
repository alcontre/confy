#pragma once

#include <wx/app.h>

namespace confy {

class App final : public wxApp {
public:
    bool OnInit() override;
    int OnExit() override;
};

}  // namespace confy
