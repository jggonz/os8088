#!/usr/bin/env python3
"""INSTALL over the network (SPEC.md 117.10), and its startup check (117.10.6).

    make httpmounttest build/install-gate.o88 && python3 tests/netinst.py

QEMU by name: the network card (docs/TESTING.md, item 5). The host serves a
published tree the way `make nettree` builds one - build/os8088.img and
build/apps.img as one tree, an OS88VER.TXT and its OS88.IDX - at
http://10.0.2.2:8096/net/, which is what build/install-gate.o88 is assembled
to ask (the shipped INSTALL asks os8088.com). That package is put on the
machine as A:\\INSTALL.O88.

  A. The check. INSTALL is started from A:; its source page asks the network
     (ONE GET, of SYSTEM/OS88VER.TXT, on a socket of its own - no index, no
     mount), finds a NEWER release and picks the network row; the HTTP
     drive is untouched - HTTPDRV.CFG names http://10.0.2.2:8095/fix/, and
     that is still its address.
  B. The install. Enter through every page onto the IDE disk's FAT16 volume
     (kept); HDI_GO NAMES the HTTP drive as the source, so the run copies the
     tree from it and does not go looking for an apps disk - and there IS one
     in B:, carrying BMARK.TXT, which must not arrive (the engine used to find
     it and copy the apps phase off the floppy). C: holds NETMARK.TXT (only
     the served tree has it), the apps disk's folders, and
     SYSTEM\\OS88VER.TXT. On the Done page, Down picks *Check for updates
     at startup*, and Restart writes C:\\AUTOSTRT\\OSCHECK.LNK naming
     INSTALL.O88 with the parameter CHECK (26.10.7).
  C. Started at boot with CHECK and a release that is NOT newer: no window,
     one toast. The same link without the parameter is the A/B, and opens
     the window - so this leg fails if OSAPI_ARG_TEXT stops answering.

BREAK IT ON PURPOSE: serve an OS88VER.TXT whose release is INSTALL's own
(`--same`) and leg A finds the network row not the default, so B never
starts from the network and fails on NETMARK.TXT - and with the apps disk in
B: it fails on BMARK.TXT as well.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import httpinstall as HI                                      # noqa: E402
import httpmount as HM                                        # noqa: E402
import os88build                                              # noqa: E402
import os88httpdrv                                            # noqa: E402
import os88lnk                                                # noqa: E402
import os88nettree                                            # noqa: E402
import os88qemu                                               # noqa: E402
import xmcheck                                                # noqa: E402
from os88geom import WIN_SIZE, MAX_WIN                        # noqa: E402

PORT = 8096
SAME = "--same" in sys.argv[1:]
MARK = b"this file is on the SERVED tree and nowhere else\r\n"
W_SEG = 22
NET_LOOK, NET_SAME, NET_NEW = 1, 4, 5
P_WELCOME, P_DONE, P_BUSY = 0, 6, 8


def gate_syms():
    out = {}
    for line in open(os88build.at("build/install-gate.map")):
        f = line.split()
        if len(f) == 3 and f[2].startswith("in_"):
            try:
                out[f[2]] = int(f[0], 16)
            except ValueError:
                pass
    return out


def rel_of():
    return open("apps/install/OS88VER.TXT", "rb").read().split(b"\n")[0].strip()


def install_seg(q, before):
    """The code segment of the INSTALL window that was not in `before`."""
    b = q.read(xmcheck.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        o = i * WIN_SIZE
        if struct.unpack_from("<H", b, o)[0] & 2 and i not in before:
            return struct.unpack_from("<H", b, o + W_SEG)[0], i
    return None, None


def key(q, k):
    q.hmp("sendkey %s" % k)         # (CONFIRMED: xmcheck.qmp, 17.5)
    os88qemu.pace(q, 0.3)           # GUEST seconds, where it was the host's


def fat16_dir(img, lba, path):
    """{name: (attr, cluster, size)} of a folder on the installed volume."""
    f = HI.Fat16(img, lba)
    raw = f.img
    ents = []
    if not path:
        ents = [raw[f.root + o * 32:f.root + o * 32 + 32] for o in range(f.nroot)]
    else:
        parent = fat16_dir(img, lba, path[:-1])
        a, c, _ = parent[path[-1]]
        body = f.body(c, 1 << 20)
        ents = [body[o:o + 32] for o in range(0, len(body), 32)]
    out = {}
    for e in ents:
        if len(e) < 32 or e[0] == 0:
            break
        if e[0] in (0xE5, 0x2E) or e[11] & 0x08:
            continue
        n = e[:8].rstrip().decode("latin-1")
        if e[8:11].strip():
            n += "." + e[8:11].rstrip().decode("latin-1")
        out[n] = (e[11], struct.unpack_from("<H", e, 26)[0],
                  struct.unpack_from("<I", e, 28)[0])
    return out


def onscreen(q, ui, w, idx):
    """Bring entry `idx` of Disk window `w` into its view: httpinstall's walk
    - the first visible row clicked to select it, then the arrows, which move
    the selection and the view with it (SPEC.md 22.26) - for kern_mega's A:,
    mega.img's twin, whose root has five folders ahead of INSTALL.O88 and so
    lists it past the window's eight rows. Answers idx."""
    import hmacold
    mo = hmacold.QMouse(q)
    base = ui._fsblk(w)
    if base is None:
        base = (HM.geom.KERNEL_SEG << 4) + ui._word("fm_vp")
    sel = lambda: struct.unpack("<H", q.read(base + HM.geom.FS_SEL, 2))[0]  # noqa: E731
    mo.to(*ui.row_xy(w, 0))
    mo._edge(True)
    mo._edge(False)
    os88qemu.acted(q, lambda: sel() == ui.scroll(w), secs=10,
                   what="the first row to be selected")
    for _ in range(64):
        was = sel()
        if was == idx:
            break
        xmcheck.qmp(q.sock, "sendkey %s" % ("up" if was > idx else "down"))
        os88qemu.acted(q, lambda: sel() != was, secs=5,
                       what="the selection to move")
    os88qemu.ui_done(q, xmcheck.sym, cap=2.0)
    return idx


