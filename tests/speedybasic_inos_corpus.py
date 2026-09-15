#!/usr/bin/env python3
"""Compile every shipped Speedy BASIC demo inside a booted os8088 guest.

The B: disk is a disposable writable copy.  QEMU is stopped through QMP
before the host parses that disk, so successful writes cannot be confused
with data still held in the emulator's block cache.
"""

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

import dispcp                                                # noqa: E402
from ethernet import Qemu                                    # noqa: E402
import os88disk                                              # noqa: E402
import os88geom                                              # noqa: E402
import os88qemu                                              # noqa: E402
import os88sym                                               # noqa: E402
import os88ui                                                # noqa: E402
from os88map import Syms                                     # noqa: E402


SYSTEM = "build/os8088.img"
APPS = "build/speedybasic.img"
MANIFEST = "apps/speedybasic/demos/MANIFEST.TXT"
SB_BUILD_DONE = 3
SB_BUILD_ERROR = 4


def u16(data, offset=0):
    return struct.unpack_from("<H", data, offset)[0]


def cstring(data):
    return bytes(data).split(b"\0", 1)[0].decode("latin-1")


def wait_for(condition, what, limit=90.0, interval=0.1):
    end = time.monotonic() + limit
    last = None
    while time.monotonic() < end:
        last = condition()
        if last:
            return last
        time.sleep(interval)
    raise RuntimeError("timed out waiting for %s (last value %r)" %
                       (what, last))


class Mouse:
    """QEMU serial mouse actions scoped to this test's private QMP socket."""

    def __init__(self, socket_path):
        self.socket_path = socket_path

    def run(self, *arguments):
        subprocess.run([sys.executable, "tools/mouse.py", self.socket_path]
                       + [str(value) for value in arguments], cwd=ROOT,
                       check=True, capture_output=True)

    def click(self, x, y):
        self.run("click", x, y)

    def repeat_click_current(self, count):
        commands = []
        for _ in range(count):
            commands += ["mouse_button 1", "sleep 0.08",
                         "mouse_button 0", "sleep 0.12"]
        subprocess.run([sys.executable, "tools/qmp.py", self.socket_path]
                       + commands, cwd=ROOT, check=True,
                       capture_output=True)

    def dblclick(self, x, y):
        # Both press pairs must share one QMP connection or the kernel's
        # nine-tick double-click window can expire between host processes.
        self.run("to", x, y)
        subprocess.run([
            sys.executable, "tools/qmp.py", self.socket_path,
            "mouse_button 1", "sleep 0.08", "mouse_button 0", "sleep 0.12",
            "mouse_button 1", "sleep 0.08", "mouse_button 0"
        ], cwd=ROOT, check=True, capture_output=True)


def windows(machine, symbol):
    return [win for win in os88geom.windows(machine, symbol) if win.visible]


def wait_window(machine, symbol, title, limit=45.0):
    def find():
        return next((win for win in windows(machine, symbol)
                     if win.title == title), None)
    return wait_for(find, "window %r" % title, limit)


def package_segment(machine, symbol, window):
    record = machine.read(symbol("wm_wins")
                          + window.i * os88geom.WIN_SIZE,
                          os88geom.WIN_SIZE)
    segment = u16(record, os88geom.W_SEG)
    if not segment or machine.read(segment << 4, 2) != b"O8":
        raise RuntimeError("%s has no live package segment" % window.title)
    return segment


