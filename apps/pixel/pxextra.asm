; =============================================================================
; os8088 - apps/pixel/pxextra.asm
;
; PiXEL's EXTRAS, a far-called lazy PART LINKED against the package (SPEC.md
; 106.25, 106.20): part 9 of PIXEL.O88. Four formats a period PC met and
; this OS's own era made - TIFF, ICO and CUR, IFF (Deluxe Paint's ILBM and
; PBM) and MacPaint - with 106.18's shape: HEAD on the UI task, DECODE on
; the worker, rows to the package's ONE emitter through K_EMIT.
;
;   HEAD     a STATE MACHINE over heads: a structure the head does not hold
;            is asked for (PXD_MORE, 106.19's mechanism - the resident reads
;            2,048 bytes from that file offset and calls HEAD again), so a
;            walk resumes at the phase it asked from ([ex_ph]). Sixteen heads
;            at most. What DECODE needs and the head walk found - TIFF's
;            strips, ICO's AND mask - goes into the TABLE, 8 KB of this
;            part's own memory
;   DECODE   the worker: the stream skipped forward, the format's bytes
;            unpacked into a row at a time and handed to K_EMIT
;
; A PNG inside an ICO is not read here: HEAD moves the head down so the PNG
; starts at its byte 0, sets the stream's base ([px_sbase]) and answers
; PXD_REDIR, and the resident hands the picture to the PNG part (106.25).
;
; tools/pixelsim.py's tiff_*, ico_*, lbm_* and mac_* are this file in
; Python, check for check, and tests/pxdecode.py holds the two to the byte.
; =============================================================================

%include "pxpart.inc"
%include "os88api.inc"               ; (OSAPI_TASK_ALIVE, for the plans)
%include "pxrec.inc"
%include "pxlink.inc"

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD ex_init, ex_decode, ex_info, ex_headv, ex_plans

