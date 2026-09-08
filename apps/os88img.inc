; =============================================================================
; os88img.inc - picture decoders: .PIX, .BMP and .PCX into packed 4bpp
; SPEC.md 93. Include LAST, after os88ui.inc and before OS88_BSS, exactly as
; os88line.inc and os88text.inc are.
;
; WHY AN INCLUDE AND NOT A KERNEL SLOT: the same argument 75.3 and 83 make. A
; slot costs a published contract that can never change; an include costs each
; package its own copy and can be revised freely - and a decoder that has met
; three real files is not a thing whose behaviour is settled.
;
; -----------------------------------------------------------------------------
; ONE OUTPUT FORMAT: packed 4bpp, two pixels per byte, HIGH NIBBLE LEFTMOST,
; indices 0..15 in os8088's own palette. That is exactly what
; OSAPI_GFX_BLIT4 takes and exactly what a .PIX block already holds, so there
; is one drawing path whatever the file was. BLIT4 and not OSAPI_GFX_BLITP,
; which would suit a planar PCX better: BLITP refuses an armed clip region,
; and a picture in a document that scrolls is always clipped.
;
; The destination stride is (width+1)/2, NO padding. Pass it to BLIT4 in BP.
;
; -----------------------------------------------------------------------------
; IT OWNS NO STATE, AND RETURNS NO POINTERS INTO ITSELF. Every variable lives
; in a block the CALLER allocates and passes in SI, the way os88line.inc takes
; its 20-byte block.
;
; Both halves of that matter and the second one is the subtler. 93.2: a
; module that names its own bss names it at an ABSOLUTE address, bss begins
; where the image ends, and two packages have different image sizes - so an
; include with private variables cannot serve a package that OVERLAYS it and
; one that does not. WORD reaches this through WORD.OVL; PAINT would reach it
; resident. For the same reason IMG_ERR is a NUMBER and not a string pointer:
; a string in an overlaid module is at a module-relative offset, and a
; resident caller reading it through DS gets whatever happens to sit there -
; the exact bug 93.2 records against an overlaid rasterizer. The caller words
; the message, which it should anyway: two packages report differently.
;
; -----------------------------------------------------------------------------
; WHAT IS ACCEPTED, AND WHAT GOES TO THE HOST TOOL INSTEAD
;
;   .PIX  any picture in the archive. No decoding at all - a block is already
;         this format (61.7), so it is found and copied. There is no 1bpp
;         .PIX and this file does not invent one: the format IS packed 4bpp
;         by definition, apps/frotz/zpic.inc is its authority, and a second
;         depth would be a change to a format three other things already
;         agree about, bought for a file nothing writes.
;   .BMP  BI_RGB, bottom-up or top-down, 4 OR 1 bit per pixel. 4bpp is what
;         SHEET, CHART and PAINT write; 1bpp is what every paint program of
;         the era wrote, and the only depth a Windows 2.x machine can put on
;         a clipboard at all.
;   .PCX  ZSoft's Technical Reference Manual revision 5, in all three of its
;         sixteen-colour-or-fewer arrangements: 1 bit in 1 plane (mono),
;         1 bit in 4 PLANES (EGA's own, and what PC Paintbrush wrote 16
;         colours in), and 4 bits in 1 plane. Four 1-bit planes ARE a 4-bit
;         index, so every one of them is exact.
;
; Five arrangements, and only TWO row routines - see img_rowconv. A 1bpp BMP
; row is one 1-bit plane and a 4bpp PCX row is a 4bpp BMP row, so each new
; depth reuses a loop the other format had already proved, rather than adding
; one that has only ever run against its own test file.
;
; REFUSED BY NAME rather than approximated: 8-bit .PCX, and 8- or 24-bit .BMP.
; Those need a 256-to-16 nearest-colour quantisation, which is a decision
; about how a picture should LOOK, and such a decision belongs on the host,
; where there is room to dither and to look at the answer. Refusal is an
; ordinary path (47).
;
; A SIXTEEN-ENTRY PALETTE IS HONOURED, which is not the same thing. A .BMP and
; a 4-plane .PCX each carry 16 RGB triples, and img_palmap maps every one to
; the nearest os8088 colour. For a file this tree wrote that is the identity;
; for a foreign one it is the difference between the picture and a
; colour-scrambled copy of it. Manhattan distance, not Euclidean:
; |dR|+|dG|+|dB| tops out at 765 and fits a word, where squared differences
; need 32 bits and buy nothing over sixteen candidates.
; =============================================================================


