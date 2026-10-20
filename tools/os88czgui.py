#!/usr/bin/env python3
"""os88cz with a window on it: split, join, pack and unpack (SPEC.md 20.14, 20.17).

    python3 tools/os88czgui.py [FILE]

Same engine, different face - tools/os88vencgui.py's rule: **it imports
`os88cz` and reimplements none of it.** Every button is one of that module's
functions and every refusal is its sentence, shown as it is.

What the window adds is the part a person does four times a week and gets
wrong once: choosing a file, seeing what it is, seeing how many disks it will
take BEFORE anything is written, and getting floppy images out the other end
that go straight onto the disks.

DROP A FILE ON THE WINDOW and it goes to the tab it belongs to (`drop_tab`):
any part of a set to Join (a `.001` or any other part, and the Join tab is
filled in), a 'CZ' file to Pack / Unpack, anything else to Split - or to
Pack, if that is the tab showing, since a plain file is what Pack takes too.
Of several files the first is taken. The drop itself is tools/os88drop.py,
the video encoder's (SPEC.md 98.2.12), and it only QUEUES: the pump takes
it, as it takes a split's log.

tkinter because it is in the standard library: one file, no pip. Tk is
imported SOFTLY, so everything above `App` loads on a machine with no display
and tests/unit/t_cz.py can check the plan arithmetic without one.
"""
import os
import queue
import sys
import threading
import traceback

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except Exception:                                            # pragma: no cover
    tk = None
    filedialog = messagebox = ttk = None

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import os88cz as CZ                                           # noqa: E402
import os88drop                                               # noqa: E402

SIZE_CHOICES = ["720k", "360k", "1.2m", "1.44m"]
METHOD_CHOICES = [("LZ4 - fast to expand on an 8088", CZ.M_LZ4),
                  ("LZB - smaller on text, ~4x the decode", CZ.M_LZB),
                  ("Store - split only", CZ.M_STORE)]


def suggest_name(path):
    """the 8.3 name a file will rejoin as: its own if it is one, else a
    truncation a person can then edit"""
    try:
        n = CZ.name83(path)
        if CZ.is_part_name(n):          # NAME.001 would be its own part's
            n = n.partition(".")[0] + ".DAT"
        return n
    except CZ.CZError:
        base, _, ext = os.path.basename(path).upper().rpartition(".")
        if not base:
            base, ext = ext, ""
        keep = lambda s: "".join(c for c in s if c.encode("latin-1", "replace")[0] in CZ._OK83)  # noqa: E731
        base, ext = keep(base)[:8] or "FILE", keep(ext)[:3]
        if len(ext) == 3 and ext.isdigit():
            ext = "DAT"
        return base + ("." + ext if ext else "")


T_SPLIT, T_JOIN, T_PACK = 0, 1, 2


def file_kind(path):
    """os88cz.kind() of a file off its head: a whole header, because a 'CZ'
    file is not known by its first two bytes alone (os88lz.cz_parse wants
    the format byte and the size behind them)"""
    with open(path, "rb") as f:
        return CZ.kind(f.read(64))


def drop_tab(kind, current, size=0):
    """the tab a dropped file goes to: `kind` is os88cz.kind() of its first
    bytes, `current` the tab showing, `size` its length. A part is a set to
    join and a 'CZ' file one to unpack, whatever is showing; a plain file is
    split, unless Pack is showing, which takes a plain file as well - one it
    can pack: over CZ.CZ_OPENMAX no machine could open the result, so that
    goes to Split whatever is showing"""
    if kind == "part":
        return T_JOIN
    if kind == "cz":
        return T_PACK
    if current == T_PACK and size <= CZ.CZ_OPENMAX:
        return T_PACK
    return T_SPLIT


