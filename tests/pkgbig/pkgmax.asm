; =============================================================================
; PKGMAX - a package at EXACTLY APP_MAX_SIZE (SPEC.md 20.1.1)
;
; NOT SHIPPED SOFTWARE: tests/pkgmax.py's fixture, on build/pkgbig.img.
;
; image + bss is APP_MAX_SIZE to the byte, so the region is 0xFE00 bytes and
; the claim behind it is a whole 64KB - the one size two 16-bit words in the
; loader used to wrap on: mem_bytes_kb_x answered 0 KB for it, and I_SIZE,
; which is KB << 10, read 0, which is how the instance table says BUILT-IN.
;
; The entry proc leaves a mark in the LAST byte of its bss, so the row can
; show the region really reaches 0xFDFF and was zeroed on the way in: it
; writes PM_MARK there only if the loader left a 0.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PKGMAX', pm_entry

PM_LAST equ APP_MAX_SIZE - 1    ; the last byte the region holds
PM_MARK equ 0xA5

pm_entry:
    push si
    cmp byte [PM_LAST], 0       ; step 7 zeroed the whole bss...
    jne .nomark
    mov byte [PM_LAST], PM_MARK ; ...and this is the proof it reached the top
.nomark:
    mov si, pm_tpl
    call OSAPI_WM_CREATE        ; BX = window ptr, CF on table full
    jc .out
    OS88_REGION_MOVABLE
.out:
    pop si
    ret

pm_paint:                       ; W_PAINT arrives white-filled: nothing to add
    ret

pm_tpl:
    dw 200, 150, 160, 60
    dw pm_ttl, pm_paint, 0, 0

pm_ttl: db 'PkgMax', 0

pm_end:
    OS88_BSS APP_MAX_SIZE - (pm_end - $$)
    OS88_IMAGE_END

%if OS88_IMAGE_SIZE + OS88_BSS_SIZE != APP_MAX_SIZE
  %error "PKGMAX must be exactly APP_MAX_SIZE - that size is its whole subject"
%endif
