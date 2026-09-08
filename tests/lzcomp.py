#!/usr/bin/env python3
"""File > Compress, and the machine's stream against the host's, BYTE FOR BYTE.

docs/plans/O88-COMPRESSION-PLAN.md wave 6; SPEC.md 20.15 is the module and 22.22 the
verb. `os88lz.lzb_compress_machine` is a mirror of `kernel/compress.inc`
statement for statement rather than a model of its output, so the assertion
here is equality of the whole file - the 'CZ' header and every byte of the
stream - and not a ratio or a round trip.

THAT DISTINCTION IS THE ROW'S VALUE. A round-trip test passes on any encoder
that emits a decodable stream, which is every parse anybody could write; it
would have said nothing about the two bugs the first draft of the module had
(a lookahead that poisoned the slot it had just read, and a write bound tested
once a symbol rather than once a pass). Equality with a reference implementation
fails on the first byte that differs and says where.

Four subjects, and each is a different half of the verb:

  PLAIN.TXT   a plain file: read, compress, write. The result must be exactly
              'CZ' + LZB + the machine model's stream
  PACKED.TXT  the SAME bytes already wrapped LZ4 - what a shipped floppy
              carries. dskw_read_x hands back the unpacked bytes, so LZ4 -> LZB
              is the same code path and the result must be the same file
  CALC.O88    a PACKAGE (SPEC.md 22.22.1), which is not a 'CZ' file at all: the
              clear prefix stays in front, flags bits 3 and 4 go on, and the
              expected bytes come from os88pkg.compress_image with the machine
              encoder passed in - so the refusals, the prefix and the in-place
              arithmetic are stated ONCE and this test carries no copy of them.
              **And then it is double-clicked**, because the only thing that
              proves a package is still a package is the machine's own loader
              running it
  TELNET.O88  ...one the BUILD already compressed: refused, and untouched
  (again)     PLAIN.TXT once it is LZB: refused as already compressed, and the
              file untouched

...and then UNCOMPRESS (SPEC.md 22.23), where a ROUND TRIP is the strongest
assertion available rather than the weakest one: PLAIN.TXT and CALC.O88 both
went in as bytes this run knows exactly, so coming back byte for byte is a
statement about the pair that no ratio could make. The package is then opened
again, because the only thing that proves a package is a package is the
machine's loader running it - and the last leg picks Uncompress off the
RIGHT-CLICK menu instead of the bar, `fm_cxi_file`/`fm_cxc_file` being a
parallel pair of arrays that can be one item out and look perfectly right.

...and one thing that is not about the verb at all. **COPY/PASTE MUST NOT
EXPAND** (SPEC.md 20.14.3): the file manager's copy engine reads raw clusters
and takes its size from `dskw_stat`, so it moves a compressed file as it sits
and `dskw_czstamp` re-derives the hint at the other end - which nothing in the
tree asserted until this row did. The installer had the same job and got it
wrong (SPEC.md 52.10.13); `tests/instdeep.py` is that half.

The disk is read back with os88flush rather than by asking os8088 - the writer
and the reader here are one FAT12 implementation, so the one bug a write can
have that matters is the one that cannot be seen from inside (docs/FIELD-NOTES.md
4's rule).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88flush                                       # noqa: E402
import os88lz                                          # noqa: E402
import os88pkg                                         # noqa: E402
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
import dispcp                                          # noqa: E402
from os88geom import MBAR_H, MENU_ITEM_H, MB_ENTSZ      # noqa: E402

S = os88sym.linear
HERE = os.path.dirname(os.path.abspath(__file__))
MACHINE = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"}

MB_XL = 6                       # menu.inc's bar entry: the cell's left edge
_EQU = os88sym.equates()        # the kernel's own constants, so an item that
FM_ICOMP = _EQU["FM_ICOMP"]     # moves moves here too (SPEC.md 22.22/22.23)
FM_IUNCOMP = _EQU["FM_IUNCOMP"]
CX_IUNCOMP = 6                  # fm_cxi_file's seventh item: Open, Cut, Copy,
                                # Rename, Delete, Compress, Uncompress
TOAST_MAX = 24


def say(*a):
    print(*a, flush=True)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def menu_pick(m, mo, cell, item):
    """Drop menu-bar cell `cell` and pick item `item` - dispcalc's helper, and
    the x comes out of the kernel's own `menu_bar` for its reason: the bar is
    rebuilt whenever the owner changes, so its cells are a runtime fact."""
    t = m.read(S("menu_bar") + cell * MB_ENTSZ, MB_ENTSZ)
    x = u16(t, MB_XL) + 6
    mo.menu(x, 8, x, MBAR_H + 1 + item * MENU_ITEM_H + 8)


def toast(m):
    """What the last verdict said. Empty when nothing is up."""
    raw = m.read(S("toast_buf"), TOAST_MAX + 1)
    return raw.split(b"\0")[0].decode("latin-1")


def cz(body, u):
    """The 'CZ' container the verb must write (SPEC.md 20.14)."""
    return (b"CZ" + bytes([os88lz.LZB, 0])
            + u.to_bytes(4, "little") + body)


def stage(d, name, data):
    """One staged file, written only when its bytes CHANGE.

    os88disk names a file by its BASENAME, so the staging directory is what
    gives PACKED.TXT its name - and rewriting it every run would defeat
    scratch_disk's mtime test and rebuild the image forty times for nothing.
    """
    path = os.path.join(d, name)
    try:
        same = open(path, "rb").read() == data
    except OSError:
        same = False
    if not same:
        open(path, "wb").write(data)
    return path


def pkg_want(image):
    """What the verb must write over an UNCOMPRESSED package.

    os88pkg.compress_image with the machine's encoder passed in: the clear
    prefix, the flag bits, the "did it get smaller" test and the in-place
    layout arithmetic are all that function's, so this test states none of
    them (SPEC.md 22.22.1). None back means the host would refuse it too,
    which makes the fixture the wrong one rather than the machine wrong.
    """
    image = bytearray(image)
    img = int.from_bytes(image[8:10], "little")
    bss = int.from_bytes(image[10:12], "little")
    out = os88pkg.compress_image(bytearray(image), img, bss, image[3], "lzb",
                                 soft=True,
                                 packer=os88lz.lzb_compress_machine)
    if out is None:
        sys.exit("lzcomp: the host refuses to compress the package fixture, "
                 "so there is nothing for the machine to be compared against")
    return bytes(out)


def build_disk(path):
    plain = open(os.path.join(HERE, "lzcomp", "plain.txt"), "rb").read()
    packed, did = os88lz.cz_wrap(plain, os88lz.LZ4)
    if not did:
        sys.exit("lzcomp: the fixture does not compress under LZ4 - PACKED.TXT "
                 "would be the plain file and the LZ4 -> LZB leg would test "
                 "nothing")
    # The shipped .o88 files are LZ4 by default (PKGZ), so the package fixture
    # is the IMAGE back out of one - what nasm emitted, which is what an
    # uncompressed package on somebody's disk looks like.
    # `at` on both, because under a frozen run the shipped packages are in
    # the run's own tree and not in build/ (docs/plans/SOAK-PARALLEL.md 14.2) -
    # and these two ARE the fixture, so reading them out of a directory
    # somebody may be building in is a torn fixture rather than a missing one.
    calc = os88pkg.image_unwrap(
        open(os88build.at("build/calc.o88"), "rb").read())
    telnet = open(os88build.at("build/telnet.o88"), "rb").read()
    if len(telnet) >= int.from_bytes(telnet[8:10], "little"):
        sys.exit("lzcomp: build/telnet.o88 is not compressed, so the "
                 "'already compressed' package leg would test nothing - "
                 "this needs a default (PKGZ=lz4) build")
    d = os.path.join(os.path.dirname(path), "lzcomp")
    os.makedirs(d, exist_ok=True)
    sub = os.path.join(d, "sub")
    os.makedirs(sub, exist_ok=True)
    return os88marty.scratch_disk(
        path,
        stage(d, "PLAIN.TXT", plain),
        stage(d, "PACKED.TXT", packed),
        stage(d, "CALC.O88", calc),
        stage(d, "TELNET.O88", telnet),
        "SUB:" + stage(sub, "RAWCOPY.TXT", packed),
        size=360), plain, packed, calc, telnet


def compress(m, mo, wx, wy, name, fails, quiet=30, item=FM_ICOMP):
    """Select `name`, pick File > Compress, and wait for the verb to finish.

    THE WAIT IS ON THE TOAST and not on a fixed sleep: the pack is seconds of
    8088 at ~1,600 cycles a byte and the disk write is more, and a sleep long
    enough for the slowest subject is dead time on every other row. Every exit
    from the verb says something (SPEC.md 22.22.1), including the refusals, so
    a toast IS the completion signal.

    ...AND THE BUDGET IS THE GUEST'S CLOCK. `quiet` counts GUEST seconds, so
    what the verb is allowed is the same on a loaded box as an idle one -
    which for this row is the difference between a slow subject finishing and
    a row that reports "nothing was said" about a machine that was simply
    given a third less CPU (docs/plans/SOAK-PARALLEL.md 1).
    """
    m.write(S("toast_buf"), b"\0")          # ...so the previous verdict cannot
    row = dispcp.row_of(m, S, name)         # be read as this one's
    x, y = dispcp.row_xy(wx, wy, row)
    mo.click(x, y)
    os88marty.settle(m)
    menu_pick(m, mo, 1, item)               # cell 0 is the chip, 1 is File
    try:
        os88marty.until(m, lambda mm: toast(mm),
                        "File > %s on %s to say something"
                        % ("Uncompress" if item == FM_IUNCOMP else "Compress",
                           name),
                        poll=0.1, guest=float(quiet))
        return toast(m)
    except os88marty.MartyError:
        pass
    fails.append("%s: nothing was said in %d guest seconds - the verb never "
                 "finished, or it returned without a verdict" % (name, quiet))
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    a = ap.parse_args()

    for f in ("build/os8088-360.img", "build/hello.o88"):
        if not os.path.exists(os88build.at(f)):
            sys.exit("lzcomp: %s is missing - run `make` first" % f)

    disk, plain, packed, calc, telnet = build_disk("/tmp/lzcomp360.img")
    want = cz(os88lz.lzb_compress_machine(plain), len(plain))
    pwant = pkg_want(calc)
    say("lzcomp: %d bytes of prose -> %d expected (%.1f%%), LZ4 on the disk "
        "is %d" % (len(plain), len(want), 100.0 * len(want) / len(plain),
                   len(packed)))
    say("lzcomp: CALC.O88 %d -> %d expected (%.1f%%), %d bytes clear"
        % (len(calc), len(pwant), 100.0 * len(pwant) / len(calc),
           os88pkg.clear_prefix(calc[3])))
    fails = []

    with os88marty.launch(os88build.at("build/os8088-360.img"),
                          apps=disk, machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        fl = os88flush.Flush(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("lzcomp: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]

        # --- 0. COPY/PASTE MOVES A COMPRESSED FILE AS IT SITS ---------------
        # Before anything is modified, and it is not the verb's: the copy
        # engine reads raw clusters (dsk_read_chain) and sizes itself from
        # dskw_stat's RAW answer, so a compressed file crosses whole and
        # dskw_czstamp re-derives the hint from the bytes at the other end.
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "SUB")
        row = dispcp.row_of(m, S, "RAWCOPY.TXT")
        rx, ry = dispcp.row_xy(wx, wy, row)
        mo.click(rx, ry)
        os88marty.settle(m)
        menu_pick(m, mo, 2, 1)                  # Edit > Copy
        os88marty.settle(m)
        menu_pick(m, mo, 3, 3)                  # Nav > Up One Folder
        os88marty.settle(m)
        menu_pick(m, mo, 2, 2)                  # Edit > Paste
        # ...and the LISTING is what says the paste landed - the file
        # appearing in the folder, on the guest's clock.
        try:
            os88marty.until(
                m, lambda mm: any(n == "RAWCOPY.TXT"
                                  for n, _ in dispcp.listing(mm, S)),
                "the pasted RAWCOPY.TXT to appear", poll=0.2, guest=40.0)
        except os88marty.MartyError:
            pass
        got = fl.volume(1).read("RAWCOPY.TXT")
        ok = got == packed
        say("  copypaste  %s  (%d bytes, the source is %d, expanded is %d)"
            % ("ok " if ok else "BAD", len(got), len(packed), len(plain)))
        if not ok:
            fails.append("RAWCOPY.TXT came out of Copy/Paste at %d bytes and "
                         "the source is %d - a copy that EXPANDS is a copy "
                         "that used the transparent read (SPEC.md 20.14.3)"
                         % (len(got), len(packed)))

        # --- 1. a plain file ------------------------------------------------
        t = compress(m, mo, wx, wy, "PLAIN.TXT", fails)
        got = fl.volume(1).read("PLAIN.TXT")
        ok = got == want
        say("  plain      %s  %r  (%d bytes, wanted %d)"
            % ("ok " if ok else "BAD", t, len(got), len(want)))
        if not ok:
            i = next((k for k in range(min(len(got), len(want)))
                      if got[k] != want[k]), min(len(got), len(want)))
            fails.append("PLAIN.TXT: the machine's file and "
                         "lzb_compress_machine's first differ at byte %d "
                         "(%d bytes against %d)" % (i, len(got), len(want)))

        # --- 2. the SAME bytes, arriving LZ4 --------------------------------
        t = compress(m, mo, wx, wy, "PACKED.TXT", fails)
        got = fl.volume(1).read("PACKED.TXT")
        ok = got == want
        say("  lz4tolzb   %s  %r  (%d bytes, wanted %d)"
            % ("ok " if ok else "BAD", t, len(got), len(want)))
        if not ok:
            fails.append("PACKED.TXT: LZ4 -> LZB did not produce the same "
                         "file as plain -> LZB, and dskw_read_x hands both "
                         "paths the identical bytes (%d against %d)"
                         % (len(got), len(want)))

        # --- 3. a PACKAGE, which is not a 'CZ' file (SPEC.md 22.22.1) -------
        t = compress(m, mo, wx, wy, "CALC.O88", fails)
        got = fl.volume(1).read("CALC.O88")
        ok = got == pwant
        say("  package    %s  %r  (%d bytes, wanted %d)"
            % ("ok " if ok else "BAD", t, len(got), len(pwant)))
        if not ok:
            i = next((k for k in range(min(len(got), len(pwant)))
                      if got[k] != pwant[k]), min(len(got), len(pwant)))
            fails.append("CALC.O88: the machine's package and "
                         "os88pkg.compress_image's first differ at byte %d "
                         "(%d bytes against %d) - byte 3 is the flags and "
                         "%d bytes should be clear in front"
                         % (i, len(got), len(pwant),
                            os88pkg.clear_prefix(calc[3])))

        # --- 3a. ...and it still LOADS, which is the only proof that counts --
        before = dispcp.win_list(m, S)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "CALC.O88")
        try:
            os88marty.until(m,
                            lambda mm: len(dispcp.win_list(mm, S)) > len(before),
                            "the re-compressed CALC.O88 to open a window",
                            poll=0.2, guest=30.0)
        except os88marty.MartyError:
            pass
        after = dispcp.win_list(m, S)
        ok = len(after) > len(before)
        say("  runs       %s  (%d windows, was %d)"
            % ("ok " if ok else "BAD", len(after), len(before)))
        if not ok:
            fails.append("the compressed CALC.O88 opened no window - "
                         "ld_check_hdr or ld_expand refused a package this "
                         "machine had just written")

        # --- 4. one the BUILD already compressed ----------------------------
        before = fl.volume(1).read("TELNET.O88")
        t = compress(m, mo, wx, wy, "TELNET.O88", fails, quiet=8)
        after = fl.volume(1).read("TELNET.O88")
        ok = t.startswith("Already") and after == before
        say("  pkgalready %s  %r  (%d bytes, was %d)"
            % ("ok " if ok else "BAD", t, len(after), len(before)))
        if not ok:
            fails.append("TELNET.O88: expected 'Already compressed' and an "
                         "untouched file, got %r and %d bytes of %d"
                         % (t, len(after), len(before)))

        # --- 5. ...and the plain file again, now LZB ------------------------
        t = compress(m, mo, wx, wy, "PLAIN.TXT", fails, quiet=8)
        again = fl.volume(1).read("PLAIN.TXT")
        ok = t.startswith("Already") and again == want
        say("  already    %s  %r  (%d bytes)"
            % ("ok " if ok else "BAD", t, len(again)))
        if not ok:
            fails.append("PLAIN.TXT a second time: expected 'Already "
                         "compressed' and the same file, got %r and %d bytes"
                         % (t, len(again)))

        # --- 6. UNCOMPRESS a 'CZ' file, and it must be the ORIGINAL bytes ---
        # A round trip is a weak assertion about an ENCODER and the strongest
        # available one about this verb: PLAIN.TXT went in as prose, came back
        # as LZB, and has to come back again byte for byte. Anything the pair
        # loses - a length, the last symbol, the eight-byte container left on
        # the front - shows up here and nowhere else.
        t = compress(m, mo, wx, wy, "PLAIN.TXT", fails, item=FM_IUNCOMP)
        back = fl.volume(1).read("PLAIN.TXT")
        ok = t.startswith("Uncompressed") and back == plain
        say("  uncz       %s  %r  (%d bytes, the original is %d)"
            % ("ok " if ok else "BAD", t, len(back), len(plain)))
        if not ok:
            i = next((k for k in range(min(len(back), len(plain)))
                      if back[k] != plain[k]), min(len(back), len(plain)))
            fails.append("PLAIN.TXT did not survive compress -> uncompress: "
                         "%r, %d bytes against %d, first differing byte %d"
                         % (t, len(back), len(plain), i))

        # --- 7. ...and a PACKAGE, which is the other container -------------
        t = compress(m, mo, wx, wy, "CALC.O88", fails, item=FM_IUNCOMP)
        back = fl.volume(1).read("CALC.O88")
        ok = t.startswith("Uncompressed") and back == calc
        say("  unpkg      %s  %r  (%d bytes, the original is %d)"
            % ("ok " if ok else "BAD", t, len(back), len(calc)))
        if not ok:
            i = next((k for k in range(min(len(back), len(calc)))
                      if back[k] != calc[k]), min(len(back), len(calc)))
            fails.append("CALC.O88 did not survive the round trip: %r, %d "
                         "bytes against %d, first differing byte %d - byte 3 "
                         "is the flags, and bits 3 and 4 must be OFF"
                         % (t, len(back), len(calc), i))

        # --- 7a. ...and the expanded package still runs --------------------
        before = dispcp.win_list(m, S)
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "CALC.O88")
        try:                    # the WINDOW APPEARING is the event, on the
            os88marty.until(    # guest's clock and not the host's
                m, lambda mm: len(dispcp.win_list(mm, S)) > len(before),
                "the expanded CALC.O88 to open a window", poll=0.2,
                guest=20.0)
        except os88marty.MartyError:
            pass
        after = dispcp.win_list(m, S)
        ok = len(after) > len(before)
        say("  unruns     %s  (%d windows, was %d)"
            % ("ok " if ok else "BAD", len(after), len(before)))
        if not ok:
            fails.append("the EXPANDED CALC.O88 opened no window - the round "
                         "trip produced bytes ld_check_hdr will not start")

        # --- 8. a file that is not compressed at all ------------------------
        t = compress(m, mo, wx, wy, "PLAIN.TXT", fails, quiet=8,
                     item=FM_IUNCOMP)
        again = fl.volume(1).read("PLAIN.TXT")
        ok = t.startswith("Not compressed") and again == plain
        say("  unplain    %s  %r  (%d bytes)"
            % ("ok " if ok else "BAD", t, len(again)))
        if not ok:
            fails.append("PLAIN.TXT is plain now: expected 'Not compressed' "
                         "and an untouched file, got %r and %d bytes"
                         % (t, len(again)))

        # --- 9. THE RIGHT-CLICK MENU IS THE OTHER SURFACE (SPEC.md 22.23) --
        # Not a duplicate of leg 6. Under WF_FULL a file-manager window has no
        # menu bar at all (SPEC.md 11.2), so `fm_ctx_file` is the ONLY surface
        # either verb has there - and it is a separate descriptor with its own
        # parallel array of FMC_* bytes, which is exactly the kind of table
        # that can be one item out and look right.
        m.write(S("toast_buf"), b"\0")
        row = dispcp.row_of(m, S, "PACKED.TXT")
        rx, ry = dispcp.row_xy(wx, wy, row)
        mo.click(rx, ry)        # RAISE THE DISK WINDOW FIRST. Two Calculator
        os88marty.settle(m)     # instances are on screen by now (3a and 7a),
                                # and a right-press goes to whatever is under
                                # the pointer - which was this leg's whole
                                # failure the first time it ran: an empty
                                # toast and an untouched file, reported
                                # against the context menu and caused by the
                                # z-order

        def aim(_mo):           # ...and the item comes off the popup's OWN
            try:                # rect, because menu_popup SHIFTS rather than
                os88marty.until(m,      # clips near an edge - and menu_btn
                                lambda mm: mm.read(S("menu_btn"), 1)[0] == 2,
                                "the context menu to be up", poll=0.05,
                                guest=10.0)     # saying 2 is that rect being
            except os88marty.MartyError:        # written (SPEC.md 12.4)
                pass
            x = u16(m.read(S("menu_x1"), 2))
            y = u16(m.read(S("menu_y1"), 2))
            return x + 8, y + 1 + CX_IUNCOMP * MENU_ITEM_H + 8

        mo.rmenu(rx, ry, 0, 0, aim=aim)
        try:
            os88marty.until(m, lambda mm: toast(mm),
                            "the context menu's Uncompress to say something",
                            poll=0.1, guest=30.0)
        except os88marty.MartyError:
            pass
        t = toast(m)
        back = fl.volume(1).read("PACKED.TXT")
        ok = t.startswith("Uncompressed") and back == plain
        say("  ctxuncomp  %s  %r  (%d bytes, the original is %d)"
            % ("ok " if ok else "BAD", t, len(back), len(plain)))
        if not ok:
            fails.append("the context menu's Uncompress said %r and left "
                         "PACKED.TXT at %d bytes (the original is %d) - "
                         "fm_cxi_file and fm_cxc_file are a PAIR and an item "
                         "index out by one dispatches the wrong command"
                         % (t, len(back), len(plain)))

    for f in fails:
        say("  FAIL: " + f)
    say("lzcomp: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
