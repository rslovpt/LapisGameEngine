#include "Lapis/Renderer/Renderer.h"

#include "Lapis/Core/JobSystem.h"
#include "Lapis/Renderer/PostShaders.h"

#include <algorithm>

#include "Lapis/Core/Log.h"
#include "Lapis/Renderer/BuiltinShaders.h"
#include "Lapis/Renderer/GL.h"

namespace Lapis {

namespace {
    constexpr u32 MaxPointLights = 16;
    constexpr u32 ShadowSlot = 1;
}

bool Renderer::Init(u32 width, u32 height, u32 msaaSamples) {
    m_Width = width ? width : 1;
    m_Height = height ? height : 1;

    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_Samples = std::min<u32>(msaaSamples, (u32)std::max(maxSamples, 1));

    using namespace BuiltinShaders;
    m_LitShader    = CreateRef<Shader>("Lit", LitVertex, LitFragment);
    m_ShadowShader = CreateRef<Shader>("ShadowDepth", ShadowVertex, ShadowFragment);
    m_SkyShader    = CreateRef<Shader>("Sky", FullscreenVertex, SkyFragment);
    m_LineShader   = CreateRef<Shader>("DebugLine", LineVertex, LineFragment);
    m_SSAOShader      = CreateRef<Shader>("SSAO", FullscreenVertex, PostShaders::SSAOFragment);
    m_BlurShader      = CreateRef<Shader>("Blur", FullscreenVertex, PostShaders::BlurFragment);
    m_BloomDownShader = CreateRef<Shader>("BloomDown", FullscreenVertex, PostShaders::BloomDownFragment);
    m_BloomUpShader   = CreateRef<Shader>("BloomUp", FullscreenVertex, PostShaders::BloomUpFragment);
    m_CompositeShader = CreateRef<Shader>("Composite", FullscreenVertex, PostShaders::CompositeFragment);
    m_FXAAShader      = CreateRef<Shader>("FXAA", FullscreenVertex, PostShaders::FXAAFragment);
    m_UpscaleShader   = CreateRef<Shader>("Upscale", FullscreenVertex, PostShaders::UpscaleFragment);
    m_SharpenShader   = CreateRef<Shader>("Sharpen", FullscreenVertex, PostShaders::SharpenFragment);
    const Shader* shaders[] = {m_LitShader.get(), m_ShadowShader.get(), m_SkyShader.get(), m_LineShader.get(),
                               m_SSAOShader.get(), m_BlurShader.get(), m_BloomDownShader.get(), m_BloomUpShader.get(),
                               m_CompositeShader.get(), m_FXAAShader.get(), m_UpscaleShader.get(), m_SharpenShader.get()};
    for (const Shader* s : shaders)
        if (!s->IsValid()) { LAPIS_FATAL("Renderer: built-in shader '{}' failed", s->GetName()); return false; }

    m_GPU = GPUInfo::Detect();
    LAPIS_INFO("GPU: {} ({}), {}", m_GPU.Name, GPUInfo::VendorString(m_GPU.Vendor), m_GPU.APIVersion);
    m_RenderWidth = m_Width;
    m_RenderHeight = m_Height;

    FramebufferSpec hdr;
    hdr.Width = m_RenderWidth;
    hdr.Height = m_RenderHeight;
    hdr.Samples = m_Samples;
    hdr.Color = ColorFormat::RGBA16F;
    hdr.Depth = DepthFormat::Depth24;
    m_HDRBuffer = CreateScope<Framebuffer>(hdr);

    FramebufferSpec resolve = hdr;
    resolve.Samples = 1;                 // depth is resolved too (SSAO, motion blur)
    m_ResolveBuffer = CreateScope<Framebuffer>(resolve);

    FramebufferSpec shadow;
    shadow.Width = shadow.Height = m_ShadowSettings.MapSize;
    shadow.Color = ColorFormat::None;
    shadow.Depth = DepthFormat::Depth32F;
    shadow.DepthCompare = true;
    m_ShadowMap = CreateScope<Framebuffer>(shadow);

    if (!m_HDRBuffer->IsComplete() || !m_ResolveBuffer->IsComplete() || !m_ShadowMap->IsComplete()) {
        LAPIS_FATAL("Renderer: could not create render targets");
        return false;
    }

    m_EmptyVAO = CreateScope<VertexArray>();
    m_LineVBO = CreateRef<VertexBuffer>(nullptr, sizeof(DebugVertex) * 1024, BufferUsage::Stream);
    m_LineVAO = CreateScope<VertexArray>();
    m_LineVAO->AddVertexBuffer(m_LineVBO, {{AttribType::Float3}, {AttribType::Float4}});

    m_White = Texture2D::CreateSolid({1, 1, 1, 1});
    m_Black = Texture2D::CreateSolid({0, 0, 0, 1});
    CreatePostTargets();
    m_InstanceVBO = CreateRef<VertexBuffer>(nullptr, sizeof(InstanceData) * 1024, BufferUsage::Stream);
    m_ShadowInstanceVBO = CreateRef<VertexBuffer>(nullptr, sizeof(InstanceData) * 1024, BufferUsage::Stream);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    m_Initialized = true;
    LAPIS_INFO("Renderer initialized ({}x{}, {}x MSAA, shadow map {})", m_Width, m_Height, m_Samples, m_ShadowSettings.MapSize);
    return true;
}

void Renderer::Shutdown() {
    m_LitShader.reset(); m_ShadowShader.reset(); m_SkyShader.reset(); m_LineShader.reset();
    m_SSAOShader.reset(); m_BlurShader.reset(); m_BloomDownShader.reset(); m_BloomUpShader.reset();
    m_CompositeShader.reset(); m_FXAAShader.reset(); m_UpscaleShader.reset(); m_SharpenShader.reset();
    m_HDRBuffer.reset(); m_ResolveBuffer.reset(); m_ShadowMap.reset();
    m_AOBuffer.reset(); m_AOBlurBuffer.reset(); m_LDR[0].reset(); m_LDR[1].reset(); m_OutBuffer.reset();
    m_BloomMips.clear();
    m_EmptyVAO.reset(); m_LineVAO.reset(); m_LineVBO.reset(); m_White.reset(); m_Black.reset();
    m_PointLights.clear(); m_Lines.clear(); m_InstanceData.clear();
    m_InstanceVBO.reset(); m_ShadowInstanceVBO.reset();
    m_DrawList.clear();
    m_Initialized = false;
}

void Renderer::Resize(u32 width, u32 height) {
    if (!m_Initialized || width == 0 || height == 0) return;
    m_Width = width;
    m_Height = height;
    CreatePostTargets();
}

// (Re)creates every resolution-dependent target from the output size and
// the render scale. Framebuffer::Resize is a no-op when nothing changed.
void Renderer::CreatePostTargets() {
    const float scale = Clamp(m_Settings.RenderScale, 0.25f, 2.0f);
    m_RenderWidth = std::max(1u, (u32)std::lround(m_Width * scale));
    m_RenderHeight = std::max(1u, (u32)std::lround(m_Height * scale));
    m_HDRBuffer->Resize(m_RenderWidth, m_RenderHeight);
    m_ResolveBuffer->Resize(m_RenderWidth, m_RenderHeight);

    auto make = [](Scope<Framebuffer>& fb, u32 w, u32 h, ColorFormat format) {
        w = std::max(w, 1u);
        h = std::max(h, 1u);
        if (fb) { fb->Resize(w, h); return; }
        FramebufferSpec spec;
        spec.Width = w;
        spec.Height = h;
        spec.Color = format;
        spec.Depth = DepthFormat::None;
        fb = CreateScope<Framebuffer>(spec);
    };
    make(m_AOBuffer, m_RenderWidth / 2, m_RenderHeight / 2, ColorFormat::RGBA8);
    make(m_AOBlurBuffer, m_RenderWidth / 2, m_RenderHeight / 2, ColorFormat::RGBA8);
    make(m_LDR[0], m_RenderWidth, m_RenderHeight, ColorFormat::RGBA8);
    make(m_LDR[1], m_RenderWidth, m_RenderHeight, ColorFormat::RGBA8);
    make(m_OutBuffer, m_Width, m_Height, ColorFormat::RGBA8);

    // Bloom chain: half resolution down to ~8 px, at most 6 levels.
    u32 w = m_RenderWidth / 2, h = m_RenderHeight / 2;
    size_t level = 0;
    for (; level < 6 && w >= 8 && h >= 8; ++level, w /= 2, h /= 2) {
        if (level >= m_BloomMips.size()) m_BloomMips.emplace_back();
        make(m_BloomMips[level], w, h, ColorFormat::RGBA16F);
    }
    m_BloomMips.resize(std::max<size_t>(level, 1));
    if (!m_BloomMips[0]) make(m_BloomMips[0], 1, 1, ColorFormat::RGBA16F);
    m_HasPrevFrame = false;
}

void Renderer::ApplySettings(const GraphicsSettings& requested) {
    GraphicsSettings s = requested;
    const std::string changes = s.ResolveForHardware(m_GPU, GetAPI());
    if (!changes.empty()) LAPIS_INFO("Graphics settings adjusted for this GPU/backend: {}", changes);
    const bool rescale = s.RenderScale != m_Settings.RenderScale;
    m_Settings = s;
    if (!m_Initialized) return;
    SetMSAA(s.MSAASamples());
    m_ShadowSettings.Enabled = s.ShadowMapSize() > 0;
    if (s.ShadowMapSize() > 0) m_ShadowSettings.MapSize = s.ShadowMapSize();
    m_ShadowSettings.Distance = s.ShadowDistance;
    if (rescale) CreatePostTargets();
}

void Renderer::SetMSAA(u32 samples) {
    if (!m_Initialized) return;
    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples = std::max<u32>(1, std::min<u32>(samples, (u32)std::max(maxSamples, 1)));
    if (samples == m_Samples) return;
    m_Samples = samples;
    FramebufferSpec spec = m_HDRBuffer->GetSpec();
    spec.Samples = samples;
    m_HDRBuffer = CreateScope<Framebuffer>(spec);
    m_HasPrevFrame = false;
}

void Renderer::BeginFrame(const CameraView& camera, const Environment& env) {
    m_Camera = camera;
    m_Env = env;
    m_DrawList.clear();
    m_PointLights.clear();
    m_Lines.clear();
    m_Stats = {};
    if (m_ShadowMap && m_ShadowMap->GetSpec().Width != m_ShadowSettings.MapSize)
        m_ShadowMap->Resize(m_ShadowSettings.MapSize, m_ShadowSettings.MapSize);
}

void Renderer::AddPointLight(const PointLight& light) {
    if (m_PointLights.size() < 1024) m_PointLights.push_back(light);
}

void Renderer::Submit(const Ref<Mesh>& mesh, const Ref<Material>& material, const Mat4& transform, const Vec4& tint) {
    if (!mesh) return;
    ++m_Stats.Submitted;
    m_DrawList.push_back({mesh.get(), material.get(), transform, tint, mesh->GetBounds().Transformed(transform)});
}

void Renderer::EndFrame(u32 targetFBO) {
    if (!m_Initialized) return;

    // Group by material, then mesh: identical neighbours become instanced batches.
    std::sort(m_DrawList.begin(), m_DrawList.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.MaterialPtr != b.MaterialPtr ? a.MaterialPtr < b.MaterialPtr : a.MeshPtr < b.MeshPtr;
    });

