#include "Lapis/Scripting/ScriptEngine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include "Lapis/Assets/AssetManager.h"
#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Input.h"
#include "Lapis/Scene/Scene.h"

namespace Lapis {

namespace {

// ============================================================================
// helpers
// ============================================================================
constexpr const char* EntityMeta = "Lapis.Entity";

ScriptEngine& Engine(lua_State* L) { return *static_cast<ScriptEngine*>(lua_callbacks(L)->userdata); }
Scene& SceneOf(lua_State* L) { return *Engine(L).GetScene(); }

void PushVec(lua_State* L, const Vec3& v) { lua_pushvector(L, v.x, v.y, v.z); }
Vec3 CheckVec(lua_State* L, int idx) { const float* v = luaL_checkvector(L, idx); return {v[0], v[1], v[2]}; }
Vec3 OptVec(lua_State* L, int idx, const Vec3& def) {
    const float* v = lua_tovector(L, idx);
    return v ? Vec3(v[0], v[1], v[2]) : def;
}

void PushEntity(lua_State* L, EntityID id) {
    if (id.IsNull()) { lua_pushnil(L); return; }
    auto* ud = static_cast<u64*>(lua_newuserdata(L, sizeof(u64)));
    *ud = id.Packed();
    luaL_getmetatable(L, EntityMeta);
    lua_setmetatable(L, -2);
}

EntityID ToEntityID(u64 packed) { return {u32(packed & 0xFFFFFFFFu), u32(packed >> 32)}; }

EntityID CheckEntity(lua_State* L, int idx) {
    return ToEntityID(*static_cast<u64*>(luaL_checkudata(L, idx, EntityMeta)));
}

EntityID OptEntity(lua_State* L, int idx) {
    if (lua_isnoneornil(L, idx)) return {};
    return CheckEntity(L, idx);
}

// Valid entity or a Lua error.
Entity CheckLiveEntity(lua_State* L, int idx) {
    Entity e = SceneOf(L).GetEntity(CheckEntity(L, idx));
    if (!e) luaL_error(L, "entity has been destroyed");
    return e;
}

std::string ConcatArgs(lua_State* L, int first) {
    std::string out;
    const int n = lua_gettop(L);
    for (int i = first; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > first) out += '\t';
        out.append(s, len);
        lua_pop(L, 1);
    }
    return out;
}

// Key names used by Input.* ("W", "Space", "Shift", "F1", ...).
int KeyFromName(const std::string& name) {
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    static const std::unordered_map<std::string, Key> names = {
        {"Space", Key::Space}, {"Enter", Key::Enter}, {"Escape", Key::Escape}, {"Tab", Key::Tab},
        {"Backspace", Key::Backspace}, {"Delete", Key::Delete}, {"Shift", Key::Shift},
        {"Control", Key::Control}, {"Ctrl", Key::Control}, {"Alt", Key::Alt},
        {"Left", Key::Left}, {"Right", Key::Right}, {"Up", Key::Up}, {"Down", Key::Down},
        {"F1", Key::F1}, {"F2", Key::F2}, {"F3", Key::F3}, {"F4", Key::F4}, {"F5", Key::F5}, {"F6", Key::F6},
        {"F7", Key::F7}, {"F8", Key::F8}, {"F9", Key::F9}, {"F10", Key::F10}, {"F11", Key::F11}, {"F12", Key::F12}};
    auto it = names.find(name);
    return it == names.end() ? -1 : static_cast<int>(it->second);
}

Key CheckKey(lua_State* L, int idx) {
    const int k = KeyFromName(luaL_checkstring(L, idx));
    if (k < 0) luaL_error(L, "unknown key name '%s'", lua_tostring(L, idx));
    return static_cast<Key>(k);
}

MouseButton CheckMouse(lua_State* L, int idx) {
    if (lua_isnumber(L, idx)) return static_cast<MouseButton>(Clamp((int)lua_tointeger(L, idx), 0, 4));
    const std::string s = luaL_checkstring(L, idx);
    if (s == "Right") return MouseButton::Right;
    if (s == "Middle") return MouseButton::Middle;
    return MouseButton::Left;
}

// ============================================================================
// global functions
// ============================================================================
int L_Print(lua_State* L) { LAPIS_INFO("[Luau] {}", ConcatArgs(L, 1)); return 0; }
int L_Warn(lua_State* L)  { LAPIS_WARN("[Luau] {}", ConcatArgs(L, 1)); return 0; }

// --- Entity metatable ---------------------------------------------------------
Material* EnsureOwnMaterial(Entity e) {
    auto* mr = e.TryGet<MeshRendererComponent>();
    if (!mr) return nullptr;
    if (!mr->MaterialAsset) mr->MaterialAsset = CreateRef<Material>();
    return mr->MaterialAsset.get();
}

int E_Destroy(lua_State* L) { SceneOf(L).DestroyEntity(SceneOf(L).GetEntity(CheckEntity(L, 1))); return 0; }

int E_AddForce(lua_State* L) {
    Entity e = CheckLiveEntity(L, 1);
    if (auto* rb = e.TryGet<RigidBodyComponent>()) rb->AddForce(CheckVec(L, 2));
    return 0;
}

int E_AddImpulse(lua_State* L) {
    Entity e = CheckLiveEntity(L, 1);
    if (auto* rb = e.TryGet<RigidBodyComponent>()) rb->AddImpulse(CheckVec(L, 2));
    return 0;
}

int E_SetColor(lua_State* L) {
    Entity e = CheckLiveEntity(L, 1);
    Vec4 c;
    if (lua_isvector(L, 2)) { Vec3 v = CheckVec(L, 2); c = {v.x, v.y, v.z, (float)luaL_optnumber(L, 3, 1.0)}; }
    else c = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4), (float)luaL_optnumber(L, 5, 1.0)};
    if (Material* m = EnsureOwnMaterial(e)) m->Albedo = c;
    return 0;
}

