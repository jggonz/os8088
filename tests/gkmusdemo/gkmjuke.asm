; gkmjuke - the game kit's music player as a jukebox (SPEC.md 133.8). A gate
; package (tests/), not shipped. Its one part is the art part that holds the
; two banks (IDs 0 and 1), written by `tools/os88gkmus.py part`.
%define CC_PKG_NAME 'GKMJUKE'
%define CC_HAS_ONKEY
%define CC_HAS_ONCLOSE
%define CC_HAS_ONTIMER
%define CC_HAS_PARTS                ; one art part: the two banks
%include "cc/crt0.asm"
    CC_PARTS_BEGIN 1
      OS88_PART OP_ASSET                           ; 0 GKMBANKS.BIN, the banks
    CC_PARTS_END
%include "gkmjuke.gen.asm"
%define BX_CORE_ONLY                ; the band core: its art lookup, bx_art_at
%include "breadxt/draw.inc"
%include "cc/gkmus.inc"
CC_IMAGE_END
