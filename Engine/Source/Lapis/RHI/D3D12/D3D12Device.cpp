// Direct3D 12 implementation of the Lapis RHI (Windows).
//
// Design
//  * One graphics queue, 2 frames in flight, 3-buffer flip-model swap chain
//    (tearing allowed when VSync is off).
//  * One root signature for every shader:
//      [0] root CBV b0   [1] root CBV b1
//      [2] table t0..t7  (SRVs copied per draw into a shader-visible ring)
//      [3] table s0..s4  (the RHI sampler kinds, rebuilt if anisotropy changes)
//  * Per-frame upload ring for constants and transient vertex data.
//  * Static buffers / textures are uploaded once through a temporary upload
//    buffer on an immediate command list (load time only).
//  * Resource states are tracked per texture; destroyed resources are kept
//    alive until the GPU has finished the frames that may use them.
//  * D3D12, DXGI and the HLSL compiler are loaded at runtime, so a machine
//    without them simply reports "DirectX 12 unavailable" and the engine
//    falls back to OpenGL.

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Window.h"
#include "Lapis/RHI/RHI.h"
#include "Lapis/Renderer/ShaderLibrary.h"

namespace Lapis::RHI {

namespace {

// ---- minimal COM smart pointer ------------------------------------------------
template <typename T>
class Com {
public:
    Com() = default;
    Com(const Com& o) : m_P(o.m_P) { if (m_P) m_P->AddRef(); }
    Com(Com&& o) noexcept : m_P(o.m_P) { o.m_P = nullptr; }
    ~Com() { Reset(); }
    Com& operator=(const Com& o) { if (this != &o) { Reset(); m_P = o.m_P; if (m_P) m_P->AddRef(); } return *this; }
    Com& operator=(Com&& o) noexcept { if (this != &o) { Reset(); m_P = o.m_P; o.m_P = nullptr; } return *this; }
    void Reset() { if (m_P) { m_P->Release(); m_P = nullptr; } }
    T* Get() const { return m_P; }
    T* operator->() const { return m_P; }
    T** Put() { Reset(); return &m_P; }
    void** PutVoid() { Reset(); return reinterpret_cast<void**>(&m_P); }
    explicit operator bool() const { return m_P != nullptr; }
    template <typename U> Com<U> As() const {
        Com<U> out;
        if (m_P) m_P->QueryInterface(__uuidof(U), out.PutVoid());
        return out;
    }
private:
    T* m_P = nullptr;
};

std::string HrString(HRESULT hr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)hr);
    return buf;
}

std::string Narrow(const wchar_t* w) {
    std::string out;
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n > 1) { out.resize((size_t)n - 1); WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr); }
    return out;
}

// ---- runtime-loaded entry points ------------------------------------------------
using PFN_CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using PFN_GetDebugInterface = HRESULT(WINAPI*)(REFIID, void**);
using PFN_SerializeRootSignature = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
using PFN_CreateDXGIFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
using PFN_D3DCompile = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                        ID3DBlob**, ID3DBlob**);

struct Libraries {
    HMODULE D3D12 = nullptr, DXGI = nullptr, Compiler = nullptr;
    PFN_CreateDevice CreateDevice = nullptr;
    PFN_GetDebugInterface GetDebugInterface = nullptr;
    PFN_SerializeRootSignature SerializeRootSignature = nullptr;
    PFN_CreateDXGIFactory2 CreateFactory = nullptr;
    PFN_D3DCompile Compile = nullptr;

    bool Load(std::string& error) {
        D3D12 = LoadLibraryW(L"d3d12.dll");
        DXGI = LoadLibraryW(L"dxgi.dll");
        Compiler = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!D3D12) { error = "d3d12.dll not found (Windows 10 or newer required)"; return false; }
        if (!DXGI) { error = "dxgi.dll not found"; return false; }
        if (!Compiler) { error = "d3dcompiler_47.dll not found (HLSL compiler)"; return false; }
        CreateDevice = reinterpret_cast<PFN_CreateDevice>(reinterpret_cast<void*>(GetProcAddress(D3D12, "D3D12CreateDevice")));
        GetDebugInterface = reinterpret_cast<PFN_GetDebugInterface>(reinterpret_cast<void*>(GetProcAddress(D3D12, "D3D12GetDebugInterface")));
        SerializeRootSignature = reinterpret_cast<PFN_SerializeRootSignature>(reinterpret_cast<void*>(GetProcAddress(D3D12, "D3D12SerializeRootSignature")));
        CreateFactory = reinterpret_cast<PFN_CreateDXGIFactory2>(reinterpret_cast<void*>(GetProcAddress(DXGI, "CreateDXGIFactory2")));
        Compile = reinterpret_cast<PFN_D3DCompile>(reinterpret_cast<void*>(GetProcAddress(Compiler, "D3DCompile")));
        if (!CreateDevice || !SerializeRootSignature || !CreateFactory || !Compile) { error = "DirectX 12 entry points missing"; return false; }
        return true;
    }
};

// ---- formats -------------------------------------------------------------------
struct DXFormats { DXGI_FORMAT Resource, View, Target; };   // Target = RTV or DSV format
DXFormats ToDX(Format f) {
    switch (f) {
        case Format::RGBA8: return {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM};
        case Format::RGBA8_SRGB: return {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
        case Format::R8: return {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM};
        case Format::RGBA16F: return {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
        case Format::R32F: return {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT};
        case Format::D24: return {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT};
        case Format::D32F: return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT};
        default: return {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN};
    }
}

constexpr u32 FramesInFlight = 2;
constexpr u32 SwapBuffers = 3;
constexpr u32 TextureSlots = 8;
constexpr u32 SamplerCount = (u32)Sampler::Count;

// ---- simple CPU descriptor allocator ------------------------------------------------
struct DescriptorPool {
    Com<ID3D12DescriptorHeap> Heap;
    u32 Increment = 0, Capacity = 0;
    std::vector<u32> Free;
    u32 Next = 0;

    bool Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, u32 capacity, bool shaderVisible = false) {
        D3D12_DESCRIPTOR_HEAP_DESC d{};
        d.Type = type;
        d.NumDescriptors = capacity;
        d.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&d, __uuidof(ID3D12DescriptorHeap), Heap.PutVoid()))) return false;
        Increment = device->GetDescriptorHandleIncrementSize(type);
        Capacity = capacity;
        return true;
    }
    u32 Allocate() {
        if (!Free.empty()) { const u32 i = Free.back(); Free.pop_back(); return i; }
        if (Next >= Capacity) { LAPIS_ERROR("D3D12: descriptor heap full ({} descriptors)", Capacity); return 0; }
        return Next++;
    }
    void Release(u32 i) { Free.push_back(i); }
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(u32 i) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = Heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)i * Increment;
        return h;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(u32 i) const {
        D3D12_GPU_DESCRIPTOR_HANDLE h = Heap->GetGPUDescriptorHandleForHeapStart();
        h.ptr += (UINT64)i * Increment;
        return h;
    }
};

// ---- resources -------------------------------------------------------------------
struct DXBuffer {
    Com<ID3D12Resource> Resource;
    u64 Size = 0;
    D3D12_GPU_VIRTUAL_ADDRESS Address = 0;
    void* Mapped = nullptr;   // upload-heap buffers only
};

