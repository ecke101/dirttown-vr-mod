#include "core.hpp"
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace launcher {
class ScopedRuntime {
    Environment old=environment();
    const std::vector<std::string> names={"XR_RUNTIME_JSON","XDG_CONFIG_HOME","XDG_CONFIG_DIRS"};
    void apply(const Environment& vars) {
        for(const auto& name:names) {
            const auto it=vars.find(name);
#ifdef _WIN32
            SetEnvironmentVariableW(wide(name).c_str(),it==vars.end()?nullptr:wide(it->second).c_str());
#else
            if(it==vars.end())unsetenv(name.c_str());else setenv(name.c_str(),it->second.c_str(),1);
#endif
        }
    }
public:
    explicit ScopedRuntime(const Environment& vars) {apply(vars);}
    ~ScopedRuntime() {apply(old);}
};
class Loader {
#ifdef _WIN32
    HMODULE module=nullptr;
#else
    void* module=nullptr;
#endif
public:
    explicit Loader(const Paths& dirs) {
#ifdef _WIN32
        module=LoadLibraryW((dirs.binaries/"openxr_loader.dll").c_str());
#else
        (void)dirs;module=dlopen("libopenxr_loader.so.1",RTLD_NOW|RTLD_LOCAL);
#endif
        if(!module)throw std::runtime_error("OpenXR loader unavailable. Install a PC OpenXR runtime and loader first.");
    }
    ~Loader() {
#ifdef _WIN32
        FreeLibrary(module);
#else
        dlclose(module);
#endif
    }
    template<class T>T get(const char* name) {
#ifdef _WIN32
        auto p=GetProcAddress(module,name);
#else
        auto p=dlsym(module,name);
#endif
        if(!p)throw std::runtime_error(std::string("OpenXR function missing: ")+name);return reinterpret_cast<T>(p);
    }
};
Json inspectRuntime(const Paths& dirs,const Environment& vars) {
    ScopedRuntime selected(vars);Loader loader(dirs);
    auto create=loader.get<PFN_xrCreateInstance>("xrCreateInstance");auto destroy=loader.get<PFN_xrDestroyInstance>("xrDestroyInstance");
    auto getInstance=loader.get<PFN_xrGetInstanceProperties>("xrGetInstanceProperties");auto getSystem=loader.get<PFN_xrGetSystem>("xrGetSystem");
    auto getProperties=loader.get<PFN_xrGetSystemProperties>("xrGetSystemProperties");auto getViews=loader.get<PFN_xrEnumerateViewConfigurationViews>("xrEnumerateViewConfigurationViews");
    auto check=[](XrResult result,const char* action) {if(XR_FAILED(result))throw std::runtime_error(std::string(action)+" (OpenXR "+std::to_string(result)+"). Start the runtime and connect/wake your headset.");};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};std::strcpy(info.applicationInfo.applicationName,"Dirt Town VR readiness");
    std::strcpy(info.applicationInfo.engineName,"DirtTownVRMod");info.applicationInfo.applicationVersion=2;info.applicationInfo.engineVersion=2;info.applicationInfo.apiVersion=XR_API_VERSION_1_0;
    XrInstance instance=XR_NULL_HANDLE;check(create(&info,&instance),"Cannot connect to OpenXR");
    try {
        XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};check(getInstance(instance,&runtime),"Cannot read runtime");
        XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XrSystemId system;
        check(getSystem(instance,&si,&system),"No ready VR headset");
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};check(getProperties(instance,system,&properties),"Cannot read headset");
        uint32_t count=0;check(getViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"Cannot read stereo views");
        if(count!=2)throw std::runtime_error("The runtime does not provide two-eye stereo");
        XrViewConfigurationView views[2]={{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        check(getViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views),"Cannot read eye resolution");
        Json result={{"runtime",runtime.runtimeName},{"headset",properties.systemName},
            {"eyes",{{views[0].recommendedImageRectWidth,views[0].recommendedImageRectHeight},{views[1].recommendedImageRectWidth,views[1].recommendedImageRectHeight}}},
            {"orientation_tracking",properties.trackingProperties.orientationTracking!=0},{"position_tracking",properties.trackingProperties.positionTracking!=0}};
        destroy(instance);return result;
    } catch(...) {destroy(instance);throw;}
}
} // namespace launcher
