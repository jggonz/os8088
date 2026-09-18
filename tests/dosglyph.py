#!/usr/bin/env python3
"""A SHIPPED document glyph reaches every path the kernel fills a slot's
glyph by, and the glass (SPEC.md 54.3.2).

    make && python3 tests/dosglyph.py [machine]

tests/unit/t_docglyph.py is the host half: the validator, the baker and the
disk builder. This is the kernel's, and the kernel writes a slot's glyph in
four places - the BAKED table it boots with, the cache SEED at a volume
switch (asc_seed), a cache HIT at a mount (asc_note) and a cache MISS's
harvest (assoc_note_app) - each of which used to REDUCE the 16x16 icon and
now prefers the block the package ships. DOS is the package, its icon a CRT
the reduction empties, so what is asserted is that DOS's slot holds the
SHIPPED bytes, and never the reduction, after each of the four:

  1. BAKED: after a cold boot, before any full mount, the slot holds them
     (tools/os88mini.py baked the shipped block into the kernel).
  2. THE GLASS: a Disk window on a floppy with a .COM at its root draws the
     composed document icon - the page with the shipped glyph inset - and it
     is found in the window's own pixels (assoc_compose is unchanged; this
     says the whole chain reaches the screen).
  3. SEED: the slot is POISONED with the reduction, Drive A is opened (a
     volume switch, so ASSOC.DAT is loaded and asc_seed runs), and the slot
     holds the shipped bytes again.
  4. HIT: poisoned again, A:\\APPS is opened (DOS.O88's row is in the cache,
     so the harvest takes asc_note), and the slot holds them again.
  5. MISS: poisoned again, the STORE's DOS row is broken (its size word,
     which is half the lookup key), APPS/ is left and re-entered, so the
     harvest READS the sector and takes assoc_img_glyph off it - and the slot
     holds them again.
  6. A HIT ON WHAT STEP 5 STORED: APPS/ is left and re-entered once more, with
     NO poison, and the slot must be unchanged. Step 5 leaves a store row
     behind and the next visit is a hit on it; since SPEC.md 54.7.4 that row
     is where asc_take reads the shipped glyph, so a harvest storing the body
     and not the glyph left zeros there - and a hit on zeros REDUCES, undoing
     step 5. Reported from the field as ".EXE icons are back to the downsized
     full icon". Steps 1-5 each look once; this needs the second look.

The fifth writer, a runtime OSAPI_ASSOC_SET claim (assoc_self_glyph), is not
driven here: DOS makes none, and it goes through the same assoc_img_glyph as
step 5 with the package's own segment for the sector.

Each poison is checked to have TAKEN before the path under test runs, so a
pass says the path wrote the slot and not that nothing touched it. The
poison is the reduction rather than zeros because zero is the UNRESOLVED
sentinel and a writer that skips blanks would leave zeros alone by design.

The 1bpp pixel half runs on CGA and Hercules; on VGA it says so and asserts
the bytes alone (tests/assocglyph.py's reason).
"""
import os
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88build                                              # noqa: E402
import os88marty                                              # noqa: E402
import os88mouse                                              # noqa: E402
import os88sym                                                # noqa: E402
import os88mini                                               # noqa: E402
import dispcp                                                 # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
SYS_IMG = "build/os8088-360.img"
DOC_IMG = "build/doscom360.img"          # DOSHELLO.COM at the root
S = os88sym.linear
NAPP = 12                                # ASSOC_NAPP
PARK = (320, 190)
PIX1BPP = ("cga", "mda")
fails = []

# The page frame assoc_compose lays down (SPEC.md 54.3), data plane, and the
# inset: glyph row i lands in data row 5 + i, shifted left by 4.
PAGE = [0x3FE0, 0x2030, 0x2028, 0x203C] + [0x2004] * 11 + [0x3FFC]


def say(s):
    print("  " + s)


def shipped_and_reduced():
    """DOS's shipped glyph and what the reduction would have made of it."""
    with open(os88build.at("build/dos.o88"), "rb") as f:
        d = f.read()
    if not d[3] & 0x20:
        sys.exit("dosglyph: build/dos.o88 does not set flags bit 5 - this "
                 "test is about a package that SHIPS a glyph")
    rows = [int.from_bytes(d[64 + 2 * y:66 + 2 * y], "little")
            for y in range(16)]
    return bytes(d[112:120]), bytes(os88mini.reduce8(rows))


def slots(m):
    """{stem: (index, 8-byte glyph)} for every live app slot."""
    stem = m.read(S("assoc_stem"), NAPP * 8)
    glyph = m.read(S("assoc_glyph"), NAPP * 8)
    out = {}
    for i in range(NAPP):
        s = bytes(stem[i * 8:(i + 1) * 8])
        if s[0]:
            out[s.decode("latin1").rstrip()] = (i, bytes(glyph[i * 8:(i + 1) * 8]))
    return out


def dos_glyph(m):
    sl = slots(m)
    if "DOS" not in sl:
        sys.exit("dosglyph: no DOS slot in assoc_stem - SPEC.md 96 makes it a "
                 "built-in row, so this kernel is not the one described")
    return sl["DOS"]


