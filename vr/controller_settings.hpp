#pragma once
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "controller_mapping.hpp"

namespace dtvr {
inline ControllerSettings readControllerSettings(const wchar_t* file) {
    ControllerSettings out;
    auto number=[&](const wchar_t* key,float fallback,float low,float high) {
        wchar_t text[64]{};GetPrivateProfileStringW(L"controls",key,L"",text,64,file);
        if(!*text)return fallback;
        wchar_t* end=nullptr;float value=std::wcstof(text,&end);
        if(end==text || *end || !std::isfinite(value) || value<low || value>high)
            throw std::runtime_error("Invalid controller deadzone/grip threshold in controls.ini");
        return value;
    };
    auto flag=[&](const wchar_t* key){return GetPrivateProfileIntW(L"controls",key,0,file)!=0;};
    out.deadzone=number(L"deadzone",.12f,0,.8f);out.gripThreshold=number(L"grip_threshold",.55f,.05f,.95f);
    out.swapHands=flag(L"swap_hands");out.leftInvertX=flag(L"invert_left_x");out.leftInvertY=flag(L"invert_left_y");
    out.rightInvertX=flag(L"invert_right_x");out.rightInvertY=flag(L"invert_right_y");
    wchar_t modifier[32]{};GetPrivateProfileStringW(L"controls",L"loader_modifier",L"auto",modifier,32,file);
    if(std::wcscmp(modifier,L"auto")==0) {
        char native[32]{};GetEnvironmentVariableA("DIRTTOWN_VR_LOADER_MODIFIER",native,32);
        out.loaderModifier=std::strcmp(native,"rs_click")==0?8:std::strcmp(native,"none")==0?15:9;
    } else if(std::wcscmp(modifier,L"lb")==0)out.loaderModifier=9;
    else if(std::wcscmp(modifier,L"rs_click")==0)out.loaderModifier=8;
    else if(std::wcscmp(modifier,L"none")==0)out.loaderModifier=15;
    else throw std::runtime_error("loader_modifier must be auto, lb, rs_click or none");
    return out;
}
} // namespace dtvr
