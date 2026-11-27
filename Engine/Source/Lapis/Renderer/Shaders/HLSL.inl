// HLSL (Shader Model 5.1) versions of the built-in shaders, included by
// ShaderLibrary.cpp. Compiled at runtime by the D3D12 backend with entry
// points VSMain / PSMain. Must stay in sync with GLSL.inl.

namespace HLSL {

#define LAPIS_HLSL_PRELUDE R"(
SamplerState           sLinearClamp : register(s0);
SamplerState           sLinearWrap  : register(s1);
SamplerState           sPointClamp  : register(s2);
SamplerState           sAniso       : register(s3);
SamplerComparisonState sShadow      : register(s4);

// OpenGL clip space (z -1..1) -> D3D (z 0..1). Projection matrices are shared.
float4 ClipFix(float4 p) { p.z = (p.z + p.w) * 0.5; return p; }
// Render targets: logical uv has (0,0) at the bottom (like OpenGL); D3D rows start at the top.
float2 RTUV(float2 uv) { return float2(uv.x, 1.0 - uv.y); }

struct FSOut {
    float4 Pos : SV_Position;
    float2 UV  : TEXCOORD0;
    float2 NDC : TEXCOORD1;
};
FSOut Fullscreen(uint id) {
    FSOut o;
    float2 p = float2((float)((id << 1) & 2), (float)(id & 2));
    o.UV = p;
    o.NDC = p * 2.0 - 1.0;
    o.Pos = float4(o.NDC, 1.0, 1.0);   // far plane (z = w stays 1 after ClipFix)
    return o;
}
)"

#define LAPIS_HLSL_FULLSCREEN_VS R"(
FSOut VSMain(uint id : SV_VertexID) { return Fullscreen(id); }
)"

// ---------------------------------------------------------------------------
// Lit
// ---------------------------------------------------------------------------
inline const char* Lit = LAPIS_HLSL_PRELUDE R"(
struct PointLight { float4 PositionRange; float4 Color; };
cbuffer CB0 : register(b0) {
    float4x4 u_ViewProjection;
    float4x4 u_LightSpace;
    float4 u_CameraPos;
    float4 u_SunDirection;
    float4 u_SunColor;
    float4 u_AmbientSky;
    float4 u_AmbientGround;
    float4 u_FogColorDensity;
    float4 u_SkyZenith;
    float4 u_SkyHorizon;
    float4 u_SkyGround;
    float4 u_Params;
    float4 u_LightCount;
    PointLight u_Lights[16];
};
cbuffer CB1 : register(b1) {
    float4 u_Albedo;
    float4 u_EmissiveMetallic;
    float4 u_MaterialParams;
};
Texture2D t0 : register(t0);   // albedo (sRGB)
Texture2D t1 : register(t1);   // shadow map (depth)

struct VSIn {
    float3 Position : ATTRIB0;
    float3 Normal   : ATTRIB1;
    float2 UV       : ATTRIB2;
    float4 M0       : ATTRIB3;   // model matrix columns (per instance)
    float4 M1       : ATTRIB4;
    float4 M2       : ATTRIB5;
    float4 M3       : ATTRIB6;
    float4 Tint     : ATTRIB7;
};
struct VSOut {
    float4 Pos        : SV_Position;
    float3 WorldPos   : TEXCOORD0;
    float3 Normal     : TEXCOORD1;
    float2 UV         : TEXCOORD2;
    float4 LightSpace : TEXCOORD3;
    float4 Tint       : TEXCOORD4;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    float4 world = i.M0 * i.Position.x + i.M1 * i.Position.y + i.M2 * i.Position.z + i.M3;
    // Normal matrix = inverse-transpose of the upper 3x3 (cofactors, sign of det kept).
    float3 a = i.M0.xyz, b = i.M1.xyz, c = i.M2.xyz;
    float det = dot(a, cross(b, c));
    float3 n = cross(b, c) * i.Normal.x + cross(c, a) * i.Normal.y + cross(a, b) * i.Normal.z;
    o.Normal = det < 0.0 ? -n : n;
    o.WorldPos = world.xyz;
    o.UV = i.UV * u_MaterialParams.zw;
    o.LightSpace = mul(u_LightSpace, world);
    o.Tint = i.Tint;
    o.Pos = ClipFix(mul(u_ViewProjection, world));
    return o;
}

