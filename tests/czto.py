#!/usr/bin/env python3
"""File > Uncompress To... - the join ASKS for the next disk (SPEC.md 22.23.6).

The set is cut on the host by tools/os88cz.py and its parts are spread over
three 1.44MB floppies the harness swaps in drive B: while the machine runs
(`Marty.mount`, the debug server's runtime mount). The result is saved to
A:, the system disk, which is where the Save box opens - so the parts and
the result are on two drives, which is the case the verb exists for. Every
assertion is guest state (the prompt's mode, the claim's saved state) or the
bytes on the floppy afterwards:

  quick   QK.001 and QK.002 both in B:/ALL: joined to A: with no prompt at
          all - the two-volume hop on its own.
  esc     ESC.001 in B:'s root and no ESC.002 anywhere: the prompt comes up
          asking for part 2, Esc stops it, and nothing is left on A: - no
          result, no CMPRESS~.TMP - and the claim word is empty.
  swap    SET.001 in B:/PARTS, SET.002 on a second disk, SET.003 on a third.
          The prompt asks for part 2; Enter with the SAME disk still in says
          `Missing SET.002` and keeps asking; the second disk goes in (and the
          motor is let stop, which is how the machine learns of a swap -
          SPEC.md 18.9.1), Enter joins part 2 from its ROOT and asks for part
          3; the third disk, Enter, `Uncompressed`, and A:'s copy is the
          original byte for byte.

  ioerr   BAD.001 on the third disk, BAD.002 on the second, which is the
          marginal one: after the prompt and the swap its reads fail, and
          the join must say `Disk error`, delete what it wrote on A: and give
          the machine back.
  alive   ...and the disk STAYS bad, so the Disk window's re-read after the
          verdict fails too, as it did on the 5150 for 30 seconds: with the
          mouse nudged at every failed attempt, the pointer must be UP
          (cur_level 0) and following it (SPEC.md 7.5.3.2). It was down and
          still, which is what made the retries look like a hang.

  hopfail (after ioerr, because it leaves a CMPRESS~.TMP) HOP.001 and HOP.002 on
          the second disk; B:'s reads fail part-way
          (a CRC error, AH = 10h, injected just after the kernel's own
          `int 13h`, so its retries and everything above them are real), and
          the error path's hop back to A: - to delete the half-written result
          - is pointed at a drive that is not there. That hop's failure
          jumped back into the error path, which hopped again, FOR EVER
          (SPEC.md 22.23.6.1): the join must say `Disk error` after one.
  arm     no job at all: a one-cluster folder on the second disk opened
          with the motor running, so its read is one sector and does not
          reach FPG_WARM, and every read of B: fails. From the SECOND failed
          attempt on, the chrome and the busy clock must be up (SPEC.md
          12.8.3.2): a retry moves no sectors, and nothing was drawn for
          however long the retries took.

Then A: is handed to os88disk's fsck.

`--break` is the negative control (docs/WRITING-TESTS.md 1): the second disk
carries a SET.002 from ANOTHER set, so the machine must refuse it and the row
must go red.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88build                                       # noqa: E402
import os88cz                                          # noqa: E402
import os88flush                                       # noqa: E402
import os88geom as geom                                # noqa: E402
import os88marty                                       # noqa: E402
import os88sym                                         # noqa: E402
import os88ui                                          # noqa: E402

MACHINE = "os8088_xt_vga_144"
FS_EDIT = os88sym.equates()["FS_EDIT"]
CL_STEP = 2
CLS_JOIN = 0x80
HDR_K = 16 + 8                  # CMZ_JST + cmz_jk's place in cmz_jst


def say(*a):
    print(*a, flush=True)


def half_text(n, seed):
    sys.path.insert(0, os.path.join(HERE, "unit"))
    import t_lzfmt
    return t_lzfmt.half_text(n, seed)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--break", dest="brk", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(os88build.at("build/os8088.img")):
        sys.exit("czto: build/os8088.img is missing - run `make` first")

    qk = half_text(60000, 5)
    qkp = os88cz.split(qk, "QK.DAT", 40000, os88cz.M_STORE, jobs=1)
    esc = half_text(60000, 7)
    escp = os88cz.split(esc, "ESC.DAT", 40000, os88cz.M_STORE, jobs=1)
    big = half_text(150000, 9)
    setp = os88cz.split(big, "SET.DAT", 70000, os88cz.M_STORE, jobs=1)
    other = os88cz.split(half_text(150000, 11), "SET.DAT", 70000,
                         os88cz.M_STORE, jobs=1)   # STORED: the part
                                                   # counts follow the sizes
    if len(qkp) != 2 or len(escp) < 2 or len(setp) != 3:
        sys.exit("czto: the fixtures are %d/%d/%d parts, wanted 2/2+/3"
                 % (len(qkp), len(escp), len(setp)))

    bad = half_text(60000, 13)
    badp = os88cz.split(bad, "BAD.DAT", 40000, os88cz.M_STORE, jobs=1)
    hopp = os88cz.split(half_text(60000, 15), "HOP.DAT", 40000,
                        os88cz.M_STORE, jobs=1)
    work = os.path.abspath(os.path.join(os88build.at("build"),
                                        "czto-%d" % os.getpid()))
    shutil.rmtree(work, ignore_errors=True)
    for d in ("x", "x/ALL", "x/PARTS", "y", "y/SUB", "z"):
        os.makedirs(os.path.join(work, d))

    def put(d, name, data):
        p = os.path.join(work, d, name)
        open(p, "wb").write(data)
        return p

    def disk(name, *files):
        img = os.path.join(work, name)
        r = subprocess.run([sys.executable,
                            os.path.join(HERE, "..", "tools", "os88disk.py"),
                            "-o", img, "--size", "1440"] + list(files),
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("czto: os88disk: " + r.stderr)
        return img

    dx = disk("x.img",
              "ALL:" + put("x/ALL", "QK.001", qkp[0]),
              "ALL:" + put("x/ALL", "QK.002", qkp[1]),
              put("x", "ESC.001", escp[0]),
              "PARTS:" + put("x/PARTS", "SET.001", setp[0]))
    dy = disk("y.img", put("y", "SET.002", (other if a.brk else setp)[1]),
              put("y", "BAD.002", badp[1]),
              put("y", "HOP.001", hopp[0]), put("y", "HOP.002", hopp[1]),
              "SUB:" + put("y/SUB", "NOTE.TXT", b"one sector\r\n"))
    dz = disk("z.img", put("z", "SET.003", setp[2]),
              put("z", "BAD.001", badp[0]))
    fails = []

    def leg(tag, ok, msg):
        say("  %-6s %s  %s" % (tag, "ok " if ok else "BAD", msg))
        if not ok:
            fails.append("%s: %s" % (tag, msg))

    with os88ui.boot(os88build.at("build/os8088.img"), apps=dx,
                     machine=MACHINE, verbose=False) as ui:
        m = ui.m
        S = ui._S
        fl = os88flush.Flush(marty=m)

        def w16(sym):
            return int.from_bytes(m.read(S(sym), 2), "little")

        def mode():
            vp = w16("fm_vp")
            if not vp:
                return 0
            return m.read((geom.KERNEL_SEG << 4) + vp + FS_EDIT, 1)[0]

        def claim():
            seg = w16("clo_seg")
            if not seg:
                return None
            h = m.read(seg << 4, 32)
            return h[CL_STEP], int.from_bytes(h[HDR_K:HDR_K + 2], "little")

        def select(name):
            win = ui.raise_window(ui.disk_window())
            idx, _ = ui.entry(name, win)
            row = ui.scroll_to(idx, win=win)
            x, y = ui.row_xy(win, row)
            ui.mo.click(x, y)
            ui.settle()

        def unto(name):
            """select `name`, File > Uncompress To..., take the Save box's
            default (A:, the name the set rejoins as) with Enter"""
            select(name)
            m.write(S("toast_buf"), b"\0")
            ui.menu_pick("File", "Uncompress To...")
            os88marty.until(m, lambda mm: w16("fdlg_win") != 0,
                            "the Save box to open", poll=0.1, guest=30.0)
            ui.settle()
            m.key("Enter")

        last_why = [None]

        def outcome(limit=120.0, was=None):
            """wait for the join to END (a toast, no claim) or to ASK (mode 7
            with the join's claim); answer ('asked', k) or ('said', text).

            `was` is the part a standing prompt already asked for: after a
            key, that prompt is still up until the key is handled, so it only
            counts as an answer once the part changes or a toast is said."""
            box = {}

            def got(mm):
                # ONLY A STILL READING COUNTS. The join says its toast and
                # THEN moves its claim on - "Missing SET.003" with the claim
                # still at part 2, "Uncompressed" with the claim not yet
                # freed - so one poll can land between the two and read a
                # prompt that is being left. A paused prompt and a finished
                # join both hold still; that moment does not
                now = (claim(), mode(), ui.toast())
                if box.get("last") != now:
                    box["last"] = now
                    return False
                c = claim()
                if c and c[0] == CLS_JOIN and mode() == 7:
                    if not 1 <= c[1] <= 999:
                        box.setdefault("odd", []).append(
                            (hex(w16("clo_seg")), c, w16("fm_vp"),
                             int(m.status()["cycles"])))
                        return False
                    t, on = ui.toast()
                    if was is None or c[1] != was or (on and t):
                        box["r"] = ("asked", c[1])
                        box["why"] = (t, on, c, mode(),
                                      int(m.status()["cycles"]))
                        return True
                    return False
                t, on = ui.toast()
                if on and t and not c and w16("fdlg_win") == 0:
                    box["r"] = ("said", t)
                    return True
                return False
            os88marty.until(m, got, "the join to finish or to ask",
                            poll=0.2, guest=limit)
            if box.get("odd"):
                say("   (transient: %d reads of the prompt with no part in "
                    "its header yet, first %r)" % (len(box["odd"]),
                                                   box["odd"][0]))
            last_why[0] = box.get("why")
            if os.environ.get("CZTO_WHY"):
                say("   (outcome %r because %r)" % (box["r"], box.get("why")))
            ui.settle()
            return box["r"]

        def key(k):
            m.write(S("toast_buf"), b"\0")
            m.key(k)

        def motor_off():
            """a swap is noticed once the motor has stopped (SPEC.md 18.9.1)"""
            os88marty.until(m, lambda mm: m.read(0x43F, 1)[0] & 0x0F == 0,
                            "the floppy motors to stop", poll=0.2, guest=10.0)

        def a_files():
            return {e.name.upper(): e for e in fl.volume(0).walk()}

        ui.open_drive("B")
        # --- quick: both parts in one folder, the result on A: ------------
        ui.open("ALL")
        unto("QK.001")
        r = outcome()
        af = a_files()
        got = fl.volume(0).read(af["QK.DAT"].path) if "QK.DAT" in af else None
        leg("quick", r == ("said", "Uncompressed") and got == qk
            and "CMPRESS~.TMP" not in af,
            "%r, A:%s" % (r, af["QK.DAT"].path if "QK.DAT" in af
                          else " has no QK.DAT"))
        ui.open("..")

        # --- esc: part 2 is nowhere, and Esc gives up cleanly --------------
        unto("ESC.001")
        r = outcome()
        leg("esc-ask", r == ("asked", 2), "%r (wanted a prompt for part 2)"
            % (r,))
        key("Escape")
        os88marty.until(m, lambda mm: claim() is None and mode() == 0,
                        "Esc to end the join", poll=0.2, guest=30.0)
        ui.settle()
        af = a_files()
        leg("esc", "ESC.DAT" not in af and "CMPRESS~.TMP" not in af,
            "A: holds %s" % sorted(n for n in af if n.startswith(("ESC",
                                                                  "CMP"))))

        # --- swap: three disks -----------------------------------------------
        ui.open("PARTS")
        unto("SET.001")
        r = outcome()
        leg("ask2", r == ("asked", 2), repr(r))
        key("Enter")                    # the same disk is still in
        r = outcome(was=2)
        t, _ = ui.toast()
        leg("again", r == ("asked", 2) and t == "Missing SET.002",
            "%r, toast %r" % (r, t))
        motor_off()
        m.mount(1, dy)
        key("Enter")
        r = outcome(was=2)
        leg("ask3", r == ("asked", 3), "%r%s" % (r, "" if r == ("asked", 3)
                                               else " - because %r"
                                               % (last_why[0],)))
        # ...and asking for part 3 is NEWS, not a miss: Enter said part 2
        # was in, and it was. [cmz_jretry] used to be a flag that only the
        # prompt cleared, so this said 'Missing SET.003' for a part nobody
        # had been asked for yet (size pass 8's follow-up)
        t, _ = ui.toast()
        leg("ask3quiet", not t.startswith("Missing"),
            "toast %r (wanted none: part 3 has not been asked for)" % (t,))
        motor_off()
        m.mount(1, dz)
        key("Enter")
        r = outcome(was=3)
        swap_seen = (r, ui.toast(), claim(), mode())
        af = a_files()
        got = (fl.volume(0).read(af["SET.DAT"].path) if "SET.DAT" in af
               else None)
        leg("swap", r == ("said", "Uncompressed") and got == big
            and "CMPRESS~.TMP" not in af and mode() == 0,
            "%r, SET.DAT %s%s" % (r, "identical" if got == big else
                                  "MISSING" if got is None else "WRONG",
                                  "" if r[0] == "said" else
                                  " - saw %r" % (swap_seen,)))

        # --- a marginal floppy: B:'s reads fail, from the kernel's int 13h --
        at = S("dsk_xfer.attempt")
        i13 = at + m.read(at, 200).index(b"\xCD\x13") + 2   # after the int
        gto = S("fcpf_fcp_goto")

        seenhops = []

        after = []

        def marginal(skip, failhop, written=False, stays=False):
            """run with every read of B: after the first `skip` failing (CRC,
            AH = 10h) for as long as the join holds its claim - and, with
            `failhop`, every hop back to A: after the first failure pointed
            at a drive that is not there. With `written`, nothing fails
            until the join has hopped to A: twice - the name check, then a
            write - so there is a result to delete. (toast, failed reads,
            failed hops). With `stays`, the disk STAYS bad after the join
            has said its verdict - the field's case, where the Disk window
            re-reads it before it repaints - and every failure then nudges
            the mouse and records (cur_level, drawn x) in `after`,
            or ('LOOPING', ...) once the hop has failed 50 times"""
            m.bp_exec(i13, gto)
            good = hits = hops = 0
            del seenhops[:]
            del after[:]
            try:
                for _ in range(20000):
                    if m.wait_stop(limit=0.3) != "breakpoint":
                        if claim() is None and mode() == 0:
                            t, on = ui.toast()
                            return (t if on else None), hits, hops
                        continue
                    rg = m.regs()
                    here = (rg["cs"] << 4) + rg["ip"]
                    if here == gto:
                        seenhops.append(rg["ax"] & 0xFF)
                        if failhop and hits and rg["ax"] & 0xFF == 0:
                            m.setreg("ax", (rg["ax"] & 0xFF00) | 25)
                            hops += 1
                            if hops >= 50:
                                return "LOOPING", hits, hops
                    elif (m.read(S("dsk_op"), 1)[0] == 0x02
                          and m.read(S("dsk_unit"), 1)[0] == 0x01
                          and (stays or w16("clo_seg"))):
                        good += 1
                        if good > skip and (not written
                                            or seenhops.count(0) >= 2):
                            m.setreg("ax", 0x1000 | (rg["ax"] & 0xFF))
                            m.setreg("flags", rg["flags"] | 1)
                            hits += 1
                            if stays and not w16("clo_seg"):
                                after.append((
                                    (m.read(S("cur_level"), 1)[0] ^ 0x80)
                                    - 0x80, w16("cur_drawn_x")))
                                m.mouse(dx=4)
                    m.run()
                return "TIMEOUT", hits, hops
            finally:
                m.bp_exec()
                m.run()

        # ioerr: BAD.001 on the third disk, BAD.002 on the second - which is
        # the marginal one. The prompt, the swap, then its reads fail
        ui.open("..")                   # the third disk's root
        unto("BAD.001")
        r = outcome()
        leg("bad-ask", r == ("asked", 2), repr(r))
        motor_off()
        m.mount(1, dy)
        key("Enter")
        r = marginal(2, False, stays=True)
        af = a_files()
        leg("ioerr", r[0] == "Disk error" and mode() == 0
            and claim() is None and "BAD.DAT" not in af
            and "CMPRESS~.TMP" not in af,
            "%r after %d failed reads of B:; A: holds %s" % (
                r[0], r[1], sorted(n for n in af
                                   if n.startswith(("BAD", "CMP")))))
        # ...and the pointer is ALIVE while the window re-reads the bad disk
        # after the verdict (SPEC.md 7.5.3.2): it was frozen there, which is
        # what made 30 seconds of retries look like a hang
        leg("alive", len(after) >= 2 and all(lv == 0 for lv, _ in after)
            and after[-1][1] != after[0][1],
            "after the verdict: %d failed reads, pointer (level, x) %r"
            % (len(after), after[:6]))
        ui.menu_pick("Nav", "Refresh")  # the disk reads again: list it
        ui.settle()

        # hopfail: HOP.001 and HOP.002 both on the second disk, still in B:.
        # B: fails part-way, and the error path's own hop back to A: - to
        # delete the half-written result - fails too. That hop's failure
        # jumped back into the error path, which hopped again, for ever
        # (SPEC.md 22.23.6.1); now the result's CMPRESS~.TMP is left on A:
        unto("HOP.001")
        r = marginal(0, True, written=True)
        af = a_files()
        leg("hopfail", r[0] == "Disk error" and r[2] == 1 and mode() == 0
            and claim() is None and "HOP.DAT" not in af,
            "%r after %d failed reads and %d failed hop(s); A: holds %s"
            % (r[0], r[1], r[2], sorted(n for n in af
                                        if n.startswith(("HOP", "CMP")))))

        # arm: NO job and no chrome - a one-cluster folder opened with the
        # motor running, so the read is one sector and FPG_WARM is not met.
        # Every read of B: fails. The chrome and the clock must be up from the
        # SECOND failed attempt: a retry moves no sectors, and before SPEC.md
        # 12.8.3.2 nothing was drawn for however long the retries took
        idx, _ = ui.entry("SUB", ui.disk_window())
        ui.scroll_to(idx, win=ui.disk_window())
        m.read(S("fpg_on"), 1)
        seen = []
        m.bp_exec(i13)
        # the motor RUNNING, as the BIOS keeps it: B:'s bit in 0040:003F and a
        # full countdown in 0040:0040 - or 12.8.3.1 counts the read as warm
        # (a stopped motor) and arms the chrome before it, and this leg would
        # be testing that instead
        m.write(0x43F, bytes([m.read(0x43F, 1)[0] | 0x02]))
        m.write(0x440, b"\xFF")
        ui.mo.dblclick(*ui.row_xy(ui.disk_window(), idx - ui.scroll()))
        for _ in range(60):
            if m.wait_stop(limit=2.0) != "breakpoint":
                break
            rg = m.regs()
            if (m.read(S("dsk_op"), 1)[0] == 0x02
                    and m.read(S("dsk_unit"), 1)[0] == 0x01):
                seen.append((m.read(S("fpg_on"), 1)[0],
                             m.read(S("dsk_run"), 1)[0]))
                m.setreg("ax", 0x1000 | (rg["ax"] & 0xFF))
                m.setreg("flags", rg["flags"] | 1)
            m.run()
        m.bp_exec()
        m.run()
        leg("arm", len(seen) >= 2 and seen[0][0] == 0
            and all(f for f, _ in seen[1:]),
            "(chrome up, run) at each failed attempt: %r" % seen[:6])

        chk = os.path.join(work, "a-fsck.img")
        fl.save(0, chk)
    r = subprocess.run([sys.executable,
                        os.path.join(HERE, "..", "tools", "os88disk.py"),
                        "--verify", chk], capture_output=True, text=True)
    leg("fsck", r.returncode == 0,
        ((r.stdout + r.stderr).strip().splitlines() or [""])[-1])
    shutil.rmtree(work, ignore_errors=True)
    for f in fails:
        say("  FAIL: " + f)
    say("czto: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
