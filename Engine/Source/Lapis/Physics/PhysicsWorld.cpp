#include "Lapis/Physics/PhysicsWorld.h"

#include "Lapis/Core/JobSystem.h"

#include <algorithm>
#include <unordered_map>

namespace Lapis {

AABB ColliderWorldAABB(const TransformComponent& t, const ColliderComponent& c) {
    const Vec3 center = t.Position + c.Offset * t.Scale;
    if (c.Shape == ColliderShape::Sphere) {
        const Sphere s = ColliderWorldSphere(t, c);
        return AABB::FromCenterExtents(s.Center, Vec3(s.Radius));
    }
    return AABB::FromCenterExtents(center, Abs(c.HalfExtents * t.Scale));
}

Sphere ColliderWorldSphere(const TransformComponent& t, const ColliderComponent& c) {
    const Vec3 s = Abs(t.Scale);
    return {t.Position + c.Offset * t.Scale, c.Radius * std::max(s.x, std::max(s.y, s.z))};
}

namespace {

struct Body {
    EntityID            ID;
    TransformComponent* Transform;
    RigidBodyComponent* RB;      // may be null (static)
    ColliderComponent*  Collider;
    float               InvMass;
    AABB                Bounds;

    Vec3 Velocity() const { return RB ? RB->Velocity : Vec3(0.0f); }
    bool Moves() const { return RB && RB->Type != BodyType::Static; }
};

struct Contact {
    int   A, B;            // body indices
    Vec3  Normal;          // from A to B
    float Penetration;
    float Restitution, Friction;
    float TargetNormalVelocity = 0.0f;
    float NormalImpulse = 0.0f;
    float TangentImpulse = 0.0f;
};

// --- narrow phase ---------------------------------------------------------------
bool SphereSphere(const Sphere& a, const Sphere& b, Vec3& n, float& pen) {
    const Vec3 d = b.Center - a.Center;
    const float dist2 = LengthSq(d), r = a.Radius + b.Radius;
    if (dist2 >= r * r) return false;
    const float dist = std::sqrt(dist2);
    n = dist > 1e-6f ? d / dist : Vec3::Up();
    pen = r - dist;
    return true;
}

// Normal points from the box towards the sphere.
bool BoxSphere(const AABB& box, const Sphere& s, Vec3& n, float& pen) {
    const Vec3 closest = box.ClosestPoint(s.Center);
    const Vec3 d = s.Center - closest;
    const float dist2 = LengthSq(d);
    if (dist2 > s.Radius * s.Radius) return false;
    if (dist2 > 1e-10f) {
        const float dist = std::sqrt(dist2);
        n = d / dist;
        pen = s.Radius - dist;
    } else {
        // Sphere center inside the box: push out along the axis of least penetration.
        const Vec3 c = box.Center(), e = box.Extents(), p = s.Center - c;
        float best = std::numeric_limits<float>::max();
        for (int i = 0; i < 3; ++i) {
            const float overlap = e[i] - std::fabs(p[i]);
            if (overlap < best) {
                best = overlap;
                n = Vec3(0.0f);
                n[i] = p[i] < 0.0f ? -1.0f : 1.0f;
            }
        }
        pen = best + s.Radius;
    }
    return true;
}

bool BoxBox(const AABB& a, const AABB& b, Vec3& n, float& pen) {
    if (!a.Intersects(b)) return false;
    const Vec3 d = b.Center() - a.Center();
    const Vec3 overlap = (a.Extents() + b.Extents()) - Abs(d);
    int axis = 0;
    if (overlap.y < overlap[axis]) axis = 1;
    if (overlap.z < overlap[axis]) axis = 2;
    if (overlap[axis] <= 0.0f) return false;
    n = Vec3(0.0f);
    n[axis] = d[axis] < 0.0f ? -1.0f : 1.0f;
    pen = overlap[axis];
    return true;
}

bool Collide(const Body& a, const Body& b, Vec3& n, float& pen) {
    const ColliderShape sa = a.Collider->Shape, sb = b.Collider->Shape;
    if (sa == ColliderShape::Sphere && sb == ColliderShape::Sphere)
        return SphereSphere(ColliderWorldSphere(*a.Transform, *a.Collider), ColliderWorldSphere(*b.Transform, *b.Collider), n, pen);
    if (sa == ColliderShape::Box && sb == ColliderShape::Box)
        return BoxBox(a.Bounds, b.Bounds, n, pen);
    if (sa == ColliderShape::Box)
        return BoxSphere(a.Bounds, ColliderWorldSphere(*b.Transform, *b.Collider), n, pen);
    // sphere (a) vs box (b): compute box->sphere and flip.
    if (!BoxSphere(b.Bounds, ColliderWorldSphere(*a.Transform, *a.Collider), n, pen)) return false;
    n = -n;
    return true;
}

u64 PairKey(EntityID a, EntityID b) {
    u32 x = a.Index, y = b.Index;
    if (x > y) std::swap(x, y);
    return (u64(x) << 32) | y;
}

void Wake(Body& b) { if (b.RB) { b.RB->Sleeping = false; b.RB->SleepTimer = 0.0f; } }

} // namespace

void PhysicsWorld::Step(Registry& registry, float dt) {
    m_BeginEvents.clear();
    if (dt <= 0.0f) return;

    // 1. Gather bodies.
    std::vector<Body> bodies;
    registry.Each<TransformComponent, ColliderComponent>([&](EntityID id, TransformComponent& t, ColliderComponent& c) {
        RigidBodyComponent* rb = registry.TryGet<RigidBodyComponent>(id);
        bodies.push_back({id, &t, rb, &c, rb ? rb->InverseMass() : 0.0f, {}});
    });

    // 2. Integrate forces -> velocities.
    for (Body& b : bodies) {
        if (!b.RB || b.RB->Type != BodyType::Dynamic) { if (b.RB) b.RB->Force = Vec3(0.0f); continue; }
        RigidBodyComponent& rb = *b.RB;
        if (!rb.Sleeping) {
            rb.Velocity += (Settings.Gravity * rb.GravityScale + rb.Force * b.InvMass) * dt;
            rb.Velocity *= 1.0f / (1.0f + rb.LinearDamping * dt);
        }
        rb.Force = Vec3(0.0f);
    }
    JobSystem::ParallelFor((u32)bodies.size(), 256, [&](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i) bodies[i].Bounds = ColliderWorldAABB(*bodies[i].Transform, *bodies[i].Collider);
    });