def machine(tmp, hd, gate, link=None):
    sysimg = os.path.join(tmp, "sys.img")
    shutil.copy(os88build.at("build/httpmount.img"), sysimg)
    cfg = os.path.join(tmp, "SYSTEM.CFG")       # HDD, ETHER and HTTPFS wanted
    open(cfg, "wb").write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                          + b"DW" + bytes([1, 2])
                          + ((1 << 1) | (1 << 4) | (1 << 10)).to_bytes(2, "little")
                          + b"\0\0")
    HI.put(sysimg, cfg, "SYSTEM.CFG")
    HI.put(sysimg, gate, "INSTALL.O88")
    if link is not None:
        lnk = os.path.join(tmp, "OSCHECK.LNK")
        open(lnk, "wb").write(link)
        subprocess.run(["mmd", "-i", sysimg, "::AUTOSTRT"],
                       capture_output=True)         # (it may be there)
        subprocess.run(["mcopy", "-i", sysimg, lnk, "::AUTOSTRT/OSCHECK.LNK"],
                       check=True, capture_output=True)
    bimg = os.path.join(tmp, "b.img")          # an APPS disk, marked
    shutil.copy(os88build.at("build/apps.img"), bimg)
    bm = os.path.join(tmp, "BMARK.TXT")
    open(bm, "wb").write(b"from the floppy in B:\r\n")
    HI.put(bimg, bm, "BMARK.TXT")
    subprocess.run(["mcopy", "-i", bimg, bm, "::APPS/BMARK.TXT"],
                   check=True, capture_output=True)
    q = HI.Q(tmp, sysimg, bimg, hd)
    os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30, what="QMP")
    xmcheck.wait_desktop(q.sock, "netinst")
    return q


