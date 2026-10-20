#!/usr/bin/env python3
"""A STALE HINT FALLS BACK TO ANOTHER DISK'S ASSOC.DAT (SPEC.md 54.4.2.3).

    make && python3 tests/assocstale.py

Reported from the field: an installed machine whose C:\\ASSOC.DAT names a
C:\\APPS\\VIDEO.O88 that has since been deleted, the apps disk in B: carrying
one in B:\\APPS - and a double-click on a .V88 toasted `Needs VIDEO.O88`.

The same shape on two floppies: the 720KB system disk with APPS\\VIDEO.O88
DELETED and its ASSOC.DAT left naming it - so the hint rung 1 reads goes
nowhere - and apps720.img in B:, whose own ASSOC.DAT says where its copy is.
A double-click on A:\\MEDIA\\OS8088.V88 must open a Video Player.

What failed was the sweep: per volume it tried the ROOT and then a folder
only the five built-in programs have (`assoc_dfold`), and a volume's
ASSOC.DAT was read only AFTER the program had been found on it. So a
declared program one folder down on another disk was never found at all.

Broken on purpose - the sweep's ASSOC.DAT rung and the APPS/GAMES default
taken out - it FAILS with the toast.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build                          # noqa: E402
import fatdel                                                 # noqa: E402

MACHINE = "os8088_5150_herc_720_gla"


def main():
    os.chdir(ROOT)
    bad = []
    img = os.path.join(ROOT, "build", "assocstale-%d.img" % os.getpid())
    with open(os88build.at("build/os8088-720.img"), "rb") as f:
        data = f.read()
    with open(img, "wb") as f:
        f.write(data)
    try:
        fatdel.delete(img, "APPS/VIDEO.O88")
        with os88ui.boot(img, apps=os88build.at("build/apps720.img"),
                         machine=MACHINE) as ui:
            ui.path("A:/MEDIA")
            ui.open("OS8088.V88", expect=None)

            def done():
                txt, on = ui.toast()
                return "Video Player" in " ".join(ui.titles()) or \
                    (on and "Needs" in txt)
            try:
                os88marty.until(ui.m, lambda mm: done(), "the open",
                                poll=0.3, limit=300.0, guest=120.0)
            except os88marty.MartyError as e:
                bad.append("neither a window nor a toast: %s" % e)
            txt, on = ui.toast()
            print("   windows %s; toast %r (%s)"
                  % (ui.titles(), txt, "up" if on else "down"))
            if not any("Video Player" in t for t in ui.titles()):
                bad.append("A:\\MEDIA\\OS8088.V88 did not open: the toast "
                           "says %r" % txt)
    finally:
        os.remove(img)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