    // Keep the point lights closest to the camera (relative to their range).
    const size_t maxLights = (size_t)Clamp(m_Settings.MaxPointLights, 0, (int)MaxPointLights);
    if (m_PointLights.size() > maxLights) {
        const Vec3 eye = m_Camera.Position;
        std::partial_sort(m_PointLights.begin(), m_PointLights.begin() + maxLights, m_PointLights.end(), [&](const PointLight& a, const PointLight& b) {
            return Distance(a.Position, eye) - a.Range < Distance(b.Position, eye) - b.Range;
        });
        m_PointLights.resize(maxLights);
    }

    // Settings that act through the environment.
    m_Env.FogDensity *= m_Settings.Fog ? m_Settings.FogAmount : 0.0f;
    m_Env.ReflectionIntensity *= m_Settings.Reflections;

    Mat4 lightSpace(1.0f);
    const bool shadows = m_Sun.CastShadows && m_ShadowSettings.Enabled;
    if (shadows) ShadowPass(lightSpace);
    MainPass(lightSpace, shadows);
    SkyPass();
    LinePass();
    PostPass(targetFBO);
    m_Stats.PointLights = (u32)m_PointLights.size();
}

void Renderer::ShadowPass(Mat4& lightSpace) {
    const float dist = m_ShadowSettings.Distance;
    const float radius = dist * 0.6f;
    const Vec3 center = m_Camera.Position + m_Camera.Forward() * (dist * 0.5f);
    const Vec3 dir = Normalize(m_Sun.Direction);
    const Vec3 up = std::fabs(dir.y) > 0.99f ? Vec3(0, 0, 1) : Vec3::Up();

    const Mat4 view = LookAt(center - dir * radius * 2.0f, center, up);
    Mat4 proj = Orthographic(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);

    // Snap to shadow-map texels so shadows don't shimmer as the camera moves.
    const float half = (float)m_ShadowSettings.MapSize * 0.5f;
    const Vec3 origin = (proj * view).TransformPoint(Vec3(0.0f)) * half;
    proj(0, 3) += (std::round(origin.x) - origin.x) / half;
    proj(1, 3) += (std::round(origin.y) - origin.y) / half;
    lightSpace = proj * view;

    const Frustum lightFrustum(lightSpace);
    m_ShadowMap->Bind();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glDisable(GL_CULL_FACE); // single-sided geometry (planes) must still cast

    m_ShadowShader->Bind();
    m_ShadowShader->SetMat4("u_LightSpace", lightSpace);
    std::vector<const DrawItem*> casters;
    casters.reserve(m_DrawList.size());
    for (const DrawItem& item : m_DrawList) {
        if (item.MaterialPtr && (!item.MaterialPtr->CastShadows || item.MaterialPtr->Unlit)) continue;
        if (!lightFrustum.Intersects(item.WorldBounds)) continue;
        casters.push_back(&item);
    }
    std::vector<Batch> batches;
    BuildBatches(casters, batches, *m_ShadowInstanceVBO);
    for (const Batch& b : batches) {
        b.MeshPtr->DrawInstanced(m_ShadowInstanceVBO->GetID(), b.First * sizeof(InstanceData), b.Count);
        ++m_Stats.DrawCalls;
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::MainPass(const Mat4& lightSpace, bool shadows) {
    m_HDRBuffer->Bind();
    const Vec3 fog = m_Env.SkyHorizon;
    glClearColor(fog.x, fog.y, fog.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);

    const Mat4 viewProj = m_Camera.ViewProjection();
    const Frustum frustum(viewProj);

    Shader& s = *m_LitShader;
    s.Bind();
    s.SetMat4("u_ViewProjection", viewProj);
    s.SetMat4("u_LightSpace", lightSpace);
    s.SetVec3("u_CameraPos", m_Camera.Position);
    s.SetVec3("u_SunDirection", Normalize(m_Sun.Direction));
    s.SetVec3("u_SunColor", m_Sun.Color * m_Sun.Intensity);
    s.SetVec3("u_AmbientSky", m_Env.SkyZenith * m_Env.AmbientIntensity * 1.2f);
    s.SetVec3("u_AmbientGround", m_Env.SkyGround * m_Env.AmbientIntensity);
    s.SetVec3("u_FogColor", m_Env.SkyHorizon);
    s.SetFloat("u_FogDensity", m_Env.FogDensity);
    s.SetVec3("u_SkyZenith", m_Env.SkyZenith);
    s.SetVec3("u_SkyHorizon", m_Env.SkyHorizon);
    s.SetVec3("u_SkyGround", m_Env.SkyGround);
    s.SetFloat("u_EnvIntensity", m_Env.ReflectionIntensity);
    s.SetInt("u_AlbedoMap", 0);
    s.SetInt("u_ShadowMap", (int)ShadowSlot);
    s.SetInt("u_ShadowsEnabled", shadows ? 1 : 0);
    s.SetFloat("u_ShadowTexel", 1.0f / (float)m_ShadowSettings.MapSize);
    s.SetInt("u_PCFRadius", Clamp(m_Settings.ShadowSoftness, 0, 2));

    glActiveTexture(GL_TEXTURE0 + ShadowSlot);
    glBindTexture(GL_TEXTURE_2D, m_ShadowMap->GetDepthTexture());

    s.SetInt("u_PointLightCount", (int)m_PointLights.size());
    for (size_t i = 0; i < m_PointLights.size(); ++i) {
        const PointLight& l = m_PointLights[i];
        const std::string base = "u_PointLights[" + std::to_string(i) + "].";
        s.SetVec3(base + "Position", l.Position);
        s.SetVec3(base + "Color", l.Color * l.Intensity);
        s.SetFloat(base + "Range", l.Range);
    }

    static const Material s_DefaultMaterial;
    // Frustum culling runs on the job system; compaction keeps draw order.
    std::vector<u8> inside(m_DrawList.size());
    const Vec3 eye = m_Camera.Position;
    const float drawDistance = m_Settings.DrawDistance;
    JobSystem::ParallelFor((u32)m_DrawList.size(), 512, [&](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i) {
            const AABB& b = m_DrawList[i].WorldBounds;
            const float radius = Length(b.Size()) * 0.5f;
            const bool near = Distance(b.Center(), eye) - radius <= drawDistance;
            inside[i] = near && frustum.Intersects(b) ? 1 : 0;
        }
    });
    std::vector<const DrawItem*> visible;
    visible.reserve(m_DrawList.size());
    for (size_t i = 0; i < m_DrawList.size(); ++i) {
        if (!inside[i]) { ++m_Stats.Culled; continue; }
        visible.push_back(&m_DrawList[i]);
    }
    std::vector<Batch> batches;
    BuildBatches(visible, batches, *m_InstanceVBO);

    const Material* bound = nullptr;
    for (const Batch& b : batches) {
        const Material* mat = b.MaterialPtr ? b.MaterialPtr : &s_DefaultMaterial;
        if (mat != bound) {
            bound = mat;
            s.SetVec4("u_Albedo", mat->Albedo);
            s.SetVec3("u_Emissive", mat->Emissive);
            s.SetFloat("u_Metallic", mat->Metallic);
            s.SetFloat("u_Roughness", mat->Roughness);
            s.SetInt("u_Unlit", mat->Unlit ? 1 : 0);
            s.SetVec2("u_Tiling", mat->Tiling);
            (mat->AlbedoMap ? mat->AlbedoMap : m_White)->Bind(0);
            if (mat->AlbedoMap && m_GPU.MaxAnisotropy > 1.0f)
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(m_Settings.AnisotropyLevel(), m_GPU.MaxAnisotropy));
        }
        b.MeshPtr->DrawInstanced(m_InstanceVBO->GetID(), b.First * sizeof(InstanceData), b.Count);
        ++m_Stats.DrawCalls;
        m_Stats.Instances += b.Count;
        m_Stats.Triangles += b.MeshPtr->GetIndexCount() / 3 * b.Count;
    }
}

