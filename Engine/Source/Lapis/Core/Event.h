#pragma once
// Window / input events delivered from the platform layer to the application.
//
//     EventDispatcher d(e);
//     d.Dispatch<WindowResizeEvent>([](WindowResizeEvent& e) { ...; return false; });

#include <functional>
#include <string>

#include "Lapis/Core/Base.h"
#include "Lapis/Core/Log.h"

namespace Lapis {

enum class EventType {
    None = 0,
    WindowClose, WindowResize, WindowFocus,
    KeyPressed, KeyReleased, KeyTyped,
    MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
};

#define LAPIS_EVENT_TYPE(type)                                                   \
    static EventType StaticType() { return EventType::type; }                    \
    EventType GetType() const override { return StaticType(); }                  \
    const char* GetName() const override { return #type; }

class Event {
public:
    virtual ~Event() = default;
    virtual EventType   GetType() const = 0;
    virtual const char* GetName() const = 0;
    virtual std::string ToString() const { return GetName(); }

    bool Handled = false;
};

class WindowCloseEvent : public Event {
public:
    LAPIS_EVENT_TYPE(WindowClose)
};

class WindowResizeEvent : public Event {
public:
    WindowResizeEvent(u32 width, u32 height) : Width(width), Height(height) {}
    std::string ToString() const override { return Format("WindowResize {}x{}", Width, Height); }
    LAPIS_EVENT_TYPE(WindowResize)
    u32 Width, Height;
};

class WindowFocusEvent : public Event {
public:
    explicit WindowFocusEvent(bool focused) : Focused(focused) {}
    LAPIS_EVENT_TYPE(WindowFocus)
    bool Focused;
};

class KeyPressedEvent : public Event {
public:
    KeyPressedEvent(int key, bool repeat) : Key(key), Repeat(repeat) {}
    std::string ToString() const override { return Format("KeyPressed {} (repeat={})", Key, Repeat); }
    LAPIS_EVENT_TYPE(KeyPressed)
    int  Key;
    bool Repeat;
};

class KeyReleasedEvent : public Event {
public:
    explicit KeyReleasedEvent(int key) : Key(key) {}
    LAPIS_EVENT_TYPE(KeyReleased)
    int Key;
};

class KeyTypedEvent : public Event {
public:
    explicit KeyTypedEvent(u32 codepoint) : Codepoint(codepoint) {}
    LAPIS_EVENT_TYPE(KeyTyped)
    u32 Codepoint;
};

class MouseButtonPressedEvent : public Event {
public:
    explicit MouseButtonPressedEvent(int button) : Button(button) {}
    LAPIS_EVENT_TYPE(MouseButtonPressed)
    int Button;
};

class MouseButtonReleasedEvent : public Event {
public:
    explicit MouseButtonReleasedEvent(int button) : Button(button) {}
    LAPIS_EVENT_TYPE(MouseButtonReleased)
    int Button;
};

class MouseMovedEvent : public Event {
public:
    MouseMovedEvent(float x, float y) : X(x), Y(y) {}
    LAPIS_EVENT_TYPE(MouseMoved)
    float X, Y;
};

class MouseScrolledEvent : public Event {
public:
    MouseScrolledEvent(float dx, float dy) : DeltaX(dx), DeltaY(dy) {}
    LAPIS_EVENT_TYPE(MouseScrolled)
    float DeltaX, DeltaY;
};

class EventDispatcher {
public:
    explicit EventDispatcher(Event& event) : m_Event(event) {}

    // Calls fn if the event is of type T. fn returns true to mark it handled.
    template <typename T, typename F>
    bool Dispatch(const F& fn) {
        if (m_Event.GetType() != T::StaticType()) return false;
        m_Event.Handled |= fn(static_cast<T&>(m_Event));
        return true;
    }

private:
    Event& m_Event;
};

using EventCallback = std::function<void(Event&)>;

} // namespace Lapis
