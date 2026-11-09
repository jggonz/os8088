; =============================================================================
; os8088 - drivers/sound/snddiag.inc
;
; THE SOUND DEBUG DISK'S HALF OF SOUND.DRV (SPEC.md 127.3). The SAME driver
; source assembled with -DSNDDIAG keeps a record of what its probes saw and
; what its stream did, and answers one more package verb, SNDV_DIAG, that
; hands the record to SNDDIAG.O88. A build without SNDDIAG expands every macro
; here to NOTHING: the shipped SOUND.DRV is byte-identical with or without
; this file (tests/unit/t_debugdisk.py assembles both and compares).
;
; Included TWICE, on purpose, which is why the layout is equates and the code
; is macros: by sound.asm (the hooks, SDG_STATE and SDG_VERB) and by
; apps/snddiag/snddiag.asm (the SDG_* offsets alone - the package expands no
; macro). One source of the layout, WEAVE-SPEC 1.2's rule, never a copy.
;
; The hooks are cheap and few. Each is one or two instructions on a path the
; driver already takes - the ISR's ACK, the DSP write, the watchdog, the open
; - and the record is resident state (SDG_STATE sits beside SBL_STATE, above
; snd_keep), because the attach-only code that fills the probe half is handed
; back after the attach and the record has to outlive it.
; =============================================================================

%ifndef SNDDIAG_INC
%define SNDDIAG_INC

SNDV_DIAG    equ 14             ; the verb (sndpkg.inc's numbering, past
                                ; SNDV_RMODE). A shipped driver answers it
                                ; CF=1 AX=0 like any verb it does not know,
                                ; which is how the package tells the builds
                                ; apart
SDG_SIG      equ 'SG'
SDG_VER      equ 1

SDGF_COPY    equ 0              ; AL: ES:DI <- the record, CX = SDG_LEN
SDGF_MIXRD   equ 1              ; AL: mixer register AH, read CL times (1..4)
                                ;     after ONE index write -> AL, AH, DL, DH
SDGF_VOICE   equ 2              ; AL: DSP D8h now (voice/speaker status),
                                ;     legal during a stream except high-speed
SDGF_DSPCMD  equ 3              ; AL: DSP command AH with no stream open, up
                                ;     to two answer bytes -> AL, AH, CL = how
                                ;     many came; the DSP is reset after it
SDGE_NOSB    equ 1              ; refusals (CF=1, AX): no DSP found
SDGE_BUSY    equ 2              ;   a stream is open / high-speed holds it
SDGE_TIME    equ 3              ;   the DSP did not answer in SBL_POLLS
SDGE_ARG     equ 4              ;   an AL this driver does not know

SDG_SCANN    equ 7              ; reset-scan slots: six bases + the retry
SDG_LOGN     equ 32             ; DSP write log entries (power of two)

; --- the record, as SDGF_COPY writes it ---------------------------------------
SDG_O_SIG    equ 0              ; dw SDG_SIG
SDG_O_VER    equ 2              ; db SDG_VER
SDG_O_LEN    equ 3              ; db SDG_LEN
SDG_O_BASE   equ 4              ; dw the DSP base the scan settled, 0 = none
SDG_O_VERHI  equ 6              ; db E1h's major
SDG_O_VERLO  equ 7              ; db ...and minor
SDG_O_IRQ    equ 8              ; db the line, 0FFh = none usable
SDG_O_HDMA   equ 9              ; db the SB16's 16-bit channel, 0 = none
SDG_O_AUTO   equ 10             ; db 1 = auto-init DMA (DSP >= 2.00)
SDG_O_OPL    equ 11             ; db 0 / SND_OPL2 / SND_OPL3
SDG_O_MIX    equ 12             ; db 0 / 1 CT1335 / 2 CT1345 / 3 CT1745
SDG_O_SBUP   equ 13             ; db 1 = the DSP tier attached
SDG_O_MPU    equ 14             ; dw the MPU-401's data port, 0 = none
SDG_O_CVX    equ 16             ; dw the Covox's port while live, 0 = none
SDG_O_PMB    equ 18             ; dw the PicoMEM's command port, 0 = none
SDG_O_PMUP   equ 20             ; db 1 = we configured a PicoMEM
SDG_O_IMETH  equ 21             ; db how the IRQ was found: 0 never asked,
                                ;    1 PicoMEM, 2 mixer 80h, 3 F2h probe
