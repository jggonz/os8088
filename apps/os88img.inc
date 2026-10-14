; =============================================================================
; os88img.inc - picture decoders: .PIX, .BMP and .PCX into packed 4bpp
; SPEC.md 94. Include LAST, after os88ui.inc and before OS88_BSS, exactly as
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
; one that does not. SCRIBE reaches this through SCRIBE.OVL; PAINT would
; reach it resident. For the same reason IMG_ERR is a NUMBER and not a string pointer:
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
IMG_RUNC     equ 34                ; byte: PCX run bytes still owed (<= 62)
IMG_RUNV     equ 35                ; byte: ...and the value being repeated
IMG_PI       equ 36                ; word: img_palmap's source entry
IMG_BESTI    equ 38                ; byte: ...its closest os8088 colour so far
IMG_ACC      equ 39                ; byte: img_row_planes' half-built output
IMG_SRGB     equ 40                ; 3 bytes: the source triple being matched
IMG_PAL      equ 44                ; 16 bytes: source index -> os8088 colour
IMG_BPP      equ 60                ; word: bits per pixel, PER PLANE: 1 or 4.
                                   ; the whole choice of row routine, and the
                                   ; only thing that differs between a 1bpp
                                   ; BMP and a 4bpp one once the header is read
IMG_NPAL     equ 62                ; word: how many palette entries the file
                                   ; actually carries. 16 everywhere except a
                                   ; 1bpp BMP, which carries TWO - and reading
                                   ; sixteen out of a small one walks off the
                                   ; end of the file
OS88IMG_SZ   equ 64                ; 76 until the second shared apps pass: the
                                   ; best distance, a plane's byte and its
                                   ; mask live in registers now, and the run
                                   ; count and value were only ever bytes

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
; THE HELPERS BELOW img_load ARE CALLER-SAVES. img_load is the only entry and
; it banks AX-DI, BP and ES once; everything under it clobbers what its own
; header says and keeps SI (the block) and ES (the file) - which is all any of
; their callers carries across a call, because every decoder loop reloads its
; registers out of the block at the top of each row. It used to be 126 single
; pushes and pops, every helper saving everything for callers that saved it
; again, and a per-row helper paying for it on every row of the picture.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; img_load - decode the file in [SI+IMG_SRCSEG] into [SI+IMG_DSTSEG].
; in:  SI = the block, its six input words filled
; out: CF=0 and IMG_W/IMG_H/IMG_STRIDE set; CF=1 and IMG_ERR names the reason.
;      SI preserved; every other register is the caller's to save.
;
; The format is decided by the file's own first bytes and never by its name.
; A .PCX called .BMP is a picture; a name is a hint (54).
;
; IMG_ERR is zeroed as a WORD here and every refusal below stores a BYTE into
; it: the high byte is therefore 0 on every path, which is what lets each
; store be one byte shorter. W/H/STRIDE are not zeroed: img_setgeom stores all
; three on every path that reaches a picture and .fail clears all three on
; every path that does not, so a zero here was overwritten either way.
; -----------------------------------------------------------------------------
img_load:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    push es
    cld                                ; img_rowout's movsb, img_palmap's lodsb
    xor ax, ax
    mov [si+IMG_ERR], ax
    mov word [si+IMG_BPP], 4           ; what every path that does not say
    inc ax                             ; otherwise gets: one plane of 4-bit
    mov [si+IMG_NPL], ax               ; pixels and a full sixteen colours
    mov word [si+IMG_NPAL], 16
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
    mov ax, [es:0]
    cmp ax, 'O' | ('8' << 8)           ; 'O8PIX' - the 61.7 archive
    jne .notpix
    cmp word [es:2], 'P' | ('I' << 8)
    jne .notpix
    cmp byte [es:4], 'X'
    jne .notpix
    call img_pix
    jmp short .done
.notpix:
    cmp ax, 'B' | ('M' << 8)
    jne .notbmp
    call img_bmp
    jmp short .done
.notbmp:
    cmp al, 10                         ; ZSoft's Manufacturer constant...
    jne .unknown
    cmp byte [es:2], 1                 ; ...and the only Encoding there is
    jne .unknown
    call img_pcx
