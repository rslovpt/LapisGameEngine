#include "TestFramework.h"

#include "Lapis/Scene/Scene.h"

using namespace Lapis;

namespace {
    Entity MakeGround(Scene& s) {
        Entity g = s.CreateEntity("Ground");
        g.Transform().Scale = {20, 1, 20};
        g.Transform().Position = {0, -0.5f, 0};
        g.Add<ColliderComponent>(); // box, no rigidbody = static
        return g;
    }
    Entity MakeBody(Scene& s, ColliderShape shape, const Vec3& pos) {
        Entity e = s.CreateEntity("Body");
        e.Transform().Position = pos;
        e.Add<ColliderComponent>().Shape = shape;
        e.Add<RigidBodyComponent>();
        return e;
    }
    void Simulate(Scene& s, float seconds) {
        const float dt = 1.0f / 60.0f;
        for (float t = 0; t < seconds; t += dt) s.OnFixedUpdate(dt);
    }
}

LAPIS_TEST(Physics, FreeFall) {
    Scene s;
    Entity e = MakeBody(s, ColliderShape::Sphere, {0, 100, 0});
    e.Get<RigidBodyComponent>().LinearDamping = 0.0f;
    s.OnStart();
    Simulate(s, 1.0f);
    // Semi-implicit Euler: ~ 0.5 g t^2 = 4.9 m (+ small integration error)
    const float fallen = 100.0f - e.Transform().Position.y;
    CHECK(fallen > 4.7f && fallen < 5.2f);
}

LAPIS_TEST(Physics, BoxRestsOnGround) {
    Scene s;
    MakeGround(s);
    Entity box = MakeBody(s, ColliderShape::Box, {0, 3, 0});
    s.OnStart();
    Simulate(s, 4.0f);
    CHECK(std::fabs(box.Transform().Position.y - 0.5f) < 0.01f); // within penetration slop
    CHECK(Length(box.Get<RigidBodyComponent>().Velocity) < 0.1f);
    CHECK(box.Get<RigidBodyComponent>().Sleeping);
}

LAPIS_TEST(Physics, SphereRestsOnGround) {
    Scene s;
    MakeGround(s);
    Entity ball = MakeBody(s, ColliderShape::Sphere, {0.3f, 2, 0.1f});
    s.OnStart();
    Simulate(s, 4.0f);
    CHECK(std::fabs(ball.Transform().Position.y - 0.5f) < 0.02f);
}

LAPIS_TEST(Physics, StackOfBoxes) {
    Scene s;
    MakeGround(s);
    Entity boxes[4];
    for (int i = 0; i < 4; ++i) boxes[i] = MakeBody(s, ColliderShape::Box, {0, 0.5f + i * 1.05f, 0});
    s.OnStart();
    Simulate(s, 5.0f);
    for (int i = 0; i < 4; ++i) CHECK(std::fabs(boxes[i].Transform().Position.y - (0.5f + i)) < 0.05f);
}

LAPIS_TEST(Physics, BouncyBallBounces) {
    Scene s;
    MakeGround(s);
    Entity ball = MakeBody(s, ColliderShape::Sphere, {0, 5, 0});
    ball.Get<RigidBodyComponent>().Restitution = 0.9f;
    s.OnStart();
    float maxYAfterBounce = 0.0f;
    bool hitGround = false;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 240; ++i) {
        s.OnFixedUpdate(dt);
        const float y = ball.Transform().Position.y;
        if (y < 0.6f) hitGround = true;
        if (hitGround) maxYAfterBounce = std::max(maxYAfterBounce, y);
    }
    CHECK(hitGround);
    CHECK(maxYAfterBounce > 3.0f); // bounced back up most of the way
}

LAPIS_TEST(Physics, MomentumTransfer) {
    Scene s;
    s.GetPhysics().Settings.Gravity = Vec3(0.0f);
    Entity a = MakeBody(s, ColliderShape::Sphere, {-2, 0, 0});
    Entity b = MakeBody(s, ColliderShape::Sphere, {2, 0, 0});
    for (Entity e : {a, b}) { auto& rb = e.Get<RigidBodyComponent>(); rb.Restitution = 1.0f; rb.LinearDamping = 0; rb.Friction = 0; }
    a.Get<RigidBodyComponent>().Velocity = {5, 0, 0};
    s.OnStart();
    Simulate(s, 1.5f);
    // Equal masses, elastic: velocities swap.
    CHECK(std::fabs(a.Get<RigidBodyComponent>().Velocity.x) < 0.3f);
    CHECK(b.Get<RigidBodyComponent>().Velocity.x > 4.5f);
}

