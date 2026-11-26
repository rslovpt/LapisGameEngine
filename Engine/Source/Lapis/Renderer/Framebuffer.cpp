#include "Lapis/Renderer/Framebuffer.h"

#include "Lapis/Core/Log.h"
#include "Lapis/Renderer/GL.h"

namespace Lapis {

namespace {
    GLenum ColorInternal(ColorFormat f) { return f == ColorFormat::RGBA16F ? GL_RGBA16F : GL_RGBA8; }
    GLenum DepthInternal(DepthFormat f) { return f == DepthFormat::Depth32F ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24; }
}

Framebuffer::Framebuffer(const FramebufferSpec& spec) : m_Spec(spec) { Create(); }
Framebuffer::~Framebuffer() { Destroy(); }

void Framebuffer::Create() {
    glGenFramebuffers(1, &m_FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    const GLsizei w = (GLsizei)(m_Spec.Width ? m_Spec.Width : 1);
    const GLsizei h = (GLsizei)(m_Spec.Height ? m_Spec.Height : 1);
    const bool ms = m_Spec.Samples > 1;

    if (m_Spec.Color != ColorFormat::None) {
        if (ms) {
            glGenRenderbuffers(1, &m_ColorRBO);
            glBindRenderbuffer(GL_RENDERBUFFER, m_ColorRBO);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, (GLsizei)m_Spec.Samples, ColorInternal(m_Spec.Color), w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_ColorRBO);
        } else {
            glGenTextures(1, &m_ColorTex);
            glBindTexture(GL_TEXTURE_2D, m_ColorTex);
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ColorInternal(m_Spec.Color), w, h, 0, GL_RGBA,
                         m_Spec.Color == ColorFormat::RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTex, 0);
        }
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    if (m_Spec.Depth != DepthFormat::None) {
        if (ms) {
            glGenRenderbuffers(1, &m_DepthRBO);
            glBindRenderbuffer(GL_RENDERBUFFER, m_DepthRBO);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, (GLsizei)m_Spec.Samples, DepthInternal(m_Spec.Depth), w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_DepthRBO);
        } else {
            glGenTextures(1, &m_DepthTex);
            glBindTexture(GL_TEXTURE_2D, m_DepthTex);
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)DepthInternal(m_Spec.Depth), w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // outside the shadow map = lit
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
            if (m_Spec.DepthCompare) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            }
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTex, 0);
        }
    }

    m_Complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!m_Complete) LAPIS_ERROR("Framebuffer {}x{} (samples {}) is incomplete", w, h, m_Spec.Samples);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Destroy() {
    if (m_ColorTex) glDeleteTextures(1, &m_ColorTex);
    if (m_DepthTex) glDeleteTextures(1, &m_DepthTex);
    if (m_ColorRBO) glDeleteRenderbuffers(1, &m_ColorRBO);
    if (m_DepthRBO) glDeleteRenderbuffers(1, &m_DepthRBO);
    if (m_FBO) glDeleteFramebuffers(1, &m_FBO);
    m_ColorTex = m_DepthTex = m_ColorRBO = m_DepthRBO = m_FBO = 0;
}

void Framebuffer::Bind() const { BindTarget(m_FBO, m_Spec.Width, m_Spec.Height); }

void Framebuffer::BindTarget(u32 fbo, u32 width, u32 height) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
}

void Framebuffer::Resize(u32 width, u32 height) {
    if (width == 0 || height == 0 || (width == m_Spec.Width && height == m_Spec.Height)) return;
    m_Spec.Width = width;
    m_Spec.Height = height;
    Destroy();
    Create();
}

void Framebuffer::BlitTo(const Framebuffer* target, bool color, bool depth) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target ? target->m_FBO : 0);
    const GLint dw = target ? (GLint)target->m_Spec.Width : (GLint)m_Spec.Width;
    const GLint dh = target ? (GLint)target->m_Spec.Height : (GLint)m_Spec.Height;
    GLbitfield mask = 0;
    if (color) mask |= GL_COLOR_BUFFER_BIT;
    if (depth) mask |= GL_DEPTH_BUFFER_BIT;
    glBlitFramebuffer(0, 0, (GLint)m_Spec.Width, (GLint)m_Spec.Height, 0, 0, dw, dh, mask, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace Lapis
