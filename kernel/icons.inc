; =============================================================================
; os8088 - icons.inc (SPEC.md 25)
;
; 1-bit icons with a mask, classic Mac style, drawn exactly like the mouse
; cursor (mouse.inc cur_draw/cur_pass): a white underlay pass using the mask
; rows as Bit Mask bits with Set/Reset = white, then a black pass from the
; data rows - generalized from the cursor's fixed 1-word x 16-row cell to
; wwords words per row and height rows.
;
; In-memory record:
;   db wwords        ; row width in words (1 = 16px, 2 = 32px)
;   db height        ; rows
;   ; height x wwords mask words, then height x wwords data words
;   ; bit 15 = leftmost pixel of the word's 16px span; words left -> right
;
; Each 16-bit source word lands at bit offset (x & 7) inside a 24-bit window
; spanning three VRAM bytes (same shifted-window trick as font.inc, widened
; by one byte): B0:B1 = row >> CL, B2 = low bits << (8-CL). Adjacent words
; of one row overlap on the shared middle byte with disjoint masks, so two
; masked writes compose correctly. A read of each target byte loads the
; latches before the masked write, so unmasked pixels survive.
;
; Clipping: rows are clipped to the screen (0..479) - icons drawn inside
; windows can hang off the bottom edge; byte columns past the right screen
; edge (>= ROW_BYTES) are skipped defensively. Leaves the GC in the default
; state (SPEC 1 rule 7). Caller holds the gfx lock.
; =============================================================================

; -----------------------------------------------------------------------------
; icon_draw_x - the API's entry: the record is the CALLER'S (SPEC.md 25.6.1)
; in:       CX = x, DX = y, ES:SI -> record; caller holds the gfx lock
; out:      CF = 0 drawn, CF = 1 refused (a record this cannot stage)
; clobbers: nothing else
;
; ico_pass reads the rows through DS and every ico_* variable through DS too,
; so DS cannot be moved to the caller's segment the way gfx_blit1_x moves it
; to the caller's band (SPEC.md 5.4.2) - the pass would then read its own
; geometry out of the package. The record is STAGED instead: it is 66 bytes at
; the outside, one `movsb` loop, and nothing measurable beside two passes over
; twelve rows.
;
; The bound is what the stage holds and it is a REFUSAL rather than a
; truncation: one word a row, sixteen rows - a 16x16 icon, and every control
; os88ui.inc draws. A wider record is a caller error, not a shape to clip.
; -----------------------------------------------------------------------------
ICO_STAGE_H equ 16              ; rows the stage holds, at one word a row
ICO_STAGE_WW equ 1              ; ...and words ACROSS. It is the `1` in
                                ; icon_draw_x's width refusal, named so the
                                ; stage's size and the test that keeps records
                                ; inside it cannot part
ICO_STAGE_SZ equ 2 + ICO_STAGE_H * (ICO_STAGE_WW * 4)

; ...and the INDEXED kind's decode buffer (SPEC.md 25.7), which is the same
; storage under a second name (25.7.2). It is the taller of the two, so it
; sizes the cell and ICO_STAGE_SZ is asserted against it below.
ICO_IBUF_H  equ 32              ; the tallest indexed record
ICO_IBUF_WW equ 2               ; ...and this kind is 32 px wide and only that
ICO_IBUF_SZ equ 2 * ICO_IBUF_H * (ICO_IBUF_WW * 2)  ; mask table + data table
; ...and the ceiling on the SHARED control glyph, asserted here because this is
; the first point both constants exist: kernel.asm includes fdlg.inc (and with
; it apps/os88ui.inc, which sets OS88UI_GW) one line above icons.inc.
;
; os88ui_glyph hands icon_draw_x the constant record `db 1, OS88UI_GW` and has
; NO fallback - the branch that used to stand in for one could not draw a
; glyph, so it went rather than being kept (SPEC.md 25.6.1). Of the four shapes
; icon_draw_x refuses - a width that is not ICO_STAGE_WW (which is ww = 0 and
; ww > 1 in one compare), rows = 0, rows > ICO_STAGE_H - the first two are
; pinned by ICO_STAGE_WW; this is the last, and a glyph grown past the
; stage would otherwise turn every control in every package into nothing at
; all, silently.
; No %ifdef around it: `equ` defines an ASSEMBLER symbol and %ifdef tests the
; PREPROCESSOR's, so `%ifdef OS88UI_GW` is false however defined it is - which
; is a guard that never fires. This file is the kernel's alone, so the symbol
; is always there and the %if can stand bare.
%if OS88UI_GW > ICO_STAGE_H
  %error "OS88UI_GW is taller than the icon stage: icon_draw_x refuses the control-glyph record and os88ui_glyph has no fallback (SPEC.md 25.6.1)"
%endif
%if ICO_STAGE_WW != 1
  %error "icon_draw_x refuses every width but ICO_STAGE_WW in ONE compare (`cmp al, ICO_STAGE_WW / jne .refuse`), which is only the same answer while the stage is ONE word across. A wider stage needs the pair back - `cmp al, ICO_STAGE_WW / ja .refuse` and `or al,al / jz .refuse` - as well as the resb below re-sized"
%endif
icon_draw_x:
    push ax
    push cx
    push si
    push di
    mov al, [es:si]             ; the header: words a row, then rows
    mov ah, [es:si+1]
    cmp al, ICO_STAGE_WW        ; ONE compare, two refusals: `ja` refused
    jne .refuse                 ; ww > 1 and `jz` refused ww = 0, so the only
                                ; surviving value was ICO_STAGE_WW itself -
                                ; which is what `jne` refuses the complement
                                ; of, over the whole byte domain, AL = 0
                                ; included (cmp 0,1 sets CF, so `ja` was
                                ; correctly not taken there either)
    or ah, ah
    jz .refuse
    cmp ah, ICO_STAGE_H
    ja .refuse
    push cx                     ; CX IS THE X, and the copy below wants it for
                                ; the count - so it is banked a second time
                                ; rather than spent. Without this the record
                                ; staged perfectly and drew at a byte count
    mov cl, ah                  ; CX = 2 + h * 4: the header, h mask words
    xor ch, ch                  ; and h data words
    shl cx, 1
    shl cx, 1
    inc cx                      ; ...+ the header. Two `inc`s are 2 bytes to
    inc cx                      ; `add cx,2`'s 3 and cost the same 6 clocks,
                                ; so it is shorter on both terms of the 8088's
                                ; max(clocks, 4.34 x bytes)
    mov di, ico_stage
    push di                     ; ...and the stage base comes back off the
                                ; stack below rather than out of an immediate
