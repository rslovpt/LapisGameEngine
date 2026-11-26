#pragma once
// A game project is a folder containing `project.lapis` plus content
// (Scenes/, Scripts/, Models/, Textures/). Packaged games ship that folder
// as `Content/` next to the game executable.
//
//   # project.lapis
//   Name=Sandbox
//   StartupScene=Scenes/Main.scene
//   MainScript=Scripts/Game.luau
//   Width=1280
//   Height=720
//   VSync=true
//   MSAA=4

#include <optional>
#include <string>

#include "Lapis/Core/Base.h"

namespace Lapis {

struct Project {
    std::string Name = "Untitled";
    std::string StartupScene = "Scenes/Main.scene";
    std::string MainScript;            // optional global script run at startup
    u32  Width = 1280;
    u32  Height = 720;
    bool VSync = true;
    u32  MSAA = 4;

    // Game graphics settings (used by the player / exported game).
    int   ShadowQuality = 2;      // 0 off, 1 low (1024), 2 medium (2048), 3 high (4096)
    float ShadowDistance = 45.0f;
    u32   MaxFPS = 0;             // 0 = unlimited (or vsync)
    bool  Fullscreen = false;
    float Exposure = 1.0f;

    std::string Directory;             // folder that contains project.lapis

    static constexpr const char* FileName = "project.lapis";

    static std::optional<Project> Load(const std::string& directory);
    static std::optional<Project> Parse(const std::string& text, const std::string& directory);
    std::string Serialize() const;
    bool Save() const;

    std::string Path(const std::string& relative) const;
};

} // namespace Lapis
