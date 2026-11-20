// Internal to WormsLoader.dll (mods use include/wormsloader.h).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "wormsloader.h"

// ---- known exe build (no ASLR, image base 0x400000) -------------------------------
constexpr DWORD kExeTimeDateStamp = 1403709881; // Steam build 01.83.31.37
constexpr DWORD kExeSizeOfImage = 0x1730000;
// Screen stack: depth at kScreenDepth, entries (8 bytes) at kScreenStack + 8*i, i = 1..depth.
// entry[0] -> screen object, screen hash at object+8. Hash = djb2-xor of the screen name.
constexpr DWORD kScreenDepth = 0x194f560;
constexpr DWORD kScreenStack = 0x1956b50;
// Per-player cursor slots (float x, y in GUI space; found by memory scan): seen in use at 0x195aab0 and 0x195b0a8.
// shortcut: slot count/start guessed from those two (stride 0x5f8, 5 players); find the array base if one is missed.
constexpr DWORD kCursorSlot0 = 0x195aab0 - 3 * 0x5f8, kCursorStride = 0x5f8;
constexpr int kCursorSlots = 5;
constexpr DWORD kCursorBounds = 0x195b0c0; // floats min x, min y, max x, max y (-512, -288, 512, 288 at 16:9)

// ---- settings & hotkeys -----------------------------------------------------------
enum class SettingType { Bool, Int, Float, Enum };
struct Setting {
    std::string id, label;
    SettingType type;
    union { bool b; int i; float f; } value;
    float fmin = 0, fmax = 0;
    int imin = 0, imax = 0;
    std::vector<std::string> options;
};
struct Hotkey {
    std::string id, label;
    int vk = 0;
    WL_HotkeyFn fn = nullptr;
    void* user = nullptr;
};
struct Mod {
    std::string name, version, author, description, dir, error;
    bool client_only = false;
    std::vector<Setting*> settings; // pointers stay stable: values are handed to mods
    std::vector<Hotkey> hotkeys;
    HMODULE module = nullptr;
};

extern std::vector<Mod*> g_mods;
extern bool g_known_build;
extern std::string g_game_dir;
extern std::string g_modset;      // human-readable "name@version;..." of net-affecting mods
extern unsigned g_modset_hash;    // 0 = no net-affecting mods loaded

void loader_late_init(); // ui hooks + mod loading, once, outside the loader lock
void wl_log(const char* fmt, ...);
double wl_time();
inline unsigned djb2x(const char* s) { // the game's own name hash (exe 0x9c0410)
    unsigned h = 5381;
    while (*s) h = (h * 33) ^ (unsigned)(int)(signed char)*s++;
    return h;
}
inline unsigned fnv1a(const std::string& s) {
    unsigned h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

// config.cpp
void config_load(Mod* m);
void config_save(Mod* m);
std::string loader_ini(const char* key, const char* def);
void loader_ini_set(const char* key, const std::string& v);

// api.cpp
extern const WL_Api g_api;
extern Mod* g_init_mod; // mod currently inside WL_ModEarlyInit/WL_ModInit
void api_run_frame();
void api_dispatch_hotkey(int vk);
bool api_filter_send(WL_SteamID to, const void* d, unsigned n, int type);
bool api_filter_recv(WL_SteamID from, const void* d, unsigned n);
bool api_filter_steam_callback(int id, void* data);

// net.cpp
void net_install();              // after steam_api.dll is loaded
extern std::string g_net_status; // shown in the Mods menu
extern std::string g_block_reason; // non-empty = current lobby refused (mod-set mismatch)
double net_last_recv();

// ui.cpp
void ui_install();
void dinput_wrap(void* idirectinput8);
