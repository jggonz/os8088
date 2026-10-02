; =============================================================================
; os8088 - tests/spkbench/spkbench.asm
;
; SPKBENCH: what the PC SPEAKER costs THIS machine (SPEC.md 34.11, 45.25).
;
;   make spkbench           -> build/spkbench{360,720,144}.img, SPKBENCH.O88
;                              alone
;
; Double-click it, press R (or click the window), and wait ~12 seconds with
; your hands off the mouse. The report is saved as SPKBENCH.TXT beside it.
;
; WHY IT EXISTS. Tracker's speaker path predicts each rate's load from a bench
; it runs on the machine (the shaper, the mixer) and ONE constant it cannot
; time there: what the sample ISR costs a sample (TSP_CS in trkspk.inc, built
; on ~325 cycles off MartyPC's 5150). The owner's 5150 and MartyPC's agree
; that a module the predictor puts at 95% falls behind at 5,512 Hz within a
; minute, so the prediction is optimistic - and nobody has timed the ISR on
; iron or on a V20. This does, the same way on every machine, and prints each
; rate's measured share beside the one Tracker assumes (TSP_ASSUME):
;
;   1. a FIXED WORKLOAD - the speaker shaper (apps/os88spkfx.inc) over a
;      256-sample span, again and again - counted for SP_TICKS ticks with the
;      speaker SHUT. That is the machine's own speed at this code;
;   2. the same workload with the speaker PLAYING a ring of silence at
;      4,800, 5,512 and 8,000 Hz. The workload now gets what the ISR leaves,
;      so 1 - (open / shut) IS THE ISR's SHARE of the machine at that rate,
;      measured, with every interrupt the machine takes included (the ROM's
;      tick, the keyboard, the mouse, refresh - whatever iron adds);
;   3. Tracker's load-time filter (SPEC.md 45.25, tsp_natural's loop) the same
;      way, speaker shut;
;   4. RAM READ speed in each 64 KB bank of conventional memory - 4 KB with
;      `rep lodsw`, benchlib's exact PIT method - for PERFORMANCE.md Part 8.2's
;      question: is a memory card slower than the planar?
;
; The 4.77 MHz CYCLE columns are counts x 4 (one PIT count is four 4.77 MHz
; clocks): exact on a PC or XT, and on a faster machine they read in 4.77 MHz
; cycles - a 7.16 MHz machine shows fewer of them, which is the point.
;
; Prefix sp_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SPKBENCH', sp_entry

SP_TICKS    equ 32                  ; ticks a speaker row runs: 1.76 s, 32
                                    ; spans of 256 a tick on a 5150, so one
                                    ; span either way is ~0.3%
SP_SPAN     equ 256
TSP_ASSUME  equ 325                 ; the ISR's cycles a sample that
                                    ; trkspk.inc's TSP_CS = 89 was built on
SP_RSIZE    equ 0x20                ; the ring: 4096 << 2 = 16 KB, 2 s at 8 kHz
SP_RL       equ 16384
SP_BANKN    equ 8                   ; iterations of a bank row (4 KB each)

