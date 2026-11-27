// GLSL 330 versions of the built-in shaders (included by ShaderLibrary.cpp).
// Constant buffers: see ShaderTypes.h. Samplers t0..t7 are bound to texture
// units 0..7 by the OpenGL backend.

namespace GLSL {

// ---------------------------------------------------------------------------
// Fullscreen triangle (3 vertices, no buffers). v_UV has (0,0) bottom-left.
// ---------------------------------------------------------------------------
inline const char* FullscreenVS = R"(
#version 330 core
out vec2 v_UV;
out vec2 v_NDC;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_UV = p;
    v_NDC = p * 2.0 - 1.0;
    gl_Position = vec4(v_NDC, 1.0, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Lit
// ---------------------------------------------------------------------------
#define LAPIS_GLSL_LIT_CB0 R"(
struct PointLight { vec4 PositionRange; vec4 Color; };
layout(std140) uniform CB0 {
    mat4 u_ViewProjection;
    mat4 u_LightSpace;
    vec4 u_CameraPos;
    vec4 u_SunDirection;
    vec4 u_SunColor;
    vec4 u_AmbientSky;
    vec4 u_AmbientGround;
    vec4 u_FogColorDensity;
    vec4 u_SkyZenith;
    vec4 u_SkyHorizon;
    vec4 u_SkyGround;
    vec4 u_Params;        // env intensity, shadow texel, shadows enabled, PCF radius
    vec4 u_LightCount;
    PointLight u_Lights[16];
};
layout(std140) uniform CB1 {
    vec4 u_Albedo;
    vec4 u_EmissiveMetallic;
    vec4 u_MaterialParams;   // roughness, unlit, tiling.xy
};
)"

inline const char* LitVS = "#version 330 core\n" LAPIS_GLSL_LIT_CB0 R"(
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in mat4 a_Model;   // per instance (locations 3-6)
layout(location = 7) in vec4 a_Tint;    // per instance
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
    v_UV = a_UV * u_MaterialParams.zw;
    v_LightSpacePos = u_LightSpace * world;
    gl_Position = u_ViewProjection * world;
}
)";

inline const char* LitFS = "#version 330 core\n" LAPIS_GLSL_LIT_CB0 R"(
const float PI = 3.14159265;
in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_UV;
in vec4 v_LightSpacePos;
in vec4 v_Tint;
uniform sampler2D t0;          // albedo (sRGB texture)
uniform sampler2DShadow t1;    // sun shadow map
out vec4 o_Color;

vec3 SkyColor(vec3 d) {
    float h = d.y;
    return h > 0.0 ? mix(u_SkyHorizon.rgb, u_SkyZenith.rgb, pow(h, 0.5))
                   : mix(u_SkyHorizon.rgb, u_SkyGround.rgb, pow(min(-h * 4.0, 1.0), 0.6));
}

float SunShadow(vec3 N, vec3 L) {
    if (u_Params.z < 0.5) return 1.0;
    vec3 p = v_LightSpacePos.xyz / v_LightSpacePos.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0005);
    int r = int(u_Params.w + 0.5);
    float lit = 0.0;
    for (int x = -r; x <= r; ++x)
        for (int y = -r; y <= r; ++y)
            lit += texture(t1, vec3(p.xy + vec2(x, y) * u_Params.y, p.z - bias));
    float n = float(2 * r + 1);
    return lit / (n * n);
}

vec3 Shade(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float roughness, float metallic) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3  H = normalize(L + V);
    float a = max(roughness * roughness, 0.002);
    float shininess = clamp(2.0 / (a * a) - 2.0, 1.0, 4096.0);
    float spec = pow(max(dot(N, H), 0.0), shininess) * (shininess + 8.0) / (8.0 * PI);
    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    vec3  F  = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3  kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + F * spec) * radiance * NdotL;
}

