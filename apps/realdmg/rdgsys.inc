; =============================================================================
; os8088 - apps/realdmg/rdgsys.inc
;
; RealDMG's SYSTEM half: everything the core (build/realdmg/rdgcore.asm, from
; apps/realdmg/rdggen.py) does not do inline - the event loop, interrupts,
; the PPU's timing, the timer, the I/O registers, the joypad, the link port,
; OAM DMA and the cartridge's bank switching. docs/REALDMG-SPEC.md is the
; contract, docs/plans/REALDMG-PLAN.md section 4 the design and rdgdefs.inc
; the layout.
;
; %included by every HOST: REALDMG.O88 and the boot-sector harness in
; tests/rdgcore/. A host provides:
;
;   rdg_putc     near proc: AL = a byte the game sent on its link port.
;               May be a bare RET.
;   rdg_trbyte   near proc, RDG_TRHOST hosts only: AL = a byte of an RDG_TRACE
;               core's trace record; preserves every register. A host that
;               defines RDG_TRHOST (the harness) gets the trace's receiving
;               half below - rdg_trace_on, rdg_trace, rdg_trline; one that does
;               not (REALDMG.O88, whose core is never an RDG_TRACE build)
;               assembles none of it.
;
; and calls, near, with CS the segment this file assembled into:
;
;   rdg_init     AX = the view's segment, BX = the core's, CX = the ROM
;               image's, DX = the image's size in 16KB banks. Copies bank 0
;               into the view and sets the machine to the state the boot ROM
;               leaves (RealDMG has no boot ROM, plan section 3.1).
;   rdg_run      AX = M-cycles to run at most. Returns AX = RDGX_*: a frame is
;               ready (a VBlank began), the budget ran out, or the game hit
;               an opcode the SM83 does not have or STOP.
;
; Everything here runs on the HOST's stack. The core enters it by a far jump
; with SS:SP the Game Boy's stack; the first thing each entry does is save
; that SP and switch to the host stack rdg_run left in S_HSS:S_HSP, so that
; nothing below writes under the game's SP.
; =============================================================================


rdg_viewseg      dw 0                    ; the view, for code on the host stack
rdg_coreseg      dw 0
; THE BANK TABLE (REALDMG-SPEC section 10.3): RDG_BTAB holds, per ROM bank,
; the segment whose 4000-7FFF is that bank, or 0 when it is not in memory.
; With rdg_nres 0 the image at CX holds every bank and rdg_init fills the table
; from it; otherwise the host keeps the banks (a cache over extended memory)
; and fills it: rdg_init zeroes it and calls rdg_bfill, and rdg_setbank calls
; rdg_bmiss (BX = the bank x 2) for a bank the table has no segment for -
; both near procs of the host's, every register theirs but BX.
rdg_nres         dw 0
rdg_bfill        dw 0
rdg_bmiss        dw 0

; =============================================================================
; rdg_init - AX = view, BX = core, CX = ROM image, DX = 16KB banks in it
; =============================================================================
rdg_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    cld
    mov [cs:rdg_viewseg],ax
    mov [cs:rdg_coreseg],bx
    mov es,ax
    ; the view to zero from 4000h, then bank 0 copied into 0000-3FFF (from
    ; itself when the host read bank 0 there: CX = the view)
    push cx
    mov di,4000h
    xor ax,ax
    mov cx,6000h
    rep stosw
    pop cx
    mov [es:S_ROMSEG],cx
    mov [es:S_NBANKS],dx
    mov al,0FFh                         ; the core's MBC path masks a bank
    cmp dx,256                          ;   with this; past 256 banks it
    ja .bm                              ;   cannot be in memory anyway
    mov al,dl
    dec al
.bm:
    mov [es:S_BMASK],al
    mov ds,cx
    xor si,si
    xor di,di
    mov cx,2000h
    rep movsw
    mov ax,es
    mov ds,ax                           ; DS = view from here on
    mov [S_VIEW],ax
    ; the bank table: bank 0 is the view's own 0000-3FFF (MBC5 can put it in
    ; 4000-7FFF), the rest the image's - or the host's, which fills them
    sub ax,400h
    mov [RDG_BTAB],ax
    mov cx,[S_NBANKS]
    dec cx
    mov di,RDG_BTAB+2
    cmp word [cs:rdg_nres],0
    jne .bcache
    mov ax,[S_ROMSEG]
.bt:
    stosw
    add ax,400h
    loop .bt
    jmp short .btd
.bcache:
    xor ax,ax
    rep stosw
    call [cs:rdg_bfill]
.btd:
    ; the two flag tables: F <-> the LAHF image the core keeps in AH
    xor bx,bx
.ftab:
    xor al,al
    test bl,80h
    jz .f1
    or al,RDGF_Z
.f1:
    test bl,40h
    jz .f2
    or al,RDGF_N
.f2:
    test bl,20h
    jz .f3
    or al,RDGF_H
.f3:
    test bl,10h
    jz .f4
    or al,RDGF_C
.f4:
    mov [RDG_F2AH+bx],al
    xor al,al
    test bl,RDGF_Z
    jz .g1
    or al,80h
.g1:
    test bl,RDGF_N
    jz .g2
    or al,40h
.g2:
    test bl,RDGF_H
    jz .g3
    or al,20h
.g3:
    test bl,RDGF_C
    jz .g4
    or al,10h
.g4:
    mov [RDG_AH2F+bx],al
    inc bl
    jnz .ftab
    ; the far vectors the core jumps through
    mov ax,cs
    mov word [S_EVVEC],rdg_ev_entry
    mov [S_EVVEC+2],ax
    mov word [S_IORVEC],rdg_ior
    mov [S_IORVEC+2],ax
    mov word [S_IOWVEC],rdg_iow
    mov [S_IOWVEC+2],ax
    mov word [S_MBCVEC],rdg_mbc
    mov [S_MBCVEC+2],ax
%ifdef RDG_TRHOST
    mov word [S_TRACEVEC],rdg_trace
    mov [S_TRACEVEC+2],ax
%endif
    mov ax,[cs:rdg_coreseg]
    mov [S_RETFAR+2],ax
    mov [S_DISP+2],ax
    mov word [S_DISP],0DD00h            ; RDG_DISPATCH, slot DD's main half
    mov [S_RESVEC+2],ax
    mov word [S_RESVEC],0DD00h          ; rdg_trace_on points it at rdg_trace
    ; the cartridge: its MBC from the header, bank 1 in ROMX
    mov es,[S_ROMSEG]
    mov al,[es:0147h]
    mov ah,RDGM_MBC1
    cmp al,01h
    jb .none
    cmp al,03h
    jbe .mbc
    mov ah,RDGM_MBC2
    cmp al,05h
    jb .none
    cmp al,06h
    jbe .mbc
    mov ah,RDGM_MBC3
    cmp al,0Fh
    jb .none
    cmp al,13h
    jbe .mbc
    mov ah,RDGM_MBC5
    cmp al,19h
    jb .none
    cmp al,1Eh
    jbe .mbc
.none:
    mov ah,RDGM_NONE
.mbc:
    mov [S_MBC],ah
    mov byte [S_BANK],1
    call rdg_setbank
    ; the registers the DMG boot ROM leaves
    mov byte [S_AF],01h                 ; A = 01, F = B0 as AH sees it
    mov al,[RDG_F2AH+0B0h]
    mov [S_AF+1],al
    mov word [S_BC],0013h
    mov word [S_DE],00D8h
    mov word [S_HL],014Dh
    mov word [S_SP],0FFFEh
    mov word [S_PC],0100h
    ; the I/O registers the boot ROM leaves
    mov byte [0FF00h],0CFh
    mov byte [0FF02h],7Eh
    mov byte [0FF0Fh],0E1h
    mov byte [0FF07h],0F8h
    mov byte [0FF46h],0FFh
    mov byte [0FF10h],80h
    mov byte [0FF11h],0BFh
    mov byte [0FF12h],0F3h
    mov byte [0FF14h],0BFh
    mov byte [0FF16h],3Fh
    mov byte [0FF19h],0BFh
    mov byte [0FF1Ah],7Fh
    mov byte [0FF1Bh],0FFh
    mov byte [0FF1Ch],9Fh
    mov byte [0FF1Eh],0BFh
    mov byte [0FF20h],0FFh
    mov byte [0FF23h],0BFh
    mov byte [0FF24h],77h
    mov byte [0FF25h],0F3h
    mov byte [0FF26h],0F1h
    mov di,0FF4Ch                       ; FF4C-FF7F: nothing on a DMG, so
    mov cx,(0FF80h-0FF4Ch)/2            ;   they read FF and the core drops
    mov ax,0FFFFh                       ;   writes there
    push es
    push ds
    pop es
    rep stosw
    pop es
    mov byte [0FF40h],91h
    mov byte [0FF41h],85h
    mov byte [0FF47h],0FCh
    mov byte [0FF48h],0FFh
    mov byte [0FF49h],0FFh
    ; time: zero is now. The LCD is on, line 0 begins now.
    mov byte [S_LCDON],1
    mov word [S_TVBL],RDG_VBL
    mov word [S_TVBL+2],0
    mov word [S_TSTAT+2],RDG_FAR_HI
    mov word [S_TTIMA+2],RDG_FAR_HI
    mov word [S_TSTOP+2],RDG_FAR_HI
    mov word [S_TNEXT],RDG_VBL
    mov word [S_BP],RDG_VBL
    mov word [S_TNF],RDG_VBL
    mov word [S_TDIV],-(0ABCCh/4)       ; the counter under DIV reads ABCCh
    mov word [S_TDIV+2],0FFFFh          ;   T-states at 0100 on a DMG (boot_div)
    mov byte [S_SNAPTAG],0FFh
    mov word [S_IDLENO],0FFFFh
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; rdg_run - AX = budget in M-cycles. Out: AX = RDGX_*.
; =============================================================================
rdg_run:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    cld
    mov ds,[cs:rdg_viewseg]
    push ax
    mov bp,[S_BP]
    call rdg_now                         ; DX:AX = now
    mov [S_NOW],ax
    mov [S_NOW+2],dx
    pop cx
    mov [S_BUDLEN],cx
    add ax,cx
    adc dx,0
    mov [S_TSTOP],ax
    mov [S_TSTOP+2],dx
    mov [S_HSP],sp
    mov [S_HSS],ss
    call rdg_sched                       ; TNEXT = the earliest event, BP to it
    mov [S_BP],bp
    cmp byte [S_HALT],0                 ; a HALT the last budget ended inside
    je .go                              ;   is still a HALT: on to the next
    call rdg_events                      ;   event, not into the instruction
    jc rdg_exit                          ;   after it (REALDMG-SPEC section 7)
