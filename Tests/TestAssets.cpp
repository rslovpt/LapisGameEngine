#include "TestFramework.h"

#include "Lapis/Assets/ImageLoader.h"
#include "Lapis/Assets/ObjLoader.h"
#include "Lapis/Renderer/Mesh.h"

using namespace Lapis;

LAPIS_TEST(Assets, ObjQuadWithNormals) {
    const char* obj =
        "# quad\n"
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n";
    auto mesh = ObjLoader::Parse(obj);
    CHECK(mesh.has_value());
    if (!mesh) return;
    CHECK(mesh->Vertices.size() == 4);
    CHECK(mesh->Indices.size() == 6);
    CHECK(NearlyEqual(mesh->Vertices[2].Normal, Vec3(0, 0, 1)));
}

LAPIS_TEST(Assets, ObjNegativeIndicesAndGeneratedNormals) {
    const char* obj = "v 0 0 0\nv 1 0 0\nv 0 0 -1\nf -3 -2 -1\n";
    auto mesh = ObjLoader::Parse(obj);
    CHECK(mesh.has_value());
    if (!mesh) return;
    CHECK(mesh->Indices.size() == 3);
    CHECK(NearlyEqual(mesh->Vertices[0].Normal, Vec3(0, 1, 0)));
}

LAPIS_TEST(Assets, ObjRejectsGarbage) {
    CHECK(!ObjLoader::Parse("f 1 2 3\n").has_value());
    CHECK(!ObjLoader::Parse("").has_value());
}

LAPIS_TEST(Assets, BmpRoundTrip) {
    std::vector<u8> rgba(3 * 2 * 4);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = u8(i * 10);
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255; // BMP 24-bit drops alpha
    auto bytes = ImageLoader::EncodeBMP(3, 2, rgba);
    auto img = ImageLoader::LoadBMP(bytes);
    CHECK(img.has_value());
    if (img) {
        CHECK(img->Width == 3 && img->Height == 2);
        CHECK(img->Pixels == rgba);
    }
}

LAPIS_TEST(Assets, TgaUncompressedAndRLE) {
    // 2x1 32-bit uncompressed, bottom-up: pixel0 = red, pixel1 = green (BGRA in file)
    std::vector<u8> raw = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 8,
                           0, 0, 255, 255,   0, 255, 0, 128};
    auto a = ImageLoader::LoadTGA(raw);
    CHECK(a.has_value());
    if (a) {
        CHECK(a->Pixels[0] == 255 && a->Pixels[1] == 0 && a->Pixels[3] == 255);
        CHECK(a->Pixels[5] == 255 && a->Pixels[7] == 128);
    }
    // 3x1 24-bit RLE: one run packet of 3 blue pixels
    std::vector<u8> rle = {0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 1, 0, 24, 0,
                           0x82, 255, 0, 0};
    auto b = ImageLoader::LoadTGA(rle);
    CHECK(b.has_value());
    if (b) for (int i = 0; i < 3; ++i) CHECK(b->Pixels[i * 4 + 2] == 255 && b->Pixels[i * 4] == 0);
    CHECK(!ImageLoader::LoadTGA({1, 2, 3}).has_value());
}

LAPIS_TEST(Assets, PrimitiveWindingMatchesNormals) {
    // Every triangle's geometric normal must agree with its vertex normals
    // (counter-clockwise front faces), otherwise back-face culling breaks.
    for (const MeshData& m : {MeshFactory::Cube(), MeshFactory::Plane(2, 3), MeshFactory::Sphere(1, 12, 8)}) {
        int bad = 0;
        for (size_t i = 0; i + 2 < m.Indices.size(); i += 3) {
            const Vertex& a = m.Vertices[m.Indices[i]];
            const Vertex& b = m.Vertices[m.Indices[i + 1]];
            const Vertex& c = m.Vertices[m.Indices[i + 2]];
            const Vec3 n = Cross(b.Position - a.Position, c.Position - a.Position);
            if (LengthSq(n) < 1e-10f) continue; // degenerate pole triangles
            if (Dot(n, a.Normal + b.Normal + c.Normal) <= 0.0f) ++bad;
        }
        CHECK(bad == 0);
    }
}
