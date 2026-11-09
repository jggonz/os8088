; =============================================================================
; os8088 - apps/arcade/arcn88.asm - the N88 core: a board's own 8088 code,
; run NATIVELY (ARCADE-SPEC 10)
;
; Gottlieb's Q*bert board is an 8088, and its program names no segment but
; 0: it lives flat in one 64KB space. So it is not translated at all - it
; runs as it is, in a 64KB claim of the host's (G), with its three segment
; loads NOPped and its waits for the frame turned into a far call to
; n88_yield (apps/arcade/qbert.patch). This part is what sits between the
; host loop and that code: the same far-call door every core has (AH_ENTER,
; AX = the view), and the way back.
;
; A RUN: the host's SS:SP kept in the view, the game's stack put back, and
; the game resumed where it yielded - or, when the host has delivered the
; board's NMI (NV_NMI), its handler first, through the vector the game
; itself wrote at G:0008h, with an interrupt frame that IRETs into the
; resume. The game runs with INTERRUPTS OFF, as nothing of the host's then
; ever lands on its stack below its own data (ARCADE-SPEC 10.3), and a run
; ends only at the next yield: the game is cooperative, one frame a run.
;
; The game's registers live on its own stack across a yield: FLAGS, AX, BX,
; CX, DX, SI, DI, BP pushed there, SS:SP in the view. DS = ES = SS = G
; throughout the game. The view's segment, for the yield, is kept in G at
; N88_GVIEW, a word the board does not map - never in CS, which kern_mega
; runs as a code selector a store through faults on (docs/plans/
; ARCADE-MEGA-PLAN.md M2): this part writes nothing through CS.
; =============================================================================
    cpu 8086
    org 0

%include "arcdefs.inc"

    dw ARC_MAGIC                    ; AH_MAGIC
    dw n88_enter                    ; AH_ENTER
    dw n88_end                      ; AH_END
    dw n88_yield                    ; AH_YIELD: the far call each yield makes

N88_GVIEW   equ 0x9FFE              ; G: the view's segment, for the yield -
                                    ;   Gottlieb's map has nothing at 6000-9FFFh
                                    ;   (the program is A000h up)

; n88_enter - far: AX = the view. Runs the game to its next yield; out:
; ST_WHY = ARCX_HALT (the game waits for the next NMI), every register but
; DS and those the host's call keeps clobbered
n88_enter:
    cli
    mov ds, ax
    mov dx, ax                      ; (the view, for G)
    mov [ST_HSS], ss
    mov [ST_HSP], sp
    mov cx, [NV_GSEG]
    mov bx, [NV_GSP]
    mov di, [NV_GCS]                ; (G as code: the NMI frame's CS)
    mov al, 0
    xchg al, [NV_NMI]
    mov ss, cx
    mov sp, bx
    mov ds, cx
    mov es, cx
    mov [N88_GVIEW], dx
    or al, al
    jz n88_resume
    pushf                           ; the NMI: a frame that IRETs into the
    push cs                         ;   resume below, the handler at the
    mov ax, n88_resume              ;   game's own vector [0008h]
    push ax
    push di                         ; G's CODE half: never a data selector
    push word [0x0008]              ;   loaded into CS (ARCADE-MEGA-PLAN M3)
    ; STKBALANCE-OK: a far JUMP into the game's NMI handler, on the GAME's
    ;   stack (SS = G): its IRET comes back to n88_resume
    retf

; the game, back where it yielded: its registers off its own stack
n88_resume:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ; STKBALANCE-OK: the game's own stack, as n88_yield left it: back into
    ;   the game past its yield's far call
    retf

; n88_yield - far, called by the game in place of its wait: everything onto
; the game's stack, and back to the host's call
n88_yield:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ds, [ss:N88_GVIEW]          ; (SS = G)
    mov [NV_GSP], sp
    mov ss, [ST_HSS]
    mov sp, [ST_HSP]
    mov byte [ST_WHY], ARCX_HALT
    ; STKBALANCE-OK: the HOST's stack again (ST_HSS:ST_HSP, kept by
    ;   n88_enter): the return to the host loop's far call
    retf

n88_end:
