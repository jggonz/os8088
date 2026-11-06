; thkcall.asm - WIN16 fixture (SPEC.md 124.26.14, WIN16-NEXT c3): discardable
; code reached by far calls only, as Microsoft's linker lays them out, and one
; segment that is also named by a segment alone.
;
;   nasm -f bin -o THKCALL.EXE tests/win16/thkcall.asm
;
; Assembled, not compiled, for the shape: Open Watcom's wlink writes a far call
; as a FAR POINTER relocation (type 3, the offset in the record) and marks no
; segment DISCARDABLE, while Microsoft's (InstallShield's _INST16.EXE, 6,955
; sites) writes `9Ah offset` with a SELECTOR relocation (type 2) on the segment
; word, the offset a literal in the instruction. This is the second.
;
; Five segments:
;
;   1  code    fixed, PRELOAD: the entry point. Calls A, B, A, A+10h, C, B, A.
;   2  DGROUP  the text the calls build.
;   3  A       3,500 bytes of discardable code. A+0 returns 'a'; A+10h loads
;              the selector of C with `mov ax, seg C` (a selector fixup that is
;              no call, in a segment that is itself loaded later) and returns
;              the byte at C:0.
;   4  B       3,500 bytes of discardable code. B+0 returns 'b'. With a code
;              budget under both (W16DEFS=-DW16_CODEKB=5) calling B discards A
;              and calling A discards B.
;   5  C       a byte 'c' at 0 and, at 10h, a function returning 'x'. The
;              entry point calls it far, but A names it by `mov`: it is KEPT
;              when A is first loaded (SPEC.md 124.26.14), not at launch.
;
; The MessageBox text is "thkcall: abacxba": the seven results in order. A
; loader that left a segment's number in a call (the thunk skipped) runs the
; wrong segment's bytes; one that discarded C leaves A+10h reading a stale
; paragraph; one that never reloaded A or B stops at the second call.
;
; tests/win16thunk.py builds WIN16 with a 5 KB code budget and reads
; w16_cnload and w16_cndrop as well.

cpu 8086
bits 16

SEGSHIFT        equ 4
CODE_AT         equ 0x200
DATA_AT         equ 0x400
A_AT            equ 0x600
B_AT            equ 0x1800
C_AT            equ 0x3000
SEG_LEN         equ 3500
MOD_KERNEL      equ 1
MOD_USER        equ 2

section mz start=0 vstart=0
        db      'MZ'
        dw      0x80
        dw      1
        dw      0
        dw      4
        dw      0, 0xFFFF
        dw      0, 0x80
        dw      0
        dw      0, 0
        dw      0x40
        dw      0
        times   0x3C - ($ - $$) db 0
        dd      0x80
stub:   push    cs
        pop     ds
        mov     dx, stub_msg - stub
        mov     ah, 9
        int     0x21
        mov     ax, 0x4C01
        int     0x21
stub_msg:
        db      'This program requires Microsoft Windows.', 13, 10, '$'
        times   0x80 - ($ - $$) db 0

section ne follows=mz vstart=0
        db      'NE', 5, 1
        dw      ne_entab, ne_entab_end - ne_entab
        dd      0
        dw      0x0302
        dw      2                           ; DGROUP is segment 2
        dw      0x0400
        dw      0x1400
        dw      t_start, 1                  ; CS:IP
        dw      0, 2                        ; SS:SP
        dw      5                           ; segments
        dw      2                           ; modules
        dw      ne_nonres_end - ne_nonres
        dw      ne_segtab
        dw      ne_restab
        dw      ne_restab
        dw      ne_modtab
        dw      ne_imptab
        dd      0x80 + ne_nonres
        dw      0
        dw      SEGSHIFT
        dw      0
        db      2
        db      0
        dw      0, 0
        dw      0
        dw      0x0300
ne_segtab:
        dw      CODE_AT >> SEGSHIFT, code_len, 0x0D40, code_len
        dw      DATA_AT >> SEGSHIFT, data_len, 0x0C41, data_len
        dw      A_AT >> SEGSHIFT, SEG_LEN, 0x1D10, SEG_LEN       ; discardable
        dw      B_AT >> SEGSHIFT, SEG_LEN, 0x1C10, SEG_LEN       ; discardable, no relocations
        dw      C_AT >> SEGSHIFT, c_len, 0x1C10, c_len           ; discardable, no relocations
ne_restab:
        db      7, 'THKCALL'
        dw      0
        db      0
ne_modtab:
        dw      ne_imp_kernel - ne_imptab
        dw      ne_imp_user - ne_imptab
ne_imptab:
        db      0
ne_imp_kernel:
        db      6, 'KERNEL'
ne_imp_user:
        db      4, 'USER'
ne_entab:
        db      0
ne_entab_end:
ne_nonres:
        db      .e - .s
