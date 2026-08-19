; =============================================================================
; os8088 - tests/drvcall/drvcall.asm
;
; DRVCALL: the gate for OSAPI_DRV_CALL (SPEC.md 20.11) - can a PACKAGE reach
; a DRIVER at all, and does the driver get the package's own segment in ES?
;
; It is docs/NET-STACK-PLAN.md stage A's proof, and it is deliberately not
; about networking: the door and the thing behind it are separable, so the
; door is tested first, in a container, with no cable and no card. RAMDISK.DRV
; publishes two verbs for exactly this (drivers/ramdisk/rdpkg.inc) and it is
; ONE OF TWO drivers that publish one - NET.DRV is the other (SPEC.md 62.11),
; and both are DRVC_FILE, which is exactly why verb 0 is IDENTIFY. HDD.DRV
; leaves DSV_PKGCALL
; at 0, which is the fence, and this package proves that too.
;
; Three lines, redrawn on every paint and on every click, so the harness can
; look at the SAME window before and after the Control Panel's Drivers page
; has the Ram Disk ticked:
;
;   Ping: DR      RDPV_IDENT answered 'DR' - we reached A DRIVER. '--' means
;                 the kernel refused: no DRVC_FILE driver published, or the
;                 one that is publishes no DSV_PKGCALL
;   Upcase: ...   RDPV_UPCASE was handed ES:SI into OUR OWN SEGMENT and wrote
;                 through it. This is the half nothing else can test: every
;                 other driver entry point in the machine is handed
;                 KERNEL_SEG in ES and could not have touched these bytes
;   Bad verb:     an RDPV_* the driver does not have must be REFUSED, not
;                 dispatched. A verb table indexed by an unchecked verb is a
;                 wild near call inside the driver
;
; NEVER SHIPPED. Its own scratch image, the fmtest precedent:
;   make drvcalltest && python3 tests/drvcall.py
;
; Every proc here is an ordinary near proc with a near `ret`: the kernel
; reaches a callback through the three-byte dispatcher in the package's own
; header (SPEC.md 20.1), and a `retf` returns into the loader's stack frame.
; =============================================================================

%include "os88api.inc"
%include "rdpkg.inc"                ; ...THE DRIVER'S OWN HEADER, the same file
                                    ; drivers/ramdisk/ramdisk.asm includes. A
                                    ; driver ABI belongs beside the driver, and
                                    ; both ends including one file is what
                                    ; stops the two drifting (SPEC.md 20.11)

    OS88_HEADER 'DRVCALL', dc_entry

DC_BADVERB  equ 7                   ; well past RDPV_MAX, and not a verb the
                                    ; driver will ever have: the refusal is the
                                    ; assertion
DC_CONT_W   equ 278                 ; 280 outer - 2px borders
DC_CONT_H   equ 59                  ; 78 outer - TITLE_H - 1px border

; -----------------------------------------------------------------------------
; dc_entry - package entry (SPEC.md 20.2)
; in:  CS=DS=ES = our segment, gfx lock NOT held
; out: BX = window, CF=1 aborts
; -----------------------------------------------------------------------------
dc_entry:
    push si
    mov si, dc_tpl
    call OSAPI_WM_CREATE
    pop si
    ret

; -----------------------------------------------------------------------------
; dc_probe - run all three checks and rewrite the three lines
; in:  nothing. Legal from a callback: the cell takes no lock and touches no
;      disk, and both verbs are register work in the driver's own segment
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
dc_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es                         ; ES IS KERNEL_SEG ON ENTRY TO EVERY
                                    ; CALLBACK (SPEC.md 20.1) and the caller
                                    ; may still need it - this routine borrows
                                    ; it twice for a `rep movsb` into its own
                                    ; data, which `stos`/`movs` can only reach
                                    ; through ES

    ; --- 1: WHO is there ----------------------------------------------------
    mov bh, RD_CLASS
    mov bl, RDPV_IDENT
    call OSAPI_DRV_CALL
    jnc .pong
    mov ax, '--'                    ; the kernel's refusal, and the ONLY thing
                                    ; a package can tell from CF alone
