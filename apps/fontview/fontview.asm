; =============================================================================
; os8088 - apps/fontview/fontview.asm
;
; FONT VIEWER lists the .F88 families in the system disk's FONTS/ folder and
; renders an editable specimen in the selected face (SPEC.md 90).  It is a
; normal package, carried in APPS/ on the system disk; the F88 declaration in
; its header is all the file manager needs to launch it from a face file.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FONT VIEWER', fv_entry, 3

; A page carrying one large A: a font document rather than a generic app.
    OS88_ICON16
    dw 0x7FF0, 0x7FF8, 0x7FFC, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x0000, 0x3FE0, 0x2030, 0x2028
    dw 0x2184, 0x2284, 0x2444, 0x27C4
    dw 0x2844, 0x3024, 0x3024, 0x2004
    dw 0x2004, 0x2004, 0x3FFC, 0x0000
    OS88_ICON16_END

; The declaration is harvested while the disk is mounted.  No run-time
; registration and no kernel special case are involved (SPEC.md 54.6, 90).
    OS88_ASSOC16
    db 1
    OS88_ASSOC_EXT 'F88'
    OS88_ASSOC16_END

FV_W        equ 620                 ; fits VGA/CGA; leaves room for long samples
FV_H        equ 155                 ; the whole CGA desktop band, dock excluded
FV_CW       equ FV_W - 2
FV_CH       equ FV_H - TITLE_H - 1
FV_DIVX     equ 116
FV_LISTX    equ 5
FV_LISTY    equ 15
FV_ROWH     equ 11
FV_RIGHTX   equ 124
FV_TEXTX    equ 128                 ; 8-aligned from a snapped content origin
FV_BOXY     equ 29
FV_BOXBOT   equ 116
FV_TEXTY    equ 35
FV_SAMPLEW  equ 480
FV_TEXTMAX  equ 127

; -----------------------------------------------------------------------------
; fv_entry - scan the machine's faces, make the window and defer the first
; face read until the window is visible.
;
; The launch name and its catalogue match are INLINE here and not two procs:
; each runs once, and their own call/ret and register banks were most of
; their size. CX and ES are banked because the procs they replaced kept them
; for this one's caller.
; -----------------------------------------------------------------------------
fv_entry:
    push si
    push di
    push cx
    push es
    push ds
    pop es                          ; ES:DI = our own segment, copy and compare
    call ty_init
    call OSAPI_ARG_FILE             ; first: OSAPI_ARG_FILE is read-and-clear,
    jc .scan                        ; and SI names the KERNEL's segment
    mov di, fv_arg
    mov cx, TY_NAMSZ - 1            ; [fv_arg + TY_NAMSZ - 1] is the zero the
    push ds                         ; loader left in the bss (nothing has
    mov ax, KERNEL_SEG              ; lettered fv_line yet), so the copy is
                                    ; NUL-terminated however long the name
    mov ds, ax
.copy:
    lodsb
    stosb
    or al, al
    loopnz .copy
    pop ds
.scan:
    call ty_scan
    ; If an F88 launched us, make that family the initial selection. A foreign
    ; face whose name is not installed - or no launch name at all, which
    ; leaves fv_arg empty and so equal to no family - opens the catalogue at
    ; its first row.
    xor bx, bx
    mov si, ty_fnames
.family:
    cmp bl, [ty_nfam]
    jae .picked
    mov di, fv_arg
    mov cx, TY_NAMSZ
    push si
    repe cmpsb
    pop si
    je .found
    add si, TY_NAMSZ
    inc bx
    jmp short .family
.found:
    mov [fv_selected], bl
.picked:
    mov si, fv_tpl                  ; [fv_loaded], [fv_pending] and
    call OSAPI_WM_CREATE            ; [fv_textlen] start as IMAGE bytes
    jc .out
    mov [fv_win], bx
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE_NOKEEP      ; AX, DX and the flags are all set again
                                    ; below before anything reads them
    mov al, 1
    call OSAPI_WM_SNAP              ; makes fv_x + FV_TEXTX byte-aligned
    mov ax, fv_onwake
    call OSAPI_WM_ONWAKE
    mov si, fv_about                ; 'About Font Viewer...' under our name in
    call OSAPI_ABOUT_SET            ; the bar (SPEC.md 12.2, 90.9)
    call OSAPI_WM_WAKE              ; ordinary launch needs the same first load
    clc
