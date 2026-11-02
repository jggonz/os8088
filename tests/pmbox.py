#!/usr/bin/env python3
"""THE PICOMEM, IN 86BOX, RUNNING ITS OWN FIRMWARE (docs/plans/completed/PICOMEM-EMU-PLAN.md).

    make pmboxtest && tools/picomem/build.sh && python3 tests/pmbox.py [--keep DIR]

vm/xt-picomem1 is a 640KB 8088 XT with an OAK VGA and a PicoMEM 1.x on IRQ 5
- tools/86box/patches/07-picomem.patch, a thin adapter around libpmhost, the
card's own firmware (Elendilon/ISA-PicoMEM pm1x-pr) compiled for the host. It
boots build/pmsys360.img (SOUND, ETHER and USBMOUSE wanted) with
build/pmboxb360.img in B: (PMDMA and SBTEST) through os88ui under $OS88_EMU=86box, with the card's command
log (OS88_PMHOST_LOG) and its DAC (OS88_PMWAV) captured. What must hold:

  1. THE CARD CAME UP AS ITS BIOS LEAVES IT: the log shows the BIOS's IRQ
     detection (30h) and DEV_Init (09h) before os8088 sent anything;
  2. SOUND.DRV ATTACHED ON IT, in SPEC.md 34.10's order: the DSP off first
     (78h 0, 34.10.2), the AdLib on (73h 1), an IRQ that is not the card's
     own 5 (77h), then 78h at 220h - every one answered 0;
  3. THE 1.x's VIRTUAL DMA WORKS END TO END: PMDMA's R runs a 64-byte
     single-cycle transfer per way of programming channel 1 (sb.inc's
     sequence; with reads between; PMINIT's order; the page first) and every
     one of the four comes back RIGHT - the card snooped the 8237 writes,
     asked the CPU through its IRQ multiplexer, and the PicoMEM BIOS's
     handler copied the 64 bytes from where they were programmed;
  4. THE CARD'S DAC PLAYED SBTEST'S TONE: SBTEST's click plays 2 s of a
     1 kHz square at 8,000 Hz through OSAPI_SND_STREAM, and the card's own
     output (49,716 Hz, its OPL's rate) carries it - a level that moves, and
     a period of 1 ms.

Then leg mouse (check 9), on vm/xt-picomem1-usbm - no 86Box mouse, the
card's USB mouse fed 86Box's pointer: USBMOUSE.DRV turns it on (52h) and
os88ui double-clicks A: open through the card's IRQ and int 33h AX=0060h.

Leg mega (check 10) is kern_mega on vm/at286-picomem1, the card its only
mouse: the card's int 33h caught inside kern_mega's IRQ reflection drives
os88ui, and SBTEST's tone plays through reflected multiplexer IRQs.
`--board 2` runs the legs on a 2.x (vm/xt-picomem2) and leaves PMDMA and
the mega leg out.

Broken on purpose, OS88_PMHOST_NOSNOOP=1 (the card stops hearing the PC's
8237 writes): MEASURED below in the plan's 6.3 - A to D still read right
(the BIOS's copy takes its source from the 8237 itself), but PMDMA's
auto-init stream never ends, so its window never closes, and FAILS.
"""
import argparse
import os
import re
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

FAILS = []
VM = "xt-picomem1"      # --board 2: xt-picomem2, its -1fd and -usbm twins
OWN = 5                 # the card's own line: the 1.x's jumper, the 2.x's IRQ 7


def check(cond, what):
    print(("  ok    " if cond else "  FAIL  ") + what)
    if not cond:
        FAILS.append(what)


