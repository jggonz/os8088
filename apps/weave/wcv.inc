; =============================================================================
; os8088 - apps/weave/wcv.inc        THE SEAM TO WEAVE.WSM (WEAVE-SPEC 1.2.2)
;
; %included by apps/weave/weave.asm - so this is the PACKAGE, and everything
; it reaches is in a segment of its own. Three routines and one dword, and
; that is deliberately the whole of the resident cost: the module is entered
; by a far call through the dword, and every counter, every sprite field and
; every flag the C side wants is read and written with wblob.inc's
; w_b/w_w/w_pb/w_pw against the module's own state block, which needs no verb
; and no byte here at all.
;
; cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; BP, DS, ES, SS:SP
; and DF preserved. The module preserves the same set, plus SI and DI.
; =============================================================================

; THE MODULE'S SIZE, injected rather than assembled: WEAVE.WSM is a separate
; nasm job, so this assembly cannot see its last label. tools/os88disk.py is
; not involved either - the Makefile writes build/wsmsize.inc from the module
; it has just built, and the package is built after it. That ordering is what
; makes the third stamp word (1.2.2) a real staleness check rather than a
; tautology, and it is also why no C-toolchain flag is involved: this is one
; %include, resolved through the -I build/ every C package already has.
%include "wsmsize.inc"

section .text

; -----------------------------------------------------------------------------
; int wcv_kb(void) - the KB the module's claim needs, ceil(size / 1024).
; -----------------------------------------------------------------------------
_wcv_kb:
    mov ax, WSM_SIZE + 1023
    mov cl, 10
    shr ax, cl
    ret

; -----------------------------------------------------------------------------
; unsigned wcv_bytes(void) - WSM_SIZE, so the caller can check the read.
; -----------------------------------------------------------------------------
_wcv_bytes:
    mov ax, WSM_SIZE
    ret

; -----------------------------------------------------------------------------
; int wcv_stamp(unsigned seg) - is this really OUR module?
; out: 0 yes, 1 not a module or truncated, 2 stale.
;
; THREE WORDS, and 1.2.2 says what each catches and what the set does not.
; The check is here rather than in C for one reason: WSM_MAGIC, WSM_ABI and
; WSM_SIZE are nasm equs out of the two files this assembly shares with the
; module, and a C copy of them would be a fourth place for the contract to
; drift - which is the exact failure the ABI word exists to prevent.
; -----------------------------------------------------------------------------
_wcv_stamp:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov ax, 1
    cmp word [es:WSM_H_MAGIC], WSM_MAGIC
    jne .out
    mov ax, 2
    cmp word [es:WSM_H_ABI], WSM_ABI    ; a CONTRACT change need not change a
    jne .out                            ; size, which is the half .OVL's two
    cmp word [es:WSM_H_SIZE], WSM_SIZE  ; size words cannot express
    jne .out
    xor ax, ax
.out:
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned wcv_stateoff(unsigned seg) - the state block's offset (1.2.2 +6).
; -----------------------------------------------------------------------------
_wcv_stateoff:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov ax, [es:WSM_H_STATE]
    pop es
    pop bp
    ret

; =============================================================================
; WEAVE-SPEC 4.12's ops/s BANNER
;
; It lives beside the canvas seam because wave 5 is the wave that COMMISSIONED
; the field run (13.1), and it is not about the canvas at all: it is the WVM's
; own self-measurement, the number the 5150 will be asked to read off the
; glass, and the thing that converts 4.12 from design arithmetic to a
; measurement.
;
; ONLY EXHAUSTED SLICES COUNT, which is 4.10's own rule and the only honest
; window: a slice that finished early spent an unknown fraction of its budget,
; and counting it would divide real ops by wall-clock time the VM was not
; running in.
;
; IT IS ASSEMBLY FOR THE ORDINARY REASON: three divisions and a modulo in C
; came to 94 instructions, and this is 30. The arithmetic is 4.12's, written
; so that no intermediate leaves 16 bits - `acc / ticks` is at most ~1,700 at
; the contract's ceiling and `acc mod ticks` is under 36.
; =============================================================================

