#!/usr/bin/env python3
"""A LOADER hands its identity to one of its own parts (SPEC.md 20.12.10).

    make rehome && python3 tests/rehome.py [360|1440] [machine] [system-image]

REHOME.O88's image is a small loader over `apps/os88parts.inc`. It reads two
parts, writes a vector into the head of part 0's bss, calls
`OSAPI_PKG_REHOME`, and returns CF=0 with NO window. `ld_start`'s step 8a then
frees the loader's region, re-owns the carve to the instance slot, and runs
step 8 AGAIN against part 0 - which is a whole `.o88` image with its own
header, name, entry and bss.

WHAT THIS ROW ASSERTS, and each line of it goes red on a different half:

  1. the launch succeeded and the window belongs to the PROGRAM - `W_SEG` and
     `I_SPTR` are part 0's segment, not the loader's;
  2. the title is `REHOMED 4/4 OK`, which is the package's own four checks:
     the handoff arrived, the asset is where the loader said, it CAN claim
     memory (SPEC.md 50.3.4's whole gate - red without `mem_own`'s two arms)
     and it may NOT free its own carve - though it MAY now unpin it
     (SPEC.md 20.12.10.5, 66.6.1.2);
  3. the program's segment is NOT the base of any claim, which is what makes
     assertion 2's third check mean something - on a geometry where the head
     slack is zero it would pass by accident;
  4. THE LOADER'S REGION IS GONE FROM `mem_tab`. This is the feature: a claim
     based at the loader's old segment must not exist;
  5. the carve's `MC_OWN` is the instance SLOT and its `MC_RLOC` is SET - owned
     the way `ld_alloc` owns a region, and MOVABLE since SPEC.md 66.6.1.2;
  6. closing it returns the heap to the free runs it had before the launch.

WHY 360KB IS THE DEFAULT here where multiseg takes both: its clusters are 1KB,
so `op_claim`'s head slack is non-zero and part 0's segment is NOT the carve's
base - which is exactly the shape SPEC.md 50.3.4 exists for. Assertion 3 is
what says the run got that shape rather than assuming it.
"""
import struct
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88build
import os88mouse
import os88sym
import os88geom
import heapmap
from os88pkg import PKG_FMT
import dispcp

KIND_PKG = 0x80         # kernel/instance.inc - a package instance

argv = sys.argv[1:]
GEOM = argv[0] if argv else "360"
DEFAULT_MACHINE = {"1440": "os8088_5150_herc_gla_144"}.get(
    GEOM, "os8088_5150_cga_gla")
