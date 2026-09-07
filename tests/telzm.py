#!/usr/bin/env python3
"""Zmodem receive on the machine, end to end, with the bytes read off the disk.

    make && make telnettest && python3 tests/telzm.py

**QEMU BY NAME, for tests/ethernet.py's reason** (SPEC.md 70.12): MartyPC has
no network card of any kind, so the emulator this tree develops on cannot host
`ETHER.DRV` at all and there is no way to put a Zmodem stream through this
package's receive path on it. `make telnettest` builds the disks -
`build/telnetsys.img` with a `SYSTEM.CFG` that already asks for the driver, so
the card is up and DHCP has bound before the first paint, and
`build/telnetdata.img` as the scratch B:.

What QEMU costs is what it always costs: the machine is not an 8088 and no
timing here means anything. **Every assertion below is about BYTES.**

FIVE ASSERTIONS, IN TWO SESSIONS OF ONE BOOT.

1. THE 8.3 MANGLE, ON THE MACHINE, AGAINST `os88bbs.MANGLE83_CASES` (SPEC.md
   70.11.4, which is SPEC.md 77.20's rule). The server sends a batch of tiny
   files whose names are the table's rows, and `[tz_name]` is read out of guest
   memory at each Save dialog. A file this machine downloads by Zmodem and the
   same file uploaded to it by FTP must land on the same name, and the two
   implementations of that rule share no code - so this is what stops them
   drifting.

   **FOUR OF THE SIXTEEN ROWS CANNOT BE PUT ON A WIRE** and are named in the
   output rather than quietly skipped: the two pathname rows, because the
   sender takes `os.path.basename` before the name ever leaves the host; `...`,
   which is not a filename any host filesystem will make; and the empty one.

2. THE CANCEL, AND ITS ORDERING (SPEC.md 70.11.4). A cancelled file dialog
   calls nothing back at all (SPEC.md 38.6), so the cancel is INFERRED from a
   `W_PAINT` arriving while `[tz_dlg]` is still set. Every one of assertion 1's
   dialogs is cancelled with Escape, and each must produce a `ZSKIP` the server
   logs - **within seconds, not within the sixty-second backstop**, which is
   what says the inference held rather than the timeout underneath it.

3. THE TERMINAL COMES BACK (SPEC.md 70.11.5). After the batch, `[te_zon]` is
   clear, `[tz_pan]` is clear and the screen buffer is the board's again.

4. TWO FILES, BYTE FOR BYTE. A second session sends one file under 4 KB - so
   one chunk - and one of about 40 KB, so many, spanning both staging halves
   and ten commits. The dialogs are answered with Return and the files are then
   read back off `build/telnetsys.img` **by hand off the BPB**, with a FAT12
   reader that shares no code with the thing under test, and compared byte for
   byte with what the server sent.

5. AND THE HEADERS BOTH WAYS, out of the server's own JSON log: the `ZRINIT`
   this end advertises (`CANFDX|CANOVIO` and **not** `CANFC32`), a `ZRPOS`, the
   `ZACK`s, and the `ZNAK` that answers the one deliberate `ZBIN32` header
   `--bin32` sends. A screenshot cannot see a header.

**WHERE THE FILES LAND.** `OSAPI_FILE_DLG` gives back a NAME and no path
(SPEC.md 38.6/19.2.1) - the dialog opened on this instance's own folder, which
is where TELNET.O88 was launched from - so a download saved with Return lands
in `APPS/` on the A: disk. That is `build/telnetsys.img`, which is 1.44MB and
has room; the scratch B: is there because a download WRITES and a gate must not
depend on which volume the user picked.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import dispcp                                              # noqa: E402
import os88bbs                                             # noqa: E402
import os88qemu                                            # noqa: E402
import os88sym                                             # noqa: E402
import telansi                                             # noqa: E402

S = os88sym.linear
SOCK = telansi.SOCK
PORT = 8095                     # NOT 8090 (ethernet), 8092 (thewire) or 8094
                                # (telansi): the gates may be run side by side,
                                # and a bound port is a gate reading the OTHER
                                # one's answers
HOSTLINE = "10.0.2.2:%d" % PORT

# The rows of os88bbs.MANGLE83_CASES that a REAL SENDER can carry. The four
# left out are named in the report rather than dropped in silence.
UNSENDABLE = {"/pub/files/banana split.mod": "the sender takes basename()",
              "C:\\dl\\banana split.mod": "the sender takes basename()",
              "...": "no host filesystem will make that file",
              "": "there is no such filename"}


def say(*a):
    print(*a)
    sys.stdout.flush()


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def extract(img, name11, path=()):
    """A file's bytes, by hand off the BPB - optionally down a folder PATH.

    Deliberately NOT os88disk.py's own extractor and a copy of tests/ftpd.py's:
    this is the SECOND OPINION, so it reads the volume the way any FAT12 driver
    would and shares no code with the thing under test.
    """
    b = open(img, "rb").read()
    bps = b[11] | (b[12] << 8)
    spc = b[13]
    rsvd = b[14] | (b[15] << 8)
    nfat = b[16]
    nroot = b[17] | (b[18] << 8)
    spf = b[22] | (b[23] << 8)
    root = (rsvd + nfat * spf) * bps
    data = root + nroot * 32
    fat = b[rsvd * bps: (rsvd + spf) * bps]

    def chain(clus, limit=None):
        out = b""
        while 2 <= clus < 0xFF0 and (limit is None or len(out) < limit):
            off = data + (clus - 2) * spc * bps
            out += b[off: off + spc * bps]
            j = clus + (clus >> 1)              # FAT12: 12 bits an entry
            v = fat[j] | (fat[j + 1] << 8)
            clus = (v >> 4) if (clus & 1) else (v & 0xFFF)
        return out

    ents = b[root: root + nroot * 32]
    for comp in path:
        found = None
        for i in range(0, len(ents), 32):
            e = ents[i:i + 32]
            if not e or e[0] == 0x00:
                break
            if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F:
                continue
            if e[:11].decode("latin-1") == comp and (e[11] & 0x10):
                found = e[26] | (e[27] << 8)
                break
        if found is None:
            return None
        ents = chain(found)
    for i in range(0, len(ents), 32):
        e = ents[i:i + 32]
        if len(e) < 32 or e[0] == 0x00:
            break
        if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F:
            continue
        if e[:11].decode("latin-1") != name11:
            continue
        size = int.from_bytes(e[28:32], "little")
        return chain(e[26] | (e[27] << 8), size)[:size]
    return None


def name11(n):
    stem, _, ext = n.partition(".")
    return (stem.upper()[:8].ljust(8) + ext.upper()[:3].ljust(3))


def make_files(d):
    """The mangle batch: one tiny file per sendable row of the table.

    Each in a directory of its own, because `README.TXT` and `readme.txt` are
    two rows of the table and ONE file on a case-insensitive host - and a gate
    that silently tested eleven rows where it printed twelve would be worse
    than one that tested none.
    """
    out = []
    for i, (src, want) in enumerate(os88bbs.MANGLE83_CASES):
        if src in UNSENDABLE:
            continue
        sub = os.path.join(d, "m%02d" % i)
        os.makedirs(sub)
        p = os.path.join(sub, src)
        with open(p, "wb") as f:
            f.write(b"z" * 16)
        out.append((p, src, want))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave QEMU running for a look afterwards")
    ap.add_argument("--skip-mangle", action="store_true",
                    help="the transfer session alone (a quick loop)")
    a = ap.parse_args()
    fails = []
    tmp = tempfile.mkdtemp()

    # **THE SYSTEM IMAGE IS REBUILT, NOT CHECKED** (tests/telansi.py's reason,
    # and here it is the whole point): QEMU mounts it WRITABLE, the download
    # lands ON it, and a second run would then find the PREVIOUS run's file and
    # pass without the guest having written a byte.
    for img in ("build/telnetsys.img", "build/telnetdata.img"):
        p = os.path.join(ROOT, img)
        if os.path.exists(p):
            os.remove(p)
    r = subprocess.run(["make", "telnettest"], capture_output=True, text=True,
                       cwd=ROOT)
    if r.returncode:
        sys.exit("telzm: make telnettest failed:\n" + r.stdout + r.stderr)

    sy = telansi.te_syms()
    for n in ("te_zon", "te_state", "tz_st", "tz_dlg", "tz_name", "tz_pan",
              "tz_req", "tz_pos", "tz_fsz", "tz_diag",
              "te_btn", "te_line", "te_hbuf"):
        if n not in sy:
            sys.exit("telzm: %s is not in the package map" % n)

    small = os.path.join(tmp, "SMALL.BIN")
    big = os.path.join(tmp, "BIG.BIN")
    # Every byte VALUE, and the `@`-before-CR context the sender's CR rule
    # turns on (tools/os88bbs.py's `_esc`), and a long run of 0xFF - which is
    # the one an `IAC IAC` this end forgot to halve would corrupt.
    sdata = (bytes(range(256)) * 12)[:3000] + b"@\r@\r@\r"
    bdata = bytearray()
    while len(bdata) < 40000:
        bdata += bytes(range(256))
        bdata += b"\xff" * 300
        bdata += b"@\r" * 20
    bdata = bytes(bdata[:40960])
    open(small, "wb").write(sdata)
    open(big, "wb").write(bdata)

    os88qemu.kill()
    if not a.keep:
        os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1",
                        "TESTIMG=build/telnetsys.img",
                        "TESTAPPS=build/telnetdata.img"],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        sys.exit("telzm: make test failed:\n" + r.stdout + r.stderr)

    img = os.path.join(ROOT, "build", "telnetsys.img")
    adata = bytes(range(256)) * 5           # ...the file AFTER the cancelled
                                            # one, compared off the disk below
    m = telansi.Qemu()
    mo = telansi.Mouse()
    logs = {}
    try:
        time.sleep(6.0)                     # ...the boot, then DHCP
        dispcp.open_drive(m, mo, S, telansi.settle, "A")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, telansi.settle, wx, wy, "APPS")
        dispcp.open_named(m, mo, S, telansi.settle, wx, wy, "TELNET.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("telzm: TELNET.O88 did not open a window")
        tw = wins2[-1]
        rec = m.read(S("wm_wins") + tw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = u16(rec, 22)
        say("telnet at %04X" % pseg)

        # **AND IT IS THE PACKAGE THIS BUILD MADE.** telansi.te_syms() proves
        # the MAP describes build/telnet.bin; it says nothing about what the
        # GUEST loaded, and a stale emulator answering the same socket is
        # CLAUDE.md's documented trap one level down - every offset resolves,
        # every read succeeds, and the numbers are another build's. The package
        # is loaded at pseg:0 with no relocation of any kind (SPEC.md 20), so
        # its first bytes ARE the file's.
        pkg = open(os.path.join(ROOT, "build", "telnet.bin"), "rb").read()
        head = m.readseg(pseg, 0, 512)
        if head != pkg[:512]:
            n = next((i for i in range(512) if head[i] != pkg[i]), -1)
            sys.exit("telzm: the guest's TELNET is not build/telnet.bin - "
                     "they first differ at offset %d (guest %02X, file %02X). "
                     "A stale emulator is answering %s, or the disk was not "
                     "rebuilt: every symbol below would resolve and describe "
                     "another build" % (n, head[n], pkg[n], SOCK))
        say("image     512 bytes at pseg:0 match build/telnet.bin")

        def rb(n):
            return m.readseg(pseg, sy[n], 1)[0]

        def rw(n):
            return u16(m.readseg(pseg, sy[n], 2))

        def rstr(n, cap=16):
            return m.readseg(pseg, sy[n], cap).split(b"\0")[0].decode("latin-1")

        def rect(name):
            d = m.readseg(pseg, sy[name], 8)
            return u16(d, 0), u16(d, 2), u16(d, 4), u16(d, 6)

        lx1, ly1, lx2, ly2 = rect("te_line")
        bx1, by1, bx2, by2 = rect("te_btn")
        mo.click((lx1 + lx2) // 2, (ly1 + ly2) // 2)
        telansi.typetext(HOSTLINE)
        # TAB AND NOT RETURN: Return in the host box IS Connect, and it would
        # dial before this test's server exists (SPEC.md 70.12.1).
        telansi.qmp("sendkey tab")
        time.sleep(0.5)
        got = rstr("te_hbuf", 48)
        if got != HOSTLINE:
            fails.append("the host box holds %r and not %r - nothing below "
                         "this connected to the test's server" % (got, HOSTLINE))

        def press_connect():
            mo.click((bx1 + bx2) // 2, (by1 + by2) // 2)

        def connected(timeout=25.0, tries=3):
            """Connect, and MEAN IT.

            One click was not enough between sessions: the Close that ends the
            previous one is a request the WORKER carries out, so a click that
            lands while the state is still TS_UP asks for a second close
            instead of a connection - and TS_ERR from a port the host has only
            just let go is the other way it fails. Both are answered by
            clicking again on a state that is no longer TS_UP.
            """
            for _ in range(tries):
                end = time.time() + timeout
                while time.time() < end:
                    st = rb("te_state")
                    if st == telansi.TS_UP:
                        return True
                    if st in (telansi.TS_DOWN, telansi.TS_ERR):
                        break
                    time.sleep(0.3)
                if rb("te_state") == telansi.TS_UP:
                    return True
                time.sleep(1.0)
                press_connect()
            return rb("te_state") == telansi.TS_UP

        def wait_start(timeout=60.0):
            """The receiver has the stream (SPEC.md 70.9.6's auto-start)."""
            end = time.time() + timeout
            while time.time() < end:
                if rb("te_zon") or rb("tz_diag"):
                    return True
                time.sleep(0.2)
            return False

        def wait_dlg(timeout=60.0):
            """A Save dialog is up.

            **AND IT DOES NOT BAIL ON [te_zon] BEING CLEAR**, which the first
            version did and which cost four emulator runs: the server waits
            half a second after the connection before it starts Zmodem, so
            [te_zon] is 0 for a while AFTER the session comes up and the early
            exit fired on the first poll. Everything the gate then read - every
            counter, [tz_st], [tz_diag] - was read BEFORE the transfer began,
            so a receiver doing exactly the right thing reported as one that
            had not run at all.
            """
            end = time.time() + timeout
            while time.time() < end:
                if rb("tz_dlg"):
                    return True
                time.sleep(0.2)
            return False

        def wait_nodlg(timeout=30.0):
            end = time.time() + timeout
            while time.time() < end:
                if not rb("tz_dlg"):
                    return True
                time.sleep(0.2)
            return False

        def wait_off(timeout=180.0):
            end = time.time() + timeout
            while time.time() < end:
                if not rb("te_zon"):
                    return True
                time.sleep(0.5)
            return False

        # =====================================================================
        # SESSION 1 - the mangle, and the cancel that carries it
        # =====================================================================
        if not a.skip_mangle:
            batch = make_files(tmp)
            srv = os88bbs.BBSServer(port=PORT, files=[p for p, _, _ in batch],
                                    zwait=0.5, timeout=240.0, once=True)
            srv.start()
            time.sleep(0.3)
            press_connect()
            if not connected():
                srv.stop()
                fails.append("session 1 never reached TS_UP (te_state %d)"
                             % rb("te_state"))
            elif not wait_start():
                srv.stop()
                fails.append("session 1: the Zmodem auto-start never fired - "
                             "[te_zon] 0, [tz_diag] 0 (SPEC.md 70.9.6)")
            else:
                seen = []
                for _, src, want in batch:
                    if not wait_dlg():
                        fails.append("no Save dialog for %r - [tz_st] %d, "
                                     "[te_zon] %d, [tz_diag] %d"
                                     % (src, rb("tz_st"), rb("te_zon"),
                                        rb("tz_diag")))
                        break
                    got = rstr("tz_name", 16)
                    seen.append((src, want, got))
                    # **COMMITTED, NOT CANCELLED.** The mangle is what this
                    # session is for, and every dialog answered with Return
                    # also lands the file - so the names are asserted twice,
                    # once in [tz_name] and once as a directory entry. The
                    # cancel gets a session of its own below, which is what
                    # SPEC.md 70.12 asks for.
                    telansi.qmp("sendkey ret")
                    if not wait_nodlg(20.0):
                        fails.append("the dialog for %r was answered and "
                                     "[tz_dlg] is still set after 20s" % src)
                        break
                for src, want, got in seen:
                    ok = "ok " if got == want else "MISMATCH"
                    say("mangle  %-28r -> %-13r %s" % (src, got, ok))
                    if got != want:
                        fails.append("mangle83(%r) is %r on the machine and %r "
                                     "in tools/os88bbs.py - the two readers of "
                                     "SPEC.md 77.20's rule disagree"
                                     % (src, got, want))
                if len(seen) != len(batch):
                    fails.append("only %d of %d dialogs appeared"
                                 % (len(seen), len(batch)))
                wait_off(60.0)
                # --- 3: the terminal is back --------------------------------
                if rb("te_zon"):
                    fails.append("[te_zon] is still set after the batch - the "
                                 "receiver never gave the stream back")
                if rb("tz_pan"):
                    fails.append("[tz_pan] is still set - the progress "
                                 "takeover never came off (SPEC.md 70.11.5)")
                srv.stop()
                logs["mangle"] = srv.log_dict()
                sent = [f for f in logs["mangle"].get("zmodem_files", [])
                        if f.get("result") == "sent"]
                say("batch   %d of %d files sent, %d dialogs"
                    % (len(sent), len(batch), len(seen)))
                if len(sent) != len(batch):
                    fails.append("%d of %d files in the batch were sent - a "
                                 "batch is ZFILE again after ZEOF and each "
                                 "file gets its own dialog (SPEC.md 70.11.2)"
                                 % (len(sent), len(batch)))
            press_connect()                     # Close: ASKED, and the worker
            for _ in range(40):                 # is what carries it out
                if rb("te_state") != telansi.TS_UP:
                    break
                time.sleep(0.3)
            time.sleep(1.5)

        # =====================================================================
        # SESSION 2 - two files, byte for byte, and one deliberate ZBIN32
        # =====================================================================
        sizes = {}
        srv = os88bbs.BBSServer(port=PORT, files=[small, big], zwait=0.5,
                                timeout=300.0, once=True, bin32=True)
        srv.start()
        time.sleep(0.3)
        press_connect()
        if not connected():
            fails.append("session 2 never reached TS_UP (te_state %d)"
                         % rb("te_state"))
        elif not wait_start():
            fails.append("session 2: the Zmodem auto-start never fired - "
                         "[te_zon] 0, [tz_diag] 0 (SPEC.md 70.9.6)")
        else:
            sizes = {}
            for want in ("SMALL.BIN", "BIG.BIN"):
                if not wait_dlg(60.0):
                    fails.append("no Save dialog for %s - [tz_st] %d, "
                                 "[te_zon] %d, [tz_diag] %d (1 asked, 2 the "
                                 "slot refused, 4 a paint inferred a cancel, "
                                 "8 the completion proc ran, 16 it went up), "
                                 "%d window(s)"
                                 % (want, rb("tz_st"), rb("te_zon"),
                                    rb("tz_diag"), len(dispcp.win_list(m, S))))
                    subprocess.run([sys.executable,
                                    os.path.join(ROOT, "tools", "shot.py"),
                                    SOCK, os.path.join(ROOT, "build",
                                                       "telzm-nodlg.png")],
                                   check=False, capture_output=True, cwd=ROOT)
                    break
                got = rstr("tz_name", 16)
                # **READ AT THE DIALOG, not after it.** [tz_fsz] is the size the
                # sender declared for the file being ASKED about, and the next
                # ZFILE overwrites it - so a read after the answer is a read of
                # whichever file came next.
                fsz = rw("tz_fsz")
                if got != want:
                    fails.append("the dialog was pre-filled %r and not %r"
                                 % (got, want))
                telansi.qmp("sendkey ret")      # ...SAVE, under the name the
                time.sleep(1.0)                 # receiver mangled
                wait_nodlg(20.0)
                say("saving  %-10s %6d bytes declared, [tz_diag] %d"
                    % (got, fsz, rb("tz_diag")))
                sizes[want] = fsz
            if not wait_off(240.0):
                fails.append("the transfer never finished - [tz_st] %d, "
                             "[tz_req] %d, %d bytes committed"
                             % (rb("tz_st"), rb("tz_req"), rw("tz_pos")))
        time.sleep(1.5)
        press_connect()
        for _ in range(40):
            if rb("te_state") != telansi.TS_UP:
                break
            time.sleep(0.3)
        time.sleep(1.5)
        srv.stop()
        logs["xfer"] = srv.log_dict()

        # =====================================================================
        # SESSION 3 - THE CANCEL (SPEC.md 70.11.4/70.12)
        # =====================================================================
        cfile = os.path.join(tmp, "CANCEL.BIN")
        afile = os.path.join(tmp, "AFTER.BIN")
        open(cfile, "wb").write(b"n" * 2048)
        open(afile, "wb").write(adata)
        srv = os88bbs.BBSServer(port=PORT, files=[cfile, afile], zwait=0.5,
                                timeout=200.0, once=True)
        srv.start()
        time.sleep(0.3)
        press_connect()
        if not connected():
            fails.append("session 3 never reached TS_UP (te_state %d)"
                         % rb("te_state"))
        elif not wait_dlg(60.0):
            fails.append("session 3: no Save dialog to cancel - [tz_st] %d, "
                         "[tz_diag] %d" % (rb("tz_st"), rb("tz_diag")))
        else:
            t0 = time.time()
            telansi.qmp("sendkey esc")      # ...and a CANCEL calls nothing
                                            # back at all (SPEC.md 38.6)
            ok = wait_nodlg(25.0)
            dt = time.time() - t0
            if not ok:
                fails.append("the dialog was cancelled and [tz_dlg] is still "
                             "set after 25s - the window-count inference did "
                             "not hold (SPEC.md 70.11.4)")
                subprocess.run([sys.executable,
                                os.path.join(ROOT, "tools", "shot.py"), SOCK,
                                os.path.join(ROOT, "build",
                                             "telzm-cancel.png")],
                               check=False, capture_output=True, cwd=ROOT)
            elif dt > 30.0:
                fails.append("the cancel took %.1fs - that is the 60-second "
                             "backstop and not the inference (SPEC.md 70.11.4)"
                             % dt)
            else:
                say("cancel  [tz_dlg] cleared in %.1fs, well inside the 60s "
                    "backstop" % dt)
            # **A ZSKIP ENDS ONE FILE AND NOT THE BATCH** (SPEC.md 70.11.2), so
            # the file AFTER the cancelled one gets its own dialog and this
            # answers it. Without the Return it would sit until the 60-second
            # backstop cancelled it too, and the gate would then be asserting
            # its own impatience.
            if not wait_dlg(60.0):
                fails.append("no Save dialog for the file AFTER the cancelled "
                             "one - a ZSKIP ends one file and not the batch "
                             "(SPEC.md 70.11.2). [tz_st] %d, [tz_diag] %d"
                             % (rb("tz_st"), rb("tz_diag")))
            else:
                say("cancel  the next file asked: %r" % rstr("tz_name", 16))
                telansi.qmp("sendkey ret")
                wait_nodlg(20.0)
            wait_off(120.0)
            if rb("te_zon"):
                fails.append("[te_zon] is still set after the cancel run - the "
                             "receiver never gave the stream back")
            if rb("tz_pan"):
                fails.append("[tz_pan] is still set - the progress takeover "
                             "never came off (SPEC.md 70.11.5)")
        time.sleep(1.0)
        press_connect()
        time.sleep(1.5)
        srv.stop()
        logs["cancel"] = srv.log_dict()
        zc = logs["cancel"].get("zmodem", [])
        if not any(e.get("frame") == "ZSKIP" and e.get("dir") == "rx"
                   for e in zc):
            fails.append("the dialog was cancelled and the server saw no "
                         "ZSKIP - a cancel that sends nothing leaves the "
                         "sender waiting (SPEC.md 70.11.2)")
        else:
            say("cancel  ZSKIP reached the server")
        cf = logs["cancel"].get("zmodem_files", [])
        if not any(f.get("result") == "skipped" for f in cf):
            fails.append("no file in the cancel run was skipped: %r" % cf)
        if not any(f.get("result") == "sent" for f in cf):
            fails.append("the file AFTER the cancelled one did not arrive - a "
                         "ZSKIP ends one file and not the batch "
                         "(SPEC.md 70.11.2): %r" % cf)
        # ...and the DISK half of the cancel run waits for the section below:
        # QEMU is still holding the image open here and has not flushed it.
    finally:
        if not a.keep:
            m.quit()
            time.sleep(1.5)             # ...let QEMU flush the image it has
                                        # been writing, before it is read back

    # --- 4: THE BYTES, off the disk, by an independent FAT12 reader ---------
    for name, want in (("SMALL.BIN", sdata), ("BIG.BIN", bdata)):
        got = sizes.get(name) if "sizes" in dir() else None
        if got is not None and got != (len(want) & 0xFFFF):
            fails.append("the dialog for %s declared %d bytes and the server "
                         "sent %d - the size is the SECOND field of the ZFILE "
                         "info block and the only other one this terminal "
                         "reads (SPEC.md 70.11.2)" % (name, got, len(want)))
        # **`MEDIA/`, AND THAT IS SPEC.md 38.10 AND NOT AN ACCIDENT.** The
        # completion proc hands back a NAME and no path; the dialog opened on
        # this instance's own folder, and an application that has chosen
        # nowhere opens on MEDIA. The first version of this gate looked in
        # APPS/ - where TELNET.O88 was launched from - and reported a transfer
        # that had worked perfectly as one that never landed.
        got = extract(img, name11(name), path=("MEDIA      ",))
        if got is None:
            where = [d for d in ("MEDIA      ", "APPS       ", "SYSTEM     ")
                     if extract(img, name11(name), path=(d,)) is not None]
            fails.append("%s is not in MEDIA/ on the A: disk (SPEC.md 38.10's "
                         "default folder)%s" % (name, (" - it is in %s"
                         % ",".join(w.strip() for w in where)) if where
                         else " and in no folder this gate looked in"))
            continue
        if got == want:
            say("bytes   %-10s %6d bytes, identical" % (name, len(got)))
        else:
            first = next((i for i in range(min(len(got), len(want)))
                          if got[i] != want[i]), min(len(got), len(want)))
            fails.append("%s is %d bytes on the disk and %d were sent; the "
                         "first difference is at offset %d (got %02X, want %02X)"
                         % (name, len(got), len(want), first,
                            got[first] if first < len(got) else -1,
                            want[first] if first < len(want) else -1))

    # --- ...and the cancel run, ON THE DISK ---------------------------------
    if extract(img, name11("CANCEL.BIN"), path=("MEDIA      ",)) is not None:
        fails.append("CANCEL.BIN is on the disk and its dialog was CANCELLED - "
                     "a ZSKIP must write nothing")
    agot = extract(img, name11("AFTER.BIN"), path=("MEDIA      ",))
    if agot != adata:
        fails.append("AFTER.BIN is %s on the disk and %d bytes were sent - the "
                     "file after a cancelled one must arrive whole (SPEC.md "
                     "70.11.2)"
                     % ("absent" if agot is None else "%d bytes" % len(agot),
                        len(adata)))
    else:
        say("cancel  CANCEL.BIN wrote nothing, AFTER.BIN arrived whole "
            "(%d bytes)" % len(agot))

    # --- ...and the mangled names as DIRECTORY ENTRIES ----------------------
    # The same rule read a second way: [tz_name] is what the receiver computed
    # and this is what the file system kept, and a Save dialog answered with
    # Return stores exactly the name it was pre-filled with.
    for src, want in os88bbs.MANGLE83_CASES:
        if src in UNSENDABLE:
            continue
        if extract(img, name11(want), path=("MEDIA      ",)) is None:
            fails.append("the batch's %r was saved as %r and MEDIA/ has no "
                         "such file" % (src, want))
    say("names   %d mangled names are directory entries in MEDIA/"
        % (len(os88bbs.MANGLE83_CASES) - len(UNSENDABLE)))

    # --- 5: the headers both ways, out of the server's own log --------------
    zl = logs.get("xfer", {}).get("zmodem", [])
    kinds = [(e.get("dir"), e.get("frame")) for e in zl]
    def has(d, f):
        return any(x == (d, f) for x in kinds)
    for d, f in (("rx", "ZRINIT"), ("rx", "ZRPOS"), ("rx", "ZACK"),
                 ("rx", "ZNAK"), ("rx", "ZFIN")):
        if has(d, f):
            say("header  %s %s" % (d, f))
        else:
            fails.append("no %s %s in the server's log - the headers are what "
                         "SPEC.md 70.11 is about and a screenshot cannot see "
                         "one" % (d, f))
    rin = [e for e in zl if e.get("what") == "ZRINIT flags"]
    if not rin:
        fails.append("the server never decoded a ZRINIT's flags")
    else:
        r0 = rin[0]
        if not (r0.get("canfdx") and r0.get("canovio")):
            fails.append("ZRINIT advertises %r - SPEC.md 70.11.1 says "
                         "CANFDX|CANOVIO" % r0)
        if r0.get("canfc32"):
            fails.append("ZRINIT advertises CANFC32 and this terminal has no "
                         "CRC-32 (SPEC.md 70.11.1) - the sender would use it")
        if r0.get("bufsize"):
            fails.append("ZRINIT names a receive buffer of %d and SPEC.md "
                         "70.11.1 sends 0: no window, so the back-pressure is "
                         "TCP's" % r0["bufsize"])
        else:
            say("header  ZRINIT bufsize 0, CANFDX|CANOVIO, no CANFC32")
    b32 = [e for e in zl if e.get("fmt") == "bin32"]
    if not b32:
        fails.append("the server logged no ZBIN32 header - --bin32 sends one "
                     "deliberately so the refusal path is DRIVEN")
    elif not has("rx", "ZNAK"):
        fails.append("a ZBIN32 header went out and no ZNAK came back "
                     "(SPEC.md 70.11.1)")

    for k in ("mangle", "xfer"):
        if k in logs:
            p = os.path.join(ROOT, "build", "telzm-%s.json" % k)
            json.dump(logs[k], open(p, "w"), indent=1, sort_keys=True)

    say("")
    for f in fails:
        say("FAIL  " + f)
    say("telzm: %d failure(s)" % len(fails))
    if not a.skip_mangle:
        for src, why in sorted(UNSENDABLE.items()):
            say("note: MANGLE83_CASES %r is not asserted here - %s"
                % (src, why))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
