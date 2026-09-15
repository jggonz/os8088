; Fixed shim for a standalone program emitted by speedybasic_compile.py.
;
; The Make rule supplies SB_COMPILED_NAME and SB_COMPILED_GEN.  Keeping the
; package header here means generated C cannot accidentally reproduce or
; drift from the C SDK's v3 header/callback trampolines.

%ifndef SB_COMPILED_NAME
  %define SB_COMPILED_NAME 'SBHELLO'
%endif
%ifndef SB_COMPILED_GEN
  %define SB_COMPILED_GEN "sbhello.gen.asm"
%endif

%define CC_PKG_NAME SB_COMPILED_NAME
%define CC_HAS_ONKEY
%define CC_HAS_ONRESIZE
%define CC_HAS_ONWAKE
%define CC_HAS_ONTIMER
%define CC_ICON "speedybasic/icon.inc"

%include "cc/crt0.asm"
%include SB_COMPILED_GEN
%include "speedybasic/compiler/gfx.inc"
%define SBN_FLAT
%include "speedybasic/sbnum.inc"
%include "speedybasic/sbmem.inc"

    CC_IMAGE_END