static const float PI = 3.14159265;

float3 SkyColor(float3 d) {
    float h = d.y;
    return h > 0.0 ? lerp(u_SkyHorizon.rgb, u_SkyZenith.rgb, pow(h, 0.5))
                   : lerp(u_SkyHorizon.rgb, u_SkyGround.rgb, pow(min(-h * 4.0, 1.0), 0.6));
}

float SunShadow(float4 lightSpace, float3 N, float3 L) {
    if (u_Params.z < 0.5) return 1.0;
    float3 p = lightSpace.xyz / lightSpace.w * 0.5 + 0.5;
    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 1.0;
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0005);
    int r = (int)(u_Params.w + 0.5);
    float lit = 0.0;
    [loop] for (int x = -r; x <= r; ++x)
        [loop] for (int y = -r; y <= r; ++y)
            lit += t1.SampleCmpLevelZero(sShadow, RTUV(p.xy + float2(x, y) * u_Params.y), p.z - bias);
    float n = (float)(2 * r + 1);
    return lit / (n * n);
}

float3 Shade(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float roughness, float metallic) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return float3(0.0, 0.0, 0.0);
    float3 H = normalize(L + V);
    float a = max(roughness * roughness, 0.002);
    float shininess = clamp(2.0 / (a * a) - 2.0, 1.0, 4096.0);
    float spec = pow(max(dot(N, H), 0.0), shininess) * (shininess + 8.0) / (8.0 * PI);
    float3 F0 = lerp((float3)0.04, albedo, metallic);
    float3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    float3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + F * spec) * radiance * NdotL;
}

float4 PSMain(VSOut i, bool front : SV_IsFrontFace) : SV_Target {
    float roughness = u_MaterialParams.x;
    float metallic = u_EmissiveMetallic.w;
    float3 emissive = u_EmissiveMetallic.rgb;
    float4 tex = t0.Sample(sAniso, i.UV);
    float3 albedo = pow(abs(u_Albedo.rgb * i.Tint.rgb), (float3)2.2) * tex.rgb;
    float alpha = u_Albedo.a * i.Tint.a * tex.a;
    if (u_MaterialParams.y > 0.5) return float4(albedo + emissive, alpha);

    float3 N = normalize(i.Normal);
    if (!front) N = -N;
    float3 V = normalize(u_CameraPos.xyz - i.WorldPos);
    float3 L = normalize(-u_SunDirection.xyz);
    float3 color = Shade(N, V, L, u_SunColor.rgb, albedo, roughness, metallic) * SunShadow(i.LightSpace, N, L);

    int count = (int)(u_LightCount.x + 0.5);
    [loop] for (int k = 0; k < count; ++k) {
        float3 toLight = u_Lights[k].PositionRange.xyz - i.WorldPos;
        float d = length(toLight);
        float window = saturate(1.0 - pow(d / u_Lights[k].PositionRange.w, 4.0));
        float atten = window * window / (d * d + 1.0);
        color += Shade(N, V, toLight / max(d, 1e-4), u_Lights[k].Color.rgb * atten, albedo, roughness, metallic);
    }

    float3 ambient = lerp(u_AmbientGround.rgb, u_AmbientSky.rgb, N.y * 0.5 + 0.5);
    color += ambient * albedo * (1.0 - metallic);

    float3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);
    float3 F0 = lerp((float3)0.04, albedo, metallic);
    float3 Fenv = F0 + (max((float3)(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
    float3 env = lerp(SkyColor(R), (u_SkyHorizon.rgb + u_SkyZenith.rgb) * 0.5, roughness);
    color += env * Fenv * u_Params.x;
    color += emissive;

    float dist = length(u_CameraPos.xyz - i.WorldPos);
    float fog = 1.0 - exp(-pow(dist * u_FogColorDensity.w, 2.0));
    color = lerp(color, u_FogColorDensity.rgb, saturate(fog));
    return float4(color, alpha);
}
)";

// ---------------------------------------------------------------------------
// Shadow depth, debug lines, sky
// ---------------------------------------------------------------------------
inline const char* Shadow = LAPIS_HLSL_PRELUDE R"(
cbuffer CB0 : register(b0) { float4x4 u_LightSpace; };
struct VSIn {
    float3 Position : ATTRIB0;
    float4 M0 : ATTRIB3; float4 M1 : ATTRIB4; float4 M2 : ATTRIB5; float4 M3 : ATTRIB6;
};
float4 VSMain(VSIn i) : SV_Position {
    float4 world = i.M0 * i.Position.x + i.M1 * i.Position.y + i.M2 * i.Position.z + i.M3;
    return ClipFix(mul(u_LightSpace, world));
}
void PSMain() {}
)";

