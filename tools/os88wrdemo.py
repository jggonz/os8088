#!/usr/bin/env python3
"""os88wrdemo.py - WaveRack's demo clips: real music, public domain or CC0.

    python3 tools/os88wrdemo.py fetch            # the sources, into build/wrdemo-src/
    python3 tools/os88wrdemo.py make             # -> apps/waverack/demo/*.WAV + CREDITS.TXT
    python3 tools/os88wrdemo.py clips OUT NAME.. # a disk's CLIPS.TXT: what it carries
    python3 tools/os88wrdemo.py --selfcheck      # the committed clips are what CLIPS says

THE CLIPS ARE COMMITTED (apps/waverack/demo/, the Video Player's shape for its
demo media): `make` reads them and needs no network and no lame. This file is
how they were made, and how they are made again: `fetch` downloads each
source from Wikimedia Commons - the MP3 Commons serves of each recording -
and holds it to a pinned SHA-256; `make` decodes it with `lame --decode` (the
one decoder on the build host: there is no ffmpeg or sox), takes the pinned
excerpt, mixes, low-passes and decimates it (44,100 Hz to 22,050 or 11,025:
whole steps, a windowed-sinc FIR then every 2nd or 4th sample), fades its
ends, normalises its peak to 0.9 of full scale and writes the format pinned
beside it - 8-bit with a seeded triangular dither, so a rerun is
byte-identical.

THE RULE FOR A SOURCE (docs/plans/WAVERACK-PLAN.md, wave 7): a recording on
Wikimedia Commons whose file page says PUBLIC DOMAIN or CC0, read before it
was taken - the date each page was read is pinned beside it. No CC-BY-SA,
nothing whose licence is about the composition and not the recording.
"""
import hashlib
import os
import subprocess
import sys
import urllib.request
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "build", "wrdemo-src")
OUT = os.path.join(ROOT, "apps", "waverack", "demo")
UA = "os8088-waverack-demo/1.0 (tools/os88wrdemo.py)"
PAGE = "https://commons.wikimedia.org/wiki/"
UP = "https://upload.wikimedia.org/wikipedia/commons/"
READ = "2026-10-28"                     # every page below was read that day

# name: (page, mp3, sha256, work, composer, performer, licence,
#        start s, length s, rate, bits, channels)
CLIPS = [
    ("FURELISE.WAV", "File:FurElise.ogg",
     "transcoded/7/7b/FurElise.ogg/FurElise.ogg.mp3",
     "b19867f33192b2a6be665571ae5dd5493e977eafd354c279d257be1b6835bd25",
     "Fur Elise (Bagatelle in A minor, WoO 59)", "Ludwig van Beethoven",
     "V Gao (Wikipedia user Gaodifan), 2006",
     "Public domain (PD-self) and CC0 1.0, by the performer",
     2.0, 12.0, 11025, 8, 1),
    ("MAPLELF.WAV",
     "File:1906 - Scott Joplin's Maple Leaf Rag (1899) played by the "
     "United States Marine Band.ogg",
     "transcoded/3/3a/1906_-_Scott_Joplin%27s_Maple_Leaf_Rag_%281899%29_"
     "played_by_the_United_States_Marine_Band.ogg/1906_-_Scott_Joplin%27s_"
     "Maple_Leaf_Rag_%281899%29_played_by_the_United_States_Marine_Band"
     ".ogg.mp3",
     "bce5ca7c142676b403e3711e565ecc2806500dc9fd292c286f7a3c680950dc2b",
     "Maple Leaf Rag (1899)", "Scott Joplin",
     "United States Marine Band, recorded October 1906",
     "Public domain (PD-USGov-Military-Marine Corps; PD-old)",
     0.2, 12.0, 11025, 8, 1),
    ("GYMNO.WAV", "File:Gymnopedie No. 1..ogg",
     "transcoded/b/b7/Gymnopedie_No._1..ogg/Gymnopedie_No._1..ogg.mp3",
     "9f122b073b42841aaf76966aa1673c468cfe0bdf2d3c8124fd653994a98b22a9",
     "Gymnopedie No. 1 (1888)", "Erik Satie",
     "Teknopazzo (Wikimedia Commons user)",
     "CC0 1.0, by the performer",
     0.2, 12.0, 11025, 8, 1),
    ("MARCH.WAV",
     "File:The Stars and Stripes Forever (2002) - United States Air Force "
     "Band of Mid-America.mp3",
     "3/35/The_Stars_and_Stripes_Forever_%282002%29_-_United_States_Air_"
     "Force_Band_of_Mid-America.mp3",
     "61771e99a5e086c84914c02606acbe6917281862efa690b0adf12f337fb8731c",
     "The Stars and Stripes Forever (1896)", "John Philip Sousa",
     "United States Air Force Band of Mid-America, 2002",
     "Public domain (PD-USGov-Military-Air Force; composition PD-old)",
     0.5, 24.0, 11025, 8, 1),
    ("NOCTURNE.WAV", "File:Chopin Nocturne No. 2 in E Flat Major, Op. 9.ogg",
     "transcoded/0/04/Chopin_Nocturne_No._2_in_E_Flat_Major%2C_Op._9.ogg/"
     "Chopin_Nocturne_No._2_in_E_Flat_Major%2C_Op._9.ogg.mp3",
     "f65c98447a4212afe77878771e5279f230cfe74173139721dd0fe412982058f2",
     "Nocturne in E-flat major, Op. 9 No. 2", "Frederic Chopin",
     "Peter Johnston, for Musopen",
     "CC0 1.0",
     2.8, 8.0, 22050, 8, 1),
    ("PRELUDE.WAV",
     "File:Kimiko Ishizaka - Bach - Well-Tempered Clavier, Book 1 - 01 "
     "Prelude No. 1 in C major, BWV 846.ogg",
     "transcoded/b/b6/Kimiko_Ishizaka_-_Bach_-_Well-Tempered_Clavier%2C_"
     "Book_1_-_01_Prelude_No._1_in_C_major%2C_BWV_846.ogg/Kimiko_Ishizaka_-"
     "_Bach_-_Well-Tempered_Clavier%2C_Book_1_-_01_Prelude_No._1_in_C_"
     "major%2C_BWV_846.ogg.mp3",
     "76b8e5374c9195d3e5dfe90fdeec232e0ce5176c6506735038d84ce1d8d3aab8",
     "Prelude No. 1 in C major, BWV 846 (Well-Tempered Clavier, Book 1)",
     "Johann Sebastian Bach",
     "Kimiko Ishizaka, the Open Well-Tempered Clavier, 2015",
     "CC0 1.0",
     1.6, 3.0, 11025, 16, 2),
]
NAMES = [c[0] for c in CLIPS]


