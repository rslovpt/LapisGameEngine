#pragma once
// Impulse-based rigid body physics operating directly on scene components.
//
// Any entity with Transform + Collider takes part. Entities without a
// RigidBody (or with BodyType::Static) never move. Physics entities should
// be root entities (no parent), since positions are treated as world space.

#include <optional>
#include <unordered_set>
#include <vector>

#include "Lapis/Math/Geometry.h"
#include "Lapis/Scene/Components.h"
#include "Lapis/Scene/Registry.h"

namespace Lapis {

struct RaycastHit {
    EntityID Entity;
    Vec3     Point;
    Vec3     Normal;
    float    Distance = 0.0f;
};

struct CollisionEvent {
    EntityID A, B;
    Vec3     Normal;     // from A towards B
    bool     IsTrigger = false;
};

struct PhysicsSettings {
    Vec3  Gravity{0.0f, -9.81f, 0.0f};
    int   SolverIterations = 10;
    float PenetrationSlop = 0.005f;
    float CorrectionPercent = 0.8f;
    float SleepVelocity = 0.08f;
    float SleepTime = 0.6f;
};

class PhysicsWorld {
public:
    PhysicsSettings Settings;

    void Step(Registry& registry, float dt);

    // Closest hit along the ray (triggers ignored), up to maxDistance.
    std::optional<RaycastHit> Raycast(Registry& registry, const Ray& ray, float maxDistance = 1000.0f,
                                      EntityID ignore = {}) const;

    // Entities whose colliders overlap the sphere.
    std::vector<EntityID> OverlapSphere(Registry& registry, const Vec3& center, float radius) const;

    // Collisions/trigger overlaps that *began* during the last Step().
    const std::vector<CollisionEvent>& GetBeginEvents() const { return m_BeginEvents; }
    size_t GetContactCount() const { return m_ContactCount; }

private:
    std::vector<CollisionEvent> m_BeginEvents;
    std::unordered_set<u64> m_PrevPairs;
    size_t m_ContactCount = 0;
};

// World-space shape helpers (also used by debug drawing).
AABB   ColliderWorldAABB(const TransformComponent& t, const ColliderComponent& c);
Sphere ColliderWorldSphere(const TransformComponent& t, const ColliderComponent& c);

} // namespace Lapis
