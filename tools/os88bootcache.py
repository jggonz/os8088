#!/usr/bin/env python3
"""os88bootcache: a WARM BOOT shared across rows (docs/plans/soak-speed-2/core.md).

Every `os88ui.boot` pays a whole boot - a MartyPC process started, the ROM
POST, the kernel and SYSTEM.CFG off the floppy, the desktop drawn: 15-30 guest
seconds, which under a four-wide soak is 5-25 host seconds of a row whose
subject is nearly never the boot. Most rows boot the SAME pair of images on
the SAME machine. This boots that pair once, holds the machine at the end of
its boot as a fork snapshot (debug_server.rs `snapshot`: a copy-on-write
process, nothing serialized, nothing that can be left out), and every later
row with the same key `restore`s a copy of it in ~0.1 s.

    OS88_BOOTCACHE=1 python3 tests/<row>.py      # opt in; os88ui.boot uses it

OPT-IN, because a restored machine is not a fresh boot in two ways a row
could see:

  * its guest clock does not start at 0 - it is the snapshot's (cycles and
    [ticks] continue from the end of the boot). Exactly as if the boot had
    happened; it did, once.
  * the boot's guest work is not REPEATED. A row whose subject is the boot
    (a boot time, a boot-path breakpoint, a disk count from reset) must not
    use os88ui.boot under this - and those rows use `launch` directly anyway,
    which this never touches.

THE KEY is everything that decides the machine at the end of its boot: the
machine name, the bytes of both floppies (sha1 of the file the launch would
copy, after $OS88_BUILD), the emulator binary and its configs/ROMs (path,
size, mtime), the gate card and the saver choice. Anything that this cannot
key on - an extra command line, a ROM in U28, a WAV capture, a hard disk
(a VHD is written THROUGH, so it is not rolled back by a fork), a PicoMEM,
86Box/Bochs - is not cached: `eligible` says why and the row boots fresh.

THE DISKS ARE THE ROW'S OWN. A forked machine's floppies are RAM (nothing in
MartyPC writes an image back but `flush`), so a restore starts from the
snapshot's sectors and its writes are private to that process. What is not
private is the server's idea of WHERE a flush with no path goes - the cache
registry's run tree - so a restored Marty here gets its own run tree with its
own copies, and `flush()`/`mount()` default and resolve against THAT.

THE REGISTRY is an instance, owned by the row that boots it until the
snapshot is held and DETACHED after (`os88marty.py instances` shows it,
label `bootcache`), paused, holding the snapshot. Entries idle for
OS88_BOOTCACHE_IDLE_MIN minutes (default 20) or beyond OS88_BOOTCACHE_MAX
(default 6) are killed by the next row that uses the cache;
`python3 tools/os88bootcache.py drop` kills them all, `list` shows them, and
`check IMG [APPS] [--machine M]` is the self-check: a cached and a fresh boot
of the same key compared on the guest's desktop state.
"""
import fcntl
import hashlib
import json
import os
import re
import signal
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88build as _build              # noqa: E402
import os88marty                        # noqa: E402
from os88marty import Marty, MartyError  # noqa: E402

VERSION = 1
IDLE_MIN = float(os.environ.get("OS88_BOOTCACHE_IDLE_MIN", "20"))
MAX_ENTRIES = int(os.environ.get("OS88_BOOTCACHE_MAX", "6"))


def enabled():
    return os.environ.get("OS88_BOOTCACHE", "0") not in ("", "0")


def root():
    d = os.path.join(os.path.dirname(os88marty._inst_root()), "bootcache")
    os.makedirs(d, exist_ok=True)
    return d


# launch() keywords a cached boot can honour. Anything else changes the
# machine or the process in a way the key does not cover.
_KW_OK = {"label", "timeout"}


def eligible(machine, kw):
    """None when (machine, launch kwargs) can be served from the cache, else
    the reason it cannot - one phrase, for the row's log."""
    if not enabled():
        return "OS88_BOOTCACHE is off"
    try:
        if os88marty.emulator() != "marty":
            return "not MartyPC"
    except MartyError as e:
        return str(e)
    for v in ("OS88_ROM", "MARTYPC_WAV", "OS88_MEGA"):
        if os.environ.get(v):
            return "$%s is set" % v
    if re.search(r"hdd|ide|_pm[12]|wire|ne2k|lpt", machine or "", re.I):
        return "machine %s has a written-through disk or a device" % machine
    for k, v in kw.items():
        if k == "boot" and v is False:
            continue
        if k not in _KW_OK:
            return "launch(%s=...)" % k
    return None


