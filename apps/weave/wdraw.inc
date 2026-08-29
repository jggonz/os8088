; =============================================================================
; os8088 - apps/weave/wdraw.inc     WEAVE's paint and hit-test cores
;
; Part of WEAVE, the .WAB runtime (docs/WEAVE-SPEC.md). %included by
; apps/weave/weave.asm after the compiled C, after wblob.inc, and after
; os88ui.inc and os88line.inc - whose routines and constants this file names.
;
; THIS FILE IS THE SEAM WEAVE-SPEC 1.2 NAMES. The component library's paint
; and hit-test cores are "assembly source %included by BOTH packages (the
; apps/os88ui.inc model, SPEC.md 20.5.1 - this platform's only code-sharing
; mechanism)", because they run under the gfx lock, once per callback, and are
; what LOOM's Preview (WEAVE-SPEC 1.7) paints with. The flow walk is NOT one
; of them and is C in wflow.c, for the reason WEAVE-SPEC 1.2 gives: it emits
; no gfx call.
;
; WHAT IS HERE AND WHAT IS NOT. Everything below takes a rect in SCREEN PIXELS
; and arguments already resolved - a label's bytes as a (segment, offset, len)
; triple, a meter's value and max as words. Which property carries which,
; where the atom pool is, and how a card's components are laid out are all
; wpaint.c's and wflow.c's: this file draws, and knows nothing about the
; bundle's shape. That is the line LOOM has to be able to cross - Preview
; hands these routines rects out of the SAME walk and gets the same picture.
;
; ONE PRIMITIVE CALL PER ROW, ALWAYS. OSAPI_FONT_RUN paints a run's cell
; backgrounds and its glyphs in one pass (SPEC.md 6.1), so a line is never
; momentarily blank - the erase-then-letter pair is the canonical double-draw
; and it is invisible in an emulator (CLAUDE.md). Nothing in this file may
; grow a `gfx_fill` followed by a `font_str` over the same cells. The one
; place two calls land on the same pixels is WD_BOLD, and it is the other way
; round on purpose: the opaque run goes down COMPLETE and the strike only ORs
; ink into it, so there is no instant at which anything is blank.
;
; ...AND THE PEN LANDS ON A MULTIPLE OF 8. WEAVE-SPEC 7.1.2 rounds the content
; origin UP at LAYOUT time, so every x this file is given is already 8-aligned
; and font_run's single-store fast path is taken on both 1bpp adapters. That
; is a property of the caller, not of this file, and it is why the round is in
; the walk rather than here.
;
; THE 47 PEN IS THE CALLER'S, ONCE PER COMPONENT. wpaint.c calls
; os88_gfx_pen() before every component and these routines inherit it, so the
; frame, the rule, the bar and the label of one control cannot disagree about
; whether it is greyed (SPEC.md 47 rule 2). os88ui_btn and os88ui_glyph take
; their own flag instead and PUT THE PEN BACK, which is why wd_button and
; wd_glyph are the two that hand the state on rather than leaning on it.
;
; All cdecl (SPEC.md 73.3), and see wblob.inc's header for the segment rules.
; =============================================================================

; W_RUNMAX bounds one drawn run, and the .bss buffer at the foot of this file.
; The widest content grid the family has is Hercules' 89 cells (WEAVE-SPEC
; 7.1.1) and a resized window can be no wider than its adapter, so 160 is more
; than twice the worst case.
%define W_RUNMAX 160

; The flag word both text cores take. It is a WEAVE vocabulary rather than a
; forwarded os88ui one because neither bit exists there: WD_BOLD is 6.2's
; double strike and WD_PAD is 6.2's "padding is the erase".
WD_BOLD equ 1
WD_PAD  equ 2

; wpaint.c answers os88ui_sbhit's parts in its own names (WD_SB_* in weave.h),
; because a C file may not name an nasm equ. These four lines are what stops
; the two vocabularies drifting: renumbering the bar's parts fails the build
; here, naming the file that has to move, instead of scrolling a list the
; wrong way at run time.
%if OS88UI_SBNONE != 0 || OS88UI_SBUP != 1 || OS88UI_SBDOWN != 2
  %error "wdraw: OS88UI_SB* renumbered - WD_SB_* in apps/weave/weave.h follows"
