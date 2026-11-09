#!/usr/bin/env python3
"""A LAZY part is not read at load, and it can be given back (SPEC.md 20.12.4).

    make mseg && python3 tests/mseglazy.py [machine] [system-image]

Goal 3 of the whole design is "load only some minimal amount, so a package
does not read for ages only to find out it is going to be refused"
(docs/plans/completed/O88-MULTISEG-PLAN.md 0). Waves 2 and 3 answered the second half of that
sentence - a refusal costs no disk. `OP_LAZY` answers the first: a part the
package declares, sizes and NEVER READS until it wants it.

MSEG's part 6 is that part, and it is the biggest of the five modules on
purpose - 3,051 bytes, six sectors - because what lazy buys is measured in the
sectors the launch did not move. **A KEY FETCHES IT**, not the entry proc: a
fetch inside the entry proc happens during the launch, so its sectors would be
indistinguishable from the carve's. On a key they are their own measurement,
and they are also what a lazy part is FOR - a working set that arrives when
the user asks for the thing that needs it, and goes away again.

FIVE ASSERTIONS:

  1. AFTER THE LAUNCH IT IS NOT THERE. op_seg answers 0 for part 6, and MSEG's
     own verdict says `MSEG 7/7 OK` - because MSEG checks part 6 against what
     it ASKED for and not against presence, so "absent" is the correct answer
     at this point and "present" would be a failure;

  2. AND NO READ AT LOAD COULD HAVE COVERED IT. This is the structural half,
     and it is what makes assertion 1 mean something: the carve is ONE run,
     from [op_first] for [op_secs] sectors, and this row reads both out of the
     guest and checks that the run ENDS BEFORE part 6's first sector. A
     package whose op_size forgot to step over a lazy row would have a carve
     that reaches it, and every byte of it would arrive with the rest;

  3. THE KEY FETCHES IT, and the fetch is where the disk goes. Sectors moved
     across the key press (dsk_dbg_sec, which is why this row builds a DISKCNT
     kernel; Bochs's FDC log on kern_mega) must be at least the part's own
     six, and afterwards op_seg answers a segment, MSEG's three independent
     module checks pass on it, and ms_lazy says 'F'. A sector the READ-AHEAD
     already held (SPEC.md 18.95) counts when the fetch is seen being served
     out of that chunk - its RAH_END moving past it. kern_mega's B: on Bochs
     is that case: part 5 is OP_XMS there and read alone, its run split at
     the track, and the second piece's fill caches the rest of the track -
     all seven of part 6's LBAs - in the call that was reading part 5
     anyway, so the fetch moves NOTHING and the count read 0. The cache
     doing its job is not the launch reading the part (no claim, no byte of
     the package's, no extra int 13h); a fetch that skips its read is still
     red, on the cache's account as well as on MSEG's checks;

  4. A SECOND KEY GIVES IT BACK. op_seg answers 0 again, ms_lazy says 'D', and
     the claim table is BYTE-FOR-BYTE what it was before the fetch. A lazy
     part that is only ever fetched is a cost deferred and not avoided - the
     claim slot and the KB are both still spent for the life of the instance -
     so op_drop is what makes the mechanism a saving;

  5. AND IT IS REPEATABLE. A third key fetches it again and the checks pass
     again, which is what says op_drop left the row and not just the heap in a
     fit state.

VERIFIED TO FAIL, and the way it failed is the argument for assertion 2. With
op_size's `OP_LAZY -> .next` taken out, so a lazy row is sized and carved like
any other, the carve runs to sector 25 instead of 19 and reads all six sectors
of part 6 at load - and **assertion 1 does not notice**. op_seg answers a lazy
row out of the row itself, which is still 0 until op_fetch writes it, so the
package sees "not here" while every byte of it has already been paid for. Only
assertion 2 fires, because only assertion 2 looks at the run.

That is the whole reason a "did the launch read it" gate cannot be written as
"is it there": presence is what the package was told, and the carve is what
the disk actually did.
"""
import os
import struct
import subprocess
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
sys.path.insert(0, "tests/multiseg")
import os88build
import os88marty
import os88mouse
import os88fat
import os88parts
import os88sym
import os88geom                                              # noqa: E402
import dispcp
import msegsym

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KNOBS = ["DISKCNT=1"]
# **DERIVED, NEVER RESTATED** (docs/plans/SOAK-PARALLEL.md 8.4). A knob's make
# VARIABLE and its nasm DEFINE are not the same string, and this line used to
# be the second copy of that mapping: `["DISK_COUNTERS"]` against `os88build`'s
# `('DISK_COUNTERS', 'KERN_BIG', 'KERN_KNOB')`. It assembled - which is exactly why it survived - but as
# a DIFFERENT cache key from the one `Tree.apply()` had just set, so the row
# re-assembled the kernel a second time to get the same answer. `fddpark`
# is the class that died of this mirror going stale; `tree()` asks make.
DEFINES = ()                    # filled from the tree in main()
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/mseg.img"
O88 = "build/mseg.o88"
LAZY = 6                            # a SEMANTIC index; everything else about
                                    # the row is read out of the package
