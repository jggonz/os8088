; Borat Teaches Typing, native C package (SPEC.md 134).
%define OS88UI_ABOUT
%define CC_PKG_NAME 'BORATYPE'
%define CC_ICON "boratype/icon.inc"
%define CC_STACK_CLASS OS88_HIGH | OS88_HIGHMEM
%define CC_HAS_ONKEY
%define CC_HAS_ONCLICK
%define CC_HAS_ONRESIZE
%define CC_HAS_MENUS
%define CC_HAS_ABOUT
%define CC_HAS_ONCLOSE
%define CC_HAS_ONTIMER
%define CC_HAS_ONWAKE
%define CC_HAS_ONMOVE
%define CC_HAS_WORKER
%define CC_HAS_FSX
%define CC_HAS_PARTS
%define CC_ONMOVE_PARTS
%define CC_HAS_OVL
%define CC_PARTS_AFTER_OVL 5
%define CC_OVL_PART
%include "cc/crt0.asm"
    CC_PARTS_BEGIN 8
      OS88_PART OP_ASSET, OP_COMP
      OS88_PART OP_ASSET, OP_COMP
      CC_OVL_ROW
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP
    CC_PARTS_END
%include "boratype.gen.asm"
%include "os88ui.inc"
%define BX_CORE_ONLY
%define BX_ART2
%define BX_TXID 500
%include "breadxt/draw.inc"
%include "cc/gk.inc"
%include "cc/gkmus.inc"
%include "boratype/bt_music.inc"
%include "boratype/bt_native.inc"
CC_IMAGE_END
