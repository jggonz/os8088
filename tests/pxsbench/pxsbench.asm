; =============================================================================
; os8088 - tests/pxsbench/pxsbench.asm
;
; PXSBENCH: the unit costs PIXELSTEIN 3D's frame table is built from (SPEC.md
; 97.1, 97.10), measured on the machine in front of you. Nothing here ships:
; `make bench` builds it onto build/bench.img, `all` does not, and
; tests/pxsbench.py reads the rows back out of this package's bss on MartyPC's
; cycle-exact 5150 so the numbers in 97.1 are MEASURED and not derived.
;
; WHY THESE ROWS. docs/plans/PIXELSTEIN-PLAN.md 3 prices a frame from six
; units - a compiled store, a texel load, a DDA crossing, a span copy, a text
; expand, a key read - and every one of them was a figure DERIVED from an
; instruction table. Every rung the plan chooses rests on them, so before any
; code is shaped around a number, the number is taken here, in one run, on one
; adapter, in one report (PERFORMANCE.md Set 64's rule about two harnesses that
; disagreed by 23% about the same primitive).
;
;   (a) STORE     the 4-byte compiled store, `mov [di + r*80], al`, eighty of
;                 them - the scaler's row store (97.3) - into RAM, and into the
;                 framebuffer in the mode the game will use on this adapter;
;                 and the WORD store `mov [di + r*80], ax` of the Resolution:
;                 Low res set (docs/plans/PIXELSTEIN-PLAN.md 16), two columns
;                 a row
;   (b) LADDER    the STATIC flat-column ladder, `mov [di + r*80], bl` entered
;                 at an index (a Duff ladder) - the Flat rung's whole column
;   (c) DDA       the patched-immediate quadrant body of 97.2, one column's
;                 walk to a wall 10 and 20 crossings away, and a near-axial
;                 walk where one walker does nearly all the work. The
;                 difference of the first two is what a crossing costs. Each
;                 column carries 97.2.1's WHOLE setup - the angle off the fan,
;                 the quadrant dispatch, the two px_tan reads, the four
;                 patches, the two muls, the pointers and keys - and a fourth
;                 row runs the harness's own scaffolding alone (push/pop bp,
;                 the DS switch, the call) so the setup can be reported NET
;   (d) COPY      5,120 bytes of shadow to the framebuffer row by row (the
;                 shadow backends' present), and a 512x80 OSAPI_GFX_BLIT1 on
;                 the desktop (WIN1's)
;   (e) EXPAND    the C160 text backend's blit, 3,840 bytes, apps/skies'
;                 cs_blit.expand instruction for instruction; and the store
;                 ladder at the attribute stride, which is what a scaler
;                 writing straight into the text buffer would pay
;   (f) TEXEL     `mov al, [es:si + v]` + the store, eighty rows - the
;                 textured scaler's row - plain, and with an `xlat` between
;                 them, which is what a per-texel ink or dither lookup costs;
;                 with the odd-row dither phase turned AS THE 1992 ENGINE
;                 TURNS IT (WL_SCALE.C's dithershift, read for technique):
;                 `ror al, 1` x 2 on CGA 320x200x4 (one 2-bit pixel) and
;                 `ror al, cl` with CL = 3 on Hercules and WIN1 (one 1bpp
;                 pixel of the 2x2 dither... three bits, one instruction) -
;                 none on Mode X and none on C160, whose 16 colours are
;                 solid - which is what graft 1's rotate costs a texel; the
;                 DUAL-PHASE word load `mov ax, [es:si + 2v]` - AL the even
;                 rows' byte, AH the odd rows' - the named FALLBACK from the
;                 rotate (97.3: part 4 doubled, no scaler rotates); and the
;                 Resolution: Low res row - the load, `mov ah, al` and the
;                 WORD store, two columns a row from one texel
;   (i) HIT       97.2.5 once a column: the hit point off the walker's
;                 pointer, u, the jamb test, nx by two MUL14, the clamp,
;                 the div, px_sctab[h], top and bot, and the seven column-
;                 array stores - the third term of the cast, which the
;                 first take of the frame table carried as a bare D of 630
;                 (below the ~800 its own multiplies, divide and stores add
;                 to, which is the FLOOR a hit can reach)
;   (j) SIM       one TICK of the simulation over the plan's own counts -
;                 the player's step with per-axis collision, 32 actors, 64
;                 doors, the line-of-sight walks - the shape wave 3 writes,
;                 written the straightforward way it would first be written.
;                 The frame table's s term (97.1) was DOT DELIRIUM's five-dot
;                 step until this row, and s is the one term the fixed point
;                 GEARS: dF/ds = F / (T - s), 2.6x at the default rung
;   (g) KEY_DOWN  eight OSAPI_KEY_DOWN calls, the input pass
;   (h) GEN       one generation of the whole scaler set into a claim, and one
;                 byte-texture transpose (bt_build): what a Size change costs,
;                 once, in whole milliseconds (method T)
;
; THE ROWS THAT WRITE VIDEO MEMORY RUN INSIDE A FULLSCREEN BRACKET (SPEC.md
; 53), in the mode the game takes on this adapter - CGA 320x200x4, Hercules
; 720x348 or Mode X - so a VRAM store is priced in the mode that will pay it
; (a CGA's wait states are the mode's, not the card's). A genuine CGA then
; also takes the 160x100x16 text retime (88.15) for the two C160 rows. The
; bracket owns the raster, so nothing is drawn on the desktop by mistake and
; the kernel's exit repaint puts the report back. THE STORE ROWS SET DS = THE
; FRAMEBUFFER and store with no segment override, because that is the
; instruction the generator emits (97.3: DS = the destination) - an `es:`
; prefix is a fifth byte, and on a bus-bound 8088 a fifth byte is ~5 clk,
; which is the whole of the RAM/VRAM difference on a card with no wait
; states. The first cut of this bench measured the prefixed form and
; reported the prefix as a framebuffer penalty.
;
; The results are ALSO left in pb_res - a dword of hundredths of a microsecond
; per iteration and a flag byte per row - which is what tests/pxsbench.py
; reads; the screen is for a person. Every row sets [bl_body] itself
; (tools/benchlint.py's rule) and is bracketed by benchlib's lap detector.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PXSBENCH', pb_entry

PB_ROWS     equ 80                ; the view: 80 rows...
PB_STRIDE   equ 80                ; ...of 80 bytes, on every backend (97.3)
PB_VIEWB    equ 64                ; ...of which the picture is 64 bytes wide
PB_N        equ 8                 ; iterations of a method-P row
PB_NDDA     equ 64                ; ...and of a DDA row: one frame's columns
PB_NRES     equ 26                ; result rows the harness reads
PB_CLAIMKB  equ 59                ; the heap claim: the four map arrays, the
                                  ; generated scaler set / byte-texture set,
                                  ; a copy of the DDA setup's tables, the
                                  ; transpose's masters and the hit's column
                                  ; arrays (the package's own tables in the
                                  ; image left no room for them in bss
                                  ; beside benchlib's arena)
PB_MINDIST  equ 23                ; 97.1: nx clamped at 0.09 tiles (Q8.8)
PB_HEIGHTK  equ 51200             ; 97.1: h = PB_HEIGHTK / nx rows
PB_HMAX     equ 120               ; ...and the tallest scaler, 1.5 x the view
PB_MAPOFS   equ 512               ; the maps start here in the claim: a walker
                                  ; pointer may run 191 bytes past either end
                                  ; of its array before the compare parks it
                                  ; (97.2.3), and an offset under that would
                                  ; wrap the compare
PB_MAPT     equ PB_MAPOFS         ; mapT[x*64 + y]   - the V walker's
PB_SPOTT    equ PB_MAPT + 4096    ; spotvisT         - ...and its marks
PB_MAP      equ PB_SPOTT + 4096   ; map[y*64 + x]    - the H walker's
PB_SPOT     equ PB_MAP + 4096     ; spotvis
PB_SPOTD    equ 4096              ; a walker's mark is this far past its cell
PB_GENOFS   equ PB_SPOT + 4096    ; the scaler set / byte set, from here
PB_MASTERS  equ 15                ; materials in the byte-texture transpose
PB_SHADES   equ 2
PB_BTSZ     equ PB_MASTERS * PB_SHADES * 1024   ; the transpose's output
PB_GENSZ    equ PB_BTSZ           ; ...which is larger than the scaler set
; THE SETUP'S TABLES, copied into the claim past the generated region, so
; that the DDA rows read px_tan and the fan with the instructions the package
; runs - no segment override, because the package keeps its maps AND its
; tables in its own segment (97.9) and only this bench has them apart
PB_T_TAN    equ PB_GENOFS + PB_GENSZ            ; px_tan, PX_TAN_N words
PB_T_FAN    equ PB_T_TAN + PX_TAN_N * 2         ; px_fan64, 64 words
PB_T_QTAB   equ PB_T_FAN + 64 * 2               ; the four quadrant bodies
PB_W_HEAD   equ PB_T_QTAB + 4 * 2               ; the heading
PB_W_COL    equ PB_W_HEAD + 2                   ; the column index, x2
PB_W_COS    equ PB_W_COL + 2                    ; cos(heading), sin(heading)
PB_W_SIN    equ PB_W_COS + 2                    ; in Q14: the hit's frame
                                                ; constants (97.2.5)
PB_C_MAST   equ PB_W_SIN + 2                    ; the transpose's masters,
                                                ; read through ES the way
                                                ; bt_build reads the art part
; THE HIT'S OUTPUTS (97.2.5): the column arrays, a word a column each, and
; px_sctab (the scaler entry per height, 0..PB_HMAX) - in the claim beside
; the maps because the package keeps its column arrays in the segment its
; maps are in (97.9), and the hit row writes them with the instruction the
; driver will
PB_T_COLS   equ PB_C_MAST + PB_MASTERS * 512
PB_C_MAT    equ 0                               ; mat[c]
PB_C_SIDE   equ PB_C_MAT + PB_ROWS * 2          ; side[c]
PB_C_U      equ PB_C_SIDE + PB_ROWS * 2         ; u[c]
PB_C_WALLH  equ PB_C_U + PB_ROWS * 2            ; wallh[c], the z-buffer
PB_C_SC     equ PB_C_WALLH + PB_ROWS * 2        ; the scaler entry
PB_C_TOP    equ PB_C_SC + PB_ROWS * 2           ; top[c]
PB_C_BOT    equ PB_C_TOP + PB_ROWS * 2          ; bot[c]
PB_T_SCTAB  equ PB_T_COLS + PB_C_BOT + PB_ROWS * 2
; THE SIMULATION'S RECORDS (row (j)): the player, 32 actors of 24 bytes
; (the plan's 4.1 row) and 64 doors of 8, in the claim beside the map they
; collide with and walk through
PB_NACT     equ 32
PB_NDOOR    equ 64
PB_ASZ      equ 24
PB_DSZ      equ 8
PB_T_SIM    equ PB_T_SCTAB + (PB_HMAX + 1) * 2
PB_T_ACT    equ PB_T_SIM + 16
PB_T_DOOR   equ PB_T_ACT + PB_NACT * PB_ASZ
PB_CLAIMEND equ PB_T_DOOR + PB_NDOOR * PB_DSZ   ; (asserted after the include)
                                  ; an actor: x, y (Q8.8), state, kind, timer,
PB_A_X      equ 0                 ; target x/y, the LOS step x/y, the distance
PB_A_Y      equ 2                 ; to the player, a frame counter, hp, facing
PB_A_STATE  equ 4
PB_A_KIND   equ 5
PB_A_TIMER  equ 6
PB_A_TX     equ 8
PB_A_TY     equ 10
PB_A_LSX    equ 12
PB_A_LSY    equ 14
PB_A_DIST   equ 16
PB_A_FRAME  equ 18
PB_A_SEES   equ 19
PB_AS_STAND equ 0
PB_AS_CHASE equ 1
PB_AS_PATROL equ 2
PB_AS_DEAD  equ 3
                                  ; a door: cell, pos (0..256), state, lock,
PB_D_CELL   equ 0                 ; timer
PB_D_POS    equ 2
PB_D_STATE  equ 4
PB_D_LOCK   equ 5
PB_D_TIMER  equ 6
PB_DS_SHUT  equ 0
PB_DS_OPENING equ 1
PB_DS_OPEN  equ 2
PB_DS_CLOSING equ 3
PB_PS_X     equ 0                 ; the player block: x, y, the generation,
PB_PS_Y     equ 2                 ; and the row's two counts
PB_PS_GEN   equ 4
PB_PS_NACT  equ 6
PB_PS_NDOOR equ 8
PB_NACT1    equ 7                 ; ...E1M1's own (97.7), the second row
PB_NDOOR1   equ 22
PB_RADIUS   equ 64                ; 0.25 tile: the collision radius (97.8)
PB_ASTEP    equ 16                ; an actor's step a tick, 1/16 tile
PB_LOSN     equ 32                ; the line-of-sight walk's steps

; the DDA rows' eye and headings (97.2): x = 8.25, y = 8.5; the angle is
; heading + px_fan64[c] with c fixed at 32 (px_fan64[32] = 6), so every
; column of a row walks the same ray. 45 degrees (tan = 1.0, an H-then-V
; alternation with no ties) and 100 units (8.8 degrees: tan 0.155, cot 6.47
; - the V walker does nine of ten)
PB_EYEX     equ 8 * 256 + 64
PB_EYEY     equ 8 * 256 + 128
PB_COL      equ 32
PB_HEAD45   equ 512 - 6
PB_HEADA    equ 100 - 6

; -----------------------------------------------------------------------------
; pb_entry - package entry (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment; gfx lock NOT held
; out: BX = window ptr, CF set = refused
; -----------------------------------------------------------------------------
pb_entry:
    push si
    mov ax, PB_CLAIMKB
    call OSAPI_MEM_CLAIM
    jc .noclaim
    mov [pb_claim], dx
.noclaim:
    call pb_texture                 ; the texel rows' source, once
    call pb_masters                 ; ...and the transpose's
    call pb_hint
    mov si, pb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [pb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; an 8-aligned content origin (SPEC.md
                                    ; 11.94): the blit row wants x on the byte
                                    ; grid, and it preserves the flags so the
                                    ; CF this proc owes the loader survives
    clc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; pb_texture - a 256-byte column-major texture column, so the texel rows read
;              something that is not all one byte (the value does not matter,
;              the load does)
; -----------------------------------------------------------------------------
pb_texture:
    push ax
    push cx
    push di
    mov di, pb_tex
    xor al, al
    mov cx, 256
.t:
    mov [di], al
    inc di
    add al, 37
    loop .t
    mov di, pb_xlat                 ; ...and the xlat row's 256-entry map
    xor al, al
    mov cx, 256
.x:
    mov [di], al
    inc di
    inc al
    loop .x
    pop di
    pop cx
    pop ax
    ret

; pb_masters - fifteen 4bpp 32x32 masters (512 bytes each), a pattern, in
;              the claim (the transpose row is skipped without one)
pb_masters:
    push ax
    push cx
    push di
    push es
    cmp word [pb_claim], 0
    je .out
    mov es, [pb_claim]
    mov di, PB_C_MAST
    mov cx, PB_MASTERS * 512
    mov al, 0x12
.m:
    mov [es:di], al
    inc di
    add al, 0x11
    loop .m
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pb_hint - the first page, before anything has been measured
; -----------------------------------------------------------------------------
pb_hint:
    push si
    call bl_blank
    mov si, pb_s_title
    call bl_sline
    call bl_head
    mov si, pb_s_hint
    call bl_sline
    pop si
    ret

pb_paint:
    call bl_paint
    ret

; -----------------------------------------------------------------------------
; pb_onkey - W_ONKEY: R runs, everything else pages the report
; -----------------------------------------------------------------------------
pb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pb_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call pb_run
    call pb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pb_onclick - a click runs it too (tests are scripted with a mouse)
; -----------------------------------------------------------------------------
pb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pb_win], si
    call pb_run
    call pb_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pb_repaint - the blit row scribbled over the page; put it back
pb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [pb_win]
    call OSAPI_WM_CONTENT
    mov [pb_cx], ax
    mov [pb_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [pb_cx]
    mov bx, [pb_cy]
    mov cx, ax
    add cx, [pb_cw]
    dec cx
    mov dx, bx
    add dx, [pb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [pb_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; (a) the compiled store ladder, and (b) the static Duff ladder
; =============================================================================

; The row store a generated scaler emits (97.3): `mov [di + r*80], al` with
; a 16-bit displacement, four bytes each, eighty of them. Rows 0 and 1 would
; assemble to the 2- and 3-byte forms; the ladder is offset by 256 so every
; store is the 4-byte one the generator emits for every row past the first
; two, and DI is handed the buffer minus 256.
%macro PB_STORE80 1
%assign PBR 0
%rep PB_ROWS
    mov [di + 256 + PBR * PB_STRIDE], %1
%assign PBR PBR+1
%endrep
%endmacro

pb_b_st_ram:                        ; DS:DI = the shadow, in our own segment
    mov di, pb_shadow - 256
    mov al, 0x55
    PB_STORE80 al
    ret

pb_b_st_vram:                       ; DS = the framebuffer (bracket only): the
    push ds                         ; scaler's own segment discipline (97.3),
    mov ds, [pb_fsi + FSI_SEG]      ; and the SAME four-byte instruction as
    xor di, di                      ; the RAM row above
    sub di, 256
    mov al, 0x55
    PB_STORE80 al
    pop ds
    ret

; the Resolution: Low row store - one WORD a row, AL = AH, two columns
pb_b_stw_ram:
    mov di, pb_shadow - 256
    mov ax, 0x5555
%assign PBR 0
%rep PB_ROWS
    mov [di + 256 + PBR * PB_STRIDE], ax
%assign PBR PBR+1
%endrep
    ret

pb_b_stw_vram:
    push ds
    mov ds, [pb_fsi + FSI_SEG]
    xor di, di
    sub di, 256
    mov ax, 0x5555
    PB_STORE80 ax
    pop ds
    ret

; the STATIC ladder (97.3): `mov [di + r*80], bl`, entered at row index N
; through a table of entry points, so a flat column of N rows is one jump
; and N stores. Written top-down from row 79 to row 0 so that entering at
; entry[N] draws rows 0..N-1... i.e. the last N stores are rows N-1 down to
; 0. The blitted picture does not care which order rows are stored in.
pb_ladder:
%assign PBR PB_ROWS - 1
%rep PB_ROWS
    mov [di + 256 + PBR * PB_STRIDE], bl
%assign PBR PBR-1
%endrep
    ret
PB_LADSTEP  equ 4                   ; bytes an entry: every store is 4 bytes

pb_b_lad80:
    mov di, pb_shadow - 256
    mov bl, 0xAA
    mov ax, PB_ROWS - 80            ; rows to SKIP...
    shl ax, 1
    shl ax, 1                       ; ...times four bytes
    add ax, pb_ladder
    call ax
    ret

pb_b_lad40:
    mov di, pb_shadow - 256
    mov bl, 0xAA
    mov ax, PB_ROWS - 40
    shl ax, 1
    shl ax, 1
    add ax, pb_ladder
    call ax
    ret

; =============================================================================
; (c) the DDA - the quadrant body of SPEC.md 97.2, q0 (dx > 0, dy > 0)
; =============================================================================

; THE BODY. Two walkers, each a pointer and a fraction; the four immediates
; marked `strict word` are PATCHED once a column with the steps read off
; px_tan, and the whole walk touches memory in exactly three places a
; crossing: the cell test, the spotvis mark, nothing else.
;
;   SI = mapT + xt*64 + vrow   the V walker's cell (the transposed map)
;   DX = the y fraction, Q0.16
;   DI = map + yt*64 + hcol    the H walker's cell (the plain map)
;   BX = the x fraction
;   CX = mapT + xt*64 + yt     the corner in the V layout (V first: SI < CX)
;   BP = map + yt*64 + xt      ...and in the H layout (H first: DI < BP)
;   AL = the generation
;
; A V pass steps x by one (64 bytes of mapT) and y by the tangent's integer
; part plus the fraction's carry - ONE adc, its immediate int + 64. The keys
; follow: CX by 64 (x), BP by 1 (x).
pb_dda_q0:
    jmp short .vcheck
.ventry:
    test byte [si], 3               ; SOLID or DOOR
    jnz .vhit
    mov [si + PB_SPOTD], al         ; spotvisT = the generation
.vfrac:
    add dx, strict word 0           ; PATCHED: the y fraction step
.vint:
    adc si, strict word 0           ; PATCHED: y's integer step + 64
    add cx, 64
    inc bp
.vcheck:
    cmp si, cx
    jb .ventry
.hentry:
    test byte [di], 3
    jnz .hhit
    mov [di + PB_SPOTD], al
.hfrac:
    add bx, strict word 0           ; PATCHED: the x fraction step
.hint:
    adc di, strict word 0           ; PATCHED: x's integer step + 64
    add bp, 64
    inc cx
.hcheck:
    cmp di, bp
    jb .hentry
    jmp short .ventry
.vhit:
    mov byte [cs:pb_hitside], 0
    ret
.hhit:
    mov byte [cs:pb_hitside], 1
    ret

PB_DDA_BYTES equ $ - pb_dda_q0

; pb_dda_col - one column's cast: 97.2.1's setup instruction for instruction,
;              then the body. The angle from the heading and the fan, the
;              quadrant off its top two bits, the two steps off px_tan (one of
;              them the tan[1024 - i] form), the four patches, the two muls
;              for the first intercepts, the pointers and the keys, the walk.
; in:  DS = ours; the maps and the table copies are in the claim
;      [PB_W_HEAD], [PB_W_COL] = the heading and the column index x2, both
;      fixed for a row so every column walks the same ray
; What is HARNESS here and not the package's: push/pop bp, the DS switch and
; the row's own call - the driver holds DS and BP for the whole frame and
; enters a column with a near jump. pb_b_ddasc runs exactly that scaffolding
; and nothing else, and tests/pxsbench.py subtracts it. What is CHEAPER here
; than in the package: the two keys are immediates (mov reg, imm16) where
; the driver loads them from the words it computed once a frame - ~10 clk
; each. The four patch stores carry a cs: override, as the driver's do:
; the bodies live in part 3 beside the driver, not in DS.
pb_dda_col:
    push bp
    push ds
    mov ds, [cs:pb_claim]
    ; --- the angle and its quadrant ---------------------------------------
    mov bx, [PB_W_COL]              ; c * 2
    mov ax, [PB_W_HEAD]
    add ax, [PB_T_FAN + bx]         ; a = heading + fan[c]
    and ax, 0FFFh
    mov bx, ax
    and bx, 1023                    ; i = a & 1023
    shl bx, 1
    mov cx, [PB_T_TAN + bx]         ; tan[i]
    neg bx
    mov dx, [PB_T_TAN + 2048 + bx]  ; tan[1024 - i]
    mov bl, ah
    shr bl, 1
    shr bl, 1                       ; the quadrant: the angle's bits 11:10
    and bx, 3
    shl bx, 1
    jmp word [PB_T_QTAB + bx]       ; ...to its body's setup. The bench has
                                    ; one body, so all four entries are .q0
.q0:                                ; an even quadrant: CX = |tan|, the V
                                    ; step; DX = |cot|, the H step
    push dx                         ; tanh, for the H half
    ; --- the V patches ----------------------------------------------------
    mov ax, cx                      ; AH = the integer part, AL = the fraction
    mov dh, al
    xor dl, dl                      ; DX = the fraction << 8
    mov [cs:pb_dda_q0.vfrac + 2], dx
    mov al, ah
    xor ah, ah
    add ax, 64                      ; ...and x's step, a row of mapT
    mov [cs:pb_dda_q0.vint + 2], ax
    ; --- the V walker's first intercept: y at x = 9 ---------------------
    mov ax, cx
    mov cx, 256 - (PB_EYEX & 255)   ; xpartial
    mul cx                          ; DX:AX = tan * xpartial, Q16.16
    mov al, ah
    mov ah, dl                      ; AX = the product >> 8: yoff in Q8.8
    add ax, PB_EYEY                 ; yint
    mov dh, al
    xor dl, dl                      ; DX = its fraction << 8
    mov al, ah
    xor ah, ah                      ; AX = vrow
    add ax, PB_MAPT + ((PB_EYEX >> 8) + 1) * 64
    mov si, ax                      ; SI = mapT + xt*64 + vrow
    ; --- the H patches ----------------------------------------------------
    pop cx                          ; tanh
    mov ax, cx
    mov bh, al
    xor bl, bl
    mov [cs:pb_dda_q0.hfrac + 2], bx
    mov al, ah
    xor ah, ah
    add ax, 64
    mov [cs:pb_dda_q0.hint + 2], ax
    ; --- the H walker's first intercept: x at y = 9 ---------------------
    push dx                         ; the V fraction, across the mul
    mov ax, cx
    mov cx, 256 - (PB_EYEY & 255)   ; ypartial
    mul cx
    mov al, ah
    mov ah, dl                      ; xoff
    add ax, PB_EYEX                 ; xint
    mov bh, al
    xor bl, bl                      ; BX = fraction << 8
    mov al, ah
    xor ah, ah                      ; AX = hcol
    add ax, PB_MAP + ((PB_EYEY >> 8) + 1) * 64
    mov di, ax                      ; DI = map + yt*64 + hcol
    pop dx
    ; --- the keys, the generation -----------------------------------------
    mov cx, PB_MAPT + ((PB_EYEX >> 8) + 1) * 64 + (PB_EYEY >> 8) + 1
    mov bp, PB_MAP + ((PB_EYEY >> 8) + 1) * 64 + (PB_EYEX >> 8) + 1
    mov al, 1
    call pb_dda_q0
    pop ds
    pop bp
    ret

; pb_b_ddasc - the scaffolding a DDA row carries and the package does not:
;              the BP and DS banking and the row's own call. Measured as a
;              row of its own so the setup is reported net of it - and the
;              hit row below carries the same scaffold, so it is net too.
pb_b_ddasc:
    push bp
    push ds
    mov ds, [cs:pb_claim]
    pop ds
    pop bp
    ret

; PB_MUL14 - apps/tank/tk3d.inc:31's MUL14, the Q14 multiply of 97.2.5:
;            AX = (AX x BX) >> 14, signed - imul, two shift/rotate pairs on
;            the 32-bit product, the high word
%macro PB_MUL14 0
    imul bx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov ax, dx
%endmacro

; pb_b_hit - 97.2.5's hit, once a column: from the walker's state to the
;            column arrays. The V side of quadrant 0 (dxs > 0, dys > 0); the
;            H side is the same arithmetic with the axes swapped, and the
;            mirror of u (255 - frac from the east and the north) is the
;            QUADRANT's, known when the body is generated, so it is not a
;            test here and not a test in the package. What the driver has
;            live in registers at the hit - the cell pointer, the fraction -
;            are two immediates here (~26 clk of this row, said in the
;            report); the column index, cos and sin are LOADED, as the
;            driver's are (it has no register to spare across the walk). The
;            scaffold is pb_b_ddasc's and the harness subtracts it.
;   SI = the V walker's cell (mapT + xt*64 + vrow)   DX = the y fraction Q0.16
pb_b_hit:
    push bp
    push ds
    mov ds, [cs:pb_claim]
    mov si, PB_MAPT + 13 * 64 + 12  ; the cell (13, 12)...
    mov dx, 0x8000                  ; ...at y = 12.5
    mov di, [PB_W_COL]              ; c * 2, the column arrays' index
    ; --- the material, and the jamb (97.2.4): DOOR in the cell the ray
    ;     came from - the un-stepped pointer's, one row of mapT back ------
    mov al, [si]
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1                       ; the material nibble
    test byte [si - 64], 2
    jz .nojamb
    mov al, 15                      ; the jamb
.nojamb:
    mov [PB_T_COLS + PB_C_MAT + di], al             ; mat[c]
    mov byte [PB_T_COLS + PB_C_SIDE + di], 0        ; side[c]: V is lit
    ; --- u: the fraction's top five bits, 32 texels --------------------
    mov al, dh
    shr al, 1
    shr al, 1
    shr al, 1
    mov [PB_T_COLS + PB_C_U + di], al               ; u[c]
    ; --- the hit point off the pointer: x = xt (a V hit from the west),
    ;     y = vrow + the fraction; then dx, dy from the eye, Q8.8 ---------
    mov ax, si
    sub ax, PB_MAPT                 ; xt*64 + vrow
    mov bx, ax
    and bx, 63                      ; vrow
    mov cl, 6
    shr ax, cl                      ; xt (a shift by CL: per column, not per
                                    ; pixel, which 97.1's rule allows)
    mov ch, bl
    mov cl, dh                      ; CX = vrow.frac
    sub cx, PB_EYEY                 ; dy
    mov ah, al
    xor al, al                      ; AX = xt << 8
    sub ax, PB_EYEX                 ; dx
    ; --- nx = dx cos + dy sin, MUL14 twice, clamped ---------------------
    mov bx, [PB_W_COS]
    PB_MUL14
    mov bp, ax
    mov ax, cx
    mov bx, [PB_W_SIN]
    PB_MUL14
    add ax, bp                      ; nx, Q8.8
    cmp ax, PB_MINDIST
    jge .far
    mov ax, PB_MINDIST
.far:
    ; --- h = K / nx: the column's one div -------------------------------
    mov bx, ax
    mov ax, PB_HEIGHTK
    xor dx, dx
    div bx                          ; AX = h
    mov [PB_T_COLS + PB_C_WALLH + di], ax           ; wallh[c], unclipped
    cmp ax, PB_HMAX
    jbe .hok
    mov ax, PB_HMAX                 ; taller than the tallest scaler: clamp
.hok:
    mov bx, ax
    shl bx, 1
    mov bx, [PB_T_SCTAB + bx]       ; px_sctab[h]
    mov [PB_T_COLS + PB_C_SC + di], bx              ; the scaler entry
    ; --- top and bot ----------------------------------------------------
    mov bx, PB_ROWS
    sub bx, ax                      ; 80 - h
    jbe .full
    shr bx, 1                       ; top = (80 - h) >> 1
    mov [PB_T_COLS + PB_C_TOP + di], bx             ; top[c]
    add bx, ax
    dec bx
    mov [PB_T_COLS + PB_C_BOT + di], bx             ; bot[c] = top + h - 1
    jmp short .done
.full:
    mov word [PB_T_COLS + PB_C_TOP + di], 0         ; the whole view
    mov word [PB_T_COLS + PB_C_BOT + di], PB_ROWS - 1
.done:
    mov [cs:pb_hith], ax            ; h, for the harness (32 at this hit)
    pop ds
    pop bp
    ret

; pb_tables - px_tan, px_fan64, the quadrant table, the hit's frame
;             constants and a px_sctab into the claim, once
pb_tables:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [pb_claim]
    mov si, px_tan
    mov di, PB_T_TAN
    mov cx, PX_TAN_N
    cld
    rep movsw
    mov si, px_fan64
    mov di, PB_T_FAN
    mov cx, 64
    rep movsw
    mov ax, pb_dda_col.q0
    mov cx, 4
    rep stosw                       ; PB_T_QTAB: four entries, one body
    mov word [es:PB_W_COL], PB_COL * 2
    mov ax, [px_sin + 512 * 2]      ; cos(512) = sin(1024 - 512): 45 degrees,
    mov [es:PB_W_COS], ax           ; the same for both
    mov [es:PB_W_SIN], ax
    mov di, PB_T_SCTAB              ; a scaler entry per height: any word
    mov cx, PB_HMAX + 1             ; will do, the lookup is what is priced
    mov ax, PB_GENOFS
.sc:
    stosw
    add ax, 96
    loop .sc
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; pb_mapclear - both map layouts open, with a solid border
pb_mapclear:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [pb_claim]
    mov di, PB_MAPT
    mov cx, 4 * 4096 / 2
    xor ax, ax
    cld
    rep stosw
    mov cx, 64                      ; the border, in both layouts: the first
    xor di, di                      ; and last major row, the first and last
    xor si, si                      ; minor column
.b:
    mov byte [es:PB_MAPT + di], 1                   ; mapT[0*64 + i]
    mov byte [es:PB_MAPT + 63 * 64 + di], 1         ; mapT[63*64 + i]
    mov byte [es:PB_MAPT + si], 1                   ; mapT[i*64 + 0]
    mov byte [es:PB_MAPT + 63 + si], 1              ; mapT[i*64 + 63]
    mov byte [es:PB_MAP + di], 1                    ; map[0*64 + i]
    mov byte [es:PB_MAP + 63 * 64 + di], 1          ; map[63*64 + i]
    mov byte [es:PB_MAP + si], 1                    ; map[i*64 + 0]
    mov byte [es:PB_MAP + 63 + si], 1               ; map[i*64 + 63]
    inc di
    add si, 64
    loop .b
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; pb_heading45 / pb_headinga - the row's heading into the claim
pb_heading45:
    push es
    mov es, [pb_claim]
    mov word [es:PB_W_HEAD], PB_HEAD45
    pop es
    ret

pb_headinga:
    push es
    mov es, [pb_claim]
    mov word [es:PB_W_HEAD], PB_HEADA
    pop es
    ret

; pb_wall - AX = x, BX = y: one solid cell, in both layouts
pb_wall:
    push ax
    push bx
    push cx
    push si
    push es
    mov es, [pb_claim]
    mov cl, 6
    mov si, ax
    shl si, cl                      ; x*64
    add si, bx
    mov byte [es:PB_MAPT + si], 1   ; mapT[x*64 + y]
    mov si, bx
    shl si, cl
    add si, ax
    mov byte [es:PB_MAP + si], 1    ; map[y*64 + x]
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; (d) the present: the row copy to the framebuffer, and the desktop blit
; =============================================================================

; pb_b_copy - 80 rows of 64 bytes, shadow to the framebuffer through the
;             per-row device offset table, `rep movsw` a row (bracket only)
pb_b_copy:
    push es
    push bp
    mov es, [pb_fsi + FSI_SEG]
    mov bx, pb_devoff
    mov si, pb_shadow
    mov bp, PB_ROWS
    cld
.row:
    mov di, [bx]
    inc bx
    inc bx
    mov cx, PB_VIEWB / 2
    rep movsw
    add si, PB_STRIDE - PB_VIEWB
    dec bp
    jnz .row
    pop bp
    pop es
    ret

; pb_devrows - the device offset of each of the 80 view rows in the mode
;              the bracket set (apps/skies' cs_devrows, the three arms)
pb_devrows:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, pb_devoff
    xor bx, bx                      ; BX = the row
.row:
    mov ax, bx
    mov cl, [pb_fsi + FSI_MODE]
    cmp cl, FSXM_HERC
    je .herc
    cmp cl, FSXM_TEXT80
    je .text
    cmp cl, FSXM_MODEX
    je .linear
    mov dx, ax                      ; CGA: bank = y & 1, 80 bytes a row
    and dx, 1
    shr ax, 1
    mov cx, PB_STRIDE
    push dx                         ; the bank, across the mul that writes
    mul cx                          ; DX (apps/skies/csraster.inc's shape)
    pop dx
    jmp short .bank
.herc:
    mov dx, ax                      ; HERC: bank = y & 3, 90 bytes a row, +5
    and dx, 3
    shr ax, 1
    shr ax, 1
    mov cx, 90
    push dx
    mul cx
    pop dx
    add ax, 5
    add ax, 60 * 90 / 4             ; ...and the box starts at row 60: 15 rows
                                    ; of each bank
    jmp short .bank
.text:
    mov cx, 160                     ; C160: 80 words a character row, no bank
    mul cx
    inc ax                          ; ...and the attribute is the odd byte
    xor dx, dx
    jmp short .bank
.linear:
    mov cx, PB_STRIDE               ; Mode X: 80 bytes a plane row, linear
    mul cx
    xor dx, dx
.bank:
    mov cl, 13
    shl dx, cl                      ; bank * 0x2000
    add ax, dx
    mov [di], ax
    inc di
    inc di
    inc bx
    cmp bx, PB_ROWS
    jb .row
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pb_b_blit1 - the 512 x 80 band onto the desktop (WIN1's present)
pb_b_blit1:
    push bp
    push es
    mov si, pb_shadow
    mov bp, PB_STRIDE
    mov ax, [pb_bx]
    mov bx, [pb_by]
    mov cx, PB_VIEWB * 8
    mov dx, PB_ROWS
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    mov byte [pb_blitcf], 0
    jnc .ok
    mov byte [pb_blitcf], 1
.ok:
    pop es
    pop bp
    ret

; =============================================================================
; (e) the C160 text backend: the expanding blit and the direct store
; =============================================================================

; pb_b_expand - 48 bytes x 80 rows of packed nibbles laid at every other
;               address: apps/skies' cs_blit.expand, verbatim in shape
pb_b_expand:
    push es
    push bp
    mov es, [pb_fsi + FSI_SEG]
    mov bx, pb_devoff
    mov si, pb_shadow
    mov bp, PB_ROWS
    cld
.row:
    mov di, [bx]                    ; the row's attribute byte (odd)
    inc bx
    inc bx
    mov cx, 48 / 2
.e:
    lodsw
    stosb
    inc di
    xchg al, ah
    stosb
    inc di
    loop .e
    add si, PB_STRIDE - 48
    dec bp
    jnz .row
    pop bp
    pop es
    ret

; pb_b_st160 - the store ladder at the attribute stride, 80 rows
pb_b_st160:
    push ds
    mov ds, [pb_fsi + FSI_SEG]      ; DS = the destination, the scaler's way
    mov di, 1 - 256
    mov al, 0x55
%assign PBR 0
%rep PB_ROWS
    mov [di + 256 + PBR * 160], al
%assign PBR PBR+1
%endrep
    pop ds
    ret

; the 160x100x16 retime (SPEC.md 88.15.2): six 6845 writes with video off
pb_c160_crtc:
    db  4, 127
    db  5,   6
    db  6, 100
    db  7, 112
    db  9,   1
    db 10, 0x20
PB_C160_NREG equ ($ - pb_c160_crtc) / 2

pb_c160_mode:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov dx, 0x3D8
    mov al, 0x01                    ; 80-column text, blink off, VIDEO OFF
    out dx, al
    cld                             ; ...BEFORE the lodsw below: the first cut
                                    ; had it after this loop, guarding only
                                    ; the rep stosw, and the CRTC writes were
                                    ; right by the luck of an earlier cld
    mov si, pb_c160_crtc
    mov cx, PB_C160_NREG
    mov dx, 0x3D4
.r:
    lodsw
    out dx, al
    inc dx
    xchg al, ah
    out dx, al
    dec dx
    loop .r
    mov dx, 0x3D9
    xor al, al
    out dx, al
    mov es, [pb_fsi + FSI_SEG]
    xor di, di
    mov ax, 0x00DE                  ; every cell the right half block, black
    mov cx, 160 * 100 / 2
    rep stosw
    mov dx, 0x3D8
    mov al, 0x09                    ; ...and the picture back on
    out dx, al
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; (f) the textured row: a texel load and a store, eighty rows
; =============================================================================

; The generated scaler's row (97.3): `mov al, [es:si + v]` then the store,
; ES = the byte-texture set, DS = the destination. v is the texel the row
; reads, 0..31 here; the generator writes the real v per row.
pb_b_tx:
    push es
    push ds
    pop es
    mov si, pb_tex
    mov di, pb_shadow - 256
%assign PBR 0
%rep PB_ROWS
    mov al, [es:si + ((PBR * 32) / PB_ROWS)]
    mov [di + 256 + PBR * PB_STRIDE], al
%assign PBR PBR+1
%endrep
    pop es
    ret

; ...and with an xlat between them: BX = a 256-byte table in DS
pb_b_txx:
    push es
    push ds
    pop es
    mov si, pb_tex
    mov di, pb_shadow - 256
    mov bx, pb_xlat
%assign PBR 0
%rep PB_ROWS
    mov al, [es:si + ((PBR * 32) / PB_ROWS)]
    xlat
    mov [di + 256 + PBR * PB_STRIDE], al
%assign PBR PBR+1
%endrep
    pop es
    ret

; ...and with the odd-row dither phase turned by `ror al, 1` x 2 - CGA
; 320x200x4's N, one 2-bit pixel (97.3): the rotate graft 1 puts after the
; even rows' stores, priced per texel. Hercules and WIN1 turn three, C160
; four; the row's delta over pb_b_tx is one rotate's cost times two
pb_b_txr2:
    push es
    push ds
    pop es
    mov si, pb_tex
    mov di, pb_shadow - 256
%assign PBR 0
%rep PB_ROWS
    mov al, [es:si + ((PBR * 32) / PB_ROWS)]
    ror al, 1
    ror al, 1
    mov [di + 256 + PBR * PB_STRIDE], al
%assign PBR PBR+1
%endrep
    pop es
    ret

; ...and the DUAL-PHASE texel: `mov ax, [es:si + 2v]` with AL the even rows'
; byte and AH the odd rows' (part 4 holds two bytes a texel, 97.4), the store
; from AL or AH by the row's parity - no rotate on any row. The word load is
; the byte load plus one bus cycle on the 8088; the delta over pb_b_tx is
; what the escape from the rotate costs a texel
pb_b_txw:
    push es
    push ds
    pop es
    mov si, pb_tex
    mov di, pb_shadow - 256
%assign PBR 0
%rep PB_ROWS
    mov ax, [es:si + ((PBR * 32) / PB_ROWS) * 2]
%if PBR & 1
    mov [di + 256 + PBR * PB_STRIDE], ah
%else
    mov [di + 256 + PBR * PB_STRIDE], al
%endif
%assign PBR PBR+1
%endrep
    pop es
    ret

; ...and the phase turned by ONE `ror al, cl` with CL = 3 - the Hercules and
; WIN1 form, where the 2x2 dither of a 1bpp byte is turned by three bits...
; (WL_SCALE.C's dithershift for the mono adapters, read for technique): one
; two-byte instruction at 8 + 4/bit, against three `ror al, 1`s. The delta
; over pb_b_tx is what the mono backends' phase costs a texel
pb_b_txr3:
    push es
    push ds
    pop es
    mov si, pb_tex
    mov di, pb_shadow - 256
    mov cl, 3
%assign PBR 0
%rep PB_ROWS
    mov al, [es:si + ((PBR * 32) / PB_ROWS)]
    ror al, cl
    mov [di + 256 + PBR * PB_STRIDE], al
%assign PBR PBR+1
%endrep
    pop es
    ret

; ...and the Resolution: Low res scaler's row (97.3): the byte load, `mov
; ah, al` so the two shadow bytes of a column carry one texel, and the WORD
; store. The delta over pb_b_tx less the word store's delta over the byte
; store (rows 16 and 0) is what the duplication costs; the odd-row phase
; then costs the rotate PLUS a second `mov ah, al`, which tools/pxsframe.py
; charges from these rows
pb_b_txl:
    push es
    push ds
    pop es
    mov si, pb_tex
    mov di, pb_shadow - 256
%assign PBR 0
%rep PB_ROWS
    mov al, [es:si + ((PBR * 32) / PB_ROWS)]
    mov ah, al
    mov [di + 256 + PBR * PB_STRIDE], ax
%assign PBR PBR+1
%endrep
    pop es
    ret

; =============================================================================
; (j) the simulation: one tick's step, the shape wave 3 writes
; =============================================================================

; pb_sim_cell - AX = x, DX = y (Q8.8) -> BX = the H map's cell index
;               [y << 6 | x]. Clobbers AX. Five instructions and no shift
;               by CL: the tile of y is its high byte, and (y & FF00) >> 2 is
;               that tile times 64
pb_sim_cell:
    mov bx, dx
    xor bl, bl
    shr bx, 1
    shr bx, 1                       ; y tile << 6
    mov al, ah
    xor ah, ah
    or bx, ax                       ; | x tile
    ret

; pb_sim_solid2 - AX = x, DX = y: the two cells at (x, y - r) and (x, y + r)
;                 - an actor's leading edge across an x move. ZF clear if
;                 solid. Clobbers AX, BX, CX; DX preserved
pb_sim_solid2:
    push dx
    push ax
    sub dx, PB_RADIUS
    call pb_sim_cell
    mov cl, [PB_MAP + bx]
    pop ax
    add dx, PB_RADIUS * 2
    call pb_sim_cell
    or cl, [PB_MAP + bx]
    pop dx
    test cl, 1
    ret

; pb_sim_solid2x - ...and (x - r, y), (x + r, y): the edge across a y move
pb_sim_solid2x:
    push ax
    sub ax, PB_RADIUS
    call pb_sim_cell
    mov cl, [PB_MAP + bx]
    pop ax
    add ax, PB_RADIUS
    call pb_sim_cell
    or cl, [PB_MAP + bx]
    test cl, 1
    ret

; pb_sim_solid4 - AX = x, DX = y: the four corners of the radius - the
;                 player's per-axis test (97.8: radius 0.25), two pairs
pb_sim_solid4:
    push ax
    sub ax, PB_RADIUS
    call pb_sim_solid2
    pop ax
    jnz .out
    add ax, PB_RADIUS
    call pb_sim_solid2
.out:
    ret

; pb_b_sim - one tick of the simulation (97.8, docs/plans/PIXELSTEIN-PLAN.md
;            1.5-1.7), written the straightforward way wave 3 would first
;            write it. A SHAPE, measured, and not the package's code - the
;            plan's counts with a plausible body each:
;
;   the player   a step along the heading (cos, sin x speed, two MUL14),
;                then x against four corner cells, then y against four
;   the actors   [PB_PS_NACT] of them: a state dispatch; the Manhattan
;                distance to the player (no mul); a timer; and by state,
;                round-robin - CHASE: a step toward the player, two
;                leading-edge cells an axis, then the line-of-sight walk;
;                PATROL: the step alone; STAND: the mark test - a stander
;                looks only when a ray crossed its cell this frame (spotvis
;                == gen), which every other one passes and walks; DEAD:
;                skipped. The walk is the 1992 engine's CheckLine's shape:
;                a slope divide per axis, then ONE TILE a step along the
;                major axis (12..19 here: the actors stand 12-19 tiles
;                off), <= 32 steps, one map read each
;   the doors    [PB_PS_NDOOR] of them, round-robin: shut, shut, opening
;                (pos += 16 a tick, then OPEN with a timer), open (the
;                timer, then the doorway test against the player's cell)
;
;            Two rows run it: the plan's CAPS (32 actors, 64 doors: 16
;            movers, 12 walks a tick) and E1M1's own counts (7 actors, 22
;            doors: 4 movers, 4 walks). On an open map, so nothing stops
;            early. State advances across iterations (a door opens, a chaser
;            closes in) and the work of a tick does not change with it.
;            Needs the claim: the map, the marks and the records live there
pb_b_sim:
    push bp
    push ds
    mov ds, [cs:pb_claim]
    ; --- the player: the step vector, then x, then y ----------------------
    mov ax, [PB_W_COS]
    mov bx, PB_RADIUS               ; the speed: a quarter tile a tick (the cap)
    PB_MUL14
    mov bp, ax                      ; the x step
    mov ax, [PB_W_SIN]
    mov bx, PB_RADIUS
    PB_MUL14
    mov si, ax                      ; the y step
    mov ax, [PB_T_SIM + PB_PS_X]
    add ax, bp
    mov dx, [PB_T_SIM + PB_PS_Y]
    push ax
    call pb_sim_solid4
    pop ax
    jnz .pxblk
    mov [PB_T_SIM + PB_PS_X], ax
.pxblk:
    mov ax, [PB_T_SIM + PB_PS_X]
    mov dx, [PB_T_SIM + PB_PS_Y]
    add dx, si
    push dx
    call pb_sim_solid4
    pop dx
    jnz .pyblk
    mov [PB_T_SIM + PB_PS_Y], dx
.pyblk:
    ; --- the actors --------------------------------------------------------
    mov si, PB_T_ACT
    mov bp, [PB_T_SIM + PB_PS_NACT]
.act:
    mov al, [si + PB_A_STATE]
    cmp al, PB_AS_DEAD
    jne .alive
    jmp .anext                      ; (the body is past a short jump's reach)
.alive:
    mov ax, [PB_T_SIM + PB_PS_X]    ; |dx| + |dy| to the player, Q8.8
    sub ax, [si + PB_A_X]
    jns .adx
    neg ax
.adx:
    mov dx, [PB_T_SIM + PB_PS_Y]
    sub dx, [si + PB_A_Y]
    jns .ady
    neg dx
.ady:
    add ax, dx
    mov [si + PB_A_DIST], ax
    dec word [si + PB_A_TIMER]      ; the animation timer
    jnz .atimer
    mov word [si + PB_A_TIMER], 30
    inc byte [si + PB_A_FRAME]
.atimer:
    cmp byte [si + PB_A_STATE], PB_AS_STAND
    jne .amove
    jmp .astand
.amove:
    ; CHASE and PATROL: a step toward the target, per axis, two leading cells
    mov ax, [si + PB_A_TX]
    sub ax, [si + PB_A_X]
    mov cx, PB_ASTEP
    mov bx, PB_RADIUS
    jns .asx
    neg cx
    neg bx
.asx:
    mov ax, [si + PB_A_X]
    add ax, cx                      ; the new x
    push ax
    add ax, bx                      ; ...and its leading edge
    mov dx, [si + PB_A_Y]
    call pb_sim_solid2
    pop ax
    jnz .axblk
    mov [si + PB_A_X], ax
.axblk:
    mov ax, [si + PB_A_TY]
    sub ax, [si + PB_A_Y]
    mov cx, PB_ASTEP
    mov bx, PB_RADIUS
    jns .asy
    neg cx
    neg bx
.asy:
    mov dx, [si + PB_A_Y]
    add dx, cx
    push dx
    add dx, bx
    mov ax, [si + PB_A_X]
    call pb_sim_solid2x
    pop dx
    jnz .ayblk
    mov [si + PB_A_Y], dx
.ayblk:
    cmp byte [si + PB_A_STATE], PB_AS_CHASE
    jne .anext
    jmp short .alos
.astand:
    mov ax, [si + PB_A_X]           ; a stander looks only when marked
    mov dx, [si + PB_A_Y]
    call pb_sim_cell
    mov al, [PB_SPOT + bx]
    cmp al, [PB_T_SIM + PB_PS_GEN]
    jne .anext
.alos:
    ; line of sight, CheckLine's shape: the slope per axis by a divide, then
    ; a tile a step along the major axis, <= 32 steps, a map read each
    mov ax, [PB_T_SIM + PB_PS_X]
    sub ax, [si + PB_A_X]           ; dx, Q8.8
    mov cx, [PB_T_SIM + PB_PS_Y]
    sub cx, [si + PB_A_Y]           ; dy
    mov bx, ax
    or bx, bx
    jns .lax
    neg bx
.lax:
    mov dx, cx
    or dx, dx
    jns .lay
    neg dx
.lay:
    cmp bx, dx
    jae .lmaj
    mov bx, dx
.lmaj:
    mov bl, bh
    xor bh, bh                      ; the major axis, in tiles
    cmp bx, PB_LOSN
    jbe .lcap
    mov bx, PB_LOSN
.lcap:
    or bx, bx
    jnz .lnz
    inc bx
.lnz:
    push bx                         ; the step count
    cwd
    idiv bx
    mov [si + PB_A_LSX], ax         ; the x step, Q8.8 a tile of the major
    mov ax, cx
    cwd
    idiv bx
    mov [si + PB_A_LSY], ax
    pop cx
    mov di, [si + PB_A_X]
    mov dx, [si + PB_A_Y]
.los:
    add di, [si + PB_A_LSX]
    add dx, [si + PB_A_LSY]
    mov bx, dx
    xor bl, bl
    shr bx, 1
    shr bx, 1
    mov ax, di
    mov al, ah
    xor ah, ah
    or bx, ax
    test byte [PB_MAP + bx], 1
    jnz .unseen
    loop .los
    mov byte [si + PB_A_SEES], 1
    jmp short .anext
.unseen:
    mov byte [si + PB_A_SEES], 0
.anext:
    add si, PB_ASZ
    dec bp
    jnz .act
    ; --- the doors --------------------------------------------------------
    mov si, PB_T_DOOR
    mov cx, [PB_T_SIM + PB_PS_NDOOR]
.door:
    mov al, [si + PB_D_STATE]
    or al, al
    jz .dnext                       ; shut: nothing to do
    cmp al, PB_DS_OPEN
    je .dopen
    cmp al, PB_DS_CLOSING
    je .dclosing
    mov ax, [si + PB_D_POS]         ; OPENING
    add ax, 16
    mov [si + PB_D_POS], ax
    cmp ax, 256
    jb .dnext
    mov byte [si + PB_D_STATE], PB_DS_OPEN
    mov word [si + PB_D_TIMER], 90
    jmp short .dnext
.dopen:
    dec word [si + PB_D_TIMER]
    jnz .dnext
    mov ax, [PB_T_SIM + PB_PS_X]    ; a body in the doorway? the player's
    mov dx, [PB_T_SIM + PB_PS_Y]    ; cell against the door's
    call pb_sim_cell
    cmp bx, [si + PB_D_CELL]
    jne .dclose
    mov word [si + PB_D_TIMER], 18  ; held: a second more
    jmp short .dnext
.dclose:
    mov byte [si + PB_D_STATE], PB_DS_CLOSING
    jmp short .dnext
.dclosing:
    mov ax, [si + PB_D_POS]
    sub ax, 16
    mov [si + PB_D_POS], ax
    ja .dnext
    mov byte [si + PB_D_STATE], PB_DS_SHUT
.dnext:
    add si, PB_DSZ
    loop .door
    pop ds
    pop bp
    ret

; pb_sim_init - AX = actors, BX = doors. The player at (8.5, 8.5) on the
;               open map; the actors in a block of open cells 12-19 tiles
;               off, states 0..3 round-robin, their target the player, every
;               other stander's cell marked; the doors in a row of cells,
;               states shut/shut/opening/open round-robin. After pb_mapclear
pb_sim_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [pb_claim]
    mov [es:PB_T_SIM + PB_PS_NACT], ax
    mov [es:PB_T_SIM + PB_PS_NDOOR], bx
    mov word [es:PB_T_SIM + PB_PS_X], 8 * 256 + 128
    mov word [es:PB_T_SIM + PB_PS_Y], 8 * 256 + 128
    mov byte [es:PB_T_SIM + PB_PS_GEN], 1
    mov di, PB_T_ACT
    xor cx, cx                      ; CX = i
.act:
    mov ax, cx
    and ax, 7
    add ax, 20
    mov ah, al
    mov al, 128                     ; x = (20 + (i & 7)).5
    mov [es:di + PB_A_X], ax
    mov ax, cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shl ax, 1
    add ax, 20
    mov ah, al
    mov al, 128                     ; y = (20 + (i >> 3) * 2).5
    mov [es:di + PB_A_Y], ax
    mov ax, cx
    and al, 3
    mov [es:di + PB_A_STATE], al
    mov byte [es:di + PB_A_KIND], 0
    mov ax, cx
    and ax, 15
    inc ax
    mov [es:di + PB_A_TIMER], ax
    mov word [es:di + PB_A_TX], 8 * 256 + 128
    mov word [es:di + PB_A_TY], 8 * 256 + 128
    mov word [es:di + PB_A_DIST], 0
    mov byte [es:di + PB_A_FRAME], 0
    mov byte [es:di + PB_A_SEES], 0
    test cl, 7                      ; every other stander (i & 7 == 0): marked
    jnz .unmarked
    mov ax, [es:di + PB_A_X]
    mov dx, [es:di + PB_A_Y]
    call pb_sim_cell                ; (registers only)
    mov byte [es:PB_SPOT + bx], 1
.unmarked:
    add di, PB_ASZ
    inc cx
    cmp cx, [es:PB_T_SIM + PB_PS_NACT]
    jb .act
    mov di, PB_T_DOOR
    xor cx, cx
.door:
    mov ax, cx
    and ax, 15
    add ax, 10                      ; x = 10 + (i & 15)
    mov bx, cx
    shr bx, 1
    shr bx, 1
    shr bx, 1
    shr bx, 1
    add bx, 30                      ; y = 30 + (i >> 4)
    mov bh, bl
    xor bl, bl
    shr bx, 1
    shr bx, 1
    or ax, bx                       ; the cell index
    mov [es:di + PB_D_CELL], ax
    mov word [es:di + PB_D_POS], 0
    mov bx, cx
    and bx, 3
    mov al, [pb_s_dstate + bx]
    mov [es:di + PB_D_STATE], al
    mov byte [es:di + PB_D_LOCK], 0
    mov ax, cx
    add ax, 90
    mov [es:di + PB_D_TIMER], ax
    add di, PB_DSZ
    inc cx
    cmp cx, [es:PB_T_SIM + PB_PS_NDOOR]
    jb .door
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pb_s_dstate: db PB_DS_SHUT, PB_DS_SHUT, PB_DS_OPENING, PB_DS_OPEN

; =============================================================================
; (g) the input pass
; =============================================================================

pb_b_keys:
    mov al, KSC_UP
    call OSAPI_KEY_DOWN
    mov al, KSC_DOWN
    call OSAPI_KEY_DOWN
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN
    mov al, KSC_RIGHT
    call OSAPI_KEY_DOWN
    mov al, 0x1E                    ; A
    call OSAPI_KEY_DOWN
    mov al, 0x20                    ; D
    call OSAPI_KEY_DOWN
    mov al, KSC_SPACE
    call OSAPI_KEY_DOWN
    mov al, 0x2A                    ; left shift
    call OSAPI_KEY_DOWN
    ret

; =============================================================================
; (h) one scaler-set generation, and one byte-texture transpose
; =============================================================================

; pb_b_gen - emit the textured scaler set (97.3) into the claim: heights 2
;            to 60 by two, 63 to 78 by three and 84 to 120 by six - 43
;            scalers, PIXELSTEIN-PLAN 13's ninth graft; each row
;            `mov al, [es:si + v]` (4 bytes) + `mov [di + r*80], al` (4), the
;            texel v stepped by a Bresenham over 32 texels, then a NEAR
;            `ret` (0xC3): the column driver lives in the same part and
;            near-calls every scaler, and the frame's one far call is into
;            the driver (97.3). The first cut emitted 0xCB, the 1992
;            engine's far return, which a near call would have popped a
;            segment for. The shape and the count are the generator's (the
;            dual-phase word load of 97.3 is the same 4 bytes as the byte
;            load, so the set's size is this set's); the bytes are
;            pxsgen.py's business in wave 2.
pb_b_gen:
    push es
    push bp                         ; a temp below; bl_time does not bank it
    mov es, [pb_claim]
    cld
    mov di, PB_GENOFS
    mov bx, 2                       ; BX = the height
.scaler:
    xor cx, cx                      ; CX = the row
    xor dx, dx                      ; DL = v, DH = the Bresenham accumulator
.row:
    mov ax, 0x8A26                  ; es: mov al, [si + disp8]
    stosw
    mov al, 0x44
    stosb
    mov al, dl                      ; v
    stosb
    mov ax, 0x8588                  ; mov [di + disp16], al
    stosw
    mov ax, cx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; row * 16
    mov bp, ax
    shl ax, 1
    shl ax, 1                       ; row * 64
    add ax, bp                      ; row * 80
    stosw
    add dh, 32                      ; the texel step: 32 texels over BX rows
.bres:
    cmp dh, bl
    jb .next
    sub dh, bl
    inc dl
    jmp short .bres
.next:
    inc cx
    cmp cx, bx
    jb .row
    mov al, 0xC3                    ; ret - near, the driver is in the part
    stosb
    cmp bx, 60
    jae .by3
    inc bx
    inc bx
    jmp short .scaler
.by3:
    cmp bx, 78
    jae .by6
    add bx, 3
    jmp short .scaler
.by6:
    add bx, 6
    cmp bx, 120
    jbe .scaler
    mov [cs:pb_genend], di
    pop bp
    pop es
    ret

; pb_b_bt - the transpose: 15 masters x 2 shades, 32x32 4bpp row-major
;           masters to byte-a-texel column-major sets through an ink table
;           (97.4's bt_build). ONE master load serves TWO texels - the byte
;           holds columns u and u+1 of one row, and the pair of output
;           columns is written together, [di] and [di + 32] - so nothing
;           branches on a column's parity and the ink table's BX is set once
;           a shade. The first cut of this loop reloaded the master byte
;           per texel and branched per texel, at 145 clk a texel; this is
;           the shape wave 2's bt_build takes.
pb_b_bt:
    push es
    push bp
    mov es, [pb_claim]
    cld
    mov di, PB_GENOFS
    xor bp, bp                      ; BP = material * 512 (the master)
.mat:
    mov bx, pb_xlat                 ; BX = the ink table (shade 0)
    mov ch, PB_SHADES
.shade:
    mov si, bp
    add si, PB_C_MAST               ; ES:SI = the master's row 0, byte 0
    mov dh, 16                      ; DH = column pairs left
.pair:
    push si
    mov cl, 32                      ; CL = v
.tx:
    mov al, [es:si]                 ; the pair: u in the high nibble
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlat
    mov [es:di], al                 ; column u, texel v
    mov al, ah
    and al, 15
    xlat
    mov [es:di + 32], al            ; column u + 1, texel v
    inc di
    add si, 16                      ; the master's next row
    dec cl
    jnz .tx
    pop si
    inc si                          ; the next byte pair of row 0
    add di, 32                      ; ...past column u + 1, already written
    dec dh
    jnz .pair
    add bx, 16                      ; shade 1's table: the next 16 inks
    dec ch
    jnz .shade
    add bp, 512
    cmp bp, PB_MASTERS * 512
    jb .mat
    pop bp
    pop es
    ret

; =============================================================================
; the bracket: the mode the game takes on this adapter, then the VRAM rows
; =============================================================================

; pb_fsx - OSAPI_FSX_RUN's proc: SI = our window, ES = KERNEL_SEG, DS = ours
pb_fsx:
    push ds
    pop es
    mov al, [pb_fsxm]
    mov di, pb_fsi
    call OSAPI_FSX_MODE
    jc .out
    mov byte [pb_inmode], 1
    cmp byte [pb_fsi + FSI_MODE], FSXM_MODEX    ; the mode the kernel echoed
                                    ; back (os88api.inc's contract is CF and
                                    ; the FSI block; AL is not promised)
    jne .nomask
    mov dx, 0x3C4                   ; Mode X: every plane, so a byte store
    mov ax, 0x0F02                  ; writes four pixels (apps/skies' cs_mxmask)
    out dx, ax
.nomask:
    call pb_devrows
    mov si, pb_devoff               ; the GAME mode's table, kept: on a CGA
    mov di, pb_devoff1              ; the C160 retime below recomputes
    mov cx, PB_ROWS                 ; pb_devoff in place, and the harness
    cld                             ; checks both (ES = DS here)
    rep movsw
    mov word [bl_n], PB_N
    mov word [bl_body], pb_b_st_vram
    mov si, pb_r_st_vram
    xor al, al
    call bl_run
    mov bx, 12
    call pb_bank
    mov word [bl_body], pb_b_stw_vram
    mov si, pb_r_stw_vram
    xor al, al
    call bl_run
    mov bx, 17
    call pb_bank
    mov word [bl_body], pb_b_copy
    mov si, pb_r_copy
    xor al, al
    call bl_run
    mov bx, 13
    call pb_bank
    ; --- a genuine CGA: the 160x100x16 text mode's two rows (88.15) ---------
    cmp byte [pb_vkind], VID_CGA
    jne .out
    mov al, FSXM_TEXT80
    mov di, pb_fsi
    call OSAPI_FSX_MODE
    jc .out
    call pb_c160_mode
    call pb_devrows
    mov word [bl_body], pb_b_expand
    mov si, pb_r_expand
    xor al, al
    call bl_run
    mov bx, 14
    call pb_bank
    mov word [bl_body], pb_b_st160
    mov si, pb_r_st160
    xor al, al
    call bl_run
    mov bx, 15
    call pb_bank
.out:
    ret

; pb_adapter - which mode the bracket takes: Mode X on a VGA, Hercules on a
;              Hercules, CGA 320x200x4 on a CGA or an EGA - asked of OUR
;              window (SPEC.md 39.18.2)
pb_adapter:
    push ax
    push bx
    push dx
    mov bx, [pb_win]
    call OSAPI_FSX_CAPS             ; AX = the mask, DL = the kind
    mov [pb_vkind], dl
    mov [pb_caps], ax
    mov byte [pb_fsxm], FSXM_CGA320
    cmp dl, VID_HERC
    jne .nh
    mov byte [pb_fsxm], FSXM_HERC
.nh:
    cmp dl, VID_VGA
    jne .nv
    mov byte [pb_fsxm], FSXM_MODEX
.nv:
    pop dx
    pop bx
    pop ax
    ret

; pb_bank - BX = the result row: bank [bl_lastus] and the flag into pb_res
pb_bank:
    push ax
    push bx
    push dx
    push si
    mov si, bx
    shl si, 1
    shl si, 1
    mov ax, [bl_lastus]
    mov dx, [bl_lastus + 2]
    mov [pb_res + si], ax
    mov [pb_res + si + 2], dx
    mov al, [bl_lscr + BL_C_FLAG]
    or al, al
    jnz .flag
    mov al, ' '
.flag:
    mov [pb_resf + bx], al
    pop si
    pop dx
    pop bx
    pop ax
    ret

; pb_skip - BX = the row: mark it not measured, with a line saying why
pb_skip:
    push ax
    push di
    mov byte [pb_resf + bx], '-'
    mov di, pb_s_skipped
    call bl_kvs
    pop di
    pop ax
    ret

; =============================================================================
; pb_run - every row, in order, into the report and into pb_res
; =============================================================================
pb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, [pb_win]
    call OSAPI_WM_CONTENT
    mov [pb_cx], ax
    mov [pb_cy], dx
    mov bx, [pb_win]
    call OSAPI_WM_GEOM              ; the LIVE content size (CGA: ~136 rows)
    mov [pb_cw], cx
    mov [pb_ch], dx
    mov ax, [pb_cx]
    add ax, 7
    and ax, 0xFFF8
    mov [pb_bx], ax                 ; the blit's x, on the byte grid
    mov ax, [pb_cy]
    add ax, [pb_ch]
    sub ax, PB_ROWS + 1
    mov [pb_by], ax                 ; ...and its y: the last 80 content rows
    call pb_adapter

    mov word [pb_done], 0
    mov byte [pb_inmode], 0
    mov word [bl_nrow], 0           ; a re-run replaces the report rather than
    mov word [bl_used], 0           ; appending to it (tests/gfxbench's line)
    mov word [bl_top], 0
    mov byte [bl_full], 0
    call bl_blank
    mov si, pb_s_title
    call bl_sline
    call bl_head
    call bl_baseline

    ; --- (a) the store, RAM ----------------------------------------------
    mov si, pb_s_hdra
    call bl_sline
    mov word [bl_n], PB_N
    mov word [bl_body], pb_b_st_ram
    mov si, pb_r_st_ram
    xor al, al
    call bl_run
    mov bx, 0
    call pb_bank
    mov word [bl_body], pb_b_stw_ram
    mov si, pb_r_stw_ram
    xor al, al
    call bl_run
    mov bx, 16
    call pb_bank

    ; --- (b) the static ladder -------------------------------------------
    mov word [bl_body], pb_b_lad80
    mov si, pb_r_lad80
    xor al, al
    call bl_run
    mov bx, 1
    call pb_bank
    mov word [bl_body], pb_b_lad40
    mov si, pb_r_lad40
    xor al, al
    call bl_run
    mov bx, 2
    call pb_bank

    ; --- (c) the DDA -----------------------------------------------------
    mov si, pb_s_hdrc
    call bl_sline
    cmp word [pb_claim], 0
    jne .dda
    mov bx, 3
    mov si, pb_r_dda10
    call pb_skip
    mov bx, 4
    mov si, pb_r_dda20
    call pb_skip
    mov bx, 5
    mov si, pb_r_dda10a
    call pb_skip
    mov bx, 18                      ; ...and the scaffold row, which the
    mov si, pb_r_ddasc              ; first cut left at 0 = '?' here, so a
    call pb_skip                    ; machine with no claim FAILED the row
    jmp .texel                      ; the other five were marked skipped on
.dda:
    mov word [bl_n], PB_NDDA
    call pb_tables
    mov word [bl_body], pb_b_ddasc  ; the scaffold alone, first
    mov si, pb_r_ddasc
    xor al, al
    call bl_run
    mov bx, 18
    call pb_bank
    call pb_heading45
    call pb_mapclear
    mov ax, 13                      ; the tenth crossing enters (13,13)
    mov bx, 13
    call pb_wall
    mov word [bl_body], pb_dda_col
    mov si, pb_r_dda10
    xor al, al
    call bl_run
    mov bx, 3
    call pb_bank
    mov al, [pb_hitside]
    mov [pb_side10], al
    call pb_mapclear
    mov ax, 18                      ; ...and the twentieth (18,18)
    mov bx, 18
    call pb_wall
    mov word [bl_body], pb_dda_col
    mov si, pb_r_dda20
    xor al, al
    call bl_run
    mov bx, 4
    call pb_bank
    mov al, [pb_hitside]
    mov [pb_side20], al
    call pb_headinga                ; near-axial: nine V passes in ten
    call pb_mapclear
    mov ax, 17                      ; x = 17, rows 8..10
    mov bx, 8
    call pb_wall
    inc bx
    call pb_wall
    inc bx
    call pb_wall
    mov word [bl_body], pb_dda_col
    mov si, pb_r_dda10a
    xor al, al
    call bl_run
    mov bx, 5
    call pb_bank
    ; the derived row: (20 crossings - 10 crossings) / 10, hundredths of a us
    mov ax, [pb_res + 4 * 4]
    mov dx, [pb_res + 4 * 4 + 2]
    sub ax, [pb_res + 3 * 4]
    sbb dx, [pb_res + 3 * 4 + 2]
    jc .nodd
    mov cx, 10
    call bl_div32
    mov si, pb_r_ddax
    mov cx, 9
    call bl_kv
.nodd:

.texel:
    ; --- (f) the texel row -----------------------------------------------
    mov si, pb_s_hdrf
    call bl_sline
    mov word [bl_n], PB_N
    mov word [bl_body], pb_b_tx
    mov si, pb_r_tx
    xor al, al
    call bl_run
    mov bx, 6
    call pb_bank
    mov word [bl_body], pb_b_txx
    mov si, pb_r_txx
    xor al, al
    call bl_run
    mov bx, 7
    call pb_bank
    mov word [bl_body], pb_b_txr2
    mov si, pb_r_txr2
    xor al, al
    call bl_run
    mov bx, 20
    call pb_bank
    mov word [bl_body], pb_b_txw
    mov si, pb_r_txw
    xor al, al
    call bl_run
    mov bx, 19
    call pb_bank
    mov word [bl_body], pb_b_txr3
    mov si, pb_r_txr3
    xor al, al
    call bl_run
    mov bx, 22
    call pb_bank
    mov word [bl_body], pb_b_txl
    mov si, pb_r_txl
    xor al, al
    call bl_run
    mov bx, 23
    call pb_bank

    ; --- (i) the hit, (j) the simulation ---------------------------------
    mov si, pb_s_hdri
    call bl_sline
    cmp word [pb_claim], 0
    jne .hit
    mov bx, 21
    mov si, pb_r_hit
    call pb_skip
    mov bx, 24
    mov si, pb_r_sim
    call pb_skip
    mov bx, 25
    mov si, pb_r_sim1
    call pb_skip
    jmp .keys
.hit:
    mov word [bl_n], PB_NDDA
    mov word [bl_body], pb_b_hit
    mov si, pb_r_hit
    xor al, al
    call bl_run
    mov bx, 21
    call pb_bank
    mov si, pb_r_hith
    mov ax, [pb_hith]
    xor dx, dx
    mov cx, 9
    call bl_kv
    call pb_mapclear                ; the open map: nothing stops a walk early
    mov ax, PB_NACT                 ; the plan's caps
    mov bx, PB_NDOOR
    call pb_sim_init
    mov word [bl_n], PB_N
    mov word [bl_body], pb_b_sim
    mov si, pb_r_sim
    xor al, al
    call bl_run
    mov bx, 24
    call pb_bank
    mov ax, PB_NACT1                ; ...and E1M1's own counts
    mov bx, PB_NDOOR1
    call pb_sim_init
    mov word [bl_body], pb_b_sim
    mov si, pb_r_sim1
    xor al, al
    call bl_run
    mov bx, 25
    call pb_bank
.keys:

    ; --- (g) the keys ----------------------------------------------------
    mov word [bl_n], PB_N
    mov word [bl_body], pb_b_keys
    mov si, pb_r_keys
    xor al, al
    call bl_run
    mov bx, 8
    call pb_bank

    ; --- (d) the desktop blit --------------------------------------------
    mov si, pb_s_hdrd
    call bl_sline
    mov word [bl_body], pb_b_blit1
    mov si, pb_r_blit1
    xor al, al
    call bl_run
    mov bx, 9
    call pb_bank
    cmp byte [pb_blitcf], 0
    je .blitok
    mov byte [pb_resf + 9], 'x'     ; refused: the row measured a refusal
.blitok:

    ; --- (h) the generation and the transpose, method T -----------------
    mov si, pb_s_hdrh
    call bl_sline
    cmp word [pb_claim], 0
    jne .gen
    mov bx, 10
    mov si, pb_r_gen
    call pb_skip
    mov bx, 11
    mov si, pb_r_bt
    call pb_skip
    jmp short .fsx
.gen:
    mov word [bl_n], 8
    mov word [bl_body], pb_b_gen
    mov si, pb_r_gen
    mov al, 1                       ; method T: hundreds of milliseconds
    call bl_run
    mov bx, 10
    call pb_bank
    mov ax, [pb_genend]
    sub ax, PB_GENOFS
    xor dx, dx
    mov si, pb_r_genb
    mov cx, 9
    call bl_kv
    mov word [bl_n], 4
    mov word [bl_body], pb_b_bt
    mov si, pb_r_bt
    mov al, 1
    call bl_run
    mov bx, 11
    call pb_bank

.fsx:
    ; --- the bracket: the VRAM rows in the game's mode --------------------
    mov si, pb_s_hdrv
    call bl_sline
    mov byte [pb_resf + 12], '-'
    mov byte [pb_resf + 13], '-'
    mov byte [pb_resf + 14], '-'
    mov byte [pb_resf + 15], '-'
    mov byte [pb_resf + 17], '-'
    mov ax, pb_fsx
    mov bx, [pb_win]
    xor cx, cx
    call OSAPI_FSX_RUN
    jnc .ran
    mov si, pb_r_st_vram
    mov di, pb_s_refused
    call bl_kvs
.ran:
    cmp byte [pb_inmode], 0
    jne .modeok
    mov si, pb_r_st_vram
    mov di, pb_s_nomode
    call bl_kvs
.modeok:
    mov si, pb_r_mode
    mov al, [pb_fsxm]
    xor ah, ah
    xor dx, dx
    mov cx, 9
    call bl_kv
    mov si, pb_r_kind
    mov al, [pb_vkind]
    xor ah, ah
    xor dx, dx
    call bl_kv

    call bl_operator
    inc word [pb_done]

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%define BL_ARENA_BYTES 6000        ; THE REPORT ARENA, TRIMMED (wave 6):
                                    ; benchlib's 16,000 is sized for
                                    ; sysbench's 337 lines, this report is
                                    ; ~40 of at most 78 bytes (~3.1 KB), and
                                    ; at 16,000 the bench's image + bss read
                                    ; 61,492 against APP_MAX_SIZE's 61,440 -
                                    ; the image had grown past a 512-byte
                                    ; step since wave 3, so bench360.img did
                                    ; not build and the row SKIPped (SPEC.md
                                    ; 97.10). Every byte here is two (bl_out)
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

; the package's own tables (97.1), so the DDA setup reads the real px_tan
; and the real fan: copied into the claim by pb_tables
%include "pixelstein/pxtab.inc"
%if PB_CLAIMEND > PB_CLAIMKB * 1024
%error "the claim does not hold the maps, the sets and the tables"
%endif

pb_tpl:
    dw 7, 22, 632, 448              ; x = 7 so WF_SNAP wants W_X + 1 on a
    dw pb_ttl, pb_paint, pb_onkey, pb_onclick   ; multiple of 8

pb_ttl:     db 'Pixelstein Bench', 0

pb_s_title: db 'PXSBENCH - the units of a PIXELSTEIN frame (97.10)', 0
pb_s_hint:  db 'Click, or press R, to run. The VRAM rows go fullscreen.', 0
pb_s_hdra:  db '-- (a)(b) the compiled store, 80 rows; the Duff ladder --', 0
pb_s_hdrc:  db '-- (c) the DDA: one column, 45 deg and near-axial --', 0
pb_s_hdrf:  db '-- (f) texel row: plain, xlat, ror x2, word, ror cl, lowres --', 0
pb_s_hdri:  db '-- (i) the hit, once a column; (j) a tick of the sim --', 0
pb_s_hdrd:  db '-- (d) the desktop present: BLIT1 512x80 --', 0
pb_s_hdrh:  db '-- (h) once a Size change: gen, bt_build (method T) --', 0
pb_s_hdrv:  db '-- (a)(d)(e) bracket: VRAM store, copy, C160 --', 0
pb_s_skipped: db 'SKIPPED (no claim)', 0
pb_s_refused: db 'BRACKET REFUSED', 0
pb_s_nomode:  db 'MODE REFUSED', 0

pb_r_st_ram:  db 'STORE 80 rows RAM', 0
pb_r_st_vram: db 'STORE 80 rows VRAM', 0
pb_r_stw_ram: db 'STORE word 80 rows RAM', 0
pb_r_stw_vram: db 'STORE word 80 rows VRAM', 0
pb_r_lad80:   db 'LADDER 80 Duff entry', 0
pb_r_lad40:   db 'LADDER 40 Duff entry', 0
pb_r_dda10:   db 'DDA col 10 crossings', 0
pb_r_dda20:   db 'DDA col 20 crossings', 0
pb_r_dda10a:  db 'DDA col 10 near-axial', 0
pb_r_ddasc:   db 'DDA scaffold only', 0
pb_r_ddax:    db 'DDA us x100/crossing', 0
pb_r_tx:      db 'TEXEL 80 rows plain', 0
pb_r_txx:     db 'TEXEL 80 rows xlat', 0
pb_r_txr2:    db 'TEXEL 80 rows ror x2', 0
pb_r_txw:     db 'TEXEL 80 rows word', 0
pb_r_txr3:    db 'TEXEL 80 rows ror cl3', 0
pb_r_txl:     db 'TEXEL 80 rows lowres', 0
pb_r_sim:     db 'SIM tick 32a 64d cap', 0
pb_r_sim1:    db 'SIM tick 7a 22d E1M1', 0
pb_r_hit:     db 'HIT col (97.2.5)', 0
pb_r_hith:    db 'HIT h (rows)', 0
pb_r_keys:    db 'KEY_DOWN x8', 0
pb_r_blit1:   db 'BLIT1 512x80 desktop', 0
pb_r_gen:     db 'GEN scaler set x1', 0
pb_r_genb:    db 'GEN set bytes', 0
pb_r_bt:      db 'BT_BUILD 15x2x1K', 0
pb_r_copy:    db 'COPY 5120B to VRAM', 0
pb_r_expand:  db 'EXPAND 3840B C160', 0
pb_r_st160:   db 'STORE 80 rows str160', 0
pb_r_mode:    db 'bracket mode (FSXM)', 0
pb_r_kind:    db 'adapter kind (VID)', 0

pb_win:     dw 0
pb_cx:      dw 0
pb_cy:      dw 0
pb_cw:      dw 0
pb_ch:      dw 0
pb_bx:      dw 0
pb_by:      dw 0
pb_claim:   dw 0
pb_genend:  dw 0
pb_caps:    dw 0
pb_done:    dw 0                    ; runs completed: the harness waits on it
pb_fsxm:    db 0
pb_vkind:   db 0
pb_inmode:  db 0
pb_blitcf:  db 0
pb_hitside: db 0
pb_side10:  db 0
pb_side20:  db 0
pb_hith:    dw 0                    ; the hit row's h, for the harness
pb_ddabytes: dw PB_DDA_BYTES        ; the body's length, for the report
pb_devoff1: times PB_ROWS dw 0      ; the game mode's device-row table, kept
                                    ; by pb_fsx for the harness. In the IMAGE
                                    ; rather than the bss: rows (f)'s two new
                                    ; texel forms and (j)'s sim put the image
                                    ; 52 bytes over the 61,440 budget with it
                                    ; in bss, and 160 bytes here fill an
                                    ; alignment step that was already spent
pb_res:     times PB_NRES dd 0      ; hundredths of a us per iteration, a row
pb_resf:    times PB_NRES db 0      ; ...and its flag: ' ' ok, 't' method T,
                                    ; '!' suspect, '-' skipped, 'x' refused

PB_BSS_RAW  equ PB_STRIDE * PB_ROWS + 256 + 256 + PB_ROWS * 2 + FSI_SIZE
PB_BSS_OWN  equ ((PB_BSS_RAW + 511) / 512) * 512   ; benchlib's base must be
                                                    ; 512-aligned (bl_save)

    OS88_BSS PB_BSS_OWN + BL_BSS_SIZE
    align 512                       ; ...and os88_image_end likewise
    OS88_IMAGE_END

pb_shadow   equ os88_image_end + 0
pb_tex      equ pb_shadow + PB_STRIDE * PB_ROWS
pb_xlat     equ pb_tex + 256
pb_devoff   equ pb_xlat + 256
pb_fsi      equ pb_devoff + PB_ROWS * 2

    BL_BSS os88_image_end + PB_BSS_OWN
