; =============================================================================
; os8088 - apps/vbattle/vbinp.inc - THE DECODE IN PLACE
; (VBATTLE-MEMORY-PLAN 12, wave M5; SPEC.md 126.4.2)
;
; Once a hold has the WHOLE bundle (vbhold.inc, [vb_hfull]), a ring given a
; clip from then on reads it where the hold has it: nothing is copied into
; the ring at all. Video Player's 98.3.18.7 (EMS), 98.3.18.11 (LOADALL on a
; 286) and 98.3.18.14 (a 386's loaded base) are the precedents, and the
; battle takes their mechanisms as they are:
;
;   - THE RING'S BOOKKEEPING STAYS. A ring in place ([RG_INP]) is filled
;     the moment it is given its clip - [RG_FILL] its whole length - so the
;     reader never reads for it, a loop is always "held" (VB_STRM), and its
;     cursors are the stream offsets they always were. What changes is the
;     ADDRESS: vb_iat turns (ring, a cursor's offset) into the record in the
;     hold, for the picture's cursor and the sound's alike.
;   - EMS: the hook maps the three pages from the one under the record into
;     the frame's quarters 0-2 with its OWN OUTs - EMSV_FRAME's recipe and
;     EMSV_BASE's first page (SPEC.md 107.4), because the hook is an ISR and
;     may call nothing - so a record of up to 32 KB from anywhere in a page
;     is whole there, and DX is the frame at it: an ordinary segment. The
;     UI task's copies (the music, a ring given its clip before the fill
;     ended) move to quarter 3, a register the hook never writes.
;   - XMS: DX is a HANDLE, VB_XH, naming [vb_xpc], and the `mov ds, dx`
;     that every read of a record starts with is VB_DSDX - a borrow of DS's
;     hidden base: a moment of protected mode on a 386 (apps/os88ub.inc),
;     LOADALL on a 286 (apps/os88la.inc). A HIDDEN BASE DIES AT ANY SEGMENT
;     LOAD and every ISR loads DS, so a borrow runs with interrupts off; the
;     decode, which the hook runs with them on, YIELDS at vdec.inc's VD_YCK
;     sites every VB_XYB record bytes (vd_yield): DS ours again, a moment
;     with interrupts on, the same base borrowed again at the same SI. The
;     sites are jumps over themselves until a session in place patches them
;     (vd_ypatch) - which is all an 8088 pays for them.
;
; TAKEN ONLY FOR A WHOLE HOLD, after a check (vb_hipq): XMS on a 286 or
; later, real mode (a token is no address under kern_mega's CPU_F_PM,
; SPEC.md 50.6.10), LOADALL probed on a 286 with INT 6 hooked, and on
; either tier the hold's first 16 bytes read in place against
; OSAPI_XMEM_COPY's - a gate a BIOS closed reads 1 MB low and fails it. EMS
; when the frame's four quarters are ours. `[vb_noinp]` is the A/B gate.
; EMS only on a board TIMED as fast as RAM (vbhold.inc's vb_espeed, 98.3.18.8):
; the decode is the hook's time, and a slow board's reads are late frames.
; =============================================================================

VB_XYB      equ 2048            ; record bytes between yields (VP_XYB's)

%define UB_DS                   ; (ub_set loads DS's base alone)
%include "os88la.inc"
%include "os88ub.inc"

; vb_iring - vb_rreset's tail: ring BX, just given clip [RG_CLIP], read IN
; PLACE when the hold may be ([vb_hinp]) - its whole clip "read" at once.
; From the UI task, or with interrupts off. AX spent
vb_iring:
    mov byte [bx+RG_INP], 0
    cmp byte [vb_hinp], 0
    je .r
    mov al, [bx+RG_CLIP]
    cmp al, 0xFF
    je .r
    push si
    push cx
    push bx
    xor ah, ah
    mov bx, ax
    call vb_crec
    pop bx
    mov ax, [si+VC_OFF]             ; (a whole hold from the file's first
    mov [bx+RG_HB], ax              ; byte: its offset in the block is its
    mov ax, [si+VC_OFF+2]           ; offset in the file)
    mov [bx+RG_HB+2], ax
    mov ax, [si+VC_LEN]
    mov [bx+RG_FILL], ax
    mov ax, [si+VC_LEN+2]
    mov [bx+RG_FILL+2], ax
    mov byte [bx+RG_INP], 1
    inc word [vb_irings]
    pop cx
    pop si
.r:
    ret

; vb_iat - BX = a ring in place, DX:AX = an offset in its clip -> DX:SI =
; the record there. EMS: its pages mapped into quarters 0-2, DX the frame
; at it; XMS: DX = VB_XH, [vb_xpc] its linear address, SI = 0. AX spent.
; The hook's (or the UI task's with interrupts off): no kernel call
vb_iat:
    add ax, [bx+RG_HB]
    adc dx, [bx+RG_HB+2]            ; DX:AX = its offset in the block
    inc word [vb_icalls]
    cmp byte [vb_htype], 2
    je .ems
    add ax, [vb_htok]
    adc dx, [vb_htok+2]
    mov [vb_xpc], ax
    mov [vb_xpc+2], dx
    mov dx, VB_XH
    xor si, si
    ret
.ems:
    push cx
    mov si, ax
    and si, EMS_PAGE - 1            ; SI: its offset in its page
    mov cl, 14
    shr ax, cl
    mov cl, 2
    shl dx, cl
    or ax, dx                       ; AX: the page, in the handle's run
    add ax, [vb_ebase]
    mov dx, [vb_eport]
    mov cx, 3
.m:
    push ax
    or al, [vb_eor]
    out dx, al
    pop ax
    inc ax
    add dx, [vb_estep]
    loop .m
    mov ax, si
    and si, 15
    mov cl, 4
    shr ax, cl
    add ax, [vb_efseg]
    mov dx, ax
    pop cx
    ret

; vb_ilen - DX:SI = a record (vb_iat's) -> CX = its length. Preserves all
; but CX and flags
vb_ilen:
    push ds
    VB_DSDX
    mov cx, [si+RH_LEN]
    VB_DSEND
    pop ds
    ret

; vb_xbeg - VB_DSDX's borrow: IF = 0 and the caller's flags just pushed.
; DS = the record's window from here, every general register and ES as
; they were
vb_xbeg:
    push ax
    push bp
    mov bp, sp
    mov al, [ss:bp + 7]             ; the flags VB_DSDX pushed: IF (bit 9) is
    and al, 2                       ; whether a yield may open them
    mov [cs:vb_xif], al
    mov byte [cs:vb_xact], 1
    pop bp
    pop ax
    call vd_ylset
; vb_xld - the borrow itself: DS's base [vb_xpc], ES's whatever ES is. IF = 0.
; Every general register and ES preserved
vb_xld:
    cmp byte [cs:vb_tier], CPU_386
    jae vb_ubld                     ; a 386: a loaded base (98.3.18.14)
    push ax
    push dx
    mov ax, [cs:vb_xpc]
    mov [cs:la_dsc], ax
    mov ax, [cs:vb_xpc + 2]
    mov [cs:la_dsc + 2], ax
    mov ax, cs
    mov [cs:la_dsv], ax             ; (a cache that did not take reads the
    mov ax, es                      ; package's own bytes: a wrong picture)
    mov [cs:la_esv], ax
    call la_lin
    mov [cs:la_esc], ax
    mov [cs:la_esc + 2], dx
    pop dx
    pop ax
    push cs
    pop ds
    jmp la_borrow                   ; ...and its ret is ours

; vb_xfin - VB_DSEND's: DS ours again, ES as it was. IF = 0
vb_xfin:
    push es
    call vb_xrst
    pop es
    mov byte [vb_xact], 0
    mov word [vd_ylim], 0xFFFF
    ret

; vb_xrst - a borrow's end, either tier's: DS = ours (a 286's API cells put
; back first, la_restore - which leaves ES ours too). IF = 0. Every general
; register kept
vb_xrst:
    cmp byte [cs:vb_tier], CPU_386
    jb la_restore                   ; (...and its ret is ours)
    push cs                         ; a 386's base: one real-mode load and
    pop ds                          ; it is gone
    ret

; vb_ubld - vb_xld on a 386 (SPEC.md 41.15): DS's hidden base loaded with
; [vb_xpc] through os88ub.inc's moment of protected mode. IF = 0. Every
; general register (all 32 bits) and ES preserved
cpu 386
vb_ubld:
    push eax
    push cs
    pop ds                          ; (ub_set's rule: DS = our image)
    mov eax, [vb_xpc]
    call ub_set
    pop eax
    ret
cpu 8086

; vd_ylset - [vd_ylim] = SI + VB_XYB, or FFFFh where that wraps. Preserves
; all but flags
vd_ylset:
    push ax
    mov ax, si
    add ax, [cs:vb_xyb]
    jnc .l
    mov ax, 0xFFFF
.l:
    mov [cs:vd_ylim], ax
    pop ax
    ret

; vd_yield - vdec.inc's VD_YCK, SI past [vd_ylim]: inside a borrow whose
; caller had interrupts on (the hook's decode: vb_hook opens them around a
; frame), DS ours, a moment with them on, and the same base borrowed again.
; The hook does not run again inside itself, so [vb_xpc] is still the
; record's. Every register preserved; DS the window again on the way out
vd_yield:
    cmp byte [cs:vb_xact], 0
    je .lim
    cmp byte [cs:vb_xif], 0
    je .lim                         ; (borrowed with interrupts off: none)
    push es
    call vb_xrst
    pop es
    inc word [cs:vb_iyc]            ; (tests/vbthold.py counts)
    sti                             ; an interrupt owed since the borrow is
    nop                             ; taken here
    nop
    cli
    call vb_xld
.lim:
    jmp vd_ylset

; vb_hipq - the hold has just filled: may a ring be read in place from now
; on? [vb_hinp] 1 and, on XMS, the yield sites live; else 0. The UI task's
vb_hipq:
    mov byte [vb_hinp], 0
    cmp byte [vb_noinp], 0          ; (the A/B gate: the copying hold)
    jne .no
    cmp byte [vb_hrn], 1            ; THE WHOLE FILE, one range from its
    jne .no                         ; first byte
    cmp word [vb_htab+HT_START], 0
    jne .no
    mov ax, [vb_hkb4]
    call vb_h4k
    cmp dx, [vb_meta+VH_TOTAL+2]
    jb .no
    ja .w
    cmp ax, [vb_meta+VH_TOTAL]
    jb .no
.w:
    cmp byte [vb_htype], 2
    jne .x
    cmp byte [vb_eq], 0x0F          ; EMS: the four quarters ours
    jne .no
    cmp byte [vb_eslow], 0          ; ...and the board as fast as RAM: the
    jne .no                         ; decode is the hook's time (98.3.18.8)
    jmp short .yes
.x:
    call OSAPI_CPU_INFO             ; AL = the tier, AH = CPU_F_*
    test ah, CPU_F_PM               ; (kern_mega: a token is no address,
    jnz .no                         ; and neither borrow is real mode's)
    mov [vb_tier], al
    cmp al, CPU_286
    jb .no
    ja .chk
    mov byte [la_csacc], 0x93       ; a 286: LOADALL, probed
    call la_capture
    call la_probe
    jc .no
.chk:
    push ds                         ; the hold's first 16 bytes, by the
    pop es                          ; driver...
    mov si, vb_xsig
    mov ax, [vb_htok]
    mov dx, [vb_htok+2]
    mov cx, 16
    mov di, 1
    call OSAPI_XMEM_COPY
    jc .no
    mov ax, [vb_htok]               ; ...and in place
    mov dx, [vb_htok+2]
    mov [vb_xpc], ax
    mov [vb_xpc+2], dx
    mov dx, VB_XH
    xor si, si
    mov di, vb_xsig
    mov cx, 8
    push ds
    VB_DSDX
    cld
    repe cmpsw
    lahf
    VB_DSEND
    pop ds
    test ah, 0x40                   ; ZF: every word the same
    jz .no
    mov al, 1
    call vd_ypatch                  ; the yield sites live
.yes:
    mov byte [vb_hinp], 1
.no:
    ret

LA_DATA                             ; (os88la.inc's, in the image)
UB_DATA                             ; (os88ub.inc's GDT, in the image)
vb_xsig:    times 16 db 0           ; the check's reference bytes
vb_xyb:     dw VB_XYB               ; the yield step (a gate pokes it lower:
                                    ; tests/vbthold.py, whose demo records
                                    ; are mostly under 2 KB)
