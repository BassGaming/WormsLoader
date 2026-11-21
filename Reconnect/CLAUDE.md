# Reconnect mod

Goal: an online match survives a short connection loss instead of returning to the frontend
("Online disconnection occurred, returning to frontend", string only, no xref).

## What v0.2.0 does
- Settings: `enabled`, `max_outage_seconds` (10–600, default 120), `diagnose` (default on).
- Hides Steam callbacks 102 (SteamServerConnectFailure), 103 (SteamServersDisconnected) and 1203 (P2PSessionConnectFail) from the game while a match packet arrived within `max_outage_seconds`.
- Hides callback 506 (LobbyChatUpdate) with state Disconnected (0x4) for a player who sent a match packet within `max_outage_seconds`, and logs every 506 with its state. Test 2026-11-20: the friend lost internet ~10 s; P2P recovered, but when their Steam client logged back on, the host got a 506 and destroyed the peer link in the same millisecond (game reacts to lobby leave, stack 0x4DFA7C). Peer-side game timeout seen: 25 s without packets.
- The game sends unreliable packets with its own reliability on top, so lost packets are resent by the game once Steam P2P comes back. The mod adds no resend layer.
- `diagnose`: hooks the peer-link destructor `0x523750` and logs the call stack plus the seconds since the last packet from that peer. That pinpoints the game's own drop/timeout code.

## Next step (needs one real test)
1. Two PCs, both with this mod, start an online match. Pull one network cable for ~30 s.
2. Read `WormsLoader.log` on both PCs: the `peer link ... destroyed ... stack:` line gives the return addresses of the drop path, and the "last packet Xs ago" gives the game's timeout value.
3. Disassemble those addresses and find the timeout compare. Patch or hook it to use `max_outage_seconds`. Bump the version to 0.2.0.

No game-side timeout patch exists yet, so v0.1.0 alone may still drop at the game's own timeout.

## Later: rejoin after a crash/restart
Research only. The relay could log the input stream, and the deterministic lockstep replay could rebuild the state.
