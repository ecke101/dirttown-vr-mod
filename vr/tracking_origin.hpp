#pragma once
#include <openxr/openxr.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dtvr {
inline bool validHeadPose(const XrSpaceLocation& head) {
    constexpr auto valid =
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((head.locationFlags & valid) != valid)
        return false;
    const auto& p = head.pose.position;
    const auto& q = head.pose.orientation;
    const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(norm) &&
           std::abs(norm - 1.f) < .01f;
}

// A saved pose belongs to one definition of LOCAL space. Never subtract it
// from a pose located after the runtime has changed that space's origin.
class TrackingOrigin {
  public:
    XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
    uint64_t generation = 0;

    void reset() {
        anchored = false;
    }

    void spaceChangePending(XrTime changeTime) {
        changes.push_back(changeTime);
    }

    bool update(const XrSpaceLocation& head, XrViewStateFlags eyes, XrTime displayTime) {
        changes.erase(std::remove_if(changes.begin(), changes.end(),
                                     [&](XrTime changeTime) {
                                         if (displayTime < changeTime)
                                             return false;
                                         reset();
                                         return true;
                                     }),
                      changes.end());
        constexpr auto headTracked =
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        constexpr auto eyesTracked =
            XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT |
            XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
        // VALID alone can describe a last-known or inferred startup pose. Wait
        // for tracking, and capture a fresh origin when tracking returns.
        if (!validHeadPose(head) || (head.locationFlags & headTracked) != headTracked ||
            (eyes & eyesTracked) != eyesTracked) {
            reset();
            return false;
        }
        if (!anchored) {
            pose = head.pose;
            anchored = true;
            ++generation;
        }
        return true;
    }

  private:
    bool anchored = false;
    std::vector<XrTime> changes;
};
} // namespace dtvr
