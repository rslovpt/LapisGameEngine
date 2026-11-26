#include "TestFramework.h"

#include "Lapis/Core/Log.h"
#include "Lapis/Core/FileSystem.h"
#include "Lapis/Math/Geometry.h"

using namespace Lapis;

LAPIS_TEST(Core, Format) {
    CHECK(Format("a {} b {}", 1, "x") == "a 1 b x");
    CHECK(Format("no args") == "no args");
    CHECK(Format("{}{}", 1) == "1{}"); // missing args stay visible
}

LAPIS_TEST(Core, FileExtension) {
    CHECK(FileSystem::ExtensionOf("Assets/a.OBJ") == ".obj");
    CHECK(FileSystem::ExtensionOf("Assets.dir/file").empty());
    CHECK(FileSystem::DirectoryOf("Assets/Models/a.obj") == "Assets/Models/");
}

LAPIS_TEST(Math, VectorBasics) {
    Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK_NEAR(Dot(a, b), 32.0f);
    CHECK(Cross(Vec3::Right(), Vec3::Up()) == Vec3(0, 0, 1));
    CHECK_NEAR(Length(Normalize(Vec3{3, 4, 0})), 1.0f);
    CHECK(Normalize(Vec3(0.0f)) == Vec3(0.0f));
}

LAPIS_TEST(Math, MatrixInverse) {
    Mat4 m = Translate({1, 2, 3}) * Rotate(0.7f, {0.3f, 1, 0.2f}) * Scale({2, 3, 4});
    CHECK(NearlyEqual(m * Inverse(m), Mat4::Identity()));
    CHECK(NearlyEqual(Inverse(Inverse(m)), m));
}

LAPIS_TEST(Math, TranslateAndTransform) {
    Mat4 t = Translate({5, 0, 0});
    CHECK(NearlyEqual(t.TransformPoint({1, 1, 1}), Vec3(6, 1, 1)));
    CHECK(NearlyEqual(t.TransformDirection({1, 1, 1}), Vec3(1, 1, 1)));
}

LAPIS_TEST(Math, RotateMatchesQuat) {
    const Vec3 axis = Normalize(Vec3{1, 2, 3});
    const float angle = 1.1f;
    Mat4 m = Rotate(angle, axis);
    Quat q = Quat::FromAxisAngle(axis, angle);
    CHECK(NearlyEqual(m, q.ToMat4()));
    Vec3 v{0.5f, -2.0f, 4.0f};
    CHECK(NearlyEqual(m.TransformDirection(v), q * v));
}

LAPIS_TEST(Math, QuatRotation) {
    Quat q = Quat::FromAxisAngle(Vec3::Up(), HALF_PI);
    // +90 deg around Y takes +X to -Z (right-handed).
    CHECK(NearlyEqual(q * Vec3(1, 0, 0), Vec3(0, 0, -1)));
    Quat qi = q * q.Inverse();
    CHECK_NEAR(qi.w, 1.0f);
}

LAPIS_TEST(Math, QuatEulerRoundTrip) {
    Vec3 e{0.3f, -1.2f, 0.4f};
    Vec3 back = Quat::FromEuler(e).ToEuler();
    CHECK(NearlyEqual(e, back));
}

LAPIS_TEST(Math, Slerp) {
    Quat a = Quat::Identity();
    Quat b = Quat::FromAxisAngle(Vec3::Up(), 1.0f);
    Quat h = Slerp(a, b, 0.5f);
    Quat expected = Quat::FromAxisAngle(Vec3::Up(), 0.5f);
    CHECK_NEAR(Dot(h, expected), 1.0f);
}

LAPIS_TEST(Math, ComposeTRS) {
    Vec3 t{1, 2, 3}, s{2, 2, 2};
    Quat r = Quat::FromAxisAngle({0, 0, 1}, 0.5f);
    CHECK(NearlyEqual(ComposeTRS(t, r, s), Translate(t) * r.ToMat4() * Scale(s)));
}

LAPIS_TEST(Math, LookAtPerspective) {
    Mat4 view = LookAt({0, 0, 5}, {0, 0, 0}, Vec3::Up());
    CHECK(NearlyEqual(view.TransformPoint({0, 0, 0}), Vec3(0, 0, -5)));
    Mat4 proj = Perspective(Radians(60.0f), 1.0f, 0.1f, 100.0f);
    Vec4 nearClip = proj * Vec4(0, 0, -0.1f, 1);
    Vec4 farClip  = proj * Vec4(0, 0, -100.0f, 1);
    CHECK_NEAR(nearClip.z / nearClip.w, -1.0f);
    CHECK_NEAR(farClip.z / farClip.w, 1.0f);
}

LAPIS_TEST(Geometry, RayTests) {
    Ray ray{{0, 0, 10}, {0, 0, -1}};
    float t = 0;
    CHECK(RayIntersectsAABB(ray, AABB({-1, -1, -1}, {1, 1, 1}), t));
    CHECK_NEAR(t, 9.0f);
    CHECK(RayIntersectsSphere(ray, Sphere{{0, 0, 0}, 2.0f}, t));
    CHECK_NEAR(t, 8.0f);
    CHECK(!RayIntersectsAABB(ray, AABB({2, 2, -1}, {3, 3, 1}), t));
    CHECK(RayIntersectsPlane(Ray{{0, 5, 0}, {0, -1, 0}}, Plane({0, 1, 0}, 0), t));
    CHECK_NEAR(t, 5.0f);
}

LAPIS_TEST(Geometry, AABBTransform) {
    AABB box({-1, -1, -1}, {1, 1, 1});
    AABB moved = box.Transformed(Translate({10, 0, 0}) * Scale({2, 1, 1}));
    CHECK(NearlyEqual(moved.Min, Vec3(8, -1, -1)));
    CHECK(NearlyEqual(moved.Max, Vec3(12, 1, 1)));
}

LAPIS_TEST(Geometry, Frustum) {
    Mat4 vp = Perspective(Radians(60.0f), 1.0f, 0.1f, 100.0f) * LookAt({0, 0, 0}, {0, 0, -1}, Vec3::Up());
    Frustum f(vp);
    CHECK(f.Contains({0, 0, -10}));
    CHECK(!f.Contains({0, 0, 10}));
    CHECK(f.Intersects(AABB({-1, -1, -21}, {1, 1, -19})));
    CHECK(!f.Intersects(AABB({50, -1, -6}, {52, 1, -4})));
    CHECK(f.Intersects(Sphere{{0, 0, 1}, 1.5f}));
}