    // 3. Broad phase: uniform spatial hash. Cell size follows the typical
    //    body size; very large bodies (ground, walls) go to a separate list
    //    that is tested against everything instead of filling many cells.
    std::vector<int> candidatesA, candidatesB;
    {
        std::vector<float> sizes;
        sizes.reserve(bodies.size());
        for (const Body& b : bodies) if (b.Moves()) { const Vec3 e = b.Bounds.Size(); sizes.push_back(std::max(e.x, std::max(e.y, e.z))); }
        float cell = 2.0f;
        if (!sizes.empty()) {
            std::nth_element(sizes.begin(), sizes.begin() + sizes.size() / 2, sizes.end());
            cell = Clamp(sizes[sizes.size() / 2] * 2.0f, 0.25f, 16.0f);
        }
        const float inv = 1.0f / cell;

        // Each small body is written into every cell its AABB touches as a
        // (cell key, body) entry; sorting the entries groups bodies per cell
        // without any hash map allocations.
        struct Range { int X0, Y0, Z0, X1, Y1, Z1; };
        std::vector<Range> ranges(bodies.size());
        struct Entry { u64 Key; int Body; };
        std::vector<Entry> entries;
        entries.reserve(bodies.size() * 2);
        std::vector<int> large;
        auto cellKey = [](int x, int y, int z) -> u64 {
            return (u64(u32(x + 0xFFFFF) & 0x1FFFFF) << 42) | (u64(u32(y + 0xFFFFF) & 0x1FFFFF) << 21) | u64(u32(z + 0xFFFFF) & 0x1FFFFF);
        };
        for (int i = 0; i < (int)bodies.size(); ++i) {
            const AABB& bb = bodies[i].Bounds;
            Range& r = ranges[i];
            r = {(int)std::floor(bb.Min.x * inv), (int)std::floor(bb.Min.y * inv), (int)std::floor(bb.Min.z * inv),
                 (int)std::floor(bb.Max.x * inv), (int)std::floor(bb.Max.y * inv), (int)std::floor(bb.Max.z * inv)};
            if ((long long)(r.X1 - r.X0 + 1) * (r.Y1 - r.Y0 + 1) * (r.Z1 - r.Z0 + 1) > 27) { large.push_back(i); continue; }
            for (int x = r.X0; x <= r.X1; ++x)
                for (int y = r.Y0; y <= r.Y1; ++y)
                    for (int z = r.Z0; z <= r.Z1; ++z) entries.push_back({cellKey(x, y, z), i});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.Key < b.Key; });

