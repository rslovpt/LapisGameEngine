#pragma once

#include <string>
#include <vector>

#include "Lapis/Core/Base.h"
#include "Lapis/Math/Geometry.h"
#include "Lapis/Renderer/Buffer.h"

namespace Lapis {

struct Vertex {
    Vec3 Position;
    Vec3 Normal;
    Vec2 UV;
};

// CPU-side mesh data. Kept separate from Mesh so loaders and generators can
// be unit-tested without a GPU.
struct MeshData {
    std::vector<Vertex> Vertices;
    std::vector<u32>    Indices;   // triangle list, counter-clockwise front faces

    AABB ComputeBounds() const;
    void RecalculateNormals();     // smooth normals from triangle faces
};

// GPU mesh: an indexed triangle list.
class Mesh : NonCopyable {
public:
    explicit Mesh(const MeshData& data, const std::string& name = "Mesh");

    void Draw() const;
    // Draws `count` instances whose data {mat4 model, vec4 tint} starts at
    // byte `offset` in `instanceBuffer` (vertex attributes 3..7).
    void DrawInstanced(u32 instanceBuffer, size_t offset, u32 count) const;

    const AABB& GetBounds() const { return m_Bounds; }
    u32 GetIndexCount() const { return m_IndexCount; }
    const std::string& GetName() const { return m_Name; }

private:
    Scope<VertexArray> m_VAO;
    u32  m_IndexCount = 0;
    AABB m_Bounds;
    std::string m_Name;
};

// Procedural primitives (all unit sized, centered at the origin).
namespace MeshFactory {
    MeshData Cube(float size = 1.0f);
    MeshData Plane(float size = 1.0f, u32 subdivisions = 1, float uvScale = 1.0f);
    MeshData Sphere(float radius = 0.5f, u32 segments = 32, u32 rings = 16);
}

} // namespace Lapis
