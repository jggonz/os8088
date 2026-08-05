; =============================================================================
; os8088 1.0 - kernel
;
; A Macintosh-style GUI for the 8086: 640x480x16 (VGA mode 12h), pre-emptive
; round-robin multitasking off the PIT, serial Microsoft mouse on COM1.
;
; Runs in real mode, near model: CS = DS = KERNEL_SEG for all kernel code and
; every task, SS = LOW_SEG, ES scratch. EVERY inter-module call inside the
; kernel is near - there is no far code and no second code segment. The
; module contracts (register use, data layouts, concurrency rules) live in
; SPEC.md; that document is binding.
;
; Three fixed entry points, at KERNEL_SEG (0x0060 - see the ladder below):
;   0060:0000  cold entry (the boot sector jumps here)
;   0060:0008  boot splash tick (SPEC.md 15): far-called by the boot sector
;              after every sector it reads, while the rest of this image is
;              still coming off the floppy
;   0060:0010  os8088 API jump table (SPEC.md 20.3): 8-byte DS-switching cells
;              at pinned offsets, far-called by loaded packages
; =============================================================================

cpu 8086
bits 16
org 0x0000

; --- global constants (SPEC.md 3) -------------------------------------------
KERNEL_SEG  equ 0x0060          ; linear 0x00600 - the first paragraph above
                                ; the BIOS data area, and the base of the ONE
                                ; 64KB region the whole kernel lives in
                                ; (docs/KERNEL-MEMORY.md). The boot sector no
                                ; longer floors it: boot/boot.asm relocates
                                ; itself out of the landing zone before it
                                ; reads a single sector
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

; --- the clip region (SPEC.md 11.3) ------------------------------------------
; wm.inc builds it; vga12.inc, font.inc and icons.inc consume it. The layout
; is therefore a cross-module contract and lives here rather than in wm.inc,
; which NASM only reaches five includes later.
WM_CLIP_MAX equ 16              ; rects; more than this and the frame is
                                ; skipped instead, which is exactly what the
                                ; wm_obscured veto used to do
WCR_X1  equ 0                   ; one clip rect, 8 bytes, inclusive corners
WCR_Y1  equ 2
WCR_X2  equ 4
WCR_Y2  equ 6
WCR_SZ  equ 8

; loadable programs (SPEC.md 20) - a package's region is a HEAP CLAIM
APP_MAX_SIZE equ 0xF000         ; the biggest single package: 60KB, and the
                                ; ceiling is now the SEGMENT rather than a
                                ; pool - a package links at org 0 and
                                ; addresses itself with 16-bit offsets, so
                                ; image + bss cannot reach 64KB whatever the
                                ; heap has free. Mirrored in apps/os88api.inc
                                ; and tools/os88pkg.py
PKG_DISP     equ 12             ; the dispatcher's fixed offset INSIDE the
                                ; .o88 header (SPEC.md 20.2): three bytes,
                                ; `call bp / retf`. Every kernel-to-package
                                ; call lands here with BP = the real target,
                                ; which is what lets every package callback
                                ; stay a near proc with a near `ret`

; =============================================================================
; The memory map (SPEC.md 2). ONE ladder, bottom to top, every rung derived
; from the one below it, and NO growth room anywhere in it: each rung is the
; measured size of what it holds, so the heap starts wherever this build's
; kernel happens to end and moves when the kernel does.
;
;   0x00000  IVT + BIOS data area                    (theirs, 1,536 B)
;   0x00600  KERNEL_SEG  .text + .bss                KIMG_PARA (derived)
;            FAT_SEG     mount-time FAT snapshot     FAT_PARA
;            LOW_SEG     .lowbss + task 0's stack    LOW_PARA
;            HEAP_SEG    the claim heap              up to int 12h's top
;
; **There is no package pool.** It was a fixed 60KB reservation between the
; kernel and the heap - unavailable to anything else whether or not a package
; was loaded - and a package's region is an ordinary heap claim now (SPEC.md
; 20.1/50), taken from the TOP of the heap downward while data claims grow
; up from the bottom. That returned 60KB to every machine: 510KB -> 570KB of
; heap on a 640KB one, and it is the whole reason a 128KB machine can run
; this at all (the pool's own top used to sit above 128KB, so such a machine
; had no heap and could load nothing).
;
; **Everything from KERNEL_SEG to the end of task 0's stack is the kernel**,
; and guard 1 holds that whole span to KERN_BUDGET - 75KB just above the
; BIOS data area. Code, data, scratch, the FAT snapshot, the disk buffers and
; every task stack are inside it. The one deliberate exception is the menu
; save-under, which is a heap claim (SPEC.md 12.4/50) because it is 20KB that
; only exists while a menu is down.
;
; The sizes are measured, not guessed. With a 0xCC fill in every byte of the
; stack region and the machine driven hard - Clock, two Bounces, About, the
; Control Panel on both its pages, the Task Manager with a window drag, a Disk
; window, the Fractal with its worker task, and Paint saving a GIF into a
; folder it created from the file dialog - the deepest mark left was 246 bytes
; on task 0's stack and 150 on a background task's.
; =============================================================================
KERN_BUDGET equ 76800           ; the whole kernel, guard 1. Growing past this
                                ; is not a build detail - see
                                ; docs/KERNEL-MEMORY.md before raising it.
                                ; It has moved three times, every one asked
                                ; for and granted: 65,536 -> 71,680 for the
                                ; SPEC.md 41 store and the two API surfaces
                                ; that came with it from the other fork;
                                ; 71,680 -> 72,704 for the driver subsystem
                                ; (SPEC.md 51) and the Control Panel pages
                                ; that drive it - which BUYS more than it
                                ; spends, the OPL2 and Sound Blaster code it
                                ; makes loadable being thousands of lines that
                                ; would otherwise be resident on a machine
                                ; with neither card; and 72,704 -> here for
                                ; SPEC.md 51.5's keyed SYSTEM.CFG, granted in
                                ; ADVANCE of further work with an optimisation
                                ; pass to follow, so the slack under this one
                                ; is temporary rather than an invitation

