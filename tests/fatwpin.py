#!/usr/bin/env python3
"""The kernel's own FAT window is a PIN, and it is somebody's (SPEC.md 18.8.3).

    make && python3 tests/fatwpin.py

`FAT_SEG` used to be a fallback: 4,608 bytes reserved on every machine, used
by whatever volume had no claim, owned by nobody.  On a machine where every
volume won its heap claim those bytes were read and written by NOTHING while
the same 4,608 bytes were bought again out of the arena.  §18.8.3 makes it a
pin - one holder at a time, recorded in `dsk_fatwc[v]` exactly like a heap
window, tried first because it costs nothing, and taken away by the next
volume that needs a home and cannot fund one.

WHAT IT ASSERTS, and each row is a different way the design fails:

  1. **The saving is real.**  A: is mounted at the desktop and holds the pin,
     so there is NO `MEM_K_FATW` record in `mem_tab` at all.  Before this
     change there was one, and two once B: had been opened.  This row is the
     5,120 bytes.

  2. **Nobody is homeless.**  `dsk_fatwc[[disk_drive]]` is non-zero at every
     sample.  This is the whole safety argument: `dsk_fatw_pick` points a
     homeless volume at `FAT_SEG` and mounts into it WITHOUT demoting the
     holder, whose `dsk_fatww` would still claim a resident sector - and
     §18.8.2's signature cannot catch it, because two os8088-built floppies of
     one geometry have byte-identical boot sectors.

  3. **At most one holder, ever.**  Two rows in `dsk_fatwc` reading `FAT_SEG`
     means two volumes each believe the same bytes are theirs alone.

  4. **`[dsk_fatseg]` agrees with the table.**  The live pointer and the
     record are the two naming words §66.2 takes a proc for; they part
     silently and the symptom is a FAT read that succeeds with another
     volume's sectors.

  5. **The window still WORKS.**  A machine that satisfied 1-4 by never
     mounting anything would pass them all, so the run opens drive B:, which
     is a real mount: BPB, FAT window, directory.  A window has to appear.

  6. **FATWNONE=1 - the degradation.**  Every heap claim refused, so the whole
     machine runs on the pin and each mount evicts the last holder.  The
     promise is that this is no worse than the fallback it replaced, so the
     same five rows must hold with zero heap windows on the machine, and the
     pin must actually MOVE to whoever is mounted.

MartyPC's, on the 360KB pair, because that is the geometry the field runs and
the one whose mount is the most work.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from harness import check, done                             # noqa: E402

MACHINE = "os8088_5150_cga_gla"
# RELATIVE, and run from ROOT: an ABSOLUTE path handed to make matches no rule,
# so make finds the file already there and says "up to date" - silently, exit 0.
# The first run of this file rebuilt nothing for its FATWNONE=1 phase and
# measured the default kernel twice, which reads exactly like the knob having
# no effect.
IMG = "build/os8088-360.img"
APPS = "build/apps360.img"

DVOL_MAX = 8
MEM_MAX, MC_SIZE, MC_SEG, MC_OWN = 32, 10, 0, 4
# SPEC.md 18.8.4: a FAT window is a purgeable CACHE now, and its owner is a
# RANGE - MEM_P_FATW + the volume - not the single tag 0xFF05 it used to be.
MEM_PG_MED = 0xFD               # ...and MED, not LOW: a shed volume falls back
MEM_P_FATW, MEM_P_FATW_N = 0xFD20, 8   # to the pin, only one volume can hold
                                # that, so two that alternate reload nine
                                # sectors A SWITCH for as long as it is gone -
                                # 45 loads on 18.8.1's reference copy, not one


def build(*args):
    subprocess.run(["make", "-s"] + list(args), check=True, cwd=ROOT,
                   stdout=subprocess.DEVNULL)


def kernel_md5():
    import hashlib
    with open(os.path.join(ROOT, "build", "kernel.bin"), "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def w(m, lin):
    return int.from_bytes(m.read(lin, 2), "little")


class State:
    """One sample of the whole FAT-window world, defines-aware."""

    def __init__(self, m, defs=()):
        S = lambda n: os88sym.linear(n, defs)               # noqa: E731
        self.fat_seg = os88sym.equates(defs)["FAT_SEG"]
        self.drive = m.read(S("disk_drive"), 1)[0]
        self.fatseg = w(m, S("dsk_fatseg"))
        self.fatw0 = w(m, S("dsk_fatw0"))
        base_w, base_c = S("dsk_fatww"), S("dsk_fatwc")
        self.ww = [w(m, base_w + 2 * i) for i in range(DVOL_MAX)]
        self.wc = [w(m, base_c + 2 * i) for i in range(DVOL_MAX)]
        # mem_tab is .lowbss and reached through SS, so it needs its own segment
        tab = os88sym.segment_of("mem_tab", defs) * 16 + os88sym.syms(defs)["mem_tab"]
        raw = m.read(tab, MEM_MAX * MC_SIZE)
        self.fatw_claims = []            # (owner, segment) for every live one
        for i in range(MEM_MAX):
            seg = int.from_bytes(raw[i * MC_SIZE:i * MC_SIZE + 2], "little")
            own = int.from_bytes(
                raw[i * MC_SIZE + MC_OWN:i * MC_SIZE + MC_OWN + 2], "little")
            if seg and MEM_P_FATW <= own < MEM_P_FATW + MEM_P_FATW_N:
                self.fatw_claims.append((own, seg))

    @property
    def pinners(self):
        return [v for v, c in enumerate(self.wc) if c == self.fat_seg]

    def __str__(self):
        return ("drive %d  fatseg %04X (FAT_SEG %04X)  fatw0 %04X  "
                "wc %s  pin %s  heap windows %d"
                % (self.drive, self.fatseg, self.fat_seg, self.fatw0,
                   "[" + " ".join("%04X" % c for c in self.wc[:3]) + "]",
                   self.pinners,
                   len(self.fatw_claims)) + (
                   "  owners " + " ".join("%04X" % o for o, _ in self.fatw_claims)
                   if self.fatw_claims else ""))


def invariants(st, when, maxheap):
    """Rows 2, 3 and 4 - true at EVERY sample, whatever the build."""
    for own, seg in st.fatw_claims:
        vol = own - MEM_P_FATW
        check(own >> 8 == MEM_PG_MED,
              "%s: window owner %04X is PURGEABLE at MED" % (when, own),
              "SPEC.md 18.8.4 - the whole point is that the machine may take "
              "the window back. An owner outside the purge range is an "
              "ordinary claim that mem_shed_one will never look at",
              got="rank %02X" % (own >> 8), want="%02X" % MEM_PG_MED)
        check(st.wc[vol] == seg,
              "%s: owner %04X names volume %d's own window" % (when, own, vol),
              "mem_pg_own maps owner -> dsk_fatwc[owner - MEM_P_FATW], and "
              "zeroing that word IS the shed notice. If the owner a claim "
              "carries does not decode back to the volume that holds it, a "
              "shed clears SOMEBODY ELSE's record - which leaves one volume "
              "pointing at freed memory and another holding a claim nothing "
              "will ever free",
              got="dsk_fatwc[%d] = %04X, claim is %04X" % (vol, st.wc[vol], seg),
              want="equal")
    check(len(st.pinners) <= 1,
          "%s: at most one volume holds FAT_SEG" % when,
          "Two holders means two volumes each believe those 4,608 bytes are "
          "theirs alone. The second one to mount overwrites the first's FAT "
          "and the first's dsk_fatww still says a sector is resident, so its "
          "next quiet mount walks another disk's chain (SPEC.md 18.8.3)",
          got=st.pinners, want="0 or 1 volume")
    check(st.wc[st.drive] != 0,
          "%s: the mounted volume has a home" % when,
          "THERE IS NO THIRD STATE (SPEC.md 18.8.3). dsk_fatw_want must "
          "return with the mounting volume holding a claim or the pin. A "
          "homeless volume is pointed at FAT_SEG by dsk_fatw_pick and mounts "
          "into it without demoting the holder",
          got="dsk_fatwc[%d] = 0" % st.drive, want="a claim or FAT_SEG")
    check(st.fatseg == st.wc[st.drive],
          "%s: [dsk_fatseg] agrees with dsk_fatwc[drive]" % when,
          "The live pointer and the record are the two naming words SPEC.md "
          "66.2 takes a relocation PROC for rather than an address. Parted, "
          "the window reads the right bytes from the wrong place",
          got="%04X vs %04X" % (st.fatseg, st.wc[st.drive]), want="equal")
    check(len(st.fatw_claims) <= maxheap,
          "%s: at most %d MEM_K_FATW heap window(s)" % (when, maxheap),
          "The pin is tried FIRST and has no gate, so the kernel's own 4,608 "
          "bytes are spent before a single byte of arena is. One claim more "
          "than this means a volume bought a window it did not need - which "
          "is the 5,120 bytes SPEC.md 18.8.3 exists to give back",
          got=len(st.fatw_claims), want="<= %d" % maxheap)


def boot(m, defs=()):
    lin_live = os88sym.linear("spl_live", defs)
    lin_entry = os88sym.linear("cold_entry", defs)
    m.run()
    started, t0 = False, time.time()
    while time.time() - t0 < 300:
        live = m.read(lin_live, 1)[0]
        if not started:
            if live == 1 and m.read(lin_entry, 1)[0] == 0xE9:
                started = True
        elif live == 0:
            time.sleep(3.0)
            return
        time.sleep(0.2)
    raise SystemExit("fatwpin: never reached a desktop - nothing below would "
                     "mean what it says")


def phase(label, defs, maxheap, expect_move):
    """Boot, sample at the desktop, open B:, sample again."""
    print("\n--- %s ---" % label)
    S = lambda n: os88sym.linear(n, defs)                   # noqa: E731
    with os88marty.launch(IMG, apps=APPS, machine=MACHINE, boot=False) as m:
        boot(m, defs)
        at_desk = State(m, defs)
        print("  at the desktop : %s" % at_desk)
        invariants(at_desk, "%s, at the desktop" % label, maxheap)
        check(at_desk.pinners == [at_desk.drive],
              "%s: the boot volume took the pin" % label,
              "A: mounts before anything can have claimed, so the kernel's "
              "own window is free and dsk_fatw_want must take it rather than "
              "spend arena on a second copy of the same 4,608 bytes",
              got=at_desk.pinners, want=[at_desk.drive])

        wins0 = len(dispcp.win_list(m, S))
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle)
        time.sleep(3.0)
        after = State(m, defs)
        wins1 = len(dispcp.win_list(m, S))
        print("  after B: mounts: %s ; windows %d -> %d"
              % (after, wins0, wins1))

        check(wins1 > wins0,
              "%s: drive B: actually mounted" % label,
              "Rows that count claims all pass on a machine that never "
              "mounted anything. The window opening is what makes them mean "
              "something: it is a real BPB read, FAT window load and "
              "directory walk",
              got="%d -> %d windows" % (wins0, wins1), want="one more window")
        invariants(after, "%s, after B:" % label, maxheap)
        if expect_move:
            check(after.pinners == [after.drive],
                  "%s: the pin FOLLOWED the mount" % label,
                  "With every heap window refused the pin is the only home "
                  "there is, so each mount must evict the last holder and "
                  "take it. A pin that stayed put means the mounted volume "
                  "is reading somebody else's window",
                  got=after.pinners, want=[after.drive])
            check(after.ww[at_desk.drive] == 0xFFFF
                  or at_desk.drive == after.drive,
                  "%s: the evicted volume was told" % label,
                  "dsk_fatw_evict must clear the victim's dsk_fatww as well "
                  "as its dsk_fatwc. A stale sector number there is exactly "
                  "the zombie window dsk_fatw_park's comment describes",
                  got="dsk_fatww[%d] = %04X" % (at_desk.drive,
                                                after.ww[at_desk.drive]),
                  want="FFFF")


HEAPFRAG = "build/heapfrag360.img"


def phase_shed():
    """The live window SHED, and what the volume is left holding (18.8.4).

    tests/heapfrag builds a comb that fills the heap, claiming at the ordinary
    rank - which outranks MEM_PG_MED, so mem_shed_one takes the FAT window. It
    is the LIVE one, B: being mounted, which is the case the single-word purge
    protocol cannot express: [dsk_fatseg] and [dsk_fatw0] name it too, and left
    alone dsk_fat_window finds its sector already resident, returns WITHOUT
    loading, and the FAT reads and writes land in whatever claim now owns those
    paragraphs.
    """
    print("\n--- the live window, shed ---")
    S = os88sym.linear
    build(IMG, HEAPFRAG)
    with os88marty.launch(IMG, apps=HEAPFRAG, machine=MACHINE, boot=False) as m:
        boot(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        time.sleep(2.0)
        before = State(m)
        print("  B: mounted     : %s" % before)
        check(len(before.fatw_claims) == 1
              and before.fatseg == before.wc[before.drive]
              and before.fatseg != before.fat_seg,
              "shed: B: is live on a HEAP window before the pressure",
              "The rows below are about shedding the LIVE window. If B: were "
              "already on the pin there would be no claim to shed and every "
              "assertion after this would pass by vacuum",
              got=str(before), want="one heap window, and it is the live one")

        w = dispcp.win_list(m, S)
        wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "HEAPFRAG.O88")
        time.sleep(6.0)
        after = State(m)
        print("  heap filled    : %s" % after)

        check(not after.fatw_claims,
              "shed: the FAT window was actually taken",
              "heapfrag claims at the ordinary rank, which outranks "
              "MEM_P_FATW's MED, so mem_shed_one must take the window. If it "
              "did not, the tag is not purgeable and nothing below is a test "
              "of SPEC.md 18.8.4 at all",
              got=len(after.fatw_claims), want=0)
        check(after.wc[after.drive] == after.fat_seg,
              "shed: the live volume was handed the PIN on its way out",
              "dsk_fatw_demote's whole job. The bytes are lost either way - "
              "that is what a cache is - but the HOME must not be, or "
              "18.8.3's 'there is no third state' ends here and dsk_fatw_pick "
              "mounts into FAT_SEG without demoting whoever holds it",
              got="dsk_fatwc[%d] = %04X" % (after.drive, after.wc[after.drive]),
              want="%04X (FAT_SEG)" % after.fat_seg)
        check(after.fatw0 == 0xFFFF,
              "shed: [dsk_fatw0] says nothing is resident",
              "The second word the protocol does not carry. Left alone, "
              "dsk_fat_window finds the sector it wants already resident and "
              "returns without loading - and then every read and write goes "
              "to a segment somebody else owns now",
              got="%04X" % after.fatw0, want="FFFF")
        invariants(after, "shed, after", 0)


def main():
    build(IMG, APPS)
    plain = kernel_md5()
    phase("default", (), maxheap=1, expect_move=False)

    build("FATWNONE=1", IMG, APPS)
    knob = kernel_md5()
    try:
        # The two phases must be looking at two DIFFERENT kernels. Without this
        # row a build that silently did not happen reads as the knob having no
        # effect, which is how the first run of this file spent twenty minutes.
        check(knob != plain,
              "FATWNONE=1 actually rebuilt the kernel",
              "Both phases would otherwise measure the same binary and the "
              "FATWNONE rows below would be asserting nothing",
              got="md5 %s twice" % knob[:12], want="two different kernels")
        phase("FATWNONE=1", ("FATW_NONE",), maxheap=0, expect_move=True)
    finally:
        build(IMG, APPS)                    # leave build/ as `all` left it
    try:
        phase_shed()
    finally:
        build(IMG, APPS)
    return done("fatwpin")


if __name__ == "__main__":
    sys.exit(main())
