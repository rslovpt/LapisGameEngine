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
#include "Lapis/Math/Geometry.h"
#include "Lapis/Renderer/Camera.h"
#include "Lapis/Renderer/Framebuffer.h"
#include "Lapis/Renderer/Material.h"
#include "Lapis/Renderer/Mesh.h"
#include "Lapis/Renderer/Shader.h"

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

class Renderer : NonCopyable {
public:
    bool Init(u32 width, u32 height, u32 msaaSamples = 4);
    void Shutdown();
    void Resize(u32 width, u32 height);
    void SetMSAA(u32 samples);          // recreates the HDR target
    u32  GetMSAA() const { return m_Samples; }

    void BeginFrame(const CameraView& camera, const Environment& env = {});
    void SetDirectionalLight(const DirectionalLight& light) { m_Sun = light; }
    void AddPointLight(const PointLight& light);
    void Submit(const Ref<Mesh>& mesh, const Ref<Material>& material, const Mat4& transform, const Vec4& tint = Vec4(1.0f));
    void EndFrame(u32 targetFBO);

    // Debug drawing (cleared every frame).
    void DrawLine(const Vec3& a, const Vec3& b, const Vec4& color = {1, 1, 0, 1});
    void DrawAABB(const AABB& box, const Vec4& color = {0, 1, 0, 1});
    void DrawWireSphere(const Vec3& center, float radius, const Vec4& color = {0, 1, 1, 1});
    void DrawAxes(const Mat4& transform, float size = 1.0f);

    ShadowSettings& GetShadowSettings() { return m_ShadowSettings; }
    const RenderStats& GetStats() const { return m_Stats; }
    const Ref<Texture2D>& GetWhiteTexture() const { return m_White; }

    // Reads back the last presented frame (RGBA8, bottom-up) from `fbo`.
    std::vector<u8> ReadPixels(u32 fbo) const;

private:
    struct DrawItem {
        Mesh*     MeshPtr;
        Material* MaterialPtr;
        Mat4      Transform;
        Vec4      Tint;
        AABB      WorldBounds;
    };
    struct DebugVertex { Vec3 Position; Vec4 Color; };

    void ShadowPass(Mat4& lightSpace);
    void MainPass(const Mat4& lightSpace, bool shadows);
    void SkyPass();
    void LinePass();
    void PostPass(u32 targetFBO);

    u32 m_Width = 0, m_Height = 0;
    u32 m_Samples = 4;

    Ref<Shader> m_LitShader, m_ShadowShader, m_SkyShader, m_PostShader, m_LineShader;
    Scope<Framebuffer> m_HDRBuffer;      // multisampled (or not, if samples <= 1)
    Scope<Framebuffer> m_ResolveBuffer;  // single-sampled HDR texture
    Scope<Framebuffer> m_ShadowMap;
    Scope<VertexArray> m_EmptyVAO;       // for fullscreen triangles
    Scope<VertexArray> m_LineVAO;
    Ref<VertexBuffer>  m_LineVBO;
    Ref<VertexBuffer>  m_InstanceVBO, m_ShadowInstanceVBO;
    struct InstanceData { Mat4 Model; Vec4 Tint; };
    std::vector<InstanceData> m_InstanceData;

    // Consecutive draw items with the same mesh+material become one instanced draw.
    struct Batch { Mesh* MeshPtr; Material* MaterialPtr; size_t First; u32 Count; };
    void BuildBatches(const std::vector<const DrawItem*>& items, std::vector<Batch>& out, VertexBuffer& buffer);
    Ref<Texture2D>     m_White;

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
