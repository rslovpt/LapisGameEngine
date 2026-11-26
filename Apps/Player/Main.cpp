// LapisPlayer - the game runtime. Loads a project folder (project.lapis +
// content) and runs it. Packaged games are a renamed copy of this exe with
// the project in Content/ next to it.
//
// Usage: LapisPlayer [--project <dir>] [--console] [--headless]
//                    [--frames N] [--screenshot file.bmp] [--fail-on-script-error]
//                    [--width W --height H]
//
// Dev keys: F1 = toggle collider debug view, F12 = screenshot into Logs/.

#include <filesystem>

#include "../Common/AppUtils.h"

#include "Lapis/Lapis.h"
#include "Lapis/Runtime/EntryPoint.h"

using namespace Lapis;

class PlayerApp : public Application {
public:
    PlayerApp(const ApplicationSpec& spec, Project project, bool failOnScriptError)
        : Application(spec), m_Project(std::move(project)), m_FailOnScriptError(failOnScriptError) {}

protected:
    bool OnInit() override {
        auto scene = CreateRef<Scene>(m_Project.Name);
        scene->SetAssetRoot(m_Project.Directory);
        if (!m_Project.StartupScene.empty()) {
            if (!SceneSerializer::Load(*scene, m_Project.Path(m_Project.StartupScene)))
                LAPIS_ERROR("Startup scene '{}' failed to load - starting with an empty scene", m_Project.StartupScene);
        }
        SetScene(scene);
        if (!m_Project.MainScript.empty()) scene->GetScripts().RunFile(m_Project.MainScript);
        return true;
    }

    void OnUpdate(float) override {
        if (Input::WasKeyPressed(Key::F1)) GetScene()->DebugDrawColliders = !GetScene()->DebugDrawColliders;
        if (Input::WasKeyPressed(Key::F12)) {
            const std::string dir = (std::filesystem::path(LapisApp::ExeDir()) / "Logs").string();
            std::filesystem::create_directories(dir);
            SaveScreenshot(dir + "/screenshot_" + std::to_string(GetTime().FrameIndex) + ".bmp");
        }
    }

    void OnShutdown() override {
        if (!m_FailOnScriptError || !GetScene()) return;
        const size_t errors = GetScene()->GetScripts().GetErrors().size();
        if (errors > 0) {
            LAPIS_ERROR("{} script error(s) occurred - exiting with failure (--fail-on-script-error)", errors);
            Quit(3);
        }
    }

private:
    Project m_Project;
    bool m_FailOnScriptError;
};

Application* Lapis::CreateApplication(int argc, char** argv) {
    LapisApp::Args args(argc, argv);
    if (args.Has("--console")) LapisApp::AttachConsole();
    if (args.Has("--headless")) LapisApp::SetEnv("LAPIS_HEADLESS", "1");

    const std::string projectDir = LapisApp::FindProject(args.Value("--project"));
    if (projectDir.empty()) {
        LapisApp::ShowError("Lapis Player", "No project found.\nRun with --project <folder containing project.lapis>, "
                                            "or place the project in a 'Content' folder next to the executable.");
        return nullptr;
    }
    auto project = Project::Load(projectDir);
    if (!project) {
        LapisApp::ShowError("Lapis Player", "Could not read " + projectDir + "/project.lapis");
        return nullptr;
    }

    ApplicationSpec spec;
    spec.Window.Title = project->Name;
    spec.Window.Width = (u32)std::stoul(args.Value("--width", std::to_string(project->Width)));
    spec.Window.Height = (u32)std::stoul(args.Value("--height", std::to_string(project->Height)));
    spec.Window.VSync = project->VSync && !args.Has("--novsync");
    spec.MSAASamples = project->MSAA;
    spec.LogFile = (std::filesystem::path(LapisApp::ExeDir()) / "Logs" / (project->Name + ".log")).string();
    spec.MaxFrames = std::stoull(args.Value("--frames", "0"));
    spec.ScreenshotOnExit = args.Value("--screenshot");
    return new PlayerApp(spec, *project, args.Has("--fail-on-script-error"));
}
