# WormsLoader

A mod loader for **Worms Clan Wars** (Steam), with a few mods and fixes for online play.

It works only with the current Steam version of the game (build 01.83.31.37). With any other version the game starts normally and no mods load.

## Install

1. Download `WormsLoader-<version>.zip` from the [Releases](https://github.com/BassGaming/WormsLoader/releases) page.
2. Open the game folder: in Steam, right-click **Worms Clan Wars** → **Manage** → **Browse local files**. This is the folder with `WormsClanWars.exe`.
3. Close the game, then extract the zip into that folder. Afterwards `dinput8.dll` and the `Mods` folder sit next to `WormsClanWars.exe`.
4. Start the game. The main menu now has a **Mods** button.

## What's included

| Mod | What it does | Default |
|---|---|---|
| **MatchStartFix** (built in) | Fixes online lobbies where everyone is ready but the match never starts. Only the host needs it. | on |
| **Reconnect** | Keeps an online match running through a short internet drop instead of making the player give up. | on |
| **SkipIntro** | Skips the legal screen, the intro movies and the "press any key" screen. | on |
| **FpsCap** | Frame-rate cap, adjustable in the Mods menu. | on, 144 fps |
| **Borderless** | Borderless fullscreen window: no screen flicker when you alt-tab. Turn it on in the Mods menu and restart the game. | off |

Click **Mods** on the main menu to see each mod's description and change its settings. Settings are saved in `Mods\<Name>\settings.ini`.

To remove a single mod, delete its folder from `Mods`.

## Playing online

- **Everyone in the lobby needs the same mods.** Reconnect changes how the match runs over the network, so a player with a different mod set (or without WormsLoader) can't join a WormsLoader host. They get "Session is no longer available". SkipIntro, FpsCap and Borderless only change your own screen, so they don't matter.
- Easiest: everyone installs the same release zip.
- Ranked and clan-ranked matches still need Team17's servers, which no longer work. Play unranked matches.

## Uninstall

Delete `dinput8.dll` from the game folder. The game then runs unmodded. You can also delete the `Mods` folder and `WormsLoader.log`.

## Troubleshooting

- **No Mods button:** check that `dinput8.dll` is next to `WormsClanWars.exe`, and open `WormsLoader.log` in the game folder. "unknown build" means your game version isn't supported.
- **Another mod also uses `dinput8.dll`:** only one of them can be installed.
- Bug reports: open an issue and attach `WormsLoader.log`.

## For developers

Requires Visual Studio 2019 Build Tools (C++), CMake and Python 3.

```
build.bat                                # Win32 Release -> build\out, runs the self-tests
powershell -File install.ps1 -GameDir "<game folder>"
```

The mod API is a single header, [`WormsLoader/include/wormsloader.h`](WormsLoader/include/wormsloader.h). [`Example/`](Example) is a template mod showing every setting type and a hotkey. Each folder's `CLAUDE.md` holds the technical notes.

Uses [Dear ImGui](https://github.com/ocornut/imgui) and [MinHook](https://github.com/TsudaKageyu/minhook). Worms is a trademark of Team17; this project is not affiliated with Team17.