inline const char* Line = LAPIS_HLSL_PRELUDE R"(
cbuffer CB0 : register(b0) { float4x4 u_ViewProjection; };
struct VSIn { float3 Position : ATTRIB0; float4 Color : ATTRIB1; };
struct VSOut { float4 Pos : SV_Position; float4 Color : COLOR0; };
VSOut VSMain(VSIn i) {
    VSOut o;
    o.Color = i.Color;
    o.Pos = ClipFix(mul(u_ViewProjection, float4(i.Position, 1.0)));
    return o;
}
float4 PSMain(VSOut i) : SV_Target { return float4(pow(abs(i.Color.rgb), (float3)2.2) * 2.0, i.Color.a); }
)";

inline const char* Sky = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) {
    float4x4 u_InverseViewProjection;
    float4 u_CameraPos;
    float4 u_SunDirection;
    float4 u_SunColor;
    float4 u_SkyZenith;
    float4 u_SkyHorizon;
    float4 u_SkyGround;
};
float4 PSMain(FSOut i) : SV_Target {
    float4 world = mul(u_InverseViewProjection, float4(i.NDC, 1.0, 1.0));
    float3 dir = normalize(world.xyz / world.w - u_CameraPos.xyz);
    float h = dir.y;
    float3 sky = h > 0.0 ? lerp(u_SkyHorizon.rgb, u_SkyZenith.rgb, pow(h, 0.5))
                         : lerp(u_SkyHorizon.rgb, u_SkyGround.rgb, pow(min(-h * 4.0, 1.0), 0.6));
    float s = max(dot(dir, -normalize(u_SunDirection.xyz)), 0.0);
    sky += u_SunColor.rgb * (pow(s, 1200.0) * 4.0 + pow(s, 12.0) * 0.06);
    return float4(sky, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Post processing (all inputs are render targets -> sampled through RTUV)
// ---------------------------------------------------------------------------
inline const char* SSAO = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) { float4x4 u_Projection; float4x4 u_InvProjection; float4 u_Params; };
Texture2D t0 : register(t0);   // depth
float3 ViewPos(float2 uv) {
    float d = t0.SampleLevel(sPointClamp, RTUV(uv), 0).r;
    float4 p = mul(u_InvProjection, float4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0));
    return p.xyz / p.w;
}
float Hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }
float4 PSMain(FSOut i) : SV_Target {
    float2 texel = u_Params.xy;
    float radius = u_Params.z;
    float depth = t0.SampleLevel(sPointClamp, RTUV(i.UV), 0).r;
    if (depth >= 1.0) return (float4)1.0;
    float3 P = ViewPos(i.UV);
    float3 r = ViewPos(i.UV + float2(texel.x, 0.0)) - P, l = P - ViewPos(i.UV - float2(texel.x, 0.0));
    float3 t = ViewPos(i.UV + float2(0.0, texel.y)) - P, b = P - ViewPos(i.UV - float2(0.0, texel.y));
    float3 dx = abs(r.z) < abs(l.z) ? r : l;
    float3 dy = abs(t.z) < abs(b.z) ? t : b;
    float3 N = normalize(cross(dx, dy));
    if (dot(N, P) > 0.0) N = -N;
    float angle = Hash(i.Pos.xy) * 6.2831853;
    float3 rnd = float3(cos(angle), sin(angle), 0.0);
    float3 T = normalize(rnd - N * dot(rnd, N));
    float3 B = cross(N, T);
    const int SAMPLES = 12;
    float occlusion = 0.0;
    [loop] for (int s = 0; s < SAMPLES; ++s) {
        float fi = (float)s;
        float h1 = frac(fi * 0.618034 + 0.31), h2 = frac(fi * 0.381966 + 0.73);
        float phi = h1 * 6.2831853, cosT = sqrt(1.0 - h2), sinT = sqrt(h2);
        float3 k = float3(cos(phi) * sinT, sin(phi) * sinT, cosT);
        float scale = (fi + 1.0) / (float)SAMPLES;
        k *= lerp(0.1, 1.0, scale * scale);
        float3 S = P + (T * k.x + B * k.y + N * k.z) * radius;
        float4 clip = mul(u_Projection, float4(S, 1.0));
        float2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sceneZ = ViewPos(suv).z;
        float range = smoothstep(0.0, 1.0, radius / max(abs(P.z - sceneZ), 1e-4));
        occlusion += (sceneZ >= S.z + 0.02 * radius ? 1.0 : 0.0) * range;
    }
    return float4((float3)(1.0 - occlusion / (float)SAMPLES), 1.0);
}
)";