def log_cmds(text):
    """[(cmd, arg, status, answer)] off the card's command log"""
    out, open_ = [], {}
    for line in text.splitlines():
        m = re.search(r"PicoMEM: cmd ([0-9A-F]{2})h arg ([0-9A-F]{4})h", line)
        if m:
            open_ = {"c": int(m.group(1), 16), "a": int(m.group(2), 16)}
            continue
        m = re.search(r"PicoMEM: cmd ([0-9A-F]{2})h done: status (\d+) answer ([0-9A-F]{4})h", line)
        if m and open_ and open_["c"] == int(m.group(1), 16):
            out.append((open_["c"], open_["a"], int(m.group(2)), int(m.group(3), 16)))
            open_ = {}
    return out


def wav_frames(path):
    """(rate, [(l, r)]) of a 16-bit stereo PCM WAV"""
    d = open(path, "rb").read()
    rate = struct.unpack_from("<I", d, 24)[0]
    n = struct.unpack_from("<I", d, 40)[0] // 4
    fr = struct.unpack_from("<%dh" % (2 * n), d, 44)
    return rate, list(zip(fr[0::2], fr[1::2]))


def check_tone(wav):
    """SBTEST's 2 s of a 1 kHz square, out of the card's own DAC"""
    if os.path.exists(wav) and os.path.getsize(wav) > 44:
        rate, fr = wav_frames(wav)
        left = [l for l, _ in fr]
        # the capture is the whole session - the BIOS's beep and PMDMA's
        # 64-byte transfers click too - so the tone is the LONGEST stretch of
        # 10 ms blocks with any level in them
        blk = rate // 100
        act = [max(left[i:i + blk]) - min(left[i:i + blk]) > 1000
               for i in range(0, len(left) - blk, blk)]
        runs, st = [], None
        for i, on in enumerate(act + [False]):
            if on and st is None:
                st = i
            elif not on and st is not None:
                runs.append((st, i))
                st = None
        a0, b0 = max(runs, key=lambda r: r[1] - r[0]) if runs else (0, 0)
        tone = left[a0 * blk:b0 * blk]
        secs = len(tone) / rate
        mean = sum(tone) / max(1, len(tone))
        ups = sum(1 for i in range(1, len(tone)) if tone[i - 1] <= mean < tone[i])
        hz = ups / secs if secs else 0
        pp = (max(tone) - min(tone)) if tone else 0
        check(1.8 < secs < 2.4, "the card's DAC played one stretch of %.2f s "
              "(SBTEST stages 2 s)" % secs)
        check(950 < hz < 1050 and pp > 2000, "...a %.0f Hz tone at %d peak to "
              "peak (SBTEST's 1 kHz square)" % (hz, pp))
    else:
        check(False, "the card's DAC was captured (%s)" % wav)


