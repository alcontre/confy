#pragma once

#include <wx/window.h>

namespace confy {

void InitializeDebugLogging();
void ShutdownDebugLogging();
void ToggleDebugConsole(wxWindow* parent);
bool IsDebugConsoleVisible();

}  // namespace confy
