#include "Lapis/Renderer/Renderer.h"

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
    m_PostShader   = CreateRef<Shader>("PostProcess", FullscreenVertex, PostFragment);
    m_LineShader   = CreateRef<Shader>("DebugLine", LineVertex, LineFragment);
    const Shader* shaders[] = {m_LitShader.get(), m_ShadowShader.get(), m_SkyShader.get(), m_PostShader.get(), m_LineShader.get()};
    for (const Shader* s : shaders)
        if (!s->IsValid()) { LAPIS_FATAL("Renderer: built-in shader '{}' failed", s->GetName()); return false; }

    FramebufferSpec hdr;
    hdr.Width = m_Width;
    hdr.Height = m_Height;
    hdr.Samples = m_Samples;
    hdr.Color = ColorFormat::RGBA16F;
    hdr.Depth = DepthFormat::Depth24;
    m_HDRBuffer = CreateScope<Framebuffer>(hdr);

    FramebufferSpec resolve = hdr;
    resolve.Samples = 1;
    resolve.Depth = DepthFormat::None;
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

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    m_Initialized = true;
    LAPIS_INFO("Renderer initialized ({}x{}, {}x MSAA, shadow map {})", m_Width, m_Height, m_Samples, m_ShadowSettings.MapSize);
    return true;
}

void Renderer::Shutdown() {
    m_LitShader.reset(); m_ShadowShader.reset(); m_SkyShader.reset(); m_PostShader.reset(); m_LineShader.reset();
    m_HDRBuffer.reset(); m_ResolveBuffer.reset(); m_ShadowMap.reset();
    m_EmptyVAO.reset(); m_LineVAO.reset(); m_LineVBO.reset(); m_White.reset();
    m_DrawList.clear();
    m_Initialized = false;
}

void Renderer::Resize(u32 width, u32 height) {
    if (!m_Initialized || width == 0 || height == 0) return;
    m_Width = width;
    m_Height = height;
    m_HDRBuffer->Resize(width, height);
    m_ResolveBuffer->Resize(width, height);
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
    if (m_PointLights.size() < MaxPointLights) m_PointLights.push_back(light);
}

void Renderer::Submit(const Ref<Mesh>& mesh, const Ref<Material>& material, const Mat4& transform) {
    if (!mesh) return;
    ++m_Stats.Submitted;
    m_DrawList.push_back({mesh.get(), material.get(), transform, mesh->GetBounds().Transformed(transform)});
}

void Renderer::EndFrame(u32 targetFBO) {
    if (!m_Initialized) return;

    // Group by material to reduce state changes.
    std::sort(m_DrawList.begin(), m_DrawList.end(),
              [](const DrawItem& a, const DrawItem& b) { return a.MaterialPtr < b.MaterialPtr; });

    Mat4 lightSpace(1.0f);
    const bool shadows = m_Sun.CastShadows;
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
    for (const DrawItem& item : m_DrawList) {
        if (item.MaterialPtr && (!item.MaterialPtr->CastShadows || item.MaterialPtr->Unlit)) continue;
        if (!lightFrustum.Intersects(item.WorldBounds)) continue;
        m_ShadowShader->SetMat4("u_Model", item.Transform);
        item.MeshPtr->Draw();
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
    const Material* bound = nullptr;
    for (const DrawItem& item : m_DrawList) {
        if (!frustum.Intersects(item.WorldBounds)) { ++m_Stats.Culled; continue; }

        const Material* mat = item.MaterialPtr ? item.MaterialPtr : &s_DefaultMaterial;
        if (mat != bound) {
            bound = mat;
            s.SetVec4("u_Albedo", mat->Albedo);
            s.SetVec3("u_Emissive", mat->Emissive);
            s.SetFloat("u_Metallic", mat->Metallic);
            s.SetFloat("u_Roughness", mat->Roughness);
            s.SetInt("u_Unlit", mat->Unlit ? 1 : 0);
            s.SetVec2("u_Tiling", mat->Tiling);
            (mat->AlbedoMap ? mat->AlbedoMap : m_White)->Bind(0);
        }
        s.SetMat4("u_Model", item.Transform);
        s.SetMat4("u_NormalMatrix", Transpose(Inverse(item.Transform)));
        item.MeshPtr->Draw();
        ++m_Stats.DrawCalls;
        m_Stats.Triangles += item.MeshPtr->GetIndexCount() / 3;
    }
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

void Renderer::PostPass(u32 targetFBO) {
    // Resolve MSAA -> single-sample HDR texture.
    m_HDRBuffer->BlitTo(m_ResolveBuffer.get(), true, false);

    Framebuffer::BindTarget(targetFBO, m_Width, m_Height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    m_PostShader->Bind();
    m_PostShader->SetInt("u_HDR", 0);
    m_PostShader->SetFloat("u_Exposure", m_Env.Exposure);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ResolveBuffer->GetColorTexture());
    m_EmptyVAO->Bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++m_Stats.DrawCalls;
    glEnable(GL_DEPTH_TEST);
    VertexArray::Unbind();
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