void Renderer::BuildBatches(const std::vector<const DrawItem*>& items, std::vector<Batch>& out, VertexBuffer& buffer) {
    m_InstanceData.clear();
    m_InstanceData.reserve(items.size());
    for (const DrawItem* item : items) {
        if (!out.empty() && out.back().MeshPtr == item->MeshPtr && out.back().MaterialPtr == item->MaterialPtr)
            ++out.back().Count;
        else
            out.push_back({item->MeshPtr, item->MaterialPtr, m_InstanceData.size(), 1});
        m_InstanceData.push_back({item->Transform, item->Tint});
    }
    if (!m_InstanceData.empty()) buffer.SetData(m_InstanceData.data(), m_InstanceData.size() * sizeof(InstanceData));
}

void Renderer::SkyPass() {
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    m_SkyShader->Bind();
    m_SkyShader->SetMat4("u_InverseViewProjection", Inverse(m_Camera.ViewProjection()));
    m_SkyShader->SetVec3("u_CameraPos", m_Camera.Position);
    m_SkyShader->SetVec3("u_SunDirection", Normalize(m_Sun.Direction));
    m_SkyShader->SetVec3("u_SunColor", m_Sun.Color * m_Sun.Intensity);
    m_SkyShader->SetVec3("u_SkyZenith", m_Env.SkyZenith);
    m_SkyShader->SetVec3("u_SkyHorizon", m_Env.SkyHorizon);
    m_SkyShader->SetVec3("u_SkyGround", m_Env.SkyGround);
    m_EmptyVAO->Bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++m_Stats.DrawCalls;
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void Renderer::LinePass() {
    if (m_Lines.empty()) return;
    m_LineVBO->SetData(m_Lines.data(), m_Lines.size() * sizeof(DebugVertex));
    m_LineShader->Bind();
    m_LineShader->SetMat4("u_ViewProjection", m_Camera.ViewProjection());
    m_LineVAO->Bind();
    glDrawArrays(GL_LINES, 0, (GLsizei)m_Lines.size());
    ++m_Stats.DrawCalls;
}

void Renderer::FullscreenPass() {
    m_EmptyVAO->Bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++m_Stats.DrawCalls;
}

void Renderer::SSAOPass() {
    const Mat4& proj = m_Camera.Projection;
    m_AOBuffer->Bind();
    m_SSAOShader->Bind();
    m_SSAOShader->SetInt("u_Depth", 0);
    m_SSAOShader->SetMat4("u_Projection", proj);
    m_SSAOShader->SetMat4("u_InvProjection", Inverse(proj));
    m_SSAOShader->SetVec2("u_DepthTexel", {1.0f / (float)m_RenderWidth, 1.0f / (float)m_RenderHeight});
    m_SSAOShader->SetFloat("u_Radius", m_Settings.AORadius);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ResolveBuffer->GetDepthTexture());
    FullscreenPass();

    m_AOBlurBuffer->Bind();
    m_BlurShader->Bind();
    m_BlurShader->SetInt("u_Source", 0);
    m_BlurShader->SetVec2("u_Texel", {1.0f / (float)m_AOBuffer->GetSpec().Width, 1.0f / (float)m_AOBuffer->GetSpec().Height});
    glBindTexture(GL_TEXTURE_2D, m_AOBuffer->GetColorTexture());
    FullscreenPass();
}

