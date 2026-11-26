#include "Lapis/Assets/AssetManager.h"

#include <filesystem>

#include "Lapis/Assets/ImageLoader.h"
#include "Lapis/Assets/ObjLoader.h"
#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"

namespace Lapis {

std::string AssetManager::Resolve(const std::string& path) const {
    const std::filesystem::path p(path);
    if (p.is_absolute() || m_Root.empty()) return path;
    return (std::filesystem::path(m_Root) / p).lexically_normal().string();
}

Ref<Mesh> AssetManager::GetMesh(const std::string& name) {
    if (auto it = m_Meshes.find(name); it != m_Meshes.end()) return it->second;

    Ref<Mesh> mesh;
    if (name == "Cube")        mesh = CreateRef<Mesh>(MeshFactory::Cube(), name);
    else if (name == "Sphere") mesh = CreateRef<Mesh>(MeshFactory::Sphere(), name);
    else if (name == "Plane")  mesh = CreateRef<Mesh>(MeshFactory::Plane(), name);
    else if (auto data = ObjLoader::Load(Resolve(name))) mesh = CreateRef<Mesh>(*data, name);

    if (!mesh) {
        LAPIS_ERROR("Assets: mesh '{}' not found - using Cube", name);
        mesh = GetMesh("Cube");
    }
    m_Meshes[name] = mesh;
    return mesh;
}

Ref<Texture2D> AssetManager::GetTexture(const std::string& name) {
    if (auto it = m_Textures.find(name); it != m_Textures.end()) return it->second;

    Ref<Texture2D> tex;
    if (name == "White") tex = Texture2D::CreateSolid({1, 1, 1, 1});
    else if (name == "Checker") tex = Texture2D::CreateChecker(256, 8, {0.92f, 0.92f, 0.92f, 1}, {0.62f, 0.62f, 0.66f, 1});
    else if (auto img = ImageLoader::Load(Resolve(name))) { tex = CreateRef<Texture2D>(*img); tex->SetPath(name); }

    if (!tex) {
        LAPIS_ERROR("Assets: texture '{}' not found - using Checker", name);
        tex = GetTexture("Checker");
    } else {
        tex->SetPath(name);
    }
    m_Textures[name] = tex;
    return tex;
}

Ref<Material> AssetManager::GetSharedMaterial(const std::string& texture, float roughness, float metallic, const Vec3& emissive,
                                              const Vec2& tiling, bool unlit, bool castShadows, float alpha) {
    const std::string key = Format("{}|{}|{}|{},{},{}|{},{}|{}|{}|{}", texture, roughness, metallic, emissive.x, emissive.y,
                                   emissive.z, tiling.x, tiling.y, unlit, castShadows, alpha);
    if (auto it = m_Materials.find(key); it != m_Materials.end()) return it->second;
    auto m = CreateRef<Material>();
    m->Name = "Shared";
    m->Albedo = {1, 1, 1, alpha};
    m->Roughness = roughness;
    m->Metallic = metallic;
    m->Emissive = emissive;
    m->Tiling = tiling;
    m->Unlit = unlit;
    m->CastShadows = castShadows;
    if (!texture.empty()) m->AlbedoMap = GetTexture(texture);
    m_Materials[key] = m;
    return m;
}

std::optional<std::string> AssetManager::ReadText(const std::string& path) const {
    return FileSystem::ReadText(Resolve(path));
}

} // namespace Lapis