def leg_snd(tmp):
    """checks 1-4: the card's boot, SOUND.DRV, PMDMA, SBTEST's tone"""
    wav = os.path.join(tmp, "card.wav")
    os.environ.update(OS88_PMWAV=wav)
    os.environ.pop("OS88_PMSD", None)
    import os88marty
    import os88ui
    from cycweb import pkg_syms, Pkg
    try:
        with os88ui.boot(os.path.join(ROOT, "build/pmsys360.img"),
                         apps=os.path.join(ROOT, "build/pmboxb360.img"),
                         machine="os8088_xt_vga", quick=True) as ui:
            m = ui.m
            logf = os.path.join(m.b.run, "86box.log")

            print("pmbox: the card's boot, and SOUND.DRV's attach")
            cmds = log_cmds(open(logf).read())
            names = [c for c, _, _, _ in cmds]
            check(0x30 in names and 0x09 in names and
                  names.index(0x30) < names.index(0x09),
                  "the BIOS detected the card's IRQ (30h) and ran DEV_Init (09h)")
            att = [x for x in cmds if x[0] in (0x73, 0x77, 0x78)]
            check(len(att) >= 4 and att[0][:2] == (0x78, 0) and
                  att[1][:2] == (0x73, 1) and att[2][0] == 0x77 and
                  att[3][:2] == (0x78, 0x220) and all(x[3] == 0 for x in att[:4]),
                  "SOUND.DRV: 78h 0, 73h 1, 77h, 78h 220h, all answered 0 (%s)"
                  % ["%02Xh %04Xh=%04Xh" % (c, g, r) for c, g, s, r in att])
            check(att[2][1] & 0xFF != OWN if len(att) > 2 else False,
                  "the SB's IRQ is not the card's own %d (77h %04Xh)"
                  % (OWN, att[2][1] if len(att) > 2 else 0))

            # PMDMA asks a 1.x where its VIRTUAL DMA copied from; a 2.x has
            # real DMA lines and copies nothing, so it has no answer to give
            if OWN == 5:
                print("pmbox: PMDMA's R - the 1.x's virtual DMA, four ways")
                syms, _ = pkg_syms("tests/pmdma/pmdma.asm", incs=("apps/", "tests/", "build/"))
                w = ui.path("B:/PMDMA.O88")
                m.key("KeyR")
                p = Pkg(m, os88marty.pkg_seg(m, w.i), syms)
                rd = lambda: list(m.read(p.addr("pd_res"), 4))
                # pd_run starts every variant at "no copy" (4) and reports each
                # as it lands; the R key's whole run is one callback
                try:
                    os88marty.until(m, lambda _: 4 not in rd(), guest=30,
                                    what="PMDMA's four variants to report")
                except os88marty.MartyError:
                    pass
                res = rd()
                check(res == [0, 0, 0, 0], "every variant copied from the address "
                      "programmed (pd_res %s: 0 right, 1 page, 2 wrong, 4 none)" % res)
                # R runs on into G and E - E a 2KB AUTO-INIT stream - in the same
                # callback, so the window closes only once the stream has ended
                try:
                    ui.close(w, limit=120.0)
                    check(True, "PMDMA's whole run ended, its auto-init stream "
                          "included (the window closed)")
                except os88ui.UIError as e:
                    check(False, "PMDMA's whole run ended, its auto-init stream "
                          "included: %s" % str(e).split(".")[0])

            print("pmbox: SBTEST's tone, through the card's DAC")
            w = ui.path("B:/SBTEST.O88")
            x, y, ww, hh = ui._rect(w.i)
            ui.mo.click(x + ww // 2, y + hh // 2, settle=0)
            os88marty.pace(m, 4.0)
    except (os88marty.MartyError, os88ui.UIError) as e:
        check(False, "the machine: %s" % str(e).split(". ")[0])

    check_tone(wav)



def leg_fdd(tmp):
    """checks 5 and 7: the card's own int 13h is B: - on a machine with ONE
    floppy drive (vm/xt-picomem1-1fd), so B: exists only on the card.

    The card's SD holds build/pmboxb360.img, mounted as floppy 1 in its
    PICOMEM.CFG (mksd.py --fdd 1:). The PicoMEM BIOS counts it into the
    equipment word and takes over int 13h for drive 1, and the kernel keeps a
    B: its own probe finds no drive behind because the card's FDD1_Attribute
    says it is an image (SPEC.md 18.97.6, dsk_fdd_pmemu). A B: listing that
    shows PMDMA.O88 is the card's firmware answering every sector through its
    PC-command channel (CMD 88h, the shared disk buffer, the BIOS's REP
    MOVSW). Then Restart (check 7)."""
    import subprocess
    import os88marty
    import os88ui
    sd = os.path.join(tmp, "fdd-sd.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/picomem/mksd.py"), sd,
                    "--fdd", "1:" + os.path.join(ROOT, "build/pmboxb360.img")],
                   check=True, stdout=subprocess.DEVNULL)
    os.environ["OS88_PMSD"] = sd
    os.environ["OS88_86BOX_VM"] = VM + "-1fd"
    os.environ.pop("OS88_PMWAV", None)
    try:
        with os88ui.boot(os.path.join(ROOT, "build/pmsys360.img"),
                         machine="os8088_xt_vga", quick=True) as ui:
            m = ui.m
            cmds = log_cmds(open(os.path.join(m.b.run, "86box.log")).read())
            ui.open_drive("B")
            names = [e.name if hasattr(e, "name") else str(e) for e in ui.listing()]
            check(any("PMDMA" in n.upper() for n in names),
                  "a machine with ONE drive has a B:, and it lists the card's "
                  "image (%s)" % names[:8])
            n88 = sum(1 for c in cmds if c[0] == 0x88)
            cmds = log_cmds(open(os.path.join(m.b.run, "86box.log")).read())
            check(sum(1 for c in cmds if c[0] == 0x88) > n88,
                  "...through the card's int 13h (CMD 88h %d -> %d)"
                  % (n88, sum(1 for c in cmds if c[0] == 0x88)))

            # check 7: RESTART on a PicoMEM is a WARM reset (SPEC.md 18.100.1:
            # 1234h at 0040:0072, then FFFF:0), so POST runs the card's BIOS
            # again - which int 19h would not. Posted as fddpark.py posts it.
            # DEV_Init (09h), not the IRQ detection: a warm boot keeps the
            # line the card's BIOS found at power-on and skips 30h
            n30 = sum(1 for c in cmds if c[0] == 0x09)
            m.write(m.sym("ui_rebootq"), bytes([2]))      # UI_RBQ_NOFLUSH
            logf = os.path.join(m.b.run, "86box.log")
            n30s = lambda: sum(1 for c in log_cmds(open(logf).read()) if c[0] == 0x09)
            try:
                os88marty.until(m, lambda _: n30s() > n30, guest=60,
                                what="the card's BIOS to run again")
            except os88marty.MartyError:
                pass
            again = n30s()
            if again > n30:
                ui.up()
            check(again > n30, "Restart re-ran the card's BIOS (DEV_Init, "
                  "09h: %d -> %d) and os8088 came back" % (n30, again))
            ui.open_drive("B")
            names = [str(e) for e in ui.listing()]
            check(any("PMDMA" in n.upper() for n in names),
                  "...with the card's B: still mounted (%s)" % names[:4])
    except (os88marty.MartyError, os88ui.UIError) as e:
        check(False, "the machine: %s" % str(e).split(". ")[0])
    finally:
        os.environ.pop("OS88_PMSD", None)
        os.environ["OS88_86BOX_VM"] = VM