def menu_pick(machine, mouse, symbol, menu_name, item_name):
    """Pick a live application menu by its decoded title and item text."""
    decoder = os88ui.UI(machine, mouse=object(), sym=symbol, verbose=False)
    cells = decoder.menus()
    matches = [(i, cell) for i, cell in enumerate(cells)
               if cell[0].upper() == menu_name.upper()]
    if len(matches) != 1:
        raise RuntimeError("menu %r is not unique in %r" %
                           (menu_name, [cell[0] for cell in cells]))
    cell_index, cell = matches[0]
    items = cell[3]
    choices = [i for i, item in enumerate(items)
               if item[0].upper() == item_name.upper()]
    if len(choices) != 1 or not items[choices[0]][1]:
        raise RuntimeError("item %r is absent/disabled in %r" %
                           (item_name, items))
    item_index = choices[0]
    bar_x = (cell[1] + cell[2]) // 2
    mouse.run("down", bar_x, os88geom.MBAR_H // 2)
    wait_for(lambda: u16(machine.read(symbol("menu_y1"), 2)),
             "%s menu to drop" % menu_name, 10.0)
    if machine.read(symbol("menu_cell"), 1)[0] != cell_index:
        mouse.run("up")
        raise RuntimeError("press on %s dropped another menu" % menu_name)
    item_x = u16(machine.read(symbol("menu_x1"), 2)) + 8
    item_y = (u16(machine.read(symbol("menu_y1"), 2)) + 1
              + item_index * os88geom.MENU_ITEM_H
              + os88geom.MENU_ITEM_H // 2)
    mouse.run("to", item_x, item_y)
    wait_for(lambda: u16(machine.read(symbol("menu_sel"), 2)) == item_index,
             "%s/%s to highlight" % (menu_name, item_name), 10.0)
    mouse.run("up")


def dialog_select(machine, mouse, symbol, name):
    rows = [row[0] for row in dispcp.snapshot(machine, symbol)]
    if name not in rows:
        raise RuntimeError("file dialog does not list %s; it lists %r" %
                           (name, rows))
    # Select through the visible list. QEMU holds a synthetic key long enough
    # for this fast guest to see an occasional repeat, so ArrowDown can skip a
    # row. The scroll bar has no repeat ambiguity: move its view until the
    # target is visible, then click the exact displayed row.
    dialog = wait_window(machine, symbol, "Open")
    cx, cy, _cx2, _cy2 = dialog.content
    target = rows.index(name)
    wanted_scroll = max(0, target - 5)
    if wanted_scroll:
        mouse.run("to", cx + 206, cy + 113)  # scroll bar's down arrow
        mouse.repeat_click_current(wanted_scroll)
        wait_for(lambda: u16(machine.read(symbol("fdlg_scrl"), 2))
                 == wanted_scroll,
                 "file dialog scroll for %s" % name, 15.0)
    mouse.click(cx + 48, cy + 30 + (target - wanted_scroll) * 16)
    wait_for(lambda: u16(machine.read(symbol("fdlg_sel"), 2))
             == target,
             "file dialog selection for %s" % name, 10.0)
    machine.key("Enter")


def open_source(machine, mouse, symbol, base, source_name, enter_demos=False):
    menu_pick(machine, mouse, symbol, "File", "Open...")
    wait_window(machine, symbol, "Open")
    if enter_demos:
        if "SPEEDY" in [row[0] for row in dispcp.snapshot(machine, symbol)]:
            dialog_select(machine, mouse, symbol, "SPEEDY")
            wait_for(lambda: "DEMOS" in
                     [row[0] for row in dispcp.snapshot(machine, symbol)],
                     "SPEEDY listing", 30.0)
        dialog_select(machine, mouse, symbol, "DEMOS")
        wait_for(lambda: source_name in
                 [row[0] for row in dispcp.snapshot(machine, symbol)],
                 "DEMOS listing", 30.0)
    dialog_select(machine, mouse, symbol, source_name)
    file_address = base + SPEEDY.sym("_sb_file_name")
    wait_for(lambda: cstring(machine.read(file_address, 13)) == source_name,
             "%s to load" % source_name, 45.0)
    wait_for(lambda: not any(win.title == "Open"
                             for win in windows(machine, symbol)),
             "Open dialog to close", 15.0)


def screenshot(socket_path, directory, name):
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, name)
    subprocess.run([sys.executable, "tools/shot.py", socket_path, path],
                   cwd=ROOT, check=True, capture_output=True)
    print("  screenshot -> %s" % path, flush=True)


def build_source(machine, mouse, symbol, base, source_name,
                 socket_path=None, capture_dir=None, capture=False):
    output = source_name.rsplit(".", 1)[0] + ".O88"
    state_address = base + SPEEDY.sym("_sbu_build")
    name_address = base + SPEEDY.sym("_sbu_package_name")
    error_address = base + SPEEDY.sym("_sbu_build_error")
    old_state = machine.read(state_address, 1)[0]
    menu_pick(machine, mouse, symbol, "Run", "Build Package...")
    wait_window(machine, symbol, "Save As")
    default = cstring(machine.read(symbol("fdlg_name"), 16))
    if default != output:
        raise RuntimeError("%s defaulted to %r, wanted %s" %
                           (source_name, default, output))
    machine.key("Enter")

    # On every build after the first, DONE is still visible from the previous
    # package. Require either a non-DONE transition or enough time for a whole
    # template read/write before accepting DONE for the new package.
    started = time.monotonic()

    if capture:
        try:
            wait_for(lambda: machine.read(state_address, 1)[0] in (2, 3, 4),
                     "%s compiler state" % source_name, 30.0, 0.03)
            screenshot(socket_path, capture_dir,
                       "speedybasic-qemu-compiler-running.png")
        except RuntimeError:
            # The completion check below carries the useful diagnostic. A
            # screenshot is supporting evidence and must not mask it.
            pass

    def finished():
        state = machine.read(state_address, 1)[0]
        name = cstring(machine.read(name_address, 13))
        if state == SB_BUILD_ERROR:
            error = cstring(machine.read(error_address, 48))
            raise RuntimeError("%s failed in os8088: %s" %
                               (source_name, error or "unknown error"))
        if state != SB_BUILD_DONE or name != output:
            return False
        return old_state != SB_BUILD_DONE or time.monotonic() - started >= 1.0

    wait_for(finished, "%s to compile" % source_name, 120.0)
    print("  %-12s -> %s" % (source_name, output), flush=True)


def launch_vm_package(machine, mouse, symbol, disk_window, source_name,
                      socket_path, capture_dir):
    """Launch one generated fallback and prove its embedded source autoruns."""
    output = source_name.rsplit(".", 1)[0] + ".O88"
    compiler = wait_window(machine, symbol, "Speedy Basic")
    mouse.click(*os88geom.close_xy(compiler.x, compiler.y))
    wait_for(lambda: not any(win.title == "Speedy Basic"
                             for win in windows(machine, symbol)),
             "compiler window to close", 30.0)
    # File dialogs and Disk windows share the mounted directory snapshot. A
    # long batch can leave the visible DEMOS listing cached before its later
    # outputs were added, so leave and re-enter it when the target is absent.
    for attempt in range(4):
        rows = wait_for(
            lambda: ([row[0] for row in dispcp.snapshot(machine, symbol)]
                     or False),
            "Disk listing while refreshing DEMOS", 15.0)
        if output in rows:
            break
        step = "DEMOS" if "DEMOS" in rows else ".." if ".." in rows else None
        if step is None or attempt == 3:
            raise RuntimeError("cannot refresh DEMOS for %s; it lists %r" %
                               (output, rows))
        dispcp.open_named(machine, mouse, symbol,
                          lambda *_a, **_k: time.sleep(1),
                          disk_window.x, disk_window.y, step)
        time.sleep(1.0)
    disk_window = next(win for win in windows(machine, symbol)
                       if win.i == disk_window.i)
    dispcp.open_named(machine, mouse, symbol,
                      lambda *_a, **_k: time.sleep(2),
                      disk_window.x, disk_window.y, output)
    try:
        app = wait_window(machine, symbol, output.rsplit(".", 1)[0], 30.0)
    except RuntimeError as error:
        screenshot(socket_path, capture_dir,
                   "speedybasic-qemu-generated-launch-failure.png")
        toast = cstring(machine.read(symbol("toast_buf"), 81))
        raise RuntimeError("%s; toast=%r, windows=%r" %
                           (error, toast,
                            [win.title for win in windows(machine, symbol)]))
    segment = package_segment(machine, symbol, app)
    base = segment << 4
    wait_for(lambda: cstring(machine.read(
                 base + SPEEDY_VM.sym("_sb_file_name"), 13)) == "COMPILED.BAS",
             "%s embedded source to load" % output, 90.0)
    def executed():
        state = u16(machine.read(base + SPEEDY_VM.sym("_sbc_status"), 2))
        if state == 5:
            message = cstring(machine.read(
                base + SPEEDY_VM.sym("_sbc_err"), 64))
            raise RuntimeError("%s embedded program failed: %s" %
                               (output, message))
        return state in (2, 3, 4)

    wait_for(executed, "%s embedded program to enter the runner" % output,
             30.0)
    time.sleep(2.0)
    screenshot(socket_path, capture_dir,
               "speedybasic-qemu-generated-life-running.png")
    print("  %s launched and executed its embedded BASIC source" % output,
          flush=True)


class FatVolume:
    """Small read-only FAT12 view used after QEMU has released the image."""

    def __init__(self, path):
        self.data = pathlib.Path(path).read_bytes()
        boot = self.data[:512]
        self.spc = boot[13]
        self.reserved = u16(boot, 14)
        self.nfats = boot[16]
        self.root_entries = u16(boot, 17)
        self.fat_sectors = u16(boot, 22)
        self.root_lba = self.reserved + self.nfats * self.fat_sectors
        root_sectors = (self.root_entries * 32 + 511) // 512
        self.data_lba = self.root_lba + root_sectors
        start = self.reserved * 512
        self.fat = self.data[start:start + self.fat_sectors * 512]

    @staticmethod
    def short_name(entry):
        stem = entry[:8].decode("ascii").rstrip()
        ext = entry[8:11].decode("ascii").rstrip()
        return stem + (("." + ext) if ext else "")

    def entries(self, raw):
        answer = {}
        for offset in range(0, len(raw) - 31, 32):
            entry = raw[offset:offset + 32]
            if entry[0] == 0:
                break
            if entry[0] in (0xe5, ord(".")) or entry[11] & 0x08:
                continue
            answer[self.short_name(entry)] = entry
        return answer

    def chain(self, first):
        clusters = []
        cluster = first
        while 2 <= cluster < 0xff8:
            if cluster in clusters:
                raise RuntimeError("FAT loop at cluster %d" % cluster)
            clusters.append(cluster)
            cluster = os88disk.fat_get(self.fat, True, cluster)
        return clusters

    def bytes_for(self, entry):
        first = u16(entry, 26)
        size = struct.unpack_from("<I", entry, 28)[0]
        return self.cluster_bytes(first)[:size]

    def cluster_bytes(self, first):
        chunks = []
        for cluster in self.chain(first):
            lba = self.data_lba + (cluster - 2) * self.spc
            chunks.append(self.data[lba * 512:(lba + self.spc) * 512])
        return b"".join(chunks)

    def directory(self, entry=None):
        if entry is None:
            start = self.root_lba * 512
            raw = self.data[start:start + self.root_entries * 32]
        else:
            # FAT directory entries carry size zero. Their allocation chain,
            # rather than the size field used by regular files, bounds the
            # directory body.
            raw = self.cluster_bytes(u16(entry, 26))
        return self.entries(raw)


def validate_disk(path, demos, temporary):
    subprocess.run([sys.executable, "tools/os88disk.py", "--verify", path],
                   cwd=ROOT, check=True)
    volume = FatVolume(path)
    root = volume.directory()
    speedy = volume.directory(root["SPEEDY"])
    demo_entries = volume.directory(speedy["DEMOS"])
    expected = {name.rsplit(".", 1)[0] + ".O88" for name in demos}
    found = {name for name in demo_entries if name.endswith(".O88")}
    if found != expected:
        raise RuntimeError("generated directory entries differ: missing=%r, "
                           "extra=%r" %
                           (sorted(expected - found), sorted(found - expected)))

    template = (ROOT / "build/SPEEDYCC.RT").read_bytes()
    vm_overlay = (ROOT / "build/SPEEDYVM.OVL").read_bytes()
    abi = {"size": len(template), "entry": u16(template, 6),
           "bss": u16(template, 10), "code": COMPILED.sym("_sbaot_code")}
    for name in sorted(expected):
        package = volume.bytes_for(demo_entries[name])
        output = pathlib.Path(temporary) / name
        output.write_bytes(package)
        checked = os88disk.validate_o88(str(output))
        expected_title = name.rsplit(".", 1)[0].encode("ascii")
        if checked[16:32].split(b"\0", 1)[0] != expected_title:
            raise RuntimeError("%s has the wrong package title" % name)
        if checked[3] & 4:
            # Complete-language fallback: a normal runnable resident image,
            # followed by its VM overlay and the original BASIC source as
            # eager O88 parts. validate_o88 above checks the outer image; the
            # nonempty tail proves this is the bundled single-file form.
            if len(checked) <= u16(checked, 8) + 32:
                raise RuntimeError("%s declares parts but has no bundled VM/source"
                                   % name)
            table = checked.find(b"O88PARTS", 32, u16(checked, 8))
            if table < 0 or checked[table + 8] != 2:
                raise RuntimeError("%s has no two-row VM/source part table" % name)
            row0, row1 = table + 10, table + 18
            if checked[row0:row0 + 2] != b"\0\0":
                raise RuntimeError("%s part 0 is not an eager code segment" % name)
            if checked[row1:row1 + 2] != b"\1\0":
                raise RuntimeError("%s part 1 is not an eager source asset" % name)
            overlay_at = u16(checked, row0 + 2) << 9
            source_at = u16(checked, row1 + 2) << 9
            overlay_len = u16(checked, row0 + 4)
            source_len = u16(checked, row1 + 4)
            source_name = name.rsplit(".", 1)[0] + ".BAS"
            source = (ROOT / "apps/speedybasic/demos" / source_name).read_bytes()
            if (overlay_len != len(vm_overlay)
                    or checked[overlay_at:overlay_at + overlay_len] != vm_overlay):
                raise RuntimeError("%s does not embed the shipping VM overlay"
                                   % name)
            embedded = checked[source_at:source_at + source_len]
            if source_len != len(source) or embedded != source:
                first = next((i for i, pair in
                              enumerate(zip(embedded, source))
                              if pair[0] != pair[1]), None)
                raise RuntimeError(
                    "%s BASIC source differs (row len=%d, expected=%d, "
                    "offset=%d, file=%d, first difference=%r)" %
                    (name, source_len, len(source), source_at, len(checked),
                     first))
        else:
            if (len(checked) != abi["size"]
                    or u16(checked, 6) != abi["entry"]
                    or u16(checked, 10) != abi["bss"]):
                raise RuntimeError("%s does not match the native template ABI"
                                   % name)
            if checked[abi["code"]:abi["code"] + 3] != b"\x55\x89\xe5":
                raise RuntimeError("%s has no generated 8086 dispatcher" % name)
    print("speedybasic in-OS corpus: %d directory entries and packages PASS" %
          len(expected))


def compile_batch(args, temporary, batch, batch_number):
    disk = os.path.join(temporary, "speedybasic-%02d.img" % batch_number)
    socket_path = os.path.join(temporary, "qmp-%02d.sock" % batch_number)
    pidfile = os.path.join(temporary, "qemu-%02d.pid" % batch_number)
    shutil.copyfile(ROOT / APPS, disk)

    os88qemu.own(pidfile, socket_path)
    command = [
        "qemu-system-i386", "-machine", "pc,vmport=off",
        "-drive", "file=%s,format=raw,if=floppy,snapshot=on" %
                  (ROOT / SYSTEM),
        "-boot", "a", "-chardev", "msmouse,id=m0",
        "-serial", "chardev:m0",
        "-drive", "file=%s,format=raw,if=floppy,index=1" % disk,
        "-display", "none", "-qmp",
        "unix:%s,server,nowait" % socket_path,
        "-daemonize", "-pidfile", pidfile,
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True)
    machine = Qemu(socket_path)
    mouse = Mouse(socket_path)
    symbol = os88sym.linear

    wait_for(lambda: u16(machine.read(symbol("desk_rows"), 2))
             and u16(machine.read(symbol("menu_nbar"), 2)),
             "os8088 desktop for batch %d" % batch_number, 90.0)
    time.sleep(2.0)
    dispcp.open_drive(machine, mouse, symbol, lambda *_a, **_k: time.sleep(1),
                      "B")
    disk_window = windows(machine, symbol)[-1]
    dispcp.open_named(machine, mouse, symbol,
                      lambda *_a, **_k: time.sleep(1),
                      disk_window.x, disk_window.y, "SPEEDY")
    disk_window = windows(machine, symbol)[-1]
    dispcp.open_named(machine, mouse, symbol,
                      lambda *_a, **_k: time.sleep(2),
                      disk_window.x, disk_window.y, "SPEEDYBA.O88")
    app = wait_window(machine, symbol, "Speedy Basic", 90.0)
    segment = package_segment(machine, symbol, app)
    base = segment << 4

    print("batch %d: %s" % (batch_number, ", ".join(batch)), flush=True)
    for index, source in enumerate(batch):
        open_source(machine, mouse, symbol, base, source,
                    enter_demos=(index == 0))
        build_source(machine, mouse, symbol, base, source,
                     socket_path, args.capture_dir,
                     batch_number == 1 and index == 0)

    if args.launch in batch:
        launch_vm_package(machine, mouse, symbol, disk_window, args.launch,
                          socket_path, args.capture_dir)

    if batch_number == 1:
        screenshot(socket_path, args.capture_dir,
                   "speedybasic-qemu-corpus-complete.png")

    pid = int(pathlib.Path(pidfile).read_text().strip())
    machine.quit()

    def process_gone():
        try:
            os.kill(pid, 0)
            return False
        except OSError:
            return True

    wait_for(process_gone, "QEMU batch %d to shut down cleanly" % batch_number,
             20.0)
    for stale in (socket_path, pidfile):
        try:
            os.unlink(stale)
        except FileNotFoundError:
            pass
    validate_disk(disk, batch, temporary)
    if args.keep:
        destination = "%s-%02d.img" % (args.keep, batch_number)
        shutil.copyfile(disk, destination)
        print("writable batch copied to %s" % destination)


def run(args):
    demos = [line.strip() for line in pathlib.Path(MANIFEST).read_text().splitlines()
             if line.strip() and not line.startswith("#")]
    if len(demos) != 29 or len(set(demos)) != len(demos):
        raise RuntimeError("manifest must contain 29 unique demos")
    if args.demo:
        unknown = sorted(set(args.demo) - set(demos))
        if unknown:
            raise RuntimeError("unknown demo(s): %s" % ", ".join(unknown))
        demos = args.demo

    subprocess.run(["make", SYSTEM, APPS], cwd=ROOT, check=True)
    # A VM fallback package carries about 50KB resident code, a 16.5KB
    # overlay, and its source. All 29 exceed a floppy even though each output
    # is independently valid, so use fresh media in bounded batches.
    ordered = sorted(demos, reverse=True)
    batches = [ordered[i:i + args.batch_size]
               for i in range(0, len(ordered), args.batch_size)]
    with tempfile.TemporaryDirectory(prefix="os88-speedybasic-corpus-") as td:
        try:
            for number, batch in enumerate(batches, 1):
                compile_batch(args, td, batch, number)
        finally:
            # The temporary directory is removed before process-level atexit
            # handlers run. Stop any failed batch while its exact pidfile and
            # QMP socket still exist, or an exception would orphan QEMU with
            # an unlinked writable floppy open.
            for pidfile in pathlib.Path(td).glob("qemu-*.pid"):
                number = pidfile.stem.split("-")[-1]
                socket_path = os.path.join(td, "qmp-%s.sock" % number)
                os88qemu.kill(str(pidfile), socket_path)
    print("speedybasic QEMU corpus: all %d demos compiled in %d clean boots PASS"
          % (len(demos), len(batches)))


SPEEDY = Syms("apps/speedybasic/speedybasic.asm",
              "build/speedybasic.bin", ["apps", "build"])
COMPILED = Syms("apps/speedybasic/compiler/template.asm",
                "build/speedycc.bin", ["apps", "build"])
SPEEDY_VM = Syms("apps/speedybasic/speedybasic.asm",
                 "build/speedybasicvm.bin", ["apps", "build"],
                 defines=("SB_VM_ASM",))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--keep", metavar="IMAGE",
                        help="copy verified batches to IMAGE-NN.img")
    parser.add_argument("--batch-size", type=int, default=8,
                        help="outputs per writable floppy (default: 8)")
    parser.add_argument("--capture-dir", default="/tmp/speedybasic-qemu-corpus")
    parser.add_argument("--demo", action="append",
                        help="compile only this manifest name (repeatable)")
    parser.add_argument("--launch",
                        help="also launch this generated demo and verify autorun")
    args = parser.parse_args()
    if not 1 <= args.batch_size <= 8:
        parser.error("--batch-size must be in 1..8")
    os.chdir(ROOT)
    run(args)


if __name__ == "__main__":
    main()
