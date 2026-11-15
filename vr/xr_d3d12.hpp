#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11on12.h>
#include <cstdlib>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "tracking_origin.hpp"

namespace dtvr {
using Microsoft::WRL::ComPtr;
inline void check_hr(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s failed: HRESULT 0x%08lx", operation,
                      (unsigned long)result);
        throw std::runtime_error(buffer);
    }
}
inline void check_xr(XrResult result, const char* operation) {
    if (XR_FAILED(result)) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s failed: XrResult %d", operation, (int)result);
        throw std::runtime_error(buffer);
    }
}
#define DT_XR_FUNCTIONS(F)                                                                         \
    F(xrDestroyInstance)                                                                           \
    F(xrGetInstanceProperties)                                                                     \
    F(xrGetSystem)                                                                                 \
    F(xrGetSystemProperties)                                                                       \
    F(xrCreateSession)                                                                             \
    F(xrDestroySession)                                                                            \
    F(xrEnumerateViewConfigurationViews)                                                           \
    F(xrCreateReferenceSpace)                                                                      \
    F(xrDestroySpace)                                                                              \
    F(xrEnumerateSwapchainFormats)                                                                 \
    F(xrCreateSwapchain)                                                                           \
    F(xrEnumerateSwapchainImages)                                                                  \
    F(xrDestroySwapchain)                                                                          \
    F(xrPollEvent)                                                                                 \
    F(xrBeginSession)                                                                              \
    F(xrEndSession)                                                                                \
    F(xrRequestExitSession)                                                                        \
    F(xrWaitFrame)                                                                                 \
    F(xrBeginFrame)                                                                                \
    F(xrLocateViews)                                                                               \
    F(xrLocateSpace)                                                                               \
    F(xrAcquireSwapchainImage)                                                                     \
    F(xrWaitSwapchainImage)                                                                        \
    F(xrReleaseSwapchainImage)                                                                     \
    F(xrEndFrame)

struct Eye {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    uint32_t width = 0, height = 0, acquired = 0, runtimeAcquired = 0;
    std::vector<XrSwapchainImageD3D12KHR> images;
    std::vector<XrSwapchainImageD3D11KHR> images11;
    ComPtr<ID3D12Resource> interopTarget;
    ComPtr<ID3D11Resource> wrappedTarget;
};

// Standard OpenXR loader, native Windows or Proton on Linux.
// The loader is an unmodified, pinned Khronos redistributable.
class XrBridge {
  public:
    HMODULE loader = nullptr;
    PFN_xrGetInstanceProcAddr getProc = nullptr;
#define DT_FIELD(name) PFN_##name name = nullptr;
    DT_XR_FUNCTIONS(DT_FIELD)
#undef DT_FIELD
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE, headSpace = XR_NULL_HANDLE;
    std::array<Eye, 2> eyes;
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    XrViewStateFlags viewStateFlags = 0;
    TrackingOrigin trackingOrigin;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    XrGraphicsRequirementsD3D12KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    bool useD3D11 = false;
    uint32_t renderWidth = 0, renderHeight = 0;
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context11;
    ComPtr<ID3D11On12Device> on12;
    ComPtr<ID3D11Query> interopQuery;
    ComPtr<ID3D12CommandQueue> interopQueue;
    ComPtr<ID3D12Fence> interopFence;
    HANDLE interopEvent = nullptr;
    uint64_t interopValue = 0;
    bool running = false, quit = false, visible = false, frameBegun = false;
    bool focused = false, picoControllerProfile = false;
    uint64_t frameCount = 0;

