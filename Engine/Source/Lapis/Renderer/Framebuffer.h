#pragma once

#include "Lapis/Core/Base.h"

namespace Lapis {

enum class ColorFormat { None, RGBA8, RGBA16F };
enum class DepthFormat { None, Depth24, Depth32F };

struct FramebufferSpec {
    u32 Width = 1280;
    u32 Height = 720;
    u32 Samples = 1;               // >1 = multisampled (renderbuffers, resolve with BlitTo)
    ColorFormat Color = ColorFormat::RGBA8;
    DepthFormat Depth = DepthFormat::Depth24;
    bool DepthCompare = false;     // depth texture usable as sampler2DShadow
};

// Render target with an optional color and depth attachment. Single-sampled
// targets use textures (sampleable); multisampled targets use renderbuffers.
class Framebuffer : NonCopyable {
public:
    explicit Framebuffer(const FramebufferSpec& spec);
    ~Framebuffer();

    void Bind() const;                       // binds and sets the viewport
    static void BindTarget(u32 fbo, u32 width, u32 height);

    void Resize(u32 width, u32 height);
    // Copies color (+depth) into `target` (nullptr = the given fbo id).
    void BlitTo(const Framebuffer* target, bool color = true, bool depth = false) const;

    const FramebufferSpec& GetSpec() const { return m_Spec; }
    u32 GetID() const { return m_FBO; }
    u32 GetColorTexture() const { return m_ColorTex; }
    u32 GetDepthTexture() const { return m_DepthTex; }
    bool IsComplete() const { return m_Complete; }

private:
    void Create();
    void Destroy();

    FramebufferSpec m_Spec;
    u32 m_FBO = 0;
    u32 m_ColorTex = 0, m_DepthTex = 0;  // single-sampled
    u32 m_ColorRBO = 0, m_DepthRBO = 0;  // multisampled
    bool m_Complete = false;
};

} // namespace Lapis
