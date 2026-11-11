#!/usr/bin/env python3
"""The Browser's GIF fixtures (SPEC.md 71.26): one page and the GIFs it names,
one case per rule of the contract, made by tools/pixcorpus.py's encoder so
that nothing binary is committed.

    python3 tests/brgifs.py DIR      writes DIR/IMGGIF.HTM and DIR/*.GIF

CASES is the table a row reads: each name, how it is written in the page and
whether the Browser must show it (True) or keep its alt box (False).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import pixcorpus  # noqa: E402


def ramp(w, h, n):
    """Rows of indices that use the whole palette, so every plan is read."""
    return [[(x * n // max(1, w) + y) % n for x in range(w)] for y in range(h)]


def pal_rgb(n):
    out = []
    for i in range(n):
        out.append(((i * 53) & 255, (i * 97 + 40) & 255, (255 - i * 29) & 255))
    return out


def cases():
    """name -> (gif bytes or None for a 404, page attributes, shows)"""
    c = {}
    p256 = pal_rgb(256)
    p16 = pal_rgb(16)
    c["PLAIN"] = (pixcorpus.gif(64, 48, ramp(64, 48, 256), gpal=p256),
                  'width=64 height=48', True)
    c["NOSIZE"] = (pixcorpus.gif(40, 24, ramp(40, 24, 16), gpal=p16),
                   '', True)
    c["ILACE"] = (pixcorpus.gif(48, 37, ramp(48, 37, 16), gpal=p16,
                                ilace=True), 'width=48 height=37', True)
    c["LOCAL"] = (pixcorpus.gif(32, 32, ramp(32, 32, 16), gpal=p16,
                                lpal=pal_rgb(16)[::-1]), '', True)
    c["TRANS"] = (pixcorpus.gif(32, 16, ramp(32, 16, 4), gpal=pal_rgb(4),
                                trans=1), '', True)
    c["ONE"] = (pixcorpus.gif(1, 1, [[3]], gpal=pal_rgb(4)), '', True)
    c["WIDE"] = (pixcorpus.gif(48, 32, ramp(48, 32, 16), gpal=p16),
                 'width=96', True)
    c["SMALL"] = (pixcorpus.gif(64, 48, ramp(64, 48, 16), gpal=p16),
                  'width=32', True)
    c["BORDER"] = (pixcorpus.gif(24, 24, ramp(24, 24, 16), gpal=p16),
                   'border=2', True)
    c["FRAME"] = (pixcorpus.gif(40, 30, ramp(20, 10, 16), gpal=p16, fw=20,
                                fh=10, left=8, top=6, bgi=2), '', True)
    good = pixcorpus.gif(16, 16, ramp(16, 16, 16), gpal=p16)
    c["CUT"] = (good[:len(good) // 2], '', False)
    # a Clear, a root, then a code past the next free one (18): refused
    bad = pixcorpus.pack_lsb([(16, 5), (3, 5), (30, 5), (17, 5)])
    c["BADCODE"] = (pixcorpus.gif(16, 16, ramp(16, 16, 16), gpal=p16,
                                  codes=bad), '', False)
    c["NOTGIF"] = (b"<html><body>not a picture</body></html>", '', False)
    c["MISSING"] = (None, '', False)
    return c


def page(cs):
    rows = ["<html><head><title>GIF fixtures</title></head><body>",
            "<h1>GIF fixtures</h1>"]
    for name, (_, attrs, shows) in cs.items():
        a = (" " + attrs) if attrs else ""
        rows.append('<p>%s <img src="%s.GIF" alt="%s"%s> end</p>'
                    % (name, name, name.lower(), a))
    rows.append('<p><a href="next.htm"><img src="PLAIN.GIF" alt="link" '
                'border=2></a> linked</p>')
    rows.append("</body></html>")
    return ("\r\n".join(rows) + "\r\n").encode("ascii")


def write(d):
    os.makedirs(d, exist_ok=True)
    cs = cases()
    for name, (data, _, _) in cs.items():
        if data is not None:
            with open(os.path.join(d, name + ".GIF"), "wb") as f:
                f.write(data)
    with open(os.path.join(d, "IMGGIF.HTM"), "wb") as f:
        f.write(page(cs))
    return cs


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    write(sys.argv[1])
    print("wrote", sys.argv[1])
