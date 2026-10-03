; =============================================================================
; os8088 - apps/pixel/pixel.asm
;
; PiXEL - an image viewer and editor (SPEC.md 106; the design record is
; docs/plans/PIXEL-PLAN.md). This is the RESIDENT package: the window, its
; layout tiers, the toolbar, the tool column, the panels, the filmstrip and
; the status bar (apps/pixel/pxui.inc), the menus and About, and the far-call
; boundary into PiXEL's lazy code PARTS (SPEC.md 106.5, apps/pixel/pxpart.inc).
;
; WAVE 1 OF THE PLAN IS WHAT IS HERE, AND NOTHING PRETENDS OTHERWISE. A
; picture is not decoded yet, so every command that needs one is greyed for
; that fact (SPEC.md 47) and the status bar says "No picture". File > Open
; already runs the Standard File dialog, and what it can tell without a
; decoder it tells: the name, the folder, the size, the format by its first
; bytes, the dimensions where the header states them, and how many pictures
; share the folder.
;
; THE ONE PART IN THIS FILE IS THE KEYBOARD CARD (apps/pixel/pxhelp.asm),
; and it is a part on purpose: it is the gate the decoders will stand on.
; View > Keyboard Help fetches it, far-calls two of its vectors and drops it
; again, and tests/pxparts.py drives exactly that.
; =============================================================================

%include "os88api.inc"
%include "pxpart.inc"

    OS88_HEADER 'PiXEL', px_entry, OS88_F_ICON | OS88_F_ASSOC | OS88_F_GLYPH | OS88_F_PARTS

    OS88_ICON16
