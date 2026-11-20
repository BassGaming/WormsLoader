# Trace (developer tool)

Client-only RE helper, not for players. F7 (arm) hooks every function in `targets.txt` with a 2-instruction
call counter (`inc [cnt]; jmp [orig]`, its own MinHook copy, functions already hooked by others are skipped).
Scroll Lock (mark) appends the counts since the last mark to `Mods\Trace\trace.txt` (`=== <window> <time>`, then `va count`).
Diff the windows to find the functions behind one action.

`targets.txt` = all `call rel32` targets + all vtable entries of the exe (24318 VAs after filter.py).
Regenerate with `re/rtti.py` (class name -> vtable, writes vtables.json), `re/targets.py`, then `re/filter.py` (drops functions that branch back into their first 5 bytes: hooking them crashed the game)
(needs a venv with pefile + capstone; `re/xref.py` holds the shared helpers and the exe path).
Verified: arming in the main menu works (game keeps running).
Keys: not F-keys: the game uses F1-F12 as weapon shortcuts (F8 pulled out the UFO), and loader hotkeys still reach the game.

Probe (always on): hooks Worm::SetEquip `0x7da080` and puts hardware write-watchpoints (DR0-3) on that worm's
+0x1c4/+0x1c8/+0x188/+0x18c; a vectored handler logs `[Probe] write ... at eip ... stack:` to WormsLoader.log.
Numpad / dumps the per-weapon data objects `*(0x19b7290 + 4*id)` as floats/hex.
Call log (always on): every function in `calllog.txt` gets a pushad thunk that logs `[Call] va n ret ecx edx eax esi edi ebx args...`
(first 40 calls each). The current list = the 109 functions that only ran while a bazooka/grenade was fired and exploded.