.out:
    pop es
    pop cx
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; fv_onwake - the only path that turns the floppy for a face change.
; W_ONKEY/W_ONCLICK merely choose and post this callback (SPEC.md 90.1).
;
; ONE EXTERNAL FACE AT A TIME, and the old one goes back BEFORE the new one is
; claimed: opening first held two face claims for the length of a floppy
; read (each is the face file's own size now, SPEC.md 6.4.2 - 2KB a shipped
; face where it was 8). Face 0 stands in meanwhile, so nothing
; ever points at a freed claim, and it is also what a face that will not open
; leaves showing - the system face, ty_open's own answer to every refusal
; (SPEC.md 6.4, 90).
; -----------------------------------------------------------------------------
fv_onwake:
    xor al, al
    xchg al, [fv_pending]
    or al, al
    jz .out
    cmp byte [ty_nfam], 0
    je .paint
    mov dl, [fv_selected]           ; DL = the row, which every ty_* call
    cmp dl, [fv_loaded]             ; below preserves
    je .paint
    mov byte [fv_loaded], 0xFF      ; nothing open until the open succeeds
    xor ax, ax
    xchg al, [fv_face]
    call ty_close                   ; the old face back first...
    xor ax, ax
    call ty_use                     ; ...face 0 meanwhile...
    mov al, dl
    call ty_openfam                 ; ...and only then the new one
    jc .bad
    mov [fv_face], al
    xor ah, ah
    call ty_use
    call ty_cache                   ; refusal is harmless: ty_putn falls back
    mov [fv_loaded], dl
    jmp short .paint                ; [fv_error] is already 0: fv_choose
.bad:                               ; cleared it when it posted this wake
    mov [fv_error], al
.paint:
    call OSAPI_GFX_LOCK
    call fv_redraw
    call OSAPI_GFX_UNLOCK
.out:
    ret

; -----------------------------------------------------------------------------
; fv_about / fv_abdismiss - the standard About card (SPEC.md 20.5.1.1, 90.9)
;
; This package shipped with NO handler at all, which is the case that section
; was written about: the cheapest thing an author can do is nothing, and what
; goes missing when they do it is the credit.
;
; fv_about is the HANDLER, so it is os88ui_about and not the _d entry -
; ui_dispatch arms no clip region before it far-calls us.
; -----------------------------------------------------------------------------
fv_about:
    push bx
    push si
    mov byte [fv_abon], 1
    mov bx, si                      ; SI = our window on entry
    mov si, fv_ablines
    call os88ui_about
    pop si
    pop bx
    ret

; Any key or click takes it down. The repaint is the WHOLE window and not the
; card's rect: what the card covered is a specimen row, a catalogue row or the
; divider, and fv_redraw is the one routine that knows how to put all three
; back (SPEC.md 90.9).
fv_abdismiss:
    cmp byte [fv_abon], 0
    je .none
    mov byte [fv_abon], 0
    call fv_redraw
    stc                             ; CF = 1: the event was OURS, and the
    ret                             ; caller must not act on it as well
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; Keyboard: arrows walk the catalogue; printable ASCII and Backspace edit the
; specimen.  The right pane alone is repainted for ordinary typing.
; Banks AX and BX: nothing below changes any other register.
; -----------------------------------------------------------------------------
fv_onkey:
    push ax
    push bx
    call fv_abdismiss               ; the card eats the keystroke that takes it
    jc .out                         ; down, so a specimen edit is not also made
    cmp ah, KSC_UP
    je .prev
    cmp ah, KSC_LEFT
    je .prev
    cmp ah, KSC_DOWN
    je .next
    cmp ah, KSC_RIGHT
    je .next
    mov bl, [fv_textlen]
    xor bh, bh
    cmp al, 8
    je .back
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    cmp bl, FV_TEXTMAX
    jae .out
    mov [fv_text + bx], al
    inc bx
    jmp short .set
.back:
    dec bx                          ; an empty specimen goes to -1 and stops
    js .out
.set:
    mov [fv_textlen], bl
    mov byte [fv_text + bx], 0
    call fv_redraw_right
    jmp short .out
.prev:
    mov al, [fv_selected]
    or al, al
    jnz .pdec
    mov al, [ty_nfam]
.pdec:
    dec al
    jmp short .pick
.next:
    mov al, [fv_selected]
    inc al
    cmp al, [ty_nfam]
    jb .pick
    xor al, al
.pick:
    cmp byte [ty_nfam], 0           ; an empty catalogue has nothing to walk
    je .out
    call fv_choose
.out:
    pop bx
    pop ax
    ret

