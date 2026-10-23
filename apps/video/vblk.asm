; =============================================================================
; os8088 - apps/video/vblk.asm
;
; THE VIDEO PLAYER'S BLOCK DECODER (SPEC.md 98.3.23): a version 2 file's
; blocks (98.1.10), on the screen. A PART of VIDEO.O88 (row 0, OP_SEG |
; OP_LAZY | OP_COMP), far-called through the vectors vblk.inc names, fetched
; by the entry proc and kept for the session.
;
; SIX KINDS, three depths:
;   VGA8 (LIN320, and MODEX a plane at a time) - Video-1's three: FILL, one
;        colour; TWO, two and a 16-bit mask; EIGHT, a pair a 2 x 2 quadrant.
;        A block is 4 x 4 pixels.
;   VGA4 - PFILL, two colours in the 4 x 4 Bayer cell's dither at a level;
;        TWO, two colours and a mask a row. A block is one byte column, 8 x 4.
;   ONE BIT (CGA, HERC) - a TILE: one byte, an entry of a 256-entry codebook
;        the stream keeps up to date (vb_cbk, here in the part's own image).
;        A block is two bytes of four rows, 16 x 4, and its rows are in the
;        banks (vb_roff).
;
; A LIST is version 1's segment grammar with a block for an entry (98.1.10):
; vb_walk reads it and calls [vb_h] with DI = the block's top row's address,
; the origin added. Rows 1..3 are r x the canvas's row step down, and that
; step is a CONSTANT for a play - so it is PATCHED into the stores' own
; displacements at VBV_SET (vb_sites, below) rather than added at each, which
; is what keeps a block's stores at `mov [es:di+disp16]`'s price.
;
; ROUND THE TEXT BOX (AH = 1 at the vectors): every byte through the player's
; callback, whose clip machinery (vosd.inc's vc_put, vc_m3b) knows the box.
; Rare - only while the full screen's text is up - so it is the general form
; and not a fast one: VGA8's 16 pixels made into vb_px first, then handed out.
;
; TWO ASSEMBLIES (SPEC.md 98.3.23.2). Plain, this is part 0, and it keeps its
; state in its own image THROUGH CS - free in real mode, where a code segment
; is ordinary memory. With -DVB_PM it is part 1, the one a protected-mode
; kernel (CPU_F_PM: kern_mega) fetches instead: there the player far-calls it
; through its CODE alias, which is never writable, so every `mov [cs:x]` would
; be a #GP the kernel completes - per block, per frame (KERN-MEGA-PLAN 14.16's
; rule is no write through CS on a path a frame runs). That arm writes
; NOTHING through CS:
;   - a block's own path keeps its state in registers or on the stack - the
;     walk's count pushed, EIGHT's c0 kept in CL, an EGA block's two colours
;     in BP - so a block costs no segment load either;
;   - VBV_SET writes through ES, which is the part's own data segment there
;     (vblk.inc's contract), and keeps it in vb_ds;
;   - every other store - a vector's head, and round the box, a byte at a
;     time anyway - is VBW: DS borrowed from vb_ds for that one store.
; The plain assembly is byte for byte the part it was: every VB_PM line is
; inside an %ifdef, VBW and VBS spell `cs:` there.
; =============================================================================
    cpu 8086
    bits 16
    org 0

%include "video/vblk.inc"

; VBS - the segment vb_set writes through; VBSEG - VBW's; VBW insn - ONE
; instruction that writes the part's own state through [VBSEG:...]
%ifdef VB_PM
  %define VBS es
  %define VBSEG ds
  %macro VBW 1+
    push ds
    mov ds, [cs:vb_ds]
    %1
    pop ds
  %endmacro
%else
  %define VBS cs
  %define VBSEG cs
  %macro VBW 1+
    %1
  %endmacro
%endif

    dw VB_SIGVAL                    ; +0
    db VB_ABIVER                    ; +2
    db VBV_N                        ; +3
    dw vb_set                       ; +4 VBV_SET
    dw vb_lin                       ; +6 VBV_LIN
    dw vb_pln                       ; +8 VBV_PLN
    dw vb_one                       ; +10 VBV_ONE

; --- the row step's patch sites ----------------------------------------------
; VBX r, k, op, reg - `op [es:di + r x step + k], reg`. Row 0 is a plain
; displacement; rows 1..3 take VBD, which assembles a 16-bit one, and the
; site is recorded for vb_set to write r x step + k into
VBD         equ 0x7F7F
%assign vb_ns 0
%macro VBX 4
%if %1 == 0
    %3 [es:di + %2], %4
%else
    %3 [es:di + VBD], %4
%assign vb_ns vb_ns + 1
..@vbs %+ vb_ns:
..@vbr %+ vb_ns equ %1
..@vbk %+ vb_ns equ %2
%endif
%endmacro

; =============================================================================
; VBV_SET - AX = the row step, BH = the banks, BL = VBM_*, CX:DX = the
; callback. Out AX = VB_PROBE, CF = 0. Keeps SI, DI, BP, DS, ES - DI too,
; which the player's door is holding the rate hook's address in
; =============================================================================
vb_set:
%ifdef VB_PM
    mov [es:vb_ds], es              ; (ES = our own data segment, vblk.inc)
%endif
    mov [VBS:vb_str], ax
    mov [VBS:vb_mode], bl
    mov [VBS:vb_cb], dx
    mov [VBS:vb_cb + 2], cx
    test bl, VBM_NEWS               ; (a session's first bracket: no
    jz .kc                          ; codebook yet - a later one goes on
    mov byte [VBS:vb_cbok], 0       ; mid-stream with the one it has)
.kc:
    push si
    push di
    xor si, si                      ; THE FOUR ROWS' OFFSETS: row r is r mod
    xor cx, cx                      ; BH banks of 2000h on and r / BH steps
    xor dx, dx                      ; down - r steps on LIN320, MODEX and
    xor bl, bl                      ; VGA4; 0, 2000h, a step and both on a
.r:                                 ; CGA; r banks on a Hercules
    mov di, cx
    add di, dx
    mov [VBS:vb_roff + si], di
    add dx, 0x2000
    inc bl
    cmp bl, bh
    jb .n
    xor dx, dx
    xor bl, bl
    add cx, ax
.n:
    inc si
    inc si
    cmp si, 8
    jb .r
    mov si, vb_sites                ; ...and every store's displacement
    mov cx, VB_NS
.s:
    mov bl, [cs:si + 2]             ; r
    xor bh, bh
    shl bx, 1
    mov ax, [cs:vb_roff + bx]
    mov bl, [cs:si + 3]             ; + k
    xor bh, bh
    add ax, bx
    mov bx, [cs:si]
    mov [VBS:bx], ax
    add si, 4
    loop .s
    pop di
    pop si
    mov ax, VB_PROBE
    clc
    retf

; =============================================================================
; VBV_LIN - LIN320's three lists (FILL, TWO, EIGHT), AH = 1 round the box
; =============================================================================
vb_lin:
    VBW mov [VBSEG:vb_org], bp
    VBW mov byte [VBSEG:vb_lin8], 1
    VBW mov word [VBSEG:vb_w], 4    ; a block is four bytes of a row
    cld
    or ah, ah
    jnz .k
    VBW mov word [VBSEG:vb_h], vb_lfill
    call vb_walk
    VBW mov word [VBSEG:vb_h], vb_ltwo
    call vb_walk
    VBW mov word [VBSEG:vb_h], vb_leight
    call vb_walk
    jmp short vb_out
.k:
    VBW mov word [VBSEG:vb_h], vb_kfill
    call vb_walk
    VBW mov word [VBSEG:vb_h], vb_ktwo
    call vb_walk
    VBW mov word [VBSEG:vb_h], vb_keight
    call vb_walk
vb_out:
    mov bp, [cs:vb_org]
    clc
    retf

; =============================================================================
; VBV_PLN - AL = a planar lead (20h on), its one list; AH = 1 round the box.
; CF = 1 on a lead this part does not know: the record is given up
; =============================================================================
vb_pln:
    VBW mov [VBSEG:vb_org], bp
    VBW mov byte [VBSEG:vb_lin8], 0
    VBW mov word [VBSEG:vb_w], 1    ; a block is one byte of a row (a plane
    cld                             ; byte on MODEX, a byte column on VGA4)
    sub al, 0x20
    mov bl, al
    xor bh, bh
    shl bx, 1
    test byte [cs:vb_mode], VBM_VGA4
    jnz .v4
    cmp al, 2
    ja .bad
    or ah, ah
    jnz .mk
    mov ax, [cs:bx + vb_mh]
    jmp short .go
.mk:
    mov ax, [cs:bx + vb_kh]
    jmp short .go
.v4:
    cmp al, 1
    ja .bad
    mov cx, vb_v2c                  ; write mode 3's two colours...
    test byte [cs:vb_mode], VBM_NOWM3
    jz .w3
    mov cx, vb_v2e                  ; ...a plane at a time on an EGA...
.w3:
    or ah, ah
    jz .vt
    mov cx, vb_v2k                  ; ...or a byte at a time round the box
.vt:
    VBW mov [VBSEG:vb_vt], cx
    mov ax, [cs:bx + vb_vh]
.go:
    VBW mov [VBSEG:vb_h], ax
    call vb_walk
    jmp vb_out                      ; (short on the plain assembly)
.bad:
    mov bp, [cs:vb_org]
    stc
    retf

; =============================================================================
; vb_walk - the list at DS:SI: [vb_h] for each block, DI = its top row's
; address; [vb_w] the bytes a block's row covers, which a skip counts from.
; [vb_h] reads its payload off DS:SI and keeps DI; it may clobber AX, BX,
; CX, DX and BP. Clobbers AX, BX, CX, DX, DI, BP
; =============================================================================
vb_walk:
    ; STKBALANCE-LOOP: VB_PM keeps a segment's count PUSHED across its walk, and this source walk reads both %ifdef arms as one - each arm alone is balanced
.seg:
    lodsb
    xor ah, ah
    or al, al
    jz .end
    js .ab
%ifdef VB_PM
    push ax                         ; (the count on the stack: VB_PM)
%else
    mov [cs:vb_n], ax               ; SKIP-CODED: the address, then a skip
%endif
    lodsw                           ; before each block
    add ax, [cs:vb_org]
    xchg ax, di
.sk:
    lodsb
    xor ah, ah
    add di, ax
    call [cs:vb_h]
    add di, [cs:vb_w]
%ifdef VB_PM
    pop ax
    dec ax
    push ax
    jnz .sk
    pop ax
%else
    dec word [cs:vb_n]
    jnz .sk
%endif
    jmp short .seg
.ab:
    and al, 0x7F                    ; ABSOLUTE: an address each (80h, a
    jz .seg                         ; count of 0, no segment)
%ifdef VB_PM
    push ax
%else
    mov [cs:vb_n], ax
%endif
.an:
    lodsw
    add ax, [cs:vb_org]
    xchg ax, di
    call [cs:vb_h]
%ifdef VB_PM
    pop ax
    dec ax
    push ax
    jnz .an
    pop ax
%else
    dec word [cs:vb_n]
    jnz .an
%endif
    jmp short .seg
.end:
    ret

; =============================================================================
; LIN320: a byte a pixel, rows a step apart - word stores of the two pixels
; a word holds. TWO and EIGHT are c0 ^ ((c1 ^ c0) & mask), the mask's bits
; made bytes by vb_tw / vb_t2, so no pixel is a branch
; =============================================================================
vb_lfill:
    lodsb
    mov ah, al
    VBX 0, 0, mov, ax
    VBX 0, 2, mov, ax
    VBX 1, 0, mov, ax
    VBX 1, 2, mov, ax
    VBX 2, 0, mov, ax
    VBX 2, 2, mov, ax
    VBX 3, 0, mov, ax
    VBX 3, 2, mov, ax
    ret

; VB_LT2 r - row r of a TWO, its nibble's entry at BX: CX = c0 c0, BP = the
; difference twice
%macro VB_LT2 1
    mov ax, [cs:bx + vb_tw]
    and ax, bp
    xor ax, cx
    VBX %1, 0, mov, ax
    mov ax, [cs:bx + vb_tw + 2]
    and ax, bp
    xor ax, cx
    VBX %1, 2, mov, ax
%endmacro

vb_ltwo:
    lodsw
    xchg ax, dx                     ; DX = the mask: bit 4r + x is c1
    lodsw                           ; AL = c1, AH = c0
    mov cl, ah
    mov ch, ah
    xor al, ah
    mov ah, al
    xchg ax, bp
    mov bl, dl                      ; row 0, the low nibble
    and bx, 0x0F
    shl bx, 1
    shl bx, 1
    VB_LT2 0
    mov bl, dl                      ; row 1
    and bx, 0xF0
    shr bx, 1
    shr bx, 1
    VB_LT2 1
    mov bl, dh                      ; row 2
    and bx, 0x0F
    shl bx, 1
    shl bx, 1
    VB_LT2 2
    mov bl, dh                      ; row 3
    and bx, 0xF0
    shr bx, 1
    shr bx, 1
    VB_LT2 3
    ret

; VB_LT8 r, k - the word at byte k of row r, of the quadrant vb_pair loaded,
; its two bits' entry at BX
%macro VB_LT8 2
    mov ax, [cs:bx + vb_t2]
    and ax, bp
    xor ax, cx
    VBX %1, %2, mov, ax
%endmacro

vb_leight:
    lodsw
    xchg ax, dx                     ; DX = the mask
    call vb_pair                    ; LEFT-TOP: rows 0, 1, bytes 0, 1
    mov bl, dl
    and bx, 0x03
    shl bx, 1
    VB_LT8 0, 0
    mov bl, dl
    and bx, 0x30
    shr bx, 1
    shr bx, 1
    shr bx, 1
    VB_LT8 1, 0
    call vb_pair                    ; RIGHT-TOP: bytes 2, 3
    mov bl, dl
    and bx, 0x0C
    shr bx, 1
    VB_LT8 0, 2
    mov bl, dl
    and bx, 0xC0
%ifdef VB_PM
    rol bl, 1                       ; (bits 7..6 to 2..1, as the shift by
    rol bl, 1                       ; five below: CL stays c0, which VB_PM
    rol bl, 1                       ; keeps nowhere else)
%else
    mov cl, 5                       ; (CX is vb_pair's - and the shift's,
    shr bx, cl                      ; for one instruction: loaded again
    call vb_pairc                   ; below before it is read)
%endif
    VB_LT8 1, 2
    call vb_pair                    ; LEFT-BOTTOM: rows 2, 3
    mov bl, dh
    and bx, 0x03
    shl bx, 1
    VB_LT8 2, 0
    mov bl, dh
    and bx, 0x30
    shr bx, 1
    shr bx, 1
    shr bx, 1
    VB_LT8 3, 0
    call vb_pair                    ; RIGHT-BOTTOM
    mov bl, dh
    and bx, 0x0C
    shr bx, 1
    VB_LT8 2, 2
    mov bl, dh
    and bx, 0xC0
%ifdef VB_PM
    rol bl, 1
    rol bl, 1
    rol bl, 1
%else
    mov cl, 5
    shr bx, cl
    call vb_pairc
%endif
    VB_LT8 3, 2
    ret

; vb_pair - a quadrant's pair, c1 then c0, off DS:SI: CX = c0 c0, BP = the
; difference twice. vb_pairc makes CX again out of [vb_c0], for the one
; place a shift needed CL between. Clobbers AX
vb_pair:
    lodsw
%ifdef VB_PM
    mov cl, ah
    mov ch, ah
    xor al, ah
    mov ah, al
    xchg ax, bp
    ret
%else
    mov [cs:vb_c0], ah
    xor al, ah
    mov ah, al
    xchg ax, bp
vb_pairc:
    mov cl, [cs:vb_c0]
    mov ch, cl
    ret
%endif

; =============================================================================
; MODEX: a block is one plane byte of four rows, pixel x in plane x - so a
; row's pixels are written by the Map Mask: the planes that take c1, then the
; rest. The record's end puts the mask back to all four (vp_decrec)
; =============================================================================
vb_mfill:
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
    lodsb
    VBX 0, 0, mov, al
    VBX 1, 0, mov, al
    VBX 2, 0, mov, al
    VBX 3, 0, mov, al
    ret

; VB_MT r - AH = row r's planes that take c1 (CL), AL = 2, DX = 3C4h
%macro VB_MT 1
    out dx, ax
    VBX %1, 0, mov, cl
    xor ah, 0x0F
    out dx, ax
    VBX %1, 0, mov, ch
%endmacro

vb_mtwo:
    lodsw
    xchg ax, bx                     ; BX = the mask
    lodsw
    xchg ax, cx                     ; CL = c1, CH = c0
    mov dx, 0x3C4
    mov al, 2
    mov ah, bl
    and ah, 0x0F
    VB_MT 0
    mov ah, bl
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    VB_MT 1
    mov ah, bh
    and ah, 0x0F
    VB_MT 2
    mov ah, bh
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    VB_MT 3
    ret

; VB_ME r, half - AH = row r's planes in HALF (3 or 0Ch) that take c1
%macro VB_ME 2
    out dx, ax
    VBX %1, 0, mov, cl
    xor ah, %2
    out dx, ax
    VBX %1, 0, mov, ch
%endmacro

; VB_MQ src, half, r0 - a quadrant: its pair into CX, then its two rows
%macro VB_MQ 3
    lodsw
    xchg ax, cx
    mov al, 2
    mov ah, %1
    and ah, %2
    VB_ME %3, %2
    mov ah, %1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    and ah, %2
    VB_ME %3 + 1, %2
%endmacro

vb_meight:
    lodsw
    xchg ax, bx                     ; BX = the mask
    mov dx, 0x3C4
    VB_MQ bl, 0x03, 0               ; left-top
    VB_MQ bl, 0x0C, 0               ; right-top
    VB_MQ bh, 0x03, 2               ; left-bottom
    VB_MQ bh, 0x0C, 2               ; right-bottom
    ret

; =============================================================================
; VGA4: a block is one byte column of four rows, every plane. Each kind
; leaves DL = ab (a in the high nibble, a mask's set bits) and the four rows'
; masks in CL, CH, BL, BH, and jumps on to [vb_vt] - write mode 3's, an
; EGA's or the box's
; =============================================================================
vb_vpfill:
    lodsw                           ; AL = ab, AH = the level, 0..16
    mov dl, al
    mov bl, ah
    cmp bl, 16                      ; (more is no level: the most there is)
    jbe .k
    mov bl, 16
.k:
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov cx, [cs:bx + vb_pf]
    mov bx, [cs:bx + vb_pf + 2]
    jmp [cs:vb_vt]

vb_vtwo:
    lodsb
    mov dl, al
    lodsw
    xchg ax, cx
    lodsw
    xchg ax, bx
    jmp [cs:vb_vt]

; vb_v2c - WRITE MODE 3 (the player set it, 98.1.3.2.4): Set/Reset a, the
; masks ANDed in; Set/Reset b, their complements. Every bit of the column is
; written, so nothing under it survives
vb_v2c:
    mov al, dl
    mov bp, ax
    mov dx, 0x3CE
    mov ah, al
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    xor al, al                      ; Set/Reset: a
    out dx, ax
    VBX 0, 0, and, cl
    VBX 1, 0, and, ch
    VBX 2, 0, and, bl
    VBX 3, 0, and, bh
    mov ax, bp
    mov ah, al
    and ah, 0x0F
    xor al, al                      ; ...and b
    out dx, ax
    not cx
    not bx
    VBX 0, 0, and, cl
    VBX 1, 0, and, ch
    VBX 2, 0, and, bl
    VBX 3, 0, and, bh
    ret

; VB_EP p - plane p on an EGA: Map Mask p, and each row's byte b ^ ((a ^ b)
; & mask) in that plane's bits - DL = (a ^ b)'s, DH = b's, FFh or 0
%macro VB_EP 1
    mov dx, 0x3C4
    mov ax, ((1 << %1) << 8) | 2
    out dx, ax
    xor dx, dx
%ifdef VB_PM
    test bp, 0x10 << %1             ; (VB_PM: the two colours in BP)
%else
    test byte [cs:vb_ab], 0x10 << %1
%endif
    jz %%na
    mov dl, 0xFF
%%na:
%ifdef VB_PM
    test bp, 1 << %1
%else
    test byte [cs:vb_ab], 1 << %1
%endif
    jz %%nb
    not dl
    mov dh, 0xFF
%%nb:
    mov al, cl
    and al, dl
    xor al, dh
    VBX 0, 0, mov, al
    mov al, ch
    and al, dl
    xor al, dh
    VBX 1, 0, mov, al
    mov al, bl
    and al, dl
    xor al, dh
    VBX 2, 0, mov, al
    mov al, bh
    and al, dl
    xor al, dh
    VBX 3, 0, mov, al
%endmacro

; vb_v2e - NO WRITE MODE 3 (an EGA): a plane at a time, in write mode 0
vb_v2e:
%ifdef VB_PM
    mov bp, dx                      ; (a handler may spend BP)
%else
    mov [cs:vb_ab], dl
%endif
    VB_EP 0
    VB_EP 1
    VB_EP 2
    VB_EP 3
    ret

; vb_v2k - ROUND THE BOX: each row's mask with a, its complement with b, a
; byte at a time through the callback (vc_m3b)
vb_v2k:
    VBW mov [VBSEG:vb_ab], dl
    VBW mov [VBSEG:vb_mk], cx
    VBW mov [VBSEG:vb_mk + 2], bx
    VBW mov [VBSEG:vb_top], di
    VBW mov byte [VBSEG:vb_i], 0
.r:
    mov bl, [cs:vb_i]
    xor bh, bh
    mov al, [cs:vb_mk + bx]
    shl bx, 1
    mov di, [cs:vb_top]
    add di, [cs:vb_roff + bx]
    mov ah, [cs:vb_ab]
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    call far [cs:vb_cb]
    mov bl, [cs:vb_i]
    xor bh, bh
    mov al, [cs:vb_mk + bx]
    not al
    mov ah, [cs:vb_ab]
    and ah, 0x0F
    call far [cs:vb_cb]
    VBW inc byte [VBSEG:vb_i]
    cmp byte [cs:vb_i], 4
    jb .r
    mov di, [cs:vb_top]
    ret

; =============================================================================
; VGA8 ROUND THE BOX: the 16 pixels into vb_px, row-major, then each handed
; to the callback - at its byte on LIN320, at its row's byte under its plane
; on MODEX
; =============================================================================
vb_kfill:
    lodsb
    mov ah, al
%assign i 0
%rep 8
    VBW mov [VBSEG:vb_px + i], ax
%assign i i + 2
%endrep
    jmp short vb_emit

vb_ktwo:
    lodsw
    xchg ax, dx                     ; DX = the mask
    lodsw
    xchg ax, cx                     ; CL = c1, CH = c0
    xor bx, bx
.i:
    mov al, ch
    shr dx, 1
    jnc .s
    mov al, cl
.s:
    VBW mov [VBSEG:vb_px + bx], al
    inc bx
    cmp bx, 16
    jb .i
    jmp short vb_emit

vb_keight:
    lodsw
    xchg ax, dx                     ; DX = the mask
    lodsw                           ; the four pairs, c1 c0 each
    VBW mov [VBSEG:vb_pr], ax
    lodsw
    VBW mov [VBSEG:vb_pr + 2], ax
    lodsw
    VBW mov [VBSEG:vb_pr + 4], ax
    lodsw
    VBW mov [VBSEG:vb_pr + 6], ax
    xor bx, bx
.i:
    mov al, [cs:vb_q + bx]          ; the pixel's quadrant, x 2
    xor ah, ah
    mov bp, ax
    mov al, [cs:bp + vb_pr + 1]
    shr dx, 1
    jnc .s
    mov al, [cs:bp + vb_pr]
.s:
    VBW mov [VBSEG:vb_px + bx], al
    inc bx
    cmp bx, 16
    jb .i

; vb_emit - vb_px's sixteen, one callback each. Keeps DI
vb_emit:
    VBW mov [VBSEG:vb_top], di
    VBW mov byte [VBSEG:vb_i], 0
.p:
    mov bl, [cs:vb_i]
    xor bh, bh
    mov al, [cs:vb_px + bx]
    mov cl, bl
    and cl, 3                       ; x
    mov ah, 1
    shl ah, cl                      ; its plane (MODEX's Map Mask)
    shr bx, 1
    and bx, 6                       ; its row, x 2
    mov di, [cs:vb_top]
    add di, [cs:vb_roff + bx]
    cmp byte [cs:vb_lin8], 0        ; LIN320: x bytes along
    je .c
    xor ch, ch
    add di, cx
.c:
    call far [cs:vb_cb]
    VBW inc byte [VBSEG:vb_i]
    cmp byte [cs:vb_i], 16
    jb .p
    mov di, [cs:vb_top]
    ret

; =============================================================================
; VBV_ONE - ONE BIT (98.1.10): DS:SI = the codebook, n(16) [first(8) n x 8
; bytes], and then the tile list; AH = 1 round the box. The entries replace
; vb_cbk's first..first + n - 1, then each tile is its entry's eight bytes,
; rows 0..3 a row's left byte then its right. CF = 1 - the record given up -
; on a codebook past its 256, or a tile before any codebook this bracket
; =============================================================================
vb_one:
    VBW mov [VBSEG:vb_org], bp
    VBW mov word [VBSEG:vb_w], 2    ; a block is two bytes of a row
    VBW mov [VBSEG:vb_box], ah
    cld
    lodsw                           ; n
    or ax, ax
    jz .t
    cmp ax, 256
    ja .bad
    xchg ax, cx
    lodsb                           ; first
    xor ah, ah
    mov di, ax
    add ax, cx
    cmp ax, 256
    ja .bad
    shl di, 1                       ; its entry's byte
    shl di, 1
    shl di, 1
    add di, vb_cbk
    shl cx, 1                       ; n x 4 words
    shl cx, 1
    push es
%ifdef VB_PM
    mov es, [cs:vb_ds]              ; (VB_PM: our data segment, not CS's)
%else
    push cs
    pop es
%endif
    rep movsw
    pop es
    VBW mov byte [VBSEG:vb_cbok], 1
.t:
    cmp byte [cs:vb_cbok], 0        ; NO CODEBOOK YET: only an empty list
    jne .w
    cmp byte [si], 0
    jne .bad
.w:
    cmp byte [cs:vb_box], 0
    jne .k
    jmp vb_tiles
.k:
    VBW mov word [VBSEG:vb_h], vb_ktile
    call vb_walk
    jmp vb_out
.bad:
    mov bp, [cs:vb_org]
    stc
    retf

; vb_tiles - the tile list onto the surface, its own walk: vb_walk's grammar
; with the tile inlined, the count in CX and the origin in BP, which are a
; generic walk's indirect call, its count in memory and its width added from
; memory - most of what a tile cost through it (98.3.23). Ends at vb_out
; VB_TILE - the tile at DS:SI (its index) onto DI: its entry's four words at
; the four rows' offsets (patched, VBX). DI kept, BX and AX spent
%macro VB_TILE 0
    lodsb
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ax, [cs:bx + vb_cbk]
    VBX 0, 0, mov, ax
    mov ax, [cs:bx + vb_cbk + 2]
    VBX 1, 0, mov, ax
    mov ax, [cs:bx + vb_cbk + 4]
    VBX 2, 0, mov, ax
    mov ax, [cs:bx + vb_cbk + 6]
    VBX 3, 0, mov, ax
%endmacro

vb_tiles:
.seg:
    lodsb
    xor ah, ah
    or al, al
    jz .end
    js .ab
    xchg ax, cx                     ; SKIP-CODED: the address, then a skip
    lodsw                           ; before each tile
    add ax, bp
    xchg ax, di
.sk:
    lodsb
    xor ah, ah
    add di, ax
    VB_TILE
    inc di
    inc di
    loop .sk
    jmp short .seg
.ab:
    and al, 0x7F                    ; ABSOLUTE: an address each
    jz .seg
    xchg ax, cx
.an:
    lodsw
    add ax, bp
    xchg ax, di
    VB_TILE
    loop .an
    jmp .seg
.end:
    jmp vb_out

; vb_ktile - a tile ROUND THE BOX: its eight bytes, one callback each. The
; record's SI and the block's DI are kept in the part, not on the stack - the
; hook's 48 bytes are this path's bound. Keeps DI
vb_ktile:
    lodsb
    VBW mov [VBSEG:vb_ksi], si
    VBW mov [VBSEG:vb_top], di
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, vb_cbk
    xchg ax, si                     ; SI = the entry, in this segment
    xor bp, bp                      ; the row, x 2
.r:
    mov di, [cs:vb_top]
    add di, [cs:bp + vb_roff]
    mov al, [cs:si]
    call far [cs:vb_cb]
    inc di
    mov al, [cs:si + 1]
    call far [cs:vb_cb]
    inc si
    inc si
    inc bp
    inc bp
    cmp bp, 8
    jb .r
    mov di, [cs:vb_top]
    mov si, [cs:vb_ksi]
    ret

; =============================================================================
; the tables
; =============================================================================
vb_mh:      dw vb_mfill, vb_mtwo, vb_meight     ; MODEX's leads 20h..22h
vb_kh:      dw vb_kfill, vb_ktwo, vb_keight     ; ...round the box
vb_vh:      dw vb_vpfill, vb_vtwo               ; VGA4's 20h, 21h

; vb_tw - TWO's nibble n as two words of byte masks: pixels 0, 1 (FFh where
; the bit is set) and pixels 2, 3
vb_tw:
%assign n 0
%rep 16
    dw ((n & 1) * 0x00FF) | (((n >> 1) & 1) * 0xFF00)
    dw (((n >> 2) & 1) * 0x00FF) | (((n >> 3) & 1) * 0xFF00)
%assign n n + 1
%endrep
vb_t2:      dw 0x0000, 0x00FF, 0xFF00, 0xFFFF   ; ...and two bits' word

; vb_pf - PFILL's four row masks at each level 0..16: pixel x of row r is
; colour a where BAYER4[r * 4 + x mod 4] < the level, the high bit leftmost
; (tools/os88vid.py's PFILL_MASKS, which vidv2 holds this to)
vb_pf:
    db 0x00, 0x00, 0x00, 0x00       ; 0
    db 0x88, 0x00, 0x00, 0x00       ; 1
    db 0x88, 0x00, 0x22, 0x00       ; 2
    db 0xAA, 0x00, 0x22, 0x00       ; 3
    db 0xAA, 0x00, 0xAA, 0x00       ; 4
    db 0xAA, 0x44, 0xAA, 0x00       ; 5
    db 0xAA, 0x44, 0xAA, 0x11       ; 6
    db 0xAA, 0x55, 0xAA, 0x11       ; 7
    db 0xAA, 0x55, 0xAA, 0x55       ; 8
    db 0xEE, 0x55, 0xAA, 0x55       ; 9
    db 0xEE, 0x55, 0xBB, 0x55       ; 10
    db 0xFF, 0x55, 0xBB, 0x55       ; 11
    db 0xFF, 0x55, 0xFF, 0x55       ; 12
    db 0xFF, 0xDD, 0xFF, 0x55       ; 13
    db 0xFF, 0xDD, 0xFF, 0x77       ; 14
    db 0xFF, 0xFF, 0xFF, 0x77       ; 15
    db 0xFF, 0xFF, 0xFF, 0xFF       ; 16

; vb_q - EIGHT's pixel i (row-major) -> its quadrant's pair, x 2
vb_q:       db 0, 0, 2, 2, 0, 0, 2, 2, 4, 4, 6, 6, 4, 4, 6, 6

; vb_sites - every patched displacement: its address, r and k
vb_sites:
%assign i 1
%rep vb_ns
    dw ..@vbs %+ i - 2
    db ..@vbr %+ i, ..@vbk %+ i
%assign i i + 1
%endrep
VB_NS       equ vb_ns

; --- the state (the part's own image: it is a claim, and writable) ----------
%ifdef VB_PM
vb_ds:      dw 0                    ; our own DATA segment (VBV_SET's ES)
%endif
vb_str:     dw 0                    ; the row step
vb_roff:    dw 0, 0, 0, 0           ; ...and rows 0..3's offsets
vb_cb:      dd 0                    ; the player's callback
vb_org:     dw 0                    ; the origin, and the caller's BP
vb_w:       dw 0                    ; a block's bytes along a row
vb_h:       dw 0                    ; vb_walk's handler
vb_n:       dw 0                    ; ...the blocks left in a segment
vb_vt:      dw 0                    ; VGA4's tail
vb_top:     dw 0                    ; the block's top-left, round the box
vb_mode:    db 0                    ; VBM_*
vb_lin8:    db 0                    ; 1 in VBV_LIN
vb_ab:      db 0                    ; VGA4's two colours
vb_i:       db 0                    ; the box's counter
vb_c0:      db 0                    ; vb_pair's c0
vb_mk:      db 0, 0, 0, 0           ; VGA4's four masks, round the box
vb_pr:      times 8 db 0            ; EIGHT's four pairs, round the box
vb_px:      times 16 db 0           ; VGA8's sixteen pixels, round the box
vb_box:     db 0                    ; VBV_ONE's AH: round the box
vb_cbok:    db 0                    ; a codebook has come this session
vb_ksi:     dw 0                    ; the record's SI, round the box
vb_cbk:     times 2048 db 0         ; ONE BIT's codebook: 256 tiles of eight
                                    ; bytes. Zeros, so the packed part pays
                                    ; a few bytes for it and the claim 2 KB