def poison(m, idx, what):
    m.write(S("assoc_glyph") + idx * 8, what)
    got = dos_glyph(m)[1]
    if got != what:
        sys.exit("dosglyph: the poison did not take (%s against %s) - the "
                 "write went somewhere else" % (got.hex(), what.hex()))


def expect(m, step, shipped):
    got = dos_glyph(m)[1]
    ok = got == shipped
    say("%s: DOS's slot = %s%s" % (step, got.hex(),
                                   "" if ok else " (WANT %s)" % shipped.hex()))
    if not ok:
        fails.append("%s: the slot holds %s, not the shipped %s"
                     % (step, got.hex(), shipped.hex()))


def find_icon(m, mo, rect, glyph):
    """Search the window's pixels for the composed document icon."""
    mo.to(*PARK)
    os88marty.settle(m)
    w, h, rows = m.vram()
    x0, y0, rw, rh = rect
    data = list(PAGE)
    for i, b in enumerate(glyph):
        data[5 + i] |= b << 4
    want = [[(data[yy] >> (15 - xx)) & 1 for xx in range(16)]
            for yy in range(16)]
    hits = {0: [], 1: []}
    for y in range(y0, min(y0 + rh, h) - 16):
        for x in range(x0, min(x0 + rw, w) - 16):
            for pol in (0, 1):
                good = True
                for yy in range(16):
                    r = rows[y + yy]
                    for xx in range(16):
                        px = 1 if r[x + xx] else 0
                        if (px ^ pol) != want[yy][xx]:
                            good = False
                            break
                    if not good:
                        break
                if good:
                    hits[pol].append((x, y))
    return hits


def break_store_row(m):
    """Spoil the STORE's DOS row so the next lookup MISSES.

    IT WAS THE CACHE'S ROW, and the trick has moved one layer along rather
    than changed. SPEC.md 54.7.4 made ASSOC.DAT's claim a FILE BUFFER: its
    bodies and its shipped glyphs are absorbed into SPEC.md 25.9's
    machine-wide store and the claim is freed before asc_use returns, so
    there is nothing in memory to spoil by the time a harvest runs. What
    decides whether that harvest reads a sector is now `ico_have`, and it
    compares the same two things - so breaking the SIZE half of the
    (name, size) key is the same staging against the thing that now answers.

    The cache's VERSION is still asserted, out of [asc_rowsz]: asc_drop
    clears the segment and the counts and deliberately leaves the stride, so
    it still reports what the last volume's ASSOC.DAT was.
    """
    rowsz = int.from_bytes(m.read(S("asc_rowsz"), 2), "little")
    if rowsz != 88:
        fails.append("the last cache's row stride is %d, not 88 - the "
                     "system disk's ASSOC.DAT is not version 2" % rowsz)
    if int.from_bytes(m.read(S("asc_seg"), 2), "little"):
        fails.append("MEM_K_ASC is still claimed after a mount - SPEC.md "
                     "54.7.4 frees it, and tests/ascabsorb.py is the row "
                     "that owns that")
    seg = int.from_bytes(m.read(S("ico_seg"), 2), "little")
    n = m.read(S("ico_n"), 1)[0]
    eq = os88sym.equates()
    stride, r_size = eq["ICO_ROW"], eq["ICO_R_SIZE"]
    say("store at %04X, %d rows of %d bytes (cache was v%d)"
        % (seg, n, stride, 1 if rowsz == 80 else 2))
    if not seg or not n:
        sys.exit("dosglyph: the store is empty, so a miss cannot be staged")
    # WHAT NAMES THE PACKAGE is the kernel's, not this file's (SPEC.md 25.9.4):
    # kern_big keys on SPEC.md 54.2's eight-byte space-padded STEM and
    # kern_small on the twelve-byte 8.3 name, so the eight bytes at the front
    # of a row read differently per build.  Both are exactly eight; which one
    # is a question about ICO_R_SIZE and is asked here rather than assumed.
    want = b"DOS     " if r_size == 8 else b"DOS.O88\0"
    for i in range(n):
        row = seg * 16 + i * stride
        if bytes(m.read(row, 8)) == want:
            m.write(row + r_size, b"\xFF\xFF")
            say("row %d is %r: its size word is now 0xFFFF" % (i, want))
            return
    sys.exit("dosglyph: the store has no %r row - APPS/ was entered at "
             "step 4, so the absorb or the harvest stored nothing for it"
             % want)


shipped, reduced = shipped_and_reduced()
say("shipped %s, the reduction would be %s" % (shipped.hex(), reduced.hex()))
if shipped == reduced:
    sys.exit("dosglyph: the shipped glyph IS the reduction, so nothing here "
             "can tell the two apart")

