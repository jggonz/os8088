#!/usr/bin/env python3
"""The PROJECT GUIDE: every released disk, what is on it, and a manual for
each program - read off the images `make` built, not written by hand.

    python3 tools/os88guide.py                 # -> build/guide/index.html
    python3 tools/os88guide.py --fragment      # ...the same page without its
                                               #    <html> skeleton (claude.ai)
    python3 tools/os88guide.py --zip           # ...and build/guide.zip
    python3 tools/os88guide.py --missing       # files with no description

**ON DEMAND, like tools/os88ladder.py.** Nothing in `make` runs it, and the
page it writes goes stale the moment a disk changes. The on-demand disks
(the one-program disks, the everything set, the live media) are only listed
with a layout when they have been built; the rest say so.

WHAT IS READ AND WHAT IS WRITTEN. The disk list and the order of the groups
are the release zip's own (`.claude/skills/release-os8088/mkzip.py`, GROUPS
and ITEMS), so the guide describes exactly what a release ships. Every
layout - folders, files, sizes, free space - is walked out of the image
itself. The only hand-written text is `tools/guide/entries.toml` (one line
per file name), `tools/guide/manuals/*.md` (one manual per program) and the
TOOLS / FIRMWARE tables below. Screenshots are never committed: a manual
names a shot by id and the capture step that makes it lives beside it. The
demo videos' browser copies ARE committed (tools/guide/videos/, made by
tools/guide/vidpreview.py), because making them takes ffmpeg and minutes; a
.V88 on a disk finds its copy by the SHA-256 of its bytes.
"""

import argparse
import hashlib
import html
import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import sys
if sys.version_info < (3, 11):                 # tomllib, below (CONTRIBUTING.md)
    sys.exit("%s: needs python3 3.11 or newer, for tomllib; this is %d.%d at %s"
             % (os.path.basename(__file__), sys.version_info[0],
                sys.version_info[1], sys.executable))
import tomllib
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from os88fat import Fat12  # noqa: E402
from os88lz import cz_unwrap  # noqa: E402

GUIDE = os.path.join(ROOT, "tools", "guide")
BUILD = os.path.join(ROOT, "build")

# --- the main axis ----------------------------------------------------------
GEOS = [
    ("1440", "1.44MB", "3.5-inch high density"),
    ("1200", "1.2MB", "5.25-inch high density"),
    ("720", "720KB", "3.5-inch double density"),
    ("360", "360KB", "5.25-inch double density, the XT"),
    ("hdd", "Hard disk & CD", "USB stick, hard disk or CD"),
    ("tools", "Tools", "Programs for your modern computer"),
    ("firmware", "Firmware", "For add-in cards"),
]
SECTORS = {2880: "1440", 2400: "1200", 1440: "720", 720: "360"}

# Which release group lands in which half of a geometry's disk list. "main"
# is the disks most people need; "own" is a disk that ships one program of
# its own, or a set that repeats programs already on the main disks.
SECTION = {"start": "main", "xt": "main", "emu": "main",
           "live": "main", "all": "own", "progs": "own"}
# Every section but "main" is one expandable item at the end of the disk row:
# (its title, the line under it). The count of disks is the page's to add.
SECTION_TITLE = {"main": ["Start here", ""],
                 "own": ["One program per disk", "and the everything set"],
                 "viddemo": ["Video demo hard drives", "a whole install, with videos"]}

# The guide's own one-line description of a disk, by release item title or by
# the disk's role. The release README's sentences are written for a reader
# choosing a download; these are for a reader looking at the disk.
BLURB = {
    "System disk": "Starts os8088. Boot from it in drive A.",
    "Software disk": "The everyday programs, with sample documents. Goes in drive B.",
    "Music disk": "Songs and a tracker module for the music programs.",
    "Office disk": "Writing, spreadsheet, chart and drawing programs.",
    "Network disk": "Browser, Telnet, FTP server, The Wire and WireChat, for an Ethernet card.",
    "Games disk": "Every game on one floppy.",
    "Emulator system disk": "A system disk whose pointer needs no mouse capture in an emulator.",
    "Live USB image": "The whole system and every program, for a USB stick.",
    "Live CD": "The live USB image as a bootable CD. Changes last until power-off.",
    "Everything set": "Every program, across as many disks as it takes.",
    "Word processor": "Word, modelled on Word for Windows 1.1a.",
    "Scribe": "Scribe, a second word processor on the same document format.",
    "Word processor, C edition": "Word again, written in C.",
    "CP/M emulator": "Runs CP/M 2.2 programs in a window, with a library of them.",
    "Commodore 64": "A Commodore 64 in a window.",
    "Apple II Plus": "An Apple II Plus in a window, with Applesoft BASIC.",
    "PaccMan": "Arcade-accurate Pac-Man. Best on a 386.",
    "RealDMG": "A Game Boy in a window. Bring your own cartridges.",
    "1942": "Air combat after the arcade game. Needs VGA or CGA.",
    "REDLINE": "A benchmark that scores your PC against a 4.77 MHz IBM PC.",
    "MIDIRack": "A MIDI music player with ten songs.",
    "PiXEL": "An image viewer and editor, with sample pictures.",
    "Weave": "Weave apps, ready to run.",
    "Weave editor": "The Weave editor, with the demo apps' sources.",
    "Video demo": "A ready-installed hard disk with demo videos: CGA and "
                  "Hercules for an XT, VGA for a 286.",
}

