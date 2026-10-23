; =============================================================================
; os8088 - apps/xtmp3/x3front.asm - XTMP3's FRONT part (SPEC.md 121.3)
;
; The granule-level half of the shared front end: scalefactors, Huffman,
; the requantisation exponents and the stereo plan - tools/x3sim.py's
; read_scf_mpeg1/2, huffman, quarter_exponents and stereo_plan, decision for
; decision - written into the granule record (apps/xtmp3/x3gran.inc section
; 1) that both backends read. Its frame-level half is x3pars.inc in the
; primary, which stages a frame into this part's data block (x3gran.inc
; section 5) and far-calls GRAN once a granule.
;
; An OP_SEG part, loaded eagerly, assembled at org 0: the header, the FIXED
; data block x3pars.inc addresses, then this code, then its tables - the
; Huffman lookup entries sliced out of x3std.bin (tools/x3tables.py) and the
; band tables of build/x3ftab.inc (tools/x3fronttab.py). Everything is reached
; through DS = this part's DATA segment (op_seg): nothing is read or written
; through CS, which under kern_mega is an execute-only alias.
;
; Labels: xf_ (this part's own), x3g_ (the shared CRC).
; =============================================================================

    cpu 8086
    bits 16
    org 0

%include "x3gran.inc"
%include "x3std.inc"
%include "x3ftab.inc"

; --- the header (x3gran.inc section 5) -------------------------------------
    db 'X3', X3G_ABI, X3K_FRONT, X3P_NV, 0
    dw xf_gran, xf_check, xf_next
    times X3P_DATA - ($ - $$) db 0
; --- the data block: x3gran.inc's fixed layout, zero at load ---------------
    times X3P_DEND - X3P_DATA db 0

; =============================================================================
; GRAN - the record for granule AL of the staged frame
; in:  DS = this part's data segment, AL = the granule
; out: CF = 0; the record at X3P_REC filled. Keeps all but AX
; =============================================================================
xf_gran:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cld
    push ds
    pop es
    mov [xf_gr], al
    ; --- the header ---
    mov di, X3P_REC
    mov al, [X3P_S_VER]
    mov [di+X3G_VER], al
    mov bl, 0                       ; vi: 0 MPEG-1, 1 MPEG-2, 2 MPEG-2.5
    cmp al, 1
    je .vi
    inc bl
    cmp al, 2
    je .vi
    inc bl
.vi:
    mov al, bl                      ; k = vi * 3 + sri
    add al, bl
    add al, bl
    add al, [X3P_S_SRI]
    mov [xf_k], al
    mov al, [X3P_S_SRI]
    mov [di+X3G_SRI], al
    mov al, [X3P_S_NCH]
    mov [di+X3G_NCH], al
    mov al, [X3P_S_MODE]
    mov [di+X3G_MODE], al
    mov ah, [X3P_S_MEXT]
    mov [di+X3G_MEXT], ah
    xor bl, bl                      ; M/S: joint, mode extension bit 1
    cmp al, 1
    jne .nms
    test ah, 2
    jz .nms
    inc bl
.nms:
    mov [di+X3G_MS], bl
    mov [xf_ms], bl
    mov al, [X3P_S_NGR]
    mov [di+X3G_NGR], al
    mov al, [xf_gr]
    mov [di+X3G_GR], al
    mov byte [di+X3G_PLAN], X3G_PLAN_LR
    mov byte [di+X3G_ISSC], 0
    mov ax, [X3P_S_RATE]
    mov [di+X3G_RATE], ax
    mov ax, [X3P_S_FRAME]
    mov [di+X3G_FRAME], ax
    mov ax, [X3P_S_FRAME+2]
    mov [di+X3G_FRAME+2], ax
    mov al, [xf_k]                  ; the band tables of this version and rate
    mov ah, X3FT_LC_N * 2
    mul ah
    add ax, x3ft_lc
    mov [xf_lc], ax
    mov al, [xf_k]
    mov ah, X3FT_SC_N
    mul ah
    add ax, x3ft_sc
    mov [xf_sc], ax
    mov al, [xf_k]
    mov ah, X3FT_SW_N
    mul ah
    add ax, x3ft_sw
    mov si, ax
    lea di, [X3P_REC+X3G_SFBS]
    mov cx, X3FT_SW_N
    rep movsb
    ; --- both channels start as silence ---
    xor bx, bx
.clr:
    mov di, [xf_crt+bx]
    xor ax, ax
    mov [di+X3G_C_OK], al
    mov [di+X3G_C_BT], ax           ; BT, MIX
    mov [di+X3G_C_WS], al
    mov [di+X3G_C_NZ], ax
    mov byte [xf_okc+bx], 0
    inc bx
    inc bx
    cmp bx, 4
    jb .clr
    cmp byte [X3P_S_VERD], X3V_OK
    jne .zero
    ; --- each channel of the frame ---
    mov byte [xf_ch], 0
.chan:
    call xf_channel
    inc byte [xf_ch]
    mov al, [xf_ch]
    cmp al, [X3P_S_NCH]
    jb .chan
    ; --- a granule with a channel bad is counted once ---
    mov al, [xf_okc]
    cmp byte [X3P_S_NCH], 2
    jne .one
    and al, [xf_okc+2]
.one:
    or al, al
    jnz .plan
    add word [X3P_CNT+X3PC_BADGR], 1
    adc word [X3P_CNT+X3PC_BADGR+2], 0
.plan:
    call xf_stereo
    jmp short .out
.zero:                              ; a dropped frame: zero spectra
    mov di, X3P_REC+X3G_CH0+X3G_C_IX
    call xf_zix
    mov di, X3P_REC+X3G_CH1+X3G_C_IX
    call xf_zix
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    retf

xf_crt:     dw X3P_REC+X3G_CH0, X3P_REC+X3G_CH1

; xf_zix - zero 576 words at DI (ES = DS). clobbers AX, CX, DI
xf_zix:
    xor ax, ax
    mov cx, 576
    rep stosw
    ret

; =============================================================================
; xf_channel - channel [xf_ch] of granule [xf_gr]: scalefactors, Huffman, the
; exponents (x3sim Decoder.granule_spectra and quarter_exponents)
; clobbers: everything but DS, ES
; =============================================================================
xf_channel:
    mov al, [xf_gr]                 ; the side information: [gr][ch]
    shl al, 1
    add al, [xf_ch]
    mov ah, X3P_SI_SZ
    mul ah
    add ax, X3P_SIDE
    mov [xf_si], ax
    mov bl, [xf_ch]
    xor bh, bh
    shl bx, 1
    mov ax, [xf_crt+bx]
    mov [xf_cr], ax
    mov ax, X3P_SF_SZ
    mul byte [xf_ch]
    add ax, X3P_SCF
    mov [xf_sf], ax
    mov si, [xf_si]
    mov di, [xf_cr]
    mov al, [si+X3P_SI_BT]
    mov [di+X3G_C_BT], al
    mov al, [si+X3P_SI_MIX]
    mov [di+X3G_C_MIX], al
    mov al, [si+X3P_SI_WS]
    mov [di+X3G_C_WS], al
    mov ax, [X3P_S_BITP]            ; end = start + part2_3_length
    add ax, [si+X3P_SI_P23]
    mov [xf_end], ax
    ; MPEG-2.5 at 8 kHz with a mixed block: undefined by ISO, a bad granule
    cmp byte [X3P_S_VER], 25
    jne .def
    cmp byte [X3P_S_SRI], 2
    jne .def
    cmp byte [si+X3P_SI_BT], 2
    jne .def
    cmp byte [si+X3P_SI_MIX], 0
    jne .bad
.def:
    mov al, [si+X3P_SI_PRE]
    mov [xf_pre], al
    cmp byte [X3P_S_VER], 1
    jne .m2
    call xf_scf1
    jmp short .scfd
.m2:
    call xf_scf2
.scfd:
    mov ax, [X3P_S_BITP]
    cmp ax, [xf_end]
    ja .bad
    call xf_huff
    jc .bad
    call xf_blocks                  ; the line map, then the exponents
    call xf_qexp
    mov di, [xf_cr]
    mov byte [di+X3G_C_OK], 1
    mov bl, [xf_ch]
    xor bh, bh
    shl bx, 1
    mov byte [xf_okc+bx], 1
    jmp short .end
.bad:
    mov di, [xf_cr]
    mov word [di+X3G_C_NZ], 0
    add di, X3G_C_IX
    call xf_zix
.end:
    mov ax, [xf_end]
    mov [X3P_S_BITP], ax
    ret

; =============================================================================
; THE BIT READER over the staged main data (X3P_S_MD0, X3P_S_BITN): the
; target of x3sim's BitReader. Past the data it reads zeros; x3pars.inc
; leaves four zero bytes after the data, which is what a peek that starts
; inside it and runs past its end reads.
; =============================================================================

; xf_peek16 - 16 bits at bit position BX, MSB first; 0 at or past the end
; out: AX   clobbers: CX, DX, SI
xf_peek16:
    xor ax, ax
    cmp bx, [X3P_S_BITN]
    jae .out
    mov si, bx
    mov cl, 3
    shr si, cl
    add si, [X3P_S_MD0]
    mov ah, [si]
    mov al, [si+1]
    mov dl, [si+2]
    mov cl, bl
    and cl, 7
    jz .out
    shl ax, cl
    mov ch, 8
    sub ch, cl
    mov cl, ch
    shr dl, cl
    or al, dl
.out:
    ret

; xf_get - CL = n bits (0..16) from the reader -> AX
; clobbers: BX, CX, DX, SI
xf_get:
    xor ax, ax
    xor ch, ch
    or cl, cl
    jz .out
    mov bx, [X3P_S_BITP]
    add [X3P_S_BITP], cx
    push cx
    call xf_peek16
    pop cx
    mov ch, 16
    sub ch, cl
    mov cl, ch
    shr ax, cl
.out:
    ret

; =============================================================================
; SCALEFACTORS (x3sim read_scf_mpeg1 / read_scf_mpeg2) into [xf_sf]
; =============================================================================

; xf_scf1 - MPEG-1. clobbers: everything but DS, ES
xf_scf1:
    mov si, [xf_si]
    mov bl, [si+X3P_SI_SFC]
    xor bh, bh
    shl bx, 1
    mov ax, [x3ft_slen1+bx]
    mov [xf_sl], ax                 ; slen1, slen2
    mov di, [xf_sf]                 ; shorts and the 'illegal' flags: zero
    add di, X3P_SF_SHRT
    mov cx, X3P_SF_SZ - X3P_SF_SHRT
    xor al, al
    rep stosb
    cmp byte [si+X3P_SI_BT], 2
    jne .long
    mov di, [xf_sf]                 ; a short block: the longs are zero...
    mov cx, 22
    rep stosb
    mov byte [xf_b], 0
    cmp byte [si+X3P_SI_MIX], 0
    je .sh
    mov di, [xf_sf]                 ; ...but a mixed one's first eight
.mx:
    mov cl, [xf_sl]
    push di
    call xf_get
    pop di
    stosb
    mov bx, di
    sub bx, [xf_sf]
    cmp bx, 8
    jb .mx
    mov byte [xf_b], 3
.sh:                                ; bands lo..11: slen1 below 6, slen2 above
    mov al, [xf_b]
    mov ah, 3
    mul ah
    add ax, [xf_sf]
    add ax, X3P_SF_SHRT
    mov di, ax
.shb:
    mov cl, [xf_sl]
    cmp byte [xf_b], 6
    jb .s1
    mov cl, [xf_sl+1]
.s1:
    mov byte [xf_w], 3
.shw:
    push cx
    push di
    call xf_get
    pop di
    pop cx
    stosb
    dec byte [xf_w]
    jnz .shw
    inc byte [xf_b]
    cmp byte [xf_b], 12
    jb .shb
    ret
.long:                              ; four groups, scfsi reuse in granule 1
    cmp byte [xf_gr], 0
    jne .g
    mov di, [xf_sf]
    mov cx, 22
    xor al, al
    rep stosb
.g:
    mov bl, [xf_ch]
    xor bh, bh
    mov al, [X3P_S_SCFSI+bx]
    mov [xf_scfsi], al
    xor bx, bx                      ; group
.grp:
    mov cl, [xf_sl]                 ; slen1 for groups 0, 1; slen2 for 2, 3
    cmp bl, 2
    jb .gl
    mov cl, [xf_sl+1]
.gl:
    mov [xf_n], cl
    mov al, [xf_gbeg+bx]
    mov ah, [xf_gbeg+bx+1]
    mov [xf_b], al
    mov [xf_be], ah
    cmp byte [xf_gr], 1
    jne .rd
    mov al, [xf_scfsi]              ; bit 3 = group 0 (the first read)
    mov cl, 3
    sub cl, bl
    shr al, cl
    test al, 1
    jnz .next                       ; reused: granule 0's stand
.rd:
    mov al, [xf_b]
    xor ah, ah
    add ax, [xf_sf]
    mov di, ax
.rb:
    mov cl, [xf_n]
    push bx
    push di
    call xf_get
    pop di
    pop bx
    stosb
    inc byte [xf_b]
    mov al, [xf_b]
    cmp al, [xf_be]
    jb .rb
.next:
    inc bx
    cmp bx, 4
    jb .grp
    ret

xf_gbeg:    db 0, 6, 11, 16, 21     ; the four groups' bands

; xf_scf2 - MPEG-2 / 2.5 (13818-3 2.4.3.2). Sets [xf_pre], and the record's
; X3G_ISSC on the intensity right channel. clobbers: everything but DS, ES
xf_scf2:
    mov si, [xf_si]
    mov byte [xf_isr], 0
    cmp byte [xf_ch], 1
    jne .nr
    cmp byte [X3P_S_MODE], 1
    jne .nr
    test byte [X3P_S_MEXT], 1
    jz .nr
    mov byte [xf_isr], 1
.nr:
    mov ax, [si+X3P_SI_SFC]
    mov byte [xf_pre], 0
    cmp byte [xf_isr], 0
    je .norm
    mov bl, al                      ; the intensity right channel
    and bl, 1
    mov [X3P_REC+X3G_ISSC], bl
    shr ax, 1
    cmp ax, 180
    jae .i4
    mov bl, 36                      ; sfc/36, (sfc%36)/6, sfc%6, 0; table 3
    div bl
    mov [xf_sl], al
    mov al, ah
    xor ah, ah
    mov bl, 6
    div bl
    mov [xf_sl+1], al
    mov [xf_sl+2], ah
    mov byte [xf_sl+3], 0
    mov byte [xf_tbl], 3
    jmp .parts
.i4:
    cmp ax, 244
    jae .i5
    sub ax, 180                     ; v/16, (v%16)/4, v%4, 0; table 4
    mov bl, al
    mov cl, 4
    shr al, cl
    mov [xf_sl], al
    mov al, bl
    and al, 15
    shr al, 1
    shr al, 1
    mov [xf_sl+1], al
    and bl, 3
    mov [xf_sl+2], bl
    mov byte [xf_sl+3], 0
    mov byte [xf_tbl], 4
    jmp .parts
.i5:
    sub ax, 244                     ; v/3, v%3, 0, 0; table 5
    mov bl, 3
    div bl
    mov [xf_sl], al
    mov [xf_sl+1], ah
    mov word [xf_sl+2], 0
    mov byte [xf_tbl], 5
    jmp .parts
.norm:
    cmp ax, 400
    jae .n1
    mov bx, ax                      ; (sfc>>4)/5, (sfc>>4)%5, (sfc&15)>>2,
    mov cl, 4                       ; sfc&3; table 0
    shr ax, cl
    mov cl, 5
    div cl
    mov [xf_sl], al
    mov [xf_sl+1], ah
    mov al, bl
    and al, 15
    shr al, 1
    shr al, 1
    mov [xf_sl+2], al
    and bl, 3
    mov [xf_sl+3], bl
    mov byte [xf_tbl], 0
    jmp .parts
.n1:
    cmp ax, 500
    jae .n2
    sub ax, 400                     ; (v>>2)/5, (v>>2)%5, v&3, 0; table 1
    mov bl, al
    shr ax, 1
    shr ax, 1
    mov cl, 5
    div cl
    mov [xf_sl], al
    mov [xf_sl+1], ah
    and bl, 3
    mov [xf_sl+2], bl
    mov byte [xf_sl+3], 0
    mov byte [xf_tbl], 1
    jmp .parts
.n2:
    sub ax, 500                     ; v/3, v%3, 0, 0; table 2; preflag
    mov bl, 3
    div bl
    mov [xf_sl], al
    mov [xf_sl+1], ah
    mov word [xf_sl+2], 0
    mov byte [xf_tbl], 2
    mov byte [xf_pre], 1
.parts:
    mov di, [xf_sf]                 ; a fresh set: every value and flag 0
    mov cx, X3P_SF_SZ
    xor al, al
    rep stosb
    xor al, al                      ; the kind: 0 long, 1 short, 2 mixed
    cmp byte [si+X3P_SI_BT], 2
    jne .k
    inc al
    cmp byte [si+X3P_SI_MIX], 0
    je .k
    inc al
.k:
    mov [xf_kind], al
    mov ah, 4                       ; nr_of_sfb[table][kind]
    mul ah
    mov bx, ax
    mov al, [xf_tbl]
    mov ah, 12
    mul ah
    add bx, ax
    add bx, x3ft_nrsfb
    mov [xf_nrp], bx
    mov word [xf_vi], 0             ; values read so far
    mov byte [xf_part], 0
.part:
    mov bl, [xf_part]
    xor bh, bh
    mov cl, [xf_sl+bx]
    mov [xf_n], cl
    mov ch, 1                       ; mx = 2^n - 1
    shl ch, cl
    dec ch
    mov [xf_mx], ch
    add bx, [xf_nrp]
    mov al, [bx]
    mov [xf_cnt], al
.val:
    cmp byte [xf_cnt], 0
    je .pnext
    mov cl, [xf_n]
    call xf_get
    mov dl, al                      ; DL = the value, DH = its 'illegal' flag
    xor dh, dh
    cmp byte [xf_isr], 0
    je .st
    cmp byte [xf_n], 0
    je .st
    cmp al, [xf_mx]
    jne .st
    inc dh
.st:
    mov ax, [xf_vi]                 ; where value number k goes
    cmp byte [xf_kind], 1
    je .tshort
    ja .tmix
    jmp short .tput                 ; long: long[k]
.tshort:
    add ax, X3P_SF_SHRT             ; short: short[k]
    jmp short .tput
.tmix:
    cmp ax, 6                       ; mixed: long[0..5], then short[9 + k - 6]
    jb .tput
    add ax, X3P_SF_SHRT + 9 - 6
.tput:
    add ax, [xf_sf]
    mov di, ax
    mov [di], dl
    mov [di+X3P_SF_ILL], dh
    inc word [xf_vi]
    dec byte [xf_cnt]
    jmp short .val
.pnext:
    inc byte [xf_part]
    cmp byte [xf_part], 4
    jb .part
    ret

; =============================================================================
; HUFFMAN (x3sim.huffman): the 576 values of the channel into its record
; out: CF = 1 when the big values ran past part2_3_length (the record's ix
; is then the caller's to zero). clobbers: everything but DS, ES
;
; The reader's position lives in BP for the whole of it (X3P_S_BITP before
; and after), and every read is one xf_pk: 16 bits at a position, zero at or
; past the data's end - x3sim BitReader.peek's answer.
; =============================================================================
xf_huff:
    mov si, [xf_si]
    mov di, [xf_cr]
    add di, X3G_C_IX
    mov [xf_ix0], di
    mov ax, [si+X3P_SI_BV]          ; nbig = min(2 big_values, 576)
    shl ax, 1
    cmp ax, 576
    jbe .nb
    mov ax, 576
.nb:
    shl ax, 1                       ; ...as a byte offset past ix0
    add ax, di
    mov [xf_nbig], ax
    mov bx, [xf_lc]
    cmp byte [si+X3P_SI_WS], 0
    je .regs
    mov ax, [bx+16]                 ; window switching: lc[8], or for a
    cmp byte [si+X3P_SI_BT], 2      ; short block 3 sc[3]
    jne .w1
    mov bx, [xf_sc]
    mov al, [bx+3]
    xor ah, ah
    mov cx, ax
    shl ax, 1
    add ax, cx
.w1:
    mov cx, 576
    jmp short .rset
.regs:
    mov al, [si+X3P_SI_R0]          ; lc[min(r0 + 1, 22)]
    xor ah, ah
    inc ax
    push ax
    cmp ax, 22
    jbe .r0
    mov ax, 22
.r0:
    shl ax, 1
    add bx, ax
    mov ax, [bx]                    ; r1
    pop cx
    mov bx, [xf_lc]
    add cl, [si+X3P_SI_R1]          ; lc[min(r0 + r1 + 2, 22)]
    inc cx
    cmp cx, 22
    jbe .r1
    mov cx, 22
.r1:
    shl cx, 1
    add bx, cx
    mov cx, [bx]                    ; r2
.rset:
    shl ax, 1                       ; both as byte offsets past ix0
    add ax, [xf_ix0]
    shl cx, 1
    add cx, [xf_ix0]
    mov [xf_r2], cx
    mov bp, [X3P_S_BITP]            ; THE READER, from here in BP
    cmp ax, [xf_nbig]               ; region 0: up to min(r1, nbig)
    jbe .l0
    mov ax, [xf_nbig]
.l0:
    mov [xf_lim], ax
    mov si, [xf_si]
    mov al, [si+X3P_SI_TAB]
    call xf_region
    jc .over
    mov ax, [xf_r2]                 ; region 1: up to min(r2, nbig)
    cmp ax, [xf_nbig]
    jbe .l1
    mov ax, [xf_nbig]
.l1:
    mov [xf_lim], ax
    mov si, [xf_si]
    mov al, [si+X3P_SI_TAB+1]
    call xf_region
    jc .over
    mov ax, [xf_nbig]               ; region 2: the rest of the big values
    mov [xf_lim], ax
    mov si, [xf_si]
    mov al, [si+X3P_SI_TAB+2]
    call xf_region
    jc .over
    ; --- count1: quadruples while bits remain ---
    mov si, [xf_si]
    mov bl, [si+X3P_SI_C1]
    xor bh, bh
    add bx, 15                      ; HUFDIR rows 15, 16: count1 A, B
    call xf_tree
    mov ax, [xf_ix0]
    add ax, 576 * 2 - 8
    mov [xf_lim], ax                ; i + 4 <= 576
.q:
    cmp di, [xf_lim]
    ja .qdone
    cmp bp, [xf_end]
    jae .qdone
    call xf_sym                     ; AL = vwxy, BP past the code
    cmp bp, [xf_end]
    ja .qdone                       ; it would pass the granule's end
    mov ah, al                      ; the four, bit 3 first, each with its
    mov cx, 4                       ; sign: v w x y into xf_q4s
    mov bx, xf_q4s
.qv:
    xor dx, dx
    test ah, 8
    jz .qz
    inc dx
    push ax
    push cx
    push bx
    mov cl, 1
    call xf_gbp
    pop bx
    pop cx
    or al, al
    pop ax
    jz .qz
    mov dx, -1
.qz:
    mov [bx], dx
    inc bx
    inc bx
    shl ah, 1
    loop .qv
    cmp bp, [xf_end]
    ja .qdone                       ; a quadruple past the end: discarded
    mov si, xf_q4s                  ; ...else it is stored
    movsw
    movsw
    movsw
    movsw
    jmp short .q
.qdone:
    ; --- the last non-zero line: back from the last one written; the rest
    ; is zero ---
    mov si, di                      ; SI = one past the last written
    mov cx, [xf_ix0]
    add cx, 576 * 2
    sub cx, di
    shr cx, 1
    xor ax, ax
    rep stosw
    mov di, [xf_ix0]
.nz:
    cmp si, di
    je .nzd
    cmp word [si-2], 0
    jne .nzd
    dec si
    dec si
    jmp short .nz
.nzd:
    sub si, di
    shr si, 1
    mov bx, [xf_cr]
    mov [bx+X3G_C_NZ], si
    mov [X3P_S_BITP], bp
    clc
    ret
.over:
    mov [X3P_S_BITP], bp
    stc
    ret

; xf_region - the pairs from DI up to [xf_lim] with table AL, the reader in
; BP. out: DI advanced; CF = 1 once the reader has passed [xf_end]
; clobbers: AX, BX, CX, DX, SI
xf_region:
    cmp di, [xf_lim]
    jae .ok
    or al, al
    jnz .tab
    mov cx, [xf_lim]                ; table 0: zeros
    sub cx, di
    shr cx, 1
    xor ax, ax
    rep stosw
.ok:
    clc
    ret
.tab:
    mov bl, al
    xor bh, bh
    mov al, [x3ft_lin+bx]
    mov [xf_lin], al
    mov bl, [x3ft_tree+bx]
    call xf_tree
.pair:
    ; THE COMMON CASE: a code that is a first-level leaf (at most 8 bits)
    ; and no 15 with linbits - both signs are then in the same 16-bit window
    mov bx, bp
    call xf_pk                      ; AX = 16 bits at BP
    mov dx, ax                      ; DX = the window
    mov cl, [xf_hsh]
    shr ax, cl
    mov si, ax
    shl si, 1
    add si, [xf_hbase]
    mov ax, [si]                    ; the entry
    test ah, 0x80
    jnz .slow                       ; a pointer: the general walk
    mov cl, ah
    and cl, 15                      ; CL = its length, AL = x << 4 | y
    cmp byte [xf_lin], 0
    je .fast
    mov ah, al
    and ah, 0xF0
    cmp ah, 0xF0
    je .lin                         ; a 15 with linbits: the general values
    mov ah, al
    and ah, 0x0F
    cmp ah, 0x0F
    je .lin
.fast:
    xor ch, ch
    add bp, cx                      ; past the code
    shl dx, cl                      ; the window past it: >= 8 bits left
    mov bl, al
    mov cl, 4
    shr al, cl
    xor ah, ah                      ; x
    or al, al
    jz .x0
    inc bp                          ; its sign
    shl dx, 1
    jnc .x0
    neg ax
.x0:
    stosw
    mov al, bl
    and ax, 15                      ; y
    jz .y0
    inc bp
    shl dx, 1
    jnc .y0
    neg ax
.y0:
    stosw
    jmp short .next
.lin:
    xor ch, ch                      ; a leaf, but linbits: the values the
    add bp, cx                      ; general way
    jmp short .vals
.slow:
    call xf_sym                     ; AL = x << 4 | y, BP past the code
.vals:
    mov dl, al
    mov cl, 4
    shr al, cl
    call xf_val                     ; x (keeps DL)
    stosw
    mov al, dl
    and al, 15
    call xf_val                     ; y
    stosw
.next:
    cmp bp, [xf_end]
    ja .past
    cmp di, [xf_lim]
    jb .pair
    clc
    ret
.past:
    stc
    ret

; xf_val - a pair value AL (0..15) into its signed word: linbits on 15, then
; the sign. out: AX. keeps DX, DI. clobbers: BX, CX, SI
xf_val:
    xor ah, ah
    or al, al
    jz .out
    cmp al, 15
    jne .sign
    mov cl, [xf_lin]
    or cl, cl
    jz .sign
    call xf_gbp
    add ax, 15
.sign:
    mov bx, bp                      ; the sign: the next bit
    push ax
    call xf_pk
    inc bp
    shl ax, 1
    pop ax
    jnc .out
    neg ax
.out:
    ret

; xf_tree - make HUFDIR row BX the current tree. clobbers: AX, BX, CX
xf_tree:
    shl bx, 1
    shl bx, 1
    mov al, [xf_hdir+bx+1]          ; its first-level width
    mov cl, 16
    sub cl, al
    mov [xf_hsh], cl
    mov ax, [xf_hdir+bx+2]          ; its first entry
    shl ax, 1
    add ax, xf_huf
    mov [xf_hbase], ax
    ret

; xf_sym - one symbol of the current tree from BP (x3tables.huff_decode)
; out: AL = the value, BP past its code. clobbers: AH, BX, CX, SI
xf_sym:
    mov bx, bp
    call xf_pk
    mov cl, [xf_hsh]
    shr ax, cl
    mov si, ax
    shl si, 1
    add si, [xf_hbase]
    mov ax, [si]                    ; the entry
    test ah, 0x80
    jnz .walk
    mov cl, ah                      ; a leaf at the first level: the common
    and cl, 15                      ; case (1.04 lookups a symbol)
    xor ch, ch
    add bp, cx
    ret
.walk:
    mov [xf_p0], bp
    mov byte [xf_used], 0
    mov cl, 16                      ; the width this lookup consumed
    sub cl, [xf_hsh]
.w:
    test ah, 0x80
    jz .leaf
    add [xf_used], cl               ; a pointer: its level is consumed
    mov cl, ah                      ; its width w = bits 11..14
    shr cl, 1
    shr cl, 1
    shr cl, 1
    and cl, 15
    and ah, 7                       ; its offset = bits 0..10
    mov [xf_off], ax
    mov [xf_w], cl
    mov bx, [xf_p0]
    add bl, [xf_used]
    adc bh, 0
    call xf_pk
    mov cl, 16
    sub cl, [xf_w]
    shr ax, cl
    add ax, [xf_off]
    mov si, ax
    shl si, 1
    add si, [xf_hbase]
    mov ax, [si]
    mov cl, [xf_w]
    jmp short .w
.leaf:
    mov bl, ah                      ; a leaf: bits 8..11 are this level's
    and bl, 15
    add bl, [xf_used]
    xor bh, bh
    add bx, [xf_p0]
    mov bp, bx
    ret

; xf_pk - 16 bits at bit position BX, MSB first; 0 at or past the end
; out: AX   clobbers: CX, SI
xf_pk:
    xor ax, ax
    cmp bx, [X3P_S_BITN]
    jae .out
    mov si, bx
    mov cl, 3
    shr si, cl
    add si, [X3P_S_MD0]
    mov ax, [si]
    xchg ah, al
    mov cl, bl
    and cl, 7
    jz .out
    mov ch, [si+2]
    shl ax, cl
    neg cl
    add cl, 8
    shr ch, cl
    or al, ch
.out:
    ret

; xf_gbp - CL = n bits (1..16) from BP -> AX; BP += n. clobbers: BX, CX, SI
xf_gbp:
    push cx
    mov bx, bp
    call xf_pk
    pop cx
    xor ch, ch
    add bp, cx
    neg cl
    add cl, 16
    shr ax, cl
    ret

; =============================================================================
; THE LINE MAP (x3sim Bands.line_map) as blocks of lines that share a band and
; a window: [xf_blk + 160 ch], [xf_nblk + ch] of them, 4 bytes each:
;   db band, db kind (0 long, 80h + window short), dw lines
; clobbers: AX, BX, CX, DX, SI, DI
; =============================================================================
xf_blocks:
    mov si, [xf_si]
    mov al, 160
    mul byte [xf_ch]
    add ax, xf_blk
    mov di, ax
    mov dx, di
    cmp byte [si+X3P_SI_BT], 2
    je .short
    mov bx, [xf_lc]                 ; long: 22 bands
    xor cx, cx
.lb:
    mov al, cl
    xor ah, ah
    stosw                           ; band, kind 0
    mov ax, [bx+2]
    sub ax, [bx]
    stosw
    inc bx
    inc bx
    inc cl
    cmp cl, 22
    jb .lb
    jmp short .done
.short:
    xor cl, cl                      ; first short band
    cmp byte [si+X3P_SI_MIX], 0
    je .sb
    mov bx, [xf_lc]                 ; mixed: 8 long bands (MPEG-1) or 6
    mov ch, 8
    cmp byte [X3P_S_VER], 1
    je .ml
    mov ch, 6
.ml:
    mov al, cl
    xor ah, ah
    stosw
    mov ax, [bx+2]
    sub ax, [bx]
    stosw
    inc bx
    inc bx
    inc cl
    cmp cl, ch
    jb .ml
    mov cl, 3
.sb:
    mov bx, [xf_sc]
.sband:
    mov al, cl
    xor ah, ah
    mov si, ax
    mov al, [bx+si+1]               ; this band's width
    sub al, [bx+si]
    xor ch, ch
.sw:
    push ax
    mov ah, ch
    or ah, 0x80
    xchg al, cl                     ; band, kind
    stosw
    xchg al, cl
    pop ax
    push ax
    xor ah, ah
    stosw
    pop ax
    inc ch
    cmp ch, 3
    jb .sw
    inc cl
    cmp cl, 13
    jb .sband
.done:
    sub di, dx
    shr di, 1
    shr di, 1
    mov bl, [xf_ch]
    xor bh, bh
    mov ax, di
    mov [xf_nblk+bx], al
    ret

; =============================================================================
; xf_qexp - the quarter exponents (x3sim.quarter_exponents) for the lines
; below the channel's NZ. clobbers: everything but DS, ES
; =============================================================================
xf_qexp:
    mov si, [xf_si]
    mov al, [si+X3P_SI_GG]          ; base = global_gain - 210 - 2 (M/S)
    xor ah, ah
    sub ax, 210
    cmp byte [xf_ms], 0
    je .b
    dec ax
    dec ax
.b:
    mov [xf_base], ax
    mov cl, 1                       ; x2, or x4 with scalefac_scale
    cmp byte [si+X3P_SI_SCL], 0
    je .m
    inc cl
.m:
    mov [xf_msh], cl
    mov di, [xf_cr]
    mov dx, [di+X3G_C_NZ]           ; lines still to fill
    add di, X3G_C_Q
    mov al, 160
    mul byte [xf_ch]
    add ax, xf_blk
    mov bx, ax
.blk:
    or dx, dx
    jz .out
    mov al, [bx+1]
    test al, 0x80
    jnz .sh
    mov al, [bx]                    ; long: base - m (sf[b] + pretab[b])
    xor ah, ah
    mov si, ax
    add si, [xf_sf]
    mov cl, [si]
    xor ch, ch
    cmp byte [xf_pre], 0
    je .lp
    mov si, ax
    add cl, [x3ft_pre+si]
.lp:
    jmp short .fill
.sh:
    and al, 3                       ; short: base - 8 sbg[w] - m sf[b][w]
    xor ah, ah
    mov si, ax
    add si, [xf_si]
    mov ch, [si+X3P_SI_SBG]
    mov si, ax                      ; window
    mov al, [bx]
    mov ah, 3
    mul ah
    add si, ax
    add si, [xf_sf]
    mov cl, [si+X3P_SF_SHRT]
    jmp short .fills
.fill:
    mov ax, cx
    mov cl, [xf_msh]
    shl ax, cl
    mov cx, [xf_base]
    sub cx, ax
    mov ax, cx
    jmp short .put
.fills:
    mov al, cl                      ; m sf
    xor ah, ah
    mov cl, [xf_msh]
    shl ax, cl
    push ax
    mov al, ch                      ; 8 sbg
    xor ah, ah
    mov cl, 3
    shl ax, cl
    pop cx
    add ax, cx
    mov cx, [xf_base]
    sub cx, ax
    mov ax, cx
.put:
    mov cx, [bx+2]                  ; this block's lines, up to NZ
    cmp cx, dx
    jbe .c
    mov cx, dx
.c:
    sub dx, cx
    rep stosw
    add bx, 4
    jmp .blk
.out:
    ret

; =============================================================================
; xf_stereo - the record's X3G_PLAN, and for an intensity frame the per-line
; plan from the RIGHT channel (x3sim.stereo_plan). clobbers: all but DS, ES
; =============================================================================
xf_stereo:
    mov di, X3P_REC
    cmp byte [X3P_S_NCH], 2
    jne .lr
    cmp byte [X3P_S_MODE], 1
    jne .lr
    cmp byte [xf_okc], 0
    je .lr
    cmp byte [xf_okc+2], 0
    je .lr
    test byte [X3P_S_MEXT], 1
    jnz .is
    cmp byte [xf_ms], 0
    je .lr
    mov byte [di+X3G_PLAN], X3G_PLAN_MS
    ret
.lr:
    mov byte [di+X3G_PLAN], X3G_PLAN_LR
    ret
.is:
    mov byte [di+X3G_PLAN], X3G_PLAN_PL
    mov byte [xf_ch], 1             ; the right channel's tables
    mov ax, X3P_SCF + X3P_SF_SZ
    mov [xf_sf], ax
    mov ax, X3P_SIDE + X3P_SI_SZ    ; [gr][1]
    mov cl, [xf_gr]
    or cl, cl
    jz .s0
    add ax, 2 * X3P_SI_SZ
.s0:
    mov [xf_si], ax
    mov si, ax
    mov byte [xf_short], 0
    cmp byte [si+X3P_SI_BT], 2
    jne .ns
    cmp byte [si+X3P_SI_MIX], 0
    jne .ns
    mov byte [xf_short], 1
.ns:
    ; --- last: the right channel's last non-zero line and its block ---
    mov bx, [X3P_REC+X3G_CH1+X3G_C_NZ]
    dec bx                          ; last (-1 when none)
    mov [xf_last], bx
    mov word [xf_top], 0xFFFF       ; top[w] = -1
    mov byte [xf_top+2], 0xFF
    mov byte [xf_bl], 0xFF          ; the block holding last (FFh none)
    mov byte [xf_blb], 0
    mov byte [xf_blk_k], 0xFF
    mov si, xf_blk + 160            ; walk the blocks: lines from DX
    mov cl, [xf_nblk+1]
    xor ch, ch
    xor dx, dx
    xor bp, bp                      ; block index
.wb:
    mov ax, [si+2]                  ; does it hold last?
    add ax, dx
    cmp word [xf_last], 0
    jl .tp
    cmp [xf_last], dx
    jl .tp
    cmp [xf_last], ax
    jge .tp
    mov ax, bp
    mov [xf_bl], al
    mov al, [si]
    mov [xf_blb], al
    mov al, [si+1]
    mov [xf_blk_k], al
.tp:
    cmp byte [xf_short], 0          ; short: is the block non-zero? top[w]
    je .wn
    push cx
    push si
    mov cx, [si+2]
    mov bx, dx
    shl bx, 1
    add bx, X3P_REC+X3G_CH1+X3G_C_IX
.nzs:
    cmp word [bx], 0
    jne .hit
    inc bx
    inc bx
    loop .nzs
    jmp short .nh
.hit:
    mov al, [si+1]
    and al, 3
    xor ah, ah
    mov bx, ax
    mov al, [si]
    mov [xf_top+bx], al
.nh:
    pop si
    pop cx
.wn:
    add dx, [si+2]
    add si, 4
    inc bp
    loop .wb
    ; --- the plan, block by block ---
    mov al, 0                       ; the base: L/R, or M/S
    cmp byte [xf_ms], 0
    je .bs
    inc al
.bs:
    mov [xf_pbase], al
    mov di, X3P_REC+X3G_PL
    mov si, xf_blk + 160
    mov cl, [xf_nblk+1]
    xor ch, ch
    xor bp, bp
.pb:
    push cx
    mov al, [xf_pbase]              ; not above: the base
    mov bl, [si+1]
    test bl, 0x80
    jz .plong
    ; a short block (band, window)
    and bl, 3
    xor bh, bh
    cmp byte [xf_short], 0
    je .pmix
    mov dl, [si]                    ; short: above = band > top[w]
    cmp dl, [xf_top+bx]
    jg .pas
    jmp .pfill
.pmix:
    mov dx, bp                      ; mixed: above = block > the last's
    cmp dl, [xf_bl]
    jle .pf1
    cmp byte [xf_bl], 0xFF
    je .pas
    jmp short .pas
.pf1:
    cmp byte [xf_bl], 0xFF
    jne .pfill
.pas:                               ; above, short: bb = min(band, 11)
    mov dl, [si]
    mov dh, dl
    cmp dl, 11
    jbe .pa1
    mov dl, 11
.pa1:
    mov al, 3
    mul dl
    add ax, bx
    add ax, [xf_sf]
    mov bx, ax
    mov al, [bx+X3P_SF_SHRT]        ; pos
    mov ah, [bx+X3P_SF_ILL+22]      ; ill
    mov dl, 0                       ; below = short and b == 12 and top[w] >= 11
    cmp byte [xf_short], 0
    je .pdec
    cmp dh, 12
    jne .pdec
    mov bl, [si+1]
    and bl, 3
    xor bh, bh
    cmp byte [xf_top+bx], 11
    jl .pdec
    mov dl, 1
    jmp short .pdec
.plong:                             ; a long block
    mov dx, bp
    cmp byte [xf_bl], 0xFF
    je .pal
    cmp dl, [xf_bl]
    jbe .pfill
.pal:                               ; above, long: bb = min(band, 20)
    mov dl, [si]
    mov dh, dl
    cmp dl, 20
    jbe .pa2
    mov dl, 20
.pa2:
    xor dh, dh
    mov bx, dx
    add bx, [xf_sf]
    mov al, [bx]                    ; pos
    mov ah, [bx+X3P_SF_ILL]         ; ill
    mov dl, 0                       ; below = b == 21 and the last is in a
    cmp byte [si], 21               ; long band >= 20
    jne .pdec
    cmp byte [xf_bl], 0xFF
    je .pdec
    test byte [xf_blk_k], 0x80
    jnz .pdec
    cmp byte [xf_blb], 20
    jb .pdec
    mov dl, 1
.pdec:
    cmp byte [X3P_S_VER], 1
    jne .pd2
    xor ah, ah                      ; MPEG-1: ill = pos >= 7
    cmp al, 7
    jb .pd2
    inc ah
.pd2:
    or dl, dl
    jz .pd3
    xor ah, ah                      ; the default position
    mov al, 3
    cmp byte [X3P_S_VER], 1
    je .pd3
    xor al, al
.pd3:
    or ah, ah
    jz .pis
    mov al, [xf_pbase]              ; illegal: not intensity
    jmp short .pfill
.pis:
    or al, X3G_PL_IS
.pfill:
    mov cx, [si+2]
    rep stosb
    add si, 4
    inc bp
    pop cx
    dec cx
    jz .pdone
    jmp .pb
.pdone:
    ret

; =============================================================================
; CHECK - the Huffman tables against x3tables.py's CRC-32s
; in:  DS = this part's data segment   out: CF = 0 / CF = 1 AX = X3E_TABLE
; =============================================================================
xf_check:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, xf_hdir
    mov cx, X3T_HUFDIR_LEN
    mov ax, 0xFFFF
    mov dx, ax
    call x3g_crc32
    not ax
    not dx
    cmp ax, X3T_HUFDIR_CRC & 0xFFFF
    jne .bad
    cmp dx, X3T_HUFDIR_CRC >> 16
    jne .bad
    mov si, xf_huf
    mov cx, X3T_HUF_LEN
    mov ax, 0xFFFF
    mov dx, ax
    call x3g_crc32
    not ax
    not dx
    cmp ax, X3T_HUF_CRC & 0xFFFF
    jne .bad
    cmp dx, X3T_HUF_CRC >> 16
    jne .bad
    clc
    jmp short .out
.bad:
    mov ax, X3E_TABLE
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

%include "x3crc.inc"
%include "x3phdr.inc"               ; the header, shared with x3pars.inc
%include "x3pnext.inc"              ; the frame loop (NEXT)

; =============================================================================
; TABLES
; =============================================================================
    X3FTAB_FRONT
    X3FTAB_PARSE                    ; (bitrates and rates, for x3phdr.inc)
    align 2
xf_hdir:    incbin "x3std.bin", X3T_HUFDIR_OFF, X3T_HUFDIR_LEN
    align 2
xf_huf:     incbin "x3std.bin", X3T_HUF_OFF, X3T_HUF_LEN

; =============================================================================
; THE PART'S OWN VARIABLES (zero at load; nothing outlives a GRAN but the
; scalefactors in the fixed block)
; =============================================================================
    align 2
