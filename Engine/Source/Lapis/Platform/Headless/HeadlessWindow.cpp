// Headless "window" for Linux: an EGL surfaceless OpenGL 4.5 core context that
// renders into an offscreen framebuffer. Used for CI and automated render tests.
#include "Lapis/Platform/Window.h"
#include "Lapis/Renderer/GL.h"
#include "Lapis/Core/Log.h"

#include <dlfcn.h>

namespace Lapis {

namespace {
    // Minimal EGL declarations (libEGL is loaded at runtime with dlopen).
    using EGLDisplay = void*;
    using EGLContext = void*;
    using EGLSurface = void*;
    using EGLConfig  = void*;
    using EGLint     = int32_t;
    using EGLBoolean = unsigned int;
    using EGLenum    = unsigned int;

    constexpr EGLint  EGL_NONE                   = 0x3038;
    constexpr EGLint  EGL_RENDERABLE_TYPE        = 0x3040;
    constexpr EGLint  EGL_OPENGL_BIT             = 0x0008;
    constexpr EGLenum EGL_OPENGL_API             = 0x30A2;
    constexpr EGLint  EGL_CONTEXT_MAJOR_VERSION  = 0x3098;
    constexpr EGLint  EGL_CONTEXT_MINOR_VERSION  = 0x30FB;
    constexpr EGLint  EGL_CONTEXT_OPENGL_PROFILE_MASK = 0x30FD;
    constexpr EGLint  EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT = 0x0001;
    constexpr EGLenum EGL_PLATFORM_SURFACELESS_MESA = 0x31DD;

    struct EGLApi {
        void* Lib = nullptr;
        void* (*GetProcAddress)(const char*) = nullptr;
        EGLDisplay (*GetDisplay)(void*) = nullptr;
        EGLDisplay (*GetPlatformDisplayEXT)(EGLenum, void*, const EGLint*) = nullptr;
        EGLBoolean (*Initialize)(EGLDisplay, EGLint*, EGLint*) = nullptr;
        EGLBoolean (*Terminate)(EGLDisplay) = nullptr;
        EGLBoolean (*BindAPI)(EGLenum) = nullptr;
        EGLBoolean (*ChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = nullptr;
        EGLContext (*CreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) = nullptr;
        EGLBoolean (*DestroyContext)(EGLDisplay, EGLContext) = nullptr;
        EGLBoolean (*MakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
        EGLint     (*GetError)() = nullptr;
    };
    EGLApi s_EGL;
    void*  s_LibGL = nullptr;

    template <typename T> void Sym(T& fn, const char* name) { fn = reinterpret_cast<T>(dlsym(s_EGL.Lib, name)); }

    bool LoadEGL() {
        if (s_EGL.Lib) return true;
        s_EGL.Lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!s_EGL.Lib) return false;
        Sym(s_EGL.GetProcAddress, "eglGetProcAddress");
        Sym(s_EGL.GetDisplay, "eglGetDisplay");
        Sym(s_EGL.Initialize, "eglInitialize");
        Sym(s_EGL.Terminate, "eglTerminate");
        Sym(s_EGL.BindAPI, "eglBindAPI");
        Sym(s_EGL.ChooseConfig, "eglChooseConfig");
        Sym(s_EGL.CreateContext, "eglCreateContext");
        Sym(s_EGL.DestroyContext, "eglDestroyContext");
        Sym(s_EGL.MakeCurrent, "eglMakeCurrent");
        Sym(s_EGL.GetError, "eglGetError");
        if (s_EGL.GetProcAddress)
            s_EGL.GetPlatformDisplayEXT = reinterpret_cast<decltype(s_EGL.GetPlatformDisplayEXT)>(
                s_EGL.GetProcAddress("eglGetPlatformDisplayEXT"));
        return s_EGL.GetProcAddress && s_EGL.Initialize && s_EGL.CreateContext && s_EGL.MakeCurrent;
    }

