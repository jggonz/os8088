; REDLINE — native CPU, memory and graphics benchmark (SPEC.md 103).
%include "os88api.inc"
RL_ROWS equ 25
RL_RUNS equ 3
    OS88_HEADER 'REDLINE', rl_entry, OS88_F_ICON

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A tachometer: the dial's rim, three ticks, the RED LINE as a filled band on
; the upper right of the arc, and the needle in it. The mask is the dial
; filled and grown by a pixel, so it reads on any desktop pattern.
;
;   ................
;   ................
;   .....######.....
;   ...##..##..##...
;   ..#.........##..
;   .#.#.......####.
;   .#.........####.
;   #.........#.####
;   #.#......#...###
;   #........#....##
;   #.......#......#
;   #......##......#
;   #.....####.....#
;   ################
;   ................
;   ................
    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay)
    dw 0x0FF0
    dw 0x3FFC
    dw 0x7FFE
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0000
    dw 0x07E0
    dw 0x1998
    dw 0x200C
    dw 0x501E
    dw 0x401E
    dw 0x802F
    dw 0xA047
    dw 0x8043
    dw 0x8081
    dw 0x8181
    dw 0x83C1
    dw 0xFFFF
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END
%define BL_ARENA_BYTES 8192             ; worst report 7,700 (SPEC.md 103.4.3)
%define BL_BOTTOM_ROWS 3            ; leave 24 pixels for view/action buttons
%define BL_TOP_ROWS 3               ; ...and 24 for the tabs (SPEC.md 103.6)
%define BL_NUMNAME                  ; CHIPSET.TXT numbers itself (103.6)

rl_entry:
    mov si, rl_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [rl_win], bx
    OS88_REGION_MOVABLE_NOKEEP      ; nothing kept: AX and DX are set below
                                    ; before they are read, CF is the `clc`
    mov ax, ru_timer
    call OSAPI_WM_ONTIMER
    mov al, 1
    call OSAPI_WM_SNAP
    mov si, rl_menus
    call OSAPI_MENU_SET
    mov si, rl_about
    call OSAPI_ABOUT_SET
    mov ax, bx
    mov bx, ru_buttons
    mov si, ru_mouseup
    mov di, ru_drag
    xor dx, dx
    call os88ui_btninit
    mov bx, [rl_win]
    mov ax, ru_press
    call OSAPI_WM_ONCLICK           ; refresh geometry, then enter SDK dispatch
    mov si, rl_intro
    call bl_sline
    mov si, rl_hint
    call bl_sline
    mov si, rl_hint2
    call bl_sline
    mov bx, [rl_win]                ; loader publishes the returned window
    clc
.out:
    ret
rl_paint:
    jmp ru_paint

; ru_clip - arm the kernel's clip on our window for a handler's drawing.
; CF = 1: none of it is visible. Preserves every register.
ru_clip:
    push bx
    mov bx, [rl_win]
    call OSAPI_WM_CLIP_SET
    pop bx
.out:
    ret
rl_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [rl_win], si
    cmp byte [rl_busy], 0
    jne .out
    mov dl, al                      ; the letter, either case; AL stays the
    and dl, 0xDF                    ; key for the navigation keys below
    cmp dl, 'B'                     ; the TABS (SPEC.md 103.6), on either
    je .benchtab
    cmp dl, 'H'
    je .tunetab
    cmp byte [cs_tab], 0            ; the Tune tab's keys are its own
    jne cs_tabkey
    cmp dl, 'R'
    je .run
    cmp dl, 'S'
    je .save
    cmp dl, 'U'
    je .summary
    cmp dl, 'D'
    je .detail
    cmp dl, 'C'
    je .compare
    cmp dl, 'Q'
    je .quit
    jmp ru_navigation.saved         ; the same eight registers, pushed alike
.summary:
    mov byte [ru_view], 0
    jmp .draw
.detail:
    mov byte [ru_view], 1
    jmp .draw
.compare:
    mov byte [ru_view], 2
.draw:
    call rl_repaint
    jmp .out
.quit:
    mov bx, [rl_win]
    call OSAPI_WM_CLOSE
    jmp .out
.benchtab:
    mov al, 0
    call cs_settab
    jmp .out
.tunetab:
    mov al, 1
    call cs_settab
    jmp .out
.run:
    call rl_run
    call rl_repaint
    jmp .out
.save:
    cmp byte [rl_ran], 0
    je .out
    mov si, rl_filename
    call bl_save
    mov si, [rl_win]
    call ru_clip
    jc .out
    call rl_paint
.out:
    jmp ru_pop8
rl_click:
    ret                            ; buttons act on release, not body clicks
rl_cmd:                             ; AL = the Bench item: its key
    push bx
    mov bx, rl_cmdkeys
    xlatb
    pop bx
    cmp al, 'h'                     ; every item but Tune is the Benchmark
    je .key                         ; tab's, so it goes there first
    push ax
    mov al, 0
    call cs_settab
    pop ax
.key:
    jmp rl_key
rl_about:
    mov byte [ru_view], 1
    mov word [bl_top], 0
    push ax
    mov al, 0
    call cs_tabset                  ; the Benchmark tab, one repaint
    pop ax
    jmp rl_repaint