; -----------------------------------------------------------------------------
; INCLUDING THIS TWICE, AND WHY THE OPTION EXISTS. NASM's preprocessor runs top
; to bottom in one pass and its assembler needs two, so a `mov [si+IMG_W], ax`
; written ABOVE this file sizes its displacement long on the first pass and
; short on the second - which is a "label changed during code generation"
; refusal, not a wrong instruction. The shared CODE has to live at the end of a
; package (os88ui.inc's own placement rule), so a caller whose code comes
; earlier needs the CONSTANTS earlier still.
;
;   %define OS88IMG_CONSTS_ONLY     ; near the top: the equs and nothing else
;   %include "os88img.inc"
;   %undef  OS88IMG_CONSTS_ONLY
;   ...
;   %include "os88img.inc"          ; at the end: the code, constants guarded
;
; A caller with no such problem includes it once and says nothing. Same split
; apps/sheet/sheet.asm makes by hand for the CH_* geometry, made once here
; instead of copied into every package that needs it.
; -----------------------------------------------------------------------------
%ifndef OS88IMG__HAVECONSTS
%define OS88IMG__HAVECONSTS

OS88IMG_RAW  equ 640               ; the raw-scanline half of the caller's row
                                   ; buffer: four 1-bit planes of a 1280-pixel
                                   ; row, which is also a 4bpp BMP row of that
                                   ; width. 1280 and not a rounder 1024
                                   ; because real PC Paintbrush art is wider
                                   ; than a screen - the sample on the Dr.
                                   ; Dobb's disc is 1152 across, and a limit
                                   ; that refuses the first real file anyone
                                   ; tries is the wrong limit
OS88IMG_ROW  equ 1280              ; ...and the PACKED row is built in the
                                   ; second half, because converting in place
                                   ; would overwrite planes still to be read
OS88IMG_WMAX equ 1280              ; wider than this is refused, not cropped

; --- the caller's block ------------------------------------------------------
IMG_SRCSEG   equ 0                 ; word  in : the file's bytes, at offset 0
IMG_SRCLEN   equ 2                 ; word  in : how many bytes were read
IMG_DSTSEG   equ 4                 ; word  in : the picture goes here, offset 0
IMG_DSTMAX   equ 6                 ; word  in : its capacity in BYTES, 0=65536
IMG_ROWBUF   equ 8                 ; word  in : OS88IMG_ROW scratch bytes in DS
IMG_PICNO    equ 10                ; word  in : .PIX picture number, 0 = first
IMG_W        equ 12                ; word  out: width in pixels
IMG_H        equ 14                ; word  out: height in pixels
IMG_STRIDE   equ 16                ; word  out: destination bytes per row
IMG_ERR      equ 18                ; word  out: IMG_E_*, 0 = loaded
; --- private below here ------------------------------------------------------
IMG_P        equ 20                ; word: the source read cursor
IMG_BPL      equ 22                ; word: PCX BytesPerLine, per plane
IMG_NPL      equ 24                ; word: PCX NPlanes
IMG_TOT      equ 26                ; word: NPL * BPL - one raw scanline
IMG_Y        equ 28                ; word: the row being decoded
IMG_SBPR     equ 30                ; word: BMP source bytes per row, padded
IMG_FLIP     equ 32                ; word: 1 = the source's first row is the
                                   ; picture's LAST (a bottom-up BMP)
IMG_RUNC     equ 34                ; word: PCX run bytes still owed
IMG_RUNV     equ 36                ; word: ...and the value being repeated
IMG_PI       equ 38                ; word: img_palmap's source entry
IMG_BESTI    equ 40                ; word: ...its closest os8088 colour
IMG_BESTD    equ 42                ; word: ...and that colour's distance
IMG_SRGB     equ 44                ; 3 bytes: the source triple being matched
IMG_MASK     equ 48                ; byte: the bit img_row_planes gathers
IMG_BPOS     equ 50                ; word: ...and the byte of the plane it is in
IMG_ACC      equ 52                ; byte: its half-built output byte
IMG_PAL      equ 54                ; 16 bytes: source index -> os8088 colour
IMG_BPP      equ 70                ; word: bits per pixel, PER PLANE: 1 or 4.
                                   ; the whole choice of row routine, and the
                                   ; only thing that differs between a 1bpp
                                   ; BMP and a 4bpp one once the header is read
IMG_NPAL     equ 72                ; word: how many palette entries the file
                                   ; actually carries. 16 everywhere except a
                                   ; 1bpp BMP, which carries TWO - and reading
                                   ; sixteen out of a small one walks off the
                                   ; end of the file
OS88IMG_SZ   equ 76

; IMG_ERR values. Numbers, not strings - see the header note. A caller maps
; them to its own wording; 0 is always "loaded".
IMG_E_WHAT   equ 1                 ; not one of the three formats
IMG_E_SHORT  equ 2                 ; the file ends inside its own header
IMG_E_DIM    equ 3                 ; a width or height of 0, or too wide
IMG_E_BIG    equ 4                 ; more bytes than the caller reserved
IMG_E_TRUNC  equ 5                 ; the pixel data ends before the picture
IMG_E_DEPTH  equ 6                 ; a bit depth for the host tool
IMG_E_COMP   equ 7                 ; a compressed BMP
IMG_E_NOPIC  equ 8                 ; no such picture in the archive
IMG_E_VER    equ 9                 ; a format version this does not know

%endif  ; OS88IMG__HAVECONSTS

%ifndef OS88IMG_CONSTS_ONLY

; -----------------------------------------------------------------------------
; img_load - decode the file in [SI+IMG_SRCSEG] into [SI+IMG_DSTSEG].
; in:  SI = the block, its six input words filled
; out: CF=0 and IMG_W/IMG_H/IMG_STRIDE set; CF=1 and IMG_ERR names the reason.
;      SI preserved; every other register is the caller's to save.
;
; The format is decided by the file's own first bytes and never by its name.
; A .PCX called .BMP is a picture; a name is a hint (54).
; -----------------------------------------------------------------------------
img_load:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov word [si+IMG_ERR], 0
    mov word [si+IMG_W], 0
    mov word [si+IMG_H], 0
    mov word [si+IMG_STRIDE], 0
    mov word [si+IMG_Y], 0
    mov word [si+IMG_BPP], 4           ; what every path that does not say
    mov word [si+IMG_NPL], 1           ; otherwise gets: one plane of 4-bit
    mov word [si+IMG_NPAL], 16         ; pixels and a full sixteen colours
    mov es, [si+IMG_SRCSEG]
    cmp word [si+IMG_SRCLEN], 16       ; enough to TELL THEM APART, and to
    jb .short                          ; hold a whole .PIX header. Each
                                       ; decoder then checks the length its
                                       ; OWN header needs.
                                       ;
                                       ; ONE floor of 128 - the longest of the
                                       ; three headers - covered all of them
                                       ; and was simpler, and 1bpp made it
                                       ; wrong: a monochrome file is a quarter
                                       ; the size of the 4bpp one it replaces,
                                       ; so a 16x16 icon is 126 bytes and a
                                       ; whole legal picture was refused for
                                       ; being SMALL. Nothing is admitted that
                                       ; was not before - a file too short for
                                       ; the header it claims is still
                                       ; refused, by the test that knows which
                                       ; header that is
    cmp word [es:0], 'O' | ('8' << 8)  ; 'O8PIX' - the 61.7 archive
    jne .notpix
    cmp word [es:2], 'P' | ('I' << 8)
    jne .notpix
    cmp byte [es:4], 'X'
    jne .notpix
    call img_pix
    jmp .done
.notpix:
    cmp word [es:0], 'B' | ('M' << 8)
    jne .notbmp
    call img_bmp
    jmp .done
.notbmp:
    cmp byte [es:0], 10                ; ZSoft's Manufacturer constant...
    jne .unknown
    cmp byte [es:2], 1                 ; ...and the only Encoding there is
    jne .unknown
    call img_pcx
    jmp .done
.unknown:
    mov word [si+IMG_ERR], IMG_E_WHAT
    jmp .fail
.short:
    mov word [si+IMG_ERR], IMG_E_SHORT
.fail:
    mov word [si+IMG_W], 0             ; A REFUSAL LEAVES NO GEOMETRY BEHIND.
    mov word [si+IMG_H], 0             ; img_setgeom stores all three before the
    mov word [si+IMG_STRIDE], 0        ; pixel data has been proved to be inside
    stc                                ; the file, so a truncated PCX refuses
    jmp .out                           ; with a complete, plausible size in the
                                       ; block - and a caller that reads IMG_W
                                       ; as "there is a picture" draws half of
                                       ; one and half of a stale claim
.done:
    cmp word [si+IMG_ERR], 0
    jne .fail
    clc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; img_setgeom - AX = width, BX = height are known. Check them, derive the
; destination stride, and prove the whole picture fits what the caller
; reserved. out: CF=1 and IMG_ERR set on refusal.
; -----------------------------------------------------------------------------
img_setgeom:
    push ax
    push bx
    push cx
    push dx
    or ax, ax
    jz .bad
    or bx, bx
    jz .bad
    cmp ax, OS88IMG_WMAX
    ja .bad
    mov [si+IMG_W], ax
    mov [si+IMG_H], bx
    inc ax
    shr ax, 1                          ; stride = (w+1)/2, packed, unpadded
    mov [si+IMG_STRIDE], ax
    mul bx                             ; DX:AX = the whole picture. `mul`
    or dx, dx                          ; writes DX, and a picture whose size
    jnz .big                           ; needs more than a word cannot fit one
    mov cx, [si+IMG_DSTMAX]            ; segment however big the claim is
    or cx, cx
    jz .fits                           ; 0 means the whole 64KB
    cmp ax, cx
    ja .big
.fits:
    clc
    jmp .out
.big:
    mov word [si+IMG_ERR], IMG_E_BIG
    stc
    jmp .out
.bad:
    mov word [si+IMG_ERR], IMG_E_DIM
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; img_rowout - the packed half of the caller's row buffer holds [IMG_STRIDE]
; bytes; write them into the destination as row [IMG_Y]. Preserves all.
;
; IMG_FLIP inverts the row number rather than the loop, so a bottom-up BMP and
; a top-down one share one reader and differ in one subtraction.
; -----------------------------------------------------------------------------
img_rowout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [si+IMG_Y]
    cmp word [si+IMG_FLIP], 0
    je .down
    mov bx, [si+IMG_H]
    dec bx
    sub bx, ax
    mov ax, bx
.down:
    mov cx, [si+IMG_STRIDE]
    push si
    mul cx                             ; DX:AX = the row's offset; img_setgeom
    pop si                             ; already proved the product fits
    mov di, ax
    mov es, [si+IMG_DSTSEG]
    mov cx, [si+IMG_STRIDE]
    mov ax, [si+IMG_ROWBUF]
    add ax, OS88IMG_RAW
    mov si, ax
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

; -----------------------------------------------------------------------------
; img_rowconv - the raw half of the caller's row buffer holds one SOURCE row;
; build the packed half from it. Preserves all.
;
; Five accepted arrangements, two row routines, and IMG_BPP alone decides:
;
;   BMP 4bpp  \  ONE plane of 4-bit pixels, already two to a byte with the
;   PCX 4x1   /  high nibble leftmost. A palette pass - img_row_nib
;
;   BMP 1bpp  \
;   PCX 1x1    >  IMG_NPL 1-bit planes of IMG_BPL bytes, bit 7 leftmost, plane
;   PCX 1x4   /   p contributing bit p of the index - img_row_planes
;
; THE CROSS-USE IS THE POINT AND IT IS WHY THIS IS A DISPATCHER RATHER THAN
; TWO MORE LOOPS. A 1bpp BMP row IS a single 1-bit plane, so it goes through
; the routine written for PCX with IMG_NPL=1 and IMG_BPL=IMG_SBPR; a 4bpp PCX
; row IS a 4bpp BMP row, byte for byte. Neither added depth needed a new pixel
; loop, so neither one is a loop that has only ever run against the test file
; written to exercise it - each arrived already proved by the other format's
; corpus, including a 1152x90 file this project did not write.
; -----------------------------------------------------------------------------
img_rowconv:
    push ax
    push bx
    cmp word [si+IMG_BPP], 4
    jne .planes
    call img_row_nib
    jmp .tail
.planes:
    call img_row_planes
.tail:
    call img_rowmask
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; img_rowmask - AN ODD WIDTH LEAVES THE LAST LOW NIBBLE OF THE PACKED ROW
; UNUSED, AND IT IS ZERO. Preserves all.
;
; It is outside the picture, so nothing draws it and nothing looks at it - and
; it still has to be decided, because the routines above did not agree about
; it. img_row_planes stops at IMG_W and leaves it 0; img_row_nib works in
; whole BYTES, so it mapped the source's own padding nibble THROUGH THE
; PALETTE and left PAL[0] there instead.
;
; Which made a decoded picture depend on a colour it does not contain: the
; same 35-wide image came out with a different last byte per row under our
; palette and under a reversed one. Invisible, and exactly the kind of
; difference that makes two readers of one file disagree for a reason neither
; of them can see.
;
; It is a property of the OUTPUT FORMAT and not of any one decoder, so it is
; decided once and not twice - and that is why it is a routine rather than
; img_rowconv's tail. .PIX does not convert anything and so never reached
; img_rowconv at all, which left the one format written by this tree as the
; one format whose padding nibble was whatever the file said.
; -----------------------------------------------------------------------------
img_rowmask:
    push ax
    push bx
    test word [si+IMG_W], 1
    jz .out
    mov bx, [si+IMG_ROWBUF]
    add bx, OS88IMG_RAW
    add bx, [si+IMG_STRIDE]
    dec bx
    mov al, [bx]
    and al, 0xF0
    mov [bx], al
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; img_palmap - fill [SI+IMG_PAL] with, for each of [SI+IMG_NPAL] source RGB
; triples at ES:DI, the nearest os8088 colour. DL = 0 for a PCX's R,G,B
; triples or 1 for a BMP's B,G,R,reserved quads. Preserves all.
;
; The table is sixteen entries whatever the file carries, and the entries the
; file does NOT carry are left as the IDENTITY rather than as zero. A 1bpp BMP
; can only ever produce index 0 or 1, so the tail is unreachable through the
; pixels - but it is reachable through a corrupt one, and a defined answer
; there is a wrong colour instead of a whole picture in black.
; -----------------------------------------------------------------------------
img_palmap:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call img_palident                  ; the entries the file does not carry
    mov word [si+IMG_PI], 0
.entry:
    cmp dl, 0
    je .rgb
    mov al, [es:di+2]                  ; a BMP palette entry is B,G,R,reserved
    mov [si+IMG_SRGB], al
    mov al, [es:di+1]
    mov [si+IMG_SRGB+1], al
    mov al, [es:di]
    mov [si+IMG_SRGB+2], al
    jmp .have
.rgb:
    mov al, [es:di]                    ; a PCX one is plain R,G,B
    mov [si+IMG_SRGB], al
    mov al, [es:di+1]
    mov [si+IMG_SRGB+1], al
    mov al, [es:di+2]
    mov [si+IMG_SRGB+2], al
.have:
    push di
    push dx
    mov word [si+IMG_BESTI], 0
    mov word [si+IMG_BESTD], 0xFFFF
    xor bx, bx                         ; bx = the candidate, 0..15
.cand:
    mov di, bx
    push bx
    mov ax, di
    mov bx, 3
    mul bx                             ; DX:AX = the triple's offset; three
    pop bx                             ; times fifteen needs no high word
    mov di, ax
    xor cx, cx
    mov al, [si+IMG_SRGB]              ; |dR| - IMG_PAL8088 IS READ THROUGH CS.
    sub al, [cs:di+img_pal8088]        ; It is a table in this file's own
    jnc .r                             ; IMAGE, and if this include is built
    neg al                             ; into an overlay that image is not the
.r:                                    ; one DS points at (93.2)
    mov ah, 0
    add cx, ax
    mov al, [si+IMG_SRGB+1]            ; + |dG|
    sub al, [cs:di+img_pal8088+1]
    jnc .g
    neg al
.g:
    mov ah, 0
    add cx, ax
    mov al, [si+IMG_SRGB+2]            ; + |dB|, and the sum is under 766
    sub al, [cs:di+img_pal8088+2]
    jnc .b
    neg al
.b:
    mov ah, 0
    add cx, ax
    cmp cx, [si+IMG_BESTD]
    jae .next
    mov [si+IMG_BESTD], cx
    mov [si+IMG_BESTI], bx
.next:
    inc bx
    cmp bx, 16
    jb .cand
    pop dx
    pop di
    mov bx, si
    add bx, IMG_PAL
    add bx, [si+IMG_PI]
    mov ax, [si+IMG_BESTI]
    mov [bx], al
    cmp dl, 0
    je .step3
    add di, 4
    jmp .stepped
.step3:
    add di, 3
.stepped:
    mov ax, [si+IMG_NPAL]
    inc word [si+IMG_PI]
    cmp [si+IMG_PI], ax
    jb .entry
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; img_palident - no palette in the file: source index IS an os8088 colour.
; .PIX blocks are already written in our own indices (61.7), so this is what
; they get, and it is also the identity a BMP written by this tree lands on.
img_palident:
    push ax
    push bx
    push cx
    mov bx, si
    add bx, IMG_PAL
    xor cx, cx
.l:
    mov al, cl
    mov [bx], al
    inc bx
    inc cx
    cmp cx, 16
    jb .l
    pop cx
    pop bx
    pop ax
    ret

; os8088's own sixteen, CBLACK..CWHITE, as R,G,B: the table
; tools/os88pix.py matches on the host and apps/os88chart.inc writes into a
; BMP palette. R,G,B order because that is a PCX header's; a BMP's is
; byte-reversed where it is read.
img_pal8088:
    db 0x00,0x00,0x00,  0x00,0x00,0xAA,  0x00,0xAA,0x00,  0x00,0xAA,0xAA
    db 0xAA,0x00,0x00,  0xAA,0x00,0xAA,  0xAA,0x55,0x00,  0xAA,0xAA,0xAA
    db 0x55,0x55,0x55,  0x55,0x55,0xFF,  0x55,0xFF,0x55,  0x55,0xFF,0xFF
    db 0xFF,0x55,0x55,  0xFF,0x55,0xFF,  0xFF,0xFF,0x55,  0xFF,0xFF,0xFF

; =============================================================================
; .PIX - the Frotz archive (61.7). Nothing is decoded: a block is already
; packed 4bpp in our own palette, so the work is finding the right one and
; checking that what the directory claims is inside the bytes actually read.
;
; It is an ARCHIVE and not a picture, which is the one thing about this format
; a caller has to know: [IMG_PICNO] chooses, and 0 means "the first entry",
; because picture NUMBERS are not contiguous (Standard 8.8.6.1) and a caller
; that just wants the artwork should not have to know what they are.
; =============================================================================
img_pix:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [es:5], 1                 ; ZP_FMT_VER. A different version is
    jne .ver                           ; refused whole rather than read hopefully
    cmp word [es:10], 16               ; the entry size is IN the file so that a
    jne .ver                           ; future tool that grows it is refused
    mov cx, [es:6]                     ; here instead of misread here
    or cx, cx
    jz .nopic
    mov di, 16                         ; the directory starts after the header
    mov ax, cx
    mov bx, 16
    mul bx                             ; DX:AX = its length
    or dx, dx
    jnz .trunc
    add ax, 16
    jc .trunc
    cmp ax, [si+IMG_SRCLEN]
    ja .trunc
.scan:
    cmp word [si+IMG_PICNO], 0
    je .found                          ; 0 = whichever is first
    mov ax, [es:di]
    cmp ax, [si+IMG_PICNO]
    je .found
    add di, 16
    dec cx
    jnz .scan
    jmp .nopic
.found:
    mov ax, [es:di+2]                  ; width
    mov bx, [es:di+4]                  ; height
    call img_setgeom
    jc .out
    cmp word [es:di+10], 0             ; the block offset's high word: a .PIX
    jne .trunc                         ; past the 64KB horizon has its pixels
    mov ax, [es:di+8]                  ; past it too, and we read one segment
    mov [si+IMG_P], ax
    mov cx, [es:di+6]                  ; the file's own stride, which may be
    mov [si+IMG_SBPR], cx              ; padded wider than ours
    cmp cx, [si+IMG_STRIDE]
    jb .trunc                          ; a stride under (w+1)/2 cannot hold the
    mov ax, cx                         ; row it claims to
    mov bx, [si+IMG_H]
    mul bx                             ; DX:AX = the block. The tool refuses
    or dx, dx                          ; one over 64KB and so does zpic.inc,
    jnz .trunc                         ; so this agrees with both
    add ax, [si+IMG_P]
    jc .trunc
    cmp ax, [si+IMG_SRCLEN]
    ja .trunc
    call img_palident                  ; already our own indices
    mov word [si+IMG_FLIP], 0
    mov word [si+IMG_Y], 0
.row:
    mov di, [si+IMG_ROWBUF]
    add di, OS88IMG_RAW                ; straight into the packed half: there
    mov cx, [si+IMG_STRIDE]            ; is nothing to convert
    mov bx, [si+IMG_P]
.copy:
    mov al, [es:bx]
    mov [di], al
    inc bx
    inc di
    dec cx
    jnz .copy
    call img_rowmask                   ; the one format that does not convert
    call img_rowout                    ; still owes the output format its rule
    mov ax, [si+IMG_SBPR]              ; the SOURCE stride, so a padded row
    add [si+IMG_P], ax                 ; steps over its own padding
    inc word [si+IMG_Y]
    mov ax, [si+IMG_Y]
    cmp ax, [si+IMG_H]
    jb .row
    jmp .out
.ver:
    mov word [si+IMG_ERR], IMG_E_VER
    jmp .out
.nopic:
    mov word [si+IMG_ERR], IMG_E_NOPIC
    jmp .out
.trunc:
    mov word [si+IMG_ERR], IMG_E_TRUNC
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; .BMP - BI_RGB at 4 or 1 bit per pixel. 4bpp is what SHEET, CHART and PAINT
; write, and the one format in this tree that already travels between
; packages. 1bpp is the other half of the era: MSP, Windows 2.x Paint, a fax,
; a scanned page - and on a Windows 2.x machine it is the ONLY depth anything
; can put on a clipboard, so a document that came from one has no other form.
;
; Everything the header says is checked before anything is believed, and above
; all that the pixel data it describes is inside the bytes actually read: the
; file came off a floppy someone else wrote (19), and the destination is a
; segment nobody else can see. Same posture as pt_bmp_in (42.6).
; =============================================================================
img_bmp:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp word [si+IMG_SRCLEN], 54       ; 14 + the smallest BITMAPINFOHEADER.
    jb .trunc                          ; every header word read below is
                                       ; inside that, and the palette and the
                                       ; pixels are bounds-checked where they
                                       ; are used
    cmp word [es:12], 0                ; bfOffBits, high word: a picture past
    jne .trunc                         ; the 64KB horizon has its pixels past
    cmp word [es:16], 0                ; it too. biSize's high word likewise -
    jne .trunc                         ; bfOffBits is the dword at 10 and biSize
                                       ; the dword at 14, so the high words are
                                       ; 12 and 16. biWidth's is checked at
                                       ; .geom, where it is read
    mov ax, [es:14]                    ; biSize: BITMAPINFOHEADER or later
    cmp ax, 40
    jb .trunc
    mov [si+IMG_P], ax                 ; banked: the palette sits after it
    cmp word [es:26], 1                ; biPlanes. A BMP states its depth in
    jne .depth                         ; biBitCount and this is 1 in every
    mov ax, [es:28]                    ; file that is not a planar oddity
    cmp ax, 4                          ; biBitCount: four...
    je .depthok
    cmp ax, 1                          ; ...or one
    jne .depth
.depthok:
    mov [si+IMG_BPP], ax
    cmp word [es:30], 0                ; biCompression: BI_RGB and nothing
    jne .comp                          ; else. RLE4 is 2 and belongs to a
    cmp word [es:32], 0                ; decoder somebody writes later, not to
    jne .comp                          ; a branch here
    cmp word [es:20], 0
    jne .trunc
    mov ax, [es:18]                    ; biWidth, low word
    cmp word [es:24], 0                ; biHeight is SIGNED and 32-bit, so its
    jne .maybeflip                     ; sign is in the HIGH word - testing
    mov bx, [es:22]                    ; [es:22] tests the height itself, which
    mov word [si+IMG_FLIP], 1          ; is nonzero for every real picture, and
    jmp .geom                          ; sent every BOTTOM-UP file down the
.maybeflip:                            ; top-down path
    cmp word [es:24], 0xFFFF           ; a 32-bit negative small enough to be a
    jne .trunc                         ; height is FFFF:xxxx; anything else is
    mov bx, [es:22]                    ; a height over 65535
    neg bx
    mov word [si+IMG_FLIP], 0          ; a NEGATIVE height is top-down
.geom:
    call img_setgeom
    jc .out
    mov ax, [si+IMG_W]                 ; the SOURCE row, rounded up to a
    cmp word [si+IMG_BPP], 1           ; multiple of FOUR BYTES - the BMP rule,
    je .sbpr1                          ; not ours, and it is padding whichever
    inc ax                             ; depth it is. 4bpp: two pixels a byte
    shr ax, 1
    jmp .sbprpad
.sbpr1:
    add ax, 7                          ; 1bpp: eight
    shr ax, 1
    shr ax, 1
    shr ax, 1
.sbprpad:
    add ax, 3
    and ax, 0xFFFC
    mov [si+IMG_SBPR], ax
    mov [si+IMG_BPL], ax               ; ONE plane, so the plane stride IS the
    cmp ax, OS88IMG_RAW                ; row - which is the whole of what lets
    ja .dim                            ; a 1bpp row reach img_row_planes
    mov ax, 16                         ; a 1bpp file carries TWO entries, and
    cmp word [si+IMG_BPP], 1           ; reading sixteen out of one walks into
    jne .havenpal                      ; its pixels - or off the end of a small
    mov ax, 2                          ; file, which is the same bug in a hat
.havenpal:
    mov [si+IMG_NPAL], ax
    mov cx, ax                         ; four bytes an entry
    shl cx, 1
    shl cx, 1
    mov di, 14                         ; the palette follows the info header
    add di, [si+IMG_P]
    jc .trunc                          ; ...and a biSize that wraps the segment
    mov ax, di                         ; is not an offset into this file
    add ax, cx
    jc .trunc
    cmp ax, [si+IMG_SRCLEN]
    ja .trunc
    mov dl, 1                          ; B,G,R,reserved
    call img_palmap
    mov ax, [es:10]                    ; bfOffBits, at last
    mov [si+IMG_P], ax
    mov ax, [si+IMG_SBPR]
    mov bx, [si+IMG_H]
    mul bx                             ; DX:AX = every source row
    or dx, dx
    jnz .trunc
    add ax, [si+IMG_P]
    jc .trunc
    cmp ax, [si+IMG_SRCLEN]
    ja .trunc
    mov word [si+IMG_Y], 0
.row:
    mov cx, [si+IMG_SBPR]              ; one source row into the raw half...
    mov di, [si+IMG_ROWBUF]
    mov bx, [si+IMG_P]
.copy:
    mov al, [es:bx]
    mov [di], al
    inc bx
    inc di
    dec cx
    jnz .copy
    call img_rowconv                   ; ...and through the palette map
    call img_rowout
    mov ax, [si+IMG_SBPR]
    add [si+IMG_P], ax
    inc word [si+IMG_Y]
    mov ax, [si+IMG_Y]
    cmp ax, [si+IMG_H]
    jb .row
    jmp .out
.depth:
    mov word [si+IMG_ERR], IMG_E_DEPTH
    jmp .out
.comp:
    mov word [si+IMG_ERR], IMG_E_COMP
    jmp .out
.dim:
    mov word [si+IMG_ERR], IMG_E_DIM
    jmp .out
.trunc:
    mov word [si+IMG_ERR], IMG_E_TRUNC
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; img_row_nib - the raw half holds one row of 4-bit pixels, two to a byte with
; the high nibble leftmost; build the packed half with every nibble mapped
; through IMG_PAL.
;
; That is a 4bpp BMP row and it is equally a 4bpp one-plane PCX row - the two
; formats differ in how the row got into the buffer and not at all in what is
; in it. Both halves are already the output layout, so this is a palette pass
; and not a repack, and for a file this tree wrote the map is the identity and
; the bytes come out equal.
;
; XLAT is the whole routine: AL indexes a 16-byte table at DS:BX, which is
; exactly what a palette map is. It also frees SI, so the block moves out of
; SI for the duration and the row pointers move in. Nibbles are shifted ONE AT
; A TIME rather than by CL: an 8086 shifts by 1 or by CL and nothing else, and
; borrowing CL would mean banking the loop counter around every pixel.
; -----------------------------------------------------------------------------
img_row_nib:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [si+IMG_STRIDE]
    mov dx, [si+IMG_ROWBUF]
    mov bx, si
    add bx, IMG_PAL                    ; BX = XLAT's table, computed while SI
    mov si, dx                         ; is still the block
    mov di, dx
    add di, OS88IMG_RAW
.b:
    mov al, [si]
    inc si
    mov ah, al                         ; both nibbles banked
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlat                               ; the LEFT pixel, mapped
    mov dl, al
    mov al, ah
    and al, 0x0F
    xlat                               ; ...and the right
    shl dl, 1
    shl dl, 1
    shl dl, 1
    shl dl, 1
    or al, dl
    mov [di], al                       ; NOT stosb: ES is the source FILE here,
    inc di                             ; and the packed row lives in DS
    loop .b
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; .PCX - ZSoft's Technical Reference Manual, revision 5, which is on the
; Dr. Dobb's File Formats disc as GRAPHICS/PCX/SPEC/PCX.TXT. Written against
; that and not against a summary, because two details here decode fine against
; your own encoder and fail against everyone else's:
;
;   - BytesPerLine "MUST be an EVEN number. Do NOT calculate from Xmax-Xmin."
;     It is padding, and the padding is inside the compressed stream.
;   - The run tag can legally encode a run of ONE, so an encoder MUST emit a
;     literal byte >= 0xC0 as a run. A decoder that treats >= 0xC0 as data
;     works on most files and desynchronises on the ones that do not.
;
; And one the manual states plainly and which shapes the loop: "There should
; always be a decoding break at the end of each scan line. But there will not
; be a decoding break at the end of each plane within each scan line." So a
; run may span planes and may not span rows, which is why IMG_RUNC is carried
; across the plane loop and cleared at each row.
;
; SIXTEEN COLOURS OR FEWER, IN ANY OF THE THREE WAYS THE FORMAT SPELLS THAT:
; 1 bit in 1 plane, 1 bit in 4 PLANES, or 4 bits in 1 plane. The four-plane
; form is EGA's own arrangement and the one PC Paintbrush actually wrote, but
; the manual permits all three and a reader that assumes the popular one is a
; reader that fails on a file it has no business failing on. Four 1-bit planes
; ARE a 4-bit index, so every arrangement here is exact - nothing is
; approximated and nothing is quantised. 8-bit PCX still is refused: THAT
; needs a 256-to-16 quantisation, which belongs on the host.
;
; The header's sixteen RGB triples at offset 16 serve all three. They are the
; palette for any PCX of sixteen colours or fewer whatever shape its pixels
; are in - a 256-colour file is the one that keeps its palette elsewhere, and
; that is the file this refuses.
; =============================================================================
img_pcx:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp word [si+IMG_SRCLEN], 128      ; the whole PCX header - which is where
    jb .trunc                          ; the sixteen-colour palette lives, so
                                       ; there is no shorter legal one
    mov al, [es:3]                     ; BitsPerPixel, PER PLANE
    xor ah, ah
    mov [si+IMG_BPP], ax
    mov al, [es:65]                    ; NPlanes
    xor ah, ah
    mov [si+IMG_NPL], ax
    cmp word [si+IMG_BPP], 1
    je .onebit
    cmp word [si+IMG_BPP], 4           ; 4 bits in ONE plane: the other legal
    jne .depth                         ; sixteen-colour spelling, and already
    cmp ax, 1                          ; the layout we want. 4 bits in FOUR
    jne .depth                         ; planes would be 16-bit colour
    jmp .npok
.onebit:
    cmp ax, 1                          ; one 1-bit plane is monochrome...
    je .npok
    cmp ax, 4                          ; ...and four ARE a 4-bit index
    jne .depth
.npok:
    mov ax, [es:8]                     ; Xmax
    sub ax, [es:4]                     ; ...less Xmin
    jc .dim
    inc ax
    mov bx, [es:10]                    ; Ymax
    sub bx, [es:6]                     ; ...less Ymin
    jc .dim
    inc bx
    call img_setgeom
    jc .out
    mov ax, [es:66]                    ; BytesPerLine - carried, never derived
    or ax, ax
    jz .dim
    test ax, 1
    jnz .dim                           ; "MUST be an EVEN number"
    mov [si+IMG_BPL], ax
    mov cx, [si+IMG_W]                 ; ...and it must still cover the width
    cmp word [si+IMG_BPP], 1           ; it claims, which is a different number
    jne .min4                          ; at each depth
    add cx, 7                          ; 1bpp: eight pixels a byte
    shr cx, 1
    shr cx, 1
    shr cx, 1
    jmp .havemin
.min4:
    inc cx                             ; 4bpp: two
    shr cx, 1
.havemin:
    cmp ax, cx                         ; AX is still BytesPerLine
    jb .dim
    mul word [si+IMG_NPL]              ; DX:AX = one raw scanline
    or dx, dx
    jnz .dim
    mov [si+IMG_TOT], ax
    cmp ax, OS88IMG_RAW
    ja .dim
    cmp byte [es:1], 3                 ; VERSION 3 IS "2.8 WITHOUT PALETTE
    je .nopal                          ; INFORMATION". The sixteen triples at
    mov di, 16                         ; 16 are then not a palette, and a file
    xor dl, dl                         ; that leaves them zero maps every index
    call img_palmap                    ; to CBLACK - a whole picture in black,
    jmp .havepal                       ; which is the outcome the palette pass
.nopal:                                ; exists to avoid. The palette a version
    call img_palident                  ; 3 file means is the fixed EGA one, and
.havepal:                              ; that IS os8088's own sixteen in order.
                                       ;
                                       ; ONE call for all three arrangements
                                       ; otherwise: R,G,B triples, and sixteen
                                       ; of them at offset 16 whatever the
                                       ; pixels look like. A monochrome file
                                       ; uses only the first two, and it MUST -
                                       ; the identity would make a scanned page
                                       ; black and BLUE, and it is on the page
                                       ; to say so
    mov word [si+IMG_P], 128           ; the pixel data begins after the header
    mov word [si+IMG_FLIP], 0          ; PCX is top-down
    mov word [si+IMG_Y], 0
.row:
    mov word [si+IMG_RUNC], 0          ; a run never spans a scan line
    mov cx, [si+IMG_TOT]
    mov di, [si+IMG_ROWBUF]
.fill:
    call img_pcxbyte                   ; -> AL, or CF=1 at the end of the file
    jc .trunc
    mov [di], al
    inc di
    dec cx
    jnz .fill
    call img_rowconv
    call img_rowout
    inc word [si+IMG_Y]
    mov ax, [si+IMG_Y]
    cmp ax, [si+IMG_H]
    jb .row
    jmp .out
.depth:
    mov word [si+IMG_ERR], IMG_E_DEPTH
    jmp .out
.dim:
    mov word [si+IMG_ERR], IMG_E_DIM
    jmp .out
.trunc:
    mov word [si+IMG_ERR], IMG_E_TRUNC
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; img_pcxbyte - the next decoded byte of the stream, in AL. CF=1 = the file
; ended first. Preserves everything else.
;
; "If the top two bits are set, the remaining six bits in the byte show how
; many times to duplicate the next byte in the file. If the top two bits are
; not set, the first byte is the data itself, with a count of one."
; -----------------------------------------------------------------------------
img_pcxbyte:
    push bx
    cmp word [si+IMG_RUNC], 0          ; still owed some of a run?
    je .fresh
    dec word [si+IMG_RUNC]
    mov ax, [si+IMG_RUNV]
    clc
    jmp .out
.fresh:
    mov bx, [si+IMG_P]
    cmp bx, [si+IMG_SRCLEN]
    jae .end
    mov al, [es:bx]
    inc bx
    mov [si+IMG_P], bx
    and al, 0xC0
    cmp al, 0xC0                       ; both top bits, and only both
    jne .literal
    mov al, [es:bx-1]
    and al, 0x3F                       ; 1..63; a count of 0 is a corrupt file
    xor ah, ah                         ; and would spin here forever
    or ax, ax
    jz .end
    cmp bx, [si+IMG_SRCLEN]
    jae .end
    dec ax                             ; this call returns the first of them
    mov [si+IMG_RUNC], ax
    mov al, [es:bx]
    xor ah, ah
    mov [si+IMG_RUNV], ax
    inc bx
    mov [si+IMG_P], bx
    clc
    jmp .out
.literal:
    mov al, [es:bx-1]
    xor ah, ah
    clc
    jmp .out
.end:
    stc
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; img_row_planes - the raw half holds one scanline as IMG_NPL 1-bit planes of
; IMG_BPL bytes each; build the packed half.
;
; Plane p contributes BIT p of every pixel's index - EGA's own arrangement, so
; four 1-bit planes are a 4-bit colour with nothing lost. ONE plane gives 0 or
; 1, and that case is not only PCX's: a 1bpp BMP row is exactly one 1-bit
; plane, MSB leftmost, so img_bmp sets IMG_NPL=1 and IMG_BPL=IMG_SBPR and
; arrives here. The result goes through IMG_PAL like every other path.
;
; This is the slow routine in the file, and it is slow on purpose: a per-pixel
; loop with one test per plane. Twiddling eight pixels at a time is possible
; and would fold the plane count, the width remainder and the palette map into
; one unrolled path - the sort of routine that works on the picture you tested
; and drops a column on the picture you did not.
; -----------------------------------------------------------------------------
img_row_planes:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov di, [si+IMG_ROWBUF]
    add di, OS88IMG_RAW                ; DI = the packed row being built
    xor bp, bp                         ; BP = x, the pixel being assembled
.pixel:
    mov ax, bp                         ; which byte of a plane holds it...
    mov cx, ax
    and cx, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [si+IMG_BPOS], ax
    mov al, 0x80                       ; ...and which bit. Bit 7 is LEFTMOST,
.mask:                                 ; so the mask walks down from it
    or cx, cx
    jz .masked
    shr al, 1
    dec cx
    jmp .mask
.masked:
    mov [si+IMG_MASK], al
    xor dl, dl                         ; DL = the index being gathered
    mov dh, 1                          ; DH = the bit this plane contributes
    xor cx, cx
.plane:
    mov ax, cx
    push dx
    mul word [si+IMG_BPL]              ; DX:AX = the plane's first byte. `mul`
    pop dx                             ; writes DX and DX is carrying the two
    add ax, [si+IMG_BPOS]              ; halves of the answer
    add ax, [si+IMG_ROWBUF]
    mov bx, ax
    mov al, [bx]
    and al, [si+IMG_MASK]
    jz .noplane
    or dl, dh
.noplane:
    shl dh, 1
    inc cx
    cmp cx, [si+IMG_NPL]
    jb .plane
    mov bx, si                         ; the index, through the palette map
    add bx, IMG_PAL
    xor dh, dh
    add bx, dx
    mov al, [bx]
    test bp, 1
    jnz .odd
    shl al, 1                          ; an even x is the HIGH nibble, and
    shl al, 1                          ; waits for its partner
    shl al, 1
    shl al, 1
    mov [si+IMG_ACC], al
    jmp .next
.odd:
    or al, [si+IMG_ACC]
    mov [di], al
    inc di
.next:
    inc bp
    cmp bp, [si+IMG_W]
    jb .pixel
    test word [si+IMG_W], 1            ; an odd width leaves one pixel in the
    jz .done                           ; high nibble with no partner, and the
    mov al, [si+IMG_ACC]               ; stride (w+1)/2 has the room for it
    mov [di], al
.done:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%endif  ; OS88IMG_CONSTS_ONLY
