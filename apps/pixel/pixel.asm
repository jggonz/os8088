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
%include "pxrec.inc"                ; the record, the formats, the decoders

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
PXPART_GIF  equ 1                   ; the GIF decoder, apps/pixel/pxgif.asm
PXPART_PNG  equ 2                   ; the PNG decoder, apps/pixel/pxpng.asm
PXPART_JPEG equ 3                   ; the JPEG decoder, apps/pixel/pxjpeg.asm
PXPART_SIMP equ 4                   ; BMP, PCX, TGA, PNM, PIX, LINKED against
                                    ; the package: apps/pixel/pxsimp.asm
PX_NPARTS   equ 5                   ; (a decoder part is never 0: [px_kheld]
                                    ; 0 is "none held", SPEC.md 106.18)
PXD_QUIET   equ 0xFE                ; a refusal already said (op_fetch's own
                                    ; toast): px_refusal says nothing more

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
PX_B_FSL    equ 19                  ; the filmstrip's two arrows
PX_B_FSR    equ 20
PX_B_SPREV  equ 21                  ; the status bar's two
PX_B_SNEXT  equ 22
PX_NB       equ 23                  ; (no panel has a title strip or a box
                                    ; any more: SPEC.md 106.15)

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
PX_ILAB     equ 8                   ; Image Info's label column, in glyphs
PX_IVSZ     equ 24                  ; ...and a value's buffer
PX_HSTW     equ 11                  ; Histogram's statistics column, glyphs:
                                    ; "Std Dev" and three digits
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
PX_R_NFRAME equ 128                 ; the Navigator's frame, where the view
                                    ; now is
PX_R_ALL    equ 63

PX_PR_NONE   equ 0                  ; what a press that was no button's hit
PX_PR_CANVAS equ 1
PX_PR_PANEL  equ 2                  ; the compact layout's panel, no control
PX_PR_HAND   equ 3                  ; a Hand drag on the picture
PX_PR_NAV    equ 4                  ; a press or drag in the Navigator
PX_NAMES     equ 128                ; the folder's pictures px_walk keeps


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
    call px_qinit                   ; the cube's level tables (pxmaster.inc)
    call px_slotsinit               ; every record owed (pxui.inc)
    call px_initstate
    mov si, px_tpl
    call OSAPI_WM_CREATE
    jnc .made
    jmp .fail
.made:
    mov [px_win], bx
    call px_artload                 ; the colour face's pictures, on a colour
                                    ; primary (SPEC.md 106.16): after the
                                    ; window, so a failed entry holds none
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
    mov word [px_ncur], 0xFFFF      ; no folder yet
    mov byte [px_zfit], 1
    mov byte [px_zreq], 0xFF        ; no re-decode asked for (SPEC.md 106.19)
    xor bx, bx
.sv:
    mov si, px_s_empty0             ; every field blank until it knows
    call px_sval                    ; (a bar of dashes reads as broken)
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
    jc .all
    cmp ax, cx                      ; an empty rect is legal and means draw
    jg .out                         ; nothing (wave-1 review NIT 6)
    cmp bx, dx
    jg .out
    jmp short .rect
.all:
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
    mov al, PX_R_TB
    call px_owe                     ; drawn whole: its records too
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
    mov al, PX_R_TOOLS
    call px_owe                     ; drawn whole: its records too
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
    mov al, PX_R_PANELS
    call px_owe                     ; drawn whole: its records too
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
    mov al, PX_R_FS
    call px_owe                     ; drawn whole: its records too
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
    mov al, PX_R_STATUS
    call px_owe                     ; drawn whole: its records too
    call px_draw_status
    mov byte [px_sdirty], 0         ; the fields on the glass are the values
    jmp short .out
.sd:
    cmp byte [px_sdirty], 0         ; a field that changed while this window
    je .fr                          ; could not show it is drawn by whatever
    call px_sflush                  ; draws next, whatever it was asked for
.fr:
    test byte [px_rmask], PX_R_NFRAME
    jz .out                         ; the frame follows the view: off where
    test byte [px_rmask], PX_R_PANELS
    jnz .out                        ; it was, on where it is (the panels,
    call px_navframe_off            ; drawn, drew it fresh)
    call px_navframe_on
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
    je .i
    mov si, px_helptab
    call os88ui_about_d
.i:
    cmp byte [px_infoon], 0
    je .out
    mov si, px_infotab
    call os88ui_about_d
.out:
    pop si
    pop bx
    ret

; px_regdraw - AL = a mask of PX_R_* regions, NOW, from a callback (the lock
; is held there and nothing has armed a clip). Lays out first, because the
; window may have moved since it was painted. A card that is up is left
; alone - taking it down repaints everything anyway. Two ways (SPEC.md
; 106.15):
;   [px_geo] set - a command that MOVED a region (a panel shown or turned,
;     the filmstrip hidden, a card taken down, a list closed over the
;     content): each region in the mask is drawn whole, as W_PAINT does;
;   otherwise the canvas, when it is in the mask, is rendered - the picture
;     is the renderer's, not a record's - and the rest of the window is
;     walked through its records (px_usync), which draw only what moved.
; Preserves all
px_regdraw:
    push ax
    push bx
    cmp byte [px_abon], 0
    jne .out
    cmp byte [px_helpon], 0
    jne .out
    cmp byte [px_infoon], 0
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
    mov byte [px_udrew], 0
    cmp byte [px_geo], 0
    jne .geo
    test al, PX_R_CANVAS
    jz .walk
    push ax
    mov al, PX_R_CANVAS
    call px_draw
    pop ax
.walk:
    call px_usync
    cmp byte [px_half], 0           ; the CGA's 11-row bar under a 13-row
    je .clr                         ; grow box: a region over it drawn, the
    test al, PX_R_CANVAS            ; box goes back
    jnz .grow
    cmp byte [px_udrew], 0
    je .clr
    jmp short .grow
.geo:
    call px_draw
    ; A SELF-INITIATED REPAINT OF THE CORNER ENDS WITH THE GROW BOX (SPEC.md
    ; 11.1.1). The box is 13 rows: inside a 14-row status bar, so only the bar
    ; reaches it on the full layout, but two rows above an 11-row one on the
    ; CGA, where the regions over the bar reach it too
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
    mov byte [px_geo], 0
    pop bx
    pop ax
    ret

; px_regpaint - AL = a mask: px_regdraw's regions drawn WHOLE (a command
; that moved them). Preserves all
px_regpaint:
    mov byte [px_geo], 1
    jmp px_regdraw

; px_update - the window through its records alone: what moved is drawn
; (SPEC.md 106.15). From a callback. Preserves all
px_update:
    push ax
    xor al, al
    call px_regdraw
    pop ax
    ret

; --- W_ONCLICK, ours, in front of the library's -------------------------------
; A card that is up is taken down by the press, and the press does nothing
; else. Otherwise the rects are laid out where the window is NOW (a move does
; not call W_PAINT), the Histogram's drop-down sees the press first, and then
; the library (SPEC.md 20.5.1.3.3)
px_clickw:
    mov [px_win], si
    call px_carddown
    jc .out
    call px_layout
    jc .out
    call px_droppress               ; CF = 1: the drop-down had it
    jc .out
    jmp os88ui_btnclick
.out:
    ret

; px_droppress - CX, DX = a press: the Histogram's drop-down's, if it is on
; show. CF = 1 it was spent there (a new channel is applied)
px_droppress:
    test byte [px_pvis], 2
    jz .no
    cmp word [px_pbhw + 2], 0
    je .no
    push ax
    push bx
    mov bx, px_hdrop
    call os88ui_drpress             ; AH = 1 spent, AL = a pick, CF = repaint
    call px_dropdone
    pop bx
    pop ax
    ret
.no:
    clc
    ret

; px_dropdone - after os88ui_drpress / drup: AH = 1 spent, AL = a pick or
; 0FFh, CF = 1 the list came down without its bank. A pick refolds the
; Histogram (SPEC.md 106.12). out CF = 1 when the press was the drop's
px_dropdone:
    pushf
    or ah, ah
    jz .free
    popf
    jnc .pick
    push ax
    mov al, PX_R_ALL                ; the list came down over the content
    call px_regpaint
    pop ax
.pick:
    cmp al, 0xFF
    je .spent
    mov [px_hchan], al
    call px_hstats
    call px_update                  ; the graph, the box, the numbers
.spent:
    stc
    ret
.free:
    popf
    clc
    ret

; px_carddown - if a card is up, take it down and repaint. CF = 1 it was
px_carddown:
    cmp byte [px_abon], 0
    jne .down
    cmp byte [px_helpon], 0
    jne .down
    cmp byte [px_infoon], 0
    jne .down
    clc
    ret
.down:
    mov byte [px_abon], 0
    mov byte [px_helpon], 0
    mov byte [px_infoon], 0
    push ax
    mov al, PX_R_ALL
    call px_regpaint
    pop ax
    stc
    ret

; a press on no button (the library hands it on): CX, DX screen, SI window.
; The canvas: with no picture it is a big Open button; with the Hand it
; starts a pan. The Navigator's picture: a pan to there. Anywhere else in a
; compact layout's panel: the next panel
px_onclick:
    mov byte [px_press], PX_PR_NONE
    push ax
    push bx
    cmp cx, [px_cvx1]
    jl .nav
    cmp cx, [px_cvx2]
    jg .nav
    cmp dx, [px_midy1]
    jl .nav
    cmp dx, [px_midy2]
    jg .nav
    mov al, PX_PR_CANVAS
    call px_haspic
    jc .got
    cmp byte [px_tool], 0           ; the Hand
    jne .out
    mov [px_hx0], cx                ; where the drag starts, and where the
    mov [px_hy0], dx                ; picture was
    mov ax, [px_ox]
    mov [px_hox], ax
    mov ax, [px_oy]
    mov [px_hoy], ax
    call OSAPI_GET_TICKS
    mov [px_htick], ax
    mov al, PX_PR_HAND
    jmp short .got
