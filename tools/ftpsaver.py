#!/usr/bin/env python3
"""FTPD under the screen saver, on QEMU + TAP (SPEC.md 79.6.2, 79.10).

An INSTRUMENT beside tools/ftptap.py and needing what it needs - root,
/dev/net/tun, iproute2, dnsmasq - plus QEMU's gdb stub, because QMP can read
guest memory and cannot write it, and both arms have to shorten the idle
period to a second.

  A  as shipped: FTPD serving has set WF_QUIET, so the session must BLANK
     ([blk_on] 1, [blk_sv] 0), and a RETR under it must arm no widget
  B  WF_QUIET cleared by hand: the session ANIMATES ([blk_sv] 1), and a
     RETR under it must still never arm the file-progress widget

[fpg_on] is sampled every 50 ms, which proves nothing on its own - so the
negative control is the measurement: with fpg_arm's [blk_on] test jumped
over, BOTH arms read `fpg_on ever 1`.

    sudo python3 tools/ftpsaver.py
"""
import ftplib, io, os, random, socket, subprocess, sys, threading, time
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import ftpd as F, os88qemu
S = F.S
TAP, HOSTIP = "ostap0", "192.168.77.1"
def sh(c, check=True): return subprocess.run(c, shell=True, check=check, capture_output=True, text=True)

class Gdb:
    def __init__(s, port=1234):
        s.k = socket.create_connection(("127.0.0.1", port)); s.k.settimeout(5)
    def pkt(s, body):
        cs = sum(body.encode()) & 0xFF
        s.k.sendall(("$%s#%02x" % (body, cs)).encode())
        buf = b""
        while True:
            buf += s.k.recv(4096)
            i = buf.find(b"#", buf.find(b"$"))
            if buf.find(b"$") >= 0 and i >= 0 and len(buf) >= i + 3:
                break
        s.k.sendall(b"+")
        return buf
    def write(s, addr, data):
        return s.pkt("M%x,%x:%s" % (addr, len(data), data.hex()))
    def detach(s):
        try: s.pkt("D")
        except Exception: pass
        s.k.close()

def poke(addr, data):
    g = Gdb(); g.write(addr, data); g.detach()

sh("ip link del %s" % TAP, check=False)
sh("ip tuntap add dev %s mode tap" % TAP); sh("ip addr add %s/24 dev %s" % (HOSTIP, TAP)); sh("ip link set %s up" % TAP)
dns = subprocess.Popen(["dnsmasq", "--no-daemon", "--interface=" + TAP, "--bind-interfaces", "--except-interface=lo",
                        "--dhcp-range=192.168.77.10,192.168.77.50,12h", "--port=0"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for f in (F.SYSIMG, F.APPIMG, "build/qmp.sock", "build/qemu.pid"):
    if os.path.exists(f): os.remove(f)
r = sh("make ftpdtest", check=False)
if r.returncode: sys.exit(r.stdout + r.stderr)
os88qemu.own()
dev = ("-netdev tap,id=n0,ifname=%s,script=no,downscript=no -device ne2k_isa,netdev=n0,iobase=0x300,irq=3 "
       "-gdb tcp::1234" % TAP)
r = subprocess.run(["make", "test", "ETHER=1", "TESTIMG=" + F.SYSIMG, "TESTAPPS=" + F.APPIMG, "ETHERDEV=" + dev],
                   capture_output=True, text=True)
if r.returncode: sys.exit(r.stdout + r.stderr)
m = F.Qemu(F.SOCK); mo = F.eth.Mouse()
fails = []
def b(name): return m.read(S(name), 1)[0]
def w(name): return int.from_bytes(m.read(S(name), 2), "little")
def quiet_recs():
    out = []
    for i in range(12):
        a = S("wm_wins") + i * 72
        fl = int.from_bytes(m.read(a, 2), "little")
        if fl & 0x1000: out.append((a, fl))
    return out

def retr_watch(f, name, blob, label):
    seen = {"fpg": 0, "blk_on": set(), "blk_sv": set()}
    stop = threading.Event()
    def watch():
        while not stop.is_set():
            try:
                seen["fpg"] |= b("fpg_on")
                seen["blk_on"].add(b("blk_on")); seen["blk_sv"].add(b("blk_sv"))
            except Exception: pass
            time.sleep(0.05)
    th = threading.Thread(target=watch); th.start()
    buf = io.BytesIO(); t = time.time()
    f.retrbinary("RETR " + name, buf.write); dt = time.time() - t
    stop.set(); th.join()
    ok = buf.getvalue() == blob
    print("%s: RETR %d bytes %.2fs %.0f KB/s %s; during it blk_on %s blk_sv %s fpg_on ever %d"
          % (label, len(blob), dt, len(blob) / dt / 1024, "exact" if ok else "WRONG",
             sorted(seen["blk_on"]), sorted(seen["blk_sv"]), seen["fpg"]))
    if not ok: fails.append(label + ": bytes wrong")
    return seen

def wait_for(pred, secs, what):
    end = time.time() + secs
    while time.time() < end:
        if pred(): return True
        time.sleep(0.25)
    fails.append("timed out waiting for " + what); return False

try:
    ip = F.wait_dhcp(m); print("guest", ip)
    F.launch(m, mo)
    q = quiet_recs()
    print("windows with WF_QUIET:", [("%05X" % a, "%04X" % fl) for a, fl in q])
    if len(q) != 1: fails.append("expected exactly FTPD's window to carry WF_QUIET, got %d" % len(q))
    f = ftplib.FTP(); f.connect(ip, 21, timeout=60); f.login("anonymous", "x")
    random.seed(2); blob = bytes(random.getrandbits(8) for _ in range(196608))
    f.storbinary("STOR SAV.DAT", io.BytesIO(blob))
    # --- A: as shipped. A 1-second idle period, then a RETR
    poke(S("ss_idle"), (18).to_bytes(2, "little"))
    wait_for(lambda: b("blk_on") == 1, 15, "a session (A)")
    print("A: blk_on %d blk_sv %d (want 1, 0: BLANKED)" % (b("blk_on"), b("blk_sv")))
    if b("blk_sv") != 0: fails.append("A: FTPD serving and the saver ANIMATED")
    sA = retr_watch(f, "SAV.DAT", blob, "A blank")
    if sA["fpg"]: fails.append("A: the file widget armed under a blanked screen")
    # --- B: clear the bit, wake, let an ANIMATED session start, RETR under it
    a, fl = q[0]
    poke(a, (fl & ~0x1000).to_bytes(2, "little"))
    mo.run("to", "120", "120")
    wait_for(lambda: b("blk_on") == 0, 10, "the wake")
    poke(S("ss_idle"), (18).to_bytes(2, "little"))
    wait_for(lambda: b("blk_sv") == 1, 20, "an animated session (B)")
    print("B: blk_on %d blk_sv %d (want 1, 1: ANIMATING)" % (b("blk_on"), b("blk_sv")))
    sB = retr_watch(f, "SAV.DAT", blob, "B animated")
    subprocess.run(["python3", "tools/shot.py", F.SOCK, "build/ftpsaver-b.png"])
    if sB["fpg"]: fails.append("B: the file widget armed over the animation")
    if 1 not in sB["blk_sv"]: fails.append("B: no animated session during the RETR")
    f.quit()
finally:
    try: m.quit()
    except Exception: pass
    dns.terminate(); sh("ip link del %s" % TAP, check=False)
print("FAILS:" if fails else "all checks passed", *fails, sep="\n  ")
