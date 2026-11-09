; =============================================================================
; os8088 - apps/c64/c64.asm
;
; The assembly shim of C64, a Commodore 64 written in C - a reimplementation
; of VICE 3.10's `x64` (docs/C64-SPEC.md). It is the top-level nasm source:
; the .c is never assembled on its own, because `nasm -f bin` has no notion of
; an external symbol, so the compiled C, the runtime, the 32-byte header and
; the hand-written includes are ONE assembly (SPEC.md 73.1).
;
; Derived from VICE (https://vice-emu.sourceforge.io/), Copyright (C)
; 1996-2025 the VICE team, GPL-2-or-later. apps/c64/ is GPL-2-or-later; the
; rest of this tree is not. See apps/c64/COPYING for the licence text.
; Nothing of VICE's SOURCE is vendored - what is carried is behaviour, tables
; and strings, each naming the VICE file it came from (C64-SPEC §2).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm error naming that function, and a C function
; with no %define is code the kernel never calls.
; =============================================================================

%define CC_PKG_NAME 'C64'           ; <= 15 chars; the Disk window's label
%define CC_STACK_CLASS OS88_HIGH | OS88_HIGHMEM    ; kern_mega may load it above 1MB (KERN-MEGA-PLAN
                                    ; item 87); the class itself unsaid

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *) - the
                                    ; level keyboard's make half (C64-SPEC 7.2)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *) - a
                                    ; click KICKS the slice driver, so a wake
                                    ; the event ring refused cannot park a
                                    ; running machine (SPEC.md 74.1)
%define CC_HAS_ABOUT                ; void os88_about(void *) - the kernel's
                                    ; name pull-down opens the same panel as
                                    ; Help > About VICE... (C64-SPEC 12)
%define CC_HAS_ONWAKE               ; void os88_onwake(void *) - THE slice
                                    ; driver's entry (SPEC.md 74.1): the one
                                    ; callback dispatched WITHOUT the gfx lock
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *) - the
                                    ; five menus of C64-SPEC 11.1, AM_NAME
                                    ; 'VICE'
