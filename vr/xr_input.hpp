#pragma once
#include "xr_d3d12.hpp"
#include "controller_mapping.hpp"
#include "controller_profiles.hpp"
#include "controller_settings.hpp"

namespace dtvr {
class XrInput {
    XrBridge* bridge = nullptr;
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction stick{}, trigger{}, grip{}, gripClick{}, primary{}, secondary{}, click{}, menu{},
        axisTouch{};
    std::wstring controlsFile;
    std::array<XrPath, 2> hands{}, profiles{};
#define DT_INPUT_FUNCTIONS(F)                                                                      \
    F(xrStringToPath)                                                                              \
    F(xrPathToString)                                                                              \
    F(xrCreateActionSet)                                                                           \
    F(xrDestroyActionSet)                                                                          \
    F(xrCreateAction)                                                                              \
    F(xrSuggestInteractionProfileBindings)                                                         \
    F(xrAttachSessionActionSets)                                                                   \
    F(xrSyncActions)                                                                               \
    F(xrGetActionStateBoolean)                                                                     \
    F(xrGetActionStateFloat)                                                                       \
    F(xrGetActionStateVector2f)                                                                    \
    F(xrGetCurrentInteractionProfile)
#define DT_INPUT_FIELD(name) PFN_##name name = nullptr;
    DT_INPUT_FUNCTIONS(DT_INPUT_FIELD)
#undef DT_INPUT_FIELD
    XrPath path(const char* text) {
        XrPath value;
        check_xr(xrStringToPath(bridge->instance, text, &value), text);
        return value;
    }
    void create(XrAction& action, XrActionType type, const char* name, const char* label) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType = type;
        std::strcpy(info.actionName, name);
        std::strcpy(info.localizedActionName, label);
        info.countSubactionPaths = 2;
        info.subactionPaths = hands.data();
        check_xr(xrCreateAction(actionSet, &info, &action), name);
    }
    bool boolean(XrAction action, unsigned hand, bool& active) {
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = hands[hand];
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        check_xr(xrGetActionStateBoolean(bridge->session, &info, &state), "Read controller button");
        active |= state.isActive;
        return state.isActive && state.currentState;
    }
    float scalar(XrAction action, unsigned hand, bool& active) {
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = action;
        info.subactionPath = hands[hand];
        XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
        check_xr(xrGetActionStateFloat(bridge->session, &info, &state),
                 "Read controller trigger/grip");
        active |= state.isActive;
        return state.isActive ? state.currentState : 0;
    }

