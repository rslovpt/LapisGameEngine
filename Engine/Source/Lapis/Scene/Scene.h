#pragma once
// A Scene is a world of entities plus the systems that run on them:
// transforms, native + Luau scripts, physics and rendering submission.

#include <string>
#include <vector>

#include "Lapis/Physics/PhysicsWorld.h"
#include "Lapis/Renderer/Renderer.h"
#include "Lapis/Scene/Components.h"
#include "Lapis/Scene/Entity.h"
#include "Lapis/Scene/Registry.h"

namespace Lapis {

class ScriptEngine;
class AssetManager;

class Scene : NonCopyable {
public:
    explicit Scene(const std::string& name = "Scene");
    ~Scene();

    // --- entities ----------------------------------------------------------------
    Entity CreateEntity(const std::string& name = "Entity");
    void   DestroyEntity(Entity entity);   // deferred to the end of the frame (children too)
    void   FlushPendingDestroys() { FlushDestroyQueue(); }
    Entity FindEntityByName(const std::string& name);
    Entity GetEntity(EntityID id) { return registry.IsValid(id) ? Entity(id, this) : Entity(); }
    void   SetParent(Entity child, Entity parent);
    std::vector<Entity> GetChildren(Entity parent);
    size_t EntityCount() const { return registry.AliveCount(); }

    // --- lifecycle ----------------------------------------------------------------
    void OnStart();                        // starts scripts; call once before the first update
    void OnUpdate(float dt);               // scripts + deferred destruction
    void OnFixedUpdate(float dt);          // physics + collision callbacks
    // interpolation = how far (0..1) the frame is between the last two physics
    // steps; moving bodies are drawn in between for smooth motion.
    void OnRender(Renderer& renderer, float aspect, const CameraView* overrideCamera = nullptr, float interpolation = 1.0f);
    void OnStop();
    bool IsRunning() const { return m_Running; }

    void UpdateTransforms();
    bool GetPrimaryCamera(float aspect, CameraView& out);
    Entity GetPrimaryCameraEntity();

    // --- systems ----------------------------------------------------------------
    Registry&      GetRegistry() { return registry; }
    PhysicsWorld&  GetPhysics() { return m_Physics; }
    ScriptEngine&  GetScripts();                 // creates the Luau VM on first use
    ScriptEngine*  TryGetScripts() const { return m_Scripts.get(); }   // nullptr if no scripts yet
    AssetManager&  GetAssets();

    // Root folder for relative asset/script paths (a project's Content dir).
    void SetAssetRoot(const std::string& root);
    const std::string& GetAssetRoot() const { return m_AssetRoot; }

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    // Lines requested by gameplay code this frame (drawn during OnRender).
    void DebugLine(const Vec3& a, const Vec3& b, const Vec4& color) { m_DebugLines.push_back({a, b, color}); }

    // 2D HUD drawn over the frame (filled by scripts each frame, consumed by the app).
    struct HudCommand { bool IsText; float X, Y, W, H; std::string Text; Vec4 Color; float Scale; };
    void HudText(float x, float y, const std::string& text, const Vec4& color, float scale = 1.0f) { m_Hud.push_back({true, x, y, 0, 0, text, color, scale}); }
    void HudRect(float x, float y, float w, float h, const Vec4& color) { m_Hud.push_back({false, x, y, w, h, {}, color, 1.0f}); }
    std::vector<HudCommand> TakeHud() { return std::move(m_Hud); }
    Vec2 ViewportSize{1280.0f, 720.0f};   // set by the application every frame

    // Per-frame performance numbers (filled by the application, read by
    // scripts through the Luau `Perf` library).
    struct PerfStats {
        float FPS = 0, FrameMs = 0, PhysicsMs = 0, ScriptMs = 0, RenderMs = 0;
        u32 DrawCalls = 0, Triangles = 0, PhysicsSteps = 0;
    } Perf;

    // Folder scripts may read/write through the Luau `File` library.
    // Fraction (0..1) of the way from the previous physics step to the
    // current one at which this frame is drawn. Set by the runtime each frame.
    float InterpolationAlpha = 1.0f;
    // Where an entity is actually drawn this frame (interpolated physics
    // position). Cameras should follow this to stay perfectly smooth.
    Vec3 GetRenderPosition(EntityID id);

    std::string SaveDirectory;           // default: <asset root>/Saved

    Environment Env;
    DirectionalLight DefaultSun;           // used when no directional light entity exists
    bool DebugDrawColliders = false;
    float Time = 0.0f;                     // seconds since OnStart

    Registry registry;

private:
    void FlushDestroyQueue();
    void DestroyRecursive(EntityID id);
    const Mat4& ComputeWorld(EntityID id, u64 frame, int depth);

    struct DebugLineCmd { Vec3 A, B; Vec4 Color; };

    std::string  m_Name;
    std::string  m_AssetRoot = ".";
    std::string  m_DefaultSave;
    PhysicsWorld m_Physics;
    Scope<ScriptEngine> m_Scripts;
    Scope<AssetManager> m_Assets;
    std::vector<EntityID> m_DestroyQueue;
    std::vector<DebugLineCmd> m_DebugLines;
    std::vector<HudCommand> m_Hud;
    u64  m_TransformFrame = 0;
    bool m_Running = false;
};

// --- Entity template implementations (need the full Scene) --------------------
template <typename T, typename... Args> T& Entity::Add(Args&&... args) { return m_Scene->registry.Add<T>(m_ID, std::forward<Args>(args)...); }
template <typename T> T&   Entity::Get() { return m_Scene->registry.Get<T>(m_ID); }
template <typename T> T*   Entity::TryGet() { return m_Scene ? m_Scene->registry.TryGet<T>(m_ID) : nullptr; }
template <typename T> bool Entity::Has() const { return m_Scene && m_Scene->registry.Has<T>(m_ID); }
template <typename T> void Entity::Remove() { m_Scene->registry.Remove<T>(m_ID); }
inline bool Entity::IsValid() const { return m_Scene && m_Scene->registry.IsValid(m_ID); }
inline const std::string& Entity::GetName() const {
    static const std::string none = "<invalid>";
    auto* n = m_Scene ? m_Scene->registry.TryGet<NameComponent>(m_ID) : nullptr;
    return n ? n->Name : none;
}

} // namespace Lapis
