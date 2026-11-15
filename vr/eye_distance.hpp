#pragma once
#include <algorithm>

namespace dtvr {
struct VrKeyEvents {
    bool recenter = false, toggleVr = false, toggleAA = false, showEyeDistance = false;
    bool eyeDistanceChanged = false;
};

class VrHotkeys {
    bool f9Down = false, f10Down = false, f11Down = false;
    int eyeDistanceTenths = 10;

  public:
    float eyeDistance() const {
        return eyeDistanceTenths / 10.f;
    }

    VrKeyEvents update(bool shift, bool f9, bool f10, bool f11) {
        const bool press9 = f9 && !f9Down, press10 = f10 && !f10Down;
        VrKeyEvents events;
        events.recenter = press9 && !shift;
        events.toggleVr = press10 && !shift;
        events.toggleAA = f11 && !f11Down;
        events.showEyeDistance = shift && (press9 || press10);
        if (events.showEyeDistance) {
            const int previous = eyeDistanceTenths;
            eyeDistanceTenths = std::clamp(previous + int(press10) - int(press9), 5, 20);
            events.eyeDistanceChanged = previous != eyeDistanceTenths;
        }
        f9Down = f9;
        f10Down = f10;
        f11Down = f11;
        return events;
    }
};
} // namespace dtvr