def leg_hdd(tmp):
    """check 6: os8088 BOOTS off a hard disk the card serves (SPEC.md 52.10.16).

    build/pmhd.img - a bootable os8088 FAT16 hard disk - is HDD 0 on the
    card's microSD (mksd.py --hdd 0:), A: is EMPTY, and the machine must
    reach a desktop: the XT's POST finds no floppy, the PicoMEM BIOS's int
    13h answers 80h, the MBR and boothd load the kernel through it, and the
    kernel's hard-disk boot volume reads the drivers through it too."""
    import subprocess
    import os88marty
    import os88ui
    sd = os.path.join(tmp, "hdd-sd.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/picomem/mksd.py"), sd,
                    "--mb", "64", "--hdd", "0:" + os.path.join(ROOT, "build/pmhd.img"),
                    # the card's own boot strap: an XT BIOS goes to ROM BASIC
                    # when its floppy is empty, as this one did without it
                    "--cfg", "PMBOOT=1"],
                   check=True, stdout=subprocess.DEVNULL)
    os.environ["OS88_PMSD"] = sd
    os.environ.pop("OS88_PMWAV", None)
    try:
        with os88ui.boot(None, apps=os.path.join(ROOT, "build/apps360.img"),
                         machine="os8088_xt_vga", quick=True) as ui:
            m = ui.m
            cmds = log_cmds(open(os.path.join(m.b.run, "86box.log")).read())
            n88 = sum(1 for c in cmds if c[0] == 0x88)
            check(n88 >= 10, "a desktop, off the card's hard disk with A: empty "
                  "(%d int 13h calls through CMD 88h; the loader reads in "
                  "long runs)" % n88)
            ui.open_drive("C")
            names = [str(e) for e in ui.listing()]
            # the kernel and drivers are SYSTEM-attributed and not listed; the
            # SYSTEM folder is pmhd.img's, and A: is empty
            check(any("SYSTEM" in n.upper() for n in names),
                  "C: is the card's disk: its SYSTEM folder lists (%s)" % names[:6])
    except (os88marty.MartyError, os88ui.UIError) as e:
        check(False, "the machine: %s" % str(e).split(". ")[0])
    finally:
        os.environ.pop("OS88_PMSD", None)


