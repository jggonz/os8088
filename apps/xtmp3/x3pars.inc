; =============================================================================
; os8088 - apps/xtmp3/x3pars.inc - XTMP3's FRONT END, frame level (SPEC.md
;                                   121.2, 121.3)
;
; tools/x3sim.py's parser, decision for decision: ID3v2 skip and title, ID3v1,
; the free-format and Layer I/II refusals, sync on an agreeing pair, Xing /
; Info / LAME / VBRI, then frame by frame the header, side information, the
; reservoir and the verdict. Granule-level work (scalefactors, Huffman, the
; exponents, the plan) is the FRONT part's (x3front.asm), far-called once a
; granule by x3p_gran.
;
; THE API IS apps/xtmp3/x3gran.inc SECTION 4 - read it there. Every proc here
; is a near proc of the PRIMARY, entered with DS = the package's segment; it
; reaches the FRONT part's fixed data block (x3gran.inc section 5) through ES
; = that part's data segment, which it loads itself, and gives ES back.
;
; THREE HALVES, by who may run them (the frame's own work, x3pnext.inc, is
; assembled into the FRONT part and reached through x3p_next):
;   x3p_open, x3p_inew, x3p_seekpos, x3p_reftext - pure: the caller's record
;       and the caller's bytes. x3p_open keeps a few words of its own (the
;       buffer it was handed), so ONE task at a time runs it.
;   x3p_probe - the UI task: it reads the file (X3P_WANT_PROBE).
;   x3p_init, x3p_start, x3p_next, x3p_gran, x3p_reset, x3p_counts - the
;       stream: x3p_next and x3p_gran on the worker, one stream at a time.
; Nothing the first half touches is touched by the third, so a probe on the
; UI task may run while the worker decodes.
;
; Labels: x3p_. The bss is X3P_BSS, laid out by the includer's X3B macro; the
; tables are X3FTAB_PARSE (build/x3ftab.inc), placed by this file.
; =============================================================================

