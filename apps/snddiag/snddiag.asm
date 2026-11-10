; =============================================================================
; os8088 - apps/snddiag/snddiag.asm - SNDDIAG.O88, "Sound Debug"
;
; THE SOUND DEBUG DISK'S PROGRAM (SPEC.md 127.4). It boots from the disk's
; \AUTOSTRT and puts on the screen everything SOUND.DRV found and everything
; a short playback does, as text a user photographs and sends us. Three
; pages, each one screenful on every adapter (17 rows of 78 columns - CGA's
; 640x200 is the floor that sizes it):
;
;   1 DETECT     the driver's probes: the reset scan base by base, the DSP
;                version, how the IRQ was found, the mixer, an ESS chip's own
;                identity, OPL, MPU-401, Covox, PicoMEM, the caps word, and a
;                BLASTER= line and a verdict in words
;   2 PLAY TEST  a tone through the real stream path (ring mode, the same
;                verbs Tracker and MIDIRack use) for ten seconds, a row a
;                second: bytes the DMA moved, block IRQs, the DSP's own voice
;                status (D8h), the mixer registers that could mute it, the
;                stream's state - and a verdict naming the first thing that
;                stopped
;   3 REGISTERS  the mixer's registers 01h..7Fh, the last 32 bytes written to
;                the DSP, the stream record and the 8259
;
; THE DETAIL COMES FROM THE DRIVER, through SNDV_DIAG (snddiag.inc), which
; only the debug disk's SOUND.DRV answers - the same source built with
; -DSNDDIAG, so what it reports is what the shipped driver does. On any other
; SOUND.DRV this still runs: page 1 says so and shows what the public verbs
; tell, and the play test still measures bytes and the state.
; =============================================================================
%include "os88api.inc"
%include "buildnum.inc"             ; BUILD_NUM (SPEC.md 14.2), from $(BUILD)
%include "sndpkg.inc"               ; SNDV_IDENT, SND_FM_INFO ...
%include "snddiag.inc"              ; the record's layout and SNDV_DIAG

%define DEC_U16
%define DEC_U32
%define DEC_FLD
    OS88_HEADER 'Sound Debug', sd_entry
    OS88_ICON16                     ; a speaker with a magnifier's handle
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0x0000, 0x0080, 0x0188, 0x0384
    dw 0x3F92, 0x3F8A, 0x3F8A, 0x3F8A
    dw 0x3F8A, 0x3F92, 0x0384, 0x0188
    dw 0x0080, 0x0000, 0x7FFE, 0x0000
    OS88_ICON16_END

SD_COLS     equ 78                  ; one row's characters
SD_LW       equ SD_COLS + 1         ; ...and its NUL
SD_ROWS     equ 17
SD_RL       equ 16384               ; the play test's ring: a power of two
SD_LEAD     equ 12288               ; what each feed keeps ahead of the card
SD_SECS     equ 10                  ; play test length, seconds
SD_TONE     equ 256                 ; the staged tone block (every period
                                    ; divides it, and it divides SD_RL)
SD_DSPGAP   equ 36                  ; ticks on page 1 before the play test

; --- the window ------------------------------------------------------------------
sd_entry:
    mov si, sd_tpl
    call OSAPI_WM_CREATE            ; BX = window, CF = table full
    jc .ret
    mov [sd_win], bx
    OS88_REGION_MOVABLE_NOKEEP sd_ret ; every byte that outlives a pass is a
                                    ; static and moves with us: a bare `ret`
    mov si, sd_pref                 ; a size per adapter (SPEC.md 11.100.1)
    call OSAPI_WM_PREFER
    mov ax, SD_DSPGAP               ; the play test starts from the timer, on
    call OSAPI_WM_TIMER             ; the UI task - verb 0 is a callback's
    jnc .timed
.notimer:
    mov byte [sd_pstate], SD_PS_NOTIMER
.timed:
    push bx
    call sd_probe                   ; ask everything once
    call sd_build
    pop bx
    clc
.ret:
    ret
sd_ret:
    ret

; sd_pitch - the row pitch for this screen: 8 on CGA's 200 lines, 10 on
; Hercules' and EGA's ~350, 12 on VGA's 480. out AX
sd_pitch:
    mov ax, [sd_vh]
    cmp ax, 300
    mov ax, 8
    jb .r
    mov ax, [sd_vh]
    cmp ax, 400
    mov ax, 10
    jb .r
    mov ax, 12
.r:
    ret

; --- painting ------------------------------------------------------------------
; sd_paint - W_PAINT: every row, then the shown copy is the built one
sd_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, si
    xor di, di                      ; every row