# The video demo drives (`make viddemo`): one per adapter that has a
# apps/video/demo/<adapter>/ directory, so a new adapter appears by itself.
VIDDEMO_NAMES = {"cga": "CGA", "herc": "Hercules", "vga": "VGA", "ega": "EGA"}
# ...and the drive each is, as the Makefile's VIDDEMO_IDE says: an XT's 31 MB
# ST-238R on an ST11R, or VGA's 504 MB IDE disk for a 286
VIDDEMO_DISK = {"vga": "IDE"}

# A disk's own name, where the release item names a PAIR or a SET.
ROLE = [
    (r"^os8088(-\d+)?\.img$", "System disk"),
    (r"^apps\d*\.img$", "Software disk"),
    (r"^emu\.img$", "Emulator system disk"),
    (r"^apps-all(-120)?-(\d+)\.img$", None),     # "Everything set, disk N"
    (r"^os8088-usb\.img$", "Live USB image"),
    (r"^os8088\.iso$", "Live CD"),
]

# --- the things that are not disks -------------------------------------------
TOOLS = [
    {"id": "tool-venc", "title": "Video encoder",
     "summary": "Turns any video into a .V88 file the Video Player can play, "
                "sized and dithered for the machine it will play on.",
     "files": [("tools/os88venc.py", "The encoder, run from a terminal."),
               ("tools/os88vencgui.py", "The same encoder with a window, previews and presets.")]},
    {"id": "tool-proxy", "title": "Web proxy",
     "summary": "Runs on your modern computer so the os8088 Browser can reach "
                "today's web, including secure (https) sites.",
     "files": [("tools/os88proxy.py", "The proxy, run from a terminal."),
               ("tools/os88proxygui.py", "The same proxy with a window. Also builds the Windows .exe.")]},
    {"id": "tool-imager", "title": "Disk imager",
     "summary": "Writes any os8088 image to a real floppy, USB stick, CompactFlash "
                "card or CD on a Mac, and checks every byte it wrote.",
     "files": [("tools/os88imager.py", "The imager. `make imager` runs the same thing.")]},
    {"id": "tool-burn", "title": "Media burner",
     "summary": "A guided way to write the live USB image to a flash drive, "
                "or burn the live CD, on a Mac.",
     "files": [("tools/os88burn.py", "The interactive guide. `make burn` runs the same thing.")]},
]
FIRMWARE = [
    {"id": "fw-picomem1", "title": "PicoMEM 1.x firmware",
     "summary": "Firmware for the PicoMEM 1.x card (with a Pico W) that fixes "
                "its Sound Blaster and its disk reads under os8088.",
     "dir": "firmware/picomem1",
     "notes": {
         "PM_W_10_16_26_OS8088.uf2": "The firmware. Copy it onto the Pico W in its BOOTSEL mode.",
         "README.md": "What the firmware fixes, and how to install it.",
         "SHA256SUMS": "Checksums, to confirm the download is intact.",
         "COPYING": "The firmware's licence (GPL version 2).",
         "v1board.patch": "The one change made to the firmware's source.",
     }},
]


