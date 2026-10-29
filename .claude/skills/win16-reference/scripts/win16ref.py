#!/usr/bin/env python3
"""Recreate the WIN16 reference machines (SPEC.md 124): MS-DOS 6.22 with a
from-scratch Windows 3.0 or Windows 3.1 install, under QEMU, from archive.org.

Everything lands in <repo>/win16ref/ (git-ignored). Nothing here is ever
committed: the downloads are Microsoft's and Duff Software's.

    win16ref.py fetch                 download + verify every input, extract samples
    win16ref.py install30             fresh DOS disk + Windows 3.0 (720K set) + WinMine 2.9
    win16ref.py install31             fresh DOS disk + Windows 3.1 (3.10.103, 1.44MB set)
    win16ref.py all                   the three above, in order
    win16ref.py run w30|w31 [CMD]     boot an installed disk; CMD is typed at C:\\>
                                      (e.g. "win /r winmine" on w30, "win /s calc" on w31)
    win16ref.py shot NAME [X,Y,W,H] [ZOOM]   screenshot the running machine to ref/NAME.png
    win16ref.py key KEY...            QEMU sendkey names (ret, alt-g, esc, equal, ...)
    win16ref.py type TEXT             type text; \\n is Enter
    win16ref.py quit                  stop the running machine

Each install step screenshots into <root>/log/<install>/NN-<step>.png. LOOK at
them: the key sequence is the one that worked on 2026-10-28, with fixed waits,
and nothing reads the screen. A diverged step is finished by hand with
key/type/shot and the install checked with mtools (the script's final check).
"""
import hashlib, json, os, shutil, socket, subprocess, sys, time, zipfile

REPO = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                      text=True, cwd=os.path.dirname(os.path.abspath(__file__))).stdout.strip()
ROOT = os.environ.get("WIN16REF", os.path.join(REPO, "win16ref"))
DL = os.path.join(ROOT, "downloads")
IA = "https://archive.org/download/"
MSW = "https://minesweepergame.com/database/download/version/"

# (dest under downloads/, url, sha256)
FETCH = [
    ("dos622/Disk1.img", IA + "disk-1_202602/Disk1.img",
     "b88030401122d234ea6aafba3cfed7de2b7b1782700a67be5498edca6f9fec5d"),
    ("dos622/Disk2.img", IA + "disk-1_202602/Disk2.img",
     "e1d48a415495a17d65316d5328a91d7df0910fb1e42b0b07e7dbf8a4b4df305a"),
    ("dos622/Disk3.img", IA + "disk-1_202602/Disk3.img",
     "52a3b4e7f5973c38f2517dbb20426bf5c8bd62f202e84a85d3d7283401d5e63c"),
    ("w30-720k.zip", IA + "microsoft-windows-3.0-3.5-720-k/Microsoft%20Windows%203.0%20(3.5%20720K).zip",
     "5a4d27c0afec21f4d519bead5159f0e352cc4adc3dbf097e87af553a8b5ecac2"),
    ("w31-103-35.zip", IA + "windows-3.1-disks/Microsoft%20Windows%203.1%20%283.10.103%29%20%283.5%29.zip",
     "af4ecad70b94363176222d15c0338ca070ba6562c0b96cb19aaab16919cc4f23"),
    ("W3.zip", IA + "w-3_20231227/W3.zip",
     "1a56afbb36dd57a57784d65b567fb8c7de4b0b753a334e7f2a92dd53c6baf612"),
    ("WinMine-2.6.zip", MSW + "82/WinMine-2.6.zip",
     "b696222753b53e2c885c9f9b06ec320587d023668b3d6b2cd374e02821bd540c"),
    ("WinMine-2.8.zip", MSW + "83/WinMine-2.8.zip",
     "1c319a155e9fca279debdf7b69c09d1a7ee5ec4e502335c1f89f370d0c404a07"),
    ("WinMine-2.9.zip", MSW + "84/WinMine-2.9.zip",
     "781ec49ef081bc0f7538b2aa94fcfda88e7020e45b715dfb88fd7e65c2f731c1"),
]
# minesweepergame.com answers 403 without these, and its links are relative
# to /database/ (the bare /download/... path is the 403 too).
HDRS = ["-A", "Mozilla/5.0", "-e", "https://minesweepergame.com/database/games.php?id=1"]