rl_run:
    mov byte [rl_busy], 1
    mov word [rl_active_label], rl_running
    xor ax, ax
    mov [rl_pass], ax
    mov [rl_active_row], ax
    mov [rl_ran], al
    mov [bl_nrow], ax
    mov [bl_used], ax
    mov [bl_top], ax
    mov [bl_full], ax               ; ...and bl_saved, the byte after it
    mov bx, [rl_win]
    call OSAPI_WM_TIMER             ; AX = 0: disarm the animation's timer
    ; Rerunning re-queries everything. Never preserve stale CPUID/RAM evidence.
    push ds
    pop es
    mov di, rl_facts_start
    xor ax, ax
    mov cx, (rl_facts_end - rl_facts_start) / 2
    cld
    rep stosw
    mov byte [rl_early], 0xFF
    mov si, rl_running
    call bl_progress
    call OSAPI_CUR_BUSY
    call rl_inventory
    call rl_select_reference
    call rl_canvas_select
    call rl_facts
    mov bx, [rl_win]
    call OSAPI_WM_CONTENT
    add ax, 16
    mov [rl_x], ax
    add dx, 60
    mov [rl_y], dx
    ; Known source contents make both repeat runs and adapters comparable.
    push ds
    pop es
    mov di, rl_src
    mov ax, 0x55AA
    mov cx, 1024
    rep stosw
    mov di, rl_pixels
    mov ax, 0x1111
    mov cx, 2048
    rep stosw
    call rl_repaint
    call rl_lab_open
    jnc .labready
    mov byte [rl_busy], 0
    mov si, rl_laberror
    jmp bl_sline
.labready:
    call bl_baseline
    mov si, rl_lastpass
    call bl_sline
    mov si, rl_method
    call bl_sline
    call bl_head
    mov ax, [bl_nrow]
    mov [rl_reportrow], ax
    mov ax, [bl_used]
    mov [rl_reportused], ax
.pass:
    xor bx, bx
.next:
    mov [rl_tableoff], bx
    mov ax, bx
    mov cl, 3
    shr ax, cl                      ; 8 bytes a table row
    mov [rl_active_row], ax
    mov si, [rl_table+bx]
    mov [rl_active_label], si
    call ru_progress_update
    mov bx, [rl_labwin]
    call OSAPI_WM_CLIP_SET
    mov bx, [rl_tableoff]
    mov si, [rl_table+bx]
    mov ax, [rl_table+bx+2]
    mov [bl_body], ax
    mov ax, [rl_table+bx+4]
    mov [bl_n], ax
    mov ax, [rl_table+bx+6]          ; slow compositor work uses tick timing
    or ax, ax
    jnz .meth
    ; ONE UNTIMED ITERATION, INTERRUPTS ON, before a method-P row: bl_time runs
    ; each iteration under cli, and a body that spans a whole tick laps the PIT
    ; there - which bl_run only finds out after the WHOLE row has run blind.
    ; On a 4.77 MHz machine that was ~40 s a pass with IRQ0 held off (wire,
    ; shaded, fractal, composition...), the clock losing every tick of it and
    ; a serial mouse overrunning. A body that took two ticks or more here goes
    ; straight to method T, the counts the references already hold for it.
    call OSAPI_GET_TICKS
    push ax
    call word [bl_body]             ; the body may clobber anything bar BP
    call OSAPI_GET_TICKS
    pop dx
    sub ax, dx                      ; modular, as bl_run's own
    cmp ax, 2
    mov ax, 0                       ; (mov leaves CF)
    jb .meth
    inc ax                          ; method T
