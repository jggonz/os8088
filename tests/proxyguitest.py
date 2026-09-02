#!/usr/bin/env python3
"""The window builds, starts the server from its own form, and lists a site.

    xvfb-run -a python3 tests/proxyguitest.py        # Linux
    python3 tests/proxyguitest.py                    # Windows / macOS

It drives the real Tk application - no mainloop, `root.update()` instead - so
what it proves is what a tester will see: the settings become an argv, the argv
becomes a running proxy, a page fetched through it appears in the Sites visited
box, and Stop puts the port back.

FOUR ASSERTIONS, and the third is the one worth having:

1. THE FORM BECOMES THE COMMAND LINE. Every control's value survives
   `parse_args`, so a setting cannot be one the engine ignores.
2. START LISTENS. On the port the form names, answering the guest's own
   HTTP/1.0 request.
3. THE SITE APPEARS, and it is the SITE - the upstream URL and its sizes, not
   the proxy path. A tester watching `/l/6q4mzt3a` scroll past learns nothing.
4. STOP RELEASES THE PORT - asserted by STARTING AGAIN, which is what a
   tester does, and not by binding a bare socket: that one fails on TIME_WAIT
   from the connection the test itself just made, which is a fact about the
   test and not about the app.

**THE FIRST HALF NEEDS NO TK AND ALWAYS RUNS.** The settings table and the
command line it builds are ordinary data, and skipping the whole file where
there is no Tk is how a seven-long FIELDS row unpacked into six variables
reached the field as `too many values to unpack` on the Start button - the
window drew perfectly and the one path nothing had exercised was the one every
tester presses first. So `check_table` runs everywhere and the WINDOW half
skips, loudly, where Tk is missing.
"""
import ast
import builtins
import os
import socket
import sys
import threading
import time
import http.server

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

import os88proxy as P                                          # noqa: E402
import os88proxygui as G                                       # noqa: E402

try:
    import tkinter as tk
except Exception as _exc:                                      # pragma: no cover
    tk = None


