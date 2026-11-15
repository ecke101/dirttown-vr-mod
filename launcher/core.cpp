#include "core.hpp"
#include <picosha2.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace launcher {
std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read " + text(file));
    std::string data{std::istreambuf_iterator<char>(in), {}};
    if (in.bad())
        throw std::runtime_error("Cannot read " + text(file));
    return data;
}
void writeFile(const fs::path& file, const std::string& data) {
    privateDirectory(file.parent_path());
    auto temp = file;
    temp += "." + std::to_string(processId()) + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(data.data(), (std::streamsize)data.size());
        out.close();
        if (!out)
            throw std::runtime_error("Cannot write " + text(temp));
    }
    replaceFile(temp, file);
}
std::string sha256(const std::string& data) {
    return picosha2::hash256_hex_string(data);
}
std::string fileHash(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read " + text(file));
    const auto hash = picosha2::hash256_hex_string(std::istreambuf_iterator<char>(in),
                                                   std::istreambuf_iterator<char>());
    if (in.bad())
        throw std::runtime_error("Cannot read " + text(file));
    return hash;
}
Json Config::json() const {
    return {{"game", game},
            {"proton", proton},
            {"runtime", runtime},
            {"graphics", graphics},
            {"scale", scale},
            {"controllers", controllers},
            {"antialiasing", antialiasing}};
}
std::string Config::scaleArgument() const {
    // GTK sets the numeric locale. Worker arguments and the injected DLL's
    // environment must still use a decimal point, regardless of that locale.
    return Json(scale).dump();
}
Config Config::fromJson(const Json& j) {
    Config c;
    c.game = j.value("game", c.game);
    c.proton = j.value("proton", c.proton);
    c.runtime = j.value("runtime", c.runtime);
    c.graphics = j.value("graphics", c.graphics);
    c.scale = j.value("scale", c.scale);
    c.controllers = j.value("controllers", c.controllers);
    c.antialiasing = j.value("antialiasing", c.antialiasing);
    c.validate();
    return c;
}
Config Config::load(const Paths& dirs) {
    const auto current = dirs.config / "config.json", legacy = dirs.legacy / "config.json";
    Config c;
    if (fs::is_regular_file(current))
        c = fromJson(Json::parse(readFile(current)));
    else if (!dirs.legacy.empty() && fs::is_regular_file(legacy))
        c = fromJson(Json::parse(readFile(legacy)));
    if (c.game.empty())
        c.game = discoverGame();
    return c;
}
void Config::save(const Paths& dirs) const {
    validate();
    writeFile(dirs.config / "config.json", json().dump(2) + '\n');
}
void Config::validate() const {
    if (!std::isfinite(scale) || scale < .25 || scale > 1.5)
        throw std::runtime_error("Resolution must be between 25% and 150%");
    if (graphics != "auto" && graphics != "d3d12" && graphics != "d3d11")
        throw std::runtime_error("Graphics must be auto, d3d12 or d3d11");
}
std::vector<fs::path> steamRoots() {
    std::vector<fs::path> roots;
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        wchar_t value[32768]{};
        DWORD size = sizeof(value), type = 0;
        if (RegQueryValueExW(key, L"SteamPath", nullptr, &type, (BYTE*)value, &size) ==
                ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ))
            roots.emplace_back(value);
        RegCloseKey(key);
    }
    roots.push_back(path(env("ProgramFiles(x86)", "C:\\Program Files (x86)")) / "Steam");
#else
    for (const char* name : {".local/share/Steam", ".steam/steam",
                             ".var/app/com.valvesoftware.Steam/.local/share/Steam"})
        roots.push_back(homePath() / name);