CYL, HEADS, SPT = 65, 16, 63          # 33.5 MB; QEMU's own CHS for this size
SOCK = os.path.join(ROOT, "q.sock")   # keep ROOT short: AF_UNIX paths die past ~104 bytes


def sh(*a, **k):
    return subprocess.run(list(a), check=True, **k)


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def fetch():
    os.makedirs(DL, exist_ok=True)
    for dest, url, want in FETCH:
        p = os.path.join(DL, dest)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        if not (os.path.exists(p) and sha(p) == want):
            print("fetch", dest)
            sh("curl", "-sSL", "--fail", *HDRS, "-o", p, url)
        got = sha(p)
        if got != want:
            sys.exit(f"{dest}: sha256 {got} != pinned {want}")
    # Disk images. unzip(1), not zipfile: the WinMine zips are IMPLODED (method
    # 6), which Python's zipfile cannot read.
    for z, d in (("w30-720k.zip", "w30disks"), ("w31-103-35.zip", "w31disks")):
        sh("unzip", "-q", "-o", "-j", os.path.join(DL, z), "-d", os.path.join(DL, d))
    s = os.path.join(ROOT, "samples")
    os.makedirs(s, exist_ok=True)       # unzip -d makes ONE level, not its parent
    for v in ("2.6", "2.8", "2.9"):
        sh("unzip", "-q", "-o", os.path.join(DL, f"WinMine-{v}.zip"), "-d", os.path.join(s, f"winmine-{v}"))
    sh("unzip", "-q", "-o", os.path.join(DL, "W3.zip"), "-d", os.path.join(s, "w3-archive"))
    print("fetch: all inputs verified;", ROOT)


# --- QEMU -------------------------------------------------------------------

def qemu(boot, floppy, disk):
    if os.path.exists(SOCK):
        os.unlink(SOCK)
    sh("qemu-system-i386", "-M", "pc", "-m", "8", "-cpu", "486",
       "-drive", f"file={floppy},format=raw,if=floppy,index=0",
       "-drive", f"file={disk},format=raw,if=ide,index=0",
       "-boot", boot, "-vga", "std", "-display", "none",
       "-qmp", f"unix:{SOCK},server,nowait",
       "-pidfile", os.path.join(ROOT, "q.pid"), "-daemonize")


class Q:
    """Raw QMP. Unlike tools/qmp.py it does NOT wait for a key to reach the
    BIOS buffer: Windows' own keyboard driver bypasses that buffer, so
    tools/qmp.py stalls 10 s a key under Windows and prints a false warning.

    EVERY CALL CONNECTS AND DISCONNECTS. QMP serves one client at a time and
    leaves the next in the listen backlog for ever, so a connection held
    across a tools/shot.py screenshot deadlocks the install (it did, on this
    script's second unattended run)."""

    def _session(self, lines):
        s = socket.socket(socket.AF_UNIX)
        s.connect(SOCK)
        f = s.makefile("rw")
        f.readline()
        out = []
        try:
            for obj in [{"execute": "qmp_capabilities"}] + [
                    {"execute": "human-monitor-command", "arguments": {"command-line": l}} for l in lines]:
                f.write(json.dumps(obj) + "\n"); f.flush()
                while True:
                    m = json.loads(f.readline())
                    if "return" in m or "error" in m:
                        out.append(m.get("return", "")); break
                if obj.get("execute") != "qmp_capabilities" and obj["arguments"]["command-line"].startswith("sendkey"):
                    time.sleep(0.12)
        finally:
            s.close()
        return out[1:]

    def hmp(self, line):
        return self._session([line])[0]

    def key(self, *ks):
        self._session(["sendkey " + k for k in ks])

    NAMES = {" ": "spc", "/": "slash", "\\": "backslash", ".": "dot", ":": "shift-semicolon",
             "\n": "ret", "-": "minus", "=": "equal", "*": "shift-8", "+": "shift-equal"}

    def type(self, text):
        self.key(*[self.NAMES.get(ch, ch.lower()) for ch in text.replace("\\n", "\n")])

    def disk(self, img):
        self.hmp(f"change floppy0 {img} raw")

    def close(self):
        pass


def shot(name, crop=None, zoom=None, d=None):
    d = d or os.path.join(ROOT, "ref")
    os.makedirs(d, exist_ok=True)
    out = os.path.join(d, name + ".png")
    a = ["python3", os.path.join(REPO, "tools/shot.py"), SOCK, out]
    if crop:
        a += ["--crop", crop]
    if zoom:
        a += ["--zoom", str(zoom)]
    sh(*a, stdout=subprocess.DEVNULL)
    return out


