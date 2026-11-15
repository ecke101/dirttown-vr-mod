#include "game_math.hpp"
#include "controller_mapping.hpp"
#include <cstdio>
#include <limits>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void verifyTrackingOrigin() {
    using namespace dtvr;
    constexpr auto validHead =
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    constexpr auto trackedHead = validHead | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
                                 XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    constexpr auto validEyes =
        XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    constexpr auto trackedEyes =
        validEyes | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    head.pose = {{0, 0, 0, 1}, {0, 0, 0}};
    TrackingOrigin origin;
    require(!origin.update(head, trackedEyes, 1), "Invalid head pose accepted as origin");
    head.locationFlags = validHead;
    require(!origin.update(head, trackedEyes, 2), "Untracked startup head accepted as origin");
    head.locationFlags = trackedHead;
    require(!origin.update(head, validEyes, 3), "Untracked startup eyes accepted as origin");
    require(origin.generation == 0, "Startup captured a stale origin");

    head.pose = {gameOrientation(.7f, -.2f), {3.5f, 1.6f, -4.f}};
    require(origin.update(head, trackedEyes, 4), "Tracked startup pose rejected");
    const auto initial = origin.pose;
    // Ordinary leaning must retain its displacement, rather than recentering
    // every frame. Mouse orbit rotates it with the game camera as before.
    head.pose.position.x += .25f;
    require(origin.update(head, trackedEyes, 5) && origin.generation == 1 &&
                origin.pose.position.x == initial.position.x,
            "Normal head movement changed the origin");
    auto lean = relativeEyePose(gameOrientation(0, 0), origin.pose, head.pose).position;
    require(std::abs(lean.x * lean.x + lean.y * lean.y + lean.z * lean.z - .0625f) < 1e-5f,
            "Normal head translation was lost");

    // WiVRn/runtime tracking recovery may move LOCAL coordinates by meters.
    // Valid last-known poses must not leave that movement as an orbit radius.
    head.locationFlags = validHead;
    require(!origin.update(head, trackedEyes, 6), "Tracking loss did not invalidate origin");
    head.locationFlags = trackedHead;
    head.pose = {gameOrientation(-.4f, .1f), {-5.f, 1.7f, 8.f}};
    require(origin.update(head, trackedEyes, 7) && origin.generation == 2,
            "Tracking recovery did not capture a new origin");
    auto verifyCenteredStereo = [&] {
        for (float yaw : {-3.f, -1.5f, 0.f, 1.5f, 3.f}) {
            XrVector3f center{};
            for (float ipd : {-.034f, .034f}) {
                XrPosef eye = head.pose;
                const auto offset = rotate(head.pose.orientation, {ipd, 0, 0});
                eye.position.x += offset.x;
                eye.position.y += offset.y;
                eye.position.z += offset.z;
                const auto relative =
                    relativeEyePose(gameOrientation(yaw, -.15f), origin.pose, eye);
                const auto p = relative.position;
                require(std::abs(p.x * p.x + p.y * p.y + p.z * p.z - ipd * ipd) < 1e-6f,
                        "Recovered eye retained a large orbit radius");
                center.x += p.x;
                center.y += p.y;
                center.z += p.z;
            }
            require(std::abs(center.x) + std::abs(center.y) + std::abs(center.z) < 1e-5f,
                    "Recovered stereo center missed the native camera");
        }
    };
    verifyCenteredStereo();

    // Reset only when the predicted display time reaches a space change.
    // Queue out of order to ensure a later event cannot hide an earlier one.
    origin.spaceChangePending(40);
    origin.spaceChangePending(20);
    require(origin.update(head, trackedEyes, 19) && origin.generation == 2,
            "Future space change reset the origin early");
    head.pose.position.x += 4;
    require(origin.update(head, trackedEyes, 20) && origin.generation == 3,
            "Space change did not reset at its effective time");
    verifyCenteredStereo();
    head.pose.position.x += .1f;
    require(origin.update(head, trackedEyes, 21) && origin.generation == 3,
            "Space change reset more than once");
    origin.reset(); // F9, scene, controlled-entity or VR enable transition.
    require(origin.update(head, trackedEyes, 30) && origin.generation == 4,
            "Explicit recenter did not capture a new origin");
    head.pose.position.z -= 7;
    require(origin.update(head, trackedEyes, 40) && origin.generation == 5,
            "Explicit recenter discarded a pending runtime space change");
    verifyCenteredStereo();
    require(origin.update(head, trackedEyes, 41) && origin.generation == 5,
            "Consumed space changes remained pending");

    require(!origin.update(head, validEyes, 42), "Eye tracking loss did not reset origin");
    require(origin.update(head, trackedEyes, 43) && origin.generation == 6,
            "Eye tracking recovery did not recenter");
    head.pose.position.x = std::numeric_limits<float>::quiet_NaN();
    require(!origin.update(head, trackedEyes, 44), "Nonfinite origin accepted");
    head.pose.position.x = 0;
    head.pose.orientation = {0, 0, 0, 0};
    require(!origin.update(head, trackedEyes, 45), "Invalid origin orientation accepted");
}
} // namespace

int main() {
    try {
        dtvr::verifyCameraMath();
        dtvr::verifyControllerMapping();
        verifyTrackingOrigin();
        std::puts(
            "PASS: eye projection/culling, view routing, tracking origin recovery, controller mapping and input isolation");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
