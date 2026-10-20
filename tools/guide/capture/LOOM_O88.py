"""Loom's screenshots, for tools/guide/manuals/LOOM_O88.md.

    python3 tools/guide/capture/run.py LOOM_O88 --apps build/weave360.img

Opens the Greeter demo's sources, adds a line to its markup, packs it, previews
it, runs the new bundle in Weave, and then makes a deliberate mistake to show
how a pack error is reported.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty  # noqa: E402
from WEAVE_O88 import hold, release, named, wrect  # noqa: E402

LINE = '    <label br="1">Made on this machine.</label>'


def typed(m, text):
    """Type at a person's pace. Loom letters every keystroke as it lands, and
    keys injected as fast as the emulator takes them overrun the keyboard
    buffer and are lost."""
    for ch in text:
        m.type_text(ch)
        os88marty.pace(m, 0.05)


def capture(ui, shot):
    m = ui.m
    ui.path("B:/LOOM/FORM.WML")
    ui.settle()

    # add a label under the title: seven lines down is the <rule> line
    for _ in range(7):
        m.key("ArrowDown")
    m.key("Home")
    typed(m, LINE)
    m.key("Enter")
    os88marty.settle(m)
    ui.mo.to(630, 430)
    shot("loom-window", rect=wrect(ui, "Loom"))

    # the script, one ^N along
    m.ctrl("KeyN")
    os88marty.settle(m)
    ui.settle()
    shot("loom-script", rect=(0, 0, 640, 200))
    m.ctrl("KeyN")                          # FORM.WFX? keep going to the WML
    m.ctrl("KeyN")
    os88marty.settle(m)

    # File > Pack Bundle, held open, then released onto it
    hold(ui, "File", "Pack Bundle")
    shot("loom-menu", rect=(0, 0, 300, 140), settle=False)
    release(ui)
    ui.settle()

    # View > Preview
    ui.menu_pick("View", "Preview")
    ui.settle()
    shot("loom-preview", rect=wrect(ui, "Loom"))
    ui.menu_pick("View", "Edit")  # the item reads Edit while previewing
    ui.settle()

    # run the bundle Pack just wrote: LOOM/FORM.WAB
    w = ui.open("FORM.WAB")
    ui.settle()
    ui.mo.to(400, 300)
    shot("loom-run", rect=(0, 0, 360, 170))
    ui.close(named(ui, "Weave"))

    # a mistake: an element Weave does not have
    ui.raise_window(named(ui, "Loom"))
    for _ in range(3):
        m.key("PageUp")
    for _ in range(8):
        m.key("ArrowDown")
    m.key("Home")
    typed(m, "    <blink>Hi</blink>")
    m.key("Enter")
    m.ctrl("KeyP")
    os88marty.settle(m)
    ui.settle()
    ui.mo.to(630, 430)
    shot("loom-error", rect=wrect(ui, "Loom"))
