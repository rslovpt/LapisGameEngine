#pragma once

#include <string>

#include "Lapis/Core/Base.h"
#include "Lapis/Math/Math.h"
#include "Lapis/Renderer/Texture.h"

namespace Lapis {

// Surface description for the built-in lit shader.
struct Material {
    std::string    Name = "Material";
    Vec4           Albedo{1.0f, 1.0f, 1.0f, 1.0f};  // sRGB color, multiplied with AlbedoMap
    Ref<Texture2D> AlbedoMap;                       // optional
    Vec2           Tiling{1.0f, 1.0f};
    Vec3           Emissive{0.0f};                  // linear HDR emission
    float          Metallic  = 0.0f;                // 0 = dielectric, 1 = metal (tints specular)
    float          Roughness = 0.5f;                // 0 = mirror-sharp highlight, 1 = very broad
    bool           CastShadows = true;
    bool           Unlit = false;

    static Ref<Material> Create(const std::string& name, const Vec4& albedo, float roughness = 0.5f, float metallic = 0.0f) {
        auto m = CreateRef<Material>();
        m->Name = name;
        m->Albedo = albedo;
        m->Roughness = roughness;
        m->Metallic = metallic;
        return m;
    }
};

} // namespace Lapis