int E_SetEmissive(lua_State* L) {
    Entity e = CheckLiveEntity(L, 1);
    if (Material* m = EnsureOwnMaterial(e)) m->Emissive = CheckVec(L, 2);
    return 0;
}

int E_LookAt(lua_State* L) {
    Entity e = CheckLiveEntity(L, 1);
    auto& t = e.Transform();
    const Vec3 dir = Normalize(CheckVec(L, 2) - t.Position);
    if (LengthSq(dir) < 1e-8f) return 0;
    const float yaw = std::atan2(-dir.x, -dir.z);
    const float pitch = std::asin(Clamp(dir.y, -1.0f, 1.0f));
    t.Rotation = Quat::FromEuler({pitch, yaw, 0.0f});
    return 0;
}

int E_SetParent(lua_State* L) {
    Scene& s = SceneOf(L);
    s.SetParent(CheckLiveEntity(L, 1), s.GetEntity(OptEntity(L, 2)));
    return 0;
}

int E_GetChildren(lua_State* L) {
    Scene& s = SceneOf(L);
    auto children = s.GetChildren(CheckLiveEntity(L, 1));
    lua_createtable(L, (int)children.size(), 0);
    for (size_t i = 0; i < children.size(); ++i) { PushEntity(L, children[i].GetID()); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}

int E_Index(lua_State* L) {
    const EntityID id = CheckEntity(L, 1);
    const std::string key = luaL_checkstring(L, 2);
    Scene& scene = SceneOf(L);
    Entity e = scene.GetEntity(id);

    // Methods (valid even on destroyed entities so `e:Destroy()` is harmless).
    static const std::unordered_map<std::string, lua_CFunction> methods = {
        {"Destroy", E_Destroy}, {"AddForce", E_AddForce}, {"AddImpulse", E_AddImpulse},
        {"SetColor", E_SetColor}, {"SetEmissive", E_SetEmissive}, {"LookAt", E_LookAt},
        {"SetParent", E_SetParent}, {"GetChildren", E_GetChildren}};
    if (auto it = methods.find(key); it != methods.end()) { lua_pushcfunction(L, it->second, key.c_str()); return 1; }

    if (key == "IsValid") { lua_pushboolean(L, (bool)e); return 1; }
    if (key == "Id") { lua_pushnumber(L, id.Index); return 1; }
    if (!e) luaL_error(L, "cannot read '%s': entity has been destroyed", key.c_str());

    auto& t = e.Transform();
    if (key == "Name") { lua_pushstring(L, e.GetName().c_str()); return 1; }
    if (key == "Position") { PushVec(L, t.Position); return 1; }
    if (key == "Rotation") { PushVec(L, t.GetEulerDegrees()); return 1; }
    if (key == "Scale") { PushVec(L, t.Scale); return 1; }
    if (key == "WorldPosition") { PushVec(L, t.WorldPosition()); return 1; }
    if (key == "Forward") { PushVec(L, t.Forward()); return 1; }
    if (key == "Right") { PushVec(L, t.Right()); return 1; }
    if (key == "Up") { PushVec(L, t.Up()); return 1; }
    if (key == "Velocity") { auto* rb = e.TryGet<RigidBodyComponent>(); PushVec(L, rb ? rb->Velocity : Vec3(0.0f)); return 1; }
    if (key == "Parent") { PushEntity(L, scene.GetEntity(t.Parent) ? t.Parent : EntityID{}); return 1; }
    if (key == "Visible") { auto* mr = e.TryGet<MeshRendererComponent>(); lua_pushboolean(L, mr && mr->Visible); return 1; }
    if (key == "Script") {
        auto* sc = e.TryGet<LuauScriptComponent>();
        if (sc && sc->InstanceRef >= 0) lua_getref(L, sc->InstanceRef); else lua_pushnil(L);
        return 1;
    }
    luaL_error(L, "Entity has no member '%s'", key.c_str());
    return 0;
}

int E_NewIndex(lua_State* L) {
    Entity e = CheckLiveEntity(L, 1);
    const std::string key = luaL_checkstring(L, 2);
    auto& t = e.Transform();
    auto wake = [&] { if (auto* rb = e.TryGet<RigidBodyComponent>()) { rb->Sleeping = false; rb->SleepTimer = 0.0f; } };

    if (key == "Position") { t.Teleport(CheckVec(L, 3)); wake(); }
    else if (key == "Rotation") t.SetEulerDegrees(CheckVec(L, 3));
    else if (key == "Scale") { t.Scale = lua_isnumber(L, 3) ? Vec3((float)lua_tonumber(L, 3)) : CheckVec(L, 3); wake(); }
    else if (key == "Velocity") { if (auto* rb = e.TryGet<RigidBodyComponent>()) { rb->Velocity = CheckVec(L, 3); wake(); } }
    else if (key == "Name") e.Get<NameComponent>().Name = luaL_checkstring(L, 3);
    else if (key == "Visible") { if (auto* mr = e.TryGet<MeshRendererComponent>()) mr->Visible = lua_toboolean(L, 3) != 0; }
    else luaL_error(L, "Entity member '%s' is read-only or unknown", key.c_str());
    return 0;
}

int E_Eq(lua_State* L) { lua_pushboolean(L, CheckEntity(L, 1) == CheckEntity(L, 2)); return 1; }

int E_ToString(lua_State* L) {
    Entity e = SceneOf(L).GetEntity(CheckEntity(L, 1));
    lua_pushstring(L, e ? ("Entity(" + e.GetName() + ")").c_str() : "Entity(<destroyed>)");
    return 1;
}

// --- Scene ------------------------------------------------------------------------
std::string OptStringField(lua_State* L, int t, const char* k, const std::string& def) {
    lua_getfield(L, t, k);
    std::string r = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return r;
}
double OptNumberField(lua_State* L, int t, const char* k, double def) {
    lua_getfield(L, t, k);
    double r = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return r;
}
bool OptBoolField(lua_State* L, int t, const char* k, bool def) {
    lua_getfield(L, t, k);
    bool r = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : def;
    lua_pop(L, 1);
    return r;
}
Vec3 OptVecField(lua_State* L, int t, const char* k, const Vec3& def) {
    lua_getfield(L, t, k);
    Vec3 r = def;
    if (lua_isnumber(L, -1)) r = Vec3((float)lua_tonumber(L, -1));
    else if (const float* v = lua_tovector(L, -1)) r = {v[0], v[1], v[2]};
    lua_pop(L, 1);
    return r;
}
bool HasField(lua_State* L, int t, const char* k) {
    lua_getfield(L, t, k);
    const bool has = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return has;
}

int S_Find(lua_State* L) {
    Entity e = SceneOf(L).FindEntityByName(luaL_checkstring(L, 1));
    PushEntity(L, e ? e.GetID() : EntityID{});
    return 1;
}

int S_Destroy(lua_State* L) { SceneOf(L).DestroyEntity(SceneOf(L).GetEntity(CheckEntity(L, 1))); return 0; }

int S_GetCamera(lua_State* L) {
    Entity e = SceneOf(L).GetPrimaryCameraEntity();
    PushEntity(L, e ? e.GetID() : EntityID{});
    return 1;
}

int S_Spawn(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Scene& scene = SceneOf(L);
    Entity e = scene.CreateEntity(OptStringField(L, 1, "Name", "Entity"));
    auto& t = e.Transform();
    t.Position = OptVecField(L, 1, "Position", Vec3(0.0f));
    t.SetEulerDegrees(OptVecField(L, 1, "Rotation", Vec3(0.0f)));
    t.Scale = OptVecField(L, 1, "Scale", Vec3(1.0f));

    const std::string mesh = OptStringField(L, 1, "Mesh", "");
    if (!mesh.empty()) {
        auto& mr = e.Add<MeshRendererComponent>();
        mr.MeshName = mesh;
        mr.MeshAsset = scene.GetAssets().GetMesh(mesh);
        auto mat = CreateRef<Material>();
        const Vec3 color = OptVecField(L, 1, "Color", Vec3(0.8f));
        mat->Albedo = {color.x, color.y, color.z, 1.0f};
        mat->Roughness = (float)OptNumberField(L, 1, "Roughness", 0.5);
        mat->Metallic = (float)OptNumberField(L, 1, "Metallic", 0.0);
        mat->Emissive = OptVecField(L, 1, "Emissive", Vec3(0.0f));
        mat->Unlit = OptBoolField(L, 1, "Unlit", false);
        mat->CastShadows = OptBoolField(L, 1, "CastShadows", true);
        const std::string tex = OptStringField(L, 1, "Texture", "");
        if (!tex.empty()) mat->AlbedoMap = scene.GetAssets().GetTexture(tex);
        const float tiling = (float)OptNumberField(L, 1, "Tiling", 1.0);
        mat->Tiling = {tiling, tiling};
        mr.MaterialAsset = mat;
    }

    const std::string physics = OptStringField(L, 1, "Physics", "");
    std::string collider = OptStringField(L, 1, "Collider", "");
    if (!physics.empty() && collider.empty()) collider = mesh == "Sphere" ? "Sphere" : "Box";
    if (!collider.empty()) {
        auto& c = e.Add<ColliderComponent>();
        c.Shape = collider == "Sphere" ? ColliderShape::Sphere : ColliderShape::Box;
        c.IsTrigger = OptBoolField(L, 1, "Trigger", false);
    }
    if (!physics.empty()) {
        auto& rb = e.Add<RigidBodyComponent>();
        rb.Type = physics == "Static" ? BodyType::Static : physics == "Kinematic" ? BodyType::Kinematic : BodyType::Dynamic;
        rb.Mass = (float)OptNumberField(L, 1, "Mass", 1.0);
        rb.Restitution = (float)OptNumberField(L, 1, "Restitution", 0.3);
        rb.Friction = (float)OptNumberField(L, 1, "Friction", 0.5);
        rb.Velocity = OptVecField(L, 1, "Velocity", Vec3(0.0f));
        rb.GravityScale = (float)OptNumberField(L, 1, "GravityScale", 1.0);
    }

    const std::string light = OptStringField(L, 1, "Light", "");
    if (!light.empty()) {
        auto& l = e.Add<LightComponent>();
        l.Type = light == "Directional" ? LightType::Directional : LightType::Point;
        l.Color = OptVecField(L, 1, "LightColor", Vec3(1.0f));
        l.Intensity = (float)OptNumberField(L, 1, "Intensity", l.Type == LightType::Point ? 10.0 : 3.0);
        l.Range = (float)OptNumberField(L, 1, "Range", 10.0);
        l.CastShadows = OptBoolField(L, 1, "CastShadows", true);
    }

    if (OptBoolField(L, 1, "Camera", false)) {
        auto& cam = e.Add<CameraComponent>();
        cam.Settings.FieldOfView = Radians((float)OptNumberField(L, 1, "FieldOfView", 60.0));
    }

    if (HasField(L, 1, "Parent")) {
        lua_getfield(L, 1, "Parent");
        scene.SetParent(e, scene.GetEntity(OptEntity(L, -1)));
        lua_pop(L, 1);
    }

    const std::string script = OptStringField(L, 1, "Script", "");
    if (!script.empty()) {
        e.Add<LuauScriptComponent>().ScriptPath = script;
        if (scene.IsRunning()) Engine(L).StartEntity(e.GetID());
    }

    PushEntity(L, e.GetID());
    return 1;
}

int S_GetEntities(lua_State* L) {
    Scene& s = SceneOf(L);
    lua_newtable(L);
    int i = 1;
    s.GetRegistry().EachEntity([&](EntityID id) { PushEntity(L, id); lua_rawseti(L, -2, i++); });
    return 1;
}

int S_SetSun(lua_State* L) {
    SceneOf(L).DefaultSun.Direction = CheckVec(L, 1);
    if (lua_gettop(L) >= 2) SceneOf(L).DefaultSun.Intensity = (float)luaL_checknumber(L, 2);
    return 0;
}
int S_SetFog(lua_State* L) { SceneOf(L).Env.FogDensity = (float)luaL_checknumber(L, 1); return 0; }
int S_SetExposure(lua_State* L) { SceneOf(L).Env.Exposure = (float)luaL_checknumber(L, 1); return 0; }
int S_SetSky(lua_State* L) {
    Scene& s = SceneOf(L);
    s.Env.SkyZenith = CheckVec(L, 1);
    s.Env.SkyHorizon = OptVec(L, 2, s.Env.SkyHorizon);
    s.Env.SkyGround = OptVec(L, 3, s.Env.SkyGround);
    return 0;
}
int S_SetDebugColliders(lua_State* L) { SceneOf(L).DebugDrawColliders = lua_toboolean(L, 1) != 0; return 0; }

// --- Input ------------------------------------------------------------------------
int I_IsKeyDown(lua_State* L)      { lua_pushboolean(L, Input::IsKeyDown(CheckKey(L, 1))); return 1; }
int I_WasKeyPressed(lua_State* L)  { lua_pushboolean(L, Input::WasKeyPressed(CheckKey(L, 1))); return 1; }
int I_WasKeyReleased(lua_State* L) { lua_pushboolean(L, Input::WasKeyReleased(CheckKey(L, 1))); return 1; }
int I_IsMouseDown(lua_State* L)    { lua_pushboolean(L, Input::IsMouseDown(CheckMouse(L, 1))); return 1; }
int I_WasMousePressed(lua_State* L){ lua_pushboolean(L, Input::WasMousePressed(CheckMouse(L, 1))); return 1; }
int I_GetMousePosition(lua_State* L) { Vec2 p = Input::GetMousePosition(); lua_pushvector(L, p.x, p.y, 0); return 1; }
int I_GetMouseDelta(lua_State* L)    { Vec2 d = Input::GetMouseDelta(); lua_pushvector(L, d.x, d.y, 0); return 1; }
int I_GetScroll(lua_State* L)        { lua_pushnumber(L, Input::GetScrollDelta()); return 1; }

// --- Physics ------------------------------------------------------------------------
int P_Raycast(lua_State* L) {
    Scene& s = SceneOf(L);
    const Ray ray{CheckVec(L, 1), Normalize(CheckVec(L, 2))};
    const float maxDist = (float)luaL_optnumber(L, 3, 1000.0);
    auto hit = s.GetPhysics().Raycast(s.GetRegistry(), ray, maxDist, OptEntity(L, 4));
    if (!hit) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 4);
    PushEntity(L, hit->Entity); lua_setfield(L, -2, "Entity");
    PushVec(L, hit->Point);     lua_setfield(L, -2, "Position");
    PushVec(L, hit->Normal);    lua_setfield(L, -2, "Normal");
    lua_pushnumber(L, hit->Distance); lua_setfield(L, -2, "Distance");
    return 1;
}
int P_OverlapSphere(lua_State* L) {
    Scene& s = SceneOf(L);
    auto ids = s.GetPhysics().OverlapSphere(s.GetRegistry(), CheckVec(L, 1), (float)luaL_checknumber(L, 2));
    lua_createtable(L, (int)ids.size(), 0);
    for (size_t i = 0; i < ids.size(); ++i) { PushEntity(L, ids[i]); lua_rawseti(L, -2, (int)i + 1); }
    return 1;
}
int P_SetGravity(lua_State* L) { SceneOf(L).GetPhysics().Settings.Gravity = CheckVec(L, 1); return 0; }
int P_GetGravity(lua_State* L) { PushVec(L, SceneOf(L).GetPhysics().Settings.Gravity); return 1; }

