#!/usr/bin/env python3
"""THE WIRE, end to end over a real card (SPEC.md 88.12).

    make && make thewiretest && python3 tests/thewire.py

**THIS ONE IS QEMU'S, AND THAT IS NOT A LAPSE.** CLAUDE.md's rule is MartyPC
first with a short list of exceptions, and this is on it for tests/ethernet.py's
reason exactly: MartyPC has no network card of any kind, so `ETHER.DRV` cannot
be hosted on it at all. QEMU's `ne2k_isa` on `-netdev user` is the only harness
there is - a gateway at 10.0.2.2, DHCP handing out 10.0.2.15.

What QEMU costs is what it always costs: the machine is not an 8088 and no
timing here means anything. Every assertion below is about BEHAVIOUR and none
is about speed, so there is no number in this file for PERFORMANCE.md to want
off the 5150.

THE HOST SERVES A FIXTURE CATALOG, packed by `tools/os88wire.py` out of
`build/hello.o88`, `build/mines.o88`, a tier-3 `WF_DISK` entry with one
sidecar and a `WF_ARC` one whose `.WPK` this file packs with `--archive` out
of a fixture tree it writes, on **port 8092** - tests/ethernet.py's is 8090, and the two gates may
be run side by side; a bound port is a gate reading the other one's answers.
`make thewiretest`'s `SYSTEM/APPDATA/WIRE.CFG` names `10.0.2.2:8092/wire/`, so
the machine asks the host for it and every byte under test is a byte this file
chose.

ELEVEN ASSERTIONS.

1. THE CATALOG ARRIVES AND IS UNDERSTOOD. `wr_n` is 4, `wr_state` is WS_DONE
   and `wr_nodrv` is 0 - which is the whole stack under it (ARP, a handshake,
   a GET, a windowed drain, a close) plus wr_catck having accepted every field.
2. THE HOST SAW THE REQUEST IT EXPECTED, `GET /wire/catalog.bin HTTP/1.0` with
   a Host: header, so the path composition and WIRE.CFG both worked.
3. THE LIST IS THE CATALOG. The scroll block's `total` - which is the machine's
   own `wr_count`, written by the painter - is 4.
4. THE FILTER FILTERS. Clicking `8088/8086` leaves `wr_filter` = 1 and that
   same count at 3, because the fixture's third entry is tier 3.
5. THE PREDICATE GREYS THE RIGHT BUTTON. Selecting the tier-3 `WF_DISK` entry
   leaves `wr_grey` bit 0 set and bit 1 clear: Load Program refuses (its files
   must be on a disk) and Add to Disk does not.
6. THE PICTURE IS DRAWN, AND THE RIGHT WAY UP. HELLO carries a `.PIC` with a
   pattern this file chose; selecting it fetches the picture and the 128 x 64
   block on the glass is compared PIXEL FOR PIXEL against the file's bits.
   That is the one assertion nothing else can make: SPEC.md 88.3 stores the
   band INVERTED so that one `gfx_blit1` is correct on all three adapters, and
   an inversion that went the wrong way draws a perfectly plausible picture in
   negative.
7. ADD TO DISK WRITES BOTH FILES, BYTE-IDENTICAL. The Save dialog's default
   button is clicked, the chain runs, and after `quit` the B: image is read on
   the HOST by an independent FAT12 reader (`tools/os88disk.py --verify`, then
   the directory walked here) and both files are compared with what the server
   sent.

8. LOAD PROGRAM RUNS ONE OUT OF MEMORY. HELLO is selected and the button
   clicked; `OSAPI_PKG_RUN` (SPEC.md 21.5) copies the fetched image into a
   region of the kernel's own and a window titled HELLO appears. Nothing was
   written to a disk and nothing was read from one - the bytes came off the
   host's socket, through a claim, into a running instance.

9. AN ARCHIVE, ADDED TO DISK (SPEC.md 88.13, 88.14). The fourth record is a
   `.WPK` this file packs with `tools/os88wire.py --archive` out of a tree
   with a `home`, three folders, entries at depths 0, 1 and 2, a STORED
   entry, an LZSS entry of ~40KB, an EMPTY file and `build/hello.o88` last
   with WAH_PROGRAM. Add to Disk, walked to B: exactly as assertion 7 walks
   it, and after `quit` every file under `TREE/` on the B: image is compared
   byte for byte with the source it was packed from - folders included, by
   the FAT12 reader in this file. That is the LZSS decoder, the folder
   banking, the grouping rule and the empty-file case in one assertion, and
   nothing on the host can make it: the decoder that matters is the 8086's.

10. AN ARCHIVE, RUN FROM RAM (SPEC.md 88.14). Load Program on the same
   record. `RDPV_MOUNT` puts a store up, the instance stands on its root, the
   tree lands there and the last entry - still in the claim - goes to
   `OSAPI_PKG_RUN`. Asserted: a THIRD live volume appears in `dsk_vtab` and
   it is `DVK_FILE` - RAMDISK.DRV serves FILES and not sectors (SPEC.md
   62.9), and nothing else on this machine registers a volume - the status
   cell says `Loaded`, and a HELLO window opens.

11. THE CLIP ASSERTION (SPEC.md 88.6.1). The Wire's two buttons are drawn at
   absolute coordinates from the WAKE handler, and Load Program has just put
   a window on top of them: the Wire's content is x 61..444 and its button
   column x 217..440 at y ~194..232, while HELLO's window is 240 x 90 at
   (200, 150) - so the buttons land INSIDE HELLO's content and the overlap is
   not incidental. What the fix does is arm `OSAPI_WM_CLIP_SET` and test each
   button's rect against it, so the launched window keeps its own pixels.
   The assertion is a REPAINT COMPARISON rather than a picture of what the
   defect looks like: HELLO's content is captured as it stands after the
   launch, then HELLO is dragged 8px and back - which is a clean full repaint
   from its own W_PAINT - and captured again. The two must agree pixel for
   pixel. Without the fix the first capture carries two button-shaped blocks
   of grey frame and lettering over HELLO's white ground and the second does
   not, so the count of dark pixels in the button column alone separates
   them; with it the region is white in both. The old kernel cannot be built
   here to show the other arm, so this is stated rather than demonstrated.

**THE LAUNCH IS THE DESKTOP ZONE** (SPEC.md 26.7). `ETHER.DRV` registers it,
so the ethertest-shaped disk that already asks for the driver has the icon on
it, and this file double-clicks the icon rather than a row in a Disk window.
Three things follow from that and all three are handled below: the zone
launches out of the BOOT volume's `SYSTEM/`, so the instance's current
directory is **A:** and `WIRE.CFG` is read off A:; and the Save dialog
therefore opens on A: and has to be walked to B: with its Drive button before
the write.
"""
import argparse
import json
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import dispcp                                              # noqa: E402
import os88sym                                             # noqa: E402
import os88qemu                                            # noqa: E402
import os88geom                                            # noqa: E402
import os88wire                                            # noqa: E402

S = os88sym.linear
SOCK = "build/qmp.sock"
PORT = 8092
SYSIMG = "build/thewire360.img"
DATIMG = "build/thewiredata.img"

WS_DONE, WS_FAIL = 7, 8                 # thewire.asm's WS_*, and WS_PAUSE
                                        # (SPEC.md 88.14) went in at 6 between
                                        # WS_BODY and these two, so both moved
                                        # up. Named here rather than read out
                                        # of the source, so the reason travels
