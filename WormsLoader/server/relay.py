"""WormsLoader relay: forwards game packets between players of a lobby over UDP.

Run on the hosting machine (or any server):  python relay.py --port 27999
Forward UDP <port> on your router, then enter <your-public-ip>:<port> as "Relay" in the
in-game Mods menu (WormsLoader page) before creating a lobby.

Frame (little endian): b"WLR1" | lobby u64 | from u64 | to u64 | payload.
to == 0 is a hello/keep-alive that registers the sender's address.
"""
import argparse
import asyncio
import struct
import time

MAGIC = b"WLR1"
HDR = struct.Struct("<4sQQQ")
MAX_FRAME = HDR.size + 1500


class Relay(asyncio.DatagramProtocol):
    def __init__(self, keep_seconds):
        self.keep = keep_seconds
        self.members = {}  # (lobby, steam_id) -> [addr, last_seen]

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        if len(data) < HDR.size or len(data) > MAX_FRAME or data[:4] != MAGIC:
            return
        _, lobby, sender, to = HDR.unpack_from(data)
        now = time.monotonic()
        # shortcut: the sender's Steam ID is trusted as claimed; add a Steam auth ticket check before running a public relay.
        # A changed addr for the same Steam ID is the reconnect case: the member keeps its slot.
        self.members[(lobby, sender)] = [addr, now]
        if to:
            target = self.members.get((lobby, to))
            if target and now - target[1] < self.keep:
                self.transport.sendto(data, target[0])

    def expire(self):
        now = time.monotonic()
        for key in [k for k, (_, seen) in self.members.items() if now - seen > self.keep]:
            del self.members[key]


async def main(port, keep):
    loop = asyncio.get_running_loop()
    transport, relay = await loop.create_datagram_endpoint(lambda: Relay(keep), local_addr=("0.0.0.0", port))
    print(f"WormsLoader relay on udp/{port}, slots kept {keep}s after a player goes silent")
    try:
        while True:
            await asyncio.sleep(10)
            relay.expire()
    finally:
        transport.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=27999)
    ap.add_argument("--keep", type=int, default=600, help="seconds a silent player keeps their slot")
    a = ap.parse_args()
    asyncio.run(main(a.port, a.keep))
