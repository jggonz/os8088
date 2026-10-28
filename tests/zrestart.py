#!/usr/bin/env python3
"""Frotz's worker RESTARTS where it parks, and its region moves (SPEC.md 66.5.9.3).

    make && make build/zrestart360.img && python3 tests/zrestart.py

Frotz hires a worker, so its region was pinned for the program's whole life
(SPEC.md 66.6.2) until it declared `zx_restart` with OS88_WORKER_RESTARTABLE.
Frotz is not park-safe, so it parks only inside OSAPI_TASK_ALIVE - the burst
loop's top and seven waits - and the restart RESUMES the wait it was in from
state in bss, rather than re-executing the opcode, which would be lossy.
[MORE] is the one wait it cannot resume, and it withdraws the declaration
while it is up. This row stops the interpreter inside EACH of those, moves the
region, and plays on.

THE LAYOUT. A region is claimed top-down and a region already at the ceiling
has nowhere to go, so a Calculator per move is opened FIRST - their regions sit
above Frotz's - and each leg closes one, leaving the hole a compaction posted
from outside ([mem_cpq] at the TRIVIAL rank, tests/pkgmove.py's door) packs
Frotz's region up into. [MORE] runs first: its hole is NOT taken (the region
is pinned while the prompt is up) and so is the next leg's.

THE LEGS, in tests/frotz/zrestart.inf's commands:

  more     [MORE] up: inst_restart is 0, the region does NOT move, and no line
           comes out twice after the key
  loop     a burst-loop park: [zx_rsm] = 0, plain re-entry
  read     "ope" typed, the region moved, "n door" + RETURN: the story parses
           "open door" - the half typed before the move is kept
  char     @read_char parked, moved, the key after it answers
  save A   parked on the prompt BEFORE RETURN; RETURN then raises ONE dialog
  save B   parked, RETURN, the dialog ANSWERED while the worker was still
           parked (the park asked for by hand), and only then the move: the
           answer is read as it stands, nothing posted twice
  restore A / B, restart A / B   the same two shapes
  verify   a breakpoint inside @verify's chunk walk, the park asked for there:
           the walk re-runs from the start and answers (the same answer as
           the no-move boot: zx_verify sums the story as it stands, so a
           story that has written its dynamic memory answers "bad" on both)

EVERY LEG, against a SECOND BOOT that plays the same keys and moves nothing:
the region's segment changed (where one is expected), the worker ran on, and
the story's dynamic memory, [zf_sp] and the window's transcript (the
scrollback, every committed line plus the live one) are byte-identical. A
leg that loses a keystroke, re-posts a request or prints a line twice cannot
pass that comparison. Two save files and no third are on the disk after.

THE FIRST RUN FOUND @save/@restore HAD NEVER WORKED (SPEC.md 66.5.9.3's last
paragraph): a save was a file of zeros. So this row is also the only gate
Frotz's saves have - the story harness cannot answer a file dialog.

VERIFIED RED (by hand, SPEC.md 66.5.9.3 has the readings): the declaration
pointed at plain zx_worker; and [MORE]'s withdrawal taken out.
"""
import argparse
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import editmove                                         # noqa: E402
import os88fixture                                      # noqa: E402
import os88flush                                        # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88sym                                          # noqa: E402
import os88ui                                           # noqa: E402

DISK = "build/zrestart360.img"
STORY = "ZRS.Z5"
MOVES = {"more": 0, "loop": 1, "read": 1, "char": 1, "save": 2,
         "restore": 2, "restart": 2, "verify": 1}
S = os88sym.linear

FROTZ_PROBES = ["zf_sseg", "zf_static", "zf_sp", "zf_stkseg", "zf_req",
                "zf_reqres", "zf_err", "zf_dead", "zi_want", "zi_linen",
                "zx_rsm", "zw_sbseg", "zw_sbhead", "zw_sbcount", "zw_sbmax",
                "zw_lbuf", "zw_lbn", "zw_wbuf", "zw_wbn", "zw_more",
                "zw_rows", "zw_uphold", "zf_ver"]
ZIW_LINE, ZIW_CHAR = 1, 2
ZREQ = {"save": 1, "restore": 2, "restart": 3}
ZXR = {"read": 1, "char": 2, "save": 3, "restore": 4, "restart": 5,
       "verify": 6}
