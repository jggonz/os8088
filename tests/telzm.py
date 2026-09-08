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
(SPEC.md 38.6/19.2.1) and the dialog opens on **`MEDIA/`** for an application
that has chosen nowhere (SPEC.md 38.10) - so a download saved with Return lands
in `MEDIA/` on the A: disk, which is `build/telnetsys.img`. `build/telnetdata.img`
is built and mounted as B: and this gate never inspects it: it is there because
a download WRITES and because the Drive button can reach it, not because these
files go there.

**WHAT THIS GATE CANNOT REACH, said here rather than left to be discovered.**
Every geometry it drives is a floppy, so `spc` is 1 and the cluster is 512:
that leaves the SINGLE-buffered arm, the over-8,192 refusal and
`OSAPI_FILE_APPEND`'s precondition at any other cluster size untested, and it
is the Drive button that would reach them. No CRC error is injected, so
`tz_subbad`, the `[tz_skip]` overlap and the whole ZRPOS-recovery path are
driven by nothing here - the `("rx","ZRPOS")` assertion below is satisfied by
the opening `ZRPOS 0`. `ZCRCE` and `ZCRCQ` never appear (the sender uses ZCRCG
and ZCRCW), nor do `ZRUB0`/`ZRUB1` (its escaper has no arm for 0x7F/0xFF), and
1,024 divides 4,096 so no subpacket ever straddles a chunk boundary. The
raise-cache purge behind SPEC.md 70.11.4's cancel rule needs a machine short of
memory and this desktop is idle.
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
import os88build

S = os88sym.linear
SOCK = telansi.SOCK
PORT = 8095                     # NOT 8090 (ethernet), 8092 (thewire) or 8094
                                # (telansi): the gates may be run side by side,
                                # and a bound port is a gate reading the OTHER
                                # one's answers
HOSTLINE = "10.0.2.2:%d" % PORT
TE_SCRSZ = 80 * 25 * 2          # ...the screen this gate reads back, which is
                                # telansi's TE_COLS x TE_ROWS x 2 (SPEC.md 70.8)

# The rows of os88bbs.MANGLE83_CASES that a REAL SENDER can carry. The four
# left out are named in the report rather than dropped in silence.
UNSENDABLE = {"/pub/files/banana split.mod": "the sender takes basename()",
              "C:\\dl\\banana split.mod": "the sender takes basename()",
              "...": "no host filesystem will make that file",
              "": "there is no such filename"}


REAL_SENDER = os88bbs.ZmodemSender      # ...captured BEFORE the patch below


class LyingSize(REAL_SENDER):
    """A sender that declares a size of 1 for a file it then sends in full.

    **THIS IS THE BLOCKER'S OWN CASE** (SPEC.md 70.11.6). `[tz_fsz]` is a
    decimal field the SENDER writes into the ZFILE info block and `[tz_pos]` is
    bytes this end has committed: two independent numbers, and `tz_frac` divided
    one by the other to size the progress bar. A declared size of 1 makes the
    first commit's quotient 2,539,520, which does not fit AX - and `div` raises
    #DE, for which this kernel installs no handler. One line of a sender took
    the machine out, with no cooperation from the user beyond pressing Save.

    The hook is `send_data`: the ZFILE info block is the only `ZCRCW`-terminated
    subpacket with a NUL in it that goes out before any `ZDATA`, so rewriting
    its first field is one substitution and needs no copy of the sender's loop.
    """

    # **AND THE SUPER CALLS NAME `ZmodemBase`, NOT `ZmodemSender`.** The gate
    # installs this class AS `os88bbs.ZmodemSender` for the session, because
    # that is the name `BBSServer` looks up - so `os88bbs.ZmodemSender.<m>` from
    # inside a method resolves to THIS class and recurses until the server
    # thread dies of it. `send_data` and `__init__` both live on `ZmodemBase`,
    # which the patch does not touch.

    def __init__(self, *a, **kw):
        REAL_SENDER.__init__(self, *a, **kw)
        self.lied = 0

    def send_data(self, data, frameend):
        if frameend == os88bbs.ZCRCW and b"\0" in data and self.lied < 9:
            name, _, rest = data.partition(b"\0")
            f = rest.split(b" ")
            if len(f) > 1 and f[0].isdigit():
                f[0] = b"1"
                data = name + b"\0" + b" ".join(f)
                self.lied += 1
        return REAL_SENDER.send_data(self, data, frameend)


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