TITLE_H = 18
FD_BX1, FD_BX2, FD_BH = 224, 286, 13    # kernel/fdlg.inc's button column
FD_BY0, FD_BY2 = 20, 60                 # ...Save, and Drive two rows down
VOL_B = ord("B") - ord("A")             # a VOLUME INDEX, which is what
                                        # [disk_drive] holds since SPEC.md 52
DVK_FILE = 2                            # kernel/disk.inc: a REDIRECTED volume,
                                        # which is what a mounted RAM disk is -
                                        # os88geom mirrors DVK_BIOS and DVK_DRV
                                        # and not this one

FIXTURE = {
    "date": "20260904",
    "entries": [
        {"stem": "HELLO", "title": "Hello", "kind": 0, "tier": 0,
         "flags": {"new": True}, "files": ["hello.o88"],
         "description": "The smallest os8088 package."},
        {"stem": "MINES", "title": "Minesweeper", "kind": "game", "tier": 0,
         "files": ["mines.o88"],
         "description": "The 1990 game, in assembly."},
        {"stem": "BIGONE", "title": "Needs a disk", "kind": 0, "tier": 3,
         "files": ["hello.o88", "mines.o88"],
         "description": "Two files, so Load Program refuses it."},
        # THE ARCHIVE (SPEC.md 88.13). Tier 0 so the 8088/8086 filter keeps
        # it: assertion 4 counts what that filter shows, and a tier-3 archive
        # would leave the two assertions reading each other's fixture.
        {"stem": "TREEONE", "title": "A whole tree", "kind": 0, "tier": 0,
         "archive": {"file": "TREEONE.WPK"},
         "description": "Folders, in one stream."},
    ],
}
ARC_STEM = "TREEONE"
ARC_HOME = "TREE"                       # the folder the whole tree lands under
ARC_ROW = 3                             # ...and its row in the unfiltered list
SIDECAR = "MINES.O88"                   # what BIGONE's second file is called
                                        # in /wire/pkg/ and on the disk
PICX, PICY = 208, 21                    # WR_PICX and the picture's y in the
                                        # detail pane, both content-relative


