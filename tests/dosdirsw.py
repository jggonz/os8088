#!/usr/bin/env python3
"""`DIR`'s SWITCHES, AND THE PAUSE THAT MAY NOT BLOCK (SPEC.md 96.33.9).

`dsh_c_dir` took a path and nothing else, so every switch was read as part of
the file name.  Reported from the field: *"dir doesn't have most of its common
command line args. Like /p"*.

**WHAT THE SWITCHES ARE WAS MEASURED**, off the IBM DOS 3.30 image and out of
`COMMAND.COM`'s own string table, and one of the answers is the reason this row
exists at all: **`/B` is NOT a DOS 3.3 switch** - the real thing answers
`Invalid parameter`, exactly as it does for `/Z` - so a box reporting 3.31 that
accepted it would be wrong in the direction nobody checks.

**AND `/P` MAY NOT WAIT FOR A KEY.**  `dos_con_key` is `W_ONKEY`'s handler and
its contract is the gfx lock HELD, so a built-in that blocked there would hold
it until a human pressed something: no pointer, no repaint, no other window -
the whole machine.  So the listing SUSPENDS, and the assertions below are about
that mechanism rather than about the text:

  1  an unknown switch is `Invalid parameter` and NOT a file name - `/Z`, and
     `/B` beside it, because that one is a judgement this row pins down.
  2  `/W` puts five names on a row and the FILE COUNT IS UNCHANGED, which is
     what says it is a layout and not a filter.
  3  `/P` STOPS mid-listing: `Strike a key when ready . . . ` is on the glass,
     `[dsh_more]` is 1, and THERE IS NO PROMPT UNDER IT - a machine asking two
     questions at once is the failure mode a suspend-instead-of-block design
     has.
  4  ...and a key RESUMES it to the same total the unpaged listing gave.  A
     resume that lost or repeated an entry would still look like a listing.
  5  Esc ABANDONS it, and the prompt comes back.

**CGA, and that is the point of the machine choice**: a page is `[con_vrows] -
1`, so 16 rows here against 24 on the adapters that fit all 25.  The system
disk's root has more entries than 16 and fewer than 24, so this is also the
only adapter in the tree where the pause fires at all on a stock floppy.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosconcga                                               # noqa: E402
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
# **B: IS A FIXTURE AND NOT A SHIPPED FLOPPY**: /P can only be
# tested against a directory with more VISIBLE entries than a page,
# and no shipped disk has one.  The system disk's root holds 21
# entries and DIR shows FIVE - sixteen are hidden or system, which
# DOS does not list and neither do we.  build/dirsw360.img is 24
# plain files: over a 16-line CGA page, under a 24-line one.
APPS = "build/dirsw360.img"
BOX = "A:/APPS/DOS.O88"
SUBPROG = "DOSHELLO"       # ...and a copy of it in BIN\\ (SPEC.md 96.33.13)
MACHINE = "os8088_5150_cga_gla"
STRIKE = "Strike a key when ready"


def fail(msg):
    print("dosdirsw: FAIL: %s" % msg)
    sys.exit(1)


class Box(object):
    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    @property
    def base(self):
        # **RESOLVED PER ACCESS, NOT CACHED** (SPEC.md 66.6.1.2): the DOS
        # box's region MOVES now - it is a re-homed carve and was pinned only
        # until that section - so a base banked in __init__ names the bytes
        # the package used to occupy, and decodes as plausible rubbish.
        return dosmap.instance(self.m) << 4

    def w(self, name):
        return int.from_bytes(self.m.read(self.base + self.dm[name], 2),
                              "little")

    def b(self, name):
        return self.m.read(self.base + self.dm[name], 1)[0]

    def rows(self):
        scr = self.m.read(self.base + self.dm["con_scr"], 80 * 25 * 2)
        out = []
        for r in range(25):
            row = scr[r * 160:(r + 1) * 160]
            out.append("".join(chr(row[i]) if 32 <= row[i] < 127 else " "
                               for i in range(0, 160, 2)).rstrip())
        return out

    def view(self):
        """Only the rows the band is SHOWING - 96.33.8's viewport."""
        vt, vr = self.w("con_vtop"), self.w("con_vrows")
        return [r for r in self.rows()[vt:vt + vr]]

    def live(self):
        return [r for r in self.view() if r.strip()]

    def type(self, s):
        self.m.type_text(s)
        os88marty.settle(self.m)

    def run(self, cmd):
        self.type(cmd + "\n")
        return self.live()


