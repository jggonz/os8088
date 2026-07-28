; =============================================================================
; os8088 1.0 - kernel
;
; A Macintosh-style GUI for the 8086: 640x480x16 (VGA mode 12h), pre-emptive
; round-robin multitasking off the PIT, serial Microsoft mouse on COM1.
;
; Runs in real mode, tiny model: CS = DS = SS = KERNEL_SEG for all kernel code
; and every task; ES is scratch. All inter-module calls are near. The module
; contracts (register use, data layouts, concurrency rules) live in SPEC.md;
; that document is binding.
;
; Three fixed entry points:
;   1000:0000  cold entry (the boot sector jumps here)
;   1000:0008  boot splash tick (SPEC.md 15): far-called by the boot sector
;              after every sector it reads, while the rest of this image is
;              still coming off the floppy
;   1000:0010  os8088 API jump table (SPEC.md 20.3): 4-byte near-jmp slots at
;              pinned offsets, called by loaded programs (replaces the
;              retired syscall gate)
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

; loadable programs (SPEC.md 20)
APP_LOAD_OFF equ 0xA000         ; where packages load (kernel segment offset)
APP_MAX_SIZE equ 0x5000         ; image + bss budget, 0xA000..0xEFFF

; double buffering (SPEC.md 32)
BB_SEG        equ 0x4000        ; back buffer base segment (plane 0)
BB_PLANE_PARA equ 0x960         ; paragraphs per plane (0x9600 = 480 rows x 80)
DB_MIN_KB     equ 500           ; int 12h floor: double-buffer only at >= 500KB
                                ; (a real 512KB machine reports less once the
                                ; BIOS takes its cut - 500 lets those in, and
                                ; the buffer ends at 0x657FF = 406KB anyway)

; =============================================================================
; Fixed entry points
; =============================================================================
cold_entry:
    jmp kmain

    times 0x08 - ($ - $$) db 0
    jmp near spl_tick           ; 1000:0008 - boot splash tick (SPEC.md 15)

    times 0x10 - ($ - $$) db 0  ; the table must land exactly at 0x0010

; =============================================================================
; os8088 API jump table (SPEC.md 20.3)
;
; Loaded programs `call` these pinned absolute offsets; each slot is a 4-byte
; cell: `jmp near target` (3 bytes) + 1 pad byte. Register contracts are the
; target routines' own. The slot order below IS the ABI - never reorder.
; =============================================================================
%macro OSAPI_SLOT 1
    jmp near %1                 ; E9 rel16 = 3 bytes
    db 0                        ; pad to a 4-byte cell
%endmacro

osapi_table:
    OSAPI_SLOT gfx_lock          ; 0x0010
    OSAPI_SLOT gfx_unlock        ; 0x0014
    OSAPI_SLOT gfx_pixel         ; 0x0018
    OSAPI_SLOT gfx_hline         ; 0x001C
    OSAPI_SLOT gfx_vline         ; 0x0020
    OSAPI_SLOT gfx_fill          ; 0x0024
    OSAPI_SLOT gfx_frame         ; 0x0028
    OSAPI_SLOT gfx_fill_gray     ; 0x002C
    OSAPI_SLOT gfx_xor_rect      ; 0x0030
    OSAPI_SLOT gfx_xor_fill      ; 0x0034
    OSAPI_SLOT font_char         ; 0x0038
    OSAPI_SLOT font_str          ; 0x003C
    OSAPI_SLOT font_width        ; 0x0040
    OSAPI_SLOT wm_create         ; 0x0044
    OSAPI_SLOT wm_show           ; 0x0048
    OSAPI_SLOT wm_hide           ; 0x004C
    OSAPI_SLOT wm_front          ; 0x0050
    OSAPI_SLOT wm_content        ; 0x0054
    OSAPI_SLOT wm_obscured       ; 0x0058
    OSAPI_SLOT task_yield        ; 0x005C
    OSAPI_SLOT task_sleep        ; 0x0060
    OSAPI_SLOT osapi_get_ticks    ; 0x0064
    OSAPI_SLOT osapi_set_color    ; 0x0068
    OSAPI_SLOT osapi_mouse        ; 0x006C
    OSAPI_SLOT osapi_srand        ; 0x0070
    OSAPI_SLOT osapi_rand         ; 0x0074
