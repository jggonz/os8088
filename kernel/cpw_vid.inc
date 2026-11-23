; =============================================================================
; os8088 - cpw_vid.inc
;
; The wide Control Panel's Display page - the mode changer (SPEC.md 135.9).
; %included by cpwide.inc after the widget kit, into CPWIDE.DRV's image.
; Prefix `cpv_`. kern_big, kern_emu and kern_mega only (cpwide.inc's %ifdef).
;
; IT READS WHAT THE HARDWARE OFFERS AND ENCODES NO LIST (SPEC.md 135.9): the
; adapters are [vid_avail]'s bits, the modes VESA.DRV's VXV_INFO records
; (SPEC.md 120.10), one call a record, through CPG_VXCALL.
;
; ITS STATE is the working area's page area (CPW_W_PAGE, SPEC.md 135.5) plus
; one byte, CPV_LIVE: 1 while that area is this page's. cpwide.inc's cpw_page
; clears it whenever another page is drawn, so coming back to Display builds
; the page again from the running mode, and a repaint of Display - every
; vid_switch's, through cp_repost - keeps the selection and a running test.
;
; NOTHING IS DRAWN AFTER CPG_VIDSET OR CPG_VIDARR (SPEC.md 31.10, 135.9.2):
; the switch re-fits every window and cp_repost hands the screen to ui_task,
; whose repaint comes back here as CWV_PAINT with the new origin.
; =============================================================================

section .modv

; --- the state ---------------------------------------------------------------
CPV_LIVE    equ CPW_W_FULL + 1  ; db 1 = the page area is this page's (the
%if CPV_LIVE >= CPW_W_KIT       ;    working area's pad byte, SPEC.md 135.9.2)
  %error "cpw_vid: CPV_LIVE needs the pad byte between CPW_W_FULL and the kit"
%endif
CPV_TB      equ CPW_W_PAGE + 0  ; the table's block (cpk_table, CPKT_SIZE)
CPV_FLT     equ CPW_W_PAGE + 18 ; db the filter tab
CPV_RSV     equ CPW_D_ON        ; DEAD: CPW_D_ON is a KEPT byte (135.5.1) now -
%if CPV_RSV != CPW_D_ON         ;    cpw_listev reads on any page
  %error "cpw_vid: CPV_RSV must be CPW_D_ON's byte"
%endif
CPV_ASEL    equ CPW_W_PAGE + 20 ; db the adapter chosen (a VID_* kind)
CPV_NREC    equ CPW_W_PAGE + 21 ; db VXV_INFO's record count (0 = none)
CPV_RUN     equ CPW_W_PAGE + 22 ; db the running record, FFh none
CPV_REC     equ CPW_W_PAGE + 23 ; db Auto's record, FFh none
CPV_NTAB    equ CPW_W_PAGE + 24 ; db the filter's tabs
CPV_TABD    equ CPW_W_PAGE + 25 ; 4 db: a tab's depth (0 = All)
CPV_LEFT    equ CPW_W_PAGE + 29 ; db a test's seconds left, 0 = no test
CPV_SKIND   equ CPW_W_PAGE + 30 ; db what ran before it: the kind...
CPV_SCLS    equ CPW_W_PAGE + 31 ; db ...the VESA class...
CPV_SARR    equ CPW_W_PAGE + 32 ; db ...the arrangement (0/1/2)
CPV_CLS     equ CPW_W_PAGE + 33 ; 16 db: each record's class, bit 5 = 15bpp
CPV_ROWS    equ CPW_W_PAGE + 49 ; 17 db: the rows - a record, FFh = Auto
CPV_GX      equ CPW_W_PAGE + 66 ; dw the chosen mode's X (the preview's)...
CPV_GY      equ CPW_W_PAGE + 68 ; dw ...its Y
CPV_GD      equ CPW_W_PAGE + 70 ; db ...its depth
%if CPV_GD + 1 > CPW_W_PAGE + CPW_W_PAGEN
  %error "cpw_vid: the page's state outgrew CPW_W_PAGE"
