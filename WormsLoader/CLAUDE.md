# WormsLoader (loader internals)

Output: `build\out\dinput8.dll`. Sources in `src/`, public API in `include/wormsloader.h` (C ABI, `WL_API_VERSION`: bump it when breaking the struct, and only ever append fields).

## Files
- `src/loader.h` — `kGames`: one entry per supported game build (exe name, PE timestamp/size, the addresses the loader reads). `g_game` is the detected entry. Adding a game = a new entry + making `ui.cpp`'s address users fit that game.
- `src/dllmain.cpp` — the proxy export, game/build detection, log, and mod loading (skips mods whose `WL_ModInfo.game` is neither `*` nor the detected game) (`loader_late_init` runs on the first `SteamAPI_Init`/`DirectInput8Create` call, outside the loader lock).
- `src/api.cpp` — `WL_Api` implementation, filter/callback lists.
- `src/config.cpp` — ini persistence: `Mods\<Name>\settings.ini` (`[settings]`, `[hotkeys]` = VK codes) and `<game>\WormsLoader.ini` (`[loader] relay=host:port`).
- `src/net.cpp` — Steam hooks, relay transport, Steam callback proxying, mod-set handshake.
- `src/ui.cpp` — ImGui overlay (hooks the **system** `opengl32!wglSwapBuffers`, so it draws after ReShade), WndProc subclass (hotkeys, input capture), DirectInput device hooks (input is blanked while the overlay has focus), `on_screen`/`press_key` for the API (`press_key` posts `WM_KEYDOWN` and the `WM_KEYUP` 0.1 s later; GUI screens read window messages, not DirectInput keys).
- `server/relay.py` — UDP relay (`python relay.py --port 27999`); `server/test_relay.py` is its test.
- `test/selftest.cpp` — hash and ini round-trip asserts (built as `wl_selftest.exe`).
- `third_party/` — MinHook 1.3.3, Dear ImGui 1.91.5 (only the OpenGL2 + Win32 backends), vendored.