def count_of(lines):
    """The `N File(s)` footer's number, or None."""
    for r in lines:
        if "File(s)" in r:
            try:
                return int(r.split("File(s)")[0].strip())
            except ValueError:
                return None
    return None


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - a plain `make` builds it" % p)

    with os88ui.boot(SYS, apps=APPS, machine=MACHINE) as ui:
        m = ui.m
        if not ui.path(BOX):
            fail("could not launch %s" % BOX)
        os88marty.settle(m)
        bx = Box(m)
        # LEARN THE BAND'S RECTANGLE NOW, while it holds a banner and a prompt
        # and nothing else - see dosconcga.band_lit for why a full band cannot
        # be measured from scratch.
        rect = dosconcga.band_rect(m, bx)
        if rect is None:
            fail("could not find the console band on the glass at all")
        page = bx.w("con_vrows") - 1
        print("dosdirsw: the band is %d rows, so a page is %d lines"
              % (bx.w("con_vrows"), page))

        # --- 0: the plain listing, which everything else is measured against -
        bx.run("B:")
        plain = bx.run("DIR")
        total = count_of(plain)
        if total is None:
            fail("a plain DIR printed no `N File(s)` footer: %r" % plain[-4:])
        print("dosdirsw: a plain DIR of B:\\ lists %d file(s)" % total)

        # --- 0b: ...AND IT IS IN ALPHABETICAL ORDER (SPEC.md 96.33.9.2) -----
        # A stated departure from DOS, which lists in DIRECTORY order - the
        # owner's line being that the ABI is 3.3's and the screen is ours.
        # Before the sort this disk's listing ended `...DIGISND3, EDUNGEON,
        # EPALACE, TRACE.LOG`, TRACE.LOG having been written last.
        #
        # **THE ENTRY COUNT IS CHECKED FIRST**, because `[] == sorted([])` is
        # True and a row whose parser silently matches nothing reports SORTED
        # for ever. That is not hypothetical: the first version of this check
        # used a regex that matched none of the 80-column rows and passed.
        names = []
        for r in plain:
            t = r.rstrip()
            if len(t) < 13 or not t[0].isalnum():
                continue
            nm, ext = t[:8].rstrip(), t[9:12].rstrip()
            if not nm or " " in nm:
                continue
            names.append(nm + ("." + ext if ext else ""))
        if len(names) < total // 2:
            fail("the listing parser found %d entries of %d reported, so the "
                 "sortedness check below would be measuring nothing: %r"
                 % (len(names), total, plain[:4]))
        if names != sorted(names):
            bad = next(a for a, b in zip(names, sorted(names)) if a != b)
            fail("DIR is not in alphabetical order - %r arrives out of place "
                 "(SPEC.md 96.33.9.2). Got %r" % (bad, names[:12]))
        print("dosdirsw: ...and its %d entries are in alphabetical order"
              % len(names))

        if total <= page:
            fail("B:\\ holds %d entries and a page is %d, so /P can never "
                 "pause here and assertion 3 would pass vacuously" % (total, page))
        if not any("bytes free" in r for r in plain):
            fail("the footer has no `bytes free` in it: %r" % plain[-3:])

        # --- 1: a switch we have not got ------------------------------------
        for sw in ("/Z", "/B"):
            got = bx.run("DIR " + sw)
            if not any("Invalid parameter" in r for r in got[-3:]):
                fail("`DIR %s` should answer DOS 3.30's own `Invalid "
                     "parameter` and the band says %r%s"
                     % (sw, got[-3:],
                        " - /B is DOS 5's switch, measured refused on the real "
                        "3.30" if sw == "/B" else ""))
        print("dosdirsw: /Z and /B are both `Invalid parameter`, as 3.30 has it")

        # --- 2: /W is a LAYOUT and not a filter ------------------------------
        wide = bx.run("DIR /W")
        if count_of(wide) != total:
            fail("/W lists %s file(s) where the long form lists %d - it is a "
                 "layout, not a filter" % (count_of(wide), total))
        names = [r for r in wide if "File(s)" not in r and ">" not in r
                 and r.strip()]
        if not any(len(r.rstrip()) > 32 for r in names):
            fail("no /W row is wider than two columns, so the five-column "
                 "layout is not happening: %r" % names[-4:])
        print("dosdirsw: /W keeps the count at %d and packs the rows" % total)

        # --- 3: /P STOPS, and owes no prompt --------------------------------
        paged = bx.run("DIR /P")
        if not bx.b("dsh_more"):
            fail("`DIR /P` over %d entries with a %d-line page did not "
                 "suspend: [dsh_more] is 0" % (total, page))
        if not any(STRIKE in r for r in paged):
            fail("[dsh_more] is set but %r is not on the glass: %r"
                 % (STRIKE, paged[-3:]))
        if paged[-1].rstrip().endswith(">"):
            fail("a prompt was printed UNDER the pause (%r) - the box is "
                 "asking two questions at once" % paged[-1])
        # **AND THE PAGE IS ON THE GLASS** (96.33.9.1 item 1).  The band's
        # BUFFER is right whether or not anything painted, so this reads
        # PIXELS: the suspended arm of dos_con_run returned before
        # dos_con_draw for a release, the marks were spent by the next
        # keystroke, and the whole listing appeared at the end - a prompt
        # asking the user to read a page that is not there.
        lit, _ = dosconcga.band_lit(m, bx, rect)
        entries = [r for r in paged if "FILE" in r]
        if len(entries) < 4:
            fail("only %d entries are in view under the pause: a page is %d "
                 "lines and the listing should have filled it (%r)"
                 % (len(entries), page, paged[:4]))
        if lit < 200:
            fail("the pause is up but the band has %d lit pixels - the page "
                 "the user is being asked to read never reached the glass "
                 "(SPEC.md 96.33.9.1)" % lit)
        print("dosdirsw: /P stopped with %r, %d entries in view and %d lit "
              "pixels behind them" % (STRIKE, len(entries), lit))

        # --- 4: ...and a key resumes it to the same total --------------------
        strike_row = [i for i, r in enumerate(bx.view()) if STRIKE in r][0]
        bx.type(" ")
        done = bx.live()
        # **AND THE RESUME STARTED ON A NEW LINE** (96.33.9.1 item 2): the
        # prompt ends in a space and no CRLF, which is COMMAND.COM's own
        # string, so a resume that did not emit one overprinted its row.
        strike_now = [r for r in bx.view() if STRIKE in r]
        for r in strike_now:
            if r.rstrip() != STRIKE + " . . .":
                fail("the pause's row now reads %r - the resumed listing was "
                     "written over it instead of starting on the line below "
                     "(SPEC.md 96.33.9.1)" % r)
        if bx.b("dsh_more"):
            fail("a key did not finish the listing: [dsh_more] is still 1")
        if count_of(done) != total:
            fail("the resumed listing totals %s where the unpaged one totalled "
                 "%d - a resume that loses or repeats an entry still looks "
                 "like a listing" % (count_of(done), total))
        if not done[-1].rstrip().endswith(">"):
            fail("no prompt came back after the listing finished: %r"
                 % done[-2:])
        print("dosdirsw: a key resumed it and the total is still %d" % total)

        # --- 5: a bare name runs from where the box STANDS (96.33.13) --------
        # `CD` moves [dos_curdir] and dos_path_take's no-separator arm left
        # [dos_dir] - the LAUNCH folder - so from the second directory onward
        # every bare name resolved against the first one.  Reported with the
        # picture: `CD SBEEPS`, DIR lists SB.COM, `sb` answers `Bad command or
        # file name` and the path box reads `B:\sb.COM`, the volume ROOT.
        bx.run("CD BIN")
        if not bx.live()[-1].rstrip().endswith("BIN>"):
            fail("CD BIN did not move the prompt: %r" % bx.live()[-1])
        bx.type("%s\n" % SUBPROG)
        raw = m.read(bx.base + bx.dm["dos_path"], 48).split(b"\0")[0]
        got = raw.decode("latin-1").upper()
        if "BIN" not in got:
            fail("running %r from B:\\BIN put %r in the path box - a bare name "
                 "must resolve against the CURRENT directory and not the "
                 "launch one (SPEC.md 96.33.13)" % (SUBPROG, got))
        if any("Bad command" in r for r in bx.live()[-3:]):
            fail("%r was refused in B:\\BIN where it lives: %r"
                 % (SUBPROG, bx.live()[-3:]))
        print("dosdirsw: ...and a bare name in a subdirectory resolves to %r"
              % got)
        # ...and it really RAN, so dismiss it and come back to the root: the
        # program waits on AH=08h, and B:\BIN holds one file where step 6
        # wants more than a page.
        os88marty.until(m, lambda _=None: bx.b("dos_inbr"),
                        "%s to be running" % SUBPROG, limit=120.0)
        bx.type(" ")
        os88marty.until(m, lambda _=None: not bx.b("dos_inbr"),
                        "%s to exit" % SUBPROG, limit=120.0)
        bx.run("CD \\")

        # --- 6: Esc abandons -------------------------------------------------
        bx.run("DIR /P")
        if not bx.b("dsh_more"):
            fail("the second `DIR /P` did not suspend")
        m.key("Escape")
        os88marty.settle(m)
        if bx.b("dsh_more"):
            fail("Esc left the listing suspended: [dsh_more] is still 1")
        if not bx.live()[-1].rstrip().endswith(">"):
            fail("Esc did not bring the prompt back: %r" % bx.live()[-2:])
        print("dosdirsw: Esc abandons the listing and the prompt returns")

    print("dosdirsw: ok - /P suspends and resumes, /W lays out, and a switch "
          "this DOS has not got is `Invalid parameter`")


if __name__ == "__main__":
    main()