def leg_net(tmp):
    """check 8: ETHER.DRV finds the card's NE2000 and DHCP binds through it.

    The default card (tools/picomem/build.sh's sd.img) has its NE2000 at
    300h on IRQ 3, as the field cards do. Its wire is the Pico W's Wi-Fi,
    which here is 86Box's slirp through the companion NIC picomem_ne2k. So
    ETHER.DRV's probe has to accept the card's un-doubled PROM (SPEC.md
    72.2), its 8390 has to send the DISCOVER and take the OFFER and ACK into
    its ring, and the card's multiplexer has to raise the NE2000's IRQ as
    int 0Bh - and the lease is slirp's 10.0.2.15."""
    import os88marty
    import os88ui
    import ethernet as eth
    syms = eth.ether_syms()
    os.environ.pop("OS88_PMSD", None)
    os.environ.pop("OS88_PMWAV", None)
    try:
        with os88ui.boot(os.path.join(ROOT, "build/pmsys360.img"),
                         machine="os8088_xt_vga", quick=True) as ui:
            m = ui.m
            S = m.sym
            drvseg = lambda: eth.u16(m.read(S("drv_tab") + eth.ETH_ROW * eth.DRVR_SZ
                                            + eth.DRVR_SEG, 2))
            seg = drvseg()
            check(seg != 0, "ETHER.DRV attached (segment %04X)" % seg)
            if not seg:
                return
            rd = lambda n, k: bytes(m.read(seg * 16 + syms[n], k))
            base = eth.u16(rd("eth_base", 2))
            mac = rd("eth_mac", 6)
            check(base == 0x300 and mac == bytes([0x28, 0xCD, 0xC1, 0x08, 0x80, 0x88]),
                  "...on the card's NE2000: base %03Xh, station %s" % (base, mac.hex()))
            try:
                os88marty.until(m, lambda _: rd("dhcp_st", 1)[0] == eth.DH_BOUND,
                                guest=30, what="DHCP to bind")
            except os88marty.MartyError:
                pass
            ip = eth.dotted(rd("eth_ip", 4))
            check(rd("dhcp_st", 1)[0] == eth.DH_BOUND and ip == "10.0.2.15",
                  "DHCP bound through the card: %s (state %d), %d frames in, %d out"
                  % (ip, rd("dhcp_st", 1)[0], eth.u16(rd("eth_nrx", 2)),
                     eth.u16(rd("eth_ntx", 2))))
    except (os88marty.MartyError, os88ui.UIError) as e:
        check(False, "the machine: %s" % str(e).split(". ")[0])


