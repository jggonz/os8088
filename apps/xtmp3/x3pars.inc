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
; THREE HALVES, by who may run them:
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
    cmp al, X3REF_NOSYN
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
x3p_reft:   dw x3p_rf1, x3p_rf2, x3p_rf3, x3p_rf4
x3p_rf1:    db 'FREE FORMAT', 0
x3p_rf2:    db 'LAYER I', 0
x3p_rf3:    db 'LAYER II', 0
x3p_rf4:    db 'NO MPEG AUDIO', 0

; =============================================================================
; THE HEADER (x3sim.parse_header), answered in registers so that nothing is
; shared between a probe on the UI task and the worker's stream
;
; x3p_hdr - the four bytes DH DL AH AL (in file order)
; out: CF = 1 not a header we could believe. CF = 0: AL = version (1, 2, 25),
;      AH = layer 1..3, BL = rate index, BH = bitrate index, CL = channels,
;      CH = misc (X3PH_*: bits 0-1 mode, 2-3 mode extension), DX = the
;      frame's bytes (0 for free format)
; clobbers: nothing else
; =============================================================================
X3PH_CRC    equ 0x10                ; a CRC word follows the header
X3PH_PAD    equ 0x20
X3PH_FREE   equ 0x40

x3p_hdr:
    push si
    push di
    cmp dh, 0xFF                    ; sync: eleven ones
    jne .no
    mov si, dx                      ; SI = b0 b1, DI = b2 b3
    mov di, ax
    mov al, dl
    and al, 0xE0
    cmp al, 0xE0
    jne .no
    mov al, dl                      ; vb = (b1 >> 3) & 3
    shr al, 1
    shr al, 1
    shr al, 1
    and al, 3
    cmp al, 1
    je .no
    mov ah, dl                      ; lb = (b1 >> 1) & 3
    shr ah, 1
    and ah, 3
    jz .no
    mov bx, di                      ; b2: br = b2 >> 4, sr = (b2 >> 2) & 3
    mov bl, bh
    mov cl, 4
    shr bh, cl
    cmp bh, 15
    je .no
    shr bl, 1
    shr bl, 1
    and bl, 3
    cmp bl, 3
    je .no
    mov cl, 25                      ; version: vb 0 -> 25, 2 -> 2, 3 -> 1
    or al, al
    jz .v
    mov cl, 2
    cmp al, 2
    je .v
    mov cl, 1
.v:
    mov al, cl
    mov cl, 4                       ; layer = 4 - lb
    sub cl, ah
    mov ah, cl
    mov cx, di                      ; b3: mode = b3 >> 6, mext = (b3 >> 4) & 3
    mov ch, cl
    shr ch, 1
    shr ch, 1
    shr ch, 1
    shr ch, 1
    mov cl, ch
    and cl, 3
    shr ch, 1
    shr ch, 1
    shl cl, 1
    shl cl, 1
    or ch, cl                       ; CH = mode | mext << 2
    mov dx, si
    test dl, 1                      ; protection_bit 0: a CRC follows
    jnz .nc
    or ch, X3PH_CRC
.nc:
    mov dx, di
    test dh, 2                      ; padding: b2 bit 1
    jz .np
    or ch, X3PH_PAD
.np:
    or bh, bh
    jnz .nf
    or ch, X3PH_FREE
.nf:
    mov cl, 2
    mov dl, ch
    and dl, 3
    cmp dl, 3
    jne .st
    mov cl, 1                       ; mode 3: mono
.st:
    xor dx, dx
    test ch, X3PH_FREE
    jnz .ok
    call x3p_fsize
.ok:
    pop di
    pop si
    clc
    ret
.no:
    pop di
    pop si
    stc
    ret