osapi_table_end:                 ; 0x0078

; build-time assertions: the table's start and span are ABI, prove them here
OSAPI_TABLE_OFF equ osapi_table - $$
OSAPI_TABLE_LEN equ osapi_table_end - osapi_table
%if OSAPI_TABLE_OFF != 0x0010
%error "os8088 API jump table must start at offset 0x0010"
%endif
%if OSAPI_TABLE_LEN != 26 * 4
%error "os8088 API jump table must be exactly 26 4-byte slots"
%endif

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
    call bb_init                ; RAM probe + back buffer (SPEC.md 32): after
                                ; the mode set (VRAM just cleared, planes
                                ; start in sync), before the first drawing
    call font_init              ; needs int 10h, so after the mode is set
    call wm_init
    call inst_init              ; instance table (SPEC.md 29) - clean boot:
                                ; no app instances exist until launched
    call mouse_init             ; IRQ4 live; cursor stays hidden until shown
    call desk_init              ; count floppy drives for the desktop icons
    call dock_init              ; dock strip scratch (SPEC.md 30)
    call files_init             ; Disk module state (no window at boot)
    call loader_init            ; package loader state
    call tm_init                ; Task Manager total-RAM read (no window)

    call gfx_lock
    call wm_paint_all
    call gfx_unlock
    call cursor_show

    jmp ui_task                 ; task 0 becomes the UI task; never returns

; =============================================================================
; osapi helpers (SPEC.md 20.4) - tiny accessors for loaded programs, reached
; only through the jump table. Each preserves all registers except its
; documented outputs.
; =============================================================================

; ---- osapi_get_ticks - out: AX = [ticks] -------------------------------------
osapi_get_ticks:
    mov ax, [ticks]
    ret

; ---- osapi_set_color - in: AL -> [gfx_color] ---------------------------------
osapi_set_color:
    mov [gfx_color], al
    ret

; ---- osapi_mouse - out: CX = [mouse_x], DX = [mouse_y], AL = [mouse_btn] -----
osapi_mouse:
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    mov al, [mouse_btn]
    ret

; ---- osapi_srand - in: AX -> [osapi_seed] -------------------------------------
osapi_srand:
    mov [osapi_seed], ax
    ret

; ---- osapi_rand - seed = seed*25173 + 13849; out: AX = new seed --------------
osapi_rand:
    push dx                     ; mul clobbers DX; only AX is an output
    mov ax, [osapi_seed]
    mov dx, 25173
    mul dx                      ; DX:AX = seed * 25173; keep the low word
    add ax, 13849
    mov [osapi_seed], ax
    pop dx
    ret

osapi_seed:  dw 0                ; PRNG state (inline data: .bss takes no init)

; -----------------------------------------------------------------------------
%include "splash.inc"           ; FIRST: must be resident within the image's
                                ; opening SPL_RESIDENT sectors (SPEC.md 15)
%include "vga12.inc"
%include "vgabb.inc"
%include "font.inc"
%include "mouse.inc"
%include "sched.inc"
%include "events.inc"
%include "wm.inc"
%include "instance.inc"
%include "menu.inc"
%include "ui.inc"
%include "apps.inc"
%include "disk.inc"
%include "loader.inc"
%include "files.inc"
%include "icons.inc"
%include "desk.inc"
%include "dock.inc"
%include "taskmgr.inc"
%include "ctrl.inc"

; =============================================================================
; Size guard: image + bss must stay below APP_LOAD_OFF (0xA000) - everything
; above it belongs to the loaded-program region (SPEC.md 20). Same-section
; label differences bound via equ - a bare label in %if is a non-scalar and
; will not assemble.
; =============================================================================
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

section .bss
; modules declared their own .bss blocks; NASM accumulates them in order,
; so this lands last
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$
%if KTEXT_SIZE + KBSS_SIZE > 0xA000
%error "kernel too big: image + bss must stay below APP_LOAD_OFF (0xA000)"
%endif