  public:
    ControllerSettings settings;
    void setup(XrBridge& xr, const wchar_t* file) {
        bridge = &xr;
        controlsFile = file;
        settings = readControllerSettings(file);
#define DT_INPUT_RESOLVE(name) xr.resolve(#name, name, xr.instance);
        DT_INPUT_FUNCTIONS(DT_INPUT_RESOLVE)
#undef DT_INPUT_RESOLVE
        hands = {path("/user/hand/left"), path("/user/hand/right")};
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strcpy(set.actionSetName, "dirttown_controls");
        std::strcpy(set.localizedActionSetName, "Dirt Town controls");
        check_xr(xrCreateActionSet(xr.instance, &set, &actionSet), "Create VR control action set");
        create(stick, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick");
        create(trigger, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger");
        create(grip, XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grip");
        create(gripClick, XR_ACTION_TYPE_BOOLEAN_INPUT, "grip_click", "Grip click");
        create(axisTouch, XR_ACTION_TYPE_BOOLEAN_INPUT, "axis_touch", "Trackpad touch");
        create(primary, XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "A or X");
        create(secondary, XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "B or Y");
        create(click, XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Thumbstick click");
        create(menu, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu");
        unsigned accepted = 0;
        for (const auto& profile : controllerProfiles()) {
            if (profile.picoExtension && !xr.picoControllerProfile)
                continue;
            try {
                std::vector<XrActionSuggestedBinding> bindings;
                for (unsigned i = 0; i < 2; ++i) {
                    const auto& defaults = profile.hands[i];
                    const std::string hand = i ? "/user/hand/right" : "/user/hand/left";
                    auto bind = [&](XrAction action, const char* name,
                                    const std::string& fallback) {
                        const std::string section = "bindings." + profile.path,
                                          key = (i ? "right." : "left.") + std::string(name);
                        std::wstring wsection(section.begin(), section.end()),
                            wkey(key.begin(), key.end()),
                            wdefault(fallback.begin(), fallback.end());
                        wchar_t component[256]{};
                        GetPrivateProfileStringW(wsection.c_str(), wkey.c_str(), wdefault.c_str(),
                                                 component, 256, controlsFile.c_str());
                        std::wstring wide = component;
                        std::string input(wide.begin(), wide.end());
                        if (input.empty())
                            return;
                        if (input.rfind("/input/", 0) != 0)
                            throw std::runtime_error("Controller binding must start with /input/");
                        bindings.push_back({action, path((hand + input).c_str())});
                    };
                    bind(stick, "stick", defaults.stick);
                    bind(trigger, "trigger", defaults.trigger);
                    bind(grip, "grip", defaults.grip);
                    bind(gripClick, "grip_click", defaults.gripClick);
                    bind(primary, "primary", defaults.primary);
                    bind(secondary, "secondary", defaults.secondary);
                    bind(click, "click", defaults.click);
                    bind(axisTouch, "axis_touch", defaults.axisTouch);
                    if (!i)
                        bind(menu, "menu", defaults.menu);
                }
                XrInteractionProfileSuggestedBinding suggestion{
                    XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
                suggestion.interactionProfile = path(profile.path.c_str());
                suggestion.countSuggestedBindings = (uint32_t)bindings.size();
                suggestion.suggestedBindings = bindings.data();
                const auto result = xrSuggestInteractionProfileBindings(xr.instance, &suggestion);
                std::printf("Controller profile %s: %d\n", profile.path.c_str(), (int)result);
                if (XR_SUCCEEDED(result))
                    ++accepted;
            } catch (const std::exception& e) {
                std::printf("Controller profile %s skipped: %s\n", profile.path.c_str(), e.what());
            }
        }
        if (!accepted)
            throw std::runtime_error("Runtime rejected controller binding profiles");
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1;
        attach.actionSets = &actionSet;
        check_xr(xrAttachSessionActionSets(xr.session, &attach), "Attach VR controls");
        std::puts("VR thumbstick/button actions attached.");
    }
    ControllerInput sample() {
        ControllerInput input;
        if (!bridge || !bridge->running || !bridge->focused)
            return input;
        XrActiveActionSet active{actionSet, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        auto result = xrSyncActions(bridge->session, &sync);
        if (result == XR_SESSION_NOT_FOCUSED)
            return input;
        check_xr(result, "Synchronize VR controls");
        input.focused = true;
        for (unsigned i = 0; i < 2; ++i) {
            XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
            info.action = stick;
            info.subactionPath = hands[i];
            XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
            check_xr(xrGetActionStateVector2f(bridge->session, &info, &state),
                     "Read controller stick");
            auto& hand = input.hands[i];
            hand.active = state.isActive;
            if (state.isActive) {
                hand.x = state.currentState.x;
                hand.y = state.currentState.y;
            }
            bool touchActive = false;
            const bool touching = boolean(axisTouch, i, touchActive);
            if (touchActive && !touching)
                hand.x = hand.y = 0;
            hand.trigger = scalar(trigger, i, hand.active);
            hand.grip = scalar(grip, i, hand.active);
            if (boolean(gripClick, i, hand.active))
                hand.grip = 1;
            hand.primary = boolean(primary, i, hand.active);
            hand.secondary = boolean(secondary, i, hand.active);
            hand.click = boolean(click, i, hand.active);
            hand.menu = i ? false : boolean(menu, i, hand.active);
            XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
            check_xr(xrGetCurrentInteractionProfile(bridge->session, hands[i], &profile),
                     "Read controller profile");
            if (profile.interactionProfile != profiles[i]) {
                profiles[i] = profile.interactionProfile;
                char name[256] = "none";
                uint32_t length = 0;
                if (profiles[i])
                    xrPathToString(bridge->instance, profiles[i], sizeof(name), &length, name);
                std::printf("Controller %u interaction profile: %s\n", i, name);
            }
        }
        return input;
    }
    ~XrInput() {
        if (actionSet && xrDestroyActionSet)
            xrDestroyActionSet(actionSet);
    }
};
#undef DT_INPUT_FUNCTIONS
} // namespace dtvr
