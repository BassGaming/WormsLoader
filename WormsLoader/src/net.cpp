// Network layer: Steam P2P hooks, optional UDP relay transport, Steam callback filtering,
// and the mod-set handshake through Steam lobby data.
//
// RE notes (build 01.83.31.37): the game only uses ISteamNetworking005 slot 0 SendP2PPacket
// (unreliable send types 0/1, channel 0) and slot 2 ReadP2PPacket (1200-byte buffer, polled),
// plus slot 3 AcceptP2PSessionWithUser. Reliability and timeouts are the game's own.
#include "loader.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <MinHook.h>
#include <cstdio>
#include <cstring>

typedef unsigned long long u64;

// ---- Steam ABI (as compiled into the game; MSVC, Steamworks ~1.2x) -------------------------
class CCallbackBase {
public:
    virtual void Run(void* param) = 0;
    virtual void Run(void* param, bool io_failure, u64 api_call) = 0;
    virtual int GetCallbackSizeBytes() = 0;
    unsigned char m_nCallbackFlags = 0; // 1 = registered
    int m_iCallback = 0;
};
typedef void* (__cdecl* Accessor_t)();
typedef bool(__cdecl* SteamAPI_Init_t)();
typedef void(__cdecl* RegisterCallback_t)(CCallbackBase*, int);
typedef void(__cdecl* UnregisterCallback_t)(CCallbackBase*);

static void** vtbl(void* iface) { return *(void***)iface; }
// ISteamUser017[2] GetSteamID and ISteamMatchmaking009[35] GetLobbyOwner return CSteamID via hidden pointer.
typedef u64*(__thiscall* GetSteamID_t)(void*, u64* out);
typedef u64*(__thiscall* GetLobbyOwner_t)(void*, u64* out, u64 lobby);
typedef const char*(__thiscall* GetLobbyData_t)(void*, u64 lobby, const char* key); // [19]
typedef bool(__thiscall* SetLobbyData_t)(void*, u64 lobby, const char* key, const char* val); // [20]
typedef bool(__thiscall* SendP2P_t)(void*, u64 to, const void* d, unsigned n, int type, int ch); // [0]
typedef bool(__thiscall* ReadP2P_t)(void*, void* d, unsigned cap, unsigned* n, u64* from, int ch); // [2]

static Accessor_t SteamNetworking_, SteamMatchmaking_, SteamUser_;
static SteamAPI_Init_t o_Init;
static RegisterCallback_t o_Register;
static UnregisterCallback_t o_Unregister;
static SendP2P_t o_Send;
static ReadP2P_t o_Read;

std::string g_net_status = "Steam P2P";
std::string g_block_reason;
static double s_last_recv = -1e9;
static u64 s_me, s_lobby;
double net_last_recv() { return s_last_recv; }

// ---- relay transport --------------------------------------------------------------
// Frame: "WLR1" | lobby u64 | from u64 | to u64 | payload. to = 0 is a keep-alive/hello.
static const char kMagic[4] = {'W', 'L', 'R', '1'};
constexpr int kHdr = 28;
static SOCKET s_sock = INVALID_SOCKET;
static sockaddr_storage s_relay;
static int s_relay_len;
static double s_last_hello;

static void relay_frame(u64 to, const void* d, unsigned n) {
    char buf[kHdr + 1500];
    if (n > 1500) return;
    memcpy(buf, kMagic, 4);
    memcpy(buf + 4, &s_lobby, 8);
    memcpy(buf + 12, &s_me, 8);
    memcpy(buf + 20, &to, 8);
    if (n) memcpy(buf + kHdr, d, n);
    sendto(s_sock, buf, kHdr + n, 0, (sockaddr*)&s_relay, s_relay_len);
}

static void relay_stop() {
    if (s_sock != INVALID_SOCKET) closesocket(s_sock);
    s_sock = INVALID_SOCKET;
    g_net_status = "Steam P2P";
}