// --- Debug / Time / App -------------------------------------------------------------
int D_DrawLine(lua_State* L) {
    const Vec3 c = OptVec(L, 3, Vec3(1, 1, 0));
    SceneOf(L).DebugLine(CheckVec(L, 1), CheckVec(L, 2), {c.x, c.y, c.z, 1.0f});
    return 0;
}
int H_Text(lua_State* L) {
    const Vec3 c = OptVec(L, 4, Vec3(1.0f));
    SceneOf(L).HudText((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), luaL_checkstring(L, 3),
                       {c.x, c.y, c.z, (float)luaL_optnumber(L, 6, 1.0)}, (float)luaL_optnumber(L, 5, 1.0));
    return 0;
}
int H_Rect(lua_State* L) {
    const Vec3 c = OptVec(L, 5, Vec3(1.0f));
    SceneOf(L).HudRect((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                       (float)luaL_checknumber(L, 4), {c.x, c.y, c.z, (float)luaL_optnumber(L, 6, 1.0)});
    return 0;
}
int H_ScreenSize(lua_State* L) { const Vec2 s = SceneOf(L).ViewportSize; lua_pushvector(L, s.x, s.y, 0); return 1; }
// --- Perf -------------------------------------------------------------------------
int Perf_Get(lua_State* L) {
    Scene& s = SceneOf(L);
    const auto& p = s.Perf;
    size_t bodies = s.GetRegistry().Count<RigidBodyComponent>();
    lua_createtable(L, 0, 12);
    auto num = [&](const char* k, double v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); };
    num("FPS", p.FPS); num("FrameMs", p.FrameMs); num("PhysicsMs", p.PhysicsMs); num("ScriptMs", p.ScriptMs);
    num("RenderMs", p.RenderMs); num("DrawCalls", p.DrawCalls); num("Triangles", p.Triangles);
    num("PhysicsSteps", p.PhysicsSteps); num("Entities", (double)s.EntityCount()); num("Bodies", (double)bodies);
    num("Contacts", (double)s.GetPhysics().GetContactCount()); num("Time", s.Time);
    return 1;
}
int Perf_FPS(lua_State* L) { lua_pushnumber(L, SceneOf(L).Perf.FPS); return 1; }

