#include "Lapis/Core/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#if LAPIS_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif LAPIS_PLATFORM_LINUX
    #include <unistd.h>
    #include <climits>
#endif

namespace Lapis::FileSystem {

bool Exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::optional<std::string> ReadText(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::optional<std::vector<u8>> ReadBinary(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const std::streamsize size = file.tellg();
    if (size < 0) return std::nullopt;
    std::vector<u8> data(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) return std::nullopt;
    return data;
}

bool WriteText(const std::string& path, const std::string& contents) {
    return WriteBinary(path, contents.data(), contents.size());
}

bool WriteBinary(const std::string& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(file);
}

std::string DirectoryOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string ExtensionOf(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string ExecutableDirectory() {
#if LAPIS_PLATFORM_WINDOWS
    char buffer[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return DirectoryOf(std::string(buffer, len));
#elif LAPIS_PLATFORM_LINUX
    char buffer[PATH_MAX] = {};
    const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    return len > 0 ? DirectoryOf(std::string(buffer, static_cast<size_t>(len))) : std::string("./");
#else
    return "./";
#endif
}

} // namespace Lapis::FileSystem
