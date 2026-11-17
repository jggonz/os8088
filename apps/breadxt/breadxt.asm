; Breadboard XT native C package. SPEC.md 130. Original source/artwork.
%define OS88UI_ARM
%define OS88UI_ALERT
%define OS88UI_ABOUT
%define OS88UI_NOGEST
%define CC_PKG_NAME 'BREADXT'
%define CC_ICON "breadxt/icon.inc"
%define CC_ASSOC "breadxt/assoc.inc"
%define CC_STACK_CLASS OS88_HIGH | OS88_HIGHMEM
%define CC_HAS_ONKEY
%define CC_HAS_ONCLICK
%define CC_HAS_ONMOUSEUP
%define CC_HAS_ONRESIZE
%define CC_HAS_MENUS
%define CC_HAS_ABOUT
%define CC_HAS_FDLG
%define CC_HAS_ONWAKE
%define CC_HAS_ONCLOSE
%define CC_HAS_ONTIMER
%define CC_HAS_ONMOVE
%define CC_HAS_ONWHEEL
%define CC_HAS_OVL
%define CC_OVL_PART
%assign CC_PARTS_AFTER_OVL 1      ; BBART.BIN follows the overlay
%include "cc/crt0.asm"
; The overlay first and eager; the face's art after it and LAZY, fetched by
; os88_main only when it fits (SPEC.md 130.6.3). Every eager row precedes
; every lazy one; the Makefile passes BBART.BIN after the cut module.
    CC_PARTS_BEGIN 2
      CC_OVL_ROW                                   ; 0 the overlay
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP        ; 1 BBART.BIN
    CC_PARTS_END
%include "breadxt.gen.asm"
%include "os88ui.inc"
%include "breadxt/draw.inc"
%include "breadxt/numeric.inc"
CC_IMAGE_END
