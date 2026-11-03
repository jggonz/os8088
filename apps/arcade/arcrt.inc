; =============================================================================
; os8088 - apps/arcade/arcrt.inc - the runtime inside a translated code part
; (ARCADE-SPEC 3.3 and 3.4)
;
; Assembled into every code part arcaot.py writes, straight after the PC
; table, so its header lands at ARC_HDR. The host far-calls arc_enter; the
; translated code comes back through arc_poll/arc_miss/arc_halt, which
; restore the host's stack and RETF.
;
; INTERRUPTS ARE OFF while translated code runs. SS is the view, and an IRQ
; would push its frame below the 8080's SP into the board's RAM; a run is
; bounded by its poll budget instead, about a millisecond (the host sizes
; it), and the host runs with interrupts on between runs - RealDMG's rule
; (REALDMG-SPEC 5.2). Every CALL made in here (vmark, in3) pushes below the
; 8080's SP, which is dead space by the 8080's own definition.
; =============================================================================

    times ARC_HDR - ($ - $$) db 0
    dw ARC_MAGIC
    dw arc_enter
    dw arc_rom
    dw arc_rom_end - arc_rom
    dw arc_code_end

; -----------------------------------------------------------------------------
; arc_enter - far call. AX = the view's segment, DX = aux's. Runs the guest
; from ST_PC until ST_BUD poll points pass, or a miss or a HLT; returns with
; ST_* written and DS = the view. Every register but SS:SP clobbered.
; -----------------------------------------------------------------------------
arc_enter:
    cli
    mov ds, ax
    mov es, dx
    mov [ST_HSS], ss
    mov [ST_HSP], sp
    mov ss, ax                      ; (mov ss holds off interrupts for the
    mov sp, [ST_SP]                 ;   next instruction; IF is 0 anyway)
    mov cx, [ST_BC]
    mov dx, [ST_DE]
    mov bx, [ST_HL]
    mov bp, [ST_BUD]
    mov di, [es:AUX_LPTR]           ; DI: the write list's next free word,
    mov si, [ST_PC]                 ;   for every run (ARCADE-SPEC 4.1)
    shl si, 1
    mov ax, [ST_AF]
    sahf                            ; the 8080's flags, last: nothing after
    jmp word [cs:si]                ;   this touches FLAGS

; -----------------------------------------------------------------------------
; arc_poll / arc_poll_ah - a poll point's budget ran out. Called with the
; 8080 PC as the dw after the CALL; _ah when the point saved the live flags
; in AH itself (its DEC BP has clobbered FLAGS since).
; -----------------------------------------------------------------------------
arc_poll:
    lahf
arc_poll_ah:
    ; STKBALANCE-OK: never returns to its caller - the CALL only carried the
    ; 8080 PC after it; SP comes back from ST_HSP and the RETF is the host's
    pop si
    mov si, [cs:si]
    mov byte [ST_WHY], ARCX_POLL
arc_save:
    mov [ST_PC], si
    mov [ST_AF], ax
    mov [ST_BC], cx
    mov [ST_DE], dx
    mov [ST_HL], bx
    mov [ST_SP], sp
    mov [ST_LEFT], bp
    mov [es:AUX_LPTR], di
    mov ss, [ST_HSS]
    mov sp, [ST_HSP]
    retf

; arc_miss - the table sent us to a PC nothing translated: SI = PC x 2.
arc_miss:
    lahf
    shr si, 1
    mov byte [ST_WHY], ARCX_MISS
    jmp arc_save

; arc_halt - HLT: SI = the PC after it. The host waits for an interrupt.
arc_halt:
    lahf
    mov byte [ST_WHY], ARCX_HALT
    jmp arc_save

; -----------------------------------------------------------------------------
; arc_in3 - IN 3: the shift register read at its offset. AL = the byte, AH
; clobbered (scratch), the flags kept (IN sets none).
; -----------------------------------------------------------------------------
arc_in3:
    pushf
    push cx
    mov cl, [V_SHOFF]
    and cl, 7
    mov si, [V_SHLO]
    shl si, cl
    mov ax, si
    mov al, ah
    pop cx
    popf
    ret
