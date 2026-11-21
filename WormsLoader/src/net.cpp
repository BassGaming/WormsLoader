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

static Accessor_t SteamNetworking_, SteamMatchmaking_, SteamUser_, SteamHTTP_;
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

// ---- diagnostics: P2P traffic summary every 2 s while there is traffic ------------------------
static unsigned s_n_sent, s_n_fail, s_n_recv;
static double s_stat_at;
static void stat_tick() {
    double now = wl_time();
    if (now - s_stat_at < 2) return;
    if (s_n_sent || s_n_recv) wl_log("p2p: sent %u (%u failed), received %u", s_n_sent, s_n_fail, s_n_recv);
    s_n_sent = s_n_fail = s_n_recv = 0;
    s_stat_at = now;
}

// ---- ISteamNetworking hooks -------------------------------------------------------
static bool __fastcall h_Send(void* self, void*, u64 to, const void* d, unsigned n, int type, int ch) {
    if (!g_block_reason.empty()) return true; // refused lobby: swallow, game times out on its own
    if (!api_filter_send(to, d, n, type)) return true;
    stat_tick();
    s_n_sent++;
    if (s_sock != INVALID_SOCKET) { relay_tick(); relay_frame(to, d, n); return true; }
    bool ok = o_Send(self, to, d, n, type, ch);
    if (!ok) s_n_fail++;
    return ok;
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
        s_n_recv++;
        stat_tick();
        return true;
    }
}

// ---- Steam callbacks --------------------------------------------------------------
// Every callback object the game registers is replaced by a proxy, so mods can hide
// callbacks (e.g. SteamServersDisconnected) from the game.
static bool cb_pass(int id, void* p) {
    static int logged;
    bool pass = api_filter_steam_callback(id, p);
    if (id != 703 && logged < 400) { // 703 = API call completed, very frequent
        logged++; wl_log("steam callback %d%s", id, pass ? "" : " (hidden from game)"); }
    return pass;
}
struct CbProxy : CCallbackBase {
    CCallbackBase* game;
    void Run(void* p) override { if (cb_pass(m_iCallback, p)) game->Run(p); }
    void Run(void* p, bool io, u64 c) override { if (cb_pass(m_iCallback, p)) game->Run(p, io, c); }
    int GetCallbackSizeBytes() override { return game->GetCallbackSizeBytes(); }
};
static std::vector<CbProxy*> s_proxies;

// One proxy per game callback object, reused when the game registers the same object again (it registers
// every callback before AND after SteamAPI_Init). Steam then sees the same pointer twice, exactly as in vanilla;
// a new proxy per call made Steam deliver every callback to the game twice. Proxies are never freed because
// Steam may still hold a second registration of one (shortcut: leaks ~70 small objects, fine for a game process).
static CbProxy* proxy_for(CCallbackBase* cb) {
    for (CbProxy* p : s_proxies) if (p->game == cb) return p;
    return nullptr;
}

static void __cdecl h_Register(CCallbackBase* cb, int id) {
    CbProxy* p = proxy_for(cb);
    if (!p) { p = new CbProxy(); p->game = cb; s_proxies.push_back(p); }
    p->m_iCallback = id; // the address may have been reused by another callback object
    o_Register(p, id);
    cb->m_iCallback = id;
    cb->m_nCallbackFlags |= 1; // so the game's destructor still calls Unregister
}

static void __cdecl h_Unregister(CCallbackBase* cb) {
    CbProxy* p = proxy_for(cb);
    o_Unregister(p ? (CCallbackBase*)p : cb); // not ours = registered before our hook
    cb->m_nCallbackFlags &= ~1;
}

// ---- diagnostics: the game's HTTP traffic (ISteamHTTP002) to the Team17 backend ----------------
// Logs method + URL, header NAMES (Authorization carries the login), POST bodies, response status and bodies.
typedef unsigned(__thiscall* HttpCreate_t)(void*, int method, const char* url);                         // [0]
typedef bool(__thiscall* HttpHeader_t)(void*, unsigned h, const char* name, const char* value);          // [3]
typedef bool(__thiscall* HttpParam_t)(void*, unsigned h, const char* name, const char* value);           // [4]
typedef bool(__thiscall* HttpBody_t)(void*, unsigned h, unsigned char* buf, unsigned size);              // [12]
typedef bool(__thiscall* HttpRawPost_t)(void*, unsigned h, const char* type, unsigned char* body, unsigned n); // [16]
static HttpCreate_t o_HttpCreate;
static HttpHeader_t o_HttpHeader;
static HttpParam_t o_HttpParam;
static HttpBody_t o_HttpBody;
static HttpRawPost_t o_HttpRawPost;

