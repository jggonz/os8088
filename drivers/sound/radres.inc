; =============================================================================
; os8088 - drivers/sound/radres.inc  (SOUND.DRV)
;
; The RAD replayer's RESIDENT half (SPEC.md 34.12, 34.12.8): OSAPI_SND_FM verbs
; 4, 5 and 6 up to the far call, the chip and channel claims a tune takes, and
; the teardown thunks. Everything that reads a tune byte or writes an OPL
; register for one is RADPLAY.DRV, which verb 4 puts in the tune's claim.
;
; What lives here and nowhere else, and why it has to:
;   - the refusals a caller gets before any memory is spent (verb 4 steps 1-6)
;   - opl_own / [rad_chip]: FM verbs 0-2 and the tone tier ask them on every
;     call, including inside IRQ0 and snd_tone_req's cli window, where a far
;     call into a claim is not a thing to make
;   - [rad_tick]: SPEC.md 34.13.7's recompute reads it, under cli, and the tick
;     proc tests it before the far call
;
; Label prefix rad_.
; =============================================================================

%include "radabi.inc"

; -----------------------------------------------------------------------------
; rad_attach - SPEC.md 34.13.2: the pacer class, once, at DRVV_ATTACH
; in:  an OPL answered
; out: [rad_class] = 0 tick / 1 RTC
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
rad_attach:
    push ax
    mov byte [rad_ibusy], 0     ; the one place a STRANDED int 70h chain can
                                ; no longer exist: nothing of ours is hooked
                                ; at attach. The byte is cleared by rad_i70x
                                ; alone, and 34.12.8's compaction window and
                                ; 34.13.3's 40-tick detach wait both leave it
                                ; set - after which every IRQ8 takes step 0's
                                ; nested path and no RTC-class tune in this
                                ; image ever advances again. The driver's bss
                                ; ships inside its image, so without this a
                                ; remount would inherit it. NOT at rad_hook's
                                ; 0 -> 1 arm, where a chain suspended in the
                                ; ROM's sti may still be live and clearing the
                                ; byte would let a fresh IRQ8 nest inside it
    mov byte [rad_class], 0
    call OSAPI_CPU_INFO         ; AL = CPU_*
    cmp al, CPU_8086
    je .out                     ; an XT: the tick, and no RTC port is read
    pushf
    cli
    mov al, 0x8A                ; register A, NMI masked
    out 0x70, al
    in al, 0x71
    mov ah, al
    mov al, 0x8B                ; register B
    out 0x70, al
    in al, 0x71
    xchg al, ah                 ; AL = A, AH = B
    push ax
    mov al, 0x0D                ; the index harmless, NMI back on
    out 0x70, al
    in al, 0x71
    pop ax
    popf
    and al, 0x7F
    cmp al, 0x26                ; the 32.768 kHz divider at 1,024 Hz
    jne .out
    test ah, 0x70               ; ...and no RTC interrupt enabled at rest
    jnz .out
    mov byte [rad_class], 1
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; rad_ready - DRVV_READY: bank the system volume's driver folder (34.12.8)
; -----------------------------------------------------------------------------
rad_ready:
    push bx
    push dx
    call OSAPI_FILE_HERE        ; DX = the directory, BL = its drive
    mov [rad_scwd], dx
    mov [rad_svol], bl
    pop dx
    pop bx
    ret

; =============================================================================
; The verbs. Entered from opl_fm_op with AL = 4..6 and every register as the
; caller left it but DH (the requester) and ES (KERNEL_SEG); DS = ours.
; Out: CF = 0 and the verb's answer, then snd_tick_ans's DX; CF = 1 and AL =
; RADE_*, AH = the RADC_* detail for RADE_CORRUPT and 0 for every other code -
; written HERE, once, so no refusal path can hand back the caller's AH or its
; own arithmetic's. BX, DX, SI, DI, BP, DS and ES preserved; CX an output where
; the verb says so (SPEC.md 34.12).
; =============================================================================
rad_verbs:
    cmp word [snd_services+DSV_FM], 0
    jne .have
    xor ax, ax                  ; RADE_NOSINK: no OPL answered
    stc
    ret
.have:
    cmp al, SNDFM_RADLOAD
    jne .v5
    call rad_load
    jmp short .ans
