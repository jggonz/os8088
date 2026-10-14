; =============================================================================
; apps/isotron/itcanvas.inc - the picture (SPEC.md 108.9, 108.10)
;
; ONE adapter-independent 320 x 200 picture of 4-bit SURFACE IDS, in a claim
; of its own (it_cseg). A pixel is a surface - floor, rim, a player's wall
; face - and never a colour: every presenter (itwin.inc windowed, wave 3's
; full screen) turns an id into colour or pattern through its own look-up
; table, so a look change and a window / full-screen toggle are a PRESENT and
; never a redraw of the game. tools/isotronsim.py's Canvas is the same picture
; and is normative: tests/isotron.py compares the two byte for byte.
;
; ONE BYTE A PIXEL, COLUMN-MAJOR: pixel (x, y) is byte x * 200 + y, 64,000
; bytes; an EVEN column's id sits in the byte's HIGH nibble and an ODD
; column's in its LOW nibble, so the OR of a column pair's two bytes is the
; packed canvas byte of ISOTRON-PLAN 6.1 (high nibble = even x) and a
; presenter's look-up table takes it as it is. What the layout buys is THE
; STEP: a wall is one column, and a column is consecutive bytes, so its
; face is ONE `rep stosb` (~10 cycles a row on the 8088) where a packed
; nibble canvas was a read-modify-write a row (~50) - SPEC.md 108.10.3 has
; the measured step. What it costs is the presenters' reads, four bytes
; 200 apart a device byte (itwin.inc), paid per presented byte and not per
; step.
;
; THE STEP (it_span) is the whole of the per-step drawing, and SPEC.md 108.10
; is its proof: a new cell's wall is one column, x = u + d, and what of it is
; visible is min(k*, 23) rows from its top pixel, k* being the first occupied
; cell on its front diagonal (u+k, d-k), k = 1..22 - the outline ring
; included, which is what clips a wall at the front and right edges with no
; arithmetic of its own. Nothing already drawn is ever redrawn.
;
; Register contract: DS = the package, ES = the work claim (it_seg: the grid),
; unless a routine says.
; =============================================================================

IT_CCOL     equ 200                 ; bytes a canvas column: the rows
IT_CROWS    equ 200
IT_CCOLS    equ 320
IT_CSIZE    equ IT_CCOL * IT_CCOLS  ; 64,000
IT_WALLH    equ 23                  ; a wall: the top pixel and 22 face rows
%ifdef IT_SCAN21                    ; THE NEGATIVE CONTROL's guest (SPEC.md
IT_PROBES   equ 21                  ; 108.10.2): isotronbound assembles a
%else                               ; package with the scan one row short and
IT_PROBES   equ 22                  ; requires it to differ from the model -
%endif                              ; the probe never looks further (108.10)
IT_YFRONT   equ 174                 ; the front outline's row at level 1
IT_LEVELDY  equ 24                  ; a tower level is 24 rows above the last
IT_SLABX1   equ 316                 ; the slab's last column
; A SPAN STAYS IN ITS COLUMN, by construction (SPEC.md 108.10.2): a cell's
; top pixel is at y = ybase - d with d >= 1 (the ring is d = 0), so at most
; 173, and its span ends at y + k* <= 173 + 23 = 196 - never past row 199
; into the next canvas column. Nothing needs counting for that; this does.
%if IT_YFRONT - 1 + IT_WALLH > IT_CROWS
  %error "a step's span could run past row 199 into the next canvas column"
%endif

; the surface ids (SPEC.md 108.9.1)
ID_VOID     equ 0
ID_FLOOR    equ 1
ID_FLOORDOT equ 2
ID_RIM      equ 3
ID_EDGE     equ 4
ID_SLABF    equ 5
ID_SLABR    equ 6
ID_PFRONT   equ 7
ID_PSIDE    equ 8
ID_TOP      equ 9                   ; EVERY wall's top pixel, whoever's: the one
                                    ; byte value it_span scans its column for
ID_CFRONT   equ 10
ID_CSIDE    equ 11
ID_LOGOF    equ 13
ID_FLASH    equ 15                  ; (12 and 14 are free)
C_LOGO      equ 0x30                ; a title letter's grid cell (owner 11)

