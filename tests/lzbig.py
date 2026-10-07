#!/usr/bin/env python3
"""File > Compress and Uncompress on files PAST 64KB (SPEC.md 20.15.4, 22.22.4).

tests/lzcomp.py is the verb on files that fit a segment; this is the same
assertion - the machine's file against `os88lz.lzb_compress_machine`'s, BYTE
FOR BYTE - on the sizes that used to answer "Too large". Each subject is a
different half of what had to change:

  BIG1.TXT   ~100KB that packs to UNDER 64KB. The encoder's SOURCE slides
             (cmz_sslide) and its output never has to; the read-back is the
             decoder crossing on its OUTPUT only, which it always could.
  BIG2.TXT   ~160KB that packs to OVER 64KB. Both of the encoder's segments
             slide, and Uncompress then hands the kernel's transparent read a
             'CZ' file whose PACKED bytes cross a segment - the decoder's
             checkpoint (SPEC.md 20.14.5.1), which nothing else in the tree
             reaches, because every shipped file is LZ4 and packed under 64KB.
  TAIL.DAT   40KB of text, then 70KB of noise: the cut falls before the noise and the
             raw tail is longer than the T word can count, so the verb must
             say `Its end won't compress` and leave the file alone. The
             mirror is asked first and must refuse too, or the fixture is
             the wrong one.

  BIG3.TXT   250KB: too big to hold TWICE on a 640KB machine (the whole
             path wants 2U + 41KB), so Compress STREAMS it (SPEC.md 22.22.5)
             - one pass through 48KB and 33KB windows, written as it goes to
             a new file that takes the name at the end. Same byte-for-byte
             assertion: the parse is the same parse, which is the whole
             design. That it really streamed is read off fm_ebuf, which holds
             the temporary name only if the streamed path ran - and its cut
             stays in the output window, so the truncate must NOT run.
  BIG4.TXT   230KB of text and 45KB of noise: the cut falls before the noise,
             ~50KB of output before the end, so the window has written past it
             and the file must be TRUNCATED back (OSAPI_FILE_WRITE_AT with a
             count of 0, 18.4.7.5) - and the kernel's truncate is breakpointed
             to prove it ran, exactly once.

Then all three big files are uncompressed and must come back as the original
bytes - the round trip is the only assertion that covers the decoder, the
32-bit read and the 32-bit write in one sentence.

**And BIG2's Compress is WATCHED** (SPEC.md 22.22.6): the hand swings the
mouse through the parse while the row samples the kernel's own cursor state,
as tests/curdisk.py does for a disk transfer. Two things must hold: the arrow
MOVES while the gfx lock is held (the parse is CPU work with no int 13h in it,
so every such move is the verb's bracket and not SPEC.md 7.4's), and the bar
says `Compressing...` - the toast is up, [toast_on], for the whole of it.
IT HAS BEEN RED FOR THE REASON IT EXISTS: the first build raised the toast
without fpg's bar bracket, the repaint spent gfx_lock's promised hide, and the
arrow was off the glass for the whole parse - 6 moves in 150 looks, every one
inside the file's own read, against 144 once the toast kept the promise.

**A 1.44MB machine**, `os8088_xt_vga_144`: the three fixtures are 330KB
between them and every 720KB profile here is 40-cylinder (docs/
DOS-DEBUGGING.md's trap), so a 360KB or 720KB disk would either not hold them
or hand the verb a short read and a wrong answer.
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88flush                                       # noqa: E402
import os88lz                                          # noqa: E402
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import dispcp                                          # noqa: E402
import lzcomp                                          # noqa: E402
from lzcomp import S, FM_IUNCOMP, compress, cz, say    # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = "os8088_xt_vga_144"
QUIET = 600                     # guest seconds for one big verb: a 160KB
                                # parse is ~1,600 cycles a byte, ~55 s, and
                                # the reads and writes are as much again - and
                                # a streamed 250KB is two parses
SAMPLES = 150                   # the watched leg: 150 looks, PACKET frames
PACKET = 4                      # apart - ~10 guest seconds of a ~130s parse
SWING, STEP = 6, 6              # six packets one way, six the other
MOVES_MIN = 20                  # measured 144; 6 with the arrow hidden


def half_text(n, seed):
    """n bytes that pack to about half - tests/unit/t_lzfmt.py's generator,
    so the host leg and this one are about the same kind of file"""
    sys.path.insert(0, os.path.join(HERE, "unit"))
    import t_lzfmt
    return t_lzfmt.half_text(n, seed)


def noise(n, seed):
    x, out = seed, bytearray()
    while len(out) < n:
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def watched(m, mo, wx, wy, fails):
    """Compress BIG2.TXT with the hand moving, and read the cursor through it.

    tests/curdisk.py's instrument: `advance` stops the guest between looks, so
    each sample is of a machine standing still, and a change in
    [cur_drawn_x]/[cur_drawn_y] between two samples that both saw the lock
    held is an arrow drawn by the mouse ISR inside the verb's lock hold.
    """
    os88marty.settle(m)
    m.write(S("toast_buf"), b"\0")
    row = dispcp.row_of(m, S, "BIG2.TXT")
    x, y = dispcp.row_xy(wx, wy, row)
    mo.click(x, y)
    os88marty.settle(m)
    lzcomp.menu_pick(m, mo, 1, lzcomp.FM_ICOMP)
    m.run()
    for _ in range(8):          # off the menu, well below the bar: an arrow
        m.mouse(dy=STEP * 3)    # that could reach the bar's rows is held
        m.advance(frames=2)     # there on purpose (SPEC.md 7.4.2 rule 3)
        m.run()
    look, up = [], False
    for i in range(SAMPLES):
        m.advance(frames=PACKET)
        look.append((m.read(S("gfx_lock_flag"), 1)[0],
                     int.from_bytes(m.read(S("cur_drawn_x"), 2), "little"),
                     int.from_bytes(m.read(S("cur_drawn_y"), 2), "little"),
                     m.read(S("toast_on"), 1)[0],
                     lzcomp.toast(m)))
        m.run()
        if i % SWING == SWING - 1:
            up = not up
        m.mouse(dy=-STEP if up else STEP)
        if lzcomp.verdict(m):
            break
    held = [s for s in look if s[0]]
    moves = sum(1 for a, b in zip(look, look[1:])
                if a[0] and b[0] and (a[1], a[2]) != (b[1], b[2]))
    # the toast goes up once the module is in and the file sized - the first
    # second of the hold is CLONE.DRV's own read - so it is counted from the
    # look where it first appears, and from there it must never go down
    first = next((i for i, s in enumerate(held)
                  if s[3] and s[4].startswith(lzcomp.BUSY)), len(held))
    held = held[first:]
    lit = sum(1 for s in held if s[3] and s[4].startswith(lzcomp.BUSY))
    say("  watch     %d looks, %d with the lock held: the arrow moved %d "
        "times, `Compressing...` up for %d" % (len(look), len(held), moves,
                                                lit))
    if not held:
        fails.append("`Compressing...` never appeared during the verb")
    if len(look) < SAMPLES // 2 or sum(1 for s in look if s[0]) < SAMPLES // 2:
        fails.append("the watched Compress held the lock for %d looks of %d "
                     "- it was not the parse that got watched"
                     % (len(held), len(look)))
    if moves < MOVES_MIN:
        fails.append("the arrow moved %d times through the parse, wanted "
                     ">= %d: the pointer is frozen (SPEC.md 22.22.6)"
                     % (moves, MOVES_MIN))
    if held and lit < len(held):
        fails.append("`Compressing...` was up for %d of %d looks inside the "
                     "verb" % (lit, len(held)))
    try:
        os88marty.until(m, lambda mm: lzcomp.verdict(mm),
                        "the watched Compress to finish", poll=0.2,
                        guest=float(QUIET))
    except os88marty.MartyError:
        fails.append("the watched Compress of BIG2.TXT never finished")


def streamed(m, mo, wx, wy, fl, name, want, trunc, fails):
    """Compress `name` on the STREAMED path, counting the kernel's truncate.

    `lzcomp.compress` waits on the toast, which a breakpoint would stop the
    guest under - so this is its gesture with a wait of its own: every hit on
    dskw_wabody's `.trunc` is counted and the machine let go again, until the
    verdict is up.
    """
    os88marty.settle(m)
    m.write(S("toast_buf"), b"\0")
    m.write(S("fm_ebuf"), b"\0")
    row = dispcp.row_of(m, S, name)
    x, y = dispcp.row_xy(wx, wy, row)
    mo.click(x, y)
    os88marty.settle(m)
    m.bp_exec("dskw_wabody.trunc")
    lzcomp.menu_pick(m, mo, 1, lzcomp.FM_ICOMP)
    hits, c0 = 0, int(m.status()["cycles"])
    while True:
        m.run()
        st = m.wait_stop(limit=2.0)
        if st == "breakpoint":
            hits += 1
            continue
        t = lzcomp.verdict(m)
        if t:
            break
        if (int(m.status()["cycles"]) - c0) / 4772727.0 > QUIET:
            fails.append("%s: the streamed Compress never finished" % name)
            break
    m.bp_exec()
    m.run()
    say("   [%s took %.1f guest s]"
        % (name, (int(m.status()["cycles"]) - c0) / 4772727.0))
    os88marty.settle(m)
    got = fl.volume(1).read(name)
    eb = m.read(S("fm_ebuf"), 13).split(b"\0")[0]
    ok = (t.startswith("Compressed") and got == want and
          eb == b"CMPRESS~.TMP" and hits == trunc)
    say("  %-9s %s  %r  (%d bytes, wanted %d; streamed %s, truncated %d "
        "time(s), wanted %d)" % (name.split(".")[0].lower(),
                                 "ok " if ok else "BAD", t, len(got),
                                 len(want), eb == b"CMPRESS~.TMP", hits,
                                 trunc))
    if not ok:
        fails.append("%s streamed: said %r, %d bytes against %d, fm_ebuf %r, "
                     "the truncate ran %d time(s) where %d was the point"
                     % (name, t, len(got), len(want), eb, hits, trunc))
    names = [n for n, _ in dispcp.listing(m, S)]
    if "CMPRESS~.TMP" in names:
        fails.append("the streamed Compress of %s left CMPRESS~.TMP behind"
                     % name)
    # ...AND THE VOLUME IS STILL A VOLUME. A truncate that freed one cluster
    # too few leaks it, and one too many cross-links the next file: the bytes
    # of THIS file read back perfectly either way, so os88disk's fsck is asked
    img = "/tmp/lzbig-fsck-%d.img" % os.getpid()
    fl.save(1, img)
    r = subprocess.run([sys.executable,
                        os.path.join(HERE, "..", "tools", "os88disk.py"),
                        "--verify", img], capture_output=True, text=True)
    os.remove(img)
    say("  fsck      %s  after %s" % ("ok " if r.returncode == 0 else "BAD",
                                      name))
    if r.returncode:
        fails.append("the volume after streaming %s fails os88disk --verify: "
                     "%s" % (name, (r.stdout + r.stderr).strip()[-300:]))


def main():
    ap = argparse.ArgumentParser()
    ap.parse_args()
    for f in ("build/os8088.img",):
        if not os.path.exists(os88build.at(f)):
            sys.exit("lzbig: %s is missing - run `make` first" % f)

    big1 = half_text(100000, 7)
    big2 = half_text(160000, 11)
    tail = half_text(40000, 3) + noise(70000, 5)
    big3 = half_text(250000, 13)
    big4 = half_text(230000, 17) + noise(45000, 19)
    want1 = cz(os88lz.lzb_compress_machine(big1), len(big1))
    want2 = cz(os88lz.lzb_compress_machine(big2), len(big2))
    want3 = cz(os88lz.lzb_compress_machine(big3), len(big3))
    want4 = cz(os88lz.lzb_compress_machine(big4), len(big4))
    try:
        os88lz.lzb_compress_machine(tail)
        sys.exit("lzbig: the mirror packs TAIL.DAT, so the refusal leg would "
                 "test nothing - the fixture's tail must be past 64KB")
    except ValueError:
        pass
    if len(want1) >= 0x10000 or len(want2) <= 0x10000:
        sys.exit("lzbig: the fixtures no longer straddle 64KB packed (%d, %d)"
                 % (len(want1), len(want2)))
    say("lzbig: BIG1 %d -> %d, BIG2 %d -> %d (packed past 64KB), TAIL %d, "
        "BIG3 %d -> %d (streamed)" % (len(big1), len(want1), len(big2),
                                      len(want2), len(tail), len(big3),
                                      len(want3)))

    # Per-run and removed on the way out: the verbs WRITE this disk, so a kept
    # image would hand the next run files that are already compressed
    # (tests/lzcomp.py's note on the same trap).
    img = "/tmp/lzbig-%d.img" % os.getpid()
    d = os.path.join(os88build.at("build"), "lzbig")
    os.makedirs(d, exist_ok=True)
    disk = os88marty.scratch_disk(
        img,
        lzcomp.stage(d, "BIG1.TXT", big1),
        lzcomp.stage(d, "BIG2.TXT", big2),
        lzcomp.stage(d, "TAIL.DAT", tail),
        lzcomp.stage(d, "BIG3.TXT", big3),
        lzcomp.stage(d, "BIG4.TXT", big4),
        size=1440)
    fails = []

    with os88marty.launch(os88build.at("build/os8088.img"),
                          apps=disk, machine=MACHINE) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        fl = os88flush.Flush(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("lzbig: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]

        def leg(tag, name, want, item=None, expect="Compressed"):
            kw = {"quiet": QUIET}
            if item is not None:
                kw["item"] = item
            t = compress(m, mo, wx, wy, name, fails, **kw)
            got = fl.volume(1).read(name)
            ok = t.startswith(expect) and got == want
            say("  %-9s %s  %r  (%d bytes, wanted %d)"
                % (tag, "ok " if ok else "BAD", t, len(got), len(want)))
            if not ok:
                i = next((k for k in range(min(len(got), len(want)))
                          if got[k] != want[k]), min(len(got), len(want)))
                fails.append("%s %s: said %r, %d bytes against %d, first "
                             "differing byte %d"
                             % (tag, name, t, len(got), len(want), i))

        leg("big1", "BIG1.TXT", want1)
        watched(m, mo, wx, wy, fails)
        got = fl.volume(1).read("BIG2.TXT")
        ok = got == want2
        say("  big2      %s  (%d bytes, wanted %d)"
            % ("ok " if ok else "BAD", len(got), len(want2)))
        if not ok:
            fails.append("big2 BIG2.TXT: %d bytes against %d"
                         % (len(got), len(want2)))
        leg("tail", "TAIL.DAT", tail, expect="Its end")
        for name, want, trunc in (("BIG3.TXT", want3, 0),
                                  ("BIG4.TXT", want4, 1)):
            streamed(m, mo, wx, wy, fl, name, want, trunc, fails)
        leg("unbig4", "BIG4.TXT", big4, item=FM_IUNCOMP,
            expect="Uncompressed")
        leg("unbig3", "BIG3.TXT", big3, item=FM_IUNCOMP,
            expect="Uncompressed")
        leg("unbig2", "BIG2.TXT", big2, item=FM_IUNCOMP,
            expect="Uncompressed")
        leg("unbig1", "BIG1.TXT", big1, item=FM_IUNCOMP,
            expect="Uncompressed")

    try:
        os.remove(img)
    except OSError:
        pass
    for f in fails:
        say("  FAIL: " + f)
    say("lzbig: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