; x3p_fsize - x3p_hdr's AL AH BL BH CH -> DX = the frame's bytes. keeps the
; rest but SI, DI
x3p_fsize:
    push ax
    push bx
    push cx
    push ax
    call x3p_kbps
    mov si, ax
    pop ax
    push ax
    call x3p_rate
    mov di, ax
    pop ax
    push ax
    mov cx, 12                      ; layer 1: (12 kbps 1000 / rate + pad) 4
    cmp ah, 1
    je .m
    mov cx, 144                     ; layer 2 or MPEG-1: 144 kbps 1000 / rate
    cmp ah, 2
    je .m
    cmp al, 1
    je .m
    mov cx, 72                      ; MPEG-2/2.5 layer 3: 72 kbps 1000 / rate
.m:
    mov ax, si
    mul cx                          ; (<= 144 x 448 = 64,512)
    mov cx, 1000
    mul cx
    div di
    pop dx                          ; DL = version, DH = layer
    pop cx
    push cx
    test ch, X3PH_PAD
    jz .p
    inc ax
.p:
    cmp dh, 1
    jne .e
    shl ax, 1
    shl ax, 1
.e:
    mov dx, ax
    pop cx
    pop bx
    pop ax
    ret

; x3p_kbps - AL = version, AH = layer, BH = bitrate index -> AX = kbps
x3p_kbps:
    push bx
    mov bl, ah                      ; row: (MPEG-1 ? 0 : 3) + layer - 1
    dec bl
    cmp al, 1
    je .r
    add bl, 3
.r:
    mov al, 15
    mul bl
    mov bl, bh
    xor bh, bh
    add bx, ax
    shl bx, 1
    mov ax, [x3p_tbr+bx]
    pop bx
    ret

; x3p_rate - AL = version, BL = rate index -> AX = Hz
x3p_rate:
    push bx
    call x3p_vi                     ; AL = vi
    mov ah, 3
    mul ah
    xor bh, bh
    add bx, ax
    shl bx, 1
    mov ax, [x3p_tsr+bx]
    pop bx
    ret

; x3p_vi - AL = version (1, 2, 25) -> AL = 0, 1, 2
x3p_vi:
    cmp al, 1
    je .z
    cmp al, 2
    je .o
    mov al, 2
    ret
.o:
    mov al, 1
    ret
.z:
    xor al, al
    ret

; x3p_sideb - AL = version, CL = channels -> AX = side-information bytes
x3p_sideb:
    cmp al, 1
    jne .m2
    mov ax, 17
    cmp cl, 1
    je .o
    mov ax, 32
    ret
.m2:
    mov ax, 9
    cmp cl, 1
    je .o
    mov ax, 17
.o:
    ret

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
    jnz .nofile                     ; stored compressed: not a byte stream
    ; the buffer: max(4 KB, 2 clusters) - every request of x3p_open is at
    ; most 4 KB, and a cluster-aligned read of that must cover it
    call OSAPI_FILE_DFREE           ; BX = sectors a cluster
    mov ax, 512
    mul bx
    mov [x3p_pclb], ax
    dec ax
    not ax
    mov [x3p_pclbm], ax
    mov ax, [x3p_pclb]              ; 2 clusters, 8 KB at least, 32 KB at most
    cmp ax, 16384                   ; (a 32 KB cluster's request that
    jae .b32                        ; straddles one is not answered: the
    shl ax, 1                       ; probe then gives up, no title)
    cmp ax, X3_FIRST_READ * 2
    jae .b
    mov ax, X3_FIRST_READ * 2
    jmp short .b
.b32:
    mov ax, 32768
.b:
    mov [x3p_pbsz], ax
    mov cl, 10
    shr ax, cl
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
    jmp short .rd
.giveup:
    mov byte [di+X3I_REF], X3REF_NOSYN
.done:
    call .free
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
; x3p_next - SI -> the X3R ring. See x3gran.inc section 4
; -----------------------------------------------------------------------------
x3p_next:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cld
    mov [x3p_rdesc], si
    mov es, [x3p_fds]
    mov ax, [si+X3R_SEG]            ; the ring, as it stands now
    mov [x3p_rseg], ax
    mov ax, [si+X3R_MASK]
    mov [x3p_rmask], ax
    mov ax, [si+X3R_RD]
    mov [x3p_rrd], ax
    mov cx, [si+X3R_WR]
    sub cx, ax
    mov [x3p_ravail], cx
    mov al, [si+X3R_EOF]
    mov [x3p_reof], al
    call x3p_fold                   ; the last frame's main data joins the
                                    ; reservoir's tail
    cmp byte [es:X3P_S_SCAN], 0
    jne .rescan
    cmp byte [es:X3P_S_SEEK], 0
    je .top
    xor bp, bp                      ; a seek: scan from the first byte
    mov byte [es:X3P_S_SCAN], 1
    jmp .scan
