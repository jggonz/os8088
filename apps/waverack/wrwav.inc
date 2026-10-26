; =============================================================================
; os8088 - apps/waverack/wrwav.inc - WaveRack's RIFF/WAVE reader (SPEC.md 122.4)
;
; THE READER walks the chunk list with OSAPI_FILE_READ_AT at 32-bit offsets.
; It accepts any chunk order, honours the odd-length pad byte, skips every
; chunk it does not know, and refuses after WW_NCHUNK = 64 chunk headers.
; Every length is checked against the file's size before it is trusted, and
; the carry of every 32-bit sum is tested. tools/wrsim.py's parse() is the
; same walk in Python - the reference the soak row holds this to, refusal
; code for refusal code ([ww_err], the E_* below and wrsim.py's).
;
; NOTHING HERE ASSUMES WHERE A CHUNK STARTS: ww_peek hands back any 1..48
; bytes of the file at any offset, reading the cluster-aligned window that
; holds them into the document's I/O claim when the window it has does not
; (READ_AT wants a cluster multiple for both the offset and the length - two
; clusters at most, so a header costs one read whatever its shape).
;
; Accepted: WAVE_FORMAT_PCM, and WAVE_FORMAT_EXTENSIBLE whose sub-format is
; PCM's GUID; 8 or 16 bits; 1 or 2 channels; the block align that implies;
; 1,000..65,535 Hz. A data size past the end is CLAMPED, one that is not a
; whole number of frames FLOORED (the status bar says how many bytes went), and
; a take that never got its header - data size 0 or FFFFFFFFh with bytes after
; it - is RECOVERED from the file's length and opens dirty.
; =============================================================================

WW_NCHUNK   equ 64

; --- the refusal codes: tools/wrsim.py's E_* - the soak row compares them ---
E_OK        equ 0
E_NOTRIFF   equ 1
E_NOFMT     equ 2
E_NODATA    equ 3
E_NOTPCM    equ 4
E_BITS      equ 5
E_CHANS     equ 6
E_BALIGN    equ 7
E_RATE      equ 8
E_CHUNKS    equ 9
E_BIG       equ 10
E_PACKED    equ 11
E_IO        equ 12
E_CLUSTER   equ 13
E_NOFILE    equ 14
E_MEM       equ 15
E_BADFMT    equ 16
; --- ...and the notes an accepted file may carry: wrsim.py's N_*
N_NONE      equ 0
N_CLAMP     equ 1
N_IGNORED   equ 2
N_RECOVER   equ 3

; -----------------------------------------------------------------------------
; ww_peek - CX (1..48) bytes of the file [wo_nbuf] at offset DX:AX
; out: CF = 0 ES:SI = them, in the I/O claim; CF = 1 [ww_err] = E_IO (the read
;      failed, or the file ended inside the bytes - callers bound-check
;      against [ww_flen] first, so this is a disk that lied).
;      Keeps AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
ww_peek:
    push ax
    push bx
    push cx
    push dx
    push di
    mov es, [wo_ioseg]
    cmp byte [ww_wv], 0             ; the window holds [off, off + CX)?
    je .read
    sub ax, [ww_wb]
    sbb dx, [ww_wb+2]
    jc .read0                       ; before it
    or dx, dx
    jnz .read0
    mov si, ax                      ; SI = off - base
    add ax, cx
    jc .read0
    cmp ax, [ww_wl]
    ja .read0
    clc
    jmp short .out
.read0:
    pop di                          ; (the offset back)
    pop dx
    pop cx
    pop bx
    pop ax
    push ax
    push bx
    push cx
    push dx
    push di
.read:
    mov bx, [ww_clu]                ; the window: the cluster holding off...
    dec bx
    not bx
    mov di, ax
    and di, bx                      ; DI = off & ~(clu - 1) (clu <= 8 KB: the
    mov [ww_wb], di                 ; mask is the low word's)
    mov [ww_wb+2], dx
    sub ax, di                      ; AX = the head
    mov si, ax
    add ax, cx                      ; AX = the head + the bytes wanted
    push ax
    mov cx, [ww_clu]                ; ...and two clusters of it
    shl cx, 1
    mov ax, di
    xor bx, bx
    push si
    mov si, wo_nbuf
    call OSAPI_FILE_READ_AT         ; out DX:AX = delivered
    pop si
    pop cx                          ; CX = what must be there
    mov es, [wo_ioseg]
    mov byte [ww_wv], 0
    jc .io
    or dx, dx
    jnz .io                         ; more than asked: not this slot's answer
    mov [ww_wl], ax
    mov byte [ww_wv], 1
    cmp ax, cx
    jb .io                          ; the file ended inside the bytes
    clc
    jmp short .out
.io:
    mov byte [ww_err], E_IO
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ww_parse - walk the file [wo_nbuf], [ww_flen] bytes long, in the folder the
;            instance stands in; [ww_clu] = the volume's cluster in bytes.
; out: CF = 0 accepted: ww_rate, ww_bits, ww_ch, ww_ba, ww_doff, ww_dlen
;      (clamped and floored), ww_note/ww_narg; CF = 1 refused: [ww_err], and
;      [ww_arg] the number its sentence names. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
ww_parse:
    push es
    xor ax, ax
    mov [ww_err], al
    mov [ww_arg], ax
    mov [ww_note], al
    mov [ww_narg], al
    mov [ww_wv], al
    mov [ww_n], al
    mov [ww_gotf], al
    mov [ww_gotd], al
    mov dx, [ww_flen+2]
    test dh, 0x80                   ; past 2^31 - 1 bytes (SPEC.md 122.3)
    jz .size
    mov al, E_BIG
    jmp .refuse
.size:
    or dx, dx
    jnz .head
    cmp word [ww_flen], 12
    jb .notriff
.head:
    xor ax, ax
    xor dx, dx
    mov cx, 12
    call ww_peek
    jc .ioerr
    cmp word [es:si], 'RI'
    jne .notriff
    cmp word [es:si+2], 'FF'
    jne .notriff
    cmp word [es:si+8], 'WA'
    jne .notriff
    cmp word [es:si+10], 'VE'
    jne .notriff
    mov word [ww_pos], 12
    mov word [ww_pos+2], 0
.walk:
    mov ax, [ww_pos]                ; a whole header before the end?
    mov dx, [ww_pos+2]
    add ax, 8
    adc dx, 0
    jc .walked                      ; (past 4 GB: wave 2's review, M1)
    call ww_vsflen                  ; CF = 1: DX:AX > the file's size
    jc .walked
    inc byte [ww_n]
    cmp byte [ww_n], WW_NCHUNK
    jbe .read
    mov al, E_CHUNKS
    jmp .refuse
.read:
    mov ax, [ww_pos]
    mov dx, [ww_pos+2]
    mov cx, 8
    call ww_peek
    jc .ioerr
    mov ax, [es:si+4]
    mov [ww_csz], ax
    mov ax, [es:si+6]
    mov [ww_csz+2], ax
    cmp word [es:si], 'fm'
    jne .notfmt
    cmp word [es:si+2], 't '
    jne .notfmt
    cmp byte [ww_gotf], 0
    jne .next
    call ww_fmt                     ; CF = 1: refused, AL = why
    jnc .both
    jmp .refuse
.notfmt:
    cmp word [es:si], 'da'
    jne .next
    cmp word [es:si+2], 'ta'
    jne .next
    cmp byte [ww_gotd], 0
    jne .next
    mov byte [ww_gotd], 1
    mov ax, [ww_pos]
    mov dx, [ww_pos+2]
    add ax, 8
    adc dx, 0
    mov [ww_doff], ax
    mov [ww_doff+2], dx
    mov ax, [ww_csz]
    mov [ww_dlen], ax
    mov ax, [ww_csz+2]
    mov [ww_dlen+2], ax
.both:
    mov al, [ww_gotf]               ; both found: the walk is done
    and al, [ww_gotd]
    jnz .walked
.next:
    mov ax, [ww_csz]                ; pos + 8 + size + (size & 1), every carry
    mov dx, [ww_csz+2]              ; an end
    test al, 1
    jz .even
    add ax, 1
    adc dx, 0
    jc .walked
.even:
    add ax, 8
    adc dx, 0
    jc .walked
    add [ww_pos], ax
    adc [ww_pos+2], dx
    jc .walked
    jmp .walk

.notriff:
    mov al, E_NOTRIFF
    jmp .refuse
.ioerr:
    mov al, E_IO
    jmp .refuse

; --- the walk is over: what it found -------------------------------------------
.walked:
    mov al, E_NOFMT
    cmp byte [ww_gotf], 0
    je .refuse
    mov al, E_NODATA
    cmp byte [ww_gotd], 0
    je .refuse
    mov al, E_NOTPCM
    mov bx, [ww_tag]
    cmp bx, 1
    jne .arg
    mov al, E_BITS
    mov bx, [ww_bits]
    cmp bx, 8
    je .bok
    cmp bx, 16
    jne .arg
.bok:
    mov al, E_CHANS
    mov bx, [ww_ch]
    dec bx
    cmp bx, 1
    jbe .cok
    inc bx
    jmp .arg
.cok:
    mov al, E_RATE
    cmp word [ww_rate+2], 0
    jne .refuse
    cmp word [ww_rate], 1000
    jb .refuse
    mov ax, [ww_bits]               ; the block align the format implies
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mul word [ww_ch]
    mov bx, ax
    mov al, E_BALIGN
    cmp bx, [ww_ba]
    jne .refuse
    ; --- the data's length: recovered, clamped, floored -----------------------
    mov ax, [ww_flen]               ; BX:CX = what the file has after doff
    mov dx, [ww_flen+2]
    sub ax, [ww_doff]
    sbb dx, [ww_doff+2]
    mov cx, ax
    mov bx, dx
    mov ax, [ww_dlen]
    mov dx, [ww_dlen+2]
    mov si, ax                      ; 0 or FFFFFFFFh: a take with no header
    or si, dx
    jz .unfin
    mov si, ax
    and si, dx
    inc si
    jnz .claimed
.unfin:
    mov ax, cx                      ; the file's length names the frames
    mov dx, bx
    mov si, ax
    or si, dx
    jz .setlen                      ; nothing after it: an empty sound
    mov byte [ww_note], N_RECOVER
    jmp short .setlen
.claimed:
    cmp dx, bx                      ; past the end: clamped
    jb .setlen
    ja .clamp
    cmp ax, cx
    jbe .setlen
.clamp:
    mov byte [ww_note], N_CLAMP
.avail:
    mov ax, cx
    mov dx, bx
.setlen:
    mov cx, [ww_ba]                 ; the floor: a whole number of frames
    dec cx
    and cx, ax
    jz .whole
    sub ax, cx
    cmp byte [ww_note], N_RECOVER
    je .whole
    mov byte [ww_note], N_IGNORED
    mov [ww_narg], cl
.whole:
    mov [ww_dlen], ax
    mov [ww_dlen+2], dx
    pop es
    clc
    ret
.arg:
    mov [ww_arg], bx
.refuse:
    mov [ww_err], al
    pop es
    stc
    ret

; ww_vsflen - CF = 1 when DX:AX is past [ww_flen]. keeps every register
ww_vsflen:
    cmp dx, [ww_flen+2]
    jne .done
    cmp ax, [ww_flen]
.done:
    ja .past
    clc
    ret
.past:
    stc
    ret

; -----------------------------------------------------------------------------
; ww_fmt - the `fmt ` chunk at [ww_pos], [ww_csz] long: its fields into
;          ww_tag (1 = PCM, else what it said), ww_ch, ww_rate, ww_ba,
;          ww_bits. out: CF = 1 refused, AL = E_BADFMT / E_IO.
;          clobbers AX, BX, CX, DX, SI, ES
; -----------------------------------------------------------------------------
ww_fmt:
    mov ax, [ww_flen]               ; BX:CX = what the file holds after the
    mov dx, [ww_flen+2]             ; header
    sub ax, [ww_pos]
    sbb dx, [ww_pos+2]
    sub ax, 8
    sbb dx, 0
    mov cx, ax
    mov bx, dx
    mov ax, 16                      ; the PCM form: 16 bytes in the chunk and
    call ww_fmtfits                 ; in the file
    jc .bad
    mov ax, [ww_pos]
    mov dx, [ww_pos+2]
    add ax, 8
    adc dx, 0
    mov cx, 16
    call ww_peek
    jc .io
    mov ax, [es:si]
    cmp ax, 0xFFFE
    jne .fields
    mov ax, [ww_flen]               ; EXTENSIBLE: 40 bytes, and the
    mov dx, [ww_flen+2]             ; sub-format's GUID
    sub ax, [ww_pos]
    sbb dx, [ww_pos+2]
    sub ax, 8
    sbb dx, 0
    mov cx, ax
    mov bx, dx
    mov ax, 40
    call ww_fmtfits
    jc .bad
    mov ax, [ww_pos]
    mov dx, [ww_pos+2]
    add ax, 8
    adc dx, 0
    mov cx, 40
    call ww_peek
    jc .io
    push di
    mov di, si
    add di, 24
    mov si, ww_guid                 ; 0001 and PCM's GUID tail, 16 bytes
    mov cx, 8
    cld
    repe cmpsw                      ; DS:SI against ES:DI
    pop di
    mov ax, 0xFFFE                  ; not PCM: the tag it said
    jne .store
    mov ax, 1
.store:
    push ax                         ; SI back on the body: the window holds
    mov ax, [ww_pos]                ; it, so this peek reads nothing
    mov dx, [ww_pos+2]
    add ax, 8
    adc dx, 0
    mov cx, 40
    call ww_peek
    pop ax
    jc .io
.fields:
    mov [ww_tag], ax
    mov ax, [es:si+2]
    mov [ww_ch], ax
    mov ax, [es:si+4]
    mov [ww_rate], ax
    mov ax, [es:si+6]
    mov [ww_rate+2], ax
    mov ax, [es:si+12]
    mov [ww_ba], ax
    mov ax, [es:si+14]
    mov [ww_bits], ax
    mov byte [ww_gotf], 1
    clc
    ret
.bad:
    mov al, E_BADFMT
    stc
    ret
.io:
    mov al, E_IO
    stc
    ret

; ww_fmtfits - CF = 0 when the chunk's size [ww_csz] and the file's rest BX:CX
; both hold AX bytes. keeps every register
ww_fmtfits:
    cmp word [ww_csz+2], 0
    jne .file
    cmp [ww_csz], ax
    jb .no
.file:
    or bx, bx
    jnz .yes
    cmp cx, ax
    jb .no
.yes:
    clc
    ret
.no:
    stc
    ret

; the extensible format's sub-format, PCM's: its first word and GUID tail
ww_guid:    dw 1
            db 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00
            db 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71

; -----------------------------------------------------------------------------
; ww_say - the status bar's sentence for [ww_err] (a refusal) or, with it 0,
;          for [ww_note] (wrsim.py's sentence(): the same words). CF = 1:
;          there was nothing to say. clobbers AX, SI, DI
; -----------------------------------------------------------------------------
ww_say:
    mov di, wr_tbuf2
    mov al, [ww_err]
    or al, al
    jz .note
    cmp al, E_NOTPCM
    jne .bits
    mov si, ww_s_fmt                ; 'Format 0011h: not PCM'
    call wr_put
    mov ax, [ww_arg]
    call wr_hex4
    mov si, ww_s_notpcm
    jmp short .tail
.bits:
    cmp al, E_BITS
    jne .chans
    mov ax, [ww_arg]                ; '24-bit is unsupported'
    call dec_u16
    mov si, ww_s_bits
    jmp short .tail
.chans:
    cmp al, E_CHANS
    jne .plain
    mov ax, [ww_arg]                ; '3 channels unsupported'
    call dec_u16
    mov si, ww_s_chans
    jmp short .tail
.plain:
    xor ah, ah                      ; the sentence table
    mov si, ax
    shl si, 1
    mov si, [ww_errs + si]
    jmp short .tail
.note:
    mov al, [ww_note]
    cmp al, N_CLAMP
    jne .n2
    mov si, ww_s_clamp
    jmp short .tail
.n2:
    cmp al, N_RECOVER
    jne .n3
    mov si, ww_s_recov
    jmp short .tail
.n3:
    cmp al, N_IGNORED
    jne .none
    mov al, [ww_narg]               ; '3 bytes ignored at end'
    xor ah, ah
    call dec_u16
    mov si, ww_s_byte
    call wr_put
    cmp byte [ww_narg], 1
    je .one
    mov byte [di], 's'
    inc di
.one:
    mov si, ww_s_ign
.tail:
    call wr_put
    mov byte [di], 0
    mov si, wr_tbuf2
    call wr_say
    clc
    ret
.none:
    stc
    ret

ww_errs:    dw 0, ww_e1, ww_e2, ww_e3, 0, 0, 0, ww_e7, ww_e8, ww_e9, ww_e10
            dw ww_e11, ww_e12, ww_e13, ww_e14, ww_e15, ww_e16
ww_e1:      db 'Not a RIFF/WAVE file', 0
ww_e2:      db 'No fmt chunk', 0
ww_e3:      db 'No data chunk', 0
ww_e7:      db 'Bad block align', 0
ww_e8:      db 'Bad sample rate', 0
ww_e9:      db 'More than 64 chunks', 0
ww_e10:     db 'File over 2 GB', 0
ww_e11:     db 'File is compressed', 0
ww_e12:     db 'Cannot read the file', 0
ww_e13:     db 'Cluster too large', 0
ww_e14:     db 'File not found', 0
ww_e15:     db 'Not enough memory', 0
ww_e16:     db 'Bad fmt chunk', 0
ww_s_fmt:   db 'Format ', 0
ww_s_notpcm: db 'h: not PCM', 0
ww_s_bits:  db '-bit is unsupported', 0
ww_s_chans: db ' channels unsupported', 0
ww_s_clamp: db 'Data clamped to file', 0
ww_s_recov: db 'Recovered; save to fix', 0
ww_s_byte:  db ' byte', 0
ww_s_ign:   db ' ignored at end', 0
