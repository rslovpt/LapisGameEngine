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
    };
    InputState s_State;

    bool ValidKey(int k)   { return k >= 0 && k < KeyCount; }
    bool ValidMouse(int b) { return b >= 0 && b < MouseCount; }
}

bool Input::IsKeyDown(Key key)      { int k = static_cast<int>(key); return ValidKey(k) && s_State.Keys[k]; }
bool Input::WasKeyPressed(Key key)  { int k = static_cast<int>(key); return ValidKey(k) && s_State.Keys[k] && !s_State.PrevKeys[k]; }
bool Input::WasKeyReleased(Key key) { int k = static_cast<int>(key); return ValidKey(k) && !s_State.Keys[k] && s_State.PrevKeys[k]; }

bool Input::IsMouseDown(MouseButton b)      { int i = static_cast<int>(b); return ValidMouse(i) && s_State.Mouse[i]; }
bool Input::WasMousePressed(MouseButton b)  { int i = static_cast<int>(b); return ValidMouse(i) && s_State.Mouse[i] && !s_State.PrevMouse[i]; }
bool Input::WasMouseReleased(MouseButton b) { int i = static_cast<int>(b); return ValidMouse(i) && !s_State.Mouse[i] && s_State.PrevMouse[i]; }

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
}

const std::string& Input::GetTypedText() { return s_State.Typed; }
int Input::GetKeyPresses(Key key) { int k = static_cast<int>(key); return ValidKey(k) ? s_State.Presses[k] : 0; }
void Input::AddKeyPress(int key) { if (ValidKey(key) && s_State.Presses[key] < 255) ++s_State.Presses[key]; }

void Input::AddTypedChar(u32 c) {
    if (c < 32 || c == 127 || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) return;
    std::string& out = s_State.Typed;
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
}

void Input::SetKey(int key, bool down)            { if (ValidKey(key)) s_State.Keys[key] = down; }
void Input::SetMouseButton(int button, bool down) { if (ValidMouse(button)) s_State.Mouse[button] = down; }
void Input::SetMousePosition(float x, float y)    { s_State.MousePos = {x, y}; }
void Input::AddMouseDelta(float dx, float dy)     { s_State.MouseDelta += Vec2(dx, dy); }
void Input::AddScroll(float delta)                { s_State.Scroll += delta; }

void Input::ResetAll() {
    std::memset(s_State.Keys, 0, sizeof(s_State.Keys));
    std::memset(s_State.Mouse, 0, sizeof(s_State.Mouse));
}

} // namespace Lapis