.cp:
    es lodsb                    ; DF = 0 is the system contract (SPEC.md 1
                                ; rule 3, 20.2) and this path already rests
                                ; on it:
                                ; ico_pass/ico_pass_bb reach `lodsw` from the
                                ; same call, and api_x does not `cld` either -
                                ; so the dependency is pre-existing, not new
    mov [di], al                ; DS is KERNEL_SEG here - the X stub's doing
    inc di
    loop .cp
    pop si                      ; the stage base...
    pop cx                      ; ...and x back
    call icon_draw
    clc
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret                         ; a pop writes no flags, so CF survives it
.refuse:
    mov word [ico_c1], (CBLACK << 8) | CWHITE   ; SPEC.md 25.6: the pen is
                                ; ONE-SHOT, and a refusal spends it too. Without
                                ; this the pair a refused draw was set for is
                                ; still armed for whatever the caller draws
                                ; next - which on the failing path is the one
                                ; time nobody is looking. A `mov` writes no
                                ; flags, so it goes ABOVE the `stc`
    stc
    jmp short .out

; -----------------------------------------------------------------------------
; icon_draw - draw a full icon record (header + mask + data)
;
; in:       CX = x, DX = y (top-left, screen px), SI -> record;
;           caller holds the gfx lock
; out:      nothing (all registers preserved)
; -----------------------------------------------------------------------------
icon_draw:
    GFXAUDIT_HOOK
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, [si]                ; header: wwords, height
    mov ah, 0
    mov [ico_ww], ax
    mov al, [si+1]              ; AH still 0
    mov [ico_h], ax
    add si, 2                   ; SI -> body (mask rows first)

; ico_draw_common - the tail both wrappers share, written once. Everything
; from here down was byte-identical in icon_draw16; the only difference
; between the two entries is how [ico_ww] and [ico_h] get set, and where SI
; is pointing when they are.
ico_draw_common:
    GFXDENTERCD                 ; whole-shape, like a glyph (SPEC.md 39.14.2)
    call ico_core
    mov word [ico_c1], (CBLACK << 8) | CWHITE   ; SPEC.md 25.6: ONE-SHOT
    GFXDLEAVE
    jmp kret_es

; -----------------------------------------------------------------------------
; icon_draw16 - draw a header-less 16x16 icon body (16 mask words + 16 data
;               words) - the harvested-icon / embedded-icon layout
;               (SPEC 19 / 20.2).
;
; in:       CX = x, DX = y, SI -> body; caller holds the gfx lock
; out:      nothing (all registers preserved)
; -----------------------------------------------------------------------------
icon_draw16:
    GFXAUDIT_HOOK               ; ...and it is the SAME primitive as far as the
                                ; audit is concerned. Missing here for as long
                                ; as it existed, which under -DGFXAUDIT meant
                                ; every 16x16 icon in the system - the Disk
                                ; window's rows, the dock's tiles, every
                                ; harvested and composed body - was drawn with
                                ; nothing counting it, while icon_draw's
                                ; header-carrying records were. The macro is
                                ; empty in every shipped build, so this costs
                                ; the machine nothing
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov word [ico_ww], 1
    mov word [ico_h], 16
    jmp short ico_draw_common   ; SI already points at the body: a 16x16 body
                                ; carries no header to step over

; -----------------------------------------------------------------------------
; icon_draw_ix - draw an INDEXED record: runs over the shared row pool
;                (SPEC.md 25.7). The desktop's four volume icons and nothing
;                else - 744 bytes of hand-authored rows became 135.
;
; in:       CX = x, DX = y, SI -> an indexed record; caller holds the gfx lock
; out:      nothing (all registers preserved)
;
; KERNEL-INTERNAL, and that is a contract rather than a convention: icon_draw
; and icon_draw_x take the PLAIN kind and their refusals do not change
; (SPEC.md 25.7.1). desk.inc names this one through cw_icon_draw_ix and
; nothing else does; no package can name it at all, there being no slot that
; takes the kind.
;
; The runs are expanded into a plain BODY and the draw is icon_draw's own from
; ico_draw_common down - so the clip, the pen, the GC setup, ico_core,
; ico_pass and ico_pass_bb are untouched by the whole of this.
; -----------------------------------------------------------------------------
icon_draw_ix:
    GFXAUDIT_HOOK               ; the same primitive as far as the audit is
                                ; concerned; empty in every shipped build
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, [si]                ; the same two bytes stand at the top of every
    mov ah, 0                   ; record, plain or indexed - which is what lets
    mov [ico_ww], ax            ; a reader (and tests/icoclip.py) see one shape
    mov al, [si+1]
    mov [ico_h], ax
    inc si                      ; SI -> the run bytes
    inc si

    ; --- expand: `2 * h` rows, mask table then data table ------------------
    push cx                     ; CX is the x and DX the y, and ico_draw_common
    push dx                     ; wants both - so they are BANKED rather than
                                ; worked around, which leaves the walk below
                                ; every register but SI and DI to spend
    push ds
    pop es                      ; ...and ES, for `stosw`. Nothing below
                                ; ico_draw_common requires a particular ES and
                                ; there is a PROOF rather than a survey of it:
                                ; icon_draw_x enters icon_draw with ES = the
                                ; CALLER'S PACKAGE SEGMENT, which is every
                                ; control glyph in every package's dialogs, so
                                ; a body that needed one would already be
                                ; broken there. Each pass sets its own
                                ; (VGA_SEG / [vid_rseg]).
                                ; DF = 0 is the system contract (SPEC.md 1
                                ; rule 3) and this file already rests on it
                                ; twice - icon_draw_x's `es lodsb` and both
                                ; passes' `lodsw`
    mov bp, [ico_h]
    shl bp, 1                   ; BP = rows still to produce
    mov di, ico_ibuf
    xor bx, bx                  ; BH stays 0 for the whole walk: nothing below
                                ; writes it and the mask keeps BL under 0x3C