; -----------------------------------------------------------------------------
sp_entry:
    push si
    call sp_hint
    mov si, sp_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [sp_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    mov si, sp_onabout
    call OSAPI_ABOUT_SET
    clc
.out:
    pop si
    ret

sp_paint:
    call bl_paint
    ret

sp_onabout:
    push si
    mov word [bl_top], 0
    call bl_paint
    pop si
    ret

; R runs, S saves, everything else pages
sp_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [sp_win], si
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
    call sp_run
    call sp_repaint
    jmp short .out
.save:
    mov si, sp_f_out
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

sp_onclick:
    push ax
    push si
    mov [sp_win], si
    cmp byte [sp_ran], 0            ; never run: a click runs it
    jne .page
    push bx
    push cx
    push dx
    push di
    call sp_run
    call sp_repaint
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

sp_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [sp_win]
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
    mov si, [sp_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the invitation, before anything is run
sp_hint:
    push si
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov si, sp_l_title
    call bl_sline
    call bl_blank
    mov si, sp_l_hint1
    call bl_sline
    mov si, sp_l_hint2
    call bl_sline
    pop si
    ret

; =============================================================================
; sp_run - the whole bench
; =============================================================================
sp_run:
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
    mov byte [sp_ran], 1
    call sp_geom                    ; bl_progress draws before bl_paint

    mov si, sp_l_title
    call bl_sline
    call OSAPI_CPU_INFO
    mov si, sp_l_cpu8086
    cmp al, CPU_8086
    je .tier
    mov si, sp_l_cpu286
    cmp al, CPU_286
    je .tier
    mov si, sp_l_cpu386
.tier:
    mov di, si
    mov si, sp_l_cpu
    call bl_kvs
    call bl_blank

    call sp_setup                   ; the ring, the source span
    jc .noring
    mov si, sp_p_spk
    call bl_progress
    mov ax, sp_brk                  ; THE SPEAKER ROWS, in our own bracket
    mov bx, [sp_win]
    mov cx, FSXF_RATE
    mov dx, 0xFFFF
    mov di, sp_hook
    call OSAPI_FSX_RUN
    jnc .ran
    mov si, sp_l_norate
    call bl_sline
    jmp short .ram
.noring:
    mov si, sp_l_noring
    call bl_sline
    jmp short .ram
.ran:
    call sp_report
.ram:
    call bl_blank
    mov si, sp_p_ram
    call bl_progress
    call sp_banks
    call bl_blank
    mov si, sp_l_end
    call bl_sline
    mov si, sp_f_out                ; saved without being asked
    call bl_save
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sp_hook:                            ; FSXF_RATE's hook: nothing to do
    ret

; sp_geom - the content's geometry for bl_progress (sysbench's sb_geom)
sp_geom:
    push ax
    push bx
    push cx
    push dx
    mov bx, [sp_win]
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

; sp_kv - bl_kv with the value field 8 wide (preserves all)
sp_kv:
    push cx
    mov cx, 8
    call bl_kv
    pop cx
    ret

; sp_setup - the ring claimed (once) and the source span, a triangle that
; walks every level. out: CF = 1 no memory
sp_setup:
    push ax
    push cx
    push dx
    push di
    cmp word [sp_rseg], 0
    jne .src
    mov ax, (SP_RL + 272 + 1023) / 1024
    call OSAPI_MEM_CLAIM
    jc .out
    mov [sp_rseg], dx
.src:
    mov di, sp_src
    xor cx, cx
    mov al, 0x80
    mov dl, 3                       ; step
.t:
    mov [di], al
    inc di
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
    cmp cx, SP_SPAN
    jb .t
    clc
.out:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sp_brk - THE BRACKET'S BODY (DS = CS, SI = our window). Every row here runs
; with interrupts ON: what the ISR takes is the thing measured
; -----------------------------------------------------------------------------
sp_brk:
    mov ax, 8000                    ; the shaper's tables need a rate: its
    call sp_ringat                  ; count table is the ring's
    mov di, sp_fam
    mov ax, SPKFX_PRE_DIFF          ; AH = 0: the carrier slide, as Audio
    call os88spkfx_init
    call sp_shaper                  ; 1. SHUT
    mov [sp_n0], ax
    xor bx, bx
.r:
    mov ax, [sp_rates + bx]         ; 2. OPEN at each rate
    or ax, ax
    jz .f
    call sp_ringat
    jc .skip
    call sp_fill
    call os88spk_go
    jc .skip
    call sp_shaper
    call os88spk_stop
    mov [sp_nr + bx], ax
    jmp short .nx
.skip:
    mov word [sp_nr + bx], 0
.nx:
    add bx, 2
    jmp short .r
.f:
    call sp_filter                  ; 3. the load filter
    mov [sp_nf], ax
    ret

; sp_ringat - AX = rate: the ring's table for it. CF = 1 refused
sp_ringat:
    push ax
    push cx
    push dx
    push di
    mov dx, ax
    mov di, [sp_rseg]
    mov ah, SP_RSIZE
    xor cl, cl
    call os88spk_init
    pop di
    pop dx
    pop cx
    pop ax
    ret

; sp_fill - the ring FULL of a count of 1 - the quietest pulse - so the ISR
; plays and never grants a dry run; TOTAL = RL ahead of CONS = 0
sp_fill:
    push ax
    push cx
    push di
    push es
    mov es, [sp_rseg]
    xor di, di
    mov cx, SP_RL
    mov al, 1
    cld
    rep stosb
    mov word [es:SP_RL], SP_RL      ; TOTAL
    mov word [es:SP_RL + 2], 0      ; CONS
    pop es
    pop di
    pop cx
    pop ax
    ret

; sp_edge - wait for a tick to begin. out: AX = it
sp_edge:
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
.w:
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .w
    pop bx
    ret

; sp_shaper - spans of the shaper for SP_TICKS ticks. out: AX = spans
sp_shaper:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    call sp_edge
    mov bx, ax
    xor dx, dx
.l:
    mov si, sp_src
    mov cx, SP_SPAN
    call os88spkfx_level
    mov si, sp_src
    mov di, sp_dst
    mov cx, SP_SPAN
    call os88spkfx_emit
    inc dx
    call OSAPI_GET_TICKS
    sub ax, bx
    cmp ax, SP_TICKS
    jb .l
    mov ax, dx
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; sp_filter - spans of Tracker's load filter (apps/tracker/trkspk.inc's
; tsp_natural, its loop to the instruction) for SP_TICKS ticks, over a copy
; of the source. out: AX = spans
sp_filter:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call sp_edge
    mov [sp_t0], ax
    xor bx, bx                      ; BX = spans
