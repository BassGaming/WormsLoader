// MatchStartFix (built in): online lobbies hang with everyone ready because the host's
// "RegisterBattle" request to Team17's (dead) server reports "pending" forever while the
// Team17 login stays busy. The game already skips a FAILED request for non-ranked lobbies and
// enters the match, so after kTimeout of "pending" we report the request as failed.
#include "loader.h"
#include <MinHook.h>
#include <map>

static const double kTimeout = 5.0;

// ServerAccess::Request (stdcall, 8 args; a4 = message name from ServerDataConfig).
// Returns 0 = done, 1/2 = pending (2 = re-login), -1 = HTTP error, -2 = failed.
typedef int(__stdcall* Req_t)(void*, void*, void*, const char*, void*, void*, void*, void*);
static Req_t o_req;

struct Track { double first = 0, last = 0; int result = 99; };
static std::map<std::string, Track> s_req; // game thread only

static int __stdcall h_req(void* sa, void* a2, void* a3, const char* name, void* a5, void* a6, void* a7, void* a8) {
    int r = o_req(sa, a2, a3, name, a5, a6, a7, a8);
    std::string key = name ? name : "?";
    Track& t = s_req[key];
    double now = wl_time();
    if (r > 0) {
        if (t.result <= 0 || t.result == 99 || now - t.last > 2) t.first = now; // a new wait starts
        if (now - t.first > kTimeout) {
            if (t.result != -1) wl_log("server request '%s' pending for %.0f s, reporting it as failed (MatchStartFix)", key.c_str(), kTimeout);
            r = -1;
        }
    }
    if (r != t.result) wl_log("server request '%s' -> %d", key.c_str(), r);
    t.result = r;
    t.last = now;
    return r;
}

void matchfix_init() {
    Mod* m = new Mod();
    m->name = "MatchStartFix";
    m->version = "1.0.0";
    m->author = "built into WormsLoader";
    m->description = "Fixes online lobbies where everyone is ready but the match never starts. When all players "
                     "are ready, the host registers the battle with Team17's server, which no longer answers, so "
                     "the game waited forever. After 5 seconds the request now counts as failed and the game "
                     "starts the match the way it already does for unreachable servers. Only the host needs it; "
                     "ranked/clan-ranked matches still need the server and are not fixed.";
    m->client_only = true; // doesn't change the simulation, vanilla players can still join
    g_mods.insert(g_mods.begin(), m);

    void* fn = (void*)g_game->server_request;
    bool ok = MH_CreateHook(fn, (void*)h_req, (void**)&o_req) == MH_OK && MH_EnableHook(fn) == MH_OK;
    if (!ok) m->error = "hook failed";
    wl_log("[MatchStartFix] server request %p: %s", fn, ok ? "hooked" : "HOOK FAILED");
}
