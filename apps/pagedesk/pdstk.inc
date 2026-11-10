; =============================================================================
; os8088 - apps/pagedesk/pdstk.inc    THE PRIMARY'S DOOR INTO THE MODULES, ON
;                                     THE PRIVATE MODULE STACK
;                                     (PAGEDESK-SPEC 3.1, 20.15; DECISIONS D23)
;
; %included by apps/pagedesk/pagedesk.asm after pdui.inc (whose pdu_vec holds
; the three far vectors). pdu_mcall is the primary's ONLY call into PDENG,
; PDREND and PDWRT, so it is the one place the stack is changed.
;
; WHY. Every module verb used to run on the UI task's 512-byte stack
; (kernel.asm's STK0_SIZE, in LOW_SEG), and a PDREND verb that calls PDENG is
; 388-470 + 158-236 bytes of module frames before the kernel's FAT and int 13h
; go under it. It ran into .lowbss below the stack - the menu bar's line
; buffer, XTGUIDE page 3. So a verb runs on a 2 KiB stack of its own, claim
; B's PDU_DC_DSTK row (pdumod.c), and every KERNEL call a module makes is
; swapped back up to the UI stack by the module's gate (pdkgate.inc), where
; the kernel's depth is what it always was.
;
; THE CONTRACT (pdabi.h's PD_DSTK_*):
;   * outermost call, a deep stack carved: bank the UI SS:SP, and with IF off
;     load SS = the row's segment (a selector on kern_mega) and SP =
;     PD_DSTK_MAG; write the RECORD at the top - PD_DSTK_MAGIC, the row's own
;     SS, the UI SS, the UI SP - so SP = PD_DSTK_UISP; IF back as it was; far
;     call the door. A module that far-calls another (PDREND -> PDENG) stays
;     on this stack: only this door swaps.
;   * on return: the answer is banked (AX, DX, CF -> pdu_mcf), then the stack
;     is CHECKED - SP must be back at PD_DSTK_UISP and the canary at offsets
;     0 and 2 intact - and the UI SS:SP restored the same way.
;   * an overrun is LOUD: pdu_stkbad = 1, the verb answers CF = 1, AX =
;     PDE_STACK, the wake is posted, and from then on every verb is refused
;     PDE_STACK except the three that let go (PDV_REND_ABORT, PDV_ENG_CLOSE,
;     PDV_WRT_ABORT) until the document's claims are given back -
;     pdu_stk_wake (pdumod.c) closes the document and says why. The evidence
;     (the 0xCC fill, the canary) is left as it was for a debugger.
;   * flat (no deep stack - no document is open - or RE-ENTERED from inside a
;     module's kernel call, a modal loop dispatching this package): the door
;     is called where it stands, as before 20.15. The gate sees SP >= 2 KiB
;     and calls the kernel directly.
;
; WHY THE KERNEL TOLERATES A FOREIGN SS FOR THE LENGTH OF A SLICE (20.15):
; sch_switch refuses to switch off a stack that is not LOW_SEG's
; (kernel/sched.inc, `cmp ax, LOW_SEG` / `jne sch_isr.tres`) so no task record
; ever holds this SP; sch_isr chains int 08h in place (`.chplain`); both mouse
; ISRs stand on their own stack whatever SS was (MOUPRIV_ENTER,
; kernel/mouse.inc). The price is that the UI task is not pre-empted while it
; is down here; disk work is not down here (the gate), so a slice is the
; window.
;
; cdecl like the rest of pdui.inc: BP, DS, SS:SP, DF preserved.
; =============================================================================

%include "pagedesk/pdabi.inc"

section .text

; -----------------------------------------------------------------------------
; unsigned pdu_mcall(int mod, unsigned verb, unsigned bx, unsigned cx,
;                    unsigned dx)
; One far call through module `mod`'s door. Answers AX; the carry lands in
; the C's pdu_mcf (1 = the verb failed, AX a PDE_*) and DX in pdu_mdx. With no
; module bound it answers PDE_NOTFOUND with pdu_mcf = 1, so "not loaded" is
; an ordinary refusal and never a jump through zero.
; -----------------------------------------------------------------------------
_pdu_mcall:
    ; STKBALANCE-OK: a STACK SWITCH (PAGEDESK-SPEC 20.15) - SS:SP move between the
    ; UI stack and the private module stack; each side is balanced on its own stack
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov bx, [bp+4]
    shl bx, 1
    shl bx, 1
    cmp word [pdu_vec + bx + 2], 0
    je .none
    lea si, [pdu_vec + bx]
    mov ax, [bp+6]
    cmp word [_pdu_stkbad], 0
    je .go
    cmp al, PDV_REND_ABORT          ; a broken stack: only letting go is
    je .go                          ; still done (the document is closing)
    cmp al, PDV_ENG_CLOSE
    je .go
    cmp al, PDV_WRT_ABORT
    je .go
    mov ax, PDE_STACK
    jmp .fail
.go:
    mov bx, [bp+8]
    mov cx, [bp+10]
    mov dx, [bp+12]
    mov di, [_pdu_dstkseg]
    or di, di
    jz .flat                        ; no document set: no deep stack
    cmp byte [pdu_sk_in], 0
    jne .flat                       ; re-entered from below a module call
    mov byte [pdu_sk_in], 1
    ; --- down: the RECORD, then the door --------------------------------------
    pushf
    pop bp                          ; BP = the caller's flags (IF)
    cli
    mov [pdu_sk_sp], sp
    mov [pdu_sk_ss], ss
    mov ss, di
    mov sp, PD_DSTK_MAG
    mov word [ss:PD_DSTK_MAG], PD_DSTK_MAGIC
    push di                         ; [PD_DSTK_SELF]
    push word [pdu_sk_ss]           ; [PD_DSTK_UISS]
    push word [pdu_sk_sp]           ; [PD_DSTK_UISP] - and SP is there now
    push bp
    popf
    call far [si]
    mov [_pdu_mdx], dx
    sbb dx, dx                      ; CF -> 0 / -1
    neg dx                          ; -> 0 / 1
    mov [_pdu_mcf], dx
    ; --- the check: balanced, and the canary whole ---------------------------
    cmp sp, PD_DSTK_UISP
    jne .over
    cmp word [ss:0], PD_DSTK_CAN0
    jne .over
    cmp word [ss:2], PD_DSTK_CAN1
    jne .over
.up:
    pushf
    pop bp
    cli
    mov ss, [pdu_sk_ss]
    mov sp, [pdu_sk_sp]
    push bp
    popf
    mov byte [pdu_sk_in], 0
    cmp word [_pdu_stkbad], 0
    je .out
    cmp word [_pdu_stktold], 0
    jne .out
    mov word [_pdu_stktold], 1      ; ONE wake, and pdu_stk_wake closes the
    push ax                         ; document from it (pdumod.c). pdu_kick is
    call _pdu_kick                  ; C: AX..DI and ES are its to lose, and
    pop ax                          ; .out takes SI DI ES back off the frame
    jmp short .out
.over:
    mov word [_pdu_stkbad], 1
    mov ax, PDE_STACK
    mov word [_pdu_mcf], 1
    jmp short .up
.flat:
    call far [si]
    mov [_pdu_mdx], dx
    sbb dx, dx
    neg dx
    mov [_pdu_mcf], dx
    jmp short .out
.none:
    mov ax, 53                      ; PDE_NOTFOUND
.fail:
    mov word [_pdu_mcf], 1
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

section .data
pdu_sk_ss:      dw 0                ; the UI SS:SP while a verb is down there
pdu_sk_sp:      dw 0
pdu_sk_in:      db 0                ; 1 = a verb is on the deep stack
                                    ; (re-entry runs flat)
align 2, db 0
section .text
