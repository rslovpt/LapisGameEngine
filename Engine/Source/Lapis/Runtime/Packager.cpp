#include "Lapis/Runtime/Packager.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

#include "luacode.h"

#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Scene/Scene.h"
#include "Lapis/Scene/SceneSerializer.h"
#include "Lapis/Scripting/ScriptEngine.h"

namespace fs = std::filesystem;

namespace Lapis::Packager {

namespace {
    bool IsBuiltinAsset(const std::string& name) {
        static const std::set<std::string> builtins = {"Cube", "Sphere", "Plane", "Checker", "White"};
        return builtins.count(name) > 0;
    }

    bool CompileCheck(const fs::path& script, Result& r) {
        auto src = FileSystem::ReadText(script.string());
        if (!src) { r.Errors.push_back("cannot read script " + script.string()); return false; }
        size_t size = 0;
        char* bytecode = luau_compile(src->data(), src->size(), nullptr, &size);
        // Luau encodes compile errors as a 0 version byte followed by the message.
        const bool ok = bytecode && size > 0 && bytecode[0] != 0;
        if (!ok) r.Errors.push_back("script " + script.filename().string() + " does not compile: " +
                                    (bytecode && size > 1 ? std::string(bytecode + 1, size - 1) : "?"));
        std::free(bytecode);
        return ok;
    }
}

namespace {
    // Chunked copy so progress can be reported for big files.
    bool CopyWithProgress(const fs::path& from, const fs::path& to, u64& copied, const std::function<void()>& tick) {
        std::ifstream in(from, std::ios::binary);
        std::ofstream out(to, std::ios::binary | std::ios::trunc);
        if (!in || !out) return false;
        std::vector<char> buffer(1 << 20);
        while (in) {
            in.read(buffer.data(), (std::streamsize)buffer.size());
            const std::streamsize n = in.gcount();
            if (n <= 0) break;
            out.write(buffer.data(), n);
            if (!out) return false;
            copied += (u64)n;
            tick();
        }
        return true;
    }
}

std::string SafeName(const std::string& name) {
    std::string out;
    for (char c : name) out += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return out.empty() ? "Game" : out;
}

std::string DefaultRuntimePath() {
#if defined(_WIN32)
    return (fs::path(FileSystem::ExecutableDirectory()) / "LapisPlayer.exe").string();
#else
    return (fs::path(FileSystem::ExecutableDirectory()) / "LapisPlayer").string();
#endif
}

Result Validate(const Project& project) {
    Result r;
    std::error_code ec;
    if (!fs::is_directory(project.Directory, ec)) { r.Errors.push_back("project folder not found: " + project.Directory); return r; }

    std::set<std::string> scripts;
    if (!project.MainScript.empty()) {
        scripts.insert(project.MainScript);
        if (!fs::exists(project.Path(project.MainScript))) r.Errors.push_back("main script '" + project.MainScript + "' not found");
    }
    for (auto& entry : fs::recursive_directory_iterator(project.Directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".luau")
            scripts.insert(fs::relative(entry.path(), project.Directory).generic_string());
    }

    for (auto& entry : fs::recursive_directory_iterator(project.Directory, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".scene") continue;
        const std::string rel = fs::relative(entry.path(), project.Directory).generic_string();
        Scene scene;
        std::string err;
        auto text = FileSystem::ReadText(entry.path().string());
        if (!text || !SceneSerializer::Deserialize(scene, *text, &err, false)) { r.Errors.push_back(rel + ": " + err); continue; }
        r.Messages.push_back("scene " + rel + ": " + std::to_string(scene.EntityCount()) + " entities");
        scene.GetRegistry().Each<MeshRendererComponent>([&](EntityID, MeshRendererComponent& mr) {
            if (!IsBuiltinAsset(mr.MeshName) && !fs::exists(fs::path(project.Directory) / mr.MeshName))
                r.Errors.push_back(rel + ": missing model '" + mr.MeshName + "'");
        });
        scene.GetRegistry().Each<LuauScriptComponent>([&](EntityID, LuauScriptComponent& sc) {
            if (!fs::exists(fs::path(project.Directory) / sc.ScriptPath)) r.Errors.push_back(rel + ": missing script '" + sc.ScriptPath + "'");
        });
    }
    if (!fs::exists(project.Path(project.StartupScene))) r.Errors.push_back("startup scene '" + project.StartupScene + "' not found");

    for (const std::string& s : scripts) {
        const fs::path p = fs::path(project.Directory) / s;
        if (fs::exists(p) && CompileCheck(p, r)) r.Messages.push_back("script " + s + ": OK");
    }
    r.Ok = r.Errors.empty();
    return r;
}

Result Package(const Project& project, const std::string& runtimePath, const std::string& outputDir, const ProgressFn& progress) {
    auto report = [&](float f, const std::string& step) { if (progress) progress(f, step); };
    report(0.0f, "Validating project...");
    Result r = Validate(project);
    if (!r.Ok) return r;
    r.Ok = false;

    std::error_code ec;
    if (!fs::exists(runtimePath, ec)) { r.Errors.push_back("runtime not found: " + runtimePath); return r; }

    // Plan the copy so progress can be measured in bytes.
    struct Item { fs::path From, To; u64 Size; };
    std::vector<Item> items;
    std::vector<fs::path> dirs;
    const std::string name = SafeName(project.Name);
    const fs::path out(outputDir);
    const fs::path exe = out / (name + fs::path(runtimePath).extension().string());
    items.push_back({runtimePath, exe, (u64)fs::file_size(runtimePath, ec)});
    for (auto& entry : fs::recursive_directory_iterator(project.Directory)) {
        const fs::path rel = fs::relative(entry.path(), project.Directory);
        if (rel.empty() || rel.string()[0] == '.') continue;
        if (entry.is_directory()) dirs.push_back(out / "Content" / rel);
        else items.push_back({entry.path(), out / "Content" / rel, (u64)entry.file_size(ec)});
    }
    u64 total = 0;
    for (const Item& it : items) total += it.Size;
    total = std::max<u64>(total, 1);

    report(0.1f, "Preparing output folder...");
    fs::remove_all(out / "Content", ec);
    fs::create_directories(out / "Content", ec);
    if (ec) { r.Errors.push_back("cannot create " + out.string() + ": " + ec.message()); return r; }
    for (const fs::path& d : dirs) fs::create_directories(d, ec);

    u64 copied = 0;
    for (const Item& it : items) {
        const std::string label = it.To == exe ? exe.filename().string() : fs::relative(it.To, out).generic_string();
        auto tick = [&] { report(0.1f + 0.9f * (float)copied / (float)total, "Copying " + label); };
        tick();
        if (!CopyWithProgress(it.From, it.To, copied, tick)) { r.Errors.push_back("copy failed: " + label); return r; }
        if (it.To != exe) ++r.FilesCopied;
    }
    fs::permissions(exe, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
    FileSystem::WriteText((out / "README.txt").string(),
        project.Name + "\n\nMade with the Lapis Engine.\nRun " + exe.filename().string() +
        " to play. Logs are written to the Logs folder next to it.\n");

    r.OutputDir = out.string();
    r.ExePath = exe.string();
    r.Ok = true;
    report(1.0f, "Done");
    LAPIS_INFO("Packaged '{}' -> {} ({} files)", project.Name, r.OutputDir, r.FilesCopied);
    return r;
}

} // namespace Lapis::Packager