def check_names(path):
    """Names and `self.` attributes that are USED and never defined.

    Both halves of the field's crash-on-Quit are this shape and neither can be
    seen by importing the module: `crash_log(text)` was called from two places
    and **never written**, so the one path that exists to explain a crash
    raised inside the reporting of the first one; and `self.who.configure()`
    outlived the label it configured by three commits. Nothing runs on Quit
    until somebody quits, so nothing here had ever executed those lines.

    It is deliberately a SMALL check rather than a type checker: module-level
    names against the module's own globals and the builtins, and `self.X`
    against what the class assigns anywhere. Everything it cannot see - a
    getattr, a name bound in a comprehension - is simply not reported.
    """
    src = open(path, encoding="utf-8").read()
    tree = ast.parse(src, path)
    bad = []
    top = set(dir(builtins)) | {"__file__", "__name__", "__doc__", "__spec__"}
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                             ast.ClassDef)):
            top.add(node.name)
        elif isinstance(node, ast.Assign):
            for t in node.targets:
                for n in ast.walk(t):
                    if isinstance(n, ast.Name):
                        top.add(n.id)
        elif isinstance(node, (ast.Import, ast.ImportFrom)):
            for a in node.names:
                top.add((a.asname or a.name).split(".")[0])
        elif isinstance(node, ast.Try):          # the soft Tk import
            for sub in ast.walk(node):
                if isinstance(sub, (ast.Import, ast.ImportFrom)):
                    for a in sub.names:
                        top.add((a.asname or a.name).split(".")[0])
                elif isinstance(sub, ast.Assign):
                    for t in sub.targets:
                        for n in ast.walk(t):
                            if isinstance(n, ast.Name):
                                top.add(n.id)
    # every name BOUND anywhere inside a function counts as defined: this is
    # about globals that do not exist, not about scope analysis.
    local = set()
    for n in ast.walk(tree):
        if isinstance(n, ast.Name) and isinstance(n.ctx, (ast.Store,
                                                          ast.Del)):
            local.add(n.id)
        elif isinstance(n, ast.arg):
            local.add(n.arg)
        elif isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef,
                            ast.ClassDef)):
            local.add(n.name)
        elif isinstance(n, ast.ExceptHandler) and n.name:
            local.add(n.name)
        elif isinstance(n, (ast.Import, ast.ImportFrom)):
            for a in n.names:
                local.add((a.asname or a.name).split(".")[0])
    for n in ast.walk(tree):
        if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Load):
            if n.id not in top and n.id not in local:
                bad.append("%s:%d uses `%s`, which is never defined"
                           % (os.path.basename(path), n.lineno, n.id))
    # A class's OWN NAMESPACE is `self.x = …` plus its methods plus its
    # class-level constants (`OLD = "https://old.reddit.com"`) plus whatever
    # its bases in this file have - without the last three every constant in
    # the tree reads as undefined, which is a check nobody would keep.
    own = {}
    for node in ast.walk(tree):
        if not isinstance(node, ast.ClassDef):
            continue
        names = set(m.name for m in node.body
                    if isinstance(m, (ast.FunctionDef, ast.AsyncFunctionDef)))
        for m in node.body:
            if isinstance(m, ast.Assign):
                for t in m.targets:
                    if isinstance(t, ast.Name):
                        names.add(t.id)
            elif isinstance(m, ast.AnnAssign) and isinstance(m.target, ast.Name):
                names.add(m.target.id)
        for n in ast.walk(node):
            if (isinstance(n, ast.Attribute) and isinstance(n.ctx, ast.Store)
                    and isinstance(n.value, ast.Name) and n.value.id == "self"):
                names.add(n.attr)
        own[node.name] = names
    for node in ast.walk(tree):
        if not isinstance(node, ast.ClassDef):
            continue
        # A class that inherits from OUTSIDE this file (a request handler, a
        # parser, a Tk widget) has a namespace this check cannot see, so it is
        # skipped whole rather than reported wrongly - a gate with a page of
        # false alarms in it is one nobody reads.
        outside = [b for b in node.bases
                   if not (isinstance(b, ast.Name)
                           and (b.id in own or b.id == "object"))]
        if outside:
            continue
        stored = set(own[node.name])
        seen, todo = set(), [b.id for b in node.bases
                             if isinstance(b, ast.Name)]
        while todo:                        # bases in this file, transitively
            b = todo.pop()
            if b in seen or b not in own:
                continue
            seen.add(b)
            stored |= own[b]
        methods = stored
        for n in ast.walk(node):
            if (isinstance(n, ast.Attribute) and isinstance(n.ctx, ast.Load)
                    and isinstance(n.value, ast.Name) and n.value.id == "self"
                    and n.attr not in stored and n.attr not in methods):
                bad.append("%s:%d reads `self.%s`, which %s never assigns"
                           % (os.path.basename(path), n.lineno, n.attr,
                              node.name))
    return sorted(set(bad))


def check_table():
    """No widgets: the table, the argv it builds, and the tabs it names."""
    bad = []
    for r in G.FIELDS:
        if len(r) != 7:
            bad.append("FIELDS row %r is %d long, not 7 - a loop that unpacks "
                       "it will crash on Start" % (r[0], len(r)))
            continue
        key, label, kind, default, choices, hint, tab = r
        if kind == "choice":
            if not choices:
                bad.append("%s is a choice with no choices" % key)
            elif default not in dict(choices):
                bad.append("%s defaults to %r, which is not one of its "
                           "choices" % (key, default))
        if not tab:
            bad.append("%s has no tab" % key)
    if bad:
        # A malformed row is what `argv_from` unpacks, so asking it anything
        # now would raise INSIDE the check rather than report - which is a
        # gate that crashes instead of failing.
        return bad
    argv = G.argv_from({})
    try:
        opts = P.parse_args(argv)
    except SystemExit:
        return bad + ["the form's own defaults are not a legal command line: "
                      "%r" % (argv,)]
    for key, label, kind, default, choices, hint, tab in G.FIELDS:
        if kind == "bool":
            continue
        if not hasattr(opts, key):
            bad.append("--%s is not a setting the engine has"
                       % key.replace("_", "-"))
    # ...and one non-default, because a table that only works at its defaults
    # is a table nobody has changed a control in.
    argv2 = G.argv_from({"reddit_thread": "A table", "port": "9099"})
    try:
        o2 = P.parse_args(argv2)
    except SystemExit:
        return bad + ["a changed control does not survive: %r" % (argv2,)]
    if o2.reddit_thread != "table" or o2.port != 9099:
        bad.append("a changed control arrives as %r/%r"
                   % (o2.reddit_thread, o2.port))
    return bad

ORIGIN_PORT = 8399
PROXY_PORT = 8388
FIXTURE = os.path.join(HERE, "proxy", "modern.htm")


