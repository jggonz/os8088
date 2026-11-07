#!/usr/bin/env python3
"""Equivalence of sch_switch's pick, base kernel bytes vs tip kernel bytes, under
unicorn. For random task tables (and every edge we care about) both kernels are
run from sch_switch to the iret, and the task they resume (the IP of the frame
they popped) and the [sch_cur] they leave must agree.

usage: uc_switch.py BASE_ROOT TIP_ROOT [N] [defines...]"""
import sys, os, random, struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_CODE
from unicorn.x86_const import *

def load(root, defines):
    import importlib.util, subprocess, json
    # a subprocess per tree: os88sym caches by module, and both trees' copies
    # are named the same
    code = ("import sys,json,os; sys.path.insert(0,'tools'); import os88sym;"
            "d=tuple(sys.argv[1:]);"
            "print(json.dumps([os88sym.syms(d), os88sym.equates(d), os88sym.sections(d)]))")
    out = subprocess.run([sys.executable, "-c", code] + list(defines), cwd=root,
                         capture_output=True, text=True, check=True).stdout
    off, equ, sec = json.loads(out.strip().splitlines()[-1])
    bdir = os.path.join(root, os.environ.get("UC_BUILD", "build"))
    full = open(os.path.join(bdir, "kernel-full.bin"), "rb").read()
    for line in open(os.path.join(bdir, "kernel.map")):
        f = line.split()
        if len(f) == 6 and f[5] == ".text" and f[4] == "progbits":
            a, b = int(f[1], 16), int(f[2], 16)
    img = full[a:b]
    return off, equ, sec, img

def run(k, st, defines):
    off, equ, sec, img = k
    MAXT = equ["MAX_TASKS"]; LOW = equ["LOW_SEG"]; KS = 0x60; TS = 8
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(KS * 16, img[:0x10000 - 0x0])
    w8 = lambda a, v: mu.mem_write(a, bytes([v & 0xFF]))
    w16 = lambda a, v: mu.mem_write(a, struct.pack("<H", v & 0xFFFF))
    r8 = lambda a: mu.mem_read(a, 1)[0]
    r16 = lambda a: struct.unpack("<H", mu.mem_read(a, 2))[0]
    K = lambda n: KS * 16 + off[n]
    tasks = K("sch_tasks")
    for i in range(MAXT):
        rec = tasks + i * TS
        w8(rec + equ["T_STATE"], st["state"][i])
        w8(rec + equ["T_FLAGS"], st["flags"][i])
        w8(rec + equ["T_INST"], 0xFF)
        # a fake frame for every task, in LOW_SEG, tagged by its IP
        fsp = 0x4000 + i * 0x40
        w16(rec + equ["T_SP"], fsp)
        frame = [0x60, 0x60, 0, 0, 0, 0, 0, 0, 0, 0x1000 + i, 0x2000, 0x0002]
        for j, v in enumerate(frame):
            w16(LOW * 16 + fsp + 2 * j, v)
        base = r16(K("sch_stkbase") + 2 * i)
        w16(LOW * 16 + base, equ["SCH_MAGIC"])
    w8(K("sch_cur"), st["cur"]); w8(K("sch_idleslot"), st["idle"])
    w8(K("fsx_task"), st["fsx"]); w8(K("fsx_worker"), st["fsxw"])
    if "sch_top" in off:
        w8(K("sch_top"), max(1, st["top"] - int(os.environ.get("UC_BREAK", "0"))))
    # the outgoing task's own frame on the live stack
    sp = 0x7000
    frame = [0x60, 0x60, 0, 0, 0, 0, 0, 0, 0, 0x1000 + st["cur"], 0x2000, 0x0002]
    for j, v in enumerate(frame):
        w16(LOW * 16 + sp + 2 * j, v)
    mu.reg_write(UC_X86_REG_SS, LOW); mu.reg_write(UC_X86_REG_SP, sp)
    mu.reg_write(UC_X86_REG_DS, KS); mu.reg_write(UC_X86_REG_ES, KS)
    mu.reg_write(UC_X86_REG_CS, KS)
    def hin(uc, port, size, ud):
        return 0
    def hout(uc, port, size, val, ud):
        pass
    mu.hook_add(UC_HOOK_INSN, hin, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_INSN, hout, None, 1, 0, UC_X86_INS_OUT)
    start = KS * 16 + off["sch_switch"]
    stkdie = KS * 16 + off["sch_stkdie"]
    mu.emu_start(start, 0x20000 + 0x1000 + 0x10, timeout=0, count=20000) if False else None
    # run until CS:IP lands in 2000:1000+n
    res = {}
    def hcode(uc, addr, size, ud):
        if 0x21000 <= addr < 0x21000 + 0x20:
            res["ip"] = addr - 0x21000; uc.emu_stop()
        if addr == stkdie:
            res["ip"] = "stkdie"; uc.emu_stop()
    mu.hook_add(UC_HOOK_CODE, hcode)
    try:
        mu.emu_start(start, 0xFFFFF, count=20000)
    except Exception as e:
        cs = mu.reg_read(UC_X86_REG_CS); ip = mu.reg_read(UC_X86_REG_IP)
        raise RuntimeError("%s at %04X:%04X" % (e, cs, ip))
    res["cur"] = r8(K("sch_cur"))
    res["sp_parked"] = r16(tasks + st["cur"] * TS + equ["T_SP"])
    if "sch_top" in off:
        res["top"] = r8(K("sch_top"))
    return res