ZW_LSTRIDE, ZW_UPMAX = 128, 12
LEGS = ("more", "loop", "read", "char", "save", "restore", "restart",
        "verify")


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


class Frotz:
    def __init__(self, ui, src=os.path.join(ROOT, "apps", "frotz")):
        self.ui, self.m = ui, ui.m
        self.P = editmove.pkg_syms(os.path.join(src, "frotz.asm"),
                                   (os.path.join(ROOT, "apps") + "/",
                                    src + "/"), FROTZ_PROBES)

    # --- where it is --------------------------------------------------------
    def win(self):
        return self.ui.window("ZRS")

    def seg(self):
        w = self.win()
        return u16(self.m.read(os88geom.winptr(self.m, w.i, S)
                               + os88geom.W_SEG, 2))

    def b(self, name):
        return self.m.read(self.seg() * 16 + self.P[name], 1)[0]

    def w(self, name):
        return u16(self.m.read(self.seg() * 16 + self.P[name], 2))

    def inst(self):
        """(instance index, task slot) of the record whose base is ours."""
        seg = self.seg()
        raw = self.m.read(S("inst_tab"), 12 * 32)
        for i in range(12):
            r = raw[i * 32:(i + 1) * 32]
            if r[2] & 0x80 and u16(r, 6) == seg:
                return i, r[3]
        raise RuntimeError("no instance record at %04x" % seg)

    def restart_word(self):
        i, _ = self.inst()
        return u16(self.m.read(S("inst_restart") + 2 * i, 2))

    def parked(self):
        _, t = self.inst()
        return self.m.read(S("sch_parked") + t, 1)[0]

    # --- what it holds ------------------------------------------------------
    def transcript(self):
        seg = self.seg()
        sb, head, cnt, mx = (self.w(n) for n in
                             ("zw_sbseg", "zw_sbhead", "zw_sbcount",
                              "zw_sbmax"))
        lines = []
        for L in range(cnt):
            slot = (head - cnt + L) % mx
            off = sb * 16 + ZW_UPMAX * ZW_LSTRIDE + slot * ZW_LSTRIDE
            n = self.m.read(off, 1)[0]
            lines.append(self.m.read(off + 2, n).decode("latin-1"))
        live = self.m.read(seg * 16 + self.P["zw_lbuf"], self.w("zw_lbn")) \
            + self.m.read(seg * 16 + self.P["zw_wbuf"], self.w("zw_wbn"))
        lines.append(live.decode("latin-1"))
        return "\n".join(lines)

    def state(self):
        st, ln = self.w("zf_sseg"), self.w("zf_static")
        sp = self.w("zf_sp")
        dyn = hashlib.md5(self.m.read(st * 16, ln)).hexdigest()[:12]
        stk = hashlib.md5(self.m.read(self.w("zf_stkseg") * 16, sp)) \
            .hexdigest()[:12]
        return {"dyn": dyn, "sp": sp, "stk": stk, "text": self.transcript()}

    # --- driving it ---------------------------------------------------------
    def front(self):
        self.ui.raise_window(self.win())

    def until(self, cond, what, limit=40, more=True):
        """Wait for `cond`, answering every [MORE] on the way (more=False to
        stop at one) - [zw_more] counts rows since the last [MORE] and NOT
        since the last input (zwin.inc), so one can come up in the middle of
        @read's own echo, and does, at the same row in both boots."""
        try:
            while True:
                os88marty.until(self.m, lambda _: cond() or
                                (more and self.more_up()), what, poll=0.1,
                                limit=limit)
                if cond():
                    break
                self.front()
                self.m.key("Space")
                os88marty.until(self.m, lambda _: not self.more_up(),
                                "[MORE] dismissed", poll=0.1, limit=limit)
        except os88marty.MartyError:
            print("  STUCK waiting for %s: want %d req %d rsm %d err %02x "
                  "dead %d restart %04x; the window ends:\n%s"
                  % (what, self.b("zi_want"), self.b("zf_req"),
                     self.b("zx_rsm"), self.b("zf_err"), self.b("zf_dead"),
                     self.restart_word(), self.transcript()[-500:]))
            print("  gfx_lock_flag %d fdlg_win %04x titles %s"
                  % (self.m.read(S("gfx_lock_flag"), 1)[0],
                     u16(self.m.read(S("fdlg_win"), 2)), self.ui.titles()))
            try:
                vw, vh, rows = self.m.vram("cga")
                os88marty.write_png(os.path.join(ROOT, "build",
                                                 "zrestart-stuck.png"),
                                    vw, vh, rows)
            except Exception as e:
                print("  (no screenshot: %s)" % e)
            raise

    def at_prompt(self):
        return (self.b("zi_want") == ZIW_LINE and self.b("zf_req") == 0
                and self.transcript().endswith(">"))

    def more_up(self):
        """[MORE] is the one wait that withdraws the declaration, so in a
        build that declares, a 0 restart word IS it."""
        return self.restart_word() == 0

    def more_count(self):
        """...and the first [MORE] of leg_more is found WITHOUT that, by
        zw_morechk's own test - [zw_more] at one short of the lower window's
        rows, which zw_morewait zeroes only after its key - because the
        restart word is what that leg ASSERTS, and a build that forgot to
        withdraw it must still be driven to a verdict. Not good enough for
        every wait: zw_notice raises the count and suppresses the prompt."""
        low = self.w("zw_rows") - self.w("zw_uphold") \
            - (1 if 0 < self.b("zf_ver") <= 3 else 0)
        return self.w("zw_more") >= max(low, 1) - 1

    def prompt(self, limit=60):
        self.until(self.at_prompt, "Frotz's prompt", limit)
        os88marty.ui_done(self.m)

    def cmd(self, text, enter=True):
        self.front()
        self.m.type_text(text)
        if enter:
            self.m.key("Enter")