    template <class T> void resolve(const char* name, T& pointer, XrInstance handle) {
        check_xr(getProc(handle, name, reinterpret_cast<PFN_xrVoidFunction*>(&pointer)), name);
        if (!pointer)
            throw std::runtime_error(std::string("Missing OpenXR function: ") + name);
    }
    XrGraphicsRequirementsD3D12KHR prepare(const wchar_t* loaderPath) {
        loader = LoadLibraryW(loaderPath);
        if (!loader)
            throw std::runtime_error("Cannot load the Khronos openxr_loader.dll (Win32 " +
                                     std::to_string(GetLastError()) + ")");
        getProc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
            GetProcAddress(loader, "xrGetInstanceProcAddr"));
        if (!getProc)
            throw std::runtime_error("OpenXR loader has no xrGetInstanceProcAddr");
        PFN_xrEnumerateInstanceExtensionProperties enumerate = nullptr;
        PFN_xrCreateInstance create = nullptr;
        resolve("xrEnumerateInstanceExtensionProperties", enumerate, XR_NULL_HANDLE);
        resolve("xrCreateInstance", create, XR_NULL_HANDLE);
        uint32_t count = 0;
        check_xr(enumerate(nullptr, 0, &count, nullptr), "Enumerate OpenXR extensions");
        std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
        check_xr(enumerate(nullptr, count, &count, extensions.data()), "Read OpenXR extensions");
        bool d3d12 = std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
            return std::strcmp(e.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0;
        });
        const bool d3d11 = std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
            return std::strcmp(e.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
        });
        const char* graphics = std::getenv("DIRTTOWN_VR_GRAPHICS");
        useD3D11 = graphics && std::strcmp(graphics, "d3d11") == 0;
        if (!graphics || std::strcmp(graphics, "auto") == 0)
            useD3D11 = !d3d12 && d3d11;
        if ((useD3D11 && !d3d11) || (!useD3D11 && !d3d12))
            throw std::runtime_error(
                "Runtime lacks the selected D3D12/D3D11 OpenXR graphics binding. Try another graphics mode or runtime.");
        std::vector<const char*> enabled{useD3D11 ? XR_KHR_D3D11_ENABLE_EXTENSION_NAME
                                                  : XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
        picoControllerProfile =
            std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
                return std::strcmp(e.extensionName, XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME) ==
                       0;
            });
        if (picoControllerProfile)
            enabled.push_back(XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME);
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        std::strcpy(info.applicationInfo.applicationName, "Dirt Town VR prototype");
        info.applicationInfo.applicationVersion = 1;
        std::strcpy(info.applicationInfo.engineName, "DirtTownMod");
        info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        info.enabledExtensionCount = (uint32_t)enabled.size();
        info.enabledExtensionNames = enabled.data();
        check_xr(create(&info, &instance), "Create OpenXR instance");
#define DT_RESOLVE(name) resolve(#name, name, instance);
        DT_XR_FUNCTIONS(DT_RESOLVE)
