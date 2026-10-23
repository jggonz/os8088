; =============================================================================
; os8088 - apps/runcpm/runcpm.asm
;
; The assembly shim of RUNCPM, RunCPM 6.9 reimplemented in C (SPEC.md 74) -
; RunCPM is Marcelo Dantas / "Mockba the Borg"'s, MIT licensed. It is the
; top-level nasm source: the .c is never assembled on its own, because
; `nasm -f bin` has no notion of an external symbol, so the compiled C, the
; runtime, the 32-byte header and the hand-written includes are ONE assembly
; (SPEC.md 73.1).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm error naming that function, and a C function
; with no %define is code the kernel never calls.
; =============================================================================

%define CC_PKG_NAME 'RUNCPM'        ; <= 15 chars; the Disk window's label
                                    ; (SPEC.md 73.14, 71)

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *) -
                                    ; the terminal has no mouse; a click is
                                    ; one more callback that KICKS the slice
                                    ; driver, so a wake the full event ring
                                    ; refused (os88_wm_wake -1) cannot park
                                    ; a running machine until a key or a
                                    ; paint (SPEC.md 74)
%define CC_HAS_ABOUT                ; void os88_about(void *)
%define CC_HAS_ONWAKE               ; void os88_onwake(void *) - THE slice
                                    ; driver's entry (SPEC.md 74.1): the one
                                    ; callback dispatched WITHOUT the gfx lock
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *) - and
                                    ; it can never be called: the set this
                                    ; program registers is EMPTY and exists so
                                    ; that its name puts 'RunCPM' in the
                                    ; kernel bar (SPEC.md 12.2, 73.12). The
                                    ; %define has to be here anyway, because
                                    ; os88_menu_set() patches the trampoline in
%define CC_HAS_WORKER               ; void os88_worker(void *) - hired only

%define CC_STACK_CLASS OS88_STACK_256 | OS88_HIGH | OS88_HIGHMEM ; ...AND HOW MUCH STACK IT WANTS
                                    ; (SPEC.md 8.7). Declared rather
                                    ; than defaulted:
                                    ; the same shape as CWORD's: os88_worker() sleeps four ticks a pass
                                    ; and destroys the window on the close path. ~10 over the floor's 64
                                    ; is 74, and 128 gives 1.73x
                                    ; It is NOT the same number as
                                    ; crt0.asm's CC_STACK, which is
                                    ; the LARGEST class: this is what
                                    ; we ask for and that is the
                                    ; widest slice we could be given
                                    ; on main.c's exit (BIOS BOOT / EXIT.COM /
                                    ; HALT) to close the window: there is no
                                    ; self-close slot (SPEC.md 74, cword's
                                    ; File > Close idiom)
%define CC_HAS_OVL                  ; the module (SPEC.md 73.14, 71): the
                                    ; per-command half of the disk layer
                                    ; (ERA/REN/MAKE/open/close/the search/
                                    ; F_SIZE/USER/BDOS 249/_CheckSUB),
                                    ; _PatchCPM, the banner's tail, the About
                                    ; panel and the debug loader - resident
                                    ; image+bss passed the 55,000 trigger in
                                    ; wave 4 and the split is by FREQUENCY: a
                                    ; record, a byte punched, a keystroke
                                    ; stay in
%define CC_OVL_PART                 ; ...and it is THE PART of RUNCPM.O88,
                                    ; not a RUNCPM.OVL beside it (SPEC.md
                                    ; 73.14.2, 74.3). EAGER, because every
                                    ; session calls it on its first wake:
                                    ; rc_boot's _PatchCPM, then the CCP's own
                                    ; DIR or .COM load is an F_OPEN/F_SFIRST.
                                    ; Lazy would only move the read a quarter
                                    ; of a second later. The CCP rides in it
                                    ; (below), so it is the only part. The
                                    ; carve is PINNED since SPEC.md 73.14.2.1
                                    ; (114.6): crt0 declares no eager carve
                                    ; movable any more
%define CC_ICON "runcpm/icon.inc"   ; RunCPM.ico, 16x16 at 1bpp (SPEC.md 20.2,
                                    ; 74.5) - crt0 puts it at file offset 32
                                    ; and checks the length

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; entry and callback trampolines, and the
                                    ; whole API bridge

; THE CCP RIDES IN THE MODULE (SPEC.md 74.3). DRI's CCP-DR.60K - the
; emulated machine's command processor, 2,048 bytes copied to 0xE400 on every
; warm boot - is the PROGRAM'S data and not the user's: rccpm.c compiles the
; CCP_DR layout in (its address, the $$$.SUB FCB at +0x7AC, the F_CCPVERSION
; answer), as upstream's globals.h picks one CCP at compile time, so a
; different file under that name would have been a wrong machine rather than
; a choice. It was a file beside the package, read at launch into a 2KB claim
; of its own.
;
; It is the FIRST thing in .modc, so it is at offset 0 of the module's part
; (RC_CCP_OFF, rccpm.c) and os88_part_seg(0) is its segment - the C asks that
; at each use and banks nothing. As a PART OF ITS OWN it cost more: a part is
; sector-aligned, so a second row is a sector of the file on the 512-byte
; geometries (2,048 bytes is four sectors packed or plain, and LZ4 takes it
; only to 1,762), and a carve holding the package's own part beside the
; module stays pinned (SPEC.md 73.14.2.1), where the RUNCPM.OVL claim it
; replaces was movable. Inside the module it is packed in one LZ4 stream with
; the code, costs no row, and moves with it. It is never executed there: it is
; Z80 code, copied out, and the module's DS is still the package's.
section .modc
rc_ccpimg:
    incbin "runcpm-disk/CCP-DR.60K" ; found through -I build/ (the fetch,
                                    ; tools/getruncpm.py, pins its SHA-256)
rc_ccpend:
    times -(rc_ccpimg - $$) db 0    ; an assembly error unless it is at 0
    times (rc_ccpend - rc_ccpimg) - 2048 db 0   ; ...and exactly 2,048 bytes
    times 2048 - (rc_ccpend - rc_ccpimg) db 0   ; (RC_CCPSIZE): each line is
section .text                       ; negative, so an error, on the wrong side

%include "runcpm.gen.asm"           ; the compiled C, found through -I build/

; The hand-written half (SPEC.md 73.11's rule that the inner loop is assembly):
%include "runcpm/rcz80.inc"         ; the Z80 core - _rc_run(n) (wave 2)
%include "runcpm/rcmem.inc"         ; the Z80-RAM movers (wave 2)
%include "runcpm/rcband.inc"        ; the row composer and the row compare -
                                    ; ONE drawing call per changed row (74.2)

    CC_IMAGE_END                    ; cc_bss_end and cc_image_end