def main():
    base_root, tip_root = sys.argv[1], sys.argv[2]
    N = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    defines = tuple(sys.argv[4:])
    B = load(base_root, defines); T = load(tip_root, defines)
    MAXT = B[1]["MAX_TASKS"]
    TFS = B[1]["TF_SERVICE"]
    rnd = random.Random(1)
    bad = 0; n = 0; picks = {}
    for it in range(N):
        # slots 0 is never free; cluster occupancy low like the allocator does
        hi = rnd.randrange(1, MAXT + 1)
        state = [rnd.choice((0, 0, 1, 2)) if 0 < i < hi else 0 for i in range(MAXT)]
        state[0] = rnd.choice((1, 2))
        flags = [TFS if rnd.random() < 0.2 else 0 for i in range(MAXT)]
        live = [i for i in range(MAXT) if state[i]]
        top = max(live) + 1
        idle = rnd.choice([0xFF] + list(range(1, MAXT)))
        if idle != 0xFF:
            state[idle] = rnd.choice((1, 1, 2)); top = max(top, idle + 1)
        cur = rnd.choice(live + ([idle] if idle != 0xFF else []))
        exiting = rnd.random() < 0.15 and cur != 0
        if exiting:                 # task_exit: own slot freed, bound lowered
            state[cur] = 0
            live2 = [i for i in range(MAXT) if state[i]]
            top = max(live2) + 1
        fsx = 0xFF if rnd.random() < 0.6 else rnd.choice(range(MAXT))
        fsxw = 0xFF if rnd.random() < 0.6 else rnd.choice(range(MAXT))
        st = dict(state=state, flags=flags, cur=cur, idle=idle, fsx=fsx, fsxw=fsxw, top=top)
        rb = run(B, st, defines); rt = run(T, st, defines)
        n += 1
        picks[rb["ip"]] = picks.get(rb["ip"], 0) + 1
        if (rb["ip"], rb["cur"], rb["sp_parked"]) != (rt["ip"], rt["cur"], rt["sp_parked"]):
            bad += 1
            if bad < 10:
                print("MISMATCH", st, rb, rt)
    print("picks", sorted(picks.items(), key=str))
    print("%d cases, %d mismatches (MAX_TASKS=%d)" % (n, bad, MAXT))
    sys.exit(1 if bad else 0)

main()
