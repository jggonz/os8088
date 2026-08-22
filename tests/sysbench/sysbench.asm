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
; The report is a FILE, and `tools/os88flush.py` is how it reaches the host -
; MartyPC keeps the guest's writes in RAM, so nothing lands on the image until
; something spends them. `os88flush.Flush(marty=m).volume(N).read(...)` from a
; driver script, or `os88flush.py <addr> get N <FILE> <out>` from the shell.
; DO NOT page it off the screen: tests/benchlib.inc's bl_save header has the
; recipe and docs/TESTING.md has the three traps in front of the RUN.
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
SB_FIND_N   equ 24                ; FILE_FIND calls per timed row (SPEC.md
                                  ; 18.95.2). One is tens of microseconds and
                                  ; the PIT wraps at 55 ms, so this stays two
                                  ; orders inside a lap on the slowest machine
SB_FIND_W   equ 4                 ; ...and whole enumerations per walk row,
                                  ; which is N times as long again
SB_FIND_X   equ 496               ; 32*31/2: the quadratic term for a 32-entry
                                  ; directory, which is the size the operations
                                  ; this question came from actually walk
SB_FIND_MAX equ 512               ; a directory that never ends is a corrupt
                                  ; one - bound the count, do not hang on it
SB_RAH_KB   equ 8                 ; the cache-capacity block's own buffer, and
                                  ; the ceiling on the cluster probe: one
                                  ; cluster has to fit it
SB_RAH_GAP  equ 9216              ; ...and the stride's floor: TWO 9-sector
                                  ; tracks, so two reads can never share a
                                  ; chunk however a fill was bounded
SB_RAH_WSTEP equ 2                ; working sets go 2, 4, 6 ... which brackets
SB_RAH_WMAX  equ 12               ; the run count to a pair.
                                  ;
                                  ; THE SWEEP'S CEILING IS WHAT SIZES
                                  ; bigfile.dat, and it was 18 for a 170KB
                                  ; file. On a one-drive machine that file is
                                  ; most of a 360KB field disk and the reports
                                  ; are the point (docs/FIELD-MACHINES.md), so
                                  ; it is 12 and the file is 104KB: the
                                  ; deepest byte a floppy sweep touches is
                                  ; (12-1) x 9216 + 1024 = 102,400. It still
                                  ; brackets DSK_RAH_RUNS = 8 with a step of
                                  ; headroom - free through 8, missing at 10,
                                  ; confirmed at 12 - and a run count raised
                                  ; past 10 would report no cliff rather than
                                  ; a wrong one, which the row below says in
                                  ; words. Raising this means growing
                                  ; bigfile.dat in the Makefile to match; the
                                  ; sweep stops honestly either way.
SB_WR_KB     equ 128              ; the LARGE write, KB. Stepped down by halves
SB_WR_MIN    equ 8                ; to this if the heap or the volume cannot
                                  ; take it, and the row SAYS which it got -
                                  ; a benchmark that silently measured 8KB
                                  ; where the reader assumed 128 is worse
                                  ; than one that refused
SB_WR_CHUNK  equ 8                ; ...and the append chunk, KB: the copy
                                  ; engine's shape (SPEC.md 22.5), which is
                                  ; what makes that row comparable to a copy
SB_WR_SLACK  equ 8                ; KB of the volume left alone. A benchmark
                                  ; that fills the disk it is measuring has
                                  ; changed the thing under test
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
    call OSAPI_WM_SNAP              ; every adapter (11.94); PRESERVES FLAGS
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
    call sb_fdd                     ; ...and SPEC.md 57.5, for a reason that is
                                    ; the same one a fourth time and sharper:
                                    ; the block exists BECAUSE no emulator can
                                    ; be asked what a 765 says about a drive
                                    ; that is not plugged in
    call sb_video                   ; ...and SPEC.md 57.4, the third of them:
                                    ; two cards on two monitors is the field
                                    ; machine's own arrangement and the one
                                    ; question no emulator can be asked
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
    call sb_build                   ; ...and WHICH BUILD (PERFORMANCE.md Part 8.1), which
                                    ; the two KB rows above cannot say: they
                                    ; round, and three disks whose kernels
                                    ; differ can print the same numbers
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
    call bl_head
    mov word [sb_i], 0
.next:
    mov bx, [sb_i]
    cmp bx, SB_NCPU
    jae .done
    call sb_entof
    mov ax, [bx+2]                  ; the body
    mov [bl_body], ax
    mov ax, [bx+10]                 ; ...and its iteration count
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

; sb_entof - BX = a row index -> BX = its sb_ctab entry (PERFORMANCE.md Part 8.1)
;
; The stride lives HERE and in SB_ENTSZ and nowhere else. It was 8 and four
; sites open-coded `shl bx, 3`; a third book per row made it 12, and an
; open-coded shift is a silent wrong row rather than a build error.
; Clobbers BX and flags only - callers hold results in AX/DX/CX/SI.
sb_entof:
    push ax
    push cx
    mov ax, bx
    mov cl, 3
    shl bx, cl                      ; i*8
    mov cl, 2
    shl ax, cl                      ; i*4
    add bx, ax                      ; ...= i*12
    add bx, sb_ctab
    pop cx
    pop ax
    ret

; sb_nomof - BX = an sb_ctab entry -> AX = the nominal for THIS cpu, x100
;
; Three books ride in the row (+4 8086, +6 286, +8 386) and [sb_cputier]'s low
; byte picks the column. Anything newer than a 386 reads the 386 column: this
; is a period OS and the tier ladder stops there, and a 486 running the 386's
; book is a closer answer than a 486 running the 8086's - which is what every
; non-8088 machine got until PERFORMANCE.md Part 8.1.
sb_nomof:
    push bx
    push cx
    mov al, [sb_cputier]
    xor ah, ah
    cmp ax, 2
    jbe .have
    mov ax, 2
.have:
    shl ax, 1                       ; a word per book
    add bx, ax
    mov ax, [bx+4]
    pop cx
    pop bx
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
    mov si, sb_s_h_der              ; ...and SAY which book, because the ratio
    cmp byte [sb_cputier], CPU_286  ; column is meaningless without it and the
    jb .book                        ; report is read months later, off a disk,
    mov si, sb_s_h_d286             ; by somebody who was not at the machine
    je .book
    mov si, sb_s_h_d386
.book:
    call bl_sline
    mov si, sb_s_h_der2
    call bl_sline
    mov word [sb_i], 0
.next:
    mov bx, [sb_i]
    cmp bx, SB_NCPU
    jae .done
    call sb_entof
    mov [sb_ent], bx
    mov bx, [sb_i]
    shl bx, 1
    shl bx, 1
    add bx, sb_res
    mov ax, [bx]
    mov dx, [bx+2]
    mov bx, [sb_ent]
    mov cx, [bx+10]                 ; N
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
    mov bx, [sb_ent]                ; ...the book figure for THIS cpu
    call sb_nomof
    xor dx, dx
    mov di, 32
    mov cx, 9
    call bl_dec
    mov ax, [sb_meas]               ; ...and measured / nominal, x100
    mov dx, [sb_meas+2]
    mov bx, [sb_ent]
    call sb_nomof
    mov bx, ax
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
    call sb_entof
    call sb_nomof                   ; ...this cpu's book, not the 8086's
    mov si, ax                      ; SI = the nominal, clocks x100
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
    call sb_entof
    mov cx, [bx+10]                 ; CX = the row's iteration count
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
    ; ...and INTO THIS MACHINE'S OWN CLOCKS (PERFORMANCE.md Part 8.1). sb_clkx100 counts
    ; in 4.77MHz periods, because that is what a PIT count is four of - so on a
    ; 16MHz 286 a one-clock-per-bit shift measures 0.28 of one. A true number
    ; in the wrong currency, and unreadable against any book. Scaling by the
    ; MUL estimate above puts it back: x100 * MHzx100 / 477, and on a 4.77MHz
    ; machine that factor is 1 BY CONSTRUCTION, so tier 0's published figure
    ; does not move.
    push dx                         ; bank the gap: sb_est below runs the same
    push ax                         ; 48-bit accumulator this came out of
    mov bx, SB_I_MUL
    call sb_est                     ; DX:AX = est MHz x100
    mov bx, ax
    mov cx, dx                      ; CX:BX = it
    pop ax
    pop dx                          ; DX:AX = the per-bit gap again
    call sb_mul16                   ; ...times the estimate
    mov bx, 477
    xor cx, cx
    call sb_divby
    jmp short .show
.bad:
    xor ax, ax
    xor dx, dx
.show:
    mov si, sb_d_shlbit
    mov cx, 9
    call bl_kv
    ; What the book says for THIS cpu, and it is DERIVED FROM THE SAME TWO
    ; ROWS the measurement subtracts rather than being a fourth constant
    ; somebody has to keep in step: (nom13 - nom4) / 9. 8086 400, 286 100,
    ; 386 0 - the barrel shifter, which is the whole reason this row stopped
    ; meaning anything past tier 0.
    mov bx, SB_I_SHL13
    call sb_entof
    call sb_nomof
    mov cx, ax
    mov bx, SB_I_SHL4
    call sb_entof
    call sb_nomof
    sub cx, ax
    mov ax, cx
    xor dx, dx
    mov cx, 1                       ; load the accumulator, then the 9 bits
    call bl_mul48
    mov cx, 9
    call bl_div48
    call bl_get32
    mov si, sb_d_shlnom
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
    call bl_sline   ; ...and NO bl_head: that heading names
                                    ; N/counts/us-per-op, and not one row here
                                    ; is a measurement

    mov ax, DBG_TAG_MOUSE           ; SPEC.md 57's registry
    call bl_dbgfind
    jc .nodbg
    mov ax, [es:bx+2]
    mov [sb_mbase], ax              ; -> mou_bases, 2 words, 0 = no UART there
    mov ax, [es:bx+4]
    mov [sb_mstate], ax             ; -> the 34-byte state span
    mov ax, [es:bx+8]               ; ...and the FIFTH word, which is the PS/2
    mov [sb_p2state], ax            ; mouse's 7 bytes (SPEC.md 9.4.4). The
                                    ; fourth is the cursor and is the HARNESS's
                                    ; (SPEC.md 9.4.3) - nothing here reads it

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
                                    ; - or 4, which is no serial port at all
                                    ; and means the PS/2 mouse won (SPEC 9.9.4)
    call sb_mb
    mov si, sb_l_mln                ; mou_line: the 8259 bit the winning packets
    mov al, 33                      ; actually arrived on. Read it TOGETHER with
    call sb_mbx                     ; the row above - 10 with row 2, or 08 with
                                    ; row 0, is a cross-wired card (SPEC 9.5.2.1),
                                    ; and FF is not a serial line at all
    mov si, sb_l_mrn0
    mov al, 0                       ; mou_run[0]: how far a LOSING port got
    call sb_mb
    mov si, sb_l_mrn1
    mov al, 2
    call sb_mb

    ; --- and the OTHER SOCKET (SPEC.md 9.9 / 9.4.4) --------------------------
    ; The step is the row that carries: a machine with no auxiliary port and a
    ; machine with one and nothing plugged into it are 2 and 4, not one absent
    ; mouse. On any 8088 this whole group is zeros and says so - kern_small
    ; carries the state and not the code, so the block is the same shape here.
    mov si, sb_l_p2st
    mov bx, [sb_p2state]
    mov al, [es:bx+1]               ; mou_p2st
    xor ah, ah
    call sb_num
    mov si, sb_l_p2on
    mov bx, [sb_p2state]
    mov al, [es:bx]                 ; mou_p2 - 0 again once it LOST the contest
    xor ah, ah
    call sb_num
    mov si, sb_l_p2id
    mov bx, [sb_p2state]
    mov al, [es:bx+2]               ; mou_p2id: 00 is a plain 3-byte mouse
    xor ah, ah
    call sb_hex
    mov si, sb_l_p2cb
    mov bx, [sb_p2state]
    mov al, [es:bx+6]               ; mou_p2cmd0: what this machine's own BIOS
    xor ah, ah                      ; had the 8042 set to, which is a fact no
    call sb_hex                     ; reading of the source can supply
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

; SPEC.md 39.12's context record, mirrored from kernel/vidsel.inc. Rule 3 of
; SPEC.md 57.3 as written: a block may change shape whenever its owner does,
; as long as the READERS change with it, and there are none outside this tree.
; The first three are inside the eighteen-word run - which is the live block's
; own order, so they are viddet.inc's layout twice removed - and the last
; three are the fields hung off the end of it.
VCTX_SEG    equ 0               ; vid_seg:    the framebuffer
VCTX_STRIDE equ 2               ; vid_stride: bytes from a row to the row one
                                ;             bank down
VCTX_BMASK  equ 4               ; vid_bmask:  y & this = the bank, so banks-1
VCTX_CW     equ 14              ; vid_cw / vid_ch: THIS DISPLAY's extent, not
VCTX_CH     equ 16              ; the desktop's (SPEC.md 39.2.1)
VCTX_VX     equ 36              ; ...and its origin in the virtual desktop
VCTX_VY     equ 38
VCTX_KIND   equ 40              ; ...and which adapter it is

