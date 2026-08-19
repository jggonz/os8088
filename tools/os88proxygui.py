#!/usr/bin/env python3
"""The os8088 proxy with a window on it - and the Windows .exe (PROXY-PLAN 13).

    python3 tools/os88proxygui.py          # ...or double-click os88proxy.exe

Same engine, different face. **It imports `os88proxy` and does not reimplement
one line of it**: the settings become the same argv `parse_args` reads on the
command line, the server is the same `ThreadingHTTPServer`, and the log is the
same `say`. That is SPEC.md 20.5.1's rule about `os88ui.inc` - one source for
two worlds - at the scale of a host tool, and it is the whole reason this file
is short.

It exists for ONE PERSON: a tester with an os8088 machine and no Python.
So the window answers the three questions that person actually has, in
this order:

    1. what do I type on os8088?          - the address, big, with a Copy
    2. is it working?                       - the sites list, filling up
    3. why is that page not loading?        - the same list, with the failure

Everything else is a setting with a sensible default. tkinter because it is in
the standard library on every Windows Python: one file, no pip, and PyInstaller
bundles it without being asked.
"""
import json
import os
import queue
import socket
import sys
import threading
import time
import traceback
from http.server import ThreadingHTTPServer

# TK IS IMPORTED SOFTLY, and that is a testing decision rather than a
# tolerance: the settings table and the command line it builds are ordinary
# data, and a machine with no Tk (this project's own container, whose python
# is not the one apt installs for) could not import this file to check them -
# so tests/proxyguitest.py SKIPPED WHOLE, and a seven-long row unpacked into
# six variables reached the field as `too many values to unpack` on Start.
# `main` still refuses without Tk; everything above it now loads anywhere.
try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except Exception:                                            # pragma: no cover
    tk = None
    filedialog = messagebox = ttk = None

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
import os88proxy as P                                         # noqa: E402

APPNAME = "os8088 proxy"
# Every control on the window, as (argv flag, widget, choices, hint, TAB) -
# the ONE place a setting is described. The form, the save file and the argv
# are all built from it, so adding a setting is adding a row here and nothing
# else - and a first-class site is a TAB, because the reddit rows had already
# pushed the general ones off the bottom of a laptop screen and there are four
# or five more sites to come.
#   key       label                       kind     default   choices  hint  tab
FIELDS = [
    ("port", "Port", "int", "8088", None,
     "os8088 connects to this. 8088 is a fine joke and a fine default.", "General"),
    ("cols", "os8088's screen", "choice", "CGA - 79 columns",
     [("CGA - 79 columns", "79"), ("Hercules - 89 columns", "89"),
      ("VGA - 79 columns", "79")],
     "Only changes where code blocks and wide tables are wrapped.", "General"),
    ("budget", "Page size", "choice", "22 KB - a good default",
     [("12 KB - fastest", "12288"), ("22 KB - a good default", "22528"),
      ("31 KB - the browser's limit", "31744")],
     "Bigger pages mean fewer of them and a longer wait for each.", "General"),
    ("strip", "Strip the page", "choice", "Full - drop menus and adverts",
     [("Full - drop menus and adverts", "full"),
      ("Light - keep more of it", "light")],
     "Every page has a Full link in its footer that undoes this once.", "General"),
    ("tables", "Tables and columns", "choice", "Rearrange into tables",
     [("Rearrange into tables", "auto"), ("Leave them alone", "off")], "", "General"),
    ("images", "Pictures", "choice", "Show the caption",
     [("Show the caption", "mark"), ("Leave them out", "drop")],
     "The browser cannot draw a picture; a caption is all there is.", "General"),
    ("reddit_api", "Reddit source", "choice", "Reddit's own JSON feed",
     [("Reddit's own JSON feed", "json"), ("The web page", "html")],
     "The JSON feed needs no account and no JavaScript. The page is guarded "
     "by a JavaScript check this proxy answers, but the feed is the sure "
     "thing.", "Reddit"),
    ("reddit_site", "Reddit", "choice", "www.reddit.com - no sign-in needed",
     [("www.reddit.com - no sign-in needed", "www"),
      ("old.reddit.com - wants a session", "old")],
     "www needs no account to read. old is smaller on the wire and shows a "
     "login wall to a signed-out reader.", "Reddit"),
    ("reddit_thread", "Reddit replies", "choice", "Quote marks (>)",
     [("Quote marks (>)", "quote"), ("Bars (|)", "bar"),
      ("A line between threads", "rule"), ("A table", "table")],
     "How a reply tree is drawn. The table is the easiest to follow and the "
     "slowest to draw; try them on the machine.", "Reddit"),
    ("reddit_indent", "Reddit reply indent", "choice", "1 character",
     [("None - flat", "0"), ("1 character", "1"), ("2 characters", "2")], "", "Reddit"),
    ("cookies", "Cookies file", "file", "", None,
     "Optional, and the ONLY way to be signed in: a cookies.txt exported "
     "from a desktop browser already signed in to the site.", "Reddit"),
    ("save_dir", "Save pages to", "dir", "", None,
     "Optional. Writes what the site sent and what os8088 got, side by "
     "side - which is what to send back when a page comes out wrong.", "General"),
    ("allow", "Only allow", "text", "", None,
     "Blank means anyone who can reach the port. `192.168.1.*` is a machine "
     "or a network.", "General"),
    ("public_base", "Address to advertise", "text", "", None,
     "Blank is right unless os8088 reaches this machine by another name.", "General"),
    ("gate", "Check every page against the browser's limits", "bool", "1",
     None, "Leave this on. It is what catches a page os8088 would read "
           "wrongly without saying so.", "General"),
]


