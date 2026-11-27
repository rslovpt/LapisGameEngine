#include "Lapis/Runtime/Project.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"

namespace Lapis {

namespace {
    std::string Trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        const size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    bool ToBool(const std::string& v) { return v == "1" || v == "true" || v == "True" || v == "yes"; }
    u32 ToU32(const std::string& v, u32 def) { try { return (u32)std::stoul(v); } catch (...) { return def; } }
}

std::optional<Project> Project::Parse(const std::string& text, const std::string& directory) {
    Project p;
    p.Directory = directory;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { LAPIS_WARN("project.lapis line {}: expected Key=Value", lineNo); continue; }
        const std::string key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (key == "Name") p.Name = value;
        else if (key == "StartupScene") p.StartupScene = value;
        else if (key == "MainScript") p.MainScript = value;
        else if (key == "Width") p.Width = ToU32(value, p.Width);
        else if (key == "Height") p.Height = ToU32(value, p.Height);
        else if (key.rfind("Graphics.", 0) == 0) {
            std::string v = value;
            const size_t hash = v.find('#');
            if (hash != std::string::npos) v = Trim(v.substr(0, hash));
            if (!p.Graphics.Set(key.substr(9), v)) LAPIS_WARN("project.lapis line {}: bad graphics setting '{}'", lineNo, key);
        }
        else if (key == "GraphicsAPI") p.GraphicsAPI = value;
        else if (key == "Multithreading") p.Multithreading = ToBool(value);
        else if (key == "WorkerThreads") p.WorkerThreads = ToU32(value, 0);
        // Older project files (before the full graphics settings).
        else if (key == "VSync") p.Graphics.VSync = ToBool(value);
        else if (key == "Fullscreen") p.Graphics.Fullscreen = ToBool(value);
        else if (key == "MSAA") {
            const u32 n = ToU32(value, 4);
            p.Graphics.AntiAliasing = n >= 8 ? GraphicsSettings::AAMSAA8 : n >= 4 ? GraphicsSettings::AAMSAA4 : n >= 2 ? GraphicsSettings::AAMSAA2 : GraphicsSettings::AAOff;
        }
        else if (key == "ShadowQuality" || key == "ShadowDistance" || key == "Exposure") p.Graphics.Set(key, value);
        else if (key == "MaxFPS") {
            const u32 fps = ToU32(value, 0);
            int best = 0;
            for (int i = 0; i < 7; ++i) if ((u32)GraphicsSettings::FrameCapValues[i] <= fps) best = i;
            p.Graphics.FrameCap = fps == 0 ? 0 : best;
        }
        else LAPIS_WARN("project.lapis line {}: unknown key '{}'", lineNo, key);
    }
    return p;
}

std::optional<Project> Project::Load(const std::string& directory) {
    const std::string file = (std::filesystem::path(directory) / FileName).string();
    auto text = FileSystem::ReadText(file);
    if (!text) { LAPIS_ERROR("Project: '{}' not found", file); return std::nullopt; }
    return Parse(*text, directory);
}

std::string Project::Serialize() const {
    std::ostringstream o;
    o << "# Lapis project\n"
      << "Name=" << Name << "\n"
      << "StartupScene=" << StartupScene << "\n";
    if (!MainScript.empty()) o << "MainScript=" << MainScript << "\n";
    o << "Width=" << Width << "\nHeight=" << Height << "\n";
    o << "# Graphics API: Auto (DirectX 12 on Windows), DirectX 12 or OpenGL\nGraphicsAPI=" << GraphicsAPI << "\n";
    o << "# Game graphics settings (defaults of the exported game)\n";
    Graphics.Serialize(o, "Graphics.");
    o << "# Multithreading (WorkerThreads=0 means automatic)\n"
      << "Multithreading=" << (Multithreading ? "true" : "false") << "\nWorkerThreads=" << WorkerThreads << "\n";
    return o.str();
}

bool Project::Save() const {
    return FileSystem::WriteText((std::filesystem::path(Directory) / FileName).string(), Serialize());
}

std::string Project::Path(const std::string& relative) const {
    return (std::filesystem::path(Directory) / relative).lexically_normal().string();
}

} // namespace Lapis