; -----------------------------------------------------------------------------
; it_canvas_init - it_xcol[x] = x * 200, once (the step's column base and
; every presenter's). Preserves everything.
; -----------------------------------------------------------------------------
it_canvas_init:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, it_xcol
    xor ax, ax
    mov cx, IT_CCOLS
    cld
.l:
    stosw
    add ax, IT_CCOL
    loop .l
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_rebuild - canvas_rebuild(h), AL = h (SPEC.md 108.9.2): the canvas VOID,
; the LOWER BODIES of levels 1..h-1, then level h whole; it_ybase is set for
; the walls of level h. A new arena, a raise and a reset are all this one
; call, and it erases the last round's walls with it. Clobbers AX..DI, BP.
;
; ONE PASS, EVERY BYTE ONCE, IN CANVAS ORDER: the canvas is column-major and
; in column x every part of every level is ONE interval of rows, and they come
; DOWN the column in level order - level h's top face, its lower body, then
; the lower bodies of h-1 .. 1 (the one-row gap between two levels and what
; lies above and below the stack being VOID). So a cursor (DI) walks the
; 64,000 bytes once: VOID up to a part, the part, and on; no separate clear,
; and nothing written twice but the outline's shared corner pixels, which the
; cursor skips. The x-dependent bounds are made once a column, the lower body's
; once for all its levels (a level is the same rows 24 further down), and the
; ids come pre-shifted for the column's nibble. The parts are the same pixel
; sets as isotronsim.level_parts, which draws them ROW by row: two derivations
; of one slab, held byte for byte by tests/isotron.py.
;
; MEASURED (MartyPC's 5150, isotronperf): see SPEC.md 108.9.2. The first
; version - a 64,000-byte clear, then each level a column at a time through a
; called run, its ids shifted per run - was 1.84M cycles at h = 1 and 2.94M
; at h = 4 (0.39 / 0.62 s).
; -----------------------------------------------------------------------------

; RB_RUN id - level rows SI..DX (inclusive, signed; DX < SI is nothing) of the
; column at [it_rbcb] (the byte of its row t = 0): VOID from the cursor DI up
; to the part, then the part in `id` (an operand: a pre-shifted byte), word
; stores. A part that starts above the cursor (a shared corner) starts at it.
; Clobbers AX, CX, SI, DX; DI is the cursor.
%macro RB_RUN 1
    cmp dx, si
    jl %%out
    mov ax, [it_rbcb]
    add si, ax                      ; SI = the part's first byte
    add dx, ax
    inc dx                          ; DX = the byte after it
    mov cx, si
    sub cx, di
    jbe %%p                         ; nothing above it to clear
    xor ax, ax                      ; VOID (0 in either nibble)
    shr cx, 1
    rep stosw
    adc cx, cx
    rep stosb
%%p:
    mov cx, dx
    sub cx, di
    jbe %%out                       ; (all of it under the cursor already)
    mov al, %1
    mov ah, al
    shr cx, 1
    rep stosw
    adc cx, cx
    rep stosb
%%out:
%endmacro

it_rebuild:
    IT_MARK it_rb0
    mov word [it_spanhk], it_span_box ; a new picture's own spans are boxes
    push es                         ; until it is presented whole (itfs.inc)
    mov [it_rbh], al
    mov ah, IT_LEVELDY              ; o = 24(h - 1), level h's offset, and
    dec al                          ; it_ybase = 174 - o
    mul ah
    mov [it_rbo], ax
    mov bl, IT_YFRONT
    sub bl, al
    mov [it_ybase], bl
    push ds
    pop es
    mov di, it_ctop                 ; no TOP in any column yet (it_span)
    mov cx, IT_CCOLS / 2
    xor ax, ax
    cld
    rep stosw
    mov es, [it_cseg]               ; ES = the canvas
    xor di, di                      ; DI = THE CURSOR
    xor bp, bp                      ; BP = x
.col:
    mov bx, bp
    shl bx, 1
    mov ax, [it_xcol + bx]
    mov [it_rbcol], ax              ; the column's row-0 byte
    sub ax, [it_rbo]
    mov [it_rbcb], ax               ; ...and level h's row t = 0 byte
    mov bx, it_rbide                ; BX = the ids, shifted for the column:
    test bp, 1                      ; an even column's in the high nibble
    jz .ev
    mov bx, it_rbido
.ev:
    mov al, [bx + ID_EDGE]          ; LEVEL h's OUTLINE, or VOID when the sweep
    cmp byte [it_rbnoe], 0          ; writes it last (it_rbnoe, SPEC.md
    je .eo                          ; 108.13.3): its four edges, the top face's
    xor al, al                      ; two and the lower body's two
.eo:
    mov [it_rbedge], al
    ; --- level h's top face (whole only), in row order ----------------------
    mov si, 74                      ; the back outline, t = 74 (x >= 100) ...
    cmp bp, 100
    jae .ol
    mov si, 174                     ; ... or the left one, t = 174 - x
    sub si, bp
.ol:
    mov dx, si
    RB_RUN [it_rbedge]
    mov si, 175                     ; the back rim: max(75, 175-x) ..
    sub si, bp                      ; min(96, 389-x)
    cmp si, 75
    jge .b0
    mov si, 75
.b0:
    mov dx, 389
    sub dx, bp
    cmp dx, 96
    jle .b1
    mov dx, 96
.b1:
    RB_RUN [bx + ID_RIM]
    mov si, 175                     ; the left rim: max(97, 175-x) ..
    sub si, bp                      ; min(173, 196-x)
    cmp si, 97
    jge .l0
    mov si, 97
.l0:
    mov dx, 196
    sub dx, bp
    cmp dx, 173
    jle .l1
    mov dx, 173
.l1:
    RB_RUN [bx + ID_RIM]
    mov si, 197                     ; the floor: max(97, 197-x) ..
    sub si, bp                      ; min(173, 389-x)
    cmp si, 97
    jge .f0
    mov si, 97
.f0:
    mov dx, 389
    sub dx, bp
    cmp dx, 173
    jle .f1
    mov dx, 173
.f1:
    cmp dx, si
    jl .lower
    RB_RUN [bx + ID_FLOOR]          ; FLOOR, then a FLOORDOT on every byte p
    mov ax, si                      ; of it with (x + p) & 3 = 0 - the
    add ax, bp                      ; pixel's (x + y) & 3 = 0, x * 200 being
    neg ax                          ; a multiple of 4
    and ax, 3
    add si, ax                      ; SI = the first dot, DX = the floor's end
    mov al, [bx + ID_FLOORDOT]
.dot:
    cmp si, dx
    jae .lower
    mov [es:si], al
    add si, 4
    jmp short .dot
.lower:
    ; --- the lower body's rows in a level, made once for every level -------
    cmp bp, 216
    ja .rgt
    mov word [it_rbe0], 174         ; the front outline, t = 174
    mov word [it_rbf0], 175         ; the front face, t = 175 .. 196 (x <= 215)
    mov word [it_rbf1], 196
    cmp bp, 215
    jbe .fid
    mov word [it_rbf1], 0           ; (x = 216: the outline alone)
.fid:
    mov al, [bx + ID_SLABF]
    jmp short .lv
.rgt:
    mov ax, 390                     ; the right outline, t = 390 - x
    sub ax, bp
    mov [it_rbe0], ax
    inc ax                          ; the right face: max(75, 391-x) ..
    cmp ax, 75                      ; min(196, 412-x)
    jge .r0
    mov ax, 75
.r0:
    mov [it_rbf0], ax
    mov ax, 412
    sub ax, bp
    cmp ax, 196
    jle .r1
    mov ax, 196
.r1:
    mov [it_rbf1], ax
    mov al, [bx + ID_SLABR]
.lv:
    mov [it_rbfid], al
    mov al, [it_rbh]
    mov [it_rbj], al                ; levels h .. 1, each 24 rows further down
    mov al, [it_rbedge]             ; (level h's outline, or VOID)
    mov [it_rbe], al
.lvl:
    mov si, [it_rbe0]
    mov dx, si
    RB_RUN [it_rbe]
    mov al, [bx + ID_EDGE]          ; every lower level's outline is drawn
    mov [it_rbe], al
    mov si, [it_rbf0]
    mov dx, [it_rbf1]
    RB_RUN [it_rbfid]
    add word [it_rbcb], IT_LEVELDY
    dec byte [it_rbj]
    jnz .lvl
    ; --- and VOID to the column's end ---------------------------------------
    mov cx, [it_rbcol]
    add cx, IT_CCOL
    sub cx, di
    xor ax, ax
    shr cx, 1
    rep stosw
    adc cx, cx
    rep stosb
    inc bp
    cmp bp, IT_SLABX1
    jbe .col
    mov cx, IT_CSIZE                ; columns 317 .. 319: VOID
    sub cx, di
    shr cx, 1
    rep stosw
    pop es
    IT_MARK it_rb1
    ret

; the slab's ids, pre-shifted: an even column's (high nibble), an odd one's
it_rbide:
    db ID_VOID << 4, ID_FLOOR << 4, ID_FLOORDOT << 4, ID_RIM << 4
    db ID_EDGE << 4, ID_SLABF << 4, ID_SLABR << 4
it_rbido:
    db ID_VOID, ID_FLOOR, ID_FLOORDOT, ID_RIM, ID_EDGE, ID_SLABF, ID_SLABR
%if (ID_VOID != 0) || (IT_CSIZE & 1)
  %error "it_rebuild clears with a 0 word: VOID must be id 0"
%endif

; -----------------------------------------------------------------------------
; it_outline - THE SWEEP'S LAST WRITE (SPEC.md 108.13.3): level h's outline,
; EDGE, into a canvas it_rebuild made with it_rbnoe - in column x the top
; face's edge (the back one at t = 74 from x = 100, the left one at
; t = 174 - x before it) and the lower body's (the front one at t = 174 to
; x = 216, the right one at t = 390 - x after it), at offset [it_swo]. The
; same pixels a plain rebuild writes there, so the canvas is then the
; model's byte for byte: no wall reaches an outline row (SPEC.md 108.9.3 -
; a wall stops at the ring's row). 317 columns, two byte stores each,
; ~25k cycles. Clobbers AX, BX, CX, DX, SI, DI.
; -----------------------------------------------------------------------------
it_outline:
    push es
    mov es, [it_cseg]
    xor si, si                      ; SI = x
.col:
    mov bx, si
    shl bx, 1
    mov di, [it_xcol + bx]
    sub di, [it_swo]                ; DI = the column's level row t = 0 byte
    mov al, ID_EDGE << 4            ; the column's nibble of EDGE
    test si, 1
    jz .ev
    mov al, ID_EDGE
.ev:
    mov bx, 74                      ; the top face's edge
    cmp si, 100
    jae .t
    mov bx, 174
    sub bx, si
.t:
    mov [es:di + bx], al
    mov bx, 174                     ; the lower body's
    cmp si, 216
    jbe .b
    mov bx, 390
    sub bx, si
.b:
    mov [es:di + bx], al
    inc si
    cmp si, IT_SLABX1
    jbe .col
    pop es
    ret

; -----------------------------------------------------------------------------
; it_title_build - the title picture (SPEC.md 108.9.4): the grid reset, level
; 1, and the word isoTron - BLOCK GLYPHS laid out ON THE GLASS (it_logo: 5 x
; 7 cells a letter, each cell IT_LOGOS x IT_LOGOS pixels) and turned into
; the floor cells whose top pixels they are: (x, y) is the top of cell
; d = 174 - y, u = x - d (SPEC.md 108.2). Each is a C_LOGO cell placed in the
; grid and drawn by it_span, the step's own renderer, so the letters'
; surfaces are wall TOPS - the glyph, white - and each letter's lower edges
; hang a LOGOF face 22 rows down, cut by the cells in front of it: raised
; block letters, read square-on, in the game's projection. Wave 2's
; letters were strokes laid along the floor's axes, and leaned with it;
; on the glass they read as blocks rather than as a word. ES = the claim.
; ~1,260 cells, one it_span each: ~0.25 s at the title. Clobbers AX..DI, BP.
; -----------------------------------------------------------------------------
IT_LOGOX    equ 98                  ; the word's left column and top row on
IT_LOGOY    equ 100                 ; the glass (level 1's floor holds it and
IT_LOGOS    equ 4                   ; its 22-row faces), and a cell's pixels
%if (IT_LOGOY + 7 * IT_LOGOS + IT_WALLH > IT_YFRONT) || (IT_LOGOX < 197 - IT_LOGOY)
  %error "the title's word does not stand on level 1's floor"
%endif

it_title_build:
    call it_grid_reset
    mov al, 1
    call it_rebuild
    mov si, it_logo
    mov word [it_lgx], IT_LOGOX     ; the letter's left column
.letter:
    lodsb                           ; its width in cells; 0 ends the word
    or al, al
    jz .done
    mov [it_lgw], al
    mov [it_lgp], si                ; its seven rows of bits
    mov word [it_lgy], IT_LOGOY     ; the cell row's top
.row:
    mov si, [it_lgp]
    lodsb
    mov [it_lgp], si
    mov [it_lgb], al                ; bit 7 the leftmost cell
    mov ax, [it_lgx]
    mov [it_lgc], ax                ; the cell's left column
.col:
    shl byte [it_lgb], 1
    jnc .nc
    mov ax, [it_lgy]                ; IT_LOGOS x IT_LOGOS pixels, each the
    mov [it_lgyy], ax               ; top of a floor cell
.by:
    mov ax, [it_lgc]
    mov [it_lgxx], ax
.bx:
    call it_logo_cell
    inc word [it_lgxx]
    mov ax, [it_lgxx]
    sub ax, [it_lgc]
    cmp ax, IT_LOGOS
    jb .bx
    inc word [it_lgyy]
    mov ax, [it_lgyy]
    sub ax, [it_lgy]
    cmp ax, IT_LOGOS
    jb .by
.nc:
    add word [it_lgc], IT_LOGOS
    cmp byte [it_lgb], 0            ; (a row's bits past its width are 0)
    jne .col
    add word [it_lgy], IT_LOGOS
    cmp word [it_lgy], IT_LOGOY + 7 * IT_LOGOS
    jb .row
    mov al, [it_lgw]                ; the next letter, a cell's gap on
    inc al
    mov ah, IT_LOGOS
    mul ah
    add [it_lgx], ax
    mov si, [it_lgp]
    jmp .letter
.done:
    ret

; it_logo_cell - the floor cell whose top pixel is (it_lgxx, it_lgyy), once
it_logo_cell:
    mov ax, IT_YFRONT
    sub ax, [it_lgyy]               ; AL = d
    mov dx, [it_lgxx]
    sub dx, ax                      ; DX = u
    mov bx, it_lhead
    mov [bx + H_U], dl
    mov [bx + H_D], al
    mov ah, GW
    mul ah
    add ax, dx
    mov si, ax                      ; SI = d * 217 + u
    cmp byte [es:si + IT_GRID], 0
    jne .x                          ; (never: a pixel is one cell)
    mov dh, C_LOGO
    mov [es:si + IT_GRID], dh
    call it_span
.x:
    ret

; -----------------------------------------------------------------------------
; it_span - ONE STEP'S PICTURE (SPEC.md 108.10). in: BX = the head record (its
; H_U / H_D are the new cell), DH = the cell's value; ES = the work claim.
; it_span_ud is the same with AL = u, AH = d already loaded (it_move's entry).
; Writes the cell's column - the top pixel and min(k*, 23) - 1 face rows
; below it - and grows the head's dirty box and the bound counters.
; ES must be it_seg on entry and is it_seg again on return. Preserves BX, DS,
; ES; clobbers AX, CX, DX, SI, DI, BP.
;
; k* COMES OFF THE CANVAS, NOT THE GRID (SPEC.md 108.10.1, Lemma 3): below a
; cell's top pixel its own column holds only faces of cells BEHIND it and the
; slab, until the TOP pixel of the nearest occupied cell in FRONT, k rows down
; - nothing behind can reach a pixel that low, and nothing further in front can
; reach one that high. So the first TOP byte in the column below is k*, and it
; is ONE `repne scasb` over at most 22 consecutive bytes (the canvas is
; column-major). The ring needs no byte: its distance is min(d, 216 - u), and
; the scan stops a row short of it. tools/isotronsim.py derives k* from the
; GRID (Lemma 2) and the guest from the CANVAS, so the canvas equality
; tests/isotron.py checks after every round holds the two derivations to each
; other.
;
; WHAT IT COSTS (MartyPC's 5150, isotronperf, SPEC.md 108.10.3): the scan is
; ~15 cycles a row where the grid's front diagonal was ~27 a cell; the wall
; is a store and one `rep stosb` down the column, ~10 a row.
; -----------------------------------------------------------------------------
it_span:
    mov ax, [bx + H_U]              ; AL = u, AH = d (H_D follows H_U)
it_span_ud:
    ; BX IS THE HEAD THROUGHOUT and SI the one scratch index, so nothing is
    ; saved: k* lives in DH once the cell value is spent, and the scan keeps
    ; its start in SI (SPEC.md 108.10.3 has what each of those was)
    mov ch, 216
    sub ch, al                      ; 216 - u: the right ring's distance...
    cmp ch, ah
    jbe .kr
    mov ch, ah                      ; ...or d, the front ring's: CH = k_ring
.kr:
    mov dl, [it_ybase]
    sub dl, ah                      ; DL = y of the top pixel
    add al, ah
    mov ah, 0
    rcl ah, 1                       ; AX = x = u + d (<= 314)
    mov bp, ax                      ; BP = x, for the box and the parity
    mov si, ax
    shl si, 1
    mov di, [it_xcol + si]          ; DI = x * 200
    mov al, dh                      ; AL = the cell value (0x10..0x31)
    xor dh, dh
    add di, dx                      ; DI = x * 200 + y
    xor ah, ah
    shl ax, 1
    mov si, ax                      ; SI = a word a cell value
    test bp, 1                      ; x's parity: the column's nibble
    jnz .odd
    mov ax, [it_wide + si - 2 * 0x10]
    jmp short .ids
.odd:
    mov ax, [it_wido + si - 2 * 0x10]
.ids:                               ; AL = TOP, AH = the face (column's nibble)
    mov es, [it_cseg]               ; (ES is the work claim on entry: it_seg)
    cld
    ; --- NOTHING BELOW: the column's lowest TOP row so far is above ours ----
    ; (it_ctop, kept here and reset with the canvas): then no cell in front
    ; has been drawn in this column at all, and k* is the ring's, min(k_ring,
    ; 23), with no scan - the early round's usual case.
    mov si, bp
    cmp [it_ctop + si], dl
    ja .scan
    mov [it_ctop + si], dl          ; ours is the column's lowest TOP now
    mov dh, ch
    cmp dh, IT_WALLH
    jbe .k
    mov dh, IT_WALLH
    jmp short .k
.scan:
    ; --- k*: the first TOP below, within min(22, k_ring - 1) rows ------------
    mov cl, ch
    dec cl                          ; the rows down to the ring, not it
    cmp cl, IT_PROBES
    jbe .ln
    mov cl, IT_PROBES
.ln:
    xor ch, ch                      ; CX = len, 0..22
    mov dh, cl                      ; DH = len
    jcxz .none
    mov si, di                      ; (the top pixel, back after the scan)
    inc di
    repne scasb                     ; AL = TOP: this column's nibble of it
    mov di, si
    jne .none
    sub dh, cl                      ; found m rows down: k* = m = len - CX,
    cmp dh, [it_maxprobe]           ; and m bytes examined - THE PROBE COUNT
    jbe .k                          ; (108.10.2), counted here on the scan
    mov [it_maxprobe], dh           ; path alone: the fast path examines none
    jmp short .k
.none:
    cmp dh, [it_maxprobe]           ; none: all len bytes examined...
    jbe .n1
    mov [it_maxprobe], dh
.n1:
    inc dh                          ; ...and k* = len + 1 - the ring, or 23
.k:
    ; --- the column: the top pixel, then k* - 1 face rows, one rep stosb ---
    stosb                           ; AL = TOP, at the top pixel
    mov al, ah                      ; the face
    mov cl, dh
    xor ch, ch
    dec cx
    rep stosb                       ; down the column: consecutive bytes
    mov es, [it_seg]                ; the work claim back
    ; --- the bound counter (SPEC.md 108.10.2): the most rows -----------------
    cmp dh, [it_maxspan]            ; DH = k* = the rows written
    jbe .nc
    mov [it_maxspan], dh
.nc:
    ; --- THE SURFACE'S TAIL (SPEC.md 108.12.3): windowed, and in EGA's full
    ; screen, the head's dirty box below; in a CGA, VGA or Hercules bracket
    ; the span written straight into video memory (itfs.inc's hooks). One
    ; indirect jump, ~30 cycles of a windowed step
    jmp [it_spanhk]

; it_span_box - it_span's windowed tail: BX = the head, BP = x, DL = y, DH = k*
it_span_box:
    ; --- the head's dirty box: x in BP, rows DL .. DL + DH -------------------
    ; THE FIRST STEP since the box went out starts it AT THIS COLUMN: a step
    ; writes its own column and no other (Lemma 1), so the column the head
    ; stood on was presented when it was drawn and is not owed again. After
    ; that, A STEP MOVES x BY EXACTLY ONE, up for N and E and down for S and
    ; W, and the top row only goes up on N - so one side of each range can
    ; grow: one compare for x and one or two for the rows.
    cmp byte [bx + H_BMV], 0
    jne .grow
    mov byte [bx + H_BMV], 1
    mov [bx + H_BX0], bp
    mov [bx + H_BX1], bp
    mov [bx + H_BY0], dl
    add dl, dh
    mov [bx + H_BY1], dl
    ret
.grow:
    mov al, [bx + H_HD]             ; (read once: the x side and the N test)
    test al, 2
    jnz .xd
    cmp bp, [bx + H_BX1]            ; N, E: x went up
    jbe .y
    mov [bx + H_BX1], bp
    jmp short .y
.xd:
    cmp bp, [bx + H_BX0]            ; S, W: x went down
    jae .y
    mov [bx + H_BX0], bp
.y:
    cmp al, HD_N
    jne .yb
    cmp dl, [bx + H_BY0]
    jae .yb
    mov [bx + H_BY0], dl
.yb:
    add dl, dh
    cmp dl, [bx + H_BY1]
    jbe .done
    mov [bx + H_BY1], dl
.done:
    ret

; -----------------------------------------------------------------------------
; it_box_clear - every head's dirty box empty: not moved (H_BMV = 0), so the
; head's next step starts its box at that step's own column (it_span).
; Preserves every register.
; -----------------------------------------------------------------------------
it_box_clear:
    push bx
    push cx
    mov bx, it_heads
    mov cx, MAXHEADS + 1
.l:
    mov byte [bx + H_BMV], 0
    add bx, H_SIZE
    loop .l
    pop cx
    pop bx
    ret

; --- tables -------------------------------------------------------------------
; it_wide / it_wido: a cell value's (top, face) ids - AL the TOP, AH the face -
; for an even column (shifted to the high nibble) and an odd one, a WORD per
; cell value from 0x10: player E/W, player N/S, computer E/W and N/S, a letter
; either way. The values between are not cells and read 0.
%macro IT_WIDS 1
    dw (ID_PFRONT << (8 + %1)) | (ID_TOP << %1)        ; 0x10 player, E/W
    dw (ID_PSIDE << (8 + %1)) | (ID_TOP << %1)         ; 0x11 player, N/S
    times 14 dw 0
    dw (ID_CFRONT << (8 + %1)) | (ID_TOP << %1)        ; 0x20 computer, E/W
    dw (ID_CSIDE << (8 + %1)) | (ID_TOP << %1)         ; 0x21 computer, N/S
    times 14 dw 0
    dw (ID_LOGOF << (8 + %1)) | (ID_TOP << %1)         ; 0x30 a letter
    dw (ID_LOGOF << (8 + %1)) | (ID_TOP << %1)         ; 0x31
%endmacro
it_wide:
    IT_WIDS 4
it_wido:
    IT_WIDS 0

; THE TITLE'S LETTERING (SPEC.md 108.9.4): "isoTron", our own 5 x 7 block
; glyphs - a width in cells, then seven rows, bit 7 the leftmost cell - and a
; 0. tools/isotronsim.py's LOGO is the same table and its selfcheck holds the
; two together.
it_logo:
    db 1, 0x80, 0x00, 0x80, 0x80, 0x80, 0x80, 0x80   ; i
    db 4, 0x00, 0x00, 0xF0, 0x80, 0xF0, 0x10, 0xF0   ; s
    db 4, 0x00, 0x00, 0xF0, 0x90, 0x90, 0x90, 0xF0   ; o
    db 5, 0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20   ; T
    db 4, 0x00, 0x00, 0xF0, 0x80, 0x80, 0x80, 0x80   ; r
    db 4, 0x00, 0x00, 0xF0, 0x90, 0x90, 0x90, 0xF0   ; o
    db 4, 0x00, 0x00, 0xF0, 0x90, 0x90, 0x90, 0x90   ; n
    db 0