%endif
%if OS88UI_SBPGUP != 3 || OS88UI_SBPGDN != 4 || OS88UI_SBTHUMB != 5
  %error "wdraw: OS88UI_SB* renumbered - WD_SB_* in apps/weave/weave.h follows"
%endif

section .text

; =============================================================================
; TEXT - the two cores every component with words in it goes through
; =============================================================================

; -----------------------------------------------------------------------------
; wd_clip - internal: how many bytes of a run to stage
; in:  CX = the source length, DX = the cell budget (0 = unbounded)
; out: CX = the bytes to copy; flags clobbered, nothing else touched
;
; The cell budget is WEAVE-SPEC 6.2's clip - "`label` is one line, clipped at
; its width" - done at CELL granularity, which is the only granularity a
; font_run has. The W_RUNMAX cap under it is the buffer's and is never
; reached by a laid-out component: the walk clamps every width to CW.
; -----------------------------------------------------------------------------
wd_clip:
    or dx, dx
    jz .cap
    cmp cx, dx
    jbe .cap
    mov cx, dx
.cap:
    cmp cx, W_RUNMAX - 1
    jbe .out
    mov cx, W_RUNMAX - 1
.out:
    ret

; -----------------------------------------------------------------------------
; wd_pad - internal: finish the staged run
; in:  CX = bytes already in w_runbuf, DX = the cell budget, BL = WD_* flags
; out: w_runbuf NUL-terminated; AX/CX/DX/DI clobbered, ES = DS
;
; WD_PAD pads with spaces out to the budget, which is WEAVE-SPEC 6.2's
; "padding is the erase - no fill-then-letter pair": an INVERT run's black bar
; has to cover the whole component and a `.text` write has to cover whatever
; longer string it replaces, and both are the SAME opaque font_run rather than
; a fill under a shorter one.
; -----------------------------------------------------------------------------
wd_pad:
    mov di, w_runbuf
    add di, cx
    test bl, WD_PAD
    jz .nul
    cmp dx, W_RUNMAX - 1
    jbe .dxok
    mov dx, W_RUNMAX - 1
.dxok:
    cmp cx, dx
    jae .nul
    push ds
    pop es                          ; cc8086:allow ES is OURS for the store
    mov ax, dx                      ; below and the caller restores it; DF is
    sub ax, cx                      ; cleared here and left clear
    mov cx, ax
    mov al, ' '
    cld
    rep stosb
.nul:
    mov byte [di], 0                ; DS-relative: ours, whatever ES is
    ret

; -----------------------------------------------------------------------------
; wd_emit - internal: put the staged run on the glass
; in:  CX = x, DX = y, AL = ink, AH = paper, BL = WD_* flags
; out: nothing kept; AX/CX/DX/SI clobbered
;
; An EMPTY run draws nothing at all rather than costing a call: a font_run of
; "" still pays PERFORMANCE.md's ~756 us of fixed cost to paint no cells.
;
; WD_BOLD is WEAVE-SPEC 6.2's double strike, and the ORDER is the whole of why
; it is not a double-draw: OSAPI_FONT_RUN is OPAQUE and lands the finished
; cells in one pass, then OSAPI_FONT_STR_XPARENT is TRANSPARENT and only ORs the same
; glyphs one pixel right into them. Nothing is ever blank and no pixel goes
; dark-to-light-to-dark. It is the one component style that costs a second
; gfx call, which is WEAVE-SPEC 14's `label` row plus ~0.8 ms when the style
; byte carries BOLD and exactly that row when it does not.
; -----------------------------------------------------------------------------
wd_emit:
    mov si, w_runbuf
    cmp byte [si], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop bx
    pop ax
    test bl, WD_BOLD
    jz .out
    push ax
    push cx
    call OSAPI_SET_COLOR            ; AL = the ink the run just used, so the
    pop cx                          ; strike inherits [gfx_dis] with it and a
    pop ax                          ; greyed bold label stipples like a greyed
    inc cx                          ; plain one (47 rule 2)
    mov si, w_runbuf
    call OSAPI_FONT_STR_XPARENT
.out:
    ret

