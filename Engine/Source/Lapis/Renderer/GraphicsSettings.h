#pragma once

// GraphicsSettings: every user-facing rendering option, described once in a
// reflection table (GraphicsSettings::Fields()). That single table drives:
//   - the renderer (Renderer::ApplySettings)
//   - saving/loading (project.lapis "Graphics.*", LapisEditor.ini "Viewport.*",
//     Saved/graphics.cfg for a player's own choices)
//   - the editor panels (File > Settings > Viewport Quality and
//     Project > Game Graphics Settings) and
//   - the Luau `Graphics` library, so games can build their own settings menu.
//
// Two independent instances exist in the editor: the *viewport* quality
// (editor only, never exported) and the project's *game* quality (baked into
// the exported game as its defaults).

#include "Lapis/Core/Base.h"
#include "Lapis/RHI/RHI.h"

#include <iosfwd>
#include <utility>
#include <string>
#include <vector>

namespace Lapis {

enum class GPUVendor { Unknown, NVIDIA, AMD, Intel, Apple, Software };

struct GPUInfo {
    GPUVendor   Vendor = GPUVendor::Unknown;
    std::string Name;           // GL_RENDERER
    std::string VendorName;     // GL_VENDOR
    std::string APIVersion;     // GL_VERSION
    bool        NVIDIA_RTX = false;   // RTX-class NVIDIA GPU (DLSS capable hardware)
    float       MaxAnisotropy = 1.0f;

    static GPUInfo FromDevice(const RHI::DeviceInfo& info);
    static const char* VendorString(GPUVendor v);
};

// Which rendering backend is running (DX12 / Vulkan are planned: PLANS Phase 12).
enum class GraphicsAPI { OpenGL, DirectX12, Vulkan };

struct GraphicsSettings {
    // ---- Display ----
    bool  VSync = true;
    int   FrameCap = 0;              // index into FrameCapValues
    bool  Fullscreen = false;
    float Gamma = 2.2f;
    // ---- Quality ----
    float RenderScale = 1.0f;        // internal resolution multiplier
    int   Upscaler = 1;              // Upscalers enum
    float Sharpness = 0.35f;
    int   AntiAliasing = 3;          // AntiAliasingModes enum
    int   ShadowQuality = 2;         // off, low, medium, high, ultra
    float ShadowDistance = 45.0f;
    int   ShadowSoftness = 1;        // hard, soft, very soft
    int   Anisotropy = 3;            // off, 2x, 4x, 8x, 16x
    int   MaxPointLights = 16;
    float DrawDistance = 1000.0f;
    // ---- Effects ----
    bool  Bloom = true;
    float BloomIntensity = 0.6f;
    float BloomThreshold = 1.0f;
    bool  AmbientOcclusion = true;
    float AOIntensity = 1.0f;
    float AORadius = 0.6f;
    float Reflections = 1.0f;
    bool  Fog = true;
    float FogAmount = 1.0f;
    bool  MotionBlur = false;
    float MotionBlurStrength = 0.5f;
    // ---- Post processing ----
    int   Tonemapper = 0;            // ACES, Neutral, Reinhard, Filmic, None
    float Exposure = 1.0f;
    float Contrast = 1.0f;
    float Saturation = 1.0f;
    float Temperature = 0.0f;
    float Vignette = 0.3f;
    float FilmGrain = 0.0f;
    float ChromaticAberration = 0.0f;
    // ---- Frame generation ----
    int   FrameGeneration = 0;       // FrameGenModes enum

    enum Upscalers { UpscaleBilinear, UpscaleSpatial, UpscaleFSR3, UpscaleXeSS, UpscaleDLSS };
    enum AntiAliasingModes { AAOff, AAFXAA, AAMSAA2, AAMSAA4, AAMSAA8, AAMSAA4FXAA };
    enum FrameGenModes { FrameGenOff, FrameGenFSR3, FrameGenXeSS, FrameGenDLSS };
    static const int FrameCapValues[7];

    // Derived values used by the renderer / application.
    u32  MSAASamples() const;
    bool FXAA() const;
    u32  ShadowMapSize() const;      // 0 = shadows off
    u32  MaxFPS() const;
    float AnisotropyLevel() const;

    enum class Preset { Low, Medium, High, Ultra };
    void ApplyPreset(Preset preset);
    static GraphicsSettings FromPreset(Preset preset);
    static const char* PresetName(Preset preset);

    // ---- reflection -----------------------------------------------------------
    enum class FieldType { Bool, Float, Int, Enum };
    struct Field {
        const char* Key;            // stable name used in files and Luau
        const char* Label;          // UI label
        const char* Category;       // Display, Quality, Effects, Post Processing, Frame Generation
        FieldType   Type;
        size_t      Offset;
        float       Min = 0, Max = 1, Step = 0;
        std::vector<const char*> Options;   // Enum labels
        const char* Help = "";

        Field(const char* key, const char* label, const char* category, FieldType type, size_t offset, float min = 0, float max = 1,
              float step = 0, std::vector<const char*> options = {}, const char* help = "")
            : Key(key), Label(label), Category(category), Type(type), Offset(offset), Min(min), Max(max), Step(step),
              Options(std::move(options)), Help(help) {}
    };
    static const std::vector<Field>& Fields();
    static const Field* FindField(const std::string& key);
    static std::vector<std::string> Categories();

    // Generic access by key (values as text: "true", "0.5", or an option label/index).
    bool        Set(const std::string& key, const std::string& value);
    std::string Get(const std::string& key) const;
    float       GetNumber(const Field& f) const;     // bools/enums as numbers
    void        SetNumber(const Field& f, float value);

    // Availability of an enum option on this machine/backend. Returns an empty
    // string if available, otherwise the reason (shown greyed out in the UI).
    // Hidden options (e.g. DLSS without an NVIDIA RTX GPU) set *hidden = true.
    static std::string OptionUnavailable(const Field& f, int option, const GPUInfo& gpu, GraphicsAPI api, bool* hidden = nullptr);
    // Replaces unavailable choices with the best working fallback and returns
    // a description of what was changed (empty if nothing).
    std::string ResolveForHardware(const GPUInfo& gpu, GraphicsAPI api);

    // "prefix.Key=value" lines.
    void Serialize(std::ostream& out, const std::string& prefix) const;
    bool SaveFile(const std::string& path) const;
    bool LoadFile(const std::string& path);   // missing keys keep their values

    bool operator==(const GraphicsSettings& o) const;
    bool operator!=(const GraphicsSettings& o) const { return !(*this == o); }
};

} // namespace Lapis