; The relocated boot sector (boot/boot.asm). The kernel now lands at 0x00600
; and runs up through 0x7C00, where the BIOS put the sector that is reading
; it - so the sector copies ITSELF out of the way first, keeping its own
; offset so every label in it still resolves at org 0x7C00. BOOT_RELOC:7C00
; is linear 0x13C00; its stack grows down from there, and guard 5 keeps the
; kernel clear of both. Both constants are mirrored in boot/boot.asm.
BOOT_RELOC  equ 0x0C00          ; 0x0C00*16 + 0x7C00 = linear 0x13C00
BOOT_LIN    equ BOOT_RELOC*16 + 0x7C00
BOOT_STACK  equ 2048            ; stack room below it

DSK_FAT_SECS equ 9              ; resident FAT cap, sectors (4,608 bytes).
                                ; Exactly what the largest geometry this OS
                                ; boots or builds declares: 1.44MB = 9, 1.2MB
                                ; = 7, 720KB = 3, 360KB = 2. It is an
                                ; ACCEPTANCE threshold (SPEC.md 18.2 rule 10),
                                ; not a buffer with slack: a volume claiming
                                ; more is refused before a byte of it is read,
                                ; and every FAT16 volume there can be is
                                ; refused by this number alone (a FAT is only
                                ; FAT16 with >= 4,085 clusters, i.e. >= 16 FAT
                                ; sectors)
FAT_PARA    equ DSK_FAT_SECS * 32     ; 512 bytes = 32 paragraphs

STK0_SIZE   equ 1024            ; task 0's stack - the UI task's, and so the
                                ; one every window callback, every menu track
                                ; and every file-dialog interaction runs on.
                                ; 4x its measured 246-byte high-water mark.
                                ; It is a CONSTANT now: it used to be "whatever
                                ; is left between .lowbss and the kernel", so
                                ; every byte saved anywhere below simply made
                                ; this bigger and freed nothing at all

; the file manager's per-window view cache (SPEC.md 2.3/22.1) - a heap claim
; per open Disk window now, not four pinned 4KB slots reserved from boot.
; What it buys is unchanged: a background file-manager window paints from
; memory, so wm_paint_all (no clip rect, on every window move) costs no
; floppy I/O. What changed is that a machine with no Disk window open pays
; nothing for it, and the Task Manager can bill the 3KB to the window.
VIEW_SLOTS  equ 4               ; max Disk windows = the kind's KD_CAP
VIEW_KB     equ 3               ; each cache: 1KB of entries + 2KB of icons

; --- the derived ladder -------------------------------------------------------
; Every base below is the one before it plus the MEASURED size of what it
; holds. KIMG_PARA and LOW_PARA forward-reference the section sizes at the end
; of this file, which is legal because a segment VALUE never changes an
; instruction's length - `mov ax, imm16` is `mov ax, imm16` whatever the
; immediate turns out to be, so NASM converges on the second pass.
; The image rounds up to a whole 512 BYTES, not to a paragraph, and that is
; not tidiness - it is what keeps every rung above it 512-aligned. int 13h
; moves one sector per call, which bounds a transfer to 512 bytes but does
; NOT stop one from straddling a 64KB physical boundary: only starting on a
; 512-byte boundary does that, and the DMA controller answers a straddle with
; error 09h. Every base below is an int 13h target - the FAT snapshot, the
; disk buffers, a package image, a package's file buffer out of the heap -
; and FAT_PARA (288) and LOW_PARA are both multiples of 32
; paragraphs, so aligning this one rung aligns the whole ladder. Guard 6
; proves it. It used to hold by luck: every base in the map was a round
; constant like 0x0300 or 0x2A00, and nothing said why that mattered.
KIMG_PARA   equ ((KTEXT_SIZE + KBSS_SIZE + 511) / 512) * 32   ; image + scratch
FAT_SEG     equ KERNEL_SEG + KIMG_PARA   ; mount-time FAT snapshot
                                ; (SPEC.md 2.1/18), reached via ES ONLY,
                                ; never DS; dsk_next_clus is the one reader
LOW_SEG     equ FAT_SEG + FAT_PARA    ; .lowbss (task stacks + disk buffers)
                                ; and, on top of it, task 0's own stack
LOW_PARA    equ ((KLOW_SIZE + STK0_SIZE + 511) / 512) * 32
STK0_TOP    equ KLOW_SIZE + STK0_SIZE - 2   ; task 0's stack top, growing down
                                ; onto the top of .lowbss; guard 3 proves the
                                ; two cannot meet
KERN_END    equ LOW_SEG + LOW_PARA    ; ...and there the kernel stops
KERN_SIZE   equ (KERN_END - KERNEL_SEG) * 16   ; what guard 1 measures

HEAP_SEG    equ KERN_END        ; the claim heap (SPEC.md 50) starts where
                                ; the kernel ACTUALLY ends, not where a
                                ; budget said it might, and runs to whatever
                                ; int 12h reports. Package regions and data
                                ; claims share it from opposite ends
                                ; (SPEC.md 50.3); nothing up here has a fixed
                                ; address any more

; double buffering (SPEC.md 32) - the back buffer is a heap CLAIM now, so
; there is no BB_SEG constant: bb_init asks for BB_KB and remembers what it
; got, and on a machine that cannot fund it the Control Panel says so.
BB_PLANE_PARA equ 0x960         ; paragraphs per plane (0x9600 = 480 rows x 80)
BB_KB         equ 150           ; 4 planes x 0x9600 bytes, in KB

