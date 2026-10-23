; =============================================================================
; os8088 - apps/weave/wfetch.inc   A LAZY PART, FETCHED FROM HOME (WEAVE-SPEC
;                                  1.2.2, 1.2.4)
;
; %included by apps/weave/weave.asm AND apps/loom/loom.asm - WEAVE-SPEC 1.2's
; sharing rule, one text and two copies of the object code. Each package
; carries its second segment as a lazy part of its own .O88 (WEAVE's canvas
; core, LOOM's preview module), and this is the one door both are fetched
; through.
;
; int w_partfetch(int part) - make part `part` resident. 0 = it is here
; (including "already was"), -1 = it could not be had and a toast has said why
; (op_fetch's own, or op_s_disk's). os88_part_seg(part) is its segment after.
;
; WHY NOT os88_part_fetch(), which is the same op_fetch: op_fetch reads the
; package's FILE by NAME, and a name resolves in the folder the instance
; STANDS in (SPEC.md 19.2.1). A bundle double-clicked in LOOM\ launches WEAVE
; out of WEAVE\ and leaves it standing beside the bundle; LOOM's File > Open
; Project walks it into the project's folder. Either way the fetch would look
; for WEAVE.O88 / LOOM.O88 where it is not. So it stands where the package
; was launched from for the read and comes back - crt0.asm's cc_ovneed
; bracket, said once more for a row that is not the overlay's, and reading the
; same two words (cc_ovhome, cc_ovhvol), which CC_OVL_LAZY's cc_ovinit banked
; at launch. QM on both legs: nothing lists either folder.
;
; UI TASK ONLY, op_fetch's rule (SPEC.md 20.6 rule 7). Both callers are on it:
; the canvas load path is an overlay tenant and the overlay refuses a worker
; at its door, and LOOM's lm_pv_need runs from a menu command.
;
; cdecl (SPEC.md 73.3); BP, SI, DI, DS, ES preserved.
; =============================================================================

%ifndef CC_OVL_LAZY
  %error "wfetch.inc reads cc_ovhome, which only CC_OVL_LAZY banks"
%endif

section .text
_w_partfetch:
    push bp
    mov bp, sp
    push si
    push di
    push es
    call OSAPI_FILE_HERE            ; DX = where we stand, BL = its drive:
    push dx                         ; banked for the way back
    push bx
    xor cx, cx                      ; CX = 0: we did not move
    cmp dx, [cc_ovhome]
    jne .go
    cmp bl, [cc_ovhvol]
    je .home
.go:
    mov dx, [cc_ovhome]
    mov bl, [cc_ovhvol]
    call OSAPI_FILE_GOTO_QM         ; clobbers AX, and CX/SI/DI on a
    jc .nohome                      ; redirected volume (SPEC.md 62.9.1)
    mov cx, 1                       ; ...so the flag is set AFTER it
.home:
    push cx
    mov al, [bp+4]
    call op_fetch                   ; claims, reads, expands; toasts a refusal
    pop cx
    pop bx
    pop dx
    pushf                           ; op_fetch's answer, across the way back
    jcxz .stay
    call OSAPI_FILE_GOTO_QM         ; CF unread: a refused return moved nothing
.stay:
    popf
    sbb ax, ax                      ; CF -> 0 / -1
    jmp short .out
.nohome:
    pop bx                          ; the program's disk is not where it was:
    pop dx                          ; GOTO_QM moved nothing, so there is no
    mov si, op_s_disk               ; way back to take - only the reason
    call op_say
    mov ax, -1
.out:
    pop es
    pop di
    pop si
    pop bp
    ret