def fixture_pic():
    """A 128 x 64 band with a pattern that cannot look right upside down.

    SPEC.md 88.3's polarity: 1 = INK. Diagonal stripes plus a solid block in
    one corner - a symmetric pattern (a checkerboard, a border) is exactly
    what an inverted blit still passes.
    """
    out = bytearray(os88wire.WIRE_PICSZ)
    for y in range(os88wire.WIRE_PICH):
        for x in range(os88wire.WIRE_PICW):
            ink = ((x + y) % 16) < 5 or (x < 24 and y < 12)
            if ink:
                out[y * os88wire.WIRE_PICB + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def fixture_tree(root):
    """The tree the .WPK is packed from, and what every file in it must hold.

    Four things are being covered and each is a different failure:
      DOCS/README.TXT   depth 1, and STORED - the method that is not a decoder
      A/0/BIG.BIN       depth 2 and ~40KB of a pattern LZSS eats, so the
                        decoder runs across forty of the worker's 1,024-byte
                        drains and pauses inside pairs
      A/1/X.COM         depth 2 again but a DIFFERENT folder, which is the
                        folder-banking path (SPEC.md 88.14)
      A/1/EMPTY.DAT     zero bytes, which completes on its entry HEADER with
                        no body at all - the one entry that never reaches the
                        decoder
      A/1/CONSOLE7.COM  a TWELVE-character name, which fills its slot with no
                        NUL (SPEC.md 88.13's late clarification)
      A/0/RANDOM[12].BIN  34,000 bytes each of INCOMPRESSIBLE noise, which
                        the writer stores - and which put the whole stream
                        over 65,536 bytes, so the dword Content-Length and
                        the 32-bit byte count are read with a non-zero high
                        word
    plus build/hello.o88 at depth 0, last and WAH_PROGRAM.
    """
    want = {}

    def put(rel, data):
        path = os.path.join(root, *rel.split("/"))
        d = os.path.dirname(path)
        if d and not os.path.isdir(d):
            os.makedirs(d)
        open(path, "wb").write(data)
        want[rel] = data

    put("DOCS/README.TXT", b"The Wire moves a tree in one stream.\r\n" * 40)
    # A RUN AND A LITERAL RUN, deliberately: a pattern with long repeats
    # exercises the pair whose distance is SHORTER than its length (88.13's
    # run), and the counter bytes between them stop the whole thing being one
    # match a broken decoder could also get right.
    big = bytearray()
    n = 0
    while len(big) < 40000:
        big += b"os8088 " * 11 + bytes([n & 0xFF, (n >> 8) & 0xFF])
        n += 1
    put("A/0/BIG.BIN", bytes(big[:40000]))
    put("A/1/X.COM", bytes(range(256)) * 3)
    # A TWELVE-CHARACTER NAME, which SPEC.md 88.13 stores with NO NUL after
    # it - `CONSOLE7.COM` is one of the five in RunCPM's master disk. The
    # slot after it is another NAME, so a reader that copied to a terminator
    # would write the file under `CONSOLE7.COM` plus whatever followed, and
    # at depth 2 the entry after this one is what follows.
    put("A/1/CONSOLE7.COM", b"MSX-DOS CONSOLE\r\n" * 7)
    # **TWO INCOMPRESSIBLE ENTRIES, TO PUT THE STREAM OVER 64KB.** LZSS makes
    # these bigger, so the writer stores them (tools/os88wire.py), and the
    # .WPK then passes 65,536 bytes on the wire - which is the only way to
    # exercise the DWORD Content-Length and the 32-bit running count with a
    # NON-ZERO HIGH WORD. A word there laps into a small plausible number
    # rather than failing, which is PERFORMANCE.md rule 3's failure mode and
    # invisible in any shorter fixture. They are 34,000 each rather than one
    # of 68,000 because WIRE_FILEMAX caps ONE entry at 63KB.
    # The generator is an LCG so the fixture is byte-for-byte reproducible.
    for which in (1, 2):
        buf, x = bytearray(), 0x1234 + which
        while len(buf) < 34000:
            x = (x * 1103515245 + 12345) & 0xFFFFFFFF
            buf.append((x >> 16) & 0xFF)
        put("A/0/RANDOM%d.BIN" % which, bytes(buf))
    put("A/1/EMPTY.DAT", b"")
    hello = open("build/hello.o88", "rb").read()
    put("HELLO.O88", hello)
    return want


def say(*a):
    print(*a)
    sys.stdout.flush()


# =============================================================================
# THE PACKAGE'S OWN BSS OFFSETS, out of its source
#
# apps/thewire declares them as `name equ os88_image_end + <expr>`, which is
# the convention every package here uses (tests/wirefps.py reads apps/wire's
# the same way) - and this evaluates the expressions rather than hard-coding
# the numbers, because a hard-coded offset is PLAUSIBLE after the bss moves and
# reads whatever landed there.
# =============================================================================
def bss_offsets():
    # THE FORMAT HEADERS COME FIRST, and they have to: the bss block sizes its
    # archive buffers out of warc.inc's WARC_* and wcat.inc's WIRE_*, so a
    # parse that read only thewire.asm evaluated every offset after the first
    # of them to nothing and dropped it - silently, which is what the guard
    # list at the bottom of this function is for.
    files = [os.path.join(ROOT, "apps", "os88api.inc")] + [
        os.path.join(ROOT, "apps", "thewire", n)
        for n in ("wcat.inc", "warc.inc", "thewire.asm")]
    consts, out = {}, {}
    equ = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s+equ\s+([^;]+?)\s*(?:;.*)?$")
    lines = []
    for f in files:
        lines += list(open(f))
    for line in lines:
        m = equ.match(line)
        if not m:
            continue
        name, expr = m.group(1), m.group(2).strip()
        if expr.startswith("os88_image_end"):
            e = expr[len("os88_image_end"):].strip().lstrip("+").strip() or "0"
            try:
                out[name] = int(eval(e, {"__builtins__": {}}, dict(consts)))
            except Exception:
                pass
        else:
            try:
                consts[name] = int(eval(expr, {"__builtins__": {}},
                                        dict(consts)))
            except Exception:
                pass
    for want in ("wr_n", "wr_state", "wr_nodrv", "wr_filter", "wr_sel",
                 "wr_grey", "wr_ox", "wr_oy", "wr_sb", "wr_catseg",
                 "wr_adone", "wr_an", "wr_aph", "wr_ramjob", "wr_msg"):
        if want not in out:
            sys.exit("thewire: %s is not in apps/thewire/thewire.asm's bss "
                     "block - the block moved and every offset this file "
                     "would read is plausible and wrong" % want)
    return out


# =============================================================================
# THE DESKTOP SERVICE ZONE (SPEC.md 26.7)
#
# Its ordinal is one past the last VOLUME zone - `DESK_SVZ` is `DVOL_MAX` and
# `desk_ord` hands it the count the volume walk just finished - so it is the
# same walk `dispcp.drive_ordinal` does, without stopping at a letter.
# dispcp.drive_xy then turns an ordinal into a point, which is the one place
# the column-and-wrap arithmetic lives.
# =============================================================================
def svc_ordinal(m):
    t = m.read(S("dsk_vtab"), dispcp.DVOL_MAX * dispcp.DV_SIZE)
    n = 0
    for v in range(dispcp.DVOL_MAX):
        r = t[v * dispcp.DV_SIZE:(v + 1) * dispcp.DV_SIZE]
        if r[dispcp.DV_KIND] == dispcp.DVK_FREE or not (r[dispcp.DV_FLAGS] & 1):
            continue
        n += 1
    return n


def live_vols(m):
    """Every LIVE volume in dsk_vtab, as (index, kind).

    A mounted RAM disk is one more of these and it is **DVK_FILE**, not
    DVK_DRV: RAMDISK.DRV serves FILES rather than sectors (SPEC.md 62.9), so
    its volume is redirected exactly as NET.DRV's is. Nothing else on this
    machine can be one - A: and B: are the BIOS floppies and the gate disk
    mounts no hard disk - so a third live volume of that kind IS the store.
    Reading the kernel's own table is what makes assertion 10 independent of
    the driver's internals: RDPV_STATE is the thing under test, so asking it
    would be asking the defendant.
    """
    t = m.read(S("dsk_vtab"), dispcp.DVOL_MAX * dispcp.DV_SIZE)
    out = []
    for v in range(dispcp.DVOL_MAX):
        r = t[v * dispcp.DV_SIZE:(v + 1) * dispcp.DV_SIZE]
        if r[dispcp.DV_KIND] == dispcp.DVK_FREE or not (r[dispcp.DV_FLAGS] & 1):
            continue
        out.append((v, r[dispcp.DV_KIND]))
    return out


def win_title(m, slot):
    """The title string of window `slot`, out of its own package's segment."""
    r = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    return (m.readseg(u16(r, W_SEG), u16(r, dispcp.W_TITLE), 16)
            .split(b"\0")[0].decode("latin1", "replace"))


def clip_check(m, mo, slot, shot, no, say):
    """SPEC.md 88.6.1: does the launched window keep its own pixels?

    `wr_onwake` draws the list, the pane, the two buttons and the status cell
    from the UI task's WAKE, and after Load Program it does so with the
    launched program's window on top of ours. Drawn at absolute coordinates
    with no region armed, the buttons landed inside that window's content and
    stayed there until it was next repainted - which the field reported as the
    Wire's buttons corrupting the program it had just loaded.

    The overlap is not incidental on this machine: the Wire's content runs to
    x 444 and its button column is x 217..440 at about y 194..232, while
    HELLO's window is 240 x 90 at (200, 150). The buttons land inside HELLO's
    content, so a Wire that draws them unclipped draws them THERE.

    What is compared is HELLO's content as it stands after the launch against
    the same content after a clean full repaint, which is what dragging the
    window 8px and back produces (SPEC.md 11.94 snaps a content origin to a
    multiple of 8, so the two positions are exact and the window comes home).
    Equal is the fix working. THE OTHER ARM CANNOT BE BUILT HERE - the kernel
    before the fix is not a knob - so what it would look like is stated: two
    button-shaped blocks of frame and lettering over HELLO's white ground in
    the first capture and not in the second, which the dark-pixel count in
    the button column alone separates.
    """
    def content(sl):
        x, y, wd, ht = dispcp.win_rect(m, S, sl)
        return x + 1, y + TITLE_H, wd - 2, ht - TITLE_H - 1

    def grab(tag):
        # **PARK THE POINTER FIRST, AT THE SAME PLACE BOTH TIMES.** The arrow
        # is drawn INTO the framebuffer (SPEC.md 7), so a screendump carries
        # it, and the drag below leaves it somewhere else - which read as 47
        # differing pixels in the shape of an arrow and was reported by the
        # first version of this assertion as the defect it exists to catch.
        # (300, 440) is desktop: below the Wire's frame and left of the zone
        # column.
        mo.run("to", "300", "440")
        time.sleep(0.6)
        png = os.path.join(tempfile.mkdtemp(), tag + ".png")
        subprocess.run(["python3", "tools/shot.py", SOCK, png],
                       check=True, capture_output=True)
        return os88wire.png_read(png)[2]

    cx, cy, cw, ch = content(slot)
    say("the launched window's content is %dx%d at (%d, %d)"
        % (cw, ch, cx, cy))
    px0 = grab("clip-a")
    shot("09-clip-a")

    tx, ty, tw, _ = dispcp.win_rect(m, S, slot)
    bar = (tx + tw // 2, ty + TITLE_H // 2)
    for dx in (8, -8):                  # out and back: two clean W_PAINTs
        mo.run("to", str(bar[0]), str(bar[1]))
        subprocess.run(["python3", "tools/qmp.py", SOCK, "mouse_button 1",
                        "sleep 0.15"], check=True, capture_output=True)
        mo.run("to", str(bar[0] + dx), str(bar[1]))
        time.sleep(0.4)
        subprocess.run(["python3", "tools/qmp.py", SOCK, "mouse_button 0"],
                       check=True, capture_output=True)
        time.sleep(1.5)
        bar = (bar[0] + dx, bar[1])
    time.sleep(1.5)
    nx, ny, nw, nh = content(slot)
    px1 = grab("clip-b")
    shot("09-clip-b")
    if (nx, ny, nw, nh) != (cx, cy, cw, ch):
        say("the window came back to (%d, %d) %dx%d and went from (%d, %d) "
            "%dx%d - comparing them by their own origins"
            % (nx, ny, nw, nh, cx, cy, cw, ch))
        cw, ch = min(cw, nw), min(ch, nh)

    def dark(px, x, y):
        r, g, b = px(x, y)
        return (r * 299 + g * 587 + b * 114) // 1000 < 128

    bad = ink0 = ink1 = 0
    for yy in range(ch):
        for xx in range(cw):
            d0 = dark(px0, cx + xx, cy + yy)
            d1 = dark(px1, nx + xx, ny + yy)
            if d0:
                ink0 += 1
            if d1:
                ink1 += 1
            if d0 != d1:
                bad += 1
    say("the launched window's content: %d dark pixels after the launch, %d "
        "after a clean repaint, %d of %d differ"
        % (ink0, ink1, bad, cw * ch))
    if bad:
        no("%d of %d pixels inside the launched window differ between the "
           "state Load Program left and a clean repaint. That is SPEC.md "
           "88.6.1: wr_onwake drew something into a window that is not ours. "
           "The dark counts are %d and %d - a surplus of a few hundred in the "
           "first is the two buttons, which land at x 217..440 inside this "
           "window's content" % (bad, cw * ch, ink0, ink1))


def zstr(m, sym, n):
    return m.read(S(sym), n).split(b"\0")[0].decode("latin1", "replace")


W_SEG = 22                              # the package's own segment, and
                                        # os88geom stops at W_TITLE


# =============================================================================
# QEMU over QMP - tests/ethernet.py's class, one connection at a time
# =============================================================================
class Qemu:
    def __init__(self, path=SOCK):
        self.path = path
        self.tmp = tempfile.mkdtemp()
        for _ in range(150):
            try:
                self.hmp("info status")
                return
            except OSError:
                time.sleep(0.2)
        raise RuntimeError("no QMP socket at %s" % path)

    def hmp(self, cmd):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.path)
        f = s.makefile("rw")

        def send(obj):
            f.write(json.dumps(obj) + "\n")
            f.flush()
            while True:
                line = f.readline()
                if not line:
                    raise RuntimeError("QMP closed")
                msg = json.loads(line)
                if "event" in msg:
                    continue
                return msg
        try:
            json.loads(f.readline())
            send({"execute": "qmp_capabilities"})
            r = send({"execute": "human-monitor-command",
                      "arguments": {"command-line": cmd}})
            return r.get("return", "")
        finally:
            f.close()
            s.close()

    def read(self, linear, n):
        p = os.path.join(self.tmp, "m.bin")
        # THE FILENAME IS QUOTED: HMP parses an unquoted /tmp/... as an
        # EXPRESSION and answers "invalid char 't'", which reads as a bad
        # address rather than a bad argument.
        self.hmp('pmemsave 0x%X %d "%s"' % (linear, n, p))
        return open(p, "rb").read()

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    # dispcp.scroll_to drives the Disk window with ARROW KEYS, and it names
    # them the way MartyPC's harness does. QMP's `sendkey` has its own names,
    # so this is the two-entry translation that lets a QEMU row use the same
    # navigation helpers every MartyPC row uses - which is the point of them.
    KEYS = {"ArrowUp": "up", "ArrowDown": "down", "Return": "ret",
            "Escape": "esc"}

    def key(self, name):
        self.hmp("sendkey " + self.KEYS.get(name, name.lower()))

    def quit(self):
        try:
            self.hmp("quit")
        except Exception:
            pass


class Mouse:
    def run(self, *args):
        subprocess.run(["python3", "tools/mouse.py", SOCK] + list(args),
                       check=True, capture_output=True)

    def click(self, x, y):
        self.run("click", str(x), str(y))
        time.sleep(0.4)

    def dblclick(self, x, y):
        # TWO `click`s ARE NOT A DOUBLE-CLICK (CLAUDE.md): the detectors
        # compare birth ticks in a 9-tick window and two processes are far too
        # slow. Position, then both presses down one QMP connection.
        self.run("to", str(x), str(y))
        subprocess.run(["python3", "tools/qmp.py", SOCK,
                        "mouse_button 1", "sleep 0.08", "mouse_button 0",
                        "sleep 0.12",
                        "mouse_button 1", "sleep 0.08", "mouse_button 0"],
                       check=True, capture_output=True)
        time.sleep(0.4)


def settle(m, card=None):
    time.sleep(2.0)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


# =============================================================================
# THE HOST'S WIRE - one catalog, two packages, and it remembers what was asked
# =============================================================================
class Server(threading.Thread):
    def __init__(self, files):
        threading.Thread.__init__(self, daemon=True)
        self.files = files              # path -> bytes
        self.asked = []
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("0.0.0.0", PORT))
        self.s.listen(4)

    def run(self):
        while True:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            c.settimeout(60)
            try:
                req = b""
                while b"\r\n\r\n" not in req and len(req) < 4096:
                    b = c.recv(1024)
                    if not b:
                        break
                    req += b
                self.asked.append(req)
                line = req.split(b"\r\n")[0].decode("latin1", "replace")
                path = line.split(" ")[1] if " " in line else ""
                body = self.files.get(path)
                if body is None:
                    c.sendall(b"HTTP/1.0 404 Not Found\r\n"
                              b"Content-Length: 0\r\n\r\n")
                else:
                    c.sendall(b"HTTP/1.0 200 OK\r\n"
                              b"Content-Type: application/octet-stream\r\n"
                              b"Content-Length: %d\r\n\r\n" % len(body) + body)
            except OSError:
                pass
            finally:
                c.close()


