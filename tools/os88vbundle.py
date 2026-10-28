#!/usr/bin/env python3
"""THE VIDEO ENCODER, PACKED FOR PEOPLE WITH NO os8088 TREE (SPEC.md 98.2.13).

    make vencbundle          # -> build/os8088-encoder.zip
    python3 tools/os88vbundle.py OUT.zip --player build/video.o88

One folder, `os8088-encoder/`, holding the encoder window, everything it
imports or runs, VIDEO.O88, a README.TXT and GUIDE.HTML - the user's guide,
with its screenshots in guide/. Unpacked anywhere and run with
`python3 os88vencgui.py`, it encodes, previews and makes disks: floppies
with the player on them, and - with `--boot`, which `make vencbundle`
passes - hard disks that BOOT, the kernel, the boot records and the
drivers carried in boot/ (`boot_dir`); the player beside the tool rides
every one of them (`player_path`).

THE FILE LIST IS COMPUTED, NOT WRITTEN. It starts at ROOTS - the window,
and the three tools it runs as processes rather than importing - and follows
every `import` of a module that lives in tools/. So a module the encoder
starts to need tomorrow is in tomorrow's bundle with nothing to remember,
and a third-party import is reported (THIRD_PARTY says which are expected,
and the README names them) rather than silently left for a user to find.

THE GUIDE'S PICTURES ARE COMPUTED TOO (`guide_files`): GUIDE.HTML is read
and every guide/ file it names goes in, and only those - a picture the page
names that is not there stops the build, and one left in the folder that
the page no longer names does not ship.

The zip is DETERMINISTIC, as the rest of the toolchain is: the entries in
sorted order, every timestamp 1980-01-01, the same modes - so the same tree
makes the same bytes.
"""
import argparse
import ast
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOTS = ("os88vencgui.py", "os88venc.py", "os88disk.py", "os88hdd.py")
THIRD_PARTY = {"numpy", "PIL", "tkinterdnd2"}
# ...and the DATA a module reads beside itself: the TEXT format's model face
# (tools/os88txtfont.py looks beside itself first, then in fonts/)
DATA = {"os88txtfont.py": (("tallx.f8", os.path.join("..", "fonts",
                                                      "tallx.f8")),)}
# THE USER'S GUIDE (docs/encoder-guide/): the page, and the guide/ pictures
# it names
GUIDE = os.path.join(HERE, "..", "docs", "encoder-guide")
GUIDE_PAGE = "GUIDE.HTML"
FOLDER = "os8088-encoder"
STAMP = (1980, 1, 1, 0, 0, 0)

README = """os8088 VIDEO ENCODER
====================

Turns a video file into an os8088 .V88, previews it as the target screen
shows it, and puts it on a disk.

    python3 os88vencgui.py

GUIDE.HTML, in this folder, is the full guide: open it in a web browser.
It shows what every "Made for" choice looks like, explains the budget that
every setting is about, and goes through each tab saying when a setting is
worth changing and why, with recipes for common machines.

NEEDS
  Python 3 with Tk (python.org's installer has it; on Linux the python3-tk
  package), and two Python packages:
      python3 -m pip install numpy pillow
  ffmpeg and ffprobe on the PATH (https://ffmpeg.org) - they read your
  video; nothing else here needs them.
  Optional: `python3 -m pip install tkinterdnd2` lets you drop a file on the
  window on any system (on Windows a drop works without it).
  Optional: tesseract on the PATH (https://github.com/tesseract-ocr/
  tesseract) for the TEXT format's --text-ocr and --text-ocr-large, which
  read the words in the picture and draw them as words.

WHAT IT MAKES
  1. Choose what the video is FOR (the machine), then the video.
  2. Encode. The .V88 is written beside the video.
  3. "...and make a disk of it" puts the .V88 on a disk image beside it,
     with VIDEO.O88 (the player, in this folder) on it too:
       - a 360 KB, 720 KB, 1.2 MB or 1.44 MB FLOPPY: put it in B: next to
         an os8088 system disk and double-click the video;
       - a 20 MB (ST11M), 32 MB (ST11R) or 32 MB (IDE) HARD DISK, a fixed
         VHD for 86Box or a real disk. It BOOTS os8088 - the kernel and
         drivers in boot/ go on it - and the video is in its root.

This folder is built from the os8088 tree by `make vencbundle`; the tools'
own help (`python3 os88venc.py --help`) has every option.
"""