; -----------------------------------------------------------------------------
; sb_video - what SPEC.md 39 arranged, and on which cards (39.19, 57.4's 'VD')
;
; A STATE DUMP, like sb_mouse and sb_ladder above and for exactly their
; reason: it is a question about hardware nobody in the container has. Two
; cards is the field machine's own arrangement (docs/FIELD-MACHINES.md), and
; the one thing no emulator can be asked is whether a monitor is plugged into
; the second one - which is the whole of why SPEC.md 39.19.1 makes Single the
; default and why these rows exist to be read off a real 5150.
;
; The rows to read TOGETHER, because each pair is a different fault:
;
;   avail vs displays   avail 6 with displays 1 is a machine that HAS two
;                       cards and is arranged Single - the default, not a
;                       failure. avail anything else with displays 2 is
;                       impossible and means vid_dual_ok has been widened.
;   desktop vs chrome   SPEC.md 39.16: the desktop is the UNION and the
;                       chrome is the PRIMARY's. Equal with displays 2 means
;                       vid_desk_union did not run; unequal with displays 1
;                       means it ran and nothing put it back.
;   origin vs size      the placement of SPEC.md 39.19.2. Display 0 is always
;                       (0,0); display 1 is (cw,0) for Right and (0,ch) for
;                       Below, and any other pair is a layout byte nothing
;                       honoured.
;   dead zone           the rows or columns the union covers and no display
;                       does (SPEC.md 39.15.3). Nonzero is normal - two cards
;                       of different sizes - and it is what ui_drag_dead and
;                       mou_clamp exist for, so a field report of a pointer
;                       vanishing is read against this number first.
; -----------------------------------------------------------------------------
sb_video:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call bl_blank
    mov si, sb_s_h_vid
    call bl_sline
    mov si, sb_s_h_vid2
    call bl_sline
    mov si, sb_s_h_vid3
    call bl_sline
    mov si, sb_s_h_vid4
    call bl_sline   ; ...and no bl_head, for sb_mouse's reason

    mov ax, DBG_TAG_VIDEO           ; SPEC.md 57's registry
    call bl_dbgfind
    jc .nodbg
    mov ax, [es:bx+2]
    mov [sb_vkind], ax              ; -> vid_kind, vid_mono, vid_planes
    mov ax, [es:bx+4]
    mov [sb_vavail], ax             ; -> vid_avail
    mov ax, [es:bx+6]
    mov [sb_vnd], ax                ; -> ndisp, cur, ox, oy, dmode, dlay
    mov ax, [es:bx+8]
    mov [sb_vcur], ax               ; -> cur_disp, cur_dprev
    mov ax, [es:bx+10]
    mov [sb_vctx], ax               ; -> the records...
    mov ax, [es:bx+12]
    mov [sb_vstr], ax               ; ...and their stride
    mov ax, [es:bx+14]
    mov [sb_vdesk], ax              ; -> vid_w, vid_h   (the DESKTOP, SPEC 39.16)
    mov ax, [es:bx+16]
    mov [sb_vchr], ax               ; -> vid_pw, vid_ph (the CHROME's extent)

    mov bx, [sb_vkind]              ; --- what the machine IS ----------------
    mov al, [es:bx]
    xor ah, ah
    mov si, sb_l_vkind
    call sb_num
    mov bx, [sb_vavail]
    mov al, [es:bx]
    xor ah, ah
    mov si, sb_l_vavail
    call sb_hex
    mov bx, [sb_vavail]             ; ...and HOW the mono card answered, which
    mov al, [es:bx+1]               ; is the row that turns "the Hercules is
    xor ah, ah                      ; not detected" into a diagnosis (SPEC.md
    mov si, sb_l_vhpr               ; 39.11.1.1). A 3 here is a fault that
    call sb_num                     ; section does not cover

    mov dx, 0x3BA                   ; ...and whether a mono card is DRIVING its
    call sb_porttog                 ; status port at all, which is the question
    mov si, sb_l_v3ba               ; a 3 above leaves open. FFFF = nothing
    call sb_hex                     ; there; anything else = a live 6845
    mov dx, 0x3DA                   ; ...and the colour side as the CONTROL, so
    call sb_porttog                 ; a machine that reports FFFF for both has
    mov si, sb_l_v3da               ; a broken test rather than two dead cards
    call sb_hex

    mov bx, [sb_vkind]              ; ...and the colour path, on the one
    cmp byte [es:bx], 0             ; adapter that has one (VID_VGA = 0)
    jne .novga
    call sb_vga_regs
.novga:

    cmp word [sb_vnd], 0            ; a kern_small kernel is single-display by
    je .small                       ; CONSTRUCTION and has no bytes to report

    mov bx, [sb_vnd]                ; --- ...and how it is arranged ----------
    mov al, [es:bx]
    xor ah, ah
    mov si, sb_l_vnd
    call sb_num
    mov bx, [sb_vnd]
    mov al, [es:bx+6]               ; vid_dmode
    xor ah, ah
    mov si, sb_l_vdm
    call sb_num
    mov bx, [sb_vnd]
    mov al, [es:bx+7]               ; vid_dlay
    xor ah, ah
    mov si, sb_l_vdl
    call sb_num
    mov bx, [sb_vcur]
    mov al, [es:bx]                 ; cur_disp
    xor ah, ah
    mov si, sb_l_vptr
    call sb_num

    mov bx, [sb_vdesk]              ; --- the desktop against the chrome -----
    mov ax, [es:bx]
    mov dx, [es:bx+2]
    mov si, sb_l_vdesk
    call sb_v2
    mov bx, [sb_vchr]
    mov ax, [es:bx]
    mov dx, [es:bx+2]
    mov si, sb_l_vchrm
    call sb_v2

    xor cl, cl                      ; --- one group per display --------------
.d:
    mov al, cl                      ; the record is BANKED and reloaded before
    call sb_vrec                    ; every read, never held in BX across a
    mov al, cl                      ; row: benchlib's line builders do not all
    xor ah, ah                      ; promise BX, and a pointer that survives
    mov si, sb_l_vd                 ; three of them and not the fourth reads
    call sb_num                     ; as one wrong number in a page of right
    mov bx, [sb_vrp]                ; ones
    mov al, [es:bx+VCTX_KIND]
    xor ah, ah
    mov si, sb_l_vdk
    call sb_num
    mov bx, [sb_vrp]
    mov ax, [es:bx+VCTX_VX]
    mov dx, [es:bx+VCTX_VY]
    mov si, sb_l_vdo
    call sb_v2
    mov bx, [sb_vrp]
    mov ax, [es:bx+VCTX_CW]
    mov dx, [es:bx+VCTX_CH]
    mov si, sb_l_vds
    call sb_v2
    mov bx, [sb_vrp]
    mov ax, [es:bx+VCTX_STRIDE]
    mov dx, [es:bx+VCTX_BMASK]
    inc dx                          ; the mask is banks-1 (0 on VGA, so 1)
    mov si, sb_l_vdb
    call sb_v2
    mov bx, [sb_vrp]
    mov ax, [es:bx+VCTX_SEG]
    mov si, sb_l_vdf
    call sb_hex
    inc cl
    mov bx, [sb_vnd]
    cmp cl, [es:bx]
    jb .d

    call sb_vdead                   ; --- ...and what no display covers ------
    jmp short .out
.small:
    mov si, sb_s_vsmall
    call bl_sline
    jmp short .out
.nodbg:
    mov si, sb_s_vnone
    call bl_sline
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_vrec - display AL's record, in BX and banked in [sb_vrp]. ES is
; KERNEL_SEG on entry. The stride comes out of the block rather than from a
; constant here, because SPEC.md 57.3 rule 3 lets the record change shape and
; a reader that assumed 42 would then walk into the middle of one.
sb_vrec:
    push ax
    push dx
    mul byte [sb_vstr]              ; mul r/m8: AX = AL * stride
    add ax, [sb_vctx]
    mov bx, ax
    mov [sb_vrp], ax
    pop dx
    pop ax
    ret

; sb_vdead - the union's area less every display's, in whole pixels
;
; It is an AREA and not a rectangle on purpose: with the two placements of
; SPEC.md 39.19.2 the displays never overlap, so the union's area minus the
; sum of theirs IS the gap - one subtraction instead of a rectangle walk, and
; it stays right if a third display is ever placed. Reported in units of 100
; pixels, because 1360 x 548 does not fit a word and this is a magnitude
; rather than a measurement.
sb_vdead:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [sb_vdesk]              ; DI:SI = the union's area
    mov ax, [es:bx]
    mov bx, [es:bx+2]
    mul bx
    mov si, ax
    mov di, dx
    xor cl, cl
.d:
    mov al, cl
    call sb_vrec
    mov ax, [es:bx+VCTX_CW]
    mov bx, [es:bx+VCTX_CH]
    mul bx
    sub si, ax
    sbb di, dx
    inc cl
    mov bx, [sb_vnd]
    cmp cl, [es:bx]
    jb .d
    mov ax, si                      ; ...as hundreds, so it fits the field
    mov dx, di
    mov cx, 100
    call bl_div32               ; ...and bl_div32 divides by CX
    mov si, sb_l_vdead
    call sb_num
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_v2 - SI = label, AX and DX = two numbers, one row. Preserves everything.
; sb_v3 - SI = label, AX / DX / CX = three values on one row. sb_v2's body
; with a third column at BL_C_N + 14, which still clears BL_C_US at 39.
; Written for the DAC triples (sb_vga_regs): an RGB entry printed as three
; separate rows is three times the report for one fact.
; =============================================================================
; sb_vga_regs - what the VGA's COLOUR PATH currently holds (SPEC.md 39.21)
;
; Reported from the field on a 5150 with a PVGA1A: after a few minutes the
; whole screen recolours - the desktop's black/white dither goes lavender, a
; red brick row goes purple, a white window frame goes cyan - with every
; shape, glyph and edge still exactly where it belongs.
;
; THAT SHAPE IS NOT DISPLAY RAM AND THIS BLOCK EXISTS TO PROVE IT EITHER WAY.
; In mode 12h a pixel is four plane bits -> one of 16 ATTRIBUTE palette
; registers -> one of 256 DAC entries -> three analog guns. A fault in the
; RAM moves PIXELS: speckle, dropped columns, sheared glyphs, garbage in
; patches. A fault anywhere in the three stages after it recolours a
; still-correct picture, uniformly, which is what was photographed.
;
; Two of those three stages can be READ BACK and the third cannot, and that
; is the whole diagnostic: run this with the screen right and again with it
; wrong. Identical numbers put the fault after the DAC - the analog output,
; the cable, the monitor - where no software can reach it. Different numbers
; put it in a register somebody wrote, and then it is ours.
;
; THE ATTRIBUTE READ BLANKS THE SCREEN and must be bracketed. Selecting a
; palette register at 3C0h needs bit 5 (PAS) CLEAR, which is the bit that
; turns video on; the write that restores it is not optional, and an
; interrupt landing between the index and the data write would leave the
; index half-selected - so the whole sequence runs with IF=0 and ends with
; PAS back up on every path. 3DAh is read first to put the port's own
; index/data flip-flop into a known state, which is the one thing about the
; attribute controller that cannot be assumed.
;
; VGA ONLY. On a CGA or a Hercules 3C0h/3C7h are nobody's, and reading them
; would print four rows of bus noise that look like measurements.
; =============================================================================
sb_vga_regs:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, sb_s_h_vga
    call bl_sline

    mov dx, 0x3C4                   ; --- SR01: the blank bit (SPEC.md 64) ---
    mov al, 1
    out dx, al
    inc dx
    in al, dx
    xor ah, ah
    mov bx, ax
    mov dx, 0x3CE                   ; --- GR06: which aperture this card
    mov al, 6                       ; decodes. 01 = A0000 alone, which is what
    out dx, al                      ; makes the B000 probe above mean anything
    inc dx                          ; (SPEC.md 39.11.1)
    in al, dx
    xor ah, ah
    mov cl, 8
    shl bx, cl
    or ax, bx
    mov si, sb_l_vsr
    call sb_hex

    xor al, al                      ; --- DAC 0, which IS the reported fault:
    call sb_dacrd                   ; a black that is not black recolours
    mov si, sb_l_vdac0              ; every dithered pixel on the screen
    call sb_v3
    mov al, 0x3F                    ; ...and WHITE, which is DAC entry 0x3F and
    call sb_dacrd                   ; not entry 15: the attribute palette maps
    mov si, sb_l_vdacf              ; colour 15 to 3Fh (measured - the sixteen
    call sb_v3                      ; registers read 00 01 02 03 04 05 14 07
                                    ; 38..3F), so entry 15 is a number no pixel
                                    ; on this screen goes through

    mov word [sb_vsum], 0           ; --- ...and all 768 bytes as one number,
    xor bl, bl                      ; because 256 entries is not a report and
.sum:                               ; a checksum is what a diff needs.
    mov al, bl                      ; THE COUNTER IS BL AND NOT DL: sb_dacrd
    call sb_dacrd                   ; ANSWERS in DX (green), so a counter kept
    add [sb_vsum], ax               ; there is overwritten by the value it just
    add [sb_vsum], dx               ; fetched - the loop then ends when green
    add [sb_vsum], cx               ; happens to be 0xFF, which is to say
    inc bl                          ; almost never. BL survives because
    jnz .sum                        ; sb_dacrd banks BX
    mov ax, [sb_vsum]
    mov si, sb_l_vdsum
    call sb_hex

    pushf                           ; --- the 16 attribute palette registers,
    cli                             ; behind the blank (see the header)
    mov word [sb_vsum], 0
    xor cx, cx
.pal:
    mov dx, 0x3DA                   ; THE FLIP-FLOP IS RESET EVERY ITERATION,
    in al, dx                       ; and this is the whole correctness of the
                                    ; loop. Only a WRITE to 3C0h toggles it;
                                    ; reading the data at 3C1h does not. So a
                                    ; reset taken once outside the loop leaves
                                    ; the port in DATA state after the first
                                    ; index write, and the SECOND iteration's
                                    ; `out 3C0` is then a DATA write into the
                                    ; register the first one selected - a
                                    ; diagnostic that scrambles the palette it
                                    ; was written to measure, which is exactly
                                    ; the fault under investigation
    mov dx, 0x3C0
    mov al, cl                      ; PAS clear: selecting a palette register
    out dx, al                      ; is what turns the screen off
    inc dx
    in al, dx
    xor ah, ah
    add [sb_vsum], ax
    mov bl, cl                      ; ...and BANK it: these sixteen bytes are
    xor bh, bh                      ; the DAC entries the screen actually goes
    mov [bx+sb_vpal], al            ; through, and the sum below is over THEM
    cmp cl, 0
    jne .pal2
    mov [sb_v3a], ax                ; ...and keep the first three, so a human
.pal2:                              ; can read the row as well as diff it
    cmp cl, 1
    jne .pal3
    mov [sb_v3b], ax
.pal3:
    cmp cl, 2
    jne .paln
    mov [sb_v3c], ax
.paln:
    inc cl
    cmp cl, 16
    jb .pal
    mov dx, 0x3DA                   ; ...and once more before the restore, for
    in al, dx                       ; the same reason: the last read left the
    mov dx, 0x3C0                   ; port in DATA state, so this would set
    mov al, 0x20                    ; palette register 15 to 0x20 instead of
    out dx, al                      ; turning the screen back on. PAS back up -
    popf                            ; and this write is not optional

    mov ax, [sb_v3a]
    mov dx, [sb_v3b]
    mov cx, [sb_v3c]
    mov si, sb_l_vpal
    call sb_v3
    mov ax, [sb_vsum]
    mov si, sb_l_vpsum
    call sb_hex

    call sb_shownsum
    mov ax, [sb_vsum]
    mov si, sb_l_vssum
    call sb_hex
    mov [sb_vshown], ax             ; ...and AGAIN, because a DAC read is not
    call sb_shownsum                ; obviously reliable on every card and this
    mov si, sb_l_vssum2             ; is the only way to find out: the IBM VGA
    call sb_hex                     ; spec says a palette access can collide
                                    ; with the display's own lookup, which is
                                    ; why software of the period programmed the
                                    ; DAC during retrace. TWO SUMS THAT DIFFER
                                    ; MEAN THE ROW ABOVE CANNOT BE TRUSTED on
                                    ; this card, and no amount of diffing two
                                    ; reports would have said so - the first
                                    ; field numbers did not correlate with what
                                    ; was on the screen, and this row is what
                                    ; that cost

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_shownsum - out AX = the sum of the sixteen DAC entries the SCREEN goes
;               through, from the attribute palette sb_vga_regs banked.
;
; Mode 12h displays 16 colours and the attribute palette says which DAC entries
; they are (00 01 02 03 04 05 14 07 38..3F on a standard table, which reads
; 05D3 on a healthy card - verified against a known-good VGA). A drift in the
; other 240 moves the all-256 sum and changes nothing anybody can see; this is
; how the two are told apart.
sb_shownsum:
    push bx
    push cx
    push dx
    mov word [sb_vsum], 0
    xor bl, bl
.s:
    push bx
    xor bh, bh
    mov al, [bx+sb_vpal]
    call sb_dacrd
    add [sb_vsum], ax
    add [sb_vsum], dx
    add [sb_vsum], cx
    pop bx
    inc bl
    cmp bl, 16
    jb .s
    mov ax, [sb_vsum]
    pop dx
    pop cx
    pop bx
    ret

; sb_porttog - DX = a status port; out AX = (every bit ever SET) << 8 | (every
;              bit ever CLEAR, as an AND). Reads only, 20,000 times - about
;              42 ms on a 4.77MHz 8088, which is two frames at 50 Hz and so
;              long enough for a vertical-sync bit to have moved.
;
; IT IS THE QUESTION TO ASK WHEN THE MEMORY PROBE SAYS NO (SPEC.md 39.11.1.1).
; A card that is present drives its status register and the retrace bits
; change as the beam scans; an empty ISA slot answers 0xFF to every read,
; forever, and OR == AND == FF says so in one number. That separates "there is
; no mono card in this machine" from "there is one and its RAM is not
; answering at B000", which no memory test can do and which a screenshot of a
; desktop with no Display page cannot even hint at.
sb_porttog:
    push bx
    push cx
    xor bh, bh                      ; BH accumulates the OR...
    mov bl, 0xFF                    ; ...BL the AND
    mov cx, 20000
.t:
    in al, dx
    or bh, al
    and bl, al
    loop .t
    mov ax, bx                      ; AH = OR (BH), AL = AND (BL) - which is
                                    ; already the order the label promises, and
                                    ; the xchg that used to be here reversed it:
                                    ; a field run read `88FF` for or/and, an
                                    ; impossible pair (AND cannot hold a bit OR
                                    ; does not), which is what caught it
    pop cx
    pop bx
    ret

; sb_dacrd - AL = a DAC entry; out AX/DX/CX = its red, green and blue (0..63)
; No blanking and no flip-flop: the DAC's read port is its own.
sb_dacrd:
    push bx
    mov dx, 0x3C7
    out dx, al
    mov dx, 0x3C9
    in al, dx
    mov bl, al
    in al, dx
    mov bh, al
    in al, dx
    mov cl, al
    xor ch, ch
    mov al, bh
    xor ah, ah
    mov dx, ax                      ; DX = green
    mov al, bl
    xor ah, ah                      ; AX = red, CX = blue
    pop bx
    ret

sb_v3:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [sb_v3a], ax                ; STAGED, not juggled: bl_dec wants AX, CX
    mov [sb_v3b], dx                ; and DX itself, and three values through
    mov [sb_v3c], cx                ; four registers is where a row prints one
    call bl_lclr                    ; of its neighbours' numbers
    xor di, di
    call bl_lput
    mov ax, [sb_v3a]
    xor dx, dx
    mov di, BL_C_N
    mov cx, 6
    call bl_dec
    mov ax, [sb_v3b]
    xor dx, dx
    mov di, BL_C_N + 7
    mov cx, 6
    call bl_dec
    mov ax, [sb_v3c]
    xor dx, dx
    mov di, BL_C_N + 14
    mov cx, 6
    call bl_dec
    call bl_lcommit
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sb_v2:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, dx                      ; bank the second before bl_dec wants DX
    call bl_lclr
    xor di, di
    call bl_lput
    mov di, BL_C_N
    xor dx, dx
    mov cx, 6
    call bl_dec
    mov ax, bx
    xor dx, dx
    mov di, BL_C_N + 7
    mov cx, 6
    call bl_dec
    call bl_lcommit
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
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
    call bl_sline   ; ...and no bl_head, for sb_mouse's reason

    mov ax, DBG_TAG_CLOCK           ; SPEC.md 57's registry
    call bl_dbgfind
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

; -----------------------------------------------------------------------------
; sb_fdd - is the second floppy drive really there? (SPEC.md 18.97/57.5)
;
; A STATE dump, like sb_mouse and sb_ladder above, and the one with the least
; ambiguous reason of the four: the kernel now REMOVES a volume on the
; strength of two status bytes read out of a uPD765, and an emulated FDC
; answers what its author believed a real one answers. So the raw bytes go in
; the report, and the field machine settles it.
;
; The field 5150 (docs/FIELD-MACHINES.md) is the case this was written for: it
; has ONE drive and DIP switches that claim two, so its expected rows are
; `claimed 2`, `probe stop 03` (Equipment Check) and `verdict 0` - and any
; other combination there is the news. A machine whose switches are right
; reads `claimed 1`, `probe ran 00` and nothing else, because there was
; nothing to contest.
;
; READ `probe stop` FIRST. `verdict 1` means "the drive was kept", and that is
; equally what a probe that PROVED it present and one that merely failed to
; prove it absent both say - which is the difference between this working and
; this being a fail-safe that never fires.
;
; ONE SUB-BLOCK PER UNIT (SPEC.md 18.98/57.5). The probe runs on units 1, 2
; and 3, and the published block used to be a single set of bytes that every
; run overwrote - so a machine with a 4865 on the 37-pin connector reported
; only the LAST unit asked, which is exactly the machine this diagnostic is
; for. `probe ran` is a BITMAP now (bit n = unit n was asked) and a unit is
; printed only if its bit is set, so a correctly-switched two-drive machine
; still reads five short rows and not twenty.
; -----------------------------------------------------------------------------
SB_FD_EQP   equ 0                   ; the block, as kernel/disk.inc lays it out
SB_FD_RAN   equ 1
SB_FD_U     equ 2                   ; ...then three rows, unit 1 first
SB_FDU_ST3  equ 0
SB_FDU_ST3B equ 1
SB_FDU_ST0  equ 2
SB_FDU_STEP equ 3
SB_FDU_VRD  equ 4
SB_FDU_SIZE equ 5

sb_fdd:
    push ax
    push bx
    push cx
    push si
    push es
    call bl_blank
    mov si, sb_s_h_fdd
    call bl_sline
    mov si, sb_s_h_fdd3
    call bl_sline   ; ...and no bl_head, for sb_mouse's reason
    mov si, sb_s_h_fdd7
    call bl_sline

    mov ax, DBG_TAG_FDD             ; SPEC.md 57's registry
    call bl_dbgfind
    jc .nodbg
    mov ax, [es:bx+2]
    mov [sb_fdstate], ax            ; -> the 17-byte state span

    mov si, sb_l_feqp               ; what int 11h claimed...
    mov al, SB_FD_EQP
    call sb_fdb

    ; ...and the WHOLE equipment word behind it, which on a 5150 is very
    ; nearly SW1 itself. The derived count above cannot say WHICH switch
    ; moved, and a field run that reads the expected number for an
    ; unexpected reason is exactly the one that costs a second trip: bits
    ; 7-6 are drives-1, 5-4 the display switches, 3-2 planar RAM, 1 the
    ; 8087 and 0 "there is a diskette drive at all". Read straight from the
    ; BIOS rather than from the kernel's banked byte, so the two disagreeing
    ; would itself be news; nothing in os8088 writes 0040:0010.
    mov si, sb_l_feqw
    int 0x11
    call sb_hex

    ; ...and SW1 ITSELF, read off the 8255 rather than out of the POST
    ; snapshot above. The two can DISAGREE, and one hex byte beside the other
    ; says which half is at fault: a machine whose equipment word reads the
    ; same drive count at two different switch positions is either not
    ; reading its switches or is having the word rewritten after POST by an
    ; option ROM, and nothing derived from int 11h can tell those apart.
    ; On a 5150 the equipment word's LOW BYTE is very nearly SW1 verbatim,
    ; so the comparison is direct.
    call sb_sw1
    jc .nosw1
    mov si, sb_l_fsw1
    call sb_hex
.nosw1:
    mov si, sb_l_fran               ; ...and which units were contested at all
    mov al, SB_FD_RAN
    call sb_fdbx

    mov cl, 1                       ; units 1..3; unit 0 has no row
.unit:
    mov bx, [sb_fdstate]
    mov al, [es:bx+SB_FD_RAN]
    mov ah, 1
    shl ah, cl
    test al, ah
    jz .nextu                       ; never asked: nothing here but initialisers

    mov al, cl                      ; the row's base offset in the block
    dec al
    mov ah, al
    shl al, 1
    shl al, 1
    add al, ah                      ; (unit - 1) * SB_FDU_SIZE...
    add al, SB_FD_U                 ; ...past the two scalars
    mov [sb_fdrow], al

    xor ah, ah                      ; a heading row whose VALUE is the unit,
    mov al, cl                      ; which needs no third copy of five labels
    mov si, sb_l_funit
    call sb_num

    mov si, sb_l_fst3               ; ...and the line, read twice
    mov al, SB_FDU_ST3
    call sb_fdux
    mov si, sb_l_fst3b
    mov al, SB_FDU_ST3B
    call sb_fdux
    mov si, sb_l_fst0
    mov al, SB_FDU_ST0
    call sb_fdux
    mov si, sb_l_fstep              ; THE row that carries
    mov al, SB_FDU_STEP
    call sb_fdux
    mov si, sb_l_fvrd
    mov al, SB_FDU_VRD
    call sb_fdu
.nextu:
    inc cl
    cmp cl, 4
    jb .unit
    jmp short .out
.nodbg:
    mov si, sb_s_fnone
    call bl_sline
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_build - one row naming the kernel this report came off (PERFORMANCE.md Part 8.1)
;
; The SUM of the three published section lengths. The block holds them
; separately - a debugger sees which section moved - but the report has room
; for one row, and a sum separates builds in practice: measured on the three
; disks that prompted this, .text alone was 56,576 / 56,798 / 56,776.
;
; A kernel too old to publish the block prints no row at all, rather than a
; zero somebody could read as a build.
; -----------------------------------------------------------------------------
sb_build:                       ; AX and SI are NOT banked: every row around
    push bx                     ; this one loads both fresh, and this package
    push es                     ; has five bytes of image left (see the align)
    mov ax, DBG_TAG_BUILD
    call bl_dbgfind
    jc .out
    mov bx, [es:bx+2]
    mov ax, [es:bx]
    add ax, [es:bx+2]
    add ax, [es:bx+4]
    mov si, sb_l_bld
    call sb_hex
.out:
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; sb_sw1 - the 5150's SW1 block, read straight off the 8255's port A
; out: CF=0 with AX = the byte; CF=1 = not an IBM PC and NOTHING was touched
;
; **IBM PC ONLY, and the gate is the model byte** at F000:FFFE - FF is the
; 5150, FE the 5160. It matters: on a 5150 port B bit 7 switches port A from
; the keyboard to the switch block, which is exactly what the ROM's own POST
; does; on a 5160 that same bit CLEARS the keyboard and SW1 lives on port C
; behind PB2 instead. Running this there would reset the keyboard and read a
; scan code as a switch setting.
;
; Port B carries the speaker gate, the two parity enables and the keyboard
; clock as well, so it is banked and put back BYTE FOR BYTE, and the window
; is a few microseconds with IF clear - a keystroke landing inside it would
; otherwise be decoded as switches.
; -----------------------------------------------------------------------------
sb_sw1:
    push es
    mov ax, 0xF000
    mov es, ax
    cmp byte [es:0xFFFE], 0xFF
    jne .no
    pushf
    cli
    in  al, 0x61                ; ports 60h/61h take the IMMEDIATE form, which
    mov ah, al                  ; is also what spares this routine a register:
    or  al, 0x80                ; port B is banked in AH and nothing else is
    out 0x61, al                ; touched
    in  al, 0x60                ; ...the switches
    xchg al, ah                 ; AL = port B again, AH = SW1
    out 0x61, al                ; and back, before anything else can run
    popf
    mov al, ah
    xor ah, ah
    clc
    jmp short .out
.no:
    stc
.out:                           ; a pop cannot disturb CF
    pop es
    ret

; sb_fdu / sb_fdux - SI = label, AL = a field offset within the unit row
; [sb_fdrow] names -> one row, as decimal / as hex.
sb_fdu:
    add al, [sb_fdrow]
    jmp short sb_fdb
sb_fdux:
    add al, [sb_fdrow]
    jmp short sb_fdbx

; sb_fdb / sb_fdbx - SI = label, AL = byte offset into the floppy block -> one row
; as decimal / as hex. ES is KERNEL_SEG on entry (sb_fdd holds it).
sb_fdb:
    push bx
    mov bl, al
    xor bh, bh
    add bx, [sb_fdstate]
    mov al, [es:bx]
    xor ah, ah
    call sb_num
    pop bx
    ret

sb_fdbx:
    push bx
    mov bl, al
    xor bh, bh
    add bx, [sb_fdstate]
    mov al, [es:bx]
    xor ah, ah
    call sb_hex
    pop bx
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
    push ax

    ; How close this report came to its own ceilings. `bl_full` already says a
    ; report TRUNCATED; nothing said how near one that did not had come, which
    ; is why the row ceiling was raised three times after the fact and the
    ; arena ceiling never was. Both are printed now.
    call bl_blank
    mov si, sb_s_h_cap
    call bl_sline
    mov si, sb_l_caprow
    mov ax, [bl_nrow]
    call sb_num
    mov si, sb_l_caprmx
    mov ax, BL_MAXROWS
    call sb_num
    mov si, sb_l_capuse
    mov ax, [bl_used]
    call sb_num
    mov si, sb_l_capmax
    mov ax, BL_ARENA
    call sb_num

    pop ax
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
    call bl_dbgfind
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

    call sb_seek                    ; ...what a head STEP costs
    call sb_motor                   ; ...and what SPIN-UP costs
    call sb_dbgctr                  ; ...and what os8088's own path issued
    call sb_find                    ; ...and what a FIND cursor would win
    call sb_rah                     ; ...and how many chunks the cache holds
    call sb_write                   ; ...and what a LARGE WRITE costs

.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_write - what a LARGE WRITE costs (SPEC.md 18.4)
;
; Every disk figure in PERFORMANCE.md Part 9 is a READ. The write path has
; never been measured on the machine it is for, and it is a different shape:
; SPEC.md 18.4's commit order is allocate + write the data, flush the FAT,
; write the directory entry, then free any replaced chain and flush again - so
; a write carries metadata a read does not, and the interesting number is how
; much of the cost that metadata is.
;
; IT WRITES WHERE SYSBENCH IS STANDING (SPEC.md 19.2), and that is the whole
; interface: run it from A: to measure a floppy and from C: to measure a hard
; disk. The row says which volume it used. Nothing here takes an option.
;
; Four operations, each answering what the others cannot:
;   one cluster   the FIXED cost - a create, a FAT flush and a directory
;                 commit with almost no data underneath them
;   create NKB    the same thing with a real payload: the slope
;   replace NKB   the same name again, which ALSO frees the chain it replaces
;                 and flushes the FAT a second time (SPEC.md 18.4)
;   append NKB    in SB_WR_CHUNK pieces - the copy engine's own shape
;                 (SPEC.md 22.5), so the row is comparable to a copy
;
; It refuses rather than damages. The volume's free space is read FIRST and
; the size steps down by halves until it fits with SB_WR_SLACK to spare; a
; write-protected disk reports its FERR_* and the block stops instead of
; timing nine refusals; and the scratch file is deleted whatever happened.
; -----------------------------------------------------------------------------
sb_write:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call bl_blank
    mov si, sb_s_h_wr
    call bl_sline

    call OSAPI_FILE_HERE            ; SAY where this landed: the whole point is
    mov al, bl                      ; that it is wherever you started sysbench
    add al, 'A'
    mov si, sb_l_wvol
    call sb_wchar

    call OSAPI_FILE_DFREE           ; DX:AX = free bytes; KB is all we need
    mov cl, 10
    call sb_shr32
    mov [sb_wfree], ax
    mov si, sb_l_wfree
    call sb_num

    mov ax, SB_WR_KB                ; the largest size that fits the VOLUME and
.fit:                               ; the HEAP both
    mov bx, ax
    add bx, SB_WR_SLACK
    cmp bx, [sb_wfree]
    ja .smaller
    push ax
    call OSAPI_MEM_CLAIM
    jc .nomem
    pop ax
    mov [sb_wkb], ax
    mov [sb_wseg], dx
    jmp short .have
.nomem:
    pop ax
.smaller:
    shr ax, 1
    cmp ax, SB_WR_MIN
    jae .fit
    mov si, sb_s_wnone
    call bl_sline
    jmp .out
.have:
    mov si, sb_l_wkb
    mov ax, [sb_wkb]
    call sb_num

    mov es, [sb_wseg]               ; something to write, one word per KB so a
    mov cx, [sb_wkb]                ; future check can verify it the way
    xor ax, ax                      ; sb_verify verifies a read
.fill:
    push cx
    xor di, di                      ; ...a KB at a time with ES walking, because
    mov cx, 512                     ; DI alone wraps at 64KB and this is 128
    cld
    rep stosw
    mov dx, es
    add dx, 64                      ; 1KB = 64 paragraphs
    mov es, dx
    pop cx
    inc ax
    loop .fill

    call bl_head
    mov word [bl_n], 1              ; --- the FIXED cost: one cluster
    mov word [bl_body], sb_b_wrsml
    mov si, sb_r_wsml
    mov al, 1
    call bl_run
    cmp word [sb_werr], 0
    je .ok1
    mov si, sb_l_werr               ; write-protected, or full: say which and
    mov ax, [sb_werr]               ; stop rather than time nine refusals
    call sb_num
    jmp .free
.ok1:
    call sb_wr_del                  ; --- the LARGE create, and it has to be a
                                    ; CREATE: the row above just made this file,
                                    ; so without the delete this measured a
                                    ; REPLACE and the two rows differed only in
                                    ; size. It cost a wrong reading of the
                                    ; trace - five single-sector commit calls
                                    ; (FAT1, FAT2, dir, FAT1, FAT2) reported as
                                    ; "a create flushes the FAT twice", when
                                    ; dskw_write step 4 is gated on
                                    ; [dskw_oldclus] and a true create has
                                    ; always skipped it (SPEC.md 18.4)
    call sb_ctr_bank
    mov word [bl_n], 1
    mov word [bl_body], sb_b_wrbig
    mov si, sb_r_wbig
    mov al, 1
    call bl_run
    call sb_ctr_take
    call sb_ctr_show
    call sb_ctr_trace               ; ...and every call it made, in order

    mov ax, [sb_wkb]                ; --- the same name AGAIN. IT NEEDS TWICE
    add ax, ax                      ; THE SIZE FREE: SPEC.md 18.4 allocates and
    add ax, SB_WR_SLACK             ; writes the new chain BEFORE it frees the
    cmp ax, [sb_wfree]              ; one it replaces, so both exist at once -
    ja .norep                       ; and without this test the row ran out of
    call sb_ctr_bank                ; space part-way and reported a plausible
    mov word [bl_n], 1              ; 112 sectors in 14 calls, which is a
    mov word [bl_body], sb_b_wrbig  ; FAILED write that reads like a fast one
    mov si, sb_r_wrep
    mov al, 1
    call bl_run
    call sb_ctr_take
    call sb_ctr_show
    cmp word [sb_werr], 0
    je .rep_ok
    mov si, sb_l_werr
    mov ax, [sb_werr]
    call sb_num
    jmp short .rep_ok
.norep:
    mov si, sb_s_wnorep
    call bl_sline
.rep_ok:

    call sb_wr_del                  ; --- and the copy engine's shape
    call sb_ctr_bank
    mov word [bl_n], 1
    mov word [bl_body], sb_b_wrapp
    mov si, sb_r_wapp
    mov al, 1
    call bl_run
    call sb_ctr_take
    call sb_ctr_show
    mov si, sb_l_wchunk
    mov ax, SB_WR_CHUNK
    call sb_num
    cmp word [sb_werr], 0
    je .free
    mov si, sb_l_werr
    mov ax, [sb_werr]
    call sb_num
.free:
    call sb_wr_del                  ; the volume goes back the way it was
    mov dx, [sb_wseg]
    call OSAPI_MEM_FREE
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_wbytes - DX:CX = [sb_wkb] KB in bytes. Clobbers AX, CX, DX, flags.
sb_wbytes:
    mov ax, [sb_wkb]
    mov dx, ax
    mov cl, 10
    shl ax, cl                      ; low word = KB << 10
    mov cl, 6
    shr dx, cl                      ; high word = KB >> 6, so 128KB fits
    mov cx, ax
    ret

; sb_b_wrsml - one cluster, so the row is the COMMIT and nothing else
sb_b_wrsml:
    push es
    mov es, [sb_wseg]
    xor bx, bx
    mov si, sb_f_wr
    mov cx, 512
    xor dx, dx
    call OSAPI_FILE_WRITE
    jc .err
    mov word [sb_werr], 0
    pop es
    ret
.err:
    mov [sb_werr], ax
    pop es
    ret

; sb_b_wrbig - [sb_wkb] KB in one call: a create, or a replace if it is there
sb_b_wrbig:
    push es
    mov es, [sb_wseg]
    xor bx, bx
    call sb_wbytes
    mov si, sb_f_wr
    call OSAPI_FILE_WRITE
    jc .err
    mov word [sb_werr], 0
    pop es
    ret
.err:
    mov [sb_werr], ax
    pop es
    ret

; sb_b_wrapp - the same bytes as SB_WR_CHUNK-KB pieces: create, then append
sb_b_wrapp:
    push es
    mov es, [sb_wseg]
    xor bx, bx
    mov si, sb_f_wr
    mov cx, SB_WR_CHUNK * 1024
    xor dx, dx
    call OSAPI_FILE_WRITE           ; the first chunk MAKES the file: append
    jc .err                         ; needs one that already exists
    mov ax, SB_WR_CHUNK
    mov [sb_wdone], ax
.more:
    mov ax, [sb_wdone]
    cmp ax, [sb_wkb]
    jae .done
    mov dx, [sb_wseg]               ; ...from where we got to in the buffer,
    mov bx, ax                      ; by SEGMENT, so nothing wraps
    mov cl, 6
    shl bx, cl                      ; KB -> paragraphs
    add dx, bx
    mov es, dx
    xor bx, bx
    mov si, sb_f_wr
    mov cx, SB_WR_CHUNK * 1024
    call OSAPI_FILE_APPEND
    jc .err
    add word [sb_wdone], SB_WR_CHUNK
    jmp short .more
.done:
    mov word [sb_werr], 0
    pop es
    ret
.err:
    mov [sb_werr], ax
    pop es
    ret

; sb_wr_del - take the scratch file away. Preserves every register.
sb_wr_del:
    push ax
    push si
    mov si, sb_f_wr
    call OSAPI_FILE_DELETE
    pop si
    pop ax
    ret

; sb_shr32 - DX:AX >>= CL, saturated into AX. Clobbers AX, DX, CL, flags.
sb_shr32:
.step:
    shr dx, 1
    rcr ax, 1
    dec cl
    jnz .step
    or dx, dx
    jz .fits
    mov ax, 0xFFFF                  ; a volume too big for a word of KB says
.fits:                              ; the largest word, rather than wrapping to
    ret                             ; a small number that reads as plausible

; sb_wchar - SI = label, AL = one character in the value column
sb_wchar:
    push ax
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov [bl_lscr + BL_C_N + 4], al
    call bl_lcommit
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_rah - SPEC.md 18.95.4: how many chunks does the sector cache actually hold
;
; Standalone, and it MEASURES `DSK_RAH_RUNS` rather than being told it: a
; package cannot read a kernel constant, and one that could would be checking
; the constant against itself. So it builds a working set of W chunks, reads
; it again, and asks the counters whether that cost any int 13h at all.
;
; The staircase is the evidence. Round-robin eviction and a re-read in the
; same order is Belady's worst case, so at W <= RUNS the second pass is FREE
; and one chunk past it EVERY read misses - not a slope, a cliff, and the
; cliff's position is the run count.
;
; Two things make the working set real. The stride is at least two TRACKS, so
; consecutive reads can never share a chunk however a fill happened to be
; bounded; and it walks ONE 170KB file (`bigfile.dat`, which the field disks
; already carry), so the chunks are its own data and not somebody's directory.
; The cluster size is PROBED rather than assumed - OSAPI_FILE_READ_AT refuses
; a capacity that is not a cluster multiple, and that refusal costs no I/O.
; -----------------------------------------------------------------------------
sb_rah:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call bl_blank
    mov si, sb_s_h_rah
    call bl_sline
    cmp word [sb_dbgblk], 0         ; the whole answer is a call count
    je .nodbg

    mov ax, SB_RAH_KB               ; a buffer of our own: this block is
    call OSAPI_MEM_CLAIM            ; standalone and the file rows' claim is
    jc .noclaim                     ; long gone by here
    mov [sb_rseg], dx

    call sb_rah_cluster             ; ...the volume's cluster size, by refusal
    jc .nofile
    mov si, sb_l_rcl
    mov ax, [sb_rcl]
    call sb_num
    mov si, sb_l_rstr
    mov ax, [sb_rstr]
    call sb_num

    mov byte [sb_rshort], 0
    mov word [sb_rw], SB_RAH_WSTEP
.sweep:
    call sb_rah_pass                ; populate W chunks...
    jc .stop
    call sb_ctr_bank
    call sb_rah_pass                ; ...and ask for them again
    jc .stop
    call sb_ctr_take
    mov si, sb_l_rw
    mov ax, [sb_rw]
    mov dx, [sb_c0i13]
    call sb_rah_row
    cmp word [sb_c0i13], 0
    jne .cliff
    mov ax, [sb_rw]
    mov [sb_rlast], ax              ; the largest working set that was free
    add ax, SB_RAH_WSTEP
    mov [sb_rw], ax
    cmp ax, SB_RAH_WMAX
    jbe .sweep
    jmp short .say
.cliff:
    mov ax, [sb_rw]
    mov [sb_rfirst], ax
.say:
    mov si, sb_l_rhold
    mov ax, [sb_rlast]
    call sb_num
    mov si, sb_l_rmiss
    mov ax, [sb_rfirst]
    call sb_num
    jmp short .free
.nofile:
    mov si, sb_s_rnofile
    call bl_sline
.free:
    mov ax, [sb_rseg]
    mov dx, ax
    call OSAPI_MEM_FREE
    jmp short .out
.stop:
    mov si, sb_s_rerr
    cmp byte [sb_rshort], 0
    je .stopsay
    mov si, sb_s_rshort
    call bl_sline
    mov si, sb_s_rshort2
.stopsay:
    call bl_sline
    jmp short .free
.noclaim:
    mov si, sb_s_rnoclaim
    call bl_sline
    jmp short .out
.nodbg:
    mov si, sb_s_nodbg
    call bl_sline
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_rah_cluster - the volume's cluster size into [sb_rcl], and the stride it
; implies into [sb_rstr]. CF=1 = the file is not here at all.
;
; OSAPI_FILE_READ_AT answers FERR_NAME for a capacity that is not a cluster
; multiple and does no I/O to find out, so doubling from 512 until one is
; accepted is a free probe - and the first ACCEPTED size is the cluster,
; because every larger multiple would be accepted too.
sb_rah_cluster:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov word [sb_rcl], 512
.try:
    mov es, [sb_rseg]
    xor bx, bx
    mov cx, [sb_rcl]
    mov si, sb_f_bigger
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_READ_AT
    jnc .got
    cmp ax, FERR_NAME               ; ...only a NAME refusal is the alignment
    jne .none                       ; one; anything else is a missing file
    shl word [sb_rcl], 1
    cmp word [sb_rcl], SB_RAH_KB * 1024
    jbe .try
.none:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.got:
    mov ax, [sb_rcl]                ; the stride: whole clusters, at least two
.round:                             ; TRACKS, so no two reads can share a chunk
    cmp ax, SB_RAH_GAP
    jae .have
    add ax, [sb_rcl]
    jmp short .round
.have:
    mov [sb_rstr], ax
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; sb_rah_pass - read one cluster at each of [sb_rw] strided offsets
sb_rah_pass:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor di, di
.next:
    mov ax, di
    mul word [sb_rstr]              ; DX:AX = the offset, 32 bits
    mov es, [sb_rseg]
    xor bx, bx
    mov cx, [sb_rcl]
    mov si, sb_f_bigger
    call OSAPI_FILE_READ_AT
    jc .fail
    or ax, ax                       ; 0 bytes = past the end: the file is too
    jnz .on                         ; short for a working set this wide, and
    or dx, dx                       ; pretending otherwise reports a cliff
    jz .short                       ; that is the FILE's and not the cache's
.on:
    inc di
    cmp di, [sb_rw]
    jb .next
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.short:
    mov byte [sb_rshort], 1         ; ...which is a different sentence from a
.fail:                              ; read that refused, and the two used to
    pop es                          ; print the same one
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; sb_rah_row - SI = label, AX = the working set, DX = the calls it cost
sb_rah_row:
    push ax
    push cx
    push dx
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    push dx
    xor dx, dx
    mov di, BL_C_N
    mov cx, 5
    call bl_dec                     ; W in the iterations column, so it reads
    pop ax                          ; down the page beside every other row
    xor dx, dx
    mov di, BL_C_CNT
    mov cx, 9
    call bl_dec
    call bl_lcommit
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_find - SPEC.md 18.95.2: what a resumable FILE_FIND cursor would be worth
;
; Standalone, and it does its own disk work: nothing above it has to have run
; and nothing below it depends on what it leaves. It enumerates whichever
; directory sysbench was launched from, so RUN IT SOMEWHERE WITH ENTRIES IN IT
; - N is reported, and every derived figure scales by the slope rather than by
; N, so a six-file floppy root still answers the question.
;
; The question is not "is enumeration slow". §18.95's cache already took the
; revolutions out of it - the int 13h row below is normally 0, which is the
; whole of §18.95.2's argument in one number - so what is left is CPU, and a
; CALL COUNT CANNOT SEE IT. This times it instead.
;
; FILE_FIND is BY ORDINAL and keeps no cursor (SPEC.md 19.7.1), so returning
; entry k means walking past the k before it: cost(k) = a + b*k, where a is
; the fixed cost plus one entry and b is one entry skipped. Two rows at the
; two ends of the SAME directory give both, so the answer extrapolates to a
; directory of any size instead of being a fact about this floppy:
;
;   today, N entries      = the measured whole walk
;   a perfect cursor      = N * a          - every call as cheap as the first
;   what it would win     = the difference
;
; N * a is the tightest bound the measured quantities support, and it is
; OPTIMISTIC on purpose: a real cursor still validates its stamp, so anything
; it wins is less than this. A bound that flatters the change is the honest
; direction to err in when the conclusion is "do not build it".
; -----------------------------------------------------------------------------
sb_find:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call bl_blank
    mov si, sb_s_h_fnd
    call bl_sline

    call sb_fcount                  ; how many entries are there to walk?
    mov si, sb_l_fn
    mov ax, [sb_fn]
    call sb_num
    cmp word [sb_fordl], 1          ; one entry has no slope and no walk to
    jb .few                         ; save: the whole block is meaningless

    call bl_head
    mov word [sb_fbord], 0          ; --- the FIRST ordinal: a, the fixed cost
    mov word [bl_n], SB_FIND_N      ; plus exactly one entry classified
    mov word [bl_body], sb_b_find
    mov si, sb_r_f0
    xor al, al
    call bl_run
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    mov [sb_fa], ax
    mov [sb_fa+2], dx

    mov ax, [sb_fordl]              ; --- ...and the LAST: a + b * that ordinal
    mov [sb_fbord], ax
    mov word [bl_n], SB_FIND_N
    mov word [bl_body], sb_b_find
    mov si, sb_r_fl
    xor al, al
    call bl_run
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    mov [sb_fb], ax
    mov [sb_fb+2], dx

    call sb_ctr_bank                ; --- and one whole enumeration, timed, so
    mov word [bl_n], SB_FIND_W      ; the model has something to be checked
    mov word [bl_body], sb_b_fwalk  ; against rather than being the answer
    mov si, sb_r_fw
    xor al, al
    call bl_run
    call sb_ctr_take
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    mov [sb_fw], ax
    mov [sb_fw+2], dx

    mov ax, [sb_fb]                 ; --- the slope: (last - first) / ordinal
    mov dx, [sb_fb+2]
    sub ax, [sb_fa]
    sbb dx, [sb_fa+2]
    jnc .slope
    xor ax, ax                      ; the last call measured faster than the
    xor dx, dx                      ; first: noise, and a negative slope would
.slope:                             ; make every row below it nonsense
    mov cx, [sb_fordl]
    call bl_div32
    mov [sb_fslope], ax
    mov [sb_fslope+2], dx
    mov si, sb_l_fsl
    call sb_us

    mov ax, [sb_fa]                 ; --- a perfect cursor: N calls, each as
    mov dx, [sb_fa+2]               ; cheap as the one that skips nothing
    mov cx, [sb_fn]
    call bl_mul48
    call bl_get32
    push ax
    push dx
    mov si, sb_l_fpc
    call sb_us
    pop dx
    pop ax
    mov bx, ax                      ; --- ...against what the walk really cost
    mov cx, dx
    mov ax, [sb_fw]
    mov dx, [sb_fw+2]
    mov si, sb_l_fwm
    call sb_us
    sub ax, bx
    sbb dx, cx
    jnc .win
    xor ax, ax                      ; a cursor cannot win less than nothing,
    xor dx, dx                      ; and on a two-entry directory it rounds
.win:                               ; there honestly
    mov si, sb_l_fwin
    call sb_us

    mov ax, [sb_fslope]             ; --- ...and the same at 32 entries, which
    mov dx, [sb_fslope+2]           ; is the size the operations this question
    mov cx, SB_FIND_X               ; came from actually walk. b * M*(M-1)/2 is
    call bl_mul48                   ; the whole quadratic term
    call bl_get32
    mov si, sb_l_fx
    call sb_us

    cmp word [sb_dbgblk], 0         ; --- and the row that decides it: with
    je .nodbg                       ; SPEC.md 18.95's cache the walk issues no
    mov si, sb_l_fi13               ; int 13h at all, so there are no
    mov ax, [sb_c0i13]              ; revolutions here for a cursor to save and
    call sb_num                     ; everything above is CPU
    mov si, sb_l_fsec
    mov ax, [sb_c0sec]
    call sb_num
.nodbg:
    jmp short .out
.few:
    mov si, sb_s_fnfew
    call bl_sline
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_fcount - walk the whole directory once: [sb_fn] entries, [sb_fordl] the
; last ordinal that answered. Preserves every register.
sb_fcount:
    push ax
    push cx
    push di
    push es
    push cs
    pop es                          ; the buffer is ours (an X cell, so ES:DI
    xor cx, cx                      ; is the CALLER's - api_file_find does not
    mov word [sb_fn], 0             ; set it)
    mov word [sb_fordl], 0
.next:
    mov [sb_fask], cx               ; ...in memory, because dsk_find is under
    mov di, sb_fent                 ; no obligation to preserve a register
    call OSAPI_FILE_FIND
    jc .done
    mov ax, [sb_fask]
    mov [sb_fordl], ax
    inc word [sb_fn]
    cmp word [sb_fn], SB_FIND_MAX   ; a directory that never ends is a corrupt
    jb .next                        ; one, and this must not spin on it
.done:
    pop es
    pop di
    pop cx
    pop ax
    ret

; sb_b_find - one FILE_FIND at [sb_fbord]. The timed body.
sb_b_find:
    push es
    push cs
    pop es
    mov di, sb_fent
    mov cx, [sb_fbord]
    call OSAPI_FILE_FIND
    pop es
    ret

; sb_b_fwalk - ...and one whole enumeration, the way a caller writes it
sb_b_fwalk:
    push es
    push cs
    pop es
    xor cx, cx
.next:
    mov di, sb_fent                 ; reloaded per call: see sb_fcount
    call OSAPI_FILE_FIND
    jnc .next
    pop es
    ret

; sb_us - SI = label, DX:AX = hundredths of a microsecond: one report line in
; the same column as every timed row above. Preserves every register.
sb_us:
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov di, BL_C_US
    call bl_usfield
    push si
    mov si, bl_s_us
    mov di, BL_C_UNIT
    call bl_lput
    pop si
    call bl_lcommit
    pop di
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

    call sb_ctr_bank                ; --- one 16KB read
    call sb_b_rdbig
    call sb_ctr_take
    mov si, sb_l_c16
    call bl_sline
    call sb_ctr_show
    call sb_ctr_trace               ; ...and every call it made, in order

    call sb_ctr_bank                ; --- the SAME 16KB read again. SPEC.md
    call sb_b_rdbig                 ; 18.95.1's gate says a streaming read is
    call sb_ctr_take                ; never cached, so this row must look like
    mov si, sb_l_c16b               ; the one above it: a cache that swallowed
    call bl_sline                   ; 16KB would show here as a collapse, and
    call sb_ctr_show                ; would have evicted everything else

    call sb_ctr_bank                ; --- ...and one ONE-SECTOR read, which is
    call sb_b_rdsml                 ; the same overhead with no data behind it
    call sb_ctr_take
    mov si, sb_l_c1
    call bl_sline
    call sb_ctr_show

    call sb_ctr_bank                ; --- the same one AGAIN: its directory
    call sb_b_rdsml                 ; sector and its own track are cached now
    call sb_ctr_take                ; (SPEC.md 18.95), so this is the cache's
    mov si, sb_l_c1b                ; hit rate stated in the only unit that
    call bl_sline                   ; matters. On a kernel without it the two
    call sb_ctr_show                ; one-sector rows are identical

    call sb_b_rdbig                 ; --- ...and now stream 16KB PAST it and
    call sb_ctr_bank                ; ask a third time. This is the pollution
    call sb_b_rdsml                 ; test: with 18.95.1's gate the stream
    call sb_ctr_take                ; cached nothing and this row still matches
    mov si, sb_l_c1c                ; the one above; without it the stream
    call bl_sline                   ; evicted the lot and this matches the COLD
    call sb_ctr_show                ; row instead

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
    mov ax, [es:bx+2]               ; ...and the run it asked for, which is AL
    mov dx, ax                      ; ALONE: SPEC.md 18.94.3 packs the volume
    xor ah, ah                      ; into AH and the WRITE flag into its top
    push dx                         ; bit, and this printed the whole word. A
    xor dx, dx                      ; read on volume 0 is 0x0009 and looks
    mov di, [sb_tcol]               ; right; a write on volume 1 is 0x8109 and
    add di, 6                       ; renders as 33033 in a 2-wide field, which
    mov cx, 2                       ; reads as a run of 33 on a 9-sector track.
    call bl_dec                     ; Invisible until something traced a WRITE
    pop dx
    test dh, 0x80                   ; ...and MARK it, because a trace that
    jz .rd                          ; cannot tell a read from a write cannot be
    mov di, [sb_tcol]               ; read at all
    add di, 8
    add di, bl_lscr
    mov byte [di], 'w'
.rd:
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

; -----------------------------------------------------------------------------
; sb_r13at - one raw single-sector read at a GIVEN cylinder
; in:  CH = cylinder. Sector 1, head 0, drive 0, through the kernel's entry.
;
; sb_r13go above always reads SB_R13_CYL, because the rows it serves are about
; what a track costs once the head is on it. The seek rows below are about
; getting there, so they need to name the cylinder.
; -----------------------------------------------------------------------------
sb_r13at:
    push ax
    push es
    push bx
    push cx
    push dx
    mov es, [sb_bseg2]
    mov bx, [sb_r13off]
    mov al, 1                       ; one sector...
    mov cl, 1                       ; ...the first one on the track
    xor dx, dx                      ; head 0, drive 0 (A:)
    call far [sb_r13ent]            ; KERNEL_SEG:dsk_dbg_raw - holds sch_lock
    mov al, ah
    xor ah, ah
    mov [sb_st13], ax
    pop dx
    pop cx
    pop bx
    pop es
    pop ax
    ret

; sb_b_seek - ONE OP IS A PAIR: cylinder 0, then [sb_skcyl]. So an op contains
; two seeks of that distance, and the row's us/op is twice one of them plus
; twice whatever rotational wait follows.
sb_b_seek:
    xor ch, ch
    call sb_r13at
    mov ch, [sb_skcyl]
    call sb_r13at
    ret

; -----------------------------------------------------------------------------
; sb_seek - what a HEAD STEP costs (PERFORMANCE.md Part 9 Set 35)
;
; This block exists because the floppy timing model in
; tools/martypc/patches/03-floppy-disk-timing.patch has exactly one number in
; it that no measurement anywhere pins: the seek. Every raw row above reads a
; single track and never moves the head, so the step rate there is the BIOS's
; own SPECIFY request (the DPT byte printed above) and the settle is the DPT's
; - both taken on trust. This says what the DRIVE actually does with them.
;
; **READ THE ROWS AS REVOLUTIONS, AND EXPECT THEM TO BE WHOLE ONES.** A read
; ends at a fixed angular position, so the next read of sector 1 waits for
; sector 1 to come round again: the seek happens INSIDE that wait and is
; invisible until it is longer than the wait. Every row is therefore quantized
; to whole revolutions, and what the block measures is the DISTANCE AT WHICH
; THE COST STEPS UP, not a smooth slope:
;
;   all rows == the baseline    every seek fits inside one revolution, so the
;                               step rate is bounded ABOVE by (rev / cyls)
;   a row costs one rev more    that seek does NOT fit; the step rate is
;                               bounded BELOW between it and the row under it
;
; On the calibration 5150 (200 ms a turn) an 8 ms/cylinder step predicts the
; break between 20 cylinders (160 ms + settle) and 39 (312 ms + settle), so a
; report where 1/5/10/20 match the baseline and 39 does not is the model being
; right. A break lower than that says the drive steps SLOWER than the BIOS
; asked for, and a report with no break at all says it steps faster than
; 200 ms / 39 = 5.1 ms and the model is charging too much.
;
; The baseline row reads cylinder 0 TWICE - same work, same rotational wait,
; no head movement - so it is the zero of the scale and not a separate thing
; the reader has to trust.
;
; It never writes, reads only cylinders the booted disk already holds, and goes
; through the kernel's entry like every raw row here (docs/FIELD-NOTES.md 10).
; -----------------------------------------------------------------------------
SB_SK_ROWS  equ 5

sb_seek:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    cmp word [sb_dbgblk], 0         ; the raw entry is the debug block's; with
    je .out                         ; no block there is nothing to call

    call bl_blank
    mov si, sb_s_h_sk
    call bl_sline
    call bl_head

    xor ch, ch                      ; warm the motor and park the head at 0, so
    call sb_r13at                   ; the baseline row below is a pure pair of
                                    ; rotations with no seek in it at all

    mov word [sb_skcyl], 0          ; --- the zero of the scale ---
    mov word [bl_n], 4
    mov word [bl_body], sb_b_seek
    mov si, sb_r_sk0
    mov al, 1
    call bl_run

    mov di, sb_sktab                ; --- and the distances ---
    mov cx, SB_SK_ROWS
.row:
    push cx
    mov ax, [di]
    mov [sb_skcyl], ax

    mov ch, [sb_skcyl]              ; PARK THE HEAD AT THE FAR CYLINDER FIRST.
    call sb_r13at                   ; An op is (read 0, read N) = two seeks of
                                    ; N - but only if the head is ALREADY at N
                                    ; when the op starts. Left at 0 by the row
                                    ; before, the first op of every row
                                    ; contains one seek instead of two and the
                                    ; row reads 12.5% short at N = 4 ops.

    mov si, [di+2]
    mov word [bl_n], 4
    mov word [bl_body], sb_b_seek
    mov al, 1
    call bl_run
    add di, 4
    pop cx
    loop .row

    mov si, sb_l_r13st              ; ...and whether the BIOS stayed happy,
    mov ax, [sb_st13]               ; because a row that FAILED is fast
    call sb_hex

.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_motor - what SPIN-UP costs (PERFORMANCE.md Part 9 Set 35)
;
; The other number the model does not have. A stopped drive has to reach
; 300 RPM before anything can be read, and the BIOS additionally waits the
; DPT's motor-start time (printed above as eighths of a second - the IBM ROM
; asks for 8, a whole second) before it will believe the platter. Nothing in
; this suite has ever separated those two from each other or from the read.
;
; `16K read, cold motor` above is a cold read, but it is a 32-sector one, so
; the spin-up is a fifth of it and buried. This is the same event with one
; sector under it, which is as close to isolating it as a package can get.
;
; **The wait is the machine's own, not ours.** The BIOS reloads a countdown at
; 0040:0040 on every disk operation and its tick handler turns the motor off
; when that reaches zero; 0040:003F bit 0 is drive 0's motor. So this waits for
; that byte to clear rather than forcing anything - and reports what it saw, so
; a run where the motor never stopped is legible as such instead of quietly
; reporting a warm read as a cold one. Bounded at SB_MT_WAIT ticks.
;
; Both rows are N = 1 and must be: the event only happens once, and a second
; iteration would be measuring a warm drive. The pair is the measurement -
; cold minus warm is spin-up plus the BIOS's motor-start wait, and the DPT row
; above says how much of it the BIOS asked for.
; -----------------------------------------------------------------------------
SB_MT_WAIT  equ 110                 ; ~6 s at 18.2065 Hz, well past the ~2 s
                                    ; the BIOS reloads

