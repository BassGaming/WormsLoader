// SkipIntro: skips the Team17 legal screen and the startup movies.
#include <string.h>
#include "wormsloader.h"

static const WL_ModInfo kInfo = {
    "SkipIntro", "1.0.0", "WormsLoader",
    "Skips the Team17 legal screen and the intro movies when the game starts. "
    "Takes effect on the next launch after changing the setting.",
    true, // client-only: startup only
};

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api* api) {
    bool* legal = api->setting_bool("legal", "Skip the Team17 legal screen", true);
    bool* movies = api->setting_bool("movies", "Skip the intro movies", true);
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