.nav:
    test byte [px_pvis], 1          ; the Navigator's picture
    jz .strip
    cmp word [px_pbhw], 0
    je .strip
    cmp cx, [px_nwell]
    jl .strip
    cmp cx, [px_nwell + 4]
    jg .strip
    cmp dx, [px_nwell + 2]
    jl .strip
    cmp dx, [px_nwell + 6]
    jg .strip
    call px_navpan
    call OSAPI_GET_TICKS
    mov [px_htick], ax
    mov al, PX_PR_NAV
    jmp short .got
.strip:
    mov al, PX_PR_PANEL             ; the compact layout's one panel, where
    cmp byte [px_tier], 0           ; no control took the press: it turns to
    je .out                         ; the next (SPEC.md 106.15 - there is no
    cmp byte [px_pnon], 0           ; title strip, and no box, to do it)
    je .out
    cmp cx, [px_pnx1]
    jle .out
    cmp dx, [px_midy1]
    jl .out
    cmp dx, [px_midy2]
    jg .out
.got:
    mov [px_press], al
.out:
    pop bx
    pop ax
    ret

; W_ONDRAG: the pressed button follows the pointer; a Hand drag or a drag in
; the Navigator pans, at most once a tick (SPEC.md 106.11); an open list
; follows the pointer
px_ondrag:
    push ax
    push bx
    call px_layout
    jc .out
    mov bx, px_hdrop
    cmp byte [px_hdrop + OS88UI_DR_OPEN], 0
    je .btn
    call os88ui_drdrag
    jmp short .out
.btn:
    call os88ui_armed
    or ax, ax
    jz .pan
    mov bx, px_btns
    call os88ui_btndrag
    jmp short .out
.pan:
    mov al, [px_press]
    cmp al, PX_PR_HAND
    je .tick
    cmp al, PX_PR_NAV
    jne .out
.tick:
    call OSAPI_GET_TICKS            ; one move a tick at most
    cmp ax, [px_htick]
    je .out
    mov [px_htick], ax
    call px_dragmove
.out:
    pop bx
    pop ax
    ret

; px_dragmove - CX, DX = the pointer during a Hand or Navigator drag
px_dragmove:
    cmp byte [px_press], PX_PR_NAV
    jne .hand
    jmp px_navpan
.hand:
    push ax
    push bx
    mov ax, cx
    sub ax, [px_hx0]
    add ax, [px_hox]
    mov bx, dx
    sub bx, [px_hy0]
    add bx, [px_hoy]
    call px_panto
    pop bx
    pop ax
    ret

; W_ONMOUSEUP: a button fires here, a pick is made, a drag ends where the
; pointer is, or the region pressed acts
px_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    call px_layout
    jnc .lay
    jmp .out
.lay:
    cmp byte [px_hdrop + OS88UI_DR_OPEN], 0
    je .btn
    mov bx, px_hdrop
    call os88ui_drup
    call px_dropdone
    jmp .out
.btn:
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
    cmp al, PX_PR_HAND
    je .drag
    cmp al, PX_PR_NAV
    jne .cv
.drag:
    mov [px_press], al              ; (px_dragmove reads it)
    call px_dragmove
    mov byte [px_press], PX_PR_NONE
    jmp short .out
.cv:
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
    cmp al, PX_PR_PANEL
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
; and fires on geometry alone - so here the fact a grey states is said in a
; toast (SPEC.md 106.12), and nothing else happens
px_bfire:
    push ax
    push bx
    push si
    mov si, ax
    shl si, 1
    test word [px_bflags + si], OS88UI_DIS
    jz .live
    test word [px_bkind + si], PX_BK_LATER
    mov si, px_s_later              ; why it is grey (mov keeps the flags)
    jnz .why
    mov si, px_s_nopico
.why:
    call px_toast
    jmp .out
.live:
    cmp ax, PX_B_OPEN
    jne .z
    cmp byte [px_busy], 1           ; Stop while a picture decodes
    jne .op
    call px_cancel
    jmp .out
.op:
    call px_cmd_open
    jmp .out
.z:
    cmp ax, PX_B_ZIN
    je .zin
    cmp ax, PX_B_NZIN
    jne .z1
.zin:
    mov al, 1
    call px_zstep
    jmp .out
.z1:
    cmp ax, PX_B_ZOUT
    je .zout
    cmp ax, PX_B_NZOUT
    jne .z2
.zout:
    xor al, al
    call px_zstep
    jmp .out
.z2:
    cmp ax, PX_B_FIT
    je .fit
    cmp ax, PX_B_NFIT
    jne .z3
.fit:
    xor ax, ax
    xor dx, dx
    call px_zoomto
    jmp short .out
.z3:
    cmp ax, PX_B_ONE
    jne .tool
    xor ax, ax
    mov dx, 1
    call px_zoomto
    jmp short .out
.tool:
    cmp ax, PX_B_T0
    jb .out
    cmp ax, PX_B_T0 + PX_NTOOL
    jae .out
    sub al, PX_B_T0
    call px_settool
.out:
    pop si
    pop bx
    pop ax
    ret

; --- W_ONKEY: AL = ascii, AH = scan, SI = the window ---------------------------
px_onkey:
    push ax
    push bx
    push dx
    mov [px_win], si
    cmp byte [px_hdrop + OS88UI_DR_OPEN], 0
    je .nodrop
    mov bx, px_hdrop                ; an open list: any key takes it down
    call os88ui_drclose
    jnc .k0
    push ax
    mov al, PX_R_ALL
    call px_regpaint
    pop ax
.k0:
    jmp .out
.nodrop:
    call px_carddown                ; any key takes a card down, and does
    jc .k0                          ; nothing else
    cmp ah, KSC_ESC                 ; Esc: stop a picture that is opening
    jne .k1
    call px_cancel
    jmp .out
.k1:
    cmp al, 0x0F                    ; Ctrl+O
    jne .k1r
    call px_cmd_open
    jmp .out
.k1r:
    cmp al, 0x12                    ; Ctrl+R: Revert
    jne .k1i
    call px_revert
    jmp .out
.k1i:
    cmp ah, 0x3B                    ; F1
    je .help
    cmp al, '?'
    jne .k2
.help:
    call px_help
    jmp .out
.k2:
    cmp al, 9                       ; Tab: the next panel, one column
    jne .k3
    cmp byte [px_tier], 0
    jne .k2t
    jmp .out
.k2t:
    call px_nextpanel
    jmp .out
.k3:
    call px_navkey                  ; the arrows, PgUp/PgDn, + - 0 1
    jnc .out
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
    pop dx
    pop bx
    pop ax
    ret

; px_navkey - AL/AH = a key: the view's own (SPEC.md 106.11). CF = 0 it was
; one of them and has been done; CF = 1 not ours. Preserves all
px_navkey:
    push ax
    push bx
    push dx
    cmp ah, KSC_LEFT
    jne .r
    mov ax, 32
    xor bx, bx
    jmp short .pan
.r:
    cmp ah, KSC_RIGHT
    jne .u
    mov ax, -32
    xor bx, bx
    jmp short .pan
.u:
    cmp ah, KSC_UP
    jne .d
    xor ax, ax
    mov bx, 32
    jmp short .pan
.d:
    cmp ah, KSC_DOWN
    jne .pu
    xor ax, ax
    mov bx, -32
    jmp short .pan
.pu:
    cmp ah, 0x49                    ; PgUp: a canvas, less 16 rows
    jne .pd
    xor ax, ax
    mov bx, [px_ch]
    sub bx, 16
    jmp short .pan
.pd:
    cmp ah, 0x51                    ; PgDn
    jne .zi
    xor ax, ax
    mov bx, [px_ch]
    sub bx, 16
    neg bx
.pan:
    call px_panby
    jmp short .yes
.zi:
    cmp al, '+'
    je .zin
    cmp al, '='
    jne .zo
.zin:
    mov al, 1
    call px_zstep
    jmp short .yes
.zo:
    cmp al, '-'
    jne .zf
    xor al, al
    call px_zstep
    jmp short .yes
.zf:
    cmp al, '0'
    jne .z1
    xor ax, ax
    xor dx, dx
    call px_zoomto
    jmp short .yes
.z1:
    cmp al, '1'
    jne .no
    xor ax, ax
    mov dx, 1
    call px_zoomto
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; --- the menu handler: AL = item, AH = menu, SI = the window --------------------
; A greyed item never arrives here
px_cmd:
    push ax
    push dx
    mov [px_win], si
    call px_carddown                ; a card is taken down first (106.2): a
                                    ; command under it would act unseen
    cmp ah, PX_M_FILE
    jne .view
    cmp al, PX_MF_OPEN
    jne .f1
    call px_cmd_open
    jmp .out
.f1:
    cmp al, PX_MF_REVERT
    jne .f2
    call px_revert
    jmp .out
.f2:
    cmp al, PX_MF_INFO
    jne .out1
    call px_infocard
.out1:
    jmp .out
.view:
    cmp ah, PX_M_VIEW
    jne .out1
    cmp al, PX_MV_ZIN
    jne .v0
    mov al, 1
    call px_zstep
    jmp .out
.v0:
    cmp al, PX_MV_ZOUT
    jne .v0b
    xor al, al
    call px_zstep
    jmp .out
.v0b:
    cmp al, PX_MV_FIT
    jne .v0c
    xor ax, ax
    xor dx, dx
    call px_zoomto
    jmp short .out
.v0c:
    cmp al, PX_MV_ACTUAL
    jne .v0d
    xor ax, ax
    mov dx, 1
    call px_zoomto
    jmp short .out
