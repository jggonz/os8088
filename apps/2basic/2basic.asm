; 2BASIC - native 8086 BASIC interpreter for OS8088.
; Numbered BASIC programs, graphics, forms and events, file and resource I/O.
; Loads compatible 2BR2/2BR3 resource packs. Runs inside the OS8088 UI task.
; Uses only the pinned OS8088 format-10 SDK; see docs/DEVELOP.TXT.
; 
%include "os88api.inc"

    OS88_HEADER '2BASIC', bas_entry, 1, OS88_STACK_384
    OS88_ICON16
    dw 0FFFFh
    dw 0FFFFh
    dw 08307h
    dw 0FB7Bh
    dw 0FB7Bh
    dw 08307h
    dw 0BF7Bh
    dw 0BF7Bh
    dw 08307h
    dw 0FFFFh
    dw 0FFFFh
    dw 0FFFFh
    dw 0FFFFh
    dw 0FFFFh
    dw 0FFFFh
    dw 0FFFFh
    dw 00000h
    dw 00000h
    dw 07CF8h
    dw 00484h
    dw 00484h
    dw 07CF8h
    dw 04084h
    dw 04084h
    dw 07CF8h
    dw 00000h
    dw 00000h
    dw 00000h
    dw 00000h
    dw 00000h
    dw 00000h
    dw 00000h
    OS88_ICON16_END

BAS_INPUT_CAP      equ 135            ; line number + 127-char source
BAS_CON_LINES      equ 35
BAS_CON_COLS       equ 64
BAS_PROG_LINES     equ 288            ; indexed packed records
BAS_SRC_CAP        equ 127
BAS_PROG_BYTES     equ 4864           ; packed source arena
BAS_MARGIN         equ 4
BAS_SCRATCH_CAP    equ 160
BAS_IO_CAP         equ 6144           ; maximum ASCII LOAD/SAVE file
BAS_IO_KB          equ BAS_IO_CAP / 1024 ; native os8088 claim: 6 KB
BAS_HELP_KB        equ 13             ; transient HELP.TXT claim, separate from LOAD/SAVE
BAS_HELP_CAP       equ BAS_HELP_KB * 1024
BAS_FILENAME_CAP   equ 13
BAS_PATH_CAP       equ 72  ; transient quoted LOAD path (8.3 components)
BAS_LINEBUF_CAP    equ 136
BAS_GOSUB_MAX      equ 16
BAS_FOR_MAX        equ 8
BAS_FOR_REC_SIZE   equ 8              ; var word, limit, step, FOR line
BAS_GFX_MAX        equ 84
BAS_GFX_REC_SIZE   equ 10             ; type/slot, dest x/y, 4 byte-sized crop fields
BAS_GFX_DELTA_MAX  equ 16             ; fast DRAWBEGIN batch records replayed without full scene erase
BAS_POKE_CAP       equ 3072
BAS_IMAGE_SLOTS    equ 4
BAS_IMAGE_MAX_W    equ 64
BAS_IMAGE_MAX_H    equ 64
BAS_IMAGE_SLOT_BYTES equ 2048           ; 64x64 packed 4bpp
BAS_IMAGE_META_SIZE equ 8               ; loaded,pad,width,height,stride
BAS_RES_SPRITE_W   equ 16
BAS_RES_SPRITE_H   equ 16
BAS_RES_COUNT      equ 99
BAS_RES_ROW_BYTES  equ 2
BAS_RES_ITEM_BYTES equ 32
BAS_RES_FILE_BYTES equ 3176
BAS_RES_CAP        equ BAS_IMAGE_SLOTS * BAS_IMAGE_SLOT_BYTES ; 8192 max resident v3 pack
BAS_GUI_MAX        equ 12
BAS_GUI_REC_SIZE   equ 16
BAS_GUI_TEXT_CAP   equ 24
BAS_FORM_TITLE_CAP equ 32
BAS_DIALOG_TEXT_CAP equ 64
BAS_DIALOG_INPUT_CAP equ 31
BAS_VAR_SLOTS      equ 32
BAS_VAR_NAME_CAP   equ 8
BAS_VAR_REC_SIZE   equ 18             ; APP[8], KEY[8], signed value word
BAS_VAR_HEADER     equ 4
BAS_VAR_BYTES      equ BAS_VAR_HEADER + (BAS_VAR_SLOTS * BAS_VAR_REC_SIZE)
BAS_VAL_TEXT_CAP   equ 16
BAS_ARRAY_ELEMS    equ 512            ; shared signed-word array pool
BAS_ARRAY_NAMES    equ 26             ; A() through Z(), one dimension each
BAS_CON_MIN_COLS  equ 26
BAS_CON_MIN_ROWS  equ 6
BAS_CON_MAX_COLS  equ BAS_CON_COLS - 1
BAS_CON_FRAME_X   equ 12             ; 8 px margins + 4 px frame
BAS_CON_FRAME_Y   equ 28             ; 8 px margins + 20 px title/frame
BAS_FORM_MIN_W    equ 120            ; client pixels
BAS_FORM_MIN_H    equ 72
BAS_FORM_DEF_W    equ 356
BAS_FORM_DEF_H    equ 220
BAS_FORM_FRAME_X  equ 4
BAS_FORM_FRAME_Y  equ 20

GUI_LABEL          equ 1
GUI_BUTTON         equ 2
GUI_CHECKBOX       equ 3
GUI_FRAME          equ 4
GUI_IMAGE          equ 5
GUI_F_VISIBLE      equ 1
GUI_F_ENABLED      equ 2
GUI_TYPE           equ 0
GUI_FLAGS          equ 1
GUI_ID             equ 2
GUI_X              equ 4
GUI_Y              equ 6
GUI_W              equ 8
GUI_H              equ 10
GUI_VALUE          equ 12

DLG_NONE           equ 0
DLG_MESSAGE        equ 1
DLG_INPUT          equ 2

GFX_PSET           equ 1
GFX_LINE           equ 2
GFX_BOX            equ 3
GFX_CIRCLE         equ 4
GFX_BITMAP         equ 5
GFX_FILLBOX        equ 6
GFX_RESOURCE       equ 7

BAS_STATE_READY    equ 0
BAS_STATE_RUNNING  equ 1
BAS_STATE_INPUT    equ 2

BERR_NONE          equ 0
BERR_SYNTAX        equ 1
BERR_DIVZERO       equ 2
BERR_UNDEFLINE     equ 3
BERR_STEPLIMIT     equ 4
BERR_OVERFLOW      equ 5
BERR_PROGRAMFULL   equ 6
BERR_BADPROGRAM    equ 7
BERR_RETURN        equ 8
BERR_GOSUBFULL     equ 9
BERR_FORFULL       equ 10
BERR_NEXT          equ 11
BERR_FORNEXT       equ 12
BERR_GFXFULL       equ 13
BERR_ILLEGAL       equ 14
BERR_SCREEN        equ 15
BERR_BMPFORMAT     equ 17
BERR_NOBMP         equ 18
BERR_NOADLIB       equ 19
BERR_FMREFUSED     equ 20
BERR_FILE          equ 21
BERR_FILENOENT      equ 22
BERR_FILEBIG        equ 23
BERR_DISKFULL       equ 24
BERR_WRITEPROT      equ 25
BERR_BADNAME        equ 26
BERR_BREAK          equ 27
BERR_NOWORKER       equ 28
BERR_PROGMODE       equ 29
BERR_REDO           equ 30
BERR_FORMFULL       equ 31
BERR_NOFORM         equ 32
BERR_BADCONTROL     equ 33
BERR_RESFORMAT      equ 34
BERR_NORES          equ 35
BERR_NOMEM          equ 36
BERR_VARSTORE      equ 37
BERR_VARFULL       equ 38
BERR_MULTIBAS      equ 39   ; folder has more than one .BAS file

; BSS layout. All large blocks are fixed-size and loader-zeroed.
BAS_O_CON_COUNT    equ 0
BAS_O_INPUT_LEN    equ BAS_O_CON_COUNT + 2
BAS_O_INPUT_POS    equ BAS_O_INPUT_LEN + 2
BAS_O_INPUT        equ BAS_O_INPUT_POS + 2
BAS_O_HISTORY_LEN  equ BAS_O_INPUT + BAS_INPUT_CAP + 1
BAS_O_HISTORY      equ BAS_O_HISTORY_LEN + 2
BAS_O_CONSOLE      equ BAS_O_HISTORY + BAS_INPUT_CAP + 1
BAS_O_CON_FG       equ BAS_O_CONSOLE + (BAS_CON_LINES * BAS_CON_COLS)
BAS_O_CON_BG       equ BAS_O_CON_FG + BAS_CON_LINES
BAS_O_PROGRAM      equ BAS_O_CON_BG + BAS_CON_LINES
BAS_O_LINE_INDEX   equ BAS_O_PROGRAM + BAS_PROG_BYTES
BAS_O_PROGRAM_USED equ BAS_O_LINE_INDEX + (BAS_PROG_LINES * 2)
BAS_O_PROGRAM_COUNT equ BAS_O_PROGRAM_USED + 2
BAS_O_SCRATCH      equ BAS_O_PROGRAM_COUNT + 2
BAS_O_FILENAME     equ BAS_O_SCRATCH + BAS_SCRATCH_CAP
BAS_O_IO_LEN       equ BAS_O_FILENAME + BAS_FILENAME_CAP + 1
BAS_O_IO_SEG       equ BAS_O_IO_LEN + 2
BAS_O_VARS         equ BAS_O_IO_SEG + 2
; LOAD/SAVE ASCII staging is a transient BAS_IO_PARAS os8088 memory grant rather
; than resident BSS. LOAD validation/commit and general scratch formatting never
; overlap. Reuse
; the 160-byte scratch block for the 136-byte textual line buffer.
BAS_O_LINEBUF      equ BAS_O_SCRATCH
BAS_O_GOSUB_COUNT  equ BAS_O_VARS + (26 * 2)
BAS_O_GOSUB_STACK  equ BAS_O_GOSUB_COUNT + 2
BAS_O_FOR_COUNT    equ BAS_O_GOSUB_STACK + (BAS_GOSUB_MAX * 2)
BAS_O_FOR_STACK    equ BAS_O_FOR_COUNT + 2
BAS_O_GFX_COUNT    equ BAS_O_FOR_STACK + (BAS_FOR_MAX * BAS_FOR_REC_SIZE)
BAS_O_GFX_LIST     equ BAS_O_GFX_COUNT + 2
BAS_O_FG_COLOR     equ BAS_O_GFX_LIST + (BAS_GFX_MAX * BAS_GFX_REC_SIZE)
BAS_O_BG_COLOR     equ BAS_O_FG_COLOR + 1
BAS_O_PROMPT_FG    equ BAS_O_BG_COLOR + 1
BAS_O_PROMPT_BG    equ BAS_O_PROMPT_FG + 1
BAS_O_SCREEN_MODE  equ BAS_O_PROMPT_BG + 1
BAS_O_TEXT_X       equ BAS_O_SCREEN_MODE + 1
BAS_O_TEXT_Y       equ BAS_O_TEXT_X + 2
BAS_O_INPUT_Y      equ BAS_O_TEXT_Y + 2
BAS_O_VISIBLE      equ BAS_O_INPUT_Y + 2
BAS_O_CONTENT_X    equ BAS_O_VISIBLE + 2
BAS_O_CONTENT_Y    equ BAS_O_CONTENT_X + 2
BAS_O_CONTENT_W    equ BAS_O_CONTENT_Y + 2
BAS_O_CONTENT_H    equ BAS_O_CONTENT_W + 2
BAS_O_BEST_PTR     equ BAS_O_CONTENT_H + 2
BAS_O_LAST_LINE    equ BAS_O_BEST_PTR + 2
BAS_O_CURRENT_LINE equ BAS_O_LAST_LINE + 2
BAS_O_STEPS        equ BAS_O_CURRENT_LINE + 2
BAS_O_EXPR_DEPTH   equ BAS_O_STEPS + 2
BAS_O_ERROR_CODE   equ BAS_O_EXPR_DEPTH + 2
BAS_O_DRAW_X0      equ BAS_O_ERROR_CODE + 2
BAS_O_DRAW_Y0      equ BAS_O_DRAW_X0 + 2
BAS_O_DRAW_X1      equ BAS_O_DRAW_Y0 + 2
BAS_O_DRAW_Y1      equ BAS_O_DRAW_X1 + 2
BAS_O_DRAW_DX      equ BAS_O_DRAW_Y1 + 2
BAS_O_DRAW_DY      equ BAS_O_DRAW_DX + 2
BAS_O_DRAW_SX      equ BAS_O_DRAW_DY + 2
BAS_O_DRAW_SY      equ BAS_O_DRAW_SX + 2
BAS_O_DRAW_ERR     equ BAS_O_DRAW_SY + 2
BAS_O_CIRCLE_X     equ BAS_O_DRAW_ERR + 2
BAS_O_CIRCLE_Y     equ BAS_O_CIRCLE_X + 2
BAS_O_CIRCLE_ERR   equ BAS_O_CIRCLE_Y + 2
BAS_O_CIRCLE_CX    equ BAS_O_CIRCLE_ERR + 2
BAS_O_CIRCLE_CY    equ BAS_O_CIRCLE_CX + 2
BAS_O_POKE_BANK    equ BAS_O_CIRCLE_CY + 2
BAS_O_IMAGE_META   equ BAS_O_POKE_BANK + BAS_POKE_CAP
BAS_O_IMAGE_DATA   equ BAS_O_IMAGE_META + (BAS_IMAGE_SLOTS * BAS_IMAGE_META_SIZE)
BAS_O_BMP_OFFSET   equ BAS_O_IMAGE_DATA + (BAS_IMAGE_SLOTS * BAS_IMAGE_SLOT_BYTES)
BAS_O_BMP_STRIDE   equ BAS_O_BMP_OFFSET + 2
BAS_O_BMP_WIDTH    equ BAS_O_BMP_STRIDE + 2
BAS_O_BMP_HEIGHT   equ BAS_O_BMP_WIDTH + 2
BAS_O_BMP_SLOT     equ BAS_O_BMP_HEIGHT + 2
BAS_O_CAP_TMP      equ BAS_O_BMP_SLOT + 2
BAS_O_WINDOW       equ BAS_O_CAP_TMP + 2
BAS_O_RUN_STATE    equ BAS_O_WINDOW + 2
BAS_O_WORKER_STARTED equ BAS_O_RUN_STATE + 1
BAS_O_EXEC_BUSY    equ BAS_O_WORKER_STARTED + 1
BAS_O_BREAK_REQ    equ BAS_O_EXEC_BUSY + 1
BAS_O_DIRTY        equ BAS_O_BREAK_REQ + 1
BAS_O_LAST_KEY     equ BAS_O_DIRTY + 1
BAS_O_INPUT_VAR    equ BAS_O_LAST_KEY + 2
BAS_O_OUT_ROW      equ BAS_O_INPUT_VAR + 2
BAS_O_OUT_COL      equ BAS_O_OUT_ROW + 2
BAS_O_PRINT_NONL   equ BAS_O_OUT_COL + 2
BAS_O_WORKER_ACTIVE equ BAS_O_PRINT_NONL + 1
BAS_O_FORM_WINDOW  equ BAS_O_WORKER_ACTIVE + 1
BAS_O_DIALOG_WINDOW equ BAS_O_FORM_WINDOW + 2
BAS_O_FORM_COUNT   equ BAS_O_DIALOG_WINDOW + 2
BAS_O_FORM_VISIBLE equ BAS_O_FORM_COUNT + 2
BAS_O_FORM_EVENT   equ BAS_O_FORM_VISIBLE + 1
BAS_O_FORM_TITLE   equ BAS_O_FORM_EVENT + 2
BAS_O_FORM_RECS    equ BAS_O_FORM_TITLE + BAS_FORM_TITLE_CAP
BAS_O_FORM_TEXT    equ BAS_O_FORM_RECS + (BAS_GUI_MAX * BAS_GUI_REC_SIZE)
BAS_O_DIALOG_MODE  equ BAS_O_FORM_TEXT + (BAS_GUI_MAX * BAS_GUI_TEXT_CAP)
BAS_O_DIALOG_RESULT equ BAS_O_DIALOG_MODE + 1
BAS_O_DIALOG_VAR   equ BAS_O_DIALOG_RESULT + 1
BAS_O_DIALOG_INPUT_LEN equ BAS_O_DIALOG_VAR + 2
BAS_O_DIALOG_TEXT  equ BAS_O_DIALOG_INPUT_LEN + 2
BAS_O_DIALOG_INPUT equ BAS_O_DIALOG_TEXT + BAS_DIALOG_TEXT_CAP
BAS_O_INPUT_READY  equ BAS_O_DIALOG_INPUT + BAS_DIALOG_INPUT_CAP + 1
BAS_O_AUTO_MODE    equ BAS_O_INPUT_READY + 1
BAS_O_AUTO_NEXT    equ BAS_O_AUTO_MODE + 1
BAS_O_AUTO_STEP    equ BAS_O_AUTO_NEXT + 2
BAS_O_DRAW_BATCH   equ BAS_O_AUTO_STEP + 2
BAS_O_CONSOLE_MIN  equ BAS_O_DRAW_BATCH + 1
BAS_O_FORM_ORIGIN_X equ BAS_O_CONSOLE_MIN + 1
BAS_O_FORM_ORIGIN_Y equ BAS_O_FORM_ORIGIN_X + 2
BAS_O_FORM_WIDTH   equ BAS_O_FORM_ORIGIN_Y + 2
BAS_O_FORM_HEIGHT  equ BAS_O_FORM_WIDTH + 2
BAS_O_FORM_REQ_W   equ BAS_O_FORM_HEIGHT + 2
BAS_O_FORM_REQ_H   equ BAS_O_FORM_REQ_W + 2
BAS_O_FORM_RESIZE  equ BAS_O_FORM_REQ_H + 2
BAS_O_DIALOG_ORIGIN_X equ BAS_O_FORM_RESIZE + 1
BAS_O_DIALOG_ORIGIN_Y equ BAS_O_DIALOG_ORIGIN_X + 2
BAS_O_VAR_DATA     equ BAS_O_DIALOG_ORIGIN_Y + 2
BAS_O_VAR_BAD      equ BAS_O_VAR_DATA + BAS_VAR_BYTES
BAS_O_VAL_TEXT     equ BAS_O_VAR_BAD + 1
BAS_O_ARRAY_USED   equ BAS_O_VAL_TEXT + BAS_VAL_TEXT_CAP
BAS_O_ARRAY_SIZE   equ BAS_O_ARRAY_USED + 2
BAS_O_ARRAY_BASE   equ BAS_O_ARRAY_SIZE + (BAS_ARRAY_NAMES * 2)
BAS_O_ARRAY_DATA   equ BAS_O_ARRAY_BASE + (BAS_ARRAY_NAMES * 2)
BAS_O_BATCH_FAST   equ BAS_O_ARRAY_DATA + (BAS_ARRAY_ELEMS * 2)
BAS_O_GFX_DELTA_COUNT equ BAS_O_BATCH_FAST + 2
BAS_O_GFX_DELTA_LIST equ BAS_O_GFX_DELTA_COUNT + 2
BAS_O_ICON_RECORD   equ BAS_O_GFX_DELTA_LIST + (BAS_GFX_DELTA_MAX * BAS_GFX_REC_SIZE)
BAS_O_VAR_DIRTY     equ BAS_O_ICON_RECORD + 66
BAS_O_HELP_TOPIC    equ BAS_O_VAR_DIRTY + 1
BAS_O_ROW_DIRTY_FIRST equ BAS_O_HELP_TOPIC + 16
BAS_O_ROW_DIRTY_LAST  equ BAS_O_ROW_DIRTY_FIRST + 2
BAS_O_PROMPT_LAST_COLS equ BAS_O_ROW_DIRTY_LAST + 2
BAS_O_VIEW_FIRST    equ BAS_O_PROMPT_LAST_COLS + 2
BAS_O_VIEW_PENDING  equ BAS_O_VIEW_FIRST + 2
BAS_O_VIEW_NEXT     equ BAS_O_VIEW_PENDING + 1
BAS_O_VIEW_LAST_TICK equ BAS_O_VIEW_NEXT + 2
BAS_O_PATH_BUF         equ BAS_O_VIEW_LAST_TICK + 2
BAS_O_PATH_LEAF        equ BAS_O_PATH_BUF + BAS_PATH_CAP
BAS_O_PATH_NEXT        equ BAS_O_PATH_LEAF + 2
BAS_O_PATH_OLD_CLUSTER equ BAS_O_PATH_NEXT + 2
BAS_O_PATH_OLD_DRIVE   equ BAS_O_PATH_OLD_CLUSTER + 2
BAS_O_PATH_DRIVE       equ BAS_O_PATH_OLD_DRIVE + 1
BAS_O_PATH_PENDING     equ BAS_O_PATH_DRIVE + 1
BAS_O_DIR_NAME         equ BAS_O_PATH_PENDING + 1
BAS_O_DIR_RECORD       equ BAS_O_DIR_NAME + 13
BAS_O_HOME_CLUSTER     equ BAS_O_DIR_RECORD + OSAPI_FIND_SZ
BAS_O_HOME_DRIVE       equ BAS_O_HOME_CLUSTER + 2
BAS_O_HELP_CLUSTER     equ BAS_O_HOME_DRIVE + 1
BAS_O_HELP_DRIVE       equ BAS_O_HELP_CLUSTER + 2
BAS_O_PATH_PREV_FILENAME equ BAS_O_HELP_DRIVE + 1
BAS_O_RES_STAGE    equ BAS_O_PATH_PREV_FILENAME + BAS_FILENAME_CAP + 1
BAS_O_RES_LENGTH   equ BAS_O_RES_STAGE + 2
BAS_O_RES_OFFSET   equ BAS_O_RES_LENGTH + 2
BAS_O_RES_COUNT    equ BAS_O_RES_OFFSET + 2
BAS_O_RES_PRESENT  equ BAS_O_RES_COUNT + 2
BAS_O_RES_NEW_OFF  equ BAS_O_RES_PRESENT + 1
BAS_O_RES_NEW_COUNT equ BAS_O_RES_NEW_OFF + 2
BAS_O_RES_PREV_END equ BAS_O_RES_NEW_COUNT + 2
BAS_O_RES_NAME    equ BAS_O_RES_PREV_END + 2
BAS_O_RES_OP      equ BAS_O_RES_NAME + 13
BAS_O_RES_ITEM_OFF equ BAS_O_RES_OP + 1
BAS_O_RES_ITEM_LEN equ BAS_O_RES_ITEM_OFF + 2
BAS_O_RES_ITEM_CNT equ BAS_O_RES_ITEM_LEN + 2
BAS_O_RES_ITEM_TYP equ BAS_O_RES_ITEM_CNT + 2
BAS_BSS_TOTAL      equ BAS_O_RES_ITEM_TYP + 1

; -----------------------------------------------------------------------------
bas_int_to_dec:
    test ax, ax
    jns bas_word_to_dec
    mov byte [di], '-'
    inc di
    neg ax
    jmp bas_word_to_dec

bas_word_to_dec:
    ; OS8088 callback/worker entry leaves ES = KERNEL_SEG. STOSB writes
    ; through ES:DI, but our conversion destination is always DS:DI.
    ; Guard it here (rather than relying on every caller) so STR$, PRINT,
    ; MESSAGEBOX, and numeric diagnostics all use the application segment.
    push es
    push ds
    pop es
    push ax
    push bx
    push cx
    push dx
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    xor cx, cx
    mov bx, 10
    test ax, ax
    jnz .divide
    mov al, '0'
    stosb
    jmp .done
.divide:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .divide
.emit:
    pop ax
    add al, '0'
    stosb
    loop .emit
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

bas_console_scroll_one:
    ; Preserve the text-only display for the kernel's one-row scroll API.
    ; Multiple pending scrolls fall back to an opaque viewport refresh.
    call bas_mark_console_view_dirty
    cmp byte [bas_screen_mode], 0
    jne .scroll_copy
    cmp byte [bas_view_pending], 2
    jae .scroll_copy
    inc byte [bas_view_pending]
.scroll_copy:
    push es
    push ds
    pop es
    push ax
    push cx
    push si
    push di
    mov si, bas_console + BAS_CON_COLS
    mov di, bas_console
    mov cx, ((BAS_CON_LINES - 1) * BAS_CON_COLS) / 2
    rep movsw
    mov si, bas_con_fg + 1
    mov di, bas_con_fg
    mov cx, BAS_CON_LINES - 1
    rep movsb
    mov si, bas_con_bg + 1
    mov di, bas_con_bg
    mov cx, BAS_CON_LINES - 1
    rep movsb
    mov di, bas_console + ((BAS_CON_LINES - 1) * BAS_CON_COLS)
    mov al, ' '
    mov cx, BAS_CON_COLS - 1
    rep stosb
    mov byte [di], 0
    mov al, [bas_fg_color]
    mov [bas_con_fg + BAS_CON_LINES - 1], al
    mov al, [bas_bg_color]
    mov [bas_con_bg + BAS_CON_LINES - 1], al
    mov word [bas_con_count], BAS_CON_LINES - 1
    pop di
    pop si
    pop cx
    pop ax
    pop es
    ret

; Ensure bas_out_row names an allocated console row. BAS_CON_LINES is a
; deliberate sentinel meaning "scroll before the next output".
bas_console_ensure_cursor:
    push es
    push ds
    pop es
    push ax
    push bx
    push cx
    push di
.row_range:
    cmp word [bas_out_row], BAS_CON_LINES
    jb .allocate
    call bas_console_scroll_one
    mov word [bas_out_row], BAS_CON_LINES - 1
.allocate:
    mov ax, [bas_con_count]
    cmp ax, [bas_out_row]
    ja .done
    mov bx, ax
    mov al, [bas_fg_color]
    mov [bas_con_fg + bx], al
    mov al, [bas_bg_color]
    mov [bas_con_bg + bx], al
    mov ax, bx
    mov cx, BAS_CON_COLS
    mul cx
    mov di, bas_console
    add di, ax
    mov al, ' '
    mov cx, BAS_CON_COLS - 1
    rep stosb
    mov byte [di], 0
    inc word [bas_con_count]
    jmp .allocate
.done:
    pop di
    pop cx
    pop bx
    pop ax
    pop es
    ret

; Mark one HUD-style character span dirty during an unscrolled graphical RUN.
; 0=clean, 1=full repaint, 2=single span. Caller supplies BX=row, DX=start column
; and CX=end column (exclusive). Batched, scrolled, wrapped or overlapping output
; safely promotes to a full retained repaint.
bas_mark_text_span_dirty:
    cmp dx, cx
    jae .clean
    cmp byte [bas_screen_mode], 1
    jne .full
    cmp byte [bas_run_state], BAS_STATE_RUNNING
    jne .full
    cmp byte [bas_dirty], 0
    jne .full
    mov ax, [bas_con_count]
    cmp ax, [bas_visible]
    ja .full
    xor bh, bh
    cmp bx, [bas_visible]
    jae .full
    cmp cx, BAS_CON_COLS - 1
    ja .full
    mov [bas_draw_x0], bx
    mov [bas_draw_y0], dx
    mov [bas_draw_x1], cx
    mov byte [bas_dirty], 2
.clean:
    ret
.full:
    mov byte [bas_dirty], 1
    ret

; Text-mode viewport changed beyond the last row of the displayed page.
; Keep a text-only paint request instead of clearing the complete window.
; Dirty 1 remains authoritative for CLS, resize and graphics invalidations.
; 5 is a clipped opaque text viewport refresh (no pixel scroll).
bas_mark_console_view_dirty:
    cmp byte [bas_dirty], 1
    je .out
    cmp byte [bas_screen_mode], 0
    jne .full
    cmp byte [bas_dirty], 2
    je .full
    cmp byte [bas_dirty], 3
    je .full
    mov byte [bas_dirty], 5
    ret
.full:
    mov byte [bas_dirty], 1
.out:
    ret

; Retain a contiguous range of changed text-mode rows for a single repaint.
; Input BX = first affected row, DX = last affected row (inclusive).
; Do not erase or repaint the full client for each PRINT/HELP/LIST statement.
; Dirty=1 remains authoritative for CLS, resize and graphics changes.
; Dirty=2/3 are existing graphics/HUD optimized paths; never overwrite them.
bas_mark_console_rows_dirty:
    push ax
    cmp byte [bas_screen_mode], 0
    jne .full
    cmp byte [bas_dirty], 1
    je .out
    cmp byte [bas_dirty], 2
    je .full
    cmp byte [bas_dirty], 3
    je .full
    cmp byte [bas_dirty], 5
    je .out
    mov ax, [bas_con_count]
    cmp ax, [bas_visible]
    ja .view
    cmp dx, ax
    jae .full
    cmp byte [bas_dirty], 4
    je .merge
    mov [bas_row_dirty_first], bx
    mov [bas_row_dirty_last], dx
    mov byte [bas_dirty], 4
    jmp .out
.merge:
    cmp bx, [bas_row_dirty_first]
    jae .merge_last
    mov [bas_row_dirty_first], bx
.merge_last:
    cmp dx, [bas_row_dirty_last]
    jbe .out
    mov [bas_row_dirty_last], dx
    jmp .out
.view:
    call bas_mark_console_view_dirty
    jmp .out
.full:
    mov byte [bas_dirty], 1
.out:
    pop ax
    ret

; Print NUL text at the LOCATE cursor. A trailing PRINT semicolon leaves the
; cursor on the current row; otherwise output advances to the next row.
bas_console_print:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    call bas_console_ensure_cursor
    mov bp, [bas_out_row]
    mov dx, [bas_out_col]
    ; Preserve the true span start independently of DX. The character loop
     ; reuses DL for foreground/background colours. Preserve the requested
    ; start column until bas_mark_text_span_dirty has consumed it.
    mov [bas_draw_y1], dx
.next_char:
    mov al, [si]
    test al, al
    jz .end_text
    cmp word [bas_out_col], BAS_CON_COLS - 1
    jb .store
    inc word [bas_out_row]
    mov word [bas_out_col], 0
    call bas_console_ensure_cursor
.store:
    mov bx, [bas_out_row]
    mov dl, [bas_fg_color]
    mov [bas_con_fg + bx], dl
    mov dl, [bas_bg_color]
    mov [bas_con_bg + bx], dl
    mov ax, bx
    mov cx, BAS_CON_COLS
    mul cx
    add ax, [bas_out_col]
    mov di, bas_console
    add di, ax
    mov al, [si]
    mov [di], al
    inc si
    inc word [bas_out_col]
    jmp .next_char

.end_text:
    ; Plain text PRINT can paint only its newly written rows. The existing
    ; graphics-mode HUD span path remains unchanged for SCREEN 1.
    cmp byte [bas_screen_mode], 0
    jne .gfx_span
    mov bx, bp
    mov dx, [bas_out_row]
    call bas_mark_console_rows_dirty
    jmp .advance
.gfx_span:
    mov dx, [bas_draw_y1]
    mov bx, [bas_out_row]
    cmp bx, bp
    jne .full_dirty
    mov cx, [bas_out_col]
    call bas_mark_text_span_dirty
    jmp .advance
.full_dirty:
    mov byte [bas_dirty], 1
.advance:
    cmp byte [bas_print_nonl], 0
    jne .done
    inc word [bas_out_row]
    mov word [bas_out_col], 0
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_append_line:
    push es
    push ds
    pop es
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, [bas_con_count]
    cmp ax, BAS_CON_LINES
    jb .slot
    push si
    mov si, bas_console + BAS_CON_COLS
    mov di, bas_console
    mov cx, ((BAS_CON_LINES - 1) * BAS_CON_COLS) / 2
    rep movsw
    mov si, bas_con_fg + 1
    mov di, bas_con_fg
    mov cx, BAS_CON_LINES - 1
    rep movsb
    mov si, bas_con_bg + 1
    mov di, bas_con_bg
    mov cx, BAS_CON_LINES - 1
    rep movsb
    pop si
    call bas_mark_console_view_dirty
    cmp byte [bas_screen_mode], 0
    jne .scroll_slot
    cmp byte [bas_view_pending], 2
    jae .scroll_slot
    inc byte [bas_view_pending]
.scroll_slot:
    mov ax, BAS_CON_LINES - 1
    jmp .dest
.slot:
    inc word [bas_con_count]
.dest:
    mov dx, ax
    mov bx, ax
    mov al, [bas_fg_color]
    mov [bas_con_fg + bx], al
    mov al, [bas_bg_color]
    mov [bas_con_bg + bx], al

    mov ax, dx
    mov bx, BAS_CON_COLS
    mul bx
    mov di, bas_console
    add di, ax
    mov cx, BAS_CON_COLS - 1
.copy:
    lodsb
    test al, al
    jz .space_tail
    stosb
    loop .copy
    jmp .terminate
.space_tail:
    mov al, ' '
    rep stosb
.terminate:
    mov byte [di], 0
.done:
    mov ax, [bas_con_count]
    mov [bas_out_row], ax
    mov word [bas_out_col], 0
    ; MUL of row*64 above overwrites DX: recover actual appended row.
    mov bx, ax
    dec bx
    mov dx, bx
    call bas_mark_console_rows_dirty
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

; The kernel retains the original pointer to the title string. Update those
; persistent bytes in place when loading/saving/clearing a BASIC program.
; ABI 10 has no title-only invalidation API. Do NOT hide/show the window here:
; that destroys the visible console and repaints the entire desktop. The
; caption is picked up on the window manager's next natural title repaint.
; The console prints LOADED/SAVED immediately so the operation is visible.
bas_title_untitled:
    ; READY's default caption is the short build label, even after a RUN
    ; against a previously loaded .BAS file. Program bytes/name remain intact.
    push si
    push es
    push ds
    pop es
    mov si, bas_s_title_default
    mov di, bas_title
    mov cx, 31
.copy_default:
    lodsb
    stosb
    test al, al
    jz .done
    loop .copy_default
    mov byte [di], 0
.done:
    pop es
    pop si
    ret

bas_title_filename:
    push si
    mov si, bas_filename
    call bas_title_from_name
    pop si
    ret

bas_title_from_name:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, bas_title
    mov bx, si
    mov si, bas_s_title_prefix
    mov cx, 31
.copy_prefix:
    lodsb
    test al, al
    jz .copy_name_start
    stosb
    loop .copy_prefix
    jmp .terminate
.copy_name_start:
    mov si, bx
.copy_name:
    lodsb
    test al, al
    jz .terminate
    stosb
    loop .copy_name
.terminate:
    mov byte [di], 0
    ; No visibility change: the ABI-10 kernel repaints retained title bytes
    ; during the next normal WM title update; never HIDE/SHOW this window.
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret


; 8086-safe conditional branch shims. CPU 8086 has only rel8 Jcc, so these
; macros invert the condition around a rel16 JMP and remain safe as routines
; grow. They are used by the larger GUI/dialog handlers.
%macro BAS_JC 1
    jnc %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JNC 1
    jc %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JZ 1
    jnz %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JNZ 1
    jz %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JA 1
    jbe %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JAE 1
    jb %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JB 1
    jae %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JBE 1
    ja %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JG 1
    jle %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JGE 1
    jl %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JL 1
    jge %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JLE 1
    jg %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JS 1
    jns %%skip
    jmp %1
%%skip:
%endmacro
%macro BAS_JNS 1
    js %%skip
    jmp %1
%%skip:
%endmacro