; A click in a catalogue row selects it.  CX/DX arrive in screen coordinates.
; Banks AX, BX and DX: nothing below changes any other register.
fv_onclick:
    push ax
    push bx
    push dx
    call fv_abdismiss               ; ...and the click likewise: dismissing is
    jc .out                         ; not also a selection (SPEC.md 90.9)
    push dx                         ; the click's y
    mov bx, si
    call fv_origin                  ; AX/DX = the content origin
    pop bx
    add ax, FV_DIVX
    cmp cx, ax
    jae .out
    add dx, FV_LISTY
    sub bx, dx
    jb .out
    xchg ax, bx                     ; AX = y below the first row's top
    mov bl, FV_ROWH
    div bl                          ; AL = row
    cmp al, [ty_nfam]
    jae .out
    call fv_choose
.out:
    pop dx
    pop bx
    pop ax
    ret

; in: AL = valid family index, SI = window; gfx lock held
fv_choose:
    cmp al, [fv_selected]
    je .out
    mov [fv_selected], al
    mov word [fv_pending], 1        ; [fv_pending] = 1 and [fv_error] = 0
    call fv_redraw                  ; marker + "Loading...", no disk work
    mov bx, si
    call OSAPI_WM_WAKE
.out:
    ret

; -----------------------------------------------------------------------------
; Painting. Every coordinate below is RELATIVE to the content origin, which
; fv_origin banks in [fv_x]/[fv_y]; fv_abs and fv_sysline add it at the call.
; -----------------------------------------------------------------------------
fv_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call fv_origin
    call fv_draw
    cmp byte [fv_abon], 0           ; ...and the About card LAST, over the lot
    je .noab                        ; (SPEC.md 20.5.1.1): the kernel's region
    mov bx, si                      ; is armed here, so it is the _d entry and
    mov si, fv_ablines              ; NOT os88ui_about, which would re-arm and
    call os88ui_about_d             ; throw this paint's damage rect away
.noab:
    pop si
    jmp fv_pop4

; Full self-repaint.  Caller holds the gfx lock.
fv_redraw:
    push ax
    push bx
    push cx
    push dx
    mov bx, [fv_win]
    call fv_origin
    xor ax, ax
    call fv_wipe
    call fv_draw
    jmp fv_pop4

; Erase and repaint only the pane touched by an ordinary keystroke.
fv_redraw_right:
    push ax
    push bx
    push cx
    push dx
    mov ax, FV_DIVX + 1
    call fv_wipe
    call fv_draw_right
fv_pop4:                            ; the shared epilogue: entered by a near
    pop dx                          ; jmp and never called, so it costs no
    pop cx                          ; stack
    pop bx
    pop ax
    ret

; One opaque line in the always-readable system face, at CX/DX relative.
fv_sysline:
    push ax
    push bx
    push cx
    push dx
    call fv_abs
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    jmp fv_pop4

; BX = our window; out AX/DX = its content origin, banked in [fv_x]/[fv_y].
fv_origin:
    call OSAPI_WM_CONTENT
    mov [fv_x], ax
    mov [fv_y], dx
    ret

; AX/CX += [fv_x], BX/DX += [fv_y]: a relative rect made a screen one.
fv_abs:
    add ax, [fv_x]
    add cx, [fv_x]
    add bx, [fv_y]
    add dx, [fv_y]
    ret

; White from relative x = AX to the content's right and bottom edges.
; Clobbers AX-DX.
fv_wipe:
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    xor bx, bx
    mov cx, FV_CW - 1
    mov dx, FV_CH - 1
    call fv_abs
    call OSAPI_GFX_FILL
    ret

; Both panes and the divider. Clobbers AX-DX.
fv_draw:
    call fv_draw_list
    call fv_draw_right
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, FV_DIVX
    mov bx, 3
    mov dx, FV_CH - 4
    call fv_abs
    call OSAPI_GFX_VLINE
    ret

fv_draw_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, FV_LISTX                ; one column: fv_sysline keeps CX
    mov dx, 4
    mov si, fv_s_fonts
    call fv_sysline
    cmp byte [ty_nfam], 0
    jne .rows
    mov dx, 4 + 13
    mov si, fv_s_none
    call fv_sysline
.rows:
    mov dx, FV_LISTY
    xor bx, bx
.row:
    cmp bl, [ty_nfam]
    jae .hint
    mov word [fv_line], '  '
    cmp bl, [fv_selected]
    jne .mark
    mov byte [fv_line], '>'