def main():
    gate = os88build.at("build/install-gate.o88")
    syms = gate_syms()
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    q = httpd = None
    log = HM.Log()
    bad = []
    try:
        # --- the served tree ----------------------------------------------
        web = os.path.join(tmp, "web")
        os.makedirs(web)
        rel = rel_of()
        newrel = rel if SAME else b"99991231"
        ver = os.path.join(tmp, "OS88VER.TXT")
        open(ver, "wb").write(newrel + b"\r\nA line of what is new,\r\n"
                              b"and a second one.\r\n")
        os88nettree.build(os88build.at("build/os8088.img"),
                          os88build.at("build/apps.img"), ver,
                          os.path.join(web, "net"))
        open(os.path.join(web, "net", "NETMARK.TXT"), "wb").write(MARK)
        tree, attrs = os88httpdrv.load(os.path.join(web, "net"))
        idx = os88httpdrv.index_build(tree, 0, attrs)
        open(os.path.join(web, "net", os88httpdrv.IDX_NAME), "wb").write(idx)
        HM.PORT = PORT
        httpd = HM.server(web, log)
        print("   served: release %s (INSTALL's own is %s), %d entries"
              % (newrel.decode(), rel.decode(), len(os88httpdrv.index_parse(idx))))

        hd = os.path.join(tmp, "hd.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", hd, "--hdd",
                        "--mbr", os88build.at("build/mbr.bin"),
                        "--boot", os88build.at("build/boothd.bin"),
                        "--kernel", os88build.at("build/kernel.sys"),
                        os88build.at("build/readme.txt")],
                       check=True, capture_output=True)
        with open(hd, "r+b") as f:      # the MBR's CODE blanked, so the
            f.write(bytes(446))         # install's commit is a change

        # --- A: the check --------------------------------------------------
        q = machine(tmp, hd, gate)
        S = xmcheck.sym
        ui = HM.os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        xmcheck.dblclick(q.sock, *HM.geom.drive_pt(q, "A", S))
        q.need(lambda: HM.front_on(ui, 0) is not None, "A:'s window", 60)
        q.done()
        w = HM.front_on(ui, 0)
        before = {x[0] for x in HI.wins(q)}
        i, _ = ui.entry("INSTALL.O88", w)
        if os88qemu.MEGA:               # kern_mega's A: is mega.img's twin,
            i = onscreen(q, ui, w, i)   # whose root lists INSTALL.O88 below
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))  # the fold
        q.need(lambda: install_seg(q, before)[0] is not None, "INSTALL", 60)
        seg, _ = install_seg(q, before)
        rd = lambda n: q.readseg(seg, syms[n], 1)[0]           # noqa: E731
        q.need(lambda: rd("in_net") != NET_LOOK, "the network's answer", 90)
        net, src = rd("in_net"), rd("in_src")
        print("   A. in_net %d, source row %d, requests %r"
              % (net, src, [g for g in log.gets()][-3:]))
        if [g for g in log.gets() if not g.endswith("/SYSTEM/OS88VER.TXT")]:
            bad.append("A. the check asked for more than SYSTEM/OS88VER.TXT "
                       "(%r): it is one GET of its own, never the HTTP "
                       "drive's mount and index (SPEC.md 117.10.3)"
                       % log.gets())
        want = NET_SAME if SAME else NET_NEW
        if net != want:
            bad.append("A. the check answered %d, not %d" % (net, want))
        if not SAME and src != 0:
            bad.append("A. a newer release did not pick the network row")
        hrow = q.read(S("drv_tab") + HM.HF_ROW * HM.DRVR_SIZE, HM.DRVR_SIZE)
        hseg = struct.unpack_from("<H", hrow, HM.geom.DRVR_SEG)[0]
        hsyms = HM.os88drvsym.symbols("httpfs.bin")
        url = q.read(os88qemu.drv_base(hseg) + hsyms["hf_url"],
                     96).split(b"\0")[0]       # a selector's base, kern_mega
        print("   A. the drive's address afterwards: %r" % url)
        if url != b"http://10.0.2.2:8095/fix/":
            bad.append("A. the user's HTTP drive was not put back: %r" % url)
        if os.environ.get("NETINST_SHOT"):     # a look at the source page
            q.done(2.0)
            subprocess.run([sys.executable, "tools/shot.py", q.sock,
                            os.environ["NETINST_SHOT"] + "-source.png"])

        # --- B: the install -----------------------------------------------
        mbr0 = open(hd, "rb").read(446)
        for page in range(5):           # source, drive, slot, mode, confirm
            p0 = rd("in_page")
            key(q, "ret")
            q.need(lambda: rd("in_page") != p0, "the page after %d" % p0, 120)
            q.done()
        q.need(lambda: rd("in_logend") == 1, "the install's end", 600)
        key(q, "ret")
        q.need(lambda: rd("in_page") == P_DONE, "the Done page", 60)
        committed = open(hd, "rb").read(446) != mbr0
        print("   B. Done; MBR %s" % ("committed" if committed else "UNCHANGED"))
        if not committed:
            bad.append("B. the install never committed the MBR")
        if os.environ.get("NETINST_SHOT"):
            subprocess.run([sys.executable, "tools/shot.py", q.sock,
                            os.environ["NETINST_SHOT"] + "-done.png"])
        key(q, "down")
        q.need(lambda: rd("in_chk") == 1, "the startup row", 20)
        key(q, "ret")                   # Restart: the link, then the reboot
        # THE DISK STILL, ON THE GUEST'S CLOCK: four readings a guest second
        # apart, after three guest seconds - it was the same shape in host
        # seconds, which a loaded box gave the guest less of (17.9). The
        # reboot's POST re-sets the tick count, which os88qemu.Clock counts
        # as one tick and not thousands
        os88qemu.pace(q, 3.0)
        os88qemu.quiesce(q, lambda: open(hd, "rb").read(), secs=1.0,
                         stable=5, limit=120.0, what="C: after the Restart")
        q.close()
        q = None
        part = struct.unpack_from("<I", open(hd, "rb").read(512), 446 + 8)[0]
        root = fat16_dir(hd, part, [])
        if "NETMARK.TXT" not in root:
            bad.append("B. NETMARK.TXT is not on C: - the install did not come "
                       "from the network")
        if "BMARK.TXT" in root or ("APPS" in root and "BMARK.TXT" in
                                   fat16_dir(hd, part, ["APPS"])):
            bad.append("B. BMARK.TXT is on C: - the apps came off the floppy "
                       "in B:, not the network (SPEC.md 117.10.5)")
        if "SYSTEM" not in root or "OS88VER.TXT" not in fat16_dir(
                hd, part, ["SYSTEM"]):
            bad.append("B. SYSTEM\\OS88VER.TXT is not on C:")
        if "OS88VER.TXT" in root:
            bad.append("B. OS88VER.TXT is in C:'s root, not SYSTEM\\")
        for d in ("APPS", "GAMES"):
            if d not in root:
                bad.append("B. %s\\ is not on C: - the apps did not come with "
                           "the tree" % d)
        nap = len(fat16_dir(hd, part, ["APPS"])) if "APPS" in root else 0
        print("   B. C: root %d entries, APPS %d" % (len(root), nap))
        lk = None
        if "AUTOSTRT" in root:
            ents = fat16_dir(hd, part, ["AUTOSTRT"])
            if "OSCHECK.LNK" in ents:
                f = HI.Fat16(hd, part)
                lk = f.body(ents["OSCHECK.LNK"][1], ents["OSCHECK.LNK"][2])
        if lk is None:
            bad.append("B. C:\\AUTOSTRT\\OSCHECK.LNK was not written")
        else:
            print("   B. OSCHECK.LNK: %r, parameter %r"
                  % (os88lnk.parse(lk), os88lnk.param(lk)))
            if os88lnk.parse(lk)[2] != "INSTALL.O88" or os88lnk.param(lk) != "CHECK":
                bad.append("B. OSCHECK.LNK does not name INSTALL.O88 with CHECK")

        # --- C: started at boot, nothing newer -----------------------------
        open(ver, "wb").write(rel + b"\r\n")
        os88nettree.build(os88build.at("build/os8088.img"),
                          os88build.at("build/apps.img"), ver,
                          os.path.join(web, "net"))
        for arg, expect in (("CHECK", False), (None, True)):
            n0 = len(log.gets())
            q = machine(tmp, hd, gate, os88lnk.make("INSTALL.O88", arg=arg))
            os88qemu.pace(q, 4.0)       # (the autostart's: GUEST seconds)
            q.done(4.0)
            got = install_seg(q, set())[0] is not None
            asked = [g for g in log.gets()[n0:] if g.endswith("OS88VER.TXT")]
            print("   C. link %s: a window %s, OS88VER.TXT asked %d time(s)"
                  % ("with CHECK" if arg else "without a parameter",
                     "opened" if got else "did not open", len(asked)))
            if got != expect:
                bad.append("C. with %s the window %s" % (
                    arg or "no parameter", "opened" if got else "did not open"))
            if arg and not asked:
                bad.append("C. CHECK did not ask the network")
            q.close()
            q = None
    finally:
        if q:
            q.close()
        if httpd:
            httpd.shutdown()
        shutil.rmtree(tmp, ignore_errors=True)
    if bad:
        for b in bad:
            print("netinst: FAIL - " + b)
        sys.exit(1)
    print("netinst: ok")


if __name__ == "__main__":
    main()
