#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <vector>

static HANDLE child = nullptr;
static void require(bool ok, const char* message) {
    if (!ok) {
        DWORD error = GetLastError();
        if (child)
            TerminateProcess(child, 1);
        std::fprintf(stderr, "%s (Win32 %lu)\n", message, error);
        ExitProcess(1);
    }
}
int wmain(int argc, wchar_t** argv) {
    wchar_t filename[32768];
    GetModuleFileNameW(nullptr, filename, 32768);
    wchar_t logDirectory[32768]{};
    GetEnvironmentVariableW(L"DIRTTOWN_VR_LOG_DIR", logDirectory, 32768);
    std::wstring report = logDirectory;
    if (report.empty()) {
        report = filename;
        report.resize(report.find_last_of(L"\\/"));
    }
    report += L"/vr-launch.log";
    _wfreopen(report.c_str(), L"w", stderr);
    setvbuf(stderr, nullptr, _IONBF, 0);
    if (argc != 3) {
        std::fprintf(stderr, "Usage: vr-launch.exe GAME_EXE INJECTOR_DLL\n");
        return 2;
    }
    std::wstring game = argv[1], dll = argv[2];
    std::wstring directory = game.substr(0, game.find_last_of(L"\\/"));
    std::wstring command = L"\"" + game + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                           nullptr, directory.c_str(), &startup, &process),
            "Create suspended game");
    child = process.hProcess;
    const SIZE_T size = (dll.size() + 1) * sizeof(wchar_t);
    void* remote =
        VirtualAllocEx(process.hProcess, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        TerminateProcess(process.hProcess, 1);
        require(false, "Allocate DLL path");
    }
    require(WriteProcessMemory(process.hProcess, remote, dll.c_str(), size, nullptr),
            "Write DLL path");
    auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process.hProcess, nullptr, 0, load, remote, 0, nullptr);
    require(thread != nullptr, "Load injector");
    require(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0, "Wait for injector");
    DWORD result = 0;
    GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    VirtualFreeEx(process.hProcess, remote, 0, MEM_RELEASE);
    if (!result) {
        TerminateProcess(process.hProcess, 1);
        require(false, "Injector DLL failed to load");
    }
    uintptr_t remoteModule = 0;
    HANDLE snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process.dwProcessId);
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry))
        do {
            if (_wcsicmp(entry.szExePath, dll.c_str()) == 0)
                remoteModule = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
        } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    HMODULE localModule = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    auto initialize = GetProcAddress(localModule, "Initialize");
    if (!remoteModule || !initialize) {
        TerminateProcess(process.hProcess, 1);
        require(false, "Find injector initializer");
    }
    auto remoteInitialize = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        remoteModule + reinterpret_cast<uintptr_t>(initialize) -
        reinterpret_cast<uintptr_t>(localModule));
    thread = CreateRemoteThread(process.hProcess, nullptr, 0, remoteInitialize,
                                reinterpret_cast<void*>(remoteModule), 0, nullptr);
    require(thread != nullptr, "Initialize render hooks");
    require(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0, "Wait for render hooks");
    GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    FreeLibrary(localModule);
    if (result) {
        TerminateProcess(process.hProcess, 1);
        require(false, "Render hook initialization failed; see inject.log");
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    std::printf("Started Dirt Town with experimental render hooks. PID %lu\n", process.dwProcessId);
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hProcess);
    child = nullptr;
    return (int)result;
}
