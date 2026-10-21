#!/usr/bin/env python3
"""What WireChat's window COSTS TO DRAW, a gesture at a time (SPEC.md 113.5.1).

    python3 tests/wcdraw.py [--vga|--cga] [--theme N] [--shot DIR] [--json OUT]

tests/mrdraw.py's instrument (SPEC.md 105.9.4), pointed at WireChat. A redraw
is priced by the primitive calls it makes (PERFORMANCE.md's first sentence),
so this counts every far call the package makes into a DRAWING cell of the
API table - armed on the cell itself, so the kernel's own painting (a menu,
the title bar) is never counted - and names the package routine that made
each one off its far return address.

MartyPC has no network card, so the window is FILLED BY INJECTION
(tests/wcprobe.py): a reply SPEC.md 113.2.3 would send is written into
`wc_resp` and handed to the UI task as the worker would hand it. Every
gesture below is then one the user makes, or one the server makes - a
message arriving, a roster changing.

The default machine is MartyPC's 4.77 MHz 5150 with Hercules; --vga is the
XT with a VGA (the Blue theme, picked from the Theme menu: the default is
Wired, SPEC.md 113.5.2), --cga the 5150's CGA. All
three take the 360KB system disk and network360.img, whose root carries
WIRECHAT.O88.

TWO ASSERTIONS, mrdraw's:

  ceilings  each gesture's calls under a number a FULL repaint (~400 on a
            VGA window) or a whole-pane pass would blow straight through:
            SPEC.md 113.5.1 promises a change draws what it changed
  identity  after the run of gestures the content is captured, a FULL
            repaint is forced (the status popup up and down) and captured
            again, and the two must be IDENTICAL: the caches are only sound
            if drawing through them arrives at the picture a paint from
            nothing does

--theme N picks the Theme menu's item N first (wirechat.asm's TH_*); the VGA
leg's default is Blue. --vga --theme 0 is Wired on a colour display: its
LEDs one OSAPI_GFX_BLITP each and its emoticons spans of their own colour,
under the same ceilings and the same identity.

--shot DIR writes the content (and the whole screen) as PNGs after the
injection, for looking at the theme the machine draws.
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty as M          # noqa: E402
import os88mouse               # noqa: E402
import os88ui                  # noqa: E402
import wcprobe                 # noqa: E402

IMG, APPS, PATH = ("build/os8088-360.img", "build/network360.img",
                   "B:/WIRECHAT.O88")
MACHINES = {"herc": "os8088_5150_herc_gla", "vga": "os8088_xt_vga",
            "cga": "os8088_5150_cga_gla"}
LK_NODRV, LK_LIVE = 1, 3
TH_BLUE = 2                    # wirechat.asm's TH_*: the Theme menu's index

# THE CEILINGS: drawing calls a gesture may make. Each is a few times what the
# gesture draws through the caches and a fraction of a full paint. The two
# replies below change a room's unread BADGE, and a badge that comes, goes or
# changes width moves the tabs' spans, so that band is drawn whole - ~25
# calls of the terminal layout's (SPEC.md 113.5): an air pair per ground and
# a run per span, the view's highlight and the badges being grounds of their
# own. The rest of each gesture is what it was
CEIL = {"a message arrives": 70, "a member goes Away": 45,
        "a key in the field": 12, "Down: the pane a line": 60,
        "PgUp: the pane a page": 160, "Status > Busy": 20,
        "Notifications: a press": 16, "idle 5 s": 0}

# x=1 (SPEC.md 113.2.1) is what a current client asks for, and what a server
# that knows it answers: the date ahead of every hhmm. --old sends the reply
# a server from before it sends.
REPLY = """OK 120 {now} 900
C 12 3 0 1
L 0 lobby The os8088 community
L 1 hardware Real iron, real problems
L 2 development Building os8088 itself
L 3 games Play, and talk about playing
J abcdefabcdef George
M 90 0 {d1}1402 Zed Is anyone running this on a Tandy 1000?
M 91 0 {d1}1405 Alin Not a Tandy, but a Book8088 with the CGA card works.
M 92 0 {d1}1411 Zed Good to know. The Tandy video is close enough to CGA I think.
M 93 0 {d1}1430 Daniel The Wire catalog has three new games since yesterday.
M 94 0 {d1}1431 Alin Excitebike is the one to try. It holds 60 frames a second on a 286.
M 95 0 {d1}1502 RetroAmy Has anyone got a serial mouse that works with the 5150?
M 96 0 {d1}1504 Zed A Logitech C7 does. Anything Microsoft-compatible at 1200 baud.
M 97 0 {d1}1520 Daniel I am going to try the FTP server on my XT tonight.
M 98 0 {d1}1521 Alin Use the NE1000 if you have one. It moves 15 KB a second.
M 99 0 {d1}1540 Zed WireChat on a Hercules card looks wonderful, all 720 columns.
M 100 0 {d1}1552 Daniel See you all tomorrow :) <3
M 101 0 {d1}1555 RetroAmy Hello from a real IBM XT!
M 102 0 {d1}1556 George Nice! Which video card? ^_^
M 103 0 {d2}0958 RetroAmy A Paradise VGA. It is slow at 4.77 MHz, but it works, and the dither on the wallpaper is lovely.
M 104 0 {d2}1601 Daniel Just joined from my 8088. @George the new build boots fine here.
D 105 {d2}1559 Alin George are you around later?
R 7 5
P Alin 1 0
P Daniel 0 1
P George 0 0
P RetroAmy 0 0
P Zed 0 2
"""
MORE = """OK 121 {now} 900
C 13 3 1 1
M 106 0 {d2}1602 Zed Anyone tried the Covox build on a real 5150?
{ment}"""
AWAY = """OK 121 {now} 900
C 13 3 0 1
R 8 5
P Alin 1 0
P Daniel 0 1
P George 0 0
P RetroAmy 0 1
P Zed 0 2
"""
# "blit1" is every run of text since the window's own face (SPEC.md
# 113.5.3): a run is composed and put down by OSAPI_GFX_BLIT1, so a ceiling
# that did not count it would pass by measuring nothing. Its pen (0x393) is
# a setting and not a drawing call, and is not here.
CELLS = {"pixel": 0x20, "hline": 0x28, "vline": 0x30, "fill": 0x38,
         "frame": 0x40, "gray": 0x48, "xor": 0x58, "blit4": 0x182,
         "blit1": 0x320, "text": 0x1E5, "textx": 0x68, "icon": 0x39D,
         "blitp": 0x3A4, "scroll": 0x19C}

SYMS = []


def body(t, old):
    d1, d2 = ("", "") if old else ("20261019 ", "20261020 ")
    # an A only from a server that knows x=1 (SPEC.md 113.2.4): Alin names
    # us in #development, which is not the view
    ment = "" if old else "A 107 2 20261020 1602 Alin\n"
    return t.format(now="1603", d1=d1, d2=d2, ment=ment)


def cell_addrs():
    return {M.KERNEL_SEG * 16 + off: name for name, off in CELLS.items()}


def caller(m, rec):
    """(SS, SP, the return IP, the return CS): a far call's return address is
    on top of the stack when the cell is reached."""
    r = rec.get("regs") or {}
    ss, sp = r.get("ss", 0), r.get("sp", 0)
    ret = bytes(m.read(ss * 16 + sp, 4))
    return ss, sp, int.from_bytes(ret[:2], "little"), \
        int.from_bytes(ret[2:], "little")


def nearest(ip):
    best = None
    for name, off in SYMS:
        if off <= ip:
            best = name
        else:
            break
    return best or "?"


def measure(ui, p, what, act, settle):
    """The package's calls alone: a hit whose far return is not into the
    package's own segment is the KERNEL calling its own cell (the planar
    blit's clip walk re-enters 0x3A4 once a fragment), not a call we made."""
    addrs = cell_addrs()
    with M.bp_trace(ui.m, *addrs.keys(), cap=50000, regs=True,
                    on_hit=caller) as tr:
        act()
        M.guest_sleep(ui.m, settle)
    by, fn, n_ = {}, {}, 0
    for h in tr.hits:
        ss, sp, ip, cs = h.get("hit") or (0, 0, 0, 0)
        if cs != p.seg:
            continue
        n_ += 1
        n = addrs.get(h["addr"], "?")
        by[n] = by.get(n, 0) + 1
        f = nearest(ip)
        fn[f] = fn.get(f, 0) + 1
    row = {"gesture": what, "calls": n_, "by": by, "callers": fn}
    print("%-26s %4d  %s" % (what, n_, " ".join(
        "%s %d" % kv for kv in sorted(by.items()))), flush=True)
    if fn:
        print("      " + ", ".join("%s %d" % kv for kv in sorted(
            fn.items(), key=lambda kv: -kv[1])[:10]), flush=True)
    return row


