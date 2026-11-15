; WireArena - a text-mode first-person ARENA SHOOTER (docs/plans/
; MULTIPLAYER-FPS-PLAN.md; what was built and why: docs/plans/completed/
; WIREARENA-*.md). Thin wall edges on black, solid stick figures, presented
; as changed cells only; 40 or 80 columns on CGA, Hercules, MDA or VGA.
; Six weapons, health and a shield; up to three bots at three skills; free-
; for-all, capture the flag, team deathmatch and king of the hill on the
; arenas of arenagen; two to four players over a serial cable, a local
; network or the internet - set up in the opening window (wasetup.inc).
; Keys: mouse look (OSAPI_MOUSE_REL, SPEC.md 9.14), W/A/S/D move, arrows
; turn and pitch, Space jumps, left button or Ctrl fires, R reloads, 1-6 and
; Q/E the weapon, Tab the board and the minimap, - and + 40 or 80 columns,
; F1-F5 the detail level, F10 the meter, V vertical retrace, [ and ] the
; mouse, P pause (offline), Esc leaves (linked: asked). B and P as benches
; are a test build's (WABENCH=1).
%include "os88api.inc"
    OS88_HEADER 'Wire Arena', wa_entry, OS88_F_ICON | OS88_F_PARTS, OS88_HIGH | OS88_HIGHMEM
%define OP_WANT_CSEG            ; the renderer part is far-called through its
%define OP_ONMOVE wa_partsmv    ; CODE selector, and its claim MOVES (SPEC.md
%define OP_NO_LAZYOK            ; (20.12.9.2: the arenas are fetched, never
                                ; asked about first - op_fetch's toast is
                                ; the answer)
%include "os88parts.inc"        ; 20.12.12): wa_partsmv re-reads both words
    OS88_ICON16                 ; ITS ICON: the corridor the game draws - a
    dw 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
    dw 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
    dw 0xffff, 0xc003, 0xa005, 0x9009, 0x8811, 0x87e1, 0x8421, 0x85a1
    dw 0x85a1, 0x8421, 0x87e1, 0x8811, 0x9009, 0xa005, 0xc003, 0xffff
    OS88_ICON16_END             ; frame, its corners running in to the far
                                ; wall, and the crosshair (mask, then lines)

%include "netpkg.inc"           ; ETHER.DRV's verbs (the raw ones, LINK-PLAN 12)
%include "waconst.inc"
%include "watab.inc"               ; its constants (the data comes later)
%include "wafb.inc"                ; the renderer part's ABI

%ifndef WA_METER                ; THE METER'S ROW AT THE START: 0 nothing (what
 %define WA_METER 0             ; ships), 1 the frame rate, 2 every number - a
%endif                          ; benchmark build's, `make WAMETER=2`; F10 cycles
wa_entry:
    clc                         ; kern_small refuses the relative mouse, and
    call OSAPI_MOUSE_REL        ; WireArena refuses kern_small (the owner,
    jc .small                   ; 2026-11-13): kern_big and kern_mega only
    call op_load                ; the renderer part (SPEC.md 20.12): SI is
    jc .out                     ; the kernel's, so before anything else; a
    call wa_rbind               ; refusal has said why in a toast
    jc .out
    call wa_sinmk               ; (the sine table: shipped coded)
    mov al, OP_CARVE            ; ...and its claim may move: wa_partsmv
    call op_movable             ; re-reads the two words wa_rbind banked
    mov byte [wa_fov], 1        ; 75 degrees: the owner's pick of the three
    mov byte [wa_mtr], WA_METER ; the meter: off, unless the build says so
    mov byte [wa_diff], 1       ; the bots: normal (AI-PLAN 4.3)
    call OSAPI_CPU_INFO         ; THE DETAIL LEVEL (HEIGHTS-PLAN 9.11): Medium
    cmp al, CPU_8086            ; on the target machine, High (Medium and the
    mov al, 2                   ; floor grid, ~13 ms a frame on a 4.77 MHz
    mov byte [wa_want], 1       ; 8088) on a 286 or better; keys 1..5. And
    je .lev                     ; THE WIDTH (the owner, 2026-11-14): 40
    mov al, 3                   ; columns on an 8086/8088, 80 on a 286+
    mov byte [wa_want], 0
.lev:
    call wa_setlevel
    mov si, wa_tpl
    call OSAPI_WM_CREATE
    jc .out
    OS88_REGION_MOVABLE
    mov [wa_win], bx
    mov si, wa_menus
    call OSAPI_MENU_SET
    call wa_lkinit              ; the link: Automatic, the window's tick
    mov bx, [wa_win]            ; (LINK-PLAN 5)
    clc
.out:
    ret
.small:
    push ds
    pop es
    mov si, wa_t_small
    xor cx, cx
    call OSAPI_TOAST
    stc
    ret

