#include "Lapis/RHI/RHI.h"

#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Window.h"

namespace Lapis::RHI {

Scope<Device> CreateGLDevice(Window& window, std::string* error);
#if defined(_WIN32)
Scope<Device> CreateD3D12Device(Window& window, std::string* error);
#endif

const char* APIName(API api) {
    switch (api) {
        case API::OpenGL: return "OpenGL";
        case API::D3D12: return "DirectX 12";
        case API::D3D11: return "DirectX 11";
        case API::Vulkan: return "Vulkan";
    }
    return "?";
}

bool APIFromName(const std::string& name, API& out) {
    if (name == "OpenGL" || name == "opengl" || name == "gl") { out = API::OpenGL; return true; }
    if (name == "DirectX 12" || name == "D3D12" || name == "d3d12" || name == "dx12") { out = API::D3D12; return true; }
    if (name == "DirectX 11" || name == "D3D11" || name == "d3d11" || name == "dx11") { out = API::D3D11; return true; }
    if (name == "Vulkan" || name == "vulkan" || name == "vk") { out = API::Vulkan; return true; }
    return false;
}

bool IsDepthFormat(Format f) { return f == Format::D24 || f == Format::D32F; }

u32 BytesPerPixel(Format f) {
    switch (f) {
        case Format::R8: return 1;
        case Format::RGBA16F: return 8;
        case Format::None: return 0;
        default: return 4;
    }
}

bool Device::IsCompiledIn(API api) {
#if defined(_WIN32)
    if (api == API::D3D12) return true;
#endif
    return api == API::OpenGL;
}

Scope<Device> Device::Create(API api, Window& window, std::string* error) {
    Scope<Device> device;
    std::string err;
    switch (api) {
        case API::OpenGL: device = CreateGLDevice(window, &err); break;
#if defined(_WIN32)
        case API::D3D12: device = CreateD3D12Device(window, &err); break;
#endif
        default: err = std::string(APIName(api)) + " is not available in this build"; break;
    }
    if (!device) {
        LAPIS_WARN("RHI: {} could not start: {}", APIName(api), err);
        if (error) *error = err;
        return nullptr;
    }
    const DeviceInfo& info = device->GetInfo();
    LAPIS_INFO("RHI: {} on {} ({}) - {}", APIName(api), info.GPUName, info.Vendor, info.Version);
    return device;
}

namespace {
Device* s_Current = nullptr;
}
Device* Current() { return s_Current; }
void SetCurrent(Device* device) { s_Current = device; }

} // namespace Lapis::RHI
