#pragma once

#include "Lapis/Math/Math.h"

namespace Lapis {

// Projection settings. The view matrix comes from the owning transform.
struct Camera {
    enum class Projection { Perspective, Orthographic };

    Projection Type = Projection::Perspective;
    float FieldOfView = Radians(60.0f); // vertical, radians
    float OrthoSize   = 10.0f;          // half-height in world units
    float NearClip    = 0.1f;
    float FarClip     = 500.0f;

    Mat4 GetProjection(float aspect) const {
        if (Type == Projection::Orthographic) {
            const float h = OrthoSize, w = OrthoSize * aspect;
            return Orthographic(-w, w, -h, h, NearClip, FarClip);
        }
        return Perspective(FieldOfView, aspect, NearClip, FarClip);
    }
};

// Everything the renderer needs to know about the viewpoint for one frame.
struct CameraView {
    Mat4  View = Mat4::Identity();
    Mat4  Projection = Mat4::Identity();
    Vec3  Position;
    float NearClip = 0.1f;
    float FarClip = 500.0f;

    Mat4 ViewProjection() const { return Projection * View; }
    Vec3 Forward() const { return Normalize(Vec3(-View(2, 0), -View(2, 1), -View(2, 2))); }
};

} // namespace Lapis