inline const char* Blur = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) { float4 u_Texel; };
Texture2D t0 : register(t0);
float4 PSMain(FSOut i) : SV_Target {
    float sum = 0.0;
    [unroll] for (int x = -2; x < 2; ++x)
        [unroll] for (int y = -2; y < 2; ++y)
            sum += t0.SampleLevel(sLinearClamp, RTUV(i.UV + (float2(x, y) + 0.5) * u_Texel.xy), 0).r;
    return float4((float3)(sum / 16.0), 1.0);
}
)";

inline const char* BloomDown = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) { float4 u_Params; };
Texture2D t0 : register(t0);
static float2 g_UV;
float3 S(float2 o) { return t0.SampleLevel(sLinearClamp, RTUV(g_UV + o * u_Params.xy), 0).rgb; }
float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 Prefilter(float3 c) {
    float threshold = u_Params.w;
    float br = max(c.r, max(c.g, c.b));
    float knee = threshold * 0.5;
    float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contrib = max(soft, br - threshold) / max(br, 1e-4);
    return c * contrib;
}
float3 Karis(float3 a, float3 b, float3 c, float3 d) {
    float wa = 1.0 / (1.0 + Luma(a)), wb = 1.0 / (1.0 + Luma(b)), wc = 1.0 / (1.0 + Luma(c)), wd = 1.0 / (1.0 + Luma(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}
float4 PSMain(FSOut i) : SV_Target {
    g_UV = i.UV;
    float3 a = S(float2(-2, 2)), b = S(float2(0, 2)), c = S(float2(2, 2));
    float3 d = S(float2(-2, 0)), e = S(float2(0, 0)), f = S(float2(2, 0));
    float3 g = S(float2(-2, -2)), h = S(float2(0, -2)), k = S(float2(2, -2));
    float3 j = S(float2(-1, 1)), m = S(float2(1, 1)), l = S(float2(-1, -1)), n = S(float2(1, -1));
    float3 color;
    if (u_Params.z > 0.5) {
        color = Karis(j, m, l, n) * 0.5 + Karis(a, b, d, e) * 0.125 + Karis(b, c, e, f) * 0.125
              + Karis(d, e, g, h) * 0.125 + Karis(e, f, h, k) * 0.125;
        color = Prefilter(min(color, (float3)64.0));
    } else {
        color = e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (j + m + l + n) * 0.125;
    }
    return float4(max(color, (float3)0.0), 1.0);
}
)";

inline const char* BloomUp = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) { float4 u_Texel; };
Texture2D t0 : register(t0);
float3 S(float2 uv) { return t0.SampleLevel(sLinearClamp, RTUV(uv), 0).rgb; }
float4 PSMain(FSOut i) : SV_Target {
    float2 t = u_Texel.xy;
    float3 c = S(i.UV) * 4.0;
    c += (S(i.UV + float2(-t.x, 0)) + S(i.UV + float2(t.x, 0)) + S(i.UV + float2(0, -t.y)) + S(i.UV + float2(0, t.y))) * 2.0;
    c += S(i.UV + float2(-t.x, -t.y)) + S(i.UV + float2(t.x, -t.y)) + S(i.UV + float2(-t.x, t.y)) + S(i.UV + float2(t.x, t.y));
    return float4(c / 16.0, 1.0);
}
)";

