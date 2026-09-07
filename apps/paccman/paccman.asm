; =============================================================================
; os8088 - apps/paccman/paccman.asm
;
; The assembly shim of PACCMAN, pacman.c written in the C this toolchain
; compiles (SPEC.md 91). It is the top-level nasm source: the .c files are
; never assembled on their own, because `nasm -f bin` has no notion of an
; external symbol, so the compiled C, the runtime, the band composer and the
; 32-byte header are ONE assembly (SPEC.md 73.1).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm external-reference error naming that function,
; and a C function with no %define is code the kernel never calls.
;
; DERIVED MATERIAL. PACCMAN is a reimplementation of Andre Weissflog's
; pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
; Weissflog, at commit 0f5ec5a: its screens, tables, timings and rules are
; carried, its code is not compiled. The tile, sprite and colour tables it
; embeds are Pac-Man arcade ROM data (Namco) and the two sound register dumps
; were captured from an arcade emulator; apps/paccman/LICENSE is the
; reference's licence, incbin'd below, and apps/paccman/README.md carries the
; full provenance.
;
; IT IS NOT PACMAN. apps/pacman (SPEC.md 89) is a different program - the
; Roklan Atari disk version in hand assembly - and nothing here may reach a
; `pacman` name, image, target or vm directory (SPEC.md 73.12's rule).
; =============================================================================

; --- os88ui.inc's FEATURE DEFINES, AND THEY BELONG AT THE TOP ----------------
; Preprocessor tests are answered in FILE ORDER and os88ui.inc's own include is
; at the END of this file, so a %define placed beside that include is BELOW
; every %ifdef that reads it: the guarded bodies assemble anyway while the
; file's own guarded blocks silently vanish (apps/weave/weave.asm says this at
; length). One feature is wanted here and it is declared here.
%define OS88UI_NOBTN                ; ...and NOT the button: nothing in this
                                    ; program has one. The About card is the
                                    ; only control it draws
%define OS88UI_ABOUT                ; the standard About card (SPEC.md
                                    ; 20.5.1.1), reached from C through
                                    ; os88_about_card()

%define CC_PKG_NAME 'PACCMAN'       ; <= 15 chars (the field is 16, NUL-padded)
                                    ; - the Disk window's label and the
                                    ; instance table's name. The kernel bar
                                    ; reads 'PaccMan' instead, off the menu
                                    ; set's AM_NAME (SPEC.md 12.2), which is
                                    ; the same literal the window is titled
                                    ; with

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *)
%define CC_HAS_ABOUT                ; void os88_about(void *)
%define CC_HAS_WORKER               ; void os88_worker(void *) - the game loop,
                                    ; hired by os88_paint (the gfx lock has to
                                    ; be held, so os88_main cannot do it)
%define CC_STACK_CLASS OS88_STACK_256 ; the worker asks for 256: a 384
                                    ; request can be REFUSED when both 384
                                    ; slices are held (docs/PACCMAN-PORT-PLAN.md)
                                    ; and NOT: CC_HAS_ONCLICK,
                                    ; CC_HAS_ONMOUSEUP, CC_HAS_ONRESIZE,
                                    ; CC_HAS_FDLG, CC_HAS_ONWAKE - an arcade
                                    ; cabinet has a joystick and no mouse, the
                                    ; window is a fixed size (WF_KEEPH below),
                                    ; and there is no file this program reads
                                    ; or writes: pacman.c has no file I/O of
                                    ; any kind and neither does this, so the
                                    ; hiscore lives for the instance

%define CC_ICON "paccman/icon.inc"  ; the 16x16 tile at file offset 32

%include "cc/crt0.asm"              ; sections, the 32-byte header, the icon,
                                    ; the entry and callback trampolines, and
                                    ; the whole API bridge (it includes
                                    ; os88thunk.asm)

%include "paccman.gen.asm"          ; the compiled C: smlrcc -S then
                                    ; tools/cc8086.py, found through -I build/

; --- the shared control library, and then the band composer ------------------
; os88ui.inc AFTER crt0.asm: the "at the END of your code" rule in its own
; header protects the icon's fixed offsets, and crt0.asm emits the header and
; the icon itself with the assertions inside it, so that hazard cannot reach a
; C package (apps/weave/weave.asm has the long form of this).
%include "os88ui.inc"

; The band composer (SPEC.md 73.11's rule that the inner loop is assembly).
; AFTER the compiled C, because the C names its four entry points and nasm
; resolves a forward reference to a label. It leaves DS, ES and DF as it found
; them - it never loads ES at all - and switches back to .text before it ends.
%include "paccman/pmcband.inc"

; The reference's licence, in the image, the way apps/pacman carries its own.
; It is DATA and it is read by nothing: it is here so that a copy of
; PACCMAN.O88 that has been separated from apps/paccman/ still carries the
; terms the tables inside it are shipped under.
section .data
pmc_license:
    incbin "apps/paccman/LICENSE"

section .text
    CC_IMAGE_END                    ; cc_bss_end and cc_image_end - the two
                                    ; forward references the header made