.done:
    cmp byte [si+IMG_ERR], 0           ; CF=0 whatever it compares
    je .out
    jmp short .fail
.unknown:
    mov byte [si+IMG_ERR], IMG_E_WHAT
    jmp short .fail
.short:
    mov byte [si+IMG_ERR], IMG_E_SHORT
.fail:
    xor ax, ax                         ; A REFUSAL LEAVES NO GEOMETRY BEHIND.
    mov [si+IMG_W], ax                 ; img_setgeom stores all three before the
    mov [si+IMG_H], ax                 ; pixel data has been proved to be inside
    mov [si+IMG_STRIDE], ax            ; the file, so a truncated PCX refuses
    stc                                ; with a complete, plausible size in the
                                       ; block - and a caller that reads IMG_W
                                       ; as "there is a picture" draws half of
                                       ; one and half of a stale claim
.out:
    pop es
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; img_setgeom - AX = width, BX = height are known. Check them, derive the
; destination stride, and prove the whole picture fits what the caller
; reserved. out: CF=1 and IMG_ERR set on refusal. Clobbers AX, BX, CX, DX.
; -----------------------------------------------------------------------------
img_setgeom:
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
    jcxz .fits                         ; 0 means the whole 64KB
    cmp ax, cx
    ja .big
.fits:
    clc
    ret
.big:
    mov byte [si+IMG_ERR], IMG_E_BIG
    stc
    ret
.bad:
    mov byte [si+IMG_ERR], IMG_E_DIM
    stc
    ret

; -----------------------------------------------------------------------------
; img_rowout - the packed half of the caller's row buffer holds [IMG_STRIDE]
; bytes; write them into the destination as row [IMG_Y], then step IMG_Y.
; out: CF=1 (JB) while rows remain. Keeps SI and ES; clobbers AX-DI.
;
; IMG_FLIP inverts the row number rather than the loop, so a bottom-up BMP and
; a top-down one share one reader and differ in one subtraction. The step and
; the test are here and not in the three row loops because all three ended
; in the same three instructions.
; -----------------------------------------------------------------------------
img_rowout:
    mov ax, [si+IMG_Y]
    cmp byte [si+IMG_FLIP], 0          ; 0 or 1, stored as a word
    je .down
    not ax                             ; H-1-Y, as H + (-Y-1)
    add ax, [si+IMG_H]
.down:
    mov cx, [si+IMG_STRIDE]
    mul cx                             ; DX:AX = the row's offset; img_setgeom
    xchg di, ax                        ; already proved the product fits
    push es
    push si
    mov es, [si+IMG_DSTSEG]
    mov si, [si+IMG_ROWBUF]
    add si, OS88IMG_RAW
    rep movsb
    pop si
    pop es
    inc word [si+IMG_Y]
    mov ax, [si+IMG_Y]
    cmp ax, [si+IMG_H]
    ret

; -----------------------------------------------------------------------------
; img_copy - [IMG_P] onward in the FILE, CX bytes into DS:DI; then IMG_P steps
; on by the SOURCE stride IMG_SBPR, so a padded row steps over its own padding
; whatever CX took. The .PIX and .BMP row loops' shared first half. Clobbers
; AL, BX, CX, DI.
; -----------------------------------------------------------------------------
img_copy:
    mov bx, [si+IMG_P]
.c:
    mov al, [es:bx]                    ; NOT movsb: the source is ES and the
    mov [di], al                       ; destination DS, the one pairing the
    inc bx                             ; string instructions cannot spell
    inc di
    loop .c
    mov ax, [si+IMG_SBPR]
    add [si+IMG_P], ax
    ret

; -----------------------------------------------------------------------------
; img_rowconv - the raw half of the caller's row buffer holds one SOURCE row;
; build the packed half from it. Clobbers AX-DI, as the row routines do.
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
;
; img_rowmask follows it in the file and both arms end in it.
; -----------------------------------------------------------------------------
img_rowconv:
    cmp byte [si+IMG_BPP], 4           ; 1 or 4, stored as a word
    jne .planes
    call img_row_nib
    jmp short img_rowmask