; --- CPU tiers and memory above 1MB (SPEC.md 41) -----------------------------
; None of this exists on tier 0, which is the target machine: an 8088 has no
; A20 line, nothing above linear 0x0FFFFF, and every routine keyed off these
; constants returns having touched no port. The tier is INFORMATION, not
; permission - kernel code branches on the verified feature bits and packages
; branch on the KB figure from osapi_xmem_caps (SPEC.md 41.1/41.8).
CPU_8086    equ 0               ; tier 0: 8086/8088. No A20, no HMA, no store.
                                ; The default [cpu_tier] and the fallback.
CPU_286     equ 1               ; tier 1: A20 gate + HMA, int 15h AH=88h
                                ; sizing and AH=87h block move (SPEC.md 41.5)
CPU_386     equ 2               ; tier 2: all of tier 1, plus unreal mode -
                                ; a 4GB data limit on FS/GS (SPEC.md 41.4)
HMA_SEG     equ 0xFFFF          ; the one segment above 1MB: HMA_SEG:0010 is
HMA_MIN_OFF equ 0x0010          ; linear 0x100000 (0xFFFF0 + 0x10) and
HMA_BYTES   equ 0xFFF0          ; HMA_SEG:FFFF is linear 0x10FFEF - the
                                ; highest byte real mode can name at all.
                                ; 65,520 bytes, DATA ONLY: the near model
                                ; pins CS = DS = KERNEL_SEG, so no code ever
                                ; lives up there (SPEC.md 41.3/41.9 rule 3)
XM_HMA_KB   equ 64              ; what a successful cpu_hma_claim takes off
                                ; the xm pool - the HMA is the first 64KB of
                                ; exactly the RAM AH=88h sizes (SPEC.md 2.4)
XM_MAX_BLKS equ 8               ; xm_alloc's fixed block table, entries: a
                                ; bulk store for a handful of large claims,
                                ; not a malloc (SPEC.md 41.5)

; =============================================================================
; Section layout (SPEC.md 2.1) - declared here, once, with attributes; every
; module afterwards switches with a bare `section .text` / `.bss` / `.lowbss`.
; NASM's -f bin resolves the attributes at layout time, so a forward reference
; to .text below is fine.
;
;   .text     the kernel image, org 0, KERNEL_SEG. ALL of it: there is no
;             .fartext any more. Cold modules used to be copied down to a
;             second segment below the kernel to buy window space, and the
;             reserve that mechanism needed was 10,752 bytes of low memory for
;             a 5,455-byte blob - so it cost more RAM than it saved the moment
;             the kernel stopped being the thing that was short (SPEC.md 33).
;   .bss      kernel scratch, KERNEL_SEG, vfollows=.text.
;   .lowbss   task stacks and the disk buffers, in LOW_SEG (SPEC.md 2.1) -
;             above the kernel image now, not below it. vstart=0, addressed
;             through SS or ES, never DS.
; =============================================================================
section .lowbss  nobits vstart=0
section .bss     nobits vfollows=.text valign=1
section .text

; =============================================================================
; Fixed entry points
; =============================================================================
cold_entry:
    jmp kmain

    times 0x08 - ($ - $$) db 0
    jmp near spl_tick           ; 0800:0008 - boot splash tick (SPEC.md 15)

    times 0x10 - ($ - $$) db 0  ; the table must land exactly at 0x0010

; =============================================================================
; os8088 API jump table (SPEC.md 20.3)
;
; Loaded programs FAR-call these pinned absolute offsets: 8-byte cells at
; 0x0010 + 8n, each one a complete DS switch around a near call into the
; kernel routine named in the comment. The slot order below IS the ABI -
; never reorder.
;
; Why 8 bytes and a DS switch. Since SPEC.md 20.1 a package lives in its OWN
; segment, so CS and DS are the package's on the way in and must be the
; KERNEL's while kernel code runs. `push cs / pop ds` is the two-byte way to
; say that (the table is .text, so CS is KERNEL_SEG here), and it clobbers no
; register - which matters, because every register in this ABI is an argument
; to something. The cell is exactly:
;
;       push ds / push cs / pop ds / call near target / pop ds / retf
;
; POP and RETF touch no flags, so a routine's CF answer survives the return
; (menu_win_set and inst_pkg_alive contractually preserve the FLAGS word).
;
; Three cells need more than that and jump to a stub below instead:
;   X  the caller's DS must reach the kernel routine (a package pointer it
;      has to dereference) - the stub puts it in ES
;   N  the caller passes a NUL name at DS:SI while ES:BX is already spoken
;      for by a data buffer - the stub STAGES the name into kernel scratch,
;      the dsk_get_dir idiom of SPEC.md 2.1
; =============================================================================
%macro OSAPI_SLOT 1                 ; 8 bytes exactly
    push ds
    push cs
    pop ds
    call %1
    pop ds
    retf
%endmacro

%macro OSAPI_JSLOT 1                ; a cell that defers to a longer stub
    jmp near %1                     ; E9 rel16 = 3 bytes
    times 5 db 0
%endmacro