    void* LoadGLProc(const char* name) {
        void* p = s_EGL.GetProcAddress(name);
        if (!p) {
            if (!s_LibGL) s_LibGL = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
            if (s_LibGL) p = dlsym(s_LibGL, name);
        }
        return p;
    }
}

class HeadlessWindow final : public Window {
public:
    explicit HeadlessWindow(const WindowProps& props) : m_Props(props) {}
    ~HeadlessWindow() override {
        if (m_Context) {
            if (m_FBO) { glDeleteFramebuffers(1, &m_FBO); glDeleteRenderbuffers(2, m_RBOs); }
            s_EGL.MakeCurrent(m_Display, nullptr, nullptr, nullptr);
            s_EGL.DestroyContext(m_Display, m_Context);
        }
        if (m_Display) s_EGL.Terminate(m_Display);
    }

    bool Init() {
        if (!LoadEGL()) { LAPIS_FATAL("Headless: libEGL.so.1 not available"); return false; }

        if (s_EGL.GetPlatformDisplayEXT)
            m_Display = s_EGL.GetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr);
        if (!m_Display && s_EGL.GetDisplay) m_Display = s_EGL.GetDisplay(nullptr);
        EGLint major = 0, minor = 0;
        if (!m_Display || !s_EGL.Initialize(m_Display, &major, &minor)) {
            LAPIS_FATAL("Headless: eglInitialize failed");
            return false;
        }
        s_EGL.BindAPI(EGL_OPENGL_API);

        const EGLint cfgAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
        EGLConfig config = nullptr;
        EGLint numConfigs = 0;
        s_EGL.ChooseConfig(m_Display, cfgAttribs, &config, 1, &numConfigs);
        if (numConfigs == 0) config = nullptr; // EGL_KHR_no_config_context

        const EGLint ctxAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
                                     EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
        m_Context = s_EGL.CreateContext(m_Display, config, nullptr, ctxAttribs);
        if (!m_Context || !s_EGL.MakeCurrent(m_Display, nullptr, nullptr, m_Context)) {
            LAPIS_FATAL("Headless: could not create a surfaceless OpenGL 4.5 context (0x{})", s_EGL.GetError());
            return false;
        }
        if (!LoadOpenGL(LoadGLProc)) return false;
        LogOpenGLInfo();
#if LAPIS_DEBUG
        EnableOpenGLDebugOutput();
#endif
        return CreateBackbuffer();
    }

    void PollEvents() override {}
    void SwapBuffers() override { glFinish(); }
    u32  GetWidth() const override  { return m_Props.Width; }
    u32  GetHeight() const override { return m_Props.Height; }
    void SetTitle(const std::string& title) override { m_Props.Title = title; }
    void SetVSync(bool enabled) override { m_Props.VSync = enabled; }
    bool IsVSync() const override { return m_Props.VSync; }
    void SetCursorLocked(bool locked) override { m_Locked = locked; }
    bool IsCursorLocked() const override { return m_Locked; }
    u32  GetBackbufferFBO() const override { return m_FBO; }
    void* GetNativeHandle() const override { return m_Context; }
    bool IsHeadless() const override { return true; }

private:
    bool CreateBackbuffer() {
        glGenFramebuffers(1, &m_FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glGenRenderbuffers(2, m_RBOs);
        glBindRenderbuffer(GL_RENDERBUFFER, m_RBOs[0]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, (GLsizei)m_Props.Width, (GLsizei)m_Props.Height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_RBOs[0]);
        glBindRenderbuffer(GL_RENDERBUFFER, m_RBOs[1]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)m_Props.Width, (GLsizei)m_Props.Height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_RBOs[1]);
        const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok) LAPIS_FATAL("Headless: backbuffer framebuffer incomplete");
        return ok;
    }

    WindowProps m_Props;
    EGLDisplay  m_Display = nullptr;
    EGLContext  m_Context = nullptr;
    GLuint      m_FBO = 0;
    GLuint      m_RBOs[2] = {};
    bool        m_Locked = false;
};

Scope<Window> Window::Create(const WindowProps& props) {
    auto window = CreateScope<HeadlessWindow>(props);
    if (!window->Init()) return nullptr;
    return window;
}

} // namespace Lapis
