#pragma once
#include "platform.hpp"
#include <json.hpp>
namespace launcher {
using Json = nlohmann::json;
inline constexpr const char* appId = "5471710";
inline constexpr const char* gameHash =
    "4f830861d4be3afd43cf45b2056a48afc716118efec2f3b28380766f8b9cc97f";
std::string readFile(const fs::path& file);
void writeFile(const fs::path& file, const std::string& data);
std::string sha256(const std::string& data);
std::string fileHash(const fs::path& file);
struct Paths {
    fs::path config, cache, logs, binaries, legacy;
};
struct Config {
    std::string game, proton, runtime, graphics = "auto";
    double scale = .75;
    bool controllers = true, antialiasing = false;
    Json json() const;
    std::string scaleArgument() const;
    static Config fromJson(const Json& value);
    static Config load(const Paths& dirs);
    void save(const Paths& dirs) const;
    void validate() const;
};
std::vector<fs::path> steamRoots();
std::vector<fs::path> steamLibraries(const std::vector<fs::path>& roots);
std::vector<std::string> vdfPaths(const std::string& data);
std::string discoverGame();
struct Proton {
    fs::path executable, prefix, client;
};
Proton findProton(const Config& config, const std::vector<fs::path>& roots = steamRoots());
std::string windowsPath(const fs::path& file);
void validateGame(const Config& config);
Environment runtimeEnvironment(const Config& config, const Paths& dirs,
                               Environment vars = environment());
Json inspectRuntime(const Paths& dirs, const Environment& vars);
struct Plan {
    std::vector<std::string> command;
    Environment vars;
    fs::path cwd;
};
Plan makePlan(const Config& config, const Paths& dirs, const std::string& command,
              bool diagnostic = false);
int run(const Config& config, const Paths& dirs, const std::string& command,
        bool diagnostic = false);
Paths prepare();
int gui(const Paths& dirs, Config config, bool smoke = false);
void selfTest();
} // namespace launcher