#undef DT_RESOLVE
        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
        check_xr(xrGetInstanceProperties(instance, &properties), "Read runtime");
        std::printf("OpenXR runtime: %s %u.%u.%u\n", properties.runtimeName,
                    (unsigned)XR_VERSION_MAJOR(properties.runtimeVersion),
                    (unsigned)XR_VERSION_MINOR(properties.runtimeVersion),
                    (unsigned)XR_VERSION_PATCH(properties.runtimeVersion));
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        check_xr(xrGetSystem(instance, &systemInfo, &system), "Find connected headset");
        XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
        check_xr(xrGetSystemProperties(instance, system, &systemProperties), "Read headset");
        std::printf("Headset: %s; orientation tracking %u; position tracking %u\n",
                    systemProperties.systemName,
                    systemProperties.trackingProperties.orientationTracking,
                    systemProperties.trackingProperties.positionTracking);
        if (useD3D11) {
            PFN_xrGetD3D11GraphicsRequirementsKHR get = nullptr;
            resolve("xrGetD3D11GraphicsRequirementsKHR", get, instance);
            XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
            check_xr(get(instance, system, &req), "D3D11 graphics requirements");
            requirements.adapterLuid = req.adapterLuid;
            requirements.minFeatureLevel = req.minFeatureLevel;
        } else {
            PFN_xrGetD3D12GraphicsRequirementsKHR get = nullptr;
            resolve("xrGetD3D12GraphicsRequirementsKHR", get, instance);
            check_xr(get(instance, system, &requirements), "D3D12 graphics requirements");
        }
        std::printf("OpenXR graphics: %s; required GPU %08lx:%08lx, feature level 0x%x\n",
                    useD3D11 ? "D3D11 interoperability" : "D3D12",
                    (unsigned long)requirements.adapterLuid.HighPart,
                    (unsigned long)requirements.adapterLuid.LowPart,
                    (unsigned)requirements.minFeatureLevel);
        return requirements;
    }
    void bind(ID3D12Device* device, ID3D12CommandQueue* queue, float resolutionScale = 0.5f) {
        const auto actual = device->GetAdapterLuid();
        if (actual.HighPart != requirements.adapterLuid.HighPart ||
            actual.LowPart != requirements.adapterLuid.LowPart)
            throw std::runtime_error(
                "Game and OpenXR runtime use different GPUs. Select the same GPU for the game and VR runtime, then restart.");
        D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
        levels.NumFeatureLevels = 1;
        levels.pFeatureLevelsRequested = &requirements.minFeatureLevel;
        check_hr(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels)),
                 "Check OpenXR GPU feature level");
        if (levels.MaxSupportedFeatureLevel < requirements.minFeatureLevel)
            throw std::runtime_error("Game GPU does not meet the OpenXR feature level requirement");
        XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
        binding.device = device;
        binding.queue = queue;
        XrGraphicsBindingD3D11KHR binding11{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        if (useD3D11) {
            createD3D11Interop(device, queue);
            binding11.device = device11.Get();
        }
        XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
        info.next =
            useD3D11 ? static_cast<const void*>(&binding11) : static_cast<const void*>(&binding);
        info.systemId = system;
        check_xr(xrCreateSession(instance, &info, &session), "Create OpenXR graphics session");
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        space.poseInReferenceSpace.orientation.w = 1;
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        check_xr(xrCreateReferenceSpace(session, &space, &localSpace), "Create local space");
        space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        check_xr(xrCreateReferenceSpace(session, &space, &headSpace), "Create head space");
        uint32_t count = 0;
        check_xr(xrEnumerateSwapchainFormats(session, 0, &count, nullptr), "Enumerate eye formats");
        std::vector<int64_t> formats(count);
        check_xr(xrEnumerateSwapchainFormats(session, count, &count, formats.data()),
                 "Read eye formats");
        for (auto candidate : {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                               DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM}) {
            if (std::find(formats.begin(), formats.end(), (int64_t)candidate) != formats.end()) {
                format = candidate;
                break;
            }
        }
        if (format == DXGI_FORMAT_UNKNOWN)
            throw std::runtime_error("No supported OpenXR eye texture format");
        check_xr(xrEnumerateViewConfigurationViews(instance, system,
                                                   XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                                   &count, nullptr),
                 "Enumerate stereo views");
        if (count != 2)
            throw std::runtime_error("Expected two stereo views");
        std::array<XrViewConfigurationView, 2> configurations{
            {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        check_xr(xrEnumerateViewConfigurationViews(instance, system,
                                                   XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                                                   &count, configurations.data()),
                 "Read stereo views");
        for (unsigned i = 0; i < 2; ++i) {
            auto& eye = eyes[i];
            const auto& c = configurations[i];
            const uint32_t maxWidth = std::min(c.maxImageRectWidth, 16384u),
                           maxHeight = std::min(c.maxImageRectHeight, 16384u);
            if (!maxWidth || !maxHeight)
                throw std::runtime_error("Runtime reported invalid maximum eye dimensions");
            eye.width =
                std::clamp((uint32_t)(c.recommendedImageRectWidth * resolutionScale), 1u, maxWidth);
            eye.height = std::clamp((uint32_t)(c.recommendedImageRectHeight * resolutionScale), 1u,
                                    maxHeight);
            renderWidth = std::max(renderWidth, eye.width);
            renderHeight = std::max(renderHeight, eye.height);
            std::printf("Eye %u: recommended %ux%u, test %ux%u\n", i, c.recommendedImageRectWidth,
                        c.recommendedImageRectHeight, eye.width, eye.height);
            XrSwapchainCreateInfo swap{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            swap.usageFlags =
                XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            swap.format = format;
            swap.sampleCount = 1;
            swap.width = eye.width;
            swap.height = eye.height;
            swap.faceCount = 1;
            swap.arraySize = 1;
            swap.mipCount = 1;
            check_xr(xrCreateSwapchain(session, &swap, &eye.swapchain), "Create eye swapchain");
            check_xr(xrEnumerateSwapchainImages(eye.swapchain, 0, &count, nullptr),
                     "Enumerate eye images");
            if (useD3D11) {
                eye.images11.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
                check_xr(xrEnumerateSwapchainImages(
                             eye.swapchain, count, &count,
                             reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images11.data())),
                         "Read D3D11 eye images");
                createInteropTarget(device, eye);
            } else {
                eye.images.resize(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
                check_xr(xrEnumerateSwapchainImages(
                             eye.swapchain, count, &count,
                             reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())),
                         "Read D3D12 eye images");
            }
        }
        std::printf(
            "OpenXR stereo ready; common game render extent %ux%u, per-eye swapchains retained\n",
            renderWidth, renderHeight);
    }
    void poll() {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult result;
        while ((result = xrPollEvent(instance, &event)) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto state = reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
                std::printf("Session state: %d\n", (int)state);
                if (state != XR_SESSION_STATE_VISIBLE && state != XR_SESSION_STATE_FOCUSED)
                    trackingOrigin.reset();
                if (state == XR_SESSION_STATE_FOCUSED && !focused)
                    trackingOrigin.reset();
                visible = state == XR_SESSION_STATE_VISIBLE || state == XR_SESSION_STATE_FOCUSED;
                focused = state == XR_SESSION_STATE_FOCUSED;
                if (state == XR_SESSION_STATE_READY && !running) {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    check_xr(xrBeginSession(session, &begin), "Begin VR session");
                    running = true;
                } else if (state == XR_SESSION_STATE_STOPPING && running) {
                    check_xr(xrEndSession(session), "End VR session");
                    running = false;
                } else if (state == XR_SESSION_STATE_EXITING ||
                           state == XR_SESSION_STATE_LOSS_PENDING)
                    quit = true;
            } else if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                const auto& change =
                    *reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&event);
                if (change.session == session &&
                    change.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                    trackingOrigin.spaceChangePending(change.changeTime);
                    std::printf("Local tracking origin change scheduled at %lld\n",
                                (long long)change.changeTime);
                }
            } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
                quit = true;
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (result != XR_EVENT_UNAVAILABLE)
            check_xr(result, "Poll VR events");
    }
    bool beginFrame() {
        poll();
        if (!running || quit)
            return false;
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        check_xr(xrWaitFrame(session, &wait, &frame), "Wait for VR frame");
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        check_xr(xrBeginFrame(session, &begin), "Begin VR frame");
        frameBegun = true;
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = frame.predictedDisplayTime;
        locate.space = localSpace;
        XrViewState state{XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        check_xr(xrLocateViews(session, &locate, &state, 2, &count, views.data()),
                 "Locate headset eye views");
        check_xr(xrLocateSpace(headSpace, localSpace, frame.predictedDisplayTime, &head),
                 "Locate headset pose");
        viewStateFlags = state.viewStateFlags;
        return frame.shouldRender && count == 2 &&
               (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
               (state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) && validHeadPose(head);
    }
    void acquire() {
        for (auto& eye : eyes) {
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            check_xr(xrAcquireSwapchainImage(eye.swapchain, &acquireInfo, &eye.runtimeAcquired),
                     "Acquire eye image");
            eye.acquired = useD3D11 ? 0 : eye.runtimeAcquired;
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            check_xr(xrWaitSwapchainImage(eye.swapchain, &wait), "Wait for eye image");
        }
    }
    void endFrame(bool rendered) {
        if (!frameBegun)
            return;
        std::array<XrCompositionLayerProjectionView, 2> projections{
            {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
             {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        if (rendered && useD3D11)
            copyD3D11Eyes();
        if (rendered)
            for (unsigned i = 0; i < 2; ++i) {
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                check_xr(xrReleaseSwapchainImage(eyes[i].swapchain, &release), "Release eye image");
                auto& p = projections[i];
                p.pose = views[i].pose;
                p.fov = views[i].fov;
                p.subImage.swapchain = eyes[i].swapchain;
                p.subImage.imageRect.extent = {(int32_t)eyes[i].width, (int32_t)eyes[i].height};
            }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = localSpace;
        layer.viewCount = 2;
        layer.views = projections.data();
        const XrCompositionLayerBaseHeader* layers[] = {
            reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = frame.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = rendered ? 1 : 0;
        end.layers = rendered ? layers : nullptr;
        check_xr(xrEndFrame(session, &end), "Submit VR frame");
        frameBegun = false;
        ++frameCount;
    }
    void createD3D11Interop(ID3D12Device* device, ID3D12CommandQueue* queue) {
        using Create =
            HRESULT(WINAPI*)(IUnknown*, UINT, const D3D_FEATURE_LEVEL*, UINT, IUnknown* const*,
                             UINT, UINT, ID3D11Device**, ID3D11DeviceContext**, D3D_FEATURE_LEVEL*);
        HMODULE library = LoadLibraryW(L"d3d11on12.dll");
        auto create =
            library ? reinterpret_cast<Create>(GetProcAddress(library, "D3D11On12CreateDevice"))
                    : nullptr;
        IUnknown* queues[] = {queue};
        D3D_FEATURE_LEVEL level;
        const char* shared = std::getenv("DIRTTOWN_VR_D3D11_INTEROP");
        const HRESULT on12Result = create && !(shared && std::strcmp(shared, "shared") == 0)
                                       ? create(device, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                                                0, queues, 1, 0, &device11, &context11, &level)
                                       : E_NOTIMPL;
        if (SUCCEEDED(on12Result))
            check_hr(device11.As(&on12), "Read D3D11On12 interface");
        else {
            device11.Reset();
            context11.Reset();
            ComPtr<IDXGIFactory4> factory;
            ComPtr<IDXGIAdapter> adapter;
            check_hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),
                     "Find shared-texture DXGI factory");
            check_hr(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)),
                     "Find game GPU for D3D11");
            check_hr(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, &device11, &level, &context11),
                     "Create shared-texture D3D11 fallback device");
            D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
            check_hr(device11->CreateQuery(&query, &interopQuery), "Create D3D11 completion query");
        }
        std::printf("D3D11 transfer backend: %s\n", on12 ? "D3D11On12" : "shared NT textures");
        interopQueue = queue;
        check_hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&interopFence)),
                 "Create interop fence");
        interopEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!interopEvent)
            throw std::runtime_error("Create interop fence event failed");
    }
    void createInteropTarget(ID3D12Device* device, Eye& eye) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = eye.width;
        desc.Height = eye.height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (!on12)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        check_hr(device->CreateCommittedResource(
                     &heap, on12 ? D3D12_HEAP_FLAG_NONE : D3D12_HEAP_FLAG_SHARED, &desc,
                     D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&eye.interopTarget)),
                 "Create D3D11 interop eye target");
        if (on12) {
            D3D11_RESOURCE_FLAGS flags{};
            check_hr(on12->CreateWrappedResource(
                         eye.interopTarget.Get(), &flags, D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_COMMON, IID_PPV_ARGS(&eye.wrappedTarget)),
                     "Wrap eye for D3D11 runtime");
        } else {
            HANDLE shared = nullptr;
            check_hr(device->CreateSharedHandle(eye.interopTarget.Get(), nullptr, GENERIC_ALL,
                                                nullptr, &shared),
                     "Share D3D12 eye texture");
            ComPtr<ID3D11Device1> device1;
            HRESULT result = device11.As(&device1);
            if (SUCCEEDED(result))
                result = device1->OpenSharedResource1(shared, IID_PPV_ARGS(&eye.wrappedTarget));
            CloseHandle(shared);
            check_hr(
                result,
                "Open shared D3D12 eye in D3D11. This driver/Proton may not support graphics interoperability");
        }
        eye.images.resize(1, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        eye.images[0].texture = eye.interopTarget.Get();
    }
    void copyD3D11Eyes() {
        for (auto& eye : eyes) {
            ID3D11Resource* resource = eye.wrappedTarget.Get();
            if (on12)
                on12->AcquireWrappedResources(&resource, 1);
            context11->CopyResource(eye.images11[eye.runtimeAcquired].texture, resource);
            if (on12)
                on12->ReleaseWrappedResources(&resource, 1);
        }
        if (!on12) {
            context11->End(interopQuery.Get());
            context11->Flush();
            BOOL complete = FALSE;
            const auto started = GetTickCount64();
            while (!complete) {
                const HRESULT status =
                    context11->GetData(interopQuery.Get(), &complete, sizeof(complete), 0);
                check_hr(status, "Wait for shared-texture copy");
                if (GetTickCount64() - started > 10000)
                    throw std::runtime_error("Shared-texture D3D11 copy timed out");
                if (!complete)
                    Sleep(1);
            }
            return;
        }
        context11->Flush();
        check_hr(interopQueue->Signal(interopFence.Get(), ++interopValue),
                 "Signal D3D11 eye transfer");
        if (interopFence->GetCompletedValue() < interopValue) {
            check_hr(interopFence->SetEventOnCompletion(interopValue, interopEvent),
                     "Wait for D3D11 eye transfer");
            if (WaitForSingleObject(interopEvent, 10000) != WAIT_OBJECT_0)
                throw std::runtime_error("D3D11 eye transfer timed out");
        }
    }
    ~XrBridge() {
        if (session && xrDestroySession) {
            for (auto& eye : eyes)
                if (eye.swapchain && xrDestroySwapchain)
                    xrDestroySwapchain(eye.swapchain);
            if (headSpace && xrDestroySpace)
                xrDestroySpace(headSpace);
            if (localSpace && xrDestroySpace)
                xrDestroySpace(localSpace);
            xrDestroySession(session);
        }
        if (instance && xrDestroyInstance)
            xrDestroyInstance(instance);
        if (interopEvent)
            CloseHandle(interopEvent);
        if (loader)
            FreeLibrary(loader);
    }
};