; --- the window ---------------------------------------------------------------
wa_paint:                       ; the link's line, then the help or the
    push si                     ; lobby (walink.inc's rows)
    call wa_lkpaint
    pop si
    cld
    ret

wa_oncmd:                       ; the menu bar: the Link menu alone
    jmp wa_lkcmd                ; (walink.inc) - a match's settings are the
                                ; setup's (NET-PLAN 2, 9.9)

; --- the bracket ----------------------------------------------------------
wa_main:
    push ds
    pop es
    mov al, [wa_want]
    call wa_setmode
    jnc .go
    mov al, 0                   ; 40 refused (Hercules): 80 it is
    call wa_setmode
    jc .out
.go:
    call wa_geominit            ; (wa_oncmd loaded the arena)
    call wa_reset
    cmp byte [wa_lkon], 0       ; a linked match: its side of things
    je .alone                   ; (walink.inc)
    call wa_lkmstart
    jmp .go2
.alone:
    call wa_bpark               ; (alone: the bots the setup did not ask for)
.go2:
    call wa_ctfstart            ; (the mode, the teams, the flags: CTF-PLAN)
    call OSAPI_MOUSE_REL        ; the desktop's motion is not ours: discard
    xor ax, ax
    mov [wa_acc], ax
    mov [wa_acc+2], ax
    call pit_now
    mov [wa_last], ax
    mov [wa_last+2], dx
.loop:                          ; DX:AX = now (the last frame's present's
    mov bx, ax                  ; end: one PIT read a frame fewer)
    mov cx, dx
    sub ax, [wa_last]
    sbb dx, [wa_last+2]
    mov [wa_last], bx
    mov [wa_last+2], cx
    cmp byte [wa_mtr], 0        ; (the meter off: nothing summed)
    je .nsum
    add [wa_tfr], ax            ; frame time, summed for the meter
    adc [wa_tfr+2], dx
.nsum:
    add ax, [wa_acc]
    adc dx, [wa_acc+2]
    cmp dx, (WA_TICK*4) >> 16   ; more than four ticks behind: drop the
    jb .owe                     ; rest of the debt (a mode switch, the
    ja .cap                     ; bench). Four, not one: at 11 frames a
    cmp ax, (WA_TICK*4) & 0xFFFF ; second the sim owes ~3 ticks a frame, and
    jbe .owe                    ; a cap of one ran the world at a third of
.cap:                           ; its speed
    mov ax, (WA_TICK*4) & 0xFFFF
    mov dx, (WA_TICK*4) >> 16
.owe:
    cmp byte [wa_paused], 0     ; (PAUSED: nothing owed, nothing kept)
    je .owe2
    xor ax, ax
    cwd
.owe2:
    xor cx, cx                  ; CX = the ticks owed (four at most): the
.cnt:                           ; debt's whole ticks, counted in registers -
    sub ax, WA_TICK             ; the rest of it is the next frame's
    sbb dx, 0
    jc .cntd
    inc cx
    jmp .cnt
.cntd:
    add ax, WA_TICK
    adc dx, 0
    mov [wa_acc], ax
    mov [wa_acc+2], dx
    jcxz .simmed
.sim:
    push cx
    call wa_tick
    pop cx
    loop .sim
.simmed:
    call wa_frame_input
    cmp byte [wa_quit], 0
    jne .out
    call wa_hudrow              ; (the HUD row's weapons and message)
    cmp byte [wa_mtr], 2        ; THE METER (F10): only with every number
    jae .timed                  ; asked for are render and present timed -
    call wa_render              ; three clock reads a frame; else the frame
    cmp byte [wa_vsync], 0      ; reads the clock once, for the sim
    je .pr1
    mov al, FSXW_VSYNC
    call OSAPI_FSX_WAIT
.pr1:
    call wa_present
    call pit_now
    cmp byte [wa_mtr], 0
    jne .mtr
    jmp .loop
.timed:
    call pit_now                ; THE METER'S SUMS, as the clock's readings
    sub [wa_trn], ax            ; themselves: render's less its start and
    sbb [wa_trn+2], dx          ; plus its end, present's the same - whole
    call wa_render              ; again once the frame is over, which is
    call pit_now                ; when wa_meter reads them
    add [wa_trn], ax
    adc [wa_trn+2], dx
    cmp byte [wa_vsync], 0
    je .present
    mov al, FSXW_VSYNC
    call OSAPI_FSX_WAIT
    call pit_now
.present:
    sub [wa_tpr], ax
    sbb [wa_tpr+2], dx
    call wa_present
    call pit_now
    add [wa_tpr], ax
    adc [wa_tpr+2], dx
.mtr:
    push dx
    push ax
    call wa_meter
    pop ax
    pop dx
    jmp .loop
.out:
    ret

; wa_setmode - AL = 0 for 80 columns, 1 for 40. CF = 1 refused.
wa_setmode:
    mov [wa_mode], al
    push ds
    pop es
    mov di, wa_fsi
    call OSAPI_FSX_MODE         ; FSXM_TEXT80 = 0, FSXM_TEXT40 = 1
    jc .out
    mov ax, [wa_fsi+FSI_SEG]
    mov [wa_vseg], ax
    mov ax, [wa_fsi+FSI_W]
    mov [wa_w], ax
    call wa_setfov
    mov ax, [wa_w]
    shl ax, 1
    mov [wa_w2], ax             ; 2W: the figures' sub-columns
    mov byte [wa_mono], 0       ; MONO IS THE DISPLAY'S KIND, not FSI_SEG's
    push dx                     ; value: on kern_mega that is a SELECTOR
    mov bx, [wa_win]            ; (SPEC.md 53.4, KERN-MEGA-MEMORY-PLAN MC7.4)
    call OSAPI_FSX_CAPS         ; and never B000h, so a Hercules there was
    cmp dl, VID_HERC            ; drawn with colour attributes. DL = the kind
    pop dx                      ; of the display our window is on - the one
    jne .colour                 ; fsx_mode's B000h fixup keys on
    mov byte [wa_mono], 1
.colour:
    mov ah, 1                   ; hide the cursor (SPEC.md 53.4's bare text
    mov cx, 0x2000              ; contract: the cursor is the app's)
    int 10h
    xor bx, bx                  ; row offsets, in cells (the HUD's; the
    mov si, wa_rowoff           ; renderer makes its own, in cell offsets)
    mov cx, WA_ROWS + 1
.rows:
    mov [si], bx
    add bx, [wa_w]
    inc si
    inc si
    loop .rows
    mov ax, [wa_rowoff+WA_ROWS*2] ; (the HUD's row, a far pointer into text
    shl ax, 1                   ; memory: wa_putc's and wa_putn's)
    mov [wa_hudp], ax
    mov ax, [wa_vseg]
    mov [wa_hudp+2], ax
    call wa_rsync               ; the mode into the renderer part, and its
    mov bx, WRV_INIT            ; row tables, stamps and lists started again
    call wa_rcall
    push ds
    pop es
    mov byte [wa_frame], 0
    call wa_mtrinit             ; and the meter's row starts again
    mov byte [wa_ovd], 1        ; (the overlay's cells move with the width,
    mov word [wa_spk], -1       ; the weapon's kept ones too)
    clc
.out:
    ret

; wa_setfov - the projection's scales for [wa_fov] (0 = 90, 1 = 75, 2 = 60
; degrees across) at [wa_w] columns: f = 1 / tan(half the field), 8.8, on
; the column scale (8W f) and both vertical ones, so a narrower view is a
; zoom and the proportions hold. A wall at the edge of a 90-degree view
; projects 1.41x the height it has at the centre; at 60 degrees 1.15x.
wa_setfov:
    mov bl, [wa_fov]
    xor bh, bh
    shl bx, 1
    mov cx, [wa_fovf+bx]
    mov ax, [wa_w]
    mul cx
    mov [wa_wf], ax             ; W f (8.8 -> sub-columns x 256 a unit at z 1)
    mov ax, [wa_w]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [wa_c16], ax            ; 8W: the screen's centre, x16 - whatever f is
    mov ax, [wa_w]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mul cx
    mov al, ah
    mov ah, dl
    mov [wa_k16], ax            ; 8 W f
    mov ax, WA_KYQ
    mul cx
    mov al, ah
    mov ah, dl
    mov [wa_kyq], ax
    mov ax, WA_KYH256
    mul cx
    mov al, ah
    mov ah, dl
    mov [wa_kyh], ax
    jmp wa_rsync                ; ...and into the renderer part
wa_fovf:    dw 256, 334, 443
wa_fovc:    db '976'    ; 1/tan(45), 1/tan(37.5), 1/tan(30), x 256

wa_reset:
    mov ax, [wa_sx]
    mov [wa_px], ax
    mov ax, [wa_sy]
    mov [wa_py], ax
    xor ax, ax
    mov [wa_pxf], ax
    mov [wa_pyf], ax
    mov ax, [wa_sa]
    mov cl, 6
    shl ax, cl
    mov [wa_ang], ax
    xor ax, ax                  ; (the rest start at 0: AX held the angle)
    mov [wa_pitch], ax
    mov [wa_jz], ax
    mov [wa_jv], ax
    mov [wa_quit], al
    mov [wa_hits], ax
    mov [wa_kills], ax
    mov [wa_feet], ax
    mov word [wa_eye], WA_EYE
%assign wa_i 0                  ; the lifts at rest, nobody on them
%rep WA_MLIFT
    mov [wa_lon+wa_i], ax
    mov [wa_loff+wa_i], ax
%assign wa_i wa_i+2
%endrep
    dec ax
    mov [wa_fpx], ax            ; (the floor: read afresh)
    inc ax
    call wa_lset0
    call wa_arm0                ; the pistol alone, full health, no shield
    call pit_now                ; (the weapon spawns' dice: the clock)
    mov [wa_seed], ax
    mov byte [wa_pkdet], 0
    call wa_pkreset             ; every pickup there, no rocket flying
    mov byte [wa_bdet], 0       ; (the bots branch at random: AI-PLAN 3.2)
    xor ax, ax                  ; THE SCORE, and three robots' names
    mov [wa_kills], ax          ; (AI-PLAN 5)
    mov [wa_kills+2], ax
    mov [wa_deaths], ax
    mov [wa_deaths+2], ax
    mov [wa_pdead], al
    mov [wa_hmsl], ax           ; (no message from the last match)
    mov [wa_paused], al         ; (nor a pause, a question, a cue)
    mov [wa_ask], al
    mov [wa_hdirt], al
    mov byte [wa_pprot], WA_PROTT
    mov [wa_hmsgt], al
    mov byte [wa_hmsgd], 1
    mov bx, wa_bname
.nm:
    call wa_rand8
    and al, WA_NNAME-1
    cmp bx, wa_bname
    je .nmok
    cmp al, [wa_bname]
    je .nm
    cmp bx, wa_bname+1
    je .nmok
    cmp al, [wa_bname+1]
    je .nm
.nmok:
    mov [bx], al
    inc bx
    cmp bx, wa_bname+WA_NFIG
    jb .nm
    mov di, wa_figs
    xor bx, bx
.fig:
    mov ax, [wa_gep]            ; ITS ROUTE: the arena's graph, from its
    dec ax                      ; home loop's first edge (AI-PLAN 3.1)
    dec ax
    mov [di+F_PATH], ax
    mov al, [wa_gph+2+bx]
    mov [di+F_HOME], al
    call wa_spawn
    add di, F_SIZE
    inc bx
    cmp bx, WA_NFIG
    jb .fig
    ret

; wa_spawn - DI = a figure: at its home loop's first edge, running, whole
wa_spawn:
    mov al, [di+F_HOME]
    ; (falls through)
wa_spawne:                      ; AL = the edge it starts on
    push si
    call wa_bset
    mov ax, [di+F_HEAD]
    mov [di+F_ANG], ax
    mov byte [di+F_BWAIT], 0
    mov word [di+F_BST], 0      ; (roaming, no reaction, no gap,
    mov word [di+F_BGAP], 0     ; unseeing, the pistol)
    mov byte [di+F_BWPN], 0
    mov word [di+F_BAMMO], 0    ; (and its target: the player)
    mov byte [di+F_HP], WA_FHP
    xor ax, ax
    cmp byte [wa_bdet], 0       ; a bench's: dummy 0 lives with a full shield,
    jne .bench                  ; its chest yellow (WEAPONS-PLAN 2.3)
    cmp byte [wa_diff], 0       ; a match's: on normal and hard, a fifth of
    je .nsh                     ; the bots' lives, any bot (the owner: "so it
    call wa_rand8               ; is not always this one")
    cmp al, WA_BSHP
    mov al, 0
    jae .nsh
    jmp .full
.bench:
    cmp di, wa_figs
    jne .nsh
.full:
    mov al, WA_FHP
.nsh:
    mov [di+F_SH], al
    mov byte [di+F_DEAD], 0
    mov byte [di+F_FLASH], 0
    mov word [di+F_PZ], 0
    call wa_figz
    call wa_runanim
    pop si
    ret

; wa_brespawn - DI = a bot whose time dead is up: back at the respawn point
; farthest (the sum of the axis distances) from the player, walking on from
; its route node (AI-PLAN 5) - in a bench, at its home loop's start
wa_brespawn:
    cmp byte [wa_bdet], 0
    jne wa_spawn
    push si
    cmp byte [wa_gside], 0      ; A TEAM MODE ON SIDES: its side's points
    je .ffa                     ; alone, the farthest from its nearest foe
    call wa_bidx                ; (CTF-PLAN 2)
    inc ax
    mov bl, al
    xor bh, bh
    mov ah, al
    mov al, [wa_tm+bx]
    push bp
    call wc_rspick
    pop bp
    mov bx, si
    jmp .placed
.ffa:
    mov si, [wa_grp]
    lodsw
    mov cx, ax
    xor bp, bp
    mov bx, si
.p:
    mov ax, [si]
    sub ax, [wa_px]
    call wa_abs
    mov dx, ax
    mov ax, [si+2]
    sub ax, [wa_py]
    call wa_abs
    add ax, dx
    cmp ax, bp
    jbe .np
    mov bp, ax
    mov bx, si
.np:
    add si, 8
    loop .p
.placed:
    mov al, [bx+6]              ; its node's first edge out
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [wa_gnp]
    mov si, ax
    mov al, [si+4]
    pop si
    jmp wa_spawne

wa_runanim:
    mov byte [di+F_ANIM], WA_A_RUN
    mov byte [di+F_NFR], WA_N_RUN
    mov byte [di+F_LOOP], 1
    mov word [di+F_PH], 0
    ret

; --- input ----------------------------------------------------------------
; once a frame: typed keys, mouse look, the trigger
wa_frame_input:
.drain:
    mov ah, 1
    int 16h
    jz .typed
    xor ah, ah
    int 16h
    mov byte [wa_hold], 0       ; any typed key frees the meter
    cmp byte [wa_ask], 0        ; LEAVE THE MATCH? asked: Y leaves, N or
    je .noask                   ; Esc stays, nothing else is taken
    cmp ah, 0x15                ; (Y)
    je .quit
    cmp ah, 0x31                ; (N)
    je .stay
    cmp ah, KSC_ESC
    jne .drain
.stay:
    mov byte [wa_ask], 0
    mov byte [wa_ovd], 1
    jmp .drain
.noask:
    cmp ah, KSC_ESC
    jne .tab
    cmp byte [wa_lkon], 0       ; (a linked match: asked first - one key
    je .quit                    ; would end your part in it)
    mov byte [wa_ask], 1
    mov byte [wa_ovd], 1
    jmp .drain
.quit:
    mov byte [wa_quit], 1
    jmp .drain
.tab:
    mov al, 1                   ; - (and the keypad's): 40 columns; = or +
    cmp ah, 0x0C                ; (and the keypad's): 80 - Clear Skies'
    je .wid                     ; view-size keys (AI-PLAN 5). Tab is the
    cmp ah, 0x4A                ; scoreboard's, held
    je .wid
    xor al, al
    cmp ah, 0x0D
    je .wid
    cmp ah, 0x4E
    jne .v
.wid:
    cmp al, [wa_mode]
    je .drain
    mov ah, [wa_mode]
    push ax
    call wa_setmode
    pop ax
    jnc .wsay
    mov al, ah                  ; refused (no 40 on Hercules): put it back
    call wa_setmode
.wsay:                          ; "Columns 40" or "80", whichever it is now
    mov bx, wa_hs_80
    cmp byte [wa_mode], 0
    je .ws
    mov bx, wa_hs_40
.ws:
    mov si, wa_hs_cols
    call wa_hudset
    jmp .drain
.v:
    cmp ah, 0x2F                ; V: present after vertical retrace
    jne .b
    xor byte [wa_vsync], 1
    mov bx, wa_hs_off           ; ("Vsync on", "Vsync off" on the HUD's
    jz .vs                      ; message: a setting says what it is now)
    mov bx, wa_hs_on
.vs:
    mov si, wa_hs_vsync
    call wa_hudset
    jmp .drain
.b:                             ; (F, G, T and L - the field of view, the
                                ; floor grid, treads, the walls' tops - are
                                ; the detail levels' now, F1-F5: the owner,
                                ; 2026-11-15)
%ifdef WA_BENCH                 ; THE BENCHES, a TEST build's (WABENCH=1):
    cmp ah, 0x30                ; B: the bench
    jne .bp
    call wa_bench
    jmp .drain
.bp:
    cmp ah, 0x19                ; P: the PLAY bench
    jne .bk
    call wa_pbstart
    jmp .drain
%endif
.bk:
    cmp ah, 0x13                ; R: reload, at any time (WEAPONS-PLAN 6)
    jne .bq
    call wa_reload
    jmp .drain
.bq:
    mov al, -1                  ; Q, E: the weapon before, the one after
    cmp ah, 0x10                ; (one the player has and can fire)
    je .cyc
    mov al, 1
    cmp ah, 0x12
    jne .bpa
.cyc:
    call wa_wcycle
    jmp .drain
.bpa:
    cmp ah, 0x19                ; P: PAUSE - offline only (a linked match
    jne .bms                    ; runs on the other machines)
    cmp byte [wa_lkon], 0
    jne .drain
    xor byte [wa_paused], 1
    mov byte [wa_ovd], 1
    jmp .drain
.bms:
    mov al, -1                  ; [ and ]: the mouse's sensitivity, a
    cmp ah, 0x1A                ; level down or up (in the session)
    je .ms
    mov al, 1
    cmp ah, 0x1B
    jne .bn
.ms:
    call wa_msset
    jmp .drain
.bn:
    cmp ah, 0x44                ; F10: THE METER - nothing, the frame rate,
    jne .bf                     ; every number, nothing again (the owner,
    mov al, [wa_mtr]            ; pass 6: off until asked for, so a frame of
    inc ax                      ; play reads no clock but its own and sums
    cmp al, 3                   ; nothing)
    jb .bm
    xor al, al
.bm:
    mov [wa_mtr], al
    call wa_mtrinit
    jmp .drain
.bf:
    mov al, ah                  ; F1..F5: the DETAIL LEVEL (HEIGHTS-PLAN
    sub al, 0x3B                ; 9.11; WEAPONS-PLAN 2.1)
    cmp al, WA_NLEV
    jae .bw
    call wa_setlevel
    xor bh, bh                  ; "Detail Medium", the level's name
    mov bl, [wa_level]
    shl bx, 1
    mov bx, [wa_hs_lev+bx]
    mov si, wa_hs_detail
    call wa_hudset
    cmp word [wa_rds], 0        ; (wa_flodv is CONFIG's: into the part now,
    je .drain                   ; if it is there - a mode set syncs it too)
    call wa_rsync
    jmp .drain
.bw:
    mov al, ah                  ; 1..6: a weapon (WEAPONS-PLAN 2.1)
    sub al, 0x02
    cmp al, WA_NWPN
    jae .drain
    call wa_wsel
    jmp .drain
.typed:
%ifdef WA_BENCH
    cmp byte [wa_pbon], 0
    je .live
    call OSAPI_MOUSE_REL        ; (the hand's motion is not the test's)
    jmp wa_pbview
%endif
.live:
    call OSAPI_MOUSE_REL
    jc .nomouse                 ; kern_small refuses: keys only
    cmp byte [wa_paused], 0     ; (paused: the hand's motion drained, and
    jne .held                   ; nothing looks or fires)
    mov bl, al                  ; the buttons
    mov ax, cx                  ; (the hand still, the usual frame: no
    or ax, dx                   ; factor, no product - a multiply and a
    jz .clamp                   ; shift of 0 by 0)
    push cx
    mov al, [wa_msens]          ; the sensitivity's level (-4..4, 0 the
    cbw                         ; first one's): its factors
    shl ax, 1
    mov si, ax
    mov ax, dx                  ; pitch: down is +dy, the horizon rises -
    imul word [wa_mspit+8+si]   ; x the level's / 64
    mov cl, 6
    sar ax, cl
    sub [wa_pitch], ax
    pop cx
    jcxz .clamp                 ; (the hand still: no turn)
    mov ax, cx                  ; yaw: right is clockwise, angle falls
    imul word [wa_msyaw+8+si]
    sub [wa_ang], ax
    jmp .clamp
.nomouse:
    cmp byte [wa_paused], 0
    jne .held
    xor bl, bl
.clamp:                         ; 112 quarter half-rows up, 290 down: 33
    mov ax, [wa_pitch]          ; and 59 degrees at 75, the same reach
    cmp ax, WA_PITCHU           ; from the tower and its foot (HEIGHTS-
    jg .cu                      ; PLAN 7.1.1; down is a falling pitch)
    cmp ax, -WA_PITCHD
    jge .fire
    mov ax, -WA_PITCHD
    jmp .cs
.cu:
    mov ax, WA_PITCHU
.cs:
    mov [wa_pitch], ax
.fire:
    xor al, al                  ; TAB HELD: the scoreboard over the view
    KEYMAP_TEST wa_kmap, 0x0F   ; (AI-PLAN 5), rebuilt as it comes and goes
    jz .sb
    inc ax
.sb:
    cmp al, [wa_sbon]
    je .sbk
    mov [wa_sbon], al
    mov byte [wa_ovd], 1
.sbk:
    KEYMAP_TEST wa_kmap, 0x1D   ; Ctrl is a trigger too: the map the
    jz .btn                     ; frame's first tick took (wa_keys)
    or bl, 1
.btn:
    and bl, 1
    cmp byte [wa_pdead], 0      ; (dead: no trigger)
    je .alive
    xor bl, bl
.alive:
    mov al, bl
    xchg al, [wa_trig]
    cmp al, 0
    jne .held                   ; edge only: one shot a press
    cmp bl, 0
    je .held
    call wa_fire
.held:
    ret

; --- the 30 Hz tick -------------------------------------------------------
; The keys a tick reads, polled ONCE A FRAME (wa_keys): the ticks a frame
; runs are its catch-up, all due at the same instant, so they read one
; poll. ONE OSAPI_KEY_MAP (SPEC.md 9.7.3) for all of them - an OSAPI_KEY_DOWN
; is ~540 clk EACH, so nine were ~1 ms of a frame, and asking only the keys
; that could have changed was still ~2,000 clk with a sweep every fourth
; frame to catch the presses the BIOS never enqueues. The map has no such
; gap: a press is seen the frame it lands, Ctrl or Alt with it or not, and a
; release the next frame.
WA_K_LEFT   equ 1
WA_K_RIGHT  equ 2
WA_K_UP     equ 4
WA_K_DOWN   equ 8
WA_K_W      equ 16
WA_K_S      equ 32
WA_K_A      equ 64
WA_K_D      equ 128
WA_K_SPACE  equ 256

; WA_KBIT scancode, bit - the held key's bit into BX
%macro WA_KBIT 2
    KEYMAP_TEST wa_kmap, %1
    jz %%up
 %if (%2) > 255
    or bh, (%2) >> 8
 %else
    or bl, %2
 %endif
%%up:
%endmacro

; wa_keys - the held keys into [wa_kbits] (WA_K_*), and the whole map into
; wa_kmap for wa_frame_input's trigger. The map's bytes the keys are in are
; taken into AX a word at a time - W with S and A, the arrows together - so
; seven of the nine tests are of a register. Keeps all but AX and the flags.
%define WA_KB(s) ((s) >> 3)             ; a scancode's byte in the map...
%define WA_KM(s) (1 << ((s) & 7))       ; ...and its bit there
%if WA_KB(0x1F) != WA_KB(0x11)+1 || WA_KB(0x1E) != WA_KB(0x1F) || WA_KB(KSC_LEFT) != WA_KB(KSC_UP) || WA_KB(KSC_RIGHT) != WA_KB(KSC_UP) || WA_KB(KSC_DOWN) != WA_KB(KSC_UP)+1
 %error "wa_keys: the scancodes are not where its two words expect them"
%endif
wa_keys:
    push bx
    push di
    mov di, wa_kmap
    call OSAPI_KEY_MAP          ; every key at once (and the first call arms)
    xor bx, bx
    mov ax, [wa_kmap+WA_KB(KSC_UP)] ; AL: up, left, right; AH: down
    test al, WA_KM(KSC_LEFT)
    jz .l
    or bl, WA_K_LEFT
.l:
    test al, WA_KM(KSC_RIGHT)
    jz .r
    or bl, WA_K_RIGHT
.r:
    test al, WA_KM(KSC_UP)
    jz .u
    or bl, WA_K_UP
.u:
    test ah, WA_KM(KSC_DOWN)
    jz .d
    or bl, WA_K_DOWN
.d:
    mov ax, [wa_kmap+WA_KB(0x11)] ; AL: W; AH: S and A
    test al, WA_KM(0x11)
    jz .w
    or bl, WA_K_W
.w:
    test ah, WA_KM(0x1F)
    jz .s
    or bl, WA_K_S
.s:
    test ah, WA_KM(0x1E)
    jz .a
    or bl, WA_K_A
.a:
    WA_KBIT 0x20, WA_K_D
    WA_KBIT KSC_SPACE, WA_K_SPACE
    mov [wa_kbits], bx
    pop di
    pop bx
    ret

wa_tick:
    cmp byte [wa_hmsgt], 0      ; the HUD's message running out
    je .c_msg
    dec byte [wa_hmsgt]
    jnz .c_msg
    mov word [wa_hmsl], 0
    mov byte [wa_hmsgd], 1
.c_msg:
    cmp byte [wa_pprot], 0      ; spawn protection running out (the heart
    je .c_pp                    ; back to red: the overlay)
    dec byte [wa_pprot]
    jnz .c_pp
    mov byte [wa_ovd], 1
.c_pp:
    cmp byte [wa_hdirt], 0      ; the hit's direction going
    je .c_hd
    dec byte [wa_hdirt]
    jnz .c_hd
    mov byte [wa_ovd], 1
.c_hd:
    cmp byte [wa_lkon], 0
    je .c_lk
    call wa_lktick              ; (a linked match: the cable, LINK-PLAN 4.2)
.c_lk:                          ; (tools' stop: the link's cost)
    mov al, [wa_frame]          ; this frame's first tick polls the keys
    cmp al, [wa_kfr]
    je .polled
    mov [wa_kfr], al
    call wa_keys
.polled:
%ifdef WA_BENCH
    cmp byte [wa_pbon], 0
    je .live
    inc word [wa_pbtk]           ; the play bench: its clock, and no keys
    mov word [wa_kbits], 0
%endif
.live:
    cmp byte [wa_pdead], 0      ; (dead: no keys)
    je .keys
    mov word [wa_kbits], 0
.keys:
    mov dx, [wa_kbits]
    or dx, dx
    jz .jump                    ; nothing held: no turn, no step
    test dl, WA_K_LEFT
    jz .r
    add word [wa_ang], 12 << 6
.r:
    test dl, WA_K_RIGHT
    jz .u
    sub word [wa_ang], 12 << 6
.u:
    test dl, WA_K_UP
    jz .d
    add word [wa_pitch], 4
.d:
    test dl, WA_K_DOWN
    jz .move
    sub word [wa_pitch], 4
.move:
    xor bx, bx                  ; BL = forward, BH = right, each -1/0/+1
    test dl, WA_K_W
    jz .s
    inc bl
.s:
    test dl, WA_K_S
    jz .a
    dec bl
.a:
    test dl, WA_K_A
    jz .dk
    dec bh
.dk:
    test dl, WA_K_D
    jz .walk
    inc bh
.walk:
    or bx, bx
    jz .jump                    ; standing: no step, nothing to collide
    call wa_camsc               ; AX = sin, DX = cos (Q14)
    sar ax, 1                   ; Q13, so forward + strafe cannot overflow
    sar dx, 1
    ; vx = fwd*cos + right*sin ; vy = fwd*sin - right*cos
    xor si, si                  ; vx
    xor di, di                  ; vy
    or bl, bl
    jz .side
    js .back
    add si, dx
    add di, ax
    jmp .side
.back:
    sub si, dx
    sub di, ax
.side:
    or bh, bh
    jz .apply
    js .left
    add si, ax
    sub di, dx
    jmp .apply
.left:
    sub si, ax
    add di, dx
.apply:
    mov ax, [wa_px]             ; (where the step starts: wa_flcross)
    mov [wa_ox], ax
    mov ax, [wa_py]
    mov [wa_oy], ax
    or si, si
    jz .ymove                   ; (no x step: the x position cannot collide)
    mov ax, si
    mov cx, WA_MSPD
    imul cx                     ; DX:AX = the step, 16.16 units
    mov bp, [wa_px]
    mov cx, [wa_pxf]
    add [wa_pxf], ax
    adc [wa_px], dx
    call wa_collide             ; (keeps CX, DI, BP)
    jnc .ymove
    mov [wa_px], bp
    mov [wa_pxf], cx
.ymove:
    or di, di
    jz .jump
    mov ax, [wa_px]             ; (the x step taken or not: from there)
    mov [wa_ox], ax
    mov ax, di
    mov cx, WA_MSPD
    imul cx
    mov bp, [wa_py]
    mov cx, [wa_pyf]
    add [wa_pyf], ax
    adc [wa_py], dx
    call wa_collide
    jnc .jump
    mov [wa_py], bp
    mov [wa_pyf], cx
.jump:
    cmp byte [wa_aflat], 0      ; HEIGHTS: the floor under the feet, the
    jne .flatj                  ; step, the fall and the landing, and the
    call wa_hjump               ; lift (HEIGHTS-PLAN 3.1, 3.2)
    jmp .grounded
.flatj:
    mov ax, [wa_jz]
    or ax, [wa_jv]
    jnz .air
    test byte [wa_kbits+1], WA_K_SPACE >> 8
    jz .grounded
    mov word [wa_jv], WA_JUMPV
.air:
    mov ax, [wa_jv]
    add [wa_jz], ax
    sub word [wa_jv], WA_GRAV
    cmp word [wa_jz], 0
    jg .grounded
    xor ax, ax
    mov [wa_jz], ax
    mov [wa_jv], ax
.grounded:
    mov ax, [wa_jz]
    mov al, ah                  ; 8.8 -> units
    cbw
    add ax, WA_EYE
    add ax, [wa_feet]           ; (the floor under the player: 0 until
    mov [wa_eye], ax            ; HEIGHTS-PLAN wave 3, but for the benches)
    cmp byte [wa_mflash], 0
    je .figs
    dec byte [wa_mflash]
.figs:
    cmp byte [wa_pdead], 0      ; (dead: the view sinks, the timer runs)
    je .figs0
    call wa_ptick
.figs0:
    xor ax, ax                  ; (the bots on each lift this tick: its
%assign wa_i 0                  ; riders the next, wa_ltick - a byte a
%rep WA_MLIFT                   ; lift, a word apart - counted straight
    mov [wa_lbot+wa_i], ax      ; into wa_lbot: its one reader, wa_lift1,
%assign wa_i wa_i+2             ; ran already this tick, so no second
%endrep                         ; table and no copy at the loop's end)
    mov bx, [wa_tkn]            ; WHOSE TURN TO LOOK (AI-PLAN 4.1): each bot
    and bx, 7                   ; every eighth tick, 0.27 s, on the tick
    shl bx, 1                   ; where tick + (WA_NFIG - its index) x 3 is a
    mov ax, [wa_blkt+bx]        ; multiple of 8 - so no two on one tick
    mov [wa_blook], ax
    mov di, wa_figs
    cmp word [wa_bdet], 0       ; (a bench or - its high byte, wa_lkon - a
    jne .lfig                   ; linked match: each figure is asked which)
    cmp di, [wa_figend]         ; THE USUAL MATCH: the bots the setup asked
    jae .figd                   ; for (wa_bpark parked the rest), each ours
.fig:                           ; and with its own mind
    call wa_figtick.solo
    call wa_bthink.mine         ; (AI-PLAN 4: sees, faces, shoots)
.c_bt:                          ; (tools' stop: its cost, by breakpoints)
    add di, F_SIZE
    cmp di, [wa_figend]
    jb .fig
.figd:
    inc word [wa_tkn]
.c_w:                           ; (the labels: tools/watrace.py's stops)
    call wa_wtick               ; the weapon's reload and its next shot,
.c_rk:                          ; the rockets, the pickups (WEAPONS-PLAN)
    cmp byte [wa_rkn], 0        ; (none live: the usual tick)
    je .c_pk
    mov byte [wa_hproj], 1      ; (the floors it asks for: a line's own)
    call wa_rktick
    mov byte [wa_hproj], 0
.c_pk:
    call wa_ctick               ; (CTF: the flags' rules, CTF-PLAN 2)
    jmp wa_pktick
.lfig:
    call wa_figtick
    cmp byte [wa_bdet], 0       ; (a bench's dummies do not think)
    jne .lnb
    call wa_bthink
.lnb:
    add di, F_SIZE
    cmp di, wa_figs + WA_NFIG*F_SIZE
    jb .lfig
    jmp .figd

%if WA_NFIG != 3
 %error "wa_blkt and wa_bidx are written for three figures"
%endif
wa_blkt:                        ; the figure that looks, by the tick's low
    dw 0, 0, wa_figs + F_SIZE, 0, 0, wa_figs + 2*F_SIZE, 0, wa_figs ; bits

; wa_collide - CF = 1 when the player at [wa_px],[wa_py] may not stand
; there: outside the room; on a FLAT arena inside a wall (every box there is
; as tall as the tallest, so the floor under the player's square - wa_floor
; over every box, each widened by the radius - is above 0 exactly there);
; on the heights where that floor is more than a step above the feet (in
; the air: above the feet at all). Keeps CX, DI, BP (its two callers' own:
; the tick's steps in x and in y); clobbers AX, BX, DX, SI.
wa_collide:
    cmp word [wa_nfl], 0        ; (an arena with no floor lines - all but
    je .nofl                    ; the twin: no call)
    call wa_flcross             ; (a floor line crossed: ARENAS-PLAN 4.5)
    jc .hit
.nofl:
    mov ax, [wa_px]
    mov bx, [wa_py]
    cmp ax, WA_ROOM_X0 + WA_RAD
    jl .hit
    cmp ax, WA_ROOM_X1 - WA_RAD
    jg .hit
    cmp bx, WA_ROOM_Y0 + WA_RAD
    jl .hit
    cmp bx, WA_ROOM_Y1 - WA_RAD
    jg .hit
    mov si, wa_allp
    call wa_floor
    cmp byte [wa_aflat], 0
    jne .flat
    xchg ax, bx                 ; DX = the feet, units (wa_fabs's sum,
    mov ax, [wa_jz]             ; here - each step of a walking tick asks
    mov si, ax                  ; it): on the ground a step more, in the
    or si, [wa_jv]              ; air nothing more - a jump reaches what
    mov al, ah                  ; its feet clear (the back room's 48, not
    cbw                         ; the platform's 80)
    add ax, [wa_feet]
    xchg ax, dx
    xchg ax, bx                 ; (AX the floor again)
    or si, si
    jnz .air
    add dx, WA_STEPUP
.air:
    cmp ax, dx
    jg .hit
.clear:
    clc
    ret
.flat:
    or ax, ax
    jz .clear
.hit:
    stc
    ret

; wa_flcross - CF = 1 when the player's step from (wa_ox, wa_oy) to (wa_px,
; wa_py) crosses one of the arena's FLOOR LINES (docs/plans/completed/WIREARENA-
; ARENAS-PLAN.md 4.5): a line the feet stop at, as the twin's walk's edge.
; A line is near only when its box and the step's meet (touching counts):
; then the step's ends lie either side of the line, and the line's ends
; either side of the step (or on it) - two cross products each, 32 bits,
; exact. Those two say the closed segments meet, so a crossing is inside
; both boxes: the boxes' test drops no line the products would stop at. (It
; was the new point within 16 of the line's box, which a step - under 15 an
; axis - also never left; the boxes are tighter, along a line the feet run
; beside.) Keeps CX, DI, BP; clobbers AX, BX, DX, SI.
;
; THE GRID (wa_flg, wa_flgmk): the twin's 16 lines were walked twice a
; walking tick, a quarter of its tick; the new point's 128-unit cell names
; the lines whose box widened by 16 reaches into it (four at most:
; arenagen's), and a cell with none - most of the room - costs the cell's
; look alone.
WA_FLGC     equ 4               ; a cell: its lines' offsets in wa_fl, 0FFh
                                ; after the last (none past the fourth)
wa_flcross:                     ; (wa_collide, its one caller, asks it
    push cx                     ; only of an arena with floor lines)
    push di
    push bp
    mov di, [wa_py]             ; THE CELL, as wa_floor's: its row x 64 is
    shr di, 1                   ; y's bits 7..10 a place down, and its
    and di, 0x03C0              ; column x 4 x's five down. (No clamp to
    mov ax, [wa_px]             ; the room: a step starts inside it by
    mov cl, 5                   ; WA_RAD, more than a step - and should the
    shr ax, cl                  ; new point leave it, wa_collide refuses
    and al, 0x3C                ; the step whatever this says)
    add di, ax
    add di, wa_flg              ; DI = the cell
    cmp byte [di], 0xFF         ; (none near it: the usual step)
    je .none
    mov ax, [wa_ox]             ; THE STEP'S BOX, for each line's test
    mov dx, [wa_px]
    cmp ax, dx
    jle .sx
    xchg ax, dx
.sx:
    mov [wa_fsb], ax
    mov [wa_fsb+2], dx
    mov ax, [wa_oy]
    mov dx, [wa_py]
    cmp ax, dx
    jle .sy
    xchg ax, dx
.sy:
    mov [wa_fsb+4], ax
    mov [wa_fsb+6], dx
    mov cx, WA_FLGC
.cell:
    mov bl, [di]
    cmp bl, 0xFF
    je .none
    xor bh, bh
    lea si, [wa_fl+bx]          ; SI = a line near the cell
.seg:
    mov ax, [si]                ; NEAR: the line's box and the step's
    mov dx, [si+4]
    cmp ax, dx
    jle .x
    xchg ax, dx
.x:
    cmp ax, [wa_fsb+2]
    jg .next
    cmp dx, [wa_fsb]
    jl .next
    mov ax, [si+2]
    mov dx, [si+6]
    cmp ax, dx
    jle .y
    xchg ax, dx
.y:
    cmp ax, [wa_fsb+6]
    jg .next
    cmp dx, [wa_fsb+4]
    jl .next
    mov ax, [wa_ox]             ; THE STEP'S ENDS either side of the line
    mov bx, [wa_oy]             ; (a point on it counts with the side its
    call .side                  ; sign bit is clear on)
    mov bp, dx
    mov ax, [wa_px]
    mov bx, [wa_py]
    call .side
    xor dx, bp
    jns .next
    mov ax, [si]                ; ...and THE LINE'S ENDS either side of the
    mov bx, [si+2]              ; step, or on it
    call .stp
    mov bp, dx
    or dx, ax
    jz .hit
    mov ax, [si+4]
    mov bx, [si+6]
    call .stp
    mov bx, dx
    or bx, ax
    jz .hit
    xor dx, bp
    js .hit
.next:
    inc di
    dec cx                      ; (no LOOP: the cell is out of its reach)
    jz .none
    jmp .cell
.none:
    clc
    jmp short .out
.hit:
    stc
.out:
    pop bp
    pop di
    pop cx
    ret
.side:                          ; AX, BX = a point: DX:AX = (x1 - x0)(y -
    sub ax, [si]                ; y0) - (y1 - y0)(x - x0)
    sub bx, [si+2]
    push ax
    mov ax, [si+4]
    sub ax, [si]
    imul bx
    pop bx
    push dx
    push ax
    mov ax, [si+6]
    sub ax, [si+2]
    imul bx
    pop bx
    sub bx, ax
    pop ax
    sbb ax, dx
    mov dx, ax
    mov ax, bx
    ret
.stp:                           ; AX, BX = a point: DX:AX = (px - ox)(y -
    sub ax, [wa_ox]             ; oy) - (py - oy)(x - ox)
    sub bx, [wa_oy]
    push ax
    mov ax, [wa_px]
    sub ax, [wa_ox]
    imul bx
    pop bx
    push dx
    push ax
    mov ax, [wa_py]
    sub ax, [wa_oy]
    imul bx
    pop bx
    sub bx, ax
    pop ax
    sbb ax, dx
    mov dx, ax
    mov ax, bx
    ret

; --- heights: the floor, the step, the fall, the lift (HEIGHTS-PLAN 3) -----
; The floor under a square is the tallest height of any box it overlaps - a
; RAMP's (a FLIGHT's, kind 1) at the square's highest point on it, the slope
; linear from 0 at its low face to its height at the face it rises toward
; (arenagen's height_at, to the unit). A move is refused where that is more
; than a step (WA_STEPUP) above the feet; on the ground the feet follow the
; floor up a step and down one, and past that they fall; in the air the
; feet land on whatever floor they come down onto. A flat arena never comes
; here (wa_aflat): its walls and its jump are as they were.

; wa_floor - AX = x, BX = y, SI = a box table (wa_lists: wa_lowp, the low
; boxes, for the player's square; wa_lowf, for a dummy's feet; wa_allp,
; every box, a wall at its height, for the player's collision): AX = the
; floor under the square (x +- r, y +- r, r the table's), DX = the record
; (x 4, into wa_bxh) of the box it is, -1 the ground. Each entry holds its
; box WIDENED by r, so overlapping it is the point inside: four compares
; against registers. Keeps BX, CX, SI, DI, BP.
;
; THE GRID: a walk over every entry was the sim's largest cost (1,400 clk a
; call, four calls a tick with the player walking and a grenade in the
; air). A table opens with 16 x 16 cells of 128 units, each the addresses
; of the entries whose widened box reaches into it, in the table's order
; (so a tie is the same box's), 0 after the last: the point's cell names
; the one or two to try. A cell that more than three reach holds 1, and
; walks them all. The edge cells take everything beyond the room, so any
; x and y get the whole table's answer.
WA_LTE      equ 12              ; a table's entry: x0-r, y0-r, x1+r, y1+r,
                                ; box x 8 (wa_boxes), box x 4 (wa_bxh)
WA_LGC      equ 8               ; a cell: three entries' addresses and a 0
WA_LG       equ 256*WA_LGC      ; the grid, at the table's start
WA_LN       equ WA_LG           ; ...then its entries' count, its r, and
WA_LR       equ WA_LG+2         ; the entries
WA_LE       equ WA_LG+4
WA_LTSZ     equ WA_LE+WA_MBOX*WA_LTE
%if WA_ROOM_X0 != 0 || WA_ROOM_Y0 != 0 || WA_ROOM_X1 != 2048 || WA_ROOM_Y1 != 2048
 %error "wa_floor's grid is 16 x 16 cells of 128 units over a room 0..2048"
%endif
wa_floor:
    push si
    push di
    push bp
    xor bp, bp                  ; BP = the floor so far, DX its record
    mov di, bx                  ; THE CELL: its row (y >> 7) x 16 cells x 8
    cmp di, WA_ROOM_Y1          ; bytes is y's bits 7..10 as they are
    jae .yout
    and di, 0x0780
.yc:
    mov dx, ax                  ; ...and its column (x >> 7) x 8
    cmp dx, WA_ROOM_X1
    jae .xout
    shr dx, 1
    shr dx, 1
    shr dx, 1
    shr dx, 1
    and dl, 0x78
.xc:
    add di, dx
    add di, si                  ; DI = the cell
    mov dx, -1
    mov si, [di]
    cmp si, 1
    jbe .few                    ; (nothing here - or more than three)
.box:
    cmp ax, [si]
    jle .next
    cmp ax, [si+4]
    jge .next
    cmp bx, [si+2]
    jle .next
    cmp bx, [si+6]
    jge .next
    mov [wa_qx], ax
    mov [wa_qy], bx
    push di
    mov di, [si+8]              ; DI = its box: the entry is the box widened
    mov ax, [di]                ; by the table's r (for a ramp's reach)
    sub ax, [si]
    mov [wa_qr], ax
    mov si, [si+10]
    xchg si, di                 ; SI = the box, DI = its record
    add di, wa_bxh
    call wa_hat                 ; AX = its height there (AX was x: again
    sub di, wa_bxh              ; after)
    cmp ax, bp
    jbe .again
    mov bp, ax
    mov dx, di
.again:
    pop di
    mov ax, [wa_qx]
.next:
    inc di
    inc di
    mov si, [di]
    or si, si
    jnz .box
.done:
    mov ax, bp
    pop bp
    pop di
    pop si
    ret
.yout:                          ; beyond the room: the edge's cells
    mov di, 0x0780
    or bx, bx
    jns .yc
    xor di, di
    jmp .yc
.xout:
    mov dx, 0x0078
    or ax, ax
    jns .xc
    xor dx, dx
    jmp .xc
.few:
    jb .done
    push cx
    mov di, sp                  ; A CROWDED CELL: every entry of the table
    mov si, [ss:di+6]           ; (SI as it came in)
    mov [wa_qx], ax
    mov [wa_qy], bx
    mov cx, [si+WA_LR]
    mov [wa_qr], cx
    mov cx, [si+WA_LN]
    add si, WA_LE
.wbox:
    cmp ax, [si]
    jle .wnext
    cmp ax, [si+4]
    jge .wnext
    cmp bx, [si+2]
    jle .wnext
    cmp bx, [si+6]
    jge .wnext
    mov di, [si+10]
    push si
    mov si, [si+8]
    add di, wa_bxh
    call wa_hat
    sub di, wa_bxh
    pop si
    cmp ax, bp
    jbe .wagain
    mov bp, ax
    mov dx, di
.wagain:
    mov ax, [wa_qx]
.wnext:
    add si, WA_LTE
    loop .wbox
    pop cx
    jmp .done

; wa_lists - once an arena is loaded: its box tables (wa_floor's), every box
; for the player's collision and the low ones (below the tallest height,
; and the lift) for the player's floor and a dummy's - each with its grid
wa_lists:
    mov di, wa_allp
    mov dx, WA_RAD
    xor bp, bp                  ; (every box)
    call .one
    mov di, wa_lowp
    mov dx, WA_RAD
    mov bp, 1                   ; (the low ones)
    call .one
    mov di, wa_lowf
    mov dx, WA_FRAD
    ; (falls through)
.one:                           ; DI = the table, DX = r, BP = 1 low only
    push di
    mov [di+WA_LR], dx
    push es
    push ds
    pop es
    xor ax, ax                  ; its grid: empty
    mov cx, WA_LG/2
    cld
    rep stosw
    pop es
    add di, 4                   ; (DI = its entries)
    xor bx, bx                  ; BX = box x 8
    xor cx, cx                  ; CX = its entries
.b:
    cmp bx, [wa_nbox8]
    jae .end
    mov si, bx
    shr si, 1                   ; SI = box x 4
    or bp, bp
    jz .take
    mov ax, [wa_bxh+si]
    cmp ax, [wa_ahmax]
    jb .take
    test byte [wa_bxh+si+2], 0x40 ; (a TALL box a stair or a lift climbs:
    jz .skip                    ; stood on all the same - ARENAS-PLAN 3)
.take:
    mov ax, [wa_boxes+bx]
    sub ax, dx
    mov [di], ax
    mov ax, [wa_boxes+bx+2]
    sub ax, dx
    mov [di+2], ax
    mov ax, [wa_boxes+bx+4]
    add ax, dx
    mov [di+4], ax
    mov ax, [wa_boxes+bx+6]
    add ax, dx
    mov [di+6], ax
    lea ax, [wa_boxes+bx]
    mov [di+8], ax
    mov [di+10], si
    add di, WA_LTE
    inc cx
.skip:
    add bx, 8
    jmp .b
.end:
    pop di
    mov [di+WA_LN], cx
    jcxz .gdone
    lea si, [di+WA_LE]          ; THE GRID: each entry, in order, into every
.ge:                            ; cell a point inside it can be in - the
    push cx                     ; cells of x0 + 1 .. x1 - 1, the same of y
    mov ax, [si+2]
    inc ax
    call .cc
    mov dl, al                  ; DL = its first row of cells...
    mov ax, [si+6]
    dec ax
    call .cc
    mov dh, al                  ; ...DH its last
    mov ax, [si]
    inc ax
    call .cc
    mov ch, al                  ; CH = its first column...
    mov ax, [si+4]
    dec ax
    call .cc
    mov cl, al                  ; ...CL its last
.grow:
    mov al, dl
    mov ah, 16
    mul ah
    add al, ch
    mov bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, di                  ; BX = the row's first cell of them
    mov al, ch                  ; AL = the column
.gcell:
    cmp word [bx], 1
    je .gnext                   ; (crowded already: it walks them all)
    push bx
    cmp word [bx], 0
    je .gput
    inc bx
    inc bx
    cmp word [bx], 0
    je .gput
    inc bx
    inc bx
    cmp word [bx], 0
    je .gput
    pop bx
    mov word [bx], 1            ; a fourth: crowded
    jmp .gnext
.gput:
    mov [bx], si
    pop bx
.gnext:
    add bx, WA_LGC
    inc al
    cmp al, cl
    jbe .gcell
    inc dl
    cmp dl, dh
    jbe .grow
    pop cx
    add si, WA_LTE
    loop .ge
.gdone:
    ret
.cc:                            ; AX = a coordinate: AL = its cell, 0..15,
    or ax, ax                   ; the edge's beyond the room
    jns .ccp
    xor ax, ax
.ccp:
    cmp ax, WA_ROOM_X1 - 1
    jbe .ccin
    mov ax, WA_ROOM_X1 - 1
.ccin:
    push cx
    mov cl, 7
    shr ax, cl
    pop cx
    ret

; wa_flgmk - once an arena is loaded: wa_flg, wa_flcross's grid - for each
; 128-unit cell the floor lines (their offsets in wa_fl, in order) whose box
; widened by 16 reaches into it, so a point inside the widened box is in
; one of its cells (wa_lists' clamp: the edge's cells take beyond the room)
wa_flgmk:
    push es
    push ds
    pop es
    mov di, wa_flg
    mov ax, 0xFFFF
    mov cx, 256*WA_FLGC/2
    cld
    rep stosw
    pop es
    mov cx, [wa_nfl]
    jcxz .out
    mov si, wa_fl
.l:
    push cx
    mov ax, [si]                ; its columns...
    mov dx, [si+4]
    call .rng
    mov cx, ax
    mov ax, [si+2]              ; ...and rows
    mov dx, [si+6]
    call .rng
    xchg ax, bx                 ; BL = the first row, BH the last
.row:                           ; (CL = the first column, CH the last)
    mov al, 64
    mul bl
    add ax, wa_flg
    xchg ax, di                 ; DI = the row's first cell
    mov dl, cl                  ; DL = the column
.c:
    mov al, dl
    xor ah, ah
    shl ax, 1
    shl ax, 1
    push di
    add di, ax                  ; DI = the cell
    mov ah, WA_FLGC
.slot:
    cmp byte [di], 0xFF
    je .put
    inc di
    dec ah
    jnz .slot
    jmp short .full             ; (arenagen refuses a fifth)
.put:
    mov ax, si
    sub ax, wa_fl
    mov [di], al
.full:
    pop di
    inc dl
    cmp dl, ch
    jbe .c
    inc bl
    cmp bl, bh
    jbe .row
    pop cx
    add si, 8
    loop .l
.out:
    ret
.rng:                           ; AX, DX = a line's two ends on an axis:
    cmp ax, dx                  ; AL = the first cell of its widened
    jle .r                      ; extent, AH = the last
    xchg ax, dx
.r:
    sub ax, 16
    call wa_lists.cc
    push ax
    xchg ax, dx
    add ax, 16
    call wa_lists.cc
    pop dx
    mov ah, al
    mov al, dl
    ret

; wa_hat - SI = a box (wa_boxes), DI = its record, the square overlapping it
; ([wa_qx], [wa_qy] +- [wa_qr]): AX = its height under the square. Keeps
; every register but AX. A ramp's: h x num / len, num the square's reach along it from its
; low face (1 <= num <= len, the square overlapping it), len its extent
; (> 0: arenagen refuses an empty box) - so the quotient is at most h and
; the divide cannot fault (HEIGHTS-PLAN 9.5)
wa_hat:
    mov ax, [di]
    test byte [di+2], 3         ; (a flight's bits 4-7 are its treads: bit
    jnz .notsol                 ; 7 is a LINE's on a SOLID alone - the
    test byte [di+2], 0x80      ; ziggurat's 12 treads read as a wall)
    jnz .line                   ; A LINE (WIREARENA-ARENAS-PLAN 2): a wall to
    ret                         ; feet, its height to shots
.notsol:
    test byte [di+2], 2
    jnz .out                    ; (a lift: its height as it is)
    push bx
    push cx
    push dx
    mov bl, [di+2]
    and bl, 0x0C                ; the face it rises toward, x 4
    cmp bl, 0x04
    je .e
    cmp bl, 0x08
    je .n
    cmp bl, 0x0C
    je .w
    mov cx, [si+6]              ; S: num = y1 - max(qy0, y0)
    mov bx, [wa_qy]
    sub bx, [wa_qr]
    cmp bx, [si+2]
    jge .s1
    mov bx, [si+2]
.s1:
    sub cx, bx
    jmp .ylen
.n:
    mov cx, [wa_qy]             ; N: num = min(qy1, y1) - y0
    add cx, [wa_qr]
    cmp cx, [si+6]
    jle .n1
    mov cx, [si+6]
.n1:
    sub cx, [si+2]
.ylen:
    mov bx, [si+6]
    sub bx, [si+2]
    jmp .div
.w:
    mov cx, [si+4]              ; W: num = x1 - max(qx0, x0)
    mov bx, [wa_qx]
    sub bx, [wa_qr]
    cmp bx, [si]
    jge .w1
    mov bx, [si]
.w1:
    sub cx, bx
    jmp .xlen
.e:
    mov cx, [wa_qx]             ; E: num = min(qx1, x1) - x0
    add cx, [wa_qr]
    cmp cx, [si+4]
    jle .e1
    mov cx, [si+4]
.e1:
    sub cx, [si]
.xlen:
    mov bx, [si+4]
    sub bx, [si]
.div:
    mul cx                      ; DX:AX = h x num, < len x 65536
    div bx                      ; (num <= len: no fault)
    pop dx
    pop cx
    pop bx
    ret
.line:
    cmp byte [wa_hproj], 0      ; (a rocket, a grenade: over it)
    jne .out
    mov ax, WA_LINEW
.out:
    ret

; wa_fabs - DX = the feet's height: the floor they stood on plus the jump,
; whole units (wa_jz is 8.8 above wa_feet). Keeps every other register.
wa_fabs:
    xchg ax, dx                 ; (AX kept in DX the while)
    mov ax, [wa_jz]
    mov al, ah
    cbw
    add ax, [wa_feet]
    xchg ax, dx
    ret

; wa_hjump - the tick's vertical: the floor under the player (wa_flr, and
; its box), the jump, the step up and down, the fall, the landing; then the
; lift. On the ground (wa_jz = wa_jv = 0) the feet ARE wa_feet; in the air
; wa_feet is a base and wa_jz the height above it, 8.8, rebased whenever it
; would go below 0 so that a fall from any height stays in range.
wa_hjump:
    mov ax, [wa_px]             ; (the floor read again only where the
    mov bx, [wa_py]             ; player has moved, or stands on the lift,
    cmp ax, [wa_fpx]            ; which moves under them)
    jne .read
    cmp bx, [wa_fpy]
    jne .read
    mov cx, [wa_flrb]           ; (CX, not AX: .read takes AX = x and BX
    or cx, cx                   ; = y. With the record in AX it read the
    js .same                    ; floor at x = the record's offset - the
    mov si, cx                  ; ground - every other tick a rider stood
    mov dl, [wa_bxh+si+2]       ; still: HEIGHTS-PLAN 9.10) - a lift's,
    and dl, 3                   ; any of them (its kind's low bits)
    cmp dl, 2
    je .read
.same:
    mov ax, [wa_flr]
.have:
    mov cx, [wa_jz]
    or cx, [wa_jv]
    jnz .air
    test byte [wa_kbits+1], WA_K_SPACE >> 8
    jz .walk
    mov word [wa_jv], WA_JUMPV
    jmp .air
.read:
    mov [wa_fpx], ax
    mov [wa_fpy], bx
    mov si, wa_lowp
    call wa_floor
    mov [wa_flr], ax
    mov [wa_flrb], dx
    jmp .have
.walk:
    mov dx, [wa_feet]           ; the step: up (wa_collide let no more
    cmp ax, dx                  ; than a step through), or down a step -
    je wa_ltick                 ; past that, a fall (on the same floor as
    jg .onto                    ; before: the usual tick)
    sub dx, ax
    cmp dx, WA_STEPUP
    ja .air
.onto:
    mov [wa_feet], ax
    jmp wa_ltick
.air:
    mov ax, [wa_jv]
    add [wa_jz], ax
    sub word [wa_jv], WA_GRAV
    mov ax, [wa_jz]
    or ax, ax
    jns .chk
    mov dx, ax                  ; below the base: whole units into wa_feet
    mov cl, 8                   ; (rounded down), the fraction kept
    sar dx, cl
    add [wa_feet], dx
    and word [wa_jz], 0x00FF
.chk:
    cmp word [wa_jv], 0         ; still rising: no landing yet
    jg wa_ltick
    call wa_fabs
    cmp dx, [wa_flr]
    jl .land                    ; (below the floor: onto it)
    jg wa_ltick
    test word [wa_jz], 0x00FF   ; at it, no fraction over: down
    jnz wa_ltick
.land:
    mov ax, [wa_flr]
    mov [wa_feet], ax
    xor ax, ax
    mov [wa_jz], ax
    mov [wa_jv], ax
    ; (falls through)

; wa_ltick - the lift's tick (HEIGHTS-PLAN 3.2): someone standing on it
; (the player's floor is its box, on the ground) for WA_LWAIT ticks sends
; it up WA_LSPD a tick to its raised height; empty for WA_LHOLD it comes
; back down. Its height goes into wa_bxh, which wa_floor reads, and the
; renderer's copy every frame (wa_rframe). A rider's feet follow it by the
; step rule: WA_LSPD is under a step.
WA_LWAIT    equ 10              ; 1/3 s on it before it starts
WA_LHOLD    equ 45              ; 1.5 s empty before it returns
WA_LSPD     equ 5               ; units a tick: 144 in ~1 s
wa_ltick:                       ; EACH LIFT (ARENAS-PLAN 4.1), SI = its
    mov cx, [wa_nlift]          ; index x 2 into the tables
    jcxz .ret
    xor si, si
.each:
    push cx
    call wa_lift1
    pop cx
    inc si
    inc si
    loop .each
.ret:
    ret
wa_lift1:
    mov bx, [wa_lift+si]
    cmp byte [wa_lkon], 0       ; LINKED, the guest: the host's lift, as its
    je .ours                    ; SNAPs say, followed at twice its speed
    call wa_lkhost              ; (LINK-PLAN 11.3)
    jnz .ours
    mov ax, [wa_bxh+bx]
    mov dx, [wa_lklift+si]
    sub dx, ax
    jz .out
    cmp dx, WA_LSPD * 2
    jle .lo
    mov dx, WA_LSPD * 2
.lo:
    cmp dx, -WA_LSPD * 2
    jge .hi
    mov dx, -WA_LSPD * 2
.hi:
    add ax, dx
    jmp .set
.ours:
    cmp byte [wa_lbot+si], 0    ; a bot riding it (AI-PLAN 3.4), or the
    jne .on                     ; player on it, on the ground
    mov ax, [wa_jz]
    or ax, [wa_jv]
    jnz .empty
    cmp bx, [wa_flrb]
    jne .empty
.on:
    mov word [wa_loff+si], 0
    inc word [wa_lon+si]
    cmp word [wa_lon+si], WA_LWAIT
    jb .out
    mov ax, [wa_bxh+bx]
    add ax, WA_LSPD
    cmp ax, [wa_ltop+si]
    jle .set
    mov ax, [wa_ltop+si]
    jmp .set
.empty:
    mov word [wa_lon+si], 0
    inc word [wa_loff+si]
    cmp word [wa_loff+si], WA_LHOLD
    jb .out
    mov ax, [wa_bxh+bx]
    cmp ax, [wa_lrest+si]       ; (at rest already: the usual tick)
    je .out
    sub ax, WA_LSPD
    cmp ax, [wa_lrest+si]
    jge .set
    mov ax, [wa_lrest+si]
.set:
    mov [wa_bxh+bx], ax
.out:
    ret

; wa_lset0 - the lifts (if any) at their rest heights
wa_lset0:
    mov cx, [wa_nlift]
    jcxz .out
    xor si, si
.l:
    mov bx, [wa_lift+si]
    mov ax, [wa_lrest+si]
    mov [wa_bxh+bx], ax
    inc si
    inc si
    loop .l
.out:
    ret

; --- the bots: seeing, facing, shooting (docs/plans/completed/WIREARENA-AI-PLAN.md 4) -----
; Fast more than good (the owner): every fourth tick a bot looks along a
; 64-unit grid of the tall boxes to the player (wa_sight); seen, it ENGAGES -
; faces the player by a cross product's sign as it walks its edge, and after
; its reaction fires at its gap, a dice roll against its accuracy deciding
; the hit; lost, it HUNTS toward where it saw the player (wa_bnext's next
; hops), and after 5 s roams again. No ray, no square root, no arctangent.
WA_BROAM    equ 0
WA_BENG     equ 1
WA_BHUNT    equ 2
WA_BFETCH   equ 3               ; (B4: to a pickup's node)
WA_BDEADT   equ 240             ; a dead figure's ticks to its respawn: 8 s
WA_BBODYT   equ 90              ; ...its body's, lying: 3 s
WA_BSHP     equ 51              ; a bot's life with a shield: 51 in 256, 20%
WA_GRUDGE   equ 90              ; hit by the player: on them 3 s, whatever
WA_HEAR     equ 1024            ; the player's shot heard within (|dx|+|dy|)
WA_SGRID    equ 128             ; the sight grid's bytes (32 x 32 bits)
WA_BDMG     equ 20              ; a bot's round (its pistol: wave B4 arms it)
WA_BLOST    equ 19              ; looks (8 ticks each) a hunt lasts: 5 s
;               easy normal hard
wa_brx:     db 24,  15,    9    ; its reaction, ticks
wa_bgap:    db 30,  20,    14   ; between its shots
wa_bacc:    db 45,  60,    75   ; % at 4 m, the player standing

; wa_bthink - DI = a figure, CX = WA_NFIG less its index: its look (every
; fourth tick, its own: the sight walk and the bearing to the player), its
; state, its turn (every tick, toward the bearing it looked along) and its
; shot. Nothing in a bench or dead. Keeps DI
wa_bthink:
    cmp word [wa_bdet], 0       ; (a bench, or - its high byte, wa_lkon - a
    jne .rare                   ; linked match: neither, the usual tick)
.mine:
    cmp byte [di+F_DEAD], 0
    jne .r
    cmp di, [wa_blook]          ; its turn to look (wa_tick: every eighth
    je .look                    ; tick, the three bots' turns apart)
.acted:
    cmp byte [di+F_BST], WA_BENG
    je .eng
.r:
    ret
.rare:
    cmp byte [wa_bdet], 0
    jne .r
    call wa_lkremote            ; (linked: only the host thinks, for its
    je .r                       ; own bots)
    jmp .mine
.look:
    call wa_bidx                ; (its grudge runs down, 8 ticks a look)
    xchg ax, bx
    sub byte [wa_grudge+bx], 8
    jnc .ng
    mov byte [wa_grudge+bx], 0
.ng:
    mov ax, bx                  ; AX = tick + (WA_NFIG - index) x 3, a
    shl ax, 1                   ; multiple of 8 on its turn: its looks
    add ax, bx                  ; counted in bits 3 up
    neg ax
    add ax, WA_NFIG * 3
    add ax, [wa_tkn]
    cmp byte [di+F_BST], WA_BENG ; WHAT IT LOOKS AT (the owner, play-testing:
    jne .pick                   ; "pick a seen target"): engaged, its target,
    and al, 24                  ; but every fourth look first a nearer enemy
    cmp al, 24                  ; if there is one (and no grudge holds it)
    jne .tgt
    call wa_bclose
    jnc .tgt
    mov ah, [di+F_BTGT]         ; (its old one, should this be out of sight)
    mov [di+F_BTGT], bl
    push ax
    call wa_sight
    pop ax
    jc .c_s                     ; seen: it is the target now
    mov [di+F_BTGT], ah
    jmp .tgt
.pick:
    cmp byte [di+F_BST], WA_BROAM ; ROAMING it looks every OTHER turn, 16
    jne .pk                     ; ticks apart (the owner, pass 6: up to
    test al, 8                  ; 0.27 s later to notice - and a sight walk
    jnz .acted                  ; and a bearing fewer, ~350 clk a tick)...
    test al, 16                 ; ...the player one look and the next live
    jmp short .pk2              ; bot the next, by the looks it takes
.pk:
    test al, 8                  ; else the player one look and the next live
.pk2:
    jnz .bots                   ; bot the next - one sight walk a look, as
    mov byte [di+F_BTGT], 0     ; ever
    jmp .tgt
.bots:
    call wa_bnextt
.tgt:
    call wa_btpos               ; CF = 0: it is dead, or gone
    jc .there
    cmp byte [di+F_BST], WA_BENG
    jne .acted
    mov byte [di+F_BST], WA_BROAM ; (its target dead: it roams)
    jmp .acted
.there:
    call wa_sight               ; CF = 1: it sees the target
.c_s:                           ; (tools' stops)
    jnc .unseen
    call wa_bear                ; AX = the bearing to the player
.c_b:
    cmp byte [di+F_BST], WA_BROAM
    jne .seen                   ; (roaming: only within 90 degrees of its
    mov dx, ax                  ; facing)
    sub dx, [di+F_ANG]
    add dx, 256
    and dx, 1023
    cmp dx, 512
    jae .unseen
.seen:
    mov [di+F_BTA], ax
    mov byte [di+F_BSEE], 1
    mov ax, [wa_tx]             ; (the node nearest the target, the hunt's
    mov bx, [wa_ty]             ; goal should it lose them)
    call wa_bcell
    mov [di+F_BGOAL], al
    cmp byte [di+F_BST], WA_BENG
    je .acted
    mov byte [di+F_BST], WA_BENG ; first sight: its reaction starts
    mov byte [di+F_BGAP], 0
    mov bl, [wa_diff]
    xor bh, bh
    mov al, [wa_brx+bx]
    mov [di+F_BRX], al
    jmp .acted
.unseen:
    mov byte [di+F_BSEE], 0
    cmp byte [di+F_BST], WA_BENG
    jne .hunt
    mov byte [di+F_BST], WA_BHUNT ; lost: hunt where it was last seen
    mov byte [di+F_BGAP], WA_BLOST
    jmp .acted
.hunt:
    cmp byte [di+F_BST], WA_BHUNT
    jne .out
    dec byte [di+F_BGAP]
    jnz .out
    mov byte [di+F_BST], WA_BROAM
    ret
.eng:
    mov ax, [di+F_BTA]          ; THE TURN: toward the bearing, 48 a tick,
    cmp ax, [di+F_ANG]          ; onto it when within that (on it already:
    je .on                      ; the usual tick)
    sub ax, [di+F_ANG]
    and ax, 1023
    cmp ax, 512
    jb .pos
    sub ax, 1024
.pos:
    cmp ax, 48
    jg .left
    cmp ax, -48
    jl .right
    mov ax, [di+F_BTA]          ; on it: it may fire
    mov [di+F_ANG], ax
.on:
    cmp byte [di+F_BRX], 0
    je .ready
    dec byte [di+F_BRX]
    ret
.ready:
    cmp byte [di+F_BGAP], 0
    je .fire
    dec byte [di+F_BGAP]
    ret
.fire:
    cmp byte [di+F_BSEE], 0
    je .out
    call wa_btpos               ; (its target's place now - and it may have
    jnc .out                    ; died since the look)
    jmp wa_bshoot
.left:
    mov ax, 48
    jmp .turn
.right:
    mov ax, -48
.turn:
    add ax, [di+F_ANG]
    and ax, 1023
    mov [di+F_ANG], ax
    cmp byte [di+F_BGAP], 0
    je .out
    dec byte [di+F_BGAP]
.out:
    ret

; wa_bear - DI = a figure: AX = the bearing from it to the player, 0..1023
; (wa_ang's 1024 to the turn, x right, y up). An octant's ratio, x 64 by
; one divide, through a 65-byte arctangent table: no sines, no products.
; Keeps DI
wa_bear:
    mov ax, [wa_tx]
    sub ax, [di+F_X]
    mov bx, [wa_ty]
    sub bx, [di+F_Y]
    xor si, si                  ; SI: bit 0 dx < 0, bit 1 dy < 0
    or ax, ax
    jns .x
    neg ax
    or si, 1
.x:
    or bx, bx
    jns .y
    neg bx
    or si, 2
.y:
    xor cl, cl                  ; CL = 1: the steep octant (|dy| > |dx|)
    cmp bx, ax
    jbe .r
    xchg ax, bx
    inc cx
.r:
    or ax, ax
    jz .zero                    ; (on top of it: any bearing)
    xchg ax, bx                 ; r = small x 64 / large: 0..64
    mov dx, 64
    mul dx
    div bx
    mov bx, ax
    mov al, [wa_atan+bx]        ; 0..128: the angle in the octant
    xor ah, ah
    or cl, cl
    jz .q
    neg ax                      ; (steep: 256 less it)
    add ax, 256
.q:
    test si, 1
    jz .q2
    neg ax                      ; (x < 0: 512 less it)
    add ax, 512
.q2:
    test si, 2
    jz .q3
    neg ax                      ; (y < 0: the turn less it)
.q3:
    and ax, 1023
    ret
.zero:
    mov ax, [di+F_ANG]
    ret
wa_atan:                        ; atan(r / 64), 1024 to the turn, r 0..64
    db 0, 3, 5, 8, 10, 13, 15, 18, 20, 23, 25, 28, 30, 33, 35, 38, 40, 42, 45, 47, 49, 52, 54, 56, 58, 61, 63, 65, 67, 69, 71, 74, 76, 78, 80, 82, 84, 85, 87, 89, 91, 93, 95, 96, 98, 100, 102, 103, 105, 106, 108, 110, 111, 113, 114, 116, 117, 119, 120, 121, 123, 124, 125, 127, 128

; wa_btpos - DI = a bot: its target's place into wa_tx, wa_ty and its
; chest's height wa_tz; CF = 0 when there is none to look at (the player
; dead, the bot dead, or itself). Keeps all but AX, BX
wa_btpos:
    mov bl, [di+F_BTGT]
    ; (falls through)
wa_btpbl:                       ; BL = the target (0 the player, 1.. a bot)
    cmp byte [wa_gmode], 0      ; A TEAM MODE: a teammate is no target
    je .ff                      ; (CTF-PLAN 2)
    push ax
    push bx
    call wa_bidx
    inc ax
    xor bh, bh
    mov ah, [wa_tm+bx]          ; (the target's team)
    mov bl, al
    cmp ah, [wa_tm+bx]          ; (and this bot's)
    pop bx
    pop ax
    je .none
.ff:
    or bl, bl
    jnz .bot
    cmp byte [wa_pdead], 0
    jne .none
    mov ax, [wa_px]
    mov [wa_tx], ax
    mov ax, [wa_py]
    mov [wa_ty], ax
    mov ax, [wa_eye]
    sub ax, 40
    mov [wa_tz], ax
    stc
    ret
.bot:
    dec bl
    mov al, F_SIZE
    mul bl
    add ax, wa_figs
    cmp ax, di
    je .none
    xchg ax, bx
    cmp byte [bx+F_DEAD], 0
    jne .none
    mov ax, [bx+F_X]
    mov [wa_tx], ax
    mov ax, [bx+F_Y]
    mov [wa_ty], ax
    mov ax, [bx+F_Z]
    add ax, 60
    mov [wa_tz], ax
    stc
    ret
.none:
    clc
    ret

; wa_bnextt - DI = a bot: its target the next live bot after its last, in
; turn (none: the player). Keeps all but AX, BX
wa_bnextt:
    push cx
    mov cx, WA_NFIG
    mov bl, [di+F_BTGT]
.n:
    inc bl
    cmp bl, WA_NFIG
    jbe .k
    mov bl, 1
.k:
    mov [di+F_BTGT], bl
    push bx
    call wa_btpos
    pop bx
    jc .out
    loop .n
    mov byte [di+F_BTGT], 0
.out:
    pop cx
    ret

; wa_tdist - DI = a figure: AX = |dx| + |dy| to wa_tx, wa_ty. Keeps all
; but AX
wa_tdist:
    push bx
    mov ax, [wa_tx]
    sub ax, [di+F_X]
    call wa_abs
    mov bx, ax
    mov ax, [wa_ty]
    sub ax, [di+F_Y]
    call wa_abs
    add ax, bx
    pop bx
    ret

; wa_bgrudge - DI = a bot: ZF = 0 when it is on the player with a grudge
; still running. Keeps all but AX
wa_bgrudge:
    cmp byte [di+F_BTGT], 0
    jne .no
    push bx
    call wa_bidx
    xchg ax, bx
    cmp byte [wa_grudge+bx], 0
    pop bx
    ret
.no:
    cmp al, al                  ; (ZF = 1)
    ret

; wa_bclose - DI = an engaged bot: CF = 1 and BL = an enemy nearer than
; three quarters of its target's distance, the nearest (its place in wa_tx,
; wa_ty, wa_tz); CF = 0 when there is none, or a grudge holds it to the
; player. Keeps all but AX, BX
wa_bclose:
    call wa_bgrudge
    jnz .no
    push cx
    push dx
    call wa_btpos
    jnc .none                   ; (its own gone: the look sees to that)
    call wa_tdist
    mov dx, ax                  ; DX = 3/4 of it, then the nearest so far
    shr ax, 1
    shr ax, 1
    sub dx, ax
    mov cx, 0xFF00              ; CH = none yet, CL = the one tried
.c:
    cmp cl, [di+F_BTGT]
    je .n
    mov bl, cl
    call wa_btpbl
    jnc .n
    call wa_tdist
    cmp ax, dx
    jae .n
    mov dx, ax
    mov ch, cl
.n:
    inc cl
    cmp cl, WA_NFIG
    jbe .c
    cmp ch, 0xFF
    je .none
    mov bl, ch
    call wa_btpbl               ; (its place again, CF = 1)
    mov bl, ch
    pop dx
    pop cx
    ret
.none:
    pop dx
    pop cx
.no:
    clc
    ret

; wa_bstruck - DI = a bot just hurt by [wa_killer] (0 the player): the
; player's hit is a grudge of WA_GRUDGE ticks, and it turns on whoever shot
; it - unless it is fighting them already, or a grudge holds it to the
; player, or its own target is nearer. Keeps every register
wa_bstruck:
    push ax
    push bx
    push dx
    mov al, [wa_killer]
    or al, al
    jnz .k
    push ax
    call wa_bidx
    xchg ax, bx
    mov byte [wa_grudge+bx], WA_GRUDGE
    pop ax
.k:
    cmp byte [di+F_BST], WA_BENG
    jne .turn
    cmp al, [di+F_BTGT]
    je .out
    push ax
    call wa_bgrudge
    pop ax
    jnz .out
    push ax
    call wa_btpos
    pop ax
    jnc .turn                   ; (its own gone)
    push ax
    call wa_tdist
    xchg ax, dx                 ; DX = its own's distance
    pop ax
    mov bl, al
    push ax
    call wa_btpbl
    pop ax
    jnc .out
    push ax
    call wa_tdist
    cmp ax, dx
    pop ax
    jae .out                    ; (its own nearer: it stays on them)
.turn:
    mov [di+F_BTGT], al         ; its reaction first, and it fires once its
    mov byte [di+F_BST], WA_BENG ; next look sees them
    mov byte [di+F_BSEE], 0
    mov byte [di+F_BGAP], 0
    mov bl, [wa_diff]
    xor bh, bh
    mov al, [wa_brx+bx]
    mov [di+F_BRX], al
.out:
    pop dx
    pop bx
    pop ax
    ret

; wa_bhear - the player's shot: every live roaming bot within WA_HEAR of it
; hunts the node nearest the player (the owner's worry: bots that fight
; each other forget the player). Linked, the bots are the host's: the
; guest's shot is said in its next SNAP and heard there (LINK-PLAN 11.3).
; Keeps every register
wa_bhear:
    cmp byte [wa_bdet], 0
    jne wa_bheara.r
    cmp byte [wa_lkon], 0
    je .us
    call wa_lkhost
    jnz .us
    mov byte [wa_lkfired], 2    ; (the guest: its next two SNAPs say so -
                                ; one lost would lose the shot)
    ret
.us:
    push ax
    push bx
    push dx
    mov ax, [wa_px]
    mov bx, [wa_py]
    xor dl, dl
    call wa_bheara
    pop dx
    pop bx
    pop ax
    ret

; wa_bheara - AX, BX = where a shot was fired, DL = the shooter's code (0
; the player, 1 figure 0): our live roaming bots within WA_HEAR hunt there.
; Keeps every register
wa_bheara:
    push ax
    push bx
    push cx
    push di
    mov [wa_tx], ax
    mov [wa_ty], bx
    call wa_bcell
    mov ah, al                  ; AH = the shot's node
    mov di, wa_figs
    mov cx, WA_NFIG
.f:
    cmp byte [di+F_DEAD], 0
    jne .n
    call wa_lkremote            ; (linked: not the other machine's)
    je .n
    cmp byte [di+F_BST], WA_BROAM
    jne .n
    push ax
    call wa_tdist
    cmp ax, WA_HEAR
    pop ax
    jae .n
    mov [di+F_BTGT], dl
    mov byte [di+F_BST], WA_BHUNT
    mov byte [di+F_BGAP], WA_BLOST
    mov [di+F_BGOAL], ah
.n:
    add di, F_SIZE
    loop .f
    pop di
    pop cx
    pop bx
    pop ax
.r:
    ret

; wa_bcell - AX, BX = x, y: AL = the route node nearest the PVS cell there
; (arenagen's table). Keeps all but AX, BX
wa_bcell:
    push cx
    mov cl, 7
    shr ax, cl
    shr bx, cl
    mov cl, 4
    shl ax, cl
    add bx, ax
    add bx, [wa_gcp]
    mov al, [bx]
    pop cx
    ret

; wa_sight - DI = a figure: CF = 1 when the line from it to the player
; enters no tall cell of the sight grid, a 64-unit step at a time (AI-PLAN
; 4.1: wrong in 1.7% of lines, all of them grazing a corner, seen past it).
; Keeps DI
wa_sight:
    push di
    mov ax, [wa_tx]
    sub ax, [di+F_X]            ; AX = dx
    mov bx, [wa_ty]
    sub bx, [di+F_Y]            ; BX = dy
    mov cx, ax                  ; the steps: max(|dx|, |dy|) / 64 + 1
    or cx, cx
    jns .ax
    neg cx
.ax:
    mov dx, bx
    or dx, dx
    jns .ay
    neg dx
.ay:
    cmp cx, dx
    jae .mx
    mov cx, dx
.mx:
    push ax
    mov ax, cx
    mov cl, 6
    shr ax, cl
    inc ax
    mov cx, ax                  ; CX = the steps
    pop ax
    shl ax, 1                   ; quarter units: a 64-unit cell is a whole
    shl ax, 1                   ; high byte
    cwd
    idiv cx
    mov si, ax                  ; SI = x's step
    mov ax, bx
    shl ax, 1
    shl ax, 1
    cwd
    idiv cx
    mov bp, ax                  ; BP = y's
    mov ax, [di+F_X]
    mov dx, [di+F_Y]
    shl ax, 1
    shl ax, 1
    shl dx, 1
    shl dx, 1
    mov di, wa_sgrid
.step:
    add ax, si                  ; AH = the column, DH = the row
    add dx, bp
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov bx, [wa_colb+bx]        ; (the column x 32)
    add bl, dh
    cmp byte [di+bx], 0
    jne .wall
    loop .step
    pop di
    stc
    ret
.wall:
    pop di
    clc
    ret
wa_colb:                        ; a column's first cell in wa_sgrid
%assign wa_i 0
%rep 32
    dw wa_i*32
%assign wa_i wa_i+1
%endrep

; wa_sgrow - once an arena is loaded: its sight grid (a bit a cell, column
; by column, 4 bytes each) as a byte a cell (wa_sgrid), which wa_sight
; reads with no shift or mask
wa_sgrow:
    push es
    push ds
    pop es
    mov si, [wa_gtp]
    mov di, wa_sgrid
    mov cx, WA_SGRID
.b:
    lodsb
    mov dl, 8
.bit:
    mov ah, al
    and ah, 1
    xchg al, ah
    stosb
    xchg al, ah
    shr al, 1
    dec dl
    jnz .bit
    loop .b
    pop es
    ret

; wa_bshoot - DI = an engaged bot on target, its reaction and gap past: it
; fires - its cue (yellow, two ticks), its tone - and the dice say whether
; it hit: its difficulty's accuracy at 4 m, 5 points less each further 4 m
; (the axes' larger distance and half the smaller), a quarter less if the
; player is moving. Keeps DI
; THE BOTS' ARMS (AI-PLAN 3.3, B4), by weapon (0 the pistol .. 5 the railgun):
; a hitscan's damage (the shotgun's by range, below; 0 a projectile's), its
; gap (0: the difficulty's), its accuracy's adjustment, its tone
;            pistol shotgun chaingun grenades rockets railgun
wa_bwdmg:  db 20,    0,      8,       0,       0,      100
wa_bwgap:  db 0,     30,     4,       30,      36,     45
wa_bwacc:  db 0,     15,     -10,     0,       0,      0
wa_bwtone: dw 900,   220,    700,     300,     150,    2400

wa_bshoot:
    inc word [wa_bshots]        ; (the bots' shots and hits: the score's,
    mov byte [di+F_FLASH], 0x86 ; and tests/wabots.py's) - its cue, five
    mov bl, [di+F_BWPN]         ; ticks, so a 10 fps frame at 80 columns
    xor bh, bh                  ; cannot miss it
    mov [wa_bw], bl
    mov al, [wa_bwgap+bx]       ; its gap
    or al, al
    jnz .gap
    mov bl, [wa_diff]
    mov al, [wa_bgap+bx]
.gap:
    mov [di+F_BGAP], al
    mov bl, [wa_bw]
    shl bx, 1
    mov ax, [wa_bwtone+bx]
    mov cx, 1
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov bl, [wa_bw]
    xor bh, bh
    or bl, bl                   ; a round of its weapon: out, the pistol
    jz .armed
    dec byte [di+F_BAMMO]
    jnz .armed
    mov byte [di+F_BWPN], 0
.armed:
    cmp bl, 3
    je .gren
    cmp bl, 4
    je .rocket
    mov al, [wa_bwacc+bx]       ; THE HITSCANS: the roll's accuracy
    mov bl, [wa_diff]
    add al, [wa_bacc+bx]
    mov [wa_bacc2], al
    mov ax, [wa_tx]             ; the distance, near enough
    sub ax, [di+F_X]
    call wa_abs
    mov bx, ax
    mov ax, [wa_ty]
    sub ax, [di+F_Y]
    call wa_abs
    cmp ax, bx
    jae .big
    xchg ax, bx
.big:
    shr bx, 1
    add ax, bx                  ; AX = the distance, units
    mov [wa_bdist], ax
    mov bl, [wa_bacc2]          ; BL = the accuracy, %
    sub ax, 256
    jbe .near
    mov cl, 8                   ; 5 points a further 256 units (4 m)
    shr ax, cl
    mov ah, 5
    mul ah
    cmp al, bl
    jae .miss
    sub bl, al
.near:
    cmp byte [di+F_BTGT], 0     ; (a bot target: always moving)
    jne .moving
    test word [wa_kbits], WA_K_W | WA_K_S | WA_K_A | WA_K_D
    jz .roll                    ; (the player moving: a quarter less)
.moving:
    mov al, bl
    shr al, 1
    shr al, 1
    sub bl, al
.roll:
    call wa_rand8               ; 0..255, against the accuracy x 2.56
    mov ah, 0
    mov cx, 100
    mul cx
    cmp ah, bl
    jae .miss
    inc word [wa_bhits]
    call wa_bidx                ; (who: the score's kill if it is one)
    inc al
    mov [wa_hurtby], al
    mov [wa_killer], al
    cmp byte [di+F_BTGT], 0
    je .player
    mov bl, [wa_bw]             ; A BOT HIT: its damage, the shotgun's
    xor bh, bh                  ; flat 35 (its kill the red collapse); its
    mov al, [wa_bwdmg+bx]       ; kill the weapon's (the railgun's bolt)
    mov ah, [wa_wdie+bx]
    cmp bl, 1
    jne .bdmg
    mov ax, 0x0123
.bdmg:
    push di
    push ax
    mov al, [di+F_BTGT]
    dec ax
    mov cl, F_SIZE
    mul cl
    add ax, wa_figs
    mov di, ax
    pop ax
    call wa_hurt                ; (and it turns on the shooter: wa_bstruck)
    pop di
    ret
.player:
    mov ax, 120                 ; hit: the player hurts
    mov cx, 2
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov byte [wa_phit], 6
    mov bl, [wa_bw]
    xor bh, bh
    mov al, [wa_wdie+bx]        ; (our death as the other machines draw it:
    mov [wa_lkpkk], al          ; by this weapon)
    mov al, [wa_bwdmg+bx]
    cmp bl, 1
    jne .dmg
    mov al, 60                  ; the shotgun: 60 within 4 m, 35 within 8,
    cmp word [wa_bdist], 256    ; 15 past
    jbe .dmg
    mov al, 35
    cmp word [wa_bdist], 512
    jbe .dmg
    mov al, 15
.dmg:
    push di
    call wa_phurt
    pop di
.miss:
    ret
.gren:
    mov al, 1
    jmp wa_blaunch
.rocket:
    xor al, al
    jmp wa_blaunch

; wa_bidx - DI = a figure: AL = its index, 0.. Keeps all but AX
wa_bidx:
    xor ax, ax                  ; (three compares at most: a divide by the
    cmp di, wa_figs             ; record's size was 160 clk, and a rocket
    je .out                     ; asks it of every figure every tick)
    inc ax
    cmp di, wa_figs + F_SIZE
    je .out
    inc ax
.out:
    ret

; wa_blaunch - DI = a bot on target, AL = 0 a rocket, 1 a grenade: one
; from its chest along its facing, its climb set to arrive at the player's
; chest after the ticks its distance takes (a grenade's against gravity
; too), its owner the bot. Keeps DI
wa_blaunch:
    mov [wa_lt], al
    push di
    mov [wa_bfig], di
    mov si, wa_rks
    mov di, wa_rkg
.slot:
    cmp byte [si+WA_RKST], 0
    je .have
    add si, WA_RKSZ
    add di, WA_RKGSZ
    cmp si, wa_rks + WA_NRK*WA_RKSZ
    jb .slot
    pop di
    ret
.have:
    inc byte [wa_rkn]
    mov bx, [wa_bfig]
    mov al, [wa_lt]
    mov [si+WA_RKTYP], al
    mov byte [si+WA_RKST], WA_RKFLY
    mov ax, [bx+F_X]
    mov [si+WA_RKX], ax
    mov ax, [bx+F_Y]
    mov [si+WA_RKY], ax
    mov ax, [bx+F_Z]
    add ax, 90
    mov [si+WA_RKZ], ax
    xor ax, ax
    mov [di+RG_XF], ax
    mov [di+RG_YF], ax
    mov [di+RG_ZF], ax
    push di
    mov di, bx
    call wa_bidx
    pop di
    inc al
    mov [di+RG_OWN], al
    mov word [di+RG_LIFE], WA_RKLIFE
    mov ax, [bx+F_ANG]
    call wa_sincos              ; AX = sin, DX = cos (Q14)
    sar dx, 1                   ; a rocket's 32 a tick...
    sar ax, 1
    mov cl, 5
    cmp byte [wa_lt], 0
    je .v
    sar dx, 1                   ; ...a grenade's 16
    sar ax, 1
    inc cl
    mov word [di+RG_LIFE], WA_GFUSE
.v:
    mov [di+RG_VX], dx
    mov [di+RG_VY], ax
    mov ax, [wa_tx]             ; the ticks there: its distance / speed
    sub ax, [bx+F_X]
    call wa_abs
    mov dx, ax
    mov ax, [wa_ty]
    sub ax, [bx+F_Y]
    call wa_abs
    add ax, dx                  ; (the axes' sum: a little long - it
    shr ax, cl                  ; arrives a little low, at the legs)
    jnz .t
    inc ax
.t:
    mov cx, ax                  ; CX = ticks
    mov ax, [wa_tz]             ; the climb: to the target's chest
    sub ax, [si+WA_RKZ]
    cmp ax, 120                 ; (bounded: the quotient stays in 16 bits)
    jle .z1
    mov ax, 120
.z1:
    cmp ax, -120
    jge .z2
    mov ax, -120
.z2:
    mov dx, 256
    imul dx
    idiv cx                     ; 8.8 a tick
    cmp byte [wa_lt], 0
    je .vz
    push ax                     ; a grenade: and gravity's fall over them,
    mov ax, WA_GRAV / 2         ; GRAV / 2 x ticks, up
    mul cx
    pop dx
    add ax, dx
    cmp ax, 16000
    jle .vz
    mov ax, 16000
.vz:
    mov [di+RG_VZ], ax
    call wa_lkqfire             ; (linked: the host's bot's FIRE)
    pop di
    ret

; wa_bfetch - DI = a roaming bot at node BL: health below 40 and the
; health pack there, or the pistol alone and a weapon spawn there - FETCH
; it (its node the goal). Keeps all but AX
wa_bfetch:
    push bx
    push cx
    push dx
    push si
    mov si, [wa_gpkn]
    lodsw
    mov cx, ax
    jcxz .out
    xor dx, dx
.p:
    lodsb
    cmp al, 0xFF                ; (no node: no bot reaches it)
    je .n
    mov bh, al
    mov al, 1
    push cx
    mov cl, dl
    shl al, cl
    pop cx
    test [wa_pkon], al
    jz .n
    push bx
    mov bx, dx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ah, [wa_pks+bx+6]
    pop bx
    cmp ah, 5
    je .hp
    ja .n                       ; (the shield: no)
    cmp byte [di+F_BWPN], 0
    jne .n
    jmp .go
.hp:
    cmp byte [di+F_HP], 40
    jae .n
.go:
    mov [di+F_BGOAL], bh
    mov byte [di+F_BST], WA_BFETCH
    jmp .out
.n:
    inc dx
    loop .p
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; wa_btake - DI = a bot at node BL: a pickup there (arenagen's table: its
; node) that helps is taken - health below 100, the shield below 100, a
; weapon other than its own or low - and gone till its time is up. Keeps
; all but AX
wa_btake:
    push bx
    push cx
    push dx
    push si
    mov si, [wa_gpkn]
    lodsw
    mov cx, ax                  ; CX = the pickups
    or cx, cx
    jnz .some
    jmp .out
.some:
    xor dx, dx                  ; DX = the pickup's index
.p:
    lodsb
    cmp al, bl
    jne .n
    mov al, 1                   ; ...there: and present?
    push cx
    mov cl, dl
    shl al, cl
    pop cx
    test [wa_pkon], al
    jz .n
    push bx
    mov bx, dx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ah, [wa_pks+bx+6]       ; its kind
    pop bx
    cmp ah, 5
    jb .wpn
    je .hp
    cmp byte [di+F_SH], WA_FHP  ; (the shield)
    jae .n
    mov byte [di+F_SH], WA_FHP
    jmp .took
.hp:
    cmp byte [di+F_HP], WA_FHP
    jae .n
    mov byte [di+F_HP], WA_FHP
    jmp .took
.wpn:
    inc ah                      ; AH = the weapon
    push bx
    mov bl, ah
    xor bh, bh
    mov ah, [wa_wgive+bx]
    cmp bl, [di+F_BWPN]
    jne .new
    cmp [di+F_BAMMO], ah
    jae .full
.new:
    mov [di+F_BWPN], bl
    mov [di+F_BAMMO], ah
    pop bx
    jmp .took
.full:
    pop bx
    jmp .n
.took:
    mov al, 1                   ; gone till its time
    push cx
    mov cl, dl
    shl al, cl
    pop cx
    not al
    and [wa_pkon], al
    push bx
    mov bx, dx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov al, [wa_pks+bx+6]       ; its kind's respawn time
    xor ah, ah
    shr bx, 1
    shr bx, 1
    push bx
    mov bx, ax
    shl bx, 1
    mov ax, [wa_pkresp+bx]
    pop bx
    mov [wa_pkt+bx], ax
    call wa_lkpktaken           ; (linked: the host's bot's TAKE)
    pop bx
.n:
    inc dx
    dec cx
    jz .out
    jmp .p
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- routes (docs/plans/completed/WIREARENA-AI-PLAN.md 3) --------------------------------
; The arena's route graph (tools/arenagen.py graph_block), copied whole into
; wa_gph by wa_loadarena: a byte of nodes, of edges, a figure's home edge
; each; then a node a WA_GNSZ (x, y, its four edges out, 0xFF none), an edge
; a WA_GESZ (x, y, heading | 0x8000 climbing, steps - a patrol leg's row, at
; F_PATH + F_LEG + 2 - then its far node, its kind, its reverse). A figure
; walks an edge as it walked a leg; at its end it takes one of the node's.
WA_GNSZ     equ 8
WA_GESZ     equ 12
WA_GETO     equ 10              ; (from the edge's row, +2: its far node...
WA_GEKIND   equ 11              ; ...its kind - 0 walk, 1 onto the lift at
WA_GEREV    equ 12              ; rest, 2 off it raised, 3 a drop...)

; wa_bset - DI = a figure, AL = an edge: on it, at its start
; Keeps all but AX
wa_bset:
    push si
    mov ah, WA_GESZ
    mul ah
    mov [di+F_LEG], ax
    mov si, [di+F_PATH]
    add si, ax
    mov ax, [si+2]
    mov [di+F_X], ax
    mov ax, [si+4]
    mov [di+F_Y], ax
    mov ax, [si+6]
    and ax, 1023                ; (bit 15: the edge climbs, wa_figz)
    mov [di+F_HEAD], ax
    mov ax, [si+8]
    mov [di+F_STEP], ax
    mov al, [si+WA_GEKIND]      ; a lift's edge: wait for it (wa_bwait;
    mov ah, al                  ; its lift in bits 4-5)
    and ah, 3
    cmp ah, 3
    jne .k
    xor al, al
.k:
    mov [di+F_BWAIT], al
    pop si
    ret

; wa_bnext - DI = a figure at the end of its edge: the next, from the node
; it reached - in a bench (wa_bdet) the node's first, its home loop's next
; leg, so the dummies walk their patrols; else any but the way back, at
; random, or the way back from a dead end (AI-PLAN 3.2's ROAM). Keeps all
; but AX, BX, CX, DX
wa_bnext:
    push si
    mov si, [di+F_PATH]
    add si, [di+F_LEG]
    mov bl, [si+WA_GETO]
    mov ch, [si+WA_GEREV]       ; CH = the way back
    cmp byte [wa_bdet], 0       ; (a bench: its home loop, nothing else)
    jne .roam
    call wa_btake               ; a pickup here that helps (AI-PLAN 3.3)
    cmp byte [di+F_BST], WA_BROAM ; roaming: FETCH health below 40, or a
    jne .goal                   ; weapon with the pistol alone
    call wa_bfetch
.goal:
    call wc_bgoal               ; (CTF: the flags first - CTF-PLAN 6)
    cmp byte [di+F_BST], WA_BHUNT ; HUNTING or FETCHING (AI-PLAN 3.2): the
    jb .roam                    ; next hop toward the goal - there, it
    mov al, [di+F_BGOAL]        ; roams again
    cmp al, bl
    je .there
    mov dl, bl
    mov al, [wa_gph]
    mul dl
    add al, [di+F_BGOAL]
    adc ah, 0                   ; AX = the pair's: which of the node's ways
    mov cl, al                  ; out, two bits, four a byte (arenagen's
    shr ax, 1                   ; graph_block)
    shr ax, 1
    mov si, ax
    add si, [wa_ghp]
    mov al, [si]
    and cl, 3
    shl cl, 1
    shr al, cl
    and ax, 3
    xor bh, bh
    mov cl, 3
    shl bx, cl
    add bx, [wa_gnp]
    add bx, ax
    mov al, [bx+4]              ; ...its edge
    jmp .take
.there:
    mov byte [di+F_BST], WA_BROAM
.roam:
    xor bh, bh
    mov cl, 3
    shl bx, cl
    add bx, [wa_gnp]            ; BX = the node
    mov al, [bx+4]
    cmp byte [wa_bdet], 0
    jne .take
    xor cl, cl                  ; CL = the ways out but back
    xor si, si
.cnt:
    mov al, [bx+4+si]
    cmp al, 0xFF
    je .ncnt
    cmp al, ch
    je .ncnt
    inc cl
.ncnt:
    inc si
    cmp si, 4
    jb .cnt
    mov al, ch
    or cl, cl
    jz .take                    ; a dead end: back
    call wa_rand8               ; AL = 0..255
    mul cl
    mov dl, ah                  ; DL = which of them
    xor si, si
.pick:
    mov al, [bx+4+si]
    inc si
    cmp al, 0xFF
    je .pick
    cmp al, ch
    je .pick
    or dl, dl
    jz .take
    dec dl
    jmp .pick
.take:
    call wa_bset
    pop si
    ret

; wa_bwait - DI = a figure waiting at its edge's start: CF = 1 while it
; still must (kind 1: the lift not yet at rest; 2: it rides the lift, not
; yet at the top - a rider, wa_lbot). Keeps all but AX, BX
wa_bwait:
    push si
    mov bl, [di+F_BWAIT]        ; (its lift: the edge's kind byte's bits
    xor bh, bh                  ; 4-5, ARENAS-PLAN 4.1 - x 2, the tables'
    shr bx, 1                   ; index)
    shr bx, 1
    shr bx, 1
    and bx, 6
    mov si, bx
    mov bx, [wa_lift+si]
    mov ax, [wa_bxh+bx]
    test byte [di+F_BWAIT], 2
    jnz .up
    cmp ax, [wa_lrest+si]
    jne .wait
.go:
    mov byte [di+F_BWAIT], 0
    pop si
    clc
    ret
.up:
    inc byte [wa_lbot+si]
    cmp ax, [wa_ltop+si]
    je .go
.wait:
    pop si
    stc
    ret

; wa_rand8 - AL = a random byte (the LCG the weapon spawns roll; its high
; byte). Keeps all but AX
wa_rand8:
    push dx
    mov ax, [wa_seed]
    mov dx, 25173
    mul dx
    add ax, 13849
    mov [wa_seed], ax
    mov al, ah
    pop dx
    ret

; wa_figz - DI = a figure: F_Z, the floor under its feet (WA_FRAD), 0 on a
; flat arena. Its patrol is arenagen's to keep to steps (patrol checks).
; Keeps every register.
wa_figz:
    push ax
    push bx
    push cx
    push dx
    push si
    xor ax, ax
    cmp byte [wa_aflat], 0
    jne .set
    mov si, [di+F_PATH]         ; a leg arenagen found on the ground all
    add si, [di+F_LEG]          ; the way (its heading's bit 15 clear):
    test byte [si+7], 0x80      ; the ground, unread
    jz .set
    mov ax, [di+F_X]
    mov bx, [di+F_Y]
    mov si, wa_lowf
    call wa_floor
.set:
    mov [di+F_Z], ax
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "wasin.inc"
; wa_bpark - alone, the bots past the setup's count off the board for the
; match, and the tick's fast loop stopped short of them (wa_figend). The
; loop used to ask wa_lkabsent of every figure every tick; the pass that
; took the question out of it (b38a065a) left the uncounted bots running,
; and one bot asked for played as three. Keeps all but the flags
wa_bpark:
    push ax
    push di
    mov al, [wa_lknb]
    mov ah, F_SIZE
    mul ah
    add ax, wa_figs
    mov [wa_figend], ax
    mov di, ax
.p:
    cmp di, wa_figs + WA_NFIG*F_SIZE
    jae .out
    cmp byte [wa_lkon], 0       ; (linked: wa_lkmstart's, per figure)
    jne .out
    mov byte [di+F_DEAD], 250
    mov byte [di+F_NFR], 0
    add di, F_SIZE
    jmp .p
.out:
    pop di
    pop ax
    ret

; wa_figtick - DI = a figure: one 30 Hz step of its patrol and its animation
wa_figtick:
    cmp byte [wa_lkon], 0       ; (a bench alone: its three dummies, always)
    jne .count
    cmp byte [wa_bdet], 0
    jne .solo
.count:
    call wa_lkabsent            ; a bot the match has none of: off the
    je .off                     ; board (the setup's count, or LINK-PLAN 8)
    cmp byte [wa_lkon], 0       ; A LINKED MATCH (LINK-PLAN 6, 8): a figure
    je .solo                    ; the other machine owns drawn from its
    call wa_lkremote            ; record; the host's own bots its own
    jne .solo
    push si
    call wa_bidx
    mov ah, WR_SZ
    mul ah
    add ax, wa_lkr
    mov si, ax
    call wa_lkfig
    pop si
    ret
.off:
    mov byte [di+F_DEAD], 250
    mov byte [di+F_NFR], 0
    ret
.solo:
    cmp word [di+F_FLASH], 0    ; (F_FLASH and F_DEAD, one word: the usual
    jne .flash                  ; tick has neither)
.live:
    cmp byte [di+F_ANIM], WA_A_FLINCH
    je .anim                    ; a flinch stops it in its tracks
    cmp byte [di+F_BWAIT], 0    ; waiting for the lift (AI-PLAN 3.4): it
    je .go                      ; stands, on the lift a rider, its floor
    call wa_bwait               ; read as the lift moves it
    jnc .go
    call wa_figz
    jmp .anim
.go:
    mov ax, [di+F_HEAD]         ; a leg's step is the same every tick of
    or ah, 0x80                 ; it: worked out once per heading
    cmp ax, [di+F_VH]
    je .stepped
    mov [di+F_VH], ax
    mov ax, [di+F_HEAD]
    call wa_sincos
    push dx
    mov cx, WA_SPEED
    imul cx                     ; DX:AX = sin x 8, Q14
    call .q14                   ; -> AX units
    mov [di+F_VY], ax
    pop ax
    imul cx
    call .q14
    mov [di+F_VX], ax
.stepped:
    mov ax, [di+F_VY]
    add [di+F_Y], ax
    mov ax, [di+F_VX]
    add [di+F_X], ax
    dec word [di+F_STEP]
    jnz .turn
    call wa_bnext               ; the next edge: a branch at the node
.turn:
    cmp byte [wa_aflat], 0      ; the floor it now stands on (none on a
    jne .flatz                  ; flat arena: F_Z stays 0)
    mov si, [di+F_PATH]         ; on a leg arenagen found on the ground all
    add si, [di+F_LEG]          ; the way, the ground - wa_figz's own test,
    test byte [si+7], 0x80      ; without its call: most ticks of most legs
    jnz .zread
    mov word [di+F_Z], 0
    jmp short .flatz
.zread:
    call wa_figz
.flatz:
    cmp byte [di+F_BST], WA_BENG ; (engaged: it faces the player - wa_bthink)
    je .anim
    mov ax, [di+F_HEAD]         ; face the way it is going, 48 a tick at most
    sub ax, [di+F_ANG]
    jz .anim                    ; (it does, most ticks)
    and ax, 1023
    cmp ax, 512
    jb .ccw
    sub ax, 1024
    cmp ax, -48
    jge .set
    mov ax, -48
    jmp .set
.ccw:
    cmp ax, 48
    jle .set
    mov ax, 48
.set:
    add ax, [di+F_ANG]
    and ax, 1023
    mov [di+F_ANG], ax
.anim:
    inc word [di+F_PH]
    mov al, [di+F_NFR]
    xor ah, ah
    shl ax, 1
    shl ax, 1                   ; quarter keyframes in the cycle
    cmp [di+F_PH], ax
    jae .end
    ret
.end:
    cmp byte [di+F_LOOP], 0
    je .hold
    mov word [di+F_PH], 0
    ret
.hold:
    dec ax
    mov [di+F_PH], ax
    cmp byte [di+F_ANIM], WA_A_GIB
    jb .hf
    mov byte [di+F_NFR], 0      ; blown to bits, or dissolved: gone until it
    ret                         ; respawns (the renderer skips it, WEAPONS-
                                ; PLAN 2.3)
.hf:
    cmp byte [di+F_ANIM], WA_A_FLINCH
    jne .out
    jmp wa_runanim              ; the flinch is over: run again
.out:
    ret
.flash:
    mov al, [di+F_FLASH]        ; (0x80 + n: a bot's firing cue, yellow:
    or al, al                   ; its count runs to 0x80, which is 0)
    je .alive
    dec ax
    cmp al, 0x80
    jne .fl
    xor al, al
.fl:
    mov [di+F_FLASH], al
.alive:
    cmp byte [di+F_DEAD], 0
    je .live
    dec byte [di+F_DEAD]
    jz .resp
    call wa_zapgo               ; (the railgun's bolt over: the dissolve)
    cmp byte [di+F_DEAD], WA_BDEADT - WA_BBODYT
    jne .body                   ; (its body lies 3 s, then is gone - no
    mov byte [di+F_NFR], 0      ; drawing for the rest)
.body:
    cmp byte [di+F_NFR], 0      ; (gone: nothing to animate)
    jne .anim
    ret
.resp:
    jmp wa_brespawn
.q14:                           ; DX:AX Q14 -> AX, rounding toward -inf
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov ax, dx
    ret

; --- weapons (docs/plans/completed/WIREARENA-WEAPONS-PLAN.md 2.2, 6) --------------------
; Six, keys 1..6: the pistol (hitscan), the shotgun (a hitscan CONE, its
; damage the figure's inverse depth), the chaingun (hitscan while the
; trigger is held), the grenade launcher (a lob that bounces, on a fuse),
; the rocket launcher (a projectile, splash) and the railgun (hitscan that
; PIERCES every figure on the ray, and a beam). Each has a magazine, a gap
; between shots and a reload - by itself when it empties, or R at any time,
; topping the magazine up from the reserve the pickups fill. Times are 30 Hz
; ticks.
WA_NWPN     equ 6
WA_WCHG     equ 2               ; the chaingun: fires while the trigger is held
WA_FHP      equ 100             ; a figure's health, and the player's
WA_PDMG     equ 34              ; the pistol: three shots kill
WA_CDMG     equ 12              ; the chaingun: nine
WA_RDMG     equ 150             ; the railgun: through 50 of a shield
WA_RKLIFE   equ 120             ; ticks a rocket flies before it is gone
WA_GFUSE    equ 60              ; ticks a grenade lives before it goes off
WA_GLOB     equ 1536            ; a grenade's lob, 8.8 units a tick up
WA_GSTEP    equ 8               ; a rolling grenade rides a rise this tall
WA_SPLR     equ 128             ; the splash's reach, units (2 m)
WA_HITR     equ 24              ; a rocket inside a figure's square: direct
WA_RKEYE    equ 8               ; a rocket leaves this far below the eye
WA_LINEW    equ 4096            ; a LINE's height to feet (wa_hat): a wall
WA_PCOOL    equ 15              ; the pistol's gap: half a second
WA_BUCKT    equ 5               ; ...the first ticks of it, the pistol held
                                ; a row up (its kick: the refire you can see)
WA_ZAPT     equ 15              ; the railgun's kill: the bolt's ticks, then
                                ; the dissolve's keyframes (wa_zapgo)
WA_BEAMT    equ 6               ; ticks the railgun's beam shows (0.2 s: two or
                                ; three frames on the 8088)
;            pistol shotgun chaingun grenades rockets railgun
wa_wmag:  db 6,     3,      50,      4,       3,      1  ; magazine
wa_wrld:  db 30,    36,     40,      60,      75,     60 ; reload
wa_wcool: db WA_PCOOL, 15,  4,       15,      18,     24 ; between shots
wa_wgive: db 0,     6,      100,     8,       6,      5  ; a pickup gives
wa_wcap:  db 0,     24,     200,     24,      24,     15 ; the reserve's cap
wa_wdie:  db 0,     1,      0,       2,       2,      3  ; its kill: the fall,
                                ; the red collapse, the bits, the bolt
; (the owner, 2026-11-14: the pistol was "a better chain gun than the chain
; gun" - half a second between its shots and twice its reload; the chaingun
; half as much again in its magazine and its pickups)
wa_pkresp:  dw 300, 300, 300, 300, 300, 600, 900 ; a pickup's respawn, by
                                ; kind (s 10 the weapons, 20 health, 30
                                ; the shield)

; wa_arm0 - the player as they start: the pistol, loaded, nothing else; full
; health, no shield. Keeps all but AX
wa_arm0:
    xor ax, ax
    mov [wa_wpn], al
    mov [wa_rld], al
    mov [wa_cool], al
    mov [wa_sh], ax
    mov [wa_beam], al
    mov [wa_res], ax
    mov [wa_res+2], ax
    mov [wa_res+4], ax
    mov [wa_mag], ax
    mov [wa_mag+2], ax
    mov [wa_mag+4], ax
    mov byte [wa_mag], 6
    mov byte [wa_own], 1
    mov word [wa_hp], WA_FHP
    mov byte [wa_ovd], 1
    ret

; wa_pkreset - every pickup there, every timer stopped, no rocket in flight;
; each weapon spawn picks its weapon (wa_pkroll: at random, or with
; [wa_pkdet] set - the benches - its first choice). Clobbers AX, CX, DI
wa_pkreset:
    mov cx, [wa_npk]
    mov al, 1
    shl al, cl
    dec al
    mov [wa_pkon], al
    push es
    push ds
    pop es
    xor ax, ax
    mov di, wa_pkt
    mov cx, WA_MPK
    rep stosw
    mov di, wa_rks
    mov cx, WA_NRK*WA_RKSZ/2
    rep stosw
    mov [wa_rkn], al
    pop es
    push si
    mov si, wa_pks
    mov cx, [wa_npk]
    jcxz .rolled
.roll:
    call wa_pkroll
    add si, WA_PKSZ
    loop .roll
.rolled:
    pop si
    ret

; wa_pkroll - SI = a pickup's record (ours): a weapon SPAWN (its choices
; byte, a bit a kind) takes one of its choices - at random, or the first
; with [wa_pkdet] - into its kind byte here and in the renderer's copy
; (wafb.inc: the one ARENA byte G writes). Keeps all but AX
wa_pkroll:
    mov ah, [si+7]
    or ah, ah
    jz .out
    push bx
    push cx
    push dx
    xor cx, cx                  ; CL = its choices, counted
    mov al, ah
.cnt:
    shr al, 1
    adc cl, 0
    or al, al
    jnz .cnt
    xor dl, dl                  ; DL = the one taken: the first...
    cmp byte [wa_pkdet], 0
    jne .pick
    call wa_rand8               ; ...or one at random (an LCG, seeded from
    mul cl                      ; the clock at the match's start)
    mov dl, ah                  ; (its high byte x choices / 256)
.pick:
    mov ah, [si+7]
    xor al, al                  ; AL = the kind
.bit:
    shr ah, 1
    jnc .nb
    or dl, dl
    jz .have
    dec dl
.nb:
    inc al
    jmp .bit
.have:
    mov [si+6], al
    push es
    mov es, [wa_rds]
    mov [es:si+WA_RAT(wa_pks)-wa_pks+6], al
    pop es
    pop dx
    pop cx
    pop bx
.out:
    ret

; wa_wsel - AL = a weapon: chosen, if the player has it and it has anything
; to fire (the pistol always). Clobbers AX, BX, CX
wa_wsel:
    cmp al, [wa_wpn]
    je .out
    mov cl, al
    mov bl, 1
    shl bl, cl
    test [wa_own], bl
    jz .out
    xor bx, bx
    mov bl, al
    or bl, bl
    jz wa_wset
    mov ah, [wa_mag+bx]
    or ah, [wa_res+bx]
    jz .out
    jmp wa_wset
.out:
    ret

; wa_wcycle - AL = -1 or +1 (Q, E): the weapon before or after the one
; held, round, the first the player has and can fire (wa_wsel's test).
; Clobbers AX, BX, CX, DX
wa_wcycle:
    mov dl, al
    mov dh, [wa_wpn]
    mov ch, WA_NWPN - 1
.n:
    add dh, dl
    jns .lo
    mov dh, WA_NWPN - 1
.lo:
    cmp dh, WA_NWPN
    jb .t
    xor dh, dh
.t:
    mov al, dh
    push dx
    push cx
    call wa_wsel
    pop cx
    pop dx
    cmp dh, [wa_wpn]
    je .out                     ; (taken)
    dec ch
    jnz .n
.out:
    ret

; wa_msset - AL = -1 or +1 ([, ]): the mouse's sensitivity a level down or
; up, -4..4 about the first one (the factors the game shipped with), and
; "Mouse sensitivity N" on the HUD's message (1..9). Clobbers AX, SI
wa_msset:
    add al, [wa_msens]
    cmp al, -4
    jl .out
    cmp al, 4
    jg .out
    mov [wa_msens], al
    add al, '5'
    mov si, wa_hk_mouse
    jmp wa_hudsay
.out:
    ret
;           1   2   3   4   5   6    7    8    9   (5 the first: 96, 64)
wa_msyaw:   dw 40, 52, 64, 80, 96, 116, 140, 168, 200 ; yaw x count
wa_mspit:   dw 27, 35, 43, 53, 64, 77,  93,  112, 133 ; pitch x count / 64

; wa_wset - AL = a weapon, held now: any reload of the last is dropped, a
; short gap before its first shot, and one that is empty starts to reload
wa_wset:
    mov [wa_wpn], al
    mov byte [wa_rld], 0
    mov byte [wa_cool], 6
    mov byte [wa_ovd], 1
    ; (falls through)

; wa_wready - the weapon held, if its magazine is empty: reloads, or with
; nothing in reserve, the pistol instead. Clobbers AX, BX
wa_wready:
    mov bl, [wa_wpn]
    xor bh, bh
    cmp byte [wa_mag+bx], 0
    jne .out
    or bl, bl
    jz .rl                      ; (the pistol's reserve is endless)
    cmp byte [wa_res+bx], 0
    jne .rl
    xor al, al                  ; nothing left: the pistol
    jmp wa_wset
.rl:
    mov al, [wa_wrld+bx]
    mov [wa_rld], al
    mov byte [wa_ovd], 1
.out:
    ret

; wa_wtick - a tick of the weapon: the railgun's beam, the gap between
; shots, a reload - which at its end tops the magazine up from the reserve
; - and the chaingun, which fires while the trigger is held
wa_wtick:
    cmp word [wa_phit], 0       ; (the heart's flash or - its high byte,
    jne .hb                     ; wa_beam - the beam: neither, the usual
.b:                             ; tick; and no reload or gap, wa_rld and
    cmp word [wa_rld], 0        ; its high byte wa_cool)
    je .auto
    cmp byte [wa_cool], 0
    je .c
    dec byte [wa_cool]
    cmp byte [wa_cool], WA_PCOOL - WA_BUCKT ; (the pistol's kick over: the
    jne .c                      ; overlay, its row back)
    cmp byte [wa_wpn], 0
    jne .c
    mov byte [wa_ovd], 1
.c:
    cmp byte [wa_rld], 0
    je .auto
    dec byte [wa_rld]
    jnz .out
    mov bl, [wa_wpn]
    xor bh, bh
    mov al, [wa_wmag+bx]
    or bl, bl
    jz .fill                    ; (the pistol: from nowhere)
    sub al, [wa_mag+bx]         ; AL = the room in the magazine
    cmp al, [wa_res+bx]
    jbe .take
    mov al, [wa_res+bx]
.take:
    sub [wa_res+bx], al
    add al, [wa_mag+bx]
.fill:
    mov [wa_mag+bx], al
    mov byte [wa_ovd], 1
.out:
    ret
.auto:
    cmp byte [wa_wpn], WA_WCHG  ; the chaingun, the trigger held, its gap
    jne .out                    ; past: another round
    cmp byte [wa_trig], 0
    je .out
    cmp byte [wa_cool], 0
    jne .out
%ifdef WA_BENCH
    cmp byte [wa_pbon], 0
    jne .out
%endif
    cmp byte [wa_pdead], 0
    jne .out
    jmp wa_fire
.hb:
    cmp byte [wa_phit], 0       ; the heart's flash, a hit taken
    je .h
    dec byte [wa_phit]
    jnz .h
    mov byte [wa_ovd], 1
.h:
    cmp byte [wa_beam], 0
    je .b
    dec byte [wa_beam]
    jnz .b
    mov byte [wa_ovd], 1        ; (the beam gone from the overlay)
    jmp .b

; wa_reload - R: the weapon held reloads now, if its magazine is not full
; and it has a reserve (the pistol's is endless) - not again while it does
wa_reload:
    cmp byte [wa_rld], 0
    jne .out
    mov bl, [wa_wpn]
    xor bh, bh
    mov al, [wa_mag+bx]
    cmp al, [wa_wmag+bx]
    jae .out
    or bl, bl
    jz .go
    cmp byte [wa_res+bx], 0
    je .out
.go:
    mov al, [wa_wrld+bx]
    mov [wa_rld], al
    mov byte [wa_ovd], 1
.out:
    ret

; wa_fire - the trigger, pressed (one shot a press): the weapon held fires if
; it is loaded, not reloading and past its gap; an empty one reloads
wa_fire:
    mov byte [wa_pprot], 0      ; (a shot ends the spawn's protection)
    mov byte [wa_killer], 0     ; (the player's shots: the player's kills)
    cmp byte [wa_rld], 0
    jne .out
    cmp byte [wa_cool], 0
    jne .out
    mov bl, [wa_wpn]
    xor bh, bh
    cmp byte [wa_mag+bx], 0
    je wa_wready
    dec byte [wa_mag+bx]
    mov al, [wa_wcool+bx]
    mov [wa_cool], al
    mov byte [wa_ovd], 1
    mov byte [wa_mflash], 2
    call wa_bhear               ; (heard: the bots near enough come)
    push bx
    shl bx, 1
    call [wa_wfire+bx]
    pop bx
    cmp byte [wa_mag+bx], 0
    je wa_wready                ; empty: reload, or back to the pistol
.out:
    ret
wa_wfire:   dw wa_fpis, wa_fshg, wa_fchg, wa_fgren, wa_frkt, wa_frail

; wa_trace - CX, DX = the cone (0, 0: the ray): TRACE in the renderer part,
; against the figures as the last frame drew them, and one respawned since
; then never hit where its body lay. AX = the nearest hit (its offset in
; wa_figs, or -1), DX = the hits (WRV_TRACE). ES = DS out
wa_trace:
    push cx
    push dx
    call wa_rframe              ; the figures as they stand (F_DEAD)...
%assign wa_i 0
%rep WA_NFIG
    cmp word [wa_figs + wa_i*F_SIZE + F_PZ], 0
    jne .drawn%[wa_i]           ; ...and one respawned since the last frame
    mov word [es:WA_RAT(wa_figs) + wa_i*F_SIZE + F_PZ], 0 ; was not drawn
.drawn%[wa_i]:                  ; where the part's projection says
%assign wa_i wa_i+1
%endrep
    pop dx
    pop cx
    mov bx, WRV_TRACE
    call wa_rcall
    push ds
    pop es
    ret

; wa_fpis - the pistol: the crosshair's ray, the nearest figure on it
wa_fpis:
    mov ax, 1200
    mov cx, 1
    mov dl, 0x40
    call OSAPI_SND_TONE
    xor cx, cx
    xor dx, dx
    call wa_trace
    or ax, ax
    js .out
    add ax, wa_figs
    mov di, ax
    mov ax, WA_PDMG             ; (AH 0: the pistol's kill)
    jmp wa_hurt
.out:
    ret

; wa_fchg - the chaingun: a round down the crosshair's ray, a little wider
; (W/32 sub-columns, a half-row), the nearest figure on it
wa_fchg:
    mov ax, 700
    mov cx, 1
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov cx, [wa_w]
    mov dx, cx
    mov cl, 5
    shr dx, cl
    mov cx, dx
    mov dx, 1
    call wa_trace
    or ax, ax
    js .out
    add ax, wa_figs
    mov di, ax
    mov ax, WA_CDMG             ; (AH 0: the fall)
    jmp wa_hurt
.out:
    ret

; wa_frail - the railgun: the crosshair's ray through EVERY figure on it
; (TRACE's mask; each nearer than the wall behind it), and its beam
wa_frail:
    mov ax, 2400
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov byte [wa_beam], WA_BEAMT
    xor cx, cx
    xor dx, dx
    call wa_trace
    mov di, wa_figs
    mov cx, WA_NFIG
.f:
    shr dl, 1
    jnc .n
    push cx
    push dx
    mov ax, 3 << 8 | WA_RDMG    ; (AH 3: the bolt, then the dissolve)
    call wa_hurt
    pop dx
    pop cx
.n:
    add di, F_SIZE
    loop .f
    ret

; wa_fshg - the shotgun: a cone W/8 sub-columns and 3 half-rows either side
; of the crosshair (about 5 degrees at any width and view), every figure in
; it hit, by its inverse depth: / 32 directly (128 at 2 m - a kill close
; up), / 64 when only the cone reaches it
wa_fshg:
    mov ax, 220
    mov cx, 2
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov cx, [wa_w]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov dx, 3
    call wa_trace
    mov di, wa_figs
    mov bx, WA_RAT(wa_figs) + F_PIZ
    mov cx, WA_NFIG
.f:
    shr dl, 1
    jnc .n
    push cx
    push dx
    push bx
    mov es, [wa_rds]
    mov ax, [es:bx]             ; its inverse depth, as the part drew it
    push ds
    pop es
    mov cl, 5
    test dh, 1
    jnz .sh
    inc cl                      ; (the cone alone: half)
.sh:
    shr ax, cl
    or ah, ah
    jz .lo
    mov al, 255
.lo:
    or al, al
    jnz .d
    inc ax
.d:
    mov ah, 1                   ; (the shotgun's kill)
    call wa_hurt
    pop bx
    pop dx
    pop cx
.n:
    shr dh, 1
    add di, F_SIZE
    add bx, F_SIZE
    loop .f
    ret

; wa_frkt - the rocket launcher: a rocket from below the eye, along the
; crosshair's ray - the yaw's direction at 32 units a tick, and the pitch's
; shear as its climb (pitch x 2^19 / kyh, 8.8 a tick)
wa_frkt:
    mov byte [wa_lt], 0
    mov ax, 150
    jmp wa_launch

; wa_fgren - the grenade launcher: a grenade along the same ray at half the
; rocket's speed, and lobbed (WA_GLOB more climb), on WA_GFUSE's fuse
wa_fgren:
    mov byte [wa_lt], 1
    mov ax, 300
    ; (falls through)

; wa_launch - AX = its tone, [wa_lt] its type: a rocket (0) or a grenade
; (1) into a free slot
wa_launch:
    mov cx, 3
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov si, wa_rks
    mov di, wa_rkg
.slot:
    cmp byte [si+WA_RKST], 0
    je .have
    add si, WA_RKSZ
    add di, WA_RKGSZ
    cmp si, wa_rks + WA_NRK*WA_RKSZ
    jb .slot
    ret                         ; (all in flight: rare - the shot is spent)
.have:
    inc byte [wa_rkn]
    mov al, [wa_lt]
    mov [si+WA_RKTYP], al
    mov ax, [wa_px]
    mov [si+WA_RKX], ax
    mov ax, [wa_py]
    mov [si+WA_RKY], ax
    mov ax, [wa_eye]
    sub ax, WA_RKEYE
    mov [si+WA_RKZ], ax
    mov byte [si+WA_RKST], WA_RKFLY
    xor ax, ax
    mov [di+RG_XF], ax
    mov [di+RG_YF], ax
    mov [di+RG_ZF], ax
    mov word [di+RG_LIFE], WA_RKLIFE
    mov byte [di+RG_OWN], 0     ; (the player's)
    mov ax, [wa_ang]
    mov cl, 6
    shr ax, cl
    call wa_sincos              ; AX = sin, DX = cos, Q14
    sar dx, 1                   ; Q14 / 2 = 8.8 x 32
    sar ax, 1
    cmp byte [wa_lt], 0
    je .v
    sar dx, 1                   ; (a grenade: 16)
    sar ax, 1
    mov word [di+RG_LIFE], WA_GFUSE
.v:
    mov [di+RG_VX], dx
    mov [di+RG_VY], ax
    mov dx, [wa_pitch]          ; the climb: (pitch << 19) / kyh - a
    shl dx, 1                   ; grenade's half that, and its lob
    shl dx, 1
    cmp byte [wa_lt], 0
    jne .vz
    shl dx, 1
.vz:
    xor ax, ax
    idiv word [wa_kyh]          ; (|pitch| <= 290: at most ~18,000)
    cmp byte [wa_lt], 0
    je .vzd
    add ax, WA_GLOB
.vzd:
    mov [di+RG_VZ], ax
    jmp wa_lkqfire              ; (linked: FIRE)

; wa_hurt - DI = a figure, AL = damage, AH = the weapon's kill (0..2): its
; shield takes it first, then its health; at 0 it dies, by that weapon's
; death. Clobbers AX, CX
wa_hurt:
    cmp byte [di+F_DEAD], 0
    jne .out
    cmp byte [wa_gmode], 0      ; A TEAM MODE: no hurt between teammates
    je .ff                      ; (CTF-PLAN 2) - [wa_killer] the shooter
    push ax
    push bx
    call wa_bidx
    inc ax
    mov bl, al
    xor bh, bh
    mov al, [wa_tm+bx]
    mov bl, [wa_killer]
    cmp al, [wa_tm+bx]
    pop bx
    pop ax
    je .out
.ff:
    call wa_lkremote            ; THE OTHER MACHINE'S (LINK-PLAN 3): the
    jne .here                   ; shooter judges - the damage on the wire,
    push ax                     ; the flinch here, no health changed: a
    push bx                     ; player's in its peer's damage word, with
    push dx                     ; who dealt it (NET-PLAN 4)
    mov dx, ax
    call wa_figslot
    cmp al, [wa_lknp]
    jae .rbot
    call wa_slotpeer
    jc .rx
    xor dh, dh
    add [bx+WP_DOUT], dx
    mov al, [wa_killer]
    call wa_lkcanon
    mov [bx+WP_ATTR], al
    jmp .rx
.rbot:
    sub al, [wa_lknp]           ; (a guest's hit on a host's bot: that
    mov bl, al                  ; bot's damage byte, by its place)
    xor bh, bh
    add [wa_lkbd+bx], dl
.rx:
    pop dx
    pop bx
    pop ax
.rfl:
    mov byte [di+F_FLASH], 5
    jmp .flinch
.here:
    call wa_bstruck             ; (AI-PLAN 8.6: it turns on whoever shot it)
    inc word [wa_hits]
    mov byte [di+F_FLASH], 5
    mov cl, [di+F_SH]
    sub cl, al
    jb .through
    mov [di+F_SH], cl           ; the shield took it all
    jmp .flinch
.through:
    neg cl                      ; CL = what the shield did not take
    mov byte [di+F_SH], 0
    cmp cl, [di+F_HP]
    jae wa_kill
    sub [di+F_HP], cl
.flinch:
    mov byte [di+F_ANIM], WA_A_FLINCH
    mov byte [di+F_NFR], WA_N_FLINCH
    mov byte [di+F_LOOP], 0
    mov word [di+F_PH], 0
.out:
    ret

; wa_kill - DI = a figure, AH = the weapon (0..2): dead, by its death - the
; pistol's fall, the shotgun's red collapse (F_FLASH held as long as F_DEAD,
; which the renderer draws in A_FLASH), the rocket's bits (GIB, then gone:
; wa_figtick). Keeps all but AX
wa_kill:
    push ax                     ; the score (AI-PLAN 5): the player's kill,
    push cx                     ; the bot's death
    mov al, [wa_killer]         ; (whoever's weapon: the player's, or a
    xor ah, ah                  ; bot's projectile)
    xchg ax, si
    inc byte [wa_kills+si]
    xchg ax, si
    call wa_bidx
    xor ah, ah
    xchg ax, si
    inc byte [wa_deaths+1+si]
    xchg ax, si
    cmp byte [wa_lkon], 0       ; (alone: the HUD says who; linked, the
    jne .lk                     ; board's count does - wa_lkcount)
    inc ax
    mov ah, al
    mov al, [wa_killer]
    call wa_hudkill
.lk:
    pop cx
    pop ax
    ; (falls through)
; wa_dieanim - DI = a figure, AH = its kill: dead by it (no score: the
; other player's death, walink.inc, comes this way too). Keeps all but AX
wa_dieanim:
    cmp byte [wa_lkon], 0       ; (linked, a bot of OURS - the host's: who
    je .an                      ; killed it. A figure the other machine
    call wa_lkremote            ; owns is counted there; our player's
    je .an                      ; death is counted in wa_phurt)
    call wa_lkbdeath
.an:
    mov [di+F_KILL], ah
    mov byte [di+F_SH], 0
    mov byte [di+F_HP], 0
    mov byte [di+F_LOOP], 0
    mov word [di+F_PH], 0
    mov byte [di+F_DEAD], WA_BDEADT ; eight seconds before it respawns (the
                                ; owner, play-testing: three let the bots
                                ; back before the player could cross)
    mov al, WA_A_DEATH
    mov ah, WA_N_DEATH
    cmp byte [di+F_KILL], 1
    jb .set
    je .shg
    mov al, WA_A_GIB
    mov ah, WA_N_GIB
    cmp byte [di+F_KILL], 3
    jb .set
    mov al, WA_A_ZAP            ; the railgun's: struck, shaking (it loops),
    mov ah, WA_N_ZAP            ; then dissolving (wa_zapgo)
    mov byte [di+F_LOOP], 1
    jmp short .set
.shg:
    mov byte [di+F_FLASH], 90   ; the shotgun's: red all the while
    mov al, WA_A_COLLAPSE
    mov ah, WA_N_COLLAPSE
.set:
    mov [di+F_ANIM], al
    mov [di+F_NFR], ah
    ret

; wa_zapgo - DI = a dead figure: the railgun's bolt over (WA_ZAPT ticks
; struck), it dissolves - head to toe, as its joints alone. Keeps every
; register
wa_zapgo:
    cmp byte [di+F_ANIM], WA_A_ZAP
    jne .out
    cmp byte [di+F_DEAD], WA_BDEADT - WA_ZAPT
    ja .out
    mov byte [di+F_ANIM], WA_A_DISSOLVE
    mov byte [di+F_NFR], WA_N_DISSOLVE
    mov byte [di+F_LOOP], 0
    mov word [di+F_PH], 0
.out:
    ret

; wa_phurt - AL = damage to the player (their own rockets' splash, until
; there are other players): the shield first, then the health; at 0 they
; start again at the arena's start with the pistol alone. Clobbers AX, BX
wa_phurt:
    cmp byte [wa_pprot], 0      ; SPAWN PROTECTION: nothing hurts
    jne wa_hurt.out
    cmp byte [wa_gmode], 0      ; A TEAM MODE: a teammate's splash hurts
    je .ff                      ; nothing (CTF-PLAN 2); our own still does
    cmp byte [wa_hurtby], 0
    je .ff
    push bx
    mov bl, [wa_hurtby]
    xor bh, bh
    mov bl, [wa_tm+bx]
    cmp bl, [wa_tm]
    pop bx
    jne .ff
    ret
.ff:
    call wa_hitdir              ; (the hit's direction: a prototype)
    xor ah, ah
    mov byte [wa_ovd], 1
    sub [wa_sh], ax
    jae .out
    mov ax, [wa_sh]             ; (what the shield did not take, negative)
    mov word [wa_sh], 0
    add [wa_hp], ax
    jg .out
    mov word [wa_hp], 0
    cmp byte [wa_pdead], 0      ; DEAD (AI-PLAN 5): once
    jne .out
    mov byte [wa_pdead], WA_PDEADT
    mov word [wa_peye], WA_EYE  ; (the view tips to the floor)
    inc byte [wa_deaths]        ; the score: a death; a kill to whoever
    mov bl, [wa_hurtby]         ; did it, or a suicide less
    xor bh, bh
    or bl, bl
    jnz .credit
    cmp byte [wa_kills], 0
    je .tone
    dec byte [wa_kills]
    jmp .tone
.credit:
    inc byte [wa_kills+bx]
.tone:
    cmp byte [wa_lkon], 0       ; (alone: the HUD says who)
    jne .lkd
    mov al, bl
    xor ah, ah
    call wa_hudkill
.lkd:
    call wa_lkdeath             ; (linked: the deaths by cause, the board)
    mov ax, 90
    mov cx, 6
    mov dl, 0x40
    call OSAPI_SND_TONE
.out:
    ret

; wa_hitdir - the player hurt by [wa_hurtby] (a figure's code; 0 itself):
; where it is, OFF the screen - beside the crosshair a ◄ (left) or ► (right),
; or under it a ▼ (behind), for WA_HDIRT ticks. On the screen, nothing: the
; one who shot is in view. A PROTOTYPE, the owner's to try. Keeps every
; register
WA_HDIRT    equ 20              ; two thirds of a second
WA_HDFOV    equ 107             ; half the view, 1024ths (37.5 degrees)
wa_hitdir:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [wa_hurtby]
    or al, al
    jz .out
    dec ax
    mov ah, F_SIZE
    mul ah
    add ax, wa_figs
    mov di, ax                  ; DI = who
    push word [wa_tx]
    push word [wa_ty]
    mov ax, [wa_px]
    mov [wa_tx], ax
    mov ax, [wa_py]
    mov [wa_ty], ax
    call wa_bear                ; AX = its bearing to us...
    pop word [wa_ty]
    pop word [wa_tx]
    add ax, 512                 ; ...ours to it, less where we face: 0
    mov bx, [wa_ang]            ; ahead, 256 to the left
    mov cl, 6
    shr bx, cl
    sub ax, bx
    and ax, 1023
    cmp ax, WA_HDFOV            ; (in view: no cue)
    jbe .out
    cmp ax, 1024 - WA_HDFOV
    jae .out
    mov cl, 1                   ; left
    cmp ax, 384
    jb .set
    inc cx                      ; right
    cmp ax, 640
    ja .set
    inc cx                      ; behind
.set:
    mov [wa_hdir], cl
    mov byte [wa_hdirt], WA_HDIRT
    mov byte [wa_ovd], 1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wa_ptick - the dead player's tick: the view sinks to the floor, the
; respawn timer counts (the HUD's second rebuilt), and at its end the player
; is back at the respawn point farthest from the nearest live bot, with the
; pistol alone and full health. Keeps all but AX, BX, CX, DX, SI
WA_PDEADT   equ 90              ; 3 s
WA_PROTT    equ 60              ; SPAWN PROTECTION: 2 s, or to the first
                                ; shot - nothing hurts a PLAYER just back
                                ; (the bots are computers: none, the owner's)
WA_PFLOOR   equ 20              ; the eye on the floor
wa_ptick:
    mov word [wa_kbits], 0      ; (no move, no jump)
    sub word [wa_peye], 6
    cmp word [wa_peye], WA_PFLOOR
    jge .eye
    mov word [wa_peye], WA_PFLOOR
.eye:
    mov ax, [wa_peye]
    add ax, [wa_feet]
    mov [wa_eye], ax
    dec byte [wa_pdead]
    jz wa_prespawn
    mov al, [wa_pdead]          ; (the HUD's count, a second at a time)
    xor ah, ah
    mov cl, 30
    div cl
    or ah, ah
    jnz .out
    mov byte [wa_ovd], 1
.out:
    ret

; wa_prespawn - the player back: at the respawn point farthest (the sum of
; the axis distances) from the nearest live bot
wa_prespawn:
    push di
    cmp byte [wa_gside], 0      ; A TEAM MODE ON SIDES: our side's points
    je .ffa                     ; alone, the farthest from our nearest foe
    mov al, [wa_tm]             ; (CTF-PLAN 2)
    xor ah, ah
    call wc_rspick
    mov di, si
    jmp .placed
.ffa:
    mov si, [wa_grp]
    lodsw
    mov cx, ax                  ; CX = the points
    xor bp, bp                  ; BP = the best's distance, DI the best
    mov di, si
.p:
    mov dx, 0x7FFF              ; DX = its nearest live bot
    mov bx, wa_figs
.f:
    cmp byte [bx+F_DEAD], 0
    jne .fn
    mov ax, [si]
    sub ax, [bx+F_X]
    call wa_abs
    mov [wa_tmp], ax
    mov ax, [si+2]
    sub ax, [bx+F_Y]
    call wa_abs
    add ax, [wa_tmp]
    cmp ax, dx
    jae .fn
    mov dx, ax
.fn:
    add bx, F_SIZE
    cmp bx, wa_figs + WA_NFIG*F_SIZE
    jb .f
    cmp dx, bp
    jbe .np
    mov bp, dx
    mov di, si
.np:
    add si, 8
    loop .p
.placed:
    mov ax, [di]
    mov [wa_px], ax
    mov ax, [di+2]
    mov [wa_py], ax
    mov ax, [di+4]
    mov cl, 6
    shl ax, cl
    mov [wa_ang], ax
    pop di
    call wa_arm0
    xor ax, ax
    mov [wa_pdead], al
    mov [wa_pxf], ax
    mov [wa_pyf], ax
    mov [wa_jz], ax
    mov [wa_jv], ax
    mov [wa_feet], ax
    mov [wa_pitch], ax
    mov word [wa_fpx], -1
    mov word [wa_eye], WA_EYE
    mov byte [wa_pprot], WA_PROTT ; (SPAWN PROTECTION: to its first shot)
    ret

; --- rockets (WEAPONS-PLAN 2.2) -----------------------------------------------
; FRAME holds each one's whole position and its state (the renderer draws
; it); wa_rkg the game's own rest: the fractions, the velocity, its life.
RG_XF       equ 0               ; fractions, 8.8's low byte in the high byte
RG_YF       equ 2
RG_ZF       equ 4
RG_VX       equ 6               ; velocity, 8.8 units a tick
RG_VY       equ 8
RG_VZ       equ 10
RG_LIFE     equ 12
RG_OWN      equ 14              ; who fired it: 0 the player, 1.. a bot
WA_RKGSZ    equ 16

; WA_RKMV axis - SI, DI = a rocket's records: the axis named (0 x, 2 y, 4 z)
; moved by its speed, 8.8 into the whole and the fraction - in line, a
; rocket's three every tick it flies. Clobbers AX, DX
%macro WA_RKMV 1
    mov dx, [di+RG_VX+%1]
    mov al, dh
    cbw                         ; AX = v >> 8, DX = (v & 255) << 8
    mov dh, dl
    xor dl, dl
    add [di+RG_XF+%1], dx
    adc [si+WA_RKX+%1], ax
%endmacro

; wa_rktick - each rocket a tick: an explosion's count; a flight's step, and
; what it meets - out of the room (gone over the walls, or into them), the
; floor or a box under it (wa_floor over every box, the player's radius
; out), a live figure (killed outright, and the explosion)
wa_rktick:
    cmp byte [wa_rkn], 0        ; (none live: the usual tick, nothing to do)
    je .done
    mov si, wa_rks
    mov di, wa_rkg
.r:
    mov al, [si+WA_RKST]
    or al, al
    jz .n
    cmp al, WA_RKFLY
    je .fly
    dec al                      ; an explosion: its ticks, 8 down to 2
    cmp al, WA_RKFLY
    jne .st
    xor al, al
    dec byte [wa_rkn]
.st:
    mov [si+WA_RKST], al
    jmp .n
.fly:
    cmp byte [si+WA_RKTYP], 0
    je .rocket
    jmp .gren
.rocket:
    dec word [di+RG_LIFE]
    jz .gone
    WA_RKMV 0
    WA_RKMV 2
    WA_RKMV 4
    mov ax, [si+WA_RKX]
    mov bx, [si+WA_RKY]
    cmp ax, WA_ROOM_X0
    jle .out
    cmp ax, WA_ROOM_X1
    jge .out
    cmp bx, WA_ROOM_Y0
    jle .out
    cmp bx, WA_ROOM_Y1
    jl .in
.out:
    cmp word [si+WA_RKZ], WA_HTOP ; out of the room: over its walls, gone;
    jge .gone                   ; into them, an explosion
    jmp .boom
.in:
    cmp word [si+WA_RKZ], 0
    jle .boom                   ; the floor
    push si
    mov si, wa_allp
    call wa_floor               ; AX = the tallest box under it (widened)
    pop si
    cmp [si+WA_RKZ], ax
    jle .boom
.hit:
    call wa_rkhit               ; a figure met: killed outright
    jc .boom
    call wa_rkhitp              ; a bot's, the player met: it goes off
    jc .boom
    jmp .n
.gren:                          ; A GRENADE: its fuse, gravity, then each
    dec word [di+RG_LIFE]       ; axis in turn - a wall (or a rise over
    jz .boom                    ; WA_GSTEP) sends it back at half its
    sub word [di+RG_VZ], WA_GRAV ; speed; the floor bounces it at half,
    mov bp, -1                  ; and rubs a quarter off its roll. BP = the
    xor bx, bx                  ; floor where it is, once a step that held
    call .gax                   ; has asked for it (.gax)
    mov bx, 2
    call .gax
    WA_RKMV 4
    mov ax, bp                  ; AX = the floor under it: as its last step
    or ax, ax                   ; that held found it, or (neither did)
    jns .gfl                    ; asked now
    mov ax, [si+WA_RKX]
    mov bx, [si+WA_RKY]
    push si
    mov si, wa_allp
    call wa_floor
    pop si
.gfl:
    cmp [si+WA_RKZ], ax
    jg .hit
    mov [si+WA_RKZ], ax         ; on it
    mov word [di+RG_ZF], 0
    mov ax, [di+RG_VZ]
    or ax, ax
    jns .hit
    neg ax
    sar ax, 1
    cmp ax, 256
    jge .bz
    xor ax, ax                  ; (under a unit a tick: it rests)
.bz:
    mov [di+RG_VZ], ax
    mov ax, [di+RG_VX]
    sar ax, 1
    sar ax, 1
    sub [di+RG_VX], ax
    mov ax, [di+RG_VY]
    sar ax, 1
    sar ax, 1
    sub [di+RG_VY], ax
    jmp .hit
.gax:                           ; BX = an axis, x 2 (0 x, 2 y): moved, and
    mov dx, [di+RG_VX+bx]       ; back with its speed turned and halved if
    or dx, dx                   ; that put it in a wall. With no speed along
    jz .gret                    ; it nothing moves, and a wall would turn
    push word [si+WA_RKX+bx]    ; nothing: not asked (a grenade at rest)
    push word [di+RG_XF+bx]
    mov al, dh                  ; (WA_RKMV, by the axis in BX)
    cbw
    mov dh, dl
    xor dl, dl
    add [di+RG_XF+bx], dx
    adc [si+WA_RKX+bx], ax
    mov ax, [si+WA_RKX]
    cmp ax, WA_ROOM_X0 + WA_RAD
    jle .gwall
    cmp ax, WA_ROOM_X1 - WA_RAD
    jge .gwall
    push bx
    mov bx, [si+WA_RKY]
    cmp bx, WA_ROOM_Y0 + WA_RAD
    jle .gw2
    cmp bx, WA_ROOM_Y1 - WA_RAD
    jge .gw2
    push si
    mov si, wa_allp
    call wa_floor
    pop si
    pop bx
    mov dx, ax
    sub dx, WA_GSTEP
    cmp dx, [si+WA_RKZ]
    jg .gwall
    mov bp, ax                  ; (clear: and this is the floor where it is)
    pop ax
    pop ax
.gret:
    ret
.gw2:
    pop bx
.gwall:
    pop word [di+RG_XF+bx]
    pop word [si+WA_RKX+bx]
    mov ax, [di+RG_VX+bx]
    neg ax
    sar ax, 1
    mov [di+RG_VX+bx], ax
    ret
.gone:
    mov byte [si+WA_RKST], 0
    dec byte [wa_rkn]
    jmp .n
.boom:
    call wa_boom
.n:
    add si, WA_RKSZ
    add di, WA_RKGSZ
    cmp si, wa_rks + WA_NRK*WA_RKSZ
    jae .done
    jmp .r
.done:
    ret

; wa_rkhit - SI = a rocket or a grenade: a live figure inside its square
; and between its feet and its head is killed outright (the bits). CF = 1
; then. Keeps all but AX, CX
wa_rkhit:
    mov al, [di+RG_OWN]         ; (its owner: not hit by its own, and its
    mov [wa_killer], al         ; kill)
    push di
    mov di, wa_figs
    mov cx, WA_NFIG
.f:
    cmp byte [di+F_DEAD], 0
    jne .fn
    mov ax, [si+WA_RKX]         ; (|dx| < WA_HITR: dx + (WA_HITR - 1) below
    sub ax, [di+F_X]            ; 2 WA_HITR - 1, unsigned - and the same of
    add ax, WA_HITR - 1         ; y; the distances first: most figures are
    cmp ax, 2*WA_HITR - 1       ; nowhere near)
    jae .fn
    mov ax, [si+WA_RKY]
    sub ax, [di+F_Y]
    add ax, WA_HITR - 1
    cmp ax, 2*WA_HITR - 1
    jae .fn
    mov ax, [si+WA_RKZ]
    sub ax, [di+F_Z]
    js .fn
    cmp ax, 120
    jg .fn
    call wa_bidx                ; (not its owner)
    inc al
    cmp al, [wa_killer]
    je .fn
    cmp byte [wa_gmode], 0      ; (A TEAM MODE: nor its owner's teammate -
    je .foe                     ; it flies on through; its splash, wa_boom
    push bx                     ; through wa_hurt, spares them too)
    mov bl, al
    xor bh, bh
    mov ah, [wa_tm+bx]
    mov bl, [wa_killer]
    cmp ah, [wa_tm+bx]
    pop bx
    je .fn
.foe:
    call wa_lkremote            ; (the other machine's: it goes off, and
    je .met                     ; that machine judges it - LINK-PLAN 3)
    mov ah, 2
    call wa_kill
.met:
    pop di
    stc
    ret
.fn:
    add di, F_SIZE
    loop .f
    pop di
    clc
    ret

; wa_rkhitp - SI, DI = a projectile's records: a bot's (its owner not the
; player) meets the player - inside the player's square and between the
; feet and 120 over them - CF = 1. Keeps all but AX, DX
wa_rkhitp:
    cmp byte [di+RG_OWN], 0
    je .no
    cmp byte [wa_pdead], 0
    jne .no
    mov ax, [si+WA_RKX]
    sub ax, [wa_px]
    add ax, WA_HITR - 1         ; (|dx| < WA_HITR, as wa_rkhit asks it)
    cmp ax, 2*WA_HITR - 1
    jae .no
    mov ax, [si+WA_RKY]
    sub ax, [wa_py]
    add ax, WA_HITR - 1
    cmp ax, 2*WA_HITR - 1
    jae .no
    call wa_fabs                ; DX = the feet
    mov ax, [si+WA_RKZ]
    sub ax, dx
    js .no
    cmp ax, 120
    jg .no
    stc
    ret
.no:
    clc
    ret

; wa_abs - AX = |AX|
wa_abs:
    or ax, ax
    jns .p
    neg ax
.p:
    ret

; wa_boom - SI = a rocket (FRAME's): it explodes there. The splash: each
; live figure and the player within WA_SPLR (the largest of the three axis
; distances, from their middle 56 up) take WA_SPLR less it - the player
; half. Keeps SI, DI
wa_boom:
    mov al, [di+RG_OWN]         ; (its owner: spared, and credited)
    mov [wa_killer], al
    push di
    mov byte [si+WA_RKST], WA_RKBOOM
    mov ax, 80
    mov cx, 5
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov di, wa_figs
    mov cx, WA_NFIG
.f:
    cmp byte [di+F_DEAD], 0
    jne .fn
    call wa_bidx
    inc al
    cmp al, [wa_killer]
    je .fn
    mov ax, [di+F_X]
    mov bx, [di+F_Y]
    mov dx, [di+F_Z]
    call wa_reach
    jae .fn
    call wa_lkremote            ; (the other machine's figures: its own
    je .fn                      ; to judge)
    push cx
    mov ah, 2
    call wa_hurt
    pop cx
.fn:
    add di, F_SIZE
    loop .f
    call wa_fabs                ; the player: DX = the feet
    mov ax, [wa_px]
    mov bx, [wa_py]
    call wa_reach
    jae .out
    mov ah, [wa_killer]         ; (a bot's: whole, and the bot's kill; the
    mov [wa_hurtby], ah         ; player's own: half, a suicide)
    or ah, ah
    jnz .whole
    shr al, 1
    jz .out
.whole:
    mov byte [wa_lkpkk], 2      ; (in bits, as the other machine draws us)
    call wa_phurt
.out:
    pop di
    ret

; wa_reach - AX, BX = x, y, DX = the floor of someone whose middle is 56 up;
; SI = a rocket: CF = 1 and AL = WA_SPLR less the distance when within
; reach. Clobbers AX, BX, DX
wa_reach:
    sub ax, [si+WA_RKX]
    call wa_abs
    xchg ax, bx
    sub ax, [si+WA_RKY]
    call wa_abs
    cmp ax, bx
    jae .y
    mov ax, bx
.y:
    xchg ax, dx
    add ax, 56
    sub ax, [si+WA_RKZ]
    call wa_abs
    cmp ax, dx
    jae .z
    mov ax, dx
.z:
    cmp ax, WA_SPLR             ; CF = within
    jae .out
    neg ax
    add ax, WA_SPLR
    stc
.out:
    ret

; --- pickups (WEAPONS-PLAN 2.4) ------------------------------------------------
; wa_pktick - each tick: a taken pickup's respawn counted down; a present one
; the player stands at (the square within WA_PKR: the player's radius and
; the pickup's; the feet from 8 below its floor to 48 above) taken - if it
; helps. Not during a bench
WA_PKR      equ 40
wa_pktick:
%ifdef WA_BENCH
    cmp byte [wa_pbon], 0
    jne .q
%endif
    cmp byte [wa_pdead], 0
    jne .q
    mov cx, [wa_npk]
    jcxz .q
    mov bx, [wa_py]             ; THE PICKUPS WITHIN REACH OF THE PLAYER'S
    shr bx, 1                   ; CELL (wa_pkgrid: 128 units square, a bit a
    shr bx, 1                   ; pickup, made when the arena loads) - with
    shr bx, 1                   ; none of them present and no timer running,
    and bx, 0x00F0              ; the usual tick, there is nothing to do
    mov ax, [wa_px]
    shl ax, 1
    or bl, ah
    mov dh, [wa_pkgrid+bx]      ; DH = them
    mov ax, [wa_pkt]            ; any timer running (the words themselves:
    or ax, [wa_pkt+2]           ; a timer counts whatever wa_pkon says - a
    or ax, [wa_pkt+4]           ; test sets one on a pickup that is there,
    or ax, [wa_pkt+6]           ; for its roll)
%if WA_MPK != 8
 %error "wa_pktick asks WA_MPK = 8 timers, four and four"
%endif
    cmp cl, 4
    jbe .t
    or ax, [wa_pkt+8]
    or ax, [wa_pkt+10]
    or ax, [wa_pkt+12]
    or ax, [wa_pkt+14]
.t:
    or ax, ax
    jnz .go
    test dh, [wa_pkon]
    jnz .go
.q:
    ret
.go:
    mov si, wa_pks
    xor bx, bx                  ; BX = the pickup x 2
    mov dl, 1                   ; DL = its bit
.p:
    cmp word [wa_pkt+bx], 0
    jne .away
    test dh, dl                 ; present: in reach?
    jnz .here
.n:
    add si, WA_PKSZ
    inc bx
    inc bx
    shl dl, 1
    loop .p
    ret
.away:
    cmp byte [wa_lkon], 0
    je .count
    call wa_lkpkhold            ; (a linked guest: back when the host says)
    je .n
.count:
    dec word [wa_pkt+bx]
    jnz .n
    or [wa_pkon], dl            ; back - a weapon spawn with its weapon
    call wa_pkroll              ; picked again
    call wa_lkpkback            ; (the host: ROLL)
    jmp .n
.here:
    mov ax, [wa_px]
    sub ax, [si]
    call wa_abs
    cmp ax, WA_PKR
    jae .n
    mov ax, [wa_py]
    sub ax, [si+2]
    call wa_abs
    cmp ax, WA_PKR
    jae .n
    push dx
    call wa_fabs                ; DX = the feet
    mov ax, dx
    pop dx
    sub ax, [si+4]
    cmp ax, -8
    jl .n
    cmp ax, 48
    jg .n
    push bx
    push cx
    push dx
    mov al, [si+6]
    call wa_pktake
    pop dx
    pop cx
    pop bx
    jnc .n
    not dl                      ; taken: gone until its time is up
    and [wa_pkon], dl
    not dl
    push bx
    mov bl, [si+6]
    xor bh, bh
    shl bx, 1
    mov ax, [wa_pkresp+bx]
    pop bx
    mov [wa_pkt+bx], ax
    mov byte [wa_ovd], 1
    call wa_lkpktaken           ; (linked: TAKE)
    jmp .n

; wa_pkgridmk - once an arena is loaded: wa_pkgrid, for each 128-unit cell
; the pickups (a bit each) a player standing somewhere in it could be within
; WA_PKR of in x and in y - x - (WA_PKR - 1) .. x + (WA_PKR - 1), the same of
; y: a cell or two each way
wa_pkgridmk:
    push es
    push ds
    pop es
    mov di, wa_pkgrid
    xor ax, ax
    mov cx, 128
    cld
    rep stosw
    pop es
    mov cx, [wa_npk]
    jcxz .out
    mov si, wa_pks
    mov dl, 1                   ; DL = the pickup's bit
.p:
    push cx
    mov ax, [si+2]
    sub ax, WA_PKR - 1
    call wa_lists.cc
    mov bl, al                  ; BL = its first row of cells...
    mov ax, [si+2]
    add ax, WA_PKR - 1
    call wa_lists.cc
    mov bh, al                  ; ...BH its last
    mov ax, [si]
    sub ax, WA_PKR - 1
    call wa_lists.cc
    mov ch, al                  ; CH = its first column...
    mov ax, [si]
    add ax, WA_PKR - 1
    call wa_lists.cc
    mov cl, al                  ; ...CL its last
.row:
    mov al, bl
    mov ah, 16
    mul ah
    add al, ch
    mov di, ax                  ; DI = the row's first cell of them
    mov al, ch
.cell:
    or [wa_pkgrid+di], dl
    inc di
    inc al
    cmp al, cl
    jbe .cell
    inc bl
    cmp bl, bh
    jbe .row
    pop cx
    add si, WA_PKSZ
    shl dl, 1
    loop .p
.out:
    ret

; wa_pktake - AL = a pickup's kind: CF = 1 it was taken (it helped). A
; weapon (kinds 0..4: weapons 1..5) the first time: it, loaded from what
; its pickup gives (wa_wgive), and in hand; again: that much more for its
; reserve, up to its cap (wa_wcap). Health (5): to 100. Shield (6): to 100.
; Clobbers AX, BX, CX
wa_pktake:
    cmp al, 5
    jb .wpn
    je .health
    cmp word [wa_sh], WA_FHP
    jae .no
    mov word [wa_sh], WA_FHP
    jmp .yes
.health:
    cmp word [wa_hp], WA_FHP
    jae .no
    mov word [wa_hp], WA_FHP
    jmp .yes
.wpn:
    inc ax                      ; AL = the weapon
    xor bx, bx
    mov bl, al
    mov cl, al
    mov ah, 1
    shl ah, cl
    test [wa_own], ah
    jnz .more
    or [wa_own], ah             ; new: loaded, and in hand - what the
    mov cl, [wa_wmag+bx]        ; pickup gives, the magazine first
    mov ch, [wa_wgive+bx]
    cmp cl, ch
    jbe .ld
    mov cl, ch
.ld:
    mov [wa_mag+bx], cl
    sub ch, cl
    mov [wa_res+bx], ch
    call wa_wset
    jmp .yes
.more:
    mov cl, [wa_wcap+bx]
    cmp [wa_res+bx], cl
    jae .no
    mov ch, [wa_wgive+bx]
    add [wa_res+bx], ch
    cmp [wa_res+bx], cl
    jbe .held
    mov [wa_res+bx], cl
.held:
    cmp bl, [wa_wpn]            ; held, and waiting for this: reload
    jne .yes
    cmp byte [wa_rld], 0
    jne .yes
    call wa_wready
.yes:
    mov ax, 1800
    mov cx, 2
    mov dl, 0x40
    call OSAPI_SND_TONE
    stc
    ret
.no:
    clc
    ret

; --- the overlay (WEAPONS-PLAN 2.5) ---------------------------------------------
; Built here when something it shows changed (wa_ovd), copied into the part
; with the frame, drawn there every frame: the weapon held in front of the
; player, right of the centre at the bottom of the view (a row lower while
; it reloads); the health and the shield at row 23's left; the magazine and
; the reserve at its right.
; A sprite: row, column from W/2 + 3, glyph, colour attribute; 0xFF ends it
WA_SPMAX    equ 16              ; a sprite's most cells (the rocket launcher's)
wa_spr:     dw wa_spr0, wa_spr1, wa_spr2, wa_spr3, wa_spr4, wa_spr5
wa_spr0:                        ; the pistol
    db 21, 1, 0xDC, 0x0F
    db 22, 0, 0xDE, 0x07, 22, 1, 0xDB, 0x0F, 22, 2, 0xDD, 0x07
    db 23, 0, 0xDE, 0x07, 23, 1, 0xDB, 0x07, 23, 2, 0xDD, 0x07
    db 0xFF
wa_spr1:                        ; the shotgun: two barrels, a brown stock
    db 20, 1, 0xDC, 0x07, 20, 2, 0xDC, 0x07
    db 21, 1, 0xDB, 0x07, 21, 2, 0xDB, 0x07
    db 22, 0, 0xDE, 0x06, 22, 1, 0xDB, 0x06, 22, 2, 0xDB, 0x06, 22, 3, 0xDD, 0x06
    db 23, 0, 0xDE, 0x06, 23, 1, 0xDB, 0x06, 23, 2, 0xDB, 0x06, 23, 3, 0xDD, 0x06
    db 0xFF
wa_spr2:                        ; the chaingun: a grey cluster of barrels
    db 20, 0, 0xDC, 0x07, 20, 1, 0xDC, 0x0F, 20, 2, 0xDC, 0x07
    db 21, 0, 0xDB, 0x07, 21, 1, 0xB1, 0x08, 21, 2, 0xDB, 0x07
    db 22, 0, 0xDE, 0x08, 22, 1, 0xDB, 0x07, 22, 2, 0xDB, 0x07, 22, 3, 0xDD, 0x08
    db 23, 0, 0xDE, 0x08, 23, 1, 0xDB, 0x07, 23, 2, 0xDB, 0x07, 23, 3, 0xDD, 0x08
    db 0xFF
wa_spr3:                        ; the grenade launcher: a short magenta drum
    db 21, 0, 0xDC, 0x05, 21, 1, 0xDB, 0x0D, 21, 2, 0xDC, 0x05
    db 22, 0, 0xDB, 0x05, 22, 1, 0xDB, 0x05, 22, 2, 0xDB, 0x05
    db 23, 0, 0xDE, 0x05, 23, 1, 0xDB, 0x0D, 23, 2, 0xDD, 0x05
    db 0xFF
wa_spr5:                        ; the railgun: a long cyan barrel, its coils
    db 19, 1, 0xDC, 0x0B
    db 20, 1, 0xDB, 0x03
    db 21, 0, 0xDE, 0x0B, 21, 1, 0xDB, 0x03, 21, 2, 0xDD, 0x0B
    db 22, 0, 0xDE, 0x03, 22, 1, 0xDB, 0x0B, 22, 2, 0xDD, 0x03
    db 23, 0, 0xDE, 0x0B, 23, 1, 0xDB, 0x03, 23, 2, 0xDD, 0x0B
    db 0xFF
wa_spr4:                        ; the rocket launcher: a green tube
    db 20, 0, 0xDC, 0x0A, 20, 1, 0xDC, 0x0A, 20, 2, 0xDC, 0x0A, 20, 3, 0xDC, 0x0A
    db 21, 0, 0xDB, 0x02, 21, 1, 0xB1, 0x02, 21, 2, 0xB1, 0x02, 21, 3, 0xDB, 0x02
    db 22, 0, 0xDB, 0x02, 22, 1, 0xDB, 0x02, 22, 2, 0xDB, 0x02, 22, 3, 0xDB, 0x02
    db 23, 0, 0xDF, 0x02, 23, 1, 0xDB, 0x0A, 23, 2, 0xDB, 0x0A, 23, 3, 0xDF, 0x02
    db 0xFF

wa_ovbuild:
    mov byte [wa_ovd], 0
    mov es, [wa_rds]            ; ES = the part, DI = its cells so far, x 2:
    xor di, di                  ; wa_ovput's, kept through every builder
    cld
    mov word [es:WA_RAT(wa_ovfresh)], 1 ; (the renderer shows it, then clears
                                ; this)
    mov byte [es:WA_RAT(wa_mmbh)], 0 ; (no minimap but under Tab)
    cmp byte [wa_sbon], 0       ; Tab held: the minimap and the scoreboard
    je .nosb                    ; (AI-PLAN 5; NET-PLAN 9.7)
    call wa_ovmm
    call wa_ovboard
.nosb:
    cmp byte [wa_pdead], 0      ; dead: the respawn's count
    je .alive
    call wa_ovrsp
.alive:
    call wa_lkmsg               ; (linked: WAITING, LINK LOST, LEFT)
    call wa_ovextra             ; (pause, the leave question, the hit's side)
    call wa_ovctf               ; (CTF: the flag line, its messages)
    mov al, [wa_wpn]            ; THE WEAPON: its cells as they were made
    xor ah, ah                  ; the last time (wa_spc, wa_spg) while it is
    cmp byte [wa_rld], 0        ; the same weapon in the same place: two rows
    je .kick                    ; down and two right while it reloads (the
    mov ah, 2                   ; owner: one row was hard to see), a row up
    jmp .key                    ; the first WA_BUCKT ticks after the
.kick:                          ; pistol's shot (its kick)
    or al, al
    jnz .key
    cmp byte [wa_cool], WA_PCOOL - WA_BUCKT
    jbe .key
    dec ah
.key:
    cmp ax, [wa_spk]
    je .sprcp
    mov [wa_spk], ax
    push di
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [wa_spr+bx]
    mov cx, [wa_w]
    shr cx, 1
    add cx, 3                   ; CX = its column 0
    xor di, di                  ; DI = its cells, x 2 - from none, whatever
                                ; the overlay holds already (the pushed DI):
                                ; left for the reload's arm alone, a sprite
                                ; made after CTF's lines began past them and
                                ; wa_spn counted cells that were never its
    cmp ah, 2                   ; (reloading: tipped away, two right)
    jne .spr
    inc cx
    inc cx
.spr:
    lodsb
    cmp al, 0xFF
    je .sprd
    mov bl, al
    add bl, ah                  ; (reloading: a row lower)
    xor bh, bh                  ; BX = the row
    lodsb
    mov dl, al
    xor dh, dh
    add dx, cx                  ; DX = the column
    lodsb
    cmp bx, WA_ROWS-1
    ja .sprn
    cmp dx, [wa_w]
    jae .sprn
    cmp di, WA_SPMAX*2
    jae .sprn
    shl bx, 1
    add dx, [wa_rowoff+bx]      ; the cell, and x 2: the renderer's offset
    shl dx, 1
    mov [wa_spc+di], dx
    mov bl, [si]
    cmp byte [wa_mono], 0
    je .sp
    mov bl, 0x07
.sp:
    mov [wa_spg+di], al
    mov [wa_spg+di+1], bl
    inc di
    inc di
.sprn:
    inc si                      ; (past its attribute)
    jmp .spr
.sprd:
    shr di, 1
    mov [wa_spn], di
    pop di
.sprcp:
    mov cx, WA_MOV*2            ; as many of them as the overlay has room
    sub cx, di                  ; for (the rest dropped, as wa_ovput drops)
    jbe .status
    shr cx, 1
    cmp cx, [wa_spn]
    jbe .fit
    mov cx, [wa_spn]
.fit:
    jcxz .status
    mov bx, cx
    mov dx, di
    mov si, wa_spc
    add di, WA_RAT(wa_ov)
    rep movsw
    mov cx, bx
    mov si, wa_spg
    mov di, dx
    add di, WA_RAT(wa_ovg)
    rep movsw
    mov di, dx
    shl bx, 1
    add di, bx
.status:
    cmp byte [wa_beam], 0       ; THE RAILGUN'S BEAM, its few ticks: from
    je .nobeam                  ; the barrel's mouth to the crosshair (a
    mov dx, 13                  ; ray from the gun to where the eye looks
.beam:                          ; draws so), one cell a row
    mov bx, dx
    sub bx, 12
    shr bx, 1                   ; (rows below the crosshair's, halved: the
    mov ax, [wa_w]              ; barrel is 3 or 4 columns right of it)
    shr ax, 1
    add bx, ax
    mov ax, 0x0B5C              ; '\' bright cyan
    call wa_ovatt
    call wa_ovput
    inc dx
    cmp dx, 19
    jb .beam
.nobeam:
    mov dx, WA_ROWS-1           ; row 23: the health
    xor bx, bx
    mov ax, 0x0C03              ; (a heart, red - and white on red while a
    cmp byte [wa_phit], 0       ; hit is fresh: AI-PLAN 4.3; cyan while
    je .hp                      ; the spawn's protection lasts)
    mov ah, 0x4F
.hp:
    cmp byte [wa_pprot], 0
    je .heart
    mov ah, 0x0B
.heart:
    call wa_ovatt
    call wa_ovput
    mov word [wa_ovat], 0x0F0F
    mov ax, [wa_hp]
    mov cx, 3
    call wa_ovnum
    cmp word [wa_sh], 0         ; the shield, when there is one
    je .ammo
    inc bx
    mov ax, 0x0E04              ; (a diamond, yellow)
    call wa_ovatt
    call wa_ovput
    mov word [wa_ovat], 0x0E07
    mov ax, [wa_sh]
    mov cx, 3
    call wa_ovnum
.ammo:
    mov bl, [wa_wpn]            ; at the right: magazine / reserve (or the
    xor bh, bh                  ; pistol's endless one)
    mov si, bx
    mov al, [wa_mag+bx]
    xor ah, ah
    mov bx, [wa_w]
    sub bx, 6
    mov word [wa_ovat], 0x0F0F
    mov cx, 2
    call wa_ovnum
    mov ax, 0x072F              ; '/'
    call wa_ovput
    or si, si
    jnz .res
    mov ax, 0x0FEC              ; (infinity)
    call wa_ovatt
    call wa_ovput
    jmp short .built
.res:
    mov al, [wa_res+si]
    xor ah, ah
    mov cx, 3
    call wa_ovnum
.built:
    shr di, 1                   ; its cells, counted
    mov [es:WA_RAT(wa_ovn)], di
    ret

; THE SCOREBOARD (AI-PLAN 5), while Tab is held: a box 20 cells wide over
; the view's middle, a header and a row each (YOU and the three robots), by
; kills, most first: the name, kills, deaths. White on blue (inverse on
; mono), the header yellow, YOU's row bright green
WA_SBW      equ 20
WA_MMROW    equ 1               ; THE MINIMAP's first row (row 0: CTF's line)
WA_SBROW    equ WA_MMROW + WA_MMH / 2 + 1 ; ...and the board's, under it
WA_NNAME    equ 8
wa_names:   db 'COG     ', 'RIVET   ', 'SERVO   ', 'SPROCKET'
            db 'DIODE   ', 'PISTON  ', 'BOLT    ', 'GEAR    '
wa_you:     db 'YOU     '

; wa_fname - BL = a figure's code here (0 the player, 1..3 the figures): SI
; = its name, WA_NNAME characters - the player's nick (YOU without one), a
; linked player's as its machine sent it, a bot's its robot name. Keeps all
; but SI
wa_fname:
    push ax
    push bx
    xor bh, bh
    or bl, bl
    jnz .fig
    mov si, wa_nick
    cmp byte [si], ' '
    ja .out
    mov si, wa_you
    jmp .out
.fig:
    cmp byte [wa_lkon], 0
    je .bot
    call wa_lksbname
    jnc .out
.bot:
    mov al, [wa_bname-1+bx]
    and ax, WA_NNAME - 1
    mov cl, 3
    shl ax, cl
    add ax, wa_names
    mov si, ax
.out:
    pop bx
    pop ax
    ret
wa_sbhead:  db ' NAME       K    D  '
wa_ovboard:
    xor bx, bx                  ; the order: by kills, most first
.o:
    mov [wa_sord+bx], bl
    inc bx
    cmp bx, 4
    jb .o
    mov cx, 3
.pass:
    xor bx, bx
.cmp:
    mov al, [wa_sord+bx]        ; two neighbours: the one with fewer kills
    mov ah, [wa_sord+bx+1]      ; goes after
    push bx
    mov bl, al
    xor bh, bh
    mov dl, [wa_kills+bx]
    mov bl, ah
    mov dh, [wa_kills+bx]
    pop bx
    cmp dl, dh
    jae .ok
    mov [wa_sord+bx], ah
    mov [wa_sord+bx+1], al
.ok:
    inc bx
    cmp bx, 3
    jb .cmp
    loop .pass
    mov dx, WA_SBROW            ; the header, then a row each
    mov si, wa_sbhead
    mov ax, 0x1E70
    call wa_ovrow
    xor bp, bp
.row:
    mov bl, [ds:bp+wa_sord]     ; (DS: BP would address the stack)
    cmp byte [wa_lkon], 0       ; (linked: the slots the match has - its
    je .rw                      ; players, then its bots: NET-PLAN 4)
    or bl, bl
    jz .rw
    xor bh, bh
    call wa_lksbname
    jc .skip
.rw:
    inc dx
    mov bl, [ds:bp+wa_sord]
    xor bh, bh
    push di                     ; (the overlay's cursor and the part, kept
    push es                     ; while the row's text is made)
    mov di, wa_sbbuf
    push ds
    pop es
    mov al, ' '
    mov cx, WA_SBW
    cld
    rep stosb
    call wa_fname               ; (the player's nick, or YOU)
    mov ax, 0x1A0F              ; (YOU: bright green, or plain on mono)
    or bx, bx
    jz .nm
    mov ax, 0x1F70
    cmp byte [wa_lkon], 0
    je .bn
    call wa_lksbname            ; (linked: its player's name, or its bot's)
    jmp .nm
.bn:
    mov al, [wa_bname-1+bx]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, wa_names
    mov si, ax
    mov ax, 0x1F70
.nm:
    push ax
    mov di, wa_sbbuf+1
    mov cx, 8
    rep movsb
    mov al, [wa_kills+bx]
    mov di, wa_sbbuf+9
    call wa_sbnum
    mov al, [wa_deaths+bx]
    mov di, wa_sbbuf+14
    call wa_sbnum
    pop ax
    pop es
    pop di
    mov si, wa_sbbuf
    call wa_ovrow
.skip:
    inc bp
    cmp bp, 4
    jb .row
    ret

; THE MINIMAP (NET-PLAN 9.7), while Tab is held: the arena's floor plan
; over the board, made on the host (arenagen's minimap: WA_MMW x WA_MMH
; samples, four kinds, a byte two 80-column cells) and only turned into
; cells here, when the overlay is rebuilt. A cell is the upper half-block,
; its top sample's colour over its bottom's; at 40 columns a cell is two
; samples wide, the larger kind of each pair (a wall wins). On it: the
; player, a red dot - the overlay is rebuilt when the dot changes cell
; (wa_hudrow), not every frame - and in capture the flag the two bases.
; Nobody else, and no flag where it is now: a plan of the arena, not a radar.
wa_mmcol:   db 0, 1, 3, 7       ; off the walk, floor, low, wall: black,
                                ; blue, cyan, grey (a background: 0..7)
wa_mmmono:  db ' ', 0xDF, 0xDC, 0xDB ; mono: a box in the top, the bottom

; wa_mmcell - AX = x, DX = y (units): AX = the minimap's cell over it, its
; row high and its column low (wa_mmkey's form). Keeps all but AX, DX
wa_mmcell:
    push cx
    or ax, ax                   ; (the room, 0..2047, both ways)
    jns .x0
    xor ax, ax
.x0:
    cmp ax, 2047
    jbe .x1
    mov ax, 2047
.x1:
    or dx, dx
    jns .y0
    xor dx, dx
.y0:
    cmp dx, 2047
    jbe .y1
    mov dx, 2047
.y1:
    mov cl, 6                   ; a column: 64 units (80), 128 (40) - a
    cmp word [wa_w], 40         ; sample on at phase 1
    jne .c
    inc cx
    cmp byte [wa_mmph], 0
    je .c
    add ax, 64
.c:
    shr ax, cl
    push ax
    mov ax, 2047                ; a row: north at the top
    sub ax, dx
    mov cx, WA_MMH / 2
    mul cx                      ; (26,611 at most: DX = 0)
    mov cl, 11
    shr ax, cl
    mov ah, al
    pop cx
    mov al, cl
    pop cx
    ret

; wa_ovmm - the minimap into the part's block (wafb.inc's wa_mma: a byte
; a cell, the renderer's to put on the glass), at WA_MMROW, centred, and
; its marks. ES = the part. Keeps DI; clobbers AX, BX, CX, DX, SI
wa_ovmm:
    push di
    mov al, WA_MMW              ; its cells across: a sample each at 80
    cmp word [wa_w], 40         ; columns, two at 40 (a cell more at phase
    jne .w                      ; 1: a sample alone at either end)
    shr al, 1
    add al, [wa_mmph]
.w:
    mov [wa_mmw], al
    mov [es:WA_RAT(wa_mmbw)], al
    mov byte [es:WA_RAT(wa_mmbh)], WA_MMH / 2
    mov bx, [wa_w]              ; its first cell, x 2
    sub bl, al
    shr bx, 1
    add bx, [wa_rowoff+WA_MMROW*2]
    shl bx, 1
    mov [wa_mmo0], bx
    mov [es:WA_RAT(wa_mmo)], bx
    mov ax, 0x07DF              ; the half every cell shares: the upper
    mov dx, 1                   ; half-block, its byte the attribute - or
    cmp byte [wa_mono], 0       ; on mono the attribute, its byte the glyph
    je .fx
    dec dx
.fx:
    mov [es:WA_RAT(wa_mmfix)], ax
    mov [es:WA_RAT(wa_mmhi)], dx
    mov ax, 0xFFFF              ; THE MARKS: none yet
    mov [es:WA_RAT(wa_mmk)], ax
    mov [es:WA_RAT(wa_mmk)+4], ax
    mov [es:WA_RAT(wa_mmk)+8], ax
    mov ax, [wa_px]             ; ...the player's cell...
    mov dx, [wa_py]
    call wa_mmcell
    mov [wa_mmkey], ax
    mov ax, 0xFFFF              ; ...and in capture the flag the bases'
    mov [wa_mmf], ax
    mov [wa_mmf+2], ax
    cmp byte [wa_gmode], 1
    jne .nf
    mov si, [wa_gflp]
    cmp word [si], 2
    jne .nf
    mov ax, [si+2]
    mov dx, [si+4]
    call wa_mmcell
    mov [wa_mmf], ax
    mov ax, [si+8]
    mov dx, [si+10]
    call wa_mmcell
    mov [wa_mmf+2], ax
.nf:
    mov word [es:WA_RAT(wa_mmp)], wa_mma
    push es
    push ds
    pop es
    mov si, [wa_gmmp]
    mov di, wa_mma              ; ES:DI = the block's bytes, ours (the part
                                ; reads them through wa_mmseg)
    xor cx, cx                  ; CH = the row, CL = the column
.r:
    xor cl, cl
.c:
    push cx
    mov al, cl
    xor ah, ah
    cmp byte [wa_mmw], WA_MMW
    jne .k40
    shr ax, 1                   ; 80: half a byte a cell
    mov bx, ax
    mov al, [si+bx]
    test cl, 1
    jz .k80
    mov cl, 4
    shr al, cl
.k80:
    mov ah, al
    and al, 3                   ; AL = the top's kind
    shr ah, 1
    shr ah, 1
    and ah, 3                   ; AH = the bottom's
    jmp short .kk
.k40:
    mov bx, ax                  ; 40: two samples a cell - 2c and 2c+1, or
    shl bl, 1                   ; at phase 1 2c-1 and 2c - each half the
    sub bl, [wa_mmph]           ; larger kind of the pair's (BL past the
    xor ax, ax                  ; map, 0FFh or 32: no sample)
    call .samp
    inc bl
    call .samp
    jmp short .kk
.samp:                          ; BL = a sample: AL, AH the larger kinds
    cmp bl, WA_MMW              ; (keeps BX)
    jae .sr
    push bx
    mov dl, bl
    shr bl, 1
    mov dh, [si+bx]
    test dl, 1
    jz .se
    mov cl, 4
    shr dh, cl
.se:
    mov dl, dh
    and dl, 3                   ; (its top's)
    cmp al, dl
    jae .s1
    mov al, dl
.s1:
    shr dh, 1
    shr dh, 1
    and dh, 3                   ; (its bottom's)
    cmp ah, dh
    jae .s2
    mov ah, dh
.s2:
    pop bx
.sr:
    ret
.kk:
    pop cx
    call wa_mmglyph             ; AL = the glyph, AH = its attribute
    push ax
    cmp byte [wa_mono], 0       ; the block's byte: the attribute, or on
    jne .bm                     ; mono the glyph
    mov al, ah
.bm:
    stosb
    pop ax
    xor bx, bx                  ; THE MARKS: the player a red dot, the
    cmp cx, [wa_mmkey]          ; bases red's and blue's pennants
    je .mk
    mov bl, 4
    cmp cx, [wa_mmf]
    je .mk
    mov bl, 8
    cmp cx, [wa_mmf+2]
    jne .nx
.mk:
    and ah, 0x70                ; (on the cell's own ground)
    or ah, [wa_mmka+bx]
    mov al, [wa_mmkg+bx]
    cmp byte [wa_mono], 0
    je .mc
    mov ah, 0x0F
.mc:
    push es
    mov es, [wa_rds]
    mov [es:WA_RAT(wa_mmk)+bx+2], ax
    mov al, ch                  ; its cell: the block's first, CH rows and
    mov ah, [wa_w]              ; CL columns on (x 2)
    mul ah
    add al, cl
    adc ah, 0
    shl ax, 1
    add ax, [wa_mmo0]
    mov [es:WA_RAT(wa_mmk)+bx], ax
    pop es
.nx:
    inc cl
    cmp cl, [wa_mmw]
    jb .c
    add si, WA_MMW / 2
    inc ch
    cmp ch, WA_MMH / 2
    jb .r
    pop es
    pop di
    ret
wa_mmkg:    db 0x07, 0, 0, 0, 0x10, 0, 0, 0, 0x11 ; the marks' glyphs and
wa_mmka:    db 0x0C, 0, 0, 0, 0x0C, 0, 0, 0, 0x09 ; colours, by BX

; wa_mmglyph - AL = a cell's top kind, AH = its bottom's: AL = its glyph,
; AH = its attribute (colour: the upper half-block, top over bottom; mono:
; a box's halves on black). Keeps all but AX
wa_mmglyph:
    push bx
    mov bx, wa_mmmono
    cmp byte [wa_mono], 0
    jne .mono
    mov bx, wa_mmcol
    xlatb                       ; AL = the top's colour
    xchg al, ah
    xlatb                       ; AL = the bottom's
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or ah, al
    mov al, 0xDF
    pop bx
    ret
.mono:
    shr al, 1                   ; (a box: kinds 2 and 3)
    shr ah, 1
    shl ah, 1
    or al, ah
    xlatb
    mov ah, 0x07
    pop bx
    ret

; wa_sbnum - AL = a count: three cells at DI, right-aligned
wa_sbnum:
    xor ah, ah
    mov cl, 10
    add di, 2
.d:
    div cl
    add ah, '0'
    mov [di], ah
    dec di
    xor ah, ah
    or al, al
    jnz .d
    ret

; wa_ovrow - SI = WA_SBW characters, AH = their colour, AL = their mono
; attribute: a row of the board at DX, centred - straight onto the overlay
; (ES, DI: wa_ovput's), its cells one after the other, those past WA_MOV
; dropped. Clobbers AX, BX, CX, SI
wa_ovrow:
    cmp byte [wa_mono], 0
    je .c
    mov ah, al
.c:
    mov bx, dx
    shl bx, 1
    mov bx, [wa_rowoff+bx]
    mov cx, [wa_w]
    sub cx, WA_SBW
    shr cx, 1
    add bx, cx
    shl bx, 1                   ; BX = its first cell's offset
    mov cx, WA_SBW
.r:
    cmp di, WA_MOV*2
    jae .full
    lodsb
    mov [es:WA_RAT(wa_ovg)+di], ax
    mov [es:WA_RAT(wa_ov)+di], bx
    inc di
    inc di
    inc bx
    inc bx
    loop .r
.full:
    ret

; wa_ovextra - the overlay's rest (ES, DI: wa_ovput's): PAUSED; the leave
; question in its outlined box; the hit's side beside the crosshair.
; Clobbers AX, BX, CX, DX, SI
wa_ovextra:
    cmp byte [wa_paused], 0     ; PAUSED, over the middle
    je .np
    mov si, wa_pausetx
    mov dx, 10
    mov ah, 0x0F
    call wa_ovtext
.np:
    cmp byte [wa_ask], 0        ; THE QUESTION (a linked match's Esc): a
    je .na                      ; box, its frame white, the question red
    mov dx, 9
    mov si, wa_asktop
    mov ah, 0x0F
    call wa_ovtext
    inc dx                      ; (its middle row in one pass: frame,
    mov bx, [wa_w]              ; question, frame)
    sub bx, WA_ASKW
    shr bx, 1
    mov ax, 0x0FB3
    call wa_ovatt
    call wa_ovput
    mov si, wa_asktx
    mov ah, 0x0C
    cmp byte [wa_mono], 0
    je .q
    mov ah, 0x0F
.q:
    lodsb
    or al, al
    jz .qd
    call wa_ovput
    jmp .q
.qd:
    mov ax, 0x0FB3
    call wa_ovatt
    call wa_ovput
    inc dx
    mov si, wa_askbot
    mov ah, 0x0F
    call wa_ovtext
.na:
    cmp byte [wa_hdirt], 0      ; THE HIT'S SIDE
    je .out
    mov bx, [wa_w]
    shr bx, 1                   ; BX = the crosshair's column, DX its row
    mov dx, WA_ROWS / 2
    mov al, 0x11                ; ◄
    sub bx, 3
    cmp byte [wa_hdir], 1
    je .h
    mov al, 0x10                ; ►
    add bx, 6
    cmp byte [wa_hdir], 2
    je .h
    sub bx, 3                   ; ▼, under it
    add dx, 2
    mov al, 0x1F
.h:
    mov ah, 0x0C
    call wa_ovatt
    call wa_ovput
.out:
    ret

; wa_ovtext - SI = text (NUL), AH = its attribute: centred at row DX onto
; the overlay. Clobbers AL, BX, CX, SI
wa_ovtext:
    call wa_ovatt
    push si
    xor cx, cx
.l:
    lodsb
    or al, al
    jz .ld
    inc cx
    jmp .l
.ld:
    pop si
    mov bx, [wa_w]
    sub bx, cx
    shr bx, 1
.c:
    lodsb
    or al, al
    jz .out
    call wa_ovput
    jmp .c
.out:
    ret
WA_ASKW     equ 24              ; the question's box, its frame included
wa_pausetx: db 'PAUSED - P plays on', 0
wa_asktop:  db 0xDA
            times WA_ASKW - 2 db 0xC4
            db 0xBF, 0
wa_askbot:  db 0xC0
            times WA_ASKW - 2 db 0xC4
            db 0xD9, 0
wa_asktx:   db ' Leave the match? Y/N ', 0 ; (WA_ASKW - 2 cells)

; wa_ovrsp - dead: "RESPAWN n" over the view's middle, n its seconds
wa_ovrsp:
    push di                     ; (the overlay's cursor and the part, kept)
    push es
    mov si, wa_rsptx
    mov di, wa_sbbuf
    push ds
    pop es
    mov cx, 10
    cld
    rep movsb
    pop es
    pop di
    mov al, [wa_pdead]
    xor ah, ah
    add ax, 29
    mov cl, 30
    div cl
    add al, '0'
    mov [wa_sbbuf+9], al
    mov dx, 13
    mov bx, [wa_w]
    sub bx, 10
    shr bx, 1
    mov si, wa_sbbuf
    mov cx, 10
    mov ah, 0x0F
.c:
    lodsb
    call wa_ovput
    loop .c
    ret
wa_rsptx:   db 'RESPAWN  0'

; wa_ovatt - AH = a colour attribute: bright, on Hercules or MDA
wa_ovatt:
    cmp byte [wa_mono], 0
    je .out
    mov ah, 0x0F
.out:
    ret

; wa_ovput - AL = a glyph, AH = its attribute, at row DX, column BX: onto
; the overlay, in the part (ES; DI = its cells so far x 2, which moves on) -
; past the screen's edge or WA_MOV cells, dropped; BX moves on. Keeps every
; other register
wa_ovput:
    cmp bx, [wa_w]
    jae .skip
    cmp di, WA_MOV*2
    jae .skip
    mov [es:WA_RAT(wa_ovg)+di], ax
    push bx
    push si
    mov si, dx
    shl si, 1
    add bx, [wa_rowoff+si]      ; the cell, and x 2: the renderer's offset
    shl bx, 1
    mov [es:WA_RAT(wa_ov)+di], bx
    pop si
    pop bx
    inc di
    inc di
.skip:
    inc bx
    ret

; wa_ovnum - AX = a number, CX = its digits (3 at most), at row DX from
; column BX (BX moves on past them), [wa_ovat] its attributes (colour high,
; mono low): leading zeros are left as the view behind them. Clobbers AX, CX
wa_ovnum:
    push si
    or ah, ah
    jnz .big
    aam                         ; under 256 (as ever in play): three digits
    add al, '0'                 ; by two AAMs, not three 16-bit divides
    mov [wa_ovdig+3], al
    mov al, ah
    aam
    add ax, '00'
    mov [wa_ovdig+2], al
    mov [wa_ovdig+1], ah
.made:
    mov si, 4
    sub si, cx                  ; SI = its first digit's place in wa_ovdig
    mov ax, [wa_ovat]
    cmp byte [wa_mono], 0
    jne .m
    mov al, ah
.m:
    mov ah, al                  ; AH = the attribute
.lead:
    cmp si, 3
    jae .p                      ; (the last digit always shows)
    cmp byte [wa_ovdig+si], '0'
    jne .p
    inc bx                      ; a leading zero: the view
    inc si
    jmp .lead
.p:
    mov al, [wa_ovdig+si]
    call wa_ovput
    inc si
    cmp si, 4
    jb .p
    pop si
    ret
.big:                           ; (a test's 30000: its low digits, by divides)
    push bx
    push dx
    push cx
    mov bx, 10
    mov si, 4
.mk:
    dec si
    xor dx, dx
    div bx
    add dl, '0'
    mov [wa_ovdig+si], dl
    loop .mk
    pop cx
    pop dx
    pop bx
    jmp .made

%ifdef WA_BENCH                 ; (B and P: a test build's, WABENCH=1)
; --- the bench --------------------------------------------------------------
; B: 64 frames from the arena's middle, turning 1/64 of a circle each, the
; dummies frozen - the SAME work on every build and in either width, so two
; runs compare. The HUD then holds its averages until the next 16 frames.
WA_BENCHN equ 64
wa_bench:
    call wa_mtron
    push word [wa_px]
    push word [wa_py]
    push word [wa_ang]
    push word [wa_pitch]
    push word [wa_eye]
    mov word [wa_px], 1024
    mov word [wa_py], 700
    mov word [wa_pitch], 0
    mov word [wa_eye], WA_EYE
    mov byte [wa_pkdet], 1      ; (every pickup there, its first choice, no
    call wa_pkreset             ; rocket: the same scene every run)
    mov di, wa_figs             ; the dummies from their starts, frozen
    mov cx, WA_NFIG
.fig:
    call wa_spawn
    add di, F_SIZE
    loop .fig
    xor ax, ax
    mov [wa_trn], ax
    mov [wa_trn+2], ax
    mov [wa_tpr], ax
    mov [wa_tpr+2], ax
    mov [wa_ncells], ax
    mov [wa_ncells+2], ax
    mov [wa_bang], ax
    mov word [wa_nbench], WA_BENCHN
.frame:
    mov ax, [wa_bang]
    mov [wa_ang], ax
    add word [wa_bang], 65536 / WA_BENCHN
    call pit_now
    mov [wa_t0], ax
    mov [wa_t0+2], dx
    call wa_render
    call pit_now
    mov bx, ax
    mov cx, dx
    sub ax, [wa_t0]
    sbb dx, [wa_t0+2]
    add [wa_trn], ax
    adc [wa_trn+2], dx
    mov [wa_t0], bx
    mov [wa_t0+2], cx
    call wa_present
    call pit_now
    sub ax, [wa_t0]
    sbb dx, [wa_t0+2]
    add [wa_tpr], ax
    adc [wa_tpr+2], dx
    mov ax, [wa_nwr]
    add [wa_ncells], ax
    adc word [wa_ncells+2], 0
    dec word [wa_nbench]
    jnz .frame
    mov cx, 2                   ; 64 frames: quarter the sums, and the meter's
.q:                             ; divide-by-16 reads them as per-frame
    shr word [wa_trn+2], 1
    rcr word [wa_trn], 1
    shr word [wa_tpr+2], 1
    rcr word [wa_tpr], 1
    shr word [wa_ncells+2], 1
    rcr word [wa_ncells], 1
    loop .q
    mov ax, [wa_trn]            ; frame = render + present
    mov dx, [wa_trn+2]
    add ax, [wa_tpr]
    adc dx, [wa_tpr+2]
    mov [wa_tfr], ax
    mov [wa_tfr+2], dx
    pop word [wa_eye]
    pop word [wa_pitch]
    pop word [wa_ang]
    pop word [wa_py]
    pop word [wa_px]
    mov byte [wa_benched], 1
    ret

; --- the PLAY bench (P) --------------------------------------------------------
; What play costs, not what a spin costs (the owner, 2026-11-13: "the higher
; framerates of the benchmark come from spinning ... or from having the
; characters static"). A fixed pose in the full arena - mid-room, not in a
; corner - the dummies RUNNING their patrols from offsets chosen on the host so
; two or more are in view the whole time (tools-side search over the patrols:
; two or more at every sample of the run, three at a fifth of them), and the
; view moved in a moderate CIRCLE, yaw and pitch together, the way a hand
; circling the mouse moves it - driven by the sim's clock, so it moves at the
; same speed whatever the frame rate. 8 s; the HUD then holds the run's
; averages, marked P, until a key. It runs on any machine the game does: the
; 5150, 86Box, MartyPC alike.
WA_PBT      equ 240             ; ticks: 8 s
WA_PB_YAW   equ 17 * 64         ; the circle: +-6 degrees of yaw (ang16)...
WA_PB_PIT   equ 16              ; ...+-2 rows of pitch (quarter half-rows)
WA_PB_STEP  equ 23              ; ...once round every ~1.5 s (1024 / 45)
wa_pbstart:
    call wa_mtron
    mov si, [wa_gpbp]           ; THE ARENA'S VIEWS (ARENAS-PLAN 4.2), the
    lodsw                       ; next each run: P's one in arenas 0 and 1,
    mov bl, [wa_pbsel]          ; the heights arena's A to E (HEIGHTS-PLAN
    cmp bl, al                  ; 9), the hedge's and the twin's own -
    jb .sel                     ; arenagen's
    xor bl, bl
.sel:
    mov bh, bl
    inc bh
    mov [wa_pbsel], bh
    mov al, WA_PBSZ
    mul bl
    add si, ax
    lodsw
    mov [wa_px], ax
    lodsw
    mov [wa_py], ax
    lodsw
    mov [wa_pba], ax
    mov [wa_ang], ax
    lodsw
    mov [wa_pbp], ax
    lodsw
    mov [wa_feet], ax
    lodsw
    mov [wa_pbm], al
    xor ax, ax
    mov [wa_pxf], ax
    mov [wa_pyf], ax
    mov [wa_jz], ax
    mov [wa_jv], ax
    mov [wa_pitch], ax
    mov [wa_pbtk], ax
    mov word [wa_fpx], -1       ; (the floor: read afresh)
    mov [wa_pbfr], ax
    mov ax, WA_EYE
    add ax, [wa_feet]
    mov [wa_eye], ax
    mov byte [wa_pkdet], 1      ; (every pickup there, its first choice, no
    call wa_pkreset             ; rocket; none taken while it runs:
                                ; wa_pktick)
    mov byte [wa_bdet], 1       ; (the dummies walk their home loops)
    mov di, wa_figs             ; the dummies from their starts, then on to
    mov si, wa_pbph             ; the bench's points along their patrols
    mov cx, WA_NFIG
.fig:
    push cx
    call wa_spawn
    lodsw
    mov cx, ax
    jcxz .placed
.walk:
    push cx
    push si
    call wa_figtick
    pop si
    pop cx
    loop .walk
.placed:
    pop cx
    add di, F_SIZE
    loop .fig
    xor ax, ax                  ; the sums, from here
    mov [wa_tfr], ax
    mov [wa_tfr+2], ax
    mov [wa_trn], ax
    mov [wa_trn+2], ax
    mov [wa_tpr], ax
    mov [wa_tpr+2], ax
    mov [wa_ncells], ax
    mov [wa_ncells+2], ax
    mov [wa_acc], ax            ; and no sim debt carried in
    mov [wa_acc+2], ax
    mov byte [wa_hold], 0
    mov byte [wa_pbon], 1
    mov si, wa_hudtpl           ; the HUD's numbers cleared while it runs,
    call wa_hudfix              ; so the screen shows only the run's result
    mov al, [wa_pbm]
    mov di, 32
    call wa_putc
    ret
wa_pbph:    dw 160, 64, 80      ; ticks along each patrol (the host search)
; the benches' poses: x, y, angle (ang16), pitch, the floor under the eye,
; the HUD's mark. P's own, then the heights arena's (HEIGHTS-PLAN 9): A the
; busiest floor view, B the open floor, C on the platform, D on the tower
; looking down, E in the annex's north hallway looking west (HEIGHTS-PLAN
; 7.4: the revised arena)
WA_PBSZ     equ 12              ; a view: x, y, angle, pitch, floor, mark

; wa_pbview - the circle: yaw and pitch at the play bench's clock
wa_pbview:
    mov ax, [wa_pbtk]
    mov cx, WA_PB_STEP
    mul cx
    call wa_sincos              ; AX = sin, DX = cos (Q14)
    push ax
    mov ax, dx
    mov cx, WA_PB_YAW
    imul cx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add dx, [wa_pba]
    mov [wa_ang], dx
    pop ax
    mov cx, WA_PB_PIT
    imul cx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add dx, [wa_pbp]
    mov [wa_pitch], dx
    ret

; wa_pbmeter - each frame of the play bench: sum it; at its end, the averages
; over every frame of the run into the HUD, marked P, and the meter held
wa_pbmeter:
    mov ax, [wa_nwr]
    add [wa_ncells], ax
    adc word [wa_ncells+2], 0
    inc word [wa_pbfr]
    cmp word [wa_pbtk], WA_PBT
    jae .done
    ret
.done:
    mov byte [wa_pbon], 0
    mov byte [wa_bdet], 0       ; (the bots roam again - the ones the setup
    call wa_bpark               ; asked for: the bench woke all three)
    mov cx, [wa_pbfr]
    mov si, wa_tfr
    call wa_avgn                ; AX = ms x 10 a frame
    push ax
    mov bx, ax
    mov ax, 0x86A0              ; 100,000 / ms10 = fps x 10
    mov dx, 1
    cmp bx, 2
    jae .div
    mov bx, 2
.div:
    div bx
    mov di, 4
    call wa_put10
    pop ax
    mov di, 12
    call wa_put10
    mov cx, [wa_pbfr]
    mov si, wa_trn
    call wa_avgn
    mov di, 19
    call wa_put10
    mov cx, [wa_pbfr]
    mov si, wa_tpr
    call wa_avgn
    mov di, 26
    call wa_put10
    mov ax, [wa_ncells]
    mov dx, [wa_ncells+2]
    div word [wa_pbfr]
    mov di, 33
    mov cx, 3
    call wa_putn
    mov al, [wa_pbm]
    mov di, 32
    call wa_putc
    mov byte [wa_hold], 1
    xor ax, ax
    mov [wa_nfr], ax
    ret

; wa_avgn - SI -> a 32-bit sum of 838 ns counts, CX = frames: AX = the
; average a frame in tenths of a ms (sum x 549 / 65536 / frames, 32-bit)
wa_avgn:
    mov ax, [si]
    mov bx, 549
    mul bx
    push dx
    mov ax, [si+2]
    mul bx
    pop bx
    add ax, bx
    adc dx, 0
    div cx
    ret

%endif

; --- the meter ------------------------------------------------------------
; Every 16 frames: average frame, render and present times, and cells
; written, into the HUD row.
wa_meter:
%ifdef WA_BENCH
    cmp byte [wa_pbon], 0
    je .norm
    jmp wa_pbmeter
%endif
.norm:
    mov ax, [wa_nwr]            ; video writes this frame
    add [wa_ncells], ax
    adc word [wa_ncells+2], 0
%ifdef WA_BENCH
    cmp byte [wa_benched], 0
    je .count
    mov byte [wa_benched], 0
    mov byte [wa_benchm], 1
    mov byte [wa_hold], 1       ; and keep them there until a key moves on
    jmp .show                   ; the bench's sums, as they stand
.count:
%endif
    cmp byte [wa_hold], 0
    je .live
    ret
.live:
    inc word [wa_nfr]
    cmp word [wa_nfr], 16
    jae .show
    ret
.show:
    mov si, wa_tfr
    call wa_avg_ms10            ; AX = ms x 10
    push ax
    mov bx, ax
    mov ax, 0x86A0              ; 100,000 / ms10 = fps x 10
    mov dx, 1
    or bx, bx
    jz .z
    cmp bx, 2
    jae .div
.z:
    mov bx, 2
.div:
    div bx
    mov di, 4
    call wa_put10
    pop ax
    cmp byte [wa_mtr], 2        ; (the frame rate alone: F10's first press)
    jb .rate
    mov di, 12
    call wa_put10
    mov si, wa_trn
    call wa_avg_ms10
    mov di, 19
    call wa_put10
    mov si, wa_tpr
    call wa_avg_ms10
    mov di, 26
    call wa_put10
    mov ax, [wa_ncells]         ; cells a frame, the average
    mov dx, [wa_ncells+2]
    mov cx, 16
    div cx
    mov di, 33
    mov cx, 3
    call wa_putn
    call .zero
    jmp .marks
.rate:
.zero:
    xor ax, ax
    mov [wa_nfr], ax
    mov [wa_tfr], ax
    mov [wa_tfr+2], ax
    mov [wa_trn], ax
    mov [wa_trn+2], ax
    mov [wa_tpr], ax
    mov [wa_tpr+2], ax
    mov [wa_ncells], ax
    mov [wa_ncells+2], ax
    ret
.marks:
    mov bl, [wa_fov]            ; the field of view: 9, 7 or 6 (tens)
    xor bh, bh
    mov al, [wa_fovc+bx]
    mov di, 36
    call wa_putc
    mov al, [wa_level]          ; the detail level, 1..5
    add al, '1'
    call wa_putc
    mov al, '-'                 ; G: the floor grid
    cmp byte [wa_grid], 0
    je .g
    mov al, 'G'
.g:
    call wa_putc
    mov al, 'N'                 ; B: these are the bench's figures
%ifdef WA_BENCH
    cmp byte [wa_benchm], 0
    je .bm
    mov al, 'B'
.bm:
    mov byte [wa_benchm], 0
%endif
    mov di, 32
    call wa_putc
    ret

; wa_avg_ms10 - SI -> a 32-bit sum of 838 ns counts over 16 frames: AX =
; the average in tenths of a millisecond (counts x 549 / 65536, in 32 bits:
; a frame of a whole second still reads true)
wa_avg_ms10:
    mov ax, [si]
    mov dx, [si+2]
    mov cx, 4
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    mov cx, 549
    push dx
    mul cx                      ; low word x 549: its high word carries
    mov bx, dx
    pop ax
    mul cx                      ; high word x 549: whole tenths x 65536
    add ax, bx
    ret

; wa_put10 - AX = a value in tenths, as "NNN.N" at HUD column DI (5 cells)
wa_put10:
    push ax
    xor dx, dx
    mov cx, 10
    div cx                      ; AX = whole, DX = tenth
    push dx
    mov cx, 3
    call wa_putn
    mov al, '.'
    call wa_putc
    pop ax
    add al, '0'
    call wa_putc
    pop ax
    ret

; wa_putn - AX as CX right-aligned digits at HUD column DI; DI moves on.
; Written from the right straight into the HUD's row (wa_hudp); a value under
; 256 - every one but a frame slower than a quarter second - gives up its
; digits by AAM, not a 16-bit divide each. Clobbers AX, CX, DX
wa_putn:
    push bx
    push si
    push es
    add di, cx                  ; DI = the column after them
    push di
    les si, [wa_hudp]
    add si, di
    add si, di                  ; ES:SI = that column's cell
    mov bx, 10
.d:
    dec di
    dec si
    dec si
    or ah, ah
    jnz .big
    aam                         ; AH = the rest, AL = its last digit
    mov dl, al
    mov al, ah
    xor ah, ah
    jmp .put
.big:
    xor dx, dx
    div bx
.put:
    cmp di, [wa_w]              ; (off the 40-column screen: dropped)
    jae .np
    add dl, '0'
    mov dh, 0x07
    mov [es:si], dx
.np:
    loop .d
    pop di
    pop es
    pop si
    pop bx
    ret

; wa_putc - AL at HUD column DI (row 24); DI moves on. Off the 40-column
; screen it is dropped.
wa_putc:
    cmp di, [wa_w]
    jae .skip
    push es
    push bx
    les bx, [wa_hudp]           ; the HUD's row in text memory (wa_setmode)
    add bx, di
    add bx, di
    mov [es:bx], al
    mov byte [es:bx+1], 0x07
    pop bx
    pop es
.skip:
    inc di
    ret

; wa_mtrinit - the meter's row for [wa_mtr] - 0 nothing, 1 the frame rate, 2
; every number: blanked, its fixed text, and the sums started again.
; wa_mtron - every number, if not already: a bench's result is its row
wa_mtron:
    cmp byte [wa_mtr], 2
    je wa_mtrinit.out
    mov byte [wa_mtr], 2
wa_mtrinit:
    mov word [wa_hscsig], 0xFFFF ; (the row blanked: the HUD's own fields
    mov word [wa_hwsig], 0xFFFF
    mov byte [wa_hmsgd], 1      ; written again, wa_hudrow)
    xor ax, ax
    mov [wa_tfr], ax
    mov [wa_tfr+2], ax
    mov [wa_trn], ax
    mov [wa_trn+2], ax
    mov [wa_tpr], ax
    mov [wa_tpr+2], ax
    mov [wa_nfr], ax
    mov [wa_ncells], ax
    mov [wa_ncells+2], ax
    mov [wa_hold], al
    xor di, di
    mov cx, [wa_w]
    mov al, ' '
.b:
    call wa_putc
    loop .b
    mov si, wa_hudfps
    cmp byte [wa_mtr], 1
    jb .out
    je wa_hudfix
    mov si, wa_hudtpl
    jmp wa_hudfix
.out:
    ret
wa_hudfps:  db 'FPS', 0

wa_hudfix:                      ; SI = the HUD's template, NUL-terminated
    xor di, di
.c:
    lodsb
    or al, al
    jz .out
    call wa_putc
    jmp .c
.out:
    ret

; --- the HUD row's own fields -------------------------------------------------
; Row 24 is the meter's when F10 asks for every number. Otherwise it carries
; THE WEAPONS at its right - 1 to 6, each in its pickup's colour where the
; player has it, dark grey where not, the one held inverse - and, with the
; meter off altogether, THE LAST KILL at its left ("COG killed YOU") for
; WA_HMSGT ticks. Each is written straight into text memory, as the meter's
; numbers are, and only when it changed: the weapons on a new (had, held),
; the message when one is posted or runs out. wa_mtrinit blanks the row and
; marks both to be written again.
WA_HMSGN    equ 26              ; the message's cells (8 + 8 + 8 and two)
WA_HMSGT    equ 120             ; its ticks: four seconds
;              pistol shotgun chaingun grenades rockets railgun
wa_hwcol:   db 0x0F,  0x06,   0x07,    0x0D,    0x0A,   0x0B
wa_hwinv:   db 0x70,  0x60,   0x70,    0x50,    0x20,   0x30
wa_hk_kill: db ' killed ', 0
wa_hk_died: db ' died', 0

; wa_hudrow - once a frame, before the render: the row's fields that
; changed. Clobbers AX, BX, CX, SI, DI
wa_hudrow:
    cmp byte [wa_sbon], 0       ; (Tab held: the minimap's dot moved to
    je .nomm                    ; another cell - the overlay again)
    mov ax, [wa_px]
    mov dx, [wa_py]
    call wa_mmcell
    cmp ax, [wa_mmkey]
    je .nomm
    mov byte [wa_ovd], 1
.nomm:
    cmp byte [wa_mtr], 2
    jae .out
    mov al, [wa_own]
    mov ah, [wa_wpn]
    cmp ax, [wa_hwsig]
    je .msg
    mov [wa_hwsig], ax
    call wa_hudwpn
.msg:
    call wa_hudsc               ; (the score against the goal)
    cmp byte [wa_hmsgd], 0
    je .out
    mov byte [wa_hmsgd], 0
    cmp byte [wa_mtr], 0        ; (the frame rate there: no message)
    jne .out
    xor di, di
    xor si, si
.c:
    mov ax, 0x0720
    cmp si, [wa_hmsl]
    jae .p
    mov al, [wa_hmsg+si]
    mov ah, [wa_hmsa+si]
.p:
    call wa_putca
    inc si
    cmp si, WA_HMSGN
    jb .c
.out:
    ret

; wa_hudsc - THE SCORE AGAINST THE GOAL, five cells left of the weapons:
; "12/20" - or with no goal (Win at none) the score alone. Free-for-all
; our kills, white; a team mode our team's score in its colour. Written
; when one of them changed. Clobbers AX, BX, CX, DX, DI
wa_hudsc:
    mov al, [wa_kills]          ; AL = the score, AH = its colour
    mov ah, 0x0F
    cmp byte [wa_gmode], 0
    je .g
    mov bl, [wa_tm]
    xor bh, bh
    mov al, [wa_caps+bx]
    mov ah, 0x0C
    or bl, bl
    jz .g
    mov ah, 0x09
.g:
    cmp byte [wa_mono], 0
    je .m
    mov ah, 0x0F
.m:
    mov dl, [wa_ggoal]          ; DL = the goal
    xor dh, dh
    cmp ax, [wa_hscsig]
    jne .draw
    cmp dx, [wa_hscsig+2]
    je .out
.draw:
    mov [wa_hscsig], ax
    mov [wa_hscsig+2], dx
    mov bl, ah                  ; BL = the colour
    mov di, [wa_w]
    sub di, WA_NWPN + 6
    or dl, dl
    jnz .goal
    mov cx, 0x0720              ; (no goal: three blanks, then the score)
    xchg ax, cx
    call wa_putca
    call wa_putca
    call wa_putca
    xchg ax, cx
    jmp short .sc
.goal:
    push dx
    call wa_put2                ; the score...
    mov ax, 0x072F              ; ...a slash...
    call wa_putca
    pop ax                      ; ...the goal
.sc:
    call wa_put2
.out:
    ret
; wa_put2 - AL = 0..99 (more: 99), BL = the attribute: two cells at HUD
; column DI, the tens blank under 10. Clobbers AX
wa_put2:
    cmp al, 99
    jbe .a
    mov al, 99
.a:
    aam                         ; AH = tens, AL = units
    push ax
    mov al, ah
    add al, '0'
    or ah, ah
    jnz .t
    mov al, ' '
.t:
    mov ah, bl
    call wa_putca
    pop ax
    add al, '0'
    mov ah, bl
    jmp wa_putca

; wa_hudwpn - the six weapons, at the row's last six columns. Clobbers AX,
; BX, CX, DI
wa_hudwpn:
    mov di, [wa_w]
    sub di, WA_NWPN
    xor bx, bx
.w:
    mov al, '1'
    add al, bl
    mov cl, bl
    mov ch, 1
    shl ch, cl
    test [wa_own], ch
    jnz .have
    mov ah, 0x08                ; not had: dark grey (on mono, nothing)
    cmp byte [wa_mono], 0
    je .p
    mov al, ' '
    jmp .p
.have:
    mov ah, [wa_hwcol+bx]
    cmp bl, [wa_wpn]
    jne .m
    mov ah, [wa_hwinv+bx]       ; held: inverse, in its colour
.m:
    cmp byte [wa_mono], 0
    je .p
    and ah, 0x70                ; (mono: plain, or inverse held)
    jnz .p
    mov ah, 0x07
.p:
    call wa_putca
    inc bx
    cmp bx, WA_NWPN
    jb .w
    ret

; wa_putca - AL at HUD column DI in the attribute AH; DI moves on. Off the
; 40-column screen it is dropped.
wa_putca:
    cmp di, [wa_w]
    jae .skip
    push es
    push bx
    les bx, [wa_hudp]
    add bx, di
    add bx, di
    mov [es:bx], ax
    pop bx
    pop es
.skip:
    inc di
    ret

; wa_hudkill - AL = the killer's code here, AH = the dead one's (0 the
; player, 1..3 the figures; the same code twice, a suicide): "KILLER killed
; DEAD" - or "DEAD died" - the HUD's message for the next WA_HMSGT ticks,
; the player's own name green - or in a team mode each name its team's
; colour. Keeps every register
wa_hudkill:
    push ax
    push bx
    push cx
    push si
    push di
    xor di, di                  ; DI = the cells so far
    cmp al, ah
    je .self
    push ax
    mov bl, al
    call wa_hudname
    mov si, wa_hk_kill
    call wa_hudtext
    pop ax
    mov bl, ah
    call wa_hudname
    jmp .set
.self:
    mov bl, ah
    call wa_hudname
    mov si, wa_hk_died
    call wa_hudtext
.set:
    mov [wa_hmsl], di
    mov byte [wa_hmsgt], WA_HMSGT
    mov byte [wa_hmsgd], 1
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
; wa_hudsay - SI = a message (NUL), AL = a character after it: the HUD's
; message for WA_HMSGT ticks, as a kill's is. Keeps every register but AX
wa_hudsay:
    push di
    xor di, di
    push ax
    call wa_hudtext
    pop ax
    mov [wa_hmsg+di], al
    mov byte [wa_hmsa+di], 0x0F
    inc di
    mov [wa_hmsl], di
    mov byte [wa_hmsgt], WA_HMSGT
    mov byte [wa_hmsgd], 1
    pop di
    ret
wa_hk_mouse: db 'Mouse sensitivity ', 0

; wa_hudset - SI = a setting's name (NUL), BX = what it is now (NUL): the
; HUD's message, the name grey and the value bright, for WA_HMSGT ticks -
; every setting a key changes in the match says so there (the owner,
; 2026-11-15: "right now I just have to see what changes"). Keeps every
; register but AX, BX and SI
wa_hudset:
    push di
    xor di, di
    call wa_hudtext
    mov si, bx
.v:
    lodsb
    or al, al
    jz .end
    mov [wa_hmsg+di], al
    mov byte [wa_hmsa+di], 0x0F
    inc di
    jmp .v
.end:
    mov [wa_hmsl], di
    mov byte [wa_hmsgt], WA_HMSGT
    mov byte [wa_hmsgd], 1
    pop di
    ret
wa_hs_detail:   db 'Detail ', 0
wa_hs_vsync:    db 'Vsync ', 0
wa_hs_cols:     db 'Columns ', 0
wa_hs_on:       db 'on', 0
wa_hs_off:      db 'off', 0
wa_hs_40:       db '40', 0
wa_hs_80:       db '80', 0
wa_hs_lev:      dw wa_hs_l0, wa_hs_l1, wa_hs_l2, wa_hs_l3, wa_hs_l4
wa_hs_l0:       db 'Very Low', 0
wa_hs_l1:       db 'Low', 0
wa_hs_l2:       db 'Medium', 0
wa_hs_l3:       db 'High', 0
wa_hs_l4:       db 'Very High', 0

wa_hudtext:                     ; SI = text (NUL), grey, at DI
    lodsb
    or al, al
    jz .out
    mov [wa_hmsg+di], al
    mov byte [wa_hmsa+di], 0x07
    inc di
    jmp wa_hudtext
.out:
    ret
wa_hudname:                     ; BL = a code: its name at DI, its trailing
    push bx                     ; spaces left off - the player's green,
    call wa_fname               ; the rest bright white
    pop bx
    mov ah, 0x0F
    or bl, bl
    jnz .a
    mov ah, 0x0A
.a:
    cmp byte [wa_gmode], 0      ; (a team mode: each name its team's
    je .a0                      ; colour, the player's too - red or blue)
    xor bh, bh
    mov ah, 0x0C
    cmp byte [wa_tm+bx], 0
    je .a0
    mov ah, 0x09
.a0:
    cmp byte [wa_mono], 0
    je .a1
    mov ah, 0x0F
.a1:
    mov cx, WA_NNAME            ; CX = its length, the trailing blanks off
.t:
    mov bx, cx
    cmp byte [si+bx-1], ' '
    ja .c
    loop .t
    ret
.c:
    lodsb
    mov [wa_hmsg+di], al
    mov [wa_hmsa+di], ah
    inc di
    loop .c
    ret

; --- the renderer part (docs/plans/completed/WIREARENA-PARTS-PLAN.md section 7) --------
; The renderer is part 0 of this file, apps/wirearena/wrpart.asm: its own
; segment, its own data, entered by far call with DS = that data. The names
; this file always called it by - wa_render, wa_present, wa_geominit,
; wa_loadarena - are the glue below, so the loop and the benches read as they
; did. What the renderer reads is copied into its FRAME BLOCK
; (apps/wirearena/wafb.inc) at a fixed offset, WR_FB; WA_RAT names a frame
; block variable's address there.

; wa_rbind - at entry, once op_load has the carve: bank the part's data
; segment and code selector and its verbs' offsets, after checking its header
; is the one this primary was built with. CF = 1 refused, with a toast.
wa_rbind:
    xor al, al
    call op_seg
    or ax, ax
    jz .bad
    mov es, ax
    cmp word [es:0], 'WR'
    jne .bad
    cmp word [es:2], 'PA'
    jne .bad
    cmp word [es:4], 'RT'
    jne .bad
    cmp word [es:WR_H_ABI], WR_ABI | (WR_NVERB << 8)
    jne .bad
    mov [wa_rds], ax
    xor bx, bx
.v:
    mov ax, [es:WR_H_VERB+bx]
    mov [wa_rverb+bx], ax
    inc bx
    inc bx
    cmp bx, WR_NVERB*2
    jb .v
    xor al, al
    call op_cseg
    mov [wa_rcs], ax
    mov ax, es                  ; THE KEYFRAMES come CODED (tools/
    mov si, [es:WR_H_POSE]      ; wadelta.py: ~7KB fewer on the disk):
    mov cx, WA_KHOT*WA_FRAMESZ  ; the hot ones in the renderer's data...
    call wa_undelta
    mov al, WA_PPOSE            ; the keyframes' part: its segment, for the
    call op_seg                 ; renderer's CONFIG (wa_rsync)
    mov [wa_posg], ax
    xor si, si                  ; ...and the pose part, whole
    mov cx, (WA_NFRAMES-WA_KHOT)*WA_FRAMESZ
    call wa_undelta
    push ds
    pop es
    clc
    ret
.bad:
    push ds
    pop es
    mov si, wa_t_part
    xor cx, cx
    call OSAPI_TOAST
    stc
    ret

; wa_undelta - AX = a segment, SI = an offset in it, CX = the bytes of
; keyframes there (whole ones, two or more): tools/wadelta.py's coding
; undone in place. A keyframe is NFACE facing rows of NJ (x, y) byte pairs;
; the file holds each keyframe less the one before, and in each, the first
; half's rows each less the row before and the second half as zeros - it is
; the first MIRRORED (the view turned half round: x negated, y the same,
; which wadelta checks). LZ packs that to under a third. So: the keyframes'
; running sum, then a keyframe at a time the rows' and the mirror. Once, at
; entry (~70 clk a byte: ~0.2 s on a 4.77 MHz 8088 for both parts, against
; ~7KB the floppy does not have to give). Keeps all but AX and the flags
wa_undelta:
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov ds, ax
    mov es, ax
    cld
    mov dx, si
    add dx, cx                  ; DX = the end
    push si
    mov di, si                  ; every keyframe plus the one before
    add di, WA_FRAMESZ
    sub cx, WA_FRAMESZ
    shr cx, 1
    call .sum
    pop si
.f:
    mov di, si                  ; its first half: each row plus the one
    add di, WA_NJ*2             ; before
    push si
    mov cx, (WA_FRAMESZ/2 - WA_NJ*2) / 2
    call .sum
    pop si                      ; (DI = its second half)
    mov cx, WA_FRAMESZ / 4
.m:
    lodsw                       ; ...the first's mirror
    neg al
    stosw
    loop .m
    mov si, di
    cmp si, dx
    jb .f
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    ret
.sum:                           ; CX pairs: [DI] += [SI], both on. A pair
    lodsw                       ; a loop: the strides are even, so the
    add [di], al                ; two bytes read are summed already (~40
    inc di                      ; clk a byte, not ~55)
    add [di], ah
    inc di
    loop .sum
    ret
%if WA_KHOT < 2 || WA_NFRAMES - WA_KHOT < 2 || WA_FRAMESZ != WA_NFACE*WA_NJ*2
 %error "wa_undelta: two keyframes or more a part, NFACE rows of NJ pairs"
%endif

; wa_sinmk - at entry, before anything turns: wa_sin is shipped as its
; second difference (tools/wadelta.py --sin; ~470 bytes fewer packed - a
; quarter sine's is 0 or -1 nearly everywhere), and two running sums give
; back the table arenagen made: the first difference from the second (from
; entry 2 on), then the table (from 1). ~32 clk a word, once. Image words,
; so an instance's own. Keeps all but AX, CX, SI and the flags
wa_sinmk:
    cld
    mov si, wa_sin + 2
    mov cx, 255
    call .sum
    mov si, wa_sin
    mov cx, 256
.sum:
    lodsw                       ; [SI] += the word before it
    add [si], ax
    loop .sum
    ret

; wa_partsmv - the parts standard's hook (OP_ONMOVE, SPEC.md 20.12.12): the
; carve moved, op_seg is right again, and the two words wa_rbind banked are
; asked for anew. Safe because the renderer part calls no kernel slot: no
; claim - so no move - happens while this task stands in it. A relocation
; proc: registers are the kernel's to restore
wa_partsmv:
    xor al, al
    call op_seg
    mov [wa_rds], ax
    xor al, al
    call op_cseg
    mov [wa_rcs], ax
    mov al, WA_PPOSE            ; (the keyframes moved with the carve: their
    call op_seg                 ; segment here, and in the part's CONFIG
    mov [wa_posg], ax           ; now - not at the next mode set)
    mov es, [wa_rds]
    mov [es:WA_RAT(wa_posg)], ax
    ret

; wa_rcall - BX = a verb (WRV_*): far-call it in the renderer part, DS = its
; data for the call. AX..DI and BP as the verb takes them; out its AX and
; flags, everything else the verb's to clobber, ES included. Built as
; push/push/RETF so no vector is read after DS has moved (apps/xtmp3/
; x3pars.inc's x3p_fcall)
wa_rcall:
    ; STKBALANCE-OK: a far CALL built by hand - the return (CS:wa_rret) and
    ; the target are pushed and RETF enters the part, whose own RETF comes
    ; back to wa_rret, which pops the DS pushed before them
    push ds
    push cs
    push word [wa_rretv]
    push word [wa_rcs]
    push word [wa_rverb+bx]
    mov ds, [wa_rds]
    retf
wa_rret:
    ; STKBALANCE-OK: the far return of wa_rcall's verb lands here, one push
    ; deep, and this pops it
    pop ds
    ret
wa_rretv:   dw wa_rret

; wa_rsync - the CONFIG section into the renderer part (after a mode set, a
; field-of-view change). Keeps every register but the flags.
; THE DETAIL LEVELS (docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md 9.11): per level,
; Very Low to Very High, the options it turns on - wa_look's LK_ bits, then
; the floor grid and the stepped treads (their G and T keys gone: NET-PLAN
; 9.9), and the
; floor lines' DASHES (wa_dash, ARENAS-PLAN 4.5: 1 a shallow line's every
; other column, 2 a steep one's every other row too - the owner's, on the
; trial build: "actually look surprisingly fine"). Moving
; an option between levels is a change to this table and nothing else.
; Last, the size under which a figure is drawn SMALL (wa_flodv, J's
; threshold: ARENAS-PLAN 5) - CONFIG's, so a level's change syncs it.
WA_NLEV equ 5
%if WA_NLEV != 5
  %error "wa_hs_lev (the HUD's names for the levels) names five"
%endif
wa_levtab:
    db LK_FEW|LK_ROUND|LK_COARSE|LK_COARSE2|LK_NOPOST|LK_ROOMF|LK_FARTOP|LK_RAMP2, 0, 0, 2, WA_FLODV2
                                                                    ; 1 Very Low
    db LK_FEW|LK_ROUND|LK_COARSE|LK_COARSE2|LK_NOPOST|LK_RAMP2, 0, 0, 1, WA_FLODV2 ; 2 Low
    db LK_FEW|LK_ROUND|LK_COARSE, 0, 0, 0, WA_FLODV2                ; 3 Medium
    db LK_FEW|LK_ROUND|LK_COARSE, 1, 0, 0, WA_FLODV                 ; 4 High
    db 0, 1, 1, 0, WA_FLODV                                         ; 5 Very High

; wa_setlevel - AL = a level, 0..WA_NLEV-1: its options into the frame
; block. Clobbers AX, BX
wa_setlevel:
    mov [wa_level], al
    xor ah, ah
    mov bx, ax
    shl ax, 1
    shl ax, 1
    add bx, ax                  ; (five bytes a level)
    mov al, [wa_levtab+bx]
    mov [wa_look], al
    mov al, [wa_levtab+bx+1]
    mov [wa_grid], al
    mov al, [wa_levtab+bx+2]
    mov [wa_treads], al
    mov al, [wa_levtab+bx+3]
    mov [wa_dash], al
    mov al, [wa_levtab+bx+4]
    mov [wa_flodv], al
    ret

wa_rsync:
    push cx
    push si
    push di
    push es
    mov es, [wa_rds]
    mov si, wa_fb_cfg
    mov di, WA_RAT(wa_fb_cfg)
    mov cx, (wa_fb_frm - wa_fb_cfg) / 2
    cld
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    ret

; wa_rframe - the FRAME section and the head of each figure record (bytes
; 0..19: where it is, which way it faces, its animation, its flash, whether
; it is dead, the floor it stands on) into the renderer part: three runs, the first taking FRAME and
; figure 0 together - FRAME's camera sin and cos made here first; then the
; lift's height, and the rockets while any is live. Clobbers AX, BX, CX,
; DX, SI, DI, ES (= the part) - its three callers keep nothing across it
WA_FHW equ WA_FIGHEAD/2
wa_rframe:
    mov ax, [wa_ang]            ; the camera's sin and cos, into FRAME (the
    or al, 0x3F                 ; part has no table) - wa_camsc's own test
    cmp ax, [wa_fsa]            ; here: a view that has not turned costs
    je .sc                      ; no call and no load of what FRAME holds
    call wa_camsc.make
.sc:
    mov [wa_mmseg], ds          ; (our segment, for the minimap's bytes:
    mov es, [wa_rds]            ; the region may have moved since)
    cld
    mov si, wa_fb_frm
    mov di, WA_RAT(wa_fb_frm)
    mov cx, (wa_figs - wa_fb_frm) / 2 + WA_FHW
    rep movsw
%assign wa_i 1
%rep WA_NFIG-1
    mov si, wa_figs + wa_i*F_SIZE
    mov di, WA_RAT(wa_figs) + wa_i*F_SIZE
    mov cx, WA_FHW
    rep movsw
%assign wa_i wa_i+1
%endrep
    mov cx, [wa_nlift]          ; the lifts' heights, which the sim moves
    jcxz .nolift                ; (HEIGHTS-PLAN 3.2: ARENA's words that
    mov bx, wa_lift             ; go G -> R)
.lift:
    mov si, [bx]
    mov ax, [wa_bxh+si]
    mov [es:WA_RAT(wa_bxh)+si], ax
    inc bx
    inc bx
    loop .lift
.nolift:
    mov cl, [wa_rkn]            ; the rockets, while any is live and once
    mov ch, cl                  ; more after (WEAPONS-PLAN 2.6): no frame
                                ; without one pays their copy
    or cl, [wa_rkcp]
    mov [wa_rkcp], ch
    jz .norock
    mov si, wa_rks
    mov di, WA_RAT(wa_rks)
    mov cx, WA_NRK*WA_RKSZ/2
    rep movsw
.norock:
    cmp byte [wa_ctfcp], 0      ; THE TEAMS' BLOCK (CTF-PLAN 4): every frame
    je .noctf                   ; of a team match, once more as one starts
    jns .ctf                    ; or ends (wa_ctfstart: 80h once, 1 always)
    mov byte [wa_ctfcp], 0
.ctf:
    mov si, wa_fb_ctf
    mov di, WA_RAT(wa_fb_ctf)
    mov cx, (wa_fb_cend - wa_fb_ctf) / 2
    rep movsw
.noctf:
    ret

; wa_camsc - AX = sin, DX = cos (Q14) of the camera's angle, wa_ang's
; 1024-step one (the renderer's wa_cang): FRAME's wa_fsn and wa_fcs, made
; again only when that angle is another (wa_fsa: wa_ang with its fraction's
; bits set, so never 0, which is "none yet"). A frame and the ticks of the
; player's walk ask for the same one until the view turns. Keeps all but
; AX, CL, DX
wa_camsc:
    mov ax, [wa_ang]
    or al, 0x3F
    cmp ax, [wa_fsa]
    jne .make
    mov ax, [wa_fsn]
    mov dx, [wa_fcs]
    ret
.make:
    mov [wa_fsa], ax
    mov cl, 6
    shr ax, cl
    call wa_sincos
    mov [wa_fsn], ax
    mov [wa_fcs], dx
    ret

; wa_render - a frame: the frame state into the part, the renderer, and back
; its frame number (the sim's key poll, wa_tick). Each figure's F_PZ here
; then says only "drawn since it last spawned" (wa_spawn zeroes it): the
; projection itself stays in the part, where TRACE reads it. ES = DS out, as
; the renderer left it.
wa_render:
    cmp byte [wa_ovd], 0        ; the overlay, rebuilt (straight into the
    je .ov                      ; part) only when something it shows changed
    call wa_ovbuild
.ov:
    call wa_rframe
    mov bx, WRV_RENDER
    call wa_rcall
    mov [wa_frame], al
    push ds
    pop es
    mov ax, 1                   ; (one register's stores: 3 bytes each,
%assign wa_i 0                  ; where an immediate's are 6)
%rep WA_NFIG
    mov [wa_figs + wa_i*F_SIZE + F_PZ], ax
%assign wa_i wa_i+1
%endrep
    ret

; wa_present - the changed cells onto the glass, and how many (the meter)
wa_present:
    mov bx, WRV_PRESENT
    call wa_rcall
    mov [wa_nwr], ax
    push ds
    pop es
    ret

; wa_geominit - once an arena is loaded: the reciprocal table and the
; coordinate steps, in the part
wa_geominit:
    mov bx, WRV_GEOM
    call wa_rcall
    push ds
    pop es
    ret

; wa_loadarena - arena [wa_arena], a part of its own (WIREARENA-PARTS-PLAN
; 7.5): fetched, read into the renderer part's working tables (ARENA), its
; ARENA section - the boxes the collision walks, the start wa_reset takes -
; copied back into ours, and dropped again: both halves keep what they need,
; so no claim outlives the call. CF = 1: it could not be had, and the parts
; standard's toast has said why. UI task (a menu command): op_fetch's rule.
WA_PARTA equ 2                  ; arena 0's row; the renderer is row 0,
                                ; the keyframes row 1 (WA_PPOSE: the carve's
                                ; rows before every lazy one)
wa_loadarena:
    mov al, [wa_arena]
    add al, WA_PARTA
    push ax
    call op_fetch
    pop ax
    jc .out
    push ax
    call op_seg
    mov dx, ax
    mov bx, WRV_ARENA
    call wa_rcall
    push ds
    push ds
    pop es
    mov si, WA_RAT(wa_fb_arn)
    mov di, wa_fb_arn
    mov cx, (wa_fb_end - wa_fb_arn) / 2
    mov ds, [wa_rds]
    cld
    rep movsw
    pop ds
    mov ax, [wa_nbox]
    mov cl, 3
    shl ax, cl
    mov [wa_nbox8], ax
    call wa_lists               ; the boxes the floor and the walls read
    call wa_pkgridmk            ; ...and the cells its pickups reach
    call wa_flgmk               ; ...and its floor lines near
    pop ax                      ; ITS ROUTE GRAPH (AI-PLAN 3.1), straight
    push ax                     ; out of the part: descriptor words 38
    call op_seg                 ; and 40, its offset and bytes
    push ds
    push ds
    pop es
    mov ds, ax
    mov si, [38]
    mov cx, [40]
    mov di, wa_gph
    cld
    rep movsb
    pop ds
    mov al, [wa_gph]            ; ...and where its nodes and edges are
    xor ah, ah
    mov cl, 3
    shl ax, cl
    mov bx, wa_gph + 2 + WA_NFIG + (WA_NFIG & 1)
    mov [wa_gnp], bx
    add ax, bx
    mov [wa_gep], ax
    mov bx, ax                  ; ...and the bots' tables after the edges:
    mov al, [wa_gph+1]          ; the sight grid, the cells' nearest nodes,
    mov ah, WA_GESZ             ; the next hops (AI-PLAN 4.1, 3.2)
    mul ah
    add ax, bx
    mov [wa_gtp], ax
    add ax, WA_SGRID
    mov [wa_gcp], ax
    add ax, WA_PVSN
    mov [wa_ghp], ax
    mov bx, ax                  ; ...and the respawn points after the next
    mov al, [wa_gph]            ; hops (four a byte), word-aligned IN THE
    mul al                      ; BLOCK (wa_gph itself may sit at an odd
    add ax, 3                   ; address: the rounding is of the offset
    shr ax, 1                   ; from it)
    shr ax, 1
    add ax, bx
    sub ax, wa_gph
    inc ax
    and al, 0xFE
    add ax, wa_gph
    mov [wa_grp], ax
    mov bx, ax                  ; ...and the pickups' nodes after them
    mov ax, [bx]
    mov cl, 3
    shl ax, cl
    add ax, bx
    inc ax
    inc ax
    mov [wa_gpkn], ax
    mov bx, ax                  ; ...and the play bench's views after
    add ax, [bx]                ; those - a count, a byte each (word-
    inc ax                      ; aligned in the block, as the respawns
    inc ax                      ; are)
    sub ax, wa_gph
    inc ax
    and al, 0xFE
    add ax, wa_gph
    mov [wa_gpbp], ax
    mov bx, ax                  ; ...and the flags' bases after the views:
    mov ax, [bx]                ; a count, 12 bytes a view (CTF-PLAN 3)
    mov cl, 12
    mul cl
    add ax, bx
    inc ax
    inc ax
    mov [wa_gflp], ax
    mov bx, ax                  ; ...and the hill after the bases: a count,
    mov ax, [bx]                ; 6 bytes a base (MODES-PLAN 6)
    mov cl, 6
    mul cl
    add ax, bx
    inc ax
    inc ax
    mov [wa_ghlp], ax
    mov bx, ax                  ; ...and the minimap after the hill: a
    mov ax, [bx]                ; count, 8 bytes each (NET-PLAN 9.7)
    mov cl, 3
    shl ax, cl
    add ax, bx
    inc ax
    inc ax
    mov [wa_gmmp], ax
    mov bx, ax                  ; ...and its phase at 40 columns, after it
    mov al, [bx + WA_MMW / 2 * WA_MMH / 2]
    mov [wa_mmph], al
    call wa_sgrow
    pop ax
    call op_drop
    clc
.out:
    ret

%include "os88pit.inc"

; --- tables -----------------------------------------------------------------
%define WA_TAB_SIN               ; the tables this half reads (the renderer
                                ; part takes the arenas and the keyframes)
%include "watab.inc"

%include "walink.inc"           ; two machines over a null modem (LINK-PLAN)
%include "wactf.inc"            ; capture the flag, and its teams (CTF-PLAN)
%include "wasetup.inc"          ; the opening window: the match setup
%define OS88UI_DROP             ; the setup's drop-downs (SPEC.md 13.14)
%define OS88UI_CHK              ; the rotation's check boxes (SPEC.md 13.15)
%define OS88UI_NOGRADIO         ; (check boxes only: no radio ring and dot,
                                ; 13.15.3 - ~150 bytes)
%define OS88UI_ABOUT            ; the About card: the keys (SPEC.md 12.2)
%define OS88UI_ABFIT            ; (thirteen lines on a CGA's content box)
%define OS88UI_ABLEFT           ; (the key table at the card's left margin)
%define OS88UI_ABPACK           ; (its lines end to end: no pointer table)
%include "os88ui.inc"           ; (its buttons)
%define OS88LINE_NOSET          ; (the field is typed into, never set,
%define OS88LINE_NORESYNC       ;  never written behind its back)
%include "os88line.inc"         ; the setup's address field (SPEC.md 83)
%if WR_CKSZ != OS88UI_CK_SIZE
 %error "wasetup.inc's WR_CKSZ is not os88ui's check box record"
%endif
%if wa_sdr_c - wa_sdr_a != OS88UI_DR_SIZE
 %error "WA_SDROP is not OS88UI_DR_SIZE bytes: os88ui writes every one"
%endif

wa_tpl:
    dw 119, 20, 402, 174        ; (wasetup.inc's WS_W x WS_H content)
    dw wa_ttl, wa_cls
    OS88_CLASS wa_cls, 0, PAINT, wa_paint, ONKEY, wa_lkkey, \
               ONCLOSE, wa_lkclose, ONTIMER, wa_lktimer, \
               ONCLICK, os88ui_btnclick, ONMOUSEUP, wa_sbtnup, \
               ONDRAG, wa_sbtndrag, ABOUT, wa_about

    OS88_MENUSET wa_menus, wa_name, wa_oncmd ; (the Link menu alone: the
        OS88_MENU wa_m_link, wa_i_link, 4   ; rest - Ready, the bots, their
    OS88_MENUSET_END wa_menus               ; skill, Ethernet - is the
                                            ; setup's, NET-PLAN 9.9)

wa_name:
wa_ttl:     db 'Wire Arena', 0
wa_m_link:  db 'Link', 0
wa_i_link:  dw wa_il_auto, wa_il_c1, wa_il_c2, wa_il_off
wa_il_auto: db 'Automatic', 0
wa_il_c1:   db 'Connect COM1', 0
wa_il_c2:   db 'Connect COM2', 0
wa_il_off:  db 'Disconnect', 0


;               0         1         2         3
;               0123456789012345678901234567890123456789
wa_hudtpl:  db 'FPS       MS      R      P      N       ', 0

TOASTSTR wa_t_small, 'Needs kern_big or mega'
TOASTSTR wa_t_part, 'Renderer part mismatch'
TOASTSTR wa_t_host, 'The host picks the arena'
TOASTSTR wa_t_ctfa, 'This arena has no flags'
TOASTSTR wa_t_rone, 'One arena stays in'

; --- the parts (SPEC.md 20.12.3): the renderer, with its BSS as zeros
; neither stored nor decoded (OP_ZFILL, 20.12.7.5), and an arena a row, read
; when a match starts and dropped again (wa_loadarena)
    OS88_PARTS_BEGIN 2 + WA_NARENA
      OS88_PART OP_SEG, OP_COMP | OP_ZFILL  ; 0 build/wrpart.bin
      OS88_PART OP_ASSET, OP_COMP           ; 1 build/wapose.bin: the figures'
%rep WA_NARENA                              ;   keyframes (PARTS-PLAN 8.6),
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP ; 2.. build/waa<n>.bin
%endrep                                     ;   eager, in the carve
    OS88_PARTS_END
WA_PPOSE    equ 1

; --- bss ------------------------------------------------------------------
; The parts standard's words first (OP_BSS, apps/os88parts.inc), then the
; FRAME BLOCK in wafb.inc's order (the renderer part holds the same layout at
; WR_FB), then this half's own.
%assign WA_BSS 0
%macro WA_VAR 2
%1 equ os88_image_end+OP_BSS+WA_BSS
%assign WA_BSS WA_BSS+%2
%endmacro
%macro WA_FBV 2
WA_VAR %1,%2
%endmacro
WA_FBLOCK
WA_VAR wa_win,2
WA_VAR wa_rds,2                  ; the renderer part: its data segment,
WA_VAR wa_rcs,2                  ;   its code selector (op_cseg)
WA_VAR wa_rverb,WR_NVERB*2       ;   and its verbs' offsets (wa_rbind)
WA_VAR wa_nwr,2                  ; video writes the last present made
WA_VAR wa_fsi,FSI_SIZE
WA_VAR wa_mode,1
WA_VAR wa_want,1
WA_VAR wa_quit,1
WA_VAR wa_vsync,1
WA_VAR wa_level,1                ; the detail level, 0..WA_NLEV-1 (keys 1..5)
WA_VAR wa_arena,1
WA_VAR wa_feet,2                 ; the floor under the player (HEIGHTS-PLAN
                                 ; 3.1): on the ground the feet, in the air
                                 ; the base wa_jz is above
WA_VAR wa_flr,2                  ; the floor under the player this tick,
WA_VAR wa_flrb,2                 ;   and its box's record (-1 the ground),
WA_VAR wa_fpx,2                  ;   read where the player stood (-1:
WA_VAR wa_fpy,2                  ;   read again)
WA_VAR wa_qx,2                   ; wa_floor's square: its middle and r
WA_VAR wa_qy,2
WA_VAR wa_qr,2
WA_VAR wa_nbox8,2                ; the boxes x 8 (wa_lists)
WA_VAR wa_ox,2                   ; where the player's step starts (wa_flcross)
WA_VAR wa_oy,2
WA_VAR wa_lon,WA_MLIFT*2         ; per lift: ticks someone has stood on
WA_VAR wa_loff,WA_MLIFT*2        ;   it, ticks it has stood empty
WA_VAR wa_allp,WA_LTSZ           ; wa_floor's box tables (wa_lists): every
WA_VAR wa_lowp,WA_LTSZ           ;   box and the low ones for the player,
WA_VAR wa_lowf,WA_LTSZ           ;   the low ones for a dummy's feet
WA_VAR wa_gph,WA_GPHSZ           ; the arena's route graph (AI-PLAN 3.1)...
WA_VAR wa_gnp,2                  ; ...its nodes...
WA_VAR wa_gep,2                  ; ...and its edges
WA_VAR wa_gtp,2                  ; the bots' tables: the sight grid, the
WA_VAR wa_gcp,2                  ;   cells' nearest nodes, the next hops
WA_VAR wa_ghp,2
WA_VAR wa_tkn,2                  ; ticks, for the bots' turns to look
WA_VAR wa_blook,2                ; ...the figure whose turn this tick is (0: none)
WA_VAR wa_diff,1                 ; the bots' difficulty: 0 easy..2 hard
WA_VAR wa_pdead,1                ; THE PLAYER DEAD: ticks to the respawn
WA_VAR wa_peye,2                 ;   (AI-PLAN 5), the eye sinking
WA_VAR wa_hurtby,1               ; who hurt the player: 0 itself, 1.. a bot
WC_BSS                          ; (CTF-PLAN: wactf.inc's)
WA_VAR wa_kills,4                ; THE SCORE: kills, deaths - YOU, the
WA_VAR wa_deaths,4               ;   three bots
WA_VAR wa_bname,4                ; the bots' names (wa_names' index)
WA_VAR wa_sbon,1                 ; Tab held: the scoreboard shown
WA_VAR wa_sord,4                 ; ...its order
WA_VAR wa_sbbuf,WA_SBW           ; ...a row of it
WA_VAR wa_grp,2                  ; the arena's respawn points...
WA_VAR wa_gpkn,2                 ; ...and its pickups' route nodes
WA_VAR wa_gpbp,2                 ; ...and its play bench's views
WA_VAR wa_killer,1               ; whose weapon: 0 the player, 1.. a bot
WA_VAR wa_bw,1                   ; a bot's shot: its weapon, accuracy,
WA_VAR wa_bacc2,1                ;   distance
WA_VAR wa_bdist,2
WA_VAR wa_bfig,2                 ; wa_blaunch's bot
WA_VAR lk_blk,LK_SIZE             ; THE LINK (walink.inc, os88cable.inc)
WA_VAR lk_sbuf,SER_BUFFER_SIZE     ; the lease's rings: in our own region
WA_VAR lk_crct,512                 ; the CRC's table
WA_VAR wa_sfoc,2                   ; THE SETUP (wasetup.inc): the focus (a
WA_VAR wa_splay,1                  ;   drop-down, or the field), Play (0
WA_VAR wa_abon,1                   ;   alone, 1 serial, 2 local, 3 internet),
WA_VAR wa_sblab,4                  ;   the About card up, the buttons' labels
WA_VAR wa_sbrects,16               ;   as drawn and their rects; what each
WA_VAR wa_sdsel,WS_NDR             ;   drop-down, the focus and the field
WA_VAR wa_sddis,WS_NDR             ;   were drawn as (0FFh: not yet)
WA_VAR wa_sdfoc,1
WA_VAR wa_sdfld,1
WA_VAR wa_sdnk,1                   ;   (the nick's field as drawn)
WA_VAR wa_sorg,4                   ;   the content's origin, as placed
WA_VAR wa_sfld,OS88LINE_SZ         ;   the address field, its text, and what
WA_VAR wa_saddr,16                 ;   it names: IP, port
WA_VAR wa_saddr6,6
WA_VAR wa_snfld,OS88LINE_SZ        ;   the nick's field and its text
WA_VAR wa_snick,WS_NKMAX
WA_VAR ud_blk,UD_SIZE              ; UDP (os88udp.inc)
WA_VAR wa_lkwant,1                 ; a network wanted: 2 local, 3 by address
WA_VAR wa_lktries,2                ; ...ticks it has been asked
WA_VAR rw_blk,RW_SIZE              ; the card (os88raw.inc)
WA_VAR wa_lknet,1                  ; 1: linking on the card, not the cable
WA_VAR wa_lkfrom,6                 ; the last frame's source
WA_VAR wa_lkpf,1                   ; the frame the cable was last read in
WA_VAR wa_lkfired,1                ; the guest fired since its last SNAP
WA_VAR wa_lkwhat,1                 ; wa_lktick: this tick SCOREs (1), SNAPs (2)
WA_VAR wa_hproj,1                  ; wa_hat: a projectile asks (a LINE its own height)
WA_VAR wa_figend,2                 ; alone: past the last bot the setup asked for
WA_VAR wa_lkclip,1                 ; wa_lkdraw: the clip taken this tick
WA_VAR wa_lkown,1                  ; wa_lkrows: a repaint of our own (no kernel white)
WA_VAR wa_lkfs,1                   ; wa_lkfig: the figure's slot
WA_VAR wa_lklift,WA_MLIFT*2        ; the guest: the host's lifts' heights
WA_VAR wa_lkli,1                   ; the host: the lift its next SNAP says
WA_VAR wa_pc,WP_SZ                 ; THE PEER BEING HEARD OR SENT TO: its
                                   ; record loaded (wa_pload), the names
WA_VAR wa_pt,WP_SZ*WL_NPEER        ; below its fields; the table of them
WA_VAR wa_lkp,1                    ; ...which one is loaded; how many there are
WA_VAR wa_lkpn,1
WA_VAR wa_lkld,1                   ; 1: a peer is loaded in wa_pc
wa_lkpeer   equ wa_pc+WP_ID
wa_lkpmac   equ wa_pc+WP_ADDR
wa_lkps     equ wa_pc+WP_PS
wa_lkpheard equ wa_pc+WP_PHEARD
wa_lkptag   equ wa_pc+WP_PTAG
wa_lklost   equ wa_pc+WP_LOST
wa_lkgone   equ wa_pc+WP_GONE
wa_lkrseen  equ wa_pc+WP_RSEEN
wa_lkheard  equ wa_pc+WP_HEARD
wa_lkdin    equ wa_pc+WP_DIN
wa_lkdout   equ wa_pc+WP_DOUT
wa_lkin     equ wa_pc+WP_IN
wa_lkqs     equ wa_pc+WP_QS
wa_lkqt     equ wa_pc+WP_QT
wa_lkattr   equ wa_pc+WP_ATTR
wa_lkbdin   equ wa_pc+WP_BDIN
wa_lkack    equ wa_pc+WP_ACK
wa_lkpslot  equ wa_pc+WP_SLOT
WA_VAR wa_lkid,4                   ; our identity
WA_VAR wa_lkst,1                   ; our lobby state (0, 1 ready, 2 in), its
WA_VAR wa_lkbeat,2                 ; OS ticks: our last HELLO
WA_VAR wa_lkarena,1                ; the match: the host's arena, difficulty,
WA_VAR wa_lkdiff,1                 ;   tag; the host's tag as the guest heard
WA_VAR wa_lktag,1                  ;   it; 1: go in (wa_lkstart)
WA_VAR wa_lkfull,1                 ; the window's rows all to draw
WA_VAR wa_bdet,1                 ; 1: the bots walk their home loops - and
WA_VAR wa_lkon,1                   ; 1: in a linked match, ITS HIGH BYTE:
                                   ; wa_bthink asks both in one compare
WA_VAR wa_lkseq,1                  ;   waiting, sent this pass, when
WA_VAR wa_lkqn,1
WA_VAR wa_lkq,WL_EVQ*WL_EVSZ
WA_VAR wa_lkev,WL_EVSZ
WA_VAR wa_lkfr,LK_MAXF             ; a body being built
WA_VAR wa_lkr,WR_SZ*WA_NFIG       ; each remote figure as its SNAP said
WA_VAR wa_lkpkk,1                  ; how we died (0..2)
WA_VAR wa_lkbots,1                 ; the lobby's bots; the match's
WA_VAR wa_lknb,1
WA_VAR wa_lkme,1                   ; our slot on the wire (0: the host)
WA_VAR wa_lknp,1                   ; the players in the match (slots 0..)
WA_VAR wa_lkrf,1                   ; the last SNAP's flags (its kill's kind)
WA_VAR wa_lkbo,1                   ; (the HSNAP's bot, counting)
WA_VAR wa_lkhby,1                  ; ...and who the other's last says hurt us
WA_VAR wa_lkbd,2                   ; the guest's hits on the two bots, and
WA_VAR wa_lkd,16                   ; deaths by cause: [dead][cause], codes
                                   ;   on the wire
WA_VAR wa_lkpnam,8                 ; its name
WA_VAR wa_lkln,WL_COLS+1           ; a window row, and the rows as drawn
WA_VAR wa_lkcache,WL_ROWS*WL_COLS
WA_VAR wa_grudge,WA_NFIG          ; each bot's grudge against the player, ticks
WA_VAR wa_tx,2                   ; a bot's target: where, its chest's
WA_VAR wa_ty,2                   ;   height (wa_btpos)
WA_VAR wa_tz,2
WA_VAR wa_sgrid,1024              ; the sight grid, a byte a 64-unit cell
WA_VAR wa_bshots,2               ; the bots' shots fired, and hits
WA_VAR wa_bhits,2
WA_VAR wa_lbot,WA_MLIFT*2        ; per lift (a byte, a word apart): bots
                                 ;   on it (riders), counted by the tick's
                                 ;   figures, read by the next's wa_lift1
WA_VAR wa_pbsel,1                ; the play bench: the heights arena's pose,
WA_VAR wa_pba,2                  ;   the angle and pitch it circles round,
WA_VAR wa_pbp,2                  ;   and its HUD mark
WA_VAR wa_pbm,1
WA_VAR wa_trig,1
WA_VAR wa_frame,1                ; the renderer's, after each frame (wa_tick)
WA_VAR wa_pxf,2
WA_VAR wa_pyf,2
WA_VAR wa_jz,2
WA_VAR wa_jv,2
WA_VAR wa_fsa,2                  ; the angle wa_fsn, wa_fcs are for (wa_camsc)
WA_VAR wa_hits,2
WA_VAR wa_tmp,2
WA_VAR wa_last,4
WA_VAR wa_acc,4
WA_VAR wa_t0,4
WA_VAR wa_tfr,4
WA_VAR wa_trn,4
WA_VAR wa_tpr,4
WA_VAR wa_ncells,4
WA_VAR wa_nfr,2
WA_VAR wa_bang,2
WA_VAR wa_nbench,2
WA_VAR wa_benched,1
WA_VAR wa_benchm,1
WA_VAR wa_pbon,1
WA_VAR wa_pbtk,2
WA_VAR wa_pbfr,2
WA_VAR wa_hold,1
WA_VAR wa_mtr,1                  ; the meter (F10): 0 off, 1 the frame rate, 2
                                 ; every number
WA_VAR wa_rowoff,(WA_ROWS+1)*2
WA_VAR wa_hudp,4                 ; the HUD's row in text memory, offset:segment
WA_VAR wa_gmmp,2                 ; -> the arena's minimap (in wa_gph)
WA_VAR wa_mmkey,2                ; ...the player's cell on it, as drawn
WA_VAR wa_mmf,4                  ; ...the bases' (FFFFh: none shown)
WA_VAR wa_mmw,1                  ; ...its cells across
WA_VAR wa_mmph,1                 ; ...its phase at 40 columns (arenagen's)
WA_VAR wa_mmo0,2                 ; ...its first cell (x 2)
WA_VAR wa_mma,WA_MMCAP           ; ...and its block's bytes (the renderer
                                 ; reads them here: wafb.inc's wa_mmseg)
WA_VAR wa_hwsig,2                ; ...its weapons as written (had, held)
WA_VAR wa_hmsl,2                 ; ...its message: cells, ticks left, to be
WA_VAR wa_hmsgt,1                ;   written again, characters, attributes
WA_VAR wa_hmsgd,1
WA_VAR wa_hmsg,WA_HMSGN
WA_VAR wa_hmsa,WA_HMSGN
WA_VAR wa_msens,1                ; the mouse's sensitivity: -4..4 about the
                                 ;   first one (the session's: [ and ])
WA_VAR wa_paused,1               ; P: offline, the sim stands
WA_VAR wa_ask,1                  ; Esc in a linked match: "Leave?" up
WA_VAR wa_pprot,1                ; SPAWN PROTECTION: the player's ticks of
                                 ;   it (the bots have none: the owner's)
WA_VAR wa_hdir,1                 ; THE HIT'S DIRECTION (a prototype): its
WA_VAR wa_hdirt,1                ;   glyph's index (1 left, 2 right, 3
                                 ;   behind) and ticks left
WA_VAR wa_hscsig,4               ; the HUD's score as written
WA_VAR wa_nick,WA_NNAME          ; THE PLAYER'S NICK, space-padded (the
                                 ;   setup's field: blank, YOU)
WA_VAR wa_kbits,2                ; the held keys this frame (wa_keys)
WA_VAR wa_kfr,1                  ; ...polled in this frame
WA_VAR wa_kmap,KEYMAP_BYTES      ; ...and the whole map they came from
WA_VAR wa_hp,2                   ; THE PLAYER (WEAPONS-PLAN 2.3): health,
WA_VAR wa_sh,2                   ;   shield
WA_VAR wa_wpn,1                  ; the weapon held (0..2)...
WA_VAR wa_own,1                  ; ...those had, a bit each...
WA_VAR wa_mag,6                  ; ...each's magazine...
WA_VAR wa_res,6                  ; ...and reserve (the pistol's unused)
WA_VAR wa_phit,1                 ; ticks of the heart's hit flash, and - ITS
WA_VAR wa_beam,1                 ;   HIGH BYTE - those the railgun's beam has
                                 ;   left: wa_wtick asks both in one compare
WA_VAR wa_lt,1                   ; wa_launch's type: 0 rocket, 1 grenade
WA_VAR wa_seed,2                 ; the weapon spawns' dice
WA_VAR wa_pkdet,1                ; 1: each spawn its first choice (benches)
WA_VAR wa_rld,1                  ; ticks of a reload left (0: none), and - ITS
WA_VAR wa_cool,1                 ;   HIGH BYTE - ticks before the next shot
WA_VAR wa_ovd,1                  ; the overlay wants rebuilding
WA_VAR wa_ovat,2                 ; wa_ovnum's attributes
WA_VAR wa_ovdig,4                ; ...and its digits (3 at most, from +1)
WA_VAR wa_spk,2                  ; the weapon's cells kept (wa_ovbuild): the
WA_VAR wa_spn,2                  ;   weapon and its reload they are for, how
WA_VAR wa_spc,WA_SPMAX*2         ;   many, their offsets and their glyphs
WA_VAR wa_spg,WA_SPMAX*2
WA_VAR wa_pkt,WA_MPK*2           ; a taken pickup's ticks to its return
WA_VAR wa_pkgrid,256             ; the pickups in reach of each 128-unit cell
WA_VAR wa_flg,256*WA_FLGC        ; the floor lines near each (wa_flgmk)
WA_VAR wa_fsb,8                  ; wa_flcross: the step's box
WA_VAR wa_rkg,WA_NRK*WA_RKGSZ    ; the rockets' fractions, velocity, life
WA_VAR wa_rkcp,1                 ; a rocket was live at the last copy
WA_VAR wa_rkn,1                  ; rockets live (flying or exploding)
    OS88_BSS OP_BSS+WA_BSS
    OS88_IMAGE_END
