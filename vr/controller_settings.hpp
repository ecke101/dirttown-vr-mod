#pragma once
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "controller_mapping.hpp"

namespace dtvr {
inline ControllerSettings readControllerSettings(const wchar_t* file) {
    ControllerSettings out;
    auto number = [&](const wchar_t* key, float fallback, float low, float high) {
        wchar_t text[64]{};
        GetPrivateProfileStringW(L"controls", key, L"", text, 64, file);
        if (!*text)
            return fallback;
        wchar_t* end = nullptr;
        float value = std::wcstof(text, &end);
        if (end == text || *end || !std::isfinite(value) || value < low || value > high)
            throw std::runtime_error("Invalid controller deadzone/grip threshold in controls.ini");
        return value;
    };
    auto flag = [&](const wchar_t* key) {
        return GetPrivateProfileIntW(L"controls", key, 0, file) != 0;
    };
    out.deadzone = number(L"deadzone", .12f, 0, .8f);
    out.gripThreshold = number(L"grip_threshold", .55f, .05f, .95f);
    out.swapHands = flag(L"swap_hands");
    out.leftInvertX = flag(L"invert_left_x");
    out.leftInvertY = flag(L"invert_left_y");
    out.rightInvertX = flag(L"invert_right_x");
    out.rightInvertY = flag(L"invert_right_y");
    return out;
}
} // namespace dtvr
