#!/usr/bin/env python3
"""What `tools/stkbalance.py` must catch, and what it must stay quiet about.

    python3 tests/unit/t_stkbalance.py

THE GATE IS ONLY WORTH ITS RUNTIME IF BOTH HALVES HOLD. A stack walker that
reports nothing passes every build and defends nothing; one that reports a
routine in ten gets ignored and then defends nothing either. The kernel spent
this whole tree's life ungated for the second reason - `stkbalance` was scoped
to SHEET and CHART because pointing it at `kernel/` produced 24 findings, and
the suite row said so in as many words. Twenty-three of those were the walker's
own model being wrong, and the twenty-fourth was real (see `op_size` below).

So this file fixes the model in both directions. Every QUIET case is an idiom
that was once a finding, and every LOUD case is a defect shape that a size pass
actually produces - deleting a `push` a callee already saves, cross-jumping two
epilogues that are not really twins, routing several call sites through one
shared door. If somebody makes this walker cleverer and it stops catching the
LOUD half, this file fails; if they make it stricter and it starts reporting
the QUIET half, this file fails too.

The fixtures are written here rather than committed as `.asm` because they are
statements about the WALKER, not about this tree's assembly - they never
assemble, they never ship, and they must not be swept up by the gates that
walk real sources.

A bare `N.N` below is a SPEC.md section.
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness as h

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "stkbalance.py")


def verdict(files):
    """(findings, stdout) for a dict of {name: source}."""
    with tempfile.TemporaryDirectory() as d:
        paths = []
        for name, src in files.items():
            p = os.path.join(d, name)
            with open(p, "w") as f:
                f.write(src)
            paths.append(p)
        r = subprocess.run([sys.executable, TOOL] + paths,
                           capture_output=True, text=True)
        last = r.stdout.strip().split("\n")[-1]
        n = int(last.split()[1]) if last.startswith("stkbalance:") else -1
        return n, r.stdout


# ---------------------------------------------------------------------------
# QUIET: idioms that are correct code.  Each was a finding before the walker
# understood it, and the file/routine named is where it actually lives.
# ---------------------------------------------------------------------------
QUIET = {
    "a continuation is not a routine (kernel/ui.inc's ui_lit_go)": {
        "a.inc": """
ui_lit_on:
    push ax
    mov al, 1
    jmp short ui_lit_go
ui_lit_off:
    push ax
    xor al, al
ui_lit_go:
    mov [thing], al
    pop ax
    ret
"""},

    "a shared tail reached across FILES (fdlg.inc -> files.inc's fm_dotin)": {
        "a.inc": """
fdlg_hasdot:
    push si
    jmp fm_dotin
""",
        "b.inc": """
fm_dotin:
    mov al, [si]
    pop si
    ret
"""},

    "`jmp short $+2` is an I/O settle, not a tail call (clockw.inc)": {
        "a.inc": """
clk_at_put:
    push ax
    push dx
    out dx, al
    jmp short $+2
    pop dx
    pop ax
    ret
"""},

    "`pushf` + `call far` chains an interrupt (splash.inc's spl_isr)": {
        "a.inc": """
spl_isr:
    pushf
    call far [cs:spl_old08]
    push ax
    mov al, 1
    pop ax
    iret
"""},

    "`push`/`push`/`retf` is a constructed FAR JUMP (driver.inc)": {
        "a.inc": """
drv_pkg_disp:
    push ds
    push word [cs:drv_fptr]
    retf
"""},

    "a jump TABLE's arms run at the dispatcher's depth (weave/wcanvas.asm)": {
        "a.inc": """
wsm_entry:
    push bp
    push si
    push di
    push es
    push ds
    mov bx, ax
    shl bx, 1
    jmp [wsm_tab + bx]
wsm_out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf
wsm_tab:
    dw wsm_v_bind
    dw wsm_v_sprite
wsm_v_bind:
    mov ax, 1
    jmp wsm_out