void Renderer::BloomPass() {
    // Downsample chain (first pass thresholds the HDR scene).
    m_BloomDownShader->Bind();
    m_BloomDownShader->SetInt("u_Source", 0);
    m_BloomDownShader->SetFloat("u_Threshold", m_Settings.BloomThreshold);
    glActiveTexture(GL_TEXTURE0);
    u32 srcTex = m_ResolveBuffer->GetColorTexture();
    Vec2 srcTexel{1.0f / (float)m_RenderWidth, 1.0f / (float)m_RenderHeight};
    for (size_t i = 0; i < m_BloomMips.size(); ++i) {
        Framebuffer& dst = *m_BloomMips[i];
        dst.Bind();
        m_BloomDownShader->SetInt("u_FirstPass", i == 0 ? 1 : 0);
        m_BloomDownShader->SetVec2("u_SrcTexel", srcTexel);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        FullscreenPass();
        srcTex = dst.GetColorTexture();
        srcTexel = {1.0f / (float)dst.GetSpec().Width, 1.0f / (float)dst.GetSpec().Height};
    }
    // Upsample back up, adding each level onto the next larger one.
    m_BloomUpShader->Bind();
    m_BloomUpShader->SetInt("u_Source", 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (size_t i = m_BloomMips.size() - 1; i > 0; --i) {
        const Framebuffer& src = *m_BloomMips[i];
        m_BloomMips[i - 1]->Bind();
        m_BloomUpShader->SetVec2("u_SrcTexel", {1.0f / (float)src.GetSpec().Width, 1.0f / (float)src.GetSpec().Height});
        glBindTexture(GL_TEXTURE_2D, src.GetColorTexture());
        FullscreenPass();
    }
    glDisable(GL_BLEND);
}

void Renderer::PostPass(u32 targetFBO) {
    // Resolve MSAA -> single-sample HDR color + depth textures.
    m_HDRBuffer->BlitTo(m_ResolveBuffer.get(), true, true);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    const GraphicsSettings& gs = m_Settings;
    if (gs.AmbientOcclusion) SSAOPass();
    if (gs.Bloom && gs.BloomIntensity > 0.0f) BloomPass();

    // Which LDR passes follow the composite?
    const bool fxaa = gs.FXAA();
    const bool upscale = m_RenderWidth != m_Width || m_RenderHeight != m_Height;
    const bool sharpen = gs.Sharpness > 0.001f;

    // ---- composite: HDR -> LDR ----
    if (fxaa || upscale || sharpen) m_LDR[0]->Bind();
    else Framebuffer::BindTarget(targetFBO, m_Width, m_Height);
    Shader& c = *m_CompositeShader;
    c.Bind();
    c.SetInt("u_HDR", 0);
    c.SetInt("u_Depth", 1);
    c.SetInt("u_AO", 2);
    c.SetInt("u_Bloom", 3);
    c.SetFloat("u_Exposure", m_Env.Exposure * gs.Exposure);
    c.SetInt("u_Tonemapper", gs.Tonemapper);
    c.SetFloat("u_Contrast", gs.Contrast);
    c.SetFloat("u_Saturation", gs.Saturation);
    c.SetFloat("u_Temperature", gs.Temperature);
    c.SetFloat("u_Gamma", gs.Gamma);
    c.SetFloat("u_Vignette", gs.Vignette);
    c.SetFloat("u_Grain", gs.FilmGrain);
    c.SetFloat("u_Chromatic", gs.ChromaticAberration);
    c.SetFloat("u_BloomIntensity", gs.Bloom ? gs.BloomIntensity : 0.0f);
    c.SetFloat("u_AOIntensity", gs.AmbientOcclusion ? gs.AOIntensity : 0.0f);
    const Mat4 viewProj = m_Camera.ViewProjection();
    c.SetFloat("u_MotionBlur", gs.MotionBlur && m_HasPrevFrame ? gs.MotionBlurStrength : 0.0f);
    c.SetMat4("u_InvViewProj", Inverse(viewProj));
    c.SetMat4("u_PrevViewProj", m_PrevViewProj);
    c.SetFloat("u_Time", m_Time);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ResolveBuffer->GetColorTexture());
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, m_ResolveBuffer->GetDepthTexture());
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, gs.AmbientOcclusion ? m_AOBlurBuffer->GetColorTexture() : m_White->GetID());
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, gs.Bloom ? m_BloomMips[0]->GetColorTexture() : m_Black->GetID());
    FullscreenPass();
    glActiveTexture(GL_TEXTURE0);

    // ---- LDR chain: FXAA (internal) -> upscale (output) -> sharpen (output) ----
    const Framebuffer* current = m_LDR[0].get();
    if (fxaa) {
        const bool last = !upscale && !sharpen;
        if (last) Framebuffer::BindTarget(targetFBO, m_Width, m_Height);
        else m_LDR[1]->Bind();
        m_FXAAShader->Bind();
        m_FXAAShader->SetInt("u_Source", 0);
        m_FXAAShader->SetVec2("u_Texel", {1.0f / (float)m_RenderWidth, 1.0f / (float)m_RenderHeight});
        glBindTexture(GL_TEXTURE_2D, current->GetColorTexture());
        FullscreenPass();
        current = m_LDR[1].get();
    }
    if (upscale) {
        if (sharpen) m_OutBuffer->Bind();
        else Framebuffer::BindTarget(targetFBO, m_Width, m_Height);
        m_UpscaleShader->Bind();
        m_UpscaleShader->SetInt("u_Source", 0);
        m_UpscaleShader->SetVec2("u_SrcSize", {(float)m_RenderWidth, (float)m_RenderHeight});
        // Supersampling (scale > 1) always uses the bilinear downsample.
        const bool down = m_RenderWidth > m_Width;
        m_UpscaleShader->SetInt("u_Mode", down || gs.Upscaler == GraphicsSettings::UpscaleBilinear ? 0 : 1);
        glBindTexture(GL_TEXTURE_2D, current->GetColorTexture());
        FullscreenPass();
        current = m_OutBuffer.get();
    }
    if (sharpen) {
        Framebuffer::BindTarget(targetFBO, m_Width, m_Height);
        m_SharpenShader->Bind();
        m_SharpenShader->SetInt("u_Source", 0);
        m_SharpenShader->SetFloat("u_Sharpness", gs.Sharpness);
        m_SharpenShader->SetVec2("u_Texel", {1.0f / (float)current->GetSpec().Width, 1.0f / (float)current->GetSpec().Height});
        glBindTexture(GL_TEXTURE_2D, current->GetColorTexture());
        FullscreenPass();
    }

    m_PrevViewProj = viewProj;
    m_HasPrevFrame = true;
    m_Time += 1.0f / 60.0f;
    if (m_Time > 1000.0f) m_Time = 0.0f;
    glEnable(GL_DEPTH_TEST);
    VertexArray::Unbind();
    Framebuffer::BindTarget(targetFBO, m_Width, m_Height);
}

