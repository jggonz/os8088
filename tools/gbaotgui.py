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

Files can be DROPPED on the window as well as chosen: a cartridge fills the
cartridge, and a NAME.RPn or a NAME.SSn fills its half of the replay and,
when its partner is beside it under the same name and digit, the other half
too (the pair RealDMG writes). A dropped half whose partner IS found goes
straight into the list of replays, where Remove takes it out again; one
whose partner is not waits in its field for the other and Add. On
Windows the drop is the shell's own, with no extra package; elsewhere it
wants `pip install tkinterdnd2`, and the window works without it.

Everything slow runs on a thread and reports into the log; the window does
not need the os8088 tree (tools/gbaotbundle.py packs it with what it needs).
"""
import os
import queue
import re
import sys
import threading
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

SIZES = (("none", 0), ("1.44 MB", 1440), ("1.2 MB", 1200), ("720 KB", 720), ("360 KB", 360))


ROM_EXTS = (".gb", ".gbc")
REPLAY = re.compile(r"(?i)(.*)\.(rp|ss)([0-9])")


def kind(path):
    """'rom', 'rp' or 'ss' by the file's name, or None"""
    b = os.path.basename(path)
    if os.path.splitext(b)[1].lower() in ROM_EXTS:
        return "rom"
    m = REPLAY.fullmatch(b)
    return m.group(2).lower() if m else None


def partner(path):
    """NAME.RPn's NAME.SSn, or the other way round, if it is there beside it
    (matched without regard to case, as a FAT disk's names are), else None"""
    m = REPLAY.fullmatch(os.path.basename(path))
    if not m:
        return None
    want = ("%s.%s%s" % (m.group(1), "ss" if m.group(2).lower() == "rp" else "rp",
                         m.group(3))).lower()
    d = os.path.dirname(path) or "."
    try:
        names = os.listdir(d)
    except OSError:
        return None
    for n in sorted(names):
        if n.lower() == want and os.path.isfile(os.path.join(d, n)):
            return os.path.join(d, n)
    return None


def win_drops(root, take):
    """Windows Explorer drops, through the shell's WM_DROPFILES on the
    toplevel's own frame - no package needed. `take` gets the list of paths
    (on the Tk thread, inside its message loop, so it only queues them).
    True if it is installed."""
    import ctypes
    from ctypes import wintypes
    u32, sh32 = ctypes.windll.user32, ctypes.windll.shell32
    LRESULT = ctypes.c_ssize_t
    PROC = ctypes.WINFUNCTYPE(LRESULT, wintypes.HWND, wintypes.UINT, wintypes.WPARAM,
                              wintypes.LPARAM)
    u32.GetParent.restype = wintypes.HWND
    u32.GetParent.argtypes = (wintypes.HWND,)
    u32.CallWindowProcW.restype = LRESULT
    u32.CallWindowProcW.argtypes = (ctypes.c_void_p, wintypes.HWND, wintypes.UINT,
                                    wintypes.WPARAM, wintypes.LPARAM)
    setlp = getattr(u32, "SetWindowLongPtrW", None) or u32.SetWindowLongW
    setlp.restype = ctypes.c_void_p
    setlp.argtypes = (wintypes.HWND, ctypes.c_int, ctypes.c_void_p)
    sh32.DragQueryFileW.restype = wintypes.UINT
    sh32.DragQueryFileW.argtypes = (ctypes.c_void_p, wintypes.UINT, wintypes.LPWSTR,
                                    wintypes.UINT)
    sh32.DragFinish.argtypes = (ctypes.c_void_p,)
    sh32.DragAcceptFiles.argtypes = (wintypes.HWND, wintypes.BOOL)
    WM_DROPFILES, GWLP_WNDPROC = 0x0233, -4
    root.update_idletasks()
    hwnd = u32.GetParent(root.winfo_id()) or root.winfo_id()   # Tk's wrapper frame
    old = None

    def proc(h, msg, wp, lp):
        if msg == WM_DROPFILES:
            paths = []
            for i in range(sh32.DragQueryFileW(wp, 0xFFFFFFFF, None, 0)):
                n = sh32.DragQueryFileW(wp, i, None, 0) + 1
                buf = ctypes.create_unicode_buffer(n)
                sh32.DragQueryFileW(wp, i, buf, n)
                paths.append(buf.value)
            sh32.DragFinish(wp)
            take(paths)
            return 0
        return u32.CallWindowProcW(old, h, msg, wp, lp)

    cb = PROC(proc)
    old = setlp(hwnd, GWLP_WNDPROC, ctypes.cast(cb, ctypes.c_void_p))
    if not old:
        return False
    root._os88_drop = cb                   # (the callback must outlive this frame)
    try:                                   # a Python started as administrator
        flt = u32.ChangeWindowMessageFilterEx   #   is not sent Explorer's drops
        for m in (WM_DROPFILES, 0x0049, 0x004A):   # unless these are let through
            flt(hwnd, m, 1, None)
    except AttributeError:
        pass
    sh32.DragAcceptFiles(hwnd, True)
    return True