# EVERY ROW IS SEVEN LONG from here down. The tab was added as an optional
# seventh element and two loops still unpacked six, which is a crash on Start
# and nothing at import - `too many values to unpack (expected 6)`, from the
# field. Normalising here means a row can gain an eighth element without
# hunting for the loops that would break.
FIELDS = [tuple(r) + (("General",) if len(r) < 7 else ()) for r in FIELDS]


def argv_from(values):
    """The form's VALUES -> the command line. No widgets, so a machine with
    no Tk can still check that every default this window can produce is one
    `parse_args` accepts - which is what the crash above needed and the
    skipped GUI test could not give."""
    out = []
    for key, label, kind, default, choices, hint, tab in FIELDS:
        val = values.get(key, default)
        if kind == "choice":
            val = dict(choices).get(val, dict(choices)[default])
        val = (val or "").strip()
        if kind == "bool":
            if val != "1":
                out.append("--no-gate")
            continue
        if not val:
            continue
        out += ["--" + key.replace("_", "-"), val]
    return out


def crash_log(text):
    """Append a traceback to `crash.log` beside the settings.

    **This was CALLED from two places and never defined**, so the one path
    that exists to explain a crash raised a `NameError` inside the reporting
    of the first crash - which is the shape it was written to avoid, and the
    field met both halves at once on Quit. Best effort by design: a reporter
    that can itself fail is worse than no reporter, so every failure here is
    swallowed and the caller's own message still reaches the window.
    """
    try:
        d = settings_dir()
        if not d:
            return ""
        path = os.path.join(d, "crash.log")
        with open(path, "a", encoding="utf-8", errors="replace") as fh:
            fh.write("---- %s ----\n%s\n"
                     % (time.strftime("%Y-%m-%d %H:%M:%S"), text))
        return path
    except Exception:
        return ""


def settings_dir():
    base = (os.environ.get("APPDATA") or os.path.expanduser("~"))
    d = os.path.join(base, "os8088proxy" if os.name == "nt" else ".os8088proxy")
    try:
        os.makedirs(d, exist_ok=True)
    except Exception:
        return None
    return d


def lan_address():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))          # no packet is sent
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


