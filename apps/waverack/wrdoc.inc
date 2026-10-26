; =============================================================================
; os8088 - apps/waverack/wrdoc.inc - the document (SPEC.md 122.3)
;
; THE CANONICAL UNIT IS THE SAMPLE FRAME: one sample per channel. Every
; position, length and offset is a frame index in 32 bits (DX:AX), and a
; byte offset is formed only at the moment a file is read, as `data offset +
; frame << shift` - the block align is 1, 2 or 4, so a shift.
;
; SOURCES (WO_NSRC = 4) are the files a document's audio lives in: the
; opened WAV now, a take and the clipboard's later. Each records its 8.3
; name, its folder (OSAPI_FILE_HERE's cluster and volume, restood with
; OSAPI_FILE_GOTO_QM before a read - SPEC.md 86.3's lesson), its data offset,
; its frame count and format, its cluster size, and its coarse envelope's
; claim and block shift (wrenv.inc). A source is never written.
;
; THE PIECE TABLE (WO_NPC = 64) is the document: an ordered list of {source,
; first frame, frame count}. An open makes one piece; the edits of wave 4
; build new lists and move no audio. Undo and the clipboard are wave 4's
; (SPEC.md 122.3): their hooks are wo_npc and the immutable sources.
;
; READING IS THE UI TASK'S (SPEC.md 20.6 rule 7): wo_read is called from a
; callback, never from a worker, and READ_AT wants a cluster multiple for both
; the offset and the length, so a read takes the cluster-aligned run holding
; the frame into the I/O claim and answers where the frame landed in it.
; =============================================================================

WO_NSRC     equ 4
WO_NPC      equ 64
WO_IOKB     equ 16                  ; the I/O claim: a scan gulp (SPEC.md 122.5)
WO_MAXCLU   equ 8192                ; a cluster two of which fit the claim

; --- a source ----------------------------------------------------------------------
WS_NAME     equ 0                   ; 13: the NUL 8.3 name
WS_VOL      equ 13                  ; byte: its volume...
WS_DIR      equ 14                  ; word: ...and folder (OSAPI_FILE_HERE)
WS_DOFF     equ 16                  ; dword: the data's offset in the file
WS_FRM      equ 20                  ; dword: its frames
WS_ENV      equ 24                  ; word: the envelope's claim, 0 none
WS_BS       equ 26                  ; byte: the envelope's block shift
WS_USED     equ 27                  ; byte: 1 = a live source
WS_NBLK     equ 28                  ; word: blocks in the envelope
WS_CLU      equ 30                  ; word: its volume's cluster, in bytes
WS_RATE     equ 32                  ; word: the source's format (a document's
WS_BITS     equ 34                  ; byte  sources share it: paste of another
WS_CH       equ 35                  ; byte  format is refused, SPEC.md 122.3)
WS_SH       equ 36                  ; byte: log2 of the block align
WS_SIZE     equ 40
; --- a piece -----------------------------------------------------------------------
WP_SRC      equ 0                   ; word: the source's index
WP_FIRST    equ 2                   ; dword: its first frame in the source
WP_CNT      equ 6                   ; dword: how many
WP_SIZE     equ 10

; -----------------------------------------------------------------------------
; wo_new - EMPTY THE DOCUMENT: the scan stopped, every claim the document
;          holds given back, the view and the cursor at nought, every
;          generation moved so each element draws the empty face again.
;          The status bar is the caller's. clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wo_new:
    mov byte [we_busy], 0           ; a scan or a refinement: abandoned
    mov si, wo_src
    mov cx, WO_NSRC
.src:
    mov dx, [si+WS_ENV]
    call wo_free
    mov [si+WS_ENV], dx
    mov byte [si+WS_USED], 0
    add si, WS_SIZE
    loop .src
    mov dx, [wo_ioseg]
    call wo_free
    mov [wo_ioseg], dx
    mov dx, [we_vseg]
    call wo_free
    mov [we_vseg], dx
    push es                         ; every document byte at nought
    push ds
    pop es
    mov di, wo_zero0
    mov cx, wo_zero1 - wo_zero0
    xor al, al
    cld
    rep stosb
    pop es
    ; (falls into wo_moved)

; wo_moved - every element that shows the document is owed a draw. keeps all
wo_moved:
    call wo_gen1
    call we_moved
    ; (falls into we_cmoved)

