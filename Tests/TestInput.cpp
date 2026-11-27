#include "TestFramework.h"

#include "Lapis/Platform/Input.h"

using namespace Lapis;

// A press and release that both happen inside one slow frame must still be
// seen as a press (and a release) during that frame.
LAPIS_TEST(Input, SubFrameTapIsNotLost) {
    Input::NewFrame();
    Input::SetKey((int)Key::Space, true);
    Input::SetKey((int)Key::Space, false);
    CHECK(Input::WasKeyPressed(Key::Space));
    CHECK(Input::WasKeyReleased(Key::Space));
    CHECK(!Input::IsKeyDown(Key::Space));
    Input::NewFrame();
    CHECK(!Input::WasKeyPressed(Key::Space));

    Input::SetMouseButton((int)MouseButton::Left, true);
    Input::SetMouseButton((int)MouseButton::Left, false);
    CHECK(Input::WasMousePressed(MouseButton::Left));
    Input::NewFrame();
    CHECK(!Input::WasMousePressed(MouseButton::Left));
}

LAPIS_TEST(Input, HeldKeyPressesOnce) {
    Input::NewFrame();
    Input::SetKey((int)Key::W, true);
    CHECK(Input::WasKeyPressed(Key::W));
    Input::NewFrame();
    Input::SetKey((int)Key::W, true);   // OS auto-repeat
    CHECK(Input::IsKeyDown(Key::W) && !Input::WasKeyPressed(Key::W));
    Input::ResetAll();                  // focus lost
    CHECK(!Input::IsKeyDown(Key::W) && Input::WasKeyReleased(Key::W));
    Input::NewFrame();
}

// Characters and key presses keep their order within a frame.
LAPIS_TEST(Input, TextEventsKeepOrder) {
    Input::NewFrame();
    Input::AddKeyPress((int)Key::Enter);
    Input::AddTypedChar('p');
    Input::SetKey((int)Key::Control, true);
    Input::AddKeyPress((int)Key::S);
    Input::SetKey((int)Key::Control, false);
    const auto& ev = Input::GetTextEvents();
    CHECK(ev.size() == 3);
    if (ev.size() != 3) return;
    CHECK(ev[0].Char == 0 && ev[0].KeyCode == Key::Enter && !ev[0].Ctrl);
    CHECK(ev[1].Char == 'p');
    CHECK(ev[2].KeyCode == Key::S && ev[2].Ctrl);
    Input::NewFrame();
    CHECK(Input::GetTextEvents().empty());
}
