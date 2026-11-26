#pragma once
// Platform-independent window with an OpenGL context.
// Window::Create() picks the backend for the current platform:
//   Windows -> Win32Window (WGL)       Linux -> HeadlessWindow (EGL, offscreen)

#include <string>

#include "Lapis/Core/Base.h"
#include "Lapis/Core/Event.h"

namespace Lapis {

struct WindowProps {
    std::string Title  = "Lapis Engine";
    u32         Width  = 1280;
    u32         Height = 720;
    bool        VSync  = true;
    bool        Resizable = true;
    int         MSAASamples = 0;   // backbuffer MSAA (the renderer does its own offscreen MSAA)
};

class Window : NonCopyable {
public:
    virtual ~Window() = default;

    virtual void PollEvents() = 0;
    virtual void SwapBuffers() = 0;

    virtual u32  GetWidth() const = 0;
    virtual u32  GetHeight() const = 0;
    float        GetAspect() const { return GetHeight() ? float(GetWidth()) / float(GetHeight()) : 1.0f; }

    virtual void SetTitle(const std::string& title) = 0;
    virtual void SetVSync(bool enabled) = 0;
    virtual bool IsVSync() const = 0;

    // Hides the cursor and confines it to the window (FPS-style mouse look).
    virtual void SetCursorLocked(bool locked) = 0;
    virtual bool IsCursorLocked() const = 0;

    // Framebuffer that represents "the screen". 0 for real windows; headless
    // backends render into an offscreen FBO instead.
    virtual u32  GetBackbufferFBO() const { return 0; }

    // Monitor DPI scale (1.0 = 96 DPI / 100%, 1.5 = 150% in Windows display
    // settings). Used to size UI so it stays readable and sharp.
    virtual float GetContentScale() const;

    virtual void* GetNativeHandle() const = 0;
    virtual bool  IsHeadless() const { return false; }

    void SetEventCallback(EventCallback cb) { m_EventCallback = std::move(cb); }

    // Returns nullptr if the window or its GL context could not be created.
    static Scope<Window> Create(const WindowProps& props = {});

protected:
    void Emit(Event& e) { if (m_EventCallback) m_EventCallback(e); }
    EventCallback m_EventCallback;
};

} // namespace Lapis