def load_mkzip():
    p = os.path.join(ROOT, ".claude", "skills", "release-os8088", "mkzip.py")
    spec = importlib.util.spec_from_file_location("mkzip", p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def geo_of(path, name):
    """The geometry, off the image when it exists and off the name when not."""
    if name.endswith(".iso") or "usb" in name:
        return "hdd"
    if os.path.exists(path):
        with open(path, "rb") as f:
            b = f.read(512)
        if len(b) == 512:
            tot = struct.unpack_from("<H", b, 19)[0]
            if tot in SECTORS:
                return SECTORS[tot]
    m = re.search(r"(360|720|120)(-\d+)?\.img$", name)
    if m:
        return {"360": "360", "720": "720", "120": "1200"}[m.group(1)]
    return "1440"


def role_of(name, item_title, item_files):
    for pat, role in ROLE:
        m = re.match(pat, name)
        if m:
            if role is None:
                return "Everything set, disk %s" % m.group(2)
            return role
    return item_title


# --- reading a hard-disk image (FAT16 behind an MBR) --------------------------
# Plain-text files whose words the guide shows under their page, read off the
# disk itself - so a README.TXT that differs per disk shows its own text.
TEXT_EXT = ("TXT", "ME", "MD")
HASH_EXT = ("V88",)       # files the guide finds a preview for, by content
TEXT_NAMES = ("LICENSE", "COPYING")
TEXT_MAX = 64 * 1024


def as_text(name, body):
    """The words of a plain-text file, or None if it is not one."""
    stem, _, ext = name.partition(".")
    if ext not in TEXT_EXT and name not in TEXT_NAMES:
        return None
    try:
        body = cz_unwrap(body)
    except Exception:
        return None
    if len(body) > TEXT_MAX:
        return None
    t = body.replace(b"\x1a", b"").decode("cp437", "replace").replace("\r\n", "\n")
    if sum(1 for c in t if c < " " and c not in "\n\t") > len(t) // 50:
        return None
    return t


def survey_fat12(path):
    """Every entry on a floppy: {path: [size, attr]}, in directory order."""
    v = Fat12(path)
    files = {}

    def walk(ents, path):
        for e in ents:
            if e[0] in (0, 0xE5) or e[11] & 0x08 or e[11] == 0x0F:
                continue
            name = Fat12.pretty(e[:11])
            if name in (".", ".."):
                continue
            first = struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            if e[11] & 0x10:
                files[path + name + "/"] = [0, e[11]]
                csz = v.spc * v.bps
                sub = []
                for c in (v.chain(first) if first else []):
                    base = v.cluster_off(c)
                    sub += [bytes(v.img[base + k * 32:base + k * 32 + 32]) for k in range(csz // 32)]
                walk(sub, path + name + "/")
            else:
                files[path + name] = [size, e[11]]
                ext = name.partition(".")[2]
                if first and (ext in TEXT_EXT or ext in HASH_EXT or name in TEXT_NAMES):
                    csz = v.spc * v.bps
                    body = b"".join(bytes(v.img[v.cluster_off(c):v.cluster_off(c) + csz])
                                    for c in v.chain(first))[:size]
                    if ext in HASH_EXT:
                        hashes[path + name] = hashlib.sha256(body).hexdigest()
                    t = as_text(name, body) if ext not in HASH_EXT else None
                    if t is not None:
                        texts[path + name] = t

    texts, hashes = {}, {}
    walk([e for _, _, e in v.entries()], "")
    return {"free": len(v.free_clusters()), "clusters": v.nclus,
            "csize": v.spc * v.bps, "files": files, "texts": texts, "hashes": hashes}


def survey_hdd(path):
    blob = open(path, "rb").read()
    if blob[0:2] == b"\xDA\xBE":
        # A SEAGATE ST11 card's disk (tools/os88hdd.py --st11): the card keeps
        # its record in cylinder 0 and hides it, so the BIOS's sector 0 is the
        # first sector of physical cylinder 1.
        blob = blob[blob[4] * blob[5] * 512:]
    base = struct.unpack_from("<I", blob, 446 + 8)[0] * 512
    byts, spc, rsvd, nfat, rootent, tot16, _, fatsz = struct.unpack_from("<HBHBHHBH", blob, base + 11)
    tot = tot16 or struct.unpack_from("<I", blob, base + 32)[0]
    fat0 = base + rsvd * byts
    rootlba = rsvd + nfat * fatsz
    rootsecs = (rootent * 32 + byts - 1) // byts
    data0 = rootlba + rootsecs
    nclus = (tot - data0) // spc

    def chain(c):
        out = []
        while 2 <= c < 0xFFF0 and len(out) <= nclus:
            out.append(c)
            c = struct.unpack_from("<H", blob, fat0 + c * 2)[0]
        return out

    files = {}

    def walk(lba, secs, path):
        off = base + lba * byts
        for i in range(secs * byts // 32):
            e = blob[off + i * 32: off + i * 32 + 32]
            if not e or e[0] == 0:
                return
            if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F or (e[11] & 0x08) or e[0:1] == b".":
                continue
            nm = e[0:11].decode("ascii", "replace")
            stem, ext = nm[:8].strip(), nm[8:].strip()
            name = stem + ("." + ext if ext else "")
            c0 = struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            if e[11] & 0x10:
                files[path + name + "/"] = [0, e[11]]
                for c in chain(c0):
                    walk(data0 + (c - 2) * spc, spc, path + name + "/")
            else:
                files[path + name] = [size, e[11]]
                if c0 and (ext in TEXT_EXT or ext in HASH_EXT or name in TEXT_NAMES):
                    body = b"".join(blob[base + (data0 + (c - 2) * spc) * byts:
                                         base + (data0 + (c - 2) * spc + spc) * byts]
                                    for c in chain(c0))[:size]
                    if ext in HASH_EXT:
                        hashes[path + name] = hashlib.sha256(body).hexdigest()
                    t = as_text(name, body) if ext not in HASH_EXT else None
                    if t is not None:
                        texts[path + name] = t

    texts, hashes = {}, {}
    walk(rootlba, rootsecs, "")
    free = sum(1 for c in range(2, nclus + 2)
               if struct.unpack_from("<H", blob, fat0 + c * 2)[0] == 0)
    return {"free": free, "clusters": nclus, "csize": spc * byts, "files": files, "texts": texts,
            "hashes": hashes}


# --- manuals ----------------------------------------------------------------
def plain(s):
    """A caption as alt text: no link brackets, no markup."""
    return re.sub(r"[*`]", "", re.sub(r"\[\[([^\]]+)\]\]", r"\1", s))


def md_inline(s):
    s = html.escape(s, quote=False)
    code = []

    def keep(m):                    # a code span is literal: nothing inside it is markup
        code.append("<code>%s</code>" % m.group(2).strip())
        return "\x00%d\x00" % (len(code) - 1)
    s = re.sub(r"(``?)(.+?)\1", keep, s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"\*([^*]+)\*", r"<em>\1</em>", s)
    s = re.sub(r"\[([^\]]+)\]\((https?://[^)]+)\)", r'<a href="\2" target="_blank" rel="noopener">\1</a>', s)
    s = re.sub(r"\[\[([A-Z0-9_.]+/?)\]\]", r'<a href="#" data-item="\1">\1</a>', s)
    return re.sub(r"\x00(\d+)\x00", lambda m: code[int(m.group(1))], s)


def md(text, shots):
    """A small Markdown: #/##/### headings, paragraphs, - lists, 1. lists,
    | tables |, > notes, and ![caption](shot:id) screenshots."""
    out, para, lst, tbl, vids = [], [], None, [], []
    nshot = 0                       # the first shot introduces the program: full width

    def flush():
        nonlocal para, lst, tbl, vids
        if vids:
            # A run of videos or shots with no blank line between them is a
            # gallery, side by side.
            out.append(vids[0] if len(vids) == 1 else '<div class="vgrid">%s</div>' % "".join(vids))
            vids = []
        if para:
            out.append("<p>%s</p>" % md_inline(" ".join(para)))
            para = []
        if lst:
            tag, items = lst
            out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % md_inline(i) for i in items), tag))
            lst = None
        if tbl:
            rows = [r for r in tbl if not re.match(r"^\|[\s:|-]+\|$", r)]
            cells = [[c.strip() for c in r.strip("|").split("|")] for r in rows]
            h = "".join("<th>%s</th>" % md_inline(c) for c in cells[0])
            b = "".join("<tr>%s</tr>" % "".join("<td>%s</td>" % md_inline(c) for c in r) for r in cells[1:])
            out.append('<div class="tbl"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>' % (h, b))
            tbl = []

    for line in text.splitlines():
        s = line.rstrip()
        m = re.match(r"^!\[([^\]]*)\]\(video:([a-z0-9/-]+)\)$", s)
        if m:
            if para or lst or tbl:
                v, vids = vids, []
                flush()
                vids = v
            cap, name = m.group(1), m.group(2)
            pv = PREVIEW_BY_NAME.get(name)
            if pv:
                vids.append('<figure class="vid"><video controls preload="none" playsinline style="aspect-ratio:%d / %d" '
                            'src="%s" poster="%s" aria-label="%s"></video><figcaption>%s</figcaption></figure>'
                            % (pv["w"], pv["h"], pv["src"], pv["poster"], html.escape(plain(cap)), md_inline(cap)))
            else:
                vids.append('<figure class="shot pending full"><div class="frame"><span>Video to come</span>'
                            '<code>%s</code></div><figcaption>%s</figcaption></figure>' % (html.escape(name), md_inline(cap)))
            continue
        if vids and s and not s.startswith("!["):
            flush()
        m = re.match(r"^!\[([^\]]*)\]\(shot:([a-z0-9-]+)\)(?:\{(left|right|full)\})?$", s)
        if m:
            if para or lst or tbl:
                v, vids = vids, []
                flush()
                vids = v
            cap, sid, place = m.group(1), m.group(2), m.group(3)
            if sid in shots:
                src, w, h = shots[sid]
                # A shot narrower than half the screen floats beside the text
                # unless the manual says otherwise; `{full}` keeps it whole.
                place = place or ("right" if w <= 320 and nshot else "full")
                nshot += 1
                vids.append('<figure class="shot %s"><button class="zoom" data-zoom="%s" aria-label="Enlarge: %s">'
                           '<img src="%s" width="%d" height="%d" alt="%s"></button><figcaption>%s</figcaption></figure>'
                           % (place, src, html.escape(plain(cap)), src, w * 2, h * 2, html.escape(plain(cap)), md_inline(cap)))
            else:
                vids.append('<figure class="shot pending %s"><div class="frame"><span>Screenshot to come</span>'
                           '<code>%s</code></div><figcaption>%s</figcaption></figure>' % (place or "full", sid, md_inline(cap)))
            continue
        m = re.match(r"^(#{1,3})\s+(.*)$", s)
        if m:
            flush()
            lvl = len(m.group(1)) + 1
            out.append("<h%d>%s</h%d>" % (lvl, md_inline(m.group(2)), lvl))
            continue
        if s.startswith("|"):
            if para or lst:
                p, t = para, tbl
                tbl = []
                flush()
                tbl = t
            tbl.append(s)
            continue
        elif tbl:
            flush()
        if s.startswith("> "):
            flush()
            out.append('<aside class="note">%s</aside>' % md_inline(s[2:]))
            continue
        m = re.match(r"^(-|\d+\.)\s+(.*)$", s)
        if m:
            tag = "ol" if m.group(1)[0].isdigit() else "ul"
            if para:
                out.append("<p>%s</p>" % md_inline(" ".join(para)))
                para = []
            if lst and lst[0] != tag:
                flush()
            if not lst:
                lst = (tag, [])
            lst[1].append(m.group(2))
            continue
        if not s:
            flush()
            continue
        if lst and line.startswith("  "):
            lst[1][-1] += " " + s.strip()
            continue
        para.append(s.strip())
    flush()
    return "\n".join(out)


def load_pages(sub):
    """tools/guide/<sub>/*.md, keyed by file name: the disk pages."""
    d = os.path.join(GUIDE, sub)
    out = {}
    if os.path.isdir(d):
        for fn in sorted(os.listdir(d)):
            if fn.endswith(".md"):
                out[fn[:-3]] = md(open(os.path.join(d, fn), encoding="utf-8").read(), {})
    return out


ONLY = None                    # --manuals: publish just these (a review round)


def load_manuals():
    d = os.path.join(GUIDE, "manuals")
    shots_dir = os.path.join(BUILD, "guide", "shots")
    out = {}
    if not os.path.isdir(d):
        return out
    for fn in sorted(os.listdir(d)):
        if not fn.endswith(".md"):
            continue
        key = fn[:-3].upper()
        if ONLY is not None and key not in ONLY:
            continue
        text = open(os.path.join(d, fn), encoding="utf-8").read()
        shots = {}
        if os.path.isdir(shots_dir):
            for s in os.listdir(shots_dir):
                if s.endswith(".png"):
                    with open(os.path.join(shots_dir, s), "rb") as f:
                        w, h = struct.unpack(">II", f.read(24)[16:24])
                    shots[s[:-4]] = ("shots/" + s, w, h)
        title = re.search(r"^#\s+(.*)$", text, re.M)
        out[key] = {"title": title.group(1) if title else key,
                    "html": md(re.sub(r"^#\s+.*\n", "", text, count=1), shots)}
    return out


# --- the data the page is built from ------------------------------------------
# --- descriptions that depend on WHERE a file is, not only its name ----------
CPM_EXT = {
    "COM": ("program", "A CP/M program"), "SUB": ("data", "A CP/M batch file, run with SUBMIT"),
    "ASM": ("document", "Assembly language source"), "Z80": ("document", "Z80 assembly source"),
    "MAC": ("document", "Assembly language source"), "PAS": ("document", "Turbo Pascal source"),
    "INC": ("document", "Turbo Pascal source, included by another"), "TXT": ("document", "Documentation"),
    "DOC": ("document", "Documentation"), "ME": ("document", "Read this first"),
    "HLP": ("document", "Help text"), "LIB": ("data", "A library for the assembler"),
    "LBR": ("data", "A CP/M library archive"), "OVR": ("program", "Part of a program, loaded as it runs"),
}


def cpm_areas():
    """RunCPM's user areas, from tools/getcpmsw.py's own table."""
    try:
        import getcpmsw
    except Exception:
        return {}
    out = {"0": ("User area 0", "RunCPM's master disk: the CP/M command processor, its utilities and their documentation.", {})}
    for _, dest, what, how, titles in getcpmsw.AREAS:
        area = dest.split("/")[1]
        progs = {}
        for t, d in titles:
            for name in t.split("/"):
                progs[name] = d
        out[area] = ("User area %d" % int(area, 16), "%s. Type %s at the A> prompt to reach it." % (what[:1].upper() + what[1:], how), progs)
    return out


def viddemo_titles(adapter):
    """Each video's lines from the folder's own 00-VIDS.TXT."""
    p = os.path.join(ROOT, "apps", "video", "demo", adapter, "00-VIDS.TXT")
    out, cur = {}, None
    for line in open(p, encoding="utf-8", errors="replace"):
        if re.match(r"^\S+\.V88\s*$", line):
            cur = line.strip()
            out[cur] = []
        elif cur and line.startswith("  ") and line.strip():
            out[cur].append(line.strip())
        elif cur and not line.strip() and out[cur]:
            cur = None
    return out


def brief(text):
    """A listing-width phrase out of a longer one: before the first comma,
    without a parenthesis or a final period."""
    t = re.sub(r"\s*\([^)]*\)", "", text).split(",")[0].split(":")[0].split(". ")[0].rstrip(".")
    return t[:1].upper() + t[1:]


def path_info(d, path, cpm, vids):
    """(kind, title, summary, short) for a file whose meaning is its place."""
    r = _path_info(d, path, cpm, vids)
    return r + (brief(r[2]),) if r else None


def _path_info(d, path, cpm, vids):
    m = re.search(r"(?:^|/)A/([0-9A-F])/(?:([^/]+))?$", path)
    if m and cpm:
        area = cpm.get(m.group(1))
        if not m.group(2):
            return ("folder", area[0], area[1]) if area else None
        name = m.group(2)
        stem, _, ext = name.partition(".")
        if area and stem in area[2] and ext == "COM":
            return ("program", stem, area[2][stem][:1].upper() + area[2][stem][1:] + ".")
        k = CPM_EXT.get(ext)
        return (k[0], name, k[1] + ".") if k else ("data", name, "A file this area's programs use.")
    if re.search(r"(?:^|/)A/$", path) and cpm:
        return ("folder", "Drive A", "CP/M's drive A. Each folder inside it is one user area.")
    name = path.split("/")[-1]
    if name in vids:
        v = vids[name]
        return ("video", v[0], " · ".join(v[1:]))
    return None


TEXTS = None
ENTRIES = {}
PREVIEWS = []           # tools/guide/videos/previews.json, as a list
PREVIEW_IDX = {}        # source SHA-256 -> index into PREVIEWS
PREVIEW_BY_NAME = {}    # "cga/03-cga4" -> its PREVIEWS record, for a manual's video:


def load_previews():
    """The committed video previews (tools/guide/vidpreview.py)."""
    global PREVIEWS, PREVIEW_IDX, PREVIEW_BY_NAME
    PREVIEWS, PREVIEW_IDX, PREVIEW_BY_NAME = [], {}, {}
    p = os.path.join(GUIDE, "videos", "previews.json")
    if not os.path.exists(p):
        return
    for h, v in json.load(open(p)).items():
        if os.path.exists(os.path.join(GUIDE, "videos", v["mp4"])):
            PREVIEW_IDX[h] = len(PREVIEWS)
            PREVIEWS.append({"src": "videos/" + v["mp4"], "w": v["w"], "h": v["h"], "secs": v["secs"],
                             "poster": "videos/" + v["poster"] if v.get("poster") else ""})
            PREVIEW_BY_NAME[v["mp4"][:-4]] = PREVIEWS[-1]


def readme_info(d, path, text):
    """A README that is not os8088's own names whose it is: the program whose
    folder it is in, or whose disk it is on."""
    if "os8088 - READ ME" in text[:200]:
        return None
    parts = path.split("/")
    owner = None
    if len(parts) > 1:
        e = ENTRIES.get(parts[-2] + "/")
        if e and e.get("kind") == "folder":
            owner = e.get("title")
        if re.match(r"^[0-9A-F]$", parts[-2]):        # a RunCPM user area
            first = text.strip().splitlines()[0] if text.strip() else ""
            owner = re.split(r"\s+(?:Copyright|-|\(c\))", first)[0].strip() or None
    owner = owner or d["role"]
    return ("document", "%s read me" % owner,
            "Notes from %s: what it is and how to use it. Double-click to read it in Note Pad." % owner,
            "%s's read me" % owner)


def disk_record(n, set_id, gkey, section, role, geo=None, vids=None, pinfo=None):
    path = os.path.join(BUILD, n)
    geo = geo or geo_of(path, n)
    d = {"id": re.sub(r"[^a-z0-9]+", "-", n.lower()), "file": n, "geo": geo, "set": set_id,
         "section": section, "group": gkey, "role": role, "built": False}
    if os.path.exists(path):
        try:
            s = None if n.endswith(".iso") else (survey_hdd(path) if geo == "hdd" else survey_fat12(path))
        except Exception as e:      # an image this reader cannot walk
            s = None
            d["error"] = str(e)
        if s:
            cpm = CPM if any(k.endswith("RUNCPM.O88") for k in s["files"]) else None
            tree = []
            for k, v in s["files"].items():
                row = [k, v[0], v[1]]
                pi = path_info(d, k, cpm, vids or {}) if pinfo is not None else None
                if pi:
                    if pi not in pinfo:
                        pinfo.append(pi)
                    row.append(pinfo.index(pi))
                tree.append(row)
            for row in tree:
                t = s.get("texts", {}).get(row[0])
                if t is not None and row[0].split("/")[-1] in ("README.TXT", "README.MD") and pinfo is not None:
                    ri = readme_info(d, row[0], t)
                    if ri:
                        if ri not in pinfo:
                            pinfo.append(ri)
                        row[3:] = [pinfo.index(ri)]
            d.update(built=True, free=s["free"], clusters=s["clusters"], csize=s["csize"], tree=tree)
            pv = {k: PREVIEW_IDX[h] for k, h in s.get("hashes", {}).items() if h in PREVIEW_IDX}
            if pv:
                d["previews"] = pv
            if s.get("texts") and TEXTS is not None:
                d["texts"] = {}
                for k, t in s["texts"].items():
                    if t not in TEXTS:
                        TEXTS.append(t)
                    d["texts"][k] = TEXTS.index(t)
        d["bytes"] = os.path.getsize(path)
        if n.endswith(".iso"):
            d["built"] = True
            d["sameas"] = "os8088-usb-img"
    return d


def collect():
    mk = load_mkzip()
    entries = {}
    p = os.path.join(GUIDE, "entries.toml")
    if os.path.exists(p):
        with open(p, "rb") as f:
            entries = tomllib.load(f)
    global CPM, TEXTS, ENTRIES
    CPM = cpm_areas()
    load_previews()
    TEXTS = []
    ENTRIES = entries
    disks, sets, pinfo = [], [], []
    for gkey, gtitle, _ in mk.GROUPS:
        if gkey == "about":
            continue
        for grp, title, required, desc, files in mk.ITEMS:
            if grp != gkey:
                continue
            names = []
            for fname, _, _ in files:
                if fname.startswith("@"):
                    lp = os.path.join(BUILD, fname[1:])
                    if os.path.exists(lp):
                        names += [os.path.basename(l.strip()) for l in open(lp) if l.strip()]
                    else:
                        names.append(fname[1:].replace(".list", "-1.img"))
                else:
                    names.append(fname)
            set_id = "set-" + re.sub(r"[^a-z0-9]+", "-", (gkey + " " + title).lower()).strip("-")
            members = []
            for n in names:
                d = disk_record(n, set_id, gkey, SECTION.get(gkey, "own"), role_of(n, title, files), pinfo=pinfo)
                members.append(d["id"])
                disks.append(d)
            sets.append({"id": set_id, "group": gkey, "groupTitle": gtitle,
                         "title": title, "desc": BLURB.get(title.split(",")[0] if title.startswith("Everything") else title, desc),
                         "required": required, "disks": members})
    vd = os.path.join(ROOT, "apps", "video", "demo")
    adapters = sorted(a for a in os.listdir(vd) if os.path.exists(os.path.join(vd, a, "00-VIDS.TXT"))) if os.path.isdir(vd) else []
    if adapters:
        members = []
        for a in adapters:
            n = "VIDDEMO-%s-%s.VHD" % (a.upper(), VIDDEMO_DISK.get(a, "ST11R"))
            d = disk_record(n, "set-viddemo", "viddemo", "viddemo",
                            "%s demo drive" % VIDDEMO_NAMES.get(a, a.upper()), geo="hdd",
                            vids=viddemo_titles(a), pinfo=pinfo)
            members.append(d["id"])
            disks.append(d)
        sets.append({"id": "set-viddemo", "group": "viddemo", "groupTitle": "Video demo hard drives",
                     "title": "Video demo", "desc": BLURB["Video demo"], "required": False,
                     "disks": members})
    tools = []
    for t in TOOLS:
        t = dict(t)
        t["files"] = [[f, n, os.path.getsize(os.path.join(ROOT, f)) if os.path.exists(os.path.join(ROOT, f)) else 0]
                      for f, n in t["files"]]
        tools.append(t)
    firmware = []
    for fw in FIRMWARE:
        fw = dict(fw)
        d = os.path.join(ROOT, fw["dir"])
        fw["files"] = [[fw["dir"] + "/" + f, fw["notes"].get(f, ""), os.path.getsize(os.path.join(d, f))]
                       for f in sorted(os.listdir(d))]
        del fw["notes"]
        firmware.append(fw)
    try:
        commit = subprocess.check_output(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], text=True).strip()
        count = subprocess.check_output(["git", "-C", ROOT, "rev-list", "--count", "HEAD"], text=True).strip()
    except Exception:
        commit, count = "?", "?"
    return {"geos": [{"key": k, "label": l, "hint": h} for k, l, h in GEOS],
            "sectionTitles": SECTION_TITLE, "blurbs": BLURB, "sets": sets, "disks": disks, "pinfo": pinfo, "texts": TEXTS,
            "previews": PREVIEWS,
            "entries": entries, "manuals": load_manuals(), "diskpages": load_pages("disks"),
            "tools": tools, "firmware": firmware,
            "build": {"commit": commit, "count": count}}


