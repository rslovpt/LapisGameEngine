#include "Lapis/UI/UIRenderer.h"

#include <cstring>

#include "Lapis/Core/Log.h"
#include "Lapis/Renderer/Buffer.h"
#include "Lapis/Renderer/GL.h"
#include "Lapis/Renderer/Shader.h"
#include "Lapis/UI/FontData.h"

namespace Lapis {

namespace {
    const char* UIVertexSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_Pos;
layout(location = 1) in vec2 a_UV;
layout(location = 2) in vec4 a_Color;
uniform vec2 u_Screen;
out vec2 v_UV;
out vec4 v_Color;
void main() {
    v_UV = a_UV;
    v_Color = a_Color;
    gl_Position = vec4(a_Pos.x / u_Screen.x * 2.0 - 1.0, 1.0 - a_Pos.y / u_Screen.y * 2.0, 0.0, 1.0);
}
)";
    const char* UIFragmentSrc = R"(
#version 330 core
in vec2 v_UV;
in vec4 v_Color;
uniform sampler2D u_Font;
out vec4 o_Color;
void main() { o_Color = vec4(v_Color.rgb, v_Color.a * texture(u_Font, v_UV).r); }
)";

    int HexValue(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }
}

UIRenderer::UIRenderer() = default;
UIRenderer::~UIRenderer() { Shutdown(); }

bool UIRenderer::Init() {
    m_Shader = CreateScope<Shader>("UI", UIVertexSrc, UIFragmentSrc);
    if (!m_Shader->IsValid()) return false;

    // Decode the baked 4-bit font atlas.
    std::vector<u8> pixels(size_t(FontData::AtlasWidth) * FontData::AtlasHeight, 0);
    size_t i = 0;
    for (int line = 0; line < FontData::PixelLines; ++line)
        for (const char* p = FontData::Pixels[line]; *p && i < pixels.size(); ++p) pixels[i++] = u8(HexValue(*p) * 17);

    glGenTextures(1, &m_FontTexture);
    glBindTexture(GL_TEXTURE_2D, m_FontTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FontData::AtlasWidth, FontData::AtlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_VBO = CreateRef<VertexBuffer>(nullptr, sizeof(UIVertex) * 6 * 512, BufferUsage::Stream);
    m_VAO = CreateScope<VertexArray>();
    m_VAO->AddVertexBuffer(m_VBO, {{AttribType::Float2}, {AttribType::Float2}, {AttribType::Float4}});
    return true;
}

void UIRenderer::Shutdown() {
    if (m_FontTexture) { glDeleteTextures(1, &m_FontTexture); m_FontTexture = 0; }
    m_Shader.reset();
    m_VAO.reset();
    m_VBO.reset();
}

void UIRenderer::Begin(u32 width, u32 height) {
    m_Width = width ? width : 1;
    m_Height = height ? height : 1;
    m_Vertices.clear();
}

void UIRenderer::Quad(size_t at, float x, float y, float w, float h, float u0, float v0, float u1, float v1, const Vec4& c) {
    const UIVertex q[6] = {
        {x, y, u0, v0, c.x, c.y, c.z, c.w},         {x + w, y, u1, v0, c.x, c.y, c.z, c.w},
        {x + w, y + h, u1, v1, c.x, c.y, c.z, c.w}, {x, y, u0, v0, c.x, c.y, c.z, c.w},
        {x + w, y + h, u1, v1, c.x, c.y, c.z, c.w}, {x, y + h, u0, v1, c.x, c.y, c.z, c.w}};
    if (at == size_t(-1)) m_Vertices.insert(m_Vertices.end(), q, q + 6);
    else std::memcpy(&m_Vertices[at], q, sizeof(q));
}

void UIRenderer::Rect(float x, float y, float w, float h, const Vec4& color) {
    const float u = (FontData::WhiteX + 1.0f) / FontData::AtlasWidth, v = (FontData::WhiteY + 1.0f) / FontData::AtlasHeight;
    Quad(size_t(-1), x, y, w, h, u, v, u, v, color);
}

void UIRenderer::RectOutline(float x, float y, float w, float h, const Vec4& c, float t) {
    Rect(x, y, w, t, c);
    Rect(x, y + h - t, w, t, c);
    Rect(x, y, t, h, c);
    Rect(x + w - t, y, t, h, c);
}

size_t UIRenderer::ReserveRect() {
    const size_t at = m_Vertices.size();
    m_Vertices.resize(at + 6, UIVertex{0, 0, 0, 0, 0, 0, 0, 0});
    return at;
}

void UIRenderer::SetRect(size_t handle, float x, float y, float w, float h, const Vec4& color) {
    if (handle + 6 > m_Vertices.size()) return;
    const float u = (FontData::WhiteX + 1.0f) / FontData::AtlasWidth, v = (FontData::WhiteY + 1.0f) / FontData::AtlasHeight;
    Quad(handle, x, y, w, h, u, v, u, v, color);
}

float UIRenderer::Text(float x, float y, std::string_view text, const Vec4& color, float scale) {
    const float startX = x;
    for (char ch : text) {
        if (ch == '\n') { x = startX; y += LineHeight(scale); continue; }
        if (ch < 32 || ch > 126) ch = '?';
        const FontData::Glyph& g = FontData::Glyphs[ch - 32];
        if (g.W > 0 && g.H > 0) {
            const float u0 = float(g.X) / FontData::AtlasWidth, v0 = float(g.Y) / FontData::AtlasHeight;
            const float u1 = float(g.X + g.W) / FontData::AtlasWidth, v1 = float(g.Y + g.H) / FontData::AtlasHeight;
            Quad(size_t(-1), std::round(x + g.OffX * scale), std::round(y + g.OffY * scale), g.W * scale, g.H * scale, u0, v0, u1, v1, color);
        }
        x += g.Advance * scale;
    }
    return x;
}

float UIRenderer::TextWidth(std::string_view text, float scale) const {
    float w = 0, best = 0;
    for (char ch : text) {
        if (ch == '\n') { best = std::max(best, w); w = 0; continue; }
        if (ch < 32 || ch > 126) ch = '?';
        w += FontData::Glyphs[ch - 32].Advance * scale;
    }
    return std::max(best, w);
}

float UIRenderer::LineHeight(float scale) const { return float(FontData::LineHeight) * scale; }

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