        for (size_t start = 0; start < entries.size();) {
            size_t end = start + 1;
            while (end < entries.size() && entries[end].Key == entries[start].Key) ++end;
            for (size_t m = start; m < end; ++m) {
                const int i = entries[m].Body;
                for (size_t n = m + 1; n < end; ++n) {
                    const int j = entries[n].Body;
                    // Report each pair only once: from the first cell (lowest
                    // corner) of the overlap of their cell ranges.
                    const Range& ri = ranges[i];
                    const Range& rj = ranges[j];
                    const u64 first = cellKey(std::max(ri.X0, rj.X0), std::max(ri.Y0, rj.Y0), std::max(ri.Z0, rj.Z0));
                    if (first != entries[m].Key) continue;
                    candidatesA.push_back(i);
                    candidatesB.push_back(j);
                }
            }
            start = end;
        }
        for (size_t l = 0; l < large.size(); ++l) {
            for (int i = 0; i < (int)bodies.size(); ++i) {
                if (i == large[l]) continue;
                // large-vs-large pairs only once
                if (std::find(large.begin(), large.begin() + l, i) != large.begin() + l) continue;
                if (!bodies[large[l]].Moves() && !bodies[i].Moves()) continue;
                if (!bodies[large[l]].Bounds.Intersects(bodies[i].Bounds)) continue;
                candidatesA.push_back(large[l]);
                candidatesB.push_back(i);
            }
        }
    }

    // 4. Narrow phase. The expensive shape tests run in parallel and only
    //    write their own result slot; events and contacts are then assembled
    //    serially in candidate order, so results are identical to a
    //    single-threaded run.
    struct NarrowResult { u8 Kind = 0; Vec3 Normal; float Penetration = 0.0f; }; // 0 none, 1 resting, 2 hit
    std::vector<NarrowResult> results(candidatesA.size());
    JobSystem::ParallelFor((u32)candidatesA.size(), 128, [&](u32 begin, u32 end) {
        for (u32 c = begin; c < end; ++c) {
            const Body& a = bodies[candidatesA[c]];
            const Body& b = bodies[candidatesB[c]];
            if (!a.Moves() && !b.Moves()) continue;
            // Resting contacts between sleeping bodies (or sleeping vs static) need no work.
            const bool aRest = !a.Moves() || (a.RB && a.RB->Sleeping);
            const bool bRest = !b.Moves() || (b.RB && b.RB->Sleeping);
            if (aRest && bRest) { results[c].Kind = 1; continue; }
            if (!a.Bounds.Intersects(b.Bounds)) continue;
            if (Collide(a, b, results[c].Normal, results[c].Penetration)) results[c].Kind = 2;
        }
    });

    std::vector<Contact> contacts;
    std::unordered_set<u64> pairs;
    for (size_t c = 0; c < candidatesA.size(); ++c) {
        const NarrowResult& r = results[c];
        if (r.Kind == 0) continue;
        Body& a = bodies[candidatesA[c]];
        Body& b = bodies[candidatesB[c]];
        const u64 key = PairKey(a.ID, b.ID);
        // Resting pair: remember it so waking up doesn't fire a new event.
        if (r.Kind == 1) {
            if (m_PrevPairs.count(key)) pairs.insert(key);
            continue;
        }
        const Vec3 n = r.Normal;
        const float pen = r.Penetration;

        pairs.insert(key);
        const bool trigger = a.Collider->IsTrigger || b.Collider->IsTrigger;
        if (!m_PrevPairs.count(key)) m_BeginEvents.push_back({a.ID, b.ID, n, trigger});
        if (trigger) continue;

        const float restA = a.RB ? a.RB->Restitution : 0.0f, restB = b.RB ? b.RB->Restitution : 0.0f;
        const float fricA = a.RB ? a.RB->Friction : 0.6f, fricB = b.RB ? b.RB->Friction : 0.6f;
        Contact ct{candidatesA[c], candidatesB[c], n, pen, std::max(restA, restB), std::sqrt(fricA * fricB)};

        // Restitution target, computed once from the pre-solve velocity.
        const float vn = Dot(b.Velocity() - a.Velocity(), n);
        ct.TargetNormalVelocity = vn < -1.0f ? -ct.Restitution * vn : 0.0f;

        // Moving bodies wake sleeping ones they hit.
        const bool aAwake = a.RB && !a.RB->Sleeping && a.Moves();
        const bool bAwake = b.RB && !b.RB->Sleeping && b.Moves();
        if (aAwake && LengthSq(a.Velocity()) > 0.25f) Wake(b);
        if (bAwake && LengthSq(b.Velocity()) > 0.25f) Wake(a);
        contacts.push_back(ct);
    }
    m_PrevPairs = std::move(pairs);
    m_ContactCount = contacts.size();

    // 5. Sequential impulse solver.
    for (int iter = 0; iter < Settings.SolverIterations; ++iter) {
        for (Contact& c : contacts) {
            Body& a = bodies[c.A];
            Body& b = bodies[c.B];
            const float k = a.InvMass + b.InvMass;
            if (k <= 0.0f) continue;

            Vec3 vr = b.Velocity() - a.Velocity();
            const float vn = Dot(vr, c.Normal);
            float dj = (c.TargetNormalVelocity - vn) / k;
            const float newImpulse = std::max(c.NormalImpulse + dj, 0.0f);
            dj = newImpulse - c.NormalImpulse;
            c.NormalImpulse = newImpulse;
            const Vec3 jn = c.Normal * dj;
            if (a.RB) a.RB->Velocity -= jn * a.InvMass;
            if (b.RB) b.RB->Velocity += jn * b.InvMass;

            // Coulomb friction.
            vr = b.Velocity() - a.Velocity();
            Vec3 tangent = vr - c.Normal * Dot(vr, c.Normal);
            const float tl = Length(tangent);
            if (tl < 1e-6f) continue;
            tangent = tangent / tl;
            float dt_ = -Dot(vr, tangent) / k;
            const float maxF = c.Friction * c.NormalImpulse;
            const float newT = Clamp(c.TangentImpulse + dt_, -maxF, maxF);
            dt_ = newT - c.TangentImpulse;
            c.TangentImpulse = newT;
            const Vec3 jt = tangent * dt_;
            if (a.RB) a.RB->Velocity -= jt * a.InvMass;
            if (b.RB) b.RB->Velocity += jt * b.InvMass;
        }
    }

    // 6. Integrate velocities -> positions.
    for (Body& b : bodies) {
        if (!b.Moves() || b.RB->Sleeping) continue;
        b.Transform->Position += b.RB->Velocity * dt;
    }

    // 7. Positional correction (prevents sinking / stacking drift).
    for (const Contact& c : contacts) {
        Body& a = bodies[c.A];
        Body& b = bodies[c.B];
        const float k = a.InvMass + b.InvMass;
        if (k <= 0.0f) continue;
        const float amount = std::max(c.Penetration - Settings.PenetrationSlop, 0.0f) / k * Settings.CorrectionPercent;
        const Vec3 corr = c.Normal * amount;
        if (a.InvMass > 0.0f) a.Transform->Position -= corr * a.InvMass;
        if (b.InvMass > 0.0f) b.Transform->Position += corr * b.InvMass;
    }

    // 8. Sleeping.
    for (Body& b : bodies) {
        if (!b.RB || b.RB->Type != BodyType::Dynamic || b.RB->Sleeping) continue;
        if (Length(b.RB->Velocity) < Settings.SleepVelocity) {
            b.RB->SleepTimer += dt;
            if (b.RB->SleepTimer > Settings.SleepTime) { b.RB->Sleeping = true; b.RB->Velocity = Vec3(0.0f); }
        } else {
            b.RB->SleepTimer = 0.0f;
        }
    }
}

