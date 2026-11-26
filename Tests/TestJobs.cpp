#include "TestFramework.h"

#include "Lapis/Core/JobSystem.h"
#include "Lapis/Scene/Scene.h"

#include <atomic>
#include <vector>

using namespace Lapis;

namespace {
    // Ensures the pool has at least 3 workers for the duration of a test.
    struct PoolGuard {
        PoolGuard() { JobSystem::SetWorkerCount(3); JobSystem::SetEnabled(true); }
        ~PoolGuard() { JobSystem::SetWorkerCount(0); JobSystem::SetEnabled(true); }
    };
}

LAPIS_TEST(Jobs, ParallelForVisitsEveryIndexOnce) {
    PoolGuard guard;
    const u32 n = 100000;
    std::vector<int> hits(n, 0);
    const u64 before = JobSystem::GetParallelDispatches();
    JobSystem::ParallelFor(n, 64, [&](u32 b, u32 e) { for (u32 i = b; i < e; ++i) hits[i]++; });
    bool ok = true;
    for (int h : hits) ok = ok && h == 1;
    CHECK(ok);
    CHECK(JobSystem::GetParallelDispatches() == before + 1);
    CHECK(JobSystem::GetWorkerCount() == 3);
}

LAPIS_TEST(Jobs, SmallWorkRunsInline) {
    PoolGuard guard;
    const u64 before = JobSystem::GetParallelDispatches();
    int sum = 0; // not atomic: must run on this thread only
    JobSystem::ParallelFor(10, 64, [&](u32 b, u32 e) { for (u32 i = b; i < e; ++i) sum += (int)i; });
    CHECK(sum == 45);
    CHECK(JobSystem::GetParallelDispatches() == before);
}

LAPIS_TEST(Jobs, NestedParallelFor) {
    PoolGuard guard;
    std::atomic<u64> total{0};
    JobSystem::ParallelFor(64, 1, [&](u32 b, u32 e) {
        for (u32 i = b; i < e; ++i)
            JobSystem::ParallelFor(1000, 16, [&](u32 b2, u32 e2) { total += e2 - b2; });
    });
    CHECK(total.load() == 64u * 1000u);
}

LAPIS_TEST(Jobs, DisabledRunsOnCaller) {
    PoolGuard guard;
    JobSystem::SetEnabled(false);
    const u64 before = JobSystem::GetParallelDispatches();
    u64 sum = 0;
    JobSystem::ParallelFor(50000, 16, [&](u32 b, u32 e) { for (u32 i = b; i < e; ++i) sum += i; });
    CHECK(sum == 50000ull * 49999ull / 2);
    CHECK(JobSystem::GetParallelDispatches() == before);
}

// The parallel physics path must give bit-identical results to the serial one.
LAPIS_TEST(Jobs, PhysicsDeterministicAcrossThreads) {
    PoolGuard guard;
    auto run = [](bool parallel) {
        JobSystem::SetEnabled(parallel);
        Scene s;
        Entity g = s.CreateEntity("Ground");
        g.Transform().Scale = {60, 1, 60};
        g.Transform().Position = {0, -0.5f, 0};
        g.Add<ColliderComponent>();
        std::vector<Entity> bodies;
        for (int i = 0; i < 600; ++i) {
            Entity e = s.CreateEntity("B");
            e.Transform().Position = {(float)(i % 12) * 1.1f - 6.0f, 1.0f + (float)(i / 144) * 1.2f, (float)((i / 12) % 12) * 1.1f - 6.0f};
            e.Add<ColliderComponent>().Shape = i % 2 ? ColliderShape::Sphere : ColliderShape::Box;
            e.Add<RigidBodyComponent>();
            bodies.push_back(e);
        }
        s.OnStart();
        for (int f = 0; f < 120; ++f) s.OnFixedUpdate(1.0f / 60.0f);
        std::vector<Vec3> out;
        for (Entity e : bodies) out.push_back(e.Transform().Position);
        return out;
    };
    const auto serial = run(false);
    const u64 before = JobSystem::GetParallelDispatches();
    const auto parallel = run(true);
    CHECK(JobSystem::GetParallelDispatches() > before);
    bool same = serial.size() == parallel.size();
    for (size_t i = 0; same && i < serial.size(); ++i)
        same = serial[i].x == parallel[i].x && serial[i].y == parallel[i].y && serial[i].z == parallel[i].z;
    CHECK(same);
    CHECK(serial.size() == 600 && serial[599].y < 1.0f + 4 * 1.2f - 0.5f); // bodies actually fell
}
