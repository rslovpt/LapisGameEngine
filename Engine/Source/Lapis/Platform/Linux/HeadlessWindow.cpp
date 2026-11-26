// Headless "window" for Linux: an EGL surfaceless OpenGL 4.5 core context that
// renders into an offscreen framebuffer. Used for CI and automated render tests.
#include "Lapis/Platform/Linux/LinuxWindows.h"

#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Linux/EGL.h"
#include "Lapis/Renderer/GL.h"

namespace Lapis {

class HeadlessWindow final : public Window {
public:
    explicit HeadlessWindow(const WindowProps& props) : m_Props(props) {}
    ~HeadlessWindow() override {
        const EGL::Api* egl = EGL::Load();
        if (egl && m_Context) {
            if (m_FBO) { glDeleteFramebuffers(1, &m_FBO); glDeleteRenderbuffers(2, m_RBOs); }
            egl->MakeCurrent(m_Display, nullptr, nullptr, nullptr);
            egl->DestroyContext(m_Display, m_Context);
        }
        if (egl && m_Display) egl->Terminate(m_Display);
    }

    bool Init() {
        const EGL::Api* egl = EGL::Load();
        if (!egl) { LAPIS_FATAL("Headless: libEGL.so.1 not available"); return false; }

        if (egl->GetPlatformDisplayEXT)
            m_Display = egl->GetPlatformDisplayEXT(EGL::PLATFORM_SURFACELESS_MESA, nullptr, nullptr);
        if (!m_Display && egl->GetDisplay) m_Display = egl->GetDisplay(nullptr);
        EGL::Int major = 0, minor = 0;
        if (!m_Display || !egl->Initialize(m_Display, &major, &minor)) {
            LAPIS_FATAL("Headless: eglInitialize failed");
            return false;
        }
        egl->BindAPI(EGL::OPENGL_API);

        const EGL::Int cfgAttribs[] = {EGL::RENDERABLE_TYPE, EGL::OPENGL_BIT, EGL::NONE};
        EGL::Config config = nullptr;
        EGL::Int numConfigs = 0;
        egl->ChooseConfig(m_Display, cfgAttribs, &config, 1, &numConfigs);
        if (numConfigs == 0) config = nullptr; // EGL_KHR_no_config_context

        const EGL::Int ctxAttribs[] = {EGL::CONTEXT_MAJOR_VERSION, 4, EGL::CONTEXT_MINOR_VERSION, 5,
                                       EGL::CONTEXT_OPENGL_PROFILE_MASK, EGL::CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL::NONE};
        m_Context = egl->CreateContext(m_Display, config, nullptr, ctxAttribs);
        if (!m_Context || !egl->MakeCurrent(m_Display, nullptr, nullptr, m_Context)) {
            LAPIS_FATAL("Headless: could not create a surfaceless OpenGL 4.5 context (0x{})", egl->GetError());
            return false;
        }
        if (!LoadOpenGL(EGL::GetGLProc)) return false;
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

    WindowProps  m_Props;
    EGL::Display m_Display = nullptr;
    EGL::Context m_Context = nullptr;
    GLuint       m_FBO = 0;
    GLuint       m_RBOs[2] = {};
    bool         m_Locked = false;
};

Scope<Window> CreateHeadlessWindow(const WindowProps& props) {
    auto window = CreateScope<HeadlessWindow>(props);
    if (!window->Init()) return nullptr;
    return window;
}

} // namespace Lapis
