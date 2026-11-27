#include "Lapis/Runtime/Application.h"

#include <algorithm>

#include "Lapis/Assets/ImageLoader.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Input.h"
#include "Lapis/Core/JobSystem.h"
#include "Lapis/Runtime/Project.h"
#include "Lapis/Scripting/ScriptEngine.h"

#include <atomic>
#include <csignal>
#include <thread>

namespace {
// Ctrl+C / SIGTERM request a normal shutdown (scene stop, GPU cleanup, log
// flush) instead of killing the process mid-frame.
std::atomic<bool> g_QuitSignal{false};
extern "C" void LapisOnQuitSignal(int) { g_QuitSignal.store(true); }
} // namespace

namespace Lapis {

Application::Application(const ApplicationSpec& spec) : m_Spec(spec) {
    s_Instance = this;
}

Application::~Application() {
    if (s_Instance == this) s_Instance = nullptr;
}

void Application::SetScene(Ref<Scene> scene) {
    if (m_Scene && m_Scene->IsRunning()) m_Scene->OnStop();
    m_Scene = std::move(scene);
    if (m_Scene && m_Running && m_Spec.AutoStartScene) m_Scene->OnStart();
}

bool Application::Init() {
    if (!m_Spec.LogFile.empty() && !Log::OpenFile(m_Spec.LogFile))
        LAPIS_WARN("Could not open log file '{}'", m_Spec.LogFile);
    LAPIS_INFO("Lapis Engine starting: '{}' {}x{}", m_Spec.Window.Title, m_Spec.Window.Width, m_Spec.Window.Height);

    JobSystem::Init(m_Spec.WorkerThreads);

    if (!CreateWindowAndDevice()) return false;
    RHI::SetCurrent(m_Device.get());

    if (!m_Renderer.Init(*m_Device, m_Window->GetWidth(), m_Window->GetHeight(), m_Spec.MSAASamples)) {
        LAPIS_FATAL("Failed to initialize the renderer");
        return false;
    }
    if (!m_UIRenderer.Init(*m_Device)) { LAPIS_FATAL("Failed to initialize the UI renderer"); return false; }
    m_UI = CreateScope<UIContext>(m_UIRenderer);
    return true;
}

// Creates the window for the requested graphics API and its device. If that
// API can't start and fallback is allowed, retries with OpenGL.
bool Application::CreateWindowAndDevice() {
    RHI::API api = m_Spec.GraphicsAPI;
    if (!RHI::Device::IsCompiledIn(api)) {
        m_APIFallbackReason = std::string(RHI::APIName(api)) + " is not available on this platform";
        api = RHI::API::OpenGL;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        WindowProps props = m_Spec.Window;
        props.OpenGLContext = api == RHI::API::OpenGL;
        m_Window = Window::Create(props);
        if (!m_Window) {
            LAPIS_FATAL("Failed to create the window{}", props.OpenGLContext ? " / OpenGL context" : "");
            return false;
        }
        m_Window->SetEventCallback([this](Event& e) { HandleEvent(e); });
        std::string error;
        m_Device = RHI::Device::Create(api, *m_Window, &error);
        if (m_Device) {
            m_Device->SetVSync(m_Spec.Window.VSync);
            if (!m_APIFallbackReason.empty()) LAPIS_WARN("Using {} instead: {}", RHI::APIName(api), m_APIFallbackReason);
            return true;
        }
        if (api == RHI::API::OpenGL || !m_Spec.AllowAPIFallback) {
            LAPIS_FATAL("Graphics: {} could not start: {}", RHI::APIName(api), error);
            return false;
        }
        m_APIFallbackReason = std::string(RHI::APIName(api)) + " could not start: " + error;
        m_Window.reset();
        api = RHI::API::OpenGL;
    }
    return false;
}

void Application::Shutdown() {
    OnShutdown();
    if (m_Scene && m_Scene->IsRunning()) m_Scene->OnStop();
    m_Scene.reset();          // GPU resources must die before the GL context
    m_UI.reset();
    m_UIRenderer.Shutdown();
    m_Renderer.Shutdown();
    RHI::SetCurrent(nullptr);
    m_Device.reset();         // the device needs the window (swap chain / context)
    m_Window.reset();
    JobSystem::Shutdown();
    LAPIS_INFO("Lapis Engine shut down (exit code {})", m_ExitCode);
    Log::CloseFile();
}

void Application::HandleEvent(Event& e) {
    EventDispatcher d(e);
    d.Dispatch<WindowCloseEvent>([this](WindowCloseEvent&) { m_Running = false; return true; });
    d.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& r) {
        m_Minimized = r.Width == 0 || r.Height == 0;
        if (!m_Minimized) m_Renderer.Resize(r.Width, r.Height);
        return false;
    });
    if (!e.Handled) OnEvent(e);
}

