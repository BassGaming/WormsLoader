// FpsCap: limits the frame rate. Waits in WormsLoader's per-frame hook, right after a frame is presented (before the game reads input for the next one).
#include <windows.h>
#include "wormsloader.h"

static const WL_Api* api;
static bool* s_on;
static int* s_fps;
static HANDLE s_timer;
static double s_last;

static const WL_ModInfo kInfo = {
    "FpsCap", "1.0.0", "WormsLoader",
    "Caps the frame rate. The game runs uncapped (often 1000+ fps in menus) unless VSync is on, "
    "which wastes power and can cause stutter. Drag the slider or type a value.",
    true, "*", // any game (no exe addresses); client-only: frame pacing is local, the match simulation does not depend on it
};

static void on_frame(void*) {
    double now = api->time();
    if (!*s_on) { s_last = now; return; }
    double target = s_last + 1.0 / *s_fps;
    if (target <= now) { s_last = now; return; } // fell behind: don't try to catch up
    double left = target - now;
    if (left > 0.002) { // sleep most of it, spin the last 1.5 ms for accuracy
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)((left - 0.0015) * 1e7);
        if (SetWaitableTimer(s_timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(s_timer, 1000);
    }
    while (api->time() < target) YieldProcessor();
    s_last = target;
}

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api* a) {
    api = a;
    s_on = api->setting_bool("enabled", "Cap the frame rate", true);
    s_fps = api->setting_int("fps", "Max FPS", 144, 20, 1000);
    // shortcut: the plain-timer fallback can oversleep by up to ~15 ms; only Windows older than 10 1803 hits it.
    s_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    if (!s_timer) s_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    api->on_frame(on_frame, nullptr);
}
