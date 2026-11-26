#pragma once
// Geometric primitives and intersection tests.

#include <limits>

#include "Lapis/Math/Math.h"

namespace Lapis {

struct Ray {
    Vec3 Origin;
    Vec3 Direction; // normalized

    Vec3 At(float t) const { return Origin + Direction * t; }
};

struct Sphere {
    Vec3  Center;
    float Radius = 0.5f;
};

struct Plane {
    Vec3  Normal{0, 1, 0};
    float D = 0.0f; // plane: Dot(Normal, p) + D = 0

    Plane() = default;
    Plane(const Vec3& n, float d) : Normal(n), D(d) {}
    static Plane FromPointNormal(const Vec3& p, const Vec3& n) { Vec3 nn = Normalize(n); return {nn, -Dot(nn, p)}; }

    float SignedDistance(const Vec3& p) const { return Dot(Normal, p) + D; }
    Plane Normalized() const { float l = Length(Normal); return l > EPSILON ? Plane(Normal / l, D / l) : *this; }
};

struct AABB {
    Vec3 Min{ std::numeric_limits<float>::max()};
    Vec3 Max{-std::numeric_limits<float>::max()};

    AABB() = default;
    AABB(const Vec3& mn, const Vec3& mx) : Min(mn), Max(mx) {}
    static AABB FromCenterExtents(const Vec3& c, const Vec3& e) { return {c - e, c + e}; }

    bool  IsValid()  const { return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z; }
    Vec3  Center()   const { return (Min + Max) * 0.5f; }
    Vec3  Extents()  const { return (Max - Min) * 0.5f; }
    Vec3  Size()     const { return Max - Min; }

    void Expand(const Vec3& p) { Min = Lapis::Min(Min, p); Max = Lapis::Max(Max, p); }
    void Expand(const AABB& b) { Expand(b.Min); Expand(b.Max); }

    bool Contains(const Vec3& p) const {
        return p.x >= Min.x && p.x <= Max.x && p.y >= Min.y && p.y <= Max.y && p.z >= Min.z && p.z <= Max.z;
    }
    bool Intersects(const AABB& o) const {
        return Min.x <= o.Max.x && Max.x >= o.Min.x &&
               Min.y <= o.Max.y && Max.y >= o.Min.y &&
               Min.z <= o.Max.z && Max.z >= o.Min.z;
    }

    // Bounding box of this box after an affine transform (Arvo's method).
    AABB Transformed(const Mat4& m) const {
        const Vec3 c = m.TransformPoint(Center());
        const Vec3 e = Extents();
        Vec3 ne;
        for (int r = 0; r < 3; ++r)
            ne[r] = std::fabs(m(r, 0)) * e.x + std::fabs(m(r, 1)) * e.y + std::fabs(m(r, 2)) * e.z;
        return FromCenterExtents(c, ne);
    }

    Vec3 ClosestPoint(const Vec3& p) const { return Lapis::Max(Min, Lapis::Min(p, Max)); }
};

// Ray vs AABB slab test. On hit, tHit = entry distance (0 if origin inside).
inline bool RayIntersectsAABB(const Ray& ray, const AABB& box, float& tHit) {
    float tMin = 0.0f, tMax = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(ray.Direction[i]) < EPSILON) {
            if (ray.Origin[i] < box.Min[i] || ray.Origin[i] > box.Max[i]) return false;
        } else {
            const float inv = 1.0f / ray.Direction[i];
            float t1 = (box.Min[i] - ray.Origin[i]) * inv;
            float t2 = (box.Max[i] - ray.Origin[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return false;
        }
    }
    tHit = tMin;
    return true;
}

inline bool RayIntersectsSphere(const Ray& ray, const Sphere& s, float& tHit) {
    const Vec3 oc = ray.Origin - s.Center;
    const float b = Dot(oc, ray.Direction);
    const float c = Dot(oc, oc) - s.Radius * s.Radius;
    if (c > 0.0f && b > 0.0f) return false;
    const float disc = b * b - c;
    if (disc < 0.0f) return false;
    tHit = std::max(0.0f, -b - std::sqrt(disc));
    return true;
}

inline bool RayIntersectsPlane(const Ray& ray, const Plane& p, float& tHit) {
    const float denom = Dot(p.Normal, ray.Direction);
    if (std::fabs(denom) < EPSILON) return false;
    const float t = -(Dot(p.Normal, ray.Origin) + p.D) / denom;
    if (t < 0.0f) return false;
    tHit = t;
    return true;
}

// View frustum extracted from a view-projection matrix (Gribb/Hartmann).
struct Frustum {
    Plane Planes[6]; // left, right, bottom, top, near, far

    Frustum() = default;
    explicit Frustum(const Mat4& vp) {
        const Vec4 r0 = vp.Row(0), r1 = vp.Row(1), r2 = vp.Row(2), r3 = vp.Row(3);
        const Vec4 p[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
        for (int i = 0; i < 6; ++i) Planes[i] = Plane(p[i].xyz(), p[i].w).Normalized();
    }

    bool Contains(const Vec3& point) const {
        for (const Plane& pl : Planes) if (pl.SignedDistance(point) < 0.0f) return false;
        return true;
    }
    bool Intersects(const Sphere& s) const {
        for (const Plane& pl : Planes) if (pl.SignedDistance(s.Center) < -s.Radius) return false;
        return true;
    }
    bool Intersects(const AABB& box) const {
        for (const Plane& pl : Planes) {
            // Positive vertex: corner furthest along the plane normal.
            const Vec3 pv{pl.Normal.x >= 0 ? box.Max.x : box.Min.x,
                          pl.Normal.y >= 0 ? box.Max.y : box.Min.y,
                          pl.Normal.z >= 0 ? box.Max.z : box.Min.z};
            if (pl.SignedDistance(pv) < 0.0f) return false;
        }
        return true;
    }
};

} // namespace Lapis
