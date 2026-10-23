; =============================================================================
; os8088 - tests/dostrap/xswitch.asm
;
; XSWITCH.EXE - SPEC.md 96.40.9.1's SWITCH probe: it proves, by its EXIT CODE
; alone, which of kern_mega's two answers ran it. It has EXEPACK's SHAPE (no
; relocations, entry at CS:0010, 'RB' at CS:000E) and none of the format, so
; `kd_xunp` RECOGNISES it and REFUSES it - which is exactly the case the HMA
; kern_dos cannot help with its loader and hands to the LOW image instead
; (docs/plans/KERN-MEGA-PLAN.md 14.26).
;
;   make build/XSWITCH.EXE
;
; Where XPACK.EXE proves the hazard and leaves the two answers looking the
; same (both exit 0x30, differing only in the arena), this one distinguishes
; them, because the switch and the floor raise put the program in DIFFERENT
; places:
;
;   * the FLOOR RAISE (14.23) keeps the HMA kern_dos, A20 open, and forces the
;     arena to 1000h - so the load segment B is AT OR ABOVE 1000h and no wrap
;     is needed. This probe reports 0x20.
;   * the SWITCH (14.26) runs the LOW kern_dos, A20 SHUT, arena from ~0140h -
;     so B is BELOW 1000h and the EXEPACK wrap works. This probe reads its own
;     paragraph 0 through (B-0FFFh):FFF0 - the one address EXEPACK's unpacker
;     uses that only the wrap resolves - and reports 0x2A when it sees it.
;   * B below 1000h with the wrap NOT resolving is the pre-14.23 HMA bug and
;     cannot happen after the switch: 0xFE says it did, which is a failure.
;
; So on kern_mega with the switch built: XSWITCH exits 0x2A (042) at ~585 KB.
; Spoil the signature (-DNOSIG) and the loader does not touch it: it stays on
; the HMA, A20 open, B below 1000h, the wrap misses, 0xFE (254) at ~621 KB -
; the control that says the probe can tell the HMA from the low image.
;
; OURS, MIT with the rest of the tree, under tests/ because it is not shipped.
; =============================================================================

    cpu 8086
    bits 16

ENTSEG  equ 1                       ; the entry block's paragraph in the image

section .hdr start=0
    dw 0x5A4D                       ; e_magic
    dw FILESZ % 512                 ; e_cblp
    dw (FILESZ + 511) / 512         ; e_cp
    dw 0                            ; e_crlc: NONE, as EXEPACK's always is
    dw 32                           ; e_cparhdr
    dw 0x0040                       ; e_minalloc
    dw 0xFFFF                       ; e_maxalloc
    dw STACKSEG                     ; e_ss
    dw 0x0200                       ; e_sp
    dw 0                            ; e_csum
    dw 0x0010                       ; e_ip: EXEPACK's
    dw ENTSEG                       ; e_cs
    dw 0x001C                       ; e_lfarlc
    dw 0                            ; e_ovno
    times 512 - ($ - $$) db 0

section .mark start=512 vstart=0
    db 'XSWMARK1'
    times 16 - ($ - $$) db 0

section .ent follows=.mark vstart=0
    times 14 db 0                   ; the rest of a 16-byte EXEPACK header
%ifdef NOSIG
    db 'rb'                         ; the control: not the signature
%else
    db 'RB'                         ; the signature, at CS:IP-2
%endif
entry:                              ; CS:0010
    mov ax, es                      ; ES = the PSP
    add ax, 0x10                    ; B, the load segment
    cmp ax, 0x1000
    jae .high                       ; AT OR ABOVE 64KB: the floor raise ran it
    ; --- below 64KB: the LOW image ran it, so the wrap must resolve ---------
    mov bx, ax
    sub bx, 0x0FFF                  ; (B-0FFFh):FFF0 is B:0 only through the
    mov ds, bx                      ; 1MB wrap - A20 shut on the low image
    mov si, 0xFFF0
    mov es, ax
    xor di, di
    mov cx, 4                       ; eight bytes of paragraph 0
    cld
    repe cmpsw
    push cs
    pop ds
    jne .lowbad
    mov dx, s_low
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C2A                  ; SWITCHED to the low image, wrap OK
    int 0x21
.lowbad:
    mov dx, s_lowbad
    mov ah, 0x09
    int 0x21
    mov ax, 0x4CFE                  ; below 64KB but the wrap missed: A20 open
    int 0x21                        ; (the pre-14.23 HMA bug)
.high:
    push cs
    pop ds
    mov dx, s_high
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C20                  ; the FLOOR RAISE, not the switch
    int 0x21

s_low:    db 'XSWITCH: low image, A20 shut, wrap OK (switched)', 13, 10, '$'
s_lowbad: db 'XSWITCH: low but the wrap missed - A20 still open', 13, 10, '$'
s_high:   db 'XSWITCH: above 64KB - the floor raise, not the switch', 13, 10, '$'

    align 16
ENTEND  equ $ - $$
section .stk follows=.ent vstart=0
    times 0x200 db 0
STKEND  equ $ - $$

STACKSEG equ ENTSEG + ENTEND / 16
FILESZ   equ 512 + 16 + ENTEND + STKEND
