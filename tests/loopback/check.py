#!/usr/bin/env python3
"""Loopback test: listens on 127.0.0.1:6454, runs the LoopbackSketch executable, then
checks the ArtDmx packets it sent. Same checks as the Processing library's LoopbackCheck.

Build the sketch first, then run:
    python3 tests/loopback/check.py tests/loopback/LoopbackSketch/build/LoopbackSketch
"""

import socket
import subprocess
import sys
import time

EXPECTED = {
    0: [0, 0, 0, 255, 255, 150, 50, 0, 50, 77],
    1: [255, 255, 255, 1, 2, 3, 200, 100, 50],
    2: [255, 255, 255, 200, 100, 50, 0, 0, 255, 0, 255, 0],
}

failures = 0


def check(name, ok, detail=""):
    global failures
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok or not detail else "  -- " + detail))
    if not ok:
        failures += 1


def main():
    exe = sys.argv[1]
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    sock.bind(("127.0.0.1", 6454))
    sock.settimeout(0.2)

    log = open(exe + ".log", "w")
    p = subprocess.Popen([exe], stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    pkts = []  # (time, universe, seq, data)
    start = time.time()
    last = 0.0
    while True:
        now = time.time()
        if now - start > 90:
            print("TIMEOUT waiting for sketch")
            p.kill()
            break
        alive = p.poll() is None
        if not alive and last and now - last > 1.5:
            break
        if not alive and not last and now - start > 30:
            break
        try:
            b, _ = sock.recvfrom(1024)
        except socket.timeout:
            continue
        if len(b) < 18 or b[8] != 0x00 or b[9] != 0x50:
            continue
        u = b[14] | (b[15] & 0x7F) << 8
        n = b[16] << 8 | b[17]
        last = time.time()
        pkts.append((last, u, b[12], list(b[18:18 + n])))
    print("sketch exit code: %s, packets: %d" % (p.poll(), len(pkts)))

    check("received packets", len(pkts) > 0)
    by_u = {}
    for k in pkts:
        by_u.setdefault(k[1], []).append(k)
    check("universes 0,1,2,3 only", set(by_u) == {0, 1, 2, 3}, "got %s" % sorted(by_u))

    for u in sorted(by_u):
        lst = by_u[u]
        check("u%d length 512" % u, all(len(k[3]) == 512 for k in lst))
        lit = [k for k in lst if any(k[3])]
        steady = lit[-1] if lit else None
        check("u%d has a lit frame" % u, steady is not None)
        if steady is not None and u in EXPECTED:
            exp = EXPECTED[u]
            got = steady[3][:len(exp)]
            check("u%d values" % u, got == exp, "expected %s got %s" % (exp, got))
            check("u%d unused channels are 0" % u, not any(steady[3][len(exp):]))
        check("u%d blackout on exit" % u, not any(lst[-1][3]))
        seq_ok = True
        for i, k in enumerate(lst):
            s = k[2]
            if s < 1 or s > 255:
                seq_ok = False
            if i > 0 and s != lst[i - 1][2] % 255 + 1:
                if seq_ok:
                    print("  u%d seq break at packet %d/%d: %d -> %d" % (u, i, len(lst), lst[i - 1][2], s))
                seq_ok = False
        check("u%d sequence 1..255, +1 each packet" % u, seq_ok)
        if len(lit) > 10:
            secs = lit[-1][0] - lit[0][0]
            fps = (len(lit) - 1) / secs
            print("  u%d: %d lit packets over %.2f s = %.1f fps" % (u, len(lit), secs, fps))
            check("u%d rate <= 45 fps" % u, fps <= 45.5, "%.1f fps" % fps)
            check("u%d rate >= 40 fps (sketch runs 60, cap 44)" % u, fps >= 40, "%.1f fps" % fps)

    print("LOOPBACK PASS" if failures == 0 else "LOOPBACK FAIL (%d)" % failures)
    sys.exit(0 if failures == 0 else 1)


if __name__ == "__main__":
    main()
