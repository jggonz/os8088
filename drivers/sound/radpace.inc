; =============================================================================
; os8088 - drivers/sound/radpace.inc  (RADPLAY.DRV)
;
; The pacers (SPEC.md 34.13): the tick class inside IRQ0's DSV_TICK, and the
; RTC class on the CMOS clock's periodic interrupt (int 70h). Both step at
; IF = 0 on the claim's private stack, entered SPEC.md 34.12.5's way: the busy
; byte tested before anything is pushed, SS:SP swapped with no register, and
; never an `sti`.
; =============================================================================

; -----------------------------------------------------------------------------
; rp_frame - one frame of whichever version, the frame marker, the counter
; in:  DS = SS = the claim, IF = 0
; out: CF = 1 the frame halted the tune (2.1 deviation 5); [rd_notes] = its
;      note plays (0 on 1.0)
; clobbers: AX, BX, DX, ES
; -----------------------------------------------------------------------------
rp_frame:
    cmp byte [rd_ver], 0x21
    jne .v1
    call r2_update
    jmp short .end
.v1:
    call r1_frame
    mov byte [rd_notes], 0
.end:
%ifdef RADSLOW
    call rp_slow
%endif
%ifdef RADLOG
    push bx
    mov bx, 0xFFFF
    call rd_logm
    pop bx
%endif
    add word [rd_st+RST_FRAMES], 1
    adc word [rd_st+RST_FRAMES+2], 0
    cmp byte [rd_halted], 1
    cmc                         ; CF = 1 exactly when [rd_halted] >= 1
