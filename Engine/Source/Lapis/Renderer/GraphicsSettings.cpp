#include "Lapis/Renderer/GraphicsSettings.h"

#include "Lapis/Core/Log.h"
#include "Lapis/Math/Math.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <ostream>
#include <sstream>

namespace Lapis {

// ---------------------------------------------------------------------------
// GPU detection
// ---------------------------------------------------------------------------
namespace {
std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool Contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }
} // namespace

const char* GPUInfo::VendorString(GPUVendor v) {
    switch (v) {
        case GPUVendor::NVIDIA: return "NVIDIA";
        case GPUVendor::AMD: return "AMD";
        case GPUVendor::Intel: return "Intel";
        case GPUVendor::Apple: return "Apple";
        case GPUVendor::Software: return "Software";
        default: return "Unknown";
    }
}

GPUInfo GPUInfo::FromDevice(const RHI::DeviceInfo& d) {
    GPUInfo info;
    info.VendorName = d.Vendor;
    info.Name = d.GPUName;
    info.APIVersion = d.Version;
    info.MaxAnisotropy = d.MaxAnisotropy;
    const std::string v = Lower(info.VendorName + " " + info.Name);
    if (Contains(v, "llvmpipe") || Contains(v, "softpipe") || Contains(v, "swiftshader") || Contains(v, "microsoft basic"))
        info.Vendor = GPUVendor::Software;
    else if (Contains(v, "nvidia") || Contains(v, "geforce") || Contains(v, "quadro")) info.Vendor = GPUVendor::NVIDIA;
    else if (Contains(v, "amd") || Contains(v, "ati ") || Contains(v, "radeon")) info.Vendor = GPUVendor::AMD;
    else if (Contains(v, "intel")) info.Vendor = GPUVendor::Intel;
    else if (Contains(v, "apple")) info.Vendor = GPUVendor::Apple;
    // RTX 20xx and newer, plus RTX professional cards ("RTX A4000", "RTX 6000 Ada").
    info.NVIDIA_RTX = info.Vendor == GPUVendor::NVIDIA && Contains(Lower(info.Name), "rtx");
    return info;
}

// ---------------------------------------------------------------------------
// Derived values / presets
// ---------------------------------------------------------------------------
const int GraphicsSettings::FrameCapValues[7] = {0, 30, 60, 120, 144, 165, 240};

u32 GraphicsSettings::MSAASamples() const {
    switch (AntiAliasing) {
        case AAMSAA2: return 2;
        case AAMSAA4: case AAMSAA4FXAA: return 4;
        case AAMSAA8: return 8;
        default: return 1;
    }
}
bool GraphicsSettings::FXAA() const { return AntiAliasing == AAFXAA || AntiAliasing == AAMSAA4FXAA; }
u32 GraphicsSettings::ShadowMapSize() const {
    static const u32 sizes[] = {0, 1024, 2048, 4096, 8192};
    return sizes[Clamp(ShadowQuality, 0, 4)];
}
u32 GraphicsSettings::MaxFPS() const { return (u32)FrameCapValues[Clamp(FrameCap, 0, 6)]; }
float GraphicsSettings::AnisotropyLevel() const { return (float)(1 << Clamp(Anisotropy, 0, 4)); }

const char* GraphicsSettings::PresetName(Preset p) {
    switch (p) {
        case Preset::Low: return "Low";
        case Preset::Medium: return "Medium";
        case Preset::High: return "High";
        default: return "Ultra";
    }
}

GraphicsSettings GraphicsSettings::FromPreset(Preset p) {
    GraphicsSettings g;
    g.ApplyPreset(p);
    return g;
}