; we_cmoved - the cursor moved: the time and the line are owed. keeps all
we_cmoved:
    push ax
    mov ax, [we_cgen]
    call wo_inc
    mov [we_cgen], ax
    pop ax
    ret

; wo_gen1 - the document's own generation. keeps all
wo_gen1:
    push ax
    mov ax, [wo_gen]
    call wo_inc
    mov [wo_gen], ax
    pop ax
    ret

; wo_inc - AX + 1 in a key's range: 0..7FFFh, never the forgotten FFFFh
wo_inc:
    inc ax
    and ax, 0x7FFF
    ret

; wo_free - DX = a claim or 0: freed. out DX = 0. keeps the rest
wo_free:
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    xor dx, dx
.out:
    ret

; -----------------------------------------------------------------------------
; wo_find - [wo_nbuf] in the folder the instance stands in: its RAW size
;           (what READ_AT reads) into [ww_flen].
; out: CF = 0 found, AL = 0 plain / 1 compressed (OSAPI_FIND_CZ); CF = 1 no
;      such file. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wo_find:
    push es
    push ds
    pop es
    xor cx, cx                      ; ordinal 0
.next:
    mov di, wo_fbuf
    push cx
    call OSAPI_FILE_FIND_RAW        ; +18 the size the file OCCUPIES
    pop bx
    jc .none
    cmp word [wo_fbuf + 14], OSAPI_FT_DIR
    jae .next
    mov si, wo_fbuf                 ; the name, any case
    mov di, wo_nbuf
.cmp:
    mov al, [si]
    call wr_upper
    mov ah, al
    mov al, [di]
    call wr_upper
    cmp al, ah
    jne .next
    inc si
    inc di
    or al, al
    jnz .cmp
    mov ax, [wo_fbuf + 18]
    mov [ww_flen], ax
    mov ax, [wo_fbuf + 20]
    mov [ww_flen+2], ax
    mov al, [wo_fbuf + 22]
    and al, OSAPI_FIND_CZ
    pop es
    clc
    ret
.none:
    pop es
    stc
    ret