.rescan:
    xor bp, bp                      ; a scan in progress: from the first byte
    jmp .scan
.top:
    cmp word [x3p_ravail], 4        ; o + 4 <= end
    jae .h
.endmore:
    cmp byte [x3p_reof], 0
    jne .end
    jmp .more
.h:
    xor bx, bx
    call x3p_rget4
    call x3p_hdr
    jc .lost
    call x3p_lockok
    jnc .have
.lost:
    add word [es:X3P_CNT+X3PC_RESYNC], 1    ; a lost sync: scan from o + 1
    adc word [es:X3P_CNT+X3PC_RESYNC+2], 0
    mov byte [es:X3P_S_SCAN], 1
    mov bp, 1
.scan:
    ; --- find_sync(b, o, end, lock), resumable: BP = the candidate --------
    mov ax, bp                      ; p + 4 > avail: no more candidates
    add ax, 4
    jc .exh
    cmp ax, [x3p_ravail]
    jbe .cand
.exh:
    cmp byte [x3p_reof], 0
    jne .gone
    mov bx, bp                      ; consume what was scanned, ask for more
    call x3p_skip
    jmp .more
.gone:
    mov bx, [x3p_ravail]            ; the rest is garbage: the stream ends
    call x3p_skip
    jmp .end
.cand:
    mov bx, bp
    call x3p_rget4
    cmp dh, 0xFF
    jne .adv
    call x3p_hdr
    jc .adv
    call x3p_lockok
    jc .adv
    mov [x3p_hreg], ax              ; keep it: it may be the one
    mov [x3p_hreg+2], bx
    mov [x3p_hreg+4], cx
    mov [x3p_hreg+6], dx
    mov bx, bp                      ; n = p + size
    add bx, dx
    jc .adv
    mov ax, bx
    add ax, 4
    jc .near
    cmp ax, [x3p_ravail]
    ja .near
    call x3p_rget4                  ; n + 4 <= avail: the next must agree
    call x3p_hdr
    jc .adv
    cmp al, [x3p_hreg]
    jne .adv
    cmp ah, [x3p_hreg+1]
    jne .adv
    cmp bl, [x3p_hreg+2]
    jne .adv
    jmp short .found
.near:
    cmp byte [x3p_reof], 0          ; past what is held: at the end, a frame
    je .wait                        ; that ends inside it is believed
    cmp bx, [x3p_ravail]
    jbe .found
    jmp short .adv
.wait:
    mov bx, bp                      ; not yet known: consume up to p
    call x3p_skip
    jmp .more
.adv:
    inc bp
    jmp .scan
.found:
    mov bx, bp
    call x3p_skip
    mov byte [es:X3P_S_SCAN], 0
    mov byte [es:X3P_S_SEEK], 0
    mov ax, [x3p_hreg]
    mov bx, [x3p_hreg+2]
    mov cx, [x3p_hreg+4]
    mov dx, [x3p_hreg+6]
.have:
    ; --- the frame at o: AL AH BL BH CL CH DX as x3p_hdr answered ---------
    cmp dx, [x3p_ravail]
    jbe .whole
    cmp byte [x3p_reof], 0
    je .more
    add word [es:X3P_CNT+X3PC_TRUNC], 1     ; the file ends inside it
    adc word [es:X3P_CNT+X3PC_TRUNC+2], 0
    jmp .end
.whole:
    call x3p_stage
    mov bx, [es:X3P_S_SIZE]
    call x3p_consume
    mov al, [es:X3P_S_NGR]
    mov ah, [es:X3P_S_VERD]
    clc
    jmp short .out
.more:
    mov ax, X3P_MORE
    stc
    jmp short .out
