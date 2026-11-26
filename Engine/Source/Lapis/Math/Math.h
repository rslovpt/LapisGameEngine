#pragma once
// Lapis math library: vectors, column-major matrices (OpenGL convention),
// quaternions and transform helpers. Right-handed, +Y up, camera looks down -Z.

#include <algorithm>
#include <cmath>
#include <ostream>

namespace Lapis {

constexpr float PI      = 3.14159265358979323846f;
constexpr float TWO_PI  = 2.0f * PI;
constexpr float HALF_PI = 0.5f * PI;
constexpr float EPSILON = 1e-6f;

constexpr float Radians(float degrees) { return degrees * (PI / 180.0f); }
constexpr float Degrees(float radians) { return radians * (180.0f / PI); }

template <typename T> constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> constexpr T Lerp(T a, T b, float t) { return a + (b - a) * t; }
inline bool NearlyEqual(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// ===========================================================================
// Vec2
// ===========================================================================
struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float s) : x(s), y(s) {}
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vec2& o) const { return !(*this == o); }
};

// ===========================================================================
// Vec3
// ===========================================================================
struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float s) : x(s), y(s), z(s) {}
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3(const Vec2& v, float z_) : x(v.x), y(v.y), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3 operator/(const Vec3& o) const { return {x / o.x, y / o.y, z / o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(const Vec3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }

    static constexpr Vec3 Zero()    { return {0, 0, 0}; }
    static constexpr Vec3 One()     { return {1, 1, 1}; }
    static constexpr Vec3 Up()      { return {0, 1, 0}; }
    static constexpr Vec3 Right()   { return {1, 0, 0}; }
    static constexpr Vec3 Forward() { return {0, 0, -1}; }
};

constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }
constexpr Vec2 operator*(float s, const Vec2& v) { return v * s; }

// ===========================================================================
// Vec4
// ===========================================================================
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr Vec4() = default;
    constexpr Vec4(float s) : x(s), y(s), z(s), w(s) {}
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    constexpr Vec3 xyz() const { return {x, y, z}; }
    constexpr Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    constexpr Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    constexpr Vec4 operator*(const Vec4& o) const { return {x * o.x, y * o.y, z * o.z, w * o.w}; }
    constexpr Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    constexpr Vec4 operator/(float s) const { return {x / s, y / s, z / s, w / s}; }
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
    constexpr bool operator==(const Vec4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
};