def missing(data):
    seen = {}
    for d in data["disks"]:
        for path, *rest in d.get("tree", []):
            if len(rest) > 2:
                continue
            n = path.rstrip("/").split("/")[-1] + ("/" if path.endswith("/") else "")
            seen.setdefault(n, set()).add(d["file"])
    return sorted((n, sorted(v)) for n, v in seen.items() if n not in data["entries"])


def render(data, fragment):
    page = open(os.path.join(GUIDE, "page.html"), encoding="utf-8").read()
    blob = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    page = page.replace("/*@DATA@*/null", blob)
    if fragment:
        return page
    return ("<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">\n"
            "</head>\n<body>\n" + page + "\n</body>\n</html>\n")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--fragment", action="store_true")
    ap.add_argument("--zip", action="store_true")
    ap.add_argument("--missing", action="store_true")
    ap.add_argument("--manuals", help="comma-separated manual keys to include, e.g. WORD_O88,CALC_O88")
    ap.add_argument("-o", "--out", default=os.path.join(BUILD, "guide"))
    a = ap.parse_args(argv)
    global ONLY
    if a.manuals:
        ONLY = set(k.strip().upper() for k in a.manuals.split(","))
    data = collect()
    if a.missing:
        for n, where in missing(data):
            print("%-14s %s" % (n, ", ".join(where[:4]) + (" ..." if len(where) > 4 else "")))
        return 0
    os.makedirs(a.out, exist_ok=True)
    fn = os.path.join(a.out, "guide-fragment.html" if a.fragment else "index.html")
    with open(fn, "w", encoding="utf-8") as f:
        f.write(render(data, a.fragment))
    for v in data["previews"]:
        for f in filter(None, (v["src"], v["poster"])):
            dst = os.path.join(a.out, f)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(GUIDE, f), dst)
    built = sum(1 for d in data["disks"] if d["built"])
    print("os88guide: %s  (%d disks, %d built; %d descriptions; %d manuals; %d undescribed)"
          % (os.path.relpath(fn, ROOT), len(data["disks"]), built, len(data["entries"]),
             len(data["manuals"]), len(missing(data))))
    if a.zip:
        zp = a.out.rstrip("/") + ".zip"
        with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(os.path.join(a.out, "index.html") if os.path.exists(os.path.join(a.out, "index.html")) else fn,
                    "guide/index.html")
            sd = os.path.join(a.out, "shots")
            if os.path.isdir(sd):
                for s in sorted(os.listdir(sd)):
                    z.write(os.path.join(sd, s), "guide/shots/" + s)
            for v in data["previews"]:
                for f in filter(None, (v["src"], v["poster"])):
                    z.write(os.path.join(a.out, f), "guide/" + f, zipfile.ZIP_STORED)
        print("os88guide: %s" % os.path.relpath(zp, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