// --- File (sandboxed to the scene's save folder) ----------------------------------
std::filesystem::path SafeSavePath(lua_State* L, int idx) {
    const std::string rel = luaL_checkstring(L, idx);
    std::filesystem::path p(rel);
    bool ok = !rel.empty() && !p.is_absolute() && !p.has_root_name() && rel.find(':') == std::string::npos;
    for (const auto& part : p) if (part == "..") ok = false;
    if (!ok) luaL_error(L, "File: '%s' is not allowed (use a relative path inside the Saved folder)", rel.c_str());
    return std::filesystem::path(SceneOf(L).SaveDirectory) / p;
}
bool WriteFile(lua_State* L, bool append) {
    const auto path = SafeSavePath(L, 1);
    size_t len = 0;
    const char* text = luaL_checklstring(L, 2, &len);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::FILE* f = std::fopen(path.string().c_str(), append ? "ab" : "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text, 1, len, f) == len;
    std::fclose(f);
    return ok;
}
int F_Write(lua_State* L)  { lua_pushboolean(L, WriteFile(L, false)); return 1; }
int F_Append(lua_State* L) { lua_pushboolean(L, WriteFile(L, true)); return 1; }
int F_Read(lua_State* L) {
    auto text = FileSystem::ReadText(SafeSavePath(L, 1).string());
    if (text) lua_pushlstring(L, text->data(), text->size()); else lua_pushnil(L);
    return 1;
}
int F_Exists(lua_State* L) { std::error_code ec; lua_pushboolean(L, std::filesystem::exists(SafeSavePath(L, 1), ec)); return 1; }
int F_Delete(lua_State* L) { std::error_code ec; lua_pushboolean(L, std::filesystem::remove(SafeSavePath(L, 1), ec)); return 1; }
int F_List(lua_State* L) {
    std::filesystem::path dir = SceneOf(L).SaveDirectory;
    if (!lua_isnoneornil(L, 1)) dir = SafeSavePath(L, 1);
    lua_newtable(L);
    std::error_code ec;
    int i = 1;
    for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
        lua_pushstring(L, e.path().filename().string().c_str());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}
int F_SaveFolder(lua_State* L) {
    lua_pushstring(L, std::filesystem::absolute(SceneOf(L).SaveDirectory).lexically_normal().string().c_str());
    return 1;
}

int T_Now(lua_State* L) { lua_pushnumber(L, SceneOf(L).Time); return 1; }
int A_Quit(lua_State* L) { Engine(L).QuitRequested = true; return 0; }
int A_SetTitle(lua_State* L) { Engine(L).RequestedTitle = luaL_checkstring(L, 1); return 0; }
int A_LockCursor(lua_State* L) { Engine(L).CursorLockRequest = lua_toboolean(L, 1) ? 1 : 0; return 0; }

void RegisterLib(lua_State* L, const char* name, std::initializer_list<luaL_Reg> funcs) {
    lua_createtable(L, 0, (int)funcs.size());
    for (const luaL_Reg& r : funcs) { lua_pushcfunction(L, r.func, r.name); lua_setfield(L, -2, r.name); }
    lua_setglobal(L, name);
}

// Pure-Luau helpers available to every script.
const char* Prelude = R"(
Vector3 = {
    new = function(x, y, z) return vector.create(x or 0, y or 0, z or 0) end,
    zero = vector.create(0, 0, 0),
    one = vector.create(1, 1, 1),
    up = vector.create(0, 1, 0),
    right = vector.create(1, 0, 0),
    forward = vector.create(0, 0, -1),
    Length = vector.magnitude,
    Normalize = vector.normalize,
    Dot = vector.dot,
    Cross = vector.cross,
    Lerp = function(a, b, t) return a + (b - a) * t end,
    Distance = function(a, b) return vector.magnitude(a - b) end,
}
Color = {
    new = function(r, g, b) return vector.create(r, g, b) end,
    White = vector.create(1, 1, 1), Black = vector.create(0, 0, 0),
    Red = vector.create(0.9, 0.15, 0.12), Green = vector.create(0.2, 0.8, 0.25),
    Blue = vector.create(0.15, 0.35, 0.95), Yellow = vector.create(1, 0.85, 0.2),
    Orange = vector.create(1, 0.5, 0.1), Purple = vector.create(0.6, 0.25, 0.9),
    Gray = vector.create(0.5, 0.5, 0.5),
    HSV = function(h, s, v)
        local i = math.floor(h * 6); local f = h * 6 - i
        local p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
        i = i % 6
        if i == 0 then return vector.create(v, t, p) elseif i == 1 then return vector.create(q, v, p)
        elseif i == 2 then return vector.create(p, v, t) elseif i == 3 then return vector.create(p, q, v)
        elseif i == 4 then return vector.create(t, p, v) else return vector.create(v, p, q) end
    end,
}
)";

} // namespace

