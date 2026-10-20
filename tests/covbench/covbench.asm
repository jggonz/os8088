; =============================================================================
; os8088 - tests/covbench/covbench.asm
;
; COVBENCH: what feeding a COVOX costs THIS machine, three ways (SPEC.md
; 34.14.3). The question it answers is the owner's: can an XT play a MOD or
; a MIDI song through a Covox at ~11 kHz? A Covox has no FIFO, so the CPU
; writes every sample; today that is an interrupt a sample (os88spk_isrd,
; ~430 cycles of a 4.77 MHz 8088 - all of an 11,025 Hz sample's 433). The
; bench prices the alternatives against ONE fixed workload, Tracker's own mix
; step (apps/tracker/trkplay.inc's MIXADD, to the instruction):
;
;   make covbench           -> build/covbench{360,720,144}.img, COVBENCH.O88
;                              alone
;
; Double-click it, press R (or click the window), and wait ~30 seconds with
; your hands off the mouse. The report is saved as COVBENCH.TXT beside it.
;
;   SHUT    the workload alone, for CB_TICKS ticks: the machine's own speed
;           at this code. Every other row is read against it.
;   ISR     os88spk_isrd as it ships - the library's ring, its grants, the
;           door - feeding the workload's port at each rate.
;   LEAN    the least an interrupt a sample can do: the sample from a ring
;           page in our own segment by a byte index, the port, the EOI, and
;           a count to the K-th entry that chains to the kernel.
;   POLL2,  NO interrupt a sample. Channel 2 runs as a square wave whose
;   POLL4   output toggles once a sample (mode 3 at 2N) and the WORKLOAD
;           reads it - port 62h bit 5 on a PC or XT, the 8255's "T/C2 OUT" -
;           every 2 or 4 mix steps, writing the next sample at each change.
;           IRQ0 is a five-instruction counter for the row: the kernel's
;           tick is OWED, not taken, and paid back when the row ends, so no
;           1,300-cycle period entry lands inside the play.
;   PIT4    WHAT SHIPS (apps/os88spk.inc's polled half): the clock is
;           channel 0's COUNT, latched, and a poll every 4 mix steps writes
;           EVERY sample due since the last - so a late poll makes samples
;           late and never loses them, where POLL2/POLL4 above see only that
;           channel 2's output changed and lose two samples to every gap
;           longer than one. Channel 0 runs a whole tick, IRQ0 owing it.
;   POLL2L  POLL2 with the kernel's tick LIVE (IRQ0 the kernel's, once a
;           rate period): what deferring the tick buys.
;
; Each row reads 1 - (its spans / SHUT's) as the output's SHARE of the
; machine, and that share x 4,773 / rate as its 4.77 MHz cycles a sample.
; A polled row also counts the samples it WROTE against the samples the PIT
; says were due: a poll that comes too late for an edge misses it, and a
; missed edge is a sample played late, so MISSED per mille is the polling's
; own honesty check. The workload never draws, so nothing else is timed.
;
; The port is the Covox's when the Sound page names one (os88spk_lptq), else
; 378h - a write to a parallel port with nothing on it, or to no port at all,
; is harmless, and the cost is the same. The ring is 80h throughout: the DAC
; holds the middle, so the bench is silent. The PC speaker is OFF in every
; row (61h bit 1 clear) - channel 2 still counts with its gate on.
;
; Prefix cb_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'COVBENCH', cb_entry

CB_TICKS    equ 32                  ; ticks a row runs: 1.76 s
CB_SPAN     equ 256                 ; mix steps a span
CB_RSIZE    equ 0x20                ; the library ring: 4096 << 2 = 16 KB
CB_RL       equ 16384

; -----------------------------------------------------------------------------
; MIXADD - Tracker's add pass, one channel, one step (trkplay.inc's macro to
; the instruction): ES:SI the sample, DS:BX the volume row, DS:DI the output,
; DX:SI the position, BP the fraction's step, the integer step patched in
; less one. AH is the one register it leaves free, which is what POLL uses.
; -----------------------------------------------------------------------------
%macro MIXADD 0
    es lodsb
    xlat
    add [di], al
    inc di
    add dx, bp
    db 0x83, 0xD6, 0                ; adc si, 0 (sign-extended imm8): a step
                                    ; of 1 + BP/65536
%endmacro

; -----------------------------------------------------------------------------
; POLL - has channel 2's output changed since the last look? AH bit 5 holds
; what it was. A change is a sample due: the next ring byte to the port. The
; ring is a page of our own segment indexed by [cb_rp]'s low byte, so the
; wrap is the byte's own; BX and DX are borrowed around the write. AL is
; MIXADD's scratch, reloaded by its next step.
; -----------------------------------------------------------------------------
%macro POLL 0
    in al, 0x62
    xor al, ah
    test al, 0x20
    jz %%n
    xor ah, 0x20
    push bx
    mov bx, [cs:cb_rp]
    mov al, [cs:bx]
    inc bl
    mov [cs:cb_rp], bx
    push dx
    mov dx, [cs:cb_port]
    out dx, al
    pop dx
    pop bx
    inc word [cs:cb_outs]
%%n:
%endmacro

; -----------------------------------------------------------------------------
; PITPOLL - os88spk's poll (OS88SPK_PNOW, then the due test): the time is
; channel 0's count, latched and negated, and a sample is due once it passes
; [cb_due]. AX spent
; -----------------------------------------------------------------------------
%macro PITPOLL 0
    xor al, al
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    neg ax
    sub ax, [cs:cb_due]
    js %%n
    call cb_pitout
%%n:
%endmacro

; -----------------------------------------------------------------------------
cb_entry:
    push si
    call cb_hint
    mov si, cb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [cb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    mov si, cb_onabout
    call OSAPI_ABOUT_SET
    clc
.out:
    pop si
    ret

cb_paint:
    call bl_paint
    ret

cb_onabout:
    push si
    mov word [bl_top], 0
    call bl_paint
    pop si
    ret

; R runs, S saves, everything else pages
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
    cmp bl, 's'
    je .save
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call cb_run
    call cb_repaint
    jmp short .out
.save:
    mov si, cb_f_out
    call bl_save
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
    push si
    mov [cb_win], si
    cmp byte [cb_ran], 0            ; never run: a click runs it
    jne .page
    push bx
    push cx
    push dx
    push di
    call cb_run
    call cb_repaint
    pop di
    pop dx
    pop cx
    pop bx
    jmp short .out
.page:
    mov al, ' '
    xor ah, ah
    call bl_key
    jc .out
    call bl_paint
.out:
    pop si
    pop ax
    ret

cb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [cb_win]
    call OSAPI_WM_GEOM
    jc .p
    push cx
    push dx
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    pop si                          ; SI = height
    pop cx                          ; CX = width
    mov bx, dx
    add cx, ax
    dec cx
    add dx, si
    dec dx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.p:
    mov si, [cb_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cb_hint:
    push si
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov si, cb_l_title
    call bl_sline
    call bl_blank
    mov si, cb_l_hint1
    call bl_sline
    mov si, cb_l_hint2
    call bl_sline
    pop si
    ret

; cb_geom - the content's geometry for bl_progress (spkbench's sp_geom)
cb_geom:
    push ax
    push bx
    push cx
    push dx
    mov bx, [cb_win]
    call OSAPI_WM_GEOM
    mov [bl_cw], cx
    mov [bl_ch], dx
    call OSAPI_WM_CONTENT
    mov [bl_cx], ax
    mov [bl_cy], dx
    mov ax, [bl_cw]
    mov cl, 3
    shr ax, cl
    cmp ax, BL_MAXLINE
    jbe .c
    mov ax, BL_MAXLINE
.c:
    mov [bl_vcols], ax
    mov ax, [bl_ch]
    shr ax, cl
    mov [bl_vrows], ax
    or ax, ax
    jz .r
    dec ax
.r:
    mov [bl_prows], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; cb_run - the whole bench
; =============================================================================
cb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov byte [cb_ran], 1
    call cb_geom
    mov si, cb_l_title
    call bl_sline
    call OSAPI_CPU_INFO
    mov si, cb_l_cpu8086
    cmp al, CPU_8086
    je .tier
    mov si, cb_l_cpu286
    cmp al, CPU_286
    je .tier
    mov si, cb_l_cpu386
.tier:
    mov di, si
    mov si, cb_l_cpu
    call bl_kvs
    call cb_setup
    jc .noring
    mov ax, [cb_port]
    xor dx, dx
    mov si, cb_l_port
    call cb_kvh
    call bl_blank
    mov si, cb_p_run
    call bl_progress
    mov ax, cb_brk                  ; EVERY ROW, in our own bracket
    mov bx, [cb_win]
    mov cx, FSXF_RATE
    mov dx, 0xFFFF
    mov di, cb_hook
    call OSAPI_FSX_RUN
    jnc .ran
    mov si, cb_l_norate
    call bl_sline
    jmp short .end
.noring:
    mov si, cb_l_noring
    call bl_sline
    jmp short .end
.ran:
    call cb_report
.end:
    call bl_blank
    mov si, cb_l_end
    call bl_sline
    mov si, cb_f_out                ; saved without being asked
    call bl_save
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cb_hook:                            ; FSXF_RATE's hook: nothing to do
    ret

; cb_kv - bl_kv with the value field 8 wide (preserves all)
cb_kv:
    push cx
    mov cx, 8
    call bl_kv
    pop cx
    ret

; cb_kvh - the port, in hex (preserves all): AX = it, SI = the label
cb_kvh:
    push ax
    push bx
    push cx
    push di
    mov di, cb_hexb
    mov cx, 4
.h:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    mov bl, al
    and bl, 0x0F
    add bl, '0'
    cmp bl, '9'
    jbe .d
    add bl, 'A' - '0' - 10
.d:
    mov [di], bl
    inc di
    loop .h
    mov byte [di], 'h'
    mov byte [di+1], 0
    mov di, cb_hexb
    call bl_kvs
    pop di
    pop cx
    pop bx
    pop ax
    ret

; cb_setup - the library's ring claimed (once), the source, the volume row,
; the output ring of 80h, and the port. out: CF = 1 no memory
cb_setup:
    push ax
    push cx
    push dx
    push di
    cmp word [cb_rseg], 0
    jne .src
    mov ax, (CB_RL + 272 + 1023) / 1024
    call OSAPI_MEM_CLAIM
    jc .out
    mov [cb_rseg], dx
.src:
    push ds
    pop es
    cld
    mov di, cb_src                  ; a triangle that walks the levels
    xor cx, cx
    mov al, 0x80
    mov dl, 3
.t:
    stosb
    add al, dl
    cmp al, 0xF0
    jb .n
    neg dl
.n:
    cmp al, 0x10
    ja .n2
    mov dl, 3
.n2:
    inc cx
    cmp cx, 512
    jb .t
    mov di, cb_vt                   ; a quarter-volume row, Tracker's shape
    xor cx, cx
.v:
    mov al, cl
    sar al, 1
    sar al, 1
    stosb
    inc cl
    jnz .v
    mov di, cb_ring                 ; the output page: silence
    mov cx, 256
    mov al, 0x80
    rep stosb
    mov dx, 0x0378
    call os88spk_lptq               ; DX = the Covox's, when one is named
    mov [cb_port], dx
    clc
.out:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cb_brk - THE BRACKET'S BODY (DS = CS, SI = our window)
; -----------------------------------------------------------------------------
cb_brk:
    mov ax, cb_w0                   ; SHUT
    mov [cb_body], ax
    call cb_spans_ticks
    mov [cb_n0], ax
    xor bx, bx
.r:
    mov ax, [cb_rates + bx]
    or ax, ax
    jz .live
    mov [cb_cur], ax
    call cb_n                       ; [cb_nn] = N
    mov di, bx                      ; a row's results: 8 words a rate
    shl di, 1
    shl di, 1
    shl di, 1
    add di, cb_res
    call cb_row_isr
    mov [di], ax
    call cb_row_lean
    mov [di+2], ax
    mov ax, cb_w2
    xor cl, cl                      ; deferred tick
    call cb_row_poll
    mov [di+4], ax
    mov [di+6], dx
    mov ax, cb_w4
    xor cl, cl
    call cb_row_poll
    mov [di+8], ax
    mov [di+10], dx
    call cb_row_pit
    mov [di+12], ax
    mov [di+14], dx
    add bx, 2
    jmp short .r
.live:
    mov ax, [cb_rates + 4]          ; POLL2 with the tick live, at the
    mov [cb_cur], ax                ; highest rate
    call cb_n
    mov ax, cb_w2
    mov cl, 1
    call cb_row_poll
    mov [cb_lv], ax
    mov [cb_lv+2], dx
    ret

; cb_n - [cb_nn] = 1,193,182 / [cb_cur]
cb_n:
    push ax
    push bx
    push dx
    mov bx, [cb_cur]
    mov dx, 0x0012
    mov ax, 0x34DE
    div bx
    mov [cb_nn], ax
    pop dx
    pop bx
    pop ax
    ret

; cb_edge - wait for a tick to begin. out: AX = it
cb_edge:
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
.w:
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .w
    pop bx
    ret

; cb_spans_ticks - spans of [cb_body] for CB_TICKS kernel ticks. out: AX =
; spans. Feeds the library ring as it goes (TOTAL = CONS + RL), which costs a
; row nothing measurable and keeps the ISR row from going dry
cb_spans_ticks:
    push bx
    push cx
    push dx
    push es
    call cb_edge
    mov bx, ax
    xor cx, cx
.l:
    call [cb_body]
    inc cx
    mov es, [cb_rseg]
    mov ax, [es:CB_RL + 2]
    add ax, CB_RL
    mov [es:CB_RL], ax
    call OSAPI_GET_TICKS
    sub ax, bx
    cmp ax, CB_TICKS
    jb .l
    mov ax, cx
    pop es
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; the workloads: one span, CB_SPAN mix steps. cb_w0 plain, cb_w2 / cb_w4 with
; a POLL every 2 / 4 steps. AH is the poll's level, kept in [cb_lvl] across
; spans. ES = DS = CS
; -----------------------------------------------------------------------------
%macro CB_WORK 3                    ; %1 the label, %2 steps a poll (0: none),
                                    ; %3 the poll (POLL or PITPOLL)
%1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    pop es
    mov ah, [cb_lvl]
    mov si, cb_src
    mov di, cb_dst
    mov bx, cb_vt
    xor dx, dx
    mov bp, 0x0A00
    mov cx, CB_SPAN / 8
%%l:
%assign %%i 0
%rep 8
    MIXADD
%assign %%i %%i + 1
%if %2 > 0
%if (%%i % %2) == 0
    %3
%endif
%endif
%endrep
    dec cx
    jz %%d
    jmp %%l
%%d:
    mov [cb_lvl], ah
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endmacro

    CB_WORK cb_w0, 0, POLL
    CB_WORK cb_w2, 2, POLL
    CB_WORK cb_w4, 4, POLL
    CB_WORK cb_wp4, 4, PITPOLL

; cb_pitout - AX = the lateness: every sample due written (os88spk_pout's
; shape, the bench's ring and count)
cb_pitout:
    push bx
    push cx
    push dx
    mov cx, ax
    mov dx, [cs:cb_port]
.one:
    mov ax, [cs:cb_nn]
    add [cs:cb_due], ax
    mov bx, [cs:cb_rp]
    mov al, [cs:bx]
    inc bl
    mov [cs:cb_rp], bx
    out dx, al
    inc word [cs:cb_outs]
    sub cx, [cs:cb_nn]
    jns .one
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cb_row_isr - the library's own Covox ISR at [cb_cur]. out: AX = spans, 0
; refused
; -----------------------------------------------------------------------------
cb_row_isr:
    push dx
    push di
    mov dx, [cb_port]
    call os88spk_lpt
    mov dx, [cb_cur]
    mov di, [cb_rseg]
    mov ah, CB_RSIZE
    xor cl, cl
    call os88spk_init
    jc .no
    call cb_fill
    call os88spk_go
    jc .no
    mov word [cb_body], cb_w0
    call cb_spans_ticks
    call os88spk_stop
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop dx
    ret

; cb_fill - the library ring full of 80h, TOTAL = RL ahead of CONS = 0
cb_fill:
    push ax
    push cx
    push di
    push es
    mov es, [cb_rseg]
    xor di, di
    mov cx, CB_RL
    mov al, 0x80
    cld
    rep stosb
    mov word [es:CB_RL], CB_RL
    mov word [es:CB_RL + 2], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cb_open - the door at N = [cb_nn], and channel 0 at AX counts with IRQ0 at
; SI (0: the kernel's own vector left in place). IF = 0 throughout, as the
; door binds. out: CF = 0, [cb_k] = K, [cb_chain] = the vector the door left;
; CF = 1 refused
; -----------------------------------------------------------------------------
cb_open:
    push ax
    push bx
    push dx
    pushf
    cli
    push ax
    mov dx, [cb_nn]
    mov bx, cs
    xor al, al
    call OSAPI_FSX_SPK              ; AX = K
    pop dx                          ; DX = channel 0's divisor
    jc .no
    mov [cb_k], ax
    mov [cb_kc], ax
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x20]
    mov [cs:cb_chain], ax
    mov ax, [0x22]
    mov [cs:cb_chain+2], ax
    or si, si
    jz .vec
    mov [0x20], si
    mov [0x22], cs
.vec:
    pop ds
    mov al, 0x34                    ; channel 0: mode 2 at DX
    out 0x43, al
    mov al, dl
    out 0x40, al
    mov al, dh
    out 0x40, al
    in al, 0x61                     ; the speaker OFF, channel 2's gate ON
    and al, 0xFC
    or al, 0x01
    out 0x61, al
    popf
    clc
    jmp short .out
.no:
    popf
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; cb_close - the door shut (the vector, channel 0 and channel 2 put back)
cb_close:
    push ax
    pushf
    cli
    mov al, 1
    call OSAPI_FSX_SPK
    popf
    pop ax
    ret

; -----------------------------------------------------------------------------
; cb_row_lean - the least an ISR a sample can be, at [cb_cur]. out: AX =
; spans, 0 refused
; -----------------------------------------------------------------------------
cb_row_lean:
    push si
    mov ax, [cb_nn]
    mov si, cb_lisr
    call cb_open
    jc .no
    mov word [cb_body], cb_w0
    call cb_spans_ticks
    call cb_close
    jmp short .out
.no:
    xor ax, ax
.out:
    pop si
    ret

cb_lisr:
    push ax
    push bx
    mov bx, [cs:cb_rp]
    mov al, [cs:bx]
    inc bl
    mov [cs:cb_rp], bx
    push dx
    mov dx, [cs:cb_port]
    out dx, al
    pop dx
    pop bx
    dec word [cs:cb_kc]
    jz .chain
    mov al, 0x20
    out 0x20, al
    pop ax
    iret
.chain:
    mov ax, [cs:cb_k]
    mov [cs:cb_kc], ax
    pop ax
    jmp far [cs:cb_chain]

; -----------------------------------------------------------------------------
; cb_row_poll - AX = the workload (cb_w2 / cb_w4), CL = 0 the tick OWED (IRQ0
; our counter) or 1 LIVE (IRQ0 the kernel's), at [cb_cur]. Channel 0 runs a
; whole rate period, K x N, so IRQ0 comes once a period either way; channel 2
; is a square wave at 2N, its output changing once a sample.
; out: AX = spans as if the row were CB_TICKS ticks of 65,536 counts, DX =
; missed per mille; AX = 0 refused
; -----------------------------------------------------------------------------
cb_row_poll:
    push bx
    push cx
    push si
    push di
    mov [cb_body], ax
    mov [cb_live], cl
    mov word [cb_owed], 0
    mov word [cb_outs], 0
    mov ax, 0xFFFF                  ; K x N: the door's own trim of it
    xor dx, dx
    div word [cb_nn]
    mul word [cb_nn]                ; AX = K x N
    mov si, cb_tisr
    cmp byte [cb_live], 0
    je .op
    mov si, cb_tisrl
.op:
    call cb_open
    jc .no
    pushf                           ; channel 2: mode 3 at 2N, lobyte then
    cli                             ; hibyte - its output a square wave that
    mov al, 0xB6                    ; changes every N counts
    out 0x43, al
    mov ax, [cb_nn]
    shl ax, 1
    out 0x42, al
    mov al, ah
    out 0x42, al
    in al, 0x62
    and al, 0x20
    mov [cb_lvl], al
    mov word [cb_outs], 0
    mov word [cb_owed], 0
    popf
    xor cx, cx                      ; spans until CB_TICKS rate periods have
.l:                                 ; come - owed or, live, taken
    call [cb_body]
    inc cx
    cmp word [cb_owed], CB_TICKS
    jb .l
    pushf
    cli
    mov ax, [cb_outs]
    mov [cb_wrote], ax
    popf
    call cb_close
    cmp byte [cb_live], 0
    jne .rep
    mov si, [cb_owed]               ; THE TICKS PAID: the kernel's IRQ0
.pay:                               ; entered once a period owed, as the
    or si, si                       ; interrupt would have entered it
    jz .rep
    pushf
    cli
    call far [cb_chain]
    dec si
    jmp short .pay
.rep:
    mov ax, 0xFFFF                  ; spans as CB_TICKS 65,536-count ticks:
    xor dx, dx                      ; x 65,536 / (K x N)
    div word [cb_nn]
    mul word [cb_nn]
    mov bx, ax
    mov dx, cx
    xor ax, ax
    div bx
    push ax
    mov ax, [cb_owed]               ; DUE: K a period owed
    mul word [cb_k]                 ; DX:AX
    mov bx, ax                      ; (< 65,536 for CB_TICKS = 32 at 11 kHz)
    sub ax, [cb_wrote]
    jnc .pos
    xor ax, ax
.pos:
    mov cx, 1000
    mul cx
    or bx, bx
    jz .z
    cmp dx, bx
    jae .z
    div bx
    mov dx, ax
    jmp short .m
.z:
    xor dx, dx
.m:
    pop ax
    jmp short .out
.no:
    xor ax, ax
    xor dx, dx
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

cb_tisr:                            ; IRQ0 while the tick is OWED
    inc word [cs:cb_owed]
    push ax
    mov al, 0x20
    out 0x20, al
    pop ax
    iret

cb_tisrl:                           ; ...and while it is LIVE: counted, then
    inc word [cs:cb_owed]           ; the kernel's own entry at once
    jmp far [cs:cb_chain]

; -----------------------------------------------------------------------------
; cb_row_pit - WHAT SHIPS, at [cb_cur]: channel 0 a whole tick (65,536) with
; IRQ0 owing it, the workload polling the latched count every 4 steps and
; writing every sample due. out: AX = spans (the row's ticks ARE 65,536
; counts), DX = missed per mille (due less written); AX = 0 refused
; -----------------------------------------------------------------------------
cb_row_pit:
    push bx
    push cx
    push si
    mov word [cb_body], cb_wp4
    xor ax, ax                      ; channel 0: divisor 0, 65,536 counts
    mov si, cb_tisr
    call cb_open
    jc .no
    pushf
    cli
    xor al, al                      ; the first sample due a sample from now
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    neg ax
    add ax, [cb_nn]
    mov [cb_due], ax
    mov word [cb_outs], 0
    mov word [cb_owed], 0
    popf
    xor cx, cx
.l:
    call [cb_body]
    inc cx
    cmp word [cb_owed], CB_TICKS
    jb .l
    pushf
    cli
    mov ax, [cb_outs]
    mov [cb_wrote], ax
    popf
    call cb_close
    mov si, [cb_owed]               ; the ticks paid
.pay:
    or si, si
    jz .rep
    pushf
    cli
    call far [cb_chain]
    dec si
    jmp short .pay
.rep:
    push cx
    mov dx, [cb_owed]               ; DUE: owed x 65,536 / N
    xor ax, ax
    div word [cb_nn]
    mov bx, ax
    sub ax, [cb_wrote]
    jnc .pos
    xor ax, ax
.pos:
    mov cx, 1000
    mul cx
    or bx, bx
    jz .z
    cmp dx, bx
    jae .z
    div bx
    mov dx, ax
    jmp short .m
.z:
    xor dx, dx
.m:
    pop ax                          ; spans
    jmp short .out
.no:
    xor ax, ax
    xor dx, dx
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cb_report - the rows as lines
; -----------------------------------------------------------------------------
cb_report:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, cb_l_hshut
    call bl_sline
    mov si, cb_l_ticks
    mov ax, CB_TICKS
    xor dx, dx
    call cb_kv
    mov si, cb_l_shut
    mov ax, [cb_n0]
    xor dx, dx
    call cb_kv
    mov ax, [cb_n0]                 ; a mix step's cycles: 32 x 65,536 x 4
    call cb_cyc                     ; / (spans x 256)
    xor dx, dx
    mov si, cb_l_step
    call cb_kv
    xor bx, bx
.r:
    mov ax, [cb_rates + bx]
    or ax, ax
    jz .live
    mov [cb_cur], ax
    call bl_blank
    mov si, cb_l_rate
    xor dx, dx
    call cb_kv
    mov di, bx
    shl di, 1
    shl di, 1
    shl di, 1
    add di, cb_res
    mov ax, [di]
    mov si, cb_l_isr
    call cb_share_lines
    mov ax, [di+2]
    mov si, cb_l_lean
    call cb_share_lines
    mov ax, [di+4]
    mov si, cb_l_poll2
    call cb_share_lines
    mov ax, [di+6]
    xor dx, dx
    mov si, cb_l_miss2
    call cb_kv
    mov ax, [di+8]
    mov si, cb_l_poll4
    call cb_share_lines
    mov ax, [di+10]
    xor dx, dx
    mov si, cb_l_miss4
    call cb_kv
    mov ax, [di+12]
    mov si, cb_l_pit4
    call cb_share_lines
    mov ax, [di+14]
    xor dx, dx
    mov si, cb_l_misp4
    call cb_kv
    add bx, 2
    jmp .r
.live:
    call bl_blank
    mov ax, [cb_rates + 4]
    mov [cb_cur], ax
    mov si, cb_l_lrate
    xor dx, dx
    call cb_kv
    mov ax, [cb_lv]
    mov si, cb_l_poll4l
    call cb_share_lines
    mov ax, [cb_lv+2]
    xor dx, dx
    mov si, cb_l_miss4l
    call cb_kv
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cb_share_lines - AX = a row's spans, SI = its label pair (share, cycles):
; the share per mille and the cycles a sample at [cb_cur], or "refused"
cb_share_lines:
    push ax
    push cx
    push dx
    or ax, ax
    jnz .have
    push si
    mov si, cb_l_refused
    call bl_sline
    pop si
    jmp short .out
.have:
    call cb_share                   ; AX = per mille
    push ax
    xor dx, dx
    call cb_kv                      ; "<name> share, p.m."
    pop ax
    mov cx, 4773                    ; cycles a sample: pm x 4773 / R
    mul cx
    mov cx, [cb_cur]
    div cx
    xor dx, dx
    add si, CB_LW                   ; the pair's second line
    call cb_kv
.out:
    pop dx
    pop cx
    pop ax
    ret

; cb_cyc - AX = spans in CB_TICKS -> AX = 4.77 MHz cycles a mix step:
; 32 x 65,536 x 4 / (spans x 256) = 32,768 / spans
cb_cyc:
    push cx
    push dx
    mov cx, ax
    or cx, cx
    jz .z
    mov ax, 32768
    xor dx, dx
    div cx
    jmp short .o
.z:
    xor ax, ax
.o:
    pop dx
    pop cx
    ret

; cb_share - AX = spans open -> AX = the output's share, per mille:
; 1000 - 1000 x open / shut
cb_share:
    push cx
    push dx
    mov cx, 1000
    mul cx
    mov cx, [cb_n0]
    or cx, cx
    jz .z
    cmp dx, cx
    jae .z
    div cx
    mov cx, 1000
    sub cx, ax
    jnc .p
    xor cx, cx
.p:
    mov ax, cx
    jmp short .o
.z:
    xor ax, ax
.o:
    pop dx
    pop cx
    ret

%include "os88spk.inc"
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================
cb_tpl:
    dw 7, 22, 632, 448
    dw cb_ttl, cb_paint, cb_onkey, cb_onclick
cb_ttl:     db 'Covox Bench', 0

cb_rates:   dw 5512, 8000, 11025, 0

cb_f_out:   db 'COVBENCH.TXT', 0
cb_l_title: db 'COVBENCH - what feeding a Covox costs this machine (SPEC.md 34.14.3)', 0
cb_l_hint1: db 'R (or a click) runs it: ~30 s, hands off the mouse. Silent: the', 0
cb_l_hint2: db 'DAC holds its middle. The report is saved as COVBENCH.TXT.', 0
cb_l_cpu:   db 'CPU tier', 0
cb_l_cpu8086: db '8086/8088 class', 0
cb_l_cpu286: db '286', 0
cb_l_cpu386: db '386 or better', 0
cb_l_port:  db 'port', 0
cb_l_noring: db 'NO MEMORY for the library ring: nothing was run', 0
cb_l_norate: db 'the kernel refused FSXF_RATE: nothing was run', 0
cb_l_hshut: db '-- the workload (Tracker', 39, 's mix step), output SHUT --', 0
cb_l_ticks: db 'ticks a row', 0
cb_l_shut:  db 'spans (256 steps) done', 0
cb_l_step:  db '...cycles a mix step', 0
cb_l_rate:  db 'rate, Hz', 0
cb_l_lrate: db 'tick live, rate Hz', 0
cb_l_refused: db '  refused here', 0
; each share label is followed, CB_LW bytes on, by its cycles label
CB_LW       equ 24
%macro CBL 2
%1: db %2, 0
    times CB_LW - ($ - %1) db 0
%endmacro
    CBL cb_l_isr,    '  isr share, p.m.'
    CBL cb_l_isrc,   '  isr cycles a sample'
    CBL cb_l_lean,   '  lean share, p.m.'
    CBL cb_l_leanc,  '  lean cycles a sample'
    CBL cb_l_poll2,  '  poll2 share, p.m.'
    CBL cb_l_poll2c, '  poll2 cycles a sample'
    CBL cb_l_poll4,  '  poll4 share, p.m.'
    CBL cb_l_poll4c, '  poll4 cycles a sample'
    CBL cb_l_pit4,   '  pit4 share, p.m.'
    CBL cb_l_pit4c,  '  pit4 cycles a sample'
    CBL cb_l_poll4l, '  poll2L share, p.m.'
    CBL cb_l_poll4lc,'  poll2L cycles a sampl'
cb_l_miss2: db '  poll2 missed, p.m.', 0
cb_l_misp4: db '  pit4 missed, p.m.', 0
cb_l_miss4: db '  poll4 missed, p.m.', 0
cb_l_miss4l: db '  poll2L missed, p.m.', 0
cb_l_end:   db 'done - saved as COVBENCH.TXT', 0
cb_p_run:   db 'running: fourteen rows (~30 s)...', 0

; the poll's and the lean ISR's shared state: in the IMAGE, because both are
; read and written through CS by code that may run with any DS
cb_rp:      dw cb_ring              ; the output page's next byte (low byte
                                    ; the index: the page wraps by itself)
cb_port:    dw 0x0378
cb_outs:    dw 0                    ; samples a polled row wrote
cb_owed:    dw 0                    ; rate periods owed the kernel
cb_due:     dw 0                    ; PIT4: the next sample's due time
cb_k:       dw 0                    ; K, samples a rate period
cb_kc:      dw 0                    ; the lean ISR's count to the chain
cb_chain:   dw 0, 0                 ; the kernel's IRQ0, far

    align 256, db 0
cb_ring:    times 256 db 0x80       ; the output page

CB_BSS_OWN  equ ((96 + 512 + 256 + 256 + 511) / 512) * 512
                                    ; benchlib's base must be 512-aligned
    OS88_BSS CB_BSS_OWN + BL_BSS_SIZE
    OS88_IMAGE_END

; --- loader-zeroed bss ------------------------------------------------------
cb_win      equ os88_image_end + 0
cb_ran      equ os88_image_end + 2
cb_rseg     equ os88_image_end + 4
cb_n0       equ os88_image_end + 6
cb_cur      equ os88_image_end + 8
cb_nn       equ os88_image_end + 10
cb_body     equ os88_image_end + 12
cb_lvl      equ os88_image_end + 14
cb_live     equ os88_image_end + 16
cb_wrote    equ os88_image_end + 18
cb_lv       equ os88_image_end + 20     ; the live row: spans, missed
cb_hexb     equ os88_image_end + 24     ; 6 bytes
cb_res      equ os88_image_end + 32     ; 8 words a rate, 3 rates = 48 bytes
cb_src      equ os88_image_end + 96
cb_dst      equ cb_src + 512
cb_vt       equ cb_dst + 256

    BL_BSS os88_image_end + CB_BSS_OWN
