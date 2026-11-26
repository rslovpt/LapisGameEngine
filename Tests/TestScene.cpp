#include "TestFramework.h"

#include "Lapis/Scene/Scene.h"
#include "Lapis/Scene/ScriptableEntity.h"

using namespace Lapis;

namespace {
    struct Health { int Value = 100; };
    struct Tag {};
}

LAPIS_TEST(ECS, CreateDestroyGenerations) {
    Registry r;
    EntityID a = r.Create();
    EntityID b = r.Create();
    CHECK(r.IsValid(a) && r.IsValid(b));
    CHECK(r.AliveCount() == 2);
    r.Destroy(a);
    CHECK(!r.IsValid(a));
    EntityID c = r.Create(); // reuses a's slot with a new generation
    CHECK(c.Index == a.Index);
    CHECK(c.Generation != a.Generation);
    CHECK(!r.IsValid(a));
    CHECK(r.IsValid(c));
}

LAPIS_TEST(ECS, Components) {
    Registry r;
    EntityID e = r.Create();
    r.Add<Health>(e).Value = 42;
    CHECK(r.Has<Health>(e));
    CHECK(!r.Has<Tag>(e));
    CHECK(r.Get<Health>(e).Value == 42);
    CHECK(r.TryGet<Tag>(e) == nullptr);
    r.Remove<Health>(e);
    CHECK(!r.Has<Health>(e));
}

LAPIS_TEST(ECS, SwapRemoveKeepsOthers) {
    Registry r;
    EntityID ids[5];
    for (int i = 0; i < 5; ++i) { ids[i] = r.Create(); r.Add<Health>(ids[i]).Value = i; }
    r.Destroy(ids[1]);
    r.Remove<Health>(ids[3]);
    CHECK(r.Get<Health>(ids[0]).Value == 0);
    CHECK(r.Get<Health>(ids[2]).Value == 2);
    CHECK(r.Get<Health>(ids[4]).Value == 4);
    CHECK(r.Count<Health>() == 3);
}

LAPIS_TEST(ECS, EachVisitsMatching) {
    Registry r;
    for (int i = 0; i < 10; ++i) {
        EntityID e = r.Create();
        r.Add<Health>(e).Value = i;
        if (i % 2 == 0) r.Add<Tag>(e);
    }
    int count = 0, sum = 0;
    r.Each<Health, Tag>([&](EntityID, Health& h, Tag&) { ++count; sum += h.Value; });
    CHECK(count == 5);
    CHECK(sum == 0 + 2 + 4 + 6 + 8);
}

LAPIS_TEST(ECS, EachSafeWhileDestroying) {
    Registry r;
    for (int i = 0; i < 6; ++i) r.Add<Health>(r.Create());
    int visited = 0;
    r.Each<Health>([&](EntityID e, Health&) { ++visited; r.Destroy(e); });
    CHECK(visited == 6);
    CHECK(r.AliveCount() == 0);
}

LAPIS_TEST(Scene, HierarchyTransforms) {
    Scene scene;
    Entity parent = scene.CreateEntity("Parent");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, parent);
    parent.Transform().Position = {10, 0, 0};
    parent.Transform().Rotation = Quat::FromAxisAngle(Vec3::Up(), HALF_PI);
    child.Transform().Position = {1, 0, 0};
    scene.UpdateTransforms();
    // Parent rotated +90 deg about Y: child's local +X maps to world -Z.
    CHECK(NearlyEqual(child.Transform().WorldPosition(), Vec3(10, 0, -1)));
    CHECK(scene.GetChildren(parent).size() == 1);

    scene.SetParent(parent, child); // cycle -> refused
    CHECK(parent.Transform().Parent.IsNull());
}

LAPIS_TEST(Scene, FindAndDeferredDestroy) {
    Scene scene;
    Entity a = scene.CreateEntity("Alpha");
    Entity b = scene.CreateEntity("Beta");
    scene.SetParent(b, a);
    CHECK(scene.FindEntityByName("Beta") == b);
    scene.OnStart();
    scene.DestroyEntity(a);
    CHECK(a.IsValid());               // still alive until end of frame
    scene.OnUpdate(0.016f);
    CHECK(!a.IsValid());
    CHECK(!b.IsValid());              // children go too
    CHECK(!scene.FindEntityByName("Alpha"));
}

namespace {
    struct Counter : ScriptableEntity {
        static inline int Created = 0, Updated = 0, Destroyed = 0;
        void OnCreate() override { ++Created; }
        void OnUpdate(float) override { ++Updated; Transform().Position.x += 1.0f; }
        void OnDestroy() override { ++Destroyed; }
    };
}

LAPIS_TEST(Scene, NativeScripts) {
    Counter::Created = Counter::Updated = Counter::Destroyed = 0;
    {
        Scene scene;
        Entity e = scene.CreateEntity("Scripted");
        e.Add<NativeScriptComponent>().Bind<Counter>();
        scene.OnStart();
        scene.OnUpdate(0.1f);
        scene.OnUpdate(0.1f);
        CHECK(Counter::Created == 1);
        CHECK(Counter::Updated == 2);
        CHECK_NEAR(e.Transform().Position.x, 2.0f);
        scene.DestroyEntity(e);
        scene.OnUpdate(0.1f);
        CHECK(Counter::Destroyed == 1);
    }
}
