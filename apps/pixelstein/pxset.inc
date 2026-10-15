; =============================================================================
; os8088 - apps/pixelstein/pxset.inc
;
; THE SETTINGS, KEPT (SPEC.md 97.8): the eight bytes the menus edit -
; Detail, Resolution, Size, Rows, the Mode item, Sound and Mouse (the last two
; wave 4's) and Colour (wave 5's, 97.14) - a file in SYSTEM\APPDATA, read once in the ENTRY PROC and
; written when a menu pick or the V key changes one.
;
; The shape is apps/skies/csset.inc (SPEC.md 88.13.9), which is apps/tank's
; tkhs.inc, which is apps/cyclone's, and its two hard-won rules are repeated
; here because getting either wrong fails QUIETLY:
;
;  - **The file lives in SYSTEM\APPDATA on OUR OWN volume** (SPEC.md 19.9),
;    not beside the package.
;  - **The folder is reached through apps/os88data.inc**, which walks with
;    OSAPI_FILE_GOTO_QM - the quiet stand that MOVES THE INSTANCE, so the
;    file cell after it resolves there - banks APPDATA's cluster, and
;    rewrites a file of unchanged length with one sector.
;
; This was the FIFTH copy of an eighty-line walker built out of
; OSAPI_FILE_GOTO, a remount for DISPLAY at every step; it is the shared
; include now, in all five packages (docs/plans/NAV-COST-PLAN.md).
;
; WHEN IT SAVES: on the UI task, from the menu handler and from the V key in
; a window; a V key inside the bracket banks px_cfgdirty and the file is
; written when the bracket is left (px_go_fsx), so no floppy is spun in a
; foreign mode. A file that matches the magic and holds nonsense is CLAMPED
; and not refused (px_set_max): a rung out of range is one byte of a file this
; program wrote.
;
; A WRITE THAT FAILED IS NOT REMEMBERED AS ONE THAT LANDED (review, wave 2):
; the record is built in px_setnew and copied into px_setbuf - whose magic IS
; the claim "this is what the disk holds" - only after od_write
; answers CF=0, and px_cfgdirty is cleared on that same answer. The first
; cut filled px_setbuf first, so one refused directory or one write-protected
; floppy made every later save of the same settings take the nothing-changed
; early-out, and the file was never written again that session.
; =============================================================================

PX_SETFSZ  equ 4 + 8                ; the magic and the eight bytes
px_set_file:  db 'PXSTEIN.CFG', 0
px_set_magic: db 'PXC', 2           ; so a stale or foreign file is refused
                                    ; (2: wave 5's eighth byte, Colour - a
                                    ; seven-byte file of 1 is refused whole
                                    ; and the defaults stand, once)
                                    ; rather than decoded
px_set_ip:    db 0                  ; os88data.inc's in-place stamps:
px_hs_ip:     db 0                  ; PXSTEIN.CFG, and pxhs.inc's PXSTEIN.HS

; What each byte is, and the largest value it may hold.
px_set_at:  dw px_detail, px_res, px_sizeix, px_rowsix, px_modepick, px_sound, px_mouse
            dw px_colour
px_set_max: db PXD_TEX, 1, PX_NSIZES - 1, 0, 1, 1, 1, 1
PX_SETN     equ 8

%include "os88data.inc"

; -----------------------------------------------------------------------------
; px_set_load - read the eight bytes once. Anything wrong with the file leaves
;               the built-in defaults standing (px_auto_init's): a settings
;               file is not worth an error box, and the first run has no
;               file at all. UI TASK ONLY - the entry proc. The Mode item is
;               applied against what THIS display offers (px_adapter ran)
; -----------------------------------------------------------------------------
px_set_load:
    cmp byte [px_setread], 0
    jne .out
    mov byte [px_setread], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov si, px_set_file
    mov bx, px_setbuf
    mov cx, PX_SETFSZ
    xor dx, dx
    call od_read                    ; ...and we stand where we started again,
    jc .bad                         ; whatever the read answered
    or dx, dx
    jnz .bad
    cmp ax, PX_SETFSZ
    jne .bad
    mov si, px_setbuf
    mov di, px_set_magic
    mov cx, 4
.mag:
    mov al, [si]
    cmp al, [di]
    jne .bad
    inc si
    inc di
    loop .mag
    xor bx, bx                      ; SI is the fifth byte of the buffer
.each:
    mov al, [si]
    cmp al, [px_set_max + bx]       ; ...clamped rather than refused
    jbe .ok
    mov al, [px_set_max + bx]
.ok:
    push bx
    shl bx, 1
    mov di, [px_set_at + bx]
    mov [di], al
    pop bx
    inc si
    inc bx
    cmp bx, PX_SETN
    jb .each
    ; the Mode item: the second one only if this display has it
    mov bl, [px_mode0]
    cmp byte [px_modepick], 0
    je .m
    mov bl, [px_mode1]
    or bl, bl
    jnz .m
    mov byte [px_modepick], 0
    mov bl, [px_mode0]
.m:
    mov [px_mode], bl
    call px_adapter
    jmp short .done
.bad:
    ; A REJECTED RECORD IS NOT "WHAT THE DISK HOLDS": the read landed in
    ; px_setbuf and px_set_save's nothing-changed test reads the magic there
    ; - a short file that begins 'PXC',1 (what a disk-full write leaves)
    ; over a zeroed tail could compare equal to the live picks and skip the
    ; write that would have fixed it (review, wave 2). One byte off the
    ; magic and the next save writes
    mov byte [px_setbuf], 0
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; px_set_save - write them back. UI TASK ONLY, which is where every W_
;               handler runs and where the bracket's own loop runs.
; -----------------------------------------------------------------------------
px_set_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    ; --- NOTHING CHANGED, NOTHING WRITTEN: px_setbuf holds what was last
    ;     read or WRITTEN AND ACKNOWLEDGED, and the MAGIC being in it is
    ;     what says so
    mov si, px_setbuf
    mov di, px_set_magic
    mov cx, 4
.same:
    mov al, [si]
    cmp al, [di]
    jne .differs
    inc si
    inc di
    loop .same
    xor bx, bx
.sameb:
    push bx
    shl bx, 1
    mov di, [px_set_at + bx]
    pop bx
    mov al, [di]
    cmp al, [si]
    jne .differs
    inc si
    inc bx
    cmp bx, PX_SETN
    jb .sameb
    mov byte [px_cfgdirty], 0       ; byte for byte what is on the disk
    jmp .gone
.differs:
    mov si, px_set_magic            ; the record, built BESIDE the claim
    mov di, px_setnew
    mov cx, 4
.mag:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .mag
    xor bx, bx
.each:
    push bx
    shl bx, 1
    mov si, [px_set_at + bx]
    pop bx
    mov al, [si]
    mov [di], al
    inc di
    inc bx
    cmp bx, PX_SETN
    jb .each
    mov si, px_set_file
    mov bx, px_setnew
    mov cx, PX_SETFSZ
    mov di, px_set_ip
    call od_write                   ; one sector, after the session's first
    jc .gone                        ; no folder, refused or short: px_setbuf
                                    ; and px_cfgdirty stand, and the next
                                    ; save tries again
    mov si, px_setnew               ; it landed: THIS is what the disk holds
    mov di, px_setbuf
    mov cx, PX_SETFSZ
    cld
    rep movsb
    mov byte [px_cfgdirty], 0
.gone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