void main() {
    float roughness = u_MaterialParams.x;
    float metallic = u_EmissiveMetallic.w;
    vec3 emissive = u_EmissiveMetallic.rgb;
    vec4 tex = texture(t0, v_UV);
    vec3 albedo = pow(u_Albedo.rgb * v_Tint.rgb, vec3(2.2)) * tex.rgb;
    float alpha = u_Albedo.a * v_Tint.a * tex.a;
    if (u_MaterialParams.y > 0.5) { o_Color = vec4(albedo + emissive, alpha); return; }

    vec3 N = normalize(v_Normal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(u_CameraPos.xyz - v_WorldPos);
    vec3 L = normalize(-u_SunDirection.xyz);
    vec3 color = Shade(N, V, L, u_SunColor.rgb, albedo, roughness, metallic) * SunShadow(N, L);

    int count = int(u_LightCount.x + 0.5);
    for (int i = 0; i < count; ++i) {
        vec3  toLight = u_Lights[i].PositionRange.xyz - v_WorldPos;
        float d = length(toLight);
        float window = clamp(1.0 - pow(d / u_Lights[i].PositionRange.w, 4.0), 0.0, 1.0);
        float atten = window * window / (d * d + 1.0);
        color += Shade(N, V, toLight / max(d, 1e-4), u_Lights[i].Color.rgb * atten, albedo, roughness, metallic);
    }

    vec3 ambient = mix(u_AmbientGround.rgb, u_AmbientSky.rgb, N.y * 0.5 + 0.5);
    color += ambient * albedo * (1.0 - metallic);

    vec3  R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);
    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    vec3  Fenv = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    vec3  env = mix(SkyColor(R), (u_SkyHorizon.rgb + u_SkyZenith.rgb) * 0.5, roughness);
    color += env * Fenv * u_Params.x;
    color += emissive;

    float dist = length(u_CameraPos.xyz - v_WorldPos);
    float fog = 1.0 - exp(-pow(dist * u_FogColorDensity.w, 2.0));
    color = mix(color, u_FogColorDensity.rgb, clamp(fog, 0.0, 1.0));
    o_Color = vec4(color, alpha);
}
)";
#undef LAPIS_GLSL_LIT_CB0

// ---------------------------------------------------------------------------
// Shadow depth, debug lines
// ---------------------------------------------------------------------------
inline const char* ShadowVS = R"(
#version 330 core
layout(std140) uniform CB0 { mat4 u_LightSpace; };
layout(location = 0) in vec3 a_Position;
layout(location = 3) in mat4 a_Model;
void main() { gl_Position = u_LightSpace * a_Model * vec4(a_Position, 1.0); }
)";
inline const char* ShadowFS = R"(
#version 330 core
void main() {}
)";