void GraphicsSettings::ApplyPreset(Preset p) {
    // Presets change quality/effects only; display + look (post) are kept.
    switch (p) {
        case Preset::Low:
            RenderScale = 0.75f; Upscaler = UpscaleSpatial; AntiAliasing = AAFXAA; ShadowQuality = 1; ShadowDistance = 30;
            ShadowSoftness = 0; Anisotropy = 1; MaxPointLights = 4; DrawDistance = 300;
            Bloom = false; AmbientOcclusion = false; Reflections = 0.5f; MotionBlur = false;
            break;
        case Preset::Medium:
            RenderScale = 1.0f; Upscaler = UpscaleSpatial; AntiAliasing = AAMSAA2; ShadowQuality = 2; ShadowDistance = 45;
            ShadowSoftness = 1; Anisotropy = 2; MaxPointLights = 8; DrawDistance = 600;
            Bloom = true; AmbientOcclusion = false; Reflections = 1.0f;
            break;
        case Preset::High:
            RenderScale = 1.0f; Upscaler = UpscaleSpatial; AntiAliasing = AAMSAA4; ShadowQuality = 3; ShadowDistance = 70;
            ShadowSoftness = 1; Anisotropy = 3; MaxPointLights = 16; DrawDistance = 1000;
            Bloom = true; AmbientOcclusion = true; Reflections = 1.0f;
            break;
        case Preset::Ultra:
            RenderScale = 1.0f; Upscaler = UpscaleSpatial; AntiAliasing = AAMSAA8; ShadowQuality = 4; ShadowDistance = 110;
            ShadowSoftness = 2; Anisotropy = 4; MaxPointLights = 16; DrawDistance = 2000;
            Bloom = true; AmbientOcclusion = true; Reflections = 1.0f;
            break;
    }
}

// ---------------------------------------------------------------------------
// Reflection table
// ---------------------------------------------------------------------------
#define LAPIS_GFX_FIELD(member) offsetof(GraphicsSettings, member)

