// LapisDiagnostics - runs on the target machine and writes a report that
// tells a developer whether the engine works there and why not if it doesn't.
//
// Output (next to the exe):
//   Logs/diagnostics_report.txt     human-readable report (PASS/FAIL per check)
//   Logs/diagnostics.log            full engine log of the run
//   Logs/diagnostics_render.bmp     the rendered test scene
//   Logs/diagnostics_project.bmp    the Sandbox project after a few seconds
//
// Usage: LapisDiagnostics [--project <dir>] [--frames N] [--no-window]
// Exit code 0 = all checks passed.

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <thread>

#include "../Common/AppUtils.h"
#include "TestFramework.h"

#include "Lapis/Lapis.h"
#include "Lapis/Renderer/GL.h"

#if defined(__linux__)
    #include <sys/utsname.h>
#endif

namespace fs = std::filesystem;
using namespace Lapis;

namespace {

struct Report {
    std::ostringstream Body;
    int Passed = 0, Failed = 0, Warned = 0;
    std::vector<std::string> FailedChecks;

    void Section(const std::string& title) { Body << "\n=== " << title << " " << std::string(60 - std::min<size_t>(title.size(), 58), '=') << "\n"; }
    void Line(const std::string& text) { Body << text << "\n"; LAPIS_INFO("{}", text); }
    void Check(const std::string& name, bool ok, const std::string& detail = "") {
        (ok ? Passed : Failed)++;
        if (!ok) FailedChecks.push_back(name);
        Line(std::string(ok ? "[PASS] " : "[FAIL] ") + name + (detail.empty() ? "" : "  -  " + detail));
    }
    void Warn(const std::string& name, const std::string& detail) {
        ++Warned;
        Line("[WARN] " + name + "  -  " + detail);
    }
};

std::string Now() {
    const std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

std::string OSDescription() {
#if defined(_WIN32)
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (auto fn = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) fn(&info);
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    return Format("Windows {}.{} build {}, RAM {} MB", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber,
                  mem.ullTotalPhys / (1024 * 1024));
#elif defined(__linux__)
    utsname u{};
    uname(&u);
    return Format("{} {} {}", u.sysname, u.release, u.machine);
#else
    return "unknown";
#endif
}

std::string CompilerDescription() {
#if defined(_MSC_VER)
    return Format("MSVC {}", _MSC_VER);
#elif defined(__clang__)
    return Format("Clang {}.{}", __clang_major__, __clang_minor__);
#elif defined(__GNUC__)
    return Format("GCC {}.{}", __GNUC__, __GNUC_MINOR__);
#else
    return "unknown";
#endif
}

const char* GLStr(GLenum e) { const GLubyte* s = glGetString(e); return s ? reinterpret_cast<const char*>(s) : "?"; }

int DrainGLErrors() {
    int n = 0;
    while (glGetError() != GL_NO_ERROR && n < 64) ++n;
    return n;
}

// Simple pixel statistics over a region of an RGBA8 bottom-up image.
struct RegionStats { float R = 0, G = 0, B = 0; float Variance = 0; };
RegionStats Analyze(const std::vector<u8>& px, u32 w, u32 h, float x0, float y0, float x1, float y1) {
    RegionStats s;
    double sum[3] = {}, sumSq = 0;
    u32 n = 0;
    for (u32 y = u32(y0 * h); y < u32(y1 * h); ++y)
        for (u32 x = u32(x0 * w); x < u32(x1 * w); ++x) {
            const u8* p = &px[(size_t(y) * w + x) * 4];
            for (int c = 0; c < 3; ++c) sum[c] += p[c];
            const double lum = (p[0] + p[1] + p[2]) / 3.0;
            sumSq += lum * lum;
            ++n;
        }
    if (!n) return s;
    s.R = float(sum[0] / n); s.G = float(sum[1] / n); s.B = float(sum[2] / n);
    const double mean = (s.R + s.G + s.B) / 3.0;
    s.Variance = float(sumSq / n - mean * mean);
    return s;
}

bool HasRedPixels(const std::vector<u8>& px) {
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        if (px[i] > 120 && px[i] > px[i + 1] * 2 && px[i] > px[i + 2] * 2) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    LapisApp::Args args(argc, argv);
    if (args.Has("--no-window")) LapisApp::SetEnv("LAPIS_HEADLESS", "1");
    const fs::path logs = fs::path(LapisApp::ExeDir()) / "Logs";
    fs::create_directories(logs);
    Log::OpenFile((logs / "diagnostics.log").string());

    Report R;
    std::vector<std::string> capturedErrors;
    const int listener = Log::AddListener([&](LogLevel level, const std::string& msg) {
        if (level >= LogLevel::Error) capturedErrors.push_back(msg);
    });

    // ------------------------------------------------------------------ system
    R.Section("System");
    R.Line("Lapis Engine " + std::string(EngineVersion) + " diagnostics, " + Now());
    R.Line("OS:        " + OSDescription());
    R.Line("Compiler:  " + CompilerDescription() + (LAPIS_DEBUG_ENABLED ? " (Debug build)" : " (Release build)"));
    R.Line("CPU cores: " + std::to_string(std::thread::hardware_concurrency()));
    R.Line("Exe dir:   " + LapisApp::ExeDir());

    // ------------------------------------------------------------- unit tests
    R.Section("Unit tests (no GPU)");
    {
        const LogLevel prev = Log::GetLevel();
        Log::SetLevel(LogLevel::Off);                 // tests log errors on purpose
        const size_t errorsBefore = capturedErrors.size();
        std::string testOutput;
        const auto summary = LapisTest::RunAll(&testOutput);
        Log::SetLevel(prev);
        capturedErrors.resize(errorsBefore);
        std::istringstream lines(testOutput);
        std::string line;
        while (std::getline(lines, line)) if (line.find("FAIL") != std::string::npos) R.Body << line << "\n";
        R.Check("Unit tests", summary.Failed == 0, Format("{} / {} passed", summary.Total - summary.Failed, summary.Total));
    }

    // --------------------------------------------------- window + GL context
    R.Section("Window and OpenGL");
    WindowProps props;
    props.Title = "Lapis Diagnostics";
    props.Width = 960;
    props.Height = 540;
    props.VSync = false;
    Scope<Window> window = Window::Create(props);
    R.Check("Create window + OpenGL context", window != nullptr, window ? (window->IsHeadless() ? "headless EGL" : "native window") : "see log");
    if (!window) {
        R.Line("Cannot continue without an OpenGL context. Common causes: missing/old GPU driver, remote desktop session,");
        R.Line("or a GPU without OpenGL 3.3 support. Update the graphics driver from the GPU vendor's website.");
    } else {
        R.Line(Format("GL_VENDOR:   {}", GLStr(GL_VENDOR)));
        R.Line(Format("GL_RENDERER: {}", GLStr(GL_RENDERER)));
        R.Line(Format("GL_VERSION:  {}", GLStr(GL_VERSION)));
        R.Line(Format("GLSL:        {}", GLStr(GL_SHADING_LANGUAGE_VERSION)));
        GLint major = 0, minor = 0, maxTex = 0, maxSamples = 0, exts = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        glGetIntegerv(GL_NUM_EXTENSIONS, &exts);
        R.Line(Format("Max texture size {}, max MSAA samples {}, {} extensions", maxTex, maxSamples, exts));
        R.Check("OpenGL 3.3 or newer", major > 3 || (major == 3 && minor >= 3), Format("{}.{}", major, minor));
        if (!glDebugMessageCallback) R.Warn("GL debug output", "glDebugMessageCallback unavailable (driver < 4.3) - GL errors are less detailed");
        DrainGLErrors();
    }

    // ----------------------------------------------------------- rendering
    Renderer renderer;
    bool rendererOk = false;
    if (window) {
        R.Section("Renderer");
        const size_t errorsBefore = capturedErrors.size();
        rendererOk = renderer.Init(window->GetWidth(), window->GetHeight(), 4);
        R.Check("Renderer init (5 built-in shaders, HDR/MSAA/shadow targets)", rendererOk);
        for (size_t i = errorsBefore; i < capturedErrors.size(); ++i) R.Body << "    " << capturedErrors[i] << "\n";
    }

    if (rendererOk) {
        Scene scene("DiagnosticsScene");
        Entity ground = scene.CreateEntity("Ground");
        ground.Transform().Scale = {30, 1, 30};
        ground.Transform().Position = {0, -0.5f, 0};
        auto& gm = ground.Add<MeshRendererComponent>();
        gm.MeshName = "Cube";
        gm.MeshAsset = scene.GetAssets().GetMesh("Cube");
        gm.MaterialAsset = Material::Create("Ground", {0.75f, 0.75f, 0.75f, 1}, 0.9f);
        gm.MaterialAsset->AlbedoMap = scene.GetAssets().GetTexture("Checker");
        gm.MaterialAsset->Tiling = {15, 15};
        ground.Add<ColliderComponent>();

        Entity cube = scene.CreateEntity("RedCube");
        cube.Transform().Position = {-1.2f, 0.5f, 0};
        auto& cm = cube.Add<MeshRendererComponent>();
        cm.MeshAsset = scene.GetAssets().GetMesh("Cube");
        cm.MaterialAsset = Material::Create("Red", {0.9f, 0.1f, 0.1f, 1}, 0.4f);

        Entity cam = scene.CreateEntity("Camera");
        cam.Add<CameraComponent>();
        cam.Transform().Position = {0, 2.5f, 6};
        cam.Transform().SetEulerDegrees({-18, 0, 0});

        Entity light = scene.CreateEntity("Light");
        light.Transform().Position = {1.5f, 1.5f, 1.5f};
        light.Add<LightComponent>().Color = {0.3f, 0.6f, 1.0f};

        R.Section("Render test");
        scene.OnStart();
        for (int i = 0; i < 5; ++i) {
            window->PollEvents();
            scene.OnUpdate(1.0f / 60.0f);
            scene.OnRender(renderer, window->GetAspect());
            renderer.EndFrame(window->GetBackbufferFBO());
            window->SwapBuffers();
        }
        const u32 w = window->GetWidth(), h = window->GetHeight();
        auto pixels = renderer.ReadPixels(window->GetBackbufferFBO());
        const int glErrors = DrainGLErrors();
        R.Check("No OpenGL errors while rendering", glErrors == 0, Format("{} error(s)", glErrors));
        R.Check("Frame stats sane", renderer.GetStats().DrawCalls >= 4 && renderer.GetStats().Triangles > 0,
                Format("{} draw calls, {} triangles", renderer.GetStats().DrawCalls, renderer.GetStats().Triangles));

        const RegionStats sky = Analyze(pixels, w, h, 0.1f, 0.85f, 0.9f, 0.98f);
        const RegionStats floor = Analyze(pixels, w, h, 0.1f, 0.02f, 0.9f, 0.25f);
        R.Check("Sky rendered (top of image is bright and blue-ish)", sky.B > 120 && sky.B >= sky.R,
                Format("avg RGB {} {} {}", (int)sky.R, (int)sky.G, (int)sky.B));
        R.Check("Ground rendered (checker texture visible)", floor.Variance > 20.0f,
                Format("luminance variance {}", (int)floor.Variance));
        R.Check("Lit red cube visible", HasRedPixels(pixels));
        const std::string shot = (logs / "diagnostics_render.bmp").string();
        R.Check("Screenshot written", ImageLoader::WriteBMP(shot, w, h, pixels), shot);

        // Luau end-to-end with GPU assets.
        R.Section("Luau scripting");
        const bool luauOk = scene.GetScripts().RunString(R"(
            local e = Scene.Spawn({ Name = "ScriptBall", Mesh = "Sphere", Position = Vector3.new(0, 3, 0),
                                    Physics = "Dynamic", Color = Color.Blue, Restitution = 0.2 })
            assert(e.Name == "ScriptBall")
        )", "=diagnostics");
        R.Check("Script spawns a physics ball", luauOk);
        for (int i = 0; i < 180; ++i) { scene.OnFixedUpdate(1.0f / 60.0f); scene.OnUpdate(1.0f / 60.0f); }
        Entity ball = scene.FindEntityByName("ScriptBall");
        R.Check("Ball fell and came to rest on the ground", ball && std::fabs(ball.Transform().Position.y - 0.5f) < 0.05f,
                ball ? Format("y = {}", ball.Transform().Position.y) : "missing");

        // Performance.
        R.Section("Performance");
        window->SetVSync(false);
        Timer timer;
        std::vector<double> frameMs;
        const int frames = std::stoi(args.Value("--frames", "240"));
        for (int i = 0; i < frames; ++i) {
            Timer ft;
            window->PollEvents();
            scene.OnFixedUpdate(1.0f / 60.0f);
            scene.OnUpdate(1.0f / 60.0f);
            scene.OnRender(renderer, window->GetAspect());
            renderer.EndFrame(window->GetBackbufferFBO());
            window->SwapBuffers();
            frameMs.push_back(ft.ElapsedMillis());
        }
        std::sort(frameMs.begin(), frameMs.end());
        double avg = 0;
        for (double v : frameMs) avg += v;
        avg /= std::max<size_t>(frameMs.size(), 1);
        R.Line(Format("{} frames: avg {} ms ({} FPS), median {} ms, 99th pct {} ms", frames, (int)(avg * 100) / 100.0,
                      (int)(1000.0 / std::max(avg, 0.001)), (int)(frameMs[frameMs.size() / 2] * 100) / 100.0,
                      (int)(frameMs[frameMs.size() * 99 / 100] * 100) / 100.0));
        if (avg > 33.0) R.Warn("Frame time", "slower than 30 FPS on the test scene (integrated/software GPU?)");
        else R.Check("Test scene renders at 30+ FPS", true);
        scene.OnStop();
    }

    // -------------------------------------------------------- project check
    if (rendererOk) {
        R.Section("Project");
        const std::string projectDir = LapisApp::FindProject(args.Value("--project"));
        if (projectDir.empty()) {
            R.Warn("Project", "no project found (pass --project <dir>)");
        } else if (auto project = Project::Load(projectDir)) {
            R.Line("Project '" + project->Name + "' at " + projectDir);
            auto scene = CreateRef<Scene>(project->Name);
            scene->SetAssetRoot(project->Directory);
            const size_t errorsBefore = capturedErrors.size();
            const bool loaded = SceneSerializer::Load(*scene, project->Path(project->StartupScene));
            R.Check("Startup scene loads", loaded, Format("{} entities", scene->EntityCount()));
            scene->OnStart();
            if (!project->MainScript.empty()) R.Check("Main script runs", scene->GetScripts().RunFile(project->MainScript), project->MainScript);
            for (int i = 0; i < 180; ++i) {
                window->PollEvents();
                scene->OnFixedUpdate(1.0f / 60.0f);
                scene->OnUpdate(1.0f / 60.0f);
                scene->OnRender(renderer, window->GetAspect());
                renderer.EndFrame(window->GetBackbufferFBO());
                window->SwapBuffers();
            }
            const auto& scriptErrors = scene->GetScripts().GetErrors();
            R.Check("No Luau script errors in 3 seconds of play", scriptErrors.empty(), Format("{} error(s)", scriptErrors.size()));
            for (size_t i = 0; i < std::min<size_t>(scriptErrors.size(), 10); ++i) R.Body << "    " << scriptErrors[i] << "\n";
            R.Check("No engine errors while running the project", capturedErrors.size() == errorsBefore,
                    Format("{} error(s)", capturedErrors.size() - errorsBefore));
            auto px = renderer.ReadPixels(window->GetBackbufferFBO());
            ImageLoader::WriteBMP((logs / "diagnostics_project.bmp").string(), window->GetWidth(), window->GetHeight(), px);
            R.Line("Project screenshot: " + (logs / "diagnostics_project.bmp").string());
            scene->OnStop();
            scene.reset();
        } else {
            R.Check("project.lapis readable", false, projectDir);
        }
    }

    renderer.Shutdown();
    window.reset();

    // ----------------------------------------------------------- summary
    R.Section("Errors captured from the engine log");
    if (capturedErrors.empty()) R.Body << "(none)\n";
    for (size_t i = 0; i < std::min<size_t>(capturedErrors.size(), 50); ++i) R.Body << capturedErrors[i] << "\n";

    std::ostringstream header;
    header << "LAPIS ENGINE DIAGNOSTICS REPORT\n";
    header << "Result: " << (R.Failed == 0 ? "ALL CHECKS PASSED" : "FAILURES DETECTED") << "  (" << R.Passed << " passed, "
           << R.Failed << " failed, " << R.Warned << " warnings)\n";
    for (const auto& f : R.FailedChecks) header << "  FAILED: " << f << "\n";
    const std::string reportPath = (logs / "diagnostics_report.txt").string();
    FileSystem::WriteText(reportPath, header.str() + R.Body.str());

    Log::RemoveListener(listener);
    std::printf("\n%s\nReport written to %s\n", header.str().c_str(), reportPath.c_str());
    Log::CloseFile();
    return R.Failed == 0 ? 0 : 1;
}
