#pragma once

#include <string>
#include <vector>

#include "Lapis/Core/Base.h"
#include "Lapis/Math/Math.h"

namespace Lapis {

// CPU-side image (always 8-bit RGBA after loading).
struct Image {
    u32 Width = 0;
    u32 Height = 0;
    std::vector<u8> Pixels; // RGBA8, rows bottom-to-top (OpenGL order)

    bool IsValid() const { return Width > 0 && Height > 0 && Pixels.size() == size_t(Width) * Height * 4; }
};

enum class TextureFilter { Nearest, Linear, Trilinear };
enum class TextureWrap   { Repeat, Clamp, Mirror };

struct TextureSpec {
    TextureFilter Filter = TextureFilter::Trilinear;
    TextureWrap   Wrap   = TextureWrap::Repeat;
    bool SRGB    = true;   // color data: true, data maps (normals etc.): false
    bool Mipmaps = true;
};

class Texture2D : NonCopyable {
public:
    Texture2D(const Image& image, const TextureSpec& spec = {});
    ~Texture2D();

    void Bind(u32 slot = 0) const;

    u32 GetWidth() const { return m_Width; }
    u32 GetHeight() const { return m_Height; }
    u32 GetID() const { return m_ID; }
    const std::string& GetPath() const { return m_Path; }
    void SetPath(const std::string& path) { m_Path = path; }

    // Built-in textures.
    static Ref<Texture2D> CreateSolid(const Vec4& color);
    static Ref<Texture2D> CreateChecker(u32 size, u32 cells, const Vec4& a, const Vec4& b);

private:
    u32 m_ID = 0;
    u32 m_Width = 0, m_Height = 0;
    std::string m_Path;
};

} // namespace Lapis