xf_k:       db 0                    ; (version, rate) index
xf_gr:      db 0
xf_ch:      db 0
xf_ms:      db 0
xf_okc:     dw 0, 0                 ; channel ok (low bytes)
xf_lc:      dw 0                    ; this version and rate's long bands...
xf_sc:      dw 0                    ; ...and short ones
xf_si:      dw 0                    ; the channel's side information...
xf_cr:      dw 0                    ; ...its record...
xf_sf:      dw 0                    ; ...its scalefactors
xf_end:     dw 0                    ; part2_3_length's end, bits
xf_pre:     db 0                    ; preflag in effect
xf_isr:     db 0                    ; 1 = the MPEG-2 intensity right channel
xf_sl:      db 0, 0, 0, 0           ; slen1/2, or the four MPEG-2 slens
xf_tbl:     db 0
xf_kind:    db 0
xf_part:    db 0
xf_cnt:     db 0
xf_n:       db 0
xf_mx:      db 0
xf_b:       db 0
xf_be:      db 0
xf_w:       db 0
xf_scfsi:   db 0
xf_nrp:     dw 0
xf_vi:      dw 0
xf_ix0:     dw 0                    ; Huffman: ix[0]'s address
xf_nbig:    dw 0                    ; ...the big values' end, the regions
xf_r2:      dw 0
xf_lim:     dw 0
xf_lin:     db 0
xf_v:       db 0
xf_hsh:     db 0                    ; the tree: 16 - its first width...
xf_used:    db 0
xf_hbase:   dw 0                    ; ...its entries
xf_p0:      dw 0
xf_off:     dw 0
xf_m:       db 0
xf_q4i:     dw 0
xf_q4s:     dw 0, 0, 0, 0
xf_base:    dw 0                    ; the exponents' base and multiplier
xf_msh:     db 0
xf_short:   db 0                    ; the plan: a short (not mixed) block
xf_pbase:   db 0
xf_bl:      db 0                    ; ...the block holding last, its band and
xf_blb:     db 0                    ; kind
xf_blk_k:   db 0
xf_top:     db 0, 0, 0
    align 2
xf_last:    dw 0
x3p_rdesc:  dw 0                    ; NEXT: the ring descriptor's offset...
x3p_rseg:   dw 0                    ; ...the ring's segment, mask, read
x3p_rmask:  dw 0                    ; position...
x3p_rrd:    dw 0
x3p_ravail: dw 0                    ; ...the bytes it holds...
x3p_sbase:  dw 0                    ; ...the side information's offset...
x3p_sbp:    dw 0                    ; ...the side reader's bit...
x3p_mlen:   dw 0                    ; ...the main data's length, offset...
x3p_mofs:   dw 0
x3p_hreg:   dw 0, 0, 0, 0           ; ...a scan's header...
x3p_reof:   db 0                    ; ...the ring's EOF...
x3p_verd:   db 0                    ; ...the verdict...
x3p_sres:   db 0                    ; ...a reserved field seen
xf_nblk:    db 0, 0
xf_blk:     times 2 * 160 db 0      ; the line maps, a channel
