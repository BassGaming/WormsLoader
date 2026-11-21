// SkipIntro: skips the Team17 legal screen, the startup movies and the "press any key" title screen.
#include <string.h>
#include "wormsloader.h"

static const WL_Api* api;
static bool* s_title;

static const WL_ModInfo kInfo = {
    "SkipIntro", "1.0.0", "WormsLoader",
    "Skips the Team17 legal screen, the intro movies and the \"press any key\" title screen when the game starts. "
    "Takes effect on the next launch after changing the setting.",
    true, "WormsClanWars", // client-only: startup only
};

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

// The title screen waits for a key; press Enter for the player while it is up. Retried because the screen
// ignores input while it fades in.
static void on_frame(void*) {
    static double next;
    if (!*s_title || !api->on_screen("ScreenTitle") || api->time() < next) return;
    next = api->time() + 0.5;
    api->press_key(0x0D); // VK_RETURN
    api->log("[SkipIntro] title screen: pressed Enter");
}

extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api* a) {
    api = a;
    bool* legal = api->setting_bool("legal", "Skip the Team17 legal screen", true);
    bool* movies = api->setting_bool("movies", "Skip the intro movies", true);
    s_title = api->setting_bool("title", "Skip the \"press any key\" title screen", true);
    api->on_frame(on_frame, nullptr);
    // GameFlow Init_LegalScreen (0x9d2b10) shows ScreenLegal until now + 5000 ms ("add eax, 0x1388" at 0x9d2bb3).
    // A 0 ms timeout ends it on the next update.
    static const unsigned char kAdd5000[5] = {0x05, 0x88, 0x13, 0x00, 0x00}, kAdd0[5] = {0x05, 0, 0, 0, 0};
    if (*legal && !memcmp(api->exe(0x9d2bb3), kAdd5000, 5) && !api->patch(api->exe(0x9d2bb3), kAdd0, 5))
        api->log("[SkipIntro] legal screen timeout = 0");
    if (!*movies) return;
    // 0x9c8680 is what the option parser calls for "/NOMOVIES": it sets the game property ShowMovies = 0.
    // It needs the property store (pointer at 0x1959154), which exists once the game has parsed its options.
    if (!*(void**)api->exe(0x1959154)) { api->log("[SkipIntro] property store not ready, movies not skipped"); return; }
    ((void(__cdecl*)())api->exe(0x9c8680))();
    api->log("[SkipIntro] ShowMovies = 0");
}