int Application::Run() {
    if (!Init()) { Shutdown(); return 1; }
    m_Running = true;
    if (!OnInit()) { LAPIS_FATAL("Application OnInit failed"); m_ExitCode = 2; Shutdown(); return m_ExitCode; }
    if (m_Scene && !m_Scene->IsRunning() && m_Spec.AutoStartScene) m_Scene->OnStart();

    Timer frameTimer, titleTimer;
    u32 framesSinceTitle = 0;

    std::signal(SIGINT, LapisOnQuitSignal);
    std::signal(SIGTERM, LapisOnQuitSignal);

    while (m_Running) {
        if (g_QuitSignal.load()) { LAPIS_INFO("Quit signal received"); m_Running = false; break; }
        Input::NewFrame();
        m_Window->PollEvents();
        if (!m_Running) break;

        // Headless/automated runs use a fixed frame time for determinism.
        float dt = m_Window->IsHeadless() || m_Spec.FixedFrameTime ? m_Spec.FixedTimestep : (float)frameTimer.Elapsed();
        frameTimer.Reset();
        dt = std::min(dt, 0.25f);

        Frame(dt);

        // Optional frame cap (sleep, then spin for the last ~1 ms for accuracy).
        if (m_Spec.MaxFPS > 0 && !m_Window->IsHeadless()) {
            const double target = 1.0 / m_Spec.MaxFPS;
            const double remaining = target - frameTimer.Elapsed();
            if (remaining > 0.002) std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.0015));
            while (frameTimer.Elapsed() < target) {}
        }

        ++m_Time.FrameIndex;
        ++framesSinceTitle;
        if (titleTimer.Elapsed() >= 0.5) {
            m_Time.FPS = (float)(framesSinceTitle / titleTimer.Elapsed());
            framesSinceTitle = 0;
            titleTimer.Reset();
            if (m_Spec.ShowStatsInTitle) {
                const RenderStats& s = m_Renderer.GetStats();
                m_Window->SetTitle(Format("{}  |  {} FPS  |  {} draws  {} tris  |  {} entities", m_Spec.Window.Title,
                                          (int)(m_Time.FPS + 0.5f), s.DrawCalls, s.Triangles,
                                          m_Scene ? m_Scene->EntityCount() : 0));
            }
        }

        if (m_Spec.MaxFrames && m_Time.FrameIndex >= m_Spec.MaxFrames) {
            LAPIS_INFO("Reached MaxFrames ({}), quitting", m_Spec.MaxFrames);
            m_Running = false;
        }
    }

    if (!m_Spec.ScreenshotOnExit.empty()) SaveScreenshot(m_Spec.ScreenshotOnExit);
    Shutdown();
    return m_ExitCode;
}

