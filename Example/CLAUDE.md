# Example mod

A template. It registers one setting of each type (bool/int/float/enum) and a hotkey (F6) that logs the values to `WormsLoader.log`. It is `client_only`, so it never affects the online mod-set check. To start a new mod, copy this folder (see the root CLAUDE.md, "Adding a mod").

`example.cpp` is written as the mod tutorial: its comments explain the API, game addresses, online rules, and (in a non-compiled block) hooks, patches, `press_key` and net filters. The build copies it with `wormsloader.h` to `Mods\Example\source\` (post-build step in the root `CMakeLists.txt`), so players and LLMs can learn the API without the repo. Keep it compiling and up to date when the API changes.