osapi_table:
    OSAPI_SLOT gfx_lock           ; 0x0010
    OSAPI_SLOT gfx_unlock         ; 0x0018
    OSAPI_SLOT gfx_pixel          ; 0x0020
    OSAPI_SLOT gfx_hline          ; 0x0028
    OSAPI_SLOT gfx_vline          ; 0x0030
    OSAPI_SLOT gfx_fill           ; 0x0038
    OSAPI_SLOT gfx_frame          ; 0x0040
    OSAPI_SLOT gfx_fill_gray      ; 0x0048
    OSAPI_SLOT gfx_xor_rect       ; 0x0050
    OSAPI_SLOT gfx_xor_fill       ; 0x0058
    OSAPI_SLOT font_char          ; 0x0060
    OSAPI_JSLOT api_font_str      ; 0x0068  X: the string is package data
    OSAPI_JSLOT api_font_width    ; 0x0070  X
    OSAPI_JSLOT api_wm_create     ; 0x0078  X: so is the template
    OSAPI_SLOT wm_show            ; 0x0080
    OSAPI_SLOT wm_hide            ; 0x0088
    OSAPI_SLOT wm_front           ; 0x0090
    OSAPI_SLOT wm_content         ; 0x0098
    OSAPI_SLOT wm_obscured        ; 0x00A0
    OSAPI_SLOT task_yield         ; 0x00A8
    OSAPI_SLOT task_sleep         ; 0x00B0
    OSAPI_SLOT osapi_get_ticks    ; 0x00B8
    OSAPI_SLOT osapi_set_color    ; 0x00C0
    OSAPI_SLOT osapi_mouse        ; 0x00C8
    OSAPI_SLOT osapi_srand        ; 0x00D0
    OSAPI_SLOT osapi_rand         ; 0x00D8
    OSAPI_SLOT osapi_snd_caps     ; 0x00E0 - sound (SPEC.md 34): what the PC
    OSAPI_SLOT osapi_snd_tone     ; 0x00E8   speaker can do, a tone, and a
    OSAPI_SLOT osapi_snd_play     ; 0x00F0   clip out of the caller's buffer
    OSAPI_JSLOT api_snd_fm        ; 0x00F8 - FM verbs (SPEC.md 34.2). X: a
                                  ;          patch-load's 11 bytes are the
                                  ;          caller's, and only live while a
                                  ;          sound DRIVER is loaded (51.4)
    OSAPI_JSLOT api_snd_stream    ; 0x0100 - PCM_BG streams (SPEC.md 34.5),
                                  ;          likewise the driver's. Both
                                  ;          answer CF=1 with no driver, which
                                  ;          is the same thing the held cells
                                  ;          they replaced did (SPEC.md 34.5/
                                  ;          34.6); holding the two numbers is
                                  ;          what puts every slot below back
                                  ;          on `main`'s address
    OSAPI_SLOT wm_sizable         ; 0x0108 - window features (SPEC.md 11.1)
    OSAPI_SLOT wm_fullscreen      ; 0x0110 - fullscreen (SPEC.md 11.2)
    OSAPI_SLOT wm_grow_paint      ; 0x0118 - grow-box restore (SPEC.md 11.1)
    OSAPI_JSLOT api_file_write    ; 0x0120 - files (SPEC.md 18.4/20.3): N,
    OSAPI_JSLOT api_file_read     ; 0x0128   because ES:BX is the data buffer
    OSAPI_JSLOT api_file_delete   ; 0x0130   and the name still has to cross
    OSAPI_JSLOT api_file_rename   ; 0x0138   (two names, this one)
    OSAPI_SLOT dskw_dfree         ; 0x0140
    OSAPI_SLOT menu_win_set       ; 0x0148 - app menus (SPEC.md 12.2): the
                                  ;          set's segment comes from the
                                  ;          window, so no stub is needed
    OSAPI_JSLOT api_fdlg_open     ; 0x0150 - the Standard File dialog
                                  ;          (SPEC.md 38.6): N, for the
                                  ;          default name
    OSAPI_SLOT osapi_video        ; 0x0158 - runtime screen geometry (39.2)
    OSAPI_JSLOT api_pkg_spawn     ; 0x0160 - worker tasks (SPEC.md 20.6): X,
                                  ;          the ownership fence needs to
                                  ;          know which segment is calling
    OSAPI_SLOT inst_pkg_alive     ; 0x0168
    OSAPI_SLOT wm_clip_set        ; 0x0170 - the clip region (SPEC.md 11.3)
    OSAPI_SLOT wm_clip_clear      ; 0x0178
    OSAPI_SLOT wm_clip_test       ; 0x0180
    OSAPI_SLOT cpu_info           ; 0x0188 - CPU tiers and memory above 1MB
    OSAPI_SLOT xm_caps            ; 0x0190   (SPEC.md 41): each body already
    OSAPI_SLOT xm_alloc           ; 0x0198   answers its SPEC.md 20.3 contract
    OSAPI_SLOT xm_free            ; 0x01A0   exactly, so the slots call
    OSAPI_SLOT xm_copy            ; 0x01A8   straight at them - and xm_copy's
                                  ;          ES:SI is the caller's own choice,
                                  ;          so no X stub is involved either
    OSAPI_SLOT wm_geom            ; 0x01B0 - content size + visibility
                                  ;          (SPEC.md 11): the one read a
                                  ;          package on EITHER fork can make
