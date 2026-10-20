; =============================================================================
; os8088 - apps/audio/apwav.inc
;
; The Audio Player's RIFF/WAVE parser (SPEC.md 86.6). Given a file name
; (already GOTO'd to the right directory) and the file's size, it answers a
; small parsed record or a refusal string. Everything the file claims about
; its own sizes is bounds-checked before it is trusted.
;
; Accepted:
;   WAVE_FORMAT_PCM       0x0001  mono, 8 bits/sample, unsigned
;   WAVE_FORMAT_DVI_ADPCM 0x0011  mono, 4 bits/sample, IMA/DVI
; and only sample rates in apw_rates. 8000..22050 play on any SB; 24000,
; 32000, 44100 need an SB Pro (DSP >= 3.00) or an SB16 - SOUND.DRV's rate
; regime split (SPEC.md 34.5/86.15). Mono 8-bit throughout. The parser accepts
; all seven; the DRIVER refuses what the card cannot clock, and ap_open_track
; reports it as "needs a Sound Blaster Pro".
;
; NOT assumed (SPEC.md 86.6): that `fmt ` is followed by `data`; a fixed
; header size; the absence of LIST / fact / INFO / id3 / other chunks. The
; walk skips every chunk it does not recognise, honours the RIFF odd-length
; pad byte, and slides a fresh cluster window forward when a skipped chunk
; runs past the one it holds (up to APW_WINDOWS windows). A header larger
; than that is refused, not mis-read. THE WINDOW IS THE LOOK-AHEAD RING
; (ES = [ap_la_seg], offset 0): a parse happens only before a track's ring
; is primed, so the ring is free, and it is the int 13h target the refill
; reads into anyway (SPEC.md 86.5.2) - no 16 KB of bss for a header.
;
; Output (module bss, prefix apw_):
;   apw_fmt     AP_FMT_PCM8 / AP_FMT_ADPCM
;   apw_rate    Hz (one of apw_rates)
;   apw_balign  nBlockAlign
;   apw_spb     wSamplesPerBlock (ADPCM; 1 for PCM8)
;   apw_datoff  dword: byte offset of the first audio byte in the file
;   apw_datlen  dword: data-chunk length, clamped to the file
;   apw_err     -> NUL reason string when the parse refused (CF = 1)
; =============================================================================

APW_WINDOWS equ 4                   ; cluster windows the walk may read in all

apw_e_riff:   db 'Not a RIFF/WAVE file', 0
apw_e_short:  db 'File is truncated', 0
apw_e_nofmt:  db 'No fmt chunk in header', 0
apw_e_nodat:  db 'No data chunk in header', 0
apw_e_big:    db 'WAV header too large', 0
apw_e_chan:   db 'Only mono WAV is supported', 0
apw_e_tag:    db 'Unsupported WAV format tag', 0
apw_e_bits:   db 'Unsupported bit depth', 0
apw_e_rate:   db 'Unsupported sample rate', 0
apw_e_blk:    db 'Bad WAV block alignment', 0
apw_e_spb:    db 'Bad ADPCM samples per block', 0
apw_e_io:     db 'Cannot read the file', 0
apw_e_clu:    db 'Cluster too large for this player', 0

apw_s_riff:   db 'RIFF'
apw_s_wave:   db 'WAVE'
apw_s_fmt:    db 'fmt '
apw_s_data:   db 'data'
apw_s_spk:    db 'o8sp'

apw_rates:    dw 8000, 11025, 16000, 22050, 24000, 32000, 44100
APW_NRATES equ ($ - apw_rates) / 2

; =============================================================================
; apw_parse - parse the named WAV
; in:  SI -> NUL 8.3 name in THIS segment (current directory already set)
;      DX:AX = the file's total size in bytes
; out: CF = 0 and the record filled, or CF = 1 with apw_err set.
; clobbers: AX..DI  (top-level call from a UI callback, after ap_ensure_bufs)
; =============================================================================
apw_parse:
    APCNT apc_parse
    push es                         ; (a callback's ES is the kernel's)
    mov es, [ap_la_seg]             ; ES = the window, for the whole walk
    mov [apw_name], si
    mov [apw_flen], ax
    mov [apw_flen+2], dx
    mov byte [apw_gotfmt], 0
    mov byte [apw_spk], 0

    ; --- cluster size in bytes -> apw_clu, read cap -> apw_rdcap ------------
    call OSAPI_FILE_DFREE           ; BX = sectors/cluster, no disk I/O
    jc .eio
    mov ax, bx
    mov cl, 9
    shl ax, cl                     ; * 512
    mov [apw_clu], ax
    cmp ax, AP_RD_CHUNK             ; a cluster a 16 KB gulp cannot hold
    ja .eclu                        ; (FAT16's 32 KB): the reason, not "Cannot
    mov [apw_rdcap], ax             ; read the file" on a readable disk

    ; --- first window at offset 0 -----------------------------------------
    xor ax, ax
    mov [apw_winb], ax
    mov [apw_winb+2], ax
    mov byte [apw_winn], 1
    call apw_readwin
    jc .eio
    cmp word [apw_winlen], 44
    jb .eshort

    xor si, si
    mov di, apw_s_riff
    call apw_cmp4
    jne .eriff
    mov si, 8
    mov di, apw_s_wave
    call apw_cmp4
    jne .eriff

    mov word [apw_pos], 12          ; window cursor
    mov word [apw_lpos], 12         ; logical file position of the cursor
    mov word [apw_lpos+2], 0

    jmp short .walk

; the head's refusals (each tail is a MOV and a short JMP: an 8086 Jcc
; reaches 127 bytes, and one that cannot costs a JMP NEAR besides)
.eio:     mov ax, apw_e_io
          jmp short .err0
.eclu:    mov ax, apw_e_clu
          jmp short .err0
.eshort:  mov ax, apw_e_short
          jmp short .err0
.eriff:   mov ax, apw_e_riff
.err0:
    mov [apw_err], ax
    stc
    pop es
    ret

; -----------------------------------------------------------------------------
.walk:
    mov ax, [apw_pos]
    add ax, 8
    cmp ax, [apw_winlen]
    jbe .havehdr
    call apw_slide                  ; bring the cursor into a fresh window
    jc .slidefail
    jmp .walk
.havehdr:
    mov si, [apw_pos]               ; SI = BX = the chunk, in the window, for
    mov bx, si                      ; the whole of this step
    mov ax, [es:bx+4]
    mov dx, [es:bx+6]
    mov [apw_cksz], ax
    mov [apw_cksz+2], dx

    mov di, apw_s_fmt               ; the three ids we know, four bytes apart
    call apw_cmp4                   ; (apw_cmp4 preserves every register)
    je .isfmt
    add di, apw_s_data - apw_s_fmt
    call apw_cmp4
    je .isdata
    add di, apw_s_spk - apw_s_data
    call apw_cmp4
    jne .skip
    cmp ax, 4                       ; 'o8sp' (SPEC.md 86.21): shaped for the
    jb .skip                        ; speaker on the host - kind 1 samples,
    mov al, [es:bx+8]               ; kind 2 the counts themselves. A chunk
    cmp al, 2                       ; cut short is ignored, not trusted
    ja .skip
    mov [apw_spk], al
    jmp .skip

; --- skip an unrecognised chunk (or fmt, once consumed) -----------------
.skip:
    mov ax, [apw_cksz]
    mov dx, [apw_cksz+2]
    add ax, 8
    adc dx, 0
    test al, 1                     ; RIFF pad byte on an odd body (8 is even)
    jz .nopad
    add ax, 1
    adc dx, 0
.nopad:
    or ax, ax
    jnz .adv
    or dx, dx
    jz .ebig                       ; zero-length chunk would stall the walk
.adv:
    add [apw_lpos], ax
    adc [apw_lpos+2], dx
    or dx, dx
    jnz .slidenow
    add ax, [apw_pos]
    jc .slidenow                    ; the sum wrapped (a 64 KB tag block): past
    cmp ax, [apw_winlen]            ; the window, not inside it
    ja .slidenow
    mov [apw_pos], ax
    jmp .walk
.slidenow:
    call apw_slide
    jc .slidefail
    jmp .walk

.slidefail:
    ; CF from apw_slide: ran out of windows, or the file ended first
    mov ax, [apw_winb]
    mov dx, [apw_winb+2]
    add ax, [apw_winlen]
    adc dx, 0
    cmp dx, [apw_flen+2]
    jb .ebig
    ja .enodat
    cmp ax, [apw_flen]
    jae .enodat
    ; (falls into .ebig)

; the walk's refusals
.ebig:    mov ax, apw_e_big
          jmp .err
.enodat:  mov ax, apw_e_nodat
          jmp .err

; --- fmt ------------------------------------------------------------------
.isfmt:
    cmp ax, 16
    jb .eblk
    add ax, bx
    jc .ebig                        ; ...a size that wrapped the word is too big
    add ax, 8                       ; end of fmt body in window coords
    jc .ebig
    cmp ax, [apw_winlen]
    ja .ebig                        ; a fmt we cannot fully see
    mov ax, [es:bx+8+0]             ; -> fmt body, at +8
    mov [apw_tag], ax
    cmp word [es:bx+8+2], 1
    jne .echan
    cmp word [es:bx+8+6], 0         ; nSamplesPerSec high word
    jne .erate
    mov ax, [es:bx+8+4]
    mov [apw_rate], ax
    mov ax, [es:bx+8+12]
    mov [apw_balign], ax
    mov ax, [es:bx+8+14]
    mov [apw_bits], ax
    mov word [apw_spb], 1
    cmp word [apw_tag], 0x0011
    jne .fmtdone
    cmp word [apw_cksz], 20
    jb .espb
    mov ax, [es:bx+8+18]
    mov [apw_spb], ax
.fmtdone:
    mov byte [apw_gotfmt], 1
    jmp .skip

; ...and the record's
.echan:   mov ax, apw_e_chan
          jmp short .err
.erate:   mov ax, apw_e_rate
          jmp short .err
.etag:    mov ax, apw_e_tag
          jmp short .err
.ebits:   mov ax, apw_e_bits
          jmp short .err
.espb:    mov ax, apw_e_spb
          jmp short .err
.enofmt:  mov ax, apw_e_nofmt
          jmp short .err
.eblk:    mov ax, apw_e_blk
.err:
    mov [apw_err], ax
    stc
.out:
    pop es
    ret


.datok:
    ; --- the final field checks: format tag -> apw_fmt ---------------------
    mov ax, [apw_balign]            ; (for ADPCM)
    mov cx, [apw_bits]
    cmp word [apw_tag], 0x0001
    je .pcm
    cmp word [apw_tag], 0x0011
    jne .etag
    cmp cx, 4
    jne .ebits
    mov byte [apw_fmt], AP_FMT_ADPCM
    ; blockalign: 8 <= balign <= APD_BLKMAX, and (balign-4)*2 + 1 == spb.
    ; Below 8, 2048 input bytes can yield under 2048 samples, and apd_pull's
    ; 4-byte block header is not resumable across a dry ring.
    cmp ax, 8
    jb .eblk
    cmp ax, APD_BLKMAX
    ja .eblk
    sub ax, 4
    add ax, ax
    inc ax                         ; AX = derived samples/block
    cmp ax, [apw_spb]
    jne .espb
    jmp short .rate
.pcm:
    cmp cx, 8
    jne .ebits
    mov byte [apw_fmt], AP_FMT_PCM8
    ; PCM blockalign is normally 1; tolerate anything, treat as 1
.rate:
    push ds
    pop es                         ; (.out restores the caller's ES)
    mov ax, [apw_rate]
    mov di, apw_rates
    mov cx, APW_NRATES
    cld
    repne scasw
    jne .erate
    clc
    jmp .out

; --- data ----------------------------------------------------------------
.isdata:
    cmp byte [apw_gotfmt], 0
    je .enofmt
    mov ax, [apw_lpos]
    mov dx, [apw_lpos+2]
    add ax, 8
    adc dx, 0
    mov [apw_datoff], ax
    mov [apw_datoff+2], dx
    mov cx, [apw_cksz]
    mov bx, [apw_cksz+2]
    mov [apw_datlen], cx
    mov [apw_datlen+2], bx
    ; clamp data end to the file length
    add ax, cx
    adc dx, bx
    jc .datclamp                   ; past 4 GB: the sum wrapped, so the
                                   ; compare below would pass it unclamped
    cmp dx, [apw_flen+2]
    jb .dgo
    ja .datclamp
    cmp ax, [apw_flen]
    jbe .dgo
.datclamp:
    mov ax, [apw_flen]
    mov dx, [apw_flen+2]
    sub ax, [apw_datoff]
    sbb dx, [apw_datoff+2]
    mov [apw_datlen], ax
    mov [apw_datlen+2], dx
.dgo:
    jmp .datok                     ; (the checks sit by their refusals)


; =============================================================================
; apw_readwin - read apw_rdcap bytes at apw_winb (cluster-aligned) into the
;               window, ES:0.  Sets apw_winlen.  CF on I/O error.
; =============================================================================
apw_readwin:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, [apw_name]
    xor bx, bx
    mov cx, [apw_rdcap]
    mov ax, [apw_winb]
    mov dx, [apw_winb+2]
    call OSAPI_FILE_READ_AT         ; out CF, DX:AX = bytes delivered
    APCNT apc_readat
    jc .err
    APADD apc_rbytes, ax
    ; DX:AX bytes; cap fits a word (<= AP_RD_CHUNK)
    or dx, dx
    jz .stlen
    mov ax, AP_RD_CHUNK
.stlen:
    mov [apw_winlen], ax
    clc
    jmp .out
.err:
    mov word [apw_winlen], 0
    stc
.out:
    pop es
    jmp ap_ret6

; =============================================================================
; apw_slide - move the window so apw_lpos is visible; set apw_pos.
;             CF = 1 when out of windows or the file ended before lpos.
; =============================================================================
apw_slide:
    APCNT apc_slide
    push ax
    push dx
    cmp byte [apw_winn], APW_WINDOWS
    jae .fail
    ; new base = apw_lpos rounded down to apw_clu
    mov ax, [apw_lpos]
    mov dx, [apw_lpos+2]
    push bx
    mov bx, [apw_clu]
    dec bx                          ; mask (clu is a power of two)
    not bx
    and ax, bx
    pop bx
    ; (apw_clu <= 8192, so masking only the low word is correct)
    mov [apw_winb], ax
    mov [apw_winb+2], dx
    inc byte [apw_winn]
    call apw_readwin
    jc .fail
    ; cursor within the new window
    mov ax, [apw_lpos]
    sub ax, [apw_winb]             ; high words are equal by construction
    mov [apw_pos], ax
    cmp ax, [apw_winlen]
    jae .fail                      ; lpos is past the bytes we could read
    clc
    pop dx
    pop ax
    ret
.fail:
    stc
    pop dx
    pop ax
    ret

; =============================================================================
; apw_cmp4 - compare 4 bytes of the window at ES:SI with DS:DI.  ZF = 1
;            when equal.  preserves every register except flags.
; =============================================================================
apw_cmp4:
    push si
    push di
    push cx
    xchg si, di                    ; CMPSB compares DS:SI with ES:DI
    mov cx, 4
    cld
    repe cmpsb
    pop cx
    pop di
    pop si
    ret
