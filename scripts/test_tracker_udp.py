"""Exercise the real UDP client against a loopback tracker (no public traffic)."""
import socket
import struct
import subprocess
import sys

binary = sys.argv[1]
for mode in ("success", "retry", "error", "short", "announce_error"):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(("127.0.0.1", 0))
        server.settimeout(5)
        proc = subprocess.Popen([binary, f"udp://127.0.0.1:{server.getsockname()[1]}/announce", str(int(mode in ("success", "retry")))])
        try:
            request, client = server.recvfrom(2048)
            magic, action, tid = struct.unpack("!QII", request)
            assert magic == 0x41727101980 and action == 0
            if mode == "retry":
                server.settimeout(20)
                repeated, sender = server.recvfrom(2048)
                assert repeated == request and sender == client
            if mode == "error":
                server.sendto(struct.pack("!II", 3, tid) + b"Denied", client)
            elif mode == "short":
                server.sendto(struct.pack("!II", 0, tid), client)
            else:
                # An unrelated transaction and wrong action must be ignored.
                server.sendto(struct.pack("!IIQ", 0, tid ^ 1, 99), client)
                server.sendto(struct.pack("!IIQ", 2, tid, 99), client)
                server.sendto(struct.pack("!IIQ", 0, tid, 1234567), client)
                request, client = server.recvfrom(2048)
                assert len(request) == 98
                connection, action, tid = struct.unpack("!QII", request[:16])
                assert connection == 1234567 and action == 1
                assert request[16:36] == b"\x12" * 20 and request[36:56] == b"\x34" * 20
                assert struct.unpack("!QQQ", request[56:80]) == (0x100000002, 0x200000003, 9)
                assert struct.unpack("!H", request[96:98])[0] == 6881
                if mode == "announce_error":
                    server.sendto(struct.pack("!II", 3, tid) + b"Denied", client)
                else:
                    server.sendto(struct.pack("!IIIII", 1, tid, 120, 2, 4) + socket.inet_aton("127.0.0.1") + struct.pack("!H", 6881), client)
            assert proc.wait(timeout=5) == 0
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
for url in ("udp://host:0/announce", "udp://host:70000/announce", "udp://host:no/announce"):
    subprocess.run([binary, url, "0"], check=True)
print("UDP tracker: handshake, wire fields, peers, lost packet retry, stale replies, rejection and invalid packets/ports passed")