SDG_O_M80    equ 22             ; db mixer 80h at the attach (DSP >= 4)
SDG_O_FTRY   equ 23             ; db F2h attempts made (0..2)
SDG_O_FIRED  equ 24             ; db the stub that confirmed: 0..3 of
                                ;    {7,5,3,2}, 0FFh none
SDG_O_IWIN   equ 25             ; db the line before the IRQ 5 test, 0 = none
SDG_O_I5SEG  equ 26             ; dw int 0Dh's segment at that test
SDG_O_IMASK  equ 28             ; db the master PIC's mask at discovery
SDG_O_SCANN  equ 29             ; db reset-scan slots used
SDG_O_SCAN   equ 30             ; SDG_SCANN x {dw base, db AL, db CF}: AL is
                                ;    the last byte 2xAh gave (0AAh = a DSP) or
                                ;    the last 2xEh status when none came
SDG_O_ISR    equ 58             ; dw sbl_isr entries with a block expected
SDG_O_SPUR   equ 60             ; dw ...and without (the spurious guard)
SDG_O_ACK    equ 62             ; db the last ACK read (2xEh / 2xFh)
SDG_O_DLI    equ 63             ; db the log's next slot (mod SDG_LOGN)
SDG_O_UNDER  equ 64             ; dw underrun pauses (sbl_under)
SDG_O_WDOG   equ 66             ; dw watchdog stops: no block IRQ for two
                                ;    block periods (sbl_tick)
SDG_O_OPENS  equ 68             ; dw playback starts by verb 0
SDG_O_DTMO   equ 70             ; dw DSP writes that timed out
SDG_O_DLOG   equ 72             ; SDG_LOGN x dw: low = the byte written,
                                ;    high = 1 if it timed out
SDG_O_LIVE   equ 136            ; the live stream, read at the copy:
SDG_O_ACT    equ 136            ; db 1 = a stream is open
SDG_O_STATE  equ 137            ; db SBL_ST_* (0 play, 1 under, 2 ended)
SDG_O_DIR    equ 138            ; db 0 out, 1 in
SDG_O_DIRECT equ 139            ; db 1 = the card reads the package's ring
SDG_O_EXT    equ 140            ; db 1 = an external ring
SDG_O_RING   equ 141            ; db 1 = ring mode
SDG_O_WIDE   equ 142            ; db 1 = 41h rate, C6h start (DSP 4)
SDG_O_HISP   equ 143            ; db 1 = high-speed 90h/91h (DSP 3)
SDG_O_TC     equ 144            ; db the time constant
SDG_O_FMT    equ 145            ; db SND_FMT_* bits
SDG_O_EXPECT equ 146            ; db 1 = a block IRQ is legitimate
SDG_O_PAUSED equ 147            ; db 1 = D0h issued, resume owed
SDG_O_HALF   equ 148            ; dw block (half) size
SDG_O_RATE   equ 150            ; dw the wide regime's rate in Hz
SDG_O_TOTAL  equ 152            ; dw the stream's three counters
SDG_O_FED    equ 154
SDG_O_CONS   equ 156
SDG_O_WD     equ 158            ; dw watchdog countdown and its reload
SDG_O_WDRST  equ 160
SDG_O_XPAGE  equ 162            ; db the 8237 page of the cycle hw_start set
SDG_O_ADPCM  equ 163            ; db 1 = ADPCM4
SDG_O_XOFF   equ 164            ; dw ...its offset
SDG_O_XCNT   equ 166            ; dw ...and length - 1
SDG_O_ACKP   equ 168            ; dw the ACK port
SDG_O_IMR    equ 170            ; db master PIC mask, read at the copy
SDG_O_IRR    equ 171            ; db master PIC request register
SDG_O_ISRR   equ 172            ; db master PIC in-service register
SDG_O_IMR2   equ 173            ; db slave PIC mask (0FFh on an XT: no slave)
SDG_LEN      equ 174

