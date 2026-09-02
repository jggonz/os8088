; =============================================================================
; os8088 - tests/bandbench/bandbench.asm
;
; BANDBENCH: is composing a row of text in RAM and putting it down with ONE
; OSAPI_GFX_BLIT1 (SPEC.md 5.4.2) cheaper than lettering it a glyph at a time?
; That question is the whole gate on the proportional-type work, and this is
; what answers it. Nothing here ships: `make bench` builds it, `all` does not.
;
; WHAT IS MEASURED, and why these rows and not others.
;
; The workload is ONE LINE OF TEXT 624 PIXELS WIDE - PERFORMANCE.md Part 2's
; 78-cell reference row, so the answer can be read against a number that was
; taken on a real 5150 rather than against this harness alone. Two shapes of
; the same line:
;
;   the 8x8 FACE, 78 cells at 8 px      - what the machine does today
;   a PROPORTIONAL face, 104 glyphs at  - what it would do instead. 104 and
;   an average 6 px advance               not 78 because a proportional line
;                                         holds a third more characters in
;                                         the same width, which is most of
;                                         the point of setting one
;
; ROW 1 IS THE BAR AND IT IS DRAWN IN THE SAME RUN AS EVERYTHING ELSE. The
; plan this harness gates was written against fontbench's 701 us/cell and
; PERFORMANCE.md Part 2's 905, two harnesses disagreeing by 23% about the same
; primitive on the same machine. Quoting either would have been a guess, so
; both 78-cell rows are measured HERE, on the machine in front of you, in the
; same session as the rows they are compared with. If they disagree with Part 2
; again, THAT is the finding and the ratio rows below are not to be believed.
;
; The proportional side is split into its two halves because they answer
; different questions and only one of them is new:
;
;   COMPOSE   the shift-and-merge inner loop, in RAM, drawing nothing. This is
;             the cost the kernel never sees and the plan models at 37.8 us a
;             glyph row. It is the number to check hardest - it is the one
;             derived rather than measured.
;   BLIT1     the emit alone: one far call, one clip test, one rowbase, and a
;             rep movsb a row. SPEC.md 5.7's floor plus the bytes.
;   BAND      the two together, which is what an application actually pays.
;
; AND THE IDENTITY ROW, which is a CORRECTNESS test wearing a benchmark's
; clothes. It composes the same string at a fixed 8-px advance from the same
; ROM glyphs OSAPI_FONT_RUN letters with, and blits it directly under a
; FONT_RUN of that string. The two rows are the same pixels or the primitive
; is wrong, and a screendump says which - which is the only way to see a
; wrong shift, a wrong bank wrap or an off-by-one row on an adapter whose
; framebuffer this harness cannot read back.
;
; THE POLARITY IS BLACK ON WHITE and that is not a detail. A band arrives in
; final screen polarity (SPEC.md 5.4.2): a set bit is a LIT pixel on all three
; adapters, so white paper is 0xFF and ink CLEARS bits. The compose is
; therefore `and [di], not glyph` and not `or`. The rejected kernel-service
; design in the plan cleared its band to the background byte and then ORed the
; ink in, which sets nothing at all against 0xFF and renders every line
; invisible; the row below would have caught that on its first run.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'BANDBENCH', bb_entry

BB_CELLS    equ 78                ; the 8x8 row: cells, and 624 px
BB_W        equ BB_CELLS * 8
BB_BYTES    equ BB_CELLS          ; the band's WIDTH in bytes...
BB_STRIDE   equ BB_BYTES + 1      ; ...and its STRIDE, which is one byte more.
                                  ; A glyph at an arbitrary pen bit spills
                                  ; into the byte after the one it starts in,
                                  ; and the last glyph of a line has to have
                                  ; somewhere to spill that is not the first
                                  ; byte of the next ROW. The blit is handed
                                  ; BB_W pixels and BB_STRIDE as BP, so the
                                  ; spare column is composed and never drawn
BB_GLYPHS   equ 104               ; the proportional row: glyphs at BB_ADV px,
BB_ADV      equ 6                 ; 104 * 6 = 624 px, the same line
BB_ROWS8    equ 8                 ; band heights measured: the 8x8 face...
BB_ROWS12   equ 12                ; ...and a 12-row proportional face
BB_BANDSZ   equ BB_STRIDE * BB_ROWS12

