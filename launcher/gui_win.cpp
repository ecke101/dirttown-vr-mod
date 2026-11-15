#include "gui.hpp"
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <functional>

namespace launcher {
class WindowsApp {
    GuiModel model;
    HWND window = nullptr, game = nullptr, runtime = nullptr, scale = nullptr, graphics = nullptr,
         controllers = nullptr, taa = nullptr, status = nullptr, resolution = nullptr;
    HFONT font = nullptr, titleFont = nullptr;
    struct Item {
        HWND hwnd;
        int x, y, w, h;
    };
    std::vector<Item> items;
    std::vector<HWND> actions;
    std::vector<double> scales{.25, .5, .625, .75, 1, 1.25, 1.5};
    float dpi = 1;
    bool smoke = false;
    ULONGLONG started = 0;
    int px(int n) const {
        return (int)std::lround(n * dpi);
    }
    static std::string value(HWND w) {
        int count = GetWindowTextLengthW(w);
        std::wstring s(count + 1, 0);
        GetWindowTextW(w, s.data(), count + 1);
        s.resize(count);
        return utf8(s);
    }
    void set(HWND w, const std::string& s) {
        SetWindowTextW(w, wide(s).c_str());
    }
    void attempt(const std::function<void()>& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            model.status = e.what();
            set(status, model.status);
            MessageBoxW(window, wide(e.what()).c_str(), L"Dirt Town VR", MB_OK | MB_ICONERROR);
        }
    }
    HWND add(const wchar_t* kind, const std::wstring& caption, int style, int id, int x, int y,
             int w, int h, bool title = false) {
        auto control =
            CreateWindowExW(wcscmp(kind, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, kind,
                            caption.c_str(), WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w),
                            px(h), window, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, (WPARAM)(title ? titleFont : font), TRUE);
        items.push_back({control, x, y, w, h});
        return control;
    }
    void browse(HWND entry, bool directory) {
        IFileOpenDialog* dialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog));
        if (FAILED(hr))
            throw std::runtime_error("Cannot open the file picker");
        DWORD flags;
        dialog->GetOptions(&flags);
        dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | (directory ? FOS_PICKFOLDERS : 0));
        if (SUCCEEDED(dialog->Show(window))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR file = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &file))) {
                    SetWindowTextW(entry, file);
                    CoTaskMemFree(file);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    Config preferences() {
        auto c = model.config;
        c.game = value(game);
        c.runtime = value(runtime);
        c.scale = scales.at((size_t)SendMessageW(scale, CB_GETCURSEL, 0, 0));
        c.graphics = value(graphics);
        c.controllers = SendMessageW(controllers, BM_GETCHECK, 0, 0) == BST_CHECKED;
        c.antialiasing = SendMessageW(taa, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return c;
    }
    void refresh() {
        set(status, model.status);
        set(resolution,
            model.resolution(scales.at((size_t)SendMessageW(scale, CB_GETCURSEL, 0, 0))));
        const bool enabled = model.available();
        for (auto button : actions)
            EnableWindow(button, enabled);
    }
    void command(unsigned id) {
        if (id == 101)
            browse(game, true);
        else if (id == 102)
            browse(runtime, false);
        else if (id >= 201 && id <= 204) {
            const char* names[] = {"runtime", "launch", "probe", "gpu-test"};
            model.start(names[id - 201], preferences());
            refresh();
        } else if (id == 205)
            openPath(model.dirs.config / "controls.ini");
        else if (id == 206)
            openPath(model.dirs.logs);
        else if (id == 207)
            openPath(model.dirs.cache / "licenses");
        else if (id == 103)
            refresh();
    }
    static LRESULT CALLBACK proc(HWND w, UINT message, WPARAM a, LPARAM b) {
        auto self = reinterpret_cast<WindowsApp*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<WindowsApp*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);
            SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (self) {
            if (message == WM_COMMAND) {
                self->attempt([&] {
                    self->command(LOWORD(a));
                });
                return 0;
            }
            if (message == WM_TIMER) {
                if (self->smoke && GetTickCount64() - self->started >= 1000)
                    DestroyWindow(w);
                else
                    self->attempt([&] {
                        self->model.poll();
                        self->refresh();
                    });
                return 0;
            }
            if (message == WM_SIZE) {
                const int extra = (int)LOWORD(b) - self->px(740);
                for (const auto& item : self->items) {
                    const bool field = item.hwnd == self->game || item.hwnd == self->runtime ||
                                       item.hwnd == self->status || item.hwnd == self->resolution;
                    const bool browse =
                        GetDlgCtrlID(item.hwnd) == 101 || GetDlgCtrlID(item.hwnd) == 102;
                    MoveWindow(item.hwnd, self->px(item.x) + (browse ? extra : 0), self->px(item.y),
                               self->px(item.w) + (field ? extra : 0), self->px(item.h), TRUE);
                }
                return 0;
            }
            if (message == WM_GETMINMAXINFO) {
                auto limits = reinterpret_cast<MINMAXINFO*>(b);
                limits->ptMinTrackSize = {self->px(760), self->px(615)};
                return 0;
            }
            if (message == WM_DESTROY) {
                PostQuitMessage(0);
                return 0;
            }
        }
        return DefWindowProcW(w, message, a, b);
    }

  public:
    WindowsApp(const Paths& dirs, Config config, bool smokeTest)
        : model(dirs, config), smoke(smokeTest) {
        auto user = GetModuleHandleW(L"user32.dll");
        auto aware = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(
            GetProcAddress(user, "SetProcessDpiAwarenessContext"));
        if (aware)
            aware((HANDLE)-4);
        else
            SetProcessDPIAware();
        auto dc = GetDC(nullptr);
        dpi = GetDeviceCaps(dc, LOGPIXELSX) / 96.f;
        ReleaseDC(nullptr, dc);
        font = CreateFontW(-px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
        titleFont = CreateFontW(-px(28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DirtTownVRLauncher";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        RECT bounds{0, 0, px(740), px(575)};
        AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT);
        window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Dirt Town VR Mod",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                 bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr,
                                 nullptr, wc.hInstance, this);
        if (!window)
            throw std::runtime_error("Cannot open launcher window");
        add(L"STATIC", L"Dirt Town VR", 0, 0, 22, 16, 650, 38, true);
        add(L"STATIC", L"OpenXR | Demo build 26658761", 0, 0, 22, 58, 650, 22);
        add(L"STATIC", L"Game installation", 0, 0, 22, 92, 650, 22);
        game = add(L"EDIT", wide(config.game), ES_AUTOHSCROLL | WS_TABSTOP, 10, 22, 118, 592, 28);
        add(L"BUTTON", L"Browse...", WS_TABSTOP, 101, 624, 118, 94, 28);
        add(L"STATIC", L"OpenXR runtime manifest (blank = active runtime)", 0, 0, 22, 160, 680, 22);
        runtime =
            add(L"EDIT", wide(config.runtime), ES_AUTOHSCROLL | WS_TABSTOP, 11, 22, 186, 592, 28);
        add(L"BUTTON", L"Browse...", WS_TABSTOP, 102, 624, 186, 94, 28);
        add(L"STATIC", L"Resolution:", 0, 0, 22, 239, 90, 24);
        scale = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 103, 112, 235,
                    108, 230);
        if (std::find(scales.begin(), scales.end(), config.scale) == scales.end())
            scales.push_back(config.scale);
        for (size_t i = 0; i < scales.size(); ++i) {
            wchar_t caption[32];
            swprintf(caption, 32, L"%g%%", scales[i] * 100);
            SendMessageW(scale, CB_ADDSTRING, 0, (LPARAM)caption);
            if (scales[i] == config.scale)
                SendMessageW(scale, CB_SETCURSEL, i, 0);
        }
        add(L"STATIC", L"Graphics:", 0, 0, 250, 239, 78, 24);
        graphics = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 104, 330, 235, 110, 150);
        size_t index = 0;
        for (const char* choice : {"auto", "d3d12", "d3d11"}) {
            SendMessageW(graphics, CB_ADDSTRING, 0, (LPARAM)wide(choice).c_str());
            if (config.graphics == choice)
                SendMessageW(graphics, CB_SETCURSEL, index, 0);
            ++index;
        }
        resolution = add(L"STATIC", L"", 0, 0, 22, 276, 690, 24);
        controllers = add(L"BUTTON", L"Use VR controllers", BS_AUTOCHECKBOX | WS_TABSTOP, 105, 22,
                          310, 240, 25);
        SendMessageW(controllers, BM_SETCHECK, config.controllers ? BST_CHECKED : BST_UNCHECKED, 0);
        taa = add(L"BUTTON", L"Temporal antialiasing", BS_AUTOCHECKBOX | WS_TABSTOP, 106, 280, 310,
                  240, 25);
        SendMessageW(taa, BM_SETCHECK, config.antialiasing ? BST_CHECKED : BST_UNCHECKED, 0);
        add(L"BUTTON", L"Controller settings...", WS_TABSTOP, 205, 22, 346, 175, 28);
        status = add(L"STATIC", L"", 0, 0, 22, 390, 690, 44);
        int x = 22;
        const wchar_t* captions[] = {L"Detect headset", L"Launch VR", L"Test headset",
                                     L"GPU self-test"};
        for (unsigned i = 0; i < 4; ++i) {
            actions.push_back(add(L"BUTTON", captions[i], WS_TABSTOP, 201 + i, x, 438, 155, 32));
            x += 169;
        }
        add(L"STATIC",
            L"Load a site on the monitor. F9: recenter | F10: VR on/off | F11: antialiasing\nRight stick: look on foot, implement in dozer/loader. Menus/HUD on monitor.",
            0, 0, 22, 486, 690, 44);
        add(L"BUTTON", L"Open logs", WS_TABSTOP, 206, 22, 538, 120, 28);
        add(L"BUTTON", L"Licences", WS_TABSTOP, 207, 154, 538, 120, 28);
        started = GetTickCount64();
        refresh();
        SetTimer(window, 1, 500, nullptr);
        ShowWindow(window, SW_SHOWNORMAL);
    }
    ~WindowsApp() {
        DeleteObject(font);
        DeleteObject(titleFont);
    }
    int run() {
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        return (int)message.wParam;
    }
};
int gui(const Paths& dirs, Config config, bool smoke) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WindowsApp app(dirs, config, smoke);
    const int code = app.run();
    CoUninitialize();
    return code;
}
void guiError(const std::string& message) {
    MessageBoxW(nullptr, wide(message).c_str(), L"Dirt Town VR", MB_OK | MB_ICONERROR);
}
} // namespace launcher