// --- vector functions ------------------------------------------------------
constexpr float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
constexpr float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr float Dot(const Vec4& a, const Vec4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(const Vec2& v) { return std::sqrt(Dot(v, v)); }
inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
inline float Length(const Vec4& v) { return std::sqrt(Dot(v, v)); }
constexpr float LengthSq(const Vec3& v) { return Dot(v, v); }
inline float Distance(const Vec3& a, const Vec3& b) { return Length(a - b); }
inline Vec2 Normalize(const Vec2& v) { float l = Length(v); return l > EPSILON ? v / l : Vec2(0.0f); }
inline Vec3 Normalize(const Vec3& v) { float l = Length(v); return l > EPSILON ? v / l : Vec3(0.0f); }
inline Vec4 Normalize(const Vec4& v) { float l = Length(v); return l > EPSILON ? v / l : Vec4(0.0f); }
inline Vec3 Min(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 Max(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline Vec3 Abs(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }
inline Vec3 Reflect(const Vec3& v, const Vec3& n) { return v - n * (2.0f * Dot(v, n)); }
inline bool NearlyEqual(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return NearlyEqual(a.x, b.x, eps) && NearlyEqual(a.y, b.y, eps) && NearlyEqual(a.z, b.z, eps);
}

inline std::ostream& operator<<(std::ostream& os, const Vec2& v) { return os << "(" << v.x << ", " << v.y << ")"; }
inline std::ostream& operator<<(std::ostream& os, const Vec3& v) { return os << "(" << v.x << ", " << v.y << ", " << v.z << ")"; }
inline std::ostream& operator<<(std::ostream& os, const Vec4& v) { return os << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")"; }

// ===========================================================================
// Mat4 - column-major: element (row r, column c) is m[c * 4 + r].
// ===========================================================================
struct Mat4 {
    float m[16] = {};

    constexpr Mat4() = default;
    // Diagonal matrix (Mat4(1) = identity).
    explicit constexpr Mat4(float d) : m{d, 0, 0, 0, 0, d, 0, 0, 0, 0, d, 0, 0, 0, 0, d} {}

    static constexpr Mat4 Identity() { return Mat4(1.0f); }

    float& operator()(int row, int col) { return m[col * 4 + row]; }
    constexpr float operator()(int row, int col) const { return m[col * 4 + row]; }

    Vec4 Column(int c) const { return {m[c * 4], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]}; }
    void SetColumn(int c, const Vec4& v) { m[c * 4] = v.x; m[c * 4 + 1] = v.y; m[c * 4 + 2] = v.z; m[c * 4 + 3] = v.w; }
    Vec4 Row(int r) const { return {m[r], m[4 + r], m[8 + r], m[12 + r]}; }

    const float* Data() const { return m; }

    Mat4 operator*(const Mat4& o) const {
        Mat4 out;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += (*this)(r, k) * o(k, c);
                out(r, c) = sum;
            }
        return out;
    }
    Vec4 operator*(const Vec4& v) const {
        return {
            m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
    }
    Mat4& operator*=(const Mat4& o) { *this = *this * o; return *this; }

    Vec3 TransformPoint(const Vec3& p) const {
        Vec4 r = *this * Vec4(p, 1.0f);
        return std::fabs(r.w) > EPSILON && r.w != 1.0f ? r.xyz() / r.w : r.xyz();
    }
    Vec3 TransformDirection(const Vec3& d) const { return (*this * Vec4(d, 0.0f)).xyz(); }
    Vec3 GetTranslation() const { return {m[12], m[13], m[14]}; }
};

inline Mat4 Transpose(const Mat4& a) {
    Mat4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out(r, c) = a(c, r);
    return out;
}

// General 4x4 inverse (cofactor expansion). Returns identity for singular input.
inline Mat4 Inverse(const Mat4& mat) {
    const float* m = mat.m;
    float inv[16];
    inv[0]  =  m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8]  =  m[4] * m[9]  * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5]  =  m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9]  = -m[0] * m[9]  * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] =  m[0] * m[9]  * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2]  =  m[1] * m[6]  * m[15] - m[1] * m[7]  * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]  - m[13] * m[3] * m[6];
    inv[6]  = -m[0] * m[6]  * m[15] + m[0] * m[7]  * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]  + m[12] * m[3] * m[6];
    inv[10] =  m[0] * m[5]  * m[15] - m[0] * m[7]  * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]  - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5]  * m[14] + m[0] * m[6]  * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]  + m[12] * m[2] * m[5];
    inv[3]  = -m[1] * m[6]  * m[11] + m[1] * m[7]  * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9]  * m[2] * m[7]  + m[9]  * m[3] * m[6];
    inv[7]  =  m[0] * m[6]  * m[11] - m[0] * m[7]  * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8]  * m[2] * m[7]  - m[8]  * m[3] * m[6];
    inv[11] = -m[0] * m[5]  * m[11] + m[0] * m[7]  * m[9]  + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]  - m[8]  * m[1] * m[7]  + m[8]  * m[3] * m[5];
    inv[15] =  m[0] * m[5]  * m[10] - m[0] * m[6]  * m[9]  - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]  + m[8]  * m[1] * m[6]  - m[8]  * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return Mat4::Identity();
    det = 1.0f / det;
    Mat4 out;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * det;
    return out;
}

inline Mat4 Translate(const Vec3& t) {
    Mat4 r(1.0f);
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

inline Mat4 Scale(const Vec3& s) {
    Mat4 r(1.0f);
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}

// Rotation of `angle` radians around (normalized) `axis`.
inline Mat4 Rotate(float angle, const Vec3& axisIn) {
    const Vec3 a = Normalize(axisIn);
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    Mat4 r(1.0f);
    r(0, 0) = t * a.x * a.x + c;       r(0, 1) = t * a.x * a.y - s * a.z; r(0, 2) = t * a.x * a.z + s * a.y;
    r(1, 0) = t * a.x * a.y + s * a.z; r(1, 1) = t * a.y * a.y + c;       r(1, 2) = t * a.y * a.z - s * a.x;
    r(2, 0) = t * a.x * a.z - s * a.y; r(2, 1) = t * a.y * a.z + s * a.x; r(2, 2) = t * a.z * a.z + c;
    return r;
}

// Right-handed perspective projection, clip depth in [-1, 1] (OpenGL).
inline Mat4 Perspective(float fovY, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r;
    r(0, 0) = f / aspect;
    r(1, 1) = f;
    r(2, 2) = (zFar + zNear) / (zNear - zFar);
    r(2, 3) = (2.0f * zFar * zNear) / (zNear - zFar);
    r(3, 2) = -1.0f;
    return r;
}

inline Mat4 Orthographic(float left, float right, float bottom, float top, float zNear, float zFar) {
    Mat4 r(1.0f);
    r(0, 0) = 2.0f / (right - left);
    r(1, 1) = 2.0f / (top - bottom);
    r(2, 2) = -2.0f / (zFar - zNear);
    r(0, 3) = -(right + left) / (right - left);
    r(1, 3) = -(top + bottom) / (top - bottom);
    r(2, 3) = -(zFar + zNear) / (zFar - zNear);
    return r;
}

inline Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = Normalize(target - eye);
    Vec3 s = Cross(f, up);
    if (LengthSq(s) < 1e-8f) s = Cross(f, std::fabs(f.y) < 0.99f ? Vec3::Up() : Vec3::Right());
    s = Normalize(s);
    const Vec3 u = Cross(s, f);
    Mat4 r(1.0f);
    r(0, 0) = s.x;  r(0, 1) = s.y;  r(0, 2) = s.z;  r(0, 3) = -Dot(s, eye);
    r(1, 0) = u.x;  r(1, 1) = u.y;  r(1, 2) = u.z;  r(1, 3) = -Dot(u, eye);
    r(2, 0) = -f.x; r(2, 1) = -f.y; r(2, 2) = -f.z; r(2, 3) = Dot(f, eye);
    return r;
}