sb_mwait:
    push ax
    push bx
    push es
    call OSAPI_GET_TICKS
    mov bx, ax
.spin:
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x3F]               ; diskette motor status, bits 0..3
    test al, 0x0F
    jz .off
    call OSAPI_GET_TICKS
    sub ax, bx
    cmp ax, SB_MT_WAIT
    jb .spin
.off:
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x3F]
    xor ah, ah
    mov [sb_mtst], ax               ; 0 = it really did stop
    pop es
    pop bx
    pop ax
    ret

sb_motor:
    push ax
    push bx
    push cx
    push dx
    push si

    cmp word [sb_dbgblk], 0
    je .out

    call bl_blank
    mov si, sb_s_h_mt
    call bl_sline
    call bl_head

    call sb_r13at                   ; touch the drive so the countdown is
                                    ; loaded, then wait for it to expire
    call sb_mwait

    mov word [bl_n], 1              ; the cold one - spin-up, the BIOS's
    mov word [bl_body], sb_b_r13one ; motor-start wait, a seek and a turn
    mov si, sb_r_mtc
    mov al, 1
    call bl_run

    mov word [bl_n], 1              ; ...and the same read with the motor
    mov word [bl_body], sb_b_r13one ; already turning. The SAME body, said
    mov si, sb_r_mtw                ; twice rather than carried
    mov al, 1
    call bl_run

    mov si, sb_l_mtst               ; 00 = the motor really was off. Anything
    mov ax, [sb_mtst]               ; else and the cold row is not cold
    call sb_hex
    mov si, sb_l_r13st
    mov ax, [sb_st13]
    call sb_hex

