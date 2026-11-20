// Mod files: game path "<mount>:/<rest>" opens Mods\<Name>\files\<mount>\<rest> instead when that file
// exists (mods in name order, first wins). A missing file must never reach the game's Open: it shows a
// modal "Missing or Corrupt file" box.
// Mod bundles: every Mods\<Name>\bundles\<File>.bdl is loaded right after the game loads its Weapons
// bundle and unloaded with it, so models in it can be named by weapon scripts (PayloadMesh etc.).
#include "loader.h"
#include "MinHook.h"

typedef long(__stdcall* BundleFn)(void* svc, const char** xstr);
static BundleFn o_load, o_unload;
static std::vector<std::string> s_bundles; // game paths, "root:/Mods/<Name>/bundles/<File>.bdl"

// The game's XString: char* that points 12 bytes past {refcount, length, capacity}.
static const char* xstring_new(const char* s) {
    const char* out = nullptr;
    ((void(__stdcall*)(const char**, const char*))g_game->xstring_ctor)(&out, s);
    return out; // shortcut: leaked (one small string per bundle load), fine for a few loads per match
}

static bool is_anchor(const char** xs) {
    const char* p = xs && *xs ? *xs : "";
    size_t n = strlen(p), m = strlen(g_game->bundle_anchor);
    return n >= m && _stricmp(p + n - m, g_game->bundle_anchor) == 0;
}

static long __stdcall h_load(void* svc, const char** xs) {
    long hr = o_load(svc, xs);
    if (hr >= 0 && is_anchor(xs))
        for (auto& b : s_bundles) {
            const char* x = xstring_new(b.c_str());
            long r = o_load(svc, &x);
            wl_log("bundle load %s -> %lx", b.c_str(), r);
        }
    return hr;
}

static long __stdcall h_unload(void* svc, const char** xs) {
    if (is_anchor(xs))
        for (auto& b : s_bundles) {
            const char* x = xstring_new(b.c_str());
            o_unload(svc, &x);
        }
    return o_unload(svc, xs);
}

struct Redirect { std::string disk, game; }; // "<dir>files\\", "root:/Mods/<Name>/files/"
static std::vector<Redirect> s_files;
typedef long(__stdcall* FsOpenFn)(void* fs, const char* path, int mode, void** out);
static FsOpenFn o_open;

static long __stdcall h_open(void* fs, const char* path, int mode, void** out) {
    const char* colon = path ? strstr(path, ":/") : nullptr;
    if (colon && strncmp(path, "root:", 5)) {
        std::string rel = std::string(path, colon) + "/" + (colon + 2); // "scripts/scripts/equip/X.txt"
        for (auto& r : s_files) {
            std::string disk = r.disk + rel;
            for (auto& c : disk) if (c == '/') c = '\\';
            if (GetFileAttributesA(disk.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            std::string game = r.game + rel;
            long hr = o_open(fs, game.c_str(), mode, out);
            wl_log("file %s -> %s (%lx)", path, game.c_str(), hr);
            return hr;
        }
    }
    return o_open(fs, path, mode, out);
}

// Hooked at the entry point: weapon scripts are read before the first frame.
static void hook_fs() {
    if (s_files.empty()) return;
    void* fn = (void*)g_game->fs_open;
    bool ok = MH_CreateHook(fn, (void*)h_open, (void**)&o_open) == MH_OK && MH_EnableHook(fn) == MH_OK;
    wl_log("file system open %p: %s", fn, ok ? "hooked" : "HOOK FAILED");
}

void assets_init() {
    for (Mod* m : g_mods)
        if (m->module && GetFileAttributesA((m->dir + "files").c_str()) != INVALID_FILE_ATTRIBUTES)
            s_files.push_back({m->dir + "files\\", "root:/Mods/" + m->name + "/files/"});
    hook_fs();
    for (Mod* m : g_mods) {
        if (!m->module) continue;
        WIN32_FIND_DATAA f;
        HANDLE h = FindFirstFileA((m->dir + "bundles\\*.bdl").c_str(), &f);
        if (h == INVALID_HANDLE_VALUE) continue;
        do s_bundles.push_back("root:/Mods/" + m->name + "/bundles/" + f.cFileName);
        while (FindNextFileA(h, &f));
        FindClose(h);
    }
    for (auto& b : s_bundles) wl_log("mod bundle %s", b.c_str());
}

// Runs every frame until the game's resource manager exists, then hooks the bundle service once.
void assets_frame() {
    static bool done;
    if (done || s_bundles.empty()) return;
    void* mgr = *(void**)g_game->resource_mgr;
    if (!mgr) return;
    done = true;
    typedef void*(__stdcall * QueryFn)(void*, const void*);
    void* svc = (*(QueryFn**)mgr)[0x58 / 4](mgr, (const void*)g_game->bundle_svc_guid);
    void** vt = *(void***)svc;
    bool ok = MH_CreateHook(vt[0x64 / 4], (void*)h_load, (void**)&o_load) == MH_OK &&
              MH_CreateHook(vt[0x68 / 4], (void*)h_unload, (void**)&o_unload) == MH_OK &&
              MH_EnableHook(vt[0x64 / 4]) == MH_OK && MH_EnableHook(vt[0x68 / 4]) == MH_OK;
    wl_log("bundle service %p load %p unload %p: %s", svc, vt[0x64 / 4], vt[0x68 / 4], ok ? "hooked" : "HOOK FAILED");
}
