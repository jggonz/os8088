; WireBall (SPEC.md 107): one court, two computers, a null modem.
; The serial lifetime and UART rate callback are wholly inside FSX.
%include "os88api.inc"
    OS88_HEADER 'WIREBALL', wb_entry, 1
    OS88_ICON16
    dw 0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff
    dw 0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xffff
    dw 0x8001,0xa005,0xa005,0xa005,0xa005,0x8001,0x8181,0x8181
    dw 0x8001,0x8421,0x8241,0x8181,0x8001,0xa005,0xa005,0x8001
    OS88_ICON16_END

WB_ROWS equ 20
WB_PH equ 4
WB_PMAX equ (WB_ROWS-WB_PH)*4
WB_YMAX equ WB_ROWS*4-2
WB_TIMEOUT equ 55
WB_FRAME equ 30
WB_PROTOCOL equ 3
WB_SPEED_MIN equ 8
WB_SPEED_MAX equ 16
WB_HELLO equ 0
WB_STATE equ 1
WB_INPUT equ 2
WB_BYE equ 3
; Per-port state, indexed by SI. Only advertised standard UARTs are touched.
WP_BASE equ 0
WP_LCR equ 2
WP_IER equ 3
WP_DLL equ 4
WP_DLM equ 5
WP_MCR equ 6
WP_TLEN equ 7
WP_TPOS equ 8
WP_RLEN equ 9
WP_ESC equ 10
WP_TX equ 11
WP_RX equ WP_TX+WB_FRAME*2+2
WP_QHEAD equ WP_RX+WB_FRAME
WP_QTAIL equ WP_QHEAD+1
WP_QERR equ WP_QTAIL+1
WP_QUEUE equ WP_QERR+1
WP_SIZE equ WP_QUEUE+128

wb_entry:
    mov si, wb_tpl
    call OSAPI_WM_CREATE
    jc .out
    OS88_REGION_MOVABLE
    OS88_ALTENTER_ARM
    mov [wb_win], bx
    mov al, 1
    call OSAPI_WM_OWNBG
    mov si, wb_prefs
    call OSAPI_WM_PREFER
    mov cx, 418
    mov dx, 154
    call OSAPI_WM_MINSIZE
    mov si, wb_menus
    call OSAPI_MENU_SET
    mov si, wb_about
    call OSAPI_ABOUT_SET
    mov ax, bx
    mov bx, wb_btrec
    mov si, wb_buttonup
    mov di, wb_buttondrag
    mov dx, wb_onclick
    call os88ui_btninit
    mov bx, [wb_win]             ; loader success returns the window, not the button record
    clc
.out:
    ret

wb_oncmd:
    ; Game menu: serial (0), practice (1).
    jmp wb_launch
wb_launch:
    mov [wb_practice], al
    mov byte [wb_abon], 0
    call wv_fontload
    mov byte [wv_fast], 1
    mov word [wv_clock], 0
    mov byte [wb_async], 0
    mov ax, wb_main
    mov bx, [wb_win]
    mov cx, FSXF_RATE
    mov dx, 2048
    mov di, wb_ratehook
    call OSAPI_FSX_RUN
    jnc .out
    mov byte [wv_fast], 0
    mov ax, wb_main
    mov bx, [wb_win]
    xor cx, cx
    call OSAPI_FSX_RUN
.out:
    ret

wb_main:
    push ds
    pop es
    call wv_mode
    jc .out
    mov ax, [wb_fsi+FSI_SEG]
    mov [wb_vseg], ax
    mov byte [wb_quit], 0
    mov byte [wb_latch], 0
    mov byte [wb_phase], 0
    mov byte [wb_side], 0
    mov byte [wb_bound], 0
    mov byte [wb_command], 0
    mov byte [wb_ack], 0
    mov byte [wb_cmdtype], 0
    mov byte [wb_peerack], 0
    mov byte [wb_owner], 0
    mov word [wb_generation], 0
    mov byte [wb_pending], 0
    mov byte [wb_urgent], 0
    mov word [wb_peer], 0
    mov word [wb_peer+2], 0
    mov word [wb_port], 0
    mov word [wb_ticksim], 0
    call wb_identity
    call wb_board
    cmp byte [wb_practice], 0
    je .serial
    mov word [wb_width], 78*4
    mov byte [wb_phase], 2
    call wb_newgame
    jmp .clock
