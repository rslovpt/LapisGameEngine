#pragma once
// GLSL sources for the engine's built-in shaders, embedded so the engine
// works without shipping shader files. GLSL 330 core = runs on GL 3.3+.

namespace Lapis::BuiltinShaders {

// ---------------------------------------------------------------------------
// Lit: Blinn-Phong style BRDF (energy-normalized), directional sun with PCF
// shadows, up to 16 point lights, hemisphere ambient, exponential fog.
// ---------------------------------------------------------------------------
inline const char* LitVertex = R"(
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in mat4 a_Model;   // per instance
layout(location = 7) in vec4 a_Tint;    // per instance

uniform mat4 u_ViewProjection;
uniform mat4 u_LightSpace;
uniform vec2 u_Tiling;

out vec3 v_WorldPos;
out vec3 v_Normal;
out vec2 v_UV;
out vec4 v_LightSpacePos;
out vec4 v_Tint;

void main() {
    v_Tint = a_Tint;
    vec4 world = a_Model * vec4(a_Position, 1.0);
    v_WorldPos = world.xyz;
    v_Normal = transpose(inverse(mat3(a_Model))) * a_Normal;
    v_UV = a_UV * u_Tiling;
    v_LightSpacePos = u_LightSpace * world;
    gl_Position = u_ViewProjection * world;
}
)";

inline const char* LitFragment = R"(
#version 330 core
#define MAX_POINT_LIGHTS 16
const float PI = 3.14159265;

struct PointLight {
    vec3  Position;
    vec3  Color;     // color * intensity
    float Range;
};

in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_UV;
in vec4 v_LightSpacePos;
in vec4 v_Tint;

uniform vec4      u_Albedo;       // sRGB
uniform sampler2D u_AlbedoMap;    // sRGB texture (hardware linearized)
uniform vec3      u_Emissive;
uniform float     u_Metallic;
uniform float     u_Roughness;
uniform int       u_Unlit;

uniform vec3 u_CameraPos;
uniform vec3 u_SunDirection;      // direction the light travels
uniform vec3 u_SunColor;          // color * intensity
uniform vec3 u_AmbientSky;
uniform vec3 u_AmbientGround;

uniform int        u_PointLightCount;
uniform PointLight u_PointLights[MAX_POINT_LIGHTS];

uniform sampler2DShadow u_ShadowMap;
uniform int   u_ShadowsEnabled;
uniform float u_ShadowTexel;
uniform int   u_PCFRadius;   // 0 hard, 1 = 3x3, 2 = 5x5

uniform vec3  u_FogColor;
uniform float u_FogDensity;

uniform vec3  u_SkyZenith;
uniform vec3  u_SkyHorizon;
uniform vec3  u_SkyGround;
uniform float u_EnvIntensity;

out vec4 o_Color;

vec3 SkyColor(vec3 d) {
    float h = d.y;
    return h > 0.0 ? mix(u_SkyHorizon, u_SkyZenith, pow(h, 0.5))
                   : mix(u_SkyHorizon, u_SkyGround, pow(min(-h * 4.0, 1.0), 0.6));
}

float SunShadow(vec3 N, vec3 L) {
    if (u_ShadowsEnabled == 0) return 1.0;
    vec3 p = v_LightSpacePos.xyz / v_LightSpacePos.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0005);
    float lit = 0.0;
    for (int x = -u_PCFRadius; x <= u_PCFRadius; ++x)
        for (int y = -u_PCFRadius; y <= u_PCFRadius; ++y)
            lit += texture(u_ShadowMap, vec3(p.xy + vec2(x, y) * u_ShadowTexel, p.z - bias));
    float n = float(2 * u_PCFRadius + 1);
    return lit / (n * n);
}

vec3 Shade(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3  H = normalize(L + V);
    float a = max(u_Roughness * u_Roughness, 0.002);
    float shininess = clamp(2.0 / (a * a) - 2.0, 1.0, 4096.0);
    float spec = pow(max(dot(N, H), 0.0), shininess) * (shininess + 8.0) / (8.0 * PI);
    vec3  F0 = mix(vec3(0.04), albedo, u_Metallic);
    vec3  F  = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3  kd = (1.0 - F) * (1.0 - u_Metallic);
    return (kd * albedo / PI + F * spec) * radiance * NdotL;
}