def content(ui, p):
    """The content as the card shows it. The pointer is parked first, on the
    desktop below the window, so the arrow is never part of a capture."""
    w, h, px = ui.m.fbuf()
    x0, y0, x1, y1 = ui.window("WireChat").content
    print("content at %d,%d (wc_ox %d, wc_oy %d), fbuf %dx%d"
          % (x0, y0, p.w("wc_ox"), p.w("wc_oy"), w, h), flush=True)
    cw, ch = x1 - x0 + 1, y1 - y0 + 1
    out = bytearray()
    for y in range(y0, y0 + ch):
        out += px[(y * w + x0) * 3:(y * w + x0 + cw) * 3]
    return cw, ch, bytes(out)


def park(mo, ui):
    w, h, _ = ui.m.fbuf()
    mo.to(w - 4, 4)
    M.guest_sleep(ui.m, 0.5)


def shot(ui, p, d, name):
    os.makedirs(d, exist_ok=True)
    cw, ch, px = content(ui, p)
    M.write_png_rgb(os.path.join(d, name + "-content.png"), cw, ch, px)
    w, h, full = ui.m.fbuf()
    M.write_png_rgb(os.path.join(d, name + "-screen.png"), w, h, full)
    print("shot: %s/%s-*.png (%dx%d content)" % (d, name, cw, ch), flush=True)