inline const char* Composite = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) { float4x4 u_InvViewProj; float4x4 u_PrevViewProj; float4 u_A; float4 u_B; float4 u_C; float4 u_D; };
Texture2D t0 : register(t0);   // HDR
Texture2D t1 : register(t1);   // depth
Texture2D t2 : register(t2);   // AO
Texture2D t3 : register(t3);   // bloom
float3 Tex0(float2 uv) { return t0.SampleLevel(sLinearClamp, RTUV(uv), 0).rgb; }
float3 Scene(float2 uv) {
    float ca = u_C.x;
    if (ca <= 0.0) return Tex0(uv);
    float2 dir = (uv - 0.5) * ca * 0.012;
    return float3(Tex0(uv + dir).r, Tex0(uv).g, Tex0(uv - dir).b);
}
float3 ACES(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
float3 Uncharted(float3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
float3 Neutral(float3 c) {
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
    return lerp(c, (float3)newPeak, g);
}
float3 Tonemap(float3 c, int mode) {
    if (mode == 0) return ACES(c);
    if (mode == 1) return saturate(Neutral(c));
    if (mode == 2) return c / (1.0 + c);
    if (mode == 3) return saturate(Uncharted(c * 2.0) / Uncharted((float3)11.2));
    return saturate(c);
}
float Hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }

float4 PSMain(FSOut i) : SV_Target {
    float exposure = u_A.x, contrast = u_A.z, saturation = u_A.w;
    int tonemapper = (int)(u_A.y + 0.5);
    float temperature = u_B.x, gamma = u_B.y, vignette = u_B.z, grain = u_B.w;
    float bloomIntensity = u_C.y, aoIntensity = u_C.z, motionBlur = u_C.w;

    float3 hdr = Scene(i.UV);
    if (motionBlur > 0.0) {
        float d = t1.SampleLevel(sPointClamp, RTUV(i.UV), 0).r;
        float4 world = mul(u_InvViewProj, float4(i.UV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0));
        world /= world.w;
        float4 prev = mul(u_PrevViewProj, world);
        float2 prevUV = prev.xy / prev.w * 0.5 + 0.5;
        float2 velocity = (i.UV - prevUV) * motionBlur;
        float len = length(velocity);
        if (len > 0.04) velocity *= 0.04 / len;
        if (len > 0.0005) {
            const int TAPS = 8;
            [unroll] for (int k = 1; k < TAPS; ++k) hdr += Scene(i.UV - velocity * ((float)k / (float)(TAPS - 1) - 0.5));
            hdr /= (float)TAPS;
        }
    }
    float ao = t2.SampleLevel(sLinearClamp, RTUV(i.UV), 0).r;
    hdr *= lerp(1.0, pow(abs(ao), max(aoIntensity, 1.0)), saturate(aoIntensity));
    hdr += t3.SampleLevel(sLinearClamp, RTUV(i.UV), 0).rgb * bloomIntensity * 0.25;
    hdr *= float3(1.0 + 0.12 * temperature, 1.0, 1.0 - 0.12 * temperature);
    float3 ldr = Tonemap(max(hdr * exposure, (float3)0.0), tonemapper);
    ldr = pow(abs(ldr), (float3)(1.0 / gamma));
    float luma = dot(ldr, float3(0.2126, 0.7152, 0.0722));
    ldr = lerp((float3)luma, ldr, saturation);
    ldr = (ldr - 0.5) * contrast + 0.5;
    float2 q = i.UV - 0.5;
    ldr *= 1.0 - dot(q, q) * vignette * 1.2;
    if (grain > 0.0) ldr += (Hash(i.Pos.xy + frac(u_D.x) * 113.0) - 0.5) * grain * 0.08;
    ldr = saturate(ldr);
    return float4(ldr, dot(ldr, float3(0.299, 0.587, 0.114)));
}
)";