; -----------------------------------------------------------------------------
; void w_draw_run(int x, int y, unsigned seg, unsigned off, unsigned len,
;                 int cells, int ink, int paper, int flags)
;
; ONE opaque text run of `len` bytes taken from the bundle claim at seg:off,
; clipped to `cells` cells and optionally padded out to them.
;
; The bytes are staged through a resident buffer because OSAPI_FONT_RUN takes
; a DS pointer and the bundle is a claim in a segment of its own. That copy is
; the price of the format's own 62KB cap, and it is one `rep movsb` of at most
; 160 bytes against a ~756 us primitive call.
;
; A run longer than the buffer is CLIPPED rather than refused: the caller has
; already clamped every component to CW cells (WEAVE-SPEC 7.2), so a longer
; run is a bundle lying about a length, and the honest answer to that is a
; short line on the glass and the validator's refusal - not a buffer walked
; off the end.
; -----------------------------------------------------------------------------
_w_draw_run:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov cx, [bp+12]                 ; len
    mov dx, [bp+14]                 ; cells
    call wd_clip
    push cx
    jcxz .staged

    push ds
    mov ax, [bp+8]                  ; the claim segment. SS-relative, so it is
    mov si, [bp+10]                 ; read before DS moves
    mov di, w_runbuf
    push ds
    pop es                          ; ES = ours: the DESTINATION
    mov ds, ax                      ; DS = the claim: the SOURCE
    cld
    rep movsb                       ; cc8086:allow both segments loaded here on
    pop ds                          ; purpose, both restored, DF left clear
.staged:
    pop cx
    mov bx, [bp+20]                 ; BL = flags
    mov dx, [bp+14]
    call wd_pad
    mov cx, [bp+4]
    mov dx, [bp+6]
    mov al, [bp+16]                 ; AL = ink...
    mov ah, [bp+18]                 ; ...AH = paper (WEAVE-SPEC 2.5.2's INVERT
    call wd_emit                    ; is the caller swapping these two, which
                                    ; is the whole of the style vocabulary
                                    ; that touches colour - SPEC.md 39.4)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; void w_draw_text(int x, int y, const char *s, int cells,
;                  int ink, int paper, int flags)
;
; The same run from a NUL string in OUR segment - a wrapped line the painter
; composed, a number, a status sentence. It stages through the same buffer and
; falls into the same tail, so the clip, the pad and the bold strike are one
; body and a caller cannot get a different answer by choosing a source.
; -----------------------------------------------------------------------------
_w_draw_text:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov si, [bp+8]                  ; ours: DS-relative
    mov di, si
    xor cx, cx
.len:
    cmp byte [di], 0
    je .got
    inc di
    inc cx
    cmp cx, W_RUNMAX - 1
    jb .len
.got:
    mov dx, [bp+10]                 ; cells
    call wd_clip
    push cx
    jcxz .staged
    mov di, w_runbuf
    push ds
    pop es                          ; cc8086:allow ES = DS for one copy inside
    cld                             ; our own segment; restored below
    rep movsb
.staged:
    pop cx
    mov bx, [bp+16]
    mov dx, [bp+10]
    call wd_pad
    mov cx, [bp+4]
    mov dx, [bp+6]
    mov al, [bp+12]
    mov ah, [bp+14]
    call wd_emit

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; =============================================================================
; THE ONE-CALL SHAPES (WEAVE-SPEC 6.3, 6.4)
;
; They take the pen as they find it, which is how `disabled` reaches them: one
; os88_gfx_pen() per component in wpaint.c sets CDGRAY *and* the dither flag
; together, and on both 1bpp adapters that is the difference between a greyed
; control and one that is pixel-identical to a live one (SPEC.md 47 rule 3 -
; the defect this tree has fixed five separate times).
; =============================================================================

