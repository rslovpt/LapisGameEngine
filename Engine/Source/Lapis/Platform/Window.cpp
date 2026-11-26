#include "Lapis/Platform/Window.h"

#include <cstdlib>
#include <string>

namespace Lapis {

// Default: honour LAPIS_UI_SCALE (useful for testing), otherwise 100%.
float Window::GetContentScale() const {
    if (const char* env = std::getenv("LAPIS_UI_SCALE")) {
        try { const float s = std::stof(env); if (s >= 0.5f && s <= 4.0f) return s; } catch (...) {}
    }
    return 1.0f;
}

} // namespace Lapis