%define CC_HAS_FDLG                 ; void os88_onfile(...) - File > Smart
                                    ; attach... on .PRG (C64-SPEC 11.3)
                                    ; NO CC_HAS_WORKER. File > Exit emulator
                                    ; used to hire one - wm_destroy under the
                                    ; lock, then task_alive - and that closes
                                    ; the WINDOW without closing the APP: the
                                    ; dock tile stayed, dead, and answered no
                                    ; click. OSAPI_WM_CLOSE (SPEC.md 75.2) is
                                    ; the kernel's own close path and needs no
                                    ; task at all (c64.c's os88_onwake)
%define CC_HAS_OVL                  ; THE OVERLAY (SPEC.md 73.14) - ON FROM THE
                                    ; FIRST COMMIT (C64-SPEC 13.1). The
                                    ; alternative is discovering at 55,000
                                    ; that the code is not the kind that can
                                    ; move (LESSONS.md 5). The split is by
                                    ; FREQUENCY: the menu command bodies
                                    ; (c64cmd.c), the Smart-attach body
                                    ; (c64load.c) and the About panel
                                    ; (c64about.c) are out; every callback,
                                    ; the core, the composer and the flush
                                    ; are in
%define CC_OVL_PART                 ; ...AND IT IS PART 1 OF C64.O88, EAGER
                                    ; (C64-SPEC 13.3, SPEC.md 73.14.2). It was
                                    ; C64.OVL beside the package, and the
                                    ; program loaded it at its FIRST WAKE in
                                    ; every session - ovl_conv_init builds
                                    ; Copy's and Paste's tables - so a lazy
                                    ; part would only have been a second read
                                    ; of this file a tick after the first.
                                    ; Eager, it rides op_load's one carve read
                                    ; behind the ROM, and the carve code was
                                    ; already here for the ROM. Measured
                                    ; against the lazy arm: 768 image bytes
                                    ; and 1,023 file bytes smaller, and one
                                    ; cluster fewer on every c64disk geometry
%define CC_HAS_FSX                  ; THE EXCLUSIVE BRACKET (SPEC.md 53): on a
                                    ; display with FSXM_VGA13, Preferences >
                                    ; Fullscreen is the C64 in COLOUR, its
                                    ; 320x200 window one byte a pixel
                                    ; (C64-SPEC 9.12) - the four os88_fsx_*
                                    ; thunks, APPLE2's idiom
%define CC_ICON "c64/icon.inc"      ; the 16x16 breadbin, drawn for this port

%define CC_HAS_PARTS                ; THE ROM IS IN THE PACKAGE (C64-SPEC 1.4,
                                    ; SPEC.md 20.12). It was C64.ROM, a
                                    ; 20,480-byte sidecar that a file copy
                                    ; could separate from the program it is
                                    ; useless without - and the port carried a
                                    ; whole halted-machine state, a permanent
                                    ; status row and three greyed menu items
                                    ; to say so when it went missing. A part
                                    ; cannot go missing
%define CC_HAS_ONMOVE               ; void os88_onmove(unsigned, unsigned) -
                                    ; the 64KB RAM and Paste's 2KB are MOVABLE
                                    ; (C64-SPEC 3.1.1), and so is...
%define CC_ONMOVE_PARTS             ; ...the parts carve, ROM and overlay
                                    ; together (SPEC.md 73.14.2.1): os88_onmove
                                    ; re-reads c64_m.romseg through
                                    ; os88_part_seg, so crt0 declares it

%define CC_PARTS_AFTER_OVL 5        ; the fast core's rows follow the overlay
%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; entry and callback trampolines, and the
                                    ; whole API bridge

; --- the parts table (SPEC.md 20.12.3) ---------------------------------------
; TWO parts, both eager, both in the one carve op_load reads at launch.
;
; PART 0: the KERNAL, BASIC and character generator, in C64-SPEC 1.4's fixed
; layout, exactly as the sidecar carried them. It is an ASSET rather than a
; SEGMENT because nothing in it is far-called - the 6510 core reaches it
; through segment arithmetic off its base (c64cpu.inc 4.3), which is what an
; ASSET is for - and it is REQUIRED, so a machine that cannot spare its 20KB
; refuses the launch instead of starting a C64 with no ROM in it.
; ...AND IT IS COMPRESSED (SPEC.md 20.12.7). 20,480 bytes of 6502 code and a
; character generator go to 17,361 under LZ4 - 84.8%, which is machine code
; again - for ~217 ms of decode at launch on a 4.77 MHz 8088 against the ~6
; clusters of floppy it gives back. `len` stays 20,480, so a machine that
; cannot spare the ROM still refuses before a sector is read.
;
; PART 1: the overlay (CC_OVL_ROW - an OP_SEG|OP_COMP row, LAST because
; CC_PACKAGE appends the cut module as the last --part). LZ4 takes it 2,018
; -> ~1,530. THE CARVE STAYS PINNED, as it always was: c64_m.romseg banks
; os88_part_seg(0), and crt0 never declares an eager carve movable (SPEC.md
; 73.14.2.1). It is claimed top-down
; (SPEC.md 20.12.10.8), so the ~2KB of module that used to sit in a movable
; claim of its own now sits beside the ROM at the ceiling, not mid-arena.
    CC_PARTS_BEGIN 7
      OS88_PART OP_ASSET, OP_COMP
      CC_OVL_ROW
      OS88_PART OP_SEG, OP_LAZY | OP_COMP   ; 2 THE FAST 6510 CORE (C64-SPEC
                                    ; 4.7): apps/m6502/m65gen.py's, fetched
                                    ; in os88_main and far-called; without it
                                    ; the accurate core runs the machine alone.
                                    ; AFTER the overlay because that one is
                                    ; eager, and every lazy row follows every
                                    ; eager one (the Makefile's sixth argument)
      OS88_PART OP_SEG, OP_LAZY | OP_COMP   ; 3 BASIC TRANSLATED AHEAD OF TIME
      OS88_PART OP_SEG, OP_LAZY | OP_COMP   ; 4 ...the KERNAL (C64-SPEC 4.8:
                                    ; apps/m6502/m65aot.py's, a segment each)
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP ; 5 ...and their PC table
      OS88_PART OP_SEG, OP_LAZY | OP_COMP   ; 6 THE RECOMPILER (C64-SPEC 4.9):
                                    ; apps/c64/c64jit.asm and m65tmpl.py's
                                    ; templates, copied into a claim of its own
    CC_PARTS_END

%include "c64.gen.asm"              ; the compiled C, found through -I build/

section .text                       ; THE HOT HALF KEEPS ITS PARITY: the
    times (($ - $$) + 1) & 1 db 0     ; core and the composers below stay at
                                    ; the byte parity they were measured at
                                    ; whatever the compiled C above them
                                    ; grows by - an odd shift re-prices every
                                    ; word fetch on a 16-bit bus (an 8086, a
                                    ; V30, a 286), where an 8088 does not care
                                    ; (PERFORMANCE.md Part 7, os88rdiff.py)
; The hand-written half (SPEC.md 73.11's rule that the inner loop is assembly):
%define C64_FAST                    ; the accurate core's hand-back hooks
%include "c64/c64cpu.inc"           ; the 6510 core - _c64_run(cycles). Wave 1
%include "c64/c64fast.inc"          ; ...and the fast core's door (C64-SPEC 4.7)
                                    ; is the register file, the 256-entry
                                    ; dispatch table and the entry/exit shell
%include "c64/c64mem.inc"           ; the RAM/ROM claim accessors and movers,
                                    ; and the core's scratch (C64-SPEC 3.5/3.6)
%include "c64/c64band.inc"          ; the 1bpp composers, the span compare and
                                    ; the row signature (C64-SPEC 9.5)
%include "c64/c64sid.inc"           ; SID voice 3's oscillator (11.4.1)
%include "c64/c64fsx.inc"           ; ...and the colour screen's 8bpp ones,
                                    ; inside the FSXM_VGA13 bracket (C64-SPEC 9.12)

; -----------------------------------------------------------------------------
; unsigned c64_read_at(const char *name, unsigned seg, unsigned cap,
;                      unsigned off_lo, unsigned off_hi)
; os88_file_read_at() into a FAR buffer at seg:0 - drive 8's image is a claim
; of 171KB and read into it 32KB at a time (C64-SPEC 11.5). The same contract:
; the offset and the capacity a whole number of clusters, the answer the bytes
; delivered, 0 at the end or refused.
; -----------------------------------------------------------------------------
_c64_read_at:
    push bp
    mov bp, sp
    push si
    push es
    mov si, [bp+4]
    mov es, [bp+6]
    xor bx, bx
    mov cx, [bp+8]
    mov ax, [bp+10]
    mov dx, [bp+12]
    call OSAPI_FILE_READ_AT
    jnc .ok
    xor ax, ax
.ok:
    pop es
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; THE FM CHIP (C64-SPEC 11.4.3): SOUND.DRV's two doors, MIDIRack's order
; (apps/midirack/mrfm.inc) - identify the driver, ask SND_FM_INFO whether it
; has a chip, and CLAIM it, OPL2 mode. On the UI task only: the claim is
; stamped with the caller's instance (SPEC.md 34.12), and the driver gives
; it back when that instance dies.
; -----------------------------------------------------------------------------
%include "drivers/sound/sndpkg.inc"

; int c64_opl_open(void) - 1 when the chip is ours, 0 when there is none
_c64_opl_open:
    push si
    push di
    push es
    mov bh, DRVC_SOUND
    mov bl, SNDV_IDENT
    call OSAPI_DRV_CALL
    jc .no
    cmp ax, SND_SIG
    jne .no
    mov al, SND_FM_INFO
    call OSAPI_SND_FM
    jc .no
    mov al, SND_FM_CLAIM
    xor cl, cl                      ; OPL2 mode: channels 0-7
    call OSAPI_SND_FM
    jc .no
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop es
    pop di
    pop si
    ret

; int c64_opl_send(const unsigned char *pairs, int n) - N register/value
; pairs to the chip in one SNDV_OPLW call; the pairs written, -1 refused
_c64_opl_send:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    pop es                          ; the batch is ours: ES:SI
    mov si, [bp+4]
    mov cx, [bp+6]
    mov bh, DRVC_SOUND
    mov bl, SNDV_OPLW
    call OSAPI_DRV_CALL
    jnc .out
    mov ax, -1
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

    CC_IMAGE_END                    ; cc_bss_end and cc_image_end
