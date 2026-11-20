# worms_ca — Worms Clan Wars modding

Everything for Worms Clan Wars (Steam, build 01.83.31.37, 32-bit, `H:\Games\SteamLibrary\steamapps\common\Worms Clan Wars`).
Private repo: GitHub `WormsLoader`.

## Folder → what it is (read only the CLAUDE.md of the folder you work on)
| Folder | What | Details |
|---|---|---|
| `WormsLoader/` | The mod loader (`dinput8.dll` proxy), mod API, overlay Mods menu, network layer, relay server | `WormsLoader/CLAUDE.md` |
| `Reconnect/` | Mod: survive short connection losses in online matches | `Reconnect/CLAUDE.md` |
| `Example/` | Mod template: every setting type + one hotkey | `Example/CLAUDE.md` |

## How the loader works (summary)
- The game imports `DINPUT8.dll`; our `dinput8.dll` in the game folder is loaded instead and forwards `DirectInput8Create` to the system DLL. (`opengl32.dll` in the game folder is ReShade — don't touch it.)
- It checks the exe build (PE timestamp + image size). Unknown build → no mods load, game runs vanilla.
- It loads `<game>\Mods\<Name>\<Name>.dll` for every folder, sorted by name. Each mod exports `WL_GetModInfo` and `WL_ModInit(const WL_Api*)`. The whole API is `WormsLoader/include/wormsloader.h`.
- A **Mods** button (ImGui overlay) shows on the main menu. The Mods screen lists the mods; a mod's page shows its description, settings and rebindable hotkeys. Settings are saved to `Mods\<Name>\settings.ini`.
- Online: the lobby host publishes its mod-set hash in Steam lobby data. Joiners with a different set are refused, because the game is deterministic lockstep and different mods would desync. Mods flagged `client_only` don't count.
- Log: `<game>\WormsLoader.log`.

## Adding a mod
1. Copy `Example/` to `<Name>/`, set `name` in `WL_ModInfo` to `<Name>`, and decide `client_only`.
2. Add `wl_mod(<Name>)` to the root `CMakeLists.txt`.
3. Write `<Name>/CLAUDE.md` (what it hooks, exe addresses, how to test) and add a row to the table above.
4. Bump `version` whenever behaviour that other players see changes; the version is part of the mod-set hash.

## Build / install / test
```
build.bat                 # VS 2019 BuildTools, Win32 Release -> build\out ; runs wl_selftest + relay test
powershell -File install.ps1             # copy dinput8.dll + Mods\* into the game folder
powershell -File install.ps1 -Uninstall  # remove dinput8.dll -> vanilla game
```
Launch: `steam://rungameid/233840`. The game must be closed while installing.

## Reverse engineering
- No ASLR: image base 0x400000, so exe VAs are absolute (`api->exe(va)`).
- No Ghidra installed. Ad-hoc RE has been done with Python `pefile` + `capstone`; a venv with these gets recreated as needed.
- Screen names hash with the game's djb2-xor (`djb2x` in `WormsLoader/src/loader.h`).
