; Browser part 7: the 1.8 renderer's on-demand half - colours (SPEC.md
; 71.34), fonts (71.35) and the table composer (71.34.2) - ONE part and so
; ONE heap claim. Three parts were three claims, and with the page's own
; arenas the Browser reached a package's eight: the image engine then had
; none, and no picture in a table was ever fetched.
;
; Fetched on the UI task: at the window's start on a colour display (the
; parser on the worker matches colour names through it), else by the first
; layout that wants a tall line or a table. Held for the session.
;
; Three conventions live side by side and each body keeps its own: the
; colour body runs with DS = the primary and reads its tables through CS;
; the font body switches DS to this part's data segment ([cs:BRCL_SELF]),
; because os88type.inc keeps its state in DS; the table body runs with DS =
; the primary, every br_* datum imported from browser.map, and reaches
; primary code far through br_tb_rtab. Nothing is written through CS.
cpu 8086                        ; THE TARGET IS AN 8088 (SPEC.md 1)
bits 16
org 0

%include "os88api.inc"
%include "brdocmarks.inc"
%include "brcolor_const.inc"
%include "brfont_const.inc"
%include "brtable_part_symbols.inc"  ; $(BUILD)/, by -I

    dw brc_f_match                  ; BRCL_MATCH
    dw 0                            ; BRCL_SELF: the first bind writes it
    dw brc_f_theme                  ; BRCL_THEME
    times BRCL_CODE - ($ - $$) db 0 ; the checkpoints (BRCL_CPN..CPTAB)
    dw brf_f_open                   ; BRFN_OPEN
    dw brf_f_adv                    ; BRFN_ADV
    dw brf_f_draw                   ; BRFN_DRAW
    dw brt_f_table                  ; BRTB_ENTRY
%if ($ - $$) != BRTB_ENTRY + 2 || ($ - $$) != BRST_CODE
%error "part 7's entry table does not match brcolor_const.inc / brfont_const.inc"
%endif

; --- brs_f_run - br_strun's row when it has colours (71.34.15): far, from the
; primary, DS = it. In: as br_strun - AX x, BX y, CX width, DX 1, BP = the
; stride ([br_cols]); the row is at DS:br_stylebuf and is scan line
; [br_bmrow] of eight. Out: CF = BLIT1's; every register kept.
;
; A RUN ONCE, EIGHT ROWS TALL. The scan line is banked in the font band; on
; the eighth, each run of equal attribute is one pen and one BLIT1 with DX = 8
; - not one a scan line, which made a bordered table's text row 8 x (2N + 1)
; BLIT1s where an uncoloured row takes 8. The band is idle here: brf_draw
; fills it afresh for every tall slice, and br_paintstyled's eight rows are
; one call with nothing between them.
brs_f_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [cs:BRCL_SELF]          ; ES = our data: the band
    mov al, [br_bmrow]
    mul byte [br_cols]              ; AX = the scan line's place (a byte MUL:
    add ax, ty_bandbuf              ; DX is not written)
    mov di, ax
    mov si, br_stylebuf
    mov cx, [br_cols]
    cld
    rep movsb
    mov al, [br_bmrow]
    xor al, 7                       ; CF = 0: banked, nothing drawn yet
    jnz .out
    mov bx, [br_bmpy]               ; the line's TOP row, eight of them
    mov dl, 8
    xor di, di                      ; DI = a run's first cell
.r:
    cmp di, [br_cols]
    jae .out                        ; (CF = 0)
    mov si, di
    mov al, [br_lattr + si]
.e:
    inc si
    cmp si, [br_cols]
    jae .emit
    cmp al, [br_lattr + si]
    je .e
.emit:                              ; cells DI..SI-1, one attribute
    mov ah, al                      ; br_apen's: AL ink, AH ground
    and al, 0Fh
    mov cl, 4
    shr ah, cl
    call OSAPI_GFX_BLIT1_PEN
    mov cx, si
    sub cx, di
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; its pixels
    mov ax, di
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [br_bmpx]
    push si                         ; (the next run's first cell)
    lea si, [di + ty_bandbuf]
    call OSAPI_GFX_BLIT1
    pop di
    jnc .r
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

brt_f_table:
    call br_table
    retf

%include "brtable.inc"
%include "brcolor_body.inc"
%include "brfont_body.inc"

%if 8 * BR_MAXCOL > TY_BANDSZ
%error "brs_f_run banks eight scan lines of BR_MAXCOL cells in the font band"
%endif