.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sb_sktab:
    dw 1,  sb_r_sk1
    dw 5,  sb_r_sk5
    dw 10, sb_r_sk10
    dw 20, sb_r_sk20
    dw 39, sb_r_sk39

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
; as a system volume at all: a floppy moves 21,307 bytes a second warm and
; delivers 512 of them every ~24 ms (Set 24) - it was 7,457 / 65 ms at Set 17
; and 2,100 / 238 ms before SPEC.md 18.91's AL fix, and BOTH of those older
; pairs are quoted all over this tree (PERFORMANCE.md
; Part 2). It is also the only
; measurement of SPEC.md 52's driver on real spinning MFM - rung 0, the
; controller ROM, which is the only rung an 8088 can take at all.
;
; THIS BLOCK NEVER WRITES. It does not format, does not partition, does not
; create a file, and does not delete one. That is not timidity about the code,
; it is a fact about the disk: C: belongs to whoever is running this - it was
; a real DOS 3.3 install on the machine this was written for
; (docs/FIELD-MACHINES.md, E1) and is an os8088 install today - and a benchmark
; has no business leaving anything behind or removing anything it did not put
; there. What that costs is the write half
; of the picture - a `bytes/second written` row would need a scratch file, and
; a run interrupted between creating and deleting one would break that promise.
; Reads are the half that can be taken safely, so reads are what this takes.
;
; WHICH file it reads is ASKED of the volume (sb_hdpick, below) rather than
; assumed of it - the biggest ordinary root file that fits the claim - and the
; report NAMES it, so nobody has to guess what the throughput row was reading.
; It used to be COMMAND.COM, on the reasoning that a DOS 3.3 system disk has
; one; the field machine's C: is an os8088 volume now, so that row answered
; FERR_NOENT and the block measured nothing while still printing four lines.
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

    call sb_hdpick                  ; WHICH file, asked of the volume rather
                                    ; than assumed of it (see the header)

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
    mov di, sb_hname
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
    push cx
    push dx
    ; WHICH volume, asked rather than assumed (SPEC.md 18.7.2). This was a
    ; hard-coded index 2, written when index 2 could only be a hard disk;
    ; SPEC.md 18.98's external floppies made that false and this block began
    ; timing a FLOPPY under the heading `the hard disk`. OSAPI_VOL_KIND is the
    ; first call a package has ever had for the question.
    mov cl, 2                   ; A: and B: are floppies by 18.7.1, so the
