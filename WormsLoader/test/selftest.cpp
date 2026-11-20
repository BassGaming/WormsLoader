// wl_selftest: checks what can be checked without the game. Exit code 0 = pass.
#undef NDEBUG // asserts are the test
#include "loader.h"
#include <cassert>
#include <cstdio>

std::string g_game_dir;

int main() {
    // Must match the game's hash (exe 0x9c0410); value cross-checked against the exe's algorithm in Python.
    assert(djb2x("ScreenMain") == 0x7d25f4c2);

    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    Mod m;
    m.dir = std::string(tmp) + "wl_selftest_";
    DeleteFileA((m.dir + "settings.ini").c_str());
    Setting b{"b", "B", SettingType::Bool}, i{"i", "I", SettingType::Int}, f{"f", "F", SettingType::Float};
    b.value.b = true; i.value.i = 7; i.imin = 0; i.imax = 10; f.value.f = 1.5f; f.fmin = 0; f.fmax = 2;
    m.settings = {&b, &i, &f};
    m.hotkeys.push_back({"k", "K", 0x75});
    config_save(&m);

    b.value.b = false; i.value.i = 0; f.value.f = 0; m.hotkeys[0].vk = 0;
    config_load(&m);
    assert(b.value.b && i.value.i == 7 && f.value.f == 1.5f && m.hotkeys[0].vk == 0x75);

    WritePrivateProfileStringA("settings", "i", "999", (m.dir + "settings.ini").c_str()); // hand-edited ini
    config_load(&m);
    assert(i.value.i == 10); // clamped

    DeleteFileA((m.dir + "settings.ini").c_str());
    puts("selftest ok");
    return 0;
}