def stem(name):
    return name.split(".")[0]


def fetch():
    os.makedirs(SRC, exist_ok=True)
    for c in CLIPS:
        dst = os.path.join(SRC, stem(c[0]) + ".mp3")
        if not (os.path.exists(dst) and
                hashlib.sha256(open(dst, "rb").read()).hexdigest() == c[3]):
            req = urllib.request.Request(UP + c[2], headers={"User-Agent": UA})
            data = urllib.request.urlopen(req, timeout=120).read()
            got = hashlib.sha256(data).hexdigest()
            if got != c[3]:
                sys.exit("os88wrdemo: %s: SHA-256 %s, pinned %s - Commons "
                         "served different bytes; read the page again "
                         "before re-pinning" % (c[0], got, c[3]))
            open(dst, "wb").write(data)
        print("os88wrdemo: %s ok" % os.path.relpath(dst, ROOT))


def lowpass_decimate(x, step):
    """x (float, 1-D) at 44,100 Hz -> every STEP-th sample after a
    windowed-sinc low-pass at 0.45 of the new Nyquist band"""
    import numpy as np
    if step == 1:
        return x
    n = 127
    fc = 0.45 / step                    # cycles a sample of the input
    t = np.arange(n) - (n - 1) / 2
    h = 2 * fc * np.sinc(2 * fc * t) * np.blackman(n)
    h /= h.sum()
    return np.convolve(x, h, mode="same")[::step]