.end:
    mov ax, X3P_END
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

; x3p_lockok - CF = 0 if x3p_hdr's AL AH BL CH is a Layer III header of the
; locked stream, not free format. keeps everything
x3p_lockok:
    cmp ah, 3
    jne .no
    test ch, X3PH_FREE
    jnz .no
    cmp al, [es:X3P_S_VER]
    jne .no
    cmp bl, [es:X3P_S_SRI]
    jne .no
    clc
    ret
.no:
    stc
    ret

; x3p_skip - consume BX bytes as skipped (garbage); a seek's scan does not
; count them. clobbers BX
x3p_skip:
    cmp byte [es:X3P_S_SEEK], 0
    jne x3p_consume
    add [es:X3P_CNT+X3PC_SKIP], bx
    adc word [es:X3P_CNT+X3PC_SKIP+2], 0
; x3p_consume - BX bytes off the ring: RD and POS, here and in the
; descriptor. clobbers BX
x3p_consume:
    push si
    add [x3p_rrd], bx
    sub [x3p_ravail], bx
    mov si, [x3p_rdesc]
    add [si+X3R_RD], bx
    add [si+X3R_POS], bx
    adc word [si+X3R_POS+2], 0
    pop si
    ret

; x3p_rget4 - DX:AX = the four bytes BX into the ring (file order, DH first)
; keeps the rest
x3p_rget4:
    push cx
    push si
    push ds
    mov cx, [x3p_rmask]
    mov si, [x3p_rrd]
    add si, bx
    mov ds, [x3p_rseg]
    and si, cx
    mov dh, [si]
    inc si
    and si, cx
    mov dl, [si]
    inc si
    and si, cx
    mov ah, [si]
    inc si
    and si, cx
    mov al, [si]
    pop ds
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; x3p_fold - the staged frame's main data joins the reservoir's tail: the
;            last min(511, tail + main) bytes of X3P_MD[512 - tail, 512 + main)
;            move to end at 512. Deferred to the NEXT x3p_next because the
;            granules of that frame read the md where it lies.
; -----------------------------------------------------------------------------
x3p_fold:
    push cx
    push si
    push di
    push ds
    mov cx, [es:X3P_S_TAIL]
    add cx, [es:X3P_S_MAIN]
    cmp cx, 511
    jbe .n
    mov cx, 511
.n:
    mov si, X3P_MD + X3P_MD_TAIL    ; source: 512 + main - newtail
    add si, [es:X3P_S_MAIN]
    sub si, cx
    mov di, X3P_MD + X3P_MD_TAIL    ; dest: 512 - newtail
    sub di, cx
    mov [es:X3P_S_TAIL], cx
    mov word [es:X3P_S_MAIN], 0
    push es
    pop ds
    rep movsb                       ; (source above dest: ascending is safe)
    pop ds
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; x3p_stage - the frame at the ring's read position (x3p_hdr's registers, DX
; = its size, all of it held): copy it, read its side information, decide its
; verdict, stage its main data. clobbers: AX, BX, CX, DX, SI, DI, BP
; -----------------------------------------------------------------------------
x3p_stage:
    mov [es:X3P_S_SIZE], dx
    mov [es:X3P_S_NCH], cl
    mov ah, ch
    and ah, 3
    mov [es:X3P_S_MODE], ah
    mov ah, ch
    shr ah, 1
    shr ah, 1
    and ah, 3
    mov [es:X3P_S_MEXT], ah
    xor ah, ah
    test ch, X3PH_CRC
    jz .c
    inc ah
.c:
    mov [es:X3P_S_CRC], ah
    mov byte [es:X3P_S_NGR], 1
    cmp al, 1
    jne .g
    mov byte [es:X3P_S_NGR], 2
.g:
    push ax                         ; AL = version, CL = channels
    push cx
    ; --- copy the frame out of the ring ---
    push ds
    mov cx, dx
    mov di, X3P_FRM
    mov si, [x3p_rrd]
    mov bx, [x3p_rmask]
    mov ds, [x3p_rseg]
