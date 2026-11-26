#include "Lapis/UI/UIRenderer.h"

#include <cstring>

#include "Lapis/Core/Log.h"
#include "Lapis/Renderer/Buffer.h"
#include "Lapis/Renderer/GL.h"
#include "Lapis/Renderer/Shader.h"
#include "Lapis/UI/FontData.h"

namespace Lapis {

namespace {
    constexpr float FontPixels = 15.0f;   // UI units for text scale 1.0

    const char* UIVertexSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec4 a_Rect;     // local x, y, width, height (pixels)
layout(location = 4) in vec4 a_Params;   // radius, mode, param, unused
uniform vec2 u_Screen;
out vec2 v_UV;
out vec4 v_Color;
out vec4 v_Rect;
out vec4 v_Params;
void main() {
    v_UV = a_UV;
    v_Color = a_Color;
    v_Rect = a_Rect;
    v_Params = a_Params;
    gl_Position = vec4(a_Pos.x / u_Screen.x * 2.0 - 1.0, 1.0 - a_Pos.y / u_Screen.y * 2.0, 0.0, 1.0);
}
)";
    const char* UIFragmentSrc = R"(
#version 330 core
in vec2 v_UV;
in vec4 v_Color;
in vec4 v_Rect;
in vec4 v_Params;
uniform sampler2D u_Font;
out vec4 o_Color;

// Signed distance to a rounded box centered at the origin.
float RoundedBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    float mode = v_Params.y;
    float alpha;
    if (mode < 0.5) {
        // SDF text: 0.5 = glyph edge. Smoothing width follows screen-space derivatives.
        float d = texture(u_Font, v_UV).r;
        float w = max(fwidth(d) * 0.75, 0.0001);
        alpha = smoothstep(0.5 - w, 0.5 + w, d);
    } else {
        vec2 halfSize = v_Rect.zw * 0.5;
        float r = min(v_Params.x, min(halfSize.x, halfSize.y));
        float dist = RoundedBox(v_Rect.xy - halfSize, halfSize, r);
        if (mode < 1.5) {
            alpha = clamp(0.5 - dist, 0.0, 1.0);                                   // fill
        } else if (mode < 2.5) {
            float t = v_Params.z;                                                  // outline
            alpha = clamp(0.5 - dist, 0.0, 1.0) * clamp(dist + t + 0.5, 0.0, 1.0);
        } else {
            float soft = max(v_Params.z, 0.001);                                   // shadow
            alpha = 1.0 - smoothstep(-soft, soft, dist);
        }
    }
    o_Color = vec4(v_Color.rgb, v_Color.a * alpha);
}
)";

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

bool UIRenderer::Init() {
    m_Shader = CreateScope<Shader>("UI", UIVertexSrc, UIFragmentSrc);
    if (!m_Shader->IsValid()) return false;

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

    glGenTextures(1, &m_FontTexture);
    glBindTexture(GL_TEXTURE_2D, m_FontTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FontData::AtlasWidth, FontData::AtlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_VBO = CreateRef<VertexBuffer>(nullptr, sizeof(UIVertex) * 6 * 1024, BufferUsage::Stream);
    m_VAO = CreateScope<VertexArray>();
    m_VAO->AddVertexBuffer(m_VBO, {{AttribType::Float2}, {AttribType::Float2}, {AttribType::Float4},
                                   {AttribType::Float4}, {AttribType::Float4}});
    return true;
}

void UIRenderer::Shutdown() {
    if (m_FontTexture) { glDeleteTextures(1, &m_FontTexture); m_FontTexture = 0; }
    m_Shader.reset();
    m_VAO.reset();
    m_VBO.reset();
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

float UIRenderer::Text(float x, float y, std::string_view text, const Vec4& color, float scale) {
    const float k = scale * FontPixels / FontData::BaseSize;   // UI units per base pixel
    const float startX = x;
    for (char ch : text) {
        if (ch == '\n') { x = startX; y += LineHeight(scale); continue; }
        if (ch < 32 || ch > 126) ch = '?';
        const FontData::Glyph& g = FontData::Glyphs[ch - 32];
        if (g.W > 0 && g.H > 0) {
            const float u0 = float(g.X) / FontData::AtlasWidth, v0 = float(g.Y) / FontData::AtlasHeight;
            const float u1 = float(g.X + g.W) / FontData::AtlasWidth, v1 = float(g.Y + g.H) / FontData::AtlasHeight;
            Quad(size_t(-1), x + g.OffX * k, y + g.OffY * k, g.W * k, g.H * k, u0, v0, u1, v1, color, 0.0f, 0.0f, 0.0f);
        }
        x += g.Advance * k;
    }
    return x;
}

float UIRenderer::TextWidth(std::string_view text, float scale) const {
    const float k = scale * FontPixels / FontData::BaseSize;
    float w = 0, best = 0;
    for (char ch : text) {
        if (ch == '\n') { best = std::max(best, w); w = 0; continue; }
        if (ch < 32 || ch > 126) ch = '?';
        w += FontData::Glyphs[ch - 32].Advance * k;
    }
    return std::max(best, w);
}

float UIRenderer::LineHeight(float scale) const { return FontData::LineHeight * scale * FontPixels / FontData::BaseSize; }

void UIRenderer::End(u32 targetFBO) {
    if (m_Vertices.empty() || !m_Shader) return;
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_Shader->Bind();
    m_Shader->SetVec2("u_Screen", Vec2((float)m_Width, (float)m_Height));
    m_Shader->SetInt("u_Font", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_FontTexture);
    m_VBO->SetData(m_Vertices.data(), m_Vertices.size() * sizeof(UIVertex));
    m_VAO->Bind();
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_Vertices.size());
    VertexArray::Unbind();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

} // namespace Lapis