LAPIS_TEST(Physics, RaycastAndEvents) {
    Scene s;
    Entity ground = MakeGround(s);
    Entity box = MakeBody(s, ColliderShape::Box, {0, 5, 0});
    box.Get<RigidBodyComponent>().Type = BodyType::Static;

    auto hit = s.GetPhysics().Raycast(s.GetRegistry(), Ray{{0, 20, 0}, {0, -1, 0}});
    CHECK(hit.has_value());
    if (hit) {
        CHECK(hit->Entity == box.GetID());
        CHECK_NEAR(hit->Distance, 14.5f);
        CHECK(NearlyEqual(hit->Normal, Vec3(0, 1, 0)));
    }
    auto groundHit = s.GetPhysics().Raycast(s.GetRegistry(), Ray{{5, 20, 0}, {0, -1, 0}});
    CHECK(groundHit && groundHit->Entity == ground.GetID());
    CHECK(!s.GetPhysics().Raycast(s.GetRegistry(), Ray{{50, 20, 0}, {0, -1, 0}}));

    // Collision begin events fire once.
    Scene s2;
    MakeGround(s2);
    MakeBody(s2, ColliderShape::Sphere, {0, 1, 0}).Get<RigidBodyComponent>().Restitution = 0.0f; // no bounces
    s2.OnStart();
    int begins = 0;
    for (int i = 0; i < 120; ++i) { s2.OnFixedUpdate(1.0f / 60.0f); begins += (int)s2.GetPhysics().GetBeginEvents().size(); }
    CHECK(begins == 1);
}

LAPIS_TEST(Physics, BroadphaseFindsAllPairs) {
    // Spatial-hash broadphase must find exactly the pairs a brute-force
    // check finds (mixed sizes, including bodies spanning many cells).
    Scene s;
    s.GetPhysics().Settings.Gravity = Vec3(0.0f);
    std::vector<Entity> boxes;
    u32 seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    for (int i = 0; i < 300; ++i) {
        Entity e = s.CreateEntity("b");
        e.Transform().Position = {(rnd() - 0.5f) * 20, (rnd() - 0.5f) * 6, (rnd() - 0.5f) * 20};
        const float big = i % 50 == 0 ? 12.0f : 1.0f;
        e.Transform().Scale = Vec3(0.4f + rnd() * 1.6f) * big;
        e.Add<ColliderComponent>();
        e.Add<RigidBodyComponent>().LinearDamping = 0.0f;
        boxes.push_back(e);
    }
    size_t expected = 0;
    for (size_t i = 0; i < boxes.size(); ++i)
        for (size_t j = i + 1; j < boxes.size(); ++j) {
            const AABB a = ColliderWorldAABB(boxes[i].Transform(), boxes[i].Get<ColliderComponent>());
            const AABB b = ColliderWorldAABB(boxes[j].Transform(), boxes[j].Get<ColliderComponent>());
            Vec3 overlap = (a.Extents() + b.Extents()) - Abs(b.Center() - a.Center());
            if (overlap.x > 0 && overlap.y > 0 && overlap.z > 0) ++expected;
        }
    s.OnStart();
    s.GetPhysics().Step(s.GetRegistry(), 1e-6f);
    CHECK(s.GetPhysics().GetContactCount() == expected);
    CHECK(expected > 50); // the test actually exercises overlaps
}

LAPIS_TEST(Physics, AxisLockKeepsBodiesIn2DPlane) {
    Scene s;
    MakeGround(s);
    Entity a = MakeBody(s, ColliderShape::Box, {0, 3, 0});
    Entity b = MakeBody(s, ColliderShape::Box, {0.3f, 5, 0.4f});   // lands off-center on a
    a.Get<RigidBodyComponent>().LockAxes = RigidBodyComponent::LockZ;
    b.Get<RigidBodyComponent>().LockAxes = RigidBodyComponent::LockZ;
    a.Get<RigidBodyComponent>().Velocity = {0, 0, 5};   // would drift in Z without the lock
    s.OnStart();
    Simulate(s, 2.0f);
    CHECK(a.Transform().Position.z == 0.0f);
    CHECK(b.Transform().Position.z == 0.4f);
    CHECK(b.Transform().Position.y > a.Transform().Position.y);   // still stacked / fell, but in-plane
}

LAPIS_TEST(Physics, RenderPositionIsInterpolated) {
    Scene s;
    Entity e = MakeBody(s, ColliderShape::Sphere, {0, 100, 0});
    Entity child = s.CreateEntity("Child");
    child.Transform().Parent = e.GetID();
    child.Transform().Position = {0, 1, 0};
    s.OnStart();
    Simulate(s, 0.5f);
    s.UpdateTransforms();
    const auto& t = e.Transform();
    s.InterpolationAlpha = 1.0f;   // drawn exactly at the simulated position
    CHECK_NEAR(s.GetRenderPosition(e.GetID()).y, t.Position.y);
    s.InterpolationAlpha = 0.0f;   // drawn at the previous step
    CHECK_NEAR(s.GetRenderPosition(e.GetID()).y, t.PrevPosition.y);
    CHECK_NEAR(s.GetRenderPosition(child.GetID()).y, t.PrevPosition.y + 1.0f);   // children follow
    s.InterpolationAlpha = 0.5f;
    CHECK_NEAR(s.GetRenderPosition(e.GetID()).y, (t.Position.y + t.PrevPosition.y) * 0.5f);
}