const std::vector<GraphicsSettings::Field>& GraphicsSettings::Fields() {
    using T = FieldType;
    static const std::vector<Field> fields = {
        // Display
        {"VSync", "VSync", "Display", T::Bool, LAPIS_GFX_FIELD(VSync), 0, 1, 0, {}, "Sync to the monitor refresh rate (no tearing)."},
        {"FrameCap", "Frame rate cap", "Display", T::Enum, LAPIS_GFX_FIELD(FrameCap), 0, 6, 0,
         {"Unlimited", "30 FPS", "60 FPS", "120 FPS", "144 FPS", "165 FPS", "240 FPS"}, "Limits the frame rate to save power / heat."},
        {"Fullscreen", "Fullscreen", "Display", T::Bool, LAPIS_GFX_FIELD(Fullscreen), 0, 1, 0, {}, "Borderless fullscreen on the current monitor."},
        {"Gamma", "Gamma / brightness", "Display", T::Float, LAPIS_GFX_FIELD(Gamma), 1.6f, 2.8f, 0.05f, {}, "Display gamma. Lower = brighter."},
        // Quality
        {"RenderScale", "Render scale", "Quality", T::Float, LAPIS_GFX_FIELD(RenderScale), 0.5f, 2.0f, 0.05f, {},
         "Internal resolution. Below 100% renders fewer pixels and upscales; above 100% supersamples."},
        {"Upscaler", "Upscaler", "Quality", T::Enum, LAPIS_GFX_FIELD(Upscaler), 0, 4, 0,
         {"Bilinear", "Lapis Spatial (FSR 1-style)", "AMD FSR 3", "Intel XeSS", "NVIDIA DLSS"}, "How the image is scaled to the screen when render scale is not 100%."},
        {"Sharpness", "Sharpening", "Quality", T::Float, LAPIS_GFX_FIELD(Sharpness), 0.0f, 1.0f, 0.05f, {}, "Contrast-adaptive sharpening."},
        {"AntiAliasing", "Anti-aliasing", "Quality", T::Enum, LAPIS_GFX_FIELD(AntiAliasing), 0, 5, 0,
         {"Off", "FXAA", "MSAA 2x", "MSAA 4x", "MSAA 8x", "MSAA 4x + FXAA"}, "Smooths jagged edges."},
        {"ShadowQuality", "Shadow quality", "Quality", T::Enum, LAPIS_GFX_FIELD(ShadowQuality), 0, 4, 0,
         {"Off", "Low (1024)", "Medium (2048)", "High (4096)", "Ultra (8192)"}, "Sun shadow map resolution."},
        {"ShadowDistance", "Shadow distance", "Quality", T::Float, LAPIS_GFX_FIELD(ShadowDistance), 15.0f, 200.0f, 5.0f, {}, "How far from the camera shadows are drawn (m)."},
        {"ShadowSoftness", "Shadow softness", "Quality", T::Enum, LAPIS_GFX_FIELD(ShadowSoftness), 0, 2, 0, {"Hard", "Soft", "Very soft"}, "Shadow edge filtering."},
        {"Anisotropy", "Texture filtering", "Quality", T::Enum, LAPIS_GFX_FIELD(Anisotropy), 0, 4, 0,
         {"Trilinear", "Anisotropic 2x", "Anisotropic 4x", "Anisotropic 8x", "Anisotropic 16x"}, "Sharper textures at grazing angles."},
        {"MaxPointLights", "Max point lights", "Quality", T::Int, LAPIS_GFX_FIELD(MaxPointLights), 0, 16, 1, {}, "Nearest lights to the camera are kept."},
        {"DrawDistance", "Draw distance", "Quality", T::Float, LAPIS_GFX_FIELD(DrawDistance), 50.0f, 3000.0f, 50.0f, {}, "Objects further away are not drawn (m)."},
        // Effects
        {"Bloom", "Bloom", "Effects", T::Bool, LAPIS_GFX_FIELD(Bloom), 0, 1, 0, {}, "Glow around bright lights and emissive objects."},
        {"BloomIntensity", "Bloom intensity", "Effects", T::Float, LAPIS_GFX_FIELD(BloomIntensity), 0.0f, 2.0f, 0.05f},
        {"BloomThreshold", "Bloom threshold", "Effects", T::Float, LAPIS_GFX_FIELD(BloomThreshold), 0.2f, 4.0f, 0.1f},
        {"AmbientOcclusion", "Ambient occlusion (SSAO)", "Effects", T::Bool, LAPIS_GFX_FIELD(AmbientOcclusion), 0, 1, 0, {}, "Contact shadows in corners and creases."},
        {"AOIntensity", "AO intensity", "Effects", T::Float, LAPIS_GFX_FIELD(AOIntensity), 0.0f, 2.0f, 0.05f},
        {"AORadius", "AO radius", "Effects", T::Float, LAPIS_GFX_FIELD(AORadius), 0.1f, 2.0f, 0.05f},
        {"Reflections", "Reflections", "Effects", T::Float, LAPIS_GFX_FIELD(Reflections), 0.0f, 1.0f, 0.05f, {}, "Sky / environment reflection strength."},
        {"Fog", "Fog", "Effects", T::Bool, LAPIS_GFX_FIELD(Fog), 0, 1, 0},
        {"FogAmount", "Fog amount", "Effects", T::Float, LAPIS_GFX_FIELD(FogAmount), 0.0f, 3.0f, 0.05f},
        {"MotionBlur", "Motion blur", "Effects", T::Bool, LAPIS_GFX_FIELD(MotionBlur), 0, 1, 0, {}, "Camera motion blur."},
        {"MotionBlurStrength", "Motion blur strength", "Effects", T::Float, LAPIS_GFX_FIELD(MotionBlurStrength), 0.0f, 1.0f, 0.05f},
        // Post processing
        {"Tonemapper", "Tonemapper", "Post Processing", T::Enum, LAPIS_GFX_FIELD(Tonemapper), 0, 4, 0,
         {"ACES Filmic", "Neutral", "Reinhard", "Uncharted 2", "None (clamp)"}, "How HDR light is mapped to the screen."},
        {"Exposure", "Exposure", "Post Processing", T::Float, LAPIS_GFX_FIELD(Exposure), 0.25f, 4.0f, 0.05f},
        {"Contrast", "Contrast", "Post Processing", T::Float, LAPIS_GFX_FIELD(Contrast), 0.5f, 1.5f, 0.02f},
        {"Saturation", "Saturation", "Post Processing", T::Float, LAPIS_GFX_FIELD(Saturation), 0.0f, 2.0f, 0.05f},
        {"Temperature", "Color temperature", "Post Processing", T::Float, LAPIS_GFX_FIELD(Temperature), -1.0f, 1.0f, 0.05f, {}, "Negative = cooler / blue, positive = warmer / orange."},
        {"Vignette", "Vignette", "Post Processing", T::Float, LAPIS_GFX_FIELD(Vignette), 0.0f, 1.0f, 0.05f},
        {"FilmGrain", "Film grain", "Post Processing", T::Float, LAPIS_GFX_FIELD(FilmGrain), 0.0f, 1.0f, 0.05f},
        {"ChromaticAberration", "Chromatic aberration", "Post Processing", T::Float, LAPIS_GFX_FIELD(ChromaticAberration), 0.0f, 1.0f, 0.05f},
        // Frame generation
        {"FrameGeneration", "Frame generation", "Frame Generation", T::Enum, LAPIS_GFX_FIELD(FrameGeneration), 0, 3, 0,
         {"Off", "AMD FSR 3 Frame Generation", "Intel XeSS Frame Generation", "NVIDIA DLSS Frame Generation"},
         "Generates in-between frames for higher displayed FPS."},
    };
    return fields;
}

