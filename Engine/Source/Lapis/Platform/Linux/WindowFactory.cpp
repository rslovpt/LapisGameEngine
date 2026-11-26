// Linux backend selection:
//   LAPIS_HEADLESS=1 or no $DISPLAY  -> offscreen EGL (CI / automated tests)
//   otherwise                         -> X11 window with an EGL context
#include <cstdlib>
#include <cstring>

#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Linux/LinuxWindows.h"

namespace Lapis {

Scope<Window> Window::Create(const WindowProps& props) {
    const char* headless = std::getenv("LAPIS_HEADLESS");
    const char* display = std::getenv("DISPLAY");
    const bool forceHeadless = headless && std::strcmp(headless, "0") != 0;

    if (!forceHeadless && display && *display) {
        if (auto window = CreateX11Window(props)) return window;
        LAPIS_WARN("X11 window creation failed - falling back to headless rendering");
    }
    return CreateHeadlessWindow(props);
}

} // namespace Lapis
