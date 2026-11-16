#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace dtvr {
struct GameHooks {
    uint32_t render, vertex, fragment, projection, resize, submit, keyboard, analog;
};
struct GameBuild {
    uint32_t steamBuild;
    std::string_view sha256;
    uint32_t imageSize, timestamp;
    GameHooks hooks;
    std::array<uint32_t, 2> playCallers;
    uint32_t onFoot, findLoader, findDozer, sdlApiTable;

    constexpr bool matchesImage(uint32_t size, uint32_t time) const {
        return imageSize == size && timestamp == time;
    }
};

// Function bodies and the native object layouts used by the hooks were
// compared across both executables. RVAs move by different amounts, so each
// exact executable fingerprint must select its own complete address profile.
inline constexpr std::array<GameBuild, 2> gameBuilds{{
    {26658761,
     "4f830861d4be3afd43cf45b2056a48afc716118efec2f3b28380766f8b9cc97f",
     0x1e4d000,
     0x6af7b17f,
     {0x64ca00, 0xd56660, 0xd56720, 0x625620, 0x626430, 0xe8e140, 0xd3d700, 0x299cc0},
     {0x244d66, 0x2ea0bb},
     0x939c70,
     0x9393b0,
     0x939790,
     0x146f280},
    {26690600,
     "d8fddd701778b9c4e19ffeedc68243d49bac983c87e6900cc6d14f6997c0e898",
     0x1e4e000,
     0x6afa43c9,
     {0x64cac0, 0xd56a60, 0xd56b20, 0x6256e0, 0x6264f0, 0xe8e540, 0xd3db00, 0x299d80},
     {0x244e26, 0x2ea17b},
     0x939cf0,
     0x939430,
     0x939810,
     0x1470280},
}};

inline constexpr const GameBuild* findGameBuild(std::string_view sha256) {
    for (const auto& build : gameBuilds)
        if (build.sha256 == sha256)
            return &build;
    return nullptr;
}
} // namespace dtvr