wsm_v_sprite:
    mov ax, 2
    jmp wsm_out
"""},

    "a local named through its owner - `font_char.chok` (font.inc)": {
        "a.inc": """
font_char:
    push ax
    cmp cx, dx
    jbe .chok
    jmp font_char.chok
.chok:
    pop ax
    ret
"""},

    "a data table is not code and is not walked through (kernel.asm's dbg_reg)": {
        # The table is an entry (its address is taken and nothing calls it), so
        # a walker that treats `dw` as code walks off the bottom of it into the
        # shared tail below and reports that tail's `ret` at -1.  Control flow
        # never runs THROUGH a table, and the tail is reached only from the
        # pusher, which balances it.
        "a.inc": """
dbg_reg:
    dw DBG_TAG_MOUSE, mou_dbg_blk
    dw DBG_TAG_CLOCK, clk_dbg_blk
shared_tail:
    pop ax
    ret
pusher:
    push ax
    mov al, 1
    jmp shared_tail
"""},

    "a declared banking pair (`; STKBALANCE-NET:`)": {
        "a.inc": """
sh_vpush:
    ; STKBALANCE-NET: +2
    pop ax
    push bx
    push cx
    push ax
    ret
user:
    call sh_vpush
    pop bx
    pop cx
    ret
"""},

    "`; STKBALANCE-OK:` exempts however the routine is REACHED (task_exit)": {
        "a.inc": """
inst_pkg_alive:
    pushf
    push di
    jmp task_exit
task_exit:
    ; STKBALANCE-OK: the task is gone and its stack with it
    pop ax
    pop ax
    pop ax
    iret
"""},

    "a loop that pushes N and pops N, MARKED `; STKBALANCE-LOOP:`": {
        # The count is in CX and no static walk can pair the two loops, so
        # the routine says so.  Without the marker this is the LOUD row
        # `a pop inside a loop` below - the two are the same shape.
        "a.inc": """
itoa:
    ; STKBALANCE-LOOP: one digit pushed a turn, popped by the second loop
    xor cx, cx
.div:
    push dx
    inc cx
    dec bx
    jnz .div
.emit:
    pop dx
    loop .emit
    ret
"""},

    "a `section` is a HARD BARRIER - nothing falls through it": {
        # A bare label at the END of a section - boot2_end, modmap_end, and
        # every *_end the size ladder declares - has no instruction of its
        # own, so the walk used to run off it into whatever the NEXT section
        # opens with.  That is a different address space and no execution
        # reaches it from there.  Here `sb_map_end` walked out of .modmap into
        # .text and reported sb_tail's continuation prologue as `ret at
        # depth -1`: a finding against a label that emits no bytes at all.
        # The three lines are worth it because the alternative is output
        # nobody can act on, which is how a gate stops being read.
        #
        # sb_tail is only ever JUMPED to, so it is a continuation and not an
        # entry.  Without that the same `ret` is also reported under sb_tail's
        # own name and main()'s de-duplication - keyed on (file, line, why) -
        # hides the phantom behind the real one.
        "a.inc": """
section .text
sb_caller:
    push ax
    jmp sb_tail

section .modmap
sb_map:
    dw 0x384F
sb_map_end:

section .text
sb_tail:
    pop ax
    ret
"""},
}


# ---------------------------------------------------------------------------
# LOUD: defect shapes a size pass produces.  Every one of these must be caught.
# ---------------------------------------------------------------------------
LOUD = {
    "a `push` whose `pop` was deleted - the classic slip": {
        "a.inc": """
leaky:
    push si
    mov ax, 1
    ret
"""},

    "a `pop` with no `push` - one register too many given back": {
        "a.inc": """
overpop:
    push si
    pop si
    pop di
    ret
"""},

    "cross-jumping two epilogues that are not really twins": {
        "a.inc": """
routine_a:
    push ax
    push bx
    push cx
    mov dx, 1
    jmp shared_tail
routine_b:
    push ax
    mov dx, 2
shared_tail:
    pop ax
    ret
