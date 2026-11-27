#include "Lapis/Renderer/Mesh.h"


namespace Lapis {

// --- MeshData ------------------------------------------------------------------
AABB MeshData::ComputeBounds() const {
    AABB box;
    for (const Vertex& v : Vertices) box.Expand(v.Position);
    return box;
}

void MeshData::RecalculateNormals() {
    for (Vertex& v : Vertices) v.Normal = Vec3(0.0f);
    for (size_t i = 0; i + 2 < Indices.size(); i += 3) {
        Vertex& a = Vertices[Indices[i]];
        Vertex& b = Vertices[Indices[i + 1]];
        Vertex& c = Vertices[Indices[i + 2]];
        const Vec3 n = Cross(b.Position - a.Position, c.Position - a.Position); // area weighted
        a.Normal += n; b.Normal += n; c.Normal += n;
    }
    for (Vertex& v : Vertices) v.Normal = Normalize(v.Normal);
}

// --- Mesh ----------------------------------------------------------------------
Mesh::Mesh(const MeshData& data, const std::string& name)
    : m_IndexCount((u32)data.Indices.size()), m_Bounds(data.ComputeBounds()), m_Name(name) {
    RHI::Device* device = RHI::Current();
    if (!device || data.Vertices.empty() || data.Indices.empty()) return;
    RHI::BufferDesc vb;
    vb.Type = RHI::BufferType::Vertex;
    vb.Size = data.Vertices.size() * sizeof(Vertex);
    vb.Data = data.Vertices.data();
    vb.Name = "MeshVertices";
    m_VertexBuffer = device->CreateBuffer(vb);
    RHI::BufferDesc ib;
    ib.Type = RHI::BufferType::Index;
    ib.Size = data.Indices.size() * sizeof(u32);
    ib.Data = data.Indices.data();
    ib.Name = "MeshIndices";
    m_IndexBuffer = device->CreateBuffer(ib);
}

Mesh::~Mesh() {
    if (RHI::Device* device = RHI::Current()) {
        if (m_VertexBuffer) device->Destroy(m_VertexBuffer);
        if (m_IndexBuffer) device->Destroy(m_IndexBuffer);
    }
}

void Mesh::Bind(RHI::Device& device) const {
    device.SetVertexBuffer(0, {m_VertexBuffer, 0, (u32)sizeof(Vertex)});
    device.SetIndexBuffer({m_IndexBuffer, 0, 0});
}

// --- MeshFactory ---------------------------------------------------------------
namespace MeshFactory {

MeshData Cube(float size) {
    const float h = size * 0.5f;
    MeshData m;
    // Each face: normal, tangent "right" and "up" axes.
    struct Face { Vec3 n, r, u; };
    const Face faces[6] = {
        {{0, 0, 1},  {1, 0, 0},  {0, 1, 0}},   // +Z
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},   // -Z
        {{1, 0, 0},  {0, 0, -1}, {0, 1, 0}},   // +X
        {{-1, 0, 0}, {0, 0, 1},  {0, 1, 0}},   // -X
        {{0, 1, 0},  {1, 0, 0},  {0, 0, -1}},  // +Y
        {{0, -1, 0}, {1, 0, 0},  {0, 0, 1}},   // -Y
    };
    for (const Face& f : faces) {
        const u32 base = (u32)m.Vertices.size();
        const Vec3 c = f.n * h;
        m.Vertices.push_back({c - f.r * h - f.u * h, f.n, {0, 0}});
        m.Vertices.push_back({c + f.r * h - f.u * h, f.n, {1, 0}});
        m.Vertices.push_back({c + f.r * h + f.u * h, f.n, {1, 1}});
        m.Vertices.push_back({c - f.r * h + f.u * h, f.n, {0, 1}});
        m.Indices.insert(m.Indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

MeshData Plane(float size, u32 subdivisions, float uvScale) {
    MeshData m;
    const u32 n = subdivisions < 1 ? 1 : subdivisions;
    const float h = size * 0.5f;
    for (u32 z = 0; z <= n; ++z)
        for (u32 x = 0; x <= n; ++x) {
            const float fx = (float)x / n, fz = (float)z / n;
            m.Vertices.push_back({{-h + fx * size, 0.0f, h - fz * size}, {0, 1, 0}, {fx * uvScale, fz * uvScale}});
        }
    for (u32 z = 0; z < n; ++z)
        for (u32 x = 0; x < n; ++x) {
            const u32 i = z * (n + 1) + x;
            m.Indices.insert(m.Indices.end(), {i, i + 1, i + n + 2, i, i + n + 2, i + n + 1});
        }
    return m;
}

MeshData Sphere(float radius, u32 segments, u32 rings) {
    MeshData m;
    for (u32 r = 0; r <= rings; ++r) {
        const float v = (float)r / rings;
        const float phi = v * PI;                 // 0 at north pole
        for (u32 s = 0; s <= segments; ++s) {
            const float u = (float)s / segments;
            const float theta = u * TWO_PI;
            const Vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), -std::sin(phi) * std::sin(theta)};
            m.Vertices.push_back({n * radius, n, {u, 1.0f - v}});
        }
    }
    for (u32 r = 0; r < rings; ++r)
        for (u32 s = 0; s < segments; ++s) {
            const u32 a = r * (segments + 1) + s;
            const u32 b = a + segments + 1;
            m.Indices.insert(m.Indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    return m;
}

} // namespace MeshFactory
} // namespace Lapis
