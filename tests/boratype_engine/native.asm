cpu 8086
; Real runtime/overlay compile fixture, never shipped as an application.
%define CC_PKG_NAME 'BTENGTEST'
%define CC_HAS_OVL
%define CC_OVL_PART
%include "cc/crt0.asm"
%include "bt-engine.gen.asm"
CC_IMAGE_END
