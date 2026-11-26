#include "Lapis/Assets/ObjLoader.h"

#include <sstream>
#include <unordered_map>

#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"

namespace Lapis::ObjLoader {

namespace {
    struct Key {
        int v, t, n;
        bool operator==(const Key& o) const { return v == o.v && t == o.t && n == o.n; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const { return (size_t(k.v) * 73856093u) ^ (size_t(k.t) * 19349663u) ^ (size_t(k.n) * 83492791u); }
    };

    // Resolves a 1-based (or negative, relative) OBJ index to 0-based; -1 if absent/invalid.
    int ResolveIndex(const std::string& s, size_t count) {
        if (s.empty()) return -1;
        const int i = std::stoi(s);
        if (i > 0) return i - 1 < (int)count ? i - 1 : -2;
        if (i < 0) return (int)count + i >= 0 ? (int)count + i : -2;
        return -2;
    }
}

std::optional<MeshData> Parse(const std::string& text) {
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    MeshData mesh;
    std::unordered_map<Key, u32, KeyHash> lookup;
    bool missingNormals = false;

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    try {
        while (std::getline(in, line)) {
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream ls(line);
            std::string tag;
            if (!(ls >> tag) || tag[0] == '#') continue;

            if (tag == "v") { Vec3 p; ls >> p.x >> p.y >> p.z; positions.push_back(p); }
            else if (tag == "vt") { Vec2 t; ls >> t.x >> t.y; uvs.push_back(t); }
            else if (tag == "vn") { Vec3 n; ls >> n.x >> n.y >> n.z; normals.push_back(Normalize(n)); }
            else if (tag == "f") {
                std::vector<u32> face;
                std::string vert;
                while (ls >> vert) {
                    std::string parts[3];
                    size_t start = 0;
                    for (int k = 0; k < 3; ++k) {
                        const size_t slash = vert.find('/', start);
                        parts[k] = vert.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                        if (slash == std::string::npos) break;
                        start = slash + 1;
                    }
                    Key key{ResolveIndex(parts[0], positions.size()), ResolveIndex(parts[1], uvs.size()),
                            ResolveIndex(parts[2], normals.size())};
                    if (key.v < 0 || key.t == -2 || key.n == -2) {
                        LAPIS_ERROR("OBJ: bad index on line {}", lineNo);
                        return std::nullopt;
                    }
                    auto it = lookup.find(key);
                    if (it == lookup.end()) {
                        Vertex v;
                        v.Position = positions[key.v];
                        v.UV = key.t >= 0 ? uvs[key.t] : Vec2(0.0f);
                        if (key.n >= 0) v.Normal = normals[key.n];
                        else missingNormals = true;
                        it = lookup.emplace(key, (u32)mesh.Vertices.size()).first;
                        mesh.Vertices.push_back(v);
                    }
                    face.push_back(it->second);
                }
                for (size_t k = 1; k + 1 < face.size(); ++k)
                    mesh.Indices.insert(mesh.Indices.end(), {face[0], face[k], face[k + 1]});
            }
            // o, g, s, usemtl, mtllib: ignored for now
        }
    } catch (const std::exception&) {
        LAPIS_ERROR("OBJ: parse error on line {}", lineNo);
        return std::nullopt;
    }

    if (mesh.Indices.empty()) return std::nullopt;
    if (missingNormals) mesh.RecalculateNormals();
    return mesh;
}

std::optional<MeshData> Load(const std::string& path) {
    auto text = FileSystem::ReadText(path);
    if (!text) { LAPIS_ERROR("OBJ: cannot read '{}'", path); return std::nullopt; }
    auto mesh = Parse(*text);
    if (!mesh) LAPIS_ERROR("OBJ: failed to load '{}'", path);
    return mesh;
}

} // namespace Lapis::ObjLoader
