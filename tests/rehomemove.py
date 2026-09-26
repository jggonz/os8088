#!/usr/bin/env python3
"""A RE-HOMED program's region moves, and its own vector follows (SPEC.md
20.12.10.5, 66.6.1).

    make rehome && python3 tests/rehomemove.py [machine] [system-image]

tests/rehome.py proves the re-home. This proves the block it leaves behind is
an ordinary movable region afterwards - and that the ONE THING no other
package in the tree has to fix, gets fixed.

**TWO GEOMETRIES, AND THE SECOND ONE IS THE EXPERIMENT.** `op_claim`'s head
slack is the gap between a part's 512-byte file boundary and the CLUSTER
boundary a read may start on. On a 512-byte-cluster volume it is ZERO, so the
program sits AT the carve's base and the carve is its region in the obvious
sense. At 360KB the slack is non-zero and the program sits INSIDE the carve -
and that shape was REFUSED the declaration until SPEC.md 66.6.1.2, on FOUR
separate readings of *the claim's base* that meant *the segment the package
runs in*: `mem_is_region`'s equality, `mem_frameless` asking `mem_in_nest`
about the wrong segment, `mem_rr_walk` matching the base alone, and
`mem_reloc_call` dispatching `PKG_DISP` into the carve's head slack.

So `python3 tests/rehomemove.py` is the easy shape and `... 360` is the shape
that was pinned. **The 360 arm is the gate**: every one of those four failures
is silent, and three of them would corrupt rather than refuse.

WHY THE RELOCATION PROC IS NOT A `ret`, which is what makes this row worth
having. `apps/os88api.inc`'s `OS88_REGION_MOVABLE` ships a bare `ret` because
every word naming an ordinary region belongs to the KERNEL - `W_SEG`,
`I_SPTR`, the owner of every claim - and `mem_region_reloc` puts those right.
A re-homed program has two words of its OWN: the loader's handoff named the
asset by absolute segment, and THE ASSET IS INSIDE THE CARVE, so it moves with
it. Nothing else in the machine knows those words exist. This row reads the
asset back THROUGH the fixed vector, which is the only way to tell a proc that
ran from one that was declared and forgotten (SPEC.md 66.2's own failure).

FIVE ASSERTIONS:
  1. the claim moved at all - without it the run proves nothing;
  2. the program's relocation proc was CALLED ([rp_moved]);
  3. the kernel's words followed - `I_SPTR`, and no claim owner or `I_SPTR`
     still names the old base;
  4. the PACKAGE's own word followed: the handoff names an address INSIDE the
     carve's new extent, one delta along, and the asset reads back through it.
     The address is the load-bearing half - a compaction does not scrub what
     it copied from, so a vector the proc never fixed still reads 'RA' 5AA5
     off the old copy. The break-it-on-purpose run is what found that;
  5. the window is still findable by title, which is `W_SEG` read back - a
     stale one does not fault, it makes the title line noise.
"""
import struct
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88build
import os88mouse
import os88geom
import heapmap
import dispcp

# **`360` PICKS THE GEOMETRY OF THE APPS FLOPPY, which is what decides the head
# slack** - not the system disk's, because the slack is a property of the
# volume the PACKAGE is read from (tests/rehome.py takes the same word for the
# same reason). A machine name may follow it.
argv = sys.argv[1:]
GEOM = "360" if argv and argv[0] == "360" else "1440"
argv = argv[1:] if GEOM == "360" else argv
if GEOM == "360":
    MACHINE = argv[0] if argv else "os8088_5150_cga_gla"
    APPS_IMG = "build/rehomemove360.img"
else:
    MACHINE = argv[0] if argv else "os8088_5150_herc_gla_144"
    APPS_IMG = "build/rehomemove.img"
SYS_IMG = argv[1] if len(argv) > 1 else "build/os8088-360.img"

# rhprog.asm's bss, which is the package's own layout and not the format's.
RP_HAND, RP_ASSET, RP_CARVE = 0, 2, 4
RP_OK, RP_ASEG, RP_MOVED = 6, 8, 16

fails = []
def say(msg):
    print("      %s" % msg)

def claims(m, S):
    return heapmap.Map(m, {n: S(n) for n in
                           ("mem_base", "mem_top", "spl_live", "mem_tab")})

def prog(m, S):
    """The live REHOMED instance: (slot, segment) or (None, None)."""
    for i in range(heapmap.INST_MAX):
        r = m.read(S("inst_tab") + i * os88geom.I_RECSZ, os88geom.I_RECSZ)
        if not r[os88geom.I_STATE] or not (r[os88geom.I_KIND] & 0x80):
            continue
        seg = struct.unpack_from("<H", r, os88geom.I_SPTR)[0]
        if m.read((seg << 4) + 16, 8).split(b"\0")[0] == b"REHOMED":
            return i, seg
    return None, None