.mark:
    mov al, bl
    call ty_famname
    mov di, fv_line + 2
    call fv_copy
    mov si, fv_line
    call fv_sysline
    add dx, FV_ROWH
    inc bx
    jmp short .row
.hint:
    mov dx, FV_CH - 12
    mov si, fv_s_pick
    call fv_sysline
    jmp fv_pop6

fv_draw_right:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, FV_RIGHTX
    mov dx, 4
    mov si, fv_s_spec
    call fv_sysline

    mov di, fv_line
    mov si, fv_s_builtin
    cmp byte [ty_nfam], 0
    je .name
    mov al, [fv_selected]
    call ty_famname
.name:
    call fv_copy
    cmp byte [fv_pending], 0
    je .error
    mov si, fv_s_loading
    call fv_copy
.error:
    cmp byte [fv_error], 0
    je .named
    mov si, fv_s_error
    call fv_copy
.named:
    mov dx, 16                      ; CX is still FV_RIGHTX
    mov si, fv_line
    call fv_sysline

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, FV_RIGHTX - 3
    mov bx, FV_BOXY
    mov cx, FV_CW - 5
    mov dx, FV_BOXBOT
    call fv_abs
    call OSAPI_GFX_FRAME

    call fv_draw_sample
    mov cx, FV_RIGHTX
    mov dx, FV_CH - 12
    mov si, fv_s_type
    call fv_sysline
fv_pop6:                            ; ...and its six-register entry
    pop di
    pop si
    jmp fv_pop4

; Wrap the mutable specimen by the current face's advances and emit one band
; per row.  ES:SI is kept in our segment for all type-library calls. DI is
; the row's RELATIVE top, and DX the face's cell height for the whole walk:
; ty_band, ty_putn and ty_flush all preserve it.
fv_draw_sample:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov si, fv_text
    xor cx, cx
    mov cl, [fv_textlen]
    mov di, FV_TEXTY
.line:
    jcxz .out
    mov ax, FV_SAMPLEW
    push cx
    call ty_fit                    ; CX = chars, AX = their pixel width
    mov bp, cx
    pop cx
    or bp, bp
    jz .out
    call ty_getrows
    xor ah, ah
    mov dx, ax                      ; DX = rows, for ty_band and ty_flush
    add ax, di
    cmp ax, FV_BOXBOT - 3
    ja .out
    call ty_band
    push cx
    mov cx, bp
    xor ax, ax
    call ty_putn
    mov ax, [fv_x]
    add ax, FV_TEXTX
    mov bx, [fv_y]
    add bx, di
    mov cx, FV_SAMPLEW
    clc                             ; THE CARRY IS AN ARGUMENT (SPEC.md 6.5.4):
    call ty_flush                   ; a face SAMPLE is never a greyed control
    pop cx
    add si, bp
    sub cx, bp
    add di, dx
    mov al, [ty_lead]               ; 0..8 (SPEC.md 6.4), so CBW is exact
    cbw
    add di, ax
    jmp short .line
.out:
    pop es
    pop bp
    jmp fv_pop6

; SI -> NUL source, DI -> destination. Copies the NUL too and leaves DI ON it,
; so the next copy appends and the last one has terminated the line.
; Clobbers AL.
fv_copy:
    lodsb
    mov [di], al
    or al, al
    jz .out
    inc di
    jmp short fv_copy
.out:
    ret

;  SPEC.md 6.5.5: the band is FV_SAMPLEW = 480 pixels wide and no wider -
; ty_fit bounds every line to it - so 60 bytes, one for the last glyph's
; spill, even: 62, and 480 bytes of bss back. Only ty_fit, ty_putn,
; ty_getrows and the scan/open/use/cache/band/flush core are reached.
%define TY_STRIDE 62
%define OS88TY_NOHIT
%define OS88TY_NOWIDTH
%define OS88TY_NOWIDTHN
%define OS88TY_NOPUT
%define OS88TY_NOADVOF
%define OS88TY_NOFAMN
%define OS88TY_NOGETASC
%define OS88TY_NOGETLEAD
%include "os88type.inc"

fv_tpl:
    dw 10, 22, FV_W, FV_H
    dw fv_title, fv_paint, fv_onkey, fv_onclick

fv_title:      db 'Font Viewer', 0
fv_s_fonts:    db 'FONTS', 0
fv_s_none:     db 'No .F88 files', 0
fv_s_pick:     db 'Click/arrows', 0
fv_s_spec:     db 'SPECIMEN', 0
fv_s_builtin:  db 'Built-in', 0
fv_s_loading:  db '  Loading...', 0
fv_s_error:    db '  Open failed', 0
fv_s_type:     db 'Type to edit; Backspace deletes', 0

