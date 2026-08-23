#!/usr/bin/env python3
"""SPEC.md 77.14's A/B: the optimised face against the reference one.

    make ftpdtest && python3 tests/ftpdpix.py

Drives the SAME scripted session through both builds and compares the window's
pixels. `make FTPDSLOW=1` is the reference - every change redraws the whole
content box, which is what this window did before the dirty mask and the
scrolled log.
"""
import subprocess
import sys
import time

sys.path.insert(0, 'tools')
sys.path.insert(0, 'tests')
import dispcp                                          # noqa: E402
import ethernet as eth                                 # noqa: E402
import os88sym                                         # noqa: E402
import importlib.util

spec = importlib.util.spec_from_file_location("g", "tests/ftpd.py")
G = importlib.util.module_from_spec(spec)
spec.loader.exec_module(G)

S = os88sym.linear
SOCK = "build/qmp.sock"


def build(slow):
    for f in ("build/ftpapps.img", "build/ftpd.o88", "build/ftpd.bin"):
        subprocess.run(["rm", "-f", f], check=True)
    cmd = ["make", "ftpdtest"] + (["FTPDSLOW=1"] if slow else [])
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit("build failed:\n" + r.stdout + r.stderr)


def boot():
    import os
    if os.path.exists("build/qemu.pid"):
        try:
            os.kill(int(open("build/qemu.pid").read().strip()), 15)
            time.sleep(1.0)
        except (OSError, ValueError):
            pass
    for f in ("build/qmp.sock", "build/qemu.pid"):
        if os.path.exists(f):
            os.remove(f)
    r = subprocess.run(["make", "test", "ETHER=1", "ETHFWD=1",
                        "TESTIMG=build/ether360.img",
                        "TESTAPPS=build/ftpapps.img"],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("boot failed:\n" + r.stdout + r.stderr)
    return G.Qemu(SOCK), eth.Mouse()


def session(tag):
    """The same steps both times, ending on a settled window."""
    m, mo = boot()
    G.wait_dhcp(m)
    G.launch(m, mo)
    import ftplib
    import io
    f = ftplib.FTP()
    f.encoding = "latin-1"
    f.connect("127.0.0.1", 2121, timeout=30)
    f.login("os8088", "os8088")
    # PASSIVE, so the log is DETERMINISTIC: active mode logs
    # `PORT 127,0,0,1,<p1>,<p2>` and the client's ephemeral port
    # differs between two runs - 171 pixels of it, which is session
    # data and not a rendering difference.
    rows = []
    f.retrlines("LIST", rows.append)
    buf = io.BytesIO()
    f.retrbinary("RETR FTPHELLO.TXT", buf.write)
    f.quit()
    time.sleep(3.0)
    fx, fy = G.ftp_win(m)
    mo.click(*G.ro_box(fx, fy))          # a tick: one 12px box
    time.sleep(2.0)
    # RAW PIXELS, cropped to the WINDOW - a PNG's bytes differ for reasons
    # that are not pixels, and the menu bar carries a clock that moves between
    # two runs minutes apart.
    out = "/tmp/%s.ppm" % tag
    subprocess.run(["python3", "tools/qmp.py", SOCK,
                    'screendump %s' % out], check=True, capture_output=True)
    rect = G.ftp_win(m)
    m.quit()
    time.sleep(1.0)
    return crop(out, rect)


def crop(path, xy):
    """The window's content box out of a P6 NetPBM - no Pillow in here."""
    b = open(path, "rb").read()
    assert b[:2] == b"P6", b[:2]
    i, fields = 2, []
    while len(fields) < 3:
        while b[i:i + 1].isspace():
            i += 1
        if b[i:i + 1] == b"#":
            while b[i:i + 1] != b"\n":
                i += 1
            continue
        j = i
        while not b[j:j + 1].isspace():
            j += 1
        fields.append(int(b[i:j]))
        i = j
    i += 1
    w, h, _ = fields
    x0, y0 = xy
    # the FRAME, generously - the content plus its border, well inside 400x176
    cw, ch = 400, 176
    out = []
    for y in range(y0, min(y0 + ch, h)):
        row = b[i + (y * w + x0) * 3: i + (y * w + x0 + cw) * 3]
        out.append(row)
    return b"".join(out)


build(True)
a = session("ref")
build(False)
b = session("opt")
n = sum(1 for x, y in zip(a, b) if x != y)
print("window pixels compared: %d bytes" % len(a))
print("DIFFERING BYTES: %d" % n)
same = n == 0 and len(a) == len(b)
print("IDENTICAL" if same else "*** THEY DIFFER ***")
# An A/B gate that prints its verdict and exits 0 either way is a gate that
# cannot fail - which is how it shipped. The exit code IS the assertion.
sys.exit(0 if same else 1)
