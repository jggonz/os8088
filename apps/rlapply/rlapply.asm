; =============================================================================
; os8088 - apps/rlapply/rlapply.asm
;
; RLAPPLY - put REDLINE's saved chipset setting back (SPEC.md 103.6.5). A
; package with NO WINDOW: it reads SYSTEM\APPDATA\REDLINE.CFG on the boot
; disk, checks it against this machine's BIOS fingerprint, writes it, says
; what it did in one toast, and ends. Nothing in a chipset survives a reset,
; so this is what is run after every boot - by hand until an autostart can
; launch it (docs/plans/REDLINE-TUNE-PLAN.md).
;
; It is a SECOND PROGRAM rather than a mode of REDLINE because of where it
; has to live: in SYSTEM/ on the system disk, beside what the machine boots
; from, where REDLINE (28KB and its benches) has no business being. What the
; two share they share as SOURCE (WEAVE-SPEC 1.2's rule): the row table and
; the fingerprint (csrows.inc), the knobs (csfe.inc) and the file and the
; apply (csapply.inc), all assembled CS_APPLY_ONLY - no probe, no sweep, no
; port touched before the fingerprint has said the setting is this board's.
;
; ENDING WITHOUT A WINDOW is the loader's own abort path (SPEC.md 20.2): the
; entry proc returns CF = 1, which frees the region. A toast the entry RAISED
; is kept and re-homed to the kernel rather than replaced by `Load failed`
; (kernel/loader.inc, .abort), and the toast's text is COPIED (OSAPI_TOAST),
; so it outlives the region it came from. The toast is 24 characters at most
; (SPEC.md 59.10), which is why the refusals here are a table of their own
; and not REDLINE's sentences.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'RLAPPLY', ra_entry, OS88_F_ICON

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A chip seen from above - its notch, four pins a side - with a check mark on
; it: the saved setting, put back. The mask is the chip and its pins, grown
; by a pixel.
;
;   ................
;   ................
;   ...####..####...
;   ...#...##...#...
;   .###........###.
;   ...#........#...
;   .###......#.###.
;   ...#.....##.#...
;   .###.#..##..###.
;   ...#.####...#...
;   .###..##....###.
;   ...#........#...
;   ...##########...
;   ................
;   ................
;   ................
    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay)
    dw 0x3FFC
    dw 0x3FFC
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x3FFC
    dw 0x3FFC
    dw 0x0000
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0000
    dw 0x1E78
    dw 0x1188
    dw 0x700E
    dw 0x1008
    dw 0x702E
    dw 0x1068
    dw 0x74CE
    dw 0x1788
    dw 0x730E
    dw 0x1008
    dw 0x1FF8
    dw 0x0000
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END

%define CS_APPLY_ONLY
%define CS_LOADER                   ; csapply.inc's load and apply: ours alone
%define OD_BOOTVOL                  ; where REDLINE saved it (103.6.4)

; -----------------------------------------------------------------------------
; ra_entry - in: DS = our segment, ES = KERNEL_SEG; out: CF = 1, always - the
; work is done and there is no window to hand back
; -----------------------------------------------------------------------------
ra_entry:
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). It lives only as long as this
    ; call, inside which mem_in_nest pins it anyway - so the declaration buys
    ; nothing today and costs 16 bytes, and that is the trade the ratchet
    ; (tests/movable.txt) asks for over an exemption with no mechanism in it
    OS88_REGION_MOVABLE ra_ret
    push ax
    push bx
    push cx
    push si
    push es
    call OSAPI_CPU_INFO
    mov [cs_tier], al
    call cs_fingerprint
    call cs_cfg_load                ; AL = CSA_*, CF = 1 refused
    jc .say
    call cs_cfg_apply
    jc .say
    mov al, CSA_OK
.say:
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [ra_says + bx]
    push ds
    pop es
    xor cx, cx                      ; the standard ~3 seconds
    call OSAPI_TOAST
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    xor bx, bx                      ; no window
    stc                             ; ...so the loader frees us: done
ra_ret:
    ret

; One line per CSA_* (csapply.inc), 24 characters at most (SPEC.md 59.10)
ra_says:    dw ra_s_ok, ra_s_nofile, ra_s_bad, ra_s_board, ra_s_row
            dw ra_s_cpu, ra_s_knob
ra_s_ok:     db 'Chipset setting applied', 0
ra_s_nofile: db 'No REDLINE.CFG to apply', 0
ra_s_bad:    db 'REDLINE.CFG is damaged', 0
ra_s_board:  db 'REDLINE.CFG: other BIOS', 0
ra_s_row:    db 'REDLINE.CFG: no such row', 0
ra_s_cpu:    db 'RLAPPLY: XT, nothing set', 0
ra_s_knob:   db 'REDLINE.CFG: bad setting', 0

%include "os88data.inc"
%include "redline/csrows.inc"
%include "redline/csfe.inc"
%include "redline/csknob.inc"
%include "redline/csapply.inc"

    OS88_BSS 0
    OS88_IMAGE_END