.v5:
    cmp al, SNDFM_RADPLAY
    jne .v6
    call rad_play
    jmp short .ans
.v6:
    cmp al, SNDFM_RADSTAT
    jne .bad
    call rad_stat
.ans:
    jc .refused
    jmp snd_tick_ans            ; SPEC.md 34.13.7: verbs 4-6 are sites too
.bad:                           ; (a tail call: DX = the tick proc, CF kept)
    mov al, RADE_BADARG
.refused:
    cmp al, RADE_CORRUPT        ; AH is a detail for CORRUPT alone
    je .keep
    mov ah, 0
.keep:
    stc
    ret

; -----------------------------------------------------------------------------
; rad_load - verb 4 (SPEC.md 34.12.1)
; in:  BX:SI -> the file, CX = its length, DH = the requester
; out: CF = 0 AL = version, AH = whole Hz, CX = tenths
;      CF = 1 AL = RADE_*, AH = RADC_* (CORRUPT), CX = offset (CORRUPT)
; -----------------------------------------------------------------------------
rad_load:
    push bx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, si                  ; 1. SI + CX past the segment
    add ax, cx
    jnc .s2
    jz .s2                      ; exactly 10000h is legal
    mov al, RADE_BADARG
    jmp .early
.s2:
    cmp cx, 17                  ; 2. the signature
    jb .notrad
    mov es, bx
    push cx
    push si
    mov di, si
    mov si, rad_sig
    mov cx, 16
    cld
    repe cmpsb
    pop si
    pop cx
    jne .notrad
    mov al, [es:si+0x10]        ; 3. the version
    cmp al, 0x10
    je .s4
    cmp al, 0x21
    je .s4
    mov al, RADE_VERSION
    jmp .early
.notrad:
    mov al, RADE_NOTRAD
    jmp .early
.s4:
    cmp cx, RAD_MAXLEN          ; 4. the length
    jbe .s5
    mov al, RADE_BIG
    jmp .early
.s5:
    cmp al, 0x21                ; 5. a 2.1 tune on an OPL2
    jne .s6
    cmp byte [opl_is3], 0
    jne .s6
    mov al, RADE_NEEDOPL3
    jmp .early
.s6:
    call rad_lock               ; 6. busy, and the verb lock taken
    jnc .s7
    mov al, RADE_BUSY
    jmp .early
.s7:
    mov [rad_nreq], dh          ; UNDER THE LOCK, with the other working copies
    mov [rad_ncx], cx           ; (34.12.1): steps 8 and 9 yield, and a second
    mov [rad_nsi], si           ; instance's verb 4 that is about to be refused
    mov [rad_nbx], bx           ; RADE_BUSY at step 6 must not reach this byte
                                ; first and have .commit stamp ITS id on the
                                ; lock holder's tune
%ifdef RADLOG
    call rad_logclaim
%endif
    mov ax, cx                  ; 7. the claim
    add ax, RAD_WORK + 1023
    mov cl, 10
    shr ax, cl
    mov bp, ax                  ; BP = KB
    call OSAPI_MEM_CLAIM_HI     ; owner = our segment: drv_release sweeps it
    jnc .s8
    mov al, RADE_NOMEM
    jmp .unlock
.s8:
    mov [rad_nseg], dx
    mov es, dx
    cmp word [rad_seg], 0       ; 8. the replayer
    je .disk
    push ds
    mov ds, [rad_seg]           ; a replace: the image is already in memory
    xor si, si
    xor di, di
    mov cx, RAD_TUNE
    cld
    rep movsb
    pop ds
    jmp short .check
.disk:
    call rad_read
    jc .noplayer
.check:
    call rad_check
    jnc .s9
.noplayer:
    mov al, RADE_NOPLAYER
    jmp .free
