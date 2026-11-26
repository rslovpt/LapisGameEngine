#pragma once
// Dependency-free image IO: TGA (uncompressed + RLE, 24/32-bit, 8-bit gray)
// and BMP (24/32-bit uncompressed). Output is always RGBA8, bottom-up rows.

#include <optional>
#include <string>
#include <vector>

#include "Lapis/Renderer/Texture.h"

namespace Lapis::ImageLoader {

std::optional<Image> Load(const std::string& path);   // by extension
std::optional<Image> LoadTGA(const std::vector<u8>& bytes);
std::optional<Image> LoadBMP(const std::vector<u8>& bytes);

// Writes a 24-bit BMP (bottom-up RGBA8 input). Used for screenshots.
bool WriteBMP(const std::string& path, u32 width, u32 height, const std::vector<u8>& rgba);
std::vector<u8> EncodeBMP(u32 width, u32 height, const std::vector<u8>& rgba);

} // namespace Lapis::ImageLoader
