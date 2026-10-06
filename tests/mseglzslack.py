#!/usr/bin/env python3
"""A fetched COMPRESSED lazy part keeps its own length and nothing of the
volume's (SPEC.md 88.10.4.1, 20.12.7.4).

    make mseglz && python3 tests/mseglzslack.py [machine] [system-image]

`op_fetch` claims `op_lazykb`'s figure for an `OP_COMP | OP_LAZY` row - R plus
the read of the packed part rounded to whole clusters - because a read may
only BEGIN on a cluster boundary and the stream has to sit above its output
while it expands. Once it has expanded the claim is shrunk in place to what
the session keeps. A shrink only ever takes the TAIL, so what it CAN keep
depends on where the part was put: expanded past the cluster's head slack,
which is where the read had left it, the slack stayed in front of the part
for the life of the instance - up to 31.5KB on a FAT16 hard disk with 32KB
clusters. It is expanded onto the claim's base now, and this row measures it.

THE FIXTURE is MSEG built `-DMSEG_LZC` - OP_COMP on part 6, its lazy module,
and on nothing else - on a 1.44MB disk built `--fatcap 1`, whose clusters are
8KB (sixteen sectors). Part 6 begins six sectors into a cluster, so its read
starts 3KB below it. On a stock floppy the slack is at most 512 bytes and the
two layouts can round to the same KB; here they cannot.

FOUR ASSERTIONS:

  1. THE FIXTURE IS WHAT IT SAYS: the row is OP_COMP | OP_LAZY, the volume's
     clusters are bigger than a sector, and the part's head slack is NOT zero.
     Without this the row below could pass on a layout that never exercised
     the slack at all, which is mseg360's lesson (tests/multiseg.py);

  2. THE KEY FETCHES IT AND IT IS RIGHT: op_seg answers a segment and MSEG's
     own verdict - a signature, a far call answering a value only that module
     computes, and a rotating sum over its data area - says `MSEG 7/7 OK`. An
     output one pointer off would fail all three;

  3. THE SEGMENT IS A CLAIM'S OWN BASE AND THE CLAIM IS THE PART: some live
     claim begins exactly at op_seg's answer, and it is `len` rounded up to a
     KB, in paragraphs. Expanded past the slack, as this first shipped, no
     claim begins there (the part is slack/16 paragraphs into one) and the
     claim that holds it is `slack + len` rounded to a KB - 6KB against 3KB on
     this fixture;

  4. A SECOND KEY GIVES IT BACK: the claim table is BYTE-FOR-BYTE what it was
     before the fetch. op_drop frees a compressed row's banked word as it
     stands and still takes the slack off a plain one - and OSAPI_MEM_FREE
     matches a claim's base exactly and op_drop never reads its CF, so a
     wrong free here is a SILENT leak that only this comparison sees.

VERIFIED TO FAIL against the shrink-past-the-slack op_fetch it replaces:
assertion 3 reports no claim at the part's segment, and the claim holding it
at 384 paragraphs (6KB) where the part needs 192 (3KB).
"""
import os
import struct
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
sys.path.insert(0, "tests/multiseg")
import os88build                                             # noqa: E402
import os88marty                                             # noqa: E402
import os88mouse                                             # noqa: E402
import os88parts                                             # noqa: E402
import os88sym                                               # noqa: E402
import os88geom                                              # noqa: E402
import dispcp                                                # noqa: E402
import msegsym                                               # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/mseglz.img"
O88 = "build/mseglzd/MSEG.O88"
LAZY = 6
MEM_MAX, MC_SIZE = os88geom.MEM_MAX, os88geom.MC_SIZE
MC_SEG, MC_PARA, MC_OWN = os88geom.MC_SEG, os88geom.MC_PARA, os88geom.MC_OWN
S = os88sym.linear
fails = []


def say(s):
    print("  " + s)


def sym(name):
    return msegsym.MSEGLZ.sym(name)


def claims(m):
    """Every live claim, as (segment, paragraphs, owner)."""
    blob = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        r = blob[i * MC_SIZE:(i + 1) * MC_SIZE]
        seg = struct.unpack_from("<H", r, MC_SEG)[0]
        if seg:
            out.append((seg, struct.unpack_from("<H", r, MC_PARA)[0],
                        struct.unpack_from("<H", r, MC_OWN)[0]))
    return out


def title_of(m, seg, rec):
    toff = struct.unpack_from("<H", rec, os88geom.W_TITLE)[0]
    return m.read((seg << 4) + toff, 24).split(b"\0")[0].decode(
        "ascii", "replace")