.go:
    jmp rdg_resume

; rdg_exit: back to rdg_run's caller, from anywhere on the host stack. DS = view.
rdg_exit:
    ; STKBALANCE-OK: never called - jumped to from any depth of the host
    ; stack; SP comes back from S_HSP, where rdg_run banked it below its pushes
    mov sp,[S_HSP]
    xor ax,ax
    xchg al,[S_EXIT]
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; rdg_resume: into the core with the state block's registers. DS = view.
rdg_resume:
    mov ax,[S_AF]
    mov bx,[S_HL]
    mov cx,[S_BC]
    mov di,[S_DE]
    mov si,[S_PC]
    mov bp,[S_BP]
    mov dx,si
    and dh,0C0h
    cmp dh,40h
    mov dx,[S_VIEW]
    jne .v
    mov dx,[S_ROMX]
.v:
    cli                                 ; the core runs with interrupts OFF:
    mov ss,[S_VIEW]                     ;   SS:SP is the Game Boy's stack, and
    mov sp,[ss:S_SP]                    ;   an IRQ's frame would land in its
                                        ;   memory (REALDMG-SPEC section 5.2)
    mov ds,dx
    xor dx,dx
    jmp far [ss:S_RESVEC]

; =============================================================================
; Entries from the core. Each arrives with SS:SP the game's stack.
; =============================================================================

; RDG_TOHOST: save the game's SP and DS, switch to the host stack, DS = view -
; and interrupts back on, which the core runs without.
%macro RDG_TOHOST 0
    mov [ss:S_SAVDS],ds
    cli
    mov [ss:S_SP],sp
    mov sp,[ss:S_HSP]
    mov ss,[ss:S_HSS]
    sti
    mov ds,[cs:rdg_viewseg]
    cld
%endmacro

; rdg_ret: back into the core at S_RETFAR, the game's DS and SP restored.
; DH (an I/O read's answer) survives; DL leaves as 0.
rdg_ret:
    cli                                 ; interrupts stay off in the core
    mov ss,[cs:rdg_viewseg]
    mov sp,[ss:S_SP]
    mov ds,[ss:S_SAVDS]
    xor dl,dl
    jmp far [ss:S_RETFAR]

; --- the event entry: BP < 0, HALT, a spin-wait, RETI, STOP, a bad opcode ---
rdg_ev_entry:
    mov [ss:S_AF],ax
    mov [ss:S_HL],bx
    mov [ss:S_BC],cx
    mov [ss:S_DE],di
    mov [ss:S_PC],si
    mov [ss:S_BP],bp
    RDG_TOHOST
    call rdg_events                      ; CF = 1: leave for the host
    jc rdg_exit
    jmp rdg_resume

; --- an I/O register read: BX = FF00-FF7F. Answers DH. ----------------------
rdg_ior:
    RDG_TOHOST
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call rdg_ioread                      ; AL = the byte
    mov dh,al
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    jmp rdg_ret

; --- an I/O register write: BX = FF00-FF7F or FFFF, [S_WVAL] = the byte -----
rdg_iow:
    RDG_TOHOST
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call rdg_iowrite
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [S_TAKEINT],0              ; IF or IE written: an interrupt now
    je rdg_ret                           ;   pending is taken at once, before
    mov byte [S_TAKEINT],0              ;   the next instruction
    call rdg_irqnow
    jmp rdg_ret

; rdg_irqnow: if an interrupt is enabled and pending, take it NOW, from inside
; an instruction's write: SI (the core's PC) is already the next
; instruction's, so it is what gets pushed. DS = view. Moves SI, SP (via
; S_SP), the core's DS (via S_SAVDS) and BP; everything else kept.
rdg_irqnow:
    push ax
    push bx
    push cx
    cmp byte [S_IME],0
    je .r
    mov al,[0FFFFh]
    and al,[0FF0Fh]
    and al,1Fh
    jz .r
    mov byte [S_IME],0
    xor cl,cl
    mov ah,1
.w:
    test al,ah
    jnz .t
    shl ah,1
    inc cl
    jmp .w
.t:
    not ah
    and [0FF0Fh],ah
    mov bx,[S_SP]
    sub bx,2
    mov [S_SP],bx
    mov [bx],si
    mov al,cl
    shl al,1
    shl al,1
    shl al,1
    add al,40h
    xor ah,ah
    mov si,ax
    mov ax,[S_VIEW]
    mov [S_SAVDS],ax
    sub bp,5
.r:
    pop cx
    pop bx
    pop ax
    ret

; --- a write to the cartridge (0000-7FFF): BX = address, [S_WVAL] = byte ----
rdg_mbc:
    RDG_TOHOST
    mov [S_PC],si                       ; rdg_setbank asks where the game runs
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call rdg_mbcwrite
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp rdg_ret

%ifdef RDG_TRHOST
; --- RDG_TRACE builds: one line per instruction, then dispatch ---------------
; rdg_trace_on - with an RDG_TRACE core, trace the first instruction after
; every return to the core as well, so the trace has no gaps.
rdg_trace_on:
    push ds
    mov ds,[cs:rdg_viewseg]
    mov word [S_RESVEC],rdg_trace
    mov [S_RESVEC+2],cs
    pop ds
    ret
rdg_trace:
    mov [ss:S_AF],ax
    mov [ss:S_HL],bx
    mov [ss:S_BC],cx
    mov [ss:S_DE],di
    mov [ss:S_PC],si
    RDG_TOHOST
    push ax
    push bx
    push cx
    push si
    push di
    call rdg_trline
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    cli                                 ; interrupts stay off in the core
    mov ss,[cs:rdg_viewseg]
    mov sp,[ss:S_SP]
    mov ds,[ss:S_SAVDS]
    xor dx,dx
    jmp far [ss:S_DISP]                 ; the RAW dispatch, never traced
; One trace record, 14 bytes, to the host's rdg_trbyte: PC, A, F (as the Game
; Boy holds it), BC, DE, HL, SP, and the time in M-cycles - words little-endian.
; tools/gbtrace.py diffs it against Peanut-GB; the first record whose time
; differs is the first wrong cycle count.
rdg_trline:
    mov ax,[S_PC]
    call rdg_trword
    mov al,[S_AF]
    call rdg_trbyte
    mov bl,[S_AF+1]
    xor bh,bh
    mov al,[RDG_AH2F+bx]
    call rdg_trbyte
    mov ax,[S_BC]
    call rdg_trword
    mov ax,[S_DE]
    call rdg_trword
    mov ax,[S_HL]
    call rdg_trword
    mov ax,[S_SP]
    call rdg_trword
    call rdg_now
rdg_trword:
    call rdg_trbyte
    mov al,ah
    jmp rdg_trbyte
%endif

; =============================================================================
; Time. Every time is a dword of M-cycles since rdg_init.
; =============================================================================

; rdg_now: DX:AX = S_TNEXT - BP (signed). BP = the core's.
rdg_now:
    mov ax,[S_TNEXT]
    mov dx,[S_TNEXT+2]
    or bp,bp
    js .neg
    sub ax,bp
    sbb dx,0
    ret
.neg:
    sub ax,bp
    sbb dx,-1
    ret

