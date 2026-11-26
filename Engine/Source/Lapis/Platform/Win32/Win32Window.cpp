// Win32 window + WGL OpenGL 4.x core-profile context.
#include "Lapis/Platform/Window.h"
#include "Lapis/Platform/Input.h"
#include "Lapis/Renderer/GL.h"
#include "Lapis/Core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Lapis {

// --- WGL extension constants / types (from wglext.h) ------------------------
namespace {
    constexpr int WGL_DRAW_TO_WINDOW_ARB         = 0x2001;
    constexpr int WGL_SUPPORT_OPENGL_ARB         = 0x2010;
    constexpr int WGL_DOUBLE_BUFFER_ARB          = 0x2011;
    constexpr int WGL_PIXEL_TYPE_ARB             = 0x2013;
    constexpr int WGL_TYPE_RGBA_ARB              = 0x202B;
    constexpr int WGL_COLOR_BITS_ARB             = 0x2014;
    constexpr int WGL_ALPHA_BITS_ARB             = 0x201B;
    constexpr int WGL_DEPTH_BITS_ARB             = 0x2022;
    constexpr int WGL_STENCIL_BITS_ARB           = 0x2023;
    constexpr int WGL_ACCELERATION_ARB           = 0x2003;
    constexpr int WGL_FULL_ACCELERATION_ARB      = 0x2027;
    constexpr int WGL_SAMPLE_BUFFERS_ARB         = 0x2041;
    constexpr int WGL_SAMPLES_ARB                = 0x2042;
    constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB  = 0x2091;
    constexpr int WGL_CONTEXT_MINOR_VERSION_ARB  = 0x2092;
    constexpr int WGL_CONTEXT_FLAGS_ARB          = 0x2094;
    constexpr int WGL_CONTEXT_PROFILE_MASK_ARB   = 0x9126;
    constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;
    constexpr int WGL_CONTEXT_DEBUG_BIT_ARB      = 0x00000001;
    constexpr int WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB = 0x00000002;

    using PFNWGLCHOOSEPIXELFORMATARBPROC    = BOOL (WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
    using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
    using PFNWGLSWAPINTERVALEXTPROC         = BOOL (WINAPI*)(int);

    PFNWGLCHOOSEPIXELFORMATARBPROC    wglChoosePixelFormatARB    = nullptr;
    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
    PFNWGLSWAPINTERVALEXTPROC         wglSwapIntervalEXT         = nullptr;

    const wchar_t* kWindowClass = L"LapisEngineWindow";
    HMODULE        s_OpenGL32   = nullptr;

    std::wstring Widen(const std::string& s) {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    // wglGetProcAddress only returns extension / post-1.1 functions; GL 1.1
    // entry points must come from opengl32.dll itself.
    void* LoadGLProc(const char* name) {
        void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
        const auto v = reinterpret_cast<intptr_t>(p);
        if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) {
            if (!s_OpenGL32) s_OpenGL32 = LoadLibraryW(L"opengl32.dll");
            p = s_OpenGL32 ? reinterpret_cast<void*>(GetProcAddress(s_OpenGL32, name)) : nullptr;
        }
        return p;
    }

    // Creates a throwaway legacy context so we can fetch the WGL ARB functions
    // needed to create a modern core-profile context.
    bool LoadWGLExtensions(HINSTANCE instance) {
        WNDCLASSW wc{};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance;
        wc.lpszClassName = L"LapisDummyWGL";
        RegisterClassW(&wc);

        HWND dummy = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1,
                                     nullptr, nullptr, instance, nullptr);
        if (!dummy) return false;
        HDC dc = GetDC(dummy);

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        const int fmt = ChoosePixelFormat(dc, &pfd);
        SetPixelFormat(dc, fmt, &pfd);

        HGLRC rc = wglCreateContext(dc);
        bool ok = rc && wglMakeCurrent(dc, rc);
        if (ok) {
            wglChoosePixelFormatARB    = reinterpret_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(wglGetProcAddress("wglChoosePixelFormatARB"));
            wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(wglGetProcAddress("wglCreateContextAttribsARB"));
            wglSwapIntervalEXT         = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(wglGetProcAddress("wglSwapIntervalEXT"));
            ok = wglChoosePixelFormatARB && wglCreateContextAttribsARB;
        }
        wglMakeCurrent(nullptr, nullptr);
        if (rc) wglDeleteContext(rc);
        ReleaseDC(dummy, dc);
        DestroyWindow(dummy);
        UnregisterClassW(wc.lpszClassName, instance);
        return ok;
    }