inline const char* LineVS = R"(
#version 330 core
layout(std140) uniform CB0 { mat4 u_ViewProjection; };
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
out vec4 v_Color;
void main() { v_Color = a_Color; gl_Position = u_ViewProjection * vec4(a_Position, 1.0); }
)";
inline const char* LineFS = R"(
#version 330 core
in vec4 v_Color;
out vec4 o_Color;
void main() { o_Color = vec4(pow(v_Color.rgb, vec3(2.2)) * 2.0, v_Color.a); }
)";

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------
inline const char* SkyFS = R"(
#version 330 core
layout(std140) uniform CB0 {
    mat4 u_InverseViewProjection;
    vec4 u_CameraPos;
    vec4 u_SunDirection;
    vec4 u_SunColor;
    vec4 u_SkyZenith;
    vec4 u_SkyHorizon;
    vec4 u_SkyGround;
};
in vec2 v_NDC;
out vec4 o_Color;
void main() {
    vec4 world = u_InverseViewProjection * vec4(v_NDC, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w - u_CameraPos.xyz);
    float h = dir.y;
    vec3 sky = h > 0.0 ? mix(u_SkyHorizon.rgb, u_SkyZenith.rgb, pow(h, 0.5))
                       : mix(u_SkyHorizon.rgb, u_SkyGround.rgb, pow(min(-h * 4.0, 1.0), 0.6));
    float s = max(dot(dir, -normalize(u_SunDirection.xyz)), 0.0);
    sky += u_SunColor.rgb * (pow(s, 1200.0) * 4.0 + pow(s, 12.0) * 0.06);
    o_Color = vec4(sky, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Post processing
// ---------------------------------------------------------------------------
inline const char* SSAOFS = R"(
#version 330 core
layout(std140) uniform CB0 { mat4 u_Projection; mat4 u_InvProjection; vec4 u_Params; };
in vec2 v_UV;
uniform sampler2D t0;   // depth
out vec4 o_Color;
vec3 ViewPos(vec2 uv) {
    float d = texture(t0, uv).r;
    vec4 p = u_InvProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
float Hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec2 texel = u_Params.xy;
    float radius = u_Params.z;
    float depth = texture(t0, v_UV).r;
    if (depth >= 1.0) { o_Color = vec4(1.0); return; }
    vec3 P = ViewPos(v_UV);
    vec3 r = ViewPos(v_UV + vec2(texel.x, 0.0)) - P, l = P - ViewPos(v_UV - vec2(texel.x, 0.0));
    vec3 t = ViewPos(v_UV + vec2(0.0, texel.y)) - P, b = P - ViewPos(v_UV - vec2(0.0, texel.y));
    vec3 dx = abs(r.z) < abs(l.z) ? r : l;
    vec3 dy = abs(t.z) < abs(b.z) ? t : b;
    vec3 N = normalize(cross(dx, dy));
    if (dot(N, P) > 0.0) N = -N;
    float angle = Hash(gl_FragCoord.xy) * 6.2831853;
    vec3 rnd = vec3(cos(angle), sin(angle), 0.0);
    vec3 T = normalize(rnd - N * dot(rnd, N));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);
    const int SAMPLES = 12;
    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        float fi = float(i);
        float h1 = fract(fi * 0.618034 + 0.31), h2 = fract(fi * 0.381966 + 0.73);
        float phi = h1 * 6.2831853, cosT = sqrt(1.0 - h2), sinT = sqrt(h2);
        vec3 k = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);
        float scale = (fi + 1.0) / float(SAMPLES);
        k *= mix(0.1, 1.0, scale * scale);
        vec3 S = P + TBN * k * radius;
        vec4 clip = u_Projection * vec4(S, 1.0);
        vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sceneZ = ViewPos(suv).z;
        float range = smoothstep(0.0, 1.0, radius / max(abs(P.z - sceneZ), 1e-4));
        occlusion += (sceneZ >= S.z + 0.02 * radius ? 1.0 : 0.0) * range;
    }
    o_Color = vec4(vec3(1.0 - occlusion / float(SAMPLES)), 1.0);
}
)";

inline const char* BlurFS = R"(
#version 330 core
layout(std140) uniform CB0 { vec4 u_Texel; };
in vec2 v_UV;
uniform sampler2D t0;
out vec4 o_Color;
void main() {
    float sum = 0.0;
    for (int x = -2; x < 2; ++x)
        for (int y = -2; y < 2; ++y)
            sum += texture(t0, v_UV + (vec2(x, y) + 0.5) * u_Texel.xy).r;
    o_Color = vec4(vec3(sum / 16.0), 1.0);
}
)";