void Application::Frame(float dt) {
    m_Time.DeltaTime = dt;
    m_Time.TotalTime += dt;
    const Vec4 hudRect = GetGameViewport();
    if (m_Scene) m_Scene->ViewportSize = {hudRect.z, hudRect.w};

    Scene::PerfStats perf;
    perf.FPS = m_Time.FPS;
    perf.FrameMs = dt * 1000.0f;

    // Fixed-step simulation (bounded to avoid the spiral of death).
    Timer phase;
    m_Accumulator += dt;
    int steps = 0;
    while (m_Accumulator >= m_Spec.FixedTimestep && steps < 8) {
        if (m_Scene) m_Scene->OnFixedUpdate(m_Spec.FixedTimestep);
        OnFixedUpdate(m_Spec.FixedTimestep);
        m_Accumulator -= m_Spec.FixedTimestep;
        ++steps;
    }
    if (steps == 8) m_Accumulator = 0.0f;
    perf.PhysicsMs = (float)phase.ElapsedMillis();
    perf.PhysicsSteps = (u32)steps;

    // Scripts see the current graphics settings unless they are mid-edit.
    if (ScriptEngine* se = m_Scene ? m_Scene->TryGetScripts() : nullptr) {
        if (!se->GraphicsEdited) se->Graphics = m_Graphics;
        se->GraphicsDefaults = m_GraphicsDefaults;
        se->GPU = m_Renderer.GetGPUInfo();
    }

    phase.Reset();
    if (m_Scene) m_Scene->InterpolationAlpha = m_Accumulator / m_Spec.FixedTimestep;
    if (m_Scene) m_Scene->OnUpdate(dt);
    OnUpdate(dt);
    perf.ScriptMs = (float)phase.ElapsedMillis();

    // Requests made by Luau scripts.
    if (ScriptEngine* se = m_Scene ? m_Scene->TryGetScripts() : nullptr; se && se->GraphicsApplyRequested) {
        se->GraphicsApplyRequested = false;
        se->GraphicsEdited = false;
        OnScriptGraphics(se->Graphics);
    }
    if (m_Scene && m_Scene->GetRegistry().Count<LuauScriptComponent>() > 0) {
        ScriptEngine& se = m_Scene->GetScripts();
        if (se.QuitRequested) { se.QuitRequested = false; OnScriptQuit(); }
        if (se.CursorLockRequest >= 0) { m_Window->SetCursorLocked(se.CursorLockRequest == 1); se.CursorLockRequest = -1; }
        if (!se.RequestedTitle.empty()) { m_Spec.Window.Title = se.RequestedTitle; se.RequestedTitle.clear(); }
    }

    if (m_Minimized) { if (m_Scene) { m_Scene->TakeHud(); m_Scene->Perf = perf; } return; }
    phase.Reset();
    m_Device->BeginFrame(m_Window->GetWidth(), m_Window->GetHeight());
    const float aspect = m_Window->GetAspect();
    if (m_Scene) {
        m_Scene->OnRender(m_Renderer, aspect, GetCameraOverride(), m_Accumulator / m_Spec.FixedTimestep);
    } else {
        CameraView view;
        view.View = LookAt({0, 2, 6}, {0, 0, 0}, Vec3::Up());
        view.Projection = Perspective(Radians(60.0f), aspect, 0.1f, 500.0f);
        view.Position = {0, 2, 6};
        m_Renderer.BeginFrame(view);
    }
    OnRender();
    m_Renderer.EndFrame();

    m_UI->BeginFrame(m_Window->GetWidth(), m_Window->GetHeight(), GetUIScale());
    if (m_Scene) {
        for (const auto& cmd : m_Scene->TakeHud()) {
            if (cmd.IsText) m_UI->TextOverlay(hudRect.x + cmd.X, hudRect.y + cmd.Y, cmd.Text, cmd.Color, cmd.Scale);
            else m_UIRenderer.Rect(hudRect.x + cmd.X, hudRect.y + cmd.Y, cmd.W, cmd.H, cmd.Color);
        }
    }
    OnUI(*m_UI);
    m_UI->EndFrame(0);
    m_Device->EndFrame();   // submit + present

    perf.RenderMs = (float)phase.ElapsedMillis();
    perf.DrawCalls = m_Renderer.GetStats().DrawCalls;
    perf.Triangles = m_Renderer.GetStats().Triangles;
    if (m_Scene) m_Scene->Perf = perf;
}

void Application::ApplyGraphicsSettings(const GraphicsSettings& settings) {
    m_Graphics = settings;
    m_Renderer.ApplySettings(settings);
    m_Device->SetVSync(settings.VSync);
    m_Spec.MaxFPS = settings.MaxFPS();
    m_Window->SetFullscreen(settings.Fullscreen);
}

void Application::ApplyProjectSettings(const Project& project) {
    if (project.WorkerThreads != 0 && project.WorkerThreads != JobSystem::GetWorkerCount()) JobSystem::SetWorkerCount(project.WorkerThreads);
    JobSystem::SetEnabled(project.Multithreading);
    m_GraphicsDefaults = project.Graphics;
    ApplyGraphicsSettings(project.Graphics);
}

bool Application::SaveScreenshot(const std::string& path) {
    if (!m_Window) return false;
    u32 w = 0, h = 0;
    auto pixels = m_Renderer.ReadPixels(w, h);
    const bool ok = !pixels.empty() && ImageLoader::WriteBMP(path, w, h, pixels);
    if (ok) LAPIS_INFO("Screenshot saved: {}", path);
    else LAPIS_ERROR("Screenshot failed: {}", path);
    return ok;
}

} // namespace Lapis
