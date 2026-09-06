#!/usr/bin/env python3
"""A LAZY part is not read at load, and it can be given back (SPEC.md 20.12.4).

    make mseg && python3 tests/mseglazy.py [machine] [system-image]

Goal 3 of the whole design is "load only some minimal amount, so a package
does not read for ages only to find out it is going to be refused"
(docs/O88-MULTISEG-PLAN.md 0). Waves 2 and 3 answered the second half of that
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
     kernel) must be at least the part's own six, and afterwards op_seg
     answers a segment, MSEG's three independent module checks pass on it, and
     ms_lazy says 'F';

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
import os88marty
import os88mouse
import os88parts
import os88sym
import dispcp
import msegsym

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KNOBS = ["DISKCNT=1"]
DEFINES = ["DISK_COUNTERS"]
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/mseg.img"
O88 = "build/mseg.o88"
LAZY = 6                            # a SEMANTIC index; everything else about
                                    # the row is read out of the package
MEM_MAX, MC_SIZE, MC_SEG, MC_OWN = 32, 10, 0, 4
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
    return out


def word(m, seg, name):
    return struct.unpack_from(
        "<H", m.read((seg << 4) + msegsym.sym(name), 2), 0)[0]


def byte(m, seg, name):
    return m.read((seg << 4) + msegsym.sym(name), 1)[0]


def part_seg(m, seg, i):
    """op_seg's answer for part i, out of MSEG's own banked copy."""
    return struct.unpack_from(
        "<H", m.read((seg << 4) + msegsym.sym("ms_seg") + i * 2, 2), 0)[0]


def title_of(m, S, seg, rec):
    import os88geom
    toff = struct.unpack_from("<H", rec, os88geom.W_TITLE)[0]
    return m.read((seg << 4) + toff, 24).split(b"\0")[0].decode(
        "ascii", "replace")


def build_counted():
    say("building the counted kernel (DISKCNT=1)...")
    r = subprocess.run(["make"] + KNOBS, cwd=ROOT, capture_output=True,
                       text=True)
    if r.returncode:
        raise SystemExit("mseglazy: `make %s` failed:\n%s"
                         % (" ".join(KNOBS), (r.stdout + r.stderr)[-1500:]))


def run():
    import os88geom
    S = lambda n: os88sym.linear(n, defines=DEFINES)     # noqa: E731
    blob = open(os.path.join(ROOT, O88), "rb").read()
    rows = os88parts.rows(blob[:blob[8] | (blob[9] << 8)])
    row = rows[LAZY]
    lazy_secs = os88parts.sectors(row)
    if not row["flags"] & os88parts.EQU["OP_LAZY"]:
        raise SystemExit("mseglazy: part %d of %s is not OP_LAZY (flags "
                         "0x%02X) - this row has nothing to measure"
                         % (LAZY, O88, row["flags"]))
    say("part %d: sector %d, %d bytes, %d sectors"
        % (LAZY, row["off"], row["len"], lazy_secs))

    with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MSEG.O88")
        os88marty.settle(m)

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
        secs = S("dsk_dbg_sec")

        def moved(before):
            return (struct.unpack_from("<H", m.read(secs, 2), 0)[0]
                    - before) & 0xFFFF

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
        say("the carve is sectors %d..%d; part %d starts at %d"
            % (first, end - 1, LAZY, row["off"]))
        if end > row["off"]:
            fails.append(
                "the carve runs to sector %d and part %d starts at %d, so the "
                "ONE READ the carve exists to be covered it. This is the "
                "structural half of assertion 1 and the half that cannot be "
                "faked: op_size must step over a lazy row, and os88pkg.py "
                "puts every lazy part after the run so that stepping over one "
                "cannot leave a hole in the middle of it (SPEC.md 20.12.4)"
                % (end - 1, LAZY, row["off"]))

        before = claims(m, S)

        # --- 3. the key, and the disk goes HERE ---------------------------
        c0 = struct.unpack_from("<H", m.read(secs, 2), 0)[0]
        m.key("KeyL")
        os88marty.settle(m)
        fetch_secs = moved(c0)
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
        m.key("KeyL")
        os88marty.settle(m)
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
        m.key("KeyL")
        os88marty.settle(m)
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


def main():
    build_counted()
    try:
        run()
    finally:
        subprocess.run(["make"], cwd=ROOT, capture_output=True, text=True)
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