static void relay_start(const std::string& addr) {
    relay_stop();
    size_t colon = addr.rfind(':');
    if (colon == std::string::npos) { wl_log("relay address '%s' needs host:port", addr.c_str()); return; }
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(addr.substr(0, colon).c_str(), addr.substr(colon + 1).c_str(), &hints, &res) || !res) {
        wl_log("relay '%s': cannot resolve", addr.c_str());
        g_net_status = "Relay " + addr + " (unresolvable, using Steam P2P)";
        return;
    }
    memcpy(&s_relay, res->ai_addr, res->ai_addrlen);
    s_relay_len = (int)res->ai_addrlen;
    freeaddrinfo(res);
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long nb = 1; ioctlsocket(s_sock, FIONBIO, &nb);
    relay_frame(0, nullptr, 0);
    s_last_hello = wl_time();
    g_net_status = "Relay " + addr;
    wl_log("relay transport: %s", addr.c_str());
}

static void relay_tick() {
    if (s_sock == INVALID_SOCKET) return;
    double now = wl_time();
    if (now - s_last_hello > 1.0) { relay_frame(0, nullptr, 0); s_last_hello = now; }
}

// Returns true and fills the game's buffer if a relay frame for this lobby is waiting.
static bool relay_read(void* d, unsigned cap, unsigned* n, u64* from) {
    char buf[kHdr + 1500];
    for (;;) {
        int r = recv(s_sock, buf, sizeof buf, 0);
        if (r <= 0) return false; // WSAEWOULDBLOCK or ICMP error: nothing to read now
        u64 lobby;
        memcpy(&lobby, buf + 4, 8);
        if (r < kHdr || memcmp(buf, kMagic, 4) || lobby != s_lobby || unsigned(r - kHdr) > cap) continue; // untrusted input
        memcpy(from, buf + 12, 8);
        *n = r - kHdr;
        memcpy(d, buf + kHdr, *n);
        return true;
    }
}

// ---- ISteamNetworking hooks -------------------------------------------------------
static bool __fastcall h_Send(void* self, void*, u64 to, const void* d, unsigned n, int type, int ch) {
    if (!g_block_reason.empty()) return true; // refused lobby: swallow, game times out on its own
    if (!api_filter_send(to, d, n, type)) return true;
    if (s_sock != INVALID_SOCKET) { relay_tick(); relay_frame(to, d, n); return true; }
    return o_Send(self, to, d, n, type, ch);
}

static bool __fastcall h_Read(void* self, void*, void* d, unsigned cap, unsigned* n, u64* from, int ch) {
    for (;;) {
        bool got = false;
        if (s_sock != INVALID_SOCKET) { relay_tick(); got = relay_read(d, cap, n, from); }
        if (!got) got = o_Read(self, d, cap, n, from, ch);
        if (!got) return false;
        if (!g_block_reason.empty()) continue;           // drain and drop
        if (!api_filter_recv(*from, d, *n)) continue;    // a mod dropped it
        s_last_recv = wl_time();
        return true;
    }
}

// ---- Steam callbacks --------------------------------------------------------------
// Every callback object the game registers is replaced by a proxy, so mods can hide
// callbacks (e.g. SteamServersDisconnected) from the game.
struct CbProxy : CCallbackBase {
    CCallbackBase* game;
    void Run(void* p) override { if (api_filter_steam_callback(m_iCallback, p)) game->Run(p); }
    void Run(void* p, bool io, u64 c) override { if (api_filter_steam_callback(m_iCallback, p)) game->Run(p, io, c); }
    int GetCallbackSizeBytes() override { return game->GetCallbackSizeBytes(); }
};
static std::vector<CbProxy*> s_proxies;

static void __cdecl h_Register(CCallbackBase* cb, int id) {
    CbProxy* p = new CbProxy();
    p->game = cb;
    p->m_iCallback = id;
    s_proxies.push_back(p);
    o_Register(p, id);
    cb->m_iCallback = id;
    cb->m_nCallbackFlags |= 1; // so the game's destructor still calls Unregister
}

