; =============================================================================
; os8088 - tests/cfsx/cfsx.asm
;
; The assembly shim of the C EXCLUSIVE-BRACKET capability gate (SPEC.md 53,
; reached from C per SPEC.md 92.2). Same four jobs as every other shim - name
; the package, declare the callbacks, include the runtime and then the
; compiled C, close the image - plus the two %defines this gate exists for and
; the one file of hand-written drawing.
;
; CC_HAS_FSX is what turns the bracket on: os88thunk.asm declares the seven
; OSAPI_FSX_* bridges and crt0.asm the cc_fsxentry trampoline only when it is
; defined, so a C package that never borrows the machine assembles byte for
; byte as it did before the bracket was wrapped. The %define and the C cannot
; drift - a %define with no `void os88_fsx_main(void *)` behind it is an nasm
; error naming that function, and the C with no %define is a function the
; kernel never calls.
;
; CC_HAS_OVL is tests/covl's mechanism, here for a reason that gate could not
; test: the module is far-called from INSIDE the bracket, which is the case
; SPEC.md 92.5 is a rule about.
; =============================================================================

%define CC_PKG_NAME 'CFSX'          ; <= 15 chars, and the stem of CFSX.OVL:
                                    ; crt0.asm builds the module's file name
                                    ; out of this and '.OVL', so the disk and
                                    ; the loader cannot disagree about it

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *)
%define CC_HAS_FSX                  ; void os88_fsx_main(void *) - THE BRACKET
%define CC_HAS_OVL                  ; ...and it far-calls a module from inside

%include "cc/crt0.asm"

%include "cfsx.gen.asm"             ; the compiled C, found through -I build/

; The drawing. EVERY kernel drawing slot is refused inside the bracket
; (SPEC.md 53.1), so the pixels are written into the framebuffer segment the
; FSI block names and the CRTC and Attribute Controller are talked to by port
; - neither of which C can do (SPEC.md 73.11's rule that the inner loop is
; never C, here for the harder reason that there is no C for `out dx, al`).
; The C calls these once per bar and once per frame, never per pixel.
%include "cfsxvga.inc"

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end
