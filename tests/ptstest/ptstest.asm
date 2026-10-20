; =============================================================================
; os8088 - tests/ptstest/ptstest.asm
;
; THE GATE FOR SPEC.md 5.6.9, and it is a DIFFERENTIAL one: the same set of
; coordinates drawn by `OSAPI_GFX_POINTS` and by `OSAPI_GFX_PIXEL` must put
; down the same pixels. Not "some pixels" and not "roughly there" - the slot
; exists to replace a gfx_pixel loop, so the only claim worth gating is that
; it is that loop's equal.
;
; ONE PASS, TWO BANDS. The pattern is drawn twice into the same window, the
; second copy PT_DY rows below the first, and the host compares band A against
; band B. That is what makes it a gate rather than a screenshot: no golden
; image, no reference build, and a failure names a row.
;
;   PT_DY IS EVEN, and that is load-bearing. gfx_ink's dither is screen
;   absolute in (x + y) parity (SPEC.md 39.4), so an ODD offset would make the
;   two bands legitimately differ on a dithered ink and the row would fail on
;   the one case it most wants to cover.
;
; FOUR CASES, because the draw path branches four ways - `gfx_ls_ink` answers
; three classes (SPEC.md 39.4) and the clip is the fourth axis:
;
;   1. a SOLID ink - the plain read-modify-write;
;   2. a DITHER ink - `gfx_ln_ink` = 1, the (x+y) parity arm;
;   3. the same as 1 with a CLIP REGION armed across the middle of the
;      pattern, which is the case SPEC.md 5.6.9.1 exists for. gfx_ls_bx1..by2
;      is whatever the last caller left in it, so a slot that trusted a stale
;      box would draw through a clip nobody set - and every windowed package
;      arms one, so this is the normal case rather than the corner.
;   4. a SOLID PAPER - black ink on a WHITE ground, which is the class an
;      ERASE is and the one this file went three revisions without drawing.
;      SPEC.md 5.6.9.3.1: `kern_small` shipped a loop that drew paper AS INK,
;      so every app-side erase in the tree was a second draw, and all three
;      cases above stayed green through it because not one of them asks the
;      slot to put a pixel OUT.
;
; The pattern is deliberately awkward: it straddles byte columns, and it steps
; BACKWARDS half way along so the points are not in address order - a slot that
; assumed monotonic x would pass a tidier pattern and fail this one.
;
; WHAT CASE 3 IS FOR is not a fabricated rect - a package cannot name one, and
; OSAPI_WM_CLIP_SET arms the window's OWN region. It is that [wm_clip_n] is
; then non-zero, so gfx_ls_box takes its .armed arm; and that the box case 2
; left in gfx_ls_bx1..by2 is at case 2's y, not this one's. A slot that trusted
; that stale box draws through a clip nobody set, which is exactly SPEC.md
; 5.6.9.1, and it is invisible on a tidy first call.
;
; Prefix pt_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PTSTEST', pt_entry

PT_BSS    equ 16

PT_N      equ 24                ; points in the pattern
PT_DY     equ 20                ; band B is this far below band A - EVEN, see
                                ; the header. It was 40, and came down with the
                                ; fourth case: a case is PT_DY + PT_H rows, so
                                ; four of them at the old pitch wanted 242 of
                                ; the 189 content rows this window has. 20 is
                                ; still clear of PT_H, so the two bands do not
                                ; touch, and still even
PT_W      equ 120               ; the pattern's extent, for the host's crop
PT_H      equ 18
PT_STEP   equ 40                ; ...and one case to the next. EVEN as well:
                                ; three cases at PT_DY * 2 put the third below
                                ; the content and the clip ate it whole, which
                                ; the row caught as `0 lit`. FOUR now reach
                                ; 4 + 3*40 + 38 = 162 of 189, so the window
                                ; did not have to grow - which matters because
                                ; tests/ptsext.py moves it onto a 200-row CGA

pt_entry:
    push si
    mov si, pt_tpl
    call OSAPI_WM_CREATE
    mov [pt_win], bx
    pop si
    ret

