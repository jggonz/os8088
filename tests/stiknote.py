#!/usr/bin/env python3
"""SPEC.md 110.13: Sticky Note - a pad on the desk over a folder of .TXT
notes, with Note Pad (by association) as the editor.

    make && python3 tests/stiknote.py [--leg herc|vga|small]

The SHIPPED 360KB system disk copied to a scratch file, on MartyPC. Every
coordinate is the package's own, out of tools/os88stik.py's STIKNOTE_GEO,
which t_stik holds to snp_geos; every bss field is os88stik's STIKNOTE_BSS_*,
which t_stik holds to the build; the notes and the state file are flushed off
the emulated drive and read by os88flush and tools/os88stik.py.

herc (default) - a 5150 with a Hercules card: the MID layout, 1bpp pixels
  A  launched from A:/STIKAPPS, the pad is a DESK window of the MID layout
     reading SYSTEM/APPDATA/NOTES: one note, WELCOME.TXT, its head the file's
     first bytes, and the first beat writes STIKNOTE.DAT (payload v2) naming
     it current.
  B  THE STACK: the sheet's top edge is ink across the sheet and the stack's
     right edge down the card; the two notches are the DESKTOP's dither, not
     paper; the turned corner's diagonal is ink and the corner past it paper.
  C  THE WATCH IS SILENT while nothing is armed: three beats with a
     breakpoint on osapi_sys_snapshot_x, and it is never reached.
  D  a press on a box ticks it: the box is the TICKED record, the file on the
     disk has `[x] ` where `[ ] ` was, and the tick's time on the 4.77 MHz XT
     is printed.
  E  the arrow opens the note in NOTE PAD (a new instance, and the watch is
     armed); typed text and Ctrl+S, then Note Pad's close: the watch disarms
     within a beat and the pad shows the saved text; the re-read is timed.
  E2 ANOTHER os8088 DISK IS NOT HOME: a TWIN of home in A: - home as the
     guest has it, with a serial of its own and STIKAPPS\\STIKNOTE.O88
     renamed, so NOTES and the very note on the pad are where home's are -
     the motor let stop, and a tick over NOTES' bank (SPEC.md 110.13.2)
     reads that disk and writes NOTHING, the pad unchanged: the quiet home
     stand mounts it as itself and the witness refuses it (110.5.1). Then
     home goes back. (The apps disk, which shares nothing, is stikswap's.)

  THE TIMES (D, E, F) are a press - or, for E, the editor's window gone - to
  the END of the visit: the first stand after its first file cell, which is
  stik_leave going home, off the kernel's own breakpoints (VISIT). The
  pointer's travel and the double-click gap are spent before the clock
  starts and the floppy motor has stopped, as between two human presses.
  F  `+` makes NOTE0001.TXT (`New note` + CR LF), it is current, and it opens
     in Note Pad; `<` turns back to WELCOME.TXT, timed on the 4.77 MHz XT.
  G  Delete is the press-again latch: the first says `Delete again to
     confirm` and deletes nothing, the second deletes the CURRENT note's file.
  H  the Color menu is GREYED on this 1bpp screen (SPEC.md 47), every item
     saying why.

vga - an XT with a VGA: the TALL layout, the papers and a full folder
  I  the pad's sheet is YELLOW (the default paper) and its edge black.
  J  Color > Pink: the sheet is pink and STIKNOTE.DAT's table says
     WELCOME pink; the pad closed and launched again is STILL pink.
  K  the folder holds 30 notes (29 empty ones put in its directory on the
     host): the pad counts 30, and `+` answers `Notes folder is full` and
     makes nothing.

small - kern_small, a copy of STIKNOTE.O88 carried there by hand (B:)
  L  it refuses to launch, in words: `No desk layer (DESK.DRV)` - the
     STIK_DESKONLY build (the plan's decision F); the shipped small disks do
     not carry it (SPEC.md 24.5.1). Needs `make small`.

**BREAK IT ON PURPOSE**: make sn_tick poll without `[sn_wslot]` (drop its
`jz .x`) and C goes red; let stik_enter go to a banked folder before its
witness and E2 goes red (the tick writes onto the apps disk); take sn_go out of sn_launch and E's Note Pad opens
an empty note in STIKAPPS\\ and the pad never shows `Hi`; drop sn_setpen's
call from snp_pcard and I reads white; draw the notches as paper (snp_put's
titled arm) and B fails.
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

LEG = sys.argv[sys.argv.index("--leg") + 1] if "--leg" in sys.argv else "herc"
if LEG == "small":
    # os88sym must resolve against kern_small's map (tests/paint1small.py's
    # pattern) - set before anything imports it
    import os88build as _B                                # noqa: E402
    _B.use_build("build/smallk")
    os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")

import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88geom as geom   # noqa: E402
import os88flush   # noqa: E402
import os88stik    # noqa: E402

BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TITLE = "Sticky Note"
S = os88stik
STIK_S_DIRTY = 16
NOTES = S.STIKNOTE_DIR
TICK9 = (0xFF80, 0x8080, 0x8280, 0x8680, 0xCC80, 0xB880, 0x9080, 0x8080,
         0xFF80)                # snpad.inc's sn_box9x, MID's ticked box
YELLOW, PINK, BLACK = (255, 255, 85), (255, 85, 255), (0, 0, 0)
BEAT = 36 / 18.2                # STIK_TICKS, in guest seconds

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def image_size():
    b = open(os.path.join(BDIR, "stiknote.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]


def cycles(m):
    return int(m.status().get("cycles", 0))


VISIT = ("osapi_file_goto", "osapi_file_goto_qm", "dwf_dskw_read",
         "dwf_dskw_read_at", "dwf_dskw_write")


def visit(hits, c0, after):
    """(c0 to the visit's end, the visit's own length), GUEST seconds. A
    visit to NOTES starts at its first stand after `after` (OSAPI_FILE_GOTO
    or _QM, whichever stik_enter makes) and ends at the first stand AFTER
    its first file cell - stik_leave going home, which every one of the
    three operations makes once its read or write is done. A save the beat
    owes comes later, and is not counted."""
    hz = os88marty.GUEST_HZ
    t0 = io = None
    for h in hits:
        if h["hit"] < after:
            continue
        goto = h["name"].startswith("osapi_file_goto")
        if t0 is None:
            if goto:
                t0 = h["hit"]
        elif io is None:
            if not goto:
                io = h["hit"]
        elif goto:
            return (h["hit"] - c0) / hz, (h["hit"] - t0) / hz
    raise SystemExit("no whole visit in the trace: %r" % (
        [(h["name"], h["hit"] - c0) for h in hits],))


def twin(src, tmp):
    """ANOTHER os8088 disk laid out like home: `src` (home as the guest has
    it) with a serial of its own (BS_VolID, +0x27) and STIKAPPS\\STIKNOTE.O88
    renamed - so NOTES and the very WELCOME.TXT the pad shows are where
    home's are, byte for byte, and our own file is not. The banked NOTES
    would take its note for ours; only the witness tells them apart."""
    b = bytearray(open(src, "rb").read())
    v = os88flush.Volume(bytes(b))
    sa = [e for e in v.listdir("") if e.name == "STIKAPPS"][0]
    at = (v.data_lba + (sa.cluster - 2) * v.spc) * 512
    i = b.index(b"STIKNOTEO88", at, at + 512 * v.spc)
    b[i + 7] = ord("X")
    b[0x27] ^= 0x5A
    out = os.path.join(tmp, "twin.img")
    open(out, "wb").write(bytes(b))
    return out


class Note(object):
    """The running Sticky Note: its pad, its bss, its files."""

    def __init__(self, ui, tmp):
        self.ui, self.m, self.tmp = ui, ui.m, tmp
        self.image = image_size()
        # the state is in the Sticky App block (SPEC.md 110.15)
        self.woff = S.state_off(S.STIKNOTE_LIB, S.STIKNOTE_STIK_BSS,
                                S.STIKNOTE_PAYLEN, self.image)
        self.pad_i = None
        self.wseg = None

    def launch(self):
        ui = self.ui
        w = ui.path("A:/STIKAPPS/STIKNOTE.O88")
        check(w is not None and w.title == TITLE, "Sticky Note opened")
        for o in list(ui.windows()):        # the folder windows go: the pad is
            if o.title != TITLE and o.visible:     # UNDER every window
                ui.close(o)
        pads = [o for o in ui.windows() if o.title == TITLE
                and o.flags & geom.WF_DESK]
        if not pads:
            raise SystemExit("no desk window called %r: %r"
                             % (TITLE, ui.windows()))
        self.pad_i = pads[0].i
        self.wseg = struct.unpack_from("<H", self.m.read(
            ui._S("wm_wins") + pads[0].i * geom.WIN_SIZE + geom.W_SEG,
            2))[0]
        os88marty.guest_sleep(self.m, 1.5)      # the first beat
        return w

    # --- state ---------------------------------------------------------------
    def bss(self, off, n=1):
        return bytes(self.m.readseg(self.wseg, self.image + off, n))

    def st(self):
        """The pad's view: the count, the place, the name, the head, the
        watch."""
        hl = struct.unpack("<H", self.bss(S.STIKNOTE_BSS_HLEN, 2))[0]
        return dict(n=self.bss(S.STIKNOTE_BSS_N)[0],
                    cur=self.bss(S.STIKNOTE_BSS_CUR)[0],
                    doc=self.bss(S.STIKNOTE_BSS_DOC, 13).split(b"\0")[0]
                    .decode("latin-1"),
                    head=self.bss(S.STIKNOTE_BSS_HBUF, min(hl, 512)),
                    wslot=self.bss(S.STIKNOTE_BSS_WSLOT)[0])

    def li(self):
        return self.bss(self.woff["stik_li"])[0]

    def pad(self):
        for o in self.ui.windows():
            if o.i == self.pad_i:
                return o
        return None

    def g(self):
        return S.STIKNOTE_GEO[self.li()]

    def face(self):
        p = self.pad()
        return p.x + 1, p.y

    def at(self, x, y):
        fx, fy = self.face()
        return fx + x, fy + y

    def press(self, pt):
        self.ui.mo.click(pt[0], pt[1], settle=0)
        os88marty.pace(self.m, 1.0)

    def until(self, cond, what, guest=30):
        self.ui._wait(cond, what, guest, snapshot=lambda: repr(self.st()))

    def timed(self, pt, cond, what, guest=60):
        """A press at `pt`, then cond: (press to the visit's end, the visit),
        GUEST seconds, off the kernel's own breakpoints (VISIT).

        The pointer's travel and os88mouse's double-click gap are spent BEFORE
        the clock starts, and nothing is paced inside it: an earlier shape of
        this timed n.press, whose own os88marty.pace(1.0) is 4.5 guest seconds,
        so every figure it printed was that pause plus the machine's work
        (7.0 s for a page turn whose visit was 0.5)."""
        mo = self.ui.mo
        mo.to(*pt)
        os88marty.guest_sleep(self.m, 2.5)  # the floppy motor has STOPPED
        mo._sep()                           # (2.03 s, SPEC.md 18.9.1), as it
                                            # has between two human presses
        with os88marty.bp_trace(self.m, *VISIT, on_hit=lambda m, r:
                                cycles(m)) as tr:
            c0 = cycles(self.m)
            mo._edge(True)
            mo._edge(False)
            self.until(cond, what, guest)
            os88marty.guest_sleep(self.m, 1.0)  # the visit's leave, if a poll
        return visit(tr.hits, c0, c0)           # saw the head before it


    def volume(self, name="flushed.img"):
        return os88flush.Volume(open(self.flush(name), "rb").read())

    def flush(self, name):
        """A: as the guest has it, into a file of its own: the path."""
        out = os.path.join(self.tmp, name)
        self.m.pause()
        try:
            self.m.flush(0, out)
        finally:
            self.m.run()
        return out

    def raw(self, path):
        try:
            return self.volume().read(path)
        except Exception:
            return None

    def saved(self, what):
        """Wait for the save a change owes, then the state file off A:."""
        os88marty.guest_sleep(self.m, 1.0)
        self.until(lambda: not self.bss(self.woff["st"])[0] & STIK_S_DIRTY,
                   what, 60)
        d = self.raw("SYSTEM/APPDATA/STIKNOTE.DAT")
        if d is None:
            return None
        p = S.parse_dat(d, S.STIKNOTE_PAYLEN, S.STIKNOTE_PAYVER)
        return S.parse_stiknote(p["payload"])

    def editor(self, before):
        """The window a launch opened: one not in `before`."""
        new = [o for o in self.ui.windows() if o.i not in before
               and o.visible]
        return new[0] if new else None

    def box_xy(self, line):
        g = self.g()
        return self.at(S.STIKNOTE_BX + 4, g["B0"] + g["P"] * line + 3)


def rec_bad(img, x, y, rec, w):
    bad = []
    for r, bits in enumerate(rec):
        for c in range(w):
            ink = bool(bits & (0x8000 >> c))
            if (not img[y + r][x + c]) != ink:
                bad.append((x + c, y + r))
    return bad


def herc(tmp):
    img = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    welcome = open(os.path.join(BDIR, "notes", "WELCOME.TXT"), "rb").read()
    with os88ui.boot(img, machine="os8088_5150_herc_gla") as ui:
        n = Note(ui, tmp)
        m = n.m
        print("A: the launch, and the folder")
        n.launch()
        check(n.li() == 1, "Hercules takes the MID layout (stik_li %d)"
              % n.li())
        s = n.st()
        check(s["n"] == 1 and s["cur"] == 0 and s["doc"] == "WELCOME.TXT",
              "one note, WELCOME.TXT, current (%r)" % s)
        check(s["head"] == welcome[:512], "its head is the file's first "
              "bytes (%d of %d)" % (len(s["head"]), len(welcome)))
        st = n.saved("the first beat's save")
        check(st is not None and st["cur"] == "WELCOME.TXT"
              and st["colors"] == {}, "STIKNOTE.DAT v2 names it current, no "
              "papers (%r)" % st)
        g = n.g()
        H = g["H"]

        def frame():
            ui.mo.to(20, 300)
            os88marty.pace(m, 1.0)
            return m.vram("herc")[2]

        print("B: the stack, the notches and the turned corner")
        f = frame()
        fx, fy = n.face()
        top = [not f[fy][fx + x] for x in range(S.STIKNOTE_FACE_W
                                                 - S.STIKNOTE_S)]
        check(all(top), "the sheet's top edge is ink across 188 columns")
        right = [not f[fy + y][fx + S.STIKNOTE_FACE_W - 1]
                 for y in range(S.STIKNOTE_S, H)]
        check(all(right), "the stack's right edge is ink down the card")
        for what, xs, ys in (("top right", range(188, 192), range(0, 4)),
                             ("bottom left", range(0, 4),
                              range(H - 4, H))):
            px = [f[fy + y][fx + x] for y in ys for x in xs]
            check(any(px) and not all(px), "the %s notch is the desktop's "
                  "dither, not paper (%d of 16 lit)" % (what, sum(px)))
        K = g["K"]
        r0 = H - S.STIKNOTE_S - K
        diag = [not f[fy + r0 + r][fx + 187 - r] for r in range(K)]
        past = [f[fy + r0 + r][fx + 187] for r in range(2, K)]
        check(all(diag) and all(past), "the turned corner: its diagonal ink, "
              "the corner past it paper")

        print("C: nothing armed, nothing polled")
        with os88marty.bp_trace(m, "osapi_sys_snapshot_x") as tr:
            os88marty.guest_sleep(m, 3 * BEAT + 0.5)
        check(tr.count("osapi_sys_snapshot_x") == 0, "three beats, no "
              "OSAPI_SYS_SNAPSHOT (%d)" % tr.count("osapi_sys_snapshot_x"))

        print("D: a box ticked on the pad")
        lines = S.stiknote_lines(welcome)
        wrap = S.stiknote_wrap(lines[1:])
        k = next(i for i, (kind, first, _) in enumerate(wrap)
                 if kind is False and first)        # the first OPEN box
        text = wrap[k][2]
        off = welcome.index(b"[ ] " + text.encode()) + 1
        secs, vis = n.timed(n.box_xy(k),
                            lambda: n.st()["head"][off:off + 1] == b"x",
                            "the tick to land in the head")
        print("      the tick, press to written: %.2f guest s (the visit "
              "%.2f)" % (secs, vis))
        d = n.raw(NOTES + "/WELCOME.TXT")
        check(d is not None and d[off - 1:off + 3] == b"[x] "
              and d[:off - 1] == welcome[:off - 1],
              "the file on the disk has `[x] ` (%r)" % (d and d[off - 1:off + 3]))
        f = frame()
        bx, by = n.at(S.STIKNOTE_BX, g["B0"] + g["P"] * k - 1)
        bad = rec_bad(f, bx, by, TICK9, 9)
        check(not bad, "the box is the ticked record, every pixel (%d off)"
              % len(bad))

        print("E: the arrow opens it in Note Pad; save, close, re-read")
        before = set(o.i for o in ui.windows())
        n.press(n.at(S.STIKNOTE_OPENX + 6, g["OY"] + 4))
        ui._wait(lambda: n.editor(before) is not None, "the editor to open",
                 60, snapshot=ui.titles)
        ed = n.editor(before)
        check(ed.title == "Note Pad", "Note Pad opened it (%r)" % ed.title)
        n.until(lambda: n.st()["wslot"] != 0, "the watch to arm")
        os88marty.guest_sleep(m, 2.0)
        m.type_text("Hi ")
        os88marty.pace(m, 1.0)
        m.ctrl("KeyS")
        ui.wait_toast(says="Saved")
        os88marty.guest_sleep(m, 1.0)
        c0 = cycles(m)
        with os88marty.bp_trace(m, *VISIT, on_hit=lambda mm, r:
                                cycles(mm)) as tr:
            ui.close(ed)            # returns once the window is gone: the
            c1 = cycles(m)          # beat is counted from there, not from
            print("      Note Pad's own close: %.2f guest s"   # the click
                  % ((c1 - c0) / os88marty.GUEST_HZ))
            n.until(lambda: n.st()["wslot"] == 0,
                    "the watch to see the close")
            gone = (cycles(m) - c1) / os88marty.GUEST_HZ
            check(gone <= BEAT + 0.5, "the watch disarmed within a beat of "
                  "the editor's window going (%.2f guest s)" % gone)
            n.until(lambda: n.st()["head"].startswith(b"Hi Welcome"),
                    "the pad to show the saved text", 30)
            os88marty.guest_sleep(m, 1.0)
        secs, vis = visit(tr.hits, c0, c1)
        print("      close to the pad's new head: %.2f guest s (the "
              "re-read's visit %.2f)" % (secs, vis))
        check(n.raw("STIKAPPS/WELCOME.TXT") is None, "Note Pad saved into "
              "NOTES, not into STIKAPPS")

        print("E2: another os8088 disk in A: is not home")
        home = n.flush("home.img")      # the system disk as it stands
        m.mount(0, twin(home, tmp))
        os88marty.guest_sleep(m, 3.0)   # the motor stops: the next stand
        head = n.st()["head"]           # must ASK which disk this is
        m.disk(reset=True)
        n.press(n.box_xy(k))            # a tick, over NOTES' bank
        os88marty.guest_sleep(m, 2.0)
        d = m.disk()
        check(d.get("writes", 0) == 0 and d.get("reads", 0) > 0,
              "the tick read the twin and wrote nothing to it (%r)" % d)
        check(n.st()["head"] == head, "the pad is unchanged - the twin's "
              "WELCOME.TXT was not taken for ours")
        m.mount(0, home)
        os88marty.guest_sleep(m, 3.0)

        print("F: + makes a note, < turns back")
        before = set(o.i for o in ui.windows())
        n.press(n.at(S.STIKNOTE_PLUSX + 2, g["HY"] + 3))
        ui._wait(lambda: n.editor(before) is not None,
                 "the new note's editor", 60, snapshot=ui.titles)
        s = n.st()
        check(s["n"] == 2 and s["cur"] == 1 and s["doc"] == "NOTE0001.TXT"
              and s["head"] == b"New note\r\n", "NOTE0001.TXT, current, its "
              "title line (%r)" % s)
        ui.close(n.editor(before))
        n.until(lambda: n.st()["wslot"] == 0, "the watch to disarm")
        check(n.raw(NOTES + "/NOTE0001.TXT") == b"New note\r\n",
              "the file on the disk")
        secs, vis = n.timed(n.at(S.STIKNOTE_PREVX + 2, g["FY"] + 3),
                            lambda: n.st()["cur"] == 0
                            and n.st()["head"].startswith(b"Hi Welcome"),
                            "the page to turn back")
        print("      the page turn, press to head: %.2f guest s (the visit "
              "%.2f)" % (secs, vis))
        check(n.st()["doc"] == "WELCOME.TXT", "WELCOME.TXT current again")
        n.press(n.at(S.STIKNOTE_NEXTX + 2, g["FY"] + 3))
        n.until(lambda: n.st()["cur"] == 1, "the page to turn on")

        print("G: Delete, twice")
        ui.menu_pick("Note", "Delete Note  Del")
        said = ui.wait_toast(says="again")
        check(n.st()["n"] == 2, "the first Delete deletes nothing (%r)"
              % said)
        ui.menu_pick("Note", "Delete Note  Del")
        n.until(lambda: n.st()["n"] == 1, "the second to delete")
        check(n.st()["doc"] == "WELCOME.TXT"
              and n.raw(NOTES + "/NOTE0001.TXT") is None,
              "NOTE0001.TXT is gone and WELCOME.TXT current")

        print("H: the Color menu, on 1bpp")
        col = [c for c in ui.menus() if c[0] == "Color"]
        its = col[0][3] if col else []
        check(len(its) == 5 and not any(on for _, on in its)
              and all(t.endswith("(color screen)") for t, _ in its),
              "five papers, every one greyed with why (%r)" % its)


def px(fb, x, y):
    w, h, data = fb
    i = (y * w + x) * 3
    return tuple(data[i:i + 3])


def fill_notes(path, count):
    """`count` EMPTY notes, NOTE0001.TXT up, written straight into the free
    slots of SYSTEM/APPDATA/NOTES on a FAT12 image - an empty file has no
    cluster, so only its directory entry is written."""
    img = bytearray(open(path, "rb").read())
    bps, spc, rsvd, nfat, nroot, _tot, _med, fatsz = \
        struct.unpack_from("<HBHBHHBH", img, 11)
    root = (rsvd + nfat * fatsz) * bps
    data0 = root + nroot * 32

    def nxt(c):
        o = rsvd * bps + c * 3 // 2
        v = struct.unpack_from("<H", img, o)[0]
        return (v >> 4) if c & 1 else (v & 0xFFF)

    def slots(c):
        out = []
        while c < 0xFF8:
            base = data0 + (c - 2) * spc * bps
            out += [base + i * 32 for i in range(spc * bps // 32)]
            c = nxt(c)
        return out

    def find(offs, name):
        for o in offs:
            if img[o:o + 11] == name.ljust(11).encode():
                return struct.unpack_from("<H", img, o + 26)[0]
        raise SystemExit("no %r on %s" % (name, path))

    c = find([root + i * 32 for i in range(nroot)], "SYSTEM")
    c = find(slots(c), "APPDATA")
    c = find(slots(c), "NOTES")
    free = [o for o in slots(c) if img[o] in (0, 0xE5)]
    if len(free) < count:
        raise SystemExit("NOTES has %d free slots, not %d" % (len(free),
                                                              count))
    for k in range(count):
        e = bytearray(32)
        e[0:11] = ("NOTE%04d" % (k + 1) + "TXT").encode()
        e[11] = 0x20
        struct.pack_into("<H", e, 24, 0x5C21)
        img[free[k]:free[k] + 32] = e
    open(path, "wb").write(img)


def vga(tmp):
    img = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    fill_notes(img, S.STIKNOTE_SLOTS - 3)       # '.', '..', WELCOME.TXT
    with os88ui.boot(img, machine="os8088_xt_vga") as ui:
        n = Note(ui, tmp)
        m = n.m
        n.launch()
        check(n.li() == 0, "VGA takes the TALL layout (stik_li %d)" % n.li())
        g = n.g()

        print("I: the default paper - yellow, black ink")
        ui.mo.to(20, 460)
        os88marty.pace(m, 1.0)
        fb = m.fbuf()
        fx, fy = n.face()
        sheet = (fx + 100, fy + g["B0"] + g["P"] * 7 + 4)
        check(px(fb, *sheet) == YELLOW, "the pad's sheet is yellow (%r)"
              % (px(fb, *sheet),))
        check(px(fb, fx, fy) == BLACK, "...its edge black")

        print("J: Color > Pink, and a relaunch")
        n.press(n.at(100, g["B0"] + g["P"] * 7 + 4))    # the bar is the pad's
        ui.menu_pick("Color", "  Pink")    # (two cells kept for the `*`)
        os88marty.pace(m, 1.0)
        st = n.saved("the paper's save")
        check(st and st["colors"] == {"WELCOME": "pink"},
              "STIKNOTE.DAT: WELCOME is pink (%r)" % st)
        ui.mo.to(20, 460)
        os88marty.pace(m, 1.0)
        fb = m.fbuf()
        check(px(fb, *n.at(100, g["B0"] + g["P"] * 7 + 4)) == PINK,
              "the pad's sheet is pink")
        ui.widget_close(n.pad())
        n.launch()
        ui.mo.to(20, 460)
        os88marty.pace(m, 1.0)
        fb = m.fbuf()
        check(px(fb, *n.at(100, g["B0"] + g["P"] * 7 + 4)) == PINK,
              "launched again, it is still pink")

        print("K: a full folder")
        s = n.st()
        check(s["n"] == S.STIKNOTE_SLOTS - 2, "thirty notes counted (%d)"
              % s["n"])
        before = set(o.i for o in ui.windows())
        n.press(n.at(S.STIKNOTE_PLUSX + 2, g["HY"] + 3))
        said = ui.wait_toast(says="full", limit=60)
        check("Notes folder is full" in said, "`+` says %r" % said)
        os88marty.guest_sleep(m, 2.0)
        check(n.editor(before) is None and n.st()["n"] == S.STIKNOTE_SLOTS - 2,
              "and nothing was made or opened")


def small(tmp):
    # THE SHIPPED PACKAGE, carried by hand: BDIR is <tree>/smallk on this leg,
    # and no small build of Sticky Note exists (it is on no small disk,
    # SPEC.md 24.5.1) - the package is the tree's own, one directory up
    # small360.img is the tree's too: `make small` writes the 128KB disks
    # beside the shipped ones and keeps only kern_small itself in smallk/
    tree = os.path.dirname(os.path.abspath(BDIR))
    apps = os88marty.scratch_disk(os.path.join(tmp, "snsmall.img"),
                                  os.path.join(tree, "stiknote.o88"))
    with os88ui.boot(os.path.join(tree, "small360.img"), apps=apps,
                     machine="os8088_5150_cga") as ui:
        print("L: kern_small refuses it, in words")
        try:
            ui.path("B:/STIKNOTE.O88")
            opened = True
        except Exception as ex:          # os88ui raises when no window opens
            print("      %s" % str(ex).splitlines()[0])
            opened = False
        said = ui.wait_toast(says="desk", limit=30)
        check(not opened and not [w for w in ui.windows()
                                  if w.title == TITLE],
              "no Sticky Note window opened")
        check("No desk layer" in said, "the toast says why (%r)" % said)


def main():
    tmp = tempfile.mkdtemp(prefix="stiknote-")
    try:
        {"herc": herc, "vga": vga, "small": small}[LEG](tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("stiknote %s: %s (%d failure(s))"
          % (LEG, "ok" if not fails else "FAILED", len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
