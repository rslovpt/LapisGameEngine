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
        else if (key == "VSync") p.VSync = ToBool(value);
        else if (key == "MSAA") p.MSAA = ToU32(value, p.MSAA);
        else if (key == "ShadowQuality") p.ShadowQuality = (int)std::min<u32>(ToU32(value, 2), 3);
        else if (key == "ShadowDistance") { try { p.ShadowDistance = std::stof(value); } catch (...) {} }
        else if (key == "MaxFPS") p.MaxFPS = ToU32(value, 0);
        else if (key == "Fullscreen") p.Fullscreen = ToBool(value);
        else if (key == "Exposure") { try { p.Exposure = std::stof(value); } catch (...) {} }
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
    o << "Width=" << Width << "\nHeight=" << Height << "\nVSync=" << (VSync ? "true" : "false") << "\nMSAA=" << MSAA << "\n";
    o << "# Game graphics settings\n"
      << "ShadowQuality=" << ShadowQuality << "\nShadowDistance=" << ShadowDistance << "\nMaxFPS=" << MaxFPS
      << "\nFullscreen=" << (Fullscreen ? "true" : "false") << "\nExposure=" << Exposure << "\n";
    return o.str();
}

bool Project::Save() const {
    return FileSystem::WriteText((std::filesystem::path(Directory) / FileName).string(), Serialize());
}

std::string Project::Path(const std::string& relative) const {
    return (std::filesystem::path(Directory) / relative).lexically_normal().string();
}

} // namespace Lapis
