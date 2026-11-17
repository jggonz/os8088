#!/usr/bin/env python3
"""Which link the Browser opens as a PAGE and which it hands to DOWNLOADS.

    python3 tests/unit/t_brautosave.py

A click on a link whose path ends in a suffix the Browser renders (.htm, .cgi,
.php, ...) navigates; any other suffix goes straight to the Save dialog and the
Download Manager (SPEC.md 71.20, br_dl_autosave), and a path with no suffix
navigates, because extensionless URLs usually serve HTML. Only the LAST path
component's suffix counts, and the query and fragment are not part of it.

This runs the routine AS IT SHIPS: build/browser.bin is loaded under unicorn
and br_dl_autosave is called on each path, so the table and the scanner are
the assembled ones and not a description of them. It also holds the routine
to SPEC.md 1's register discipline - every register but the flags comes back
as it went in - which it broke until this row existed (DX).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                        # noqa: E402

try:
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import UC_X86_REG_AX, UC_X86_REG_BX, \
        UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI, \
        UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_DS, \
        UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_EFLAGS
except ImportError:
    print("SKIP: the unicorn module is not installed")
    sys.exit(0)

SEG = 0x1000
BASE = SEG << 4
RET = 0xFFF8                    # the return address: emulation stops there

DOWNLOAD = [
    "/file.zip", "/a/b/FILE.ZIP", "/readme.txt", "/README.TXT", "/pic.gif",
    "/pic.jpg", "/prog.exe", "/arc.tar.gz", "/v1.2/data.bin", "/get.zip?x=1",
    "/a.zip#top", "/doc.pdf", "/song.mod", "/a.htm.zip", "/a.htmx", "/a.ph",
    "/dir.htm/file.zip",
]
PAGE = [
    "/", "/index.htm", "/INDEX.HTML", "/cgi-bin/search.cgi", "/a.php",
    "/a.PHP?x=1.zip", "/p.phtml", "/p.shtml", "/p.asp", "/p.aspx", "/p.jsp",
    "/dir.v2/", "/dir.v2/page", "/noext", "/a.htm#sec.zip", "/a.html?q=1",
    "/x.cgi?file=a.zip", "/a.zip.htm", "/script.pl", "/page.cfm",
    "/doc.xhtml", "/Page.CFM?id=3",
]
FAILS = []


PSEG = 0x3000                   # part 0, where br_dl_defaultname lives now
PBASE = PSEG << 4               # (SPEC.md 71.20.5): CS here, DS the primary's


def symbols(name="build/browser.map"):
    out = {}
    with open(os88build.at(name), encoding="ascii") as f:
        for line in f:
            m = re.match(r"^\s*([0-9A-F]+)\s+(?:[0-9A-F]+\s+)?(\S+)\s*$", line)
            if m:
                out.setdefault(m.group(2), int(m.group(1), 16))
    return out


def main():
    image = open(os88build.at("build/browser.bin"), "rb").read()
    sym = symbols()
    entry, path = sym["br_dl_autosave"], sym["br_path"]
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(BASE, image)
    seed = {UC_X86_REG_AX: 0x1111, UC_X86_REG_BX: 0x2222,
            UC_X86_REG_CX: 0x3333, UC_X86_REG_DX: 0x4444,
            UC_X86_REG_DI: 0x5555, UC_X86_REG_BP: 0x6666}
    names = {UC_X86_REG_AX: "AX", UC_X86_REG_BX: "BX", UC_X86_REG_CX: "CX",
             UC_X86_REG_DX: "DX", UC_X86_REG_DI: "DI", UC_X86_REG_BP: "BP",
             UC_X86_REG_SI: "SI"}

    def run(url):
        mu.mem_write(BASE + path, url.encode("ascii") + b"\0")
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            mu.reg_write(r, SEG)
        for r, v in seed.items():
            mu.reg_write(r, v)
        mu.reg_write(UC_X86_REG_SI, path)
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.mem_write(BASE + 0xFFF0, bytes((RET & 255, RET >> 8)))
        mu.reg_write(UC_X86_REG_EFLAGS, 0x0002)
        mu.emu_start(BASE + entry, BASE + RET, count=200_000)
        clobbered = [names[r] for r in list(seed) + [UC_X86_REG_SI]
                     if mu.reg_read(r) != (path if r == UC_X86_REG_SI
                                           else seed[r])]
        return bool(mu.reg_read(UC_X86_REG_EFLAGS) & 1), clobbered

    for want, urls in ((True, DOWNLOAD), (False, PAGE)):
        for url in urls:
            got, clobbered = run(url)
            what = "%-22s -> %s" % (url, "download" if want else "page")
            if got != want:
                FAILS.append(what + ", got " + ("download" if got else "page"))
                print("  FAIL  " + FAILS[-1])
            elif clobbered:
                FAILS.append(what + ", but it clobbered " + ",".join(clobbered))
                print("  FAIL  " + FAILS[-1])
            else:
                print("  ok    " + what)
    n = len(DOWNLOAD) + len(PAGE)
    print("t_brautosave: %d of %d paths routed as expected" % (n - len(FAILS), n))
    suggested(mu, sym, image)
    return 1 if FAILS else 0


# --- THE NAME A DOWNLOAD SUGGESTS (SPEC.md 71.20.4) -------------------------
# A page fetch that turned out to be a file suggests the server's filename=
# from Content-Disposition when it is a plain 8.3-sized name, else the URL's
# last component, else DOWNLOAD.BIN. The headers go through br_hdrb, the
# fetch's own byte-at-a-time parser, so br_cdbyte and the line end that closes
# an unquoted value are the shipped ones; then br_dl_defaultname is asked, with
# br_dl_index FFFFh (the content-type route) and with a link index (which must
# never read a filename=).
HEADERS = [
    # (Content-Disposition line or None, URL path, index, expected name)
    ('attachment; filename="REPORT.ZIP"', "/get.cgi?id=7", 0xFFFF, "REPORT.ZIP"),
    ("attachment; filename=data.bin", "/get.cgi?id=7", 0xFFFF, "data.bin"),
    ("ATTACHMENT; FILENAME=Up.Txt; size=9", "/get.cgi", 0xFFFF, "Up.Txt"),
    ('inline; filename="a-name-too-long.zip"', "/get.cgi", 0xFFFF, "get.cgi"),
    ('attachment; filename="../../AUTOEXEC.BAT"', "/get.cgi", 0xFFFF, "get.cgi"),
    ('attachment; filename="C:X.BIN"', "/get.cgi", 0xFFFF, "get.cgi"),
    ('attachment; filename=""', "/get.cgi", 0xFFFF, "get.cgi"),
    ("attachment; filename*=UTF-8''x.zip", "/get.cgi", 0xFFFF, "get.cgi"),
    (None, "/files/report.zip", 0xFFFF, "report.zip"),
    (None, "/download-test.cgi?id=7", 0xFFFF, "DOWNLOAD.BIN"),
    ('attachment; filename="REPORT.ZIP"', "/get.cgi", 3, "get.cgi"),
]


def suggested(mu, sym, image):
    hdrb = sym["br_hdrb"]
    # br_dl_defaultname is part 0's (SPEC.md 71.20.5): the part is loaded at
    # its own segment and called with CS there and DS the primary's, as
    # br_dl_link's far entry runs it
    deflt = symbols("build/browser.part.map")["br_dl_defaultname"]
    mu.mem_write(PBASE, open(os88build.at("build/browser.part.bin"), "rb").read())
    bss0, bss1 = len(image), sym["br_parse_bss_end"]
    bad = 0
    for cd, path, index, want in HEADERS:
        mu.mem_write(BASE + bss0, bytes(bss1 - bss0))      # a fresh fetch
        head = b"HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\n"
        if cd is not None:
            head += b"Content-Disposition: " + cd.encode("ascii") + b"\r\n"
        head += b"\r\n"
        for byte in head:
            call(mu, hdrb, ax=byte)
        mu.mem_write(BASE + sym["br_dl_path"], path.encode("ascii") + b"\0")
        mu.mem_write(BASE + sym["br_dl_index"], bytes((index & 255, index >> 8)))
        call(mu, deflt, cs=PSEG)
        got = bytes(mu.mem_read(BASE + sym["br_dl_name"], 14)).split(b"\0")[0]
        what = "%-44s %-24s -> %s" % (cd, path + ("" if index == 0xFFFF
                                                 else " (link %d)" % index), want)
        if got.decode("ascii", "replace") != want:
            bad += 1
            FAILS.append(what + ", got %r" % got)
            print("  FAIL  " + FAILS[-1])
        else:
            print("  ok    " + what)
    print("t_brautosave: %d of %d suggested names as expected"
          % (len(HEADERS) - bad, len(HEADERS)))


def call(mu, entry, ax=0, cs=SEG):
    """Near-call ENTRY at CS (DS, ES and SS at SEG) with AX, returning at RET."""
    for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
        mu.reg_write(r, SEG)
    mu.reg_write(UC_X86_REG_CS, cs)
    mu.reg_write(UC_X86_REG_AX, ax)
    mu.reg_write(UC_X86_REG_SP, 0xFFF0)
    mu.mem_write(BASE + 0xFFF0, bytes((RET & 255, RET >> 8)))
    mu.emu_start((cs << 4) + entry, (cs << 4) + RET, count=200_000)


if __name__ == "__main__":
    sys.exit(main())
