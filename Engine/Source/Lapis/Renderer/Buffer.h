#pragma once
// GPU buffers and vertex arrays.

#include <initializer_list>
#include <vector>

#include "Lapis/Core/Base.h"

namespace Lapis {

enum class BufferUsage { Static, Dynamic, Stream };

class VertexBuffer : NonCopyable {
public:
    VertexBuffer(const void* data, size_t size, BufferUsage usage = BufferUsage::Static);
    ~VertexBuffer();

    void Bind() const;
    // Replaces the contents; grows the buffer if needed.
    void SetData(const void* data, size_t size);
    size_t GetSize() const { return m_Size; }

private:
    u32 m_ID = 0;
    size_t m_Size = 0;
    BufferUsage m_Usage;
};

class IndexBuffer : NonCopyable {
public:
    IndexBuffer(const u32* indices, u32 count);
    ~IndexBuffer();

    void Bind() const;
    u32 GetCount() const { return m_Count; }

private:
    u32 m_ID = 0;
    u32 m_Count = 0;
};

enum class AttribType { Float, Float2, Float3, Float4 };

struct VertexAttrib {
    AttribType Type;
    bool Normalized = false;
};

// Owns a VAO describing how vertex buffer data maps to shader inputs.
// Attribute i is bound to `layout(location = i)` in the shader.
class VertexArray : NonCopyable {
public:
    VertexArray();
    ~VertexArray();

    void Bind() const;
    static void Unbind();

    void AddVertexBuffer(Ref<VertexBuffer> vb, std::initializer_list<VertexAttrib> layout);
    void SetIndexBuffer(Ref<IndexBuffer> ib);

    const Ref<IndexBuffer>& GetIndexBuffer() const { return m_IndexBuffer; }

private:
    u32 m_ID = 0;
    u32 m_NextAttrib = 0;
    std::vector<Ref<VertexBuffer>> m_VertexBuffers;
    Ref<IndexBuffer> m_IndexBuffer;
};

} // namespace Lapis