%endif
CPV_MSEL    equ CPV_TB + CPKT_SEL   ; the table's selected row IS the block's
CPV_NMAX    equ 16              ; records kept (VESA.DRV's VX_NMODES)

; --- the geometry, content-relative (SPEC.md 135.9.2) -------------------------
CPV_L       equ CPW_PX + 4      ; the page's left
CPV_AY      equ 48              ; the Adapter and Mode preview groups' top...
CPV_AB      equ 122             ; ...and bottom
CPV_AR      equ CPV_L + 255     ; the Adapter group's right
CPV_PX      equ CPV_L + 260     ; the Mode preview group's left
CPV_ZW      equ 80              ; its picture zone's width
CPV_BY      equ 127             ; the Modes group's top
CPV_TY      equ CPV_BY + 14     ; ...its filter strip
CPV_HY      equ CPV_BY + 30     ; ...its header line
CPV_RY      equ CPV_BY + 42     ; ...its table
CPV_RH      equ 12              ; a ROW (cpv_row): 2 + 8 + 2
CPV_CR      equ CPV_L + 211     ; the controls' right
CPV_IX      equ CPV_L + 216     ; the Running mode / Keep group's left
CPV_TEST    equ 15              ; Test...'s seconds

; control ids (os88ui_arm): CPW_ID0 + n, n the cpv_ctab row
CPV_ISTART  equ 0               ; Use this mode at startup
CPV_IFC     equ 1               ; Force Color
CPV_IAPPLY  equ 2
CPV_ITEST   equ 3
CPV_IKEEP   equ 4
CPV_IREV    equ 5
CPV_IARR    equ 6               ; 6, 7, 8: Single, Right, Below
CPV_NCTL    equ 9

; =============================================================================
; The edges (cpwide.inc dispatches here while [cp_sel] is CP_IVID)
; =============================================================================

; -----------------------------------------------------------------------------
; cpv_page - the page area, whole (cpw_page's dispatch)
; in:  ES = the claim, the origin current. out: nothing; spends AX..DI, BP
; -----------------------------------------------------------------------------
cpv_page:
    cmp byte [es:CPV_LIVE], 0
    jne .have
    xor ax, ax                  ; --- a fresh page: the running adapter,
    mov di, CPW_W_PAGE          ; the running mode, no filter, no test
    mov cx, CPW_W_PAGEN / 2
    cld
    rep stosw
    mov al, [vid_kind]
    mov [es:CPV_ASEL], al
    mov byte [es:CPV_LIVE], 1
    call cpv_build
    mov al, [es:CPV_RUN]        ; ...and the running record's row chosen
    call cpv_rowof
    mov [es:CPV_MSEL], ax
    jmp short .draw
.have:
    call cpv_build
.draw:
    call cpv_ground
    call cpv_adap
    call cpv_prev
    call cpv_modes
    mov al, 1
    call cpv_ctls
    jmp cpv_rbox

; -----------------------------------------------------------------------------
; cpv_small - cpw_small's dispatch: the window is under the face's size (a
; switch made it so). During a test the Keep this mode? group IS the page, at
; (4, 4), so a test into a small desktop can still be kept or reverted.
; out: CF = 0 drawn; CF = 1 no test - cpw_small draws its own line
; -----------------------------------------------------------------------------
cpv_small:
    call cpv_testing
    jc .r
    call cpv_wh                 ; the ground round the group
    xor ax, ax
    xor bx, bx
    mov cx, [es:CPW_W_CW]
    dec cx
    mov dx, 3
    call cpw_fill               ; above it
    mov bx, 4
    mov dx, 4 + 49
    mov cx, 3
    call cpw_fill               ; left of it
    mov ax, 4 + 264
    mov cx, [es:CPW_W_CW]
    dec cx
    call cpw_fill               ; right of it
    xor ax, ax
    mov bx, 4 + 50
    mov dx, [es:CPW_W_CH]
    dec dx
    call cpw_fill               ; below it
    call cpv_kbox
    clc
.r:
    ret

; cpv_testing - CF = 0 a test is running on this page; keeps all
cpv_testing:
    cmp byte [es:CPV_LIVE], 0
    je .no
    cmp byte [es:CPV_LEFT], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; cpv_big - CF = 0 the window holds the face (cpw_paint's own test); keeps all
cpv_big:
    cmp word [es:CPW_W_CW], CPW_W1 - 2
    jb .r
    cmp word [es:CPW_W_CH], CPW_H1 - TITLE_H - 1
.r:
    ret

; -----------------------------------------------------------------------------
; cpv_click - a press anywhere in the window (cpw_click's dispatch)
; in:  AX/BX = the point, content-relative; CX/DX = the same, SCREEN
; out: nothing. While a test runs the page is MODAL: Keep and Revert only
; -----------------------------------------------------------------------------
cpv_click:
    mov cx, ax                  ; CX/DX = the point, content-relative
    mov dx, bx
    call cpv_testing
    jnc .ctl                    ; only the Keep group's two buttons
    call cpv_big
    jb .r                       ; small, no test: nothing here is drawn
    cmp ax, CPW_LW
    jge .page
    jmp cpw_click.list          ; the page list, as every page has it
.page:
    cmp dx, CPW_BY
    jl .r                       ; the header strip
    cmp dx, CPV_AB
    jg .modes
    cmp dx, CPV_AY + CPK_BANDH
    jl .r
    cmp cx, CPV_L + 1
    jl .r
    cmp cx, CPV_AR - 1
    jg .r
    cmp dx, CPV_AY + CPK_BANDH + 4 * CPV_RH
    jge .ctl                    ; the desktop line's radios
    mov ax, dx                  ; --- an adapter row: which slot?
    sub ax, CPV_AY + CPK_BANDH
    mov bl, CPV_RH
    div bl
    call cpv_slot               ; AL = the kind in it
    jc .r
    cmp al, [es:CPV_ASEL]
    je .r
    mov [es:CPV_ASEL], al       ; another adapter: its rows, the preview,
    call cpv_build              ; the Modes group and the controls' greying
    mov al, [es:CPV_RUN]
    call cpv_rowof
    mov [es:CPV_MSEL], ax
    mov word [es:CPV_TB + CPKT_TOP], 0
    call cpv_arows
    call cpv_prev
    call cpv_modes
    xor al, al
    jmp cpv_ctls
.modes:
    cmp byte [es:CPV_ASEL], VID_VESA
    jne .ctl
    cmp dx, CPV_TY
    jl .r
    cmp dx, CPV_TY + CPK_TABH - 1
    jg .tbl
    sub cx, CPV_L + 4           ; --- a filter tab
    mov si, cpv_tlab
    call cpk_tabhit
    jc .r
    cmp al, [es:CPV_FLT]
    je .r
    mov [es:CPV_FLT], al
    call cpv_build              ; the rows the filter keeps
    xor ax, ax
    mov [es:CPV_TB + CPKT_TOP], ax
    mov [es:CPV_MSEL], ax       ; (Auto, the first row of every filter)
    call cpv_tabs
    mov bx, CPV_TB
    call cpk_table
    call cpv_prev
    xor al, al
    jmp cpv_ctls
.tbl:
    mov bx, CPV_TB              ; --- the table: a row, or the bar (acted on)
    call cpk_tbclick
    jc .ctl
    cmp ax, 0FFFFh
    je .r
    mov bx, [es:CPV_MSEL]
    cmp ax, bx
    je .r
    mov [es:CPV_MSEL], ax       ; the row losing the bar, the one gaining it
    call cpv_trowat
    xchg ax, bx
    call cpv_trowat
    call cpv_prev
    xor al, al
    jmp cpv_ctls
.ctl:                           ; --- a control: armed, drawn pressed, and it
    call cpv_hit                ; acts on the release (SPEC.md 13.8.3)
    jc .r
    call cpv_ctlok
    jc .r
    push ax
    add ax, CPW_ID0
    CPWC os88ui_arm
    pop ax
    mov ah, 1                   ; pressed
    jmp cpv_ctl1
.r:
    ret

; -----------------------------------------------------------------------------
; cpv_up - a wide control's release (cpw_edge's dispatch): it acts when the
; release is on the control the press armed
; in:  CX/DX = the SCREEN point. out: nothing
; -----------------------------------------------------------------------------
cpv_up:
    call cpw_org
    CPWC os88ui_fire            ; AX = what was armed
    sub ax, CPW_ID0
    jb .r
    cmp ax, CPV_NCTL
    jae .r
    push ax
    mov ax, cx
    sub ax, [es:CPW_W_OX]
    mov bx, dx
    sub bx, [es:CPW_W_OY]
    mov cx, ax
    mov dx, bx
    call cpv_hit                ; AX = the control under the release
    pop bx                      ; BX = the one armed
    jc .up
    cmp ax, bx
    je .act
.up:
    mov ax, bx                  ; released elsewhere: drawn up, nothing done
    xor ah, ah
    jmp cpv_ctl1
.r:
    ret
.act:
    cmp al, CPV_IAPPLY
    je .apply
    cmp al, CPV_ITEST
    je cpv_test
    cmp al, CPV_IREV
    je cpv_revert
    cmp al, CPV_IKEEP
    je .keep
    cmp al, CPV_IARR
    jae .arr
    cmp al, CPV_IFC
    je .fc
    call cpv_sclass             ; --- Use this mode at startup: the VE byte
    test byte [es:CPV_LIVE], 2  ; (cpv_sclass: CL = the row's class, and
    jz .vok                     ; CPV_LIVE bit 1 = it is ticked)
    xor cl, cl                  ; ticked: un-ticking is Auto
.vok:
    mov [drv_cfg + CFG_VE], cl
    mov ah, CPG_WDSET           ; owed to SYSTEM.CFG at the close (31.8)
    call cpw_gate
    mov bx, CPV_TB              ; the marks moved: the rows, the controls
    call cpk_tbrows             ; and the Running mode group's last line
    xor al, al
    call cpv_ctls
    jmp cpv_rbox
.fc:
    mov ah, CPG_VGC             ; Force Color (SPEC.md 31.10.4)
    call cpw_gate
    xor al, al
    jmp cpv_ctls
.apply:
    call cpv_target             ; AL = the kind, CL = the class
    mov ah, CPG_VIDSET          ; switched, kept, owed - and NOTHING drawn
    jmp cpw_gate                ; (SPEC.md 31.10)
.keep:
    call cpv_tend               ; Keep: the switch stands (cp_repost owed it)
    call cpv_big
    jb .ksmall
    jmp cpv_rbox
.ksmall:
    jmp cpw_small               ; the small state's own line again
.arr:
    sub al, CPV_IARR            ; the desktop line, now (SPEC.md 31.10.2)
    mov ah, CPG_VIDARR
    jmp cpw_gate

; -----------------------------------------------------------------------------
; cpv_key - a key (cpw_key's dispatch): Esc reverts a test; the rest dropped
; in:  AX = the key word
; -----------------------------------------------------------------------------
cpv_key:
    cmp al, 27
    jne .r
    call cpv_testing
    jnc cpv_revert
.r:
    ret

; -----------------------------------------------------------------------------
; cpv_tick - the 1 Hz tick (cpw_tick's dispatch; [cp_tickp] = CP_IVID during
; a test, SPEC.md 135.9.4): count, revert at 0, and draw the countdown when
; the window can be drawn on (SPEC.md 14's background contract)
; -----------------------------------------------------------------------------
cpv_tick:
    call cpv_testing
    jc .r
    dec byte [es:CPV_LEFT]
    jz cpv_revert               ; no answer: back to what ran (135.9.3)
    mov al, KIND_CTRL
    KDOOR inst_find_kind        ; DI = the panel's record
    jc .r
    mov bx, [di+I_WIN]
    mov [es:CPW_W_WIN], bx
    mov bp, wm_obscured         ; hidden or covered: counted, not drawn
    call KERNEL_SEG:cw_mem_disp
    jc .r
    call cpw_org
    jmp cpv_kline
.r:
    ret

; -----------------------------------------------------------------------------
; cpv_quit - the panel is closing (cpw_close's first act): a test running is
; reverted while the claim is still held, so SYSTEM.CFG is written with what
; ran before it (SPEC.md 135.9.3). The gfx lock is taken unless the running
; task holds it already
; -----------------------------------------------------------------------------
cpv_quit:
    call cpv_testing
    jc .r
    mov al, [sch_cur]
    cmp al, [gfx_lock_own]
    je cpv_revert
    call KERNEL_SEG:cw_gfx_lock
    call cpv_revert
    call KERNEL_SEG:cw_gfx_unlock
.r:
    ret

; =============================================================================
; Test... (SPEC.md 135.9.3)
; =============================================================================

; cpv_test - save what runs, start the countdown, switch. Draws nothing
cpv_test:
    mov al, [vid_kind]
    mov [es:CPV_SKIND], al
    mov al, [vx_class]
    mov [es:CPV_SCLS], al
    call cpv_arrnow
    mov [es:CPV_SARR], al
    mov byte [es:CPV_LEFT], CPV_TEST
    mov byte [cp_tickp], CP_IVID    ; the resident gate ticks this page
    call cpv_target
    mov ah, CPG_VIDSET
    call cpw_gate
    jnc .r
    call cpv_tend               ; refused: no test, and the page repainted
    mov ah, CPG_REPOST
    call cpw_gate
.r:
    ret

; cpv_tend - no test any more: the countdown off and the gate back to
; Date/Time's. Keeps all
cpv_tend:
    mov byte [es:CPV_LEFT], 0
    mov byte [cp_tickp], CP_ITIME
    ret

; -----------------------------------------------------------------------------
; cpv_revert - back to EXACTLY what ran before the test: its kind, its class
; and its arrangement; the VGA if that switch is refused. Draws nothing
; -----------------------------------------------------------------------------
cpv_revert:
    call cpv_tend
    mov al, [es:CPV_SKIND]
    mov cl, [es:CPV_SCLS]
    mov ah, CPG_VIDSET
    call cpw_gate
    jnc .arr
    mov al, VID_VGA             ; a revert that fails leaves a working
    mov ah, CPG_VIDSET          ; display: the VGA every machine this face
    call cpw_gate               ; opens on has
.arr:
    call cpv_arrnow
    cmp al, [es:CPV_SARR]
    je .r
    mov al, [es:CPV_SARR]
    mov ah, CPG_VIDARR
    call cpw_gate
.r:
    ret

; cpv_arrnow - AL = the arrangement running, as its radio: 0 Single, 1 Right,
; 2 Below (cp_vid_msel's arithmetic). Keeps the rest
cpv_arrnow:
    mov al, [vid_dmode]
    or al, al
    jz .r
    mov al, [vid_dlay]
    inc ax
.r:
    ret

; -----------------------------------------------------------------------------
; cpv_target - what Apply and Test... switch to
; out: AL = the kind chosen, CL = the class (the row's; Auto 0; 0 off VESA)
; -----------------------------------------------------------------------------
cpv_target:
    call cpv_sclass
    mov al, [es:CPV_ASEL]
    ret

; -----------------------------------------------------------------------------
; cpv_sclass - the selected row's class and whether it is the startup one
; out: CL = its class byte (Auto, or not the VESA adapter: 0); CPV_LIVE bit 1
;      = it is what the VE byte names. BL = its record (FFh Auto). Keeps the
;      rest
; -----------------------------------------------------------------------------
cpv_sclass:
    push ax
    and byte [es:CPV_LIVE], 1
    xor cl, cl
    mov bl, 0FFh
    cmp byte [es:CPV_ASEL], VID_VESA
    jne .out
    mov bx, [es:CPV_MSEL]
    mov bl, [es:bx + CPV_ROWS]
    cmp bl, 0FFh
    je .ve
    push bx
    xor bh, bh
    mov cl, [es:bx + CPV_CLS]
    and cl, 0xDF                ; (bit 5 is this page's, not the class's)
    pop bx
.ve:
    cmp cl, [drv_cfg + CFG_VE]
    jne .out
    or byte [es:CPV_LIVE], 2
.out:
    pop ax
    ret

; =============================================================================
; The model: the records, the filter's tabs and the rows (SPEC.md 135.9.1)
; =============================================================================

; cpv_info - VXV_INFO through the gate (SPEC.md 120.10): CL = 0FFh the card,
; or a record. out: the verb's (AX, CX, DX, SI, DI), CF. Keeps BX
cpv_info:
    mov al, VXV_INFO
    mov ah, CPG_VXCALL
    jmp cpw_gate

; -----------------------------------------------------------------------------
; cpv_build - the card's records, the filter's tabs and the rows, from the
;             driver as it is now. Keeps the adapter, the filter (clamped) and
;             the selection (clamped). Spends AX..DI
; -----------------------------------------------------------------------------
cpv_build:
    mov byte [es:CPV_NREC], 0
    mov word [es:CPV_RUN], 0FFFFh   ; RUN and REC: none
    test byte [vid_avail], VID_A_VESA
    jz .tabs
    mov cl, 0FFh                ; --- the card
    call cpv_info
    jc .tabs
    cmp cl, CPV_NMAX
    jbe .n
    mov cl, CPV_NMAX
.n:
    mov [es:CPV_NREC], cl
    mov ax, si
    mov [es:CPV_REC], al
    cmp byte [vid_kind], VID_VESA
    jne .k0
    mov [es:CPV_RUN], ch
.k0:
    xor bx, bx                  ; --- each record's class, bit 5 its 15bpp
.k:
    cmp bl, [es:CPV_NREC]
    jae .tabs
    mov cl, bl
    call cpv_info
    jnc .kc
    xor cx, cx                  ; (a record that does not answer: no class,
.kc:                            ; so it is never a row)
    cmp ch, 15
    jne .ks
    or cl, 0x20
.ks:
    mov [es:bx + CPV_CLS], cl
    inc bx
    jmp short .k
.tabs:
    xor dx, dx                  ; --- the depths among the reachable records:
    xor bx, bx                  ; DL bit 0 = 8, bit 1 = 15, bit 2 = 16
.t:
    cmp bl, [es:CPV_NREC]
    jae .tl
    call cpv_reach
    jc .tn
    call cpv_dbit               ; AH = the record's depth bit
    or dl, ah
.tn:
    inc bx
    jmp short .t
.tl:
    mov di, CPV_TABD            ; the tabs: All, then each depth present
    mov byte [es:di], 0
    inc di
    mov si, cpv_s_tall          ; ...and their labels into cpv_tlab
    mov bx, cpv_tlab
    call .lab
    mov cx, 3
    mov al, 1
    mov si, cpv_s_t8
.td:
    test dl, al
    jz .tx
    push ax
    call .depth                 ; AH = 8 / 15 / 16 for bit AL
    mov [es:di], ah
    pop ax
    inc di
    call .lab
    jmp short .ty
.tx:
    push bx
    mov bx, si                  ; (past a label not used)
.tz:
    inc bx
    cmp byte [cs:bx - 1], 0
    jne .tz
    mov si, bx
    pop bx
.ty:
    shl al, 1
    loop .td
    CSDO mov byte [CSEG bx], 0  ; the strip's end
    mov ax, di
    sub ax, CPV_TABD
    mov [es:CPV_NTAB], al
    cmp [es:CPV_FLT], al
    jb .rows
    mov byte [es:CPV_FLT], 0
.rows:
    mov di, CPV_ROWS            ; --- the rows: Auto, then the reachable
    mov byte [es:di], 0FFh      ; records the filter keeps, in the card's
    inc di                      ; order (SPEC.md 120.10)
    mov bl, [es:CPV_FLT]
    xor bh, bh
    mov dh, [es:bx + CPV_TABD]  ; DH = the depth kept, 0 = all
    xor bx, bx
.r:
    cmp bl, [es:CPV_NREC]
    jae .rl
    call cpv_reach
    jc .rn
    or dh, dh
    jz .rt
    call cpv_dbit
    mov al, ah
    xor ah, ah
    push bx
    mov bl, al
    call .depthb                ; AL = the record's depth
    pop bx
    cmp al, dh
    jne .rn
.rt:
    mov [es:di], bl
    inc di
.rn:
    inc bx
    jmp short .r
.rl:
    sub di, CPV_ROWS
    mov [es:CPV_TB + CPKT_N], di
    cmp [es:CPV_MSEL], di       ; the selection and the top, clamped
    jb .m
    mov word [es:CPV_MSEL], 0
.m:
    mov ax, di
    sub ax, [es:CPV_TB + CPKT_NV]
    jge .tp
    xor ax, ax
.tp:
    cmp [es:CPV_TB + CPKT_TOP], ax
    jle .out
    mov [es:CPV_TB + CPKT_TOP], ax
.out:
    ret
.lab:                           ; CS:SI's label onto CS:BX, both advanced
    push ax
.lc:
    mov al, [cs:si]
    inc si
    CSDO mov [CSEG bx], al
    inc bx
    or al, al
    jnz .lc
    pop ax
    ret
.depth:                         ; AL = a depth bit -> AH = its bpp
    push bx
    mov bl, al
    call .depthb
    mov ah, al
    pop bx
    ret
.depthb:                        ; BL = a depth bit -> AL = its bpp
    mov al, 8
    shr bl, 1
    jc .dr
    mov al, 15
    shr bl, 1
    jc .dr
    mov al, 16
.dr:
    ret

; cpv_dbit - record BL's depth as a bit: AH = 1 (8), 2 (15), 4 (16). Keeps
; the rest
cpv_dbit:
    push bx
    xor bh, bh
    mov bl, [es:bx + CPV_CLS]
    mov ah, 1
    test bl, VXC_D16
    jz .r
    mov ah, 2
    test bl, 0x20
    jnz .r
    mov ah, 4
.r:
    pop bx
    ret

; -----------------------------------------------------------------------------
; cpv_reach - is record BL the one its class reaches (VESA.DRV's vx_find: the
;             deepest of the class, ties to the first - SPEC.md 135.9.1)?
; out: CF = 0 yes, CF = 1 no (or no class). Keeps every register
; -----------------------------------------------------------------------------
cpv_reach:
    push ax
    push bx
    push cx
    xor bh, bh
    mov ah, [es:bx + CPV_CLS]   ; AH = its class and 15bpp bit
    test ah, VXC_SET
    jz .no
    xor cx, cx                  ; CL = the other record
.o:
    cmp cl, [es:CPV_NREC]
    jae .yes
    cmp cl, bl
    je .on
    push bx
    mov bl, cl
    mov al, [es:bx + CPV_CLS]
    pop bx
    mov ch, al
    xor ch, ah
    test ch, 0xDF
    jnz .on                     ; another class
    cmp al, ah
    jb .no                      ; deeper (its 15bpp bit clear, ours set)
    ja .on
    cmp cl, bl
    jb .no                      ; as deep, and first
.on:
    inc cl
    jmp short .o
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; cpv_rowof - the row showing record AL (FFh: none, so Auto's row 0)
; out: AX = the row. Keeps the rest
cpv_rowof:
    push bx
    push cx
    mov cx, [es:CPV_TB + CPKT_N]
    xor bx, bx
.r:
    cmp bx, cx
    jae .none
    cmp al, [es:bx + CPV_ROWS]
    je .hit
    inc bx
    jmp short .r
.none:
    xor bx, bx
.hit:
    xchg ax, bx
    pop cx
    pop bx
    ret

; cpv_slot - adapter slot AL -> AL = the kind in it, CF = 1 none. Keeps the
; rest. The order is VGA, VESA, EGA, Hercules, CGA, absent adapters skipped
cpv_slot:
    push bx
    push cx
    push si
    mov ah, al                  ; AH = the slot wanted
    mov si, cpv_aord
    mov bx, 5
.n:
    mov al, [cs:si]
    inc si
    mov cl, al
    mov ch, 1
    shl ch, cl
    test [vid_avail], ch
    jz .s
    or ah, ah
    jz .hit
    dec ah
.s:
    dec bx
    jnz .n
    stc
.hit:                           ; (CF = 0 off the `or`)
    pop si
    pop cx
    pop bx
    ret
cpv_aord:   db VID_VGA, VID_VESA, VID_EGA, VID_HERC, VID_CGA

; =============================================================================
; Text: the 12-row ROW every line on the page is (SPEC.md 135.9.2)
; =============================================================================

; -----------------------------------------------------------------------------
; cpv_row - one ROW: two rows of paper, the text at ES:CPW_W_STR as one opaque
;           run padded (or cut) to the row's whole cells, two rows of paper,
;           and the paper in the columns past the last cell
; in:  CX/DX = left/top, content-relative; DI = the width in px; AL = ink,
;      AH = paper. out: nothing; every register kept
; -----------------------------------------------------------------------------
cpv_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, ax                  ; BP = ink : paper
    mov si, di                  ; SI = the width
    and di, 0xFFF8              ; DI = the cells' px
    push di
    shr di, 1
    shr di, 1
    shr di, 1
    add di, CPW_W_STR           ; DI -> where the run ends
    mov bx, CPW_W_STR
.f:
    cmp bx, di
    jae .cut
    cmp byte [es:bx], 0
    je .pad
    inc bx
    jmp short .f
.pad:
    mov byte [es:bx], ' '
    inc bx
    cmp bx, di
    jb .pad
.cut:
    mov byte [es:bx], 0
    pop di
    mov ax, bp
    mov [gfx_color], ah
    mov ax, cx
    mov bx, dx
    add cx, si
    dec cx                      ; CX = the right edge
    inc dx
    call cpw_fill               ; the two rows above the text...
    add bx, 10
    add dx, 10
    call cpw_fill               ; ...the two below it...
    sub bx, 8
    sub dx, 2
    add ax, di
    cmp ax, cx
    jg .run
    call cpw_fill               ; ...the columns past the cells
.run:
    sub ax, di
    mov cx, ax
    mov dx, bx
    mov si, CPW_W_STR
    mov ax, bp
    mov bl, 2
    call cpw_run
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cpv_wh - the pen white (the page's ground). Keeps all
cpv_wh:
    mov byte [gfx_color], CWHITE
    ret

; cpv_srow - an image string (CS:SI) as a ROW: cpv_row's other registers.
; Keeps all
cpv_srow:
    push di
    mov di, CPW_W_STR
    call cpw_strcat
    pop di
    jmp cpv_row

; cpv_new - DI = CPW_W_STR with a space in it (every line's leading cell)
cpv_new:
    mov di, CPW_W_STR
    mov byte [es:di], ' '
    inc di
    mov byte [es:di], 0
    ret

; cpv_col - pad ES:DI with spaces to column AL of CPW_W_STR. Keeps AX
cpv_col:
    push ax
    xor ah, ah
    add ax, CPW_W_STR
.p:
    cmp di, ax
    jae .z
    mov byte [es:di], ' '
    inc di
    jmp short .p
.z:
    mov byte [es:di], 0
    pop ax
    ret

; cpv_wxh - "XxY" at ES:DI. in AX = X, DX = Y; spends AX, CX, DX
cpv_wxh:
    push dx
    call cpw_utoa
    mov byte [es:di], 'x'
    inc di
    pop ax
    jmp cpw_utoa

; cpv_cols - CL = a depth (1, 4, 8, 15, 16) -> SI = its colours. Keeps the rest
cpv_cols:
    mov si, cpv_s_c2
    cmp cl, 1
    je .r
    mov si, cpv_s_c16
    cmp cl, 4
    je .r
    mov si, cpv_s_c256
    cmp cl, 8
    je .r
    mov si, cpv_s_c32k
    cmp cl, 15
    je .r
    mov si, cpv_s_c64k
.r:
    ret

; -----------------------------------------------------------------------------
; cpv_geo - a mode's geometry: AL = a kind; BL = a record for VESA (FFh: Auto's)
; out: AX = X, DX = Y, CL = the depth (1, 4, 8, 15, 16). Keeps BX, SI, DI
; -----------------------------------------------------------------------------
cpv_geo:
    cmp al, VID_VESA
    jne .planar
    push si
    push di
    mov cl, bl
    cmp cl, 0FFh
    jne .rec
    mov cl, [es:CPV_REC]
    cmp cl, 0FFh
    je .live
.rec:
    cmp cl, [es:CPV_NREC]
    jae .live
    call cpv_info
    jc .live
    mov cl, ch
    jmp short .out
.live:                          ; no record to ask: the live (or attached)
    mov ax, [vid_vxrow + 14]    ; one's geometry and its drawing depth
    mov dx, [vid_vxrow + 16]
    mov cl, [vx_bpp]
.out:
    pop di
    pop si
    ret
.planar:
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [cs:bx + cpv_ptab]
    mov dx, [cs:bx + cpv_ptab + 2]
    mov cl, 4                   ; VGA and EGA: 16 colours
    cmp bl, VID_HERC * 4
    je .mono
    cmp bl, VID_CGA * 4
    jne .p
.mono:
    mov cl, 1
.p:
    pop bx
    ret
cpv_ptab:   dw 640, 480,  720, 348,  640, 200,  640, 350    ; VGA HERC CGA EGA

; cpv_name - "<adapter>" for kind AL onto ES:DI. Keeps the rest
cpv_name:
    push bx
    push si
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [cs:bx + cpv_anam]
    call cpw_strcat
    pop si
    pop bx
    ret
cpv_anam:   dw cpv_s_vga, cpv_s_herc, cpv_s_cga, cpv_s_ega, cpv_s_vesa

; -----------------------------------------------------------------------------
; cpv_mode - "<adapter> WxH, <n> colours" onto ES:DI for kind AL, record BL.
;            Spends AX, CX, DX
; -----------------------------------------------------------------------------
cpv_mode:
    call cpv_name
    mov byte [es:di], ' '
    inc di
    call cpv_geo
    push cx
    call cpv_wxh
    mov si, cpv_s_comma
    call cpw_strcat
    pop cx
    call cpv_cols
    call cpw_strcat
    mov si, cpv_s_colours
    jmp cpw_strcat

; =============================================================================
; The page's parts
; =============================================================================

; cpv_sty2 - AX = the last row of the page area (2 above the status line)
cpv_sty2:
    call cpw_sty
    dec ax
    dec ax
    ret

; cpv_c0 - AX = the controls' top (SPEC.md 135.9.2: the status top - 54)
cpv_c0:
    call cpw_sty
    sub ax, 54
    ret

; cpv_r - CX = the page's right (the content width - 5). Keeps the rest
cpv_r:
    mov cx, [es:CPW_W_CW]
    sub cx, 5
    ret

; -----------------------------------------------------------------------------
; cpv_ground - the page's ground: the margins and the gaps between the groups,
; eight spans that touch no group (SPEC.md 135.9.2)
; -----------------------------------------------------------------------------
cpv_ground:
    call cpv_wh
    mov ax, CPW_PX              ; above everything
    mov bx, CPW_BY
    mov cx, [es:CPW_W_CW]
    dec cx
    mov dx, CPV_AY - 1
    call cpw_fill
    call cpv_sty2               ; the right margin, to the status rule
    xchg ax, dx
    call cpv_r
    mov ax, cx
    inc ax
    mov cx, [es:CPW_W_CW]
    dec cx
    mov bx, CPV_AY
    call cpw_fill
    mov ax, CPW_PX              ; the left margin
    mov cx, CPV_L - 1
    call cpw_fill
    call cpv_r                  ; between the top groups and the Modes group
    mov ax, CPV_L
    mov bx, CPV_AB + 1
    mov dx, CPV_BY - 1
    call cpw_fill
    mov ax, CPV_AR + 1          ; between the Adapter and Mode preview groups
    mov cx, CPV_PX - 1
    mov bx, CPV_AY
    mov dx, CPV_AB
    call cpw_fill
    call cpv_c0                 ; between the Modes group and the controls
    mov dx, ax
    dec dx
    mov bx, ax
    sub bx, 4
    mov ax, CPV_L
    call cpv_r
    call cpw_fill
    mov bx, dx                  ; between the controls and the right group
    inc bx
    add dx, 50
    mov ax, CPV_CR + 1
    mov cx, CPV_IX - 1
    call cpw_fill
    mov bx, dx                  ; under them both
    inc bx
    call cpv_sty2
    xchg ax, dx
    mov ax, CPV_L
    call cpv_r
    jmp cpw_fill

; -----------------------------------------------------------------------------
; cpv_adap - the Adapter group: its box, the rows, the desktop line
; -----------------------------------------------------------------------------
cpv_adap:
    mov ax, CPV_L
    mov bx, CPV_AY
    mov cx, CPV_AR
    mov dx, CPV_AB
    mov si, cpv_s_gad
    call cpk_group
    call cpv_arows
    call cpv_wh                 ; the group's last two rows
    mov ax, CPV_L + 1
    mov bx, CPV_AB - 2
    mov cx, CPV_AR - 1
    mov dx, CPV_AB - 1
    call cpw_fill
    mov dx, CPV_AY + CPK_BANDH + 4 * CPV_RH  ; --- the desktop line (31.10.2)
    mov cx, CPV_L + 1
    mov ax, (CWHITE << 8) | CBLACK
    CPWK vid_dual_ok
    jnc .arr
    mov di, CPV_AR - CPV_L - 1  ; no pairing: a blank row
    mov byte [es:CPW_W_STR], 0
    jmp cpv_row
.arr:
    mov di, 64
    mov si, cpv_s_desk
    call cpv_srow               ; 'Desktop'
    xor bl, bl                  ; BL = the radio
.ar:
    mov al, bl
    add al, CPV_IARR
    xor ah, ah
    call cpv_ctl1               ; its glyph
    mov al, bl                  ; ...and its label, past it
    call cpv_arect              ; AX/CX = its x span
    add ax, 12
    mov di, cx
    sub di, ax
    inc di
    mov cx, ax
    xor bh, bh
    mov si, bx
    shl si, 1
    mov si, [cs:si + cpv_arnam]
    mov ax, (CWHITE << 8) | CBLACK
    call cpv_srow
    inc bl
    cmp bl, 3
    jb .ar
    call cpv_wh                 ; the two columns past Below
    mov ax, CPV_L + 253
    mov cx, CPV_AR - 1
    mov bx, dx
    add dx, CPV_RH - 1
    jmp cpw_fill
cpv_arnam:  dw cpv_s_sing, cpv_s_rgt, cpv_s_blw

; cpv_arect - radio AL's x span: AX = its left, CX = its right (the glyph and
; its label). Keeps the rest
cpv_arect:
    push bx
    mov bl, al
    xor bh, bh
    mov al, [cs:bx + cpv_arx]
    mov cl, [cs:bx + cpv_arx + 1]
    xor ah, ah
    xor ch, ch
    add ax, CPV_L
    add cx, CPV_L - 1
    pop bx
    ret
cpv_arx:    db 65, 133, 193, 253    ; each radio's left, and past the last

; cpv_arows - the four adapter row slots: each adapter's line, the chosen one
; barred, an empty slot a blank row
cpv_arows:
    xor bl, bl
    mov dx, CPV_AY + CPK_BANDH
.s:
    call cpv_new
    mov al, bl
    call cpv_slot
    mov si, (CWHITE << 8) | CBLACK
    jc .row
    cmp al, [es:CPV_ASEL]
    jne .t
    mov si, (CBLUE << 8) | CWHITE
.t:
    push bx                     ; "<adapter> WxH", the live geometry for a
    push dx                     ; running VESA, and ', running'
    push ax
    call cpv_name
    cmp al, VID_VESA
    jne .geo
    cmp byte [vid_kind], VID_VESA
    jne .run
.geo:
    mov byte [es:di], ' '
    inc di
    mov bl, [es:CPV_RUN]
    call cpv_geo
    call cpv_wxh
.run:
    pop ax
    cmp al, [vid_kind]
    jne .tn
    push si
    mov si, cpv_s_running
    call cpw_strcat
    pop si
.tn:
    pop dx
    pop bx
.row:
    mov ax, si
    mov cx, CPV_L + 1
    mov di, CPV_AR - CPV_L - 1
    call cpv_row
    add dx, CPV_RH
    inc bl
    cmp bl, 4
    jb .s
    ret


; -----------------------------------------------------------------------------
; cpv_prev - the Mode preview group: a monitor drawn to the chosen mode's
; aspect - a bezel and the screen, and the ground in four spans round them -
; and two lines beside it (SPEC.md 135.9.1)
; -----------------------------------------------------------------------------
CPV_ZL      equ CPV_PX + 1      ; the picture zone: x ZL..ZL+79, y ZT..AB-1
CPV_ZT      equ CPV_AY + CPK_BANDH
CPV_ZR      equ CPV_ZL + CPV_ZW - 1
CPV_ZC      equ CPV_ZL + CPV_ZW / 2
cpv_prev:
    mov ax, CPV_PX
    mov bx, CPV_AY
    call cpv_r
    mov dx, CPV_AB
    mov si, cpv_s_gpv
    call cpk_group
    mov al, [es:CPV_ASEL]       ; --- the chosen mode
    mov bx, [es:CPV_MSEL]
    mov bl, [es:bx + CPV_ROWS]
    cmp al, VID_VESA
    je .g
    mov bl, 0FFh
.g:
    call cpv_geo
    or ax, ax                   ; a mode with no geometry draws 4:3
    jz .flat
    or dx, dx
    jnz .have
.flat:
    mov ax, 4
    mov dx, 3
.have:
    mov [es:CPV_GX], ax
    mov [es:CPV_GY], dx
    mov [es:CPV_GD], cl
    call cpv_new                ; --- ' WxH'
    call cpv_wxh
    mov dx, CPV_ZT
    call .line
    call cpv_new                ; ' <n> colours'
    mov cl, [es:CPV_GD]
    call cpv_cols
    call cpw_strcat
    mov si, cpv_s_colours
    call cpw_strcat
    mov dx, CPV_ZT + CPV_RH
    call .line
    call cpv_wh                 ; the rows under the lines
    mov ax, CPV_ZR + 1
    mov bx, CPV_ZT + 2 * CPV_RH
    call cpv_r
    dec cx
    mov dx, CPV_AB - 1
    call cpw_fill
    ; --- the screen, SW x SH in a 64 x 40 box at the mode's aspect: 40X >=
    ; 64Y is SW 64 and SH 64Y/X, else SH 40 and SW 40X/Y (X <= 1024 and Y <=
    ; 768, so 40X and 64Y fit a word)
    mov ax, 40
    mul word [es:CPV_GX]
    xchg ax, cx                 ; CX = 40X
    mov ax, 64
    mul word [es:CPV_GY]        ; AX = 64Y
    cmp cx, ax
    jb .tall
    xor dx, dx
    div word [es:CPV_GX]        ; SH = 64Y / X
    mov bx, 64
    jmp short .box
.tall:
    xchg ax, cx
    xor dx, dx
    div word [es:CPV_GY]        ; SW = 40X / Y
    xchg ax, bx
    mov ax, 40
.box:                           ; BX = SW, AX = SH
    mov bp, ax
    add bp, CPV_ZT + 2 + 7      ; BP = the bezel's bottom row (the zone's
                                ; tallest: 40 + 8 rows from ZT + 2, to 109)
    add bx, 8
    mov si, CPV_ZC
    shr bx, 1
    sub si, bx                  ; SI = the bezel's left
    mov di, CPV_ZC - 1
    add di, bx                  ; DI = its right
    call cpv_wh                 ; --- the ground, in four spans
    mov ax, CPV_ZL
    mov bx, CPV_ZT
    mov cx, CPV_ZR
    mov dx, CPV_ZT + 1
    call cpw_fill               ; above the bezel
    mov bx, CPV_ZT + 2
    mov dx, bp
    mov cx, si
    dec cx
    call cpw_fill               ; left of it
    mov ax, di
    inc ax
    mov cx, CPV_ZR
    call cpw_fill               ; right of it
    mov ax, CPV_ZL
    mov bx, bp
    inc bx
    mov dx, CPV_AB - 1
    call cpw_fill               ; under it
    mov byte [gfx_color], CDGRAY    ; --- the bezel: top, bottom, sides
    mov ax, si
    mov cx, di
    mov bx, CPV_ZT + 2
    mov dx, CPV_ZT + 5
    call cpw_fill
    mov bx, bp
    sub bx, 3
    mov dx, bp
    call cpw_fill
    mov bx, CPV_ZT + 6
    mov dx, bp
    sub dx, 4
    mov cx, si
    add cx, 3
    call cpw_fill
    mov ax, di
    sub ax, 3
    mov cx, di
    call cpw_fill
    mov byte [gfx_color], 3     ; --- and the screen (cyan)
    mov ax, si
    add ax, 4
    mov cx, di
    sub cx, 4
    jmp cpw_fill
.line:                          ; the composed line beside the zone, at DX
    mov cx, CPV_ZR + 1
    call cpv_r
    mov di, cx
    sub di, CPV_ZR + 1
    mov cx, CPV_ZR + 1
    mov ax, (CWHITE << 8) | CBLACK
    jmp cpv_row

; -----------------------------------------------------------------------------
; cpv_modes - the Modes group: the VESA adapter's filter, header and table, or
; for any other adapter what it is in a line (SPEC.md 135.9.1)
; -----------------------------------------------------------------------------
cpv_modes:
    call cpv_c0
    sub ax, 5
    xchg ax, dx                 ; DX = the group's bottom
    mov ax, CPV_L
    mov bx, CPV_BY
    call cpv_r
    mov si, cpv_s_gmd
    call cpk_group
    mov bp, dx                  ; BP = its bottom
    cmp byte [es:CPV_ASEL], VID_VESA
    je .vesa
    call cpv_new                ; --- one mode: say which
    mov si, cpv_s_one
    call cpw_strcat
    mov al, [es:CPV_ASEL]
    mov bl, 0FFh
    call cpv_mode
    mov dx, CPV_BY + CPK_BANDH
    call .w
    call cpv_row
    mov di, CPW_W_STR
    mov byte [es:di], 0
    test byte [vid_avail], VID_A_VESA
    jnz .l2
    mov si, cpv_s_load          ; ...and where more come from, or that the
    cmp word [vesa_row + DRVR_SEG], 0   ; driver IS loaded and attaches at
    je .ld                      ; the next start (SPEC.md 120.4, 135.9.1)
    mov si, cpv_s_rst
.ld:
    call cpw_strcat
.l2:
    add dx, CPV_RH
    call .w
    call cpv_row
    add dx, CPV_RH
    mov bx, dx
    jmp short .under
.vesa:
    call .w
    push di
    call cpv_wh
    mov ax, cx
    call cpv_r
    dec cx
    mov bx, CPV_BY + CPK_BANDH
    mov dx, CPV_TY - 1
    call cpw_fill               ; above the strip
    mov bx, CPV_TY + CPK_TABH
    mov dx, CPV_HY - 1
    call cpw_fill               ; between it and the header
    call cpv_tabs
    pop di
    mov cx, CPV_L + 1
    mov dx, CPV_HY
    mov si, cpv_s_head
    mov ax, (CLGRAY << 8) | CBLACK
    call cpv_srow               ; the header line
    mov [es:CPV_TB + CPKT_X], cx    ; --- the table
    mov word [es:CPV_TB + CPKT_Y], CPV_RY
    mov [es:CPV_TB + CPKT_W], di
    mov word [es:CPV_TB + CPKT_RH], CPV_RH
    mov ax, bp
    sub ax, CPV_RY + 1
    xor dx, dx
    mov bx, CPV_RH
    div bx
    mov [es:CPV_TB + CPKT_NV], ax
    mul bx
    add ax, CPV_RY
    xchg ax, bx                 ; BX = the first row under it
    mov word [es:CPV_TB + CPKT_ROW], cpv_trow
    push bx
    mov bx, CPV_TB
    call cpk_table
    pop bx
.under:
    call cpv_wh                 ; the group's inside under it all
    mov ax, CPV_L + 1
    call cpv_r
    dec cx
    mov dx, bp
    dec dx
    cmp bx, dx
    jg .r
    call cpw_fill
.r:
    ret
.w:                             ; DI = a row's width (the group's inside),
    call cpv_r                  ; CX = its left, AX = black on white
    mov di, cx
    sub di, CPV_L + 1
    mov cx, CPV_L + 1
    mov ax, (CWHITE << 8) | CBLACK
    ret

; cpv_tabs - the filter strip and its row's ground either side
cpv_tabs:
    call cpv_wh
    mov ax, CPV_L + 1
    mov bx, CPV_TY
    mov cx, CPV_L + 3
    mov dx, CPV_TY + CPK_TABH - 1
    call cpw_fill
    mov cx, CPV_L + 4
    mov dx, bx
    mov si, cpv_tlab
    mov al, [es:CPV_FLT]
    call cpk_tabs               ; CX = past the last tab
    mov ax, cx
    call cpv_r
    dec cx
    add dx, CPK_TABH - 1
    call cpv_wh
    cmp ax, cx
    jg .r
    call cpw_fill
.r:
    ret

; -----------------------------------------------------------------------------
; cpv_trow - the table's row painter (CPKT_ROW, SPEC.md 135.5): a row as
; Resolution, Colours, Memory and Status, one ROW
; in:  AX = the row, BL = its ground, CX/DX = left/top, DI = width. Keeps all
; -----------------------------------------------------------------------------
cpv_trow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push bx
    push cx
    push dx
    push di
    mov si, ax
    mov al, [es:si + CPV_ROWS]
    call cpv_rtext
    pop di
    pop dx
    pop cx
    pop bx
    mov ax, (CWHITE << 8) | CBLACK
    cmp bl, CBLUE
    jne .p
    mov ax, (CBLUE << 8) | CWHITE
.p:
    call cpv_row
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cpv_trowat - row AX of the table drawn where it is, if it is in view.
; Keeps all
cpv_trowat:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, ax
    sub ax, [es:CPV_TB + CPKT_TOP]
    jb .r
    cmp ax, [es:CPV_TB + CPKT_NV]
    jae .r
    mov dx, CPV_RH
    mul dx
    add ax, [es:CPV_TB + CPKT_Y]
    xchg ax, dx                 ; DX = its top
    mov cx, [es:CPV_TB + CPKT_X]
    mov di, [es:CPV_TB + CPKT_W]
    sub di, CPK_SBW
    mov ax, bx
    mov bl, CWHITE
    cmp ax, [es:CPV_MSEL]
    jne .p
    mov bl, CBLUE
.p:
    call cpv_trow
.r:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpv_rtext - a table row's text into CPW_W_STR: AL = its record, FFh Auto.
;             Columns at cells 1, 12, 20 and 28. Spends AX..DI
; -----------------------------------------------------------------------------
cpv_rtext:
    mov bl, al
    call cpv_new
    cmp bl, 0FFh
    jne .rec
    mov si, cpv_s_auto
    call cpw_strcat
    mov al, 12
    call cpv_col
    mov si, cpv_s_best
    call cpw_strcat
    jmp short .marks
.rec:
    push di
    mov cl, bl
    call cpv_info               ; AX = X, DX = Y, CH = depth, SI = KB
    pop di
    jc .marks
    push si
    push cx
    call cpv_wxh
    mov al, 12
    call cpv_col
    pop cx
    mov cl, ch
    call cpv_cols
    call cpw_strcat
    mov al, 20
    call cpv_col
    pop ax
    call cpw_utoa
    mov byte [es:di], 'K'
    inc di
    mov byte [es:di], 0
.marks:
    mov al, 28
    call cpv_col
    xor dl, dl                  ; DL = 1 once a mark is down
    mov al, [drv_cfg + CFG_VE]
    cmp bl, 0FFh
    jne .m1
    or al, al                   ; Auto: 'startup' when the byte is 0
    jnz .r
    mov si, cpv_s_mstart
    jmp short .mark
.m1:
    cmp bl, [es:CPV_RUN]
    jne .m2
    mov si, cpv_s_mcur
    call .mark
.m2:
    cmp bl, [es:CPV_REC]
    jne .m3
    mov si, cpv_s_mrec
    call .mark
.m3:
    or al, al
    jz .r
    push bx
    xor bh, bh
    mov ah, [es:bx + CPV_CLS]
    pop bx
    and ah, 0xDF
    cmp ah, al
    jne .r
    mov si, cpv_s_mstart
.mark:
    or dl, dl
    jz .mk
    push si
    mov si, cpv_s_comma
    call cpw_strcat
    pop si
.mk:
    mov dl, 1
    jmp cpw_strcat
.r:
    ret

; =============================================================================
; The controls (SPEC.md 135.9.1): two check boxes and two buttons under the
; table, Keep and Revert in their group, the desktop line's three radios
; =============================================================================

; -----------------------------------------------------------------------------
; cpv_ctls - the check boxes' glyphs and labels, the buttons, and (AL = 1, a
; full paint) the ground between them
; -----------------------------------------------------------------------------
cpv_ctls:
    push ax
    call cpv_c0
    mov dx, ax                  ; DX = the controls' top
    mov cx, CPV_L + 12
    mov di, CPV_CR - CPV_L - 11
    mov al, CPV_ISTART
    call cpv_ctlok
    mov ax, (CWHITE << 8) | CBLACK
    jnc .s
    mov al, CDGRAY              ; greyed: the label in the grey pen (47 r2)
.s:
    mov si, cpv_s_start
    call cpv_srow
    add dx, 14
    push ax
    mov al, CPV_IFC
    call cpv_ctlok
    pop ax
    mov al, CBLACK
    jnc .f
    mov al, CDGRAY
.f:
    mov si, cpv_s_fc
    call cpv_srow
    pop ax
    or al, al
    jz .ctl
    call cpv_wh                 ; --- the ground between them (full)
    mov ax, CPV_L
    mov cx, CPV_CR
    mov bx, dx                  ; (DX = C0 + 14)
    sub bx, 2
    dec dx
    call cpw_fill               ; rows C0+12..C0+13
    add bx, 14
    add dx, 18
    call cpw_fill               ; rows C0+26..C0+31
    add bx, 6
    add dx, 18                  ; rows C0+32..C0+49, round the buttons
    mov ax, CPV_L + 64
    mov cx, CPV_L + 67
    call cpw_fill               ; between the buttons
    mov ax, CPV_L + 132
    mov cx, CPV_CR
    call cpw_fill               ; right of them
.ctl:
    xor bx, bx
.c:
    mov ax, bx                  ; every control's own picture, up
    call cpv_ctl1
    inc bx
    cmp bx, CPV_IKEEP
    jb .c
    ret

; -----------------------------------------------------------------------------
; cpv_ctl1 - control AL drawn: AH = 1 pressed. Keeps all
; A check box and a radio through CPG_GLYPH (os88ui_glyph is CTRL.DRV's), a
; button through cpk_button; greyed as cpv_ctlok says (SPEC.md 47 rule 1)
; -----------------------------------------------------------------------------
cpv_ctl1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, ax                  ; BP = the control and its press
    call cpv_ctlok
    sbb di, di                  ; DI = FFFFh greyed
    mov ax, bp
    call cpv_rect               ; AX/BX/CX/DX = its rect
    push ax
    mov ax, bp
    cmp al, CPV_IAPPLY
    jb .check
    cmp al, CPV_IARR
    jae .radio
    and di, OS88UI_DIS          ; --- a button
    or ah, ah
    jz .b
    or di, OS88UI_DOWN
.b:
    xor ah, ah
    sub al, CPV_IAPPLY
    mov si, ax
    shl si, 1
    mov si, [cs:si + cpv_blab]
    pop ax
    call cpk_button
    jmp short .out
.radio:
    sub al, CPV_IARR
    mov ah, al
    call cpv_arrnow
    cmp al, ah
    mov al, OS88UI_GRADIO
    jmp short .on
.check:
    cmp al, CPV_IFC
    je .fc
    push bx
    call cpv_sclass
    pop bx
    test byte [es:CPV_LIVE], 2
    mov al, OS88UI_GCHECK
    jmp short .nz
.fc:
    test byte [drv_cfg + CFG_VGC], CFG_VGCB
    mov al, OS88UI_GCHECK
.nz:
    mov si, cpd_ck0             ; the Drivers page's tick box (QA-8), up and
    jz .z0                      ; live; greyed or pressed is the gate's glyph
    mov si, cpd_ck1
    or al, OS88UI_GON
.z0:
    test di, di
    jnz .g
    test bp, 0FF00h
    jnz .g
    pop ax                      ; its left (content); BX is still its top
    call cpd_blit12
    jmp short .out
.on:
    jne .g
    or al, OS88UI_GON
.g:
    mov cx, bp
    or ch, ch
    jz .gu
    or al, OS88UI_GDOWN
.gu:
    pop cx                      ; CX/DX = its top left, SCREEN
    add cx, [es:CPW_W_OX]
    mov dx, bx
    add dx, [es:CPW_W_OY]
    mov bx, di
    and bl, 1                   ; BL = 1 greyed
    mov ah, CPG_GLYPH
    call cpw_gate
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
cpv_blab:   dw cpv_s_apply, cpv_s_test, cpv_s_keep, cpv_s_revert

; -----------------------------------------------------------------------------
; cpv_ctlok - is control AL live? CF = 1 greyed. Keeps every register
; -----------------------------------------------------------------------------
cpv_ctlok:
    push ax
    push bx
    push cx
    cmp al, CPV_IFC
    jb .start
    je .fc
    cmp al, CPV_IKEEP
    jb .switch
    cmp al, CPV_IARR
    jb .yes                     ; Keep and Revert
    sub al, CPV_IARR            ; a radio: not the one on
    mov ah, al
    call cpv_arrnow
    cmp al, ah
    je .no
    jmp short .yes
.start:
    cmp byte [es:CPV_ASEL], VID_VESA
    jne .no                     ; not the VESA adapter: no class to keep
    call cpv_sclass
    cmp bl, 0FFh
    jne .yes
    test byte [es:CPV_LIVE], 2
    jnz .no                     ; Auto, ticked: there is nothing to un-tick to
    jmp short .yes
.fc:
    test byte [vid_avail], VID_A_VGA
    jz .no
    cmp byte [vid_kind], VID_VESA
    je .no                      ; a VESA desktop: its DAC is its own (135.9.1)
    jmp short .yes
.switch:                        ; Apply / Test...: not what is running
    mov al, [es:CPV_ASEL]
    cmp al, [vid_kind]
    jne .yes
    cmp al, VID_VESA
    jne .no
    mov bx, [es:CPV_MSEL]
    mov al, [es:bx + CPV_ROWS]
    cmp al, 0FFh
    jne .rec
    mov al, [es:CPV_REC]        ; Auto is the recommended record
.rec:
    cmp al, [es:CPV_RUN]
    jne .yes
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpv_rect - control AL's rect, content-relative: AX/BX/CX/DX. Keeps SI, DI
; -----------------------------------------------------------------------------
cpv_rect:
    push si
    push di
    mov si, ax
    and si, 0x00FF              ; SI = the control
    cmp si, CPV_IARR
    jae .arr
    call cpv_kpos               ; AX/BX = the Keep group's left/top
    cmp si, CPV_IKEEP
    jae .base
    call cpv_c0                 ; ...or the controls' (L, C0)
    xchg ax, bx
    mov ax, CPV_L
.base:
    shl si, 1
    shl si, 1
    xor cx, cx
    xor dx, dx
    mov cl, [cs:si + cpv_ctab + 2]
    add cx, ax
    mov dl, [cs:si + cpv_ctab + 3]
    add dx, bx
    push dx
    xor dx, dx
    mov dl, [cs:si + cpv_ctab]
    add ax, dx
    mov dl, [cs:si + cpv_ctab + 1]
    add bx, dx
    pop dx
    jmp short .out
.arr:
    mov ax, si
    sub al, CPV_IARR
    call cpv_arect
    mov bx, CPV_AY + CPK_BANDH + 4 * CPV_RH
    mov dx, bx
    add dx, CPV_RH - 1
.out:
    pop di
    pop si
    ret
; each control's rect from its base, in bytes: left, top, right, bottom
cpv_ctab:   db 0, 0, 211, 11        ; Use this mode at startup (L, C0)
            db 0, 14, 211, 25       ; Force Color
            db 0, 32, 63, 49        ; Apply
            db 68, 32, 131, 49      ; Test...
            db 6, 26, 69, 43        ; Keep   (the Keep group's left, top)
            db 74, 26, 137, 43      ; Revert

; -----------------------------------------------------------------------------
; cpv_hit - the control at a point: CX/DX content-relative -> AX = it, CF = 0;
; CF = 1 none. Only what is drawn answers: Keep and Revert during a test and
; nothing else, the desktop radios only where the line is drawn
; Keeps the rest
; -----------------------------------------------------------------------------
cpv_hit:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, cx
    mov di, dx
    xor bp, bp
.n:
    cmp bp, CPV_NCTL
    jae .no
    call cpv_testing
    jc .nt
    cmp bp, CPV_IKEEP           ; a test: Keep and Revert alone
    jb .skip
    cmp bp, CPV_IARR
    jae .no
    jmp short .try
.nt:
    cmp bp, CPV_IKEEP
    jb .try
    cmp bp, CPV_IARR
    jb .skip
    CPWK vid_dual_ok
    jc .no
.try:
    mov ax, bp
    call cpv_rect
    cmp si, ax
    jl .skip
    cmp si, cx
    jg .skip
    cmp di, bx
    jl .skip
    cmp di, dx
    jg .skip
    mov ax, bp
    clc
    jmp short .out
.skip:
    inc bp
    jmp short .n
.no:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; cpv_kpos - the Keep group's place: AX/BX = left/top, CX = right. Under the
; table in the face; at (4, 4) in the small state (SPEC.md 135.9.3). Keeps DX
cpv_kpos:
    call cpv_big
    jb .small
    call cpv_c0
    xchg ax, bx
    mov ax, CPV_IX
    jmp cpv_r
.small:
    mov ax, 4
    mov bx, ax
    mov cx, 4 + 263
    ret

; =============================================================================
; The right group: Running mode, or Keep this mode? during a test
; =============================================================================
cpv_rbox:
    call cpv_testing
    jnc cpv_kbox
    call cpv_c0                 ; --- Running mode
    mov bx, ax
    mov dx, ax
    add dx, 49
    mov ax, CPV_IX
    call cpv_r
    mov si, cpv_s_grun
    call cpk_group
    mov bp, bx                  ; BP = its top
    call cpv_new                ; the running adapter and mode
    mov al, [vid_kind]
    mov bl, [es:CPV_RUN]
    call cpv_mode
    call .line
    call cpv_new                ; the card
    mov si, cpv_s_novx
    test byte [vid_avail], VID_A_VESA
    jnz .vx
    cmp word [vesa_row + DRVR_SEG], 0
    je .c
    mov si, cpv_s_novl          ; loaded at run time: it attaches at boot
    jmp short .c
.vx:
    mov si, cpv_s_nolist
    cmp byte [es:CPV_NREC], 0
    je .c
    mov si, cpv_s_vbe           ; ' VBE M.m, <mem> video memory'
    call cpw_strcat
    push di
    mov cl, 0FFh
    call cpv_info               ; AX = the version, DX = 64KB units
    pop di
    push dx
    push ax
    mov al, ah
    xor ah, ah
    call cpw_utoa
    mov byte [es:di], '.'
    inc di
    pop ax
    xor ah, ah
    call cpw_utoa
    mov si, cpv_s_comma
    call cpw_strcat
    pop ax
    mov si, cpv_s_mb
    cmp ax, 16
    jb .kb
    mov cl, 4
    shr ax, cl
    jmp short .mem
.kb:
    mov cl, 6
    shl ax, cl
    mov si, cpv_s_kb
.mem:
    call cpw_utoa
    call cpw_strcat
    mov si, cpv_s_vm
.c:
    call cpw_strcat
    call .line
    call cpv_wh                 ; the group's last 13 rows
    mov ax, CPV_IX + 1
    call cpv_r
    dec cx
    mov bx, bp
    add bx, CPV_RH
    mov dx, bx
    add dx, CPV_RH
    jmp cpw_fill
.line:                          ; the composed line, the next of three
    add bp, CPV_RH
    call cpv_r
    mov di, cx
    sub di, CPV_IX + 1
    mov cx, CPV_IX + 1
    mov dx, bp
    mov ax, (CWHITE << 8) | CBLACK
    jmp cpv_row

; -----------------------------------------------------------------------------
; cpv_kbox - Keep this mode?: the countdown, Keep and Revert, at cpv_kpos
; -----------------------------------------------------------------------------
cpv_kbox:
    call cpv_kpos               ; AX/BX = left/top, CX = right
    push ax                     ; --- its title: white on blue, one ROW - the
    push cx                     ; mockup's band, and on a 1bpp desktop (the
    mov di, cx                  ; small state after a test of a Hercules or a
    sub di, ax                  ; CGA) blue is black and the words stay
    inc di                      ; white, where the kit's grey band would set
    mov cx, ax                  ; black words on black
    mov dx, bx
    mov si, cpv_s_gkeep
    mov ax, (CBLUE << 8) | CWHITE
    call cpv_srow
    pop cx
    pop ax
    mov byte [gfx_color], CBLACK    ; --- its frame: left, right, bottom
    add bx, CPK_BANDH
    mov dx, bx
    add dx, 49 - CPK_BANDH
    push cx
    mov cx, ax
    call cpw_fill
    pop cx
    push ax
    mov ax, cx
    call cpw_fill
    pop ax
    mov bx, dx
    call cpw_fill
    sub bx, 49
    call cpv_kline
    call cpv_wh                 ; --- the ground round the buttons
    inc ax
    dec cx
    add bx, 24
    mov dx, bx
    inc dx
    call cpw_fill               ; under the line
    push cx
    add bx, 2
    add dx, 18
    mov cx, ax
    add cx, 4
    call cpw_fill               ; left of Keep
    add ax, 69
    add cx, 68
    call cpw_fill               ; between Keep and Revert
    add ax, 68
    pop cx
    call cpw_fill               ; right of Revert
    sub ax, 137
    add bx, 18
    add dx, 5
    call cpw_fill               ; under them
    mov ax, CPV_IKEEP
    call cpv_ctl1
    inc ax
    jmp cpv_ctl1

; cpv_kline - ' Reverting in N seconds.' at the Keep group's first line
cpv_kline:
    push ax
    push bx
    push cx
    push dx
    push di
    call cpv_kpos
    push ax
    push bx
    push cx
    call cpv_new
    mov si, cpv_s_rev1
    call cpw_strcat
    mov al, [es:CPV_LEFT]
    xor ah, ah
    call cpw_utoa
    mov si, cpv_s_rev2
    call cpw_strcat
    pop di                      ; DI = the right
    pop dx
    add dx, CPK_BANDH
    pop cx
    inc cx
    sub di, cx                  ; the inside's width
    mov ax, (CWHITE << 8) | CBLACK
    call cpv_row
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the page's strings ---------------------------------------------------------
cpv_s_gad:      db 'Adapter', 0
cpv_s_gpv:      db 'Mode preview', 0
cpv_s_gmd:      db 'Modes', 0
cpv_s_grun:     db 'Running mode', 0
cpv_s_gkeep:    db ' Keep this mode?', 0
cpv_s_vga:      db 'VGA', 0
cpv_s_herc:     db 'Hercules', 0
cpv_s_cga:      db 'CGA', 0
cpv_s_ega:      db 'EGA', 0
cpv_s_vesa:     db 'VESA', 0
cpv_s_running:  db ', running', 0
cpv_s_desk:     db ' Desktop', 0
cpv_s_sing:     db ' Single', 0
cpv_s_rgt:      db ' Right', 0
cpv_s_blw:      db ' Below', 0
cpv_s_comma:    db ', ', 0
cpv_s_colours:  db ' colours', 0
cpv_s_c2:       db '2', 0
cpv_s_c16:      db '16', 0
cpv_s_c256      equ cpv_s_t8        ; (the filter's labels are the colours)
cpv_s_c32k      equ cpv_s_t8 + 4
cpv_s_c64k      equ cpv_s_t8 + 11
cpv_s_one:      db 'One mode: ', 0
cpv_s_load:     db ' Load VESA Display on the Drivers page for more.', 0
cpv_s_head:     db ' Resolution Colours Memory  Status', 0
cpv_s_auto:     db 'Auto', 0
cpv_s_best:     db '(best)', 0
cpv_s_mcur:     db 'current', 0
cpv_s_mrec:     db 'recommended', 0
cpv_s_mstart:   db 'startup', 0
cpv_s_tall:     db 'All', 0
cpv_s_t8:       db '256', 0, '32,768', 0, '65,536', 0
cpv_s_start:    db ' Use mode at startup', 0
cpv_s_fc:       db ' Force Color', 0
cpv_s_apply:    db 'Apply', 0
cpv_s_test:     db 'Test...', 0
cpv_s_keep:     db 'Keep', 0
cpv_s_revert:   db 'Revert', 0
cpv_s_novx:     db 'VESA Display is not loaded', 0
cpv_s_novl:     db 'Restart to use VESA Display', 0
cpv_s_rst:      db ' Restart to use the VESA Display modes.', 0
cpv_s_nolist:   db 'No mode list from the driver', 0
cpv_s_vbe:      db 'VBE ', 0
cpv_s_mb:       db ' MB', 0
cpv_s_vm:       db ' video memory', 0
cpv_s_kb:       db 'K', 0
cpv_s_rev1:     db 'Reverting in ', 0
cpv_s_rev2:     db ' seconds.', 0

section .modvb                  ; ...and the filter strip's labels, built
cpv_tlab:   resb 24             ; from the depths the card offers (cpv_build)
section .text
