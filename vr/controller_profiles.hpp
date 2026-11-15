#pragma once
#include <array>
#include <string>
#include <vector>

namespace dtvr {
struct HandBindings {
    std::string stick, trigger, grip, gripClick, primary, secondary, click, menu, axisTouch;
};
struct ControllerProfile {
    std::string path;
    std::array<HandBindings, 2> hands;
    bool picoExtension = false;
};
inline std::vector<ControllerProfile> controllerProfiles() {
    const HandBindings touchLeft{"/input/thumbstick",
                                 "/input/trigger/value",
                                 "/input/squeeze/value",
                                 "",
                                 "/input/x/click",
                                 "/input/y/click",
                                 "/input/thumbstick/click",
                                 "/input/menu/click",
                                 ""};
    const HandBindings touchRight{"/input/thumbstick",
                                  "/input/trigger/value",
                                  "/input/squeeze/value",
                                  "",
                                  "/input/a/click",
                                  "/input/b/click",
                                  "/input/thumbstick/click",
                                  "",
                                  ""};
    const HandBindings index{"/input/thumbstick",
                             "/input/trigger/value",
                             "/input/squeeze/value",
                             "",
                             "/input/a/click",
                             "/input/b/click",
                             "/input/thumbstick/click",
                             "",
                             ""};
    const HandBindings vive{
        "/input/trackpad",       "/input/trigger/value", "", "/input/squeeze/click",
        "/input/trackpad/click", "/input/menu/click",    "", "",
        "/input/trackpad/touch"};
    const HandBindings motion{"/input/thumbstick",
                              "/input/trigger/value",
                              "",
                              "/input/squeeze/click",
                              "/input/trackpad/click",
                              "/input/menu/click",
                              "/input/thumbstick/click",
                              "",
                              ""};
    const HandBindings simple{"", "", "", "", "/input/select/click", "/input/menu/click",
                              "", "", ""};
    return {{"/interaction_profiles/oculus/touch_controller", {touchLeft, touchRight}},
            {"/interaction_profiles/bytedance/pico4_controller", {touchLeft, touchRight}, true},
            {"/interaction_profiles/valve/index_controller", {index, index}},
            {"/interaction_profiles/htc/vive_controller", {vive, vive}},
            {"/interaction_profiles/microsoft/motion_controller", {motion, motion}},
            {"/interaction_profiles/khr/simple_controller", {simple, simple}}};
}
} // namespace dtvr