; -----------------------------------------------------------------------------
; void wd_rule(int x1, int x2, int y)     - WEAVE-SPEC 6.3's `rule`, one hline
; -----------------------------------------------------------------------------
_wd_rule:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_GFX_HLINE
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; void wd_box(int x1, int y1, int x2, int y2)   - `box`, one frame
; -----------------------------------------------------------------------------
_wd_box:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; void wd_meter(int x1, int y1, int x2, int y2, int value, int max)
;
; WEAVE-SPEC 6.4's framed horizontal bar. TWO calls for a full paint, which is
; 14's row: the frame, then one fill of the part that is filled. A value of 0
; costs ONE - there is nothing to fill - and that is the honest answer rather
; than a call that paints nothing.
;
; The interior is x1+1..x2-1 and the filled span is `interior * value / max`,
; which needs 32 bits on the way through: an interior can be 1,278 px and a
; value 32,000, and the product is 40 million. `mul` puts the high half in DX
; and `div` takes it back, so the quotient - which is at most the interior,
; because value is clamped to max first - is exact and cannot overflow.
; MUL WRITES DX: nothing may be banked there across it (calc's cal_layout lost
; a row's y1 exactly here, and every button came out at y = 0).
; -----------------------------------------------------------------------------
_wd_meter:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, [bp+4]                  ; --- the frame ---
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call OSAPI_GFX_FRAME

    mov di, [bp+14]                 ; DI = max
    or di, di
    jz .out                         ; a max of 0 fills nothing and divides by
                                    ; nothing (wval refuses one, and a painter
                                    ; that trusts that is a painter that
                                    ; divides by zero the day it changes)
    mov si, [bp+12]                 ; SI = value, clamped to max
    cmp si, di
    jbe .vok
    mov si, di
.vok:
    or si, si
    jz .out
    mov ax, [bp+8]                  ; interior width = (x2-1) - (x1+1) + 1
    sub ax, [bp+4]
    dec ax
    jle .out                        ; no interior: the frame is the whole of it
    mul si
    div di                          ; AX = the filled span, in pixels
    or ax, ax
    jz .out
    mov cx, [bp+4]                  ; x1+1 .. x1+span
    inc cx
    add ax, cx
    dec ax
    mov bx, cx
    mov cx, ax
    mov ax, bx
    mov bx, [bp+6]
    inc bx
    mov dx, [bp+10]
    dec dx
    cmp bx, dx
    jg .out                         ; a frame with no rows inside it
    cmp ax, cx
    jg .out
    call OSAPI_GFX_FILL
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; void wd_xor(int x1, int y1, int x2, int y2)
;
; WEAVE-SPEC 6.8's selection bar. ONE call each way and its own inverse, which
; is 14's "list | selection move | 2 (XOR) | ~1.6 ms".
;
; SPEC.md 13.8 forbids an XOR-ed PRESSED state and this is not one: a press is
; a transient a repaint knows nothing about, where a list's selection is state
; the painter re-applies from `sel` on every paint (calc's cal_restore rule -
; "a row is one opaque run over EXACTLY its own rect"). The window must then
; never ask for a partial OSAPI_WM_DAMAGE, which WEAVE does not (SPEC.md
; 11.90's interlock, and it wants OSAPI_WM_OWNBG first anyway).
; -----------------------------------------------------------------------------
_wd_xor:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call OSAPI_GFX_XOR_FILL
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; =============================================================================
; THE os88ui WRAPPERS (WEAVE-SPEC 6.5, 6.6, 6.8)
;
; Each one builds the flag word os88ui.inc documents out of plain 0/1
; arguments, so no OS88UI_* constant is ever copied into C - the one place two
; vocabularies could drift is the scroll bar's parts, and the %if at the top
; of this file is what stops that one.
; =============================================================================

; -----------------------------------------------------------------------------
; void wd_button(int *rect, const char *label, int dis, int down, int fill)
;
; WEAVE-SPEC 6.5. `rect` is a pointer into wpaint.c's ONE rect table - the
; same four words the hit test reads through os88ui_bfind, so the drawn
; control and the clickable control cannot drift (SPEC.md 22's fm_hit
; discipline, apps/calc/calc.asm's cal_layout rule).
;
; `fill` is os88ui's own question - CAN THIS BUTTON BE DRAWN A SECOND TIME
; WITHOUT THE GROUND BEING REPAINTED FIRST? - and the card painter answers 0
; because the kernel whitens a window's content before W_PAINT and w_repaint2
; whitens it on every other path. A per-control repaint (wave 3's press) must
; answer 1, or a greyed caption ORs on top of the live one it is replacing and
; comes out pixel-identical to it.
; -----------------------------------------------------------------------------
_wd_button:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di
    cmp word [bp+8], 0
    je .nodis
    or di, OS88UI_DIS
.nodis:
    cmp word [bp+10], 0
    je .nodown
    or di, OS88UI_DOWN
.nodown:
    cmp word [bp+12], 0
    je .nofill
    or di, OS88UI_FILL
.nofill:
    mov bx, [bp+4]                  ; the rect: OURS, so DS-relative, which is
    mov si, [bp+6]                  ; what os88ui_btn's own ds: overrides want
    call os88ui_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; void wd_glyph(int x, int y, int radio, int on, int dis)
;
; WEAVE-SPEC 6.6's 12x12 check box or radio button, over its own white box.
; It draws NO LABEL and gives no combined hit rect - both are wpaint.c's, and
; the label is why that file sets the 47 pen itself around it.
;
; 44-64 OSAPI_GFX_PIXEL calls, 35-50 ms field-measured. A toggle repaints ONE
; glyph, never the row (14's `check / radio` row).
; -----------------------------------------------------------------------------
_wd_glyph:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    mov al, OS88UI_GCHECK
    cmp word [bp+8], 0
    je .kind
    mov al, OS88UI_GRADIO
.kind:
    cmp word [bp+10], 0
    je .noon
    or al, OS88UI_GON
.noon:
    xor ah, ah
    cmp word [bp+12], 0
    je .go
    mov ah, 1                       ; THE DISABLED FLAG RIDES IN AH here and
.go:                                ; not in DI - os88ui_glyph's one departure
    mov cx, [bp+4]                  ; from os88ui_btn's shape
    mov dx, [bp+6]
    call os88ui_glyph
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; void wd_sbar(int *blk)                     - the whole bar, ~16 calls
; void wd_sbmove(int *blk, int oldpos)       - the thumb only, 3 calls
; int  wd_sbhit(int *blk, int x, int y)      - WD_SB_* (weave.h), 0 calls
;
; `blk` is os88ui.inc's seven-word scroll block, which wpaint.c fills:
;
;     +0 x1  +2 y1  +4 x2  +6 y2   the bar, ABSOLUTE and INCLUSIVE
;     +8 total   +10 fit   +12 pos
;
; Everything else - the arrow cells, the track, the thumb's height and travel -
; is derived INSIDE os88ui.inc so a painter and a hit-tester cannot hold
; different opinions about it. wd_sbmove is WEAVE-SPEC 6.8's scroll: the thumb
; translates and the frame, the rules, the arrows and the untouched track stay
; exactly where they are.
; -----------------------------------------------------------------------------
_wd_sbar:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    call os88ui_sbar
    pop bx
    pop bp
    ret

_wd_sbmove:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov ax, [bp+6]                  ; the pos it was at; the block already
    call os88ui_sbmove              ; holds the new one
    pop bx
    pop bp
    ret

_wd_sbhit:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov bx, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call os88ui_sbhit
    xor ah, ah                      ; a shim answering in AL must clear AH -
    pop dx                          ; without it the C `int` carries a stale
    pop cx                          ; high byte, which is a plausible value and
    pop bx                          ; never the right one
    pop bp
    ret

; -----------------------------------------------------------------------------
; int wd_hit(int *rects, int n, int x, int y)   - THE HIT TEST CORE
;
; Which of `n` four-word rects holds the point, INDEX PLUS ONE, 0 for none.
; It reads the same table wd_button was handed a row of, which is the whole
; point of there being one table (SPEC.md 22).
;
; Plus one so that 0 means "nothing", which is the sentinel os88ui_arm and
; os88ui_fire already use - so wave 3's whole press-and-release cycle is
; `wd_hit -> arm` then `fire`, `wd_hit`, compare, with no separate flag.
; -----------------------------------------------------------------------------
_wd_hit:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov bx, [bp+4]
    mov ax, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call os88ui_bfind
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; =============================================================================
; THE INPUT FIELD (WEAVE-SPEC 6.7)
; =============================================================================

; -----------------------------------------------------------------------------
; void wd_input(int x1, int y1, int x2, int y2, const char *s)
;
; The field AT REST: the frame and its contents, no caret and no editing.
; `disabled` reaches it through the pen the caller already set, as it does
; every shape above - only the two os88ui wrappers take a flag, and only
; because os88ui puts the pen back itself.
; Wave 3 gives each `input` a real apps/os88line.inc block and takes over the
; keystroke path; what this shares with that day is the thing that would
; otherwise drift - THE PEN'S X - and it shares it by asking os88line_pen for
; it through a resident block rather than by repeating the arithmetic.
;
; WEAVE-SPEC 7.1.2 is why that matters: os88line's inset is 3, so an
; unrounded pen sits at 3 mod 8 and NO field in the family ever takes
; font_run's single-store path, at any window position, on any adapter. That
; was measured at 246 transient pixels per keystroke on a 5150.
;
; ONE opaque run, padded to the columns the box shows: the padding IS the
; erase (WEAVE-SPEC 6.2), so there is no fill under a shorter string and no
; instant at which the field is blank. os88line_draw itself is NOT called
; here, and the reason is SPEC.md 47: it forces CBLACK for its frame and its
; text, so a greyed field would come out with a solid frame and dithered
; letters - two halves of one control disagreeing, which is rule 2's own
; failure. Wave 3 either fixes that in os88line.inc (it is the shared body's
; bug, not WEAVE's) or keeps this path for the disabled case.
; -----------------------------------------------------------------------------
_wd_input:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov ax, [bp+4]                  ; --- the frame, in the pen the caller set
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call OSAPI_GFX_FRAME

    ; --- the geometry, ASKED of os88line.inc rather than repeated -----------
    mov si, wd_lnblk                ; ours: DS-relative
    mov ax, [bp+4]
    mov [si+LN_X1], ax
    mov ax, [bp+6]
    mov [si+LN_Y1], ax
    mov ax, [bp+8]
    mov [si+LN_X2], ax
    mov ax, [bp+10]
    mov [si+LN_Y2], ax
    call os88line_cols              ; CX = the columns the box shows, >= 1
    mov [wd_icols], cx
    call os88line_pen               ; AX = the pen, ROUNDED UP to a multiple
    mov [wd_ipen], ax               ; of 8 - the whole reason this block exists

    ; --- the contents: one opaque run, padded to those columns -------------
    xor cx, cx                      ; CX = the string's length
    mov si, [bp+12]
    or si, si
    jz .stage                       ; a null pointer is an empty field
.len:
    cmp byte [si], 0
    je .stage
    inc si
    inc cx
    cmp cx, W_RUNMAX - 1
    jb .len
.stage:
    mov dx, [wd_icols]
    call wd_clip
    push cx
    jcxz .staged
    mov si, [bp+12]
    mov di, w_runbuf
    push ds
    pop es                          ; cc8086:allow ES = DS for one copy inside
    cld                             ; our own segment; restored at the exit
    rep movsb
.staged:
    pop cx
    mov bx, WD_PAD                  ; the padding IS the erase (WEAVE-SPEC 6.2)
    mov dx, [wd_icols]
    call wd_pad

    mov cx, [wd_ipen]
    mov dx, [bp+6]
    add dx, LN_INSET                ; the glyph band, os88line's own inset
    mov al, CBLACK                  ; ink; [gfx_dis] carries `disabled`, which
    mov ah, CWHITE                  ; font_run honours on both its paths
    call wd_emit

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

section .bss

; The staging buffer, in bss - which SPEC.md 73.9 prices as the cheap half,
; where an initialised array in .data is paid for twice.
w_runbuf: resb W_RUNMAX

; ONE apps/os88line.inc block, filled per draw. A field at rest has no state
; to keep between paints - no caret, no view offset, no focus - so one scratch
; serves every `input` on the card, which is os88ui_krect's argument one file
; over: they are all UI-task, all under the gfx lock, and nothing in the
; drawing path re-enters. Wave 3's editing needs one block PER FIELD and this
; becomes the painter's alone.
wd_lnblk: resb OS88LINE_SZ
wd_ipen:  resw 1                    ; ...and its two answers, banked. BP is the
wd_icols: resw 1                    ; frame pointer and an 8086 cannot address
                                    ; [sp], so a scratch word is the shape

section .text                       ; SWITCH BACK (CLAUDE.md's section
                                    ; discipline): the next thing the shim
                                    ; %includes must land in .text
