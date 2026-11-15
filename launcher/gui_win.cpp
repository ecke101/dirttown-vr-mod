#include "gui.hpp"
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <functional>
#include <fstream>

namespace launcher {
class WindowsApp {
    GuiModel model;
    HWND window = nullptr, game = nullptr, runtime = nullptr, scale = nullptr, graphics = nullptr,
         controllers = nullptr, taa = nullptr, status = nullptr, resolution = nullptr;
    HFONT font = nullptr, titleFont = nullptr, launchFont = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(247, 248, 246));
    HWND launch = nullptr, advancedToggle = nullptr, diagnosticsToggle = nullptr;
    bool failed = false, advancedOpen = false, diagnosticsOpen = false, smokeExpanded = false;
    int scroll = 0;
    enum Section { Main, Advanced, DiagnosticsToggle, Diagnostics, Footer };
    struct Item {
        HWND hwnd;
        int x, y, w, h;
        Section section;
        bool stretch, right;
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
        if (value(w) != s)
            SetWindowTextW(w, wide(s).c_str());
    }
    void attempt(const std::function<void()>& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            model.status = e.what();
            failed = true;
            set(status, model.status);
            InvalidateRect(status, nullptr, TRUE);
        }
    }
    HWND add(const wchar_t* kind, const std::wstring& caption, int style, int id, int x, int y,
             int w, int h, bool title = false, Section section = Main, bool stretch = false,
             bool right = false) {
        if (wcscmp(kind, L"BUTTON") == 0 && !(style & BS_AUTOCHECKBOX))
            style |= BS_OWNERDRAW;
        if (wcscmp(kind, L"BUTTON") == 0)
            style |= BS_NOTIFY;
        if (wcscmp(kind, L"STATIC") == 0)
            style |= SS_NOPREFIX;
        auto control =
            CreateWindowExW(wcscmp(kind, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, kind,
                            caption.c_str(), WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w),
                            px(h), window, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, (WPARAM)(title ? titleFont : font), TRUE);
        items.push_back({control, x, y, w, h, section, stretch, right});
        return control;
    }
    void reveal(HWND control) {
        RECT bounds, client;
        GetWindowRect(control, &bounds);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
        GetClientRect(window, &client);
        if (bounds.top < px(8))
            scroll += bounds.top - px(8);
        else if (bounds.bottom > client.bottom - px(8))
            scroll += bounds.bottom - client.bottom + px(8);
        else
            return;
        layout();
    }
    void layout() {
        if (!window)
            return;
        RECT client;
        GetClientRect(window, &client);
        const int content = 630 + (advancedOpen ? 128 : 0) + (diagnosticsOpen ? 128 : 0);
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMax = px(content) - 1;
        info.nPage = client.bottom;
        info.nPos = scroll;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
        info.fMask = SIF_POS;
        GetScrollInfo(window, SB_VERT, &info);
        scroll = info.nPos;
        const int extra = client.right - px(740);
        for (const auto& item : items) {
            const bool visible = (item.section != Advanced || advancedOpen) &&
                                 (item.section != Diagnostics || diagnosticsOpen);
            ShowWindow(item.hwnd, visible ? SW_SHOW : SW_HIDE);
            int y = item.y;
            if (item.section >= DiagnosticsToggle && advancedOpen)
                y += 128;
            if (item.section == Footer && diagnosticsOpen)
                y += 128;
            MoveWindow(item.hwnd, px(item.x) + (item.right ? extra : 0), px(y) - scroll,
                       px(item.w) + (item.stretch ? extra : 0), px(item.h), TRUE);
        }
        InvalidateRect(window, nullptr, TRUE);
    }
    void drawButton(const DRAWITEMSTRUCT& item) {
        FillRect(item.hDC, &item.rcItem, background);
        const bool primary = item.CtlID == 202;
        const bool disabled = item.itemState & ODS_DISABLED;
        const bool pressed = item.itemState & ODS_SELECTED;
        COLORREF fill = primary ? RGB(239, 179, 84) : RGB(255, 255, 255);
        if (pressed)
            fill = primary ? RGB(221, 160, 68) : RGB(229, 233, 227);
        if (disabled)
            fill = RGB(229, 232, 227);
        auto brush = CreateSolidBrush(fill);
        auto pen = CreatePen(PS_SOLID, 1, primary ? fill : RGB(193, 201, 193));
        auto oldBrush = SelectObject(item.hDC, brush);
        auto oldPen = SelectObject(item.hDC, pen);
        RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right,
                  item.rcItem.bottom, px(12), px(12));
        SelectObject(item.hDC, oldBrush);
        SelectObject(item.hDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, disabled ? RGB(126, 137, 129) : RGB(32, 36, 31));
        auto oldFont = SelectObject(item.hDC, primary ? launchFont : font);
        auto caption = wide(value(item.hwndItem));
        RECT text = item.rcItem;
        text.left += px(12);
        text.right -= px(12);
        DrawTextW(item.hDC, caption.c_str(), -1, &text,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX |
                      ((item.CtlID == 208 || item.CtlID == 209) ? DT_LEFT : DT_CENTER));
        if (item.itemState & ODS_FOCUS) {
            RECT focus = item.rcItem;
            InflateRect(&focus, -px(4), -px(4));
            DrawFocusRect(item.hDC, &focus);
        }
        SelectObject(item.hDC, oldFont);
    }
    void capture(const char* name) {
        RECT client;
        GetClientRect(window, &client);
        auto dc = GetDC(window);
        auto memory = CreateCompatibleDC(dc);
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = client.right;
        bitmap.bmiHeader.biHeight = -client.bottom;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        auto image = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
        auto previous = SelectObject(memory, image);
        if (!PrintWindow(window, memory, PW_CLIENTONLY))
            BitBlt(memory, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        const size_t bytes = (size_t)client.right * client.bottom * 4;
        header.bfSize = (DWORD)(header.bfOffBits + bytes);
        std::ofstream out(model.dirs.logs / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(&bitmap.bmiHeader), sizeof(BITMAPINFOHEADER));
        out.write(static_cast<const char*>(pixels), bytes);
        SelectObject(memory, previous);
        DeleteObject(image);
        DeleteDC(memory);
        ReleaseDC(window, dc);
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
        const char* backends[] = {"auto", "d3d12", "d3d11"};
        const auto backend = SendMessageW(graphics, CB_GETCURSEL, 0, 0);
        c.graphics = backend >= 0 && backend < 3 ? backends[backend] : "auto";
        c.controllers = SendMessageW(controllers, BM_GETCHECK, 0, 0) == BST_CHECKED;
        c.antialiasing = SendMessageW(taa, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return c;
    }
    void refresh() {
        const bool enabled = model.available();
        set(status, !enabled && !model.pending && !failed
                        ? "Dirt Town is already open. Close it, then launch VR from here."
                        : model.status);
        set(resolution,
            model.resolution(scales.at((size_t)SendMessageW(scale, CB_GETCURSEL, 0, 0))));
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
            failed = false;
            refresh();
        } else if (id == 205)
            openPath(model.dirs.config / "controls.ini");
        else if (id == 206)
            openPath(model.dirs.logs);
        else if (id == 207)
            openPath(model.dirs.cache / "licenses");
        else if (id == 103)
            refresh();
        else if (id == 208 || id == 209) {
            auto& expanded = id == 208 ? advancedOpen : diagnosticsOpen;
            expanded = !expanded;
            set(id == 208 ? advancedToggle : diagnosticsToggle,
                std::string(expanded ? "v  " : ">  ") +
                    (id == 208 ? "Advanced setup" : "Troubleshooting & controls"));
            layout();
        }
    }
    static LRESULT CALLBACK proc(HWND w, UINT message, WPARAM a, LPARAM b) {
        auto self = reinterpret_cast<WindowsApp*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<WindowsApp*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);
            SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)self);
        }
        if (self) {
            if (message == WM_COMMAND) {
                const auto control = (HWND)b;
                wchar_t kind[32]{};
                GetClassNameW(control, kind, 32);
                const bool button = _wcsicmp(kind, L"BUTTON") == 0;
                const auto event = HIWORD(a);
                if ((button && event == BN_SETFOCUS) ||
                    (!button && (event == EN_SETFOCUS || event == CBN_SETFOCUS)))
                    self->reveal(control);
                if (button && event != BN_CLICKED)
                    return 0;
                self->attempt([&] {
                    self->command(LOWORD(a));
                });
                return 0;
            }
            if (message == WM_TIMER) {
                if (self->smoke && GetTickCount64() - self->started >= 1000) {
                    if (!self->smokeExpanded) {
                        self->capture("launcher-preview.bmp");
                        self->command(208);
                        self->command(209);
                        self->smokeExpanded = true;
                        SetWindowPos(w, nullptr, 0, 0, self->px(760), self->px(930),
                                     SWP_NOMOVE | SWP_NOZORDER);
                        self->layout();
                    } else {
                        self->capture("launcher-advanced-preview.bmp");
                        DestroyWindow(w);
                    }
                } else
                    self->attempt([&] {
                        if (self->model.poll())
                            self->failed = false;
                        self->refresh();
                    });
                return 0;
            }
            if (message == WM_DRAWITEM) {
                self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(b));
                return TRUE;
            }
            if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
                auto dc = (HDC)a;
                SetBkColor(dc, RGB(247, 248, 246));
                SetTextColor(dc, (HWND)b == self->status && self->failed ? RGB(161, 50, 36)
                                                                         : RGB(43, 53, 45));
                return (LRESULT)self->background;
            }
            if (message == WM_SIZE) {
                self->layout();
                return 0;
            }
            if (message == WM_VSCROLL) {
                const auto action = LOWORD(a);
                if (action == SB_THUMBTRACK) {
                    SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
                    GetScrollInfo(w, SB_VERT, &info);
                    self->scroll = info.nTrackPos;
                } else if (action == SB_LINEUP)
                    self->scroll -= self->px(32);
                else if (action == SB_LINEDOWN)
                    self->scroll += self->px(32);
                else if (action == SB_PAGEUP)
                    self->scroll -= self->px(300);
                else if (action == SB_PAGEDOWN)
                    self->scroll += self->px(300);
                self->layout();
                return 0;
            }
            if (message == WM_MOUSEWHEEL) {
                self->scroll -= GET_WHEEL_DELTA_WPARAM(a) * self->px(48) / WHEEL_DELTA;
                self->layout();
                return 0;
            }
            if (message == WM_GETMINMAXINFO) {
                auto limits = reinterpret_cast<MINMAXINFO*>(b);
                limits->ptMinTrackSize = {self->px(760), self->px(520)};
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
        launchFont = CreateFontW(-px(20), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                                 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DirtTownVRLauncher";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = background;
        RegisterClassW(&wc);
        RECT bounds{0, 0, px(740), px(630)};
        AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW | WS_VSCROLL, FALSE, WS_EX_CONTROLPARENT);
        window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Dirt Town VR Mod",
                                 WS_OVERLAPPEDWINDOW | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT,
                                 bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr,
                                 nullptr, wc.hInstance, this);
        if (!window)
            throw std::runtime_error("Cannot open launcher window");
        add(L"STATIC", L"Dirt Town VR", 0, 0, 28, 22, 600, 38, true);
        add(L"STATIC", L"Your next shift, from the driver's seat.", 0, 0, 28, 66, 600, 22);
        add(L"STATIC", L"Game installation", 0, 0, 28, 108, 600, 22);
        game = add(L"EDIT", wide(config.game), ES_AUTOHSCROLL | WS_TABSTOP, 10, 28, 134, 574, 34,
                   false, Main, true);
        add(L"BUTTON", L"Browse...", WS_TABSTOP, 101, 614, 134, 98, 34, false, Main, false, true);
        launch = add(L"BUTTON", L"Launch VR", WS_TABSTOP, 202, 28, 190, 484, 56, false, Main, true);
        actions.push_back(launch);
        actions.push_back(add(L"BUTTON", L"Detect headset", WS_TABSTOP, 201, 524, 190, 188, 56,
                              false, Main, false, true));
        status = add(L"STATIC", L"", 0, 0, 28, 262, 684, 48, false, Main, true);
        add(L"STATIC", L"Display & controls", 0, 0, 28, 326, 400, 24);
        add(L"BUTTON", L"Controller settings...", WS_TABSTOP, 205, 530, 316, 182, 34, false, Main,
            false, true);
        add(L"STATIC", L"Render resolution", 0, 0, 28, 370, 150, 24);
        scale = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 103, 184, 364,
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
        resolution = add(L"STATIC", L"", 0, 0, 308, 366, 404, 44, false, Main, true);
        controllers = add(L"BUTTON", L"Use VR controllers", BS_AUTOCHECKBOX | WS_TABSTOP, 105, 28,
                          422, 230, 28);
        SendMessageW(controllers, BM_SETCHECK, config.controllers ? BST_CHECKED : BST_UNCHECKED, 0);
        taa = add(L"BUTTON", L"Antialiasing (optional)", BS_AUTOCHECKBOX | WS_TABSTOP, 106, 280,
                  422, 250, 28);
        SendMessageW(taa, BM_SETCHECK, config.antialiasing ? BST_CHECKED : BST_UNCHECKED, 0);
        advancedToggle = add(L"BUTTON", L">  Advanced setup", WS_TABSTOP, 208, 28, 478, 684, 36,
                             false, Main, true);
        add(L"STATIC", L"OpenXR runtime manifest (optional)", 0, 0, 28, 530, 680, 22, false,
            Advanced, true);
        runtime = add(L"EDIT", wide(config.runtime), ES_AUTOHSCROLL | WS_TABSTOP, 11, 28, 556, 574,
                      34, false, Advanced, true);
        SendMessageW(runtime, EM_SETCUEBANNER, TRUE, (LPARAM)L"Use the active OpenXR runtime");
        add(L"BUTTON", L"Browse...", WS_TABSTOP, 102, 614, 556, 98, 34, false, Advanced, false,
            true);
        add(L"STATIC", L"Graphics backend", 0, 0, 28, 610, 150, 24, false, Advanced);
        graphics = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 104, 184, 604, 150, 150,
                       false, Advanced);
        const char* choices[] = {"auto", "d3d12", "d3d11"};
        const wchar_t* captions[] = {L"Automatic", L"D3D12", L"D3D11"};
        for (unsigned i = 0; i < 3; ++i) {
            SendMessageW(graphics, CB_ADDSTRING, 0, (LPARAM)captions[i]);
            if (config.graphics == choices[i])
                SendMessageW(graphics, CB_SETCURSEL, i, 0);
        }
        diagnosticsToggle = add(L"BUTTON", L">  Troubleshooting & controls", WS_TABSTOP, 209, 28,
                                526, 684, 36, false, DiagnosticsToggle, true);
        actions.push_back(
            add(L"BUTTON", L"Test headset", WS_TABSTOP, 203, 28, 578, 175, 36, false, Diagnostics));
        actions.push_back(add(L"BUTTON", L"Test graphics", WS_TABSTOP, 204, 215, 578, 175, 36,
                              false, Diagnostics));
        add(L"BUTTON", L"Open logs", WS_TABSTOP, 206, 402, 578, 150, 36, false, Diagnostics);
        add(L"STATIC",
            L"After launching, load a site on your monitor. Menus and HUD stay on the monitor.\nRight stick: look while walking; control the implement in a dozer or loader.",
            0, 0, 28, 630, 684, 48, false, Diagnostics, true);
        add(L"STATIC", L"F9  Recenter     F10  VR on/off     F11  Antialiasing", 0, 0, 28, 588, 530,
            24, false, Footer, true);
        add(L"BUTTON", L"Licences", WS_TABSTOP, 207, 614, 578, 98, 34, false, Footer, false, true);
        layout();
        started = GetTickCount64();
        refresh();
        SetTimer(window, 1, 500, nullptr);
        ShowWindow(window, SW_SHOWNORMAL);
        SetFocus(launch);
    }
    ~WindowsApp() {
        DeleteObject(font);
        DeleteObject(titleFont);
        DeleteObject(launchFont);
        DeleteObject(background);
    }
    int run() {
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
                wchar_t kind[32]{};
                const auto focus = GetFocus();
                GetClassNameW(focus, kind, 32);
                if (_wcsicmp(kind, L"BUTTON") == 0) {
                    SendMessageW(focus, BM_CLICK, 0, 0);
                    continue;
                }
            }
            if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
                IsWindowEnabled(launch) && !SendMessageW(scale, CB_GETDROPPEDSTATE, 0, 0) &&
                !SendMessageW(graphics, CB_GETDROPPEDSTATE, 0, 0)) {
                attempt([&] {
                    command(202);
                });
                continue;
            }
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
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
