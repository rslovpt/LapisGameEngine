#pragma once
// Loads and caches GPU assets by name. Names are either built-ins
// ("Cube", "Sphere", "Plane", "Checker", "White") or paths relative to the
// asset root (a project's Content folder).

#include <optional>
#include <string>
#include <unordered_map>

#include "Lapis/Renderer/Mesh.h"
#include "Lapis/Renderer/Texture.h"

namespace Lapis {

class AssetManager : NonCopyable {
public:
    explicit AssetManager(std::string root = ".") : m_Root(std::move(root)) {}

    void SetRoot(const std::string& root) { m_Root = root; }
    const std::string& GetRoot() const { return m_Root; }

    // Absolute paths are returned unchanged; others are joined to the root.
    std::string Resolve(const std::string& path) const;

    Ref<Mesh>      GetMesh(const std::string& name);
    Ref<Texture2D> GetTexture(const std::string& name);
    std::optional<std::string> ReadText(const std::string& path) const;

    void Clear() { m_Meshes.clear(); m_Textures.clear(); }
    size_t MeshCount() const { return m_Meshes.size(); }
    size_t TextureCount() const { return m_Textures.size(); }

private:
    std::string m_Root;
    std::unordered_map<std::string, Ref<Mesh>> m_Meshes;
    std::unordered_map<std::string, Ref<Texture2D>> m_Textures;
};

} // namespace Lapis