; rdg_force: make the core leave at its next check. BP = 0, TNEXT = now.
rdg_force:
    push ax
    push dx
    call rdg_now
    mov [S_TNEXT],ax
    mov [S_TNEXT+2],dx
    sub ax,[S_TFRAME]                   ; and S_TNF, which an LCDC write that
    mov [S_TNF],ax                      ;   moved S_TFRAME relies on
    xor bp,bp
    pop dx
    pop ax
    ret

; rdg_sched: TNEXT = the earliest pending event (never more than 0x7FFF
; M-cycles past now), BP = TNEXT - now. In: [S_NOW] = now.
; RDG_MIN T: DX:AX = min(DX:AX, [T]) - inline: rdg_sched runs twice an event,
; and an event is every line on a game with the HBlank interrupt on
%macro RDG_MIN 1
    cmp dx,[%1+2]
    jb %%keep
    ja %%take
    cmp ax,[%1]
    jbe %%keep
%%take:
    mov ax,[%1]
    mov dx,[%1+2]
%%keep:
%endmacro
; RDG_DUE T: CF = 0 if [T] <= [S_NOW], 1 if it is still to come - the
; borrow of now - T (DX:AX = that difference)
%macro RDG_DUE 1
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    sub ax,[%1]
    sbb dx,[%1+2]
%endmacro

rdg_sched:
    call rdg_nextev
    mov [S_TNEXT],ax
    mov [S_TNEXT+2],dx
    sub ax,[S_NOW]
    mov bp,ax
    push dx                             ; S_TFRAME to the frame holding now,
.fr:                                    ;   then S_TNF for the core
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    sub ax,[S_TFRAME]
    sbb dx,[S_TFRAME+2]
    jnz .fadv
    cmp ax,RDG_FRAME
    jb .fok
.fadv:
    add word [S_TFRAME],RDG_FRAME
    adc word [S_TFRAME+2],0
    jmp .fr
.fok:
    pop dx
    mov ax,[S_TNEXT]
    sub ax,[S_TFRAME]
    mov [S_TNF],ax
    ret

; rdg_nextev: DX:AX = the earliest pending event, never more than 0x7FFF
; M-cycles past [S_NOW] - rdg_sched's question alone, for rdg_events' skip,
; which wants only where to go and not the core's TNEXT, BP or TNF
rdg_nextev:
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    add ax,7FFFh
    adc dx,0
    RDG_MIN S_TVBL
    RDG_MIN S_TSTAT
    RDG_MIN S_TTIMA
    RDG_MIN S_TSTOP
    ret

; rdg_lcdpos: the PPU's position at [S_NOW]. Out: AL = LY, DX = M-cycles into
; the line. Normalises S_TFRAME to the frame holding now.
rdg_lcdpos:
    push bx
.again:
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    sub ax,[S_TFRAME]
    sbb dx,[S_TFRAME+2]
    jnz .adv                            ; a frame's worth or more behind
    cmp ax,RDG_FRAME
    jb .ok
.adv:
    add word [S_TFRAME],RDG_FRAME
    adc word [S_TFRAME+2],0
    jmp .again
.ok:
    xor dx,dx
    mov bx,RDG_LINE
    div bx                              ; AX = line, DX = position
    pop bx
    ret

; =============================================================================
; rdg_events - the core left for an event. DS = view, state block current.
; CF = 1: return to the host (S_EXIT says why); CF = 0: resume the core.
; =============================================================================
rdg_events:
    inc word [S_EVCOUNT]
    mov bp,[S_BP]
    call rdg_now
    mov [S_NOW],ax
    mov [S_NOW+2],dx
    cmp byte [S_EXIT],0
    jne .leave
    ; HALT or a spin-wait: nothing happens until the next event, so go there
    mov al,[S_HALT]
    or al,[S_IDLE]
    jz .due
    mov byte [S_IDLE],0
    cmp byte [S_HALT],0
    je .skip
    mov al,[0FFFFh]
    and al,[0FF0Fh]
    and al,1Fh
    jz .skip
    mov byte [S_HALT],0                 ; a pending interrupt ends HALT
    jmp .due