def quit_qemu():
    if os.path.exists(SOCK):
        try:
            q = Q(); q.hmp("quit"); q.close()
        except OSError:
            pass
    time.sleep(2)


def wait(s):
    time.sleep(s)


# --- disks ------------------------------------------------------------------

def mtools_env(img):
    rc = os.path.join(ROOT, "mtoolsrc")
    with open(rc, "w") as f:
        f.write(f'drive c: file="{img}" partition=1 cylinders={CYL} heads={HEADS} sectors={SPT} mformat_only\n'
                "mtools_skip_check=1\n")
    return dict(os.environ, MTOOLSRC=rc, MTOOLS_SKIP_CHECK="1")


def dos_disk(img):
    """A blank 33.5 MB disk made bootable MS-DOS 6.22: partitioned and
    formatted by mtools, then FDISK /MBR + SYS C: from a COPY of DOS disk 1
    (its AUTOEXEC replaced), so no DOS SETUP and no keystrokes."""
    with open(img, "wb") as f:
        f.truncate(CYL * HEADS * SPT * 512)
    env = mtools_env(img)
    subprocess.run(["mpartition", "-I", "c:"], env=env, capture_output=True)
    sh("mpartition", "-c", "-t", str(CYL), "-h", str(HEADS), "-s", str(SPT), "-b", str(SPT), "c:", env=env)
    sh("mpartition", "-a", "c:", env=env)          # active: the MBR code boots it
    sh("mformat", "-v", "DOS622", "c:", env=env)
    boot = os.path.join(ROOT, "dosboot.img")
    shutil.copy(os.path.join(DL, "dos622/Disk1.img"), boot)
    put(boot, "AUTOEXEC.BAT", "FDISK /MBR\r\nSYS C:\r\n", floppy=True)
    put(boot, "CONFIG.SYS", "FILES=30\r\nBUFFERS=20\r\n", floppy=True)
    qemu("a", boot, img); wait(20); quit_qemu()
    put(img, "CONFIG.SYS", "FILES=30\r\nBUFFERS=20\r\n")
    put(img, "AUTOEXEC.BAT", "@ECHO OFF\r\nPROMPT $P$G\r\nPATH C:\\DOS;C:\\\r\n")
    out = subprocess.run(["mdir", "-a", "c:"], env=env, capture_output=True, text=True).stdout
    if "COMMAND" not in out or "IO       SYS" not in out:
        sys.exit("dos_disk: SYS C: did not land:\n" + out)