.find:                          ; search starts past them
    mov al, cl
    call OSAPI_VOL_KIND
    jc .nextv                   ; no volume in that slot
    cmp al, VK_FIXED
    je .found
.nextv:
    inc cl
    cmp cl, 8                   ; DVOL_MAX, which the SDK does not publish -
    jb .find                    ; an over-long walk just answers CF=1 more
    stc                         ; nothing fixed is mounted
    jmp short .goout
.found:
    mov [sb_hdvol], cl
    xor dx, dx
    mov bl, cl
    call OSAPI_FILE_GOTO
.goout:
    pop dx
    pop cx
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
    mov si, sb_hname
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

; -----------------------------------------------------------------------------
; sb_hdpick - name the biggest ordinary file in C:'s root that fits the claim
; in:  volume 2 current, at its root
; out: [sb_hname] = a NUL name, or the empty string if there is nothing to read
;
; It used to be `COMMAND.COM`, on the reasoning that a DOS 3.3 system disk has
; one. The field machine's C: is an os8088 volume now (docs/FIELD-MACHINES.md)
; and that row has been answering FERR_NOENT ever since, so the throughput
; measurement this block exists for produced nothing at all - a benchmark that
; silently measures no bytes is the worst of the three outcomes.
;
; BIGGEST-THAT-FITS rather than first-found, because the row is a RATE and a
; 96-byte file measures the call and not the disk. Still read-only, still
; creates nothing and deletes nothing, which is this block's whole contract.
; -----------------------------------------------------------------------------
sb_hdpick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [sb_hname], 0
    mov word [sb_hbest], 0
    xor cx, cx
