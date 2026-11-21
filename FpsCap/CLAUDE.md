# FpsCap mod

Client-only. Caps the frame rate: settings `enabled` and `fps` (20-1000, default 144; slider + input box).
Waits in `api->on_frame`, which runs in the overlay's `wglSwapBuffers` hook right after the frame is presented, so the game reads input after the wait (lowest lag). Capping exactly at the monitor refresh with VSync off gives uneven frame delivery; with G-Sync/FreeSync cap a few fps below the refresh.
Sleeps on a high-resolution waitable timer, then spins the last 1.5 ms. Missed frames don't accumulate debt.
The game itself runs uncapped (1800+ fps in menus) unless `/VSYNC:1` is in `CommonData\local.cfg`.
Test: the `perf:` line in `WormsLoader.log` (every 60 s) shows fps, 1% low, worst frame and overlay cost.
