#pragma once
// Built-in components. Components are plain data; systems in Scene,
// PhysicsWorld and ScriptEngine operate on them.

#include <functional>
#include <string>

#include "Lapis/Core/Base.h"
#include "Lapis/Math/Math.h"
#include "Lapis/Renderer/Camera.h"
#include "Lapis/Renderer/Material.h"
#include "Lapis/Renderer/Mesh.h"
#include "Lapis/Scene/Registry.h"

namespace Lapis {

struct NameComponent {
    std::string Name = "Entity";
};

struct TransformComponent {
    Vec3     Position{0.0f};
    Quat     Rotation = Quat::Identity();
    Vec3     Scale{1.0f};
    EntityID Parent;                    // null = root

    Mat4     World = Mat4::Identity();  // computed by Scene::UpdateTransforms
    u64      WorldFrame = 0;            // internal bookkeeping

    // Render interpolation (physics runs at a fixed rate, rendering doesn't):
    // PrevPosition = position before the last physics step; RenderOffset is
    // applied only while a frame is being rendered.
    Vec3     PrevPosition{0.0f};
    Vec3     RenderOffset{0.0f};

    Mat4 LocalMatrix() const { return ComposeTRS(Position + RenderOffset, Rotation, Scale); }
    // Moves without interpolating from the old position (teleport).
    void Teleport(const Vec3& p) { Position = p; PrevPosition = p; }

    // Euler angles in degrees (pitch, yaw, roll).
    Vec3 GetEulerDegrees() const { Vec3 e = Rotation.ToEuler(); return {Degrees(e.x), Degrees(e.y), Degrees(e.z)}; }
    void SetEulerDegrees(const Vec3& d) { Rotation = Quat::FromEuler({Radians(d.x), Radians(d.y), Radians(d.z)}); }

    // Local-space basis vectors.
    Vec3 Forward() const { return Rotation * Vec3::Forward(); }
    Vec3 Right() const   { return Rotation * Vec3::Right(); }
    Vec3 Up() const      { return Rotation * Vec3::Up(); }

    Vec3 WorldPosition() const { return World.GetTranslation(); }
};

struct MeshRendererComponent {
    Ref<Mesh>     MeshAsset;
    Ref<Material> MaterialAsset;
    std::string   MeshName;     // asset key used for serialization ("Cube", "Models/rock.obj")
    bool          Visible = true;
};

struct CameraComponent {
    Camera Settings;
    bool   Primary = true;
};

enum class LightType { Directional, Point };

struct LightComponent {
    LightType Type = LightType::Point;
    Vec3      Color{1.0f};
    float     Intensity = 10.0f;
    float     Range = 10.0f;       // point lights
    bool      CastShadows = true;  // directional lights
};

// --- physics -----------------------------------------------------------------
enum class BodyType { Static, Dynamic, Kinematic };
enum class ColliderShape { Box, Sphere };

struct RigidBodyComponent {
    BodyType Type = BodyType::Dynamic;
    float    Mass = 1.0f;
    Vec3     Velocity{0.0f};
    float    Restitution = 0.3f;    // bounciness 0..1
    float    Friction = 0.5f;
    float    LinearDamping = 0.05f;
    float    GravityScale = 1.0f;
    bool     Sleeping = false;
    float    SleepTimer = 0.0f;

    // Accumulated per step, cleared by the physics world.
    Vec3     Force{0.0f};

    float InverseMass() const { return (Type == BodyType::Dynamic && Mass > 0.0f) ? 1.0f / Mass : 0.0f; }
    void  AddForce(const Vec3& f) { Force += f; Sleeping = false; }
    void  AddImpulse(const Vec3& j) { Velocity += j * InverseMass(); Sleeping = false; }
};

// Box colliders are axis-aligned in world space (rotation is ignored) and
// scaled by the transform's scale; sphere radius is scaled by the largest
// scale axis.
struct ColliderComponent {
    ColliderShape Shape = ColliderShape::Box;
    Vec3  HalfExtents{0.5f};
    float Radius = 0.5f;
    Vec3  Offset{0.0f};
    bool  IsTrigger = false;
};

} // namespace Lapis
