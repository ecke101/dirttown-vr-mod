#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <memory>
#include <io.h>
#ifdef _MSC_VER
#include <intrin.h>
#define DT_CALLER _ReturnAddress()
#else
#define DT_CALLER __builtin_return_address(0)
#endif
#include "game_math.hpp"
#include "game_gpu.hpp"
#include "xr_input.hpp"
#include "sdl_virtual_pad.hpp"

// Addresses refer only to Dirt Town Demo build 26658761, verified by the launcher.
namespace {
uintptr_t base;
std::wstring outputDirectory, binaryDirectory, controlsFile;
FILE* logFile;
std::mutex logMutex;
std::atomic<unsigned> scenes{0};
thread_local unsigned sceneNumber = 0;
thread_local bool eyeActive = false;
thread_local const void* activeEyeCamera = nullptr;
thread_local unsigned primaryViews = 0, auxiliaryViews = 0;
thread_local dtvr::CameraMatrices matrices;
using Render = void (*)(void*, void*, void*, const void*, float, void*);
using Uniform = void (*)(void*, uint32_t, const void*, uint32_t);
using Resize = void (*)(void*, uint32_t, uint32_t, float);
using Projection = void* (*)(void*, const void*, float, float);
using Submit = bool (*)(void*);
using Keyboard = const bool* (*)(int*);
using Analog = float (*)(void*, uint8_t, uint8_t, uint8_t, void*);
Render renderOriginal;
Uniform vertexOriginal, fragmentOriginal;
Resize resizeOriginal;
Projection projectionOriginal;
Submit submitOriginal;
Keyboard keyboardOriginal;
Analog analogOriginal;
std::unique_ptr<dtvr::XrBridge> xr;
std::unique_ptr<dtvr::EyeTransfer> transfer;
std::unique_ptr<dtvr::XrInput> controllers;
dtvr::SdlVirtualPad virtualPad;
void* vrRenderer = nullptr;
std::atomic<void*> pendingCommand{nullptr};
bool enabled = true, anchored = false;
bool vrRequested = true;
bool controllersRequested = true;
bool debugInput = false;
float resolutionScale = 0.75f;
ULONGLONG retryAfter = 0;
XrPosef anchor{};
bool f9Down = false, f10Down = false, f11Down = false, antialiasing = true;
template <class T> T& at(void* p, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(p) + offset);
}
template <class... T> void log(const char* format, T... args) {
    std::lock_guard<std::mutex> guard(logMutex);
    if (logFile) {
        std::fprintf(logFile, format, args...);
        std::fputc('\n', logFile);
        std::fflush(logFile);
    }
}
const bool* keyboardHook(int* count) {
    int length = 0;
    const bool* physical = keyboardOriginal(&length);
    if (count)
        *count = length;
    const auto keys = virtualPad.snapshot().keys;
    if (!physical || !keys || length > 512 || length < 0)
        return physical;
    thread_local std::array<bool, 512> combined;
    std::copy(physical, physical + length, combined.begin());
    for (unsigned i = 0; i < 64 && i < (unsigned)length; ++i)
        if (keys & (uint64_t(1) << i))
            combined[i] = true;
    return combined.data();
}
float analogHook(void* input, uint8_t channel, uint8_t device, uint8_t context, void* source) {
    const float native = analogOriginal(input, channel, device, context, source);
    if (device != 1 || channel < 4 || channel > 7)
        return native;
    const auto state = virtualPad.snapshot();
    const float result = dtvr::hydraulicControl(state, channel, device, native);
    const bool loader = state.directLoader && (channel == 4 || channel == 5);
    const bool dozer = state.directDozer && (channel == 6 || channel == 7);
    if (debugInput && (loader || dozer)) {
        thread_local ULONGLONG previous = 0;
        const auto now = GetTickCount64();
        const float axis = loader ? state.loaderAxes[channel - 4] : state.dozerAxes[channel - 6];
        if (now - previous >= 1000 && axis != 0) {
            log("VR %s hydraulic: channel=%u context=%u native=%.3f routed=%.3f",
                loader ? "loader" : "dozer", channel, context, native, result);
            previous = now;
        }
    }
    return result;
}
struct ControlContext {
    bool seated = false, dozer = false, loader = false, gradeHold = false, tiltLocked = false;
};
ControlContext controlContext(void* world) {
    ControlContext context;
    // Same checked entity lookup used by the native dozer grade-hold action.
    const uint64_t entity = at<uint64_t>(world, 0xeaa0);
    if ((entity & 0xffffffff80000000ULL) != 0x200000000ULL)
        return context;
    auto* begin = at<void**>(world, 0xe8c0);
    auto* end = at<void**>(world, 0xe8c8);
    const uint32_t index = (uint32_t)entity;
    const uintptr_t first = (uintptr_t)begin, last = (uintptr_t)end;
    if (!first || last < first || index >= (last - first) / sizeof(void*))
        return context;
    void* machine = begin[index];
    if (!machine)
        return context;
    // Walking avatars also live in the machine array. The native on-foot
    // predicate searches the walker collection at World+0xc7a8.
    auto onFoot = reinterpret_cast<bool (*)(void*)>(base + 0x939c70);
    context.seated = !onFoot(world);
    auto findLoader = reinterpret_cast<void* (*)(void*, void*)>(base + 0x9393b0);
    if (at<void*>(world, 0xc618) && at<void*>(world, 0xc620))
        context.loader = findLoader(world, machine) != nullptr;
    // Native read-only search of the dozer collection, not a guessed type tag.
    if (!at<void*>(world, 0xc748))
        return context;
    auto findDozer = reinterpret_cast<void* (*)(void*, void*)>(base + 0x939790);
    if (void* dozer = findDozer(world, machine)) {
        context.dozer = true;
        context.gradeHold = at<uint8_t>(dozer, 0xe10) != 0;
        context.tiltLocked = at<uint8_t>(dozer, 0xe19) != 0;
    }
    return context;
}
void updateControllers(bool active, void* world) {
    if (!active || !controllers) {
        virtualPad.neutral();
        return;
    }
    try {
        auto input = controllers->sample();
        const auto context = controlContext(world);
        input.seated = context.seated;
        input.dozer = context.dozer;
        input.loader = context.loader;
        const auto pad = dtvr::mapControllers(input, controllers->settings);
        virtualPad.publish(pad);
        static ULONGLONG last = 0;
        static uint32_t previousButtons = UINT32_MAX;
        static uint64_t previousKeys = UINT64_MAX;
        static unsigned previousContext = UINT_MAX;
        const unsigned contextBits = context.seated | (context.dozer << 1) |
                                     (context.gradeHold << 2) | (context.tiltLocked << 3) |
                                     (context.loader << 4);
        const auto now = GetTickCount64();
        if (contextBits != previousContext)
            log("Control context: seated=%d dozer=%d loader=%d grade-hold=%d", context.seated,
                context.dozer, context.loader, context.gradeHold);
        uint32_t buttons = 0;
        for (unsigned i = 0; i < pad.buttons.size(); ++i)
            if (pad.buttons[i])
                buttons |= 1u << i;
        if (debugInput && (now - last >= 1000 || buttons != previousButtons ||
                           pad.keys != previousKeys || contextBits != previousContext)) {
            log("VR controls: focused=%d active=%d/%d axes=%d,%d,%d,%d,%d,%d buttons=%04x keys=%llx grip=%.2f/%.2f seated=%d dozer=%d grade-hold=%d tilt-lock=%d",
                input.focused, input.hands[0].active, input.hands[1].active, pad.axes[0],
                pad.axes[1], pad.axes[2], pad.axes[3], pad.axes[4], pad.axes[5], buttons,
                (unsigned long long)pad.keys, input.hands[0].grip, input.hands[1].grip,
                context.seated, context.dozer, context.gradeHold, context.tiltLocked);
            last = now;
            previousButtons = buttons;
            previousKeys = pad.keys;
            previousContext = contextBits;
        }
        previousContext = contextBits;
    } catch (const std::exception& e) {
        virtualPad.neutral();
        log("VR controller input error: %s", e.what());
    }
}
const void* adjustView(uint32_t slot, const void* data, uint32_t length,
                       std::array<unsigned char, 1744>& copy) {
    if (!eyeActive || slot != 0 || length != 1744)
        return data;
    const void* adjusted = dtvr::patchEyeView(matrices, slot, data, length, copy);
    if (adjusted == data)
        ++auxiliaryViews;
    else
        ++primaryViews;
    return adjusted;
}
void vertexHook(void* cmd, uint32_t slot, const void* data, uint32_t length) {
    std::array<unsigned char, 1744> copy;
    vertexOriginal(cmd, slot, adjustView(slot, data, length, copy), length);
}
void fragmentHook(void* cmd, uint32_t slot, const void* data, uint32_t length) {
    std::array<unsigned char, 1744> copy;
    fragmentOriginal(cmd, slot, adjustView(slot, data, length, copy), length);
}
void* projectionHook(void* out, const void* camera, float aspect, float farZ) {
    if (!eyeActive || camera != activeEyeCamera)
        return projectionOriginal(out, camera, aspect, farZ);
    std::memcpy(out, matrices.viewProjection.data(), 64);
    return out;
}
void resizeHook(void* renderer, uint32_t width, uint32_t height, float scale) {
    if (renderer == vrRenderer && xr && xr->session && enabled) {
        width = xr->renderWidth;
        height = xr->renderHeight;
        scale = 1;
    }
    resizeOriginal(renderer, width, height, scale);
}
bool submitHook(void* cmd) {
    bool result = submitOriginal(cmd);
    if (pendingCommand.load() == cmd) {
        pendingCommand.store(nullptr);
        try {
            if (!result)
                throw std::runtime_error("SDL rejected stereo command buffer");
            transfer->submit(*xr);
            xr->endFrame(true);
            static ULONGLONG started = 0, lastSubmit = 0;
            static unsigned count = 0;
            const auto now = GetTickCount64();
            if (!started || now - lastSubmit > 1000) {
                started = now;
                count = 0;
            }
            lastSubmit = now;
            ++count;
            if (count >= 120) {
                log("VR submitted frame %llu; measured %.2f FPS; display period %.2f ms; TAA=%d",
                    (unsigned long long)xr->frameCount,
                    count * 1000. / std::max<ULONGLONG>(1, now - started),
                    xr->frame.predictedDisplayPeriod / 1000000., antialiasing);
                count = 0;
                started = now;
            }
        } catch (const std::exception& e) {
            virtualPad.neutral();
            log("VR submission error: %s", e.what());
            enabled = false;
            // A failed submission can leave acquired images, an open XR frame or GPU
            // work in flight. Keep those resources alive and stop using this session.
            vrRequested = false;
            log("VR disabled after submission failure; restart the game to reconnect.");
        }
    }
    return result;
}
void renderStereo(void* renderer, void* cmd, void* world, const void* camera, float time,
                  void* extra) {
    using namespace dtvr;
    if (GetTickCount64() < retryAfter) {
        renderOriginal(renderer, cmd, world, camera, time, extra);
        return;
    }
    if (!xr) {
        xr = std::make_unique<XrBridge>();
        xr->prepare((binaryDirectory + L"openxr_loader.dll").c_str());
    }
    if (!xr->session) {
        void* backend = at<void*>(cmd, 0x110);
        auto* queue = at<ID3D12CommandQueue*>(backend, 0x90);
        ComPtr<ID3D12Device> device;
        check_hr(queue->GetDevice(IID_PPV_ARGS(&device)), "Get game device for VR");
        log("D3D12 native handles: backend=%p queue=%p device=%p commands=%p", backend, queue,
            device.Get(), at<void*>(cmd, 0x120));
        xr->bind(device.Get(), queue, resolutionScale);
        transfer = std::make_unique<EyeTransfer>();
        transfer->setup(queue, *xr);
        if (controllersRequested)
            try {
                controllers = std::make_unique<XrInput>();
                controllers->setup(*xr, controlsFile.c_str());
                virtualPad.setup(base);
            } catch (const std::exception& e) {
                controllers.reset();
                virtualPad.neutral();
                log("VR controller setup error: %s", e.what());
            }
    }
    const bool recenter = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    const bool toggle = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    const bool toggleAA = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (recenter && !f9Down)
        anchored = false;
    if (toggle && !f10Down) {
        enabled = !enabled;
        transfer->resetHistory();
        log("VR enabled: %d", enabled);
    }
    if (toggleAA && !f11Down) {
        antialiasing = !antialiasing;
        log("Temporal antialiasing enabled: %d", antialiasing);
    }
    transfer->setAntialiasing(antialiasing);
    f9Down = recenter;
    f10Down = toggle;
    f11Down = toggleAA;
    bool draw = xr->beginFrame();
    updateControllers(enabled && draw, world);
    if (xr->quit)
        throw std::runtime_error("Headset session ended; reconnecting");
    if (!enabled || !draw) {
        xr->endFrame(false);
        renderOriginal(renderer, cmd, world, camera, time, extra);
        return;
    }
    if (!anchored) {
        anchor = xr->head.pose;
        anchored = true;
        transfer->resetHistory();
        log("Recentered headset at game camera");
    }
    if (vrRenderer != renderer || at<uint32_t>(renderer, 0x108) != xr->renderWidth ||
        at<uint32_t>(renderer, 0x10c) != xr->renderHeight) {
        vrRenderer = renderer;
        resizeOriginal(renderer, xr->renderWidth, xr->renderHeight, 1);
    }
    xr->acquire();
    const auto& source = *static_cast<const GameCamera*>(camera);
    const auto gameRotation = gameOrientation(source.yaw, source.pitch);
    const auto worldFromTracking = product(gameRotation, conjugate(anchor.orientation));
    for (unsigned i = 0; i < 2; ++i) {
        GameCamera eyeCamera = source;
        auto p = xr->views[i].pose.position;
        auto offset = rotate(worldFromTracking, {p.x - anchor.position.x, p.y - anchor.position.y,
                                                 p.z - anchor.position.z});
        eyeCamera.position[0] += offset.x;
        eyeCamera.position[1] += offset.y;
        eyeCamera.position[2] += offset.z;
        auto orientation = product(worldFromTracking, xr->views[i].pose.orientation);
        auto forward = rotate(orientation, {0, 0, -1});
        eyeCamera.yaw = std::atan2(forward.x, -forward.z);
        eyeCamera.pitch = std::asin(std::clamp(forward.y, -1.f, 1.f));
        eyeCamera.fov =
            cullingFov(orientation, xr->views[i].fov, (float)xr->renderWidth / xr->renderHeight);
        matrices = cameraMatrices(orientation, xr->views[i].fov, source.nearZ, source.farZ);
        TemporalView temporalView;
        temporalView.unjitteredVP = matrices.viewProjection;
        for (unsigned c = 0; c < 3; ++c)
            temporalView.origin[c] = eyeCamera.position[c];
        temporalView.frame = xr->frameCount;
        if (antialiasing) {
            const unsigned sample = (unsigned)(xr->frameCount % 8) + 1;
            temporalView.jitterX = halton(sample, 2) - .5f;
            temporalView.jitterY = halton(sample, 3) - .5f;
            jitterCamera(matrices, temporalView.jitterX, temporalView.jitterY, xr->renderWidth,
                         xr->renderHeight);
        }
        temporalView.inverseJitteredVP = matrices.inverseViewProjection;
        transfer->setView(i, temporalView);
        eyeActive = true;
        activeEyeCamera = &eyeCamera;
        primaryViews = auxiliaryViews = 0;
        // The desktop renderer normally records postprocessing on a second list.
        // Keep each eye's geometry and postprocessing together before capturing it.
        renderOriginal(renderer, cmd, world, &eyeCamera, time, cmd);
        eyeActive = false;
        activeEyeCamera = nullptr;
        if (xr->frameCount < 4 || xr->frameCount % 300 == 1) {
            auto* first = at<unsigned char*>(renderer, 0x8e8);
            auto* end = at<unsigned char*>(renderer, 0x8f0);
            const size_t mirrors =
                (reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(first)) / 0x150;
            log("VR View routing: eye=%u primary=%u auxiliary-preserved=%u mirrors=%zu position=(%.5f %.5f %.5f)",
                i, primaryViews, auxiliaryViews, mirrors, eyeCamera.position[0],
                eyeCamera.position[1], eyeCamera.position[2]);
        }
        void* container = at<void*>(renderer, 0x100);

        void* texture = at<void*>(container, 0x28);
        auto* resource = at<ID3D12Resource*>(texture, 0x20);
        auto* list = at<ID3D12GraphicsCommandList*>(cmd, 0x120);
        const auto usage = at<uint32_t>(container, 8);
        const auto shaderResourceState =
            static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        auto state = (usage & 1) ? shaderResourceState : D3D12_RESOURCE_STATE_RENDER_TARGET;
        transfer->capture(i, list, resource, state);
        void* normalContainer = at<void*>(renderer, 0x9f0);
        auto* normals = at<ID3D12Resource*>(at<void*>(normalContainer, 0x28), 0x20);
        const auto normalUsage = at<uint32_t>(normalContainer, 8);
        auto normalState =
            (normalUsage & 1) ? shaderResourceState : D3D12_RESOURCE_STATE_RENDER_TARGET;
        transfer->captureDepth(i, list, normals, normalState);
    }
    pendingCommand.store(cmd);
}
void renderHook(void* renderer, void* cmd, void* world, const void* camera, float time,
                void* extra) {
    sceneNumber = ++scenes;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(DT_CALLER) - base;
    // Play-mode view, excluding the animated main menu/site browser cameras.
    if (vrRequested && (caller == 0x244d66 || caller == 0x2ea0bb)) {
        try {
            renderStereo(renderer, cmd, world, camera, time, extra);
        } catch (const std::exception& e) {
            eyeActive = false;
            activeEyeCamera = nullptr;
            virtualPad.neutral();
            log("VR rendering error: %s", e.what());
            // Startup or a disconnected session can recover on a fresh instance.
            // A partial recorded frame is left allocated until process exit.
            if (xr && xr->frameBegun) {
                xr.release();
                transfer.release();
                vrRequested = false;
            } else {
                controllers.reset();
                transfer.reset();
                xr.reset();
                retryAfter = GetTickCount64() + 2000;
            }
            vrRenderer = nullptr;
            anchored = false;
            renderOriginal(renderer, cmd, world, camera, time, extra);
        }
    } else {
        virtualPad.neutral();
        renderOriginal(renderer, cmd, world, camera, time, extra);
    }
    sceneNumber = 0;
}
bool install(uintptr_t rva, void* hook, void** original) {
    auto status = MH_CreateHook(reinterpret_cast<void*>(base + rva), hook, original);
    log("Hook RVA %llx: %s", (unsigned long long)rva, MH_StatusToString(status));
    return status == MH_OK;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI Initialize(LPVOID modulePointer) {
    auto module = static_cast<HMODULE>(modulePointer);
    wchar_t filename[32768];
    GetModuleFileNameW(module, filename, 32768);
    binaryDirectory = filename;
    binaryDirectory.resize(binaryDirectory.find_last_of(L"\\/") + 1);
    wchar_t directory[32768]{};
    const DWORD dirLength = GetEnvironmentVariableW(L"DIRTTOWN_VR_LOG_DIR", directory, 32768);
    if (dirLength >= 32768)
        return 1;
    outputDirectory = dirLength ? directory : binaryDirectory;
    if (outputDirectory.back() != L'\\' && outputDirectory.back() != L'/')
        outputDirectory += L'\\';
    wchar_t controls[32768]{};
    const DWORD controlsLength = GetEnvironmentVariableW(L"DIRTTOWN_VR_CONTROLS", controls, 32768);
    if (controlsLength >= 32768)
        return 1;
    controlsFile = controlsLength ? controls : (binaryDirectory + L"controls.ini");
    logFile = _wfopen((outputDirectory + L"inject.log").c_str(), L"w");
    base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    log("Dirt Town experimental renderer hook; base=%p", reinterpret_cast<void*>(base));
    char scaleText[64];
    const DWORD scaleLength =
        GetEnvironmentVariableA("DIRTTOWN_VR_RESOLUTION_SCALE", scaleText, sizeof(scaleText));
    if (scaleLength) {
        char* end = nullptr;
        if (scaleLength >= sizeof(scaleText))
            return 1;
        const float scale = std::strtof(scaleText, &end);
        if (end == scaleText || *end || !std::isfinite(scale) || scale < .25f || scale > 1.5f) {
            log("Invalid VR resolution scale: %s", scaleText);
            return 1;
        }
        resolutionScale = scale;
    }
    log("Per-eye resolution scale: %.3f of recommended width/height", resolutionScale);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage != 0x1e4d000) {
        log("Unexpected image size: %lx. Hooks not installed.", nt->OptionalHeader.SizeOfImage);
        return 1;
    }
    if (MH_Initialize() != MH_OK)
        return 1;
    try {
        dtvr::verifyCameraMath();
        dtvr::verifyControllerMapping();
        log("Camera/mirror math and controller mapping self-checks: passed");
    } catch (const std::exception& e) {
        log("%s", e.what());
        return 1;
    }
    char setting[16]{};
    GetEnvironmentVariableA("DIRTTOWN_VR_TAA", setting, sizeof(setting));
    antialiasing = std::strcmp(setting, "0") != 0;
    debugInput = GetEnvironmentVariableA("DIRTTOWN_VR_DEBUG_INPUT", setting, sizeof(setting)) > 0 &&
                 std::strcmp(setting, "1") == 0;
    bool ok = install(0x64ca00, reinterpret_cast<void*>(renderHook),
                      reinterpret_cast<void**>(&renderOriginal));
    ok = install(0xd56660, reinterpret_cast<void*>(vertexHook),
                 reinterpret_cast<void**>(&vertexOriginal)) &&
         ok;
    ok = install(0xd56720, reinterpret_cast<void*>(fragmentHook),
                 reinterpret_cast<void**>(&fragmentOriginal)) &&
         ok;
    ok = install(0x625620, reinterpret_cast<void*>(projectionHook),
                 reinterpret_cast<void**>(&projectionOriginal)) &&
         ok;
    ok = install(0x626430, reinterpret_cast<void*>(resizeHook),
                 reinterpret_cast<void**>(&resizeOriginal)) &&
         ok;
    // Backend submission also catches SDL's submit-and-acquire-fence path.
    ok = install(0xe8e140, reinterpret_cast<void*>(submitHook),
                 reinterpret_cast<void**>(&submitOriginal)) &&
         ok;
    ok = install(0xd3d700, reinterpret_cast<void*>(keyboardHook),
                 reinterpret_cast<void**>(&keyboardOriginal)) &&
         ok;
    ok = install(0x299cc0, reinterpret_cast<void*>(analogHook),
                 reinterpret_cast<void**>(&analogOriginal)) &&
         ok;
    if (!ok)
        return 1;
    vrRequested = GetEnvironmentVariableW(L"DIRTTOWN_VR_DIAGNOSTIC", nullptr, 0) == 0;
    char controllerSetting[8]{};
    GetEnvironmentVariableA("DIRTTOWN_VR_CONTROLLERS", controllerSetting,
                            sizeof(controllerSetting));
    controllersRequested = std::strcmp(controllerSetting, "0") != 0;
    if (vrRequested) {
        _wfreopen((outputDirectory + L"vr-runtime.log").c_str(), L"w", stdout);
        _dup2(_fileno(stdout), _fileno(stderr));
        setvbuf(stdout, nullptr, _IONBF, 0);
        log("OpenXR will connect when the playable scene loads; automatic reconnect enabled.");
    }
    auto result = MH_EnableHook(MH_ALL_HOOKS);
    log("Enable hooks: %s", MH_StatusToString(result));
    return result == MH_OK ? 0 : 1;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}
