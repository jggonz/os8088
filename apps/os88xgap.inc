; =============================================================================
; apps/os88xgap.inc - THE GAP: where to put a buffer that copies to and from
; extended memory (docs/reports/XMS-GAP-VICTORY286-2026-10-15.md)
;
; ON A PAGE-INTERLEAVED BOARD A COPY'S SPEED IS SET BY HOW FAR APART ITS TWO
; ENDS ARE. The Victory 286 alternates 2 KB DRAM pages between its two banks,
; so a copy whose read and write land in different banks keeps both rows
; open, and one whose read and write land in the same bank closes a row on
; every access: the same 8 KB copy, either direction, is 2,980 us with its
; ends 0 mod 4 KB apart and 1,661 us with them 2 KB apart, linear in between
; - the "good boots and slow boots" of a whole week of field reports, which
; were boots whose heap happened to leave one gap or the other.
;
; THE PERIOD IS THE BOARD'S, so it is MEASURED, never assumed: a board with
; 256 Kbit parts has 1 KB pages and a 2 KB period, where 2 KB apart is the
; WORST placement; a board that does not interleave has no preference at
; all; and a 386's cache may hide the lot. xg_gap times one 8 KB copy at
; gaps of 0, 512 bytes and 1, 2, 4 and 8 KB, and answers the fastest - G -
; when it is clearly faster than 0 apart. G is half the board's period, so
; the rule a caller places by is:
;
;     (conventional end - extended end) mod 2G = G
;
; and a buffer can always be moved there by claiming 2G more and starting
; up to 2G - 1 bytes in. Copies that step both ends by the same amount - a
; ring of 32 KB slots against 32 KB chunks - keep the gap for ever.
;
; WRITTEN ONCE, ASSEMBLED BY EACH CALLER (the os88la.inc shape): the
; measurement is a package's, against a block it owns, so it costs no
; resident byte and no driver byte. XMEM.DRV has 21 bytes of its 2 KB claim
; left, and a KB more of it is a KB of every 286 and 386's heap.
;
; CONTEXT: OSAPI_XMEM_COPY's - the UI task. ~40 ms on a 286 (twelve 8 KB
; copies), so once per instance. The CALLER DEFINES THE DATA (XG_DATA).
; =============================================================================

XG_NEED     equ 32768               ; the block xg_gap writes over: the gap's
                                    ; start (< 16 KB) + 8 KB + the 8 KB copy
XG_LEN      equ 8192                ; one timed copy

; xg_gap - DX:AX = the linear base of an XMS block the caller OWNS, at least
; XG_NEED long, whose first XG_NEED bytes may be overwritten. The copies go
; UP from a SCRATCH claim of 8 KB at the DMA door - 8 KB read there, nothing
; written - so the conventional end is conventional on every kernel: it was
; DS:0, and under kern_mega a package's DS may be above 1MB, where both its
; linear address and its speed are not the conventional end's
; (KERN-MEGA-PLAN 14.14). out CF=0 AX = G in bytes (0: no placement is
; faster); CF=1 AX = 0, a copy or the scratch was refused. Every other
; register kept
xg_gap:
    push bx
    push cx
    push dx
    push si
    push es
    push ax
    push dx
    mov ax, XG_LEN / 1024
    xor cx, cx
    call OSAPI_MEM_CLAIM_DMA_HI     ; DX = the scratch
    mov es, dx
    pop dx
    pop ax
    jc .fail
    mov bx, es                      ; C, ES:0's linear address - its low
    mov cl, 4                       ; 14 bits are all the arithmetic needs,
    shl bx, cl                      ; everything being mod 16 KB
    sub bx, ax
    and bx, 0x3FFF                  ; x0: the block's offset 0 mod 16 KB
    add ax, bx                      ; apart from C, so g is the gap exactly
    adc dx, 0                       ; (mod 16 KB, it is -g: the same rows)
    mov [xg_s0], ax
    mov [xg_s0+2], dx
    mov word [xg_bt], 0xFFFF
    mov word [xg_best], 0
    mov si, xg_gaps
.g:
    mov ax, [si]
    call xg_time
    jc .fail
%ifdef XG_TIMES
    mov bx, si                      ; (each gap's count kept, xg_tt: what a
    sub bx, xg_gaps                 ; caller's diagnostic saves, the board's
    mov [xg_tt + bx], ax            ; interleave as this boot found it)
%endif
    cmp si, xg_gaps
    jne .c
    mov [xg_t0], ax                 ; 0 apart: the reference
.c:
    cmp ax, [xg_bt]
    jae .n
    mov [xg_bt], ax
    mov ax, [si]
    mov [xg_best], ax
.n:
    inc si
    inc si
    cmp si, xg_gaps_end
    jb .g
    mov ax, [xg_t0]                 ; a preference only if it is a sixteenth
    mov bx, ax                      ; better than 0 apart: a board with none
    shr bx, cl                      ; (CL is still 4) measures within its
    sub ax, bx                      ; noise, and its answer is 0
    cmp [xg_bt], ax
    mov ax, [xg_best]
    jbe .out
    xor ax, ax
.out:
    clc
    jmp short .ret
.fail:
    xor ax, ax
    stc
.ret:
    pushf
    mov dx, es                      ; the scratch back (ES = 0 when it was
    or dx, dx                       ; refused: OSAPI_MEM_CLAIM answers DX = 0)
    jz .nf
    call OSAPI_MEM_FREE
.nf:
    popf
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; xg_time - AX = g: the least PIT count of two 8 KB copies up from ES:0 to
; [xg_s0] + g, in AX. CF=1 a copy refused. Every other register kept
xg_time:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, ax
    mov bp, 0xFFFF
    mov cx, 2
.r:
    push cx
    call xg_pit
    push ax                         ; the count before
    mov ax, [xg_s0]
    mov dx, [xg_s0+2]
    add ax, bx
    adc dx, 0
    xor si, si
    xor di, di                      ; UP: reads ES:0, writes the block
    mov cx, XG_LEN
    call OSAPI_XMEM_COPY
    pop cx                          ; (a pop: the copy's CF stands)
    jc .bad
    call xg_pit
    sub cx, ax                      ; a down-counter: before - after, mod
    cmp cx, bp                      ; 65536 (well inside a half-period:
    jae .k                          ; 8 KB is ~3 ms on the slowest 286)
    mov bp, cx
.k:
    pop cx
    loop .r
    mov ax, bp
    clc
    jmp short .out
.bad:
    pop cx
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; xg_pit - AX = PIT counter 0, latched. Every other register kept
xg_pit:
    pushf
    cli
    xor al, al
    out 0x43, al                    ; latch counter 0
    in al, 0x40
    mov ah, al
    in al, 0x40
    popf
    xchg al, ah
    ret

xg_gaps:    dw 0, 512, 1024, 2048, 4096, 8192
xg_gaps_end:

; the caller's data, wherever suits it
%macro XG_DATA 0
xg_s0:      dd 0                    ; the block's start, 0 apart from DS:0
xg_t0:      dw 0                    ; the count 0 apart
xg_bt:      dw 0                    ; the least count
xg_best:    dw 0                    ; ...and its gap
%ifdef XG_TIMES
xg_tt:      times 6 dw 0            ; every gap's count, xg_gaps' order
%endif
%endmacro