struct DXTexture {
    Com<ID3D12Resource> Resource;
    TextureDesc Desc;
    D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
    u32 SRV = UINT32_MAX;     // index in the CPU SRV pool
    u32 RTV = UINT32_MAX;     // index in the RTV pool (color render targets)
    u32 DSV = UINT32_MAX;     // index in the DSV pool (depth render targets)
};

struct DXRenderTarget { TextureHandle Color, Depth; u32 Width = 0, Height = 0; };

struct DXPipeline {
    Com<ID3D12PipelineState> PSO;
    PipelineDesc Desc;
};

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
};

// Per-frame upload memory (constants, transient vertices). Grows by adding pages.
struct UploadRing {
    struct Page { Com<ID3D12Resource> Resource; u8* CPU = nullptr; D3D12_GPU_VIRTUAL_ADDRESS GPU = 0; u64 Size = 0; };
    std::vector<Page> Pages;
    size_t Current = 0;
    u64 Offset = 0;
};

class D3D12Device final : public Device {
public:
    explicit D3D12Device(Window& window) : m_Window(window) {}
    ~D3D12Device() override { Shutdown(); }

    bool Init(std::string* error) {
        std::string err;
        if (!m_Lib.Load(err)) { if (error) *error = err; return false; }
        m_Hwnd = static_cast<HWND>(m_Window.GetNativeHandle());
        if (!m_Hwnd) { if (error) *error = "no native window"; return false; }

#if LAPIS_DEBUG
        const bool debug = true;
#else
        const bool debug = GetEnvironmentVariableA("LAPIS_D3D12_DEBUG", nullptr, 0) > 0;
#endif
        if (debug && m_Lib.GetDebugInterface) {
            Com<ID3D12Debug> dbg;
            if (SUCCEEDED(m_Lib.GetDebugInterface(__uuidof(ID3D12Debug), dbg.PutVoid()))) {
                dbg->EnableDebugLayer();
                LAPIS_INFO("D3D12: debug layer enabled");
            }
        }

        HRESULT hr = m_Lib.CreateFactory(debug ? DXGI_CREATE_FACTORY_DEBUG : 0, __uuidof(IDXGIFactory4), m_Factory.PutVoid());
        if (FAILED(hr)) hr = m_Lib.CreateFactory(0, __uuidof(IDXGIFactory4), m_Factory.PutVoid());
        if (FAILED(hr)) { if (error) *error = "CreateDXGIFactory2 failed " + HrString(hr); return false; }

        // Adapter: prefer the high-performance GPU, never a software adapter.
        Com<IDXGIAdapter1> adapter;
        if (auto f6 = m_Factory.As<IDXGIFactory6>()) {
            for (UINT i = 0; f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), adapter.PutVoid()) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 d;
                adapter->GetDesc1(&d);
                if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                    SUCCEEDED(m_Lib.CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), m_Device.PutVoid())))
                    break;
                adapter.Reset();
            }
        }
        if (!m_Device) {
            for (UINT i = 0; m_Factory->EnumAdapters1(i, adapter.Put()) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 d;
                adapter->GetDesc1(&d);
                if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                if (SUCCEEDED(m_Lib.CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), m_Device.PutVoid()))) break;
            }
        }
        if (!m_Device) { if (error) *error = "no GPU with DirectX 12 support (feature level 11_0)"; return false; }
        if (debug) m_InfoQueue = m_Device.As<ID3D12InfoQueue>();   // validation messages -> engine log

        DXGI_ADAPTER_DESC1 ad;
        adapter->GetDesc1(&ad);
        m_Info.Api = API::D3D12;
        m_Info.GPUName = Narrow(ad.Description);
        switch (ad.VendorId) {
            case 0x10DE: m_Info.Vendor = "NVIDIA"; break;
            case 0x1002: case 0x1022: m_Info.Vendor = "AMD"; break;
            case 0x8086: m_Info.Vendor = "Intel"; break;
            default: m_Info.Vendor = "Unknown vendor"; break;
        }
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
        fl.NumFeatureLevels = (UINT)(sizeof(levels) / sizeof(levels[0]));
        fl.pFeatureLevelsRequested = levels;
        m_Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl));
        const unsigned lvl = (unsigned)fl.MaxSupportedFeatureLevel;
        m_Info.Version = "Direct3D 12, feature level " + std::to_string(lvl >> 12) + "_" + std::to_string((lvl >> 8) & 0xF) +
                         ", " + std::to_string(ad.DedicatedVideoMemory / (1024 * 1024)) + " MB VRAM";
        m_Info.MaxAnisotropy = 16.0f;
        m_Info.MaxSamples = 1;
        for (u32 s : {8u, 4u, 2u}) {
            bool ok = true;
            for (DXGI_FORMAT f : {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT}) {
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{};
                q.Format = f;
                q.SampleCount = s;
                if (FAILED(m_Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) || q.NumQualityLevels == 0) ok = false;
            }
            if (ok) { m_Info.MaxSamples = s; break; }
        }

        // Queue, fence, command lists.
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(m_Device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), m_Queue.PutVoid()))) { if (error) *error = "CreateCommandQueue failed"; return false; }
        if (FAILED(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), m_Fence.PutVoid()))) { if (error) *error = "CreateFence failed"; return false; }
        m_FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        for (u32 i = 0; i < FramesInFlight; ++i)
            if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), m_Frames[i].Allocator.PutVoid()))) {
                if (error) *error = "CreateCommandAllocator failed";
                return false;
            }
        if (FAILED(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Frames[0].Allocator.Get(), nullptr,
                                               __uuidof(ID3D12GraphicsCommandList), m_List.PutVoid()))) {
            if (error) *error = "CreateCommandList failed";
            return false;
        }
        m_List->Close();

        // Descriptor heaps.
        if (!m_RTVs.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256) || !m_DSVs.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64) ||
            !m_SRVs.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096) ||
            !m_GPUViews.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, true) ||
            !m_GPUSamplers.Init(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SamplerCount * 64, true)) {
            if (error) *error = "CreateDescriptorHeap failed";
            return false;
        }
        m_NullSRV = m_SRVs.Allocate();
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC n{};
            n.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            n.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            n.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            n.Texture2D.MipLevels = 1;
            m_Device->CreateShaderResourceView(nullptr, &n, m_SRVs.Cpu(m_NullSRV));
        }
        WriteSamplers(8.0f);

        if (!CreateRootSignature(err)) { if (error) *error = err; return false; }

        // Swap chain.
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = std::max(m_Window.GetWidth(), 1u);
        sd.Height = std::max(m_Window.GetHeight(), 1u);
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = SwapBuffers;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (auto f5 = m_Factory.As<IDXGIFactory5>()) {
            BOOL tearing = FALSE;
            if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) m_Tearing = true;
        }
        if (m_Tearing) sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        Com<IDXGISwapChain1> sc1;
        hr = m_Factory->CreateSwapChainForHwnd(m_Queue.Get(), m_Hwnd, &sd, nullptr, nullptr, sc1.Put());
        if (FAILED(hr)) { if (error) *error = "CreateSwapChainForHwnd failed " + HrString(hr); return false; }
        m_Factory->MakeWindowAssociation(m_Hwnd, DXGI_MWA_NO_ALT_ENTER);
        m_SwapChain = sc1.As<IDXGISwapChain3>();
        if (!m_SwapChain) { if (error) *error = "IDXGISwapChain3 unavailable"; return false; }
        m_BackWidth = sd.Width;
        m_BackHeight = sd.Height;
        CreateBackbufferViews();

        // Compile every shader now: a failure here falls back to OpenGL
        // instead of breaking later in the middle of a frame.
        int count = 0;
        const ShaderLibrary::Source* all = ShaderLibrary::All(count);
        for (int i = 0; i < count; ++i) {
            if (!all[i].HLSL) continue;
            if (!CompileShader(all[i], err)) { if (error) *error = err; return false; }
        }
        m_Initialized = true;
        return true;
    }

    void Shutdown() {
        if (!m_Device) return;
        WaitIdle();
        m_Garbage.clear();
        m_Pipelines = {};
        m_Textures = {};
        m_Buffers = {};
        m_Backbuffers[0].Reset(); m_Backbuffers[1].Reset(); m_Backbuffers[2].Reset();
        m_SwapChain.Reset();
        if (m_FenceEvent) { CloseHandle(m_FenceEvent); m_FenceEvent = nullptr; }
        m_Device.Reset();
    }

    const DeviceInfo& GetInfo() const override { return m_Info; }

    // =========================================================================
    // Resources
    // =========================================================================
    BufferHandle CreateBuffer(const BufferDesc& desc) override {
        DXBuffer b;
        b.Size = std::max<u64>(desc.Size, 16);
        if (desc.Data) {
            // Immutable: default heap, filled once through an upload buffer.
            b.Resource = CreateCommitted(D3D12_HEAP_TYPE_DEFAULT, BufferResourceDesc(b.Size), D3D12_RESOURCE_STATE_COPY_DEST);
            if (!b.Resource) return {};
            Com<ID3D12Resource> upload = CreateCommitted(D3D12_HEAP_TYPE_UPLOAD, BufferResourceDesc(b.Size), D3D12_RESOURCE_STATE_GENERIC_READ);
            if (!upload) return {};
            void* p = nullptr;
            upload->Map(0, nullptr, &p);
            std::memcpy(p, desc.Data, (size_t)desc.Size);
            upload->Unmap(0, nullptr);
            ImmediateBegin();
            m_Imm->CopyBufferRegion(b.Resource.Get(), 0, upload.Get(), 0, desc.Size);
            D3D12_RESOURCE_BARRIER barrier = Transition(b.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                        desc.Type == BufferType::Index ? D3D12_RESOURCE_STATE_INDEX_BUFFER
                                                                                       : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            m_Imm->ResourceBarrier(1, &barrier);
            ImmediateEnd();
        } else {
            // Dynamic: CPU-writable upload heap, persistently mapped.
            b.Resource = CreateCommitted(D3D12_HEAP_TYPE_UPLOAD, BufferResourceDesc(b.Size), D3D12_RESOURCE_STATE_GENERIC_READ);
            if (!b.Resource) return {};
            b.Resource->Map(0, nullptr, &b.Mapped);
        }
        b.Address = b.Resource->GetGPUVirtualAddress();
        BufferHandle h;
        h.ID = m_Buffers.Add(std::move(b));
        return h;
    }

    TextureHandle CreateTexture(const TextureDesc& desc) override {
        DXTexture t;
        t.Desc = desc;
        t.Desc.Data = nullptr;
        const DXFormats f = ToDX(desc.Fmt);
        const bool depth = IsDepthFormat(desc.Fmt);
        const u32 w = std::max(desc.Width, 1u), h = std::max(desc.Height, 1u);
        u32 mips = 1;
        if (desc.Mipmaps && desc.Data && desc.Samples <= 1) while ((std::max(w, h) >> mips) > 0) ++mips;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w;
        rd.Height = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = (UINT16)mips;
        rd.Format = f.Resource;
        rd.SampleDesc.Count = std::max(desc.Samples, 1u);
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        D3D12_CLEAR_VALUE clear{};
        const D3D12_CLEAR_VALUE* clearPtr = nullptr;
        if (desc.RenderTarget) {
            rd.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            clear.Format = f.Target;
            if (depth) clear.DepthStencil.Depth = 1.0f;
            else { clear.Color[3] = 1.0f; }
            clearPtr = &clear;
        }
        t.State = desc.Data ? D3D12_RESOURCE_STATE_COPY_DEST : (desc.RenderTarget ? (depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET)
                                                                                   : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        HRESULT hr = m_Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, t.State, clearPtr, __uuidof(ID3D12Resource), t.Resource.PutVoid());
        if (FAILED(hr)) { LAPIS_ERROR("D3D12: texture '{}' {}x{} failed {}", desc.Name, w, h, HrString(hr)); return {}; }

        if (desc.Data) UploadTexture(t, desc.Data, rd, w, h, mips);

        // Views.
        t.SRV = m_SRVs.Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = f.View;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (rd.SampleDesc.Count > 1) sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        else { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = mips; }
        m_Device->CreateShaderResourceView(t.Resource.Get(), &sv, m_SRVs.Cpu(t.SRV));
        if (desc.RenderTarget && depth) {
            t.DSV = m_DSVs.Allocate();
            D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
            dv.Format = f.Target;
            dv.ViewDimension = rd.SampleDesc.Count > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
            m_Device->CreateDepthStencilView(t.Resource.Get(), &dv, m_DSVs.Cpu(t.DSV));
        } else if (desc.RenderTarget) {
            t.RTV = m_RTVs.Allocate();
            D3D12_RENDER_TARGET_VIEW_DESC rv{};
            rv.Format = f.Target;
            rv.ViewDimension = rd.SampleDesc.Count > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
            m_Device->CreateRenderTargetView(t.Resource.Get(), &rv, m_RTVs.Cpu(t.RTV));
        }
        TextureHandle handle;
        handle.ID = m_Textures.Add(std::move(t));
        return handle;
    }

    RenderTargetHandle CreateRenderTarget(TextureHandle color, TextureHandle depth, const char* name) override {
        DXRenderTarget rt;
        rt.Color = color;
        rt.Depth = depth;
        const DXTexture* any = m_Textures.Get(color.ID) ? m_Textures.Get(color.ID) : m_Textures.Get(depth.ID);
        if (!any) { LAPIS_ERROR("D3D12: render target '{}' has no attachments", name); return {}; }
        rt.Width = any->Desc.Width;
        rt.Height = any->Desc.Height;
        RenderTargetHandle h;
        h.ID = m_Targets.Add(rt);
        return h;
    }

    PipelineHandle CreatePipeline(const PipelineDesc& desc) override {
        const ShaderLibrary::Source* src = ShaderLibrary::Find(desc.Shader.c_str());
        if (!src || !src->HLSL) { LAPIS_ERROR("D3D12: shader '{}' has no HLSL version", desc.Shader); return {}; }
        std::string err;
        const CompiledShader* cs = CompileShader(*src, err);
        if (!cs) return {};

        std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
        static const DXGI_FORMAT attribFormats[] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
        for (const VertexAttribute& a : desc.Attributes) {
            D3D12_INPUT_ELEMENT_DESC e{};
            e.SemanticName = "ATTRIB";
            e.SemanticIndex = a.Location;
            e.Format = attribFormats[(int)a.Fmt];
            e.InputSlot = a.Slot;
            e.AlignedByteOffset = a.Offset;
            const bool instanced = a.Slot == 1 && desc.Slot1PerInstance;
            e.InputSlotClass = instanced ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            e.InstanceDataStepRate = instanced ? 1 : 0;
            elements.push_back(e);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = m_RootSignature.Get();
        pd.VS = {cs->VS->GetBufferPointer(), cs->VS->GetBufferSize()};
        pd.PS = {cs->PS->GetBufferPointer(), cs->PS->GetBufferSize()};
        pd.InputLayout = {elements.empty() ? nullptr : elements.data(), (UINT)elements.size()};
        pd.PrimitiveTopologyType = desc.Prim == Topology::Lines ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.SampleMask = UINT_MAX;
        pd.SampleDesc.Count = std::max(desc.Samples, 1u);

        D3D12_RASTERIZER_DESC& r = pd.RasterizerState;
        r.FillMode = D3D12_FILL_MODE_SOLID;
        r.CullMode = desc.Cull == CullMode::Back ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
        r.FrontCounterClockwise = TRUE;    // same winding as OpenGL (CCW front)
        r.DepthBias = (INT)desc.DepthBias;
        r.SlopeScaledDepthBias = desc.SlopeBias;
        r.DepthBiasClamp = 0.0f;
        r.DepthClipEnable = TRUE;
        r.MultisampleEnable = desc.Samples > 1 ? TRUE : FALSE;
        r.AntialiasedLineEnable = FALSE;

        D3D12_RENDER_TARGET_BLEND_DESC& b = pd.BlendState.RenderTarget[0];
        b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        b.BlendOp = b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        b.LogicOp = D3D12_LOGIC_OP_NOOP;
        switch (desc.Blend) {
            case BlendMode::Opaque: b.BlendEnable = FALSE; b.SrcBlend = b.SrcBlendAlpha = D3D12_BLEND_ONE; b.DestBlend = b.DestBlendAlpha = D3D12_BLEND_ZERO; break;
            case BlendMode::Alpha:
                b.BlendEnable = TRUE;
                b.SrcBlend = D3D12_BLEND_SRC_ALPHA; b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                b.SrcBlendAlpha = D3D12_BLEND_ONE;  b.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                break;
            case BlendMode::Additive:
                b.BlendEnable = TRUE;
                b.SrcBlend = b.SrcBlendAlpha = D3D12_BLEND_ONE;
                b.DestBlend = b.DestBlendAlpha = D3D12_BLEND_ONE;
                break;
        }

        D3D12_DEPTH_STENCIL_DESC& d = pd.DepthStencilState;
        d.StencilEnable = FALSE;
        switch (desc.Depth) {
            case DepthMode::Off: d.DepthEnable = FALSE; d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; d.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS; break;
            case DepthMode::Less: d.DepthEnable = TRUE; d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; d.DepthFunc = D3D12_COMPARISON_FUNC_LESS; break;
            case DepthMode::LessEqualRead: d.DepthEnable = TRUE; d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; d.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; break;
            case DepthMode::Always: d.DepthEnable = TRUE; d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; d.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS; break;
        }

        if (desc.ColorFormat != Format::None) {
            pd.NumRenderTargets = 1;
            pd.RTVFormats[0] = ToDX(desc.ColorFormat).Target;
        }
        pd.DSVFormat = desc.DepthFormat != Format::None ? ToDX(desc.DepthFormat).Target : DXGI_FORMAT_UNKNOWN;

        DXPipeline p;
        p.Desc = desc;
        const HRESULT hr = m_Device->CreateGraphicsPipelineState(&pd, __uuidof(ID3D12PipelineState), p.PSO.PutVoid());
        if (FAILED(hr)) { FlushDebugMessages(); LAPIS_ERROR("D3D12: pipeline '{}' failed {}", desc.Shader, HrString(hr)); return {}; }
        PipelineHandle h;
        h.ID = m_Pipelines.Add(std::move(p));
        return h;
    }

    void Destroy(BufferHandle h) override {
        if (DXBuffer* b = m_Buffers.Get(h.ID)) { Defer(b->Resource); m_Buffers.Remove(h.ID); }
    }
    void Destroy(TextureHandle h) override {
        if (DXTexture* t = m_Textures.Get(h.ID)) {
            Defer(t->Resource);
            if (t->SRV != UINT32_MAX) m_SRVs.Release(t->SRV);   // descriptors are copied per draw, safe to reuse
            if (t->RTV != UINT32_MAX) m_RTVs.Release(t->RTV);
            if (t->DSV != UINT32_MAX) m_DSVs.Release(t->DSV);
            m_Textures.Remove(h.ID);
        }
    }
    void Destroy(RenderTargetHandle h) override { m_Targets.Remove(h.ID); }
    void Destroy(PipelineHandle h) override {
        if (DXPipeline* p = m_Pipelines.Get(h.ID)) {
            DeferPSO(p->PSO);
            if (m_Bound == p) m_Bound = nullptr;
            m_Pipelines.Remove(h.ID);
        }
    }
    const TextureDesc* GetTextureDesc(TextureHandle h) const override { const DXTexture* t = m_Textures.Get(h.ID); return t ? &t->Desc : nullptr; }

    // =========================================================================
    // Frame
    // =========================================================================
    void BeginFrame(u32 width, u32 height) override {
        if (!m_Initialized || m_Lost) return;
        width = std::max(width, 1u);
        height = std::max(height, 1u);
        if (width != m_BackWidth || height != m_BackHeight) Resize(width, height);

        Frame& f = m_Frames[m_FrameIndex];
        WaitForFence(f.FenceValue);
        CollectGarbage();
        f.Ring.Current = 0;
        f.Ring.Offset = 0;
        m_ViewCursor = m_FrameIndex * (m_GPUViews.Capacity / FramesInFlight);
        m_ViewEnd = m_ViewCursor + m_GPUViews.Capacity / FramesInFlight;

        f.Allocator->Reset();
        m_List->Reset(f.Allocator.Get(), nullptr);
        ID3D12DescriptorHeap* heaps[] = {m_GPUViews.Heap.Get(), m_GPUSamplers.Heap.Get()};
        m_List->SetDescriptorHeaps(2, heaps);
        m_List->SetGraphicsRootSignature(m_RootSignature.Get());
        m_List->SetGraphicsRootDescriptorTable(3, m_GPUSamplers.Gpu(m_SamplerSet * SamplerCount));
        m_BackIndex = m_SwapChain->GetCurrentBackBufferIndex();
        m_BackState = D3D12_RESOURCE_STATE_PRESENT;
        m_InFrame = true;
        m_Bound = nullptr;
        FrameDrawCalls = 0;
    }

    void EndFrame() override {
        if (!m_InFrame) return;
        m_InFrame = false;
        ID3D12Resource* back = m_Backbuffers[m_BackIndex].Get();

        if (m_CaptureRequested) {
            m_CaptureRequested = false;
            CaptureBackbuffer(back);
        }
        if (m_BackState != D3D12_RESOURCE_STATE_PRESENT) {
            D3D12_RESOURCE_BARRIER b = Transition(back, m_BackState, D3D12_RESOURCE_STATE_PRESENT);
            m_List->ResourceBarrier(1, &b);
            m_BackState = D3D12_RESOURCE_STATE_PRESENT;
        }
        m_List->Close();
        ID3D12CommandList* lists[] = {m_List.Get()};
        m_Queue->ExecuteCommandLists(1, lists);

        const bool tear = !m_VSync && m_Tearing;   // we only use borderless fullscreen, where tearing is allowed
        const HRESULT hr = m_SwapChain->Present(m_VSync ? 1 : 0, tear ? DXGI_PRESENT_ALLOW_TEARING : 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            LAPIS_FATAL("D3D12: GPU device removed (reason {}) - the driver crashed or was updated", HrString(m_Device->GetDeviceRemovedReason()));
            m_Lost = true;
        }
        Frame& f = m_Frames[m_FrameIndex];
        f.FenceValue = ++m_FenceCounter;
        m_Queue->Signal(m_Fence.Get(), f.FenceValue);
        m_FrameIndex = (m_FrameIndex + 1) % FramesInFlight;
        FlushDebugMessages();
    }

    // Debug layer messages (LAPIS_D3D12_DEBUG=1 or debug builds) go to the log.
    void FlushDebugMessages() {
        if (!m_InfoQueue) return;
        const UINT64 count = m_InfoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < count && m_DebugMessagesLogged < 200; ++i) {
            SIZE_T size = 0;
            m_InfoQueue->GetMessage(i, nullptr, &size);
            std::vector<char> buffer(size);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (FAILED(m_InfoQueue->GetMessage(i, msg, &size))) continue;
            const std::string text(msg->pDescription, msg->DescriptionByteLength ? msg->DescriptionByteLength - 1 : 0);
            if (msg->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) { LAPIS_ERROR("D3D12 debug: {}", text); ++m_DebugMessagesLogged; }
            else if (msg->Severity == D3D12_MESSAGE_SEVERITY_WARNING) { LAPIS_WARN("D3D12 debug: {}", text); ++m_DebugMessagesLogged; }
        }
        m_InfoQueue->ClearStoredMessages();
    }

    void SetVSync(bool enabled) override { m_VSync = enabled; }

    void SetMaxAnisotropy(float level) override {
        const float a = std::max(1.0f, std::min(level, 16.0f));
        if (a == m_Anisotropy) return;
        WriteSamplers(a);
        if (m_InFrame) m_List->SetGraphicsRootDescriptorTable(3, m_GPUSamplers.Gpu(m_SamplerSet * SamplerCount));
    }

    // =========================================================================
    // Commands
    // =========================================================================
    void BeginPass(RenderTargetHandle rt, const ClearValues& clear) override {
        if (!m_InFrame) return;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{}, dsv{};
        bool hasColor = false, hasDepth = false;
        u32 w = m_BackWidth, h = m_BackHeight;
        if (const DXRenderTarget* t = m_Targets.Get(rt.ID)) {
            w = t->Width;
            h = t->Height;
            if (DXTexture* c = m_Textures.Get(t->Color.ID)) { SetState(*c, D3D12_RESOURCE_STATE_RENDER_TARGET); rtv = m_RTVs.Cpu(c->RTV); hasColor = true; }
            if (DXTexture* d = m_Textures.Get(t->Depth.ID)) { SetState(*d, D3D12_RESOURCE_STATE_DEPTH_WRITE); dsv = m_DSVs.Cpu(d->DSV); hasDepth = true; }
        } else {
            if (m_BackState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                D3D12_RESOURCE_BARRIER b = Transition(m_Backbuffers[m_BackIndex].Get(), m_BackState, D3D12_RESOURCE_STATE_RENDER_TARGET);
                m_List->ResourceBarrier(1, &b);
                m_BackState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
            rtv = m_RTVs.Cpu(m_BackRTV[m_BackIndex]);
            hasColor = true;
        }
        m_List->OMSetRenderTargets(hasColor ? 1 : 0, hasColor ? &rtv : nullptr, FALSE, hasDepth ? &dsv : nullptr);
        D3D12_VIEWPORT vp{0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
        D3D12_RECT sc{0, 0, (LONG)w, (LONG)h};
        m_List->RSSetViewports(1, &vp);
        m_List->RSSetScissorRects(1, &sc);
        if (clear.Color && hasColor) m_List->ClearRenderTargetView(rtv, clear.Rgba, 0, nullptr);
        if (clear.Depth && hasDepth) m_List->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, clear.DepthValue, 0, 0, nullptr);
        m_Bound = nullptr;
        for (u32 i = 0; i < TextureSlots; ++i) m_Slots[i] = {};
        m_SlotsDirty = true;
    }

    void EndPass() override {}

    void SetPipeline(PipelineHandle h) override {
        DXPipeline* p = m_Pipelines.Get(h.ID);
        m_Bound = p;
        if (!p || !m_InFrame) return;
        m_List->SetPipelineState(p->PSO.Get());
        m_List->IASetPrimitiveTopology(p->Desc.Prim == Topology::Lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void SetConstants(u32 slot, const void* data, u32 size) override {
        if (!m_InFrame || slot > 1) return;
        D3D12_GPU_VIRTUAL_ADDRESS va = 0;
        void* dst = Upload(size, 256, va);
        if (!dst) return;
        std::memcpy(dst, data, size);
        m_List->SetGraphicsRootConstantBufferView(slot, va);
    }

    void SetTexture(u32 slot, TextureHandle h, Sampler) override {
        if (slot >= TextureSlots) return;
        m_Slots[slot] = h;
        m_SlotsDirty = true;
    }

    void SetVertexBuffer(u32 slot, const BufferView& view) override {
        if (!m_InFrame || slot > 1) return;
        D3D12_VERTEX_BUFFER_VIEW v{};
        if (const DXBuffer* b = m_Buffers.Get(view.Buffer.ID)) {
            v.BufferLocation = b->Address + view.Offset;
            v.SizeInBytes = (UINT)(b->Size - view.Offset);
        } else if (view.Buffer.ID & TransientFlag) {
            v.BufferLocation = TransientAddress(view);
            v.SizeInBytes = (UINT)(m_Frames[m_FrameIndex].Ring.Pages[(view.Buffer.ID & ~TransientFlag)].Size - view.Offset);
        }
        u32 stride = view.Stride;
        if (m_Bound && m_Bound->Desc.Strides[slot]) stride = m_Bound->Desc.Strides[slot];
        v.StrideInBytes = stride;
        m_List->IASetVertexBuffers(slot, 1, &v);
    }

    void SetIndexBuffer(const BufferView& view) override {
        if (!m_InFrame) return;
        D3D12_INDEX_BUFFER_VIEW v{};
        if (const DXBuffer* b = m_Buffers.Get(view.Buffer.ID)) {
            v.BufferLocation = b->Address + view.Offset;
            v.SizeInBytes = (UINT)(b->Size - view.Offset);
        }
        v.Format = DXGI_FORMAT_R32_UINT;
        m_List->IASetIndexBuffer(&v);
    }

    BufferView Transient(const void* data, u64 size, u32 stride) override {
        BufferView v;
        if (!m_InFrame) return v;
        D3D12_GPU_VIRTUAL_ADDRESS va = 0;
        void* dst = Upload(size, 16, va);
        if (!dst) return v;
        std::memcpy(dst, data, (size_t)size);
        const UploadRing& ring = m_Frames[m_FrameIndex].Ring;
        v.Buffer.ID = TransientFlag | (u32)ring.Current;     // page index, resolved in SetVertexBuffer
        v.Offset = va - ring.Pages[ring.Current].GPU;
        v.Stride = stride;
        return v;
    }

    void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex) override {
        if (!m_Bound || !m_InFrame || !vertexCount) return;
        FlushTextures();
        m_List->DrawInstanced(vertexCount, instanceCount, firstVertex, 0);
        ++FrameDrawCalls;
    }

    void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 baseVertex) override {
        if (!m_Bound || !m_InFrame || !indexCount) return;
        FlushTextures();
        m_List->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, baseVertex, 0);
        ++FrameDrawCalls;
    }

    void Resolve(RenderTargetHandle src, RenderTargetHandle dst) override {
        if (!m_InFrame) return;
        const DXRenderTarget* s = m_Targets.Get(src.ID);
        const DXRenderTarget* d = m_Targets.Get(dst.ID);
        if (!s || !d) return;
        DXTexture* sc = m_Textures.Get(s->Color.ID);
        DXTexture* dc = m_Textures.Get(d->Color.ID);
        if (sc && dc) {
            if (sc->Desc.Samples > 1) {
                SetState(*sc, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                SetState(*dc, D3D12_RESOURCE_STATE_RESOLVE_DEST);
                m_List->ResolveSubresource(dc->Resource.Get(), 0, sc->Resource.Get(), 0, ToDX(dc->Desc.Fmt).Resource);
            } else {
                SetState(*sc, D3D12_RESOURCE_STATE_COPY_SOURCE);
                SetState(*dc, D3D12_RESOURCE_STATE_COPY_DEST);
                m_List->CopyResource(dc->Resource.Get(), sc->Resource.Get());
            }
        }
        DXTexture* sd = m_Textures.Get(s->Depth.ID);
        DXTexture* dd = m_Textures.Get(d->Depth.ID);
        if (sd && dd) {
            if (sd->Desc.Samples > 1) {
                // Depth can't be ResolveSubresource'd: copy sample 0 with a depth-writing shader.
                const PipelineHandle p = DepthResolvePipeline(dd->Desc.Fmt);
                if (!p) return;
                SetState(*dd, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_DSVs.Cpu(dd->DSV);
                m_List->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
                D3D12_VIEWPORT vp{0.0f, 0.0f, (float)d->Width, (float)d->Height, 0.0f, 1.0f};
                D3D12_RECT rc{0, 0, (LONG)d->Width, (LONG)d->Height};
                m_List->RSSetViewports(1, &vp);
                m_List->RSSetScissorRects(1, &rc);
                SetPipeline(p);
                for (u32 i = 0; i < TextureSlots; ++i) m_Slots[i] = {};
                m_Slots[0] = s->Depth;
                m_SlotsDirty = true;
                FlushTextures();
                m_List->DrawInstanced(3, 1, 0, 0);
                m_Bound = nullptr;
            } else {
                SetState(*sd, D3D12_RESOURCE_STATE_COPY_SOURCE);
                SetState(*dd, D3D12_RESOURCE_STATE_COPY_DEST);
                m_List->CopyResource(dd->Resource.Get(), sd->Resource.Get());
            }
        }
    }

    void CaptureNextFrame() override { m_CaptureRequested = true; }

    std::vector<u8> GetCapturedFrame(u32& width, u32& height) override {
        width = height = 0;
        if (!m_Readback) return {};
        WaitIdle();   // the copy was recorded in the last submitted frame
        width = m_CaptureWidth;
        height = m_CaptureHeight;
        std::vector<u8> pixels(size_t(width) * height * 4);
        u8* src = nullptr;
        if (FAILED(m_Readback->Map(0, nullptr, reinterpret_cast<void**>(&src)))) return {};
        // D3D rows are top-down; the RHI returns bottom row first.
        for (u32 y = 0; y < height; ++y)
            std::memcpy(&pixels[size_t(height - 1 - y) * width * 4], src + size_t(y) * m_CapturePitch, size_t(width) * 4);
        D3D12_RANGE none{0, 0};
        m_Readback->Unmap(0, &none);
        return pixels;
    }

private:
    static constexpr u32 TransientFlag = 0x80000000u;

    struct Frame {
        Com<ID3D12CommandAllocator> Allocator;
        UINT64 FenceValue = 0;
        UploadRing Ring;
    };
    struct Garbage { UINT64 Fence; Com<ID3D12Resource> Resource; Com<ID3D12PipelineState> PSO; };
    std::vector<Garbage> m_Garbage;
    struct CompiledShader { std::string Name; Com<ID3DBlob> VS, PS; };

    // ---- helpers ------------------------------------------------------------------
    static D3D12_RESOURCE_DESC BufferResourceDesc(u64 size) {
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = size;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return d;
    }

    Com<ID3D12Resource> CreateCommitted(D3D12_HEAP_TYPE heap, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = heap;
        Com<ID3D12Resource> r;
        const HRESULT hr = m_Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, __uuidof(ID3D12Resource), r.PutVoid());
        if (FAILED(hr)) LAPIS_ERROR("D3D12: CreateCommittedResource ({} bytes) failed {}", (u64)desc.Width, HrString(hr));
        return r;
    }

    static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = r;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return b;
    }

    void SetState(DXTexture& t, D3D12_RESOURCE_STATES state) {
        if (t.State == state) return;
        D3D12_RESOURCE_BARRIER b = Transition(t.Resource.Get(), t.State, state);
        m_List->ResourceBarrier(1, &b);
        t.State = state;
    }

    // Destroyed objects stay alive until the GPU has passed every command
    // submitted so far (tagged with the next fence value), whenever Destroy is called.
    void Defer(const Com<ID3D12Resource>& r) { if (r) m_Garbage.push_back({m_FenceCounter + 1, r, {}}); }
    void DeferPSO(const Com<ID3D12PipelineState>& p) { if (p) m_Garbage.push_back({m_FenceCounter + 1, {}, p}); }
    void CollectGarbage() {
        const UINT64 done = m_Fence->GetCompletedValue();
        m_Garbage.erase(std::remove_if(m_Garbage.begin(), m_Garbage.end(), [&](const Garbage& g) { return g.Fence <= done; }), m_Garbage.end());
    }

    void WaitForFence(UINT64 value) {
        if (value == 0 || m_Fence->GetCompletedValue() >= value) return;
        m_Fence->SetEventOnCompletion(value, m_FenceEvent);
        WaitForSingleObject(m_FenceEvent, 10000);
    }

    void WaitIdle() {
        if (!m_Queue || !m_Fence) return;
        const UINT64 v = ++m_FenceCounter;
        m_Queue->Signal(m_Fence.Get(), v);
        WaitForFence(v);
    }

    // Immediate command list for uploads at load time (waits for completion).
    void ImmediateBegin() {
        if (!m_ImmAllocator) {
            m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), m_ImmAllocator.PutVoid());
            m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_ImmAllocator.Get(), nullptr, __uuidof(ID3D12GraphicsCommandList), m_Imm.PutVoid());
            m_Imm->Close();
        }
        m_ImmAllocator->Reset();
        m_Imm->Reset(m_ImmAllocator.Get(), nullptr);
    }
    void ImmediateEnd() {
        m_Imm->Close();
        ID3D12CommandList* lists[] = {m_Imm.Get()};
        m_Queue->ExecuteCommandLists(1, lists);
        WaitIdle();
    }

    // CPU mip chain (box filter) + copy of every level, then ready for sampling.
    void UploadTexture(DXTexture& t, const void* data, const D3D12_RESOURCE_DESC& rd, u32 w, u32 h, u32 mips) {
        const u32 bpp = BytesPerPixel(t.Desc.Fmt);
        std::vector<std::vector<u8>> levels(mips);
        const u8* src0 = static_cast<const u8*>(data);
        levels[0].assign(src0, src0 + size_t(w) * h * bpp);
        u32 lw = w, lh = h;
        for (u32 m = 1; m < mips; ++m) {
            const u32 nw = std::max(lw / 2, 1u), nh = std::max(lh / 2, 1u);
            const std::vector<u8>& prev = levels[m - 1];
            std::vector<u8>& cur = levels[m];
            cur.resize(size_t(nw) * nh * bpp);
            for (u32 y = 0; y < nh; ++y)
                for (u32 x = 0; x < nw; ++x)
                    for (u32 c = 0; c < bpp; ++c) {
                        const u32 x0 = std::min(x * 2, lw - 1), x1 = std::min(x * 2 + 1, lw - 1);
                        const u32 y0 = std::min(y * 2, lh - 1), y1 = std::min(y * 2 + 1, lh - 1);
                        const u32 sum = prev[(size_t(y0) * lw + x0) * bpp + c] + prev[(size_t(y0) * lw + x1) * bpp + c] +
                                        prev[(size_t(y1) * lw + x0) * bpp + c] + prev[(size_t(y1) * lw + x1) * bpp + c];
                        cur[(size_t(y) * nw + x) * bpp + c] = (u8)((sum + 2) / 4);
                    }
            lw = nw;
            lh = nh;
        }

        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(mips);
        std::vector<UINT> rows(mips);
        std::vector<UINT64> rowBytes(mips);
        UINT64 total = 0;
        m_Device->GetCopyableFootprints(&rd, 0, mips, 0, fp.data(), rows.data(), rowBytes.data(), &total);
        Com<ID3D12Resource> upload = CreateCommitted(D3D12_HEAP_TYPE_UPLOAD, BufferResourceDesc(total), D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!upload) return;
        u8* dst = nullptr;
        upload->Map(0, nullptr, reinterpret_cast<void**>(&dst));
        lw = w;
        for (u32 m = 0; m < mips; ++m) {
            const size_t rowSize = size_t(std::max(w >> m, 1u)) * bpp;
            for (UINT r = 0; r < rows[m]; ++r)
                std::memcpy(dst + fp[m].Offset + size_t(r) * fp[m].Footprint.RowPitch, levels[m].data() + size_t(r) * rowSize, rowSize);
        }
        upload->Unmap(0, nullptr);

        ImmediateBegin();
        for (u32 m = 0; m < mips; ++m) {
            D3D12_TEXTURE_COPY_LOCATION to{}, from{};
            to.pResource = t.Resource.Get();
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.SubresourceIndex = m;
            from.pResource = upload.Get();
            from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            from.PlacedFootprint = fp[m];
            m_Imm->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        }
        D3D12_RESOURCE_BARRIER b = Transition(t.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_Imm->ResourceBarrier(1, &b);
        ImmediateEnd();
        t.State = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    void CaptureBackbuffer(ID3D12Resource* back) {
        const u32 pitch = (m_BackWidth * 4 + 255) & ~255u;
        const u64 size = (u64)pitch * m_BackHeight;
        if (!m_Readback || m_ReadbackSize < size) {
            if (m_Readback) Defer(m_Readback);
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd = BufferResourceDesc(size);
            m_Readback.Reset();
            if (FAILED(m_Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         __uuidof(ID3D12Resource), m_Readback.PutVoid())))
                return;
            m_ReadbackSize = size;
        }
        D3D12_RESOURCE_BARRIER b = Transition(back, m_BackState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_List->ResourceBarrier(1, &b);
        m_BackState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = m_Readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = m_BackWidth;
        dst.PlacedFootprint.Footprint.Height = m_BackHeight;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = pitch;
        src.pResource = back;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        m_List->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        m_CaptureWidth = m_BackWidth;
        m_CaptureHeight = m_BackHeight;
        m_CapturePitch = pitch;
    }

    // Upload ring allocation (per frame, grows by pages of at least 8 MB).
    void* Upload(u64 size, u64 align, D3D12_GPU_VIRTUAL_ADDRESS& va) {
        UploadRing& r = m_Frames[m_FrameIndex].Ring;
        for (;;) {
            if (r.Current < r.Pages.size()) {
                UploadRing::Page& p = r.Pages[r.Current];
                const u64 offset = (r.Offset + align - 1) / align * align;
                if (offset + size <= p.Size) {
                    r.Offset = offset + size;
                    va = p.GPU + offset;
                    return p.CPU + offset;
                }
                ++r.Current;
                r.Offset = 0;
                continue;
            }
            UploadRing::Page page;
            page.Size = std::max<u64>(8ull << 20, (size + 0xFFFF) & ~0xFFFFull);
            page.Resource = CreateCommitted(D3D12_HEAP_TYPE_UPLOAD, BufferResourceDesc(page.Size), D3D12_RESOURCE_STATE_GENERIC_READ);
            if (!page.Resource) return nullptr;
            page.Resource->Map(0, nullptr, reinterpret_cast<void**>(&page.CPU));
            page.GPU = page.Resource->GetGPUVirtualAddress();
            r.Pages.push_back(std::move(page));
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS TransientAddress(const BufferView& v) const {
        const UploadRing& r = m_Frames[m_FrameIndex].Ring;
        const u32 page = v.Buffer.ID & ~TransientFlag;
        return page < r.Pages.size() ? r.Pages[page].GPU + v.Offset : 0;
    }

    // Copies the 8 SRVs of the current bindings into the shader-visible ring.
    void FlushTextures() {
        if (!m_SlotsDirty) return;
        m_SlotsDirty = false;
        if (m_ViewCursor + TextureSlots > m_ViewEnd) {
            LAPIS_WARN("D3D12: shader-visible descriptor ring exhausted this frame");
            m_ViewCursor = m_ViewEnd - TextureSlots;
        }
        const u32 base = m_ViewCursor;
        m_ViewCursor += TextureSlots;
        for (u32 i = 0; i < TextureSlots; ++i) {
            u32 srv = m_NullSRV;
            if (DXTexture* t = m_Textures.Get(m_Slots[i].ID)) {
                SetState(*t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                srv = t->SRV;
            }
            m_Device->CopyDescriptorsSimple(1, m_GPUViews.Cpu(base + i), m_SRVs.Cpu(srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        m_List->SetGraphicsRootDescriptorTable(2, m_GPUViews.Gpu(base));
    }

    void WriteSamplers(float anisotropy) {
        m_Anisotropy = anisotropy;
        m_SamplerSet = (m_SamplerSet + 1) % 64;   // never overwrite a set the GPU may still use
        auto make = [&](u32 index, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE mode, bool compare = false) {
            D3D12_SAMPLER_DESC s{};
            s.Filter = filter;
            s.AddressU = s.AddressV = s.AddressW = mode;
            s.MaxAnisotropy = filter == D3D12_FILTER_ANISOTROPIC ? (UINT)anisotropy : 1;
            s.ComparisonFunc = compare ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER;
            s.BorderColor[0] = s.BorderColor[1] = s.BorderColor[2] = s.BorderColor[3] = 1.0f;
            s.MinLOD = 0.0f;
            s.MaxLOD = D3D12_FLOAT32_MAX;
            m_Device->CreateSampler(&s, m_GPUSamplers.Cpu(m_SamplerSet * SamplerCount + index));
        };
        make((u32)Sampler::LinearClamp, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        make((u32)Sampler::LinearWrap, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
        make((u32)Sampler::PointClamp, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        make((u32)Sampler::AnisoWrap, anisotropy > 1.0f ? D3D12_FILTER_ANISOTROPIC : D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
        make((u32)Sampler::ShadowCompare, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, true);
    }

    bool CreateRootSignature(std::string& error) {
        D3D12_DESCRIPTOR_RANGE srv{};
        srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv.NumDescriptors = TextureSlots;
        srv.BaseShaderRegister = 0;
        srv.OffsetInDescriptorsFromTableStart = 0;
        D3D12_DESCRIPTOR_RANGE smp{};
        smp.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        smp.NumDescriptors = SamplerCount;
        smp.BaseShaderRegister = 0;
        smp.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[4] = {};
        for (u32 i = 0; i < 2; ++i) {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[i].Descriptor.ShaderRegister = i;
            params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &srv;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &smp;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rd{};
        rd.NumParameters = 4;
        rd.pParameters = params;
        rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Com<ID3DBlob> blob, err;
        HRESULT hr = m_Lib.SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, blob.Put(), err.Put());
        if (FAILED(hr)) {
            error = "root signature: " + (err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : HrString(hr));
            return false;
        }
        hr = m_Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), m_RootSignature.PutVoid());
        if (FAILED(hr)) { error = "CreateRootSignature failed " + HrString(hr); return false; }
        return true;
    }

    const CompiledShader* CompileShader(const ShaderLibrary::Source& src, std::string& error) {
        for (const CompiledShader& c : m_Shaders)
            if (c.Name == src.Name) return &c;
        CompiledShader c;
        c.Name = src.Name;
#if LAPIS_DEBUG
        const UINT flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        const size_t len = std::strlen(src.HLSL);
        for (int stage = 0; stage < 2; ++stage) {
            Com<ID3DBlob> code, errors;
            const HRESULT hr = m_Lib.Compile(src.HLSL, len, src.Name, nullptr, nullptr, stage == 0 ? "VSMain" : "PSMain",
                                             stage == 0 ? "vs_5_1" : "ps_5_1", flags, 0, code.Put(), errors.Put());
            if (FAILED(hr)) {
                error = std::string("HLSL shader '") + src.Name + "' (" + (stage == 0 ? "vertex" : "pixel") + ") failed to compile:\n" +
                        (errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : HrString(hr));
                LAPIS_ERROR("D3D12: {}", error);
                return nullptr;
            }
            (stage == 0 ? c.VS : c.PS) = code;
        }
        m_Shaders.push_back(std::move(c));
        return &m_Shaders.back();
    }

    PipelineHandle DepthResolvePipeline(Format depthFormat) {
        PipelineHandle& p = depthFormat == Format::D32F ? m_DepthResolve32 : m_DepthResolve24;
        if (!p) {
            PipelineDesc d;
            d.Shader = "DepthResolve";
            d.ColorFormat = Format::None;
            d.DepthFormat = depthFormat;
            d.Depth = DepthMode::Always;
            d.Name = "DepthResolve";
            p = CreatePipeline(d);
        }
        return p;
    }

    void CreateBackbufferViews() {
        for (u32 i = 0; i < SwapBuffers; ++i) {
            m_SwapChain->GetBuffer(i, __uuidof(ID3D12Resource), m_Backbuffers[i].PutVoid());
            if (m_BackRTV[i] == UINT32_MAX) m_BackRTV[i] = m_RTVs.Allocate();
            m_Device->CreateRenderTargetView(m_Backbuffers[i].Get(), nullptr, m_RTVs.Cpu(m_BackRTV[i]));
        }
    }

    void Resize(u32 width, u32 height) {
        WaitIdle();
        for (u32 i = 0; i < SwapBuffers; ++i) m_Backbuffers[i].Reset();
        const HRESULT hr = m_SwapChain->ResizeBuffers(SwapBuffers, width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                      m_Tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        if (FAILED(hr)) LAPIS_ERROR("D3D12: ResizeBuffers({}x{}) failed {}", width, height, HrString(hr));
        m_BackWidth = width;
        m_BackHeight = height;
        CreateBackbufferViews();
    }

    Window& m_Window;
    HWND m_Hwnd = nullptr;
    Libraries m_Lib;
    DeviceInfo m_Info;
    Com<IDXGIFactory4> m_Factory;
    Com<ID3D12Device> m_Device;
    Com<ID3D12InfoQueue> m_InfoQueue;
    u32 m_DebugMessagesLogged = 0;
    Com<ID3D12CommandQueue> m_Queue;
    Com<ID3D12Fence> m_Fence;
    HANDLE m_FenceEvent = nullptr;
    UINT64 m_FenceCounter = 0;
    Com<ID3D12GraphicsCommandList> m_List;
    Com<ID3D12CommandAllocator> m_ImmAllocator;
    Com<ID3D12GraphicsCommandList> m_Imm;
    Com<ID3D12RootSignature> m_RootSignature;
    Com<IDXGISwapChain3> m_SwapChain;
    Com<ID3D12Resource> m_Backbuffers[SwapBuffers];
    u32 m_BackRTV[SwapBuffers] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    u32 m_BackIndex = 0;
    D3D12_RESOURCE_STATES m_BackState = D3D12_RESOURCE_STATE_PRESENT;
    u32 m_BackWidth = 1, m_BackHeight = 1;
    bool m_Tearing = false, m_VSync = true, m_InFrame = false, m_Initialized = false, m_Lost = false;

    Frame m_Frames[FramesInFlight];
    u32 m_FrameIndex = 0;

    DescriptorPool m_RTVs, m_DSVs, m_SRVs, m_GPUViews, m_GPUSamplers;
    u32 m_NullSRV = 0;
    u32 m_ViewCursor = 0, m_ViewEnd = 0;
    u32 m_SamplerSet = 0;
    float m_Anisotropy = 0.0f;

    Pool<DXBuffer> m_Buffers;
    Pool<DXTexture> m_Textures;
    Pool<DXRenderTarget> m_Targets;
    Pool<DXPipeline> m_Pipelines;
    std::deque<CompiledShader> m_Shaders;   // deque: stable addresses for returned pointers
    PipelineHandle m_DepthResolve24, m_DepthResolve32;

    DXPipeline* m_Bound = nullptr;
    TextureHandle m_Slots[TextureSlots];
    bool m_SlotsDirty = true;

    bool m_CaptureRequested = false;
    Com<ID3D12Resource> m_Readback;
    u64 m_ReadbackSize = 0;
    u32 m_CaptureWidth = 0, m_CaptureHeight = 0, m_CapturePitch = 0;
};

} // namespace

Scope<Device> CreateD3D12Device(Window& window, std::string* error) {
    auto device = CreateScope<D3D12Device>(window);
    if (!device->Init(error)) return nullptr;
    return device;
}

} // namespace Lapis::RHI