; --- the About card's lines (SPEC.md 20.5.1.1, 90.9) -------------------------
; FIVE lines. The content box is FV_CW = 618 px = 77 cells on every adapter -
; this window is one size everywhere (FV_W/FV_H, and FV_H is the whole CGA
; band) - so the widest line here, 29 cells, is nowhere near the widget's
; clamp and nothing is split across two lines the way Mines and Hello had to.
fv_ablines:
    dw fv_ab1, fv_ab2, fv_ab3, fv_ab4, fv_ab5, 0
fv_ab1:     db 'Font Viewer for os8088', 0
fv_ab2:     db 'The system face browser', 0
fv_ab3:     db 0
fv_ab4:     db 'Contributed by Jorge Gonzalez', 0
fv_ab5:     db 'Any key or click closes', 0

; --- the shared controls (SPEC.md 20.5.1) ------------------------------------
; AT THE END OF THE CODE, which is os88ui.inc's own rule: the header and the
; OS88_ICON16 block are at fixed offsets in the image (SPEC.md 20.2) and code
; emitted between them fails the icon macro's offset assertion.
;
; NOBTN because this card draws no button and no scroll bar - the card IS the
; only control this package takes, and without the opt-out every package
; carrying the include pays 116 bytes for a glyph nothing calls.
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"

; --- state whose first value is not zero, and the specimen LAST --------------
; Four bytes the entry proc used to store, fifteen bytes of code, are simply
; what the image arrives with: every launch reads its own copy (SPEC.md 20.2).
; [fv_pending]/[fv_error] are adjacent because fv_choose sets both in one word.
fv_loaded:   db 0xFF                ; family row actually open (none yet)
fv_pending:  db 1                   ; the first face load is owed
fv_error:    db 0
fv_textlen:  db FV_INITLEN

; THE SPECIMEN IS THE LAST BYTE OF THE IMAGE, and its zero tail is not in it:
; the buffer runs on into the first FV_TAIL bytes of the bss, which the loader
; zeroes (SPEC.md 20.2), so the 61 zeros it used to ship are in RAM exactly as
; before and on no disk.
fv_text:
    db 'The quick brown fox jumps over the lazy dog. ABC abc 0123456789 !?'
FV_INITLEN equ $ - fv_text
FV_TAIL    equ FV_TEXTMAX + 1 - FV_INITLEN

; The bss, after the specimen's tail. FV_PAD puts [fv_win] on an EVEN offset,
; and FV_BSS_VARS being even puts ty_bandbuf on one with it: the compose loops
; write the band a word at a time and [fv_x]/[fv_y] are read by every drawing
; call, and an odd word costs an 8086 or a 286 a second bus cycle.
FV_BSS_VARS equ 2 + 2 + 2 + 1 + 1 + 1 + 1 + 24    ; ...the 1 is spare: even
FV_PAD      equ (os88_image_end - $$ + FV_TAIL) & 1
%if FV_BSS_VARS & 1
    %error "FV_BSS_VARS must stay even, or ty_bandbuf goes odd"
%endif
FV_BSS_OWN  equ FV_TAIL + FV_BSS_VARS + FV_PAD
    OS88_BSS FV_BSS_OWN + TY_BSS_SIZE
    OS88_IMAGE_END

%if os88_image_end - fv_text != FV_INITLEN
    %error "fv_text must be the last thing in the image: its tail is the bss"
%endif

fv_win       equ os88_image_end + FV_TAIL + FV_PAD     ; word
fv_x         equ fv_win + 2         ; content origin
fv_y         equ fv_win + 4
fv_selected  equ fv_win + 6         ; family row requested
fv_face      equ fv_win + 7         ; ty_* handle (0 = built-in)
fv_abon      equ fv_win + 8         ; the About card is up (SPEC.md 90.9)
fv_line      equ fv_win + 10        ; 24-byte composed label
; THE LAUNCH NAME SHARES fv_line. It is read by fv_entry alone, before the
; window exists and so before anything can be lettered, and is dead from the
; first paint on: TY_NAMSZ bytes of every instance's bss for a value nothing
; reads after its first millisecond.
fv_arg       equ fv_line
%if TY_NAMSZ > 24
    %error "fv_arg no longer fits in fv_line"
%endif

    TY_BSS os88_image_end + FV_BSS_OWN
