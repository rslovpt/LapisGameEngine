#pragma once

// Lapis RHI (render hardware interface)
// =====================================
// The renderer and the UI talk only to this interface; each graphics API
// implements it once:
//
//   OpenGL 4.5   RHI/OpenGL/GLDevice.cpp     (all platforms)
//   Direct3D 12  RHI/D3D12/D3D12Device.cpp   (Windows, default there)
//
// Model: an immediate-style command interface on top of handles.
//   * Resources (buffers, textures, render targets, pipelines) are created
//     up front and referenced by small handles (0 = none).
//   * Per-frame data (instances, debug lines, UI vertices, constants) goes
//     through Transient()/SetConstants(): copied into a per-frame ring, so
//     the caller never has to worry about the GPU still reading it.
//   * Shaders are looked up by name in the shader library
//     (Renderer/ShaderLibrary.h), which holds GLSL and HLSL for each.
//
// Conventions every backend follows (so shaders behave identically):
//   * Clip space is OpenGL's (z in -1..1, y up). The D3D shaders remap z in
//     the vertex stage; projection matrices are shared.
//   * Texture coordinates: images (Texture2D) are stored bottom row first,
//     render-target textures are sampled with v = 0 at the bottom on every
//     backend (HLSL flips v when sampling render targets).
//   * Constant buffers use only 16-byte members (Vec4 / Mat4) so std140 and
//     HLSL packing are identical. Slot 0 = per pass, slot 1 = per material.
//   * Texture slots t0..t7, each sampled with a fixed sampler kind (below).

#include <string>
#include <vector>

#include "Lapis/Core/Base.h"

namespace Lapis {
class Window;
}

namespace Lapis::RHI {

enum class API : u8 { OpenGL, D3D12, D3D11, Vulkan };
const char* APIName(API api);
bool APIFromName(const std::string& name, API& out);
// The best API for this platform (DirectX 12 on Windows, OpenGL elsewhere).
API DefaultAPI();
// "Auto" / "" = DefaultAPI(); otherwise a name accepted by APIFromName.
API ResolveAPIChoice(const std::string& choice);

enum class Format : u8 {
    None,
    RGBA8,        // UNORM
    RGBA8_SRGB,   // UNORM, sRGB -> linear when sampled
    R8,
    RGBA16F,
    R32F,
    D24,          // depth 24 (D24S8 on D3D)
    D32F,
};
bool IsDepthFormat(Format f);
u32  BytesPerPixel(Format f);

// Sampler kinds. HLSL uses register s<kind>; GLSL binds a sampler object.
enum class Sampler : u8 {
    LinearClamp = 0,
    LinearWrap = 1,
    PointClamp = 2,
    AnisoWrap = 3,       // trilinear + anisotropic (level from SetMaxAnisotropy)
    ShadowCompare = 4,   // depth comparison (LEQUAL), border = lit
    Count
};

enum class BufferType : u8 { Vertex, Index };   // index buffers are u32

struct BufferDesc {
    BufferType  Type = BufferType::Vertex;
    u64         Size = 0;
    const void* Data = nullptr;   // initial contents (required for immutable buffers)
    const char* Name = "";
};

struct TextureDesc {
    u32    Width = 1, Height = 1;
    Format Fmt = Format::RGBA8;
    u32    Samples = 1;
    bool   Mipmaps = false;        // generate a full chain from Data
    bool   RenderTarget = false;   // usable as color/depth attachment
    const void* Data = nullptr;    // tightly packed, bottom row first (Image order)
    const char* Name = "";
};

struct Handle {
    u32 ID = 0;
    explicit operator bool() const { return ID != 0; }
    bool operator==(const Handle& o) const { return ID == o.ID; }
    bool operator!=(const Handle& o) const { return ID != o.ID; }
};
struct BufferHandle : Handle {};
struct TextureHandle : Handle {};
struct RenderTargetHandle : Handle {};
struct PipelineHandle : Handle {};

struct BufferView {
    BufferHandle Buffer;
    u64 Offset = 0;
    u32 Stride = 0;           // vertex buffers
};

enum class AttribFormat : u8 { Float1, Float2, Float3, Float4 };
struct VertexAttribute {
    u8 Location = 0;          // GLSL location / HLSL semantic index (ATTRIB<n>)
    u8 Slot = 0;              // vertex buffer slot (0 = per vertex, 1 = per instance)
    AttribFormat Fmt = AttribFormat::Float3;
    u32 Offset = 0;
};

enum class Topology : u8 { Triangles, Lines };
enum class BlendMode : u8 { Opaque, Alpha, Additive };
enum class CullMode : u8 { None, Back };
enum class DepthMode : u8 {
    Off,              // no test, no write
    Less,             // test LESS, write
    LessEqualRead,    // test LEQUAL, no write (sky)
    Always,           // write without test (depth resolve)
};

struct PipelineDesc {
    std::string Shader;                       // name in the shader library
    std::vector<VertexAttribute> Attributes;
    u32  Strides[2] = {0, 0};
    bool Slot1PerInstance = true;
    Topology  Prim = Topology::Triangles;
    BlendMode Blend = BlendMode::Opaque;
    CullMode  Cull = CullMode::None;
    DepthMode Depth = DepthMode::Off;
    float DepthBias = 0.0f, SlopeBias = 0.0f;
    Format ColorFormat = Format::RGBA8;       // Format::None = depth-only
    Format DepthFormat = Format::None;
    u32    Samples = 1;
    const char* Name = "";
};

struct ClearValues {
    bool  Color = false, Depth = false;
    float Rgba[4] = {0, 0, 0, 1};
    float DepthValue = 1.0f;
};

struct DeviceInfo {
    API         Api = API::OpenGL;
    std::string GPUName, Vendor, Version;
    u32   MaxSamples = 1;
    float MaxAnisotropy = 1.0f;
};

class Device : NonCopyable {
public:
    // Creates a device for `api` on `window`. Returns nullptr (and fills
    // *error) if the API is unavailable or initialisation fails, so the
    // caller can fall back to another API.
    static Scope<Device> Create(API api, Window& window, std::string* error = nullptr);
    static bool IsCompiledIn(API api);

