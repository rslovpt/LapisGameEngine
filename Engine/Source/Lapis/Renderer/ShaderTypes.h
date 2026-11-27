#pragma once

// Constant buffer layouts shared by C++, GLSL (std140 uniform blocks CB0/CB1)
// and HLSL (cbuffer b0/b1). Only Vec4 / Mat4 members, so every API packs
// them identically. Integers and flags are stored in float lanes.

#include "Lapis/Math/Math.h"

namespace Lapis::ShaderTypes {

constexpr int MaxPointLights = 16;

struct PointLightData {
    Vec4 PositionRange;   // xyz position, w range
    Vec4 Color;           // rgb = color * intensity
};

struct LitPass {                 // CB0 of "Lit"
    Mat4 ViewProjection;
    Mat4 LightSpace;
    Vec4 CameraPos;              // xyz
    Vec4 SunDirection;           // xyz, direction the light travels
    Vec4 SunColor;               // rgb
    Vec4 AmbientSky;
    Vec4 AmbientGround;
    Vec4 FogColorDensity;        // rgb color, w density
    Vec4 SkyZenith;
    Vec4 SkyHorizon;
    Vec4 SkyGround;
    Vec4 Params;                 // x env intensity, y shadow texel, z shadows enabled, w PCF radius
    Vec4 LightCount;             // x number of point lights
    PointLightData Lights[MaxPointLights];
};

struct Material {                // CB1 of "Lit"
    Vec4 Albedo;                 // sRGB rgba
    Vec4 EmissiveMetallic;       // rgb emissive (linear), w metallic
    Vec4 Params;                 // x roughness, y unlit, zw tiling
};

struct MatrixOnly {              // CB0 of "Shadow" and "Line"
    Mat4 Matrix;
};

struct Sky {                     // CB0 of "Sky"
    Mat4 InvViewProjection;
    Vec4 CameraPos;
    Vec4 SunDirection;
    Vec4 SunColor;
    Vec4 Zenith;
    Vec4 Horizon;
    Vec4 Ground;
};

struct SSAO {                    // CB0 of "SSAO"
    Mat4 Projection;
    Mat4 InvProjection;
    Vec4 Params;                 // xy depth texel size, z radius
};

struct Texel {                   // CB0 of "Blur", "BloomUp", "FXAA"
    Vec4 Params;                 // xy texel size (of the source)
};

struct BloomDown {               // CB0 of "BloomDown"
    Vec4 Params;                 // xy source texel, z first pass, w threshold
};

struct Composite {               // CB0 of "Composite"
    Mat4 InvViewProj;
    Mat4 PrevViewProj;
    Vec4 A;                      // exposure, tonemapper, contrast, saturation
    Vec4 B;                      // temperature, gamma, vignette, grain
    Vec4 C;                      // chromatic aberration, bloom intensity, AO intensity, motion blur
    Vec4 D;                      // time
};

struct Upscale {                 // CB0 of "Upscale"
    Vec4 Params;                 // xy source size, z mode (0 bilinear, 1 spatial)
};

struct Sharpen {                 // CB0 of "Sharpen"
    Vec4 Params;                 // xy texel, z sharpness
};

struct UI {                      // CB0 of "UI"
    Vec4 Screen;                 // xy screen size in UI units
};

} // namespace Lapis::ShaderTypes