def _stamp(path):
    try:
        st = os.stat(path)
    except OSError:
        return [path, None]
    return [os.path.realpath(path), st.st_size, st.st_mtime_ns]


def key(image, apps, machine, card=None, saver=False):
    base = os88marty.base_run_dir()
    parts = {"v": VERSION, "machine": machine, "card": card,
             "saver": bool(saver),
             "image": os88marty._sha1(_build.at(image)) if image else None,
             "apps": os88marty._sha1(_build.at(apps)) if apps else None,
             "bin": _stamp(os.environ.get("OS88_MARTY_BIN")
                           or os.path.join(base, "martypc_headless"))}
    tree = []
    for sub in ("configs", os.path.join("media", "roms")):
        top = os.path.join(base, sub)
        for dp, _dn, fn in os.walk(top, followlinks=True):
            for f in sorted(fn):
                p = os.path.join(dp, f)
                tree.append([os.path.relpath(p, base)] + _stamp(p)[1:])
    parts["tree"] = sorted(tree)
    h = hashlib.sha1(json.dumps(parts, sort_keys=True).encode()).hexdigest()
    return h[:16], parts


class _Lock(object):
    def __init__(self, path):
        self.path = path

    def __enter__(self):
        self.f = open(self.path, "a+")
        fcntl.flock(self.f, fcntl.LOCK_EX)
        return self

    def __exit__(self, *e):
        fcntl.flock(self.f, fcntl.LOCK_UN)
        self.f.close()