def floppy(img, size, files):
    import subprocess
    subprocess.check_call([sys.executable, os.path.join(HERE, "os88disk.py"), "-o", img,
                           "--size", str(size)] + list(files), stdout=subprocess.DEVNULL)


def main():
    import tkinter as tk
    from tkinter import filedialog, ttk

    dnd = None                             # (tkinterdnd2, where it is installed
    if sys.platform != "win32":            #   and the shell's drop is not there)
        try:
            import tkinterdnd2 as dnd
        except ImportError:
            dnd = None
    root = dnd.TkinterDnD.Tk() if dnd else tk.Tk()
    root.title("os8088 Game Boy precompiler")
    f = ttk.Frame(root, padding=10)
    f.grid(sticky="nsew")
    root.columnconfigure(0, weight=1)
    root.rowconfigure(0, weight=1)
    rom, out, name = tk.StringVar(), tk.StringVar(), tk.StringVar()
    frames, check, disk = tk.IntVar(value=3000), tk.IntVar(value=0), tk.StringVar(value="none")
    embed = tk.IntVar(value=1)
    mega = tk.IntVar(value=0)
    nimg = tk.IntVar(value=10)
    prefer = tk.StringVar(value="Keep the ROM inside, fewer code images")
    PREFS = [("Keep the ROM inside, fewer code images", "rom"),
             ("Keep every code image, the ROM beside it", "code")]
    rpv, ssv = tk.StringVar(), tk.StringVar()     # the replay being chosen
    reps = []                                    # ...and those added to the list
    RPTYPES = (("RealDMG replay", "*.rp? *.RP?"), ("all files", "*"))
    SSTYPES = (("RealDMG save state", "*.ss? *.SS?"), ("all files", "*"))

    def pick_rom():
        p = filedialog.askopenfilename(title="A Game Boy cartridge",
                                       filetypes=(("Game Boy ROM", "*.gb *.GB *.gbc *.GBC"),
                                                  ("all files", "*")))
        if p:
            set_rom(p)

    def set_rom(p):
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

    def set_half(p, mine, other):
        """one half of the replay, and the other from its partner if it is
        there: True if the partner was found"""
        mine.set(p)
        q = partner(p)
        if q:
            other.set(q)
            say("%s pairs with %s." % (os.path.basename(p), os.path.basename(q)))
        else:
            say("No %s beside %s - choose it." % (
                "save state (.SSn)" if mine is rpv else "replay (.RPn)", os.path.basename(p)))
        return bool(q)

    def pick_rp():
        p = filedialog.askopenfilename(title="A replay (NAME.RPn)", filetypes=RPTYPES)
        if p:
            set_half(p, rpv, ssv)

    def pick_ss():
        p = filedialog.askopenfilename(title="The save state it starts from (NAME.SSn)",
                                       filetypes=SSTYPES)
        if p:
            set_half(p, ssv, rpv)

    def staged():
        """the replay in the two fields, if both are filled; None otherwise -
        and a line to the log, and False, if one is filled or a file is not there"""
        rp, ss = rpv.get().strip(), ssv.get().strip()
        if not rp and not ss:
            return None
        if not (rp and ss):
            say("A replay needs both halves: the NAME.RPn and the NAME.SSn it starts from.")
            return False
        for x in (rp, ss):
            if not os.path.isfile(x):
                say("Not found: %s" % x)
                return False
        return (rp, ss)

    def add_replay(dropping=False):
        pr = staged()
        if not pr:
            if pr is None and not dropping:
                say("Choose or drop a replay and its save state first.")
            return
        if pr not in reps:
            reps.append(pr)
            replist.insert("end", "%s from %s" % tuple(os.path.basename(x) for x in pr))
            if dropping:
                say("Added replay: %s from %s" % tuple(os.path.basename(x) for x in pr))
        rpv.set("")
        ssv.set("")

    def remove_replay():
        for i in reversed(replist.curselection()):
            replist.delete(i)
            del reps[i]

    drops = queue.Queue()

    def dropped(paths):
        for p in paths:
            k = kind(p)
            if k == "rom":
                set_rom(p)
                say("Cartridge: %s" % p)
            elif k in ("rp", "ss"):
                found = set_half(p, rpv, ssv) if k == "rp" else set_half(p, ssv, rpv)
                if found:                      # both halves found: straight to the
                    add_replay(dropping=True)  #   list; a lone half waits in its
            else:                              #   field for the other
                say("Not a cartridge (.gb/.gbc) or a replay (.RPn/.SSn): %s" % p)

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
    for label, var, cmd in (("Replay (.RPn)", rpv, pick_rp),
                            ("...from save state (.SSn)", ssv, pick_ss)):
        ttk.Label(f, text=label).grid(row=r, column=0, sticky="w")
        ttk.Entry(f, textvariable=var, width=60).grid(row=r, column=1, sticky="ew")
        ttk.Button(f, text="Choose...", command=cmd).grid(row=r, column=2)
        r += 1
    ttk.Label(f, text="Replays").grid(row=r, column=0, sticky="nw")
    replist = tk.Listbox(f, height=3)
    replist.grid(row=r, column=1, sticky="ew")
    bf = ttk.Frame(f)
    bf.grid(row=r, column=2, sticky="n")
    ttk.Button(bf, text="Add", command=add_replay).grid(row=0, column=0)
    ttk.Button(bf, text="Remove", command=remove_replay).grid(row=1, column=0)
    r += 1
    ttk.Label(f, text="Check: frames (0 = none)").grid(row=r, column=0, sticky="w")
    ttk.Spinbox(f, from_=0, to=20000, increment=500, textvariable=check,
                width=8).grid(row=r, column=1, sticky="w")
    r += 1
    ttk.Checkbutton(f, text="Put the ROM inside the package (one file)",
                    variable=embed).grid(row=r, column=1, sticky="w")
    r += 1
    ttk.Checkbutton(f, text="For kern_mega (protected mode; runs on every kernel)",
                    variable=mega).grid(row=r, column=1, sticky="w")
    r += 1
    ttk.Label(f, text="Code images, at most").grid(row=r, column=0, sticky="w")
    ttk.Spinbox(f, from_=1, to=29, increment=1, textvariable=nimg,
                width=8).grid(row=r, column=1, sticky="w")
    r += 1                                 # (10: what this machine checks; more
    ttk.Label(f, text="If it does not all fit").grid(row=r, column=0, sticky="w")
    ttk.Combobox(f, textvariable=prefer, values=[p for p, _ in PREFS], state="readonly",
                 width=44).grid(row=r, column=1, sticky="w")   # are for kern_mega)
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
        while not drops.empty():
            dropped(drops.get())
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
                       a["frames"], a["reps"], a["check"], log=say, embed=a["embed"],
                       mega=a["mega"], images=a["images"], prefer=a["prefer"])
            a["embed"] = getattr(gbaot.package_fit, "embedded", a["embed"])
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
        pr = staged()                      # a replay in the fields and not yet
        if pr is False:                    #   Added is used too: it is what the
            return                         #   fields say, and Add is for a second
        use = list(reps) + ([pr] if pr and pr not in reps else [])
        for rp, ss in use:
            say("replay: %s from %s" % (os.path.basename(rp), os.path.basename(ss)))
        a = {"rom": rom.get(), "out": out.get(), "name": name.get().upper(),
             "frames": frames.get(), "reps": use, "check": check.get(),
             "disk": dict(SIZES)[disk.get()], "embed": bool(embed.get()),
             "mega": bool(mega.get()), "images": nimg.get(),
             "prefer": dict(PREFS)[prefer.get()]}
        go.configure(state="disabled")
        say("making %s from %s..." % (a["out"], a["rom"]))
        threading.Thread(target=work, args=(a,), daemon=True).start()

    go.configure(command=start)
    hint = "Drop a cartridge, or a .RPn/.SSn replay, anywhere on this window."
    try:
        if sys.platform == "win32" and win_drops(root, drops.put):
            say(hint)
        elif dnd:
            def reg(w):
                w.drop_target_register(dnd.DND_FILES)
                w.dnd_bind("<<Drop>>", lambda e: drops.put(root.tk.splitlist(e.data)))
                for c in w.winfo_children():
                    reg(c)
            reg(root)
            say(hint)
        elif sys.platform != "win32":
            say("(To drop files on this window: python3 -m pip install tkinterdnd2)")
    except Exception as e:                 # a drop is a nicety: the buttons stay
        say("(Dropping files is unavailable here: %s)" % e)
    try:
        import unicorn  # noqa: F401
    except ImportError:
        say("This needs the unicorn package: python3 -m pip install unicorn")
        go.configure(state="disabled")
    try:                                   # what it will build FROM, said
        import gbaot                       #   before anything is made: a
        import shutil                      #   missing nasm or a half-copied
        if gbaot.BUNDLED:                  #   tree is otherwise found a
            say("Using the prebuilt binaries in %s." % gbaot.PREBUILT)   # minute in
        else:
            gbaot.need_sources()
            if not shutil.which("nasm"):
                say("This os8088 tree needs nasm on the PATH (https://nasm.us) to "
                    "build the precompiler's parts - or use the GBSTATIC bundle, which "
                    "carries them prebuilt.")
                go.configure(state="disabled")
    except SystemExit as e:
        say(str(e))
        go.configure(state="disabled")
    root.after(150, pump)
    root.mainloop()


if __name__ == "__main__":
    main()
