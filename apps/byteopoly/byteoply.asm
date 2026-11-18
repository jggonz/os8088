; Byteopoly native C package (SPEC.md 131). Original source and artwork.
; The shim does what SPEC.md 73.1 allows and no more: names the package,
; declares the callbacks the C defines, lays out the parts, and includes the
; runtime, the compiled C and the shared assembly it calls. Lead-owned.
%define OS88UI_ARM
%define OS88UI_ALERT
%define OS88UI_ABOUT
%define OS88UI_NOGEST
%define CC_PKG_NAME 'BYTEOPOLY'
%define CC_ICON "byteopoly/icon.inc"
%define CC_STACK_CLASS OS88_HIGH | OS88_HIGHMEM
%define CC_HAS_ONKEY
%define CC_HAS_ONCLICK
%define CC_HAS_ONRESIZE
%define CC_HAS_MENUS
%define CC_HAS_ABOUT
%define CC_HAS_ONCLOSE
%define CC_HAS_ONTIMER
%define CC_HAS_ONMOVE
%define CC_HAS_FDLG
%define CC_HAS_OVL
%define CC_OVL_PART
%assign CC_PARTS_AFTER_OVL 2      ; BPART.BIN and BPART2.BIN follow the overlay
%include "cc/crt0.asm"
; The overlay first and eager; the two art parts after it and LAZY, each its
; own claim, both fetched by os88_main and REQUIRED (SPEC.md 131.10.7).
    CC_PARTS_BEGIN 3
      CC_OVL_ROW                                   ; 0 the overlay
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP        ; 1 BPART.BIN
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP        ; 2 BPART2.BIN (art2.h, 256..)
    CC_PARTS_END
%include "byteoply.gen.asm"
%include "os88ui.inc"
%define BX_CORE_ONLY                ; the band core without BREADXT's own tail
%define BX_ART2                     ; art IDs 256.. in BPART2.BIN, indexed text,
%define BX_TXID 460                 ; text handles from BP_A2_TX_APP (131.10.7)
%include "breadxt/draw.inc"
%include "cc/gk.inc"                     ; kit lane: the 3D widgets and scenes (SPEC.md 133)
%include "byteopoly/bp_cable.inc"        ; link lane: the cable bridge
%include "byteopoly/bp_app.inc"          ; app lane: the alert bridge
%include "cc/gkmus.inc"                  ; sound lane: the kit's music and effects (133.8)
CC_IMAGE_END
