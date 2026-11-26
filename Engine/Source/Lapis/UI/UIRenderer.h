#pragma once
// Batched 2D renderer for UI: anti-aliased (rounded) rectangles, outlines,
// soft shadows and signed-distance-field text, all in one draw call.
//
// Coordinates are logical UI units (origin top-left). A UI scale (e.g. the
// monitor's DPI scale, 1.5 at 150%) maps them to pixels, so the UI keeps its
// size and stays sharp on high-DPI screens.

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

    // width/height in pixels; scale = pixels per UI unit.
    void Begin(u32 width, u32 height, float scale = 1.0f);
    void End(u32 targetFBO);

    void Rect(float x, float y, float w, float h, const Vec4& color, float radius = 0.0f);
    void RectOutline(float x, float y, float w, float h, const Vec4& color, float thickness = 1.0f, float radius = 0.0f);
    void Shadow(float x, float y, float w, float h, float radius, float softness, const Vec4& color);

    // Default font size is 1.0 = 15 UI units. y is the top of the line.
    // Returns the x position after the text.
    float Text(float x, float y, std::string_view text, const Vec4& color, float scale = 1.0f);
    float TextWidth(std::string_view text, float scale = 1.0f) const;
    float LineHeight(float scale = 1.0f) const;

    // Reserve a filled rounded rect whose geometry is only known later
    // (e.g. a popup background sized after its items were laid out).
    size_t ReserveRect();
    void   SetRect(size_t handle, float x, float y, float w, float h, const Vec4& color, float radius = 0.0f);
    size_t ReserveShadow() { return ReserveRect(); }
    void   SetShadow(size_t handle, float x, float y, float w, float h, float radius, float softness, const Vec4& color);

    float GetWidth() const { return m_LogicalW; }   // logical units
    float GetHeight() const { return m_LogicalH; }
    float GetScale() const { return m_Scale; }

private:
    struct UIVertex {
        float X, Y, U, V;
        float R, G, B, A;
        float LocalX, LocalY, RectW, RectH;   // pixel position inside the rect + rect size (pixels)
        float Radius, Mode, Param, Pad;       // Mode: 0 text, 1 fill, 2 outline (Param = thickness), 3 shadow (Param = softness)
    };
    void Quad(size_t at, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
              const Vec4& c, float mode, float radius, float param, float grow = 0.0f);

    std::vector<UIVertex> m_Vertices;
    Scope<Shader>       m_Shader;
    Scope<VertexArray>  m_VAO;
    Ref<VertexBuffer>   m_VBO;
    u32   m_FontTexture = 0;
    u32   m_Width = 1, m_Height = 1;
    float m_Scale = 1.0f, m_LogicalW = 1.0f, m_LogicalH = 1.0f;
};

} // namespace Lapis
