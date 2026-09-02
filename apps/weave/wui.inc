; =============================================================================
; os8088 - apps/weave/wui.inc     the shared alert, the arm word, and the
;                                 bytecode core's one way back into C
;
; Part of WEAVE, the .WAB runtime (docs/WEAVE-SPEC.md). %included by
; apps/weave/weave.asm AFTER os88ui.inc (it calls into that file) and beside
; apps/weave/wdraw.inc, whose header carries the calling convention all of
; these obey.
;
; WHY IT IS SEPARATE FROM wdraw.inc. That file DRAWS: every routine in it
; takes screen pixels and paints. Nothing here draws. These are the four
; places where the C has to reach something that is a near proc rather than a
; value - the alert's completion, the arm/fire word, and the bridge the
; bytecode core leaves through - and mixing them into the paint core would put
; a `call _w_native` inside the file LOOM's Preview includes to draw with
; (WEAVE-SPEC 1.2's seam), which is exactly the line that seam draws.
;
; All cdecl (SPEC.md 73.3), all near, and none of them writes a `retf`: a
; package author never does (SPEC.md 20 - the boundary is solved once, in
; crt0.asm's dispatcher, and every callback below is an ordinary near proc).
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; wvm_native - the ONE symbol apps/weave/wvm.inc names outside itself.
;
; Its header states the contract in full. In one line: the request is already
; in the native block (which is the C's own array, so nothing is staged), DS
; is ours, ES is whatever the slice's caller had, and the answer is
; 0 done / 1 script error / 2 collect and re-execute.
;
; The compiled C is what answers, so this is a trampoline and nothing else -
; the crt0.asm callback shape, minus the arguments, because the block IS the
; arguments. AX is not pushed: it is the answer.
; -----------------------------------------------------------------------------
wvm_native:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    call _w_native                  ; C: int w_native(void)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; int wd_ask(const char *msg, void *win, int set) - raise WEAVE-SPEC 8.2's
; alert through the shared engine (SPEC.md 75.3, os88ui.inc's OS88UI_ALERT).
; out: 0 it is up, -1 refused - one is already up and has been RAISED, which
;      is what that engine has instead of modality, or the window table is
;      full. Either way alert() returns and the app carries on (8.2).
;
; The message is a near pointer in OUR segment and is not copied: os88ui.inc
; holds the pointer and letters from it when the alert paints, so the buffer
; has to outlive the dialog. wevent.c's w_amsg[] is a static for exactly that
; reason, and it is the reason this takes a pointer rather than staging.
;
; WEAVE IS THE FIRST C PACKAGE IN THIS TREE TO USE THE SHARED ALERT. Nothing
; about it needed changing for one - the completion below is an ordinary near
; proc and the kernel calls the alert window's own callbacks with DS = our
; segment, the way it calls every package callback (SPEC.md 20.1) - but it had
; never been tried, so the fact is written down here rather than assumed.
; -----------------------------------------------------------------------------
_wd_ask:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [bp+4]                  ; the message
    mov bx, [bp+6]                  ; our window
    mov ax, [bp+8]                  ; the button set
    mov di, wu_done
    call os88ui_ask
    mov ax, 0                       ; MOV: CF is the answer
    jnc .ok
    dec ax
.ok:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; wu_done - the alert's completion (os88ui.inc's contract).
; in:  AL = the 0-based button, or OS88UI_ACANCEL for a DISMISSED alert - its
;      close box, its minimize box or Esc. SI = our window. The gfx lock is
;      HELD and the alert is already destroyed.
;
; HANDLE THE CANCEL: it is the only answer that arrives without the user
; having chosen anything, and WEAVE-SPEC 3.4's onalert record carries
; `1 OK/Yes, 0 Cancel/No` - so a dismissal is a 0 and not an absent event.
; The app asked to be told; it is told.
; -----------------------------------------------------------------------------
wu_done:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    xor ah, ah
    cmp al, OS88UI_ACANCEL
    jne .have
    mov ax, -1
.have:
    push ax                         ; arg 1: int button (-1 = dismissed)
    call _w_alertdone               ; C: void w_alertdone(int)
    add sp, 2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; The arm word (SPEC.md 13.7's press/release pair, os88ui.inc's OS88UI_ARM).
; Three lines each, and they exist only because a C file may not name an nasm
; label. What is armed is wd_hit's own answer - the rect index PLUS ONE - so
; 0 means nothing, which is what os88ui_fire answers when a release arrives
; with no press behind it.
; -----------------------------------------------------------------------------
_wd_arm:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    call os88ui_arm
    pop bp
    ret

_wd_fire:
    call os88ui_fire
    ret

_wd_armed:
    call os88ui_armed
    ret

; =============================================================================
; THE ONE-LINE FIELD (apps/os88line.inc, WEAVE-SPEC 6.7)
;
; Six wrappers and no logic. That file declares NO STORAGE - every routine
; takes SI = a block the caller owns, which is what lets one window have two
; fields - so WEAVE's blocks are ordinary C arrays and these are the way in.
;
; SI AND NOT BX is os88line.inc's own choice, and its header says why: the gfx
; primitives take AX/BX/CX/DX as their rect, and a block in BX would mean a
; shuffle at every call. From C the block arrives at [bp+4] either way.
; =============================================================================

; void wd_ldraw(int *blk) - the field, whole: frame, contents and caret.
_wd_ldraw:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    call os88line_draw
    pop si
    pop bp
    ret

; int wd_lkey(int *blk, int ascii, int scan) - 0 the field used it, -1 it is
; the caller's (Enter, Tab, Escape and everything else it does not edit with).
_wd_lkey:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov al, [bp+6]
    mov ah, [bp+8]
    call os88line_key
    mov ax, 0                       ; MOV: CF is the answer
    jnc .ok
    dec ax
.ok:
    pop si
    pop bp
    ret

; int wd_lclick(int *blk, int x, int y) - 0 it was inside and FOCUS IS SET
; with the caret placed at the column clicked, -1 outside and untouched.
_wd_lclick:
    push bp
    mov bp, sp
    push cx
    push dx
    push si
    mov si, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call os88line_click
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop si
    pop dx
    pop cx
    pop bp
    ret

; void wd_lset(int *blk, const char *s) - replace the contents; the caret
; lands at the end. TRUNCATES at the block's capacity rather than refusing,
; which is that routine's own documented behaviour.
_wd_lset:
    push bp
    mov bp, sp
    push di
    push si
    mov si, [bp+4]
    mov di, [bp+6]
    call os88line_set
    pop si
    pop di
    pop bp
    ret

; void wd_lcaron(int *blk) / wd_lcaroff(int *blk) - the 1px bar, on and off,
; at the block's own caret index. os88line_caroff puts back the character it
; was covering in ONE opaque cell, so the glyph never blinks on the way.
_wd_lcaron:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov ax, [si+LN_CAR]
    call os88line_caron
    pop si
    pop bp
    ret

_wd_lcaroff:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov ax, [si+LN_CAR]
    call os88line_caroff
    pop si
    pop bp
    ret