; =============================================================================
; x3p_init - adopt the FRONT part: check its header, keep its vectors, and
;            run its CHECK (the Huffman tables' CRC-32)
; in:  AX = its DATA segment (op_seg), DX = its CODE selector (op_cseg)
; out: CF = 0; CF = 1 AX = X3E_ABI / X3E_TABLE
; =============================================================================
x3p_init:
    push bx
    push es
    mov [x3p_fds], ax
    mov [x3p_fcs], dx
    mov es, ax
    cmp word [es:X3P_SIG], X3K_SIGVAL
    jne .abi
    cmp byte [es:X3P_SIG+2], X3G_ABI
    jne .abi
    cmp byte [es:X3P_SIG+3], X3K_FRONT
    jne .abi
    mov ax, [es:X3P_VGRAN]
    mov [x3p_gvec], ax
    mov ax, [es:X3P_VNEXT]
    mov [x3p_nvec], ax
    mov bx, [es:X3P_VCHECK]
    call x3p_fcall                  ; CHECK
    jmp short .out
.abi:
    mov ax, X3E_ABI
    stc
.out:
    pop es
    pop bx
    ret

; x3p_fcall - far-call the FRONT part at offset BX, DS = its data segment for
; the call, AX as the verb takes it. out: the verb's AX and CF. keeps BX
; Built as push/push/RETF so nothing is read through CS (an execute-only
; alias under kern_mega).
x3p_fcall:
    ; STKBALANCE-OK: a far CALL built by hand - the return (CS:x3p_fret) and
    ; the target are pushed and RETF enters the part, whose own RETF comes
    ; back to x3p_fret, which pops what x3p_fcall pushed before them
    push ds
    push cx
    mov cx, [x3p_fds]
    push cs                         ; the return: a far one, to x3p_fret
    push word [x3p_fretv]
    push word [x3p_fcs]             ; the target
    push bx
    mov ds, cx
    retf                            ; = call far FRONT:BX
x3p_fret:
    ; STKBALANCE-OK: the far return of x3p_fcall's verb lands here, two
    ; pushes deep, and these pop them
    pop cx
    pop ds
    ret
x3p_fretv:  dw x3p_fret

; =============================================================================
; x3p_reftext - AL = X3REF_* -> SI = its NUL name, CF = 1 for 0
; =============================================================================
x3p_reftext:
    cmp al, X3REF_PACKED
    ja .no
    or al, al
    jz .no
    push bx
    mov bl, al
    xor bh, bh
    dec bx
    shl bx, 1
    mov si, [x3p_reft+bx]
    pop bx
    clc
    ret
.no:
    stc
    ret
x3p_reft:   dw x3p_rf1, x3p_rf2, x3p_rf3, x3p_rf4, x3p_rf5
x3p_rf1:    db 'FREE FORMAT', 0
x3p_rf2:    db 'LAYER I', 0
x3p_rf3:    db 'LAYER II', 0
x3p_rf4:    db 'NO MPEG AUDIO', 0
x3p_rf5:    db 'PACKED FILE', 0

%include "x3phdr.inc"              ; the header, shared with the FRONT part

; =============================================================================
; THE OPEN (x3sim Stream.__init__), as a state machine over the caller's reads
; =============================================================================
X3PO_HEAD   equ 0                   ; the first call: the head, the tags in it
X3PO_TAGS   equ 1                   ; a tag header beyond the head
X3PO_V1     equ 2                   ; the last 128 bytes
X3PO_FREE   equ 3                   ; the first header after the tags
X3PO_SYNC   equ 4                   ; the scan for an agreeing pair
X3PO_XING   equ 5                   ; the first frame's tag
X3PO_DONE   equ 6

; x3p_inew - DI -> an X3I record, DX:AX = the file's bytes: zero it, set it
x3p_inew:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    push ax
    xor ax, ax
    mov cx, X3I_SIZE / 2
    cld
    rep stosw
    pop ax
    pop es
    pop di
    mov [di+X3I_FSIZE], ax
    mov [di+X3I_FSIZE+2], dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3p_open - see x3gran.inc section 4
; in:  DI -> X3I, ES:BX = bytes, CX = count, DX:AX = their file offset
; out: CF = 0, AX = X3P_DONE / X3P_NEED
; -----------------------------------------------------------------------------
x3p_open:
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    cld
    mov [x3p_obseg], es
    mov [x3p_obofs], bx
    mov [x3p_oblen], cx
    mov [x3p_obpos], ax
    mov [x3p_obpos+2], dx
    mov al, [di+X3I_STAGE]
    cmp al, X3PO_HEAD
    je x3p_o_head
    cmp al, X3PO_TAGS
    je x3p_o_tags
    cmp al, X3PO_V1
    jne .n1
    jmp x3p_o_v1
.n1:
    cmp al, X3PO_FREE
    jne .n2
    jmp x3p_o_free
.n2:
    cmp al, X3PO_SYNC
    jne .n3
    jmp x3p_o_sync
.n3:
    cmp al, X3PO_XING
    jne .n4
    jmp x3p_o_xing
.n4:
    jmp x3p_o_done

; x3p_o_need - ask for [DX:AX, + CX) and return X3P_NEED (the tail jumps here)
x3p_o_need:
    mov [di+X3I_WANT], ax
    mov [di+X3I_WANT+2], dx
    mov [di+X3I_WANTN], cx
    mov ax, X3P_NEED
x3p_o_ret:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

; x3p_ohave - is [DX:AX, + CX) in the buffer?  CF = 0: ES:SI -> its first
; byte. keeps the rest
x3p_ohave:
    push ax
    push dx
    sub ax, [x3p_obpos]             ; DX:AX = the offset into the buffer
    sbb dx, [x3p_obpos+2]
    jc .no                          ; before it
    or dx, dx
    jnz .no
    push ax
    add ax, cx
    jc .no2
    cmp ax, [x3p_oblen]
    pop ax
    ja .no
    mov si, [x3p_obofs]
    add si, ax
    mov es, [x3p_obseg]
    pop dx
    pop ax
    clc
    ret
.no2:
    pop ax
.no:
    pop dx
    pop ax
    stc
    ret

; x3p_oclamp - CX = min(CX, file size - DX:AX) (0 if past it). keeps the rest
x3p_oclamp:
    push ax
    push dx
    neg ax                          ; DX:AX = size - offset
    adc dx, 0
    neg dx
    add ax, [di+X3I_FSIZE]
    adc dx, [di+X3I_FSIZE+2]
    js .z
    or dx, dx
    jnz .out
    cmp ax, cx
    jae .out
    mov cx, ax
    jmp short .out
.z:
    xor cx, cx
.out:
    pop dx
    pop ax
    ret

; x3p_ld - DX:AX = the dword at [DI+BX]
%macro X3P_LD 1
    mov ax, [di+%1]
    mov dx, [di+%1+2]
%endmacro
%macro X3P_ST 1
    mov [di+%1], ax
    mov [di+%1+2], dx
%endmacro

; --- X3PO_HEAD: the first read ------------------------------------------------
x3p_o_head:
    xor ax, ax                      ; it must be offset 0 and the whole head
    xor dx, dx
    mov cx, X3_FIRST_READ
    call x3p_oclamp
    call x3p_ohave
    jnc .h
    jmp x3p_o_need
.h:
    mov word [di+X3I_SCANP], 0      ; o = 0
    mov word [di+X3I_SCANP+2], 0
    mov byte [di+X3I_NTAG], 0
    mov byte [x3p_ohead], 1         ; titles may be read in this call
    jmp short x3p_o_tagl
x3p_o_tags:
    mov byte [x3p_ohead], 0
x3p_o_tagl:
    ; --- the ID3v2 tags at o, X3_ID3_MAX_TAGS = 4 in a row ---
    cmp byte [di+X3I_NTAG], 4
    jb .t
    jmp .tdone
.t:
    X3P_LD X3I_SCANP                ; o + 10 > file: no tag
    mov cx, 10
    push cx
    call x3p_oclamp
    cmp cx, 10
    pop cx
    jae .t10
    jmp .tdone
.t10:
    call x3p_ohave
    jnc .th
    mov byte [di+X3I_STAGE], X3PO_TAGS
    jmp x3p_o_need
.th:
    cmp word [es:si], 'I' | ('D' << 8)
    jne .tdone
    cmp byte [es:si+2], '3'
    jne .tdone
    mov al, [es:si+3]               ; version 2..4, revision not FFh
    cmp al, 2
    jb .tdone
    cmp al, 4
    ja .tdone
    cmp byte [es:si+4], 0xFF
    je .tdone
    push si
    add si, 6
    call x3p_synch                  ; DX:AX = the size, CF = 1 not synchsafe
    pop si
    jc .tdone
    mov [x3p_otsz], ax              ; this tag's size
    mov [x3p_otsz+2], dx
    ; end = o + 10 + size + (10 for a v2.4 footer)
    add ax, 10
    adc dx, 0
    cmp byte [es:si+3], 4
    jne .nf
    test byte [es:si+5], 0x10
    jz .nf
    add ax, 10
    adc dx, 0
.nf:
    add ax, [di+X3I_SCANP]
    adc dx, [di+X3I_SCANP+2]
    mov [x3p_oend], ax
    mov [x3p_oend+2], dx
    cmp byte [x3p_ohead], 0         ; titles: only out of the head, only
    je .next                        ; when the tag is not unsynchronised
    test byte [es:si+5], 0x80
    jnz .next
    call x3p_o_titles
.next:
    mov ax, [x3p_oend]
    mov dx, [x3p_oend+2]
    X3P_ST X3I_SCANP
    inc byte [di+X3I_NTAG]
    jmp x3p_o_tagl
.tdone:
    X3P_LD X3I_SCANP                ; START: the first byte after the tags
    X3P_ST X3I_START
    X3P_LD X3I_FSIZE
    X3P_ST X3I_AEND
    mov byte [di+X3I_STAGE], X3PO_V1
    ; fall into the ID3v1 stage
x3p_o_v1:
    X3P_LD X3I_FSIZE                ; no ID3v1 in a file under 128 bytes
    sub ax, 128
    sbb dx, 0
    jc .done
    mov cx, 128
    call x3p_ohave
    jnc .h
    jmp x3p_o_need
.h:
    cmp word [es:si], 'T' | ('A' << 8)
    jne .done
    cmp byte [es:si+2], 'G'
    jne .done
    X3P_ST X3I_AEND                 ; the audio ends at the tag
    or byte [di+X3I_FLAGS], X3IF_V1
    push si
    add si, 3                       ; title[30], then artist[30]
    push di
    add di, X3I_TITLE
    call x3p_v1txt
    pop di
    pop si
    add si, 33
    push di
    add di, X3I_ARTIST
    call x3p_v1txt
    pop di
.done:
    mov byte [di+X3I_STAGE], X3PO_FREE
x3p_o_free:
    X3P_LD X3I_START                ; the first header after the tags being
    mov cx, 4                       ; free format refuses by name
    push cx
    call x3p_oclamp
    cmp cx, 4
    pop cx
    jb .ok
    call x3p_ohave
    jnc .h
    jmp x3p_o_need
.h:
    call x3p_o4                     ; DX:AX = the four bytes at ES:SI
    call x3p_hdr
    jc .ok
    test ch, X3PH_FREE
    jz .ok
    mov byte [di+X3I_REF], X3REF_FREE
    jmp x3p_o_done
.ok:
    X3P_LD X3I_START
    X3P_ST X3I_SCANP
    mov byte [di+X3I_STAGE], X3PO_SYNC
    ; fall into the scan
x3p_o_sync:
    ; --- find_sync(b, START, AEND) with no lock -------------------------------
.cand:
    X3P_LD X3I_SCANP                ; p + 4 > AEND: none
    add ax, 4
    adc dx, 0
    cmp dx, [di+X3I_AEND+2]
    jb .in
    ja .none
    cmp ax, [di+X3I_AEND]
    jbe .in
.none:
    mov byte [di+X3I_REF], X3REF_NOSYN
    jmp x3p_o_done
.in:
    X3P_LD X3I_SCANP
    mov cx, 4
    call x3p_ohave
    jnc .h4
.want:
    X3P_LD X3I_SCANP                ; ask for p and what a pair needs past it
    mov cx, X3_FIRST_READ
    call x3p_oclamp
    jmp x3p_o_need
.h4:
    cmp byte [es:si], 0xFF
    jne .adv
    call x3p_o4
    mov [x3p_oraw], dx              ; (file order: DH DL AH AL)
    mov [x3p_oraw+2], ax
    call x3p_hdr
    jc .adv
    test ch, X3PH_FREE
    jnz .adv
    mov [x3p_oh], ax                ; this header, for the pair and the tag
    mov [x3p_oh+2], bx
    mov [x3p_oh+4], cx
    mov [x3p_oh+6], dx
    mov bp, dx                      ; n = p + size
    X3P_LD X3I_SCANP
    add ax, bp
    adc dx, 0
    mov [x3p_on], ax
    mov [x3p_on+2], dx
    add ax, 4                       ; n + 4 <= AEND: the next must agree
    adc dx, 0
    cmp dx, [di+X3I_AEND+2]
    jb .pair
    ja .end
    cmp ax, [di+X3I_AEND]
    jbe .pair
.end:
    mov ax, [x3p_on]                ; else n == AEND believes it
    cmp ax, [di+X3I_AEND]
    jne .adv
    mov ax, [x3p_on+2]
    cmp ax, [di+X3I_AEND+2]
    jne .adv
    jmp short .found
.pair:
    mov ax, [x3p_on]
    mov dx, [x3p_on+2]
    mov cx, 4
    call x3p_ohave
    jc .want
    call x3p_o4
    call x3p_hdr
    jc .adv
    cmp al, [x3p_oh]                ; agrees: version, layer, rate
    jne .adv
    cmp ah, [x3p_oh+1]
    jne .adv
    cmp bl, [x3p_oh+2]
    jne .adv
    jmp short .found
.adv:
    add word [di+X3I_SCANP], 1
    adc word [di+X3I_SCANP+2], 0
    jmp .cand
.found:
    X3P_LD X3I_SCANP                ; the first frame
    X3P_ST X3I_FIRST
    sub ax, [di+X3I_START]          ; skipped = o - START
    sbb dx, [di+X3I_START+2]
    X3P_ST X3I_SKIP
    mov ax, [x3p_oh]
    cmp ah, 3
    je .l3
    mov byte [di+X3I_REF], X3REF_L1
    cmp ah, 1
    je .ref
    mov byte [di+X3I_REF], X3REF_L2
.ref:
    jmp x3p_o_done
.l3:
    mov cx, [x3p_oraw]              ; the header, in file order
    xchg cl, ch
    mov [di+X3I_HDR], cx
    mov cx, [x3p_oraw+2]
    xchg cl, ch
    mov [di+X3I_HDR+2], cx
    mov [di+X3I_VER], al            ; the stream: version, rate, channels,
    mov bx, [x3p_oh+2]              ; bitrate, its frame's bytes
    mov [di+X3I_SRI], bl
    mov cx, [x3p_oh+4]
    mov [di+X3I_NCH], cl
    call x3p_rate
    mov [di+X3I_RATE], ax
    mov ax, [x3p_oh]
    call x3p_kbps
    mov [di+X3I_KBPS], ax
    mov ax, [x3p_oh+6]
    mov [x3p_ofsz], ax
    mov ax, [x3p_oh+4]
    mov [x3p_omisc], ah
    mov byte [di+X3I_STAGE], X3PO_XING
x3p_o_xing:
    ; --- xing_info: Xing / Info after the side information, or VBRI -----------
    X3P_LD X3I_FIRST
    mov cx, 192
    call x3p_oclamp
    call x3p_ohave
    jnc .h
    jmp x3p_o_need
.h:
    mov bp, cx                      ; BP = the bytes in hand from FIRST
    mov [x3p_ofst], si              ; ES:SI = FIRST
    mov al, [di+X3I_VER]            ; base = 4 + 2 (CRC) + side bytes
    mov cl, [di+X3I_NCH]
    call x3p_sideb
    add ax, 4
    test byte [x3p_omisc], X3PH_CRC
    jz .nc
    add ax, 2
.nc:
    mov bx, ax                      ; BX = base, relative to FIRST
    lea ax, [bx+4]
    cmp ax, bp
    ja .vbri
    mov si, [x3p_ofst]
    add si, bx
    cmp word [es:si], 'X' | ('i' << 8)
    jne .info
    cmp word [es:si+2], 'n' | ('g' << 8)
    je .xing
.info:
    cmp word [es:si], 'I' | ('n' << 8)
    jne .vbri
    cmp word [es:si+2], 'f' | ('o' << 8)
    jne .vbri
.xing:
    or byte [di+X3I_FLAGS], X3IF_TAGF
    mov dh, [es:si+6]               ; the flags' low two bytes (big-endian)
    mov dl, [es:si+7]
    mov [x3p_oxf], dl
    add bx, 8                       ; p = base + 8
    test dl, 1
    jz .nfr
    lea ax, [bx+4]
    cmp ax, bp
    ja .fin
    call x3p_obe32                  ; frames
    X3P_ST X3I_FRAMES
    or byte [di+X3I_FLAGS], X3IF_EXACT
    add bx, 4
.nfr:
    test byte [x3p_oxf], 2
    jz .nby
    lea ax, [bx+4]
    cmp ax, bp
    ja .fin
    call x3p_obe32                  ; bytes
    X3P_ST X3I_BYTES
    add bx, 4
.nby:
    test byte [x3p_oxf], 4
    jz .ntoc
    lea ax, [bx+100]                ; the TOC, when it is all in hand
    cmp ax, bp
    ja .ntoc0
    push di
    push ds
    mov si, [x3p_ofst]
    add si, bx
    add di, X3I_TOC
    push es
    push ds
    pop es
    pop ds
    mov cx, 100
    rep movsb
    pop ds
    pop di
    push ds
    pop es
    mov es, [x3p_obseg]
    or byte [di+X3I_FLAGS], X3IF_TOC
.ntoc0:
    add bx, 100
.ntoc:
    test byte [x3p_oxf], 8
    jz .nq
    add bx, 4
.nq:
    ; a LAME (or Lavf, Lavc, GOGO) tag: p + 24 <= the frame's end
    lea ax, [bx+24]
    cmp ax, [x3p_ofsz]
    ja .fin
    cmp ax, bp
    ja .fin
    mov si, [x3p_ofst]
    add si, bx
    mov ax, [es:si]
    mov dx, [es:si+2]
    cmp ax, 'L' | ('A' << 8)
    jne .lv
    cmp dx, 'M' | ('E' << 8)
    je .lame
.lv:
    cmp ax, 'L' | ('a' << 8)
    jne .gogo
    cmp dx, 'v' | ('f' << 8)
    je .lame
    cmp dx, 'v' | ('c' << 8)
    je .lame
.gogo:
    cmp ax, 'G' | ('O' << 8)
    jne .fin
    cmp dx, 'G' | ('O' << 8)
    jne .fin
.lame:
    mov al, [es:si+21]              ; delay: 12 bits, padding: 12 bits
    mov ah, [es:si+22]
    mov dl, [es:si+23]
    mov cl, 4
    xor bh, bh
    mov bl, al
    shl bx, cl
    mov ch, ah
    shr ch, cl
    or bl, ch
    mov [di+X3I_DELAY], bx
    and ah, 15
    mov bh, ah
    mov bl, dl
    mov [di+X3I_PAD], bx
    or byte [di+X3I_FLAGS], X3IF_LAME
    jmp short .fin
.vbri:
    cmp bp, 36 + 18                 ; VBRI 36 bytes in
    jb .fin
    mov si, [x3p_ofst]
    cmp word [es:si+36], 'V' | ('B' << 8)
    jne .fin
    cmp word [es:si+38], 'R' | ('I' << 8)
    jne .fin
    or byte [di+X3I_FLAGS], X3IF_TAGF | X3IF_VBRI | X3IF_EXACT
    mov ah, [es:si+36+6]
    mov al, [es:si+36+7]
    mov [di+X3I_DELAY], ax
    mov bx, 36 + 14
    call x3p_obe32
    X3P_ST X3I_FRAMES
.fin:
    call x3p_o_finish
x3p_o_done:
    mov byte [di+X3I_STAGE], X3PO_DONE
    mov ax, X3P_DONE
    jmp x3p_o_ret

; x3p_obe32 - DX:AX = the big-endian dword at FIRST + BX (ES:[x3p_ofst+BX])
x3p_obe32:
    push si
    mov si, [x3p_ofst]
    add si, bx
    mov dh, [es:si]
    mov dl, [es:si+1]
    mov ah, [es:si+2]
    mov al, [es:si+3]
    pop si
    ret

; x3p_o4 - DX:AX = the four bytes at ES:SI, file order (DH first)
x3p_o4:
    mov dh, [es:si]
    mov dl, [es:si+1]
    mov ah, [es:si+2]
    mov al, [es:si+3]
    ret

; x3p_synch - DX:AX = the synchsafe size at ES:SI, CF = 1 if a byte has bit 7
x3p_synch:
    push bx
    push cx
    mov al, [es:si]
    or al, [es:si+1]
    or al, [es:si+2]
    or al, [es:si+3]
    test al, 0x80
    jnz .no
    xor dx, dx                      ; (b0 << 21) | (b1 << 14) | (b2 << 7) | b3
    xor ax, ax
    mov bx, 0
.b:
    mov cl, 7                       ; DX:AX <<= 7, then | the byte
.sh:
    shl ax, 1
    rcl dx, 1
    dec cl
    jnz .sh
    or al, [es:si+bx]
    inc bx
    cmp bx, 4
    jb .b
    pop cx
    pop bx
    clc
    ret
.no:
    pop cx
    pop bx
    stc
    ret

; x3p_o_finish - the durations and the first audio frame
x3p_o_finish:
    X3P_LD X3I_FIRST                ; AUDIO = FIRST (+ the tag frame)
    test byte [di+X3I_FLAGS], X3IF_TAGF
    jz .a
    add ax, [x3p_ofsz]
    adc dx, 0
.a:
    X3P_ST X3I_AUDIO
    test byte [di+X3I_FLAGS], X3IF_EXACT
    jz .est
    ; exact: frames x 576 x granules / rate, in ms
    mov cx, 576
    cmp byte [di+X3I_VER], 1
    jne .g
    mov cx, 1152
.g:
    X3P_LD X3I_FRAMES               ; frames x samples (< 2^32 below 3.7 M
    call x3p_mul32                  ; frames: hours); BX:DX:AX
    mov cx, [di+X3I_RATE]
    call x3p_div48                  ; seconds, remainder in BX
    push bx
    mov cx, 1000
    call x3p_mul32                  ; whole seconds x 1000
    pop cx                          ; the remainder samples x 1000 / rate
    push dx
    push ax
    mov ax, cx
    mov cx, 1000
    mul cx
    div word [di+X3I_RATE]
    mov bx, ax
    pop ax
    pop dx
    add ax, bx
    adc dx, 0
    jmp short .ms
.est:
    ; estimated: (AEND - FIRST) x 8 / kbps, in ms: q = bytes / kbps, r the
    ; remainder; ms = 8 q + 8 r / kbps
    xor ax, ax
    xor dx, dx
    mov cx, [di+X3I_KBPS]
    jcxz .ms
    mov ax, [di+X3I_AEND]
    mov dx, [di+X3I_AEND+2]
    sub ax, [di+X3I_FIRST]
    sbb dx, [di+X3I_FIRST+2]
    call x3p_div48
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    push dx
    push ax
    mov ax, bx
    mov cx, 8
    mul cx
    div word [di+X3I_KBPS]
    mov bx, ax
    pop ax
    pop dx
    add ax, bx
    adc dx, 0
.ms:
    X3P_ST X3I_DURMS
    add ax, 500                     ; seconds, rounded half up
    adc dx, 0
    mov cx, 1000
    call x3p_div48
    or dx, dx
    jz .s
    mov ax, 0xFFFF
.s:
    mov [di+X3I_DUR], ax
    ret

; x3p_mul32 - DX:AX x CX -> BX:DX:AX (unsigned). keeps CX
x3p_mul32:
    push si
    mov si, dx
    mul cx                          ; low x CX
    push ax
    mov bx, dx
    mov ax, si
    mul cx                          ; high x CX
    add ax, bx
    adc dx, 0
    mov bx, dx
    mov dx, ax
    pop ax
    pop si
    ret

; x3p_div48 - DX:AX / CX -> DX:AX quotient, BX remainder (unsigned; the
; dividend's top word, if any, in BX on entry is NOT used: a 32-bit dividend)
; keeps CX
x3p_div48:
    push si
    mov si, ax
    mov ax, dx
    xor dx, dx
    div cx                          ; high word
    mov bx, ax
    mov ax, si
    div cx                          ; low word, with the remainder carried
    xchg bx, dx                     ; DX = high quotient, BX = remainder
    pop si
    ret

; x3p_o_titles - TIT2 / TPE1 (TT2 / TP1 in v2.2) out of the tag at ES:SI,
; which lies in the head (x3sim.id3v2_scan's inner loop). lim = min(o + 10 +
; size, 4096, file)
x3p_o_titles:
    push si
    push bp
    mov al, [es:si+3]
    mov [x3p_over], al
    mov bp, [di+X3I_SCANP]          ; o (the head is the first 4 KB: a word)
    mov ax, [x3p_otsz]              ; lim = o + 10 + size, capped
    mov dx, [x3p_otsz+2]
    add ax, bp
    adc dx, 0
    add ax, 10
    adc dx, 0
    or dx, dx
    jnz .cap
    cmp ax, X3_FIRST_READ
    jbe .c2
.cap:
    mov ax, X3_FIRST_READ
.c2:
    cmp ax, [x3p_oblen]             ; (the head is the file when it is short)
    jbe .c3
    mov ax, [x3p_oblen]
.c3:
    mov [x3p_olim], ax
    lea bx, [bp+10]                 ; p = o + 10 (offsets in the head)
    test byte [es:si+5], 0x40       ; an extended header (v2.3, v2.4)
    jz .fr
    cmp byte [x3p_over], 3
    jb .fr
    lea ax, [bx+4]
    cmp ax, [x3p_olim]
    ja .none
    mov si, [x3p_obofs]
    add si, bx
    cmp byte [x3p_over], 3
    jne .e4
    cmp word [es:si], 0             ; v2.3: p += 4 + the big-endian size
    jne .none
    mov ah, [es:si+2]
    mov al, [es:si+3]
    add ax, 4
    jc .none
    add bx, ax
    jc .none
    jmp short .fr
.e4:
    call x3p_synch                  ; v2.4: p += the synchsafe size
    jc .none
    or dx, dx
    jnz .none
    add bx, ax
    jc .none
.fr:
    mov cx, 10                      ; a frame header: 6 bytes (v2.2) or 10
    cmp byte [x3p_over], 2
    jne .fl
    mov cx, 6
.fl:
    mov ax, bx                      ; while p + hdr <= lim
    add ax, cx
    jc .none
    cmp ax, [x3p_olim]
    ja .none
    mov si, [x3p_obofs]
    add si, bx
    cmp byte [x3p_over], 2
    jne .f34
    xor dx, dx                      ; v2.2: a 24-bit size
    mov dl, [es:si+3]
    mov ah, [es:si+4]
    mov al, [es:si+5]
    jmp short .fs
.f34:
    cmp byte [x3p_over], 3
    jne .f4
    mov dh, [es:si+4]               ; v2.3: 32-bit big-endian
    mov dl, [es:si+5]
    mov ah, [es:si+6]
    mov al, [es:si+7]
    jmp short .fs
.f4:
    push si
    add si, 4
    call x3p_synch                  ; v2.4: synchsafe
    pop si
    jc .none
.fs:
    mov bp, ax                      ; fs == 0, a NUL id: the padding
    or bp, dx
    jz .none
    cmp byte [es:si], 0
    je .none
    add bx, cx                      ; body = p + hdr; body + fs <= lim
    or dx, dx
    jnz .none
    mov bp, ax
    add ax, bx
    jc .none
    cmp ax, [x3p_olim]
    ja .none
    ; the id: TIT2 / TT2 -> title, TPE1 / TP1 -> artist
    push ax
    xor ax, ax                      ; AX = the destination field or 0
    cmp byte [x3p_over], 2
    jne .id4
    cmp word [es:si], 'T' | ('T' << 8)
    jne .i2a
    cmp byte [es:si+2], '2'
    jne .i2a
    mov ax, X3I_TITLE
.i2a:
    cmp word [es:si], 'T' | ('P' << 8)
    jne .idd
    cmp byte [es:si+2], '1'
    jne .idd
    mov ax, X3I_ARTIST
    jmp short .idd
.id4:
    cmp word [es:si], 'T' | ('I' << 8)
    jne .i4a
    cmp word [es:si+2], 'T' | ('2' << 8)
    jne .i4a
    mov ax, X3I_TITLE
.i4a:
    cmp word [es:si], 'T' | ('P' << 8)
    jne .idd
    cmp word [es:si+2], 'E' | ('1' << 8)
    jne .idd
    mov ax, X3I_ARTIST
.idd:
    or ax, ax
    jz .skip
    push di
    add di, ax
    cmp byte [di], 0                ; the first one wins
    jne .skp2
    mov si, [x3p_obofs]
    add si, bx
    mov cx, bp
    call x3p_text
.skp2:
    pop di
.skip:
    pop bx                          ; p = body + fs
    jmp .fr
.none:
    pop bp
    pop si
    ret

; x3p_text - an ID3 text frame's payload (ES:SI, CX bytes, CX >= 1) into the
; field at DS:DI: at most X3_TITLE printable ASCII, '?' for anything else,
; NUL ends it, spaces stripped both ends (x3sim._text_field). Byte 0 is the
; encoding: 0 Latin-1, 1 UTF-16 with a BOM, 2 UTF-16BE, 3 UTF-8
; clobbers: AX, BX, CX, DX, SI
x3p_text:
    push di
    mov bx, di                      ; BX = the field, DX = characters put
    xor dx, dx
    mov al, [es:si]
    inc si
    dec cx
    cmp al, 1
    je .u16
    cmp al, 2
    je .u16
    mov ah, al                      ; AH = the encoding (3: skip 80h..BFh)
.b8:
    jcxz .end
    mov al, [es:si]
    inc si
    dec cx
    or al, al
    jz .end
    cmp ah, 3
    jne .b8p
    cmp al, 0x80
    jb .b8p
    cmp al, 0xC0
    jb .b8
.b8p:
    call x3p_tput
    cmp dx, X3_TITLE
    jb .b8
    jmp short .end
.u16:
    mov ah, 0                       ; AH = 1 big-endian
    cmp al, 2
    jne .bom
    mov ah, 1
    jmp short .u
.bom:
    cmp cx, 2
    jb .u
    cmp word [es:si], 0xFFFE        ; FE FF (as a word: FFFEh) big-endian
    jne .le
    mov ah, 1
    add si, 2
    sub cx, 2
    jmp short .u
.le:
    cmp word [es:si], 0xFEFF        ; FF FE little-endian
    jne .u
    add si, 2
    sub cx, 2
.u:
    cmp cx, 2                       ; pairs while k < len - 1
    jb .end
    push cx
    mov cl, [es:si]
    mov ch, [es:si+1]
    add si, 2
    or ah, ah
    jz .ule
    xchg cl, ch                     ; big-endian: hi = the first
.ule:
    ; CH = hi, CL = lo
    mov al, cl
    or al, ch
    jz .upop
    mov al, '?'
    or ch, ch
    jnz .uq
    mov al, cl
.uq:
    pop cx
    sub cx, 2
    call x3p_tput
    cmp dx, X3_TITLE
    jb .u
    jmp short .end
.upop:
    pop cx
.end:
    add bx, dx                      ; the NUL after the last one put
    mov byte [bx], 0
    pop di
    jmp x3p_strip

; x3p_tput - AL into the field at [BX + DX] as printable or '?'; DX + 1
x3p_tput:
    cmp al, 0x20
    jb .q
    cmp al, 0x7F
    jb .p
.q:
    mov al, '?'
.p:
    push bx
    add bx, dx
    mov [bx], al
    mov byte [bx+1], 0              ; kept NUL-ended as it grows
    pop bx
    inc dx
    ret

; x3p_v1txt - an ID3v1 field (ES:SI, 30 bytes) into DS:DI unless DI's field
; is already set: up to its first NUL, '?' for non-printable, stripped
; clobbers: AX, BX, CX, DX, SI
x3p_v1txt:
    cmp byte [di], 0
    jne .out
    push di
    mov bx, di
    xor dx, dx
    mov cx, 30
.c:
    mov al, [es:si]
    inc si
    or al, al
    jz .e
    call x3p_tput
    loop .c
.e:
    add bx, dx
    mov byte [bx], 0
    pop di
    call x3p_strip
.out:
    ret

; x3p_strip - strip spaces from both ends of the NUL string at DS:DI
; clobbers: AX, BX, SI
x3p_strip:
    push di
    mov si, di
.l:
    cmp byte [si], ' '
    jne .lead
    inc si
    jmp short .l
.lead:
    mov bx, di                      ; copy down
.cp:
    mov al, [si]
    mov [bx], al
    inc si
    inc bx
    or al, al
    jnz .cp
    dec bx                          ; BX -> the NUL; trailing spaces off
.t:
    cmp bx, di
    je .out
    cmp byte [bx-1], ' '
    jne .out
    dec bx
    mov byte [bx], 0
    jmp short .t
.out:
    pop di
    ret

%ifdef X3P_WANT_PROBE
; =============================================================================
; x3p_probe - x3gran.inc section 4. UI task only
; in:  SI -> NUL 8.3 name, BL = volume, DX = folder cluster, DI -> X3I
; out: CF = 0 the record complete; CF = 1 AX = FERR_* / X3E_MEM
; =============================================================================
X3P_PTAIL   equ 4096                ; a 32 KB cluster's read lands this far in
                                    ; (x3p_probe): room for a request's tail
x3p_probe:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [x3p_pname], si
    mov [x3p_pinfo], di
    push dx
    push bx
    call OSAPI_FILE_HERE            ; where we stand, to come back to
    mov [x3p_phdir], dx
    mov [x3p_phvol], bl
    pop bx
    pop dx
    call OSAPI_FILE_GOTO_QM
    jc .fail0
    ; the size: OSAPI_FILE_FIND over this folder, the only way a package can
    xor cx, cx
.scan:
    push ds
    pop es
    mov di, x3p_pfind
    call OSAPI_FILE_FIND
    jc .nofile
    mov si, x3p_pfind
    mov di, [x3p_pname]
    call x3p_same
    jne .scan
    cmp word [x3p_pfind+14], OSAPI_FT_DIR
    jae .nofile
    test byte [x3p_pfind+22], OSAPI_FIND_CZ
    jnz .packed                     ; stored compressed: not a byte stream
    ; the buffer: max(4 KB, 2 clusters) - every request of x3p_open is at
    ; most 4 KB, and a cluster-aligned read of that must cover it
    call OSAPI_FILE_DFREE           ; BX = sectors a cluster
    mov ax, 512
    mul bx
    mov [x3p_pclb], ax
    dec ax
    not ax
    mov [x3p_pclbm], ax
    mov word [x3p_pboff], 0
    mov ax, [x3p_pclb]              ; 2 clusters, 8 KB at least
    cmp ax, 16384
    ja .b32
    shl ax, 1
    cmp ax, X3_FIRST_READ * 2
    jae .b
    mov ax, X3_FIRST_READ * 2
.b:
    mov [x3p_pbsz], ax
    mov cl, 10
    shr ax, cl
    jmp short .claim
.b32:                               ; 32 KB CLUSTERS: two are past a segment,
    mov word [x3p_pbsz], 32768      ; so ONE is read 4 KB in, and a request
    mov word [x3p_pboff], X3P_PTAIL ; that runs past it is answered with its
    mov ax, 32768 / 1024 + X3P_PTAIL / 1024     ; tail moved down in front
.claim:                             ; of the next one (.rd): 36 KB
    call OSAPI_MEM_CLAIM            ; AX = KB
    jc .nomem
    mov [x3p_pseg], dx
    mov di, [x3p_pinfo]
    mov ax, [x3p_pfind+18]
    mov dx, [x3p_pfind+20]
    call x3p_inew
    xor ax, ax                      ; the head first
    xor dx, dx
    mov byte [x3p_pit], 64          ; a bound on a hostile file's scan
.rd:
    mov [x3p_pwant], ax             ; X: what it wants. Read at A, the cluster
    mov [x3p_pwant+2], dx           ; boundary below it
    and ax, [x3p_pclbm]
    call x3p_pread                  ; CX = bytes at DX:AX = A
    jc .rderr
    mov bx, [x3p_pwant]
    sub bx, ax                      ; k = X - A (< a cluster)
    sub cx, bx                      ; the bytes from X
    jnc .k
    xor cx, cx
.k:
    add bx, [x3p_pboff]             ; BX = X's place in the buffer
    cmp word [x3p_pboff], 0         ; A 32 KB CLUSTER whose end the request
    je .call                        ; passes (and the file goes on): its tail
    mov di, [x3p_pinfo]             ; moved down in front of the next
    cmp cx, [di+X3I_WANTN]          ; cluster, read 4 KB in
    jae .call
    mov ax, cx
    add ax, bx
    cmp ax, X3P_PTAIL + 32768
    jb .call                        ; (the cluster was the file's last)
    push cx
    push ds
    mov es, [x3p_pseg]
    mov ds, [x3p_pseg]
    mov si, bx
    mov di, X3P_PTAIL
    sub di, cx                      ; the tail (< 4 KB: a request is at most
    mov bx, di                      ; 4 KB) ends where the next cluster begins
    cld
    rep movsb
    pop ds
    pop cx
    push bx
    push cx
    mov ax, [x3p_pwant]
    mov dx, [x3p_pwant+2]
    and ax, [x3p_pclbm]
    add ax, 32768
    adc dx, 0
    call x3p_pread                  ; CX = the next cluster's bytes
    mov ax, cx
    pop cx
    pop bx
    jc .rderr
    add cx, ax
.call:
    mov ax, [x3p_pwant]
    mov dx, [x3p_pwant+2]
    mov es, [x3p_pseg]
    mov di, [x3p_pinfo]
    call x3p_open                   ; ES:BX = X's bytes
    cmp ax, X3P_DONE
    je .done
    dec byte [x3p_pit]
    jz .giveup
    mov ax, [di+X3I_WANT]
    mov dx, [di+X3I_WANT+2]
    jmp .rd
.giveup:
    mov byte [di+X3I_REF], X3REF_NOSYN
.done:
    call .free
    call .back
    clc
    jmp short .out
.packed:                            ; A PACKED FILE is refused by name - Play
    mov di, [x3p_pinfo]             ; says so (x3_trk_open) - and never read
    mov ax, [x3p_pfind+18]
    mov dx, [x3p_pfind+20]
    call x3p_inew
    mov byte [di+X3I_REF], X3REF_PACKED
    call .back
    clc
    jmp short .out
.rderr:
    push ax
    call .free
    call .back
    pop ax
    stc
    jmp short .out
.nomem:
    call .back
    mov ax, X3E_MEM
    stc
    jmp short .out
.nofile:
    call .back
    mov ax, 2                       ; FERR_NOENT's number (os88api.inc)
    stc
    jmp short .out
.fail0:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.free:
    mov dx, [x3p_pseg]
    call OSAPI_MEM_FREE
    ret
.back:
    push ax
    mov dx, [x3p_phdir]
    mov bl, [x3p_phvol]
    call OSAPI_FILE_GOTO_QM
    pop ax
    ret

; x3p_pread - the buffer's window at DX:AX (cluster-aligned): CX = the bytes
; delivered. Reads of 32 KB at most at a time. clobbers BX, SI, ES
x3p_pread:
    push di
    mov [x3p_ppos], ax
    mov [x3p_ppos+2], dx
    xor di, di                      ; DI = bytes so far
.c:
    mov cx, [x3p_pbsz]
    sub cx, di
    jz .done
    cmp cx, 32768
    jbe .n
    mov cx, 32768
.n:
    mov es, [x3p_pseg]
    mov bx, di
    add bx, [x3p_pboff]             ; (4 KB in, on a 32 KB cluster)
    mov si, [x3p_pname]
    mov ax, [x3p_ppos]
    mov dx, [x3p_ppos+2]
    add ax, di
    adc dx, 0
    call OSAPI_FILE_READ_AT
    jc .err
    or ax, ax
    jz .done
    add di, ax
    cmp ax, cx
    jae .c
.done:
    mov cx, di
    mov ax, [x3p_ppos]
    mov dx, [x3p_ppos+2]
    pop di
    clc
    ret
.err:
    pop di
    stc
    ret

; x3p_same - two NUL 8.3 names at DS:SI and DS:DI, case-insensitively.
; out ZF = 1 if the same. clobbers AX, SI, DI
x3p_same:
.c:
    mov al, [si]
    mov ah, [di]
    inc si
    inc di
    cmp al, 'a'
    jb .a
    cmp al, 'z'
    ja .a
    sub al, 0x20
.a:
    cmp ah, 'a'
    jb .b
    cmp ah, 'z'
    ja .b
    sub ah, 0x20
.b:
    cmp al, ah
    jne .out
    or al, al
    jnz .c
.out:
    ret
%endif ; X3P_WANT_PROBE

; =============================================================================
; THE STREAM (x3sim Stream.frames, x3sim Decoder.decode_frame)
; =============================================================================

; -----------------------------------------------------------------------------
; x3p_start - DI -> X3I (opened, not refused), SI -> the X3R ring
; -----------------------------------------------------------------------------
x3p_start:
    push ax
    push cx
    push di
    push es
    mov es, [x3p_fds]
    cld
    push di
    xor ax, ax                      ; counters and the staged state: zero
    mov di, X3P_CNT
    mov cx, (X3P_SIDE - X3P_CNT) / 2
    rep stosw
    pop di
    mov al, [di+X3I_VER]
    mov [es:X3P_S_VER], al
    mov byte [es:X3P_S_LAYER], 3
    mov al, [di+X3I_SRI]
    mov [es:X3P_S_SRI], al
    mov al, [di+X3I_NCH]
    mov [es:X3P_S_NCH0], al
    mov ax, [di+X3I_RATE]
    mov [es:X3P_S_RATE], ax
    mov ax, [di+X3I_SKIP]           ; the bytes the open skipped are the
    mov [es:X3P_CNT+X3PC_SKIP], ax  ; stream's (x3sim counts them there)
    mov ax, [di+X3I_SKIP+2]
    mov [es:X3P_CNT+X3PC_SKIP+2], ax
    pop es
    pop di
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; x3p_reset - SI -> the ring, positioned and refilled: the reservoir empty, a
;             scan for the next agreeing pair (not counted as a resync)
; -----------------------------------------------------------------------------
x3p_reset:
    push es
    mov es, [x3p_fds]
    mov word [es:X3P_S_HELD], 0
    mov word [es:X3P_S_TAIL], 0
    mov word [es:X3P_S_MAIN], 0
    mov byte [es:X3P_S_SCAN], 0
    mov byte [es:X3P_S_SEEK], 1
    mov byte [es:X3P_S_VERD], X3V_RES
    pop es
    clc
    ret

; -----------------------------------------------------------------------------
; x3p_counts - ES:DI -> X3PC_SIZE bytes: the verdict counters
; -----------------------------------------------------------------------------
x3p_counts:
    push cx
    push si
    push di
    push ds
    mov si, X3P_CNT
    mov cx, X3PC_SIZE
    mov ds, [x3p_fds]
    cld
    rep movsb
    pop ds
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; x3p_gran - AL = the granule -> DX:AX = the record
; -----------------------------------------------------------------------------
x3p_gran:
    push bx
    mov bx, [x3p_gvec]
    call x3p_fcall
    mov dx, [x3p_fds]
    mov ax, X3P_REC
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; x3p_next - SI -> the X3R ring. See x3gran.inc section 4. The frame's work is
; the FRONT part's (x3pnext.inc, its NEXT verb), kept out of the primary's
; segment: this is the near entry the engine calls, on the worker
; -----------------------------------------------------------------------------
x3p_next:
    push bx
    push es
    push ds                         ; ES:SI = the ring descriptor
    pop es
    mov bx, [x3p_nvec]
    call x3p_fcall
    pop es
    pop bx
    ret

; =============================================================================
; x3p_seekpos - DI -> X3I, DX:AX = milliseconds -> DX:AX = a file offset to
; read from, CX = the frame index there (saturating). Not part of the model:
; a seek resyncs (x3p_reset) and the pre-roll rebuilds the history
; =============================================================================
x3p_seekpos:
    push bx
    push si
    push bp
    mov cx, 1000                    ; samples = s x rate + rem x rate / 1000
    call x3p_div48                  ; DX:AX = s, BX = ms remainder
    push bx
    mov cx, [di+X3I_RATE]
    call x3p_mul32                  ; BX:DX:AX
    pop si
    push dx
    push ax
    mov ax, si
    mul cx
    mov cx, 1000
    div cx
    mov bx, ax
    pop ax
    pop dx
    add ax, bx
    adc dx, 0
    mov cx, 576                     ; frames = samples / (576 x granules)
    cmp byte [di+X3I_VER], 1
    jne .g
    mov cx, 1152
.g:
    call x3p_div48
    mov [x3p_sf], ax                ; the frame index f (32 bits)
    mov [x3p_sf+2], dx
    ; the fraction of the stream: f / frames, when the count is known
    mov bx, [di+X3I_FRAMES]
    mov bp, [di+X3I_FRAMES+2]
    mov si, bx
    or si, bp
    jz .cbr
    ; scale both until the count fits a word
.sc:
    or bp, bp
    jz .sd
    shr bp, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .sc
.sd:
    or bx, bx
    jz .cbr
    cmp dx, 0                       ; past the end: the last byte
    jne .last
    cmp ax, bx
    jae .last
    mov [x3p_sfr], bx
    test byte [di+X3I_FLAGS], X3IF_TOC
    jz .lin
    cmp word [di+X3I_BYTES], 0
    jne .toc
    cmp word [di+X3I_BYTES+2], 0
    je .lin
.toc:
    ; pct256 = f x 25600 / frames; i = pct256 >> 8, frac = pct256 & 255
    mov cx, 25600
    mul cx
    div bx                          ; AX = pct256 (< 25600)
    mov cl, ah                      ; i
    xor ch, ch
    mov bl, al                      ; frac
    mov si, cx
    add si, di
    mov al, [si+X3I_TOC]            ; a = toc[i], b = toc[i + 1] or 256
    xor ah, ah
    mov dx, 256
    cmp cx, 99
    jae .b
    mov dl, [si+X3I_TOC+1]
    xor dh, dh
.b:
    sub dx, ax                      ; t = a 256 + (b - a) frac (Q8 of 256)
    push ax
    mov al, bl
    xor ah, ah
    imul dx
    pop dx
    xchg dh, dl
    xor dl, dl
    add ax, dx                      ; AX = t: 0..65535 of the stream
    mov cx, ax
    mov ax, [di+X3I_BYTES]          ; offset = FIRST + t x BYTES / 65536
    mov dx, [di+X3I_BYTES+2]
    call x3p_mul32                  ; BX:DX:AX
    mov ax, dx
    mov dx, bx
    add ax, [di+X3I_FIRST]
    adc dx, [di+X3I_FIRST+2]
    jmp .clamp
.lin:
    ; offset = AUDIO + f x (AEND - AUDIO) / frames
    mov cx, ax                      ; f (scaled)
    mov ax, [di+X3I_AEND]
    mov dx, [di+X3I_AEND+2]
    sub ax, [di+X3I_AUDIO]
    sbb dx, [di+X3I_AUDIO+2]
    call x3p_mul32                  ; BX:DX:AX = length x f
    mov cx, [x3p_sfr]
    push ax                         ; 48 / 16: the top word first
    mov ax, bx
    push dx
    xor dx, dx
    div cx
    pop ax                          ; (the top quotient is 0 when f < frames)
    div cx
    mov bx, ax                      ; middle
    pop ax
    div cx
    mov dx, bx
    jmp short .add
.cbr:
    ; offset = AUDIO + f x 576 granules x kbps 125 / rate
    mov ax, [x3p_sf]
    mov dx, [x3p_sf+2]
    mov cx, 576
    cmp byte [di+X3I_VER], 1
    jne .c1
    mov cx, 1152
.c1:
    call x3p_mul32                  ; samples (32 bits enough below hours)
    push dx
    push ax
    mov ax, [di+X3I_KBPS]
    mov cx, 125
    mul cx
    mov cx, ax                      ; bytes a second
    pop ax
    pop dx
    call x3p_mul32                  ; BX:DX:AX = samples x bytes/s
    mov cx, [di+X3I_RATE]
    push ax
    mov ax, bx
    push dx
    xor dx, dx
    div cx
    pop ax
    div cx
    mov bx, ax
    pop ax
    div cx
    mov dx, bx
.add:
    add ax, [di+X3I_AUDIO]
    adc dx, [di+X3I_AUDIO+2]
    jmp short .clamp
.last:
    mov ax, [di+X3I_AEND]
    mov dx, [di+X3I_AEND+2]
    sub ax, 1
    sbb dx, 0
.clamp:
    cmp dx, [di+X3I_AEND+2]         ; within [AUDIO, AEND)
    jb .lo
    ja .hi
    cmp ax, [di+X3I_AEND]
    jb .lo
.hi:
    mov ax, [di+X3I_AEND]
    mov dx, [di+X3I_AEND+2]
    sub ax, 1
    sbb dx, 0
.lo:
    cmp dx, [di+X3I_AUDIO+2]
    ja .ok
    jb .au
    cmp ax, [di+X3I_AUDIO]
    jae .ok
.au:
    mov ax, [di+X3I_AUDIO]
    mov dx, [di+X3I_AUDIO+2]
.ok:
    mov cx, [x3p_sf]                ; the frame index, saturating
    cmp word [x3p_sf+2], 0
    je .f
    mov cx, 0xFFFF
.f:
    pop bp
    pop si
    pop bx
    ret

; =============================================================================
; the tables (build/x3ftab.inc: bitrates, rates)
; =============================================================================
    X3FTAB_PARSE

; =============================================================================
; THE BSS: x3pars.inc's words, laid out by the includer's X3B macro
; =============================================================================
%macro X3P_BSS 0
    X3B x3p_fds, 2                  ; the FRONT part: its data segment...
    X3B x3p_fcs, 2                  ; ...its code selector...
    X3B x3p_gvec, 2                 ; ...GRAN's offset...
    X3B x3p_nvec, 2                 ; ...and NEXT's
    X3B x3p_obseg, 2                ; x3p_open: the buffer it was handed...
    X3B x3p_obofs, 2
    X3B x3p_oblen, 2
    X3B x3p_obpos, 4
    X3B x3p_ohead, 1                ; ...1 = it is the head...
    X3B x3p_over, 1                 ; ...the tag's version...
    X3B x3p_otsz, 4                 ; ...the tag's size and end...
    X3B x3p_oend, 4
    X3B x3p_olim, 2                 ; ...the title scan's limit...
    X3B x3p_oh, 8                   ; ...the candidate header...
    X3B x3p_on, 4                   ; ...and its successor's offset...
    X3B x3p_ofsz, 2                 ; ...the first frame's bytes, misc...
    X3B x3p_omisc, 1
    X3B x3p_oxf, 1                  ; ...the Xing flags...
    X3B x3p_ofst, 2                 ; ...where FIRST lies in the buffer
    X3B x3p_oraw, 4                 ; ...the candidate's bytes
    X3B x3p_sf, 4                   ; x3p_seekpos: the frame index...
    X3B x3p_sfr, 2                  ; ...and the frame count, scaled
%ifdef X3P_WANT_PROBE
    X3B x3p_pname, 2                ; x3p_probe: the name...
    X3B x3p_pinfo, 2                ; ...the record...
    X3B x3p_phdir, 2                ; ...where we stood...
    X3B x3p_phvol, 1
    X3B x3p_pit, 1                  ; ...reads left...
    X3B x3p_pclb, 2                 ; ...the cluster, mask...
    X3B x3p_pclbm, 2
    X3B x3p_pbsz, 2                 ; ...the buffer's size, segment...
    X3B x3p_pboff, 2                ; ...where a read lands in it...
    X3B x3p_pseg, 2
    X3B x3p_ppos, 4                 ; ...its file offset...
    X3B x3p_pwant, 4                ; ...the offset x3p_open asked for...
    X3B x3p_pfind, OSAPI_FIND_SZ    ; ...and the file's directory entry
    X3B x3p_qinfo, X3I_SIZE         ; the queue's record (x3_e_qadd's probe)
%endif
%endmacro