EX_HEADS    equ 16                  ; heads a walk may read (106.19's rule)
EX_HEADMAX  equ 2048                ; ...each the bytes from where it was asked
EX_TABLE    equ 8192                ; the TABLE's bytes
EX_STRIPS   equ 1024                ; TIFF strips the TABLE holds
EX_CNTS     equ 4096                ; ...their byte counts' place in it
EX_DONE     equ 0x7FFF              ; LZW_RUN's "the strip is whole"

%define LZW_BSS     ex_lzw
%define LZW_FILL    tf_lfill
%define LZW_RUN     tf_lrun
%define LZW_EBAD    PXD_DATA
%define LZW_EARLY   1               ; TIFF's code size grows a code early...
%define LZW_MSB     1               ; ...and its codes are MSB first

; PXV_INIT - out AX = PXP_PROBE
ex_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_INFO - nothing to say
ex_info:
    mov ax, PXE_NOTSUP
    stc
    retf

; PXV_PLANS - [px_plan] for the picture's palette (106.20): pxplan.inc's
ex_plans:
    call pl_plans
    retf

; PXV_HEAD - ex_head's walk, and the NEXT head it asks for read here
; (pxhmore.inc), until it answers
ex_headv:
    call ex_head
    jnc .x
    cmp ax, PXD_MORE
    jne .no
    call ph_more
    jnc ex_headv
.no:
    stc
.x:
    retf

%include "pxhmore.inc"

; =============================================================================
; PXV_HEAD - the UI task: DS = the package, DI = the context. The format is
; the sniff's ([px_cur + PXR_FMT]); the head is [PXK_HSEG]:0, its bytes from
; file offset HBASE + HPOS to HBASE + HLEN
; =============================================================================
ex_head:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    mov [cs:ex_hsp], sp             ; (ex_hfail's way out from any depth)
    cmp word [8], PXL_IMAGE         ; THE STAMP (106.20)
    jne .link
    cmp word [10], PXL_BSS
    jne .link
    cmp byte [di + PXK_HCNT], 0     ; the first call of a walk
    jne .res
    mov byte [di + PXK_HCNT], 1
    mov byte [cs:ex_ph], 0
.res:
    mov ax, [di + PXK_HBASE]        ; the head's window, [hlo, hhi): from the
    mov dx, [di + PXK_HBASE + 2]    ; offset asked for (HPOS), not from its
    add ax, [di + PXK_HPOS]         ; cluster's start, and 2,048 bytes at
    adc dx, 0                       ; most - pixelsim's head is the 2,048
    mov [cs:ex_hlo], ax             ; bytes FROM that offset, so the heads
    mov [cs:ex_hlo + 2], dx         ; a walk counts are its, on a volume of
    mov bx, [di + PXK_HLEN]         ; any cluster
    sub bx, [di + PXK_HPOS]
    cmp bx, EX_HEADMAX
    jbe .hl
    mov bx, EX_HEADMAX
.hl:
    add ax, bx
    adc dx, 0
    mov [cs:ex_hhi], ax
    mov [cs:ex_hhi + 2], dx
    mov al, [px_cur + PXR_FMT]
    cmp al, PXF_TIFF
    jne .i
    jmp tf_head
.i:
    cmp al, PXF_ICO
    jne .l
    jmp ic_head
.l:
    cmp al, PXF_LBM
    jne .m
    jmp lb_head
.m:
    cmp al, PXF_MAC
    jne .n
    jmp mc_head
.n:
    mov ax, PXD_NOTPIC              ; (the resident sends only these four)
    jmp ex_hfail
.link:
    mov ax, PXD_LINK
    jmp ex_hfail

; ex_hok - a format's HEAD done, its answers in the context
ex_hok:
    clc
ex_hret:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret                             ; (to ex_headv, the vector)

; ex_hfail - AX = a PXD_* (PXD_MORE included): out of HEAD from any depth
ex_hfail:
    ; STKBALANCE-OK: HEAD's way out from any depth - SP back to ex_head's
    ; own frame from [ex_hsp], so what was pushed after it is abandoned
    mov sp, [cs:ex_hsp]
    stc
    jmp short ex_hret

; ex_need - DX:AX = a file offset, CX = bytes (at most 2,048) a structure
; needs WHOLE in one head: ES:SI = them when this head holds them. Else it
; does not return: `cut short` when the FILE does not hold them (checked
; before a head is counted), `bad header` past sixteen heads, and otherwise
; PXD_MORE with that offset asked for. DI = the context. Clobbers BX
ex_need:
    cmp dx, [cs:ex_hlo + 2]         ; off >= hlo...
    jb .out
    ja .lo
    cmp ax, [cs:ex_hlo]
    jb .out
.lo:
    push ax
    push dx
    add ax, cx                      ; ...and off + n <= hhi
    adc dx, 0
    jc .out2
    cmp dx, [cs:ex_hhi + 2]
    ja .out2
    jb .in
    cmp ax, [cs:ex_hhi]
    ja .out2
.in:
    pop dx
    pop ax
    mov si, ax
    sub si, [di + PXK_HBASE]
    mov es, [di + PXK_HSEG]
    ret
.out2:
    pop dx
    pop ax
.out:
    mov bx, ax                      ; THE FILE must hold them
    mov si, dx
    add bx, cx
    adc si, 0
    jc ex_short
    cmp si, [di + PXK_FSZ + 2]
    ja ex_short
    jb ex_more
    cmp bx, [di + PXK_FSZ]
    ja ex_short
; ex_more - DX:AX = the offset the next head starts at
ex_more:
    ; STKBALANCE-OK: reached from any depth of HEAD; it leaves by ex_hfail,
    ; which puts SP back to the frame from [ex_hsp]
    mov [di + PXK_HNEXT], ax
    mov [di + PXK_HNEXT + 2], dx
    inc byte [di + PXK_HCNT]
    cmp byte [di + PXK_HCNT], EX_HEADS
    ja ex_badh
    mov ax, PXD_MORE
    jmp ex_hfail
ex_short:
    ; STKBALANCE-OK: a refusal from any depth of HEAD - ex_hfail puts SP
    ; back to the frame from [ex_hsp]
    mov ax, PXD_TRUNC
    jmp ex_hfail
ex_badh:
    ; STKBALANCE-OK: as ex_short
    mov ax, PXD_HEAD
    jmp ex_hfail

; ex_run - DX:AX = a file offset, CX = bytes (at most EX_TABLE): a RUN of
; them copied into CS:[ex_rdst], through as many heads as it takes - the
; file must hold all of it (`cut short`, before any head), then what this
; head holds of it is copied and the next head starts at the first byte it
; did not. [ex_rdone] counts across the calls (its caller zeroes it). DI =
; the context. Clobbers BX, SI
ex_run:
    push ax
    push dx
    add ax, cx
    adc dx, 0
    jc ex_short
    cmp dx, [di + PXK_FSZ + 2]
    ja ex_short
    jb .ok
    cmp ax, [di + PXK_FSZ]
    ja ex_short
.ok:
    pop dx
    pop ax
.l:
    mov bx, [cs:ex_rdone]
    cmp bx, cx
    jb .more
    ret
.more:
    push ax
    push dx
    add ax, bx                      ; DX:AX = the next byte wanted
    adc dx, 0
    cmp dx, [cs:ex_hlo + 2]         ; in this head?
    jb .new
    ja .c1
    cmp ax, [cs:ex_hlo]
    jb .new
.c1:
    cmp dx, [cs:ex_hhi + 2]
    ja .new
    jb .in
    cmp ax, [cs:ex_hhi]
    jae .new
.in:
    push cx
    mov si, ax                      ; SI = its place in the head
    sub si, [di + PXK_HBASE]
    mov bx, [cs:ex_hhi]             ; what the head holds from there...
    sub bx, ax
    sub cx, [cs:ex_rdone]           ; ...against what is still wanted
    cmp bx, cx
    jbe .k
    mov bx, cx
.k:
    mov cx, bx
    push di
    push ds
    push es
    mov ax, [di + PXK_HSEG]
    mov di, [cs:ex_rdst]            ; ES:DI = the TABLE's place for them
    add di, [cs:ex_rdone]
    add [cs:ex_rdone], cx
    push cs
    pop es
    mov ds, ax                      ; DS:SI = the head's
    cld
    rep movsb
    pop es
    pop ds
    pop di
    pop cx
    pop dx
    pop ax
    jmp short .l
.new:
    jmp ex_more                     ; (DX:AX: that byte)

; ex_w / ex_d - the word / dword at ES:SI in the file's byte order
; ([ex_be]): AX / DX:AX. Preserve all else
ex_w:
    mov ax, [es:si]
    cmp byte [cs:ex_be], 0
    je .x
    xchg al, ah
.x:
    ret
ex_d:
    cmp byte [cs:ex_be], 0
    jne .be
    mov ax, [es:si]
    mov dx, [es:si + 2]
    ret
.be:
    mov dx, [es:si]
    xchg dl, dh
    mov ax, [es:si + 2]
    xchg al, ah
    ret

; ex_dim - DX:AX = a dimension: CF = 1 unless 1..PX_DIMMAX. Preserves all
ex_dim:
    or dx, dx
    jnz .no
    or ax, ax
    jz .no
    cmp ax, PX_DIMMAX
    ja .no
    clc
    ret
.no:
    stc
    ret

; ex_ans - the context's answers that every format gives: AX = width, BX =
; height, CL = RF_*, CH = bits, DL = PK_*, SI = palette entries, BP = the
; scratch's paragraphs, DH = PXK_FLAGS. DI = the context. Preserves all
ex_ans:
    mov [di + PXK_SW], ax
    mov [di + PXK_SH], bx
    mov [di + PXK_RF], cl
    mov [di + PXK_BITS], ch
    mov [di + PXK_PACK], dl
    mov [di + PXK_NPAL], si
    mov [di + PXK_DPARA], bp
    mov [di + PXK_FLAGS], dh
    ret

; =============================================================================
; TIFF (SPEC.md 106.25). The tags read, in the order their values are: a
; SLOT each - its type, count and the entry's own four bytes as scanned,
; then its first value
; =============================================================================
TS_W        equ 0                   ; 256 ImageWidth
TS_H        equ 1                   ; 257 ImageLength
TS_BPS      equ 2                   ; 258 BitsPerSample
TS_COMP     equ 3                   ; 259 Compression
TS_PHOT     equ 4                   ; 262 PhotometricInterpretation
TS_FILL     equ 5                   ; 266 FillOrder
TS_SOFF     equ 6                   ; 273 StripOffsets (an array)
TS_SPP      equ 7                   ; 277 SamplesPerPixel
TS_RPS      equ 8                   ; 278 RowsPerStrip
TS_SCNT     equ 9                   ; 279 StripByteCounts (an array)
TS_PLAN     equ 10                  ; 284 PlanarConfiguration
TS_PRED     equ 11                  ; 317 Predictor
TS_CMAP     equ 12                  ; 320 ColorMap (an array)
TS_XS       equ 13                  ; 338 ExtraSamples
TS_N        equ 14
tf_tags:    dw 256, 257, 258, 259, 262, 266, 273, 277, 278, 279, 284, 317
            dw 320, 338

tf_head:
    mov bl, [cs:ex_ph]
    xor bh, bh
    shl bx, 1
    jmp [cs:tf_phase + bx]
tf_phase:   dw tf_p0, tf_p1, tf_p2, tf_p3, tf_p4, tf_p5, tf_p6

; --- 0: the 8-byte header, in the first head --------------------------------
tf_p0:
    mov es, [di + PXK_HSEG]
    cmp word [di + PXK_HLEN], 8
    jb .bad
    mov ax, [es:0]
    mov byte [cs:ex_be], 0
    cmp ax, 'II'
    je .o
    inc byte [cs:ex_be]
    cmp ax, 'MM'
    jne .bad
.o:
    mov si, 2
    call ex_w
    cmp ax, 42
    jne .bad
    mov si, 4
    call ex_d
    or dx, dx                       ; IFD0 at 8 or more
    jnz .i
    cmp ax, 8
    jb .bad
.i:
    mov [cs:tf_ifd], ax
    mov [cs:tf_ifd + 2], dx
    xor ax, ax
    mov [cs:tf_have], ax
    mov [cs:tf_tiled], al
    mov byte [cs:ex_ph], 1
    jmp short tf_p1
.bad:
    jmp ex_badh

; --- 1: the IFD's count, 1..170 ------------------------------------------------
tf_p1:
    mov ax, [cs:tf_ifd]
    mov dx, [cs:tf_ifd + 2]
    mov cx, 2
    call ex_need
    call ex_w
    or ax, ax
    jz .bad
    cmp ax, 170
    ja .bad
    mov [cs:tf_n], ax
    mov byte [cs:ex_ph], 2
    jmp short tf_p2
.bad:
    jmp ex_badh

; --- 2: the whole IFD from ONE head, scanned: nothing read through a head ------
tf_p2:
    mov ax, [cs:tf_n]               ; 2 + 12 n bytes
    mov [cs:tf_cnt], ax
    mov cx, 12
    mul cx
    add ax, 2
    mov cx, ax
    mov ax, [cs:tf_ifd]
    mov dx, [cs:tf_ifd + 2]
    call ex_need                    ; ES:SI = the IFD
    add si, 2
.e:
    call ex_w                       ; the tag
    xor bx, bx
.f:
    cmp ax, [cs:tf_tags + bx]
    je .slot
    add bx, 2
    cmp bx, 2 * TS_N
    jb .f
    cmp ax, 322                     ; a tile tag: noted, its type not read
    jb .nx
    cmp ax, 324
    ja .nx
    mov byte [cs:tf_tiled], 1
    jmp short .nx
.slot:
    shr bx, 1                       ; BX = the slot
    push si
    add si, 2
    call ex_w                       ; its type: SHORT or LONG
    cmp ax, 3
    je .ty
    cmp ax, 4
    jne .bad
.ty:
    mov [cs:tf_ty + bx], al
    add si, 2
    call ex_d                       ; its count: not 0
    mov cx, ax
    or cx, dx
    jz .bad
    mov cl, bl                      ; present (a later one replaces it)
    push ax
    mov ax, 1
    shl ax, cl
    or [cs:tf_have], ax
    pop ax
    shl bx, 1
    shl bx, 1
    mov [cs:tf_cn + bx], ax
    mov [cs:tf_cn + bx + 2], dx
    mov ax, [es:si + 4]             ; the entry's own four bytes, as they are
    mov [cs:tf_rw + bx], ax
    mov ax, [es:si + 6]
    mov [cs:tf_rw + bx + 2], ax
    pop si
.nx:
    add si, 12
    dec word [cs:tf_cnt]
    jnz .e
    mov byte [cs:tf_vi], 0
    mov byte [cs:ex_ph], 3
    jmp short tf_p3
.bad:
    pop si                          ; (the entry's place)
    jmp ex_badh

; --- 3: THE VALUES, slot by slot: the first, from the entry's own bytes when
; count x size <= 4, else from a head at the offset they hold ----------------
tf_p3:
    mov bl, [cs:tf_vi]
    xor bh, bh
    cmp bx, TS_N
    jb .v
    mov byte [cs:ex_ph], 4
    jmp tf_p4
.v:
    cmp bl, TS_SOFF                 ; (the three arrays are read later)
    je .nx
    cmp bl, TS_SCNT
    je .nx
    cmp bl, TS_CMAP
    je .nx
    call tf_has
    jz .nx
    call tf_first                   ; DX:AX
    mov bl, [cs:tf_vi]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov [cs:tf_v + bx], ax
    mov [cs:tf_v + bx + 2], dx
.nx:
    inc byte [cs:tf_vi]
    jmp short tf_p3

; tf_has - BX = a slot: ZF = 0 when the IFD had it. Preserves all
tf_has:
    push ax
    push cx
    mov cl, bl
    mov ax, 1
    shl ax, cl
    test [cs:tf_have], ax
    pop cx
    pop ax
    ret

; tf_first - BX = a present slot: DX:AX = its first value - the entry's own
; bytes when count x size <= 4, else read at the offset they hold (ex_need:
; a head, perhaps). Clobbers BX, CX, SI, ES
tf_first:
    mov cx, 2                       ; CX = the size
    cmp byte [cs:tf_ty + bx], 3
    je .s
    mov cx, 4
.s:
    shl bx, 1
    shl bx, 1
    cmp word [cs:tf_cn + bx + 2], 0 ; count x size <= 4: count 1, or 2 SHORTs
    jne .far
    cmp word [cs:tf_cn + bx], 1
    je .in
    cmp word [cs:tf_cn + bx], 2
    jne .far
    cmp cx, 2
    jne .far
.in:
    push cs
    pop es
    lea si, [bx + tf_rw]
    jmp short .val
.far:
    push cx
    push cs
    pop es
    lea si, [bx + tf_rw]
    call ex_d                       ; DX:AX = where
    pop cx
    call ex_need
.val:
    cmp cx, 2
    je .w
    jmp ex_d
.w:
    call ex_w
    xor dx, dx
    ret

; tf_get - BX = a slot, DX:AX = its default: DX:AX = its value when the IFD
; had it. Preserves all else
tf_get:
    call tf_has
    jz .x
    push bx
    shl bx, 1
    shl bx, 1
    mov ax, [cs:tf_v + bx]
    mov dx, [cs:tf_v + bx + 2]
    pop bx
.x:
    ret

; tf_w16 - BX = a slot whose value must fit a word, else CF = 1: AX = it
; (DX:AX's default 1). Preserves all but AX
tf_w16:
    push dx
    mov ax, 1
    xor dx, dx
    call tf_get
    or dx, dx
    pop dx
    jz .ok
    stc
    ret
.ok:
    clc
    ret

; --- 4: THE CHECKS, in pixelsim's order; then a palette's ColorMap ------------
tf_p4:
    mov bx, TS_W                    ; width and length: present...
    call tf_has
    jz .badh
    mov bx, TS_H
    call tf_has
    jz .badh
    mov ax, [cs:tf_v + 4 * TS_W]    ; ...and 1..8,192
    mov dx, [cs:tf_v + 4 * TS_W + 2]
    call ex_dim
    jc .dims
    mov [cs:tf_w], ax
    mov ax, [cs:tf_v + 4 * TS_H]
    mov dx, [cs:tf_v + 4 * TS_H + 2]
    call ex_dim
    jc .dims
    mov [cs:tf_h], ax
    cmp byte [cs:tf_tiled], 0       ; tiles
    jne .pack
    mov bx, TS_COMP                 ; compression 1, 5 or 32773
    call tf_w16
    jc .pack
    cmp ax, 1
    je .c
    cmp ax, 5
    je .c
    cmp ax, 32773
    jne .pack
.c:
    mov [cs:tf_comp], ax
    mov bx, TS_PHOT                 ; photometric present, 0..3
    call tf_has
    jz .badh
    call tf_w16
    jc .depth
    cmp ax, 3
    ja .depth
    mov [cs:tf_phot], al
    mov bx, TS_FILL                 ; FillOrder 1
    call tf_w16
    jc .pack
    cmp ax, 1
    jne .pack
    mov bx, TS_SOFF                 ; the two arrays present
    call tf_has
    jz .badh
    mov bx, TS_SCNT
    call tf_has
    jz .badh
    mov bx, TS_RPS                  ; RowsPerStrip: 0 bad, past h is h
    mov ax, [cs:tf_h]
    xor dx, dx
    call tf_get
    mov cx, ax
    or cx, dx
    jz .badh
    or dx, dx
    jnz .rh
    cmp ax, [cs:tf_h]
    jbe .r
.rh:
    mov ax, [cs:tf_h]
.r:
    mov [cs:tf_rps], ax
    mov bx, TS_BPS                  ; the bits and the samples (words, or a
    call tf_w16                     ; kind that is not read)
    jnc .b
    mov ax, 0xFFFF
.b:
    mov [cs:tf_bits], ax
    mov bx, TS_SPP
    call tf_w16
    jnc .sp
    mov ax, 0xFFFF
.sp:
    mov [cs:tf_spp], ax
    mov bx, TS_PLAN                 ; planar: 1, or 2 with one sample
    call tf_w16
    jc .pack
    cmp ax, 1
    je .pl
    cmp ax, 2
    jne .pack
    cmp word [cs:tf_spp], 1
    jne .pack
.pl:
    mov bx, TS_PRED                 ; predictor: 1, or 2 on 8-bit samples
    call tf_w16
    jc .pack
    mov [cs:tf_pred], al
    cmp ax, 1
    je .k
    cmp ax, 2
    jne .pack
    cmp word [cs:tf_bits], 8
    jne .pack
.k:
    mov ax, [cs:tf_bits]            ; THE KIND
    mov cx, [cs:tf_spp]
    cmp byte [cs:tf_phot], 2
    jne .gp
    cmp ax, 8                       ; RGB: 8 bits, 3 or 4 samples
    jne .depth
    cmp cx, 3
    je .kok
    cmp cx, 4
    je .kok
    jmp .depth
.gp:
    cmp cx, 1                       ; grey, palette: one sample of 1/2/4/8
    jne .depth
    cmp ax, 1
    je .kok
    cmp ax, 2
    je .kok
    cmp ax, 4
    je .kok
    cmp ax, 8
    jne .depth
.kok:
    mul cl                          ; the row: ceil(w x bits x samples / 8)
    mov [cs:tf_bps], al             ; (at most 8,192 x 32 bits: 32 KB)
    mul word [cs:tf_w]
    add ax, 7
    adc dx, 0
    mov cl, 3
    shr ax, cl
    mov cl, 13
    shl dx, cl
    or ax, dx
    mov [cs:tf_rb], ax
    cmp byte [cs:tf_phot], 3
    jne .strips
    ; --- PALETTE: the ColorMap present, SHORT, 3 x 2^bits of them -------------
    mov bx, TS_CMAP
    call tf_has
    jz .badh
    cmp byte [cs:tf_ty + TS_CMAP], 3
    jne .badh
    mov cl, [cs:tf_bits]
    mov ax, 1
    shl ax, cl
    mov [cs:tf_ne], ax
    mov dx, ax
    shl ax, 1
    add ax, dx                      ; 3 x 2^bits
    cmp word [cs:tf_cn + 4 * TS_CMAP + 2], 0
    jne .badh
    cmp [cs:tf_cn + 4 * TS_CMAP], ax
    jne .badh
    shl ax, 1                       ; its bytes, from ONE head
    mov cx, ax
    push cs
    pop es
    mov si, tf_rw + 4 * TS_CMAP
    call ex_d
    call ex_need                    ; ES:SI = R's values, then G's, then B's
    mov bx, px_spal                 ; each the high byte of its word
    mov cx, [cs:tf_ne]
    mov dx, cx
    shl dx, 1                       ; DX = a table's bytes
    mov al, [cs:ex_be]              ; (the high byte: the first in big-endian)
    xor al, 1
    cbw
    add si, ax
.cm:
    mov al, [es:si]
    mov [bx], al
    push si
    add si, dx
    mov al, [es:si]
    mov [bx + 1], al
    add si, dx
    mov al, [es:si]
    mov [bx + 2], al
    pop si
    add si, 2
    add bx, 3
    loop .cm
.strips:
    ; --- THE STRIPS: ceil(h / rps), at most 1,024; both arrays that long -----
    mov ax, [cs:tf_h]
    add ax, [cs:tf_rps]
    dec ax
    xor dx, dx
    div word [cs:tf_rps]
    cmp ax, EX_STRIPS
    ja .big
    mov [cs:tf_ns], ax
    cmp word [cs:tf_cn + 4 * TS_SOFF + 2], 0
    jne .c1
    cmp [cs:tf_cn + 4 * TS_SOFF], ax
    jb .badh
.c1:
    cmp word [cs:tf_cn + 4 * TS_SCNT + 2], 0
    jne .c2
    cmp [cs:tf_cn + 4 * TS_SCNT], ax
    jb .badh
.c2:
    mov word [cs:ex_rdone], 0
    mov byte [cs:ex_ph], 5
    jmp tf_p5
.badh:
    jmp ex_badh
.dims:
    mov ax, PXD_DIMS
    jmp ex_hfail
.pack:
    mov ax, PXD_PACK
    jmp ex_hfail
.depth:
    mov ax, PXD_DEPTH
    jmp ex_hfail
.big:
    mov ax, PXD_BIG
    jmp ex_hfail

; --- 5, 6: StripOffsets' first n values, then StripByteCounts': from the
; entry's own bytes when count x size <= 4, else a RUN through heads ---------
tf_p5:
    mov bx, TS_SOFF
    mov word [cs:ex_rdst], ex_table
    call tf_arr
    mov word [cs:ex_rdone], 0
    mov byte [cs:ex_ph], 6
    jmp short tf_p6
tf_p6:
    mov bx, TS_SCNT
    mov word [cs:ex_rdst], ex_table + EX_CNTS
    call tf_arr
    jmp tf_order

; tf_arr - BX = an array's slot, [ex_rdst] its place in the TABLE: its first
; [tf_ns] values there as dwords (the native order) - from the entry's own
; four bytes when count x size <= 4, else a RUN through heads. Clobbers all
; but DI
tf_arr:
    mov al, [cs:tf_ty + bx]
    mov [cs:tf_aty], al
    mov cx, [cs:tf_ns]              ; CX = the run's bytes: n x size
    shl cx, 1
    cmp al, 3
    je .s
    shl cx, 1
.s:
    shl bx, 1
    shl bx, 1                       ; BX = the slot's dword
    cmp word [cs:tf_cn + bx + 2], 0 ; count x size <= 4: count 1, or 2
    jne .run                        ; SHORTs
    mov ax, [cs:tf_cn + bx]
    cmp byte [cs:tf_aty], 3
    jne .l4
    cmp ax, 2
    ja .run
    jmp short .own
.l4:
    cmp ax, 1
    ja .run
.own:
    mov si, [cs:ex_rdst]            ; the four bytes, copied
    mov ax, [cs:tf_rw + bx]
    mov [cs:si], ax
    mov ax, [cs:tf_rw + bx + 2]
    mov [cs:si + 2], ax
    jmp short .conv
.run:
    push cx
    push cs
    pop es
    lea si, [bx + tf_rw]
    call ex_d                       ; DX:AX = where the values are
    pop cx
    call ex_run
.conv:
    push cs                         ; the values to dwords, in place -
    pop es                          ; backwards for SHORTs, which widen
    mov cx, [cs:tf_ns]
    cmp byte [cs:tf_aty], 3
    jne .lc
    mov si, cx                      ; SI = the last value's word...
    dec si
    shl si, 1
    mov bx, si
    shl bx, 1                       ; ...BX its dword
    add si, [cs:ex_rdst]
    add bx, [cs:ex_rdst]
.ws:
    call ex_w
    mov [cs:bx], ax
    mov word [cs:bx + 2], 0
    sub si, 2
    sub bx, 4
    loop .ws
    ret
.lc:
    mov si, [cs:ex_rdst]
.wl:
    call ex_d
    mov [cs:si], ax
    mov [cs:si + 2], dx
    add si, 4
    loop .wl
    ret

; tf_order - each strip at or past the end of the one before it (unbounded:
; a sum that carries is past any offset); then the context's answers
tf_order:
    mov cx, [cs:tf_ns]
    dec cx
    jz .ok
    mov si, ex_table
.o:
    mov ax, [cs:si]                 ; the end of strip i - 1...
    mov dx, [cs:si + 2]
    add ax, [cs:si + EX_CNTS]
    adc dx, [cs:si + EX_CNTS + 2]
    jc .pack
    cmp dx, [cs:si + 6]             ; ...against where strip i starts
    ja .pack
    jb .n
    cmp ax, [cs:si + 4]
    ja .pack
.n:
    add si, 4
    loop .o
.ok:
    ; --- THE ANSWERS ------------------------------------------------------------
    mov cl, RF_GREY
    xor si, si                      ; SI = the palette's entries
    mov al, [cs:tf_phot]
    cmp al, 2
    jb .rf
    mov cl, RF_RGB
    je .rf
    mov cl, RF_IDX
    mov si, [cs:tf_ne]
.rf:
    mov dl, PK_NONE                 ; DL = the packing
    mov bp, [cs:tf_rb]              ; BP = the scratch: the raw row...
    add bp, 15
    shr bp, 1
    shr bp, 1
    shr bp, 1
    shr bp, 1
    mov word [cs:tf_raw], 0
    mov ax, [cs:tf_comp]
    cmp ax, 1
    je .pk
    mov dl, PK_RLE
    cmp ax, 5
    jne .pk
    mov dl, PK_LZW                  ; ...after the LZW's tables
    add bp, LZW_KB * 64
    mov word [cs:tf_raw], LZW_KB * 1024
.pk:
    mov ch, [cs:tf_bps]
    mov ax, [cs:tf_w]
    mov bx, [cs:tf_h]
    xor dh, dh
    call ex_ans
    mov byte [cs:tf_alpha], 0       ; RGBA: the fourth sample is alpha when
    cmp byte [cs:tf_spp], 4         ; ExtraSamples says 1 or 2
    jne .x
    mov bx, TS_XS
    xor ax, ax
    xor dx, dx
    call tf_get
    or dx, dx
    jnz .x
    dec ax
    cmp ax, 1
    ja .x
    mov byte [cs:tf_alpha], 1
.x:
    jmp ex_hok
.pack:
    mov ax, PXD_PACK
    jmp ex_hfail

; =============================================================================
; ICO and CUR (SPEC.md 106.25)
; =============================================================================
ic_head:
    mov byte [cs:ex_be], 0
    mov bl, [cs:ex_ph]
    xor bh, bh
    shl bx, 1
    jmp [cs:ic_phase + bx]
ic_phase:   dw ic_p0, ic_p1, ic_p2, ic_p3, ic_p4

; --- 0: the directory, in the first head; the image picked ----------------------
ic_p0:
    mov es, [di + PXK_HSEG]
    cmp word [di + PXK_HLEN], 6
    jb .bad
    cmp word [es:0], 0              ; reserved 0, type 1 or 2, 1..127 images
    jne .bad
    mov ax, [es:2]
    mov [cs:ic_type], al
    dec ax
    cmp ax, 1
    ja .bad
    mov cx, [es:4]
    dec cx
    cmp cx, 126
    ja .bad
    inc cx
    mov ax, 16                      ; the directory's end: 6 + 16 n
    mul cx
    add ax, 6
    mov [cs:ic_dsz], ax
    cmp word [di + PXK_FSZ + 2], 0  ; the file must hold it
    jne .h
    cmp ax, [di + PXK_FSZ]
    ja .short
.h:
    mov si, 6                       ; THE IMAGE: the largest w x h, then the
    xor bx, bx                      ; larger bits (a CUR's are 0), the first
    mov word [cs:ic_kwh], 0         ; on a tie. BX = the best's entry
    mov word [cs:ic_kwh + 2], 0
    mov word [cs:ic_kb], 0
    mov byte [cs:ic_first], 1
.e:
    mov al, [es:si]                 ; w x h, a byte of 0 being 256
    xor ah, ah
    or al, al
    jnz .w
    inc ah
.w:
    push ax
    mov al, [es:si + 1]
    xor ah, ah
    or al, al
    jnz .hh
    inc ah
.hh:
    pop dx
    mul dx                          ; DX:AX = w x h
    push bx
    xor bx, bx                      ; BX = its bits (a CUR: 0)
    cmp byte [cs:ic_type], 1
    jne .kb
    mov bx, [es:si + 6]
.kb:
    cmp byte [cs:ic_first], 0
    jne .take
    cmp dx, [cs:ic_kwh + 2]         ; strictly greater (w x h, bits)
    jb .no
    ja .take
    cmp ax, [cs:ic_kwh]
    jb .no
    ja .take
    cmp bx, [cs:ic_kb]
    jbe .no
.take:
    mov [cs:ic_kwh], ax
    mov [cs:ic_kwh + 2], dx
    mov [cs:ic_kb], bx
    mov byte [cs:ic_first], 0
    pop bx
    mov bx, si                      ; (the best entry's place)
    jmp short .nx
.no:
    pop bx
.nx:
    add si, 16
    loop .e
    mov ax, [es:bx + 12]            ; its offset: past the directory...
    mov dx, [es:bx + 14]
    or dx, dx
    jnz .of
    cmp ax, [cs:ic_dsz]
    jb .bad
.of:
    mov [cs:ic_off], ax
    mov [cs:ic_off + 2], dx
    cmp dx, [di + PXK_FSZ + 2]      ; ...and inside the file
    jb .in
    ja .short
    cmp ax, [di + PXK_FSZ]
    jae .short
.in:
    mov byte [cs:ex_ph], 1
    jmp short ic_p1
.bad:
    jmp ex_badh
.short:
    jmp ex_short

; --- 1: its first four bytes: a PNG's signature, or a DIB header's size --------
ic_p1:
    mov ax, [cs:ic_off]
    mov dx, [cs:ic_off + 2]
    mov cx, 4
    call ex_need
    cmp word [es:si], 0x5089        ; 89 'PNG'
    jne .dib
    cmp word [es:si + 2], 0x474E
    jne .dib
    jmp ic_png
.dib:
    call ex_d                       ; the header's size: 40, 108 or 124
    or dx, dx
    jnz .bad
    cmp ax, 40
    je .ok
    cmp ax, 108
    je .ok
    cmp ax, 124
    jne .bad
.ok:
    mov [cs:ic_hsz], ax
    mov byte [cs:ex_ph], 2
    jmp short ic_p2
.bad:
    jmp ex_badh

; --- 2: the DIB's header, whole (cut short before any field's check) -----------
ic_p2:
    mov ax, [cs:ic_off]
    mov dx, [cs:ic_off + 2]
    mov cx, [cs:ic_hsz]
    call ex_need
    mov ax, [es:si + 4]             ; width 1..8,192 (signed)
    mov dx, [es:si + 6]
    call ex_dim
    jc .dims
    mov [cs:ic_w], ax
    mov ax, [es:si + 8]             ; height: not negative, even...
    mov dx, [es:si + 10]
    test dh, 0x80
    jnz .bad
    test al, 1
    jnz .bad
    shr dx, 1                       ; ...its half 1..8,192
    rcr ax, 1
    call ex_dim
    jc .dims
    mov [cs:ic_hh], ax
    cmp word [es:si + 12], 1        ; planes 0 or 1
    ja .bad
    mov ax, [es:si + 14]            ; bits 1, 4, 8, 24, 32
    mov [cs:ic_bits], al
    cmp ax, 1
    je .bk
    cmp ax, 4
    je .bk
    cmp ax, 8
    je .bk
    cmp ax, 24
    je .bk
    cmp ax, 32
    jne .depth
.bk:
    mov ax, [es:si + 16]            ; compression 0
    or ax, [es:si + 18]
    jnz .pack
    mov word [cs:ic_ne], 0
    cmp byte [cs:ic_bits], 8        ; a palette: ClrUsed, or 2^bits; no more
    ja .np
    mov cl, [cs:ic_bits]
    mov bx, 1
    shl bx, cl
    mov ax, [es:si + 32]
    mov dx, [es:si + 34]
    mov cx, ax
    or cx, dx
    jz .all
    or dx, dx
    jnz .bad
    cmp ax, bx
    ja .bad
    mov bx, ax
.all:
    mov [cs:ic_ne], bx
.np:
    mov byte [cs:ex_ph], 3
    jmp short ic_p3
.bad:
    jmp ex_badh
.dims:
    mov ax, PXD_DIMS
    jmp ex_hfail
.depth:
    mov ax, PXD_DEPTH
    jmp ex_hfail
.pack:
    mov ax, PXD_PACK
    jmp ex_hfail

; --- 3: header and palette from one head; the palette into [px_spal]; then
; the AND mask's size --------------------------------------------------------
ic_p3:
    mov cx, [cs:ic_ne]
    shl cx, 1
    shl cx, 1
    add cx, [cs:ic_hsz]
    mov ax, [cs:ic_off]
    mov dx, [cs:ic_off + 2]
    call ex_need
    add si, [cs:ic_hsz]
    mov bx, px_spal                 ; BGRx quads, to R, G, B
    mov cx, [cs:ic_ne]
    jcxz .pd
.q:
    mov al, [es:si + 2]
    mov [bx], al
    mov al, [es:si + 1]
    mov [bx + 1], al
    mov al, [es:si]
    mov [bx + 2], al
    add si, 4
    add bx, 3
    loop .q
.pd:
    mov ax, [cs:ic_ne]              ; fewer than 256: the view's background
    or ax, ax                       ; is entry n (a masked pixel's)
    jz .xo
    cmp ax, 256
    jae .xo
    mov byte [bx], PXP_BG
    mov byte [bx + 1], PXP_BG
    mov byte [bx + 2], PXP_BG
.xo:
    mov ax, [cs:ic_ne]              ; the XOR rows' place: off + hsz + 4 n
    shl ax, 1
    shl ax, 1
    add ax, [cs:ic_hsz]
    xor dx, dx
    add ax, [cs:ic_off]
    adc dx, [cs:ic_off + 2]
    mov [cs:ic_xo], ax
    mov [cs:ic_xo + 2], dx
    mov al, [cs:ic_bits]            ; a row's bytes: ((w x bits + 31) / 32) x 4
    xor ah, ah
    mul word [cs:ic_w]
    add ax, 31
    adc dx, 0
    mov cl, 5
    shr ax, cl
    mov cl, 11
    shl dx, cl
    or ax, dx
    shl ax, 1
    shl ax, 1
    mov [cs:ic_stride], ax
    mov ax, [cs:ic_w]               ; ...the mask's: ((w + 31) / 32) x 4
    add ax, 31
    mov cl, 5
    shr ax, cl
    shl ax, 1
    shl ax, 1
    mov [cs:ic_ms], ax
    cmp byte [cs:ic_bits], 32       ; 32 bits: the alpha is the mask
    je ic_done
    mul word [cs:ic_hh]             ; the mask's bytes: 8,192 at most
    or dx, dx
    jnz .big
    cmp ax, EX_TABLE
    ja .big
    mov [cs:ic_msz], ax
    mov word [cs:ex_rdone], 0
    mov byte [cs:ex_ph], 4
    jmp short ic_p4
.big:
    mov ax, PXD_BIG
    jmp ex_hfail

; --- 4: the AND mask, a run through heads into the TABLE -----------------------
ic_p4:
    mov ax, [cs:ic_stride]          ; it follows the XOR rows
    mul word [cs:ic_hh]
    add ax, [cs:ic_xo]
    adc dx, [cs:ic_xo + 2]
    mov cx, [cs:ic_msz]
    mov word [cs:ex_rdst], ex_table
    call ex_run
; ic_done - the answers
ic_done:
    mov cl, RF_RGB                  ; RGB, but for a palette of fewer than 256
    xor si, si
    mov ax, [cs:ic_ne]
    or ax, ax
    jz .rf
    cmp ax, 256
    jae .rf
    mov cl, RF_IDX
    mov si, ax
    inc si
.rf:
    mov ch, [cs:ic_bits]
    mov dl, PK_NONE
    mov dh, PXKF_UP                 ; bottom-up
    mov bp, [cs:ic_stride]          ; the scratch: a raw row
    add bp, 15
    shr bp, 1
    shr bp, 1
    shr bp, 1
    shr bp, 1
    mov ax, [cs:ic_w]
    mov bx, [cs:ic_hh]
    call ex_ans
    jmp ex_hok

; --- PNG-in-ICO: the PNG part reads it, from its own first byte ---------------
ic_png:
    cmp byte [px_rflat], 0          ; a file in memory has no base to move
    je .disk
    mov ax, PXD_PACK
    jmp ex_hfail
.disk:
    mov ax, [di + PXK_FSZ]          ; its first min(33, what is left) bytes in
    mov dx, [di + PXK_FSZ + 2]      ; one head: its HEAD reads 33
    sub ax, [cs:ic_off]
    sbb dx, [cs:ic_off + 2]
    mov cx, 33
    or dx, dx
    jnz .n
    cmp ax, cx
    jae .n
    mov cx, ax
.n:
    mov ax, [cs:ic_off]
    mov dx, [cs:ic_off + 2]
    call ex_need                    ; ES:SI = the PNG's first byte
    mov cx, [cs:ex_hhi]             ; ...and the head from there, moved down
    sub cx, ax                      ; to its start
    mov [di + PXK_HLEN], cx
    mov word [di + PXK_HPOS], 0
    mov [di + PXK_HBASE], ax
    mov [di + PXK_HBASE + 2], dx
    mov [px_sbase], ax              ; THE STREAM'S BASE: the ring reads from
    mov [px_sbase + 2], dx          ; here (106.25)
    push ds
    push di
    push es
    pop ds
    xor di, di
    cld
    rep movsb
    pop di
    pop ds
    mov ax, PXD_REDIR
    jmp ex_hfail

; =============================================================================
; IFF: ILBM and PBM (SPEC.md 106.25) - big-endian, a chunk at a time to BODY
; =============================================================================
lb_head:
    mov byte [cs:ex_be], 1
    mov bl, [cs:ex_ph]
    xor bh, bh
    shl bx, 1
    jmp [cs:lb_phase + bx]
lb_phase:   dw lb_p0, lb_p1, lb_p2, lb_p3, lb_p4

; --- 0: 'FORM', a size, 'ILBM' or 'PBM ' -------------------------------------
lb_p0:
    mov es, [di + PXK_HSEG]
    cmp word [di + PXK_HLEN], 12
    jb .bad
    cmp word [es:0], 'FO'
    jne .bad
    cmp word [es:2], 'RM'
    jne .bad
    mov byte [cs:lb_pbm], 0
    cmp word [es:8], 'IL'
    jne .p
    cmp word [es:10], 'BM'
    je .ok
    jmp short .bad
.p:
    cmp word [es:8], 'PB'
    jne .bad
    cmp word [es:10], 'M '
    jne .bad
    mov byte [cs:lb_pbm], 1
.ok:
    xor ax, ax
    mov [cs:lb_bm], al
    mov [cs:lb_cmap], al
    mov [cs:lb_camg], ax
    mov word [cs:lb_pos], 12
    mov [cs:lb_pos + 2], ax
    mov byte [cs:ex_ph], 1
    jmp short lb_p1
.bad:
    jmp ex_badh

; --- 1: a chunk's header ------------------------------------------------------
lb_p1:
    mov ax, [cs:lb_pos]
    mov dx, [cs:lb_pos + 2]
    mov cx, 8
    call ex_need
    add ax, 8                       ; its data
    adc dx, 0
    mov [cs:lb_d], ax
    mov [cs:lb_d + 2], dx
    push si
    add si, 4
    call ex_d
    mov [cs:lb_sz], ax
    mov [cs:lb_sz + 2], dx
    pop si
    mov ax, [es:si]                 ; its id
    mov dx, [es:si + 2]
    cmp ax, 'BM'
    jne .c
    cmp dx, 'HD'
    jne .nx
    cmp word [cs:lb_sz + 2], 0      ; BMHD: 20 bytes or more
    jne .bm
    cmp word [cs:lb_sz], 20
    jb .bad
.bm:
    mov byte [cs:ex_ph], 2
    jmp lb_p2
.c:
    cmp ax, 'CM'
    jne .g
    cmp dx, 'AP'
    jne .nx
    mov byte [cs:lb_cmap], 1        ; CMAP: min(size / 3, 256) entries
    mov ax, [cs:lb_sz]
    mov dx, [cs:lb_sz + 2]
    mov cx, 3
    cmp dx, cx
    jae .c256
    div cx
    cmp ax, 256
    jbe .ck
.c256:
    mov ax, 256
.ck:
    mov [cs:lb_k], ax
    or ax, ax
    jz .c0
    mov byte [cs:ex_ph], 3
    jmp lb_p3
.c0:
    call lb_pal0                    ; (a CMAP of no entries: all black)
    jmp short .nx
.g:
    cmp ax, 'CA'
    jne .b
    cmp dx, 'MG'
    jne .nx
    cmp word [cs:lb_sz + 2], 0      ; CAMG: read when 4 bytes or more
    jne .cg
    cmp word [cs:lb_sz], 4
    jb .nx
.cg:
    mov byte [cs:ex_ph], 4
    jmp lb_p4
.b:
    cmp ax, 'BO'
    jne .nx
    cmp dx, 'DY'
    jne .nx
    cmp byte [cs:lb_bm], 0          ; BODY: the walk's end - after a BMHD
    je .bad
    jmp lb_end
.nx:
    jmp lb_next
.bad:
    jmp ex_badh

; lb_next - on to the chunk after this one: data + size + (size & 1),
; unbounded (a carry is past any file: `cut short` at the next header)
lb_next:
    mov ax, [cs:lb_d]
    mov dx, [cs:lb_d + 2]
    add ax, [cs:lb_sz]
    adc dx, [cs:lb_sz + 2]
    jc .far
    test byte [cs:lb_sz], 1
    jz .p
    add ax, 1
    adc dx, 0
    jnc .p
.far:
    mov ax, 0xFFFF
    mov dx, ax
.p:
    mov [cs:lb_pos], ax
    mov [cs:lb_pos + 2], dx
    mov byte [cs:ex_ph], 1
    jmp lb_p1

; lb_pal0 - [px_spal] all black. Preserves all
lb_pal0:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, px_spal
    mov cx, 768 / 2
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; --- 2: BMHD's 20 bytes: the size checked there and then ----------------------
lb_p2:
    mov ax, [cs:lb_d]
    mov dx, [cs:lb_d + 2]
    mov cx, 20
    call ex_need
    call ex_w                       ; width
    xor dx, dx
    call ex_dim
    jc .dims
    mov [cs:lb_w], ax
    add si, 2
    call ex_w                       ; height
    call ex_dim
    jc .dims
    mov [cs:lb_h], ax
    mov al, [es:si + 6]             ; planes, masking, compression
    mov [cs:lb_planes], al
    mov al, [es:si + 7]
    mov [cs:lb_mask], al
    mov al, [es:si + 8]
    mov [cs:lb_comp], al
    mov byte [cs:lb_bm], 1
    jmp lb_next
.dims:
    mov ax, PXD_DIMS
    jmp ex_hfail

; --- 3: CMAP's entries into [px_spal], black past them -------------------------
lb_p3:
    mov cx, [cs:lb_k]
    mov ax, cx
    shl cx, 1
    add cx, ax
    mov ax, [cs:lb_d]
    mov dx, [cs:lb_d + 2]
    call ex_need
    call lb_pal0
    push ds
    push di
    push ds
    push es
    pop ds
    pop es
    mov di, px_spal
    cld
    rep movsb
    pop di
    pop ds
    jmp lb_next

; --- 4: CAMG's mode --------------------------------------------------------------
lb_p4:
    mov ax, [cs:lb_d]
    mov dx, [cs:lb_d + 2]
    mov cx, 4
    call ex_need
    add si, 2
    call ex_w                       ; (its low word holds EHB and HAM)
    mov [cs:lb_camg], ax
    jmp lb_next

; lb_end - BODY: the checks, the palette, the answers
lb_end:
    mov ax, [cs:lb_d]
    mov [cs:lb_body], ax
    mov ax, [cs:lb_d + 2]
    mov [cs:lb_body + 2], ax
    mov ax, [cs:lb_sz]
    mov [cs:lb_bsize], ax
    mov ax, [cs:lb_sz + 2]
    mov [cs:lb_bsize + 2], ax
    cmp byte [cs:lb_comp], 1        ; compression 0 or 1
    ja .pack
    mov al, [cs:lb_planes]
    cmp byte [cs:lb_pbm], 0
    je .il
    cmp al, 8                       ; PBM: 8 planes
    jne .depth
    jmp short .pal
.il:
    or al, al                       ; ILBM: 1..8 planes, and not HAM
    jz .depth
    cmp al, 8
    ja .depth
    test word [cs:lb_camg], 0x800
    jnz .depth
.pal:
    mov cl, al                      ; THE PALETTE: 2^planes (PBM 256)
    mov ax, 1
    shl ax, cl
    mov [cs:lb_ne], ax
    cmp byte [cs:lb_cmap], 0
    jne .cut
    mov cx, ax                      ; no CMAP: greys, i x 255 / (n - 1)
    dec cx                          ; CX = n - 1
    mov bx, px_spal
    xor si, si                      ; SI = i
.gr:
    mov ax, 255
    mul si
    div cx
    mov [bx], al
    mov [bx + 1], al
    mov [bx + 2], al
    add bx, 3
    inc si
    cmp si, [cs:lb_ne]
    jb .gr
    jmp short .ehb
.cut:
    mov ax, [cs:lb_ne]              ; a CMAP longer than the palette: the rest
    mov cx, ax                      ; black (the palette is n entries)
    shl ax, 1
    add ax, cx
    mov cx, 768
    sub cx, ax
    jbe .ehb
    push es
    push di
    push ds
    pop es
    mov di, px_spal
    add di, ax
    xor al, al
    cld
    rep stosb
    pop di
    pop es
.ehb:
    cmp byte [cs:lb_pbm], 0         ; EHB: 6 planes, entries 32..63 those
    jne .ans                        ; of 0..31 halved
    cmp byte [cs:lb_planes], 6
    jne .ans
    test byte [cs:lb_camg], 0x80
    jz .ans
    mov bx, px_spal
    mov cx, 32 * 3
.h:
    mov al, [bx]
    shr al, 1
    mov [bx + 32 * 3], al
    inc bx
    loop .h
.ans:
    mov ax, [cs:lb_w]               ; a row's bytes: PBM w rounded to even;
    cmp byte [cs:lb_pbm], 0         ; ILBM (planes + the mask plane) x
    je .ir                          ; ((w + 15) >> 4) x 2
    inc ax
    and al, 0xFE
    jmp short .rl
.ir:
    add ax, 15
    mov cl, 4
    shr ax, cl
    shl ax, 1
    mov [cs:lb_pb], ax
    mov cl, [cs:lb_planes]
    cmp byte [cs:lb_mask], 1
    jne .nm
    inc cl
.nm:
    xor ch, ch
    mul cx
.rl:
    mov [cs:lb_rl], ax
    add ax, 15
    mov cl, 4
    shr ax, cl
    mov bp, ax                      ; the scratch: a raw row
    mov cl, RF_IDX
    mov ch, [cs:lb_planes]
    mov dl, PK_NONE
    cmp byte [cs:lb_comp], 0
    je .pk
    mov dl, PK_RLE
.pk:
    xor dh, dh
    mov si, [cs:lb_ne]
    mov ax, [cs:lb_w]
    mov bx, [cs:lb_h]
    call ex_ans
    jmp ex_hok
.pack:
    mov ax, PXD_PACK
    jmp ex_hfail
.depth:
    mov ax, PXD_DEPTH
    jmp ex_hfail

; =============================================================================
; MacPaint (SPEC.md 106.25): 576 x 720, 1 bit, behind 512 bytes of header -
; and, when it carries 'PNTG' at 65, the 128 of a MacBinary one
; =============================================================================
mc_head:
    mov es, [di + PXK_HSEG]
    xor ax, ax                      ; the data's start
    cmp word [di + PXK_FSZ + 2], 0
    jne .mb
    cmp word [di + PXK_FSZ], 69
    jb .st
.mb:
    cmp byte [es:0], 0
    jne .st
    cmp word [es:65], 'PN'
    jne .st
    cmp word [es:67], 'TG'
    jne .st
    mov ax, 128
.st:
    add ax, 512                     ; the file holds the header at least
    mov [cs:mc_off], ax
    cmp word [di + PXK_FSZ + 2], 0
    jne .ok
    cmp ax, [di + PXK_FSZ]
    jbe .ok
    jmp ex_short
.ok:
    mov word [px_spal], 0xFFFF      ; white, black
    mov byte [px_spal + 2], 0xFF
    mov ax, 576
    mov bx, 720
    mov cx, RF_IDX | (1 << 8)
    mov dx, PK_RLE
    mov si, 2
    mov bp, 72 / 16 + 1
    call ex_ans
    jmp ex_hok

; =============================================================================
; PXV_DECODE - the worker: DS = the package, DI = the context. The stream
; read forward from the ring's windows ([px_rpos] .. [px_rend] in
; [px_rsegc], K_RING for the next), a row at a time into the row buffer
; ([PXK_RSEG]:0), each to K_EMIT
; =============================================================================
ex_decode:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    push ds
    mov [cs:ex_dsp], sp
    cmp word [8], PXL_IMAGE
    jne .link
    cmp word [10], PXL_BSS
    jne .link
    mov ax, [di + PXK_SVC + 2 * PXS_EMIT]   ; the two services, far
    mov [cs:ex_emitf], ax
    mov [cs:ex_emitf + 2], ds
    mov ax, [di + PXK_SVC + 2 * PXS_RING]
    mov [cs:ex_ringf], ax
    mov [cs:ex_ringf + 2], ds
    mov ax, [di + PXK_RSEG]
    mov [cs:ex_rseg], ax
    mov ax, [di + PXK_DSEG]
    mov [cs:ex_dseg], ax
    xor ax, ax
    mov [cs:ex_lim], al
    mov [cs:ex_rmode], al
    mov [cs:ex_y], ax
    cld
    mov al, [px_cur + PXR_FMT]
    cmp al, PXF_TIFF
    je .t
    cmp al, PXF_ICO
    je .i
    cmp al, PXF_LBM
    je .l
    cmp al, PXF_MAC
    je .m
    mov ax, PXD_DATA
    jmp short ex_dfail
.t:
    call tf_dec
    jmp short .ok
.i:
    call ic_dec
    jmp short .ok
.l:
    call lb_dec
    jmp short .ok
.m:
    call mc_dec
    jmp short .ok
.link:
    mov ax, PXD_LINK
    jmp short ex_dfail
.ok:
    xor ax, ax
    clc
ex_dret:
    pop ds
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; ex_dfail - AX = a PXD_*: out of DECODE from any depth
ex_dfail:
    ; STKBALANCE-OK: DECODE's way out from any depth - SP back to its frame
    ; from [ex_dsp], what was pushed after abandoned; ex_dret's pops are
    ; the frame's own (DS the package again)
    mov sp, [cs:ex_dsp]
    stc
    jmp short ex_dret

; --- THE STREAM --------------------------------------------------------------------

; ex_rnext - the next ring window (K_RING). CF = 1 at the end. Preserves all
ex_rnext:
    call far [cs:ex_ringf]
    ret

; ex_rskip - on to absolute offset DX:AX (forward only: one behind costs
; nothing); the file ending first is `cut short`. DS = the package.
; Preserves all
ex_rskip:
    push ax
    push bx
    push cx
    push dx
.l:
    mov bx, ax                      ; DX:BX = the target - the window's start
    mov cx, dx
    sub bx, [px_rbase]
    sbb cx, [px_rbase + 2]
    jb .ok
    or cx, cx
    jnz .past
    cmp bx, [px_rend]
    ja .past
    cmp bx, [px_rpos]
    jb .ok
    mov [px_rpos], bx
.ok:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.past:
    push ax
    mov ax, [px_rend]
    mov [px_rpos], ax
    pop ax
    call ex_rnext
    jnc .l
    jmp ex_short_d

; ex_setlim - DX:AX = the bytes the stream may give from here (a strip's, a
; BODY's): past them is `cut short`. Preserves all
ex_setlim:
    mov [cs:ex_left], ax
    mov [cs:ex_left + 2], dx
    mov byte [cs:ex_lim], 1
    ret

; ex_rb - AL = the next byte, inside the bound; its end, or the file's, is
; `cut short`. DS = the package. Preserves all but AL
ex_rb:
    cmp byte [cs:ex_lim], 0
    je .go
    sub word [cs:ex_left], 1
    sbb word [cs:ex_left + 2], 0
    jc .sh
.go:
    push si
.again:
    mov si, [px_rpos]
    cmp si, [px_rend]
    jae .next
    push ds
    mov ds, [px_rsegc]
    lodsb
    pop ds
    mov [px_rpos], si
    pop si
    ret
.next:
    call ex_rnext
    jnc .again
    pop si
.sh:
    jmp ex_short_d
ex_short_d:
    ; STKBALANCE-OK: `cut short` from any depth of DECODE - ex_dfail puts SP
    ; back to the frame from [ex_dsp]
    mov ax, PXD_TRUNC
    jmp ex_dfail

; ex_rdn - CX bytes into ES:DI (DI advanced), inside the bound. DS = the
; package. Preserves all but DI
ex_rdn:
    cmp byte [cs:ex_lim], 0
    je .go
    sub [cs:ex_left], cx
    sbb word [cs:ex_left + 2], 0
    jc ex_short_d
.go:
    push ax
    push cx
    push si
.l:
    jcxz .x
    mov ax, [px_rend]
    sub ax, [px_rpos]
    jnz .have
    call ex_rnext
    jnc .l
    pop si
    pop cx
    pop ax
    jmp ex_short_d
.have:
    cmp ax, cx
    jbe .n
    mov ax, cx
.n:
    sub cx, ax
    push cx
    mov cx, ax
    mov si, [px_rpos]
    add [px_rpos], ax
    push ds
    mov ds, [px_rsegc]
    rep movsb
    pop ds
    pop cx
    jmp short .l
.x:
    pop si
    pop cx
    pop ax
    ret

; ex_rle - CX bytes into ES:DI (DI advanced) from the RUN-LENGTH stream
; (PackBits, ByteRun1: 0..127 n + 1 literal bytes, 129..255 the next byte
; 257 - n times, 128 nothing), its state kept across calls ([ex_rmode]) so a
; run may cross a row; nothing is read once CX are made - a literal's rest
; or a repeat's stays unread (SPEC.md 106.25). Preserves all but DI
ex_rle:
    push ax
    push bx
    push cx
.l:
    jcxz .x
    cmp byte [cs:ex_rmode], 0
    jne .have
    call ex_rb                      ; a control byte
    cmp al, 128
    je .l                           ; (nothing)
    jb .lit
    mov bl, al                      ; a repeat: 257 - n of the next byte
    mov ax, 257
    sub al, bl
    sbb ah, 0
    mov [cs:ex_rcnt], ax
    call ex_rb
    mov [cs:ex_rval], al
    mov byte [cs:ex_rmode], 2
    jmp short .have
.lit:
    inc al                          ; a literal: n + 1 bytes
    xor ah, ah
    mov [cs:ex_rcnt], ax
    mov byte [cs:ex_rmode], 1
.have:
    mov bx, [cs:ex_rcnt]            ; BX = this piece: min(the run, CX)
    cmp bx, cx
    jbe .k
    mov bx, cx
.k:
    sub cx, bx
    sub [cs:ex_rcnt], bx
    push cx
    mov cx, bx
    cmp byte [cs:ex_rmode], 1
    je .rd
    mov al, [cs:ex_rval]            ; a repeat's byte
    rep stosb
    jmp short .z
.rd:
    call ex_rdn                     ; a literal's bytes
.z:
    pop cx
    cmp word [cs:ex_rcnt], 0        ; the run spent: the next control
    jne .l
    mov byte [cs:ex_rmode], 0
    jmp short .l
.x:
    pop cx
    pop bx
    pop ax
    ret

; ex_emit - the row buffer is source row [ex_y]: K_EMIT (the emitter counts
; the rows complete and the progress itself); [ex_y] on. A cancel leaves the
; decode. Preserves all
ex_emit:
    push ax
    push bx
    push dx
    mov ax, [cs:ex_y]
    mov bx, 0xFFFF
    mov dx, bx
    call far [cs:ex_emitf]
    jc ex_dfail
    inc word [cs:ex_y]
    pop dx
    pop bx
    pop ax
    ret

; ex_unpk - CX values of BL bits (1, 2, 4 or 8) from DS:SI, MSB first, into
; ES:DI, a byte each. Clobbers AX, CX, DX, SI, DI
ex_unpk:
    cmp bl, 8
    jne .bits
    rep movsb
    ret
.bits:
    mov dh, 1                       ; DH = the value's mask
    push cx
    mov cl, bl
    shl dh, cl
    dec dh
    pop cx
.byte:
    lodsb
    mov ah, al
    mov dl, 8                       ; DL = the bits left in it
.px:
    push cx
    mov cl, bl
    rol ah, cl                      ; the next value at the bottom
    pop cx
    mov al, ah
    and al, dh
    stosb
    dec cx
    jz .x
    sub dl, bl
    jnz .px
    jmp short .byte
.x:
    ret

; ex_planar - BL planes of BP bytes each at DS:0, the plane p row at p x BP,
; into CX indices at ES:0: bit 7 - (x & 7) of plane p's byte x >> 3, as
; bit p. Clobbers AX, BX, CX, DX, SI, DI
ex_planar:
    push cx
    xor di, di                      ; the indices 0...
    xor al, al
    rep stosb
    pop cx
    mov bh, bl                      ; ...then plane by plane from the top one:
.p:                                 ; an index is twice itself and the bit
    dec bh
    mov al, bh
    xor ah, ah
    mul bp
    mov si, ax
    xor di, di
    push cx
.byte:
    lodsb
    mov ah, al
    mov dl, 8
.bit:
    shl ah, 1
    rcl byte [es:di], 1
    inc di
    dec cx
    jz .pd
    dec dl
    jnz .bit
    jmp short .byte
.pd:
    pop cx
    or bh, bh
    jnz .p
    ret

; ex_blend - AL = a channel, AH = its alpha: AL = it over the view's
; background, PNG's arithmetic (106.18): (t + (t >> 8)) >> 8, t = a c +
; (255 - a) 85 + 128. Preserves all but AX
ex_blend:
    push bx
    push dx
    mov bl, ah
    mul ah                          ; a x c
    mov dx, ax
    mov al, 255
    sub al, bl
    mov ah, PXP_BG
    mul ah                          ; (255 - a) x 85
    add ax, dx
    add ax, 128
    mov dl, ah
    xor dh, dh
    add ax, dx
    mov al, ah
    pop dx
    pop bx
    ret

; =============================================================================
; TIFF's DECODE: the strips in order, a row at a time
; =============================================================================
tf_dec:
    mov word [cs:tf_si], 0
.strip:
    mov bx, [cs:tf_si]
    cmp bx, [cs:tf_ns]
    jb .s
    ret
.s:
    shl bx, 1
    shl bx, 1
    mov ax, [cs:ex_table + bx]      ; to its offset...
    mov dx, [cs:ex_table + bx + 2]
    call ex_rskip
    mov ax, [cs:ex_table + EX_CNTS + bx]    ; ...and its bytes, no more
    mov dx, [cs:ex_table + EX_CNTS + bx + 2]
    call ex_setlim
    mov ax, [cs:tf_h]               ; its rows: rps, the last strip the rest
    sub ax, [cs:ex_y]
    cmp ax, [cs:tf_rps]
    jbe .r
    mov ax, [cs:tf_rps]
.r:
    mov [cs:tf_left], ax
    mov byte [cs:ex_rmode], 0       ; (a PackBits stream is a strip's)
    cmp word [cs:tf_comp], 5
    je .lzw
.row:
    mov es, [cs:ex_dseg]            ; NONE and PACKBITS: a row's bytes...
    mov di, [cs:tf_raw]
    mov cx, [cs:tf_rb]
    cmp word [cs:tf_comp], 1
    je .none
    call ex_rle
    jmp short .got
.none:
    call ex_rdn
.got:
    call tf_rowdone                 ; ...made into pixels, and emitted
    cmp word [cs:tf_left], 0
    jne .row
    jmp short .nx
.lzw:
    call tf_lzw
.nx:
    inc word [cs:tf_si]
    jmp .strip

; tf_lzw - the strip's rows from ONE LZW stream: its strings fill them in
; order (tf_lrun), and it is done when the last is whole; the End code or
; the strip's bytes before that are `cut short`
tf_lzw:
    mov word [cs:tf_rpos], 0
    push ds
    push cs
    pop ds                          ; (the LZW's state is this part's)
    mov es, [ex_dseg]
    mov al, 8
    call lzw_decode
    pop ds
    jnc .short
    cmp ax, EX_DONE
    je .x
    jmp ex_dfail
.short:
    jmp ex_short_d
.x:
    ret

; tf_lfill - LZW_FILL: the strip's bytes into ES:DI, CX of room; out CX = put
; there, 0 at the strip's end or the file's. DS = this part's; keeps DS,
; ES, BP
tf_lfill:
    push ds
    mov ds, [cs:PXP_PKG]
    mov ax, [cs:ex_left]            ; AX = what the strip has left, at most
    cmp word [cs:ex_left + 2], 0    ; the room
    jne .r
    cmp ax, cx
    jbe .w
.r:
    mov ax, cx
.w:
    or ax, ax
    jz .end
.win:
    mov si, [px_rpos]
    mov bx, [px_rend]
    sub bx, si                      ; BX = the window's bytes
    jnz .have
    call ex_rnext
    jnc .win
.end:
    xor cx, cx
    pop ds
    ret
.have:
    cmp bx, ax
    jbe .n
    mov bx, ax
.n:
    mov cx, bx
    add [px_rpos], bx
    sub [cs:ex_left], bx
    sbb word [cs:ex_left + 2], 0
    push cx
    mov ds, [px_rsegc]
    rep movsb
    pop cx
    pop ds
    ret

; tf_lrun - LZW_RUN: ES:SI = a string of CX bytes, into the row in order; a
; row whole is made into pixels and emitted, and the strip's last answers
; CF = 1 AX = EX_DONE. Keeps DS, ES, BP and DX
tf_lrun:
.l:
    mov ax, [cs:tf_rb]
    sub ax, [cs:tf_rpos]            ; the row's room
    cmp ax, cx
    jbe .k
    mov ax, cx
.k:
    push cx
    mov cx, ax
    mov di, [cs:tf_raw]
    add di, [cs:tf_rpos]
    add [cs:tf_rpos], ax
    push ds
    push es
    pop ds
    rep movsb
    pop ds
    pop cx
    sub cx, ax
    mov ax, [cs:tf_rpos]
    cmp ax, [cs:tf_rb]
    jb .more
    mov word [cs:tf_rpos], 0
    call tf_rowdone
    cmp word [cs:tf_left], 0
    je .done
.more:
    or cx, cx
    jnz .l
    clc
    ret
.done:
    mov ax, EX_DONE
    stc
    ret

; tf_rowdone - the raw row at [ex_dseg]:[tf_raw]: the predictor undone, the
; pixels made into the row buffer, emitted. Preserves all
tf_rowdone:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ds, [cs:ex_dseg]
    mov es, [cs:ex_rseg]
    cld
    cmp byte [cs:tf_pred], 2        ; PREDICTOR 2: each byte plus the one a
    jne .px                         ; pixel before it
    mov si, [cs:tf_raw]
    mov bx, [cs:tf_spp]
    mov cx, [cs:tf_rb]
    sub cx, bx
    jbe .px
.pr:
    mov al, [si]
    add [si + bx], al
    inc si
    loop .pr
.px:
    mov si, [cs:tf_raw]
    xor di, di
    mov cx, [cs:tf_w]
    mov al, [cs:tf_phot]
    cmp al, 2
    je .rgb
    mov bl, [cs:tf_bits]            ; grey and palette: the values
    call ex_unpk
    cmp byte [cs:tf_phot], 3
    je .em
    mov bl, [cs:tf_bits]            ; GREY: scaled by 255, 85, 17 or 1, and
    xor bh, bh                      ; WhiteIsZero turned over
    mov bl, [cs:tf_gk + bx]
    xor di, di
    mov cx, [cs:tf_w]
.g:
    mov al, [es:di]
    mul bl
    cmp byte [cs:tf_phot], 0
    jne .gb
    not al
.gb:
    stosb
    loop .g
    jmp short .em
.rgb:
    cmp byte [cs:tf_spp], 3         ; RGB: as it stands...
    jne .rgba
    mov ax, cx
    shl cx, 1
    add cx, ax
    rep movsb
    jmp short .em
.rgba:
    cmp byte [cs:tf_alpha], 0       ; ...RGBA: the alpha composited, or the
    jne .al                         ; fourth sample dropped
.d4:
    movsb
    movsb
    movsb
    inc si
    loop .d4
    jmp short .em
.al:
    mov ah, [si + 3]
    lodsb
    call ex_blend
    stosb
    mov ah, [si + 2]
    lodsb
    call ex_blend
    stosb
    mov ah, [si + 1]
    lodsb
    call ex_blend
    stosb
    inc si
    loop .al
.em:
    call ex_emit
    dec word [cs:tf_left]
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tf_gk:      db 0, 255, 85, 0, 17, 0, 0, 0, 1    ; a level's scale, by bits

; =============================================================================
; ICO's DECODE: the XOR rows, bottom-up, the AND mask's bits over them
; =============================================================================
ic_dec:
    mov ax, [cs:ic_xo]
    mov dx, [cs:ic_xo + 2]
    call ex_rskip
    mov word [cs:ic_r], 0
.row:
    mov ax, [cs:ic_r]
    cmp ax, [cs:ic_hh]
    jb .go
    ret
.go:
    mov bx, [cs:ic_hh]              ; the source row: bottom-up
    dec bx
    sub bx, ax
    mov [cs:ex_y], bx
    mul word [cs:ic_ms]             ; its mask row
    add ax, ex_table
    mov [cs:ic_mrow], ax
    mov es, [cs:ex_dseg]            ; its bytes
    xor di, di
    mov cx, [cs:ic_stride]
    call ex_rdn
    push ds
    mov ds, [cs:ex_dseg]
    mov es, [cs:ex_rseg]
    xor si, si
    xor di, di
    mov cx, [cs:ic_w]
    mov al, [cs:ic_bits]
    cmp al, 24
    je .b24
    cmp al, 32
    je .b32
    mov bl, al                      ; 1..8 bits: the indices...
    call ex_unpk
    cmp word [cs:ic_ne], 256
    jae .rgbp
    xor di, di                      ; ...masked ones the background's entry
    xor bx, bx
.mi:
    call ic_masked
    jz .mn
    mov al, [cs:ic_ne]
    mov [es:di], al
.mn:
    inc di
    inc bx
    cmp bx, [cs:ic_w]
    jb .mi
    jmp .em
.rgbp:
    mov bx, [cs:ic_w]               ; 256 entries: their colours, from the
    dec bx                          ; last pixel back (they widen in place)
.rp:
    mov si, bx
    mov al, [es:bx]
    xor ah, ah
    mov di, ax
    shl di, 1
    add di, ax                      ; DI = its entry in [px_spal]
    mov si, bx
    shl si, 1
    add si, bx                      ; SI = where its colour goes
    call ic_masked
    jz .rc
    mov byte [es:si], PXP_BG
    mov byte [es:si + 1], PXP_BG
    mov byte [es:si + 2], PXP_BG
    jmp short .rn
.rc:
    push ds
    mov ds, [cs:PXP_PKG]
    mov al, [px_spal + di]
    mov [es:si], al
    mov al, [px_spal + di + 1]
    mov [es:si + 1], al
    mov al, [px_spal + di + 2]
    mov [es:si + 2], al
    pop ds
.rn:
    dec bx
    jns .rp
    jmp short .em
.b24:
    xor bx, bx                      ; BGR, masked the background
.p24:
    call ic_masked
    jz .c24
    mov al, PXP_BG
    stosb
    stosb
    stosb
    add si, 3
    jmp short .n24
.c24:
    mov al, [si + 2]
    stosb
    mov al, [si + 1]
    stosb
    mov al, [si]
    stosb
    add si, 3
.n24:
    inc bx
    loop .p24
    jmp short .em
.b32:
    mov ah, [si + 3]                ; BGRA, the alpha composited
    mov al, [si + 2]
    call ex_blend
    stosb
    mov ah, [si + 3]
    mov al, [si + 1]
    call ex_blend
    stosb
    mov ah, [si + 3]
    mov al, [si]
    call ex_blend
    stosb
    add si, 4
    loop .b32
.em:
    pop ds
    call ex_emit
    inc word [cs:ic_r]
    jmp .row

; ic_masked - BX = a pixel: ZF = 0 when the AND mask's row [ic_mrow] has its
; bit set (24-bit and paletted images; 32 has none). Preserves all
ic_masked:
    push ax
    push bx
    push cx
    mov cl, bl
    and cl, 7
    shr bx, 1
    shr bx, 1
    shr bx, 1
    add bx, [cs:ic_mrow]
    mov al, [cs:bx]
    shl al, cl
    test al, 0x80
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; IFF's DECODE: BODY's bytes, one stream, a row at a time
; =============================================================================
lb_dec:
    mov ax, [cs:lb_body]
    mov dx, [cs:lb_body + 2]
    call ex_rskip
    mov ax, [cs:lb_bsize]
    mov dx, [cs:lb_bsize + 2]
    call ex_setlim
.row:
    mov ax, [cs:ex_y]
    cmp ax, [cs:lb_h]
    jb .go
    ret
.go:
    mov es, [cs:ex_dseg]
    xor di, di
    mov cx, [cs:lb_rl]
    cmp byte [cs:lb_comp], 0
    je .raw
    call ex_rle
    jmp short .cv
.raw:
    call ex_rdn
.cv:
    push ds
    mov ds, [cs:ex_dseg]
    mov es, [cs:ex_rseg]
    xor si, si
    xor di, di
    mov cx, [cs:lb_w]
    cmp byte [cs:lb_pbm], 0
    je .il
    rep movsb                       ; PBM: its bytes
    jmp short .em
.il:
    mov bl, [cs:lb_planes]          ; ILBM: its planes
    mov bp, [cs:lb_pb]
    call ex_planar
.em:
    pop ds
    call ex_emit
    jmp short .row

; =============================================================================
; MacPaint's DECODE: 720 rows of 72 bytes, one PackBits stream to the end
; =============================================================================
mc_dec:
    mov ax, [cs:mc_off]
    xor dx, dx
    call ex_rskip
.row:
    cmp word [cs:ex_y], 720
    jb .go
    ret
.go:
    mov es, [cs:ex_dseg]
    xor di, di
    mov cx, 72
    call ex_rle
    push ds
    mov ds, [cs:ex_dseg]
    mov es, [cs:ex_rseg]
    mov cx, 576
    mov bl, 1
    mov bp, 72
    call ex_planar
    pop ds
    call ex_emit
    jmp short .row

%include "os88lzw.inc"

; --- this part's own memory ------------------------------------------------------
ex_hsp:     dw 0                    ; HEAD's frame
ex_dsp:     dw 0                    ; DECODE's
ex_ph:      db 0                    ; HEAD's phase: where a walk resumes
ex_be:      db 0                    ; the file's byte order: 1 big-endian
ex_hlo:     dd 0                    ; the head's window, file offsets
ex_hhi:     dd 0
ex_rdst:    dw 0                    ; ex_run's place in the TABLE...
ex_rdone:   dw 0                    ; ...and the bytes it has put there
ex_emitf:   dd 0                    ; K_EMIT, far
ex_ringf:   dd 0                    ; K_RING, far
ex_rseg:    dw 0                    ; the row buffer
ex_dseg:    dw 0                    ; the scratch
ex_lim:     db 0                    ; the stream is bounded...
ex_left:    dd 0                    ; ...to this many bytes more
ex_rmode:   db 0                    ; the run-length stream: 0 a control next,
ex_rcnt:    dw 0                    ; 1 a literal's, 2 a repeat's - this many
ex_rval:    db 0                    ; more, of this byte
ex_y:       dw 0                    ; the next row to emit
tf_ifd:     dd 0                    ; TIFF: IFD0
tf_n:       dw 0                    ; ...its entries
tf_cnt:     dw 0                    ; ...the scan's count down
tf_have:    dw 0                    ; ...the slots present
tf_tiled:   db 0
tf_vi:      db 0                    ; ...the value slot the walk is at
tf_aty:     db 0                    ; ...an array's type
tf_ty:      times TS_N db 0         ; a slot's type,
tf_cn:      times TS_N dd 0         ; its count,
tf_rw:      times TS_N dd 0         ; the entry's own four bytes,
tf_v:       times TS_N dd 0         ; and its first value
tf_w:       dw 0                    ; the picture: width, height...
tf_h:       dw 0
tf_rps:     dw 0                    ; ...rows a strip, strips
tf_ns:      dw 0
tf_bits:    dw 0                    ; ...bits, samples, their product
tf_spp:     dw 0
tf_bps:     db 0
tf_comp:    dw 0                    ; ...compression, predictor, kind, alpha
tf_pred:    db 0
tf_phot:    db 0
tf_alpha:   db 0
tf_ne:      dw 0                    ; ...a palette's entries
tf_rb:      dw 0                    ; ...a row's bytes, and their place in
tf_raw:     dw 0                    ; the scratch
tf_si:      dw 0                    ; DECODE: the strip
tf_left:    dw 0                    ; ...its rows left
tf_rpos:    dw 0                    ; ...the LZW's bytes in the row
ic_type:    db 0                    ; ICO: 1 an icon, 2 a cursor
ic_dsz:     dw 0                    ; ...the directory's end
ic_kwh:     dd 0                    ; ...the best so far: w x h, bits
ic_kb:      dw 0
ic_first:   db 0
ic_off:     dd 0                    ; ...the image
ic_hsz:     dw 0                    ; ...its DIB: header's bytes, width,
ic_w:       dw 0                    ; half-height, bits, entries
ic_hh:      dw 0
ic_bits:    db 0
ic_ne:      dw 0
ic_xo:      dd 0                    ; ...where its XOR rows are, their bytes
ic_stride:  dw 0
ic_ms:      dw 0                    ; ...the mask's row bytes, its bytes
ic_msz:     dw 0
ic_r:       dw 0                    ; DECODE: the row, and its mask row
ic_mrow:    dw 0
lb_pbm:     db 0                    ; IFF: PBM, not ILBM
lb_bm:      db 0                    ; ...BMHD seen, CMAP seen
lb_cmap:    db 0
lb_camg:    dw 0                    ; ...CAMG's low word
lb_pos:     dd 0                    ; ...the walk: a chunk, its data, size
lb_d:       dd 0
lb_sz:      dd 0
lb_k:       dw 0                    ; ...CMAP's entries
lb_w:       dw 0                    ; ...BMHD: width, height, planes,
lb_h:       dw 0                    ; masking, compression
lb_planes:  db 0
lb_mask:    db 0
lb_comp:    db 0
lb_ne:      dw 0                    ; ...the palette's entries
lb_body:    dd 0                    ; ...BODY's data and size
lb_bsize:   dd 0
lb_pb:      dw 0                    ; ...a plane row's bytes, a row's
lb_rl:      dw 0
mc_off:     dw 0                    ; MacPaint: where the PackBits start
ex_lzw:     times LZW_BSSSZ db 0    ; the LZW's state (os88lzw.inc)

%include "pxplan.inc"                ; the plans: shared source (106.20)

ex_table:   times EX_TABLE db 0     ; THE TABLE: TIFF's strips, ICO's mask