; --- every slot main publishes keeps main's NUMBER (SPEC.md 20.8) -----------
;     The fork moved this block down three cells when it retired the
;     paragraph-counting arena, and a package built against main's SDK then
;     called wm_resize where it meant cm_alloc. main's numbers are the ABI:
;     the three arena slots stay at 0x01B8..0x01C8 as wrappers over the
;     claim heap (osapi_cm_*, kernel/memory.inc), the six slots after them
;     stay where main put them, and everything this fork ADDED starts at
;     0x0200 - main's next free number, which merging makes ours.
    OSAPI_JSLOT api_cm_alloc      ; 0x01B8 - main's v3 arena (SPEC.md 20.8):
                                  ;          AX = PARAGRAPHS -> AX = segment.
                                  ;          X - the owner fence needs the
                                  ;          caller's segment
    OSAPI_JSLOT api_cm_free       ; 0x01C0 - AX = a base segment you own; X
    OSAPI_SLOT osapi_cm_caps      ; 0x01C8 - AX/DX = largest/total free
                                  ;          PARAGRAPHS, BL = free records
    OSAPI_SLOT wm_resize          ; 0x01D0 - resize a window (SPEC.md 11.1):
                                  ;          BX = win, CX = w, DX = h; lock
                                  ;          held. Retires the last liberty
                                  ;          in docs/PAINT-NOTES.md - an app
                                  ;          writing W_W/W_H itself
    OSAPI_SLOT gfx_blit4          ; 0x01D8 - packed 4bpp block (SPEC.md 5.4):
                                  ;          ES:SI = source, BP = stride,
                                  ;          AX/BX = dest, CX/DX = w/h. ES is
                                  ;          the caller's own choice here, so
                                  ;          no stub is needed
    OSAPI_SLOT wm_about_set       ; 0x01E0 - the app-name pull-down (12.2):
                                  ;          BX = win, SI = your About handler
    OSAPI_JSLOT api_file_readbig  ; 0x01E8 - the one file op with no 64KB
                                  ;          ceiling (SPEC.md 18.4): N, and
                                  ;          the destination advances BY
                                  ;          SEGMENT, so a package can load a
                                  ;          116KB module into a heap claim
    OSAPI_SLOT osapi_gfx_dbuf     ; 0x01F0 - a package's own bb_set (SPEC.md
                                  ;          32): AL = 1 arm / 0 disarm, out
                                  ;          AL = the state before, to hand
                                  ;          back. CF=1 on the wrong adapter
                                  ;          or a heap that cannot fund it
    OSAPI_SLOT gfx_scroll         ; 0x01F8 - vertical scroll blit (SPEC.md
                                  ;          5.5): AX/BX/CX/DX = the rect,
                                  ;          SI = signed dy. The vacated rows
                                  ;          are the caller's to repaint
; --- and from here on, the slots this fork ADDS --------------------------------
    OSAPI_JSLOT api_mem_claim     ; 0x0200 - the claim heap (SPEC.md 50.3):
    OSAPI_JSLOT api_mem_free      ; 0x0208   X, same fence as the spawn
    OSAPI_SLOT osapi_mem_avail    ; 0x0210
    OSAPI_SLOT osapi_font_glyphs  ; 0x0218 - the kernel's 8x8 glyph table
                                  ;          (SPEC.md 6): out SI = its offset
                                  ;          in KERNEL_SEG, AL = first code,
                                  ;          AH = last, CX = bytes per glyph
    OSAPI_SLOT wm_onsize          ; 0x0220 - install the resize negotiator
                                  ;          (SPEC.md 11.1): BX = win, AX =
                                  ;          near proc. The other half of
                                  ;          docs/PAINT-NOTES.md's resize
                                  ;          complaint - wm_resize is the app
                                  ;          asking, this is the app answering
    OSAPI_SLOT osapi_file_here    ; 0x0228 - where the file API's names
                                  ;          resolve (SPEC.md 18.4/19.2)
    OSAPI_SLOT osapi_file_goto    ; 0x0230 - ...and how to put it back
    OSAPI_JSLOT api_mem_regrow    ; 0x0238 - resize a claim you already hold
                                  ;          (SPEC.md 50.3): X, same owner
                                  ;          fence as the claim itself. In
                                  ;          place when the paragraphs above
                                  ;          are free, which is what stops a
                                  ;          grow needing old + new at once
    OSAPI_SLOT wm_title_set       ; 0x0240 - retitle a window and redraw ONLY
                                  ;          its caption (SPEC.md 11.92): BX =
                                  ;          win, AX = the new string (0 = the
                                  ;          bytes W_TITLE names changed in
                                  ;          place). Not an X cell: the string
                                  ;          is read through W_SEG, which is
                                  ;          already the caller's segment
    OSAPI_JSLOT api_drv_task      ; 0x0248 - a DRIVER's worker task (SPEC.md
                                  ;          51.7): AX = a near entry in its
                                  ;          own segment, or 0 = "this IS the
                                  ;          worker, and it is exiting". X,
                                  ;          because the fence is an identity
                                  ;          test on the caller's segment
    OSAPI_JSLOT api_mem_claim_dma ; 0x0250 - a claim an ISA DMA controller can
                                  ;          reach (SPEC.md 50.3): AX = KB,
                                  ;          CX = KB of the HEAD that must not
                                  ;          cross a 64KB physical boundary.
                                  ;          X, the claim's own owner fence.
                                  ;          A separate cell and not a CX on
                                  ;          mem_claim, because every existing
                                  ;          caller passes garbage there and
                                  ;          the failure would be silent
osapi_table_end:                  ; 0x0258

; build-time assertions: the table's start and span are ABI, prove them here
OSAPI_TABLE_OFF equ osapi_table - $$
OSAPI_TABLE_LEN equ osapi_table_end - osapi_table
%if OSAPI_TABLE_OFF != 0x0010
%error "os8088 API jump table must start at offset 0x0010"
%endif
%if OSAPI_TABLE_LEN != 73 * 8
%error "os8088 API jump table must be exactly 73 8-byte slots"
%endif

; =============================================================================
; The stubs the X and N cells jump to (SPEC.md 20.3). Each ends in retf and
; restores every segment register it borrowed.
; =============================================================================

; X: ES = the caller's DS, so the kernel routine can reach package data
%macro OSAPI_XSTUB 2
%1:
    push ds                     ; the caller's DS...
    push es                     ; ...and its ES
    push ds
    pop es                      ; ES = the caller's DS
    push cs
    pop ds                      ; DS = KERNEL_SEG
    call %2
    pop es
    pop ds
    retf