static void __cdecl h_Unregister(CCallbackBase* cb) {
    for (size_t i = 0; i < s_proxies.size(); i++) {
        if (s_proxies[i]->game != cb) continue;
        o_Unregister(s_proxies[i]);
        delete s_proxies[i];
        s_proxies.erase(s_proxies.begin() + i);
        cb->m_nCallbackFlags &= ~1;
        return;
    }
    o_Unregister(cb); // not ours (registered before our hook)
}

// ---- mod-set handshake ------------------------------------------------------------
struct LobbyEnter_t { u64 lobby; unsigned perms; bool locked; unsigned response; };

struct LobbyEnterCb : CCallbackBase {
    void Run(void* p) override { on_enter(((LobbyEnter_t*)p)->lobby); }
    void Run(void* p, bool, u64) override { Run(p); }
    int GetCallbackSizeBytes() override { return sizeof(LobbyEnter_t); }

    static void on_enter(u64 lobby) {
        void* mm = SteamMatchmaking_();
        u64 owner = 0;
        ((GetLobbyOwner_t)vtbl(mm)[35])(mm, &owner, lobby);
        auto get = [&](const char* k) { const char* v = ((GetLobbyData_t)vtbl(mm)[19])(mm, lobby, k); return std::string(v ? v : ""); };
        char ours[16];
        sprintf_s(ours, "%08x", g_modset_hash);
        s_lobby = lobby;
        g_block_reason.clear();

        if (owner == s_me) {
            auto set = (SetLobbyData_t)vtbl(mm)[20];
            set(mm, lobby, "wl_modset", ours);
            set(mm, lobby, "wl_mods", g_modset.c_str());
            std::string relay = loader_ini("relay", "");
            set(mm, lobby, "wl_relay", relay.c_str());
            if (relay.empty()) relay_stop(); else relay_start(relay);
            wl_log("lobby %llx: hosting, mod set %s, relay '%s'", lobby, ours, relay.c_str());
            return;
        }

        std::string theirs = get("wl_modset");
        if (theirs.empty() ? g_modset_hash != 0 : theirs != ours)
            g_block_reason = theirs.empty()
                ? "Host has no WormsLoader mods, you have: " + g_modset
                : "Mod set differs.\nHost: " + get("wl_mods") + "\nYou:  " + g_modset;
        wl_log("lobby %llx: host mod set '%s', ours %s -> %s", lobby, theirs.c_str(), ours,
               g_block_reason.empty() ? "ok" : "REFUSED");
        std::string relay = get("wl_relay");
        if (g_block_reason.empty() && !relay.empty()) relay_start(relay); else relay_stop();
    }
};
static LobbyEnterCb s_lobby_cb;

static bool __cdecl h_Init() {
    bool ok = o_Init();
    loader_late_init();
    if (!ok) { wl_log("SteamAPI_Init failed, network layer off"); return ok; }

    void* user = SteamUser_();
    ((GetSteamID_t)vtbl(user)[2])(user, &s_me);
    void* net = SteamNetworking_();
    g_api.hook(vtbl(net)[0], (void*)h_Send, (void**)&o_Send);
    g_api.hook(vtbl(net)[2], (void*)h_Read, (void**)&o_Read);
    s_lobby_cb.m_iCallback = 504; // LobbyEnter_t
    o_Register(&s_lobby_cb, 504);
    wl_log("network layer ready, local steam id %llx", s_me);
    return ok;
}

void net_install() {
    HMODULE s = GetModuleHandleA("steam_api.dll");
    if (!s) { wl_log("steam_api.dll not loaded, network layer off"); return; }
    SteamNetworking_ = (Accessor_t)GetProcAddress(s, "SteamNetworking");
    SteamMatchmaking_ = (Accessor_t)GetProcAddress(s, "SteamMatchmaking");
    SteamUser_ = (Accessor_t)GetProcAddress(s, "SteamUser");
    g_api.hook(GetProcAddress(s, "SteamAPI_Init"), (void*)h_Init, (void**)&o_Init);
    g_api.hook(GetProcAddress(s, "SteamAPI_RegisterCallback"), (void*)h_Register, (void**)&o_Register);
    g_api.hook(GetProcAddress(s, "SteamAPI_UnregisterCallback"), (void*)h_Unregister, (void**)&o_Unregister);
}