## Game facts used (build 01.83.31.37)
| What | Where | Notes |
|---|---|---|
| Screen stack | depth `0x194f560`, entries `0x1956b50 + 8*i` (i=1..depth), screen object at entry[0], hash at obj+8 | `ScreenMain` = `0x7d25f4c2` (verified live). Every change is logged as `screen -> xxxxxxxx` |
| Name hash | `0x9c0410` | djb2 with xor, start 5381, signed chars |
| Game cursor | per-player slots, float x, y; seen in use at `0x195aab0` and `0x195b0a8` (stride `0x5f8`); GUI bounds at `0x195b0c0` (min x/y, max x/y) | GUI space 1024x576 (16:9), centre 0,0, **Y up**. Moved only by DirectInput deltas, so the Windows cursor drifts away from it. Which slot the mouse drives changes between launches, so the overlay follows the slot that moved last (until the first mouse move it falls back to the Windows cursor) |
| Title screen | `ScreenTitle` = `0x2381fde9`; takes keys from window messages | used by SkipIntro |
| Weapon table | `0xd37738`, 71 records (`kEquip_Max` = 0x47, id 0 = Nothing) of 20 B: name, gadget type, payload type, panel prompt string, flags (1 = weapon, 2 = utility, 0 = unused). Ids 16, 17, 44, 46 are `PADDING1-4` | code checks `id < 0x47` everywhere; per-weapon team inventory records are 0x24 B (loop at `0xa55bdb`). Not hooked yet |
| Weapon selection | Panel click `0x89e740` (HudEquipPanel, `(panel, id)` stdcall-ish, `ret 8`) writes the request into the worm: byte `+0x188 = 1`, `+0x18c = id` (an older path uses `+0x1c8`, 0x47 = none). Worm tick `0x7e1700` (cdecl `(worm, key)`, game thread, every logic tick) consumes it, records `(key, id)` int pairs via `0x9763f0`/`0x9759d0` (replay/input recorder; playback reads them back with `0x9763c0`), then calls Worm::SetEquip `0x7da080` (**stdcall** `(worm, id)`, `ret 8`), which stores `+0x1c4 = id` and calls GadgetMan::SetEquip `0x93aac0` (esi = GadgetMan, stack `id, ...`): it picks the launcher by `table[id].gadgetType` and activates it with the per-weapon data object `*(0x19b7290 + 4*id)` | verified live (Trace probe) |
| Worm | class `Worm`, vtable `0xdf5e14`, objects ~0x3e8 B apart. `+0x1c4` current weapon. Slot 37 (`+0x94`) = TakeDamage `0x7de3a0` (thiscall `(int damage)`); the "Damage-Health" event handler `0x7e076f` calls it, adds HUD damage numbers (`0x899420`) and adds to float `+0x214` | slot 37 not called by us yet |
| Explosion | Payload impact/detonate: `0x8dbcc0` (thiscall, ecx = payload; ProjectilePayload vtable slot 45; the grenade calls it from its fuse at `0x8d357e`) -> `0x93c440` -> `0x8c27d0(?, params*, ...)` (params: position, radius; carves land via `0x7968a0(x, y, radius)`) -> `0x953ab0` loops over objects -> `0x7e0050` (ecx = worm, eax = push vector) pushes each worm in range. Damage is applied through the Damage-Health event | verified live (call log) |
| Fuse bombs | TimeBombPayload (Grenade, EMP, Cluster, Banana share it): launch `0x8d3630` (slot 40; fuse end `+0x10c` = params `+0x14` (now) + params `+0x50` (fuse, -1 = none)); fuse check `0x8d33d0`/slot 41 each tick; fuse end -> slot 42 `0x8d35a0` -> `0x8dbcc0` | game logic tick is ~30 Hz |
| Weapon parameters | hard-coded in C++ `*Payload` / `*Gadget` classes (no per-weapon damage data in the bundles). Crate odds: `g_Weights` in CrateWeapon.cpp; panel: `HudEquipPanel` (`m_GadgetToggleArray[y][x]`), button data `g_ButtonDataModern/Classic` (HudEquipMapping.cpp) | not located yet |
| Go-to-screen | `0xa18950` cdecl(hash, float fade, int player) | not hooked yet |
| Steam imports | `SteamNetworking`, `SteamMatchmaking`, `SteamUser`, ... from steam_api.dll | interface versions: Networking005, MatchMaking009, User017 |
| P2P use | only slot 0 Send (send types 0/1, channel 0), slot 2 Read (1200 B buffer), slot 3 Accept | the game's own reliability/timeouts run on top; it never asks Steam for session state |
| Peer link | vtable `0xdffe58` (size 0xf8), Steam ID at +0xf0, slot0 dtor `0x523750`, slot6 send `0x5236e0` | created in the receive loop around `0x503970` |
| Lobby | game keeps its data under key `_Blob`, uses MM slots 13/14/15/19/20/23/35 | our keys: `wl_modset`, `wl_mods`, `wl_relay` |
| CSteamID returns | hidden return pointer (thiscall) | e.g. `GetSteamID(this, &out)` |

## Mod-set handshake
`LobbyEnter_t` (504) → if we own the lobby, set `wl_modset` (hex hash of `01.83.31.37|name@ver;...` over the non-client-only mods), `wl_mods` and `wl_relay`. Otherwise compare. On a mismatch (or a vanilla host while we have net mods), all P2P traffic for the lobby is dropped and an overlay message explains why. The game then fails to connect by itself; we don't force-leave the lobby.

## Relay
Frames are `WLR1 | lobby | from | to | payload` over UDP; `to=0` is a 1-second hello. The host's `relay=` setting goes into the lobby, and joiners follow it. The relay keeps a player's slot for `--keep` seconds of silence, and an address change for the same Steam ID counts as a reconnect.
Known gap: the relay trusts the claimed Steam ID (no auth ticket). Fine for friends, not for a public relay.

## Verified / not verified
- Verified live (single PC): build check, mod loading, Steam hooks installed, overlay drawing with ReShade present, Mods button only on the main menu and clickable by hand (user-confirmed), Mods screen + WormsLoader page.
- Not yet verified: a mod's settings/hotkey page in game, the hotkey rebind, an online match through the hooks, the handshake, the relay transport (only unit-tested).
- Cosmetic: ImGui draws its own cursor over the Mods window (the game's cursor is drawn underneath it).
