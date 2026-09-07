; =============================================================================
; os8088 - apps/infones/nifsx.inc      SPEC.md 53's exclusive bracket
;
; Part of INFONES (SPEC.md 91.6). Derived from InfoNES fe3295c0 under
; Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
; InfoNES fe3295c0, restructured for 8086 real mode. THIS FILE carries no
; InfoNES material: InfoNES's front ends are Win32, SDL, GTK and a Zaurus
; framebuffer, and none of them is this.
;
; %included by apps/infones/infones.asm AFTER the compiled C.
;
; -----------------------------------------------------------------------------
; WHY THE BRACKET IS HERE AND NOT IN THE SDK
; -----------------------------------------------------------------------------
; OSAPI_FSX_* is DELIBERATELY not wrapped for C: apps/cc/os88.h says "the
; rules are the whole feature and none of them is checkable from C". So the
; whole of it is written in the package, which costs no bytes in any other C
; package and needs no crt0 change - and exactly ONE cdecl helper crosses back
; into the C, so that nimenu.c can grey the mode and clip items before entry.
; apps/tank/tank.asm:431-475 and apps/cyclone/cyclone.asm:8086-8190 are the
; shapes this copies.
;
; -----------------------------------------------------------------------------
; WAVE 1 SCOPE, AND WHY IT IS THE CAPS READ AND NOTHING ELSE
; -----------------------------------------------------------------------------
; The bracket runs a GAME, and there is no composer until wave 2 (niband.inc).
; What wave 1 needs of this file is the ONE thing the menu already asks:
; whether this window's display offers a fullscreen mode at all, which is what
; greys `Full screen` with a true fact rather than a guess (SPEC.md 47).
;
; Wave 2 adds, in this file and in this order: OSAPI_FSX_RUN with
; FSXF_FASTTICK and the near entry it names, OSAPI_FSX_MODE per adapter,
; OSAPI_FSX_SURF, OSAPI_FSX_WAIT on FSXW_FRAME as the FRAME BUDGET (never
; FSXW_VSYNC, which is the adapter's rate and not the NES's - SPEC.md 91.6.3),
; OSAPI_FSX_PAGE for Mode X's flip, the DAC programming (64 colours mirrored
; four times), the four present routines, and the int 16h typed-key poll
; beside the os88_key_down sweep.
;
; ONE RULE THAT IS ALREADY TRUE AND MUST STAY TRUE: **no ovl_* is called from
; inside the bracket, ever** (SPEC.md 91.6.5). It would be legal by 73.14's
; rules and by the SP gate - the bracket runs on the UI task - and it would
; read a floppy while the machine is in a foreign video mode, with any refusal
; toasted onto a bar the user cannot see.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], ...; the answer in AX; BP,
; DS, SS:SP and DF preserved.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; int ni_fsx_caps(void *win) - OSAPI_FSX_CAPS (slot 0x02C0) for the display
; THIS window is on.
;
; in:  [bp+4] = the window pointer os88_main was given
; out: AX = the FSXM bitmask (bit n = mode id n is settable on that display),
;      0 = this display offers no fullscreen mode at all
;
; IT IS CALLED AT USE AND ITS ANSWER IS NEVER BANKED. os88api.inc:2438-2449 is
; explicit: on a two-display machine this is a question about a DISPLAY and a
; window moves between them, so an answer banked in an entry proc describes
; the window you were LAUNCHED FROM, because yours does not exist yet
; (SPEC.md 39.18.2). The C calls it from ni_menu_state, which runs on every
; state change.
;
; DL comes back holding that display's VID_* kind and this helper drops it:
; SPEC.md 39.24's rule is to branch on BPP and not on the adapter's name, and
; the mask above already answers the only question the menu has.
; -----------------------------------------------------------------------------
_ni_fsx_caps:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [bp+4]
    call OSAPI_FSX_CAPS             ; AX = the settable ids, DL = the
                                    ; display's VID_* kind, which this helper
                                    ; drops with the pop below
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret
