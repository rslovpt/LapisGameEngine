#pragma once

// Post-processing shaders (GLSL 330). All passes draw a fullscreen triangle
// with BuiltinShaders::FullscreenVertex.
//
//   HDR scene -> [SSAO + blur] -> [bloom down/up chain] -> Composite (motion
//   blur, chromatic aberration, AO, bloom, exposure, tonemapping, grading,
//   gamma, vignette, grain) -> [FXAA] -> [upscale] -> [sharpen] -> screen

namespace Lapis::PostShaders {

// Screen-space ambient occlusion from the depth buffer only (normals are
// reconstructed from neighbouring depths). Output: AO in .r (1 = unoccluded).
inline const char* SSAOFragment = R"(
#version 330 core
in vec2 v_UV;
uniform sampler2D u_Depth;
uniform mat4  u_Projection;
uniform mat4  u_InvProjection;
uniform vec2  u_DepthTexel;
uniform float u_Radius;
out vec4 o_Color;

vec3 ViewPos(vec2 uv) {
    float d = texture(u_Depth, uv).r;
    vec4 p = u_InvProjection * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
float Hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    float depth = texture(u_Depth, v_UV).r;
    if (depth >= 1.0) { o_Color = vec4(1.0); return; }
    vec3 P = ViewPos(v_UV);
    // Normal from the smaller of the forward/backward differences (avoids halos at edges).
    vec3 r = ViewPos(v_UV + vec2(u_DepthTexel.x, 0.0)) - P, l = P - ViewPos(v_UV - vec2(u_DepthTexel.x, 0.0));
    vec3 t = ViewPos(v_UV + vec2(0.0, u_DepthTexel.y)) - P, b = P - ViewPos(v_UV - vec2(0.0, u_DepthTexel.y));
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
        // Fixed hemisphere kernel, denser near the center.
        float fi = float(i);
        float h1 = fract(fi * 0.618034 + 0.31), h2 = fract(fi * 0.381966 + 0.73);
        float phi = h1 * 6.2831853, cosT = sqrt(1.0 - h2), sinT = sqrt(h2);
        vec3 k = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);
        float scale = (fi + 1.0) / float(SAMPLES);
        k *= mix(0.1, 1.0, scale * scale);
        vec3 S = P + TBN * k * u_Radius;
        vec4 clip = u_Projection * vec4(S, 1.0);
        vec2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sceneZ = ViewPos(suv).z;
        float range = smoothstep(0.0, 1.0, u_Radius / max(abs(P.z - sceneZ), 1e-4));
        occlusion += (sceneZ >= S.z + 0.02 * u_Radius ? 1.0 : 0.0) * range;
    }
    o_Color = vec4(vec3(1.0 - occlusion / float(SAMPLES)), 1.0);
}
)";

// 4x4 box blur that matches the 4x4 SSAO rotation pattern.
inline const char* BlurFragment = R"(
#version 330 core
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_Texel;
out vec4 o_Color;
void main() {
    float sum = 0.0;
    for (int x = -2; x < 2; ++x)
        for (int y = -2; y < 2; ++y)
            sum += texture(u_Source, v_UV + (vec2(x, y) + 0.5) * u_Texel).r;
    o_Color = vec4(vec3(sum / 16.0), 1.0);
}
)";

// Bloom downsample: 13-tap filter. The first pass applies a soft threshold
// and a Karis average (stops single bright pixels from flickering).
inline const char* BloomDownFragment = R"(
#version 330 core
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2  u_SrcTexel;
uniform int   u_FirstPass;
uniform float u_Threshold;
out vec4 o_Color;
vec3 S(vec2 o) { return texture(u_Source, v_UV + o * u_SrcTexel).rgb; }
float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 Prefilter(vec3 c) {
    float br = max(c.r, max(c.g, c.b));
    float knee = u_Threshold * 0.5;
    float soft = clamp(br - u_Threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contrib = max(soft, br - u_Threshold) / max(br, 1e-4);
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
    if (u_FirstPass == 1) {
        color = Karis(j, k, l, m) * 0.5 + Karis(a, b, d, e) * 0.125 + Karis(b, c, e, f) * 0.125
              + Karis(d, e, g, h) * 0.125 + Karis(e, f, h, i) * 0.125;
        color = Prefilter(min(color, vec3(64.0)));
    } else {
        color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    o_Color = vec4(max(color, vec3(0.0)), 1.0);
}
)";

// Bloom upsample: 3x3 tent, added onto the next larger mip (additive blend).
inline const char* BloomUpFragment = R"(
#version 330 core
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_SrcTexel;
out vec4 o_Color;
void main() {
    vec2 t = u_SrcTexel;
    vec3 c = texture(u_Source, v_UV).rgb * 4.0;
    c += (texture(u_Source, v_UV + vec2(-t.x, 0)).rgb + texture(u_Source, v_UV + vec2(t.x, 0)).rgb
        + texture(u_Source, v_UV + vec2(0, -t.y)).rgb + texture(u_Source, v_UV + vec2(0, t.y)).rgb) * 2.0;
    c += texture(u_Source, v_UV + vec2(-t.x, -t.y)).rgb + texture(u_Source, v_UV + vec2(t.x, -t.y)).rgb
       + texture(u_Source, v_UV + vec2(-t.x, t.y)).rgb + texture(u_Source, v_UV + vec2(t.x, t.y)).rgb;
    o_Color = vec4(c / 16.0, 1.0);
}
)";