; -----------------------------------------------------------------------------
; wo_open - OPEN [wo_nbuf], in the folder the instance stands in, as the
;           document: whatever was open is emptied first (SPEC.md 122.3 -
;           the dirty question is wave 4's), the header walked (wrwav.inc),
;           the claims made, one piece laid, the view fitted and the scan
;           begun. A refusal leaves the document EMPTY and says why.
; in:  BX = the window. out: CF = 0 opened (the scan running), CF = 1
;      refused ([ww_err], and the status bar). clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wo_open:
    push bx
    call wo_new
    mov byte [ww_note], N_NONE
    call wo_find
    mov bl, E_NOFILE
    jc .refuse
    mov bl, E_PACKED                ; a compressed file reads raw: its bytes
    or al, al                       ; are not the sound's
    jnz .refuse
    call OSAPI_FILE_DFREE           ; BX = sectors a cluster (no disk I/O)
    mov ax, bx
    mov bl, E_CLUSTER
    jc .refuse
    or ax, ax
    jz .refuse
    mov cl, 9
    shl ax, cl
    jc .refuse
    cmp ax, WO_MAXCLU
    ja .refuse
    mov [ww_clu], ax
    mov bl, E_MEM
    cmp word [wr_bseg], 0           ; the waveform band, claimed at entry
    je .refuse
    mov ax, WO_IOKB
    call OSAPI_MEM_CLAIM
    jc .refuse
    mov [wo_ioseg], dx
    call ww_parse                   ; CF = 1: [ww_err]
    jnc .parsed
    mov bl, [ww_err]
    jmp .refuse
.parsed:
    ; --- source 0 --------------------------------------------------------------
    mov di, wo_src
    mov si, wo_nbuf
    mov cx, 13
.name:
    lodsb
    mov [di], al
    inc di
    loop .name
    mov di, wo_src
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the volume
    mov [di+WS_DIR], dx
    mov [di+WS_VOL], bl
    mov ax, [ww_doff]
    mov [di+WS_DOFF], ax
    mov ax, [ww_doff+2]
    mov [di+WS_DOFF+2], ax
    mov ax, [ww_clu]
    mov [di+WS_CLU], ax
    mov ax, [ww_rate]
    mov [di+WS_RATE], ax
    mov [wo_rate], ax
    mov al, [ww_bits]
    mov [di+WS_BITS], al
    mov [wo_bits], al
    mov al, [ww_ch]
    mov [di+WS_CH], al
    mov [wo_ch], al
    mov al, [ww_ba]
    mov [wo_ba], al
    xor cl, cl                      ; the shift: 1 -> 0, 2 -> 1, 4 -> 2
.sh:
    shr al, 1
    jz .shd
    inc cl
    jmp short .sh
.shd:
    mov [di+WS_SH], cl
    mov [wo_sh], cl
    mov ax, [ww_dlen]               ; frames = the data >> shift
    mov dx, [ww_dlen+2]
    jcxz .frm
.fsh:
    shr dx, 1
    rcr ax, 1
    loop .fsh
.frm:
    mov [di+WS_FRM], ax
    mov [di+WS_FRM+2], dx
    mov [wo_frames], ax
    mov [wo_frames+2], dx
    ; --- its envelope's claim (SPEC.md 122.5) and the view cache ----------------
    call we_envsize                 ; CL = the block shift, BX = the blocks,
    mov [di+WS_BS], cl              ; AX = the claim in KB (0: an empty sound)
    mov [di+WS_NBLK], bx
    or ax, ax
    jz .noenv
    call OSAPI_MEM_CLAIM
    mov bl, E_MEM
    jc .refuse
    mov [di+WS_ENV], dx
.noenv:
    mov ax, WE_VKB
    call OSAPI_MEM_CLAIM
    mov bl, E_MEM
    jc .refuse
    mov [we_vseg], dx
    mov byte [di+WS_USED], 1
    ; --- the document: one piece, the whole source --------------------------------
    mov word [wo_pc + WP_SRC], 0
    xor ax, ax
    mov [wo_pc + WP_FIRST], ax
    mov [wo_pc + WP_FIRST+2], ax
    mov ax, [wo_frames]
    mov [wo_pc + WP_CNT], ax
    mov ax, [wo_frames+2]
    mov [wo_pc + WP_CNT+2], ax
    mov word [wo_npc], 1
    mov si, wo_nbuf                 ; the document's name, as the LCD shows it
    mov di, wo_name
    mov cx, 13
.dname:
    lodsb
    call wr_upper
    mov [di], al
    inc di
    loop .dname
    xor al, al
    cmp byte [ww_note], N_RECOVER   ; a recovered take is dirty (SPEC.md
    jne .clean                      ; 122.4): saved, it is repaired
    inc al
.clean:
    mov [wo_dirty], al
    mov byte [wo_doc], 1
    call wo_moved
    pop bx
    call we_open                    ; the view fitted, the scan begun
    clc
    ret
.refuse:
    mov [ww_err], bl
    call wo_new                     ; every claim back: an EMPTY document
    mov al, [ww_err]
    mov [ww_err], al
    call ww_say
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; wo_locate - which piece holds document frame DX:AX (< [wo_frames])
; out: SI = the piece, DX:AX = the frame's offset inside it, CX:BX = the
;      frames left in the piece from there. CF = 1: past the end
; -----------------------------------------------------------------------------
wo_locate:
    push di
    mov si, wo_pc
    mov di, [wo_npc]
.p:
    or di, di
    jz .end
    cmp dx, [si+WP_CNT+2]           ; inside this one?
    jb .in
    ja .over
    cmp ax, [si+WP_CNT]
    jb .in
.over:
    sub ax, [si+WP_CNT]
    sbb dx, [si+WP_CNT+2]
    add si, WP_SIZE
    dec di
    jmp short .p
.in:
    mov bx, [si+WP_CNT]
    mov cx, [si+WP_CNT+2]
    sub bx, ax
    sbb cx, dx
    pop di
    clc
    ret
.end:
    pop di
    stc
    ret

; -----------------------------------------------------------------------------
; wo_read - READ DOCUMENT FRAMES: from frame DX:AX, at most CX of them, in
;           one read of BP bytes (two clusters at least; at most the claim).
;           UI task only: READ_AT, after restanding in the source's folder.
; out: CF = 0, ES:DI = the first frame in the I/O claim, CX = the frames
;      there (all in one piece; 0 = at the end); CF = 1 a read error.
;      clobbers AX, BX, DX, SI
; -----------------------------------------------------------------------------
wo_read:
    push cx
    call wo_locate                  ; SI = the piece, DX:AX = the offset in
    pop di                          ; it, CX:BX = what is left; DI = asked
    jc wo_rdend
    or cx, cx                       ; at most what is left of the piece
    jnz .ask
    cmp bx, di
    jae .ask
    mov di, bx
.ask:
    mov cx, di
    add ax, [si+WP_FIRST]           ; the source's own frame
    adc dx, [si+WP_FIRST+2]
    push ax
    push dx
    mov ax, [si+WP_SRC]
    mov bx, WS_SIZE
    mul bx
    add ax, wo_src
    mov si, ax                      ; SI = the source record
    pop dx
    pop ax
    ; (falls into wo_srcread)

; -----------------------------------------------------------------------------
; wo_srcread - READ SOURCE FRAMES: source SI, its frame DX:AX, at most CX, in
;              one read of BP bytes (see wo_read). The I/O claim remembers
;              the run it holds, so a second read of it costs nothing.
; out: as wo_read. clobbers AX, BX, DX, SI
; -----------------------------------------------------------------------------
wo_srcread:
    push bp
    mov bx, [si+WS_FRM]             ; at most what the source has from there
    mov di, [si+WS_FRM+2]
    sub bx, ax
    sbb di, dx
    jc .end0
    or di, di
    jnz .keep
    cmp bx, cx
    jae .keep
    mov cx, bx
.keep:
    or cx, cx
    jnz .some
    jmp .end0
.some:
    mov [wo_rmax], cx
    mov cl, [si+WS_SH]              ; the byte offset: doff + frame << shift
    xor ch, ch
    jcxz .noshift
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.noshift:
    add ax, [si+WS_DOFF]
    adc dx, [si+WS_DOFF+2]
    mov bx, [si+WS_CLU]             ; the cluster holding it, and the head
    dec bx
    mov di, ax
    and di, bx                      ; DI = the head (< a cluster)
    sub ax, di                      ; DX:AX = the cluster's offset
    mov cx, [si+WS_CLU]             ; the read: BP bytes, two clusters at
    shl cx, 1                       ; least (a head of almost one, and a frame)
    cmp bp, cx
    jbe .cap
    mov cx, bp
.cap:
    cmp byte [wo_cv], 0             ; the claim holds that run already?
    je .read
    cmp si, [wo_csrc]
    jne .read
    cmp ax, [wo_cbase]
    jne .read
    cmp dx, [wo_cbase+2]
    jne .read
    cmp cx, [wo_ccap]
    jne .read
    mov ax, [wo_clen]
    jmp short .got
.read:
    mov byte [wo_cv], 0
    call wo_stand                   ; in the source's folder
    jc .err
    push si
    push di
    mov [wo_csrc], si
    mov [wo_cbase], ax
    mov [wo_cbase+2], dx
    mov [wo_ccap], cx
    mov es, [wo_ioseg]
    xor bx, bx                      ; (SI = the record = its name, WS_NAME 0)
    call OSAPI_FILE_READ_AT         ; out DX:AX = delivered
    pop di
    pop si
    jc .err
    or dx, dx
    jnz .err
    mov [wo_clen], ax
    mov byte [wo_cv], 1
.got:
    sub ax, di                      ; the bytes from the frame on
    jbe .end0
    mov cl, [si+WS_SH]
    shr ax, cl
    cmp ax, [wo_rmax]
    jbe .n
    mov ax, [wo_rmax]
.n:
    mov cx, ax
    mov es, [wo_ioseg]
    pop bp
    clc
    ret
.err:
    mov byte [wo_cv], 0
    mov es, [wo_ioseg]
    pop bp
    stc
    ret
.end0:
    pop bp
wo_rdend:
    xor cx, cx
    mov es, [wo_ioseg]
    xor di, di
    clc
    ret

; wo_stand - stand in source SI's folder (OSAPI_FILE_GOTO_QM, only when the
; instance is elsewhere: a dialog may have moved it). CF = 1 the folder is
; gone. keeps every register
wo_stand:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the volume
    cmp dx, [si+WS_DIR]
    jne .go
    cmp bl, [si+WS_VOL]
    je .here