MACHINE = argv[1] if len(argv) > 1 else DEFAULT_MACHINE
SYS_IMG = argv[2] if len(argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/rehome.img" if GEOM == "1440" else "build/rehome360.img"

fails = []
def say(msg):
    print("      %s" % msg)

# --- what the FILE says, on the host ----------------------------------------
# The loader's image size decides where part 0 starts in the file, and part 0's
# own two header fields are the length the loader hands to OSAPI_PKG_REHOME.
# Read them here so the guest's numbers are checked against the build's rather
# than against themselves.
pkg = open(os88build.at("build/rehome.o88"), "rb").read()
LD_IMG = struct.unpack_from("<H", pkg, 8)[0]
P0_OFF = (LD_IMG + 511) // 512 * 512
P0_IMG, P0_BSS = struct.unpack_from("<HH", pkg, P0_OFF + 8)
say("build: loader image %d, part 0 at file+%d, its image %d + bss %d = %d"
    % (LD_IMG, P0_OFF, P0_IMG, P0_BSS, P0_IMG + P0_BSS))
if pkg[P0_OFF:P0_OFF + 2] != b"O8" or pkg[P0_OFF + 2] != PKG_FMT:
    sys.exit("rehome: part 0 of build/rehome.o88 is not a v3 package image - "
             "nothing below can run (SPEC.md 20.12.10.4)")

def claims(m, S):
    return heapmap.Map(m, {n: S(n) for n in
                           ("mem_base", "mem_top", "spl_live", "mem_tab")})

with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    S = m.sym
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    rows = dispcp.listing(m, S)
    if not any(n.upper() == "REHOME.O88" for n, _ in rows):
        sys.exit("rehome: REHOME.O88 is not on %s - run `make rehome`. It "
                 "lists %r" % (APPS_IMG, [n for n, _ in rows]))

    before = claims(m, S)
    wins_before = dispcp.win_list(m, S)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "REHOME.O88")
    os88marty.settle(m)
    status = m.read(S("ld_status"), 1)[0]
    after = dispcp.win_list(m, S)
    say("ld_status = %d, windows %r -> %r" % (status, wins_before, after))
    if status != 0 or len(after) <= len(wins_before):
        sys.exit("rehome: REHOME did not load (ld_status %d). 4 = the entry "
                 "proc refused, which for this package means op_load or "
                 "OSAPI_PKG_REHOME said no; the step 8a arm aborts the same "
                 "way (SPEC.md 20.12.10)" % status)

    # --- 1. the window belongs to the PROGRAM --------------------------------
    slot = after[-1]
    rec = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    wseg = struct.unpack_from("<H", rec, os88geom.W_SEG)[0]
    toff = struct.unpack_from("<H", rec, os88geom.W_TITLE)[0]
    title = m.read((wseg << 4) + toff, 24).split(b"\0")[0].decode(
        "ascii", "replace")
    hdr = m.read(wseg << 4, 32)
    name = hdr[16:32].split(b"\0")[0].decode("ascii", "replace")
    say("window seg %04X, header name %r, title %r" % (wseg, name, title))
    if name != "REHOMED":
        fails.append(
            "the window's segment holds a package called %r, not 'REHOMED'. "
            "The kernel is still running the LOADER: step 8a either did not "
            "fire or did not move [ld_base] (SPEC.md 20.12.10.1)" % name)
    if (struct.unpack_from("<H", hdr, 8)[0], struct.unpack_from("<H", hdr, 10)[0]) \
            != (P0_IMG, P0_BSS):
        fails.append(
            "the window's segment does not hold part 0's header: image/bss "
            "read %r where the file says %r"
            % ((struct.unpack_from("<H", hdr, 8)[0],
                struct.unpack_from("<H", hdr, 10)[0]), (P0_IMG, P0_BSS)))

    # --- 2. the package's own four checks ------------------------------------
    if title != "REHOMED 4/4 OK":
        fails.append(
            "REHOME's own verdict is %r and not 'REHOMED 4/4 OK'. Check 1 is "
            "the handoff in its bss, 2 the asset's segment, 3 OSAPI_MEM_CLAIM "
            "(SPEC.md 50.3.4 - red without mem_own's two arms) and 4 that its "
            "carve refuses both MEM_FREE and MEM_MOVABLE (20.12.10.5)" % title)

    # --- 3. ...and the program IS at a claim base, because the carve was TRIMMED
    # THE CARVE HAD TWO SHAPES and now has one (SPEC.md 20.12.10.5). op_claim's
    # head slack is the VOLUME's cluster alignment - zero on 512-byte clusters,
    # 1KB-512 on the 360KB disk's - and the re-home returns it to the heap, so
    # the claim's base is the program's segment at every geometry. The loader
    # wrote the carve's ORIGINAL base into the program's handoff (RP_CARVE)
    # before the kernel touched the claim, so the trim is read straight off
    # the two numbers: on 360KB the recorded base must be BELOW the program
    # and no claim may start there any more.
    now = claims(m, S)
    at_base = wseg in [c.seg for c in now.claims]
    rp_carve = struct.unpack_from("<H", m.read((wseg << 4) + P0_IMG + 4, 2))[0]
    say("the program is %s the carve's base; the loader recorded the carve at "
        "%04X (head slack %d paragraphs)"
        % ("AT" if at_base else "INSIDE", rp_carve, wseg - rp_carve))
    if not at_base:
        fails.append(
            "the program's segment %04X is not the base of any claim, so the "
            "re-home did not TRIM the carve to it - mem_reown_x's trim "
            "(SPEC.md 20.12.10.5) is what makes a re-homed region an ordinary "
            "one, and every compactor rule reads the base" % wseg)
    if GEOM == "360" and rp_carve >= wseg:
        fails.append(
            "the loader recorded its carve at %04X and the program is at "
            "%04X on a 360KB disk, whose 1KB clusters must leave a head slack "
            "- so this run had nothing to trim and tested nothing: REHOME's "
            "image must be an ODD number of sectors" % (rp_carve, wseg))
    elif GEOM != "360" and rp_carve != wseg:
        fails.append(
            "the loader recorded its carve at %04X and the program is at %04X "
            "on a 512-byte-cluster volume, where op_claim's head slack is zero "
            "by construction (SPEC.md 20.12.2). Something moved the run"
            % (rp_carve, wseg))
    if rp_carve != wseg and rp_carve in [c.seg for c in now.claims]:
        fails.append(
            "a claim still starts at the carve's old base %04X, so the slack "
            "was split off rather than returned" % rp_carve)

    # --- 4. THE LOADER'S REGION IS GONE --------------------------------------
    # The instance's I_SPTR is the program now, so the loader's old base is not
    # recorded anywhere to compare against - what CAN be said exactly is that
    # no claim is owned by a segment that is not a live package's, and that the
    # region count did not grow by two.
    inst = None
    for i in range(heapmap.INST_MAX):
        r = m.read(S("inst_tab") + i * os88geom.I_RECSZ, os88geom.I_RECSZ)
        if r[os88geom.I_STATE] and (r[os88geom.I_KIND] & KIND_PKG) \
                and struct.unpack_from("<H", r, os88geom.I_SPTR)[0] == wseg:
            inst = i
    say("instance slot %r has I_SPTR = %04X" % (inst, wseg))
    if inst is not None:
        rec = m.read(S("inst_tab") + inst * os88geom.I_RECSZ, os88geom.I_RECSZ)
        isize = struct.unpack_from("<H", rec, os88geom.I_SIZE)[0]
        say("I_SIZE = %d (want the PROGRAM's image + bss = %d)"
            % (isize, P0_IMG + P0_BSS))
        if isize != P0_IMG + P0_BSS:
            fails.append(
                "I_SIZE is %d and the program's image + bss is %d. Step 9 "
                "publishes [ld_need], which ld_alloc filled with the LOADER's "
                "region size - so the arm has to overwrite it. That word is "
                "the bound in instance.inc's 'an entry inside its own "
                "image+bss' fence, which is what OSAPI_FSX_RUN is checked "
                "against (SPEC.md 53.1): a full-screen package whose entry "
                "proc sits above the loader's old size is REFUSED, and Clear "
                "Skies is exactly that shape" % (isize, P0_IMG + P0_BSS))
    if inst is None:
        fails.append(
            "no live KIND_PKG instance has I_SPTR = %04X, so step 9 published "
            "the loader's segment and not the program's - [ld_base] was not "
            "moved before .call8 (SPEC.md 20.12.10.1)" % wseg)

    grew = len(now.claims) - len(before.claims)
    say("claims %d -> %d (+%d): %s"
        % (len(before.claims), len(now.claims), grew,
           ", ".join("%04X/%s" % (c.seg, heapmap.owner(c.own))
                     for c in now.claims if c not in before.claims)))
    carve = [c for c in now.claims if c.own == inst] if inst is not None else []
    if at_base:
        say("(the zero-slack shape: this claim IS the program's region, so "
            "mem_is_region holds, a move would rewrite I_SPTR correctly, and "
            "the declaration below is expected to have been TAKEN - "
            "SPEC.md 20.12.10.5. tests/rehomemove.py is what then moves it)")
    if len(carve) != 1:
        fails.append(
            "%d claims are owned by instance slot %r and exactly one should "
            "be - the carve, re-stamped from the loader's segment by "
            "mem_reown_x. The loader's own REGION was owned by this slot too, "
            "so two of them means it was never freed (SPEC.md 20.12.10.5)"
            % (len(carve), inst))
    else:
        c = carve[0]
        say("carve %04X..%04X %.1fK owner=slot %d rloc=%d door=%s"
            % (c.seg, c.end, c.kb, inst, c.rloc, "hi" if c.hi else "lo"))
        if not (c.seg <= wseg < c.end):
            fails.append(
                "the program at %04X is not inside the claim owned by its own "
                "slot (%04X..%04X) - the wrong claim was re-owned"
                % (wseg, c.seg, c.end))
        # THE DOOR NO LONGER TELLS THE TWO APART, AND THAT IS THE POINT
        # (SPEC.md 20.12.10.8). This read `if c.hi: fail` - the carve came in
        # bottom-up, the loader's region top-down, so the door was a free
        # discriminator. The carve is claimed TOP-DOWN now, through
        # mem_claim_hi_x like every other CS-based claim, so that test fails
        # on a correct machine and the discriminating work has already been
        # done above by the two checks that do not depend on a door: exactly
        # ONE claim on this slot (two means the loader's region was never
        # freed) and the program sitting INSIDE it.
        #
        # So the check is INVERTED rather than deleted, and it is still
        # load-bearing: a bottom-up survivor now means op_claim went back
        # through OSAPI_MEM_CLAIM, which is the regression SPEC.md 20.12.10.8
        # exists to prevent - it lands the carve at the FLOOR and leaves the
        # loader's freed region as a hole beneath it.
        if not c.hi:
            fails.append(
                "the surviving claim came in by the BOTTOM-UP door, so "
                "op_claim is not using mem_claim_hi_x: a claim that becomes "
                "a region must come in by a region's door, or it lands at "
                "the heap floor with the loader's freed region left as a "
                "hole under it (SPEC.md 20.12.10.8)")
        # --- 5. ...and its MC_RLOC, WHICHEVER SHAPE IT IS IN -----------------
        # The program declares itself movable either way (rhprog.asm's
        # rp_reloc) and the kernel takes it either way since SPEC.md 66.6.1.2.
        # **THIS USED TO ASSERT THE OPPOSITE FOR ONE OF THE TWO SHAPES** - a
        # non-zero head slack puts the program INSIDE its carve, which was
        # refused the declaration on a reading of the compactor that was
        # accurate and is now fixed. tests/rehomemove.py's `360` arm is what
        # then moves that shape; this row only says the declaration took.
        if c.rloc == 0:
            fails.append(
                "the carve's MC_RLOC is 0, so the program's region is PINNED "
                "- and rhprog.asm declares itself movable, so the kernel "
                "refused a declaration it should have taken. The program sits "
                "%s, and the second of those is the one that has to be said "
                "out loud: it was correctly refused there until SPEC.md "
                "66.6.1.2, because four places in the compactor read *the "
                "claim's base* where they meant *the segment the package runs "
                "in* - mem_is_region's equality, mem_frameless asking "
                "mem_in_nest about the wrong segment, mem_rr_walk matching "
                "the base alone, and mem_reloc_call dispatching PKG_DISP into "
                "the carve's head slack. A refusal here means one of those "
                "went back (SPEC.md 66.6.1, 66.6.1.2)"
                % ("AT the carve's base" if at_base
                   else "INSIDE the carve, a non-zero head slack up"))

    # --- 6. close it, and the heap comes back --------------------------------
    import os88ui
    ui = os88ui.UI(m, mouse=mo, sym=S)
    ui.menu_pick("REHOMED", "Close")
    os88marty.settle(m)
    end = claims(m, S)
    say("free runs before %r, after close %r"
        % ([(b, p) for b, p in before.runs()], [(b, p) for b, p in end.runs()]))
    if end.runs() != before.runs():
        fails.append(
            "the heap did not come back: free runs were %r before the launch "
            "and are %r after the close. The carve is owned by an instance "
            "SLOT now, so mem_free_rec_x's first sweep is what frees it "
            "(SPEC.md 20.12.10.6)" % (before.runs(), end.runs()))

if fails:
    print("\nrehome: FAIL on %s" % GEOM)
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\nrehome: a loader loaded its parts, handed its identity to one of "
      "them, and was FREED - the program runs in the carve, claims memory "
      "through SPEC.md 50.3.4's fence, and gives it all back. PASS on %s (%s)"
      % (GEOM, MACHINE))
