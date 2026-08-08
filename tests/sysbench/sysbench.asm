; =============================================================================
; os8088 - tests/sysbench/sysbench.asm
;
; SYSBENCH: the machine under the graphics. CPU, bus, memory, the clock, the
; scheduler's own interrupt load, the API's far-call floor and the floppy - the
; things PERFORMANCE.md Part 2 quotes without ever having measured on the
; target, and the things a future agent needs in order to price a change it
; cannot run.
;
;   make bench
;   make test TESTAPPS=build/bench.img
;
; The same caution as its sibling: under QEMU the microsecond column is the
; HOST's speed. With `-icount shift=3,sleep=off` the counts column is guest
; INSTRUCTIONS, which is reproducible and is not time. build/bench360.img on a
; real 4.77 MHz 8088 is where these numbers mean what they say.
;
; --- the headline: 8086-nominal clocks against an 8088 -----------------------
;
; PERFORMANCE.md Part 2 ends with "8086-nominal cycle counts under-report an
; 8088 by 20-40%", cites a plan document, and leaves it there - so every margin
; anyone has computed from an instruction-timing table since has rested on a
; range someone remembered. The CPU block measures it: each row runs SB_UNROLL
; copies of one instruction, and the derived table beside it prints the
; MEASURED clocks, the 8086 book figure, and the ratio. The interesting part is
; that the ratio is not one number - it is near 1.0 for `mul`, which is
; execution-bound, and much worse for `nop`, which on an 8088 is starved by a
; 4-byte prefetch queue behind an 8-bit bus. That shape is the actual finding,
; and a single "add 30%" cannot carry it.
;
; One PIT count is EXACTLY four CPU clocks on a period machine: both divide the
; same 14.31818 MHz crystal, the PIT by 12 and the 8088 by 3. So the clock
; column needs no calibration on an IBM PC or XT - and on a turbo clone it is
; wrong by exactly the turbo factor, which is why the block also derives an
; estimated CPU speed from the two execution-bound rows. Two estimates, from
; different instructions, that must agree.
;
; --- the interrupt load ------------------------------------------------------
;
; The one measurement here that is about os8088 rather than about the machine.
; The same fixed workload is timed twice: once with method P, whose cli window
; excludes every interrupt, and once with method T, which includes all of them.
; The difference is what the tick, the mouse and the scheduler cost per second
; of ordinary work - a figure nothing in this tree has ever put a number on,
; and one that bounds every "is there room for this?" question.
;
; --- the floppy --------------------------------------------------------------
;
; dsk_xfer issues one int 13h per sector (SPEC.md 18.4.1), so throughput is
; dominated by rotational latency rather than by bandwidth, and it is the
; reason a 116KB module load is a coffee break. Two reads of the same file are
; timed: the first pays the motor spin-up, the second does not. Both are
; reported, because quoting either alone is misleading.
;
; Prefix sb_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SYSBENCH', sb_entry

SB_UNROLL   equ 32                ; copies of the instruction under test in one
                                  ; body. Enough that the call, the ret and the
                                  ; two PIT reads are a few percent, few enough
                                  ; that the body stays inside the prefetch
                                  ; behaviour of ordinary code
SB_BWROWS   equ 32                ; the bandwidth shape gfxbench uses, so the
SB_BWCOLS   equ 64                ; RAM rows of the two harnesses are directly
SB_BWBYTES  equ SB_BWROWS * SB_BWCOLS   ; comparable - which is the point of
                                  ; measuring RAM in both (rule 7)
SB_WORKN    equ 800               ; iterations of the interrupt-load workload.
                                  ; One is ~2.5 ms of rep stosw on a 4.77 MHz
                                  ; 8088, so this is two seconds - 36 ticks, a
                                  ; 3% quantisation. At 200 it was 9 ticks and
                                  ; the answer was quantised to 11%
SB_BIGKB    equ 32                ; the heap claim the file reads land in
SB_TICKTRY  equ 30000             ; bound on every wait-for-tick spin here. A
                                  ; stopped tick must produce a wrong number,
                                  ; never a hung machine