; -----------------------------------------------------------------------------
; pt_paint - W_PAINT, the gfx lock HELD. Draws all four cases.
; in:  SI = the window
; -----------------------------------------------------------------------------
pt_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    mov [pt_win], bx
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    add ax, 8
    mov [pt_x0], ax
    add dx, 4
    mov [pt_y0], dx

    ; --- case 1: a solid ink -------------------------------------------------
    mov byte [pt_gnd], CBLACK
    mov byte [pt_ink], CWHITE
    xor al, al                  ; unclipped
    call pt_case

    ; --- case 2: a dither ink, the (x+y) parity arm --------------------------
    mov ax, [pt_y0]
    add ax, PT_STEP
    mov [pt_y0], ax
    mov byte [pt_gnd], CBLACK
    mov byte [pt_ink], CLGRAY   ; SPEC.md 39.4: grey is the 50% dither on 1bpp
    xor al, al
    call pt_case

    ; --- case 3: solid, with a clip region across the pattern ----------------
    mov ax, [pt_y0]
    add ax, PT_STEP
    mov [pt_y0], ax
    mov byte [pt_gnd], CBLACK
    mov byte [pt_ink], CWHITE
    mov al, 1                   ; ...and this one arms a clip
    call pt_case

    ; --- case 4: solid PAPER, which is what an ERASE is ----------------------
    ; The ground and the ink are the other way up here and nowhere else: black
    ; on white asks the slot to CLEAR a bit, and cases 1 to 3 only ever ask it
    ; to set one. SPEC.md 5.6.9.3.1 is the defect that went through all three.
    mov ax, [pt_y0]
    add ax, PT_STEP
    mov [pt_y0], ax
    mov byte [pt_gnd], CWHITE
    mov byte [pt_ink], CBLACK
    xor al, al
    call pt_case

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_case - one case: build the pattern at [pt_y0], draw it with POINTS, build
;           it again PT_DY lower and draw THAT with gfx_pixel a point.
; in:  AL != 0 = arm a clip region over both bands first
; -----------------------------------------------------------------------------
pt_case:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pt_clip], al
    push ax
    mov al, [pt_gnd]            ; A FLAT GROUND under BOTH bands, in ONE fill
    call OSAPI_SET_COLOR        ; so the two start identical. Without it the
    mov ax, [pt_x0]             ; ink is white on the window's white content,
    mov bx, [pt_y0]             ; every case compares background to background,
    mov cx, ax                  ; two solid bands agree perfectly and the row
    add cx, PT_W - 1            ; is GREEN having tested nothing - which is
    mov dx, bx                  ; docs/WRITING-TESTS.md 1 exactly, and is how
    add dx, PT_DY + PT_H - 1    ; the first version of this file read
    call OSAPI_GFX_FILL
    mov al, [pt_ink]            ; ...and then the case's own ink
    call OSAPI_SET_COLOR
    pop ax
    or al, al
    jz .a
    mov bx, [pt_win]            ; SPEC.md 11.3's region, armed the way every
    call OSAPI_WM_CLIP_SET      ; windowed package arms it - a package cannot
    jnc .a                      ; name a rect of its own, and does not need to:
    mov byte [pt_clip], 0       ; what this case is for is [wm_clip_n] != 0, so
                                ; gfx_ls_box takes its .armed arm and the box
                                ; the previous case left behind is WRONG for
                                ; this one (SPEC.md 5.6.9.1). CF = 1 is "not
                                ; one pixel visible" and there is nothing to
                                ; clear
.a:
    mov ax, [pt_y0]             ; --- band A, through the new slot
    call pt_build
    mov si, pt_pat
    mov cx, PT_N
    call OSAPI_GFX_POINTS

    mov ax, [pt_y0]             ; --- band B, one gfx_pixel a point
    add ax, PT_DY
    call pt_build
    mov si, pt_pat
    mov di, PT_N
.px:
    mov cx, [si]
    mov dx, [si+2]
    call OSAPI_GFX_PIXEL
    add si, 4
    dec di
    jnz .px

    cmp byte [pt_clip], 0
    je .out
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_build - fill pt_pat with the pattern, based at ([pt_x0], AX)
;
; Awkward on purpose: it straddles byte columns, the x steps BACKWARDS half way
; so the points are not in address order, and entry 12 repeats entry 11 - a set
; is not a path, and a slot that assumed monotonic x or unique points would
; pass a tidier pattern and fail this one.
; -----------------------------------------------------------------------------
pt_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, pt_pat
    mov bx, ax                  ; BX = the base row
    xor cx, cx                  ; CX = index
.each:
    mov ax, cx
    xor si, si                  ; SI = the row band this point belongs to
    cmp cx, 12
    jb .fwd
    mov ax, 23
    sub ax, cx                  ; ...and back the other way
    mov si, 9
.fwd:
    mov dx, ax
    add dx, dx
    add dx, dx
    add dx, dx                  ; x = 8 * i, so every byte column is touched
    add dx, ax                  ; ...+ i, so the bit inside it walks too
    add dx, [pt_x0]
    mov [di], dx
    mov dx, ax
    and dx, 7
    add dx, si                  ; ...and the second twelve are a second BAND,
    add dx, bx                  ; so all 24 points are DISTINCT. They were not:
                                ; the second twelve repeated the first in
                                ; reverse, and a kernel patched to draw every
                                ; OTHER point still drew every position, so the
                                ; row stayed green on a broken slot
    mov [di+2], dx
    add di, 4
    inc cx
    cmp cx, PT_N
    jb .each
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pt_tpl:
    dw 200, 20, 176, 208       ; tall enough for three cases at PT_STEP
    dw pt_ttl, pt_paint, 0, 0

pt_ttl:   db 'PtsTest', 0

pt_pat:   times PT_N * 4 db 0   ; the coordinate array, in the IMAGE: it is an
                                ; X slot, so the stub puts our own segment in
                                ; ES and we hand over a bare offset

    OS88_BSS PT_BSS
    OS88_IMAGE_END

pt_win     equ os88_image_end + 0    ; word: our window
pt_x0      equ os88_image_end + 2    ; word: the pattern's left
pt_y0      equ os88_image_end + 4    ; word: ...and the current case's top
pt_clip    equ os88_image_end + 6    ; byte: this case armed a clip
pt_ink     equ os88_image_end + 7    ; byte: ...and its ink
pt_gnd     equ os88_image_end + 8    ; byte: ...and the ground under both bands,
                                     ; which case 4 turns over (SPEC.md
                                     ; 5.6.9.3.1)
