; =============================================================================
; os8088 - apps/pixelstein/pxhud.inc
;
; THE STATUS BAR AND THE CARDS (SPEC.md 97.13; wave 4): the 24 rows under the
; view (20 on the 160x100 retime) - floor, score, lives, the face, health,
; ammo, the keys and the weapon - and the text laid over the view for the
; seven states' cards, all through ONE per-backend writer that takes a
; one-bit bitmap and puts it on the destination in the backend's own pixels.
;
; NOT ONE KERNEL DRAWING SLOT (SPEC.md 53.7): the bar and the cards are
; composed into the destination the view is composed into - the shadow on
; CGA4, C160, Hercules and WIN1 (shadow rows PX_HUDROW0.., the view's 80
; rows above them, one 80-byte stride for both, 97.3), the hidden page on
; Mode X - and they reach the glass the way the view does: the bracket's
; present copies the bar's dirty rectangle through a device-row table of
; its own (px_hudoff), the window's worker blits it with OSAPI_GFX_BLIT1,
; and Mode X shows it with the page. The letters are the kernel's 8x8 face
; through OSAPI_FONT_GLYPHS, which IS legal in a bracket (apps/tank,
; apps/skies do the same); the digits, the face, the keys and the weapon
; are pxhuda.inc's one-bit masters (97.4).
;
; CHANGE-ONLY (the plan's rule, tests/pxshud.py): every field keeps the
; value last drawn ON EACH PAGE (px_hudv; Mode X has two, every other
; backend one), and a frame redraws a field only when its value differs -
; so a quiet second rewrites nothing, and a round spent redraws two digit
; cells. Nothing writes a pixel twice: every cell is opaque (ink or
; ground), so a field is overwritten, never cleared first.
;
; THE CELL IS THE BACKEND'S (px_gcb bytes of the destination per 8-pixel
; glyph cell): 2 on CGA4 (four 2-bit pixels a byte), 2 on Hercules (a
; glyph bit DOUBLED - the box is 640 dots wide and a single-width bar would
; be half of it, and the dots are narrow), 2 byte columns on Mode X (four
; planes), 1 on WIN1 (the desktop's own pixels: 320 of the 512), 4 on C160
; (two pixels a byte). The main bar is 40 cells; C160's is 20, and has its
; own layout (px_hlay_c: no labels, the face cropped to its 20 rows).
; =============================================================================

PX_HUDROW0  equ PX_ROWS             ; the bar's first row in the destination
PXF_MSG     equ 0                   ; the left label row, or a message on it
PXF_LABR    equ 1                   ; the right label row (drawn once a page)
PXF_FLOOR   equ 2
PXF_SCORE   equ 3
PXF_LIVES   equ 4
PXF_FACE    equ 5
PXF_HEALTH  equ 6
PXF_AMMO    equ 7
PXF_KEYS    equ 8
PXF_WPN     equ 9
PXF_N       equ 10
PXI_TEXT    equ 0                   ; the bar's four inks (px_hinks)
PXI_FACE    equ 1
PXI_GOLD    equ 2
PXI_SILV    equ 3
PXM_NONE    equ 0                   ; the left label row's message: none...
PXM_PAUSED  equ 1                   ; ...PAUSED (the bracket's pause)
PXM_DETAIL  equ 2                   ; ...the rung and resolution (Auto's step)
PXM_SIZE    equ 3                   ; ...the Size (the V key)
PXM_MAP     equ 4                   ; ...the Tab map is up (wave 6)
PX_MSGT     equ 36                  ; ticks a message stands (2 s)
PX_GRINT    equ 36                  ; ...and the grin
PXCD_LINES   equ 8                   ; a card's lines...
PXCD_LW      equ 26                  ; ...their bytes, NUL included
PXCD_ROW0    equ 4                   ; ...the first one's row, and the pitch
PXCD_PITCH   equ 9
PXCD_LNAME   equ 4                   ; the ENTER card's name line
PXCD_LCODE   equ 7                   ; the attract card's code line

; the main bar's layout, three bytes a field: the cell, the row, the digits
; (0xFF cell: the field is not on this bar)
px_hlay_m:
    db  0, 0, 0                     ; PXF_MSG    "FLOOR SCORE LIVES " cells 0..17
    db 20, 0, 0                     ; PXF_LABR   "HEALTH AMMO" cells 20..30
    db  1, 8, 2                     ; PXF_FLOOR  under FLOOR
    db  6, 8, 5                     ; PXF_SCORE  under SCORE
    db 14, 8, 1                     ; PXF_LIVES  under LIVES
    db 18, 0, 0                     ; PXF_FACE   16 x 24, cells 18..19
    db 21, 8, 3                     ; PXF_HEALTH under HEALTH
    db 28, 8, 2                     ; PXF_AMMO   under AMMO
    db 32, 4, 0                     ; PXF_KEYS   gold at row 4, silver at 14
    db 35, 4, 0                     ; PXF_WPN    24 x 16, cells 35..37
; C160's: 20 cells of four bytes and 20 rows - digits only, the face cropped
px_hlay_c:
    db 0xFF, 0, 0
    db 0xFF, 0, 0
    db  0, 2, 1
    db  2, 2, 5
    db  8, 2, 1
    db 10, 0, 0
    db 13, 2, 3
    db 17, 2, 2
    db 19, 1, 0
    db 0xFF, 0, 0

px_s_labl:  db 'FLOOR SCORE LIVES', 0
px_s_labr:  db 'HEALTH AMMO', 0
px_s_mpause: db 'PAUSED', 0
px_s_msize: db 'SIZE ', 0
px_s_mmap:  db 'MAP - TAB TO PLAY', 0

; per backend (px_back 0..6): the writer, the cell's bytes, the bar's first
; byte, its layout, its rows, and the four inks as the byte a lit full cell
; is (CGA4 palette 0: 3 brown for text and the face and the gold key, 1
; green for the silver; C160 the attribute nibble twice; Mode X the DAC
; entry; the 1bpp backends all bits)
px_hbk:
    dw 0, 0
    db 0, 0, 0, 0, 0, 0, 0, 0       ; PXB_NONE
    dw px_gput_x2, px_hlay_m
    db 2, 0, 24, 0, 0xFF, 0xFF, 0xFF, 0x55      ; CGA4
    dw px_gput_4, px_hlay_c
    db 4, 0, 20, 0, 0xEE, 0xFF, 0xEE, 0x77      ; C160
    dw px_gput_x2, px_hlay_m
    db 2, 0, 24, 0, 0xFF, 0xFF, 0xFF, 0xFF      ; HERC
    dw px_gput_mx, px_hlay_m
    db 2, 0, 24, 0, 14, 15, 14, 7               ; MODEX
    dw px_gput_1, px_hlay_m
    db 1, 20, 24, 0, 0xFF, 0xFF, 0xFF, 0xFF     ; WIN1: 320 of the band's 512
    dw px_gput_1, px_hlay_m
    db 1, 20, 24, 0, 0xFF, 0xFF, 0xFF, 0xFF     ; WIN4: WIN1's bar and cards,
                                                ; bits the pen colours (97.14)
PXHB_SIZE   equ 12

; the doubling table: a nibble of glyph bits to a byte of bit PAIRS (CGA4's
; 2-bit pixels, Hercules' doubled dots)
px_x2:  db 0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F
        db 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
; ...and C160's: two glyph bits to the two nibbles they light
px_x4:  db 0x00, 0x0F, 0xF0, 0xFF
px_hzero: times PXU_WPNSZ db 0      ; a blank cell, digit, key, weapon or face
%if PXU_FACESZ > PXU_WPNSZ || PXU_DIGSZ > PXU_WPNSZ
%error "px_hzero must cover a face and a digit (pxhud.inc)"
%endif

; -----------------------------------------------------------------------------
; px_font_init - the kernel's 8x8 face (OSAPI_FONT_GLYPHS), once at entry
; -----------------------------------------------------------------------------
px_font_init:
    push ax
    push cx
    push dx
    push si
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL/AH = its range
    mov [px_gtab], si
    mov [px_gseg], dx
    mov [px_gfirst], ax
    pop si
    pop dx
    pop cx
    pop ax
    ret

; px_glyph - AL = a character -> px_glyb its eight rows (zeros outside the
;            table's range). preserves every register
px_glyph:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    push ds
    pop es
    mov di, px_glyb
    cld
    cmp al, [px_gfirst]
    jb .blank
    cmp al, [px_glast]
    ja .blank
    sub al, [px_gfirst]
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    add si, [px_gtab]
    mov ds, [px_gseg]               ; the table is NOT in the kernel's segment
    movsw                           ; (os88api.inc's own warning)
    movsw
    movsw
    movsw
    jmp short .out
.blank:
    xor ax, ax
    stosw
    stosw
    stosw
    stosw
.out:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; px_hud_setup - the bar's words for [px_back], and every field forgotten on
;                both pages so the next frame draws it whole (entry to a
;                bracket, back to the window, a Mode change). The shadow's
;                bar rows are cleared here - never on a Size pick, which
;                touches the view's rows alone (px_shadow_clear)
; preserves every register
; -----------------------------------------------------------------------------
px_hud_setup:
    push ax
    push bx
    push cx
    push di
    push es
    mov al, [px_back]
    mov ah, PXHB_SIZE
    mul ah
    mov bx, ax
    add bx, px_hbk
    mov ax, [bx]
    mov [px_gputp], ax
    mov ax, [bx + 2]
    mov [px_hlay], ax
    mov al, [bx + 4]
    mov [px_gcb], al
    mov al, [bx + 5]
    mov [px_hx0], al
    mov al, [bx + 6]
    mov [px_hrows], al
    mov ax, [bx + 8]
    mov [px_hinks], ax
    mov ax, [bx + 10]
    mov [px_hinks + 2], ax
    push ds
    pop es
    cld
    mov di, px_hudv                 ; nothing of the bar on either page:
    mov cx, 2 * PXF_N               ; every field's value, and the inputs'
    mov ax, 0xFFFF                  ; snapshots (a floor of 0xFF is none a
    rep stosw                       ; level stream can hold, so the first
    mov di, px_hsnap                ; compare differs)
    mov cx, PXF_SNAP
    rep stosw
    mov di, px_hcell                ; ...and every number's cells (0xFFFF:
    mov cx, PX_HCELLS               ; neither a digit nor px_hf_num's blank
    rep stosw                       ; 0xFF twice, so each differs once more)
    mov byte [px_hr0], 0xFF
    mov byte [px_hr1], 0
    mov byte [px_hb0], 0xFF
    mov byte [px_hb1], 0
    mov byte [px_hrqn], 0
    mov byte [px_vcur], 0           ; (Mode X: neither page shows a pose yet)
    mov byte [px_hudd], 1
    cmp byte [px_back], PXB_MODEX
    je .out                         ; Mode X: r_setup cleared both pages
    cmp word [px_shseg], 0
    je .out
    mov es, [px_shseg]              ; the shadow's bar rows, black
    mov di, PX_HUDROW0 * PX_STRIDE
    mov cx, PX_HUDROWS * PX_STRIDE / 2
    xor ax, ax
    rep stosw
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; px_hud_val - BX = a field -> AX = what the bar should show there now.
;              clobbers nothing else
; -----------------------------------------------------------------------------
px_hud_val:
    push bx
    shl bx, 1
    jmp [px_hvtab + bx]
.msg:
    mov al, [px_hmsg]               ; the message AND its post's serial: a
    mov ah, [px_hmsgn]              ; Size over a Size, or Auto's step over
    jmp short .out                  ; its last, is a different line (review)
.labr:
    mov ax, 1
    jmp short .out
.floor:
    mov al, [px_floor]
    inc al
    jmp short .byte
.score:
    mov ax, [px_score]
    jmp short .out
.lives:
    mov al, [px_lives]
    cmp al, 9
    jbe .byte
    mov al, 9
    jmp short .byte
.face:
    cmp word [px_hlay], px_hlay_c   ; C160 HAS NO LABEL ROW for PAUSED, so a
    jne .face1                      ; pause there blanks the face (6): a
    mov al, 6                       ; pause with no mark on the glass could
    cmp byte [px_hmsg], PXM_PAUSED  ; not be told from a hang (review)
    je .byte
.face1:
    mov ah, [px_health]
    mov al, 5                       ; dead - BEFORE the grin (review r2: a
    or ah, ah                       ; death just after a treasure grinned
    jz .byte                        ; through the wash)
    mov al, 4                       ; the grin (a treasure just taken)
    cmp byte [px_grin], 0
    jne .byte
    mov al, 0
    cmp ah, 75
    jae .byte
    inc al
    cmp ah, 50
    jae .byte
    inc al
    cmp ah, 25
    jae .byte
    inc al
    jmp short .byte
.health:
    mov al, [px_health]
    jmp short .byte
.ammo:
    mov al, [px_ammo]
    jmp short .byte
.keys:
    mov al, [px_keys]
    and al, 3
    jmp short .byte
.wpn:
    mov al, [px_weapon]
    cmp byte [px_ammo], 0
    jne .byte
    xor al, al                      ; no rounds: the knife, as it is drawn
.byte:
    xor ah, ah
.out:
    pop bx
    ret
px_hvtab: dw px_hud_val.msg, px_hud_val.labr, px_hud_val.floor, px_hud_val.score
          dw px_hud_val.lives, px_hud_val.face, px_hud_val.health, px_hud_val.ammo
          dw px_hud_val.keys, px_hud_val.wpn

; -----------------------------------------------------------------------------
; px_hud_snap - the bar's INPUTS as ten bytes at DI (ES = DS): the floor, the
;               score, the lives, the health, the ammo, the keys, the weapon,
;               whether the face grins, the message. What every field's
;               value is a function of, so two equal snapshots draw the same
;               bar - and comparing them is ten bytes, where asking each field
;               its value is ~250 clk a field (review, wave 4: the first cut
;               asked all ten a frame and a step, ~2 ms a frame on the 5150,
;               and tests/pixelstein.py read it). The grin is a BIT: its
;               clock counts every tick and would otherwise owe a look a tick.
;               clobbers AX; DI + PXF_SNAP
; -----------------------------------------------------------------------------
PXF_SNAP    equ 10
px_hud_snap:
    mov al, [px_floor]
    stosb
    mov ax, [px_score]
    stosw
    mov al, [px_lives]
    stosb
    mov al, [px_health]
    stosb
    mov al, [px_ammo]
    stosb
    mov al, [px_keys]
    stosb
    mov al, [px_weapon]
    stosb
    mov al, [px_grin]
    or al, al
    jz .g
    mov al, 1
.g:
    stosb
    mov al, [px_hmsg]               ; the message plus its serial (a
    add al, [px_hmsgn]              ; multiple of 8 over a PXM_* of 0..7:
                                    ; wave 6's PXM_MAP is the fifth)
    stosb
    ret

; -----------------------------------------------------------------------------
; px_hud_poll - once a simulation step (px_step): does the bar owe a look?
;               The inputs' snapshot against the one each page was last
;               drawn from (px_hsnap) - a difference sets px_hudd, which the
;               idle predicate reads, and nothing is drawn here: the frame
;               does it. A frame ALREADY OWED (a force, a dirty view, a card)
;               needs no look - px_hud_draw compares anyway - so the step
;               that moved the player pays 139 clk here; a still one 1,049
;               (the snap and one 10-byte compare; Mode X two) - both
;               MEASURED on the 5150 by breakpoint (review r1, 97.13).
;               preserves every register
; -----------------------------------------------------------------------------
px_hud_poll:
    push ax
    mov al, [px_hudd]
    or al, [px_dirty]
    or al, [px_force]
    or al, [px_cardd]
    pop ax
    jnz .quick                      ; owed already: the frame looks
    cmp word [px_gputp], 0
    je .quick                       ; no backend yet
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, px_hsnow
    call px_hud_snap
    mov si, px_hsnow
    mov di, px_hsnap
    mov cx, PXF_SNAP
    repe cmpsb
    jne .owe
    cmp byte [px_back], PXB_MODEX   ; off Mode X the pages are one
    jne .out
    mov si, px_hsnow
    mov di, px_hsnap + PXF_SNAP     ; ...and the other page's
    mov cx, PXF_SNAP
    repe cmpsb
    je .out
.owe:
    mov byte [px_hudd], 1
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
.quick:
    ret

; -----------------------------------------------------------------------------
; px_hud_draw - the bar's changed fields onto this frame's page (97.13).
;               DS = ours, px_dseg/px_dbase this frame's destination.
;               out: px_hr0..px_hr1 / px_hb0..px_hb1 what was written
;               clobbers everything but DS
; -----------------------------------------------------------------------------
px_hud_draw:
    mov byte [px_hudd], 0
    cmp word [px_gputp], 0
    je .none
    ; THE INPUTS FIRST (px_hud_snap): unchanged since this page was drawn,
    ; nothing to ask any field - the common frame costs ~915 clk here
    ; (measured, 97.13: the snap and one 10-byte compare) and returns
    push ds
    pop es
    cld
    mov di, px_hsnow
    call px_hud_snap
    mov bx, px_hsnap                ; this page's last snapshot
    cmp byte [px_back], PXB_MODEX
    jne .s0
    cmp byte [px_page], 0
    je .s0
    add bx, PXF_SNAP
.s0:
    mov si, px_hsnow
    mov di, bx
    mov cx, PXF_SNAP
    repe cmpsb
    jne .chg
    cmp byte [px_back], PXB_MODEX
    je .mx
    ret                             ; off Mode X: nothing owed anywhere
.chg:
    mov si, px_hsnow                ; changed: this page's snapshot is now
    mov di, bx                      ; the one it is drawn from
    mov cx, PXF_SNAP
    rep movsb
    mov si, px_hudv
    cmp byte [px_back], PXB_MODEX
    jne .p0
    cmp byte [px_page], 0
    je .p0
    add si, PXF_N * 2               ; Mode X's second page keeps its own
.p0:
    xor bx, bx
.f:
    push bx
    push si
    mov di, bx                      ; the layout's entry
    add di, bx
    add di, bx
    add di, [px_hlay]
    cmp byte [di], 0xFF
    je .next                        ; not on this bar
    call px_hud_val
    cmp ax, [si]
    je .next
    mov [si], ax
    inc word [px_hudn]              ; the gate's count of field rewrites
    mov dl, [di]                    ; DL = the cell, DH = the row
    mov dh, [di + 1]
    mov cl, [di + 2]                ; CL = the digits
    shl bx, 1
    push word [px_hr0]              ; ONE RECTANGLE A FIELD (review, wave 4):
    push word [px_hb0]              ; the union of a kill frame's score and
    mov word [px_hr0], 0x00FF       ; ammo is 48 bytes x 16 rows where 14 x
    mov word [px_hb0], 0x00FF       ; 16 changed - so each field's own
    call [px_hdtab + bx]            ; rectangle is queued for the shadow
    call px_hrq_add                  ; presents, and the union (WIN1's one
    pop ax                          ; OSAPI_GFX_BLIT1, where the CALL is the
    cmp al, [px_hb0]                ; cost) is grown from it as before
    jae .u0
    mov [px_hb0], al
.u0:
    cmp ah, [px_hb1]
    jbe .u1
    mov [px_hb1], ah
.u1:
    pop ax
    cmp al, [px_hr0]
    jae .u2
    mov [px_hr0], al
.u2:
    cmp ah, [px_hr1]
    jbe .next
    mov [px_hr1], ah
.next:
    pop si
    pop bx
    inc si
    inc si
    inc bx
    cmp bx, PXF_N
    jb .f
    ; off Mode X the two pages are one: page 1's words follow page 0's, and
    ; its snapshot page 0's - copied only after a change (review, wave 4:
    ; every equal frame used to copy both, ~500 clk for nothing)
    push ds
    pop es
    cmp byte [px_back], PXB_MODEX
    je .mx
    mov si, px_hudv
    mov di, px_hudv + PXF_N * 2
    mov cx, PXF_N
    rep movsw
    mov si, px_hsnap
    mov di, px_hsnap + PXF_SNAP
    mov cx, PXF_SNAP
    rep movsb
.none:
    ret
.mx:
    mov si, px_hsnap                ; the other page still owes a look?
    mov di, px_hsnap + PXF_SNAP
    mov cx, PXF_SNAP
    repe cmpsb
    je .none
    mov byte [px_hudd], 1
    ret

px_hdtab: dw px_hf_msg, px_hf_labr, px_hf_num, px_hf_num, px_hf_num, px_hf_face
          dw px_hf_num, px_hf_num, px_hf_keys, px_hf_wpn

; --- the field drawers: AX = the value, DL = the cell, DH = the row, CL = the
;     digits; clobber everything but DS ---------------------------------------
; A NUMBER PUTS ONLY THE CELLS THAT CHANGED (review r2): its laid-out cells
; are kept per field and page (px_hcell), so a score of 1200 -> 1300 puts
; one cell, not five, and ammo 8 -> 7 does not rewrite its blank tens.
; Priced on the 5150's CGA at ~4.0k clk a digit cell (px_gput_x2 ~145 a
; glyph byte, ~200 a row, x16, plus px_hput's ~0.85k): a score change went
; ~20k clk of draw to ~4-8k. The field's value compare (px_hud_draw) still
; decides whether this runs at all; this decides what it writes
px_hf_num:
    push ax
    shr bx, 1                       ; BX = the field (px_hud_draw's BX x 2)
    mov al, [px_hcoff + bx]
    xor ah, ah
    add ax, px_hcell
    cmp byte [px_back], PXB_MODEX
    jne .pg
    cmp byte [px_page], 0
    je .pg
    add ax, PX_HCELLS               ; Mode X's second page keeps its own
.pg:
    mov [px_hcp], ax
    pop ax
    mov ch, cl                      ; CH = the cells
    mov di, px_hnum + 5             ; the digits right to left, blank-padded
    mov bx, 10
.d:
    dec di
    push dx
    xor dx, dx
    div bx
    mov [di], dl
    pop dx
    dec ch
    jz .laid
    or ax, ax
    jnz .d
.bl:
    dec di                          ; a leading blank
    mov byte [di], 0xFF
    dec ch
    jnz .bl
.laid:
    ; DI = the first cell's byte, CL cells from there
    xor ch, ch
.c:
    mov al, [di]
    mov bx, [px_hcp]
    cmp al, [bx]
    je .same                        ; this cell shows this digit already
    mov [bx], al
    push cx
    push dx
    push di
    mov si, px_hzero
    cmp al, 0xFF
    je .blank
    mov ah, PXU_DIGSZ
    mul ah
    mov si, ax
    add si, px_hud_dig
.blank:
    mov cx, (PXU_DIGH << 8) | PXU_DIGW
    mov al, PXI_TEXT
    call px_hput
    pop di
    pop dx
    pop cx
.same:
    inc word [px_hcp]
    inc dl
    inc di
    loop .c
    ret
; each number field's first byte in a page's px_hcell (FLOOR 2 cells, SCORE
; 5, LIVES 1, HEALTH 3, AMMO 2 at most - the main bar's counts, C160's no
; more)
px_hcoff: db 0, 0, 0, 2, 7, 0, 8, 11, 0, 0
PX_HCELLS   equ 13

px_hf_face:
    mov si, px_hzero                ; 6: the paused C160's blank face (a
    cmp al, PXU_NFACE               ; face is 48 bytes, px_hzero's size)
    jae .have
    mov ah, PXU_FACESZ
    mul ah
    mov si, ax
    add si, px_hud_face
.have:
    mov cx, (PXU_FACEH << 8) | PXU_FACEW
    cmp byte [px_hrows], PXU_FACEH
    jae .whole
    add si, PXU_FACEW * 2           ; C160: rows 2..21 of the 24 (97.4's
    mov ch, 20                      ; contract keeps the ends expendable)
.whole:
    mov al, PXI_FACE
    jmp px_hput

px_hf_keys:
    push ax
    push dx
    mov si, px_hzero
    test al, 1
    jz .g
    mov si, px_hud_key
.g:
    mov cx, (PXU_KEYH << 8) | 1
    mov al, PXI_GOLD
    call px_hput
    pop dx
    pop ax
    add dh, 10                      ; the silver key ten rows under the gold
    mov si, px_hzero
    test al, 2
    jz .s
    mov si, px_hud_key + PXU_KEYSZ
.s:
    mov cx, (PXU_KEYH << 8) | 1
    mov al, PXI_SILV
    jmp px_hput

px_hf_wpn:
    mov ah, PXU_WPNSZ
    mul ah
    mov si, ax
    add si, px_hud_wpn
    mov cx, (PXU_WPNH << 8) | PXU_WPNW
    mov al, PXI_TEXT
    jmp px_hput

px_hf_labr:
    mov si, px_s_labr
    mov cl, 11
    jmp px_htext

px_hf_msg:
    or al, al
    jnz .msg
    mov si, px_s_labl               ; the labels, padded to the face
    mov cl, 18
    jmp px_htext
.msg:
    mov di, px_hmbuf                ; a message, built and padded
    mov si, px_s_mpause
    cmp al, PXM_PAUSED
    je .cat
    mov si, px_s_mmap
    cmp al, PXM_MAP
    jne .det
.cat:
    call px_strcat
    jmp short .say
.det:
    cmp al, PXM_SIZE
    je .size
    call px_detail_str              ; "TEXTURED LOW RES" (pxwin.inc's words)
    jmp short .say
.size:
    mov si, px_s_msize
    call px_strcat
    mov al, [px_size]
    call px_dec3
.say:
    mov byte [di], 0
    mov si, px_hmbuf
    mov cl, 18
    jmp px_htext

; -----------------------------------------------------------------------------
; px_htext - the NUL string at SI in the kernel's face from bar cell DL, row
;            DH, padded with blanks to CL cells (the pad IS the erase, 27.2's
;            trick: one opaque pass). clobbers everything but DS
; -----------------------------------------------------------------------------
px_htext:
    xor ch, ch
.c:
    lodsb
    or al, al
    jnz .ch
    dec si                          ; the pad: stay on the NUL
    mov al, ' '
.ch:
    call px_glyph
    push cx
    push dx
    push si
    mov si, px_glyb
    mov cx, (8 << 8) | 1
    mov al, PXI_TEXT
    call px_hput
    pop si
    pop dx
    pop cx
    inc dl
    loop .c
    ret

; -----------------------------------------------------------------------------
; px_hput - one bitmap onto the bar: DS:SI its rows (CL bytes a row, CH
;           rows), DL the bar cell, DH the bar row, AL the ink (PXI_*).
;           The destination and the dirty rectangle follow from the backend.
;           clobbers everything but DS
; -----------------------------------------------------------------------------
%if PX_STRIDE != 80
%error "px_hput multiplies a row by 80 in shifts: restate it"
%endif
px_hput:
    push cx
    mov bl, al
    xor bh, bh
    mov bl, [px_hinks + bx]
    mov [px_gink], bl
    ; the rectangle: rows DH .. DH + CH - 1, bytes hx0 + DL*cb .. + CL*cb - 1
    mov al, dh
    cmp al, [px_hr0]
    jae .r0
    mov [px_hr0], al
.r0:
    add al, ch
    dec al
    cmp al, [px_hr1]
    jbe .r1
    mov [px_hr1], al
.r1:
    ; NO MUL (review r2: three a cell were ~280 clk, ~5k on the label row):
    ; px_gcb is 1, 2 or 4, so a byte times it is none, one or two shifts,
    ; and the row times PX_STRIDE = 80 is (x4 + x) x16
    mov al, dl
    xor ah, ah
    test byte [px_gcb], 6
    jz .m0
    shl al, 1
    test byte [px_gcb], 4
    jz .m0
    shl al, 1
.m0:
    add al, [px_hx0]
    mov bx, ax                      ; BX = the first byte
    cmp al, [px_hb0]
    jae .b0
    mov [px_hb0], al
.b0:
    mov al, cl
    xor ah, ah
    test byte [px_gcb], 6
    jz .m1
    shl al, 1
    test byte [px_gcb], 4
    jz .m1
    shl al, 1
.m1:
    add ax, bx
    dec ax
    cmp al, [px_hb1]
    jbe .b1
    mov [px_hb1], al
.b1:
    mov al, dh                      ; DI = the destination
    xor ah, ah
    add ax, PX_HUDROW0
    mov di, ax
    shl ax, 1
    shl ax, 1
    add ax, di                      ; x5...
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; ...x16: x PX_STRIDE
    mov di, ax
    add di, bx
    add di, [px_dbase]
    mov es, [px_dseg]
    pop cx
    jmp [px_gputp]

; -----------------------------------------------------------------------------
; THE WRITERS: DS:SI the bitmap (CL bytes a row, CH rows), ES:DI the
; destination's top-left, [px_gink] the lit byte; 80 bytes a destination row
; (the one stride, 97.3). Every cell opaque: a clear glyph bit writes the
; ground, black on every backend. clobber AX, BX, CX, DX, SI, DI, BP
; -----------------------------------------------------------------------------
; CGA4 and Hercules: two bytes a cell, each glyph bit a bit PAIR
px_gput_x2:
    cld
    mov dl, cl
    mov dh, ch
    mov bx, px_x2
.row:
    mov cl, dl
    push di
.b:
    lodsb
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlat
    and al, [px_gink]
    stosb
    mov al, ah
    and al, 0x0F
    xlat
    and al, [px_gink]
    stosb
    dec cl
    jnz .b
    pop di
    add di, PX_STRIDE
    dec dh
    jnz .row
    ret

; WIN1: the desktop's own dots, a byte a cell (the pen colours it on VGA)
px_gput_1:
    cld
    mov dl, cl
    mov dh, ch
    xor ch, ch
.row:
    mov cl, dl
    push di
    rep movsb
    pop di
    add di, PX_STRIDE
    dec dh
    jnz .row
    ret

; C160: four bytes a cell, each byte two pixels (the attribute's nibbles)
px_gput_4:
    cld
    mov dl, cl
    mov dh, ch
    mov bx, px_x4
.row:
    mov cl, dl
    push di
.b:
    lodsb
    mov ah, al
    mov ch, 4
.p:
    mov al, ah
    rol al, 1
    rol al, 1
    mov ah, al
    and al, 3
    xlat
    and al, [px_gink]
    stosb
    dec ch
    jnz .p
    dec cl
    jnz .b
    pop di
    add di, PX_STRIDE
    dec dh
    jnz .row
    ret

; Mode X: a glyph byte at a time, both byte columns, each written TWICE
; through the map mask - once with the planes of its lit pixels and the
; ink, once with the others and 0 (pixel k of a column on plane k, glyph
; bit 7 - k of its nibble, so the nibble is bit-reversed into the mask by
; px_mxrev). The first cut went plane by plane, re-reading every glyph
; byte four times with two tests and two memory-operand inks each: ~600
; clk a glyph byte against this ~250 (four OUTs, four writes, two xlats),
; estimated - Mode X is not timed on MartyPC (review r2; CLAUDE.md's
; performance rule 5: the first cut priced the OUTs and not the re-reads).
; Nothing is written twice: each (plane, byte) is reached by exactly one of
; the pair. The map mask is put back to all four planes, which the compose
; assumes (97.3)
px_gput_mx:
    cld
    mov bx, px_mxrev
    mov dx, 0x3C4
    mov al, ch
    xor ah, ah
    mov bp, ax                      ; BP = rows
    mov [px_gdim], cl               ; bytes a row
.row:
    mov cl, [px_gdim]
    push di
.b:
    lodsb
    mov ch, al                      ; CH = the glyph byte
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlat                            ; the left column's lit planes
    mov ah, al
    mov al, 2
    out dx, ax
    mov al, [px_gink]
    mov [es:di], al
    xor ah, 0x0F                    ; ...and its dark ones
    mov al, 2
    out dx, ax
    mov byte [es:di], 0
    mov al, ch
    and al, 0x0F
    xlat                            ; the right column's
    mov ah, al
    mov al, 2
    out dx, ax
    mov al, [px_gink]
    mov [es:di + 1], al
    xor ah, 0x0F
    mov al, 2
    out dx, ax
    mov byte [es:di + 1], 0
    inc di
    inc di
    dec cl
    jnz .b
    pop di
    add di, PX_STRIDE
    dec bp
    jnz .row
    mov ax, 0x0F02                  ; every plane again
    out dx, ax
    ret
; a nibble's bits reversed: glyph bit 3 - k (pixel k) to map-mask bit k
px_mxrev: db 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15

; -----------------------------------------------------------------------------
; px_hud_present - the bar's dirty rectangle onto the glass, the SHADOW
;                  backends in a bracket (px_present's tail): the rows
;                  px_hr0..px_hr1, the bytes px_hb0..px_hb1, through
;                  px_hudoff. Mode X drew into its page; WIN1 blits it
;                  (px_hud_blit). clobbers everything but DS
; -----------------------------------------------------------------------------
px_hud_present:
    mov al, [px_hrqn]
    or al, al
    jz .union                       ; nothing queued: the union (empty or not)
    cmp al, PXF_N
    ja .union                       ; overflowed (a draw never presented)
    mov si, px_hrq
.q:
    push ax
    push si
    mov ax, [si]
    mov [px_hr0], ax
    mov ax, [si + 2]
    mov [px_hb0], ax
    call px_hud_rect
    pop si
    pop ax
    add si, 4
    dec al
    jnz .q
    jmp short .clear
.union:
    call px_hud_rect
.clear:
    mov byte [px_hrqn], 0
    mov byte [px_hr0], 0xFF
    mov byte [px_hr1], 0
    mov byte [px_hb0], 0xFF
    mov byte [px_hb1], 0
    ret

; px_hrq_add - the field just drawn (px_hr0..px_hb1 its own rectangle) onto
;             the present's queue; a full queue falls back to the union.
;             clobbers AX, DI
px_hrq_add:
    mov al, [px_hr0]
    cmp al, [px_hr1]
    ja .out                         ; the field wrote nothing
    mov al, [px_hrqn]
    cmp al, PXF_N
    jae .over
    xor ah, ah
    mov di, ax
    shl di, 1
    shl di, 1
    add di, px_hrq
    mov ax, [px_hr0]
    mov [di], ax
    mov ax, [px_hb0]
    mov [di + 2], ax
    inc byte [px_hrqn]
.out:
    ret
.over:
    mov byte [px_hrqn], 0xFF
    ret

; px_hud_rect - ONE rectangle, px_hr0..px_hr1 x px_hb0..px_hb1, shadow to
;               glass (nothing when it is empty). clobbers everything but DS
px_hud_rect:
    mov al, [px_hr0]
    cmp al, [px_hr1]
    ja .none
    xor ah, ah
    mov bx, ax
    shl bx, 1
    add bx, px_hudoff
    mov cl, [px_hr1]
    sub cl, al
    inc cl
    xor ch, ch
    mov bp, cx                      ; BP = rows (a counter)
    mov dx, PX_STRIDE
    mul dx
    mov si, ax
    add si, PX_HUDROW0 * PX_STRIDE
    mov dl, [px_hb0]
    xor dh, dh
    add si, dx                      ; DS:SI = the rectangle's first byte
    mov cl, [px_hb1]
    sub cl, dl
    inc cl                          ; CX = its bytes a row
    mov es, [px_fsi + FSI_SEG]
    push ds
    mov ds, [cs:px_shseg]
    cld
    cmp byte [cs:px_back], PXB_C160
    je .c160
.row:
    mov di, [cs:bx]
    add di, dx
    push cx
    push si
    rep movsb
    pop si
    pop cx
    add si, PX_STRIDE
    inc bx
    inc bx
    dec bp
    jnz .row
    pop ds
    jmp short .none
.c160:
    mov di, [cs:bx]                 ; the attribute at the stride of two
    add di, dx
    add di, dx
    push cx
    push si
.e:
    movsb
    inc di
    loop .e
    pop si
    pop cx
    add si, PX_STRIDE
    inc bx
    inc bx
    dec bp
    jnz .c160
    pop ds
.none:
    ret

; -----------------------------------------------------------------------------
; px_hud_blit - the bar in a WINDOW (WIN1): its dirty rectangle, or all of it
;               after a paint blackened it (px_hudw), with ONE OSAPI_GFX_BLIT1
;               (SPEC.md 5.4.2) at the band's left + 96 - the bar is 320 of
;               the band's 512 dots, centred under the widest band. The
;               lock held, the clip armed, px_geom_win done.
;               clobbers AX, BX, CX, DX, SI, BP, ES
; -----------------------------------------------------------------------------
px_hud_blit:
    cmp byte [px_hudw], 0
    je .rect
    mov byte [px_hudw], 0
    mov byte [px_hr0], 0            ; the whole bar
    mov byte [px_hr1], PX_HUDROWS - 1
    mov byte [px_hb0], 20
    mov byte [px_hb1], 59
.rect:
    mov al, [px_hr0]
    cmp al, [px_hr1]
    ja .none
    cmp byte [px_bpp], 1
    jbe .mono
    push ax
    mov al, CWHITE
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
    pop ax
.mono:
    xor ah, ah
    mov bx, [px_by]
    add bx, PX_ROWS
    add bx, ax                      ; BX = y
    mov dl, [px_hr1]
    sub dl, al
    inc dl
    xor dh, dh                      ; DX = rows
    mov cx, PX_STRIDE
    push dx
    mul cx
    pop dx
    mov si, ax
    add si, PX_HUDROW0 * PX_STRIDE
    mov al, [px_hb0]
    xor ah, ah
    add si, ax                      ; ES:SI = the rectangle
    mov cl, [px_hb1]
    sub cl, al
    inc cl
    xor ch, ch
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = its dots
    sub ax, 20                      ; its byte within the bar...
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [px_bx]
    add ax, 96                      ; AX = x
    mov bp, PX_STRIDE
    mov es, [px_shseg]
    call OSAPI_GFX_BLIT1
.none:
    mov byte [px_hrqn], 0            ; (the window blits the union alone)
    mov byte [px_hr0], 0xFF
    mov byte [px_hr1], 0
    mov byte [px_hb0], 0xFF
    mov byte [px_hb1], 0
    ret

; =============================================================================
; THE CARDS (97.13): the text of a state over a black view
; =============================================================================

; px_card_lowe - line AL - 1 of the card is owed ALONE (a typed letter) -
;                unless the whole card is still owed, which covers it
px_card_lowe:
    cmp byte [px_cardd], 0
    je .line
    cmp byte [px_cardl], 0
    je .out                         ; the whole card is coming: nothing to add
.line:
    mov [px_cardl], al
    jmp px_card_owe
.out:
    ret

; px_card_owe - the card is to be drawn: once, or on both Mode X pages
px_card_owe:
    cmp byte [px_state], PXST_DYING ; ONLY A CARD STATE OWES A CARD (review,
    jbe .none                       ; wave 4): PLAY (0) and DYING (1) and the
    cmp byte [px_state], PXST_DEMO  ; demo draw none, and px_cardd left at 1
    je .none                        ; there made every idle pass a frame -
                                    ; and on Mode X a flip between two poses
    mov byte [px_cardd], 1
    cmp byte [px_back], PXB_MODEX
    jne .out
    mov byte [px_cardd], 2
.out:
    ret
.none:
    mov byte [px_cardd], 0
    ret

; -----------------------------------------------------------------------------
; px_card_draw - this state's card onto this frame's destination: the band
;                black and every line, or [px_cardl] - 1 alone (a typed
;                initial or code letter: the line is fixed-width and opaque,
;                so it is overwritten and nothing else moves - SPEC.md
;                85.9.1's reason). Sets px_r0..px_r1. clobbers all but DS
; -----------------------------------------------------------------------------
px_card_draw:
    call px_card_text
    mov al, [px_cardl]
    or al, al
    jz .whole
    cmp byte [px_cardd], 0          ; the last page to take it: the line is
    jne .one                        ; done with
    mov byte [px_cardl], 0
.one:
    dec al
    call px_card_line
    ret
.whole:
    mov byte [px_r0], 0
    mov byte [px_r1], PX_ROWS - 1
    mov es, [px_dseg]               ; the band, black: Size bytes of 80 rows
    mov di, [px_dbase]
    mov al, [px_x0]
    xor ah, ah
    add di, ax
    mov dl, PX_ROWS
    mov cl, [px_size]
    xor ch, ch
    mov bx, PX_STRIDE
    sub bx, cx
    xor ax, ax
    cld
.clr:
    push cx
    rep stosb
    pop cx
    add di, bx
    dec dl
    jnz .clr
    xor al, al
.l:
    push ax
    call px_card_line
    pop ax
    inc al
    cmp al, PXCD_LINES
    jb .l
    mov al, [px_force]              ; the view's memory: "the whole band
    call px_force_all               ; was drawn", so leaving the card
    mov [px_force], al              ; repaints it all (px_fade_draw's shape)
    ret                             ; - and a force owed meanwhile stays owed

; px_card_line - line AL of px_cbuf, centred in the band. clobbers all but DS
px_card_line:
    mov bl, al                      ; BL = the line
    mov ah, PXCD_LW
    mul ah
    mov si, ax
    add si, px_cbuf
    mov al, bl
    mov ah, PXCD_PITCH
    mul ah
    add al, PXCD_ROW0
    mov dh, al                      ; DH = the row
    push si                         ; the length
    xor cx, cx
.len:
    cmp byte [si], 0
    je .have
    inc si
    inc cx
    jmp short .len
.have:
    pop si
    jcxz .out
    mov al, [px_size]               ; the band's cells
    xor ah, ah
    div byte [px_gcb]
    cmp cl, al
    jbe .fits
    mov cl, al                      ; clipped to the band (C160 at Size 48)
.fits:
    sub al, cl
    shr al, 1                       ; the first cell, centred
    mov dl, al
    mov al, dh                      ; DH = the row -> the rows' dirty range
    cmp al, [px_r0]
    jae .r0
    mov [px_r0], al
.r0:
    add al, 7
    cmp al, [px_r1]
    jbe .r1
    mov [px_r1], al
.r1:
.c:
    push cx
    push dx
    lodsb
    push si
    call px_glyph
    mov al, dl                      ; DI = the base + row * 80 + x0 + cell * cb
    mul byte [px_gcb]
    mov bl, [px_x0]
    xor bh, bh
    add ax, bx
    mov di, ax
    mov al, dh
    mov bl, PX_STRIDE
    mul bl
    add di, ax
    add di, [px_dbase]
    mov es, [px_dseg]
    mov bl, [px_hinks + PXI_TEXT]
    mov [px_gink], bl
    mov si, px_glyb
    mov cx, (8 << 8) | 1
    call [px_gputp]
    pop si
    pop dx
    pop cx
    inc dl
    loop .c
.out:
    ret

; -----------------------------------------------------------------------------
; px_card_text - px_cbuf's eight lines for [px_state] (97.13)
; clobbers AX, BX, CX, DX, SI, DI, ES (left = DS)
; -----------------------------------------------------------------------------
px_card_text:
    push ds
    pop es
    cld
    mov di, px_cbuf                 ; every line empty
    mov cx, PXCD_LINES * PXCD_LW / 2
    xor ax, ax
    rep stosw
    mov al, [px_state]
    cmp al, PXST_READY
    je .ready
    cmp al, PXST_DONE
    je .done
    cmp al, PXST_OVER
    je .over
    cmp al, PXST_ENTER
    je .enter
    ; --- ATTRACT ------------------------------------------------------------
    xor bx, bx
    mov si, px_s_ctitle
    call px_cput
    cmp byte [px_demodone], 0
    jne .demo
    cmp byte [px_inbr], 0           ; an 8086's window: the machine plays
    jne .hs                         ; best fullscreen, and says so first
    cmp byte [px_tier], CPU_8086    ; (PIXELSTEIN-PLAN 15)
    jne .hs
    mov bl, 1
    mov si, px_s_cfull
    call px_cput
.hs:
    xor dx, dx                      ; the table's first three rows
.hrow:
    mov bl, dl
    add bl, 2
    call px_cline
    mov al, dl
    add al, '1'
    stosb
    mov al, ' '
    stosb
    mov bx, dx
    mov ax, bx
    shl bx, 1
    add bx, ax                      ; the initials, three a row
    mov al, [px_hsn + bx]
    stosb
    mov al, [px_hsn + bx + 1]
    stosb
    mov al, [px_hsn + bx + 2]
    stosb
    mov al, ' '
    stosb
    mov bx, dx
    shl bx, 1
    mov ax, [px_hs + bx]
    call px_decw
    inc dx
    cmp dx, 3
    jb .hrow
    jmp short .keys
.demo:
    mov bl, 1
    mov si, px_s_cdemo
    call px_cput
    mov bl, 2                       ; "FRAMES 81 TICKS 135"
    mov si, px_s_cframes
    call px_cput
    mov ax, [px_demof]
    call px_decw
    mov si, px_s_cticks
    call px_strcat
    mov ax, [px_demot]
    call px_decw
    mov bl, 3                       ; "Textured  Low res 64": what the run
    call px_cline                   ; drew at (px_demo_restore kept it), so
    mov ax, [px_demorg]             ; a field number says its rung and Size
    call px_detail_stra
    mov al, ' '
    mov [di], al
    inc di
    mov al, [px_demorg + 2]
    call px_dec3
    mov bl, 4
    mov si, px_s_cfps
    call px_cput
    mov ax, [px_demofps]            ; tenths
    xor dx, dx
    mov cx, 10
    div cx
    push dx
    call px_decw
    mov al, '.'
    stosb
    pop ax
    add al, '0'
    stosb
.keys:
    mov bl, 5
    mov si, px_s_cplay
    call px_cput
    mov bl, 6
    mov si, px_s_ctdemo
    call px_cput
    mov bl, PXCD_LCODE
    mov si, px_s_ccode
    mov al, [px_codep]
    cmp al, 0xFF
    je .cl
    mov si, px_s_cbad
    cmp al, 0xFE
    je .cl
    mov si, px_s_ccode2             ; "CODE " then the four
    call px_cput
    mov ax, [px_code]
    stosw
    mov ax, [px_code + 2]
    stosw
    ret
.cl:
    jmp px_cput
    ; --- READY ------------------------------------------------------------------
.ready:
    mov bl, 3
    call px_cfloor
    mov bl, 5
    mov si, px_s_cready
    jmp px_cput
    ; --- LEVELDONE: the ratios --------------------------------------------------
.done:
    xor bx, bx
    call px_cfloor
    mov si, px_s_cdone
    call px_strcat
    mov bl, 2
    mov si, px_s_ckills
    mov al, [px_ckill]
    mov ah, [px_nkill]
    call px_cratio
    mov bl, 3
    mov si, px_s_csec
    mov al, [px_csec]
    mov ah, [px_nsec]
    call px_cratio
    mov bl, 4
    mov si, px_s_cloot
    mov al, [px_ctreas]
    mov ah, [px_ntreas]
    call px_cratio
    mov bl, 5
    mov si, px_s_ctime
    call px_cput
    mov ax, [px_ltime]              ; ticks -> seconds: x 10 / 182
    mov cx, 10
    mul cx
    mov cx, 182
    div cx
    xor dx, dx
    mov cx, 60
    div cx                          ; AX = minutes, DX = seconds
    push dx
    call px_decw
    mov al, ':'
    stosb
    pop ax
    aam
    xchg al, ah
    add ax, '00'
    stosw
    mov bl, 7
    mov al, [px_floor]
    inc al
    cmp al, PXL_NLEV
    jae .last
    mov si, px_s_ccode2             ; (px_cput takes AX: the floor is read
    call px_cput                    ; again after it - the first cut showed
    mov al, [px_floor]              ; the FIRST floor's code on every card)
    inc al
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov si, ax
    mov ax, [px_levpw + si]
    stosw
    mov ax, [px_levpw + si + 2]
    stosw
    ret
.last:
    mov si, px_s_clast
    jmp px_cput
    ; --- OVER -------------------------------------------------------------------
.over:
    xor bx, bx
    mov si, px_s_cover
    cmp byte [px_victory], 0
    je .ov
    mov si, px_s_cwon
.ov:
    call px_cput
    mov bl, 2
    call px_cscore
    mov bl, 3
    jmp px_cfloor
    ; --- ENTER ------------------------------------------------------------------
.enter:
    xor bx, bx
    mov si, px_s_chigh
    call px_cput
    mov bl, 2
    call px_cscore
    mov bl, PXCD_LNAME
    mov si, px_s_cname
    call px_cput
    xor bx, bx                      ; "NAME " then the three, the cursor and
.n:                                 ; the pad: nine cells whatever is typed
    mov al, [px_ini + bx]
    cmp bl, [px_inip]
    jb .nc
    mov al, ' '
    jne .nc
    mov al, '_'
.nc:
    stosb
    inc bx
    cmp bx, 4
    jb .n
    mov bl, 6
    mov si, px_s_csave
    jmp px_cput

; px_cline - DI = line BL of px_cbuf. clobbers AX
px_cline:
    push bx
    mov al, bl
    mov ah, PXCD_LW
    mul ah
    mov di, ax
    add di, px_cbuf
    pop bx
    ret

; px_cput - line BL := the string at SI; DI past it (to append to)
px_cput:
    call px_cline
    jmp px_strcat

; px_cfloor - line BL := "FLOOR n"
px_cfloor:
    mov si, px_s_cfloor
    call px_cput
    mov al, [px_floor]
    inc al
    jmp px_dec3

; px_cscore - line BL := "SCORE n"
px_cscore:
    mov si, px_s_cscore
    call px_cput
    mov ax, [px_score]
    jmp px_decw

; px_cratio - line BL := SI's word then AL of AH as a percentage (a floor
;             with none of a thing reads 100%)
px_cratio:
    push ax
    call px_cput
    pop ax
    mov cl, ah
    mov ah, 100
    or cl, cl
    jz .all
    mul ah                          ; AX = found x 100
    div cl                          ; AL = the percentage
    xor ah, ah
    jmp short .say
.all:
    mov ax, 100
.say:
    call px_dec3
    mov al, '%'
    stosb
    ret

; px_decw - AX in decimal at DI, no leading zeros; DI past it. The digits
;           are built right to left in px_hnum (no push per digit: a loop of
;           variable stack depth is what tests/unit/t_stkbalance.py refuses).
;           clobbers AX
px_decw:
    push bx
    push dx
    push si
    mov si, px_hnum + 6
    mov bx, 10
.d:
    xor dx, dx
    div bx
    dec si
    add dl, '0'
    mov [si], dl
    or ax, ax
    jnz .d
.o:
    mov al, [si]
    mov [di], al
    inc di
    inc si
    cmp si, px_hnum + 6
    jb .o
    pop si
    pop dx
    pop bx
    ret

px_s_ctitle: db 'PIXELSTEIN 3D', 0
px_s_cfull:  db 'PRESS F FOR FULL SCREEN', 0
px_s_cplay:  db 'SPACE: PLAY', 0
px_s_ctdemo: db 'T: TIMEDEMO', 0
px_s_ccode:  db 'C: CODE  ', 0     ; the code line is NINE cells in all three
px_s_cbad:   db 'BAD CODE ', 0     ; of its forms, so a typed letter
px_s_ccode2: db 'CODE ', 0         ; overwrites it whole (px_cardl)
px_s_cdemo:  db 'TIMEDEMO', 0
px_s_cframes: db 'FRAMES ', 0
px_s_cticks: db ' TICKS ', 0
px_s_cfps:   db 'FPS ', 0
px_s_cready: db 'GET READY', 0
px_s_cfloor: db 'FLOOR ', 0
px_s_cdone:  db ' DONE', 0
px_s_ckills: db 'KILLS ', 0
px_s_csec:   db 'SECRETS ', 0
px_s_cloot:  db 'LOOT ', 0
px_s_ctime:  db 'TIME ', 0
px_s_clast:  db 'THE TOP', 0
px_s_cover:  db 'GAME OVER', 0
px_s_cwon:   db 'YOU ESCAPED', 0
px_s_cscore: db 'SCORE ', 0
px_s_chigh:  db 'HIGH SCORE', 0
px_s_cname:  db 'NAME ', 0
px_s_csave:  db 'ENTER: SAVE', 0
