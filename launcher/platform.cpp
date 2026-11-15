#include "platform.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace launcher {
bool EnvLess::operator()(const std::string& a,const std::string& b) const {
#ifdef _WIN32
    return _stricmp(a.c_str(),b.c_str())<0;
#else
    return a<b;
#endif
}
std::string env(const std::string& name,const std::string& fallback) {
    const auto vars=environment();auto it=vars.find(name);return it==vars.end()?fallback:it->second;
}
#ifdef _WIN32
std::wstring wide(const std::string& s) {
    if(s.empty())return {};
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if(!count)throw std::runtime_error("Invalid UTF-8 path");
    std::wstring result(count,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),result.data(),count);return result;
}
std::string utf8(const std::wstring& s) {
    if(s.empty())return {};
    int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(!count)throw std::runtime_error("Invalid Windows path");
    std::string result(count,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),result.data(),count,nullptr,nullptr);return result;
}
#endif
std::wstring quoteWindows(const std::wstring& s) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t c:s) {
        if(c==L'\\') {++slashes;continue;}
        out.append(c==L'"'?slashes*2+1:slashes,L'\\');out+=c;slashes=0;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
Environment environment() {
    Environment out;
#ifdef _WIN32
    wchar_t* block=GetEnvironmentStringsW();if(!block)throw std::runtime_error("Cannot read process environment");
    for(const wchar_t* p=block;*p;p+=wcslen(p)+1) {
        const std::wstring entry=p;auto equal=entry.find(L'=',entry[0]==L'='?1:0);
        if(equal!=std::wstring::npos)out[utf8(entry.substr(0,equal))]=utf8(entry.substr(equal+1));
    }
    FreeEnvironmentStringsW(block);
#else
    for(char** p=environ;*p;++p) {std::string entry=*p;auto equal=entry.find('=');if(equal!=std::string::npos)out[entry.substr(0,equal)]=entry.substr(equal+1);}
#endif
    return out;
}
fs::path executablePath() {
#ifdef _WIN32
    std::wstring file(32768,0);DWORD size=GetModuleFileNameW(nullptr,file.data(),(DWORD)file.size());
    if(!size || size==file.size())throw std::runtime_error("Cannot find launcher executable");file.resize(size);return fs::path(file);
#else
    return fs::read_symlink("/proc/self/exe");
#endif
}
fs::path homePath() {
#ifdef _WIN32
    return path(env("USERPROFILE"));
#else
    return path(env("HOME"));
#endif
}
fs::path configPath() {
#ifdef _WIN32
    return path(env("LOCALAPPDATA",text(homePath()/"AppData/Local")))/"DirtTownVR";
#else
    return path(env("XDG_CONFIG_HOME",text(homePath()/".config")))/"dirttown-vr-mod";
#endif
}
fs::path cachePath() {
#ifdef _WIN32
    return configPath()/"cache";
#else
    return path(env("XDG_CACHE_HOME",text(homePath()/".cache")))/"dirttown-vr-mod";
#endif
}
fs::path statePath() {
#ifdef _WIN32
    return configPath();
#else
    return path(env("XDG_STATE_HOME",text(homePath()/".local/state")))/"dirttown-vr-mod";
#endif
}
void privateDirectory(const fs::path& folder) {
    if(folder.empty())return;
    const bool created=fs::create_directories(folder);
#ifndef _WIN32
    if(created && chmod(folder.c_str(),0700))throw std::runtime_error("Cannot secure launcher directory: "+text(folder));
#else
    (void)created;
#endif
}
void replaceFile(const fs::path& temporary,const fs::path& target) {
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace file: "+text(target));
#else
    fs::rename(temporary,target);
#endif
}
unsigned processId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return (unsigned)getpid();
#endif
}
bool gameRunning() {
#ifdef _WIN32
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check running processes");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
    if(Process32FirstW(snapshot,&entry))do {if(_wcsicmp(entry.szExeFile,L"dirttown.exe")==0) {found=true;break;}}while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
#else
    for(const auto& entry:fs::directory_iterator("/proc")) {
        const auto name=entry.path().filename().string();if(name.empty() || name.find_first_not_of("0123456789")!=std::string::npos)continue;
        std::ifstream file(entry.path()/"comm");std::string comm;std::getline(file,comm);
        if(comm=="dirttown.exe")return true;
    }
    return false;
#endif
}
LaunchLock::LaunchLock(const fs::path& file) {
    privateDirectory(file.parent_path());
#ifdef _WIN32
    HANDLE h=CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    OVERLAPPED offset{};
    if(h==INVALID_HANDLE_VALUE || !LockFileEx(h,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&offset)) {
        if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);throw std::runtime_error("Another VR launch or test is running");
    }
    handle=(intptr_t)h;