def run(a, move):
    """One boot. move=False is the control: the same keys, nothing posted,
    nothing parked by hand, no Calculators. Returns {leg: state}."""
    out, fails = {}, []
    with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine,
                     quick=True) as ui:
        m = ui.m
        if move:
            # ONE PER MOVE ([MORE]'s hole is the next leg's): ten for the
            # whole row, which with the Disk window and Frotz is MAX_WIN
            moves = sum(MOVES[x] for x in a.legs)
            ncalc = max(moves, 1 if "more" in a.legs else 0)
            for k in range(ncalc):
                ui.path("B:/CALC.O88")
            print("opened %d Calculators" % ncalc)
        ui.path("B:/" + STORY)
        ui.wait_window("ZRS")           # retitled once the story is in
        z = Frotz(ui)
        z.prompt()
        print("%s: Frotz at %04x, story %04x, restart word %04x"
              % ("MOVE" if move else "CONTROL", z.seg(), z.w("zf_sseg"),
                 z.restart_word()))
        if move and z.restart_word() == 0:
            fails.append("the worker never declared a restart point")

        spare = [False]                 # [MORE]'s hole, not taken

        def hole():
            if not move:
                return
            if spare[0]:
                spare[0] = False
                return
            calcs = [w for w in ui.windows() if w.title.startswith("Calc")
                     and w.visible]
            if not calcs:
                raise RuntimeError("no Calculator left to close")
            ui.close(calcs[-1])

        def post():
            wptr = os88geom.winptr(m, z.win().i, S)
            m.write(S("mem_cpq_lvl"), bytes([0xFC]))
            m.write(S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
            os88marty.until(m, lambda _: u16(m.read(S("mem_cpq"), 2)) == 0,
                            "the posted compaction", poll=0.1, limit=30)
            os88marty.ui_done(m)
            # ...AND A PARK WE ASKED FOR, WE WITHDRAW. mem_compact clears
            # [inst_parkreq] only when IT asked (its [mem_parked]); a worker
            # already standing in a hand-made park needs no asking, so the
            # request would outlive the pass and hold the worker for ever
            m.write(S("inst_parkreq"), b"\x00")

        def move_now(leg, want=True):
            """Close a Calculator, post the compaction, check the region."""
            if not move:
                return
            before, sbefore = z.seg(), z.w("zf_sseg")
            hole()
            post()
            after = z.seg()
            ok = (after != before) == want
            print("  %-10s region %04x -> %04x %s   (story %04x -> %04x)"
                  % (leg, before, after, "OK" if ok else
                     ("DID NOT MOVE" if want else "MOVED, and must not"),
                     sbefore, z.w("zf_sseg")))
            if not ok:
                fails.append("%s: region %s" % (leg, "did not move" if want
                                                else "moved under [MORE]"))

        def handpark():
            """Ask for the park ourselves, and wait for the worker to stand in
            it - the only way to hold it still while the UI task answers."""
            if not move:
                return
            m.write(S("inst_parkreq"), b"\x01")
            z.until(lambda: z.parked() == 1, "the worker parked by hand")

        def rsm(leg, want):
            if move and z.b("zx_rsm") != want:
                fails.append("%s: [zx_rsm] = %d, want %d"
                             % (leg, z.b("zx_rsm"), want))

        def check(leg):
            out[leg] = z.state()

        def leg_more():
            """[MORE]: pinned while the prompt is up."""
            z.cmd("more")
            z.until(lambda: z.more_count() or z.b("zi_want") == ZIW_LINE,
                    "the [MORE] prompt", more=False)
            os88marty.pace(m, 0.5)      # ...and into its wait loop
            if z.restart_word() != 0:
                fails.append("[MORE] never withdrew the declaration")
            else:
                print("  [MORE] up: inst_restart = 0")
            before = z.seg()
            move_now("more", want=False)
            spare[0] = move and z.seg() == before
            z.prompt()                  # every [MORE] dismissed with a key
            if move and z.restart_word() == 0:
                fails.append("the declaration did not come back after [MORE]")
            check("more")

        def leg_loop():
            """The burst loop's own park: [zx_rsm] = 0."""
            z.cmd("loop")
            z.until(lambda: z.b("zi_want") == 0, "the loop running")
            rsm("loop", 0)
            move_now("loop")
            z.prompt()
            check("loop")

        def leg_read():
            """@read with half a line typed: the half is kept."""
            z.cmd("ope", enter=False)
            z.until(lambda: z.w("zi_linen") == 3, "'ope' in the line")
            rsm("read", ZXR["read"])
            move_now("read")
            z.cmd("n door")
            z.prompt()
            if "typed: open door\nparsed: open door" not in z.transcript():
                fails.append("read: the story did not see 'open door'")
            check("read")

        def leg_char():
            z.cmd("char")
            z.until(lambda: z.b("zi_want") == ZIW_CHAR, "@read_char waiting")
            rsm("char", ZXR["char"])
            move_now("char")
            z.front()
            m.key("KeyX")
            z.prompt()
            check("char")

        def request(op, early, answer):
            """A: moved on the prompt, before RETURN. B: RETURN pressed and
            the UI task's answer in while the worker stood parked, THEN the
            move - so the resumed wait finds the request already answered."""
            leg = "%s %s" % (op, "A" if early else "B")
            z.cmd(op)
            z.until(lambda: z.b("zf_req") == ZREQ[op], "the %s prompt" % op)
            rsm(leg, ZXR[op])
            if early:
                move_now(leg)
            else:
                handpark()
            z.front()
            m.key("Enter")
            answer()
            z.until(lambda: z.b("zf_req") == 0, "the %s answered" % op)
            if not early:
                if move and z.parked() != 1:
                    fails.append("%s: the worker ran before the move" % leg)
                move_now(leg)
            z.prompt()
            if move and u16(m.read(S("fdlg_win"), 2)) != 0:
                fails.append("%s: a second dialog came up" % leg)
            check(leg)

        def count():
            z.cmd("count")
            z.prompt()

        def leg_save():
            count()
            count()
            request("save", True, lambda: ui.chooser_save("ZRS.SAV"))  # n 2
            count()
            request("save", False, lambda: ui.chooser_save("ZRS2.SAV"))  # n 3

        def leg_restore():
            count()
            request("restore", True, lambda: ui.chooser_open("ZRS.SAV"))
            count()                                     # back at n 2: 3
            request("restore", False, lambda: ui.chooser_open("ZRS2.SAV"))
            count()                                     # back at n 3: 4

        def leg_restart():
            request("restart", True, lambda: None)
            request("restart", False, lambda: None)

        def leg_verify():
            """A breakpoint in @verify's chunk walk, the park asked for
            there: the walk re-runs from the start."""
            if move:
                hole()
                s0 = z.seg()
                m.breakpoints([{"type": "exec",
                                "addr": s0 * 16 + z.P["zx_verify.walked"]}])
            z.cmd("verify")
            if move:
                if not m.wait_stop(20):
                    fails.append("verify: the chunk walk was never reached")
                m.write(S("inst_parkreq"), b"\x01")
                m.breakpoints([])
                m.run()
                z.until(lambda: z.parked() == 1, "the worker parked in @verify")
                rsm("verify", ZXR["verify"])
                spare[0] = True             # the hole is already open
                move_now("verify")
            z.prompt()
            # AN ANSWER, and the same one as the no-move boot's (the text
            # check below). NOT "verify ok": zx_verify sums the story AS IT
            # STANDS, so a story that has written its dynamic memory - this
            # one has, every turn - answers "bad" on both boots; the real
            # Frotz sums the FILE. A standing difference, not this row's
            tail = z.transcript().split("\n")[-2:]
            if not tail[0].startswith("verify "):
                fails.append("verify: no answer (%r)" % tail)
            check("verify")

        legs = {"more": leg_more, "loop": leg_loop, "read": leg_read,
                "char": leg_char, "save": leg_save, "restore": leg_restore,
                "restart": leg_restart, "verify": leg_verify}
        try:
            for leg in a.legs:
                legs[leg]()
        except (os88marty.MartyError, os88ui.UIError, RuntimeError) as e:
            fails.append("%s run stopped: %s"
                         % ("move" if move else "control",
                            str(e).split(". ")[0]))
            return out, fails

        err, dead = z.b("zf_err"), z.b("zf_dead")
        if err or dead:
            fails.append("the VM stopped: zf_err %02x zf_dead %d" % (err, dead))
        if move and "save" in a.legs:
            # ONE FILE PER SAVE: what the disk holds, read off the flushed
            # image by an independent FAT walker (tools/os88flush.py)
            names = os88flush.Flush(marty=m).volume(1).names("")
            out["_files"] = sorted(n for n in names if n.endswith(".SAV"))
            print("  save files on B: %s" % out["_files"])
            if out["_files"] != ["ZRS.SAV", "ZRS2.SAV"]:
                fails.append("save files %s, want ZRS.SAV and ZRS2.SAV"
                             % out["_files"])
    return out, fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--no-control", action="store_true",
                    help="the move run alone (development)")
    ap.add_argument("--control-only", action="store_true",
                    help="the no-move boot alone (development)")
    ap.add_argument("--legs", default=",".join(LEGS),
                    help="a comma list out of %s (a red run's subset); "
                    "restore needs save before it" % ",".join(LEGS))
    a = ap.parse_args()
    a.legs = [x for x in LEGS if x in a.legs.split(",")]
    if "restore" in a.legs and "save" not in a.legs:
        ap.error("restore opens the files save writes")
    os88fixture.need(DISK)
    if a.control_only:
        ref, fails = run(a, False)
        for leg in ref:
            print(leg, ref[leg] if leg.startswith("_") else
                  (ref[leg]["dyn"], ref[leg]["sp"]))
        print(ref.get("verify", {}).get("text", "")[-1500:])
        return 1 if fails else 0
    got, fails = run(a, True)
    if not a.no_control:
        ref, f2 = run(a, False)
        fails += ["control: " + f for f in f2]
        for leg in ref:
            if leg.startswith("_"):
                continue
            g, r = got.get(leg), ref[leg]
            if g is None:
                fails.append("%s: no reading" % leg)
                continue
            for k in ("dyn", "sp", "stk", "text"):
                if g[k] != r[k]:
                    fails.append("%s: %s differs from the no-move run" % (leg, k))
                    if k == "text":
                        print("---- move\n%s\n---- control\n%s\n----"
                              % (g[k][-600:], r[k][-600:]))
            print("  %-10s dyn %s sp %d stk %s  %s"
                  % (leg, g["dyn"], g["sp"], g["stk"],
                     "same" if all(g[k] == r[k] for k in r) else "DIFFERENT"))
    print("zrestart: %s" % ("FAIL: " + "; ".join(fails) if fails
                            else "every park site restarted losslessly"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