// --- debug drawing ---------------------------------------------------------------
void Renderer::DrawLine(const Vec3& a, const Vec3& b, const Vec4& color) {
    m_Lines.push_back({a, color});
    m_Lines.push_back({b, color});
}

void Renderer::DrawAABB(const AABB& box, const Vec4& color) {
    const Vec3& n = box.Min;
    const Vec3& x = box.Max;
    const Vec3 c[8] = {{n.x, n.y, n.z}, {x.x, n.y, n.z}, {x.x, x.y, n.z}, {n.x, x.y, n.z},
                       {n.x, n.y, x.z}, {x.x, n.y, x.z}, {x.x, x.y, x.z}, {n.x, x.y, x.z}};
    const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& edge : e) DrawLine(c[edge[0]], c[edge[1]], color);
}

void Renderer::DrawWireSphere(const Vec3& center, float radius, const Vec4& color) {
    constexpr int Segments = 24;
    for (int axis = 0; axis < 3; ++axis) {
        for (int i = 0; i < Segments; ++i) {
            const float a0 = TWO_PI * i / Segments, a1 = TWO_PI * (i + 1) / Segments;
            auto point = [&](float a) {
                const float c = std::cos(a) * radius, s = std::sin(a) * radius;
                if (axis == 0) return center + Vec3(0, c, s);
                if (axis == 1) return center + Vec3(c, 0, s);
                return center + Vec3(c, s, 0);
            };
            DrawLine(point(a0), point(a1), color);
        }
    }
}

void Renderer::DrawAxes(const Mat4& t, float size) {
    const Vec3 o = t.TransformPoint(Vec3(0.0f));
    DrawLine(o, t.TransformPoint({size, 0, 0}), {1, 0, 0, 1});
    DrawLine(o, t.TransformPoint({0, size, 0}), {0, 1, 0, 1});
    DrawLine(o, t.TransformPoint({0, 0, size}), {0, 0, 1, 1});
}

std::vector<u8> Renderer::ReadPixels(u32 fbo) const {
    std::vector<u8> pixels(size_t(m_Width) * m_Height * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)m_Width, (GLsizei)m_Height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return pixels;
}

} // namespace Lapis
