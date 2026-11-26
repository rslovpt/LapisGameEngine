#pragma once
// Game packaging ("cook + stage"), shared by LapisPackager and the editor's
// File > Export Game:
//
//   <OutputDir>/<Game>.exe   copy of the LapisPlayer runtime
//   <OutputDir>/Content/     the project folder
//   <OutputDir>/README.txt
//
// The project is validated first: scenes must parse, referenced models,
// textures and scripts must exist and every .luau file must compile.

#include <functional>
#include <string>
#include <vector>

#include "Lapis/Runtime/Project.h"

namespace Lapis::Packager {

struct Result {
    bool Ok = false;
    std::string OutputDir;
    std::string ExePath;
    size_t FilesCopied = 0;
    std::vector<std::string> Messages;   // progress / info lines
    std::vector<std::string> Errors;
};

// Validation only (no files written).
Result Validate(const Project& project);

// Progress callback: fraction 0..1 and a short description of the current step.
// Called from the thread that runs Package().
using ProgressFn = std::function<void(float fraction, const std::string& step)>;

// Validates, then writes the packaged game. runtimePath = LapisPlayer executable.
Result Package(const Project& project, const std::string& runtimePath, const std::string& outputDir,
               const ProgressFn& progress = nullptr);

// "My Game!" -> "My_Game_"
std::string SafeName(const std::string& name);

// Default runtime location: LapisPlayer(.exe) next to the running executable.
std::string DefaultRuntimePath();

} // namespace Lapis::Packager
