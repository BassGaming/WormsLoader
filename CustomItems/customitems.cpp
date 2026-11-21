// CustomItems: the Stock Crash weapon. It lives in the game's unused weapon slot 17 (PADDING1): the slot's
// weapon-table record, crate weight and panel button are patched at the entry point, before the game reads
// them. Its script is files\scripts\scripts\equip\StockCrash.txt, its chart model is in bundles\CustomItems.bdl
// (both served by the loader). Damage is not in scripts (the game's Story data, per payload StoryName), so it
// is overridden here, in Worm::ShockDamage, while one of the weapon's payloads explodes.
#include <string.h>
#include "wormsloader.h"

static const WL_Api* api;

static const WL_ModInfo kInfo = {
    "CustomItems", "0.2.0", "WormsLoader",
    "Adds the Stock Crash: a bazooka whose shell bounces for the chosen fuse time (1-5 s, like the grenade), "
    "then crashes harmlessly into 8 red falling charts. Each chart that hits a worm deals 10-69 damage. "
    "Found in weapon crates.",
    false, "WormsClanWars", // gameplay: every player needs it
};

enum { kId = 17 };                                        // PADDING1
static const char kShellStory[] = "GrenadeRound";         // StoryNames in StockCrash.txt
static const char kChartStory[] = "ClusterBombletRound";

// Game addresses (build 01.83.31.37)
enum : unsigned {
    kWeaponTable = 0xd37738,  // 71 x {name, gadget type, payload type, prompt, flags}
    kCrateWeights = 0xcddbf8, // float[71], read once on the first crate
    kButtonsModern = 0xed6790, kButtonsClassic = 0xed6b00, // 71 x {panel cell (WeaponButton%02d), cost, cost name}
    kSetEquip = 0x7da080,     // Worm::SetEquip, stdcall(worm, id)
    kExplode = 0x93fd20,      // Payload explode, thiscall(payload)
    kShockDamage = 0x7d9010,  // Worm vtable slot 23, thiscall(worm, story ctx, float out[4]); out[0] = damage
    kObjects = 0x194f6c8,     // physics objects: base + idx * 0xa4 + 0xa0240 = float x, y (idx = word payload+0x2c)
    kPanelRefresh = 0x89e2a0, // HudEquipPanel refresh, stdcall(panel); every frame while the panel is open
    kPanelButton = 0x89dbf0,  // HudEquipPanel::GetButton, thiscall(panel, cell) -> element "WeaponButton%02d"
    kButtonSet = 0xa1be90,    // see set_button
    kPanelUpdate = 0x89e440,  // HudEquipPanel per-frame update, thiscall(panel); calls the refresh while shown
    kPanelInventory = 0x89d5d0, kAmmoKey = 0xed66a4, // see give_weapons
    kCacheLoad = 0x7bcd00, kCacheBuild = 0x7bce90, kCacheWrite = 0x7e2580, // WeaponCache.aim, stdcall(x)
};

// The mod's weapons: free slots of the game's weapon table (PADDING1-4 = 17, 18, 45, 47).
struct Weapon { int id; const char* name; int gadget, payload; };
static const Weapon kWeapons[] = {
    {kId, "StockCrash", 10, 4},  // launcher + cluster payload, like ClusterBomb
    {18, "TestBazooka", 10, 1},  // shortcut: test copy of Bazooka.txt to tell slot problems from script problems; remove
};

static int s_equip;     // last weapon equipped by any worm (game logic, same on every client)
static int s_exploding; // 0, 1 = Stock Crash shell, 2 = Stock Crash chart
static unsigned s_seed, s_hits;

static int(__stdcall* o_set_equip)(void*, int);
static int __stdcall h_set_equip(void* worm, int id) {
    s_equip = id;
    return o_set_equip(worm, id);
}

static void(__fastcall* o_explode)(void*, void*);
static void __fastcall h_explode(void* payload, void* edx) {
    if (s_equip != kId) return o_explode(payload, edx);
    // vtable +0xc0: the payload's StoryName (what the game passes to the Story lookup)
    const char* story = ((const char*(__fastcall*)(void*, void*))(*(void***)payload)[0xc0 / 4])(payload, 0);
    int kind = !story ? 0 : !strcmp(story, kShellStory) ? 1 : !strcmp(story, kChartStory) ? 2 : 0;
    const float* pos = (const float*)(*(char**)kObjects + *(unsigned short*)((char*)payload + 0x2c) * 0xa4 + 0xa0240);
    unsigned x, y;
    memcpy(&x, pos, 4); memcpy(&y, pos + 1, 4);
    s_seed = x * 2654435761u ^ y * 2246822519u; // lockstep: positions match on every client
    s_hits = 0;
    api->log("[CustomItems] explode %s (%d) at %.1f %.1f", story ? story : "?", kind, pos[0], pos[1]);
    int prev = s_exploding;
    s_exploding = kind;
    o_explode(payload, edx);
    s_exploding = prev;
}