def render(c):
    import numpy as np
    name, _, _, _, _, _, _, _, start, secs, rate, bits, ch = c
    mp3 = os.path.join(SRC, stem(name) + ".mp3")
    dec = os.path.join(SRC, stem(name) + ".dec.wav")
    if not os.path.exists(dec):
        subprocess.run(["lame", "--quiet", "--decode", mp3, dec], check=True)
    w = wave.open(dec)
    if (w.getframerate(), w.getsampwidth()) != (44100, 2):
        sys.exit("os88wrdemo: %s decodes to %d Hz %d-bit, not 44,100 16-bit"
                 % (name, w.getframerate(), 8 * w.getsampwidth()))
    sch = w.getnchannels()
    a = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    a = a.reshape(-1, sch).astype(np.float64) / 32768.0
    a = a[int(start * 44100):int((start + secs) * 44100) + 256]
    if ch == 1:
        a = a.mean(axis=1, keepdims=True)
    elif sch == 1:
        a = np.repeat(a, 2, axis=1)
    step = 44100 // rate
    y = np.stack([lowpass_decimate(a[:, k], step) for k in range(ch)], 1)
    y = y[:int(secs * rate)]
    fi, fo = int(0.02 * rate), int(0.3 * rate)
    env = np.ones(len(y))
    env[:fi] = np.linspace(0, 1, fi)
    env[-fo:] = np.linspace(1, 0, fo)
    y *= env[:, None]
    y *= 0.9 / np.abs(y).max()
    if bits == 8:
        rng = np.random.default_rng(int(hashlib.sha256(name.encode())
                                        .hexdigest()[:8], 16))
        d = (rng.random(y.shape) - rng.random(y.shape)) / 127.0
        q = np.clip(np.round((y + d) * 127.0) + 128, 0, 255).astype(np.uint8)
        raw = q.tobytes()
    else:
        raw = np.clip(np.round(y * 32767.0), -32768, 32767) \
            .astype("<i2").tobytes()
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, name)
    with wave.open(out, "wb") as o:
        o.setnchannels(ch)
        o.setsampwidth(bits // 8)
        o.setframerate(rate)
        o.writeframes(raw)
    return out


def fmt(c):
    return "%s Hz, %d-bit %s" % (format(c[10], ","), c[11],
                                 "mono" if c[12] == 1 else "stereo")


def credits():
    """CREDITS.TXT, in Note Pad's shape (tools/checkreadme.py): headings in
    capitals within 28 columns, everything else prose that re-flows"""
    L = ["WAVERACK DEMO CLIPS", "",
         "Every clip is a short excerpt of a recording on Wikimedia Commons "
         "whose file page says Public Domain or CC0. Each page was read on "
         "%s before its recording was taken. The excerpts were cut, mixed, "
         "resampled and normalised by tools/os88wrdemo.py in the os8088 "
         "source tree, from the MP3 Commons serves of each file." % READ, ""]
    for c in CLIPS:
        name, page, _, _, work, comp, perf, lic, start, secs = c[:10]
        L += [name,
              "Work: %s, by %s." % (work, comp),
              "Performer: %s." % perf,
              "Licence: %s." % lic,
              "Excerpt: %.1f s from %.1f s, as %s." % (secs, start, fmt(c)),
              "Source: " + PAGE + page.replace(" ", "_"), ""]
    return text(L)


def text(lines):
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


def clips_txt(names):
    have = [c for c in CLIPS if c[0] in names]
    rest = [c for c in CLIPS if c[0] not in names]
    L = ["WAVERACK CLIPS", "",
         "The demo clips on this disk are in MEDIA. Open them from "
         "WAVERACK.O88 with File > Open.", ""]
    L += ["%s: %s, %s." % (c[0], c[5], fmt(c)) for c in have]
    if rest:
        L += ["", "NOT ON THIS DISK", "",
              "The 1.44MB WaveRack disk carries every clip; this one leaves "
              "room to record a take.", ""]
        L += ["%s: %s, %s." % (c[0], c[5], fmt(c)) for c in rest]
    L += ["", "Where each recording comes from, and its licence, is in "
          "CREDITS.TXT."]
    return text(L)


def selfcheck():
    bad = 0
    for c in CLIPS:
        p = os.path.join(OUT, c[0])
        if not os.path.exists(p):
            print("os88wrdemo: %s is not committed" % c[0])
            bad += 1
            continue
        w = wave.open(p)
        got = (w.getframerate(), 8 * w.getsampwidth(), w.getnchannels(),
               w.getnframes())
        want = (c[10], c[11], c[12], int(c[9] * c[10]))
        if got != want:
            print("os88wrdemo: %s is %r, the table says %r" % (c[0], got, want))
            bad += 1
    cr = os.path.join(OUT, "CREDITS.TXT")
    if not os.path.exists(cr) or open(cr, "rb").read() != credits():
        print("os88wrdemo: CREDITS.TXT is not what the table writes")
        bad += 1
    print("os88wrdemo: selfcheck %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    if argv[0] == "--selfcheck":
        return selfcheck()
    if argv[0] == "fetch":
        fetch()
    elif argv[0] == "make":
        for c in CLIPS:
            out = render(c)
            print("os88wrdemo: %s %s, %d bytes" % (
                os.path.relpath(out, ROOT), fmt(c), os.path.getsize(out)))
        open(os.path.join(OUT, "CREDITS.TXT"), "wb").write(credits())
    elif argv[0] == "clips":
        bad = [n for n in argv[2:] if n not in NAMES]
        if bad:
            sys.exit("os88wrdemo: no clip %s" % ", ".join(bad))
        open(argv[1], "wb").write(clips_txt(argv[2:]))
    else:
        sys.exit("os88wrdemo: fetch, make, clips or --selfcheck")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
