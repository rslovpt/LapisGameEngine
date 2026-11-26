#pragma once
// Batched 2D renderer for UI: solid rectangles and text in window pixels
// (origin top-left). Everything is drawn in one draw call per frame on top
// of the 3D image.

#include <string_view>
#include <vector>

#include "Lapis/Core/Base.h"
#include "Lapis/Math/Math.h"

namespace Lapis {

class Shader;
class VertexArray;
class VertexBuffer;

class UIRenderer : NonCopyable {
public:
    UIRenderer();
    ~UIRenderer();

    bool Init();
    void Shutdown();
    bool IsInitialized() const { return m_FontTexture != 0; }

    void Begin(u32 width, u32 height);
    void End(u32 targetFBO);       // draws everything recorded since Begin

    void Rect(float x, float y, float w, float h, const Vec4& color);
    void RectOutline(float x, float y, float w, float h, const Vec4& color, float thickness = 1.0f);
    // Returns the x position after the text. y is the top of the line.
    float Text(float x, float y, std::string_view text, const Vec4& color, float scale = 1.0f);

    float TextWidth(std::string_view text, float scale = 1.0f) const;
    float LineHeight(float scale = 1.0f) const;

    // Reserves a rectangle whose geometry is set later (backgrounds whose
    // size is only known after their contents were laid out).
    size_t ReserveRect();
    void   SetRect(size_t handle, float x, float y, float w, float h, const Vec4& color);

    u32 GetWidth() const { return m_Width; }
    u32 GetHeight() const { return m_Height; }

private:
    struct UIVertex { float X, Y, U, V; float R, G, B, A; };
    void Quad(size_t at, float x, float y, float w, float h, float u0, float v0, float u1, float v1, const Vec4& c);

    std::vector<UIVertex> m_Vertices;
    Scope<Shader>       m_Shader;
    Scope<VertexArray>  m_VAO;
    Ref<VertexBuffer>   m_VBO;
    u32 m_FontTexture = 0;
    u32 m_Width = 1, m_Height = 1;
};

} // namespace Lapis