#endif
    return roots;
}
std::vector<std::string> vdfPaths(const std::string& data) {
    // Read quoted VDF tokens, including escaped Windows paths.
    std::vector<std::string> tokens, result;
    for (size_t i = 0; i < data.size(); ++i)
        if (data[i] == '"') {
            std::string token;
            bool closed = false;
            while (++i < data.size()) {
                char c = data[i];
                if (c == '"') {
                    closed = true;
                    break;
                }
                if (c == '\\' && i + 1 < data.size()) {
                    char next = data[i + 1];
                    if (next == '\\' || next == '"') {
                        c = next;
                        ++i;
                    }
                }
                token += c;
            }
            if (closed)
                tokens.push_back(token);
        }
    for (size_t i = 1; i < tokens.size(); ++i)
        if (tokens[i - 1] == "path")
            result.push_back(tokens[i]);
    return result;
}
std::vector<fs::path> steamLibraries(const std::vector<fs::path>& roots) {
    auto libraries = roots;
    for (const auto& root : roots) {
        const auto file = root / "steamapps/libraryfolders.vdf";
        if (fs::is_regular_file(file))
            for (const auto& value : vdfPaths(readFile(file))) {
                auto candidate = path(value);
                if (std::find(libraries.begin(), libraries.end(), candidate) == libraries.end())
                    libraries.push_back(candidate);
            }
    }
    return libraries;
}
std::string discoverGame() {
    for (const auto& root : steamLibraries(steamRoots())) {
        auto game = root / "steamapps/common/Dirt Town Demo";
        if (fs::is_regular_file(game / "dirttown.exe"))
            return text(game);
    }
    return {};
}
Proton findProton(const Config& config, const std::vector<fs::path>& roots) {
    const auto game = config.game.empty() ? fs::path{} : fs::absolute(path(config.game));
    Proton p;
    if (!game.empty())
        p.prefix = game.parent_path().parent_path() / "compatdata" / appId;
    if (!config.proton.empty()) {
        p.executable = fs::absolute(path(config.proton));
        if (fs::is_directory(p.executable))
            p.executable /= "proton";
    } else {
        const auto recorded = p.prefix / "config_info";
        if (!p.prefix.empty() && fs::is_regular_file(recorded)) {
            std::istringstream lines(readFile(recorded));
            std::string line;
            const std::string marker = "/files/share/fonts/";
            while (std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.size() >= marker.size() &&
                    line.compare(line.size() - marker.size(), marker.size(), marker) == 0) {
                    auto candidate = path(line.substr(0, line.size() - marker.size())) / "proton";
                    if (fs::is_regular_file(candidate))
                        p.executable = candidate;
                    break;
                }
            }
        }
        if (p.executable.empty())
            for (const auto& root : steamLibraries(roots)) {
                for (const char* name : {"Proton - Experimental", "Proton 11.0", "Proton 10.0"}) {
                    auto candidate = root / "steamapps/common" / name / "proton";
                    if (fs::is_regular_file(candidate)) {
                        p.executable = candidate;
                        break;
                    }
                }
                if (!p.executable.empty())
                    break;
            }
    }
    if (p.executable.empty() || !fs::is_regular_file(p.executable))
        throw std::runtime_error(
            "No Proton found. Start the game once through Steam or select a Proton executable.");
    p.executable = fs::canonical(p.executable);
    for (const auto& root : roots)
        if (fs::is_directory(root)) {
            p.client = root;
            break;
        }
    if (p.client.empty() && !game.empty())
        p.client = game.parent_path().parent_path().parent_path();
    if (p.client.empty())
        throw std::runtime_error(
            "Steam installation not found. Start Steam before running diagnostics.");
    return p;
}
std::string windowsPath(const fs::path& file) {
#ifdef _WIN32
    return text(fs::absolute(file));
#else
    auto s = text(fs::absolute(file));
    std::replace(s.begin(), s.end(), '/', '\\');
    return "Z:" + s;
#endif
}
void validateGame(const Config& config) {
    if (config.game.empty() || !fs::is_regular_file(path(config.game) / "dirttown.exe"))
        throw std::runtime_error("Select the Dirt Town Demo folder containing dirttown.exe");
    if (fileHash(path(config.game) / "dirttown.exe") != gameHash)
        throw std::runtime_error(
            "Unsupported game executable. This mod supports Steam build 26658761 only.");
}
Environment runtimeEnvironment(const Config& config, const Paths& dirs, Environment vars) {
    auto selected = config.runtime.empty()
                        ? (vars.count("XR_RUNTIME_JSON") ? vars.at("XR_RUNTIME_JSON") : "")
                        : config.runtime;
    if (selected.empty())
        return vars;
    const auto source = fs::canonical(path(selected));
    auto j = Json::parse(readFile(source));
    if (!j.contains("runtime") || !j["runtime"].is_object() ||
        !j["runtime"].contains("library_path") || !j["runtime"]["library_path"].is_string() ||
        j["runtime"]["library_path"].get<std::string>().empty())
        throw std::runtime_error("Runtime manifest has no library_path");
#ifdef _WIN32
    vars["XR_RUNTIME_JSON"] = text(source);
#else
    for (const char* key : {"library_path", "MND_libmonado_path"})
        if (j["runtime"].contains(key) && j["runtime"][key].is_string()) {
            auto s = j["runtime"][key].get<std::string>();
            if (!s.empty() && path(s).is_relative())
                j["runtime"][key] = text((source.parent_path() / path(s)).lexically_normal());
        }
    auto overlay = dirs.config / "runtimes" / sha256(text(source)).substr(0, 16);
    writeFile(overlay / "openxr/1/active_runtime.json", j.dump(2) + '\n');
    const auto configured =
        vars.count("XDG_CONFIG_HOME") ? path(vars.at("XDG_CONFIG_HOME")) : fs::path{};
    const auto previous = text(configured.is_absolute() ? configured : homePath() / ".config");
    const auto openvr = path(previous) / "openvr/openvrpaths.vrpath";
    if (!vars.count("VR_PATHREG_OVERRIDE") && fs::is_regular_file(openvr))
        vars["VR_PATHREG_OVERRIDE"] = text(openvr);
    vars["XDG_CONFIG_HOME"] = text(overlay);
    vars["XDG_CONFIG_DIRS"] =
        previous + ":" + (vars.count("XDG_CONFIG_DIRS") ? vars.at("XDG_CONFIG_DIRS") : "/etc/xdg");
    vars.erase("XR_RUNTIME_JSON");
#endif
    return vars;
}
Plan makePlan(const Config& config, const Paths& dirs, const std::string& command,
              bool diagnostic) {
    config.validate();
    const bool game = command == "launch";
    if (game && config.game.empty())
        throw std::runtime_error("Select the Dirt Town Demo folder containing dirttown.exe");
    Plan p;
    p.vars = runtimeEnvironment(config, dirs);
    const auto exe = game                     ? "vr-launch.exe"
                     : command == "probe"     ? "vr-probe.exe"
                     : command == "gpu-test"  ? "temporal-test.exe"
                     : command == "core-test" ? "core-test.exe"
                                              : "eye-transfer-test.exe";
    for (const std::string name : {std::string(exe), std::string("openxr_loader.dll"),
                                   game ? "dirttown-vr.dll" : std::string(exe)})
        if (!fs::is_regular_file(dirs.binaries / name))
            throw std::runtime_error("Missing " + name +
                                     ". Rebuild or download a complete launcher.");
    p.vars["DIRTTOWN_VR_RESOLUTION_SCALE"] = config.scaleArgument();
    p.vars["DIRTTOWN_VR_CONTROLLERS"] = config.controllers ? "1" : "0";
    p.vars["DIRTTOWN_VR_TAA"] = config.antialiasing ? "1" : "0";
    p.vars["DIRTTOWN_VR_GRAPHICS"] = config.graphics;
    p.vars["SteamAppId"] = p.vars["SteamGameId"] = game ? appId : "0";
    if (diagnostic)
        p.vars["DIRTTOWN_VR_DIAGNOSTIC"] = "1";
    else
        p.vars.erase("DIRTTOWN_VR_DIAGNOSTIC");
    privateDirectory(dirs.logs);
    p.vars["DIRTTOWN_VR_LOG_DIR"] = windowsPath(dirs.logs);
    p.vars["DIRTTOWN_VR_CONTROLS"] = windowsPath(dirs.config / "controls.ini");
#ifdef _WIN32
    p.command = {text(dirs.binaries / exe)};
#else
    auto proton = findProton(config);
    auto prefix = game ? proton.prefix : dirs.config / "probe-prefix";
    fs::create_directories(prefix);
    p.vars["STEAM_COMPAT_DATA_PATH"] = text(prefix);
    p.vars["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = text(proton.client);
    p.vars["SteamAppId"] = p.vars["SteamGameId"] = game ? appId : "0";
    p.vars["PROTON_LOG"] = "1";
    p.vars["PROTON_LOG_DIR"] = text(dirs.logs);
    p.vars["PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES"] = "1";
    p.vars.erase("XR_RUNTIME_JSON");
    p.command = {text(proton.executable), "run", text(dirs.binaries / exe)};
#endif
    if (game) {
        p.command.push_back(windowsPath(path(config.game) / "dirttown.exe"));
        p.command.push_back(windowsPath(dirs.binaries / "dirttown-vr.dll"));
    }
    p.cwd = game ? fs::absolute(path(config.game)) : dirs.logs;
    return p;
}
int run(const Config& config, const Paths& dirs, const std::string& command, bool diagnostic) {
    LaunchLock lock(dirs.config / "launch.lock");
    if (gameRunning())
        throw std::runtime_error("Quit Dirt Town before launching a game or GPU/headset test.");
    if (command == "launch")
        validateGame(config);
    auto plan = makePlan(config, dirs, command, diagnostic);
    if (!diagnostic && command != "gpu-test" && command != "core-test" && command != "eye-test")
        std::cout << inspectRuntime(dirs, plan.vars).dump(2) << std::endl;
    Process child;
    auto log = dirs.logs / (command == "launch" ? "launch.log" : "test.log");
    child.start(plan.command, plan.vars, plan.cwd, log);
    const int code = child.wait();
    std::cout << "Exit: " << code << " Log: " << text(log) << std::endl;
    return code;
}
} // namespace launcher
