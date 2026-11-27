#include "Lapis/UI/UIRenderer.h"

#include <cstring>

#include "Lapis/Core/Log.h"
#include "Lapis/UI/FontData.h"

namespace Lapis {

namespace {
    constexpr float FontPixels = 15.0f;   // UI units for text scale 1.0

    int Base64Value(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }
}

UIRenderer::UIRenderer() = default;
UIRenderer::~UIRenderer() { Shutdown(); }

bool UIRenderer::Init(RHI::Device& device) {
    m_Device = &device;
    RHI::PipelineDesc pd;
    pd.Shader = "UI";
    pd.Name = "UI";
    pd.Attributes = {{0, 0, RHI::AttribFormat::Float2, 0},  {1, 0, RHI::AttribFormat::Float2, 8}, {2, 0, RHI::AttribFormat::Float4, 16},
                     {3, 0, RHI::AttribFormat::Float4, 32}, {4, 0, RHI::AttribFormat::Float4, 48}};
    pd.Strides[0] = sizeof(UIVertex);
    pd.Blend = RHI::BlendMode::Alpha;
    pd.ColorFormat = RHI::Format::RGBA8;
    m_Pipeline = device.CreatePipeline(pd);
    if (!m_Pipeline) return false;

    // Decode the baked SDF atlas (base64).
    std::vector<u8> pixels;
    pixels.reserve(size_t(FontData::AtlasWidth) * FontData::AtlasHeight);
    u32 acc = 0;
    int bits = 0;
    for (int line = 0; line < FontData::AtlasBase64Lines; ++line) {
        for (const char* p = FontData::AtlasBase64[line]; *p; ++p) {
            const int v = Base64Value(*p);
            if (v < 0) continue;
            acc = (acc << 6) | (u32)v;
            bits += 6;
            if (bits >= 8) { bits -= 8; pixels.push_back(u8((acc >> bits) & 0xFF)); }
        }
    }
    pixels.resize(size_t(FontData::AtlasWidth) * FontData::AtlasHeight, 0);

    RHI::TextureDesc td;
    td.Width = FontData::AtlasWidth;
    td.Height = FontData::AtlasHeight;
    td.Fmt = RHI::Format::R8;
    td.Data = pixels.data();
    td.Name = "FontAtlas";
    m_FontTexture = device.CreateTexture(td);
    return true;
}

void UIRenderer::Shutdown() {
    if (m_Device) {
        if (m_FontTexture) m_Device->Destroy(m_FontTexture);
        if (m_Pipeline) m_Device->Destroy(m_Pipeline);
    }
    m_FontTexture = {};
    m_Pipeline = {};
    m_Device = nullptr;
}

void UIRenderer::Begin(u32 width, u32 height, float scale) {
    m_Width = width ? width : 1;
    m_Height = height ? height : 1;
    m_Scale = scale > 0.1f ? scale : 1.0f;
    m_LogicalW = m_Width / m_Scale;
    m_LogicalH = m_Height / m_Scale;
    m_Vertices.clear();
}

void UIRenderer::Quad(size_t at, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                      const Vec4& c, float mode, float radius, float param, float grow) {
    // Work in pixels; `grow` enlarges the quad (shadows/AA) without moving the shape.
    const float s = m_Scale;
    const float px = x * s - grow, py = y * s - grow, pw = w * s + 2 * grow, ph = h * s + 2 * grow;
    const float rw = w * s, rh = h * s;
    auto v = [&](float qx, float qy, float u, float vv) {
        return UIVertex{qx, qy, u, vv, c.x, c.y, c.z, c.w, qx - x * s, qy - y * s, rw, rh, radius * s, mode, param * s, 0};
    };
    const UIVertex q[6] = {v(px, py, u0, v0), v(px + pw, py, u1, v0), v(px + pw, py + ph, u1, v1),
                           v(px, py, u0, v0), v(px + pw, py + ph, u1, v1), v(px, py + ph, u0, v1)};
    if (at == size_t(-1)) m_Vertices.insert(m_Vertices.end(), q, q + 6);
    else std::memcpy(&m_Vertices[at], q, sizeof(q));
}

namespace {
    void WhiteUV(float& u, float& v) {
        u = (FontData::WhiteX + 0.5f) / FontData::AtlasWidth;
        v = (FontData::WhiteY + 0.5f) / FontData::AtlasHeight;
    }
}

void UIRenderer::Rect(float x, float y, float w, float h, const Vec4& color, float radius) {
    float u, v; WhiteUV(u, v);
    Quad(size_t(-1), x, y, w, h, u, v, u, v, color, 1.0f, radius, 0.0f);
}

void UIRenderer::RectOutline(float x, float y, float w, float h, const Vec4& color, float thickness, float radius) {
    float u, v; WhiteUV(u, v);
    Quad(size_t(-1), x, y, w, h, u, v, u, v, color, 2.0f, radius, thickness);
}

void UIRenderer::Shadow(float x, float y, float w, float h, float radius, float softness, const Vec4& color) {
    float u, v; WhiteUV(u, v);
    Quad(size_t(-1), x, y, w, h, u, v, u, v, color, 3.0f, radius, softness, softness * 2.0f * m_Scale);
}

size_t UIRenderer::ReserveRect() {
    const size_t at = m_Vertices.size();
    m_Vertices.resize(at + 6, UIVertex{});
    return at;
}

void UIRenderer::SetRect(size_t handle, float x, float y, float w, float h, const Vec4& color, float radius) {
    if (handle + 6 > m_Vertices.size()) return;
    float u, v; WhiteUV(u, v);
    Quad(handle, x, y, w, h, u, v, u, v, color, 1.0f, radius, 0.0f);
}

void UIRenderer::SetShadow(size_t handle, float x, float y, float w, float h, float radius, float softness, const Vec4& color) {
    if (handle + 6 > m_Vertices.size()) return;
    float u, v; WhiteUV(u, v);
    Quad(handle, x, y, w, h, u, v, u, v, color, 3.0f, radius, softness, softness * 2.0f * m_Scale);
}

float UIRenderer::Text(float x, float y, std::string_view text, const Vec4& color, float scale, int font) {
    font = Clamp(font, 0, FontData::FontCount - 1);
    const float k = scale * FontPixels / FontData::BaseSize;   // UI units per base pixel
    const float startX = x;
    for (char ch : text) {
        if (ch == '\n') { x = startX; y += LineHeight(scale, font); continue; }
        if (ch < 32 || ch > 126) ch = '?';
        const FontData::Glyph& g = FontData::Glyphs[font][ch - 32];
        if (g.W > 0 && g.H > 0) {
            const float u0 = float(g.X) / FontData::AtlasWidth, v0 = float(g.Y) / FontData::AtlasHeight;
            const float u1 = float(g.X + g.W) / FontData::AtlasWidth, v1 = float(g.Y + g.H) / FontData::AtlasHeight;
            Quad(size_t(-1), x + g.OffX * k, y + g.OffY * k, g.W * k, g.H * k, u0, v0, u1, v1, color, 0.0f, 0.0f, 0.0f);
        }
        x += g.Advance * k;
    }
    return x;
}

float UIRenderer::TextWidth(std::string_view text, float scale, int font) const {
    font = Clamp(font, 0, FontData::FontCount - 1);
    const float k = scale * FontPixels / FontData::BaseSize;
    float w = 0, best = 0;
    for (char ch : text) {
        if (ch == '\n') { best = std::max(best, w); w = 0; continue; }
        if (ch < 32 || ch > 126) ch = '?';
        w += FontData::Glyphs[font][ch - 32].Advance * k;
    }
    return std::max(best, w);
}

float UIRenderer::LineHeight(float scale, int font) const {
    return FontData::LineHeight[Clamp(font, 0, FontData::FontCount - 1)] * scale * FontPixels / FontData::BaseSize;
}

void UIRenderer::End(u32 /*targetFBO*/) {
    if (m_Vertices.empty() || !m_Pipeline) return;
    // Draws on top of the frame in the window (no clear).
    m_Device->BeginPass({});
    m_Device->SetPipeline(m_Pipeline);
    const Vec4 screen((float)m_Width, (float)m_Height, 0.0f, 0.0f);
    m_Device->SetConstants(0, &screen, sizeof(screen));
    m_Device->SetTexture(0, m_FontTexture, RHI::Sampler::LinearClamp);
    m_Device->SetVertexBuffer(0, m_Device->Transient(m_Vertices.data(), m_Vertices.size() * sizeof(UIVertex), sizeof(UIVertex)));
    m_Device->Draw((u32)m_Vertices.size());
    m_Device->EndPass();
}

} // namespace Lapis
