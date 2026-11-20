# WormsLoader (loader internals)

Output: `build\out\dinput8.dll`. Sources in `src/`, public API in `include/wormsloader.h` (C ABI, `WL_API_VERSION`: bump it when breaking the struct, and only ever append fields).

## Files
- `src/dllmain.cpp` — the proxy export, build check, log, and mod loading (`loader_late_init` runs on the first `SteamAPI_Init`/`DirectInput8Create` call, outside the loader lock).
- `src/api.cpp` — `WL_Api` implementation, filter/callback lists.
- `src/config.cpp` — ini persistence: `Mods\<Name>\settings.ini` (`[settings]`, `[hotkeys]` = VK codes) and `<game>\WormsLoader.ini` (`[loader] relay=host:port`).
- `src/net.cpp` — Steam hooks, relay transport, Steam callback proxying, mod-set handshake.
- `src/ui.cpp` — ImGui overlay (hooks the **system** `opengl32!wglSwapBuffers`, so it draws after ReShade), WndProc subclass (hotkeys, input capture), DirectInput device hooks (input is blanked while the overlay has focus).
- `server/relay.py` — UDP relay (`python relay.py --port 27999`); `server/test_relay.py` is its test.
- `test/selftest.cpp` — hash and ini round-trip asserts (built as `wl_selftest.exe`).
- `third_party/` — MinHook 1.3.3, Dear ImGui 1.91.5 (only the OpenGL2 + Win32 backends), vendored.

## Game facts used (build 01.83.31.37)
| What | Where | Notes |
|---|---|---|
| Screen stack | depth `0x194f560`, entries `0x1956b50 + 8*i` (i=1..depth), screen object at entry[0], hash at obj+8 | `ScreenMain` = `0x7d25f4c2` (verified live). Every change is logged as `screen -> xxxxxxxx` |
| Name hash | `0x9c0410` | djb2 with xor, start 5381, signed chars |
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
- Verified live (single PC): build check, mod loading, Steam hooks installed, overlay drawing with ReShade present, Mods button only on the main menu, Mods screen + WormsLoader page.
- Not yet verified: a mod's settings/hotkey page in game, the hotkey rebind, an online match through the hooks, the handshake, the relay transport (only unit-tested).
- Cosmetic: the game's own cursor freezes while the Mods screen is open (input blocked), and ImGui draws its own.