def _read(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def _write(path, rec):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(rec, f, indent=1, sort_keys=True)
    os.replace(tmp, path)


def _alive(rec):
    return bool(rec) and os88marty._is_marty(rec.get("pid"),
                                             rec.get("pid_start"))


def _kill(rec):
    pid = (rec or {}).get("pid")
    if pid and _alive(rec):
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass


def entries():
    out = []
    r = root()
    for name in sorted(os.listdir(r)):
        if name.endswith(".json"):
            rec = _read(os.path.join(r, name))
            if rec:
                rec["_path"] = os.path.join(r, name)
                rec["alive"] = _alive(rec)
                out.append(rec)
    return out


def _sweep(keep):
    """Kill entries idle past IDLE_MIN, and the least recently used beyond
    MAX_ENTRIES. `keep` (a key) is never swept."""
    now = time.time()
    live = []
    for rec in entries():
        if rec.get("key") == keep:
            continue
        if not rec["alive"] or now - rec.get("used", 0) > IDLE_MIN * 60:
            with _Lock(rec["_path"] + ".lock"):
                _kill(rec)
                try:
                    os.remove(rec["_path"])
                except OSError:
                    pass
            continue
        live.append(rec)
    live.sort(key=lambda r: r.get("used", 0), reverse=True)
    for rec in live[max(0, MAX_ENTRIES - 1):]:
        with _Lock(rec["_path"] + ".lock"):
            _kill(rec)
            try:
                os.remove(rec["_path"])
            except OSError:
                pass


def _make(k, parts, image, apps, machine, card, saver):
    """A MISS: boot once, detached, to the end of the boot; snapshot; let go."""
    t0 = time.time()
    # OWNED while it boots - a row killed mid-boot leaves an orphan `reap`
    # takes - and DETACHED only once the snapshot is held
    m = os88marty.launch(image, apps=apps, machine=machine, boot=False,
                         label="bootcache", card=card, saver=saver)
    try:
        m.run()
        os88marty.kernel_up(m)
        if not saver:
            os88marty.no_saver(m)
        m.pause()
        snap = m.snapshot()
        rec = {"key": k, "parts": parts, "pid": m.pid, "port": m.port,
               "pid_start": os88marty._proc_start(m.pid), "snap": snap["id"],
               "cycles": snap.get("cycles"), "made": time.time(),
               "used": time.time(), "uses": 0, "run_dir": m.run_dir,
               "image": image, "apps": apps, "machine": machine,
               "boot_host_s": round(time.time() - t0, 2)}
    except BaseException:
        pid = m.pid
        m.close()
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
        raise
    inst = m._rec
    m._proc = m._rec = None     # close() now drops the connection only
    m._detached = True
    if inst is not None:
        inst["owner_pid"] = None
        inst["detached"] = True
        os88marty._write_record(inst)
    return rec, m               # the connection: the first restore uses it


class CachedMarty(Marty):
    """A Marty restored from the cache, OWNED by this process like a launch:
    its own run tree and disks, a registry record, killed on close."""

    def close(self):
        pid = self.pid
        Marty.close(self)
        rec, self._rec = getattr(self, "_rec", None), None
        if pid:
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
        if rec is not None:
            try:
                rec["ended"] = True
                rec["ended_at"] = time.time()
                os88marty._write_record(rec)
                os88marty._drop_instance(
                    rec, heavy_only=os88marty.KEEP_MINUTES > 0)
            except Exception:
                pass

    def _here(self, path):
        if path is None or os.path.isabs(path):
            return path
        return os.path.join(self.run_dir, path)

    def flush(self, drive=0, path=None, fmt="raw"):
        if path is None:
            path = self._mounted.get(drive)
        return Marty.flush(self, drive, self._here(path), fmt)

    def mount(self, drive, path):
        path = self._here(path)
        r = Marty.mount(self, drive, path)
        self._mounted[drive] = path
        return r


def _free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _registry(rec, secs=10.0):
    """A connection to the registry. The server takes ONE client and notices
    a departed one only when it next looks at that socket, so a connection
    closed a moment ago (the last row's, a miss's own) can still be "its
    client" for a few milliseconds: retry that refusal, briefly."""
    end = time.time() + secs
    while True:
        try:
            return Marty("127.0.0.1:%d" % rec["port"])
        except MartyError as e:
            if "already has a client" not in str(e) or time.time() > end:
                raise
            time.sleep(0.02)


def _restore(rec, image, apps, machine, saver, label, reg=None):
    if reg is None:
        reg = _registry(rec)
    try:
        last = None
        for _ in range(3):
            port = _free_port()
            try:
                reg.cmd(cmd="restore", id=rec["snap"], port=port)
                break
            except MartyError as e:
                last = e
        else:
            raise last
    finally:
        reg.close()             # the registry's _proc is None: it stays up
    r = None
    end = time.time() + 15.0
    while r is None:
        try:
            r = CachedMarty("127.0.0.1:%d" % port)
        except MartyError:
            if time.time() > end:
                raise MartyError("bootcache: restored snapshot never came up "
                                 "on port %d" % port)
            time.sleep(0.01)
    try:
        return _adopt(r, port, rec, image, apps, machine, saver, label)
    except BaseException:
        pid = r.pid
        Marty.close(r)
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
        raise


def _adopt(r, port, rec, image, apps, machine, saver, label):
    # ITS OWN RUN TREE, as launch would have made: copies of the floppies the
    # key was made from, where a flush with no path lands, and a record so
    # `instances` and `reap` see the process (owned by this one).
    tag = os88marty._tag(label or "bootcache-row")
    home = os.path.join(os88marty._inst_root(), tag)
    run_dir = home              # (launch's private tree IS its home too)
    os.makedirs(os.path.join(run_dir, "media", "floppies"), exist_ok=True)
    r._mounted = {}
    sha = {}
    for i, src in enumerate((image, apps)):
        if src is None:
            continue
        dst = os.path.join(run_dir, "media", "floppies", "run%d.img" % i)
        sha[str(i)] = os88marty._stage_floppy(_build.at(src), dst)
        r._mounted[i] = dst
    r.run_dir = run_dir
    r.port = port
    r.addr = "127.0.0.1:%d" % port
    r._detached = False
    r._wav = False
    r._proc = None
    r._bootcache = rec["key"]
    rr = {"pid": r.pid, "pid_start": os88marty._proc_start(r.pid),
          "port": port, "machine": machine,
          "image": os.path.abspath(_build.at(image)) if image else None,
          "apps": os.path.abspath(_build.at(apps)) if apps else None,
          "sha1": sha, "run_dir": run_dir,
          "log": os.path.join(rec.get("run_dir") or run_dir, "martypc.log"),
          "label": (label or "row") + "+bootcache", "owner_pid": os.getpid(),
          "detached": False, "started": time.time(), "private": True,
          "ended": False, "dir": home, "bootcache": rec["key"]}
    os88marty._write_record(rr)
    r._rec = rr
    os88marty._saver_arm(r, not saver)
    r.run()                     # a restore comes up as it was held: paused
    return r


def launch(image, apps=None, machine="os8088_5150_cga", card=None,
           saver=False, label=None, verbose=True, **_kw):
    """A RUNNING Marty at the end of the boot of (image, apps, machine):
    restored from the cache, made on a miss. The caller has checked
    `eligible`."""
    if label is None:
        label = os.path.basename(getattr(sys.modules.get("__main__"),
                                         "__file__", "") or "row")
    k, parts = key(image, apps, machine, card, saver)
    path = os.path.join(root(), k + ".json")
    t0 = time.time()
    with _Lock(path + ".lock"):
        rec = _read(path)
        hit = _alive(rec)
        reg = None
        if not hit:
            _kill(rec)
            rec, reg = _make(k, parts, image, apps, machine, card, saver)
            _write(path, rec)   # NOW: a failed restore must not leak it
        r = _restore(rec, image, apps, machine, saver, label, reg=reg)
        rec["used"] = time.time()
        rec["uses"] = rec.get("uses", 0) + 1
        _write(path, rec)
    try:
        _sweep(k)
    except Exception:
        pass
    if verbose:
        print("      bootcache %s %s (%.2f host s)"
              % (k, "hit" if hit else "MISS, booted", time.time() - t0))
    return r


def drop():
    for rec in entries():
        with _Lock(rec["_path"] + ".lock"):
            _kill(rec)
            try:
                os.remove(rec["_path"])
            except OSError:
                pass
            print("dropped %s (%s)" % (rec.get("key"), rec.get("machine")))


def check(image, apps, machine):
    """THE SELF-CHECK: a cached restore against a fresh boot of the same key,
    on the desktop STATE a row starts from. Prints what differs; rc 1 if any.

    Compared after both are `kernel_up` + `ui_done`, so the window list, the
    menu bar, the desktop's drive zones, the heap's top and the mouse cell
    must agree; [ticks] and the cycle count are expected to differ and are
    not compared."""
    import os88sym
    names = ("desk_rows", "menu_nbar", "spl_live", "ss_idle", "mouse_x",
             "mouse_y", "mouse_btn", "dsk_vtab", "win_count", "win_top",
             "wm_zorder", "mem_top", "heap_top")
    os.environ["OS88_BOOTCACHE"] = "1"
    machine = os88marty.machine(machine)

    def snap(m):
        os88marty.ui_done(m)
        out = {}
        for n in names:
            try:
                a = os88sym.linear(n)
            except (Exception, SystemExit):
                continue
            out[n] = bytes(m.read(a, 16)).hex()
        os88marty.guest_sleep(m, 0.1)       # the glass lags by a frame
        w, h, rgb = m.fbuf()
        out["fbuf"] = "%dx%d %s" % (w, h, hashlib.sha1(rgb).hexdigest())
        return out
    with launch(image, apps, machine) as c:
        a = snap(c)
    with os88marty.launch(image, apps=apps, machine=machine,
                          boot=False) as f:
        f.run()
        os88marty.kernel_up(f)
        os88marty.no_saver(f)
        b = snap(f)
    bad = [n for n in a if a.get(n) != b.get(n)]
    for n in sorted(set(a) | set(b)):
        print("%-10s %s %s" % (n, "ok  " if a.get(n) == b.get(n) else "DIFF",
                               a.get(n) if a.get(n) == b.get(n)
                               else "%s / fresh %s" % (a.get(n), b.get(n))))
    return 1 if bad else 0


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sp = ap.add_subparsers(dest="verb", required=True)
    sp.add_parser("list")
    sp.add_parser("drop")
    c = sp.add_parser("check")
    c.add_argument("image")
    c.add_argument("apps", nargs="?")
    c.add_argument("--machine", default="os8088_5150_cga")
    a = ap.parse_args(argv)
    if a.verb == "list":
        for rec in entries():
            print("%s %s pid %s port %s uses %s %s %s" % (
                rec.get("key"), "alive" if rec["alive"] else "DEAD",
                rec.get("pid"), rec.get("port"), rec.get("uses"),
                rec.get("machine"), rec.get("image")))
        return 0
    if a.verb == "drop":
        drop()
        return 0
    return check(a.image, a.apps, a.machine)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