class Origin(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *a):
        pass

    def do_GET(self):
        body = open(FIXTURE, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def guest_fetch(path, port):
    req = ("GET %s HTTP/1.0\r\nHost: 127.0.0.1:%d\r\nUser-Agent: os8088\r\n\r\n"
           % (path, port)).encode()
    s = socket.create_connection(("127.0.0.1", port), 15)
    s.sendall(req)
    buf = b""
    while True:
        blk = s.recv(4096)
        if not blk:
            break
        buf += blk
    s.close()
    return buf


def pump(root, seconds=2.0):
    end = time.time() + seconds
    while time.time() < end:
        root.update()
        time.sleep(0.02)


def main():
    fails = check_table()
    for mod in ("os88proxygui.py", "os88proxy.py"):
        fails.extend(check_names(os.path.join(
            os.path.dirname(HERE), "tools", mod)))
    for f in fails:
        print("FAIL: %s" % f)
    print("the settings table: %s"
          % ("ok" if not fails else "%d FAILURE(S)" % len(fails)))
    if tk is None:
        print("SKIPPED (the window half): no Tk in this python")
        print("         Linux: apt install python3-tk, then xvfb-run -a "
              "python3 %s" % sys.argv[0])
        return 1 if fails else 0
    return _window(fails)


def _window(fails):
    # ...carrying whatever the table half already found, so one run reports
    # both rather than stopping at the first.
    fails = list(fails)
    threading.Thread(
        target=lambda: http.server.ThreadingHTTPServer(
            ("127.0.0.1", ORIGIN_PORT), Origin).serve_forever(),
        daemon=True).start()

    root = tk.Tk()
    root.withdraw()                       # no window on a test machine
    app = G.App(root)
    app.vars["port"][0].set(str(PROXY_PORT))
    app.vars["cols"][0].set("Hercules - 89 columns")
    app.vars["budget"][0].set("12 KB - fastest")
    app.vars["images"][0].set("Leave them out")
    root.update()

    # 1. the form IS the command line
    argv = app.argv()
    opts = P.parse_args(argv)
    print("argv: %s" % " ".join(argv))
    if opts.port != PROXY_PORT or opts.cols != 89 or opts.budget != 12288:
        fails.append("the form did not reach the options: %s" % argv)
    if opts.images != "drop":
        fails.append("a choice did not map to its flag: images=%s" % opts.images)
    # INDEXED, not unpacked: `check_table` above is the one place that
    # asserts a row's length, and it runs where there is no Tk. Unpacking here
    # as well means a row that grows an eighth element crashes the WINDOW half
    # instead of being reported by the half written to report it - which is
    # exactly how the seventh element (the tab) reached the field.
    for row in G.FIELDS:
        key = row[0]
        if not hasattr(opts, key):
            fails.append("the form has %r and the engine has no such option"
                         % key)

    # 2. it listens
    app.start()
    pump(root, 1.0)
    if app.httpd is None:
        print("FAILED to start; log follows:\n%s"
              % app.log.get("1.0", "end").strip())
        return 1

    # 3. a SITE appears in the box - the upstream URL, not the proxy path
    body = guest_fetch("/h/127.0.0.1:%d/page.html" % ORIGIN_PORT, PROXY_PORT)
    if b"<html>" not in body:
        fails.append("the proxy served nothing useful: %r" % body[:120])
    pump(root, 1.5)
    seen = app.log.get("1.0", "end")
    if ("127.0.0.1:%d/page.html" % ORIGIN_PORT) not in seen:
        fails.append("the site is not in the list:\n%s" % seen)
    if "->" not in seen:
        fails.append("no before/after sizes in the list")
    if "/l/" in seen or "/h/" in seen:
        fails.append("the list shows PROXY paths, which say nothing to a "
                     "tester:\n%s" % seen)
    print("Sites visited box:\n%s"
          % "\n".join("   " + l for l in seen.strip().splitlines()))

    # 4. Stop gives the port back - proved by starting again on it
    app.stop()
    pump(root, 0.5)
    app.start()
    pump(root, 0.5)
    if app.httpd is None:
        fails.append("Start after Stop failed - the port was not released")
    else:
        body = guest_fetch("/", PROXY_PORT)
        if b"os8088 proxy" not in body:
            fails.append("the restarted proxy does not answer: %r" % body[:80])
    app.stop()
    app.quit()

    print("\n%s" % ("FAILURES:\n  " + "\n  ".join(fails) if fails
                    else "proxyguitest: all good"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
