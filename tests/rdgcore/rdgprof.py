#!/usr/bin/env python3
"""Where RealDMG's time goes, by handler and by rdgsys routine, on MartyPC.

    python3 tests/rdgcore/rdgprof.py GAME.GB [--secs S] [--lead S] [--machine M]
    python3 tests/rdgcore/rdgprof.py GAME.GB --box [VM] [--samples N] [--lead S]

Boots GAME.GB in the bare harness (tests/rdgcore/rdghost.asm, MODE 3: gbprof's
platformer input) on MartyPC's cycle-accurate 8088, lets it play for --lead
guest seconds, then samples the live CS:IP from the debug server for --secs
host seconds - tools/os88prof.py's method, which costs the guest nothing. A
sample in the core's segment is charged to the handler or tail holding it, a
sample in the harness's to the rdgsys.inc routine, by the nearest preceding
global label in nasm's listing; the rest is the BIOS.

--box samples 86Box's 286 instead (default vm/286, the ami286 at 12.5 MHz):
the machine is held, stepped one emulated millisecond at a time, and its
CS:IP read between steps through the automation socket - so the sample
clock is emulated time and the host's load cannot bend it. 86Box times
instructions by its model and is not cycle-accurate (docs/86BOX.md); the
8088 profile above is exact.
"""
import argparse
import bisect
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests", "rdgcore"))
import os88marty  # noqa: E402
import rdgimg  # noqa: E402

S_BANK = 0x4000 + 0x35                  # rdgdefs.inc: the ROM bank in 4000-7FFF

HOST, CORE, CACHE, HASH = 0x7600, 0x10000, 0x90000, 0x80000   # rdghost.asm's segments
HASHLEN = 0x10000                       # (its 64KB block table, 16.3)
CACHE2 = 0x70000                        # ...and CACHE2_SEG


def labels(lst, section=None):
    """(offset, name) of every global label in a nasm listing - a label line
    carries no address, so it takes the next one that emits a byte. A label
    a MACRO makes (`%1:` in its body, `PPU_LINE ppu_linep, 1, ...` the call)
    is named by the call's first argument: without it every such routine's
    samples went to the label before it - ppu_linep's to ppu_pal, the
    sprites' to ppu_wplace."""
    out, pending = [], []
    call = None
    for line in open(lst, errors="replace"):
        m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s", line)
        mc = re.match(r"\s*\d+\s+(?:<\d+>\s+)?([A-Z][A-Z0-9_]+)\s+([A-Za-z_][A-Za-z0-9_]*)\b", line)
        if mc and not m:
            call = mc.group(2)
        lab = re.search(r"^\s*\d+\s+(?:[0-9A-F]{8}\s+\S+\s+)?(?:<\d+>\s+)?([A-Za-z_][A-Za-z0-9_]*):", line)
        if not lab and call and re.search(r"^\s*\d+\s+(?:<\d+>\s+)?%1:", line):
            pending.append(call)
            call = None
            continue
        if lab and not m:
            pending.append(lab.group(1))
            continue
        if m:
            a = int(m.group(1), 16)
            if lab:
                pending.append(lab.group(1))
            for n in pending:
                out.append((a, n))
            pending = []
    out.sort()
    return out