def notify_xy(p):
    """The status bar's `Notifications:`, a press on which toggles them and
    saves the settings - and a save asks for a WAKE (wc_wantsave), which is
    how an injected reply reaches the UI task with no worker behind it."""
    return (p.w("wc_ox") + p.w("wc_cw") - 6 * 8,
            p.w("wc_oy") + p.w("wc_sy") + 6)


def deliver(mo, ui, p, text, hist=False):
    """The reply, handed over by a press on Notifications: so a gesture's
    count includes the status bar's own redraw - one band."""
    wcprobe.inject(p, text, hist)
    mo.click(*notify_xy(p))
    M.until(ui.m, lambda _: p.b("wc_wake") == 0, "the reply taken", poll=.1,
            limit=20)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--shot")
    ap.add_argument("--theme", type=int)
    ap.add_argument("--old", action="store_true",
                    help="inject replies in the format before x=1")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--vga", action="store_true")
    g.add_argument("--cga", action="store_true")
    a = ap.parse_args()
    kind = "vga" if a.vga else "cga" if a.cga else "herc"
    rows, ok = [], True
    with os88ui.boot(IMG, apps=APPS, machine=MACHINES[kind]) as ui:
        ui.path(PATH)
        p = wcprobe.attach(ui)
        M.until(ui.m, lambda _: p.b("wc_cfgld") == 1, "the settings read",
                poll=.2, limit=60)
        ui.m.type_text("George\n")
        M.until(ui.m, lambda _: p.b("wc_link") == LK_NODRV, "no driver",
                poll=.2, limit=60)
        p.put("wc_frs", b"Alin\0")
        p.put("wc_fn", b"\x01")
        mo = os88mouse.Mouse(marty=ui.m)
        deliver(mo, ui, p, body(REPLY, a.old), hist=True)
        M.until(ui.m, lambda _: p.b("wc_link") == LK_LIVE, "live", poll=.2,
                limit=20)
        M.guest_sleep(ui.m, 2.0)
        th = a.theme
        if th is None and kind == "vga":
            # a colour theme, so the badges, lamps and nicks are each their
            # own ground and ink (the default, Wired, is black on white)
            th = TH_BLUE
        if th is not None:
            ui.menu_pick("Theme", th)
            M.until(ui.m, lambda _: p.b("wc_thm") == th, "theme %d" % th,
                    poll=.2, limit=20)
            M.guest_sleep(ui.m, 1.0)
        park(mo, ui)
        if a.shot:
            shot(ui, p, a.shot, kind)
        global SYMS
        SYMS = sorted(((k, v) for k, v in p.s.items()
                       if "." not in k and v < 0x8000), key=lambda kv: kv[1])
        key = ui.m.key
        rows.append(measure(ui, p, "idle 5 s", lambda: None, 5.0))
        rows.append(measure(ui, p, "a message arrives",
                            lambda: deliver(mo, ui, p, body(MORE, a.old)),
                            1.5))
        rows.append(measure(ui, p, "a member goes Away",
                            lambda: deliver(mo, ui, p, body(AWAY, a.old)),
                            1.5))
        rows.append(measure(ui, p, "a key in the field",
                            lambda: ui.m.type_text("h"), 1.0))
        print("top %d of %d, rows %d" % (p.w("wc_top"), p.w("wc_total"),
                                         p.w("wc_rows")), flush=True)
        rows.append(measure(ui, p, "PgUp: the pane a page",
                            lambda: key("PageUp"), 1.0))
        rows.append(measure(ui, p, "Down: the pane a line",
                            lambda: key("ArrowDown"), 1.0))
        print("top %d of %d, rows %d" % (p.w("wc_top"), p.w("wc_total"),
                                         p.w("wc_rows")), flush=True)
        rows.append(measure(ui, p, "Status > Busy",
                            lambda: ui.menu_pick("Status", "Busy"), 1.0))
        rows.append(measure(ui, p, "Notifications: a press",
                            lambda: mo.click(*notify_xy(p)), 1.0))
        for r in rows:
            c = CEIL.get(r["gesture"])
            if c is not None and r["calls"] > c:
                print("FAIL %s: %d drawing calls, ceiling %d"
                      % (r["gesture"], r["calls"], c), flush=True)
                ok = False
        # THE IDENTITY: the picture the caches drew, against a repaint
        park(mo, ui)
        before = content(ui, p)
        rect = [int.from_bytes(p.data("wc_brects", 8)[i:i + 2], "little")
                for i in (0, 2, 4, 6)]
        mo.click((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2)
        M.until(ui.m, lambda _: p.b("wc_pop") != 0, "the status popup",
                poll=.1, limit=10)
        key("Escape")
        M.until(ui.m, lambda _: p.b("wc_pop") == 0, "the popup down",
                poll=.1, limit=10)
        park(mo, ui)
        M.guest_sleep(ui.m, 1.5)
        after = content(ui, p)
        cw, ch, ap_ = before
        bad = [(i // 3 % cw, i // 3 // cw) for i in range(0, len(ap_), 3)
               if ap_[i:i + 3] != after[2][i:i + 3]]
        if bad:
            xs = [x for x, _ in bad]
            ys = [y for _, y in bad]
            print("FAIL identity: %d pixels differ between the incremental "
                  "picture and a repaint, in (%d,%d)-(%d,%d) of the content"
                  % (len(bad), min(xs), min(ys), max(xs), max(ys)),
                  flush=True)
            if a.shot:
                M.write_png_rgb(os.path.join(a.shot, kind + "-incr.png"),
                                cw, ch, ap_)
                M.write_png_rgb(os.path.join(a.shot, kind + "-repaint.png"),
                                cw, ch, after[2])
            ok = False
        else:
            print("PASS identity: %dx%d content, the incremental picture IS "
                  "the repaint's" % (cw, ch), flush=True)
        if a.shot:
            shot(ui, p, a.shot, kind + "-end")
    print("gestures: %d drawing calls" % sum(r["calls"] for r in rows))
    if a.json:
        json.dump(rows, open(a.json, "w"), indent=1)
    print("%s wcdraw" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
