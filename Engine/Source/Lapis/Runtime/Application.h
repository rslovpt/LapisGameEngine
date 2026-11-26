#pragma once
// Application base class + main loop.
//
//   class MyGame : public Lapis::Application { ... };
//   Lapis::Application* Lapis::CreateApplication(int argc, char** argv) { return new MyGame(); }
//
// Loop per frame: input -> fixed-step physics (N x FixedTimestep) ->
// variable update (scripts) -> render -> present.

#include <string>

#include "Lapis/Core/Base.h"
#include "Lapis/Core/Event.h"
#include "Lapis/Core/Time.h"
#include "Lapis/Platform/Window.h"
#include "Lapis/Renderer/Renderer.h"
#include "Lapis/Scene/Scene.h"
#include "Lapis/UI/UIContext.h"
#include "Lapis/UI/UIRenderer.h"

namespace Lapis {

struct ApplicationSpec {
    WindowProps Window;
    std::string LogFile;               // empty = no log file
    u32   MSAASamples = 4;
    u32   WorkerThreads = 0;           // job system workers (0 = automatic)
    float FixedTimestep = 1.0f / 60.0f;
    bool  ShowStatsInTitle = true;
    u64   MaxFrames = 0;               // >0: quit automatically after N frames (automated tests)
    std::string ScreenshotOnExit;      // write the last frame as BMP before quitting
    bool  AutoStartScene = true;
    u32   MaxFPS = 0;                  // frame limiter (0 = off)       // false: the scene is only rendered until started (editor)
};

class Application : NonCopyable {
public:
    explicit Application(const ApplicationSpec& spec);
    virtual ~Application();

    // Runs until Quit() or the window closes. Returns the process exit code.
    int Run();
    void Quit(int exitCode = 0) { m_Running = false; m_ExitCode = exitCode; }

    Window&    GetWindow() { return *m_Window; }
    Renderer&  GetRenderer() { return m_Renderer; }
    UIContext& GetUI() { return *m_UI; }
    Scene*     GetScene() { return m_Scene.get(); }
    void       SetScene(Ref<Scene> scene);
    const FrameTime& GetTime() const { return m_Time; }
    const ApplicationSpec& GetSpec() const { return m_Spec; }

    // Applies a project's game graphics settings (vsync, MSAA, shadows,
    // fullscreen, frame cap, exposure) to the window/renderer/scene.
    void ApplyGraphicsSettings(const struct Project& project);

    // Writes the current backbuffer to a BMP file.
    bool SaveScreenshot(const std::string& path);

    static Application& Get() { return *s_Instance; }

protected:
    virtual bool OnInit() { return true; }          // window + renderer exist
    virtual void OnShutdown() {}
    virtual void OnFixedUpdate(float dt) { LAPIS_UNUSED(dt); }
    virtual void OnUpdate(float dt) { LAPIS_UNUSED(dt); }
    virtual void OnRender() {}                      // after scene submission, before present
    virtual void OnUI(UIContext& ui) { LAPIS_UNUSED(ui); } // 2D UI on top of the frame
    virtual void OnEvent(Event& e) { LAPIS_UNUSED(e); }
    // A script called App.Quit(). Games quit; the editor stops play mode instead.
    virtual void OnScriptQuit() { Quit(); }

    // UI scale (pixels per UI unit). Default: the monitor's DPI scale.
    virtual float GetUIScale() { return m_Window->GetContentScale(); }

    // Rectangle (x, y, w, h) in UI units that the game's HUD is drawn into.
    // The editor shrinks it so menus/toolbars don't cover the game's HUD.
    virtual Vec4 GetGameViewport() {
        const float s = GetUIScale();
        return {0, 0, m_Window->GetWidth() / s, m_Window->GetHeight() / s};
    }

    // Camera override for the scene (e.g. an editor camera). nullptr = scene camera.
    virtual const CameraView* GetCameraOverride() { return nullptr; }

    ApplicationSpec m_Spec;

private:
    bool Init();
    void Shutdown();
    void HandleEvent(Event& e);
    void Frame(float dt);

    Scope<Window> m_Window;
    Renderer      m_Renderer;
    UIRenderer    m_UIRenderer;
    Scope<UIContext> m_UI;
    Ref<Scene>    m_Scene;
    FrameTime     m_Time;
    bool m_Running = false;
    bool m_Minimized = false;
    int  m_ExitCode = 0;
    float m_Accumulator = 0.0f;

    static inline Application* s_Instance = nullptr;
};

// Implemented by the game / runtime.
Application* CreateApplication(int argc, char** argv);

} // namespace Lapis
