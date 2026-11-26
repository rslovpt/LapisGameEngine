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
    Entity FindEntityByName(const std::string& name);
    Entity GetEntity(EntityID id) { return registry.IsValid(id) ? Entity(id, this) : Entity(); }
    void   SetParent(Entity child, Entity parent);
    std::vector<Entity> GetChildren(Entity parent);
    size_t EntityCount() const { return registry.AliveCount(); }

    // --- lifecycle ----------------------------------------------------------------
    void OnStart();                        // starts scripts; call once before the first update
    void OnUpdate(float dt);               // scripts + deferred destruction
    void OnFixedUpdate(float dt);          // physics + collision callbacks
    void OnRender(Renderer& renderer, float aspect, const CameraView* overrideCamera = nullptr);
    void OnStop();
    bool IsRunning() const { return m_Running; }

    void UpdateTransforms();
    bool GetPrimaryCamera(float aspect, CameraView& out);
    Entity GetPrimaryCameraEntity();

    // --- systems ----------------------------------------------------------------
    Registry&      GetRegistry() { return registry; }
    PhysicsWorld&  GetPhysics() { return m_Physics; }
    ScriptEngine&  GetScripts();
    AssetManager&  GetAssets();

    // Root folder for relative asset/script paths (a project's Content dir).
    void SetAssetRoot(const std::string& root);
    const std::string& GetAssetRoot() const { return m_AssetRoot; }

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    // Lines requested by gameplay code this frame (drawn during OnRender).
    void DebugLine(const Vec3& a, const Vec3& b, const Vec4& color) { m_DebugLines.push_back({a, b, color}); }

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
    PhysicsWorld m_Physics;
    Scope<ScriptEngine> m_Scripts;
    Scope<AssetManager> m_Assets;
    std::vector<EntityID> m_DestroyQueue;
    std::vector<DebugLineCmd> m_DebugLines;
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