    // Per-window DPI (Windows 10+), falling back to the system DPI.
    UINT WindowDpi(HWND hwnd) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static auto fn = reinterpret_cast<GetDpiForWindowFn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
        if (fn && hwnd) { const UINT dpi = fn(hwnd); if (dpi) return dpi; }
        HDC dc = GetDC(nullptr);
        const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(nullptr, dc);
        return dpi > 0 ? (UINT)dpi : 96;
    }

    int MouseButtonFromMessage(UINT msg, WPARAM wParam) {
        switch (msg) {
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: return 0;
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: return 1;
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: return 2;
            case WM_XBUTTONDOWN: case WM_XBUTTONUP: return GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4;
            default: return -1;
        }
    }
}

class Win32Window final : public Window {
public:
    explicit Win32Window(const WindowProps& props) : m_Props(props) {}
    ~Win32Window() override { Shutdown(); }

    bool Init();

    void PollEvents() override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_CursorLocked && m_Focused) ClipToClientRect();
    }

    void SwapBuffers() override { ::SwapBuffers(m_DC); }

    u32 GetWidth() const override  { return m_Props.Width; }
    u32 GetHeight() const override { return m_Props.Height; }

    void SetTitle(const std::string& title) override {
        m_Props.Title = title;
        SetWindowTextW(m_Hwnd, Widen(title).c_str());
    }

    void SetVSync(bool enabled) override {
        m_Props.VSync = enabled;
        if (wglSwapIntervalEXT) wglSwapIntervalEXT(enabled ? 1 : 0);
    }
    bool IsVSync() const override { return m_Props.VSync; }

    void SetCursorLocked(bool locked) override {
        if (locked == m_CursorLocked) return;
        m_CursorLocked = locked;
        if (locked) {
            while (ShowCursor(FALSE) >= 0) {}
            ClipToClientRect();
        } else {
            while (ShowCursor(TRUE) < 0) {}
            ClipCursor(nullptr);
        }
    }
    bool IsCursorLocked() const override { return m_CursorLocked; }

    void* GetNativeHandle() const override { return m_Hwnd; }

    void SetFullscreen(bool fullscreen) override {
        if (fullscreen == m_Fullscreen) return;
        m_Fullscreen = fullscreen;
        if (fullscreen) {
            m_WindowedPlacement.length = sizeof(m_WindowedPlacement);
            GetWindowPlacement(m_Hwnd, &m_WindowedPlacement);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            GetMonitorInfoW(MonitorFromWindow(m_Hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowLongW(m_Hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(m_Hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        } else {
            SetWindowLongW(m_Hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowPlacement(m_Hwnd, &m_WindowedPlacement);
            SetWindowPos(m_Hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }
    }
    bool IsFullscreen() const override { return m_Fullscreen; }

    std::string GetClipboardText() const override {
        std::string out;
        if (!OpenClipboard(m_Hwnd)) return out;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) {
                const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
                if (n > 1) { out.resize(n - 1); WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr); }
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        // Normalize Windows line endings for the editor.
        std::string clean;
        clean.reserve(out.size());
        for (char c : out) if (c != '\r') clean += c;
        return clean;
    }

    void SetClipboardText(const std::string& text) override {
        std::string crlf;
        for (char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
        const std::wstring w = Widen(crlf);
        if (!OpenClipboard(m_Hwnd)) return;
        EmptyClipboard();
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
            if (void* p = GlobalLock(mem)) {
                std::memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(mem);
                if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
            } else {
                GlobalFree(mem);
            }
        }
        CloseClipboard();
    }

    float GetContentScale() const override {
        if (std::getenv("LAPIS_UI_SCALE")) return Window::GetContentScale();
        return (float)WindowDpi(m_Hwnd) / 96.0f;
    }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
    void ClipToClientRect();
    void Shutdown();

    WindowProps m_Props;
    HINSTANCE   m_Instance = nullptr;
    HWND        m_Hwnd = nullptr;
    HDC         m_DC = nullptr;
    HGLRC       m_GLContext = nullptr;
    bool        m_CursorLocked = false;
    bool        m_Focused = true;
    bool        m_Fullscreen = false;
    WINDOWPLACEMENT m_WindowedPlacement{};
};

bool Win32Window::Init() {
    m_Instance = GetModuleHandleW(nullptr);
    SetProcessDPIAware();

    if (!LoadWGLExtensions(m_Instance)) {
        LAPIS_FATAL("Win32: failed to load WGL extensions (is an OpenGL driver installed?)");
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = m_Instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // The requested size is in "100% scale" units: grow it with the display
    // scale so windows look the same size on high-DPI monitors, but keep it
    // within the screen.
    {
        const float dpiScale = (float)WindowDpi(nullptr) / 96.0f;
        const float maxW = GetSystemMetrics(SM_CXSCREEN) * 0.92f, maxH = GetSystemMetrics(SM_CYSCREEN) * 0.88f;
        const float fit = std::min({dpiScale, maxW / (float)m_Props.Width, maxH / (float)m_Props.Height});
        if (fit > 1.0f) { m_Props.Width = (u32)(m_Props.Width * fit); m_Props.Height = (u32)(m_Props.Height * fit); }
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!m_Props.Resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT rect{0, 0, (LONG)m_Props.Width, (LONG)m_Props.Height};
    AdjustWindowRect(&rect, style, FALSE);
    const int w = rect.right - rect.left, h = rect.bottom - rect.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    m_Hwnd = CreateWindowExW(0, kWindowClass, Widen(m_Props.Title).c_str(), style,
                             x > 0 ? x : CW_USEDEFAULT, y > 0 ? y : CW_USEDEFAULT, w, h,
                             nullptr, nullptr, m_Instance, this);
    if (!m_Hwnd) { LAPIS_FATAL("Win32: CreateWindowEx failed ({})", GetLastError()); return false; }
    m_DC = GetDC(m_Hwnd);

    // Pick a pixel format: try with MSAA first, then without.
    int pixelFormat = 0;
    UINT numFormats = 0;
    for (int samples : {m_Props.MSAASamples, 0}) {
        const int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, GL_TRUE, WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
            WGL_DOUBLE_BUFFER_ARB, GL_TRUE, WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
            WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB, WGL_COLOR_BITS_ARB, 32, WGL_ALPHA_BITS_ARB, 8,
            WGL_DEPTH_BITS_ARB, 24, WGL_STENCIL_BITS_ARB, 8,
            WGL_SAMPLE_BUFFERS_ARB, samples > 0 ? 1 : 0, WGL_SAMPLES_ARB, samples,
            0};
        if (wglChoosePixelFormatARB(m_DC, attribs, nullptr, 1, &pixelFormat, &numFormats) && numFormats > 0) break;
        pixelFormat = 0;
    }
    if (!pixelFormat) { LAPIS_FATAL("Win32: no suitable OpenGL pixel format"); return false; }

    PIXELFORMATDESCRIPTOR pfd{};
    DescribePixelFormat(m_DC, pixelFormat, sizeof(pfd), &pfd);
    if (!SetPixelFormat(m_DC, pixelFormat, &pfd)) { LAPIS_FATAL("Win32: SetPixelFormat failed"); return false; }

    // Create the newest core context the driver gives us (4.5 -> 3.3).
    const int versions[][2] = {{4, 6}, {4, 5}, {4, 3}, {4, 1}, {3, 3}};
    for (const auto& v : versions) {
        const int ctxAttribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, v[0], WGL_CONTEXT_MINOR_VERSION_ARB, v[1],
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
#if LAPIS_DEBUG
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB | WGL_CONTEXT_DEBUG_BIT_ARB,
#else
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
#endif
            0};
        m_GLContext = wglCreateContextAttribsARB(m_DC, nullptr, ctxAttribs);
        if (m_GLContext) break;
    }
    if (!m_GLContext) { LAPIS_FATAL("Win32: could not create an OpenGL 3.3+ core context"); return false; }
    wglMakeCurrent(m_DC, m_GLContext);

    if (!LoadOpenGL(LoadGLProc)) { LAPIS_FATAL("Win32: failed to load OpenGL functions"); return false; }
    LogOpenGLInfo();
#if LAPIS_DEBUG
    EnableOpenGLDebugOutput();
#endif
    SetVSync(m_Props.VSync);

    // Raw mouse input for precise, unaccelerated mouse-look deltas.
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // generic desktop
    rid.usUsage     = 0x02; // mouse
    rid.hwndTarget  = m_Hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    ShowWindow(m_Hwnd, SW_SHOW);
    SetForegroundWindow(m_Hwnd);
    SetFocus(m_Hwnd);
    return true;
}

void Win32Window::Shutdown() {
    if (m_CursorLocked) SetCursorLocked(false);
    if (m_GLContext) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(m_GLContext); m_GLContext = nullptr; }
    if (m_Hwnd) {
        if (m_DC) ReleaseDC(m_Hwnd, m_DC);
        DestroyWindow(m_Hwnd);
        m_Hwnd = nullptr;
        m_DC = nullptr;
    }
    UnregisterClassW(kWindowClass, m_Instance);
}

void Win32Window::ClipToClientRect() {
    RECT rect;
    GetClientRect(m_Hwnd, &rect);
    POINT tl{rect.left, rect.top}, br{rect.right, rect.bottom};
    ClientToScreen(m_Hwnd, &tl);
    ClientToScreen(m_Hwnd, &br);
    RECT clip{tl.x, tl.y, br.x, br.y};
    ClipCursor(&clip);
}

LRESULT CALLBACK Win32Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->m_Hwnd == hwnd) return self->WndProc(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Win32Window::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE: {
            WindowCloseEvent e;
            Emit(e);
            return 0; // the application decides when to destroy the window
        }
        case WM_SIZE: {
            const u32 w = LOWORD(lParam), h = HIWORD(lParam);
            if (w != m_Props.Width || h != m_Props.Height) {
                m_Props.Width = w;
                m_Props.Height = h;
                WindowResizeEvent e(w, h);
                Emit(e);
            }
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            m_Focused = msg == WM_SETFOCUS;
            if (!m_Focused) { Input::ResetAll(); ClipCursor(nullptr); }
            WindowFocusEvent e(m_Focused);
            Emit(e);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const int key = static_cast<int>(wParam);
            const bool repeat = (lParam & (1 << 30)) != 0;
            Input::SetKey(key, true);
            Input::AddKeyPress(key);
            KeyPressedEvent e(key, repeat);
            Emit(e);
            if (msg == WM_SYSKEYDOWN && key == VK_F4) break; // keep Alt+F4 working
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const int key = static_cast<int>(wParam);
            Input::SetKey(key, false);
            KeyReleasedEvent e(key);
            Emit(e);
            return 0;
        }
        case WM_CHAR: {
            Input::AddTypedChar(static_cast<u32>(wParam));
            KeyTypedEvent e(static_cast<u32>(wParam));
            Emit(e);
            return 0;
        }
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN: {
            const int b = MouseButtonFromMessage(msg, wParam);
            Input::SetMouseButton(b, true);
            SetCapture(m_Hwnd);
            MouseButtonPressedEvent e(b);
            Emit(e);
            return msg == WM_XBUTTONDOWN ? TRUE : 0;
        }
        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP: {
            const int b = MouseButtonFromMessage(msg, wParam);
            Input::SetMouseButton(b, false);
            ReleaseCapture();
            MouseButtonReleasedEvent e(b);
            Emit(e);
            return msg == WM_XBUTTONUP ? TRUE : 0;
        }
        case WM_MOUSEMOVE: {
            const float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
            Input::SetMousePosition(x, y);
            MouseMovedEvent e(x, y);
            Emit(e);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const float d = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
            Input::AddScroll(d);
            MouseScrolledEvent e(0.0f, d);
            Emit(e);
            return 0;
        }
        case WM_INPUT: {
            RAWINPUT raw{};
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                Input::AddMouseDelta((float)raw.data.mouse.lLastX, (float)raw.data.mouse.lLastY);
            }
            break; // DefWindowProc must still run for WM_INPUT
        }
        default: break;
    }
    return DefWindowProcW(m_Hwnd, msg, wParam, lParam);
}

Scope<Window> Window::Create(const WindowProps& props) {
    auto window = CreateScope<Win32Window>(props);
    if (!window->Init()) return nullptr;
    return window;
}

} // namespace Lapis