def wait_desktop(m, letter="A", secs=90):
    """Block until drive `letter` HAS a desktop zone, or say what it saw.

    This was `time.sleep(6.0)`, and six seconds is a guess about somebody
    else's box. The row then called open_drive, which walks dsk_vtab and
    raises "drive A: has no desktop zone on this machine" when the volume is
    not mounted yet - a message about the DISK for a machine that was still
    booting. Under a soak, with four lanes on four cores, QEMU gets a fraction
    of a core and six seconds is not the boot.

    docs/WRITING-TESTS.md's rule: wait on the CONDITION, not the clock. The
    condition is the one open_drive is about to test, so a pass here means the
    next line cannot fail for this reason - and the failure names the machine
    rather than the feature.
    """
    for _ in range(int(secs / 0.4)):
        try:
            if dispcp.drive_ordinal(m, S, letter) is not None:
                return
        except Exception:                   # the guest is not answering yet
            pass
        time.sleep(0.4)
    sys.exit("telzm: drive %s: had no desktop zone after %ds - the guest "
             "never reached a desktop (a boot failure, not a telzm failure)"
             % (letter, secs))


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
              "tz_req", "tz_pos", "tz_rcv", "tz_fsz", "tz_diag", "te_scr",
              "tz_why", "tz_ferr",
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
    ldata = bytes(range(256)) * 24          # ...and the lying sender's, which
                                            # must land whole despite its ZFILE
    m = telansi.Qemu()
    mo = telansi.Mouse()
    logs = {}
    try:
        wait_desktop(m, "A")            # ...the boot, then DHCP
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
        pkg = open(os.path.join(ROOT, os88build.at("build/telnet.bin")),
                   "rb").read()
        head = bytearray(m.readseg(pseg, 0, 512))
        want = bytearray(pkg[:512])
        # THE FLAGS BYTE IS THE ONE DIFFERENCE, and it is not a mismatch
        # (SPEC.md 20.13.5, tests/lzload.py's own note): every shipped package
        # is compressed on this branch, and the EXPANDED image keeps saying so
        # in bits 3 and 4 while the raw `build/telnet.bin` nasm emitted cannot.
        # Without this the row read `guest 09, file 01` at offset 3 and blamed
        # a stale emulator for a package that is byte-for-byte correct.
        head[3] &= ~0x18
        want[3] &= ~0x18
        if head != want:
            n = next((i for i in range(512) if head[i] != want[i]), -1)
            sys.exit("telzm: the guest's TELNET is not build/telnet.bin - "
                     "they first differ at offset %d (guest %02X, file %02X). "
                     "A stale emulator is answering %s, or the disk was not "
                     "rebuilt: every symbol below would resolve and describe "
                     "another build" % (n, head[n], want[n], SOCK))
        say("image     512 bytes at pseg:0 match build/telnet.bin")

        def rb(n):
            return m.readseg(pseg, sy[n], 1)[0]

        def rw(n):
            return u16(m.readseg(pseg, sy[n], 2))

        def rstr(n, cap=16):
            return m.readseg(pseg, sy[n], cap).split(b"\0")[0].decode("latin-1")

        def why():
            """[tz_why] -> the string it points at, which is the terminal's own
            account of why a transfer stopped (SPEC.md 47)."""
            p = rw("tz_why")
            if not p:
                return ""
            return (m.readseg(pseg, p, 32).split(b"\0")[0]
                    .decode("latin-1"))

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
            # **AND THE BOARD'S SCREEN IS UNTOUCHED.** The receiver owns the
            # stream from the handover to the sender's closing `OO`, so not one
            # byte of it may reach the ANSI parser - and ending on the FIRST of
            # those two `O`s left the second to be printed, so every completed
            # batch used to leave a stray `O` on the screen.
            scr = m.readseg(pseg, sy["te_scr"], TE_SCRSZ)
            lit = [i // 2 for i in range(0, TE_SCRSZ, 2) if scr[i] != 0x20]
            got = bytes(scr[2 * c] for c in lit)
            say("screen  %d cell(s) hold a glyph after the batch: %r"
                % (len(lit), got))
            # **`**B0` IS THE ONLY THING THAT MAY BE THERE**, and it is SPEC.md
            # 70.9.6's own behaviour: the auto-start's matched bytes are DRAWN
            # on the way past, the 0x18 is consumed as CAN and the final `0` is
            # never drawn because the detector fires on it. Anything else is a
            # byte of the transfer that reached the ANSI parser - which is what
            # ending on the FIRST of the sender's two closing `O`s used to
            # leave behind.
            if got != b"**B0" or lit != [0, 1, 2, 3]:
                fails.append("the terminal holds %r at cells %r after a Zmodem "
                             "batch; SPEC.md 70.9.6 leaves exactly `**B0` in "
                             "the first four, so the rest is transfer data that "
                             "reached the ANSI parser" % (got, lit[:8]))
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
        # SESSION 3 - A SENDER THAT DECLARES THE WRONG SIZE (SPEC.md 70.11.6)
        # =====================================================================
        lfile = os.path.join(tmp, "LIAR.BIN")
        open(lfile, "wb").write(ldata)
        os88bbs.ZmodemSender = LyingSize
        try:
            srv = os88bbs.BBSServer(port=PORT, files=[lfile], zwait=0.5,
                                    timeout=200.0, once=True)
            srv.start()
            time.sleep(0.3)
            press_connect()
            if not connected():
                fails.append("session 3 never reached TS_UP (te_state %d)"
                             % rb("te_state"))
            elif not wait_start():
                fails.append("session 3: the Zmodem auto-start never fired for "
                             "the lying sender - [te_zon] 0, so its ZRQINIT "
                             "never arrived")
            elif not wait_dlg(60.0):
                fails.append("session 3: no Save dialog for the lying sender - "
                             "[tz_st] %d, [tz_diag] %d"
                             % (rb("tz_st"), rb("tz_diag")))
            else:
                say("liar    declared %d bytes, sending %d"
                    % (rw("tz_fsz"), len(ldata)))
                telansi.qmp("sendkey ret")
                wait_nodlg(20.0)
                if not wait_off(180.0):
                    fails.append("the lying sender's transfer never finished - "
                                 "[tz_st] %d, %d bytes committed"
                                 % (rb("tz_st"), rw("tz_pos")))
                # **THE MACHINE IS STILL THERE**, which is the whole assertion:
                # an #DE on a kernel with no int 0 handler is an uncontrolled
                # far jump, and its symptom is a guest that has stopped
                # answering rather than a wrong progress bar.
                st = rb("te_state")
                if st != telansi.TS_UP:
                    fails.append("after a ZFILE declaring size 1 the session is "
                                 "in state %d - a `div` by a number the WIRE "
                                 "chose raises #DE and this kernel installs no "
                                 "int 0 handler (SPEC.md 70.11.6)" % st)
                else:
                    say("liar    the session is still up and the terminal is "
                        "back")
            time.sleep(1.0)
            press_connect()
            for _ in range(40):
                if rb("te_state") != telansi.TS_UP:
                    break
                time.sleep(0.3)
            time.sleep(1.5)
            srv.stop()
            logs["liar"] = srv.log_dict()
            if srv.error:
                fails.append("the lying sender's server thread died: %s"
                             % srv.error)
        finally:
            os88bbs.ZmodemSender = REAL_SENDER

        # =====================================================================
        # SESSION 4 - THE CANCEL (SPEC.md 70.11.4/70.12)
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
            fails.append("session 4 never reached TS_UP (te_state %d)"
                         % rb("te_state"))
        elif not wait_dlg(60.0):
            fails.append("session 4: no Save dialog to cancel - [tz_st] %d, "
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
            ok = wait_off(120.0)
            say("cancel  [tz_st] %d [tz_req] %d [tz_diag] %d, %d committed of "
                "%d received, reason %r"
                % (rb("tz_st"), rb("tz_req"), rb("tz_diag"), rw("tz_pos"),
                   rw("tz_rcv"), why()))
            if rb("tz_ferr"):
                say("cancel  the commit was refused with FERR %d"
                    % rb("tz_ferr"))
            if not ok:
                fails.append("the cancel run's second file never finished")
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

        # =====================================================================
        # SESSION 5 - THE MANGLE, AND IT RUNS LAST
        #
        # **THE FOLDER IS WHY IT IS LAST AND WHY ITS TAIL IS CANCELLED.** A
        # subdirectory on this volume is ONE 512-byte cluster - sixteen entries
        # - and it does not grow: three are taken before this test runs and the
        # four sessions above add four more, so nine slots are left for a table
        # of twelve rows. And a CANCELLED dialog is only reaped on a later UI
        # pass (SPEC.md 38.1.1), during which `OSAPI_FILE_DLG` refuses and the
        # receiver's bounded retry gives up - which was reliable for one cancel
        # and not for two, so cancels are safe only where no dialog follows.
        #
        # Both constraints point the same way: this session runs LAST, commits
        # the first nine rows and cancels the last three. Every row's mangle is
        # asserted from [tz_name] at its dialog; the committed ones are
        # asserted again as directory entries.
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
                    # **CANCELLED, EXCEPT ONE, AND THE FOLDER IS WHY.** A
                    # subdirectory on this volume is ONE 512-byte cluster - 16
                    # entries - and it does not grow: committing all twelve
                    # filled `MEDIA/` and the next session's first write was
                    # refused FERR_DIRFULL, which is a fact about the gate and
                    # not about the receiver. So the mangle is asserted from
                    # [tz_name] and the ROUND TRIP from one committed row, and
                    # the other eleven exercise the cancel path eleven times
                    # over on the way past.
                    # **WHICH ROWS ARE CANCELLED IS NOT ARBITRARY**, and the
                    # two constraints pull opposite ways.
                    #
                    #  * A subdirectory on this volume is ONE 512-byte cluster -
                    #    sixteen entries - and it does not grow, so committing
                    #    all twelve filled `MEDIA/` and the next session's first
                    #    write came back FERR_DIRFULL.
                    #  * TWO CONSECUTIVE CANCELS produced no third dialog: the
                    #    dialog a cancel leaves behind is only reaped on a later
                    #    UI pass (SPEC.md 38.1.1), `OSAPI_FILE_DLG` refuses
                    #    while it is up, and the receiver's bounded retry then
                    #    gives up and ZSKIPs. Observed, not explained - see
                    #    /tmp/bbs-reports/w4-fix.md.
                    #
                    # So four NON-ADJACENT rows are cancelled, which leaves
                    # seven distinct names on the disk, two spare slots, and no
                    # cancel next to another.
                    # **TWO CANCELS IN A ROW, THEN A THIRD DIALOG** (rows 3 and
                    # 4, with row 5 following), which is what the w4 re-review
                    # traced to MAJOR 4's remaining hole rather than to the
                    # kernel: Escape destroys the dialog and drops `[fdlg_win]`
                    # in one keystroke, so a repeated refusal meant a dialog was
                    # really up while `[tz_dlg]` said otherwise. With the slot
                    # asked before the paint, and `[tz_dtry]` reset on the
                    # give-up path, the third dialog must appear.
                    #
                    # The other cancels are the last three, where no dialog
                    # follows and the folder's sixteen entries are what decides:
                    # nine rows commit and MEDIA/ keeps two slots spare.
                    n = len(seen)
                    keep = n not in (3, 4) and n <= 9
                    telansi.qmp("sendkey ret" if keep else "sendkey esc")
                    if not wait_nodlg(25.0):
                        fails.append("the dialog for %r was %s and [tz_dlg] is "
                                     "still set after 25s"
                                     % (src, "answered" if keep else
                                        "cancelled"))
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
                zf = logs["mangle"].get("zmodem_files", [])
                sent = [f for f in zf if f.get("result") == "sent"]
                skip = [f for f in zf if f.get("result") == "skipped"]
                say("batch   %d dialogs, %d file(s) sent and %d skipped of %d"
                    % (len(seen), len(sent), len(skip), len(batch)))
                if len(seen) != len(batch):
                    fails.append("%d of %d dialogs appeared - a batch is ZFILE "
                                 "again after ZEOF and each file gets its own "
                                 "(SPEC.md 70.11.2)" % (len(seen), len(batch)))
                if len(sent) != 7 or len(skip) != 5:
                    fails.append("the batch answered seven dialogs with Return "
                                 "and five with Escape - two of them ADJACENT, "
                                 "which is the case the w4 re-review traced to "
                                 "MAJOR 4 - and the server saw %d sent and %d "
                                 "skipped: %r" % (len(sent), len(skip), zf))
            press_connect()                     # Close: ASKED, and the worker
            for _ in range(40):                 # is what carries it out
                if rb("te_state") != telansi.TS_UP:
                    break
                time.sleep(0.3)
            time.sleep(1.5)

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
    for name, want in (("SMALL.BIN", sdata), ("BIG.BIN", bdata),
                       ("LIAR.BIN", ldata)):
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

    # --- ...and the ROUND TRIP, as a directory entry ------------------------
    # The same rule read a second way: [tz_name] is what the receiver computed
    # and this is what the file system kept. One row rather than twelve, because
    # a subdirectory here is 16 entries and does not grow - the other eleven
    # dialogs are cancelled and write nothing, which is what leaves room for the
    # sessions below.
    # The nine rows the batch COMMITTED, read back as directory entries:
    # [tz_name] is what the receiver computed and this is what the file system
    # kept, which is the same rule read twice (SPEC.md 70.11.4). The three the
    # batch cancels are asserted at their dialogs and write nothing.
    sendable = [w for src, w in os88bbs.MANGLE83_CASES
                if src not in UNSENDABLE]
    landed = [w for i, w in enumerate(sendable, 1)
              if i not in (3, 4) and i <= 9]
    missing = [w for w in landed
               if extract(img, name11(w), path=("MEDIA      ",)) is None]
    if missing:
        fails.append("the batch committed %r and MEDIA/ has no such file(s)"
                     % missing)
    else:
        say("names   %d mangled names are directory entries in MEDIA/"
            % len(set(name11(w) for w in landed)))

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

    for k in sorted(logs):
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
