; =============================================================================
; jop 1.0 - kernel
;
; A Macintosh-style GUI for the 8086: 640x480x16 (VGA mode 12h), pre-emptive
; round-robin multitasking off the PIT, serial Microsoft mouse on COM1.
;
; Runs in real mode, tiny model: CS = DS = SS = KERNEL_SEG for all kernel code
; and every task; ES is scratch. All inter-module calls are near. The module
; contracts (register use, data layouts, concurrency rules) live in SPEC.md;
; that document is binding.
;
; Two fixed entry points:
;   1000:0000  cold entry (the boot sector jumps here)
;   1000:0010  syscall gate (kept for ABI compatibility; the GUI ignores it)
; =============================================================================

cpu 8086
bits 16
org 0x0000

; --- global constants (SPEC.md 3) -------------------------------------------
KERNEL_SEG  equ 0x1000
SAVE_SEG    equ 0x2000          ; save-under heap (menus), via ES
VGA_SEG     equ 0xA000          ; mode 12h planar framebuffer
SCREEN_W    equ 640
SCREEN_H    equ 480
ROW_BYTES   equ 80
MBAR_H      equ 20              ; menu bar height, px
TITLE_H     equ 18              ; window title bar height, px

CBLACK      equ 0
CWHITE      equ 15
CLGRAY      equ 7
CDGRAY      equ 8

; =============================================================================
; Fixed entry points
; =============================================================================
cold_entry:
    jmp kmain

    times 0x10 - ($ - $$) db 0  ; the gate must land exactly at 0x0010

syscall_gate:
    retf                        ; the text-shell ABI is retired; keep the slot

; =============================================================================
; Boot (SPEC.md 15)
; =============================================================================
kmain:
    cli
    mov ax, KERNEL_SEG          ; the boot sector jumped here with its own
    mov ds, ax                  ; segments; setting ours up is our job
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFE
    sti
    cld

    call sched_init             ; pre-emption live from here on
    call evq_init
    call vga_mode12
    call font_init              ; needs int 10h, so after the mode is set
    call wm_init
    call mouse_init             ; IRQ4 live; cursor stays hidden until shown
    call apps_init              ; windows + background tasks

    call gfx_lock
    call wm_paint_all
    call gfx_unlock
    call cursor_show

    jmp ui_task                 ; task 0 becomes the UI task; never returns

; -----------------------------------------------------------------------------
%include "vga12.inc"
%include "font.inc"
%include "mouse.inc"
%include "sched.inc"
%include "events.inc"
%include "wm.inc"
%include "menu.inc"
%include "ui.inc"
%include "apps.inc"

; =============================================================================
; Size guard: image + bss must stay below 0xF000 so the task stacks and the
; boot stack at 0xFFFE keep clear air. Same-section label differences bound
; via equ - a bare label in %if is a non-scalar and will not assemble.
; =============================================================================
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

section .bss
; modules declared their own .bss blocks; NASM accumulates them in order,
; so this lands last
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$
%if KTEXT_SIZE + KBSS_SIZE > 0xF000
%error "kernel too big: image + bss must stay below 0xF000"
%endif