.v0d:
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
    pop dx
    pop ax
    ret

; --- the About handler (OSAPI_ABOUT_SET, SPEC.md 12.7) ----------------------------
px_about:
    push bx
    push si
    cmp byte [px_abon], 0           ; another card up: down first, or its edges
    jne .up                         ; show round the smaller About card
    call px_carddown
.up:
    mov byte [px_helpon], 0
    mov byte [px_infoon], 0
    mov byte [px_abon], 1
    mov bx, [px_win]
    mov si, px_ablines
    call os88ui_about               ; arms its own clip: CF = 1 not visible,
    pop si                          ; and the flag stands for the next paint
    pop bx
    ret

; --- W_ONRESIZE: the box changed under us. Nothing is kept across a paint, so
; there is nothing to re-derive; the full repaint that follows lays out anew
; (the view too: px_layout recomputes it, and Fit follows the new canvas).
; Every record is owed here all the same (speed review F3): a landing on a
; display of another KIND at the same depth - a CGA's half-height pictures
; against a Hercules' - must not leave the records describing the old layout
; if a repaint ever does not follow
px_onresize:
    push ax
    mov al, PX_R_ALL
    call px_owe
    pop ax
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

; --- W_ONWAKE: the launch document's handover (SPEC.md 54.10), and the
; worker's kicks (SPEC.md 106.9). NO LOCK HERE: the pump reads the disk first,
; then the lock is taken for the drawing
px_onwake:
    push ax
    push bx
    push dx
    push si
    push di
    mov [px_win], si
    cmp byte [px_argpend], 0
    je .pump
    mov byte [px_argpend], 0
    mov dx, [px_argdir]             ; the DOCUMENT's folder, not ours (54.8:
    mov bl, [px_argvol]             ; the kernel stood in the program's)
    call OSAPI_FILE_GOTO
    jnc .there
    call OSAPI_GFX_LOCK             ; gone (a disk swapped): said, from inside
    mov si, px_s_notfile            ; the lock, where a toast reaches the
    call px_toast                   ; glass (54.10)
    call OSAPI_GFX_UNLOCK
    jmp short .pump
.there:
    mov si, px_argname
    mov di, px_oname
    call px_strcpy
    call OSAPI_GFX_LOCK
    call px_open
    call OSAPI_GFX_UNLOCK
.pump:
    cmp byte [px_busy], 0
    je .out
    cmp byte [px_busy], 1
    jne .job
    call px_pumpfill                ; the disk, with no lock held
.job:
    call OSAPI_GFX_LOCK
    cmp byte [px_job], JOB_NONE
    jne .prog
    call px_finish                  ; the worker has answered
    jmp short .unl
.prog:
    cmp byte [px_busy], 1
    jne .unl
    call px_progpaint               ; the new rows, the progress
.unl:
    call OSAPI_GFX_UNLOCK
.out:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; px_progpaint - while a picture decodes: the rows that came, and the
; name field's percentage when it moved (at most once a tick). Lock held
px_progpaint:
    push ax
    push bx
    push dx
    call px_progfield
    cmp byte [px_abon], 0           ; a card is up: the rows wait for it
    jne .out
    cmp byte [px_helpon], 0
    jne .out
    cmp byte [px_infoon], 0
    jne .out
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call px_layout                  ; ONE layout for both
    jc .clr
    call px_paintrows
    cmp byte [px_sdirty], 0
    je .clr
    call px_sflush
.clr:
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop dx
    pop bx
    pop ax
    ret

; px_progfield - the name field says "Opening 42%" while a picture decodes:
; source rows emitted over the source's rows. Preserves all
px_progfield:
    cmp byte [px_busy], 1           ; (the end of a decode: the name again,
    je .on                          ; which px_compose has set)
    ret
.on:
    push ax
    push bx
    push dx
    push si
    push di
    mov ax, [px_srcdone]
    mov bx, 100
    mul bx
    mov bx, [px_cur + PXR_SH]
    or bx, bx
    jz .out
    div bx
    cmp ax, 100
    jbe .p
    mov ax, 99
.p:
    mov si, px_s_opening
    mov di, px_cline
    call px_strcpy
    xor dx, dx
    call px_u32n
    mov si, px_s_pct
    call px_strcat
    mov bx, PX_SF_NAME
    mov si, px_cline
    call px_sval
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
    cmp byte [px_busy], 0
    je .go
    mov si, px_s_busy
    call px_toast
    jmp short .out
.go:
    mov al, 0
    mov bx, [px_win]
    mov di, px_dlgdone
    xor si, si
    call OSAPI_FILE_DLG             ; CF = 1: one is up already
.out:
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
    mov di, px_oname                ; the kernel's buffer, copied out FIRST
    mov cx, 12
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [px_oname + 12], 0
    pop si
    push ds
    pop es                          ; ES = ours again
    cmp byte [px_oname], 0
    je .out
    call px_open                    ; pxpump.inc
.out:
    ret

; px_revert - File > Revert (Ctrl+R): the shown picture read again from its
; file, in its folder. Lock held
px_revert:
    push bx
    push dx
    push si
    push di
    cmp byte [px_cur + PXR_FHAVE], 0
    je .out
    mov dx, [px_cur + PXR_DIR]
    mov bl, [px_cur + PXR_VOL]
    call OSAPI_FILE_GOTO
    jc .out
    mov si, px_cur + PXR_NAME
    mov di, px_oname
    call px_strcpy
    call px_open
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; px_infocard - File > Image Info...: Image Info's lines as a card, for the
; compact layout where the panel may not be the one showing
px_infocard:
    push bx
    push si
    push di
    push cx
    xor di, di                      ; the card's lines: each one composed
    mov bx, px_infotab              ; into its own slot of the help buffer
.l:
    call px_infoline                ; px_cline = line DI
    mov ax, di
    mov cx, PX_IVSZ + 8
    mul cx
    add ax, px_helpbuf
    mov [bx], ax
    push di
    mov di, ax
    mov si, px_cline
    call px_strcpy
    pop di
    add bx, 2
    inc di
    cmp di, PX_INFON
    jb .l
    mov word [bx], 0
    mov byte [px_abon], 0
    mov byte [px_helpon], 0
    mov byte [px_infoon], 1
    mov bx, [px_win]
    mov si, px_infotab
    call os88ui_about
    pop cx
    pop di
    pop si
    pop bx
    ret

; px_shown - after an open, or a refusal: the window says what it now knows.
; Lock held
px_shown:
    push ax
    push bx
    call px_flags
    mov bx, [px_win]                ; "PiXEL - NAME.EXT" in the title bar: a
    mov ax, px_title                ; strip, not a repaint (SPEC.md 11.92)
    call OSAPI_WM_TITLE
    mov al, PX_R_TB | PX_R_CANVAS | PX_R_PANELS | PX_R_FS | PX_R_STATUS
    call px_regdraw                 ; (the tool column is the same column)
    pop bx
    pop ax
    ret

; px_settool - AL = a tool: latch it; the walk draws the two buttons that
; change
px_settool:
    push ax
    push si
    cmp al, PX_NTOOL
    jae .out
    cmp al, [px_tool]
    je .out
    mov [px_tool], al
    call px_flags
    call px_update                  ; the two buttons whose flags moved
.out:
    pop si
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
    call px_regpaint
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
    mov al, PX_R_CANVAS | PX_R_PANELS
    call px_regpaint                ; the canvas takes the column, or gives it
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
    call px_flags
    mov al, PX_R_TOOLS | PX_R_CANVAS | PX_R_PANELS | PX_R_FS
    call px_regpaint                ; ...which lays out, and relabels
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
    mov byte [px_pfail], 0          ; from here an answer is the PART's
    call far [px_pfar]              ; DS = ours, CS = the part
    jmp short .out
.badp:
    pop es
    mov ax, PXE_BADPART
    jmp short .own
.nopart:
    mov ax, PXE_PART
.own:
    mov byte [px_pfail], 1          ; ...and these are px_pcall's own
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
    jnc .moved                      ; home is gone (PiXEL's disk swapped
    mov si, px_s_diskback           ; out): no fetch at all, rather than a
    call px_toast                   ; part read out of whatever PIXEL.O88
    stc                             ; stands in this folder (wave-1 review
    ret                             ; MIN-2)
.moved:
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

; px_wcall - THE WORKER'S far call (SPEC.md 106.18): vector BL of part AL,
; which must be here - the UI task fetched it for HEAD and keeps it while a
; decode runs - so nothing is fetched and op_seg is a lookup. Its own far
; pointer, because the UI task may be in px_pcall (F1) at the same time.
; out the vector's CF/AX, or CF = 1 AX = PXD_DATA when the part is not
; here. BX, DX, SI preserved
px_wcall:
    push bx
    push dx
    push si
    push es
    mov dl, bl
    call op_seg
    or ax, ax
    jz .no
    mov es, ax
    mov ax, cs
    mov [es:PXP_PKG], ax            ; stamped before EVERY call (106.5)
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov ax, [es:PXP_VEC + bx]
    mov [px_wfar], ax
    mov [px_wfar + 2], es
    pop es
    call far [px_wfar]
    jmp short .out
.no:
    pop es
    mov ax, PXD_DATA
    stc
.out:
    pop si
    pop dx
    pop bx
    ret

; px_fpart - AL = a PXF_*: AL = the decoder part that reads it, 0 none.
; Preserves all but AL
px_fpart:
    cmp al, PXF_GIF
    jne .p
    mov al, PXPART_GIF
    ret
.p:
    cmp al, PXF_PNG
    jne .j
    mov al, PXPART_PNG
    ret
.j:
    cmp al, PXF_JPEG
    jne .s
    mov al, PXPART_JPEG
    ret