"""},

    "ONE handler for two depths - apps/os88parts.inc's op_size (REAL BUG)": {
        "a.inc": """
op_size:
    push ax
    push cx
    add cx, 511
    jc .ovfc
    pop cx
    pop ax
    clc
    ret
.have:
    push cx
    add cx, 511
    jc .ovfc
    pop cx
    clc
    ret
.ovfc:
    pop cx
    stc
    ret
"""},

    "a label reached at two different depths": {
        "a.inc": """
forky:
    cmp ax, 1
    je .skip
    push bx
.skip:
    pop bx
    ret
"""},

    "a BACKWARD tail-merge, which is the shape that went silent": {
        # Size pass 2's adverse review found that the suppression rule tested
        # only "backward, same file" - and a tail merge IS a backward
        # cross-jump, so the gate went blind to the commonest shape a size pass
        # creates.  It could not build a case that stayed silent (its synthetics
        # tripped the ret-at-depth check first), so the fix rested on the
        # reasoning alone.  This is that case, and it needs three things at
        # once: the two depths must meet at a label rather than at a `ret`, the
        # target must be EARLIER in the file and owned by another routine, and
        # the BALANCED path must be walked first so the deeper one is the one
        # suppressed.  Without the owner test this file reports 0 findings and
        # the only trace is the back-edge counter moving to 1.
        "a.inc": """
victim:
    push cx
tail:
    pop cx
    ret

entry:
    push ax
    cmp ax, 1
    je .one
    push bx
    jmp tail
.one:
    jmp tail
"""},

    "a tail `jmp` into another routine carrying depth": {
        "a.inc": """
caller:
    push si
    jmp callee
callee:
    mov ax, 1
    ret
"""},

    "a pop INSIDE a loop - the second turn pops the return address": {
        # Statically this is an itoa: the loop target dominates the source,
        # the edge is backward and inside one routine, and every test that
        # suppressed the itoa's conflict suppressed this one with it.  So
        # nothing is suppressed without a marker now, and a routine that is
        # not marked reports here.
        "a.inc": """
looppop:
    push cx
.l:
    pop cx
    loop .l
    ret
"""},

    "a tail-merged epilogue one register deep, reached by a BACKWARD jmp": {
        # `.done` pops AX for the straight path, and `.alt` has pushed BX
        # before jumping back into it.  The old rule read the backward
        # same-routine edge as a loop and the walk went quiet at exit 0.
        "a.inc": """
merged:
    push ax
    cmp bx, 1
    je .alt
.done:
    pop ax
    ret
.alt:
    push bx
    jmp .done
"""},

    "a jump table whose arms are `owner.local` (mppui.inc, wfx.inc)": {
        # `dw disp.arm0, disp.arm1` used to read as four words - disp, arm0,
        # disp, arm1 - so the dispatch pushed disp's own global at depth +1
        # (backward, same owner: suppressed) and .arm1 was never walked. It
        # pushes BX and never pops it, and passed with exit 0.
        "a.inc": """
disp:
    push ax
    mov bx, [sel]
    jmp [disp_tab + bx]
.arm0:
    pop ax
    ret
.arm1:
    push bx
    pop ax
    ret
disp_tab:
    dw disp.arm0, disp.arm1
"""},
}


def main():
    for why, files in sorted(QUIET.items()):
        n, out = verdict(files)
        h.eq(n, 0, "QUIET: %s" % why,
             why="this is correct code and the walker must not report it; a "
                 "gate that flags an idiom gets ignored, and an ignored gate "
                 "is worse than none.\n" + out)
    for why, files in sorted(LOUD.items()):
        n, out = verdict(files)
        h.check(n > 0, "LOUD: %s" % why,
                why="this is a real imbalance of exactly the shape a size "
                    "pass produces; if the walker goes quiet here the gate "
                    "is decorative.\n" + out)
    return h.done("stkbalance walker")


if __name__ == "__main__":
    sys.exit(main())