.cp:
    and si, bx
    movsb
    loop .cp
    pop ds
    pop cx
    pop ax
    ; --- counted: a frame; a change of channels ---
    add word [es:X3P_CNT+X3PC_FRAMES], 1
    adc word [es:X3P_CNT+X3PC_FRAMES+2], 0
    add word [es:X3P_S_FRAME], 1
    adc word [es:X3P_S_FRAME+2], 0
    cmp cl, [es:X3P_S_NCH0]
    je .same
    add word [es:X3P_CNT+X3PC_CHG], 1
    adc word [es:X3P_CNT+X3PC_CHG+2], 0
.same:
    ; --- side information: from 4 (+2 with a CRC) ---
    call x3p_sideb                  ; AX = its bytes
    mov bx, 4
    cmp byte [es:X3P_S_CRC], 0
    je .nc
    add bx, 2
.nc:
    mov [x3p_sbase], bx
    add bx, ax                      ; the main data: [p + side, size)
    mov ax, [es:X3P_S_SIZE]
    mov byte [x3p_verd], X3V_BAD
    mov word [x3p_mlen], 0
    cmp bx, ax
    ja .verd                        ; no room for the side information
    sub ax, bx
    mov [x3p_mlen], ax
    mov [x3p_mofs], bx
    call x3p_side                   ; CF = 1: a reserved field
    jc .verd
    ; total part2_3_length > (mdb + main) x 8: badside
    xor bx, bx
    xor ax, ax
    mov cl, [es:X3P_S_NGR]
    shl cl, 1
    xor ch, ch
    mov si, X3P_SIDE
.sum:
    cmp byte [es:X3P_S_NCH], 2      ; (a mono frame has a row of one)
    je .s2
    test bx, 1
    jnz .sk
.s2:
    add ax, [es:si+X3P_SI_P23]
.sk:
    add si, X3P_SI_SZ
    inc bx
    loop .sum
    mov cx, [es:X3P_S_MDB]
    add cx, [x3p_mlen]
    shl cx, 1                       ; (<= 511 + 1441, x 8 fits a word)
    shl cx, 1
    shl cx, 1
    cmp ax, cx
    ja .verd
    mov ax, [es:X3P_S_MDB]          ; main_data_begin past what is held
    cmp ax, [es:X3P_S_HELD]
    jbe .ok
    mov byte [x3p_verd], X3V_RES
    jmp short .verd
.ok:
    mov byte [x3p_verd], X3V_OK
    cmp ax, [es:X3P_CNT+X3PC_REACH]
    jbe .r
    mov [es:X3P_CNT+X3PC_REACH], ax
.r:
    mov bx, X3P_MD + X3P_MD_TAIL    ; the md: the reservoir's last mdb bytes
    sub bx, ax                      ; then this frame's main data
    mov [es:X3P_S_MD0], bx
    add ax, [x3p_mlen]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [es:X3P_S_BITN], ax
    mov word [es:X3P_S_BITP], 0
.verd:
    mov al, [x3p_verd]
    mov [es:X3P_S_VERD], al
    cmp al, X3V_BAD
    jne .v1
    add word [es:X3P_CNT+X3PC_BAD], 1
    adc word [es:X3P_CNT+X3PC_BAD+2], 0
.v1:
    cmp al, X3V_RES
    jne .v2
    add word [es:X3P_CNT+X3PC_SHORT], 1
    adc word [es:X3P_CNT+X3PC_SHORT+2], 0
.v2:
    ; --- this frame's main data joins the reservoir, whatever the verdict ---
    mov cx, [x3p_mlen]
    mov si, [x3p_mofs]
    add si, X3P_FRM
    mov di, X3P_MD + X3P_MD_TAIL
    push ds
    push es
    pop ds
    rep movsb
    xor ax, ax                      ; four zero bytes past its end
    stosw
    stosw
    pop ds
    mov ax, [x3p_mlen]
    mov [es:X3P_S_MAIN], ax
    add ax, [es:X3P_S_HELD]         ; held = min(4096, held + main)
    cmp ax, X3_RESERVOIR
    jbe .hd
    mov ax, X3_RESERVOIR