.serial:
    mov word [wb_width], 156*4
    call wb_openports
.clock:
    mov ax, [wv_clock]
    mov [wv_last], ax
    call OSAPI_GET_TICKS
    mov [wb_last], ax
    mov [wb_seen], ax
    mov [wb_send], ax
    sub word [wb_send], 18
.loop:
    cmp byte [wb_practice], 0
    jne .keys
    call wb_service
    call wb_schedule
.keys:
    call wb_keys
    cmp byte [wb_quit], 0
    jne .leave
    call OSAPI_GET_TICKS
    cmp ax, [wb_last]
    je .frames
    mov [wb_last], ax
    inc word [wb_ticksim]
    call wb_tick
.frames:
    cmp byte [wv_fast], 0
    je .legacy
    mov ax, [wv_clock]
    sub ax, [wv_last]
    cmp ax, 8
    jb .present                ; draw ahead of retrace; motion keeps its own clock
.advance:
    add word [wv_last], 8
    call wb_frame
    mov ax, [wv_clock]
    sub ax, [wv_last]
    cmp ax, 8
    jae .advance
    jmp .present
.legacy:
    ; A small kernel refuses FSXF_RATE; keep its nonblocking text court.
    cmp byte [wv_legacytick], 0
    je .loop
    mov byte [wv_legacytick], 0
    mov byte [wv_legacyframes], 4
.legacy_step:
    call wb_frame
    cmp byte [wb_practice], 0
    jne .legacy_next
    call wb_service
.legacy_next:
    dec byte [wv_legacyframes]
    jnz .legacy_step
.present:
    call wb_draw
    cmp byte [wb_practice], 0
    jne .loop
    call wb_schedule
    jmp .loop
.leave:
    call wb_sound_quiet
    cmp byte [wb_practice], 0
    jne .out
    ; Queue a bounded farewell, while still polling both ports.
    cmp byte [wb_bound], 0
    je .close
    mov byte [wb_packet+3], WB_BYE
    mov si, [wb_port]
    call wb_packet_make
    mov byte [wb_packet+3], WB_BYE
    call wb_encode
    call OSAPI_GET_TICKS
    mov [wb_last], ax
.drain:
    call wb_service
    mov si, [wb_port]
    mov al, [si+WP_TPOS]
    cmp al, [si+WP_TLEN]
    jae .close
    call OSAPI_GET_TICKS
    sub ax, [wb_last]
    cmp ax, 6
    jb .drain
.close:
    call wb_closeports
.out:
    call wv_leave
    call wb_sound_quiet           ; a final receive during BYE cannot leave a cue sounding
    ret

wb_tpl:
    dw 30,24,418,295
    dw wb_title,wb_paint,wb_onkey,wb_onclick
    OS88_MENUSET wb_menus, wb_title, wb_oncmd
        OS88_MENU wb_game, wb_items, 2
    OS88_MENUSET_END wb_menus
wb_items: dw wb_serial,wb_local
wb_game: db 'Game',0
wb_serial: db 'Serial match',0
wb_local: db 'Practice',0
wb_title: db 'WireBall',0
wb_ablines: dw wb_a1,wb_a2,wb_a3,wb_a4,0
wb_a1: db 'WireBall for os8088',0
wb_a2: db 'Pong over a null modem',0
wb_a3: db 'COM1/COM2 - 2400 baud, 8N1',0
wb_a4: db 'Serial protocol version 3',0
%define OS88UI_ABOUT
%define OS88UI_ARM
%include "os88ui.inc"
%include "wireball/wbfront.inc"
%include "wireball/art/wbfront.inc"
%include "wireball/wblink.inc"
%include "wireball/wbgame.inc"
%include "wireball/wbsound.inc"
%include "wireball/wbvideo.inc"

