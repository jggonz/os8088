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
%assign CC_PARTS_AFTER_OVL 1      ; BPART.BIN follows the overlay
%include "cc/crt0.asm"
; The overlay first and eager; the art after it and LAZY, fetched by
; os88_main when it fits (SPEC.md 131.10), as Breadboard XT does.
    CC_PARTS_BEGIN 2
      CC_OVL_ROW                                   ; 0 the overlay
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP        ; 1 BPART.BIN
    CC_PARTS_END
%include "byteoply.gen.asm"
%include "os88ui.inc"
%define BX_CORE_ONLY                ; the band core without BREADXT's own tail
%include "breadxt/draw.inc"
%include "byteopoly/bp_cable.inc"        ; link lane: the cable bridge
%include "byteopoly/bp_app.inc"          ; app lane: the alert bridge
%include "cc/gkmus.inc"                  ; sound lane: the kit's music and effects (132.8)
CC_IMAGE_END
