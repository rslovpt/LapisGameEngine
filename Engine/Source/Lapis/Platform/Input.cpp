#include "Lapis/Platform/Input.h"

#include <cstring>

namespace Lapis {

namespace {
    constexpr int KeyCount   = static_cast<int>(Key::Count);
    constexpr int MouseCount = static_cast<int>(MouseButton::Count);

    struct InputState {
        bool  Keys[KeyCount] = {};
        bool  PrevKeys[KeyCount] = {};
        bool  Mouse[MouseCount] = {};
        bool  PrevMouse[MouseCount] = {};
        Vec2  MousePos;
        Vec2  MouseDelta;
        float Scroll = 0.0f;
        std::string Typed;
        u8   Presses[KeyCount] = {};
        std::vector<Input::TextEvent> TextEvents;
        // Latched transitions: a press + release inside one (slow) frame still
        // counts as a press and a release.
        bool KeyPressed[KeyCount] = {}, KeyReleased[KeyCount] = {};
        bool MousePressed[MouseCount] = {}, MouseReleased[MouseCount] = {};
    };
    InputState s_State;

    bool ValidKey(int k)   { return k >= 0 && k < KeyCount; }
    bool ValidMouse(int b) { return b >= 0 && b < MouseCount; }
}

bool Input::IsKeyDown(Key key)      { int k = static_cast<int>(key); return ValidKey(k) && s_State.Keys[k]; }
bool Input::WasKeyPressed(Key key)  { int k = static_cast<int>(key); return ValidKey(k) && s_State.KeyPressed[k]; }
bool Input::WasKeyReleased(Key key) { int k = static_cast<int>(key); return ValidKey(k) && s_State.KeyReleased[k]; }

bool Input::IsMouseDown(MouseButton b)      { int i = static_cast<int>(b); return ValidMouse(i) && s_State.Mouse[i]; }
bool Input::WasMousePressed(MouseButton b)  { int i = static_cast<int>(b); return ValidMouse(i) && s_State.MousePressed[i]; }
bool Input::WasMouseReleased(MouseButton b) { int i = static_cast<int>(b); return ValidMouse(i) && s_State.MouseReleased[i]; }

Vec2  Input::GetMousePosition() { return s_State.MousePos; }
Vec2  Input::GetMouseDelta()    { return s_State.MouseDelta; }
float Input::GetScrollDelta()   { return s_State.Scroll; }

void Input::NewFrame() {
    std::memcpy(s_State.PrevKeys, s_State.Keys, sizeof(s_State.Keys));
    std::memcpy(s_State.PrevMouse, s_State.Mouse, sizeof(s_State.Mouse));
    s_State.MouseDelta = {};
    s_State.Scroll = 0.0f;
    s_State.Typed.clear();
    std::memset(s_State.Presses, 0, sizeof(s_State.Presses));
    s_State.TextEvents.clear();
    std::memset(s_State.KeyPressed, 0, sizeof(s_State.KeyPressed));
    std::memset(s_State.KeyReleased, 0, sizeof(s_State.KeyReleased));
    std::memset(s_State.MousePressed, 0, sizeof(s_State.MousePressed));
    std::memset(s_State.MouseReleased, 0, sizeof(s_State.MouseReleased));
}

const std::string& Input::GetTypedText() { return s_State.Typed; }
int Input::GetKeyPresses(Key key) { int k = static_cast<int>(key); return ValidKey(k) ? s_State.Presses[k] : 0; }
namespace {
Input::TextEvent ModifierEvent() {
    Input::TextEvent e;
    e.Ctrl = s_State.Keys[(int)Key::Control];
    e.Shift = s_State.Keys[(int)Key::Shift];
    e.Alt = s_State.Keys[(int)Key::Alt];
    return e;
}
constexpr size_t MaxTextEvents = 4096;   // bound memory if nobody reads them
} // namespace

const std::vector<Input::TextEvent>& Input::GetTextEvents() { return s_State.TextEvents; }

void Input::AddKeyPress(int key) {
    if (!ValidKey(key)) return;
    if (s_State.Presses[key] < 255) ++s_State.Presses[key];
    if (s_State.TextEvents.size() < MaxTextEvents) {
        TextEvent e = ModifierEvent();
        e.KeyCode = static_cast<Key>(key);
        s_State.TextEvents.push_back(e);
    }
}

void Input::AddTypedChar(u32 c) {
    if (c < 32 || c == 127 || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) return;
    if (s_State.TextEvents.size() < MaxTextEvents) {
        TextEvent e = ModifierEvent();
        e.Char = c;
        s_State.TextEvents.push_back(e);
    }
    std::string& out = s_State.Typed;
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
}

void Input::SetKey(int key, bool down) {
    if (!ValidKey(key) || s_State.Keys[key] == down) return;   // ignore auto-repeat
    s_State.Keys[key] = down;
    (down ? s_State.KeyPressed : s_State.KeyReleased)[key] = true;
}
void Input::SetMouseButton(int button, bool down) {
    if (!ValidMouse(button) || s_State.Mouse[button] == down) return;
    s_State.Mouse[button] = down;
    (down ? s_State.MousePressed : s_State.MouseReleased)[button] = true;
}
void Input::SetMousePosition(float x, float y)    { s_State.MousePos = {x, y}; }
void Input::AddMouseDelta(float dx, float dy)     { s_State.MouseDelta += Vec2(dx, dy); }
void Input::AddScroll(float delta)                { s_State.Scroll += delta; }

void Input::ResetAll() {
    // Focus loss: release everything (reported as releases, never as presses).
    for (int k = 0; k < KeyCount; ++k) if (s_State.Keys[k]) { s_State.Keys[k] = false; s_State.KeyReleased[k] = true; }
    for (int b = 0; b < MouseCount; ++b) if (s_State.Mouse[b]) { s_State.Mouse[b] = false; s_State.MouseReleased[b] = true; }
}

} // namespace Lapis
