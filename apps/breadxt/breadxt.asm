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
%define CC_HAS_ONWHEEL
%define CC_HAS_OVL
%define CC_OVL_PART
%include "cc/crt0.asm"
%include "breadxt.gen.asm"
%include "os88ui.inc"
%include "breadxt/draw.inc"
%include "breadxt/numeric.inc"
CC_IMAGE_END
