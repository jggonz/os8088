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

%define CC_PKG_NAME 'RUNCPM'        ; <= 15 chars; the Disk window's label,
                                    ; and the stem of RUNCPM.OVL if the
                                    ; measured size line ever asks for one
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
                                    ; on main.c's exit (BIOS BOOT / EXIT.COM /
                                    ; HALT) to close the window: there is no
                                    ; self-close slot (SPEC.md 74, cword's
                                    ; File > Close idiom)
%define CC_HAS_OVL                  ; RUNCPM.OVL (SPEC.md 73.14, 71): the
                                    ; per-command half of the disk layer
                                    ; (ERA/REN/MAKE/open/close/the search/
                                    ; F_SIZE/USER/BDOS 249/_CheckSUB),
                                    ; _PatchCPM, the banner's tail and the
                                    ; debug loader - resident image+bss
                                    ; passed the 55,000 trigger in wave 4
                                    ; and the split is by FREQUENCY: a
                                    ; record, a byte punched, a keystroke
                                    ; stay in. First loaded by rc_boot on
                                    ; the first wake, before any folder
                                    ; move
%define CC_ICON "runcpm/icon.inc"   ; RunCPM.ico, 16x16 at 1bpp (SPEC.md 20.2,
                                    ; 74.5) - crt0 puts it at file offset 32
                                    ; and checks the length

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; entry and callback trampolines, and the
                                    ; whole API bridge

%include "runcpm.gen.asm"           ; the compiled C, found through -I build/

; The hand-written half (SPEC.md 73.11's rule that the inner loop is assembly):
%include "runcpm/rcz80.inc"         ; the Z80 core - _rc_run(n) (wave 2)
%include "runcpm/rcmem.inc"         ; the Z80-RAM movers (wave 2)
%include "runcpm/rcband.inc"        ; the row composer and the row compare -
                                    ; ONE drawing call per changed row (74.2)

    CC_IMAGE_END                    ; cc_bss_end and cc_image_end
