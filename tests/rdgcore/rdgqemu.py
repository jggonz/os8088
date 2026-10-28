#!/usr/bin/env python3
"""Run a tests/rdgcore image under QEMU and print what it sent on COM1.

    python3 tests/rdgcore/rdgqemu.py IMG [--until TEXT ...] [--timeout S]

Polls the serial log for any of the --until strings (default: "Passed",
"Failed", "RDGHOST DONE", "RDGHOST BADOP") rather than sleeping a fixed time,
then kills QEMU. Exit 0 if a "Passed"-like line arrived, 1 otherwise. QEMU
only counts work here (docs/TESTING.md): correctness, never speed.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# THE SAHF PROBE. QEMU 11.0.2's TCG (Homebrew, an M1 host) loses SAHF's flags
# when the instruction before it in the same translation block was a logic op:
# `or ah,20h / sahf / jz` falls through with AH's ZF set. That is the
# recompiler's CP template followed by the JR it is fused with (rdggen.py: CP
# keeps N in AH's bit 5, then SAHF restores the flags the OR clobbered), so
# dmg-acid2's first `LDH A,(44h) / CP 90h / JR NZ` read line 144 and looped
# for ever, and the frame came back blank - 10,191 pixels "wrong" on a
# renderer that draws every one right. The code is correct 8086 code (a 286
# runs it); the emulator is not. So a QEMU that fails this is run one guest
# instruction a block, where SAHF's flags are written back before the jump
# reads them: the same guest, executed as the hardware executes it.
SAHF_PROBE = """
cpu 8086
bits 16
org 0x7C00
    xor ax,ax
    mov ds,ax
    mov ss,ax
    mov sp,0x7C00
    mov dx,0x3FB
    mov al,0x80
    out dx,al
    mov dx,0x3F8
    mov al,1
    out dx,al
    inc dx
    xor al,al
    out dx,al
    mov dx,0x3FB
    mov al,3
    out dx,al
    mov bl,'N'
    mov al,90h
    cmp al,90h
    lahf
    or ah,20h
    sahf
    jnz .say
    mov bl,'Y'
.say:
    mov dx,0x3FD
.w: in al,dx
    test al,20h
    jz .w
    mov dx,0x3F8
    mov al,bl
    out dx,al
.h: hlt
    jmp .h
    times 510-($-$$) db 0
    dw 0xAA55
"""
_tcg = None


def tcg_args():
    """The QEMU arguments that make it execute SAHF right: none when the
    probe's SAHF works, else one instruction a translation block. Asked once
    a process, and remembered in build/realdmg/ against QEMU's version."""
    global _tcg
    if _tcg is not None:
        return _tcg
    ver = subprocess.run(["qemu-system-i386", "--version"], capture_output=True,
                         text=True).stdout.split("\n")[0]
    key = hashlib.sha256((ver + SAHF_PROBE).encode()).hexdigest()[:16]
    memo = os.path.join(ROOT, "build", "realdmg", "qemu-sahf-%s" % key)
    if os.path.exists(memo):
        ok = open(memo).read().strip() == "Y"
    else:
        with tempfile.TemporaryDirectory(prefix="rdgsahf") as tmp:
            src, binf, img = (os.path.join(tmp, n) for n in ("p.asm", "p.bin", "p.img"))
            open(src, "w").write(SAHF_PROBE)
            subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-o", binf, src])
            open(img, "wb").write(open(binf, "rb").read() + bytes(1474560 - 512))
            text = _qemu(img, ["Y", "N"], 20, [])
        if text not in ("Y", "N"):
            sys.exit("rdgqemu: the SAHF probe said %r (%s)" % (text, ver))
        ok = text == "Y"
        os.makedirs(os.path.dirname(memo), exist_ok=True)
        tmpf = memo + ".%d" % os.getpid()
        open(tmpf, "w").write(text)
        os.replace(tmpf, memo)
    if not ok:
        sys.stderr.write("rdgqemu: %s loses SAHF's flags after an OR; running it one "
                         "instruction a block\n" % ver)
    _tcg = [] if ok else ["-accel", "tcg,one-insn-per-tb=on"]
    return _tcg


def _qemu(img, until, timeout, extra):
    fd, log = tempfile.mkstemp(prefix="rdgq-", suffix=".log")
    os.close(fd)
    p = subprocess.Popen(["qemu-system-i386", "-drive", "file=%s,format=raw,if=floppy" % img,
                          "-boot", "a", "-display", "none", "-serial", "file:%s" % log,
                          "-no-reboot"] + extra, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    t0, text = time.time(), ""
    try:
        while time.time() - t0 < timeout:
            text = open(log, "rb").read().decode("latin-1")
            if any(u in text for u in until):
                break
            time.sleep(0.1)
    finally:
        p.kill()
        p.wait()
        os.unlink(log)
    return text


def run(img, until, timeout, quiet=False, final=(), poll=0.05):
    """`final`: markers that END the output (e.g. " END\n", the DONE line's
    last word), so a match on one needs no 0.3 s trailing read."""
    fd, log = tempfile.mkstemp(prefix="rdgq-", suffix=".log")
    os.close(fd)
    p = subprocess.Popen(["qemu-system-i386", "-drive", "file=%s,format=raw,if=floppy" % img,
                          "-boot", "a", "-display", "none", "-serial", "file:%s" % log,
                          "-no-reboot"] + tcg_args(), stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    t0, text = time.time(), ""
    try:
        while time.time() - t0 < timeout:
            text = open(log, "rb").read().decode("latin-1")
            if any(u in text for u in final):
                break
            if any(u in text for u in until):
                time.sleep(0.3)
                text = open(log, "rb").read().decode("latin-1")
                break
            time.sleep(poll)
    finally:
        p.kill()
        p.wait()
        os.unlink(log)
    if not quiet:
        sys.stdout.write(text)
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("img")
    ap.add_argument("--until", action="append")
    ap.add_argument("--timeout", type=float, default=60)
    a = ap.parse_args()
    until = a.until or ["Passed", "Failed", "RDGHOST DONE", "RDGHOST BADOP"]
    text = run(a.img, until, a.timeout)
    sys.exit(0 if "Passed" in text else 1)


if __name__ == "__main__":
    main()
