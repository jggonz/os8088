#!/usr/bin/env python3
"""FTPKEEP: a STOR to a FIXED disk commits per 256 KB, not per chunk (SPEC.md
77.50, 18.4.9.3).

    make && make ftpkeeptest && python3 tests/ftpkeep.py

**QEMU, FOR tests/ftpd.py's REASON**: MartyPC has no NIC. Nothing here is a
time. What is counted is WORK, which QEMU counts exactly: the hard disk's
write OPERATIONS (`info blockstats`), one per `int 13h` write the guest made.

THE MACHINE BOOTS ITS HARD DISK (`make test TESTHD=`), because WSEQF_KEEP is
a fixed disk's and the boot partition is the one fixed volume QEMU gives a
guest with no driver asked for. FTPD serves the folder it was launched from,
so both arms sit in C:'s root.

FOUR LEGS, each on a freshly built image:

1. KEPT (FTPD.O88, what ships). A 1.5 MB STOR, byte-exact back over RETR AND
   off the image by an independent FAT reader on the host, and the volume
   fsck-clean (`os88disk.py --verify-hdd`).
2. PLAIN (FTPDP8.O88, FTPPLAIN=1 - every chunk committed, what shipped
   before). The same file, the same checks. The A/B is the write count:
   every committed chunk is at least a FAT write and a directory write that
   the kept stream does not make, so PLAIN must make at least 1.5 more
   writes per 8 KB chunk than KEPT. **This is the leg that goes red when the
   feature is broken**: take `WSEQF_KEEP` out of FD_WSEQF, or the
   `[dws_keep]` test out of gfx_unlock, and KEPT commits per chunk too and
   the two counts meet.
3. A POWER CUT, KEPT. A 2 MB STOR, QEMU killed once ~1.2 MB has reached the
   disk. The file must be EXACTLY what the last checkpoint committed - its
   size one stage (the create: 32 KB here, SPEC.md 77.50.2) plus a whole
   number of 256 KB checkpoints, its
   bytes the blob's prefix - and the volume must still verify: the held
   chain past it is unreachable, so it is free space or lost clusters and
   never a wrong file.
4. A WHOLE NUMBER OF STAGES, cut at the 226: 1 MB, and the machine killed
   the moment the server says it is done. All of it must be on the disk -
   the server once finished such a file without closing the stream.
"""
import ftplib
import io
import os
import random
import re
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import dispcp                                          # noqa: E402
import ethernet as eth                                 # noqa: E402
import ftpd as fd                                      # noqa: E402
import os88qemu                                        # noqa: E402
import os88sym                                         # noqa: E402

S = os88sym.linear
SOCK = "build/qmp.sock"
HDIMG = "build/ftpkeep.img"
AIMG, BIMG = "build/ftpkeepa.img", "build/ftpkeepb.img"
STG = 8192
CKPT = 262144
say = fd.say


def blob(n, seed):
    r = random.Random(seed)
    return bytes(r.getrandbits(8) for _ in range(n))


