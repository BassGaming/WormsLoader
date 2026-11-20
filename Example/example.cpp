// Example: the smallest mod that uses every setting type and a hotkey. Copy this folder to start a mod.
#include "wormsloader.h"

static const WL_Api* api;
static bool* s_flag;
static int* s_count;
static float* s_scale;
static int* s_mode;

static const WL_ModInfo kInfo = {
    "Example", "1.0.0", "WormsLoader",
    "Shows how a mod declares settings and hotkeys. Press the hotkey to write the current values to WormsLoader.log.",
    true, // client-only: touches nothing other players see, so it is left out of the mod-set check
};

static void on_hotkey(void*) {
    static const char* const modes[] = {"Off", "Low", "High"};
    api->log("[Example] flag=%d count=%d scale=%.2f mode=%s", *s_flag, *s_count, *s_scale, modes[*s_mode]);
}

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api* a) {
    api = a;
    static const char* const modes[] = {"Off", "Low", "High"};
    s_flag = api->setting_bool("flag", "A checkbox", true);
    s_count = api->setting_int("count", "An integer", 3, 0, 10);
    s_scale = api->setting_float("scale", "A float", 1.0f, 0.5f, 2.0f);
    s_mode = api->setting_enum("mode", "A choice", 1, modes, 3);
    api->hotkey("print", "Log the values above", 0x75 /* F6 */, on_hotkey, nullptr);
}