.next:
    push cs
    pop es
    mov di, sb_hfind
    call OSAPI_FILE_FIND
    jc .done
    cmp word [sb_hfind+14], OSAPI_FT_DIR
    jae .next                       ; a folder or the synthesized '..'
    cmp word [sb_hfind+20], 0       ; over 64KB: it cannot fit the claim and
    jne .next                       ; the high word is the only cheap test
    mov ax, [sb_hfind+18]
    cmp ax, SB_BIGKB * 1024
    ja .next
    cmp ax, [sb_hbest]
    jbe .next
    mov [sb_hbest], ax
    mov si, sb_hfind                ; bank the NAME: hd_ifind's lesson, since
    mov di, sb_hname                ; the walk overwrites the record every pass
    push cx
    mov cx, 13
    cld
    rep movsb
    pop cx
    jmp short .next
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
; label, body, 8086 x100, 286 x100, 386 x100, iterations.  (PERFORMANCE.md Part 8.1)
;
; THREE BOOKS IN ONE ROW, because a row is one instruction and the books
; disagree about it - and the alternative, a table per CPU, is three places
; that have to stay in step about what row 12 is. sb_nomof picks the column.
sb_ctab:
    dw sb_c_nop,     sb_b_nop,       300,  300,  300, 800
    dw sb_c_movrr,   sb_b_movrr,     200,  200,  200, 800
    dw sb_c_add,     sb_b_add,       300,  200,  200, 800
    dw sb_c_inc,     sb_b_inc,       200,  200,  200, 800
    dw sb_c_cmp,     sb_b_cmp,       300,  200,  200, 800
    dw sb_c_xchg,    sb_b_xchg,      300,  300,  300, 800
    dw sb_c_shl1,    sb_b_shl1,      200,  200,  300, 800
