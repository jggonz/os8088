#!/usr/bin/env python3
"""THE GAME BOY PRECOMPILER'S WINDOW (docs/REALDMG-SPEC.md 20.10, 20.11).

    python3 tools/gbaotgui.py            # or, in the bundle: python3 gbaotgui.py

Choose a Game Boy cartridge and it writes GAME.O88 beside it: RealDMG with
the whole cartridge translated ahead of time (tools/gbaot.py's `make`), so
the game never stops to translate. With "Put the ROM inside the package"
ticked - the default - the package CONTAINS the cartridge ROM: it is one
file that runs alone, it is the cartridge itself, and it must not be shared.
Unticked, the ROM is not inside it - put the two side by side on an os8088
disk, double-click the package, and it opens its ROM itself. Optionally:

  * REPLAYS - a NAME.RPn RealDMG recorded (Save State > Record Replay) and
    the NAME.SSn it starts from: the counters are taken from real play as
    well as the harness's script, so the code you actually ran is what a
    small heap keeps;
  * a CHECK - so many frames of the package's code run beside the
    interpreter, every frame compared (minutes);
  * a FLOPPY - the package and the ROM on a disk image (360KB to 1.44MB).

Everything slow runs on a thread and reports into the log; the window does
not need the os8088 tree (tools/gbaotbundle.py packs it with what it needs).
"""
import os
import queue
import sys
import threading
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

SIZES = (("none", 0), ("1.44 MB", 1440), ("1.2 MB", 1200), ("720 KB", 720), ("360 KB", 360))


def floppy(img, size, files):
    import subprocess
    subprocess.check_call([sys.executable, os.path.join(HERE, "os88disk.py"), "-o", img,
                           "--size", str(size)] + list(files), stdout=subprocess.DEVNULL)


