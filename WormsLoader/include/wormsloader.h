// WormsLoader mod API. This is the ONLY header a mod includes.
// A mod is a 32-bit DLL at <game>\Mods\<Name>\<Name>.dll exporting:
//   extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo();
//   extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api* api);
#pragma once
#include <stdbool.h>

#define WL_API_VERSION 1

typedef unsigned long long WL_SteamID;

typedef struct WL_ModInfo {
    const char* name;        // must equal the folder/DLL name
    const char* version;     // part of the mod-set hash: bump it whenever gameplay/netcode behaviour changes
    const char* author;
    const char* description; // shown in the Mods menu
    bool client_only;        // true = cannot affect other players (UI only); excluded from the mod-set hash
} WL_ModInfo;

typedef void (*WL_HotkeyFn)(void* user);
typedef void (*WL_FrameFn)(void* user);
// Return false to drop the packet.
typedef bool (*WL_SendFilter)(WL_SteamID to, const void* data, unsigned size, int send_type, void* user);
typedef bool (*WL_RecvFilter)(WL_SteamID from, const void* data, unsigned size, void* user);
// Return false to hide this Steam callback from the game.
typedef bool (*WL_SteamCallbackFilter)(int callback_id, void* data, void* user);

typedef struct WL_Api {
    int version; // WL_API_VERSION

    // --- general ---------------------------------------------------------------
    // MinHook: creates and enables the hook. Returns 0 on success.
    int (*hook)(void* target, void* detour, void** original);
    // Byte patch with VirtualProtect. Returns 0 on success.
    int (*patch)(void* address, const void* bytes, unsigned size);
    // The exe has no ASLR and the loader only runs on the known build, so a VA from
    // Ghidra/the RE notes is a valid pointer. Exists so mods document "this is an exe VA".
    void* (*exe)(unsigned va);
    void (*log)(const char* fmt, ...); // -> <game>\WormsLoader.log
    double (*time)(void);              // seconds, monotonic

    // --- settings & hotkeys (call ONLY inside WL_ModInit) -------------------------
    // Return a pointer to the live value. The loader draws the widget in the Mods menu
    // and persists it to Mods\<Name>\settings.ini.
    bool*  (*setting_bool)(const char* id, const char* label, bool def);
    int*   (*setting_int)(const char* id, const char* label, int def, int min, int max);
    float* (*setting_float)(const char* id, const char* label, float def, float min, float max);
    int*   (*setting_enum)(const char* id, const char* label, int def, const char* const* options, int count);
    // default_vk: a Windows virtual-key code (0 = unbound). Rebindable in the Mods menu.
    void   (*hotkey)(const char* id, const char* label, int default_vk, WL_HotkeyFn fn, void* user);

    // --- runtime ---------------------------------------------------------------
    void (*on_frame)(WL_FrameFn fn, void* user); // every rendered frame, game thread
    // Game network traffic (Steam P2P or relay, mods don't see the difference).
    void (*on_send)(WL_SendFilter fn, void* user);
    void (*on_recv)(WL_RecvFilter fn, void* user);
    // Steam callbacks the game registered (e.g. 103 SteamServersDisconnected).
    void (*on_steam_callback)(WL_SteamCallbackFilter fn, void* user);
    // Seconds since the last game packet was received from anyone (huge if never).
    double (*seconds_since_net_recv)(void);
} WL_Api;
