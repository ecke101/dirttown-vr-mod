#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include "controller_mapping.hpp"

namespace dtvr {
// SDL3 public virtual-joystick ABI. The unused optional callback pointers are
// null. Layout verified against SDL release-3.2.26 SDL_joystick.h (zlib license).
struct VirtualJoystickDesc {
    uint32_t version;
    uint16_t type,padding,vendor,product,naxes,nbuttons,nballs,nhats,ntouchpads,nsensors,padding2[2];
    uint32_t buttonMask,axisMask;
    const char* name;
    const void *touchpads,*sensors;
    void* userdata;
    void(*update)(void*);
    void* unusedCallbacks[7];
};
static_assert(sizeof(VirtualJoystickDesc)==136 && offsetof(VirtualJoystickDesc,name)==40);
class SdlVirtualPad {
    mutable std::mutex mutex;
    PadState current;
    uint64_t updated=0;
    void* joystick=nullptr;
    bool(*setAxis)(void*,int,int16_t)=nullptr;
    bool(*setButton)(void*,int,bool)=nullptr;
    template<class T> static T api(uintptr_t base,size_t slot) {
        return *reinterpret_cast<T*>(base+0x146f280+slot*8);
    }
    static void update(void* data) {static_cast<SdlVirtualPad*>(data)->apply();}
    void apply() {
        if(!joystick)return;
        const auto state=snapshot();
        for(unsigned i=0;i<state.axes.size();++i)setAxis(joystick,i,state.axes[i]);
        for(unsigned i=0;i<state.buttons.size();++i)setButton(joystick,i,state.buttons[i]);
    }
public:
    uint32_t id=0;
    void setup(uintptr_t base) {
        if(id)return;
        auto attach=api<uint32_t(*)(const VirtualJoystickDesc*)>(base,28);
        auto detach=api<bool(*)(uint32_t)>(base,151);
        auto isGamepad=api<bool(*)(uint32_t)>(base,608);
        auto open=api<void*(*)(uint32_t)>(base,665);
        auto error=api<const char*(*)()>(base,294);
        setAxis=api<bool(*)(void*,int,int16_t)>(base,825);
        setButton=api<bool(*)(void*,int,bool)>(base,827);
        VirtualJoystickDesc desc{};desc.version=sizeof(desc);desc.type=1;
        desc.naxes=6;desc.nbuttons=15;desc.axisMask=0x3f;desc.buttonMask=0x7fff;
        desc.name="Dirt Town VR controllers";desc.userdata=this;desc.update=update;
        id=attach(&desc);
        if(!id)throw std::runtime_error(std::string("Attach virtual gamepad: ")+error());
        if(!isGamepad(id)) {detach(id);id=0;throw std::runtime_error("Virtual controller was not recognized as a gamepad");}
        joystick=open(id);
        if(!joystick) {detach(id);id=0;throw std::runtime_error(std::string("Open virtual gamepad: ")+error());}
        apply();std::printf("SDL virtual gamepad attached: id=%u, six axes, fifteen buttons\n",id);
    }
    void publish(const PadState& state) {
        std::lock_guard<std::mutex> guard(mutex);current=state;updated=GetTickCount64();
    }
    void neutral() {
        std::lock_guard<std::mutex> guard(mutex);current=PadState{};updated=0;
    }
    PadState snapshot() const {
        std::lock_guard<std::mutex> guard(mutex);
        return freshControllerInput(updated,GetTickCount64())?current:PadState{};
    }
};
} // namespace dtvr