# =============================================================================
# AN INDEPENDENT FAT12 READER for the write-back check
#
# `tools/os88disk.py --verify` is the STRUCTURAL fsck and is run first; this
# walks the root directory for the two names and pulls their cluster chains,
# so what the assertion compares is bytes off the image and not the guest's
# opinion of them.
# =============================================================================
def fat12_files(path):
    img = open(path, "rb").read()
    bps = u16(img, 11)
    spc = img[13]
    res = u16(img, 14)
    nfat = img[16]
    nroot = u16(img, 17)
    spf = u16(img, 22)
    root = (res + nfat * spf) * bps
    data = root + nroot * 32
    fat = img[res * bps:(res + spf) * bps]

    def nxt(c):
        o = c + (c >> 1)
        v = u16(fat, o)
        return (v >> 4) if (c & 1) else (v & 0xFFF)

    def chain(c, size=None):
        blob = b""
        while 2 <= c < 0xFF8 and (size is None or len(blob) < size):
            off = data + (c - 2) * spc * bps
            blob += img[off:off + spc * bps]
            c = nxt(c)
        return blob if size is None else blob[:size]

    out = {}

    def walk(entries, prefix):
        for i in range(0, len(entries) - 31, 32):
            e = entries[i:i + 32]
            if e[0] in (0x00, 0xE5) or e[11] & 0x08:
                continue
            name = e[0:8].decode("latin1").rstrip()
            ext = e[8:11].decode("latin1").rstrip()
            if name in (".", ".."):
                continue
            nm = prefix + name + ("." + ext if ext else "")
            c = u16(e, 26)
            if e[11] & 0x10:            # a folder, and the Save dialog OPENS
                                        # IN ONE: SPEC.md 38.10 starts it in
                                        # MEDIA, so the write lands there and
                                        # a reader that only knew the root
                                        # would report it as never written.
                                        # **AND IT RECURSES ALL THE WAY** now:
                                        # an archive's tree is three deep
                                        # (SPEC.md 88.13's WARC_DEPTH) and a
                                        # one-level walk would report every
                                        # file under A/0/ as missing
                if nm.count("/") <= 4:
                    walk(chain(c), nm + "/")
                continue
            out[nm.upper()] = chain(c, struct.unpack_from("<I", e, 28)[0])

    walk(img[root:root + nroot * 32], "")
    return out


