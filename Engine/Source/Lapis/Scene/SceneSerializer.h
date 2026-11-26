#pragma once
// Human-readable scene files (*.scene). One component per line, attached
// to the most recent `Entity` line:
//
//   # Lapis Scene v1
//   Environment fog=0.006 exposure=1
//   Entity id=1 name="Ground"
//     Transform position=0,-0.5,0 rotation=0,0,0 scale=40,1,40
//     MeshRenderer mesh="Cube" color=0.8,0.8,0.8,1 roughness=0.9 texture="Checker" tiling=20,20
//     Collider shape=Box
//   Entity id=2 name="Ball"
//     Transform position=0,5,0 parent=1
//     Script path="Scripts/Ball.luau"
//
// Values are numbers, comma-separated vectors, booleans (0/1/true/false) or
// "quoted strings". Rotations are Euler angles in degrees. `parent` refers to
// another entity's `id` in the same file.

#include <string>

namespace Lapis {

class Scene;

namespace SceneSerializer {

std::string Serialize(Scene& scene);
bool Save(Scene& scene, const std::string& path);

// loadAssets=false skips GPU uploads (meshes/textures) - used by tests/tools.
bool Deserialize(Scene& scene, const std::string& text, std::string* error = nullptr, bool loadAssets = true);
bool Load(Scene& scene, const std::string& path, bool loadAssets = true);

} // namespace SceneSerializer
} // namespace Lapis
