// ==WindhawkMod==
// @id              desktop-dots
// @name            Desktop Dots
// @description     GNOME-style virtual desktop indicator on the far left of the taskbar
// @version         1.1
// @author          Marco Kretz
// @github          https://github.com/marco-kretz
// @homepage        https://github.com/marco-kretz/win11-desktop-dots
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lgdiplus -lgdi32 -lole32 -ldwmapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Desktop Dots

GNOME-style virtual desktop indicator on the far left of the Windows 11 taskbar (optionally on every monitor).

- **Filled dot**: at least one window is open on that desktop
- **Hollow dot**: the desktop is empty
- **Pill**: the active desktop (with a short animation when switching)

Left-click a dot to switch to that desktop. Right-click toggles hiding trailing empty desktops.

Requires a centered taskbar with the Widgets button disabled (otherwise the dots overlap it).
Pairs well with the *Disable Virtual Desktop Transition* mod for instant switching.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- allMonitors: false
  $name: Show on all monitors
  $description: Also show the dots on the taskbars of secondary monitors
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

const wchar_t VdKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";
const wchar_t ClassName[] = L"DesktopDots";
const UINT_PTR TIMER_REFRESH = 1, TIMER_ANIM = 2;

// Public IVirtualDesktopManager; GUIDs spelled out so we don't depend on libuuid exporting them.
const CLSID CLSID_VDM = {0xaa509086, 0x5ca9, 0x4c25, {0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a}};
const IID IID_VDM = {0xa5cd92ff, 0x29be, 0x454c, {0x8d, 0x04, 0xd8, 0x28, 0x79, 0xfb, 0x3f, 0x1b}};

struct Bar {
    HWND tray, hwnd;
    std::vector<float> slotEnds;
};

HANDLE g_stop, g_thread;
HWND g_hwnd;  // bar on the primary taskbar; owns the timers and the lifetime of RunBar
std::vector<Bar> g_bars;
IVirtualDesktopManager* g_vdm;
std::vector<GUID> g_desktops, g_occupied;
int g_current;
bool g_hideEmpty, g_allMonitors;
std::vector<float> g_grow;  // 0 = dot, 1 = pill, per desktop

bool Contains(const std::vector<GUID>& v, const GUID& g) {
    return std::find(v.begin(), v.end(), g) != v.end();
}

std::vector<BYTE> ReadBinary(const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, VdKey, name, RRF_RT_REG_BINARY, nullptr, nullptr, &size) != ERROR_SUCCESS)
        return {};
    std::vector<BYTE> data(size);
    if (RegGetValueW(HKEY_CURRENT_USER, VdKey, name, RRF_RT_REG_BINARY, nullptr, data.data(), &size) != ERROR_SUCCESS)
        return {};
    data.resize(size);
    return data;
}

bool IsAppWindow(HWND h) {
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return false;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    int cloaked = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    // Windows on other desktops are cloaked by the shell; anything else cloaked (e.g. suspended UWP) is not really open.
    return (cloaked & ~DWM_CLOAKED_SHELL) == 0 && GetWindowTextLengthW(h) > 0;
}

BOOL CALLBACK CollectDesktop(HWND h, LPARAM) {
    GUID id;
    if (IsAppWindow(h) && g_vdm->GetWindowDesktopId(h, &id) == S_OK) g_occupied.push_back(id);
    return TRUE;
}

void Capsule(Gdiplus::GraphicsPath& p, float x, float y, float w, float d) {
    p.AddArc(x, y, d, d, 90, 180);
    p.AddArc(x + w - d, y, d, d, 270, 180);
    p.CloseFigure();
}