// HDR -> display. Writes luma to alpha for FXAA.
inline const char* CompositeFragment = R"(
#version 330 core
in vec2 v_UV;
uniform sampler2D u_HDR;
uniform sampler2D u_Depth;
uniform sampler2D u_AO;
uniform sampler2D u_Bloom;
uniform float u_Exposure;
uniform int   u_Tonemapper;
uniform float u_Contrast;
uniform float u_Saturation;
uniform float u_Temperature;
uniform float u_Gamma;
uniform float u_Vignette;
uniform float u_Grain;
uniform float u_Chromatic;
uniform float u_BloomIntensity;
uniform float u_AOIntensity;
uniform float u_MotionBlur;      // 0 = off
uniform mat4  u_InvViewProj;
uniform mat4  u_PrevViewProj;
uniform float u_Time;
out vec4 o_Color;

vec3 Scene(vec2 uv) {
    if (u_Chromatic <= 0.0) return texture(u_HDR, uv).rgb;
    vec2 dir = (uv - 0.5) * u_Chromatic * 0.012;
    return vec3(texture(u_HDR, uv + dir).r, texture(u_HDR, uv).g, texture(u_HDR, uv - dir).b);
}

vec3 ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 Uncharted(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
// Khronos PBR Neutral: keeps base colors faithful, compresses only highlights.
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
vec3 Tonemap(vec3 c) {
    if (u_Tonemapper == 0) return ACES(c);
    if (u_Tonemapper == 1) return clamp(Neutral(c), 0.0, 1.0);
    if (u_Tonemapper == 2) return c / (1.0 + c);
    if (u_Tonemapper == 3) return clamp(Uncharted(c * 2.0) / Uncharted(vec3(11.2)), 0.0, 1.0);
    return clamp(c, 0.0, 1.0);
}
float Hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
    vec3 hdr = Scene(v_UV);

    // Camera motion blur: reproject this pixel into the previous frame.
    if (u_MotionBlur > 0.0) {
        float d = texture(u_Depth, v_UV).r;
        vec4 world = u_InvViewProj * vec4(v_UV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
        world /= world.w;
        vec4 prev = u_PrevViewProj * world;
        vec2 prevUV = prev.xy / prev.w * 0.5 + 0.5;
        vec2 velocity = (v_UV - prevUV) * u_MotionBlur;
        float len = length(velocity);
        if (len > 0.04) velocity *= 0.04 / len;   // clamp the blur length
        if (len > 0.0005) {
            const int TAPS = 8;
            for (int i = 1; i < TAPS; ++i) hdr += Scene(v_UV - velocity * (float(i) / float(TAPS - 1) - 0.5));
            hdr /= float(TAPS);
        }
    }

    float ao = texture(u_AO, v_UV).r;
    hdr *= mix(1.0, pow(ao, max(u_AOIntensity, 1.0)), clamp(u_AOIntensity, 0.0, 1.0));
    hdr += texture(u_Bloom, v_UV).rgb * u_BloomIntensity * 0.25;

    // White balance before tonemapping.
    hdr *= vec3(1.0 + 0.12 * u_Temperature, 1.0, 1.0 - 0.12 * u_Temperature);
    vec3 ldr = Tonemap(max(hdr * u_Exposure, vec3(0.0)));
    ldr = pow(ldr, vec3(1.0 / u_Gamma));

    float luma = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
    ldr = mix(vec3(luma), ldr, u_Saturation);
    ldr = (ldr - 0.5) * u_Contrast + 0.5;

    vec2 q = v_UV - 0.5;
    ldr *= 1.0 - dot(q, q) * u_Vignette * 1.2;
    if (u_Grain > 0.0) ldr += (Hash(gl_FragCoord.xy + fract(u_Time) * 113.0) - 0.5) * u_Grain * 0.08;

    ldr = clamp(ldr, 0.0, 1.0);
    o_Color = vec4(ldr, dot(ldr, vec3(0.299, 0.587, 0.114)));
}
)";