; =============================================================================
%ifdef SNDDIAG

; --- the hooks ----------------------------------------------------------------

; after a reset-scan `call sbl_f_reset`: AL and CF as it answered
%macro SDG_SCANHOOK 0
    pushf
    push bx
    push cx
    mov cl, 0
    adc cl, 0                   ; CL = the CF sbl_f_reset answered (push and
    mov bl, [sdg_scann]         ; mov leave the flags alone)
    cmp bl, SDG_SCANN
    jae %%full
    inc byte [sdg_scann]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    push ax
    mov ax, [sbl_base]
    mov [sdg_scan+bx], ax
    pop ax
    mov [sdg_scan+bx+2], al
    mov [sdg_scan+bx+3], cl
%%full:
    pop cx
    pop bx
    popf
%endmacro

%macro SDG_SET 2                ; a byte cell from an 8-bit register/imm
    mov byte [%1], %2
%endmacro

%macro SDG_ISRHOOK 0            ; sbl_isr, a block expected, AL = the ACK.
    mov [sdg_ack], al           ; No clock read here: the BIOS tick is at
    inc word [sdg_isr]          ; 0:046Ch, and segment 0 is a fault on
%endmacro                       ; kern_mega - the package samples per second

%macro SDG_SPURHOOK 0
    inc word [sdg_spur]
%endmacro

%macro SDG_WDOGHOOK 0
    inc word [sdg_wdog]
%endmacro

%macro SDG_OPENHOOK 0
    inc word [sdg_opens]
%endmacro

; in sbl_dsp_wr, AH = the byte, at the write (%1 = 0) or the timeout (%1 = 1)
%macro SDG_DSPHOOK 1
    pushf
    cli
    push bx
    mov bl, [sdg_dli]
    inc byte [sdg_dli]
    and bl, SDG_LOGN - 1
    and byte [sdg_dli], SDG_LOGN - 1
    xor bh, bh
    shl bx, 1
    mov [sdg_dlog+bx], ah
    mov byte [sdg_dlog+bx+1], %1
%if %1
    inc word [sdg_dtmo]
%endif
    pop bx
    popf
%endmacro

; --- the resident record --------------------------------------------------------
%macro SDG_STATE 0
sdg_imeth:  db 0
sdg_m80:    db 0
sdg_ftry:   db 0
sdg_iwin:   db 0
sdg_i5seg:  dw 0
sdg_scann:  db 0
sdg_ack:    db 0
sdg_dli:    db 0
sdg_scan:   times SDG_SCANN * 4 db 0
sdg_isr:    dw 0
sdg_spur:   dw 0
sdg_wdog:   dw 0
sdg_opens:  dw 0
sdg_dtmo:   dw 0
sdg_mx:     dw 0, 0             ; SDGF_MIXRD / SDGF_DSPCMD's answer bytes
sdg_dlog:   times SDG_LOGN dw 0
%endmacro

; --- the verb ---------------------------------------------------------------------
; sdg_verb - SNDV_DIAG (SPEC.md 127.3). in: AL = SDGF_*, ES = the caller's
; segment (OSAPI_DRV_CALL), DS = ours. Every port touch is under pushf/cli and
; bounded; nothing here is attach-only, so it may run all session.
%macro SDG_VERB 0
sdg_verb:
    cmp al, SDGF_COPY
    je sdg_copy
    cmp word [sbl_base], 0
    je sdg_nosb
    cmp al, SDGF_MIXRD
    je sdg_mixrd
    cmp al, SDGF_VOICE
    je sdg_voice
    cmp al, SDGF_DSPCMD
    je sdg_dspcmd
    mov ax, SDGE_ARG
    stc
    ret
sdg_nosb:
    mov ax, SDGE_NOSB
    stc
    ret

