; =============================================================================
; os8088 - apps/os88alt.inc
;
; ALT+ENTER, THE FULL-SCREEN KEY, for a package that has a full screen to go
; to (SPEC.md 11.2.1.1). It is opt-in and costs a package that does not
; include it exactly nothing.
;
; WHY THERE IS A FILE FOR FOUR INSTRUCTIONS. The key arrives by two different
; routes depending on where the app is, and the second one is a poll with an
; edge in it that every fsx app would otherwise write out by hand:
;
;   WINDOWED   the kernel hands your W_ONKEY `AX = KEY_ALTENTER` (SPEC.md
;              9.7.1). No XT BIOS enqueues this combination at all - measured,
;              the BIOS buffer's tail does not move - so `kbd_track` latches
;              the scancode and `ui_task` spends it like any typed key. One
;              `cmp ax, KEY_ALTENTER` beside your other keys is the whole of
;              this half.
;
;   IN AN FSX BRACKET   there is no dispatch at all (SPEC.md 53.1), so no
;              synthesised event can reach you and your own `int 16h` poll
;              cannot see the key either. `os88alt_edge` asks the key-state
;              map instead, which is free in a loop that is already polling.
;
; THREE THINGS IT DOES FOR YOU, each of which was a defect first:
;
;   THE MAP MUST BE ARMED. `kbm_isr` does not track a scancode until
;   something has called `OSAPI_KEY_DOWN` (SPEC.md 9.7), and until it does
;   NEITHER half above works - not the poll and not the kernel's latch. That
;   is deliberate scoping rather than an oversight, so `OS88_ALTENTER_ARM` is
;   what asks, once, in your entry proc. An app that forgets gets a hotkey
;   that is silently dead.
;
;   A LEVEL READ IS NOT AN EDGE. `OSAPI_KEY_DOWN` answers "is this key down",
;   and a hold looks identical on every pass - so a bracket polling it raw
;   would leave full screen, re-enter, and leave again for as long as a
;   finger stayed on the key. `[os88alt_dn]` is what the last pass saw.
;
;   THE PRESS THAT GOT YOU IN IS STILL HELD. A user holds Alt+Enter a good
;   deal longer than one frame, so a bracket that starts polling from "up"
;   reads that same hold as the press that takes it out again and the full
;   screen flashes past. `OS88_ALTENTER_SEED` at the top of the bracket says
;   "assume down", and the first poll clears it when they let go. Entered
;   from a MENU it costs nothing - the first poll finds the keys up.
;
; --- IN A PACKAGE ------------------------------------------------------------
;   %include "os88alt.inc"          ; at the end of your code, os88ui's place
;
;   ...in your entry proc:          OS88_ALTENTER_ARM
;   ...in your W_ONKEY:             cmp ax, KEY_ALTENTER
;                                   je .togglefullscreen
;   ...at the top of your bracket:  OS88_ALTENTER_SEED
;   ...in your bracket's loop:      call os88alt_edge
;                                   jc .leavefullscreen
;
; An app on SPEC.md 11.2's LATCH rather than SPEC.md 53's bracket needs only
; the first two: its window keeps taking W_ONKEY while it is full screen, so
; the one `cmp` is both directions.
; =============================================================================

section .text

; --- os88alt_edge - a FRESH Alt+Enter press, off the key-state map ----------
; in:  nothing; out: CF=1 = a fresh press, CF=0 = the same hold, or nothing
; clobbers: nothing but the flags
;
; Two far calls a pass, and the second only when the first says Alt is down at
; all - which is almost never, so the standing cost of asking is one far call
; in a loop that was already spending one on `int 16h`.
os88alt_edge:
    push ax
    mov al, KSC_ALT
    call OSAPI_KEY_DOWN
    jnc .up
    mov al, KSC_ENTER
    call OSAPI_KEY_DOWN
    jnc .up
    cmp byte [os88alt_dn], 0        ; both down - a PRESS only if the last pass
    jne .same                       ; did not already have them
    mov byte [os88alt_dn], 1
    pop ax
    stc
    ret
.up:
    mov byte [os88alt_dn], 0        ; released: the next both-down is a press
.same:
    pop ax
    clc
    ret

; --- the edge's one byte, and it is IN `.text` ON PURPOSE --------------------
; **A PACKAGE-SIDE INCLUDE MAY NOT DECLARE `section .bss`.** Every package in
; this tree hand-chains its bss as `equ os88_image_end + N` offsets, and a
; `-f bin` `.bss` lands at os88_image_end too - so a `resb` here is not a new
; byte, it is an ALIAS of whatever the package put first in its own chain, and
; the seed's store of 1 goes through it.
;
; That is not a hypothetical. This file shipped with a `resb 1` for one commit
; and it cost two apps: Telnet's full-screen terminal came up with `l\x01ne 00`
; in row 0 - the seed, landing in the console buffer - and Paint refused to
; enter full screen a SECOND time, because the byte it aliased there behaves
; exactly like `[pt_fs]` left set. Both were diagnosed as the chord's own
; defect first, and Paint's poll was removed over it.
;
; apps/os88ui.inc has the rule in the other direction and had it right: its
; `section .bss` is inside `%ifdef OS88UI_KERNEL`, and its comment says in as
; many words that a package declares its own. A `db` in `.text` is what the
; kernel does for the same kind of byte (`kbd_ae`, `kbm_p5`, `kbd_armed`), it
; is initialised rather than trusting a zeroed section, and a package image is
; ordinary writable RAM - so it costs one byte of image and cannot collide
; with anything.
os88alt_dn: db 0                    ; what the last os88alt_edge pass saw