static void log_text(const char* what, unsigned h, const void* d, unsigned n) {
    std::string t((const char*)d, n > 1500 ? 1500 : n);
    for (char& c : t) if (c == 13 || c == 10) c = ' ';
    wl_log("http #%u %s (%u bytes): %s", h, what, n, t.c_str());
}
static unsigned __fastcall h_HttpCreate(void* self, void*, int method, const char* url) {
    static const char* kM[] = {"INVALID", "GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS"};
    unsigned h = o_HttpCreate(self, method, url);
    wl_log("http #%u %s %s", h, method >= 0 && method < 7 ? kM[method] : "?", url ? url : "(null)");
    return h;
}
static bool __fastcall h_HttpHeader(void* self, void*, unsigned h, const char* name, const char* value) {
    bool secret = name && !_stricmp(name, "Authorization");
    wl_log("http #%u header %s: %s", h, name ? name : "?", secret ? "(hidden)" : value ? value : "");
    return o_HttpHeader(self, h, name, value);
}
static bool __fastcall h_HttpParam(void* self, void*, unsigned h, const char* name, const char* value) {
    wl_log("http #%u param %s=%s", h, name ? name : "?", value ? value : "");
    return o_HttpParam(self, h, name, value);
}
static bool __fastcall h_HttpBody(void* self, void*, unsigned h, unsigned char* buf, unsigned size) {
    bool ok = o_HttpBody(self, h, buf, size);
    if (ok) log_text("response body", h, buf, size);
    return ok;
}
static bool __fastcall h_HttpRawPost(void* self, void*, unsigned h, const char* type, unsigned char* body, unsigned n) {
    log_text(type ? type : "post body", h, body, n);
    return o_HttpRawPost(self, h, type, body, n);
}
// Swap one entry of an interface's vtable. NOT an inline hook: steamclient's interface methods are tiny thunks
// that the linker folds across interfaces, so patching the code also caught other interfaces' calls (crashed
// on lobby creation).
static void vt_hook(void** vt, int i, void* fn, void** orig) {
    DWORD old;
    if (!VirtualProtect(&vt[i], sizeof(void*), PAGE_READWRITE, &old)) { wl_log("vt_hook %d failed", i); return; }
    *orig = vt[i];
    vt[i] = fn;
    VirtualProtect(&vt[i], sizeof(void*), old, &old);
}

static void http_install() {
    void* http = SteamHTTP_ ? SteamHTTP_() : nullptr;
    if (!http) { wl_log("SteamHTTP not available"); return; }
    void** vt = vtbl(http);
    vt_hook(vt, 0, (void*)h_HttpCreate, (void**)&o_HttpCreate);
    vt_hook(vt, 3, (void*)h_HttpHeader, (void**)&o_HttpHeader);
    vt_hook(vt, 4, (void*)h_HttpParam, (void**)&o_HttpParam);
    vt_hook(vt, 12, (void*)h_HttpBody, (void**)&o_HttpBody);
    vt_hook(vt, 16, (void*)h_HttpRawPost, (void**)&o_HttpRawPost);
}

// Call results: HTTPRequestCompleted_t (2101) carries the status code. Each 2101 call result the game registers
// is wrapped in a one-shot proxy that logs it and forwards; the game sees the same Run calls as before.
typedef void(__cdecl* RegisterCR_t)(CCallbackBase*, u64);
static RegisterCR_t o_RegisterCR, o_UnregisterCR;
struct HttpCompleted_t { unsigned handle; u64 context; bool ok; int status; };
struct CrProxy : CCallbackBase {
    CCallbackBase* game; u64 call;
    void Run(void* p) override { Run(p, false, call); }
    void Run(void* p, bool io, u64 c) override {
        HttpCompleted_t* r = (HttpCompleted_t*)p;
        wl_log("http #%u completed: %s, status %d%s", r->handle, r->ok ? "ok" : "FAILED", r->status, io ? " (io failure)" : "");
        game->Run(p, io, c);
    }
    int GetCallbackSizeBytes() override { return game->GetCallbackSizeBytes(); }
};
static std::vector<CrProxy*> s_cr; // shortcut: completed proxies are never freed (a few per request), diagnostic only
static void __cdecl h_RegisterCR(CCallbackBase* cb, u64 call) {
    if (cb->m_iCallback != 2101) { o_RegisterCR(cb, call); return; }
    CrProxy* p = new CrProxy();
    p->game = cb; p->call = call; p->m_iCallback = 2101;
    s_cr.push_back(p);
    o_RegisterCR(p, call);
}
static void __cdecl h_UnregisterCR(CCallbackBase* cb, u64 call) {
    for (CrProxy* p : s_cr) if (p->game == cb && p->call == call) { o_UnregisterCR(p, call); return; }
    o_UnregisterCR(cb, call);
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
    vt_hook(vtbl(net), 0, (void*)h_Send, (void**)&o_Send);
    vt_hook(vtbl(net), 2, (void*)h_Read, (void**)&o_Read);
    http_install();
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
    SteamHTTP_ = (Accessor_t)GetProcAddress(s, "SteamHTTP");
    g_api.hook(GetProcAddress(s, "SteamAPI_RegisterCallResult"), (void*)h_RegisterCR, (void**)&o_RegisterCR);
    g_api.hook(GetProcAddress(s, "SteamAPI_UnregisterCallResult"), (void*)h_UnregisterCR, (void**)&o_UnregisterCR);
    g_api.hook(GetProcAddress(s, "SteamAPI_Init"), (void*)h_Init, (void**)&o_Init);
    g_api.hook(GetProcAddress(s, "SteamAPI_RegisterCallback"), (void*)h_Register, (void**)&o_Register);
    g_api.hook(GetProcAddress(s, "SteamAPI_UnregisterCallback"), (void*)h_Unregister, (void**)&o_Unregister);
}
