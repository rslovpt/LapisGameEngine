// Native Linux window: Xlib window + EGL OpenGL 4.x core context.
#include "Lapis/Platform/Linux/LinuxWindows.h"

#if LAPIS_HAS_X11

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Input.h"
#include "Lapis/Platform/Linux/EGL.h"
#include "Lapis/Renderer/GL.h"

namespace Lapis {

namespace {
    int TranslateKey(KeySym sym) {
        if (sym >= XK_a && sym <= XK_z) return 'A' + int(sym - XK_a);
        if (sym >= XK_A && sym <= XK_Z) return 'A' + int(sym - XK_A);
        if (sym >= XK_0 && sym <= XK_9) return '0' + int(sym - XK_0);
        if (sym >= XK_F1 && sym <= XK_F12) return int(Key::F1) + int(sym - XK_F1);
        switch (sym) {
            case XK_space:     return int(Key::Space);
            case XK_Return:    return int(Key::Enter);
            case XK_Escape:    return int(Key::Escape);
            case XK_Tab:       return int(Key::Tab);
            case XK_BackSpace: return int(Key::Backspace);
            case XK_Delete:    return int(Key::Delete);
            case XK_Insert:    return int(Key::Insert);
            case XK_Home:      return int(Key::Home);
            case XK_End:       return int(Key::End);
            case XK_Page_Up:   return int(Key::PageUp);
            case XK_Page_Down: return int(Key::PageDown);
            case XK_Shift_L: case XK_Shift_R:     return int(Key::Shift);
            case XK_Control_L: case XK_Control_R: return int(Key::Control);
            case XK_Alt_L: case XK_Alt_R:         return int(Key::Alt);
            case XK_Left:  return int(Key::Left);
            case XK_Right: return int(Key::Right);
            case XK_Up:    return int(Key::Up);
            case XK_Down:  return int(Key::Down);
            default: return -1;
        }
    }
}

class X11Window final : public Window {
public:
    explicit X11Window(const WindowProps& props) : m_Props(props) {}
    ~X11Window() override {
        const EGL::Api* egl = EGL::Load();
        if (m_CursorLocked) SetCursorLocked(false);
        if (egl && m_EGLDisplay) {
            egl->MakeCurrent(m_EGLDisplay, nullptr, nullptr, nullptr);
            if (m_Context) egl->DestroyContext(m_EGLDisplay, m_Context);
            if (m_Surface) egl->DestroySurface(m_EGLDisplay, m_Surface);
            egl->Terminate(m_EGLDisplay);
        }
        if (m_Display) {
            if (m_BlankCursor) XFreeCursor(m_Display, m_BlankCursor);
            if (m_Window) XDestroyWindow(m_Display, m_Window);
            if (m_Colormap) XFreeColormap(m_Display, m_Colormap);
            XCloseDisplay(m_Display);
        }
    }