#undef LAPIS_GFX_FIELD

const GraphicsSettings::Field* GraphicsSettings::FindField(const std::string& key) {
    for (const Field& f : Fields())
        if (key == f.Key) return &f;
    return nullptr;
}

std::vector<std::string> GraphicsSettings::Categories() {
    std::vector<std::string> out;
    for (const Field& f : Fields())
        if (std::find(out.begin(), out.end(), f.Category) == out.end()) out.push_back(f.Category);
    return out;
}

// Typed member access through the table's byte offsets. All fields are bool,
// int or float members of this standard-layout struct.
namespace {
template <typename T> T& Member(GraphicsSettings& g, size_t offset) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(&g) + offset); }
template <typename T> const T& Member(const GraphicsSettings& g, size_t offset) {
    return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(&g) + offset);
}
} // namespace

float GraphicsSettings::GetNumber(const Field& f) const {
    switch (f.Type) {
        case FieldType::Bool: return Member<bool>(*this, f.Offset) ? 1.0f : 0.0f;
        case FieldType::Float: return Member<float>(*this, f.Offset);
        default: return (float)Member<int>(*this, f.Offset);
    }
}

void GraphicsSettings::SetNumber(const Field& f, float value) {
    if (!std::isfinite(value)) return;
    switch (f.Type) {
        case FieldType::Bool: Member<bool>(*this, f.Offset) = value != 0.0f; break;
        case FieldType::Float: {
            float v = Clamp(value, f.Min, f.Max);
            if (f.Step > 0.0f) v = Clamp(f.Min + std::round((v - f.Min) / f.Step) * f.Step, f.Min, f.Max);
            Member<float>(*this, f.Offset) = v;
            break;
        }
        default: Member<int>(*this, f.Offset) = Clamp((int)std::lround(value), (int)f.Min, (int)f.Max); break;
    }
}

bool GraphicsSettings::Set(const std::string& key, const std::string& value) {
    const Field* f = FindField(key);
    if (!f) return false;
    const std::string v = Lower(value);
    if (f->Type == FieldType::Bool) {
        if (v == "true" || v == "1" || v == "on" || v == "yes") { SetNumber(*f, 1); return true; }
        if (v == "false" || v == "0" || v == "off" || v == "no") { SetNumber(*f, 0); return true; }
        return false;
    }
    if (f->Type == FieldType::Enum)
        for (size_t i = 0; i < f->Options.size(); ++i)
            if (v == Lower(f->Options[i])) { SetNumber(*f, (float)i); return true; }
    std::istringstream in(value);
    float number = 0.0f;
    if (!(in >> number)) return false;
    SetNumber(*f, number);
    return true;
}

std::string GraphicsSettings::Get(const std::string& key) const {
    const Field* f = FindField(key);
    if (!f) return {};
    std::ostringstream o;
    switch (f->Type) {
        case FieldType::Bool: return Member<bool>(*this, f->Offset) ? "true" : "false";
        case FieldType::Float: o << Member<float>(*this, f->Offset); return o.str();
        default: return std::to_string(Member<int>(*this, f->Offset));
    }
}