with os88marty.launch(SYS_IMG, apps=DOC_IMG, machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    card = m.cmd(cmd="video")["type"]

    # --- 1. baked -----------------------------------------------------------
    idx, _ = dos_glyph(m)
    expect(m, "1 baked", shipped)

    # --- 2. the glass -------------------------------------------------------
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    names = [n for n, _ in dispcp.listing(m, S)]
    say("B:\\ = %r" % names)
    if "DOSHELLO.COM" not in names:
        sys.exit("dosglyph: %s has no DOSHELLO.COM at its root" % DOC_IMG)
    rect = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
    if card in PIX1BPP:
        hits = find_icon(m, mo, rect, shipped)
        n = len(hits[0]) + len(hits[1])
        say("2 glass: the composed icon found %d time(s) in the window %r"
            % (n, tuple(rect)))
        if n < 1:
            fails.append("the page-with-shipped-glyph icon is not in the "
                         "Disk window's pixels")
        bad = find_icon(m, mo, rect, reduced)
        if bad[0] or bad[1]:
            fails.append("the page-with-REDUCTION icon IS in the window")
    else:
        say("2 glass: card is %r, no 1bpp framebuffer - bytes only" % card)

    # --- 2b. ...AND THE CACHED COMPOSITION IS KEYED ON THE GLYPH ------------
    # The body just drawn is cached in SPEC.md 25.9's store, and it is the
    # only body there that is DERIVED rather than read: assoc_compose builds
    # it from the slot's glyph, which the baked table, the seed, a hit and a
    # harvest all write. A row keyed on the SLOT alone therefore outlives what
    # it was composed from - and the case that bites is SPEC.md 54.7.3's own,
    # one layer along: a slot assoc_app_new creates UNRESOLVED composes the
    # bare page, and the documents of that association go on drawing it for
    # the rest of the session even after the folder their program lives in is
    # browsed and the glyph resolved. So the glyph is IN the key (25.9.4), and
    # this reads it back out of the store rather than trusting that.
    eqs = os88sym.equates()
    seg = int.from_bytes(m.read(S("ico_seg"), 2), "little")
    n = m.read(S("ico_n"), 1)[0]
    docs = []
    for r in range(n):
        k = bytes(m.read(seg * 16 + r * eqs["ICO_ROW"], eqs["ICO_KEY"]))
        if k[0] == 0xFF:                       # a composed DOCUMENT row
            docs.append((r, k[1], k[2:10]))
    live = [d for d in docs if any(d[2])]
    say("2b store: %d document row(s), %d with a resolved glyph in the key"
        % (len(docs), len(live)))
    if not docs:
        fails.append("no composed document row in the store, so B:\ drew its "
                     ".COM with the generic icon and step 2 cannot have "
                     "passed for the reason it thinks")
    elif not live:
        fails.append("every document row's key carries eight ZERO bytes where "
                     "the composing glyph belongs - so one association is one "
                     "row for ever and a resolved glyph can never replace a "
                     "composition made before it (SPEC.md 25.9.4)")
    else:
        for r, slot, g in live:
            want = bytes(m.read(S("assoc_glyph") + slot * 8, 8))
            if g != want:
                fails.append("document row %d says slot %d composed from %s, "
                             "and that slot now holds %s - the key has drifted "
                             "from the thing it names"
                             % (r, slot, g.hex(), want.hex()))

    # --- 3. the seed, on a volume switch ------------------------------------
    poison(m, idx, reduced)
    dispcp.open_drive(m, mo, S, os88marty.settle, "A")
    expect(m, "3 seed (asc_seed, Drive A opened)", shipped)
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]

    # --- 4. a hit, at a mount -----------------------------------------------
    poison(m, idx, reduced)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    names = [n for n, _ in dispcp.listing(m, S)]
    if "DOS.O88" not in names:
        sys.exit("dosglyph: A:\\APPS has no DOS.O88: %r" % names)
    expect(m, "4 hit (asc_note, APPS/ entered)", shipped)

    # --- 5. a miss, and the harvest reads the sector ------------------------
    poison(m, idx, reduced)
    break_store_row(m)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "..")
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    expect(m, "5 miss (assoc_img_glyph off the sector)", shipped)

    # --- 6. ...AND A HIT ON THE ROW THAT MISS JUST WROTE ---------------------
    # The fifth writer is not the end of the chain, because step 5 leaves a
    # STORE ROW behind (SPEC.md 25.9) and the next visit to this folder is a
    # HIT on it. That row is the one asc_take reads the shipped glyph out of
    # since SPEC.md 54.7.4, so a harvest that stored the body and not the
    # glyph left a column of zeros - and a hit on zeros takes the `.body` arm
    # and REDUCES. Not a missing glyph: a WORSE one, overwriting what step 5
    # had just got right, after which every document of this association draws
    # with the reduction. Reported from the field as ".EXE icons are back to
    # the downsized full icon", measured here as 7effbfdfb1ff7e00 ->
    # 7e818181b17e3c00, and invisible to steps 1-5 because each of them looks
    # once and this needs the SECOND look.
    #
    # No poison: the whole point is that nothing but the kernel touches the
    # slot between step 5 and here, so a change is the defect by definition.
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "..")
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    expect(m, "6 hit on the harvested row", shipped)

if fails:
    print("dosglyph: FAIL")
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("dosglyph: ok - the shipped glyph survives the baked table, the seed, "
      "a hit and a miss, and is on the glass")