void RenderBar(Bar& b, int n, Gdiplus::Color color) {
    RECT tr;
    GetWindowRect(b.tray, &tr);
    float s = GetDpiForWindow(b.tray) / 96.f;
    float dot = 8 * s, pill = 24 * s, gap = 8 * s, pad = 12 * s, pen = 1.5f * s;
    int w = (int)std::ceil(pad * 2 + n * dot + (n - 1) * gap + (pill - dot));
    int h = tr.bottom - tr.top;

    Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        Gdiplus::SolidBrush brush(color);
        Gdiplus::Pen outline(color, pen);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(1, 0, 0, 0));  // alpha 1 so clicks between dots still reach us
        b.slotEnds.assign(n, 0);
        float x = pad, y = (h - dot) / 2.f;
        for (int i = 0; i < n; i++) {
            float dw = dot + (pill - dot) * g_grow[i];
            Gdiplus::GraphicsPath path;
            if (g_grow[i] > 0.5f || Contains(g_occupied, g_desktops[i])) {
                Capsule(path, x, y, dw, dot);
                g.FillPath(&brush, &path);
            } else {
                Capsule(path, x + pen / 2, y + pen / 2, dw - pen, dot - pen);
                g.DrawPath(&outline, &path);
            }
            x += dw + gap;
            b.slotEnds[i] = x - gap / 2;
        }
    }

    SetWindowPos(b.hwnd, HWND_TOP, 0, 0, w, h, SWP_NOACTIVATE);
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP hbmp;
    bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbmp);
    HGDIOBJ old = SelectObject(mem, hbmp);
    SIZE sz{w, h};
    POINT src{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(b.hwnd, screen, nullptr, &sz, mem, &src, 0, &blend, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteObject(hbmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void Render() {
    int n = (int)g_desktops.size();
    // Only trailing empty desktops are hidden, so the remaining dots keep their positions.
    if (g_hideEmpty) {
        int last = -1;
        for (int i = 0; i < n; i++)
            if (Contains(g_occupied, g_desktops[i])) last = i;
        n = std::max(g_current, last) + 1;
    }
    n = std::max(n, 1);

    DWORD light = 0, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    Gdiplus::Color color = light == 1 ? Gdiplus::Color(230, 0, 0, 0) : Gdiplus::Color(230, 255, 255, 255);

    for (Bar& b : g_bars)
        if (b.hwnd) RenderBar(b, n, color);
}

// Ease every slot towards its target width; the total stays constant because the shrinking and growing slot move equally.
void Animate() {
    bool done = true;
    for (size_t i = 0; i < g_grow.size(); i++) {
        float target = (int)i == g_current ? 1.f : 0.f;
        g_grow[i] += (target - g_grow[i]) * 0.3f;
        if (std::fabs(target - g_grow[i]) < 0.01f) g_grow[i] = target;
        else done = false;
    }
    if (done) KillTimer(g_hwnd, TIMER_ANIM);
    Render();
}

HWND CreateBar(HWND tray) {
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, ClassName, nullptr,
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, tray, nullptr,
                                (HINSTANCE)&__ImageBase, nullptr);
    if (hwnd) g_bars.push_back({tray, hwnd, {}});
    return hwnd;
}

// Secondary taskbars come and go with monitors; our bars die with them (WM_DESTROY clears hwnd).
void SyncSecondaryBars() {
    g_bars.erase(std::remove_if(g_bars.begin(), g_bars.end(), [](const Bar& b) { return !b.hwnd; }), g_bars.end());
    HWND tray = nullptr;
    while ((tray = FindWindowExW(nullptr, tray, L"Shell_SecondaryTrayWnd", nullptr))) {
        DWORD pid = 0;
        GetWindowThreadProcessId(tray, &pid);
        if (pid == GetCurrentProcessId() &&
            std::none_of(g_bars.begin(), g_bars.end(), [&](const Bar& b) { return b.tray == tray; }))
            CreateBar(tray);
    }
}

void Refresh() {
    if (g_allMonitors) SyncSecondaryBars();
    auto ids = ReadBinary(L"VirtualDesktopIDs");
    g_desktops.assign((const GUID*)ids.data(), (const GUID*)ids.data() + ids.size() / sizeof(GUID));
    auto cur = ReadBinary(L"CurrentVirtualDesktop");
    auto it = cur.size() == sizeof(GUID) ? std::find(g_desktops.begin(), g_desktops.end(), *(const GUID*)cur.data())
                                         : g_desktops.end();
    g_current = it != g_desktops.end() ? (int)(it - g_desktops.begin()) : -1;
    if (g_desktops.empty()) {
        g_desktops = {GUID{}};
        g_current = 0;
    }

    g_occupied.clear();
    EnumWindows(CollectDesktop, 0);

    // Desktop added/removed: jump straight to the new layout instead of animating.
    if (g_grow.size() != g_desktops.size()) {
        g_grow.assign(g_desktops.size(), 0);
        if (g_current >= 0) g_grow[g_current] = 1;
    }
    SetTimer(g_hwnd, TIMER_ANIM, 15, nullptr);
    Animate();
}

int HitTest(HWND h, int x) {
    for (const Bar& b : g_bars)
        if (b.hwnd == h)
            for (size_t i = 0; i < b.slotEnds.size(); i++)
                if (x < b.slotEnds[i]) return (int)i;
    return -1;
}

// The public API can't switch desktops, so press Ctrl+Win+Left/Right as often as needed.
void SwitchTo(int target) {
    if (target < 0 || g_current < 0 || target == g_current) return;
    BYTE arrow = target > g_current ? VK_RIGHT : VK_LEFT;
    keybd_event(VK_LWIN, 0, KEYEVENTF_EXTENDEDKEY, 0);
    keybd_event(VK_LCONTROL, 0, 0, 0);
    for (int i = 0; i < std::abs(target - g_current); i++) {
        keybd_event(arrow, 0, KEYEVENTF_EXTENDEDKEY, 0);
        keybd_event(arrow, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    }
    keybd_event(VK_LCONTROL, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_LWIN, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
}

// Native Win32 menu: gets the Windows 11 look (rounded corners, dark mode).
// Autostart and Exit from the standalone app are covered by Windhawk itself.
void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, g_hideEmpty ? MF_CHECKED : 0, 1, L"Hide trailing empty desktops");

    // Our bar is a no-activate child of the taskbar; a hidden top-level owner in our thread
    // can take the foreground, which the menu needs to close when clicking elsewhere.
    HWND owner = CreateWindowExW(0, L"Static", nullptr, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    // Undocumented uxtheme export (by ordinal, stable since Windows 10 1903). Explorer already set its
    // preferred app mode, so we leave SetPreferredAppMode (#135) alone.
    using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
    if (auto allowDark = (AllowDarkModeForWindowFn)GetProcAddress(GetModuleHandleW(L"uxtheme.dll"), MAKEINTRESOURCEA(133)))
        allowDark(owner, TRUE);
    SetForegroundWindow(owner);
    POINT pt;
    GetCursorPos(&pt);
    int command = TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, owner, nullptr);
    DestroyMenu(menu);
    DestroyWindow(owner);

    if (command == 1 && g_hwnd) {
        g_hideEmpty = !g_hideEmpty;
        Wh_SetIntValue(L"HideEmpty", g_hideEmpty);
        Render();
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER: wp == TIMER_REFRESH ? Refresh() : Animate(); return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_LBUTTONUP: SwitchTo(HitTest(h, GET_X_LPARAM(lp))); return 0;
        case WM_RBUTTONUP: ShowMenu(); return 0;
        case WM_DESTROY:
            if (h == g_hwnd) g_hwnd = nullptr;
            for (Bar& b : g_bars)
                if (b.hwnd == h) b.hwnd = nullptr;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void RunBar(HWND tray) {
    if (CoCreateInstance(CLSID_VDM, nullptr, CLSCTX_ALL, IID_VDM, (void**)&g_vdm) != S_OK) return;
    g_grow.clear();
    g_hwnd = CreateBar(tray);
    if (g_hwnd) {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, VdKey, 0, KEY_NOTIFY, &key) != ERROR_SUCCESS) key = nullptr;
        HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        auto watch = [&] {
            if (key) RegNotifyChangeKeyValue(key, TRUE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE);
        };
        watch();
        // ponytail: 1s polling catches opened/closed/moved windows; a WinEvent hook if that lag bothers.
        SetTimer(g_hwnd, TIMER_REFRESH, 1000, nullptr);
        Refresh();

        // Waiting on the events here (instead of a blocking notify thread like the app) keeps every
        // call on this thread, so Wh_ModUninit can join it before the DLL unloads.
        HANDLE handles[] = {g_stop, changed};
        while (g_hwnd) {
            DWORD r = MsgWaitForMultipleObjects(2, handles, FALSE, INFINITE, QS_ALLINPUT);
            if (r == WAIT_OBJECT_0) {
                DestroyWindow(g_hwnd);
            } else if (r == WAIT_OBJECT_0 + 1) {
                watch();
                Refresh();
            } else {
                MSG m;
                while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&m);
                    DispatchMessageW(&m);
                }
            }
        }
        if (key) RegCloseKey(key);
        CloseHandle(changed);
    }
    for (const Bar& b : g_bars)
        if (b.hwnd) DestroyWindow(b.hwnd);
    g_bars.clear();
    g_vdm->Release();
    g_vdm = nullptr;
}

