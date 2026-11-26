#pragma once
// Polling-style input. Key codes match Win32 virtual-key codes.
//
//     if (Input::IsKeyDown(Key::W)) ...
//     if (Input::WasKeyPressed(Key::Space)) ...   // only on the frame it went down

#include <string>

#include "Lapis/Core/Base.h"
#include "Lapis/Math/Math.h"

namespace Lapis {

enum class Key : int {
    Backspace = 0x08, Tab = 0x09, Enter = 0x0D, Shift = 0x10, Control = 0x11, Alt = 0x12,
    Pause = 0x13, CapsLock = 0x14, Escape = 0x1B, Space = 0x20,
    PageUp = 0x21, PageDown = 0x22, End = 0x23, Home = 0x24,
    Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28, Insert = 0x2D, Delete = 0x2E,
    D0 = '0', D1, D2, D3, D4, D5, D6, D7, D8, D9,
    A = 'A', B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count = 256
};

enum class MouseButton : int { Left = 0, Right = 1, Middle = 2, X1 = 3, X2 = 4, Count = 5 };

class Input {
public:
    static bool IsKeyDown(Key key);
    static bool WasKeyPressed(Key key);   // went down this frame
    static bool WasKeyReleased(Key key);  // went up this frame

    static bool IsMouseDown(MouseButton button);
    static bool WasMousePressed(MouseButton button);
    static bool WasMouseReleased(MouseButton button);

    // Text typed this frame (UTF-8, printable characters only) and key
    // presses including OS auto-repeat (for text editing).
    static const std::string& GetTypedText();
    static int  GetKeyPresses(Key key);

    static Vec2  GetMousePosition();  // window pixels, origin top-left
    static Vec2  GetMouseDelta();     // raw movement this frame
    static float GetScrollDelta();    // wheel notches this frame

    // --- called by the platform layer / main loop ---------------------------
    static void NewFrame();           // must run before polling window events
    static void SetKey(int key, bool down);
    static void AddKeyPress(int key);          // every press, including repeats
    static void AddTypedChar(u32 codepoint);
    static void SetMouseButton(int button, bool down);
    static void SetMousePosition(float x, float y);
    static void AddMouseDelta(float dx, float dy);
    static void AddScroll(float delta);
    static void ResetAll();           // e.g. when the window loses focus
};

} // namespace Lapis
