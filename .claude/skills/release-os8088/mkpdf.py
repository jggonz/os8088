#!/usr/bin/env python3
"""Print the release's shareable PDF from its HTML (SKILL.md step 6b).

    python3 .claude/skills/release-os8088/mkpdf.py WORK/release.html \
        --version v1.0.YYYYMMDD [--out ~/Documents]

WORK/release.html is the page you wrote from pdf/example-20261111.html, with
its screenshots beside it in WORK/. This prints it with headless Chrome,
renders every page to WORK/pg-N.png so you can LOOK at each one, and copies
the PDF to <out>/os8088-<version>-release.pdf.

Chrome is the printer because it is already on the machine and needs no
Python package: pip was broken on this host's Python 3.15 the first time
(truststore could not parse the macOS version), so reportlab was never an
option. The page's own @page rule sets the paper; --no-pdf-header-footer
keeps Chrome's date and file URL off it.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

CHROMES = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "google-chrome", "google-chrome-stable", "chromium", "chromium-browser",
]


def find_chrome():
    for c in CHROMES:
        p = c if os.path.isabs(c) else shutil.which(c)
        if p and os.path.exists(p):
            return p
    sys.exit("mkpdf: no Chrome or Chromium found (tried %s)" % ", ".join(CHROMES))


def render_pages(pdf, work):
    """One PNG per page into WORK, for looking at. pdftoppm, else mutool."""
    for old in glob.glob(os.path.join(work, "pg-*.png")):
        os.remove(old)
    if shutil.which("pdftoppm"):
        subprocess.run(["pdftoppm", "-png", "-r", "80", pdf,
                        os.path.join(work, "pg")], check=True)
    elif shutil.which("mutool"):
        subprocess.run(["mutool", "draw", "-r", "80", "-o",
                        os.path.join(work, "pg-%d.png"), pdf], check=True)
    else:
        print("mkpdf: neither pdftoppm nor mutool - open the PDF and look at "
              "every page yourself")
        return []
    return sorted(glob.glob(os.path.join(work, "pg-*.png")))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("html")
    ap.add_argument("--version", required=True, help="e.g. v1.0.20261111")
    ap.add_argument("--out", default=os.path.expanduser("~/Documents"))
    a = ap.parse_args()

    html = os.path.abspath(a.html)
    work = os.path.dirname(html)
    if not os.path.exists(html):
        sys.exit("mkpdf: %s not found" % html)
    # A picture the page names and the folder lacks prints as a broken box
    # and Chrome says nothing, so refuse it here.
    import re
    src = open(html, encoding="utf-8").read()
    missing = [s for s in re.findall(r'<img[^>]*\ssrc="([^"]+)"', src)
               if "://" not in s and not os.path.exists(os.path.join(work, s))]
    if missing:
        sys.exit("mkpdf: the page names pictures that are not in %s: %s"
                 % (work, ", ".join(missing)))
    if "background" in src and re.search(r"body[^{]*\{[^}]*background\s*:\s*#(?!fff)", src):
        print("mkpdf: WARNING - the body background is not white; the PDF is "
              "meant to print on white (SKILL.md step 6b)")

    pdf = os.path.join(work, "out.pdf")
    if os.path.exists(pdf):
        os.remove(pdf)
    subprocess.run([find_chrome(), "--headless=new", "--disable-gpu",
                    "--no-pdf-header-footer", "--print-to-pdf=" + pdf,
                    "file://" + html],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(pdf) or os.path.getsize(pdf) == 0:
        sys.exit("mkpdf: Chrome wrote no PDF")

    pages = render_pages(pdf, work)
    os.makedirs(a.out, exist_ok=True)
    dest = os.path.join(a.out, "os8088-%s-release.pdf" % a.version)
    shutil.copyfile(pdf, dest)
    print("mkpdf: %s, %d page(s), %d bytes" % (dest, len(pages) or 0,
                                               os.path.getsize(dest)))
    for p in pages:
        print("  look at: %s" % p)


if __name__ == "__main__":
    main()
