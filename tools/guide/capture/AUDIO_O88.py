"""Audio Player's screenshots, for tools/guide/manuals/AUDIO_O88.md.

No shipped disk carries a .WAV, so this one makes its own: run the script
itself first, then the capture on a VGA XT with a Sound Blaster.

    python3 tools/guide/capture/AUDIO_O88.py     # writes build/guide/audio360.img
    python3 tools/guide/capture/run.py AUDIO_O88 \\
        --apps build/guide/audio360.img --machine os8088_xt_vga_sb
"""
import math
import os
import subprocess
import sys
import tempfile
import wave

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))), ""))
import os88marty  # noqa: E402

TITLE = "Audio Player"
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
DISK = os.path.join(ROOT, "build", "guide", "audio360.img")


def _tune(path, notes, rate=8000, beat=0.25):
    """A short melody as 8-bit unsigned mono PCM: each note a soft square
    with a decay, so the clock and the bar have something to time."""
    out = bytearray()
    for n in notes:
        f = 440.0 * 2 ** ((n - 69) / 12.0) if n else 0
        cnt = int(rate * beat)
        for j in range(cnt):
            t = j / float(rate)
            env = math.exp(-3.0 * t / beat)
            v = 0.0
            if f:
                v = 0.45 * env * (1 if math.sin(2 * math.pi * f * t) >= 0 else -1)
                v += 0.2 * env * math.sin(2 * math.pi * f * 0.5 * t)
            out.append(max(0, min(255, int(128 + 127 * v))))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(bytes(out))


def build():
    scale = [60, 64, 67, 72, 67, 64, 62, 65, 69, 74, 69, 65,
             64, 67, 71, 76, 71, 67, 65, 69, 72, 77, 72, 69]
    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, "SUNRISE.WAV")
        b = os.path.join(tmp, "CHIMES.WAV")
        _tune(a, scale * 4)                     # 24 seconds
        _tune(b, [84, 0, 79, 0, 76, 0, 72, 0] * 5)   # 10 seconds
        os.makedirs(os.path.dirname(DISK), exist_ok=True)
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                        "-o", DISK, "--size", "360",
                        "APPS:" + os.path.join(ROOT, "build", "audio.o88"),
                        "MEDIA:" + a, "MEDIA:" + b], check=True)
    print(DISK)


def _menu_shot(ui, shot, title, sid):
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 2.0)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(600, 460, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    # A double-click on a .WAV opens Audio Player on it and plays it.
    ui.path("B:/MEDIA/SUNRISE.WAV")
    ui.wait_window(TITLE)
    ui.mo.to(620, 460)
    os88marty.guest_sleep(ui.m, 2.5)
    shot("audio-window", window=TITLE, pad=8, settle=False)

    # File > Open... while it plays adds a second song without stopping it.
    ui.menu_pick("File", "Open...")
    ch = ui.chooser()
    x, y, ww, hh = ui._rect(ch.i)
    shot("audio-open", rect=(x - 6, y - 6, ww + 14, hh + 14), settle=False)
    ui.chooser_open("CHIMES.WAV")
    os88marty.guest_sleep(ui.m, 2.0)

    # Repeat latched down, then Space pauses.
    ui.menu_pick("Play", "Repeat")
    ui.m.key("Space")
    ui.mo.to(620, 460)
    os88marty.guest_sleep(ui.m, 1.5)
    shot("audio-paused", window=TITLE, pad=8, settle=False)

    _menu_shot(ui, shot, "Play", "audio-play-menu")

    ui.menu_pick(TITLE, "About " + TITLE)
    ui.mo.to(620, 460)
    os88marty.guest_sleep(ui.m, 1.5)
    shot("audio-about", window=TITLE, pad=8, settle=False)


if __name__ == "__main__":
    build()