MEM_MAX, MC_SIZE, MC_SEG, MC_OWN = (os88geom.MEM_MAX, os88geom.MC_SIZE,
                                    os88geom.MC_SEG, os88geom.MC_OWN)
fails = []


def say(s):
    print("  " + s)


def claims(m, S):
    """Every live claim, as (segment, owner) - msegnomem's reader."""
    blob = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        r = blob[i * MC_SIZE:(i + 1) * MC_SIZE]
        seg = struct.unpack_from("<H", r, MC_SEG)[0]
        if seg:
            out.append((seg, struct.unpack_from("<H", r, MC_OWN)[0]))
    if getattr(m, "bochs", False):
        # kern_mega: a claim may be a POOL block above 1MB with no mem_tab
        # record at all, so the pool's own table is part of "the claims"
        # (kernel/mega.inc's mx_tab: MX_OFF, MX_KB, MX_OWN; 96 x 10 bytes)
        mx = m.read(m.tail("mx_tab", DEFINES), 96 * 10)
        out += [("pool", mx[k] | mx[k + 1] << 8, mx[k + 2] | mx[k + 3] << 8,
                 mx[k + 4]) for k in range(0, len(mx), 10)
                if mx[k + 2] | mx[k + 3] << 8]
    return out


def lin(m, seg, off=0):
    """SEG:OFF's linear address - an LDT selector's base on kern_mega
    (OS88_MEGA, docs/BOCHS.md), `seg << 4` everywhere else."""
    return m.lin(seg, off) if getattr(m, "bochs", False) else (seg << 4) + off


def word(m, seg, name):
    return struct.unpack_from(
        "<H", m.read(lin(m, seg, msegsym.sym(name)), 2), 0)[0]


def byte(m, seg, name):
    return m.read(lin(m, seg, msegsym.sym(name)), 1)[0]


def part_seg(m, seg, i):
    """op_seg's answer for part i, out of MSEG's own banked copy."""
    return struct.unpack_from(
        "<H", m.read(lin(m, seg, msegsym.sym("ms_seg") + i * 2), 2), 0)[0]


