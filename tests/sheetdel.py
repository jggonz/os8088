#!/usr/bin/env python3
"""File > Delete, and the No that must not delete (SPEC.md 81.79).

    make && python3 tests/sheetdel.py

Excel's File menu carries Delete after Save As; §81.39.2 listed it missing.
The kernel's file dialog has only OPEN and SAVE modes, so the PICKER is an
Open and a flag tells `sh_ondlg` what was meant by it - which makes two
things worth gating rather than one.

  1. **No must not delete.** The flag is cleared before the alert goes up, so
     a dismissed or refused alert cannot leave the NEXT Open behaving like a
     Delete either. This is the check that fails on a build where the answer
     is ignored, and the Yes check below would still pass on that build.
  2. **Yes deletes the file that was PICKED** - and leaves the open document
     alone. `sh_ondlg` copies the chosen name into `sh_name`, which is the
     open document's own name, so a Delete had to be given a buffer of its
     own; without it, Save afterwards would have written over the very file
     the user had just chosen to delete.

The volume is read back off the guest rather than the glass, because "is the
file gone" is a question about the DISK.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
import os88flush                                             # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp
import os88ui                                                # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402

WORK = "build/sheetdel"                 # this row's own paths (WRITING-TESTS 5.5)
DISK = "build/sheetdel.img"
DOC, JUNK = "SHIN.SLK", "ZJUNK.SLK"

FILE_MENU = (75, 45)
ITEM = lambda x, i: (x + 17, 57 + 12 * i + 2)
DELETE = 4                              # ...and Delete in it. LAST, because
                                        # Excel puts it after Save As - so
                                        # unlike 81.78 nothing else moved
PICK_ROW = (150, 82)                    # the picker's second row: the junk
                                        # file, which is at the disk ROOT so
                                        # that no folder has to be entered
PICK_OK = (345, 63)                     # ...and its button, which says "Open"
YES, NO = (268, 124), (360, 124)


def build_disk():
    os.makedirs(WORK, exist_ok=True)
    doc = os.path.join(WORK, DOC)
    junk = os.path.join(WORK, JUNK)
    open(doc, "wb").write(F.write_sylk({(0, 0): 1.0}))
    open(junk, "wb").write(F.write_sylk({(0, 0): 9.0}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK,
                    "--size", "360", "APPS:build/sheet.o88",
                    "APPS:build/CHART.OVL", "APPS:build/MACRO.OVL", "APPS:" + doc, junk],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    os.chdir(os.path.join(HERE, ".."))
    build_disk()
    with M.launch(SF.SYS, apps=DISK, machine=SF.MACHINE) as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        ui = os88ui.UI(m, mouse=mo)
        dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="B")
        M.settle(m)
        ui.open("APPS")                 # BY NAME: SF.open_shin's own lesson
        SF.open_shin(m, mo)
        M.settle(m, limit=180)

        def delete(answer, tag):
            mo.menu(FILE_MENU[0], FILE_MENU[1], *ITEM(FILE_MENU[0], DELETE))
            M.settle(m, limit=60)
            ui.chooser_open(JUNK)       # THE PICKER IS A DISK WINDOW since
                                        # the 2026-10-14 merge (38.3), so its
                                        # rows and its buttons both moved -
                                        # PICK_ROW/PICK_OK were the old
                                        # geometry. By NAME, confirmed against
                                        # the chooser's own FS_SEL, so a
                                        # re-sorted listing cannot pick the
                                        # wrong file the way it once did
            w, h, rows = m.vram("cga")
            M.write_png(os.path.join(WORK, tag + "-ask.png"), w, h, rows)
            mo.click(*answer)
            M.settle(m, limit=90)
            return sorted(os88flush.Flush(marty=m).volume(1).names())

        after_no = delete(NO, "1-no")
        after_yes = delete(YES, "2-yes")

    check(JUNK in after_no, "No does NOT delete the file",
          "the alert's answer is the whole of the decision, and a build that "
          "ignored it would still pass the Yes check below. The flag that "
          "says 'this Open was a Delete' is cleared BEFORE the alert goes up, "
          "so a dismissed one cannot leave the next Open behaving like a "
          "Delete either",
          got=after_no, want="%s still on the volume" % JUNK)
    check(JUNK not in after_yes, "Yes deletes it",
          "OSAPI_FILE_DELETE on the name the picker chose",
          got=after_yes, want="%s gone" % JUNK)
    check("APPS/" + DOC in after_yes,
          "...and the OPEN DOCUMENT is untouched",
          "sh_ondlg copies the chosen name into sh_name, which is the open "
          "document's own name. A Delete needed a buffer of its own or Save "
          "afterwards would have written over the file just deleted",
          got=after_yes, want="APPS/%s still there" % DOC)
    done("sheetdel")


if __name__ == "__main__":
    main()