static unsigned rng() {
    unsigned h = s_seed + 0x9e3779b9u * ++s_hits;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}

static int(__fastcall* o_shock)(void*, void*, void*, float*);
static int __fastcall h_shock(void* worm, void* edx, void* ctx, float* out) {
    int r = o_shock(worm, edx, ctx, out);
    if (s_exploding && out && out[0] > 0) { // 0 = the worm is out of the blast
        float game = out[0];
        out[0] = s_exploding == 1 ? 0.0f : (float)(10 + rng() % 60);
        api->log("[CustomItems] worm %p damage %.0f -> %.0f", worm, game, out[0]);
    }
    return r;
}

// Aiming needs each weapon's launch points (per worm class and aim angle), which the game loads at match load from
// the precomputed common:/WeaponCache.aim (0x7bcd00). That file has no entries for the free slots (no aim, shots
// drop at the worm's feet with 0 power). So the game's own builder (0x7bce90, normally behind
// /CREATECOMPILEDFILES) runs instead: ~0.2 s, from the loaded weapon scripts, same result on every client.
// Its file writer (0x7e2580) is skipped: it is broken in the release exe (writes through a null stream).
static void(__stdcall* o_cache_load)(void*);
static void __stdcall h_cache_load(void* x) {
    double t = api->time();
    ((void(__stdcall*)(void*))api->exe(kCacheBuild))(x);
    api->log("[CustomItems] weapon launch cache built (%.2f s)", api->time() - t);
}
static long(__stdcall* o_cache_write)(void*);
static long __stdcall h_cache_write(void*) { return 0; }

static void patch(unsigned va, const void* p, unsigned n) {
    if (api->patch(api->exe(va), p, n)) api->log("[CustomItems] patch %x failed", va);
}

// Weapon panel pages. The panel layout (DataPC\FrontEnd.gui) has exactly 65 cells (WeaponButton00-64), all used
// by the game's weapons. Page 0 = the game's layout; every further page clears all cells and shows the next 65
// mod weapons. A page is the cell column of the two button tables; the panel refresh (every frame while it is
// open) assigns each mapped weapon to its button, and buttons left unmapped are reset to "Nothing" here.
enum { kCells = 65 };
enum { kModCount = sizeof kWeapons / sizeof *kWeapons, kPages = 1 + (kModCount + kCells - 1) / kCells };
static int s_cells[2][71]; // original cells, modern/classic
static int s_page, s_shown = -1;

static void pages_init() {
    const unsigned tables[2] = {kButtonsModern, kButtonsClassic};
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 71; i++) s_cells[t][i] = *(int*)api->exe(tables[t] + 12 * i);
    for (const Weapon& w : kWeapons) s_cells[0][w.id] = s_cells[1][w.id] = -1;
}

static void pages_apply(int page) {
    const unsigned tables[2] = {kButtonsModern, kButtonsClassic};
    for (int t = 0; t < 2; t++)
        for (int i = 0; i < 71; i++) *(int*)api->exe(tables[t] + 12 * i) = page ? -1 : s_cells[t][i]; // .data: writable
    for (int k = 0; page && k < kModCount; k++)
        if (k / kCells == page - 1)
            for (int t = 0; t < 2; t++) *(int*)api->exe(tables[t] + 12 * kWeapons[k].id) = k % kCells;
}

static void* get_button(void* panel, int cell) { // HudEquipPanel cell -> its WeaponButton%02d element (or null)
    return ((void*(__thiscall*)(void*, int))api->exe(kPanelButton))(panel, cell);
}

// Button::Set(style, id, ammo, delay): esi = button, ecx = style (panel +0x5dc), stdcall-style args, ret 0xc
static void set_button(void* button, int style, int id, int ammo, int delay) {
    void* fn = api->exe(kButtonSet);
    __asm {
        push delay
        push ammo
        push id
        mov esi, button
        mov ecx, style
        call fn
    }
}

