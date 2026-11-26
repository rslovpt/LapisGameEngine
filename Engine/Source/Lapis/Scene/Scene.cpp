#include "Lapis/Scene/Scene.h"

#include <algorithm>
#include <filesystem>

#include "Lapis/Assets/AssetManager.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Scene/ScriptableEntity.h"
#include "Lapis/Scripting/ScriptEngine.h"

namespace Lapis {

Scene::Scene(const std::string& name) : m_Name(name) {}

Scene::~Scene() {
    if (m_Running) OnStop();
    // Scripts hold Lua refs to entities; tear them down before the registry.
    m_Scripts.reset();
}

ScriptEngine& Scene::GetScripts() {
    if (!m_Scripts) m_Scripts = CreateScope<ScriptEngine>(this);
    return *m_Scripts;
}

AssetManager& Scene::GetAssets() {
    if (!m_Assets) m_Assets = CreateScope<AssetManager>(m_AssetRoot);
    return *m_Assets;
}

void Scene::SetAssetRoot(const std::string& root) {
    m_AssetRoot = root;
    if (SaveDirectory.empty() || SaveDirectory == m_DefaultSave) {
        m_DefaultSave = (std::filesystem::path(root) / "Saved").lexically_normal().string();
        SaveDirectory = m_DefaultSave;
    }
    if (m_Assets) m_Assets->SetRoot(root);
}

// --- entities ---------------------------------------------------------------------
Entity Scene::CreateEntity(const std::string& name) {
    Entity e(registry.Create(), this);
    e.Add<NameComponent>(NameComponent{name});
    e.Add<TransformComponent>();
    return e;
}

void Scene::DestroyEntity(Entity entity) {
    if (!entity || entity.GetScene() != this) return;
    m_DestroyQueue.push_back(entity.GetID());
}

void Scene::DestroyRecursive(EntityID id) {
    if (!registry.IsValid(id)) return;
    for (Entity child : GetChildren(Entity(id, this))) DestroyRecursive(child.GetID());

    if (auto* ns = registry.TryGet<NativeScriptComponent>(id); ns && ns->Instance) {
        auto instance = ns->Instance; // keep alive during OnDestroy
        instance->OnDestroy();
    }
    if (m_Scripts && registry.Has<LuauScriptComponent>(id)) m_Scripts->DestroyEntity(id);
    registry.Destroy(id);
}

void Scene::FlushDestroyQueue() {
    // Destruction callbacks may queue more entities; loop until stable.
    while (!m_DestroyQueue.empty()) {
        auto queue = std::move(m_DestroyQueue);
        m_DestroyQueue.clear();
        for (EntityID id : queue) DestroyRecursive(id);
    }
}

Entity Scene::FindEntityByName(const std::string& name) {
    Entity found;
    registry.Each<NameComponent>([&](EntityID id, NameComponent& n) {
        if (!found && n.Name == name) found = Entity(id, this);
    });
    return found;
}

void Scene::SetParent(Entity child, Entity parent) {
    if (!child) return;
    auto& t = child.Transform();
    if (!parent) { t.Parent = {}; return; }
    // Refuse cycles.
    for (EntityID p = parent.GetID(); registry.IsValid(p); p = registry.Get<TransformComponent>(p).Parent) {
        if (p == child.GetID()) { LAPIS_WARN("SetParent: '{}' would become its own ancestor", child.GetName()); return; }
    }
    t.Parent = parent.GetID();
}

std::vector<Entity> Scene::GetChildren(Entity parent) {
    std::vector<Entity> out;
    if (!parent) return out;
    registry.Each<TransformComponent>([&](EntityID id, TransformComponent& t) {
        if (t.Parent == parent.GetID()) out.emplace_back(id, this);
    });
    return out;
}

// --- transforms -------------------------------------------------------------------
const Mat4& Scene::ComputeWorld(EntityID id, u64 frame, int depth) {
    auto& t = registry.Get<TransformComponent>(id);
    if (t.WorldFrame == frame) return t.World;
    t.WorldFrame = frame;
    const Mat4 local = t.LocalMatrix();
    if (depth < 64 && registry.IsValid(t.Parent) && registry.Has<TransformComponent>(t.Parent)) {
        const Mat4 parentWorld = ComputeWorld(t.Parent, frame, depth + 1); // copy: pool may not move, but be safe
        registry.Get<TransformComponent>(id).World = parentWorld * local;
    } else {
        t.World = local;
    }
    return registry.Get<TransformComponent>(id).World;
}

void Scene::UpdateTransforms() {
    ++m_TransformFrame;
    registry.Each<TransformComponent>([&](EntityID id, TransformComponent&) { ComputeWorld(id, m_TransformFrame, 0); });
}

// --- lifecycle --------------------------------------------------------------------
void Scene::OnStart() {
    if (m_Running) return;
    m_Running = true;
    Time = 0.0f;
    UpdateTransforms();

    registry.Each<NativeScriptComponent>([&](EntityID id, NativeScriptComponent& ns) {
        if (!ns.Instance && ns.Instantiate) {
            auto instance = ns.Instantiate();
            instance->Self = Entity(id, this);
            registry.Get<NativeScriptComponent>(id).Instance = instance;
            instance->OnCreate();
        }
    });
    if (registry.Count<LuauScriptComponent>() > 0) {
        GetScripts();
        registry.Each<LuauScriptComponent>([&](EntityID id, LuauScriptComponent&) { m_Scripts->StartEntity(id); });
    }
    FlushDestroyQueue();
}

void Scene::OnUpdate(float dt) {
    if (!m_Running) return;
    Time += dt;

    registry.Each<NativeScriptComponent>([&](EntityID id, NativeScriptComponent& ns) {
        if (!ns.Instance && ns.Instantiate) {
            auto instance = ns.Instantiate();
            instance->Self = Entity(id, this);
            registry.Get<NativeScriptComponent>(id).Instance = instance;
            instance->OnCreate();
        }
        auto* live = registry.TryGet<NativeScriptComponent>(id);
        if (live && live->Instance) { auto keep = live->Instance; keep->OnUpdate(dt); }
    });

    if (registry.Count<LuauScriptComponent>() > 0) GetScripts().UpdateEntities(dt);

    FlushDestroyQueue();
    UpdateTransforms();
}

void Scene::OnFixedUpdate(float dt) {
    if (!m_Running) return;
    registry.Each<RigidBodyComponent, TransformComponent>([](EntityID, RigidBodyComponent&, TransformComponent& t) {
        t.PrevPosition = t.Position;
    });
    m_Physics.Step(registry, dt);

    if (m_Scripts) {
        for (const CollisionEvent& ev : m_Physics.GetBeginEvents()) {
            m_Scripts->DispatchCollision(ev.A, ev.B);
            m_Scripts->DispatchCollision(ev.B, ev.A);
        }
    }
    FlushDestroyQueue();
}

void Scene::OnStop() {
    if (!m_Running) return;
    registry.Each<NativeScriptComponent>([&](EntityID, NativeScriptComponent& ns) {
        if (ns.Instance) { auto keep = ns.Instance; keep->OnDestroy(); ns.Instance.reset(); }
    });
    if (m_Scripts) {
        registry.Each<LuauScriptComponent>([&](EntityID id, LuauScriptComponent&) { m_Scripts->DestroyEntity(id); });
    }
    m_Running = false;
}

// --- rendering --------------------------------------------------------------------
Entity Scene::GetPrimaryCameraEntity() {
    Entity result;
    registry.Each<CameraComponent>([&](EntityID id, CameraComponent& c) {
        if (!result || c.Primary) { if (!result || !result.Get<CameraComponent>().Primary) result = Entity(id, this); }
    });
    return result;
}

bool Scene::GetPrimaryCamera(float aspect, CameraView& out) {
    Entity cam = GetPrimaryCameraEntity();
    if (!cam) return false;
    const auto& t = cam.Get<TransformComponent>();
    const auto& c = cam.Get<CameraComponent>();
    // Remove scale from the camera's world matrix before inverting.
    Mat4 world = t.World;
    for (int col = 0; col < 3; ++col) {
        Vec4 axis = world.Column(col);
        const float len = Length(axis.xyz());
        if (len > EPSILON) world.SetColumn(col, axis / len);
    }
    out.View = Inverse(world);
    out.Projection = c.Settings.GetProjection(aspect);
    out.Position = t.World.GetTranslation();
    out.NearClip = c.Settings.NearClip;
    out.FarClip = c.Settings.FarClip;
    return true;
}

void Scene::OnRender(Renderer& renderer, float aspect, const CameraView* overrideCamera, float interpolation) {
    // Draw physics bodies between their last two simulated positions.
    const bool interpolate = m_Running && interpolation < 1.0f;
    if (interpolate) {
        const float a = Clamp(interpolation, 0.0f, 1.0f);
        registry.Each<RigidBodyComponent, TransformComponent>([&](EntityID, RigidBodyComponent& rb, TransformComponent& t) {
            if (rb.Type == BodyType::Static || rb.Sleeping) return;
            const Vec3 offset = (t.PrevPosition - t.Position) * (1.0f - a);
            if (LengthSq(offset) < 4.0f) t.RenderOffset = offset; // ignore teleports
        });
    }
    UpdateTransforms();

    CameraView view;
    if (overrideCamera) view = *overrideCamera;
    else if (!GetPrimaryCamera(aspect, view)) {
        view.View = LookAt({0, 5, 12}, {0, 0, 0}, Vec3::Up());
        view.Projection = Perspective(Radians(60.0f), aspect, 0.1f, 500.0f);
        view.Position = {0, 5, 12};
    }
    renderer.BeginFrame(view, Env);

    DirectionalLight sun = DefaultSun;
    registry.Each<LightComponent, TransformComponent>([&](EntityID, LightComponent& l, TransformComponent& t) {
        if (l.Type == LightType::Directional) {
            sun.Direction = Normalize(t.World.TransformDirection(Vec3::Forward()));
            sun.Color = l.Color;
            sun.Intensity = l.Intensity;
            sun.CastShadows = l.CastShadows;
        } else {
            renderer.AddPointLight({t.World.GetTranslation(), l.Color, l.Intensity, l.Range});
        }
    });
    renderer.SetDirectionalLight(sun);

    registry.Each<MeshRendererComponent, TransformComponent>([&](EntityID, MeshRendererComponent& mr, TransformComponent& t) {
        if (mr.Visible && mr.MeshAsset) renderer.Submit(mr.MeshAsset, mr.MaterialAsset, t.World);
    });

    if (DebugDrawColliders) {
        registry.Each<ColliderComponent, TransformComponent>([&](EntityID id, ColliderComponent& c, TransformComponent& t) {
            auto* rb = registry.TryGet<RigidBodyComponent>(id);
            const Vec4 color = c.IsTrigger ? Vec4(1, 0.4f, 1, 1) : (rb && rb->Sleeping) ? Vec4(0.3f, 0.3f, 1, 1) : Vec4(0, 1, 0, 1);
            if (c.Shape == ColliderShape::Sphere) {
                const Sphere s = ColliderWorldSphere(t, c);
                renderer.DrawWireSphere(s.Center, s.Radius, color);
            } else {
                renderer.DrawAABB(ColliderWorldAABB(t, c), color);
            }
        });
    }
    for (const auto& l : m_DebugLines) renderer.DrawLine(l.A, l.B, l.Color);
    m_DebugLines.clear();

    if (interpolate) {
        registry.Each<TransformComponent>([](EntityID, TransformComponent& t) { t.RenderOffset = Vec3(0.0f); });
        UpdateTransforms(); // gameplay code sees the real simulated positions
    }
}

} // namespace Lapis