; SDGF_COPY: ES:DI -> SDG_LEN bytes; out CX = SDG_LEN, CF = 0
sdg_copy:
    push ax
    push bx
    push si
    push di
    cld
    pushf
    cli
    mov ax, SDG_SIG
    stosw                                   ; 0
    mov ax, SDG_VER | (SDG_LEN << 8)
    stosw                                   ; 2
    mov ax, [sbl_base]
    stosw                                   ; 4
    mov al, [sbl_verhi]
    mov ah, [sbl_verlo]
    stosw                                   ; 6
    mov al, [sbl_irq]
    mov ah, [sbl_hdma]
    stosw                                   ; 8
    mov al, [sbl_auto]
    mov ah, [opl_kind]
    stosw                                   ; 10
    mov al, [mix_kind]
    mov ah, [sbl_up]
    stosw                                   ; 12
    mov ax, [mpu_base]
    stosw                                   ; 14
    mov ax, [cvx_port]
    stosw                                   ; 16
%ifdef PICOMEM
    mov ax, [pm_base]
    stosw                                   ; 18
    mov al, [pm_up]
%else
    xor ax, ax
    stosw
%endif
    mov ah, [sdg_imeth]
    stosw                                   ; 20
    mov al, [sdg_m80]
    mov ah, [sdg_ftry]
    stosw                                   ; 22
    mov al, [sbl_dsc_fired]
    mov ah, [sdg_iwin]
    stosw                                   ; 24
    mov ax, [sdg_i5seg]
    stosw                                   ; 26
    mov al, [sbl_dsc_mask]
    mov ah, [sdg_scann]
    stosw                                   ; 28
    mov si, sdg_scan
    mov cx, SDG_SCANN * 2
    rep movsw                               ; 30
    mov ax, [sdg_isr]
    stosw                                   ; 58
    mov ax, [sdg_spur]
    stosw                                   ; 60
    mov al, [sdg_ack]
    mov ah, [sdg_dli]
    stosw                                   ; 62
    mov ax, [sbl_under]
    stosw                                   ; 64
    mov ax, [sdg_wdog]
    stosw                                   ; 66
    mov ax, [sdg_opens]
    stosw                                   ; 68
    mov ax, [sdg_dtmo]
    stosw                                   ; 70
    mov si, sdg_dlog
    mov cx, SDG_LOGN
    rep movsw                               ; 72
    mov al, [sbl_str_act]
    mov ah, [sbl_str_state]
    stosw                                   ; 136
    mov al, [sbl_str_dir]
    mov ah, [sbl_direct]
    stosw                                   ; 138
    mov al, [sbl_ext]
    mov ah, [sbl_ring_on]
    stosw                                   ; 140
    mov al, [sbl_wide]
    mov ah, [sbl_hisp]
    stosw                                   ; 142
    mov al, [sbl_tc]
    mov ah, [sbl_fmt]
    stosw                                   ; 144
    mov al, [sbl_expect]
    mov ah, [sbl_paused]
    stosw                                   ; 146
    mov ax, [sbl_half]
    stosw                                   ; 148
    mov ax, [sbl_rate]
    stosw                                   ; 150
    mov ax, [sbl_total]
    stosw                                   ; 152
    mov ax, [sbl_fed]
    stosw                                   ; 154
    mov ax, [sbl_consumed]
    stosw                                   ; 156
    mov ax, [sbl_wd]
    stosw                                   ; 158
    mov ax, [sbl_wd_rst]
    stosw                                   ; 160
    mov al, [sbl_xpage]
    mov ah, [sbl_adpcm]
    stosw                                   ; 162
    mov ax, [sbl_xoff]
    stosw                                   ; 164
    mov ax, [sbl_xcnt]
    stosw                                   ; 166
    mov ax, [sbl_ackp]
    stosw                                   ; 168
    in al, 0x21                             ; the PIC: the mask has no side
    stosb                                   ; 170 - effect; the two OCW3 reads
    mov al, 0x0A                            ; leave the IRR selected, which
    out 0x20, al                            ; is the 8259's own default and
    in al, 0x20                             ; what kernel/kbddiag.inc puts
    stosb                                   ; 171 - back as well
    mov al, 0x0B
    out 0x20, al
    in al, 0x20
    stosb                                   ; 172
    mov al, 0x0A
    out 0x20, al
    call OSAPI_CPU_INFO                     ; an XT has no slave: its port
    cmp al, CPU_8086                        ; A1h floats, and says so as FFh
    mov al, 0xFF
    je .noslave
    in al, 0xA1