inline const char* FXAA = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) { float4 u_Texel; };
Texture2D t0 : register(t0);
float4 T(float2 uv) { return t0.SampleLevel(sLinearClamp, RTUV(uv), 0); }
float4 PSMain(FSOut i) : SV_Target {
    const float REDUCE_MIN = 1.0 / 128.0, REDUCE_MUL = 1.0 / 8.0, SPAN_MAX = 8.0;
    float2 tx = u_Texel.xy;
    float nw = T(i.UV + float2(-1.0, -1.0) * tx).a;
    float ne = T(i.UV + float2( 1.0, -1.0) * tx).a;
    float sw = T(i.UV + float2(-1.0,  1.0) * tx).a;
    float se = T(i.UV + float2( 1.0,  1.0) * tx).a;
    float4 center = T(i.UV);
    float m = center.a;
    float lumaMin = min(m, min(min(nw, ne), min(sw, se)));
    float lumaMax = max(m, max(max(nw, ne), max(sw, se)));
    if (lumaMax - lumaMin < max(0.0312, lumaMax * 0.125)) return center;
    float2 dir = float2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduce = max((nw + ne + sw + se) * 0.25 * REDUCE_MUL, REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, (float2)(-SPAN_MAX), (float2)SPAN_MAX) * tx;
    float4 a = 0.5 * (T(i.UV + dir * (1.0 / 3.0 - 0.5)) + T(i.UV + dir * (2.0 / 3.0 - 0.5)));
    float4 b = a * 0.5 + 0.25 * (T(i.UV - dir * 0.5) + T(i.UV + dir * 0.5));
    return (b.a < lumaMin || b.a > lumaMax) ? a : b;
}
)";

inline const char* Upscale = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) { float4 u_Params; };
Texture2D t0 : register(t0);
float4 T(float2 uv) { return t0.SampleLevel(sLinearClamp, RTUV(uv), 0); }
float4 PSMain(FSOut i) : SV_Target {
    if (u_Params.z < 0.5) return T(i.UV);
    float2 size = u_Params.xy;
    float2 texel = 1.0 / size;
    float2 pos = i.UV * size;
    float2 tc = floor(pos - 0.5) + 0.5;
    float2 f = pos - tc;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 off12 = w2 / w12;
    float2 t0c = (tc - 1.0) * texel, t3c = (tc + 2.0) * texel, t12 = (tc + off12) * texel;
    float4 c = (float4)0.0;
    c += T(float2(t0c.x, t0c.y)) * w0.x * w0.y;
    c += T(float2(t12.x, t0c.y)) * w12.x * w0.y;
    c += T(float2(t3c.x, t0c.y)) * w3.x * w0.y;
    c += T(float2(t0c.x, t12.y)) * w0.x * w12.y;
    c += T(float2(t12.x, t12.y)) * w12.x * w12.y;
    c += T(float2(t3c.x, t12.y)) * w3.x * w12.y;
    c += T(float2(t0c.x, t3c.y)) * w0.x * w3.y;
    c += T(float2(t12.x, t3c.y)) * w12.x * w3.y;
    c += T(float2(t3c.x, t3c.y)) * w3.x * w3.y;
    float4 a = T(tc * texel), b = T((tc + float2(1, 0)) * texel);
    float4 d = T((tc + float2(0, 1)) * texel), e = T((tc + float2(1, 1)) * texel);
    return clamp(c, min(min(a, b), min(d, e)), max(max(a, b), max(d, e)));
}
)";