def closure(roots=ROOTS):
    """(the tools/ modules the roots need, the third-party imports seen)"""
    std = set(sys.stdlib_module_names)
    seen, todo, other = set(), list(roots), set()
    while todo:
        f = todo.pop()
        if f in seen:
            continue
        seen.add(f)
        tree = ast.parse(open(os.path.join(HERE, f), encoding="utf-8").read())
        for n in ast.walk(tree):
            if isinstance(n, ast.Import):
                mods = [a.name for a in n.names]
            elif isinstance(n, ast.ImportFrom) and n.module and not n.level:
                mods = [n.module]
            else:
                continue
            for m in mods:
                top = m.split(".")[0]
                if os.path.exists(os.path.join(HERE, top + ".py")):
                    todo.append(top + ".py")
                elif top not in std:
                    other.add(top)
    return sorted(seen), other


def guide_files(base=GUIDE):
    """[(name in the bundle, path)]: GUIDE.HTML and every guide/ file it
    names in a src= or an href=, which must exist"""
    page = os.path.join(base, GUIDE_PAGE)
    text = open(page, encoding="utf-8").read()
    names = sorted(set(re.findall(r'(?:src|href)="(guide/[^"#?]+)"', text)))
    miss = [n for n in names if not os.path.isfile(os.path.join(base, n))]
    if miss:
        sys.exit("os88vbundle: %s names %s, which %s not there" % (
            GUIDE_PAGE, ", ".join(miss), "is" if len(miss) == 1 else "are"))
    return [(GUIDE_PAGE, page)] + [(n, os.path.join(base, n)) for n in names]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--player", required=True, help="VIDEO.O88 to ship")
    ap.add_argument("--boot", metavar="BUILD",
                    help="a build folder to take the hard disk's BOOT files "
                    "from (the window's HD_BOOT and HD_WANT: the kernel, the "
                    "boot records, HDD.DRV, CTRL/SOUND/HIBER.DRV) into "
                    "boot/, so the bundle's hard disks boot")
    a = ap.parse_args()
    mods, other = closure()
    stray = other - THIRD_PARTY
    if stray:
        sys.exit("os88vbundle: the encoder imports %s, which is neither in "
                 "tools/ nor expected (THIRD_PARTY) - add it there and to "
                 "the README, or drop the import" % ", ".join(sorted(stray)))
    files = [(m, open(os.path.join(HERE, m), "rb").read(), 0o755)
             for m in mods]
    for m in mods:
        for name, src in DATA.get(m, ()):
            files.append((name, open(os.path.join(HERE, src), "rb").read(),
                          0o644))
    files.append(("VIDEO.O88", open(a.player, "rb").read(), 0o644))
    for name, path in guide_files():     # the user's guide and its pictures
        files.append((name, open(path, "rb").read(), 0o644))
    if a.boot:                  # THE BOOT FILES, from the window's own lists
        sys.path.insert(0, HERE)  # so the two cannot drift (boot_dir)
        import os88vencgui as gui
        for _, f in gui.HD_BOOT + gui.HD_WANT:
            if f != "video.o88":            # (the player, above)
                files.append(("boot/" + f,
                              open(os.path.join(a.boot, f), "rb").read(),
                              0o644))
    files.append(("README.TXT", README.replace("\n", "\r\n").encode(),
                  0o644))
    tmp = a.out + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data, mode in sorted(files):
            zi = zipfile.ZipInfo("%s/%s" % (FOLDER, name), STAMP)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100000 | mode) << 16
            z.writestr(zi, data)
    os.replace(tmp, a.out)
    print("os88vbundle: %s - %d files: %s" % (
        a.out, len(files), " ".join(n for n, _, _ in sorted(files))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
