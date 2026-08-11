; =============================================================================
; os8088 - tests/xmtest/xmtest.asm
;
; THE EXTENDED-MEMORY TEARDOWN GATE (SPEC.md 41.5, 29.4).
;
; It answers one question that nothing else in the tree can: when an instance
; holding blocks above 1MB goes away, are those blocks freed?
;
; That question needs a package for a structural reason. xm_alloc stamps a
; block with the CALLING INSTANCE (xm_owner -> snd_req_inst), and only a real
; dispatched callback has one - so a block cannot be made to belong to an
; instance from the debugger, from the kernel, or from a script. Something has
; to BE the instance, hold the blocks, and then close.
;
; It exists because the release was WIRED, then silently UNWIRED, and nobody
; noticed for a year: xm_release_rec had three call sites in instance.inc
; until the #51 integration merge dropped all three, and the comment
; describing them outlived the code (docs/UPSTREAM.md's own hazard - a call
; that is simply absent breaks no build). It cost nothing only because the
; single consumer that ever existed, SPEC.md 53.6.1's fullscreen desktop
; stash, was removed. This gate is what makes the next such loss loud.
;
; HOW TO RUN IT, and it must be a machine with a store: QEMU on a 386 is the
; one to hand (SPEC.md 41.7 - MartyPC's 8088 can never have extended memory,
; so on the target machine this package correctly does nothing at all and says
; so). tests/xmcheck.py drives the whole thing and reads the verdict out of
; the kernel's own block table:
;
;     make test TESTAPPS=build/xmtest.img
;     python3 tests/xmcheck.py build/qmp.sock
;
; The package itself is deliberately dumb - it claims, it draws what it got,
; and it closes like any other window. THE ASSERTION IS OUTSIDE IT, in
; xm_tab, because a package asking the kernel "did you free my blocks?" is the
; writer and the reader being the same code, which is the failure this whole
; area has already had once (docs/FIELD-NOTES.md 4's shape).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'XMTEST', xt_entry

XT_CONT_W  equ 238                  ; content width: 240 outer - 2px borders
XT_CONT_H  equ 71                   ; content height: 90 outer - TITLE_H - 1

XT_BLKS    equ 3                    ; blocks claimed, and THREE rather than one
                                    ; on purpose: xm_release_inst walks the
                                    ; whole table and frees EVERY entry
                                    ; matching the slot, so one block would
                                    ; pass a release that stopped at the first
XT_KB      equ 4                    ; KB per block - small, so the gate runs on
                                    ; a 1MB AT as well as a fat one

; -----------------------------------------------------------------------------
; xt_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; THE CLAIMS ARE MADE HERE, IN THE ENTRY PROC, and that is the sharp end of
; this gate rather than the convenient place. The entry proc used to have NO
; INSTANCE IDENTITY: inst_caller (SPEC.md 34.3, which xm_owner goes through)
; answers with the dispatched callback's stamp or else the RUNNING TASK's
; T_INST, and the loader far-calls the entry on the UI task, whose T_INST is
; 0xFF - so a block claimed here came back stamped XM_OWN_KERN and no teardown
; could ever free it. This gate is what found that (measured: owner 0xFF from
; here, owner 0x01 from W_PAINT), and SPEC.md 41.5.1 is the fix: the loader
; now brackets the entry call with the same snd_disp_set stamp every other
; dispatch site uses.
;
; So claiming HERE tests two things at once - that the entry proc is
; identified, and that its blocks are released - and claiming from a callback
; would test only the second. The entry proc is also where an app naturally
; sizes itself, which is what made the gap worth closing.
; -----------------------------------------------------------------------------
xt_entry:
    push ax
    push cx
    push dx
    push si
    push di

    mov si, xt_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out
    call xt_claim                   ; ...and the blocks, as this instance
    clc                             ; (xt_claim's own CF is not ours to return)
.out:
    pop di                          ; BX is NOT restored: it is the output
    pop si                          ; (wm_create left the window ptr in it) and
    pop dx                          ; pop does not disturb the CF we return
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; xt_paint - W_PAINT: what we hold, so a human can see the same thing the
;            checker reads out of the kernel
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
xt_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [xt_cx], ax
    mov [xt_cy], dx

    mov al, CWHITE                  ; erase the content
    call OSAPI_SET_COLOR
    mov ax, [xt_cx]
    mov bx, [xt_cy]
    mov cx, ax
    add cx, XT_CONT_W - 1
    mov dx, bx
    add dx, XT_CONT_H - 1
    call OSAPI_GFX_FILL

    mov al, CBLACK
    call OSAPI_SET_COLOR

    mov al, [xt_got]                ; 'XMS blocks held: ' + one digit, at a
    add al, '0'                     ; FIXED pen rather than a measured one -
    mov [xt_s_n], al                ; the label is 17 cells of an 8px font, so
                                    ; the digit's x is arithmetic, and a test
                                    ; package has no business calling
                                    ; font_width to place two strings
    mov cx, [xt_cx]
    add cx, 8
    mov dx, [xt_cy]
    add dx, 8
    mov si, xt_s_held
    call OSAPI_FONT_STR

    mov cx, [xt_cx]
    add cx, 8 + 17*8
    mov dx, [xt_cy]
    add dx, 8
    mov si, xt_s_n
    call OSAPI_FONT_STR

    mov cx, [xt_cx]
    add cx, 8
    mov dx, [xt_cy]
    add dx, 24
    mov si, xt_s_close
    cmp byte [xt_got], 0
    jne .say
    mov si, xt_s_nostore
.say:
    call OSAPI_FONT_STR

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; xt_claim - take XT_BLKS blocks above 1MB, ONCE, from the entry proc
; in:  called from xt_entry, which the loader now stamps (SPEC.md 41.5.1)
; out: [xt_got] = how many were granted; preserves all registers (flags)
;
; THREE blocks rather than one, deliberately: xm_release_inst walks the whole
; table and frees EVERY entry matching the slot, so a single block would pass
; a release that stopped at the first match.
; -----------------------------------------------------------------------------
xt_claim:
    push ax
    push dx
    push si
    push di
    cmp byte [xt_done], 0
    jne .out
    mov byte [xt_done], 1
    xor di, di                      ; DI = block index
.claim:
    mov ax, XT_KB * 1024            ; DX:AX = bytes wanted
    xor dx, dx
    call OSAPI_XMEM_ALLOC           ; CF=1 and AX = 0 no store / 1 no entry /
    jc .out                         ; 2 no run that big. No store is not a
    mov si, di                      ; FAILURE here - it is the target machine,
    shl si, 1                       ; and the window says so; only xmcheck.py
    shl si, 1                       ; judges
    mov [xt_base + si], ax          ; the 32-bit linear base it handed us
    mov [xt_base + si + 2], dx
    inc byte [xt_got]
    inc di
    cmp di, XT_BLKS
    jb .claim
.out:
    pop di
    pop si
    pop dx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) -------------------------
xt_tpl:
    dw 180, 120, 240, 90            ; x, y, w, h -> content 238 x 71
    dw xt_ttl, xt_paint, 0, 0       ; no onkey, no onclick

xt_ttl:      db 'XmTest', 0
xt_s_held:   db 'XMS blocks held: ', 0
xt_s_n:      db '0', 0
xt_s_close:  db 'Close me, then check.', 0
xt_s_nostore: db 'No store: nothing to test.', 0

    OS88_BSS 20
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
xt_got      equ os88_image_end + 0   ; byte: blocks actually claimed
xt_base     equ os88_image_end + 2   ; XT_BLKS 32-bit bases, for a human
xt_cx       equ os88_image_end + 14  ; the content origin, banked once a paint
xt_cy       equ os88_image_end + 16  ; (the window moves)
xt_done     equ os88_image_end + 18  ; byte: the claims are made once
