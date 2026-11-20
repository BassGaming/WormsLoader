# Borderless mod

Client-only. Setting `enabled`; takes effect at the next launch. Uses `WL_ModEarlyInit` (exe entry point), because
the game reads its display options and switches the display mode before `WL_ModInit` runs.
- `0x6965e0` (display config option handler, virtual, thiscall with 2 stack args, `ret 8`): after each option, bit 0
  of `this+0x30` (fullscreen; `/WIN` clears it, `/FS` sets it) is cleared, so the game always runs windowed.
  `/W` is stored at `this+0x28`, `/H` at `this+0x2c`.
- `ChangeDisplaySettingsW/A` become no-ops, so the desktop display mode stays.
- `CreateWindowExW` for a top-level window with hInstance NULL or the exe: the style becomes `WS_POPUP` with no
  frame and no `WS_EX_TOPMOST`, and the rect covers the primary monitor. The game recreates its window on focus
  loss and on Video Settings changes, so this runs every time.
- `SetWindowPos` on that window: the rect stays the monitor rect, and `HWND_TOPMOST` is ignored.
Set the game resolution to the desktop resolution: the game renders at `/W` x `/H`.
Verified: without `/WIN` in `CommonData\local.cfg`, the window is `0,0,2560,1440`, style `0x96000000`, ex style 0.