.meth:
    mov bx, [rl_tableoff]
    mov si, [rl_table+bx]           ; the label again: the probe spent SI
    mov word [rl_angle], 0        ; identical complete revolutions in every row
    mov byte [bl_lapped], 0        ; explicit T rows must not inherit a P lap
    call bl_run
    push bx
    mov bx, [rl_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    mov di, bx
    shr di, 1
    mov ax, [rl_pass]
    mov cx, RL_ROWS*4
    mul cx
    add di, ax
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [rl_samples+di], ax
    mov [rl_samples+di+2], dx
    push bx
    mov bx, di
    shr bx, 1
    shr bx, 1
    mov al, 'P'
    cmp byte [bl_meth], 0
    je .pitflag
    mov al, 'T'
.pitflag:
    cmp word [bl_max], BL_SUSPECT
    jb .lapflag
    mov al, '!'
.lapflag:
    cmp byte [bl_lapped], 0
    je .flag
    mov al, 'w'
.flag:
    mov [rl_sampleflags+bx], al
    pop bx
    add bx, 8
    cmp bx, RL_ROWS*8
    jb .next
    inc word [rl_pass]
    cmp word [rl_pass], RL_RUNS
    jae .mean
    mov ax, [rl_reportrow]
    mov [bl_nrow], ax
    mov ax, [rl_reportused]
    mov [bl_used], ax
    jmp .pass
.mean:
    xor di, di
.average:
    xor ax, ax
    mov [bl_m], ax
    mov [bl_m+2], ax
    mov [bl_m+4], ax
    mov si, di
    mov cx, RL_RUNS
.sum:
    mov ax, [rl_samples+si]
    add [bl_m], ax
    mov ax, [rl_samples+si+2]
    adc [bl_m+2], ax
    adc word [bl_m+4], 0
    add si, RL_ROWS*4
    loop .sum
    mov cx, RL_RUNS
    call bl_div48
    mov ax, [bl_m]
    mov dx, [bl_m+2]
    mov [rl_results+di], ax
    mov [rl_results+di+2], dx
    add di, 4
    cmp di, RL_ROWS*4
    jb .average
    call rl_sample_report
    call bl_blank
    mov ax, [bl_nrow]
    mov [rl_clockrow], ax
    call rl_clockreport
    call rl_frontclock
    call ru_indices
    call rl_comparisons
    mov si, rl_footer
    call bl_sline
    mov byte [rl_ran], 1
    mov byte [rl_busy], 0
    mov bx, [rl_labwin]
    call OSAPI_WM_DESTROY          ; secondary record only; retain the package
    mov word [rl_labwin], 0
    call ru_start_animation
    inc word [rl_runs]
    mov word [bl_top], 0
    ret

; rl_repaint - the whole window again, from a HANDLER (a key, a menu, a
; button's release, an alert's answer). The kernel arms a clip for W_PAINT
; and for nothing else, so this arms one first: without it the repaint lands
; on whatever lies OVER the window - the Tune tab's own alert was painted out
; of sight this way the instant it opened (SPEC.md 103.6)
rl_repaint:
    call ru_clip
    jc ru_clip.out                  ; not a pixel of us shows: nothing to do
    mov bx, [rl_win]
    call OSAPI_WM_CONTENT
    push ax
    push dx
    call OSAPI_WM_GEOM
    pop bx
    pop ax
    add cx, ax
    add dx, bx
    dec cx
    dec dx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov si, [rl_win]
    jmp rl_paint

; Inventory values are labelled by their sources, rather than "installed" guesses.
rl_facts:
    mov si, rl_intro
    call bl_sline
    mov si, rl_l_canvas
    mov ax, [rl_canvas_h]
    call rl_num
    mov si, rl_l_cpu
    mov di, [rl_cpuname]
    call bl_kvs
    cmp byte [rl_cpuid], 0
    je .ram
    mov si, rl_brand
    cmp byte [si], 0
    je .signature
    call bl_sline
.signature:
    cmp byte [rl_leaf1], 0
    je .ram
    mov si, rl_l_family
    mov ax, [rl_family]
    call rl_num
    mov si, rl_l_model
    mov ax, [rl_model]
    call rl_num
    mov si, rl_l_step
    mov ax, [rl_stepping]
    call rl_num
    mov si, rl_l_sig
    mov ax, [rl_signature]
    mov dx, [rl_signature+2]
    mov cx, 10
    call bl_kv
    mov si, rl_l_features
    mov ax, [rl_features]
    mov dx, [rl_features+2]
    call bl_kv
.ram:
    mov si, rl_l_conv
    mov ax, [rl_convkb]
    call rl_num
    cmp byte [rl_mapok], 0
    je .ext
    mov si, rl_l_ram
    mov ax, [rl_ramkb]
    mov dx, [rl_ramkb+2]
    mov cx, 10
    call bl_kv
    jmp .os
.ext:
    cmp byte [rl_extok], 0
    je .unknown
    mov si, rl_l_ext
    mov ax, [rl_extkb]
    call rl_num
    mov si, rl_extlimit
    call bl_sline
    jmp .os
.unknown:
    mov si, rl_ramunknown
    call bl_sline
.os:
    mov si, rl_l_free
    mov ax, [rl_free]
    call rl_num
    mov si, rl_l_largest
    mov ax, [rl_largest]
    call rl_num
    mov si, rl_l_xfree
    mov ax, [rl_xfree]
    call rl_num
    mov si, rl_l_fpu
    mov ax, [rl_tier]
    and ax, CPU_F_X87 << 8
    jz .fpu
    mov ax, 1
.fpu:
    call rl_num
    mov si, rl_l_video
    mov ax, [rl_video]
    and ax, 255
    call rl_num
    mov si, rl_videokey
    call bl_sline
    mov si, rl_l_width
    mov ax, [rl_vw]
    call rl_num
    mov si, rl_l_height
    mov ax, [rl_vh]
    call rl_num
    mov si, rl_l_bpp
    mov al, [rl_video+1]
    xor ah, ah
    call rl_num
    mov si, rl_l_eq
    mov ax, [rl_equipment]
    call rl_num
    push es
    mov ax, 0xF000
    mov es, ax
    mov di, rl_biosdate
    mov bx, 0xFFF5
    mov cx, 8
.date:
    mov al, [es:bx]
    ; A bogus date in a clone ROM should remain printable.
    cmp al, 32
    jb .bad
    cmp al, 126
    jbe .char
.bad:
    mov al, '?'
.char:
    mov [di], al
    inc di
    inc bx
    loop .date
    mov si, rl_l_bios
    mov di, rl_biosdate
    call bl_kvs
    mov si, rl_l_biosmodel
    xor ax, ax
    mov al, [es:0xFFFE]
    call rl_num
    mov ax, 0x40
    mov es, ax
    xor bx, bx
.ports:
    mov si, [rl_portlabels+bx]
    mov ax, [es:bx]
    call rl_num
    add bx, 2
    cmp bx, 14
    jb .ports
    pop es
    jmp bl_blank
rl_num:
    xor dx, dx
    mov cx, 10
    jmp bl_kv

; Display MHz x100 with decimal point; different clock sources remain separate.
rl_mhzline:
    call bl_lclr
    xor di, di
    call bl_lput
    mov di, 24
    mov cx, 8
    call ru_cents
    mov si, rl_unitmhz
    mov di, 36
    call bl_lput
    jmp bl_lcommit
; Each line also sets the Snapshot pane's one clock fact (ru_fclock): the
; nominal figure, else the TSC's, else the MUL estimate, else n/a.
rl_clockreport:
    xor ax, ax
    cwd
    mov di, ru_mhzest
    call rl_clockset
    mov ax, [rl_nominal]
    or ax, ax
    jz .tsc
    mov cx, 100
    mul cx
    mov di, ru_mhznom
    call rl_clockset
    mov si, rl_l_nominal
    call rl_mhzline
.tsc:
    mov ax, [rl_tscmhz]
    mov dx, [rl_tscmhz+2]
    mov cx, ax
    or cx, dx
    jz .legacy
    cmp word [rl_nominal], 0        ; the pane keeps a nominal figure
    jne .tscline
    mov di, ru_mhztsc
    call rl_clockset
.tscline:
    mov si, rl_l_tsc
    call rl_mhzline
    mov si, rl_tsclimit
    jmp bl_sline
.legacy:
    cmp word [rl_nominal], 0
    jne .out
    mov bx, 0                       ; book column: 808x / 286 / 386
    cmp byte [rl_early], 1
    jbe .estimate
    mov bx, 2
    cmp word [rl_cpuname], rl_cpu286
    je .estimate
    mov bx, 4
    cmp word [rl_cpuname], rl_cpu386
    je .estimate
    mov si, rl_clockunknown
    jmp bl_sline
.estimate:
    mov [rl_book], bx
    mov ax, [rl_mulbook+bx]
    mov di, 8                       ; MUL result (row 2)
    call rl_estimate
    call rl_clockval                ; (the label is ru_mhzest already)
    mov si, rl_l_mulmhz
    call rl_mhzline
    mov bx, [rl_book]
    mov ax, [rl_divbook+bx]
    mov di, 12                      ; DIV result (row 3)
    call rl_estimate
    mov si, rl_l_divmhz
    call rl_mhzline
    mov si, rl_clocklimit
    call bl_sline
.out:
    ret
; DI = the pane's clock label, DX:AX = its MHz x100. Preserves everything.
rl_clockset:
    mov [ru_clocklabel], di
rl_clockval:
    mov [ru_clockvalue], ax
    mov [ru_clockvalue+2], dx
    ret
rl_estimate:
    ; nominal clocks/body * N * PIT_Hz / counts / 10000 = MHz x100.
    ; MUL/DIV bodies are 64 copies. Divide measured counts by N first;
    ; denominator counts/body >=~300 on the supported generations.
    push ax
    mov ax, [rl_results+di]
    mov dx, [rl_results+di+2]
    mov cx, 64
    call bl_div32
    mov bx, ax
    pop ax
    mov cx, 7636                    ; 64 * 1.193182 * 100, rounded
    mul cx
    or bx, bx
    jz .zero
    cmp dx, bx
    jae .zero
    div bx
    xor dx, dx
    ret
.zero:
    xor ax, ax
    xor dx, dx
    ret

; Move clock lines beside CPU identity by rotating report INDEX entries.
; Text stays in its arena; no second copy and no drawing inside timing spans.
rl_frontclock:
    push ds
    pop es
    cld
    mov ax, [bl_nrow]
    sub ax, [rl_clockrow]
    mov [rl_clockrows], ax
    xchg cx, ax
    mov si, [rl_clockrow]
    shl si, 1
    add si, bl_idx
    mov di, rl_clockidx
    rep movsw                       ; set the clock rows' entries aside
    mov si, [rl_clockrow]
    shl si, 1
    add si, bl_idx - 2
    mov di, [rl_clockrows]
    shl di, 1
    add di, si
    mov cx, [rl_clockrow]
    sub cx, 3                       ; keep title, canvas and CPU/vendor at
                                    ; rows 0..2 (rl_facts' order)
    std
    rep movsw                       ; the rest down, last first: they overlap
    cld
    mov cx, [rl_clockrows]
    mov si, rl_clockidx
    mov di, bl_idx + 6              ; ...so the clock lines land at row 3
    rep movsw
    ret

 ; Select by the actual OS mode; EGA or altered geometry is not VGA 12h.
rl_select_reference:
    mov word [rl_gfxbase], 0
    mov word [rl_gfxname], rl_refunknown
    mov si, rl_reftab
    cld
.try:
    lodsw                           ; the mode word, or 0 past the last
    or ax, ax
    jz .out
    cmp ax, [rl_video]
    lodsw
    jne .skip4
    cmp ax, [rl_vw]
    lodsw
    jne .skip2
    cmp ax, [rl_vh]
    jne .skip2
    lodsw
    mov [rl_gfxbase], ax
    lodsw
    mov [rl_gfxname], ax
.out:
    ret
.skip4:
    lodsw
.skip2:
    lodsw
    lodsw
    jmp .try
; mode word, width, height, the graphics reference, its name
rl_reftab:
    dw 0x0102, 640, 200, rl_baseline+24, rl_refcga
    dw 0x0101, 720, 348, rl_baseline_herc, rl_refherc
    dw 0x0400, 640, 480, rl_baseline_vga, rl_refvga
    dw 0

; DI = workload byte offset; DX:AX = measured reference, CF = unavailable.
; CPU/RAM use the common PC reference; graphics use the selected mode.
rl_reference:
    push bx
    push di
    mov bx, rl_baseline
    cmp di, 24
    jb .read
    mov bx, [rl_gfxbase]
    or bx, bx
    jz .missing
    sub di, 24
.read:
    mov ax, [bx+di]
    mov dx, [bx+di+2]
    clc
    jmp .out
.missing:
    xor ax, ax
    xor dx, dx
    stc
.out:
    pop di
    pop bx
    ret

rl_comparisons:
    mov si, rl_indexintro
    call bl_sline
    mov si, rl_gfxlabel
    mov di, [rl_gfxname]
    call bl_kvs
    ; Each bucket's mean, ahead of the per-workload rows (and of the scale
    ; line the profile tools read those rows after).
    xor bx, bx
.group:
    mov di, bx
    shl di, 1
    mov si, [rl_glabels+di]
    shl di, 1
    mov ax, [ru_gscores+di]
    mov dx, [ru_gscores+di+2]
    mov cx, ax
    or cx, dx
    jz .gna
    mov cx, 10
    call bl_kv
    jmp .gnext
.gna:
    mov di, rl_unresolved
    cmp bx, 2
    jne .gput
    cmp word [rl_gfxbase], 0
    jne .gput
    mov di, rl_refunknown
.gput:
    call bl_kvs
.gnext:
    inc bx
    cmp bx, 3
    jb .group
    mov si, rl_scalelabel
    mov ax, [ru_scale]
    mov dx, [ru_scale+2]
    mov cx, 1000
    call bl_div32
    mov cx, 10
    call bl_kv
    xor bx, bx
.row:
    mov [rl_row], bx
    mov di, bx
    shr di, 1
    call rl_reference
    jnc .have
    mov si, rl_gfxskip
    jmp bl_sline
.have:
    mov ax, [rl_results+di]
    or ax, [rl_results+di+2]
    jnz .resolved
    mov si, [rl_table+bx]
    mov di, rl_unresolved
    call bl_kvs
    jmp .next
.resolved:
    mov ax, [ru_scores+di]          ; ru_indices' index, the one the
    mov dx, [ru_scores+di+2]        ; dashboard draws: the same division
    mov bx, [rl_row]
    mov si, [rl_table+bx]
    push bx
    mov cx, 10
    call bl_kv
    ; The text report shares the dashboard's highest-score-plus-5x scale.
    mov cx, 50
    call bl_mul48
    mov bx, [ru_scale]
    mov cx, [ru_scale+2]
    call rl_div48by32
    or dx, dx
    jnz .cap
    cmp ax, 50
    jbe .length
.cap:
    mov ax, 50
.length:
    or ax, ax
    jnz .nonzero
    inc ax
.nonzero:
    xchg cx, ax
    call bl_lclr
    mov di, bl_lscr
    mov byte [di], '['
    inc di
    push ds
    pop es
    mov al, '#'
    cld
    rep stosb                       ; CX is 1..50
    mov byte [di], ']'
    call bl_lcommit
    pop bx
.next:
    add bx, 8
    cmp bx, RL_ROWS * 8
    jb .row
    ret

 ; Divide benchlib's 48-bit product by a full 32-bit denominator. Zero
; or overflow saturates, so neither produces a plausible wrapped index.
rl_div48by32:
    mov si, bx
    or si, cx
    jz .saturate
    xor ax, ax
    xor dx, dx
    mov si, 48
.bit:
    shl word [bl_m], 1
    rcl word [bl_m+2], 1
    rcl word [bl_m+4], 1
    rcl ax, 1
    rcl dx, 1
    jc .subtract
    cmp dx, cx
    ja .subtract
    jb .next
    cmp ax, bx
    jb .next
.subtract:
    sub ax, bx
    sbb dx, cx
    or word [bl_m], 1
.next:
    dec si
    jnz .bit
    cmp word [bl_m+4], 0
    jne .saturate
    mov ax, [bl_m]
    mov dx, [bl_m+2]
    ret
.saturate:
    mov ax, 0xFFFF
    mov dx, ax
    ret

; Fixed instruction mixes, all 8086 encodings.
rl_alu:
    mov ax, 0x1234
    mov bx, 0x5678
%rep 128
    add ax, bx
    xor bx, ax
%endrep
    ret
rl_shift:
    mov ax, 0x5555
    mov cl, 4
%rep 128
    ror ax, cl
%endrep
    ret
rl_mul:
    mov bx, 7
%rep 64
    mov ax, 0x5555
    mul bx
%endrep
    ret
rl_div:
    mov bx, 7
%rep 64
    xor dx, dx
    mov ax, 0x5555
    div bx
%endrep
    ret
rl_copy:
    push ds
    pop es
    mov si, rl_src
    mov di, rl_dst
    mov cx, 1024
    cld
    rep movsw
    ret
rl_fillram:
    push ds
    pop es
    mov di, rl_dst
    mov ax, 0x55AA
    mov cx, 1024
    cld
    rep stosw
    ret
rl_rect:
    mov ax, [rl_x]
    mov bx, [rl_y]
    mov cx, ax
    add cx, 63
    mov dx, bx
    add dx, 31
    ret
rl_fillgfx:
    call rl_rect
    call OSAPI_GFX_FILL
    ret
rl_hline:
    mov ax, [rl_x]
    mov bx, ax
    add bx, 255
    mov dx, [rl_y]
    call OSAPI_GFX_HLINE
    ret
rl_frame:
    call rl_rect
    call OSAPI_GFX_FRAME
    ret
rl_text:
    mov cx, [rl_x]
    mov dx, [rl_y]
    mov si, rl_textsample
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret
rl_blit1:
    push ds
    pop es
    mov si, rl_src
    mov bp, 8
    mov ax, [rl_x]
    ; byte-align within sandbox, regardless of window position.
    add ax, 7
    and ax, 0xFFF8
    mov bx, [rl_y]
    mov cx, 64
    mov dx, 32
    call OSAPI_GFX_BLIT1
    ret
rl_blit4:
    push ds
    pop es
    mov si, rl_pixels
    mov bp, 32
    mov ax, [rl_x]
    mov bx, [rl_y]
    mov cx, 64
    mov dx, 32
    call OSAPI_GFX_BLIT4
    ret

%include "redline/workloads.inc"
%include "redline/detect.inc"
%include "redline/baseline.inc"
%include "redline/baseline-herc.inc"
%include "redline/baseline-vga.inc"
; Share the proven timing/report machinery with the developer harnesses.
%include "benchlib.inc"
%include "redline/ui.inc"
%define OS88UI_NOGLYPH
%define OS88UI_SCROLL               ; the results and fact panes' bars
%define OS88UI_DROP                 ; the Tune tab's chipset pick (103.6)
%define OS88UI_ALERT                ; ...and its two questions
%include "os88ui.inc"
%define OD_BOOTVOL                  ; REDLINE.CFG is RLAPPLY's (103.6.4)
%include "os88data.inc"
%include "redline/csrows.inc"
%include "redline/csfe.inc"
%include "redline/csne.inc"
%include "redline/csapply.inc"
%include "redline/csweep.inc"
%include "redline/chipset.inc"

rl_tpl: dw 7, 22, 632, 448, rl_title, rl_paint, rl_key, rl_click
rl_title: db 'REDLINE', 0
rl_menu: db 'Bench', 0
rl_menuitems: dw rl_runitem, rl_saveitem, ru_summary, ru_detailed, ru_compare
             dw rl_csitem
rl_csitem: db 'Tune', 0
rl_runitem: db 'Run', 0
rl_saveitem: db 'Save Report', 0
OS88_MENUSET rl_menus, rl_title, rl_cmd
    OS88_MENU rl_menu, rl_menuitems, 6
OS88_MENUSET_END rl_menus
rl_intro: db 'REDLINE 1.0  //  CPU + GRAPHICS PERFORMANCE LAB', 0
rl_hint: db 'U Summary  D Detailed  C Compare  R Run  S Save  H Chipset  Q Quit', 0
rl_hint2: db '3 runs averaged; 3 graphics tiers. F1: report help.', 0
rl_running: db 'REDLINE: probing hardware and measuring fixed workloads...', 0
rl_method: db 'PIT 1.193182 MHz; net counts, IRQs between bodies; fixed work.', 0
rl_footer: db 'S saves REDLINE.TXT here. R repeats. Home shows system facts.', 0
rl_filename: db 'REDLINE.TXT', 0
rl_l_cpu: db 'CPU / vendor', 0
rl_l_family: db 'CPUID family', 0
rl_l_model: db 'CPUID model', 0
rl_l_step: db 'CPUID stepping', 0
rl_l_sig: db 'CPUID signature (dec)', 0
rl_l_features: db 'CPUID EDX bits (dec)', 0
rl_l_conv: db 'BIOS conventional KB', 0
rl_l_ram: db 'BIOS E820 usable KB', 0
rl_l_ext: db 'BIOS AH88 extended KB', 0
rl_extlimit: db 'AH88 may cap at 15/64MB; this is not a physical DIMM count.', 0
rl_ramunknown: db 'Extended/total installed RAM: firmware size unavailable.', 0
rl_l_free: db 'OS free conventional KB', 0
rl_l_largest: db 'OS largest run KB', 0
rl_l_xfree: db 'OS free extended KB', 0
rl_l_fpu: db 'OS x87 detected (0/1)', 0
rl_l_video: db 'OS video adapter', 0
rl_videokey: db 'Video IDs: 0 VGA  1 Hercules  2 CGA  3 EGA. OS graphics mode.', 0
rl_l_width: db 'Screen width pixels', 0
rl_l_height: db 'Screen height pixels', 0
rl_l_bpp: db 'Screen bits per pixel', 0
rl_l_eq: db 'BIOS equipment (dec)', 0
rl_l_bios: db 'ROM BIOS date', 0
rl_l_biosmodel: db 'ROM model byte (dec)', 0
rl_portlabels: dw rl_com1, rl_com2, rl_com3, rl_com4, rl_lpt1, rl_lpt2, rl_lpt3
rl_com1: db 'BDA COM1 base (dec)', 0
rl_com2: db 'BDA COM2 base (dec)', 0
rl_com3: db 'BDA COM3 base (dec)', 0
rl_com4: db 'BDA COM4 base (dec)', 0
rl_lpt1: db 'BDA LPT1 base (dec)', 0
rl_lpt2: db 'BDA LPT2 base (dec)', 0
rl_lpt3: db 'BDA LPT3 base (dec)', 0
rl_l_nominal: db 'CPUID nominal CPU MHz', 0
rl_l_tsc: db 'Measured TSC MHz', 0
rl_l_mulmhz: db 'CPU MHz estimate (MUL)', 0
rl_l_divmhz: db 'CPU MHz estimate (DIV)', 0
rl_unitmhz: db 'MHz', 0
rl_tsclimit: db 'TSC frequency may differ from core MHz (turbo/scaling/translation).', 0
rl_clocklimit: db 'Book timing estimates: clones/wait states/prefetch can affect MHz.', 0
rl_clockunknown: db 'CPU clock MHz unavailable for this family; timings remain valid.', 0
rl_indexintro: db 'PC index: 1000 = 4.77 MHz MartyPC 5150; mode-matched graphics.', 0
rl_unresolved: db 'below timer resolution', 0
rl_gfxskip: db 'Graphics index unavailable: no reference for this mode.', 0
rl_gfxlabel: db 'Graphics reference', 0
rl_scalelabel: db 'Bar scale maximum (x)', 0
rl_glabels: dw rl_gcpu, rl_gram, rl_ggfx
rl_gcpu: db 'CPU mean index (4)', 0
rl_gram: db 'RAM mean index (2)', 0
rl_ggfx: db 'Graphics mean index (19)', 0
rl_refcga: db 'CGA 640x200x1, 4.77 MHz PC', 0
rl_refherc: db 'Hercules 720x348x1, 4.77 MHz PC', 0
rl_refvga: db 'VGA 640x480x4, 4.77 MHz PC', 0
rl_refunknown: db 'unavailable for this mode', 0
rl_gfxbase: dw 0
rl_gfxname: dw rl_refunknown
rl_textsample: db 'Redline 0123456789 AaBb CPU/GFX', 0
rl_mulbook: dw 129, 23, 24
rl_divbook: dw 160, 26, 26
rl_table:
    dw rl_label_alu, rl_alu, 64, 0
    dw rl_label_shift, rl_shift, 64, 0
    dw rl_label_mul, rl_mul, 64, 0
    dw rl_label_div, rl_div, 64, 0
    dw rl_label_copy, rl_copy, 32, 0
    dw rl_label_ram, rl_fillram, 32, 0
    dw rl_label_fill, rl_fillgfx, 24, 0
    dw rl_label_line, rl_hline, 64, 0
    dw rl_label_frame, rl_frame, 24, 0
    dw rl_label_text, rl_text, 16, 0
    dw rl_label_blit1, rl_blit1, 32, 0
    dw rl_label_blit4, rl_blit4, 16, 0
    dw rl_label_fill_large, rl_fill_large, 8, 0
    dw rl_label_lines_many, rl_lines_many, 8, 0
    dw rl_label_frames_many, rl_frames_many, 8, 0
    dw rl_label_text_grid, rl_text_grid, 8, 0
    dw rl_label_blit1_large, rl_blit1_large, 8, 0
    dw rl_label_blit4_large, rl_blit4_large, 8, 0
    dw rl_label_wire, rl_wire, 48, 0
    dw rl_label_shaded, rl_shaded, 48, 0
    dw rl_label_fractal, rl_fractal, 2, 0
    dw rl_label_patternblit, rl_patternblit, 4, 0
    dw rl_label_scroll, rl_scroll, 8, 0
    dw rl_label_composition, rl_windows_move, 4, 0
    dw rl_label_resize, rl_resize, 4, 1
rl_label_alu: db 'ALU 256 instructions', 0
rl_label_shift: db 'Rotate 128 x 4 bits', 0
rl_label_mul: db 'MUL 64 fixed operands', 0
rl_label_div: db 'DIV 64 fixed operands', 0
rl_label_copy: db 'RAM copy 2048 bytes', 0
rl_label_ram: db 'RAM fill 2048 bytes', 0
rl_label_fill: db 'Graphics fill 64x32', 0
rl_label_line: db 'Horizontal line 256px', 0
rl_label_frame: db 'Graphics frame 64x32', 0
rl_label_text: db 'Opaque text 31 cells', 0
rl_label_blit1: db 'Mono blit 64x32', 0
rl_label_blit4: db 'Packed 4bpp blit 64x32', 0
rl_win: dw 0
rl_ran: db 0
rl_runs: dw 0
rl_label_fill_large: db 'L2 Full canvas fill', 0
rl_label_lines_many: db 'L2 Line field 4px', 0
rl_label_frames_many: db 'L2 Nested frames x8', 0
rl_label_text_grid: db 'L2 Opaque text grid x8', 0
rl_label_blit1_large: db 'L2 Mono blit 128xH', 0
rl_label_blit4_large: db 'L2 Packed blit 128xH/2', 0
rl_label_wire: db 'L3 Projected wireframe cube', 0
rl_label_shaded: db 'L3 Projected shaded cube', 0
rl_label_fractal: db 'L3 Mandelbrot 64x32', 0
rl_l_canvas: db 'Canvas height pixels', 0
rl_label_patternblit: db 'L3 Patterned packed 128xH/2', 0
rl_label_scroll: db 'L3 Full canvas scroll', 0
rl_label_composition: db 'L3 Nested moving windows', 0
rl_label_resize: db 'L3 Window resize/repaint', 0
rl_busy: db 0
rl_pass: dw 0
rl_active_row: dw 0
rl_active_label: dw rl_running
rl_tableoff: dw 0
rl_reportrow: dw 0
rl_reportused: dw 0
rl_x: dw 0
rl_y: dw 0
rl_book: dw 0
rl_clockrow: dw 0
rl_clockrows: dw 0
rl_clockidx: times 8 dw 0           ; at most four clock report lines
rl_row: dw 0
rl_facts_start:
rl_tier: dw 0
rl_early: db 0
rl_cpuid: db 0
rl_cyrix: db 0
rl_extok: db 0
rl_mapok: db 0
rl_leaf1: db 0
rl_cpuname: dw 0
rl_vendor: times 14 db 0
rl_brand: times 50 db 0
rl_cpumax: dd 0
rl_signature: dd 0
rl_features: dd 0
rl_family: dw 0
rl_model: dw 0
rl_stepping: dw 0
rl_nominal: dw 0
rl_tscmhz: dd 0
rl_tscstart: dq 0
rl_clockspan: dd 0
rl_convkb: dw 0
rl_extkb: dw 0
rl_ramtotal: dq 0
rl_ramkb: dd 0
rl_mapentry: times 24 db 0
rl_free: dw 0
rl_largest: dw 0
rl_xfree: dw 0
rl_video: dw 0
rl_vw: dw 0
rl_vh: dw 0
rl_equipment: dw 0
rl_results: times RL_ROWS dd 0
rl_samples: times RL_ROWS*RL_RUNS dd 0
rl_sampleflags: times RL_ROWS*RL_RUNS db 0
rl_facts_end:
; The loader-zeroed bss. OS88_BSS_ALIGNED puts what fits in front of the
; 512-aligned block below, so it costs no pad: the two big tables first, then
; the byte- and word-sized state fills the rest of the gap to within a byte
; whatever the image's length, then what did not fit follows the block.
    OS88_ABUF ru_fbuf, RU_FACTS*10        ; ten bytes a value: 9 cells + NUL
    OS88_ABUF ru_ftab, RU_FACTS*2*2
    OS88_ABYTE ru_view               ; 0 Summary, 1 Detailed, 2 Compare
    OS88_ABYTE ru_small
    OS88_ABYTE ru_valid
    OS88_ABYTE ru_bits
    OS88_ABYTE ru_hdr
    OS88_AWORD ru_cur
    OS88_AWORD ru_kvcols
    OS88_AWORD ru_row
    OS88_AWORD ru_x
    OS88_AWORD ru_y
    OS88_AWORD ru_w
    OS88_AWORD ru_h
    OS88_AWORD ru_split
    OS88_AWORD ru_lower
    OS88_AWORD ru_foot
    OS88_AWORD ru_pitch
    OS88_AWORD ru_offset
    OS88_AWORD ru_port
    OS88_AWORD ru_resw
    OS88_AWORD ru_restop
    OS88_AWORD ru_resulty
    OS88_AWORD ru_barx
    OS88_AWORD ru_bary
    OS88_AWORD ru_barw
    OS88_AWORD ru_fillw
    OS88_AWORD ru_barvalue
    OS88_AWORD ru_bigx
    OS88_AWORD ru_bigy
    OS88_AWORD ru_gx
    OS88_AWORD ru_gy
    OS88_ABUF ru_overall, 4
    OS88_ABUF ru_clockvalue, 4
    OS88_ABUF ru_sum, 6
    OS88_ABUF rl_biosdate, 9          ; 8 ROM date bytes and their NUL
    OS88_ABUF ru_pad, 11
    OS88_ABUF ru_gscores, 3*4
    OS88_ABUF ru_buttonflags, 8*2
    OS88_ABUF ru_value, 16
    OS88_ABUF ru_rects, 32*2
    OS88_ABUF ru_scores, RL_ROWS*4
    OS88_ABUF cs_lines, CS_LINES*CS_LW  ; the Chipset report (chipset.inc)
    OS88_ABUF cs_sw_ref, 1024           ; Tune's references (csweep.inc)
    OS88_ABUF cs_bt, 2*CS_BTW           ; the BIOS's text (chipset.inc)
; ...then benchlib's, whose first buffer is the 512-aligned one int 13h moves
; (bl_out, the CR LF image bl_save writes). The workloads' three buffers
; OVERLAP it: they are live only inside rl_run, which refills rl_src and
; rl_pixels before the first body and which the save can never interrupt
; (rl_key refuses every key while rl_busy), and bl_out is live only inside
; bl_save. 8,192 bytes of bss that were never both in use.
    OS88_BSS_ALIGNED rl_sect, BL_BSS_SIZE
rl_src equ rl_sect
rl_dst equ rl_src + 2048
rl_pixels equ rl_dst + 2048
%if BL_OUTSZ < 8192
    %error the workload buffers must stay inside bl_out (raise BL_ARENA_BYTES)
%endif
    BL_BSS rl_sect
    OS88_ADJACENT bl_full, bl_saved, 1  ; rl_run clears the pair as one word
