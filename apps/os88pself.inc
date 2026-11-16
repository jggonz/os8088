; =============================================================================
; os8088 - apps/os88pself.inc
;
; op_self - ONE compressed lazy row, read and expanded by the package itself
; (docs/plans/DOS-PARTS-PLAN.md 1.1). The parts standard's OP_SELF read
; (SPEC.md 20.12.9.1) written ONCE, for a package that has to fetch a part
; where op_fetch is not: after a re-home (the loader that carried the
; standard's body is freed, SPEC.md 20.12.10), or from code that cannot afford
; the whole fetch surface.
;
; OPT-IN. `%define OP_WANT_SELF` before os88parts.inc emits it from the body
; for a package with a table; a carrier with NO table (DOS's re-homed host)
; `%include`s this file directly. It names nothing but its arguments, the
; OP_R_* constants and five OSAPI cells - no op_* bss word, because a tableless
; carrier reserves none and they would alias its own cells.
;
; in:  DS:SI -> the row (OP_ROW bytes, wherever the package holds a copy),
;      DS:DI -> the file's NUL-terminated name, AL = the stream's format
;      (OSAPI_LZ_*, the table's OP_T_FMT), AH = the least KB the claim must
;      be (0 = what the read needs): a caller that keeps working space past
;      the part - DOS's network part and its buffers, SPEC.md 96.23.11 - asks
;      for one claim of the whole rather than spending a second. The instance
;      STANDS in the file's
;      folder: every file cell resolves there (SPEC.md 19.2.1). UI task only
;      (a read and a claim, SPEC.md 20.6 rule 7).
; out: CF=0, DX = a claim of the CALLER'S (OSAPI_MEM_FREE it), the part's
;      `len` bytes at DX:0 and the rest of the claim UNDEFINED (the stream's
;      tail is in it), CX = len. CF=1 = not had, and nothing is held.
; clobbers: AX, BX, SI, DI, ES, BP, flags
;
; THE ARITHMETIC IS op_fetch's (SPEC.md 20.12.7.4), with one difference that
; is what makes it small: EVERYTHING IS ONE SEGMENT. The read lands
; R = roundup512(len - packed) bytes up the claim (a disk-visible base is
; 512-aligned), the stream is the head slack past that, and it expands DOWN
; onto offset 0 - so ES:BX, DS:SI and ES:DI are all the claim plus an offset
; and there is no paragraph sum to make, which is also why it needs no window
; on kern_mega (a selector plus an offset is still an address). The price is
; the ceiling: R + the read must fit 64KB, which every part this is for does,
; and a row that does not is refused rather than misread.
;
; WHOLE CLUSTERS, asked of the volume every call: kern_small's READ_AT still
; takes clusters (SPEC.md 18.4.4.2), so the offset is rounded down to one and
; the slack kept. Not cached - OSAPI_FILE_DFREE is ~105 ms on a 20MB disk and
; nothing here is a loop (SPEC.md 18.4.5), and a cache is bss a tableless
; carrier would have to reserve.
;
; HOSTILE DATA (the row came off the disk inside our own image): a part that
; does not get smaller is refused (os88pkg.py writes none, so it is not ours),
; every sum is carry-checked, the read must deliver the slack and the whole
; stream, and OSAPI_DECOMP is bounded on both sides - told the exact output
; and the exact input, it refuses rather than writing past either.
; =============================================================================
op_self:
    push si                     ; [bp+4] the row
    push di                     ; [bp+2] the name
    push ax                     ; [bp+0] the format
    mov bp, sp
    call OSAPI_FILE_DFREE       ; BX = sectors per cluster
    jc .no
    dec bx
    cmp bx, 63                  ; 1 to 64 sectors: a cluster of at most 32KB,
    ja .no                      ; which a 16-bit capacity can name
    inc bx
    mov cl, 9
    shl bx, cl
    dec bx                      ; BX = bytes per cluster - 1: a mask
    mov ax, [si+OP_R_OFF]
    mov dx, ax
    mov cl, OP_GSH              ; `off` is in PARAGRAPHS (SPEC.md 20.12.2)
    shl ax, cl
    mov cl, 16 - OP_GSH
    shr dx, cl                  ; DX:AX = the part's byte offset
    mov di, ax
    and di, bx                  ; DI = the head slack...
    sub ax, di                  ; ...and the read starts on its cluster
    push dx                     ; [bp-2]
    push ax                     ; [bp-4] the read's offset
    push di                     ; [bp-6] the slack
    mov ax, [si+OP_R_ZKB]       ; the PACKED length
    add ax, di
    jc .no6
    mov di, ax                  ; DI = what must arrive: slack + stream
    add ax, bx
    jc .no6
    not bx
    and ax, bx
    mov cx, ax                  ; CX = the read, in whole clusters
    mov bx, [si+OP_R_LEN]
    sub bx, [si+OP_R_ZKB]       ; len - packed, which os88pkg.py never lets be
    jbe .no6                    ; <= 0: a row that says so is not ours
    add bx, 511
    jc .no6
    and bx, 0FE00h              ; BX = R, where the read lands
    add ax, bx                  ; R + the read: the claim
    jc .no6
    add ax, 15
    rcr ax, 1                   ; (the carry back in)
    shr ax, 1
    shr ax, 1
    shr ax, 1                   ; AX = PARAGRAPHS, rounded up
    mov dh, [bp+1]              ; ...and never under the caller's floor (AH,
    xor dl, dl                  ; banked with the format, in KB; [bp] is
    shr dx, 1                   ; SS's, and so is this frame): DX = it in
    shr dx, 1                   ; paragraphs
    cmp ax, dx
    jae .kb
    xchg ax, dx
.kb:
    push bx                     ; (BX = R, below)
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P            ; DX = the claim
    pop bx
    jc .no6
    push dx                     ; [bp-8]
    mov es, dx
    mov si, [bp+2]
    mov ax, [bp-4]
    mov dx, [bp-2]
    call OSAPI_FILE_READ_AT     ; ES:BX, CX -> DX:AX delivered (< 64KB)
    jc .free
    cmp ax, di
    jb .free                    ; short: the file ends before the stream does
    mov si, [bp+4]
    mov cx, [si+OP_R_ZKB]
    mov dx, [si+OP_R_LEN]       ; BX:DX = the exact output, below
    add bx, [bp-6]              ; BX = the stream: R + the slack up the claim
    mov al, [bp+0]
    push ds
    push es
    pop ds
    mov si, bx                  ; DS:SI the stream, ES:0 its output
    xor bx, bx
    xor di, di
    call OSAPI_DECOMP
    pop ds
    jc .free
    pop dx                      ; the claim, the part at its base
    mov si, [bp+4]
    mov cx, [si+OP_R_LEN]
    add sp, 6                   ; the read's offset and the slack
    clc
    jmp short .out
.free:
    pop dx
    call OSAPI_MEM_FREE
.no6:
    add sp, 6
.no:
    stc
.out:
    pop ax
    pop di
    pop si
    ret
