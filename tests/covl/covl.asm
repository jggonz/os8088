; =============================================================================
; os8088 - tests/covl/covl.asm
;
; The assembly shim of the C OVERLAY capability gate (SPEC.md 67.14). Same
; four jobs as every other shim - name the package, declare the callbacks,
; include the runtime and then the compiled C, close the image - plus the one
; line this gate exists for.
;
; CC_HAS_OVL is what turns the mechanism on: crt0.asm declares the .modc
; section, the loader, the stamp check and the binder only when it is defined,
; so a package that does not use overlays assembles byte for byte as it did
; before they existed. The compiled C refuses to assemble WITHOUT it if it has
; any ovl_* function - tools/cc8086.py emits a %error saying so - which is the
; same "the two halves cannot drift silently" property the callback %defines
; already have.
; =============================================================================

%define CC_PKG_NAME 'COVL'          ; <= 15 chars, and the stem of COVL.OVL:
                                    ; crt0.asm builds the module's file name
                                    ; out of this and '.OVL', so the disk and
                                    ; the loader cannot disagree about it

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *)
%define CC_HAS_OVL                  ; ...and this package has ovl_* functions

%include "cc/crt0.asm"

%include "covl.gen.asm"             ; the compiled C, found through -I build/

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end