// FXAA (classic single-pass variant): blends along the detected edge.
inline const char* FXAAFragment = R"(
#version 330 core
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_Texel;
out vec4 o_Color;
void main() {
    const float REDUCE_MIN = 1.0 / 128.0, REDUCE_MUL = 1.0 / 8.0, SPAN_MAX = 8.0;
    float nw = texture(u_Source, v_UV + vec2(-1.0, -1.0) * u_Texel).a;
    float ne = texture(u_Source, v_UV + vec2( 1.0, -1.0) * u_Texel).a;
    float sw = texture(u_Source, v_UV + vec2(-1.0,  1.0) * u_Texel).a;
    float se = texture(u_Source, v_UV + vec2( 1.0,  1.0) * u_Texel).a;
    vec4  center = texture(u_Source, v_UV);
    float m = center.a;
    float lumaMin = min(m, min(min(nw, ne), min(sw, se)));
    float lumaMax = max(m, max(max(nw, ne), max(sw, se)));
    if (lumaMax - lumaMin < max(0.0312, lumaMax * 0.125)) { o_Color = center; return; }
    vec2 dir = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduce = max((nw + ne + sw + se) * 0.25 * REDUCE_MUL, REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * u_Texel;
    vec4 a = 0.5 * (texture(u_Source, v_UV + dir * (1.0 / 3.0 - 0.5)) + texture(u_Source, v_UV + dir * (2.0 / 3.0 - 0.5)));
    vec4 b = a * 0.5 + 0.25 * (texture(u_Source, v_UV - dir * 0.5) + texture(u_Source, v_UV + dir * 0.5));
    o_Color = (b.a < lumaMin || b.a > lumaMax) ? a : b;
}
)";

// Upscaling. Mode 0: bilinear. Mode 1: Lapis Spatial - Catmull-Rom bicubic
// (9 bilinear taps) with anti-ringing clamp to the nearest 2x2 texels.
inline const char* UpscaleFragment = R"(
#version 330 core
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_SrcSize;
uniform int  u_Mode;
out vec4 o_Color;
void main() {
    if (u_Mode == 0) { o_Color = texture(u_Source, v_UV); return; }
    vec2 texel = 1.0 / u_SrcSize;
    vec2 pos = v_UV * u_SrcSize;
    vec2 tc = floor(pos - 0.5) + 0.5;
    vec2 f = pos - tc;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 off12 = w2 / w12;
    vec2 t0 = (tc - 1.0) * texel, t3 = (tc + 2.0) * texel, t12 = (tc + off12) * texel;
    vec4 c = vec4(0.0);
    c += texture(u_Source, vec2(t0.x, t0.y)) * w0.x * w0.y;
    c += texture(u_Source, vec2(t12.x, t0.y)) * w12.x * w0.y;
    c += texture(u_Source, vec2(t3.x, t0.y)) * w3.x * w0.y;
    c += texture(u_Source, vec2(t0.x, t12.y)) * w0.x * w12.y;
    c += texture(u_Source, vec2(t12.x, t12.y)) * w12.x * w12.y;
    c += texture(u_Source, vec2(t3.x, t12.y)) * w3.x * w12.y;
    c += texture(u_Source, vec2(t0.x, t3.y)) * w0.x * w3.y;
    c += texture(u_Source, vec2(t12.x, t3.y)) * w12.x * w3.y;
    c += texture(u_Source, vec2(t3.x, t3.y)) * w3.x * w3.y;
    // Anti-ringing: clamp to the 2x2 neighbourhood.
    vec4 a = texture(u_Source, tc * texel), b = texture(u_Source, (tc + vec2(1, 0)) * texel);
    vec4 d = texture(u_Source, (tc + vec2(0, 1)) * texel), e = texture(u_Source, (tc + vec2(1, 1)) * texel);
    o_Color = clamp(c, min(min(a, b), min(d, e)), max(max(a, b), max(d, e)));
}
)";

// Contrast-adaptive sharpening: sharpens less where local contrast is high.
inline const char* SharpenFragment = R"(
#version 330 core
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2  u_Texel;
uniform float u_Sharpness;
out vec4 o_Color;
void main() {
    vec3 c = texture(u_Source, v_UV).rgb;
    vec3 n = texture(u_Source, v_UV + vec2(0.0, -u_Texel.y)).rgb;
    vec3 s = texture(u_Source, v_UV + vec2(0.0,  u_Texel.y)).rgb;
    vec3 e = texture(u_Source, v_UV + vec2( u_Texel.x, 0.0)).rgb;
    vec3 w = texture(u_Source, v_UV + vec2(-u_Texel.x, 0.0)).rgb;
    vec3 mn = min(c, min(min(n, s), min(e, w)));
    vec3 mx = max(c, max(max(n, s), max(e, w)));
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));
    vec3 wgt = -amp * mix(0.125, 0.2, u_Sharpness);
    vec3 result = (c + (n + s + e + w) * wgt) / (1.0 + 4.0 * wgt);
    o_Color = vec4(clamp(result, 0.0, 1.0), 1.0);
}
)";

} // namespace Lapis::PostShaders