def part_lbas(row):
    """The LBAs on B: holding part ROW's bytes - MSEG.O88's cluster chain
    out of the image itself (tools/os88fat.py), so nothing about where the
    file landed is assumed."""
    v = os88fat.Fat12(APPS_IMG)
    e = v.find("MSEG.O88")[2]
    if e is None:
        raise SystemExit("mseglazy: no MSEG.O88 in the root of %s" % APPS_IMG)
    lbas = [v.cluster_lba(c) + k
            for c in v.chain(struct.unpack_from("<H", e, 26)[0])
            for k in range(v.spc)]
    lo = os88parts.byte_off(row)
    return set(lbas[lo // 512:(lo + row["len"] - 1) // 512 + 1])


def rah_held(m, S, lbas):
    """{lba: (slot, chunk's first LBA, RAH_END)} for every LBA of LBAS a live
    read-ahead chunk of B:'s holds (SPEC.md 18.95; tests/ldcost.py reads the
    same table). On kern_mega the STORE is above 1MB but the table is .bss."""
    e = os88sym.equates(DEFINES)
    rec = e["RAH_REC"]
    rd = lambda n, k: bytes(m.read(S(n), k))                     # noqa: E731
    runs = struct.unpack_from("<H", rd("dsk_rah_runs", 2))[0]
    sig = struct.unpack_from("<H", rd("dsk_sigcur", 2))[0]
    tab = rd("dsk_rah_tab", e["DSK_RAH_RUNS"] * rec)
    out = {}
    for i in range(runs):
        r = tab[i * rec:(i + 1) * rec]
        lba, n, rsig = (struct.unpack_from("<H", r, e[f])[0]
                        for f in ("RAH_LBA", "RAH_N", "RAH_SIG"))
        if not n or r[e["RAH_VOL"]] != 1 or rsig != sig:
            continue
        for x in lbas & set(range(lba, lba + n)):
            out[x] = (i, lba, r[e["RAH_END"]])
    return out


def title_of(m, S, seg, rec):
    import os88geom
    toff = struct.unpack_from("<H", rec, os88geom.W_TITLE)[0]
    return m.read(lin(m, seg, toff), 24).split(b"\0")[0].decode(
        "ascii", "replace")


def build_counted():
    """The counted kernel, in a PRIVATE TREE (tools/os88build.py).

    It used to go into build/ with a `make` in a `finally` to put the plain
    kernel back - two full builds, and in between any row reading the tree
    would have driven a DISKCNT kernel nobody asked for.
    """
    global SYS_IMG, APPS_IMG, O88, DEFINES
    say("building the counted kernel (DISKCNT=1)...")
    # `mseg` COMES ALONG, and that is a fix rather than tidiness: this row's
    # own usage line says `make mseg && python3 tests/mseglazy.py`, mseg is NOT
    # in `all`, and the row never built it - so on any tree where somebody had
    # not typed that by hand it died with FileNotFoundError on build/mseg.o88.
    # That is the ABSENT-artefact shape exactly: an absent gate
    # reading as a failing one. A private tree has to name what it wants, and
    # naming it is what fixed it.
    t = os88build.tree(*KNOBS, targets=("os8088-360.img", "mseg")).apply()
    DEFINES = t.defines         # ...and apply() has already set the default
    SYS_IMG = t.img("os8088-360.img")
    APPS_IMG = t.img("mseg.img")
    O88 = t.img("mseg.o88")


def run():
    import os88geom
    S = lambda n: os88sym.linear(n, defines=DEFINES)     # noqa: E731
    blob = open(O88, "rb").read()
    rows = os88parts.image_rows(blob)
    row = rows[LAZY]
    lazy_secs = os88parts.sectors(row)
    if not row["flags"] & os88parts.EQU["OP_LAZY"]:
        raise SystemExit("mseglazy: part %d of %s is not OP_LAZY (flags "
                         "0x%02X) - this row has nothing to measure"
                         % (LAZY, O88, row["flags"]))
    say("part %d: granule %d (file byte %d), %d bytes, %d sectors"
        % (LAZY, row["off"], os88parts.byte_off(row), row["len"], lazy_secs))

    # Every read below is guest state (the window list, MSEG's bss, the
    # claim table, the sector count): the boot gates on the kernel's desktop
    # state and every wait is `ui_done`, never a picture settle.
    done = lambda mm, **k: os88marty.ui_done(mm)
    with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE,
                          boot=False) as m:
        m.run()
        os88marty.kernel_up(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, done, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, done, wx, wy, "MSEG.O88")
        os88marty.ui_done(m)

        slots = dispcp.win_list(m, S)
        seg = 0
        for i in slots:
            rec = m.read(S("wm_wins") + i * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
            sg = struct.unpack_from("<H", rec, os88geom.W_SEG)[0]
            if sg and title_of(m, S, sg, rec).startswith("MSEG "):
                seg, wrec = sg, rec
        if not seg:
            raise SystemExit("mseglazy: MSEG did not launch - nothing below "
                             "can be asked. `python3 tests/multiseg.py` is "
                             "the row that says why.")
        if MEGA:
            # NO KNOB KERNEL ON kern_mega: Bochs's controller names every
            # sector it moves (the shim's xfers()), so the count is B:'s reads
            # - and the LBAs themselves, kept for the cache's account below
            got = set()

            def count():
                x = [o // 512 for d, o, w in m.xfers() if d == 1 and w == "r"]
                got.update(x)
                return len(x)
        else:
            secs = S("dsk_dbg_sec")

            def count():
                return struct.unpack_from("<H", m.read(secs, 2), 0)[0]

        def moved(before):
            if MEGA:
                return count()      # (xfers() is since the last call)
            return (count() - before) & 0xFFFF

        # --- 1. it is not here, and MSEG agrees that is correct ------------
        t = title_of(m, S, seg, wrec)
        ps = part_seg(m, seg, LAZY)
        say("after the launch: op_seg(%d) = %04X, verdict %r, ms_lazy %r"
            % (LAZY, ps, t, chr(byte(m, seg, "ms_lazy"))))
        want = "MSEG %d/%d OK" % (len(rows), len(rows))

        def bad():
            """WHICH parts did not pass - a bitmask MSEG keeps beside the
            count, so a `6/7` here names the row instead of leaving the reader
            to reproduce three checks host-side."""
            v = word(m, seg, "ms_bad")
            return [i for i in range(len(rows)) if v & (1 << i)]
        if ps:
            fails.append(
                "part %d came back at segment %04X after the LAUNCH. A lazy "
                "part is one op_size steps over: not claimed, not read, and "
                "op_seg answering 0 until op_fetch has been called (SPEC.md "
                "20.12.4)" % (LAZY, ps))
        if t != want:
            fails.append(
                "MSEG says %r and should say %r. Part %d is checked against "
                "what the package ASKED for and not against presence, so "
                "absent is correct here - this verdict failing means either "
                "the lazy row is being treated as an ordinary one or one of "
                "the other six broke - the parts that did not pass are %r"
                % (t, want, LAZY, bad()))

        # --- 2. ...and the carve could not have reached it ----------------
        first = word(m, seg, "op_first")
        run_secs = word(m, seg, "op_secs")
        end = first + run_secs
        say("the carve is granules %d..%d; part %d starts at %d"
            % (first, end - 1, LAZY, row["off"]))
        if end > row["off"]:
            fails.append(
                "the carve runs to granule %d and part %d starts at %d, so the "
                "ONE READ the carve exists to be covered it. This is the "
                "structural half of assertion 1 and the half that cannot be "
                "faked: op_size must step over a lazy row, and os88pkg.py "
                "puts every lazy part after the run so that stepping over one "
                "cannot leave a hole in the middle of it (SPEC.md 20.12.4)"
                % (end - 1, LAZY, row["off"]))

        before = claims(m, S)

        # --- 3. the key, and the disk goes HERE ---------------------------
        # ...or the READ-AHEAD does (SPEC.md 18.95). A fill caches the rest
        # of the track past whatever missed, so the launch's read of the part
        # BEFORE this one can leave part 6's sectors in a chunk, and the
        # fetch is then served out of it with no transfer at all. That is the
        # cache doing its job and not the loader reading the part: no claim,
        # no byte of the package's, no int 13h the launch would not have
        # made (the fill is the call that was reading part 5 anyway). It is
        # what kern_mega's B: does on Bochs - part 5 is OP_XMS there, read
        # alone through op_xload, its 3 sectors split at the track and the
        # second piece filling 9 - and the count was 0. So the account is
        # per SECTOR: each one the fetch needs either moves at the key or
        # sits in a live chunk before it, and a chunk that excuses one must
        # then show the fetch's read MOVING its end past it (RAH_END)
        need = part_lbas(row)
        held0 = rah_held(m, S, need)
        c0 = count()
        if MEGA:
            got.clear()
        os88marty.key_taken(m, "KeyL")
        os88marty.ui_done(m)
        fetch_secs = moved(c0)
        held1 = rah_held(m, S, need)
        served = {lba for lba, (slot, base, end) in held0.items()
                  if lba in held1 and held1[lba][:2] == (slot, base)
                  and end != held1[lba][2] > lba - base}
        if served:
            say("...and the read-ahead held %d of its %d sectors (LBA %d..%d)"
                " and served them to the fetch"
                % (len(served), len(need), min(served), max(served)))
        stale = sorted(set(held0) - served)
        if stale:
            fails.append(
                "the read-ahead held part %d's LBAs %r before the key and the "
                "fetch's read did not end past them in that chunk (RAH_END) - "
                "so they were neither moved at the key nor served out of the "
                "cache" % (LAZY, stale))
        if MEGA:
            short = sorted(need - got - served)
            if short:
                fails.append(
                    "part %d's LBAs %r neither moved at the key (Bochs's FDC "
                    "log: %r) nor came out of the read-ahead"
                    % (LAZY, short, sorted(got)))
        fetch_secs += len(served)
        ps = part_seg(m, seg, LAZY)
        lz = chr(byte(m, seg, "ms_lazy"))
        t = title_of(m, S, seg, wrec)
        say("after the key: op_seg(%d) = %04X, %d sectors, ms_lazy %r, %r"
            % (LAZY, ps, fetch_secs, lz, t))
        if not ps:
            fails.append(
                "part %d is still absent after the key. op_fetch claims and "
                "reads it, and a toast has said why if it refused - check the "
                "screen. On this machine the part is %d KB and the heap has "
                "hundreds" % (LAZY, (row["len"] + 1023) // 1024))
        elif fetch_secs < lazy_secs:
            fails.append(
                "the fetch moved %d sectors and part %d is %d. THE READ HAS "
                "TO BE HERE: the launch did not do it (assertions 1 and 2) "
                "and the part's bytes are on the disk, so a fetch that moves "
                "fewer sectors than the part occupies has not read it - and "
                "MSEG's checks below would then be reading whatever the claim "
                "came with" % (fetch_secs, LAZY, lazy_secs))
        if lz != "F":
            fails.append("ms_lazy says %r and should say 'F' - MSEG did not "
                         "take the fetch path" % lz)
        if t != want:
            fails.append(
                "MSEG says %r after the fetch and should say %r. Part 6's "
                "check is now the same three the other modules get - the "
                "signature MSEG reads for itself, a far call to <part>:0 "
                "answering a value only that module computes, and a ROTATING "
                "sum over its data area against the figure the assembler "
                "computed - so a claim that was never filled, a wrong base "
                "and a transposed read are three different failures and this "
                "is one of them - the parts that did not pass are %r"
                % (t, want, bad()))

        # --- 4. ...and the second key gives it back ------------------------
        os88marty.key_taken(m, "KeyL")
        os88marty.ui_done(m)
        ps = part_seg(m, seg, LAZY)
        lz = chr(byte(m, seg, "ms_lazy"))
        after = claims(m, S)
        say("after the drop: op_seg(%d) = %04X, ms_lazy %r, claims %d -> %d"
            % (LAZY, ps, lz, len(before), len(after)))
        if ps:
            fails.append("part %d still answers %04X after op_drop, so op_seg "
                         "would hand out a segment that is back in the heap"
                         % (LAZY, ps))
        if lz != "D":
            fails.append("ms_lazy says %r and should say 'D'" % lz)
        if after != before:
            fails.append(
                "the claim table is not what it was before the fetch:\\n"
                "    before %r\\n    after  %r\\n"
                "op_drop frees the one claim op_fetch made, and THAT is what "
                "makes lazy a saving rather than a postponement - a part that "
                "is only ever fetched still spends its KB and one of the "
                "eight claim slots (MEM_OWNER_MAX, SPEC.md 50) for the life "
                "of the instance" % (before, after))

        # --- 5. and again ---------------------------------------------------
        os88marty.key_taken(m, "KeyL")
        os88marty.ui_done(m)
        ps = part_seg(m, seg, LAZY)
        t = title_of(m, S, seg, wrec)
        say("and again: op_seg(%d) = %04X, %r" % (LAZY, ps, t))
        if not ps or t != want:
            fails.append(
                "the second fetch did not work (segment %04X, verdict %r). "
                "op_fetch reads the row's own off/len every time and op_drop "
                "clears only the word it banked the segment in, so fetching "
                "twice is the same call twice - this failing means op_drop "
                "left the ROW, and not just the heap, in a state a fetch "
                "cannot start from" % (ps, t))


MEGA = bool(os.environ.get("OS88_MEGA"))     # kern_mega under Bochs


def main():
    if not MEGA:                # (kern_mega counts through Bochs's FDC log)
        build_counted()
    run()
    if fails:
        print("\nmseglazy: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("\nmseglazy: the lazy part was outside the carve, arrived on a key "
          "and went back on the next one - PASS on %s" % MACHINE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
