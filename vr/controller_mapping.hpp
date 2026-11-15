#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace dtvr {
struct HandInput {
    float x=0,y=0,trigger=0,grip=0;
    bool active=false,primary=false,secondary=false,click=false,menu=false;
};
struct ControllerInput {
    std::array<HandInput,2> hands;
    bool focused=false,seated=false,dozer=false,loader=false;
};
struct PadState {
    std::array<int16_t,6> axes{{0,0,0,0,-32768,-32768}};
    std::array<bool,15> buttons{};
    uint64_t keys=0;
    std::array<float,2> loaderAxes{};
    std::array<float,2> dozerAxes{};
    bool directLoader=false;
    bool directDozer=false;
};
struct ControllerSettings {
    float deadzone=.12f,gripThreshold=.55f;
    bool swapHands=false,leftInvertX=false,leftInvertY=false,rightInvertX=false,rightInvertY=false;
};
inline float cleanAxis(float value) {return std::isfinite(value)?std::clamp(value,-1.f,1.f):0.f;}
inline int16_t stickAxis(float value,float deadzone=.12f) {
    value=cleanAxis(value);
    deadzone=std::isfinite(deadzone)?std::clamp(deadzone,0.f,.8f):.12f;
    if(std::abs(value)<=deadzone)return 0;
    return (int16_t)std::lround(std::copysign((std::abs(value)-deadzone)/(1-deadzone),value)*32767);
}
inline float triggerValue(float value) {return std::isfinite(value)?std::clamp(value,0.f,1.f):0.f;}
inline int16_t triggerAxis(float value) {return (int16_t)std::lround(triggerValue(value)*65535-32768);}
inline PadState mapControllers(const ControllerInput& input,const ControllerSettings& settings={}) {
    PadState out;
    if(!input.focused)return out;
    const auto& l=input.hands[settings.swapHands?1:0];const auto& r=input.hands[settings.swapHands?0:1];
    auto left=l.active?l:HandInput{};auto right=r.active?r:HandInput{};
    if(settings.leftInvertX)left.x=-left.x;if(settings.leftInvertY)left.y=-left.y;
    if(settings.rightInvertX)right.x=-right.x;if(settings.rightInvertY)right.y=-right.y;
    const bool lb=cleanAxis(left.grip)>settings.gripThreshold,rb=cleanAxis(right.grip)>settings.gripThreshold;
    out.axes={stickAxis(left.x,settings.deadzone),stickAxis(-left.y,settings.deadzone),
        stickAxis(right.x,settings.deadzone),stickAxis(-right.y,settings.deadzone),
        triggerAxis(left.trigger),triggerAxis(right.trigger)};
    out.buttons[0]=right.primary;out.buttons[1]=right.secondary;
    out.buttons[2]=left.primary;out.buttons[6]=left.menu;
    out.buttons[8]=right.click;out.buttons[9]=lb;out.buttons[10]=rb;
    // A dozer's native right-stick implement axes require LB. Select that
    // layer automatically, leaving the left drive stick completely separate.
    // Do not force LB in other machines: it reverses an excavator's left track.
    if(input.dozer && right.active)out.buttons[9]=true;
    // Dozer triggers angle the blade, or pitch it while holding the left grip.
    // Route analog implement input directly so triggers cannot also drive it.
    if(input.dozer && (left.active || right.active)) {
        out.directDozer=true;
        out.dozerAxes[lb?1:0]=stickAxis(triggerValue(right.trigger)-triggerValue(left.trigger),settings.deadzone)/32767.f;
        out.axes[4]=out.axes[5]=-32768;
    }
    // Feed the loader's native hydraulic channels separately. Exposing these
    // axes as RS would also feed camera look and custom click-modifier bindings.
    if(input.loader && right.active) {
        out.directLoader=true;
        out.loaderAxes={out.axes[3]/32767.f,-out.axes[2]/32767.f};
        out.axes[2]=out.axes[3]=0;
    }
    // Hold the left stick click for the native d-pad blade angle/pitch controls.
    if(left.click && input.dozer) {
        out.axes[2]=out.axes[3]=0;
        out.buttons[11]=right.y>.5f;out.buttons[12]=right.y<-.5f;
        out.buttons[13]=right.x<-.5f;out.buttons[14]=right.x>.5f;
    }
    // These game actions have no default gamepad binding. Merge only their
    // keyboard-state bits inside this game process; never send OS key events.
    if(left.secondary)out.keys|=uint64_t(1)<<(lb?28:9); // grip+Y: tailgate Y; Y: enter/exit F
    if(rb && right.secondary) {
        out.keys|=uint64_t(1)<<11; // grip+B: free tilt / lift axle H
        out.buttons[1]=out.buttons[10]=false;
    }
    if(left.click && right.click) {
        out.buttons[6]=true;out.buttons[8]=false;
        for(unsigned i=11;i<15;++i)out.buttons[i]=false;
        out.axes[2]=out.axes[3]=0;
        out.loaderAxes={};
        out.dozerAxes={};
    }
    return out;
}
inline float hydraulicControl(const PadState& state,unsigned channel,unsigned device,float native) {
    if(device!=1)return native;
    if(state.directLoader && (channel==4 || channel==5))
        return std::clamp(native+state.loaderAxes[channel-4],-1.f,1.f);
    if(state.directDozer && (channel==6 || channel==7))
        return std::clamp(native+state.dozerAxes[channel-6],-1.f,1.f);
    return native;
}
inline bool freshControllerInput(uint64_t updated,uint64_t now) {
    return updated && now>=updated && now-updated<=250;
}
inline void verifyControllerMapping() {
    auto require=[](bool ok) {if(!ok)throw std::runtime_error("VR controller mapping self-check failed");};
    ControllerInput input;input.focused=true;input.seated=true;
    for(auto& hand:input.hands)hand.active=true;
    input.hands[0].y=1;input.hands[1].trigger=1;
    auto pad=mapControllers(input);
    require(pad.axes[1]==-32767 && pad.axes[4]==-32768 && pad.axes[5]==32767);
    input.dozer=true;pad=mapControllers(input);
    require(pad.buttons[9] && pad.axes[2]==0 && pad.axes[3]==0);
    input.hands[0].y=-1;pad=mapControllers(input);
    require(pad.axes[1]==32767 && pad.axes[2]==0 && pad.axes[3]==0);
    input.hands[0].y=0;input.hands[1].x=1;input.hands[1].y=1;pad=mapControllers(input);
    require(pad.buttons[9] && pad.axes[0]==0 && pad.axes[1]==0 && pad.axes[2]==32767 && pad.axes[3]==-32767);
    input.dozer=false;require(!mapControllers(input).buttons[9]);
    input.seated=false;pad=mapControllers(input);
    require(pad.axes[2]==32767 && pad.axes[3]==-32767 && !pad.buttons[9]);
    input.seated=input.loader=true;pad=mapControllers(input);
    require(pad.directLoader && pad.axes[2]==0 && pad.axes[3]==0 && !pad.buttons[8] && !pad.buttons[9]);
    require(hydraulicControl(pad,4,1,0)==-1 && hydraulicControl(pad,5,1,0)==-1);
    require(hydraulicControl(pad,4,0,.25f)==.25f && hydraulicControl(pad,4,2,.25f)==.25f);
    require(hydraulicControl(pad,6,1,.25f)==.25f && hydraulicControl(PadState{},4,1,.25f)==.25f);
    input.hands[1].x=.56f;pad=mapControllers(input);
    require(std::abs(hydraulicControl(pad,5,1,0)+.5f)<.0001f);
    input.loader=false;
    input.seated=true;input.hands[1].x=input.hands[1].y=0;
    input.loader=true;pad=mapControllers(input);
    require(!pad.buttons[8] && !pad.buttons[9] && pad.loaderAxes==std::array<float,2>{});input.loader=false;
    input.hands[0].secondary=true;require(mapControllers(input).keys==(uint64_t(1)<<9));
    input.hands[0].grip=1;require(mapControllers(input).keys==(uint64_t(1)<<28));
    input.hands[1].grip=1;input.hands[1].secondary=true;pad=mapControllers(input);
    require((pad.keys&(uint64_t(1)<<11)) && !pad.buttons[1] && !pad.buttons[10]);
    input.dozer=true;input.hands[0].click=true;input.hands[1].x=1;pad=mapControllers(input);
    require(pad.buttons[14] && pad.axes[2]==0 && pad.axes[3]==0);
    input.dozer=true;input.hands[0].active=false;input.hands[1].active=false;pad=mapControllers(input);
    require(pad.keys==0 && pad.axes==PadState{}.axes && pad.buttons==PadState{}.buttons);
    input.focused=false;input.hands[0].active=true;input.hands[0].secondary=true;
    require(mapControllers(input).keys==0);
    require(freshControllerInput(100,350) && !freshControllerInput(100,351) && !freshControllerInput(0,1));
    require(stickAxis(.05f)==0 && triggerAxis(NAN)==-32768 && stickAxis(INFINITY)==0);
    input=ControllerInput{};input.focused=input.seated=true;input.hands[0].active=true;input.hands[0].x=1;
    ControllerSettings settings;settings.swapHands=true;settings.rightInvertX=true;
    pad=mapControllers(input,settings);require(pad.axes[0]==0 && pad.axes[2]==-32767);
    input.hands[1].active=true;input.hands[0].click=input.hands[1].click=true;
    pad=mapControllers(input);require(pad.buttons[6] && !pad.buttons[8] && pad.axes[2]==0);
    input.loader=true;input.hands[1].y=1;pad=mapControllers(input);
    require(pad.buttons[6] && pad.loaderAxes==std::array<float,2>{});

    input=ControllerInput{};input.focused=input.seated=input.dozer=true;
    for(auto& hand:input.hands)hand.active=true;
    input.hands[0].trigger=1;input.hands[0].y=1;input.hands[1].x=1;
    pad=mapControllers(input);
    require(pad.directDozer && pad.dozerAxes[0]==-1 && pad.dozerAxes[1]==0);
    require(pad.axes[1]==-32767 && pad.axes[2]==32767 && pad.axes[4]==-32768 && pad.axes[5]==-32768);
    require(hydraulicControl(pad,6,1,0)==-1 && hydraulicControl(pad,7,1,0)==0);
    require(hydraulicControl(pad,6,0,.25f)==.25f && hydraulicControl(pad,6,2,.25f)==.25f);
    require(hydraulicControl(pad,4,1,.25f)==.25f && hydraulicControl(PadState{},6,1,.25f)==.25f);
    input.hands[0].trigger=0;input.hands[1].trigger=.56f;pad=mapControllers(input);
    require(std::abs(hydraulicControl(pad,6,1,0)-.5f)<.0001f);
    input.hands[1].trigger=1;pad=mapControllers(input);
    require(hydraulicControl(pad,6,1,.5f)==1);
    input.hands[0].grip=1;pad=mapControllers(input);
    require(pad.dozerAxes[0]==0 && hydraulicControl(pad,7,1,0)==1);
    input.hands[0].trigger=1;input.hands[1].trigger=0;pad=mapControllers(input);
    require(pad.dozerAxes[0]==0 && hydraulicControl(pad,7,1,0)==-1);
    input.hands[1].trigger=1;pad=mapControllers(input);
    require(pad.dozerAxes==std::array<float,2>{}); // opposing triggers cancel
    input.hands[0].trigger=NAN;input.hands[1].trigger=INFINITY;
    require(mapControllers(input).dozerAxes==std::array<float,2>{});
    input.hands[1].trigger=1;input.hands[1].active=false;
    require(mapControllers(input).dozerAxes==std::array<float,2>{});
    input.hands[1].active=true;input.focused=false;pad=mapControllers(input);
    require(!pad.directDozer && pad.dozerAxes==std::array<float,2>{} && pad.axes==PadState{}.axes);
    input.focused=true;input.hands[0].click=input.hands[1].click=true;pad=mapControllers(input);
    require(pad.buttons[6] && pad.dozerAxes==std::array<float,2>{});
    input.hands[0].click=input.hands[1].click=false;input.dozer=false;pad=mapControllers(input);
    require(!pad.directDozer && pad.axes[5]==32767 && hydraulicControl(pad,7,1,.25f)==.25f);
    input.dozer=true;settings=ControllerSettings{};settings.swapHands=true;
    pad=mapControllers(input,settings);
    require(hydraulicControl(pad,6,1,0)==-1 && pad.dozerAxes[1]==0);
}
} // namespace dtvr
