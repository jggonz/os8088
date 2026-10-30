; =============================================================================
; os8088 - tests/cpubench/cpubench.asm
;
; CPUBENCH: WHAT THE INSTRUCTIONS REALDMG'S TRANSLATED CODE IS MADE OF COST
; THIS MACHINE. The precompiled and recompiled Game Boy code (REALDMG-SPEC
; 16, 20) is a stream of a dozen instruction shapes - conditional jumps taken
; and not, near jumps between blocks, LAHF/SAHF around the flags, SUB BP for
; the time, memory through SS - and 86Box's 286 is the lens most people see
; it through, while the owner's Packard Bell Victory runs it ~30% faster.
; Which shapes the two machines price differently is the question, and it
; decides which translator round is worth its bytes on which machine.
;
; Each row times ONE shape, a thousand of it a row (five passes of 200,
; unrolled: the pass's own DEC/JZ/JMP is 1.5% of the cheapest row), with
; tests/benchlib.inc's PIT timing - so the us column of a row IS nanoseconds
; per instruction. A few rows are pairs (a TEST before the jump that reads
; it); their single partner row is beside them, so the difference is the
; jump alone. Every row is CPU and memory only: no adapter, no disk, no
; kernel call inside a timed span. It runs on every kernel; on kern_mega the
; segment-register rows are protected-mode loads, which is part of the
; answer.
;
;   Then the interleave (docs/reports/XMS-GAP-VICTORY286-2026-10-15.md): a
; copy with its ends 0, 1, 2 and 3 KB apart mod 4 KB, and reads whose data
; sits 0-3 KB from the code reading it - whether code and data sharing a
; DRAM bank costs translated code what it costs a copy.
;
;   ...open CPUBENCH.O88, click the window (or press R). The report is saved
;   beside the package as CPUBENCH1.TXT, CPUBENCH2.TXT... - bring them back.
;
; `make cpubench` puts it alone on a 1.44MB and a 360KB disk.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'CPUBENCH', cb_entry

CB_PASS     equ 5                   ; passes of CB_REP: 1,000 a row
CB_REP      equ 200
CB_N        equ 20                  ; iterations of a row (each 1,000 ops):
                                    ;   the slowest shape (a far call and
                                    ;   return) is ~10 us on a 286, 10 ms an
                                    ;   iteration - well under the PIT's 33 ms

; a body: CB_PASS x CB_REP of one or two instructions, BX = SP - 64 (a word
; of stack nobody is using, interrupts being off), BP = 1 and ES = DS
%macro CB_BODY 2-3
%1:
    push cx
    push bx
    push bp
    push es
    push ds
    pop es
    mov bx, sp
    sub bx, 64
    mov bp, 1                       ; (SUB BP,0 leaves SF clear)
    mov cx, CB_PASS
%%pass:
%rep CB_REP
    %2
%if %0 > 2
    %3
%endif
%endrep
    dec cx
    jz %%done
    jmp %%pass
%%done:
    pop es
    pop bp
    pop bx
    pop cx
    ret
%endmacro

cb_entry:
    push si
    call bl_blank
    mov si, cb_s_title
    call bl_sline
    mov si, cb_s_hint
    call bl_sline
    mov si, cb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [cb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
.out:
    pop si
    ret

cb_paint:
    call bl_paint
    ret

cb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [cb_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call cb_run
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [cb_win], si
    call cb_run
    call bl_paint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the shapes --------------------------------------------------------------
    CB_BODY cb_b_nop, nop                           ; 1 byte: the fetch floor
    CB_BODY cb_b_movrr, {mov dx, cx}                ; 2 bytes, register
    CB_BODY cb_b_rdss, {mov dh, [ss:bx]}            ; 3: a template's RAM read
    CB_BODY cb_b_wrss, {mov [ss:bx], dh}            ; 3: ...and write
    CB_BODY cb_b_rdabs, {mov dl, [word ss:bx+2]}    ; 5: as a view field
    CB_BODY cb_b_wimm, {mov word [ss:bx], 0x1234}   ; 5: a long store
    CB_BODY cb_b_test, {test al, 0}                 ; the pair's partner
    CB_BODY cb_b_jcnt, {test al, 0}, {jnz $+2}      ; + a jump not taken
    CB_BODY cb_b_cmp, {cmp al, al}                  ; the pair's partner
    CB_BODY cb_b_jct, {cmp al, al}, {je $+2}        ; + a jump taken
    CB_BODY cb_b_jmps, {jmp short $+2}              ; a short jump
    CB_BODY cb_b_jmpn, {db 0xE9, 0, 0}              ; a near jump (rel16 0)
    CB_BODY cb_b_lsahf, {lahf}, {sahf}                 ; the flags' round trip
    CB_BODY cb_b_subbp, {sub bp, 5}                 ; the time charge
    CB_BODY cb_b_subjs, {sub bp, 0}, {js $+2}       ; ...and its event test
    CB_BODY cb_b_jns, {sub bp, 0}, {jns $+2}        ; ...the peephole's jns
    CB_BODY cb_b_segl, {mov es, [cb_dsv]}           ; a segment load
    CB_BODY cb_b_pushpop, {push dx}, {pop dx}          ; a stack round trip
    CB_BODY cb_b_callr, {call cb_ret}               ; near call + return
    CB_BODY cb_b_callf, {call far [cb_fret]}        ; far call + return

cb_ret:
    ret
cb_retf:
    retf

; a copy, a word at a time, 1,000 bytes of our own bss to another 1,000
cb_b_movsw:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, cb_buf
    mov di, cb_buf + 1000
    mov cx, 500
    cld
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    ret

; the interleave rows (docs/reports/XMS-GAP-VICTORY286-2026-10-15.md: a
; page-interleaved board closes a DRAM row on every access whose two ends
; share a bank). A copy of 1,000 bytes in our 16 KB claim, its ends
; [cb_csrc] and [cb_cdst]; and 1,000 reads of one byte of it, [cb_roff],
; placed against the CODE reading it
cb_b_cpy:
    push cx
    push si
    push di
    push ds
    push es
    mov si, [cb_csrc]
    mov di, [cb_cdst]
    mov ds, [cb_dseg]
    push ds
    pop es
    mov cx, 500
    cld
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    ret

cb_b_rdes:
    push cx
    push bx
    push es
    mov es, [cb_dseg]
    mov bx, [cb_roff]
    mov cx, CB_PASS
.pass:
..@cb_rdloop:
%rep CB_REP
    mov dh, [es:bx]
%endrep
    dec cx
    jz .done
    jmp .pass
.done:
    pop es
    pop bx
    pop cx
    ret

; cb_gaps - the eight interleave rows, when the 16 KB claim was had
cb_gaps:
    mov si, cb_s_ghdr
    call bl_sline
    mov dx, [cb_dseg]
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = the claim's linear base
    mov [cb_dlin], ax
    neg ax
    and ax, 0x0FFF                  ; the first offset 0 mod 4 KB
    mov [cb_d0], ax
    mov dx, cs
    mov bl, SEG_LIN
    call OSAPI_SEG
    add ax, ..@cb_rdloop + CB_REP * 3 / 2   ; the loop's middle, linear
    mov [cb_clin], ax
    xor bx, bx                      ; BX = the gap, 0 to 3 KB
    mov di, cb_r_gcopy
.c:
    mov ax, [cb_d0]
    mov [cb_csrc], ax
    add ax, 8192
    add ax, bx
    mov [cb_cdst], ax
    mov word [bl_body], cb_b_cpy
    mov si, di
    xor al, al
    call bl_run
    add di, CB_GLEN
    add bx, 1024
    cmp bx, 4096
    jb .c
    xor bx, bx
.r:
    mov ax, [cb_clin]               ; the byte BX past the code, mod 4 KB,
    add ax, bx                      ;   in the claim's first 4 KB
    sub ax, [cb_dlin]
    and ax, 0x0FFF
    mov [cb_roff], ax
    mov word [bl_body], cb_b_rdes
    mov si, di
    xor al, al
    call bl_run
    add di, CB_GLEN
    add bx, 1024
    cmp bx, 4096
    jb .r
    ret

cb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov word [bl_nrow], 0
    mov si, cb_s_title
    call bl_sline
    call OSAPI_CPU_INFO             ; AL = the tier, for the report
    xor ah, ah
    xor dx, dx
    mov cx, 5
    mov si, cb_s_tier
    call bl_kv
    mov [cb_dsv], ds
    mov [cb_fret + 2], cs           ; (a selector on kern_mega: CS as it is)
    mov word [cb_fret], cb_retf
    mov si, cb_s_hdr
    call bl_sline
    call bl_baseline
    mov word [bl_n], CB_N
    mov word [bl_body], cb_b_nop
    mov si, cb_r_nop
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_movrr
    mov si, cb_r_movrr
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_rdss
    mov si, cb_r_rdss
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_wrss
    mov si, cb_r_wrss
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_rdabs
    mov si, cb_r_rdabs
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_wimm
    mov si, cb_r_wimm
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_test
    mov si, cb_r_test
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_jcnt
    mov si, cb_r_jcnt
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_cmp
    mov si, cb_r_cmp
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_jct
    mov si, cb_r_jct
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_jmps
    mov si, cb_r_jmps
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_jmpn
    mov si, cb_r_jmpn
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_lsahf
    mov si, cb_r_lsahf
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_subbp
    mov si, cb_r_subbp
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_subjs
    mov si, cb_r_subjs
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_jns
    mov si, cb_r_jns
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_segl
    mov si, cb_r_segl
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_pushpop
    mov si, cb_r_pushpop
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_callr
    mov si, cb_r_callr
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_callf
    mov si, cb_r_callf
    xor al, al
    call bl_run
    mov word [bl_body], cb_b_movsw
    mov si, cb_r_movsw
    xor al, al
    call bl_run
    cmp word [cb_dseg], 0
    jne .have
    mov ax, 16                      ; the interleave rows' 16 KB
    call OSAPI_MEM_CLAIM
    jc .nogap
    mov [cb_dseg], dx
.have:
    call cb_gaps
    jmp short .saved
.nogap:
    mov si, cb_s_nomem
    call bl_sline
.saved:
    call bl_operator
    mov si, cb_f_txt
    call bl_save
    inc word [cb_done]              ; (a harness waits on this)
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%define BL_ARENA_BYTES 4000
%define BL_NUMBERED                 ; a field run saves NAME1, NAME2... (benchlib)
%define BL_INTRO_HOW 'automatically, when the run ends' ; benchlib's banner
%include "benchlib.inc"

cb_tpl:
    dw 7, 22, 632, 300
    dw cb_ttl, cb_paint, cb_onkey, cb_onclick

cb_ttl:       db 'CPU Bench', 0
cb_f_txt:     db 'CPUBENCH.TXT', 0
cb_s_title:   db 'CPUBENCH - the shapes of translated code, priced', 0
cb_s_hint:    db 'Click, or press R, to run. Rows are 1,000 of one shape.', 0
cb_s_hdr:     db '-- a row: 1,000 instructions, so us = ns each --', 0
cb_s_tier:    db 'CPU tier', 0
cb_r_nop:     db 'nop', 0
cb_r_movrr:   db 'mov dx,cx', 0
cb_r_rdss:    db 'mov dh,[ss:bx]', 0
cb_r_wrss:    db 'mov [ss:bx],dh', 0
cb_r_rdabs:   db 'mov dl,[ss:abs]', 0
cb_r_wimm:    db 'mov word [ss:bx],imm', 0
cb_r_test:    db 'test al,0', 0
cb_r_jcnt:    db 'test + jnz not taken', 0
cb_r_cmp:     db 'cmp al,al', 0
cb_r_jct:     db 'cmp + je taken', 0
cb_r_jmps:    db 'jmp short', 0
cb_r_jmpn:    db 'jmp near', 0
cb_r_lsahf:   db 'lahf + sahf', 0
cb_r_subbp:   db 'sub bp,5', 0
cb_r_subjs:   db 'sub bp + js not taken', 0
cb_r_jns:     db 'sub bp + jns taken', 0
cb_r_segl:    db 'mov es,[mem]', 0
cb_r_pushpop: db 'push + pop', 0
cb_r_callr:   db 'call + ret near', 0
cb_r_callf:   db 'call + ret far', 0
cb_r_movsw:   db 'rep movsw, 2 bytes', 0

cb_s_ghdr:    db '-- the interleave: a page-interleaved board (2 KB pages) --', 0
cb_s_nomem:   db 'NO MEMORY for the interleave rows (16 KB)', 0
CB_GLEN       equ 24                ; each label below, padded to this
cb_r_gcopy:   db 'copy 1000, ends 0K', 0
              times CB_GLEN - 19 db 0
              db 'copy 1000, ends 1K', 0
              times CB_GLEN - 19 db 0
              db 'copy 1000, ends 2K', 0
              times CB_GLEN - 19 db 0
              db 'copy 1000, ends 3K', 0
              times CB_GLEN - 19 db 0
              db 'read, 0K from code', 0
              times CB_GLEN - 19 db 0
              db 'read, 1K from code', 0
              times CB_GLEN - 19 db 0
              db 'read, 2K from code', 0
              times CB_GLEN - 19 db 0
              db 'read, 3K from code', 0
              times CB_GLEN - 19 db 0

cb_win:       dw 0
cb_dseg:      dw 0
cb_dlin:      dw 0
cb_clin:      dw 0
cb_d0:        dw 0
cb_csrc:      dw 0
cb_cdst:      dw 0
cb_roff:      dw 0
cb_dsv:       dw 0
cb_fret:      dw 0, 0
cb_done:      dw 0                  ; runs completed

CB_BSS_OWN  equ 2000                ; the copy's two 1,000-byte buffers
    OS88_BSS CB_BSS_OWN + BL_BSS_SIZE
    OS88_IMAGE_END

cb_buf      equ os88_image_end + 0

    BL_BSS os88_image_end + CB_BSS_OWN