# --- an independent FAT reader for the partition, so the guest is not asked
# whether the guest wrote it (tests/ftpd.py's assertion 4, on a hard disk) ---
def fat_read(img, name83):
    d = open(img, "rb").read()
    lba0 = struct.unpack_from("<I", d, 446 + 8)[0]
    b = d[lba0 * 512:]
    bps, spc, rsv, nfat, nroot = struct.unpack_from("<HBHBH", b, 11)
    tot16, _, fatsz = struct.unpack_from("<HBH", b, 19)
    tot = tot16 or struct.unpack_from("<I", b, 32)[0]
    rootoff = (rsv + nfat * fatsz) * bps
    dataoff = rootoff + nroot * 32
    nclus = (tot - (rsv + nfat * fatsz) - nroot * 32 // bps) // spc
    fat16 = nclus >= 4085
    fat = b[rsv * bps:(rsv + fatsz) * bps]

    def nxt(c):
        if fat16:
            return struct.unpack_from("<H", fat, c * 2)[0]
        v = struct.unpack_from("<H", fat, c * 3 // 2)[0]
        return (v >> 4) if c & 1 else (v & 0xFFF)
    eoc = 0xFFF8 if fat16 else 0xFF8
    for i in range(nroot):
        e = b[rootoff + i * 32:rootoff + i * 32 + 32]
        if e[0] == 0:
            break
        if e[0] == 0xE5 or e[11] & 0x08:
            continue
        if e[:11] != name83:
            continue
        clus = struct.unpack_from("<H", e, 26)[0]
        size = struct.unpack_from("<I", e, 28)[0]
        out = bytearray()
        while clus >= 2 and clus < eoc and len(out) < size:
            o = dataoff + (clus - 2) * spc * bps
            out += b[o:o + spc * bps]
            clus = nxt(clus)
        return bytes(out[:size]), size
    return None, None


def wr_ops(m):
    t = m.hmp("info blockstats")
    for line in t.splitlines():
        if line.startswith("ide0-hd0"):
            g = re.search(r"wr_operations=(\d+)", line)
            w = re.search(r"wr_bytes=(\d+)", line)
            return int(g.group(1)), int(w.group(1))
    raise RuntimeError("no ide0-hd0 in info blockstats:\n" + t)


def boot():
    for f in (HDIMG, AIMG, BIMG):
        if os.path.exists(f):
            os.remove(f)
    r = subprocess.run(["make", "ftpkeeptest"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("ftpkeep: make ftpkeeptest failed:\n" + r.stdout + r.stderr)
    os88qemu.kill()
    os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1", "ETHFWD=1",
                        "TESTHD=" + HDIMG, "TESTIMG=" + AIMG,
                        "TESTAPPS=" + BIMG], capture_output=True, text=True)
    if r.returncode:
        sys.exit("ftpkeep: make test failed:\n" + r.stdout + r.stderr)
    m = eth.Qemu(SOCK)
    mo = eth.Mouse()
    ip = fd.wait_dhcp(m)
    if not ip:
        raise RuntimeError("the card never bound an address")
    return m, mo


def launch(m, mo, pkg):
    def settle(mm, card=None):
        time.sleep(2.0)
    dispcp.open_drive(m, mo, S, settle, "C")
    wins = dispcp.win_list(m, S)
    wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
    for _ in range(4):
        dispcp.open_named(m, mo, S, settle, wx, wy, pkg)
        fx, fy = fd.wait_win(m, 12.0)
        if fx is not None:
            break
    else:
        raise RuntimeError("%s never opened a window" % pkg)
    mo.click(*fd.start_btn(fx, fy))
    time.sleep(1.5)
    for _ in range(30):
        try:
            f = fd.connect()
            break
        except (OSError, ftplib.all_errors):
            time.sleep(1.0)
    else:
        raise RuntimeError("nothing answered on port 21")
    f.login("os8088", "os8088")
    return f


def leg(pkg, data, fails):
    m, mo = boot()
    try:
        f = launch(m, mo, pkg)
        w0 = wr_ops(m)
        f.storbinary("STOR UP.DAT", io.BytesIO(data))
        time.sleep(1.5)                     # the 226 is sent after the close
        w1 = wr_ops(m)
        back = fd.retr(f, "UP.DAT")
        if back != data:
            fails.append("%s: RETR gave %d bytes, not the %d stored exactly"
                         % (pkg, len(back), len(data)))
        try:
            f.quit()
        except Exception:
            pass
        time.sleep(1.0)
    finally:
        m.quit()
        time.sleep(1.0)
    got, size = fat_read(HDIMG, b"UP      DAT")
    if got != data:
        fails.append("%s: the HOST reads UP.DAT as %r bytes, not the %d stored"
                     % (pkg, size, len(data)))
    r = subprocess.run(["python3", "tools/os88disk.py", "--verify-hdd", HDIMG],
                       capture_output=True, text=True)
    if r.returncode:
        fails.append("%s: the volume does not verify:\n%s" % (pkg, r.stdout + r.stderr))
    ops = w1[0] - w0[0]
    say("%-11s %d bytes: %d disk write ops, %d bytes written; host read %s, "
        "fsck %s" % (pkg, len(data), ops, w1[1] - w0[1],
                     "exact" if got == data else "WRONG",
                     "clean" if not r.returncode else "FAILED"))
    return ops


def cut(data, fails):
    m, mo = boot()
    try:
        f = launch(m, mo, "FTPD.O88")
        w0 = wr_ops(m)[1]
        th = threading.Thread(target=lambda: _stor_quietly(f, data), daemon=True)
        th.start()
        end = time.time() + 120
        while time.time() < end:
            if wr_ops(m)[1] - w0 >= 1200 * 1024:
                break
            time.sleep(0.05)
        else:
            fails.append("cut: 1.2 MB never reached the disk")
    finally:
        m.quit()                            # THE POWER CUT
        time.sleep(1.0)
    got, size = fat_read(HDIMG, b"UP      DAT")
    if got is None:
        fails.append("cut: UP.DAT is not on the disk at all")
        return
    # THE FIRST COMMIT IS ONE STAGE: 8 KB on an 8088, 32 KB on a 286 or
    # better (SPEC.md 77.50.2) - and QEMU's CPU is a 386, so it is 32 here
    first = [s0 for s0 in (32768, STG)
             if size > s0 and (size - s0) % CKPT == 0]
    ok_size = bool(first)
    say("cut: UP.DAT is %d bytes after the cut (%s + %s checkpoints); "
        "prefix %s" % (size, "%d KB" % (first[0] // 1024) if first else "?",
                       (size - first[0]) // CKPT if first else "?",
                       "exact" if got == data[:size] else "WRONG"))
    if not ok_size:
        fails.append("cut: UP.DAT is %d bytes - not the create plus a whole "
                     "number of 256 KB checkpoints" % size)
    if got != data[:size]:
        fails.append("cut: UP.DAT's bytes are not the blob's prefix")
    r = subprocess.run(["python3", "tools/os88disk.py", "--verify-hdd", HDIMG],
                       capture_output=True, text=True)
    say("cut: " + (r.stdout.strip().splitlines() or ["?"])[-1])
    if r.returncode:
        fails.append("cut: the volume does not verify after the cut:\n"
                     + r.stdout + r.stderr)


def exact(data, fails):
    """A file that is a WHOLE NUMBER OF STAGES, and the power cut at the 226.

    Such a file ends with nothing staged, and the server used to finish there
    without the close - so the 226 went out with up to 256 KB still unlinked,
    and only the kernel's next commit point saved it (SPEC.md 77.50.1, found
    in the field's own A/B: `ck 2` where three were owed). Killing the
    machine the moment the 226 arrives leaves the kernel no such point.
    """
    m, mo = boot()
    try:
        f = launch(m, mo, "FTPD.O88")
        f.storbinary("STOR UP.DAT", io.BytesIO(data))
    finally:
        m.quit()                            # at the 226: nothing else ran
        time.sleep(1.0)
    got, size = fat_read(HDIMG, b"UP      DAT")
    say("exact: %d bytes stored, %s on the disk at the 226"
        % (len(data), size))
    if got != data:
        fails.append("exact: a %d-byte file reads back as %s bytes after a "
                     "cut at the 226 - the stream was not closed before the "
                     "server said it was done" % (len(data), size))


def _stor_quietly(f, data):
    try:
        f.storbinary("STOR UP.DAT", io.BytesIO(data))
    except Exception:
        pass


def main():
    fails = []
    data = blob(1536 * 1024 + 1000, 77)     # not a chunk multiple: the tail
    kept = leg("FTPD.O88", data, fails)
    plain = leg("FTPDP8.O88", data, fails)
    chunks = len(data) // STG
    say("A/B: PLAIN %d writes, KEPT %d - %.2f more a chunk over %d chunks"
        % (plain, kept, (plain - kept) / chunks, chunks))
    if plain - kept < chunks * 1.5:
        fails.append("the kept stream saved %d writes over %d chunks - it is "
                     "still committing per chunk" % (plain - kept, chunks))
    cut(blob(2048 * 1024, 78), fails)
    exact(blob(1024 * 1024, 79), fails)
    say("")
    if fails:
        for f in fails:
            say("FAIL " + f)
        sys.exit(1)
    say("ftpkeep: all assertions passed")


if __name__ == "__main__":
    main()
