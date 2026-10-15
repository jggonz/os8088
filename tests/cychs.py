#!/usr/bin/env python3
"""Cyclone's high-score table survives a save and a re-launch (SPEC.md 67.23,
77.31.1).

    make && python3 tests/cychs.py

WHAT IS UNDER TEST. `cy_hstab` IS the file: CYCLONE.HS is read straight into
it and written straight out of it (`cy_hs_load`/`cy_hs_save`), it sits at a
512-byte boundary because a transfer that straddles a 64KB physical page is
refused by int 13h (SPEC.md 77.31.1), and the apps size pass moved it there
with `OS88_BSS_ALIGNED` (SPEC.md 20.5). Nothing exercised any of that - every
other Cyclone row plays the game and none of them ends one.

THE SCORE IS PLANTED, not played for. Once the warp has finished the row
writes a score, no lives and a death that expires on the next frame into the
package's own variables - `tests/cycplay.py`'s way to a death - so
`cy_die_update` takes its `.over` arm and offers the score to the table
exactly as a real game over does. The initials are then TYPED, because
`cy_init_key` -> `cy_init_commit` -> `cy_hs_save` is the path a player takes
and the one the row exists for.

TWO GAMES, because `od_write` has two arms and a game over reaches both: the
session's first save of the file is a full `OSAPI_FILE_WRITE` (it creates
CYCLONE.HS in SYSTEM/APPDATA), and the second is the one-sector in-place
`OSAPI_FILE_WRITE_AT` the stamp `cy_hs_ip` enables. Each save is bracketed on
`cy_hs_save`'s own return, where CF is still `od_write`'s answer (every pop
after it leaves the flags alone), so a refused write is named at the save and
not inferred from the read-back.

THE ASSERTION IS THE READ-BACK: the window is closed, Cyclone is launched
again from the same disk, and the new instance's `cy_hs_load` must hand back
both rows, names and order intact, above the built-in defaults.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): taking `call od_write` out of
`cy_hs_save` turns this red at the first save; pointing `cy_hs_load`'s read at
any buffer but `cy_hstab` turns it red at the read-back.
"""
import sys, os
# THIS TREE's root, DERIVED - never a hard-coded path (tests/cycweb.py's note)
_R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_R, "tools"))
sys.path.insert(0, os.path.join(_R, "tests"))
import os88marty, os88ui, os88geom                      # noqa: E402
from cycweb import pkg_syms, Pkg, u16, CYS_TITLE, CYS_PLAY   # noqa: E402

CYS_DIE = 4
NHS = 5
# (score, initials typed, the row it must take)
GAMES = [(77777, "qzx", 0), (12345, "ABC", 1)]
WANT = [(77777, b"QZX"), (12345, b"ABC"), (5000, b"---"), (4000, b"---"),
        (3000, b"---")]


def table(p):
    """The five rows as (score, initials), out of the live cy_hstab."""
    s = p.m.read(p.addr("cy_hs"), NHS * 4)
    n = p.m.read(p.addr("cy_hsn"), NHS * 3)
    return [(u16(s, 4 * i) | (u16(s, 4 * i + 2) << 16), bytes(n[3*i:3*i+3]))
            for i in range(NHS)]


def launch(ui, syms, image):
    """Open Cyclone off B: and answer its Pkg, checked against this source."""
    w = ui.path("B:/GAMES/CYCLONE.O88")
    seg = u16(ui.m.read(os88geom.winptr(ui.m, w.i, ui.sym) + os88geom.W_SEG, 2))
    lo, n = syms["cy_entry"], 2048           # cycweb's guard: a knob build in
    if bytes(ui.m.read(seg * 16 + lo, n)) != image[lo:lo + n]:  # build/ moves
        raise SystemExit("cychs: the CYCLONE.O88 running here is not this "
                         "source - run `make` (a knob build in build/ is the "
                         "usual cause)")
    p = Pkg(ui.m, seg, syms)
    # [cy_hired], NOT [cy_hsload]. cy_hs_load sets its own flag BEFORE the
    # read, so waiting on it returns with the floppy still turning and the
    # built-in table in memory - which is exactly a read-back that failed,
    # and the first run of this row reported one. cy_paint calls cy_hire
    # AFTER cy_hs_load returns, so the worker being hired is the load done.
    os88marty.until(ui.m, lambda _m: p.rb("cy_hired") == 1,
                    "the first paint to load the table and hire the worker",
                    poll=0.2, guest=30.0)
    if p.rb("cy_hsload") != 1:
        raise SystemExit("cychs: the worker is hired and cy_hs_load never "
                         "ran - cy_paint's order has changed")
    return w, p


def start(m, p):
    """Title -> play, cycplay's way: Enter until the title lets go."""
    for _ in range(10):
        if p.rb("cy_state") != CYS_TITLE:
            break
        m.key("Enter")
        try:
            os88marty.until(m, lambda _m: p.rb("cy_state") != CYS_TITLE,
                            "the title to take Enter", poll=0.2, guest=9.0)
        except os88marty.MartyError:
            pass
    os88marty.until(m, lambda _m: p.rb("cy_state") == CYS_PLAY,
                    "the warp to finish", poll=0.5, limit=90)