# =============================================================================
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave QEMU running for a look afterwards")
    ap.add_argument("--shots", metavar="DIR", default=None,
                    help="write a screenshot per step into DIR")
    a = ap.parse_args()
    fails = []

    def no(msg):
        fails.append(msg)
        say("FAIL: " + msg)

    off = bss_offsets()

    # **THE DISKS ARE REBUILT, NOT CHECKED.** QEMU mounts both writable and
    # assertion 6 WRITES to the data disk, so a run that inherited the last
    # run's B: would find the two files already there and pass having proved
    # nothing. Staleness is not the hazard here - a dirty image is - and
    # `make` cannot see the difference, because the guest's write leaves the
    # image NEWER than everything it was built from (tests/ethernet.py's note,
    # one disk along).
    for f in (SYSIMG, DATIMG):
        if os.path.exists(f):
            os.remove(f)
    r = subprocess.run(["make", "thewiretest"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("thewire: make thewiretest failed:\n" + r.stdout + r.stderr)

    # --- the fixture the host will serve ------------------------------------
    tmp = tempfile.mkdtemp()
    man = os.path.join(tmp, "wire.json")
    json.dump(FIXTURE, open(man, "w"))
    pics = os.path.join(tmp, "pic")
    os.makedirs(pics, exist_ok=True)
    pic = fixture_pic()
    open(os.path.join(pics, "HELLO.PIC"), "wb").write(pic)
    # THE ARCHIVE IS PACKED BEFORE THE CATALOG IS, and it has to be: every
    # figure in a WF_ARC record - WC_SIZE, WC_TOTAL, WC_NEEDKB and the icon -
    # comes out of the .WPK's own header rather than out of the manifest
    # (tools/os88wire.py), which is the same rule the machine checks the other
    # way round in wr_ahdrck.
    src = os.path.join(tmp, "tree")
    os.makedirs(src, exist_ok=True)
    tree = fixture_tree(src)
    try:
        wpk = os88wire.archive(src, home=ARC_HOME, program="HELLO.O88")
    except os88wire.Refused as e:
        sys.exit("thewire: the fixture tree will not pack: %s" % e)
    open(os.path.join("build", ARC_STEM + ".WPK"), "wb").write(wpk)
    say("archive %s: %d bytes on the wire, %d entries, %d files unpacked"
        % (ARC_STEM + ".WPK", len(wpk), len(tree), len(tree)))
    if len(wpk) <= 0xFFFF:
        sys.exit("thewire: the fixture archive is %d bytes and the whole "
                 "point of the two incompressible entries is to pass 65,536 "
                 "- without that the dword Content-Length and the 32-bit "
                 "byte count are never read with a high word in them"
                 % len(wpk))
    try:
        cat = os88wire.pack(json.load(open(man)), "build", pics)
    except os88wire.Refused as e:
        sys.exit("thewire: the fixture will not pack: %s" % e)
    hello = open("build/hello.o88", "rb").read()
    mines = open("build/mines.o88", "rb").read()
    served = {"/wire/catalog.bin": cat,
              "/wire/pkg/HELLO.O88": hello,
              "/wire/pkg/MINES.O88": mines,
              "/wire/pkg/BIGONE.O88": hello,
              "/wire/pkg/%s.WPK" % ARC_STEM: wpk,
              "/wire/pic/HELLO.PIC": pic}
    srv = Server(served)
    srv.start()
    say("http server on 10.0.2.2:%d, catalog %d bytes, %d records"
        % (PORT, len(cat), len(FIXTURE["entries"])))

    # A SURVIVOR KEEPS THE SOCKET, so the new machine cannot bind and every
    # read below would come from the OLD one - which reads as a change that
    # did nothing (CLAUDE.md). Kill it by PID out of the pidfile and never
    # with `pkill -f qemu`, whose pattern matches the calling shell.
    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)
    os88qemu.own()
    r = subprocess.run(["make", "test", "ETHER=1", "TESTIMG=" + SYSIMG,
                        "TESTAPPS=" + DATIMG], capture_output=True, text=True)
    if r.returncode:
        sys.exit("thewire: make test failed:\n" + r.stdout + r.stderr)

    m = Qemu()
    mo = Mouse()
    img = os.path.getsize("build/thewire.bin")

    def shot(tag):
        if a.shots:
            os.makedirs(a.shots, exist_ok=True)
            subprocess.run(["python3", "tools/shot.py", SOCK,
                            os.path.join(a.shots, tag + ".png")], check=True)

    try:
        # --- 0: the zone exists, and it is the Wire's ----------------------
        # ETHER.DRV registers it (SPEC.md 26.7), so a non-zero desk_svc_seg is
        # also the proof that the driver attached at all - and the caption and
        # the file are read back because a zone that launched something else
        # would open a window this test would then measure.
        seg = 0
        for _ in range(60):
            seg = u16(m.read(S("desk_svc_seg"), 2))
            if seg:
                break
            time.sleep(0.5)
        cap = zstr(m, "desk_svc_cap", 12)
        fil = zstr(m, "desk_svc_file", 13)
        say("service zone: driver %04X, caption %r, launches %r"
            % (seg, cap, fil))
        if not seg:
            m.quit()
            sys.exit("thewire: no desktop service zone - ETHER.DRV never "
                     "attached, or SYSTEM.CFG did not ask for it")
        if cap != "Wire":
            no("the zone's caption is %r and the brand is 'Wire'" % cap)
        if fil != "THEWIRE.O88":
            no("the zone launches %r" % fil)

        wins = dispcp.win_list(m, S)
        zx, zy = dispcp.drive_xy(m, S, svc_ordinal(m))
        say("double-clicking the Wire zone at (%d, %d)" % (zx, zy))
        mo.dblclick(zx, zy)
        settle(m)
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            shot("00-zone")
            m.quit()
            sys.exit("thewire: the Wire zone opened no window")
        ww = wins2[-1]
        rec = m.read(S("wm_wins") + ww * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = u16(rec, 22)
        say("The Wire at %04X, image %d bytes" % (pseg, img))

        def b(name, n=1):
            return m.readseg(pseg, img + off[name], n)

        def w(name):
            return u16(b(name, 2))

        # --- 1: the catalog arrives and is understood -----------------------
        state = n = 0
        for _ in range(80):
            time.sleep(0.5)
            state, n = b("wr_state")[0], w("wr_n")
            if n or state == WS_FAIL:
                break
        say("wr_state = %d, wr_n = %d, wr_nodrv = %d, wr_catseg = %04X"
            % (state, n, b("wr_nodrv")[0], w("wr_catseg")))
        shot("01-catalog")
        if b("wr_nodrv")[0]:
            no("wr_nodrv is set: net_find or NETV_STATE refused, so the card "
               "or the driver never came up and nothing below is about the "
               "Wire")
        if state == WS_FAIL:
            no("wr_state is WS_FAIL: the fetch failed and the package knows")
        elif state != WS_DONE:
            no("wr_state is %d and not WS_DONE - the fetch never completed"
               % state)
        if n != 4:
            no("wr_n is %d and the fixture catalog has 4 records: either it "
               "did not arrive or wr_catck refused a field. **THE FOURTH IS "
               "AN ARCHIVE OF %d BYTES**, and a reader that bounds every "
               "record's WC_SIZE to 16 bits and WIRE_FILEMAX refuses it and "
               "takes the WHOLE CATALOG down with it - which is how the "
               "site's own catalog was found broken (SPEC.md 88.2's "
               "WIRE_ARCMAX)" % (n, len(wpk)))

        # --- 2: what the host was actually asked for ------------------------
        first = srv.asked[0] if srv.asked else None
        say("server saw: %r" % (first.split(b"\r\n")[0] if first else None))
        if first is None:
            no("nothing reached the host at all")
        else:
            if not first.startswith(b"GET /wire/catalog.bin HTTP/1.0\r\n"):
                no("the first request line is %r, and WIRE.CFG named "
                   "10.0.2.2:8092/wire/" % first.split(b"\r\n")[0])
            if b"Host: 10.0.2.2" not in first:
                no("no Host: header in the request")
            if b"User-Agent: os8088 Wire 1.0" not in first:
                no("no User-Agent: the Wire identifies itself (SPEC.md 88.4)")

        # --- 3: the list is the catalog -------------------------------------
        # [wr_sb+8] is the scroll block's `total`, which the painter fills from
        # the machine's own wr_count. Reading it is reading what the LIST says
        # rather than what the catalog holds.
        def shown():
            return u16(m.readseg(pseg, img + off["wr_sb"] + 8, 2))

        say("the list shows %d of %d" % (shown(), n))
        if shown() != 4:
            no("the list shows %d rows over a 4-record catalog" % shown())

        ox, oy = w("wr_ox"), w("wr_oy")
        say("content origin (%d, %d)" % (ox, oy))
        if ox % 8:
            no("the content origin's x is %d, which is not a multiple of 8 - "
               "SPEC.md 11.94 snaps it, and every 8-aligned pen in the "
               "package is derived from that" % ox)

        # --- 4: the filter filters ------------------------------------------
        mo.click(ox + 96 + 6, oy + 2 + 6)        # the `8088/8086` radio
        time.sleep(1.0)
        shot("02-filter")
        say("wr_filter = %d, the list shows %d" % (b("wr_filter")[0], shown()))
        if b("wr_filter")[0] != 1:
            no("wr_filter is %d after clicking the 8088/8086 radio"
               % b("wr_filter")[0])
        if shown() != 3:
            no("the 8088/8086 filter shows %d rows and the fixture has three "
               "tier-0 entries" % shown())

        mo.click(ox + 48 + 6, oy + 2 + 6)        # ...and back to All
        time.sleep(1.0)
        if shown() != 4:
            no("back on All the list shows %d rows" % shown())

        def rect(name):
            """One of the Wire's own button rects, absolute, out of its bss.

            **NOT A COORDINATE THIS FILE COMPUTES.** wr_geom fills wr_ra and
            wr_rb and os88ui_btn draws from them, so reading them is reading
            the four words the painter used - os88ui.inc's own "GEOMETRY IS A
            POINTER" discipline, from the outside. A y derived here instead
            missed the Load Program button by nothing visible and reported
            OSAPI_PKG_RUN as broken.
            """
            return [u16(m.readseg(pseg, img + off[name] + i * 2, 2))
                    for i in range(4)]

        def press(name):
            r = rect(name)
            mo.click((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)

        def settled():
            """Wait until no transfer is in flight and no chain is running.

            A selection change starts a PICTURE fetch of its own (SPEC.md
            88.8), and the predicate refuses both buttons while one is in
            flight - correctly. A gate that clicks before it lands is testing
            the refusal, and quietly.
            """
            for _ in range(80):
                if (b("wr_state")[0] in (0, WS_DONE, WS_FAIL)
                        and b("wr_job")[0] == 0):
                    return True
                time.sleep(0.5)
            return False

        # --- 6: the picture, pixel for pixel --------------------------------
        # HELLO is row 0 and carries WF_PIC, so selecting it starts a second
        # transfer of its own - which is also the one place the generation
        # counter is exercised by an ordinary click (SPEC.md 88.5).
        mo.click(ox + 40, oy + 19 + 8)
        for _ in range(40):
            time.sleep(0.5)
            if b("wr_picok")[0]:
                break
        say("wr_picok = %d after selecting HELLO" % b("wr_picok")[0])
        if not b("wr_picok")[0]:
            no("the picture never arrived: WF_PIC is set on HELLO and "
               "/wire/pic/HELLO.PIC was served")
        elif "/wire/pic/HELLO.PIC" not in b"".join(srv.asked).decode(
                "latin1", "replace"):
            no("the host was never asked for /wire/pic/HELLO.PIC")
        else:
            time.sleep(1.0)
            shot("06-picture")
            png = os.path.join(tempfile.mkdtemp(), "pic.png")
            subprocess.run(["python3", "tools/shot.py", SOCK, png],
                           check=True, capture_output=True)
            _, _, px = os88wire.png_read(png)
            bad = 0
            for y in range(os88wire.WIRE_PICH):
                for x in range(os88wire.WIRE_PICW):
                    bit = (pic[y * os88wire.WIRE_PICB + (x >> 3)]
                           >> (7 - (x & 7))) & 1
                    r, g, bl = px(ox + PICX + x, oy + PICY + y)
                    dark = (r * 299 + g * 587 + bl * 114) // 1000 < 128
                    if dark != bool(bit):
                        bad += 1
            say("the picture on the glass differs from the file in %d of %d "
                "pixels" % (bad, os88wire.WIRE_PICW * os88wire.WIRE_PICH))
            if bad:
                no("%d of %d picture pixels are wrong. All 8,192 wrong is the "
                   "INVERSION going the other way (SPEC.md 88.3), which draws "
                   "a plausible picture in negative; a few hundred is the "
                   "block landing at the wrong x or y" % (bad, 8192))

        # --- 5: the predicate greys the right button ------------------------
        # Row 2 is BIGONE: tier 3, two files, so WF_DISK. Load Program must
        # refuse with 'Needs its files on a disk' and Add to Disk must not.
        mo.click(ox + 40, oy + 19 + 2 * 16 + 8)
        time.sleep(2.0)
        shot("03-selected")
        sel, grey = w("wr_sel"), b("wr_grey")[0]
        say("wr_sel = %d, wr_grey = %d" % (sel, grey))
        if sel != 2:
            no("wr_sel is %d after clicking the third row" % sel)
        if not grey & 1:
            no("Load Program is NOT greyed on a WF_DISK record: SPEC.md 88.7 "
               "refuses it because OSAPI_PKG_RUN would run it with its "
               "overlay nowhere (wr_grey = %d)" % grey)
        if grey & 2:
            no("Add to Disk is greyed on a WF_DISK record, and Add to Disk is "
               "exactly what such a record is FOR (wr_grey = %d)" % grey)

        def save_to_b(tag, base):
            """Add to Disk..., walk the Save dialog to B:, and press Save.

            THE DIALOG OPENS ON THE VOLUME THE WIRE WAS LAUNCHED FROM, which
            since the zone landed is A: - the boot disk, whose SYSTEM/ the
            zone reads the package out of. Its Drive button steps to the next
            LIVE volume and wraps (SPEC.md 38.11), so this walks rather than
            assuming one click is enough: a machine that mounted a hard disk
            would need two, and [disk_drive] is the machine's own answer. The
            second call finds it already on B:, which the loop reads and
            leaves alone.
            """
            settled()
            press("wr_rb")                       # Add to Disk...
            time.sleep(2.0)
            shot(tag + "-savedlg")
            wins = dispcp.win_list(m, S)
            if len(wins) <= len(base):
                no("Add to Disk... put up no Save dialog")
                return False
            dx, dy = dispcp.win_rect(m, S, wins[-1])[:2]
            bx = dx + 1 + (FD_BX1 + FD_BX2) // 2
            for _ in range(dispcp.DVOL_MAX):
                if m.read(S("disk_drive"), 1)[0] == VOL_B:
                    break
                mo.click(bx, dy + TITLE_H + FD_BY2 + FD_BH // 2)
                time.sleep(1.2)
            vol = m.read(S("disk_drive"), 1)[0]
            say("the Save dialog is on volume %d (B: is %d)" % (vol, VOL_B))
            shot(tag + "-onB")
            if vol != VOL_B:
                no("the Save dialog would not walk to B:, so the write below "
                   "is about the wrong disk")
                return False
            mo.click(bx, dy + TITLE_H + FD_BY0 + FD_BH // 2)
            time.sleep(2.0)
            for _ in range(240):
                time.sleep(0.5)
                if b("wr_job")[0] == 0 and b("wr_state")[0] in (WS_DONE,
                                                               WS_FAIL):
                    break
            shot(tag + "-added")
            say("after the chain: wr_job = %d, wr_state = %d, wr_msg = %04X"
                % (b("wr_job")[0], b("wr_state")[0], w("wr_msg")))
            return b("wr_state")[0] != WS_FAIL

        def raise_wire():
            """Click a dead spot in the Wire's content, which raises it.

            Everything after a Load Program has ANOTHER window on top, and a
            click that lands on that window is a click the Wire never sees -
            which reads as the button doing nothing. (ox + 4, oy + 4) is
            inside the filter row's band and to the left of every radio, so
            wr_filhit finds no hit and the whole of the click is the raise.
            It is also outside HELLO's 240 x 90 at (200, 150), so it cannot
            be swallowed by the very window it exists to get out from under.
            """
            mo.click(ox + 4, oy + 4)
            time.sleep(1.0)

        def pick(row):
            raise_wire()
            mo.click(ox + 40, oy + 19 + row * 16 + 8)
            time.sleep(1.5)
            settled()

        # --- 6: Add to Disk writes both files -------------------------------
        save_to_b("04", wins2)

        # --- 8: Load Program, out of memory (SPEC.md 21.5, 88.8) -----------
        if have_pkg_run():
            mo.click(ox + 40, oy + 19 + 0 * 16 + 8)   # HELLO: tier 0, one
            time.sleep(1.5)                           # file, so both allowed
            if not settled():
                no("the picture fetch that selecting HELLO starts never "
                   "settled, so Load Program would refuse with 'A transfer "
                   "is already running' and this would be testing that")
            say("before Load: sel=%d grey=%d state=%d job=%d"
                % (w("wr_sel"), b("wr_grey")[0], b("wr_state")[0],
                   b("wr_job")[0]))
            if b("wr_grey")[0] & 1:
                no("Load Program is greyed on HELLO, which is tier 0 with one "
                   "file and no WF_DISK - the click below would be refused")
            before = dispcp.win_list(m, S)
            press("wr_ra")                            # Load Program
            for _ in range(60):
                time.sleep(0.5)
                if len(dispcp.win_list(m, S)) > len(before):
                    break
            after = dispcp.win_list(m, S)
            # **AND SAY WHICH SIDE FAILED.** [wr_job] still WJ_LOAD with the
            # transfer settled and the claim still held means the UI task went
            # into OSAPI_PKG_RUN and did not come out - the Wire has done its
            # whole half and the loader has the machine. Reported as that,
            # with the CPU's own registers, rather than as "no window
            # appeared", which reads as the package's fault.
            if (b("wr_job")[0] == 1 and b("wr_fseg", 2) != b"\0\0"
                    and b("wr_state")[0] in (WS_DONE, WS_FAIL)):
                say("WEDGED INSIDE OSAPI_PKG_RUN - wr_job is still WJ_LOAD, "
                    "the transfer settled at state %d and the claim %04X is "
                    "still held, so wr_pkgrun never returned."
                    % (b("wr_state")[0], w("wr_fseg")))
                for ln in m.hmp("info registers").splitlines()[:5]:
                    say("    " + ln.strip())
            shot("06-loaded")
            titles = []
            for slot in after:
                r = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE,
                           dispcp.WIN_SIZE)
                ttl = u16(r, dispcp.W_TITLE)
                titles.append(m.readseg(u16(r, W_SEG), ttl, 16)
                              .split(b"\0")[0].decode("latin1", "replace"))
            say("windows now: %r, wr_msg = %04X"
                % (titles, w("wr_msg")))
            asked = b"".join(srv.asked).decode("latin1", "replace")
            if "/wire/pkg/HELLO.O88" not in asked:
                no("the host was never asked for /wire/pkg/HELLO.O88, so "
                   "Load Program never started a transfer at all")
            if len(after) <= len(before):
                no("Load Program opened no window: OSAPI_PKG_RUN refused, or "
                   "the wake never ran the image")
            elif "HELLO" not in [t.upper() for t in titles]:
                no("Load Program opened a window and it is not HELLO's: %r"
                   % titles)
            else:
                say("HELLO is running, and nothing it needs was ever on a "
                    "disk: the image came off the host's socket, through a "
                    "claim, into OSAPI_PKG_RUN")
                # --- 11: THE CLIP ASSERTION (SPEC.md 88.6.1) ---------------
                # HERE rather than after the archive's launch, and it is the
                # same code path: what 88.6.1 is about is `wr_onwake` drawing
                # from the UI task's WAKE with the launched window on top of
                # ours, which is what has just happened. Doing it on the
                # PLAIN Load Program also keeps the assertion standing when
                # the archive's own launch cannot run.
                clip_check(m, mo, after[-1], shot, no, say)
        else:
            no("apps/os88api.inc carries no OSAPI_PKG_RUN, so this build has "
               "no kernel half - the assertion above cannot run and its "
               "absence must not read as a pass")

        # --- 9: AN ARCHIVE, ADDED TO DISK (SPEC.md 88.13, 88.14) ------------
        # The read-back is at the bottom of this file with assertion 7's; what
        # is asserted here is that the machine got through the whole chain -
        # one transfer, an unpacker paused at every entry boundary, and five
        # files written into three folders it made as it met them.
        pick(ARC_ROW)
        say("archive row: wr_sel = %d, wr_grey = %d" % (w("wr_sel"),
                                                        b("wr_grey")[0]))
        if w("wr_sel") != ARC_ROW:
            no("wr_sel is %d after clicking the archive's row" % w("wr_sel"))
        if b("wr_grey")[0] & 2:
            no("Add to Disk is greyed on a WF_ARC record. The writer sets "
               "WF_FLOPPY with WF_ARC on purpose (SPEC.md 88.2) and a reader "
               "that knows bit 4 must ignore bit 3 on it - this is that "
               "reader failing to (wr_grey = %d)" % b("wr_grey")[0])
        arc_added = save_to_b("07", dispcp.win_list(m, S))
        if not arc_added:
            no("the archive chain ended in WS_FAIL: wr_msg = %04X, and the "
               "candidates are wr_s_badarc (a field wr_ahdrck or wr_aentck "
               "refused), wr_s_lost (the stream ended short of its N "
               "entries) or wr_s_wrote (a file or a folder the disk refused)"
               % w("wr_msg"))

        # --- 10: AN ARCHIVE, RUN FROM RAM (SPEC.md 88.14) -------------------
        vols0 = live_vols(m)
        say("live volumes before Load Program: %r" % (vols0,))
        pick(ARC_ROW)
        if b("wr_grey")[0] & 1:
            no("Load Program is greyed on the archive: the predicate refused "
               "the RAM disk (SPEC.md 88.7's three reasons), so either "
               "RAMDISK.DRV is not up - build/wirecfg/SYSTEM.CFG asks for it "
               "with BIT_RAM - or RDPV_STATE answered a store it could not "
               "make. wr_grey = %d" % b("wr_grey")[0])
        before = dispcp.win_list(m, S)
        press("wr_ra")                            # Load Program
        for _ in range(240):
            time.sleep(0.5)
            if (b("wr_job")[0] == 0
                    and b("wr_state")[0] in (WS_DONE, WS_FAIL)):
                break
        time.sleep(2.0)
        shot("08-fromram")
        after = dispcp.win_list(m, S)
        vols1 = live_vols(m)
        say("live volumes after: %r, windows %d -> %d, wr_state = %d"
            % (vols1, len(before), len(after), b("wr_state")[0]))
        asked = b"".join(srv.asked).decode("latin1", "replace")
        if "/wire/pkg/%s.WPK" % ARC_STEM not in asked:
            no("the host was never asked for /wire/pkg/%s.WPK, so no archive "
               "transfer started at all" % ARC_STEM)
        new_vols = [v for v in vols1 if v not in vols0]
        if not new_vols:
            no("no new volume appeared: RDPV_MOUNT never put a store up, so "
               "the tree went nowhere (SPEC.md 88.14 step 3)")
        elif new_vols[0][1] != DVK_FILE:
            no("the new volume is kind %d and a RAM disk is DVK_FILE = %d - "
               "nothing else on this machine registers a volume"
               % (new_vols[0][1], DVK_FILE))
        else:
            say("a store is mounted: volume %d, DVK_FILE" % new_vols[0][0])
        if len(after) <= len(before):
            no("Load Program on the archive opened no window, and the tree "
               "carries HELLO.O88 last with WAH_PROGRAM (SPEC.md 88.14)")
        else:
            ttl = win_title(m, after[-1])
            say("the launched window is %r, wr_msg = %04X" % (ttl, w("wr_msg")))
            if ttl.upper() != "HELLO":
                no("the archive launched %r and its program entry is "
                   "HELLO.O88" % ttl)
            msg = m.readseg(pseg, w("wr_msg"), 48).split(b"\0")[0]
            say("the status cell says %r" % msg.decode("latin1", "replace"))
            if not msg.startswith(b"Loaded"):
                no("the status cell says %r and SPEC.md 88.14 says `Loaded "
                   "<title> from the Wire`" % msg)
    finally:
        if not a.keep:
            m.quit()
            time.sleep(1.5)

    # --- assertion 6's other half, on the host ------------------------------
    r = subprocess.run(["python3", "tools/os88disk.py", "--verify", DATIMG],
                       capture_output=True, text=True)
    say((r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr)
        else "")
    if r.returncode:
        no("the data disk does not pass os88disk.py --verify after the write")
    got = fat12_files(DATIMG)
    say("B: now holds %r" % sorted(got))
    # THE PATH IS NOT ASSERTED, only the NAME. The Standard File dialog opens
    # in MEDIA (SPEC.md 38.10) and the user can walk anywhere from there, so
    # where a file lands is the DIALOG's answer and not the Wire's - what the
    # Wire owes is that both files went to the SAME folder under the right
    # names with the right bytes.
    where = None
    for name, want in (("BIGONE.O88", hello), (SIDECAR, mines)):
        hit = [k for k in got if k.split("/")[-1] == name]
        if not hit:
            no("%s is nowhere on B: - Add to Disk did not write it" % name)
            continue
        k = hit[0]
        if where is None:
            where = k.rsplit("/", 1)[0] if "/" in k else ""
        elif (k.rsplit("/", 1)[0] if "/" in k else "") != where:
            no("%s landed in %r and the .O88 landed in %r - a sidecar has to "
               "go beside its package or the package cannot find it" % (k, where))
        if got[k] != want:
            no("%s on B: is %d bytes and the host sent %d, or the bytes differ"
               % (k, len(got[k]), len(want)))
        else:
            say("%s: %d bytes, byte-identical to what the host sent"
                % (k, len(want)))

    # --- assertion 9's other half: the whole tree, off the image -----------
    # THE PATH IS MATCHED FROM `TREE/` DOWN and not from the root, for
    # assertion 7's reason one level deeper: the Save dialog decides where the
    # home folder lands and the archive decides everything under it, so what
    # the Wire owes is the SHAPE of the tree and the bytes in it.
    got_tree = {}
    for k, v in got.items():
        parts = k.split("/")
        if ARC_HOME in parts:
            got_tree["/".join(parts[parts.index(ARC_HOME) + 1:])] = v
    say("%s/ on B: holds %r" % (ARC_HOME, sorted(got_tree)))
    if not got_tree:
        no("nothing landed under %s/ on B: - Add to Disk on the archive wrote "
           "no tree at all (SPEC.md 88.13's home folder)" % ARC_HOME)
    for rel, want in sorted(tree.items()):
        blob = got_tree.get(rel.upper())
        if blob is None:
            no("%s/%s is nowhere on B:. Its folders are made as the entries "
               "are met (SPEC.md 88.14), so a missing file at depth 2 is the "
               "folder walk and a missing one at depth 0 is the chain ending "
               "early" % (ARC_HOME, rel))
        elif blob != want:
            first = next((i for i in range(min(len(blob), len(want)))
                          if blob[i] != want[i]), min(len(blob), len(want)))
            no("%s/%s is %d bytes on B: and the source is %d; they first "
               "differ at byte %d. A length that is right with bytes that are "
               "wrong is the LZSS decoder (SPEC.md 88.13's distance, length "
               "or the one-byte-at-a-time copy that makes a run); a length "
               "that is short is the entry ending early"
               % (ARC_HOME, rel, len(blob), len(want), first))
        else:
            say("%s/%s: %d bytes, byte-identical to what was packed"
                % (ARC_HOME, rel, len(want)))

    say("thewire: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


def have_pkg_run():
    """Has the kernel half landed? The SDK is the one place that says so."""
    src = open(os.path.join(ROOT, "apps", "os88api.inc")).read()
    return "%define OSAPI_PKG_RUN" in src


if __name__ == "__main__":
    sys.exit(main())