; void wcv_ops(int spent) - one exhausted slice's budget.
_wcv_ops:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    add [wcv_acc], ax
    call OSAPI_GET_TICKS            ; AX = [ticks]
    mov [wcv_now], ax
    sub ax, [wcv_t0]
    cmp ax, 18
    jb .out                         ; the window is not closed yet
    cmp ax, 36
    ja .reset                       ; ...and one longer than 36 ticks is
                                    ; DISCARDED rather than scaled: the VM was
                                    ; idle for most of it and the quotient
                                    ; would be a statement about the desktop
    mov [wcv_win], ax
    mov ax, [wcv_acc]
    or ax, ax
    jz .reset
    xor dx, dx
    div word [wcv_win]              ; AX = ops a tick, DX = what is left over
    mov di, ax
    mov si, dx
    mov bx, 18
    mul bx                          ; the 18 of 18.2...
    mov cx, ax
    mov ax, di
    mov bx, 5
    xor dx, dx
    div bx
    add cx, ax                      ; ...and the 0.2, as a fifth
    mov ax, si
    mov bx, 18
    mul bx                          ; DX = 0: si < 36, so si x 18 < 648
    div word [wcv_win]
    add cx, ax                      ; ...and the leftover's own share
    mov [wcv_ops], cx
.reset:
    mov word [wcv_acc], 0
    mov ax, [wcv_now]
    mov [wcv_t0], ax
.out:
    pop di
    pop si
    pop bp
    ret

; unsigned wcv_opsps(void) - the last full window's answer, 0 = never yet.
_wcv_opsps:
    mov ax, [wcv_ops]
    ret

; -----------------------------------------------------------------------------
; unsigned wcv_call(unsigned verb, unsigned a, unsigned b, unsigned c)
;
; One far call into WEAVE.WSM (WEAVE-SPEC 6.10.3). `verb` is a WORD - the low
; byte is the verb and the high byte its flag - so WSMV_SPRITE's read/write
; bit costs no fourth argument.
;
; With no module bound the answer is 0, which is the same "it did not happen"
; a refused overlay gives (apps/cc/crt0.asm), and for the same reason: every
; caller has to be able to ask "did it run" separately from "what did it say".
; A bundle with no <canvas> never binds one and every call here is that 0.
; -----------------------------------------------------------------------------
_wcv_call:
    push bp
    mov bp, sp
    push si
    push di
    push es
    cmp word [wcv_vec + 2], 0
    je .none
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call far [wcv_vec]
    jmp short .out
.none:
    xor ax, ax
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void wcv_bindmod(unsigned seg)
;
; Stamp the module's claim into the vector, or 0 to forget it. The OFFSET is
; WSM_ENTRY, an assemble-time constant out of the one file both assemblies
; share - so the two halves cannot disagree about where the door is.
; -----------------------------------------------------------------------------
_wcv_bindmod:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov [wcv_vec + 2], ax
    mov word [wcv_vec], WSM_ENTRY
    pop bp
    ret

; -----------------------------------------------------------------------------
; void wcv_run(void *win)
;
; The worker's body, and it DOES NOT RETURN while a module is bound
; (SPEC.md 20.6 rule 2). It returns only when there is none, which is the
; degenerate case crt0.asm's park loop then handles - a worker that was
; spawned and whose module then went away is a worker with nothing to do, not
; a worker that may exit.
;
; THE ENTRY THE KERNEL WAS GIVEN IS STILL IN THIS IMAGE, and it has to be:
; OSAPI_TASK_SPAWN checks that the entry falls inside the calling instance's
; own region and refuses otherwise (SPEC.md 20.6's ownership fence). So
; cc_worker calls os88_worker(), which calls this, which far-calls the module
; - three instructions of trampoline for a rule that cannot be argued with.
; -----------------------------------------------------------------------------
_wcv_run:
    push bp
    mov bp, sp
    cmp word [wcv_vec + 2], 0
    je .out
    mov bx, [bp+4]                  ; our window, for TASK_ALIVE and WM_WAKE
    mov ax, WSMV_WORKER
    call far [wcv_vec]
.out:
    pop bp
    ret

section .bss
wcv_acc:    resw 1                  ; 4.12's accumulator...
wcv_t0:     resw 1                  ; ...the tick the window opened...
wcv_ops:    resw 1                  ; ...and the last closed window's answer
wcv_win:    resw 1                  ; the window's length, and `now`, both
wcv_now:    resw 1                  ; banked across the three divisions

section .data
wcv_vec:    dw 0, 0                 ; (offset, segment) - the segment is 0
                                    ; until the load path has read, stamped
                                    ; and believed the module
section .text