.s:
    push bx
    mov si, sp_dst
    mov cx, SP_SPAN
    xor di, di
    xor bp, bp
.b:
    mov ah, [si]
    xor al, al
    sar ax, 1
    mov dx, ax
    sub ax, bp
    mov bp, dx
    mov dx, di
    sar dx, 1
    sar dx, 1
    sar dx, 1
    sub di, dx
    add di, ax
    mov ax, di
    mov [si], ah
    inc si
    loop .b
    pop bx
    inc bx
    call OSAPI_GET_TICKS
    sub ax, [sp_t0]
    cmp ax, SP_TICKS
    jb .s
    mov ax, bx
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sp_report - the speaker rows as lines
; -----------------------------------------------------------------------------
sp_report:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, sp_l_hspk
    call bl_sline
    mov si, sp_l_ticks
    mov ax, SP_TICKS
    xor dx, dx
    call sp_kv
    ; the shaper, shut: spans, samples a second, 4.77 MHz cycles a sample
    mov si, sp_l_shut
    mov ax, [sp_n0]
    xor dx, dx
    call sp_kv
    mov ax, [sp_n0]
    call sp_rate
    mov si, sp_l_sps
    call sp_kv
    mov ax, [sp_n0]
    call sp_cyc                     ; AX = cycles a sample
    mov [sp_cs], ax
    xor dx, dx
    mov si, sp_l_cps
    call sp_kv
    ; the filter
    mov si, sp_l_filt
    mov ax, [sp_nf]
    xor dx, dx
    call sp_kv
    mov ax, [sp_nf]
    call sp_cyc
    xor dx, dx
    mov si, sp_l_fcps
    call sp_kv
    call bl_blank
    ; each rate: the share of the machine the ISR took, and a pulse's cycles
    mov si, sp_l_hisr
    call bl_sline
    xor bx, bx
.r:
    mov ax, [sp_rates + bx]
    or ax, ax
    jz .done
    mov [sp_cur], ax
    mov si, sp_l_rate
    xor dx, dx
    call sp_kv
    mov ax, [sp_nr + bx]
    or ax, ax
    jnz .have
    mov si, sp_l_refused
    call bl_sline
    jmp short .nx
.have:
    mov si, sp_l_open
    xor dx, dx
    call sp_kv
    call sp_share                   ; AX = per mille
    mov [sp_pm], ax
    xor dx, dx
    mov si, sp_l_share
    call sp_kv
    mov ax, [sp_pm]                 ; a pulse, 4.77 MHz cycles: pm x 4773 / R
    mov cx, 4773
    mul cx
    mov cx, [sp_cur]
    div cx
    mov [sp_pc], ax
    xor dx, dx
    mov si, sp_l_pulse
    call sp_kv
    mov ax, TSP_ASSUME              ; what Tracker's predictor assumes the
    mul word [sp_cur]               ; ISR takes: TSP_ASSUME cycles a sample,
    mov cx, 4773                    ; as per mille of the machine
    div cx
    xor dx, dx
    mov si, sp_l_tsp
    call sp_kv
.nx:
    add bx, 2
    jmp .r
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sp_rate - AX = spans in SP_TICKS -> DX:AX = samples a second:
; spans x 256 x 18.2065 / 32 = spans x 14565 / 100
sp_rate:
    push cx
    mov cx, 14565
    mul cx
    mov cx, 100
    call bl_div32
    pop cx
    ret

; sp_cyc - AX = spans -> AX = 4.77 MHz cycles a sample:
; 32 ticks x 65536 counts x 4 / (spans x 256) = 32768 / spans
sp_cyc:
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

; sp_share - AX = spans open -> AX = the ISR's share, per mille:
; 1000 - 1000 x open / shut
sp_share:
    push cx
    push dx
    mov cx, 1000
    mul cx
    mov cx, [sp_n0]
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

; -----------------------------------------------------------------------------
; sp_banks - RAM read speed, one row a 64 KB bank of conventional memory,
; benchlib's method P: 4 KB of `rep lodsw` an iteration, interrupts off only
; for the iteration. Read-only, so any bank is safe to read
; -----------------------------------------------------------------------------
sp_banks:
    push ax
    push bx
    push cx
    push si
    mov si, sp_l_hram
    call bl_sline
    call bl_baseline
    call bl_head
    xor bx, bx
.b:
    mov ax, bx
    mov cl, 12
    shl ax, cl
    mov [sp_bseg], ax
    mov si, bx
    shl si, 1
    mov si, [sp_bnames + si]
    mov word [bl_n], SP_BANKN
    mov word [bl_body], sp_bbody
    xor al, al                      ; method P
    call bl_run
    inc bx
    cmp bx, 10
    jb .b
    pop si
    pop cx
    pop bx
    pop ax
    ret