.row:
    call sd_drawrow
    inc di
    cmp di, SD_ROWS
    jb .row
    push ds
    pop es
    mov si, sd_txt
    mov di, sd_shown
    mov cx, SD_ROWS * SD_LW
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sd_drawrow - BX = window, DI = row: one opaque run, the header inverse
sd_drawrow:
    push ax
    push cx
    push dx
    push si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 4
    add dx, 2
    call sd_pitch
    push dx
    mul di                          ; AX = row * pitch (DX:AX, small)
    pop dx
    add dx, ax
    mov ax, di
    mov si, SD_LW
    push dx
    mul si
    pop dx
    add ax, sd_txt
    mov si, ax
    mov ax, (CWHITE << 8) | CBLACK  ; AL ink, AH ground
    or di, di
    jnz .ink
    mov ax, (CBLACK << 8) | CWHITE  ; the header row, inverse
.ink:
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop ax
    ret

; sd_refresh - draw only the rows that changed since they were last drawn.
; Called with the gfx lock held (the timer's and the key's environment)
sd_refresh:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bx, [sd_win]
    push ds
    pop es
    cld
    xor di, di
.row:
    mov ax, di
    mov cx, SD_LW
    mul cx
    mov si, ax
    push di
    lea di, [sd_shown+si]
    add si, sd_txt
    mov cx, SD_LW
    push si
    push di
    repe cmpsb
    pop di
    pop si
    je .same
    mov cx, SD_LW
    rep movsb                       ; shown <- built
    pop di
    call sd_drawrow
    jmp short .next
.same:
    pop di
.next:
    inc di
    cmp di, SD_ROWS
    jb .row
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- keys ----------------------------------------------------------------------
sd_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp al, 27
    je .quit
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 20h
.up:
    cmp al, '1'
    jb .notpg
    cmp al, '3'
    ja .notpg
    sub al, '1'
    mov [sd_page], al
    cmp al, 2                       ; the registers: read them now
    jne .redo
    call sd_mixdump
    jmp short .redo
.notpg:
    cmp al, 'P'
    jne .notp
    call sd_pstart
    jmp short .redo
.notp:
    cmp al, 'R'
    jne .notr
    cmp byte [sd_pstate], SD_PS_RUN ; not while one plays
    je .out
    mov al, [sd_ratei]
    inc ax
    cmp al, SD_NRATE
    jb .r1
    xor al, al
.r1:
    mov [sd_ratei], al
    jmp short .redo
.notr:
    cmp al, 'V'
    jne .out
    xor byte [sd_vsamp], 1
.redo:
    call sd_recopy
    call sd_build
    call sd_refresh
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.quit:
    mov bx, si
    call OSAPI_WM_CLOSE             ; deferred: return and draw nothing
    jmp short .out

sd_onclose:
    call sd_pstop
    clc
    ret

sd_about:
    mov bx, si
    mov si, sd_ablines
    jmp os88ui_about

; --- probing (once, at the entry) ----------------------------------------------
sd_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_VIDEO                ; AX w, BX h, DL adapter, DH bpp
    mov [sd_vh], bx
    mov [sd_vkind], dl
    mov [sd_vbpp], dh
    call OSAPI_CPU_INFO
    mov [sd_cpu], ax
    call OSAPI_SND_CAPS             ; AX caps, DX bit 1 driver loaded
    mov [sd_caps], ax
    mov [sd_capdx], dx
    mov al, SND_FM_INFO
    call OSAPI_SND_FM               ; AL = SND_OPL2 / SND_OPL3, CF none
    jnc .fm
    xor al, al
.fm:
    mov [sd_fm], al
    mov bx, DRVC_SOUND * 256 + SNDV_IDENT
    call OSAPI_DRV_CALL
    jc .nodrv
    cmp ax, SND_SIG
    jne .nodrv
    mov byte [sd_drv], 1
    mov bx, DRVC_SOUND * 256 + SNDV_DIAG
    mov al, SDGF_COPY
    mov di, sd_rec
    call OSAPI_DRV_CALL             ; ES = our DS: the record lands here
    jc .nodrv
    cmp word [sd_rec+SDG_O_SIG], SDG_SIG
    jne .nodrv
    mov byte [sd_diag], 1
    cmp byte [sd_rec+SDG_O_SBUP], 0 ; no DSP tier: nothing more to ask
    je .nodrv
    cmp byte [sd_rec+SDG_O_VERHI], 3
    jne .noe7                       ; an ESS answers DSP 3.01; ask it E7h,
    mov bx, DRVC_SOUND * 256 + SNDV_DIAG
    mov ax, SDGF_DSPCMD | (0xE7 << 8)   ; which is its identify (a
    call OSAPI_DRV_CALL             ; Creative DSP does not answer it, and the
    jc .noe7                        ; driver resets the DSP after either way)
    mov [sd_e7], ax
    mov [sd_e7n], cl