class App(object):
    def __init__(self, root):
        self.root = root
        self.q = queue.Queue()
        self.httpd = None
        self.proxy = None
        self.thread = None
        self.vars = {}
        self.detail = tk.BooleanVar(value=False)
        root.title(APPNAME)
        root.minsize(760, 560)
        root.geometry("880x680")
        self._build()
        self._load_settings()
        self._pump()

    # --- the window --------------------------------------------------------
    def _build(self):
        pad = dict(padx=8, pady=4)
        top = ttk.Frame(self.root)
        top.pack(fill="x", **pad)

        self.btn = ttk.Button(top, text="Start", width=12, command=self.toggle)
        self.btn.pack(side="left")
        self.state = ttk.Label(top, text="Stopped", font=("TkDefaultFont", 10, "bold"))
        self.state.pack(side="left", padx=12)

        addr = ttk.LabelFrame(self.root,
                              text="On os8088: Browser > Open Location")
        addr.pack(fill="x", **pad)
        self.addr = tk.Entry(addr, font=("TkFixedFont", 16), justify="center",
                             relief="flat", state="readonly",
                             readonlybackground="white")
        self.addr.pack(side="left", fill="x", expand=True, padx=8, pady=8)
        ttk.Button(addr, text="Copy", command=self.copy_addr).pack(side="right",
                                                                   padx=8)

        nb = ttk.Notebook(self.root)
        nb.pack(fill="x", **pad)
        self.form = nb
        tabs, rows = {}, {}
        for row in FIELDS:
            tab = row[6]
            if tab not in tabs:
                f = ttk.Frame(nb)
                f.columnconfigure(2, weight=1)
                nb.add(f, text=tab)
                tabs[tab], rows[tab] = f, 0
        for key, label, kind, default, choices, hint, tab in FIELDS:
            box = tabs[tab]
            i = rows[tab]
            rows[tab] = i + 1
            # GRID rather than a Frame a row: packed rows each size themselves,
            # so the hints started at a different x on every line and the
            # widest one pushed the window past the edge of the screen.
            v = tk.StringVar(value=default)
            if kind == "bool":
                w = ttk.Checkbutton(box, text=label, variable=v,
                                    onvalue="1", offvalue="0")
                w.grid(row=i, column=0, columnspan=2, sticky="w", padx=8, pady=2)
            else:
                ttk.Label(box, text=label).grid(row=i, column=0, sticky="w",
                                                padx=(8, 6), pady=2)
                if kind == "choice":
                    w = ttk.Combobox(box, textvariable=v, state="readonly",
                                     values=[c[0] for c in choices], width=26)
                else:
                    w = ttk.Entry(box, textvariable=v, width=28)
                w.grid(row=i, column=1, sticky="w", pady=2)
                if kind in ("file", "dir"):
                    ttk.Button(box, text="Browse...", width=9,
                               command=lambda vv=v, k=kind: self.browse(vv, k)
                               ).grid(row=i, column=3, padx=6)
            if hint:
                ttk.Label(box, text=hint, foreground="#555", wraplength=290,
                          justify="left").grid(row=i, column=2, sticky="w",
                                               padx=10)
            self.vars[key] = (v, w)

        seen = ttk.LabelFrame(self.root, text="Sites visited")
        seen.pack(fill="both", expand=True, **pad)
        self.log = tk.Text(seen, height=12, wrap="none", state="disabled",
                           font=("TkFixedFont", 9), background="#111111",
                           foreground="#d0d0d0", insertbackground="#d0d0d0")
        sb = ttk.Scrollbar(seen, orient="vertical", command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.log.pack(side="left", fill="both", expand=True)
        self.log.tag_configure("page", foreground="#9ad")
        self.log.tag_configure("ok", foreground="#8c8")
        self.log.tag_configure("bad", foreground="#f88")
        self.log.tag_configure("note", foreground="#888")

        foot = ttk.Frame(self.root)
        foot.pack(fill="x", **pad)
        ttk.Checkbutton(foot, text="Show technical detail",
                        variable=self.detail).pack(side="left")
        ttk.Button(foot, text="Clear", command=self.clear).pack(side="left",
                                                                padx=8)
        ttk.Label(foot, text="os8088 proxy %s" % P.VERSION,
                  foreground="#666").pack(side="right")
        self.root.protocol("WM_DELETE_WINDOW", self.quit)
        self._show_addr()

    def browse(self, var, kind="file"):
        if kind == "dir":
            name = filedialog.askdirectory(title="Folder to save pages in")
        else:
            name = filedialog.askopenfilename(
                title="cookies.txt exported from a desktop browser",
                filetypes=[("Cookie files", "*.txt"), ("All files", "*.*")])
        if name:
            var.set(name)

    def copy_addr(self):
        self.root.clipboard_clear()
        self.root.clipboard_append(self.addr.get())

    def clear(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _show_addr(self):
        port = self.vars["port"][0].get().strip() or "8088"
        text = "%s:%s" % (lan_address(), port)
        self.addr.configure(state="normal")
        self.addr.delete(0, "end")
        self.addr.insert(0, text)
        self.addr.configure(state="readonly")

    # --- settings ----------------------------------------------------------
    def _settings_file(self):
        d = settings_dir()
        return os.path.join(d, "gui.json") if d else None

    def _load_settings(self):
        path = self._settings_file()
        if not path or not os.path.exists(path):
            return
        try:
            with open(path) as fh:
                saved = json.load(fh)
            for key, (var, _w) in self.vars.items():
                if key in saved:
                    var.set(saved[key])
            self.detail.set(bool(saved.get("_detail", False)))
        except Exception as exc:
            self.write("could not read saved settings: %s" % exc, "note")
        self._show_addr()

    def _save_settings(self):
        path = self._settings_file()
        if not path:
            return
        try:
            data = {k: v.get() for k, (v, _w) in self.vars.items()}
            data["_detail"] = bool(self.detail.get())
            with open(path, "w") as fh:
                json.dump(data, fh, indent=1)
        except Exception:
            pass

    def argv(self):
        """The form, as the command line. Nothing else knows about widgets."""
        return argv_from(dict((k, v[0].get()) for k, v in self.vars.items()))

    # --- running -----------------------------------------------------------
    def toggle(self):
        self.stop() if self.httpd else self.start()

    def start(self):
        try:
            opts = P.parse_args(self.argv())
        except SystemExit:
            messagebox.showerror(APPNAME, "One of the settings is not valid.")
            return
        except Exception as exc:
            messagebox.showerror(APPNAME, str(exc))
            return
        P.LOG_SINK = lambda m: self.q.put(("note", m))
        P.PAGE_SINK = lambda rec: self.q.put(("event", rec))
        try:
            self.proxy = P.Proxy(opts)
            self.httpd = P.Server((opts.bind, opts.port), P.Handler)
        except OSError as exc:
            # The one failure a tester will actually hit, in words rather than
            # in an errno: something else already has the port.
            self.httpd = None
            messagebox.showerror(
                APPNAME,
                "Could not listen on port %d.\n\n%s\n\nIf the proxy is already "
                "running in another window, use that one - or pick a different "
                "port here." % (opts.port, exc))
            return
        self.httpd.proxy = self.proxy
        self.httpd.daemon_threads = True
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()
        self._save_settings()
        self._show_addr()
        self.btn.configure(text="Stop")
        self.state.configure(text="Running", foreground="#070")
        for _key, (_v, w) in self.vars.items():
            try:
                w.configure(state="disabled")
            except Exception:
                pass
        self.write("ready - on os8088 type  %s" % self.addr.get(), "ok")
        self.write("Reddit needs no account. To read it as YOU, export a "
                   "cookies.txt and put it in the Reddit tab.", "note")
        if os.name == "nt":
            self.write("if Windows asks about the firewall, allow it on "
                       "PRIVATE networks only", "note")

    def _serve(self):
        try:
            self.httpd.serve_forever()
        except Exception:
            # A dead accept loop is the failure a tester reports as "it stopped
            # answering", so it is written down twice: in the window, in red,
            # whatever the detail box says, and in crash.log.
            text = traceback.format_exc()
            crash_log(text)
            self.q.put(("fatal", "the server stopped answering: %s"
                        % text.strip().splitlines()[-1]))

    def stop(self):
        if self.httpd:
            try:
                self.httpd.shutdown()
                self.httpd.server_close()
            except Exception:
                pass
        if self.proxy:
            self.proxy.links.save()
        self.httpd = self.proxy = None
        P.LOG_SINK = P.PAGE_SINK = None
        self.btn.configure(text="Start")
        self.state.configure(text="Stopped", foreground="")
        for _key, (_v, w) in self.vars.items():
            try:
                w.configure(state="readonly" if isinstance(w, ttk.Combobox)
                            else "normal")
            except Exception:
                pass
        self.write("stopped", "note")

    def quit(self):
        self._save_settings()
        self.stop()
        self.root.destroy()

    # --- the list ----------------------------------------------------------
    def write(self, text, tag="note"):
        self.log.configure(state="normal")
        self.log.insert("end", "%s  %s\n" % (time.strftime("%H:%M:%S"), text),
                        tag)
        # A tester leaves this open all day, so the box is BOUNDED - 2,000
        # lines of Tk text widget is a few hundred KB and stays scrollable.
        if int(self.log.index("end-1c").split(".")[0]) > 2000:
            self.log.delete("1.0", "500.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _event(self, rec):
        if rec.get("kind") == "route":
            self.write("%s  %s%s" % (rec.get("path", "?"), rec.get("status"),
                                     "" if rec.get("status") == 200 else " !"),
                       "note" if rec.get("status") == 200 else "bad")
            return
        url = rec.get("url", "")
        for pfx in ("https://", "http://"):
            if url.startswith(pfx):
                url = url[len(pfx):]
        st = rec.get("status", 200)
        bits = ["%s -> %s" % (P.human(rec.get("raw", 0)),
                              P.human(rec.get("out", 0)))]
        if rec.get("pages", 1) != 1:
            bits.append("%d pages" % rec["pages"])
        bits.append("from memory" if rec.get("cached")
                    else "%.1fs" % (rec.get("ms", 0) / 1000.0))
        # The numbers ride in brackets after the site rather than in padded
        # columns: TkFixedFont is not fixed on every platform, and a table
        # whose columns wander reads worse than a sentence that does not try.
        line = "%s  (%s)" % (P.short(url, 64), ", ".join(bits))
        if st != 200:
            line += "   [%s]" % st
        self.write(line, "page" if st == 200 else "bad")

    def _pump(self):
        try:
            while True:
                kind, payload = self.q.get_nowait()
                if kind == "event":
                    self._event(payload)
                elif kind == "fatal":
                    self.write(payload, "bad")
                elif self.detail.get() or "GATE:" in payload or \
                        "WARNING" in payload or "FAIL" in payload:
                    self.write(payload, "bad" if "GATE:" in payload else "note")
        except queue.Empty:
            pass
        # WATCHDOG: a server thread that has died must not look like a quiet
        # afternoon. The button and the label go back to Stopped so the next
        # press restarts it rather than doing nothing.
        if self.httpd is not None and self.thread is not None \
                and not self.thread.is_alive():
            self.write("the server thread is gone - press Start again", "bad")
            self.stop()
        self.root.after(150, self._pump)


def self_report(path):
    """What this binary IS, written to a FILE and not to stdout.

    A `--noconsole` PyInstaller exe has no stdout attached when it is started
    from a console, so a CI step that captured its output would capture
    nothing and pass - the failure this check exists to catch, wearing the
    check's own clothes. A file cannot be empty by accident.
    """
    rec = {"version": P.VERSION, "fold": "htmsim" if P.HAVE_HTMSIM else "crude",
           "python": sys.version.split()[0], "frozen": bool(getattr(sys, "frozen", False)),
           "tk": str(tk.TkVersion) if tk is not None else ""}
    with open(path, "w") as fh:
        json.dump(rec, fh, indent=1)
    return rec


def main():
    if "--self-report" in sys.argv:
        i = sys.argv.index("--self-report")
        path = sys.argv[i + 1] if len(sys.argv) > i + 1 else "os88proxy-report.json"
        print(self_report(path))
        return 0
    if tk is None:                                           # pragma: no cover
        sys.stderr.write("This needs Tk. On Linux: apt install python3-tk\n")
        return 2
    root = tk.Tk()
    try:
        root.call("ttk::style", "theme", "use", "vista")
    except Exception:
        pass
    app = App(root)

    def on_callback_error(*exc):
        text = "".join(traceback.format_exception(*exc))
        crash_log(text)
        app.write("internal error (written to crash.log): %s"
                  % text.strip().splitlines()[-1], "bad")
    root.report_callback_exception = on_callback_error
    try:
        root.mainloop()
    except KeyboardInterrupt:
        app.quit()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        # A frozen .exe has nowhere to print a traceback, and a window that
        # vanishes tells a tester nothing at all.
        try:
            import tkinter.messagebox as mb
            mb.showerror(APPNAME, traceback.format_exc())
        except Exception:
            pass
        raise