def main():
    import tkinter as tk
    from tkinter import filedialog, ttk

    root = tk.Tk()
    root.title("os8088 Game Boy precompiler")
    f = ttk.Frame(root, padding=10)
    f.grid(sticky="nsew")
    root.columnconfigure(0, weight=1)
    root.rowconfigure(0, weight=1)
    rom, out, name = tk.StringVar(), tk.StringVar(), tk.StringVar()
    frames, check, disk = tk.IntVar(value=3000), tk.IntVar(value=0), tk.StringVar(value="none")
    embed = tk.IntVar(value=1)
    reps = []

    def pick_rom():
        p = filedialog.askopenfilename(title="A Game Boy cartridge",
                                       filetypes=(("Game Boy ROM", "*.gb *.GB *.gbc *.GBC"),
                                                  ("all files", "*")))
        if not p:
            return
        rom.set(p)
        import gbaot
        try:
            name.set(gbaot.rom_name(p))
        except SystemExit:
            stem = "".join(c for c in os.path.splitext(os.path.basename(p))[0].upper()
                           if c.isalnum())[:8] or "GAME"
            name.set(stem + ".GB")
        out.set(os.path.join(os.path.dirname(p), name.get().rsplit(".", 1)[0] + ".O88"))

    def pick_out():
        p = filedialog.asksaveasfilename(title="The package", defaultextension=".O88",
                                         filetypes=(("os8088 package", "*.O88"),))
        if p:
            out.set(p)

    def add_replay():
        rp = filedialog.askopenfilename(title="A replay (NAME.RPn)")
        if not rp:
            return
        ss = filedialog.askopenfilename(title="...and the save state it starts from (NAME.SSn)")
        if ss:
            reps.append((rp, ss))
            replist.insert("end", "%s from %s" % (os.path.basename(rp), os.path.basename(ss)))

    r = 0
    for label, var, cmd in (("Cartridge", rom, pick_rom), ("Package", out, pick_out)):
        ttk.Label(f, text=label).grid(row=r, column=0, sticky="w")
        ttk.Entry(f, textvariable=var, width=60).grid(row=r, column=1, sticky="ew")
        ttk.Button(f, text="Choose...", command=cmd).grid(row=r, column=2)
        r += 1
    ttk.Label(f, text="ROM's name on the disk").grid(row=r, column=0, sticky="w")
    ttk.Entry(f, textvariable=name, width=14).grid(row=r, column=1, sticky="w")
    r += 1
    ttk.Label(f, text="Counters: script frames").grid(row=r, column=0, sticky="w")
    ttk.Spinbox(f, from_=0, to=20000, increment=500, textvariable=frames,
                width=8).grid(row=r, column=1, sticky="w")
    r += 1
    ttk.Label(f, text="Replays").grid(row=r, column=0, sticky="nw")
    replist = tk.Listbox(f, height=3)
    replist.grid(row=r, column=1, sticky="ew")
    ttk.Button(f, text="Add...", command=add_replay).grid(row=r, column=2, sticky="n")
    r += 1
    ttk.Label(f, text="Check: frames (0 = none)").grid(row=r, column=0, sticky="w")
    ttk.Spinbox(f, from_=0, to=20000, increment=500, textvariable=check,
                width=8).grid(row=r, column=1, sticky="w")
    r += 1
    ttk.Checkbutton(f, text="Put the ROM inside the package (one file)",
                    variable=embed).grid(row=r, column=1, sticky="w")
    r += 1
    ttk.Label(f, text="...and a floppy").grid(row=r, column=0, sticky="w")
    ttk.Combobox(f, textvariable=disk, values=[s for s, _ in SIZES], state="readonly",
                 width=10).grid(row=r, column=1, sticky="w")
    r += 1
    go = ttk.Button(f, text="Make the package")
    go.grid(row=r, column=1, sticky="w", pady=6)
    r += 1
    log = tk.Text(f, height=18, width=96, state="disabled")
    log.grid(row=r, column=0, columnspan=3, sticky="nsew")
    f.rowconfigure(r, weight=1)
    f.columnconfigure(1, weight=1)
    q = queue.Queue()

    def say(s):
        q.put(str(s))

    def pump():
        while not q.empty():
            s = q.get()
            if s == "\0done":
                go.configure(state="normal")
                continue
            log.configure(state="normal")
            log.insert("end", s + "\n")
            log.see("end")
            log.configure(state="disabled")
        root.after(150, pump)

    def work(a):
        try:
            import gbaot
            gbaot.make(os.path.join(gbaot.ROOT, "build"), a["rom"], a["out"], a["name"],
                       a["frames"], a["reps"], a["check"], log=say, embed=a["embed"])
            if a["disk"]:
                import shutil
                import tempfile
                t = tempfile.mkdtemp()
                gb = os.path.join(t, a["name"])
                shutil.copy(a["rom"], gb)
                img = os.path.splitext(a["out"])[0] + "-%d.img" % a["disk"]
                floppy(img, a["disk"], [a["out"]] + ([] if a["embed"] else [gb]))
                shutil.rmtree(t)
                say("floppy: %s - put it in B: beside an os8088 system disk" % img)
            if a["embed"]:
                say("DONE: %s. Copy it to an os8088 disk and double-click it." % a["out"])
            else:
                say("DONE: %s. Copy it and %s to one folder of an os8088 disk and "
                    "double-click the package." % (a["out"], a["name"]))
        except SystemExit as e:
            say("STOPPED: %s" % e)
        except Exception:
            say(traceback.format_exc())
        q.put("\0done")

    def start():
        if not rom.get() or not out.get():
            say("Choose a cartridge first.")
            return
        a = {"rom": rom.get(), "out": out.get(), "name": name.get().upper(),
             "frames": frames.get(), "reps": list(reps), "check": check.get(),
             "disk": dict(SIZES)[disk.get()], "embed": bool(embed.get())}
        go.configure(state="disabled")
        say("making %s from %s..." % (a["out"], a["rom"]))
        threading.Thread(target=work, args=(a,), daemon=True).start()

    go.configure(command=start)
    try:
        import unicorn  # noqa: F401
    except ImportError:
        say("This needs the unicorn package: python3 -m pip install unicorn")
        go.configure(state="disabled")
    root.after(150, pump)
    root.mainloop()


if __name__ == "__main__":
    main()