.noe7:
    cmp byte [sd_rec+SDG_O_MIX], 0
    je .nodrv
    mov bx, DRVC_SOUND * 256 + SNDV_DIAG
    mov ax, SDGF_MIXRD | (0x40 << 8)    ; ESS: mixer 40h four times after ONE
    mov cl, 4                           ; index write - 18h, the part number,
    call OSAPI_DRV_CALL                 ; and the config port (Linux es18xx's
    jc .nodrv                           ; identify)
    mov [sd_m40], ax
    mov [sd_m40+2], dx
.nodrv:
    call sd_mixdump
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sd_recopy - the record again (the live half and the counters)
sd_recopy:
    cmp byte [sd_diag], 0
    je .r
    push ax
    push bx
    push cx
    push di
    mov bx, DRVC_SOUND * 256 + SNDV_DIAG
    mov al, SDGF_COPY
    mov di, sd_rec
    call OSAPI_DRV_CALL
    pop di
    pop cx
    pop bx
    pop ax
.r:
    ret

; sd_mixrd - AH = register -> AL (CF=1 none: no diag driver / no mixer)
sd_mixrd:
    cmp byte [sd_diag], 0
    je .no
    cmp byte [sd_rec+SDG_O_MIX], 0
    je .no
    push bx
    push cx
    push dx
    mov bx, DRVC_SOUND * 256 + SNDV_DIAG
    mov al, SDGF_MIXRD
    mov cl, 1
    call OSAPI_DRV_CALL
    pop dx
    pop cx
    pop bx
    ret
.no:
    stc
    ret

; sd_isess - CF=0 when mixer 40h said ESS (18h then a part number)
sd_isess:
    cmp byte [sd_m40], 0x18
    jne .no
    cmp byte [sd_m40+1], 0x18
    je .no
    cmp byte [sd_m40+1], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; --- the play test ---------------------------------------------------------------
SD_PS_IDLE    equ 0
SD_PS_RUN     equ 1
SD_PS_DONE    equ 2
SD_PS_FAIL    equ 3
SD_PS_NOTIMER equ 4

sd_ontimer:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [sd_pstate], SD_PS_IDLE
    jne .run
    cmp byte [sd_auto], 0           ; the first firing: the automatic start
    jne .out
    mov byte [sd_auto], 1
    call sd_pstart
    jmp short .draw
.run:
    cmp byte [sd_pstate], SD_PS_RUN
    jne .out
    call sd_ptick
.draw:
    call sd_build
    call sd_refresh
    cmp byte [sd_pstate], SD_PS_RUN
    jne .out
    mov bx, [sd_win]
    mov ax, 1
    call OSAPI_WM_TIMER
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sd_pstart - grant, stage, open; on page 2 from here on
sd_pstart:
    cmp byte [sd_pstate], SD_PS_RUN
    je .ret
    cmp byte [sd_pstate], SD_PS_NOTIMER
    je .ret
    mov byte [sd_auto], 1
    mov byte [sd_page], 1
    mov byte [sd_prow], 0
    mov word [sd_perr], 0
    push ds
    pop es
    mov di, sd_rows                 ; the table, cleared
    mov cx, SD_SECS * SD_ROWSZ
    xor al, al
    cld
    rep stosb
    mov bl, [sd_ratei]
    xor bh, bh
    shl bx, 1
    mov ax, [sd_rates+bx]
    mov [sd_prate], ax
    mov ax, [sd_periods+bx]
    call sd_tone                    ; sd_tonebuf <- one period, repeated
    mov ax, 0x0007                  ; grant SD_RL
    mov cx, SD_RL
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .egrant
    mov [sd_grant], si
    mov di, si
    mov dx, SD_RL / SD_TONE
.stage:
    mov ax, 0x0006
    mov si, sd_tonebuf
    mov cx, SD_TONE
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .estage
    add di, SD_TONE
    dec dx
    jnz .stage
    call sd_recopy                  ; the counters before
    mov ax, [sd_rec+SDG_O_ISR]
    mov [sd_pisr0], ax
    mov [sd_pisrl], ax
    mov ax, SND_OPENF_RING << 8     ; verb 0, ring
    mov si, [sd_grant]
    mov cx, SD_LEAD
    mov dx, [sd_prate]
    call OSAPI_SND_STREAM           ; AL = err, AH = handle
    or al, al
    jnz .eopen
    mov [sd_hand], ah
    mov byte [sd_pstate], SD_PS_RUN
    call OSAPI_GET_TICKS
    mov [sd_pt0], ax
    mov word [sd_plast], 0
    xor ax, ax
    mov [sd_psum], ax
    mov [sd_psum+2], ax
    call sd_recopy                  ; the open's regime, for page 2
    mov bx, [sd_win]
    mov ax, 1
    call OSAPI_WM_TIMER
