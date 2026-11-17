; Byteopoly's two-word arithmetic (SPEC.md 131.4.1) executed on the 8088:
; the SHIPPING apps/byteopoly/bp_math.c under the actual C calling ABI, so
; the host harness's integer promotion cannot hide a defect the guest has.
%define CC_PKG_NAME 'BPMATH'
%define CC_HAS_ONKEY
%define CC_HAS_ONWAKE
%include "cc/crt0.asm"
%include "bpmath.gen.asm"
CC_IMAGE_END
