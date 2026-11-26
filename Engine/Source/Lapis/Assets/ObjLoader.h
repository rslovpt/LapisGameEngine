#pragma once
// Wavefront OBJ loader: positions, UVs, normals, polygon faces (fan
// triangulated), negative indices. Smooth normals are generated if missing.

#include <optional>
#include <string>

#include "Lapis/Renderer/Mesh.h"

namespace Lapis::ObjLoader {

std::optional<MeshData> Parse(const std::string& objText);
std::optional<MeshData> Load(const std::string& path);

} // namespace Lapis::ObjLoader