.skip:
    cmp byte [S_HALT],0                 ; a SPIN-WAIT's skip runs with
    jne .skh                            ;   interrupts on, so the budget - the
    cmp word [S_TVBL+2],RDG_FAR_HI       ;   core's interrupts-off time - is
    je .skh                             ;   not spent by it: TNEXT the next
    push word [S_TSTOP+2]               ;   event but the budget's, and the
    mov word [S_TSTOP+2],RDG_FAR_HI      ;   budget moved on by the skip. ~3 of
    call rdg_nextev                      ;   Super Mario Land's 4.4 budget
    pop word [S_TSTOP+2]                ;   exits a frame ended in one. (The
    sub ax,[S_NOW]                      ;   LCD off keeps it: no VBlank would
    or ax,ax                            ;   end the run; HALT lapses it below.
    jle .due                            ;   The test is the difference's sign)
    add [S_TSTOP],ax
    adc word [S_TSTOP+2],0
    jmp short .skd
.skh:
    call rdg_nextev                      ; DX:AX = the next event...
    sub ax,[S_NOW]                      ; ...AX = how far: within 0x7FFF
    or ax,ax                            ;   (its sign, not the operands')
    jle .due                            ; one is already due
.skd:
    add [S_SKIPPED],ax                  ; (the core's TNEXT and BP are
    adc word [S_SKIPPED+2],0            ;   rdg_sched's, on the way out)
    add [S_NOW],ax
    adc word [S_NOW+2],0
.due:
    ; every event whose time has come
    RDG_DUE S_TVBL
    jc .n1
    call rdg_vblank
.n1:
    RDG_DUE S_TSTAT
    jc .n2
    call rdg_statev
.n2:
    RDG_DUE S_TTIMA
    jc .n3
    call rdg_timaev
.n3:
    RDG_DUE S_TSTOP
    jc .n4
    mov word [S_TSTOP+2],RDG_FAR_HI
    cmp byte [S_HALT],0                 ; HALTED, the budget is not the
    je .bud                             ;   core's interrupts-off time - this
    cmp word [S_TVBL+2],RDG_FAR_HI       ;   runs with them on - so it lapses,
    jne .n4                             ;   and the VBlank's own exit ends the
.bud:                                   ;   run; with the LCD off nothing would
    cmp byte [S_EXIT],0
    jne .n4
    mov byte [S_EXIT],RDGX_BUDGET
.n4:
    ; an interrupt, if one is enabled and pending
    mov al,[0FFFFh]
    and al,[0FF0Fh]
    and al,1Fh
    jz .noint
    mov byte [S_HALT],0
    cmp byte [S_IME],0
    je .noint
    mov byte [S_IME],0
    mov cl,0
    mov ah,1
.which:
    test al,ah
    jnz .take
    shl ah,1
    inc cl
    jmp .which
.take:
    not ah
    and [0FF0Fh],ah
    mov bx,[S_SP]
    sub bx,2
    mov [S_SP],bx
    mov ax,[S_PC]
    mov [bx],ax
    mov al,cl                           ; PC = 40h + 8 * the bit
    shl al,1
    shl al,1
    shl al,1
    add al,40h
    xor ah,ah
    mov [S_PC],ax
    add word [S_NOW],5
    adc word [S_NOW+2],0
.noint:
    cmp byte [S_HALT],0                 ; still halted: on to the next event
    je .sched
    cmp byte [S_EXIT],0
    jne .sched
    jmp .skip
.sched:
    call rdg_sched
    mov [S_BP],bp
    cmp byte [S_EXIT],0
    jne .left                           ; (scheduled already: not twice)
    clc
    ret
.leave:
    call rdg_sched
    mov [S_BP],bp
.left:
    stc
    ret
.isdue:                                 ; CF = 1 if [SI] <= now: no borrow
    mov ax,[S_NOW]                      ;   in now - [SI]
    mov dx,[S_NOW+2]
    sub ax,[si]
    sbb dx,[si+2]
    cmc
    ret

; rdg_catchup: run every event already due at [S_NOW] that sets IF - from
; inside an instruction, so nothing here takes an interrupt or leaves. A read
; of IF sees what the hardware would; the core's own TNEXT may then point at
; an event already handled, which costs one empty event entry and no more.
rdg_catchup:
    push ax
    push bx
    push dx
    push si
    mov si,S_TVBL
    call rdg_events.isdue
    jnc .c1
    call rdg_vblank
.c1:
    mov si,S_TSTAT
    call rdg_events.isdue
    jnc .c2
    call rdg_statev
.c2:
    mov si,S_TTIMA
    call rdg_events.isdue
    jnc .c3
    call rdg_timaev
.c3:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; --- VBlank: line 144 began -------------------------------------------------
rdg_vblank:
    call rdg_sqframe
    call rdg_rtcframe
    or byte [0FF0Fh],01h
    test byte [0FF41h],10h
    jz .n
    or byte [0FF0Fh],02h
.n:
    inc word [S_FRAMES]
    mov al,[S_FRAMES]                   ; the frame that ended took no snapshot
    dec al                              ;   (no raster write): its tag aged two
    cmp al,[S_SNAPTAG]                  ;   behind, or the byte came round to a
    je .tag                             ;   stale RDG_SNAP every 256 frames and
    dec al                              ;   that frame was composed from it -
    mov [S_SNAPTAG],al                  ;   all zeros, an LCD off, a white one
.tag:
    mov byte [S_IDLECNT],0
    add word [S_TVBL],RDG_FRAME
    adc word [S_TVBL+2],0
    cmp byte [S_EXIT],0
    jne .r
    mov byte [S_EXIT],RDGX_FRAME
.r:
    ret

; --- STAT: an enabled LYC, mode 2 or mode 0 condition began -----------------
; Judged at the event's OWN time, S_TSTAT: the core reaches an event at its
; next check, M-cycles later, and a line's position by then is not the one
; the condition began at.
rdg_statev:
    mov al,[0FF41h]                     ; MODE 0's alone (Pokemon Gold's, for
    and al,68h                          ;   the whole game - 144 a frame): the
    cmp al,08h                          ;   next is the line after's, 114 on,
    jne .gen                            ;   with no position worked out and
    mov ax,[S_TSTAT]                    ;   no walk, while rdg_statsch's line
    cmp ax,[S_STATT]                    ;   is still the one S_TSTAT is for.
    jne .gen                            ;   Line 143's next is a frame on: the
    mov ax,[S_TSTAT+2]                  ;   walk's
    cmp ax,[S_STATT+2]
    jne .gen
    mov al,[S_STATLN]
    cmp al,143
    jae .gen
    or byte [0FF0Fh],02h
    inc byte [S_STATLN]
    mov ax,[S_TSTAT]
    mov dx,[S_TSTAT+2]
    add ax,RDG_LINE
    adc dx,0
    mov [S_TSTAT],ax
    mov [S_TSTAT+2],dx
    mov [S_STATT],ax
    mov [S_STATT+2],dx
    ret
.gen:
    push word [S_NOW+2]
    push word [S_NOW]
    mov ax,[S_TSTAT]
    mov [S_NOW],ax
    mov ax,[S_TSTAT+2]
    mov [S_NOW+2],ax
    call .judge
    pop word [S_NOW]
    pop word [S_NOW+2]
    ret
.judge:
    call rdg_lcdpos                      ; AL = LY, DX = position
    mov ah,[0FF41h]
    or dx,dx
    jnz .h
    test ah,40h                         ; LYC
    jz .m2
    cmp al,[0FF45h]
    je .irq
.m2:
    test ah,20h                         ; mode 2, lines 0-143
    jz .r
    cmp al,144
    jb .irq
    jmp .r
.h:
    cmp dx,RDG_HBL
    jne .r
    test ah,08h                         ; mode 0
    jz .r
    cmp al,144
    jae .r
.irq:
    or byte [0FF0Fh],02h
.r:
    jmp rdg_statsch

; rdg_sqch: SI = the channel whose register BX is (FF10-FF14: 1, FF15-FF19:
; 2, FF20-FF23: the noise channel's)
rdg_sqch:
    mov si,S_SQ1
    cmp bl,15h
    jb .r
    mov si,S_SQ2
    cmp bl,20h
    jb .r
    mov si,S_SQ4
.r:
    ret

; rdg_sqframe: a frame of the two square channels' envelopes and lengths -
; 64 Hz envelope steps are 274/256 of a frame, 256 Hz length steps
; 1,097/256 (59.7275 Hz). The speaker plays what they leave (section 17).
rdg_sqframe:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si,S_SQ1
    mov bx,0FF12h
    call .ch
    mov si,S_SQ2
    mov bx,0FF17h
    call .ch
    mov si,S_SQ4                        ; the noise: a square's envelope
    mov bx,0FF21h
    call .ch
    mov si,S_SQ3                        ; the wave: a length and no envelope
    cmp byte [si+SQ_ON],0
    je .w
    call .len
.w:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.ch:
    cmp byte [si+SQ_ON],0
    je .r
    mov al,[bx]                         ; NRx2: the envelope's period, way
    mov cl,al
    and cl,7
    jz .len                             ; period 0: the volume holds
    xor ch,ch
    mov dh,cl                           ; DX = period x 256
    xor dl,dl
    add word [si+SQ_ENV],274
.env:
    cmp [si+SQ_ENV],dx
    jb .len
    sub [si+SQ_ENV],dx
    test al,08h
    jz .dn
    cmp byte [si+SQ_VOL],15
    jae .env
    inc byte [si+SQ_VOL]
    jmp short .env
.dn:
    cmp byte [si+SQ_VOL],0
    je .env
    dec byte [si+SQ_VOL]
    jmp short .env
.len:
    cmp byte [si+SQ_LEN],0
    je .r
    add word [si+SQ_LACC],1097
.l:
    cmp word [si+SQ_LACC],256
    jb .r
    sub word [si+SQ_LACC],256
    dec byte [si+SQ_LEN]
    jnz .l
    mov byte [si+SQ_ON],0
.r:
    ret

; rdg_statsch: S_TSTAT = the first STAT condition AFTER now.
rdg_statsch:
    mov word [S_TSTAT+2],RDG_FAR_HI
    cmp byte [S_LCDON],0
    je .r
    mov ah,[0FF41h]
    test ah,68h
    jz .r
    call rdg_lcdpos                      ; AL = LY, DX = position
    mov ah,[0FF41h]                     ; (its DIV left AH 0)
    mov bl,al                           ; BL = line, DX = position, CX = lines
    xor cx,cx                           ;   from now (0 = this one)
    test ah,28h                         ; LYC ALONE is arithmetic: the walk
    jnz .line                           ;   below is 155 lines of it, and it
    mov al,[0FF45h]                     ;   is Super Mario Land's status-bar
    cmp al,154                          ;   split, every frame. A mode bit
    jae .r                              ;   ends the walk within a line or
    sub al,bl                           ;   two (ten, in VBlank), so only
    jnc .ly                             ;   LYC-only skips it
    add al,154
.ly:
    jnz .lyn                            ; LYC = this line: its start is past,
    mov al,154                          ;   so the next is a frame on
.lyn:
    mov cl,al
    xor si,si
    jmp .found
.line:
    cmp cx,155
    ja .r
    ; candidates in line BL, at frame time CX*114 - position + offset
    or cx,cx
    jnz .start
    ; this line: only what is still ahead of the position
    cmp bl,144
    jae .next
    test ah,08h
    jz .next
    cmp dx,RDG_HBL
    jae .next
    mov si,RDG_HBL
    jmp .found
.start:
    test ah,40h
    jz .s2
    cmp bl,[0FF45h]
    je .at0
.s2:
    cmp bl,144
    jae .next
    test ah,20h
    jnz .at0
    test ah,08h
    jz .next
    mov si,RDG_HBL
    jmp .found
.at0:
    xor si,si
.found:                                 ; time = now - DX + CX*114 + SI
    push dx
    mov ax,RDG_LINE
    mul cx
    add ax,si
    pop dx
    sub ax,dx
    add ax,[S_NOW]
    mov dx,[S_NOW+2]
    adc dx,0
    mov [S_TSTAT],ax
    mov [S_TSTAT+2],dx
    mov [S_STATT],ax                    ; for rdg_statev's next line: BL's
    mov [S_STATT+2],dx                  ;   mode 0, or none (FFh)
    mov al,0FFh
    cmp si,RDG_HBL
    jne .ln
    mov al,bl
.ln:
    mov [S_STATLN],al
.r:
    ret
.next:
    inc cx
    inc bl
    cmp bl,154
    jb .line
    xor bl,bl
    jmp .line

; --- TIMA overflowed ---------------------------------------------------------
rdg_timaev:
    or byte [0FF0Fh],04h
    mov al,[0FF06h]                     ; TIMA = TMA, from the overflow on
    mov [S_TIMA0],al
    mov ax,[S_TTIMA]
    mov dx,[S_TTIMA+2]
    mov [S_TTBASE],ax
    mov [S_TTBASE+2],dx
    jmp rdg_timasch

; rdg_tshift: CL = log2 of M-cycles per TIMA tick for TAC
rdg_tshift:
    mov cl,[0FF07h]
    and cl,3
    jnz .n
    mov cl,8                            ; 4096 Hz: 256 M-cycles
    ret
.n:
    shl cl,1                            ; 01: 2, 10: 4, 11: 6
    ret

; TIMA ticks off the SAME free-running counter DIV is the top of: once every
; 2^shift M-cycles of (t - S_TDIV), whenever TIMA was written. So with
; P(t) = (t - TDIV) >> shift,  TIMA(now) = TIMA0 + P(now) - P(TTBASE).

; rdg_phase: DX:AX = P(DX:AX). CL = shift. Kept: everything else.
rdg_phase:
    push cx
    sub ax,[S_TDIV]
    sbb dx,[S_TDIV+2]
.sh:
    shr dx,1
    rcr ax,1
    dec cl
    jnz .sh
    pop cx
    ret

; rdg_timasch: S_TTIMA = TDIV + (P(TTBASE) + 256 - TIMA0) << shift, or never
rdg_timasch:
    mov word [S_TTIMA+2],RDG_FAR_HI
    test byte [0FF07h],04h
    jz .r
    call rdg_tshift
    mov ax,[S_TTBASE]
    mov dx,[S_TTBASE+2]
    call rdg_phase
    add ax,256
    adc dx,0
    sub al,[S_TIMA0]
    sbb ah,0
    sbb dx,0
.sh:
    shl ax,1
    rcl dx,1
    dec cl
    jnz .sh
    add ax,[S_TDIV]
    adc dx,[S_TDIV+2]
    mov [S_TTIMA],ax
    mov [S_TTIMA+2],dx
.r:
    ret

; rdg_tima: AL = TIMA at [S_NOW], the timer running
rdg_tima:
    push bx
    push cx
    push dx
    call rdg_tshift
    mov ax,[S_TTBASE]
    mov dx,[S_TTBASE+2]
    call rdg_phase
    mov bx,ax
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    call rdg_phase
    sub ax,bx
    add al,[S_TIMA0]
    pop dx
    pop cx
    pop bx
    ret

; rdg_tbit: CF = 1 if the timer runs and the counter's bit TIMA watches is 1
; now - TIMA ticks when it falls, so a DIV or TAC write that makes it fall
; ticks TIMA too (Mooneye's tim*_div_trigger and rapid_toggle).
rdg_tbit:
    push ax
    push cx
    test byte [0FF07h],04h              ; CF = 0
    jz .r
    call rdg_tshift
    dec cl
    mov ax,[S_NOW]
    sub ax,[S_TDIV]
    shr ax,cl
    shr ax,1
.r:
    pop cx
    pop ax
    ret

; rdg_tinc: one TIMA tick now, overflow and all. TIMA0 is TIMA now.
rdg_tinc:
    inc byte [S_TIMA0]
    jnz .r
    push ax
    mov al,[0FF06h]
    mov [S_TIMA0],al
    or byte [0FF0Fh],04h
    pop ax
.r:
    ret

; rdg_tsettle: TIMA0 = TIMA now, TTBASE = now - before the rate or the
; counter under it changes
rdg_tsettle:
    push ax
    test byte [0FF07h],04h
    jz .s
    call rdg_tima
    mov [0FF05h],al
.s:
    mov al,[0FF05h]
    mov [S_TIMA0],al
    mov ax,[S_NOW]
    mov [S_TTBASE],ax
    mov ax,[S_NOW+2]
    mov [S_TTBASE+2],ax
    pop ax
    ret

; The bits of FF00-FF3F a DMG reads back as 1 (Mooneye's unused_hwio and
; boot_hwio). FF00, FF04-FF07 and FF0F are answered or masked elsewhere.
rdg_iomask:
    db 000h,000h,07Eh,0FFh, 000h,000h,000h,000h, 0FFh,0FFh,0FFh,0FFh, 0FFh,0FFh,0FFh,000h
    db 080h,03Fh,000h,0FFh, 0BFh,0FFh,03Fh,000h, 0FFh,0BFh,07Fh,0FFh, 09Fh,0FFh,0BFh,0FFh
    db 0FFh,000h,000h,0BFh, 000h,000h,070h,0FFh, 0FFh,0FFh,0FFh,0FFh, 0FFh,0FFh,0FFh,0FFh
    db 000h,000h,000h,000h, 000h,000h,000h,000h, 000h,000h,000h,000h, 000h,000h,000h,000h

; =============================================================================
; I/O registers. BX = the address; DS = view; BP = the core's.
; =============================================================================
rdg_ioread:
    call rdg_now
    mov [S_NOW],ax
    mov [S_NOW+2],dx
    cmp bl,44h
    je .ly
    cmp bl,41h
    je .stat
    cmp bl,00h
    je .joy
    cmp bl,04h
    je .div
    cmp bl,05h
    je .tima
    cmp bl,0Fh
    jne .plain
    call rdg_catchup                     ; IF: every event already due, first
.plain:
    mov al,[bx]
    cmp bl,40h                          ; FF00-FF3F: the bits a DMG reads
    jae .pr                             ;   back as 1 - unused bits, unused
    push bx                             ;   registers and the sound
    xor bh,bh                           ;   registers' write-only halves
    or al,[cs:rdg_iomask+bx]
    pop bx
.pr:
    ret
.ly:
    xor al,al
    cmp byte [S_LCDON],0
    je .r
    call rdg_lcdpos
.r:
    ret
.stat:
    mov al,[0FF41h]
    and al,78h
    or al,80h
    cmp byte [S_LCDON],0
    je .r
    mov ah,al
    call rdg_lcdpos                      ; AL = LY, DX = position
    cmp al,[0FF45h]
    jne .s0
    or ah,04h
.s0:
    cmp al,144
    jb .s1
    or ah,1                             ; mode 1
    jmp .s9
.s1:
    cmp dx,20
    jae .s2
    or ah,2                             ; mode 2
    jmp .s9
.s2:
    cmp dx,RDG_HBL
    jae .s9                             ; mode 0
    or ah,3                             ; mode 3
.s9:
    mov al,ah
    ret
.joy:                                   ; 0 = pressed AND selected
    mov ah,[0FF00h]
    mov al,0Fh
    mov dl,[S_JOY]
    test ah,10h                         ; P14 low: the directions
    jnz .j1
    mov dh,dl
    not dh
    and al,dh
.j1:
    test ah,20h                         ; P15 low: the buttons
    jnz .j2
    mov dh,dl
    shr dh,1
    shr dh,1
    shr dh,1
    shr dh,1
    not dh
    and al,dh
.j2:
    and al,0Fh
    and ah,30h
    or al,ah
    or al,0C0h
    ret
.div:
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    sub ax,[S_TDIV]
    sbb dx,[S_TDIV+2]
    mov cl,6
    shr ax,cl
    mov cl,10
    shl dx,cl
    or ax,dx
    ret
.tima:
    test byte [0FF07h],04h
    jz .tstat
    call rdg_tima
    ret
.tstat:
    mov al,[0FF05h]
    ret

rdg_iowrite:
    call rdg_now
    mov [S_NOW],ax
    mov [S_NOW+2],dx
    mov al,[S_WVAL]
    cmp bl,0FFh
    je .ie
    cmp bl,40h
    jae .lcd
    cmp bl,0Fh
    je .if
    cmp bl,00h
    je .joy
    cmp bl,02h
    je .sc
    cmp bl,04h
    je .div
    cmp bl,05h
    je .tima
    cmp bl,07h
    je .tac
    cmp bl,12h
    je .nr2
    cmp bl,14h
    je .nr4
    cmp bl,17h
    je .nr2
    cmp bl,19h
    je .nr4
    cmp bl,26h
    je .nr52
    cmp bl,21h                          ; the noise channel: a square's
    je .nr2                             ;   registers, one along
    cmp bl,23h
    je .nr4
    cmp bl,1Ah
    je .nr30
    cmp bl,1Eh
    je .nr34
.plain:
    mov [bx],al
    ret
; --- the square channels, for the speaker (REALDMG-SPEC section 17)
.nr2:                                   ; NRx2: the DAC off silences at once
    mov [bx],al
    test al,0F8h
    jnz .r2
    call rdg_sqch                        ; SI = the channel
    mov byte [si+SQ_ON],0
.r2:
    ret
.nr4:                                   ; NRx4: bit 7 triggers
    mov [bx],al
    test al,80h
    jz .r2
    call rdg_sqch
    mov ah,[bx-2]                       ; NRx2
    test ah,0F8h
    jz .r2                              ; the DAC off: a trigger does nothing
    mov byte [si+SQ_ON],1
    mov cl,4
    shr ah,cl
    mov [si+SQ_VOL],ah
    xor cx,cx
    mov [si+SQ_ENV],cx
    mov [si+SQ_LACC],cx
    mov byte [si+SQ_LEN],0              ; no length: until something stops it
    test al,40h
    jz .r2
    mov al,[bx-3]                       ; NRx1: 64 - the length, in 256 Hz steps
    and al,3Fh
    neg al
    add al,64
    mov [si+SQ_LEN],al
    ret
.nr52:                                  ; the master switch off: all off
    and al,80h
    or al,70h
    mov [bx],al
    test al,80h
    jnz .r2
    mov byte [S_SQ1+SQ_ON],0
    mov byte [S_SQ2+SQ_ON],0
    mov byte [S_SQ3+SQ_ON],0
    mov byte [S_SQ4+SQ_ON],0
    ret
; --- the wave channel (REALDMG-SPEC section 17.1)
.nr30:                                  ; NR30: its DAC, bit 7 - off silences
    mov [bx],al
    test al,80h
    jnz .r2
    mov byte [S_SQ3+SQ_ON],0
    ret
.nr34:                                  ; NR34: bit 7 triggers, with the DAC
    mov [bx],al                         ;   on; bit 6 a length of 256 - NR31
    test al,80h
    jz .r2
    test byte [0FF1Ah],80h
    jz .r2
    mov byte [S_SQ3+SQ_ON],1
    xor cx,cx
    mov [S_SQ3+SQ_LACC],cx
    mov byte [S_SQ3+SQ_LEN],0
    test al,40h
    jz .r2
    mov al,[0FF1Bh]
    neg al                              ; (256 - n: 0 is 256, kept as 255)
    jnz .wl
    dec al
.wl:
    mov [S_SQ3+SQ_LEN],al
    ret
.if:
    or al,0E0h                          ; (IF's top three read as 1s; the
.ie:                                    ;   rest is IE's)
    mov [bx],al
    mov byte [S_TAKEINT],1
    jmp rdg_force
.joy:
    and al,30h
    or al,0C0h
    mov [bx],al
    ret
.sc:
    mov [bx],al
    and al,81h
    cmp al,81h
    jne .r
    mov al,[0FF01h]                     ; the game sent a byte; nobody is on
    push ax                             ;   the other end, so it reads FF back
    mov bl,[S_SERN]
    xor bh,bh
    mov [RDG_SERBUF+bx],al
    inc byte [S_SERN]
    jnz .sc1
    dec byte [S_SERN]
.sc1:
    pop ax
    call rdg_putc
    mov byte [0FF01h],0FFh
    and byte [0FF02h],7Fh
    or byte [0FF0Fh],08h
    jmp rdg_force
.div:
    call rdg_tsettle
    call rdg_tbit                        ; zeroing the counter drops the bit
    jnc .d1                             ;   TIMA watches: a falling edge, so
    call rdg_tinc                        ;   TIMA ticks
.d1:
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    mov [S_TDIV],ax
    mov [S_TDIV+2],dx
    call rdg_timasch
    jmp rdg_force
.r:
    ret
.tima:
    mov [S_TIMA0],al
    mov [bx],al
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    mov [S_TTBASE],ax
    mov [S_TTBASE+2],dx
    call rdg_timasch
    jmp rdg_force
.tac:
    call rdg_tsettle                     ; TIMA under the old rate, first
    call rdg_tbit                        ; and if the bit TIMA watches was 1
    sbb ah,ah                           ;   and the new TAC's is 0, that is
    or al,0F8h                          ;   a falling edge: TIMA ticks
    mov [0FF07h],al
    or ah,ah
    jz .t1
    call rdg_tbit
    jc .t1
    call rdg_tinc
.t1:
    call rdg_timasch
    jmp rdg_force
.lcd:
    cmp bl,4Ch
    jae .r                              ; nothing on a DMG
    cmp bl,46h
    je .dma
    cmp bl,44h
    je .r                               ; LY is read-only
    cmp bl,41h
    je .stat
    cmp bl,45h
    je .lyc
    call rdg_rlog                        ; a raster write, if mid-frame
    cmp bl,40h
    jne .plain
    ; LCDC: the PPU starts or stops with bit 7
    mov ah,[0FF40h]
    mov [bx],al
    xor ah,al
    test ah,80h
    jz .r
    test al,80h
    jz .off
    mov byte [S_LCDON],1
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    mov [S_TFRAME],ax
    mov [S_TFRAME+2],dx
    add ax,RDG_VBL
    adc dx,0
    mov [S_TVBL],ax
    mov [S_TVBL+2],dx
    call rdg_statsch
    jmp rdg_force
.off:
    mov byte [S_LCDON],0
    mov word [S_TVBL+2],RDG_FAR_HI
    mov word [S_TSTAT+2],RDG_FAR_HI
    jmp rdg_force
.stat:
    and al,78h
    mov ah,[0FF41h]
    and ah,07h
    or al,ah
    mov [bx],al
    call rdg_statsch
    jmp rdg_force
.lyc:
    mov [bx],al
    call rdg_statsch
    jmp rdg_force
.dma:
    mov [bx],al
    push ds
    mov es,[S_VIEW]
    mov di,0FE00h
    mov ah,al
    xor al,al
    mov si,ax                           ; SI = source
    cmp ah,40h
    jb .dv
    cmp ah,80h
    jae .dv
    mov ds,[S_ROMX]
.dv:
    mov cx,160
    rep movsb
    pop ds
    ret

; rdg_rlog: a write to LCDC/SCY/SCX/BGP/OBP0/OBP1/WY/WX, AL = the value, BL =
; the register. If it lands inside the visible lines of a frame, log it with
; the line it takes effect on (plan section 4.4), so the renderer can draw
; the frame in bands; the first such write of a frame first snapshots all
; eight as the frame began.
rdg_rlog:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cl,al                           ; CL = the value, CH = the register
    mov ch,bl
    cmp byte [S_LCDON],0
    je .done
    call rdg_lcdpos                      ; AL = LY, DX = position
    cmp dx,RDG_HBL
    jb .cur
    inc al                              ; written in HBlank: from the next line
.cur:
    cmp al,144
    jae .done
    mov dl,al                           ; DL = the line
    mov al,[S_FRAMES]
    cmp al,[S_SNAPTAG]
    je .app
    mov [S_SNAPTAG],al                  ; this frame's first: the snapshot
    mov byte [S_RLOGN],0
    mov al,[0FF40h]
    mov [RDG_SNAP+0],al
    mov ax,[0FF42h]
    mov [RDG_SNAP+1],ax
    mov ax,[0FF47h]
    mov [RDG_SNAP+3],ax
    mov al,[0FF49h]
    mov [RDG_SNAP+5],al
    mov ax,[0FF4Ah]
    mov [RDG_SNAP+6],ax
.app:
    mov al,[S_RLOGN]
    cmp al,RDG_RLOG_MAX
    jae .done
    inc byte [S_RLOGN]
    xor ah,ah
    shl ax,1
    shl ax,1
    mov si,ax
    mov [RDG_RLOG+si],dl                 ; line
    mov [RDG_RLOG+si+1],ch               ; register
    mov [RDG_RLOG+si+2],cl               ; value
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The cartridge. BX = 0000-7FFF, [S_WVAL] = the byte.
; =============================================================================
rdg_mbcwrite:
    mov al,[S_WVAL]
    mov ah,[S_MBC]
    cmp ah,RDGM_NONE
    je .r
    cmp bh,20h
    jb .ramen
    cmp bh,40h
    jb .rom
    cmp bh,60h
    jb .hi
    cmp ah,RDGM_MBC3
    je rdg_rtclatch
    cmp ah,RDGM_MBC1
    jne .r
    and al,1
    mov [S_MBCMODE],al
    jmp rdg_rammap                       ; mode 1: the high bits pick RAM too
.r:
    ret
.ramen:
    cmp ah,RDGM_MBC2
    jne .re
    test bh,01h                         ; MBC2: address bit 8 picks the bank
    jnz .rom2
.re:
    and al,0Fh
    cmp al,0Ah
    mov al,0
    jne .re1
    inc al
.re1:
    mov [S_RAMEN],al
    ret
.rom:
    cmp ah,RDGM_MBC2
    je .rom2b
    cmp ah,RDGM_MBC5
    je .rom5
    cmp ah,RDGM_MBC3
    je .rom3
    and al,1Fh                          ; MBC1: five bits, 0 means 1
    jnz .r1
    inc al
.r1:
    mov [S_BANK],al
    jmp rdg_setbank
.rom2:
.rom2b:
    and al,0Fh
    jnz .r2
    inc al
.r2:
    mov [S_BANK],al
    jmp rdg_setbank
.rom3:
    and al,7Fh
    jnz .r3
    inc al
.r3:
    mov [S_BANK],al
    jmp rdg_setbank
.rom5:
    cmp bh,30h
    jae .rom5h
    mov [S_BANK],al
    jmp rdg_setbank
.rom5h:
    and al,1
    mov [S_BANKHI],al
    jmp rdg_setbank
.hi:
    cmp ah,RDGM_MBC1
    jne .ram
    and al,3                            ; MBC1: bank bits 5-6 (mode 0)
    mov [S_BANKHI],al
    call rdg_setbank
    jmp rdg_rammap                       ; ...or the RAM bank (mode 1)
.ram:
    mov [S_RAMBANK],al
    jmp rdg_rammap

; =============================================================================
; CARTRIDGE RAM (REALDMG-SPEC section 10.1). A000-BFFF in the view is a
; WINDOW: it holds one 8KB bank, and the host's S_RAMSEG (S_RAMBANKS of them,
; bank 0 first) is where the rest live. A bank switch copies the window out
; to its bank and the new bank in, so a game's reads and writes are plain
; memory - and the recompiler's - at the cost of 16KB moved per switch, which
; games make rarely. With S_RAMSEG 0 (one bank or none) nothing moves. An
; MBC3's clock registers (08h-0Ch written to 4000) are banks too: the window
; is filled with the latched register, and a write the game left in it is
; taken back into the clock when the window changes (rdg_rtccap).
; =============================================================================

; rdg_rammap: the window for S_RAMBANK / S_MBCMODE+S_BANKHI. DS = view.
rdg_rammap:
    cmp word [S_RAMSEG],0
    je .r
    push ax
    mov al,[S_RAMBANK]
    cmp byte [S_MBC],RDGM_MBC1
    jne .n1
    xor al,al                           ; MBC1: bank 0 in mode 0, the high
    cmp byte [S_MBCMODE],0              ;   bits in mode 1
    je .n1
    mov al,[S_BANKHI]
.n1:
    cmp byte [S_MBC],RDGM_MBC3
    jne .bank
    cmp byte [S_RTC+RTC_ON],0
    je .bank
    cmp al,08h
    jb .bank
    cmp al,0Ch
    ja .bank
    or al,80h                           ; 88h-8Ch: a clock register
    jmp short .have
.bank:
    mov ah,[S_RAMBANKS]                 ; a power of two, or 0/1: bank 0
    dec ah
    jns .m
    xor ah,ah
.m:
    and al,ah
.have:
    cmp al,[S_RAMCUR]
    je .p
    call rdg_ramout
    mov [S_RAMCUR],al
    call rdg_ramin
.p:
    pop ax
.r:
    ret

; rdg_ramout: the window back to where it belongs - its bank in S_RAMSEG, or
; a clock register the game wrote. DS = view; S_RAMSEG non-zero. The host
; calls it too, before it saves (rdg_ramsync).
rdg_ramsync:
    cmp word [S_RAMSEG],0
    je rdg_ramin.r
rdg_ramout:
    push ax
    push cx
    push si
    push di
    push es
    mov al,[S_RAMCUR]
    test al,80h
    jz .bank
    call rdg_rtccap
    jmp short .r
.bank:
    call rdg_rambank                     ; ES = the bank
    mov si,0A000h
    xor di,di
    mov cx,1000h
    cld
    rep movsw
.r:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; rdg_ramin: S_RAMCUR into the window. DS = view.
rdg_ramin:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    cld
    mov al,[S_RAMCUR]
    test al,80h
    jz .bank
    and ax,7
    mov si,ax
    mov al,[S_RTC+RTC_LAT+si]           ; (88h-8Ch: LAT+0..4)
    mov ah,al
    push ds
    pop es
    mov di,0A000h
    mov cx,1000h
    rep stosw
    jmp short .p
.bank:
    call rdg_rambank
    push ds
    push es
    pop ds
    pop es
    xor si,si
    mov di,0A000h
    mov cx,1000h
    rep movsw
.p:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
.r:
    ret

; rdg_rambank: AL = a bank, DS = view: ES = its 8KB in S_RAMSEG
rdg_rambank:
    push ax
    push cx
    and ax,0Fh
    mov cl,9
    shl ax,cl                           ; x 200h paragraphs
    add ax,[S_RAMSEG]
    mov es,ax
    pop cx
    pop ax
    ret

; --- the MBC3 clock (REALDMG-SPEC section 10.2) -----------------------------

; rdg_rtccap: a write the game left in a clock register's window becomes the
; register. DS = view, [S_RAMCUR] = 88h-8Ch.
rdg_rtccap:
    push ax
    push bx
    mov bl,[S_RAMCUR]
    and bx,7
    mov al,[0A000h]
    cmp al,[S_RTC+RTC_LAT+bx]
    je .r
    mov [S_RTC+RTC_LAT+bx],al
    mov [S_RTC+RTC_LIVE+bx],al
.r:
    pop bx
    pop ax
    ret

; rdg_rtclatch: AL = a byte written to 6000-7FFF; 00 then 01 latches. DS =
; view.
rdg_rtclatch:
    xchg al,[S_RTC+RTC_LST]
    cmp al,0
    jne .r
    cmp byte [S_RTC+RTC_LST],1
    jne .r
    test byte [S_RAMCUR],80h
    jz .l
    call rdg_rtccap                      ; a write first, then the latch
.l:
    mov ax,[S_RTC+RTC_LIVE]
    mov [S_RTC+RTC_LAT],ax
    mov ax,[S_RTC+RTC_LIVE+2]
    mov [S_RTC+RTC_LAT+2],ax
    mov al,[S_RTC+RTC_LIVE+4]
    mov [S_RTC+RTC_LAT+4],al
    test byte [S_RAMCUR],80h
    jz .r
    jmp rdg_ramin                        ; the register on view is new
.r:
    ret

; rdg_rtcframe: a Game Boy frame of the clock, 1,097/65,536 of a second - the
; clock keeps GAME time, as the cartridge's own crystal would beside a CPU
; running at the game's speed (the host adds the time it was switched off).
rdg_rtcframe:
    cmp byte [S_RTC+RTC_ON],0
    je .r
    add word [S_RTC+RTC_ACC],1097
    jnc .r
    test byte [S_RTC+RTC_LIVE+4],40h    ; halted
    jnz .r
    call rdg_rtcsec
.r:
    ret

; rdg_rtcsec: one second on the live clock. Registers past their range count
; on to 63 (59 for hours, to 31) and wrap without a carry, as the chip does.
rdg_rtcsec:
    push ax
    mov al,[S_RTC+RTC_LIVE+0]
    call .six
    mov [S_RTC+RTC_LIVE+0],al
    jnz .r
    mov al,[S_RTC+RTC_LIVE+1]
    call .six
    mov [S_RTC+RTC_LIVE+1],al
    jnz .r
    mov al,[S_RTC+RTC_LIVE+2]
    inc al
    cmp al,24
    je .day
    and al,1Fh                          ; 24-31 count on and wrap, no carry
    mov [S_RTC+RTC_LIVE+2],al
    jmp short .r
.day:
    mov byte [S_RTC+RTC_LIVE+2],0
    inc byte [S_RTC+RTC_LIVE+3]         ; a day
    jnz .r
    xor byte [S_RTC+RTC_LIVE+4],01h     ; day bit 8
    test byte [S_RTC+RTC_LIVE+4],01h
    jnz .r
    or byte [S_RTC+RTC_LIVE+4],80h      ; day 511 -> 0: the carry
.r:
    pop ax
    ret
.six:                                   ; AL + 1: ZF = it rolled 59 -> 0
    inc al
    cmp al,64
    je .w
    cmp al,60
    jne .nz                             ; (ZF clear)
    xor al,al                           ; ZF set
.nz:
    ret
.w:
    xor al,al                           ; 63 -> 0 without a carry
    cmp al,1                            ; (ZF clear)
    ret

; rdg_rtcadd: DX:AX seconds on the live clock at once - the host's, for the
; time a saved game was switched off. DS = view.
rdg_rtcadd:
    test byte [S_RTC+RTC_LIVE+4],40h    ; halted: it was not counting
    jnz .r
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx,60
    call .div                           ; SI = seconds to add
    mov bx,si
    call .div
    mov bh,0
    push si                             ; minutes
    mov cx,24
    call .div                           ; SI = hours, DX:AX = days
    mov cl,[S_RTC+RTC_LIVE+0]
    add cl,bl
    xor ch,ch                           ; CH = the carry
    cmp cl,60
    jb .s
    sub cl,60
    inc ch
.s:
    mov [S_RTC+RTC_LIVE+0],cl
    pop bx
    mov cl,[S_RTC+RTC_LIVE+1]
    add cl,bl
    add cl,ch
    xor ch,ch
    cmp cl,60
    jb .m
    sub cl,60
    inc ch
.m:
    mov [S_RTC+RTC_LIVE+1],cl
    mov bx,si
    mov cl,[S_RTC+RTC_LIVE+2]
    add cl,bl
    add cl,ch
    xor ch,ch
    cmp cl,24
    jb .h
    sub cl,24
    inc ch
.h:
    mov [S_RTC+RTC_LIVE+2],cl
    mov bl,ch                           ; DX:AX += day + the carry
    xor bh,bh
    add ax,bx
    adc dx,0
    mov bl,[S_RTC+RTC_LIVE+3]
    mov bh,[S_RTC+RTC_LIVE+4]
    and bh,1
    add ax,bx
    adc dx,0
    mov bl,[S_RTC+RTC_LIVE+4]
    or dx,dx
    jnz .over
    cmp ax,512
    jb .d
.over:
    or bl,80h                           ; past day 511: the carry
.d:
    mov [S_RTC+RTC_LIVE+3],al
    and bl,0FEh
    and ah,1
    or bl,ah
    mov [S_RTC+RTC_LIVE+4],bl
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret
.div:                                   ; DX:AX / CX: DX:AX, SI = remainder
    push bx
    mov bx,ax
    mov ax,dx
    xor dx,dx
    div cx
    xchg ax,bx
    div cx
    mov si,dx
    mov dx,bx
    pop bx
    ret

; rdg_setbank: S_ROMX for S_BANK/S_BANKHI, masked to the ROM's size. If the
; game is executing from 4000-7FFF the core's DS (S_SAVDS) moves with it.
rdg_setbank:
    push ax
    push dx
    mov al,[S_BANK]
    mov ah,[S_BANKHI]
    cmp byte [S_MBC],RDGM_MBC1
    jne .m5
    mov dl,ah                           ; MBC1: hi bits are bank bits 5-6
    mov ah,0
    shl dl,1
    shl dl,1
    shl dl,1
    shl dl,1
    shl dl,1
    or al,dl
    jmp .mask
.m5:
    cmp byte [S_MBC],RDGM_MBC5
    je .mask
    xor ah,ah
.mask:
    mov dx,[S_NBANKS]
    dec dx
    and ax,dx
    push bx
    mov bx,ax                           ; ROMX: the bank table's (rdg_init)
    shl bx,1
    mov ax,[S_FRAMES]                   ; the frame it was last switched to,
    mov [RDG_BUSE+bx],ax                 ;   for the cache (MBC2 and MBC1's
    mov ax,[RDG_BTAB+bx]                 ;   high bits come only this way)
    or ax,ax
    jnz .in
    cmp word [cs:rdg_bmiss],0            ; not in memory: the host fetches it
    je .none
    call [cs:rdg_bmiss]
    mov ax,[RDG_BTAB+bx]
    or ax,ax
    jnz .in
.none:
    mov ax,[S_VIEW]                     ; (it could not: bank 0, not garbage)
    sub ax,400h
.in:
    pop bx
    mov [S_ROMX],ax
    mov dx,[S_PC]
    and dh,0C0h
    cmp dh,40h
    jne .r
    mov [S_SAVDS],ax
.r:
    pop dx
    pop ax
    ret

; =============================================================================
; SAVE STATES (REALDMG-SPEC 10.4). The machine is already where a state file
; wants it: the Game Boy's 8000-FFFF at its own addresses in the view, the
; cartridge's RAM banks in S_RAMSEG, and everything else in the state block.
; The host writes those regions as they are; these three build and read back
; the small part, the record at RDG_STREC, which sits just under 8000h so the
; record and the view are one contiguous run. DS = the view, always.
; =============================================================================

; rdg_stram: AX = the bytes of the RAM part a state of this game carries -
; S_RAMSEG's banks, 8KB a bank and one at least (a clock or a battery claims
; a bank of its own), or 0 when the window alone is the RAM. Kept: the rest
rdg_stram:
    xor ax,ax
    cmp word [S_RAMSEG],0
    je .r
    mov al,[S_RAMBANKS]
    or al,al
    jnz .n
    inc ax
.n:
    mov ah,al
    xor al,al
    shl ax,1
    shl ax,1
    shl ax,1
    shl ax,1
    shl ax,1                            ; banks x 8,192
.r:
    ret

; rdg_stcompose: the record at RDG_STREC, from the machine as it stands. The
; host has put the RAM window back in its bank (rdg_ramsync). Kept: all
rdg_stcompose:
    push ax
    push cx
    push si
    push di
    push es
    cld
    push ds
    pop es
    mov di,RDG_STREC
    mov cx,ST_RECSZ / 2
    xor ax,ax
    rep stosw
    mov di,RDG_STREC
    call .head
    mov si,RDG_STATE
    mov di,RDG_STREC+ST_BLOCK
    mov cx,128
    rep movsw
    mov si,RDG_SNAP
    mov cx,8
    rep movsw
    mov si,RDG_RLOG
    mov cx,RDG_RLOG_MAX * 2
    rep movsw
    mov si,S_SQ3                        ; (the wave and noise records: out
    mov cx,8                            ;   of the full state block)
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret
.head:                                  ; ES:DI: what this game's record
    mov ax,'RD'                         ;   opens with - also rdg_stcheck's
    stosw                               ;   to compare (ST_ROMID's end)
    mov ax,'ST'
    stosw
    mov al,ST_VERSION
    stosb
    call rdg_stram
    mov al,ah
    shr al,1
    shr al,1                            ; (KB)
    stosb
    inc di
    inc di
    mov si,0134h
    mov cx,28 / 2
    rep movsw
    ret

; rdg_stcheck: CF = 0 if the record at RDG_STREC is a state of THIS game, at
; this format, with the RAM part this game's state would have. Kept: all
rdg_stcheck:
    push ax
    push cx
    push si
    push di
    push es
    cld
    push ds
    pop es
    mov di,RDG_STREC+ST_RECSZ - 40h      ; (the record's own tail, zeroed: the
    mov cx,20h                          ;   header as this game's is, there)
    xor ax,ax
    rep stosw
    mov di,RDG_STREC+ST_RECSZ - 40h
    push di
    call rdg_stcompose.head
    pop si
    mov di,RDG_STREC
    mov cx,ST_ROMID + 28
    repe cmpsb
    pushf
    mov di,RDG_STREC+ST_RECSZ - 40h      ; the tail zero again
    mov cx,20h
    xor ax,ax
    rep stosw
    popf
    je .ok
    stc
    jmp short .r
.ok:
    clc
.r:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; rdg_stapply: the machine from the record at RDG_STREC - RDG_SNAP and the raster
; log, and from its state block the Game Boy's fields alone (rdg_sttab); the
; host's (segments, vectors, the recompiler's place, the profile, scratch)
; and the buttons held NOW stay as they are. The host has read the view's
; 8000-FFFF and the RAM banks back first. Then what the host derives: the
; bank in 4000-7FFF (rdg_setbank - which may fetch it: rdg_bmiss), and the
; spin-wait's "known not to spin", which was another machine's. Kept: all
rdg_stapply:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cld
    push ds
    pop es
    mov si,RDG_STREC+ST_SNAP
    mov di,RDG_SNAP
    mov cx,8
    rep movsw
    mov si,RDG_STREC+ST_RLOG
    mov di,RDG_RLOG
    mov cx,RDG_RLOG_MAX * 2
    rep movsw
    mov si,RDG_STREC+ST_SQX
    mov di,S_SQ3
    mov cx,8
    rep movsw
    mov bx,rdg_sttab
.f:
    mov al,[cs:bx]                      ; a range: its offset and length
    mov cl,[cs:bx+1]
    or cl,cl
    jz .fixed
    xor ah,ah
    xor ch,ch
    mov si,ax
    add si,RDG_STREC+ST_BLOCK
    mov di,ax
    add di,RDG_STATE
    rep movsb
    add bx,2
    jmp short .f
.fixed:
    mov byte [S_EXIT],0
    mov word [S_IDLENO],0FFFFh
    cmp byte [S_RLOGN],RDG_RLOG_MAX     ; WHAT INDEXES IS NOT TRUSTED: the
    jbe .rl                             ;   log's count, inside the log...
    mov byte [S_RLOGN],RDG_RLOG_MAX
.rl:
    mov al,[S_RAMCUR]                   ; ...and what A000-BFFF holds, a bank
    test al,80h                         ;   the cartridge has (rdg_rammap's
    jz .rb                              ;   mask) or, an MBC3 with the clock,
    cmp byte [S_MBC],RDGM_MBC3          ;   88h-8Ch - else bank 0
    jne .r0
    cmp byte [S_RTC+RTC_ON],0
    je .r0
    cmp al,88h
    jb .r0
    cmp al,8Ch
    jbe .rc
.r0:
    xor al,al
    jmp short .rc
.rb:
    mov ah,[S_RAMBANKS]
    dec ah
    jns .rm
    xor ah,ah
.rm:
    and al,ah
.rc:
    mov [S_RAMCUR],al
    call rdg_setbank
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
; the state block's GAME BOY fields (rdgdefs.inc): a field added to the block
; is the host's until it is listed here, which is the safe way round - one
; that should have been is a state that comes back wrong, and gbstate.py's
; replay says so
rdg_sttab:
    db 00h, 0Eh                         ; AF BC DE HL SP PC BP
    db 30h, 07h                         ; WVAL IME HALT EXIT IDLE BANK BANKHI
                                        ;   (not MBC NBANKS: the cartridge's)
    db 3Ah, 03h                         ; RAMEN RAMBANK MBCMODE
                                        ;   (not S_JOY: the buttons held now)
    db 3Eh, 28h                         ; FRAMES, the clocks, TIMA0 LCDON
                                        ;   SNAPOK RLOGN SERN BADOP
    db 6Eh, 12h                         ; RAMCUR, the MBC3 clock
    db 0ACh, 02h                        ; SNAPTAG TAKEINT
    db 0B4h, 02h                        ; TNF (not BMASK: the cartridge's)
    db 0D4h, 10h                        ; the two square channels
    db 0, 0
