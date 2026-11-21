// =====================================================================================================
// Example mod for WormsLoader: a working template that shows the whole mod API.
//
// This file and wormsloader.h are copied next to Example.dll (Mods\Example\source\) so that anyone,
// person or LLM, can learn how to write a mod without the repository.
//
// HOW A MOD WORKS
//   - A mod is a 32-bit Windows DLL at <game>\Mods\<Name>\<Name>.dll. WormsLoader (dinput8.dll in the game
//     folder) loads every folder in Mods\, sorted by name.
//   - It exports WL_GetModInfo and WL_ModInit, and optionally WL_ModEarlyInit (see the bottom of this file).
//   - Everything it can ask the loader for is in wormsloader.h (the WL_Api struct). Include only that header.
//   - Build: MSVC, Win32 (x86), Release, static CRT (/MT). In the repository: add wl_mod(<Name>) to
//     CMakeLists.txt and run build.bat. Without the repository, any 32-bit DLL with these exports works:
//       cl /LD /MT /O2 example.cpp /Fe:Example.dll
//
// GAME MEMORY
//   The game exe has no ASLR (image base 0x400000), so an address from a disassembler is a valid pointer:
//   api->exe(0x9d2bb3). Addresses belong to one game build; the loader only loads mods on a build it knows
//   (WL_ModInfo.game says which game the mod is for). Hook functions with api->hook (MinHook) and change code
//   bytes with api->patch. Always check the original bytes before patching (see SkipIntro).
//
// ONLINE
//   The game is deterministic lockstep: everyone in a match must simulate the same thing. A mod that changes
//   gameplay must set client_only = false; the loader then only lets players with the same mods (name and
//   version) into the same lobby. Bump the version whenever such a mod's behaviour changes.
// =====================================================================================================
#include "wormsloader.h"

static const WL_Api* api; // keep the pointer: it stays valid for the life of the game
static bool* s_flag;      // setting pointers always hold the current value (edited live from the Mods menu)
static int* s_count;
static float* s_scale;
static int* s_mode;

// Shown in the Mods menu. name must equal the folder and DLL name.
static const WL_ModInfo kInfo = {
    "Example",     // name
    "1.1.0",       // version
    "WormsLoader", // author
    "Shows how a mod declares settings and hotkeys. Press the hotkey to write the current values to "
    "WormsLoader.log. Its commented source is in Mods\\Example\\source.",
    true,            // client_only: touches nothing other players see, so it is left out of the mod-set check
    "WormsClanWars", // game: the mod is only loaded for this game ("*" = any game, only if it uses no addresses)
};

static const char* const kModes[] = {"Off", "Low", "High"};

// Hotkey callback: runs on the game thread when the key is pressed (never while typing in the Mods menu).
static void on_hotkey(void*) {
    api->log("[Example] game=%s flag=%d count=%d scale=%.2f mode=%s", api->game(), *s_flag, *s_count, *s_scale,
             kModes[*s_mode]);
}

// Runs once per rendered frame on the game thread. Keep it cheap.
static void on_frame(void*) {
    // api->on_screen tells which GUI screen is on top. Screen names are in the exe's strings ("Screen...").
    static bool was_main;
    bool main = api->on_screen("ScreenMain");
    if (main && !was_main && *s_flag) api->log("[Example] main menu reached");
    was_main = main;
}

/* ---- More things a mod can do (not compiled; copy what you need) ------------------------------------

// 1. Hook a game function. Declare its exact calling convention. MSVC thiscall methods can be hooked as
//    __fastcall with a dummy edx argument.
typedef int(__fastcall* Foo_t)(void* self, void* edx, int arg);
static Foo_t o_Foo;
static int __fastcall h_Foo(void* self, void* edx, int arg) {
    int r = o_Foo(self, edx, arg); // call the original
    return r;
}
//    in WL_ModInit:  api->hook(api->exe(0x123456), (void*)h_Foo, (void**)&o_Foo);

// 2. Patch bytes, only when the original bytes match (another build or mod could have changed them).
static const unsigned char kOld[] = {0x05, 0x88, 0x13, 0x00, 0x00}, kNew[] = {0x05, 0x00, 0x00, 0x00, 0x00};
//    if (!memcmp(api->exe(0x9d2bb3), kOld, 5)) api->patch(api->exe(0x9d2bb3), kNew, 5);

// 3. Press a key in the game's menus (Windows VK code), e.g. Enter = 0x0D:
//    api->press_key(0x0D);

// 4. Watch or drop the game's network packets (Steam P2P or relay):
static bool on_recv(WL_SteamID from, const void* data, unsigned size, void*) { return true; } // false = drop
//    api->on_recv(on_recv, nullptr);   api->on_send(...);   api->on_steam_callback(...);

------------------------------------------------------------------------------------------------------ */

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

// Called once at startup, after the game has created its globals. Register settings and hotkeys here
// (only here or in WL_ModEarlyInit). Each one shows up on the mod's page in the Mods menu and is saved in
// Mods\<Name>\settings.ini; the returned pointer already holds the saved value.
extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api* a) {
    api = a;
    s_flag = api->setting_bool("flag", "A checkbox", true);              // id (ini key), label, default
    s_count = api->setting_int("count", "An integer", 3, 0, 10);         // + min, max
    s_scale = api->setting_float("scale", "A float", 1.0f, 0.5f, 2.0f);  // + min, max
    s_mode = api->setting_enum("mode", "A choice", 1, kModes, 3);        // default index, options, count
    api->hotkey("print", "Log the values above", 0x75 /* VK_F6 */, on_hotkey, nullptr); // rebindable
    api->on_frame(on_frame, nullptr);
}

// Optional. Runs at the exe entry point, before the game reads its options or opens its window (game globals
// do not exist yet). Borderless uses it to change the display options. Most mods don't need it.
// extern "C" __declspec(dllexport) void WL_ModEarlyInit(const WL_Api* a) { }
