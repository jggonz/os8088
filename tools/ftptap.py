#!/usr/bin/env python3
"""FTPD over a TAP link to the HOST's own TCP stack (SPEC.md 72.4.3).

An INSTRUMENT, not a suite row: it needs root, /dev/net/tun, iproute2,
dnsmasq and (for --loss) iptables, and it times things.

slirp - tests/ftpd.py's peer - ACKs every segment at once, so a stop-and-wait
sender looks as fast there as a windowed one. Linux DELAYS the ACK of a lone
segment, as every real client does, so this is the harness that sees a
download held to the peer's delayed-ACK timer. QEMU's CPU is not an 8088:
only what the PROTOCOL bounds means anything in these times, never the rate
a 286 or a 5150 would reach.

--loss P drops that fraction of the guest's TCP frames on the host side, the
loss SPEC.md 72.4 said no harness here could produce, so a retransmit path is
driven on demand and the bytes are compared.

    sudo python3 tools/ftptap.py .            # this tree
    sudo python3 tools/ftptap.py <worktree>   # another, for an A/B
    python3 tools/ftptap.py . --size 131072 --reps 3 --loss 0.03

It rebuilds and boots ftpdtest: build/ether360.img + build/ftpapps.img
          Run it with: python3 tests/ftpd.py's two images in the named tree, as
tests/ftpd.py does, and tears the TAP and dnsmasq down after.
"""
import argparse, ftplib, io, os, random, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("tree")
ap.add_argument("--size", type=int, default=131072)
ap.add_argument("--reps", type=int, default=3)
ap.add_argument("--loss", type=float, default=0.0, help="drop this fraction of the guest's TCP frames after login")
a = ap.parse_args()
os.chdir(a.tree)
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import ftpd as F, os88qemu

TAP, HOSTIP = "ostap0", "192.168.77.1"
def sh(c, check=True):
    return subprocess.run(c, shell=True, check=check, capture_output=True, text=True)

sh("ip link del %s" % TAP, check=False)
sh("ip tuntap add dev %s mode tap" % TAP)
sh("ip addr add %s/24 dev %s" % (HOSTIP, TAP))
sh("ip link set %s up" % TAP)
sh("pkill -f '[d]nsmasq.*ostap0'", check=False)
dns = subprocess.Popen(["dnsmasq", "--no-daemon", "--interface=" + TAP,
                        "--bind-interfaces", "--except-interface=lo",
                        "--dhcp-range=192.168.77.10,192.168.77.50,12h",
                        "--port=0", "--dhcp-option=3," + HOSTIP],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for f in (F.SYSIMG, F.APPIMG, "build/qmp.sock", "build/qemu.pid"):
    if os.path.exists(f): os.remove(f)
r = sh("make ftpdtest", check=False)
if r.returncode: sys.exit(r.stdout + r.stderr)
os88qemu.own()
dev = ("-netdev tap,id=n0,ifname=%s,script=no,downscript=no "
       "-device ne2k_isa,netdev=n0,iobase=0x300,irq=3" % TAP)
r = subprocess.run(["make", "test", "ETHER=1", "TESTIMG=" + F.SYSIMG,
                    "TESTAPPS=" + F.APPIMG, "ETHERDEV=" + dev],
                   capture_output=True, text=True)
if r.returncode: sys.exit(r.stdout + r.stderr)
m = F.Qemu(F.SOCK); mo = F.eth.Mouse()
try:
    ip = F.wait_dhcp(m)
    print("guest", ip)
    F.launch(m, mo)
    f = ftplib.FTP(); f.connect(ip, 21, timeout=60); f.login("anonymous", "x")
    if a.loss:
        sh("iptables -I INPUT -i %s -p tcp -m statistic --mode random --probability %g -j DROP" % (TAP, a.loss))
        print("dropping %g of the guest's TCP frames" % a.loss)
    random.seed(1)
    blob = bytes(random.getrandbits(8) for _ in range(a.size))
    t = time.time(); f.storbinary("STOR TAP.DAT", io.BytesIO(blob)); ts = time.time() - t
    print("STOR %d bytes %.2fs %.1f KB/s" % (a.size, ts, a.size / ts / 1024))
    for i in range(a.reps):
        buf = io.BytesIO()
        t = time.time(); f.retrbinary("RETR TAP.DAT", buf.write); tr = time.time() - t
        ok = buf.getvalue() == blob
        print("RETR %d bytes %.2fs %.1f KB/s %s" % (a.size, tr, a.size / tr / 1024,
                                                   "exact" if ok else "WRONG (%d)" % len(buf.getvalue())))
    f.quit()
finally:
    try: m.quit()
    except Exception: pass
    dns.terminate()
    if a.loss:
        sh("iptables -D INPUT -i %s -p tcp -m statistic --mode random --probability %g -j DROP" % (TAP, a.loss), check=False)
    sh("ip link del %s" % TAP, check=False)