def listing(src, inc, extra=()):
    lst = os.path.join(ROOT, "build", "realdmg", os.path.basename(src) + ".lst")
    tmp = os.path.join(ROOT, "build", "realdmg", os.path.basename(src) + ".prof.bin")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error"] + sum([["-I", i] for i in inc], [])
                          + list(extra) + ["-o", tmp, "-l", lst, src])
    os.unlink(tmp)
    return lst


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--secs", type=float, default=60)
    ap.add_argument("--lead", type=float, default=50,
                    help="guest seconds before sampling (with --box: after the harness starts)")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--box", nargs="?", const="286", metavar="VM")
    ap.add_argument("--samples", type=int, default=10000)
    ap.add_argument("--render", action="store_true", help="compose every frame too (MODE 5)")
    ap.add_argument("--renderd", action="store_true",
                    help="...by damage, as the package does (MODE 7)")
    ap.add_argument("--nojit", action="store_true", help="the interpreter alone")
    ap.add_argument("--static", action="store_true",
                    help="the recompiler's Static (REALDMG-SPEC 16.8): 6,000 frames recording, "
                         "then the game again preloaded and fixed; give --lead past the first pass")
    ap.add_argument("--blocks", action="store_true",
                    help="with --box: also the translated samples by block (bank:PC)")
    ap.add_argument("--dump", metavar="PC,PC,...",
                    help="with --blocks: the translated code of these blocks, disassembled")
    ap.add_argument("--hostips", action="store_true",
                    help="the host's samples by instruction, off rdghost's listing")
    ap.add_argument("--pcs", action="store_true",
                    help="with --box: also the Game Boy PCs (SI) the core's samples were at")
    ap.add_argument("--seg2", action="store_true",
                    help="the recompiler's second cache segment (REALDMG-SPEC 16.2.1)")
    ap.add_argument("--vga", action="store_true",
                    help="...and rdg_renderd's lines copied onto VGA mode 0Dh (MODE 12)")
    a = ap.parse_args()
    out = os.path.join(ROOT, "build", "realdmg")
    core, host = rdgimg.build_core(False, out), rdgimg.build_host(out)
    img = os.path.join(out, "prof.img")
    rdgimg.make_image(a.rom, img, 1440 if a.box else 360,
                     "renderv" if a.vga else
                     "renderd" if a.renderd else "render" if a.render else "play",
                     6000 if a.static else 60000, core=core, host=host,
                     jit="static" if a.static else not a.nojit, seg2=a.seg2)
    mine = rdgimg.private(out)               # where this run built its core
    inc = [os.path.join(ROOT, "apps/realdmg/"), os.path.join(ROOT, "tests/rdgcore/"), mine + "/"]
    csyms = labels(listing(os.path.join(mine, "rdgcore.asm"), inc))
    hsyms = labels(listing(os.path.join(ROOT, "tests/rdgcore/rdghost.asm"), inc))
    hsyms = [(o, n) for o, n in hsyms if n not in ("boot", "boot_go", "load")]  # the boot section's
    ck, hk = [o for o, _ in csyms], [o for o, _ in hsyms]
    hits, tot, pcs, jips, per, dumps = {}, 0, {}, [], {}, []
    hips = []

    sys.path.insert(0, os.path.join(ROOT, "apps", "realdmg"))
    import rdggen
    tails = set(rdggen.TAILS)

    def charge(ip):
        if CACHE <= ip < CACHE + 0x10000:       # the recompiler's segment
            off = ip - CACHE
            if (off >> 8) in tails:             # ...its copies of the tails
                i = bisect.bisect_right(ck, off) - 1
                return "tail " + (csyms[i][1] if i >= 0 else "?")
            return "translated code"
        if CACHE2 <= ip < CACHE2 + 0x10000:     # its second segment (16.2.1)
            return "translated code"
        if CORE <= ip < CORE + 0x10000:
            i = bisect.bisect_right(ck, ip - CORE) - 1
            return "core " + (csyms[i][1] if i >= 0 else "?")
        if HOST <= ip < CORE:
            i = bisect.bisect_right(hk, ip - HOST) - 1
            return "host " + (hsyms[i][1] if i >= 0 else "?")
        return "BIOS" if ip >= 0xF0000 else "other %05X" % ip

    if a.box:
        import os88box
        b = os88box.launch(a.box, name="rdgprof", hold=True, fresh=True, a=img)
        try:
            for _ in range(600):                    # to the harness's banner...
                b.cmd("step 500")
                try:
                    t = "".join(bytes.fromhex(x).decode("cp437")
                                for x in b.cmd("text").get("chars", []))
                except SystemExit:                  # not a text mode yet - or, with
                    t = ""                          #   --vga, the harness's mode 0Dh:
                    if a.vga and b.memr(HOST, 1)[0] == 7:   # h_mode, 12 made 7
                        break
                if "EXIT FOR BOOT" in t:            # an AT's first boot stops in setup
                    b.tap("return")
                if "RealDMG core harness" in t:
                    break
            else:
                sys.exit("rdgprof: the harness never started")
            for _ in range(int(a.lead * 2)):        # ...then into play
                b.cmd("step 500")
            for _ in range(a.samples):
                b.cmd("step 1")
                r = b.cmd("regs")
                name = charge(r["cs_base"] + (r["eip"] & 0xFFFF))
                hits[name] = hits.get(name, 0) + 1
                if a.hostips and name.startswith("host"):
                    hips.append(r["eip"] & 0xFFFF)
                if a.blocks and name == "translated code":
                    jips.append(r["eip"] & 0xFFFF)
                if a.pcs and name.startswith("core"):
                    pc = r["esi"] & 0xFFF0
                    if 0x4000 <= pc < 0x8000:       # ...and the bank there
                        pc |= b.memr((r["ss"] << 4) + S_BANK, 1)[0] << 16
                    pcs[pc] = pcs.get(pc, 0) + 1
                tot += 1
            if jips:                                # each translated sample to the
                tab = b.memr(HASH, HASHLEN)         #   block at or below it (16.3),
                blocks = sorted((int.from_bytes(tab[k+4:k+6], "little"),   # the first
                                 int.from_bytes(tab[k:k+2], "little"), tab[k+2])  # segment's
                                for k in range(0, HASHLEN, 8) if (tab[k+4] | tab[k+5]) and
                                int.from_bytes(tab[k+6:k+8], "little") == CACHE >> 4)
                offs = [o for o, _, _ in blocks]
                for ip in jips:
                    k = bisect.bisect_right(offs, ip) - 1
                    if k >= 0:
                        key = "%04X%s" % (blocks[k][1], ":%02X" % blocks[k][2]
                                          if blocks[k][2] != 0xFF else "")
                        per[key] = per.get(key, 0) + 1
                for pc in (a.dump.split(",") if a.dump else ()):
                    for k, (o, p, bk) in enumerate(blocks):
                        if p == int(pc, 16):
                            end = min([q for q in offs if q > o] + [o + 512, 0x10000])
                            code = b.memr(CACHE + o, end - o)
                            hot = sorted(set(ip for ip in jips if o <= ip < end))
                            dumps.append((pc, bk, o, code, [jips.count(ip) for ip in hot], hot))
        finally:
            try:
                b.cmd("quit")
            except BaseException:
                pass
        c0, c1 = 0, a.samples * 4772.727                # printed as emulated ms below
    else:
        m = os88marty.launch(img, machine=a.machine, boot=False)
        try:
            m.run()
            os88marty.guest_sleep(m, a.lead)
            c0 = int(m.status()["cycles"])
            t0 = time.time()
            while time.time() - t0 < a.secs:
                ip = m.cmd(cmd="status")["flat_ip"]
                tot += 1
                name = charge(ip)
                hits[name] = hits.get(name, 0) + 1
            c1 = int(m.status()["cycles"])
        finally:
            m.close()
    print("%d samples over %.1f guest seconds" % (tot, (c1 - c0) / 4772727.0))
    groups = {}
    for k, n in hits.items():
        g = "core" if k.startswith("core") else ("rdgsys/host" if k.startswith("host") else "BIOS/other")
        groups[g] = groups.get(g, 0) + n
    print("  " + "   ".join("%s %.1f%%" % (g, 100.0 * n / tot) for g, n in sorted(groups.items())))
    for k, n in sorted(hits.items(), key=lambda x: -x[1])[:40]:
        print("  %5.1f%%  %s" % (100.0 * n / tot, k))
    if per:
        print("translated samples by block (PC:bank key), of all samples:")
        for k, n in sorted(per.items(), key=lambda x: -x[1])[:24]:
            print("  %5.1f%%  %s" % (100.0 * n / tot, k))
    for pc, bk, o, code, counts, hot in dumps:
        tmp = os.path.join(ROOT, "build", "realdmg", "rdgprof.dump")
        with open(tmp, "wb") as f:
            f.write(code)
        dis = subprocess.run(["ndisasm", "-b16", "-o", str(o), tmp], capture_output=True,
                             text=True).stdout
        os.unlink(tmp)
        n = dict(zip(hot, counts))
        print("block %s (key %02X) at %04X, %d bytes:" % (pc, bk, o, len(code)))
        for ln in dis.splitlines():
            ip = int(ln[:8], 16)
            print("%4s %s" % (n.get(ip, ""), ln))
    if hips:
        src = {}
        for line in open(os.path.join(ROOT, "build", "realdmg", "rdghost.asm.lst"),
                         errors="replace"):
            m = re.match(r"\s*\d+\s+([0-9A-F]{8})\s+\S+\s+(?:<\d+>\s+)?(.*)", line)
            if m and int(m.group(1), 16) not in src:
                src[int(m.group(1), 16)] = m.group(2).strip()
        cnt = {}
        for ip in hips:
            cnt[ip] = cnt.get(ip, 0) + 1
        print("the host's samples by instruction (the one AFTER the sample's IP, as"
              " a 286 reports the next):")
        for ip, n in sorted(cnt.items(), key=lambda x: -x[1])[:int(os.environ.get("RDGPROF_IPS", "60"))]:
            i = bisect.bisect_right(hk, ip - 0) - 1
            print("  %5.2f%%  %04X %-14s %s" % (100.0 * n / tot, ip,
                  hsyms[i][1] if i >= 0 else "?", src.get(ip, "")[:60]))
    if pcs:
        print("the core's samples by Game Boy PC (SI, 16-byte buckets):")
        for k, n in sorted(pcs.items(), key=lambda x: -x[1])[:24]:
            print("  %5.1f%%  %s%04X" % (100.0 * n / tot, "%02X:" % (k >> 16) if k >> 16 else "",
                                           k & 0xFFFF))


if __name__ == "__main__":
    main()