; No worker, no absolute segment into our own region, no IRQ vector.
%assign WB_BSS 0
%macro WB_VAR 2
%1 equ os88_image_end+WB_BSS
%assign WB_BSS WB_BSS+%2
%endmacro
WB_VAR wb_win,2
WB_VAR wb_ox,2
WB_VAR wb_oy,2
WB_VAR wb_abon,1
WB_VAR wb_frontw,2
WB_VAR wb_fronth,2
WB_VAR wb_arth,2
WB_VAR wb_artx,2
WB_VAR wb_buttony,2
WB_VAR wb_frontmono,1
WB_VAR wb_help,1
WB_VAR wb_brects,32
WB_VAR wb_blabels,8
WB_VAR wb_fsi,FSI_SIZE
WB_VAR wb_vseg,2
WB_VAR wb_practice,1
WB_VAR wb_quit,1
WB_VAR wb_latch,1
WB_VAR wb_phase,1             ; 0 searching, 1 echo handshake, 2 playing
WB_VAR wb_side,1              ; 0 left, 1 right; each judges its local paddle
WB_VAR wb_bound,1
WB_VAR wb_pic,1
WB_VAR wb_identity_value,4
WB_VAR wb_peer,4
WB_VAR wb_port,2
WB_VAR wb_ports,WP_SIZE*2
WB_VAR wb_packet,WB_FRAME
WB_VAR wb_last,2
WB_VAR wb_seen,2
WB_VAR wb_send,2
WB_VAR wb_ticksim,2
WB_VAR wb_width,2
WB_VAR wb_left,1
WB_VAR wb_right,1
WB_VAR wb_ballx,2
WB_VAR wb_bally,2
WB_VAR wb_vx,1
WB_VAR wb_vy,1
WB_VAR wb_scorel,1
WB_VAR wb_scorer,1
WB_VAR wb_flags,1
WB_VAR wb_count,1
WB_VAR wb_command,1
WB_VAR wb_ack,1
WB_VAR wb_cmdtype,1
WB_VAR wb_drawold,6           ; three previous sprite VRAM offsets (ffff = absent)
WB_VAR wb_hudold,80           ; last displayed HUD, only changed cells written
WB_VAR wb_hud,80
WB_VAR wb_crossings,2         ; diagnostic: left player crossings through the seam
WB_VAR wb_frames,2
WB_VAR wb_fracx,1            ; quarter-tick remainders, 0..3
WB_VAR wb_fracy,1
WB_VAR wb_returns,1          ; saturates at 16 returns
WB_VAR wb_padold,1
WB_VAR wb_lspin,1
WB_VAR wb_rspin,1
WB_VAR wb_spinage,1
WB_VAR wb_owner,1
WB_VAR wb_generation,2
WB_VAR wb_pending,1
WB_VAR wb_urgent,1
WB_VAR wb_peerack,1
WB_VAR wb_async,1
WB_VAR wb_subtick,2
WB_VAR wb_prevx,2
WB_VAR wb_prevy,2
WB_VAR wb_stepvx,2
WB_VAR wb_stepvy,2
WB_VAR wb_plane,2
WB_VAR wb_gap,2
WB_VAR wb_impact,2
WB_VAR wb_remainder,2
WB_VAR wb_contactpad,1
WB_VAR wb_contactspin,1
WB_VAR wv_fast,1
WB_VAR wv_clock,2
WB_VAR wv_last,2
WB_VAR wv_legacytick,1
WB_VAR wv_legacyframes,1
WV_BSS
WB_VAR wb_marked,1
WB_VAR wb_muted,1
WB_VAR wb_soundseq,2
WB_VAR wb_soundwait,1
WB_VAR wb_soundhz,2
WB_VAR wb_soundnotes,2
WB_VAR wb_soundactive,1
    OS88_BSS WB_BSS
    OS88_IMAGE_END
