#!/usr/bin/env python3
"""Directory mutations load DISKOPS before changing the caller's B:/CASE.

The public API bench does Rename, New Folder, empty RMDIR and recursive
RMDIR. Host FAT reads verify their actual effects. Missing/mismatched
DISKOPS.DRV must return FERR_IO with BOTH floppy images byte-identical.
Every call records CF/AX, non-AX registers, instance HERE and stack balance;
global mount/CWD and the module's acquired-hold drop are checked too.

Runner prerequisites: build/diskmut.img and build/diskmut.bin. No build or
shared artifact mutation happens in this row. Pre-existing LIVE nested
module holds are covered separately by lzcomp/czjoin; this bench asserts
only its own acquired holds, including reactivating a disarmed cache.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                                # noqa: E402
import os88fat                                                  # noqa: E402
import os88flush                                                # noqa: E402
import os88marty                                                # noqa: E402
import os88lz                                                   # noqa: E402
import os88sym                                                  # noqa: E402
import os88ui                                                   # noqa: E402
from kbdsettings import require, set_file                        # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
COMMANDS = (("KeyR", b"r", b"OLD.TXT\0"),
            ("KeyM", b"m", b"ADDED\0"),
            ("KeyE", b"e", b"EMPTY\0"),
            ("KeyT", b"t", b"TREE\0"))


def u16(data, at=0):
    return int.from_bytes(data[at:at + 2], "little")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--fixture", default="build/diskmut.img")
    ap.add_argument("--binary", default="build/diskmut.bin")
    a = ap.parse_args(argv)
    image = Path(os88build.at(a.image)).resolve()
    fixture = Path(os88build.at(a.fixture)).resolve()
    binary = Path(os88build.at(a.binary)).resolve()
    require(fixture.is_file() and binary.is_file(),
            "build the declared diskmut fixture before running this row")
    rawbin = binary.read_bytes()
    eq, syms = os88sym.equates(), os88sym.syms()
    source_module = os88lz.cz_unwrap(os88fat.Fat12(str(image)).read("DISKOPS.DRV"))
    require(source_module[:3] == b"O8" + bytes([eq["MOD_VER"]]),
            "DISKOPS.DRV fixture is not a module header")
    corrupt = bytearray(source_module)
    corrupt[4] ^= 1                         # valid shape, wrong BUILD_NUM
    corrupt_file, _ = os88lz.cz_wrap(bytes(corrupt), os88lz.LZ4, filt=True)
    build = Path(os88build.at("build")).resolve()
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="diskmut-", dir=build) as tmp:
        scratch = Path(tmp)
        for case in ("success", "missing", "mismatched"):
            system = scratch / (case + ".img")
            shutil.copyfile(image, system)
            set_file(system, "SYSTEM.CFG", None, scratch)
            if case != "success":
                set_file(system, "DISKOPS.DRV", None if case == "missing" else corrupt_file, scratch)
            with os88ui.boot(str(system), apps=str(fixture), machine=a.machine,
                             quick=True) as ui:
                m, S = ui.m, ui.sym
                win = ui.path("B:/CASE/DISKMUT.O88")
                os88marty.ui_done(m, "directory-mutation fixture to finish opening")
                pkg = os88marty.Pkg(m, win, image=len(rawbin), sym=S)
                flush = os88flush.Flush(marty=m)
                volume = flush.volume(1)
                before_b = bytes(volume.img)
                before_a = bytes(flush.volume(0).img)
                folder = next(e for e in volume.listdir() if e.name == "CASE")
                cwd = folder.cluster
                require(cwd >= 2, "target CASE is not a real subdirectory")
                original = bytes(volume.read("CASE/OLD.TXT"))
                require(volume.read("CASE/TREE/CHILD/LEAF.TXT"),
                        "recursive-delete fixture has no nested leaf")

                def disarmed():
                    off, seg = (u16(m.read(S("CLFP") + i, 2)) for i in (0, 2))
                    cold = (S("mod_gone") - syms["mod_gone"]) // 16
                    return off == syms["mod_gone"] and seg == cold

                require(disarmed(), "DISKOPS is already live before the first mutation")
                for key, command, name in COMMANDS:
                    old = pkg.byte(0)
                    ui.press(key, need=True)
                    ui._wait(lambda: pkg.byte(0) == (old + 1) & 0xff,
                             "directory mutation %s to complete" % command,
                             os88ui.T_WINDOW)
                    record = bytes(pkg.read(0, 42))
                    require(record[1:2] == command, "wrong bench command ran")
                    regs = [u16(record, 18 + 2 * n) for n in range(12)]
                    cs, ss, ds, es, bp, di, si, dx, cx, bx, ax, flags = regs
                    expected_di = rawbin.index(b"NEW.TXT\0") if command == b"r" else 0xd567
                    require((cs, ss, ds, es, bp, di, si, dx, cx, bx) ==
                            (u16(record, 16), u16(record, 14), u16(record, 6),
                             0x1234, 0x5678, expected_di,
                             rawbin.index(name), 0xd345, 0xc234, 0xb123),
                            "%s %s clobbered a non-AX register: %r" % (case, command, regs))
                    require(u16(record, 2) == u16(record, 4), "public mutation unbalanced SP")
                    expected_error = 0 if case == "success" else eq["FERR_IO"]
                    require(ax == expected_error and flags & 1 == (case != "success"),
                            "%s %s returned AX=%04x CF=%d" % (case, command, ax, flags & 1))
                    require((u16(record, 8), u16(record, 10), record[12], record[13]) ==
                            (cwd, cwd, 1, 1), "instance CWD/volume changed during module loading")
                    require(m.read(S("dsk_unit"), 1) == b"\x01" and
                            u16(m.read(S("dsk_cwd"), 2)) == cwd and
                            m.read(S("dsk_mntok"), 1) == b"\x01",
                            "mounted target volume/CWD was not restored to B:/CASE")
                    require(disarmed(), "directory mutation kept a module hold it acquired")
                    volume = flush.volume(1)
                    require(bytes(flush.volume(0).img) == before_a,
                            "directory mutation changed boot volume A")
                    if case != "success":
                        require(bytes(volume.img) == before_b,
                                "%s refusal changed target-volume metadata/data" % case)
                    else:
                        names = set(volume.names("CASE"))
                        if command == b"r":
                            require("CASE/OLD.TXT" not in names and
                                    volume.read("CASE/NEW.TXT") == original,
                                    "Rename did not preserve the file bytes in CASE")
                        elif command == b"m":
                            entry = next(e for e in volume.listdir("CASE") if e.name == "ADDED")
                            require(entry.is_dir and not volume.listdir(entry.path),
                                    "New Folder did not create an empty directory")
                            dots = volume._dir_bytes(entry.cluster)
                            require(dots[:11] == b".          " and
                                    dots[32:43] == b"..         " and
                                    u16(dots, 26) == entry.cluster and u16(dots, 58) == cwd,
                                    "new directory's dot/parent links are wrong")
                        elif command == b"e":
                            require("CASE/EMPTY" not in names, "empty RMDIR left the directory")
                        else:
                            require(not any(n == "CASE/TREE" or n.startswith("CASE/TREE/")
                                            for n in names), "recursive RMDIR left its tree")
                    print(case, command.decode(), "CF/AX/registers/CWD/mount/drop/disk: ok")
                saved = scratch / (case + "-after.img")
                flush.save(1, str(saved))
                subprocess.run([sys.executable, str(ROOT / "tools/os88disk.py"),
                                "--verify", str(saved)], check=True)
                ui.close(win)
    print("PASS directory mutation success and missing/mismatched module refusal")


if __name__ == "__main__":
    main()