// ---------------------------------------------------------------------------
// Hardware / backend availability
// ---------------------------------------------------------------------------
std::string GraphicsSettings::OptionUnavailable(const Field& f, int option, const GPUInfo& gpu, GraphicsAPI api, bool* hidden) {
    if (hidden) *hidden = false;
    const std::string key = f.Key;
    // Vendor SDKs (FSR 3, XeSS, DLSS) need DX12/Vulkan *and* their SDK
    // integration, which is not done yet (PLANS.txt Phase 12).
    constexpr bool VendorSDKsIntegrated = false;
    const bool modernAPI = (api == GraphicsAPI::DirectX12 || api == GraphicsAPI::Vulkan) && VendorSDKsIntegrated;
    const char* needsAPI = api == GraphicsAPI::OpenGL ? "Needs the DirectX 12 / Vulkan renderer + vendor SDK (coming soon)"
                                                      : "Vendor SDK integration coming soon";

    if (key == "Upscaler") {
        if (option == UpscaleDLSS) {
            if (!gpu.NVIDIA_RTX) { if (hidden) *hidden = true; return "Requires an NVIDIA RTX GPU"; }
            return modernAPI ? "" : needsAPI;
        }
        if (option == UpscaleFSR3 || option == UpscaleXeSS) return modernAPI ? "" : needsAPI;
    }
    if (key == "FrameGeneration") {
        if (option == FrameGenDLSS) {
            if (!gpu.NVIDIA_RTX) { if (hidden) *hidden = true; return "Requires an NVIDIA RTX 40-series or newer GPU"; }
            return modernAPI ? "" : needsAPI;
        }
        if (option == FrameGenFSR3 || option == FrameGenXeSS) return modernAPI ? "" : needsAPI;
    }
    if (key == "Anisotropy" && option > 0 && (float)(1 << option) > gpu.MaxAnisotropy + 0.5f) return "Not supported by this GPU";
    return "";
}

std::string GraphicsSettings::ResolveForHardware(const GPUInfo& gpu, GraphicsAPI api) {
    std::string changes;
    for (const Field& f : Fields()) {
        if (f.Type != FieldType::Enum) continue;
        int v = Member<int>(*this, f.Offset);
        if (OptionUnavailable(f, v, gpu, api).empty()) continue;
        const int original = v;
        // Fall back to the closest lower option that works (option 0 always does,
        // except anisotropy which falls back to the GPU maximum).
        while (v > 0 && !OptionUnavailable(f, v, gpu, api).empty()) --v;
        if (std::string(f.Key) == "Upscaler" && v < UpscaleSpatial) v = UpscaleSpatial;
        Member<int>(*this, f.Offset) = v;
        changes += std::string(changes.empty() ? "" : ", ") + f.Label + ": " + f.Options[original] + " -> " + f.Options[v];
    }
    return changes;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
void GraphicsSettings::Serialize(std::ostream& out, const std::string& prefix) const {
    for (const Field& f : Fields()) {
        out << prefix << f.Key << "=";
        if (f.Type == FieldType::Enum) out << Member<int>(*this, f.Offset) << "    # " << f.Options[Clamp(Member<int>(*this, f.Offset), 0, (int)f.Options.size() - 1)];
        else out << Get(f.Key);
        out << "\n";
    }
}

bool GraphicsSettings::SaveFile(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
    if (!file) return false;
    file << "# Lapis graphics settings\n";
    Serialize(file, "");
    return (bool)file;
}

bool GraphicsSettings::LoadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;
    std::string line;
    while (std::getline(file, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
            size_t i = 0;
            while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
            return s.substr(i);
        };
        Set(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
    return true;
}

bool GraphicsSettings::operator==(const GraphicsSettings& o) const {
    for (const Field& f : Fields())
        if (GetNumber(f) != o.GetNumber(f)) return false;
    return true;
}

} // namespace Lapis