void main() {
    vec4 tex = texture(u_AlbedoMap, v_UV);
    vec3 albedo = pow(u_Albedo.rgb * v_Tint.rgb, vec3(2.2)) * tex.rgb;
    float alpha = u_Albedo.a * v_Tint.a * tex.a;

    if (u_Unlit != 0) {
        o_Color = vec4(albedo + u_Emissive, alpha);
        return;
    }

    vec3 N = normalize(v_Normal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(u_CameraPos - v_WorldPos);

    // Sun
    vec3 L = normalize(-u_SunDirection);
    vec3 color = Shade(N, V, L, u_SunColor, albedo) * SunShadow(N, L);

    // Point lights
    for (int i = 0; i < u_PointLightCount; ++i) {
        vec3  toLight = u_PointLights[i].Position - v_WorldPos;
        float d = length(toLight);
        float window = clamp(1.0 - pow(d / u_PointLights[i].Range, 4.0), 0.0, 1.0);
        float atten = window * window / (d * d + 1.0);
        color += Shade(N, V, toLight / max(d, 1e-4), u_PointLights[i].Color * atten, albedo);
    }

    // Hemisphere ambient (diffuse)
    vec3 ambient = mix(u_AmbientGround, u_AmbientSky, N.y * 0.5 + 0.5);
    color += ambient * albedo * (1.0 - u_Metallic);

    // Environment reflection from the analytic sky (cheap stand-in for IBL)
    vec3  R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);
    vec3  F0 = mix(vec3(0.04), albedo, u_Metallic);
    vec3  Fenv = F0 + (max(vec3(1.0 - u_Roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec3  env = mix(SkyColor(R), (u_SkyHorizon + u_SkyZenith) * 0.5, u_Roughness);
    color += env * Fenv * u_EnvIntensity;
    color += u_Emissive;

    // Fog
    float dist = length(u_CameraPos - v_WorldPos);
    float fog = 1.0 - exp(-pow(dist * u_FogDensity, 2.0));
    color = mix(color, u_FogColor, clamp(fog, 0.0, 1.0));

    o_Color = vec4(color, alpha);
}
)";

// ---------------------------------------------------------------------------
// Shadow depth pass
// ---------------------------------------------------------------------------
inline const char* ShadowVertex = R"(
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 3) in mat4 a_Model;   // per instance
uniform mat4 u_LightSpace;
void main() { gl_Position = u_LightSpace * a_Model * vec4(a_Position, 1.0); }
)";

inline const char* ShadowFragment = R"(
#version 330 core
void main() {}
)";

// ---------------------------------------------------------------------------
// Fullscreen triangle (no vertex buffers; draw 3 vertices)
// ---------------------------------------------------------------------------
inline const char* FullscreenVertex = R"(
#version 330 core
out vec2 v_UV;
out vec2 v_NDC;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_UV = p;
    v_NDC = p * 2.0 - 1.0;
    gl_Position = vec4(v_NDC, 1.0, 1.0); // far plane: sky only fills empty pixels
}
)";

// Procedural sky: gradient + sun disk.
inline const char* SkyFragment = R"(
#version 330 core
in vec2 v_NDC;
uniform mat4 u_InverseViewProjection;
uniform vec3 u_CameraPos;
uniform vec3 u_SunDirection;
uniform vec3 u_SunColor;
uniform vec3 u_SkyZenith;
uniform vec3 u_SkyHorizon;
uniform vec3 u_SkyGround;
out vec4 o_Color;
void main() {
    vec4 world = u_InverseViewProjection * vec4(v_NDC, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w - u_CameraPos);
    float h = dir.y;
    vec3 sky = h > 0.0 ? mix(u_SkyHorizon, u_SkyZenith, pow(h, 0.5))
                       : mix(u_SkyHorizon, u_SkyGround, pow(min(-h * 4.0, 1.0), 0.6));
    float s = max(dot(dir, -normalize(u_SunDirection)), 0.0);
    sky += u_SunColor * (pow(s, 1200.0) * 4.0 + pow(s, 12.0) * 0.06);
    o_Color = vec4(sky, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Debug lines (position + color)
// ---------------------------------------------------------------------------
inline const char* LineVertex = R"(
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
uniform mat4 u_ViewProjection;
out vec4 v_Color;
void main() { v_Color = a_Color; gl_Position = u_ViewProjection * vec4(a_Position, 1.0); }
)";

inline const char* LineFragment = R"(
#version 330 core
in vec4 v_Color;
out vec4 o_Color;
void main() { o_Color = vec4(pow(v_Color.rgb, vec3(2.2)) * 2.0, v_Color.a); }
)";

} // namespace Lapis::BuiltinShaders