.s:
    cmp al, PXF_BMP                 ; BMP, PCX (4, 5), TGA, PIX, PNM (7-9)
    je .simp
    cmp al, PXF_PCX
    je .simp
    cmp al, PXF_TGA
    jb .n
    cmp al, PXF_PNM
    ja .n
.simp:
    mov al, PXPART_SIMP
    ret
.n:
    xor al, al
    ret

; px_kneed - AL = the decoder part an open needs, 0 none: one decoder part
; at a time (SPEC.md 106.18), so any OTHER held one is dropped before the
; new one is fetched. UI task. Preserves all
px_kneed:
    push ax
    mov ah, [px_kheld]
    cmp ah, al
    je .out
    or ah, ah
    jz .set
    push ax
    mov al, ah
    call px_pdrop
    pop ax
.set:
    mov [px_kheld], al
.out:
    pop ax
    ret

; px_kkeep - after a refusal or a cancel: the held decoder part stays only
; while the picture shown is of its kind. Preserves all
px_kkeep:
    push ax
    xor al, al
    cmp byte [px_cur + PXR_HAVE], 0
    je .k
    mov al, [px_cur + PXR_FMT]
    call px_fpart
.k:
    call px_kneed
    pop ax
    ret

; =============================================================================
; WHAT A FILE IS, WITHOUT DECODING IT (SPEC.md 106.6)
; =============================================================================