.run:
    mov bl, [si]                ; run byte: pool row << 4 | repeat count - 1
    inc si
    mov cl, bl
    shr bl, 1                   ; BL = the pool row's BYTE offset. `(b >> 2) &
    shr bl, 1                   ; 0x3C` is `(b >> 4) * 4` in two shifts and a
    and bl, 0x3C                ; mask rather than six shifts, and the mask is
                                ; what drops the count nibble on the way past
    mov ax, [bx+ico_pool]       ; the row, once, however many times it repeats
    mov dx, [bx+ico_pool+2]
    and cl, 0x0F
    inc cl
    mov ch, 0                   ; CX = the repeat count, 1..16
    sub bp, cx                  ; ...off the budget BEFORE `loop` spends it
.rep:
    stosw                       ; the row's two words, `count` times over. The
    xchg ax, dx                 ; xchg pair is 2 bytes and puts them back, so
    stosw                       ; the row is fetched ONCE however long the run
    xchg ax, dx
    loop .rep
    jnz .run                    ; ...ON `sub bp, cx`'s OWN ZF, from before the
                                ; loop. Every instruction between the two is
                                ; flag-free on the 8086 - `stosw` and `xchg`
                                ; write none and neither does `loop`, which
                                ; decrements CX and tests it without touching
                                ; the flags - so the `or bp,bp` that used to
                                ; re-ask costs two bytes and answers the same.
                                ; ANYTHING ADDED TO `.rep` THAT WRITES FLAGS
                                ; BREAKS THIS, silently and only for a record
                                ; of more than one run. BP lands on exactly 0
                                ; rather than stepping past it because IHALF
                                ; counted both halves at assembly time
    pop dx
    pop cx

    mov si, ico_ibuf            ; ...and from here down it IS a plain body
    jmp ico_draw_common

; -----------------------------------------------------------------------------
; ico_core - shared body: clip, set up the GC, run the white (mask) pass and
;            the black (data) pass, restore GC defaults.
;
; in:       CX = x, DX = y, SI -> body, [ico_ww]/[ico_h] set
; clobbers: everything but DS (wrappers restore); caller holds the gfx lock
; -----------------------------------------------------------------------------
ico_core:
    call fnt_unlazy             ; whole-shape clipped like a glyph (7.1.4)
    mov [ico_x], cx
    mov [ico_y], dx

    cmp word [wm_clip_n], 0     ; whole-icon against a background task's clip
    je .noclip                  ; region, the font_char rule (SPEC.md 11.3):
    mov ax, cx                  ; an icon is one shape or none
    mov bx, dx
    mov cx, [ico_ww]            ; CX = 16 * wwords = the body's pixel width
    shl cx, 1                   ; (a "word" of an icon row is 16 pixels, not
    shl cx, 1                   ; 8 - four shifts, not three)
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx
    add dx, [ico_h]             ; DX is STILL y - nothing between the bank at
    dec dx                      ; `mov bx,dx` and here writes it - so y2 is
                                ; one add rather than a load and an add
    call wm_clip_test
    mov cx, [ico_x]
    mov dx, [ico_y]
    jc .none
.noclip:

    mov ax, [ico_ww]            ; source/VRAM stride bookkeeping
    shl ax, 1
    mov [ico_rb], ax            ; bytes per icon row (= 2 * wwords)
    mov bx, [vid_stride]
    sub bx, ax
    mov [ico_rowadv], bx        ; DI advance after a row's words (the planar
                                ; pass only - the banked one goes through
                                ; gfx_nextrow instead)

    mov [ico_maskp], si         ; mask rows first, data rows after
    mov ax, [ico_rb]            ; = 2 * wwords, set three lines up, so this is
    mul word [ico_h]            ; the table's BYTES straight off (DX clobbered)
    add ax, si
    mov [ico_datap], ax

    ; --- vertical clip to 0..[vid_chm1] (y is signed here) --------------------
    mov bx, [ico_h]             ; BX = rows to draw
    mov ax, [ico_y]
    or  ax, ax
    jns .top_ok
    neg ax                      ; AX = rows clipped off the top
    cmp ax, bx
    jae .none                   ; fully above the screen
    sub bx, ax
    mul word [ico_rb]           ; AX = bytes to skip in both tables
    add [ico_maskp], ax
    add [ico_datap], ax
    xor ax, ax                  ; AX is dead after the two adds, and zeroing a
    mov [ico_y], ax             ; register to store it is a byte shorter than
                                ; the immediate form
.top_ok:
    mov dx, [vid_ch]            ; ...held across the bottom clip rather than
    mov ax, [ico_y]             ; read three times. DX has been scratch since
    cmp ax, dx                  ; the `mul` above
    jae .none                   ; fully below the screen
    add ax, bx
    cmp ax, dx
    jbe .rows_ok
    mov bx, dx
    sub bx, [ico_y]             ; hang off the bottom edge: draw what fits
.rows_ok:
    mov [ico_rows], bx

    ; --- horizontal setup: byte column, shift pair, VRAM offset --------------
    ; A base column >= the row stride (x past the right edge, or negative x
    ; seen as huge unsigned) makes ico_pass skip every byte write.
    mov ax, [ico_x]
    mov cl, 3
    shr ax, cl
    mov [ico_col0], ax          ; base byte column (x / 8)
    mov di, ax
    mov ax, [ico_y]             ; AX = the row's base offset (DX clobbered;
    call gfx_rowbase            ; CX is loaded below)
    add di, ax                  ; DI = rowbase(y) + x/8
    mov cl, [ico_x]             ; a second read of x is a byte cheaper than
    and cl, 7                   ; carrying it in BX: CL = shift (x & 7)
    mov ch, 8
    sub ch, cl                  ; CH = 8 - shift

%ifndef GFX_VGA
    jmp ico_core_bb             ; no VGA: the 1bpp twin is the only pass there
%else                           ; is, and it returns straight to the wrapper
    cmp byte [vid_mono], 0      ; 1bpp? (SPEC.md 39.5) - the software
    jne ico_core_bb             ; twin returns straight to the wrapper

    mov ax, VGA_SEG
    mov es, ax

    mov dx, 0x3CE               ; GC1: enable Set/Reset on all planes
    mov ax, 0x0F01
    out dx, ax
    mov ah, [ico_c1]            ; GC0: Set/Reset color = the underlay
    mov al, 0                   ; (SPEC.md 25.6; CWHITE unless icon_pen said
    out dx, ax                  ; otherwise)
    mov al, 8                   ; leave the index at GC8 (Bit Mask)
    out dx, al
    mov si, [ico_maskp]
    call ico_pass               ; white underlay from the mask rows - it banks
                                ; DI itself, on every path including the
                                ; fully-clipped early-out
    dec dx                      ; 0x3CF -> 0x3CE. ico_pass's DOCUMENTED output
                                ; (see its header): the port is loaded above
                                ; its own early-out precisely so this is true
                                ; whether or not it drew anything
    mov ah, [ico_c2]            ; GC0: Set/Reset color = the picture's
    mov al, 0
    out dx, ax
    mov al, 8
    out dx, al
    mov si, [ico_datap]
    call ico_pass               ; black pixels on top

    call vga_gc_reset           ; back to SPEC.md 1 rule 7's resting state.
                                ; The five instructions this replaces wrote
                                ; GC1 and GC8 and left GC3 alone; the helper
                                ; is a SUPERSET - it writes GC3 = replace too
                                ; - so it can only move the card closer to the
                                ; documented rest. It costs ~160-200 clocks
                                ; (a near call/ret, two push/pop pairs and one
                                ; more word `out`), ~35 us against a VGA icon
                                ; of milliseconds; nine other sites in
                                ; vga12.inc already call it, and this was the
                                ; last hand-rolled copy.
%endif
.none:
    ret

%ifdef GFX_VGA
; -----------------------------------------------------------------------------
; ico_pass - one masked pass over [ico_rows] rows of [ico_ww] words each.
;            Each word lands at bit offset CL in a 3-byte window at DI+2w;
;            zero mask bytes and byte columns past the row end are skipped.
;
; in:       SI -> word rows, DI = VRAM offset of the row's first byte,
;           CL = shift, CH = 8-shift, ES = VGA_SEG,
;           GC: Set/Reset enabled + color set, index register = 8
; outs:     DX = 0x3CF, the GC DATA port - a documented OUTPUT, not a
;           leftover. ico_core's second pen reaches 0x3CE with `dec dx` on the
;           strength of it, which is why the load is ABOVE the early-out
;           below: a fully-clipped icon has to leave the same DX as one that
;           drew.
;           preserves CX, DI; clobbers ax, bx, dx, si, bp
; -----------------------------------------------------------------------------
ico_pass:
    push di
    mov dx, 0x3CF               ; GC data port; index already 8 (Bit Mask)
    mov bp, [ico_rows]
    or  bp, bp
    jz .done                    ; degenerate record / fully clipped
.row:
    mov ax, [ico_col0]
    mov [ico_col], ax           ; running byte column (right-edge clip)
    mov ax, [ico_ww]
    mov [ico_wleft], ax
.word:
    lodsw                       ; AX = row word, MSB = leftmost pixel
    mov bx, ax
    shr bx, cl                  ; BH:BL = B0:B1
    xchg cl, ch
    shl ax, cl                  ; AL = B2 (low bits spilling into byte 3)
    xchg cl, ch
    mov ah, al                  ; stash B2

    mov al, bh                  ; --- byte 0, column [ico_col] ---
    or  al, al
    jz .b1
    cmp word [ico_col], ROW_BYTES
    jae .b1
    out dx, al
    mov al, [es:di]             ; load latches
    mov [es:di], al
.b1:
    mov al, bl                  ; --- byte 1, column +1 ---
    or  al, al
    jz .b2
    cmp word [ico_col], ROW_BYTES-1
    jae .b2
    out dx, al
    mov al, [es:di+1]
    mov [es:di+1], al
.b2:
    mov al, ah                  ; --- byte 2, column +2 ---
    or  al, al
    jz .wnext
    cmp word [ico_col], ROW_BYTES-2
    jae .wnext
    out dx, al
    mov al, [es:di+2]
    mov [es:di+2], al
.wnext:
    inc di                      ; next word of this row
    inc di
    add word [ico_col], 2
    dec word [ico_wleft]
    jnz .word
    add di, [ico_rowadv]        ; next scanline (80 - 2*wwords)
    dec bp
    jnz .row
.done:
    pop di
    ret
%endif                          ; ...end of the VGA icon pass

; -----------------------------------------------------------------------------
; ico_core_bb - the software renderer's icon body (SPEC.md 32/39.5): the white underlay
;               ORs the shifted mask-row bits into all four planes, the
;               black pass AND-NOTs the data-row bits out of them - color
;               15 under the mask, 0 under the data, like the VRAM passes.
; in:       DI = row offset, CL/CH = shift pair, ico_* vars set by ico_core
; out:      returns to icon_draw/icon_draw16's epilogue (ico_core contract)
; clobbers: everything but DS (the wrappers restore)
; -----------------------------------------------------------------------------
ico_core_bb:
    mov ax, [ico_col0]
    cmp ax, [vid_stride]        ; x < 0 (huge unsigned) or entirely off the
    jae .none                   ; right edge: the passes would skip every
                                ; byte - so skip them. It is also what proves
                                ; ico_pass_bb's byte column cannot wrap: col0
                                ; is below the stride, the stride is 90 at
                                ; most, and the walk adds 2*ww <= 4 to it.
    mov al, [ico_c1]            ; SPEC.md 25.6: WHITE ors the rows in and
    call ico_bbop_of            ; anything else ands them out - which is the
    mov [ico_bbop], al          ; hardcoded pair's own behaviour, and gives a
    mov si, [ico_maskp]         ; pressed control its black underlay and a
    call ico_pass_bb            ; disabled one its stipple for free (the
    mov al, [ico_c2]            ; CALLER dithers the data rows; a 1bpp op has
    call ico_bbop_of            ; nowhere to put a grey)
    mov [ico_bbop], al
    mov si, [ico_datap]
    call ico_pass_bb
.none:
    ret

; ico_bbop_of - AL = a colour -> AL = 0 (or) for white, 1 (and-not) otherwise
; The 1bpp reduction is gfx_ink's (SPEC.md 39.4): only pure white lights a
; pixel, and the dither class is a black stipple, so it ands out like black.
ico_bbop_of:
    call gfx_ink
    cmp al, 0xFF
    mov al, 0
    je .out
    inc al
.out:
    ret

; -----------------------------------------------------------------------------
; ico_pass_bb - one masked pass over the renderer's single plane: the same
;               3-byte shifted window and edge clipping as ico_pass, with
;               [ico_bbop] picking OR (0) or AND-NOT (1) per byte.
; in:       SI -> word rows, DI = row offset, CL = shift, CH = 8-shift,
;           [ico_bbop] = 0/1, ico_* vars set
; out:      none; preserves CX, DI; clobbers ax, bx, dx, si, bp, es
;           (DH is the row stride and DL the running byte column, for the
;            whole pass - gfx_nextrow and ico_bbop_byte both leave DX alone)
; -----------------------------------------------------------------------------
ico_pass_bb:
    push di
    cmp word [ico_rows], 0
    je .done                    ; degenerate record / fully clipped
    mov es, [vid_rseg]          ; ONE PLANE, SO NO LOOP (SPEC.md 39.26): this
    mov bp, [ico_rows]          ; body is only ever entered with [vid_mono]
                                ; set, and vid_depth_set writes mono and
                                ; planes TOGETHER - so [vid_planes] is 1 here
                                ; and the walk never restarted. The two
                                ; restart-point cells existed only to restart
                                ; it, and go with it.
    mov dh, [vid_stride]        ; DH = the right-edge limit, for the whole
                                ; pass. A BYTE READ OF A WORD: [vid_stride] is
                                ; 80, 80 or 90 on the three adapters, so the
                                ; high byte is zero on all of them and no %if
                                ; can check a runtime value. A stride over 255
                                ; needs this pair widened, not just this read.
.row:
    mov dl, [ico_col0]          ; DL = running byte column, INCREMENTED past
                                ; each byte instead of three limits being
                                ; compared against a fixed column. Same
                                ; inequality: `bcol + k >= stride` is
                                ; `bcol >= stride - k`.
    mov ax, [ico_ww]
    mov [ico_wleft], ax
.word:
    lodsw                       ; AX = row word, MSB = leftmost pixel
    mov bx, ax
    shr bx, cl                  ; BH:BL = B0:B1
    xchg cl, ch
    shl ax, cl                  ; AL = B2 (low bits spilling into byte 3)
    xchg cl, ch
    mov ah, al                  ; stash B2

    mov al, bh                  ; --- byte 0, column DL ---
    or  al, al
    jz .b1
    cmp dl, dh
    jae .b1
    call ico_bbop_byte
.b1:
    inc di                      ; --- byte 1, column +1 ---
    inc dl                      ; A SKIPPED BYTE STILL ADVANCES, which is why
                                ; the step is here and not below the tests:
                                ; every refusal above lands on the NEXT byte's
                                ; label and .wnext moves neither DI nor DL any
                                ; more. Routing one straight to .wnext puts
                                ; the row-end `sub di,[ico_rb]` on the wrong
                                ; byte.
    mov al, bl
    or  al, al
    jz .b2
    cmp dl, dh
    jae .b2
    call ico_bbop_byte
.b2:
    inc di                      ; --- byte 2, column +2 ---
    inc dl
    mov al, ah
    or  al, al
    jz .wnext
    cmp dl, dh
    jae .wnext
    call ico_bbop_byte
.wnext:                         ; NOTHING TO DO. The three-byte windows overlap
                                ; by one - words step by two - so this word's
                                ; byte 2 IS the next word's byte 0, and .b2's
                                ; step already put DI and DL on it. The old
                                ; `add di,2` was here because .b1 and .b2 each
                                ; undid their own `inc di` before falling
                                ; through; nothing undoes anything now.
    dec word [ico_wleft]
    jnz .word
    sub di, [ico_rb]            ; back to the row's first byte, then down one
    call gfx_nextrow            ; scan line the adapter's way (SPEC.md 39.3)
    dec bp
    jnz .row
.done:
    pop di
    ret

; -----------------------------------------------------------------------------
; ico_bbop_byte - internal: apply [ico_bbop] to [es:di] with mask AL
;                 (0 = or in, 1 = and-not out); AH and all else preserved
; -----------------------------------------------------------------------------
ico_bbop_byte:
    cmp byte [ico_bbop], 0
    jne .clr
    or [es:di], al
    ret
.clr:
    not al
    and [es:di], al
    ret

; =============================================================================
; THE INDEXED RECORD (SPEC.md 25.7) - the desktop's four volume icons
;
; ico_disk32, ico_disk14, ico_hdd14 and ico_hdd32 held 744 bytes of `dw` rows
; between them and are the same picture four times: a diskette and a hard disk,
; at 32 rows and at 14 (SPEC.md 26.4). Enumerated, the four hold SIXTEEN
; DISTINCT ROWS - and a diskette's mask is one row repeated 32 times.
;
; So they are runs over a shared pool: one byte a run, `pool row << 4 | count -
; 1`, the first `height` rows the runs produce being the mask table and the
; next `height` the data table - which is a plain record's own order, so what
; icon_draw_ix hands ico_draw_common IS a plain body. 135 bytes for the four.
;
; A RUN DOES NOT CROSS THE MASK/DATA BOUNDARY. That costs exactly two bytes
; over letting it, and buys a source that reads as "these are the mask rows,
; these are the data rows" plus an assembly-time count of EACH HALF - a
; miscount inside one table that the other's compensates for is otherwise
; invisible until somebody looks at the icon. IHALF is that count; nothing
; below emits a byte the assembler has not counted, because a run stream is
; unreadable data and a wrong one draws a scrambled picture rather than
; failing to build.
; =============================================================================
%macro IREC 2                   ; wwords, height: opens a record
  %if (%1) != ICO_IBUF_WW
    %error "an indexed record is 32 px wide (ww = 2) and only that - it is what makes a pool row four bytes"
  %endif
  %if (%2) > ICO_IBUF_H
    %error "an indexed record taller than ico_ibuf holds: grow ICO_IBUF_H, which is the .bss cell as well as this test"
  %endif
    db %1, %2
  %assign ICO_IXH   (%2)
  %assign ICO_IXROW 0
%endmacro

%macro IRUN 2                   ; pool row, repeat count
  %if (%1) > 15
    %error "a pool row index is 0..15 - it is the run byte's HIGH nibble"
  %endif
  %if (%2) < 1 || (%2) > 16
    %error "a run is 1..16 rows - it is the run byte's LOW nibble, plus one"
  %endif
    db ((%1) << 4) | ((%2) - 1)
  %assign ICO_IXROW ICO_IXROW + (%2)
%endmacro

%macro IHALF 0                  ; ...and closes one table of exactly `height`
  %if ICO_IXROW != ICO_IXH
    %error "this table's runs do not add up to the record's height"
  %endif
  %assign ICO_IXROW 0
%endmacro

; -----------------------------------------------------------------------------
; ico_pool - the sixteen rows the four records are made of, four bytes each.
;            Indexed 0..15 by a run byte's high nibble, which is why there are
;            sixteen of them and not seventeen: the nibble IS the bound.
; -----------------------------------------------------------------------------
ico_pool:
    dw 0x3FFF, 0xFFFC           ;  0 IR_SOLID  ..############################..
    dw 0x2040, 0x0204           ;  1 IR_DSIDE  ..#......#............#......#..
    dw 0x204F, 0x0204           ;  2 IR_DSHUT  ..#......#..####......#......#..
    dw 0x207F, 0xFE04           ;  3 IR_DSBOT  ..#......##############......#..
    dw 0x2000, 0x0004           ;  4 IR_HOLLOW ..#..........................#..
    dw 0x27FF, 0xFFE4           ;  5 IR_DLTOP  ..#..######################..#..
    dw 0x2400, 0x0024           ;  6 IR_DLSIDE ..#..#....................#..#..
    dw 0x0000, 0x0000           ;  7 IR_BLANK  ................................
    dw 0x0C00, 0x0030           ;  8 IR_FEET   ....##....................##....
    dw 0x23FF, 0xFFC4           ;  9 IR_PLAT   ..#...####################...#..
    dw 0x241F, 0xF824           ; 10 IR_HHUB   ..#..#.....##########.....#..#..
    dw 0x27E7, 0xE7E4           ; 11 IR_BAYS   ..#..######..######..######..#..
    dw 0x247F, 0xFE24           ; 12 IR_RING   ..#..#...##############...#..#..
    dw 0x2480, 0x0124           ; 13 IR_FACE   ..#..#..#..............#..#..#..
    dw 0x2487, 0x8124           ; 14 IR_HUB    ..#..#..#....####......#..#..#..
    dw 0x2424, 0x2424           ; 15 IR_BAYSID ..#..#....#..#....#..#....#..#..

IR_SOLID    equ  0
IR_DSIDE    equ  1
IR_DSHUT    equ  2
IR_DSBOT    equ  3
IR_HOLLOW   equ  4
IR_DLTOP    equ  5
IR_DLSIDE   equ  6
IR_BLANK    equ  7
IR_FEET     equ  8
IR_PLAT     equ  9
IR_HHUB     equ 10
IR_BAYS     equ 11
IR_RING     equ 12
IR_FACE     equ 13
IR_HUB      equ 14
IR_BAYSID   equ 15
%if ($ - ico_pool) != 16 * 4
  %error "ico_pool is sixteen rows of two words - the run byte's high nibble indexes it and cannot reach a seventeenth"
%endif

; -----------------------------------------------------------------------------
; ico_disk32 - 32x32 Mac-style 3.5" floppy. Black data = 1px body outline,
;              shutter with cutout window, label rectangle; mask = the solid
;              disk silhouette so the desktop dither never bleeds through.
;
; data (black pixels), # = set:            rows
;   ..############################..        0      top edge
;   ..#......#............#......#..        1      body + shutter sides
;   ..#......#..####......#......#..        2..8    + cutout window (filled)
;   ..#......#............#......#..        9
;   ..#......##############......#..       10      shutter bottom
;   ..#..........................#..       11..14
;   ..#..######################..#..       15      label top
;   ..#..#....................#..#..       16..30  label sides
;   ..############################..       31      bottom edge
;
; mask (white underlay), every row:
;   ..############################..        0..31   solid silhouette
; -----------------------------------------------------------------------------
ico_disk32:
    IREC 2, 32                  ; wwords, height

    ; --- mask: 32 rows, the white underlay -------------------------------------
    IRUN IR_SOLID,  16      ;  0..15  ..############################..
    IRUN IR_SOLID,  16      ; 16..31  ..############################..
    IHALF

    ; --- data: 32 rows, the picture -------------------------------------
    IRUN IR_SOLID,   1      ;  0      ..############################..
    IRUN IR_DSIDE,   1      ;  1      ..#......#............#......#..
    IRUN IR_DSHUT,   7      ;  2.. 8  ..#......#..####......#......#..
    IRUN IR_DSIDE,   1      ;  9      ..#......#............#......#..
    IRUN IR_DSBOT,   1      ; 10      ..#......##############......#..
    IRUN IR_HOLLOW,  4      ; 11..14  ..#..........................#..
    IRUN IR_DLTOP,   1      ; 15      ..#..######################..#..
    IRUN IR_DLSIDE, 15      ; 16..30  ..#..#....................#..#..
    IRUN IR_SOLID,   1      ; 31      ..############################..
    IHALF

; -----------------------------------------------------------------------------
; ico_disk14 - the same diskette, 32 wide by 14 ROWS (SPEC.md 26.4)
;
; The CGA's pixels are 2.4:1 TALL - 640x200 on a 4:3 tube - so ico_disk32
; above renders 32 wide by about 77 high there: a stretched column, not a
; diskette. 14 rows is 33.6 real units against 32, which is square to within
; 5% and is also what a 3.5" diskette actually is (90 x 94mm).
;
; It is drawn and not squashed. Halving ico_disk32 by OR-ing row pairs was
; the cheap option and it thickens every 1px feature into 2 - the shutter
; window, the label box and the two vertical rules are all single lines, so
; what comes out is a blob with the diskette's outline. Selecting alternate
; rows loses them instead. The shutter keeps 3 rows of window and the label
; box 5 of interior, which is the proportion the 32-row icon has.
; -----------------------------------------------------------------------------
ico_disk14:
    IREC 2, 14                  ; wwords, height

    ; --- mask: 14 rows, the white underlay -------------------------------------
    IRUN IR_SOLID,  14      ;  0..13  ..############################..
    IHALF

    ; --- data: 14 rows, the picture -------------------------------------
    IRUN IR_SOLID,   1      ;  0      ..############################..
    IRUN IR_DSIDE,   1      ;  1      ..#......#............#......#..
    IRUN IR_DSHUT,   3      ;  2.. 4  ..#......#..####......#......#..
    IRUN IR_DSBOT,   1      ;  5      ..#......##############......#..
    IRUN IR_HOLLOW,  1      ;  6      ..#..........................#..
    IRUN IR_DLTOP,   1      ;  7      ..#..######################..#..
    IRUN IR_DLSIDE,  5      ;  8..12  ..#..#....................#..#..
    IRUN IR_SOLID,   1      ; 13      ..############################..
    IHALF

; -----------------------------------------------------------------------------
; ico_hdd14 - the hard disk at the same 32 x 14 (SPEC.md 26.4)
;
; ico_hdd32's three stacked sections - platter, bays, feet - in the rows
; there are for them: one platter with its hub, one row of three bays, and
; the two feet that make it sit on a desk rather than float.
; -----------------------------------------------------------------------------
ico_hdd14:
    IREC 2, 14                  ; wwords, height

    ; --- mask: 14 rows, the white underlay -------------------------------------
    IRUN IR_BLANK,   1      ;  0      ................................
    IRUN IR_SOLID,  12      ;  1..12  ..############################..
    IRUN IR_FEET,    1      ; 13      ....##....................##....
    IHALF

    ; --- data: 14 rows, the picture -------------------------------------
    IRUN IR_BLANK,   1      ;  0      ................................
    IRUN IR_SOLID,   1      ;  1      ..############################..
    IRUN IR_HOLLOW,  1      ;  2      ..#..........................#..
    IRUN IR_PLAT,    1      ;  3      ..#...####################...#..
    IRUN IR_DLSIDE,  1      ;  4      ..#..#....................#..#..
    IRUN IR_HHUB,    1      ;  5      ..#..#.....##########.....#..#..
    IRUN IR_DLSIDE,  1      ;  6      ..#..#....................#..#..
    IRUN IR_PLAT,    1      ;  7      ..#...####################...#..
    IRUN IR_SOLID,   1      ;  8      ..############################..
    IRUN IR_HOLLOW,  1      ;  9      ..#..........................#..
    IRUN IR_BAYS,    2      ; 10..11  ..#..######..######..######..#..
    IRUN IR_SOLID,   1      ; 12      ..############################..
    IRUN IR_FEET,    1      ; 13      ....##....................##....
    IHALF

; -----------------------------------------------------------------------------
; ico_app16 - 16x16 generic application: a diamond outline with a filled
;             square at the center; mask = the solid diamond silhouette.
;
; data (black pixels):                mask (white underlay):
;   .......##.......                    .......##.......
;   ......#..#......                    ......####......
;   .....#....#.....                    .....######.....
;   ....#......#....                    ....########....
;   ...#........#...                    ...##########...
;   ..#..........#..                    ..############..
;   .#....####....#.                    .##############.
;   #.....####.....#                    ################
;   #.....####.....#                    ################
;   .#....####....#.                    .##############.
;   ..#..........#..                    ..############..
;   ...#........#...                    ...##########...
;   ....#......#....                    ....########....
;   .....#....#.....                    .....######.....
;   ......#..#......                    ......####......
;   .......##.......                    .......##.......
; -----------------------------------------------------------------------------
ico_app16:
    db 1, 16                    ; wwords, height

    ; --- mask: solid diamond --------------------------------------------------
    dw 0x0180                   ;  0  .......##.......
    dw 0x03C0                   ;  1  ......####......
    dw 0x07E0                   ;  2  .....######.....
    dw 0x0FF0                   ;  3  ....########....
    dw 0x1FF8                   ;  4  ...##########...
    dw 0x3FFC                   ;  5  ..############..
    dw 0x7FFE                   ;  6  .##############.
    dw 0xFFFF                   ;  7  ################
    dw 0xFFFF                   ;  8  ################
    dw 0x7FFE                   ;  9  .##############.
    dw 0x3FFC                   ; 10  ..############..
    dw 0x1FF8                   ; 11  ...##########...
    dw 0x0FF0                   ; 12  ....########....
    dw 0x07E0                   ; 13  .....######.....
    dw 0x03C0                   ; 14  ......####......
    dw 0x0180                   ; 15  .......##.......

    ; --- data: diamond outline + center square --------------------------------
    dw 0x0180                   ;  0  .......##.......
    dw 0x0240                   ;  1  ......#..#......
    dw 0x0420                   ;  2  .....#....#.....
    dw 0x0810                   ;  3  ....#......#....
    dw 0x1008                   ;  4  ...#........#...
    dw 0x2004                   ;  5  ..#..........#..
    dw 0x43C2                   ;  6  .#....####....#.
    dw 0x83C1                   ;  7  #.....####.....#
    dw 0x83C1                   ;  8  #.....####.....#
    dw 0x43C2                   ;  9  .#....####....#.
    dw 0x2004                   ; 10  ..#..........#..
    dw 0x1008                   ; 11  ...#........#...
    dw 0x0810                   ; 12  ....#......#....
    dw 0x0420                   ; 13  .....#....#.....
    dw 0x0240                   ; 14  ......#..#......
    dw 0x0180                   ; 15  .......##.......

; -----------------------------------------------------------------------------
; ico_hdd32 - 32x32 hard disk: the drive body, a platter face and two feet.
;             What a driver-backed volume gets on the desktop (SPEC.md 26),
;             so a hard disk never reads as a floppy that will not eject.
;
; data (black pixels), # = set:            rows
;   ..............................         0
;   ..############################..       1      case top
;   ..#..........................#..       2
;   ..#...####################...#..       3      platter top
;   ..#..#....................#..#..       4
;   ..#..#...##############...#..#..       5      spindle ring top
;   ..#..#..#..............#..#..#..       6..11  the platter's face
;   ..#..#...##############...#..#..      12
;   ..#..#....................#..#..      13
;   ..#...####################...#..      14
;   ..#..........................#..      15
;   ..############################..      16     case rule
;   ..#..........................#..      17
;   ..#..######..######..######..#..      18     the three drive bays
;   ..#..######..######..######..#..      19..21
;   ..#..........................#..      22
;   ..############################..      23     case bottom
;   .....##................##.......      24..26 feet
;
; mask (white underlay), rows 1..23 solid, the feet's rows narrower - so the
; desktop dither never shows through the body and the feet still read.
; -----------------------------------------------------------------------------
ico_hdd32:
    IREC 2, 32                  ; wwords, height

    ; --- mask: 32 rows, the white underlay -------------------------------------
    IRUN IR_BLANK,   1      ;  0      ................................
    IRUN IR_SOLID,  16      ;  1..16  ..############################..
    IRUN IR_SOLID,   7      ; 17..23  ..############################..
    IRUN IR_FEET,    2      ; 24..25  ....##....................##....
    IRUN IR_BLANK,   6      ; 26..31  ................................
    IHALF

    ; --- data: 32 rows, the picture -------------------------------------
    IRUN IR_BLANK,   1      ;  0      ................................
    IRUN IR_SOLID,   1      ;  1      ..############################..
    IRUN IR_HOLLOW,  1      ;  2      ..#..........................#..
    IRUN IR_PLAT,    1      ;  3      ..#...####################...#..
    IRUN IR_DLSIDE,  1      ;  4      ..#..#....................#..#..
    IRUN IR_RING,    1      ;  5      ..#..#...##############...#..#..
    IRUN IR_FACE,    2      ;  6.. 7  ..#..#..#..............#..#..#..
    IRUN IR_HUB,     2      ;  8.. 9  ..#..#..#....####......#..#..#..
    IRUN IR_FACE,    2      ; 10..11  ..#..#..#..............#..#..#..
    IRUN IR_RING,    1      ; 12      ..#..#...##############...#..#..
    IRUN IR_DLSIDE,  1      ; 13      ..#..#....................#..#..
    IRUN IR_PLAT,    1      ; 14      ..#...####################...#..
    IRUN IR_HOLLOW,  1      ; 15      ..#..........................#..
    IRUN IR_SOLID,   1      ; 16      ..############################..
    IRUN IR_HOLLOW,  1      ; 17      ..#..........................#..
    IRUN IR_BAYS,    1      ; 18      ..#..######..######..######..#..
    IRUN IR_BAYSID,  2      ; 19..20  ..#..#....#..#....#..#....#..#..
    IRUN IR_BAYS,    1      ; 21      ..#..######..######..######..#..
    IRUN IR_HOLLOW,  1      ; 22      ..#..........................#..
    IRUN IR_SOLID,   1      ; 23      ..############################..
    IRUN IR_FEET,    2      ; 24..25  ....##....................##....
    IRUN IR_BLANK,   6      ; 26..31  ................................
    IHALF


; --- SPEC.md 25.6: the two colours a record is drawn in ----------------------
; The mask pass lays [ico_c1] and the data pass draws [ico_c2] over it. They
; were CWHITE and CBLACK, written into GC0 as immediates, which is every icon
; in the system and is not every 1bpp SHAPE the pass can draw: a pressed
; control is the same shape with the two exchanged, and a disabled one wants
; the overlay in CDGRAY (SPEC.md 47).
;
; ONE-SHOT, like gfx_blit1_pen (SPEC.md 5.4.2.2): icon_pen sets the pair and
; ico_core puts it back on the way out, so a caller that does not set it gets
; the icon pair and no icon site had to change.
ico_c1:     db CWHITE           ; the mask pass's colour: the underlay
ico_c2:     db CBLACK           ; ...and the data pass's: the picture
%if ico_c2 - ico_c1 != 1
  %error "ico_c1/ico_c2 must stay adjacent: icon_pen sets both in one word store"
%endif

; -----------------------------------------------------------------------------
; icon_pen - set the pair the NEXT icon_draw uses (SPEC.md 25.6)
; in:       AL = the mask pass's colour, AH = the data pass's
; out:      nothing
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
icon_pen:
    mov [ico_c1], ax
    ret


section .bss
%if ICO_STAGE_SZ > ICO_IBUF_SZ
  %error "ico_ibuf is the taller of the pair and sizes the cell (SPEC.md 25.7.2) - ICO_STAGE_SZ has outgrown it"
%endif
ico_ibuf:                       ; ONE BUFFER, TWO NAMES (SPEC.md 25.7.2): the
ico_stage:                      ; indexed kind's decode target and the X slot's
                                ; record stage. SPEC.md 25.6.1's argument for
                                ; there being ONE stage is the whole of the
                                ; argument for their sharing - every caller is
                                ; under the gfx lock and nothing in a drawing
                                ; path re-enters, so a second stager cannot
                                ; start between the first's copy and its
                                ; passes. Neither ISR that draws touches an
                                ; icon: the mouse's draws the pointer.
                                ; tests/icoclip.py reports the SPAN of .bss a
                                ; draw moves, from here across 512 bytes,
                                ; because a decoder that mis-sizes its buffer
                                ; writes into the cells below and the pixels
                                ; can be perfect while it does
    resb ICO_IBUF_SZ
ico_ww      resw 1              ; current record: words per row
ico_h       resw 1              ; current record: total rows
ico_rows    resw 1              ; rows after vertical clip
ico_rb      resw 1              ; bytes per icon row (2 * wwords)
ico_rowadv  resw 1              ; DI advance after a row (80 - 2*wwords)
ico_maskp   resw 1              ; -> first (unclipped) mask row
ico_datap   resw 1              ; -> first (unclipped) data row
ico_col0    resw 1              ; base byte column (x / 8)
ico_col     resw 1              ; running byte column inside ico_pass.
                                ; ico_pass_bb's is DL and its limit DH, so it
                                ; has no cell here at all
ico_wleft   resw 1              ; words left in the current row
ico_x       resw 1              ; requested x
ico_y       resw 1              ; requested y, clamped to 0 after top clip
ico_bbop    resb 1              ; bb pass: 0 = or (white), 1 = and-not (black)
section .text