def leg_mouse(tmp):
    """check 9: the card's USB mouse is the machine's only pointer.

    vm/xt-picomem1-usbm has no 86Box mouse at all: the adapter hands 86Box's
    pointer to the card as USB reports (pm_mouse_move, every 8 ms), and the
    card's firmware raises its IRQ and its BIOS does int 33h AX = 0060h -
    which only USBMOUSE.DRV's PicoMEM backend answers (SPEC.md 9.12.7). So
    the driver turned the card's mouse on (52h), and os88ui reaching a drive
    zone and DOUBLE-CLICKING it open is movement and buttons through all of
    that. MEASURED broken on purpose two ways: with the card's mouse off
    and no 86Box mouse, the pointer is stuck; with USBMOUSE.DRV built in the
    old order (52h's enable before the int 33h hook), the boot below never
    reaches the desktop - and with that build and no early movement it does,
    so the freeze is the field's: the order AND a report already waiting."""
    import os88marty
    import os88ui
    os.environ.pop("OS88_PMSD", None)
    os.environ.pop("OS88_PMWAV", None)
    os.environ["OS88_86BOX_VM"] = VM + "-usbm"
    try:
        # Launched PAUSED and the mouse moved FIRST: the card then holds a
        # report from before os8088 started, which its firmware raises the
        # instant 52h turns the mouse on (SPEC.md 9.12.7, FIELD-NOTES 66) -
        # so this boot is the one that froze the 5150 when the driver enabled
        # the mouse before hooking int 33h.
        cm = os88marty.launch(os.path.join(ROOT, "build/pmsys360.img"),
                              machine="os8088_xt_vga", boot=False)
        m = cm.__enter__()
        try:
            m.b.mouse(12, 9, 0)
            m.run()
            ui = os88ui.UI(m)
            ui.up(limit=180.0)
            os88marty.no_saver(m)
            leg_mouse_up(ui, m)
        finally:
            cm.__exit__(None, None, None)
    except (os88marty.MartyError, os88ui.UIError, SystemExit) as e:
        check(False, "the machine: %s" % str(e).split(". ")[0])
    finally:
        os.environ["OS88_86BOX_VM"] = VM


def leg_mouse_up(ui, m):
    """check 9's two halves, on a machine that is up"""
    cmds = log_cmds(open(os.path.join(m.b.run, "86box.log")).read())
    on = [x for x in cmds if x[0] == 0x52]
    check(bool(on) and on[-1][3] & 0xFF == 0,
          "USBMOUSE.DRV turned the card's mouse on (52h, %s)" %
          ["%02Xh=%04Xh" % (c, r) for c, _, _, r in on])
    ptr = lambda: tuple(struct.unpack("<HH", bytes(m.read(m.sym("mouse_x"), 4))))
    p0 = ptr()
    w = ui.open_drive("A")
    p1 = ptr()
    check(p1 != p0 and w is not None,
          "the pointer moved %r -> %r and the double-click opened A:"
          % (p0, p1))
    ui.close(w)


