#!/usr/bin/env python3
"""A RE-HOMED program refuses itself, and nothing leaks (SPEC.md 20.12.10.6).

    make rehome && python3 tests/rehomeabort.py [machine] [system-image]

THE ONE UNWIND PATH NOTHING ELSE REACHES. By the time a re-homed program's
entry proc runs, `ld_start`'s step 8a has already freed the LOADER's region,
re-owned the parts carve to the instance SLOT, and pointed `[ld_base]` at the
program. If that entry then returns CF=1, `ld_unreserve` has to clean up an
arrangement no ordinary launch ever produces:

  - XMS by the instance RECORD, as always;
  - heap by the SLOT - which is the only thing that reaches the CARVE, the
    claim having no segment owner any more;
  - heap by `[ld_base]`, which is the PROGRAM's segment and reaches whatever
    the program itself claimed before it gave up.

`build/rehomeabort.img` is the same package built `-DRH_ABORT`: every check
runs, then it takes one claim of its own and returns CF=1. So a failure here
cannot be a package that never got going - tests/rehome.py runs the identical
source to a window.

THREE ASSERTIONS:
  1. the launch reports LD_EABORT (4) and no window appears - the refusal was
     taken rather than swallowed;
  2. no instance is left live, and no claim is owned by that slot or by the
     program's segment;
  3. THE HEAP COMES BACK BYTE FOR BYTE - the same free runs as before the
     launch. The carve is 2KB and the leak is invisible from the glass, which
     is why this is a row and not an argument.
"""
import struct
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88mouse
import os88geom
import heapmap
import dispcp

argv = sys.argv[1:]
MACHINE = argv[0] if argv else "os8088_5150_cga_gla"
SYS_IMG = argv[1] if len(argv) > 1 else "build/os8088-360.img"
APPS_IMG = "build/rehomeabort.img"

fails = []
def say(msg):
    print("      %s" % msg)

def claims(m, S):
    return heapmap.Map(m, {n: S(n) for n in
                           ("mem_base", "mem_top", "spl_live", "mem_tab")})

with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    S = m.sym
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]

    before = claims(m, S)
    wins_before = dispcp.win_list(m, S)
    try:
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name="REHOMEX.O88")
    except Exception as e:
        say("open refused (expected): %s" % str(e).splitlines()[-1].strip())
    os88marty.settle(m)

    # --- 1. the refusal was taken -------------------------------------------
    status = m.read(S("ld_status"), 1)[0]
    wins_after = dispcp.win_list(m, S)
    say("ld_status = %d (want 4 = LD_EABORT), windows %r -> %r"
        % (status, wins_before, wins_after))
    if status != 4:
        fails.append(
            "ld_status is %d and LD_EABORT is 4. The -DRH_ABORT build returns "
            "CF=1 from the re-homed entry proc, so anything else means the "
            "refusal never reached ld_start's `jc .abort` after .call8" % status)
    if len(wins_after) != len(wins_before):
        fails.append(
            "a window survived the refusal: %r -> %r. .abort sweeps by the "
            "W_SEG creator stamp BEFORE ld_unreserve, so a survivor's repaint "
            "would far-call a freed claim (SPEC.md 20.2)"
            % (wins_before, wins_after))

    # --- 2. nothing is left live --------------------------------------------
    live = []
    for i in range(heapmap.INST_MAX):
        r = m.read(S("inst_tab") + i * os88geom.I_RECSZ, os88geom.I_RECSZ)
        if r[os88geom.I_STATE] and (r[os88geom.I_KIND] & 0x80):
            live.append((i, struct.unpack_from("<H", r, os88geom.I_SPTR)[0]))
    say("live package instances: %r" % ([(i, "%04X" % s) for i, s in live],))
    for i, s in live:
        if m.read((s << 4) + 16, 8).split(b"\0")[0] == b"REHOMED":
            fails.append(
                "instance slot %d is still live at %04X and calls itself "
                "REHOMED. The record was never published (step 9 is past the "
                "refusal), so this is a slot ld_unreserve did not release"
                % (i, s))

    after = claims(m, S)
    say("claims %d -> %d" % (len(before.claims), len(after.claims)))

    # --- 3. THE HEAP COMES BACK ---------------------------------------------
    if after.runs() != before.runs():
        leaked = [c for c in after.claims if c not in before.claims]
        fails.append(
            "the heap did not come back. Free runs were %r before the launch "
            "and are %r after the refusal; what is left over is %r. The CARVE "
            "is owned by the instance SLOT after the re-home and by no "
            "segment at all, so ld_unreserve's `call ld_slot / call "
            "mem_free_owner_x` is the ONLY sweep that reaches it "
            "(SPEC.md 20.12.10.6)"
            % (before.runs(), after.runs(),
               ["%04X/%s %.1fK" % (c.seg, heapmap.owner(c.own), c.kb)
                for c in leaked]))
    else:
        say("free runs identical to before the launch: %r" % (after.runs(),))

if fails:
    print("\nrehomeabort: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\nrehomeabort: a re-homed program refused itself with the loader "
      "already freed and its carve owned by a slot - the refusal was taken, "
      "no window and no instance survived, and the heap came back byte for "
      "byte. PASS (%s)" % MACHINE)
