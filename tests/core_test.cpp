#include "game_math.hpp"
#include "controller_mapping.hpp"
#include "eye_distance.hpp"
#include "headset_hud.hpp"
#include "game_builds.hpp"
#include <cstdio>
#include <limits>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void verifyGameBuilds() {
    using namespace dtvr;
    require(gameBuilds.size() == 2, "Missing supported game build");
    for (const auto& build : gameBuilds) {
        require(findGameBuild(build.sha256) == &build &&
                    build.matchesImage(build.imageSize, build.timestamp) &&
                    !build.matchesImage(build.imageSize, build.timestamp + 1) &&
                    !build.matchesImage(build.imageSize + 0x1000, build.timestamp),
                "Game build fingerprint/image checks failed");
        const auto& h = build.hooks;
        for (auto rva : {h.render, h.vertex, h.fragment, h.projection, h.resize, h.submit,
                         h.keyboard, h.analog, build.onFoot, build.findLoader, build.findDozer,
                         build.sdlApiTable, build.playCallers[0], build.playCallers[1]})
            require(rva >= 0x1000 && rva < build.imageSize,
                    "Game build profile has an address outside the image");
    }
    require(gameBuilds[0].hooks.render != gameBuilds[1].hooks.render &&
                gameBuilds[0].sdlApiTable != gameBuilds[1].sdlApiTable &&
                gameBuilds[0].playCallers != gameBuilds[1].playCallers,
            "Updated build reused the original render/input addresses");
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

void verifyEyeDistance() {
    using namespace dtvr;
    VrHotkeys keys;
    require(keys.eyeDistance() == 1.f, "Eye distance does not default to OpenXR spacing");
    auto event = keys.update(true, false, true, false);
    require(event.eyeDistanceChanged && event.showEyeDistance && !event.toggleVr &&
                !event.recenter && std::abs(keys.eyeDistance() - 1.1f) < 1e-6f,
            "Shift+F10 changed VR state instead of eye distance");
    event = keys.update(false, false, true, false);
    require(!event.toggleVr && !event.eyeDistanceChanged,
            "Releasing Shift while holding F10 toggled VR");
    keys.update(false, false, false, false);
    event = keys.update(false, false, true, false);
    require(event.toggleVr && !event.showEyeDistance, "Unmodified F10 stopped toggling VR");
    for (int i = 0; i < 30; ++i) {
        keys.update(false, false, false, false);
        event = keys.update(true, true, false, false);
        require(!event.recenter && !event.toggleVr,
                "Scale decrease triggered recenter or VR toggle");
    }
    require(keys.eyeDistance() == .5f && event.showEyeDistance && !event.eyeDistanceChanged,
            "Eye distance did not clamp at 0.5x");
    for (int i = 0; i < 30; ++i) {
        keys.update(false, false, false, false);
        keys.update(true, false, true, false);
    }
    require(keys.eyeDistance() == 2.f, "Eye distance did not clamp at 2x");
    keys.update(false, false, false, false);
    event = keys.update(true, true, true, false);
    require(!event.eyeDistanceChanged && !event.toggleVr && !event.recenter,
            "Simultaneous scale keys changed the distance or VR state");
    keys.update(false, false, false, false);
    event = keys.update(false, true, false, true);
    require(event.recenter && event.toggleAA && !event.showEyeDistance,
            "Unmodified recenter/antialiasing keys stopped working");

    XrPosef anchor{gameOrientation(.7f, -.2f), {3.5f, 1.6f, -4.f}};
    XrPosef head = anchor;
    head.position.x += .25f;
    head.position.y += .1f;
    head.orientation = gameOrientation(-.4f, .3f);
    for (float yaw : {-2.f, 0.f, 2.f})
        for (float scale : {.5f, 1.f, 2.f}) {
            const auto game = gameOrientation(yaw, .1f);
            const auto expectedCenter = relativeEyePose(game, anchor, head).position;
            std::array<XrVector3f, 2> positions;
            for (unsigned i = 0; i < 2; ++i) {
                XrPosef eye = head;
                // Include rotated and asymmetric eye components, not a fixed X offset.
                const auto baseline = rotate(head.orientation, {i ? .034f : -.034f, 0, 0});
                eye.position.x += baseline.x;
                eye.position.y += baseline.y;
                eye.position.z += baseline.z;
                const auto scaled = scaledEyePose(eye, head, scale);
                require(std::memcmp(&eye.orientation, &scaled.orientation,
                                    sizeof(eye.orientation)) == 0,
                        "Eye distance changed the runtime eye orientation");
                if (scale == 1.f)
                    require(std::memcmp(&eye, &scaled, sizeof(eye)) == 0,
                            "1x did not preserve the exact OpenXR eye pose");
                positions[i] = relativeEyePose(game, anchor, scaled).position;
            }
            const XrVector3f center{(positions[0].x + positions[1].x) / 2,
                                    (positions[0].y + positions[1].y) / 2,
                                    (positions[0].z + positions[1].z) / 2};
            require(std::abs(center.x - expectedCenter.x) + std::abs(center.y - expectedCenter.y) +
                            std::abs(center.z - expectedCenter.z) <
                        1e-5f,
                    "Eye-distance effect scaled leaning or introduced an orbit radius");
            const XrVector3f delta{positions[1].x - positions[0].x, positions[1].y - positions[0].y,
                                   positions[1].z - positions[0].z};
            require(std::abs(std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) -
                             .068f * scale) < 1e-5f,
                    "Eye separation did not scale from the runtime baseline");
        }
    const auto label = eyeDistanceHudImage(1.2f);
    const auto other = eyeDistanceHudImage(.5f);
    require(label != other && label[0] == (label[0] & 0x00ffffff) &&
                (label[(hudHeight / 2) * hudWidth + hudWidth / 2] >> 24) == 235,
            "Headset scale label did not render its value and rounded panel");
    unsigned letters = 0;
    for (auto pixel : label)
        letters += (pixel & 255) > 100;
    require(letters > 200, "Headset scale label has no readable text");
}
} // namespace

int main() {
    try {
        dtvr::verifyCameraMath();
        dtvr::verifyControllerMapping();
        verifyTrackingOrigin();
        verifyEyeDistance();
        verifyGameBuilds();
        std::puts(
            "PASS: eye projection/culling, tracking origin recovery, eye-distance controls/HUD, view routing and controller mapping");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
