; tiny16.asm - the smallest Windows 3.x program WIN16 (SPEC.md 124) loads,
; assembled by NASM with a hand-built NE header so that no C compiler is
; needed for the fast row to have something to read (docs/plans/WIN16-PLAN.md
; S1, D10).
;
;   nasm -f bin -o TINY16.EXE tests/win16/tiny16.asm
;
; It is a real program: on the Windows 3.0 reference machine (`win /r
; tiny16`, .claude/skills/win16-reference) it shows one message box and
; exits. What it starts with is what every Windows 3.x program starts with -
; InitTask, WaitEvent(0), InitApp(hInstance) - and its MessageBox is USER.1.
;
; It is ALSO a trap for the census (tools/os88ne.py), which is the other half
; of the reason it exists:
;
;   * five bytes of data sit inside the code segment, behind a jump, and read
;     as pusha / enter / pop cs / push imm8 / imul to a linear sweep. The
;     census must reach none of them, so the base build's verdict is 8086;
;   * `t16_pick` dispatches through a jmp cs:[bx+table] switch table, so its
;     three cases are reached only by reading the table;
;   * `t16_lonely` is called by nothing and starts with push bp / mov bp, sp,
;     so only the prologue heuristic finds it.
;
; Variants (each a -D on the command line; tests/unit/t_win16ne.py builds them):
;
;   -DT16_186         MB_OK is pushed with `push 0`: a REACHED 186 instruction,
;                     verdict 186
;   -DT16_LONELY186   t16_lonely shifts by an immediate: a 186 instruction only
;                     the prologue heuristic reaches, verdict 186
;   -DT16_FPU         an fld/fstp pair with OS fixups (type 5, FIDRQQ) on their
;                     fwait bytes, the MS C emulator convention: verdict 8086 +
;                     x87, two OS fixups, `fld m64` and `fstp m64`
;   -DT16_PROBE286    the 286 probe Open Watcom's start-up uses (push sp / pop
;                     ax / cmp ax, sp / jnz) guarding an smsw: verdict STILL
;                     8086, because an 8086 never executes the smsw
;   -DT16_UNKNOWN     one import more, USER.182 (KillSystemTimer), which
;                     only Windows' own drivers call and WIN16 does not
;                     provide: calling it ends the program, naming it
;                     (SPEC.md 124.12; it was USER.287 until S8 provided that)

%ifdef T16_186
cpu 186
%elifdef T16_LONELY186
cpu 186
%elifdef T16_PROBE286
cpu 286
%else
cpu 8086
%endif
bits 16

SEGSHIFT        equ 4           ; segments start on 16-byte boundaries
CODE_AT         equ 0x200       ; where the two segments sit in the file: fixed,
DATA_AT         equ 0x300       ;   because NASM cannot shift a section's start
                                ;   (bin reports overlapping sections if one grows)
MOD_KERNEL      equ 1           ; module reference table indices, 1-based
MOD_USER        equ 2

; ---------------------------------------------------------------------------
; The DOS stub: what an MZ loader runs. 64 bytes of header, 64 of stub.
; ---------------------------------------------------------------------------
section mz start=0 vstart=0
        db      'MZ'
        dw      0x80            ; bytes in the last page: the image is 128
        dw      1               ; pages
        dw      0               ; relocations
        dw      4               ; header paragraphs
        dw      0, 0xFFFF       ; min/max extra paragraphs
        dw      0, 0x80         ; SS:SP
        dw      0               ; checksum
        dw      0, 0            ; CS:IP
        dw      0x40            ; relocation table offset: >= 0x40 marks a new exe
        dw      0               ; overlay
        times   0x3C - ($ - $$) db 0
        dd      0x80            ; e_lfanew: the NE header
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

; ---------------------------------------------------------------------------
; The NE header and its tables. Offsets here are relative to the header.
; ---------------------------------------------------------------------------
section ne follows=mz vstart=0
        db      'NE', 5, 1                  ; linker 5.1
        dw      ne_entab, ne_entab_end - ne_entab
        dd      0                           ; CRC
        dw      0x0302                      ; multiple DGROUP, Windows API program
        dw      2                           ; DGROUP is segment 2
        dw      0x0400                      ; local heap
        dw      0x1400                      ; stack
        dw      t16_start, 1                ; CS:IP
        dw      0, 2                        ; SS:SP - SP 0 is "top of DGROUP"
        dw      2                           ; segments
        dw      2                           ; modules
        dw      ne_nonres_end - ne_nonres
        dw      ne_segtab
        dw      ne_restab                   ; resources: none (equal to restab)
        dw      ne_restab
        dw      ne_modtab
        dw      ne_imptab
        dd      0x80 + ne_nonres            ; non-resident names, a FILE offset
        dw      0                           ; movable entries
        dw      SEGSHIFT
        dw      0                           ; resource segments
        db      2                           ; Windows
        db      0
        dw      0, 0                        ; gangload area
        dw      0                           ; minimum code swap
        dw      0x0300                      ; expects Windows 3.00
