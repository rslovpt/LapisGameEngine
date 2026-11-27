// OpenGL 4.5 implementation of the Lapis RHI.
//
//  * Vertex input uses separate attribute formats (one VAO per pipeline,
//    buffers bound per draw with glBindVertexBuffer).
//  * Constants and transient vertex data are written into ring buffers that
//    are orphaned at the start of every frame (no GPU stalls). If a ring
//    fills up mid-frame another buffer is added, so data already bound is
//    never overwritten.
//  * Samplers are sampler objects bound per texture unit.

#include <algorithm>
#include <cstring>

#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Window.h"
#include "Lapis/RHI/RHI.h"
#include "Lapis/Renderer/GL.h"
#include "Lapis/Renderer/ShaderLibrary.h"

namespace Lapis::RHI {

namespace {

struct GLFormat { GLenum Internal, Format, Type; };
GLFormat ToGL(Format f) {
    switch (f) {
        case Format::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case Format::RGBA8_SRGB: return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case Format::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case Format::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
        case Format::R32F: return {GL_R32F, GL_RED, GL_FLOAT};
        case Format::D24: return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT};
        case Format::D32F: return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
        default: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    }
}

// Slot table with a free list; handle = index + 1.
template <typename T>
struct Pool {
    std::vector<T> Items;
    std::vector<bool> Alive;
    std::vector<u32> Free;
    u32 Add(T item) {
        u32 i;
        if (!Free.empty()) { i = Free.back(); Free.pop_back(); Items[i] = std::move(item); Alive[i] = true; }
        else { i = (u32)Items.size(); Items.push_back(std::move(item)); Alive.push_back(true); }
        return i + 1;
    }
    T* Get(u32 h) { return h && h <= Items.size() && Alive[h - 1] ? &Items[h - 1] : nullptr; }
    const T* Get(u32 h) const { return h && h <= Items.size() && Alive[h - 1] ? &Items[h - 1] : nullptr; }
    void Remove(u32 h) { if (Get(h)) { Alive[h - 1] = false; Items[h - 1] = T{}; Free.push_back(h - 1); } }
    template <typename F> void ForEach(F f) { for (size_t i = 0; i < Items.size(); ++i) if (Alive[i]) f(Items[i]); }
};

struct GLBuffer { GLuint ID = 0; GLenum Target = 0; u64 Size = 0; };
struct GLTexture { GLuint Tex = 0; GLuint RBO = 0; TextureDesc Desc; };
struct GLRenderTarget { GLuint FBO = 0; TextureHandle Color, Depth; u32 Width = 0, Height = 0; };
struct GLPipeline { GLuint Program = 0; GLuint VAO = 0; PipelineDesc Desc; };

// Growing set of stream buffers, reset each frame.
struct Ring {
    GLenum Target = GL_ARRAY_BUFFER;
    u64 Capacity = 0;
    u32 Alignment = 16;
    std::vector<BufferHandle> Buffers;   // registered in the buffer pool
    size_t Current = 0;
    u64 Offset = 0;
};

GLuint Compile(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LAPIS_ERROR("GL: {} shader '{}' failed:\n{}", type == GL_VERTEX_SHADER ? "vertex" : "fragment", name, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

class GLDevice final : public Device {
public:
    explicit GLDevice(Window& window) : m_Window(window) {}

    bool Init(std::string* error) {
        auto str = [](GLenum e) { const GLubyte* s = glGetString(e); return s ? std::string((const char*)s) : std::string(); };
        m_Info.Api = API::OpenGL;
        m_Info.GPUName = str(GL_RENDERER);
        m_Info.Vendor = str(GL_VENDOR);
        m_Info.Version = "OpenGL " + str(GL_VERSION);
        GLint samples = 1, align = 256;
        glGetIntegerv(GL_MAX_SAMPLES, &samples);
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
        m_Info.MaxSamples = (u32)std::max(samples, 1);
        GLfloat aniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &aniso);
        while (glGetError() != GL_NO_ERROR) {}
        m_Info.MaxAnisotropy = std::max(1.0f, (float)aniso);
        if (!glBindVertexBuffer || !glBindSampler) {
            if (error) *error = "OpenGL 4.3+ required (separate vertex formats, sampler objects)";
            return false;
        }

        // Samplers (indices match RHI::Sampler).
        glGenSamplers((GLsizei)Sampler::Count, m_Samplers);
        auto setup = [](GLuint s, GLenum minF, GLenum magF, GLenum wrap) {
            glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, (GLint)minF);
            glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, (GLint)magF);
            glSamplerParameteri(s, GL_TEXTURE_WRAP_S, (GLint)wrap);
            glSamplerParameteri(s, GL_TEXTURE_WRAP_T, (GLint)wrap);
        };
        setup(m_Samplers[(int)Sampler::LinearClamp], GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        setup(m_Samplers[(int)Sampler::LinearWrap], GL_LINEAR, GL_LINEAR, GL_REPEAT);
        setup(m_Samplers[(int)Sampler::PointClamp], GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);
        setup(m_Samplers[(int)Sampler::AnisoWrap], GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT);
        const GLuint shadow = m_Samplers[(int)Sampler::ShadowCompare];
        setup(shadow, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_BORDER);
        const float border[4] = {1, 1, 1, 1};
        glSamplerParameterfv(shadow, GL_TEXTURE_BORDER_COLOR, border);
        glSamplerParameteri(shadow, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(shadow, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        SetMaxAnisotropy(8.0f);

        m_Constants.Target = GL_UNIFORM_BUFFER;
        m_Constants.Capacity = 2u << 20;
        m_Constants.Alignment = (u32)std::max(align, 16);
        m_Vertices.Target = GL_ARRAY_BUFFER;
        m_Vertices.Capacity = 8u << 20;
        m_Vertices.Alignment = 16;

        glGenVertexArrays(1, &m_EmptyVAO);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        glFrontFace(GL_CCW);
        return true;
    }

    ~GLDevice() override {
        m_Pipelines.ForEach([](GLPipeline& p) { glDeleteProgram(p.Program); glDeleteVertexArrays(1, &p.VAO); });
        m_Targets.ForEach([](GLRenderTarget& t) { glDeleteFramebuffers(1, &t.FBO); });
        m_Textures.ForEach([](GLTexture& t) { if (t.Tex) glDeleteTextures(1, &t.Tex); if (t.RBO) glDeleteRenderbuffers(1, &t.RBO); });
        m_Buffers.ForEach([](GLBuffer& b) { glDeleteBuffers(1, &b.ID); });
        glDeleteSamplers((GLsizei)Sampler::Count, m_Samplers);
        glDeleteVertexArrays(1, &m_EmptyVAO);
    }

    const DeviceInfo& GetInfo() const override { return m_Info; }

    // ---- resources -------------------------------------------------------------
    BufferHandle CreateBuffer(const BufferDesc& desc) override {
        GLBuffer b;
        b.Target = desc.Type == BufferType::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        b.Size = desc.Size;
        glGenBuffers(1, &b.ID);
        // Upload through GL_ARRAY_BUFFER (buffers are typeless in GL): binding an
        // element buffer here would change the currently bound vertex array.
        glBindBuffer(GL_ARRAY_BUFFER, b.ID);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)desc.Size, desc.Data, desc.Data ? GL_STATIC_DRAW : GL_STREAM_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        BufferHandle h;
        h.ID = m_Buffers.Add(b);
        return h;
    }

    TextureHandle CreateTexture(const TextureDesc& desc) override {
        GLTexture t;
        t.Desc = desc;
        t.Desc.Data = nullptr;
        const GLFormat f = ToGL(desc.Fmt);
        const GLsizei w = (GLsizei)std::max(desc.Width, 1u), hgt = (GLsizei)std::max(desc.Height, 1u);
        if (desc.Samples > 1) {
            glGenRenderbuffers(1, &t.RBO);
            glBindRenderbuffer(GL_RENDERBUFFER, t.RBO);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, (GLsizei)desc.Samples, f.Internal, w, hgt);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        } else {
            glGenTextures(1, &t.Tex);
            glBindTexture(GL_TEXTURE_2D, t.Tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)f.Internal, w, hgt, 0, f.Format, f.Type, desc.Data);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            const bool mips = desc.Mipmaps && desc.Data;
            if (mips) glGenerateMipmap(GL_TEXTURE_2D);
            // Without this a mipmapping sampler would make a mip-less texture incomplete.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mips ? 1000 : 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        TextureHandle h;
        h.ID = m_Textures.Add(t);
        return h;
    }

    RenderTargetHandle CreateRenderTarget(TextureHandle color, TextureHandle depth, const char* name) override {
        GLRenderTarget rt;
        rt.Color = color;
        rt.Depth = depth;
        glGenFramebuffers(1, &rt.FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.FBO);
        auto attach = [&](TextureHandle th, GLenum point) {
            const GLTexture* t = m_Textures.Get(th.ID);
            if (!t) return;
            rt.Width = t->Desc.Width;
            rt.Height = t->Desc.Height;
            if (t->RBO) glFramebufferRenderbuffer(GL_FRAMEBUFFER, point, GL_RENDERBUFFER, t->RBO);
            else glFramebufferTexture2D(GL_FRAMEBUFFER, point, GL_TEXTURE_2D, t->Tex, 0);
        };
        attach(color, GL_COLOR_ATTACHMENT0);
        attach(depth, GL_DEPTH_ATTACHMENT);
        if (color) { glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0); }
        else { glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE); }
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            LAPIS_ERROR("GL: render target '{}' ({}x{}) is incomplete", name, rt.Width, rt.Height);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        RenderTargetHandle h;
        h.ID = m_Targets.Add(rt);
        return h;
    }

    PipelineHandle CreatePipeline(const PipelineDesc& desc) override {
        const ShaderLibrary::Source* src = ShaderLibrary::Find(desc.Shader.c_str());
        if (!src || !src->GLSLVertex) { LAPIS_ERROR("GL: shader '{}' not found", desc.Shader); return {}; }
        GLuint vs = Compile(GL_VERTEX_SHADER, src->GLSLVertex, src->Name);
        GLuint fs = Compile(GL_FRAGMENT_SHADER, src->GLSLFragment, src->Name);
        if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return {}; }
        GLPipeline p;
        p.Desc = desc;
        p.Program = glCreateProgram();
        glAttachShader(p.Program, vs);
        glAttachShader(p.Program, fs);
        glLinkProgram(p.Program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(p.Program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetProgramInfoLog(p.Program, sizeof(log), nullptr, log);
            LAPIS_ERROR("GL: shader '{}' failed to link:\n{}", src->Name, log);
            glDeleteProgram(p.Program);
            return {};
        }
        // Fixed bindings: CB0/CB1 -> 0/1, samplers t0..t7 -> units 0..7.
        for (GLuint i = 0; i < 2; ++i) {
            const char* block = i == 0 ? "CB0" : "CB1";
            const GLuint index = glGetUniformBlockIndex(p.Program, block);
            if (index != GL_INVALID_INDEX) glUniformBlockBinding(p.Program, index, i);
        }
        glUseProgram(p.Program);
        for (int i = 0; i < 8; ++i) {
            const char names[8][3] = {"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7"};
            const GLint loc = glGetUniformLocation(p.Program, names[i]);
            if (loc >= 0) glUniform1i(loc, i);
        }
        glUseProgram(0);

        glGenVertexArrays(1, &p.VAO);
        glBindVertexArray(p.VAO);
        static const GLint counts[] = {1, 2, 3, 4};
        for (const VertexAttribute& a : desc.Attributes) {
            glEnableVertexAttribArray(a.Location);
            glVertexAttribFormat(a.Location, counts[(int)a.Fmt], GL_FLOAT, GL_FALSE, a.Offset);
            glVertexAttribBinding(a.Location, a.Slot);
        }
        glVertexBindingDivisor(1, desc.Slot1PerInstance ? 1 : 0);
        glBindVertexArray(0);

        PipelineHandle h;
        h.ID = m_Pipelines.Add(p);
        return h;
    }

    void Destroy(BufferHandle h) override { if (GLBuffer* b = m_Buffers.Get(h.ID)) { glDeleteBuffers(1, &b->ID); m_Buffers.Remove(h.ID); } }
    void Destroy(TextureHandle h) override {
        if (GLTexture* t = m_Textures.Get(h.ID)) {
            if (t->Tex) glDeleteTextures(1, &t->Tex);
            if (t->RBO) glDeleteRenderbuffers(1, &t->RBO);
            m_Textures.Remove(h.ID);
        }
    }
    void Destroy(RenderTargetHandle h) override { if (GLRenderTarget* t = m_Targets.Get(h.ID)) { glDeleteFramebuffers(1, &t->FBO); m_Targets.Remove(h.ID); } }
    void Destroy(PipelineHandle h) override {
        if (GLPipeline* p = m_Pipelines.Get(h.ID)) {
            glDeleteProgram(p->Program);
            glDeleteVertexArrays(1, &p->VAO);
            if (m_Bound == p) m_Bound = nullptr;
            m_Pipelines.Remove(h.ID);
        }
    }
    const TextureDesc* GetTextureDesc(TextureHandle h) const override { const GLTexture* t = m_Textures.Get(h.ID); return t ? &t->Desc : nullptr; }

    // ---- frame ----------------------------------------------------------------
    void BeginFrame(u32 width, u32 height) override {
        m_BackWidth = std::max(width, 1u);
        m_BackHeight = std::max(height, 1u);
        FrameDrawCalls = 0;
        ResetRing(m_Constants);
        ResetRing(m_Vertices);
    }
    void EndFrame() override {
        glBindVertexArray(0);
        m_Window.SwapBuffers();
    }
    void SetVSync(bool enabled) override { m_Window.SetVSync(enabled); }
    void SetMaxAnisotropy(float level) override {
        const float a = std::max(1.0f, std::min(level, m_Info.MaxAnisotropy));
        if (m_Info.MaxAnisotropy > 1.0f) glSamplerParameterf(m_Samplers[(int)Sampler::AnisoWrap], GL_TEXTURE_MAX_ANISOTROPY, a);
        while (glGetError() != GL_NO_ERROR) {}
    }

    // ---- commands ---------------------------------------------------------------
    void BeginPass(RenderTargetHandle rt, const ClearValues& clear) override {
        u32 w = m_BackWidth, h = m_BackHeight;
        GLuint fbo = m_Window.GetBackbufferFBO();
        if (const GLRenderTarget* t = m_Targets.Get(rt.ID)) { fbo = t->FBO; w = t->Width; h = t->Height; }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, (GLsizei)w, (GLsizei)h);
        glDisable(GL_SCISSOR_TEST);
        GLbitfield mask = 0;
        if (clear.Color) { glColorMask(1, 1, 1, 1); glClearColor(clear.Rgba[0], clear.Rgba[1], clear.Rgba[2], clear.Rgba[3]); mask |= GL_COLOR_BUFFER_BIT; }
        if (clear.Depth) { glDepthMask(GL_TRUE); glClearDepth(clear.DepthValue); mask |= GL_DEPTH_BUFFER_BIT; }
        if (mask) glClear(mask);
        m_Bound = nullptr;
    }
    void EndPass() override {}

    void SetPipeline(PipelineHandle h) override {
        GLPipeline* p = m_Pipelines.Get(h.ID);
        if (!p) { m_Bound = nullptr; return; }
        m_Bound = p;
        const PipelineDesc& d = p->Desc;
        glUseProgram(p->Program);
        glBindVertexArray(d.Attributes.empty() ? m_EmptyVAO : p->VAO);
        switch (d.Depth) {
            case DepthMode::Off: glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); break;
            case DepthMode::Less: glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); break;
            case DepthMode::LessEqualRead: glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); break;
            case DepthMode::Always: glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE); break;
        }
        if (d.Cull == CullMode::Back) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); } else glDisable(GL_CULL_FACE);
        switch (d.Blend) {
            case BlendMode::Opaque: glDisable(GL_BLEND); break;
            case BlendMode::Alpha: glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
            case BlendMode::Additive: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); break;
        }
        if (d.DepthBias != 0.0f || d.SlopeBias != 0.0f) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(d.SlopeBias, d.DepthBias); }
        else glDisable(GL_POLYGON_OFFSET_FILL);
        const bool color = d.ColorFormat != Format::None;
        glColorMask(color, color, color, color);
    }

    void SetConstants(u32 slot, const void* data, u32 size) override {
        u64 offset = 0;
        const GLBuffer* b = Allocate(m_Constants, data, size, offset);
        if (b) glBindBufferRange(GL_UNIFORM_BUFFER, slot, b->ID, (GLintptr)offset, (GLsizeiptr)size);
    }

    void SetTexture(u32 slot, TextureHandle h, Sampler s) override {
        const GLTexture* t = m_Textures.Get(h.ID);
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, t ? t->Tex : 0);
        glBindSampler(slot, m_Samplers[(int)s]);
    }

    void SetVertexBuffer(u32 slot, const BufferView& view) override {
        const GLBuffer* b = m_Buffers.Get(view.Buffer.ID);
        u32 stride = view.Stride;
        if (m_Bound && slot < 2 && m_Bound->Desc.Strides[slot]) stride = m_Bound->Desc.Strides[slot];
        glBindVertexBuffer(slot, b ? b->ID : 0, (GLintptr)view.Offset, (GLsizei)stride);
    }

    void SetIndexBuffer(const BufferView& view) override {
        const GLBuffer* b = m_Buffers.Get(view.Buffer.ID);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b ? b->ID : 0);   // recorded in the bound VAO
        m_IndexOffset = view.Offset;
    }

    BufferView Transient(const void* data, u64 size, u32 stride) override {
        BufferView v;
        u64 offset = 0;
        const GLBuffer* b = Allocate(m_Vertices, data, size, offset);
        if (b) { v.Buffer = m_Vertices.Buffers[m_Vertices.Current]; v.Offset = offset; v.Stride = stride; }
        return v;
    }

    void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex) override {
        if (!m_Bound || !vertexCount) return;
        glDrawArraysInstanced(Prim(), (GLint)firstVertex, (GLsizei)vertexCount, (GLsizei)instanceCount);
        ++FrameDrawCalls;
    }
    void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 baseVertex) override {
        if (!m_Bound || !indexCount) return;
        const void* offset = reinterpret_cast<const void*>((uintptr_t)(m_IndexOffset + firstIndex * 4ull));
        glDrawElementsInstancedBaseVertex(Prim(), (GLsizei)indexCount, GL_UNSIGNED_INT, offset, (GLsizei)instanceCount, baseVertex);
        ++FrameDrawCalls;
    }

    void Resolve(RenderTargetHandle src, RenderTargetHandle dst) override {
        const GLRenderTarget* s = m_Targets.Get(src.ID);
        const GLRenderTarget* d = m_Targets.Get(dst.ID);
        if (!s || !d) return;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s->FBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d->FBO);
        GLbitfield mask = 0;
        if (s->Color && d->Color) mask |= GL_COLOR_BUFFER_BIT;
        if (s->Depth && d->Depth) mask |= GL_DEPTH_BUFFER_BIT;
        glBlitFramebuffer(0, 0, (GLint)s->Width, (GLint)s->Height, 0, 0, (GLint)d->Width, (GLint)d->Height, mask, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    std::vector<u8> ReadBackbuffer(u32& width, u32& height) override {
        width = m_BackWidth;
        height = m_BackHeight;
        std::vector<u8> pixels(size_t(width) * height * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, m_Window.GetBackbufferFBO());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return pixels;
    }

private:
    GLenum Prim() const { return m_Bound && m_Bound->Desc.Prim == Topology::Lines ? GL_LINES : GL_TRIANGLES; }

    void ResetRing(Ring& r) {
        r.Current = 0;
        r.Offset = 0;
        // Orphan last frame's storage so the GPU can keep reading it.
        for (BufferHandle h : r.Buffers) {
            if (const GLBuffer* b = m_Buffers.Get(h.ID)) {
                glBindBuffer(r.Target == GL_UNIFORM_BUFFER ? GL_UNIFORM_BUFFER : GL_ARRAY_BUFFER, b->ID);
                glBufferData(r.Target == GL_UNIFORM_BUFFER ? GL_UNIFORM_BUFFER : GL_ARRAY_BUFFER, (GLsizeiptr)b->Size, nullptr, GL_STREAM_DRAW);
            }
        }
    }

    const GLBuffer* Allocate(Ring& r, const void* data, u64 size, u64& offset) {
        const u64 aligned = (r.Offset + r.Alignment - 1) / r.Alignment * r.Alignment;
        if (r.Buffers.empty() || aligned + size > m_Buffers.Get(r.Buffers[r.Current].ID)->Size) {
            // Next buffer (create one if needed, big enough for this request).
            if (!r.Buffers.empty()) ++r.Current;
            if (r.Current >= r.Buffers.size() || m_Buffers.Get(r.Buffers[r.Current].ID)->Size < size) {
                BufferDesc d;
                d.Size = std::max<u64>(r.Capacity, size);
                const BufferHandle h = CreateBuffer(d);
                if (r.Current >= r.Buffers.size()) r.Buffers.push_back(h);
                else { Destroy(r.Buffers[r.Current]); r.Buffers[r.Current] = h; }
            }
            r.Offset = 0;
            return Allocate(r, data, size, offset);
        }
        const GLBuffer* b = m_Buffers.Get(r.Buffers[r.Current].ID);
        const GLenum target = r.Target == GL_UNIFORM_BUFFER ? GL_UNIFORM_BUFFER : GL_ARRAY_BUFFER;
        glBindBuffer(target, b->ID);
        glBufferSubData(target, (GLintptr)aligned, (GLsizeiptr)size, data);
        offset = aligned;
        r.Offset = aligned + size;
        return b;
    }

    Window& m_Window;
    DeviceInfo m_Info;
    Pool<GLBuffer> m_Buffers;
    Pool<GLTexture> m_Textures;
    Pool<GLRenderTarget> m_Targets;
    Pool<GLPipeline> m_Pipelines;
    GLuint m_Samplers[(int)Sampler::Count] = {};
    GLuint m_EmptyVAO = 0;
    Ring m_Constants, m_Vertices;
    GLPipeline* m_Bound = nullptr;
    u64 m_IndexOffset = 0;
    u32 m_BackWidth = 1, m_BackHeight = 1;
};

} // namespace

Scope<Device> CreateGLDevice(Window& window, std::string* error) {
    auto device = CreateScope<GLDevice>(window);
    if (!device->Init(error)) return nullptr;
    return device;
}

} // namespace Lapis::RHI