.planes:
    call img_row_planes                ; ...and on into img_rowmask

; -----------------------------------------------------------------------------
; img_rowmask - AN ODD WIDTH LEAVES THE LAST LOW NIBBLE OF THE PACKED ROW
; UNUSED, AND IT IS ZERO. Clobbers BX.
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
    test byte [si+IMG_W], 1
    jz .out
    mov bx, [si+IMG_ROWBUF]
    add bx, [si+IMG_STRIDE]
    and byte [bx+OS88IMG_RAW-1], 0xF0
.out:
    ret

; -----------------------------------------------------------------------------
; img_palmap - fill [SI+IMG_PAL] with, for each of [SI+IMG_NPAL] source RGB
; triples at ES:DI, the nearest os8088 colour. DL = 0 for a PCX's R,G,B
; triples or 1 for a BMP's B,G,R,reserved quads. Clobbers AX-DI and BP.
;
; The table is sixteen entries whatever the file carries, and the entries the
; file does NOT carry are left as the IDENTITY rather than as zero. A 1bpp BMP
; can only ever produce index 0 or 1, so the tail is unreachable through the
; pixels - but it is reachable through a corrupt one, and a defined answer
; there is a wrong colour instead of a whole picture in black.
;
; THE TWO ORDERS ARE ONE ARM: they agree about the middle byte and differ in
; which end is red, so the entry is read once and the ends are exchanged for a
; BMP. The candidate walk then steps BX by three through the table - a channel
; at a time, so no `mul` finds a triple - and the two formats differ again only
; in the size of an entry, DX (0 or 1) on top of three.
; -----------------------------------------------------------------------------
img_palmap:
    call img_palident                  ; the entries the file does not carry
    mov dh, 0
    mov word [si+IMG_PI], 0
.entry:
    push dx
    mov al, [es:di]                    ; R,G,B - or B,G,R,reserved
    mov ah, [es:di+1]
    mov cl, [es:di+2]
    or dl, dl
    jz .rgb
    xchg al, cl                        ; a BMP's red is at the other end
.rgb:
    mov [si+IMG_SRGB], ax              ; R and G
    mov [si+IMG_SRGB+2], cl            ; ...and B
    mov bp, 0xFFFF                     ; BP = the best distance so far
    xor ax, ax                         ; AH stays 0 for every `add cx, ax`
    xor bx, bx                         ; BX = the candidate's triple
    cwd                                ; DL = the candidate, DH = 0
.cand:
    push si
    add si, IMG_SRGB
    xor cx, cx                         ; CX = |dR|+|dG|+|dB|, under 766
    mov dh, 3
.chan:
    lodsb                              ; IMG_PAL8088 IS READ THROUGH CS. It is
    sub al, [cs:bx+img_pal8088]        ; a table in this file's own IMAGE, and
    jnc .pos                           ; if this include is built into an
    neg al                             ; overlay that image is not the one DS
.pos:                                  ; points at (93.2)
    add cx, ax
    inc bx
    dec dh
    jnz .chan
    pop si
    cmp cx, bp
    jae .next
    mov bp, cx
    mov [si+IMG_BESTI], dl
.next:
    inc dx                             ; DH is 0 again: this is DL
    cmp dl, 16
    jb .cand
    mov bx, [si+IMG_PI]
    mov al, [si+IMG_BESTI]
    mov [bx+si+IMG_PAL], al
    pop dx
    add di, 3                          ; three bytes an entry, or four
    add di, dx
    inc bx
    mov [si+IMG_PI], bx
    cmp bx, [si+IMG_NPAL]
    jb .entry
    ret

; img_palident - no palette in the file: source index IS an os8088 colour.
; .PIX blocks are already written in our own indices (61.7), so this is what
; they get, and it is also the identity a BMP written by this tree lands on.
; Clobbers BX.
img_palident:
    mov bx, 15
