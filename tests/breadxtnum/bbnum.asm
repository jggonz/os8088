; Same numeric bridge as shipping Breadboard XT, under actual C calling ABI.
%define CC_PKG_NAME 'BBNUM'
%define CC_HAS_ONKEY
%define CC_HAS_ONWAKE
%define OS88UI_ARM
%define OS88UI_ALERT
%define OS88UI_NOGEST
%include "cc/crt0.asm"
%include "bbnum.gen.asm"
%include "os88ui.inc"
%include "breadxt/draw.inc"
%include "breadxt/numeric.inc"
CC_IMAGE_END
