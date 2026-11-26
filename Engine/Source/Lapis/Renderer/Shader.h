#pragma once

#include <string>
#include <unordered_map>

#include "Lapis/Core/Base.h"
#include "Lapis/Math/Math.h"

namespace Lapis {

// A linked GLSL vertex + fragment program with a uniform location cache.
class Shader : NonCopyable {
public:
    Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
    ~Shader();

    // Loads "<path>.vert" / "<path>.frag"-style files from disk.
    static Ref<Shader> FromFiles(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);

    bool IsValid() const { return m_Program != 0; }
    const std::string& GetName() const { return m_Name; }
    u32 GetID() const { return m_Program; }

    void Bind() const;

    void SetInt(const std::string& name, int v);
    void SetFloat(const std::string& name, float v);
    void SetVec2(const std::string& name, const Vec2& v);
    void SetVec3(const std::string& name, const Vec3& v);
    void SetVec4(const std::string& name, const Vec4& v);
    void SetMat4(const std::string& name, const Mat4& m);

private:
    int GetLocation(const std::string& name);

    std::string m_Name;
    u32 m_Program = 0;
    std::unordered_map<std::string, int> m_UniformCache;
};

} // namespace Lapis
