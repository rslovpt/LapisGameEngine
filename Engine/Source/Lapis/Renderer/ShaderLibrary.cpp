#include "Lapis/Renderer/ShaderLibrary.h"

#include <cstring>

#include "Lapis/Math/Math.h"
#include "Lapis/Renderer/ShaderTypes.h"

namespace Lapis::ShaderLibrary {

#include "Shaders/GLSL.inl"
#include "Shaders/HLSL.inl"

// Constant buffers must stay 16-byte granular (identical std140 / HLSL packing).
static_assert(sizeof(Vec4) == 16 && sizeof(Mat4) == 64, "unexpected math type layout");
static_assert(sizeof(ShaderTypes::LitPass) % 16 == 0 && sizeof(ShaderTypes::Material) % 16 == 0, "cbuffer size");
static_assert(sizeof(ShaderTypes::Composite) % 16 == 0 && sizeof(ShaderTypes::Sky) % 16 == 0, "cbuffer size");

namespace {
const Source s_Shaders[] = {
    {"Lit", GLSL::LitVS, GLSL::LitFS, HLSL::Lit},
    {"Shadow", GLSL::ShadowVS, GLSL::ShadowFS, HLSL::Shadow},
    {"Line", GLSL::LineVS, GLSL::LineFS, HLSL::Line},
    {"Sky", GLSL::FullscreenVS, GLSL::SkyFS, HLSL::Sky},
    {"SSAO", GLSL::FullscreenVS, GLSL::SSAOFS, HLSL::SSAO},
    {"Blur", GLSL::FullscreenVS, GLSL::BlurFS, HLSL::Blur},
    {"BloomDown", GLSL::FullscreenVS, GLSL::BloomDownFS, HLSL::BloomDown},
    {"BloomUp", GLSL::FullscreenVS, GLSL::BloomUpFS, HLSL::BloomUp},
    {"Composite", GLSL::FullscreenVS, GLSL::CompositeFS, HLSL::Composite},
    {"FXAA", GLSL::FullscreenVS, GLSL::FXAAFS, HLSL::FXAA},
    {"Upscale", GLSL::FullscreenVS, GLSL::UpscaleFS, HLSL::Upscale},
    {"Sharpen", GLSL::FullscreenVS, GLSL::SharpenFS, HLSL::Sharpen},
    {"UI", GLSL::UIVS, GLSL::UIFS, HLSL::UI},
    {"DepthResolve", nullptr, nullptr, HLSL::DepthResolve},
};
} // namespace

const Source* Find(const char* name) {
    for (const Source& s : s_Shaders)
        if (std::strcmp(s.Name, name) == 0) return &s;
    return nullptr;
}

const Source* All(int& count) {
    count = (int)(sizeof(s_Shaders) / sizeof(s_Shaders[0]));
    return s_Shaders;
}

} // namespace Lapis::ShaderLibrary
