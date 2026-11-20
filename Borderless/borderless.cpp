// Borderless: borderless fullscreen. The game only has exclusive fullscreen and a captioned window. This mod
// forces the game's windowed mode, keeps the desktop display mode (ChangeDisplaySettings becomes a no-op)
// and makes the game window a captionless popup that covers the monitor.
#include <windows.h>
#include "wormsloader.h"

static const WL_Api* api;
static HWND s_game;

typedef HWND(WINAPI* CreateWindowExW_t)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef BOOL(WINAPI* SetWindowPos_t)(HWND, HWND, int, int, int, int, UINT);
typedef LONG(WINAPI* ChangeDisplaySettingsW_t)(DEVMODEW*, DWORD);
typedef LONG(WINAPI* ChangeDisplaySettingsA_t)(DEVMODEA*, DWORD);
static CreateWindowExW_t o_CreateWindowExW;
static SetWindowPos_t o_SetWindowPos;
static ChangeDisplaySettingsW_t o_CdsW;
static ChangeDisplaySettingsA_t o_CdsA;
typedef void(__fastcall* DisplayOption_t)(void* self, void* edx, void* opt, void* val);
static DisplayOption_t o_DisplayOption;

static const WL_ModInfo kInfo = {
    "Borderless", "1.0.0", "WormsLoader",
    "Runs the game as a borderless window covering the whole monitor, with no display mode switch. "
    "Set the game's resolution to your desktop resolution. Takes effect on the next launch.",
    true, // client-only: window handling only
};

static RECT monitor_rect(HWND h) {
    MONITORINFO mi = {sizeof mi};
    HMONITOR m = h ? MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY) : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(m, &mi);
    return mi.rcMonitor;
}

static const DWORD kFrame = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

static void make_borderless(HWND h) {
    LONG st = GetWindowLongW(h, GWL_STYLE);
    SetWindowLongW(h, GWL_STYLE, (st & ~kFrame) | WS_POPUP);
    RECT r = monitor_rect(h);
    o_SetWindowPos(h, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

static bool is_game_window(DWORD style, HWND parent, HINSTANCE inst) {
    return !(style & WS_CHILD) && !parent && (!inst || inst == GetModuleHandleW(nullptr)); // the game passes NULL
}

static HWND WINAPI h_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int h,
                                     HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    if (!is_game_window(style, parent, inst)) return o_CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
    RECT r = monitor_rect(nullptr);
    style = (style & ~kFrame) | WS_POPUP;
    ex &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_TOPMOST); // fullscreen mode makes it topmost
    s_game = o_CreateWindowExW(ex, cls, name, style, r.left, r.top, r.right - r.left, r.bottom - r.top, parent, menu, inst, param);
    api->log("[Borderless] game window %p created borderless (%ldx%ld)", s_game, r.right - r.left, r.bottom - r.top);
    return s_game;
}

// The game positions/sizes its window itself (windowed mode centres it); keep it on the monitor rect.
static BOOL WINAPI h_SetWindowPos(HWND h, HWND after, int x, int y, int w, int hh, UINT flags) {
    if (h && h == s_game) {
        RECT r = monitor_rect(h);
        x = r.left; y = r.top; w = r.right - r.left; hh = r.bottom - r.top;
        flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        if (after == HWND_TOPMOST) flags |= SWP_NOZORDER; // stay a normal window so alt-tab works
    }
    return o_SetWindowPos(h, after, x, y, w, hh, flags);
}

// Exclusive fullscreen switches the display mode; borderless keeps the desktop mode.
static LONG WINAPI h_CdsW(DEVMODEW*, DWORD) { return DISP_CHANGE_SUCCESSFUL; }
static LONG WINAPI h_CdsA(DEVMODEA*, DWORD) { return DISP_CHANGE_SUCCESSFUL; }

// 0x6965e0: the display config's option handler (thiscall, 2 stack args), run for each startup option.
// "/WIN" clears bit 0 of this+0x30 and "/FS" sets it (fullscreen). Clearing it after every option forces windowed.
static void __fastcall h_DisplayOption(void* self, void* edx, void* opt, void* val) {
    o_DisplayOption(self, edx, opt, val);
    *((BYTE*)self + 0x30) &= ~1;
}

static BOOL CALLBACK find_existing(HWND h, LPARAM) {
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetParent(h) && is_game_window(GetWindowLongW(h, GWL_STYLE), nullptr, (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE))) {
        s_game = h;
        return FALSE;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

extern "C" __declspec(dllexport) void WL_ModEarlyInit(const WL_Api* a) {
    api = a;
    bool* on = api->setting_bool("enabled", "Borderless fullscreen", true);
    if (!*on) return;
    api->hook(api->exe(0x6965e0), (void*)h_DisplayOption, (void**)&o_DisplayOption);
    HMODULE u = LoadLibraryW(L"user32.dll");
    api->hook((void*)GetProcAddress(u, "CreateWindowExW"), (void*)h_CreateWindowExW, (void**)&o_CreateWindowExW);
    api->hook((void*)GetProcAddress(u, "SetWindowPos"), (void*)h_SetWindowPos, (void**)&o_SetWindowPos);
    api->hook((void*)GetProcAddress(u, "ChangeDisplaySettingsW"), (void*)h_CdsW, (void**)&o_CdsW);
    api->hook((void*)GetProcAddress(u, "ChangeDisplaySettingsA"), (void*)h_CdsA, (void**)&o_CdsA);
}

extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api*) {
    if (!o_CreateWindowExW) return; // disabled
    EnumWindows(find_existing, 0); // fallback: a window that somehow exists already
    if (s_game && (GetWindowLongW(s_game, GWL_STYLE) & WS_CAPTION)) {
        make_borderless(s_game);
        api->log("[Borderless] existing game window %p made borderless", s_game);
    }
}