inline bool NearlyEqual(const Mat4& a, const Mat4& b, float eps = 1e-4f) {
    for (int i = 0; i < 16; ++i) if (!NearlyEqual(a.m[i], b.m[i], eps)) return false;
    return true;
}

// ===========================================================================
// Quat - unit quaternion rotation (x, y, z = vector part, w = scalar part)
// ===========================================================================
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static constexpr Quat Identity() { return {0, 0, 0, 1}; }

    static Quat FromAxisAngle(const Vec3& axis, float angle) {
        const Vec3 a = Normalize(axis);
        const float s = std::sin(angle * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
    }

    // Euler angles in radians: pitch (X), yaw (Y), roll (Z). Applied yaw * pitch * roll.
    static Quat FromEuler(const Vec3& euler) {
        return FromAxisAngle(Vec3::Up(), euler.y) * FromAxisAngle(Vec3::Right(), euler.x) *
               FromAxisAngle({0, 0, 1}, euler.z);
    }

    Quat operator*(const Quat& q) const {
        return {
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z};
    }

    Vec3 operator*(const Vec3& v) const {
        const Vec3 u{x, y, z};
        const Vec3 t = Cross(u, v) * 2.0f;
        return v + t * w + Cross(u, t);
    }

    Quat Conjugate() const { return {-x, -y, -z, w}; }
    float LengthSq() const { return x * x + y * y + z * z + w * w; }
    Quat Normalized() const {
        const float l = std::sqrt(LengthSq());
        return l > EPSILON ? Quat{x / l, y / l, z / l, w / l} : Identity();
    }
    Quat Inverse() const {
        const float l2 = LengthSq();
        return l2 > EPSILON ? Quat{-x / l2, -y / l2, -z / l2, w / l2} : Identity();
    }

    Mat4 ToMat4() const {
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;
        Mat4 r(1.0f);
        r(0, 0) = 1 - 2 * (yy + zz); r(0, 1) = 2 * (xy - wz);     r(0, 2) = 2 * (xz + wy);
        r(1, 0) = 2 * (xy + wz);     r(1, 1) = 1 - 2 * (xx + zz); r(1, 2) = 2 * (yz - wx);
        r(2, 0) = 2 * (xz - wy);     r(2, 1) = 2 * (yz + wx);     r(2, 2) = 1 - 2 * (xx + yy);
        return r;
    }

    // Euler angles (pitch, yaw, roll) matching FromEuler's yaw*pitch*roll order.
    Vec3 ToEuler() const {
        const Mat4 m = ToMat4();
        Vec3 e;
        e.x = std::asin(Clamp(-m(1, 2), -1.0f, 1.0f));
        if (std::fabs(m(1, 2)) < 0.9999f) {
            e.y = std::atan2(m(0, 2), m(2, 2));
            e.z = std::atan2(m(1, 0), m(1, 1));
        } else {
            e.y = std::atan2(-m(2, 0), m(0, 0));
            e.z = 0.0f;
        }
        return e;
    }
};

inline float Dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline Quat Slerp(const Quat& a, Quat b, float t) {
    float cosTheta = Dot(a, b);
    if (cosTheta < 0.0f) { b = {-b.x, -b.y, -b.z, -b.w}; cosTheta = -cosTheta; }
    if (cosTheta > 0.9995f) {
        return Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.Normalized();
    }
    const float theta = std::acos(cosTheta);
    const float sinTheta = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / sinTheta;
    const float wb = std::sin(t * theta) / sinTheta;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Translation * Rotation * Scale.
inline Mat4 ComposeTRS(const Vec3& t, const Quat& r, const Vec3& s) {
    Mat4 m = r.ToMat4();
    m(0, 0) *= s.x; m(1, 0) *= s.x; m(2, 0) *= s.x;
    m(0, 1) *= s.y; m(1, 1) *= s.y; m(2, 1) *= s.y;
    m(0, 2) *= s.z; m(1, 2) *= s.z; m(2, 2) *= s.z;
    m(0, 3) = t.x; m(1, 3) = t.y; m(2, 3) = t.z;
    return m;
}

} // namespace Lapis
