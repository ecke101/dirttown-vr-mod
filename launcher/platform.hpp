#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace launcher {
namespace fs=std::filesystem;
inline fs::path path(const std::string& value) {return fs::u8path(value);}
inline std::string text(const fs::path& value) {return value.u8string();}
struct EnvLess {bool operator()(const std::string& a,const std::string& b) const;};
using Environment=std::map<std::string,std::string,EnvLess>;
Environment environment();
std::string env(const std::string& name,const std::string& fallback="");
fs::path executablePath();
fs::path homePath();
fs::path configPath();
fs::path cachePath();
fs::path statePath();
void privateDirectory(const fs::path& folder);
void replaceFile(const fs::path& temporary,const fs::path& target);
bool gameRunning();
unsigned processId();
std::wstring wide(const std::string& value);
std::string utf8(const std::wstring& value);
std::wstring quoteWindows(const std::wstring& value);
class LaunchLock {
    intptr_t handle=-1;
public:
    explicit LaunchLock(const fs::path& file);
    ~LaunchLock();
    LaunchLock(const LaunchLock&)=delete;
    LaunchLock& operator=(const LaunchLock&)=delete;
};
class Process {
    intptr_t handle=-1;
public:
    Process()=default;
    ~Process();
    Process(const Process&)=delete;
    Process& operator=(const Process&)=delete;
    void start(const std::vector<std::string>& command,const Environment& vars,const fs::path& cwd,const fs::path& log);
    std::optional<int> poll();
    int wait();
};
void openPath(const fs::path& file);
} // namespace launcher