.hd:
    mov [es:X3P_S_HELD], ax
    mov byte [es:X3P_S_GRNXT], 0
    ret

; -----------------------------------------------------------------------------
; x3p_side - the side information (x3sim.parse_side) of the frame staged at
; X3P_FRM + [x3p_sbase], into X3P_SIDE, X3P_S_MDB and X3P_S_SCFSI
; out: CF = 1 a reserved field. clobbers: AX, BX, CX, DX, SI, BP
; -----------------------------------------------------------------------------
x3p_side:
    mov word [x3p_sbp], 0
    mov byte [x3p_sres], 0
    cmp byte [es:X3P_S_VER], 1
    jne .m2
    mov cl, 9                       ; MPEG-1: main_data_begin 9, private 5/3
    call x3p_sget
    mov [es:X3P_S_MDB], ax
    mov cl, 5
    cmp byte [es:X3P_S_NCH], 1
    je .p1
    mov cl, 3
.p1:
    call x3p_sget
    mov cl, 4                       ; scfsi, a channel: four bits, group 0
    call x3p_sget                   ; the most significant
    mov [es:X3P_S_SCFSI], al
    mov byte [es:X3P_S_SCFSI+1], 0
    cmp byte [es:X3P_S_NCH], 1
    je .gr
    mov cl, 4
    call x3p_sget
    mov [es:X3P_S_SCFSI+1], al
    jmp short .gr
.m2:
    mov cl, 8                       ; MPEG-2: main_data_begin 8, private 1/2
    call x3p_sget
    mov [es:X3P_S_MDB], ax
    mov cl, 1
    cmp byte [es:X3P_S_NCH], 1
    je .p2
    mov cl, 2
.p2:
    call x3p_sget
    mov word [es:X3P_S_SCFSI], 0
.gr:
    xor bp, bp                      ; granule x 2 + channel
.gc:
    mov ax, bp
    mov ah, X3P_SI_SZ
    mul ah
    add ax, X3P_SIDE
    mov si, ax
    mov cl, 12
    call x3p_sget
    mov [es:si+X3P_SI_P23], ax
    mov cl, 9
    call x3p_sget
    mov [es:si+X3P_SI_BV], ax
    cmp ax, 288
    jbe .bv
    mov byte [x3p_sres], 1
.bv:
    mov cl, 8
    call x3p_sget
    mov [es:si+X3P_SI_GG], al
    mov cl, 4
    cmp byte [es:X3P_S_VER], 1
    je .sf
    mov cl, 9
.sf:
    call x3p_sget
    mov [es:si+X3P_SI_SFC], ax
    mov cl, 1
    call x3p_sget
    mov [es:si+X3P_SI_WS], al
    or al, al
    jz .nows
    mov cl, 2
    call x3p_sget
    mov [es:si+X3P_SI_BT], al
    or al, al
    jnz .bt
    mov byte [x3p_sres], 1          ; window switching with block type 0
.bt:
    mov cl, 1
    call x3p_sget
    mov [es:si+X3P_SI_MIX], al
    mov cl, 5
    call x3p_sget
    mov [es:si+X3P_SI_TAB], al
    mov cl, 5
    call x3p_sget
    mov [es:si+X3P_SI_TAB+1], al
    mov byte [es:si+X3P_SI_TAB+2], 0
    mov cl, 3
    call x3p_sget
    mov [es:si+X3P_SI_SBG], al
    mov cl, 3
    call x3p_sget
    mov [es:si+X3P_SI_SBG+1], al
    mov cl, 3
    call x3p_sget
    mov [es:si+X3P_SI_SBG+2], al
    mov al, 7                       ; region0: 8 for a pure short block
    cmp byte [es:si+X3P_SI_BT], 2
    jne .r7
    cmp byte [es:si+X3P_SI_MIX], 0
    jne .r7
    inc al
.r7:
    mov [es:si+X3P_SI_R0], al
    mov byte [es:si+X3P_SI_R1], 36
    jmp short .pre