def leg_mega(tmp):
    """check 10: KERN_MEGA WITH THE CARD - the 286 protected-mode kernel on
    vm/at286-picomem1 (the kern_mega rows' mr286, plus a PicoMEM 1.x whose
    USB mouse is the machine's only pointer), SOUND, ETHER and USBMOUSE
    wanted. Every interrupt the card raises is its option ROM's, so
    kern_mega REFLECTS it to real mode (mega_irqrom, KERN-MEGA-PLAN 10.3
    item 18) - and the two things that ride those reflections are the ones
    this boots: SBTEST's tone, every 128-byte block of it a reflected
    multiplexer IRQ whose ROM handler copies it, and the card's mouse, whose
    ROM does int 33h AX=0060h inside that reflection, caught by mega_cap33
    and replayed in protected mode (14.117 - until now only ever run
    against a model ROM under Bochs). The mouse is moved before the machine
    runs, as in check 9."""
    import os88marty
    import os88ui
    wav = os.path.join(tmp, "mega.wav")
    os.environ.update(OS88_MEGA="1", OS88_MEGA_EMU="86box",
                      OS88_MEGA_VM="at286-picomem1", OS88_PMWAV=wav)
    os.environ.pop("OS88_PMSD", None)
    try:
        import os88box86api
        # build/mega.img with a SYSTEM.CFG wanting SOUND, ETHER and USBMOUSE
        # (the pmsys disks' set), written the way the backends write theirs
        a = os88box86api._with_cfg(os.path.join(ROOT, "build/mega.img"),
                                   (1 << 0) | (1 << 4) | (1 << 6))
        cm = os88marty.launch(a, apps=os.path.join(ROOT, "build/pmboxb360.img"),
                              machine="os8088_xt_vga", boot=False)  # (the profile is the AT)
        m = cm.__enter__()
        try:
            m.b.mouse(12, 9, 0)
            m.run()
            ui = os88ui.UI(m)
            ui.up(limit=240.0)
            os88marty.no_saver(m)
            feat = m.read(m.sym("cpu_feat"), 1)[0]
            check(feat & 0x10, "kern_mega is in PROTECTED mode (cpu_feat %02Xh "
                  "has CPU_F_PM)" % feat)
            cmds = log_cmds(open(os.path.join(m.b.run, "86box.log")).read())
            names = [c for c, _, _, _ in cmds]
            att = [x[:2] for x in cmds if x[0] in (0x73, 0x77, 0x78)]
            check(0x52 in names and att[:2] == [(0x78, 0), (0x73, 1)]
                  and (0x78, 0x220) in att,
                  "kern_mega is up with the card: SOUND.DRV's 78h 0, 73h 1 ... "
                  "78h 220h and USBMOUSE.DRV's 52h (%d commands)" % len(cmds))
            ptr = lambda: tuple(struct.unpack("<HH", bytes(m.read(m.sym("mouse_x"), 4))))
            p0 = ptr()
            w = ui.path("B:/SBTEST.O88")
            check(ptr() != p0 and w is not None,
                  "the card's mouse, through the reflected int 33h, moved the "
                  "pointer %r -> %r and opened B:'s SBTEST" % (p0, ptr()))
            x, y, ww, hh = ui._rect(w.i)
            ui.mo.click(x + ww // 2, y + hh // 2, settle=0)
            os88marty.pace(m, 4.0)
        finally:
            cm.__exit__(None, None, None)
    except (os88marty.MartyError, os88ui.UIError, SystemExit) as e:
        check(False, "the machine: %s" % str(e).split(". ")[0])
    finally:
        for k in ("OS88_MEGA", "OS88_MEGA_EMU", "OS88_MEGA_VM", "OS88_PMWAV"):
            os.environ.pop(k, None)
    check_tone(wav)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("legs", nargs="*", default=["snd", "fdd", "hdd", "net", "mouse", "mega"],
                    help="snd (checks 1-4), fdd (checks 5, 7), hdd (check 6), "
                         "net (check 8), mouse (check 9), mega (check 10, the 1.x only)")
    ap.add_argument("--keep", help="leave the captures and logs here")
    ap.add_argument("--board", type=int, choices=(1, 2), default=1,
                    help="2: the same legs on a PicoMEM 2.x (vm/xt-picomem2: "
                         "our firmware built PICOMEM_20, IRQ 7 jumpered, DRQ 1)")
    a = ap.parse_args()
    global VM, OWN
    if a.board == 2:
        VM, OWN = "xt-picomem2", 7
    for f in ("build/pmsys360.img", "build/pmboxb360.img", "build/apps360.img",
              "build/pmhd.img",
              "build/picomem/v%d/libpmhost-v%d.so" % (a.board, a.board)):
        if not os.path.exists(os.path.join(ROOT, f)):
            print("SKIP: %s is not built (make pmboxtest; tools/picomem/build.sh)" % f)
            return 0
    tmp = a.keep or tempfile.mkdtemp(prefix="pmbox-")
    os.makedirs(tmp, exist_ok=True)
    os.environ.update(OS88_EMU="86box", OS88_86BOX_VM=VM,
                      OS88_PMHOST_LOG="1")
    if "snd" in a.legs:
        leg_snd(tmp)
    if "fdd" in a.legs:
        leg_fdd(tmp)
    if "hdd" in a.legs:
        leg_hdd(tmp)
    if "net" in a.legs:
        leg_net(tmp)
    if "mouse" in a.legs:
        leg_mouse(tmp)
    if "mega" in a.legs and a.board == 1:
        leg_mega(tmp)
    if not a.keep:
        shutil.rmtree(tmp, ignore_errors=True)
    if FAILS:
        print("pmbox: FAIL (%d)" % len(FAILS))
        return 1
    print("pmbox: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