    bool Init() {
        const EGL::Api* egl = EGL::Load();
        if (!egl || !egl->CreateWindowSurface) { LAPIS_ERROR("X11: libEGL not available"); return false; }

        m_Display = XOpenDisplay(nullptr);
        if (!m_Display) { LAPIS_ERROR("X11: cannot open display"); return false; }

        m_EGLDisplay = egl->GetPlatformDisplayEXT
            ? egl->GetPlatformDisplayEXT(EGL::PLATFORM_X11_KHR, m_Display, nullptr)
            : egl->GetDisplay(m_Display);
        EGL::Int major = 0, minor = 0;
        if (!m_EGLDisplay || !egl->Initialize(m_EGLDisplay, &major, &minor)) { LAPIS_ERROR("X11: eglInitialize failed"); return false; }
        egl->BindAPI(EGL::OPENGL_API);

        const EGL::Int cfgAttribs[] = {EGL::SURFACE_TYPE, EGL::WINDOW_BIT, EGL::RENDERABLE_TYPE, EGL::OPENGL_BIT,
                                       EGL::RED_SIZE, 8, EGL::GREEN_SIZE, 8, EGL::BLUE_SIZE, 8,
                                       EGL::DEPTH_SIZE, 24, EGL::NONE};
        EGL::Config config = nullptr;
        EGL::Int count = 0;
        if (!egl->ChooseConfig(m_EGLDisplay, cfgAttribs, &config, 1, &count) || count == 0) {
            LAPIS_ERROR("X11: no suitable EGL config");
            return false;
        }

        EGL::Int visualId = 0;
        egl->GetConfigAttrib(m_EGLDisplay, config, EGL::NATIVE_VISUAL_ID, &visualId);
        XVisualInfo tmpl{};
        tmpl.visualid = (VisualID)visualId;
        int numVisuals = 0;
        XVisualInfo* vi = XGetVisualInfo(m_Display, VisualIDMask, &tmpl, &numVisuals);
        const int screen = DefaultScreen(m_Display);
        Visual* visual = vi ? vi->visual : DefaultVisual(m_Display, screen);
        const int depth = vi ? vi->depth : DefaultDepth(m_Display, screen);
        const ::Window root = RootWindow(m_Display, screen);
        m_Colormap = XCreateColormap(m_Display, root, visual, AllocNone);

        XSetWindowAttributes swa{};
        swa.colormap = m_Colormap;
        swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                         StructureNotifyMask | FocusChangeMask | ExposureMask;
        m_Window = XCreateWindow(m_Display, root, 0, 0, m_Props.Width, m_Props.Height, 0, depth, InputOutput, visual,
                                 CWColormap | CWEventMask, &swa);
        if (vi) XFree(vi);
        if (!m_Window) { LAPIS_ERROR("X11: XCreateWindow failed"); return false; }

        m_DeleteAtom = XInternAtom(m_Display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(m_Display, m_Window, &m_DeleteAtom, 1);
        XStoreName(m_Display, m_Window, m_Props.Title.c_str());
        Bool supported = False;
        XkbSetDetectableAutoRepeat(m_Display, True, &supported);
        XMapWindow(m_Display, m_Window);
        XFlush(m_Display);

        m_Surface = egl->CreateWindowSurface(m_EGLDisplay, config, (unsigned long)m_Window, nullptr);
        if (!m_Surface) { LAPIS_ERROR("X11: eglCreateWindowSurface failed (0x{})", egl->GetError()); return false; }

        const int versions[][2] = {{4, 6}, {4, 5}, {4, 3}, {3, 3}};
        for (const auto& v : versions) {
            const EGL::Int ctxAttribs[] = {EGL::CONTEXT_MAJOR_VERSION, v[0], EGL::CONTEXT_MINOR_VERSION, v[1],
                                           EGL::CONTEXT_OPENGL_PROFILE_MASK, EGL::CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL::NONE};
            m_Context = egl->CreateContext(m_EGLDisplay, config, nullptr, ctxAttribs);
            if (m_Context) break;
        }
        if (!m_Context || !egl->MakeCurrent(m_EGLDisplay, m_Surface, m_Surface, m_Context)) {
            LAPIS_ERROR("X11: could not create an OpenGL 3.3+ core context");
            return false;
        }
        if (!LoadOpenGL(EGL::GetGLProc)) return false;
        LogOpenGLInfo();
#if LAPIS_DEBUG
        EnableOpenGLDebugOutput();
#endif
        SetVSync(m_Props.VSync);

        // Invisible cursor for mouse look.
        char empty[8] = {};
        Pixmap blank = XCreateBitmapFromData(m_Display, m_Window, empty, 8, 8);
        XColor black{};
        m_BlankCursor = XCreatePixmapCursor(m_Display, blank, blank, &black, &black, 0, 0);
        XFreePixmap(m_Display, blank);
        return true;
    }

    void PollEvents() override {
        while (XPending(m_Display)) {
            XEvent ev;
            XNextEvent(m_Display, &ev);
            HandleEvent(ev);
        }
        if (m_CursorLocked && m_Focused) {
            // Re-center so the cursor never hits the screen edge.
            const int cx = (int)m_Props.Width / 2, cy = (int)m_Props.Height / 2;
            XWarpPointer(m_Display, None, m_Window, 0, 0, 0, 0, cx, cy);
            m_LastX = (float)cx;
            m_LastY = (float)cy;
            m_IgnoreWarp = true;
            XFlush(m_Display);
        }
    }

    void SwapBuffers() override { EGL::Load()->SwapBuffers(m_EGLDisplay, m_Surface); }

    u32 GetWidth() const override  { return m_Props.Width; }
    u32 GetHeight() const override { return m_Props.Height; }

    void SetTitle(const std::string& title) override {
        m_Props.Title = title;
        XStoreName(m_Display, m_Window, title.c_str());
    }

    void SetVSync(bool enabled) override {
        m_Props.VSync = enabled;
        if (EGL::Load()->SwapInterval) EGL::Load()->SwapInterval(m_EGLDisplay, enabled ? 1 : 0);
    }
    bool IsVSync() const override { return m_Props.VSync; }

    void SetCursorLocked(bool locked) override {
        if (locked == m_CursorLocked) return;
        m_CursorLocked = locked;
        if (locked) {
            XDefineCursor(m_Display, m_Window, m_BlankCursor);
            XGrabPointer(m_Display, m_Window, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, m_Window, None, CurrentTime);
        } else {
            XUndefineCursor(m_Display, m_Window);
            XUngrabPointer(m_Display, CurrentTime);
        }
        XFlush(m_Display);
    }
    bool IsCursorLocked() const override { return m_CursorLocked; }

    void* GetNativeHandle() const override { return reinterpret_cast<void*>(m_Window); }

private:
    void HandleEvent(XEvent& ev) {
        switch (ev.type) {
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == m_DeleteAtom) { WindowCloseEvent e; Emit(e); }
                break;
            case ConfigureNotify: {
                const u32 w = (u32)ev.xconfigure.width, h = (u32)ev.xconfigure.height;
                if (w != m_Props.Width || h != m_Props.Height) {
                    m_Props.Width = w;
                    m_Props.Height = h;
                    WindowResizeEvent e(w, h);
                    Emit(e);
                }
                break;
            }
            case FocusIn:
            case FocusOut: {
                m_Focused = ev.type == FocusIn;
                if (!m_Focused) Input::ResetAll();
                WindowFocusEvent e(m_Focused);
                Emit(e);
                break;
            }
            case KeyPress:
            case KeyRelease: {
                const int key = TranslateKey(XLookupKeysym(&ev.xkey, 0));
                if (key < 0) break;
                const bool down = ev.type == KeyPress;
                const bool repeat = down && Input::IsKeyDown(static_cast<Key>(key));
                Input::SetKey(key, down);
                if (down) { KeyPressedEvent e(key, repeat); Emit(e); }
                else { KeyReleasedEvent e(key); Emit(e); }
                break;
            }
            case ButtonPress:
            case ButtonRelease: {
                const unsigned b = ev.xbutton.button;
                if (b == Button4 || b == Button5) {
                    if (ev.type == ButtonPress) {
                        const float d = b == Button4 ? 1.0f : -1.0f;
                        Input::AddScroll(d);
                        MouseScrolledEvent e(0.0f, d);
                        Emit(e);
                    }
                    break;
                }
                const int button = b == Button1 ? 0 : b == Button3 ? 1 : b == Button2 ? 2 : -1;
                if (button < 0) break;
                Input::SetMouseButton(button, ev.type == ButtonPress);
                if (ev.type == ButtonPress) { MouseButtonPressedEvent e(button); Emit(e); }
                else { MouseButtonReleasedEvent e(button); Emit(e); }
                break;
            }
            case MotionNotify: {
                const float x = (float)ev.xmotion.x, y = (float)ev.xmotion.y;
                const bool isWarp = m_IgnoreWarp && x == m_LastX && y == m_LastY;
                if (!isWarp && m_HasLast) Input::AddMouseDelta(x - m_LastX, y - m_LastY);
                m_IgnoreWarp = false;
                m_LastX = x;
                m_LastY = y;
                m_HasLast = true;
                Input::SetMousePosition(x, y);
                MouseMovedEvent e(x, y);
                Emit(e);
                break;
            }
            default: break;
        }
    }

    WindowProps  m_Props;
    Display*     m_Display = nullptr;
    ::Window     m_Window = 0;
    Colormap     m_Colormap = 0;
    Atom         m_DeleteAtom = 0;
    Cursor       m_BlankCursor = 0;
    EGL::Display m_EGLDisplay = nullptr;
    EGL::Surface m_Surface = nullptr;
    EGL::Context m_Context = nullptr;
    bool  m_CursorLocked = false, m_Focused = true, m_HasLast = false, m_IgnoreWarp = false;
    float m_LastX = 0, m_LastY = 0;
};

Scope<Window> CreateX11Window(const WindowProps& props) {
    auto window = CreateScope<X11Window>(props);
    if (!window->Init()) return nullptr;
    return window;
}

} // namespace Lapis

#else

namespace Lapis {
Scope<Window> CreateX11Window(const WindowProps&) { return nullptr; }
}

#endif