def put(img, name, text, floppy=False):
    tmp = os.path.join(ROOT, name)
    with open(tmp, "w", newline="") as f:
        f.write(text)
    if floppy:
        sh("mcopy", "-o", "-i", img, tmp, "::", env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
    else:
        sh("mcopy", "-o", tmp, "c:/", env=mtools_env(img))
    os.unlink(tmp)


def check_install(img, files):
    env = mtools_env(img)
    out = subprocess.run(["mdir", "-b", "c:/WINDOWS"], env=env, capture_output=True, text=True).stdout.upper()
    miss = [f for f in files if f not in out]
    if miss:
        sys.exit(f"{img}: missing after install: {miss} - look at the log screenshots")
    print("installed:", img)


# --- installs ---------------------------------------------------------------

def install30():
    d = os.path.join(ROOT, "w30"); log = os.path.join(ROOT, "log/w30")
    os.makedirs(d, exist_ok=True); os.makedirs(log, exist_ok=True)
    img = os.path.join(d, "c.img"); disks = os.path.join(DL, "w30disks")
    dos_disk(img)
    qemu("c", os.path.join(disks, "DISK01.IMG"), img); wait(10)
    q = Q(); n = [0]

    def step(name, w, *keys):
        q.key(*keys); wait(w); n[0] += 1; shot(f"{n[0]:02d}-{name}", d=log)

    q.type("a:\n"); wait(1); q.type("setup\n"); wait(10); shot("00-welcome", d=log)
    step("directory", 4, "ret")                  # Welcome -> C:\WINDOWS
    step("hardware", 5, "ret")                   # -> detected hardware list
    step("disk2-prompt", 20, "ret")              # accept; copies, asks for disk 2
    q.disk(os.path.join(disks, "DISK02.IMG"))
    step("options", 25, "ret")                   # graphical: printers/apps/docs
    step("options-off", 3, "alt-p", "alt-a", "alt-d")
    step("disk3-prompt", 20, "ret")
    for k in range(3, 8):
        q.disk(os.path.join(disks, f"DISK0{k}.IMG"))
        step(f"disk{k}", 25, "ret")              # the last ret lands on the
    step("saved-ok", 8, "ret")                   # CONFIG.SYS dialog; then OK
    step("to-dos", 6, "alt-d")                   # Exit Setup -> Return to DOS
    q.hmp("eject floppy0"); q.close(); quit_qemu()
    check_install(img, ["WIN.COM", "CALC.EXE", "PROGMAN.EXE", "SYSTEM.INI"])
    shutil.copy(img, os.path.join(d, "c-pristine.img"))
    env = mtools_env(img)
    w = os.path.join(ROOT, "samples/winmine-2.9")
    sh("mcopy", "-o", os.path.join(w, "WINMINE.EXE"), os.path.join(w, "WINMINE.HLP"), "c:/WINDOWS/", env=env)


def install31():
    d = os.path.join(ROOT, "w31"); log = os.path.join(ROOT, "log/w31")
    os.makedirs(d, exist_ok=True); os.makedirs(log, exist_ok=True)
    img = os.path.join(d, "c.img"); disks = os.path.join(DL, "w31disks")
    dos_disk(img)
    qemu("c", os.path.join(disks, "Disk01.img"), img); wait(10)
    q = Q(); n = [0]

    def step(name, w, *keys):
        q.key(*keys); wait(w); n[0] += 1; shot(f"{n[0]:02d}-{name}", d=log)

    q.type("a:\n"); wait(1); q.type("setup\n"); wait(12); shot("00-welcome", d=log)
    step("express-or-custom", 4, "ret")
    step("disk2-prompt", 25, "ret")              # Express Setup
    q.disk(os.path.join(disks, "Disk02.img"))
    step("name", 30, "ret")
    # As recorded: name + Enter, then it took TWO more Enters (the verify
    # dialog's Continue among them) before Setup asked for disk 3.
    q.type("os8088\n"); wait(4); shot("03-typed", d=log)
    step("verify", 30, "ret")
    step("disk3-prompt", 40, "ret")
    for k in range(3, 7):
        q.disk(os.path.join(disks, f"Disk0{k}.img"))
        step(f"disk{k}", 45, "ret")              # after disk 6: printer list
    step("tutorial", 30, "ret")                  # "No Printer Attached"
    step("exit-setup", 8, "alt-s")               # Skip Tutorial
    step("to-dos", 6, "alt-d")                   # Return to MS-DOS
    q.hmp("eject floppy0"); q.close(); quit_qemu()
    check_install(img, ["WIN.COM", "WINMINE.EXE", "CALC.EXE", "SYSTEM.INI"])
    shutil.copy(img, os.path.join(d, "c-pristine.img"))
    env = mtools_env(img)
    out = os.path.join(ROOT, "samples/w31")
    os.makedirs(out, exist_ok=True)
    sh("mcopy", "-o", "c:/WINDOWS/WINMINE.EXE", "c:/WINDOWS/CALC.EXE", out, env=env)


def run(which, cmd=None):
    img = os.path.join(ROOT, which, "c.img")
    boot = os.path.join(ROOT, "dosboot.img")
    qemu("c", boot, img)
    q = Q(); q.hmp("eject floppy0"); wait(14)
    if cmd:
        q.type(cmd + "\n")
    q.close()
    print("running", which, "- QMP at", SOCK)


def main(a):
    sys.stdout.reconfigure(line_buffering=True)   # progress reaches a log as it happens
    if not a:
        sys.exit(__doc__)
    os.makedirs(ROOT, exist_ok=True)
    c = a[0]
    if c == "fetch": fetch()
    elif c == "install30": install30()
    elif c == "install31": install31()
    elif c == "all": fetch(); install30(); install31()
    elif c == "run": run(a[1], a[2] if len(a) > 2 else None)
    elif c == "shot": print(shot(a[1], a[2] if len(a) > 2 else None, a[3] if len(a) > 3 else None))
    elif c == "key": q = Q(); q.key(*a[1:]); q.close()
    elif c == "type": q = Q(); q.type(" ".join(a[1:])); q.close()
    elif c == "quit": quit_qemu()
    else: sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
