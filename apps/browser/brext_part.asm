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
%if ($ - $$) != BRTB_ENTRY + 2
%error "part 7's entry table does not match brcolor_const.inc / brfont_const.inc"
%endif

brt_f_table:
    call br_table
    retf

%include "brtable.inc"
%include "brcolor_body.inc"
%include "brfont_body.inc"