sb_e_shl4:
    dw sb_c_shlcl,   sb_b_shlcl,    2400,  900,  300, 400  ; 8+4n / 5+n / 3
sb_e_shl13:
    dw sb_c_shlcl13, sb_b_shlcl13,  6000, 1800,  300, 400  ; ...the 386 is flat
    dw sb_c_load,    sb_b_load,     1400,  500,  400, 400
    dw sb_c_store,   sb_b_store,    1500,  300,  200, 400
    dw sb_c_noovr,   sb_b_noovr,    1300,  500,  400, 400
    dw sb_c_ovr,     sb_b_ovr,      1500,  500,  400, 400  ; no override
    dw sb_c_idx,     sb_b_idx,      1700,  500,  400, 400  ; penalty past 8086
    dw sb_c_jmp,     sb_b_jmp,      1500,  700,  700, 400
    dw sb_c_pushpop, sb_b_pushpop,  1900,  800,  600, 300  ; 11+8 / 3+5 / 2+4
    dw sb_c_callret, sb_b_callret,  2700, 1800, 1700, 300  ; 19+8 / 7+11 / 7+10
sb_e_mul:
    dw sb_c_mul,     sb_b_mul,     12900, 2300, 2400, 100  ; 4+125 / 2+21 / 2+22
    dw sb_c_mulm,    sb_b_mulm,    13800, 2600, 2600, 100
sb_e_div:
    dw sb_c_div,     sb_b_div,     16000, 2600, 2600, 60   ; 3+4+153 / 2+2+22
sb_ctab_end:

SB_ENTSZ equ 12
SB_NCPU  equ (sb_ctab_end - sb_ctab) / SB_ENTSZ
; The rows other blocks reach for BY INDEX, derived from the table rather
; than written down: sb_mhz reads the machine's clock out of the two
; execution-bound rows and sb_shlbit subtracts the two shift rows, so a row
; inserted above them used to move the answer instead of the row.
SB_I_MUL   equ (sb_e_mul   - sb_ctab) / SB_ENTSZ
SB_I_DIV   equ (sb_e_div   - sb_ctab) / SB_ENTSZ
SB_I_SHL4  equ (sb_e_shl4  - sb_ctab) / SB_ENTSZ
SB_I_SHL13 equ (sb_e_shl13 - sb_ctab) / SB_ENTSZ

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
sb_hname:   times 14 db 0     ; sb_hdpick's answer, and what the report NAMES
sb_f_big:   db 'BENCH.DAT', 0
sb_f_sml:   db 'BENCHSML.DAT', 0

sb_n_8086:  db '8086/8088 (tier 0)', 0
sb_n_286:   db '80286 (tier 1)', 0
sb_n_386:   db '80386+ (tier 2)', 0
sb_n_nostamp: db 'unknown (boot sector predates the timer)', 0
sb_n_yes:   db 'yes', 0
sb_n_no:    db 'no (CF set)', 0

sb_h_1:     db 'The machine under the graphics: book clocks against this CPU, RAM', 0
sb_h_2:     db 'bandwidth, the clock, what the kernel own interrupts cost, and the floppy.', 0
sb_h_3:     db '   R  or the Bench menu   run it.  About 40 seconds on a 4.77MHz 8088 -', 0
sb_h_4:     db '                          most of it the two 16KB floppy reads - and the', 0
sb_h_5:     db '                          machine is FROZEN throughout. Watch this line.', 0
sb_h_6:     db '                          it SAVES ITSELF when it finishes.  S or the', 0
sb_h_6b:    db '                          Bench menu writes it again, after a disk swap.', 0
sb_h_7:     db '   Space PgDn PgUp Up Dn Home End   page through it afterwards.', 0

sb_s_h_hdd:  db '-- the hard disk (SPEC.md 52), if this machine has one --', 0
sb_s_hddno:  db '   No FIXED volume is mounted (18.7.2 asked, not assumed). Skipped.', 0

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
sb_l_bld:     db 'kernel build hex', 0
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
sb_s_h_vid:  db '-- the displays: what SPEC.md 39 arranged, and on which cards (39.19) --', 0
sb_s_h_vid2: db '   kind 0=Vga 1=Herc 2=Cga; avail is a BITMAP of 1<<kind, so 6 = both.', 0
sb_s_h_vid3: db '   mono probe: 0 it IS the screen, 1 memory, 2 after 3BFh, 3 refused.', 0
sb_s_h_vid4: db '   3BA/3DA: FFFF = no card drives that port. 3DA is the control.', 0
sb_s_vnone:  db '   this kernel publishes no display block (built before SPEC.md 57.4).', 0
sb_s_vsmall: db '   kern_small: single-display by CONSTRUCTION, so there is nothing set.', 0
sb_l_vkind:  db '  adapter running', 0
sb_l_vavail: db '  adapters avail (hex)', 0
sb_l_vhpr:   db '  mono probe 1/2/3', 0
sb_l_v3ba:   db '  3BA or/and (hex)', 0
sb_l_v3da:   db '  3DA or/and (hex)', 0
sb_s_h_vga:  db '   the VGA colour path, read back (SPEC.md 39.21) - run it good AND bad.', 0
sb_l_vsr:    db '  SR01 GR06 (hex)', 0
sb_l_vdac0:  db '  dac 0 r g b', 0
sb_l_vdacf:  db '  dac 3F r g b (white)', 0
sb_l_vdsum:  db '  dac 0..255 sum (hex)', 0
sb_l_vpal:   db '  attr pal 0 1 2', 0
sb_l_vpsum:  db '  attr pal sum (hex)', 0
sb_l_vssum:  db '  dac SHOWN 16 sum (hex)', 0
sb_l_vssum2: db '  ...read again (hex)', 0
sb_l_vnd:    db '  displays brought up', 0
sb_l_vdm:    db '  desktop 0=Sing 1=Ext', 0
sb_l_vdl:    db '  layout 0=Right 1=Below', 0
sb_l_vptr:   db '  pointer is on display', 0
sb_l_vdesk:  db '  desktop w h (union)', 0
sb_l_vchrm:  db '  chrome  w h (primary)', 0
sb_l_vd:     db '  -- display', 0
sb_l_vdk:    db '     adapter', 0
sb_l_vdo:    db '     origin x y', 0
sb_l_vds:    db '     size w h', 0
sb_l_vdb:    db '     stride banks', 0
sb_l_vdf:    db '     framebuffer (hex)', 0
sb_l_vdead:  db '  dead zone, 100s of px', 0
sb_s_h_mou:  db '-- the mouse: the port contest and the identify burst (SPEC.md 9.4.1) --', 0
sb_s_h_mou2: db '   base and first byte are HEX. Identified reads 4D / 1 / stamp 0.', 0
sb_s_h_mou3: db '   PS/2 step: 0 not an AT, 2 no aux port, 4-6 no answer, 9 live.', 0
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
sb_l_mpt:    db '  winning row 0/2/4=PS2', 0
sb_l_mln:    db '  winning IRQ 10=4 FF=P2', 0
sb_l_p2st:   db '  PS/2 step (9=live)', 0
sb_l_p2on:   db '  PS/2 live now', 0
sb_l_p2id:   db '  PS/2 device ID hex', 0
sb_l_p2cb:   db '  8042 cmd byte hex', 0

sb_s_h_fdd:  db '-- the floppies: is drive B really there? (SPEC.md 18.97) --', 0
sb_s_h_fdd3: db '   stop 00 not run 01 TRK0 02 after seek 03 ABSENT 04 refused 05 ST0 ok.', 0
sb_s_h_fdd7: db '   ST3 bit 4 = TRK0, ST0 overrules a clear one. ran bitmap. equip 7-6+1.', 0
sb_s_fnone:  db '   this kernel publishes no floppy block (built before SPEC.md 57.5).', 0
sb_l_feqp:   db '  drives int 11h claims', 0
sb_l_feqw:   db '  equip word hex', 0
sb_l_fsw1:   db '  SW1 direct hex', 0
sb_l_fran:   db '  probe ran bitmap hex', 0
sb_l_funit:  db '  --- unit', 0
sb_l_fst3:   db '  ST3 motor off hex', 0
sb_l_fst3b:  db '  ST3 after seek hex', 0
sb_l_fst0:   db '  ST0 drained hex', 0
sb_l_fstep:  db '  probe stop hex', 0
sb_l_fvrd:   db '  verdict 1=kept 0=gone', 0

