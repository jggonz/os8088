#!/usr/bin/env python3
"""Can the PAGE be clicked? Links, a form field and the submit button.

    make && make browsertest && python3 tests/brclick.py [--adapter cga|herc]

Everything here was reachable by keyboard alone before, and on a real page
nobody presses Tab first: they click the box, type into a field that does not
have focus, and the keystrokes scroll the page instead. Three assertions.

1. LINKS PARSE AND UNDERLINE. tests/htm/demo.htm carries one of each shape the
   resolver has to tell apart - an in-page anchor, an https link, a relative
   one and an absolute one - so the hrefs coming back in order is also a check
   that none of them was swallowed or merged.

2. A CLICK ON A LINK FOLLOWS IT. The relative link is the one driven, because
   this page came off a FLOPPY and so has no server: the honest answer is a
   refusal naming that, and a browser that instead composed `http:///...` and
   let br_split reject it would report a bad ADDRESS about a good link.

3. A CLICK REACHES A FORM FIELD AND THE SUBMIT BUTTON. Both are ordinary
   document text - that is what makes them wrap and scroll with no painter of
   their own - so both are found by document OFFSET, and this drives the whole
   path: click the field, type, click the button, read the composed query.

The positions are computed from the app's OWN line table and document rather
than from a screenshot: a click that lands one cell off is the failure this
test exists to catch, so it must not be aimed by eye.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402

S = os88sym.linear
u16 = lambda b: b[0] | (b[1] << 8)                     # noqa: E731
_IBMROM = os.path.exists("tools/martypc/roms/ibm5150")
MACHINE = ({"cga": "os8088_5150_cga", "herc": "os8088_5150_herc"} if _IBMROM
           else {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"})

WANT_HREFS = ["#http", "https://example.invalid/", "torture.htm",
              "http://frogfind.de/", "#"]


def say(*a):
    print(*a)
    sys.stdout.flush()


def browser_syms():
    """The package's own map, asserted to describe the shipped binary."""
    d = tempfile.mkdtemp()
    src, mp, out = (os.path.join(d, n) for n in ("b.asm", "b.map", "b.bin"))
    shutil.copy("apps/browser/browser.asm", src)
    with open(src, "a") as f:
        f.write("\n[map all %s]\n" % mp)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-I", "apps/browser/", "-I", "drivers/net/",
                    "-o", out, src], check=True)
    if open(out, "rb").read() != open("build/browser.bin", "rb").read():
        sys.exit("brclick: the mapped build is not build/browser.bin - every "
                 "offset it names would be plausible and wrong")
    syms = {}
    for line in open(mp):
        m = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()

    sy, fails = browser_syms(), []
    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "DEMO.HTM")
        w = dispcp.win_list(m, S)[-1]
        rec = m.read(S("wm_wins") + w * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        rb = lambda n: m.readseg(pseg, sy[n], 1)[0]        # noqa: E731
        rw = lambda n: u16(m.readseg(pseg, sy[n], 2))      # noqa: E731

        # --- 0: the bar OPENS holding `http://`, focused, caret at the end
        # (SPEC.md 71.7). It is read before anything is clicked, because the
        # very next assertion below is that a page click takes the focus away.
        LN_LEN, LN_CAR, LN_FOCUS = 12, 14, 18
        bar = bytes(m.readseg(pseg, sy["br_ubuf"], 16)).split(b"\0")[0]
        blen = u16(m.readseg(pseg, sy["br_loc"] + LN_LEN, 2))
        bcar = u16(m.readseg(pseg, sy["br_loc"] + LN_CAR, 2))
        bfoc = m.readseg(pseg, sy["br_loc"] + LN_FOCUS, 1)[0]
        say("bar opens %r len %d caret %d focus %d"
            % (bar.decode("latin-1"), blen, bcar, bfoc))
        if bar != b"http://":
            fails.append("the bar opens holding %r, not 'http://'" % bar)
        if not bfoc:
            fails.append("the bar opens WITHOUT focus, so the first keystroke "
                         "goes to the page")
        if bcar != len(b"http://") or blen != len(b"http://"):
            fails.append("the caret is at %d of %d and should be at the end "
                         "of the scheme" % (bcar, blen))

        # --- 1: the link table ------------------------------------------
        n = rw("br_lnkn")
        lseg = rw("br_lnkseg")
        say("links: %d, arena %04X" % (n, lseg))
        if not lseg:
            sys.exit("brclick: no link arena was claimed - nothing below can "
                     "be measured")
        arena = bytes(m.readseg(lseg, 0, max(rw("br_lnkw"), 1)))
        offs = [u16(m.readseg(pseg, sy["br_lnkoff"] + i * 2, 2))
                for i in range(n)]
        hrefs = [arena[o:arena.index(b"\0", o)].decode("latin-1") for o in offs]
        say("hrefs: %r" % hrefs)
        if hrefs != WANT_HREFS:
            fails.append("hrefs %r != %r" % (hrefs, WANT_HREFS))

        # --- the app's own line table, for aiming ------------------------
        docseg = rw("br_docseg")
        nl = rw("br_nlines")
        tab = bytes(m.readseg(rw("br_lseg"), 0, nl * 6))

        def line_of(off):
            for i in range(nl):
                o, e = u16(tab[i * 6:i * 6 + 2]), u16(tab[i * 6 + 2:i * 6 + 4])
                if o <= off < e:
                    return i
            return None

        def column_of(line, off):
            """br_build's walk, in Python: markers are not cells."""
            o = u16(tab[line * 6:line * 6 + 2])
            e = u16(tab[line * 6 + 2:line * 6 + 4])
            col = tab[line * 6 + 4]
            doc = bytes(m.readseg(docseg, o, e - o))
            i = 0
            while i < len(doc):
                if o + i == off:
                    return col
                ch = doc[i]
                if ch == 14:            # D_LNK1 carries two payload bytes
                    i += 3
                    continue
                i += 1
                if ch < 0x20:
                    continue
                col += 1
            return None

        def goto(line):
            """Scroll until `line` is in the band.

            **THE ARROWS ARE AIMED FROM THE APP'S OWN br_sb* RECT**, not from
            the window's corners. Guessing them cost this test a run: the band
            starts BR_LTOT below the content top, so a click 30px into the
            window lands in the location bar and the up arrow is never hit -
            which reads exactly like a browser that will not scroll up.
            """
            for _ in range(400):
                top, rows = rw("br_top"), rw("br_rows")
                if top <= line < top + rows:
                    return True
                before = rw("br_top")
                sx = rw("br_sbx") + 7
                if line < top:
                    mo.click(sx, rw("br_sby") + 5)          # up arrow cell
                else:
                    mo.click(sx, rw("br_sby2") - 5)         # ...and down
                if rw("br_top") == before:
                    return False        # clamped at an end: it cannot be shown
            return False

        def click_offset(off):
            ln = line_of(off)
            if ln is None:
                return "offset %d is on no display line" % off
            if not goto(ln):
                return "could not scroll display line %d into view" % ln
            col = column_of(ln, off)
            if col is None:
                return "offset %d is in no cell of line %d" % (off, ln)
            os88marty.settle(m)
            px = rw("br_cx") + col * 8 + 3
            py = rw("br_cy") + (ln - rw("br_top")) * 8 + 3
            mo.click(px, py)
            os88marty.settle(m)
            return None

        # --- 2: click the RELATIVE link ---------------------------------
        # find where it sits in the document: the byte after its D_LNK1
        doc = bytes(m.readseg(docseg, 0, rw("br_doclen")))
        want = 2                                    # 'torture.htm', index 2
        marker = bytes([14, 0x10 | (want & 15), 0x10 | (want >> 4)])
        at = doc.find(marker)
        if at < 0:
            fails.append("link %d's marker is not in the document" % want)
        else:
            err = click_offset(at + 3)
            if err:
                fails.append("relative link: " + err)
            else:
                st = rb("br_nstate")
                msg = rw("br_nmsg")
                say("after clicking the relative link: state %d, msg %04X"
                    % (st, msg))
                if st != 7:                         # BN_ERR
                    fails.append("a relative link on a page with no server "
                                 "should refuse out loud; state is %d" % st)
                elif msg != sy["br_s_nohost"]:
                    fails.append("it refused, but not with the no-server "
                                 "reason (%04X vs %04X) - any old error here "
                                 "would pass a state check"
                                 % (msg, sy["br_s_nohost"]))

        # --- 2b: THAT PAGE CLICK TOOK THE FOCUS AWAY (SPEC.md 71.7) ------
        # A caret drawn in a control the keyboard is no longer going to is
        # the whole of the bug; LN_FOCUS is what draws it.
        bfoc = m.readseg(pseg, sy["br_loc"] + LN_FOCUS, 1)[0]
        say("bar focus after a click on the page: %d" % bfoc)
        if bfoc:
            fails.append("a click on the page left the location bar focused, "
                         "so it is still showing a caret")

        # --- 2c: A COALESCED SCROLL STILL DRAWS (SPEC.md 71.9) -----------
        # br_scroll_by skips the redraw while more events are queued, which is
        # only safe because the skip is BOUNDED - so the assertion is that the
        # glass catches up. [br_ptop] is what the screen shows and [br_top] is
        # where the view is; the two part during a burst and must not stay
        # parted. goto() above has just driven a burst of arrow clicks.
        os88marty.settle(m)
        top, ptop = rw("br_top"), rw("br_ptop")
        say("after the scrolling: br_top %d, br_ptop %d" % (top, ptop))
        if top != ptop:
            fails.append("the view is at line %d and the screen is drawn for "
                         "%d - a coalesced scroll never resolved" % (top, ptop))

        # --- 3: the form field, then the submit button -------------------
        fofs = u16(m.readseg(pseg, sy["br_flds"], 2))
        sub0, sub1 = rw("br_subofs"), rw("br_subend")
        say("field @%d, submit %d..%d" % (fofs, sub0, sub1))
        err = click_offset(fofs)
        if err:
            fails.append("field: " + err)
        else:
            say("focus after the click: %02X" % rb("br_fcur"))
            if rb("br_fcur") != 0:
                fails.append("clicking the field did not focus it (%02X)"
                             % rb("br_fcur"))
            m.type_text("hi")           # ...through the real 8255 and int 09h
            os88marty.settle(m)
            typed = bytes(m.readseg(docseg, fofs, 4)).decode("latin-1")
            say("the field holds %r" % typed)
            if not typed.startswith("hi"):
                fails.append("typing after a click did not reach the field: %r"
                             % typed)
            # --- 3b: BACKSPACE LEAVES NO TRAIL (SPEC.md 71.7) ------------
            # The field's caret is a document byte, so moving it left means
            # blanking where it was - and nothing did, so the field filled up
            # with '_', one per backspace. Exactly one may be present.
            m.type_text("\b")
            os88marty.settle(m)
            after = bytes(m.readseg(docseg, fofs, 8))
            say("after one backspace the field holds %r"
                % after.decode("latin-1"))
            if after.count(b"_") != 1:
                fails.append("backspace left %d carets in the field (%r) - "
                             "the vacated cell was not blanked"
                             % (after.count(b"_"), after))
            if not after.startswith(b"h_"):
                fails.append("backspace did not remove the character: %r"
                             % after)
            m.type_text("i")                        # ...put it back for 3
            os88marty.settle(m)
        err = click_offset(sub0 + 2)                # inside '< Submit >'
        if err:
            fails.append("submit: " + err)
        else:
            url = bytes(m.readseg(pseg, sy["br_url"], 120))
            url = url.split(b"\0")[0].decode("latin-1")
            say("composed query: %r" % url)
            if "q=hi" not in url:
                fails.append("clicking Submit did not compose the query: %r"
                             % url)
        if a.shot:
            wpx, hpx, data = m.fbuf()
            os88marty.write_png_rgb(a.shot, wpx, hpx, data)
            say("wrote %s" % a.shot)

    for f in fails:
        say("FAIL: " + f)
    say("brclick: " + ("ok" if not fails else "FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