def estimate(nbytes, size):
    """how many parts a STORED split would be - the upper bound shown before
    anything is encoded, since compression only ever makes it fewer"""
    cap = CZ.part_size(size) - CZ.CS_HDR
    blocks = -(-nbytes // CZ.BLOCK)
    per = max(1, cap // CZ.REC_MAX)
    return -(-blocks // per)


class App:                                                   # pragma: no cover
    def __init__(self, root, path=None):
        self.root = root
        self.q = queue.Queue()
        self.busy = False
        self.auto = {}                  # var -> the value WE put there
        root.title("os88cz - split, join, pack")
        root.minsize(620, 440)
        nb = ttk.Notebook(root)
        nb.pack(fill="both", expand=True, padx=8, pady=8)
        self.nb = nb
        self._split_tab(nb)
        self._join_tab(nb)
        self._pack_tab(nb)
        bot = ttk.Frame(root)
        bot.pack(fill="both", expand=True, padx=8, pady=(0, 8))
        self.bar = ttk.Progressbar(bot, mode="determinate", maximum=1000)
        self.bar.pack(fill="x")
        self.log = tk.Text(bot, height=9, wrap="word")
        self.log.pack(fill="both", expand=True, pady=(6, 0))
        self.log.configure(state="disabled")
        root.after(100, self._pump)
        if path:
            self.take(path)

    # --- layout ------------------------------------------------------------
    def _row(self, f, r, label, var, browse=None, width=52):
        ttk.Label(f, text=label).grid(row=r, column=0, sticky="w", pady=3)
        e = ttk.Entry(f, textvariable=var, width=width)
        e.grid(row=r, column=1, sticky="we", pady=3)
        if browse:
            ttk.Button(f, text="Browse...", command=browse).grid(
                row=r, column=2, padx=(6, 0))
        f.columnconfigure(1, weight=1)
        return e

    def _split_tab(self, nb):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text="Split")
        self.s_in = tk.StringVar()
        self.s_out = tk.StringVar()
        self.s_name = tk.StringVar()
        self.s_size = tk.StringVar(value="720k")
        self.s_meth = tk.IntVar(value=CZ.M_LZ4)
        self.s_img = tk.BooleanVar(value=True)
        self.s_plan = tk.StringVar(value="Choose a file to split.")
        self._row(f, 0, "File", self.s_in, self._pick_split)
        self._row(f, 1, "Parts go to", self.s_out, self._pick_sout)
        self._row(f, 2, "Rejoins as (8.3)", self.s_name, width=16)
        ttk.Label(f, text="Part size").grid(row=3, column=0, sticky="w")
        cb = ttk.Combobox(f, textvariable=self.s_size, values=SIZE_CHOICES,
                          width=14)
        cb.grid(row=3, column=1, sticky="w", pady=3)
        cb.bind("<<ComboboxSelected>>", lambda e: self._plan())
        cb.bind("<KeyRelease>", lambda e: self._plan())
        ttk.Label(f, text="Blocks").grid(row=4, column=0, sticky="nw")
        mf = ttk.Frame(f)
        mf.grid(row=4, column=1, sticky="w")
        for i, (t, v) in enumerate(METHOD_CHOICES):
            ttk.Radiobutton(mf, text=t, variable=self.s_meth, value=v).grid(
                row=i, column=0, sticky="w")
        ttk.Checkbutton(f, text="...and a floppy image per part "
                        "(NAME-001.IMG)", variable=self.s_img
                        ).grid(row=5, column=1, sticky="w", pady=4)
        ttk.Label(f, textvariable=self.s_plan, foreground="#335").grid(
            row=6, column=0, columnspan=3, sticky="w", pady=4)
        self.s_go = ttk.Button(f, text="Split", command=self.do_split)
        self.s_go.grid(row=7, column=2, sticky="e")

    def _join_tab(self, nb):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text="Join")
        self.j_in = tk.StringVar()
        self.j_out = tk.StringVar()
        self.j_info = tk.StringVar(value="Choose any part of a set - "
                                   "NAME.001, NAME.002 ...")
        self._row(f, 0, "A part", self.j_in, self._pick_join)
        self._row(f, 1, "Result goes to", self.j_out, self._pick_jout)
        ttk.Label(f, textvariable=self.j_info, wraplength=560,
                  foreground="#335").grid(row=2, column=0, columnspan=3,
                                          sticky="w", pady=6)
        self.j_go = ttk.Button(f, text="Join", command=self.do_join)
        self.j_go.grid(row=3, column=2, sticky="e")

    def _pack_tab(self, nb):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text="Pack / Unpack")
        self.p_in = tk.StringVar()
        self.p_out = tk.StringVar()
        self.p_lzb = tk.BooleanVar(value=False)
        self.p_info = tk.StringVar(value="One file into a 'CZ' file the "
                                   "machine reads transparently, or back.")
        self._row(f, 0, "File", self.p_in, self._pick_pack)
        self._row(f, 1, "Write to", self.p_out, self._pick_pout)
        ttk.Checkbutton(f, text="LZB (smaller, slower to open on an 8088)",
                        variable=self.p_lzb).grid(row=2, column=1,
                                                  sticky="w")
        ttk.Label(f, textvariable=self.p_info, wraplength=560,
                  foreground="#335").grid(row=3, column=0, columnspan=3,
                                          sticky="w", pady=6)
        bf = ttk.Frame(f)
        bf.grid(row=4, column=1, columnspan=2, sticky="e")
        self.p_go = ttk.Button(bf, text="Pack", command=self.do_pack)
        self.p_go.pack(side="left", padx=4)
        self.u_go = ttk.Button(bf, text="Unpack", command=self.do_unpack)
        self.u_go.pack(side="left")

    # --- choosing ------------------------------------------------------------
    def take(self, path):
        """a file handed over on the command line or dropped on the window:
        the tab it belongs to (`drop_tab`), filled in"""
        if os.path.isdir(path):
            self.say(f"{path}: a folder - drop a file")
            return
        try:
            k = file_kind(path)
        except OSError as e:
            self.say(str(e))
            return
        t = drop_tab(k, self.nb.index(self.nb.select()),
                     os.path.getsize(path))
        self.nb.select(t)
        (self._set_split, self._set_join, self._set_pack)[t](path)

    def dropped(self, paths):
        """a drop, on Tk's thread through the pump: the first file taken.
        Not while a job runs - its fields are what it is working from"""
        paths = [p for p in paths if p]
        if self.busy:
            self.say("Busy - drop it again when this one is done.")
            return
        if not paths:
            return
        if len(paths) > 1:
            self.say(f"{len(paths)} files dropped: taking "
                     f"{os.path.basename(paths[0])}.")
        self.take(paths[0])

    def _follow(self, var, value):
        """put `value` in `var` unless the user chose what is there: a
        second file dropped takes its own folder, a folder browsed or typed
        stays"""
        if not var.get() or var.get() == self.auto.get(str(var)):
            var.set(value)
            self.auto[str(var)] = value

    def _pick_split(self):
        p = filedialog.askopenfilename(title="A file to split")
        if p:
            self._set_split(p)

    def _set_split(self, p):
        self.s_in.set(p)
        self._follow(self.s_out, os.path.dirname(p))
        self.s_name.set(suggest_name(p))
        self._plan()

    def _plan(self):
        p = self.s_in.get()
        try:
            n = os.path.getsize(p)
            k = estimate(n, self.s_size.get())
            self.s_plan.set(f"{n:,} bytes: at most {k} part(s) of "
                            f"{CZ.part_size(self.s_size.get()):,} bytes - "
                            "fewer if it compresses.")
        except (OSError, CZ.CZError) as e:
            self.s_plan.set(str(e) if p else "Choose a file to split.")

    def _pick_sout(self):
        d = filedialog.askdirectory(title="Where the parts go")
        if d:
            self.s_out.set(d)

    def _pick_join(self):
        p = filedialog.askopenfilename(
            title="Any part of a set",
            filetypes=[("Parts", "*.0?? *.1?? *.2?? *.3?? *.4?? *.5?? *.6?? "
                        "*.7?? *.8?? *.9??"), ("Any file", "*")])
        if p:
            self._set_join(p)

    def _set_join(self, p):
        self.j_in.set(p)
        self._follow(self.j_out, os.path.dirname(p))
        try:
            self.j_info.set(CZ.describe(open(p, "rb").read()))
        except (OSError, CZ.CZError) as e:
            self.j_info.set(str(e))

    def _pick_jout(self):
        d = filedialog.askdirectory(title="Where the joined file goes")
        if d:
            self.j_out.set(d)

    def _pick_pack(self):
        p = filedialog.askopenfilename(title="A file to pack or unpack")
        if p:
            self._set_pack(p)

    def _set_pack(self, p):
        self.p_in.set(p)
        try:
            blob = open(p, "rb").read()
            if CZ.kind(blob) == "plain" and len(blob) > CZ.CZ_OPENMAX:
                self.p_info.set(
                    f"{len(blob):,} bytes: too big to pack. The machine "
                    f"expands a 'CZ' file whole, in memory, and no machine "
                    f"has more than {CZ.CZ_OPENMAX // 1024}KB to give it - "
                    "use the Split tab, whose sets join in 82KB whatever "
                    "their size.")
                self.p_out.set("")
                return
            self.p_info.set(CZ.describe(blob))
            self.p_out.set(p[:-3] if CZ.kind(blob) == "cz" and
                           p.upper().endswith(".CZ") else
                           (p + ".OUT" if CZ.kind(blob) == "cz" else p + ".CZ"))
        except (OSError, CZ.CZError) as e:
            self.p_info.set(str(e))

    def _pick_pout(self):
        p = filedialog.asksaveasfilename(title="Write to")
        if p:
            self.p_out.set(p)

    # --- doing ---------------------------------------------------------------
    def say(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _progress(self, done, total):
        self.q.put(("bar", done, total))

    def _run(self, what, fn):
        if self.busy:
            return
        self.busy = True
        for b in (self.s_go, self.j_go, self.p_go, self.u_go):
            b.state(["disabled"])
        self.bar["value"] = 0
        self.say(what + "...")

        def work():
            try:
                self.q.put(("say", fn()))
            except CZ.CZError as e:
                self.q.put(("err", str(e)))
            except Exception:
                self.q.put(("err", traceback.format_exc()))
            self.q.put(("done",))
        threading.Thread(target=work, daemon=True).start()

    def _pump(self):
        try:
            while True:
                m = self.q.get_nowait()
                if m[0] == "bar":
                    self.bar["value"] = 1000 * m[1] // max(m[2], 1)
                elif m[0] == "say":
                    self.say(m[1])
                elif m[0] == "err":
                    self.say("REFUSED: " + m[1])
                    messagebox.showerror("os88cz", m[1])
                elif m[0] == "drop":
                    self.dropped(m[1])
                elif m[0] == "done":
                    self.busy = False
                    for b in (self.s_go, self.j_go, self.p_go, self.u_go):
                        b.state(["!disabled"])
        except queue.Empty:
            pass
        self.root.after(100, self._pump)

    def do_split(self):
        src, out = self.s_in.get(), self.s_out.get() or None
        name, size = self.s_name.get(), self.s_size.get()
        meth, imgs = self.s_meth.get(), self.s_img.get()

        def fn():
            nm, n, parts = CZ.split_file(src, out, size, meth, name,
                                         self._progress)
            tot = sum(os.path.getsize(p) for p in parts)
            lines = [f"{nm}: {n:,} bytes -> {len(parts)} part(s), "
                     f"{tot:,} bytes ({tot / n:.1%})"]
            lines += [f"  {os.path.basename(p)}  {os.path.getsize(p):,}"
                      for p in parts]
            if imgs:
                if size.lower() in CZ.GEOM_KB:
                    lines += [f"  {os.path.basename(p)}" for p in
                              CZ.make_images(parts, size, out)]
                else:
                    lines.append("  (no images: a size in bytes is not a "
                                 "disk)")
            lines.append(f"On the machine: copy every part into ONE folder "
                         f"and Uncompress {CZ.part_name(nm, 1)} "
                         "(SPEC.md 22.23.5).")
            return "\n".join(lines)
        self._run("Splitting " + os.path.basename(src), fn)

    def do_join(self):
        src, out = self.j_in.get(), self.j_out.get() or None

        def fn():
            p, n = CZ.join_file(src, out, self._progress)
            return f"{p}: {n:,} bytes"
        self._run("Joining", fn)

    def do_pack(self):
        src, out = self.p_in.get(), self.p_out.get()
        meth = CZ.M_LZB if self.p_lzb.get() else CZ.M_LZ4

        def fn():
            d = open(src, "rb").read()
            z = CZ.pack(d, meth)
            open(out or src + ".CZ", "wb").write(z)
            return (f"{out}: {len(d):,} -> {len(z):,} bytes "
                    f"({len(z) / len(d):.1%})")
        self._run("Packing", fn)

    def do_unpack(self):
        src, out = self.p_in.get(), self.p_out.get()

        def fn():
            blob = open(src, "rb").read()
            if CZ.kind(blob) == "part":
                p, n = CZ.join_file(src, os.path.dirname(out) or None,
                                    self._progress)
                return f"{p}: {n:,} bytes (a part: the set was joined)"
            d = CZ.unpack(blob)
            open(out, "wb").write(d)
            return f"{out}: {len(d):,} bytes"
        self._run("Unpacking", fn)


def main():                                                  # pragma: no cover
    if tk is None:
        sys.exit("os88czgui: this Python has no tkinter - `os88cz.py` is the "
                 "same tool on the command line")
    root = os88drop.make_root()         # drag and drop, where installed
    app = App(root, sys.argv[1] if len(sys.argv) > 1 else None)
    how = os88drop.enable_drop(root, lambda paths: app.q.put(("drop", paths)))
    if how:
        app.say("Drop a file on this window: a part joins, a 'CZ' file "
                "unpacks, anything else splits.")
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
