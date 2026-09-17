; =============================================================================
; os8088 - RADPLAY.DRV
;
; The RAD replayer: SOUND.DRV's on-demand half (SPEC.md 34.12, 34.12.8). The
; validator, both replay engines, the register shadow and both pacers live
; here, and they are read off the system volume by OSAPI_SND_FM verb 4 into
; the tune's own claim and freed with it - so a sound machine that never opens
; a tune carries none of it (docs/RADBOX-PLAN.md 3.2's size rule; the measured
; sizes are SPEC.md 34.12.8's).
;
; **IT IS NOT A DRIVER.** HDDTOOL.DRV's shape (SPEC.md 52.11): the driver
; header and dispatcher, class DRVC_OVL, RAD_ABI_VER in the spare word, and no
; row in drv_tab. SOUND.DRV owns it and is the only thing that calls it.
;
; ONE CLAIM, ONE SEGMENT. The claim is laid out
;
;   +0          this image (code, tables) and its working set (zero at load)
;   +RAD_TUNE   the tune, copied in by the resident before RADV_LOAD
;   end-1024    the private stack the replay step runs on
;
; so CS = DS = SS = the claim inside a replay frame, every field of the
; replayer's state is an absolute address, and every offset the engine keeps
; into the tune is a claim offset - of which 0 is never one, and means "none".
;
; What runs where:
;   RADV_LOAD, _START, _PAUSE, _RESUME, _STOP, _STAT  a verb, in the caller's
;       task at its IF, on its stack, DS = the claim (the resident sets it)
;   RADV_KILL   DSV_RELINST / detach, at IF = 0, on the dying task's stack
;   RADV_TICK   inside IRQ0 at IF = 0 - swaps to the private stack
;   rd_isr70    the RTC periodic interrupt's stepping half - far-jumped from
;               SOUND.DRV's rad_i70, which owns int 70h, the busy byte and
;               the BIOS chain (34.13.3); swaps likewise
;
; Label prefixes, a STATED EXCEPTION to the rad_ family (SPEC.md 34.12.8):
; this image is assembled on its own, so nothing here can collide with a
; driver or kernel label, and it keeps one prefix per part - rd_ the verbs
; and plumbing, rv_ the validator, r2_ / r1_ the two engines, rp_ the pacers.
; =============================================================================

%include "os88drv.inc"
%include "radabi.inc"

    OS88_OVERLAY 'RAD player', RAD_ABI_VER, rd_entry

%if ($ - $$) != RO_STATE
    %error "radplay: the overlay header is not 32 bytes"
%endif
ro_state:   db 0                ; RO_STATE - see radabi.inc
ro_chip:    db 0                ; RO_CHIP
ro_class:   db 0                ; RO_CLASS  (the resident writes these six
ro_is3:     db 0                ; RO_IS3     before RADV_LOAD, in the claim)
ro_rseg:    dw 0                ; RO_RSEG
ro_rsvc:    dw 0                ; RO_RSVC
ro_top:     dw 0                ; RO_TOP
ro_logseg:  dw 0                ; RO_LOGSEG
ro_len:     dw 0                ; RO_LEN
%if ($ - $$) != RO_END
    %error "radplay: the RO_* block moved"
%endif

; -----------------------------------------------------------------------------
; rd_entry - the dispatcher's landing site
; in:  AL = RADV_*; DS = the claim (RADV_TICK: DS = the resident's)
; out: per verb
; -----------------------------------------------------------------------------
rd_entry:
    cmp al, RADV_TICK
    je rp_tick
    cmp al, RADV_STAT
    je rd_stat
    cmp al, RADV_KILL
    je rd_kill
    cmp al, RADV_LOAD
    je rd_load
    cmp al, RADV_START
    je rd_start
    cmp al, RADV_PAUSE
    je rd_pause
    cmp al, RADV_RESUME
    je rd_resume
    cmp al, RADV_STOP
    je rd_stop
    cmp al, RADV_DISARM
    je rd_disarm
    stc                         ; a verb from a newer resident than this image
    ret

; The signature again, on this side of the copy - see rd_load (SPEC.md 34.12.1)
rd_sig:     db 'RAD by REALiTY!!'

; =============================================================================
; The verbs
; =============================================================================

; -----------------------------------------------------------------------------
; rd_load - RADV_LOAD: validate the copy and parse it (SPEC.md 34.12.1 step 6)
; in:  DS = the claim, the RO_* block filled, the file at RAD_TUNE
; out: CF = 0: AL = version, CX = rate in tenths, [ro_state] = 1
;      CF = 1: AL = RADE_CORRUPT, AH = RADC_*, CX = the file offset
; clobbers: AH on success, flags
; -----------------------------------------------------------------------------
rd_load:
    push bx
    push dx
    push si
    push di
    push es
    cld
    push ds
    pop es
    mov di, rd_ws               ; the working set: all of it zero, whatever
    mov cx, RAD_TUNE            ; the image read or copy left there
    sub cx, di
    xor al, al
    rep stosb
    mov byte [ro_state], 0
    mov byte [ro_chip], 0
    mov [rp_seg], ds
    ; Steps 2, 3 and 5 AGAIN, on the COPY (SPEC.md 34.12.1). The resident
    ; proved them through ES:BX on the CALLER'S buffer, and steps 8 and 9
    ; yield for hundreds of ms, so a caller whose own worker rewrites that
    ; buffer in the window could otherwise turn 10h into 21h and reach the
    ; 2.1 engine on a chip that answered OPL2 - which D1 refuses outright.
    ; The answers are byte-identical to the resident's: it passes a
    ; non-CORRUPT AL through with CX restored and zeroes AH.
    mov si, RAD_TUNE
    mov di, rd_sig
    mov cx, 16
    repe cmpsb                  ; ES = DS here, both are the claim
    mov al, RADE_NOTRAD
    jne .refuse                 ; neither mov nor pop touched ZF
    mov al, [RAD_TUNE+0x10]
    cmp al, 0x10
    je .copyok
    cmp al, 0x21
    jne .badver
    cmp byte [ro_is3], 0
    jne .copyok
    mov al, RADE_NEEDOPL3
    jmp short .refuse
.badver:
    mov al, RADE_VERSION
.refuse:
    stc
    jmp short .out
.copyok:
    mov cx, [ro_len]
    call rv_valid               ; SPEC.md 96.4.4 against the bytes in the claim
    jc .out
    mov [rd_ver], al
    call rd_parse
    mov byte [ro_state], 1
    mov byte [rd_st+RST_STATE], 1
    mov al, [rd_ver]
    mov [rd_st+RST_VER], al
    mov al, [ro_class]
    mov [rd_st+RST_PACER], al
    call rd_ratecalc            ; AX = tenths (SPEC.md 34.13.1)
    mov [rd_rate], ax
    mov [rd_st+RST_RATE], ax
    mov cx, ax
    mov al, [rd_ver]
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_ratecalc - the tune's frame rate in tenths of a Hz (SPEC.md 34.13.1)
; out: AX
; -----------------------------------------------------------------------------
rd_ratecalc:
    mov al, [RAD_TUNE+0x11]
    test al, 0x40
    jz .noslow
    mov ax, 182
    ret
.noslow:
    cmp byte [rd_ver], 0x21
    jne .fifty
    test al, 0x20
    jz .fifty
    mov ax, [RAD_TUNE+0x12]     ; BPM 46..300 (validated)
    shl ax, 1
    shl ax, 1
    ret
.fifty:
    mov ax, 500
    ret

; -----------------------------------------------------------------------------
; rd_start - RADV_START (SPEC.md 34.12.2 steps 0, 2 and 3)
; in:  DS = the claim; the resident has claimed channels 0..7 and set its chip
;      byte (step 1)
; out: CF = 0; CF = 1 AL = RADE_PACER, the stop sequence written
; -----------------------------------------------------------------------------
rd_start:
    push bx
    push cx
    push dx
    push si
    push di
    call rp_disarm              ; step 0: a restart writes no stop sequence
    mov byte [rd_fast], 0
    call rd_rezero
%ifdef RADLOG
    call rd_log_reset
%endif
    mov byte [rd_phase], 0
    call rd_startseq            ; step 2, unfiltered, initialising the shadow
%ifdef RADLOG
    mov bx, 0xFFFE
    call rd_logm
%endif
    mov byte [rd_phase], 1
    mov byte [rd_fast], 1       ; frames run at IF = 0: opl_wrf (34.11.2)
    mov byte [ro_chip], 1
    and byte [rd_st+RST_FLAGS], ~(RSTF_HALTED | RSTF_LOOPED)
    pushf                       ; step 3, and "playing" set in the SAME window:
    cli                         ; a HALT after it must not be overwritten by a
    mov byte [ro_state], 2      ; late "playing" (34.12.2's windows)
    mov byte [rd_st+RST_STATE], 2
    call rp_arm
    jnc .armed
    mov byte [ro_state], 1      ; refused: stopped in the SAME window, so no
    mov byte [rd_st+RST_STATE], 1   ; verb 6 between here and the stop
    popf                        ; sequence reads a tune that never started as
    jmp short .pacer            ; playing (34.12.2); ro_chip is the sequence's
.armed:
    popf
    clc
    jmp short .out
.pacer:
    call rd_stopseq_slow        ; steps 1-2 undone; the state set stopped
    mov al, RADE_PACER
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_stop - RADV_STOP: disarm, stop sequence, default patch, rewind (34.12.2)
; -----------------------------------------------------------------------------
rd_stop:
    pushf                       ; the state read and the pacer disarmed in ONE
    cli                         ; window (34.12.2): once disarmed nothing can
    cmp byte [ro_state], 1      ; HALT, so the sequence below runs at the
    jbe .none                   ; caller's IF on a tune that is still ours
    call rp_disarm
    popf
    call rd_stopseq_slow
    clc
    ret
.none:
    popf                        ; stopped (a HALT included): CF = 0, no write
    clc
    ret

; -----------------------------------------------------------------------------
; rd_disarm - RADV_DISARM: the pacer off inside one cli window (34.12.2 start)
; -----------------------------------------------------------------------------
rd_disarm:
    pushf
    cli
    call rp_disarm
    popf
    clc
    ret

; the voluntary end: FFFDh, the stop sequence and the patch through the
; slow writer at the caller's IF, then stopped and rewound
rd_stopseq_slow:
    mov byte [rd_fast], 0
    ; fall through
; ...and the shared body; rd_fast chooses the writer
rd_stopseq:
    push bx
%ifdef RADLOG
    mov bx, 0xFFFD
    call rd_logm
%endif
    mov byte [rd_phase], 0
    cmp byte [rd_ver], 0x21
    jne .v1
    call r2_body                ; the 2.1 stop: start less its leading 105h,
    mov bx, 0x104               ; then 104h <- 00h while NEW is still 1, then
    xor al, al                  ; NEW off (SPEC.md 96.4.6, 34.11.2)
    call rd_set
    mov bx, 0x105
    call rd_set
    jmp short .patch
.v1:
    call r1_endplayer           ; 20h..F5h <- 00h
    cmp byte [ro_is3], 0
    je .patch
    mov bx, 0x105               ; OPL2 mode on an OPL3 (34.11.3); a 1.0 tune
    xor al, al                  ; never set NEW, so 104h is not written
    call rd_set
.patch:
    call rd_patch               ; the default patch, nine channels (34.12.3)
    mov byte [ro_chip], 0
    mov byte [ro_state], 1
    mov byte [rd_st+RST_STATE], 1
    call rd_rewind
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_pause / rd_resume - RADV_PAUSE / RADV_RESUME (SPEC.md 34.12.2)
; -----------------------------------------------------------------------------
rd_pause:
    pushf                       ; read, disarm and mark paused in ONE window
    cli                         ; (34.12.2): a HALT cannot follow the disarm,
    cmp byte [ro_state], 2      ; and one before it leaves the state 1 this
    jne .none                   ; reads
    call rp_disarm
    mov byte [ro_state], 3
    mov byte [rd_st+RST_STATE], 3
    popf
    mov byte [rd_fast], 0
    call rd_keysoff             ; every keyed channel, through the shadow
    clc
    ret
.none:
    popf
    clc
    ret

rd_resume:
    pushf                       ; read, mark playing, arm: ONE window, so a
    cli                         ; HALT the first frame takes is never
    cmp byte [ro_state], 3      ; overwritten by a late "playing" (34.12.2)
    jne .none
    mov byte [rd_fast], 1       ; frames again: opl_wrf
    mov byte [ro_state], 2
    mov byte [rd_st+RST_STATE], 2
    call rp_arm
    jc .refuse
.none:
    popf
    clc
    ret
.refuse:
    mov byte [ro_state], 3      ; still paused, in the same window
    mov byte [rd_st+RST_STATE], 3
    mov byte [rd_fast], 0
    popf
    mov al, RADE_PACER
    stc
    ret

; -----------------------------------------------------------------------------
; rd_kill - RADV_KILL: DSV_RELINST or detach, at IF = 0 (SPEC.md 34.12.4)
; out: CF = 0; the chip in OPL2 mode with the default patch, the pacer unhooked
; -----------------------------------------------------------------------------
rd_kill:
    cmp byte [ro_chip], 0
    je .done
    push bx
    call rp_disarm
    mov byte [rd_fast], 1       ; IF = 0 here: opl_wrf (34.11.2)
    call rd_keysoff
    cmp byte [rd_ver], 0x21
    jne .nonew
    mov bx, 0x104
    xor al, al
    call rd_set
.nonew:
    cmp byte [ro_is3], 0
    je .nothree
    mov bx, 0x105
    xor al, al
    call rd_set
.nothree:
    call rd_patch
    mov byte [ro_chip], 0
    mov byte [ro_state], 1
    mov byte [rd_st+RST_STATE], 1
    pop bx
.done:
    clc
    ret

; -----------------------------------------------------------------------------
; rd_halt - SPEC.md 34.13.3 step 5's HALT, at interrupt time on the private
;           stack: the stop sequence and the patch through opl_wrf, RSTF_HALTED,
;           and the resident told to release channels 0..7 and the chip
; -----------------------------------------------------------------------------
rd_halt:
    push ax
    call rp_disarm
    mov byte [rd_fast], 1
    call rd_stopseq
    or byte [rd_st+RST_FLAGS], RSTF_HALTED
    mov al, RADS_HALTED         ; the resident gives channels 0..7 and the
    call rp_svc                 ; chip back
    pop ax
.exit:                          ; tests/radcost.py's bracket
    ret

; -----------------------------------------------------------------------------
; rd_stat - RADV_STAT: the status block, atomically (SPEC.md 34.12.7)
; in:  DS = the claim, ES:DI -> the caller's buffer, CX = bytes (<= RAD_ST_LEN)
; out: CF = 0
; -----------------------------------------------------------------------------
rd_stat:
    push ax
    push cx
    push si
    push di
    cld
    pushf
    cli
    cmp byte [ro_class], 1
    jne .copy
    cmp byte [ro_state], 2
    jne .copy
    cmp byte [rp_armed], 1      ; ...and only while OUR vector is hooked: a
    jne .copy                   ; PIE set on a refused start is a stranger's
    call rp_pie                 ; the RTC class: PIE re-asserted (34.13.4)
.copy:
    mov al, [rd_st+RST_FLAGS]
    and al, ~(RSTF_OPL3 | RSTF_SLOW | RSTF_CHIP)
    cmp byte [ro_is3], 0
    je .f1
    or al, RSTF_OPL3
.f1:
    test byte [RAD_TUNE+0x11], 0x40
    jz .f2
    or al, RSTF_SLOW
.f2:
    cmp byte [ro_chip], 0
    je .f3
    or al, RSTF_CHIP
.f3:
    mov [rd_st+RST_FLAGS], al
    cmp byte [ro_state], 0      ; the position and channel halves, built now
    je .blk                     ; rather than every frame
    cmp byte [rd_ver], 0x21
    jne .s1
    call r2_status
    jmp short .blk
.s1:
    call r1_status
.blk:
    mov si, rd_st
    rep movsb
    mov byte [rd_st+RST_BURST], 0
    popf
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret

; =============================================================================
; Engine plumbing shared by both versions
; =============================================================================

; -----------------------------------------------------------------------------
; rd_rezero - SPEC.md 34.12.2 step 0: the replay state back to its at-load
;             values (96.4.5), and the status block's play fields with it
; -----------------------------------------------------------------------------
rd_rezero:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov di, rd_eng              ; every engine field, both versions
    mov cx, rd_eng_end - rd_eng
    xor al, al
    rep stosb
    mov di, rd_st + RST_ORDER   ; ...and the status block's play half
    mov cx, RAD_ST_LEN - RST_ORDER
    rep stosb
    pop es
    mov al, [RAD_TUNE+0x11]
    and al, 0x1F
    mov [rd_speed], al
    mov al, [rd_ordlen]
    mov [rd_st+RST_ORDLEN], al
    mov word [rp_acc], 0
    mov word [rp_secsn], 0
    pop di
    pop cx
    pop ax
    ret

; rd_rewind - what a stop leaves in the status block: order 0, line 0
rd_rewind:
    mov byte [rd_st+RST_ORDER], 0
    mov byte [rd_st+RST_LINE], 0
    ret

; -----------------------------------------------------------------------------
; rd_startseq - the version's start sequence (SPEC.md 96.4.6), through rd_set
; -----------------------------------------------------------------------------
rd_startseq:
    push bx
    cmp byte [rd_ver], 0x21
    jne .v1
    mov bx, 0x105               ; deviation 1: NEW on first
    mov al, 1
    call rd_set
    call r2_body
    pop bx
    ret
.v1:
    call r1_start
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_keysoff - key off every channel whose shadowed B0h (and on 2.1 1B0h) has
;              bit 5 set, through the shadow (pause, kill)
; -----------------------------------------------------------------------------
rd_keysoff:
    push ax
    push bx
    mov bx, 0xB0
.lp:
    mov al, [rd_shadow+bx]
    test al, 0x20
    jz .next
    and al, 0xDF
    call rd_set
.next:
    inc bx
    cmp bl, 0xB9
    jb .lp
    cmp byte [rd_ver], 0x21
    jne .out
    cmp bh, 0
    jne .out
    mov bx, 0x1B0
    jmp .lp
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_patch - the default patch on all nine channels (SPEC.md 34.12.3): 99
;            writes, straight to the ports (not through the shadow: the next
;            start rewrites every register the shadow names), not logged
; -----------------------------------------------------------------------------
rd_patch:
    push ax
    push bx
    push cx
    push dx
    push si
    xor cx, cx                  ; CL = channel
.chan:
    mov bx, cx
    mov dl, [cs:rd_slotoff+bx]  ; the modulator slot
    mov si, rd_defpatch
    mov dh, 2
.op:
    xor bx, bx
.reg:
    push bx
    mov al, [cs:rd_opregs+bx]
    add al, dl
    mov bl, al
    xor bh, bh
    mov al, [cs:si]
    inc si
    call rd_out
    pop bx
    inc bx
    cmp bx, 5
    jb .reg
    add dl, 3
    dec dh
    jnz .op
    mov bl, 0xC0
    add bl, cl
    xor bh, bh
    mov al, [cs:si]
    call rd_out
    inc cl
    cmp cl, 9
    jb .chan
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The writers (SPEC.md 34.11.2, 34.12.6)
; =============================================================================

; -----------------------------------------------------------------------------
; rd_set - one register write through the shadow
; in:  BX = register 000h..1FFh, AL = value; DS = the claim
; out: nothing; clobbers nothing (flags)
;
; A frame's write whose value the shadow already holds is not sent ([rd_phase]
; = 1); the start and stop sequences are sent whole ([rd_phase] = 0). Either
; way the shadow is what the 2.1 engine reads back (34.12.6).
; -----------------------------------------------------------------------------
rd_set:
    cmp byte [rd_phase], 0
    je .send
    cmp [rd_shadow+bx], al
    je rd_out.ret
.send:
    mov [rd_shadow+bx], al
%ifdef RADLOG
    call rd_log
%endif
    ; fall through
; -----------------------------------------------------------------------------
; rd_out - the port write itself: opl_wrf when [rd_fast], else opl_wr's timing
; in:  BX = register 000h..1FFh, AL = value
; -----------------------------------------------------------------------------
rd_out:
    cmp byte [rd_fast], 0
    je .slow
    push dx                     ; opl_wrf: index out, data out, nothing else
    mov dx, 0x388
    or bh, bh
    jz .fport
    mov dl, 0x8A                ; 100h..1FFh: 38Ah/38Bh
.fport:
    xchg al, bl                 ; AL = the register, BL = the value
    out dx, al
    inc dx
    xchg al, bl                 ; ...and back
    out dx, al
    pop dx
.ret:
    ret
.slow:                          ; opl_wr / opl_wr2: select, 6 counted status
    push ax                     ; reads at 388h, data, then 35 more at the
    push cx                     ; caller's IF (SPEC.md 34.1). AH = the value
    push dx
    mov dx, 0x388
    or bh, bh
    jz .sport
    mov dl, 0x8A
.sport:
    mov ah, al
    pushf
    cli
    mov al, bl
    out dx, al
    push dx
    mov dx, 0x388
    mov cx, 6
.a:
    in al, dx
    loop .a
    pop dx
    inc dx
    mov al, ah
    out dx, al
    popf
    mov dx, 0x388
    mov cx, 35
.b:
    in al, dx
    loop .b
    pop dx
    pop cx
    pop ax
    ret

%ifdef RADLOG
; -----------------------------------------------------------------------------
; rd_log / rd_logm - one .RLG record into the RADLOG buffer (SPEC.md 96.8)
; in:  BX = register (or FFFEh/FFFFh/FFFDh with rd_logm), AL = value
; -----------------------------------------------------------------------------
rd_logm:
    push ax
    xor al, al
    call rd_log
    pop ax
    ret

rd_log:
    push di
    push es
    pushf
    cli
    cmp word [ro_logseg], 0
    je .out
    mov es, [ro_logseg]
    mov di, [es:0]
    cmp di, RAD_LOGKB * 1024 - 2 - 3
    jbe .room
    or word [es:0], 0x8000      ; FULL: bit 15 says a record was dropped, so
    jmp short .out              ; a gate cannot compare a truncated stream
.room:
    mov [es:di+2], bx
    mov [es:di+4], al
    add word [es:0], 3
.out:
    popf
    pop es
    pop di
    ret

rd_log_reset:
    push es
    cmp word [ro_logseg], 0
    je .out
    mov es, [ro_logseg]
    mov word [es:0], 0
.out:
    pop es
    ret
%endif

%include "radval.inc"           ; the validator (SPEC.md 96.4.4)
%include "rad2.inc"             ; the 2.1 engine (96.4.5)
%include "rad1.inc"             ; the 1.0 engine (96.4.5)
%include "radpace.inc"          ; the pacers (34.13)

; =============================================================================
; Tables
; =============================================================================
rd_slotoff:  db 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12
rd_opregs:   db 0x20, 0x40, 0x60, 0x80, 0xE0
rd_defpatch:                    ; SOUND.DRV's opl_defpatch, byte for byte
    db 0x21, 0x28, 0xF0, 0x07, 0x00
    db 0x21, 0x00, 0xF0, 0x07, 0x01
    db 0x00

rd_notefreq: dw 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5
             dw 0x202, 0x220, 0x241, 0x263, 0x287, 0x2AE

; =============================================================================
; The working set - zeroed by rd_load, so it need not ship (os88drv.py strips
; a trailing zero run and nothing reads it before rd_load's rep stosb)
; =============================================================================
rd_ws:
rd_far:      dd 0               ; the resident's dispatcher, for RADS_*
rd_ver:      db 0               ; 10h / 21h
rd_phase:    db 0               ; 1 = frames: the shadow filter is on
rd_fast:     db 0               ; 1 = opl_wrf (IF = 0), 0 = opl_wr's timing
rd_rate:     dw 0               ; tenths of a Hz
rd_shadow:   times 512 db 0     ; registers 000h..1FFh (34.12.6)
rd_st:       times RAD_ST_LEN db 0   ; the status block (34.12.7)
rd_ordlen:   db 0
rd_ordlist:  dw 0               ; claim offset of the order list
rd_zins:     times 24 db 0      ; the all-zero instrument (2.1 deviation 2)
r2_insptr:   times 127 dw 0     ; claim offset of each instrument's +0 byte
r2_insriff:  times 127 dw 0     ; ...and of its riff's lines, 0 = none
r2_insreg:   times 127 * 22 db 0    ; ...and its register values (r2_insvals)
r2_tracks:   times 100 dw 0     ; claim offset of each pattern's lines
r2_riffs:    times 90 dw 0      ; [riff * 9 + channel - 1]
r1_inst:     times 31 dw 0      ; 1.0: claim offset of each instrument
r1_patlist:  dw 0               ; 1.0: the pattern table's claim offset
rv_sp:       dw 0               ; the validator's unwind point
rv_ver:      db 0
rv_seen:     times 32 db 0      ; pattern numbers / riff ids already met

; --- the engine state proper: rd_rezero zeroes rd_eng .. rd_eng_end ----------
rd_eng:
rd_speed:    db 0
rd_speedcnt: db 0
rd_order:    db 0
rd_line:     db 0
rd_notes:    db 0               ; note plays this frame (2.1 deviation 5)
rd_halted:   db 0
r2_track:    dw 0
r2_ljump:    db 0               ; the line a D effect named, + 1 (0 = none)
r2_entr:     db 0
r2_notenum:  db 0
r2_octnum:   db 0
r2_instnum:  db 0
r2_effect:   db 0
r2_param:    db 0
r2_dummy:    db 0               ; the throwaway holder of tick_riff's peek
r2_chans:    times 9 * CH_SIZE db 0
r1_old43:    times 9 db 0
r1_olda0:    times 9 db 0
r1_oldb0:    times 9 db 0
r1_tsspd:    times 9 db 0
r1_tsfrq:    times 9 dw 0
r1_tson:     times 9 db 0
r1_port:     times 9 db 0
r1_vols:     times 9 db 0
r1_inst9:    times 9 db 0       ; the instrument each channel last loaded (status)
r1_ppos:     dw 0               ; the pattern pointer, claim offset or 0
r1_noct:     db 0               ; the octave of the note being played
rd_eng_end:

; --- the pacers' state (radpace.inc) -----------------------------------------
rp_seg:      dw 0               ; our own segment, for the SS swap
rp_busy:     db 0               ; the TICK class only: int 70h's is the resident's
rp_oss:      dw 0
rp_osp:      dw 0
rp_armed:    db 0
rp_acc:      dw 0
rp_secsn:    dw 0               ; tick: 2,000 a tick; RTC: periodic interrupts
rp_tnotes:   db 0
rp_rch:      dd 0               ; the resident's int 70h chain entry (RADS_HOOK)
rp_rx:       dd 0               ; ...and its exit, which clears its busy byte.
                                ; Both segments are loaded from RO_RSEG just
                                ; before each jump: the image moves (34.12.8)
rp_mask:     db 0               ; the PIC mask bits arm found set
rp_n:        dw 0
rp_exp:      dw 0
rp_frac:     dw 0, 0
rp_lastt:    dw 0
rp_nocred:   db 0
rp_nirq:     dd 0               ; RTC: interrupts that reached step 5, and...
rp_nstep:    dd 0               ; ...those that ran a frame (34.13.3, the gate's)

rd_ws_end:
RD_WS_END equ rd_ws_end - $$
%if RD_WS_END > RAD_TUNE - RAD_READ_HEAD
    %error "radplay: image + working set is past RAD_TUNE - RAD_READ_HEAD"
%endif

OS88_DRV_END