def run():
    for f in (APPS_IMG, O88, SYS_IMG):
        if not os.path.exists(os88build.at(f)):
            raise SystemExit("mseglzslack: %s is missing - `make mseglz` "
                             "(and `make` for the system disk)" % f)
    blob = open(os88build.at(O88), "rb").read()
    rows = os88parts.rows(blob[:blob[8] | (blob[9] << 8)])
    row = rows[LAZY]
    want_flags = os88parts.EQU["OP_COMP"] | os88parts.EQU["OP_LAZY"]
    img = open(os88build.at(APPS_IMG), "rb").read(512)
    clb = struct.unpack_from("<H", img, 11)[0] * img[13]
    slack = (row["off"] * 512) & (clb - 1)
    kept_kb = (row["len"] + 1023) // 1024
    say("part %d: sector %d, %d bytes unpacked, flags 0x%02X; clusters %d "
        "bytes, so the head slack is %d"
        % (LAZY, row["off"], row["len"], row["flags"], clb, slack))

    # --- 1. the fixture is what it says ----------------------------------
    if row["flags"] & want_flags != want_flags:
        raise SystemExit("mseglzslack: part %d is not OP_COMP | OP_LAZY "
                         "(flags 0x%02X) - the fixture is not built "
                         "-DMSEG_LZC" % (LAZY, row["flags"]))
    if clb <= 512 or slack == 0:
        raise SystemExit(
            "mseglzslack: the fixture has clusters of %d bytes and a head "
            "slack of %d - nothing below would measure the slack. Build the "
            "disk `--fatcap 1` and keep part %d off a cluster boundary"
            % (clb, slack, LAZY))

    with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MSEG.O88")
        os88marty.settle(m)

        seg = 0
        for i in dispcp.win_list(m, S):
            rec = m.read(S("wm_wins") + i * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
            sg = struct.unpack_from("<H", rec, os88geom.W_SEG)[0]
            if sg and title_of(m, sg, rec).startswith("MSEG "):
                seg, wrec = sg, rec
        if not seg:
            raise SystemExit("mseglzslack: MSEG did not launch off the 8KB-"
                             "cluster disk - nothing below can be asked")
        want = "MSEG %d/%d OK" % (len(rows), len(rows))

        def part_seg():
            return struct.unpack_from(
                "<H", m.read((seg << 4) + sym("ms_seg") + LAZY * 2, 2), 0)[0]

        t = title_of(m, seg, wrec)
        say("after the launch: op_seg(%d) = %04X, verdict %r"
            % (LAZY, part_seg(), t))
        if t != want:
            fails.append("MSEG says %r before the fetch and should say %r"
                         % (t, want))
        before = claims(m)

        # --- 2. the key fetches it, and it is right ----------------------
        m.key("KeyL")
        os88marty.settle(m)
        ps = part_seg()
        t = title_of(m, seg, wrec)
        lz = chr(m.read((seg << 4) + sym("ms_lazy"), 1)[0])
        say("after the key: op_seg(%d) = %04X, ms_lazy %r, %r"
            % (LAZY, ps, lz, t))
        if not ps or lz != "F":
            fails.append(
                "part %d was not fetched (segment %04X, ms_lazy %r) - a toast "
                "has said why; check the screen" % (LAZY, ps, lz))
        elif t != want:
            fails.append(
                "MSEG says %r after the fetch and should say %r: the part's "
                "bytes are not at the segment op_seg answers. op_fetch "
                "expands a compressed lazy row onto its claim's BASE and "
                "banks that (SPEC.md 20.12.7.4)" % (t, want))

        # --- 3. the segment is a claim's base, and the claim is the part --
        after = claims(m)
        new = [c for c in after if c not in before]
        say("claims the fetch added: %s"
            % ", ".join("%04X+%d para" % (c[0], c[1]) for c in new))
        at = [c for c in after if c[0] == ps]
        if ps and not at:
            holder = [c for c in after if c[0] <= ps < c[0] + c[1]]
            fails.append(
                "no claim BEGINS at the part's segment %04X - it is inside "
                "%s, %d paragraphs in. That is the head slack (%d bytes, %d "
                "paragraphs) still in front of the part for the session: "
                "op_fetch must expand an OP_COMP | OP_LAZY row onto the "
                "claim's base, because a shrink only takes the tail (SPEC.md "
                "88.10.4.1)"
                % (ps, ["%04X+%d" % c[:2] for c in holder],
                   ps - holder[0][0] if holder else -1, slack, slack // 16))
        elif at and at[0][1] != kept_kb * 64:
            fails.append(
                "the claim at %04X is %d paragraphs and the part is %d bytes, "
                "so the session should keep %dKB = %d paragraphs. op_fetch "
                "shrinks the claim through OSAPI_MEM_REGROW once the part "
                "has expanded (SPEC.md 88.10.4.1)"
                % (ps, at[0][1], row["len"], kept_kb, kept_kb * 64))
        if len(new) != 1:
            fails.append("the fetch added %d claims and should add ONE "
                         "(SPEC.md 20.12.4): %r" % (len(new), new))

        # --- 4. the second key gives it back, exactly ----------------------
        m.key("KeyL")
        os88marty.settle(m)
        ps2 = part_seg()
        final = claims(m)
        say("after the drop: op_seg(%d) = %04X, claims %d -> %d"
            % (LAZY, ps2, len(after), len(final)))
        if ps2:
            fails.append("part %d still answers %04X after op_drop"
                         % (LAZY, ps2))
        if final != before:
            fails.append(
                "the claim table is not what it was before the fetch:\n"
                "    before %r\n    after  %r\n"
                "op_drop frees a compressed lazy row's banked word AS IT "
                "STANDS - it is the claim's base now - and OSAPI_MEM_FREE "
                "matches a base exactly with nobody reading its CF, so a "
                "free of the wrong word is a silent leak" % (before, final))
        say("kept %dKB for a %d-byte part on %d-byte clusters (the slack in "
            "front of it would have made it %dKB)"
            % (kept_kb, row["len"], clb,
               (slack + row["len"] + 1023) // 1024))


def main():
    run()
    if fails:
        print("\nmseglzslack: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("\nmseglzslack: a compressed lazy part kept its own length at its "
          "claim's base, and the drop gave it all back - PASS on %s" % MACHINE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