def game(m, p, syms, score, initials, row):
    """Plant a game over at `score`, type the initials, bracket the save."""
    m.pause()
    p.ww("cy_score", score & 0xFFFF)
    m.write(p.addr("cy_score") + 2, bytes([(score >> 16) & 0xFF,
                                           score >> 24]))
    p.wb("cy_lives", 0)
    p.ww("cy_dietim", 1)                 # ...expiring on the next frame
    p.wb("cy_state", CYS_DIE)            # LAST: the frame reads it first
    m.go()
    os88marty.until(m, lambda _m: p.rb("cy_ient") == 1,
                    "the initials prompt (cy_hs_submit refused %d?)" % score,
                    poll=0.2, guest=20.0)
    got = p.rw("cy_hsidx")
    if got != row:
        raise SystemExit("cychs: %d took row %d of the table, want %d"
                         % (score, got, row))
    for ch in initials:
        before = p.rb("cy_ipos")
        m.type_text(ch)
        os88marty.until(m, lambda _m: p.rb("cy_ipos") != before,
                        "cy_init_key to take %r" % ch, poll=0.1, guest=10.0)
    # The save, bracketed: cy_hs_save's entry, then its own return - whose CF
    # is od_write's answer, the pops between them writing no flag.
    m.bp_exec(p.addr("cy_hs_save"))
    m.run()
    m.key("Enter")
    if not m.wait_stop(limit=60.0):
        raise SystemExit("cychs: Enter did not reach cy_hs_save")
    r = m.regs()
    ret = p.seg * 16 + u16(m.read((r["ss"] << 4) + r["sp"], 2))
    # ...and od_write's FULL arm on the way, which says which of the two
    # writes this was: the in-place WRITE_AT falls into .full only on a
    # refusal, so a save that reaches it with a current stamp did not write
    # in place.
    full = p.addr("od_write.full")
    m.bp_exec(ret, full)
    tookfull = False
    while True:
        m.run()
        if not m.wait_stop(limit=120.0):
            raise SystemExit("cychs: cy_hs_save never returned")
        r = m.regs()
        if (r["cs"] << 4) + r["ip"] != full:
            break
        tookfull = True
    cf = r["flags"] & 1
    m.bp_exec()
    m.go()
    ip = p.rb("cy_hs_ip")
    print("   game %d -> row %d as %s: cy_hs_save CF=%d, %s write, stamp %d"
          % (score, row, initials.upper(), cf,
             "FULL" if tookfull else "IN-PLACE", ip))
    if cf:
        raise SystemExit("cychs: the save of %d was REFUSED (od_write CF=1)"
                         % score)
    if not ip:
        raise SystemExit("cychs: the save returned CF=0 but od_write never "
                         "stamped cy_hs_ip - it did not take its WRITE arm")
    if tookfull != (row == 0):
        raise SystemExit("cychs: save %d took the %s arm - the session's "
                         "first save must CREATE the file and the second "
                         "must write it IN PLACE" % (row + 1,
                         "full" if tookfull else "in-place"))
    os88marty.until(m, lambda _m: p.rb("cy_ient") == 0,
                    "the initials prompt to close", poll=0.1, guest=10.0)


def main():
    syms, image = pkg_syms(os.path.join(_R, "apps/cyclone/cyclone.asm"),
                           (os.path.join(_R, "apps") + "/",))
    with os88ui.boot(os.path.join(_R, "build/os8088-360.img"),
                     apps=os.path.join(_R, "build/apps360.img"),
                     machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        w, p = launch(ui, syms, image)
        first = table(p)
        print("   first launch, no file: %s" % first)
        if first != [(5000 - 1000 * i, b"---") for i in range(NHS)]:
            raise SystemExit("cychs: the first launch did not come up on the "
                             "built-in table - is there a CYCLONE.HS on the "
                             "disk already?")
        ui.mo.to(2, 2)
        for score, initials, row in GAMES:
            start(m, p)
            game(m, p, syms, score, initials, row)
            # ...and back to the title for the next one: cy_overt runs out
            os88marty.until(m, lambda _m: p.rb("cy_state") == CYS_TITLE,
                            "the game over to return to the title",
                            poll=0.5, guest=30.0)
        live = table(p)
        print("   in memory after two saves: %s" % live)
        if live != WANT:
            raise SystemExit("cychs: the table in memory is wrong before it "
                             "was ever read back - cy_hs_submit or "
                             "cy_init_commit, not the file")

        ui.close(w)
        w, p = launch(ui, syms, image)
        back = table(p)
        print("   re-launched, read back: %s" % back)
        if back != WANT:
            raise SystemExit("cychs: the re-launched Cyclone read back %s, "
                             "want %s" % (back, WANT))
    print("ok: two games saved (full write, then in place) and read back")
    return 0


if __name__ == "__main__":
    sys.exit(main())