.nows:
    mov word [es:si+X3P_SI_BT], 0   ; BT, MIX
    mov cl, 5
    call x3p_sget
    mov [es:si+X3P_SI_TAB], al
    mov cl, 5
    call x3p_sget
    mov [es:si+X3P_SI_TAB+1], al
    mov cl, 5
    call x3p_sget
    mov [es:si+X3P_SI_TAB+2], al
    mov word [es:si+X3P_SI_SBG], 0
    mov byte [es:si+X3P_SI_SBG+2], 0
    mov cl, 4
    call x3p_sget
    mov [es:si+X3P_SI_R0], al
    mov cl, 3
    call x3p_sget
    mov [es:si+X3P_SI_R1], al
.pre:
    xor al, al
    cmp byte [es:X3P_S_VER], 1
    jne .np
    mov cl, 1
    call x3p_sget
.np:
    mov [es:si+X3P_SI_PRE], al
    mov cl, 1
    call x3p_sget
    mov [es:si+X3P_SI_SCL], al
    mov cl, 1
    call x3p_sget
    mov [es:si+X3P_SI_C1], al
    mov cx, 3                       ; Huffman table 4 or 14: reserved
    xor bx, bx
.tt:
    mov al, [es:si+X3P_SI_TAB+bx]
    cmp al, 4
    je .rs
    cmp al, 14
    jne .tn
.rs:
    mov byte [x3p_sres], 1
.tn:
    inc bx
    loop .tt
    ; the next: channels, then granules (a mono frame skips odd rows)
    inc bp
    cmp byte [es:X3P_S_NCH], 2
    je .nx
    inc bp
.nx:
    mov al, [es:X3P_S_NGR]
    shl al, 1
    xor ah, ah
    cmp bp, ax
    jae .done
    jmp .gc
.done:
    cmp byte [x3p_sres], 1
    cmc                             ; CF = 1 when a field was reserved
    ret

; x3p_sget - CL = n bits (1..12) of the side information -> AX. clobbers: CX,
; DX
x3p_sget:
    push bx
    push si
    mov bx, [x3p_sbp]
    mov si, bx
    shr si, 1
    shr si, 1
    shr si, 1
    add si, [x3p_sbase]
    add si, X3P_FRM
    mov ah, [es:si]                 ; 24 bits from the byte holding the
    mov al, [es:si+1]               ; position
    mov dl, [es:si+2]
    xor ch, ch
    add [x3p_sbp], cx
    mov ch, cl                      ; CH = n
    mov cl, bl
    and cl, 7
    jz .al
    shl ax, cl                      ; the window's top bits at bit 15
    mov dh, 8
    sub dh, cl
    mov cl, dh
    shr dl, cl
    or al, dl
.al:
    mov cl, 16
    sub cl, ch
    shr ax, cl
    pop si
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
    X3B x3p_gvec, 2                 ; ...GRAN's offset
    X3B x3p_rdesc, 2                ; x3p_next: the ring descriptor...
    X3B x3p_rseg, 2                 ; ...its segment, mask, read position...
    X3B x3p_rmask, 2
    X3B x3p_rrd, 2
    X3B x3p_ravail, 2               ; ...the bytes it holds...
    X3B x3p_reof, 1                 ; ...and its EOF
    X3B x3p_verd, 1                 ; the frame being staged: its verdict...
    X3B x3p_hreg, 8                 ; ...a scan's header...
    X3B x3p_sbase, 2                ; ...its side information's offset...
    X3B x3p_sbp, 2                  ; ...the side reader's bit...
    X3B x3p_sres, 1                 ; ...a reserved field seen...
    X3B x3p_mlen, 2                 ; ...its main data's length and offset
    X3B x3p_mofs, 2
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
    X3B x3p_pseg, 2
    X3B x3p_ppos, 4                 ; ...its file offset...
    X3B x3p_pwant, 4                ; ...the offset x3p_open asked for...
    X3B x3p_pfind, OSAPI_FIND_SZ    ; ...and the file's directory entry
    X3B x3p_qinfo, X3I_SIZE         ; the queue's record (x3_e_qadd's probe)
%endif
%endmacro
