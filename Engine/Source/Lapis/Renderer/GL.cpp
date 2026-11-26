#include "Lapis/Renderer/GL.h"

#include "Lapis/Core/Log.h"

namespace Lapis {

#define LAPIS_GL_DEFINE(ret, name, params) PFN_##name name = nullptr;
LAPIS_GL_FUNCTIONS(LAPIS_GL_DEFINE)
LAPIS_GL_OPTIONAL_FUNCTIONS(LAPIS_GL_DEFINE)
#undef LAPIS_GL_DEFINE

bool LoadOpenGL(GLProcLoader loader) {
    int missing = 0;
#define LAPIS_GL_LOAD(ret, name, params)                                       \
    name = reinterpret_cast<PFN_##name>(loader(#name));                        \
    if (!name) { LAPIS_ERROR("OpenGL: missing function {}", #name); ++missing; }
    LAPIS_GL_FUNCTIONS(LAPIS_GL_LOAD)
#undef LAPIS_GL_LOAD

#define LAPIS_GL_LOAD_OPT(ret, name, params) name = reinterpret_cast<PFN_##name>(loader(#name));
    LAPIS_GL_OPTIONAL_FUNCTIONS(LAPIS_GL_LOAD_OPT)
#undef LAPIS_GL_LOAD_OPT

    return missing == 0;
}

void LogOpenGLInfo() {
    auto str = [](GLenum e) { const GLubyte* s = glGetString(e); return s ? reinterpret_cast<const char*>(s) : "?"; };
    LAPIS_INFO("OpenGL vendor:   {}", str(GL_VENDOR));
    LAPIS_INFO("OpenGL renderer: {}", str(GL_RENDERER));
    LAPIS_INFO("OpenGL version:  {}", str(GL_VERSION));
    LAPIS_INFO("GLSL version:    {}", str(GL_SHADING_LANGUAGE_VERSION));
}

namespace {
    void LAPIS_APIENTRY DebugCallback(GLenum, GLenum, GLuint id, GLenum severity, GLsizei,
                                      const GLchar* message, const void*) {
        switch (severity) {
            case GL_DEBUG_SEVERITY_HIGH:   LAPIS_ERROR("GL [{}]: {}", id, message); break;
            case GL_DEBUG_SEVERITY_MEDIUM: LAPIS_WARN("GL [{}]: {}", id, message); break;
            case GL_DEBUG_SEVERITY_LOW:    LAPIS_TRACE("GL [{}]: {}", id, message); break;
            default: break; // notifications are too noisy
        }
    }
}

void EnableOpenGLDebugOutput() {
    if (!glDebugMessageCallback) return;
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(DebugCallback, nullptr);
}

} // namespace Lapis
