#pragma once
// Minimal EGL declarations; libEGL.so.1 is loaded at runtime (no EGL
// headers or import libraries needed to build).

#include <cstdint>

namespace Lapis::EGL {

using Display = void*;
using Context = void*;
using Surface = void*;
using Config  = void*;
using Int     = int32_t;
using Boolean = unsigned int;
using Enum    = unsigned int;

constexpr Int  NONE                   = 0x3038;
constexpr Int  SURFACE_TYPE           = 0x3033;
constexpr Int  WINDOW_BIT             = 0x0004;
constexpr Int  RENDERABLE_TYPE        = 0x3040;
constexpr Int  OPENGL_BIT             = 0x0008;
constexpr Int  RED_SIZE               = 0x3024;
constexpr Int  GREEN_SIZE             = 0x3023;
constexpr Int  BLUE_SIZE              = 0x3022;
constexpr Int  ALPHA_SIZE             = 0x3021;
constexpr Int  DEPTH_SIZE             = 0x3025;
constexpr Int  NATIVE_VISUAL_ID       = 0x302E;
constexpr Enum OPENGL_API             = 0x30A2;
constexpr Int  CONTEXT_MAJOR_VERSION  = 0x3098;
constexpr Int  CONTEXT_MINOR_VERSION  = 0x30FB;
constexpr Int  CONTEXT_OPENGL_PROFILE_MASK = 0x30FD;
constexpr Int  CONTEXT_OPENGL_CORE_PROFILE_BIT = 0x0001;
constexpr Enum PLATFORM_X11_KHR       = 0x31D5;
constexpr Enum PLATFORM_SURFACELESS_MESA = 0x31DD;

struct Api {
    void* Lib = nullptr;
    void*   (*GetProcAddress)(const char*) = nullptr;
    Display (*GetDisplay)(void*) = nullptr;
    Display (*GetPlatformDisplayEXT)(Enum, void*, const Int*) = nullptr;
    Boolean (*Initialize)(Display, Int*, Int*) = nullptr;
    Boolean (*Terminate)(Display) = nullptr;
    Boolean (*BindAPI)(Enum) = nullptr;
    Boolean (*ChooseConfig)(Display, const Int*, Config*, Int, Int*) = nullptr;
    Boolean (*GetConfigAttrib)(Display, Config, Int, Int*) = nullptr;
    Context (*CreateContext)(Display, Config, Context, const Int*) = nullptr;
    Boolean (*DestroyContext)(Display, Context) = nullptr;
    Surface (*CreateWindowSurface)(Display, Config, unsigned long, const Int*) = nullptr;
    Boolean (*DestroySurface)(Display, Surface) = nullptr;
    Boolean (*MakeCurrent)(Display, Surface, Surface, Context) = nullptr;
    Boolean (*SwapBuffers)(Display, Surface) = nullptr;
    Boolean (*SwapInterval)(Display, Int) = nullptr;
    Int     (*GetError)() = nullptr;
};

// Loads libEGL once; returns nullptr if unavailable.
const Api* Load();

// OpenGL function loader backed by eglGetProcAddress (+ libGL fallback).
void* GetGLProc(const char* name);

} // namespace Lapis::EGL