inline const char* BloomDownFS = R"(
#version 330 core
layout(std140) uniform CB0 { vec4 u_Params; };   // src texel, first pass, threshold
in vec2 v_UV;
uniform sampler2D t0;
out vec4 o_Color;
vec3 S(vec2 o) { return texture(t0, v_UV + o * u_Params.xy).rgb; }
float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 Prefilter(vec3 c) {
    float threshold = u_Params.w;
    float br = max(c.r, max(c.g, c.b));
    float knee = threshold * 0.5;
    float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contrib = max(soft, br - threshold) / max(br, 1e-4);
    return c * contrib;
}
vec3 Karis(vec3 a, vec3 b, vec3 c, vec3 d) {
    float wa = 1.0 / (1.0 + Luma(a)), wb = 1.0 / (1.0 + Luma(b)), wc = 1.0 / (1.0 + Luma(c)), wd = 1.0 / (1.0 + Luma(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}
void main() {
    vec3 a = S(vec2(-2, 2)), b = S(vec2(0, 2)), c = S(vec2(2, 2));
    vec3 d = S(vec2(-2, 0)), e = S(vec2(0, 0)), f = S(vec2(2, 0));
    vec3 g = S(vec2(-2, -2)), h = S(vec2(0, -2)), i = S(vec2(2, -2));
    vec3 j = S(vec2(-1, 1)), k = S(vec2(1, 1)), l = S(vec2(-1, -1)), m = S(vec2(1, -1));
    vec3 color;
    if (u_Params.z > 0.5) {
        color = Karis(j, k, l, m) * 0.5 + Karis(a, b, d, e) * 0.125 + Karis(b, c, e, f) * 0.125
              + Karis(d, e, g, h) * 0.125 + Karis(e, f, h, i) * 0.125;
        color = Prefilter(min(color, vec3(64.0)));
    } else {
        color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    o_Color = vec4(max(color, vec3(0.0)), 1.0);
}
)";

inline const char* BloomUpFS = R"(
#version 330 core
layout(std140) uniform CB0 { vec4 u_Texel; };
in vec2 v_UV;
uniform sampler2D t0;
out vec4 o_Color;
void main() {
    vec2 t = u_Texel.xy;
    vec3 c = texture(t0, v_UV).rgb * 4.0;
    c += (texture(t0, v_UV + vec2(-t.x, 0)).rgb + texture(t0, v_UV + vec2(t.x, 0)).rgb
        + texture(t0, v_UV + vec2(0, -t.y)).rgb + texture(t0, v_UV + vec2(0, t.y)).rgb) * 2.0;
    c += texture(t0, v_UV + vec2(-t.x, -t.y)).rgb + texture(t0, v_UV + vec2(t.x, -t.y)).rgb
       + texture(t0, v_UV + vec2(-t.x, t.y)).rgb + texture(t0, v_UV + vec2(t.x, t.y)).rgb;
    o_Color = vec4(c / 16.0, 1.0);
}
)";

inline const char* CompositeFS = R"(
#version 330 core
layout(std140) uniform CB0 { mat4 u_InvViewProj; mat4 u_PrevViewProj; vec4 u_A; vec4 u_B; vec4 u_C; vec4 u_D; };
in vec2 v_UV;
uniform sampler2D t0;   // HDR
uniform sampler2D t1;   // depth
uniform sampler2D t2;   // AO
uniform sampler2D t3;   // bloom
out vec4 o_Color;

vec3 Scene(vec2 uv) {
    float ca = u_C.x;
    if (ca <= 0.0) return texture(t0, uv).rgb;
    vec2 dir = (uv - 0.5) * ca * 0.012;
    return vec3(texture(t0, uv + dir).r, texture(t0, uv).g, texture(t0, uv - dir).b);
}
vec3 ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 Uncharted(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
vec3 Neutral(vec3 c) {
    const float start = 0.8 - 0.04, desat = 0.15;
    float x = min(c.r, min(c.g, c.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    c -= offset;
    float peak = max(c.r, max(c.g, c.b));
    if (peak < start) return c;
    float d = 1.0 - start;
    float newPeak = 1.0 - d * d / (peak + d - start);
    c *= newPeak / peak;
    float g = 1.0 - 1.0 / (desat * (peak - newPeak) + 1.0);
    return mix(c, vec3(newPeak), g);
}
vec3 Tonemap(vec3 c, int mode) {
    if (mode == 0) return ACES(c);
    if (mode == 1) return clamp(Neutral(c), 0.0, 1.0);
    if (mode == 2) return c / (1.0 + c);
    if (mode == 3) return clamp(Uncharted(c * 2.0) / Uncharted(vec3(11.2)), 0.0, 1.0);
    return clamp(c, 0.0, 1.0);
}
float Hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    float exposure = u_A.x, contrast = u_A.z, saturation = u_A.w;
    int tonemapper = int(u_A.y + 0.5);
    float temperature = u_B.x, gamma = u_B.y, vignette = u_B.z, grain = u_B.w;
    float bloomIntensity = u_C.y, aoIntensity = u_C.z, motionBlur = u_C.w;

    vec3 hdr = Scene(v_UV);
    if (motionBlur > 0.0) {
        float d = texture(t1, v_UV).r;
        vec4 world = u_InvViewProj * vec4(v_UV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
        world /= world.w;
        vec4 prev = u_PrevViewProj * world;
        vec2 prevUV = prev.xy / prev.w * 0.5 + 0.5;
        vec2 velocity = (v_UV - prevUV) * motionBlur;
        float len = length(velocity);
        if (len > 0.04) velocity *= 0.04 / len;
        if (len > 0.0005) {
            const int TAPS = 8;
            for (int i = 1; i < TAPS; ++i) hdr += Scene(v_UV - velocity * (float(i) / float(TAPS - 1) - 0.5));
            hdr /= float(TAPS);
        }
    }
    float ao = texture(t2, v_UV).r;
    hdr *= mix(1.0, pow(ao, max(aoIntensity, 1.0)), clamp(aoIntensity, 0.0, 1.0));
    hdr += texture(t3, v_UV).rgb * bloomIntensity * 0.25;
    hdr *= vec3(1.0 + 0.12 * temperature, 1.0, 1.0 - 0.12 * temperature);
    vec3 ldr = Tonemap(max(hdr * exposure, vec3(0.0)), tonemapper);
    ldr = pow(ldr, vec3(1.0 / gamma));
    float luma = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
    ldr = mix(vec3(luma), ldr, saturation);
    ldr = (ldr - 0.5) * contrast + 0.5;
    vec2 q = v_UV - 0.5;
    ldr *= 1.0 - dot(q, q) * vignette * 1.2;
    if (grain > 0.0) ldr += (Hash(gl_FragCoord.xy + fract(u_D.x) * 113.0) - 0.5) * grain * 0.08;
    ldr = clamp(ldr, 0.0, 1.0);
    o_Color = vec4(ldr, dot(ldr, vec3(0.299, 0.587, 0.114)));
}
)";

inline const char* FXAAFS = R"(
#version 330 core
layout(std140) uniform CB0 { vec4 u_Texel; };
in vec2 v_UV;
uniform sampler2D t0;
out vec4 o_Color;
void main() {
    const float REDUCE_MIN = 1.0 / 128.0, REDUCE_MUL = 1.0 / 8.0, SPAN_MAX = 8.0;
    vec2 tx = u_Texel.xy;
    float nw = texture(t0, v_UV + vec2(-1.0, -1.0) * tx).a;
    float ne = texture(t0, v_UV + vec2( 1.0, -1.0) * tx).a;
    float sw = texture(t0, v_UV + vec2(-1.0,  1.0) * tx).a;
    float se = texture(t0, v_UV + vec2( 1.0,  1.0) * tx).a;
    vec4  center = texture(t0, v_UV);
    float m = center.a;
    float lumaMin = min(m, min(min(nw, ne), min(sw, se)));
    float lumaMax = max(m, max(max(nw, ne), max(sw, se)));
    if (lumaMax - lumaMin < max(0.0312, lumaMax * 0.125)) { o_Color = center; return; }
    vec2 dir = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduce = max((nw + ne + sw + se) * 0.25 * REDUCE_MUL, REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * tx;
    vec4 a = 0.5 * (texture(t0, v_UV + dir * (1.0 / 3.0 - 0.5)) + texture(t0, v_UV + dir * (2.0 / 3.0 - 0.5)));
    vec4 b = a * 0.5 + 0.25 * (texture(t0, v_UV - dir * 0.5) + texture(t0, v_UV + dir * 0.5));
    o_Color = (b.a < lumaMin || b.a > lumaMax) ? a : b;
}
)";

inline const char* UpscaleFS = R"(
#version 330 core
layout(std140) uniform CB0 { vec4 u_Params; };   // src size, mode
in vec2 v_UV;
uniform sampler2D t0;
out vec4 o_Color;
void main() {
    if (u_Params.z < 0.5) { o_Color = texture(t0, v_UV); return; }
    vec2 size = u_Params.xy;
    vec2 texel = 1.0 / size;
    vec2 pos = v_UV * size;
    vec2 tc = floor(pos - 0.5) + 0.5;
    vec2 f = pos - tc;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 off12 = w2 / w12;
    vec2 t0c = (tc - 1.0) * texel, t3c = (tc + 2.0) * texel, t12 = (tc + off12) * texel;
    vec4 c = vec4(0.0);
    c += texture(t0, vec2(t0c.x, t0c.y)) * w0.x * w0.y;
    c += texture(t0, vec2(t12.x, t0c.y)) * w12.x * w0.y;
    c += texture(t0, vec2(t3c.x, t0c.y)) * w3.x * w0.y;
    c += texture(t0, vec2(t0c.x, t12.y)) * w0.x * w12.y;
    c += texture(t0, vec2(t12.x, t12.y)) * w12.x * w12.y;
    c += texture(t0, vec2(t3c.x, t12.y)) * w3.x * w12.y;
    c += texture(t0, vec2(t0c.x, t3c.y)) * w0.x * w3.y;
    c += texture(t0, vec2(t12.x, t3c.y)) * w12.x * w3.y;
    c += texture(t0, vec2(t3c.x, t3c.y)) * w3.x * w3.y;
    vec4 a = texture(t0, tc * texel), b = texture(t0, (tc + vec2(1, 0)) * texel);
    vec4 d = texture(t0, (tc + vec2(0, 1)) * texel), e = texture(t0, (tc + vec2(1, 1)) * texel);
    o_Color = clamp(c, min(min(a, b), min(d, e)), max(max(a, b), max(d, e)));
}
)";

inline const char* SharpenFS = R"(
#version 330 core
layout(std140) uniform CB0 { vec4 u_Params; };   // texel, sharpness
in vec2 v_UV;
uniform sampler2D t0;
out vec4 o_Color;
void main() {
    vec2 tx = u_Params.xy;
    vec3 c = texture(t0, v_UV).rgb;
    vec3 n = texture(t0, v_UV + vec2(0.0, -tx.y)).rgb;
    vec3 s = texture(t0, v_UV + vec2(0.0,  tx.y)).rgb;
    vec3 e = texture(t0, v_UV + vec2( tx.x, 0.0)).rgb;
    vec3 w = texture(t0, v_UV + vec2(-tx.x, 0.0)).rgb;
    vec3 mn = min(c, min(min(n, s), min(e, w)));
    vec3 mx = max(c, max(max(n, s), max(e, w)));
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));
    vec3 wgt = -amp * mix(0.125, 0.2, u_Params.z);
    vec3 result = (c + (n + s + e + w) * wgt) / (1.0 + 4.0 * wgt);
    o_Color = vec4(clamp(result, 0.0, 1.0), 1.0);
}
)";