BB_FIRST    equ 32                ; the ROM glyph range OSAPI_FONT_GLYPHS
BB_LAST     equ 126               ; answers about (SPEC.md 6)
BB_NGLYPH   equ BB_LAST - BB_FIRST + 1
BB_GTABSZ   equ BB_NGLYPH * 8

BB_N        equ 8                 ; iterations a row. A 78-cell FONT_RUN is
                                  ; ~71 ms on the target, so eight of them is
                                  ; over half a second and every row here is
                                  ; bracketed by benchlib's lap detector

; -----------------------------------------------------------------------------
; bb_entry - package entry (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment; gfx lock NOT held
; out: BX = window ptr, CF set = refused
; -----------------------------------------------------------------------------
bb_entry:
    push si
    call bb_glyphs                  ; the ROM face into our own segment, once
    call bb_ptab_build              ; ...its pre-shifted twin, 12,160 bytes
    call bb_teststr                 ; ...and the 78-character line to set
    call bb_hint
    mov si, bb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [bb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; the content origin onto a multiple of 8,
                                    ; so `aligned` means aligned (SPEC.md
                                    ; 11.94) - it preserves flags, so the CF
                                    ; this proc owes the loader survives
    clc
.out:
    pop si
    ret                             ; NEAR: the kernel reaches every proc here
                                    ; through our own dispatcher (SPEC.md 20.1)

; -----------------------------------------------------------------------------
; bb_glyphs - copy the kernel's 8x8 table into our own segment (SPEC.md 6)
; in:  -
; out: bb_gtab filled; preserves all registers
;
; It answers DX:SI and the table lives in LOW_SEG, NOT in KERNEL_SEG - so the
; segment comes from the CALL and not from anything this package holds. Copied
; once rather than read through ES per glyph, because the compose loop below
; is the thing being measured and an ES: override on its hot instruction would
; be measuring the wrong program.
; -----------------------------------------------------------------------------
bb_glyphs:
    push ax
    push cx
    push dx
    push si
    push di
    push ds
    push es
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, CX = bytes per glyph
    mov ax, ds
    mov es, ax                      ; ES:DI = ours
    mov di, bb_gtab
    mov ds, dx                      ; DS:SI = theirs
    mov cx, BB_GTABSZ
    cld
    rep movsb
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_teststr - fill bb_str with BB_CELLS characters of ordinary prose
; in:  -
; out: bb_str filled and NUL-terminated; preserves all registers
;
; Built rather than typed so the length is BB_CELLS by construction: a string
; one character short measures a 77-cell row and reports it as 78.
; -----------------------------------------------------------------------------
bb_teststr:
    push ax
    push cx
    push si
    push di
    mov di, bb_str
    mov cx, BB_CELLS
    mov si, bb_seed
.c:
    mov al, [si]
    or al, al
    jnz .have
    mov si, bb_seed                 ; wrap the seed rather than pad with
    mov al, [si]                    ; spaces: blank cells are the CHEAP ones
.have:                              ; (SPEC.md 6.1's renderer skips a blank
    mov [di], al                    ; glyph row whole), and a row of them
    inc si                          ; would flatter every figure here
    inc di
    loop .c
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_hint - the first page, before anything has been measured
; -----------------------------------------------------------------------------
bb_hint:
    push si
    call bl_blank
    mov si, bb_s_title
    call bl_sline
    call bl_head
    mov si, bb_s_hint
    call bl_sline
    pop si
    ret

bb_paint:
    call bl_paint
    ret

; -----------------------------------------------------------------------------
; bb_onkey - W_ONKEY: R runs, everything else pages the report
; -----------------------------------------------------------------------------
bb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [bb_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call bb_run
    call bb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_onclick - a click runs it, so the harness can be driven by the QMP mouse
;              alone (tests are scripted with tools/mouse.py)
; -----------------------------------------------------------------------------
bb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [bb_win], si
    call bb_run
    call bb_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bb_repaint - the measured rows scribbled over the page; put it back
bb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [bb_win]
    call OSAPI_WM_CONTENT
    mov [bb_cx], ax
    mov [bb_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [bb_cx]
    mov bx, [bb_cy]
    mov cx, ax
    add cx, [bb_cw]
    dec cx
    mov dx, bx
    add dx, [bb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [bb_win]
    call bl_paint
    call bb_ident                   ; ...and the correctness pair LAST, on top
                                    ; of the report. It has to be drawn again
                                    ; here: the run drew it, and then this
                                    ; routine white-filled the content it was
                                    ; standing in. A screendump is the whole
                                    ; assertion, so the pixels have to survive
                                    ; the repaint that follows the run
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the measured bodies. Each is called [bl_n] times inside one cli window, and
; each draws at ([bb_bx], [bb_by]) - the LAST content row, so a body cannot
; scribble over a result line that has already been written.
; =============================================================================

; --- 78 cells of 8x8, byte-aligned: the fast path (SPEC.md 6.1) -------------
bb_b_run8:
    mov cx, [bb_bx]
    mov dx, [bb_by]
    mov si, bb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; --- ...and at x+5, which is where a proportional pen lands seven times in
; eight and where a dragged window puts every run today
bb_b_run8u:
    mov cx, [bb_bx]
    add cx, 5
    mov dx, [bb_by]
    mov si, bb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; --- COMPOSE: 104 glyphs at a 6-px advance into the band, drawing nothing ---
bb_b_comp:
    call bb_compose
    ret

; --- BLIT1: the emit alone, 624 x 8 -----------------------------------------
bb_b_blit8:
    mov si, bb_band
    mov bp, BB_STRIDE
    mov ax, [bb_bx]
    mov bx, [bb_by]
    mov cx, BB_W
    mov dx, BB_ROWS8
    push ds
    pop es                          ; ES:SI = the band, which is ours
    call OSAPI_GFX_BLIT1
    ret

; --- ...and 624 x 12, a real proportional face's height ----------------------
bb_b_blit12:
    mov si, bb_band
    mov bp, BB_STRIDE
    mov ax, [bb_bx]
    mov bx, [bb_by]
    mov cx, BB_W
    mov dx, BB_ROWS12
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    ret

; --- BAND: compose then emit, which is what an application pays -------------
bb_b_band:
    call bb_compose
    call bb_b_blit12
    ret

; -----------------------------------------------------------------------------
; bb_compose - set BB_GLYPHS glyphs into the band at a BB_ADV-pixel advance
; in:  bb_gtab; [bb_adv] = the advance in pixels
; out: bb_band composed; preserves all registers
;
; THE INNER LOOP IS THE THING THIS HARNESS EXISTS TO PRICE. A proportional pen
; lands on an arbitrary bit, so every glyph row is widened into a 16-bit window
; and shifted - the same arithmetic font_char does per cell (SPEC.md 6), moved
; into the caller's RAM where it costs 15.3 us a byte instead of the
; framebuffer's 16.7 and where no drawing call is paid for it.
;
; `shr ax, cl` and not three single shifts: the shift count here is the pen's
; own bit offset and changes per glyph, so the unrolled form would be a jump
; table. An 8086 charges 8 + 4/bit for the CL form, ~22 clocks at an average
; offset of 3.5, against the ~756 us a drawing call costs - it is not close
; enough to matter, and the straight-line code is what can be read.
; -----------------------------------------------------------------------------
bb_compose:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov di, bb_band                 ; paper first: 0xFF is WHITE, and ink
    mov cx, BB_BANDSZ               ; CLEARS bits (SPEC.md 5.4.2's polarity)
    mov al, 0xFF
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es

    xor bp, bp                      ; BP = the pen, in pixels
    mov dx, [bb_nglyph]             ; DX = glyphs left
    mov si, bb_str
.glyph:
    mov al, [si]
    inc si
    or al, al
    jnz .have
    mov si, bb_str                  ; the line is BB_CELLS long and there are
    mov al, [si]                    ; BB_GLYPHS of them: wrap rather than stop
    inc si
.have:
    sub al, BB_FIRST
    cmp al, BB_NGLYPH
    jb .inrange
    xor al, al                      ; outside 32..126 draws as space (SPEC.md 6)
.inrange:
    mov bl, al                      ; BX = glyph index * 8
    xor bh, bh
    mov cl, 3
    shl bx, cl
    add bx, bb_gtab

    mov di, bp                      ; DI = the pen's byte column
    mov cl, 3
    shr di, cl
    add di, bb_band
    mov cx, bp                      ; CL = the pen's bit offset within it
    and cl, 7

    mov word [bb_rowsleft], 8       ; the band is 12 rows and the ROM glyph is
.row:                               ; 8: the rest stays paper, exactly as a
    mov al, [bx]                    ; face with a descender leaves it
    mov ah, al                      ; the 16-bit window: AH = the byte the
    xor al, al                      ; glyph starts in, AL = what spills into
    shr ax, cl                      ; the next one
    not ax                          ; ink CLEARS, so carry the complement
    and [di], ah
    and [di+1], al
    inc bx
    add di, BB_STRIDE
    dec word [bb_rowsleft]
    jnz .row

    add bp, [bb_adv]
    dec dx
    jnz .glyph

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_ptab_build - the PRE-SHIFTED glyph table, built once
; in:  bb_gtab
; out: bb_ptab filled; preserves all registers
;
; 95 glyphs x 8 pen phases x 8 rows x one WORD: the 16-bit window already
; shifted into place AND already complemented, so the compose loop below is
; three instructions a row instead of seven. 12,160 bytes, and the arithmetic
; that fills it is the arithmetic bb_compose does per glyph - done 6,080 times
; at load instead of 832 times a line.
;
; This exists to price SPEC.md 5.4.2's caller side. bb_compose measures the
; obvious loop; this measures the one a library would ship, and the difference
; between them is the whole of what a face costs in RAM.
; -----------------------------------------------------------------------------
bb_ptab_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov di, bb_ptab
    xor dx, dx                      ; DX = the glyph, 0..94
.g:
    mov bx, dx
    mov cl, 3
    shl bx, cl
    add bx, bb_gtab                 ; BX = its eight ROM rows
    xor ch, ch                      ; CH = the pen phase, 0..7
.p:
    mov si, bx
    mov bp, 8                       ; BP as a COUNTER, never as a base - it
.r:                                 ; addresses SS when it is one (SPEC.md 1)
    mov al, [si]
    mov ah, al                      ; the 16-bit window, shifted into the
    xor al, al                      ; phase and complemented once and for all
    mov cl, ch
    shr ax, cl
    not ax
    mov [di], ax
    inc si
    inc di
    inc di
    dec bp
    jnz .r
    inc ch
    cmp ch, 8
    jb .p
    inc dx
    cmp dx, BB_NGLYPH
    jb .g
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_compose_t - the same line, composed out of bb_ptab
; in:  [bb_nglyph], [bb_adv]
; out: bb_band composed; preserves all registers
;
; Three instructions a row and no loop at all: the eight rows are unrolled and
; the band's stride is an assemble-time DISPLACEMENT, so nothing advances DI
; either. That last part is a design statement rather than a benchmark trick -
; it says a shipped library should fix its band stride at build time rather
; than carry it in a register, and this row is what that decision is worth.
; -----------------------------------------------------------------------------
bb_compose_t:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov di, bb_band
    mov cx, BB_BANDSZ
    mov al, 0xFF
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es

    xor bp, bp
    mov dx, [bb_nglyph]
    mov si, bb_str
.glyph:
    mov al, [si]
    inc si
    or al, al
    jnz .have
    mov si, bb_str
    mov al, [si]
    inc si
.have:
    sub al, BB_FIRST
    cmp al, BB_NGLYPH
    jb .inrange
    xor al, al
.inrange:
    mov bl, al
    xor bh, bh
    mov cl, 7                       ; BX = (glyph * 8 + phase) * 16
    shl bx, cl                      ; = glyph * 128, before the phase
    mov cx, bp
    and cx, 7
    mov ax, cx
    mov cl, 4
    shl ax, cl
    add bx, ax
    add bx, bb_ptab

    mov di, bp
    mov cl, 3
    shr di, cl
    add di, bb_band

%assign BBR 0
%rep 8
    mov ax, [bx + BBR * 2]
    and [di + BBR * BB_STRIDE], ah
    and [di + BBR * BB_STRIDE + 1], al
%assign BBR BBR+1
%endrep

    add bp, [bb_adv]
    dec dx
    jnz .glyph

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bb_b_compt:
    call bb_compose_t
    ret

bb_b_bandt:
    call bb_compose_t
    call bb_b_blit12
    ret

; -----------------------------------------------------------------------------
; bb_ident - the CORRECTNESS row: the same string two ways, one above the other
; in:  gfx lock held
; out: [bb_cf] = 0 the blit reported drawn, 1 refused; preserves all registers
;
; Composed at a fixed 8-pixel advance from the very glyphs FONT_RUN letters
; with, so the two rows are the same pixels if - and only if - the primitive's
; shift, its row walk and its bank wrap are all right. Nothing here reads the
; framebuffer back: a screendump is the assertion, which is why the two rows
; are adjacent and left on screen.
; -----------------------------------------------------------------------------
bb_ident:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov cx, [bb_ix]                 ; the reference: one opaque FONT_RUN
    mov dx, [bb_iy]
    mov si, bb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    push word [bb_adv]              ; ...and the same line composed at 8 px
    mov word [bb_adv], 8
    mov word [bb_nglyph], BB_CELLS
    call bb_compose
    pop word [bb_adv]
    mov word [bb_nglyph], BB_GLYPHS

    mov si, bb_band
    mov bp, BB_STRIDE
    mov ax, [bb_ix]
    mov bx, [bb_iy]
    add bx, 10
    mov cx, BB_W
    mov dx, BB_ROWS8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    mov byte [bb_cf], 0
    jnc .ok
    mov byte [bb_cf], 1
.ok:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_run - every row, in order, into the report
; -----------------------------------------------------------------------------
bb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [bb_win]
    call OSAPI_WM_CONTENT
    mov [bb_cx], ax
    mov [bb_cy], dx
    mov bx, [bb_win]
    call OSAPI_WM_GEOM              ; CX/DX = the content's LIVE size, and the
    mov [bb_cw], cx                 ; template's is not it: wm_fit clamps a
    mov [bb_ch], dx                 ; 632x448 window onto the adapter it lands
                                    ; on (SPEC.md 39.7), so on CGA the content
                                    ; is 136 rows and not 446. Taking the
                                    ; CONSTANT put every measured row below the
                                    ; screen, where FONT_RUN clipped away
                                    ; whole and reported 3 PIT counts for a
                                    ; 78-cell line - a row that reads as an
                                    ; impossibly fast draw and is no draw
    mov ax, [bb_cx]
    add ax, 7                       ; the measured x: content left, rounded UP
    and ax, 0xFFF8                  ; to a multiple of 8, whatever pixel the
    mov [bb_bx], ax                 ; window was dragged to
    mov [bb_ix], ax
    mov ax, [bb_cy]
    add ax, [bb_ch]
    sub ax, 13                      ; the LAST content rows: a measured body
    mov [bb_by], ax                 ; must not scribble a result line
    sub ax, 22
    mov [bb_iy], ax

    call bl_blank
    mov si, bb_s_title
    call bl_sline
    call bl_head
    call bl_baseline                ; the loop overhead every P row is net of

    mov si, bb_s_hdr
    call bl_sline

    mov word [bl_n], BB_N
    mov word [bl_body], bb_b_run8
    mov si, bb_r_run8
    xor al, al
    call bl_run
    mov ax, [bl_lastus]             ; the BAR, banked for the ratio rows
    mov dx, [bl_lastus+2]
    mov [bb_tbar], ax
    mov [bb_tbar+2], dx

    mov word [bl_body], bb_b_run8u
    mov si, bb_r_run8u
    xor al, al
    call bl_run

    mov word [bl_body], bb_b_comp
    mov si, bb_r_comp
    xor al, al
    call bl_run

    mov word [bl_body], bb_b_blit8
    mov si, bb_r_blit8
    xor al, al
    call bl_run

    mov word [bl_body], bb_b_blit12
    mov si, bb_r_blit12
    xor al, al
    call bl_run

    mov word [bl_body], bb_b_band
    mov si, bb_r_band
    xor al, al
    call bl_run

    mov word [bl_body], bb_b_compt
    mov si, bb_r_compt
    xor al, al
    call bl_run

    mov word [bl_body], bb_b_bandt
    mov si, bb_r_bandt
    xor al, al
    call bl_run
    mov ax, [bl_lastus]             ; the PRE-SHIFTED band is the one the
    mov dx, [bl_lastus+2]           ; ratio quotes: it is what a library would
    mov [bb_tband], ax              ; ship, and the plain compose above is
    mov [bb_tband+2], dx            ; what it is an improvement on

    ; --- the derived row: how the two shapes of the same line compare -------
    call bl_blank
    mov si, bb_s_ratio
    call bl_sline
    mov ax, [bb_tbar]
    mov dx, [bb_tbar+2]
    mov bx, [bb_tband]
    mov cx, [bb_tband+2]
.fit:
    or cx, cx                       ; bl_ratio's divisor is CX, and both of
    jz .fitted                      ; these are 32-bit hundredths of a us -
    shr dx, 1                       ; a 71 ms row is 7,140,000 of them. Shift
    rcr ax, 1                       ; BOTH until the divisor fits, which costs
    shr cx, 1                       ; the ratio one part in 32,768 at worst
    rcr bx, 1
    jmp short .fit
.fitted:
    mov cx, bx
    or cx, cx
    jz .noratio
    call bl_ratio                   ; DX:AX * 100 / CX
    call bl_lclr
    mov si, bb_r_x100
    xor di, di
    call bl_lput
    mov di, BL_C_N
    mov cx, 9
    call bl_dec
    call bl_lcommit
.noratio:

    call bb_ident                   ; ...and the correctness pair, last, so it
    call bl_lclr                    ; survives on screen under the report
    mov si, bb_r_ident
    xor di, di
    call bl_lput
    mov al, [bb_cf]
    or al, al
    mov si, bb_s_drawn
    jz .cf0
    mov si, bb_s_refused
.cf0:
    mov di, BL_C_N
    call bl_lput
    call bl_lcommit

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

bb_tpl:
    dw 7, 22, 632, 448              ; x = 7 so WF_SNAP wants W_X + 1 on a
    dw bb_ttl, bb_paint, bb_onkey, bb_onclick   ; multiple of 8

bb_ttl:     db 'Band Bench', 0

bb_s_title: db 'BANDBENCH - one 624px line, lettered and composed', 0
bb_s_hint:  db 'Click the window, or press R, to run.', 0
bb_s_hdr:   db '-- the line: 78 cells of 8x8, and 104 glyphs at 6px --', 0
bb_s_ratio: db '-- what the composed line costs against the lettered one --', 0

; The seed the 78-character line is built from. Ordinary prose with ascenders,
; descenders, capitals and figures, so the blank-row fraction is a normal
; face's rather than a chosen one's (SPEC.md 6.2).
bb_seed:    db 'The quick brown fox jumps over 13 lazy dogs; Pack my box with 5 jugs. ', 0

bb_r_run8:  db 'FONT_RUN 78 aligned', 0
bb_r_run8u: db 'FONT_RUN 78 at x+5', 0
bb_r_comp:  db 'COMPOSE 104 glyphs', 0
bb_r_blit8: db 'BLIT1 624x8', 0
bb_r_blit12: db 'BLIT1 624x12', 0
bb_r_band:  db 'BAND compose+blit12', 0
bb_r_compt: db 'COMPOSE preshifted', 0
bb_r_bandt: db 'BAND preshift+blit12', 0
bb_r_x100:  db 'FONT_RUN/BAND x100', 0
bb_r_ident: db 'identity blit says', 0
bb_s_drawn: db 'DRAWN (CF=0)', 0
bb_s_refused: db 'REFUSED (CF=1)', 0

bb_win:     dw 0
bb_cx:      dw 0
bb_cy:      dw 0
bb_cw:      dw 0
bb_ch:      dw 0
bb_bx:      dw 0
bb_by:      dw 0
bb_ix:      dw 0
bb_iy:      dw 0
bb_adv:     dw BB_ADV
bb_nglyph:  dw BB_GLYPHS
bb_rowsleft: dw 0
bb_tbar:    dw 0, 0
bb_tband:   dw 0, 0
bb_cf:      db 0

BB_PTABSZ   equ BB_NGLYPH * 8 * 8 * 2     ; 95 glyphs x 8 phases x 8 rows
BB_BSS_OWN  equ BB_GTABSZ + BB_BANDSZ + BB_CELLS + 1 + BB_PTABSZ

    OS88_BSS BB_BSS_OWN + BL_BSS_SIZE
    OS88_IMAGE_END

bb_gtab     equ os88_image_end + 0
bb_band     equ os88_image_end + BB_GTABSZ
bb_str      equ os88_image_end + BB_GTABSZ + BB_BANDSZ
bb_ptab     equ os88_image_end + BB_GTABSZ + BB_BANDSZ + BB_CELLS + 1

    BL_BSS os88_image_end + BB_BSS_OWN
