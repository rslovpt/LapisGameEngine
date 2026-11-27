#include "Lapis/Renderer/Renderer.h"

#include <algorithm>
#include <cmath>

#include "Lapis/Core/JobSystem.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Renderer/ShaderTypes.h"

namespace Lapis {

namespace {
constexpr u32 MaxPointLights = (u32)ShaderTypes::MaxPointLights;

// Vertex layouts.
std::vector<RHI::VertexAttribute> MeshLayout(bool instanced, bool full) {
    using RHI::AttribFormat;
    std::vector<RHI::VertexAttribute> a = {{0, 0, AttribFormat::Float3, 0}};
    if (full) {
        a.push_back({1, 0, AttribFormat::Float3, 12});
        a.push_back({2, 0, AttribFormat::Float2, 24});
    }
    if (instanced) {
        for (u8 i = 0; i < 4; ++i) a.push_back({u8(3 + i), 1, AttribFormat::Float4, u32(i * 16)});   // model columns
        if (full) a.push_back({7, 1, AttribFormat::Float4, 64});                                        // tint
    }
    return a;
}
} // namespace

GraphicsAPI Renderer::GetAPI() const {
    if (m_Device && m_Device->GetInfo().Api == RHI::API::D3D12) return GraphicsAPI::DirectX12;
    if (m_Device && m_Device->GetInfo().Api == RHI::API::Vulkan) return GraphicsAPI::Vulkan;
    return GraphicsAPI::OpenGL;
}

// =============================================================================
// Setup
// =============================================================================
void Renderer::CreateTarget(Target& t, u32 w, u32 h, RHI::Format color, RHI::Format depth, u32 samples, const char* name) {
    w = std::max(w, 1u);
    h = std::max(h, 1u);
    if (t.RT && t.Width == w && t.Height == h) {
        const RHI::TextureDesc* d = t.Color ? m_Device->GetTextureDesc(t.Color) : m_Device->GetTextureDesc(t.Depth);
        if (d && d->Samples == samples) return;   // unchanged
    }
    DestroyTarget(t);
    RHI::TextureDesc desc;
    desc.Width = w;
    desc.Height = h;
    desc.Samples = samples;
    desc.RenderTarget = true;
    desc.Name = name;
    if (color != RHI::Format::None) { desc.Fmt = color; t.Color = m_Device->CreateTexture(desc); }
    if (depth != RHI::Format::None) { desc.Fmt = depth; t.Depth = m_Device->CreateTexture(desc); }
    t.RT = m_Device->CreateRenderTarget(t.Color, t.Depth, name);
    t.Width = w;
    t.Height = h;
}

void Renderer::DestroyTarget(Target& t) {
    if (!m_Device) return;
    if (t.RT) m_Device->Destroy(t.RT);
    if (t.Color) m_Device->Destroy(t.Color);
    if (t.Depth) m_Device->Destroy(t.Depth);
    t = Target{};
}

bool Renderer::Init(RHI::Device& device, u32 width, u32 height, u32 msaaSamples) {
    m_Device = &device;
    m_Width = width ? width : 1;
    m_Height = height ? height : 1;
    const RHI::DeviceInfo& info = device.GetInfo();
    m_Samples = std::max(1u, std::min(msaaSamples, info.MaxSamples));
    m_GPU = GPUInfo::FromDevice(info);
    LAPIS_INFO("GPU: {} ({}), {}", m_GPU.Name, GPUInfo::VendorString(m_GPU.Vendor), m_GPU.APIVersion);

    m_White = Texture2D::CreateSolid({1, 1, 1, 1});
    m_Black = Texture2D::CreateSolid({0, 0, 0, 1});
    m_ShadowMapSize = m_ShadowSettings.MapSize;
    CreateTarget(m_ShadowMap, m_ShadowMapSize, m_ShadowMapSize, RHI::Format::None, RHI::Format::D32F, 1, "ShadowMap");
    CreatePostTargets();
    CreatePipelines();
    if (!m_P.Lit || !m_P.Shadow || !m_P.Sky || !m_P.Composite || !m_P.Line) {
        LAPIS_FATAL("Renderer: built-in shaders failed to compile on {}", RHI::APIName(info.Api));
        return false;
    }
    m_Initialized = true;
    LAPIS_INFO("Renderer initialized ({}x{}, {}x MSAA, shadow map {}, {})", m_Width, m_Height, m_Samples, m_ShadowMapSize,
               RHI::APIName(info.Api));
    return true;
}

void Renderer::CreatePipelines() {
    DestroyPipelines();
    using namespace RHI;
    using RF = RHI::Format;
    auto make = [&](const char* shader, RF color, RF depth, u32 samples, auto&& configure) {
        PipelineDesc d;
        d.Shader = shader;
        d.ColorFormat = color;
        d.DepthFormat = depth;
        d.Samples = samples;
        d.Name = shader;
        configure(d);
        return m_Device->CreatePipeline(d);
    };
    auto none = [](PipelineDesc&) {};

    // Scene pipelines (HDR target, MSAA).
    m_P.Lit = make("Lit", RF::RGBA16F, RF::D24, m_Samples, [](PipelineDesc& d) {
        d.Attributes = MeshLayout(true, true);
        d.Strides[0] = sizeof(Vertex);
        d.Strides[1] = sizeof(Mat4) + sizeof(Vec4);
        d.Cull = CullMode::Back;
        d.Depth = DepthMode::Less;
        d.Blend = BlendMode::Opaque;
    });
    m_P.Sky = make("Sky", RF::RGBA16F, RF::D24, m_Samples, [](PipelineDesc& d) { d.Depth = DepthMode::LessEqualRead; });
    m_P.Line = make("Line", RF::RGBA16F, RF::D24, m_Samples, [](PipelineDesc& d) {
        d.Attributes = {{0, 0, AttribFormat::Float3, 0}, {1, 0, AttribFormat::Float4, 12}};
        d.Strides[0] = sizeof(DebugVertex);
        d.Prim = Topology::Lines;
        d.Depth = DepthMode::Less;
    });
    m_P.Shadow = make("Shadow", RF::None, RF::D32F, 1, [](PipelineDesc& d) {
        d.Attributes = MeshLayout(true, false);
        d.Strides[0] = sizeof(Vertex);
        d.Strides[1] = sizeof(Mat4) + sizeof(Vec4);
        d.Cull = CullMode::None;          // single-sided geometry (planes) must still cast
        d.Depth = DepthMode::Less;
        d.DepthBias = 4.0f;
        d.SlopeBias = 2.0f;
    });
    // Post (single-sampled).
    m_P.SSAO = make("SSAO", RF::RGBA8, RF::None, 1, none);
    m_P.Blur = make("Blur", RF::RGBA8, RF::None, 1, none);
    m_P.BloomDown = make("BloomDown", RF::RGBA16F, RF::None, 1, none);
    m_P.BloomUp = make("BloomUp", RF::RGBA16F, RF::None, 1, [](PipelineDesc& d) { d.Blend = BlendMode::Additive; });
    m_P.Composite = make("Composite", RF::RGBA8, RF::None, 1, none);
    m_P.FXAA = make("FXAA", RF::RGBA8, RF::None, 1, none);
    m_P.Upscale = make("Upscale", RF::RGBA8, RF::None, 1, none);
    m_P.Sharpen = make("Sharpen", RF::RGBA8, RF::None, 1, none);
    m_PipelineSamples = m_Samples;
}

void Renderer::DestroyPipelines() {
    if (!m_Device) return;
    RHI::PipelineHandle* all[] = {&m_P.Lit, &m_P.Shadow, &m_P.Sky, &m_P.Line, &m_P.SSAO, &m_P.Blur, &m_P.BloomDown,
                                  &m_P.BloomUp, &m_P.Composite, &m_P.FXAA, &m_P.Upscale, &m_P.Sharpen};
    for (RHI::PipelineHandle* p : all) {
        if (*p) m_Device->Destroy(*p);
        *p = {};
    }
}

void Renderer::Shutdown() {
    if (!m_Device) return;
    DestroyPipelines();
    DestroyTarget(m_HDR); DestroyTarget(m_Resolve); DestroyTarget(m_ShadowMap);
    DestroyTarget(m_AO); DestroyTarget(m_AOBlur); DestroyTarget(m_LDR[0]); DestroyTarget(m_LDR[1]); DestroyTarget(m_Out);
    for (Target& t : m_BloomMips) DestroyTarget(t);
    m_BloomMips.clear();
    m_White.reset(); m_Black.reset();
    m_PointLights.clear(); m_Lines.clear(); m_InstanceData.clear(); m_DrawList.clear();
    m_Initialized = false;
    m_Device = nullptr;
}

void Renderer::Resize(u32 width, u32 height) {
    if (!m_Initialized || width == 0 || height == 0) return;
    m_Width = width;
    m_Height = height;
    CreatePostTargets();
}

// (Re)creates every resolution-dependent target. Unchanged targets are kept.
void Renderer::CreatePostTargets() {
    using RHI::Format;
    const float scale = Clamp(m_Settings.RenderScale, 0.25f, 2.0f);
    m_RenderWidth = std::max(1u, (u32)std::lround(m_Width * scale));
    m_RenderHeight = std::max(1u, (u32)std::lround(m_Height * scale));
    const u32 rw = m_RenderWidth, rh = m_RenderHeight;
    CreateTarget(m_HDR, rw, rh, Format::RGBA16F, Format::D24, m_Samples, "HDR");
    CreateTarget(m_Resolve, rw, rh, Format::RGBA16F, Format::D24, 1, "Resolve");
    CreateTarget(m_AO, rw / 2, rh / 2, Format::RGBA8, Format::None, 1, "SSAO");
    CreateTarget(m_AOBlur, rw / 2, rh / 2, Format::RGBA8, Format::None, 1, "SSAOBlur");
    CreateTarget(m_LDR[0], rw, rh, Format::RGBA8, Format::None, 1, "LDR0");
    CreateTarget(m_LDR[1], rw, rh, Format::RGBA8, Format::None, 1, "LDR1");
    CreateTarget(m_Out, m_Width, m_Height, Format::RGBA8, Format::None, 1, "Output");

    // Bloom chain: half resolution down to ~8 px, at most 6 levels.
    u32 w = rw / 2, h = rh / 2;
    size_t level = 0;
    for (; level < 6 && w >= 8 && h >= 8; ++level, w /= 2, h /= 2) {
        if (level >= m_BloomMips.size()) m_BloomMips.emplace_back();
        CreateTarget(m_BloomMips[level], w, h, Format::RGBA16F, Format::None, 1, "Bloom");
    }
    if (level == 0) { if (m_BloomMips.empty()) m_BloomMips.emplace_back(); CreateTarget(m_BloomMips[0], 1, 1, Format::RGBA16F, Format::None, 1, "Bloom"); level = 1; }
    for (size_t i = level; i < m_BloomMips.size(); ++i) DestroyTarget(m_BloomMips[i]);
    m_BloomMips.resize(level);
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
    m_Device->SetMaxAnisotropy(s.AnisotropyLevel());
    if (rescale) CreatePostTargets();
}

void Renderer::SetMSAA(u32 samples) {
    if (!m_Initialized) return;
    samples = std::max<u32>(1, std::min<u32>(samples, m_Device->GetInfo().MaxSamples));
    if (samples == m_Samples) return;
    m_Samples = samples;
    CreateTarget(m_HDR, m_RenderWidth, m_RenderHeight, RHI::Format::RGBA16F, RHI::Format::D24, m_Samples, "HDR");
    CreatePipelines();   // scene pipelines are created for a sample count
    m_HasPrevFrame = false;
}

// =============================================================================
// Frame
// =============================================================================
void Renderer::BeginFrame(const CameraView& camera, const Environment& env) {
    m_Camera = camera;
    m_Env = env;
    m_DrawList.clear();
    m_PointLights.clear();
    m_Lines.clear();
    m_Stats = {};
    if (m_Initialized && m_ShadowSettings.MapSize != m_ShadowMapSize) {
        m_ShadowMapSize = m_ShadowSettings.MapSize;
        CreateTarget(m_ShadowMap, m_ShadowMapSize, m_ShadowMapSize, RHI::Format::None, RHI::Format::D32F, 1, "ShadowMap");
    }
}

void Renderer::AddPointLight(const PointLight& light) {
    if (m_PointLights.size() < 1024) m_PointLights.push_back(light);
}

void Renderer::Submit(const Ref<Mesh>& mesh, const Ref<Material>& material, const Mat4& transform, const Vec4& tint) {
    if (!mesh) return;
    ++m_Stats.Submitted;
    m_DrawList.push_back({mesh.get(), material.get(), transform, tint, mesh->GetBounds().Transformed(transform)});
}

void Renderer::EndFrame() {
    if (!m_Initialized) return;
    const u32 drawsBefore = m_Device->FrameDrawCalls;

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
    PostPass();
    m_Stats.PointLights = (u32)m_PointLights.size();
    m_Stats.DrawCalls = m_Device->FrameDrawCalls - drawsBefore;
}

RHI::BufferView Renderer::BuildBatches(const std::vector<const DrawItem*>& items, std::vector<Batch>& out) {
    m_InstanceData.clear();
    m_InstanceData.reserve(items.size());
    for (const DrawItem* item : items) {
        if (!out.empty() && out.back().MeshPtr == item->MeshPtr && out.back().MaterialPtr == item->MaterialPtr)
            ++out.back().Count;
        else
            out.push_back({item->MeshPtr, item->MaterialPtr, m_InstanceData.size(), 1});
        m_InstanceData.push_back({item->Transform, item->Tint});
    }
    if (m_InstanceData.empty()) return {};
    return m_Device->Transient(m_InstanceData.data(), m_InstanceData.size() * sizeof(InstanceData), sizeof(InstanceData));
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
    const float half = (float)m_ShadowMapSize * 0.5f;
    const Vec3 origin = (proj * view).TransformPoint(Vec3(0.0f)) * half;
    proj(0, 3) += (std::round(origin.x) - origin.x) / half;
    proj(1, 3) += (std::round(origin.y) - origin.y) / half;
    lightSpace = proj * view;

    const Frustum lightFrustum(lightSpace);
    std::vector<const DrawItem*> casters;
    casters.reserve(m_DrawList.size());
    for (const DrawItem& item : m_DrawList) {
        if (item.MaterialPtr && (!item.MaterialPtr->CastShadows || item.MaterialPtr->Unlit)) continue;
        if (!lightFrustum.Intersects(item.WorldBounds)) continue;
        casters.push_back(&item);
    }
    std::vector<Batch> batches;
    const RHI::BufferView instances = BuildBatches(casters, batches);

    RHI::ClearValues clear;
    clear.Depth = true;
    m_Device->BeginPass(m_ShadowMap.RT, clear);
    m_Device->SetPipeline(m_P.Shadow);
    ShaderTypes::MatrixOnly cb{lightSpace};
    m_Device->SetConstants(0, &cb, sizeof(cb));
    for (const Batch& b : batches) {
        b.MeshPtr->Bind(*m_Device);
        m_Device->SetVertexBuffer(1, {instances.Buffer, instances.Offset + b.First * sizeof(InstanceData), sizeof(InstanceData)});
        m_Device->DrawIndexed(b.MeshPtr->GetIndexCount(), b.Count);
    }
    m_Device->EndPass();
}

void Renderer::MainPass(const Mat4& lightSpace, bool shadows) {
    const Mat4 viewProj = m_Camera.ViewProjection();
    const Frustum frustum(viewProj);

    // Pass constants.
    ShaderTypes::LitPass pass{};
    pass.ViewProjection = viewProj;
    pass.LightSpace = lightSpace;
    pass.CameraPos = Vec4(m_Camera.Position, 1.0f);
    pass.SunDirection = Vec4(Normalize(m_Sun.Direction), 0.0f);
    pass.SunColor = Vec4(m_Sun.Color * m_Sun.Intensity, 0.0f);
    pass.AmbientSky = Vec4(m_Env.SkyZenith * m_Env.AmbientIntensity * 1.2f, 0.0f);
    pass.AmbientGround = Vec4(m_Env.SkyGround * m_Env.AmbientIntensity, 0.0f);
    pass.FogColorDensity = Vec4(m_Env.SkyHorizon, m_Env.FogDensity);
    pass.SkyZenith = Vec4(m_Env.SkyZenith, 0.0f);
    pass.SkyHorizon = Vec4(m_Env.SkyHorizon, 0.0f);
    pass.SkyGround = Vec4(m_Env.SkyGround, 0.0f);
    pass.Params = Vec4(m_Env.ReflectionIntensity, 1.0f / (float)m_ShadowMapSize, shadows ? 1.0f : 0.0f,
                       (float)Clamp(m_Settings.ShadowSoftness, 0, 2));
    pass.LightCount = Vec4((float)m_PointLights.size(), 0, 0, 0);
    for (size_t i = 0; i < m_PointLights.size(); ++i) {
        const PointLight& l = m_PointLights[i];
        pass.Lights[i].PositionRange = Vec4(l.Position, l.Range);
        pass.Lights[i].Color = Vec4(l.Color * l.Intensity, 0.0f);
    }

    // Frustum + draw distance culling on the job system; compaction keeps draw order.
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
    const RHI::BufferView instances = BuildBatches(visible, batches);

    RHI::ClearValues clear;
    clear.Color = clear.Depth = true;
    clear.Rgba[0] = m_Env.SkyHorizon.x;
    clear.Rgba[1] = m_Env.SkyHorizon.y;
    clear.Rgba[2] = m_Env.SkyHorizon.z;
    m_Device->BeginPass(m_HDR.RT, clear);
    m_Device->SetPipeline(m_P.Lit);
    m_Device->SetConstants(0, &pass, sizeof(pass));
    m_Device->SetTexture(1, m_ShadowMap.Depth, RHI::Sampler::ShadowCompare);   // always a depth texture

    static const Material s_DefaultMaterial;
    const Material* bound = nullptr;
    for (const Batch& b : batches) {
        const Material* mat = b.MaterialPtr ? b.MaterialPtr : &s_DefaultMaterial;
        if (mat != bound) {
            bound = mat;
            ShaderTypes::Material m;
            m.Albedo = mat->Albedo;
            m.EmissiveMetallic = Vec4(mat->Emissive, mat->Metallic);
            m.Params = Vec4(mat->Roughness, mat->Unlit ? 1.0f : 0.0f, mat->Tiling.x, mat->Tiling.y);
            m_Device->SetConstants(1, &m, sizeof(m));
            const Texture2D* tex = mat->AlbedoMap ? mat->AlbedoMap.get() : m_White.get();
            m_Device->SetTexture(0, tex->GetHandle(), tex->GetSampler());
        }
        b.MeshPtr->Bind(*m_Device);
        m_Device->SetVertexBuffer(1, {instances.Buffer, instances.Offset + b.First * sizeof(InstanceData), sizeof(InstanceData)});
        m_Device->DrawIndexed(b.MeshPtr->GetIndexCount(), b.Count);
        m_Stats.Instances += b.Count;
        m_Stats.Triangles += b.MeshPtr->GetIndexCount() / 3 * b.Count;
    }

    SkyPass();
    LinePass();
    m_Device->EndPass();
}

void Renderer::SkyPass() {
    ShaderTypes::Sky cb;
    cb.InvViewProjection = Inverse(m_Camera.ViewProjection());
    cb.CameraPos = Vec4(m_Camera.Position, 1.0f);
    cb.SunDirection = Vec4(Normalize(m_Sun.Direction), 0.0f);
    cb.SunColor = Vec4(m_Sun.Color * m_Sun.Intensity, 0.0f);
    cb.Zenith = Vec4(m_Env.SkyZenith, 0.0f);
    cb.Horizon = Vec4(m_Env.SkyHorizon, 0.0f);
    cb.Ground = Vec4(m_Env.SkyGround, 0.0f);
    m_Device->SetPipeline(m_P.Sky);
    m_Device->SetConstants(0, &cb, sizeof(cb));
    m_Device->Draw(3);
}

void Renderer::LinePass() {
    if (m_Lines.empty()) return;
    const RHI::BufferView v = m_Device->Transient(m_Lines.data(), m_Lines.size() * sizeof(DebugVertex), sizeof(DebugVertex));
    ShaderTypes::MatrixOnly cb{m_Camera.ViewProjection()};
    m_Device->SetPipeline(m_P.Line);
    m_Device->SetConstants(0, &cb, sizeof(cb));
    m_Device->SetVertexBuffer(0, v);
    m_Device->Draw((u32)m_Lines.size());
}

// =============================================================================
// Post processing
// =============================================================================
void Renderer::FullscreenPass(RHI::PipelineHandle pipeline) {
    m_Device->SetPipeline(pipeline);
    m_Device->Draw(3);
}

void Renderer::SSAOPass() {
    ShaderTypes::SSAO cb;
    cb.Projection = m_Camera.Projection;
    cb.InvProjection = Inverse(m_Camera.Projection);
    cb.Params = Vec4(1.0f / (float)m_RenderWidth, 1.0f / (float)m_RenderHeight, m_Settings.AORadius, 0.0f);
    m_Device->BeginPass(m_AO.RT);
    m_Device->SetPipeline(m_P.SSAO);
    m_Device->SetConstants(0, &cb, sizeof(cb));
    m_Device->SetTexture(0, m_Resolve.Depth, RHI::Sampler::PointClamp);
    m_Device->Draw(3);
    m_Device->EndPass();

    ShaderTypes::Texel blur{Vec4(1.0f / (float)m_AO.Width, 1.0f / (float)m_AO.Height, 0, 0)};
    m_Device->BeginPass(m_AOBlur.RT);
    m_Device->SetPipeline(m_P.Blur);
    m_Device->SetConstants(0, &blur, sizeof(blur));
    m_Device->SetTexture(0, m_AO.Color, RHI::Sampler::LinearClamp);
    m_Device->Draw(3);
    m_Device->EndPass();
}

void Renderer::BloomPass() {
    // Downsample chain (first pass thresholds the HDR scene).
    RHI::TextureHandle src = m_Resolve.Color;
    Vec2 texel{1.0f / (float)m_RenderWidth, 1.0f / (float)m_RenderHeight};
    for (size_t i = 0; i < m_BloomMips.size(); ++i) {
        const Target& dst = m_BloomMips[i];
        ShaderTypes::BloomDown cb{Vec4(texel.x, texel.y, i == 0 ? 1.0f : 0.0f, m_Settings.BloomThreshold)};
        m_Device->BeginPass(dst.RT);
        m_Device->SetPipeline(m_P.BloomDown);
        m_Device->SetConstants(0, &cb, sizeof(cb));
        m_Device->SetTexture(0, src, RHI::Sampler::LinearClamp);
        m_Device->Draw(3);
        m_Device->EndPass();
        src = dst.Color;
        texel = {1.0f / (float)dst.Width, 1.0f / (float)dst.Height};
    }
    // Upsample back up, adding each level onto the next larger one.
    for (size_t i = m_BloomMips.size() - 1; i > 0; --i) {
        const Target& s = m_BloomMips[i];
        ShaderTypes::Texel cb{Vec4(1.0f / (float)s.Width, 1.0f / (float)s.Height, 0, 0)};
        m_Device->BeginPass(m_BloomMips[i - 1].RT);
        m_Device->SetPipeline(m_P.BloomUp);
        m_Device->SetConstants(0, &cb, sizeof(cb));
        m_Device->SetTexture(0, s.Color, RHI::Sampler::LinearClamp);
        m_Device->Draw(3);
        m_Device->EndPass();
    }
}

void Renderer::PostPass() {
    // Resolve MSAA -> single-sample HDR color + depth textures.
    m_Device->Resolve(m_HDR.RT, m_Resolve.RT);

    const GraphicsSettings& gs = m_Settings;
    if (gs.AmbientOcclusion) SSAOPass();
    const bool bloom = gs.Bloom && gs.BloomIntensity > 0.0f;
    if (bloom) BloomPass();

    const bool fxaa = gs.FXAA();
    const bool upscale = m_RenderWidth != m_Width || m_RenderHeight != m_Height;
    const bool sharpen = gs.Sharpness > 0.001f;

    // ---- composite: HDR -> LDR ----
    ShaderTypes::Composite c;
    const Mat4 viewProj = m_Camera.ViewProjection();
    c.InvViewProj = Inverse(viewProj);
    c.PrevViewProj = m_PrevViewProj;
    c.A = Vec4(m_Env.Exposure * gs.Exposure, (float)gs.Tonemapper, gs.Contrast, gs.Saturation);
    c.B = Vec4(gs.Temperature, gs.Gamma, gs.Vignette, gs.FilmGrain);
    c.C = Vec4(gs.ChromaticAberration, bloom ? gs.BloomIntensity : 0.0f, gs.AmbientOcclusion ? gs.AOIntensity : 0.0f,
               gs.MotionBlur && m_HasPrevFrame ? gs.MotionBlurStrength : 0.0f);
    c.D = Vec4(m_Time, 0, 0, 0);
    m_Device->BeginPass(fxaa || upscale || sharpen ? m_LDR[0].RT : RHI::RenderTargetHandle{});
    m_Device->SetPipeline(m_P.Composite);
    m_Device->SetConstants(0, &c, sizeof(c));
    m_Device->SetTexture(0, m_Resolve.Color, RHI::Sampler::LinearClamp);
    m_Device->SetTexture(1, m_Resolve.Depth, RHI::Sampler::PointClamp);
    m_Device->SetTexture(2, gs.AmbientOcclusion ? m_AOBlur.Color : m_White->GetHandle(), RHI::Sampler::LinearClamp);
    m_Device->SetTexture(3, bloom ? m_BloomMips[0].Color : m_Black->GetHandle(), RHI::Sampler::LinearClamp);
    m_Device->Draw(3);
    m_Device->EndPass();

    // ---- LDR chain: FXAA (internal) -> upscale (output) -> sharpen (output) ----
    const Target* current = &m_LDR[0];
    if (fxaa) {
        const bool last = !upscale && !sharpen;
        ShaderTypes::Texel cb{Vec4(1.0f / (float)m_RenderWidth, 1.0f / (float)m_RenderHeight, 0, 0)};
        m_Device->BeginPass(last ? RHI::RenderTargetHandle{} : m_LDR[1].RT);
        m_Device->SetPipeline(m_P.FXAA);
        m_Device->SetConstants(0, &cb, sizeof(cb));
        m_Device->SetTexture(0, current->Color, RHI::Sampler::LinearClamp);
        m_Device->Draw(3);
        m_Device->EndPass();
        current = &m_LDR[1];
    }
    if (upscale) {
        // Supersampling (scale > 1) always uses the bilinear downsample.
        const bool down = m_RenderWidth > m_Width;
        const float mode = down || gs.Upscaler == GraphicsSettings::UpscaleBilinear ? 0.0f : 1.0f;
        ShaderTypes::Upscale cb{Vec4((float)m_RenderWidth, (float)m_RenderHeight, mode, 0)};
        m_Device->BeginPass(sharpen ? m_Out.RT : RHI::RenderTargetHandle{});
        m_Device->SetPipeline(m_P.Upscale);
        m_Device->SetConstants(0, &cb, sizeof(cb));
        m_Device->SetTexture(0, current->Color, RHI::Sampler::LinearClamp);
        m_Device->Draw(3);
        m_Device->EndPass();
        current = &m_Out;
    }
    if (sharpen) {
        ShaderTypes::Sharpen cb{Vec4(1.0f / (float)current->Width, 1.0f / (float)current->Height, gs.Sharpness, 0)};
        m_Device->BeginPass({});
        m_Device->SetPipeline(m_P.Sharpen);
        m_Device->SetConstants(0, &cb, sizeof(cb));
        m_Device->SetTexture(0, current->Color, RHI::Sampler::LinearClamp);
        m_Device->Draw(3);
        m_Device->EndPass();
    }

    m_PrevViewProj = viewProj;
    m_HasPrevFrame = true;
    m_Time += 1.0f / 60.0f;
    if (m_Time > 1000.0f) m_Time = 0.0f;
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

} // namespace Lapis
