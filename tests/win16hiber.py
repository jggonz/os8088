#!/usr/bin/env python3
"""win16hiber - a Windows program across a hibernate (SPEC.md 124.18.6,
docs/plans/WIN16-PLAN.md S12).

    python3 tests/win16hiber.py          MartyPC, os8088_xt_hdd (row win16hiber)

*As a user, hibernating with a Windows program open gives it back to me where
I left it, and it goes on working.* tests/hibernate.py's --sum clean pass
with a Windows program where Mines was: a hard disk of this process's own
(hibernate.py's lesson) whose root holds the kernel, the modules, DOS.O88,
WIN16.O88 and MENU.EXE (a fixture, `make win16fixtures`), so the double-click
goes through the shipped forward (124.18.1) - DOS.O88 and WIN16.O88 found in
the program's own folder, the association's rung 2.

  1. MENU.EXE runs in WIN16, waiting in GetMessage, and the machine
     hibernates from the System menu with it open;
  2. the next boot resumes it: the same WIN16 instance at the same segment,
     still RUNNING (W16S_RUN) - nothing of it was held across the image but
     its own claims (124.18.6);
  3. it is PLAYED ON: F3 makes MENU open a MessageBox, which must come up and
     be answered, and its close box then closes it - WIN16's window gone and
     the instance free.

The hard disk is MartyPC's XT-IDE, as every hibernate row's is; the
assertions are memory reads and never times.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
import hibernate as H                                         # noqa: E402
from hibernate import M, check, done                          # noqa: E402
from win16fpu import pkg_syms, msgrow                         # noqa: E402

W16S_RUNNING, W16S_ENDED = 3, 4
PROG = "MENU.EXE"


def fixture():
    import subprocess
    _B = H._B
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", H.TEMPLATE, "--out", H.VHD,
         "--kernel", _B.at(H.KERNEL), "--vbr", _B.at("build/boothd.bin"),
         "--mbr", _B.at("build/mbr.bin"),
         "--file", "HIBER.DRV=" + _B.at("build/hiber.drv"),
         "--file", "CTRL.DRV=" + _B.at("build/ctrl.drv"),
         "--file", "HDD.DRV=" + _B.at("build/hdd.drv"),
         "--file", "DOS.O88=" + _B.at("build/kdos/DOS.O88"),
         "--file", "WIN16.O88=" + _B.at("build/win16.o88"),
         "--file", "%s=%s" % (PROG, _B.at("build/win16/" + PROG))])


def win16(m, S):
    """WIN16's instance - the live package whose w16_name is the program,
    which DOS.O88's, open for a moment around the forward, never is - and
    its state"""
    raw = m.read(m.sym("inst_tab"), H.I_RECSZ * H.INST_MAX)
    for i in range(H.INST_MAX):
        r = raw[i * H.I_RECSZ:(i + 1) * H.I_RECSZ]
        if r[H.I_STATE] == 1 and r[H.I_KIND] & H.KIND_PKG:
            seg = struct.unpack_from("<H", r, H.I_SPTR)[0]
            if bytes(m.read(seg * 16 + S["w16_name"], len(PROG) + 1)) \
                    == PROG.encode() + b"\0":
                return seg, m.read(seg * 16 + S["w16_state"], 1)[0]
    return None, None


def frame(m, seg):
    """WIN16's window - the instance whose package base is seg - as x, y, w
    out of its kernel window record (W_X, W_Y, W_W); 0, 0, 0 if none"""
    raw = m.read(m.sym("inst_tab"), H.I_RECSZ * H.INST_MAX)
    for i in range(H.INST_MAX):
        r = raw[i * H.I_RECSZ:(i + 1) * H.I_RECSZ]
        if r[H.I_STATE] == 1 and struct.unpack_from("<H", r, H.I_SPTR)[0] == seg:
            win = struct.unpack_from("<H", r, H.I_WIN)[0]
            if win and H.visible(m, win):
                return struct.unpack("<HHH", bytes(m.readseg(0x0060, win + 2, 6)))
    return 0, 0, 0


def pw(m, seg, S, name):
    return struct.unpack("<H", m.read(seg * 16 + S[name], 2))[0]


def pb(m, seg, S, name):
    return m.read(seg * 16 + S[name], 1)[0]


def main():
    if not os.path.exists(H._B.at("build/win16/" + PROG)):
        print("win16hiber: SKIP - no build/win16/%s (make win16fixtures)" % PROG)
        return 77
    import os88ui
    from os88mouse import Mouse
    S = pkg_syms()
    row = msgrow()
    try:
        fixture()
        with H.boot(False) as m:
            mo = Mouse(marty=m)
            ui = os88ui.UI(m, mouse=mo)
            ui.path("C:/" + PROG)
            M.until(m, lambda mm: win16(mm, S)[1] == W16S_RUNNING
                    and pb(mm, win16(mm, S)[0], S, "w16_inprog") == 0,
                    "%s to wait in its message loop" % PROG, poll=0.1, limit=600)
            H.quiet(m)
            seg, st = win16(m, S)
            name = bytes(m.read(seg * 16 + 16, 16)).split(b"\0")[0].decode("latin-1")
            print("1. %s runs in WIN16 at %04x (header name %r); hibernating"
                  % (PROG, seg, name))
            H.hibernate(m, mo, mouse=True)
        ok, have = H.wrote(["HIBERNAT.PTR", "HIBERNAT.IMG"])
        check(ok, "both files are on the disk after power-off", got=have)
        with H.boot(False) as m:
            mo = Mouse(marty=m)
            M.until(m, lambda mm: H.byte(mm, "hb_mode") == H.HB_M_RESUME,
                    "the fresh boot to ask the question", poll=0.05, limit=240)
            H.quiet(m)
            H.answer(m, mo, "[hb_resumes] to reach 1",
                     lambda mm: H.word(mm, "hb_resumes") == 1)
            H.quiet(m)
            seg2, st2 = win16(m, S)
            check(seg2 == seg, "WIN16 came back where it was", got=seg2, want=seg)
            check(st2 == W16S_RUNNING, "...still running its program",
                  got=st2, want=W16S_RUNNING)
            print("2. resumed: WIN16 at %s, state %s" % (
                "%04x" % seg2 if seg2 else None, st2))
            if seg2 != seg or st2 != W16S_RUNNING:
                return
            x, y, ww = frame(m, seg)
            check(ww > 0, "its window is on the resumed desktop", got=(x, y, ww))
            if not ww:
                return
            mo.click(x + ww // 2, y + 8)     # its title bar: to the front
            H.quiet(m)
            m.key("F3")                      # IDM_ASK: a MessageBox
            M.until(m, lambda mm: pw(mm, seg, S, "w16_idx") == row
                    and pb(mm, seg, S, "w16_inprog") == 0,
                    "MENU's MessageBox after the resume", poll=0.1, limit=120)
            H.quiet(m)
            print("3. played on: F3 opened MENU's MessageBox")
            m.key("Enter")
            M.until(m, lambda mm: pw(mm, seg, S, "w16_idx") != row,
                    "the MessageBox to be answered", poll=0.1, limit=60)
            H.quiet(m)
            import os88geom as geom
            mo.click(*geom.close_xy(x, y, ww, m=m, sym=m.sym))
            M.until(m, lambda mm: win16(mm, S)[0] is None,
                    "MENU to close and WIN16 with it", poll=0.1, limit=120)
            check(True, "its close box closed it, and WIN16's instance is free")
    finally:
        H.sweep()
    done("win16hiber")


if __name__ == "__main__":
    sys.exit(main())
