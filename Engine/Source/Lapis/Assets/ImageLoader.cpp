#include "Lapis/Assets/ImageLoader.h"

#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"

namespace Lapis::ImageLoader {

namespace {
    u16 ReadU16(const u8* p) { return u16(p[0] | (p[1] << 8)); }
    u32 ReadU32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
    void WriteU16(std::vector<u8>& o, u16 v) { o.push_back(u8(v)); o.push_back(u8(v >> 8)); }
    void WriteU32(std::vector<u8>& o, u32 v) { for (int i = 0; i < 4; ++i) o.push_back(u8(v >> (8 * i))); }
}

std::optional<Image> Load(const std::string& path) {
    auto bytes = FileSystem::ReadBinary(path);
    if (!bytes) { LAPIS_ERROR("Image: cannot read '{}'", path); return std::nullopt; }
    const std::string ext = FileSystem::ExtensionOf(path);
    std::optional<Image> img;
    if (ext == ".tga") img = LoadTGA(*bytes);
    else if (ext == ".bmp") img = LoadBMP(*bytes);
    else { LAPIS_ERROR("Image: unsupported format '{}' ({})", ext, path); return std::nullopt; }
    if (!img) LAPIS_ERROR("Image: failed to decode '{}'", path);
    return img;
}

std::optional<Image> LoadTGA(const std::vector<u8>& b) {
    if (b.size() < 18) return std::nullopt;
    const u8 idLength = b[0], colorMapType = b[1], type = b[2];
    const u16 width = ReadU16(&b[12]), height = ReadU16(&b[14]);
    const u8 bpp = b[16], descriptor = b[17];
    const bool rle = type == 10 || type == 11;
    const bool gray = type == 3 || type == 11;
    if (colorMapType != 0 || !(type == 2 || type == 3 || type == 10 || type == 11)) return std::nullopt;
    if (!(bpp == 24 || bpp == 32 || (gray && bpp == 8)) || width == 0 || height == 0) return std::nullopt;

    const size_t bytesPerPixel = bpp / 8;
    const size_t pixelCount = size_t(width) * height;
    size_t pos = 18 + idLength;

    Image img;
    img.Width = width;
    img.Height = height;
    img.Pixels.resize(pixelCount * 4);

    auto readPixel = [&](size_t index) -> bool {
        if (pos + bytesPerPixel > b.size()) return false;
        u8* out = &img.Pixels[index * 4];
        if (gray) { out[0] = out[1] = out[2] = b[pos]; out[3] = 255; }
        else {
            out[0] = b[pos + 2]; out[1] = b[pos + 1]; out[2] = b[pos];  // BGR(A) -> RGB(A)
            out[3] = bytesPerPixel == 4 ? b[pos + 3] : 255;
        }
        pos += bytesPerPixel;
        return true;
    };

    if (!rle) {
        for (size_t i = 0; i < pixelCount; ++i) if (!readPixel(i)) return std::nullopt;
    } else {
        size_t i = 0;
        while (i < pixelCount) {
            if (pos >= b.size()) return std::nullopt;
            const u8 header = b[pos++];
            const size_t count = (header & 0x7F) + 1u;
            if (i + count > pixelCount) return std::nullopt;
            if (header & 0x80) {                    // run-length packet
                if (!readPixel(i)) return std::nullopt;
                for (size_t k = 1; k < count; ++k)
                    std::copy_n(&img.Pixels[i * 4], 4, &img.Pixels[(i + k) * 4]);
            } else {                                // raw packet
                for (size_t k = 0; k < count; ++k) if (!readPixel(i + k)) return std::nullopt;
            }
            i += count;
        }
    }

    // TGA is bottom-up unless descriptor bit 5 is set; we want bottom-up.
    if (descriptor & 0x20) {
        const size_t row = size_t(width) * 4;
        for (u32 y = 0; y < height / 2u; ++y)
            std::swap_ranges(img.Pixels.begin() + y * row, img.Pixels.begin() + (y + 1) * row,
                             img.Pixels.begin() + (height - 1 - y) * row);
    }
    return img;
}

std::optional<Image> LoadBMP(const std::vector<u8>& b) {
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return std::nullopt;
    const u32 dataOffset = ReadU32(&b[10]);
    const i32 width = (i32)ReadU32(&b[18]);
    i32 height = (i32)ReadU32(&b[22]);
    const u16 bpp = ReadU16(&b[28]);
    const u32 compression = ReadU32(&b[30]);
    if (width <= 0 || height == 0 || !(bpp == 24 || bpp == 32) || !(compression == 0 || compression == 3))
        return std::nullopt;

    const bool topDown = height < 0;
    if (topDown) height = -height;
    const size_t bytesPerPixel = bpp / 8;
    const size_t stride = (size_t(width) * bytesPerPixel + 3) & ~size_t(3);
    if (dataOffset + stride * size_t(height) > b.size()) return std::nullopt;

    Image img;
    img.Width = (u32)width;
    img.Height = (u32)height;
    img.Pixels.resize(size_t(width) * height * 4);
    for (i32 y = 0; y < height; ++y) {
        const i32 dstY = topDown ? height - 1 - y : y;  // keep bottom-up
        const u8* src = &b[dataOffset + stride * y];
        u8* dst = &img.Pixels[size_t(dstY) * width * 4];
        for (i32 x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * bytesPerPixel + 2];
            dst[x * 4 + 1] = src[x * bytesPerPixel + 1];
            dst[x * 4 + 2] = src[x * bytesPerPixel + 0];
            dst[x * 4 + 3] = bytesPerPixel == 4 ? src[x * bytesPerPixel + 3] : 255;
        }
    }
    return img;
}

std::vector<u8> EncodeBMP(u32 width, u32 height, const std::vector<u8>& rgba) {
    const u32 stride = (width * 3 + 3) & ~3u;
    const u32 dataSize = stride * height;
    std::vector<u8> o;
    o.reserve(54 + dataSize);
    o.push_back('B'); o.push_back('M');
    WriteU32(o, 54 + dataSize); WriteU32(o, 0); WriteU32(o, 54);
    WriteU32(o, 40); WriteU32(o, width); WriteU32(o, height);
    WriteU16(o, 1); WriteU16(o, 24); WriteU32(o, 0); WriteU32(o, dataSize);
    WriteU32(o, 2835); WriteU32(o, 2835); WriteU32(o, 0); WriteU32(o, 0);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const u8* p = &rgba[(size_t(y) * width + x) * 4];
            o.push_back(p[2]); o.push_back(p[1]); o.push_back(p[0]);
        }
        for (u32 pad = width * 3; pad < stride; ++pad) o.push_back(0);
    }
    return o;
}

bool WriteBMP(const std::string& path, u32 width, u32 height, const std::vector<u8>& rgba) {
    if (rgba.size() < size_t(width) * height * 4) return false;
    const std::vector<u8> bytes = EncodeBMP(width, height, rgba);
    return FileSystem::WriteBinary(path, bytes.data(), bytes.size());
}

} // namespace Lapis::ImageLoader