#else
    int fd=open(file.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(fd<0 || flock(fd,LOCK_EX|LOCK_NB)) {if(fd>=0)close(fd);throw std::runtime_error("Another VR launch or test is running");}
    handle=fd;
#endif
}
LaunchLock::~LaunchLock() {
    if(handle==-1)return;
#ifdef _WIN32
    CloseHandle((HANDLE)handle);
#else
    close((int)handle);
#endif
}
Process::~Process() {
#ifdef _WIN32
    if(handle!=-1)CloseHandle((HANDLE)handle);
#endif
}
void Process::start(const std::vector<std::string>& command,const Environment& vars,const fs::path& cwd,const fs::path& log) {
    if(command.empty() || handle!=-1)throw std::runtime_error("Invalid process launch");
    privateDirectory(log.parent_path());
#ifdef _WIN32
    std::wstring line;for(const auto& arg:command) {if(!line.empty())line+=L' ';line+=quoteWindows(wide(arg));}
    std::vector<wchar_t> block;for(const auto& entry:vars) {const auto s=wide(entry.first+"="+entry.second);block.insert(block.end(),s.begin(),s.end());block.push_back(0);}block.push_back(0);
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE output=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE || input==INVALID_HANDLE_VALUE) {
        if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
        throw std::runtime_error("Cannot open launch log");
    }
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input;startup.hStdOutput=startup.hStdError=output;PROCESS_INFORMATION process{};
    BOOL ok=CreateProcessW(path(command[0]).c_str(),line.data(),nullptr,nullptr,TRUE,CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW,
        block.data(),cwd.empty()?nullptr:cwd.c_str(),&startup,&process);
    CloseHandle(output);CloseHandle(input);
    if(!ok)throw std::runtime_error("Cannot start "+command[0]+" (Win32 "+std::to_string(GetLastError())+")");
    CloseHandle(process.hThread);handle=(intptr_t)process.hProcess;
#else
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    int error=posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,log.c_str(),O_CREAT|O_WRONLY|O_TRUNC,0600);
    if(!error)error=posix_spawn_file_actions_adddup2(&actions,STDOUT_FILENO,STDERR_FILENO);
    if(!error)error=posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    if(!error && !cwd.empty())error=posix_spawn_file_actions_addchdir_np(&actions,cwd.c_str());
    std::vector<std::string> storage;for(const auto& pair:vars)storage.push_back(pair.first+"="+pair.second);
    std::vector<char*> argv,envp;for(const auto& arg:command)argv.push_back(const_cast<char*>(arg.c_str()));argv.push_back(nullptr);
    for(auto& value:storage)envp.push_back(value.data());envp.push_back(nullptr);
    pid_t pid=-1;if(!error)error=posix_spawnp(&pid,argv[0],&actions,nullptr,argv.data(),envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if(error)throw std::runtime_error("Cannot start "+command[0]+": "+std::strerror(error));handle=pid;
#endif
}
std::optional<int> Process::poll() {
    if(handle==-1)return {};
#ifdef _WIN32
    if(WaitForSingleObject((HANDLE)handle,0)==WAIT_TIMEOUT)return {};
    DWORD code=1;if(!GetExitCodeProcess((HANDLE)handle,&code))throw std::runtime_error("Cannot read process exit status");
    CloseHandle((HANDLE)handle);handle=-1;return (int)code;
#else
    int status=0;const auto result=waitpid((pid_t)handle,&status,WNOHANG);
    if(result==0 || (result<0 && errno==EINTR))return {};
    if(result<0)throw std::runtime_error("Cannot read process exit status");handle=-1;
    return WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
#endif
}
int Process::wait() {for(;;) {auto code=poll();if(code)return *code;std::this_thread::sleep_for(std::chrono::milliseconds(50));}}
void openPath(const fs::path& file) {
#ifdef _WIN32
    if((intptr_t)ShellExecuteW(nullptr,L"open",file.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error("Cannot open "+text(file));
#else
    auto child=std::make_shared<Process>();child->start({"/usr/bin/xdg-open",text(file)},environment(),{},statePath()/"logs/open.log");
    // Reap this short-lived helper without keeping the GUI open.
    std::thread([child] {try {child->wait();} catch(...) {}}).detach();
#endif
}
} // namespace launcher