%endmacro

    OSAPI_XSTUB api_font_str,   font_str_x
    OSAPI_XSTUB api_font_width, font_width_x
    OSAPI_XSTUB api_wm_create,  wm_create
    OSAPI_XSTUB api_pkg_spawn,  inst_pkg_spawn
    OSAPI_XSTUB api_mem_claim,  osapi_mem_claim
    OSAPI_XSTUB api_mem_claim_dma, osapi_mem_claim_dma
    OSAPI_XSTUB api_mem_free,   osapi_mem_free
    OSAPI_XSTUB api_cm_alloc,   osapi_cm_alloc
    OSAPI_XSTUB api_cm_free,    osapi_cm_free
    OSAPI_XSTUB api_mem_regrow, osapi_mem_regrow
    OSAPI_XSTUB api_snd_fm,     osapi_snd_fm_x
    OSAPI_XSTUB api_drv_task,   drv_task
    OSAPI_XSTUB api_snd_stream, osapi_snd_stream

; N: the name at the caller's DS:SI is staged into kernel scratch first,
; because ES:BX belongs to the caller's data buffer and cannot carry it
%macro OSAPI_NSTUB 2
%1:
    push ds
    push si
    push di
    push es
    push cs
    pop es                      ; ES = KERNEL for the copy destination
    mov di, api_name
    call api_copyname           ; caller DS:SI -> ES:DI, at most 13 bytes
    pop es                      ; the caller's ES back: it is the buffer
    pop di                      ; and its DI, which fdlg_open needs as an
                                ; input (the completion proc's offset)
    push cs
    pop ds                      ; DS = KERNEL
    mov si, api_name
    call %2
    pop si
    pop ds
    retf
%endmacro

    OSAPI_NSTUB api_file_write,  dskw_write
    OSAPI_NSTUB api_file_read,   dskw_read
    OSAPI_NSTUB api_file_delete, dskw_delete
    OSAPI_NSTUB api_file_readbig, dskw_readbig
    OSAPI_NSTUB api_fdlg_open,   fdlg_open

; ...and the two-name case, which needs DI as well and so is written out
api_file_rename:
    push ds
    push si
    push di
    push es
    push cs
    pop es                      ; ES = KERNEL
    push di                     ; bank the new-name pointer across the first
    mov di, api_name            ; copy, which needs DI itself
    call api_copyname           ; old name
    pop si                      ; SI = the caller's DI = the new name
    mov di, api_name2
    call api_copyname           ; new name
    pop es
    push cs
    pop ds                      ; DS = KERNEL
    mov si, api_name
    mov di, api_name2
    call dskw_rename
    pop di
    pop si
    pop ds
    retf

; -----------------------------------------------------------------------------
; api_copyname - stage a NUL 8.3 name across the segment boundary
; in:  DS:SI = the caller's name, ES:DI = kernel scratch (13 bytes)
; out: nothing (all registers preserved); the copy is NUL-terminated even if
;      the source was not - a package cannot make this run on
; -----------------------------------------------------------------------------
api_copyname:
    push ax
    push cx
    push si
    push di
    cld
    mov cx, 13
.c:
    lodsb
    stosb
    or al, al
    jz .done
    loop .c
    mov byte [es:di-1], 0
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

api_name:   times 13 db 0       ; staged names (.text, not .bss: the file
api_name2:  times 13 db 0       ; slots are reachable before anything clears
                                ; .bss, and -f bin clears nothing)

; =============================================================================
; Boot (SPEC.md 15)
; =============================================================================
kmain:
    cli
    mov ax, KERNEL_SEG          ; the boot sector jumped here with its own
    mov ds, ax                  ; segments; setting ours up is our job
    mov es, ax
    mov ax, LOW_SEG             ; SS is NOT KERNEL_SEG (SPEC.md 2.1): the task
    mov ss, ax                  ; stacks sit in their own segment just above
    mov sp, STK0_TOP            ; the image, so a stack offset stays small and
    sti                         ; the kernel's own 64KB window stays for code
    cld

    call cpu_detect             ; CPU tier + memory above 1MB (SPEC.md 41),
                                ; here and nowhere else: BEFORE sched_init,
                                ; because this is the last moment at which no
                                ; kernel ISR is installed - the unreal-mode
                                ; window inside xm_init runs with CR0.PE set
                                ; and a real-mode IVT, so the only handlers
                                ; that may fire in it are the BIOS's own, and
                                ; a tick lost here costs nothing ([ticks] is
                                ; zeroed by sched_init anyway)
    call cpu_a20_enable         ; ...and VERIFY it: the feature bit is set by
                                ; the wraparound probe, never by the poke
                                ; (SPEC.md 41.2). A no-op on tier 0 - an 8088
                                ; has no gate and port 0x92 belongs to
                                ; something else there
    call xm_init                ; size the store (int 15h AH=88h, on task 0
                                ; per SPEC.md 7), claim the HMA, arm unreal
                                ; mode on tier 2, publish [xm_kb] LAST

    call sched_init             ; pre-emption live from here on
    call evq_init
    call clk_init               ; system clock (SPEC.md 37): probe the RTC,
                                ; or fall back to the fixed date - before the
                                ; mode set, so the very first menu bar paint
                                ; already carries a valid clock
    call vid_init               ; video adapter (SPEC.md 39): probe, publish
                                ; the runtime geometry, set the mode. Re-runs
                                ; what the splash already did, which is what
                                ; wipes the loading screen.
    call mem_init               ; the claim heap (SPEC.md 50): int 12h, the
                                ; empty map. FIRST of the memory users -
                                ; every claim below goes through it
    call bb_init                ; back buffer (SPEC.md 32): can this ADAPTER
                                ; double-buffer? The memory question is asked
                                ; of the heap when the buffer is armed
    call font_init              ; needs int 10h, so after the mode is set
    call wm_init
    call menu_init              ; menu bar owner (SPEC.md 12): Locator, so
                                ; the first wm_paint_all already has a bar
    call inst_init              ; instance table (SPEC.md 29) - clean boot:
                                ; no app instances exist until launched
    call mouse_init             ; IRQ4 live; cursor stays hidden until shown
    call desk_init              ; count floppy drives for the desktop icons
    call dock_init              ; dock strip scratch (SPEC.md 30)
    call files_init             ; Disk module state (no window at boot)
    call loader_init            ; package loader state
    call tm_init                ; Task Manager total-RAM read (no window)
    call drv_init               ; the driver table (SPEC.md 51) - BEFORE
                                ; snd_init, whose tone route reads the
                                ; published service table on its first tick
    call snd_init               ; sound layer (SPEC.md 34.7): saves the 61h
                                ; boot bits, stores its .bss state, publishes
                                ; snd_live LAST - snd_tick has been running
                                ; gated since sched_init hooked int 08h

    call drv_boot               ; ...and load what SYSTEM.CFG asks for
                                ; (SPEC.md 51.3). Before the first paint, so
                                ; a machine whose sound driver loads has
                                ; sound from the first frame; nothing here
                                ; can stop the boot.

    call gfx_lock
    call wm_paint_all
    call gfx_unlock
    call cursor_show

    call drv_notice             ; ...and only NOW say what did not load: a
                                ; window needs a screen that has been painted

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

; ---- osapi_font_glyphs - the kernel's own 8x8 font (SPEC.md 6/20.3) ---------
; out: SI = font_glyphs (an offset in KERNEL_SEG - read it through ES, which
;      is KERNEL_SEG on entry to every callback), AL = FONT_FIRST, AH =
;      FONT_LAST, CX = 8 bytes per glyph, row 0 first, bit 7 leftmost.
;
; For an app that draws text into its OWN pixels rather than onto the screen
; (apps/paint's text tool). Before this it re-probed the ROM font through
; int 10h and carried the kernel's F000:FA6E fallback - 40 lines to arrive
; at a table the kernel already had.
osapi_font_glyphs:
    mov si, font_glyphs
    mov al, FONT_FIRST
    mov ah, FONT_LAST
    mov cx, 8
    ret

; ---- osapi_file_here / osapi_file_goto - the volume's location (SPEC.md 19.2)
;
; The file API resolves every name in the volume's CURRENT directory, which is
; one global word shared by every window and by the Standard File dialog
; (SPEC.md 18.4/19.2). That is fine while a save is happening - the dialog
; leaves the volume in the folder the user picked, so the write lands there -
; and wrong the moment the app wants to write to the SAME PLACE again later,
; because any Disk window navigating anywhere has moved it since.
;
; So an app that means "where I saved last time" has to say so:
;
;   osapi_file_here   out DX = the current directory's first cluster (0 = the
;                     root), BL = the drive (0 = A:, 1 = B:). No disk I/O.
;   osapi_file_goto   in  DX = a cluster from osapi_file_here, BL = its drive;
;                     out CF=1 the volume could not be listed there and is
;                     back at the root with the write gate shut. This is a
;                     REMOUNT (SPEC.md 19.2) - real floppy I/O, UI-task
;                     context only, exactly like the other file slots.
;
; Storing the pair beside the file name is what makes an app's "Save" mean
; the same file its "Save As" chose.
; -----------------------------------------------------------------------------
osapi_file_here:
    mov dx, [dsk_cwd]
    mov bl, [disk_drive]
    ret

osapi_file_goto:
    push ax
    mov ax, dx
    mov dl, bl
    call dsk_chdir              ; CF = the mount failed; it has already put
    pop ax                      ; the volume back at the root
    ret

; ---- osapi_video - the screen the program actually got (SPEC.md 39.2) --------
; out: AX = width, BX = height, CX = first row the dock owns (so the usable
;      desktop is rows MBAR_H..CX-1), DL = 0 VGA / 1 Hercules / 2 CGA,
;      DH = bits per pixel, 4 or 1
osapi_video:
    mov ax, [vid_w]
    mov bx, [vid_h]
    mov cx, [vid_dock_y0]
    mov dl, [vid_kind]
    mov dh, 4
    cmp byte [vid_mono], 0
    je .r
    mov dh, 1
.r:
    ret

osapi_seed:  dw 0                ; PRNG state (inline data: .bss takes no init)

; -----------------------------------------------------------------------------
%include "viddet.inc"           ; video adapters (SPEC.md 39): the splash
                                ; probes and sets the mode on its first tick,
                                ; so this must be resident with it
%include "splash.inc"           ; must be resident within the image's opening
                                ; SPL_RESIDENT sectors (SPEC.md 15)
%include "cpudet.inc"           ; CPU tiers + the A20 line (SPEC.md 41.1-41.3)
%include "xmem.inc"             ; memory above 1MB (SPEC.md 41.4/41.5): after
                                ; cpudet.inc, whose tier and feature bits it
                                ; branches on and whose cpu_hma_claim it calls
%include "vga12.inc"
%include "vgabb.inc"
%include "font.inc"
%include "mouse.inc"
%include "sched.inc"
%include "events.inc"
%include "clock.inc"            ; the system clock (SPEC.md 37): after
                                ; sched.inc, whose [ticks] it advances from
%include "wm.inc"
%include "memory.inc"           ; the claim heap (SPEC.md 50): after
                                ; instance.inc, whose records own the claims
%include "instance.inc"
%include "menu.inc"
%include "ui.inc"
%include "apps.inc"
%include "disk.inc"
%include "diskw.inc"          ; the FAT write path (SPEC.md 18.4): after
                                ; disk.inc, whose constants and layout it uses
%include "loader.inc"
%include "files.inc"
%include "fdlg.inc"             ; the Standard File dialog (SPEC.md 38)
%include "icons.inc"
%include "desk.inc"
%include "dock.inc"
%include "taskmgr.inc"
%include "ctrl.inc"
%include "driver.inc"           ; loadable drivers (SPEC.md 51): after
                                ; diskw (it reads and writes the system disk)
                                ; and memory (a driver image is a claim)
%include "snd.inc"              ; the sound layer (SPEC.md 34): PC speaker

; =============================================================================
; Size guards (SPEC.md 15.1). Same-section label differences bound via equ -
; a bare label in %if is a non-scalar and will not assemble, and a difference
; across two sections is not a constant at all, which is why each section
; measures itself against its own $$.
;
; kernel_text_end MUST be the last thing in .text: it is simultaneously the
; size of the image and the base of .bss (see the section layout at the top of
; this file), and through KIMG_PARA it is where the FAT snapshot begins.
; =============================================================================
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

section .lowbss
kernel_low_end:
KLOW_SIZE equ kernel_low_end - $$

section .bss
; modules declared their own .bss blocks; NASM accumulates them in order,
; so this lands last
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$

; What the Task Manager's RAM view reports (SPEC.md 28), in KB rounded up:
; the whole kernel, buffers and stacks included, because since SPEC.md 2 that
; is one contiguous span and there is nothing of the kernel outside it.
KERN_KB    equ (KERN_SIZE + 1023) / 1024
KBUF_KB    equ ((FAT_PARA + LOW_PARA) * 16 + 1023) / 1024

; 1. THE budget: the whole kernel - image, scratch, FAT snapshot, disk
;    buffers and every task stack - is one span starting at KERNEL_SEG, and
;    it fits KERN_BUDGET (75KB) just above the BIOS data area. This is the guard
;    the project is steering by; raising KERN_BUDGET is a decision, not a
;    build fix (docs/KERNEL-MEMORY.md).
%if KERN_SIZE > KERN_BUDGET
%error "kernel too big: it must fit KERN_BUDGET - see docs/KERNEL-MEMORY.md"
%endif
; 2. the kernel's own segment is 64KB like any other, and .text + .bss are
;    both addressed through it, so they have to fit one whether or not the
;    budget above is ever raised.
%if KTEXT_SIZE + KBSS_SIZE > 65536
%error "kernel image + bss overflows one 64KB segment"
%endif
; 3. task 0's stack grows DOWN from STK0_TOP onto the top of .lowbss, and
;    both live in LOW_SEG. STK0_SIZE is the whole of the gap between them,
;    so this proves the constant is actually a stack and not a rounding
;    error - and that a LOW_SEG offset still fits a 16-bit register.
%if STK0_SIZE < 512
%error "STK0_SIZE is too small to be a stack"
%endif
%if KLOW_SIZE + STK0_SIZE > 65536
%error "lowbss + task 0's stack overflows one 64KB segment"
%endif
; 3b. menu_bar is a LITERAL byte count (.bss may not forward-reference), so
;    nothing makes it follow MENU_BARMAX. It gained a cell the day the app
;    name became a pull-down (SPEC.md 12.2); this is what catches the next one.
%if MENU_BARMAX * MB_ENTSZ > 84
%error "menu_bar is too small for MENU_BARMAX cells - raise the resb in menu.inc"
%endif
; 4. the menu save-under (SPEC.md 2.2/12.4) must fit MENU_SAVE_KB. gfx_save
;    costs planes x rows x (byte span + 1); the two clamps in menu.inc bound
;    both factors, and this is where they are checked. The claim itself is
;    menu_save_kb, sized from the RECT ACTUALLY DROPPED - so this is now the
;    ceiling that arithmetic can never exceed rather than the figure claimed,
;    and what it really proves is that menu_save_kb's multiplies stay inside
;    16 bits. The save-under is the ONE kernel buffer outside the budget
;    above, deliberately: it exists only while a menu is down (SPEC.md 50).
%if 4 * (MENU_POPMAX*MENU_ITEM_H + 2) * (MENU_MAXW/8 + 2) > MENU_SAVE_KB*1024
%error "menu save-under can overflow its claim - lower MENU_POPMAX/MENU_MAXW"
%endif
; 6. every base an int 13h transfer can land on is 512-byte aligned, or a
;    single-sector read can still straddle a 64KB DMA boundary and fail with
;    error 09h (see KIMG_PARA above). A segment is 512-aligned when it is a
;    multiple of 32 paragraphs.
%if (KERNEL_SEG % 32) || (FAT_SEG % 32) || (LOW_SEG % 32)
%error "a disk-buffer segment is not 512-byte aligned - see KIMG_PARA"
%endif
%if HEAP_SEG % 32
%error "the heap base is not 512-byte aligned - see KIMG_PARA"
%endif
; 6b. ...and so is every claim in it, which is what a package region rides
;     on now that it is a claim rather than a pool slot: mem_claim rounds to
;     whole KB, so every base it hands out is HEAP_SEG + n*64 paragraphs.
;     MEM_PARA_KB is that 64; if it ever stopped being a multiple of 32 an
;     int 13h read of a package image could straddle a 64KB DMA boundary and
;     the symptom would be "Disk error" on the LARGE packages only.
%if MEM_PARA_KB % 32
%error "a heap claim is not 512-byte aligned - a package image is read into one"
%endif
; 5. the boot sector relocates itself to BOOT_RELOC before it reads a sector,
;    and its stack grows down from there. The kernel's landing zone must end
;    below that stack, or the sectors would overwrite the code that is
;    reading them. boot/boot.asm carries its own copy of both constants (it
;    is assembled separately); change one and change the other.
%if KERNEL_SEG*16 + KERN_SIZE > BOOT_LIN - BOOT_STACK
%error "the kernel would land on the relocated boot sector's stack"
%endif