.l:
    mov [bx+si+IMG_PAL], bl
    dec bx
    jns .l
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
;
; Every exit is a bare `ret` now and the error stores share one tail, which
; brought img_setgeom's `jc .out` from exactly +127 - the last distance a short
; jump reaches, so NASM emitted it short or as a near pair according to the
; includer's layout - to +80, short in every includer.
; =============================================================================
img_pix:
    cmp byte [es:5], 1                 ; ZP_FMT_VER. A different version is
    jne .ver                           ; refused whole rather than read hopefully
    cmp word [es:10], 16               ; the entry size is IN the file so that a
    jne .ver                           ; future tool that grows it is refused
    mov cx, [es:6]                     ; here instead of misread here
    jcxz .nopic
    mov di, 16                         ; the directory starts after the header
    mov ax, cx
    mov bx, 16
    mul bx                             ; DX:AX = its length
    or dx, dx
    jnz .trunc
    add ax, bx
    jc .trunc
    cmp ax, [si+IMG_SRCLEN]
    ja .trunc
.scan:
    mov ax, [si+IMG_PICNO]
    or ax, ax
    je .found                          ; 0 = whichever is first
    cmp ax, [es:di]
    je .found
    add di, bx
    loop .scan
.nopic:
    mov al, IMG_E_NOPIC
    jmp short .err
.ver:
    mov al, IMG_E_VER
    jmp short .err
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
    xchg ax, cx                        ; row it claims to
    mul word [si+IMG_H]                ; DX:AX = the block. The tool refuses
    or dx, dx                          ; one over 64KB and so does zpic.inc,
    jnz .trunc                         ; so this agrees with both
    add ax, [si+IMG_P]
    jc .trunc
    cmp ax, [si+IMG_SRCLEN]
    ja .trunc
    call img_palident                  ; already our own indices
    mov [si+IMG_FLIP], dx              ; DX is 0 from the multiply
    mov [si+IMG_Y], dx
.row:
    mov di, [si+IMG_ROWBUF]
    add di, OS88IMG_RAW                ; straight into the packed half: there
    mov cx, [si+IMG_STRIDE]            ; is nothing to convert
    call img_copy
    call img_rowmask                   ; the one format that does not convert
    call img_rowout                    ; still owes the output format its rule
    jb .row
    ret
.trunc:
    mov al, IMG_E_TRUNC
.err:
    mov [si+IMG_ERR], al
.out:
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
    mov bx, [es:22]                    ; biHeight, low word
    mov cx, [es:24]                    ; biHeight is SIGNED and 32-bit, so its
    jcxz .up                           ; sign is in the HIGH word - testing
    inc cx                             ; [es:22] tests the height itself, which
    jnz .trunc                         ; is nonzero for every real picture, and
    neg bx                             ; sent every BOTTOM-UP file down the
    jmp short .geom                    ; top-down path. A 32-bit negative small
                                       ; enough to be a height is FFFF:xxxx and
                                       ; anything else is a height over 65535;
                                       ; a NEGATIVE height is top-down, and CX
.up:                                   ; is 0 for it. A positive one is
    inc cx                             ; bottom-up: FLIP = 1
.geom:
    mov [si+IMG_FLIP], cx
    call img_setgeom
    jc .out
    mov ax, [si+IMG_W]                 ; the SOURCE row, rounded up to a
    mov cl, 1                          ; multiple of FOUR BYTES - the BMP rule,
    cmp byte [si+IMG_BPP], 1           ; not ours, and it is padding whichever
    jne .sbpr                          ; depth it is. 4bpp: two pixels a byte,
    add ax, 6                          ; 1bpp: eight - which is the same round-
    mov cl, 3                          ; up by 1 and by 7 and the same shift
.sbpr:                                 ; by 1 and by 3
    inc ax
    shr ax, cl
    add ax, 3
    and al, 0xFC
    mov [si+IMG_SBPR], ax
    mov [si+IMG_BPL], ax               ; ONE plane, so the plane stride IS the
    cmp ax, OS88IMG_RAW                ; row - which is the whole of what lets
    ja .dim                            ; a 1bpp row reach img_row_planes
    mov cx, 16                         ; a 1bpp file carries TWO entries, and
    cmp byte [si+IMG_BPP], 1           ; reading sixteen out of one walks into
    jne .havenpal                      ; its pixels - or off the end of a small
    mov cl, 2                          ; file, which is the same bug in a hat