.ret:
    ret
.egrant:
    mov byte [sd_pfail], 'G'
    jmp short .fail
.estage:
    mov byte [sd_pfail], 'S'
    jmp short .free
.eopen:
    mov byte [sd_pfail], 'O'
.free:
    push ax
    mov si, [sd_grant]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
    pop ax
.fail:
    xor ah, ah
    mov [sd_perr], ax
    mov byte [sd_pstate], SD_PS_FAIL
    ret

; sd_pstop - close and free, if a stream is ours
sd_pstop:
    cmp byte [sd_pstate], SD_PS_RUN
    jne .r
    push ax
    push si
    mov ah, [sd_hand]
    mov al, 2
    call OSAPI_SND_STREAM
    mov si, [sd_grant]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
    mov byte [sd_pstate], SD_PS_DONE
    pop si
    pop ax
.r:
    ret

; sd_ptick - feed, and at each whole second take a row
sd_ptick:
    mov ah, [sd_hand]
    mov al, 3                       ; status: AX state, DX consumed
    call OSAPI_SND_STREAM
    mov [sd_pst], al
    add dx, SD_LEAD                 ; keep the ring full: the tone is the
    mov cx, dx                      ; same in every byte, so nothing is
    mov ah, [sd_hand]               ; ever restaged - the total just moves
    mov al, 1
    call OSAPI_SND_STREAM
    call OSAPI_GET_TICKS
    sub ax, [sd_pt0]                ; ticks since the open
    mov cx, 10
    mul cx                          ; tenths of a tick...
    mov cx, 182
    div cx                          ; ...AX = whole seconds
    mov bl, [sd_prow]
    xor bh, bh
    cmp ax, bx
    jbe .ret                        ; this second's row is taken
    ; --- a row: second BX+1 -------------------------------------------------
    mov ax, SD_ROWSZ
    mul bx
    mov di, ax
    add di, sd_rows
    mov ah, [sd_hand]
    mov al, SND_V_PPOS              ; bytes played exactly
    call OSAPI_SND_STREAM
    jnc .pp
    mov ah, [sd_hand]               ; (an older driver: verb 3's count)
    mov al, 3
    call OSAPI_SND_STREAM
.pp:
    mov [di+SR_STATE], al
    mov ax, dx
    sub ax, [sd_plast]              ; bytes this second (mod 65536)
    mov [sd_plast], dx
    mov [di+SR_BYTES], ax
    add [sd_psum], ax
    adc word [sd_psum+2], 0
    mov byte [di+SR_VALID], 1
    call sd_recopy
    mov ax, [sd_rec+SDG_O_ISR]
    mov cx, ax
    sub ax, [sd_pisrl]
    mov [sd_pisrl], cx
    mov [di+SR_IRQ], ax
    mov byte [di+SR_D8F], 0         ; D8h: the voice's own status
    cmp byte [sd_diag], 0
    je .nod8
    cmp byte [sd_vsamp], 0
    je .nod8
    cmp byte [sd_rec+SDG_O_VERHI], 3
    jb .nod8
    push bx
    mov bx, DRVC_SOUND * 256 + SNDV_DIAG
    mov al, SDGF_VOICE
    call OSAPI_DRV_CALL
    pop bx
    mov byte [di+SR_D8F], 2         ; 2 = asked and refused / no answer
    jc .nod8
    mov [di+SR_D8], al
    mov byte [di+SR_D8F], 1
.nod8:
    xor bx, bx                      ; the mixer registers that can mute it
.mx:
    mov ah, [sd_mxregs+bx]
    or ah, ah
    jz .mxdone
    cmp ah, 0x60                    ; 60h/62h/64h are an ESS's own
    jb .mxrd
    call sd_isess
    jc .mxskip
.mxrd:
    call sd_mixrd
    jc .mxskip
    mov [bx+di+SR_MIX], al
    or byte [di+SR_MIXF], 1
.mxskip:
    inc bx
    jmp short .mx
.mxdone:
    inc byte [sd_prow]
    cmp byte [sd_prow], SD_SECS
    jb .ret
    call sd_pstop                   ; ten rows: done
    call sd_mixdump
.ret:
    ret

; sd_tone - AX = period in samples, a power of two dividing SD_TONE: a
; square wave at three-eighths of full scale either side of 80h
sd_tone:
    push ax
    push bx
    push cx
    push di
    shr ax, 1                       ; half a period: the bit that flips
    mov bx, ax
    mov di, sd_tonebuf
    xor cx, cx
.b:
    test cx, bx
    mov al, 0xB0
    jz .s
    mov al, 0x50
.s:
    mov [di], al
    inc di
    inc cx
    cmp cx, SD_TONE
    jb .b
    pop di
    pop cx
    pop bx
    pop ax
    ret

; --- os88dec, os88ui -----------------------------------------------------------
%include "os88dec.inc"
%define OS88UI_ABOUT                ; the standard About card and nothing else
%define OS88UI_NOBTN
%include "os88ui.inc"
%include "snddiag/sdtext.inc"              ; the three pages' text

; --- data ----------------------------------------------------------------------
sd_tpl:     dw 0, MBAR_H, 640, 240
            dw sd_ttl, sd_cls
; its CLASS (SPEC.md 11.104): every handler, and the flags it is born with
    OS88_CLASS sd_cls, 0, PAINT, sd_paint, ONKEY, sd_onkey, ABOUT, sd_about, \
               ONCLOSE, sd_onclose, ONTIMER, sd_ontimer
    OS88_PREFER sd_pref, 640, SD_ROWS*12+6+TITLE_H+2, 712, SD_ROWS*10+6+TITLE_H+2, 640, SD_ROWS*8+6+TITLE_H+2
sd_ttl      equ 16                  ; the header's name field

SD_NRATE    equ 4
sd_rates:   dw 22050, 11025, 8000, 44100
sd_periods: dw 64, 32, 16, 128      ; ~345, 345, 500 and 345 Hz
sd_mxregs:  db 0x22, 0x04, 0x60, 0x62, 0x64, 0
SD_NMX      equ 5

sd_ablines:
    dw sd_ab1, sd_ab2, sd_ab3, 0
sd_ab1:     db 'Sound Debug for os8088', 0
sd_ab2:     db 'What SOUND.DRV found, and what a tone does', 0
sd_ab3:     db 'Photograph pages 1, 2 and 3 and send them to us', 0

sd_ratei:   db 0
sd_vsamp:   db 1                    ; D8h sampling on

; the play table: one row a second
SR_VALID    equ 0
SR_STATE    equ 1
SR_BYTES    equ 2
SR_IRQ      equ 4
SR_D8F      equ 6                   ; 0 not asked, 1 SR_D8 holds it, 2 refused
SR_D8       equ 7
SR_MIXF     equ 8
SR_MIX      equ 9                   ; SD_NMX bytes
SD_ROWSZ    equ 16

; --- bss: offsets past the image, zeroed at load ---------------------------------
%assign SDB 0
%macro SDV 2
%1 equ os88_image_end + SDB
  %assign SDB SDB + (%2)
%endmacro
SDV sd_win, (1)*2
SDV sd_vh, (1)*2
SDV sd_vkind, (1)*1
SDV sd_vbpp, (1)*1
SDV sd_cpu, (1)*2
SDV sd_caps, (1)*2
SDV sd_capdx, (1)*2
SDV sd_fm, (1)*1
SDV sd_drv, (1)*1
SDV sd_diag, (1)*1
SDV sd_page, (1)*1
SDV sd_auto, (1)*1
SDV sd_pstate, (1)*1
SDV sd_pfail, (1)*1
SDV sd_pst, (1)*1
SDV sd_prow, (1)*1
SDV sd_hand, (1)*1
SDV sd_e7n, (1)*1
%assign SDB SDB + (1)*1
SDV sd_e7, (1)*2
SDV sd_m40, (4)*1
SDV sd_perr, (1)*2
SDV sd_prate, (1)*2
SDV sd_grant, (1)*2
SDV sd_pt0, (1)*2
SDV sd_plast, (1)*2
SDV sd_psum, (2)*2
SDV sd_pisr0, (1)*2
SDV sd_pisrl, (1)*2
SDV sd_rec, (SDG_LEN + 2)*1
SDV sd_rows, (SD_SECS * SD_ROWSZ)*1
SDV sd_tonebuf, (SD_TONE)*1
SDV sd_txt, (SD_ROWS * SD_LW)*1
SDV sd_shown, (SD_ROWS * SD_LW)*1
SDV sd_num, (16)*1
SDV sd_rowp, (1)*2
SDV sd_mixok, (1)*1
SDV sd_mixd, (128)*1
OS88_BSS SDB
OS88_IMAGE_END