; px_examine - [px_fname], in the folder the instance is standing in: walk
; the folder (its pictures, sorted, and this one's place and size), read its
; HEAD (SPEC.md 106.9) - or, packed on the disk, the whole file expanded -
; and name the format by its bytes. CF = 1 AX = PXD_NOFILE, PXD_READ,
; PXD_BIG or PXD_MEM; on any of them nothing is held
px_examine:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    xor ax, ax
    mov [px_fsize], ax
    mov [px_fsize + 2], ax
    mov [px_fw], ax
    mov [px_fh], ax
    mov [px_ffmt], al
    mov [px_fbits], al
    mov [px_fprog], al
    mov [px_cur + PXR_PACK], al
    mov [px_cur + PXR_SCL], al
    call px_walk                    ; CF = 1: the name is not in this folder
    mov ax, PXD_NOFILE
    jc .out
    mov si, px_fname                ; the format by its extension first...
    call px_extfmt
    mov [px_ffmt], al
    call px_head                    ; CF = 1 AX = why
    jc .out
    mov ax, [px_hlen]
    mov [px_snlen], ax
    mov es, [px_hseg]
    call px_sniffbuf                ; ...then by its bytes, which win
    mov byte [px_fhave], 1
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; px_walk - OSAPI_FILE_FIND over the folder: [px_fcount] pictures PiXEL
; reads, [px_fidx] this one's place among them sorted by name (1-based),
; [px_fsize] and [px_fcomp]; and the first PX_NAMES of them, sorted, in
; [px_names] with this one at [px_ncur] (the filmstrip's cards). CF = 1 when
; [px_fname] is not one of the files listed
px_walk:
    mov word [px_fcount], 0
    mov word [px_fidx], 1
    mov word [px_nnames], 0
    mov word [px_ncur], 0xFFFF
    mov byte [px_ffound], 0
    mov byte [px_fcomp], 0
    xor cx, cx
.next:
    mov di, px_find
    call OSAPI_FILE_FIND            ; CX = the next ordinal
    jc .end
    cmp byte [px_find + 14], OSAPI_FT_DIR
    jae .next                       ; a folder, or '..'
    mov si, px_find                 ; OURS, whatever its extension: the bytes
    mov di, px_fname                ; name the format (106.6), so a misnamed
    call px_strcmp                  ; picture still opens (wave-1 review
    jne .pic                        ; MAJ-2)
    mov byte [px_ffound], 1
    mov ax, [px_find + 18]          ; the size, expanded
    mov [px_fsize], ax
    mov ax, [px_find + 20]
    mov [px_fsize + 2], ax
    mov al, [px_find + 22]          ; bit 0: packed on the disk
    and al, 1
    mov [px_fcomp], al
.pic:
    mov si, px_find
    call px_extfmt
    or al, al
    jz .next                        ; not a picture PiXEL names: not counted
    inc word [px_fcount]
    cmp word [px_nnames], PX_NAMES  ; kept for the cards
    jae .cmp
    push cx
    mov ax, [px_nnames]
    mov di, 13
    mul di
    add ax, px_names
    mov di, ax
    mov si, px_find
    mov cx, 13
    cld
    rep movsb
    mov byte [di - 1], 0
    inc word [px_nnames]
    pop cx
.cmp:
    mov si, px_find
    mov di, px_fname
    call px_strcmp                  ; CF = 1: listed < ours
    jae .next
    inc word [px_fidx]
    jmp short .next
.end:
    call px_sortnames
    call px_namesum                 ; what the filmstrip's cards are made of
    cmp byte [px_ffound], 0
    jne .yes
    stc
    ret
.yes:
    clc
    ret

; px_namesum - [px_nsum] = a sum over the folder's sorted names: the
; filmstrip's cards are drawn again when it moves (SPEC.md 106.15), and only
; then - a Revert walks the same folder to the same sum. Preserves all
px_namesum:
    push ax
    push cx
    push si
    mov ax, [px_nnames]
    mov cx, 13
    mul cx
    mov cx, ax
    mov ax, [px_nnames]
    mov si, px_names
    jcxz .out
.s:
    rol ax, 1
    add al, [si]
    adc ah, 0
    inc si
    loop .s
.out:
    mov [px_nsum], ax
    pop si
    pop cx
    pop ax
    ret

; px_sortnames - [px_names] in name order (insertion: there are at most
; PX_NAMES), and [px_ncur] where [px_fname] is among them. Preserves all
px_sortnames:
    mov byte [px_pvalid], 0         ; (its swap is px_tbuf: px_plan's bytes)
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, 1                       ; BX = i
.i:
    cmp bx, [px_nnames]
    jae .find
    mov cx, bx                      ; CX = j
.j:
    jcxz .in
    mov ax, cx
    mov di, 13
    mul di
    add ax, px_names
    mov di, ax                      ; DI = names[j]
    lea si, [di - 13]               ; SI = names[j - 1]
    call px_strcmp                  ; CF = 1: in order
    jbe .in
    push cx                         ; swap the two through px_tbuf
    push si
    push di
    push es
    push ds
    pop es
    mov cx, 13
    mov di, px_tbuf
    cld
    rep movsb                       ; tbuf = names[j - 1]
    pop es
    pop di
    pop si
    push si
    push di
    push es
    push ds
    pop es
    xchg si, di
    mov cx, 13
    rep movsb                       ; names[j - 1] = names[j]
    pop es
    pop di
    pop si
    push si
    push di
    push es
    push ds
    pop es
    mov si, px_tbuf
    mov cx, 13
    rep movsb                       ; names[j] = tbuf
    pop es
    pop di
    pop si
    pop cx
    dec cx
    jmp short .j
.in:
    inc bx
    jmp short .i
.find:
    xor bx, bx
    mov si, px_names
.f:
    cmp bx, [px_nnames]
    jae .out
    mov di, px_fname
    call px_strcmp
    jne .fn
    mov [px_ncur], bx
    jmp short .out
.fn:
    add si, 13
    inc bx
    jmp short .f
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; px_head - THE HEAD (SPEC.md 106.9): the file's first 2 KB or first
; cluster, whichever is more, by OSAPI_FILE_READ_AT into a DMA-safe claim
; held until the header is parsed; or, for a file packed on the disk
; (SPEC.md 20.14), the whole file expanded by OSAPI_FILE_READ into a claim of
; its own, which is then also the ring. [px_hlen] = what a header may be
; parsed from (at most PX_HEADMAX). CF = 1 AX = PXD_READ, PXD_BIG or PXD_MEM
px_head:
    cmp byte [px_fcomp], 0
    jne .flat
    call px_clbytes                 ; AX = the cluster
    mov cx, ax
    cmp ax, PX_HEADMAX
    jae .h
    mov ax, PX_HEADMAX
.h:
    mov [px_tcl], ax                ; (the capacity)
    add cx, ax                      ; a JPEG's NEXT head (SPEC.md 106.19) is
    mov [px_hcap], cx               ; read from a cluster: a cluster more
    mov ax, cx
    add ax, 1023
    mov cl, 10
    shr ax, cl                      ; KB
    call px_dmaclaim
    jnc .hc
    mov ax, PXD_MEM
    ret
.hc:
    mov [px_hraw], bx
    mov [px_hseg], dx
    push es
    mov es, dx
    xor bx, bx
    mov cx, [px_tcl]
    xor ax, ax
    xor dx, dx
    mov si, px_fname
    call OSAPI_FILE_READ_AT         ; DX:AX = bytes delivered
    pop es
    jc .rd
    or dx, dx
    jnz .full
    cmp ax, PX_HEADMAX
    jbe .len
.full:
    mov ax, PX_HEADMAX
.len:
    mov [px_hlen], ax
    clc
    ret
.rd:
    call px_headfree
    mov ax, PXD_READ
    stc
    ret
.flat:                              ; packed: the whole file, expanded
    mov ax, [px_fsize]
    mov dx, [px_fsize + 2]
    add ax, 1023
    adc dx, 0
    mov cx, 10
.kb:
    shr dx, 1
    rcr ax, 1
    loop .kb
    or dx, dx
    jnz .big
    cmp ax, 0xFFFF - 2
    jae .big
    push ax
    add ax, 2                       ; a KB for the alignment, and spare
    call OSAPI_MEM_CLAIM
    pop ax
    jc .big
    mov [px_flatraw], dx
    add dx, 31
    and dx, 0xFFE0
    mov [px_flatseg], dx
    push es
    mov es, dx
    xor bx, bx
    mov cx, 10                      ; DX:CX = the capacity in bytes
    xor dx, dx
.cap:
    shl ax, 1
    rcl dx, 1
    loop .cap
    mov cx, ax
    mov si, px_fname
    call OSAPI_FILE_READ            ; expands (SPEC.md 20.14.3)
    pop es
    jc .frd
    mov [px_flatlen], ax
    mov [px_flatlen + 2], dx
    mov byte [px_rflat], 1
    mov bx, [px_flatseg]
    mov [px_hseg], bx
    mov word [px_hraw], 0
    or dx, dx
    jnz .ffull
    cmp ax, PX_HEADMAX
    jbe .flen
.ffull:
    mov ax, PX_HEADMAX
.flen:
    mov [px_hlen], ax
    clc
    ret
.frd:
    call px_relflat
    mov ax, PXD_READ
    stc
    ret
.big:
    mov ax, PXD_BIG
    stc
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
    mov ah, 4
    cmp bl, 6
    je .pbits
    mov ah, 2
    cmp bl, 4
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
    sub cx, 10                      ; THE LIMIT, once: a marker and the frame
    jbe .out                        ; header's fields must start below it, so
    mov si, 2                       ; no read can pass the bytes read - and
.m:                                 ; SI is compared, never SI + 10, which a
    cmp si, cx                      ; lying length could wrap past 64K
    jae .out                        ; (wave-1 review MAJ-1)
    cmp byte [es:si], 0xFF
    jne .out
    mov al, [es:si + 1]
    cmp al, 0xFF                    ; fill bytes
    jne .mk
    inc si
    jz .out
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
    jc .out
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
    ; 3: the format (a progressive JPEG says so on the Packing line: the
    ; value column is fourteen characters, and "JPEG, progressive" was cut)
    mov al, [px_ffmt]
    call px_fmtname                 ; SI = its name
    mov bx, 3
    call px_ivset
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
    cmp byte [px_cur + PXR_SCL], 0  ; shown smaller: "640x480 at 1/4", the
    je .nodim                       ; compact form, or it would not fit the
    mov si, px_cvdims               ; fourteen characters
    mov di, px_cline
    call px_strcpy
    mov si, px_s_at
    mov di, px_cline
    call px_strcat
    mov cl, [px_cur + PXR_SCL]
    mov ax, 1
    shl ax, cl
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
    ; 6: the packing, 7: the palette - what a header parse found
    mov si, px_s_dash
    mov di, px_s_dash
    call px_haspic
    jc .nopk
    mov bl, [px_cur + PXR_PACK]
    xor bh, bh
    shl bx, 1
    mov si, [px_packnames + bx]
    mov al, [px_cur + PXR_PMODE]
    mov di, px_s_pcube
    cmp al, PM_CUBE
    je .nopk
    mov di, px_s_pgrey
    cmp al, PM_GREY
    je .nopk
    push si                         ; "Own, 256"
    mov si, px_s_pown
    mov di, px_cline
    call px_strcpy
    mov ax, [px_cur + PXR_NPAL]
    xor dx, dx
    call px_u32n
    pop si
    mov di, px_cline
.nopk:
    mov bx, 6
    call px_ivset
    mov si, di
    mov bx, 7
    call px_ivset
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
    ; the filmstrip: "13 pictures in this folder"
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
; a control that needs a picture is grey without one, and one a later wave
; owns is grey always (SPEC.md 106.12 - a press on either says which in a
; toast); the latched tool; each box's -, + or >; and the menus' items that
; follow a picture. Preserves all
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
    test cx, PX_BK_LATER
    jnz .grey
    test cx, PX_BK_PIC
    jz .nd
    call px_haspic
    jnc .nd
.grey:
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
    cmp byte [px_col], 0            ; the colour face paints every button
    je .ml                          ; itself (SPEC.md 13.8.10, 106.16)
    xor bx, bx
.own:
    or word [px_bflags + bx], OS88UI_OWN
    add bx, 2
    cmp bx, PX_NB * 2
    jb .own
.ml:
    call px_menulive
    pop si
    pop cx
    pop bx
    pop ax
    ret

; px_menulive - the menu items that follow the picture: live (the string
; past its MENU_DIS) or grey (the string with it). The set is re-registered
; only when an item changed. Preserves all
px_menulive:
    push ax
    push bx
    push cx
    push dx
    push si
    xor cx, cx                      ; CX = something changed
    xor dx, dx                      ; DX = 1: live
    call px_haspic
    jc .g
    inc dx
.g:
    mov si, px_mlive
.l:
    mov bx, [si]                    ; the item's slot in its menu
    or bx, bx
    jz .done
    mov ax, [si + 2]                ; its string, MENU_DIS first
    add ax, dx
    cmp [bx], ax
    je .n
    mov [bx], ax
    inc cx
.n:
    add si, 4
    jmp short .l
.done:
    jcxz .out
    call px_menuset
.out:
    pop si
    pop dx
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
%include "pxmaster.inc"             ; the image model (SPEC.md 106.8)
%include "pxpump.inc"               ; the worker and its file pump (106.9)
%include "pxsimple.inc"             ; BMP, PCX, TGA, PNM, PIX (106.10)
%include "pxview.inc"               ; the renderer, Navigator, Histogram (106.11)
%include "os88rseq.inc"             ; READ_SEQ behind READ_AT's registers

; px_zfield - the status bar's zoom: "100%", or "Fit 47%" (SPEC.md 106.12).
; Marked for a redraw only when it changed. Preserves all
px_zfield:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, px_s_dash
    call px_haspic
    jc .set
    mov ax, [px_zcur]               ; (Z x 100 + 1/2) >> 16
    mov dx, [px_zcur + 2]
    mov cx, 100
    call px_mulx
    add ax, 0x8000
    adc dx, 0
    mov di, px_cline
    mov byte [di], 0
    cmp byte [px_zfit], 0
    je .n
    mov si, px_s_fitsp
    call px_strcpy
.n:
    mov ax, dx
    xor dx, dx
    call px_u32n
    mov si, px_s_pct
    call px_strcat
    mov si, px_cline
.set:
    mov bx, PX_SF_ZOOM
    call px_sval
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
            db CWHITE                   ; a filmstrip card's inside
px_pal1:    db CWHITE, CWHITE, CBLACK, CWHITE, CWHITE, CBLACK, CBLACK, CBLACK
            db CWHITE

; --- per display kind: VGA, Hercules (and EGA), CGA ----------------------------
px_k_thumb: dw 60, 40, 25           ; a filmstrip card's box's rows: 4:3 for
                                    ; its 80 at the pixel's own aspect
px_k_nav:   dw 100, 64, 48          ; the Navigator's body: the rows its
                                    ; title strip had, given to the picture
                                    ; (SPEC.md 106.15)
px_k_hist:  dw 84, 56, 58           ; ...the Histogram's: to the graph (and
                                    ; on a CGA its four lines whole)
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
    dw px_s_lt, px_s_gt
    dw px_s_lt, px_s_gt
%if ($ - px_lab_full) != PX_NB * 2
  %error "px_lab_full has a label per button"
%endif

; --- the colour face's labels: every entry our painter (OS88UI_OWN), which
; finds the picture and the caption by the button's index ----------------------
px_lab_col: times PX_NB dw px_bpaint

; --- the colour face's pictures, PIXEL.GFX (SPEC.md 106.16) -------------------
; tools/pixart.py draws them, and compares these numbers with its own
; (--check-asm): the toolbar's faces are PXA_TBH rows of their own widths
; (PXA_TABLES, PX_B_* order then Stop), PXA_TBS states each - up, pressed,
; greyed - then the tools', PXA_TLW x PXA_TLH, up and pressed
PXA_TBH     equ 30
PXA_TBN     equ 11
PXA_TBS     equ 3
PXA_GAP     equ 2                   ; chrome right of each toolbar button
PXA_STOP    equ 10                  ; Stop's faces, after the ten
PXA_TLW     equ 32                  ; a tool's face: the column's width...
PXA_TLH     equ 24
PXA_TLN     equ 6
PXA_TLS     equ 2
PXA_TLX0    equ 4                   ; ...its button's columns...
PXA_TLX1    equ 27
PXA_TLY0    equ 1                   ; ...and rows
PXA_TLY1    equ 22
PXA_TLFACE  equ 4 * PXA_TLH * PXA_TLW / 8
PXA_MINFREE equ 256                 ; KB free beside the pictures, or no
                                    ; pictures (a picture's master is sized
                                    ; from the heap: SPEC.md 106.8)
%macro PXA_TABLES 1-*
pxa_w:
  %rep %0
    db %1
    %rotate 1
  %endrep
pxa_off:
  %assign PXA_O 0
  %rep %0
    dw PXA_O
    %assign PXA_O PXA_O + PXA_TBS * 4 * PXA_TBH * (%1 / 8)
    %rotate 1
  %endrep
PXA_TLOFF   equ PXA_O
%endmacro
    PXA_TABLES 40, 40, 40, 40, 56, 64, 40, 40, 48, 64, 40
PXA_SIZE    equ PXA_TLOFF + PXA_TLN * PXA_TLS * PXA_TLFACE
%if PXA_SIZE != 27648
  %error "PIXEL.GFX is 27,648 bytes (tools/pixart.py)"
%endif
pxa_name:   db 'PIXEL.GFX', 0

; --- each button's kind: OS88UI_IMG for a picture, PX_BK_PIC when it needs a
; picture and is greyed without one (px_flags) ---------------------------------
PX_BK_PIC   equ 0x8000              ; grey without a picture
PX_BK_LATER equ 0x4000              ; grey in this build: a later wave's
px_bkind:
    dw OS88UI_IMG                               ; Open (Stop while opening)
    dw OS88UI_IMG | PX_BK_LATER                 ; Save
    dw OS88UI_IMG | PX_BK_LATER                 ; Prev
    dw OS88UI_IMG | PX_BK_LATER                 ; Next
    dw OS88UI_IMG | PX_BK_PIC                   ; Zoom In
    dw OS88UI_IMG | PX_BK_PIC                   ; Zoom Out
    dw OS88UI_IMG | PX_BK_PIC                   ; Fit
    dw PX_BK_PIC                                ; 1:1
    dw OS88UI_IMG | PX_BK_LATER                 ; Rotate
    dw OS88UI_IMG | PX_BK_LATER                 ; Slideshow
    times PX_NTOOL dw OS88UI_IMG                ; the tools
    dw PX_BK_PIC, PX_BK_PIC, PX_BK_PIC          ; Navigator's +, -, Fit
    dw PX_BK_LATER, PX_BK_LATER                 ; the filmstrip's arrows
    dw PX_BK_LATER, PX_BK_LATER                 ; the status bar's
%if ($ - px_bkind) != PX_NB * 2
  %error "px_bkind has a kind per button"
%endif

px_toolkeys: db 'HZMCER', 0         ; Hand, Zoom, Marquee, Crop, Eyedropper,
                                    ; Rotate - the tool column's order

; --- the panels ------------------------------------------------------------------
px_pbodyp:  dw px_body_nav, px_body_hist, px_body_info
px_hlabs:   dw px_s_hmean, px_s_hsd, px_s_hmin, px_s_hmax
px_hvals:   dw px_hmean, px_hsd, px_hmin, px_hmax
px_s_hmean: db 'Mean    ', 0
px_s_hsd:   db 'Std Dev ', 0
px_s_hmin:  db 'Min     ', 0
px_s_hmax:  db 'Max     ', 0
px_ilabels: dw px_s_ifile, px_s_ifold, px_s_isize, px_s_ifmt, px_s_ipix
            dw px_s_idep, px_s_ipack, px_s_ipal
px_s_ifile: db 'File', 0
px_s_ifold: db 'Folder', 0
px_s_isize: db 'Size', 0
px_s_ifmt:  db 'Format', 0
px_s_ipix:  db 'Pixels', 0
px_s_idep:  db 'Depth', 0
px_s_ipack: db 'Packing', 0
px_s_ipal:  db 'Palette', 0

; the Histogram's channel (OS88UI_DROP, SPEC.md 13.14): the record and its
; items. The rect is the painter's to fill in
px_hdrop:   dw 0, 0, 0, 0
            dw px_hditems
            dw 4
            dw 0
            dw 0
            db 0, 0xFF
            dw 0, 0
            dw 0
            dw 0
px_hditems: dw px_s_dluma, px_s_dred, px_s_dgreen, px_s_dblue
px_s_dluma: db 'Luma', 0
px_s_dred:  db 'Red', 0
px_s_dgreen: db 'Green', 0
px_s_dblue: db 'Blue', 0

; --- the status fields: widths in glyphs, and the order they leave a narrow
; bar in (the name never does) ----------------------------------------------------
px_sfw:     db 12, 9, 4, 8, 10, 13, 12, 10
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
PX_MF_OPEN  equ 0
PX_MF_REVERT equ 2
PX_MF_INFO  equ 8
PX_M_VIEW   equ 4
PX_MV_ZIN   equ 0
PX_MV_ZOUT  equ 1
PX_MV_FIT   equ 2
PX_MV_ACTUAL equ 3
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
px_mi_revert: db MENU_DIS, 'Revert  Ctrl+R', 0
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

; the menu items that follow the picture (px_menulive): each one's slot in
; its menu and its string, which begins MENU_DIS - the slot points at the
; string to grey it and one past it to make it live
px_mlive:
    dw px_mif + 2 * PX_MF_REVERT, px_mi_revert
    dw px_mif + 2 * PX_MF_INFO, px_mi_info
    dw px_miv + 2 * PX_MV_ZIN, px_mi_zin
    dw px_miv + 2 * PX_MV_ZOUT, px_mi_zout
    dw px_miv + 2 * PX_MV_FIT, px_mi_fit
    dw px_miv + 2 * PX_MV_ACTUAL, px_mi_actual
    dw 0

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
px_s_fsnone: db 'Open a picture to see its folder', 0
px_s_fsn:   db ' pictures in this folder', 0
px_s_bytes: db ' bytes', 0
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
px_s_busy:  db 'Still opening a picture', 0
px_s_diskback: db "Put PiXEL's disk back", 0
px_s_later: db 'Not in this build yet', 0
px_s_nopico: db 'No picture open', 0
px_s_opening: db 'Opening ', 0
px_s_pct:   db '%', 0
px_s_fitsp: db 'Fit ', 0
px_s_at:    db ' at 1/', 0
px_s_colon: db ': ', 0
px_s_pown:  db 'Own, ', 0
px_s_pcube: db 'Cube', 0
px_s_pgrey: db 'Grey', 0
px_c_stop:  db 'Stop', 0
px_packnames: dw px_s_pknone, px_s_pkrle, px_s_pkrle8, px_s_pkrle4, px_s_pkbf
              dw px_s_pklzw, px_s_pklzwi, px_s_pkdefl, px_s_pkdefli
              dw px_s_pkjpg, px_s_pkjpgp
%if ($ - px_packnames) != 2 * PK_N
  %error "px_packnames has a name per PK_*"
%endif
px_s_pklzw: db 'LZW', 0
px_s_pklzwi: db 'LZW interlaced', 0 ; (fourteen: the Info value's cells)
px_s_pkdefl: db 'Deflate', 0
px_s_pkdefli: db 'Deflate, Adam7', 0
px_s_pkjpg: db 'Baseline DCT', 0
px_s_pkjpgp: db 'Progressive', 0
px_s_pknone: db 'None', 0
px_s_pkrle: db 'RLE', 0
px_s_pkrle8: db 'RLE8', 0
px_s_pkrle4: db 'RLE4', 0
px_s_pkbf:  db 'Bit fields', 0

; the refusals (SPEC.md 106.10), PXD_* order: "FORMAT: reason", at most 24
px_reasons: dw px_s_empty0, px_s_r1, px_s_r2, px_s_r3, px_s_r4, px_s_r5
            dw px_s_r6, px_s_r7, px_s_r8, px_s_r9, px_s_r10, px_s_r11
            dw px_s_notfile, px_s_notpic
px_s_notpic: db 'Not a picture', 0
px_s_r1:    db 'bad header', 0
px_s_r2:    db 'size not valid', 0
px_s_r3:    db 'depth not read', 0
px_s_r4:    db 'packing not read', 0
px_s_r5:    db 'cut short', 0
px_s_r6:    db 'damaged', 0
px_s_r7:    db 'not enough memory', 0
px_s_r8:    db 'disk read failed', 0
px_s_r9:    db 'right-to-left', 0
px_s_r10:   db 'not read yet', 0
px_s_r11:   db 'too big to unpack', 0
%if PXD_NREASON != 14
  %error "px_reasons has a line per PXD_*"
%endif

%include "pxicons.inc"

%define OS88UI_BIMG                 ; the one-write button body, and pictures
%define OS88UI_BOWN                 ; ...and the colour face's own (13.8.10)
%define OS88UI_ABOUT                ; the About card, and the keyboard card
%define OS88UI_NOGLYPH              ; no check box, no radio
%define OS88UI_DROP                 ; the Histogram's channel
%include "os88ui.inc"

%define GFXE_BAND_W   PX_HBW        ; the Histogram's graph: one band
%define GFXE_BAND_H   PX_HBH
%define GFXE_BAND_BUF px_hband
%define GFXE_BAND
%include "os88gfx.inc"

; --- the part table, and the standard's code after it (SPEC.md 20.12.3) -------
    OS88_PARTS_BEGIN PX_NPARTS
      OS88_PART OP_SEG, OP_COMP | OP_LAZY   ; 0 the keyboard card (106.5)
      OS88_PART OP_SEG, OP_COMP | OP_LAZY   ; 1 GIF (106.18)
      OS88_PART OP_SEG, OP_COMP | OP_LAZY   ; 2 PNG (106.18)
      OS88_PART OP_SEG, OP_COMP | OP_LAZY   ; 3 JPEG (106.19)
      OS88_PART OP_SEG, OP_COMP | OP_LAZY   ; 4 the simple five (106.20)
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
px_oname    equ PXB + 23            ; 13: the name px_open is to open
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
px_col      equ PXB + 54            ; byte: the COLOUR face (SPEC.md 106.16)
px_fsuser   equ PXB + 55            ; byte: 0 the tier's, 1 on, 2 off
px_pnon     equ PXB + 56            ; byte: the panel column is laid out
px_pnoff    equ PXB + 57            ; byte: View > Hide Panels
px_pvis     equ PXB + 58            ; byte: the panels laid out, a bit each
px_geo      equ PXB + 59            ; byte: px_regdraw repaints its regions
px_pcur     equ PXB + 60            ; byte: the compact layout's panel
px_dither   equ PXB + 61            ; byte: 0 ordered, 1 diffusion
px_btnh     equ PXB + 62            ; word
px_sth      equ PXB + 64
px_hsth     equ PXB + 66            ; the Histogram's body
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
px_c_chrome equ PXB + 120           ; 9 bytes: the palette, px_pal4's order
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
px_c_card   equ PXB + 180           ; byte: a filmstrip card's inside
px_rkind    equ PXB + 181           ; byte: OSAPI_WM_DISPLAY's own kind
px_hchan    equ PXB + 182           ; byte: the Histogram's channel
px_zfit     equ PXB + 183           ; byte: the zoom is Fit
                                    ; (PXB + 184..187 free)
px_press    equ PXB + 188           ; byte
px_abon     equ PXB + 189           ; byte
px_helpon   equ PXB + 190           ; byte
px_ppart    equ PXB + 191           ; byte
px_pvec     equ PXB + 192           ; byte
px_pmoved   equ PXB + 193           ; byte
px_pwas     equ PXB + 194           ; word
px_pwvol    equ PXB + 196           ; byte
px_infoon   equ PXB + 197           ; byte: the Image Info card is up
px_pfar     equ PXB + 198           ; dword: the far vector being called
px_pres     equ PXB + 202           ; word: INIT's answer, for the gate
px_pcalls   equ PXB + 204           ; word: far calls made, for the gate
px_clb      equ PXB + 206
px_snlen    equ PXB + 208
px_tyy0     equ PXB + 210           ; the status text's row (px_draw_status)
px_tbon     equ PXB + 212           ; PX_TBN bytes
px_tbcap    equ PXB + 226           ; PX_TBN bytes
px_tbx      equ PXB + 240           ; PX_TBN words
px_find     equ PXB + 266           ; OSAPI_FIND_SZ
px_brects   equ PXB + 290           ; PX_NB * 8
px_bflags   equ px_brects + PX_NB * 8           ; PX_NB words
px_lab_half equ px_bflags + PX_NB * 2           ; PX_NB words
px_sv       equ px_lab_half + PX_NB * 2         ; PX_NSF * PX_SVSZ
px_ival     equ px_sv + PX_NSF * PX_SVSZ        ; PX_INFON * PX_IVSZ
px_line     equ px_ival + PX_INFON * PX_IVSZ    ; PX_LINEMAX + 1
px_cline    equ px_line + PX_LINEMAX + 2
px_cvline   equ px_cline + PX_LINEMAX + 2       ; 48
px_cvdims   equ px_cvline + 48                  ; 16
px_cvcols   equ px_cvdims + 16                  ; 16
px_fsline   equ px_cvcols + 16                  ; 48
px_title    equ px_fsline + 48                  ; 24
px_helptab  equ px_title + 24                   ; (PX_HELPMAX + 1) words
px_helpbuf  equ px_helptab + (PX_HELPMAX + 1) * 2
px_hicons   equ px_helpbuf + PX_HELPSZ          ; PXI_N * PXH_SZ
px_sfxw     equ px_hicons + PXI_N * PXH_SZ  ; PX_NSF words: each field's x
px_digs     equ px_sfxw + PX_NSF * 2            ; 10: px_u32's digits
px_infotab  equ px_digs + 10                    ; (PX_INFON + 1) words: the card
; --- wave 2: the picture (SPEC.md 106.8) -------------------------------------
px_cur      equ px_infotab + (PX_INFON + 1) * 2 ; PXR_SZ: the picture shown
px_prev     equ px_cur + PXR_SZ                 ; PXR_SZ: what a cancel restores
px_pal      equ px_prev + PXR_SZ                ; 768: its palette
px_ppal     equ px_pal + 768                    ; 768: the previous one's
px_spal     equ px_ppal + 768                   ; 768: the file's, as parsed
px_plan     equ px_spal + 768                   ; 768: a PAL palette's plans
px_t1       equ px_plan + 768                   ; 256: 1bpp thresholds
px_tbuf     equ px_plan                         ; 769: a PCX's tail, and the
                                                ; name sort's swap - on the UI
                                                ; task, where no table is ever
                                                ; being built (an open is
                                                ; refused while one is), so it
                                                ; shares the plans' bytes,
                                                ; which only px_tables reads
                                                ; (the histogram's counts and
                                                ; bins: the master's tail,
                                                ; pxmaster.inc, SPEC.md 106.20)
px_names    equ px_t1 + 256                     ; PX_NAMES x 13: the folder
px_pplan    equ px_names + PX_NAMES * 13        ; 768: the previous picture's
                                                ; plans, banked (106.20)
px_rcur     equ px_pplan + 768                  ; FSEQ_SIZE: READ_SEQ's cursor
px_gsum     equ px_rcur + FSEQ_SIZE             ; 4 x PX_HBW: graph columns
px_hband    equ px_gsum + 4 * PX_HBW            ; PX_HBST x PX_HBH: the graph
px_w2       equ px_hband + PX_HBST * PX_HBH     ; the words and bytes below
; the record's fields under their wave-1 names: the shown picture's
px_fname    equ px_cur + PXR_NAME
px_fsize    equ px_cur + PXR_FSIZE
px_fw       equ px_cur + PXR_SW
px_fh       equ px_cur + PXR_SH
px_ffmt     equ px_cur + PXR_FMT
px_fbits    equ px_cur + PXR_BITS
px_fprog    equ px_cur + PXR_PROG
px_fhave    equ px_cur + PXR_FHAVE
px_fcount   equ px_cur + PXR_FCOUNT
px_fidx     equ px_cur + PXR_FIDX
%assign PXV 0
%macro PXVAR 2                      ; name, bytes
%1 equ px_w2 + PXV
%assign PXV PXV + %2
%endmacro
    PXVAR px_ffound, 1
    PXVAR px_fcomp, 1               ; the file is packed on the disk
    PXVAR px_n0, 2                  ; px_div's 48-bit dividend...
    PXVAR px_n1, 2
    PXVAR px_n2, 2
    PXVAR px_dv0, 2                 ; ...and 32-bit divisor
    PXVAR px_dv1, 2
    PXVAR px_tmw, 2                 ; a scale's master, tried
    PXVAR px_tmh, 2
    PXVAR px_tscl, 1
    PXVAR px_tquant, 1              ; the emitter quantises into the cube
    PXVAR px_kscl, 1
    PXVAR px_avl, 2                 ; OSAPI_MEM_AVAIL's two answers
    PXVAR px_avt, 2
    PXVAR px_wrowsz, 2              ; the work claim: the row's bytes
    PXVAR px_wo_acc, 2              ; ...and paragraph offsets
    PXVAR px_wo_lin, 2
    PXVAR px_wo_lut, 2
    PXVAR px_wseg, 2                ; the WORK claim
    PXVAR px_drawsz, 2              ; the decoder's raw row
    PXVAR px_dlinsz, 2              ; a PCX line
    PXVAR px_rcnt, 1                ; rows summed into the block so far
    PXVAR px_srcdone, 2             ; source rows emitted
    PXVAR px_rdone, 2               ; master rows complete
    PXVAR px_wbase, 2               ; the work claim itself (px_wseg is its
                                    ; row's segment, past the tables)
    PXVAR px_qrow, 2                ; the master row the dither is on
    PXVAR px_vkey, 16               ; what the canvas's view was made from
    PXVAR px_vkeyn, 16
    PXVAR px_bgr, 1                 ; the row is B, G, R
    ; the pump (pxpump.inc)
    PXVAR px_nslot, 1
    PXVAR px_chunk, 2
    PXVAR px_ringraw, 2             ; the ring's claim
    PXVAR px_sseg, 4                ; its slots' segments
    PXVAR px_slen, 4                ; ...their bytes
    PXVAR px_sfull, 2               ; ...whether the pump has filled them
    PXVAR px_fslot, 1               ; the pump's next slot
    PXVAR px_wslot, 1               ; the worker's
    PXVAR px_feof, 1
    PXVAR px_rerr, 1
    PXVAR px_rq, 1                  ; the request byte
    PXVAR px_rhave, 1               ; the worker holds a slot
    PXVAR px_rpos, 2                ; the worker's window: where...
    PXVAR px_rend, 2                ; ...its end...
    PXVAR px_rsegc, 2               ; ...its segment...
    PXVAR px_rbase, 4               ; ...its first byte's offset in the file
    PXVAR px_rnxt, 4                ; ...and the next window's
    PXVAR px_fabs, 4                ; the pump's offset
    PXVAR px_fk, 2                  ; an in-memory file's window
    PXVAR px_rflat, 1               ; the file is in memory (a CZ file)
    PXVAR px_flatseg, 2
    PXVAR px_flatraw, 2
    PXVAR px_flatlen, 4
    PXVAR px_hseg, 2                ; the head
    PXVAR px_hraw, 2
    PXVAR px_hlen, 2
    PXVAR px_job, 1                 ; the worker's job, written LAST
    PXVAR px_wres, 1                ; ...and its answer
    PXVAR px_wstage, 1
    PXVAR px_abort, 1
    PXVAR px_busy, 1                ; 1 decoding, 2 rebuilding tables
    PXVAR px_wspawned, 1
    PXVAR px_onworker, 1
    PXVAR px_wtick, 2
    PXVAR px_rfmt, 1                ; the refused file's format
    PXVAR px_tabdirty, 1
    PXVAR px_tabok, 1               ; the view claim's tables are good
    PXVAR px_tdepth, 1              ; ...for this depth
    PXVAR px_hok, 1                 ; the histogram is counted
    PXVAR px_pctlast, 2             ; the progress shown
    PXVAR px_ptick, 2
    ; the decode's parameters (pxsimple.inc)
    PXVAR px_dfmt, 1
    PXVAR px_dtop, 1
    PXVAR px_dmask, 1
    PXVAR px_desz, 1
    PXVAR px_dkind, 1
    PXVAR px_dpsz, 1
    PXVAR px_doff, 4
    PXVAR px_hsz, 2
    PXVAR px_tw, 4
    PXVAR px_th, 4
    PXVAR px_dnpl, 2
    PXVAR px_dbpp, 2
    PXVAR px_dcomp, 4
    PXVAR px_dused, 4
    PXVAR px_dpalo, 2
    PXVAR px_dstride, 2
    PXVAR px_dbpl, 2
    PXVAR px_dmaxv, 2
    PXVAR px_mbo, 3
    PXVAR px_mbs, 3
    PXVAR px_mmx, 3
    PXVAR px_ttail, 4
    PXVAR px_tgot, 2
    PXVAR px_tcl, 2
    PXVAR px_tkpos, 2
    PXVAR px_tokget, 2
    PXVAR px_tokeof, 2
    PXVAR px_ri, 2
    PXVAR px_rx, 2
    PXVAR px_rnib, 1
    PXVAR px_rcnt2, 1
    PXVAR px_rval, 1
    PXVAR px_rraw, 1
    PXVAR px_rpix, 4
    PXVAR px_rtri, 3
    PXVAR px_rsamp, 2
    ; the renderer (pxview.inc)
    PXVAR px_vseg, 2                ; the VIEW claim
    PXVAR px_vdepth, 1
    PXVAR px_vo_c2p, 2
    PXVAR px_vo_row, 2
    PXVAR px_vo_band, 2
    PXVAR px_vo_thm, 2
    PXVAR px_planp, 2
    PXVAR px_pvalid, 1              ; [px_plan] is [px_pal]'s 4bpp plans
    PXVAR px_ppvalid, 1             ; ...and [px_pplan] [px_ppal]'s
    PXVAR px_pdefer, 1              ; px_pumpfill has deferred a fill
    PXVAR px_vcan, VW_SZ            ; the canvas's view
    PXVAR px_vthm, VW_SZ            ; the Navigator's
    PXVAR px_vtry, VW_SZ            ; a zoom, tried
    PXVAR px_vw, 2                  ; px_vmake's box and zoom
    PXVAR px_vh, 2
    PXVAR px_vz, 4
    PXVAR px_z, 4                   ; the user's zoom
    PXVAR px_zcur, 4                ; the zoom shown
    PXVAR px_ox, 2                  ; the pan
    PXVAR px_oy, 2
    PXVAR px_cw, 2                  ; the canvas's size
    PXVAR px_ch, 2
    PXVAR px_cbv, 2                 ; the composer's pass
    PXVAR px_cgrp, 2
    PXVAR px_cbint, 2
    PXVAR px_cbfrac, 2
    PXVAR px_chfrac, 2
    PXVAR px_chint, 2
    PXVAR px_crseg, 2
    PXVAR px_ccnt, 2
    PXVAR px_cdep, 1
    PXVAR px_useP, 1
    PXVAR px_c2pb, 2
    PXVAR px_c2px, 2
    PXVAR px_rx0, 2
    PXVAR px_ry0, 2
    PXVAR px_ryend, 2
    PXVAR px_rw, 2
    PXVAR px_rby, 2
    PXVAR px_rbn, 2
    PXVAR px_qx1, 2                 ; px_rcanvas's rect...
    PXVAR px_qy1, 2
    PXVAR px_qx2, 2
    PXVAR px_qy2, 2
    PXVAR px_ax1, 2                 ; ...and the picture's part of it
    PXVAR px_ay1, 2
    PXVAR px_ax2, 2
    PXVAR px_ay2, 2
    PXVAR px_px1, 2
    PXVAR px_px2, 2
    PXVAR px_ypaint, 2              ; rows painted from the top...
    PXVAR px_ypbot, 2               ; ...from the bottom
    PXVAR px_cspan1, 2              ; the complete rows a render used
    PXVAR px_cspan2, 2
    PXVAR px_pdx, 2                 ; a pan's move
    PXVAR px_pdy, 2
    PXVAR px_pxg, 2
    PXVAR px_hx0, 2                 ; a Hand drag: where it started
    PXVAR px_hy0, 2
    PXVAR px_hox, 2
    PXVAR px_hoy, 2
    PXVAR px_htick, 2
    PXVAR px_nwell, 8               ; the Navigator's well
    PXVAR px_nw1, 2
    PXVAR px_nw2, 2
    PXVAR px_nh1, 2
    PXVAR px_nh2, 2
    PXVAR px_ntx, 2
    PXVAR px_thok, 1                ; the thumbnail's bank: good...
    PXVAR px_thser, 1               ; ...for this picture...
    PXVAR px_thdep, 1               ; ...this depth...
    PXVAR px_thbw, 2                ; ...this box
    PXVAR px_thbh, 2
    PXVAR px_nfon, 1                ; the frame is on the glass
    PXVAR px_nfr, 8                 ; ...there
    PXVAR px_hn, 4                  ; the statistics
    PXVAR px_hs1, 4
    PXVAR px_hmean, 2
    PXVAR px_hsd, 2
    PXVAR px_hmin, 2
    PXVAR px_hmax, 2
    PXVAR px_gx1, 2                 ; the graph
    PXVAR px_gy1, 2
    PXVAR px_gw, 2
    PXVAR px_gh, 2
    PXVAR px_gmax, 4
    PXVAR px_nnames, 2              ; the folder's pictures, sorted
    PXVAR px_ncur, 2                ; ...and the open one among them
    PXVAR px_fcx1, 2                ; the filmstrip's cards
    PXVAR px_fcx2, 2
    PXVAR px_fcl, 2
    PXVAR px_fcn, 2
    PXVAR px_fcs, 2
    PXVAR px_fct, 2
    PXVAR px_fcny, 2
    PXVAR px_fcx, 2
    PXVAR px_fcb, 2
    PXVAR px_fccur, 1
    PXVAR px_fcname, 2
    PXVAR px_fcix, 2
    PXVAR px_fciy2, 2
    PXVAR px_fcpair, 2
    PXVAR px_fcfmt, 2
    PXVAR px_fcmid, 2
    PXVAR px_mcap, 2                ; a test's cap on the largest run, KB
    PXVAR px_lastref, 1             ; the last refusal's number, for a test
    PXVAR px_ndone, 2               ; opens ended, for a test
    PXVAR px_clok, 1                ; the cluster's size, asked this open
    PXVAR px_clsz, 2
    PXVAR px_cvmsg, 32              ; the empty canvas's line: a refusal
    ; the records a command draws through, and the colour face (pxui.inc,
    ; SPEC.md 106.15, 106.16)
    PXVAR px_colwas, 1              ; the face last laid out
    PXVAR px_plast, 2               ; the panel column's last usable row
    PXVAR px_nbx1, 2                ; the Navigator's body's left
    PXVAR px_nwf, 8                 ; ...its well's frame
    PXVAR px_hcol, 2                ; the Histogram's column
    PXVAR px_hwf, 8                 ; ...its well's frame
    PXVAR px_knew, 8                ; a key being made
    PXVAR px_kn, 8                  ; the thumbnail as drawn
    PXVAR px_kg, 8                  ; the graph as drawn
    PXVAR px_kd, 2                  ; the drop-down as drawn
    PXVAR px_kcard, 2 * PX_MAXCARD  ; each card as drawn
    PXVAR px_kfser, 2               ; ...out of the names that summed to this
    PXVAR px_nsum, 2                ; the folder's names' sum
    PXVAR px_fsmany, 1
    PXVAR px_fry1, 2                ; the filmstrip's body
    PXVAR px_fry2, 2
    PXVAR px_bkey, 4 * PX_NB        ; each button's flags and picture as drawn
    PXVAR px_slot, 2                ; px_tband's slot, or 0
    PXVAR px_slots, PX_NSLOT * PX_SLOTSZ
    PXVAR px_capslot, SL_CELLS + PX_LINEMAX
    PXVAR px_udrew, 1               ; draws a walk made (the CGA's grow box)
    PXVAR px_pax, 2                 ; px_bpaint's arguments...
    PXVAR px_pbx, 2
    PXVAR px_pdi, 2
    PXVAR px_bv, 8                  ; ...px_bevel's rect...
    PXVAR px_bf, 8                  ; ...its face...
    PXVAR px_bvpic, 2               ; ...its picture and caption...
    PXVAR px_bvcap, 2
    PXVAR px_bvy, 2                 ; ...and the next row
    PXVAR pxa_seg, 2                ; PIXEL.GFX's claim, or 0
    ; the decoder parts (SPEC.md 106.18)
    PXVAR px_k, PXK_SZ              ; THE CONTEXT, both vectors' DI
    PXVAR px_kheld, 1               ; the decoder part held, 0 none
    PXVAR px_pfail, 1               ; px_pcall's answer was its own
    PXVAR px_wfar, 4                ; px_wcall's far vector
    PXVAR px_dsc, 8                 ; the decoder scratch, paragraphs, at
                                    ; each scale; FFFFh not usable (106.19)
    PXVAR px_hcap, 2                ; the head claim's bytes, a cluster
                                    ; multiple: a JPEG's next head (106.19)
    PXVAR px_sfloor, 1              ; the scale a JPEG may be no finer than
    PXVAR px_zreq, 1                ; a re-decode's scale, FFh none
    PXVAR px_keepv, 1               ; the open keeps the view (a re-decode)
    PXVAR px_kdi, 2                 ; K_COLS: rows complete
    PXVAR px_ccol, 2                ; ...its block's first column
    PXVAR px_cpix, 2                ; ...pixels a row
    PXVAR px_cn, 2                  ; ...rows
    PXVAR px_kcrow, 2               ; ...its first row
    PXVAR px_csrc, 4                ; ...where the block is
    PXVAR px_wo_dec, 2              ; ...its place in the work claim
    PXVAR px_kbx, 2                 ; a service's BX and DX, banked across
    PXVAR px_kdx, 2                 ; px_emit
PX_BSSEND   equ px_w2 + PXV
PX_BSS      equ PX_BSSEND - PXB

PXI_PHOTO   equ (pxi_photo - pxi_open) / PXI_SZ
PXI_STOP    equ (pxi_stop - pxi_open) / PXI_SZ

    OS88_BSS OP_BSS + PX_BSS
    OS88_IMAGE_END