.s9:
    push ds                     ; 9. the file into the claim...
    mov cx, [rad_ncx]
    mov si, [rad_nsi]
    mov ds, [rad_nbx]
    mov di, RAD_TUNE
    cld
    rep movsb
    pop ds
    mov al, [rad_class]         ; ...the block the overlay reads...
    mov [es:RO_CLASS], al
    mov al, [opl_is3]
    mov [es:RO_IS3], al
    mov [es:RO_RSEG], cs
    mov word [es:RO_RSVC], rad_svc
    mov ax, bp
    mov cl, 10
    shl ax, cl                  ; KB x 1024 (0 at 64KB: the stack top wraps)
    mov [es:RO_TOP], ax
    mov ax, [rad_logseg]
    mov [es:RO_LOGSEG], ax
    mov ax, [rad_ncx]
    mov [es:RO_LEN], ax
    mov dx, es                  ; ...and validated there, and parsed
    mov al, RADV_LOAD
    call rad_callseg
    jc .free
    mov [rad_nver], al
    mov [rad_nrate], cx
    cmp word [rad_seg], 0       ; accepted: the old tune goes now
    je .commit
    call rad_unload
.commit:
    mov es, [rad_nseg]
    mov ax, es
    mov bx, [es:6]
    mov dl, [rad_nreq]
    pushf
    cli
    mov [rad_seg], ax
    mov [rad_far+2], ax
    mov [rad_ent], bx
    mov [rad_owner], dl
    popf
    call rad_unlock
    mov cx, [rad_nrate]
    mov ax, cx
    mov bl, 10
    div bl                      ; AL = whole Hz
    mov ah, al
    mov al, [rad_nver]
    clc
    jmp short .out
.free:
    push ax
    push dx
    mov dx, [rad_nseg]
    call OSAPI_MEM_FREE
    pop dx
    pop ax
.unlock:
    call rad_unlock
    cmp al, RADE_CORRUPT
    je .early
    mov cx, [rad_ncx]           ; CX is an output for CORRUPT alone
.early:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rad_read - RADPLAY.DRV off the system volume into ES:0 (SPEC.md 34.12.8)
; out: CF = 0 read
; -----------------------------------------------------------------------------
rad_read:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_FILE_HERE        ; where the USER is, to be put back
    push dx
    push bx
    mov dx, [rad_scwd]
    mov bl, [rad_svol]
    call OSAPI_FILE_GOTO
    jc .back
    xor bx, bx                  ; ES:BX = the claim's head
    mov cx, RAD_TUNE
    xor dx, dx
    mov si, rad_name
    call OSAPI_FILE_READ        ; DX:AX = the image's size
    jc .back
    or dx, dx
    jnz .short
    cmp ax, RO_END
    jae .back                   ; (CF = 0)
.short:
    stc
