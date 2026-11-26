// LapisPackager - turns a project folder into a standalone game:
//
//   Export/<Game>/
//       <Game>.exe        (copy of the LapisPlayer runtime)
//       Content/          (project.lapis, scenes, scripts, models, textures)
//       README.txt
//
// Before copying it "cooks" (validates) the project: every scene must parse,
// every referenced script must exist and compile, every referenced model or
// texture file must exist. Any error aborts packaging.
//
// Usage: LapisPackager <projectDir> [outputDir] [--runtime <path to LapisPlayer>]

#include <cstdio>
#include <filesystem>
#include <set>

#include "../Common/AppUtils.h"

#include "luacode.h"

#include "Lapis/Lapis.h"

namespace fs = std::filesystem;
using namespace Lapis;

namespace {

int s_Errors = 0;
void Error(const std::string& msg) { std::fprintf(stderr, "  ERROR: %s\n", msg.c_str()); ++s_Errors; }
void Info(const std::string& msg)  { std::printf("  %s\n", msg.c_str()); }

std::string SafeName(const std::string& name) {
    std::string out;
    for (char c : name) out += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return out.empty() ? "Game" : out;
}

bool CompileCheck(const fs::path& script) {
    auto src = FileSystem::ReadText(script.string());
    if (!src) { Error("cannot read script " + script.string()); return false; }
    size_t size = 0;
    char* bytecode = luau_compile(src->data(), src->size(), nullptr, &size);
    // Luau encodes compile errors as bytecode version 0 followed by the message.
    const bool ok = bytecode && size > 0 && bytecode[0] != 0;
    if (!ok) Error("script " + script.filename().string() + " does not compile: " + std::string(bytecode ? bytecode + 1 : "?", bytecode ? size - 1 : 1));
    std::free(bytecode);
    return ok;
}

bool IsBuiltinAsset(const std::string& name) {
    static const std::set<std::string> builtins = {"Cube", "Sphere", "Plane", "Checker", "White"};
    return builtins.count(name) > 0;
}

void CookProject(const Project& project) {
    std::printf("Validating project '%s'\n", project.Name.c_str());
    std::set<std::string> scripts;
    if (!project.MainScript.empty()) scripts.insert(project.MainScript);

    // All scripts in the project must compile, referenced or not.
    for (auto& entry : fs::recursive_directory_iterator(project.Directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".luau")
            scripts.insert(fs::relative(entry.path(), project.Directory).generic_string());
    }

    for (auto& entry : fs::recursive_directory_iterator(project.Directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".scene") continue;
        Scene scene;
        std::string err;
        auto text = FileSystem::ReadText(entry.path().string());
        if (!text || !SceneSerializer::Deserialize(scene, *text, &err, false)) {
            Error(entry.path().filename().string() + ": " + err);
            continue;
        }
        Info("scene " + fs::relative(entry.path(), project.Directory).generic_string() + ": " +
             std::to_string(scene.EntityCount()) + " entities");
        scene.GetRegistry().Each<MeshRendererComponent>([&](EntityID, MeshRendererComponent& mr) {
            if (!IsBuiltinAsset(mr.MeshName) && !fs::exists(fs::path(project.Directory) / mr.MeshName))
                Error("missing model '" + mr.MeshName + "'");
        });
        scene.GetRegistry().Each<LuauScriptComponent>([&](EntityID, LuauScriptComponent& sc) {
            if (!fs::exists(fs::path(project.Directory) / sc.ScriptPath)) Error("missing script '" + sc.ScriptPath + "'");
            scripts.insert(sc.ScriptPath);
        });
    }
    if (!fs::exists(project.Path(project.StartupScene))) Error("startup scene '" + project.StartupScene + "' not found");

    for (const std::string& s : scripts) {
        const fs::path p = fs::path(project.Directory) / s;
        if (fs::exists(p) && CompileCheck(p)) Info("script " + s + ": OK");
    }
}

} // namespace

int main(int argc, char** argv) {
    LapisApp::Args args(argc, argv);
    const auto positional = args.Positional({"--runtime"});
    if (positional.empty() || args.Has("--help")) {
        std::printf("Usage: LapisPackager <projectDir> [outputDir] [--runtime <LapisPlayer path>]\n");
        return positional.empty() ? 1 : 0;
    }
    Log::SetLevel(LogLevel::Warn);

    auto project = Project::Load(positional[0]);
    if (!project) { std::fprintf(stderr, "No project.lapis in '%s'\n", positional[0].c_str()); return 1; }

    CookProject(*project);
    if (s_Errors > 0) {
        std::fprintf(stderr, "\nPackaging FAILED: %d error(s). Nothing was written.\n", s_Errors);
        return 2;
    }

#if defined(_WIN32)
    const char* runtimeName = "LapisPlayer.exe";
    const char* exeExt = ".exe";
#else
    const char* runtimeName = "LapisPlayer";
    const char* exeExt = "";
#endif
    const fs::path runtime = args.Value("--runtime", (fs::path(LapisApp::ExeDir()) / runtimeName).string());
    if (!fs::exists(runtime)) { std::fprintf(stderr, "Runtime not found: %s\n", runtime.string().c_str()); return 3; }

    const std::string name = SafeName(project->Name);
    const fs::path out = positional.size() > 1 ? fs::path(positional[1]) : fs::current_path() / "Export" / name;
    std::error_code ec;
    fs::remove_all(out / "Content", ec);
    fs::create_directories(out / "Content", ec);
    if (ec) { std::fprintf(stderr, "Cannot create %s: %s\n", out.string().c_str(), ec.message().c_str()); return 4; }

    const fs::path exe = out / (name + exeExt);
    fs::copy_file(runtime, exe, fs::copy_options::overwrite_existing, ec);
    if (ec) { std::fprintf(stderr, "Cannot copy runtime: %s\n", ec.message().c_str()); return 4; }
    fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);

    size_t files = 0;
    for (auto& entry : fs::recursive_directory_iterator(project->Directory)) {
        const fs::path rel = fs::relative(entry.path(), project->Directory);
        if (rel.empty() || rel.string()[0] == '.') continue;
        if (entry.is_directory()) { fs::create_directories(out / "Content" / rel, ec); continue; }
        fs::copy_file(entry.path(), out / "Content" / rel, fs::copy_options::overwrite_existing, ec);
        if (ec) { std::fprintf(stderr, "Copy failed for %s: %s\n", rel.string().c_str(), ec.message().c_str()); return 4; }
        ++files;
    }

    FileSystem::WriteText((out / "README.txt").string(),
        project->Name + "\n\nMade with the Lapis Engine.\nRun " + name + exeExt +
        " to play. Logs are written to the Logs folder.\n");

    std::printf("\nPackaged '%s' -> %s\n  %s\n  Content/ (%zu files)\n", project->Name.c_str(), out.string().c_str(),
                exe.filename().string().c_str(), files);
    return 0;
}
