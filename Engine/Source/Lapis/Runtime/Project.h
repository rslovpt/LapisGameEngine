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

    std::string Directory;             // folder that contains project.lapis

    static constexpr const char* FileName = "project.lapis";

    static std::optional<Project> Load(const std::string& directory);
    static std::optional<Project> Parse(const std::string& text, const std::string& directory);
    std::string Serialize() const;
    bool Save() const;

    std::string Path(const std::string& relative) const;
};

} // namespace Lapis
