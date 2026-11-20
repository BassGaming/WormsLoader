// WL_Api implementation handed to mods.
#include "loader.h"
#include <MinHook.h>
#include <cstdarg>
#include <cstdio>

Mod* g_init_mod = nullptr;

template <class F> struct Cb { F fn; void* user; };
static std::vector<Cb<WL_FrameFn>> s_frame;
static std::vector<Cb<WL_SendFilter>> s_send;
static std::vector<Cb<WL_RecvFilter>> s_recv;
static std::vector<Cb<WL_SteamCallbackFilter>> s_steamcb;

static int api_hook(void* target, void* detour, void** original) {
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s == MH_OK) s = MH_EnableHook(target);
    if (s != MH_OK) wl_log("hook %p failed: %s", target, MH_StatusToString(s));
    return s == MH_OK ? 0 : (int)s;
}

static int api_patch(void* addr, const void* bytes, unsigned n) {
    DWORD old;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old)) return 1;
    memcpy(addr, bytes, n);
    VirtualProtect(addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
    return 0;
}

static void* api_exe(unsigned va) { return (void*)(uintptr_t)va; }

static void api_log(const char* fmt, ...) {
    char buf[1024];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    wl_log("%s", buf);
}

static Setting* add_setting(const char* id, const char* label, SettingType t) {
    if (!g_init_mod) { wl_log("setting '%s' registered outside WL_ModInit, ignored", id); static Setting dummy; return &dummy; }
    Setting* s = new Setting();
    s->id = id; s->label = label; s->type = t;
    g_init_mod->settings.push_back(s);
    return s;
}
static bool* api_setting_bool(const char* id, const char* label, bool def) {
    Setting* s = add_setting(id, label, SettingType::Bool); s->value.b = def; return &s->value.b;
}
static int* api_setting_int(const char* id, const char* label, int def, int mn, int mx) {
    Setting* s = add_setting(id, label, SettingType::Int); s->value.i = def; s->imin = mn; s->imax = mx; return &s->value.i;
}
static float* api_setting_float(const char* id, const char* label, float def, float mn, float mx) {
    Setting* s = add_setting(id, label, SettingType::Float); s->value.f = def; s->fmin = mn; s->fmax = mx; return &s->value.f;
}
static int* api_setting_enum(const char* id, const char* label, int def, const char* const* opts, int n) {
    Setting* s = add_setting(id, label, SettingType::Enum); s->value.i = def; s->imin = 0; s->imax = n - 1;
    for (int i = 0; i < n; i++) s->options.push_back(opts[i]);
    return &s->value.i;
}
static void api_hotkey(const char* id, const char* label, int vk, WL_HotkeyFn fn, void* user) {
    if (!g_init_mod) { wl_log("hotkey '%s' registered outside WL_ModInit, ignored", id); return; }
    g_init_mod->hotkeys.push_back({id, label, vk, fn, user});
}

static void api_on_frame(WL_FrameFn f, void* u) { s_frame.push_back({f, u}); }
static void api_on_send(WL_SendFilter f, void* u) { s_send.push_back({f, u}); }
static void api_on_recv(WL_RecvFilter f, void* u) { s_recv.push_back({f, u}); }
static void api_on_steam_callback(WL_SteamCallbackFilter f, void* u) { s_steamcb.push_back({f, u}); }
static double api_since_recv() { return wl_time() - net_last_recv(); }

const WL_Api g_api = {
    WL_API_VERSION, api_hook, api_patch, api_exe, api_log, wl_time,
    api_setting_bool, api_setting_int, api_setting_float, api_setting_enum, api_hotkey,
    api_on_frame, api_on_send, api_on_recv, api_on_steam_callback, api_since_recv,
};

void api_run_frame() { for (auto& c : s_frame) c.fn(c.user); }

void api_dispatch_hotkey(int vk) {
    for (Mod* m : g_mods)
        for (auto& h : m->hotkeys)
            if (h.vk && h.vk == vk && h.fn) h.fn(h.user);
}

bool api_filter_send(WL_SteamID to, const void* d, unsigned n, int type) {
    for (auto& c : s_send) if (!c.fn(to, d, n, type, c.user)) return false;
    return true;
}
bool api_filter_recv(WL_SteamID from, const void* d, unsigned n) {
    for (auto& c : s_recv) if (!c.fn(from, d, n, c.user)) return false;
    return true;
}
bool api_filter_steam_callback(int id, void* data) {
    for (auto& c : s_steamcb) if (!c.fn(id, data, c.user)) return false;
    return true;
}