    virtual ~Device() = default;
    virtual const DeviceInfo& GetInfo() const = 0;

    // ---- resources ----
    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual RenderTargetHandle CreateRenderTarget(TextureHandle color, TextureHandle depth, const char* name = "") = 0;
    virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;   // 0 if the shader fails
    virtual void Destroy(BufferHandle h) = 0;
    virtual void Destroy(TextureHandle h) = 0;
    virtual void Destroy(RenderTargetHandle h) = 0;
    virtual void Destroy(PipelineHandle h) = 0;
    virtual const TextureDesc* GetTextureDesc(TextureHandle h) const = 0;

    // ---- frame ----
    // BeginFrame resizes the swap chain if needed. EndFrame submits + presents.
    virtual void BeginFrame(u32 width, u32 height) = 0;
    virtual void EndFrame() = 0;
    virtual void SetVSync(bool enabled) = 0;
    virtual void SetMaxAnisotropy(float level) = 0;

    // ---- commands ----
    // rt = 0 renders to the window. The viewport covers the whole target.
    virtual void BeginPass(RenderTargetHandle rt, const ClearValues& clear = {}) = 0;
    virtual void EndPass() = 0;
    virtual void SetPipeline(PipelineHandle p) = 0;
    virtual void SetConstants(u32 slot, const void* data, u32 size) = 0;
    virtual void SetTexture(u32 slot, TextureHandle t, Sampler s) = 0;
    virtual void SetVertexBuffer(u32 slot, const BufferView& view) = 0;
    virtual void SetIndexBuffer(const BufferView& view) = 0;
    // Copies data into this frame's upload ring; valid until EndFrame.
    virtual BufferView Transient(const void* data, u64 size, u32 stride = 0) = 0;
    virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0) = 0;
    virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 baseVertex = 0) = 0;

    // Resolves (or copies) color and depth of src into dst (same size; dst single-sampled).
    virtual void Resolve(RenderTargetHandle src, RenderTargetHandle dst) = 0;
    // Screenshots: the next EndFrame copies the finished frame (just before
    // present, while it is still valid) and GetCapturedFrame returns it as
    // RGBA8, bottom row first. Empty if nothing was captured.
    virtual void CaptureNextFrame() = 0;
    virtual std::vector<u8> GetCapturedFrame(u32& width, u32& height) = 0;

    // Counters for the stats overlay.
    u32 FrameDrawCalls = 0;
};

// The device used by resources created without an explicit device
// (Texture2D, Mesh, ...). Set by the application.
Device* Current();
void SetCurrent(Device* device);

} // namespace Lapis::RHI