static void(__stdcall* o_refresh)(void*);
static void* s_panel; // the live panel (last update/refresh)
static void __stdcall h_refresh(void* panel) {
    s_panel = panel;
    s_page %= kPages;
    if (s_page != s_shown) {
        pages_apply(s_page);
        api->log("[CustomItems] weapon panel page %d/%d", s_page + 1, kPages);
    }
    // reset the buttons this page does not map (they keep the weapon id of the page shown before)
    if (s_page != s_shown) {
        bool used[kCells] = {};
        for (int i = 0; i < 71; i++) {
            int c = *(int*)api->exe(kButtonsModern + 12 * i);
            if (c >= 0 && c < kCells) used[c] = true;
        }
        for (int c = 0; c < kCells; c++)
            if (!used[c])
                if (void* b = get_button(panel, c)) set_button(b, *(int*)((char*)panel + 0x5dc), 0, 0, 0xff);
        s_shown = s_page;
    }
    o_refresh(panel);
}

// The panel only refreshes while it is shown (and not at all in the first turn), so a page change would wait:
// its per-frame update (called for the live panel, game thread) forces the refresh right away.
static void(__fastcall* o_update)(void*, void*);
static void __fastcall h_update(void* panel, void* edx) {
    s_panel = panel;
    if (s_page % kPages != s_shown) h_refresh(panel);
    o_update(panel, edx);
}

// Testing only (desyncs online): give the panel's team one of each mod weapon. The panel's inventory
// (HudEquipPanel 0x89d5d0, esi = panel) is 71 records of 20 bytes; +0xc = ammo xor the key at 0xed66a4.
static void give_weapons(void*) {
    if (!s_panel) return;
    void* fn = api->exe(kPanelInventory);
    void* panel = s_panel;
    char* inv;
    __asm {
        mov esi, panel
        call fn
        mov inv, eax
    }
    unsigned key = *(unsigned*)api->exe(kAmmoKey);
    for (const Weapon& w : kWeapons) {
        memcpy(inv + 20 * w.id, inv + 20 * 1, 20); // a Bazooka record (availability flags), then ammo 1
        *(unsigned*)(inv + 20 * w.id + 0xc) = 1 ^ key;
    }
    api->log("[CustomItems] test: gave 1 of each mod weapon (inventory %p)", inv);
}

static void next_page(void*) {
    s_page = (s_page + 1) % kPages;
    api->log("[CustomItems] next page key -> page %d", s_page + 1);
}

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

// At the entry point: the tables must be patched before the game reads them (scripts load before the first frame).
extern "C" __declspec(dllexport) void WL_ModEarlyInit(const WL_Api* a) {
    api = a;
    bool* only = api->setting_bool("crate_test", "Testing: weapon crates always hold the Stock Crash (restart to apply)", false);

    float one = 1.0f, zero = 0.0f;
    if (*only)
        for (int i = 0; i < 71; i++) patch(kCrateWeights + 4 * i, &zero, 4);
    struct Rec { const char* name; int gadget, payload; const char* prompt; int flags; };
    struct Button { int cell, cost; const char* cost_name; };
    static Rec recs[kModCount];
    static const Button btn = {-1, 30, "HolyHandGrenadeCost"}; // cost name = an existing HUD.txt key; cell set per page
    for (int k = 0; k < kModCount; k++) {
        const Weapon& w = kWeapons[k];
        recs[k] = {w.name, w.gadget, w.payload, "Gui.WeaponPanel..Prompt", 1}; // shortcut: no prompt text yet
        patch(kWeaponTable + 20 * w.id, &recs[k], sizeof(Rec));
        patch(kCrateWeights + 4 * w.id, &one, 4);
        patch(kButtonsModern + 12 * w.id, &btn, sizeof btn);
        patch(kButtonsClassic + 12 * w.id, &btn, sizeof btn);
    }
    pages_init();
    api->hotkey("next_page", "Weapon panel: next page (mod weapons)", 0x22 /*VK_NEXT*/, next_page, nullptr);
    if (*only) api->hotkey("give", "Testing: give the current team 1 of each mod weapon", 0x21 /*VK_PRIOR*/, give_weapons, nullptr);

    bool ok = !api->hook(api->exe(kSetEquip), (void*)h_set_equip, (void**)&o_set_equip) &&
              !api->hook(api->exe(kExplode), (void*)h_explode, (void**)&o_explode) &&
              !api->hook(api->exe(kShockDamage), (void*)h_shock, (void**)&o_shock) &&
              !api->hook(api->exe(kPanelRefresh), (void*)h_refresh, (void**)&o_refresh) &&
              !api->hook(api->exe(kPanelUpdate), (void*)h_update, (void**)&o_update) &&
              !api->hook(api->exe(kCacheLoad), (void*)h_cache_load, (void**)&o_cache_load) &&
              !api->hook(api->exe(kCacheWrite), (void*)h_cache_write, (void**)&o_cache_write);
    api->log("[CustomItems] Stock Crash in slot %d%s, hooks %s", kId, *only ? " (crate test)" : "", ok ? "ok" : "FAILED");
}

extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api*) {}
