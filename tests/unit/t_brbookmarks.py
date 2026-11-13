#!/usr/bin/env python3
"""The Browser's bookmark records in the URL store, as they ship.

    python3 tests/unit/t_brbookmarks.py

The bookmark file's image lives in the URL store (SPEC.md 71.23), a claim of
its own named by [br_urlseg], and the menu reads a copy of each title in the
segment. This loads build/browser.bin under unicorn with a store of three
records at its own segment and calls, as they ship:

- br_bmfindurl on each record's URL, in another case, and on a miss. It used
  to leave SI where the previous record's mismatch was, so only record 0 could
  be found: with two or more bookmarks Ctrl+D added a duplicate and the star
  could not remove the second. Every register but AX and the flags must come
  back as it went in;
- br_bmmenuset, which must copy each title into br_bmtitles and point
  br_bmitems at the copies (a menu item is a near pointer into the segment);
- br_bmurlget on each item, which must copy the record's URL into br_ubuf and
  refuse an item past the count;
- all three with no store at all ([br_urlseg] 0): a miss, the `(empty)` item,
  and a refusal, never a read of segment 0.
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

SEG = 0x1000                    # the Browser's segment
BASE = SEG << 4
STORE = 0x3000                  # the URL store's
RET = 0xFFF8
TITLE, URL, HEADER = 24, 160, 5
RECSZ = TITLE + URL

RECORDS = [
    ("Alpha", "http://a.example/x"),
    ("Beta page", "http://b.example/y"),
    ("Gamma", "http://c.example/z/long/path"),
]
FAILS = []


def symbols():
    out = {}
    with open(os88build.at("build/browser.map"), encoding="ascii") as f:
        for line in f:
            m = re.match(r"^\s*([0-9A-F]+)\s+(?:[0-9A-F]+\s+)?(\S+)\s*$", line)
            if m:
                out.setdefault(m.group(2), int(m.group(1), 16))
    return out


def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        FAILS.append(what)


def main():
    sym = symbols()
    image = open(os88build.at("build/browser.bin"), "rb").read()
    bm = sym["BR_URLBM"]
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(BASE, image)
    mu.mem_write(BASE + len(image), bytes(sym["br_parse_bss_end"] - len(image)))
    store = bytearray(sym["BR_URLSTORE"])
    rec = b"BM01" + bytes((len(RECORDS),))
    for title, url in RECORDS:
        rec += title.encode().ljust(TITLE, b"\0") + url.encode().ljust(URL, b"\0")
    store[bm:bm + len(rec)] = rec
    mu.mem_write(STORE << 4, bytes(store))
    # a byte pattern in segment 0's first KB: a read of it must not match
    mu.mem_write(0, bytes(range(256)) * 4)

    def setseg(v):
        mu.mem_write(BASE + sym["br_urlseg"], bytes((v & 255, v >> 8)))

    def call(name, regs=None):
        seed = {UC_X86_REG_AX: 0x1111, UC_X86_REG_BX: 0x2222,
                UC_X86_REG_CX: 0x3333, UC_X86_REG_DX: 0x4444,
                UC_X86_REG_SI: 0x5555, UC_X86_REG_DI: 0x6666,
                UC_X86_REG_BP: 0x7777}
        seed.update(regs or {})
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            mu.reg_write(r, SEG)
        for r, v in seed.items():
            mu.reg_write(r, v)
        mu.reg_write(UC_X86_REG_SP, 0xFFF0)
        mu.mem_write(BASE + 0xFFF0, bytes((RET & 255, RET >> 8)))
        mu.reg_write(UC_X86_REG_EFLAGS, 0x0002)
        mu.emu_start(BASE + sym[name], BASE + RET, count=500_000)
        out = {r: mu.reg_read(r) for r in seed}
        out["cf"] = bool(mu.reg_read(UC_X86_REG_EFLAGS) & 1)
        out["es"] = mu.reg_read(UC_X86_REG_ES)
        out["seed"] = seed
        return out

    query = sym["br_bmrequrl"]

    def find(url):
        mu.mem_write(BASE + query, url.encode() + b"\0")
        r = call("br_bmfindurl", {UC_X86_REG_SI: query})
        kept = [n for n, reg in (("BX", UC_X86_REG_BX), ("CX", UC_X86_REG_CX),
                                 ("DX", UC_X86_REG_DX), ("SI", UC_X86_REG_SI),
                                 ("DI", UC_X86_REG_DI), ("BP", UC_X86_REG_BP))
                if r[reg] != r["seed"][reg]]
        if r["es"] != SEG:
            kept.append("ES")
        return (None if r["cf"] else r[UC_X86_REG_AX]), kept

    setseg(STORE)
    for i, (_, url) in enumerate(RECORDS):
        got, clob = find(url)
        check(got == i and not clob,
              "br_bmfindurl %-30s -> record %d (got %s%s)"
              % (url, i, got, ", clobbered " + ",".join(clob) if clob else ""))
    got, _ = find(RECORDS[1][1].upper())
    check(got == 1, "br_bmfindurl ignores case -> record 1 (got %s)" % got)
    got, _ = find("http://b.example/")
    check(got is None, "br_bmfindurl on a prefix of record 1 is a miss (got %s)" % got)
    got, _ = find("http://nowhere.example/")
    check(got is None, "br_bmfindurl on an unknown URL is a miss (got %s)" % got)

    call("br_bmmenuset")
    titles, items = sym["br_bmtitles"], sym["br_bmitems"]
    nitem = sym["BR_BMNITEM"]
    n = mu.mem_read(BASE + nitem, 2)
    check(n[0] | n[1] << 8 == len(RECORDS),
          "br_bmmenuset sets %d menu items (got %d)" % (len(RECORDS), n[0] | n[1] << 8))
    for i, (title, _) in enumerate(RECORDS):
        p = mu.mem_read(BASE + items + 2 * i, 2)
        p = p[0] | p[1] << 8
        text = bytes(mu.mem_read(BASE + p, TITLE)).split(b"\0")[0].decode()
        check(p == titles + i * TITLE and text == title,
              "menu item %d points at its title copy %r (got %04X %r)"
              % (i, title, p, text))

    ubuf = sym["br_ubuf"]
    for i, (_, url) in enumerate(RECORDS):
        mu.mem_write(BASE + ubuf, b"\xAA" * URL)
        r = call("br_bmurlget", {UC_X86_REG_AX: i})
        got = bytes(mu.mem_read(BASE + ubuf, URL)).split(b"\0")[0].decode("latin-1")
        check(not r["cf"] and got == url and r["es"] == SEG,
              "br_bmurlget %d -> %s (got %r, CF=%d)" % (i, url, got, r["cf"]))
    r = call("br_bmurlget", {UC_X86_REG_AX: len(RECORDS)})
    check(r["cf"], "br_bmurlget past the count refuses")

    setseg(0)
    got, _ = find(RECORDS[0][1])
    check(got is None, "with no store, br_bmfindurl is a miss (got %s)" % got)
    call("br_bmmenuset")
    p = mu.mem_read(BASE + items, 2)
    n = mu.mem_read(BASE + nitem, 2)
    check((p[0] | p[1] << 8) == sym["br_it_bmnone"] and (n[0] | n[1] << 8) == 1,
          "with no store, the menu is the one `(empty)` item")
    r = call("br_bmurlget", {UC_X86_REG_AX: 0})
    check(r["cf"], "with no store, br_bmurlget refuses")

    total = 4 + 2 + 1 + len(RECORDS) + len(RECORDS) + 1 + 3
    print("t_brbookmarks: %d of %d as expected" % (total - len(FAILS), total))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
