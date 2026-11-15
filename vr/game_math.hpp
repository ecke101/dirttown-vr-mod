#pragma once
#include "xr_d3d12.hpp"

namespace dtvr {
inline XrQuaternionf conjugate(XrQuaternionf q) {
    return {-q.x, -q.y, -q.z, q.w};
}
inline XrQuaternionf product(XrQuaternionf a, XrQuaternionf b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline XrVector3f rotate(XrQuaternionf q, XrVector3f v) {
    auto r = product(product(q, {v.x, v.y, v.z, 0}), conjugate(q));
    return {r.x, r.y, r.z};
}
inline XrQuaternionf gameOrientation(float yaw, float pitch) {
    return product({0, -std::sin(yaw / 2), 0, std::cos(yaw / 2)},
                   {std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)});
}
// Position is relative to the native game camera, whose world coordinates are
// doubles. Keep only the small tracking displacement in float precision.
inline XrPosef relativeEyePose(XrQuaternionf gameRotation, const XrPosef& anchor,
                               const XrPosef& eye) {
    const auto worldFromTracking = product(gameRotation, conjugate(anchor.orientation));
    return {product(worldFromTracking, eye.orientation),
            rotate(worldFromTracking,
                   {eye.position.x - anchor.position.x, eye.position.y - anchor.position.y,
                    eye.position.z - anchor.position.z})};
}
inline Matrix inverse(Matrix input) {
    double a[4][8]{};
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c) {
            a[r][c] = input[c * 4 + r];
            a[r][c + 4] = (r == c);
        }
    for (unsigned c = 0; c < 4; ++c) {
        unsigned pivot = c;
        for (unsigned r = c + 1; r < 4; ++r)
            if (std::abs(a[r][c]) > std::abs(a[pivot][c]))
                pivot = r;
        if (std::abs(a[pivot][c]) < 1e-12)
            throw std::runtime_error("Singular camera matrix");
        for (unsigned k = 0; k < 8; ++k)
            std::swap(a[c][k], a[pivot][k]);
        const double scale = a[c][c];
        for (unsigned k = 0; k < 8; ++k)
            a[c][k] /= scale;
        for (unsigned r = 0; r < 4; ++r)
            if (r != c) {
                double v = a[r][c];
                for (unsigned k = 0; k < 8; ++k)
                    a[r][k] -= v * a[c][k];
            }
    }
    Matrix output{};
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c)
            output[c * 4 + r] = (float)a[r][c + 4];
    return output;
}
inline Matrix reversedProjection(const XrFovf& fov, float nearZ, float farZ) {
    auto m = projection(fov, nearZ, farZ);
    m[10] = nearZ / (farZ - nearZ);
    m[14] = farZ * nearZ / (farZ - nearZ);
    return m;
}
// The game's SIMD position occupies four doubles. Its fourth lane is padding.
// Rendering subtracts camWorld before multiplication, so view translation is zero.
struct GameCamera {
    double position[4];
    float yaw, pitch, fov, pad, nearZ, farZ;
};
static_assert(sizeof(GameCamera) == 56);
struct CameraMatrices {
    Matrix view, viewProjection, inverseViewProjection;
};
inline const void* patchEyeView(const CameraMatrices& matrices, uint32_t slot, const void* data,
                                uint32_t length, std::array<unsigned char, 1744>& copy) {
    // The renderer also uploads this View layout for planar mirrors. Only the
    // primary view starts with the exact VP returned for the active eye camera.
    if (slot != 0 || length != copy.size() ||
        std::memcmp(data, matrices.viewProjection.data(), sizeof(Matrix)) != 0)
        return data;
    std::memcpy(copy.data(), data, length);
    std::memcpy(copy.data() + 64, matrices.inverseViewProjection.data(), sizeof(Matrix));
    std::memcpy(copy.data() + 192, matrices.view.data(), sizeof(Matrix));
    return copy.data();
}
inline CameraMatrices cameraMatrices(XrQuaternionf orientation, const XrFovf& fov, float nearZ,
                                     float farZ) {
    XrPosef pose{orientation, {0, 0, 0}};
    CameraMatrices m{};
    m.view = viewMatrix(pose);
    m.viewProjection = multiply(reversedProjection(fov, nearZ, farZ), m.view);
    m.inverseViewProjection = inverse(m.viewProjection);
    return m;
}
inline float cullingFov(XrQuaternionf orientation, const XrFovf& fov, float aspect) {
    auto forward = rotate(orientation, {0, 0, -1});
    const float yaw = std::atan2(forward.x, -forward.z),
                pitch = std::asin(std::clamp(forward.y, -1.f, 1.f));
    const auto relative = product(conjugate(gameOrientation(yaw, pitch)), orientation);
    float bound = 0;
    aspect = std::max(.01f, aspect);
    for (float x : {std::tan(fov.angleLeft), std::tan(fov.angleRight)})
        for (float y : {std::tan(fov.angleDown), std::tan(fov.angleUp)}) {
            const auto ray = rotate(relative, {x, y, -1});
            if (ray.z >= -.0001f)
                return 179.f;
            const float h = std::abs(ray.x / ray.z), v = std::abs(ray.y / ray.z);
            bound = std::max({bound, h, v, h / aspect, v * aspect});
        }
    return std::min(179.f, 2 * std::atan(bound) * 180.f / 3.14159265358979323846f + 2.f);
}
inline float halton(unsigned index, unsigned base) {
    float value = 0, weight = 1;
    while (index) {
        weight /= base;
        value += weight * (index % base);
        index /= base;
    }
    return value;
}
inline void jitterCamera(CameraMatrices& matrices, float x, float y, uint32_t width,
                         uint32_t height) {
    const float dx = 2 * x / width, dy = -2 * y / height;
    for (unsigned c = 0; c < 4; ++c) {
        matrices.viewProjection[c * 4] += dx * matrices.viewProjection[c * 4 + 3];
        matrices.viewProjection[c * 4 + 1] += dy * matrices.viewProjection[c * 4 + 3];
    }
    matrices.inverseViewProjection = inverse(matrices.viewProjection);
}
inline void verifyCameraMath() {
    // Boundary rays map to +/-1; reversed depth maps near to 1 and far to 0.
    XrFovf fov{-.8f, .9f, .6f, -.7f};
    auto p = reversedProjection(fov, .3f, 2000.f);
    auto close = [](double a, double b) {
        if (std::abs(a - b) > 2e-4)
            throw std::runtime_error("VR camera math self-check failed");
    };
    close(cullingFov({0, 0, 0, 1}, {-.9f, .9f, .9f, -.9f}, 1),
          1.8f * 180.f / 3.14159265358979323846f + 2.f);
    // Rolled, asymmetric and wide-FOV eyes must stay inside conservative culling.
    if (cullingFov({0, 0, .70710678f, .70710678f}, {-1.2f, .8f, .6f, -.7f}, 1) < 139.f ||
        cullingFov({0, 0, 0, 1}, {-.8f, .9f, .6f, -.7f}, .6f) <
            cullingFov({0, 0, 0, 1}, {-.8f, .9f, .6f, -.7f}, 1))
        throw std::runtime_error("VR wide-FOV culling self-check failed");
    close(p[0] * std::tan(fov.angleLeft) - p[8], -1);
    close(p[0] * std::tan(fov.angleRight) - p[8], 1);
    close(p[5] * std::tan(fov.angleDown) - p[9], -1);
    close(p[5] * std::tan(fov.angleUp) - p[9], 1);
    close((-p[10] * .3 + p[14]) / .3, 1);
    close((-p[10] * 2000 + p[14]) / 2000, 0);
    for (int i = 0; i < 101; ++i) {
        auto m = cameraMatrices(gameOrientation(i * .062f - 3.1f, i * .02f - 1), fov, .3f, 2000);
        auto original = m.viewProjection;
        jitterCamera(m, .25f, -.375f, 1296, 1296);
        // Pick the homogeneous camera-forward direction and compare NDC shift.
        auto direction = rotate(gameOrientation(i * .062f - 3.1f, i * .02f - 1), {0, 0, -1});
        auto clip = [&](const Matrix& p, unsigned row) {
            return p[row] * direction.x + p[4 + row] * direction.y + p[8 + row] * direction.z +
                   p[12 + row];
        };
        close(clip(m.viewProjection, 0) / clip(m.viewProjection, 3) -
                  clip(original, 0) / clip(original, 3),
              .5 / 1296);
        close(clip(m.viewProjection, 1) / clip(m.viewProjection, 3) -
                  clip(original, 1) / clip(original, 3),
              .75 / 1296);
        auto unit = multiply(m.viewProjection, m.inverseViewProjection);
        for (unsigned j = 0; j < 16; ++j)
            close(unit[j], j % 5 == 0 ? 1 : 0);
    }
    // Reference camera and View uniform captured from the stock renderer.
    auto view = viewMatrix({gameOrientation(-.3332442939f, -.0797905922f), {0, 0, 0}});
    close(view[0], .944986);
    close(view[2], .326056);
    close(view[5], .996818);
    close(view[8], -.327111);
    // A reflected view shares the buffer layout but must retain its own VP,
    // inverse and reflected basis. Exercise the same routing used by the hooks.
    auto eye = cameraMatrices(gameOrientation(.4f, -.2f), fov, .3f, 2000.f);
    std::array<unsigned char, 1744> primary{}, auxiliary{}, copy{};
    primary.fill(0x5a);
    auxiliary.fill(0xa5);
    std::memcpy(primary.data(), eye.viewProjection.data(), sizeof(Matrix));
    if (patchEyeView(eye, 0, primary.data(), primary.size(), copy) != copy.data() ||
        std::memcmp(copy.data() + 64, eye.inverseViewProjection.data(), sizeof(Matrix)) ||
        std::memcmp(copy.data() + 192, eye.view.data(), sizeof(Matrix)) ||
        std::memcmp(copy.data() + 256, primary.data() + 256, primary.size() - 256) ||
        patchEyeView(eye, 0, auxiliary.data(), auxiliary.size(), copy) != auxiliary.data() ||
        patchEyeView(eye, 1, primary.data(), primary.size(), copy) != primary.data() ||
        patchEyeView(eye, 0, primary.data(), 240, copy) != primary.data())
        throw std::runtime_error("VR primary/mirror View routing self-check failed");
}
} // namespace dtvr
