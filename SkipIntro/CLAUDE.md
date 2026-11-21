# SkipIntro mod

Client-only. `legal` and `movies` take effect at the next launch (the patch happens in `WL_ModInit`); `title` is live.
- `legal`: GameFlow `Init_LegalScreen` (`0x9d2b10`) shows `ScreenLegal` until now + 5000 ms (`add eax, 0x1388` at `0x9d2bb3`). Patched to 0 ms, so it ends on the next update. Exec state `0x9d2a90`, done state `0x9d2960`.
- `movies`: calls `0x9c8680`, the handler the option parser runs for `/NOMOVIES`; it sets the game property `ShowMovies = 0`. Needs the property store at `*0x1959154`.
- `title`: while `ScreenTitle` ("press any key") is on top, presses Enter every 0.5 s via `api->press_key` (a posted `WM_KEYDOWN`/`WM_KEYUP`). The title ignores input while it fades in (~2.5 s); the first press after that goes to the main menu. The title takes keys from window messages: an injected DirectInput key does nothing. RE notes: GameFlow `0x9d1ed8` shows it (calls `0xa18c80`, hash global `0x1956c70`), then states `0x9d1920` -> `0x9d18f0` -> `0x9d18b0`; the key handling goes through a Platform.cpp sign-in path around `0x989154`, which is why it isn't patched.
- Measured: window to `ScreenTitle` went from 7.6 s to 2.7 s; `ScreenTitle` to `ScreenMain` takes about 3 s with `title` on, untouched.