%include "pxappico.inc"
    OS88_ICON16_END

    ; THE ASSOCIATIONS (SPEC.md 54.6; the plan's decision 9). Five is the
    ; header's maximum. BMP and GIF are NOT here: they are Paint's built-in
    ; rows (kernel/assoc.inc), and taking them would make "what opens a .BMP"
    ; depend on harvest order. PiXEL still opens both from File > Open.
    OS88_ASSOC16
    db 5
    OS88_ASSOC_EXT 'JPG'
    OS88_ASSOC_EXT 'PNG'
    OS88_ASSOC_EXT 'PCX'
    OS88_ASSOC_EXT 'TIF'
    OS88_ASSOC_EXT 'PIX'
    OS88_ASSOC16_END

    OS88_DOCGLYPH8
    PXI_DOCGLYPH
    OS88_DOCGLYPH8_END

%include "os88parts.inc"

; --- the parts (SPEC.md 106.5) --------------------------------------------------
PXPART_HELP equ 0                   ; the keyboard card, apps/pixel/pxhelp.asm
PX_NPARTS   equ 1

; --- the buttons: ONE record for the window (os88ui_btnclick finds a window's
; record by walking the package's list, so every control lives in it) ---------
PX_B_OPEN   equ 0                   ; the toolbar, in the toolbar's order
PX_B_SAVE   equ 1
PX_B_PREV   equ 2
PX_B_NEXT   equ 3
PX_B_ZIN    equ 4
PX_B_ZOUT   equ 5
PX_B_FIT    equ 6
PX_B_ONE    equ 7
PX_B_ROT    equ 8
PX_B_SHOW   equ 9
PX_B_T0     equ 10                  ; the tool column
PX_NTOOL    equ 6
PX_B_NZIN   equ 16                  ; the Navigator's +, - and Fit
PX_B_NZOUT  equ 17
PX_B_NFIT   equ 18
PX_B_PB0    equ 19                  ; each panel's box, three
PX_B_FSBOX  equ 22                  ; the filmstrip's box...
PX_B_FSL    equ 23                  ; ...and its two arrows
PX_B_FSR    equ 24
PX_B_SPREV  equ 25                  ; the status bar's two
PX_B_SNEXT  equ 26
PX_NB       equ 27

PX_TBN      equ 13                  ; toolbar items, separators included
PX_NSF      equ 8                   ; status fields
PX_SF_NAME  equ 0
PX_SF_DIMS  equ 1
PX_SF_FMT   equ 2
PX_SF_ZOOM  equ 3
PX_SF_COLS  equ 4
PX_SF_BYTES equ 5
PX_SF_MEM   equ 6
PX_SF_POS   equ 7
PX_SVSZ     equ 16                  ; a field's value buffer
PX_LINEMAX  equ 92                  ; widest run: 720 / 8 and a margin
PX_ILAB     equ 7                   ; Image Info's label column, in glyphs
PX_IVSZ     equ 24                  ; ...and a value's buffer
PX_HSTW     equ 9                   ; Histogram's statistics column, glyphs
PXH_SZ      equ 2 + 8 * 4           ; a half-height picture record
PX_HELPSZ   equ 400                 ; the keyboard card's text, copied in
PX_HELPMAX  equ 12                  ; ...and its lines
PX_MEMT     equ 91                  ; ticks between looks at free memory (5 s)

PX_R_TB     equ 1                   ; px_regdraw's regions
PX_R_TOOLS  equ 2
PX_R_CANVAS equ 4
PX_R_PANELS equ 8
PX_R_FS     equ 16
PX_R_STATUS equ 32
PX_R_SDIRTY equ 64                  ; only the status fields that changed
PX_R_ALL    equ 63

PX_PR_NONE   equ 0                  ; what a press that was no button's hit
PX_PR_CANVAS equ 1
PX_PR_STRIP  equ 2

; --- the formats PiXEL names (SPEC.md 106.6) -----------------------------------
PXF_NONE    equ 0
PXF_JPEG    equ 1
PXF_PNG     equ 2
PXF_GIF     equ 3
PXF_BMP     equ 4
PXF_PCX     equ 5
PXF_TIFF    equ 6
PXF_TGA     equ 7
PXF_PIX     equ 8
PXF_PNM     equ 9
PXF_ICO     equ 10
PXF_LBM     equ 11
PXF_MAC     equ 12
PXF_PACKED  equ 13                  ; a 'CZ' wrapper (SPEC.md 20.14)
PXF_N       equ 14

; =============================================================================
; px_entry - the loader's call. SI = the kernel's name buffer, ES = KERNEL_SEG
; =============================================================================
px_entry:
    push es
    call op_load                    ; FIRST (os88parts.inc rule 1): it copies
    pop es                          ; our file's name, which every later fetch
    jnc .loaded                     ; reads the part out of
    jmp .fail
.loaded:
    call OSAPI_ARG_FILE             ; a document launch (SPEC.md 54.5): bank
    jc .nodoc                       ; it now, open it from the wake (54.10)
    mov di, px_argname
    mov cx, 12
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [px_argname + 12], 0
    mov [px_argdir], dx
    mov [px_argvol], bl
    mov byte [px_argpend], 1
.nodoc:
    call OSAPI_FILE_HERE            ; where we were launched from: the folder
    mov [px_homedir], dx            ; PIXEL.O88 is in, which every part fetch
    mov [px_homevol], bl            ; goes back to (px_pfetch)
    call px_halfinit
    call px_initstate
    mov si, px_tpl
    call OSAPI_WM_CREATE
    jnc .made
    jmp .fail
.made:
    mov [px_win], bx
    OS88_REGION_MOVABLE             ; SPEC.md 66.6.1: our region may move
    mov al, 1
    call OSAPI_WM_SIZABLE           ; every paint lays out from the live box
    mov al, 1
    call OSAPI_WM_OWNBG             ; we paint every pixel (SPEC.md 11.90.1)
    mov al, 1
    call OSAPI_WM_SNAP              ; content on a byte boundary (11.94): the
                                    ; 8-pixel grid every run is aligned to
    mov cx, PX_MINW
    mov dx, PX_MINH
    call OSAPI_WM_MINSIZE
    mov si, px_pref
    call OSAPI_WM_PREFER
    mov si, px_menus
    call OSAPI_MENU_SET
    mov si, px_about
    call OSAPI_ABOUT_SET
    push bx
    mov ax, bx                      ; THE GESTURE (SPEC.md 20.5.1.3): press
    mov bx, px_btns                 ; draws down, release fires, a slide off
    mov si, px_onup                 ; cancels
    mov di, px_ondrag
    mov dx, px_onclick              ; ...a press on no button: ours
    call os88ui_btninit
    pop bx
    mov ax, px_clickw               ; ...and the press reaches US first, so
    call OSAPI_WM_ONCLICK           ; the rects are where the window is NOW
    mov ax, px_onwake
    call OSAPI_WM_ONWAKE
    mov ax, px_onresize
    call OSAPI_WM_ONRESIZE
    mov ax, px_ontimer
    call OSAPI_WM_ONTIMER
    mov ax, PX_MEMT
    call OSAPI_WM_TIMER             ; the status bar's free memory
    call px_memfield
    mov bx, [px_win]                ; the loader publishes the window we
    clc                             ; return in BX
    ret
.fail:
    stc
    ret

; px_initstate - the state a fresh window starts in. Preserves all
px_initstate:
    push ax
    push bx
    push si
    mov byte [px_tool], 0           ; the Hand
    mov byte [px_c_rule], CBLACK    ; until the first layout picks a palette
    xor bx, bx
.sv:
    mov si, px_s_dash               ; every field "-" until it knows better
    call px_sval
    inc bx
    cmp bx, PX_NSF
    jb .sv
    mov bx, PX_SF_NAME
    mov si, px_s_nopic
    call px_sval
    xor bx, bx                      ; Image Info: "-" for every value
.iv:
    mov si, px_s_dash
    call px_ivset
    inc bx
    cmp bx, PX_INFON
    jb .iv
    mov si, px_s_fs0
    mov di, px_fstitle
    call px_strcpy
    mov si, px_s_fsnone
    mov di, px_fsline
    call px_strcpy
    call px_flags
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; THE CALLBACKS
; =============================================================================

; --- W_PAINT: SI = the window ----------------------------------------------------
; Lays out from the live box, asks for the damage (SPEC.md 11.90.2) and draws
; the regions it touches, each whole and every pixel once; then a card that is
; up goes on top of them. With OWNBG every pixel of the content is ours.
px_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [px_win], si
    call px_layout
    jc .out
    mov bx, [px_win]
    call OSAPI_WM_DAMAGE            ; CF = 1: all of it; else AX..DX
    jnc .rect
    mov ax, [px_cx0]
    mov bx, [px_cy0]
    mov cx, [px_xr]
    mov dx, [px_yb]
.rect:
    mov [px_dx1], ax
    mov [px_dy1], bx
    mov [px_dx2], cx
    mov [px_dy2], dx
    mov al, PX_R_ALL
    call px_draw                    ; every region the damage touches
    call px_cards
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_draw - AL = a mask of PX_R_* regions: draw each one in it that meets the
; damage rect [px_dx1..px_dy2]. Lock held and clip armed by the caller
px_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [px_rmask], al
    test byte [px_rmask], PX_R_TB
    jz .t
    mov ax, [px_cx0]
    mov bx, [px_cy0]
    mov cx, [px_xr]
    mov dx, [px_tby2]
    call px_meets
    jc .t
    call px_draw_tb
.t:
    test byte [px_rmask], PX_R_TOOLS
    jz .c
    mov ax, [px_cx0]
    mov bx, [px_midy1]
    mov cx, [px_tcx2]
    mov dx, [px_midy2]
    call px_meets
    jc .c
    call px_draw_tools
.c:
    test byte [px_rmask], PX_R_CANVAS
    jz .p
    mov ax, [px_cvx1]
    mov bx, [px_midy1]
    mov cx, [px_cvx2]
    mov dx, [px_midy2]
    call px_meets
    jc .p
    call px_draw_canvas
.p:
    test byte [px_rmask], PX_R_PANELS
    jz .f
    cmp byte [px_pnon], 0
    je .f
    mov ax, [px_pnx1]
    mov bx, [px_midy1]
    mov cx, [px_xr]
    mov dx, [px_midy2]
    call px_meets
    jc .f
    call px_draw_panels
.f:
    test byte [px_rmask], PX_R_FS
    jz .s
    cmp byte [px_fson], 0
    je .s
    mov ax, [px_cx0]
    mov bx, [px_fsy1]
    mov cx, [px_xr]
    mov dx, [px_sty1]
    dec dx
    call px_meets
    jc .s
    call px_draw_fs
.s:
    test byte [px_rmask], PX_R_STATUS
    jz .sd
    mov ax, [px_cx0]
    mov bx, [px_sty1]
    mov cx, [px_xr]
    mov dx, [px_yb]
    call px_meets
    jc .sd
    call px_draw_status
    mov byte [px_sdirty], 0         ; the fields on the glass are the values
    jmp short .out
.sd:
    cmp byte [px_sdirty], 0         ; a field that changed while this window
    je .out                         ; could not show it is drawn by whatever
    call px_sflush                  ; draws next, whatever it was asked for
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_meets - does AX/BX/CX/DX meet the damage rect? CF = 0 yes. Preserves all
px_meets:
    cmp cx, [px_dx1]
    jl .no
    cmp ax, [px_dx2]
    jg .no
    cmp dx, [px_dy1]
    jl .no
    cmp bx, [px_dy2]
    jg .no
    clc
    ret
.no:
    stc
    ret

; px_cards - the About or keyboard card, drawn LAST and over everything, from
; the painter (os88ui_about_d: the region is already armed). Preserves all
px_cards:
    push bx
    push si
    mov bx, [px_win]
    cmp byte [px_abon], 0
    je .h
    mov si, px_ablines
    call os88ui_about_d
.h:
    cmp byte [px_helpon], 0
    je .out
    mov si, px_helptab
    call os88ui_about_d
.out:
    pop si
    pop bx
    ret

; px_regdraw - AL = a mask of PX_R_* regions: draw them NOW, from a callback
; (the lock is held there and nothing has armed a clip). Lays out first,
; because the window may have moved since it was painted. A card that is up
; is left alone - taking it down repaints everything anyway. Preserves all
px_regdraw:
    push ax
    push bx
    cmp byte [px_abon], 0
    jne .out
    cmp byte [px_helpon], 0
    jne .out
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET          ; CF = 1: not a pixel of us is visible
    jc .out
    call px_layout
    jc .clr
    push ax
    mov ax, [px_cx0]                ; "damage" = the whole content
    mov [px_dx1], ax
    mov ax, [px_cy0]
    mov [px_dy1], ax
    mov ax, [px_xr]
    mov [px_dx2], ax
    mov ax, [px_yb]
    mov [px_dy2], ax
    pop ax
    call px_draw
    ; A SELF-INITIATED REPAINT OF THE CORNER ENDS WITH THE GROW BOX (SPEC.md
    ; 11.1.1). The box is 13 rows: inside a 14-row status bar, so only the bar
    ; reaches it on the full layout, but two rows above an 11-row one on the
    ; CGA, where the regions over the bar reach it too. A status FIELD never
    ; does - the bar's text stops PX_GROW short - and the five-second memory
    ; look must not flash the box
    test al, PX_R_STATUS
    jnz .grow
    cmp byte [px_half], 0
    je .clr
    test al, PX_R_PANELS | PX_R_FS | PX_R_CANVAS
    jz .clr
.grow:
    mov bx, [px_win]
    call OSAPI_WM_GROW
.clr:
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop bx
    pop ax
    ret

; px_regbtn - SI = a button: redraw it alone, now, from a callback
px_regbtn:
    push bx
    cmp byte [px_abon], 0
    jne .out
    cmp byte [px_helpon], 0
    jne .out
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call px_layout
    jc .clr
    call px_btn
.clr:
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop bx
    ret

; --- W_ONCLICK, ours, in front of the library's -------------------------------
; A card that is up is taken down by the press, and the press does nothing
; else. Otherwise the rects are laid out where the window is NOW (a move does
; not call W_PAINT) and the library sees the press first (SPEC.md 20.5.1.3.3)
px_clickw:
    mov [px_win], si
    call px_carddown
    jc .out
    call px_layout
    jc .out
    jmp os88ui_btnclick
.out:
    ret

; px_carddown - if a card is up, take it down and repaint. CF = 1 it was
px_carddown:
    cmp byte [px_abon], 0
    jne .down
    cmp byte [px_helpon], 0
    jne .down
    clc
    ret
.down:
    mov byte [px_abon], 0
    mov byte [px_helpon], 0
    push ax
    mov al, PX_R_ALL
    call px_regdraw
    pop ax
    stc
    ret

; a press on no button (the library hands it on): CX, DX screen, SI window
px_onclick:
    mov byte [px_press], PX_PR_NONE
    push ax
    push bx
    mov ax, PX_PR_CANVAS
    cmp cx, [px_cvx1]
    jl .strip
    cmp cx, [px_cvx2]
    jg .strip
    cmp dx, [px_midy1]
    jl .strip
    cmp dx, [px_midy2]
    jg .strip
    jmp short .got
.strip:
    mov ax, PX_PR_STRIP             ; a panel strip on the compact layout:
    cmp byte [px_tier], 0           ; the strip itself turns the page too
    je .out
    cmp byte [px_pnon], 0
    je .out
    cmp cx, [px_pnx1]
    jle .out
    mov bx, [px_pcurw]
    shl bx, 1
    cmp dx, [px_pyw + bx]
    jl .out
    push ax
    mov ax, [px_pyw + bx]
    add ax, [px_sh]
    cmp dx, ax
    pop ax
    jge .out
.got:
    mov [px_press], al
.out:
    pop bx
    pop ax
    ret

; W_ONDRAG: the pressed button follows the pointer
px_ondrag:
    push ax
    push bx
    call os88ui_armed
    or ax, ax
    jz .out
    call px_layout
    jc .out
    mov bx, px_btns
    call os88ui_btndrag
.out:
    pop bx
    pop ax
    ret

; W_ONMOUSEUP: a button fires here, or the region pressed acts
px_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    call px_layout
    jc .out
    call os88ui_armed
    or ax, ax
    jz .region
    mov bx, px_btns
    call os88ui_btnup               ; AX = the button, index + 1, or 0
    or ax, ax
    jz .out
    dec ax
    call px_bfire
    jmp short .out
.region:
    mov al, [px_press]
    mov byte [px_press], PX_PR_NONE
    cmp al, PX_PR_CANVAS
    jne .st
    cmp cx, [px_cvx1]               ; released where it was pressed: the
    jl .out                         ; empty canvas is a big Open button
    cmp cx, [px_cvx2]
    jg .out
    cmp dx, [px_midy1]
    jl .out
    cmp dx, [px_midy2]
    jg .out
    call px_cmd_open
    jmp short .out
.st:
    cmp al, PX_PR_STRIP
    jne .out
    call px_nextpanel
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_bfire - AX = a button that fired. A greyed one did NOT: the library arms
; and fires on geometry alone, so the fact a grey states is checked here
px_bfire:
    push ax
    push bx
    push si
    mov si, ax
    shl si, 1
    test word [px_bflags + si], OS88UI_DIS
    jnz .out
    cmp ax, PX_B_OPEN
    jne .tool
    call px_cmd_open
    jmp short .out
.tool:
    cmp ax, PX_B_T0
    jb .out
    cmp ax, PX_B_T0 + PX_NTOOL
    jae .box
    sub al, PX_B_T0
    call px_settool
    jmp short .out
.box:
    cmp ax, PX_B_PB0
    jb .out
    cmp ax, PX_B_PB0 + 3
    jae .fsbox
    sub al, PX_B_PB0
    call px_panelbox
    jmp short .out
.fsbox:
    cmp ax, PX_B_FSBOX
    jne .out
    xor byte [px_fscol], 1
    call px_flags
    mov al, PX_R_ALL                ; the canvas grows or shrinks with it
    call px_regdraw
.out:
    pop si
    pop bx
    pop ax
    ret

; --- W_ONKEY: AL = ascii, AH = scan, SI = the window ---------------------------
px_onkey:
    push ax
    push bx
    mov [px_win], si
    call px_carddown                ; any key takes a card down, and does
    jc .out                         ; nothing else
    cmp al, 0x0F                    ; Ctrl+O
    jne .k1
    call px_cmd_open
    jmp short .out
.k1:
    cmp ah, 0x3B                    ; F1
    je .help
    cmp al, '?'
    jne .k2
.help:
    call px_help
    jmp short .out
.k2:
    cmp al, 9                       ; Tab: the next panel, one column
    jne .k3
    cmp byte [px_tier], 0
    je .out
    call px_nextpanel
    jmp short .out
.k3:
    and al, 0xDF                    ; the tools' letters, either case
    mov bx, px_toolkeys
.tk:
    cmp byte [bx], 0
    je .out
    cmp al, [bx]
    je .tset
    inc bx
    jmp short .tk
.tset:
    sub bx, px_toolkeys
    mov al, bl
    call px_settool
.out:
    pop bx
    pop ax
    ret

; --- the menu handler: AL = item, AH = menu, SI = the window --------------------
; A greyed item never arrives here. What does: File > Open, and View's four
; that need no picture.
px_cmd:
    push ax
    mov [px_win], si
    cmp ah, PX_M_FILE
    jne .view
    cmp al, 0
    jne .out
    call px_cmd_open
    jmp short .out
.view:
    cmp ah, PX_M_VIEW
    jne .out
    cmp al, PX_MV_DITHER
    jne .v1
    call px_dithertog
    jmp short .out
.v1:
    cmp al, PX_MV_PANELS
    jne .v2
    call px_paneltog
    jmp short .out
.v2:
    cmp al, PX_MV_FILM
    jne .v3
    call px_filmtog
    jmp short .out
.v3:
    cmp al, PX_MV_HELP
    jne .out
    call px_help
.out:
    pop ax
    ret

; --- the About handler (OSAPI_ABOUT_SET, SPEC.md 12.7) ----------------------------
px_about:
    push bx
    push si
    mov byte [px_helpon], 0
    mov byte [px_abon], 1
    mov bx, [px_win]
    mov si, px_ablines
    call os88ui_about               ; arms its own clip: CF = 1 not visible,
    pop si                          ; and the flag stands for the next paint
    pop bx
    ret

; --- W_ONRESIZE: the box changed under us. Nothing is kept across a paint, so
; there is nothing to re-derive; the full repaint that follows lays out anew
px_onresize:
    ret

; --- W_ONTIMER: free memory, every PX_MEMT ticks ------------------------------------
px_ontimer:
    push ax
    push bx
    mov [px_win], si
    call px_memfield
    cmp byte [px_sdirty], 0         ; the usual answer: nothing moved, and
    je .rearm                       ; nothing is drawn or even laid out
    mov al, PX_R_SDIRTY
    call px_regdraw
.rearm:
    mov bx, [px_win]
    mov ax, PX_MEMT
    call OSAPI_WM_TIMER
    pop bx
    pop ax
    ret

; --- W_ONWAKE: the launch document's handover (SPEC.md 54.10) ----------------
; No lock here: the disk work happens first, then the lock for the drawing.
px_onwake:
    push ax
    push bx
    push dx
    push si
    push di
    mov [px_win], si
    cmp byte [px_argpend], 0
    je .out
    mov byte [px_argpend], 0
    mov dx, [px_argdir]             ; the DOCUMENT's folder, not ours (54.8:
    mov bl, [px_argvol]             ; the kernel stood in the program's)
    call OSAPI_FILE_GOTO
    jc .out
    mov si, px_argname
    mov di, px_fname
    call px_strcpy
    call px_examine                 ; reads the disk: no lock yet
    call OSAPI_GFX_LOCK
    call px_shown
    call OSAPI_GFX_UNLOCK
.out:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; COMMANDS
; =============================================================================

; px_cmd_open - File > Open: the Standard File dialog (SPEC.md 38)
px_cmd_open:
    push ax
    push bx
    push si
    push di
    mov al, 0
    mov bx, [px_win]
    mov di, px_dlgdone
    xor si, si
    call OSAPI_FILE_DLG             ; CF = 1: one is up already
    pop di
    pop si
    pop bx
    pop ax
    ret

; px_dlgdone - the dialog's completion (SPEC.md 38.6): lock held, ES:DI = the
; chosen name in the KERNEL's segment and valid for this call only, DX:CX its
; size from the listing (0 = not a file there). The instance is standing in
; the chosen folder (38.10), which is the picture's folder from now on
px_dlgdone:
    push si
    mov si, di
    mov di, px_fname                ; the kernel's buffer, copied out FIRST
    mov cx, 12
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [px_fname + 12], 0
    pop si
    push ds
    pop es                          ; ES = ours again
    cmp byte [px_fname], 0
    je .none
    call px_examine
    jc .none
    call px_shown
    ret
.none:
    mov si, px_s_notfile
    call px_toast
    ret

; px_shown - after px_examine: the window says what it now knows. Lock held
px_shown:
    push ax
    push bx
    call px_flags
    mov bx, [px_win]                ; "PiXEL - NAME.EXT" in the title bar: a
    mov ax, px_title                ; strip, not a repaint (SPEC.md 11.92)
    call OSAPI_WM_TITLE
    mov al, PX_R_CANVAS | PX_R_PANELS | PX_R_FS | PX_R_SDIRTY
    call px_regdraw
    pop bx
    pop ax
    ret

; px_settool - AL = a tool: latch it, and redraw the two buttons that change
px_settool:
    push ax
    push si
    cmp al, PX_NTOOL
    jae .out
    cmp al, [px_tool]
    je .out
    mov ah, [px_tool]
    mov [px_tool], al
    call px_flags
    mov al, ah
    xor ah, ah
    add ax, PX_B_T0
    mov si, ax
    call px_regbtn                  ; the old one up
    mov al, [px_tool]
    xor ah, ah
    add ax, PX_B_T0
    mov si, ax
    call px_regbtn                  ; the new one down
.out:
    pop si
    pop ax
    ret

; px_panelbox - AL = a panel whose box fired: on the full layout it collapses
; or expands; on the compact one the column turns to the next panel
px_panelbox:
    push ax
    push bx
    cmp byte [px_tier], 0
    je .full
    call px_nextpanel
    jmp short .out
.full:
    xor ah, ah
    mov bx, ax
    mov al, [px_bit + bx]
    xor [px_pcol], al
    call px_flags
    mov al, PX_R_PANELS
    call px_regdraw
.out:
    pop bx
    pop ax
    ret

; px_nextpanel - the compact layout's one panel, turned to the next
px_nextpanel:
    push ax
    mov al, [px_pcur]
    inc al
    cmp al, 3
    jb .s
    xor al, al
.s:
    mov [px_pcur], al
    xor ah, ah
    mov [px_pcurw], ax
    call px_flags
    mov al, PX_R_PANELS
    call px_regdraw
    pop ax
    ret

; px_paneltog - View > Hide Panels / Show Panels
px_paneltog:
    push ax
    xor byte [px_pnoff], 1
    mov ax, px_mi_hidep
    cmp byte [px_pnoff], 0
    je .l
    mov ax, px_mi_showp
.l:
    mov [px_miv + 2 * PX_MV_PANELS], ax
    call px_menuset
    mov al, PX_R_ALL
    call px_regdraw
    pop ax
    ret

; px_filmtog - View > Hide Filmstrip / Show Filmstrip. The choice outlives a
; change of tier: it is the user's, where the tier's default is only a default
px_filmtog:
    push ax
    call px_layout
    mov al, 2                       ; shown now: off from now on
    cmp byte [px_fson], 0
    jne .set
    mov al, 1                       ; ...else on
.set:
    mov [px_fsuser], al
    mov byte [px_fscol], 0
    call px_flags
    mov al, PX_R_ALL
    call px_regdraw                 ; ...which lays out, and relabels
    pop ax
    ret

; px_filmlabel - the View menu's Filmstrip item says what choosing it would
; do to the filmstrip AS LAID OUT - which the tier decides as often as the
; user does, so every layout asks. The set is re-registered only when the
; word changes: OSAPI_MENU_SET draws nothing, but it is not free either
px_filmlabel:
    push ax
    mov ax, px_mi_hidef
    cmp byte [px_fson], 0
    jne .l
    mov ax, px_mi_showf
.l:
    cmp ax, [px_miv + 2 * PX_MV_FILM]
    je .out
    mov [px_miv + 2 * PX_MV_FILM], ax
    call px_menuset
.out:
    pop ax
    ret

; px_dithertog - View > Dither: the windowed view's choice (SPEC.md 106.2).
; A setting, so it is live before there is a picture to apply it to
px_dithertog:
    push ax
    xor byte [px_dither], 1
    mov ax, px_mi_dord
    cmp byte [px_dither], 0
    je .l
    mov ax, px_mi_ddif
.l:
    mov [px_miv + 2 * PX_MV_DITHER], ax
    call px_menuset
    pop ax
    ret

; px_menuset - re-register the set, which is what a relabel needs to show
px_menuset:
    push bx
    push si
    mov bx, [px_win]
    mov si, px_menus
    call OSAPI_MENU_SET
    pop si
    pop bx
    ret

; px_toast - SI = a line of ours (<= 24 glyphs, SPEC.md 59.10)
px_toast:
    push cx
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop cx
    ret

; =============================================================================
; THE KEYBOARD CARD, through the part boundary (SPEC.md 106.5)
; =============================================================================

; px_help - fetch the card's part, prove it (INIT), copy its lines (INFO),
; drop it, and put the card up. The part holds a claim only for this call
px_help:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov byte [px_abon], 0
    mov al, PXPART_HELP
    mov bl, PXV_INIT
    call px_pcall                   ; AX = PXP_PROBE
    jc .fail
    mov [px_pres], ax
    push ds
    pop es
    mov di, px_helpbuf
    mov cx, PX_HELPSZ
    mov al, PXPART_HELP
    mov bl, PXV_INFO
    call px_pcall                   ; AX = the lines
    jc .fail
    cmp ax, PX_HELPMAX
    jbe .n
    mov ax, PX_HELPMAX
.n:
    mov cx, ax                      ; the card's line table, out of the text
    mov si, px_helpbuf
    mov di, px_helptab
    jcxz .tend
.t:
    mov [di], si
    add di, 2
.z:
    lodsb
    or al, al
    jnz .z
    loop .t
.tend:
    mov word [di], 0
    mov al, PXPART_HELP
    call px_pdrop                   ; ...and the claim goes back now
    mov byte [px_helpon], 1
    mov bx, [px_win]
    mov si, px_helptab
    call os88ui_about
    jmp short .out
.fail:
    mov al, PXPART_HELP
    call px_pdrop
    cmp ax, PXE_PART                ; op_fetch has said why when it failed;
    je .out                         ; a part that answered wrongly has not
    mov si, px_s_badpart
    call px_toast
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; px_pcall - far-call vector BL of part AL, fetching the part first if it is
; not here (SPEC.md 106.5). UI task only: a fetch reads the disk.
; in:  AL = the part, BL = the vector (PXV_*); CX, DI, ES and anything else
;      the vector takes, untouched on the way in
; out: the vector's CF/AX - or CF = 1 with AX = PXE_PART when the part could
;      not be fetched (op_fetch has said why), PXE_BADPART when what was
;      fetched does not carry this ABI. BX, DX and SI preserved
px_pcall:
    push bx
    push dx
    push si
    mov [px_ppart], al
    mov [px_pvec], bl
    push cx
    push di
    push es
    call px_pfetch                  ; clobbers what op_fetch clobbers
    pop es
    pop di
    pop cx
    jc .nopart
    push es
    mov al, [px_ppart]
    call op_seg
    or ax, ax
    jz .badp
    mov es, ax                      ; check the header before trusting it
    cmp word [es:PXP_SIG], PXP_SIGVAL
    jne .badp
    cmp byte [es:PXP_ABI], PXP_ABIVER
    jne .badp
    mov bl, [px_pvec]
    cmp bl, [es:PXP_NVEC]
    jae .badp
    mov dx, cs                      ; rule 2: the package's segment, stamped
    mov [es:PXP_PKG], dx            ; before EVERY call
    xor bh, bh
    shl bx, 1
    mov dx, [es:PXP_VEC + bx]
    mov [px_pfar], dx
    mov [px_pfar + 2], es
    pop es
    inc word [px_pcalls]
    call far [px_pfar]              ; DS = ours, CS = the part
    jmp short .out
.badp:
    pop es
    mov ax, PXE_BADPART
    stc
    jmp short .out
.nopart:
    mov ax, PXE_PART
    stc
.out:
    pop si
    pop dx
    pop bx
    ret

; px_pfetch - [px_ppart] here, or CF = 1 (op_fetch has toasted why). A part
; is read out of PIXEL.O88, and the instance may be standing anywhere since
; File > Open moved it (SPEC.md 38.10) - so the fetch goes home to the folder
; the package was launched from and back again (SCRIBE's bracket, 95.8.6).
; clobbers: AX, BX, CX, DX, SI, DI, ES
px_pfetch:
    mov al, [px_ppart]
    call op_seg
    or ax, ax
    jz .fetch
    clc
    ret
.fetch:
    mov byte [px_pmoved], 0
    call OSAPI_FILE_HERE            ; DX = where the user is, BL = its drive
    mov [px_pwas], dx
    mov [px_pwvol], bl
    cmp dx, [px_homedir]
    jne .go
    cmp bl, [px_homevol]
    je .here
.go:
    mov dx, [px_homedir]
    mov bl, [px_homevol]
    call OSAPI_FILE_GOTO
    mov byte [px_pmoved], 1
.here:
    mov al, [px_ppart]
    call op_fetch                   ; claims, reads, expands (SPEC.md 20.12.4)
    pushf
    cmp byte [px_pmoved], 0
    je .back
    mov dx, [px_pwas]
    mov bl, [px_pwvol]
    call OSAPI_FILE_GOTO
.back:
    popf
    ret

; px_pdrop - AL = a part: give its claim back. Preserves all
px_pdrop:
    call op_drop
    ret

; =============================================================================
; WHAT A FILE IS, WITHOUT DECODING IT (SPEC.md 106.6)
; =============================================================================

; px_examine - [px_fname], in the folder the instance is standing in: walk
; the folder (its pictures, where this one sorts among them, its size), read
; its first cluster for the format and the header's dimensions, and compose
; everything the window shows about it. CF = 1 it is not a file here
px_examine:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov word [px_fsize], 0
    mov word [px_fsize + 2], 0
    mov byte [px_ffmt], PXF_NONE
    mov word [px_fw], 0
    mov word [px_fh], 0
    mov byte [px_fbits], 0
    mov byte [px_fprog], 0
    call px_walk                    ; CF = 1: the name is not in this folder
    jnc .found
    jmp .out
.found:
    mov si, px_fname                ; the format by its extension first...
    call px_extfmt
    mov [px_ffmt], al
    call px_sniff                   ; ...then by its bytes, which win
    mov byte [px_fhave], 1
    call px_compose
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_walk - OSAPI_FILE_FIND over the folder: [px_fcount] pictures PiXEL
; reads, [px_fidx] this one's place among them sorted by name (1-based), and
; [px_fsize]. CF = 1 when [px_fname] is not one of the files listed
px_walk:
    mov word [px_fcount], 0
    mov word [px_fidx], 1
    mov byte [px_ffound], 0
    xor cx, cx
.next:
    mov di, px_find
    call OSAPI_FILE_FIND            ; CX = the next ordinal
    jc .end
    cmp byte [px_find + 14], OSAPI_FT_DIR
    jae .next                       ; a folder, or '..'
    mov si, px_find
    call px_extfmt
    or al, al
    jz .next                        ; not a picture PiXEL names
    inc word [px_fcount]
    mov si, px_find
    mov di, px_fname
    call px_strcmp                  ; CF = 1: listed < ours; ZF = equal
    je .ours
    jnc .next
    inc word [px_fidx]
    jmp short .next
.ours:
    mov byte [px_ffound], 1
    mov ax, [px_find + 18]
    mov [px_fsize], ax
    mov ax, [px_find + 20]
    mov [px_fsize + 2], ax
    jmp short .next
.end:
    cmp byte [px_ffound], 0
    jne .yes
    stc
    ret
.yes:
    clc
    ret

; px_extfmt - SI = an 8.3 name: AL = the PXF_* its extension names, 0 none.
; Preserves all but AL
px_extfmt:
    push bx
    push cx
    push si
    push di
.dot:
    mov al, [si]
    or al, al
    jz .none
    inc si
    cmp al, '.'
    jne .dot
    mov di, px_exts                 ; 3 letters and the PXF_*, a row
.row:
    mov al, [di + 3]
    or al, al
    jz .none
    mov cx, 3
    xor bx, bx
.c:
    mov al, [si + bx]
    cmp al, [di + bx]
    jne .nx
    inc bx
    loop .c
    cmp byte [si + 3], 0
    jne .nx
    mov al, [di + 3]
    jmp short .out
.nx:
    add di, 4
    jmp short .row
.none:
    xor al, al
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; px_strcmp - compare the NUL strings SI and DI: ZF = 1 equal; else CF = 1
; when SI sorts first. Preserves all
px_strcmp:
    push ax
    push si
    push di
.l:
    mov al, [si]
    cmp al, [di]
    jne .out
    or al, al
    jz .out
    inc si
    inc di
    jmp short .l
.out:
    pop di
    pop si
    pop ax
    ret

; px_sniff - read the file's first cluster and name what it is (SPEC.md
; 106.6). READ_AT's capacity is a whole cluster (SPEC.md 18.4.4), so the
; buffer is one, in a DMA-safe claim that lives only for this call. Anything
; that refuses leaves the extension's answer standing
px_sniff:
    call OSAPI_VOL_STAT             ; AX = sectors a cluster, CX = bytes each
    jc .out
    mul cx                          ; AX = the cluster's bytes (<= 32KB)
    or dx, dx
    jnz .out
    mov [px_clb], ax
    add ax, 1023
    mov cl, 10
    shr ax, cl                      ; AX = its KB
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA        ; all of it DMA-safe: it is a disk buffer
    jc .out
    mov [px_sniffseg], dx
    mov es, dx
    xor bx, bx
    mov cx, [px_clb]
    xor ax, ax
    xor dx, dx
    mov si, px_fname
    call OSAPI_FILE_READ_AT         ; DX:AX = bytes delivered
    jc .free
    or dx, dx
    jz .lenok
    mov ax, [px_clb]
.lenok:
    mov [px_snlen], ax
    call px_sniffbuf                ; ES:0 = the bytes
.free:
    push ds
    pop es
    mov dx, [px_sniffseg]
    call OSAPI_MEM_FREE
.out:
    push ds
    pop es
    ret

; px_sniffbuf - ES:0..[px_snlen] = a file's first bytes: the format, and the
; header's width, height and bits where it states them. Every offset is
; checked against the length first; a header that lies is simply not believed
px_sniffbuf:
    mov cx, [px_snlen]
    cmp cx, 32
    jae .long
    jmp .out
.long:
    cmp word [es:0], 0xD8FF         ; FF D8: JPEG
    jne .png
    cmp byte [es:2], 0xFF
    jne .png
    mov byte [px_ffmt], PXF_JPEG
    jmp px_sniffjpeg
.png:
    cmp word [es:0], 0x5089         ; 89 'P' 'N' 'G'
    jne .gif
    cmp word [es:2], 0x474E
    jne .gif
    mov byte [px_ffmt], PXF_PNG
    cmp word [es:16], 0             ; a width past 65535 is not believed
    jne .out
    cmp word [es:20], 0
    jne .out
    mov ax, [es:18]
    xchg al, ah
    mov [px_fw], ax
    mov ax, [es:22]
    xchg al, ah
    mov [px_fh], ax
    mov al, [es:24]                 ; bit depth, times the samples a pixel
    mov bl, [es:25]                 ; colour type: 0 grey, 2 RGB, 3 palette,
    mov ah, 3                       ; 4 grey + alpha, 6 RGBA
    cmp bl, 2
    je .pbits
    cmp bl, 6
    je .pbits
    mov ah, 1
.pbits:
    mul ah
    mov [px_fbits], al
    jmp .out
.gif:
    cmp word [es:0], 0x4947         ; 'GIF8'
    jne .bmp
    cmp word [es:2], 0x3846
    jne .bmp
    mov byte [px_ffmt], PXF_GIF
    mov ax, [es:6]
    mov [px_fw], ax
    mov ax, [es:8]
    mov [px_fh], ax
    mov al, [es:10]
    mov ah, 8
    test al, 0x80
    jz .gbits
    and al, 7
    inc al
    mov ah, al
.gbits:
    mov [px_fbits], ah
    jmp .out
.bmp:
    cmp word [es:0], 0x4D42         ; 'BM'
    jne .pcx
    mov byte [px_ffmt], PXF_BMP
    cmp word [es:14], 12            ; an OS/2 v1 header: 16-bit sizes
    jne .bmpw
    mov ax, [es:18]
    mov [px_fw], ax
    mov ax, [es:20]
    mov [px_fh], ax
    mov al, [es:24]
    mov [px_fbits], al
    jmp .out
.bmpw:
    cmp word [es:20], 0
    jne .out
    mov ax, [es:18]
    mov [px_fw], ax
    mov ax, [es:22]                 ; a negative height is a top-down picture
    or ax, ax
    jns .bh
    neg ax
.bh:
    mov [px_fh], ax
    mov al, [es:28]
    mov [px_fbits], al
    jmp .out
.pcx:
    cmp byte [es:0], 0x0A           ; PCX: the manufacturer byte and RLE
    jne .tif
    cmp byte [es:2], 1
    jne .tif
    cmp byte [px_ffmt], PXF_PCX     ; ...and only by that name: two bytes are
    jne .tif                        ; too common a signature to trust alone
    mov ax, [es:8]
    sub ax, [es:4]
    inc ax
    mov [px_fw], ax
    mov ax, [es:10]
    sub ax, [es:6]
    inc ax
    mov [px_fh], ax
    cmp cx, 66
    jb .out
    mov al, [es:3]
    mul byte [es:65]
    mov [px_fbits], al
    jmp short .out
.tif:
    cmp word [es:0], 0x4949         ; 'II' 42 / 'MM' 42: TIFF
    jne .tifm
    cmp word [es:2], 42
    je .istif
.tifm:
    cmp word [es:0], 0x4D4D
    jne .pix
    cmp word [es:2], 0x2A00
    jne .pix
.istif:
    mov byte [px_ffmt], PXF_TIFF
    jmp short .out
.pix:
    cmp word [es:0], 0x384F         ; 'O8PIX', os8088's own (SPEC.md 94)
    jne .pnm
    cmp word [es:2], 0x4950
    jne .pnm
    mov byte [px_ffmt], PXF_PIX
    jmp short .out
.pnm:
    cmp byte [es:0], 'P'            ; 'P1'..'P6'
    jne .cz
    mov al, [es:1]
    cmp al, '1'
    jb .cz
    cmp al, '6'
    ja .cz
    mov byte [px_ffmt], PXF_PNM
    jmp short .out
.cz:
    cmp word [es:0], 0x5A43         ; 'CZ': packed on the disk (SPEC.md
    jne .out                        ; 20.14), which READ_AT hands over raw
    mov byte [px_ffmt], PXF_PACKED
.out:
    ret

; px_sniffjpeg - walk the markers to the frame header: its height, width,
; components, and whether it is progressive. ES:0 = the bytes, CX = their
; length. A JPEG whose frame header is past the first cluster (a large EXIF
; thumbnail) is named without dimensions
px_sniffjpeg:
    mov si, 2
.m:
    mov ax, si
    add ax, 10
    cmp ax, cx
    jae .out
    cmp byte [es:si], 0xFF
    jne .out
    mov al, [es:si + 1]
    cmp al, 0xFF                    ; fill bytes
    jne .mk
    inc si
    jmp short .m
.mk:
    cmp al, 0xD0                    ; RSTn, SOI, EOI and TEM have no length
    jb .len
    cmp al, 0xD9
    jbe .two
.len:
    cmp al, 0x01
    je .two
    cmp al, 0xC0
    jb .skip
    cmp al, 0xCF
    ja .skip
    cmp al, 0xC4                    ; DHT, JPG and DAC are not frame headers
    je .skip
    cmp al, 0xC8
    je .skip
    cmp al, 0xCC
    je .skip
    cmp al, 0xC2
    jne .sof
    mov byte [px_fprog], 1
.sof:
    mov ax, [es:si + 5]
    xchg al, ah
    mov [px_fh], ax
    mov ax, [es:si + 7]
    xchg al, ah
    mov [px_fw], ax
    mov al, [es:si + 9]             ; components: 1 grey, 3 colour
    mov ah, 8
    mul ah
    mov [px_fbits], al
    jmp short .out
.skip:
    mov ax, [es:si + 2]
    xchg al, ah
    add ax, 2
    jc .out
    add si, ax
    jc .out
    jmp short .m
.two:
    add si, 2
    jmp short .m
.out:
    ret

; px_compose - everything the window shows about the file, from what
; px_examine found: Image Info's values, the status fields, the canvas's
; second line, the filmstrip's title and line, and the window title
px_compose:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; Image Info 0: the name
    mov bx, 0
    mov si, px_fname
    call px_ivset
    ; 1: the folder, "B:\PATH"
    call px_pathstr                 ; px_cline = the folder
    mov bx, 1
    mov si, px_cline
    call px_ivset
    ; 2: the size, "87,432 bytes"
    mov di, px_cline
    mov ax, [px_fsize]
    mov dx, [px_fsize + 2]
    call px_u32
    mov si, px_s_bytes
    call px_strcat
    mov bx, 2
    mov si, px_cline
    call px_ivset
    mov bx, PX_SF_BYTES
    call px_sval
    ; 3: the format, and ", progressive" for a progressive JPEG
    mov al, [px_ffmt]
    call px_fmtname                 ; SI = its name
    mov di, px_cline
    call px_strcpy
    cmp byte [px_fprog], 0
    je .np
    mov si, px_s_prog
    call px_strcat
.np:
    mov bx, 3
    mov si, px_cline
    call px_ivset
    mov al, [px_ffmt]
    call px_fmtname
    mov bx, PX_SF_FMT
    call px_sval
    ; 4: the pixels, "640 x 480" (Info) and "640x480" (status)
    mov si, px_s_dash
    mov di, px_cline
    call px_strcpy
    mov di, px_cvdims
    call px_strcpy
    cmp word [px_fw], 0
    je .nodim
    cmp word [px_fh], 0
    je .nodim
    mov di, px_cline
    mov ax, [px_fw]
    xor dx, dx
    call px_u32
    mov si, px_s_x
    call px_strcat
    mov ax, [px_fh]
    xor dx, dx
    call px_u32n                    ; (appends)
    mov di, px_cvdims
    mov ax, [px_fw]
    xor dx, dx
    call px_u32
    mov si, px_s_xs
    call px_strcat
    mov ax, [px_fh]
    xor dx, dx
    call px_u32n
.nodim:
    mov bx, 4
    mov si, px_cline
    call px_ivset
    mov bx, PX_SF_DIMS
    mov si, px_cvdims
    call px_sval
    ; 5: the depth, "24-bit", and the status bar's colours
    mov si, px_s_dash
    mov di, px_cline
    call px_strcpy
    mov si, px_s_dash
    mov di, px_cvcols
    call px_strcpy
    mov al, [px_fbits]
    or al, al
    jz .nobits
    mov di, px_cline
    xor ah, ah
    xor dx, dx
    call px_u32
    mov si, px_s_bit
    call px_strcat
    mov si, px_s_c16m               ; the status bar: 2, 16, 256, 64K or
    cmp byte [px_fbits], 24         ; 16M colors
    jae .cols
    mov si, px_s_c64k
    cmp byte [px_fbits], 8
    ja .cols
    mov cl, [px_fbits]
    mov ax, 1
    shl ax, cl
    mov di, px_cvcols
    xor dx, dx
    call px_u32
    mov si, px_s_cols
    call px_strcat
    jmp short .nobits
.cols:
    mov di, px_cvcols
    call px_strcpy
.nobits:
    mov bx, 5
    mov si, px_cline
    call px_ivset
    mov bx, PX_SF_COLS
    mov si, px_cvcols
    call px_sval
    ; the status bar's name and place: "NAME.EXT", "6 of 13"
    mov bx, PX_SF_NAME
    mov si, px_fname
    call px_sval
    mov di, px_cline
    mov ax, [px_fidx]
    xor dx, dx
    call px_u32
    mov si, px_s_of
    call px_strcat
    mov ax, [px_fcount]
    xor dx, dx
    call px_u32n
    mov bx, PX_SF_POS
    mov si, px_cline
    call px_sval
    ; the canvas's second line: "VACATION.JPG  JPEG  640 x 480"
    mov si, px_fname
    mov di, px_cvline
    call px_strcpy
    mov si, px_s_gap
    call px_strcat
    mov al, [px_ffmt]
    call px_fmtname
    call px_strcat
    cmp word [px_fw], 0
    je .ncv
    mov si, px_s_gap
    call px_strcat
    mov si, px_ival + 4 * PX_IVSZ
    call px_strcat
.ncv:
    ; the filmstrip: "Images (13)" and "13 pictures in this folder"
    mov di, px_fstitle
    mov si, px_s_fsimg
    call px_strcpy
    mov ax, [px_fcount]
    xor dx, dx
    call px_u32n
    mov si, px_s_rpar
    call px_strcat
    mov di, px_fsline
    mov ax, [px_fcount]
    xor dx, dx
    call px_u32
    mov si, px_s_fsn
    call px_strcat
    ; the title: "PiXEL - VACATION.JPG"
    mov si, px_s_ttl
    mov di, px_title
    call px_strcpy
    mov si, px_s_dashsp
    call px_strcat
    mov si, px_fname
    call px_strcat
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_pathstr - px_cline = "D:\FOLDER\..." for where the instance stands
px_pathstr:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call OSAPI_FILE_HERE            ; BL = the drive
    mov al, 'A'
    add al, bl
    mov [px_cline], al
    mov byte [px_cline + 1], ':'
    push ds
    pop es
    mov di, px_cline + 2
    mov cx, PX_LINEMAX - 2
    call OSAPI_FILE_PATH            ; "\DIR\DIR", or CF = 1
    jnc .ok
    mov byte [px_cline + 2], '\'
    mov byte [px_cline + 3], 0
.ok:
    cmp byte [px_cline + 2], 0      ; the root answers ""
    jne .out
    mov byte [px_cline + 2], '\'
    mov byte [px_cline + 3], 0
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_fmtname - AL = a PXF_*: SI = its name. Preserves all but SI
px_fmtname:
    push bx
    mov bl, al
    xor bh, bh
    cmp bl, PXF_N
    jb .ok
    xor bl, bl
.ok:
    shl bx, 1
    mov si, [px_fmtnames + bx]
    pop bx
    ret

; =============================================================================
; STATE -> FLAGS, LABELS AND FIELDS
; =============================================================================

; px_flags - every button's flags and every box's label, from the state:
; the pictures, greyed while there is no picture (SPEC.md 47: the fact is
; "No picture", and the status bar's name field says it); the latched tool;
; each box's -, + or >. Preserves all
px_flags:
    push ax
    push bx
    push cx
    push si
    xor bx, bx
.f:
    mov ax, [px_bkind + bx]         ; the button's own bits: IMG, DIS-able
    mov cx, ax
    and ax, OS88UI_IMG
    test cx, PX_BK_PIC              ; needs a picture: greyed, for now always
    jz .nd
    or ax, OS88UI_DIS
.nd:
    mov [px_bflags + bx], ax
    add bx, 2
    cmp bx, PX_NB * 2
    jb .f
    mov al, [px_tool]               ; the latched tool
    xor ah, ah
    add ax, PX_B_T0
    mov bx, ax
    shl bx, 1
    or word [px_bflags + bx], OS88UI_LATCH
    call px_boxlabels
    pop si
    pop cx
    pop bx
    pop ax
    ret

; px_boxlabels - each box's label from the state: '>' on the compact layout,
; where the box turns the column to the next panel, else '-' to collapse or
; '+' to expand. Called by px_flags and by every layout, because the tier is
; the layout's to know. Preserves all
px_boxlabels:
    push ax
    push bx
    push cx
    push si
    xor bx, bx
.b:
    mov ax, px_s_next
    cmp byte [px_tier], 0
    jne .bl
    mov ax, px_s_minus
    mov cl, [px_bit + bx]
    test [px_pcol], cl
    jz .bl
    mov ax, px_s_plus
.bl:
    mov si, bx
    add si, PX_B_PB0
    shl si, 1
    mov [px_lab_full + si], ax
    mov [px_lab_half + si], ax
    inc bx
    cmp bx, 3
    jb .b
    mov ax, px_s_minus              ; ...and the filmstrip's
    cmp byte [px_fscol], 0
    je .fl
    mov ax, px_s_plus
.fl:
    mov [px_lab_full + 2 * PX_B_FSBOX], ax
    mov [px_lab_half + 2 * PX_B_FSBOX], ax
    pop si
    pop cx
    pop bx
    pop ax
    ret

; px_halfinit - the CGA's half-height pictures (pxicons.inc): each pair of
; rows ORed into one, so every one-pixel stroke survives. And the labels
; array that names them. Once, at launch. Preserves all
px_halfinit:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, pxi_open
    mov di, px_hicons
    mov bx, PXI_N
.icon:
    mov byte [di], 1
    mov byte [di + 1], 8
    mov cx, 8
.mask:
    mov word [di + 2], 0xFFFF
    add di, 2
    loop .mask
    sub di, 16                      ; DI = the record again
    push si
    add si, 2 + 32                  ; the source's data rows
    mov cx, 8
.data:
    mov ax, [si]
    or ax, [si + 2]
    mov [di + 2 + 16], ax
    add si, 4
    add di, 2
    loop .data
    pop si
    add di, PXH_SZ - 16             ; the next record
    add si, PXI_SZ
    dec bx
    jnz .icon
    xor bx, bx                      ; the labels: a picture's half twin
.lab:
    mov ax, [px_lab_full + bx]
    cmp ax, pxi_open
    jb .keep
    cmp ax, pxi_open + PXI_N * PXI_SZ
    jae .keep
    sub ax, pxi_open
    push dx
    xor dx, dx
    mov cx, PXI_SZ
    div cx
    mov cx, PXH_SZ
    mul cx
    pop dx
    add ax, px_hicons
.keep:
    mov [px_lab_half + bx], ax
    add bx, 2
    cmp bx, PX_NB * 2
    jb .lab
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; px_memfield - the status bar's "Memory: 412K", from OSAPI_MEM_AVAIL's total
; free KB. px_sval marks it for redraw only when it changed
px_memfield:
    push ax
    push bx
    push dx
    push si
    push di
    call OSAPI_MEM_AVAIL            ; BX = total free KB
    mov si, px_s_mem
    mov di, px_cline
    call px_strcpy
    mov ax, bx
    xor dx, dx
    call px_u32n
    mov si, px_s_k
    call px_strcat
    mov bx, PX_SF_MEM
    mov si, px_cline
    call px_sval
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; px_sval - field BX's value := SI (cut at its width). Marks it dirty when it
; changed, and only then. Preserves all
px_sval:
    push ax
    push cx
    push si
    push di
    mov di, bx
    mov cl, 4
    shl di, cl
    add di, px_sv                   ; DI = its buffer
    mov cl, [px_sfw + bx]
    xor ch, ch
    push si
    push di
.cmp:                               ; the same value: nothing to do
    mov al, [si]
    cmp al, [di]
    jne .diff
    or al, al
    jz .same
    inc si
    inc di
    loop .cmp
    cmp byte [di], 0                ; equal for the whole width
    je .same
.diff:
    pop di
    pop si
    mov cl, [px_sfw + bx]
.cp:
    mov al, [si]
    or al, al
    jz .term
    mov [di], al
    inc si
    inc di
    loop .cp
.term:
    mov byte [di], 0
    mov al, [px_bit + bx]
    or [px_sdirty], al
    jmp short .out
.same:
    pop di
    pop si
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; px_sflush - redraw the dirty status fields, each alone as its own run.
; Lock held and clip armed by the caller (px_regdraw). Preserves all
px_sflush:
    push ax
    push bx
    xor bx, bx
.f:
    mov al, [px_bit + bx]
    test [px_sdirty], al
    jz .n
    test [px_skeep], al
    jz .n                           ; not on the bar at this width
    call px_sfrun
.n:
    inc bx
    cmp bx, PX_NSF
    jb .f
    mov byte [px_sdirty], 0
    pop bx
    pop ax
    ret

; px_ivset - Image Info's value BX := SI (cut to its buffer). Preserves all
px_ivset:
    push ax
    push cx
    push dx
    push si
    push di
    mov ax, bx
    mov cx, PX_IVSZ
    mul cx
    add ax, px_ival
    mov di, ax
    mov cx, PX_IVSZ - 1
.cp:
    mov al, [si]
    or al, al
    jz .t
    mov [di], al
    inc si
    inc di
    loop .cp
.t:
    mov byte [di], 0
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; STRINGS AND NUMBERS
; =============================================================================

; px_strcpy - SI to DI, NUL included. Preserves all but DI, left ON the NUL
px_strcpy:
    push ax
    push si
.c:
    mov al, [si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    jmp short .c
.out:
    pop si
    pop ax
    ret

; px_strcat - SI onto the end of the string DI is ON (or anywhere in): DI
; left on the new NUL. Preserves all but DI
px_strcat:
.e:
    cmp byte [di], 0
    je px_strcpy
    inc di
    jmp short .e

; px_u32 - DX:AX as decimal with thousands commas at DI, NUL-terminated; DI
; left on the NUL. px_u32n is the same appended to the string DI is in
px_u32n:
.e:
    cmp byte [di], 0
    je px_u32
    inc di
    jmp short .e
px_u32:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, px_digs + 10            ; the digits, written backwards into a
.d:                                 ; buffer (not pushed: a loop that grows
    mov cx, 10                      ; the stack is one stkbalance cannot see
    mov bx, ax                      ; the end of)
    mov ax, dx                      ; DX:AX / 10, two divides
    xor dx, dx
    div cx
    xchg ax, bx                     ; BX = the high quotient
    div cx                          ; AX = the low one, DX = the digit
    add dl, '0'
    dec si
    mov [si], dl
    mov dx, bx
    mov cx, ax
    or cx, dx
    jnz .d
    mov cx, px_digs + 10
    sub cx, si                      ; CX = how many
.p:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jz .end
    mov ax, cx                      ; a comma before every third digit left
    mov bl, 3
    div bl
    or ah, ah
    jnz .p
    mov byte [di], ','
    inc di
    jmp short .p
.end:
    mov byte [di], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "pxui.inc"

; =============================================================================
; DATA
; =============================================================================

; --- the window (SPEC.md 106.1) -----------------------------------------------
PX_MINW     equ 336                 ; below this the toolbar loses its tail
PX_MINH     equ 150                 ; ...and the tool column its last tool

px_tpl:     dw 0, 20, 640, 440      ; the frame; OSAPI_WM_PREFER resizes it
            dw px_ttl, px_paint, px_onkey, 0

    ; As wide as each card and as tall as it will give us (os88api.inc's
    ; OS88_PREFER note): the kernel clamps the height to the desktop band
    OS88_PREFER px_pref, 640, 480,  720, 348,  640, 200

px_ttl:     db 'PiXEL', 0

; --- the palettes, by depth (px_display): chrome, body, strip, strip text,
; canvas, canvas text, rule, chrome text ---------------------------------------
px_pal4:    db CLGRAY, CWHITE, CBLUE, CWHITE, CDGRAY, CWHITE, CDGRAY, CBLACK
px_pal1:    db CWHITE, CWHITE, CBLACK, CWHITE, CWHITE, CBLACK, CBLACK, CBLACK

; --- per display kind: VGA, Hercules (and EGA), CGA ----------------------------
px_k_thumb: dw 48, 31, 20           ; a filmstrip thumbnail's rows: 64 wide
                                    ; at the pixel's own aspect
px_k_nav:   dw 60, 46, 40           ; the Navigator's body (its buttons fit)
px_k_pw:    dw 176, 184, 176        ; the panel column's width

px_bit:     db 1, 2, 4, 8, 16, 32, 64, 128

; --- the toolbar: per item, the button (0xFF = a separator), its cell in
; glyphs with captions and without, the button's width, the caption's length,
; and the caption -----------------------------------------------------------------
PX_TBD_BTN  equ 0
PX_TBD_CC   equ 1
PX_TBD_NC   equ 2
PX_TBD_W    equ 3
PX_TBD_CL   equ 4
PX_TBD_CAP  equ 6
PX_TBDSZ    equ 8
px_tbdef:
    db PX_B_OPEN, 6, 3, PX_BTNW, 4, 0
    dw px_c_open
    db PX_B_SAVE, 6, 3, PX_BTNW, 4, 0
    dw px_c_save
    db 0xFF, 1, 1, 0, 0, 0
    dw px_s_empty0
    db PX_B_PREV, 6, 3, PX_BTNW, 4, 0
    dw px_c_prev
    db PX_B_NEXT, 6, 3, PX_BTNW, 4, 0
    dw px_c_next
    db 0xFF, 1, 1, 0, 0, 0
    dw px_s_empty0
    db PX_B_ZIN, 9, 3, PX_BTNW, 7, 0
    dw px_c_zin
    db PX_B_ZOUT, 10, 3, PX_BTNW, 8, 0
    dw px_c_zout
    db PX_B_FIT, 5, 3, PX_BTNW, 3, 0
    dw px_c_fit
    db PX_B_ONE, 5, 4, PX_ONEW, 0, 0
    dw px_s_empty0
    db 0xFF, 1, 1, 0, 0, 0
    dw px_s_empty0
    db PX_B_ROT, 8, 3, PX_BTNW, 6, 0
    dw px_c_rot
    db PX_B_SHOW, 11, 3, PX_BTNW, 9, 0
    dw px_c_show
%if ($ - px_tbdef) != PX_TBN * PX_TBDSZ
  %error "px_tbdef has PX_TBN rows of PX_TBDSZ bytes"
%endif
px_c_open:  db 'Open', 0
px_c_save:  db 'Save', 0
px_c_prev:  db 'Prev', 0
px_c_next:  db 'Next', 0
px_c_zin:   db 'Zoom In', 0
px_c_zout:  db 'Zoom Out', 0
px_c_fit:   db 'Fit', 0
px_c_rot:   db 'Rotate', 0
px_c_show:  db 'Slideshow', 0
px_s_empty0: db 0

; --- the buttons' labels (full-height pictures; px_lab_half is the CGA's) -----
px_lab_full:
    dw pxi_open, pxi_save, pxi_prev, pxi_next, pxi_zoomin, pxi_zoomout
    dw pxi_fit, px_s_one, pxi_rotate, pxi_show
    dw pxi_hand, pxi_magnify, pxi_marquee, pxi_crop, pxi_eyedrop, pxi_rotate
    dw px_s_plus, px_s_minus, px_s_fit
    dw px_s_minus, px_s_minus, px_s_minus
    dw px_s_minus, px_s_lt, px_s_gt
    dw px_s_lt, px_s_gt
%if ($ - px_lab_full) != PX_NB * 2
  %error "px_lab_full has a label per button"
%endif

; --- each button's kind: OS88UI_IMG for a picture, PX_BK_PIC when it needs a
; picture and is greyed without one (px_flags) ---------------------------------
PX_BK_PIC   equ 0x8000
px_bkind:
    dw OS88UI_IMG                               ; Open
    dw OS88UI_IMG | PX_BK_PIC                   ; Save
    dw OS88UI_IMG | PX_BK_PIC                   ; Prev
    dw OS88UI_IMG | PX_BK_PIC                   ; Next
    dw OS88UI_IMG | PX_BK_PIC                   ; Zoom In
    dw OS88UI_IMG | PX_BK_PIC                   ; Zoom Out
    dw OS88UI_IMG | PX_BK_PIC                   ; Fit
    dw PX_BK_PIC                                ; 1:1
    dw OS88UI_IMG | PX_BK_PIC                   ; Rotate
    dw OS88UI_IMG | PX_BK_PIC                   ; Slideshow
    times PX_NTOOL dw OS88UI_IMG                ; the tools
    dw PX_BK_PIC, PX_BK_PIC, PX_BK_PIC          ; Navigator's +, -, Fit
    dw 0, 0, 0                                  ; the panel boxes
    dw 0                                        ; the filmstrip's box
    dw PX_BK_PIC, PX_BK_PIC                     ; ...its arrows
    dw PX_BK_PIC, PX_BK_PIC                     ; the status bar's
%if ($ - px_bkind) != PX_NB * 2
  %error "px_bkind has a kind per button"
%endif

px_toolkeys: db 'HZMCER', 0         ; Hand, Zoom, Marquee, Crop, Eyedropper,
                                    ; Rotate - the tool column's order

; --- the panels ------------------------------------------------------------------
px_ptitle:  dw px_s_pnav, px_s_phist, px_s_pinfo
px_pbodyp:  dw px_body_nav, px_body_hist, px_body_info
px_s_pnav:  db 'Navigator', 0
px_s_phist: db 'Histogram', 0
px_s_pinfo: db 'Image Info', 0
px_hlines:  dw px_s_hmean, px_s_hsd, px_s_hmin, px_s_hmax
px_s_hmean: db 'Mean    -', 0
px_s_hsd:   db 'Std Dev -', 0
px_s_hmin:  db 'Min     -', 0
px_s_hmax:  db 'Max     -', 0
px_ilabels: dw px_s_ifile, px_s_ifold, px_s_isize, px_s_ifmt, px_s_ipix
            dw px_s_idep
px_s_ifile: db 'File', 0
px_s_ifold: db 'Folder', 0
px_s_isize: db 'Size', 0
px_s_ifmt:  db 'Format', 0
px_s_ipix:  db 'Pixels', 0
px_s_idep:  db 'Depth', 0

; --- the status fields: widths in glyphs, and the order they leave a narrow
; bar in (the name never does) ----------------------------------------------------
px_sfw:     db 12, 9, 4, 4, 10, 12, 12, 10
px_sdrop:   db PX_SF_COLS, PX_SF_BYTES, PX_SF_MEM, PX_SF_FMT, PX_SF_ZOOM
            db PX_SF_DIMS, PX_SF_POS

; --- the formats --------------------------------------------------------------------
px_fmtnames:
    dw px_s_dash, px_f_jpeg, px_f_png, px_f_gif, px_f_bmp, px_f_pcx
    dw px_f_tiff, px_f_tga, px_f_pix, px_f_pnm, px_f_ico, px_f_lbm
    dw px_f_mac, px_f_packed
px_f_jpeg:  db 'JPEG', 0
px_f_png:   db 'PNG', 0
px_f_gif:   db 'GIF', 0
px_f_bmp:   db 'BMP', 0
px_f_pcx:   db 'PCX', 0
px_f_tiff:  db 'TIFF', 0
px_f_tga:   db 'TGA', 0
px_f_pix:   db 'PIX', 0
px_f_pnm:   db 'PNM', 0
px_f_ico:   db 'ICO', 0
px_f_lbm:   db 'ILBM', 0
px_f_mac:   db 'MAC', 0
px_f_packed: db 'Packed', 0
; the extensions a picture of PiXEL's may carry: three letters and its PXF_*
px_exts:
    db 'JPG', PXF_JPEG
    db 'JPE', PXF_JPEG
    db 'PNG', PXF_PNG
    db 'GIF', PXF_GIF
    db 'BMP', PXF_BMP
    db 'PCX', PXF_PCX
    db 'TIF', PXF_TIFF
    db 'TGA', PXF_TGA
    db 'PIX', PXF_PIX
    db 'PBM', PXF_PNM
    db 'PGM', PXF_PNM
    db 'PPM', PXF_PNM
    db 'PNM', PXF_PNM
    db 'ICO', PXF_ICO
    db 'CUR', PXF_ICO
    db 'LBM', PXF_LBM
    db 'IFF', PXF_LBM
    db 'MAC', PXF_MAC
    db 0, 0, 0, 0

; --- the menus (SPEC.md 106.2). A greyed item begins MENU_DIS; a separator is
; a greyed row of dashes (kernel/menu.inc's own idiom) ------------------------------
PX_M_FILE   equ 0
PX_M_VIEW   equ 4
PX_MV_DITHER equ 6
PX_MV_PANELS equ 7
PX_MV_FILM  equ 8
PX_MV_HELP  equ 9

    OS88_MENUSET px_menus, px_ttl, px_cmd
        OS88_MENU px_m_file, px_mif, 9
        OS88_MENU px_m_edit, px_mie, 7
        OS88_MENU px_m_image, px_mii, 11
        OS88_MENU px_m_fx, px_mix, 10
        OS88_MENU px_m_view, px_miv, 10
    OS88_MENUSET_END px_menus

px_m_file:  db 'File', 0
px_m_edit:  db 'Edit', 0
px_m_image: db 'Image', 0
px_m_fx:    db 'Effects', 0
px_m_view:  db 'View', 0
px_sep:     db MENU_DIS, '----------------', 0

px_mif: dw px_mi_open, px_mi_saveas, px_mi_revert, px_sep, px_mi_prev
        dw px_mi_next, px_mi_slide, px_sep, px_mi_info
px_mi_open:   db 'Open...  Ctrl+O', 0
px_mi_saveas: db MENU_DIS, 'Save As...', 0
px_mi_revert: db MENU_DIS, 'Revert', 0
px_mi_prev:   db MENU_DIS, 'Previous Image', 0
px_mi_next:   db MENU_DIS, 'Next Image', 0
px_mi_slide:  db MENU_DIS, 'Slideshow', 0
px_mi_info:   db MENU_DIS, 'Image Info...', 0

px_mie: dw px_mi_undo, px_sep, px_mi_copy, px_sep, px_mi_selall
        dw px_mi_desel, px_mi_crop
px_mi_undo:   db MENU_DIS, 'Undo', 0
px_mi_copy:   db MENU_DIS, 'Copy', 0
px_mi_selall: db MENU_DIS, 'Select All', 0
px_mi_desel:  db MENU_DIS, 'Deselect', 0
px_mi_crop:   db MENU_DIS, 'Crop to Selection', 0

px_mii: dw px_mi_rcw, px_mi_rccw, px_mi_r180, px_mi_fliph, px_mi_flipv
        dw px_sep, px_mi_resize, px_mi_alev, px_mi_bc, px_mi_grey, px_mi_inv
px_mi_rcw:    db MENU_DIS, 'Rotate 90 CW', 0
px_mi_rccw:   db MENU_DIS, 'Rotate 90 CCW', 0
px_mi_r180:   db MENU_DIS, 'Rotate 180', 0
px_mi_fliph:  db MENU_DIS, 'Flip Horizontal', 0
px_mi_flipv:  db MENU_DIS, 'Flip Vertical', 0
px_mi_resize: db MENU_DIS, 'Resize...', 0
px_mi_alev:   db MENU_DIS, 'Auto Levels', 0
px_mi_bc:     db MENU_DIS, 'Brightness/Contrast...', 0
px_mi_grey:   db MENU_DIS, 'Greyscale', 0
px_mi_inv:    db MENU_DIS, 'Invert', 0

px_mix: dw px_mi_blur, px_mi_sharp, px_mi_edge, px_mi_emb, px_mi_pixl
        dw px_sep, px_mi_sepia, px_mi_post, px_mi_thr, px_mi_gam
px_mi_blur:   db MENU_DIS, 'Blur', 0
px_mi_sharp:  db MENU_DIS, 'Sharpen', 0
px_mi_edge:   db MENU_DIS, 'Edge Detect', 0
px_mi_emb:    db MENU_DIS, 'Emboss', 0
px_mi_pixl:   db MENU_DIS, 'Pixelate', 0
px_mi_sepia:  db MENU_DIS, 'Sepia', 0
px_mi_post:   db MENU_DIS, 'Posterize...', 0
px_mi_thr:    db MENU_DIS, 'Threshold...', 0
px_mi_gam:    db MENU_DIS, 'Gamma...', 0

px_miv: dw px_mi_zin, px_mi_zout, px_mi_fit, px_mi_actual, px_sep
        dw px_mi_full, px_mi_dord, px_mi_hidep, px_mi_hidef, px_mi_help
px_mi_zin:    db MENU_DIS, 'Zoom In', 0
px_mi_zout:   db MENU_DIS, 'Zoom Out', 0
px_mi_fit:    db MENU_DIS, 'Fit', 0
px_mi_actual: db MENU_DIS, 'Actual Size', 0
px_mi_full:   db MENU_DIS, 'Full Screen', 0
px_mi_dord:   db 'Dither: Ordered', 0
px_mi_ddif:   db 'Dither: Diffusion', 0
px_mi_hidep:  db 'Hide Panels', 0
px_mi_showp:  db 'Show Panels', 0
px_mi_hidef:  db 'Hide Filmstrip', 0
px_mi_showf:  db 'Show Filmstrip', 0
px_mi_help:   db 'Keyboard Help  F1', 0

; --- About (SPEC.md 12.7): the app-name cell's item, not a Help menu -------------
px_ablines: dw px_ab1, px_ab2, px_ab3, px_ab0, px_ab4, 0
px_ab1:     db 'PiXEL', 0
px_ab2:     db 'An image viewer and editor', 0
px_ab3:     db 'for VGA, CGA and Hercules', 0
px_ab0:     db 0
px_ab4:     db 'Part of os8088', 0

; --- the words the window says ---------------------------------------------------
px_s_empty: db 'Open a picture  (Ctrl+O)', 0
px_s_nopic: db 'No picture', 0
px_s_dash:  db '-', 0
px_s_one:   db '1:1', 0
px_s_plus:  db '+', 0
px_s_minus: db '-', 0
px_s_next:  db '>', 0
px_s_lt:    db '<', 0
px_s_gt:    db '>', 0
px_s_fit:   db 'Fit', 0
px_s_fs0:   db 'Images', 0
px_s_fsimg: db 'Images (', 0
px_s_rpar:  db ')', 0
px_s_fsnone: db 'Open a picture to see its folder', 0
px_s_fsn:   db ' pictures in this folder', 0
px_s_bytes: db ' bytes', 0
px_s_prog:  db ', progressive', 0
px_s_x:     db ' x ', 0
px_s_xs:    db 'x', 0
px_s_bit:   db '-bit', 0
px_s_cols:  db ' colors', 0
px_s_c16m:  db '16M colors', 0
px_s_c64k:  db '64K colors', 0
px_s_of:    db ' of ', 0
px_s_gap:   db '  ', 0
px_s_mem:   db 'Memory: ', 0
px_s_k:     db 'K', 0
px_s_ttl:   db 'PiXEL', 0
px_s_dashsp: db ' - ', 0
px_s_notfile: db 'Not a file here', 0       ; toasts: 24 glyphs at most
px_s_badpart: db 'PiXEL part is damaged', 0 ; (SPEC.md 59.10)

%include "pxicons.inc"

%define OS88UI_BIMG                 ; the one-write button body, and pictures
%define OS88UI_ABOUT                ; the About card, and the keyboard card
%define OS88UI_NOGLYPH              ; no check box, no radio
%include "os88ui.inc"

; --- the part table, and the standard's code after it (SPEC.md 20.12.3) -------
    OS88_PARTS_BEGIN PX_NPARTS
      OS88_PART OP_SEG, OP_LAZY             ; 0 the keyboard card (106.5: plain)
    OS88_PARTS_END

; =============================================================================
; BSS - an equ chain after the parts standard's own (os88parts.inc)
; =============================================================================
PXB equ os88_image_end + OP_BSS
px_win      equ PXB + 0             ; word
px_homedir  equ PXB + 2             ; word: the folder PIXEL.O88 is in
px_homevol  equ PXB + 4             ; byte
px_argvol   equ PXB + 5             ; byte
px_argdir   equ PXB + 6             ; word
px_argpend  equ PXB + 8             ; byte
px_tool     equ PXB + 9             ; byte
px_argname  equ PXB + 10            ; 13
px_fname    equ PXB + 23            ; 13
px_cx0      equ PXB + 36            ; word: the content box, absolute
px_cy0      equ PXB + 38
px_w        equ PXB + 40
px_h        equ PXB + 42
px_xr       equ PXB + 44
px_yb       equ PXB + 46
px_bpp      equ PXB + 48            ; byte: the display's depth
px_kind     equ PXB + 49            ; byte: VGA / Hercules (and EGA) / CGA
px_half     equ PXB + 50            ; byte: half-height pictures (a CGA)
px_tier     equ PXB + 51            ; byte: 0 full, 1 compact
px_caps     equ PXB + 52            ; byte: the toolbar has captions
px_fson     equ PXB + 53            ; byte: the filmstrip is laid out
px_fscol    equ PXB + 54            ; byte: ...collapsed
px_fsuser   equ PXB + 55            ; byte: 0 the tier's, 1 on, 2 off
px_pnon     equ PXB + 56            ; byte: the panel column is laid out
px_pnoff    equ PXB + 57            ; byte: View > Hide Panels
px_pvis     equ PXB + 58            ; byte: the panels laid out, a bit each
px_pcol     equ PXB + 59            ; byte: ...collapsed by the user
px_pcur     equ PXB + 60            ; byte: the compact layout's panel
px_dither   equ PXB + 61            ; byte: 0 ordered, 1 diffusion
px_btnh     equ PXB + 62            ; word
px_sth      equ PXB + 64
px_sh       equ PXB + 66
px_lp       equ PXB + 68
px_thh      equ PXB + 70
px_navh     equ PXB + 72
px_pw       equ PXB + 74
px_tbh      equ PXB + 76
px_tby2     equ PXB + 78
px_sty1     equ PXB + 80
px_fsy1     equ PXB + 82
px_midy1    equ PXB + 84
px_midy2    equ PXB + 86
px_tcx2     equ PXB + 88
px_pnx1     equ PXB + 90
px_cvx1     equ PXB + 92
px_cvx2     equ PXB + 94
px_sbx      equ PXB + 96
px_pyw      equ PXB + 98            ; 3 words: each panel's strip
px_pbhw     equ PXB + 104           ; 3 words: ...its body's rows
px_pbody    equ PXB + 110           ; 3 words: ...its body's full height
px_pcurw    equ PXB + 116           ; word: px_pcur, as a word
px_pc       equ PXB + 118           ; byte: px_fillc's colour
px_rmask    equ PXB + 119           ; byte
px_c_chrome equ PXB + 120           ; 8 bytes: the palette, px_pal4's order
px_c_body   equ PXB + 121
px_c_strip  equ PXB + 122
px_c_stript equ PXB + 123
px_c_canvas equ PXB + 124
px_c_cvtext equ PXB + 125
px_c_rule   equ PXB + 126
px_c_text   equ PXB + 127
px_dx1      equ PXB + 128           ; the damage rect
px_dy1      equ PXB + 130
px_dx2      equ PXB + 132
px_dy2      equ PXB + 134
px_icpair   equ PXB + 136
px_icx      equ PXB + 138
px_icy      equ PXB + 140
px_tbink    equ PXB + 142
px_tbx1     equ PXB + 144
px_tbx2     equ PXB + 146
px_tby      equ PXB + 148
px_tbcx     equ PXB + 150
px_tbn      equ PXB + 152
px_sty      equ PXB + 154
px_sbox     equ PXB + 156
px_bx1      equ PXB + 158
px_by1      equ PXB + 160
px_bx2      equ PXB + 162
px_by2      equ PXB + 164
px_tyy      equ PXB + 166
px_rx1      equ PXB + 168
px_ry1      equ PXB + 170
px_rx2      equ PXB + 172
px_ry2      equ PXB + 174
px_scells   equ PXB + 176
px_skeep    equ PXB + 178           ; byte
px_sdirty   equ PXB + 179           ; byte
                                    ; (PXB + 180..187 free)
px_press    equ PXB + 188           ; byte
px_abon     equ PXB + 189           ; byte
px_helpon   equ PXB + 190           ; byte
px_ppart    equ PXB + 191           ; byte
px_pvec     equ PXB + 192           ; byte
px_pmoved   equ PXB + 193           ; byte
px_pwas     equ PXB + 194           ; word
px_pwvol    equ PXB + 196           ; byte
px_fhave    equ PXB + 197           ; byte: a file has been examined
px_pfar     equ PXB + 198           ; dword: the far vector being called
px_pres     equ PXB + 202           ; word: INIT's answer, for the gate
px_pcalls   equ PXB + 204           ; word: far calls made, for the gate
px_fsize    equ PXB + 206           ; dword
px_fw       equ PXB + 210
px_fh       equ PXB + 212
px_ffmt     equ PXB + 214           ; byte
px_fbits    equ PXB + 215           ; byte
px_fprog    equ PXB + 216           ; byte
px_ffound   equ PXB + 217           ; byte
px_fcount   equ PXB + 218
px_fidx     equ PXB + 220
px_clb      equ PXB + 222
px_sniffseg equ PXB + 224
px_snlen    equ PXB + 226
px_tyy0     equ PXB + 228           ; the status text's row (px_draw_status)
px_tbon     equ PXB + 230           ; PX_TBN bytes
px_tbcap    equ PXB + 244           ; PX_TBN bytes
px_tbx      equ PXB + 258           ; PX_TBN words
px_find     equ PXB + 284           ; OSAPI_FIND_SZ
px_brects   equ PXB + 308           ; PX_NB * 8
px_bflags   equ px_brects + PX_NB * 8           ; PX_NB words
px_lab_half equ px_bflags + PX_NB * 2           ; PX_NB words
px_sv       equ px_lab_half + PX_NB * 2         ; PX_NSF * PX_SVSZ
px_ival     equ px_sv + PX_NSF * PX_SVSZ        ; PX_INFON * PX_IVSZ
px_line     equ px_ival + PX_INFON * PX_IVSZ    ; PX_LINEMAX + 1
px_cline    equ px_line + PX_LINEMAX + 2
px_cvline   equ px_cline + PX_LINEMAX + 2       ; 48
px_cvdims   equ px_cvline + 48                  ; 16
px_cvcols   equ px_cvdims + 16                  ; 16
px_fstitle  equ px_cvcols + 16                  ; 24
px_fsline   equ px_fstitle + 24                 ; 48
px_title    equ px_fsline + 48                  ; 24
px_helptab  equ px_title + 24                   ; (PX_HELPMAX + 1) words
px_helpbuf  equ px_helptab + (PX_HELPMAX + 1) * 2
px_hicons   equ px_helpbuf + PX_HELPSZ          ; PXI_N * PXH_SZ
px_sfxw     equ px_hicons + PXI_N * PXH_SZ  ; PX_NSF words: each field's x
px_digs     equ px_sfxw + PX_NSF * 2            ; 10: px_u32's digits
PX_BSSEND   equ px_digs + 10
PX_BSS      equ PX_BSSEND - PXB

PXI_PHOTO   equ (pxi_photo - pxi_open) / PXI_SZ

    OS88_BSS OP_BSS + PX_BSS
    OS88_IMAGE_END
