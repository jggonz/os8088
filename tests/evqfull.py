#!/usr/bin/env python3
"""A full event ring loses the OLDEST input, and never a WAKE (SPEC.md 10.1).

    make && python3 tests/evqfull.py [--machine os8088_5150_cga_gla]

The starvation this policy exists for takes seconds of guest time to set up
and is a measurement, not a gate (docs/FIELD-NOTES.md 27). The POLICY is a
property of nine instructions and can be asked directly: boot a desktop, park
the CPU on a stub that calls `evq_push` twenty times with a counter in EV_A -
the mouse ISR cannot interfere, because `park` leaves IF=0 - and read the ring
back.

Two questions, and they are the two halves of 10.1:

  1. twenty input records into a sixteen-record ring leaves the LAST sixteen,
     oldest first. Before 10.1 it left the FIRST sixteen, which is the lockout.
  2. a WAKE at the head is a promise `wm_wake` has already recorded in
     `wm_wkq` and only a dispatch clears, so it must survive - the push falls
     back to refusing the newest instead.

**ON THE GLaBIOS TWIN**, `os8088_5150_herc_gla` - the same machine with
`rom_set = "glabios_pc"`. `os8088_5150_herc` wants `ibm5150_82_v4`, the
reader's own dump of the 27 OCT 82 BIOS, which this repo cannot ship, so this
row died in `os88marty.launch` on any clean tree. Nothing here is a timing
question at all - it parks the CPU on a stub with IF=0 and reads a ring back -
and it PASSES IDENTICALLY on both, checked with the IBM ROM dropped in beside
GLaBIOS. tools/martypc/configs/os8088_machines.toml says what the twin's
caveat does and does not cover.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL_SEG = 0x0060
EVQ_CAP = 16
EVQ_RECSIZE = 8
EVT_MDOWN = 1
EVT_WAKE = 4


def stub(sym, at, rec, n):
    """mov si,rec / mov cx,n / .l: call evq_push / inc [si+EV_A] / loop / hlt"""
    body = bytearray()
    body += bytes([0xBE]) + rec.to_bytes(2, "little")        # mov si, rec
    body += bytes([0xB9]) + n.to_bytes(2, "little")          # mov cx, n
    loop = at + len(body)
    rel = (sym["evq_push"] - (loop + 3)) & 0xFFFF
    body += bytes([0xE8]) + rel.to_bytes(2, "little")        # call evq_push
    body += bytes([0xFF, 0x44, 0x02])                        # inc word [si+2]
    body += bytes([0xE2, (loop - (at + len(body) + 2)) & 0xFF])   # loop .l
    body += bytes([0xEB, 0xFE])                              # jmp $
    end = at + len(body)
    return bytes(body), end


def run_pushes(m, sym, at, rec, evtype, first_a, n):
    """Park on the stub and let it push n records of `evtype`, EV_A = first_a.."""
    blob, done = stub(sym, at, rec, n)
    m.write((KERNEL_SEG << 4) + at, blob)
    m.write((KERNEL_SEG << 4) + rec,
            evtype.to_bytes(2, "little") + first_a.to_bytes(2, "little")
            + b"\x00\x00\x00\x00")
    r = m.regs()
    m.cmd(cmd="park", cs=KERNEL_SEG, ip=at)                  # ...and IF := 0
    for reg, val in (("ds", KERNEL_SEG), ("es", KERNEL_SEG),
                     ("ss", r["ss"]), ("sp", r["sp"])):
        m.setreg(reg, val)
    m.bp_exec((KERNEL_SEG << 4) + done - 2)
    m.run()
    if m.wait_stop(120) is None:
        raise SystemExit("evqfull: the push stub never finished")
    m.breakpoints([])


def ring(m, sym):
    """The queue's records, oldest first: [(type, a), ...]."""
    def w(name):
        return int.from_bytes(m.read(os88sym.linear(name), 2), "little")
    head, n = w("evq_head"), w("evq_count")
    buf = m.read(os88sym.linear("evq_buf"), EVQ_CAP * EVQ_RECSIZE)
    out = []
    for i in range(n):
        o = (head + i * EVQ_RECSIZE) & (EVQ_CAP * EVQ_RECSIZE - 1)
        out.append((int.from_bytes(buf[o:o + 2], "little"),
                    int.from_bytes(buf[o + 2:o + 4], "little")))
    return out


def reset(m, sym, at):
    rel = (sym["evq_init"] - (at + 3)) & 0xFFFF
    m.write((KERNEL_SEG << 4) + at,
            bytes([0xE8]) + rel.to_bytes(2, "little") + bytes([0xEB, 0xFE]))
    r = m.regs()
    m.cmd(cmd="park", cs=KERNEL_SEG, ip=at)
    for reg, val in (("ds", KERNEL_SEG), ("ss", r["ss"]), ("sp", r["sp"])):
        m.setreg(reg, val)
    m.bp_exec((KERNEL_SEG << 4) + at + 3)
    m.run()
    m.wait_stop(120)
    m.breakpoints([])


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    sym = os88sym.syms()
    at = sym["snd_xlat"]                # 256 idle .bss bytes inside KERNEL_SEG,
                                        # rebuilt per clip and so idle on a
                                        # desktop nothing is playing on. NOT
                                        # gfx_pairtab0 any more: that pair went
                                        # to .lowbss (SPEC.md 5.4.1.1), and an
                                        # offset in LOW_SEG poked at KERNEL_SEG
                                        # lands in the middle of the kernel
    rec = at + 64
    bad = 0

    with os88marty.launch(a.image, machine=a.machine) as m:
        os88marty.settle(m)
        m.pause()

        # --- 1. twenty input records, sixteen slots -------------------------
        reset(m, sym, at)
        run_pushes(m, sym, at, rec, EVT_MDOWN, 0, 20)
        got = ring(m, sym)
        want = [(EVT_MDOWN, i) for i in range(20 - EVQ_CAP, 20)]
        print("  20 x MDOWN into a %d-record ring -> %d records, EV_A %r"
              % (EVQ_CAP, len(got), [g[1] for g in got]))
        if got != want:
            print("  FAIL: SPEC.md 10.1 wants the LAST %d, EV_A %r"
                  % (EVQ_CAP, [w[1] for w in want]))
            bad += 1
        else:
            print("  ok: the newest %d survived - the oldest were discarded"
                  % EVQ_CAP)

        # --- 2. ...unless the oldest is a WAKE ------------------------------
        reset(m, sym, at)
        run_pushes(m, sym, at, rec, EVT_WAKE, 0x1234, 1)
        run_pushes(m, sym, at, rec, EVT_MDOWN, 0, 20)
        got = ring(m, sym)
        want = ([(EVT_WAKE, 0x1234)]
                + [(EVT_MDOWN, i) for i in range(EVQ_CAP - 1)])
        print("  WAKE + 20 x MDOWN -> %d records, head %r, EV_A %r"
              % (len(got), got[0] if got else None, [g[1] for g in got[1:]]))
        if got != want:
            print("  FAIL: SPEC.md 10.1 keeps a WAKE at the head and refuses"
                  " the newest instead - wanted %r" % (want,))
            bad += 1
        else:
            print("  ok: the wake survived and the newest presses were refused")

    print()
    print("evqfull: %s" % ("FAIL (%d)" % bad if bad else "PASS"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
