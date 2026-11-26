#pragma once
// Small helpers shared by the Lapis executables (player, packager, diagnostics).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "Lapis/Core/FileSystem.h"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace LapisApp {

struct Args {
    std::vector<std::string> List;
    Args(int argc, char** argv) { for (int i = 1; i < argc; ++i) List.emplace_back(argv[i]); }

    bool Has(const std::string& flag) const {
        for (const auto& a : List) if (a == flag) return true;
        return false;
    }
    std::string Value(const std::string& flag, const std::string& def = "") const {
        for (size_t i = 0; i + 1 < List.size(); ++i) if (List[i] == flag) return List[i + 1];
        return def;
    }
    // Positional arguments (anything not starting with "--" and not a flag value).
    std::vector<std::string> Positional(const std::vector<std::string>& flagsWithValues) const {
        std::vector<std::string> out;
        for (size_t i = 0; i < List.size(); ++i) {
            const std::string& a = List[i];
            if (a.rfind("--", 0) == 0) {
                for (const auto& f : flagsWithValues) if (a == f) { ++i; break; }
                continue;
            }
            out.push_back(a);
        }
        return out;
    }
};

inline std::string ExeDir() { return Lapis::FileSystem::ExecutableDirectory(); }

inline bool HasProject(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "project.lapis", ec);
}

// Finds a project: explicit path > packaged "Content" next to the exe >
// the repository's Projects/Sandbox (for development builds).
inline std::string FindProject(const std::string& explicitPath) {
    namespace fs = std::filesystem;
    if (!explicitPath.empty()) return explicitPath;
    const fs::path exe = ExeDir();
    if (HasProject(exe / "Content")) return (exe / "Content").string();
    for (const char* rel : {"../Projects/Sandbox", "../../Projects/Sandbox", "../../../Projects/Sandbox", "Projects/Sandbox"}) {
        const fs::path p = (exe / rel).lexically_normal();
        if (HasProject(p)) return p.string();
    }
    return {};
}

inline void ShowError(const std::string& title, const std::string& message) {
    std::fprintf(stderr, "%s: %s\n", title.c_str(), message.c_str());
#if defined(_WIN32)
    MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
#endif
}

// GUI-subsystem executables have no console; --console attaches one.
inline void AttachConsole() {
#if defined(_WIN32)
    if (AllocConsole()) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
}

// Opens a file or folder with the OS default handler (Explorer, VS Code, ...).
inline void OpenInShell(const std::string& path) {
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    if (std::getenv("LAPIS_NO_SHELL_OPEN")) return;
    const std::string cmd = "xdg-open '" + path + "' >/dev/null 2>&1 &";
    [[maybe_unused]] int r = std::system(cmd.c_str());
#endif
}

// Opens a folder or file in Visual Studio Code (the `code` command that VS
// Code installs on PATH). Falls back to the default handler.
inline void OpenInVSCode(const std::string& path) {
#if defined(_WIN32)
    const std::string args = "/c code \"" + path + "\"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, args.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, args.c_str(), -1, w.data(), n);
    const auto result = (INT_PTR)ShellExecuteW(nullptr, L"open", L"cmd.exe", w.c_str(), nullptr, SW_HIDE);
    if (result <= 32) OpenInShell(path);
#else
    if (std::getenv("LAPIS_NO_SHELL_OPEN")) return;
    const std::string cmd = "code '" + path + "' >/dev/null 2>&1 || xdg-open '" + path + "' >/dev/null 2>&1 &";
    [[maybe_unused]] int r = std::system(cmd.c_str());
#endif
}

inline void SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

} // namespace LapisApp
