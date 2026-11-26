#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Lapis/Core/Base.h"

namespace Lapis::FileSystem {

bool Exists(const std::string& path);

std::optional<std::string>     ReadText(const std::string& path);
std::optional<std::vector<u8>> ReadBinary(const std::string& path);

bool WriteText(const std::string& path, const std::string& contents);
bool WriteBinary(const std::string& path, const void* data, size_t size);

// "Assets/Models/cube.obj" -> "Assets/Models/"
std::string DirectoryOf(const std::string& path);
// "Assets/Models/cube.obj" -> ".obj" (lower-case)
std::string ExtensionOf(const std::string& path);

// Directory containing the running executable (used to find Assets/).
std::string ExecutableDirectory();

} // namespace Lapis::FileSystem
