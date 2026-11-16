#include "core.hpp"
#include "../vr/game_builds.hpp"
#include <clocale>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace launcher {
void selfTest() {
    auto require = [](bool ok, const char* test) {
        if (!ok)
            throw std::runtime_error(std::string("Launcher check failed: ") + test);
    };
    auto rejects = [&](auto action, const char* test) {
        bool rejected = false;
        try {
            action();
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, test);
    };
    require(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 vector");
    const auto root = fs::temp_directory_path() /
                      path("dirttown-launcher-" + std::to_string(processId()) + "-å space $()");
    privateDirectory(root);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(p, e);
        }
    } cleanup{root};
    Paths dirs{root / "config", root / "cache", root / "logs", root / "bin", root / "legacy"};
    Config c;
    const auto* original =
        dtvr::findGameBuild("4f830861d4be3afd43cf45b2056a48afc716118efec2f3b28380766f8b9cc97f");
    const auto* updated =
        dtvr::findGameBuild("d8fddd701778b9c4e19ffeedc68243d49bac983c87e6900cc6d14f6997c0e898");
    require(original && original->steamBuild == 26658761 && updated &&
                updated->steamBuild == 26690600,
            "exact executable fingerprints select both supported builds");
    require(original->matchesImage(0x1e4d000, 0x6af7b17f) &&
                updated->matchesImage(0x1e4e000, 0x6afa43c9) &&
                !updated->matchesImage(original->imageSize, original->timestamp) &&
                !original->matchesImage(updated->imageSize, updated->timestamp),
            "mixed executable/address profiles are rejected");
    require(!dtvr::findGameBuild("") && !dtvr::findGameBuild(std::string(64, '0')),
            "unknown executables are rejected");
    require(!c.antialiasing && !Config::fromJson(Json::object()).antialiasing,
            "antialiasing defaults off");
    require(Config::fromJson({{"antialiasing", true}}).antialiasing,
            "explicit antialiasing opt-in preserved");
    c.game = text(root / "Steam Library/steamapps/common/Dirt Town Demo");
    c.scale = 1;
    c.controllers = false;
    writeFile(dirs.legacy / "config.json", c.json().dump());
    auto loaded = Config::load(dirs);
    require(loaded.game == c.game && loaded.scale == 1 && !loaded.controllers,
            "migrate preferences");
    loaded.scale = .625;
    loaded.save(dirs);
    require(Config::load(dirs).scale == .625, "save/read preferences");
    for (double scale : {0., .1, 2., std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
        auto invalid = c;
        invalid.scale = scale;
        rejects(
            [&] {
                invalid.validate();
            },
            "resolution validation");
    }
    auto invalid = c;
    invalid.graphics = "unknown";
    rejects(
        [&] {
            invalid.validate();
        },
        "graphics validation");
    const auto vdf = vdfPaths(
        "\"libraryfolders\" { \"0\" { \"path\" \"D:\\\\Steam Library\" } \"1\" { \"path\" \"/games/å\" } }");
    require(vdf.size() == 2 && vdf[0] == "D:\\Steam Library" && vdf[1] == "/games/å", "VDF paths");
    auto steam = root / "Steam", other = root / "Other Library";
    writeFile(steam / "steamapps/libraryfolders.vdf", "\"path\" " + Json(text(other)).dump());
    auto libraries = steamLibraries({steam});
    require(libraries.size() == 2 && libraries[1] == other, "secondary Steam libraries");
    privateDirectory(path(c.game));
    writeFile(path(c.game) / "dirttown.exe", "unknown game");
    rejects(
        [&] {
            validateGame(c);
        },
        "unsupported game rejection");
    require(readFile(path(c.game) / "dirttown.exe") == "unknown game", "game remains read-only");
    for (const char* file :
         {"vr-launch.exe", "openxr_loader.dll", "dirttown-vr.dll", "core-test.exe"})
        writeFile(dirs.binaries / file, "fixture");
#ifndef _WIN32
    auto proton = root / "Recorded Proton/proton";
    writeFile(proton, "fixture");
    auto prefix = path(c.game).parent_path().parent_path() / "compatdata" / appId;
    writeFile(prefix / "config_info", text(proton.parent_path()) + "/files/share/fonts/\n");
    require(findProton(c, {steam}).executable == proton, "recorded Proton");
    c.proton = text(proton);
    auto diagnostics = c;
    diagnostics.game.clear();
    auto diagnosticProton = findProton(diagnostics, {steam});
    require(diagnosticProton.executable == proton && diagnosticProton.prefix.empty(),
            "explicit Proton without a game folder");
    auto installedProton = steam / "steamapps/common/Proton - Experimental/proton";
    writeFile(installedProton, "fixture");
    diagnostics.proton.clear();
    require(findProton(diagnostics, {steam}).executable == installedProton,
            "discover Proton without a game folder");
#endif
    c.antialiasing = false;
    c.graphics = "d3d11";
    auto plan = [&] {
        struct NumericLocale {
            std::string previous = std::setlocale(LC_NUMERIC, nullptr);
            ~NumericLocale() {
                std::setlocale(LC_NUMERIC, previous.c_str());
            }
        } numericLocale;
        // Reproduce GTK's decimal-comma locale where installed. The same
        // checks also run on hosts that only provide the default C locale.
        for (const char* name : {"sv_SE.UTF-8", "sv_SE.utf8", "Swedish_Sweden.1252"})
            if (std::setlocale(LC_NUMERIC, name))
                break;
        c.scale = .625;
        require(c.scaleArgument() == "0.625", "locale-independent GUI scale argument");
        return makePlan(c, dirs, "launch");
    }();
    require(plan.vars.at("DIRTTOWN_VR_RESOLUTION_SCALE") == "0.625",
            "locale-independent injected scale");
    require(plan.vars.at("DIRTTOWN_VR_CONTROLLERS") == "0" &&
                plan.vars.at("DIRTTOWN_VR_TAA") == "0" &&
                plan.vars.at("DIRTTOWN_VR_GRAPHICS") == "d3d11",
            "per-launch settings");
#ifdef _WIN32
    require(plan.command.size() == 3 && plan.command[0] == text(dirs.binaries / "vr-launch.exe"),
            "native Windows plan");
#else
    require(plan.command.size() == 5 && plan.command[0] == c.proton && plan.command[1] == "run" &&
                plan.command[3].find("Z:") == 0,
            "Proton plan");
    require(plan.vars.at("STEAM_COMPAT_DATA_PATH") ==
                text(path(c.game).parent_path().parent_path() / "compatdata" / appId),
            "correct game prefix");
#endif
    fs::remove(dirs.binaries / "dirttown-vr.dll");
    rejects(
        [&] {
            makePlan(c, dirs, "launch");
        },
        "missing binary validation");
    auto manifest = root / "runtime/custom.json";
    Json data = {
        {"runtime", {{"library_path", "lib/runtime.so"}, {"MND_libmonado_path", "lib/monado.so"}}}};
    writeFile(manifest, data.dump());
    c.runtime = text(manifest);
    Environment base{{"KEEP", "yes"}, {"XDG_CONFIG_HOME", text(root / "user-config")}};
    writeFile(root / "user-config/openvr/openvrpaths.vrpath", "{}");
    const auto selected = runtimeEnvironment(c, dirs, base);
    require(readFile(manifest) == data.dump() && base.size() == 2,
            "runtime source/environment preserved");
#ifdef _WIN32
    require(fs::equivalent(path(selected.at("XR_RUNTIME_JSON")), manifest),
            "Windows runtime override");
#else
    auto overlay = path(selected.at("XDG_CONFIG_HOME")) / "openxr/1/active_runtime.json";
    auto rewritten = Json::parse(readFile(overlay));
    require(rewritten["runtime"]["library_path"] == text(root / "runtime/lib/runtime.so") &&
                rewritten["runtime"]["MND_libmonado_path"] == text(root / "runtime/lib/monado.so"),
            "relative runtime libraries");
    require(selected.at("VR_PATHREG_OVERRIDE") ==
                    text(root / "user-config/openvr/openvrpaths.vrpath") &&
                !selected.count("XR_RUNTIME_JSON"),
            "preserve Proton bridge/bootstrap");
    base["VR_PATHREG_OVERRIDE"] = "/custom/vrpath";
    require(runtimeEnvironment(c, dirs, base).at("VR_PATHREG_OVERRIDE") == "/custom/vrpath",
            "explicit bootstrap override");
    for (const char* invalid : {"", "relative-path"}) {
        base["XDG_CONFIG_HOME"] = invalid;
        const auto fallback = runtimeEnvironment(c, dirs, base);
        require(fallback.at("XDG_CONFIG_DIRS").find(text(homePath() / ".config") + ":") == 0,
                "runtime overlay ignores invalid XDG config directories");
    }
#endif
    require(quoteWindows(L"a b") == L"\"a b\"" && quoteWindows(L"a\\") == L"\"a\\\\\"" &&
                quoteWindows(L"a\"b") == L"\"a\\\"b\"",
            "Windows argument escaping");
    LaunchLock lock(root / "launch.lock");
    rejects(
        [&] {
            LaunchLock second(root / "launch.lock");
        },
        "concurrent launch exclusion");
    Process child;
    child.start({text(executablePath()), "--help"}, environment(), root, root / "child.log");
    require(child.wait() == 0 &&
                readFile(root / "child.log").find("Dirt Town VR") != std::string::npos,
            "child process and Unicode paths");
}
} // namespace launcher