// ============================================================================
// ScriptEngine
// ============================================================================
ScriptEngine::ScriptEngine(Scene* scene) : m_Scene(scene) {
    m_L = luaL_newstate();
    lua_callbacks(m_L)->userdata = this;
    luaL_openlibs(m_L);

    lua_pushcfunction(m_L, L_Print, "print"); lua_setglobal(m_L, "print");
    lua_pushcfunction(m_L, L_Warn, "warn");   lua_setglobal(m_L, "warn");

    luaL_newmetatable(m_L, EntityMeta);
    lua_pushcfunction(m_L, E_Index, "Entity.__index");       lua_setfield(m_L, -2, "__index");
    lua_pushcfunction(m_L, E_NewIndex, "Entity.__newindex"); lua_setfield(m_L, -2, "__newindex");
    lua_pushcfunction(m_L, E_Eq, "Entity.__eq");             lua_setfield(m_L, -2, "__eq");
    lua_pushcfunction(m_L, E_ToString, "Entity.__tostring"); lua_setfield(m_L, -2, "__tostring");
    lua_pushstring(m_L, "Entity");                           lua_setfield(m_L, -2, "__type");
    lua_pop(m_L, 1);

    RegisterLib(m_L, "Scene", {{"Find", S_Find}, {"Spawn", S_Spawn}, {"Destroy", S_Destroy},
                               {"GetCamera", S_GetCamera}, {"GetEntities", S_GetEntities},
                               {"SetSun", S_SetSun}, {"SetFog", S_SetFog}, {"SetExposure", S_SetExposure},
                               {"SetSky", S_SetSky}, {"SetDebugColliders", S_SetDebugColliders}});
    RegisterLib(m_L, "Input", {{"IsKeyDown", I_IsKeyDown}, {"WasKeyPressed", I_WasKeyPressed},
                               {"WasKeyReleased", I_WasKeyReleased}, {"IsMouseDown", I_IsMouseDown},
                               {"WasMousePressed", I_WasMousePressed}, {"GetMousePosition", I_GetMousePosition},
                               {"GetMouseDelta", I_GetMouseDelta}, {"GetScroll", I_GetScroll}});
    RegisterLib(m_L, "Physics", {{"Raycast", P_Raycast}, {"OverlapSphere", P_OverlapSphere},
                                 {"SetGravity", P_SetGravity}, {"GetGravity", P_GetGravity}});
    RegisterLib(m_L, "Debug", {{"Log", L_Print}, {"Warn", L_Warn}, {"DrawLine", D_DrawLine}});
    RegisterLib(m_L, "Time", {{"Now", T_Now}});
    RegisterLib(m_L, "Perf", {{"Get", Perf_Get}, {"FPS", Perf_FPS}});
    RegisterLib(m_L, "File", {{"Write", F_Write}, {"Append", F_Append}, {"Read", F_Read}, {"Exists", F_Exists},
                              {"Delete", F_Delete}, {"List", F_List}, {"SaveFolder", F_SaveFolder}});
    RegisterLib(m_L, "HUD", {{"Text", H_Text}, {"Rect", H_Rect}, {"ScreenSize", H_ScreenSize}});
    RegisterLib(m_L, "App", {{"Quit", A_Quit}, {"SetTitle", A_SetTitle}, {"LockCursor", A_LockCursor}});

    // The prelude defines real globals, so it runs on the main state (not sandboxed).
    {
        size_t size = 0;
        char* bytecode = luau_compile(Prelude, std::strlen(Prelude), nullptr, &size);
        if (luau_load(m_L, "=prelude", bytecode, size, 0) != 0 || lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
            ReportError(lua_tostring(m_L, -1));
            lua_pop(m_L, 1);
        }
        std::free(bytecode);
    }

    // Make all globals/builtins read-only; each script gets its own
    // writable environment via luaL_sandboxthread.
    luaL_sandbox(m_L);
}

