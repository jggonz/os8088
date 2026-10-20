#!/usr/bin/env python3
"""BEVERLY.MOD, COMPRESSED, opened by a double-click - and by the DIALOG.

    make lzmodtest && python3 tests/lzmod.py              # SPEC.md 20.14.5
    make lzmodtest && python3 tests/lzmod.py --dialog     # SPEC.md 38.6.1
    make lzmodtest && python3 tests/lzmod.py --nohint     # SPEC.md 20.14.6.3
    make lzmodbigtest && python3 tests/lzmod.py --fmt lz4big # SPEC.md 20.14.5.2

TWO ROUTES, AND THE SECOND ONE IS WHY THIS FILE HAS A FLAG. Until SPEC.md
38.6.1 this row drove a double-click ONLY, which is the route that goes
through OSAPI_FILE_FIND - and OSAPI_FILE_FIND decodes the compression hint.
The Standard File dialog did not: fdlg_sizeof answered out of the staged
listing entry, whose size is deliberately the ON-DISK one (SPEC.md 19.1), so
every app that funds a claim from SPEC.md 38.6's DX:CX claimed a third of what
the read was about to deliver and then failed its own read.

The user-visible result was that **BEVERLY.MOD opened by double-click and
refused with "File too big" from File > Open**, in BOTH MOD players, on any
machine - and no gate saw it, because this one used the working route and the
two fixtures that DO drive a dialog (trackmove360.img, mppmove360.img) ship
the module UNCOMPRESSED, where the packed and unpacked sizes are the same
number. A defect reachable only through the route nothing tested.

So --dialog is the same disk, the same module and the same byte-for-byte
comparison, reached through Tracker's own File > Open instead. It asserts
nothing the default arm does not; what it changes is the surface that answers
"how big is it", which is the entire bug.

This is the file the whole feature is for. 116,085 bytes is 114 of a 360KB
disk's 354 clusters, which is why that geometry ships the module on a floppy of
its own (SPEC.md 24.4). LZ4 takes it to 42,177 - 36.3%, 42 clusters - so
Tracker and the module fit one disk with 294 clusters left, and ~145 sectors
of floppy time go away for ~1.2 seconds of decode.

It is also the only file in the tree that exercises the decoder's SEGMENT
CROSSING: 116KB is not a segment, so every path in SPEC.md 20.14.5 - the
bumped ES, the borrowed match source one segment down, lz_cross splitting a
copy at the boundary, and LZ_F_BUMP retiring the offset compare - runs here and
nowhere else.

--nohint IS THE FIELD'S DISK. The module's directory hint (SPEC.md 20.14.1)
is struck on a scratch copy - which is what Windows leaves when the MEDIA
folder is copied out and back, since it writes NTRes and CrtTimeTenth itself -
so OSAPI_FILE_FIND reports the PACKED 42KB, Tracker claims that, and the read's
sniff (20.14.6) finds 116KB. Before 20.14.6.3 that was 'File too big' and no
module; the read now says how many KB it needs and Tracker claims again.

FOUR ASSERTIONS, and the third is the one that makes the others worth having:

  1. the disk file really is compressed - the 'CZ' header and the directory
     hint, checked on the HOST before the machine is started, so a fixture
     that quietly stopped compressing cannot pass this row;
  2. a double-click on the .MOD row opens Tracker through the association
     (SPEC.md 54), which is the user-visible requirement in one action;
  3. all 116,085 bytes in the guest's claim are BYTE FOR BYTE the original.
     Nothing less will do: a decoder that got one match wrong across the
     64KB boundary still opens a window, still shows the title, and still
     plays - it plays a click;
  4. ...and Tracker holds it - `mp_loaded` - so the bytes above are a module
     the application accepted and not a buffer it read and rejected. Whether
     the mixer TICKS is a question about the machine's sound card and not
     about this feature, so `mp_row` is reported and not asserted, exactly as
     tests/trackmove.py does it.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import os88build                                       # noqa: E402
import os88spkfx                                        # noqa: E402
import os88lz                                          # noqa: E402
import dispcp                                          # noqa: E402
import os88geom                                        # noqa: E402
import os88ui                                          # noqa: E402
from os88fixture import need                           # noqa: E402
from trackmove import pkg_syms                         # noqa: E402

SRC = "apps/tracker/beverly.mod"
PACKED = "build/lzf/BEVERLY.MOD"
IMG = "build/lzmod360.img"
CZ_MARK, CZ_M, CZ_H, CZ_L = 0x5A, 12, 13, 20   # +fmt: 20.14.2.4
S = os88sym.linear


def say(*a):
    print(*a, flush=True)


def report(fails):
    for f in fails:
        say("  FAIL: " + f)
    say("lzmod: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


def dirents(img):
    """Every root-directory entry of a FAT12 image, raw."""
    d = open(img, "rb").read()
    bps = struct.unpack_from("<H", d, 11)[0]
    res = struct.unpack_from("<H", d, 14)[0]
    nfat, nent = d[16], struct.unpack_from("<H", d, 17)[0]
    fsz = struct.unpack_from("<H", d, 22)[0]
    off = (res + nfat * fsz) * bps
    for i in range(nent):
        e = d[off + i * 32:off + i * 32 + 32]
        if e[0] not in (0, 0xE5):
            yield e


def host_checks(fails):
    """Assertion 1, before a machine is involved.

    THROUGH `os88build.at`, because these two are the very files the guest is
    about to boot (14.2). Under a frozen run the tree holds them and `build/`
    may not, so reading the literal path either misses or - worse - asserts
    about one build's fixture and boots another's.
    """
    plain = open(SRC, "rb").read()
    blob = open(os88build.at(PACKED), "rb").read()
    parsed = os88lz.cz_parse(blob)
    if parsed is None:
        fails.append("%s is not a 'CZ' file" % PACKED)
        return plain
    fmt, n = parsed
    if n != len(plain) or os88lz.cz_unwrap(blob) != plain:
        fails.append("%s does not expand to %s" % (PACKED, SRC))
    if "lz4big" in PACKED and (fmt != os88lz.LZ4
                               or len(blob) - os88lz.CZ_HDR <= 0xFFFF):
        fails.append("the lz4big fixture is %s and %d bytes of stream - it "
                     "has to be LZ4 and PAST 64KB, or this arm is the lz4 "
                     "arm again and tests nothing of SPEC.md 20.14.5.2"
                     % (os88lz.NAMES[fmt], len(blob) - os88lz.CZ_HDR))
    say("  packed     %d -> %d bytes (%.1f%%, %s)"
        % (len(plain), len(blob), 100.0 * len(blob) / len(plain),
           os88lz.NAMES[fmt]))
    for e in dirents(os88build.at(IMG)):
        if e[:11] != b"BEVERLY MOD":
            continue
        hint = struct.unpack_from("<H", e, CZ_L)[0] | (e[CZ_H] << 16)
        size = struct.unpack_from("<I", e, 28)[0]
        ok = (CZ_MARK <= e[CZ_M] <= CZ_MARK + 1     # ...the mark CARRIES the
              and hint == len(plain)                # format (SPEC.md
              and size == len(blob))                # 20.14.2.4)
        say("  hint       mark=%02X unpacked=%d size=%d  %s"
            % (e[CZ_M], hint, size, "ok" if ok else "WRONG"))
        if not ok:
            fails.append("the directory hint on the disk is wrong")
        break
    else:
        fails.append("BEVERLY.MOD is not in %s" % IMG)
    return plain


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--dialog", action="store_true",
                    help="open the module through Tracker's File > Open "
                         "(SPEC.md 38.6.1) instead of by a double-click on "
                         "the row. The dialog answers the size from a "
                         "different kernel surface, and that surface reported "
                         "the PACKED size until 38.6.1 - so this arm is the "
                         "one that fails on the defect and the double-click "
                         "arm is the one that cannot see it")
    ap.add_argument("--fmt", default="lz4", choices=("lz4", "lzb", "lz4big"),
                    help="lzb wraps the module with the bit-oriented format "
                         "instead - the only way LZB's own crossing arm is "
                         "ever EXECUTED (SPEC.md 20.14.5). It costs ~10s of "
                         "host compression in the FIXTURE, which is why it is "
                         "not the default. lz4big is a module padded until "
                         "its LZ4 form is PAST 64KB PACKED (SPEC.md "
                         "20.14.5.2), so the decoder's LZ4 source slides and "
                         "a ~30KB literal run is copied in 16KB pieces")
    ap.add_argument("--nohint", action="store_true",
                    help="strike BEVERLY.MOD's directory hint on a scratch "
                         "copy first, as a Windows copy does (SPEC.md "
                         "20.14.6.3)")
    a = ap.parse_args()

    global PACKED, IMG, SRC
    if a.fmt == "lzb":
        PACKED, IMG = "build/lzb/BEVERLY.MOD", "build/lzmodlzb360.img"
    if a.fmt == "lz4big":
        PACKED, IMG = "build/lz4big/BEVERLY.MOD", "build/lzmodbig360.img"
        SRC = os88build.at("build/lz4big/plain.mod")
    # NO KNOB ON EITHER ARM, and that is worth stating because this row used
    # to build one. The shipped kernel carries BOTH decoders (SPEC.md
    # 20.13.6), so the only thing that differs between the arms is the
    # FIXTURE - which format the module on the scratch disk is wrapped in -
    # and the kernel that reads it is the one everybody boots. The lzb arm
    # was `make all lzmodlzbtest` in build/ followed by a bare `make` to put
    # the tree back, for a kernel that came out byte-identical either way.
    need(IMG)
    fails = []
    plain = host_checks(fails)
    P = pkg_syms("apps/tracker/tracker.asm")
    apps = os88build.at(IMG)
    if a.nohint:
        apps = strike(apps)
    try:
        return run(a, apps, plain, P, fails)
    finally:
        if a.nohint:
            os.remove(apps)


def strike(src):
    """A scratch copy of `src` with BEVERLY.MOD's hint zeroed: the three
    cells SPEC.md 20.14.1 keeps it in, which a foreign OS writes as its own."""
    d = bytearray(open(src, "rb").read())
    bps = struct.unpack_from("<H", d, 11)[0]
    res = struct.unpack_from("<H", d, 14)[0]
    nfat, nent = d[16], struct.unpack_from("<H", d, 17)[0]
    fsz = struct.unpack_from("<H", d, 22)[0]
    off = (res + nfat * fsz) * bps
    for i in range(nent):
        e = off + i * 32
        if d[e:e + 11] == b"BEVERLY MOD":
            d[e + CZ_M] = d[e + CZ_H] = 0
            d[e + CZ_L] = d[e + CZ_L + 1] = 0
            break
    else:
        sys.exit("lzmod: no BEVERLY.MOD in %s to strike" % src)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                       "build", "lzmod-nohint-%d.img" % os.getpid())
    out = os.path.abspath(out)          # ABSOLUTE: under a frozen soak tree
                                        # launch() maps a relative build/
                                        # path into THAT tree, where this
                                        # scratch copy is not
    open(out, "wb").write(d)
    say("  hint       STRUCK on a scratch copy (--nohint)")
    return out


def run(a, apps, plain, P, fails):
    with os88marty.launch("build/os8088-360.img", apps=apps,
                          machine=a.machine) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)   # SPEC.md 79: it DRAWS, so the settle
                                # after the wait below can never return
                                # once it is up - and this row waits on
                                # a 116KB module. os88ui.boot does this
                                # by default and a bare launch does not
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("lzmod: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        say("lzmod: B: lists %r" % [r[0] for r in dispcp.listing(m, S)])

        if a.dialog:
            # --- SPEC.md 38.6.1: through the DIALOG, whose size surface is
            # the one that was wrong. Tracker is launched with NO module and
            # then asked for one by name, so the figure trk_fdone funds its
            # claim from is fdlg_sizeof's and nothing else's.
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy,
                              "TRACKER.O88")
            try:
                os88marty.until(
                    m, lambda mm: len(dispcp.win_list(mm, S)) > len(wins),
                    "Tracker's window to open", poll=0.2, guest=40.0)
            except os88marty.MartyError:
                pass
            tw = dispcp.win_list(m, S)
            trk = [w for w in os88geom.windows(m, S)
                   if w.visible and w.title.startswith("Tracker")]
            if len(tw) <= len(wins) or not trk:
                fails.append("Tracker did not open from TRACKER.O88")
                return report(fails)
            trackwin = trk[-1].i         # BY TITLE: this arm has two windows
                                         # open and the Disk window it was
                                         # launched from lands at the same
                                         # origin, so "the newest slot" is not
                                         # a way to tell them apart

            # THE MENU AND NOT THE 'L' ACCELERATOR. Both are Tracker's own
            # Open (SPEC.md 45), and the key does not survive being scripted:
            # menu_pick reads the LIVE bar out of menu_bar[], which
            # menu_layout rebuilds on every raise, and it confirms the
            # pull-down is down AND that the highlighted item is the one meant
            # BEFORE it releases. A key press that lands on the wrong window,
            # or arrives while a panel is still up, does nothing and says
            # nothing - which is an hour of looking at a test that reports
            # "the file dialog never opened" about a kernel that is fine.
            ui = os88ui.UI(m, mouse=mo, sym=S)
            ui.settle()                 # a menu picked while the app is
                                        # still coming up is lost
            ui.menu_pick("File", "Open...")
            # THE CHOOSER IS A DISK WINDOW (SPEC.md 38.1), so the row is named
            # and not clicked at a remembered offset: `chooser` waits for
            # [fdlg_win] and the window to be up and painted (FDLG.DRV is a
            # module on kern_small - its read, then its window), and
            # `chooser_open` resolves BEVERLY.MOD out of the chooser's own
            # listing, opens it and waits for the chooser to come down. It
            # opens on B:'s ROOT: Tracker has chosen nothing yet and this disk
            # has no MEDIA folder (SPEC.md 38.10).
            try:
                w = ui.chooser()
            except os88ui.UIError as e:
                fails.append("the file dialog never opened: File > Open ran "
                             "and fdlg_win is %04X (%s)"
                             % (int.from_bytes(m.read(S("fdlg_win"), 2),
                                               "little"), e))
                return report(fails)
            say("  dialog     %r, listing %r"
                % (w.title, [r[0] for r in ui.listing(w)]))
            ui.chooser_open("BEVERLY.MOD")
            say("  route      File > Open (fdlg_sizeof's answer)")
        else:
            # The ASSOCIATION opens Tracker and loads the module in one action
            # - which is the requirement, not a shortcut past the File menu.
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy,
                              "BEVERLY.MOD")
            say("  route      double-click (OSAPI_FILE_FIND's size)")
            trackwin = None             # ...the association has not opened it
                                        # yet; the wait below is what finds it
        # WAITED FOR, NOT SLEPT THROUGH, and the budget is the GUEST's clock -
        # so a loaded box gives the machine the same 40 seconds of its own
        # time this needs, instead of thirty host seconds of which it may get
        # twenty. The timeout is swallowed because the sentence below says
        # more about what went wrong than `until`'s does.
        if not a.dialog:
            try:
                os88marty.until(
                    m, lambda mm: len(dispcp.win_list(mm, S)) > len(wins),
                    "Tracker's window to open", poll=0.2, guest=40.0)
            except os88marty.MartyError:
                pass
        wins2 = dispcp.win_list(m, S)
        if len(wins2) < len(wins):
            fails.append("no window opened: the association did not run, or "
                         "Tracker refused the module")
            return report(fails)
        if trackwin is None:
            trackwin = wins2[-1]
        rec = m.read(S("wm_wins") + trackwin * dispcp.WIN_SIZE,
                     dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        say("  window     Tracker at %04X" % pseg)

        # WAIT FOR THE CLAIM, do not sleep for it. [trk_modseg] going
        # non-zero IS "the module is in memory", so the read costs what the
        # guest costs and not a flat twenty seconds - and on a slow box it
        # waits LONGER rather than reading a zero and blaming the decoder,
        # which is the failure a fixed sleep has.
        def claimed(mm):
            return int.from_bytes(mm.readseg(pseg, P["trk_modseg"], 2),
                                  "little")
        try:
            os88marty.until(m, claimed, "Tracker to claim the module",
                            poll=0.2, guest=60.0)
        except os88marty.MartyError:
            pass
        # ...and THE LOAD DONE, read rather than settled on: with no card
        # Tracker PLAYS what it loaded, through the speaker in its window
        # (SPEC.md 45.25), so the screen never stops changing and a settle
        # there waits out its whole budget
        try:
            os88marty.until(
                m, lambda mm: int.from_bytes(
                    mm.readseg(pseg, P["mp_loaded"], 2), "little"),
                "Tracker to finish the load", poll=0.2, guest=60.0)
        except os88marty.MartyError:
            pass
        modseg = claimed(m)
        if not modseg:
            msg = int.from_bytes(m.readseg(pseg, P["tui_msgp"], 2), "little")
            say("  status     %s" % next(
                (k for k, v in P.items() if v == msg and k.startswith("trk_s_")),
                "+%04x" % msg))
            fails.append(
                "[trk_modseg] is 0: Tracker opened and holds no module - a "
                "read that was REFUSED looks exactly like this" +
                (", and on THIS arm the first thing to check is SPEC.md "
                 "38.6.1: fdlg_sizeof answering the PACKED size is exactly "
                 "this failure, and the double-click arm passes through it "
                 "because OSAPI_FILE_FIND decodes the hint"
                 if a.dialog else
                 ", so check the kernel carries this format"))
            return report(fails)
        say("  module     claimed at %04X" % modseg)

        # NO CARD: Tracker pre-emphasises the samples IN PLACE once the load
        # is done (SPEC.md 45.25) - the speaker's filter paid once rather
        # than per output sample - so the bytes in memory are the file's with
        # each sample's play length through that filter. The ranges are read
        # out of Tracker's own sample table rather than re-derived, and the
        # filter applied to the file's bytes here: the header and patterns
        # must still match to the byte, and the samples to the filter.
        want = plain
        if "tsp_pre" in P:
            try:
                os88marty.until(
                    m, lambda mm: mm.readseg(pseg, P["tsp_pre"], 1)[0],
                    "the samples filtered", poll=0.2, guest=20.0)
            except os88marty.MartyError:
                pass
            if m.readseg(pseg, P["tsp_pre"], 1)[0]:
                want = bytearray(plain)
                tab = m.readseg(pseg, P["mp_smptab"], 31 * 12)
                for i in range(31):
                    sg, of, pl = (int.from_bytes(tab[i * 12 + k:i * 12 + k + 2],
                                                 "little") for k in (0, 2, 4))
                    at = (sg << 4) + of - (modseg << 4)
                    want[at:at + pl] = os88spkfx.tracker_natural(bytes(want[at:at + pl]))
                want = bytes(want)
                say("  samples    high-passed in place, a bass or a drum "
                    "squared up (no card, SPEC.md 45.25.4): compared "
                    "through tools/os88spkfx.py's tracker_natural")
        got = b""
        while len(got) < len(plain):        # 116KB, in segment-sized reads
            k = min(0x8000, len(plain) - len(got))
            got += m.readseg(modseg + (len(got) >> 4), 0, k)
        if got == want:
            say("  bytes      ok  (all %d, byte for byte)" % len(plain))
        else:
            bad = [i for i in range(len(plain)) if got[i] != want[i]]
            fails.append("%d of %d bytes differ, first at %d (0x%X) - which "
                         "is %s the 64KB boundary"
                         % (len(bad), len(plain), bad[0], bad[0],
                            "past" if bad[0] >= 0x10000 else "before"))

        r1 = m.readseg(pseg, P["mp_row"], 1)[0]
        try:                            # the replayer's next row, or three
            os88marty.until(            # guest seconds of none
                m, lambda _: m.readseg(pseg, P["mp_row"], 1)[0] != r1,
                "the replayer to move", poll=0.05, guest=3.0)
        except os88marty.MartyError:
            pass
        r2 = m.readseg(pseg, P["mp_row"], 1)[0]
        loaded = int.from_bytes(m.readseg(pseg, P["mp_loaded"], 2), "little")
        say("  loaded     %s  (mp_loaded=%d, row %d -> %d%s)"
            % ("ok " if loaded else "BAD", loaded, r1, r2,
               "" if r1 != r2 else " - no sound card on this machine, so the"
                                   " mixer has nothing to tick"))
        if not loaded:
            fails.append("Tracker read the bytes and did not accept them")

    return report(fails)


if __name__ == "__main__":
    sys.exit(main())