.noslave:
    stosb                                   ; 173
%if SDG_LEN != 174
  %error "sdg_copy writes the record in SDG_O_* order"
%endif
    popf
    pop di
    pop si
    pop bx
    pop ax
    mov cx, SDG_LEN
    clc
    ret

; SDGF_MIXRD: AH = register, CL = reads (1..4) after one index write.
; out AL, AH, DL, DH = the reads in order (0 past the count), CF = 0
sdg_mixrd:
    push cx
    push si
    and cx, 3                   ; 1..4 (4, and 0, read four)
    jnz .n
    mov cl, 4
.n:
    mov si, sdg_mx
    mov word [si], 0
    mov word [si+2], 0
    mov dx, [sbl_base]
    add dx, 4
    pushf
    cli
    mov al, ah
    out dx, al
    inc dx
.r:
    in al, dx
    mov [si], al
    inc si
    loop .r
    popf
    mov ax, [sdg_mx]
    mov dx, [sdg_mx+2]
    pop si
    pop cx
    clc
    ret

; SDGF_VOICE: D8h now. Refused under high-speed DMA, which takes no command.
; Under cli: a DSP that is moving data takes a command only just after its
; busy bit clears (the ES1868's datasheet gives 13us), and the block IRQ
; must not land between the write and the read. out AL = the answer
sdg_voice:
    cmp byte [sbl_str_act], 0
    je .go
    cmp byte [sbl_hisp], 0
    jne sdg_busy
.go:
    mov ah, 0xD8
    pushf
    cli
    call sdg_cmd1
    jc .t
    popf
    clc
    ret
.t:
    popf
    jmp short sdg_time

; SDGF_DSPCMD: AH = a command, only with no stream open (so no IRQ and no
; cli: an unanswered command polls for ms). Up to two answer bytes -> AL, AH,
; CL = how many came. The DSP is RESET after it, whatever it answered, so an
; operand it may wait for or a byte left unread ends here, as at an attach.
sdg_dspcmd:
    cmp byte [sbl_str_act], 0
    jne sdg_busy
    mov word [sdg_mx], 0
    xor cx, cx
    call sdg_cmd1
    jc .reset
    inc cx
    mov [sdg_mx], al
    call sdg_rd
    jc .reset
    inc cx
    mov [sdg_mx+1], al
.reset:
    push ax
    push cx
    xor cx, cx                  ; the attach's slow-DSP allowance
    call sbl_f_reset
    pop cx
    pop ax
    mov ax, [sdg_mx]
    jcxz sdg_time
    clc
    ret
sdg_busy:
    mov ax, SDGE_BUSY
    stc
    ret
sdg_time:
    mov ax, SDGE_TIME
    stc
    ret

; sdg_cmd1: write AH, read one byte -> AL (CF=0), or CF=1 AX=SDGE_TIME
sdg_cmd1:
    mov al, ah
    call sbl_dsp_wr
    jc sdg_time
; sdg_rd: one byte from 2xAh within SBL_POLLS -> AL; CF=1 AX=SDGE_TIME
sdg_rd:
    push cx
    push dx
    mov dx, [sbl_base]
    add dx, 0x0E
    mov cx, SBL_POLLS
.p:
    in al, dx
    test al, 0x80
    jnz .ok
    loop .p
    pop dx
    pop cx
    jmp sdg_time
.ok:
    sub dx, 4
    in al, dx
    pop dx
    pop cx
    clc
    ret
%endmacro

%else ; ---- the shipped build: every hook is nothing ---------------------------
%macro SDG_SCANHOOK 0
%endmacro
%macro SDG_SET 2
%endmacro
%macro SDG_ISRHOOK 0
%endmacro
%macro SDG_SPURHOOK 0
%endmacro
%macro SDG_WDOGHOOK 0
%endmacro
%macro SDG_OPENHOOK 0
%endmacro
%macro SDG_DSPHOOK 1
%endmacro
%macro SDG_STATE 0
%endmacro
%macro SDG_VERB 0
%endmacro
%endif

%endif ; SNDDIAG_INC
