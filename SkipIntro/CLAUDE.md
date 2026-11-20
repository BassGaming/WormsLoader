# SkipIntro mod

Client-only. Both settings take effect at the next launch (the patch happens in `WL_ModInit`).
- `legal`: GameFlow `Init_LegalScreen` (`0x9d2b10`) shows `ScreenLegal` until now + 5000 ms (`add eax, 0x1388` at `0x9d2bb3`). Patched to 0 ms, so it ends on the next update. Exec state `0x9d2a90`, done state `0x9d2960`.
- `movies`: calls `0x9c8680`, the handler the option parser runs for `/NOMOVIES`; it sets the game property `ShowMovies = 0`. Needs the property store at `*0x1959154`.
- Measured: window to `ScreenTitle` went from 7.6 s to 2.7 s. The title screen ("press a key", `ScreenTitle`) is left alone, because it may assign the input device to player 1.