inline const char* Sharpen = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
cbuffer CB0 : register(b0) { float4 u_Params; };
Texture2D t0 : register(t0);
float3 T(float2 uv) { return t0.SampleLevel(sLinearClamp, RTUV(uv), 0).rgb; }
float4 PSMain(FSOut i) : SV_Target {
    float2 tx = u_Params.xy;
    float3 c = T(i.UV);
    float3 n = T(i.UV + float2(0.0, -tx.y));
    float3 s = T(i.UV + float2(0.0,  tx.y));
    float3 e = T(i.UV + float2( tx.x, 0.0));
    float3 w = T(i.UV + float2(-tx.x, 0.0));
    float3 mn = min(c, min(min(n, s), min(e, w)));
    float3 mx = max(c, max(max(n, s), max(e, w)));
    float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, (float3)1e-4)));
    float3 wgt = -amp * lerp(0.125, 0.2, u_Params.z);
    float3 result = (c + (n + s + e + w) * wgt) / (1.0 + 4.0 * wgt);
    return float4(saturate(result), 1.0);
}
)";

// ---------------------------------------------------------------------------
// UI: the font atlas is an image texture (not a render target): no flip.
// ---------------------------------------------------------------------------
inline const char* UI = LAPIS_HLSL_PRELUDE R"(
cbuffer CB0 : register(b0) { float4 u_Screen; };
Texture2D t0 : register(t0);
struct VSIn {
    float2 Pos    : ATTRIB0;
    float2 UV     : ATTRIB1;
    float4 Color  : ATTRIB2;
    float4 Rect   : ATTRIB3;
    float4 Params : ATTRIB4;
};
struct VSOut {
    float4 Pos    : SV_Position;
    float2 UV     : TEXCOORD0;
    float4 Color  : TEXCOORD1;
    float4 Rect   : TEXCOORD2;
    float4 Params : TEXCOORD3;
};
VSOut VSMain(VSIn i) {
    VSOut o;
    o.UV = i.UV; o.Color = i.Color; o.Rect = i.Rect; o.Params = i.Params;
    o.Pos = ClipFix(float4(i.Pos.x / u_Screen.x * 2.0 - 1.0, 1.0 - i.Pos.y / u_Screen.y * 2.0, 0.0, 1.0));
    return o;
}
float RoundedBox(float2 p, float2 halfSize, float r) {
    float2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float4 PSMain(VSOut i) : SV_Target {
    float mode = i.Params.y;
    float alpha;
    if (mode < 0.5) {
        float d = t0.Sample(sLinearClamp, i.UV).r;
        float w = max(fwidth(d) * 0.75, 0.0001);
        alpha = smoothstep(0.5 - w, 0.5 + w, d);
    } else {
        float2 halfSize = i.Rect.zw * 0.5;
        float r = min(i.Params.x, min(halfSize.x, halfSize.y));
        float dist = RoundedBox(i.Rect.xy - halfSize, halfSize, r);
        if (mode < 1.5) alpha = saturate(0.5 - dist);
        else if (mode < 2.5) alpha = saturate(0.5 - dist) * saturate(dist + i.Params.z + 0.5);
        else { float soft = max(i.Params.z, 0.001); alpha = 1.0 - smoothstep(-soft, soft, dist); }
    }
    return float4(i.Color.rgb, i.Color.a * alpha);
}
)";

// D3D12 backend only: copies sample 0 of a multisampled depth buffer into a
// single-sampled depth texture (D3D can't ResolveSubresource depth).
inline const char* DepthResolve = LAPIS_HLSL_PRELUDE LAPIS_HLSL_FULLSCREEN_VS R"(
Texture2DMS<float> t0 : register(t0);
float PSMain(FSOut i) : SV_Depth { return t0.Load(int2(i.Pos.xy), 0); }
)";

#undef LAPIS_HLSL_PRELUDE
#undef LAPIS_HLSL_FULLSCREEN_VS

} // namespace HLSL