ScriptEngine::~ScriptEngine() {
    if (m_L) lua_close(m_L);
}

void ScriptEngine::ReportError(const std::string& message) {
    LAPIS_ERROR("[Luau] {}", message);
    m_Errors.push_back(message);
}

bool ScriptEngine::RunString(const std::string& source, const std::string& chunkName) {
    size_t size = 0;
    char* bytecode = luau_compile(source.data(), source.size(), nullptr, &size);
    lua_State* T = lua_newthread(m_L);
    luaL_sandboxthread(T);
    const int loadStatus = luau_load(T, chunkName.c_str(), bytecode, size, 0);
    std::free(bytecode);
    bool ok = loadStatus == 0;
    if (!ok) ReportError(lua_tostring(T, -1));
    else if (lua_pcall(T, 0, 0, 0) != LUA_OK) { ReportError(lua_tostring(T, -1)); ok = false; }
    lua_pop(m_L, 1); // thread
    return ok;
}

bool ScriptEngine::RunFile(const std::string& path) {
    auto src = m_Scene->GetAssets().ReadText(path);
    if (!src) { ReportError("cannot read script '" + path + "'"); return false; }
    return RunString(*src, "@" + path);
}

int ScriptEngine::LoadModule(const std::string& path) {
    if (auto it = m_Modules.find(path); it != m_Modules.end()) return it->second;

    auto src = m_Scene->GetAssets().ReadText(path);
    if (!src) { ReportError("cannot read script '" + path + "'"); m_Modules[path] = -1; return -1; }

    size_t size = 0;
    char* bytecode = luau_compile(src->data(), src->size(), nullptr, &size);
    lua_State* T = lua_newthread(m_L);
    luaL_sandboxthread(T);
    const std::string chunk = "@" + path;
    const int status = luau_load(T, chunk.c_str(), bytecode, size, 0);
    std::free(bytecode);

    int ref = -1;
    if (status != 0) {
        ReportError(lua_tostring(T, -1));
    } else if (lua_pcall(T, 0, 1, 0) != LUA_OK) {
        ReportError(lua_tostring(T, -1));
    } else if (!lua_istable(T, -1)) {
        ReportError("script '" + path + "' must return a table");
    } else {
        ref = lua_ref(T, -1);
    }
    lua_pop(m_L, 1); // thread
    m_Modules[path] = ref;
    return ref;
}