.exit:                          ; (tests/radcost.py's breakpoint)
    ret

%ifdef RADSLOW
; -----------------------------------------------------------------------------
; rp_slow - tests/radrtc.py's heavy frame: a 4 ms busy wait at IF = 0, timed on
; PIT channel 0 (counter-latch reads; mode 3 counts down by 2 a clock, so 4 ms
; is 4,773 clocks and 9,546 counts, and the reload is a modular wrap)
; -----------------------------------------------------------------------------
rp_slow:
    push ax
    push bx
    push cx
    push dx
    call .read
    mov bx, ax                  ; BX = the previous reading
    xor dx, dx                  ; DX = counts elapsed
.lp:
    call .read
    mov cx, bx
    sub cx, ax
    mov bx, ax
    add dx, cx
    cmp dx, 9546
    jb .lp
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.read:
    mov al, 0x00                ; latch channel 0
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    ret
%endif

; rp_sec - one more second played, saturating
rp_sec:
    cmp word [rd_st+RST_SECS], 0xFFFF
    je .out
    inc word [rd_st+RST_SECS]
.out:
    ret

; rd_status_pos - the position half of the status block, both versions
rd_status_pos:
    push ax
    push bx
    mov al, [rd_order]
    mov [rd_st+RST_ORDER], al
    mov al, [rd_ordlen]
    mov [rd_st+RST_ORDLEN], al
    mov al, [rd_line]
    mov [rd_st+RST_LINE], al
    mov al, [rd_speed]
    mov [rd_st+RST_SPEED], al
    mov al, 0xFF
    mov bl, [rd_order]
    cmp bl, [rd_ordlen]
    jae .patt
    xor bh, bh
    add bx, [rd_ordlist]
    mov al, [bx]
    test al, 0x80
    jz .patt
    and al, 0x7F
    xor ah, ah
    mov bx, [rd_ordlist]
    add bx, ax
    mov al, [bx]
    and al, 0x7F
.patt:
    mov [rd_st+RST_PATT], al
    pop bx
    pop ax
    ret

; rd_parse - the version's parse
rd_parse:
    cmp byte [rd_ver], 0x21
    jne .v1
    jmp r2_parse
.v1:
    jmp r1_parse

; =============================================================================
; Arm and disarm (SPEC.md 34.13.3)
; =============================================================================

; -----------------------------------------------------------------------------
; rp_arm - start the pacer
; in:  DS = the claim
; out: CF = 0 armed; CF = 1 the RTC's interrupt is somebody else's (RADE_PACER)
; -----------------------------------------------------------------------------
rp_arm:
    push ax
    push bx
    push cx
    push es
    mov word [rp_acc], 0
    mov byte [rp_tnotes], 0
    cmp byte [ro_class], 1
    je .rtc
    mov byte [rp_armed], 1      ; tick class: rp_tick tests this and nothing
    clc                         ; else is hooked
    jmp .out
.rtc:
    xor bx, bx
    mov es, bx
    pushf
    cli
    mov al, 0x8B                ; step 1: register B, NMI masked
    out 0x70, al
    in al, 0x71
    mov ah, al
    test ah, 0x30
    jnz .refuse                 ; an alarm or update interrupt: not ours
    test ah, 0x40
    jz .take
    test byte [es:0x4A0], 1     ; PIE on: only the BIOS wait may own it
    jz .refuse
.take:
    push ax                     ; step 2: the vector, installed as the
    mov bx, rd_isr70            ; RESIDENT's entry - its BIOS chain must
    mov cx, RD_I70BACK          ; return into SOUND.DRV, never into a claim
    mov al, RADS_HOOK           ; (34.13.3)
    call rp_svc
    mov [rp_rch], bx            ; the offsets only: the segment is loaded
    mov [rp_rx], cx             ; from RO_RSEG at each jump (34.12.8)
    pop ax
    mov al, 0x8C                ; step 3: a stale flag read away
    out 0x70, al
    in al, 0x71
    mov al, 0x8B                ; step 4: PIE on, every other bit as read
    out 0x70, al
    mov al, ah
    or al, 0x40
    out 0x71, al
    in al, 0xA1                 ; step 5: the PIC masks, bits remembered
    mov bl, al
    and bl, 0x01
    and al, 0xFE
    out 0xA1, al
    in al, 0x21
    mov bh, al
    and bh, 0x04
    and al, 0xFB
    out 0x21, al
    or bl, bh
    mov [rp_mask], bl
    mov al, 0x0D                ; step 6: the index harmless, NMI back on
    out 0x70, al
    in al, 0x71
    xor ax, ax
    mov [rp_n], ax
    mov [rp_exp], ax
    mov [rp_frac], ax
    mov [rp_frac+2], ax
    mov [rp_nocred], al
    mov ax, [es:0x46C]
    mov [rp_lastt], ax
    mov byte [rp_armed], 1
    popf
    clc
    jmp short .out
.refuse:
    mov al, 0x0D
    out 0x70, al
    in al, 0x71
    popf
    stc
.out:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_svc - a service call into the resident (RADS_*): its dispatcher at
;          RO_RSEG:PKG_DISP, BP = its rad_svc. Every caller is at IF = 0, so
;          the shared far pointer is never written by two at once
; in:  AL = RADS_*, the rest per service; DS = the claim
; -----------------------------------------------------------------------------
rp_svc:
    push bp
    push ax
    mov ax, [ro_rseg]
    mov [rd_far+2], ax
    mov word [rd_far], PKG_DISP
    pop ax
    mov bp, [ro_rsvc]
    call far [rd_far]
    pop bp
    ret

; -----------------------------------------------------------------------------
; rp_disarm - stop the pacer; nothing when it is not armed
; in:  DS = the claim (CS in the handler's HALT)
; -----------------------------------------------------------------------------
rp_disarm:
    cmp byte [rp_armed], 0
    je .ret
    cmp byte [ro_class], 1
    je .rtc
    mov byte [rp_armed], 0      ; THE TICK CLASS: this byte is what stops a
    ret                         ; frame - rp_tick reads it and nothing is
                                ; hooked - so clearing it is the whole disarm
.rtc:                           ; THE RTC CLASS: what stops a frame there is
    push ax                     ; the VECTOR, given back by RADS_UNHOOK below,
    push bx                     ; and the byte is cleared in that same cli
    push es
    xor bx, bx
    mov es, bx
    pushf
    cli
    mov byte [rp_armed], 0      ; with rd_stat's read of it (34.12.7) and
                                ; rp_arm's write, both at IF = 0
    test byte [es:0x4A0], 1
    jnz .vec                    ; a BIOS wait owns PIE and IRQ8: leave both
    mov al, 0x8B
    out 0x70, al
    in al, 0x71
    and al, 0xBF
    mov ah, al
    mov al, 0x8B
    out 0x70, al
    mov al, ah
    out 0x71, al
    mov al, 0x8C
    out 0x70, al
    in al, 0x71
    test byte [rp_mask], 0x01
    jz .m2
    in al, 0xA1
    or al, 0x01
    out 0xA1, al
.m2:
    test byte [rp_mask], 0x04
    jz .vec
    in al, 0x21
    or al, 0x04
    out 0x21, al
.vec:
    mov al, RADS_UNHOOK         ; the resident's vector back, and its armed
    call rp_svc                 ; byte cleared in this same window
    mov al, 0x0D
    out 0x70, al
    in al, 0x71
    popf
    pop es
    pop bx
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; rp_pie - SPEC.md 34.12.7: re-assert PIE if something cleared it. IF = 0
; -----------------------------------------------------------------------------
rp_pie:
    push ax
    mov al, 0x8B
    out 0x70, al
    in al, 0x71
    test al, 0x40
    jnz .ok
    or al, 0x40
    mov ah, al
    mov al, 0x8B
    out 0x70, al
    mov al, ah
    out 0x71, al
    mov al, 0x8C
    out 0x70, al
    in al, 0x71
.ok:
    mov al, 0x0D
    out 0x70, al
    in al, 0x71
    pop ax
    ret

; =============================================================================
; rp_tick - RADV_TICK: the tick class, inside IRQ0 (SPEC.md 34.13.5)
; in:  IF = 0, DS = the resident's; AX, DX free (snd_tick banks the rest)
; =============================================================================
rp_tick:
    cmp byte [cs:rp_armed], 1
    jne .ret
    cmp byte [cs:ro_class], 0
    jne .ret
    cmp byte [cs:rp_busy], 0
    jne .ret
    mov byte [cs:rp_busy], 1
    mov [cs:rp_oss], ss
    mov [cs:rp_osp], sp
    mov ss, [cs:rp_seg]
    mov sp, [cs:ro_top]
    push bx
    push cx
    push si
    push di
    push bp
    push ds
    push es
    push cs
    pop ds
    mov ax, [rp_acc]
    add ax, [rd_rate]
    mov byte [rp_tnotes], 0
    xor cx, cx                  ; CL = frames run
.lp:
    cmp ax, 182
    jb .end
    cmp cl, RAD_FRMAX
    jae .end
    cmp byte [rp_tnotes], RAD_PNMAX
    jae .end
    sub ax, 182
    push ax
    push cx
    call rp_frame
    pop cx
    pop ax
    jc .halt
    inc cl
    mov dl, [rd_notes]
    add [rp_tnotes], dl
    jmp .lp
.halt:
    call rd_halt
    jmp short .out
.end:
    cmp ax, 182
    jb .acc
    mov ax, 181                 ; plays slow, never queues
.acc:
    mov [rp_acc], ax
    cmp cl, [rd_st+RST_BURST]
    jbe .secs
    mov [rd_st+RST_BURST], cl
.secs:
    mov ax, [rp_secsn]
    add ax, 2000
    cmp ax, 36413
    jb .s1
    sub ax, 36413
    call rp_sec
.s1:
    mov [rp_secsn], ax
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    mov ss, [cs:rp_oss]
    mov sp, [cs:rp_osp]
    mov byte [cs:rp_busy], 0
.ret:
    ret

; =============================================================================
; rd_isr70 - the RTC periodic interrupt (SPEC.md 34.13.3), the claim's half:
;            far-JUMPED from SOUND.DRV's rad_i70 with the busy byte (the
;            resident's) already set and nothing pushed
; =============================================================================
rd_isr70:
    mov [cs:rp_oss], ss
    mov [cs:rp_osp], sp
    mov ss, [cs:rp_seg]
    mov sp, [cs:ro_top]
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    push cs
    pop ds
    mov al, 0x8B                ; step 1: an alarm or update interrupt on?
    out 0x70, al
    in al, 0x71
    test al, 0x30
    jnz .chain
    xor bx, bx                  ; step 2: a BIOS int 15h wait running?
    mov es, bx
    test byte [es:0x4A0], 1
    jnz .chain
    mov al, 0x8C                ; step 3: register C
    out 0x70, al
    in al, 0x71
    mov ah, al
    mov al, 0x20                ; steps 3 and 4: EOI, slave then master
    out 0xA0, al
    out 0x20, al
    test ah, 0x40
    jnz .step5
    mov al, 0x0D                ; spurious: step 6, uncounted
    out 0x70, al
    in al, 0x71
    jmp .step6
.chain:
    mov al, 0x0D                ; the index parked and NMI back on before the
    out 0x70, al                ; BIOS runs (34.13.4): a disarmed return from
    in al, 0x71                 ; it never comes back here to do it
    mov ax, [ro_rseg]           ; the resident where it is NOW: rad_i70 has
    mov [rp_rch+2], ax          ; just stamped it (34.12.8)
    pop es                      ; steps 1 and 2: the BIOS on the stack it
    pop ds                      ; expects, with the private stack left empty
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov ss, [cs:rp_oss]
    mov sp, [cs:rp_osp]
    jmp far [cs:rp_rch]         ; the resident chains: its return address is
                                ; SOUND.DRV's, and it comes back below only
.back:                          ; while a claim is armed (34.13.3)
RD_I70BACK equ .back            ; ...named for rp_arm's RADS_HOOK
    mov [cs:rp_oss], ss         ; re-saved: the chain may have nested
    mov [cs:rp_osp], sp
    mov ss, [cs:rp_seg]
    mov sp, [cs:ro_top]
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    push cs
    pop ds
    call rp_pie                 ; step 2's: the BIOS may have cleared PIE
.step5:
    mov al, 0x0D
    out 0x70, al
    in al, 0x71
    add word [rp_nirq], 1       ; the gate's counters (34.13.3): interrupts
    adc word [rp_nirq+2], 0     ; that reached this step...
    cmp word [rp_n], 0xFFFF     ; a counted periodic interrupt
    je .tk
    inc word [rp_n]
.tk:
    xor cx, cx                  ; CX = the credit
    xor bx, bx
    mov es, bx
    mov ax, [es:0x46C]
    mov dx, ax
    sub dx, [rp_lastt]          ; d
    jz .k
    mov [rp_lastt], ax
    mov byte [rp_tnotes], 0
    cmp byte [rp_nocred], 0
    jne .reset
    cmp dx, 18
    ja .reset
    push dx                     ; exp += 56 d
    mov ax, 56
    mul dx
    add [rp_exp], ax
    pop dx
    push dx                     ; frac += 290,672 d = 4:6F70h x d
    mov ax, 0x6F70
    mul dx
    add [rp_frac], ax
    adc [rp_frac+2], dx
    pop dx
    mov ax, 4
    mul dx
    add [rp_frac+2], ax
.fl:
    cmp word [rp_frac+2], 0x12  ; while frac >= 1,193,182 (12:34DEh)
    jb .fd
    ja .fs
    cmp word [rp_frac], 0x34DE
    jb .fd
.fs:
    sub word [rp_frac], 0x34DE
    sbb word [rp_frac+2], 0x12
    inc word [rp_exp]
    jmp .fl
.fd:
    mov ax, [rp_exp]
    cmp ax, [rp_n]
    jbe .n2
    mov cx, ax                  ; periods never delivered
    sub cx, [rp_n]
    mov [rp_n], ax
.n2:
    mov ax, [rp_exp]
    add ax, 57
    cmp [rp_n], ax
    jbe .n3
    mov ax, [rp_exp]            ; the two crystals, re-aligned
    mov [rp_n], ax
.n3:
    mov ax, [rp_n]
    cmp ax, [rp_exp]
    jbe .min
    mov ax, [rp_exp]
.min:
    sub [rp_n], ax
    sub [rp_exp], ax
    jmp short .k
.reset:
    xor ax, ax
    mov [rp_exp], ax
    mov [rp_n], ax
    mov [rp_frac], ax
    mov [rp_frac+2], ax
    mov [rp_nocred], al
.k:
%ifdef RADNOCREDIT
    xor cx, cx                  ; tests/radrtc.py's negative control: the tick
%endif                          ; discipline computed and never credited
    inc cx                      ; k = 1 + credit
    mov ax, [rp_secsn]          ; seconds: 1,024 periodic interrupts
    add ax, cx
    cmp ax, 1024
    jb .s2
    sub ax, 1024
    call rp_sec
.s2:
    mov [rp_secsn], ax
    mov ax, [rd_rate]           ; t = acc + rate x k, a dword
    xor dx, dx
    cmp cx, 1                   ; k = 1 on all but ~18 interrupts a second:
    je .k1                      ; an add, not a 16 x 16 mul (34.13.6)
    mul cx
.k1:
    add ax, [rp_acc]
    adc dx, 0
    xor cx, cx                  ; CL = frames run
.fr:
    or dx, dx
    jnz .one
    cmp ax, 10240
    jb .fdone
.one:
    cmp cl, RAD_FRMAX
    jae .fdone
    cmp byte [rp_tnotes], RAD_PNMAX
    jae .fdone
    sub ax, 10240
    sbb dx, 0
    push ax
    push dx
    push cx
    call rp_frame
    pop cx
    pop dx
    pop ax
    jc .halt
    inc cl
    mov bl, [rd_notes]
    add [rp_tnotes], bl
    jmp .fr
.halt:
    call rd_halt
    jmp short .step6
.fdone:
    or dx, dx
    jnz .cap
    cmp ax, 10240
    jb .setacc
.cap:
    mov ax, 10239
.setacc:
    mov [rp_acc], ax
    or cl, cl                   ; ...and those that ran a frame
    jz .nost
    add word [rp_nstep], 1
    adc word [rp_nstep+2], 0
.nost:
    cmp byte [rp_tnotes], RAD_PNMAX
    jb .burst
    mov byte [rp_nocred], 1     ; this tick's work reached the budget
.burst:
    cmp cl, [rd_st+RST_BURST]
    jbe .step6
    mov [rd_st+RST_BURST], cl
.step6:
    mov ax, [ro_rseg]           ; the exit's segment, loaded now (34.12.8)
    mov [rp_rx+2], ax
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov ss, [cs:rp_oss]
    mov sp, [cs:rp_osp]
    jmp far [cs:rp_rx]          ; the resident clears its busy byte and irets
