// Persistence: Mods\<Name>\settings.ini ([settings] + [hotkeys]) and <game>\WormsLoader.ini.
#include "loader.h"
#include <cstdlib>

static std::string mod_ini(Mod* m) { return m->dir + "settings.ini"; }

static std::string ini_get(const std::string& file, const char* sec, const char* key, const char* def) {
    char buf[512];
    GetPrivateProfileStringA(sec, key, def, buf, sizeof buf, file.c_str());
    return buf;
}

void config_load(Mod* m) {
    std::string f = mod_ini(m);
    for (Setting* s : m->settings) {
        std::string v = ini_get(f, "settings", s->id.c_str(), "");
        if (v.empty()) continue;
        switch (s->type) {
        case SettingType::Bool: s->value.b = v == "1" || v == "true"; break;
        case SettingType::Int:
        case SettingType::Enum: {
            int i = atoi(v.c_str());
            s->value.i = i < s->imin ? s->imin : i > s->imax ? s->imax : i; // ini is user-editable
            break;
        }
        case SettingType::Float: {
            float x = (float)atof(v.c_str());
            s->value.f = x < s->fmin ? s->fmin : x > s->fmax ? s->fmax : x;
            break;
        }
        }
    }
    for (Hotkey& h : m->hotkeys) {
        std::string v = ini_get(f, "hotkeys", h.id.c_str(), "");
        if (!v.empty()) h.vk = atoi(v.c_str()) & 0xff;
    }
}

void config_save(Mod* m) {
    std::string f = mod_ini(m);
    for (Setting* s : m->settings) {
        std::string v;
        switch (s->type) {
        case SettingType::Bool: v = s->value.b ? "1" : "0"; break;
        case SettingType::Int:
        case SettingType::Enum: v = std::to_string(s->value.i); break;
        case SettingType::Float: v = std::to_string(s->value.f); break;
        }
        WritePrivateProfileStringA("settings", s->id.c_str(), v.c_str(), f.c_str());
    }
    for (Hotkey& h : m->hotkeys)
        WritePrivateProfileStringA("hotkeys", h.id.c_str(), std::to_string(h.vk).c_str(), f.c_str());
}

std::string loader_ini(const char* key, const char* def) {
    return ini_get(g_game_dir + "WormsLoader.ini", "loader", key, def);
}
void loader_ini_set(const char* key, const std::string& v) {
    WritePrivateProfileStringA("loader", key, v.c_str(), (g_game_dir + "WormsLoader.ini").c_str());
}
