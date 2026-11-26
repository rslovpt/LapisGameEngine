#include "Lapis/Renderer/Texture.h"

#include "Lapis/Core/Log.h"
#include "Lapis/Renderer/GL.h"

namespace Lapis {

Texture2D::Texture2D(const Image& image, const TextureSpec& spec) : m_Width(image.Width), m_Height(image.Height) {
    if (!image.IsValid()) {
        LAPIS_ERROR("Texture2D: invalid image ({}x{}, {} bytes)", image.Width, image.Height, image.Pixels.size());
        return;
    }
    glGenTextures(1, &m_ID);
    glBindTexture(GL_TEXTURE_2D, m_ID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, spec.SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8, (GLsizei)image.Width, (GLsizei)image.Height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, image.Pixels.data());

    GLint wrap = GL_REPEAT;
    if (spec.Wrap == TextureWrap::Clamp) wrap = GL_CLAMP_TO_EDGE;
    if (spec.Wrap == TextureWrap::Mirror) wrap = GL_MIRRORED_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    const bool mips = spec.Mipmaps && spec.Filter == TextureFilter::Trilinear;
    GLint minF = GL_LINEAR, magF = GL_LINEAR;
    if (spec.Filter == TextureFilter::Nearest) { minF = GL_NEAREST; magF = GL_NEAREST; }
    if (mips) minF = GL_LINEAR_MIPMAP_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
    if (mips) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        glGetError(); // anisotropy is an extension before GL 4.6; ignore if unsupported
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture2D::~Texture2D() {
    if (m_ID) glDeleteTextures(1, &m_ID);
}

void Texture2D::Bind(u32 slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_ID);
}

namespace {
    u8 ToByte(float v) { return static_cast<u8>(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }
}

Ref<Texture2D> Texture2D::CreateSolid(const Vec4& color) {
    Image img;
    img.Width = img.Height = 1;
    img.Pixels = {ToByte(color.x), ToByte(color.y), ToByte(color.z), ToByte(color.w)};
    TextureSpec spec;
    spec.Filter = TextureFilter::Nearest;
    spec.Mipmaps = false;
    return CreateRef<Texture2D>(img, spec);
}

Ref<Texture2D> Texture2D::CreateChecker(u32 size, u32 cells, const Vec4& a, const Vec4& b) {
    Image img;
    img.Width = img.Height = size;
    img.Pixels.resize(size_t(size) * size * 4);
    const u32 cell = size / (cells ? cells : 1);
    for (u32 y = 0; y < size; ++y)
        for (u32 x = 0; x < size; ++x) {
            const bool odd = ((x / cell) + (y / cell)) % 2 == 1;
            const Vec4& c = odd ? b : a;
            u8* p = &img.Pixels[(size_t(y) * size + x) * 4];
            p[0] = ToByte(c.x); p[1] = ToByte(c.y); p[2] = ToByte(c.z); p[3] = ToByte(c.w);
        }
    return CreateRef<Texture2D>(img);
}

} // namespace Lapis