DWORD WINAPI BarThread(void*) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdip;
    Gdiplus::GdiplusStartup(&gdip, &gdipInput, nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = (HINSTANCE)&__ImageBase;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = ClassName;
    RegisterClassW(&wc);

    // The taskbar may not exist yet when Explorer loads the mod, and is rebuilt if it gets recreated.
    while (WaitForSingleObject(g_stop, 0) == WAIT_TIMEOUT) {
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        DWORD pid = 0;
        if (tray) GetWindowThreadProcessId(tray, &pid);
        if (tray && pid != GetCurrentProcessId()) break;  // a separate file explorer process; the taskbar lives elsewhere
        if (tray) RunBar(tray);
        WaitForSingleObject(g_stop, tray ? 2000 : 1000);
    }

    UnregisterClassW(ClassName, (HINSTANCE)&__ImageBase);
    Gdiplus::GdiplusShutdown(gdip);
    CoUninitialize();
    return 0;
}

BOOL Wh_ModInit() {
    g_hideEmpty = Wh_GetIntValue(L"HideEmpty", 0);
    g_allMonitors = Wh_GetIntSetting(L"allMonitors");
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, BarThread, nullptr, 0, nullptr);
    return TRUE;
}

void Wh_ModUninit() {
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    *bReload = TRUE;
    return TRUE;
}
