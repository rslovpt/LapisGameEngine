#include "Lapis/Renderer/Buffer.h"

#include <algorithm>

#include "Lapis/Renderer/GL.h"

namespace Lapis {

namespace {
    GLenum ToGL(BufferUsage u) {
        switch (u) {
            case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
            case BufferUsage::Stream:  return GL_STREAM_DRAW;
            default:                   return GL_STATIC_DRAW;
        }
    }
    int ComponentCount(AttribType t) {
        switch (t) {
            case AttribType::Float:  return 1;
            case AttribType::Float2: return 2;
            case AttribType::Float3: return 3;
            case AttribType::Float4: return 4;
        }
        return 0;
    }
}

// --- VertexBuffer ------------------------------------------------------------
VertexBuffer::VertexBuffer(const void* data, size_t size, BufferUsage usage) : m_Size(size), m_Usage(usage) {
    glGenBuffers(1, &m_ID);
    glBindBuffer(GL_ARRAY_BUFFER, m_ID);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size, data, ToGL(usage));
}

VertexBuffer::~VertexBuffer() { glDeleteBuffers(1, &m_ID); }

void VertexBuffer::Bind() const { glBindBuffer(GL_ARRAY_BUFFER, m_ID); }

void VertexBuffer::SetData(const void* data, size_t size) {
    glBindBuffer(GL_ARRAY_BUFFER, m_ID);
    if (m_Usage == BufferUsage::Stream) {
        // Orphan the old storage so the driver doesn't wait for the GPU.
        m_Size = std::max(size, m_Size);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_Size, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)size, data);
    } else if (size > m_Size) {
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size, data, ToGL(m_Usage));
        m_Size = size;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)size, data);
    }
}

// --- IndexBuffer -------------------------------------------------------------
IndexBuffer::IndexBuffer(const u32* indices, u32 count) : m_Count(count) {
    glGenBuffers(1, &m_ID);
    // Bind through GL_ARRAY_BUFFER so no VAO's element binding is disturbed.
    glBindBuffer(GL_ARRAY_BUFFER, m_ID);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(u32)), indices, GL_STATIC_DRAW);
}

IndexBuffer::~IndexBuffer() { glDeleteBuffers(1, &m_ID); }

void IndexBuffer::Bind() const { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ID); }

// --- VertexArray ---------------------------------------------------------------
VertexArray::VertexArray() { glGenVertexArrays(1, &m_ID); }
VertexArray::~VertexArray() { glDeleteVertexArrays(1, &m_ID); }

void VertexArray::Bind() const { glBindVertexArray(m_ID); }
void VertexArray::Unbind() { glBindVertexArray(0); }

void VertexArray::AddVertexBuffer(Ref<VertexBuffer> vb, std::initializer_list<VertexAttrib> layout) {
    glBindVertexArray(m_ID);
    vb->Bind();

    GLsizei stride = 0;
    for (const auto& a : layout) stride += ComponentCount(a.Type) * (GLsizei)sizeof(float);

    size_t offset = 0;
    for (const auto& a : layout) {
        const int n = ComponentCount(a.Type);
        glEnableVertexAttribArray(m_NextAttrib);
        glVertexAttribPointer(m_NextAttrib, n, GL_FLOAT, a.Normalized ? GL_TRUE : GL_FALSE, stride,
                              reinterpret_cast<const void*>(offset));
        offset += n * sizeof(float);
        ++m_NextAttrib;
    }
    m_VertexBuffers.push_back(std::move(vb));
    glBindVertexArray(0);
}

void VertexArray::SetIndexBuffer(Ref<IndexBuffer> ib) {
    glBindVertexArray(m_ID);
    ib->Bind();
    m_IndexBuffer = std::move(ib);
    glBindVertexArray(0);
}

} // namespace Lapis
