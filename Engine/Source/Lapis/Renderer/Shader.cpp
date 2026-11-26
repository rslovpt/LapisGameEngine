#include "Lapis/Renderer/Shader.h"

#include <vector>

#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Renderer/GL.h"

namespace Lapis {

namespace {
    GLuint Compile(const std::string& shaderName, GLenum type, const std::string& source) {
        GLuint shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
            glGetShaderInfoLog(shader, (GLsizei)log.size(), nullptr, log.data());
            LAPIS_ERROR("Shader '{}' ({} stage) failed to compile:\n{}", shaderName,
                        type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }
}

Shader::Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
    : m_Name(name) {
    GLuint vs = Compile(name, GL_VERTEX_SHADER, vertexSrc);
    GLuint fs = Compile(name, GL_FRAGMENT_SHADER, fragmentSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1));
        glGetProgramInfoLog(program, (GLsizei)log.size(), nullptr, log.data());
        LAPIS_ERROR("Shader '{}' failed to link:\n{}", name, log.data());
        glDeleteProgram(program);
        return;
    }
    m_Program = program;
    LAPIS_TRACE("Shader '{}' compiled", name);
}

Shader::~Shader() {
    if (m_Program) glDeleteProgram(m_Program);
}

Ref<Shader> Shader::FromFiles(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath) {
    auto vs = FileSystem::ReadText(vertexPath);
    auto fs = FileSystem::ReadText(fragmentPath);
    if (!vs || !fs) {
        LAPIS_ERROR("Shader '{}': could not read '{}' or '{}'", name, vertexPath, fragmentPath);
        return nullptr;
    }
    auto shader = CreateRef<Shader>(name, *vs, *fs);
    return shader->IsValid() ? shader : nullptr;
}

void Shader::Bind() const { glUseProgram(m_Program); }

int Shader::GetLocation(const std::string& name) {
    auto it = m_UniformCache.find(name);
    if (it != m_UniformCache.end()) return it->second;
    const int loc = glGetUniformLocation(m_Program, name.c_str());
    m_UniformCache.emplace(name, loc); // -1 is cached too (optimized-out uniforms)
    return loc;
}

void Shader::SetInt(const std::string& n, int v)          { glUniform1i(GetLocation(n), v); }
void Shader::SetFloat(const std::string& n, float v)      { glUniform1f(GetLocation(n), v); }
void Shader::SetVec2(const std::string& n, const Vec2& v) { glUniform2f(GetLocation(n), v.x, v.y); }
void Shader::SetVec3(const std::string& n, const Vec3& v) { glUniform3f(GetLocation(n), v.x, v.y, v.z); }
void Shader::SetVec4(const std::string& n, const Vec4& v) { glUniform4f(GetLocation(n), v.x, v.y, v.z, v.w); }
void Shader::SetMat4(const std::string& n, const Mat4& m) { glUniformMatrix4fv(GetLocation(n), 1, GL_FALSE, m.Data()); }

} // namespace Lapis