.s:     db      'thkcall - os8088 WIN16 fixture (SPEC.md 124.26.14)'
.e:     dw      0
        db      0
ne_nonres_end:

%assign NREL 0

; FARCALL module, ordinal: an import by ordinal (type 3, kind 1)
%macro FARCALL 2
%assign NREL NREL+1
        db      0x9A
rsite_%[NREL]:
        dw      0xFFFF, 0
%xdefine rat_%[NREL] 3
%xdefine rk_%[NREL] 1
%xdefine r1_%[NREL] %1
%xdefine r2_%[NREL] %2
%endmacro

; CALLA / CALLB / CALLC off: CALL FAR seg:off with the offset a literal and a
; SELECTOR relocation (type 2, internal) on the segment word. Each target's
; sites are one record's chain: the word at a site holds the next site.
%assign NA 0
%assign NB 0
%assign NC 0
%macro CALLA 1
%assign NA NA+1
%assign NA1 NA+1
        db      0x9A
        dw      %1
asite_%[NA]:
        dw      asite_%[NA1]
%endmacro
%macro CALLB 1
%assign NB NB+1
%assign NB1 NB+1
        db      0x9A
        dw      %1
bsite_%[NB]:
        dw      bsite_%[NB1]
%endmacro
%macro CALLC 1
%assign NC NC+1
%assign NC1 NC+1
        db      0x9A
        dw      %1
csite_%[NC]:
        dw      csite_%[NC1]
%endmacro

section code start=CODE_AT vstart=0
t_start:
        xor     bp, bp
        push    bp
        FARCALL MOD_KERNEL, 91              ; InitTask
        or      ax, ax
        jz      t_fail
        mov     [t_hinst], di
        xor     ax, ax
        push    ax
        FARCALL MOD_KERNEL, 30              ; WaitEvent(0)
        push    word [t_hinst]
        FARCALL MOD_USER, 5                 ; InitApp(hInstance)
        or      ax, ax
        jz      t_fail
        CALLA   0x00
        mov     [t_text + 9], al
        CALLB   0x00
        mov     [t_text + 10], al
        CALLA   0x00
        mov     [t_text + 11], al
        CALLA   0x10
        mov     [t_text + 12], al
        CALLC   0x10
        mov     [t_text + 13], al
        CALLB   0x00
        mov     [t_text + 14], al
        CALLA   0x00
        mov     [t_text + 15], al
        xor     ax, ax                      ; MessageBox(NULL, text, caption, MB_OK)
        push    ax
        push    ds
        mov     ax, t_text
        push    ax
        push    ds
        mov     ax, t_caption
        push    ax
        xor     ax, ax
        push    ax
        FARCALL MOD_USER, 1
        mov     ax, 0x4C00
        int     0x21
t_fail:
        mov     ax, 0x4CFF
        int     0x21
%assign NA1 NA+1
%assign NB1 NB+1
%assign NC1 NC+1
asite_%[NA1] equ 0xFFFF                     ; the chains end
bsite_%[NB1] equ 0xFFFF
csite_%[NC1] equ 0xFFFF
code_len equ $ - $$

section crel follows=code align=1 vstart=0
        dw      NREL + 3
%assign k 1
%rep NREL
        db      rat_%[k], rk_%[k]
        dw      rsite_%[k], r1_%[k], r2_%[k]
%assign k k+1
%endrep
        db      2, 0                        ; selector, internal
        dw      asite_1, 3, 0               ; every call into A
        db      2, 0
        dw      bsite_1, 4, 0               ; ... into B
        db      2, 0
        dw      csite_1, 5, 0               ; ... into C

section data start=DATA_AT vstart=0
        times   16 db 0
t_hinst:
        dw      0
t_text:
        db      'thkcall: ???????', 0
t_caption:
        db      'os8088 WIN16', 0
data_len equ $ - $$

section a start=A_AT vstart=0
a_f1:                                       ; A+0
        mov     al, 'a'
        retf
        times   0x10 - ($ - $$) db 0x90
a_f2:                                       ; A+10h: the byte at C:0
        db      0xB8                        ; mov ax, seg C
a_movsite:
        dw      0xFFFF
        mov     es, ax
        mov     al, [es:0]
        retf
        times   SEG_LEN - ($ - $$) db 0x90
section acrel follows=a align=1 vstart=0
        dw      1
        db      2, 0
        dw      a_movsite, 5, 0

section b start=B_AT vstart=0
b_f1:                                       ; B+0
        mov     al, 'b'
        retf
        times   SEG_LEN - ($ - $$) db 0x90

section c start=C_AT vstart=0
        db      'c'                         ; C+0: the byte A+10h reads
        times   0x10 - ($ - $$) db 0
c_f1:                                       ; C+10h
        mov     al, 'x'
        retf
        times   0x20 - ($ - $$) db 0
c_len equ $ - $$
