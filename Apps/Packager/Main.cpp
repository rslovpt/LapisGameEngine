// LapisPackager - command-line game packaging (same code as the editor's
// File > Export Game). See Lapis/Runtime/Packager.h.
//
// Usage: LapisPackager <projectDir> [outputDir] [--runtime <LapisPlayer path>]

#include <cstdio>
#include <filesystem>

#include "../Common/AppUtils.h"

#include "Lapis/Lapis.h"
#include "Lapis/Runtime/Packager.h"

using namespace Lapis;

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

    const std::string out = positional.size() > 1
        ? positional[1]
        : (std::filesystem::current_path() / "Export" / Packager::SafeName(project->Name)).string();
    std::printf("Packaging project '%s'\n", project->Name.c_str());
    const auto result = Packager::Package(*project, args.Value("--runtime", Packager::DefaultRuntimePath()), out);
    for (const auto& m : result.Messages) std::printf("  %s\n", m.c_str());
    for (const auto& e : result.Errors) std::fprintf(stderr, "  ERROR: %s\n", e.c_str());
    if (!result.Ok) {
        std::fprintf(stderr, "\nPackaging FAILED: %zu error(s). Nothing was written.\n", result.Errors.size());
        return 2;
    }
    std::printf("\nPackaged -> %s\n  %s\n  Content/ (%zu files)\n", result.OutputDir.c_str(), result.ExePath.c_str(), result.FilesCopied);
    return 0;
}
