// Reconnect: keep an online match alive through a short connection loss.
//
// The game sends only unreliable Steam P2P packets and runs its own reliability layer, so lost
// packets are resent by the game itself once the link is back. What kills the match is
//   (a) Steam callbacks telling the game the connection is gone, and
//   (b) the game's own per-peer timeout (location still unknown, see CLAUDE.md).
// (a) is handled here. For (b) the "Diagnose drops" setting logs where the game destroys a peer
// link, which pinpoints the timeout code for the next version.
#include <windows.h>
#include <map>
#include "wormsloader.h"

static const WL_Api* api;
static bool* s_enabled;
static int* s_max_outage;
static bool* s_diagnose;
static std::map<WL_SteamID, double> s_peer_recv; // last packet time per peer

static const WL_ModInfo kInfo = {
    "Reconnect", "0.2.0", "WormsLoader",
    "Keeps an online match running through a short connection loss instead of dropping you to the menu.\n\n"
    "While packets from the match arrived within the last 'Max outage' seconds, Steam's "
    "'connection lost' notifications are hidden from the game; the game's own netcode resends what was "
    "lost once the link returns.\n\n"
    "All players in the lobby need this mod (same version).",
    false, "WormsClanWars",
};

// LobbyChatUpdate_t (callback 506)
#pragma pack(push, 8)
struct LobbyChatUpdate { WL_SteamID lobby, changed, making_change; unsigned state; };
#pragma pack(pop)
enum { kMemberDisconnected = 4 };

static bool on_steam_callback(int id, void* data, void*) {
    if (!*s_enabled) return true;
    if (id == 506) {
        // When a player's Steam connection drops, Steam removes them from the lobby as
        // "disconnected" once they come back, and the host's game then destroys their peer link
        // although match packets are flowing again. Hide that while the player is still sending.
        auto* u = (LobbyChatUpdate*)data;
        auto it = s_peer_recv.find(u->changed);
        double since = it == s_peer_recv.end() ? -1 : api->time() - it->second;
        api->log("[Reconnect] lobby member %llx state change 0x%x, last packet %.1fs ago", u->changed, u->state, since);
        if (!(u->state & kMemberDisconnected) || since < 0 || since > *s_max_outage) return true;
        api->log("[Reconnect] hid the lobby disconnect of %llx from the game", u->changed);
        return false;
    }
    // 102 SteamServerConnectFailure, 103 SteamServersDisconnected, 1203 P2PSessionConnectFail
    if (id != 102 && id != 103 && id != 1203) return true;
    double since = api->seconds_since_net_recv();
    if (since > *s_max_outage) return true; // not in a match, or outage too long: let the game handle it
    api->log("[Reconnect] hid Steam callback %d from the game (last match packet %.1fs ago)", id, since);
    return false;
}

static bool on_recv(WL_SteamID from, const void*, unsigned, void*) {
    s_peer_recv[from] = api->time();
    return true;
}

// Peer link object (vtable 0xdffe58), slot 0 = scalar deleting destructor. Steam ID at +0xf0.
typedef void*(__thiscall* LinkDtor_t)(void*, unsigned);
static LinkDtor_t o_LinkDtor;
static void* __fastcall h_LinkDtor(void* self, void*, unsigned flags) {
    if (*s_diagnose) {
        WL_SteamID peer = *(WL_SteamID*)((char*)self + 0xf0);
        auto it = s_peer_recv.find(peer);
        double since = it == s_peer_recv.end() ? -1 : api->time() - it->second;
        void* frames[16];
        USHORT n = CaptureStackBackTrace(1, 16, frames, nullptr);
        char buf[16 * 11 + 1] = {};
        for (USHORT i = 0; i < n; i++) sprintf_s(buf + i * 11, 12, " %08X ", (unsigned)(uintptr_t)frames[i]);
        api->log("[Reconnect] peer link %llx destroyed, last packet from it %.1fs ago. stack:%s", peer, since, buf);
    }
    return o_LinkDtor(self, flags);
}

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api* a) {
    api = a;
    s_enabled = api->setting_bool("enabled", "Enabled", true);
    s_max_outage = api->setting_int("max_outage_seconds", "Max outage (seconds)", 120, 10, 600);
    s_diagnose = api->setting_bool("diagnose", "Diagnose drops (log to WormsLoader.log)", true);
    api->on_steam_callback(on_steam_callback, nullptr);
    api->on_recv(on_recv, nullptr);
    api->hook(api->exe(0x523750), (void*)h_LinkDtor, (void**)&o_LinkDtor);
}
