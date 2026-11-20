// Internal to WormsLoader.dll (mods use include/wormsloader.h).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "wormsloader.h"

// ---- supported games (no ASLR, image base 0x400000) ---------------------------------
// One entry per game build. The exe file name picks the game, PE timestamp + image size pick the build;
// everything game-specific the loader itself touches lives here. To support another game, add an entry
// and make the code that reads these addresses cope with that game's layout.
struct Game {
    const char* id;         // WL_ModInfo.game / WL_Api.game, = exe name without ".exe"
    const char* build;      // shown in the menu; part of the mod-set hash
    DWORD timestamp, image_size;
    // Screen stack: depth at screen_depth, entries (8 bytes) at screen_stack + 8*i, i = 1..depth.
    // entry[0] -> screen object, screen hash at object+8. Hash = djb2-xor of the screen name.
    DWORD screen_depth, screen_stack;
    // Per-player cursor slots: float x, y in GUI space. Bounds: floats min x, min y, max x, max y.
    DWORD cursor_slot0, cursor_stride, cursor_bounds;
    int cursor_slots;
    // Bundles (assets.cpp): resource manager pointer, GUID of its bundle service, XString ctor,
    // and the game bundle whose load/unload mod bundles follow.
    DWORD resource_mgr, bundle_svc_guid, xstring_ctor;
    const char* bundle_anchor;
    DWORD fs_open; // file system Open (vtable 0xe055cc slot 5): stdcall(fs, path, mode, IXStream**) -> HRESULT
};
// shortcut: Clan Wars cursor slot count/start guessed from the two seen in use (0x195aab0, 0x195b0a8);
// find the array base if a player's cursor is missed.
inline const Game kGames[] = {
    {"WormsClanWars", "01.83.31.37", 1403709881, 0x1730000, 0x194f560, 0x1956b50, 0x195aab0 - 3 * 0x5f8, 0x5f8, 0x195b0c0, 5,
     0x1947a10, 0xca91b0, 0x556ac0, "/Weapons.bdl", 0x57d590},
};
constexpr int kCursorSlotsMax = 8;

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
extern const Game* g_game; // null = unknown game or build: no mods load
extern bool g_known_build; // == (g_game != nullptr)
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

// assets.cpp
void assets_init();  // at the exe entry point, after the mod DLLs are loaded
void assets_frame(); // game thread, every frame

// ui.cpp
void ui_install();
void dinput_wrap(void* idirectinput8);
bool ui_on_screen(const char* name);
void ui_press_key(int vk);