.havenpal:
    mov [si+IMG_NPAL], cx
    shl cx, 1                          ; four bytes an entry
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
    mul word [si+IMG_H]                ; DX:AX = every source row
    or dx, dx
    jnz .trunc
    add ax, [si+IMG_P]
    jc .trunc
    cmp ax, [si+IMG_SRCLEN]
    ja .trunc
    mov [si+IMG_Y], dx
.row:
    mov cx, [si+IMG_SBPR]              ; one source row into the raw half...
    mov di, [si+IMG_ROWBUF]
    call img_copy
    call img_rowconv                   ; ...and through the palette map
    call img_rowout
    jb .row
    ret
.depth:
    mov al, IMG_E_DEPTH
    jmp short .err
.comp:
    mov al, IMG_E_COMP
    jmp short .err
.dim:
    mov al, IMG_E_DIM
    jmp short .err
.trunc:
    mov al, IMG_E_TRUNC
.err:
    mov [si+IMG_ERR], al
.out:
    ret

; -----------------------------------------------------------------------------
; img_row_nib - the raw half holds one row of 4-bit pixels, two to a byte with
; the high nibble leftmost; build the packed half with every nibble mapped
; through IMG_PAL. Clobbers AX, BX, CX, DX, DI.
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
    push si
    mov cx, [si+IMG_STRIDE]
    lea bx, [si+IMG_PAL]               ; BX = XLAT's table, computed while SI
    mov si, [si+IMG_ROWBUF]            ; is still the block
    lea di, [si+OS88IMG_RAW]
.b:
    lodsb
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
    pop si
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
    cmp word [si+IMG_SRCLEN], 128      ; the whole PCX header - which is where
    jb .trunc                          ; the sixteen-colour palette lives, so
                                       ; there is no shorter legal one
    xor ax, ax
    mov al, [es:3]                     ; BitsPerPixel, PER PLANE
    mov [si+IMG_BPP], ax
    mov al, [es:65]                    ; NPlanes
    mov [si+IMG_NPL], ax
    cmp byte [si+IMG_BPP], 1           ; (AH is 0: both are bytes in the file)
    je .onebit
    cmp byte [si+IMG_BPP], 4           ; 4 bits in ONE plane: the other legal
    jne .depth                         ; sixteen-colour spelling, and already
    cmp al, 1                          ; the layout we want. 4 bits in FOUR
    jne .depth                         ; planes would be 16-bit colour
    jmp short .npok
.onebit:
    cmp al, 1                          ; one 1-bit plane is monochrome...
    je .npok
    cmp al, 4                          ; ...and four ARE a 4-bit index
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
    test al, 1
    jnz .dim                           ; "MUST be an EVEN number"
    mov [si+IMG_BPL], ax
    mov cx, [si+IMG_W]                 ; ...and it must still cover the width
    cmp byte [si+IMG_BPP], 1           ; it claims, which is a different number
    jne .min4                          ; at each depth
    add cx, 7                          ; 1bpp: eight pixels a byte
    shr cx, 1
    shr cx, 1
    shr cx, 1
    jmp short .havemin
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
    jmp short .havepal                 ; which is the outcome the palette pass
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
    xor ax, ax
    mov [si+IMG_FLIP], ax              ; PCX is top-down
    mov [si+IMG_Y], ax
.row:
    mov byte [si+IMG_RUNC], 0          ; a run never spans a scan line
    mov cx, [si+IMG_TOT]
    mov di, [si+IMG_ROWBUF]
.fill:
    call img_pcxbyte                   ; -> AL, or CF=1 at the end of the file
    jc .trunc
    mov [di], al
    inc di
    loop .fill
    call img_rowconv
    call img_rowout
    jb .row
    ret
.depth:
    mov al, IMG_E_DEPTH
    jmp short .err
.dim:
    mov al, IMG_E_DIM
    jmp short .err
