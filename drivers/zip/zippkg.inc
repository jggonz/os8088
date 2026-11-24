; =============================================================================
; zippkg.inc - ZIP.DRV's package door (SPEC.md 138.7)
;
; OSAPI_DRV_CALL with BH = DRVC_ZIP and BL = a ZIPV_* (apps/os88api.inc, where
; the verbs and the ZIPS_/ZIPP_/ZIPM_ answers live so a package needs no
; include of ours). UI TASK ONLY, both verbs: each talks to the drive and may
; wait on it, a spin-up for seconds (SPEC.md 20.6 rule 7). DI comes back the
; caller's (SPEC.md 20.11); ES is the CALLING PACKAGE's segment here and
; nothing below reads through it.
;
; Eject is also the page's button (zipcp.inc), so the routine is here once.
; =============================================================================

; -----------------------------------------------------------------------------
; zip_pkg - DSV_PKGCALL
; in:  BL = ZIPV_INFO / ZIPV_EJECT
; out: ZIPV_INFO: CF = 0, AL = ZIPS_*, AH = ZIPP_* | ZIPM_* << 4, CX:BX = the
;      medium's sectors (0 = none), DX = the port
;      ZIPV_EJECT: CF = 0 ejected; CF = 1 and AL = an int 13h status
;      any other verb: CF = 1, nothing done
; -----------------------------------------------------------------------------
zip_pkg:
    cmp bl, ZIPV_EJECT
    je zp_eject
    cmp bl, ZIPV_INFO
    jne .no
    call zs_query               ; the drive, now: its CF is not the answer
    mov ah, [zt_mode]
    mov cl, 4
    shl ah, cl
    or ah, [zp_proto]
    mov al, [zp_st]
    mov bx, [zp_cap]
    mov cx, [zp_cap+2]
    mov dx, [zt_base]
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; zp_eject - write back, unmount, eject, and the empty zone back (138.7)
; out: CF = 0 the drive ejected; CF = 1 and AL = an int 13h status
; clobbers: AX, flags
;
; OSAPI_VOL_DEL FIRST, and it is the kernel's to write back whatever it holds
; dirty for the volume (SPEC.md 138.1) before the row goes - so the disk
; leaves with its FAT whole. Then START STOP UNIT with LoEj, riding out one
; UNIT ATTENTION (a disk swapped since the last command reports that first),
; and the volume added again whatever the drive said: the zone stays on the
; desktop as an empty drive's, and the next disk is learned by the next read.
; -----------------------------------------------------------------------------
zp_eject:
    push cx
    push dx
    call zip_voldel
    mov dl, 2
.go:
    mov ax, SC_STARTSTOP | (0x02 << 8)  ; byte 4: LoEj = 1, Start = 0
    xor cx, cx
    call zs_simple
    jnc .done
    cmp al, 0x06
    jne .done
    dec dl
    jnz .go
    stc
.done:
    pushf
    mov byte [zp_med], ZM_UNKNOWN
    jc .kept
    mov byte [zp_st], ZIPS_NODISK
    xor dx, dx
    mov [zp_cap], dx
    mov [zp_cap+2], dx
.kept:
    call zip_voladd
    popf
    pop dx
    pop cx
    ret
