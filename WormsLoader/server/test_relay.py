"""Self-check for relay.py: routing, lobby isolation, junk rejection, and a player reconnecting
from a new address (new UDP port) keeps receiving.  Run: python test_relay.py"""
import asyncio
import socket

from relay import HDR, MAGIC, Relay

LOBBY, A, B = 0x1100, 0xA, 0xB


def frame(lobby, sender, to, payload=b""):
    return HDR.pack(MAGIC, lobby, sender, to) + payload


def client():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    s.settimeout(0.5)
    return s


def recv(s):
    try:
        return s.recv(4096)
    except socket.timeout:
        return None


async def main():
    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(lambda: Relay(600), local_addr=("127.0.0.1", 0))
    relay = transport.get_extra_info("sockname")

    async def send(s, data):
        s.sendto(data, relay)
        await asyncio.sleep(0.05)  # let the relay process it

    a, b = client(), client()
    await send(a, frame(LOBBY, A, 0))
    await send(b, frame(LOBBY, B, 0))

    await send(a, frame(LOBBY, A, B, b"turn 1"))
    assert await loop.run_in_executor(None, recv, b) == frame(LOBBY, A, B, b"turn 1")

    await send(a, frame(0x2200, A, B, b"other lobby"))  # B is not in lobby 0x2200
    await send(a, b"junk")
    assert await loop.run_in_executor(None, recv, b) is None

    b.close()  # B's connection drops; B comes back on a new port
    b2 = client()
    await send(b2, frame(LOBBY, B, 0))
    await send(a, frame(LOBBY, A, B, b"turn 2"))
    assert await loop.run_in_executor(None, recv, b2) == frame(LOBBY, A, B, b"turn 2")

    transport.close()
    print("relay test ok")


asyncio.run(main())