.trunc:
    mov al, IMG_E_TRUNC
.err:
    mov [si+IMG_ERR], al
.out:
    ret

; -----------------------------------------------------------------------------
; img_pcxbyte - the next decoded byte of the stream, in AL. CF=1 = the file
; ended first. Clobbers AH and BX.
;
; "If the top two bits are set, the remaining six bits in the byte show how
; many times to duplicate the next byte in the file. If the top two bits are
; not set, the first byte is the data itself, with a count of one."
;
; IMG_RUNC and IMG_RUNV are reached as BYTES only: a count is at most 62 and a
; value is a byte, so neither word's high byte is ever read.
; -----------------------------------------------------------------------------
img_pcxbyte:
    cmp byte [si+IMG_RUNC], 0          ; still owed some of a run?
    je .fresh                          ; (CF=0 whatever it compares, and `dec`
    dec byte [si+IMG_RUNC]             ; leaves CF alone)
    mov al, [si+IMG_RUNV]
    ret
.fresh:
    mov bx, [si+IMG_P]
    cmp bx, [si+IMG_SRCLEN]
    jae .end
    mov al, [es:bx]
    inc bx
    cmp al, 0xC0                       ; both top bits, and only both
    jb .lit
    and al, 0x3F                       ; 1..63; a count of 0 is a corrupt file
    jz .end                            ; and would spin here forever
    cmp bx, [si+IMG_SRCLEN]
    jae .end
    dec ax                             ; this call returns the first of them
    mov [si+IMG_RUNC], al
    mov al, [es:bx]
    inc bx
    mov [si+IMG_RUNV], al
.lit:
    mov [si+IMG_P], bx
    clc
    ret
.end:
    stc
    ret

; -----------------------------------------------------------------------------
; img_row_planes - the raw half holds one scanline as IMG_NPL 1-bit planes of
; IMG_BPL bytes each; build the packed half. Clobbers AX, BX, CX, DX, DI, BP.
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
; and drops a column on the picture you did not. What it does NOT do any more
; is MULTIPLY per plane per pixel to find the plane: the planes are IMG_BPL
; apart, so the byte steps down by that much instead, and the bit's mask is
; one shift by CL rather than a loop of them.
; -----------------------------------------------------------------------------
img_row_planes:
    mov di, [si+IMG_ROWBUF]
    add di, OS88IMG_RAW                ; DI = the packed row being built
    xor bp, bp                         ; BP = x, the pixel being assembled
.pixel:
    mov bx, bp                         ; which byte of a plane holds it...
    mov cl, bl
    and cl, 7
    mov ah, 0x80                       ; ...and which bit. Bit 7 is LEFTMOST,
    shr ah, cl                         ; so the mask walks down from it
    shr bx, 1
    shr bx, 1
    shr bx, 1
    add bx, [si+IMG_ROWBUF]            ; BX = that byte in plane 0
    xor dx, dx                         ; DL = the index being gathered
    inc dh                             ; DH = the bit this plane contributes
    mov cx, [si+IMG_NPL]
.plane:
    test [bx], ah
    jz .noplane
    or dl, dh
.noplane:
    shl dh, 1
    add bx, [si+IMG_BPL]               ; the same byte, one plane on
    loop .plane
    mov bl, dl                         ; the index, through the palette map
    mov bh, 0
    mov al, [bx+si+IMG_PAL]
    test bp, 1
    jnz .odd
    mov cl, 4                          ; an even x is the HIGH nibble, and
    shl al, cl                         ; waits for its partner
    mov [si+IMG_ACC], al
    jmp short .next
.odd:
    or al, [si+IMG_ACC]
    mov [di], al
    inc di
.next:
    inc bp
    cmp bp, [si+IMG_W]
    jb .pixel
    test byte [si+IMG_W], 1            ; an odd width leaves one pixel in the
    jz .done                           ; high nibble with no partner, and the
    mov al, [si+IMG_ACC]               ; stride (w+1)/2 has the room for it
    mov [di], al
.done:
    ret

%endif  ; OS88IMG_CONSTS_ONLY
