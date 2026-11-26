#include "Lapis/Platform/Linux/EGL.h"

#include <dlfcn.h>

namespace Lapis::EGL {

namespace {
    Api   s_Api;
    bool  s_Tried = false;
    void* s_LibGL = nullptr;

    template <typename T> void Sym(T& fn, const char* name) { fn = reinterpret_cast<T>(dlsym(s_Api.Lib, name)); }
}

const Api* Load() {
    if (s_Tried) return s_Api.Lib ? &s_Api : nullptr;
    s_Tried = true;
    s_Api.Lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!s_Api.Lib) return nullptr;
    Sym(s_Api.GetProcAddress, "eglGetProcAddress");
    Sym(s_Api.GetDisplay, "eglGetDisplay");
    Sym(s_Api.Initialize, "eglInitialize");
    Sym(s_Api.Terminate, "eglTerminate");
    Sym(s_Api.BindAPI, "eglBindAPI");
    Sym(s_Api.ChooseConfig, "eglChooseConfig");
    Sym(s_Api.GetConfigAttrib, "eglGetConfigAttrib");
    Sym(s_Api.CreateContext, "eglCreateContext");
    Sym(s_Api.DestroyContext, "eglDestroyContext");
    Sym(s_Api.CreateWindowSurface, "eglCreateWindowSurface");
    Sym(s_Api.DestroySurface, "eglDestroySurface");
    Sym(s_Api.MakeCurrent, "eglMakeCurrent");
    Sym(s_Api.SwapBuffers, "eglSwapBuffers");
    Sym(s_Api.SwapInterval, "eglSwapInterval");
    Sym(s_Api.GetError, "eglGetError");
    if (s_Api.GetProcAddress)
        s_Api.GetPlatformDisplayEXT = reinterpret_cast<decltype(s_Api.GetPlatformDisplayEXT)>(
            s_Api.GetProcAddress("eglGetPlatformDisplayEXT"));
    if (!s_Api.GetProcAddress || !s_Api.Initialize || !s_Api.CreateContext || !s_Api.MakeCurrent) {
        dlclose(s_Api.Lib);
        s_Api = {};
        return nullptr;
    }
    return &s_Api;
}

void* GetGLProc(const char* name) {
    void* p = s_Api.GetProcAddress ? s_Api.GetProcAddress(name) : nullptr;
    if (!p) {
        if (!s_LibGL) s_LibGL = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (s_LibGL) p = dlsym(s_LibGL, name);
    }
    return p;
}

} // namespace Lapis::EGL