sp_bbody:                           ; 4 KB read from [sp_bseg]:0
    push cx
    push si
    push ds
    mov ds, [sp_bseg]
    xor si, si
    mov cx, 2048
    cld
    rep lodsw
    pop ds
    pop si
    pop cx
    ret

%include "os88spk.inc"
%include "os88spkfx.inc"
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================
sp_tpl:
    dw 7, 22, 632, 448
    dw sp_ttl, sp_paint, sp_onkey, sp_onclick
sp_ttl:     db 'Spk Bench', 0

sp_rates:   dw 4800, 5512, 8000, 0

sp_f_out:   db 'SPKBENCH.TXT', 0
sp_l_title: db 'SPKBENCH - what the PC speaker costs this machine (SPEC.md 45.25)', 0
sp_l_hint1: db 'R (or a click) runs it: ~12 s, hands off the mouse, and the', 0
sp_l_hint2: db 'speaker ticks quietly. The report is saved as SPKBENCH.TXT.', 0
sp_l_cpu:   db 'CPU tier', 0
sp_l_cpu8086: db '8086/8088 class', 0
sp_l_cpu286: db '286', 0
sp_l_cpu386: db '386 or better', 0
sp_l_noring: db 'NO MEMORY for the speaker ring: the speaker rows were skipped', 0
sp_l_norate: db 'the kernel refused FSXF_RATE: the speaker rows were skipped', 0
sp_l_hspk:  db '-- the speaker shaper, a fixed workload, speaker SHUT --', 0
sp_l_ticks: db 'ticks a row', 0
sp_l_shut:  db 'shaper spans (256) done', 0
sp_l_sps:   db '...samples a second', 0
sp_l_cps:   db '...cycles a sample', 0
sp_l_filt:  db 'load-filter spans done', 0
sp_l_fcps:  db '...cycles a sample', 0
sp_l_hisr:  db '-- the same, speaker PLAYING: what the sample ISR takes --', 0
sp_l_rate:  db 'rate, Hz', 0
sp_l_refused: db '  the speaker refused this rate here', 0
sp_l_open:  db '  shaper spans done', 0
sp_l_share: db '  ISR share, per mille', 0
sp_l_pulse: db '  ISR cycles a sample', 0
sp_l_tsp:   db '  Tracker assumes (p.m.)', 0
sp_l_hram:  db '-- RAM read, 4 KB of rep lodsw in each 64 KB bank --', 0
sp_l_end:   db 'done - saved as SPKBENCH.TXT', 0
sp_p_spk:   db 'running: the speaker rows (~9 s)...', 0
sp_p_ram:   db 'running: the RAM banks...', 0
sp_bnames:  dw sp_b0, sp_b1, sp_b2, sp_b3, sp_b4, sp_b5, sp_b6, sp_b7
            dw sp_b8, sp_b9
sp_b0:      db 'bank 0 (00000h)', 0
sp_b1:      db 'bank 1 (10000h)', 0
sp_b2:      db 'bank 2 (20000h)', 0
sp_b3:      db 'bank 3 (30000h)', 0
sp_b4:      db 'bank 4 (40000h)', 0
sp_b5:      db 'bank 5 (50000h)', 0
sp_b6:      db 'bank 6 (60000h)', 0
sp_b7:      db 'bank 7 (70000h)', 0
sp_b8:      db 'bank 8 (80000h)', 0
sp_b9:      db 'bank 9 (90000h)', 0

SP_BSS_OWN  equ ((64 + SP_SPAN * 2 + SPKFX_NLEV * 256 + 511) / 512) * 512
                                    ; benchlib's base must be 512-aligned
    OS88_BSS SP_BSS_OWN + BL_BSS_SIZE
    OS88_IMAGE_END

; --- loader-zeroed bss ------------------------------------------------------
sp_win      equ os88_image_end + 0
sp_ran      equ os88_image_end + 2
sp_rseg     equ os88_image_end + 4
sp_n0       equ os88_image_end + 6
sp_nf       equ os88_image_end + 8
sp_cur      equ os88_image_end + 10
sp_pm       equ os88_image_end + 12
sp_pc       equ os88_image_end + 14
sp_cs       equ os88_image_end + 16
sp_t0       equ os88_image_end + 18
sp_bseg     equ os88_image_end + 20
sp_nr       equ os88_image_end + 32     ; a word a rate (4)
sp_src      equ os88_image_end + 64
sp_dst      equ sp_src + SP_SPAN
sp_fam      equ sp_dst + SP_SPAN        ; the shaper's family, in our segment

    BL_BSS os88_image_end + SP_BSS_OWN