std::optional<RaycastHit> PhysicsWorld::Raycast(Registry& registry, const Ray& rayIn, float maxDistance, EntityID ignore) const {
    Ray ray{rayIn.Origin, Normalize(rayIn.Direction)};
    std::optional<RaycastHit> best;
    registry.Each<TransformComponent, ColliderComponent>([&](EntityID id, TransformComponent& t, ColliderComponent& c) {
        if (c.IsTrigger || id == ignore) return;
        float dist = 0.0f;
        Vec3 normal;
        if (c.Shape == ColliderShape::Sphere) {
            const Sphere s = ColliderWorldSphere(t, c);
            if (!RayIntersectsSphere(ray, s, dist)) return;
            normal = Normalize(ray.At(dist) - s.Center);
        } else {
            const AABB box = ColliderWorldAABB(t, c);
            if (!RayIntersectsAABB(ray, box, dist)) return;
            // Normal = face of the box closest to the hit point.
            const Vec3 local = ray.At(dist) - box.Center();
            const Vec3 e = box.Extents();
            int axis = 0;
            float bestRatio = -1.0f;
            for (int i = 0; i < 3; ++i) {
                const float r = e[i] > 0.0f ? std::fabs(local[i]) / e[i] : 0.0f;
                if (r > bestRatio) { bestRatio = r; axis = i; }
            }
            normal = Vec3(0.0f);
            normal[axis] = local[axis] < 0.0f ? -1.0f : 1.0f;
        }
        if (dist > maxDistance || (best && dist >= best->Distance)) return;
        best = RaycastHit{id, ray.At(dist), normal, dist};
    });
    return best;
}

std::vector<EntityID> PhysicsWorld::OverlapSphere(Registry& registry, const Vec3& center, float radius) const {
    std::vector<EntityID> result;
    const Sphere probe{center, radius};
    registry.Each<TransformComponent, ColliderComponent>([&](EntityID id, TransformComponent& t, ColliderComponent& c) {
        Vec3 n; float pen;
        const bool hit = c.Shape == ColliderShape::Sphere ? SphereSphere(probe, ColliderWorldSphere(t, c), n, pen)
                                                          : BoxSphere(ColliderWorldAABB(t, c), probe, n, pen);
        if (hit) result.push_back(id);
    });
    return result;
}

} // namespace Lapis
