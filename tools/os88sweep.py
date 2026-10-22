#!/usr/bin/env python3
"""os88sweep - boot every package on the apps disk once, and screenshot it.

    python3 tools/os88sweep.py vesa out/        # on build/vesa.img (1024x768x16)
    python3 tools/os88sweep.py vga out/         # on build/os8088.img (the VGA desktop)
    python3 tools/os88sweep.py vesa out/ GAMES/GORILLAS APPS/PAINT   # a few

One QEMU boot a package (four at a time), each on a system disk of its own
that carries one \\AUTOSTRT link to the package on build/apps.img, so the
package is up with nothing driven; a screendump at +40 s, Escape, another
at +4 s. The PNGs land in the output directory as <sys>-<pkg>-1.png and
-2.png, with a contact sheet beside them, and the log says each dump's size
and how much of it is not white.

It is a DIAGNOSTIC and not a gate: nothing here asserts. It exists because
SPEC.md 120's rows passed while nine packages opened at the wrong size on
the packed display, and a sheet of thirty-six screens beside the same
thirty-six on VGA showed every one of them in an hour (docs/TESTING.md,
"The first look found what the rows had not asked"). QEMU, not an 8088.
"""
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
JOBS = 4


def packages():
    """Every .O88 in APPS/, GAMES/ and SYSTEM/ of build/apps.img, as FOLDER/NAME."""
    out = []
    for folder in ("APPS", "GAMES", "SYSTEM"):
        r = subprocess.run(["mdir", "-i", os.path.join(ROOT, "build", "apps.img"),
                            "::" + folder], capture_output=True, text=True)
        for line in r.stdout.splitlines():
            m = re.match(r"^(\w+)\s+O88\s", line)
            if m:
                out.append("%s/%s" % (folder, m.group(1)))
    return out


def disk_cmd(sysimg, out, link):
    """tools/os88disk.py's line for the system disk, as `make -n` prints it,
    with the output and (for the VGA desk) the VESA settings file changed."""
    target = "build/vesa.img" if sysimg == "vesa" else "build/os8088.img"
    r = subprocess.run(["make", "-n", "-B", target], cwd=ROOT, capture_output=True, text=True)
    lines = r.stdout.replace("\\\n", " ").splitlines()
    cmd = next(l for l in lines if "os88disk.py -o " + target in l)
    cmd = cmd.replace("-o " + target, "-o " + out)
    # WITH THE OTHER POSITIONALS, which start after --kernel: argparse takes
    # positionals in one run, so a link appended after os8088.img's trailing
    # --dir-slots is swallowed by it and one slipped in after -o, ahead of
    # the run, is "unrecognized"
    return re.sub(r"(--kernel \S+)", r"\1 AUTOSTRT:" + link, cmd, count=1)


def one(args):
    i, sysimg, outdir, pkg = args
    folder, name = pkg.split("/")
    tag = "%s-%s" % (sysimg, name.lower())
    work = os.path.join(outdir, ".work", tag)
    os.makedirs(work, exist_ok=True)
    link = os.path.join(work, name + ".LNK")
    subprocess.run(["python3", "tools/os88lnk.py", link, "%s\\%s.O88" % (folder, name),
                    "--vol", "B"], cwd=ROOT, check=True, capture_output=True)
    disk = os.path.join(work, "sys.img")
    subprocess.run(disk_cmd(sysimg, disk, link), shell=True, cwd=ROOT, check=True,
                   capture_output=True)
    apps = os.path.join(work, "apps.img")
    shutil.copy(os.path.join(ROOT, "build", "apps.img"), apps)
    sock = os.path.join(ROOT, "build", "sweep%d.sock" % i)
    pid = os.path.join(ROOT, "build", "sweep%d.pid" % i)
    subprocess.run("qemu-system-i386 -machine pc,vmport=off -vga std"
                   " -drive file=%s,format=raw,if=floppy"
                   " -drive file=%s,format=raw,if=floppy,index=1 -boot a"
                   " -chardev msmouse,id=m0 -serial chardev:m0 -display none"
                   " -qmp unix:%s,server,nowait -daemonize -pidfile %s"
                   % (disk, apps, sock, pid), shell=True, cwd=ROOT, check=True,
                   capture_output=True)

    def dump(suffix):
        r = subprocess.run(["python3", "tools/shot.py", sock,
                            os.path.join(outdir, tag + suffix + ".png")],
                           cwd=ROOT, capture_output=True, text=True)
        return r.stdout.strip().split(": ")[-1]
    try:
        time.sleep(40)
        r1 = dump("-1")
        subprocess.run(["python3", "tools/qmp.py", sock, "sendkey esc"], cwd=ROOT,
                       capture_output=True)
        time.sleep(4)
        r2 = dump("-2")
    finally:
        subprocess.run(["python3", "tools/qmp.py", sock, "quit"], cwd=ROOT,
                       capture_output=True)
        time.sleep(1)
        try:
            os.kill(int(open(pid).read()), 9)
        except Exception:
            pass
    shutil.rmtree(work, ignore_errors=True)
    return "%-22s %-28s | esc: %s" % (tag, r1, r2)


def sheet(sysimg, outdir):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    files = sorted(f for f in os.listdir(outdir) if f.startswith(sysimg + "-") and f.endswith("-1.png"))
    cols, w, h = 6, 320, 240
    img = Image.new("RGB", (cols * w, ((len(files) + cols - 1) // cols) * (h + 14)), "white")
    d = ImageDraw.Draw(img)
    for i, f in enumerate(files):
        im = Image.open(os.path.join(outdir, f)).convert("RGB")
        sz = im.size
        x, y = (i % cols) * w, (i // cols) * (h + 14)
        img.paste(im.resize((w, h)), (x, y + 14))
        d.text((x + 4, y + 1), "%s %dx%d" % (f, sz[0], sz[1]), fill="black")
    img.save(os.path.join(outdir, sysimg + "-sheet.png"))


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("vesa", "vga"):
        print(__doc__)
        return 2
    sysimg, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    pkgs = sys.argv[3:] or packages()
    with ThreadPoolExecutor(JOBS) as ex:
        for line in ex.map(one, [(i, sysimg, outdir, p) for i, p in enumerate(pkgs)]):
            print(line, flush=True)
    sheet(sysimg, outdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
