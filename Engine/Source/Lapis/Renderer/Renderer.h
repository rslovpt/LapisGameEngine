#pragma once
// Forward scene renderer:
//   shadow pass -> HDR MSAA lit pass -> sky -> debug lines -> resolve -> tone map
//
// Usage per frame:
//   renderer.BeginFrame(cameraView, environment);
//   renderer.SetDirectionalLight(sun);  renderer.AddPointLight(...);
//   renderer.Submit(mesh, material, transform);  ...
//   renderer.EndFrame(window.GetBackbufferFBO());

#include <vector>

#include "Lapis/Core/Base.h"
#include "Lapis/RHI/RHI.h"
#include "Lapis/Math/Geometry.h"
#include "Lapis/Renderer/Camera.h"
#include "Lapis/Renderer/GraphicsSettings.h"
#include "Lapis/Renderer/Material.h"
#include "Lapis/Renderer/Mesh.h"
#include "Lapis/Renderer/Texture.h"

namespace Lapis {

struct DirectionalLight {
    Vec3  Direction{-0.45f, -1.0f, -0.35f}; // direction the light travels
    Vec3  Color{1.0f, 0.95f, 0.88f};
    float Intensity = 3.2f;
    bool  CastShadows = true;
};

struct PointLight {
    Vec3  Position;
    Vec3  Color{1.0f};
    float Intensity = 10.0f;
    float Range = 10.0f;
};

struct Environment {
    Vec3  SkyZenith{0.16f, 0.32f, 0.75f};   // linear HDR colors
    Vec3  SkyHorizon{0.70f, 0.80f, 0.95f};
    Vec3  SkyGround{0.25f, 0.24f, 0.22f};
    float AmbientIntensity = 0.45f;
    float ReflectionIntensity = 1.0f;
    float FogDensity = 0.006f;
    float Exposure = 1.0f;
};

struct ShadowSettings {
    bool  Enabled = true;
    u32   MapSize = 2048;
    float Distance = 45.0f; // world units covered in front of the camera
};

struct RenderStats {
    u32 DrawCalls = 0;
    u32 Triangles = 0;
    u32 Instances = 0;      // objects drawn in the main pass
    u32 Submitted = 0;
    u32 Culled = 0;
    u32 PointLights = 0;
};

// Forward renderer with HDR, shadows, instancing and a full post chain.
// Talks only to the RHI, so it runs on every graphics backend.
class Renderer : NonCopyable {
public:
    bool Init(RHI::Device& device, u32 width, u32 height, u32 msaaSamples = 4);
    void Shutdown();
    void Resize(u32 width, u32 height);
    void SetMSAA(u32 samples);          // recreates the HDR target
    u32  GetMSAA() const { return m_Samples; }

    // Applies every quality / effect / post-processing option. Options the
    // GPU or backend can't run fall back automatically (see GraphicsSettings).
    void ApplySettings(const GraphicsSettings& settings);
    const GraphicsSettings& GetSettings() const { return m_Settings; }
    const GPUInfo& GetGPUInfo() const { return m_GPU; }
    GraphicsAPI GetAPI() const;
    u32 GetRenderWidth() const { return m_RenderWidth; }    // internal resolution
    u32 GetRenderHeight() const { return m_RenderHeight; }

    void BeginFrame(const CameraView& camera, const Environment& env = {});
    void SetDirectionalLight(const DirectionalLight& light) { m_Sun = light; }
    void AddPointLight(const PointLight& light);
    void Submit(const Ref<Mesh>& mesh, const Ref<Material>& material, const Mat4& transform, const Vec4& tint = Vec4(1.0f));
    void EndFrame();                    // renders everything into the window

    void DrawLine(const Vec3& a, const Vec3& b, const Vec4& color = {1, 1, 0, 1});
    void DrawAABB(const AABB& box, const Vec4& color = {0, 1, 0, 1});
    void DrawWireSphere(const Vec3& center, float radius, const Vec4& color = {0, 1, 1, 1});
    void DrawAxes(const Mat4& transform, float size = 1.0f);

    ShadowSettings& GetShadowSettings() { return m_ShadowSettings; }
    const RenderStats& GetStats() const { return m_Stats; }
    const Ref<Texture2D>& GetWhiteTexture() const { return m_White; }

    // RGBA8 pixels of the last frame, bottom row first.
    std::vector<u8> ReadPixels(u32& width, u32& height) const;

private:
    struct DrawItem {
        Mesh*     MeshPtr;
        Material* MaterialPtr;
        Mat4      Transform;
        Vec4      Tint;
        AABB      WorldBounds;
    };
    struct DebugVertex { Vec3 Position; Vec4 Color; };
    struct InstanceData { Mat4 Model; Vec4 Tint; };
    struct Batch { Mesh* MeshPtr; Material* MaterialPtr; size_t First; u32 Count; };

    // A render target with its own textures.
    struct Target {
        RHI::TextureHandle Color, Depth;
        RHI::RenderTargetHandle RT;
        u32 Width = 0, Height = 0;
    };
    void CreateTarget(Target& t, u32 w, u32 h, RHI::Format color, RHI::Format depth, u32 samples, const char* name);
    void DestroyTarget(Target& t);

    void ShadowPass(Mat4& lightSpace);
    void MainPass(const Mat4& lightSpace, bool shadows);
    void SkyPass();
    void LinePass();
    void PostPass();
    void SSAOPass();
    void BloomPass();
    void CreatePostTargets();
    void CreatePipelines();
    void DestroyPipelines();
    void FullscreenPass(RHI::PipelineHandle pipeline);
    RHI::BufferView BuildBatches(const std::vector<const DrawItem*>& items, std::vector<Batch>& out);

    RHI::Device* m_Device = nullptr;
    u32 m_Width = 0, m_Height = 0;               // output (window) size
    u32 m_RenderWidth = 0, m_RenderHeight = 0;   // internal size (render scale)
    u32 m_Samples = 4;

    struct Pipelines {
        RHI::PipelineHandle Lit, Shadow, Sky, Line, SSAO, Blur, BloomDown, BloomUp, Composite, FXAA, Upscale, Sharpen;
    } m_P;
    u32 m_PipelineSamples = 0;                   // MSAA the scene pipelines were built for

    Target m_HDR;            // multisampled scene (or single-sampled if MSAA off)
    Target m_Resolve;        // single-sampled HDR color + depth
    Target m_ShadowMap;
    Target m_AO, m_AOBlur;   // half internal resolution
    Target m_LDR[2];         // internal resolution
    Target m_Out;            // output resolution (upscale -> sharpen)
    std::vector<Target> m_BloomMips;
    u32 m_ShadowMapSize = 0;

    std::vector<InstanceData> m_InstanceData;
    Ref<Texture2D> m_White, m_Black;
    GraphicsSettings m_Settings;
    GPUInfo m_GPU;
    Mat4  m_PrevViewProj = Mat4::Identity();
    bool  m_HasPrevFrame = false;
    float m_Time = 0.0f;

    CameraView        m_Camera;
    Environment       m_Env;
    DirectionalLight  m_Sun;
    ShadowSettings    m_ShadowSettings;
    std::vector<PointLight> m_PointLights;
    std::vector<DrawItem>   m_DrawList;
    std::vector<DebugVertex> m_Lines;
    RenderStats m_Stats;
    bool m_Initialized = false;
};

} // namespace Lapis