sb_s_h_lad:  db '-- the clock: which rung of the RTC ladder answered (SPEC.md 37.90) --', 0
sb_s_h_lad2: db '   tier 0 none 1 AT 2 MM58167 3 RP5C01 4 BIOS; stop FF passed, 01-07 no.', 0
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
sb_s_h_der:  db '-- the same rows as clocks, against the 8086 book --', 0
sb_s_h_d286: db '-- the same rows as clocks, against the 80286 book --', 0
sb_s_h_d386: db '-- the same rows as clocks, against the 80386 book --', 0
sb_s_h_der2: db 'instruction           measx100  nom x100  ratiox100', 0
sb_s_h_mem:  db '-- RAM bandwidth: 2048 bytes an iteration (gfxbench has the VRAM) --', 0
sb_s_h_clk:  db '-- the clock and the timers --', 0
sb_s_h_isr:  db '-- what the kernel own interrupts cost --', 0
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
; It says which DRIVE and NOT which interleave, and that distinction cost this
; project four sets of wrong belief (PERFORMANCE.md Set 37): a B/s row divides
; by the WHOLE int 13h call, and a quarter of one is the ROM's head-settle
; delay loop, so 1:1 media reads as 2:1 and the arithmetic above agrees to
; 0.4%. Say so in the report, next to the numbers that invite it.
sb_l_dptn:   db 'DPT EOT (18.92 patches)', 0
sb_l_dpts:   db 'DPT step/head unload', 0
sb_l_dpth:   db 'DPT head settle ms', 0
sb_l_dptm:   db 'DPT motor start /8 s', 0
sb_r_131:    db 'int 13h 1 sector', 0
sb_r_139:    db 'int 13h track, 1 call', 0
sb_r_13n:    db 'int 13h track, 9 calls', 0
sb_l_r13st:  db 'int 13h last status AH', 0
sb_s_h_sk:   db '-- what a HEAD STEP costs: the same read either side of a seek --', 0
sb_r_sk0:    db 'seek 0 cyl (baseline)', 0
sb_r_sk1:    db 'seek 1 cyl, pair', 0
sb_r_sk5:    db 'seek 5 cyl, pair', 0
sb_r_sk10:   db 'seek 10 cyl, pair', 0
sb_r_sk20:   db 'seek 20 cyl, pair', 0
sb_r_sk39:   db 'seek 39 cyl, pair', 0
sb_s_h_mt:   db '-- what SPIN-UP costs: one sector cold, then the same one warm --', 0
sb_r_mtc:    db '1 sector, motor COLD', 0
sb_r_mtw:    db '1 sector, motor warm', 0
sb_l_mtst:   db 'motor status 40:3F', 0
sb_s_h_cap:  db '-- how close this report came to its own ceilings --', 0
sb_l_caprow: db 'report rows used', 0
sb_l_caprmx: db '  ...of BL_MAXROWS', 0
sb_l_capuse: db 'arena bytes used', 0
sb_l_capmax: db '  ...of BL_ARENA', 0
sb_d_r13b:   db 'bios track 1 call B/s', 0
sb_d_r13s:   db 'bios track 9 calls B/s', 0
sb_s_nodbg:  db 'This kernel carries no disk instrument - build DISKCNT=1.', 0
sb_s_h_ctr:  db '-- what os8088 own transfer path ISSUES, per operation --', 0
sb_s_h_wr:   db '-- SPEC.md 18.4: what a LARGE WRITE costs, where you started --', 0
sb_s_wnone:  db '   no room on the volume or in the heap: the write rows were skipped.', 0
sb_s_wnorep: db '   replace SKIPPED: it needs TWICE the size free (SPEC.md 18.4).', 0
sb_r_wsml:   db 'WRITE, one cluster', 0
sb_r_wbig:   db 'WRITE, create', 0
sb_r_wrep:   db 'WRITE, replace', 0
sb_r_wapp:   db 'WRITE, append in chunks', 0
sb_l_wvol:   db '  volume it wrote to', 0
sb_l_wfree:  db '  free on it, KB', 0
sb_l_wkb:    db '  ...so the write is KB', 0
sb_l_wchunk: db '  ...in chunks of KB', 0
sb_l_werr:   db '  REFUSED, FERR_', 0
sb_f_wr:     db 'SBWRITE.TMP', 0
sb_s_h_rah:  db '-- SPEC.md 18.95.4: how many chunks does the sector cache hold --', 0
sb_s_rnofile: db '   BIGFILE.DAT is not on this volume: nothing wide to walk.', 0
sb_s_rnoclaim: db '   no 8KB heap claim available: the cache rows were skipped.', 0
sb_s_rerr:   db '   a read refused mid-sweep - the rows above are what stands.', 0
sb_s_rshort: db '   BIGFILE.DAT ran out: the sweep is bounded by the FILE here,', 0
sb_s_rshort2: db '   not by the cache. Grow it, or read the rows above as a floor.', 0
sb_l_rcl:    db '  cluster bytes, probed', 0
sb_l_rstr:   db '  ...so the stride is', 0
sb_l_rw:     db '  chunks re-read, i13h', 0
sb_l_rhold:  db '  MEASURED: widest kept', 0
sb_l_rmiss:  db '  ...and the width miss', 0
sb_f_bigger: db 'BIGFILE.DAT', 0
sb_s_h_fnd:  db '-- SPEC.md 18.95.2: what a resumable FILE_FIND cursor would win --', 0
sb_s_fnfew:  db '   ...one entry or none: no slope to fit. Run it somewhere fuller.', 0
sb_r_f0:     db 'FIND, first ordinal', 0
sb_r_fl:     db 'FIND, last ordinal', 0
sb_r_fw:     db 'FIND, a whole walk', 0
sb_l_fn:     db '  entries in this dir', 0
sb_l_fsl:    db '  per entry SKIPPED', 0
sb_l_fpc:    db '  a perfect cursor would be', 0
sb_l_fwm:    db '  the walk as measured', 0
sb_l_fwin:   db '  ...so a cursor is worth', 0
sb_l_fx:     db '  ...and at 32 entries', 0
sb_l_fi13:   db '  int 13h in that walk', 0
sb_l_fsec:   db '  sectors in that walk', 0
sb_l_c16:    db '  one 16KB FILE_READ:', 0
sb_l_c16b:   db '  ...the same 16KB read again:', 0
sb_l_c1:     db '  one 1-sector FILE_READ:', 0
sb_l_c1b:    db '  ...the same 1-sector read again:', 0
sb_l_c1c:    db '  ...and again, after 16KB streamed past it:', 0
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
sb_d_shlbit: db 'shl clk/bit x100 meas', 0
sb_d_shlnom: db 'shl clk/bit x100 book', 0

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
SB_O_SYSKB equ 274              ; ...and the scalars end at 273 now
SB_O_RES   equ SB_O_SYSKB + SYSKB_SIZE
SB_O_RROW  equ SB_O_RES + SB_NCPU * 4
SB_O_RAM   equ SB_O_RROW + SB_BWROWS * 2
SB_O_RAM2  equ SB_O_RAM + SB_BWBYTES
SB_BSS_OWN equ ((SB_O_RAM2 + SB_BWBYTES + 511) / 512) * 512   ; benchlib's base must be
                                        ; 512-ALIGNED: bl_out is an int 13h target

    align 512                   ; ...and os88_image_end likewise, which this
                                ; costs up to 511 bytes of image and buys the
                                ; alignment of every bss offset below
                                ;
                                ; **THIS PACKAGE WAS AT ITS CEILING**, and
                                ; the way out was the PROSE. image + bss must
                                ; fit APP_MAX_SIZE - the 60KB SEGMENT, which
                                ; is unraisable - so with bss at 38,452 the
                                ; image may not cross 22,528. It reached
                                ; 22,519: nine bytes, with two rows fought for
                                ; a byte at a time.
                                ;
                                ; 41% of the image was TEXT (9,270 bytes in
                                ; 267 labels), and 3,893 of it was the three-
                                ; to-five explanatory lines each block carried
                                ; - restating SPEC.md to a reader who has
                                ; SPEC.md. Thirty-five of those lines went and
                                ; five legends were compressed to one line
                                ; each: **20,029, so 2,499 spare**, and four
                                ; 512-byte rungs off the package.
                                ;
                                ; What was KEPT is every section title and
                                ; every legend that turns a hex column into
                                ; meaning - probe stop codes, the RTC tiers,
                                ; the adapter kinds, the equipment-word bits.
                                ; Without those the numbers need a second
                                ; document open beside them, which is not what
                                ; a field report is for. NOTHING MEASURED WAS
                                ; REMOVED: sysbench times no glyph work (all
                                ; 28 bl_body routines are cpu, RAM, clock, API
                                ; or disk), so the strings were never test
                                ; data - that is tests/fontbench's job.
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
sb_fdstate  equ os88_image_end + 244   ; word: -> the floppy state span (245),
                                       ; SPEC.md 57.5. MOVED TWICE, and the
                                       ; second time is the lesson: at the
                                       ; merge it went from 214 (sb_hbest on
                                       ; the branch this met) to 240 - which is
                                       ; sb_skcyl, so the collision was carried
                                       ; rather than fixed. It only ever worked
                                       ; because sb_disk runs BEFORE sb_fdd and
                                       ; sb_fdd reads this word in the same
                                       ; breath it writes it: nothing enforces
                                       ; either, and two equs naming one word
                                       ; assemble perfectly and produce a
                                       ; report full of plausible nonsense
                                       ; (docs/UPSTREAM.md's whole point about
                                       ; a silent difference). Past every
                                       ; scalar now, with SB_O_SYSKB moved up
sb_hdvol    equ os88_image_end + 247   ; byte: the FIXED volume sb_hdd found
                                       ; (SPEC.md 18.7.2), which used to be a
                                       ; hard-coded 2. 247 was the spare
sb_fdrow    equ os88_image_end + 246   ; byte: the unit sub-block sb_fdu is
                                       ; printing, an offset into the span
                                       ; above (SPEC.md 18.98)
sb_vkind    equ os88_image_end + 184   ; word: -> vid_kind    (SPEC.md 57.4)
sb_vavail   equ os88_image_end + 186   ; word: -> vid_avail
sb_vnd      equ os88_image_end + 188   ; word: -> ndisp/cur/ox/oy/dmode/dlay
sb_vcur     equ os88_image_end + 190   ; word: -> cur_disp
sb_vctx     equ os88_image_end + 192   ; word: -> the context records...
sb_vstr     equ os88_image_end + 194   ; word: ...and their stride
sb_vdesk    equ os88_image_end + 196   ; word: -> vid_w, vid_h
sb_vchr     equ os88_image_end + 198   ; word: -> vid_pw, vid_ph
sb_vrp      equ os88_image_end + 200   ; word: the record being printed
sb_rshort   equ os88_image_end + 202   ; byte: the sweep ran off the END of
                                       ; BIGFILE.DAT rather than refusing (203)
sb_fbord    equ os88_image_end + 124   ; word: the ordinal the timed FIND asks
sb_fask     equ os88_image_end + 126   ; word: ...and the one the counter did
sb_fn       equ os88_image_end + 128   ; word: entries in this directory
sb_fordl    equ os88_image_end + 130   ; word: the last ordinal that answered
sb_fa       equ os88_image_end + 132   ; dword: FIND(0), hundredths of a us
sb_fb       equ os88_image_end + 136   ; dword: ...and FIND(the last ordinal)
sb_fw       equ os88_image_end + 140   ; dword: one whole walk (140..143)
sb_fslope   equ os88_image_end + 144   ; dword: per entry SKIPPED (144..147)
sb_fent     equ os88_image_end + 148   ; OSAPI_FIND_SZ bytes (148..171)
sb_rseg     equ os88_image_end + 172   ; word: the cache block's own claim
sb_rcl      equ os88_image_end + 174   ; word: the volume's cluster bytes
sb_rstr     equ os88_image_end + 176   ; word: ...and the stride it implies
sb_rw       equ os88_image_end + 178   ; word: the working set being tried
sb_rlast    equ os88_image_end + 180   ; word: the widest that stayed free
sb_rfirst   equ os88_image_end + 182   ; word: ...and the first that missed
sb_p2state  equ os88_image_end + 184   ; word: -> the PS/2 mouse's 7-byte span
                                       ; (SPEC.md 9.4.4), the mouse block's
                                       ; fifth word. In the gap at 184, which
                                       ; the scalar map above leaves free
sb_wseg     equ os88_image_end + 204   ; word: the write buffer's claim
sb_wkb      equ os88_image_end + 206   ; word: ...and the KB it actually got
sb_werr     equ os88_image_end + 208   ; word: the FERR_* a write refused with
sb_wfree    equ os88_image_end + 210   ; word: KB free on the volume before
sb_wdone    equ os88_image_end + 212   ; word: KB appended so far
sb_hbest    equ os88_image_end + 214   ; word: the biggest fitting file so far
sb_hfind    equ os88_image_end + 216   ; OSAPI_FIND_SZ bytes (216..239)
sb_skcyl    equ os88_image_end + 240   ; word: the cylinder the seek pair steps
                                       ; to, 0 for the baseline row
sb_mtst     equ os88_image_end + 242   ; word: 40:3F before the cold row - 0 if
                                       ; the motor really had stopped (242..243)
sb_v3a      equ os88_image_end + 248   ; word: sb_v3's three staged values
sb_v3b      equ os88_image_end + 250   ; word:
sb_v3c      equ os88_image_end + 252   ; word:
sb_vsum     equ os88_image_end + 254   ; word: the DAC checksum being built
sb_vpal     equ os88_image_end + 256   ; 16 bytes: the attribute palette as
                                       ; read, so the DAC entries the screen
                                       ; goes through can be summed (256..271)
sb_vshown   equ os88_image_end + 272   ; word: the first shown-16 sum, so the
                                       ; second can be compared against it
sb_syskb    equ os88_image_end + SB_O_SYSKB    ; SYSKB_SIZE bytes
sb_res      equ os88_image_end + SB_O_RES      ; SB_NCPU dwords
sb_rrow     equ os88_image_end + SB_O_RROW     ; SB_BWROWS words
sb_ram      equ os88_image_end + SB_O_RAM      ; the bandwidth source...
sb_ram2     equ os88_image_end + SB_O_RAM2     ; ...and destination

    BL_BSS os88_image_end + SB_BSS_OWN