.pong:
    mov [dc_r1+6], ax               ; AL first: NASM's 'DR' is 'D' in the low
                                    ; byte, so this stores D then R.
                                    ;
                                    ; RDPV_IDENT is verb 0 of EVERY package
                                    ; door (SPEC.md 20.11.1) and this line is
                                    ; the whole reason it exists: NET.DRV is
                                    ; DRVC_FILE as well, so the class alone
                                    ; does not say whose verbs are behind it

    ; --- 2: whose segment did the driver get? -------------------------------
    ; Reset the subject from a pristine copy first: upper-casing is idempotent
    ; and a second paint would otherwise report a pass it did not earn.
    push ds
    pop es                          ; `movs` writes through ES and the string
    cld                             ; is OURS - without this it lands in the
    mov si, dc_src                  ; KERNEL's segment, which is the trap
    mov di, dc_r2 + 8               ; apps/modplug shipped with
    mov cx, DC_SUBJ
    rep movsb

    mov si, dc_r2 + 8
    mov cx, DC_SUBJ
    mov bh, RD_CLASS
    mov bl, RDPV_UPCASE
    call OSAPI_DRV_CALL
    jnc .upok
    xor ax, ax                      ; refused: report a length of 0, which the
.upok:                              ; harness reads as "the driver never ran"
    mov di, dc_r2 + 8 + DC_SUBJ + 3
    call dc_dec2

    ; --- 3: the fence inside the driver -------------------------------------
    mov bh, RD_CLASS
    mov bl, DC_BADVERB
    call OSAPI_DRV_CALL
    mov si, dc_s_ref
    jc .badok
    mov si, dc_s_took               ; it DISPATCHED an unchecked verb
.badok:
    mov di, dc_r3 + 10
    mov cx, DC_VERD
    push ds
    pop es
    cld
    rep movsb

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- dc_dec2 - AX (0..99) as two ASCII digits at DS:DI ----------------------
dc_dec2:
    push ax
    push bx
    push dx
    xor dx, dx
    mov bx, 10
    div bx                          ; AX = tens, DX = units
    add al, '0'
    add dl, '0'
    mov [di], al
    mov [di+1], dl
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_paint - W_PAINT: probe, then letter the three lines
; in:  SI = window; lock held, content already white
; -----------------------------------------------------------------------------
dc_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    call dc_probe
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 10
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, dc_r1
    call OSAPI_FONT_STR
    add dx, 14
    mov si, dc_r2
    call OSAPI_FONT_STR
    add dx, 14
    mov si, dc_r3
    call OSAPI_FONT_STR
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- a click re-probes: the driver may have been ticked since the last paint.
; White-fill our own content and letter it again - the files.inc idiom, and
; the lock is already held (SPEC.md 20.1).
dc_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov bx, dx
    mov cx, ax
    add cx, DC_CONT_W-1
    add dx, DC_CONT_H-1
    call OSAPI_GFX_FILL
    call dc_paint                   ; NEAR: SI is still the window
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dc_onkey:
    ret

DC_SUBJ     equ 11                  ; 'hello world'
DC_VERD     equ 8                   ; 'refused ' / 'DISPATCH'

; --- window template (SPEC.md 11) --------------------------------------------
dc_tpl:
    dw 200, 200, 280, 78
    dw dc_ttl, dc_paint, dc_onkey, dc_onclick

dc_ttl:     db 'Driver Call', 0
dc_src:     db 'hello world'        ; the pristine subject, copied in per probe
dc_s_ref:   db 'refused '
dc_s_took:  db 'DISPATCH'

dc_r1:      db 'Ping: ..', 0        ; [+6..7]
dc_r2:      db 'Upcase: hello world n=..', 0   ; [+8..] and [+22..23]
dc_r3:      db 'Bad verb: ........', 0         ; [+10..17]

    OS88_IMAGE_END
