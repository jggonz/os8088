#!/usr/bin/env python3
"""Installed Boratype against missing, corrupt, protected and full storage.

Fixtures are private software disks; no guest state or production disk is
patched. This is actual filesystem acceptance, alongside host fault injection.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
import boratype_target as T
import os88ui
import os88sym
from os88fat import Fat12
from os88flush import Flush, Volume


def checksum(data):
    data = bytearray(data)
    data[18:20] = bytes(2)
    a = b = 0
    for value in data:
        a = (a + value) % 255
        b = (b + a) % 255
    struct.pack_into("<H", data, 18, a | b << 8)
    return bytes(data)


def canonical(work):
    source, executable = work / "fixture.c", work / "fixture"
    source.write_text('''#define BT_HOST
#include <stdio.h>
#include "bt.h"
#include "bt_lessons.c"
#include "bt_core.c"
#include "bt_codec.c"
static BTByte bytes[BT_FILE_SIZE];
int main(void) {
    bt_init(); bt_store.generation=42;
    bt_store.profile[0].name[0]='D'; bt_store.profile[0].name[1]='i';
    bt_store.profile[0].name[2]='s'; bt_store.profile[0].name[3]='k';
    bt_store.profile[0].name[4]=0;
    if(bt_encode(bytes,sizeof(bytes))!=sizeof(bytes)) return 1;
    return fwrite(bytes,1,sizeof(bytes),stdout)!=sizeof(bytes);
}
''')
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O1", "-Wall", "-Werror",
                    "-I", str(T.SRC / "apps/boratype"), str(source), "-o", str(executable)], check=True)
    return subprocess.check_output([str(executable)])


def fixture(work, build, case, good):
    folder = work / case
    folder.mkdir()
    disk = folder / "apps.img"
    command = [sys.executable, str(ROOT / "tools/os88disk.py"), "-o", str(disk),
               "--size", "360", str(build / "boratype.o88")]
    if case != "missing-folder":
        command += ["--folder", "SYSTEM/APPDATA"]
    for slot, generation in (("BORATA.BTT", 42), ("BORATB.BTT", 41)):
        data = bytearray(good)
        struct.pack_into("<H", data, 8, generation)
        if case == "old-version":
            struct.pack_into("<H", data, 4, 2)
        elif case == "invalid-fields":
            data[33] = 101  # first explicit lesson-best byte, outside0..100
        data = checksum(data)
        if case == "truncated":
            data = data[:100]
        if case not in ("missing-folder", "missing-files"):
            path = folder / slot
            path.write_bytes(data)
            command.append("SYSTEM/APPDATA:" + str(path))
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    if case == "read-only-files":
        raw = bytearray(disk.read_bytes())
        for slot in ("BORATA.BTT", "BORATB.BTT"):
            name = Fat12.raw11(slot)
            assert raw.count(name) == 1, "fixture directory entry not unique"
            raw[raw.index(name) + 11] |= 1
        disk.write_bytes(raw)
        assert all(e.attr & 1 for e in Volume(raw).walk() if e.name.endswith(".BTT"))
    elif case == "full-disk":
        fat = Fat12(disk)
        filler = folder / "FILLER.BIN"
        filler.write_bytes(bytes(len(fat.free_clusters()) * fat.spc * fat.bps))
        fat.add(filler, "FILLER.BIN")
        assert not fat.free_clusters()
        fat.save()
    return disk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, default=ROOT / "build/boratype-storage")
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    build = Path(os88sym.kernel_image()).parent
    assert Fat12(build / "os8088-360.img").read("KERNEL.SYS") == (build / "kernel.sys").read_bytes(), \
        "storage boot disk carries a stale kernel; rebuild shipped images"
    os88sym.syms()
    symbols, layout = T.app_symbols(build), T.target_layout()
    started = time.monotonic()
    report = {"status": "RUNNING", "rows": [], "source": str(T.SRC), "build": str(build)}
    try:
        with tempfile.TemporaryDirectory(prefix="boratype-storage-") as temporary:
            work = Path(temporary)
            good = canonical(work)
            for case in ("missing-folder", "missing-files", "truncated", "old-version",
                         "invalid-fields", "read-only-files", "full-disk"):
                disk = fixture(work, build, case, good)
                row = {"case": case, "status": "RUNNING"}
                report["rows"].append(row)
                with os88ui.boot(str(build / "os8088-360.img"), apps=str(disk),
                                 machine="os8088_xt_vga", quick=True) as ui:
                    app = T.App(ui, T.open_package(ui, "B:/BORATYPE.O88"), symbols, layout, row)
                    retained = case in ("read-only-files", "full-disk")
                    assert app.generation() == (42 if retained else 0)
                    profile = app.profile()
                    if retained:
                        assert profile.split(b"\0", 1)[0] == b"Disk"
                    else:
                        assert profile.split(b"\0", 1)[0] == b"Player 1"
                    generation = app.generation()
                    app.menu("File", "Save progress")
                    refused = case in ("missing-folder", "read-only-files", "full-disk")
                    ui._wait(lambda: not app.global_word("bt_save_pending") and
                             app.text("bt_notice").startswith("Save failed" if refused else "Progress saved"),
                             "completed save result", 90, snapshot=lambda: app.text("bt_notice"))
                    notice = app.text("bt_notice")
                    assert notice.startswith("Save failed" if refused else "Progress saved"), notice
                    assert app.generation() == (generation if refused else generation + 1)
                    assert app.profile() == profile, "failed/load/save changed live profile"
                    app.shot(args.evidence, case)
                    row.update(status="PASS", notice=notice, generation=app.generation())
                    ui.close(app.win, limit=90)
                    exported = args.evidence / (case + ".img")
                    Flush(marty=ui.m).save(1, str(exported))
                    saved = Volume(exported.read_bytes(), case)
                    if retained:
                        assert saved.read("SYSTEM/APPDATA/BORATA.BTT") == good, "newest valid save was damaged"
                    row["exported_sha256"] = hashlib.sha256(exported.read_bytes()).hexdigest()
                print("boratype storage:", case, "PASS", notice, flush=True)
        report["status"] = "PASS"
    except BaseException as error:
        report["status"] = "FAIL"
        report["error"] = repr(error)
        raise
    finally:
        report["host_seconds"] = round(time.monotonic() - started, 3)
        (args.evidence / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
