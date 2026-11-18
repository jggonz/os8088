; The game kit's gallery (SPEC.md 133): a C gate package that draws every
; widget of apps/cc/gk.inc and one compiled scene, so the kit is LOOKED at
; on each adapter. Not shipped; `make gkdemo` builds build/gkdemo.img.
%define CC_PKG_NAME 'GKDEMO'
%define CC_HAS_ONKEY
%define CC_HAS_ONMOVE
%define CC_HAS_PARTS
%include "cc/crt0.asm"
    CC_PARTS_BEGIN 1
      OS88_PART OP_ASSET, OP_COMP                 ; 0 GKDART.BIN, eager
    CC_PARTS_END
%include "gkdemo.gen.asm"
%define BX_CORE_ONLY
%include "breadxt/draw.inc"
%include "cc/gk.inc"
CC_IMAGE_END
