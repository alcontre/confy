#pragma once

#include <wx/string.h>

namespace confy {

inline constexpr const char APP_NAME[]    = "Confy";
inline constexpr const char APP_VERSION[] = "1.0.0";

inline wxString AppTitle()
{
   return wxString::Format("%s v%s", APP_NAME, APP_VERSION);
}

} // namespace confy