def bss(m, seg):
    img = struct.unpack_from("<H", m.read(seg << 4, 32), 8)[0]
    return m.read((seg << 4) + img, 18)

def filler(m, S):
    """FILLER's (fl_done, fl_nask) - or None - read through its window, so a
    compaction that moves it is followed, and only while the gfx lock is FREE:
    its fill runs inside its first W_PAINT and an ask round inside W_ONKEY,
    both with the lock held, so the screen is stillest while it works."""
    if m.read(S("gfx_lock_flag"), 1)[0]:
        return None
    for w in os88geom.windows(m, S):
        if w.title.startswith("Filler"):
            seg = struct.unpack_from("<H", m.read(os88geom.winptr(m, w.i, S)
                                                  + os88geom.W_SEG, 2))[0]
            if not seg:
                return None
            img = struct.unpack_from("<H", m.read(seg << 4, 32), 8)[0]
            b = m.read((seg << 4) + img, 22)    # filler.asm's bss table:
            return b[20], struct.unpack_from("<H", b, 16)[0]  # done, nask
    return None

with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    S = m.sym
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    dispcp.open_named(m, mo, S, os88marty.settle, *disk, name="REHOME.O88")
    os88marty.settle(m)

    slot, seg0 = prog(m, S)
    if slot is None:
        sys.exit("rehomemove: REHOME did not re-home - no live instance is "
                 "called REHOMED (ld_status %d). tests/rehome.py is the row "
                 "that diagnoses that" % m.read(S("ld_status"), 1)[0])
    b = bss(m, seg0)
    asset0 = struct.unpack_from("<H", b, RP_ASSET)[0]
    carve = [c for c in claims(m, S).claims if c.own == slot]
    if len(carve) != 1:
        sys.exit("rehomemove: %d claims on slot %d, want 1" % (len(carve), slot))
    say("program at %04X (slot %d), asset at %04X, carve %04X..%04X rloc=%d"
        % (seg0, slot, asset0, carve[0].seg, carve[0].end, carve[0].rloc))
    # **THE HEAD SLACK IS THE SUBJECT AND NOT A PRECONDITION** (SPEC.md
    # 66.6.1.2). This used to `sys.exit` when the program sat INSIDE the carve
    # rather than at its base, on the ground that the declaration could not be
    # taken there - which was true, and was the defect: four separate places in
    # the compactor read *the claim's base* where they meant *the segment the
    # package runs in*, so the one shape that most needs to move was the one
    # that could not. The `360` arm exists to be in that shape.
    inside = carve[0].seg != seg0
    say("head slack %d paragraph(s), so the program sits %s"
        % (seg0 - carve[0].seg, "INSIDE the carve" if inside else "AT its base"))
    if carve[0].rloc == 0:
        sys.exit("rehomemove: the carve is PINNED (MC_RLOC 0), so nothing "
                 "below can move it. rhprog.asm declares itself movable and "
                 "OSAPI_MEM_MOVABLE refused%s (SPEC.md 66.6.1, 66.6.1.2)"
                 % (" - and the program is INSIDE the carve, which is "
                    "mem_find_own's containment arm gone" if inside else ""))

    # --- the forcing asks, tests/regmove.py's own idiom ----------------------
    # FILLER takes the arena down to a few tens of KB and then asks for one KB
    # more than the largest free run, so a GRANT means a compaction happened.
    # Each grant is given straight back and the next ask is made against a more
    # packed arena, which walks the ascending pass to exhaustion.
    dispcp.open_named(m, mo, S, os88marty.settle, *disk, name="FILLER.O88")
    try:
        os88marty.until(m, lambda _: (filler(m, S) or (0,))[0],
                        "the filler's fill", poll=0.3, limit=60)
    except os88marty.MartyError as e:
        say("(%s)" % e)
    os88marty.settle(m)
    for _ in range(6):
        was = (filler(m, S) or (0, None))[1]
        m.key("KeyA")
        try:                                # one round of asks, and the
            os88marty.until(                # compaction a grant made
                m, lambda _: (filler(m, S) or (0, was))[1] != was,
                "the filler's asks", poll=0.3, limit=30)
        except os88marty.MartyError as e:
            say("(%s)" % e)
        os88marty.settle(m)
        if prog(m, S)[1] != seg0:
            break

    slot2, seg1 = prog(m, S)
    now = claims(m, S)

    # --- 1. it moved ---------------------------------------------------------
    if seg1 is None:
        say("the REHOMED instance is gone - see assertion 5's note")
        fails.append(
            "no live instance is called REHOMED any more. Its I_SPTR either "
            "went stale (so the name is read out of freed memory) or the "
            "package died in its relocation proc (SPEC.md 66.2)")
    elif seg1 == seg0:
        fails.append(
            "the region never moved (%04X throughout), so this run proves "
            "NOTHING. The filler's asks did not reach a pass that could pack "
            "this claim - check the free-run dump above against "
            "tests/regmove.py's, whose setup this copies" % seg0)
    else:
        say("the region moved %04X -> %04X" % (seg0, seg1))

        # --- 2. our own proc RAN --------------------------------------------
        b2 = bss(m, seg1)
        moved = b2[RP_MOVED]
        say("[rp_moved] = %d, handoff asset %04X -> %04X, carve %04X"
            % (moved, asset0, struct.unpack_from("<H", b2, RP_ASSET)[0],
               struct.unpack_from("<H", b2, RP_CARVE)[0]))
        if moved == 0:
            fails.append(
                "the region moved and [rp_moved] is still 0, so the package's "
                "own relocation proc was never called. That is SPEC.md 66.2's "
                "'declared and forgot' arriving from the other side: the "
                "kernel took the declaration and did not dispatch it")

        # --- 3. the KERNEL's words followed ---------------------------------
        inst = m.read(S("inst_tab"), heapmap.INST_MAX * os88geom.I_RECSZ)
        sptrs = [struct.unpack_from("<H", inst,
                                    i * os88geom.I_RECSZ + os88geom.I_SPTR)[0]
                 for i in range(heapmap.INST_MAX)]
        owners = set(c.own for c in now.claims)
        if seg1 not in sptrs or seg0 in sptrs or seg0 in owners:
            fails.append(
                "a kernel word did not follow the move: I_SPTR %r, claim "
                "owners %r, old base %04X, new %04X. mem_rr_tab names "
                "inst_tab + I_SPTR and mem_tab + MC_OWN (SPEC.md 66.6.1)"
                % ([hex(s) for s in sptrs if s],
                   [hex(o) for o in sorted(owners)], seg0, seg1))

        # --- 4. ...AND THE PACKAGE'S OWN ONE --------------------------------
        # The assertion nothing else in the tree makes. Read the asset back
        # through the vector the proc was supposed to fix.
        asset1 = struct.unpack_from("<H", b2, RP_ASSET)[0]
        # THE SIGNATURE ALONE IS NOT ENOUGH, and the break-it run is what said
        # so: a compaction copies the block DOWN and does not scrub what it
        # came from, so a vector the proc never fixed still points at a
        # perfectly good copy of the asset and reads 'RA' 5AA5 exactly like a
        # fixed one. The load-bearing half is WHERE it points - inside the
        # carve's NEW extent - and the delta below.
        carve1 = [c for c in now.claims if c.own == slot2]
        if len(carve1) == 1 and not (carve1[0].seg <= asset1 < carve1[0].end):
            fails.append(
                "the handoff names %04X, which is OUTSIDE the carve's new "
                "extent %04X..%04X - so it is the address the asset used to "
                "be at. It still READS correctly, because a compaction does "
                "not scrub what it copied from, which is exactly why this "
                "assertion is about the address and not the bytes"
                % (asset1, carve1[0].seg, carve1[0].end))
        sig = m.read(asset1 << 4, 4)
        if sig != b"RA" + struct.pack("<H", 0x5AA5):
            fails.append(
                "the handoff vector names %04X and the bytes there are %r, "
                "not 'RA' + 0x5AA5. THE ASSET IS INSIDE THE CARVE, so it "
                "moved with it, and no kernel word names it - the package's "
                "own relocation proc is the only thing that can fix this "
                "(SPEC.md 20.12.10.2). It was %04X before the move"
                % (asset1, sig, asset0))
        else:
            say("the asset still reads 'RA' 5AA5 through the FIXED vector "
                "(%04X)" % asset1)
        if asset1 - asset0 != seg1 - seg0:
            fails.append(
                "the vector moved by %d paragraphs and the region by %d - the "
                "proc applied the wrong delta" % (asset1 - asset0, seg1 - seg0))

        # --- 5. the window still works --------------------------------------
        titles = [w.title for w in os88geom.windows(m, S) if w.visible]
        say("windows: %r" % (titles,))
        if not any(t.startswith("REHOMED") for t in titles):
            fails.append(
                "no visible window's title starts with 'REHOMED'. A W_SEG the "
                "move did not fix does not fault - the title is read THROUGH "
                "it, so it comes back as line noise and the window is no "
                "longer findable by name (tests/regmove.py saw exactly this "
                "with mem_region_reloc stubbed to `ret`): %r" % (titles,))

if fails:
    print("\nrehomemove: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\nrehomemove: a re-homed program's carve is an ordinary movable region "
      "- it packed down under the compactor, the kernel's words followed, and "
      "the package's OWN vector into the part beside it followed too. PASS "
      "(%s, apps %s)" % (MACHINE, GEOM))
