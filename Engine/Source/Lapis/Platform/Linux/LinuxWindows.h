#pragma once

#include "Lapis/Platform/Window.h"

namespace Lapis {

Scope<Window> CreateHeadlessWindow(const WindowProps& props);
Scope<Window> CreateX11Window(const WindowProps& props);   // nullptr if X11 is unavailable

} // namespace Lapis
