// dinput8.dll proxy: forwards DirectInput8Create to the system DLL, checks the exe build,
// installs the core hooks and loads Mods\<Name>\<Name>.dll.
#include "loader.h"
#include <MinHook.h>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <mutex>

std::vector<Mod*> g_mods;
const Game* g_game;
bool g_known_build = false;
std::string g_game_dir;
std::string g_modset;
unsigned g_modset_hash = 0;

static FILE* g_logf;
static std::mutex g_log_mx;

void wl_log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_log_mx);
    if (!g_logf) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_logf, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_logf, fmt, a); va_end(a);
    fputc('\n', g_logf);
    fflush(g_logf);
}

double wl_time() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

// Phase 1, at the exe entry point (before the game's code runs): load the DLLs, call WL_ModEarlyInit.
static void load_mods() {
    std::string pattern = g_game_dir + "Mods\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    std::vector<std::string> names;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') names.push_back(fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(names.begin(), names.end());

    for (auto& n : names) {
        Mod* m = new Mod();
        m->name = n;
        m->dir = g_game_dir + "Mods\\" + n + "\\";
        g_mods.push_back(m);
        std::string dll = m->dir + n + ".dll";
        HMODULE mod = LoadLibraryExA(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!mod) { m->error = "cannot load " + n + ".dll (error " + std::to_string(GetLastError()) + ")"; wl_log("[%s] %s", n.c_str(), m->error.c_str()); continue; }
        auto info_fn = (const WL_ModInfo* (*)())GetProcAddress(mod, "WL_GetModInfo");
        const WL_ModInfo* info = info_fn ? info_fn() : nullptr;
        if (!info || !GetProcAddress(mod, "WL_ModInit")) { m->error = "missing WL_GetModInfo/WL_ModInit export"; wl_log("[%s] %s", n.c_str(), m->error.c_str()); continue; }
        if (!info->game || (strcmp(info->game, "*") && _stricmp(info->game, g_game->id))) {
            m->error = std::string("made for ") + (info->game ? info->game : "an older WormsLoader") + ", not " + g_game->id;
            wl_log("[%s] %s", n.c_str(), m->error.c_str());
            continue;
        }
        m->module = mod;
        m->version = info->version ? info->version : "?";
        m->author = info->author ? info->author : "";
        m->description = info->description ? info->description : "";
        m->client_only = info->client_only;
        if (auto early = (void (*)(const WL_Api*))GetProcAddress(mod, "WL_ModEarlyInit")) {
            g_init_mod = m;
            early(&g_api);
            g_init_mod = nullptr;
        }
    }
}

// Phase 2, at SteamAPI_Init / DirectInput8Create: call WL_ModInit (game globals exist by then).
static void init_mods() {
    for (Mod* m : g_mods) {
        if (!m->module) continue;
        g_init_mod = m;
        ((void (*)(const WL_Api*))GetProcAddress(m->module, "WL_ModInit"))(&g_api);
        g_init_mod = nullptr;
        wl_log("[%s] loaded v%s%s", m->name.c_str(), m->version.c_str(), m->client_only ? " (client only)" : "");
    }

    // Mods are sorted by name, so the string is the same on every machine with the same set.
    for (Mod* m : g_mods)
        if (m->error.empty() && !m->client_only) g_modset += m->name + "@" + m->version + ";";
    g_modset_hash = g_modset.empty() ? 0 : fnv1a(std::string(g_game->build) + "|" + g_modset);
    wl_log("mod set: '%s' hash %08x", g_modset.c_str(), g_modset_hash);
}

typedef int(__cdecl* Entry_t)();
static Entry_t o_entry;
static int __cdecl h_entry() { // outside the loader lock, before the CRT and WinMain
    load_mods();
    assets_init();
    return o_entry();
}

// Runs once, outside the loader lock: first of SteamAPI_Init / DirectInput8Create.
void loader_late_init() {
    static bool done = false;
    if (done) return;
    done = true;
    ui_install();
    if (g_known_build) {
        init_mods();
    }
}

typedef HRESULT(WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, void* outer) {
    static DI8Create_t real = [] {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH); // SysWOW64 for a 32-bit process
        strcat_s(path, "\\dinput8.dll");
        return (DI8Create_t)GetProcAddress(LoadLibraryA(path), "DirectInput8Create");
    }();
    loader_late_init();
    if (!real) return E_FAIL;
    HRESULT hr = real(inst, ver, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out) dinput_wrap(*out);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);

    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    g_game_dir = exe;
    g_game_dir.resize(g_game_dir.find_last_of('\\') + 1);
    g_logf = _fsopen((g_game_dir + "WormsLoader.log").c_str(), "w", _SH_DENYWR);

    // shortcut: build identified by PE timestamp + image size, not a file hash; good enough for Steam builds.
    auto base = (BYTE*)GetModuleHandleA(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    std::string exe_name = std::string(exe).substr(g_game_dir.size());
    const Game* game = nullptr;
    for (const Game& g : kGames)
        if (!_stricmp((std::string(g.id) + ".exe").c_str(), exe_name.c_str())) game = &g;
    if (game && nt->FileHeader.TimeDateStamp == game->timestamp && nt->OptionalHeader.SizeOfImage == game->image_size) g_game = game;
    g_known_build = g_game != nullptr;
    wl_log("WormsLoader starting. %s timestamp %u size %x -> %s %s", exe_name.c_str(), nt->FileHeader.TimeDateStamp,
           nt->OptionalHeader.SizeOfImage, game ? game->id : "unsupported game,",
           g_game ? g_game->build : game ? "UNKNOWN BUILD, mods disabled" : "mods disabled");

    MH_Initialize();
    if (g_known_build) {
        net_install(); // steam_api.dll is a static import of the exe, already mapped
        if (g_api.hook(base + nt->OptionalHeader.AddressOfEntryPoint, (void*)h_entry, (void**)&o_entry))
            wl_log("cannot hook the exe entry point, WL_ModEarlyInit will not run");
    }
    return TRUE;
}
