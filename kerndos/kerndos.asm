; =============================================================================
; os8088 - kerndos/kerndos.asm
;
; **THE ROOT, AND IT IS NOT A KERNEL** (docs/plans/KERN-DOS-PLAN.md 4.2): no
; boot sequence, no task table, no scheduler, no API table, no window manager,
; no drawing layer, no menu, no event loop, no driver layer and no on-demand
; modules. It is a resident INT 21h with a FAT reader under it, so that a DOS
; program can have ~600KB of a 640KB machine.
;
; It %includes the KERNEL'S OWN disk layer rather than re-implementing it, and
; kdshim.inc is what those files name and this root has to answer. Wave 3's
; whole question is how big that shim is: a shim near the size of a
; purpose-written FAT reader means the reuse is not paying, and the plan is
; explicitly allowed to come back with "write a reader instead".
; =============================================================================
cpu 8086
bits 16

%include "kdlayout.inc"         ; the segment ladder and the sizes

; THE SECTIONS, DECLARED IN THE ORDER THEY LAND, which is the kernel's rule
; (SPEC.md 2.6) with the boot overlay left out. `.cold` is ordinary code here
; and not a second rung: nothing in kern_dos is boot-only, because kern_dos is
; not booted.
; **`vstart=0` ON `.lowbss`, AND IT IS NOT DECORATION** (kernel/kernel.asm's
; own declaration): those buffers are reached through SS = LOW_SEG, so their
; labels have to be offsets from the START of that segment. A bare `section`
; gives them the absolute offset they happen to land at in the flat image -
; 0x3BFC here - and then every disk-visible base is 15KB adrift AND not
; 512-aligned, which is the one thing CLAUDE.md's hard rules say answers
; `int 13h` with error 09h. What it looked like from outside was a mount that
; printed its first message and never came back.
section .lowbss nobits vstart=0
section .bss  nobits
section .text
kd_text_start:
; **THE FIRST BYTES OF THE IMAGE ARE A JUMP**, because kern_dos is ENTERED and
; not booted: SPEC.md 87.5's stub reads it into low memory and far-jumps to its
; base (docs/plans/KERN-DOS-PLAN.md 2), so offset 0 has to be somewhere to go.
;
; **AND THE JUMP IS BEFORE THE SHIM'S OWN `section .text`**, which is not a
; style point: kdshim.inc opens `.text` to put its stubs in, so including it
; first puts `fpg_begin`'s `ret` at offset 0. The far jump then returns
; through whatever the loader left on the stack, and what that looks like from
; outside is a loader that printed `go` and a machine that said nothing ever
; again.
%ifdef KD_GATE
    jmp kd_gate_entry
%endif
%include "kdshim.inc"           ; what the kernel's own files name
%include "dskwin.inc"           ; the mount-owned window (SPEC.md 2.1.2)
%include "disk.inc"             ; volumes, mount, the FAT read path
%include "diskw.inc"            ; the FAT write path
%include "kdgate.inc"           ; wave 3's mount-and-read gate, KD_GATE only
; --- and the image's own size, which is what the rung has to clear ---------
; A length is measured INSIDE its own section, `$$` being that section's
; start. Across two sections nasm will not subtract at all ("operands differ
; by a non-scalar"), which is the assembler declining to answer a question
; the caller has got wrong.
section .text
kd_e_text:
KD_S_TEXT equ kd_e_text - $$
section .cold
kd_e_cold:
KD_S_COLD equ kd_e_cold - $$
section .ovlw
kd_e_ovlw:
KD_S_OVLW equ kd_e_ovlw - $$
section .modf
modf_end:                       ; diskw.inc writes this into a module header
kd_e_modf:                      ; it will never build one of (SPEC.md 2.8)
KD_S_MODF equ kd_e_modf - $$
section .bss
kd_e_bss:
KD_S_BSS equ kd_e_bss - $$
section .lowbss
kd_e_lowbss:
KD_S_LOWBSS equ kd_e_lowbss - $$
section .text
kd_text_end:

; --- what the ladder asserted ------------------------------------------------
; **THE WHOLE IMAGE AND NOT `.text`.** FAT_SEG sits on top of this rung, so a
; rung short by a kilobyte puts the FAT snapshot INSIDE the code: measuring
; `.text` alone passed at 40KB on a 45KB image, and the mount then overwrote
; the routine that called it.
KTEXT_SIZE equ KD_S_TEXT + KD_S_COLD + KD_S_OVLW + KD_S_MODF
; **AND THE BSS, WHICH `-f bin` PUTS ABOVE THE IMAGE** and which read `equ 0`
; here while it was thousands of bytes. It is the SAME hazard as the one the
; paragraph above records, one section along: `.bss` is nobits so it costs no
; disk, but it occupies address space between the last emitted byte and
; FAT_SEG - so a rung that clears the image and not the bss puts the FAT
; snapshot in the DOS core's own variables, and nothing says so until a mount
; overwrites a handle table.
KBSS_SIZE  equ KD_S_BSS
KIMG_SIZE  equ KTEXT_SIZE + KBSS_SIZE
%if KIMG_SIZE > KD_IMG_KB * 1024
 %error "kern_dos outgrew KD_IMG_KB - raise it in kdlayout.inc, and note that \
the whole of kern_dos has about 39KB before the 600KB target is missed \
(docs/plans/KERN-DOS-PLAN.md 1)"
%endif

; ...and the same question for `.lowbss`, which sits at LOW_SEG under the
; stack rather than above the image: KD_LOW_KB has to hold the disk layer's
; buffers AND leave room for KD_STACK to grow down into.
%if KD_S_LOWBSS > KD_STACK
 %error "kern_dos's .lowbss buffers reach the stack - raise KD_LOW_KB"
%endif
