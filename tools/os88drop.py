#!/usr/bin/env python3
"""os88drop - drop files on a Tk window, the best way this machine has.

Shared by the host-side windows (tools/os88vencgui.py, SPEC.md 98.2.12, and
tools/os88czgui.py, SPEC.md 20.17.4) so there is one copy of it. Tk has no
file drop of its own, so:

  - tkinterdnd2, where it is installed (any platform). The window's root must
    then be its TkinterDnD.Tk, which `make_root` answers;
  - on Windows without it, the shell's own WM_DROPFILES through ctypes, with
    nothing installed at all;
  - anywhere else, no drop - and `enable_drop` says so by answering None, so
    a window only offers what it has.

`got(paths)` is called ON TK'S THREAD, inside the event. Both windows only
QUEUE the paths there and take them in their pump, as they take a worker's
log.

Imports nothing from Tk at the top, so the tests that load a window module
on a machine with no display still load.
"""
import sys


def make_root():
    """the window's Tk root: tkinterdnd2's where it is installed, so the drop
    can be registered on it, else a plain one. Tk must be importable"""
    try:
        from tkinterdnd2 import TkinterDnD
        return TkinterDnD.Tk()
    except Exception:
        import tkinter
        return tkinter.Tk()


def hook_win_drop(root, got):
    """DRAG AND DROP ON WINDOWS with nothing installed: the shell's own
    WM_DROPFILES, through ctypes - the window's procedure subclassed so the
    message reaches us, every other message passed to the one Tk set.
    `got(paths)` is called ON TK'S THREAD, inside the message; the App only
    queues it. True when the hook is in"""
    import ctypes
    from ctypes import wintypes
    user32, shell32 = ctypes.windll.user32, ctypes.windll.shell32
    LRESULT = ctypes.c_ssize_t
    WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wintypes.HWND, wintypes.UINT,
                                 wintypes.WPARAM, wintypes.LPARAM)
    GWLP_WNDPROC, WM_DROPFILES = -4, 0x0233
    setlong = user32.SetWindowLongPtrW if ctypes.sizeof(
        ctypes.c_void_p) == 8 else user32.SetWindowLongW
    setlong.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
    setlong.restype = ctypes.c_void_p
    user32.CallWindowProcW.argtypes = [ctypes.c_void_p, wintypes.HWND,
                                       wintypes.UINT, wintypes.WPARAM,
                                       wintypes.LPARAM]
    user32.CallWindowProcW.restype = LRESULT
    shell32.DragAcceptFiles.argtypes = [wintypes.HWND, wintypes.BOOL]
    shell32.DragQueryFileW.argtypes = [ctypes.c_void_p, wintypes.UINT,
                                       ctypes.c_wchar_p, wintypes.UINT]
    shell32.DragQueryFileW.restype = wintypes.UINT
    shell32.DragFinish.argtypes = [ctypes.c_void_p]
    root.update_idletasks()
    # the frame Windows draws round Tk's window: a drop anywhere in it is
    # found by walking up from the child under the pointer
    hwnd = int(root.wm_frame(), 16)
    old = []

    def proc(h, msg, wp, lp):
        if msg == WM_DROPFILES:
            try:
                n = shell32.DragQueryFileW(wp, 0xFFFFFFFF, None, 0)
                paths = []
                for i in range(n):
                    size = shell32.DragQueryFileW(wp, i, None, 0) + 1
                    buf = ctypes.create_unicode_buffer(size)
                    shell32.DragQueryFileW(wp, i, buf, size)
                    paths.append(buf.value)
                got(paths)
            except Exception:
                pass
            finally:
                shell32.DragFinish(wp)
            return 0
        return user32.CallWindowProcW(old[0], h, msg, wp, lp)
    cb = WNDPROC(proc)
    old.append(setlong(hwnd, GWLP_WNDPROC, ctypes.cast(cb, ctypes.c_void_p)))
    if not old[0]:
        return False
    shell32.DragAcceptFiles(hwnd, True)
    root._os88_drop = cb                # ctypes frees a callback nobody holds
    return True


def enable_drop(root, got):
    """Drag and drop, the best way this machine has: tkinterdnd2 where it is
    installed (any platform - `root` must then be its TkinterDnD.Tk), else
    the shell's own on Windows. Returns how, or None"""
    try:
        from tkinterdnd2 import DND_FILES
        root.drop_target_register(DND_FILES)
        root.dnd_bind("<<Drop>>", lambda e: got(
            list(root.tk.splitlist(e.data))))
        return "tkinterdnd2"
    except Exception:
        pass
    if sys.platform == "win32":
        try:
            if hook_win_drop(root, got):
                return "windows"
        except Exception:
            pass
    return None
