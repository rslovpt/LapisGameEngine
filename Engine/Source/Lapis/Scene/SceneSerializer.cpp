#include "Lapis/Scene/SceneSerializer.h"

#include <sstream>
#include <unordered_map>
#include <vector>

#include "Lapis/Assets/AssetManager.h"
#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Scene/Scene.h"
#include "Lapis/Scripting/ScriptEngine.h"

namespace Lapis::SceneSerializer {

namespace {

// --- writing -------------------------------------------------------------------
std::string Num(float v) {
    std::ostringstream ss;
    ss.precision(6);
    ss << v;
    return ss.str();
}
std::string V3(const Vec3& v) { return Num(v.x) + "," + Num(v.y) + "," + Num(v.z); }
std::string V4(const Vec4& v) { return Num(v.x) + "," + Num(v.y) + "," + Num(v.z) + "," + Num(v.w); }
std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + "\"";
}

// --- reading -------------------------------------------------------------------
struct Line {
    std::string Tag;
    std::unordered_map<std::string, std::string> Values;

    bool Has(const char* k) const { return Values.count(k) > 0; }
    std::string Str(const char* k, const std::string& def = "") const {
        auto it = Values.find(k);
        return it == Values.end() ? def : it->second;
    }
    float F(const char* k, float def) const {
        auto it = Values.find(k);
        if (it == Values.end()) return def;
        try { return std::stof(it->second); } catch (...) { return def; }
    }
    bool B(const char* k, bool def) const {
        auto it = Values.find(k);
        if (it == Values.end()) return def;
        return it->second == "1" || it->second == "true" || it->second == "True";
    }
    std::vector<float> List(const char* k) const {
        std::vector<float> out;
        auto it = Values.find(k);
        if (it == Values.end()) return out;
        std::stringstream ss(it->second);
        std::string part;
        while (std::getline(ss, part, ',')) {
            try { out.push_back(std::stof(part)); } catch (...) { out.push_back(0.0f); }
        }
        return out;
    }
    Vec3 V3(const char* k, const Vec3& def) const {
        auto l = List(k);
        if (l.size() == 1) return Vec3(l[0]);
        return l.size() >= 3 ? Vec3(l[0], l[1], l[2]) : def;
    }
    Vec4 V4(const char* k, const Vec4& def) const {
        auto l = List(k);
        if (l.size() == 3) return {l[0], l[1], l[2], 1.0f};
        return l.size() >= 4 ? Vec4(l[0], l[1], l[2], l[3]) : def;
    }
    Vec2 V2(const char* k, const Vec2& def) const {
        auto l = List(k);
        if (l.size() == 1) return Vec2(l[0]);
        return l.size() >= 2 ? Vec2(l[0], l[1]) : def;
    }
};

bool ParseLine(const std::string& raw, Line& out, std::string& err) {
    out = {};
    size_t i = 0;
    auto skipWs = [&] { while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\r')) ++i; };
    skipWs();
    if (i >= raw.size() || raw[i] == '#') return true; // blank / comment
    while (i < raw.size() && !std::isspace((unsigned char)raw[i])) out.Tag += raw[i++];
    while (true) {
        skipWs();
        if (i >= raw.size() || raw[i] == '#') break;
        std::string key;
        while (i < raw.size() && raw[i] != '=' && !std::isspace((unsigned char)raw[i])) key += raw[i++];
        if (i >= raw.size() || raw[i] != '=') { err = "expected key=value near '" + key + "'"; return false; }
        ++i;
        std::string value;
        if (i < raw.size() && raw[i] == '"') {
            ++i;
            bool closed = false;
            while (i < raw.size()) {
                if (raw[i] == '\\' && i + 1 < raw.size()) { value += raw[i + 1]; i += 2; continue; }
                if (raw[i] == '"') { closed = true; ++i; break; }
                value += raw[i++];
            }
            if (!closed) { err = "unterminated string for '" + key + "'"; return false; }
        } else {
            while (i < raw.size() && !std::isspace((unsigned char)raw[i])) value += raw[i++];
        }
        out.Values[key] = value;
    }
    return true;
}

} // namespace

std::string Serialize(Scene& scene) {
    std::ostringstream o;
    o << "# Lapis Scene v1\n";
    o << "Scene name=" << Quote(scene.GetName()) << "\n";
    const Environment& env = scene.Env;
    o << "Environment zenith=" << V3(env.SkyZenith) << " horizon=" << V3(env.SkyHorizon) << " ground=" << V3(env.SkyGround)
      << " ambient=" << Num(env.AmbientIntensity) << " reflection=" << Num(env.ReflectionIntensity)
      << " fog=" << Num(env.FogDensity) << " exposure=" << Num(env.Exposure) << "\n";
    o << "Sun direction=" << V3(scene.DefaultSun.Direction) << " color=" << V3(scene.DefaultSun.Color)
      << " intensity=" << Num(scene.DefaultSun.Intensity) << " shadows=" << (scene.DefaultSun.CastShadows ? 1 : 0) << "\n";
    o << "Physics gravity=" << V3(scene.GetPhysics().Settings.Gravity) << "\n";

    Registry& reg = scene.GetRegistry();
    std::unordered_map<u32, u32> fileIds; // entity index -> id in file
    u32 next = 1;
    reg.EachEntity([&](EntityID id) { fileIds[id.Index] = next++; });

    reg.EachEntity([&](EntityID id) {
        Entity e(id, &scene);
        o << "\nEntity id=" << fileIds[id.Index] << " name=" << Quote(e.GetName()) << "\n";
        if (auto* t = e.TryGet<TransformComponent>()) {
            o << "  Transform position=" << V3(t->Position) << " rotation=" << V3(t->GetEulerDegrees()) << " scale=" << V3(t->Scale);
            if (reg.IsValid(t->Parent)) o << " parent=" << fileIds[t->Parent.Index];
            o << "\n";
        }
        if (auto* mr = e.TryGet<MeshRendererComponent>()) {
            o << "  MeshRenderer mesh=" << Quote(mr->MeshName.empty() && mr->MeshAsset ? mr->MeshAsset->GetName() : mr->MeshName);
            if (const Material* m = mr->MaterialAsset.get()) {
                o << " color=" << V4(m->Albedo * mr->Tint) << " roughness=" << Num(m->Roughness) << " metallic=" << Num(m->Metallic)
                  << " emissive=" << V3(m->Emissive) << " tiling=" << Num(m->Tiling.x) << "," << Num(m->Tiling.y)
                  << " unlit=" << (m->Unlit ? 1 : 0) << " shadows=" << (m->CastShadows ? 1 : 0);
                if (m->AlbedoMap && !m->AlbedoMap->GetPath().empty()) o << " texture=" << Quote(m->AlbedoMap->GetPath());
            }
            if (!mr->Visible) o << " visible=0";
            o << "\n";
        }
        if (auto* c = e.TryGet<ColliderComponent>()) {
            o << "  Collider shape=" << (c->Shape == ColliderShape::Sphere ? "Sphere" : "Box") << " size=" << V3(c->HalfExtents)
              << " radius=" << Num(c->Radius) << " offset=" << V3(c->Offset) << " trigger=" << (c->IsTrigger ? 1 : 0) << "\n";
        }
        if (auto* rb = e.TryGet<RigidBodyComponent>()) {
            const char* type = rb->Type == BodyType::Static ? "Static" : rb->Type == BodyType::Kinematic ? "Kinematic" : "Dynamic";
            o << "  RigidBody type=" << type << " mass=" << Num(rb->Mass) << " restitution=" << Num(rb->Restitution)
              << " friction=" << Num(rb->Friction) << " damping=" << Num(rb->LinearDamping) << " gravity=" << Num(rb->GravityScale)
              << " velocity=" << V3(rb->Velocity);
            if (rb->LockAxes) {
                o << " lock=";
                if (rb->LockAxes & RigidBodyComponent::LockX) o << "x";
                if (rb->LockAxes & RigidBodyComponent::LockY) o << "y";
                if (rb->LockAxes & RigidBodyComponent::LockZ) o << "z";
            }
            o << "\n";
        }
        if (auto* l = e.TryGet<LightComponent>()) {
            o << "  Light type=" << (l->Type == LightType::Directional ? "Directional" : "Point") << " color=" << V3(l->Color)
              << " intensity=" << Num(l->Intensity) << " range=" << Num(l->Range) << " shadows=" << (l->CastShadows ? 1 : 0) << "\n";
        }
        if (auto* c = e.TryGet<CameraComponent>()) {
            o << "  Camera fov=" << Num(Degrees(c->Settings.FieldOfView)) << " near=" << Num(c->Settings.NearClip)
              << " far=" << Num(c->Settings.FarClip) << " primary=" << (c->Primary ? 1 : 0)
              << " ortho=" << (c->Settings.Type == Camera::Projection::Orthographic ? 1 : 0) << " size=" << Num(c->Settings.OrthoSize) << "\n";
        }
        if (auto* s = e.TryGet<LuauScriptComponent>()) o << "  Script path=" << Quote(s->ScriptPath) << "\n";
    });
    return o.str();
}

bool Save(Scene& scene, const std::string& path) {
    const bool ok = FileSystem::WriteText(path, Serialize(scene));
    if (!ok) LAPIS_ERROR("Scene: could not write '{}'", path);
    return ok;
}

bool Deserialize(Scene& scene, const std::string& text, std::string* error, bool loadAssets) {
    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    Entity current;
    std::unordered_map<u32, EntityID> idMap;
    std::vector<std::pair<EntityID, u32>> parentLinks;

    auto fail = [&](const std::string& msg) {
        const std::string full = Format("scene line {}: {}", lineNo, msg);
        if (error) *error = full;
        LAPIS_ERROR("Scene: {}", full);
        return false;
    };

    while (std::getline(in, raw)) {
        ++lineNo;
        Line l;
        std::string err;
        if (!ParseLine(raw, l, err)) return fail(err);
        if (l.Tag.empty()) continue;

        if (l.Tag == "Scene") { scene.SetName(l.Str("name", scene.GetName())); continue; }
        if (l.Tag == "Environment") {
            Environment& env = scene.Env;
            env.SkyZenith = l.V3("zenith", env.SkyZenith);
            env.SkyHorizon = l.V3("horizon", env.SkyHorizon);
            env.SkyGround = l.V3("ground", env.SkyGround);
            env.AmbientIntensity = l.F("ambient", env.AmbientIntensity);
            env.ReflectionIntensity = l.F("reflection", env.ReflectionIntensity);
            env.FogDensity = l.F("fog", env.FogDensity);
            env.Exposure = l.F("exposure", env.Exposure);
            continue;
        }
        if (l.Tag == "Sun") {
            DirectionalLight& s = scene.DefaultSun;
            s.Direction = l.V3("direction", s.Direction);
            s.Color = l.V3("color", s.Color);
            s.Intensity = l.F("intensity", s.Intensity);
            s.CastShadows = l.B("shadows", s.CastShadows);
            continue;
        }
        if (l.Tag == "Physics") { scene.GetPhysics().Settings.Gravity = l.V3("gravity", scene.GetPhysics().Settings.Gravity); continue; }
        if (l.Tag == "Entity") {
            current = scene.CreateEntity(l.Str("name", "Entity"));
            if (l.Has("id")) idMap[(u32)l.F("id", 0)] = current.GetID();
            continue;
        }

        if (!current) return fail("'" + l.Tag + "' before any Entity line");

        if (l.Tag == "Transform") {
            auto& t = current.Transform();
            t.Position = l.V3("position", t.Position);
            t.SetEulerDegrees(l.V3("rotation", Vec3(0.0f)));
            t.Scale = l.V3("scale", t.Scale);
            if (l.Has("parent")) parentLinks.emplace_back(current.GetID(), (u32)l.F("parent", 0));
        } else if (l.Tag == "MeshRenderer") {
            auto& mr = current.Add<MeshRendererComponent>();
            mr.MeshName = l.Str("mesh", "Cube");
            mr.Visible = l.B("visible", true);
            // Objects with identical material settings share one material (and
            // batch into instanced draws); the color becomes the per-object tint.
            mr.Tint = l.V4("color", Vec4(1.0f));
            const std::string texture = l.Str("texture", "");
            const float roughness = l.F("roughness", 0.5f), metallic = l.F("metallic", 0.0f);
            const Vec3 emissive = l.V3("emissive", Vec3(0.0f));
            const Vec2 tiling = l.V2("tiling", Vec2(1.0f));
            const bool unlit = l.B("unlit", false), shadows = l.B("shadows", true);
            if (loadAssets) {
                mr.MeshAsset = scene.GetAssets().GetMesh(mr.MeshName);
                mr.MaterialAsset = scene.GetAssets().GetSharedMaterial(texture, roughness, metallic, emissive, tiling, unlit, shadows);
            } else {
                auto mat = CreateRef<Material>();
                mat->Roughness = roughness; mat->Metallic = metallic; mat->Emissive = emissive;
                mat->Tiling = tiling; mat->Unlit = unlit; mat->CastShadows = shadows;
                mr.MaterialAsset = mat;
            }
        } else if (l.Tag == "Collider") {
            auto& c = current.Add<ColliderComponent>();
            c.Shape = l.Str("shape", "Box") == "Sphere" ? ColliderShape::Sphere : ColliderShape::Box;
            c.HalfExtents = l.V3("size", c.HalfExtents);
            c.Radius = l.F("radius", c.Radius);
            c.Offset = l.V3("offset", c.Offset);
            c.IsTrigger = l.B("trigger", false);
        } else if (l.Tag == "RigidBody") {
            auto& rb = current.Add<RigidBodyComponent>();
            const std::string type = l.Str("type", "Dynamic");
            rb.Type = type == "Static" ? BodyType::Static : type == "Kinematic" ? BodyType::Kinematic : BodyType::Dynamic;
            rb.Mass = l.F("mass", rb.Mass);
            rb.Restitution = l.F("restitution", rb.Restitution);
            rb.Friction = l.F("friction", rb.Friction);
            rb.LinearDamping = l.F("damping", rb.LinearDamping);
            rb.GravityScale = l.F("gravity", rb.GravityScale);
            rb.Velocity = l.V3("velocity", rb.Velocity);
            for (char c : l.Str("lock", ""))
                rb.LockAxes |= c == 'x' ? RigidBodyComponent::LockX : c == 'y' ? RigidBodyComponent::LockY : c == 'z' ? RigidBodyComponent::LockZ : 0;
        } else if (l.Tag == "Light") {
            auto& li = current.Add<LightComponent>();
            li.Type = l.Str("type", "Point") == "Directional" ? LightType::Directional : LightType::Point;
            li.Color = l.V3("color", li.Color);
            li.Intensity = l.F("intensity", li.Intensity);
            li.Range = l.F("range", li.Range);
            li.CastShadows = l.B("shadows", true);
        } else if (l.Tag == "Camera") {
            auto& c = current.Add<CameraComponent>();
            c.Settings.FieldOfView = Radians(l.F("fov", 60.0f));
            c.Settings.NearClip = l.F("near", c.Settings.NearClip);
            c.Settings.FarClip = l.F("far", c.Settings.FarClip);
            c.Settings.OrthoSize = l.F("size", c.Settings.OrthoSize);
            if (l.B("ortho", false)) c.Settings.Type = Camera::Projection::Orthographic;
            c.Primary = l.B("primary", true);
        } else if (l.Tag == "Script") {
            current.Add<LuauScriptComponent>().ScriptPath = l.Str("path");
        } else {
            LAPIS_WARN("Scene line {}: unknown component '{}' ignored", lineNo, l.Tag);
        }
    }

    for (auto& [child, parentId] : parentLinks) {
        auto it = idMap.find(parentId);
        if (it == idMap.end()) { LAPIS_WARN("Scene: parent id {} not found", parentId); continue; }
        scene.SetParent(scene.GetEntity(child), scene.GetEntity(it->second));
    }
    return true;
}

bool Load(Scene& scene, const std::string& path, bool loadAssets) {
    auto text = FileSystem::ReadText(path);
    if (!text) { LAPIS_ERROR("Scene: cannot read '{}'", path); return false; }
    std::string err;
    const bool ok = Deserialize(scene, *text, &err, loadAssets);
    if (ok) LAPIS_INFO("Scene '{}' loaded from {} ({} entities)", scene.GetName(), path, scene.EntityCount());
    return ok;
}

} // namespace Lapis::SceneSerializer