// ---------------------------------------------------------------------------
// UI (SDF text + rounded rects)
// ---------------------------------------------------------------------------
inline const char* UIVS = R"(
#version 330 core
layout(std140) uniform CB0 { vec4 u_Screen; };
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec4 a_Rect;
layout(location = 4) in vec4 a_Params;
out vec2 v_UV;
out vec4 v_Color;
out vec4 v_Rect;
out vec4 v_Params;
void main() {
    v_UV = a_UV;
    v_Color = a_Color;
    v_Rect = a_Rect;
    v_Params = a_Params;
    gl_Position = vec4(a_Pos.x / u_Screen.x * 2.0 - 1.0, 1.0 - a_Pos.y / u_Screen.y * 2.0, 0.0, 1.0);
}
)";
inline const char* UIFS = R"(
#version 330 core
in vec2 v_UV;
in vec4 v_Color;
in vec4 v_Rect;
in vec4 v_Params;
uniform sampler2D t0;   // font atlas (R8)
out vec4 o_Color;
float RoundedBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    float mode = v_Params.y;
    float alpha;
    if (mode < 0.5) {
        float d = texture(t0, v_UV).r;
        float w = max(fwidth(d) * 0.75, 0.0001);
        alpha = smoothstep(0.5 - w, 0.5 + w, d);
    } else {
        vec2 halfSize = v_Rect.zw * 0.5;
        float r = min(v_Params.x, min(halfSize.x, halfSize.y));
        float dist = RoundedBox(v_Rect.xy - halfSize, halfSize, r);
        if (mode < 1.5) alpha = clamp(0.5 - dist, 0.0, 1.0);
        else if (mode < 2.5) alpha = clamp(0.5 - dist, 0.0, 1.0) * clamp(dist + v_Params.z + 0.5, 0.0, 1.0);
        else { float soft = max(v_Params.z, 0.001); alpha = 1.0 - smoothstep(-soft, soft, dist); }
    }
    o_Color = vec4(v_Color.rgb, v_Color.a * alpha);
}
)";

} // namespace GLSL