; -----------------------------------------------------------------------------
; Package entry. The loader shows the returned window.
; -----------------------------------------------------------------------------
bas_entry:
    push si
    ; SDK 0.2 enters with ES=KERNEL_SEG. All our in-segment string
    ; operations (banner, title, transcript) require ES=DS instead.
    push ds
    pop es
    mov si, bas_tpl
    call OSAPI_WM_CREATE
    pop si
    jc .out
    mov [bas_window], bx
    ; The region may move (SPEC.md 66.6.1): nothing here caches our own
    ; segment, and every claim we hold is a separate, pinned block.
    OS88_REGION_MOVABLE
    ; The current file ABI is UI-task-only. Worker SVAR posts a wake and
    ; bas_win_cls's ONWAKE handler performs the actual FAT write. The console is
    ; born sizable (its class's style word, SPEC.md 11.104).
    push bx
    ; The launch directory is the RESET anchor and the fallback for HELP.TXT.
    ; Capture it once before any sample LOAD changes the instance directory.
    call OSAPI_FILE_HERE
    mov [bas_home_cluster], dx
    mov [bas_home_drive], bl
    call bas_init

    mov si, bas_form_tpl
    call OSAPI_WM_CREATE
    jc .no_form
    mov [bas_form_window], bx
.no_form:
    mov si, bas_dialog_tpl
    call OSAPI_WM_CREATE
    jc .no_dialog
    mov [bas_dialog_window], bx
.no_dialog:
    pop bx
    clc
.out:
    ret

; AX = KB wanted -> CF=0 and DX = the base segment, CF=1 refused. Every
; register but DX kept: the KB door's contract, over OSAPI_MEM_P's paragraphs
; (SPEC.md 50.3.7). A claim here is a whole-file disk buffer, so it is not
; MEMP_CPU - its base stays 512-aligned for int 13h.
bas_claim_kb:
    push ax
    push bx
    push cx
    mov cl, 6
    shl ax, cl                   ; KB -> paragraphs
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    pop cx
    pop bx
    pop ax
    ret

bas_onwake:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    cmp byte [bas_var_dirty], 0
    je .out
    ; The wake can race a second SVAR: write the current complete table,
    ; which is authoritative, rather than an obsolete queued snapshot.
    push ds
    pop es
    mov bx, bas_var_data
    mov cx, BAS_VAR_BYTES
    xor dx, dx
    mov si, bas_s_var_file
    call OSAPI_FILE_WRITE
    jc .failed
    mov byte [bas_var_dirty], 0
    jmp .out
.failed:
    ; Keep the dirty mark for a later wake. The next SVAR/worker redraw
    ; retries, and the in-memory values remain available through LVAR.
    mov byte [bas_var_bad], 1
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_init:
    push ax
    push si
    ; FORM and the dialogs stand on light grey in colour. On a 1bpp adapter
    ; grey is a dither (SPEC.md 39.4), and an opaque text run cannot put a
    ; dither under its glyphs, so there they stand on white - the mono
    ; desktop's own dialog ground.
    push bx
    push cx
    push dx
    call OSAPI_VIDEO
    mov al, CLGRAY
    cmp dh, 4
    jae .ground
    mov al, CWHITE
.ground:
    mov [bas_ui_ground], al
    pop dx
    pop cx
    pop bx
    mov byte [bas_fg_color], CBLACK
    mov byte [bas_bg_color], CWHITE
    mov byte [bas_prompt_fg], CBLACK
    mov byte [bas_prompt_bg], CWHITE
    mov byte [bas_screen_mode], 0
    mov byte [bas_run_state], BAS_STATE_READY
    mov byte [bas_dirty], 1
    mov si, bas_s_form_default
    mov di, bas_form_title
    mov cx, BAS_FORM_TITLE_CAP
.init_form_title:
    lodsb
    stosb
    test al, al
    jz .form_title_done
    loop .init_form_title
    mov byte [di-1], 0
.form_title_done:
    mov word [bas_form_req_w], BAS_FORM_DEF_W
    mov word [bas_form_req_h], BAS_FORM_DEF_H
    mov byte [bas_form_resize], 0
    call OSAPI_GET_TICKS
    call OSAPI_SRAND
    call bas_varstore_load
    mov si, bas_s_banner
    call bas_append_line
    mov si, bas_s_ready
    call bas_append_line
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; Paint transcript and current input prompt.
; in: SI = opaque window handle; gfx lock held
; -----------------------------------------------------------------------------
bas_paint:
    cmp byte [bas_exec_busy], 0
    jne .busy_return
    push es
    push ds
    pop es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    call OSAPI_WM_CONTENT
    mov [bas_content_x], ax
    mov [bas_content_y], dx
    add ax, BAS_MARGIN
    add dx, BAS_MARGIN
    mov [bas_text_x], ax
    mov [bas_text_y], dx

    call OSAPI_WM_GEOM
    jc .out
    mov [bas_content_w], cx
    mov [bas_content_h], dx

    ; Repaint the entire content in the current background colour.
    mov al, [bas_bg_color]
    call OSAPI_SET_COLOR
    mov ax, [bas_content_x]
    mov bx, [bas_content_y]
    mov cx, [bas_content_w]
    add cx, ax
    dec cx
    mov dx, [bas_content_h]
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL

    cmp byte [bas_screen_mode], 1
    jne .layout
    call bas_replay_graphics

.layout:
    mov ax, [bas_content_h]
    sub ax, BAS_MARGIN * 2
    jbe .no_rows
    shr ax, 1
    shr ax, 1
    shr ax, 1
    cmp ax, 2
    jae .rows_ok
    mov ax, 2
.rows_ok:
    dec ax
    cmp ax, BAS_CON_LINES
    jbe .vis_ok
    mov ax, BAS_CON_LINES
.vis_ok:
    mov [bas_visible], ax
    mov dx, ax
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [bas_text_y]
    mov [bas_input_y], dx
    jmp .draw

.no_rows:
    mov word [bas_visible], 1
    mov dx, [bas_text_y]
    add dx, 8
    mov [bas_input_y], dx

.draw:
    mov ax, [bas_con_count]
    mov cx, [bas_visible]
    cmp ax, cx
    jbe .start_zero
    sub ax, cx
    jmp .have_start
.start_zero:
    xor ax, ax
.have_start:
    mov [bas_view_first], ax     ; baseline for incremental RUN console scrolling
    mov byte [bas_view_pending], 0
    mov bp, ax
    mov di, [bas_text_y]

.line_loop:
    mov ax, [bas_con_count]
    cmp bp, ax
    jae .prompt

    ; Paint only the occupied span of this console row. LOCATE can allocate
    ; blank rows between HUD fields; clearing those rows across the full client
    ; used to punch horizontal white bands through retained graphics.
    mov ax, bp
    mov bx, BAS_CON_COLS
    mul bx
    mov si, bas_console
    add si, ax
    xor bx, bx
.find_text_first:
    mov al, [si + bx]
    test al, al
    jz .next_line
    cmp al, ' '
    jne .text_first_found
    inc bx
    jmp .find_text_first
.text_first_found:
    mov [bas_draw_y0], bx
    mov [bas_draw_y1], bx
.find_text_last:
    mov al, [si + bx]
    test al, al
    jz .text_span_ready
    cmp al, ' '
    je .text_last_next
    mov ax, bx
    inc ax
    mov [bas_draw_y1], ax
.text_last_next:
    inc bx
    jmp .find_text_last
.text_span_ready:
    ; Opaque FONT_RUN below is the single-pass erase+glyph of this occupied
    ; span. The old fill and transparent font pair caused double drawing.
    mov ax, bp
    mov bx, BAS_CON_COLS
    mul bx
    mov si, bas_console
    add si, ax
    add si, [bas_draw_y0]
    push di
    mov dx, di
    mov di, bas_console
    add di, ax
    add di, [bas_draw_y1]
    mov al, [di]
    push ax
    mov byte [di], 0
    mov cx, [bas_draw_y0]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [bas_text_x]
    mov bx, bp
    mov al, [bas_con_fg + bx]
    mov ah, [bas_con_bg + bx]
    call OSAPI_FONT_RUN
    pop ax
    mov [di], al
    pop di

.next_line:
    add di, 8
    inc bp
    mov ax, di
    cmp ax, [bas_input_y]
    jb .line_loop

.prompt:
    ; RUN owns the complete graphics canvas. Do not even erase the READY/INPUT
    ; prompt row while RUNNING: with retained text capped at 35 rows, bas_input_y
    ; sits at logical y=280 on a taller window and that eight-pixel erase clipped
    ; 2BCHECK's bottom rank no matter how many extra CONSOLESIZE rows were added.
    cmp byte [bas_run_state], BAS_STATE_RUNNING
    je .running_prompt

    mov al, [bas_prompt_bg]
    call OSAPI_SET_COLOR
    mov ax, [bas_content_x]
    mov bx, [bas_input_y]
    mov cx, [bas_content_w]
    add cx, ax
    dec cx
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL

    mov al, [bas_prompt_fg]
    call OSAPI_SET_COLOR
    mov cx, [bas_text_x]
    mov dx, [bas_input_y]
    mov si, bas_s_prompt_right
    cmp byte [bas_run_state], BAS_STATE_INPUT
    jne .prompt_char
    mov si, bas_s_prompt_question
.prompt_char:
    mov al, [bas_prompt_fg]
    mov ah, [bas_prompt_bg]
    call OSAPI_FONT_RUN
    ; Keep the insertion cursor visible while allowing Left/Right/Home/End to
    ; edit recalled or newly typed text. BX is the first visible source byte.
    mov bx, [bas_input_pos]
    mov bp, BAS_CON_MAX_COLS - 2
    cmp bx, bp
    jbe .view_from_zero
    sub bx, bp
    jmp .view_ready
.view_from_zero:
    xor bx, bx
.view_ready:
    mov ax, [bas_input_len]
    sub ax, bx
    cmp ax, bp
    jbe .prompt_visible
    mov ax, bp
.prompt_visible:
    mov si, bas_input
    add si, bx
    mov di, bas_scratch
    mov cx, ax
    rep movsb
    mov byte [di], 0
    ; The full paint has already cleared the prompt background. Save the
    ; current visible input width for the subsequent one-pass prompt updates.
    mov [bas_prompt_last_cols], ax
    mov si, bas_scratch
    mov cx, [bas_text_x]
    add cx, 16
    mov dx, [bas_input_y]
    push bx
    mov al, [bas_prompt_fg]
    mov ah, [bas_prompt_bg]
    call OSAPI_FONT_RUN
    pop bx

    mov ax, [bas_input_pos]
    sub ax, bx
.cursor_visible:
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [bas_text_x]
    add ax, 16
    mov bx, [bas_input_y]
    mov dx, bx
    add dx, 7
    push ax
    mov al, [bas_prompt_fg]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
    jmp .out
.running_prompt:
    ; Do not paint a RUNNING footer into the graphics canvas. During execution
    ; the grow box is also hidden so a resize cannot leave stale duplicate
    ; footer/grow-box pixels while the worker owns the drawing state.
    jmp .out

.out:
    cmp byte [bas_run_state], BAS_STATE_RUNNING
    je .no_grow
    mov bx, [bas_window]
    call OSAPI_WM_GROW
.no_grow:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret
.busy_return:
    mov byte [bas_dirty], 1
    ret

; -----------------------------------------------------------------------------
; Keyboard callback. Enter commits a command; Escape clears current input.
; -----------------------------------------------------------------------------
bas_onkey:
    push es
    push ds
    pop es
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [bas_window], si
    cmp byte [bas_run_state], BAS_STATE_READY
    je .ready_mode
    cmp byte [bas_run_state], BAS_STATE_INPUT
    je .input_mode

    ; RUN BREAK: Escape, Ctrl+C, or Ctrl+Break (BIOS scan 46h). The
    ; worker checks the request between statements and after SLEEP ticks.
    ; Do not repaint here: the worker still owns the output viewport.
    cmp al, 27
    je .request_break
    cmp al, 3
    je .request_break
    test al, al
    jnz .capture_key
    cmp ah, 46h
    jne .capture_key
.request_break:
    mov byte [bas_break_req], 1
    jmp .out
.capture_key:
    xor bx, bx
    test al, al
    jz .extended_key
    mov bl, al
    jmp .store_key
.extended_key:
    mov bl, ah
    mov bh, 1
.store_key:
    mov [bas_last_key], bx
    jmp .out

.input_mode:
    cmp al, 27
    je .input_break
    cmp al, 3
    je .input_break
    test al, al
    jnz .input_key
    cmp ah, 46h
    je .input_break
.input_key:
    cmp al, 8
    je .backspace
    cmp al, 13
    je .input_enter
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    mov bx, [bas_input_len]
    cmp bx, BAS_INPUT_CAP
    jae .out
    mov [bx + bas_input], al
    inc bx
    mov [bas_input_len], bx
    mov [bas_input_pos], bx
    mov byte [bx + bas_input], 0
    jmp .redraw_prompt

.input_break:
    mov byte [bas_break_req], 1
    mov byte [bas_run_state], BAS_STATE_RUNNING
    mov word [bas_input_len], 0
    mov word [bas_input_pos], 0
    mov byte [bas_input], 0
    mov byte [bas_dirty], 1
    jmp .redraw

.input_enter:
    mov si, bas_input
    call bas_skip_spaces
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .input_redo
    push ax
    call bas_expect_end
    pop ax
    jc .input_redo
    mov bx, [bas_input_var]
    mov [bas_vars + bx], ax
    push ax
    mov si, bas_input
    call bas_append_line
    pop ax
    mov word [bas_input_len], 0
    mov word [bas_input_pos], 0
    mov byte [bas_input], 0
    mov byte [bas_run_state], BAS_STATE_RUNNING
    mov byte [bas_dirty], 1
    jmp .redraw

.input_redo:
    mov si, bas_s_redo
    call bas_append_line
    mov word [bas_input_len], 0
    mov word [bas_input_pos], 0
    mov byte [bas_input], 0
    mov byte [bas_run_state], BAS_STATE_INPUT
    jmp .redraw

.ready_mode:
    ; BIOS-style extended keys arrive with AL=0 and the scan code in AH.
    ; READY mode provides a one-line insertion editor around recalled commands.
    test al, al
    jnz .ready_ascii
    cmp ah, 48h
    je .history_up
    cmp ah, 4Bh
    je .cursor_left
    cmp ah, 4Dh
    je .cursor_right
    cmp ah, 47h
    je .cursor_home
    cmp ah, 4Fh
    je .cursor_end
    cmp ah, 53h
    je .delete_at_cursor
    jmp .out
.ready_ascii:
    cmp al, 27
    je .clear
    cmp al, 8
    je .backspace
    cmp al, 13
    je .enter
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out

    mov bx, [bas_input_len]
    cmp bx, BAS_INPUT_CAP
    jae .out
    mov dl, al
    mov di, bas_input
    add di, bx
    inc di
    mov si, di
    dec si
    mov cx, bx
    sub cx, [bas_input_pos]
    inc cx                              ; include the terminating NUL
    std
    rep movsb
    cld
    mov bx, [bas_input_pos]
    mov [bx + bas_input], dl
    inc bx
    mov [bas_input_pos], bx
    inc word [bas_input_len]
    jmp .redraw_prompt

.history_up:
    call bas_history_recall
    jc .out
    jmp .redraw_prompt

.cursor_left:
    cmp word [bas_input_pos], 0
    je .out
    dec word [bas_input_pos]
    jmp .redraw_prompt

.cursor_right:
    mov bx, [bas_input_pos]
    cmp bx, [bas_input_len]
    jae .out
    inc word [bas_input_pos]
    jmp .redraw_prompt

.cursor_home:
    mov word [bas_input_pos], 0
    jmp .redraw_prompt

.cursor_end:
    mov bx, [bas_input_len]
    mov [bas_input_pos], bx
    jmp .redraw_prompt

.delete_at_cursor:
    mov bx, [bas_input_pos]
    cmp bx, [bas_input_len]
    jae .out
    mov di, bas_input
    add di, bx
    mov si, di
    inc si
    mov cx, [bas_input_len]
    sub cx, bx
    cld
    rep movsb                           ; includes the terminating NUL
    dec word [bas_input_len]
    jmp .redraw_prompt

.backspace:
    mov bx, [bas_input_pos]
    test bx, bx
    jz .out
    mov si, bas_input
    add si, bx
    mov di, si
    dec di
    mov cx, [bas_input_len]
    sub cx, bx
    inc cx                              ; include the terminating NUL
    cld
    rep movsb
    dec word [bas_input_pos]
    dec word [bas_input_len]
    jmp .redraw_prompt

.clear:
    mov word [bas_input_len], 0
    mov word [bas_input_pos], 0
    mov byte [bas_input], 0
    mov byte [bas_input_ready], 0
    mov byte [bas_auto_mode], 0
    jmp .redraw_prompt

.enter:
    mov si, bas_input
    cmp byte [si], 0
    je .clear_after
    cmp byte [bas_auto_mode], 0
    je .enter_command
    push si
    call bas_skip_spaces
    call bas_parse_uword
    jc .auto_not_blank
    call bas_skip_spaces
    cmp byte [si], 0
    jne .auto_not_blank
    pop si
    mov byte [bas_auto_mode], 0
    mov byte [bas_input_ready], 0
    jmp .clear_after
.auto_not_blank:
    pop si
.enter_command:
    call bas_history_store
    mov byte [bas_input_ready], 0
    call bas_append_line
    call bas_execute_command
.clear_after:
    ; EDIT may prepare a replacement source line for the next keystroke.
    cmp byte [bas_input_ready], 0
    jne .keep_input
    xor ax, ax
    mov [bas_input_len], ax
    mov [bas_input_pos], ax
    mov [bas_input], al
.keep_input:
    ; Enter may append command output and history, so repaint the complete
    ; transcript now. Prompt-row-only repaint remains for ordinary editing.
    jmp .redraw

.redraw_prompt:
    ; No full-window dirty mark: the idle interpreter worker must never see
    ; a transient dirty=1 while the UI callback redraws just the prompt.
    pop di
    pop si
    push si
    push di
    ; Keyboard callbacks hold the gfx lock but do not implicitly establish
    ; the window content clip. Bound partial prompt updates to our window.
    mov bx, [bas_window]
    call OSAPI_WM_CLIP_SET
    jc .out
    call bas_redraw_prompt
    cmp byte [bas_exec_busy], 0
    jne .out
    mov byte [bas_dirty], 0
    jmp .out

.redraw:
    pop di
    pop si
    push si
    push di
    ; Even the UI/keyboard path must clip its own text rows; the worker
    ; already does so. This is important when a window is moved or narrowed.
    mov bx, [bas_window]
    call OSAPI_WM_CLIP_SET
    jc .out
    ; A background worker can present the queued text before this callback
    ; resumes. Dirty=0 means it is already painted: do not turn it back into
    ; a full erase/repaint merely because the partial job was consumed.
    cmp byte [bas_dirty], 0
    je .redraw_clean
    cmp byte [bas_dirty], 4
    je .redraw_rows
    cmp byte [bas_dirty], 5
    je .redraw_view
    jmp .redraw_all
.redraw_clean:
    call bas_redraw_prompt
    jmp .redraw_done
.redraw_rows:
    call bas_redraw_console_rows
    call bas_redraw_prompt
    jmp .redraw_done
.redraw_view:
    call bas_redraw_console_view
    call bas_redraw_prompt
    jmp .redraw_done
.redraw_all:
    mov byte [bas_dirty], 1
    call bas_redraw
.redraw_done:
    cmp byte [bas_exec_busy], 0
    jne .out
    mov byte [bas_dirty], 0

.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

; Preserve the most recently submitted non-empty READY prompt line.
bas_history_store:
    push ax
    push cx
    push si
    push di
    mov ax, [bas_input_len]
    test ax, ax
    jz .out
    mov [bas_history_len], ax
    mov cx, ax
    mov si, bas_input
    mov di, bas_history
    rep movsb
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; Replace the current READY prompt with the one-entry history buffer.
; CF set when no history exists.
bas_history_recall:
    push ax
    push cx
    push si
    push di
    mov ax, [bas_history_len]
    test ax, ax
    jz .empty
    mov [bas_input_len], ax
    mov [bas_input_pos], ax
    mov cx, ax
    mov si, bas_history
    mov di, bas_input
    rep movsb
    mov byte [di], 0
    mov byte [bas_input_ready], 0
    mov byte [bas_auto_mode], 0
    clc
    jmp .out
.empty:
    stc
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; Refresh cached content coordinates before a prompt-only repaint. Windows may
; be moved or resized without a full application paint between key callbacks.
bas_refresh_prompt_layout:
    push ax
    push bx
    push cx
    push dx
    mov bx, [bas_window]
    call OSAPI_WM_CONTENT
    mov [bas_content_x], ax
    mov [bas_content_y], dx
    add ax, BAS_MARGIN
    add dx, BAS_MARGIN
    mov [bas_text_x], ax
    mov [bas_text_y], dx
    mov bx, [bas_window]
    call OSAPI_WM_GEOM
    jc .out
    mov [bas_content_w], cx
    mov [bas_content_h], dx
    mov ax, dx
    sub ax, BAS_MARGIN * 2
    jbe .no_rows
    shr ax, 1
    shr ax, 1
    shr ax, 1
    cmp ax, 2
    jae .rows_ok
    mov ax, 2
.rows_ok:
    dec ax
    cmp ax, BAS_CON_LINES
    jbe .vis_ok
    mov ax, BAS_CON_LINES
.vis_ok:
    mov [bas_visible], ax
    mov dx, ax
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [bas_text_y]
    mov [bas_input_y], dx
    jmp .out
.no_rows:
    mov word [bas_visible], 1
    mov dx, [bas_text_y]
    add dx, 8
    mov [bas_input_y], dx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Repaint only the changed character span of one unscrolled retained console
; row without clearing/replaying the SCREEN 1 graphics canvas. Scratch contract:
; bas_draw_x0=row, bas_draw_y0=start column, bas_draw_x1=end column (exclusive).
; The worker presents immediately, so these ordinary parser/drawing scratch words
; are safe to borrow until this repaint completes.
bas_redraw_console_span:
    mov bp, [bas_draw_x0]
    mov di, bp
    shl di, 1
    shl di, 1
    shl di, 1
    add di, [bas_text_y]

    ; One opaque FONT_RUN covers exactly [bas_draw_y0, bas_draw_x1).
    ; No GFX_FILL first: on real XT it flashes and wastes a drawing pass.
    mov ax, bp
    mov cx, BAS_CON_COLS
    mul cx
    mov si, bas_console
    add si, ax
    add si, [bas_draw_y0]
    mov di, bas_console
    add di, ax
    add di, [bas_draw_x1]
    mov al, [di]
    push ax
    mov byte [di], 0
    mov cx, [bas_draw_y0]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [bas_text_x]
    mov dx, bp
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [bas_text_y]
    mov bx, bp
    mov al, [bas_con_fg + bx]
    mov ah, [bas_con_bg + bx]
    call OSAPI_FONT_RUN
    pop ax
    mov [di], al
    ret

; Paint the queued text-mode rows without erasing the canvas or old
; transcript. Only non-space spans are drawn, each via one opaque FONT_RUN.
; Only valid while con_count <= visible; larger/scrolled history takes the
; ordinary full repaint. Scratch field dirty_first/last records the range.
bas_redraw_console_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call bas_refresh_prompt_layout
    mov ax, [bas_con_count]
    cmp ax, [bas_visible]
    ja .full
    mov bp, [bas_row_dirty_first]
.row:
    cmp bp, [bas_row_dirty_last]
    ja .done
    cmp bp, [bas_con_count]
    jae .done
    mov ax, bp
    mov bx, BAS_CON_COLS
    mul bx
    mov si, bas_console
    add si, ax
    xor bx, bx
.find_start:
    mov al, [si + bx]
    test al, al
    jz .next
    cmp al, ' '
    jne .found
    inc bx
    cmp bx, BAS_CON_COLS - 1
    jb .find_start
    jmp .next
.found:
    mov dx, bx                     ; start column
    mov cx, bx                     ; exclusive end column
.find_end:
    mov al, [si + bx]
    test al, al
    jz .paint
    cmp al, ' '
    je .scan_next
    mov cx, bx
    inc cx
.scan_next:
    inc bx
    cmp bx, BAS_CON_COLS - 1
    jb .find_end
.paint:
    mov di, si
    add di, cx
    mov al, [di]
    push ax
    mov byte [di], 0
    add si, dx
    mov cx, dx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [bas_text_x]
    mov dx, bp
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [bas_text_y]
    mov bx, bp
    mov al, [bas_con_fg + bx]
    mov ah, [bas_con_bg + bx]
    call OSAPI_FONT_RUN
    pop ax
    mov [di], al
.next:
    inc bp
    jmp .row
.full:
    ; A resize between dirty marking and painting invalidates row positions.
    mov si, [bas_window]
    call bas_redraw
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The retained transcript may exceed the window's visible rows. Instead of
; full GFX_FILL + replay (which flashes the entire console on each RUN PRINT),
; scroll one 8px text row with the native byte-aligned screen scroll API.
; Fallback on clipping refusal, multiple rows or changed geometry: redraw the
; visible text with opaque FONT_RUNs, NEVER blank the complete client first.
; Worker calls this with gfx lock + window clip; READY calls already hold lock.
bas_redraw_console_view:
    ; Keep the native pixel scroller disabled. Rendering padded blank cells
    ; for every history row would be unnecessarily expensive.
    ; Draw the occupied prefix once, and clear the remaining tail with a
    ; single solid rectangle. Nothing outside our window is touched: callers
    ; already own the gfx lock and have armed OSAPI_WM_CLIP_SET.
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call bas_refresh_prompt_layout
    mov ax, [bas_con_count]
    sub ax, [bas_visible]
    jnc .first_ready
    xor ax, ax
.first_ready:
    mov [bas_view_next], ax
    xor bp, bp
.paint_row:
    cmp bp, [bas_visible]
    jae .done
    mov bx, [bas_view_next]
    add bx, bp
    cmp bx, [bas_con_count]
    jae .done
    mov ax, bx
    mov cx, BAS_CON_COLS
    mul cx
    mov si, bas_console
    add si, ax
    mov di, si
    add di, BAS_CON_COLS - 2
    mov cx, BAS_CON_COLS - 1
.trim_spaces:
    cmp byte [di], ' '
    jne .have_length
    dec di
    loop .trim_spaces
.have_length:
    ; CX=number of printable columns. A blank row is CX=0.
    mov [bas_draw_x0], cx
    cmp cx, BAS_CON_COLS - 1
    jae .text_prefix
    ; One fill for the blank suffix, including the entire row if empty.
    ; GFX_FILL is a single kernel rectangle, not 63 opaque space glyphs.
    mov al, [bas_con_bg + bx]
    call OSAPI_SET_COLOR
    mov ax, [bas_draw_x0]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [bas_text_x]
    push bx
    mov bx, bp
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [bas_text_y]
    mov dx, bx
    add dx, 7
    mov cx, [bas_text_x]
    add cx, ((BAS_CON_COLS - 1) * 8) - 1
    call OSAPI_GFX_FILL
    pop bx
.text_prefix:
    cmp word [bas_draw_x0], 0
    je .next_row
    mov di, si
    add di, [bas_draw_x0]
    mov al, [di]
    push ax
    mov byte [di], 0
    mov al, [bas_con_fg + bx]
    mov ah, [bas_con_bg + bx]
    mov cx, [bas_text_x]
    mov dx, bp
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [bas_text_y]
    call OSAPI_FONT_RUN
    pop ax
    mov [di], al
.next_row:
    inc bp
    jmp .paint_row
.done:
    mov ax, [bas_view_next]
    mov [bas_view_first], ax
    mov byte [bas_view_pending], 0
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Repaint only the active prompt row while editing. The retained transcript is
; untouched, avoiding the full-window clear/repaint flicker on every keystroke.
bas_redraw_prompt:
    cmp byte [bas_exec_busy], 0
    jne .busy
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    call bas_refresh_prompt_layout

    ; Opaque FONT_RUN below paints changed input glyphs and the trailing
    ; space in one pass. No erase-first GFX_FILL that visibly blanks the
    ; entire prompt row on each keypress on 4.77MHz hardware.
    mov cx, [bas_text_x]
    mov dx, [bas_input_y]
    cmp byte [bas_run_state], BAS_STATE_RUNNING
    je .running
    mov si, bas_s_prompt_right
    cmp byte [bas_run_state], BAS_STATE_INPUT
    jne .prompt_char
    mov si, bas_s_prompt_question
.prompt_char:
    mov al, [bas_prompt_fg]
    mov ah, [bas_prompt_bg]
    call OSAPI_FONT_RUN

    ; Match full-paint behaviour and scroll around the true insertion cursor.
    mov bx, [bas_input_pos]
    mov bp, BAS_CON_MAX_COLS - 2
    cmp bx, bp
    jbe .view_from_zero
    sub bx, bp
    jmp .view_ready
.view_from_zero:
    xor bx, bx
.view_ready:
    mov ax, [bas_input_len]
    sub ax, bx
    cmp ax, bp
    jbe .prompt_visible
    mov ax, bp
.prompt_visible:
    mov si, bas_input
    add si, bx
    mov di, bas_scratch
    mov bp, ax                    ; current visible input width
    mov cx, ax
    rep movsb
    ; The previous cursor (or deleted suffix) has to be erased, too. Pad the
    ; *same* opaque text run through the previous width plus one cell rather
    ; than clearing the entire row in a separate drawing call.
    mov cx, [bas_prompt_last_cols]
    cmp cx, bp
    jae .old_width
    mov cx, bp
.old_width:
    sub cx, bp
    inc cx
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov [bas_prompt_last_cols], bp
    mov si, bas_scratch
    mov cx, [bas_text_x]
    add cx, 16
    mov dx, [bas_input_y]
    push bx
    mov al, [bas_prompt_fg]
    mov ah, [bas_prompt_bg]
    call OSAPI_FONT_RUN
    pop bx

    mov ax, [bas_input_pos]
    sub ax, bx
.cursor_visible:
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [bas_text_x]
    add ax, 16
    mov bx, [bas_input_y]
    mov dx, bx
    add dx, 7
    push ax
    mov al, [bas_prompt_fg]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
    jmp .done
.running:
    ; In text mode the old READY prompt must disappear when RUN begins,
    ; but wiping the entire window is unnecessary. One opaque background
    ; run covers that fixed prompt row, without touching the transcript.
    cmp byte [bas_screen_mode], 0
    jne .done
    mov si, bas_s_blank_console
    mov cx, [bas_text_x]
    mov dx, [bas_input_y]
    mov al, [bas_fg_color]
    mov ah, [bas_bg_color]
    call OSAPI_FONT_RUN
.done:
    ; The window manager paints the grow box on full paints. Redrawing it
    ; after every typed character caused unnecessary extra window work.
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.busy:
    mov byte [bas_dirty], 1
    ret

bas_redraw:
    cmp byte [bas_exec_busy], 0
    jne .busy
    ; bas_paint already performs the needed full background clear. Doing a
    ; second GFX_FILL before it visibly blanked the screen twice on an XT.
    call bas_paint
    ret
.busy:
    mov byte [bas_dirty], 1
    ret

; -----------------------------------------------------------------------------
; Execute one direct command or store one numbered source line.
; -----------------------------------------------------------------------------
bas_execute_command:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov word [bas_current_line], 0
    mov si, bas_input
    call bas_skip_spaces
    mov al, [si]
    cmp al, '0'
    jb .commands
    cmp al, '9'
    jbe .numbered

.commands:
    mov byte [bas_auto_mode], 0
    mov si, bas_input
    call bas_skip_spaces
    cmp byte [si], 27h
    je .done
    cmp byte [si], '?'
    jne .check_rem
    inc si
    jmp .print
.check_rem:
    mov di, bas_kw_rem
    call bas_match_word
    jnc .done

    mov di, bas_kw_help
    call bas_match_direct_word
    jnc .help

    mov di, bas_kw_reset
    call bas_match_direct_word
    jnc .reset

    mov di, bas_kw_new
    call bas_match_direct_word
    jnc .new

    mov di, bas_kw_list
    call bas_match_direct_word
    jnc .list

    mov di, bas_kw_delete
    call bas_match_direct_word
    jnc .delete

    mov di, bas_kw_edit
    call bas_match_direct_word
    jnc .edit

    mov di, bas_kw_auto
    call bas_match_direct_word
    jnc .auto

    mov di, bas_kw_cls
    call bas_match_direct_word
    jnc .cls

    mov di, bas_kw_run
    call bas_match_direct_word
    jnc .run

    mov di, bas_kw_save
    call bas_match_direct_word
    jnc .save

    mov di, bas_kw_load
    call bas_match_direct_word
    jnc .load

    mov di, bas_kw_print
    call bas_match_direct_word
    jnc .print

    mov di, bas_kw_input
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_sleep
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_promptcolor
    call bas_match_direct_word
    jnc .promptcolor

    mov di, bas_kw_color
    call bas_match_direct_word
    jnc .color

    mov di, bas_kw_locate
    call bas_match_direct_word
    jnc .locate

    mov di, bas_kw_screen
    call bas_match_direct_word
    jnc .screen

    mov di, bas_kw_pset
    call bas_match_direct_word
    jnc .pset

    mov di, bas_kw_line
    call bas_match_direct_word
    jnc .line

    mov di, bas_kw_box
    call bas_match_direct_word
    jnc .box

    mov di, bas_kw_fillbox
    call bas_match_direct_word
    jnc .fillbox

    mov di, bas_kw_circle
    call bas_match_direct_word
    jnc .circle

    mov di, bas_kw_beep
    call bas_match_direct_word
    jnc .beep

    mov di, bas_kw_sound
    call bas_match_direct_word
    jnc .sound

    mov di, bas_kw_randomize
    call bas_match_direct_word
    jnc .randomize

    mov di, bas_kw_poke
    call bas_match_direct_word
    jnc .poke

    mov di, bas_kw_fmpreset
    call bas_match_direct_word
    jnc .fmpreset

    mov di, bas_kw_fmnote
    call bas_match_direct_word
    jnc .fmnote

    mov di, bas_kw_fmoff
    call bas_match_direct_word
    jnc .fmoff

    mov di, bas_kw_fmstop
    call bas_match_direct_word
    jnc .fmstop

    mov di, bas_kw_loadbmp
    call bas_match_direct_word
    jnc .loadbmp

    mov di, bas_kw_loadres
    call bas_match_direct_word
    jnc .loadres

    mov di, bas_kw_putres
    call bas_match_direct_word
    jnc .putres

    mov di, bas_kw_resselect
    call bas_match_direct_word
    jnc .resselect

    mov di, bas_kw_putbmp
    call bas_match_direct_word
    jnc .putbmp

    mov di, bas_kw_freebmp
    call bas_match_direct_word
    jnc .freebmp

    mov di, bas_kw_drawbegin
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_drawend
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_form
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_label
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_button
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_checkbox
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_frame
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_image
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_showform
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_closeform
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_settext
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_setvalue
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_enable
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_visible
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_messagebox
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_inputbox
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_svar
    call bas_match_direct_word
    jnc .svar

    mov di, bas_kw_lvar
    call bas_match_direct_word
    jnc .lvar

    mov di, bas_kw_consolesize
    call bas_match_direct_word
    jnc .consolesize

    mov di, bas_kw_minconsole
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_restoreconsole
    call bas_match_direct_word
    jnc .program_only

    mov di, bas_kw_version
    call bas_match_direct_word
    jnc .version

    mov di, bas_kw_capacity
    call bas_match_direct_word
    jnc .capacity

    mov di, bas_kw_mem
    call bas_match_direct_word
    jnc .mem

    mov di, bas_kw_dim
    call bas_match_direct_word
    jnc .dim

    mov di, bas_kw_let
    call bas_match_direct_word
    jnc .let

    ; GW-BASIC immediate assignment: A=expression; array targets are allowed after DIM.
    mov si, bas_input
    call bas_skip_spaces
    call bas_exec_assignment
    jnc .done
    call bas_report_error
    jmp .done

.help:
    ; Read only the chosen section from APPS/HELP.TXT. The disk API is
    ; UI-task only, and direct commands run on that task.
    call bas_help_run
    jmp .done

.reset:
    call bas_expect_end
    jc .command_error
    call bas_reset_console
    jmp .done

.new:
    call bas_expect_end
    jc .command_error
    call bas_new_program
    call bas_clear_vars
    call bas_title_untitled
    mov word [bas_gosub_count], 0
    mov word [bas_for_count], 0
    mov si, bas_s_ready
    call bas_append_line
    jmp .done

.list:
    call bas_skip_spaces
    cmp byte [si], 0
    jne .list_args
    mov ax, 1
    mov bx, 0FFFFh
    call bas_list_program
    jmp .done
.list_args:
    call bas_parse_uword
    jc .direct_syntax
    test ax, ax
    jz .direct_syntax
    mov dx, ax
    mov bx, ax
    call bas_skip_spaces
    cmp byte [si], '-'
    jne .list_end
    inc si
    call bas_parse_uword
    jc .direct_syntax
    mov bx, ax
.list_end:
    call bas_expect_end
    jc .command_error
    cmp bx, dx
    jb .direct_syntax
    mov ax, dx
    call bas_list_program
    jmp .done

.delete:
    call bas_skip_spaces
    call bas_parse_uword
    jc .direct_syntax
    test ax, ax
    jz .direct_syntax
    mov dx, ax
    mov bx, ax
    call bas_skip_spaces
    cmp byte [si], '-'
    jne .delete_end
    inc si
    call bas_parse_uword
    jc .direct_syntax
    mov bx, ax
.delete_end:
    call bas_expect_end
    jc .command_error
    cmp bx, dx
    jb .direct_syntax
    mov ax, dx
    call bas_delete_range
    jmp .done

.edit:
    call bas_skip_spaces
    call bas_parse_uword
    jc .direct_syntax
    test ax, ax
    jz .direct_syntax
    push ax
    call bas_expect_end
    pop ax
    jc .command_error
    call bas_prepare_edit
    jc .command_error
    jmp .done

.auto:
    call bas_skip_spaces
    cmp byte [si], 0
    jne .auto_args
    mov ax, 10
    mov bx, 10
    jmp .auto_set
.auto_args:
    call bas_parse_uword
    jc .direct_syntax
    test ax, ax
    jz .direct_syntax
    mov dx, ax
    mov bx, 10
    call bas_skip_spaces
    cmp byte [si], ','
    jne .auto_restore
    inc si
    call bas_parse_uword
    jc .direct_syntax
    mov bx, ax
    test bx, bx
    jz .direct_syntax
.auto_restore:
    mov ax, dx
.auto_end:
    call bas_expect_end
    jc .command_error
.auto_set:
    mov [bas_auto_next], ax
    mov [bas_auto_step], bx
    mov byte [bas_auto_mode], 1
    call bas_prepare_auto
    jmp .done

.cls:
    call bas_expect_end
    jc .command_error
    call bas_cls
    jmp .done

.run:
    call bas_expect_end
    jc .command_error
    call bas_start_run
    jc .run_error
    ; Exactly one blank line separates RUN from the first program output.
    mov si, bas_s_empty
    call bas_append_line
    jmp .done
.run_error:
    call bas_report_error
    jmp .done

.save:
    call bas_save_program
    jmp .done

.load:
    call bas_load_program
    jmp .done

.print:
    call bas_exec_print
    jmp .direct_exec_result

.program_only:
    mov byte [bas_error_code], BERR_PROGMODE
    call bas_report_error
    jmp .done

.promptcolor:
    call bas_exec_promptcolor
    jmp .direct_exec_result

.color:
    call bas_exec_color
    jmp .direct_exec_result

.locate:
    call bas_exec_locate
    jmp .direct_exec_result

.screen:
    call bas_exec_screen
    jmp .direct_exec_result

.pset:
    mov al, GFX_PSET
    call bas_exec_graphics
    jmp .direct_exec_result

.line:
    mov al, GFX_LINE
    call bas_exec_graphics
    jmp .direct_exec_result

.box:
    mov al, GFX_BOX
    call bas_exec_graphics
    jmp .direct_exec_result

.fillbox:
    mov al, GFX_FILLBOX
    call bas_exec_graphics
    jmp .direct_exec_result

.circle:
    mov al, GFX_CIRCLE
    call bas_exec_graphics
    jmp .direct_exec_result

.beep:
    call bas_exec_beep
    jmp .direct_exec_result

.sound:
    call bas_exec_sound
    jmp .direct_exec_result

.randomize:
    call bas_exec_randomize
    jmp .direct_exec_result

.poke:
    call bas_exec_poke
    jmp .direct_exec_result

.fmpreset:
    call bas_exec_fmpreset
    jmp .direct_exec_result

.fmnote:
    call bas_exec_fmnote
    jmp .direct_exec_result

.fmoff:
    call bas_exec_fmoff
    jmp .direct_exec_result

.fmstop:
    call bas_exec_fmstop
    jmp .direct_exec_result

.loadbmp:
    call bas_exec_loadbmp
    jmp .direct_exec_result

.loadres:
    call bas_exec_loadres
    jmp .direct_exec_result

.resselect:
    call bas_exec_resselect
    jmp .direct_exec_result

.putres:
    call bas_exec_putres
    jmp .direct_exec_result

.putbmp:
    call bas_exec_putbmp
    jmp .direct_exec_result

.freebmp:
    call bas_exec_freebmp
    jmp .direct_exec_result

.svar:
    call bas_exec_svar
    jmp .direct_exec_result

.lvar:
    call bas_exec_lvar
    jmp .direct_exec_result

.consolesize:
    call bas_exec_consolesize
    jmp .direct_exec_result

.direct_exec_result:
    jnc .done
    call bas_report_error
    jmp .done

.version:
    call bas_expect_end
    jc .command_error
    mov si, bas_s_banner
    call bas_append_line
    jmp .done

.capacity:
    call bas_expect_end
    jc .command_error
    call bas_show_capacity
    jmp .done

.mem:
    call bas_expect_end
    jc .command_error
    call bas_show_memory
    jmp .done

.dim:
    call bas_exec_dim
    jmp .direct_exec_result

.let:
    call bas_exec_assignment
    jmp .direct_exec_result

.direct_syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.command_error:
    call bas_report_error
    jmp .done

.numbered:
    mov si, bas_input
    call bas_store_line
    jc .numbered_error
    cmp byte [bas_auto_mode], 0
    je .done
    call bas_auto_advance
    jmp .done
.numbered_error:
    call bas_report_error

.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Small lexical helpers.
; -----------------------------------------------------------------------------
bas_skip_spaces:
    cmp byte [si], ' '
    jne .out
    inc si
    jmp bas_skip_spaces
.out:
    ret

; Case-insensitive keyword match.
; in: SI=input, DI=uppercase NUL keyword
; out: CF clear and SI after keyword on match; CF set and original SI otherwise
bas_match_word:
    push ax
    push bx
    mov bx, si
.loop:
    mov al, [di]
    test al, al
    jz .endword
    mov ah, [si]
    cmp ah, 'a'
    jb .cmp
    cmp ah, 'z'
    ja .cmp
    sub ah, 32
.cmp:
    cmp ah, al
    jne .fail
    inc si
    inc di
    jmp .loop
.endword:
    mov al, [si]
    cmp al, '0'
    jb .ok
    cmp al, '9'
    jbe .fail
    cmp al, 'A'
    jb .ok
    cmp al, 'Z'
    jbe .fail
    cmp al, '_'
    je .fail
    cmp al, 'a'
    jb .ok
    cmp al, 'z'
    jbe .fail
    jmp .ok
.fail:
    mov si, bx
    stc
    pop bx
    pop ax
    ret
.ok:
    clc
    pop bx
    pop ax
    ret

; Parse unsigned 16-bit decimal at SI.
; out: AX=value, SI advanced, CF set on missing digits/overflow
; Direct-command dispatch always retries from the start of the READY line.
; DI = uppercase keyword; output is the same as bas_match_word.
bas_match_direct_word:
    mov si, bas_input
    call bas_skip_spaces
    jmp bas_match_word

 ; Statement dispatch historically leaves DX holding the statement start on every
; keyword attempt. Preserve that side effect while removing redundant SI saves.
bas_match_statement_word:
    mov dx, si
    jmp bas_match_word

bas_parse_uword:
    push bx
    push cx
    push dx
    xor ax, ax
    xor cx, cx
.loop:
    mov bl, [si]
    cmp bl, '0'
    jb .done
    cmp bl, '9'
    ja .done
    sub bl, '0'
    xor bh, bh
    cmp ax, 6553
    ja .fail
    jne .safe
    cmp bx, 5
    ja .fail
.safe:
    mov dx, ax
    shl ax, 1
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add ax, dx
    add ax, bx
    inc si
    inc cx
    jmp .loop
.done:
    test cx, cx
    jz .fail
    clc
    pop dx
    pop cx
    pop bx
    ret
.fail:
    stc
    pop dx
    pop cx
    pop bx
    ret

bas_expect_end:
    call bas_skip_spaces
    cmp byte [si], 0
    je .ok
    cmp byte [si], 27h
    jne .fail
.ok:
    clc
    ret
.fail:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
    ret

; Parse one single-letter integer variable.
; out: BX = byte offset in bas_vars, AL = variable index 0..25
bas_parse_variable:
    call bas_skip_spaces
    mov al, [si]
    call bas_upper_al
    cmp al, 'A'
    jb .fail
    cmp al, 'Z'
    ja .fail
    sub al, 'A'
    xor ah, ah
    mov bx, ax
    shl bx, 1
    inc si
    clc
    ret
.fail:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
    ret

bas_parse_comma:
    call bas_skip_spaces
    cmp byte [si], ','
    jne .fail
    inc si
    clc
    ret
.fail:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
    ret

; Parse a non-negative integer expression.
bas_parse_coord:
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    clc
    ret
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
    ret

; QBasic-style point: (x,y). Parentheses are required.
; out: AX=x, BX=y
bas_parse_point:
    call bas_skip_spaces
    cmp byte [si], '('
    jne .syntax
    inc si
    call bas_parse_coord
    jc .fail
    push ax
    call bas_parse_comma
    jc .pop_fail
    call bas_parse_coord
    jc .pop_fail
    mov bx, ax
    pop ax
    call bas_skip_spaces
    cmp byte [si], ')'
    jne .syntax_after
    inc si
    clc
    ret
.pop_fail:
    pop ax
.fail:
    stc
    ret
.syntax_after:
    ; discard saved coordinate state is already gone here
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
    ret

; Parse optional ,colour after a graphics shape.
; out: DL=colour
bas_parse_optional_color:
    mov dl, [bas_fg_color]
    call bas_skip_spaces
    cmp byte [si], ','
    jne .end
    inc si
    call bas_parse_coord
    jc .fail
    cmp ax, 15
    ja .illegal
    mov dl, al
.end:
    call bas_expect_end
    ret
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
    ret

; ----------------------------------------------------------------------------
; HELP [topic] - 2BASIC's small disk-backed command reference.
; HELP.TXT lives at the app's launch directory (normally B:/). Each section begins with
; @TOPIC; subsequent lines are displayed until the next section header.
; No permanent 6 KB buffer: claim the usual I/O staging block only for HELP.
; This command runs on UI task; never invoke these FAT calls from the worker.
; ----------------------------------------------------------------------------
bas_help_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    xor ax, ax
    mov al, [bas_fg_color]
    push ax                   ; restore user-selected COLOR on every exit
    call bas_skip_spaces
    mov di, bas_help_topic
    xor bx, bx
    cmp byte [si], 0
    BAS_JNZ .topic_arg
    mov si, bas_s_help_index
.topic_arg:
    mov al, [si]
    test al, al
    BAS_JZ .topic_ready
    cmp al, ' '
    BAS_JZ .topic_ready
    cmp bx, 15
    BAS_JAE .unknown
    call bas_upper_al
    mov [di], al
    inc di
    inc si
    inc bx
    jmp .topic_arg
.topic_ready:
    mov byte [di], 0
    call bas_expect_end
    BAS_JC .syntax
    mov ax, BAS_HELP_KB
    call bas_claim_kb
    BAS_JC .mem_fail
    mov [bas_io_seg], dx
    mov es, dx
    xor bx, bx
    mov cx, BAS_HELP_CAP
    xor dx, dx
    ; HELP.TXT is always beside the app at the launch directory, even when
    ; a game's program directory is active. Restore it before examining CF.
    call OSAPI_FILE_HERE
    mov [bas_help_cluster], dx
    mov [bas_help_drive], bl
    mov dx, [bas_home_cluster]
    mov bl, [bas_home_drive]
    call OSAPI_FILE_GOTO_QM
    BAS_JC .read_fail
    mov es, [bas_io_seg]
    xor bx, bx
    mov cx, BAS_HELP_CAP
    xor dx, dx
    mov si, bas_s_help_file
    call OSAPI_FILE_READ
    pushf
    push ax
    push dx
    mov dx, [bas_help_cluster]
    mov bl, [bas_help_drive]
    call OSAPI_FILE_GOTO_QM
    pop dx
    pop ax
    popf
    BAS_JC .read_fail
    cmp dx, 0
    BAS_JNZ .bad_file
    cmp ax, BAS_HELP_CAP
    BAS_JAE .bad_file
    mov di, ax
    mov byte [es:di], 0        ; sentinel within the claimed block
    xor di, di
.find_section:
    cmp byte [es:di], 0
    BAS_JZ .unknown_file
    cmp byte [es:di], '@'
    BAS_JNZ .skip_section_line
    inc di
    mov si, bas_help_topic
.match_section:
    mov al, [si]
    test al, al
    BAS_JZ .name_end
    cmp al, [es:di]
    BAS_JNZ .skip_section_line
    inc si
    inc di
    jmp .match_section
.name_end:
    mov al, [es:di]
    cmp al, 13
    BAS_JZ .match
    cmp al, 10
    BAS_JNZ .skip_section_line
.match:
    ; A blank row above the reference, then a distinct foreground colour.
    push es
    push ds
    pop es
    mov si, bas_s_empty
    call bas_append_line
    pop es
    mov byte [bas_fg_color], CLBLUE
    call bas_help_skipline
.emit_loop:
    mov al, [es:di]
    test al, al
    BAS_JZ .done_file
    cmp al, '@'
    BAS_JZ .done_file
    xor bx, bx
.copy_line:
    mov al, [es:di]
    test al, al
    BAS_JZ .emit_line
    cmp al, 13
    BAS_JZ .emit_line
    cmp al, 10
    BAS_JZ .emit_line
    cmp bx, BAS_CON_COLS - 1
    BAS_JAE .skip_char
    mov [bas_scratch+bx], al
    inc bx
.skip_char:
    inc di
    jmp .copy_line
.emit_line:
    mov byte [bas_scratch+bx], 0
    test bx, bx
    BAS_JZ .next_line
    push es
    push ds
    pop es                  ; bas_append_line uses ES:DI writes
    mov si, bas_scratch
    call bas_append_line
    pop es
.next_line:
    call bas_help_skipline
    jmp .emit_loop
.skip_section_line:
    call bas_help_skipline
    jmp .find_section
.unknown_file:
    mov si, bas_s_help_topic
    jmp .report_file
.read_fail:
    push ax
    call bas_free_io
    pop ax
    cmp ax, FERR_NOENT
    BAS_JNZ .bad_read
    mov si, bas_s_no_help
    jmp .report
.bad_read:
    mov si, bas_s_bad_help
    jmp .report
.bad_file:
    mov si, bas_s_bad_help
    jmp .report_file
.done_file:
    ; The closing blank row belongs to the reference, not the next prompt.
    push es
    push ds
    pop es
    mov si, bas_s_empty
    call bas_append_line
    pop es
    call bas_free_io
    jmp .out
.report_file:
    push si
    call bas_free_io
    pop si
.report:
    push ds
    pop es
    call bas_append_line
    jmp .out
.mem_fail:
    mov si, bas_s_out_memory
    jmp .report
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    call bas_report_error
    jmp .out
.unknown:
    mov si, bas_s_help_topic
    jmp .report
.out:
    pop ax
    mov [bas_fg_color], al
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ES:DI points inside the temporary HELP file (ASCIIZ bounded by sentinel).
bas_help_skipline:
    push ax
.again:
    mov al, [es:di]
    test al, al
    jz .out
    inc di
    cmp al, 10
    jne .again
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; Packed program store. Each record is:
;   word line number, byte source length, source bytes, byte NUL
; Records are sorted by line number inside a 4864-byte arena. The separate
; 256-word index contains direct record pointers for execution and lookup.
; -----------------------------------------------------------------------------
bas_store_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call bas_skip_spaces
    call bas_parse_uword
    jc .syntax
    test ax, ax
    jz .syntax
    mov [bas_current_line], ax
    call bas_skip_spaces
    mov [bas_best_ptr], si

    xor cx, cx
.check_len:
    cmp byte [si], 0
    je .len_ok
    inc cx
    cmp cx, BAS_SRC_CAP
    ja .syntax
    inc si
    jmp .check_len
.len_ok:
    mov [bas_last_line], cx

    ; Find the first record whose line is >= the requested line.
    mov di, bas_program
    mov bx, [bas_program_used]
.find:
    test bx, bx
    jz .absent
    mov ax, [di]
    cmp ax, [bas_current_line]
    jae .position
    xor ax, ax
    mov al, [di + 2]
    add ax, 4
    add di, ax
    sub bx, ax
    jmp .find
.position:
    cmp ax, [bas_current_line]
    jne .absent
    xor bx, bx
    mov bl, [di + 2]
    add bx, 4                         ; BX = old record size
    jmp .have_position
.absent:
    xor bx, bx                        ; no old record
.have_position:
    mov [bas_draw_y0], di             ; insertion/replacement pointer
    mov [bas_draw_x0], bx             ; old size

    mov si, [bas_best_ptr]
    cmp byte [si], 0
    jne .write
    test bx, bx
    jz .ok                            ; deleting an absent line is harmless

    ; Delete the existing record by closing the gap.
    mov dx, di
    sub dx, bas_program
    add dx, bx                        ; tail offset
    mov cx, [bas_program_used]
    sub cx, dx                        ; tail bytes
    mov si, bas_program
    add si, dx
    mov di, [bas_draw_y0]
    cld
    rep movsb
    mov ax, [bas_program_used]
    sub ax, [bas_draw_x0]
    mov [bas_program_used], ax
    dec word [bas_program_count]
    call bas_rebuild_index
    jmp .ok

.write:
    mov ax, [bas_last_line]
    add ax, 4
    mov [bas_draw_x1], ax             ; new record size

    test bx, bx
    jnz .capacity
    cmp word [bas_program_count], BAS_PROG_LINES
    jae .full
.capacity:
    mov ax, [bas_program_used]
    sub ax, [bas_draw_x0]
    add ax, [bas_draw_x1]
    cmp ax, BAS_PROG_BYTES
    ja .full
    mov [bas_cap_tmp], ax             ; final used bytes

    mov ax, [bas_draw_x1]
    cmp ax, [bas_draw_x0]
    je .write_record
    ja .grow

    ; Shrink replacement: move the tail left.
    mov dx, [bas_draw_y0]
    sub dx, bas_program
    add dx, [bas_draw_x0]             ; old tail offset
    mov cx, [bas_program_used]
    sub cx, dx
    mov si, bas_program
    add si, dx
    mov di, [bas_draw_y0]
    add di, [bas_draw_x1]
    cld
    rep movsb
    jmp .write_record

.grow:
    ; Insert/grow: move the tail right, backwards to avoid overlap.
    mov dx, [bas_draw_y0]
    sub dx, bas_program
    add dx, [bas_draw_x0]             ; old tail offset
    mov cx, [bas_program_used]
    sub cx, dx
    jcxz .write_record
    mov si, bas_program
    add si, [bas_program_used]
    dec si
    mov di, si
    mov ax, [bas_draw_x1]
    sub ax, [bas_draw_x0]
    add di, ax
    std
    rep movsb
    cld

.write_record:
    mov di, [bas_draw_y0]
    mov ax, [bas_current_line]
    stosw
    mov ax, [bas_last_line]
    stosb
    mov si, [bas_best_ptr]
    mov cx, [bas_last_line]
    cld
    rep movsb
    xor al, al
    stosb
    mov ax, [bas_cap_tmp]
    mov [bas_program_used], ax
    cmp word [bas_draw_x0], 0
    jne .reindex
    inc word [bas_program_count]
.reindex:
    call bas_rebuild_index
.ok:
    clc
    jmp .out
.full:
    mov byte [bas_error_code], BERR_PROGRAMFULL
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
.out:
    cld
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Rebuild sorted direct pointers from the packed arena.
bas_rebuild_index:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, bas_program
    mov di, bas_line_index
    mov cx, [bas_program_used]
    xor bx, bx
.loop:
    test cx, cx
    jz .done
    cmp bx, BAS_PROG_LINES
    jae .done
    mov [di], si
    add di, 2
    inc bx
    xor ax, ax
    mov al, [si + 2]
    add ax, 4
    add si, ax
    sub cx, ax
    jmp .loop
.done:
    mov [bas_program_count], bx
    mov ax, [bas_program_used]
    cmp ax, BAS_PROG_BYTES
    jae .out
    mov si, bas_program
    add si, ax
    mov byte [si], 0
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

bas_new_program:
    push ax
    push cx
    push di
    xor ax, ax
    mov [bas_program_used], ax
    mov [bas_program_count], ax
    mov di, bas_program
    mov cx, BAS_PROG_BYTES / 2
    rep stosw
    mov di, bas_line_index
    mov cx, BAS_PROG_LINES
    rep stosw
    pop di
    pop cx
    pop ax
    ret

bas_clear_vars:
    push ax
    push cx
    push di
    xor ax, ax
    mov di, bas_vars
    mov cx, 26
    rep stosw
    mov [bas_array_used], ax
    mov di, bas_array_size
    mov cx, BAS_ARRAY_NAMES * 2 + BAS_ARRAY_ELEMS
    rep stosw
    pop di
    pop cx
    pop ax
    ret

bas_clear_graphics:
    push ax
    push cx
    push di
    cmp byte [bas_draw_batch], 0
    je .batch_ok
    mov byte [bas_batch_fast], 0       ; CLS/scene reset requires a true full repaint
.batch_ok:
    xor ax, ax
    mov [bas_gfx_count], ax
    mov di, bas_gfx_list
    mov cx, (BAS_GFX_MAX * BAS_GFX_REC_SIZE) / 2
    rep stosw
    mov byte [bas_dirty], 1
    pop di
    pop cx
    pop ax
    ret

bas_cls:
    push ax
    push cx
    push di
    xor ax, ax
    mov [bas_con_count], ax
    mov di, bas_console
    mov cx, (BAS_CON_LINES * BAS_CON_COLS) / 2
    rep stosw
    mov di, bas_con_fg
    mov cx, BAS_CON_LINES
    rep stosb
    mov di, bas_con_bg
    mov cx, BAS_CON_LINES
    rep stosb
    mov word [bas_out_row], 0
    mov word [bas_out_col], 0
    mov byte [bas_dirty], 1
    pop di
    pop cx
    pop ax
    call bas_clear_graphics
    ret

; Find the first line strictly greater than AX.
; out: SI=record, DX=line, CF clear; CF set when none
bas_find_next_after:
    push ax
    push bx
    push cx
    push di
    mov bx, ax
    mov di, bas_line_index
    mov cx, [bas_program_count]
.scan:
    jcxz .none
    mov si, [di]
    mov dx, [si]
    cmp dx, bx
    ja .found
    add di, 2
    loop .scan
.none:
    stc
    jmp .out
.found:
    clc
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; Find exact line AX.
; out: SI=record, CF clear; CF set if absent
bas_find_exact:
    push ax
    push cx
    push di
    mov di, bas_line_index
    mov cx, [bas_program_count]
.scan:
    jcxz .none
    mov si, [di]
    cmp [si], ax
    je .found
    ja .none
    add di, 2
    loop .scan
.none:
    stc
    jmp .out
.found:
    clc
.out:
    pop di
    pop cx
    pop ax
    ret

; List inclusive line range AX..BX. Long source lines wrap safely.
bas_list_program:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [bas_draw_x0], ax
    mov [bas_draw_x1], bx
    test ax, ax
    jz .start_zero
    dec ax
.start_zero:
    mov [bas_last_line], ax
.next_line:
    mov ax, [bas_last_line]
    call bas_find_next_after
    jc .done
    cmp dx, [bas_draw_x1]
    ja .done
    mov [bas_last_line], dx
    add si, 3
    mov [bas_best_ptr], si
    mov bp, 1
.chunk:
    mov di, bas_scratch
    cmp bp, 1
    jne .continuation
    mov ax, [bas_last_line]
    call bas_word_to_dec
    mov al, ' '
    stosb
    mov cx, BAS_CON_COLS - 7
    jmp .copy
.continuation:
    mov al, ' '
    stosb
    stosb
    mov cx, BAS_CON_COLS - 3
.copy:
    mov si, [bas_best_ptr]
.copy_loop:
    mov al, [si]
    test al, al
    jz .emit_last
    stosb
    inc si
    loop .copy_loop
    mov [bas_best_ptr], si
    mov byte [di], 0
    mov si, bas_scratch
    call bas_append_line
    xor bp, bp
    jmp .chunk
.emit_last:
    mov byte [di], 0
    mov si, bas_scratch
    call bas_append_line
    jmp .next_line
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Delete inclusive range AX..BX from the packed store.
bas_delete_range:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [bas_draw_x0], ax
    mov [bas_draw_y1], bx
    test ax, ax
    jz .start
    dec ax
.start:
    mov [bas_last_line], ax
.next:
    mov ax, [bas_last_line]
    call bas_find_next_after
    jc .done
    cmp dx, [bas_draw_y1]
    ja .done
    mov [bas_last_line], dx
    mov di, bas_scratch
    mov ax, dx
    call bas_word_to_dec
    mov byte [di], 0
    mov si, bas_scratch
    call bas_store_line
    jc .done
    jmp .next
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Copy a stored line into the command editor for replacement.
; in: AX=line. CF set if absent.
bas_prepare_edit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, ax
    call bas_find_exact
    jc .missing
    mov di, bas_input
    mov ax, dx
    call bas_word_to_dec
    mov al, ' '
    stosb
    add si, 3
.copy:
    lodsb
    stosb
    test al, al
    jnz .copy
    mov ax, di
    sub ax, bas_input
    dec ax
    mov [bas_input_len], ax
    mov [bas_input_pos], ax
    mov byte [bas_input_ready], 1
    clc
    jmp .out
.missing:
    mov byte [bas_error_code], BERR_UNDEFLINE
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Prepare the next AUTO numbered prompt.
bas_prepare_auto:
    push ax
    push di
    mov di, bas_input
    mov ax, [bas_auto_next]
    call bas_word_to_dec
    mov al, ' '
    stosb
    mov byte [di], 0
    mov ax, di
    sub ax, bas_input
    mov [bas_input_len], ax
    mov [bas_input_pos], ax
    mov byte [bas_input_ready], 1
    pop di
    pop ax
    ret

bas_auto_advance:
    push ax
    mov ax, [bas_auto_next]
    add ax, [bas_auto_step]
    jc .off
    test ax, ax
    jz .off
    mov [bas_auto_next], ax
    call bas_prepare_auto
    jmp .out
.off:
    mov byte [bas_auto_mode], 0
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; Responsive RUN engine. RUN is started by the UI callback, while one persistent
; worker executes statements outside the window task. The worker never returns.
; -----------------------------------------------------------------------------
bas_start_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    cmp byte [bas_run_state], BAS_STATE_READY
    jne .worker_error
    call bas_clear_vars
    mov byte [bas_draw_batch], 0
    mov word [bas_gosub_count], 0
    mov word [bas_for_count], 0
    mov word [bas_current_line], 0
    mov byte [bas_break_req], 0
    ; Permit an immediate first viewport paint, then cap expensive scrolling.
    call OSAPI_GET_TICKS
    sub ax, 2
    mov [bas_view_last_tick], ax
    mov word [bas_last_key], 0
    mov word [bas_input_len], 0
    mov word [bas_input_pos], 0
    mov byte [bas_input], 0

    ; File slots are UI-task-only. Preload literal LOADBMP/LOADRES declarations before
    ; the worker begins; the worker later validates the populated slot and
    ; treats that statement as a no-op. Use each image slot once per RUN.
    call bas_preload_run_images
    jc .error

    cmp byte [bas_worker_started], 0
    jne .start
    mov ax, bas_worker
    mov bx, [bas_window]
    call OSAPI_TASK_SPAWN
    jc .worker_error
    ; Restartable (SPEC.md 66.6.2): bas_worker parks only in TASK_ALIVE at
    ; the top of its loop, and every interpreter register it needs across
    ; statements lives in bss, so a restart costs one iteration.
    OS88_WORKER_RESTARTABLE bas_worker
    mov byte [bas_worker_started], 1
.start:
    mov byte [bas_run_state], BAS_STATE_RUNNING
    ; While running, retain the loaded program name in the caption bytes.
    ; READY restores the base title in bas_worker_finish_ready.
    cmp byte [bas_filename], 0
    je .run_untitled
    call bas_title_filename
    jmp .run_title_done
.run_untitled:
    call bas_title_untitled
.run_title_done:
    ; Text-mode RUN has already dirtied the echoed command/blank separator.
    ; Preserve that incremental request; a full erase at every RUN start
    ; causes an avoidable whole-console flash on the XT. Graphics mode still
    ; requires a proper retained-canvas invalidation.
    cmp byte [bas_screen_mode], 0
    je .ready
    mov byte [bas_dirty], 1
.ready:
    clc
    jmp .out

.worker_error:
    mov byte [bas_error_code], BERR_NOWORKER
.error:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_preload_run_images:
    push ax
    push dx
    push si
    push di
    mov word [bas_current_line], 0
.next:
    mov ax, [bas_current_line]
    call bas_find_next_after
    jc .done
    mov [bas_current_line], dx
    add si, 3
    call bas_skip_spaces
    push si
    mov di, bas_kw_loadbmp
    call bas_match_word
    jc .try_resource
    add sp, 2
    call bas_exec_loadbmp
    jc .fail
    jmp .next
.try_resource:
    pop si
    push si
    mov di, bas_kw_loadres
    call bas_match_word
    jc .not_image
    add sp, 2
    call bas_exec_loadres
    jc .fail
    jmp .next
.not_image:
    pop si
    jmp .next
.done:
    mov word [bas_current_line], 0
    clc
    jmp .out
.fail:
    stc
.out:
    pop di
    pop si
    pop dx
    pop ax
    ret

bas_worker:
    ; Worker is entered by the kernel too. Establish a permanent local
    ; destination segment for the interpreter's MOVS/STOS operations.
    push ds
    pop es
.loop:
    mov bx, [bas_window]
    call OSAPI_TASK_ALIVE

    cmp byte [bas_run_state], BAS_STATE_RUNNING
    je .running
    call bas_worker_redraw
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp .loop

.running:
    cmp byte [bas_break_req], 0
    jne .break
    mov byte [bas_exec_busy], 1
    mov byte [bas_worker_active], 1

    mov ax, [bas_current_line]
    call bas_find_next_after
    jc .natural_end
    mov [bas_current_line], dx
    add si, 3
    call bas_exec_statement
    pushf
    mov byte [bas_worker_active], 0
    mov byte [bas_exec_busy], 0
    popf
    jc .runtime_error
    test al, al
    jnz .normal_end

    call bas_worker_redraw
    call OSAPI_TASK_YIELD
    jmp .loop

.natural_end:
    mov byte [bas_worker_active], 0
    mov byte [bas_exec_busy], 0
.normal_end:
    call bas_worker_finish_ready
    jmp .loop

.runtime_error:
    call bas_report_error
    call bas_worker_finish_ready
    jmp .loop

.break:
    mov byte [bas_error_code], BERR_BREAK
    call bas_report_error
    call bas_worker_finish_ready
    jmp .loop

bas_worker_finish_ready:
    mov byte [bas_draw_batch], 0
    mov byte [bas_worker_active], 0
    mov byte [bas_exec_busy], 0
    mov byte [bas_break_req], 0
    mov byte [bas_run_state], BAS_STATE_READY
    mov word [bas_input_len], 0
    mov word [bas_input_pos], 0
    mov byte [bas_input], 0
    call bas_fm_alloff_quiet
    call bas_gui_finish_program
    ; A finished, errored or interrupted RUN always restores the default
    ; caption. Do not clear the loaded program or filename in memory.
    call bas_title_untitled
    mov si, bas_s_ready
    call bas_append_line
    ; A text-mode program finishes by painting READY and the prompt only.
    ; SCREEN 1 still needs a complete retained canvas repaint when finishing.
    cmp byte [bas_screen_mode], 0
    je .text_done
    mov byte [bas_dirty], 1
.text_done:
    call bas_worker_redraw
    ret

bas_worker_redraw:
    cmp byte [bas_var_dirty], 0
    je .no_var_wake
    push bx
    mov bx, [bas_window]
    call OSAPI_WM_WAKE
    pop bx
.no_var_wake:
    cmp byte [bas_draw_batch], 0
    jne .out
    mov al, [bas_dirty]
    test al, al
    jz .out
    ; On a real XT, repainting all 35 console rows after EVERY PRINT can
    ; dominate program execution. Coalesce text-viewport overflow paints to
    ; at most one per two 18.2 Hz ticks (~9 fps), keeping the pending dirty
    ; state intact until it is presented. Final READY state always flushes.
    cmp al, 5
    jne .present
    cmp byte [bas_run_state], BAS_STATE_RUNNING
    jne .present
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [bas_view_last_tick]
    cmp ax, 2
    jae .view_due
    pop bx
    jmp .out
.view_due:
    mov [bas_view_last_tick], bx
    pop bx
.present:
    ; GET_TICKS answered in AX: reload the saved dirty kind before it becomes
    ; the renderer dispatch selector. Otherwise a tick value can select FULL.
    mov al, [bas_dirty]
    ; SDK 0.2: the retired double-buffer API cannot be used. The kernel now
    ; renders the locked, clipped dirty span or retained graphics delta
    ; directly. No clear is issued on the two incremental paths.
    mov byte [bas_dirty], 0
    push ax
    call OSAPI_GFX_LOCK
    mov bx, [bas_window]
    call OSAPI_WM_GEOM
    jc .discard
    call OSAPI_WM_CLIP_SET
    jc .discard
    pop ax
    cmp al, 4
    je .console_rows
    cmp al, 5
    je .console_view
    cmp al, 3
    je .delta
    cmp al, 2
    je .span
    ; A full canvas invalidation still requires complete retained repaint.
    mov si, [bas_window]
    call bas_paint
    jmp .unlock
.span:
    call bas_redraw_console_span
    jmp .unlock
.console_rows:
    call bas_redraw_console_rows
    cmp byte [bas_run_state], BAS_STATE_RUNNING
    je .unlock
    call bas_redraw_prompt
    jmp .unlock
.console_view:
    call bas_redraw_console_view
    cmp byte [bas_run_state], BAS_STATE_RUNNING
    je .unlock
    call bas_redraw_prompt
    jmp .unlock
.delta:
    call bas_replay_graphics_delta
    jmp .unlock
.discard:
    pop ax
.unlock:
    call OSAPI_GFX_UNLOCK
.out:
    ret

; Execute one stored statement at SI.
; out: AL=0 continue, AL=1 END; CF set on error
bas_exec_statement:
    call bas_skip_spaces
    cmp byte [si], 0
    je .continue
    cmp byte [si], 27h
    je .continue
    cmp byte [si], '?'
    jne .check_rem
    inc si
    jmp .do_print
.check_rem:

    mov di, bas_kw_rem
    call bas_match_statement_word
    jnc .continue

    mov di, bas_kw_cls
    call bas_match_statement_word
    jnc .do_cls

    mov di, bas_kw_print
    call bas_match_statement_word
    jnc .do_print

    mov di, bas_kw_input
    call bas_match_statement_word
    jnc .do_input

    mov di, bas_kw_end
    call bas_match_statement_word
    jnc .do_end

    mov di, bas_kw_goto
    call bas_match_statement_word
    jnc .do_goto

    mov di, bas_kw_if
    call bas_match_statement_word
    jnc .do_if

    mov di, bas_kw_on
    call bas_match_statement_word
    jnc .do_on

    mov di, bas_kw_dim
    call bas_match_statement_word
    jnc .do_dim

    mov di, bas_kw_let
    call bas_match_statement_word
    jnc .do_let

    mov di, bas_kw_gosub
    call bas_match_statement_word
    jnc .do_gosub

    mov di, bas_kw_return
    call bas_match_statement_word
    jnc .do_return

    mov di, bas_kw_for
    call bas_match_statement_word
    jnc .do_for

    mov di, bas_kw_next
    call bas_match_statement_word
    jnc .do_next

    mov di, bas_kw_promptcolor
    call bas_match_statement_word
    jnc .do_promptcolor

    mov di, bas_kw_color
    call bas_match_statement_word
    jnc .do_color

    mov di, bas_kw_locate
    call bas_match_statement_word
    jnc .do_locate

    mov di, bas_kw_screen
    call bas_match_statement_word
    jnc .do_screen

    mov di, bas_kw_pset
    call bas_match_statement_word
    jnc .do_pset

    mov di, bas_kw_line
    call bas_match_statement_word
    jnc .do_line

    mov di, bas_kw_box
    call bas_match_statement_word
    jnc .do_box

    mov di, bas_kw_fillbox
    call bas_match_statement_word
    jnc .do_fillbox

    mov di, bas_kw_circle
    call bas_match_statement_word
    jnc .do_circle

    mov di, bas_kw_beep
    call bas_match_statement_word
    jnc .do_beep

    mov di, bas_kw_sound
    call bas_match_statement_word
    jnc .do_sound

    mov di, bas_kw_randomize
    call bas_match_statement_word
    jnc .do_randomize

    mov di, bas_kw_sleep
    call bas_match_statement_word
    jnc .do_sleep

    mov di, bas_kw_poke
    call bas_match_statement_word
    jnc .do_poke

    mov di, bas_kw_fmpreset
    call bas_match_statement_word
    jnc .do_fmpreset

    mov di, bas_kw_fmnote
    call bas_match_statement_word
    jnc .do_fmnote

    mov di, bas_kw_fmoff
    call bas_match_statement_word
    jnc .do_fmoff

    mov di, bas_kw_fmstop
    call bas_match_statement_word
    jnc .do_fmstop

    mov di, bas_kw_loadbmp
    call bas_match_statement_word
    jnc .do_loadbmp

    mov di, bas_kw_loadres
    call bas_match_statement_word
    jnc .do_loadres

    mov di, bas_kw_putres
    call bas_match_statement_word
    jnc .do_putres

    ; Recognize RESSELECT in numbered programs and at the direct prompt.
    mov di, bas_kw_resselect
    call bas_match_statement_word
    jnc .do_resselect

    mov di, bas_kw_putbmp
    call bas_match_statement_word
    jnc .do_putbmp

    mov di, bas_kw_freebmp
    call bas_match_statement_word
    jnc .do_freebmp

    mov di, bas_kw_drawbegin
    call bas_match_statement_word
    jnc .do_drawbegin

    mov di, bas_kw_drawend
    call bas_match_statement_word
    jnc .do_drawend

    mov di, bas_kw_form
    call bas_match_statement_word
    jnc .do_form

    mov di, bas_kw_label
    call bas_match_statement_word
    jnc .do_label

    mov di, bas_kw_button
    call bas_match_statement_word
    jnc .do_button

    mov di, bas_kw_checkbox
    call bas_match_statement_word
    jnc .do_checkbox

    mov di, bas_kw_frame
    call bas_match_statement_word
    jnc .do_frame

    mov di, bas_kw_image
    call bas_match_statement_word
    jnc .do_image

    mov di, bas_kw_showform
    call bas_match_statement_word
    jnc .do_showform

    mov di, bas_kw_closeform
    call bas_match_statement_word
    jnc .do_closeform

    mov di, bas_kw_settext
    call bas_match_statement_word
    jnc .do_settext

    mov di, bas_kw_setvalue
    call bas_match_statement_word
    jnc .do_setvalue

    mov di, bas_kw_enable
    call bas_match_statement_word
    jnc .do_enable

    mov di, bas_kw_visible
    call bas_match_statement_word
    jnc .do_visible

    mov di, bas_kw_messagebox
    call bas_match_statement_word
    jnc .do_messagebox

    mov di, bas_kw_inputbox
    call bas_match_statement_word
    jnc .do_inputbox

    mov di, bas_kw_svar
    call bas_match_statement_word
    jnc .do_svar

    mov di, bas_kw_lvar
    call bas_match_statement_word
    jnc .do_lvar

    mov di, bas_kw_consolesize
    call bas_match_statement_word
    jnc .do_consolesize

    mov di, bas_kw_minconsole
    call bas_match_statement_word
    jnc .do_minconsole

    mov di, bas_kw_restoreconsole
    call bas_match_statement_word
    jnc .do_restoreconsole

    call bas_exec_assignment
    jc .fail
.normal_result:
    jc .fail
.continue:
    xor al, al
    clc
    ret
.do_cls:
    call bas_expect_end
    jc .fail
    call bas_cls
    xor al, al
    clc
    ret
.do_print:
    call bas_exec_print
    jmp .normal_result
.do_input:
    call bas_exec_input
    jmp .normal_result
.do_end:
    call bas_expect_end
    jc .fail
    mov al, 1
    clc
    ret
.do_goto:
    call bas_exec_goto
    jmp .normal_result
.do_if:
    ; IF may execute an inline END, so preserve the nested statement's AL result
    ; instead of forcing the normal continue result.
    call bas_exec_if
    jc .fail
    ret
.do_on:
    call bas_exec_on
    jmp .normal_result
.do_dim:
    call bas_exec_dim
    jmp .normal_result
.do_let:
    call bas_exec_assignment
    jmp .normal_result
.do_gosub:
    call bas_exec_gosub
    jmp .normal_result
.do_return:
    call bas_exec_return
    jmp .normal_result
.do_for:
    call bas_exec_for
    jmp .normal_result
.do_next:
    call bas_exec_next
    jmp .normal_result
.do_promptcolor:
    call bas_exec_promptcolor
    jmp .normal_result

.do_color:
    call bas_exec_color
    jmp .normal_result
.do_locate:
    call bas_exec_locate
    jmp .normal_result
.do_screen:
    call bas_exec_screen
    jmp .normal_result
.do_pset:
    mov al, GFX_PSET
    call bas_exec_graphics
    jmp .normal_result
.do_line:
    mov al, GFX_LINE
    call bas_exec_graphics
    jmp .normal_result
.do_box:
    mov al, GFX_BOX
    call bas_exec_graphics
    jmp .normal_result
.do_fillbox:
    mov al, GFX_FILLBOX
    call bas_exec_graphics
    jmp .normal_result
.do_circle:
    mov al, GFX_CIRCLE
    call bas_exec_graphics
    jmp .normal_result
.do_beep:
    call bas_exec_beep
    jmp .normal_result
.do_sound:
    call bas_exec_sound
    jmp .normal_result
.do_randomize:
    call bas_exec_randomize
    jmp .normal_result
.do_sleep:
    call bas_exec_sleep
    jmp .normal_result
.do_poke:
    call bas_exec_poke
    jmp .normal_result
.do_fmpreset:
    call bas_exec_fmpreset
    jmp .normal_result
.do_fmnote:
    call bas_exec_fmnote
    jmp .normal_result
.do_fmoff:
    call bas_exec_fmoff
    jmp .normal_result
.do_fmstop:
    call bas_exec_fmstop
    jmp .normal_result
.do_loadbmp:
    call bas_exec_loadbmp
    jmp .normal_result
.do_loadres:
    call bas_exec_loadres
    jmp .normal_result
.do_putres:
    call bas_exec_putres
    jmp .normal_result
.do_resselect:
    call bas_exec_resselect
    jmp .normal_result
.do_putbmp:
    call bas_exec_putbmp
    jmp .normal_result
.do_freebmp:
    call bas_exec_freebmp
    jmp .normal_result
.do_drawbegin:
    call bas_expect_end
    jc .fail
    ; A batch that does not clear/rebuild the scene can be committed as a
    ; small retained delta. This is the XT fast path: the records still join
    ; the retained scene for later window repaints, but DRAWEND need only draw
    ; the commands that actually changed on the live canvas.
    mov byte [bas_draw_batch], 1
    mov byte [bas_batch_fast], 1
    mov word [bas_gfx_delta_count], 0
    mov byte [bas_dirty], 0
    xor al, al
    clc
    ret
.do_drawend:
    call bas_expect_end
    jc .fail
    mov byte [bas_draw_batch], 0
    cmp byte [bas_batch_fast], 0
    je .drawend_full
    cmp word [bas_gfx_delta_count], 0
    je .drawend_keep_dirty
    mov byte [bas_dirty], 3
    jmp .drawend_done
.drawend_full:
    mov byte [bas_dirty], 1
    jmp .drawend_done
.drawend_keep_dirty:
    ; Text-only/no-op batches keep whatever dirty state their statements made.
.drawend_done:
    xor al, al
    clc
    ret
.do_form:
    call bas_exec_form
    jmp .gui_done
.do_label:
    mov al, GUI_LABEL
    call bas_exec_add_control
    jmp .gui_done
.do_button:
    mov al, GUI_BUTTON
    call bas_exec_add_control
    jmp .gui_done
.do_checkbox:
    mov al, GUI_CHECKBOX
    call bas_exec_add_control
    jmp .gui_done
.do_frame:
    mov al, GUI_FRAME
    call bas_exec_add_control
    jmp .gui_done
.do_image:
    mov al, GUI_IMAGE
    call bas_exec_add_control
    jmp .gui_done
.do_showform:
    call bas_exec_showform
    jmp .gui_done
.do_closeform:
    call bas_exec_closeform
    jmp .gui_done
.do_settext:
    call bas_exec_settext
    jmp .gui_done
.do_setvalue:
    mov al, 2
    call bas_exec_control_state
    jmp .gui_done
.do_enable:
    xor al, al
    call bas_exec_control_state
    jmp .gui_done
.do_visible:
    mov al, 1
    call bas_exec_control_state
    jmp .gui_done
.do_messagebox:
    call bas_exec_messagebox
    jmp .gui_done
.do_inputbox:
    call bas_exec_inputbox
    jmp .gui_done
.do_svar:
    call bas_exec_svar
    jmp .gui_done
.do_lvar:
    call bas_exec_lvar
    jmp .gui_done
.do_consolesize:
    call bas_exec_consolesize
    jmp .gui_done
.do_minconsole:
    call bas_exec_minconsole
    jmp .gui_done
.do_restoreconsole:
    call bas_exec_restoreconsole
.gui_done:
    jc .fail
    xor al, al
    clc
    ret
.fail:
    stc
    ret

bas_exec_print:
    push ax
    push bx
    push cx
    push dx
    push di

    mov byte [bas_print_nonl], 0
    call bas_skip_spaces
    mov di, bas_scratch
    cmp byte [si], 0
    je .empty
    cmp byte [si], 27h
    jne .item
.empty:
    mov byte [di], 0
    mov si, bas_scratch
    call bas_console_print
    clc
    jmp .out

.item:
    call bas_skip_spaces
    cmp byte [si], 27h
    je .emit
    cmp byte [si], '"'
    je .strexpr
    push si
    push di
    mov di, bas_kw_strs
    call bas_match_word
    pop di
    pop si
    jnc .strexpr
    ; RESLINE$ is a string expression, not a numeric function.
    push si
    push di
    mov di, bas_kw_resline
    call bas_match_word
    pop di
    pop si
    jnc .strexpr
    jmp .expr
.strexpr:
    mov cx, bas_scratch + BAS_SCRATCH_CAP
    sub cx, di
    call bas_parse_strexpr
    jc .fail
    jmp .separator

.expr:
    push di
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    pop di
    jc .fail
    cmp di, bas_scratch + BAS_SCRATCH_CAP - 8
    jae .overflow
    call bas_int_to_dec

.separator:
    call bas_skip_spaces
    cmp byte [si], ';'
    jne .endcheck
    inc si
    call bas_skip_spaces
    cmp byte [si], 0
    je .trailing
    cmp byte [si], 27h
    jne .item
.trailing:
    mov byte [bas_print_nonl], 1
    jmp .emit
.endcheck:
    cmp byte [si], 0
    je .emit
    cmp byte [si], 27h
    jne .syntax
.emit:
    mov byte [di], 0
    mov si, bas_scratch
    call bas_console_print
    clc
    jmp .out

.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; INPUT ["prompt";] variable
; The worker parks in BAS_STATE_INPUT. The UI callback accepts an integer
; expression and resumes execution at the line following INPUT.
bas_exec_input:
    push ax
    push bx
    push di

    cmp byte [bas_worker_active], 0
    je .program_only
    call bas_skip_spaces
    cmp byte [si], '"'
    jne .variable
    inc si
    mov di, bas_scratch
.copy_prompt:
    mov al, [si]
    cmp al, 0
    je .syntax
    cmp al, '"'
    je .prompt_end
    cmp di, bas_scratch + BAS_SCRATCH_CAP - 2
    jae .overflow
    stosb
    inc si
    jmp .copy_prompt
.prompt_end:
    inc si
    mov byte [di], 0
    call bas_skip_spaces
    cmp byte [si], ';'
    je .prompt_sep
    cmp byte [si], ','
    jne .syntax
.prompt_sep:
    inc si
    push si
    mov si, bas_scratch
    call bas_append_line
    pop si
.variable:
    call bas_parse_variable
    jc .fail
    mov [bas_input_var], bx
    call bas_expect_end
    jc .fail
    mov word [bas_input_len], 0
    mov word [bas_input_pos], 0
    mov byte [bas_input], 0
    mov byte [bas_run_state], BAS_STATE_INPUT
    mov byte [bas_dirty], 1
    clc
    jmp .out
.program_only:
    mov byte [bas_error_code], BERR_PROGMODE
    stc
    jmp .out
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop di
    pop bx
    pop ax
    ret

; LOCATE row,column. Rows are 1..52 and columns 1..63.
bas_exec_locate:
    push ax
    push bx
    push dx
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    cmp ax, 1
    jb .illegal
    cmp ax, BAS_CON_LINES
    ja .illegal
    dec ax
    mov [bas_draw_x0], ax
    call bas_parse_comma
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    cmp ax, 1
    jb .illegal
    cmp ax, BAS_CON_COLS - 1
    ja .illegal
    dec ax
    mov [bas_out_col], ax
    mov dx, [bas_draw_x0]
    mov [bas_out_row], dx
    call bas_expect_end
    jc .fail
    call bas_console_ensure_cursor
    clc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; SLEEP ticks is worker-only and yields the task for approximately ticks/18 sec.
bas_exec_sleep:
    push ax
    cmp byte [bas_worker_active], 0
    je .program_only
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    mov [bas_steps], ax
    call bas_expect_end
    jc .fail
    cmp word [bas_steps], 0
    jne .sleep_loop
    call OSAPI_TASK_YIELD
    clc
    jmp .out
.sleep_loop:
    cmp byte [bas_break_req], 0
    jne .done
    mov ax, 1
    call OSAPI_TASK_SLEEP
    dec word [bas_steps]
    jnz .sleep_loop
.done:
    clc
    jmp .out
.program_only:
    mov byte [bas_error_code], BERR_PROGMODE
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop ax
    ret

bas_exec_dim:
    push ax
    push bx
    push cx
    push dx
    push di
    call bas_skip_spaces
    mov al, [si]
    cmp al, 'a'
    jb .upper
    cmp al, 'z'
    ja .upper
    sub al, 32
.upper:
    cmp al, 'A'
    jb .syntax
    cmp al, 'Z'
    ja .syntax
    sub al, 'A'
    xor ah, ah
    mov dx, ax
    shl dx, 1
    inc si
    call bas_skip_spaces
    cmp byte [si], '('
    jne .syntax
    inc si
    mov word [bas_expr_depth], 0
    ; The recursive expression parser consumes DX/DL. Retain the original
    ; array name's word offset across index/bounds expression evaluation.
    push dx
    call bas_parse_expr
    pop dx                       ; POP does not alter the parser's carry flag
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, BAS_ARRAY_ELEMS - 1
    ja .illegal
    inc ax
    mov cx, ax
    call bas_skip_spaces
    cmp byte [si], ')'
    jne .syntax
    inc si
    call bas_expect_end
    jc .fail
    mov bx, dx
    cmp word [bas_array_size + bx], 0
    jne .illegal
    mov ax, [bas_array_used]
    add ax, cx
    cmp ax, BAS_ARRAY_ELEMS
    ja .nomem
    mov [bas_array_size + bx], cx
    mov ax, [bas_array_used]
    mov [bas_array_base + bx], ax
    add [bas_array_used], cx
    shl ax, 1
    mov di, bas_array_data
    add di, ax
    xor ax, ax
    rep stosw
    clc
    jmp .out
.nomem:
    mov byte [bas_error_code], BERR_NOMEM
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Parse a scalar or one-dimensional array assignment target.
; out: BX = absolute near pointer to signed word, SI advanced.
bas_parse_target:
    push ax
    push cx
    push dx
    call bas_skip_spaces
    mov al, [si]
    cmp al, 'a'
    jb .upper
    cmp al, 'z'
    ja .upper
    sub al, 32
.upper:
    cmp al, 'A'
    jb .syntax
    cmp al, 'Z'
    ja .syntax
    sub al, 'A'
    xor ah, ah
    mov dx, ax
    shl dx, 1
    inc si
    call bas_skip_spaces
    cmp byte [si], '('
    je .array
    mov bx, bas_vars
    add bx, dx
    clc
    jmp .out
.array:
    inc si
    mov word [bas_expr_depth], 0
    ; The recursive expression parser consumes DX/DL. Retain the original
    ; array name's word offset across index/bounds expression evaluation.
    push dx
    call bas_parse_expr
    pop dx                       ; POP does not alter the parser's carry flag
    jc .fail
    test ax, ax
    js .illegal
    mov cx, ax
    call bas_skip_spaces
    cmp byte [si], ')'
    jne .syntax
    inc si
    mov bx, dx
    mov ax, [bas_array_size + bx]
    test ax, ax
    jz .illegal
    cmp cx, ax
    jae .illegal
    mov ax, [bas_array_base + bx]
    add ax, cx
    shl ax, 1
    mov bx, bas_array_data
    add bx, ax
    clc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

bas_exec_assignment:
    push ax
    push bx
    push dx
    push di

    call bas_parse_target
    jc .fail
    push bx
    call bas_skip_spaces
    cmp byte [si], '='
    jne .target_syntax
    inc si
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .expr_fail
    push ax
    call bas_expect_end
    pop ax
    jc .expr_fail
    pop bx
    mov [bx], ax
    clc
    jmp .out
.target_syntax:
    pop bx
    mov byte [bas_error_code], BERR_SYNTAX
    stc
    jmp .out
.expr_fail:
    pop bx
.fail:
    stc
.out:
    pop di
    pop dx
    pop bx
    pop ax
    ret

bas_exec_goto:
    push ax
    push dx
    call bas_skip_spaces
    call bas_parse_uword
    jc .syntax
    test ax, ax
    jz .syntax
    mov dx, ax
    call bas_expect_end
    jc .fail
    mov ax, dx
    call bas_find_exact
    jc .undef
    dec dx
    mov [bas_current_line], dx
    clc
    jmp .out
.undef:
    mov byte [bas_error_code], BERR_UNDEFLINE
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop dx
    pop ax
    ret

bas_exec_if:
    push bx
    push dx
    push di

    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .out
    mov dx, ax
    call bas_skip_spaces
    mov di, bas_kw_then
    call bas_match_word
    jc .syntax
    call bas_skip_spaces
    cmp byte [si], 0
    je .syntax

    ; Keep the compact feature intentionally non-recursive. This avoids the
    ; classic dangling-ELSE ambiguity without adding a block/control stack.
    push si
    mov di, bas_kw_if
    call bas_match_word
    pop si
    jnc .syntax

    ; Find an optional ELSE outside quoted strings/comments. BX is zero when
    ; absent and SI is restored to the first THEN-branch character.
    call bas_find_if_else
    jc .syntax
    test dx, dx
    jz .false_branch

    ; True branch. Temporarily terminate it at ELSE so every existing statement
    ; parser can be reused unchanged. Restore the source byte even on failure.
    test bx, bx
    jz .true_whole
    push bx
    mov byte [bx], 0
    call bas_exec_if_branch
    pop bx
    mov byte [bx], 'E'
    jmp .out
.true_whole:
    call bas_exec_if_branch
    jmp .out

.false_branch:
    test bx, bx
    jz .no_branch
    mov si, bx
    add si, 4
    call bas_exec_if_branch
    jmp .out
.no_branch:
    xor al, al
    clc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
.out:
    pop di
    pop dx
    pop bx
    ret

; Execute one selected inline IF branch. A leading decimal number retains the
; original BASIC shorthand meaning GOTO line; otherwise dispatch an ordinary
; 2BASIC statement. Nested inline IF is intentionally rejected by bas_exec_if.
bas_exec_if_branch:
    call bas_skip_spaces
    mov al, [si]
    cmp al, '0'
    jb .statement
    cmp al, '9'
    ja .statement
    call bas_exec_goto
    jc .fail
    xor al, al
    clc
.fail:
    ret
.statement:
    call bas_exec_statement
    ret

; Locate an ELSE keyword belonging to this single-line IF. Quoted text is
; skipped, and apostrophe/REM comments consume the remainder of the branch.
; in: SI = first THEN-branch character
; out: BX = ELSE keyword address or 0; SI restored; CF set on malformed shape
bas_find_if_else:
    push ax
    push cx
    push dx
    push di
    push bp
    call bas_skip_spaces
    mov bp, si
    cmp byte [si], 0
    jne .branch_present
    jmp .bad
.branch_present:

    ; REM starts a comment statement, so an ELSE token inside its text is data.
    push si
    mov di, bas_kw_rem
    call bas_match_word
    pop si
    jc .not_rem
    jmp .none
.not_rem:
    mov bx, bp
.scan:
    mov al, [bx]
    test al, al
    jz .none
    cmp al, 27h
    je .none
    cmp al, '"'
    je .quote

    ; ELSE must start at a word boundary. bas_match_word checks the trailing
    ; boundary, so only the preceding character needs checking here.
    cmp bx, bp
    je .try_else
    mov al, [bx - 1]
    cmp al, '0'
    jb .try_else
    cmp al, '9'
    jbe .next
    cmp al, 'A'
    jb .try_else
    cmp al, 'Z'
    jbe .next
    cmp al, '_'
    je .next
    cmp al, 'a'
    jb .try_else
    cmp al, 'z'
    jbe .next
.try_else:
    mov si, bx
    mov di, bas_kw_else
    call bas_match_word
    jc .next
    cmp bx, bp
    je .bad
    call bas_skip_spaces
    cmp byte [si], 0
    je .bad
    mov si, bp
    clc
    jmp .out
.next:
    inc bx
    jmp .scan
.quote:
    inc bx
.quote_loop:
    mov al, [bx]
    test al, al
    jz .bad
    inc bx
    cmp al, '"'
    jne .quote_loop
    jmp .scan
.none:
    xor bx, bx
    mov si, bp
    clc
    jmp .out
.bad:
    mov si, bp
    stc
.out:
    pop bp
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Structured flow, colour, sound and retained graphics.
; -----------------------------------------------------------------------------
bas_exec_on:
    push ax
    push bx
    push cx
    push dx
    push di
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    mov [bas_draw_x0], ax
    mov word [bas_draw_x1], 0
    mov word [bas_cap_tmp], 0
    call bas_skip_spaces
    mov di, bas_kw_goto
    call bas_match_word
    jnc .list
    mov di, bas_kw_gosub
    call bas_match_word
    jc .syntax
    mov word [bas_cap_tmp], 1
.list:
    xor bx, bx
.next:
    call bas_skip_spaces
    call bas_parse_uword
    jc .syntax
    test ax, ax
    jz .syntax
    inc bx
    cmp bx, [bas_draw_x0]
    jne .not_selected
    mov [bas_draw_x1], ax
.not_selected:
    call bas_skip_spaces
    cmp byte [si], ','
    jne .list_done
    inc si
    jmp .next
.list_done:
    call bas_expect_end
    jc .fail
    mov ax, [bas_draw_x0]
    test ax, ax
    jle .no_branch
    mov dx, [bas_draw_x1]
    test dx, dx
    jz .no_branch
    mov ax, dx
    call bas_find_exact
    jc .undef
    cmp word [bas_cap_tmp], 0
    je .jump
    mov bx, [bas_gosub_count]
    cmp bx, BAS_GOSUB_MAX
    jae .full
    shl bx, 1
    mov ax, [bas_current_line]
    mov [bas_gosub_stack + bx], ax
    inc word [bas_gosub_count]
.jump:
    dec dx
    mov [bas_current_line], dx
.no_branch:
    clc
    jmp .out
.undef:
    mov byte [bas_error_code], BERR_UNDEFLINE
    stc
    jmp .out
.full:
    mov byte [bas_error_code], BERR_GOSUBFULL
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_exec_gosub:
    push ax
    push bx
    push dx
    call bas_skip_spaces
    call bas_parse_uword
    jc .syntax
    test ax, ax
    jz .syntax
    mov dx, ax
    call bas_expect_end
    jc .fail
    mov ax, dx
    call bas_find_exact
    jc .undef
    mov bx, [bas_gosub_count]
    cmp bx, BAS_GOSUB_MAX
    jae .full
    shl bx, 1
    mov ax, [bas_current_line]
    mov [bas_gosub_stack + bx], ax
    inc word [bas_gosub_count]
    dec dx
    mov [bas_current_line], dx
    clc
    jmp .out
.undef:
    mov byte [bas_error_code], BERR_UNDEFLINE
    stc
    jmp .out
.full:
    mov byte [bas_error_code], BERR_GOSUBFULL
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

bas_exec_return:
    push ax
    push bx
    call bas_expect_end
    jc .fail
    mov bx, [bas_gosub_count]
    test bx, bx
    jz .empty
    dec bx
    mov [bas_gosub_count], bx
    shl bx, 1
    mov ax, [bas_gosub_stack + bx]
    mov [bas_current_line], ax
    clc
    jmp .out
.empty:
    mov byte [bas_error_code], BERR_RETURN
.fail:
    stc
.out:
    pop bx
    pop ax
    ret

bas_exec_for:
    push ax
    push bx
    push cx
    push dx
    push di

    call bas_parse_variable
    jc .fail
    mov [bas_draw_x0], bx              ; variable byte offset
    call bas_skip_spaces
    cmp byte [si], '='
    jne .syntax
    inc si
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    mov [bas_draw_y0], ax              ; start
    mov bx, [bas_draw_x0]
    mov [bas_vars + bx], ax

    call bas_skip_spaces
    mov di, bas_kw_to
    call bas_match_word
    jc .syntax
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    mov [bas_draw_x1], ax              ; limit
    mov word [bas_draw_y1], 1          ; default STEP

    call bas_skip_spaces
    cmp byte [si], 0
    je .parsed
    cmp byte [si], 27h
    je .parsed
    mov di, bas_kw_step
    call bas_match_word
    jc .syntax
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    jz .illegal
    mov [bas_draw_y1], ax
.parsed:
    call bas_expect_end
    jc .fail

    ; Skip the loop body when the initial value has already crossed the limit.
    mov ax, [bas_draw_y1]
    test ax, ax
    js .negative_step
    mov ax, [bas_draw_y0]
    cmp ax, [bas_draw_x1]
    jg .skip_body
    jmp .push_frame
.negative_step:
    mov ax, [bas_draw_y0]
    cmp ax, [bas_draw_x1]
    jl .skip_body

.push_frame:
    mov ax, [bas_for_count]
    cmp ax, BAS_FOR_MAX
    jae .full
    mov bx, BAS_FOR_REC_SIZE
    mul bx
    mov di, bas_for_stack
    add di, ax
    mov ax, [bas_draw_x0]
    mov [di], ax
    mov ax, [bas_draw_x1]
    mov [di + 2], ax
    mov ax, [bas_draw_y1]
    mov [di + 4], ax
    mov ax, [bas_current_line]
    mov [di + 6], ax
    inc word [bas_for_count]
    clc
    jmp .out

.skip_body:
    call bas_skip_to_matching_next
    jmp .out
.full:
    mov byte [bas_error_code], BERR_FORFULL
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Skip from the current FOR line to its matching NEXT, respecting nested FORs.
bas_skip_to_matching_next:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [bas_current_line]
    mov cx, 1
.scan:
    mov ax, bx
    call bas_find_next_after
    jc .missing
    mov bx, dx
    add si, 3
    call bas_skip_spaces
    push si
    mov di, bas_kw_for
    call bas_match_word
    pop si
    jnc .nested_for
    mov di, bas_kw_next
    call bas_match_word
    jc .scan
    dec cx
    jnz .scan
    mov [bas_current_line], bx
    clc
    jmp .out
.nested_for:
    inc cx
    jmp .scan
.missing:
    mov byte [bas_error_code], BERR_FORNEXT
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_exec_next:
    push ax
    push bx
    push cx
    push dx
    push di

    mov ax, [bas_for_count]
    test ax, ax
    jz .empty
    dec ax
    mov bx, BAS_FOR_REC_SIZE
    mul bx
    mov di, bas_for_stack
    add di, ax

    call bas_skip_spaces
    cmp byte [si], 0
    je .check_end
    cmp byte [si], 27h
    je .check_end
    call bas_parse_variable
    jc .fail
    cmp bx, [di]
    jne .empty
.check_end:
    call bas_expect_end
    jc .fail
.variable_ok:
    mov bx, [di]
    mov ax, [bas_vars + bx]
    add ax, [di + 4]
    jo .overflow
    mov [bas_vars + bx], ax
    mov dx, [di + 4]
    test dx, dx
    js .negative
    cmp ax, [di + 2]
    jle .again
    jmp .finished
.negative:
    cmp ax, [di + 2]
    jge .again
.finished:
    dec word [bas_for_count]
    clc
    jmp .out
.again:
    mov ax, [di + 6]
    mov [bas_current_line], ax
    clc
    jmp .out
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    jmp .out
.empty:
    mov byte [bas_error_code], BERR_NEXT
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; PROMPTCOLOR [fg,bg] changes only the editing prompt, NOT console text
; colours or historical output. Bare PROMPTCOLOR restores black-on-white.
; Both operands must be integers in 0..15. Commit is atomic on success.
bas_exec_promptcolor:
    push ax
    push bx
    push dx
    call bas_skip_spaces
    cmp byte [si], 0
    jne .args
    mov byte [bas_prompt_fg], CBLACK
    mov byte [bas_prompt_bg], CWHITE
    clc
    jmp .out
.args:
    call bas_parse_coord
    jc .out
    cmp ax, 15
    ja .illegal
    push ax
    call bas_parse_comma
    jc .pop_fail
    call bas_parse_coord
    jc .pop_fail
    cmp ax, 15
    ja .pop_illegal
    push ax
    call bas_expect_end
    pop dx
    pop bx
    jc .out
    mov [bas_prompt_fg], bl
    mov [bas_prompt_bg], dl
    clc
    jmp .out
.pop_illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.pop_fail:
    pop ax
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

bas_exec_color:
    push ax
    call bas_skip_spaces
    cmp byte [si], 0
    je .syntax

    ; QBasic-compatible background-only form: COLOR ,background.
    cmp byte [si], ','
    jne .parse_foreground
    xor ax, ax
    mov al, [bas_fg_color]
    mov [bas_draw_x0], ax
    inc si
    jmp .parse_background

.parse_foreground:
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, 15
    ja .illegal
    mov [bas_draw_x0], ax
    call bas_skip_spaces
    cmp byte [si], ','
    jne .foreground_only
    inc si

.parse_background:
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, 15
    ja .illegal
    mov [bas_bg_color], al
    call bas_expect_end
    jc .fail
    jmp .set_fg

.foreground_only:
    call bas_expect_end
    jc .fail
.set_fg:
    mov ax, [bas_draw_x0]
    mov [bas_fg_color], al
    mov byte [bas_dirty], 1
    clc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop ax
    ret

bas_exec_screen:
    push ax
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    cmp ax, 0
    je .set
    cmp ax, 1
    jne .illegal
.set:
    push ax
    call bas_expect_end
    pop ax
    jc .fail
    mov [bas_screen_mode], al
    call bas_cls
    clc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop ax
    ret

bas_exec_beep:
    push ax
    push cx
    push dx
    call bas_expect_end
    jc .fail
    mov ax, 880
    mov cx, 3
    mov dl, 40h
    call OSAPI_SND_TONE
    clc
    jmp .out
.fail:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

bas_exec_sound:
    push ax
    push cx
    push dx
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    jle .illegal
    mov [bas_draw_x0], ax              ; preserve frequency across parser calls
    call bas_parse_comma
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    jle .illegal
    mov cx, ax
    call bas_expect_end
    jc .fail
    mov ax, [bas_draw_x0]
    mov dl, 40h
    call OSAPI_SND_TONE
    clc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

bas_exec_randomize:
    push ax
    call bas_skip_spaces
    cmp byte [si], 0
    je .ticks
    cmp byte [si], 27h
    jne .seed
.ticks:
    call OSAPI_GET_TICKS
    call OSAPI_SRAND
    clc
    jmp .out
.seed:
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    push ax
    call bas_expect_end
    pop ax
    jc .fail
    call OSAPI_SRAND
    clc
    jmp .out
.fail:
    stc
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; Native-memory and os8088 sound/image commands.
; -----------------------------------------------------------------------------
bas_exec_poke:
    push ax
    push bx
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, BAS_POKE_CAP - 1
    ja .illegal
    mov [bas_draw_x0], ax
    call bas_parse_comma
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, 255
    ja .illegal
    mov bx, ax
    call bas_expect_end
    jc .fail
    mov ax, [bas_draw_x0]
    xchg ax, bx
    mov [bas_poke_bank + bx], al
    clc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop bx
    pop ax
    ret

bas_require_fm:
    call OSAPI_SND_CAPS
    test ax, SND_CAP_FM
    jnz .ok
    mov byte [bas_error_code], BERR_NOADLIB
    stc
    ret
.ok:
    clc
    ret

bas_exec_fmpreset:
    push ax
    push bx
    push cx
    push dx
    push si
    call bas_require_fm
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, 7
    ja .illegal
    mov [bas_draw_x0], ax
    call bas_parse_comma
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, 7
    ja .illegal
    mov [bas_draw_x1], ax
    call bas_expect_end
    jc .fail
    mov ax, [bas_draw_x1]
    mov bx, 11
    mul bx
    mov si, bas_fm_patches
    add si, ax
    mov cx, [bas_draw_x0]
    mov al, 2
    call OSAPI_SND_FM
    jc .refused
    clc
    jmp .out
.refused:
    mov byte [bas_error_code], BERR_FMREFUSED
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_exec_fmnote:
    push ax
    push bx
    push cx
    push dx
    call bas_require_fm
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, 7
    ja .illegal
    mov [bas_draw_x0], ax
    call bas_parse_comma
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    cmp ax, 19
    jb .illegal
    cmp ax, 6208
    ja .illegal
    mov bx, ax
    call bas_expect_end
    jc .fail
    mov cx, [bas_draw_x0]
    xor al, al
    call OSAPI_SND_FM
    jc .refused
    clc
    jmp .out
.refused:
    mov byte [bas_error_code], BERR_FMREFUSED
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_exec_fmoff:
    push ax
    push cx
    push dx
    call bas_require_fm
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, 7
    ja .illegal
    mov cx, ax
    call bas_expect_end
    jc .fail
    mov al, 1
    call OSAPI_SND_FM
    jc .refused
    clc
    jmp .out
.refused:
    mov byte [bas_error_code], BERR_FMREFUSED
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

bas_exec_fmstop:
    push ax
    push cx
    push dx
    call bas_expect_end
    jc .fail
    call bas_require_fm
    jc .fail
    mov al, 3
    xor cx, cx
    call OSAPI_SND_FM
    jc .refused
    clc
    jmp .out
.refused:
    mov byte [bas_error_code], BERR_FMREFUSED
.fail:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

bas_fm_alloff_quiet:
    push ax
    push cx
    push dx
    call OSAPI_SND_CAPS
    test ax, SND_CAP_FM
    jz .out
    mov al, 3
    xor cx, cx
    call OSAPI_SND_FM
.out:
    pop dx
    pop cx
    pop ax
    ret

bas_exec_putbmp:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [bas_screen_mode], 1
    jne .screen_error
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, BAS_IMAGE_SLOTS - 1
    ja .illegal
    mov [bas_bmp_slot], ax
    call bas_parse_comma
    jc .fail
    call bas_parse_coord
    jc .fail
    cmp ax, 2047
    ja .illegal
    mov [bas_draw_x0], ax
    call bas_parse_comma
    jc .fail
    call bas_parse_coord
    jc .fail
    cmp ax, 2047
    ja .illegal
    mov [bas_draw_y0], ax

    ; Legacy form PUTBMP slot,x,y draws the complete image. A source rectangle
    ; adds sourceX,sourceY,width,height. Packed 4bpp starts on a byte boundary,
    ; therefore sourceX must be even.
    mov word [bas_draw_x1], 0
    mov word [bas_draw_y1], 0
    mov word [bas_draw_dx], 0
    mov word [bas_draw_dy], 0
    call bas_skip_spaces
    cmp byte [si], 0
    je .validate_image
    call bas_parse_comma
    jc .fail
    call bas_parse_coord
    jc .fail
    test ax, 1
    jnz .illegal
    mov [bas_draw_x1], ax
    call bas_parse_comma
    jc .fail
    call bas_parse_coord
    jc .fail
    mov [bas_draw_y1], ax
    call bas_parse_comma
    jc .fail
    call bas_parse_coord
    jc .fail
    test ax, ax
    jz .illegal
    mov [bas_draw_dx], ax
    call bas_parse_comma
    jc .fail
    call bas_parse_coord
    jc .fail
    test ax, ax
    jz .illegal
    mov [bas_draw_dy], ax
    call bas_expect_end
    jc .fail

.validate_image:
    mov bx, [bas_bmp_slot]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    cmp byte [bas_image_meta + bx], 1
    jne .noimage
    cmp word [bas_draw_dx], 0
    je .store
    mov ax, [bas_draw_x1]
    add ax, [bas_draw_dx]
    jc .illegal
    cmp ax, [bas_image_meta + bx + 2]
    ja .illegal
    mov ax, [bas_draw_y1]
    add ax, [bas_draw_dy]
    jc .illegal
    cmp ax, [bas_image_meta + bx + 4]
    ja .illegal

.store:
    mov ax, [bas_gfx_count]
    cmp ax, BAS_GFX_MAX
    jae .full
    mov bx, BAS_GFX_REC_SIZE
    mul bx
    mov di, bas_gfx_list
    add di, ax
    mov byte [di], GFX_BITMAP
    mov ax, [bas_bmp_slot]
    mov [di + 1], al
    mov ax, [bas_draw_x0]
    mov [di + 2], ax
    mov ax, [bas_draw_y0]
    mov [di + 4], ax
    mov ax, [bas_draw_x1]
    mov [di + 6], al
    mov ax, [bas_draw_y1]
    mov [di + 7], al
    mov ax, [bas_draw_dx]
    mov [di + 8], al
    mov ax, [bas_draw_dy]
    mov [di + 9], al
    call bas_gfx_note_delta
    inc word [bas_gfx_count]
    mov byte [bas_dirty], 1
    clc
    jmp .out
.screen_error:
    mov byte [bas_error_code], BERR_SCREEN
    stc
    jmp .out
.noimage:
    mov byte [bas_error_code], BERR_NOBMP
    stc
    jmp .out
.full:
    mov byte [bas_error_code], BERR_GFXFULL
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; Copy one newly stored retained command into the current fast batch delta.
; in: DI -> complete BAS_GFX_REC_SIZE record. A delta overflow safely promotes
; the batch to the existing full-scene DRAWEND path.
bas_gfx_note_delta:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [bas_draw_batch], 0
    je .out
    cmp byte [bas_batch_fast], 0
    je .out
    mov ax, [bas_gfx_delta_count]
    cmp ax, BAS_GFX_DELTA_MAX
    jae .overflow
    mov bx, BAS_GFX_REC_SIZE
    mul bx
    mov bx, bas_gfx_delta_list
    add bx, ax
    mov si, di
    mov di, bx
    mov cx, BAS_GFX_REC_SIZE
    rep movsb
    inc word [bas_gfx_delta_count]
    jmp .out
.overflow:
    mov byte [bas_batch_fast], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; A solid retained fill completely replaces older small retained objects inside
; it. Remove those covered records before appending the new fill. This keeps XT
; incremental square/icon updates bounded instead of consuming all 84 records.
; Only object types whose complete bounds are cheap and unambiguous are pruned:
; FILLBOX, BOX and the fixed 16x16 resource glyph.
bas_gfx_prune_fillbox:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    ; Normalize the new covering rectangle into circle temporaries.
    mov ax, [bas_draw_x0]
    mov bx, [bas_draw_x1]
    cmp ax, bx
    jbe .nx
    xchg ax, bx
.nx:
    mov [bas_circle_x], ax
    mov [bas_circle_err], bx
    mov ax, [bas_draw_y0]
    mov bx, [bas_draw_y1]
    cmp ax, bx
    jbe .ny
    xchg ax, bx
.ny:
    mov [bas_circle_y], ax
    mov [bas_circle_cx], bx

    xor bp, bp                              ; record index
.scan:
    mov ax, [bas_gfx_count]
    cmp bp, ax
    jae .out
    mov ax, bp
    mov bx, BAS_GFX_REC_SIZE
    mul bx
    mov si, bas_gfx_list
    add si, ax
    mov al, [si]
    cmp al, GFX_RESOURCE
    je .resource
    cmp al, GFX_FILLBOX
    je .rect
    cmp al, GFX_BOX
    jne .next
.rect:
    mov ax, [si + 2]
    mov bx, [si + 6]
    cmp ax, bx
    jbe .rx
    xchg ax, bx
.rx:
    mov dx, ax                              ; old x1
    mov di, bx                              ; old x2
    mov ax, [si + 4]
    mov bx, [si + 8]
    cmp ax, bx
    jbe .ry
    xchg ax, bx
.ry:
    jmp .test                               ; AX=y1 BX=y2 DX=x1 DI=x2
.resource:
    mov dx, [si + 2]
    mov di, dx
    add di, BAS_RES_SPRITE_W - 1
    mov ax, [si + 4]
    mov bx, ax
    add bx, BAS_RES_SPRITE_H - 1
.test:
    cmp dx, [bas_circle_x]
    jb .next
    cmp di, [bas_circle_err]
    ja .next
    cmp ax, [bas_circle_y]
    jb .next
    cmp bx, [bas_circle_cx]
    ja .next

    ; Delete record BP by shifting the tail left one record.
    mov ax, [bas_gfx_count]
    dec ax
    sub ax, bp
    jz .deleted
    mov cx, ax
    mov ax, BAS_GFX_REC_SIZE
    mul cx
    mov cx, ax
    mov di, si
    add si, BAS_GFX_REC_SIZE
    rep movsb
.deleted:
    dec word [bas_gfx_count]
    jmp .scan                               ; inspect the record shifted into BP
.next:
    inc bp
    jmp .scan
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_exec_freebmp:
    push ax
    push bx
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, BAS_IMAGE_SLOTS - 1
    ja .illegal
    mov bx, ax
    call bas_expect_end
    jc .fail
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov byte [bas_image_meta + bx], 0
    mov byte [bas_dirty], 1
    clc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop bx
    pop ax
    ret

bas_exec_loadbmp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call bas_parse_bmp_filename
    jc .fail
    call bas_parse_comma
    jc .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    jc .fail
    test ax, ax
    js .illegal
    cmp ax, BAS_IMAGE_SLOTS - 1
    ja .illegal
    mov [bas_bmp_slot], ax
    call bas_expect_end
    jc .fail

    cmp byte [bas_worker_active], 0
    je .read_file
    mov bx, [bas_bmp_slot]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    cmp byte [bas_image_meta + bx], 1
    jne .noimage
    clc
    jmp .out

.read_file:
    ; BMP parsing shares the same transient 5 KiB whole-file grant as textual
    ; LOAD/SAVE. Existing bitmap slots remain resident and are not sacrificed.
    mov ax, BAS_IO_KB
    call bas_claim_kb
    jc .no_memory
    mov [bas_io_seg], dx
    mov es, dx
    xor bx, bx
    mov cx, BAS_IO_CAP
    mov si, bas_filename
    xor dx, dx
    call OSAPI_FILE_READ
    jc .file_error_ext
    mov [bas_io_len], ax
    cmp ax, 118
    jb .bad_ext

    cmp word [es:0], 4D42h
    jne .bad_ext
    cmp word [es:12], 0
    jne .bad_ext
    mov ax, [es:10]
    mov [bas_bmp_offset], ax
    cmp ax, 118
    jb .bad_ext
    cmp ax, [bas_io_len]
    jae .bad_ext
    cmp word [es:16], 0
    jne .bad_ext
    cmp word [es:14], 40
    jb .bad_ext
    cmp word [es:20], 0
    jne .bad_ext
    mov ax, [es:18]
    test ax, ax
    jz .bad_ext
    cmp ax, BAS_IMAGE_MAX_W
    ja .bad_ext
    mov [bas_bmp_width], ax
    cmp word [es:24], 0
    jne .bad_ext
    mov ax, [es:22]
    test ax, ax
    jz .bad_ext
    cmp ax, BAS_IMAGE_MAX_H
    ja .bad_ext
    mov [bas_bmp_height], ax
    cmp word [es:26], 1
    jne .bad_ext
    cmp word [es:28], 4
    jne .bad_ext
    cmp word [es:30], 0
    jne .bad_ext
    cmp word [es:32], 0
    jne .bad_ext

    mov ax, [bas_bmp_width]
    inc ax
    shr ax, 1
    add ax, 3
    and ax, 0FFFCh
    mov [bas_bmp_stride], ax
    mov bx, [bas_bmp_height]
    mul bx
    test dx, dx
    jnz .bad_ext
    cmp ax, BAS_IMAGE_SLOT_BYTES
    ja .bad_ext
    add ax, [bas_bmp_offset]
    jc .bad_ext
    cmp ax, [bas_io_len]
    ja .bad_ext

    ; Locate the last stored BMP row (BMP is bottom-up) and the slot buffer.
    mov ax, [bas_bmp_height]
    dec ax
    mov bx, [bas_bmp_stride]
    mul bx
    add ax, [bas_bmp_offset]
    mov [bas_best_ptr], ax

    mov ax, [bas_bmp_slot]
    mov bx, BAS_IMAGE_SLOT_BYTES
    mul bx
    add ax, bas_image_data
    mov [bas_draw_x0], ax
    mov ax, [bas_bmp_height]
    mov [bas_last_line], ax

.copy_row:
    cmp word [bas_last_line], 0
    je .store_meta
    mov si, [bas_best_ptr]
    mov di, [bas_draw_x0]
    mov cx, [bas_bmp_stride]
.copy_byte:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .copy_byte
    mov [bas_draw_x0], di
    mov ax, [bas_bmp_stride]
    shl ax, 1
    sub si, ax
    mov [bas_best_ptr], si
    dec word [bas_last_line]
    jmp .copy_row

.store_meta:
    push ds
    pop es
    call bas_free_io
    mov bx, [bas_bmp_slot]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov byte [bas_image_meta + bx], 1
    mov byte [bas_image_meta + bx + 1], 0
    mov ax, [bas_bmp_width]
    mov [bas_image_meta + bx + 2], ax
    mov ax, [bas_bmp_height]
    mov [bas_image_meta + bx + 4], ax
    mov ax, [bas_bmp_stride]
    mov [bas_image_meta + bx + 6], ax
    ; Direct mode reports the loaded filename. During RUN, remain silent so a
    ; program can load its own assets without adding status text to its output.
    cmp word [bas_current_line], 0
    jne .loaded_ok
    mov si, bas_s_bmp_loaded
    call bas_file_status
.loaded_ok:
    clc
    jmp .out
.noimage:
    mov byte [bas_error_code], BERR_NOBMP
    stc
    jmp .out
.no_memory:
    mov byte [bas_error_code], BERR_NOMEM
    stc
    jmp .out
.file_error_ext:
    push ax
    push ds
    pop es
    call bas_free_io
    pop ax
    call bas_set_file_error
    stc
    jmp .out
.bad_ext:
    push ds
    pop es
    call bas_free_io
    mov byte [bas_error_code], BERR_BMPFORMAT
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_show_capacity:
    push ax
    push si
    mov si, bas_s_cap_image
    mov ax, OS88_IMAGE_SIZE
    call bas_emit_capacity_value
    mov si, bas_s_cap_bss
    mov ax, BAS_BSS_TOTAL
    call bas_emit_capacity_value
    mov si, bas_s_cap_head
    mov ax, APP_MAX_SIZE - (OS88_IMAGE_SIZE + BAS_BSS_TOTAL)
    call bas_emit_capacity_value
    mov si, bas_s_cap2
    call bas_append_line
    mov si, bas_s_cap3
    call bas_append_line
    mov si, bas_s_cap4
    call bas_append_line
    mov si, bas_s_cap5
    call bas_append_line
    mov si, bas_s_cap6
    call bas_append_line
    pop si
    pop ax
    ret

bas_show_memory:
    push ax
    push si
    mov si, bas_s_mem_used
    mov ax, [bas_program_used]
    call bas_emit_capacity_value
    mov si, bas_s_mem_free
    mov ax, BAS_PROG_BYTES
    sub ax, [bas_program_used]
    call bas_emit_capacity_value
    mov si, bas_s_mem_lines
    mov ax, [bas_program_count]
    call bas_emit_capacity_value
    mov si, bas_s_mem_gosub
    mov ax, [bas_gosub_count]
    call bas_emit_capacity_value
    mov si, bas_s_mem_for
    mov ax, [bas_for_count]
    call bas_emit_capacity_value
    mov si, bas_s_mem_gfx
    mov ax, [bas_gfx_count]
    call bas_emit_capacity_value
    pop si
    pop ax
    ret

; Emit SI prefix followed by unsigned AX and a NUL.
bas_emit_capacity_value:
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, ax
    mov di, bas_scratch
    mov cx, BAS_SCRATCH_CAP - 7
.copy:
    lodsb
    test al, al
    jz .number
    stosb
    loop .copy
.number:
    mov ax, bx
    call bas_word_to_dec
    mov byte [di], 0
    mov si, bas_scratch
    call bas_append_line
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; AL selects GFX_* on entry. Shapes use QBasic-style coordinate syntax.
bas_exec_graphics:
    push ax
    push bx
    push cx
    push dx
    push di

    xor ah, ah
    mov [bas_best_ptr], ax
    cmp byte [bas_screen_mode], 1
    jne .screen_error
    mov ax, [bas_gfx_count]
    cmp ax, BAS_GFX_MAX
    jae .full

    call bas_parse_point
    jc .fail
    cmp ax, 2047
    ja .illegal
    cmp bx, 2047
    ja .illegal
    mov [bas_draw_x0], ax
    mov [bas_draw_y0], bx

    mov ax, [bas_best_ptr]
    cmp al, GFX_PSET
    je .finish_shape
    cmp al, GFX_CIRCLE
    je .circle

    call bas_skip_spaces
    cmp byte [si], '-'
    jne .syntax
    inc si
    call bas_parse_point
    jc .fail
    cmp ax, 2047
    ja .illegal
    cmp bx, 2047
    ja .illegal
    mov [bas_draw_x1], ax
    mov [bas_draw_y1], bx
    jmp .finish_shape

.circle:
    call bas_parse_comma
    jc .fail
    call bas_parse_coord
    jc .fail
    test ax, ax
    jz .illegal
    cmp ax, 1023
    ja .illegal
    mov [bas_draw_x1], ax              ; radius
    mov word [bas_draw_y1], 0

.finish_shape:
    call bas_parse_optional_color
    jc .fail
    mov [bas_draw_dx], dx              ; MUL destroys DX; retain selected colour
    mov ax, [bas_best_ptr]
    cmp al, GFX_FILLBOX
    jne .no_prune
    call bas_gfx_prune_fillbox
.no_prune:
    mov ax, [bas_gfx_count]
    mov bx, BAS_GFX_REC_SIZE
    mul bx
    mov di, bas_gfx_list
    add di, ax
    mov ax, [bas_best_ptr]
    mov [di], al
    mov dx, [bas_draw_dx]
    mov [di + 1], dl
    mov ax, [bas_draw_x0]
    mov [di + 2], ax
    mov ax, [bas_draw_y0]
    mov [di + 4], ax
    mov ax, [bas_draw_x1]
    mov [di + 6], ax
    mov ax, [bas_draw_y1]
    mov [di + 8], ax
    call bas_gfx_note_delta
    inc word [bas_gfx_count]
    mov byte [bas_dirty], 1
    clc
    jmp .out
.screen_error:
    mov byte [bas_error_code], BERR_SCREEN
    stc
    jmp .out
.full:
    mov byte [bas_error_code], BERR_GFXFULL
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Repaint all retained graphics. The paint callback already holds the gfx lock.
bas_replay_graphics:
    push ax
    push si
    mov ax, [bas_gfx_count]
    mov si, bas_gfx_list
    call bas_replay_graphics_list
    pop si
    pop ax
    ret

; Draw only the records collected by a fast DRAWBEGIN batch.
bas_replay_graphics_delta:
    push ax
    push si
    mov ax, [bas_gfx_delta_count]
    mov si, bas_gfx_delta_list
    call bas_replay_graphics_list
    pop si
    pop ax
    ret

; in: AX=count, SI=list; preserves caller registers.
bas_replay_graphics_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [bas_last_line], ax             ; paint-only loop counter
    mov [bas_best_ptr], si
.loop:
    cmp word [bas_last_line], 0
    je .out
    mov si, [bas_best_ptr]
    cmp byte [si], GFX_RESOURCE
    je .pen_ready
    mov al, [si + 1]
    call OSAPI_SET_COLOR
.pen_ready:
    mov si, [bas_best_ptr]              ; API calls need not preserve SI

    mov ax, [si + 2]
    add ax, [bas_content_x]
    add ax, BAS_MARGIN
    mov [bas_draw_x0], ax
    mov ax, [si + 4]
    add ax, [bas_content_y]
    add ax, BAS_MARGIN
    mov [bas_draw_y0], ax
    mov ax, [si + 6]
    mov [bas_draw_x1], ax
    mov ax, [si + 8]
    mov [bas_draw_y1], ax

    mov al, [si]
    cmp al, GFX_PSET
    je .pset
    cmp al, GFX_LINE
    je .line
    cmp al, GFX_BOX
    je .box
    cmp al, GFX_FILLBOX
    je .fillbox
    cmp al, GFX_CIRCLE
    je .circle
    cmp al, GFX_BITMAP
    je .bitmap
    cmp al, GFX_RESOURCE
    je .resource
    jmp .next
.pset:
    mov cx, [bas_draw_x0]
    mov dx, [bas_draw_y0]
    call OSAPI_GFX_PIXEL
    jmp .next
.line:
    mov ax, [bas_draw_x1]
    add ax, [bas_content_x]
    add ax, BAS_MARGIN
    mov [bas_draw_x1], ax
    mov ax, [bas_draw_y1]
    add ax, [bas_content_y]
    add ax, BAS_MARGIN
    mov [bas_draw_y1], ax
    call bas_draw_line_abs
    jmp .next
.box:
    mov cx, [bas_draw_x1]
    add cx, [bas_content_x]
    add cx, BAS_MARGIN
    mov dx, [bas_draw_y1]
    add dx, [bas_content_y]
    add dx, BAS_MARGIN
    mov ax, [bas_draw_x0]
    mov bx, [bas_draw_y0]
    cmp ax, cx
    jbe .box_x_ok
    xchg ax, cx
.box_x_ok:
    cmp bx, dx
    jbe .box_y_ok
    xchg bx, dx
.box_y_ok:
    call OSAPI_GFX_FRAME
    jmp .next
.fillbox:
    mov cx, [bas_draw_x1]
    add cx, [bas_content_x]
    add cx, BAS_MARGIN
    mov dx, [bas_draw_y1]
    add dx, [bas_content_y]
    add dx, BAS_MARGIN
    mov ax, [bas_draw_x0]
    mov bx, [bas_draw_y0]
    cmp ax, cx
    jbe .fill_x_ok
    xchg ax, cx
.fill_x_ok:
    cmp bx, dx
    jbe .fill_y_ok
    xchg bx, dx
.fill_y_ok:
    call OSAPI_GFX_FILL
    jmp .next
.circle:
    mov ax, [bas_draw_x0]
    mov [bas_circle_cx], ax
    mov ax, [bas_draw_y0]
    mov [bas_circle_cy], ax
    mov ax, [bas_draw_x1]
    call bas_draw_circle_abs
    jmp .next
.bitmap:
    mov si, [bas_best_ptr]
    xor bx, bx
    mov bl, [si + 1]
    cmp bl, BAS_IMAGE_SLOTS - 1
    ja .next

    mov ax, bx
    mov cx, BAS_IMAGE_META_SIZE
    mul cx
    mov di, bas_image_meta
    add di, ax
    cmp byte [di], 1
    jne .next

    ; Locate the resident slot, then offset by sourceY*stride+sourceX/2.
    ; sourceX was validated even when the retained command was created.
    mov ax, bx
    mov cx, BAS_IMAGE_SLOT_BYTES
    mul cx
    mov si, bas_image_data
    add si, ax
    mov bp, [di + 6]
    mov ax, [bas_best_ptr]
    mov bx, ax
    xor ax, ax
    mov al, [bx + 7]
    mul bp
    add si, ax
    xor ax, ax
    mov al, [bx + 6]
    shr ax, 1
    add si, ax

    xor cx, cx
    mov cl, [bx + 8]
    test cx, cx
    jnz .crop_width
    mov cx, [di + 2]
.crop_width:
    xor dx, dx
    mov dl, [bx + 9]
    test dx, dx
    jnz .crop_height
    mov dx, [di + 4]
.crop_height:
    push ds
    pop es
    mov ax, [bas_draw_x0]
    mov bx, [bas_draw_y0]
    call OSAPI_GFX_BLIT4
    jmp .next

.resource:
    call bas_draw_resource_abs

.next:
    add word [bas_best_ptr], BAS_GFX_REC_SIZE
    dec word [bas_last_line]
    jmp .loop
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Draw one retained 2BR2 16x16 glyph. Zero source bits are transparent;
; one bits use the retained foreground colour. The fixed quarter-turn selector
; avoids storing rotated copies in the resource file.
bas_draw_resource_abs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call bas_resource_ready
    jc .out
    mov si, [bas_best_ptr]
    test byte [si + 7], 3
    jnz .pixel_fallback
    ; One native masked 16x16 operation instead of up to 256 pixel calls.
    ; 2BR2 stores row bytes MSB-left; ICON_DRAW wants words bit15-left.
    xor ax, ax
    mov al, [si + 1]               ; validated 0..98 thing index
    mov cl, 5                      ; 32 bytes per thing
    shl ax, cl
    mov si, bas_image_data
    add si, [bas_res_offset]
    add si, ax
    mov di, bas_icon_record
    mov byte [di], 1               ; mask+data record
    mov byte [di + 1], 16
    add di, 2
    mov cx, BAS_RES_SPRITE_H
.icon_row:
    lodsw
    xchg al, ah                    ; byte order -> bit15 leftmost
    mov [di], ax                   ; mask bits; clear pixels transparent
    mov [di + 32], ax              ; same pixels as foreground data
    add di, 2
    loop .icon_row
    mov si, [bas_best_ptr]
    mov al, [si + 6]               ; retained foreground
    mov ah, al                     ; both mask+data use same ink
    call OSAPI_ICON_PEN
    mov si, bas_icon_record
    mov cx, [bas_draw_x0]
    mov dx, [bas_draw_y0]
    call OSAPI_ICON_DRAW
    jnc .out
.pixel_fallback:
    mov si, [bas_best_ptr]
    mov al, [si + 6]
    call OSAPI_SET_COLOR
    mov word [bas_draw_sx], 0           ; destination/source scan row
.row:
    cmp word [bas_draw_sx], BAS_RES_SPRITE_H
    jae .out
    mov word [bas_draw_sy], 0           ; destination/source scan column
.col:
    cmp word [bas_draw_sy], BAS_RES_SPRITE_W
    jae .next_row

    ; Map destination dx/dy back to source sx/sy for a clockwise quarter-turn.
    mov di, [bas_best_ptr]
    mov al, [di + 7]
    and al, 3
    cmp al, 1
    je .rot90
    cmp al, 2
    je .rot180
    cmp al, 3
    je .rot270
    mov ax, [bas_draw_sy]               ; sx = dx
    mov bx, [bas_draw_sx]               ; sy = dy
    jmp .sample
.rot90:
    mov ax, [bas_draw_sx]               ; sx = dy
    mov bx, BAS_RES_SPRITE_W - 1
    sub bx, [bas_draw_sy]               ; sy = 15-dx
    jmp .sample
.rot180:
    mov ax, BAS_RES_SPRITE_W - 1
    sub ax, [bas_draw_sy]               ; sx = 15-dx
    mov bx, BAS_RES_SPRITE_H - 1
    sub bx, [bas_draw_sx]               ; sy = 15-dy
    jmp .sample
.rot270:
    mov ax, BAS_RES_SPRITE_H - 1
    sub ax, [bas_draw_sx]               ; sx = 15-dy
    mov bx, [bas_draw_sy]               ; sy = dx

.sample:
    ; DI -> packed source byte for this source pixel.
    mov di, [bas_best_ptr]
    xor dx, dx
    mov dl, [di + 1]
    mov cl, 1
    shl dx, cl
    shl dx, cl
    shl dx, cl
    shl dx, cl
    shl dx, cl                          ; thing * 32
    mov di, bas_image_data
    add di, [bas_res_offset]
    add di, dx
    mov dx, bx
    shl dx, cl                          ; source row * 2
    add di, dx
    mov bx, ax
    mov cx, ax
    shr bx, 1
    shr bx, 1
    shr bx, 1                          ; source column / 8
    add di, bx
    and cx, 7
    mov al, 80h
    shr al, cl
    test byte [di], al
    jz .advance

    mov cx, [bas_draw_x0]
    add cx, [bas_draw_sy]
    mov dx, [bas_draw_y0]
    add dx, [bas_draw_sx]
    call OSAPI_GFX_PIXEL

.advance:
    inc word [bas_draw_sy]
    jmp .col
.next_row:
    inc word [bas_draw_sx]
    jmp .row
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Bresenham line using absolute screen coordinates in bas_draw_x0..bas_draw_y1.
bas_draw_line_abs:
    push ax
    push bx
    push cx
    push dx

    mov ax, [bas_draw_x1]
    sub ax, [bas_draw_x0]
    jns .dx_pos
    neg ax
    mov word [bas_draw_sx], -1
    jmp .dx_done
.dx_pos:
    mov word [bas_draw_sx], 1
.dx_done:
    mov [bas_draw_dx], ax

    mov ax, [bas_draw_y1]
    sub ax, [bas_draw_y0]
    jns .dy_pos
    neg ax
    mov word [bas_draw_sy], -1
    jmp .dy_abs
.dy_pos:
    mov word [bas_draw_sy], 1
.dy_abs:
    neg ax
    mov [bas_draw_dy], ax
    add ax, [bas_draw_dx]
    mov [bas_draw_err], ax

.loop:
    mov cx, [bas_draw_x0]
    mov dx, [bas_draw_y0]
    call OSAPI_GFX_PIXEL
    mov ax, [bas_draw_x0]
    cmp ax, [bas_draw_x1]
    jne .step
    mov ax, [bas_draw_y0]
    cmp ax, [bas_draw_y1]
    je .done
.step:
    mov bx, [bas_draw_err]
    shl bx, 1
    cmp bx, [bas_draw_dy]
    jl .skip_x
    mov ax, [bas_draw_dy]
    add [bas_draw_err], ax
    mov ax, [bas_draw_sx]
    add [bas_draw_x0], ax
.skip_x:
    cmp bx, [bas_draw_dx]
    jg .loop
    mov ax, [bas_draw_dx]
    add [bas_draw_err], ax
    mov ax, [bas_draw_sy]
    add [bas_draw_y0], ax
    jmp .loop
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Midpoint circle. AX=radius; centre is in bas_circle_cx/cy.
bas_draw_circle_abs:
    push ax
    push bx
    push cx
    push dx
    mov [bas_circle_x], ax
    mov word [bas_circle_y], 0
    mov bx, 1
    sub bx, ax
    mov [bas_circle_err], bx
.loop:
    mov ax, [bas_circle_x]
    cmp ax, [bas_circle_y]
    jl .done
    call bas_circle_plot8
    inc word [bas_circle_y]
    cmp word [bas_circle_err], 0
    jl .inside
    dec word [bas_circle_x]
    mov ax, [bas_circle_y]
    sub ax, [bas_circle_x]
    shl ax, 1
    inc ax
    add [bas_circle_err], ax
    jmp .loop
.inside:
    mov ax, [bas_circle_y]
    shl ax, 1
    inc ax
    add [bas_circle_err], ax
    jmp .loop
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_circle_plot8:
    push ax
    push cx
    push dx

    mov ax, [bas_circle_x]
    mov cx, [bas_circle_cx]
    add cx, ax
    mov dx, [bas_circle_cy]
    add dx, [bas_circle_y]
    call OSAPI_GFX_PIXEL

    mov ax, [bas_circle_x]
    mov cx, [bas_circle_cx]
    add cx, ax
    mov dx, [bas_circle_cy]
    sub dx, [bas_circle_y]
    call OSAPI_GFX_PIXEL

    mov ax, [bas_circle_x]
    mov cx, [bas_circle_cx]
    sub cx, ax
    mov dx, [bas_circle_cy]
    add dx, [bas_circle_y]
    call OSAPI_GFX_PIXEL

    mov ax, [bas_circle_x]
    mov cx, [bas_circle_cx]
    sub cx, ax
    mov dx, [bas_circle_cy]
    sub dx, [bas_circle_y]
    call OSAPI_GFX_PIXEL

    mov ax, [bas_circle_y]
    mov cx, [bas_circle_cx]
    add cx, ax
    mov dx, [bas_circle_cy]
    add dx, [bas_circle_x]
    call OSAPI_GFX_PIXEL

    mov ax, [bas_circle_y]
    mov cx, [bas_circle_cx]
    add cx, ax
    mov dx, [bas_circle_cy]
    sub dx, [bas_circle_x]
    call OSAPI_GFX_PIXEL

    mov ax, [bas_circle_y]
    mov cx, [bas_circle_cx]
    sub cx, ax
    mov dx, [bas_circle_cy]
    add dx, [bas_circle_x]
    call OSAPI_GFX_PIXEL

    mov ax, [bas_circle_y]
    mov cx, [bas_circle_cx]
    sub cx, ax
    mov dx, [bas_circle_cy]
    sub dx, [bas_circle_x]
    call OSAPI_GFX_PIXEL

    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Integer expression parser.
; Precedence: comparison, +/-, */MOD, unary, primary.
; -----------------------------------------------------------------------------
bas_parse_expr:
    jmp bas_parse_or

; Lowest precedence first: OR, AND, NOT, comparison, arithmetic.
; Logical operators always return 0 or 1 and evaluate both operands.
bas_parse_or:
    call bas_parse_and
    jc .fail
.loop:
    call bas_skip_spaces
    push si
    mov di, bas_kw_or
    call bas_match_word
    jc .done
    pop bx
    push ax
    call bas_parse_and
    jc .rhs_fail
    mov cx, ax
    pop bx
    xor ax, ax
    test bx, bx
    jnz .true
    test cx, cx
    jz .loop
.true:
    mov ax, 1
    jmp .loop
.done:
    pop si
    clc
    ret
.rhs_fail:
    pop bx
.fail:
    stc
    ret

bas_parse_and:
    call bas_parse_not
    jc .fail
.loop:
    call bas_skip_spaces
    push si
    mov di, bas_kw_and
    call bas_match_word
    jc .done
    pop bx
    push ax
    call bas_parse_not
    jc .rhs_fail
    mov cx, ax
    pop bx
    xor ax, ax
    test bx, bx
    jz .loop
    test cx, cx
    jz .loop
    mov ax, 1
    jmp .loop
.done:
    pop si
    clc
    ret
.rhs_fail:
    pop bx
.fail:
    stc
    ret

bas_parse_not:
    push bx
    xor bx, bx
.scan:
    call bas_skip_spaces
    push si
    mov di, bas_kw_not
    call bas_match_word
    jc .value
    pop ax
    xor bl, 1
    jmp .scan
.value:
    pop si
    push bx
    call bas_parse_compare
    pop bx
    jc .fail
    test bl, 1
    jz .ok
    test ax, ax
    jz .not_true
    xor ax, ax
    jmp .ok
.not_true:
    mov ax, 1
.ok:
    clc
    pop bx
    ret
.fail:
    stc
    pop bx
    ret

bas_parse_compare:
    call bas_parse_add
    jc .fail
    push ax
    call bas_skip_spaces

    mov dl, 0
    mov al, [si]
    cmp al, '='
    je .eq
    cmp al, '<'
    je .ltlead
    cmp al, '>'
    je .gtlead
    pop ax
    clc
    ret
.eq:
    mov dl, 1
    inc si
    jmp .right
.ltlead:
    inc si
    cmp byte [si], '='
    je .le
    cmp byte [si], '>'
    je .ne
    mov dl, 2
    jmp .right
.le:
    mov dl, 3
    inc si
    jmp .right
.ne:
    mov dl, 4
    inc si
    jmp .right
.gtlead:
    inc si
    cmp byte [si], '='
    je .ge
    mov dl, 5
    jmp .right
.ge:
    mov dl, 6
    inc si
.right:
    push dx
    call bas_parse_add
    jc .right_fail
    mov cx, ax
    pop dx
    pop bx
    xor ax, ax
    cmp dl, 1
    je .cmp_eq
    cmp dl, 2
    je .cmp_lt
    cmp dl, 3
    je .cmp_le
    cmp dl, 4
    je .cmp_ne
    cmp dl, 5
    je .cmp_gt
    cmp bx, cx
    jge .true
    jmp .false
.cmp_eq:
    cmp bx, cx
    je .true
    jmp .false
.cmp_lt:
    cmp bx, cx
    jl .true
    jmp .false
.cmp_le:
    cmp bx, cx
    jle .true
    jmp .false
.cmp_ne:
    cmp bx, cx
    jne .true
    jmp .false
.cmp_gt:
    cmp bx, cx
    jg .true
    jmp .false
.true:
    mov ax, 1
.false:
    clc
    ret
.right_fail:
    pop dx
    pop ax
.fail:
    stc
    ret

bas_parse_add:
    call bas_parse_mul
    jc .fail
.loop:
    call bas_skip_spaces
    mov dl, [si]
    cmp dl, '+'
    je .op
    cmp dl, '-'
    jne .done
.op:
    inc si
    push dx
    push ax
    call bas_parse_mul
    jc .rhs_fail
    mov cx, ax
    pop bx
    pop dx
    cmp dl, '+'
    je .add
    mov ax, bx
    sub ax, cx
    jo .overflow
    jmp .loop
.add:
    mov ax, bx
    add ax, cx
    jo .overflow
    jmp .loop
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    ret
.rhs_fail:
    pop ax
    pop dx
.fail:
    stc
    ret
.done:
    clc
    ret

bas_parse_mul:
    call bas_parse_unary
    jc .fail
.loop:
    call bas_skip_spaces
    mov dl, 0
    mov bl, [si]
    cmp bl, '*'
    je .mulop
    cmp bl, '/'
    je .divop
    push ax
    push si
    mov di, bas_kw_mod
    call bas_match_word
    jc .notmod
    pop bx
    add sp, 2
    mov dl, 3
    jmp .haveop
.notmod:
    pop si
    pop ax
    jmp .done
.mulop:
    mov dl, 1
    inc si
    jmp .haveop
.divop:
    mov dl, 2
    inc si
.haveop:
    push dx
    push ax
    call bas_parse_unary
    jc .rhs_fail
    mov cx, ax
    pop bx
    pop dx
    cmp dl, 1
    je .do_mul
    test cx, cx
    jz .divzero
    cmp bx, 8000h
    jne .select_div
    cmp cx, 0FFFFh
    je .overflow
.select_div:
    cmp dl, 3
    je .do_mod
.do_div:
    mov ax, bx
    cwd
    idiv cx
    jmp .loop
.do_mod:
    mov ax, bx
    cwd
    idiv cx
    mov ax, dx
    jmp .loop
.do_mul:
    mov ax, bx
    imul cx
    jo .overflow
    jmp .loop
.divzero:
    mov byte [bas_error_code], BERR_DIVZERO
    stc
    ret
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    ret
.rhs_fail:
    pop ax
    pop dx
.fail:
    stc
    ret
.done:
    clc
    ret

bas_parse_unary:
    push bx
    xor bx, bx
.loop:
    call bas_skip_spaces
    cmp byte [si], '+'
    je .plus
    cmp byte [si], '-'
    jne .primary
    xor bl, 1
.plus:
    inc si
    jmp .loop
.primary:
    ; BL is the accumulated unary sign flag. bas_parse_primary is free to
    ; use BX (variables, PEEK addresses and nested expressions do), so preserve
    ; the flag explicitly across the primary parse. Without this, PEEK(odd)
    ; and parenthesized expressions whose parser leaves BL odd are negated.
    push bx
    call bas_parse_primary
    pop bx
    jc .fail
    test bl, 1
    jz .ok
    neg ax
    jo .overflow
.ok:
    clc
    pop bx
    ret
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
.fail:
    stc
    pop bx
    ret

%macro BAS_PRIMARY_TRY 2
    push si
    mov di, %1
    call bas_match_word
    jc %%miss
    pop bx
    jmp %2
%%miss:
    pop si
%endmacro

bas_parse_primary:
    call bas_skip_spaces

    BAS_PRIMARY_TRY bas_kw_rescount, .rescount
    BAS_PRIMARY_TRY bas_kw_reslen, .reslen
    BAS_PRIMARY_TRY bas_kw_resbyte, .resbyte
    BAS_PRIMARY_TRY bas_kw_resword, .resword
    BAS_PRIMARY_TRY bas_kw_val, .val
    BAS_PRIMARY_TRY bas_kw_event, .event
    BAS_PRIMARY_TRY bas_kw_value, .value
    BAS_PRIMARY_TRY bas_kw_peek, .peek
    BAS_PRIMARY_TRY bas_kw_inkey, .inkey
    BAS_PRIMARY_TRY bas_kw_ticks, .ticks
    BAS_PRIMARY_TRY bas_kw_mouselx, .mouselx
    BAS_PRIMARY_TRY bas_kw_mousely, .mousely
    BAS_PRIMARY_TRY bas_kw_mousex, .mousex
    BAS_PRIMARY_TRY bas_kw_mousey, .mousey
    BAS_PRIMARY_TRY bas_kw_mouseb, .mouseb
    BAS_PRIMARY_TRY bas_kw_screenw, .screenw
    BAS_PRIMARY_TRY bas_kw_screenh, .screenh
    BAS_PRIMARY_TRY bas_kw_screenbpp, .screenbpp
    BAS_PRIMARY_TRY bas_kw_videomode, .videomode
    BAS_PRIMARY_TRY bas_kw_adlib, .adlib
    BAS_PRIMARY_TRY bas_kw_fre, .fre
    BAS_PRIMARY_TRY bas_kw_rnd, .rnd

    mov al, [si]
    cmp al, '('
    jne .not_paren
    jmp .paren
.not_paren:
    cmp al, '0'
    jb .variable
    cmp al, '9'
    ja .variable
    call bas_parse_uword
    jnc .number_ok
    jmp .syntax
.number_ok:
    cmp ax, 32767
    jbe .number_range
    jmp .overflow
.number_range:
    clc
    ret

.variable:
    cmp al, 'a'
    jb .upper
    cmp al, 'z'
    ja .upper
    sub al, 32
.upper:
    cmp al, 'A'
    jae .var_low_ok
    jmp .syntax
.var_low_ok:
    cmp al, 'Z'
    jbe .var_high_ok
    jmp .syntax
.var_high_ok:
    sub al, 'A'
    xor ah, ah
    mov bx, ax
    shl bx, 1
    inc si
    call bas_skip_spaces
    cmp byte [si], '('
    je .array_value
    mov ax, [bas_vars + bx]
    clc
    ret
.array_value:
    push bx
    inc si
    call bas_parse_expr
    jc .array_fail
    test ax, ax
    js .array_illegal_pop
    mov cx, ax
    call bas_skip_spaces
    cmp byte [si], ')'
    jne .array_syntax_pop
    inc si
    pop bx
    mov ax, [bas_array_size + bx]
    test ax, ax
    jz .illegal
    cmp cx, ax
    jae .illegal
    mov ax, [bas_array_base + bx]
    add ax, cx
    shl ax, 1
    mov bx, ax
    mov ax, [bas_array_data + bx]
    clc
    ret
.array_fail:
    pop bx
    stc
    ret
.array_illegal_pop:
    pop bx
    jmp .illegal
.array_syntax_pop:
    pop bx
    jmp .syntax

.rescount:
    mov al, 2
    call bas_expr_res
    ret
.reslen:
    mov al, 1
    call bas_expr_res
    ret
.resbyte:
    mov al, 3
    call bas_expr_res
    ret
.resword:
    mov al, 4
    call bas_expr_res
    ret

.inkey:
    mov ax, [bas_last_key]
    mov word [bas_last_key], 0
    clc
    ret

.event:
    call bas_expr_event
    ret

.value:
    call bas_expr_value
    ret

.val:
    call bas_expr_val
    ret

.peek:
    call bas_skip_spaces
    cmp byte [si], '('
    je .peek_open
    jmp .syntax
.peek_open:
    inc si
    call bas_parse_expr
    jnc .peek_expr_ok
    stc
    ret
.peek_expr_ok:
    test ax, ax
    jns .peek_nonneg
    jmp .illegal
.peek_nonneg:
    cmp ax, BAS_POKE_CAP - 1
    jbe .peek_range
    jmp .illegal
.peek_range:
    mov bx, ax
    call bas_skip_spaces
    cmp byte [si], ')'
    je .peek_close
    jmp .syntax
.peek_close:
    inc si
    xor ax, ax
    mov al, [bas_poke_bank + bx]
    clc
    ret

.ticks:
    call OSAPI_GET_TICKS
    clc
    ret

.mouselx:
    call OSAPI_MOUSE
    mov ax, cx
    sub ax, [bas_content_x]
    sub ax, BAS_MARGIN
    clc
    ret

.mousely:
    call OSAPI_MOUSE
    mov ax, dx
    sub ax, [bas_content_y]
    sub ax, BAS_MARGIN
    clc
    ret

.mousex:
    call OSAPI_MOUSE
    mov ax, cx
    clc
    ret

.mousey:
    call OSAPI_MOUSE
    mov ax, dx
    clc
    ret

.mouseb:
    call OSAPI_MOUSE
    xor ah, ah
    clc
    ret

.screenw:
    call OSAPI_VIDEO
    clc
    ret

.screenh:
    call OSAPI_VIDEO
    mov ax, bx
    clc
    ret

.screenbpp:
    call OSAPI_VIDEO
    xor ax, ax
    mov al, dh
    clc
    ret

.videomode:
    call OSAPI_VIDEO
    xor ax, ax
    mov al, dl
    clc
    ret

.adlib:
    call OSAPI_SND_CAPS
    test ax, SND_CAP_FM
    jz .adlib_no
    mov ax, -1
    clc
    ret
.adlib_no:
    xor ax, ax
    clc
    ret

.fre:
    call bas_skip_spaces
    cmp byte [si], '('
    je .fre_open
    jmp .syntax
.fre_open:
    inc si
    call bas_skip_spaces
    cmp byte [si], '0'
    je .fre_zero
    jmp .syntax
.fre_zero:
    inc si
    call bas_skip_spaces
    cmp byte [si], ')'
    je .fre_close
    jmp .syntax
.fre_close:
    inc si
    push bx
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P             ; AX = largest claim, in PARAGRAPHS
    pop bx
    cmp ax, 2047
    jbe .fre_scale
    mov ax, 32767                ; signed BASIC integer saturation
    clc
    ret
.fre_scale:
    mov cl, 4                    ; paragraphs -> bytes; 8086 shift count in CL
    shl ax, cl
    clc
    ret

.rnd:
    call bas_skip_spaces
    cmp byte [si], '('
    je .rnd_open
    jmp .syntax
.rnd_open:
    inc si
    call bas_parse_expr
    jnc .rnd_expr_ok
    stc
    ret
.rnd_expr_ok:
    test ax, ax
    jg .rnd_positive
    jmp .illegal
.rnd_positive:
    mov cx, ax
    call bas_skip_spaces
    cmp byte [si], ')'
    je .rnd_close
    jmp .syntax
.rnd_close:
    inc si
    call OSAPI_RAND
    xor dx, dx
    div cx
    mov ax, dx
    clc
    ret

.paren:
    inc si
    inc word [bas_expr_depth]
    cmp word [bas_expr_depth], 8
    jbe .paren_depth_ok
    dec word [bas_expr_depth]
    jmp .syntax
.paren_depth_ok:
    call bas_parse_expr
    jc .paren_fail
    call bas_skip_spaces
    cmp byte [si], ')'
    je .paren_close
    mov byte [bas_error_code], BERR_SYNTAX
    jmp .paren_fail
.paren_close:
    inc si
    dec word [bas_expr_depth]
    clc
    ret
.paren_fail:
    dec word [bas_expr_depth]
    stc
    ret

.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
    ret
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
    ret
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    ret

; -----------------------------------------------------------------------------
; SAVE/LOAD. SAVE "NAME" writes in current program directory;
; LOAD "NAME" or LOAD "\GAMES\2B99" adopts the source directory.
; .BAS is appended to the 8.3 filename.
; -----------------------------------------------------------------------------
; LOAD "\GAMES\2B99" -- directory-aware program loading, SDK ABI 10.
; File APIs resolve 8.3 names in the package's per-instance working directory.
; FILE_FIND supplies each folder cluster, FILE_GOTO_QM makes it the instance's
; working directory; LOADRES/LOADBMP/SAVE then work relative to that folder.
; Save the previous directory and roll it back on ANY unsuccessful LOAD.
; Paths accept a leading \ for volume root, otherwise resolve from current cwd.
; Folder components are deliberately 8.3 basename-only, no traversal via '..'.
; -----------------------------------------------------------------------------
bas_parse_load_filename:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [bas_path_pending], 0
    call bas_skip_spaces
    cmp byte [si], '"'
    BAS_JNZ .syntax
    inc si
    mov di, bas_path_buf
    mov cx, BAS_PATH_CAP - 1
.copy:
    mov al, [si]
    test al, al
    BAS_JZ .syntax
    cmp al, '"'
    BAS_JZ .copied
    cmp al, 32
    BAS_JBE .syntax
    cmp al, 126
    BAS_JA .syntax
    test cx, cx
    BAS_JZ .syntax
    call bas_upper_al
    mov [di], al               ; no STOSB: ES enters callback as KERNEL_SEG
    inc di
    inc si
    dec cx
    jmp .copy
.copied:
    mov byte [di], 0
    inc si
    call bas_expect_end
    BAS_JC .syntax
    mov si, bas_path_buf
    mov ax, si
    mov [bas_path_leaf], ax
.scan:
    mov al, [si]
    test al, al
    BAS_JZ .scanned
    cmp al, '\'
    BAS_JNZ .scan_next
    mov ax, si
    inc ax
    mov [bas_path_leaf], ax
.scan_next:
    inc si
    jmp .scan
.scanned:
    mov si, [bas_path_leaf]
    cmp byte [si], 0
    BAS_JZ .syntax
    ; Parse the leaf in-place, separate from BASIC's command/scratch buffer.
    ; Do not rewrite bas_linebuf (aliased with parser scratch) while
    ; resolving quoted paths. The path loader instead uses
    ; a bounded, explicit 8.3 leaf decoder. SAVE and plain BAS syntax do not
    ; depend on a second mutable command string.
    mov di, bas_filename
    xor bx, bx
.leaf_base:
    mov al, [si]
    test al, al
    BAS_JZ .leaf_append
    cmp al, '.'
    BAS_JZ .leaf_ext
    cmp al, 'A'
    BAS_JB .leaf_not_letter
    cmp al, 'Z'
    BAS_JBE .leaf_valid
.leaf_not_letter:
    cmp al, '0'
    BAS_JB .leaf_special
    cmp al, '9'
    BAS_JBE .leaf_valid
.leaf_special:
    cmp al, '_'
    BAS_JZ .leaf_valid
    cmp al, '-'
    BAS_JNZ .syntax
.leaf_valid:
    cmp bx, 8
    BAS_JAE .syntax
    mov [di], al
    inc di
    inc si
    inc bx
    jmp .leaf_base
.leaf_ext:
    test bx, bx
    BAS_JZ .syntax
    cmp byte [si+1], 'B'
    BAS_JNZ .syntax
    cmp byte [si+2], 'A'
    BAS_JNZ .syntax
    cmp byte [si+3], 'S'
    BAS_JNZ .syntax
    cmp byte [si+4], 0
    BAS_JNZ .syntax
    jmp .leaf_finish
.leaf_append:
    test bx, bx
    BAS_JZ .syntax
.leaf_finish:
    mov byte [di], '.'
    mov byte [di+1], 'B'
    mov byte [di+2], 'A'
    mov byte [di+3], 'S'
    mov byte [di+4], 0
    mov ax, [bas_path_leaf]
    cmp ax, bas_path_buf
    je .okay           ; plain LOAD name stays in current working directory

    call OSAPI_FILE_HERE
    mov [bas_path_old_cluster], dx
    mov [bas_path_old_drive], bl
    mov [bas_path_drive], bl
    mov byte [bas_path_pending], 1
    mov si, bas_path_buf
    cmp byte [si], '\'
    BAS_JNZ .folder
    xor dx, dx               ; leading backslash means volume root
    mov bl, [bas_path_drive]
    call OSAPI_FILE_GOTO_QM
    BAS_JC .folder_missing
    inc si
.folder:
    cmp si, [bas_path_leaf]
    BAS_JAE .try_leaf_directory
    mov di, bas_dir_name
    mov cx, 8
.component:
    mov al, [si]
    cmp al, '\'
    je .component_end
    cmp al, 'A'
    BAS_JB .component_not_letter
    cmp al, 'Z'
    BAS_JBE .component_char
.component_not_letter:
    cmp al, '0'
    BAS_JB .component_special
    cmp al, '9'
    BAS_JBE .component_char
.component_special:
    cmp al, '_'
    je .component_char
    cmp al, '-'
    BAS_JNZ .syntax
.component_char:
    test cx, cx
    BAS_JZ .syntax
    mov [di], al
    inc di
    inc si
    dec cx
    jmp .component
.component_end:
    cmp di, bas_dir_name
    je .syntax
    mov byte [di], 0
    inc si
    mov [bas_path_next], si
    call bas_path_find_dir
    BAS_JC .folder_missing
    mov si, [bas_path_next]
    jmp .folder

; The final name can itself be a directory.  For example,
;   LOAD "\GAMES\2B99"
; enters the 2B99 folder and selects its ONLY .BAS file.  An explicit
; extension always means a file, even when a similarly named folder exists.
; When there is no matching folder, retain ordinary LOAD "NAME.BAS" behavior.
.try_leaf_directory:
    mov si, [bas_path_leaf]
    mov di, bas_dir_name
    mov cx, 8
.copy_last:
    mov al, [si]
    test al, al
    BAS_JZ .last_ready
    cmp al, '.'
    BAS_JZ .okay             ; explicit .BAS: never infer a folder
    test cx, cx
    BAS_JZ .okay             ; already validated by filename parser
    mov [di], al
    inc di
    inc si
    dec cx
    jmp .copy_last
.last_ready:
    mov byte [di], 0
    call bas_path_find_dir
    BAS_JC .okay             ; no such folder: use the ordinary BAS filename
    call bas_path_pick_single_bas
    BAS_JC .failed           ; zero or multiple BAS: preserve error reason
    jmp .okay
.folder_missing:
    mov byte [bas_error_code], BERR_FILENOENT
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.failed:
    stc
    jmp .out
.okay:
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

; Find one subfolder by its canonical uppercase 8.3 display name.
; Uses only the public directory ordinal API; no direct FAT access.
bas_path_find_dir:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    xor cx, cx
.next:
    mov di, bas_dir_record
    call OSAPI_FILE_FIND
    BAS_JC .not_found
    cmp word [bas_dir_record+14], OSAPI_FT_DIR
    BAS_JNZ .next
    mov si, bas_dir_name
    mov di, bas_dir_record
.compare:
    mov al, [si]
    cmp al, [di]
    BAS_JNZ .next
    test al, al
    BAS_JZ .found
    inc si
    inc di
    jmp .compare
.found:
    mov dx, [bas_dir_record+16] ; first cluster of this directory
    mov bl, [bas_path_drive]
    call OSAPI_FILE_GOTO_QM
    jmp .out
.not_found:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The instance now stands inside the requested final folder. Select one
; .BAS, not the first arbitrarily. OSAPI_FILE_FIND returns a 13-byte NUL name,
; a type word at +14, and next ordinal in CX. The old path-next scratch word
; is reused as the match counter after directory traversal is complete.
; A missing .BAS and an ambiguous folder leave the old BASIC program intact.
bas_path_pick_single_bas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov word [bas_path_next], 0
    xor cx, cx
.next:
    mov di, bas_dir_record
    call OSAPI_FILE_FIND
    BAS_JC .end_scan
    cmp word [bas_dir_record+14], OSAPI_FT_DIR
    BAS_JAE .next            ; ignore folders, '..', etc.
    mov si, bas_dir_record
.look_dot:
    mov al, [si]
    test al, al
    BAS_JZ .next
    cmp al, '.'
    je .check_ext
    inc si
    jmp .look_dot
.check_ext:
    mov al, [si+1]
    call bas_upper_al
    cmp al, 'B'
    BAS_JNZ .next
    mov al, [si+2]
    call bas_upper_al
    cmp al, 'A'
    BAS_JNZ .next
    mov al, [si+3]
    call bas_upper_al
    cmp al, 'S'
    BAS_JNZ .next
    cmp byte [si+4], 0
    BAS_JNZ .next
    cmp word [bas_path_next], 0
    BAS_JNZ .ambiguous
    mov word [bas_path_next], 1
    mov si, bas_dir_record
    mov di, bas_filename
    push cx
    mov cx, BAS_FILENAME_CAP
    rep movsb
    pop cx
    jmp .next
.end_scan:
    cmp word [bas_path_next], 1
    je .one_file
    mov byte [bas_error_code], BERR_FILENOENT
    stc
    jmp .out
.ambiguous:
    mov byte [bas_error_code], BERR_MULTIBAS
    stc
    jmp .out
.one_file:
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

bas_load_rollback:
    push ax
    push bx
    push dx
    cmp byte [bas_path_pending], 0
    BAS_JZ .out
    mov dx, [bas_path_old_cluster]
    mov bl, [bas_path_old_drive]
    call OSAPI_FILE_GOTO_QM
    mov byte [bas_path_pending], 0
.out:
    pop dx
    pop bx
    pop ax
    ret

; Reset CONSOLE presentation, not BASIC memory: NEW deletes source instead.
; Exactly as at startup: default title, foreground/background, blank transcript,
; text mode, clean prompt, cleared graphics and default working directory.
bas_reset_console:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov dx, [bas_home_cluster]
    mov bl, [bas_home_drive]
    call OSAPI_FILE_GOTO_QM
    ; Invalid/removable launch medium: still reset visuals even if navigation fails.
    mov byte [bas_path_pending], 0
    mov byte [bas_screen_mode], 0
    mov byte [bas_fg_color], CBLACK
    mov byte [bas_bg_color], CWHITE
    mov byte [bas_prompt_fg], CBLACK
    mov byte [bas_prompt_bg], CWHITE
    mov byte [bas_auto_mode], 0
    mov word [bas_input_len], 0
    mov word [bas_input_pos], 0
    mov byte [bas_input], 0
    mov word [bas_prompt_last_cols], 0
    mov byte [bas_history], 0
    mov word [bas_history_len], 0
    mov byte [bas_print_nonl], 0
    mov word [bas_gosub_count], 0
    mov word [bas_for_count], 0
    mov byte [bas_break_req], 0
    mov byte [bas_run_state], BAS_STATE_READY
    mov byte [bas_draw_batch], 0
    mov word [bas_gfx_delta_count], 0
    mov byte [bas_filename], 0
    call bas_title_untitled
    call bas_cls
    mov si, bas_s_banner
    call bas_append_line
    mov si, bas_s_ready
    call bas_append_line
    ; One intentional chrome repaint on explicit RESET: ABI-10 has no
    ; OSAPI_WM_TITLE-only call. This is never used on LOAD, typing, or RUN.
    mov bx, [bas_window]
    call OSAPI_WM_HIDE
    call OSAPI_WM_SHOW
    ; The explicit show has presented the entire reset canvas. Prevent the
    ; Enter callback from immediately repainting those same 35 rows again.
    mov byte [bas_dirty], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; -----------------------------------------------------------------------------
bas_parse_filename:
    push di
    mov di, bas_ext_bas
    call bas_parse_typed_filename
    pop di
    jc .out
    call bas_expect_end
.out:
    ret

bas_parse_bmp_filename:
    push di
    mov di, bas_ext_bmp
    call bas_parse_typed_filename
    pop di
    ret

; Shared quoted 8.3 filename parser. DI points at a three-letter uppercase
; extension. The extension may be omitted in BASIC source and is then appended.
bas_parse_typed_filename:
    push ax
    push bx
    push cx
    push dx
    push di
    mov dx, di
    call bas_skip_spaces
    cmp byte [si], '"'
    jne .fail
    inc si
    mov di, bas_filename
    xor bx, bx
.base:
    mov al, [si]
    cmp al, 0
    je .fail
    cmp al, '"'
    je .append_ext
    cmp al, '.'
    je .given_ext
    call bas_upper_al
    cmp al, 'A'
    jb .maybe_digit
    cmp al, 'Z'
    jbe .store
.maybe_digit:
    cmp al, '0'
    jb .maybe_special
    cmp al, '9'
    jbe .store
.maybe_special:
    cmp al, '_'
    je .store
    cmp al, '-'
    jne .fail
.store:
    cmp bx, 8
    jae .fail
    stosb
    inc bx
    inc si
    jmp .base
.given_ext:
    test bx, bx
    jz .fail
    inc si
    push di
    mov di, dx
    mov cx, 3
.check_ext:
    mov al, [si]
    call bas_upper_al
    cmp al, [di]
    jne .bad_ext
    inc si
    inc di
    loop .check_ext
    cmp byte [si], '"'
    jne .bad_ext
    pop di
    jmp .write_ext
.bad_ext:
    pop di
    jmp .fail
.append_ext:
    test bx, bx
    jz .fail
.write_ext:
    mov al, '.'
    stosb
    push si
    mov si, dx
    mov cx, 3
    rep movsb
    pop si
    mov byte [di], 0
    inc si
    call bas_skip_spaces
    clc
    jmp .out
.fail:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_upper_al:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret

; Emit unsigned word to ES:DI for the temporary SAVE staging block.
; Normal STR$/PRINT use bas_word_to_dec (which explicitly writes DS:DI);
; preserving ES here avoids silently losing ASCII line numbers on SAVE.
bas_word_to_dec_file:
    push ax
    push bx
    push cx
    push dx
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    xor cx, cx
    mov bx, 10
.file_divide:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .file_divide
.file_digit:
    pop ax
    add al, '0'
    stosb
    loop .file_digit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_save_program:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call bas_parse_filename
    jc .syntax

    ; The kernel file ABI is whole-file. Keep its 5 KiB staging outside the
    ; package and resident BSS: allocate only for this SAVE, then free it.
    mov ax, BAS_IO_KB
    call bas_claim_kb
    jc .no_memory
    mov [bas_io_seg], dx
    mov es, dx
    xor di, di
    mov word [bas_last_line], 0
.next:
    mov ax, [bas_last_line]
    call bas_find_next_after
    jc .staged
    mov [bas_last_line], dx
    mov ax, dx
    ; SAVE is different from ordinary STR$/PRINT: ES points to the
    ; temporary file staging claim. Do not redirect STOSB back to DS.
    call bas_word_to_dec_file
    mov al, ' '
    stosb
    add si, 3
.copy:
    lodsb
    test al, al
    jz .eol
    stosb
    jmp .copy
.eol:
    mov al, 13
    stosb
    mov al, 10
    stosb
    jmp .next

.staged:
    mov cx, di
    mov [bas_io_len], cx
    xor bx, bx
    mov si, bas_filename
    xor dx, dx
    call OSAPI_FILE_WRITE
    jc .file_error_ext

    push ds
    pop es
    call bas_free_io
    mov si, bas_s_saved
    call bas_file_status
    call bas_title_filename
    clc
    jmp .out
.syntax:
    call bas_report_error
    stc
    jmp .out
.no_memory:
    mov si, bas_s_out_memory
    call bas_append_line
    stc
    jmp .out
.file_error_ext:
    ; Preserve the file error in AX while returning ES to our data segment and
    ; releasing the temporary staging grant.
    push ax
    push ds
    pop es
    call bas_free_io
    pop ax
    call bas_report_file_error
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Validate the complete textual program before replacing the current one.
; Files must contain strictly increasing numbered lines. This makes LOAD atomic
; without retaining a second full program arena.
bas_validate_io_program:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, [bas_io_seg]
    mov es, ax
    xor si, si
    mov cx, [bas_io_len]
    mov di, bas_linebuf
    xor bx, bx                        ; current textual line length
    xor dx, dx                        ; previous BASIC line number
    xor bp, bp                        ; packed bytes required
    mov word [bas_cap_tmp], 0         ; source line count
.parse:
    jcxz .eof
    mov al, [es:si]
    inc si
    dec cx
    cmp al, 13
    je .newline
    cmp al, 10
    je .newline
    cmp al, 32
    jb .bad
    cmp al, 126
    ja .bad
    cmp bx, BAS_INPUT_CAP
    jae .bad
    mov [di], al
    inc di
    inc bx
    jmp .parse
.newline:
    test bx, bx
    jz .parse
    mov byte [di], 0
    call .validate_line
    jc .bad
    mov di, bas_linebuf
    xor bx, bx
    jmp .parse
.eof:
    test bx, bx
    jz .good
    mov byte [di], 0
    call .validate_line
    jc .bad
.good:
    clc
    jmp .out
.bad:
    mov byte [bas_error_code], BERR_BADPROGRAM
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.validate_line:
    push ax
    push bx
    push cx
    push si
    mov si, bas_linebuf
    call bas_skip_spaces
    call bas_parse_uword
    jc .line_bad
    test ax, ax
    jz .line_bad
    cmp ax, dx
    jbe .line_bad
    mov dx, ax
    call bas_skip_spaces
    cmp byte [si], 0
    je .line_bad
    xor bx, bx
.source_len:
    cmp byte [si], 0
    je .source_done
    inc bx
    cmp bx, BAS_SRC_CAP
    ja .line_bad
    inc si
    jmp .source_len
.source_done:
    add bx, 4
    add bp, bx
    cmp bp, BAS_PROG_BYTES
    ja .line_bad
    inc word [bas_cap_tmp]
    cmp word [bas_cap_tmp], BAS_PROG_LINES
    ja .line_bad
    clc
    jmp .line_out
.line_bad:
    stc
.line_out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

bas_load_program:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; Parsing an attempted filename must not destroy the previous filename
    ; when LOAD fails. Preserve the displayed program identity transactionally.
    ; SI points to the command's quoted path. The backup uses SI as
    ; source; restore it before calling the path parser.
    push si
    push es
    push ds
    pop es
    mov si, bas_filename
    mov di, bas_prev_filename
    mov cx, BAS_FILENAME_CAP + 1
    rep movsb
    pop es
    pop si

    call bas_parse_load_filename
    jc .syntax

    mov ax, BAS_IO_KB
    call bas_claim_kb
    jc .no_memory
    mov [bas_io_seg], dx
    mov es, dx
    xor bx, bx
    mov cx, BAS_IO_CAP
    mov si, bas_filename
    xor dx, dx
    call OSAPI_FILE_READ
    jc .file_error_ext
    mov [bas_io_len], ax

    ; Validation reads the external grant through ES but keeps the current
    ; packed program intact until the complete source has passed.
    call bas_validate_io_program
    jc .bad_ext

    ; Program mutation routines use string instructions and expect ES=DS.
    push es
    push ds
    pop es
    call bas_new_program
    pop es

    xor si, si
    mov cx, [bas_io_len]
    mov di, bas_linebuf
    xor bx, bx
.parse:
    jcxz .eof
    mov al, [es:si]
    inc si
    dec cx
    cmp al, 13
    je .newline
    cmp al, 10
    je .newline
    mov [di], al
    inc di
    inc bx
    jmp .parse
.newline:
    test bx, bx
    jz .parse
    mov byte [di], 0
    push si
    push cx
    push es
    push ds
    pop es
    mov si, bas_linebuf
    call bas_store_line
    pop es
    pop cx
    pop si
    jc .bad_after_commit_ext
    mov di, bas_linebuf
    xor bx, bx
    jmp .parse
.eof:
    test bx, bx
    jz .loaded
    mov byte [di], 0
    push es
    push ds
    pop es
    mov si, bas_linebuf
    call bas_store_line
    pop es
    jc .bad_after_commit_ext
.loaded:
    push ds
    pop es
    call bas_free_io
    call bas_clear_vars
    mov word [bas_gosub_count], 0
    mov word [bas_for_count], 0
    ; Commit new working directory ONLY after a validated successful LOAD.
    mov byte [bas_path_pending], 0
    mov si, bas_s_loaded
    call bas_file_status
    call bas_title_filename
    clc
    jmp .out
.bad_ext:
    push ds
    pop es
    call bas_free_io
    call bas_report_error
    stc
    jmp .out
.bad_after_commit_ext:
    push ds
    pop es
    call bas_free_io
    ; Validation should make this unreachable; leave a clean empty program if
    ; an internal inconsistency is ever detected.
    call bas_new_program
    mov byte [bas_error_code], BERR_BADPROGRAM
    call bas_report_error
    stc
    jmp .out
.syntax:
    call bas_report_error
    stc
    jmp .out
.no_memory:
    mov si, bas_s_out_memory
    call bas_append_line
    stc
    jmp .out
.file_error_ext:
    push ax
    push ds
    pop es
    call bas_free_io
    pop ax
    call bas_report_file_error
    stc
.out:
    ; The command dispatcher consumes CF from LOAD. DIRECTORY rollback
    ; performs comparisons and a filesystem call which overwrite FLAGS;
    ; preserve the original success/error result across that cleanup.
    pushf
    jnc .keep_name
    push ds
    pop es
    mov si, bas_prev_filename
    mov di, bas_filename
    mov cx, BAS_FILENAME_CAP + 1
    rep movsb
.keep_name:
    call bas_load_rollback
    popf
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Release the transient ASCII LOAD/SAVE staging grant. The allocator stamps the
; block to this application instance, and MEM_FREE preserves all registers.
bas_free_io:
    push dx
    mov dx, [bas_io_seg]
    test dx, dx
    jz .done
    call OSAPI_MEM_FREE
    mov word [bas_io_seg], 0
.done:
    pop dx
    ret

; Build and emit "PREFIX FILENAME". SI points at prefix.
bas_file_status:
    push ax
    push cx
    push si
    push di
    mov di, bas_scratch
    mov cx, BAS_SCRATCH_CAP - 1
.copy_prefix:
    lodsb
    test al, al
    jz .space
    stosb
    loop .copy_prefix
.space:
    mov al, ' '
    stosb
    mov si, bas_filename
.copy_name:
    lodsb
    stosb
    test al, al
    jz .emit
    loop .copy_name
    mov byte [di - 1], 0
.emit:
    mov si, bas_scratch
    call bas_append_line
    pop di
    pop si
    pop cx
    pop ax
    ret

; Convert an os8088 file error in AX into a normal interpreter error.
; This allows file operations used by RUN to stop with the source line number,
; while direct mode reports the same message without an IN <line> suffix.
bas_set_file_error:
    cmp ax, FERR_NOENT
    je .noent
    cmp ax, FERR_BIG
    je .big
    cmp ax, FERR_FULL
    je .full
    cmp ax, FERR_DIRFULL
    je .full
    cmp ax, FERR_WPROT
    je .wprot
    cmp ax, FERR_PROT
    je .wprot
    cmp ax, FERR_NAME
    je .badname
    mov byte [bas_error_code], BERR_FILE
    ret
.noent:
    mov byte [bas_error_code], BERR_FILENOENT
    ret
.big:
    mov byte [bas_error_code], BERR_FILEBIG
    ret
.full:
    mov byte [bas_error_code], BERR_DISKFULL
    ret
.wprot:
    mov byte [bas_error_code], BERR_WRITEPROT
    ret
.badname:
    mov byte [bas_error_code], BERR_BADNAME
    ret

bas_report_file_error:
    push si
    cmp ax, FERR_NOENT
    je .noent
    cmp ax, FERR_BIG
    je .big
    cmp ax, FERR_FULL
    je .full
    cmp ax, FERR_DIRFULL
    je .full
    cmp ax, FERR_WPROT
    je .wprot
    cmp ax, FERR_PROT
    je .wprot
    cmp ax, FERR_NAME
    je .badname
    mov si, bas_s_file_error
    jmp .emit
.noent:
    mov si, bas_s_no_file
    jmp .emit
.big:
    mov si, bas_s_file_big
    jmp .emit
.full:
    mov si, bas_s_disk_full
    jmp .emit
.wprot:
    mov si, bas_s_write_prot
    jmp .emit
.badname:
    mov si, bas_s_bad_name
.emit:
    call bas_append_line
    pop si
    ret

bas_report_error:
    push si
    mov al, [bas_error_code]
    cmp al, BERR_DIVZERO
    je .divzero
    cmp al, BERR_UNDEFLINE
    je .undef
    cmp al, BERR_STEPLIMIT
    je .step
    cmp al, BERR_OVERFLOW
    je .overflow
    cmp al, BERR_PROGRAMFULL
    je .full
    cmp al, BERR_BADPROGRAM
    je .badprog
    cmp al, BERR_RETURN
    je .return
    cmp al, BERR_GOSUBFULL
    je .gosubfull
    cmp al, BERR_FORFULL
    je .forfull
    cmp al, BERR_NEXT
    je .next
    cmp al, BERR_FORNEXT
    je .fornext
    cmp al, BERR_GFXFULL
    je .gfxfull
    cmp al, BERR_ILLEGAL
    je .illegal
    cmp al, BERR_SCREEN
    je .screen
    cmp al, BERR_BMPFORMAT
    je .bmpformat
    cmp al, BERR_NOBMP
    je .nobmp
    cmp al, BERR_NOADLIB
    je .noadlib
    cmp al, BERR_FMREFUSED
    je .fmrefused
    cmp al, BERR_FILE
    je .file
    cmp al, BERR_FILENOENT
    je .filenoent
    cmp al, BERR_FILEBIG
    je .filebig
    cmp al, BERR_DISKFULL
    je .diskfull
    cmp al, BERR_WRITEPROT
    je .writeprot
    cmp al, BERR_BADNAME
    je .badname
    cmp al, BERR_BREAK
    je .break
    cmp al, BERR_NOWORKER
    je .noworker
    cmp al, BERR_PROGMODE
    je .progmode
    cmp al, BERR_REDO
    je .redo
    cmp al, BERR_FORMFULL
    je .formfull
    cmp al, BERR_NOFORM
    je .noform
    cmp al, BERR_BADCONTROL
    je .badcontrol
    cmp al, BERR_RESFORMAT
    je .resformat
    cmp al, BERR_NORES
    je .nores
    cmp al, BERR_NOMEM
    je .nomem
    cmp al, BERR_VARSTORE
    je .varstore
    cmp al, BERR_VARFULL
    je .varfull
    cmp al, BERR_MULTIBAS
    BAS_JZ .multibas
    mov si, bas_s_syntax
    jmp .emit
.divzero:
    mov si, bas_s_divzero
    jmp .emit
.undef:
    mov si, bas_s_undef
    jmp .emit
.step:
    mov si, bas_s_step
    jmp .emit
.overflow:
    mov si, bas_s_overflow
    jmp .emit
.full:
    mov si, bas_s_prog_full
    jmp .emit
.badprog:
    mov si, bas_s_bad_program
    jmp .emit
.return:
    mov si, bas_s_return
    jmp .emit
.gosubfull:
    mov si, bas_s_gosub_full
    jmp .emit
.forfull:
    mov si, bas_s_for_full
    jmp .emit
.next:
    mov si, bas_s_next
    jmp .emit
.fornext:
    mov si, bas_s_for_next
    jmp .emit
.gfxfull:
    mov si, bas_s_gfx_full
    jmp .emit
.illegal:
    mov si, bas_s_illegal
    jmp .emit
.screen:
    mov si, bas_s_screen
    jmp .emit
.bmpformat:
    mov si, bas_s_bmp_format
    jmp .emit
.nobmp:
    mov si, bas_s_no_bmp
    jmp .emit
.noadlib:
    mov si, bas_s_no_adlib
    jmp .emit
.fmrefused:
    mov si, bas_s_fm_refused
    jmp .emit
.file:
    mov si, bas_s_file_error
    jmp .emit
.filenoent:
    mov si, bas_s_no_file
    jmp .emit
.multibas:
    mov si, bas_s_multi_bas
    jmp .emit
.filebig:
    mov si, bas_s_file_big
    jmp .emit
.diskfull:
    mov si, bas_s_disk_full
    jmp .emit
.writeprot:
    mov si, bas_s_write_prot
    jmp .emit
.badname:
    mov si, bas_s_bad_name
    jmp .emit
.break:
    mov si, bas_s_break
    jmp .emit
.noworker:
    mov si, bas_s_no_worker
    jmp .emit
.progmode:
    mov si, bas_s_prog_mode
    jmp .emit
.formfull:
    mov si, bas_s_form_full
    jmp .emit
.noform:
    mov si, bas_s_no_form
    jmp .emit
.badcontrol:
    mov si, bas_s_bad_control
    jmp .emit
.resformat:
    mov si, bas_s_bad_res
    jmp .emit
.nores:
    mov si, bas_s_no_res
    jmp .emit
.nomem:
    mov si, bas_s_out_memory
    jmp .emit
.varstore:
    mov si, bas_s_var_store
    jmp .emit
.varfull:
    mov si, bas_s_var_full
    jmp .emit
.redo:
    mov si, bas_s_redo
.emit:
    call bas_emit_error_string
    pop si
    ret

; Append an error and, during RUN, the source line that raised it.
bas_emit_error_string:
    push ax
    push cx
    push si
    push di
    mov di, bas_scratch
    mov cx, BAS_SCRATCH_CAP - 12
.copy:
    lodsb
    stosb
    test al, al
    jz .copied
    loop .copy
    mov byte [di - 1], 0
.copied:
    mov ax, [bas_current_line]
    test ax, ax
    jz .emit
    dec di
    mov al, ' '
    stosb
    mov al, 'I'
    stosb
    mov al, 'N'
    stosb
    mov al, ' '
    stosb
    mov ax, [bas_current_line]
    call bas_word_to_dec
    mov byte [di], 0
.emit:
    mov si, bas_scratch
    call bas_append_line
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Numeric and console helpers.

; -----------------------------------------------------------------------------
; Compact 2BASIC resource packs. 2BR1 stores 99 fixed 12x12 4bpp things as
; four 50x50 atlases. The four existing bitmap slots are reused, so LOADRES
; adds no resident image buffers. PUTRES maps one thing to cropped PUTBMP data.
; -----------------------------------------------------------------------------
bas_parse_res_filename:
    push di
    mov di, bas_ext_res
    call bas_parse_typed_filename
    pop di
    ret

bas_resource_ready:
    ; A valid loaded pack (2BR2 or 2BR3) owns all four image slots.
    ; The BSS bank offset/count are committed ONLY after validation.
    push bx
    push cx
    cmp byte [bas_res_present], 1
    jne .no
    cmp word [bas_res_count], 0
    je .no
    cmp word [bas_image_data], 4232h     ; "2B"
    jne .no
    xor bx, bx
    mov cx, BAS_IMAGE_SLOTS
.check:
    cmp byte [bas_image_meta + bx], 2
    jne .no
    add bx, BAS_IMAGE_META_SIZE
    loop .check
    clc
    jmp .out
.no:
    stc
.out:
    pop cx
    pop bx
    ret

; Validated 2BR3: read-only packed entry names at bas_image_data+24.
; Input SI=upper-case NUL name; output DI=entry in our segment, CF clear.
; Entry records are exactly 24 bytes; only loaded 2BR3 supports names.
bas_res_find:
    push ax
    push bx
    push cx
    push si
    cmp byte [bas_res_present], 1
    jne .miss
    cmp word [bas_image_data + 2], 3352h  ; "R3"
    jne .miss
    mov cx, [bas_image_data + 6]
    mov di, bas_image_data + 24
.next:
    test cx, cx
    jz .miss
    push si
    push di
    mov bx, 12
.compare:
    mov al, [si]
    cmp al, [di]
    jne .different
    or al, al
    jz .match
    inc si
    inc di
    dec bx
    jnz .compare
.match:
    pop di
    pop si
    clc
    jmp .done
.different:
    pop di
    pop si
    add di, 24
    dec cx
    jmp .next
.miss:
    stc
.done:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; Validate an 8KB-or-smaller 2BR3 resource in ES:0000, AX=file bytes.
; Strict type, bounds, ordering and checksum checks before any live commit.
; On success: new sprite-bank offset/count staged in separate BSS.
bas_res_validate_v3:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [bas_res_length], ax
    cmp ax, 24
    jb .bad
    cmp word [es:0], 4232h
    jne .bad
    cmp word [es:2], 3352h
    jne .bad
    cmp byte [es:4], 3
    jne .bad
    cmp byte [es:5], 0
    jne .bad
    cmp word [es:8], 24
    jne .bad
    cmp byte [es:14], 24
    jne .bad
    cmp byte [es:15], 0
    jne .bad
    cmp ax, [es:12]
    jne .bad
    xor bx, bx
    mov cx, 6
    mov di, 18
.reserved:
    cmp byte [es:di], 0
    jne .bad
    inc di
    loop .reserved
    mov cx, [es:6]
    cmp cx, 32                  ; native bounded index: 32 named entries
    ja .bad
    mov ax, 24
    mov dx, cx
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add ax, dx                 ; 8 of the 24 bytes
    add ax, dx                 ; 16
    add ax, dx                 ; 24
    cmp ax, [es:10]
    jne .bad
    cmp ax, [bas_res_length]
    ja .bad
    mov [bas_res_prev_end], ax
    ; checksum is an unsigned 16-bit sum of all directory/data bytes.
    mov cx, [bas_res_length]
    sub cx, 24
    mov si, 24
    xor bx, bx
.sum:
    jcxz .sum_done
    xor ax, ax
    mov al, [es:si]
    add bx, ax
    inc si
    loop .sum
.sum_done:
    cmp bx, [es:16]
    jne .bad
    mov word [bas_res_new_off], 0
    mov word [bas_res_new_count], 0
    mov cx, [es:6]
    mov di, 24
.entry:
    test cx, cx
    jz .good
    ; Name must start A-Z. Remaining name bytes NUL/uppercase ASCII.
    mov al, [es:di]
    cmp al, 'A'
    jb .bad
    cmp al, 'Z'
    ja .bad
    ; Entries in host format are packed in ascending non-overlapping order.
    mov bx, [es:di+18]         ; file payload offset
    test bl, 1
    jnz .bad
    cmp bx, [bas_res_prev_end]
    jb .bad
    mov dx, [es:di+20]         ; size
    add bx, dx
    jc .bad
    cmp bx, [bas_res_length]
    ja .bad
    mov [bas_res_prev_end], bx
    mov al, [es:di+12]
    cmp al, 1
    jb .bad
    cmp al, 6
    ja .bad
    cmp al, 3
    jne .not_bank
    cmp byte [es:di+13], 1    ; mono sprite bank uses ink/transparent
    jne .bad
    cmp word [es:di+14], 16
    jne .bad
    cmp word [es:di+16], 16
    jne .bad
    mov bx, [es:di+22]        ; sprite count
    test bx, bx
    jz .bad
    cmp bx, 99
    ja .bad
    mov ax, bx
    mov bl, 5
    mov cl, bl
    shl ax, cl                 ; count*32
    cmp ax, dx
    jne .bad
    cmp word [bas_res_new_count], 0
    jne .bank_done
    mov [bas_res_new_count], ax
    shr ax, cl                 ; restore count
    mov [bas_res_new_count], ax
    mov ax, [es:di+18]
    mov [bas_res_new_off], ax
.bank_done:
    jmp .advance
.not_bank:
    ; v3 text/data/image metadata stays resident, and is available
    ; through RESLEN/RESBYTE/RESWORD without allocating extra buffers.
    cmp byte [es:di+13], 1
    ja .bad
    cmp al, 6
    jne .advance
    test dl, 1
    jnz .bad
    mov ax, dx
    shr ax, 1
    cmp ax, [es:di+22]
    jne .bad
.advance:
    add di, 24
    mov cx, [es:6]
    mov ax, di
    sub ax, 24
    mov bx, 24
    xor dx, dx
    div bx
    sub cx, ax
    jmp .entry
.good:
    clc
    jmp .out
.bad:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_exec_loadres:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call bas_parse_res_filename
    BAS_JC .fail
    call bas_expect_end
    BAS_JC .fail
    cmp byte [bas_worker_active], 0
    je .read_file
    cmp byte [bas_res_present], 1
    jne .noresource
    clc
    jmp .out
.read_file:
    ; Stage all I/O in a temporary 8KB claim, keeping the active pack intact
    ; until the new one has passed format/range/checksum validation.
    mov ax, 8
    call bas_claim_kb
    BAS_JC .no_mem
    mov [bas_res_stage], dx
    mov es, dx
    xor bx, bx
    mov cx, BAS_RES_CAP
    mov si, bas_filename
    xor dx, dx
    call OSAPI_FILE_READ
    BAS_JC .read_error
    test dx, dx
    BAS_JNZ .bad
    cmp ax, 8
    BAS_JB .bad
    mov [bas_res_length], ax
    mov es, [bas_res_stage]
    cmp word [es:0], 4232h
    BAS_JNZ .bad
    cmp word [es:2], 3252h
    BAS_JZ .legacy
    cmp word [es:2], 3352h
    BAS_JNZ .bad
    call bas_res_validate_v3
    BAS_JC .bad
    jmp .commit
.legacy:
    cmp word [bas_res_length], BAS_RES_FILE_BYTES
    BAS_JNZ .bad
    cmp word [es:4], 1010h
    BAS_JNZ .bad
    cmp word [es:6], 0163h
    BAS_JNZ .bad
    mov word [bas_res_new_off], 8
    mov word [bas_res_new_count], 99
.commit:
    ; DS=claim, ES=app, SS remains kernel low-stack. No OSAPI while DS swapped.
    push ds
    push ds
    pop es
    mov ax, [bas_res_stage]
    mov ds, ax
    xor si, si
    mov di, bas_image_data
    mov cx, [es:bas_res_length]
    cld
    rep movsb
    pop ds
    mov ax, [bas_res_new_off]
    mov [bas_res_offset], ax
    mov ax, [bas_res_new_count]
    mov [bas_res_count], ax
    mov byte [bas_res_present], 1
    mov di, bas_image_meta
    mov cx, BAS_IMAGE_SLOTS
.meta:
    mov byte [di], 2
    mov byte [di+1], 0
    mov word [di+2], 16
    mov word [di+4], 16
    mov word [di+6], 2
    add di, BAS_IMAGE_META_SIZE
    loop .meta
    mov word [bas_error_code], 0
    cmp word [bas_current_line], 0
    jne .release_ok
    mov si, bas_s_res_loaded
    call bas_file_status
.release_ok:
    mov byte [bas_dirty], 1
    clc
    jmp .release
.read_error:
    call bas_set_file_error
    stc
    jmp .release
.bad:
    mov byte [bas_error_code], BERR_RESFORMAT
    stc
    jmp .release
.no_mem:
    mov byte [bas_error_code], BERR_NOMEM
    stc
    jmp .out
.noresource:
    mov byte [bas_error_code], BERR_NORES
.fail:
    stc
    jmp .out
.release:
    ; Preserve error flag while freeing the transient claim.
    pushf
    mov dx, [bas_res_stage]
    call OSAPI_MEM_FREE
    popf
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; A user-facing name parser (upper-case, 1..12 characters).
; The resource list has fixed 12-byte names, so no transient strings/heap.
bas_parse_res_name:
    push ax
    push bx
    push di
    call bas_skip_spaces
    cmp byte [si], '"'
    jne .syntax
    inc si
    mov di, bas_res_name
    mov bx, 12
.copy:
    mov al, [si]
    or al, al
    jz .syntax
    cmp al, '"'
    je .close
    test bx, bx
    jz .syntax
    call bas_upper_al
    mov [di], al
    inc si
    inc di
    dec bx
    jmp .copy
.close:
    cmp di, bas_res_name
    je .syntax
    inc si
    mov byte [di], 0
    clc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
.out:
    pop di
    pop bx
    pop ax
    ret

; RESSELECT "BANK" -- changes the bank used by numeric PUTRES.
bas_exec_resselect:
    push ax
    push bx
    push di
    push si                  ; preserve BASIC statement cursor across lookup
    call bas_parse_res_name
    BAS_JC .fail
    call bas_expect_end
    BAS_JC .fail
    mov si, bas_res_name
    call bas_res_find
    BAS_JC .noresource
    cmp byte [di+12], 3
    BAS_JNZ .noresource
    cmp word [di+14], 16
    BAS_JNZ .noresource
    cmp word [di+16], 16
    BAS_JNZ .noresource
    mov ax, [di+22]
    cmp ax, 99
    BAS_JA .noresource
    mov [bas_res_count], ax
    mov ax, [di+18]
    mov [bas_res_offset], ax
    clc
    jmp .out
.noresource:
    mov byte [bas_error_code], BERR_NORES
.fail:
    stc
.out:
    pop si
    pop di
    pop bx
    pop ax
    ret

; RESLEN("NAME"), RESCOUNT("BANK"), RESBYTE("NAME",index),
; RESWORD("NAME",index) share one bounds-checked numeric handler.
; Input AL=1 length / 2 count / 3 byte / 4 signed word.
bas_expr_res:
    push bx
    push cx
    push dx
    push di
    mov [bas_res_op], al       ; caller's function selector
    call bas_skip_spaces
    cmp byte [si], '('
    jne .syntax
    inc si
    call bas_parse_res_name
    BAS_JC .fail
    push si
    mov si, bas_res_name
    call bas_res_find
    pop si
    BAS_JC .noresource
    mov ax, [di+18]
    mov [bas_res_item_off], ax
    mov ax, [di+20]
    mov [bas_res_item_len], ax
    mov ax, [di+22]
    mov [bas_res_item_cnt], ax
    mov al, [di+12]
    mov [bas_res_item_typ], al
    mov al, [bas_res_op]
    cmp al, 1
    je .length
    cmp al, 2
    je .count
    call bas_parse_comma
    BAS_JC .fail
    ; Nested RESLEN/RESBYTE calls may overwrite the shared lookup scratch.
    ; Bank this entry's metadata and function selector across the parser.
    mov ax, [bas_res_item_off]
    push ax
    mov ax, [bas_res_item_len]
    push ax
    mov ax, [bas_res_item_cnt]
    push ax
    mov al, [bas_res_item_typ]
    xor ah, ah
    push ax
    mov al, [bas_res_op]
    xor ah, ah
    push ax
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    mov bx, ax
    pop ax
    mov [bas_res_op], al
    pop ax
    mov [bas_res_item_typ], al
    pop ax
    mov [bas_res_item_cnt], ax
    pop ax
    mov [bas_res_item_len], ax
    pop ax
    mov [bas_res_item_off], ax
    mov ax, bx
    BAS_JC .fail
    test ax, ax
    js .illegal
    cmp byte [bas_res_op], 4
    je .word
    ; RESBYTE(name,index): unsigned 0..255 from any resource payload.
    cmp ax, [bas_res_item_len]
    jae .illegal
    add ax, [bas_res_item_off]
    mov bx, ax
    xor ax, ax
    mov al, [bas_image_data+bx]
    jmp .finish
.word:
    ; RESWORD(name,index): signed word from a WORDS16 resource.
    cmp byte [bas_res_item_typ], 6
    jne .illegal
    cmp ax, [bas_res_item_cnt]
    jae .illegal
    shl ax, 1
    add ax, [bas_res_item_off]
    mov bx, ax
    mov ax, [bas_image_data+bx]
    jmp .finish
.length:
    mov ax, [bas_res_item_len]
    jmp .finish
.count:
    cmp byte [bas_res_item_typ], 3
    je .has_count
    cmp byte [bas_res_item_typ], 6
    je .has_count
    xor ax, ax
    jmp .finish
.has_count:
    mov ax, [bas_res_item_cnt]
.finish:
    push ax
    call bas_skip_spaces
    cmp byte [si], ')'
    jne .syntax_pop
    inc si
    pop ax
    clc
    jmp .out
.syntax_pop:
    pop ax
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
    jmp .out
.noresource:
    mov byte [bas_error_code], BERR_NORES
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

bas_exec_putres:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [bas_screen_mode], 1
    BAS_JNZ .screen_error
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    BAS_JC .fail
    test ax, ax
    BAS_JS .illegal
    cmp ax, BAS_RES_COUNT - 1
    BAS_JA .illegal
    mov [bas_draw_sx], ax             ; resource id
    call bas_parse_comma
    BAS_JC .fail
    call bas_parse_coord
    BAS_JC .fail
    cmp ax, 2047
    BAS_JA .illegal
    mov [bas_draw_x0], ax
    call bas_parse_comma
    BAS_JC .fail
    call bas_parse_coord
    BAS_JC .fail
    cmp ax, 2047
    BAS_JA .illegal
    mov [bas_draw_y0], ax

    ; Optional foreground and quarter-turn orientation. Legacy three-argument
    ; PUTRES stays black, unrotated. Orientation is 0..3 = 0/90/180/270 CW.
    mov word [bas_draw_dx], 0
    mov word [bas_draw_dy], 0
    call bas_skip_spaces
    cmp byte [si], 0
    je .args_done
    call bas_parse_comma
    BAS_JC .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    BAS_JC .fail
    test ax, ax
    BAS_JS .illegal
    cmp ax, 15
    BAS_JA .illegal
    mov [bas_draw_dx], ax
    call bas_skip_spaces
    cmp byte [si], 0
    je .args_done
    call bas_parse_comma
    BAS_JC .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    BAS_JC .fail
    test ax, ax
    BAS_JS .illegal
    cmp ax, 3
    BAS_JA .illegal
    mov [bas_draw_dy], ax
.args_done:
    call bas_expect_end
    BAS_JC .fail
    call bas_resource_ready
    BAS_JC .noresource
    mov ax, [bas_draw_sx]
    cmp ax, [bas_res_count]
    BAS_JAE .illegal

    mov ax, [bas_gfx_count]
    cmp ax, BAS_GFX_MAX
    BAS_JAE .full
    mov bx, BAS_GFX_REC_SIZE
    mul bx
    mov di, bas_gfx_list
    add di, ax
    mov byte [di], GFX_RESOURCE
    mov ax, [bas_draw_sx]
    mov [di + 1], al
    mov ax, [bas_draw_x0]
    mov [di + 2], ax
    mov ax, [bas_draw_y0]
    mov [di + 4], ax
    mov ax, [bas_draw_dx]
    mov [di + 6], al
    mov ax, [bas_draw_dy]
    mov [di + 7], al
    mov word [di + 8], 0
    call bas_gfx_note_delta
    inc word [bas_gfx_count]
    mov byte [bas_dirty], 1
    clc
    jmp .out
.screen_error:
    mov byte [bas_error_code], BERR_SCREEN
    stc
    jmp .out
.noresource:
    mov byte [bas_error_code], BERR_NORES
    stc
    jmp .out
.full:
    mov byte [bas_error_code], BERR_GFXFULL
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; 2BASIC single-form GUI and modal dialogs
; =============================================================================

; Parse a small string expression into DS:DI. CX includes the terminating NUL.
; Supported terms are quoted literals and STR$(integer-expression), joined by +.
; String variables are deliberately not introduced: this is formatting/conversion
; machinery for PRINT, dialogs and control captions.
bas_parse_strexpr:
    push ax
    push bx
    push dx
    push bp
    mov bp, cx
    test bp, bp
    BAS_JZ .overflow
    dec bp
.term:
    call bas_skip_spaces
    cmp byte [si], '"'
    BAS_JZ .quoted
    push si
    push di
    mov di, bas_kw_resline
    call bas_match_word
    pop di
    pop bx
    BAS_JNC .resline
    mov si, bx
    push di
    mov di, bas_kw_strs
    call bas_match_word
    pop di
    BAS_JC .syntax
    call bas_skip_spaces
    cmp byte [si], '('
    BAS_JNZ .syntax
    inc si
    mov word [bas_expr_depth], 0
    ; Nested expressions use DI for keyword matching. Save our output
    ; cursor or STR$ writes digits at the wrong DS offset.
    push di
    call bas_parse_expr
    pop di                       ; POP retains CF from parser
    BAS_JC .fail
    push ax
    call bas_skip_spaces
    cmp byte [si], ')'
    BAS_JNZ .str_pop_syntax
    inc si
    pop ax
    ; Any signed 16-bit integer needs at most six bytes including '-'.
    cmp bp, 6
    BAS_JB .overflow
    push di
    call bas_int_to_dec
    mov ax, di
    pop bx
    sub ax, bx
    sub bp, ax
    jmp .after_term
.resline:
    ; RESLINE$("NAME",n) appends one bounded CP437 text line.
    push di                     ; preserve string output pointer
    call bas_skip_spaces
    cmp byte [si], '('
    BAS_JNZ .resline_name_syntax
    inc si
    call bas_parse_res_name
    BAS_JC .resline_name_fail
    push si
    mov si, bas_res_name
    call bas_res_find
    pop si
    BAS_JC .resline_missing
    cmp byte [di+12], 4
    BAS_JNZ .resline_missing
    mov bx, di                  ; entry pointer
    pop di                      ; restore output pointer
    push bx                     ; retain entry across nested integer parser
    call bas_parse_comma
    BAS_JC .resline_fail
    push di
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    pop di
    BAS_JC .resline_fail
    test ax, ax
    BAS_JS .resline_range
    push ax
    call bas_skip_spaces
    cmp byte [si], ')'
    BAS_JNZ .resline_syntax_pop
    inc si
    pop ax
    pop bx
    call bas_copy_resline
    BAS_JC .fail
    jmp .after_term
.resline_syntax_pop:
    pop ax
    jmp .resline_syntax
.resline_range:
    mov byte [bas_error_code], BERR_ILLEGAL
    jmp .resline_fail
.resline_syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.resline_fail:
    pop bx
    stc
    jmp .fail
.resline_missing:
    mov byte [bas_error_code], BERR_NORES
    jmp .resline_name_fail
.resline_name_syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.resline_name_fail:
    pop di
    stc
    jmp .fail

.str_pop_syntax:
    pop ax
    jmp .syntax
.quoted:
    inc si
.qcopy:
    mov al, [si]
    test al, al
    BAS_JZ .syntax
    cmp al, '"'
    BAS_JZ .qdone
    test bp, bp
    BAS_JZ .overflow
    mov [di], al
    inc di
    inc si
    dec bp
    jmp .qcopy
.qdone:
    inc si
.after_term:
    call bas_skip_spaces
    cmp byte [si], '+'
    BAS_JNZ .done
    inc si
    jmp .term
.done:
    mov byte [di], 0
    clc
    jmp .out
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop bp
    pop dx
    pop bx
    pop ax
    ret

; AX = 0-based line, BX = TEXT index entry, DI output, BP bytes available.
; Consumes neither the main BASIC source pointer SI nor any interpreter heap.
bas_copy_resline:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax                 ; requested line
    mov cx, [bx+20]           ; remaining source bytes
    mov ax, [bx+18]
    mov bx, bas_image_data
    add bx, ax
.find:
    test dx, dx
    jz .copy
    test cx, cx
    jz .bad
    mov al, [bx]
    inc bx
    dec cx
    cmp al, 10
    jne .find
    dec dx
    jmp .find
.copy:
    test cx, cx
    jz .good
    mov al, [bx]
    cmp al, 10
    je .good
    cmp al, 13
    je .good
    test bp, bp
    jz .overflow
    mov [di], al
    inc di
    inc bx
    dec bp
    dec cx
    jmp .copy
.good:
    clc
    jmp .out
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    jmp .out
.bad:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; VAL(string-expression) numeric function. Leading/trailing spaces and one sign
; are accepted; the remaining text must be decimal digits fitting signed 16-bit.
bas_expr_val:
    push bx
    push di
    call bas_skip_spaces
    cmp byte [si], '('
    BAS_JNZ .syntax
    inc si
    mov di, bas_val_text
    mov cx, BAS_VAL_TEXT_CAP
    call bas_parse_strexpr
    BAS_JC .fail
    call bas_skip_spaces
    cmp byte [si], ')'
    BAS_JNZ .syntax
    inc si
    push si
    mov si, bas_val_text
    call bas_parse_val_text
    pop si
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
.out:
    pop di
    pop bx
    ret

bas_parse_val_text:
    push bx
    xor bx, bx
    call bas_skip_spaces
    cmp byte [si], '-'
    BAS_JNZ .plus
    mov bl, 1
    inc si
    jmp .digits
.plus:
    cmp byte [si], '+'
    BAS_JNZ .digits
    inc si
.digits:
    call bas_parse_uword
    BAS_JC .syntax
    test bl, bl
    BAS_JNZ .negative
    cmp ax, 32767
    BAS_JA .overflow
    jmp .tail
.negative:
    cmp ax, 32768
    BAS_JA .overflow
    neg ax
.tail:
    call bas_skip_spaces
    cmp byte [si], 0
    BAS_JNZ .syntax
    clc
    jmp .out
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
.out:
    pop bx
    ret

; Parse a quoted string into DS:DI. CX includes the terminating NUL.
; SI advances past the closing quote. CF=1 on malformed/too-long text.
bas_parse_qstring:
    push ax
    push bx
    call bas_skip_spaces
    cmp byte [si], '"'
    BAS_JNZ .fail
    inc si
    mov bx, cx
    dec bx
.copy:
    mov al, [si]
    test al, al
    BAS_JZ .fail
    cmp al, '"'
    BAS_JZ .done
    test bx, bx
    BAS_JZ .overflow
    mov [di], al
    inc di
    inc si
    dec bx
    jmp .copy
.done:
    mov byte [di], 0
    inc si
    clc
    jmp .out
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
    stc
    jmp .out
.fail:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; 2BASIC.DAT application-variable store.
; One fixed 580-byte file carries 32 signed integer values. Names are literal,
; case-insensitive 1..8 character APP/KEY tokens. LVAR returns 0 when absent.
; The store is loaded once on application startup. SVAR writes the whole small
; image atomically through the os8088 whole-file API. During RUN the interpreter
; worker takes the graphics mutex around the file slot: legitimate UI-task disk
; callers already execute under that mutex, recreating the serialization that the
; kernel file ABI otherwise obtains from its single UI-task calling convention.
; -----------------------------------------------------------------------------
bas_varstore_reset:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, bas_var_data
    mov cx, BAS_VAR_BYTES
    xor ax, ax
    rep stosb
    mov byte [bas_var_data], '2'
    mov byte [bas_var_data+1], 'B'
    mov byte [bas_var_data+2], 'D'
    mov byte [bas_var_data+3], '1'
    mov byte [bas_var_bad], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

bas_varstore_load:
    push ax
    push bx
    push cx
    push si
    push es
    call bas_varstore_reset
    push ds
    pop es
    mov bx, bas_var_data
    mov cx, BAS_VAR_BYTES
    mov si, bas_s_var_file
    xor dx, dx
    call OSAPI_FILE_READ
    BAS_JC .read_error
    cmp ax, BAS_VAR_BYTES
    BAS_JNZ .bad
    cmp byte [bas_var_data], '2'
    BAS_JNZ .bad
    cmp byte [bas_var_data+1], 'B'
    BAS_JNZ .bad
    cmp byte [bas_var_data+2], 'D'
    BAS_JNZ .bad
    cmp byte [bas_var_data+3], '1'
    BAS_JNZ .bad
    mov byte [bas_var_bad], 0
    jmp .out
.read_error:
    cmp ax, FERR_NOENT
    BAS_JZ .out                    ; first run: empty store is normal
.bad:
    call bas_varstore_reset
    mov byte [bas_var_bad], 1
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; Parse one quoted APP/KEY token into exactly eight zero-padded bytes at DI.
bas_parse_var_name:
    push ax
    push bx
    push cx
    call bas_skip_spaces
    cmp byte [si], '"'
    BAS_JNZ .syntax
    inc si
    mov cx, BAS_VAR_NAME_CAP
    xor bx, bx
.copy:
    mov al, [si]
    test al, al
    BAS_JZ .syntax
    cmp al, '"'
    BAS_JZ .close
    test cx, cx
    BAS_JZ .syntax
    call bas_upper_al
    cmp al, 'A'
    BAS_JB .digit
    cmp al, 'Z'
    BAS_JBE .put
.digit:
    cmp al, '0'
    BAS_JB .under
    cmp al, '9'
    BAS_JBE .put
.under:
    cmp al, '_'
    BAS_JNZ .syntax
.put:
    mov [di], al
    inc di
    inc si
    inc bx
    dec cx
    jmp .copy
.close:
    test bx, bx
    BAS_JZ .syntax
    inc si
.zero:
    jcxz .ok
    mov byte [di], 0
    inc di
    dec cx
    jmp .zero
.ok:
    clc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; Compare current DI record with bas_filename APP and bas_val_text KEY.
bas_var_record_match:
    push ax
    push bx
    push si
    push di
    mov si, bas_filename
    mov bx, BAS_VAR_NAME_CAP
.app:
    mov al, [si]
    cmp al, [di]
    BAS_JNZ .miss
    inc si
    inc di
    dec bx
    jnz .app
    mov si, bas_val_text
    mov bx, BAS_VAR_NAME_CAP
.key:
    mov al, [si]
    cmp al, [di]
    BAS_JNZ .miss
    inc si
    inc di
    dec bx
    jnz .key
    clc
    jmp .out
.miss:
    stc
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; out CF=0 found DI=record; CF=1 absent DI=first free record or 0 when full.
bas_var_find:
    push ax
    push cx
    push dx
    mov di, bas_var_data + BAS_VAR_HEADER
    xor dx, dx
    mov cx, BAS_VAR_SLOTS
.scan:
    cmp byte [di], 0
    BAS_JNZ .compare
    test dx, dx
    BAS_JNZ .next
    mov dx, di
    jmp .next
.compare:
    call bas_var_record_match
    BAS_JNC .found
.next:
    add di, BAS_VAR_REC_SIZE
    loop .scan
    mov di, dx
    stc
    jmp .out
.found:
    clc
.out:
    pop dx
    pop cx
    pop ax
    ret

bas_varstore_write:
    push ax
    push bx
    push cx
    push si
    push es
    cmp byte [bas_var_bad], 0
    BAS_JNZ .bad_store
    cmp byte [bas_worker_active], 0
    BAS_JZ .ui_write
    ; No FAT calls from the worker (SDK SPEC 18.4 / 74.1). Mutate the
    ; in-memory table immediately, and request a UI-task commit via EVT_WAKE.
    mov byte [bas_var_dirty], 1
    mov bx, [bas_window]
    call OSAPI_WM_WAKE
    clc                             ; queued or retried on next redraw
    jmp .out
.ui_write:
    push ds
    pop es
    mov bx, bas_var_data
    mov cx, BAS_VAR_BYTES
    mov si, bas_s_var_file
    xor dx, dx
    call OSAPI_FILE_WRITE
    BAS_JNC .ok
    call bas_set_file_error
    stc
    jmp .out
.ok:
    mov byte [bas_var_dirty], 0
    clc
    jmp .out
.bad_store:
    mov byte [bas_error_code], BERR_VARSTORE
    stc
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; SVAR "APP","KEY",integer-expression
bas_exec_svar:
    push ax
    push bx
    push cx
    push di
    cmp byte [bas_var_bad], 0
    BAS_JNZ .bad_store
    mov di, bas_filename
    call bas_parse_var_name
    BAS_JC .fail
    call bas_parse_comma
    BAS_JC .fail
    mov di, bas_val_text
    call bas_parse_var_name
    BAS_JC .fail
    call bas_parse_comma
    BAS_JC .fail
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    BAS_JC .fail
    mov [bas_draw_x0], ax
    call bas_expect_end
    BAS_JC .fail
    call bas_var_find
    BAS_JNC .have
    test di, di
    BAS_JZ .full
    push di
    push es
    push ds
    pop es
    mov si, bas_filename
    mov cx, BAS_VAR_NAME_CAP
    rep movsb
    mov si, bas_val_text
    mov cx, BAS_VAR_NAME_CAP
    rep movsb
    pop es
    pop di
.have:
    mov ax, [bas_draw_x0]
    mov [di+16], ax
    call bas_varstore_write
    jmp .out
.full:
    mov byte [bas_error_code], BERR_VARFULL
    stc
    jmp .out
.bad_store:
    mov byte [bas_error_code], BERR_VARSTORE
.fail:
    stc
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; LVAR "APP","KEY",variable -- missing entries load as zero.
bas_exec_lvar:
    push ax
    push bx
    push cx
    push di
    cmp byte [bas_var_bad], 0
    BAS_JNZ .bad_store
    mov di, bas_filename
    call bas_parse_var_name
    BAS_JC .fail
    call bas_parse_comma
    BAS_JC .fail
    mov di, bas_val_text
    call bas_parse_var_name
    BAS_JC .fail
    call bas_parse_comma
    BAS_JC .fail
    call bas_parse_variable
    BAS_JC .fail
    mov [bas_draw_x0], bx
    call bas_expect_end
    BAS_JC .fail
    call bas_var_find
    mov ax, 0
    BAS_JC .store
    mov ax, [di+16]
.store:
    mov bx, [bas_draw_x0]
    mov [bas_vars+bx], ax
    clc
    jmp .out
.bad_store:
    mov byte [bas_error_code], BERR_VARSTORE
.fail:
    stc
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; BX=index -> DI=control record.
bas_gui_rec_ptr:
    mov di, bx
    shl di, 1
    shl di, 1
    shl di, 1
    shl di, 1
    add di, bas_form_recs
    ret

; BX=index -> DI=control caption slot.
bas_gui_text_ptr:
    push ax
    mov ax, bx
    shl ax, 1
    shl ax, 1
    shl ax, 1                  ; *8
    mov di, ax
    shl ax, 1                  ; *16
    add di, ax                 ; *24
    add di, bas_form_text
    pop ax
    ret

; AX=id -> BX=index, DI=record, CF=0 found.
bas_gui_find_id:
    push cx
    xor bx, bx
    mov cx, [bas_form_count]
.loop:
    jcxz .miss
    call bas_gui_rec_ptr
    cmp ax, [di+GUI_ID]
    BAS_JZ .found
    inc bx
    loop .loop
.miss:
    stc
    pop cx
    ret
.found:
    clc
    pop cx
    ret

; AX=id -> BX=index, DI=record. Existing IDs are replaced; new IDs append.
bas_gui_alloc:
    test ax, ax
    BAS_JZ .bad
    call bas_gui_find_id
    BAS_JNC .clear
    mov bx, [bas_form_count]
    cmp bx, BAS_GUI_MAX
    BAS_JAE .full
    inc word [bas_form_count]
    call bas_gui_rec_ptr
.clear:
    push ax
    push cx
    push di
    xor ax, ax
    mov cx, BAS_GUI_REC_SIZE/2
    rep stosw
    pop di
    pop cx
    pop ax
    mov [di+GUI_ID], ax
    mov byte [di+GUI_FLAGS], GUI_F_VISIBLE | GUI_F_ENABLED
    clc
    ret
.bad:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
    ret
.full:
    mov byte [bas_error_code], BERR_FORMFULL
    stc
    ret

; Parse expression into AX and reject negative values.
bas_gui_parse_nonneg:
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    BAS_JC .out
    test ax, ax
    BAS_JS .bad
    clc
.out:
    ret
.bad:
    mov byte [bas_error_code], BERR_ILLEGAL
    stc
    ret

; Common id,x,y prefix. Outputs id/x/y in draw_x0/draw_y0/draw_x1.
bas_gui_parse_id_xy:
    call bas_gui_parse_nonneg
    BAS_JC .fail
    test ax, ax
    BAS_JZ .illegal
    mov [bas_draw_x0], ax
    call bas_parse_comma
    BAS_JC .fail
    call bas_gui_parse_nonneg
    BAS_JC .fail
    mov [bas_draw_y0], ax
    call bas_parse_comma
    BAS_JC .fail
    call bas_gui_parse_nonneg
    BAS_JC .fail
    mov [bas_draw_x1], ax
    clc
    ret
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
    ret

; Repaint the form from a worker statement.
bas_gui_repaint_form:
    push ax
    push bx
    push si
    cmp byte [bas_form_visible], 0
    BAS_JZ .out
    mov bx, [bas_form_window]
    test bx, bx
    BAS_JZ .out
    call OSAPI_GFX_LOCK
    call OSAPI_WM_GEOM
    BAS_JC .unlock
    call OSAPI_WM_CLIP_SET
    BAS_JC .unlock
    mov si, [bas_form_window]
    call bas_form_paint
.unlock:
    call OSAPI_GFX_UNLOCK
.out:
    pop si
    pop bx
    pop ax
    ret

; FORM "title"[,client-width,client-height[,YES|NO]] -- reset the one
; custom form and its controls. Explicit dimensions are rejected outside the
; safe live-desktop range; omitted dimensions use the clamped default.
bas_exec_form:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, bas_form_title
    mov cx, BAS_FORM_TITLE_CAP
    call bas_parse_strexpr
    BAS_JC .fail

    ; Defaults preserve the original 360x240 frame while making resizing opt-in.
    mov word [bas_draw_x0], BAS_FORM_DEF_W
    mov word [bas_draw_y0], BAS_FORM_DEF_H
    mov byte [bas_form_resize], 0
    call bas_skip_spaces
    cmp byte [si], 0
    BAS_JZ .clamp_default
    cmp byte [si], 27h
    BAS_JZ .clamp_default
    cmp byte [si], ','
    BAS_JNZ .syntax
    inc si
    call bas_gui_parse_nonneg
    BAS_JC .fail
    cmp ax, BAS_FORM_MIN_W
    BAS_JB .illegal
    mov [bas_draw_x0], ax
    call bas_parse_comma
    BAS_JC .fail
    call bas_gui_parse_nonneg
    BAS_JC .fail
    cmp ax, BAS_FORM_MIN_H
    BAS_JB .illegal
    mov [bas_draw_y0], ax

    ; Explicit sizes must fit this adapter's usable desktop before frame chrome.
    call OSAPI_VIDEO             ; AX=screen width, CX=dock top
    sub ax, BAS_FORM_FRAME_X
    cmp [bas_draw_x0], ax
    BAS_JA .illegal
    mov ax, cx
    sub ax, MBAR_H + BAS_FORM_FRAME_Y
    cmp [bas_draw_y0], ax
    BAS_JA .illegal

    call bas_skip_spaces
    cmp byte [si], 0
    BAS_JZ .apply
    cmp byte [si], 27h
    BAS_JZ .apply
    cmp byte [si], ','
    BAS_JNZ .syntax
    inc si
    call bas_skip_spaces
    push si
    mov di, bas_kw_yes
    call bas_match_word
    pop dx
    BAS_JNC .resize_yes
    mov si, dx
    mov di, bas_kw_no
    call bas_match_word
    BAS_JC .syntax
    mov byte [bas_form_resize], 0
    jmp .option_end
.resize_yes:
    mov byte [bas_form_resize], 1
.option_end:
    call bas_expect_end
    BAS_JC .fail
    jmp .apply

.clamp_default:
    ; Omitted dimensions are defaults, clamped rather than rejected on CGA/HGC.
    call OSAPI_VIDEO
    sub ax, BAS_FORM_FRAME_X
    cmp word [bas_draw_x0], ax
    BAS_JBE .default_w_ok
    mov [bas_draw_x0], ax
.default_w_ok:
    mov ax, cx
    sub ax, MBAR_H + BAS_FORM_FRAME_Y
    cmp word [bas_draw_y0], ax
    BAS_JBE .default_h_ok
    mov [bas_draw_y0], ax
.default_h_ok:
    call bas_expect_end
    BAS_JC .fail

.apply:
    mov ax, [bas_draw_x0]
    mov [bas_form_req_w], ax
    mov ax, [bas_draw_y0]
    mov [bas_form_req_h], ax
    mov word [bas_form_count], 0
    mov word [bas_form_event], 0
    mov byte [bas_form_visible], 0
    mov bx, [bas_form_window]
    test bx, bx
    BAS_JZ .noform
    call OSAPI_GFX_LOCK
    call OSAPI_WM_HIDE
    mov cx, [bas_form_req_w]
    add cx, BAS_FORM_FRAME_X
    mov dx, [bas_form_req_h]
    add dx, BAS_FORM_FRAME_Y
    call OSAPI_WM_RESIZE
    BAS_JC .unlock_noform
    push ax
    mov al, [bas_form_resize]
    mov ah, WFB_SIZABLE
    call OSAPI_WM_FLAG
    pop ax
    call OSAPI_GFX_UNLOCK
    clc
    jmp .out
.unlock_noform:
    call OSAPI_GFX_UNLOCK
.noform:
    mov byte [bas_error_code], BERR_NOFORM
    stc
    jmp .out
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AL=control type. Parse and create LABEL/BUTTON/CHECKBOX/FRAME/IMAGE.
bas_exec_add_control:
    push ax
    push bx
    push cx
    push dx
    push di
    xor ah, ah
    mov [bas_cap_tmp], ax
    call bas_gui_parse_id_xy
    BAS_JC .fail ; IMAGE uses id,slot,x,y: the common parser read id,slot,x.
    cmp byte [bas_cap_tmp], GUI_IMAGE
    BAS_JNZ .normal_xy
    mov ax, [bas_draw_y0]       ; slot
    cmp ax, BAS_IMAGE_SLOTS-1
    BAS_JA .illegal
    mov [bas_draw_y1], ax
    mov ax, [bas_draw_x1]       ; x
    mov [bas_draw_y0], ax
    call bas_parse_comma
    BAS_JC .fail
    call bas_gui_parse_nonneg   ; y
    BAS_JC .fail
    mov [bas_draw_x1], ax
    call bas_expect_end
    BAS_JC .fail
    mov ax, [bas_draw_x0]
    call bas_gui_alloc
    BAS_JC .fail
    mov byte [di+GUI_TYPE], GUI_IMAGE
    mov ax, [bas_draw_y0]
    mov [di+GUI_X], ax
    mov ax, [bas_draw_x1]
    mov [di+GUI_Y], ax
    mov word [di+GUI_W], BAS_IMAGE_MAX_W
    mov word [di+GUI_H], BAS_IMAGE_MAX_H
    mov ax, [bas_draw_y1]
    mov [di+GUI_VALUE], ax
    jmp .made

.normal_xy:
    ; Common parser produced id,x,y.
    cmp byte [bas_cap_tmp], GUI_LABEL
    BAS_JZ .text_only
    cmp byte [bas_cap_tmp], GUI_CHECKBOX
    BAS_JZ .text_only ; BUTTON/FRAME add width,height.
    call bas_parse_comma
    BAS_JC .fail
    call bas_gui_parse_nonneg
    BAS_JC .fail
    cmp ax, 4
    BAS_JB .illegal
    mov [bas_draw_y1], ax       ; width
    call bas_parse_comma
    BAS_JC .fail
    call bas_gui_parse_nonneg
    BAS_JC .fail
    cmp ax, 4
    BAS_JB .illegal
    mov [bas_draw_dx], ax       ; height
    call bas_parse_comma
    BAS_JC .fail
    jmp .caption

.text_only:
    call bas_parse_comma
    BAS_JC .fail
    mov word [bas_draw_y1], 160
    mov word [bas_draw_dx], 12
.caption:
    mov ax, [bas_draw_x0]
    call bas_gui_alloc
    BAS_JC .fail
    mov al, [bas_cap_tmp]
    mov byte [di+GUI_TYPE], al
    mov ax, [bas_draw_y0]
    mov [di+GUI_X], ax
    mov ax, [bas_draw_x1]
    mov [di+GUI_Y], ax
    mov ax, [bas_draw_y1]
    mov [di+GUI_W], ax
    mov ax, [bas_draw_dx]
    mov [di+GUI_H], ax
    push di
    call bas_gui_text_ptr
    mov cx, BAS_GUI_TEXT_CAP
    call bas_parse_strexpr
    pop di
    BAS_JC .fail
    call bas_expect_end
    BAS_JC .fail
.made:
    call bas_gui_repaint_form
    clc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_exec_showform:
    call bas_expect_end
    BAS_JC .fail
    mov bx, [bas_form_window]
    test bx, bx
    BAS_JZ .noform
    call OSAPI_GFX_LOCK
    call OSAPI_WM_SHOW
    call OSAPI_WM_FRONT
    call OSAPI_GFX_UNLOCK
    mov byte [bas_form_visible], 1
    call bas_gui_repaint_form
    clc
    ret
.noform:
    mov byte [bas_error_code], BERR_NOFORM
.fail:
    stc
    ret

bas_exec_closeform:
    call bas_expect_end
    BAS_JC .fail
    mov bx, [bas_form_window]
    test bx, bx
    BAS_JZ .noform
    call OSAPI_GFX_LOCK
    call OSAPI_WM_HIDE
    call OSAPI_GFX_UNLOCK
    mov byte [bas_form_visible], 0
    mov word [bas_form_event], -1
    clc
    ret
.noform:
    mov byte [bas_error_code], BERR_NOFORM
.fail:
    stc
    ret

; SETTEXT id,"caption"
bas_exec_settext:
    push ax
    push bx
    push cx
    push di
    call bas_gui_parse_nonneg
    BAS_JC .fail
    call bas_gui_find_id
    BAS_JC .badctl
    call bas_parse_comma
    BAS_JC .fail
    push bx
    call bas_gui_text_ptr
    mov cx, BAS_GUI_TEXT_CAP
    call bas_parse_strexpr
    pop bx
    BAS_JC .fail
    call bas_expect_end
    BAS_JC .fail
    call bas_gui_repaint_form
    clc
    jmp .out
.badctl:
    mov byte [bas_error_code], BERR_BADCONTROL
.fail:
    stc
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; ENABLE/VISIBLE/SETVALUE id,state. AL selects 0 enable,1 visible,2 value.
bas_exec_control_state:
    push ax
    push bx
    push dx
    push di
    xor ah, ah
    mov [bas_cap_tmp], ax
    call bas_gui_parse_nonneg
    BAS_JC .fail
    call bas_gui_find_id
    BAS_JC .badctl
    ; DI is the CONTROL RECORD. Numeric expression parsing also uses DI
    ; to probe function keywords (RND, VALUE, etc.), so do not let it
    ; overwrite the control-record destination or keyword-table bytes.
    push di
    call bas_parse_comma
    BAS_JC .parse_fail_pop
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    pop di                     ; POP preserves the expression's carry flag
    BAS_JC .fail
    cmp byte [bas_cap_tmp], 2
    BAS_JZ .value
    test ax, ax
    BAS_JZ .clearflag
    mov al, GUI_F_ENABLED
    cmp byte [bas_cap_tmp], 0
    BAS_JZ .setflag
    mov al, GUI_F_VISIBLE
.setflag:
    or [di+GUI_FLAGS], al
    jmp .done
.clearflag:
    mov al, GUI_F_ENABLED
    cmp byte [bas_cap_tmp], 0
    BAS_JZ .clear_do
    mov al, GUI_F_VISIBLE
.clear_do:
    not al
    and [di+GUI_FLAGS], al
    jmp .done
.value:
    mov [di+GUI_VALUE], ax
.done:
    call bas_expect_end
    BAS_JC .fail
    call bas_gui_repaint_form
    clc
    jmp .out
.parse_fail_pop:
    pop di
    jmp .fail
.badctl:
    mov byte [bas_error_code], BERR_BADCONTROL
.fail:
    stc
.out:
    pop di
    pop dx
    pop bx
    pop ax
    ret

; CONSOLESIZE columns,rows -- size the live console/canvas. The visible window
; height is independent of the fixed 35-row retained text buffer; only the
; current adapter usable desktop bounds rows above BAS_CON_MIN_ROWS.
bas_exec_consolesize:
    push ax
    push bx
    push cx
    push dx
    call bas_gui_parse_nonneg
    BAS_JC .fail
    cmp ax, BAS_CON_MIN_COLS
    BAS_JB .illegal
    cmp ax, BAS_CON_MAX_COLS
    BAS_JA .illegal
    mov [bas_draw_x0], ax
    call bas_parse_comma
    BAS_JC .fail
    call bas_gui_parse_nonneg
    BAS_JC .fail
    cmp ax, BAS_CON_MIN_ROWS
    BAS_JB .illegal
    mov [bas_draw_y0], ax
    call bas_expect_end
    BAS_JC .fail

    ; Reject sizes the live adapter cannot display instead of relying on an
    ; invisible kernel clamp. The dock owns rows CX..screen bottom.
    call OSAPI_VIDEO
    mov dx, [bas_draw_x0]
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, BAS_CON_FRAME_X
    cmp dx, ax
    BAS_JA .illegal
    ; Derive the row ceiling from this adapter before multiplying the request.
    ; This both decouples geometry from BAS_CON_LINES and avoids 16-bit overflow
    ; for a deliberately huge row argument.
    mov dx, cx
    sub dx, MBAR_H + BAS_CON_FRAME_Y
    shr dx, 1
    shr dx, 1
    shr dx, 1
    mov ax, [bas_draw_y0]
    cmp ax, dx
    BAS_JA .illegal

    mov cx, [bas_draw_x0]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, BAS_CON_FRAME_X
    mov dx, [bas_draw_y0]
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, BAS_CON_FRAME_Y
    mov bx, [bas_window]
    call OSAPI_GFX_LOCK
    ; WM_RESIZE is deliberately geometry-only and draws no replacement frame.
    ; Resizing a visible console therefore left its old, much wider frame on
    ; screen even though the window record had the requested dimensions. Hide
    ; first, resize, then show the same window so the manager redraws the frame
    ; at the new geometry. This is a one-time CONSOLESIZE transition, not part
    ; of the steady-state RUN repaint path.
    call OSAPI_WM_HIDE
    call OSAPI_WM_RESIZE
    pushf
    ; Always restore visibility, even if the resize itself is refused. The
    ; saved carry flag below still reports the resize result to BASIC.
    call OSAPI_WM_SHOW
    call OSAPI_WM_FRONT
    call OSAPI_GFX_UNLOCK
    popf
    BAS_JC .noform
    mov byte [bas_dirty], 1
    clc
    jmp .out
.noform:
    mov byte [bas_error_code], BERR_NOFORM
    stc
    jmp .out
.illegal:
    mov byte [bas_error_code], BERR_ILLEGAL
.fail:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_exec_minconsole:
    call bas_expect_end
    BAS_JC .fail
    mov bx, [bas_window]
    call OSAPI_GFX_LOCK
    call OSAPI_WM_MINIMIZE
    call OSAPI_GFX_UNLOCK
    mov byte [bas_console_min], 1
    clc
    ret
.fail:
    stc
    ret

bas_exec_restoreconsole:
    call bas_expect_end
    BAS_JC .fail
    call bas_gui_restore_console
    clc
    ret
.fail:
    stc
    ret

bas_gui_restore_console:
    push bx
    mov bx, [bas_window]
    test bx, bx
    BAS_JZ .out
    call OSAPI_GFX_LOCK
    call OSAPI_WM_SHOW
    call OSAPI_WM_FRONT
    call OSAPI_GFX_UNLOCK
    mov byte [bas_console_min], 0
.out:
    pop bx
    ret

; Program termination contract: console front, form/dialog hidden.
bas_gui_finish_program:
    push ax
    push bx
    push dx
    xor dx, dx                        ; did we hide any modal windows?
    cmp byte [bas_dialog_mode], DLG_NONE
    jne .have_modal
    cmp byte [bas_form_visible], 0
    je .after_modal
.have_modal:
    ; One lock, and no unnecessary HIDE of already-hidden secondary windows.
    call OSAPI_GFX_LOCK
    cmp byte [bas_dialog_mode], DLG_NONE
    je .check_form
    mov bx, [bas_dialog_window]
    test bx, bx
    jz .check_form
    call OSAPI_WM_HIDE
    inc dx
.check_form:
    cmp byte [bas_form_visible], 0
    je .unlock
    mov bx, [bas_form_window]
    test bx, bx
    jz .unlock
    call OSAPI_WM_HIDE
    inc dx
.unlock:
    call OSAPI_GFX_UNLOCK
.after_modal:
    cmp byte [bas_console_min], 0
    jne .restore
    test dx, dx
    jz .done                        ; common case: console stayed on glass
    mov bx, [bas_window]
    call OSAPI_GFX_LOCK
    call OSAPI_WM_FRONT              ; bring forward only once, no SHOW
    call OSAPI_GFX_UNLOCK
    jmp .done
.restore:
    call bas_gui_restore_console
.done:
    mov byte [bas_dialog_mode], DLG_NONE
    mov byte [bas_dialog_result], 0
    mov byte [bas_form_visible], 0
    pop dx
    pop bx
    pop ax
    ret

; EVENT numeric function. Waits without blocking the UI task.
bas_expr_event:
    cmp byte [bas_dialog_mode], DLG_NONE
    BAS_JNZ .program_only
    cmp byte [bas_worker_active], 0
    BAS_JZ .program_only
.wait:
    cmp byte [bas_break_req], 0
    BAS_JNZ .break
    mov ax, [bas_form_event]
    test ax, ax
    BAS_JNZ .take
    mov bx, [bas_form_window]
    test bx, bx
    BAS_JZ .closed
    call OSAPI_WM_GEOM
    BAS_JC .closed
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp .wait
.take:
    mov word [bas_form_event], 0
    clc
    ret
.closed:
    mov byte [bas_form_visible], 0
    call bas_gui_restore_console
    mov ax, -1
    clc
    ret
.break:
    mov byte [bas_error_code], BERR_BREAK
    stc
    ret
.program_only:
    mov byte [bas_error_code], BERR_PROGMODE
    stc
    ret

; VALUE(id) numeric function.
bas_expr_value:
    call bas_skip_spaces
    cmp byte [si], '('
    BAS_JNZ .syntax
    inc si
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    BAS_JC .fail
    push ax
    call bas_skip_spaces
    cmp byte [si], ')'
    BAS_JNZ .pop_syntax
    inc si
    pop ax
    call bas_gui_find_id
    BAS_JC .badctl
    mov ax, [di+GUI_VALUE]
    clc
    ret
.pop_syntax:
    pop ax
.syntax:
    mov byte [bas_error_code], BERR_SYNTAX
.fail:
    stc
    ret
.badctl:
    mov byte [bas_error_code], BERR_BADCONTROL
    stc
    ret

; MESSAGEBOX "text"[,expression]
; Optional integer expression is appended to the quoted text. This keeps the
; dialog primitive tiny while allowing programs such as 2B99 to report a final
; score/time without introducing string variables or concatenation machinery.
bas_exec_messagebox:
    push cx
    push di
    mov di, bas_dialog_text
    mov cx, BAS_DIALOG_TEXT_CAP
    call bas_parse_strexpr
    BAS_JC .fail
    call bas_skip_spaces
    cmp byte [si], 0
    je .show
    call bas_parse_comma
    BAS_JC .fail
    push di
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    pop di
    BAS_JC .fail
    cmp di, bas_dialog_text + BAS_DIALOG_TEXT_CAP - 7
    jae .overflow
    call bas_int_to_dec
    mov byte [di], 0
    call bas_expect_end
    BAS_JC .fail
.show:
    mov byte [bas_dialog_mode], DLG_MESSAGE
    mov byte [bas_dialog_result], 0
    call bas_gui_wait_dialog
    jmp .out
.overflow:
    mov byte [bas_error_code], BERR_OVERFLOW
.fail:
    stc
.out:
    pop di
    pop cx
    ret

; INPUTBOX "prompt",A -- integer expression entry.
bas_exec_inputbox:
    push bx
    push cx
    push di
    mov di, bas_dialog_text
    mov cx, BAS_DIALOG_TEXT_CAP
    call bas_parse_strexpr
    BAS_JC .fail
    call bas_parse_comma
    BAS_JC .fail
    call bas_parse_variable
    BAS_JC .fail
    mov [bas_dialog_var], bx
    call bas_expect_end
    BAS_JC .fail
    mov word [bas_dialog_input_len], 0
    mov byte [bas_dialog_input], 0
    mov byte [bas_dialog_mode], DLG_INPUT
    mov byte [bas_dialog_result], 0
    call bas_gui_wait_dialog
    jmp .out
.fail:
    stc
.out:
    pop di
    pop cx
    pop bx
    ret

; Show dialog and park worker until OK/Cancel/close/break.
bas_gui_wait_dialog:
    push ax
    push bx
    mov bx, [bas_dialog_window]
    test bx, bx
    BAS_JZ .noform
    call OSAPI_GFX_LOCK
    call OSAPI_WM_SHOW
    call OSAPI_WM_FRONT
    call OSAPI_GFX_UNLOCK
.wait:
    cmp byte [bas_break_req], 0
    BAS_JNZ .break
    cmp byte [bas_dialog_result], 0
    BAS_JNZ .done
    mov bx, [bas_dialog_window]
    call OSAPI_WM_GEOM
    BAS_JC .cancel
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp .wait
.cancel:
    mov byte [bas_dialog_result], 2
.done:
    mov byte [bas_dialog_mode], DLG_NONE
    clc
    jmp .out
.break:
    call OSAPI_GFX_LOCK
    mov bx, [bas_dialog_window]
    call OSAPI_WM_HIDE
    call OSAPI_GFX_UNLOCK
    mov byte [bas_dialog_mode], DLG_NONE
    mov byte [bas_error_code], BERR_BREAK
    stc
    jmp .out
.noform:
    mov byte [bas_error_code], BERR_NOFORM
    stc
.out:
    pop bx
    pop ax
    ret

; Accept current INPUTBOX text. Called by dialog UI callback under gfx lock.
bas_dialog_accept:
    push ax
    push bx
    push si
    mov si, bas_dialog_input
    mov word [bas_expr_depth], 0
    call bas_parse_expr
    BAS_JC .bad
    push ax
    call bas_expect_end
    pop ax
    BAS_JC .bad
    mov bx, [bas_dialog_var]
    mov [bas_vars+bx], ax
    mov byte [bas_dialog_result], 1
    mov bx, [bas_dialog_window]
    call OSAPI_WM_HIDE
    clc
    jmp .out
.bad:
    mov byte [bas_error_code], BERR_NONE
    mov ax, 880
    mov cx, 2
    mov dl, 20h
    call OSAPI_SND_TONE
    stc
.out:
    pop si
    pop bx
    pop ax
    ret

; ----------------------------------------------------------------------------
; Form window callbacks
; ----------------------------------------------------------------------------
bas_form_paint:
    push es
    push ds
    pop es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [bas_form_origin_x], ax
    mov [bas_form_origin_y], dx
    call OSAPI_WM_GEOM
    BAS_JC .out

    ; The kernel supplies the grow box; 2BASIC enforces its own safe client
    ; minimum and the live desktop maximum whenever resizing is enabled.
    cmp byte [bas_form_resize], 0
    BAS_JZ .geometry_ok
    mov [bas_draw_x1], cx
    mov [bas_draw_y1], dx
    mov ax, cx
    cmp ax, BAS_FORM_MIN_W
    BAS_JAE .live_w_min_ok
    mov ax, BAS_FORM_MIN_W
.live_w_min_ok:
    mov [bas_draw_x0], ax
    mov ax, dx
    cmp ax, BAS_FORM_MIN_H
    BAS_JAE .live_h_min_ok
    mov ax, BAS_FORM_MIN_H
.live_h_min_ok:
    mov [bas_draw_y0], ax
    call OSAPI_VIDEO
    sub ax, BAS_FORM_FRAME_X
    cmp word [bas_draw_x0], ax
    BAS_JBE .live_w_max_ok
    mov [bas_draw_x0], ax
.live_w_max_ok:
    mov ax, cx
    sub ax, MBAR_H + BAS_FORM_FRAME_Y
    cmp word [bas_draw_y0], ax
    BAS_JBE .live_h_max_ok
    mov [bas_draw_y0], ax
.live_h_max_ok:
    mov ax, [bas_draw_x0]
    cmp ax, [bas_draw_x1]
    BAS_JNZ .fix_live_size
    mov ax, [bas_draw_y0]
    cmp ax, [bas_draw_y1]
    BAS_JZ .reload_geometry
.fix_live_size:
    mov cx, [bas_draw_x0]
    add cx, BAS_FORM_FRAME_X
    mov dx, [bas_draw_y0]
    add dx, BAS_FORM_FRAME_Y
    mov bx, [bas_form_window]
    call OSAPI_WM_RESIZE
.reload_geometry:
    mov bx, [bas_form_window]
    call OSAPI_WM_CONTENT
    mov [bas_form_origin_x], ax
    mov [bas_form_origin_y], dx
    call OSAPI_WM_GEOM
    BAS_JC .out
.geometry_ok:
    mov [bas_form_width], cx
    mov [bas_form_height], dx

    mov al, [bas_ui_ground]
    call OSAPI_SET_COLOR
    mov ax, [bas_form_origin_x]
    mov bx, [bas_form_origin_y]
    mov cx, [bas_form_width]
    add cx, ax
    dec cx
    mov dx, [bas_form_height]
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL

    xor bp, bp
.control_loop:
    cmp bp, [bas_form_count]
    BAS_JAE .grow
    mov bx, bp
    call bas_gui_rec_ptr
    test byte [di+GUI_FLAGS], GUI_F_VISIBLE
    BAS_JZ .next
    mov ax, [bas_form_origin_x]
    add ax, [di+GUI_X]
    mov bx, [bas_form_origin_y]
    add bx, [di+GUI_Y]
    mov dl, [di+GUI_TYPE]
    cmp dl, GUI_LABEL
    BAS_JZ .draw_label
    cmp dl, GUI_BUTTON
    BAS_JZ .draw_button
    cmp dl, GUI_CHECKBOX
    BAS_JZ .draw_check
    cmp dl, GUI_FRAME
    BAS_JZ .draw_frame
    cmp dl, GUI_IMAGE
    BAS_JZ .draw_image
    jmp .next

.draw_label:
    mov cx, ax
    mov dx, bx
    mov al, CBLACK
    test byte [di+GUI_FLAGS], GUI_F_ENABLED
    BAS_JNZ .label_color
    mov al, CDGRAY
.label_color:
    mov ah, [bas_ui_ground]         ; the form's ground, under the run
    push ax
    mov bx, bp
    call bas_gui_text_ptr
    mov si, di
    pop ax
    call OSAPI_FONT_RUN            ; one opaque pass (SPEC.md 6.1, 6.6)
    jmp .next

.draw_button:
    push ax
    push bx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop bx
    pop ax
    mov cx, ax
    add cx, [di+GUI_W]
    dec cx
    mov dx, bx
    add dx, [di+GUI_H]
    dec dx
    call OSAPI_GFX_FILL
    push ax
    push bx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop bx
    pop ax
    mov cx, ax
    add cx, [di+GUI_W]
    dec cx
    mov dx, bx
    add dx, [di+GUI_H]
    dec dx
    call OSAPI_GFX_FRAME
    add ax, 4
    add bx, 5
    mov cx, ax
    mov dx, bx
    mov bx, bp
    call bas_gui_text_ptr
    mov si, di
    mov ax, CWHITE * 256 + CBLACK   ; the button's face, under the run
    call OSAPI_FONT_RUN            ; one opaque pass (SPEC.md 6.1, 6.6)
    jmp .next

.draw_check:
    push ax
    push bx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop bx
    pop ax
    mov cx, ax
    add cx, 10
    mov dx, bx
    add dx, 10
    call OSAPI_GFX_FILL
    push ax
    push bx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop bx
    pop ax
    mov cx, ax
    add cx, 10
    mov dx, bx
    add dx, 10
    call OSAPI_GFX_FRAME
    cmp word [di+GUI_VALUE], 0
    BAS_JZ .check_text
    add ax, 3
    add bx, 3
    mov cx, ax
    add cx, 4
    mov dx, bx
    add dx, 4
    call OSAPI_GFX_FILL
.check_text:
    mov ax, [bas_form_origin_x]
    add ax, [di+GUI_X]
    add ax, 15
    mov bx, [bas_form_origin_y]
    add bx, [di+GUI_Y]
    add bx, 2
    mov cx, ax
    mov dx, bx
    mov bx, bp
    call bas_gui_text_ptr
    mov si, di
    mov ah, [bas_ui_ground]         ; the form's ground, under the run
    mov al, CBLACK
    call OSAPI_FONT_RUN            ; one opaque pass (SPEC.md 6.1, 6.6)
    jmp .next

.draw_frame:
    push ax
    push bx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop bx
    pop ax
    mov cx, ax
    add cx, [di+GUI_W]
    dec cx
    mov dx, bx
    add dx, [di+GUI_H]
    dec dx
    call OSAPI_GFX_FRAME
    add ax, 6
    mov cx, ax
    mov dx, bx
    mov bx, bp
    call bas_gui_text_ptr
    mov si, di
    mov ah, [bas_ui_ground]         ; a group box: the caption's cells break
    mov al, CBLACK
                                    ; the frame's top edge under it
    call OSAPI_FONT_RUN            ; one opaque pass (SPEC.md 6.1, 6.6)
    jmp .next

.draw_image:
    mov dx, [di+GUI_VALUE]
    cmp dx, BAS_IMAGE_SLOTS-1
    BAS_JA .next
    push bp
    push ax
    push bx
    mov ax, dx
    mov cx, BAS_IMAGE_META_SIZE
    mul cx
    mov si, bas_image_meta
    add si, ax
    cmp byte [si], 1
    BAS_JNZ .image_pop ; Compute the image-data address before loading DX with the height.
    ; 8086 MUL writes DX:AX, so doing this afterwards would silently turn
    ; every form IMAGE into a zero-height blit.
    mov ax, [di+GUI_VALUE]
    mov cx, BAS_IMAGE_SLOT_BYTES
    mul cx
    push ax
    mov cx, [si+2]
    mov dx, [si+4]
    mov bp, [si+6]
    pop si
    add si, bas_image_data
    push ds
    pop es
    pop bx
    pop ax
    call OSAPI_GFX_BLIT4
    pop bp
    jmp .next
.image_pop:
    pop bx
    pop ax
    pop bp
.next:
    inc bp
    jmp .control_loop
.grow:
    mov bx, [bas_form_window]
    call OSAPI_WM_GROW
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

bas_form_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [bas_dialog_mode], DLG_NONE
    BAS_JNZ .out
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [bas_form_origin_x], ax
    mov [bas_form_origin_y], dx
    call OSAPI_MOUSE
    sub cx, [bas_form_origin_x]
    sub dx, [bas_form_origin_y]
    xor bp, bp
.loop:
    cmp bp, [bas_form_count]
    BAS_JAE .out
    mov bx, bp
    call bas_gui_rec_ptr
    mov al, [di+GUI_FLAGS]
    and al, GUI_F_VISIBLE | GUI_F_ENABLED
    cmp al, GUI_F_VISIBLE | GUI_F_ENABLED
    BAS_JNZ .next
    cmp byte [di+GUI_TYPE], GUI_BUTTON
    BAS_JZ .hit_test
    cmp byte [di+GUI_TYPE], GUI_CHECKBOX
    BAS_JZ .hit_test
    cmp byte [di+GUI_TYPE], GUI_IMAGE
    BAS_JNZ .next
.hit_test:
    mov ax, [di+GUI_X]
    cmp cx, ax
    BAS_JL .next
    add ax, [di+GUI_W]
    cmp cx, ax
    BAS_JGE .next
    mov ax, [di+GUI_Y]
    cmp dx, ax
    BAS_JL .next
    add ax, [di+GUI_H]
    cmp dx, ax
    BAS_JGE .next
    cmp byte [di+GUI_TYPE], GUI_CHECKBOX
    BAS_JNZ .event
    xor word [di+GUI_VALUE], 1
.event:
    mov ax, [di+GUI_ID]
    mov [bas_form_event], ax
    mov si, [bas_form_window]
    call bas_form_paint
    jmp .out
.next:
    inc bp
    jmp .loop
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_form_onkey:
    cmp byte [bas_dialog_mode], DLG_NONE
    BAS_JNZ .out
    cmp al, 27
    BAS_JNZ .out
    mov bx, [bas_form_window]
    call OSAPI_WM_HIDE
    mov byte [bas_form_visible], 0
    mov word [bas_form_event], -1
    mov bx, [bas_window]
    call OSAPI_WM_SHOW
    call OSAPI_WM_FRONT
    mov byte [bas_console_min], 0
.out:
    ret

; ----------------------------------------------------------------------------
; Shared message/input dialog callbacks
; ----------------------------------------------------------------------------
bas_dialog_paint:
    push es
    push ds
    pop es
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [bas_dialog_origin_x], ax
    mov [bas_dialog_origin_y], dx
    call OSAPI_WM_GEOM
    BAS_JC .out
    mov al, [bas_ui_ground]
    call OSAPI_SET_COLOR
    mov ax, [bas_dialog_origin_x]
    mov bx, [bas_dialog_origin_y]
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov cx, [bas_dialog_origin_x]
    add cx, 10
    mov dx, [bas_dialog_origin_y]
    add dx, 12
    mov si, bas_dialog_text
    mov ah, [bas_ui_ground]         ; the dialog's ground
    mov al, CBLACK
    call OSAPI_FONT_RUN            ; one opaque pass (SPEC.md 6.1, 6.6)
    cmp byte [bas_dialog_mode], DLG_INPUT
    BAS_JNZ .buttons
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [bas_dialog_origin_x]
    add ax, 10
    mov bx, [bas_dialog_origin_y]
    add bx, 38
    mov cx, ax
    add cx, 215
    mov dx, bx
    add dx, 16
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [bas_dialog_origin_x]
    add ax, 10
    mov bx, [bas_dialog_origin_y]
    add bx, 38
    mov cx, ax
    add cx, 215
    mov dx, bx
    add dx, 16
    call OSAPI_GFX_FRAME
    mov cx, ax
    add cx, 4
    mov dx, bx
    add dx, 4
    mov si, bas_dialog_input
    mov ax, CWHITE * 256 + CBLACK   ; the input field's face
    call OSAPI_FONT_RUN            ; one opaque pass (SPEC.md 6.1, 6.6)
.buttons:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [bas_dialog_origin_x]
    add ax, 45
    mov bx, [bas_dialog_origin_y]
    add bx, 72
    mov cx, ax
    add cx, 70
    mov dx, bx
    add dx, 20
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [bas_dialog_origin_x]
    add ax, 45
    mov bx, [bas_dialog_origin_y]
    add bx, 72
    mov cx, ax
    add cx, 70
    mov dx, bx
    add dx, 20
    call OSAPI_GFX_FRAME
    mov cx, ax
    add cx, 26
    mov dx, bx
    add dx, 6
    mov si, bas_s_ok
    mov ax, CWHITE * 256 + CBLACK   ; the button's face
    call OSAPI_FONT_RUN            ; one opaque pass (SPEC.md 6.1, 6.6)
    cmp byte [bas_dialog_mode], DLG_INPUT
    BAS_JNZ .out
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [bas_dialog_origin_x]
    add ax, 130
    mov bx, [bas_dialog_origin_y]
    add bx, 72
    mov cx, ax
    add cx, 80
    mov dx, bx
    add dx, 20
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [bas_dialog_origin_x]
    add ax, 130
    mov bx, [bas_dialog_origin_y]
    add bx, 72
    mov cx, ax
    add cx, 80
    mov dx, bx
    add dx, 20
    call OSAPI_GFX_FRAME
    mov cx, ax
    add cx, 14
    mov dx, bx
    add dx, 6
    mov si, bas_s_cancel
    mov ax, CWHITE * 256 + CBLACK   ; the button's face
    call OSAPI_FONT_RUN            ; one opaque pass (SPEC.md 6.1, 6.6)
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

bas_dialog_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_MOUSE
    sub cx, [bas_dialog_origin_x]
    sub dx, [bas_dialog_origin_y]
    cmp dx, 72
    BAS_JL .out
    cmp dx, 92
    BAS_JG .out
    cmp cx, 45
    BAS_JL .cancel_test
    cmp cx, 115
    BAS_JG .cancel_test
    cmp byte [bas_dialog_mode], DLG_INPUT
    BAS_JNZ .message_ok
    call bas_dialog_accept
    jmp .out
.message_ok:
    mov byte [bas_dialog_result], 1
    mov bx, [bas_dialog_window]
    call OSAPI_WM_HIDE
    jmp .out
.cancel_test:
    cmp byte [bas_dialog_mode], DLG_INPUT
    BAS_JNZ .out
    cmp cx, 130
    BAS_JL .out
    cmp cx, 210
    BAS_JG .out
    mov byte [bas_dialog_result], 2
    mov bx, [bas_dialog_window]
    call OSAPI_WM_HIDE
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bas_dialog_onkey:
    push ax
    push bx
    push si
    cmp byte [bas_dialog_mode], DLG_MESSAGE
    BAS_JNZ .input
    cmp al, 13
    BAS_JZ .message_done
    cmp al, 27
    BAS_JNZ .out
.message_done:
    mov byte [bas_dialog_result], 1
    mov bx, [bas_dialog_window]
    call OSAPI_WM_HIDE
    jmp .out
.input:
    cmp byte [bas_dialog_mode], DLG_INPUT
    BAS_JNZ .out
    cmp al, 27
    BAS_JZ .cancel
    cmp al, 13
    BAS_JZ .accept
    cmp al, 8
    BAS_JZ .backspace
    cmp al, 32
    BAS_JB .out
    cmp al, 126
    BAS_JA .out
    mov bx, [bas_dialog_input_len]
    cmp bx, BAS_DIALOG_INPUT_CAP
    BAS_JAE .out
    mov [bas_dialog_input+bx], al
    inc bx
    mov [bas_dialog_input_len], bx
    mov byte [bas_dialog_input+bx], 0
    jmp .repaint
.backspace:
    mov bx, [bas_dialog_input_len]
    test bx, bx
    BAS_JZ .out
    dec bx
    mov [bas_dialog_input_len], bx
    mov byte [bas_dialog_input+bx], 0
    jmp .repaint
.accept:
    call bas_dialog_accept
    jmp .out
.cancel:
    mov byte [bas_dialog_result], 2
    mov bx, [bas_dialog_window]
    call OSAPI_WM_HIDE
    jmp .out
.repaint:
    mov si, [bas_dialog_window]
    call bas_dialog_paint
.out:
    pop si
    pop bx
    pop ax
    ret

; --- window -------------------------------------------------------------------
bas_tpl:
    dw 30, 34, 580, 390
    dw bas_title, bas_win_cls
bas_ui_ground:   db CLGRAY       ; FORM/dialog ground: bas_init picks it
bas_title:       db '2BASIC B37', 0
                 times 32 - ($ - bas_title) db 0

bas_form_tpl:
    dw 135, 78, 360, 240
    dw bas_form_title, bas_form_cls

bas_dialog_tpl:
    dw 185, 145, 250, 130
    dw bas_s_dialog_title, bas_dialog_cls

; The window classes (SPEC.md 11.104): every handler, and the flags each
; window is born with. FORM's sizable bit is the program's to set at run
; time (OSAPI_WM_FLAG, WFB_SIZABLE).
    OS88_CLASS bas_win_cls, WF_SIZABLE, PAINT, bas_paint, ONKEY, bas_onkey, \
               ONWAKE, bas_onwake
    OS88_CLASS bas_form_cls, 0, PAINT, bas_form_paint, \
               ONKEY, bas_form_onkey, ONCLICK, bas_form_onclick
    OS88_CLASS bas_dialog_cls, 0, PAINT, bas_dialog_paint, \
               ONKEY, bas_dialog_onkey, ONCLICK, bas_dialog_onclick

; --- keywords -----------------------------------------------------------------
bas_kw_help:       db 'HELP', 0
bas_kw_reset:      db 'RESET', 0
bas_kw_new:        db 'NEW', 0
bas_kw_list:       db 'LIST', 0
bas_kw_delete:     db 'DELETE', 0
bas_kw_edit:       db 'EDIT', 0
bas_kw_auto:       db 'AUTO', 0
bas_kw_cls:        db 'CLS', 0
bas_kw_run:        db 'RUN', 0
bas_kw_save:       db 'SAVE', 0
bas_kw_load:       db 'LOAD', 0
bas_kw_print:      db 'PRINT', 0
bas_kw_input:      db 'INPUT', 0
bas_kw_rem:        db 'REM', 0
bas_kw_end:        db 'END', 0
bas_kw_goto:       db 'GOTO', 0
bas_kw_gosub:      db 'GOSUB', 0
bas_kw_return:     db 'RETURN', 0
bas_kw_if:         db 'IF', 0
bas_kw_on:         db 'ON', 0
bas_kw_dim:        db 'DIM', 0
bas_kw_then:       db 'THEN', 0
bas_kw_else:       db 'ELSE', 0
bas_kw_let:        db 'LET', 0
bas_kw_for:        db 'FOR', 0
bas_kw_to:         db 'TO', 0
bas_kw_step:       db 'STEP', 0
bas_kw_next:       db 'NEXT', 0
bas_kw_promptcolor: db 'PROMPTCOLOR', 0
bas_kw_color:      db 'COLOR', 0
bas_kw_locate:     db 'LOCATE', 0
bas_kw_screen:     db 'SCREEN', 0
bas_kw_pset:       db 'PSET', 0
bas_kw_line:       db 'LINE', 0
bas_kw_box:        db 'BOX', 0
bas_kw_fillbox:    db 'FILLBOX', 0
bas_kw_drawbegin:  db 'DRAWBEGIN', 0
bas_kw_drawend:    db 'DRAWEND', 0
bas_kw_circle:     db 'CIRCLE', 0
bas_kw_beep:       db 'BEEP', 0
bas_kw_sound:      db 'SOUND', 0
bas_kw_randomize:  db 'RANDOMIZE', 0
bas_kw_sleep:      db 'SLEEP', 0
bas_kw_poke:       db 'POKE', 0
bas_kw_peek:       db 'PEEK', 0
bas_kw_inkey:      db 'INKEY', 0
bas_kw_fmpreset:   db 'FMPRESET', 0
bas_kw_fmnote:     db 'FMNOTE', 0
bas_kw_fmoff:      db 'FMOFF', 0
bas_kw_fmstop:     db 'FMSTOP', 0
bas_kw_loadbmp:    db 'LOADBMP', 0
bas_kw_loadres:    db 'LOADRES', 0
bas_kw_putres:     db 'PUTRES', 0
bas_kw_resselect:  db 'RESSELECT', 0
bas_kw_rescount:   db 'RESCOUNT', 0
bas_kw_reslen:     db 'RESLEN', 0
bas_kw_resbyte:    db 'RESBYTE', 0
bas_kw_resword:    db 'RESWORD', 0
bas_kw_resline:    db 'RESLINE$', 0
bas_kw_putbmp:     db 'PUTBMP', 0
bas_kw_freebmp:    db 'FREEBMP', 0
bas_kw_capacity:   db 'CAPACITY', 0
bas_kw_mem:        db 'MEM', 0
bas_kw_version:    db 'VERSION', 0
bas_kw_ticks:      db 'TICKS', 0
bas_kw_mouselx:    db 'MOUSELX', 0
bas_kw_mousely:    db 'MOUSELY', 0
bas_kw_mousex:     db 'MOUSEX', 0
bas_kw_mousey:     db 'MOUSEY', 0
bas_kw_mouseb:     db 'MOUSEB', 0
bas_kw_screenw:    db 'SCREENW', 0
bas_kw_screenh:    db 'SCREENH', 0
bas_kw_screenbpp:  db 'SCREENBPP', 0
bas_kw_videomode: db 'VIDEOMODE', 0
bas_kw_adlib:      db 'ADLIB', 0
bas_kw_fre:        db 'FRE', 0
bas_kw_rnd:        db 'RND', 0
bas_kw_mod:        db 'MOD', 0
bas_kw_and:        db 'AND', 0
bas_kw_or:         db 'OR', 0
bas_kw_not:        db 'NOT', 0
bas_kw_form:       db 'FORM', 0
bas_kw_label:      db 'LABEL', 0
bas_kw_button:     db 'BUTTON', 0
bas_kw_checkbox:   db 'CHECKBOX', 0
bas_kw_frame:      db 'FRAME', 0
bas_kw_image:      db 'IMAGE', 0
bas_kw_showform:   db 'SHOWFORM', 0
bas_kw_closeform:  db 'CLOSEFORM', 0
bas_kw_event:      db 'EVENT', 0
bas_kw_value:      db 'VALUE', 0
bas_kw_svar:       db 'SVAR', 0
bas_kw_lvar:       db 'LVAR', 0
bas_kw_strs:       db 'STR$', 0
bas_kw_val:        db 'VAL', 0
bas_kw_settext:    db 'SETTEXT', 0
bas_kw_setvalue:   db 'SETVALUE', 0
bas_kw_enable:     db 'ENABLE', 0
bas_kw_visible:    db 'VISIBLE', 0
bas_kw_messagebox: db 'MESSAGEBOX', 0
bas_kw_inputbox:   db 'INPUTBOX', 0
bas_kw_consolesize: db 'CONSOLESIZE', 0
bas_kw_minconsole: db 'MINCONSOLE', 0
bas_kw_restoreconsole: db 'RESTORECONSOLE', 0
bas_kw_yes:        db 'YES', 0
bas_kw_no:         db 'NO', 0

; --- messages -----------------------------------------------------------------
bas_ext_bas:       db 'BAS'
bas_ext_bmp:       db 'BMP'
bas_ext_res:       db 'RES'
bas_s_var_file:    db '2BASIC.DAT', 0
bas_s_help_file:   db 'HELP.TXT', 0
bas_s_help_index:  db 'INDEX', 0
bas_var_magic:     db '2BD1'
bas_s_prompt_right: db ']', 0
bas_s_prompt_question: db '?', 0
bas_s_banner:      db '2BASIC B37', 0
bas_s_title_prefix: db '2BASIC B37 - ', 0
bas_s_title_default: db '2BASIC B37', 0
bas_s_untitled:    db 'UNTITLED', 0
bas_s_ready:       db 'READY', 0
bas_s_blank_console: times BAS_CON_COLS - 1 db ' '
                     db 0
bas_s_empty:       db 0
bas_s_running:     db 'RUNNING - ESC / CTRL+C BREAK', 0
bas_s_redo:        db '?REDO FROM START', 0
bas_s_saved:       db 'SAVED', 0
bas_s_loaded:      db 'LOADED', 0
bas_s_syntax:      db '?SYNTAX ERROR', 0
bas_s_divzero:     db '?DIVISION BY ZERO', 0
bas_s_undef:       db '?UNDEFINED LINE', 0
bas_s_step:        db '?STEP LIMIT', 0
bas_s_overflow:    db '?OVERFLOW', 0
bas_s_prog_full:   db '?PROGRAM FULL', 0
bas_s_bad_program: db '?BAD PROGRAM FILE', 0
bas_s_return:      db '?RETURN WITHOUT GOSUB', 0
bas_s_gosub_full:  db '?OUT OF GOSUB SPACE', 0
bas_s_for_full:    db '?OUT OF FOR SPACE', 0
bas_s_next:        db '?NEXT WITHOUT FOR', 0
bas_s_for_next:    db '?FOR WITHOUT NEXT', 0
bas_s_gfx_full:    db '?GRAPHICS LIST FULL', 0
bas_s_illegal:     db '?ILLEGAL FUNCTION CALL', 0
bas_s_screen:      db '?SCREEN 1 REQUIRED', 0
bas_s_bmp_format:  db '?UNSUPPORTED BMP', 0
bas_s_no_bmp:      db '?IMAGE SLOT EMPTY', 0
bas_s_bad_res:     db '?BAD RESOURCE FILE', 0
bas_s_no_res:      db '?RESOURCE NOT LOADED', 0
bas_s_no_adlib:    db '?NO ADLIB', 0
bas_s_fm_refused:  db '?FM REQUEST REFUSED', 0
bas_s_file_error:  db '?FILE ERROR', 0
bas_s_no_file:     db '?FILE NOT FOUND', 0
bas_s_multi_bas:   db '?MULTIPLE BAS FILES - SPECIFY NAME', 0
bas_s_file_big:    db '?FILE TOO LARGE', 0
bas_s_out_memory:  db '?OUT OF MEMORY', 0
bas_s_var_store:   db '?VARIABLE STORE ERROR', 0
bas_s_var_full:    db '?VARIABLE STORE FULL', 0
bas_s_disk_full:   db '?DISK FULL', 0
bas_s_write_prot:  db '?WRITE PROTECTED', 0
bas_s_bad_name:    db '?BAD FILE NAME', 0
bas_s_break:       db 'BREAK', 0
bas_s_no_worker:   db '?NO WORKER SLOT', 0
bas_s_prog_mode:   db '?PROGRAM MODE ONLY', 0
bas_s_form_full:   db '?FORM CONTROL LIMIT', 0
bas_s_no_form:     db '?FORM UNAVAILABLE', 0
bas_s_bad_control: db '?UNKNOWN CONTROL', 0
bas_s_no_help:     db '?HELP.TXT NOT FOUND', 0
bas_s_bad_help:    db '?HELP.TXT READ ERROR', 0
bas_s_help_topic:  db '?UNKNOWN HELP TOPIC', 0
bas_s_bmp_loaded:  db 'LOADED', 0
bas_s_res_loaded:  db 'RESOURCE LOADED', 0
bas_s_mem_used:    db 'PROGRAM BYTES USED ', 0
bas_s_mem_free:    db 'PROGRAM BYTES FREE ', 0
bas_s_mem_lines:   db 'PROGRAM LINES ', 0
bas_s_mem_gosub:   db 'GOSUB DEPTH ', 0
bas_s_mem_for:     db 'FOR DEPTH ', 0
bas_s_mem_gfx:     db 'GRAPHICS USED ', 0
bas_s_cap_image:   db 'IMAGE BYTES ', 0
bas_s_cap_bss:     db 'BSS BYTES ', 0
bas_s_cap_head:    db 'PACKAGE HEADROOM ', 0
bas_s_cap2:        db 'PROGRAM 4864 BYTES, 288 LINES, 127 CHARS', 0
bas_s_cap3:        db 'USER MEMORY 3072B; ARRAYS 512 WORDS', 0
bas_s_cap4:        db 'GRAPHICS 84 RETAINED COMMANDS', 0
bas_s_cap5:        db 'IMAGES 4 SLOTS, MAX 64X64 4BPP', 0
bas_s_cap6:        db 'CONSOLE 35X63, VIEW 26..63 X 6..ADAPTER', 0
bas_s_form_default: db '2BASIC FORM', 0
bas_s_dialog_title: db '2BASIC DIALOG', 0
bas_s_ok:           db 'OK', 0
bas_s_cancel:       db 'CANCEL', 0

; 8 compact two-operator OPL2 patches: mod 20/40/60/80/E0,
; carrier 20/40/60/80/E0, then channel C0.
bas_fm_patches:
    db 21h,3Fh,0F2h,0F4h,00h, 01h,00h,0F2h,0F4h,00h, 01h ; piano
    db 01h,20h,0F3h,77h,00h, 01h,00h,0F3h,77h,00h, 00h ; organ
    db 21h,18h,0F1h,65h,00h, 01h,00h,0F1h,65h,00h, 01h ; bass
    db 11h,10h,0F4h,23h,00h, 01h,00h,0F5h,14h,00h, 01h ; bell
    db 21h,10h,0F2h,54h,00h, 01h,00h,0F2h,54h,00h, 01h ; brass
    db 21h,08h,0F1h,34h,00h, 01h,00h,0F2h,24h,00h, 01h ; lead
    db 31h,00h,0F7h,08h,00h, 01h,00h,0F7h,08h,00h, 01h ; laser
    db 31h,00h,0FFh,0Fh,00h, 21h,00h,0FFh,0Fh,00h, 07h ; explosion

    OS88_BSS BAS_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed per-instance state -----------------------------------------
bas_con_count    equ os88_image_end + BAS_O_CON_COUNT
bas_input_len    equ os88_image_end + BAS_O_INPUT_LEN
bas_input_pos    equ os88_image_end + BAS_O_INPUT_POS
bas_input        equ os88_image_end + BAS_O_INPUT
bas_history_len  equ os88_image_end + BAS_O_HISTORY_LEN
bas_history      equ os88_image_end + BAS_O_HISTORY
bas_console      equ os88_image_end + BAS_O_CONSOLE
bas_con_fg       equ os88_image_end + BAS_O_CON_FG
bas_con_bg       equ os88_image_end + BAS_O_CON_BG
bas_program      equ os88_image_end + BAS_O_PROGRAM
bas_line_index   equ os88_image_end + BAS_O_LINE_INDEX
bas_program_used equ os88_image_end + BAS_O_PROGRAM_USED
bas_program_count equ os88_image_end + BAS_O_PROGRAM_COUNT
bas_scratch      equ os88_image_end + BAS_O_SCRATCH
bas_filename     equ os88_image_end + BAS_O_FILENAME
bas_io_len       equ os88_image_end + BAS_O_IO_LEN
bas_io_seg       equ os88_image_end + BAS_O_IO_SEG
bas_vars         equ os88_image_end + BAS_O_VARS
bas_linebuf      equ os88_image_end + BAS_O_LINEBUF
bas_gosub_count  equ os88_image_end + BAS_O_GOSUB_COUNT
bas_gosub_stack  equ os88_image_end + BAS_O_GOSUB_STACK
bas_for_count    equ os88_image_end + BAS_O_FOR_COUNT
bas_for_stack    equ os88_image_end + BAS_O_FOR_STACK
bas_gfx_count    equ os88_image_end + BAS_O_GFX_COUNT
bas_gfx_list     equ os88_image_end + BAS_O_GFX_LIST
bas_fg_color     equ os88_image_end + BAS_O_FG_COLOR
bas_bg_color     equ os88_image_end + BAS_O_BG_COLOR
bas_prompt_fg    equ os88_image_end + BAS_O_PROMPT_FG
bas_prompt_bg    equ os88_image_end + BAS_O_PROMPT_BG
bas_screen_mode  equ os88_image_end + BAS_O_SCREEN_MODE
bas_text_x       equ os88_image_end + BAS_O_TEXT_X
bas_text_y       equ os88_image_end + BAS_O_TEXT_Y
bas_input_y      equ os88_image_end + BAS_O_INPUT_Y
bas_visible      equ os88_image_end + BAS_O_VISIBLE
bas_content_x    equ os88_image_end + BAS_O_CONTENT_X
bas_content_y    equ os88_image_end + BAS_O_CONTENT_Y
bas_content_w    equ os88_image_end + BAS_O_CONTENT_W
bas_content_h    equ os88_image_end + BAS_O_CONTENT_H
bas_best_ptr     equ os88_image_end + BAS_O_BEST_PTR
bas_last_line    equ os88_image_end + BAS_O_LAST_LINE
bas_current_line equ os88_image_end + BAS_O_CURRENT_LINE
bas_steps        equ os88_image_end + BAS_O_STEPS
bas_expr_depth   equ os88_image_end + BAS_O_EXPR_DEPTH
bas_error_code   equ os88_image_end + BAS_O_ERROR_CODE
bas_draw_x0      equ os88_image_end + BAS_O_DRAW_X0
bas_draw_y0      equ os88_image_end + BAS_O_DRAW_Y0
bas_draw_x1      equ os88_image_end + BAS_O_DRAW_X1
bas_draw_y1      equ os88_image_end + BAS_O_DRAW_Y1
bas_draw_dx      equ os88_image_end + BAS_O_DRAW_DX
bas_draw_dy      equ os88_image_end + BAS_O_DRAW_DY
bas_draw_sx      equ os88_image_end + BAS_O_DRAW_SX
bas_draw_sy      equ os88_image_end + BAS_O_DRAW_SY
bas_draw_err     equ os88_image_end + BAS_O_DRAW_ERR
bas_circle_x     equ os88_image_end + BAS_O_CIRCLE_X
bas_circle_y     equ os88_image_end + BAS_O_CIRCLE_Y
bas_circle_err   equ os88_image_end + BAS_O_CIRCLE_ERR
bas_circle_cx    equ os88_image_end + BAS_O_CIRCLE_CX
bas_circle_cy    equ os88_image_end + BAS_O_CIRCLE_CY
bas_poke_bank    equ os88_image_end + BAS_O_POKE_BANK
bas_image_meta   equ os88_image_end + BAS_O_IMAGE_META
bas_image_data   equ os88_image_end + BAS_O_IMAGE_DATA
bas_bmp_offset   equ os88_image_end + BAS_O_BMP_OFFSET
bas_bmp_stride   equ os88_image_end + BAS_O_BMP_STRIDE
bas_bmp_width    equ os88_image_end + BAS_O_BMP_WIDTH
bas_bmp_height   equ os88_image_end + BAS_O_BMP_HEIGHT
bas_bmp_slot     equ os88_image_end + BAS_O_BMP_SLOT
bas_cap_tmp      equ os88_image_end + BAS_O_CAP_TMP
bas_window       equ os88_image_end + BAS_O_WINDOW
bas_run_state    equ os88_image_end + BAS_O_RUN_STATE
bas_worker_started equ os88_image_end + BAS_O_WORKER_STARTED
bas_exec_busy    equ os88_image_end + BAS_O_EXEC_BUSY
bas_break_req    equ os88_image_end + BAS_O_BREAK_REQ
bas_dirty        equ os88_image_end + BAS_O_DIRTY
bas_last_key     equ os88_image_end + BAS_O_LAST_KEY
bas_input_var    equ os88_image_end + BAS_O_INPUT_VAR
bas_out_row      equ os88_image_end + BAS_O_OUT_ROW
bas_out_col      equ os88_image_end + BAS_O_OUT_COL
bas_print_nonl   equ os88_image_end + BAS_O_PRINT_NONL
bas_worker_active equ os88_image_end + BAS_O_WORKER_ACTIVE
bas_form_window   equ os88_image_end + BAS_O_FORM_WINDOW
bas_dialog_window equ os88_image_end + BAS_O_DIALOG_WINDOW
bas_form_count    equ os88_image_end + BAS_O_FORM_COUNT
bas_form_visible  equ os88_image_end + BAS_O_FORM_VISIBLE
bas_form_event    equ os88_image_end + BAS_O_FORM_EVENT
bas_form_title    equ os88_image_end + BAS_O_FORM_TITLE
bas_form_recs     equ os88_image_end + BAS_O_FORM_RECS
bas_form_text     equ os88_image_end + BAS_O_FORM_TEXT
bas_dialog_mode   equ os88_image_end + BAS_O_DIALOG_MODE
bas_dialog_result equ os88_image_end + BAS_O_DIALOG_RESULT
bas_dialog_var    equ os88_image_end + BAS_O_DIALOG_VAR
bas_dialog_input_len equ os88_image_end + BAS_O_DIALOG_INPUT_LEN
bas_dialog_text   equ os88_image_end + BAS_O_DIALOG_TEXT
bas_dialog_input  equ os88_image_end + BAS_O_DIALOG_INPUT
bas_input_ready   equ os88_image_end + BAS_O_INPUT_READY
bas_auto_mode     equ os88_image_end + BAS_O_AUTO_MODE
bas_auto_next     equ os88_image_end + BAS_O_AUTO_NEXT
bas_auto_step     equ os88_image_end + BAS_O_AUTO_STEP
bas_draw_batch    equ os88_image_end + BAS_O_DRAW_BATCH
bas_console_min   equ os88_image_end + BAS_O_CONSOLE_MIN
bas_form_origin_x equ os88_image_end + BAS_O_FORM_ORIGIN_X
bas_form_origin_y equ os88_image_end + BAS_O_FORM_ORIGIN_Y
bas_form_width    equ os88_image_end + BAS_O_FORM_WIDTH
bas_form_height   equ os88_image_end + BAS_O_FORM_HEIGHT
bas_form_req_w    equ os88_image_end + BAS_O_FORM_REQ_W
bas_form_req_h    equ os88_image_end + BAS_O_FORM_REQ_H
bas_form_resize   equ os88_image_end + BAS_O_FORM_RESIZE
bas_dialog_origin_x equ os88_image_end + BAS_O_DIALOG_ORIGIN_X
bas_dialog_origin_y equ os88_image_end + BAS_O_DIALOG_ORIGIN_Y
bas_var_data      equ os88_image_end + BAS_O_VAR_DATA
bas_var_bad       equ os88_image_end + BAS_O_VAR_BAD
bas_val_text      equ os88_image_end + BAS_O_VAL_TEXT
bas_array_used    equ os88_image_end + BAS_O_ARRAY_USED
bas_array_size    equ os88_image_end + BAS_O_ARRAY_SIZE
bas_array_base    equ os88_image_end + BAS_O_ARRAY_BASE
bas_array_data    equ os88_image_end + BAS_O_ARRAY_DATA
bas_batch_fast    equ os88_image_end + BAS_O_BATCH_FAST
bas_gfx_delta_count equ os88_image_end + BAS_O_GFX_DELTA_COUNT
bas_gfx_delta_list equ os88_image_end + BAS_O_GFX_DELTA_LIST
bas_icon_record   equ os88_image_end + BAS_O_ICON_RECORD
bas_var_dirty     equ os88_image_end + BAS_O_VAR_DIRTY
bas_help_topic    equ os88_image_end + BAS_O_HELP_TOPIC
bas_row_dirty_first equ os88_image_end + BAS_O_ROW_DIRTY_FIRST
bas_row_dirty_last  equ os88_image_end + BAS_O_ROW_DIRTY_LAST
bas_prompt_last_cols equ os88_image_end + BAS_O_PROMPT_LAST_COLS
bas_view_first      equ os88_image_end + BAS_O_VIEW_FIRST
bas_view_pending    equ os88_image_end + BAS_O_VIEW_PENDING
bas_view_last_tick equ os88_image_end + BAS_O_VIEW_LAST_TICK
bas_path_buf       equ os88_image_end + BAS_O_PATH_BUF
bas_path_leaf      equ os88_image_end + BAS_O_PATH_LEAF
bas_path_next      equ os88_image_end + BAS_O_PATH_NEXT
bas_path_old_cluster equ os88_image_end + BAS_O_PATH_OLD_CLUSTER
bas_path_old_drive equ os88_image_end + BAS_O_PATH_OLD_DRIVE
bas_path_drive     equ os88_image_end + BAS_O_PATH_DRIVE
bas_path_pending   equ os88_image_end + BAS_O_PATH_PENDING
bas_dir_name       equ os88_image_end + BAS_O_DIR_NAME
bas_dir_record     equ os88_image_end + BAS_O_DIR_RECORD
bas_prev_filename  equ os88_image_end + BAS_O_PATH_PREV_FILENAME
bas_home_cluster   equ os88_image_end + BAS_O_HOME_CLUSTER
bas_home_drive     equ os88_image_end + BAS_O_HOME_DRIVE
bas_help_cluster   equ os88_image_end + BAS_O_HELP_CLUSTER
bas_help_drive     equ os88_image_end + BAS_O_HELP_DRIVE
bas_view_next       equ os88_image_end + BAS_O_VIEW_NEXT
bas_res_stage       equ os88_image_end + BAS_O_RES_STAGE
bas_res_length      equ os88_image_end + BAS_O_RES_LENGTH
bas_res_offset      equ os88_image_end + BAS_O_RES_OFFSET
bas_res_count       equ os88_image_end + BAS_O_RES_COUNT
bas_res_present     equ os88_image_end + BAS_O_RES_PRESENT
bas_res_new_off     equ os88_image_end + BAS_O_RES_NEW_OFF
bas_res_new_count   equ os88_image_end + BAS_O_RES_NEW_COUNT
bas_res_prev_end    equ os88_image_end + BAS_O_RES_PREV_END
bas_res_name        equ os88_image_end + BAS_O_RES_NAME
bas_res_op          equ os88_image_end + BAS_O_RES_OP
bas_res_item_off    equ os88_image_end + BAS_O_RES_ITEM_OFF
bas_res_item_len    equ os88_image_end + BAS_O_RES_ITEM_LEN
bas_res_item_cnt    equ os88_image_end + BAS_O_RES_ITEM_CNT
bas_res_item_typ    equ os88_image_end + BAS_O_RES_ITEM_TYP