.back:
    pop bx
    pop dx
    pushf                       ; the answer survives the way back
    call OSAPI_FILE_GOTO
    popf
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rad_check - is ES:0 a RADPLAY.DRV of this vintage? (hd_tool_check's tests)
; out: CF = 0 yes
; -----------------------------------------------------------------------------
rad_check:
    cmp word [es:0], 0x384F     ; 'O8'
    jne .no
    cmp byte [es:2], 4
    jne .no
    cmp byte [es:3], DRVC_OVL
    jne .no
    cmp word [es:4], 0
    jne .no
    cmp byte [es:PKG_DISP], 0xFF
    jne .no
    cmp byte [es:PKG_DISP+1], 0xD5
    jne .no
    cmp byte [es:PKG_DISP+2], 0xCB
    jne .no
    cmp word [es:10], RAD_ABI_VER
    jne .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; rad_callseg - one far call into an overlay
; in:  DX = its segment, AL = RADV_*; the verb's own registers (ES:DI, CX)
; out: the verb's; BX, BP and DS preserved
;
; The far pointer is built on the STACK, never in a shared word: verb 6 is an
; any-task verb and a replace names a claim that is not [rad_seg] yet, so two
; callers must not be able to aim one another's call.
; -----------------------------------------------------------------------------
rad_callseg:
    ; STKBALANCE-OK: a constructed far CALL - the return address (CS:.back)
    ; and the dispatcher's far pointer are pushed and `retf` jumps; the overlay's
    ; own `retf` consumes the return address, so .back is reached at +3
    push bx
    push bp
    push ds
    mov ds, dx
.stamp:                         ; (tests/radmove.py's negative control)
    mov [RO_RSEG], cs           ; where the resident is NOW (34.12.8)
    mov bp, [6]                 ; the entry, out of its own header
    push cs                     ; the return address...
    mov bx, .back
    push bx
    push dx                     ; ...and the dispatcher, as a far "return"
    mov bx, PKG_DISP
    push bx
    retf
.back:
    pop ds
    pop bp
    pop bx
    ret

; -----------------------------------------------------------------------------
; rad_lock / rad_unlock - the verb lock (verbs 4 and 5, and an owner's unload)
; in:  DH = the requester
; out: rad_lock CF = 1 busy - another instance holds channels 0..7 or a tune,
;      or any verb 4/5 is running
; -----------------------------------------------------------------------------
rad_lock:
    push ax
    push bx
    pushf
    cli
    cmp byte [rad_lk], 0xFF
    jne .busy
    xor bx, bx
.own:
    mov al, [opl_own+bx]
    cmp al, 0xFF
    je .next
    cmp al, dh
    jne .busy
.next:
    inc bx
    cmp bx, 8
    jb .own
    cmp word [rad_seg], 0
    je .take
    cmp [rad_owner], dh
    jne .busy
.take:
    mov [rad_lk], dh
    popf
    pop bx
    pop ax
    clc
    ret
.busy:
    popf
    pop bx
    pop ax
    stc
    ret

rad_unlock:
    mov byte [rad_lk], 0xFF
    ret

; -----------------------------------------------------------------------------
; rad_unload - stop the tune (voluntary), give the chip back, free the claim
; in:  [rad_seg] names one; the caller's IF
; -----------------------------------------------------------------------------
rad_unload:
    push ax
    push dx
    mov byte [rad_tick], 0      ; the tick proc stops calling in first
    mov dx, [rad_seg]
    mov al, RADV_STOP
    call rad_callseg
    call rad_chipoff
    pushf
    cli
    mov word [rad_seg], 0
    mov byte [rad_owner], 0xFF
    popf
    call OSAPI_MEM_FREE
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rad_kill - the involuntary end: DSV_RELINST for the owner, and detach
; in:  IF = 0, [rad_seg] names one
; -----------------------------------------------------------------------------
rad_kill:
    push ax
    push dx
    mov byte [rad_tick], 0
    mov dx, [rad_seg]
    mov al, RADV_KILL
    call rad_callseg
    call rad_chipoff
    mov word [rad_seg], 0
    mov byte [rad_owner], 0xFF
    call OSAPI_MEM_FREE
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rad_chipoff - channels 0..7 back from the tune's owner, and the chip byte
; -----------------------------------------------------------------------------
rad_chipoff:
    push ax
    push bx
    pushf
    cli
    cmp byte [rad_chip], 0
    je .out
    mov al, [rad_owner]
    xor bx, bx
.ch:
    cmp [opl_own+bx], al
    jne .next
    mov byte [opl_own+bx], 0xFF
.next:
    inc bx
    cmp bx, 8
    jb .ch
    mov byte [rad_chip], 0
.out:
    popf
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rad_play - verb 5 (SPEC.md 34.12.2)
; in:  AH = RADP_*, DH = the requester
; out: CF = 0; CF = 1 AL = RADE_NOTUNE / BUSY / PACER / BADARG
; -----------------------------------------------------------------------------
rad_play:
    push bx
    push dx
    cmp word [rad_seg], 0
    je .notune
    cmp [rad_owner], dh
    jne .notune
    cmp ah, RADP_STOP
    ja .badarg
    call rad_lock
    jc .busy
    cmp ah, RADP_START
    jne .notstart
    mov byte [rad_tick], 0      ; step 0: a restart's pacer disarmed FIRST,
    push dx                     ; in the overlay's own cli window, so no HALT
    mov dx, [rad_seg]           ; can give the claim below away (34.12.2)
    mov al, RADV_DISARM
    call rad_callseg
    pop dx
    pushf                       ; step 1: channels 0..7 and the chip, RE-READ
    cli                         ; here - a HALT before the disarm freed them,
    xor bx, bx                  ; and another instance may have taken one
.free:                          ; since rad_lock looked
    mov al, [opl_own+bx]
    cmp al, 0xFF
    je .fnext
    cmp al, dh
    je .fnext
    popf
    call rad_tickset
    call rad_unlock
    jmp .busy
.fnext:
    inc bx
    cmp bx, 8
    jb .free
    xor bx, bx
.claim:
    mov [opl_own+bx], dh
    inc bx
    cmp bx, 8
    jb .claim
    mov byte [rad_chip], 1
    popf
    mov dx, [rad_seg]           ; (DH was the requester until here)
    mov al, RADV_START
    call rad_callseg
    jnc .ticked
    call rad_chipoff            ; RADE_PACER: the overlay undid the sequence
    jmp short .refused
.notstart:
    mov dx, [rad_seg]
    cmp ah, RADP_STOP
    jne .pr
    mov byte [rad_tick], 0
    mov al, RADV_STOP
    call rad_callseg
    call rad_chipoff
    jmp short .ticked
.pr:
    mov al, RADV_PAUSE
    cmp ah, RADP_PAUSE
    je .call
    mov al, RADV_RESUME
.call:
    mov byte [rad_tick], 0
    call rad_callseg
    jc .refused
.ticked:
    call rad_tickset
    call rad_unlock
    clc
    jmp short .out
.refused:
    call rad_tickset
    call rad_unlock
    stc
    jmp short .out
.notune:
    mov al, RADE_NOTUNE
    stc
    jmp short .out
.badarg:
    mov al, RADE_BADARG
    stc
    jmp short .out
.busy:
    mov al, RADE_BUSY
    stc
.out:
    pop dx
    pop bx
    ret

; rad_tickset - [rad_tick] = a tick-class tune is PLAYING (SPEC.md 34.13.7)
rad_tickset:
    push ax
    push es
    pushf                       ; the state read and the byte written in one
    cli                         ; window: a HALT's clear is never overwritten
    xor al, al
    cmp byte [rad_class], 0
    jne .set
    cmp word [rad_seg], 0
    je .set
    mov es, [rad_seg]
    cmp byte [es:RO_STATE], 2
    jne .set
    inc al
.set:
    mov [rad_tick], al
    popf
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; rad_stat - verb 6 (SPEC.md 34.12.7): any task, no lock
; in:  BX:DI -> the buffer, CX = its length, DH = the requester
; out: CF = 0 CX = bytes written; CF = 1 AL = RADE_BADARG / RADE_NOTUNE
; -----------------------------------------------------------------------------
rad_stat:
    push dx
    push es
    push cx                     ; a refusal hands CX back as it came
    push ax
    cmp cx, RAD_ST_LEN
    jbe .n
    mov cx, RAD_ST_LEN
.n:
    mov ax, di
    add ax, cx
    pop ax
    jnc .inseg
    jz .inseg
    mov al, RADE_BADARG
    jmp short .refuse
.inseg:
    pushf                       ; the tune test, the segment read and the copy
    cli                         ; in ONE window (34.12.7): the owner's UI task
    cmp word [rad_seg], 0       ; can free the claim, and a worker preempted
    je .notune                  ; between a test and the far call would jump
    cmp [rad_owner], dh         ; into a block that is no longer the tune's
    jne .notune
    mov dx, [rad_seg]
    mov es, bx
    push ax
    mov al, RADV_STAT
    call rad_callseg            ; rd_stat runs whole at IF = 0 too
    pop ax
    popf
    add sp, 2                   ; CX = bytes written: the pushed copy dropped
    clc
    jmp short .out
.notune:
    popf
    mov al, RADE_NOTUNE
.refuse:
    pop cx
    stc
.out:
    pop es
    pop dx
    ret

; -----------------------------------------------------------------------------
; rad_svc - the overlay's service call back into us (RADS_*)
; in:  AL = RADS_*; DS = the claim; IF = 0 (the pacer)
; -----------------------------------------------------------------------------
rad_svc:
    cmp al, RADS_HOOK
    je rad_hook
    cmp al, RADS_UNHOOK
    je rad_unhook
    push ds
    push cs
    pop ds
    cmp al, RADS_HALTED
    jne .out
    mov byte [rad_tick], 0      ; the tick proc stops calling in; the kernel's
    call rad_chipoff            ; cell follows at the owner's next FM verb
.out:
    pop ds
    ret

; -----------------------------------------------------------------------------
; rad_hook / rad_unhook - RADS_HOOK / RADS_UNHOOK: int 70h is the RESIDENT's
;                         (SPEC.md 34.13.3). IF = 0, inside the overlay's
;                         arm or disarm window
; rad_hook in:  DS = the claim, BX = its handler, CX = its post-chain entry
;          out: BX = rad_i70ch, CX = rad_i70x (this segment: RO_RSEG)
; -----------------------------------------------------------------------------
rad_hook:
    push ax
    push es
    mov [cs:rad_ient], bx
    mov [cs:rad_ient+2], ds
    mov [cs:rad_iback], cx
    mov [cs:rad_iback+2], ds
    xor ax, ax
    mov es, ax
    cmp byte [cs:rad_iarm], 0   ; already ours: the saved vector is not
    jne .set                    ; overwritten with our own entry
    mov ax, [es:0x1C0]
    mov [cs:rad_old70], ax
    mov ax, [es:0x1C2]
    mov [cs:rad_old70+2], ax
    mov word [es:0x1C0], rad_i70
    mov [es:0x1C2], cs
    mov byte [cs:rad_iarm], 1
.set:
    mov bx, rad_i70ch
    mov cx, rad_i70x
    pop es
    pop ax
    ret

rad_unhook:
    cmp byte [cs:rad_iarm], 0
    je .out
    push ax
    push es
    xor ax, ax
    mov es, ax
    mov ax, [cs:rad_old70]
    mov [es:0x1C0], ax
    mov ax, [cs:rad_old70+2]
    mov [es:0x1C2], ax
    mov byte [cs:rad_iarm], 0
    pop es
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; rad_i70 - int 70h while an RTC-class tune is armed: SPEC.md 34.13.3's
;           resident half. Step 0 here; the claim steps; the BIOS chain is
;           made from rad_i70ch so its return address is never in a claim
; -----------------------------------------------------------------------------
rad_i70:
    cmp byte [cs:rad_ibusy], 0  ; step 0, before anything is pushed
    je .enter
    push ax                     ; nested inside a BIOS chain: acknowledge,
    mov al, 0x8C                ; never step, never touch the private stack
    out 0x70, al
    in al, 0x71
    mov al, 0x0D
    out 0x70, al
    in al, 0x71
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    pop ax
    iret
.enter:
    mov byte [cs:rad_ibusy], 1
    push ds                     ; the image may have moved since the claim
    mov ds, [cs:rad_ient+2]     ; last saw it (34.12.8): its RO_RSEG is
.stamp:                         ; stamped here, the 2 bytes popped before
    mov [RO_RSEG], cs           ; (tests/radmove.py: .stamp)
    pop ds                      ; the jump
    jmp far [cs:rad_ient]       ; the claim's handler; nothing left pushed

; rad_i70ch - far-JUMPED from the claim's steps 1 and 2 with every register
; and SS:SP the interrupted ones, busy still set
rad_i70ch:
    pushf
    call far [cs:rad_old70]
    cmp byte [cs:rad_iarm], 0   ; IF = 0 again: disarmed while the BIOS ran (a
    je rad_i70x                 ; task switch inside its sti) - enter no claim
    jmp far [cs:rad_iback]      ; the armed claim's post-chain entry

; rad_i70x - the claim's step 6 far-jumps here: busy cleared, back
rad_i70x:
    mov byte [cs:rad_ibusy], 0
    iret

%ifdef RADLOG
; rad_logclaim - the RADLOG buffer, once (a -DRADLOG build only, SPEC.md 96.8)
rad_logclaim:
    cmp word [rad_logseg], 0
    jne .out
    push ax
    push dx
    push es
    mov ax, RAD_LOGKB
    call OSAPI_MEM_CLAIM_HI
    jc .no
    mov [rad_logseg], dx
    mov es, dx
    mov word [es:0], 0
.no:
    pop es
    pop dx
    pop ax
.out:
    ret
%endif

; --- state --------------------------------------------------------------------
rad_sig:     db 'RAD by REALiTY!!'
rad_name:    db 'RADPLAY.DRV', 0
rad_far:     dw PKG_DISP, 0     ; the overlay's dispatcher, for the tick path
rad_owner:   db 0xFF            ; the instance holding the tune
rad_lk:      db 0xFF            ; the requester inside verb 4/5, FFh = none
rad_ibusy:   db 0               ; int 70h: a handler is running or chaining
rad_iarm:    db 0               ; int 70h: rad_i70 is installed
rad_old70:   dd 0               ; ...and what it replaced
rad_ient:    dd 0               ; the armed claim's handler
rad_iback:   dd 0               ; ...and its post-chain entry
