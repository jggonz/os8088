#!/usr/bin/env python3
"""ptrbench - per-byte ISR and per-packet cursor-move cycles on MartyPC.

    python3 ptrbench.py <tree> <machine> [--packets N] [--dx 3 --dy 2]

Boots <tree>/build/os8088-360.img, settles the mouse contest, arms LOGGING
breakpoints (no stop, no round trip) at the serial ISR's doors, its iret, and
cur_move's entry and return, then sweeps the pointer with packets paced one
per ~26 ms (a 1200-baud mouse's own rate) and reports cycles per event.
"""
import argparse, json, os, sys, statistics

ap = argparse.ArgumentParser()
ap.add_argument("tree")
ap.add_argument("machine")
ap.add_argument("--packets", type=int, default=60)
ap.add_argument("--dx", type=int, default=3)
ap.add_argument("--dy", type=int, default=2)
ap.add_argument("--gap", type=int, default=125000, help="guest cycles per packet")
ap.add_argument("--json")
ap.add_argument("--img", default="build/os8088-360.img")
a = ap.parse_args()

tree = os.path.abspath(a.tree)
os.chdir(tree)
sys.path.insert(0, os.path.join(tree, "tools"))
import os88ui, os88marty, os88sym  # noqa

KSEG = 0x60


def mapsyms():
    out = {}
    for line in open("build/kernel.map"):
        p = line.split()
        if len(p) == 3:
            try:
                out[p[2]] = (int(p[0], 16), int(p[1], 16))
            except ValueError:
                pass
    return out


S = mapsyms()
full = open("build/kernel-full.bin", "rb").read()


def text_flat(name):
    real, virt = S[name]
    return KSEG * 16 + virt, real


def find_after(name, opbytes, limit=0x80):
    flat, real = text_flat(name)
    i = full.find(bytes(opbytes), real, real + limit)
    assert i >= 0, (name, opbytes)
    return flat + (i - real)


isr4, _ = text_flat("mou_isr")
isr3, _ = text_flat("mou_isr3")
iret = find_after("mou_eoi", [0xCF])
cmove, _ = text_flat("cur_move")
# the instruction after `call cur_move` in mou_apply.go: C6 06 <cur_dirty> 00
go, goreal = text_flat("mou_apply.go")
i = full.find(b"\xe8", goreal, goreal + 8)
cmret = go + (i - goreal) + 3
apply_, _ = text_flat("mou_apply")

with os88ui.boot(a.img, machine=a.machine, quick=True, verbose=False) as ui:
    m = ui.m
    # settle the contest and park the pointer mid-screen
    for _ in range(6):
        m.mouse(dx=1, dy=1)
        m.advance(cycles=a.gap)
    m.advance(cycles=2000000)
    bps = [{"type": "exec", "addr": x, "log": True}
           for x in (isr4, isr3, iret, cmove, cmret, apply_)]
    m.breakpoints(bps)
    m.cmd(cmd="bplog")
    c0 = int(m.status()["cycles"])
    sign = 1
    for k in range(a.packets):
        if k and k % 20 == 0:
            sign = -sign
        m.mouse(dx=sign * a.dx, dy=sign * a.dy)
        m.advance(cycles=a.gap)
    c1 = int(m.status()["cycles"])
    r = m.cmd(cmd="bplog")
    m.breakpoints([])
    hits = sorted(((int(c), int(ad)) for ad, c in r.get("hits", ())))
    dropped = r.get("dropped", 0)
    st = m.status()

names = {isr4: "isr4", isr3: "isr3", iret: "iret", cmove: "cmove",
         cmret: "cmret", apply_: "apply"}
isr, mv, pkt = [], [], []
open_isr = open_mv = None
had_apply = False
for c, ad in hits:
    n = names.get(ad & 0xFFFFF, "?")
    if n in ("isr4", "isr3"):
        open_isr, had_apply = c, False
    elif n == "apply":
        had_apply = True
    elif n == "iret" and open_isr is not None:
        (pkt if had_apply else isr).append(c - open_isr)
        open_isr = None
    elif n == "cmove":
        open_mv = c
    elif n == "cmret" and open_mv is not None:
        mv.append(c - open_mv)
        open_mv = None


def summ(xs):
    if not xs:
        return "none"
    return "n=%d median=%d min=%d max=%d mean=%.0f" % (
        len(xs), statistics.median(xs), min(xs), max(xs), statistics.mean(xs))


span = c1 - c0
tot_isr = sum(isr) + sum(pkt)
print("machine", a.machine, "tree", tree)
print("window cycles", span, "(%.1f ms)" % (span / 4772.727), "dropped", dropped)
print("isr byte (no packet):", summ(isr))
print("isr byte (packet)   :", summ(pkt))
print("cur_move            :", summ(mv))
print("ISR share of window : %.2f%%" % (100.0 * tot_isr / span))
res = dict(machine=a.machine, span=span, isr=isr, pkt=pkt, mv=mv)
if a.json:
    json.dump(res, open(a.json, "w"))