using Matrix = std::array<float, 16>;
inline Matrix identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
inline Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (unsigned c = 0; c < 4; ++c)
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned k = 0; k < 4; ++k)
                out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    return out;
}
inline Matrix viewMatrix(const XrPosef& pose) {
    auto q = pose.orientation;
    q.x = -q.x;
    q.y = -q.y;
    q.z = -q.z;
    Matrix m = identity();
    m[0] = 1 - 2 * (q.y * q.y + q.z * q.z);
    m[1] = 2 * (q.x * q.y + q.z * q.w);
    m[2] = 2 * (q.x * q.z - q.y * q.w);
    m[4] = 2 * (q.x * q.y - q.z * q.w);
    m[5] = 1 - 2 * (q.x * q.x + q.z * q.z);
    m[6] = 2 * (q.y * q.z + q.x * q.w);
    m[8] = 2 * (q.x * q.z + q.y * q.w);
    m[9] = 2 * (q.y * q.z - q.x * q.w);
    m[10] = 1 - 2 * (q.x * q.x + q.y * q.y);
    m[12] = -(m[0] * pose.position.x + m[4] * pose.position.y + m[8] * pose.position.z);
    m[13] = -(m[1] * pose.position.x + m[5] * pose.position.y + m[9] * pose.position.z);
    m[14] = -(m[2] * pose.position.x + m[6] * pose.position.y + m[10] * pose.position.z);
    return m;
}
inline Matrix projection(const XrFovf& fov, float nearZ = 0.05f, float farZ = 1000.f) {
    const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
    const float d = std::tan(fov.angleDown), u = std::tan(fov.angleUp);
    Matrix m{};
    m[0] = 2 / (r - l);
    m[5] = 2 / (u - d);
    m[8] = (r + l) / (r - l);
    m[9] = (u + d) / (u - d);
    m[10] = farZ / (nearZ - farZ);
    m[11] = -1;
    m[14] = farZ * nearZ / (nearZ - farZ);
    return m;
}
inline D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}
} // namespace dtvr
