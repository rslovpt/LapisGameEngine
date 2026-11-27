#include "Lapis/Renderer/Texture.h"

#include "Lapis/Core/Log.h"

namespace Lapis {

Texture2D::Texture2D(const Image& image, const TextureSpec& spec) : m_Width(image.Width), m_Height(image.Height) {
    if (!image.IsValid()) {
        LAPIS_ERROR("Texture2D: invalid image ({}x{}, {} bytes)", image.Width, image.Height, image.Pixels.size());
        return;
    }
    RHI::Device* device = RHI::Current();
    if (!device) { LAPIS_ERROR("Texture2D: no graphics device"); return; }
    RHI::TextureDesc desc;
    desc.Width = image.Width;
    desc.Height = image.Height;
    desc.Fmt = spec.SRGB ? RHI::Format::RGBA8_SRGB : RHI::Format::RGBA8;
    desc.Mipmaps = spec.Mipmaps && spec.Filter == TextureFilter::Trilinear;
    desc.Data = image.Pixels.data();
    desc.Name = "Texture2D";
    m_Handle = device->CreateTexture(desc);
    if (spec.Filter == TextureFilter::Nearest) m_Sampler = RHI::Sampler::PointClamp;
    else if (!desc.Mipmaps) m_Sampler = spec.Wrap == TextureWrap::Clamp ? RHI::Sampler::LinearClamp : RHI::Sampler::LinearWrap;
}

Texture2D::~Texture2D() {
    if (m_Handle) if (RHI::Device* device = RHI::Current()) device->Destroy(m_Handle);
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