; -----------------------------------------------------------------------------
; sb_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
sb_entry:
    push si
    call sb_facts
    call sb_hint                    ; the invitation, so the first thing on
    mov si, sb_tpl                  ; screen is not a blank page
    call OSAPI_WM_CREATE
    jc .out
    mov [sb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; mono only; PRESERVES FLAGS
    mov si, sb_menus
    call OSAPI_MENU_SET
    mov si, sb_onabout
    call OSAPI_ABOUT_SET
    clc
.out:
    pop si
    ret

sb_paint:
    call bl_paint
    ret

; -----------------------------------------------------------------------------
; sb_onkey - R runs, S saves, everything else pages
; -----------------------------------------------------------------------------
sb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [sb_win], si
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
    call sb_run
    call sb_repaint
    jmp short .out
.save:
    mov si, sb_f_out
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

sb_onclick:
    push ax
    push si
    mov [sb_win], si
    cmp byte [sb_ran], 0            ; never run: a click runs it, which is what
    jne .page                       ; a user who has just read the invitation
    push bx                         ; will try (tests/fontbench's idiom)
    push cx
    push dx
    push di
    call sb_run
    call sb_repaint
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

sb_oncmd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [sb_win], si
    or ah, ah
    jnz .out
    cmp al, 0
    je .run
    cmp al, 1
    je .save
    cmp al, 2
    je .top
    jmp short .out
.run:
    call sb_run
    call sb_repaint
    jmp short .out
.save:
    mov si, sb_f_out
    call bl_save
    call bl_paint
    jmp short .out
.top:
    mov word [bl_top], 0
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sb_onabout:
    push si
    mov word [bl_top], 0
    call bl_paint
    pop si
    ret

; sb_repaint - nothing here draws outside the text, but a page whose status
; line sat stale for the length of a floppy read is worth putting back whole
sb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, [sb_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, dx
    mov cx, ax
    add cx, [sb_cw]
    dec cx
    add dx, [sb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [sb_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; sb_facts - what the machine says about itself, before anything is timed
; =============================================================================
sb_facts:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call OSAPI_VIDEO
    mov [sb_vw], ax
    mov [sb_vh], bx
    mov [sb_kind], dl
    call OSAPI_CPU_INFO
    mov [sb_cputier], ax
    call OSAPI_MEM_AVAIL
    mov [sb_mlarge], ax
    mov [sb_mtotal], bx
    call OSAPI_SND_CAPS             ; AX = caps word (SND_CAP_*)
    mov [sb_snd], ax
    call OSAPI_XMEM_CAPS            ; AX = KB the pool can still hand out
    mov [sb_xms], ax
    push ds
    pop es
    mov di, sb_syskb
    call OSAPI_SYS_KB
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; sb_run - the whole suite
; =============================================================================
sb_run:
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
    mov byte [sb_ran], 1

    call sb_geom
    call sb_mktab
    call bl_baseline
    mov [sb_bcnt], ax
    mov [sb_bcnt+2], dx

    mov si, sb_p_head
    call bl_progress
    call sb_header
    mov si, sb_p_cpu
    call bl_progress
    call sb_cpu
    call sb_cpuderive
    mov si, sb_p_mem
    call bl_progress
    call sb_mem
    mov si, sb_p_clk
    call bl_progress
    call sb_clock
    mov si, sb_p_isr
    call bl_progress
    call sb_isrload
    mov si, sb_p_os
    call bl_progress
    call sb_os
    mov si, sb_p_dsk
    call bl_progress
    call sb_disk
    mov si, sb_p_hdd
    call bl_progress
    call sb_hdd
    mov si, sb_p_mou
    call bl_progress
    call sb_mouse
    call sb_ladder                  ; SPEC.md 37.92 - a state dump, like the
                                    ; mouse block above and for its reason
    call bl_operator                ; ...and what the OPERATOR was doing
    call sb_trailer
    mov si, sb_f_out                ; SAVE IT, without being asked: a report
    call bl_save                    ; that is only in RAM is a report that
                                    ; needs the run doing again (see sb_onkey)

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sb_geom:
    push ax
    push bx
    push cx
    push dx
    mov bx, [sb_win]
    call OSAPI_WM_GEOM
    mov [sb_cw], cx
    mov [sb_ch], dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top -
    mov [bl_cx], ax                 ; bl_progress draws before bl_paint ever
    mov [bl_cy], dx                 ; runs, so it cannot wait for these
    mov cx, [sb_cw]
    mov ax, cx
    mov cl, 3
    shr ax, cl
    cmp ax, BL_MAXLINE
    jbe .c
    mov ax, BL_MAXLINE
.c:
    mov [bl_vcols], ax
    mov ax, [sb_ch]
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

; sb_mktab - the RAM row table, gfxbench's shape exactly
sb_mktab:
    push ax
    push cx
    push di
    mov di, sb_rrow
    mov cx, SB_BWROWS
    mov ax, sb_ram
.r:
    mov [di], ax
    add di, 2
    add ax, SB_BWCOLS
    loop .r
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; the report blocks
; =============================================================================

; -----------------------------------------------------------------------------
; sb_hint - the report an unrun harness shows. Built out of the same arena the
; results use, so it pages and saves like anything else; a run replaces it.
; -----------------------------------------------------------------------------
sb_hint:
    push si
    mov si, sb_s_ttl1
    call bl_sline
    mov si, sb_s_ttl2
    call bl_sline
    call bl_blank
    mov si, sb_h_1
    call bl_sline
    mov si, sb_h_2
    call bl_sline
    call bl_blank
    mov si, sb_h_3
    call bl_sline
    mov si, sb_h_4
    call bl_sline
    mov si, sb_h_5
    call bl_sline
    call bl_blank
    mov si, sb_h_6
    call bl_sline
    mov si, sb_h_6b
    call bl_sline
    mov si, sb_h_7
    call bl_sline
    pop si
    ret

sb_header:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, sb_s_ttl1
    call bl_sline
    mov si, sb_s_ttl2
    call bl_sline
    call bl_blank

    mov si, sb_l_cpu
    mov di, sb_n_8086
    cmp byte [sb_cputier], CPU_286
    jne .c1
    mov di, sb_n_286
.c1:
    cmp byte [sb_cputier], CPU_386
    jne .c2
    mov di, sb_n_386
.c2:
    call bl_kvs
    mov si, sb_l_feat
    mov al, [sb_cputier+1]
    xor ah, ah
    call sb_hex
    mov si, sb_l_adapter
    mov al, [sb_kind]
    xor ah, ah
    call sb_num
    mov si, sb_l_scrw
    mov ax, [sb_vw]
    call sb_num
    mov si, sb_l_scrh
    mov ax, [sb_vh]
    call sb_num
    call sb_boot                    ; how long this machine took to get here
    mov si, sb_l_kern
    mov ax, [sb_syskb + SK_KERN]
    call sb_num
    mov si, sb_l_img
    mov ax, [sb_syskb + SK_IMG]
    call sb_num
    mov si, sb_l_buf
    mov ax, [sb_syskb + SK_BUF]
    call sb_num
    mov si, sb_l_heap
    mov ax, [sb_syskb + SK_HEAP]
    call sb_num
    mov si, sb_l_claim
    mov ax, [sb_syskb + SK_CLAIM]
    call sb_num
    mov si, sb_l_mlarge
    mov ax, [sb_mlarge]
    call sb_num
    mov si, sb_l_mtotal
    mov ax, [sb_mtotal]
    call sb_num
    mov si, sb_l_xms
    mov ax, [sb_xms]
    call sb_num
    mov si, sb_l_snd
    mov ax, [sb_snd]
    call sb_hex
    mov si, sb_l_drive
    call bl_lclr
    xor di, di
    call bl_lput
    call bl_drive
    mov [bl_lscr + BL_C_N], al
    call bl_lcommit

    call bl_blank
    mov si, sb_s_pit1
    call bl_sline
    mov si, sb_s_pit2
    call bl_sline
    mov si, sb_s_pit3
    call bl_sline
    mov si, sb_l_ovh
    mov ax, [sb_bcnt]
    mov dx, [sb_bcnt+2]
    mov cx, 9
    call bl_kv
    call bl_blank
    mov si, sb_s_warn1
    call bl_sline
    mov si, sb_s_warn2
    call bl_sline
    mov si, sb_s_warn3
    call bl_sline
    mov si, sb_s_warn4
    call bl_sline
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sb_num:
    push cx
    push dx
    xor dx, dx
    mov cx, 9
    call bl_kv
    pop dx
    pop cx
    ret

sb_hex:
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov di, BL_C_N
    call bl_hex4
    call bl_lcommit
    pop di
    ret

; --- block 1: the instruction rows -------------------------------------------
sb_cpu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call bl_blank
    mov si, sb_s_h_cpu
    call bl_sline
    mov si, sb_s_h_cpu2
    call bl_sline
    call bl_head
    mov word [sb_i], 0
.next:
    mov bx, [sb_i]
    cmp bx, SB_NCPU
    jae .done
    mov cl, 3
    shl bx, cl
    add bx, sb_ctab
    mov ax, [bx+2]                  ; the body
    mov [bl_body], ax
    mov ax, [bx+6]                  ; ...and its iteration count
    mov [bl_n], ax
    mov si, [bx]                    ; the label
    xor al, al                      ; method P
    call bl_run
    mov bx, [sb_i]
    shl bx, 1
    shl bx, 1
    add bx, sb_res
    mov ax, [bl_last]               ; bl_run PRESERVES every register: the
    mov dx, [bl_last+2]             ; result is in bl_last, not in DX:AX
    mov [bx], ax
    mov [bx+2], dx
    inc word [sb_i]
    jmp short .next
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 2: the same rows as clocks, against the 8086 book -----------------
;
; measured clocks x100 = counts * 400 / (N * SB_UNROLL) - exact on a period
; machine, four clocks a count. The ratio column is the number PERFORMANCE.md
; Part 2 has been quoting as a remembered range.
sb_cpuderive:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call bl_blank
    mov si, sb_s_h_der
    call bl_sline
    mov si, sb_s_h_der2
    call bl_sline
    mov word [sb_i], 0
.next:
    mov bx, [sb_i]
    cmp bx, SB_NCPU
    jae .done
    mov cl, 3
    shl bx, cl
    add bx, sb_ctab
    mov [sb_ent], bx
    mov bx, [sb_i]
    shl bx, 1
    shl bx, 1
    add bx, sb_res
    mov ax, [bx]
    mov dx, [bx+2]
    mov bx, [sb_ent]
    mov cx, [bx+6]                  ; N
    call sb_clkx100                 ; DX:AX = measured clocks x100
    mov [sb_meas], ax
    mov [sb_meas+2], dx

    call bl_lclr                    ; label
    mov bx, [sb_ent]
    mov si, [bx]
    xor di, di
    call bl_lput
    mov ax, [sb_meas]               ; measured
    mov dx, [sb_meas+2]
    mov di, 22
    mov cx, 9
    call bl_dec
    mov bx, [sb_ent]                ; the 8086 book figure
    mov ax, [bx+4]
    xor dx, dx
    mov di, 32
    mov cx, 9
    call bl_dec
    mov ax, [sb_meas]               ; ...and measured / nominal, x100
    mov dx, [sb_meas+2]
    mov bx, [sb_ent]
    mov bx, [bx+4]
    xor cx, cx
    call gb_ratio_sb
    mov di, 42
    mov cx, 9
    call bl_dec
    call bl_lcommit
    inc word [sb_i]
    jmp .next
.done:
    call sb_mhz
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_mhz - the CPU speed, from the two execution-bound rows
;
; MUL and DIV spend most of their time in the sequencer rather than at the bus,
; so their measured clocks should equal the book figure on a 4.77 MHz machine
; whatever the prefetch is doing. Turn that round and the ratio IS the clock:
; MHz x100 = 477 * nominal / measured. Two rows, two estimates, and they have
; to agree - which is the only check available on a machine whose only other
; timebase is the PIT this harness is already using.
sb_mhz:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov bx, SB_I_MUL
    call sb_est
    mov si, sb_d_mhzmul
    mov cx, 9
    call bl_kv
    mov bx, SB_I_DIV
    call sb_est
    mov si, sb_d_mhzdiv
    mov cx, 9
    call bl_kv
    call sb_shlbit
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_est - BX = a sb_ctab index -> DX:AX = estimated MHz x100
sb_est:
    push bx
    push cx
    push si
    push di
    mov di, bx                      ; DI = the entry index
    mov cl, 3
    shl bx, cl
    add bx, sb_ctab
    mov si, [bx+4]                  ; SI = the nominal, clocks x100
    mov bx, di
    call sb_clkof                   ; DX:AX = measured clocks x100
    mov bx, ax
    mov cx, dx                      ; CX:BX = measured
    mov ax, si                      ; ...and 477 (4.7727 MHz x100) times the
    xor dx, dx                      ; nominal over it
    mov si, 477
    call sb_mul16
    call sb_divby
    pop di
    pop si
    pop cx
    pop bx
    ret

; sb_clkof - BX = a row's index in sb_ctab -> DX:AX = its measured clocks x100
; clobbers: AX, DX, flags
sb_clkof:
    push bx
    push cx
    push si
    mov si, bx
    mov cl, 3
    shl bx, cl
    add bx, sb_ctab
    mov cx, [bx+6]                  ; CX = the row's iteration count
    mov bx, si
    shl bx, 1
    shl bx, 1
    add bx, sb_res
    mov ax, [bx]                    ; DX:AX = the row's counts
    mov dx, [bx+2]
    call sb_clkx100
    pop si
    pop cx
    pop bx
    ret

; sb_shlbit - what a variable shift costs PER BIT, from the two shl r16,cl
;             rows nine bits apart
;
; The 8086 book says 8 clocks plus 4 per bit, and this project has been
; spending that number rather than measuring it: SPEC.md 5.7 traded two edge-
; mask shifts and gfx_rowbase's shl-by-13 for table lookups on the strength of
; it, which is most of what came off the per-call floor. A single shl row
; cannot check it - only the SLOPE can - so this subtracts the two and divides
; by the nine bits between them. It must land near 400 (4.00 clocks x100), and
; both rows it is derived from are on the screen above it, which is the point:
; a reader can recompute it by hand and catch the harness lying
; (PERFORMANCE.md Part 6 rule 7).
sb_shlbit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, SB_I_SHL13
    call sb_clkof
    mov cx, ax                      ; SI:CX = the 13-bit shift
    mov si, dx
    mov bx, SB_I_SHL4
    call sb_clkof                   ; DX:AX = the 4-bit one
    sub cx, ax
    sbb si, dx
    jc .bad                         ; a negative difference is not a number
    mov ax, cx                      ; DX:AX = the gap, clocks x100 for 9 bits
    mov dx, si
    mov cx, 1
    call bl_mul48
    mov cx, 9
    call bl_div48
    call bl_get32
    jmp short .show
.bad:
    xor ax, ax
    xor dx, dx
.show:
    mov si, sb_d_shlbit
    mov cx, 9
    call bl_kv
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 3: memory bandwidth (the RAM half; the framebuffer is gfxbench's) -
sb_mem:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, sb_s_h_mem
    call bl_sline
    call bl_head
    mov ax, ds
    mov [sb_seg], ax
    mov word [sb_tab], sb_rrow
    mov word [bl_n], 8
    mov word [bl_body], sb_b_stosw
    mov si, sb_r_sw
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_stosb
    mov si, sb_r_sb
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_movsw
    mov si, sb_r_mw
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_movsb
    mov si, sb_r_mb
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_scasb
    mov si, sb_r_sc
    xor al, al
    call bl_run
    mov word [bl_n], 4
    mov word [bl_body], sb_b_rmw
    mov si, sb_r_rm
    xor al, al
    call bl_run
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 4: the clock ------------------------------------------------------
sb_clock:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, sb_s_h_clk
    call bl_sline
    call bl_head
    mov word [bl_n], 300
    mov word [bl_body], sb_b_pit
    mov si, sb_r_pit
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_ticks
    mov si, sb_r_gt
    xor al, al
    call bl_run
    mov word [bl_n], 60
    mov word [bl_body], sb_b_bios1a
    mov si, sb_r_1a
    xor al, al
    call bl_run
    mov word [bl_n], 100
    mov word [bl_body], sb_b_int16
    mov si, sb_r_k16
    xor al, al
    call bl_run
                                    ; NO task_sleep ROW. It is a WORKER's call
                                    ; (SPEC.md 20.6) and this runs on the UI
                                    ; task: sch_switch's "nothing ready" leg
                                    ; resumes the outgoing task, so on a quiet
                                    ; machine task 0 sleeping returns at once -
                                    ; measured as 18 sleeps of one tick taking
                                    ; zero ticks - while leaving T_STATE = 2 on
                                    ; a task the scheduler documents as one
                                    ; that never sleeps. Measuring it costs the
                                    ; number nothing and risks the machine.
    call sb_ticklen                 ; PIT counts observed inside one BIOS tick
    mov si, sb_d_tick
    mov cx, 9
    call bl_kv
    call sb_rtc                     ; and whether int 1Ah AH=02h answers at all
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_ticklen - PIT counts inside one BIOS tick
; out: DX:AX = the count
;
; Counter 0 is reloaded with 0, i.e. 65536, so a tick IS 65536 counts by
; construction and this row exists to CHECK that, not to discover it: the whole
; harness converts ticks to counts by that identity (benchlib, method T). What
; it actually measures is 65536 plus the residual between two same-phase
; samples, so a few hundred counts of ISR-entry jitter is expected and a figure
; that is not near 65536 means the timebase assumption is wrong on this
; machine - which would invalidate every method-T row above it.
sb_ticklen:
    push bx
    push cx
    push si
    push di
    call OSAPI_GET_TICKS
    mov si, ax
    mov cx, SB_TICKTRY
.e1:
    call OSAPI_GET_TICKS            ; wait for a tick edge, so both samples are
    cmp ax, si                      ; taken at the same phase
    jne .go
    loop .e1
.go:
    mov si, ax
    call bl_pit
    mov di, ax                      ; DI = the PIT at the edge
    mov cx, SB_TICKTRY
.e2:
    call OSAPI_GET_TICKS
    cmp ax, si
    jne .end
    loop .e2
.end:
    call bl_pit
    mov bx, di
    sub bx, ax                      ; the down-counter's residual, modular
    mov ax, bx
    xor dx, dx
    add ax, 0                       ; ...on top of the 65536 a full wrap costs
    adc dx, 1
    pop di
    pop si
    pop cx
    pop bx
    ret

; sb_rtc - does int 1Ah AH=02h answer? (SPEC.md 37.90 rung 4)
; An XT BIOS implements AH=00h/01h and nothing else, so this is the one thing
; a package can say about the clock ladder from outside the kernel.
sb_rtc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, 2
    int 0x1A
    jc .no
    mov si, sb_l_rtc
    mov di, sb_n_yes
    call bl_kvs
    mov si, sb_l_rtch                ; the hour, in BCD as the BIOS returns it
    mov al, ch
    xor ah, ah
    call sb_hex
    jmp short .out
.no:
    mov si, sb_l_rtc
    mov di, sb_n_no
    call bl_kvs
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 5: what the kernel's own interrupts cost --------------------------
sb_isrload:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, sb_s_h_isr
    call bl_sline
    mov si, sb_s_h_isr2
    call bl_sline
    call bl_head
    mov ax, ds
    mov [sb_seg], ax
    mov word [sb_tab], sb_rrow
    mov word [bl_n], SB_WORKN
    mov word [bl_body], sb_b_stosw
    mov si, sb_r_wp
    xor al, al                      ; method P: the cli window excludes every
    call bl_run                     ; interrupt
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [sb_wp], ax
    mov [sb_wp+2], dx
    mov word [bl_n], SB_WORKN
    mov word [bl_body], sb_b_stosw
    mov si, sb_r_wt
    mov al, 1                       ; method T: all of them included
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [sb_wt], ax
    mov [sb_wt+2], dx

    mov ax, [sb_wt]                 ; (T - P) / T, x100: the share of a second
    mov dx, [sb_wt+2]               ; of ordinary work the tick, the mouse and
    sub ax, [sb_wp]                 ; the scheduler take
    sbb dx, [sb_wp+2]
    jnc .ok
    xor ax, ax
    xor dx, dx
.ok:
    mov bx, [sb_wt]
    mov cx, [sb_wt+2]
    call gb_ratio_sb
    mov si, sb_d_isr
    mov cx, 9
    call bl_kv
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 6: the API's far-call floor and the scheduler ---------------------
sb_os:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, sb_s_h_os
    call bl_sline
    call bl_head
    mov word [bl_n], 300
    mov word [bl_body], sb_b_near
    mov si, sb_r_nc
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_ticks
    mov si, sb_r_fc
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_yield
    mov si, sb_r_yl
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_rand
    mov si, sb_r_rn
    xor al, al
    call bl_run
    mov word [bl_body], sb_b_here
    mov si, sb_r_hr
    xor al, al
    call bl_run
    mov word [bl_n], 60
    mov word [bl_body], sb_b_dfree
    mov si, sb_r_df
    xor al, al
    call bl_run
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 7: the floppy -----------------------------------------------------
;
; Both file rows are method T and refuse to be clever: a read is seconds, the
; gfx lock is held for all of it and the machine looks frozen, which is itself
; the finding. A machine whose heap cannot fund the claim, or a volume without
; the data files, records that and moves on - refusal is a normal path
; (PERFORMANCE.md Part 6 rule 9).
sb_disk:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, sb_s_h_dsk
    call bl_sline
    call bl_head
    mov ax, SB_BIGKB
    call OSAPI_MEM_CLAIM            ; DX = the base segment
    jc .noclaim
    mov [sb_bseg], dx

    mov word [bl_n], 1              ; the first read pays the motor spin-up
    mov word [bl_body], sb_b_rdbig
    mov si, sb_r_d1
    mov al, 1
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [sb_td1], ax
    mov [sb_td1+2], dx
    mov si, sb_l_derr               ; ...and says whether it worked at all
    mov ax, [sb_rerr]
    call sb_num
    mov si, sb_l_dsz
    mov ax, [sb_rsz]
    call sb_num

    mov word [bl_n], 1              ; the second does not - the SAME body
    mov word [bl_body], sb_b_rdbig  ; deliberately, and it says so rather than
    mov si, sb_r_d2                 ; carrying it (tools/benchlint.py)
    mov al, 1
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [sb_td2], ax
    mov [sb_td2+2], dx

    call sb_verify                  ; ...and CHECK WHAT IT READ, on every
                                    ; kernel. This lived inside the DISKCNT
                                    ; block at first, which meant it did not
                                    ; run on the plain kernel the field
                                    ; actually booted - so the 4x speedup of
                                    ; PERFORMANCE.md Part 9 Set 17 was taken
                                    ; with the one check that licensed it
                                    ; switched off. A guard that only runs on
                                    ; the build you are not shipping is not a
                                    ; guard.

    mov word [bl_n], 4              ; a one-sector file: the per-call cost of
    mov word [bl_body], sb_b_rdsml  ; finding and opening one, with almost no
    mov si, sb_r_ds                 ; data behind it
    mov al, 1
    call bl_run

    call sb_raw13                   ; ...and the same disk with no kernel code
                                    ; in the way at all (below)

    mov ax, [sb_bseg]               ; hand the claim back: a benchmark that
    mov dx, ax                      ; holds 32KB for the session changes what
    call OSAPI_MEM_FREE             ; every row after it is measuring
    jmp .rate
.noclaim:
    mov si, sb_s_noclaim
    call bl_sline
    jmp .out
.rate:
    mov ax, [sb_td2]                ; bytes per second, from the WARM read.
    mov dx, [sb_td2+2]              ; counts / 1193 is milliseconds exactly
    mov cx, 1193                    ; enough (1.193182 counts per us), and
    call gb_div_sb                  ; bytes * 1000 / ms then fits 32 bits,
    mov bx, ax                      ; which bytes * 1,000,000 / us does not
    mov cx, dx
    or bx, bx
    jnz .have
    or cx, cx
    jnz .have
    mov bx, 1                       ; a read too fast to time: do not divide
.have:                              ; by zero, and the row will say so
    mov ax, [sb_rsz]
    xor dx, dx
    mov si, 1000
    call sb_mul16
    call sb_divby
    mov si, sb_d_rate
    mov cx, 9
    call bl_kv
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_mouse - the port contest and the identify burst (SPEC.md 9.4.1/9.4.2)
;
; The one thing in this report that is not a measurement: it is a STATE dump,
; because the question it answers cannot be asked any other way. What a real
; Microsoft mouse on a real serial card does when its DTR/RTS is raised is not
; something either emulator in docs/FIELD-MACHINES.md can be trusted about -
; QEMU's msmouse ignores DTR outright - and the answer decides whether the
; kernel spends the session power-cycling a working mouse.
;
; It reads the block through SPEC.md 57's registry at 0060:000E, and the block
; is UNCONDITIONAL (SPEC.md 9.4.2) precisely so that a field disk - built with
; no knob, by docs/FIELD-MACHINES.md's rule - carries it.
;
; Two things about WHEN this runs, and both are why the columns are split the
; way they are. By the time anyone has launched sysbench the mouse has been
; used, so `seen`, `port` and `poller state` are already settled and say
; nothing about the boot. The identify columns do not move after mouse_init
; and the poller's tick stamp is never written unless it actually dropped
; DTR - so `hpt = 0` is the assertion that matters here, and it survives the
; user having driven the machine for ten minutes first.
; -----------------------------------------------------------------------------
sb_mouse:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call bl_blank
    mov si, sb_s_h_mou
    call bl_sline
    mov si, sb_s_h_mou2
    call bl_sline
    mov si, sb_s_h_mou3
    call bl_sline                   ; ...and NO bl_head: that heading names
                                    ; N/counts/us-per-op, and not one row here
                                    ; is a measurement

    mov ax, DBG_TAG_MOUSE           ; SPEC.md 57's registry
    call sb_dbgfind
    jc .nodbg
    mov ax, [es:bx+2]
    mov [sb_mbase], ax              ; -> mou_bases, 2 words, 0 = no UART there
    mov ax, [es:bx+4]
    mov [sb_mstate], ax             ; -> the 34-byte state span

    mov bx, [sb_mbase]              ; --- which ports exist at all ------------
    mov ax, [es:bx]
    mov si, sb_l_mb0
    call sb_hex
    mov bx, [sb_mbase]
    mov ax, [es:bx+2]
    mov si, sb_l_mb1
    call sb_hex

    mov si, sb_l_mid0               ; --- the identify burst, the whole point -
    mov al, 9                       ; mou_idn[0]
    call sb_mb
    mov si, sb_l_mid1
    mov al, 11
    call sb_mb
    mov si, sb_l_mfb0               ; mou_idb0[0] - 4D is 'M' and is the answer
    mov al, 13                      ; nothing in the container can give
    call sb_mbx
    mov si, sb_l_mfb1
    mov al, 15
    call sb_mbx
    mov si, sb_l_mok0               ; mou_ident[0]
    mov al, 21
    call sb_mb
    mov si, sb_l_mok1
    mov al, 23
    call sb_mb

    mov si, sb_l_mnd0               ; --- what the contest then cost ----------
    mov al, 5                       ; mou_need[0]: 1 = first packet wins,
    call sb_mb                      ; 8 = MOU_LOCKN, the third of a second
    mov si, sb_l_mnd1
    mov al, 7
    call sb_mb

    mov si, sb_l_mhpt               ; --- did the poller ever touch it? -------
    mov bx, [sb_mstate]             ; mou_hpt is a WORD and is the assertion
    mov ax, [es:bx+28]              ; that matters: 0 = it never dropped DTR
    call sb_num
    mov si, sb_l_mhps
    mov al, 27                      ; mou_hpst
    call sb_mb

    mov si, sb_l_msn                ; --- settled state; the operator's own
    mov al, 26                      ; clicks decided these long before now
    call sb_mb
    mov si, sb_l_mpt
    mov al, 4                       ; mou_port: a ROW (0 or 2), not a COM number
    call sb_mb
    mov si, sb_l_mln                ; mou_line: the 8259 bit the winning packets
    mov al, 33                      ; actually arrived on. Read it TOGETHER with
    call sb_mbx                     ; the row above - 10 with row 2, or 08 with
                                    ; row 0, is a cross-wired card (SPEC 9.5.2.1)
    mov si, sb_l_mrn0
    mov al, 0                       ; mou_run[0]: how far a LOSING port got
    call sb_mb
    mov si, sb_l_mrn1
    mov al, 2
    call sb_mb
    jmp .out
.nodbg:
    mov si, sb_s_mnone
    call bl_sline
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_mb / sb_mbx - SI = label, AL = byte offset into the state block -> one row
; as decimal / as hex. ES is KERNEL_SEG on entry (sb_mouse holds it).
sb_mb:
    push bx
    mov bl, al
    xor bh, bh
    add bx, [sb_mstate]
    mov al, [es:bx]
    xor ah, ah
    call sb_num
    pop bx
    ret

sb_mbx:
    push bx
    mov bl, al
    xor bh, bh
    add bx, [sb_mstate]
    mov al, [es:bx]
    xor ah, ah
    call sb_hex
    pop bx
    ret

; -----------------------------------------------------------------------------
; sb_ladder - which rung of the RTC ladder answered, and where it stopped
;            (SPEC.md 37.90/37.92)
;
; A STATE dump like sb_mouse and for the same reason, only more so: the ladder
; walks four rungs against chips that are in no emulator this project uses.
; The 5150's SixPakPlus carries the MM58167 the third rung was written for and
; is the only place that rung has ever run, so when it stops answering the
; symptom is the fallback date - one byte, identical whichever of seven gates
; refused, and identical again to having no card at all.
;
; NOT sb_clock or sb_rtc: those are the benchmark rows above, which TIME a
; clock read and ask whether int 1Ah answers. This one reads kernel state.
;
; `probe stop` is the row that carries: 00 means an earlier rung claimed and
; this one never ran, FF means it passed, and 01..07 name the gate. The four
; raw bytes beside it are what separates "no card is answering" (all FF) from
; "the card is there and one gate is stricter than its silicon".
; -----------------------------------------------------------------------------
sb_ladder:
    push ax
    push bx
    push si
    push es
    call bl_blank
    mov si, sb_s_h_lad
    call bl_sline
    mov si, sb_s_h_lad2
    call bl_sline
    mov si, sb_s_h_lad3
    call bl_sline                   ; ...and no bl_head, for sb_mouse's reason

    mov ax, DBG_TAG_CLOCK           ; SPEC.md 57's registry
    call sb_dbgfind
    jc .nodbg
    mov ax, [es:bx+2]
    mov [sb_cstate], ax             ; -> the 7-byte state span

    mov si, sb_l_ctier              ; the headline: 0 = nothing answered and
    xor al, al                      ; the machine is on 4 July 2026
    call sb_cb
    mov si, sb_l_cref
    mov al, 1
    call sb_cb
    mov si, sb_l_cstep              ; ...and the diagnosis
    mov al, 2
    call sb_cbx
    mov si, sb_l_cr00
    mov al, 3
    call sb_cbx
    mov si, sb_l_cr08
    mov al, 4
    call sb_cbx
    mov si, sb_l_csig
    mov al, 5
    call sb_cbx
    mov si, sb_l_cr08w
    mov al, 6
    call sb_cbx
    jmp short .out
.nodbg:
    mov si, sb_s_cnone
    call bl_sline
.out:
    pop es
    pop si
    pop bx
    pop ax
    ret

; sb_cb / sb_cbx - SI = label, AL = byte offset into the clock block -> one row
; as decimal / as hex. ES is KERNEL_SEG on entry (sb_ladder holds it).
sb_cb:
    push bx
    mov bl, al
    xor bh, bh
    add bx, [sb_cstate]
    mov al, [es:bx]
    xor ah, ah
    call sb_num
    pop bx
    ret

sb_cbx:
    push bx
    mov bl, al
    xor bh, bh
    add bx, [sb_cstate]
    mov al, [es:bx]
    xor ah, ah
    call sb_hex
    pop bx
    ret

sb_trailer:
    push si
    call bl_blank
    mov si, sb_s_end1
    call bl_sline
    mov si, sb_s_end2
    call bl_sline
    pop si
    ret

; =============================================================================
; arithmetic
; =============================================================================

; sb_clkx100 - DX:AX counts over CX iterations of SB_UNROLL instructions
; out: DX:AX = 4.77 MHz CPU clocks per instruction, x100
sb_clkx100:
    push cx
    push si
    mov si, cx
    mov cx, 400                     ; four clocks a count, x100
    call bl_mul48
    mov cx, si
    or cx, cx
    jnz .d
    mov cx, 1
.d:
    call bl_div48
    mov cx, SB_UNROLL
    call bl_div48
    call bl_get32
    pop si
    pop cx
    ret

; gb_ratio_sb - (DX:AX / CX:BX) * 100, both 32-bit. gfxbench's gb_ratio, under
; a name of its own so the two harnesses stay independent sources.
gb_ratio_sb:
    push bx
    push cx
    push si
.shift:
    or cx, cx
    jz .have
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .shift
.have:
    or bx, bx
    jnz .div
    mov bx, 1
.div:
    mov si, bx
    mov cx, 100
    call bl_mul48
    mov cx, si
    call bl_div48
    call bl_get32
    pop si
    pop cx
    pop bx
    ret

; gb_div_sb - DX:AX / CX, saturating
gb_div_sb:
    push cx
    push si
    mov si, cx
    mov cx, 1
    call bl_mul48
    mov cx, si
    or cx, cx
    jnz .d
    mov cx, 1
.d:
    call bl_div48
    call bl_get32
    pop si
    pop cx
    ret

; sb_mul16 - DX:AX *= SI, saturating
sb_mul16:
    push cx
    mov cx, si
    call bl_mul48
    call bl_get32
    pop cx
    ret

; sb_divby - DX:AX /= CX:BX (32-bit divisor), by shifting both until the
; divisor fits a word
sb_divby:
    push bx
    push cx
    push si
.shift:
    or cx, cx
    jz .have
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .shift
.have:
    or bx, bx
    jnz .div
    mov bx, 1
.div:
    mov si, bx
    mov cx, 1
    call bl_mul48
    mov cx, si
    call bl_div48
    call bl_get32
    pop si
    pop cx
    pop bx
    ret

; =============================================================================
; the measured bodies
; =============================================================================

; --- the instruction rows. SB_UNROLL copies each, so the call, the ret and the
; two PIT reads around them are amortised down to a few percent, and the
; baseline row takes even that off.

sb_b_nop:
%rep SB_UNROLL
    nop
%endrep
    ret

sb_b_movrr:
%rep SB_UNROLL
    mov ax, bx
%endrep
    ret

sb_b_add:
%rep SB_UNROLL
    add ax, bx
%endrep
    ret

sb_b_inc:
%rep SB_UNROLL
    inc ax
%endrep
    ret

sb_b_cmp:
%rep SB_UNROLL
    cmp ax, bx
%endrep
    ret

sb_b_xchg:
%rep SB_UNROLL
    xchg ax, bx
%endrep
    ret

sb_b_shl1:
%rep SB_UNROLL
    shl ax, 1
%endrep
    ret

; TWO shift counts, and the pair is the point: one row can only report a
; total, and the model everything in this tree reasons with - a variable
; shift costs 8 clocks PLUS 4 PER BIT - is a claim about the slope. Subtract
; them and divide by 9 and the per-bit cost falls out; sb_shlbit prints it, so
; the pair and the derived number can contradict each other (PERFORMANCE.md
; Part 6 rule 7). 13 is not an arbitrary second point: it is the shift
; gfx_rowbase used to do on every drawing call, until SPEC.md 5.7 made it a
; table lookup on the strength of exactly this model.
sb_b_shlcl:
    mov cl, 4
%rep SB_UNROLL
    shl ax, cl
%endrep
    ret

sb_b_shlcl13:
    mov cl, 13
%rep SB_UNROLL
    shl ax, cl
%endrep
    ret

sb_b_load:
%rep SB_UNROLL
    mov ax, [sb_scr]
%endrep
    ret

sb_b_store:
%rep SB_UNROLL
    mov [sb_scr], ax
%endrep
    ret

; The next two differ by ONE BYTE - the segment override - and nothing else, so
; the gap between their rows is the override's cost with the addressing, the
; prefetch and the loop identical on both sides.
sb_b_noovr:
    mov si, sb_scr
%rep SB_UNROLL
    mov al, [si]
%endrep
    ret

sb_b_ovr:
    push ds
    pop es
    mov si, sb_scr
%rep SB_UNROLL
    mov al, [es:si]
%endrep
    ret

; A TABLE LOOKUP, which is what the shift rows above get traded for. The
; kernel does this in four places now - gfx_inktab, the two edge-mask tables
; and vid_banktab (SPEC.md 5.7) - and each of those trades was made against a
; written-down EA cost, never a measured one. [bx+disp16] is the addressing
; mode all four use.
sb_b_idx:
    xor bx, bx
%rep SB_UNROLL
    mov al, [bx + sb_scr]
%endrep
    ret

sb_b_jmp:
%rep SB_UNROLL
    jmp short $+2                   ; taken, and it flushes the prefetch queue
%endrep
    ret

sb_b_pushpop:
%rep SB_UNROLL
    push ax
    pop ax
%endrep
    ret

sb_b_callret:
%rep SB_UNROLL
    call sb_nil
%endrep
    ret
sb_nil:
    ret

; MUL and DIV reload their operand every time, so the row is one reload plus
; one arithmetic instruction and the nominal in sb_ctab says so. DIV is
; reloaded rather than chained for a reason that is not tidiness: an 8086
; divide whose quotient will not fit is an INTERRUPT, not a wrong answer, and
; inside a package that is a hung machine.
sb_b_mul:
    mov bx, 7
%rep SB_UNROLL
    mov ax, 0x5555
    mul bx
%endrep
    ret

; The MEMORY form, because that is the one on the path: gfx_rowbase multiplies
; by [cs:vid_stride] on every drawing call and SPEC.md 5.7 left it there, on
; the argument that the alternative is a per-row table KERN_BUDGET cannot
; fund. The register row above cannot price that decision - this one can.
sb_b_mulm:
    mov word [sb_scr], 7
%rep SB_UNROLL
    mov ax, 0x5555
    mul word [sb_scr]
%endrep
    ret

sb_b_div:
    mov bx, 7
%rep SB_UNROLL
    xor dx, dx
    mov ax, 0x5555
    div bx
%endrep
    ret

; --- memory bandwidth: gfxbench's shape, so the RAM rows can be compared -----

sb_b_stosw:
    push es
    mov es, [sb_seg]
    mov bx, [sb_tab]
    mov si, SB_BWROWS
    xor ax, ax
    cld
.r:
    mov di, [bx]
    mov cx, SB_BWCOLS / 2
    rep stosw
    add bx, 2
    dec si
    jnz .r
    pop es
    ret

sb_b_stosb:
    push es
    mov es, [sb_seg]
    mov bx, [sb_tab]
    mov si, SB_BWROWS
    xor al, al
    cld
.r:
    mov di, [bx]
    mov cx, SB_BWCOLS
    rep stosb
    add bx, 2
    dec si
    jnz .r
    pop es
    ret

sb_b_movsw:
    push es
    push ds
    pop es
    mov di, sb_ram2
    mov si, sb_ram
    mov cx, SB_BWBYTES / 2
    cld
    rep movsw
    pop es
    ret

sb_b_movsb:
    push es
    push ds
    pop es
    mov di, sb_ram2
    mov si, sb_ram
    mov cx, SB_BWBYTES
    cld
    rep movsb
    pop es
    ret

sb_b_scasb:
    push es
    push ds
    pop es
    mov di, sb_ram
    mov cx, SB_BWBYTES
    mov al, 0xFF                    ; never matches, and repNE is what walks
    cld                             ; the whole run on that: `repe` repeats
    repne scasb                     ; while EQUAL, so scanning for a byte that
                                    ; is not there stopped at the first
                                    ; comparison and the row measured 25 us
                                    ; for 2,048 bytes on a 4.77MHz machine -
                                    ; an impossible number that shipped
                                    ; because nothing on a fast host looks
                                    ; impossible (PERFORMANCE.md Part 9)
    pop es
    ret

sb_b_rmw:
    push es
    mov es, [sb_seg]
    mov bx, [sb_tab]
    mov si, SB_BWROWS
.r:
    mov di, [bx]
    mov cx, SB_BWCOLS
.b:
    mov al, [es:di]
    or al, 0
    mov [es:di], al
    inc di
    loop .b
    add bx, 2
    dec si
    jnz .r
    pop es
    ret

; --- the clock and the OS ----------------------------------------------------

sb_b_pit:
    call bl_pit
    ret

sb_b_ticks:
    call OSAPI_GET_TICKS
    ret

sb_b_bios1a:
    xor ah, ah
    int 0x1A                        ; BIOS read tick count
    ret

sb_b_int16:
    mov ah, 1
    int 0x16                        ; BIOS keyboard status, the cheapest real
    ret                             ; BIOS call there is

sb_b_near:
    call sb_nil
    ret

sb_b_yield:
    call OSAPI_TASK_YIELD
    ret

sb_b_rand:
    call OSAPI_RAND
    ret

sb_b_here:
    call OSAPI_FILE_HERE
    ret

sb_b_dfree:
    call OSAPI_FILE_DFREE
    ret

; --- the floppy --------------------------------------------------------------

sb_b_rdbig:
    push es
    mov es, [sb_bseg]
    xor bx, bx
    mov si, sb_f_big
    mov cx, SB_BIGKB * 1024
    xor dx, dx
    call OSAPI_FILE_READ            ; out CF=0 and DX:AX = the file's size
    jc .err
    mov [sb_rsz], ax
    mov word [sb_rerr], 0
    pop es
    ret
.err:
    mov [sb_rerr], ax
    mov word [sb_rsz], 0
    pop es
    ret

; -----------------------------------------------------------------------------
; sb_raw13 - the same drive with NO kernel code in the way (SPEC.md 18.91)
;
; Every other floppy row here measures os8088's transfer path. This one calls
; int 13h itself, so it prices the BIOS, the controller, the drive and the
; media's physical interleave and nothing else - which is the only way to
; answer "what SHOULD our transfer rate be" on a machine nobody can attach a
; logic analyser to.
;
; It exists because of PERFORMANCE.md Part 9 Set 13 and the DOS cross-check
; that followed it. THE REVOLUTION TIME BELONGS TO THE DRIVE AND NOT TO THE
; MEDIA, which this header used to get wrong: a 360KB drive turns at 300 RPM
; (200 ms), and a 1.2MB drive turns at 360 RPM (167 ms) even with a 360KB disk
; in it - which is the Compaq Portable III, and reading its report against the
; 300 RPM scale credits it with 0.82 of a revolution to fetch one sector, a
; figure no drive can produce. A 9-sector track holds 4,608 data bytes:
; 23,040 bytes/second if a whole
; track arrives in one turn, 11,520 at a 2:1 interleave, and 2,560 if only one
; sector is caught per revolution. os8088 measures 2,161 - about 0.84 sectors
; a revolution - while DOS 3.3 on the SAME machine, drive and media copies at
; roughly 12,700, which is five sectors a revolution. So the media is not the
; problem and the drive is not the problem. These three rows say whether the
; BIOS is: if `9 sectors, 1 call` is near one revolution then the hardware
; streams perfectly and the fault is entirely ours, and if it is near nine
; then it does not and no amount of batching above it can help.
;
; READ ONLY, and it reads a cylinder the disk it booted from already holds.
; It never writes, and it needs no drive it is not already using.
;
; Two things it has to get right. The transfer must not straddle a 64KB
; physical boundary (the ISA DMA page register does not carry, and the
; controller answers 09h), so the buffer is placed inside the claim rather
; than at its base. And every row is method T - tick-timed with interrupts ON
; - because int 13h waits on IRQ6 and a cli window around it is a hang, not a
; measurement.
;
; **IT HARD FROZE THE 5150 ONCE, on the first run of a cold boot, and ran
; normally after a reboot** (docs/FIELD-NOTES.md 10). The hazard is real and
; unfixable from inside a package: the BIOS runs its disk handler and its
; IRQ6 nesting on whichever 256-byte task stack is current (SPEC.md 8), on
; top of this routine's frame and bl_run's and benchlib's, and the kernel's
; own dsk_xfer additionally holds sch_lock across every int 13h so nothing
; can switch underneath one. A package can do neither, and whether it dies
; depends on where the tick lands - which is why it is intermittent. It is
; kept because it answered the question nothing else could and the answer was
; worth a 6.3x correction; it is not a pattern to copy, and no shipped
; package may issue int 13h.
; -----------------------------------------------------------------------------
SB_R13_CYL  equ 5                   ; a cylinder every geometry here has
SB_R13_N    equ 9                   ; ...and one 9-sector track off it

sb_raw13:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    ; --- find the kernel's instrument, or say there is none ----------------
    push es
    mov ax, DBG_TAG_DISK            ; SPEC.md 57's registry
    call sb_dbgfind
    jc .nodbg
    mov [sb_dbgblk], bx
    mov ax, [es:bx+12]
    mov [sb_r13ent], ax             ; the FAR entry: offset then segment
    mov [sb_r13ent+2], es
    pop es
    mov word [sb_r13off], 0
    jmp short .haveblk
.nodbg:
    pop es
    mov word [sb_dbgblk], 0
    call bl_blank
    mov si, sb_s_nodbg
    call bl_sline
    jmp .out
.haveblk:

    ; --- place the buffer so 9 sectors cannot cross a 64KB page -------------
    mov ax, [sb_bseg]
    mov bx, ax
    and bx, 0x0FFF                  ; paragraphs into the current 64KB page
    mov cx, 4096
    sub cx, bx                      ; ...and paragraphs left in it
    cmp cx, (SB_R13_N * 512) / 16
    jae .placed                     ; room where the claim starts
    add ax, cx                      ; else start at the page boundary, which
.placed:                            ; is inside the claim (16KB = 1024 paras)
    mov [sb_bseg2], ax

    call bl_blank
    mov si, sb_s_h_r13
    call bl_sline
    mov si, sb_s_h_r132
    call bl_sline
    mov si, sb_s_h_r133
    call bl_sline
    mov si, sb_s_h_r134
    call bl_sline
    mov si, sb_s_h_r135
    call bl_sline
    mov si, sb_s_h_r136
    call bl_sline
    mov si, sb_s_h_r137
    call bl_sline

    ; --- the diskette parameter table the BIOS is actually using (18.92) ----
    push es
    xor ax, ax
    mov es, ax
    mov bx, [es:0x1E*4]             ; int 1Eh is a FAR POINTER to the table
    mov ax, [es:0x1E*4+2]
    mov es, ax
    mov al, [es:bx+4]               ; EOT - the byte 18.92 patches
    xor ah, ah
    mov [sb_st13], ax
    mov al, [es:bx]                 ; step rate / head unload
    xor ah, ah
    mov [sb_bcnt13], ax
    mov al, [es:bx+9]               ; head settle, ms
    xor ah, ah
    mov di, ax
    mov al, [es:bx+10]              ; motor start, eighths of a second
    xor ah, ah
    mov si, ax
    pop es
    push si
    push di
    mov si, sb_l_dptn
    mov ax, [sb_st13]
    call sb_num
    mov si, sb_l_dpts
    mov ax, [sb_bcnt13]
    call sb_hex
    pop ax
    mov si, sb_l_dpth
    call sb_num
    pop ax
    mov si, sb_l_dptm
    call sb_num

    call bl_head
    call sb_b_r13one                ; warm the motor; this one is not timed

    mov word [bl_n], 8              ; one sector, one call
    mov word [bl_body], sb_b_r13one
    mov si, sb_r_131
    mov al, 1
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [sb_t131], ax
    mov [sb_t131+2], dx

    mov word [bl_n], 4              ; nine sectors, ONE call
    mov word [bl_body], sb_b_r13trk
    mov si, sb_r_139
    mov al, 1
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [sb_t139], ax
    mov [sb_t139+2], dx

    mov word [bl_n], 4              ; ...and the same nine, one call each
    mov word [bl_body], sb_b_r13nine
    mov si, sb_r_13n
    mov al, 1
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [sb_t13n], ax
    mov [sb_t13n+2], dx

    mov si, sb_l_r13st              ; ...and whether the BIOS was happy
    mov ax, [sb_st13]
    call sb_hex

    mov ax, [sb_t139]               ; bytes/sec for the batched track. CX is
    mov dx, [sb_t139+2]             ; the bytes the WHOLE ROW moved, not one
    mov cx, 4 * SB_R13_N * 512      ; iteration's: bl_last is a total and the
    call sb_r13rate                 ; first version divided a 4-iteration
    mov si, sb_d_r13b               ; count by one iteration's bytes, so both
    mov cx, 9                       ; rates read 4x low in the field set that
    call bl_kv                      ; found them (PERFORMANCE.md Part 9 Set 14)
    mov ax, [sb_t13n]               ; ...and for the same nine one at a time
    mov dx, [sb_t13n+2]
    mov cx, 4 * SB_R13_N * 512
    call sb_r13rate
    mov si, sb_d_r13s
    mov cx, 9
    call bl_kv

    call sb_dbgctr                  ; ...and what os8088's own path issued

.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_dbgctr - what os8088's own path ISSUES, per operation (SPEC.md 18.94)
;
; The row the whole floppy investigation came down to, and it has already
; answered once (PERFORMANCE.md Part 9 Set 15). On the 5150 one 16KB read -
; 32 sectors of file - moved **148 sectors in 34 int 13h calls**, longest run
; 9, no resets. So SPEC.md 18.91's splitter WORKS: 4.35 sectors a call is not
; one-per-call, and every call costs about what the raw int 13h rows above say
; the BIOS charges. **We are simply moving 4.6x the data the file contains.**
;
; The same binary on the same image under QEMU moves 34 sectors in 6 calls,
; which is very nearly optimal - so whatever the extra 116 sectors are, they
; are machine-dependent, and that is what these rows are now shaped to find.
; Each operation is measured on its own by banking the free-running counters,
; doing the thing and subtracting, so the ONE-SECTOR read isolates the fixed
; cost (open, directory walk, any remount) from the data.
; -----------------------------------------------------------------------------
sb_dbgctr:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, sb_s_h_ctr
    call bl_sline
    mov si, sb_s_h_ctr2
    call bl_sline

    call sb_ctr_bank                ; --- one 16KB read
    call sb_b_rdbig
    call sb_ctr_take
    mov si, sb_l_c16
    call bl_sline
    call sb_ctr_show
    call sb_ctr_trace               ; ...and every call it made, in order

    call sb_ctr_bank                ; --- ...and one ONE-SECTOR read, which is
    call sb_b_rdsml                 ; the same overhead with no data behind it
    call sb_ctr_take
    mov si, sb_l_c1
    call bl_sline
    call sb_ctr_show

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_ctr_bank - snapshot the counters, and clear the two per-operation ones
sb_ctr_bank:
    push ax
    push bx
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [sb_dbgblk]
    mov ax, [es:bx+2]
    mov [sb_c0mnt], ax
    mov ax, [es:bx+4]
    mov [sb_c0sec], ax
    mov ax, [es:bx+6]
    mov [sb_c0i13], ax
    mov word [es:bx+8], 0           ; longest run and resets are per-operation
    mov word [es:bx+10], 0
    mov word [es:bx+14], 0          ; ...and so is the (LBA, run) trace
    pop es
    pop bx
    pop ax
    ret

; sb_ctr_take - ...and subtract, leaving the deltas in the sb_c0* words
sb_ctr_take:
    push ax
    push bx
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [sb_dbgblk]
    mov ax, [es:bx+2]
    sub ax, [sb_c0mnt]
    mov [sb_c0mnt], ax
    mov ax, [es:bx+4]
    sub ax, [sb_c0sec]
    mov [sb_c0sec], ax
    mov ax, [es:bx+6]
    sub ax, [sb_c0i13]
    mov [sb_c0i13], ax
    mov ax, [es:bx+8]
    mov [sb_c0max], ax
    mov ax, [es:bx+10]
    mov [sb_c0rst], ax
    pop es
    pop bx
    pop ax
    ret

sb_ctr_show:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, sb_l_csec
    mov ax, [sb_c0sec]
    call sb_num
    mov si, sb_l_ci13
    mov ax, [sb_c0i13]
    call sb_num
    mov si, sb_l_cmax
    mov ax, [sb_c0max]
    call sb_num
    mov si, sb_l_cmnt
    mov ax, [sb_c0mnt]
    call sb_num
    mov si, sb_l_crst
    mov ax, [sb_c0rst]
    call sb_num
    mov ax, [sb_c0sec]              ; sectors per call x100
    xor dx, dx
    mov si, 100
    call sb_mul16
    mov bx, [sb_c0i13]
    or bx, bx
    jnz .div
    mov bx, 1
.div:
    mov cx, 0
    call sb_divby
    mov si, sb_d_cspc
    mov cx, 9
    call bl_kv
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_ctr_trace - the (LBA, run) of every int 13h the last read issued
;
; A total cannot tell "reads too much" from "reads the same thing twice", and
; those want completely different fixes. 148 sectors for a 32-sector file is
; either one long walk over sectors nobody asked for, or a short walk done
; four times, and the LBAs say which at a glance: repeats mean re-reading,
; a monotone climb past the file's own length means over-reading.
;
; Four pairs a line, `lba+run`, in the order they were issued.
; -----------------------------------------------------------------------------
sb_ctr_trace:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [sb_dbgblk]
    mov ax, [es:bx+14]              ; slots filled
    mov [sb_tn], ax
    or ax, ax
    jz .out
    mov ax, [es:bx+16]              ; ...and where they are
    mov [sb_tbase], ax
    mov word [sb_tslot], 0
    mov si, sb_l_ctrc
    call bl_sline
.line:
    call bl_lclr
    mov word [sb_tcol], 2
.pair:
    mov bx, [sb_tslot]
    add bx, bx
    add bx, bx
    add bx, [sb_tbase]
    mov ax, [es:bx]                 ; the LBA
    xor dx, dx
    mov di, [sb_tcol]
    mov cx, 5
    call bl_dec
    mov bx, [sb_tslot]
    add bx, bx
    add bx, bx
    add bx, [sb_tbase]
    mov ax, [es:bx+2]               ; ...and the run it asked for
    xor dx, dx
    mov di, [sb_tcol]
    add di, 6
    mov cx, 2
    call bl_dec
    add word [sb_tcol], 10
    inc word [sb_tslot]
    mov ax, [sb_tslot]
    cmp ax, [sb_tn]
    jae .flush                      ; ran out of slots mid-line
    cmp word [sb_tcol], 42          ; four pairs fit; a fifth would not
    jb .pair
.flush:
    call bl_lcommit
    mov ax, [sb_tslot]
    cmp ax, [sb_tn]
    jb .line
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_verify - is BENCH.DAT actually what BENCH.DAT is?
;
; The Makefile fills it with (i>>9) & 0xFF, so **every byte of sector n is n**
; - which makes a whole-file check three instructions per byte and, more to
; the point, makes the two failures this exists to catch unmistakable. A
; transfer that skips sectors leaves a GAP (sector n holding m > n); one that
; re-reads leaves a REPEAT (m < n). Either way the row names the first sector
; that disagrees and what it found there instead.
;
; It is here because SPEC.md 18.91 stopped believing int 13h's AL and started
; trusting CF=0 for the whole request (PERFORMANCE.md Part 9 Set 16). That is
; the right reading of the contract and it is what DOS does, but it is also
; exactly the change that would corrupt a file silently if some BIOS really
; did terminate a multi-sector read early - the old code's whole reason for
; existing. A benchmark that got 6x faster and quietly wrong would be the
; worst possible outcome, so the disk says.
; -----------------------------------------------------------------------------
sb_verify:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [sb_bseg]
    xor bx, bx                      ; ES:BX walks the buffer
    xor cx, cx                      ; CX = sector number, 0..31
.sec:
    mov al, [es:bx]                 ; first byte of the sector...
    cmp al, cl
    jne .bad
    mov al, [es:bx+511]             ; ...and its last, which catches a
    cmp al, cl                      ; transfer that started right and drifted
    jne .bad
    add bx, 512
    inc cx
    cmp cx, 32
    jb .sec
    mov si, sb_l_vok
    mov di, sb_n_ok
    call bl_kvs
    jmp short .out
.bad:
    push ax
    mov si, sb_l_vbad               ; the first sector that disagreed...
    mov ax, cx
    call sb_num
    pop ax
    xor ah, ah
    mov si, sb_l_vgot               ; ...and the sector number it holds
    call sb_num
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_dbgfind - look a published block up in the debug registry (SPEC.md 57)
; in:  AX = the block's tag ('MO', 'DD')
; out: CF=0 with BX = its offset in KERNEL_SEG and ES = KERNEL_SEG;
;      CF=1 if this kernel does not publish it
; clobbers: BX, ES, CF
;
; One word at 0060:000E names a list of (tag, offset) pairs ended by tag 0.
; The tag is also the block's own first word, so this checks that the offset
; it followed landed where it meant to - which is the whole reason a reader
; can trust a number it found by walking a pointer out of a fixed address.
; -----------------------------------------------------------------------------
sb_dbgfind:
    push ax
    push si
    mov bx, KERNEL_SEG
    mov es, bx
    mov si, [es:0x000E]             ; the registry, or 0
    or si, si
    jz .none
.scan:
    mov bx, [es:si]                 ; the tag
    or bx, bx
    jz .none                        ; end of list: not published here
    cmp bx, ax
    je .hit
    add si, 4
    jmp short .scan
.hit:
    mov bx, [es:si+2]               ; ...and the block behind it
    cmp [es:bx], ax                 ; which must lead with the same tag
    jne .none
    pop si
    pop ax
    clc
    ret
.none:
    pop si
    pop ax
    stc
    ret

; sb_r13rate - DX:AX = counts for CX bytes -> DX:AX = bytes per second
sb_r13rate:
    push bx
    push cx
    push si
    push cx
    mov cx, 1193                    ; counts -> ms (1.193182 counts per us)
    call gb_div_sb
    mov bx, ax
    mov cx, dx
    or bx, bx
    jnz .have
    or cx, cx
    jnz .have
    mov bx, 1                       ; too fast to time: do not divide by zero
.have:
    pop ax                          ; the byte count back
    xor dx, dx
    mov si, 1000
    call sb_mul16
    call sb_divby
    pop si
    pop cx
    pop bx
    ret

; The three bodies. AH=02h read, AL=count, CH=cylinder, CL=sector (1-based),
; DH=head, DL=drive 0 (A:), ES:BX = the buffer. The status is banked every
; time, so a row that failed cannot read as a fast row.
; sb_r13go - one raw read through the KERNEL's entry, never int 13h from here
; in:  AL = sectors, CL = first sector (1-based); CH/DX/ES:BX are set here
sb_r13go:
    push es
    push bx
    push cx
    push dx
    mov es, [sb_bseg2]
    mov bx, [sb_r13off]
    mov ch, SB_R13_CYL
    xor dx, dx                      ; head 0, drive 0 (A:)
    call far [sb_r13ent]            ; KERNEL_SEG:dsk_dbg_raw - holds sch_lock
    mov al, ah
    xor ah, ah
    mov [sb_st13], ax
    pop dx
    pop cx
    pop bx
    pop es
    ret

sb_b_r13one:
    mov ax, 1
    mov cl, 1
    call sb_r13go
    ret

sb_b_r13trk:
    mov ax, SB_R13_N
    mov cl, 1
    call sb_r13go
    ret

sb_b_r13nine:
    push cx
    mov word [sb_r13off], 0
    mov cl, 1
.next:
    push cx
    mov ax, 1
    call sb_r13go
    add word [sb_r13off], 512
    pop cx
    inc cl
    cmp cl, SB_R13_N
    jbe .next
    mov word [sb_r13off], 0
    pop cx
    ret

sb_b_rdsml:
    push es
    mov es, [sb_bseg]
    xor bx, bx
    mov si, sb_f_sml
    mov cx, 1024
    xor dx, dx
    call OSAPI_FILE_READ
    pop es
    ret

; --- block 8: the hard disk, if the machine has one ---------------------------
;
; Every row in the floppy block above has a hard-disk twin nobody has ever
; seen, starting with the one that decides whether a hard disk is worth having
; as a system volume at all: a floppy moves 7,457 bytes a second and takes
; ~65 ms to fetch a sector in a run - 2,100 and 238 before SPEC.md 18.91's
; AL fix, and that older pair is quoted all over this tree (PERFORMANCE.md
; Part 2). It is also the only
; measurement of SPEC.md 52's driver on real spinning MFM - rung 0, the
; controller ROM, which is the only rung an 8088 can take at all.
;
; THIS BLOCK NEVER WRITES. It does not format, does not partition, does not
; create a file, and does not delete one. That is not timidity about the code,
; it is a fact about the disk: the machine this was written for
; (docs/FIELD-MACHINES.md, E1) has a real DOS 3.3 install on its C:, with its
; owner's data on it, and a benchmark has no business leaving anything behind
; or removing anything it did not put there. What that costs is the write half
; of the picture - a `bytes/second written` row would need a scratch file, and
; a run interrupted between creating and deleting one would break that promise.
; Reads are the half that can be taken safely, so reads are what this takes.
;
; The file it reads is COMMAND.COM, because a DOS 3.3 system disk has one and
; reading it changes nothing. The report NAMES it, so nobody has to guess what
; the throughput row was reading.
;
; IT MUST SURVIVE THERE BEING NO HARD DISK, which is every other machine
; including the one it was developed on. There is no cheaper way to ask than
; to try: no package-visible call enumerates volumes (OSAPI_VOL_* are the
; driver's, and answer a package CF=1), so the probe is a FILE_GOTO to volume
; index 2 and a refusal prints a line and skips the rows. On a floppy-only
; machine that costs one failed mount, once.
;
; AND IT MUST PUT THE CURRENT VOLUME BACK, on every path including the failed
; probe - which leaves the volume elsewhere by contract. Every name the file
; API resolves goes through ONE global current-volume/current-directory pair
; shared with the Disk windows and the file dialog, so a block that walked off
; to C: and did not come back would send the user's next `S` - save the report
; - to the hard disk.
;
; BOTH PATHS ARE VERIFIED, under QEMU, because neither can be debugged on the
; machine it was written for. With no hard disk the block prints its refusal
; and the report still saves to A:, which is the volume-restore working. With
; one - a 20MB FAT16 partition on an emulated ST-225 geometry, mounted through
; the driver's rung 0 - all four rows produce numbers, the read returns error
; 0 and the file's exact size, and THE DISK IMAGE IS BYTE-FOR-BYTE IDENTICAL
; AFTERWARDS. That last one is the property that matters, and it is the one to
; re-check if anything in this block is ever changed.
sb_hdd:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, sb_s_h_hdd
    call bl_sline
    mov si, sb_s_h_hdd2
    call bl_sline
    call bl_head

    call OSAPI_FILE_HERE            ; bank where we are BEFORE anything moves
    mov [sb_hclus], dx
    mov [sb_hdrv], bl

    call sb_hdd_go
    jc .none

    mov ax, SB_BIGKB                ; on C: now
    call OSAPI_MEM_CLAIM
    jc .noclaim
    mov [sb_bseg], dx

    mov word [bl_n], 16             ; the FAT walk on a 20MB FAT16 volume. On
    mov word [bl_body], sb_b_dfree  ; a floppy the whole FAT is resident and
    mov si, sb_r_hdf                ; this is 40 ms; here the 9-sector window
    xor al, al                      ; (SPEC.md 18.8) has to page, which is the
    call bl_run                     ; number 18.8.1 was written against

    mov word [bl_n], 1              ; the FIRST read: cold, seek and all, and
    mov word [bl_body], sb_b_hddrd  ; the floppy block's cold/warm shape
    mov si, sb_r_hd1
    mov al, 1
    call bl_run
    mov si, sb_l_hderr              ; ...and whether it worked at all, on what
    mov ax, [sb_herr]
    call sb_num
    mov si, sb_l_hdsz
    mov ax, [sb_hsz]
    call sb_num
    mov si, sb_l_hdfn
    mov di, sb_f_cmd
    call bl_kvs
    cmp word [sb_hsz], 0            ; no file, no throughput to measure
    je .nofile

    mov ax, [bl_last]               ; THE COLD READ IS ALSO THE CALIBRATION:
    mov dx, [bl_last+2]             ; pick an iteration count that makes the
    call sb_hdn                     ; warm row about six seconds long
    mov word [bl_body], sb_b_hddrd
    mov si, sb_r_hd2
    mov al, 1
    call bl_run
    mov ax, [bl_last]               ; ...so the rate needs counts per READ
    mov dx, [bl_last+2]
    mov cx, 1
    call bl_mul48
    mov cx, [bl_n]
    call bl_div48
    call bl_get32
    mov [sb_thd], ax
    mov [sb_thd+2], dx
    mov si, sb_l_hdn                ; and SAY how many, because a count nobody
    mov ax, [bl_n]                  ; can see is a row nobody can check
    call sb_num
.nofile:
    mov ax, [sb_bseg]               ; hand the claim back before the mount row
    mov dx, ax
    call OSAPI_MEM_FREE
    call sb_hdd_back

    mov word [bl_n], 4              ; one mount of each volume per iteration:
    mov word [bl_body], sb_b_hdmnt  ; what a copy pays per file (SPEC.md 22.5).
    mov si, sb_r_hdm                ; Both halves are real, and the label says
    mov al, 1                       ; so - the floppy's own mount is the larger
    call bl_run                     ; of the two and the floppy block prices it
    call sb_hdd_back                ; the row ends where it started, but this
                                    ; block does not get to ASSUME that
    call sb_hddrate
    jmp short .out
.noclaim:
    call sb_hdd_back
    mov si, sb_s_noclaim
    call bl_sline
    jmp .out
.none:
    call sb_hdd_back                ; a failed goto leaves the volume elsewhere
    mov si, sb_s_hddno
    call bl_sline
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_boot - the boot timer (SPEC.md 15.4), as ticks and as milliseconds
;
; The one number in this suite that CANNOT be measured by this suite: it is
; over before a package can run. The kernel carries it from the boot sector's
; first instruction to the first desktop frame, which on a floppy machine is
; mostly the kernel read - 125 sectors, which was 125 x 238 ms before
; SPEC.md 18.91's AL fix and is a measured 9.94 s of boot after it
; - so it is the row that any change to the boot path has to answer to.
;
; Both units, deliberately: the tick is what was actually counted and the
; millisecond is what a human compares, and printing the raw count beside the
; derived one is the redundancy Part 6 rule 7 asks for. The resolution is one
; tick, 54.925 ms, and it is quantisation rather than noise - a boot that
; measures 84 ticks took between 84 and 85 of them.
sb_boot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_BOOT_TICKS
    cmp ax, 0xFFFF                  ; the boot sector never stamped it
    je .none
    push ax
    mov si, sb_l_boott
    call sb_num
    pop ax
    xor dx, dx                      ; ms = ticks * 54925 / 1000, in 48 bits
    mov si, 54925                   ; because ticks * 54925 leaves the word
    call sb_mul16                   ; behind immediately
    mov cx, 1
    call bl_mul48
    mov cx, 1000
    call bl_div48
    call bl_get32
    mov si, sb_l_bootms
    mov cx, 9
    call bl_kv
    jmp short .out
.none:
    mov si, sb_l_boott
    mov di, sb_n_nostamp
    call bl_kvs
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_hdn - [bl_n] = the number of reads that makes about six seconds, from
;          ONE read's counts in DX:AX. Clamped to 4..240.
;
; The drive is a ten-fold unknown before the first run - an ST-225 through a
; controller ROM could be 60 KB/s or 500 - and method T quantises to 54.92 ms,
; so any FIXED iteration count is either a wasted trip (too coarse to quote)
; or a wasted row (seconds spent past the point of diminishing return). Six
; seconds is about a hundred ticks, so under 1%, and the benchmark is allowed
; to take its time: it has to be accurate and useful, not fast.
;
; There is no two-size decomposition here, deliberately - OSAPI_FILE_READ
; refuses a buffer smaller than the file (FERR_BIG) rather than reading a
; prefix, so a per-call term and a per-byte term cannot be separated from one
; file. What this row measures is one whole-file read, seek included.
sb_hdn:
    push ax
    push bx
    push cx
    push dx
    mov bx, ax                      ; CX:BX = one read's counts
    mov cx, dx
    mov ax, 0x3BB4                  ; DX:AX = 7,159,092 counts, six seconds
    mov dx, 0x006D
    call sb_divby
    or dx, dx                       ; a quotient past a word is past the cap
    jnz .cap
    cmp ax, 240
    jbe .lo
.cap:
    mov ax, 240
.lo:
    cmp ax, 4
    jae .set
    mov ax, 4
.set:
    mov [bl_n], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_hddrate - bytes/second from the read row, the floppy block's arithmetic
sb_hddrate:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp word [sb_hsz], 0            ; nothing read: nothing to divide
    je .out
    mov ax, [sb_thd]
    mov dx, [sb_thd+2]
    mov cx, 1193
    call gb_div_sb
    mov bx, ax
    mov cx, dx
    or bx, bx
    jnz .have
    or cx, cx
    jnz .have
    mov bx, 1                       ; too fast to time: do not divide by zero
.have:
    mov ax, [sb_hsz]
    xor dx, dx
    mov si, 1000
    call sb_mul16
    call sb_divby
    mov si, sb_d_hdrate
    mov cx, 9
    call bl_kv
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_hdd_go   - make volume index 2 current, at its root. CF = there is none.
; sb_hdd_back - put the banked volume back. Safe to call twice.
sb_hdd_go:
    push ax
    push bx
    push dx
    xor dx, dx
    mov bl, 2
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

sb_hdd_back:
    push ax
    push bx
    push dx
    mov dx, [sb_hclus]
    mov bl, [sb_hdrv]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

sb_b_hddrd:
    push es
    mov es, [sb_bseg]
    xor bx, bx
    mov si, sb_f_cmd
    mov cx, SB_BIGKB * 1024
    xor dx, dx
    call OSAPI_FILE_READ            ; out CF=0 and DX:AX = the file's size
    jc .err
    mov [sb_hsz], ax
    mov word [sb_herr], 0
    pop es
    ret
.err:
    mov [sb_herr], ax
    mov word [sb_hsz], 0
    pop es
    ret

sb_b_hdmnt:
    call sb_hdd_go
    call sb_hdd_back
    ret

%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

sb_tpl:
    dw 7, 22, 632, 448
    dw sb_ttl, sb_paint, sb_onkey, sb_onclick

sb_ttl:     db 'Sys Bench', 0

; label, body, 8086 nominal clocks x100, iterations.
;
; The nominal column is Intel's 8086 timing table, the one every margin in this
; tree has been computed from: EA calculation included where the operand needs
; one, and the reload instructions counted where a row has them. It is the
; BOOK figure and not a claim about this machine - the whole point of the row
; beside it is to find out how far apart the two are.
sb_ctab:
    dw sb_c_nop,     sb_b_nop,       300, 800    ; nop
    dw sb_c_movrr,   sb_b_movrr,     200, 800    ; mov r16,r16
    dw sb_c_add,     sb_b_add,       300, 800    ; add r16,r16
    dw sb_c_inc,     sb_b_inc,       200, 800    ; inc r16
    dw sb_c_cmp,     sb_b_cmp,       300, 800    ; cmp r16,r16
    dw sb_c_xchg,    sb_b_xchg,      300, 800    ; xchg ax,r16
    dw sb_c_shl1,    sb_b_shl1,      200, 800    ; shl r16,1
sb_e_shl4:
    dw sb_c_shlcl,   sb_b_shlcl,    2400, 400    ; shl r16,cl  (8 + 4*4)
sb_e_shl13:
    dw sb_c_shlcl13, sb_b_shlcl13,  6000, 400    ; shl r16,cl  (8 + 4*13)
    dw sb_c_load,    sb_b_load,     1400, 400    ; mov ax,[disp16]  (8 + EA 6)
    dw sb_c_store,   sb_b_store,    1500, 400    ; mov [disp16],ax  (9 + EA 6)
    dw sb_c_noovr,   sb_b_noovr,    1300, 400    ; mov al,[si]      (8 + EA 5)
    dw sb_c_ovr,     sb_b_ovr,      1500, 400    ; ...with a segment override
    dw sb_c_idx,     sb_b_idx,      1700, 400    ; mov al,[bx+disp16] (8 + EA 9)
    dw sb_c_jmp,     sb_b_jmp,      1500, 400    ; jmp short, taken
    dw sb_c_pushpop, sb_b_pushpop,  1900, 300    ; push ax + pop ax (11 + 8)
    dw sb_c_callret, sb_b_callret,  2700, 300    ; call near + ret  (19 + 8)
sb_e_mul:
    dw sb_c_mul,     sb_b_mul,      12900, 100   ; mov ax,imm + mul r16 (4+125)
    dw sb_c_mulm,    sb_b_mulm,     13800, 100   ; ...+ mul word [m] (4+128+EA 6)
sb_e_div:
    dw sb_c_div,     sb_b_div,      16000, 60    ; xor + mov + div r16 (3+4+153)
sb_ctab_end:

SB_NCPU  equ (sb_ctab_end - sb_ctab) / 8
; The rows other blocks reach for BY INDEX, derived from the table rather
; than written down: sb_mhz reads the machine's clock out of the two
; execution-bound rows and sb_shlbit subtracts the two shift rows, so a row
; inserted above them used to move the answer instead of the row.
SB_I_MUL   equ (sb_e_mul   - sb_ctab) / 8
SB_I_DIV   equ (sb_e_div   - sb_ctab) / 8
SB_I_SHL4  equ (sb_e_shl4  - sb_ctab) / 8
SB_I_SHL13 equ (sb_e_shl13 - sb_ctab) / 8

sb_c_nop:     db 'nop', 0
sb_c_movrr:   db 'mov r16,r16', 0
sb_c_add:     db 'add r16,r16', 0
sb_c_inc:     db 'inc r16', 0
sb_c_cmp:     db 'cmp r16,r16', 0
sb_c_xchg:    db 'xchg ax,r16', 0
sb_c_shl1:    db 'shl r16,1', 0
sb_c_shlcl:   db 'shl r16,cl (4)', 0
sb_c_shlcl13: db 'shl r16,cl (13)', 0
sb_c_load:    db 'mov ax,[disp16]', 0
sb_c_store:   db 'mov [disp16],ax', 0
sb_c_noovr:   db 'mov al,[si]', 0
sb_c_ovr:     db 'mov al,[es:si]', 0
sb_c_idx:     db 'mov al,[bx+disp16]', 0
sb_c_jmp:     db 'jmp short (taken)', 0
sb_c_pushpop: db 'push ax + pop ax', 0
sb_c_callret: db 'call near + ret', 0
sb_c_mul:     db 'mov ax,i + mul r16', 0
sb_c_mulm:    db 'mov ax,i + mul [m]', 0
sb_c_div:     db 'xor+mov+div r16', 0

sb_f_out:   db 'SYSBENCH.TXT', 0
sb_f_cmd:   db 'COMMAND.COM', 0
sb_f_big:   db 'BENCH.DAT', 0
sb_f_sml:   db 'BENCHSML.DAT', 0

sb_n_8086:  db '8086/8088 (tier 0)', 0
sb_n_286:   db '80286 (tier 1)', 0
sb_n_386:   db '80386+ (tier 2)', 0
sb_n_nostamp: db 'unknown (boot sector predates the timer)', 0
sb_n_yes:   db 'yes', 0
sb_n_no:    db 'no (CF set)', 0

sb_h_1:     db 'The machine under the graphics: 8086 book clocks against this CPU, RAM', 0
sb_h_2:     db 'bandwidth, the clock, what the kernel own interrupts cost, and the floppy.', 0
sb_h_3:     db '   R  or the Bench menu   run it.  About 40 seconds on a 4.77MHz 8088 -', 0
sb_h_4:     db '                          most of it the two 16KB floppy reads - and the', 0
sb_h_5:     db '                          machine is FROZEN throughout. Watch this line.', 0
sb_h_6:     db '                          it SAVES ITSELF when it finishes.  S or the', 0
sb_h_6b:    db '                          Bench menu writes it again, after a disk swap.', 0
sb_h_7:     db '   Space PgDn PgUp Up Dn Home End   page through it afterwards.', 0

sb_s_h_hdd:  db '-- the hard disk (SPEC.md 52), if this machine has one --', 0
sb_s_h_hdd2: db '   READ ONLY: nothing here formats, partitions, writes or deletes.', 0
sb_s_hddno:  db 'No volume at index 2 - no hard disk mounted. Rows skipped.', 0

sb_p_head:  db 'running: reading the machine...', 0
sb_p_cpu:   db 'running: instruction timings (1 of 8)', 0
sb_p_mem:   db 'running: RAM bandwidth (2 of 8)', 0
sb_p_clk:   db 'running: the clock and the timers (3 of 8)', 0
sb_p_isr:   db 'running: what the kernel interrupts cost - 4 seconds (4 of 8)', 0
sb_p_os:    db 'running: the API far-call floor (5 of 8)', 0
sb_p_dsk:   db 'running: the floppy - two 16KB reads, the slow one (6 of 8)', 0
sb_p_hdd:   db 'running: the hard disk, if there is one (7 of 8)', 0
sb_p_mou:   db 'running: the mouse port and its identify burst (8 of 8)', 0

sb_s_ttl1:  db 'os8088 SYSBENCH - cpu, bus, memory, clock, scheduler, floppy', 0
sb_s_ttl2:  db '============================================================', 0

sb_l_cpu:     db 'cpu tier', 0
sb_l_feat:    db 'cpu feature bits', 0
sb_l_adapter: db 'video kind 0/1/2', 0
sb_l_scrw:    db 'screen width px', 0
sb_l_scrh:    db 'screen height px', 0
sb_l_boott:   db 'boot ticks', 0
sb_l_bootms:  db 'boot ms', 0
sb_l_kern:    db 'kernel span KB', 0
sb_l_img:     db 'kernel image KB', 0
sb_l_buf:     db 'fat+stacks+bufs KB', 0
sb_l_heap:    db 'claim heap KB', 0
sb_l_claim:   db 'claimed out of it KB', 0
sb_l_mlarge:  db 'largest free run KB', 0
sb_l_mtotal:  db 'total free KB', 0
sb_l_xms:     db 'above 1MB free KB', 0
sb_l_snd:     db 'sound caps word', 0
sb_l_drive:   db 'current volume', 0
sb_l_ovh:     db 'loop overhead counts', 0
sb_l_rtc:     db 'int 1Ah AH=02h', 0
sb_l_rtch:    db '  its hour, BCD', 0
sb_l_derr:    db '  read error code', 0
sb_l_dsz:     db '  bytes read', 0
sb_l_hderr:   db '  hdd read error code', 0
sb_l_hdsz:    db '  hdd bytes read', 0

; --- the mouse (SPEC.md 9.4.1/9.4.2) -----------------------------------------
sb_s_h_mou:  db '-- the mouse: the port contest and the identify burst (SPEC.md 9.4.1) --', 0
sb_s_h_mou2: db '   STATE, not a measurement: base and first byte are HEX, rest decimal.', 0
sb_s_h_mou3: db '   A mouse that identified reads: first byte 4D, identified 1, stamp 0.', 0
sb_s_mnone:  db '   this kernel publishes no mouse block (built before SPEC.md 9.4.2).', 0
sb_l_mb0:    db '  COM1 base (0=absent)', 0
sb_l_mb1:    db '  COM2 base (0=absent)', 0
sb_l_mid0:   db '  ident bytes COM1', 0
sb_l_mid1:   db '  ident bytes COM2', 0
sb_l_mfb0:   db '  first byte COM1 hex', 0
sb_l_mfb1:   db '  first byte COM2 hex', 0
sb_l_mok0:   db '  identified COM1', 0
sb_l_mok1:   db '  identified COM2', 0
sb_l_mnd0:   db '  packets needed COM1', 0
sb_l_mnd1:   db '  packets needed COM2', 0
sb_l_mhpt:   db '  poller stamp (0=nvr)', 0
sb_l_mhps:   db '  poller state', 0
sb_l_msn:    db '  mouse found', 0
sb_l_mpt:    db '  winning row (0/2)', 0
sb_l_mln:    db '  winning IRQ hex 10=4', 0

sb_s_h_lad:  db '-- the clock: which rung of the RTC ladder answered (SPEC.md 37.90) --', 0
sb_s_h_lad2: db '   STATE, not a measurement. tier: 0 none 1 AT 2 MM58167 3 RP5C01 4 BIOS.', 0
sb_s_h_lad3: db '   probe stop: 00 not run, FF passed, 01-07 the gate that refused.', 0
sb_s_cnone:  db '   this kernel publishes no clock block (built before SPEC.md 37.92).', 0
sb_l_ctier:  db '  tier that answered', 0
sb_l_cref:   db '  int 1Ah readable', 0
sb_l_cstep:  db '  NS probe stop hex', 0
sb_l_cr00:   db '  NS reg 00 hex', 0
sb_l_cr08:   db '  NS reg 08 hex', 0
sb_l_csig:   db '  NS 0D wr AA rd hex', 0
sb_l_cr08w:  db '  NS 08 wr FF rd hex', 0
sb_l_mrn0:   db '  run reached COM1', 0
sb_l_mrn1:   db '  run reached COM2', 0
sb_l_hdfn:    db '  hdd file read', 0
sb_l_hdn:     db '  hdd warm reads N', 0

sb_s_pit1:  db 'One PIT count is 838 ns and EXACTLY four 4.77MHz CPU clocks: both', 0
sb_s_pit2:  db 'divide the 14.31818MHz crystal, the PIT by 12 and the 8088 by 3.', 0
sb_s_pit3:  db 't = tick-timed, ! = near the 55ms wrap, w = it LAPPED and is tick-timed.', 0
sb_s_warn1: db 'CAUTION: under QEMU every time column is the HOST speed. Boot with', 0
sb_s_warn2: db '-icount shift=3,sleep=off and read counts as guest INSTRUCTIONS.', 0
sb_s_warn3: db 'A tick-timed (t) row of 0 counts means it finished inside one 55ms', 0
sb_s_warn4: db 'tick - true on a fast host, and never true on the machine this is for.', 0

sb_s_h_cpu:  db '-- cpu: 32 copies of one instruction per iteration --', 0
sb_s_h_cpu2: db '   (us/op is the whole 32, not one instruction - see the table below)', 0
sb_s_h_der:  db '-- the same rows as clocks, against the 8086 book --', 0
sb_s_h_der2: db 'instruction           measx100  nom x100  ratiox100', 0
sb_s_h_mem:  db '-- RAM bandwidth: 2048 bytes an iteration (gfxbench has the VRAM) --', 0
sb_s_h_clk:  db '-- the clock and the timers --', 0
sb_s_h_isr:  db '-- what the kernel own interrupts cost --', 0
sb_s_h_isr2: db '   the same work timed with interrupts off, then with them on', 0
sb_s_h_os:   db '-- the API far-call floor and the scheduler --', 0
sb_s_h_dsk:  db '-- the floppy: one int 13h per sector, so latency not bandwidth --', 0

sb_r_sw:   db 'RAM rep stosw', 0
sb_r_sb:   db 'RAM rep stosb', 0
sb_r_mw:   db 'RAM rep movsw', 0
sb_r_mb:   db 'RAM rep movsb', 0
sb_r_sc:   db 'RAM repne scasb', 0
sb_r_rm:   db 'RAM read-mod-write', 0

sb_r_pit:  db 'PIT latch + read', 0
sb_r_gt:   db 'OSAPI GET_TICKS', 0
sb_r_1a:   db 'int 1Ah AH=00h', 0
sb_r_k16:  db 'int 16h AH=01h', 0

sb_r_wp:   db 'work, interrupts off', 0
sb_r_wt:   db 'work, interrupts on', 0

sb_r_nc:   db 'near call + ret', 0
sb_r_fc:   db 'API far call cell', 0
sb_r_yl:   db 'TASK_YIELD', 0
sb_r_rn:   db 'RAND', 0
sb_r_hr:   db 'FILE_HERE', 0
sb_r_df:   db 'FILE_DFREE', 0

sb_r_d1:   db 'read 16K, cold motor', 0
sb_r_d2:   db 'read 16K, warm', 0
sb_r_ds:   db 'read 1 sector file', 0
sb_r_hdf:  db 'HDD FILE_DFREE', 0
sb_r_hd1:  db 'HDD read 1st, cold', 0
sb_r_hd2:  db 'HDD read warm, xN', 0
sb_r_hdm:  db 'HDD mount + back to A', 0

sb_d_mhzmul: db 'est CPU MHz x100 MUL', 0
sb_d_mhzdiv: db 'est CPU MHz x100 DIV', 0
sb_d_tick:   db 'PIT/tick want 65536', 0
sb_d_isr:    db 'interrupt load pct', 0
sb_d_rate:   db 'floppy bytes/sec', 0

sb_s_h_r13:  db '-- the same drive with NO kernel code in the way: raw int 13h --', 0
sb_s_h_r132: db '   READ ONLY. A 9-sector track is 4,608 bytes. THE TURN DEPENDS ON', 0
sb_s_h_r133: db '   THE DRIVE: a 360K drive is 300 RPM (200 ms), a 1.2M one 360 RPM', 0
sb_s_h_r134: db '   (167 ms) even on 360K media. 1:1 / 2:1 / one-per-turn B/s are', 0
sb_s_h_r135: db '   23040 / 11520 / 2560 at 300, and 27648 / 13824 / 3072 at 360.', 0
sb_s_h_r136: db '   The B/s ROWS below are measured, not derived, so they are right', 0
sb_s_h_r137: db '   either way - and `int 13h 1 sector` is ONE turn, so it says which.', 0
sb_l_dptn:   db 'DPT EOT (18.92 patches)', 0
sb_l_dpts:   db 'DPT step/head unload', 0
sb_l_dpth:   db 'DPT head settle ms', 0
sb_l_dptm:   db 'DPT motor start /8 s', 0
sb_r_131:    db 'int 13h 1 sector', 0
sb_r_139:    db 'int 13h track, 1 call', 0
sb_r_13n:    db 'int 13h track, 9 calls', 0
sb_l_r13st:  db 'int 13h last status AH', 0
sb_d_r13b:   db 'bios track 1 call B/s', 0
sb_d_r13s:   db 'bios track 9 calls B/s', 0
sb_s_nodbg:  db 'This kernel carries no disk instrument - build DISKCNT=1.', 0
sb_s_h_ctr:  db '-- what os8088 own transfer path ISSUES, per operation --', 0
sb_s_h_ctr2: db '   the 1-sector read is the same overhead with no data in it', 0
sb_l_c16:    db '  one 16KB FILE_READ:', 0
sb_l_c1:     db '  one 1-sector FILE_READ:', 0
sb_l_cmnt:   db 'disk_mount calls', 0
sb_l_ctrc:   db '  every int 13h it issued, as lba+run:', 0
sb_l_vok:    db 'data check, 32 sectors', 0
sb_n_ok:     db '           OK', 0
sb_l_vbad:   db 'DATA WRONG at sector', 0
sb_l_vgot:   db '  ...it holds sector', 0
sb_l_csec:   db 'sectors moved', 0
sb_l_ci13:   db 'int 13h calls', 0
sb_l_cmax:   db 'longest run, sectors', 0
sb_l_crst:   db 'controller resets', 0
sb_d_cspc:   db 'sectors per call x100', 0
sb_d_hdrate: db 'hdd bytes/sec', 0
sb_d_shlbit: db 'shl clk/bit x100 ~400', 0

sb_s_noclaim: db '  (no 32KB heap claim available: the file rows were skipped)', 0

sb_s_end1:  db 'End of report. R re-runs it, S saves SYSBENCH.TXT to the current', 0
sb_s_end2:  db 'volume and directory (SPEC.md 19.2).', 0

sb_about:   db 'Sys Bench', 0

    OS88_MENUSET sb_menus, sb_about, sb_oncmd
        OS88_MENU sb_m_bench, sb_i_bench, 3
    OS88_MENUSET_END sb_menus

sb_m_bench: db 'Bench', 0
sb_i_bench: dw sb_it_run, sb_it_save, sb_it_top
sb_it_run:  db 'Run', 0
sb_it_save: db 'Save Report', 0
sb_it_top:  db 'Top of Report', 0

; The bss offsets past the scalars are derived, never hand-totalled: a figure
; that is too small is a package writing over benchlib's arena, which assembles
; cleanly and produces a report full of plausible nonsense.
SB_O_SYSKB equ 124
SB_O_RES   equ SB_O_SYSKB + SYSKB_SIZE
SB_O_RROW  equ SB_O_RES + SB_NCPU * 4
SB_O_RAM   equ SB_O_RROW + SB_BWROWS * 2
SB_O_RAM2  equ SB_O_RAM + SB_BWBYTES
SB_BSS_OWN equ ((SB_O_RAM2 + SB_BWBYTES + 511) / 512) * 512   ; benchlib's base must be
                                        ; 512-ALIGNED: bl_out is an int 13h target

    align 512                   ; ...and os88_image_end likewise, which this
                                ; costs up to 511 bytes of image and buys the
                                ; alignment of every bss offset below
    OS88_BSS SB_BSS_OWN + BL_BSS_SIZE
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
sb_win      equ os88_image_end + 0     ; word
sb_vw       equ os88_image_end + 2     ; word
sb_vh       equ os88_image_end + 4     ; word
sb_kind     equ os88_image_end + 6     ; byte
sb_pad0     equ os88_image_end + 7     ; byte
sb_cputier  equ os88_image_end + 8     ; word: AL tier, AH feature bits
sb_mlarge   equ os88_image_end + 10    ; word
sb_mtotal   equ os88_image_end + 12    ; word
sb_snd      equ os88_image_end + 14    ; word
sb_xms      equ os88_image_end + 16    ; word
sb_cw       equ os88_image_end + 18    ; word
sb_ch       equ os88_image_end + 20    ; word
sb_i        equ os88_image_end + 22    ; word: the sb_ctab walk's index
sb_ent      equ os88_image_end + 24    ; word: ...and the entry it is at
sb_seg      equ os88_image_end + 26    ; word: bandwidth target segment
sb_tab      equ os88_image_end + 28    ; word: ...and its row table
sb_scr      equ os88_image_end + 30    ; word: the load/store rows' operand
sb_bseg     equ os88_image_end + 32    ; word: the file rows' heap claim
sb_rsz      equ os88_image_end + 34    ; word: bytes the big read returned
sb_rerr     equ os88_image_end + 36    ; word: ...or the FERR_* it refused with
sb_bcnt     equ os88_image_end + 38    ; dword: the baseline row's raw counts
sb_meas     equ os88_image_end + 42    ; dword: the clocks column being built
sb_wp       equ os88_image_end + 46    ; dword: the workload, interrupts off
sb_wt       equ os88_image_end + 50    ; dword: ...and on
sb_td1      equ os88_image_end + 54    ; dword: the cold read
sb_td2      equ os88_image_end + 58    ; dword: ...and the warm one (58..61)
sb_ran      equ os88_image_end + 62    ; byte: has the suite been run yet?
sb_hdrv     equ os88_image_end + 63    ; byte: the volume the hdd block banked
sb_hclus    equ os88_image_end + 64    ; word: ...and its directory cluster
sb_hsz      equ os88_image_end + 66    ; word: bytes the hard-disk read got
sb_herr     equ os88_image_end + 68    ; word: ...or the FERR_* it refused with
sb_thd      equ os88_image_end + 70    ; dword: that read's counts (70..73)
sb_bseg2    equ os88_image_end + 74    ; word: the raw int 13h block's buffer,
                                       ; page-safe inside the claim above
sb_bcnt13   equ os88_image_end + 76    ; word: sectors the last int 13h asked
sb_st13     equ os88_image_end + 78    ; word: ...and the AH it answered with
sb_t131     equ os88_image_end + 80    ; dword: one sector, one call
sb_t139     equ os88_image_end + 84    ; dword: nine sectors, one call
sb_t13n     equ os88_image_end + 88    ; dword: nine sectors, nine calls (92)
sb_dbgblk   equ os88_image_end + 92    ; word: SPEC.md 18.94's block, or 0
sb_r13ent   equ os88_image_end + 94    ; dword: ...and its FAR entry (94..97)
sb_r13off   equ os88_image_end + 98    ; word: the raw read's buffer offset
sb_c0sec    equ os88_image_end + 100   ; word: sectors one 16KB read moved
sb_c0i13    equ os88_image_end + 102   ; word: ...and int 13h calls it took
sb_c0max    equ os88_image_end + 104   ; word: the longest run in it
sb_c0rst    equ os88_image_end + 106   ; word: ...and controller resets (107)
sb_c0mnt    equ os88_image_end + 108   ; word: disk_mount calls in it
sb_tcol     equ os88_image_end + 110   ; word: the trace line's next column
sb_tslot    equ os88_image_end + 112   ; word: ...and the slot it is printing
sb_tn       equ os88_image_end + 114   ; word: how many slots there are
sb_tbase    equ os88_image_end + 116   ; word: where the trace array is (117)
sb_mbase    equ os88_image_end + 118   ; word: -> mou_bases (SPEC.md 9.4.2)
sb_mstate   equ os88_image_end + 120   ; word: -> the mouse state span (121)
sb_cstate   equ os88_image_end + 122   ; word: -> the clock state span (123),
                                       ; SPEC.md 37.92
sb_syskb    equ os88_image_end + SB_O_SYSKB    ; SYSKB_SIZE bytes
sb_res      equ os88_image_end + SB_O_RES      ; SB_NCPU dwords
sb_rrow     equ os88_image_end + SB_O_RROW     ; SB_BWROWS words
sb_ram      equ os88_image_end + SB_O_RAM      ; the bandwidth source...
sb_ram2     equ os88_image_end + SB_O_RAM2     ; ...and destination

    BL_BSS os88_image_end + SB_BSS_OWN