ne_segtab:
        dw      CODE_AT >> SEGSHIFT, code_len, 0x0D40, code_len
        dw      DATA_AT >> SEGSHIFT, data_len, 0x0C41, data_len
ne_restab:
        db      6, 'TINY16'
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
.s:     db      'tiny16 - os8088 WIN16 fixture (SPEC.md 124)'
.e:     dw      0
        db      0
ne_nonres_end:

; ---------------------------------------------------------------------------
; Segment 1: code. Every far call to Windows is a relocation record below.
; ---------------------------------------------------------------------------
%assign NREL 0

; FARCALL module, ordinal: a far call the loader fixes up (import by ordinal)
%macro FARCALL 2
%assign NREL NREL+1
        db      0x9A
rsite_%[NREL]:
        dw      0xFFFF, 0                   ; 0xFFFF ends the (one-site) chain
%xdefine rat_%[NREL] 3                      ; far pointer
%xdefine rk_%[NREL] 1                       ; import by ordinal
%xdefine r1_%[NREL] %1
%xdefine r2_%[NREL] %2
%endmacro

; OSFIX type: the next instruction is an x87 one whose fwait the loader may
; patch into the emulator's interrupt (the MS C emulator convention)
%macro OSFIX 1
%assign NREL NREL+1
rsite_%[NREL]:
%xdefine rat_%[NREL] 5
%xdefine rk_%[NREL] 3                       ; OS fixup
%xdefine r1_%[NREL] %1
%xdefine r2_%[NREL] 0
%endmacro

section code start=CODE_AT vstart=0
t16_start:
        xor     bp, bp
        push    bp
        FARCALL MOD_KERNEL, 91              ; InitTask
        or      ax, ax
        jz      t16_fail
        mov     [t16_hinst], di
        xor     ax, ax
        push    ax
        FARCALL MOD_KERNEL, 30              ; WaitEvent(0)
        push    word [t16_hinst]
        FARCALL MOD_USER, 5                 ; InitApp(hInstance)
        or      ax, ax
        jz      t16_fail
        call    t16_pick
%ifdef T16_PROBE286
        push    sp                          ; an 8086 pushes SP-2, a 286 SP
        pop     ax
        cmp     ax, sp
        jnz     .not286
        smsw    ax
.not286:
%endif
%ifdef T16_FPU
        OSFIX   5
        fld     qword [t16_two01]           ; NASM emits the fwait (9B) first
        OSFIX   5
        fstp    qword [t16_two01]
%endif
        xor     ax, ax                      ; MessageBox(NULL, text, caption, MB_OK)
        push    ax
        push    ds
        mov     ax, t16_text
        push    ax
        push    ds
        mov     ax, t16_caption
        push    ax
%ifdef T16_186
        push    0
%else
        xor     ax, ax
        push    ax
%endif
        FARCALL MOD_USER, 1                 ; MessageBox
%ifdef T16_UNKNOWN
        FARCALL MOD_USER, 182               ; KillSystemTimer: not provided
%endif
        jmp     short t16_quit
        db      0x60, 0xC8, 0x0F, 0x6A, 0x69    ; data: never executed
t16_quit:
        mov     ax, 0x4C00
        int     0x21
t16_fail:
        mov     ax, 0x4CFF
        int     0x21

; a switch: its cases are reached only through the table
t16_pick:
        mov     bx, 1
        shl     bx, 1
        jmp     [cs:bx + t16_cases]
t16_cases:
        dw      t16_case0, t16_case1, t16_case2
t16_case0:
        mov     ax, 0
        ret
t16_case1:
        mov     ax, 1
        ret
t16_case2:
        mov     ax, 2
        ret

; called by nothing: only its prologue says it is code
t16_lonely:
        push    bp
        mov     bp, sp
%ifdef T16_LONELY186
        shl     ax, 3
%else
        shl     ax, 1
%endif
        pop     bp
        ret
code_len equ $ - $$

; the relocation table follows the segment's bytes directly
section crel follows=code align=1 vstart=0
        dw      NREL
%assign k 1
%rep NREL
        db      rat_%[k], rk_%[k]
        dw      rsite_%[k], r1_%[k], r2_%[k]
%assign k k+1
%endrep

; ---------------------------------------------------------------------------
; Segment 2: DGROUP. Windows owns its first 16 bytes (InitTask writes the
; stack bounds and the local heap's address there).
; ---------------------------------------------------------------------------
section data start=DATA_AT vstart=0
        times   16 db 0
t16_hinst:
        dw      0
t16_two01:
        dq      2.01
t16_text:
        db      'tiny16: a Windows program assembled by NASM', 0
t16_caption:
        db      'os8088 WIN16', 0
data_len equ $ - $$