.go:
    mov dx, [si+WS_DIR]
    mov bl, [si+WS_VOL]
    call OSAPI_FILE_GOTO_QM
    jmp short .out
.here:
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; 32-BIT ARITHMETIC (frames are dwords; the 8086 has 16-bit multiply/divide)
; =============================================================================

; -----------------------------------------------------------------------------
; wo_muldiv - DX:AX * BX / CX, the product held in 48 bits, the quotient's low
;             32 bits in DX:AX (the callers' quotients fit). CX != 0.
;             out BX = the remainder. keeps CX, SI, DI
; -----------------------------------------------------------------------------
wo_muldiv:
    push si
    push di
    mov si, dx                      ; the high word
    mul bx                          ; DX:AX = low * BX
    mov di, ax                      ; DI = p0
    push dx
    mov ax, si
    mul bx                          ; DX:AX = high * BX
    pop si
    add ax, si
    adc dx, 0                       ; DX:AX = p2:p1
    mov si, ax
    mov ax, dx
    xor dx, dx
    div cx                          ; p2 / CX (its quotient is dropped)
    mov ax, si
    div cx                          ; (rem:p1) / CX = q1
    mov si, ax
    mov ax, di
    div cx                          ; (rem:p0) / CX = q0
    mov bx, dx
    mov dx, si
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; wo_div32 - DX:AX / CX:BX, both unsigned 32 (CX:BX != 0), by shift and
;            subtract. out DX:AX = the quotient, CX:BX = the remainder.
;            keeps SI, DI
; -----------------------------------------------------------------------------
wo_div32:
    push si
    push di
    push bp
    mov si, bx                      ; DI:SI = the divisor
    mov di, cx
    xor bx, bx                      ; CX:BX = the remainder
    xor cx, cx
    mov bp, 32
.bit:
    shl ax, 1                       ; the dividend's top bit into the
    rcl dx, 1                       ; remainder; the quotient's bit in at
    rcl bx, 1                       ; the dividend's bottom
    rcl cx, 1
    cmp cx, di
    jb .no
    ja .yes
    cmp bx, si
    jb .no
.yes:
    sub bx, si
    sbb cx, di
    inc ax
.no:
    dec bp
    jnz .bit
    pop bp
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; wo_time - frame DX:AX as the LCD's 'MM:SS.cc' at DI (8 characters and a
;           NUL), at the document's rate; '--:--.--' past 99 minutes, and
;           00:00.00 with no document. TWO STEPS - the seconds, then the
;           remainder's hundredths - so 2^31 frames never overflow.
;           clobbers AX, BX, CX, DX
; -----------------------------------------------------------------------------
wo_time:
    mov cx, [wo_rate]
    jcxz .zero
    mov bx, ax                      ; the seconds: DX:AX / rate, high first
    mov ax, dx
    xor dx, dx
    div cx
    xchg ax, bx                     ; BX = the seconds' high word
    div cx                          ; BX:AX = the seconds, DX = the remainder
    push ax
    mov ax, dx                      ; the hundredths: rem * 100 / rate
    mov dx, 100
    mul dx
    div cx
    mov cl, al                      ; CL = 0..99
    pop ax
    or bx, bx                       ; the minutes: past 99 they do not fit
    jnz .dash
    xor dx, dx
    mov bx, 60
    div bx                          ; AX = minutes, DX = seconds
    cmp ax, 99
    ja .dash
    call wo_two
    mov byte [di], ':'
    inc di
    mov ax, dx
    call wo_two
    mov byte [di], '.'
    inc di
    mov al, cl
    xor ah, ah
    call wo_two
    mov byte [di], 0
    ret
.zero:
    mov ax, '00'
    mov [di], ax
    mov byte [di+2], ':'
    mov [di+3], ax
    mov byte [di+5], '.'
    mov [di+6], ax
    mov byte [di+8], 0
    ret
.dash:
    mov ax, '--'
    mov [di], ax
    mov byte [di+2], ':'
    mov [di+3], ax
    mov byte [di+5], '.'
    mov [di+6], ax
    mov byte [di+8], 0
    ret

; wo_two - AL (0..99) as two digits at DI; DI past them. clobbers AX
wo_two:
    aam
    add ax, '00'
    xchg al, ah
    mov [di], ax
    add di, 2
    ret