bool ScriptEngine::CallMethod(int instanceRef, const char* method, int nargs, EntityID entity) {
    // Stack on entry: nargs arguments. Calls instance:method(args...).
    lua_getref(m_L, instanceRef);                 // args..., self
    lua_getfield(m_L, -1, method);                // args..., self, fn
    if (!lua_isfunction(m_L, -1)) { lua_pop(m_L, 2 + nargs); return true; }
    lua_insert(m_L, -(nargs + 2));                // fn, args..., self
    lua_insert(m_L, -(nargs + 1));                // fn, self, args...
    if (lua_pcall(m_L, nargs + 1, 0, 0) != LUA_OK) {
        Entity e = m_Scene->GetEntity(entity);
        ReportError(Format("{} ({}:{})", lua_tostring(m_L, -1), e ? e.GetName() : "?", method));
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

void ScriptEngine::StartEntity(EntityID id) {
    auto* sc = m_Scene->GetRegistry().TryGet<LuauScriptComponent>(id);
    if (!sc || sc->Started) return;
    sc->Started = true;

    const int module = LoadModule(sc->ScriptPath);
    if (module < 0) { sc->Failed = true; return; }

    // instance = setmetatable({ Entity = e }, { __index = module })
    lua_newtable(m_L);
    PushEntity(m_L, id);
    lua_setfield(m_L, -2, "Entity");
    lua_newtable(m_L);
    lua_getref(m_L, module);
    lua_setfield(m_L, -2, "__index");
    lua_setmetatable(m_L, -2);
    sc->InstanceRef = lua_ref(m_L, -1);
    lua_pop(m_L, 1);

    if (!CallMethod(sc->InstanceRef, "OnStart", 0, id)) {
        // keep running OnUpdate anyway; errors are reported per call
    }
}

void ScriptEngine::UpdateEntities(float dt) {
    m_Scene->GetRegistry().Each<LuauScriptComponent>([&](EntityID id, LuauScriptComponent& sc) {
        if (!sc.Started) StartEntity(id);
        auto* live = m_Scene->GetRegistry().TryGet<LuauScriptComponent>(id); // StartEntity may reallocate
        if (!live || live->InstanceRef < 0 || live->Failed) return;
        lua_pushnumber(m_L, dt);
        CallMethod(live->InstanceRef, "OnUpdate", 1, id);
    });
}

void ScriptEngine::DispatchCollision(EntityID self, EntityID other) {
    auto* sc = m_Scene->GetRegistry().TryGet<LuauScriptComponent>(self);
    if (!sc || sc->InstanceRef < 0) return;
    PushEntity(m_L, other);
    CallMethod(sc->InstanceRef, "OnCollision", 1, self);
}

void ScriptEngine::DestroyEntity(EntityID id) {
    auto* sc = m_Scene->GetRegistry().TryGet<LuauScriptComponent>(id);
    if (!sc || sc->InstanceRef < 0) return;
    const int ref = sc->InstanceRef;
    CallMethod(ref, "OnDestroy", 0, id);
    lua_unref(m_L, ref);
    if (auto* again = m_Scene->GetRegistry().TryGet<LuauScriptComponent>(id)) again->InstanceRef = -1;
}

} // namespace Lapis
