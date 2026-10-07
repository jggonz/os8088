; Native XT Excitebike-style racer for os8088.  Contracts: SPEC.md 102.
; Wave 1: the package skeleton - desktop splash and help, the fullscreen
; bracket with its loading screen, the adapter art loaded and checked, a static
; placeholder scene built from the compiled dictionaries, Alt+Enter, Esc.  The
; race itself arrives in the next waves.  Every asset is original and compiled
; from committed sources by tools/excitebike_assets.py; nothing is read from a
; ROM, at build time or at run time (SPEC.md 102, plan section 0).
%include "os88api.inc"
OS88_HEADER 'EXCITEBIKE', xb_entry, 1, OS88_STACK_256
OS88_ICON16
    dw 0x01f0,0x01f0,0x03f0,0x03f0,0x03f0,0x07f8,0x07f8,0x7ffe
    dw 0xffff,0xffff,0xffff,0xffff,0xffff,0xffff,0xfe7f,0xfc3f
    dw 0x0000,0x00e0,0x00e0,0x01e0,0x01c0,0x01e0,0x03b0,0x0370
    dw 0x3bfc,0x6ffe,0x8e79,0x9a49,0x8661,0xc423,0x781e,0x0000
OS88_ICON16_END
%macro SAVE 0
 push ax
 push bx
 push cx
 push dx
 push si
 push di
 push bp
 push es
%endmacro
%macro RESTORE 0
 pop es
 pop bp
 pop di
 pop si
 pop dx
 pop cx
 pop bx
 pop ax
%endmacro
%include "const.inc"

xb_entry:
    mov si, xb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [xb_win], bx
    OS88_REGION_MOVABLE
    OS88_ALTENTER_ARM
    mov si, xb_pref
    call OSAPI_WM_PREFER
    mov al, 1
    call OSAPI_WM_SNAP
    mov si, xb_about
    call OSAPI_ABOUT_SET
    mov word [xb_x0], XB_X0
    mov bx, [xb_win]
    mov ax, xb_timer
    call OSAPI_WM_ONTIMER
    mov bx, [xb_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    mov bx, [xb_win]
    clc
.out: ret

xb_paint:
    SAVE
    call xb_frontpaint
    cmp byte [xb_abon], 0
    je .done
    mov bx, [xb_win]
    mov si, xb_credits
    call os88ui_about_d
.done:
    RESTORE
    ret

xb_about:
    SAVE
    mov byte [xb_abon], 1
    mov bx, [xb_win]
    mov si, xb_credits
    call os88ui_about
    RESTORE
    ret

; The one window timer: it reveals the splash a band at a time and re-arms
; itself until the picture is whole (front.inc). No worker, no blocking wait.
xb_timer:
    SAVE
    call xb_fronttick
    mov bx, [xb_win]
    mov ax, 1
    cmp byte [xb_fs], 0
    jne .off
    mov dx, [xb_reveal]
    cmp dx, [xb_frontheight]
    jb .arm
.off:
    xor ax, ax
.arm:
    call OSAPI_WM_TIMER
    RESTORE
    ret

xb_click:
    SAVE
    cmp byte [xb_abon], 0
    je .go
    mov byte [xb_abon], 0
    jmp .paint
.go:
    ; A cached window drag can move the pixels without invoking our painter,
    ; so translate the click against the window's live content origin.
    push dx
    mov bx, [xb_win]
    call OSAPI_WM_CONTENT
    pop di
    sub cx, ax
    sub di, dx
    mov dx, di
    cmp byte [xb_frontscale], 2
    jne .hit
    shr dx, 1
.hit:
    cmp byte [xb_help], 0
    jne .back
    cmp cx, 112
    jb .done
    cmp cx, 328
    ja .done
    cmp dx, 98
    jb .start
    cmp dx, 110
    ja .done
.back:
    xor byte [xb_help], 1
    call xb_frontreset
    jmp .paint
.start:
    cmp dx, 84
    jb .done
    cmp dx, 98
    ja .done
    call xb_launch
.paint:
    mov bx, [xb_win]
    call OSAPI_WM_CLIP_SET
    jc .done
    call xb_paint
.done:
    RESTORE
    ret

xb_onkey:
    SAVE
    cmp byte [xb_abon], 0
    je .key
    mov byte [xb_abon], 0
    jmp .paint
.key:
    cmp al, 27
    je .back
    cmp ax, KEY_ALTENTER
    je .go
    cmp al, 13
    je .go
    or al, 20h
    cmp al, 'h'
    je .help
    jmp .done
.help:
    xor byte [xb_help], 1
    call xb_frontreset
    jmp .paint
.back:
    cmp byte [xb_help], 0
    je .done
    mov byte [xb_help], 0
    call xb_frontreset
    jmp .paint
.go:
    call xb_launch
.paint:
    mov bx, [xb_win]
    call OSAPI_WM_CLIP_SET
    jc .done
    call xb_paint
.done:
    RESTORE
    ret

xb_tpl: dw 52, 32, 452, 284, xb_title, xb_paint, xb_onkey, xb_click
OS88_PREFER xb_pref, 452,284,452,284,452,154
xb_title: db 'Excitebike',0
xb_credits: dw xb_title,xb_credit1,xb_credit2,xb_credit3,0
xb_credit1: db 'Native 8086 motocross racer',0
xb_credit2: db 'Original artwork, sound and code',0
xb_credit3: db 'Inspired by Excitebike (Nintendo, 1984)',0

%include "front.inc"
%include "video.inc"
%include "world.inc"
%include "game.inc"
%include "sim.inc"
%include "input.inc"
%include "hud.inc"
%include "ai.inc"
%include "audio.inc"
%include "flow.inc"
%include "vga.inc"
%include "cga.inc"
%include "herc.inc"
%include "sprite.inc"
%include "exbtables.inc"
%include "exbtracks.inc"
%include "exbscripts.inc"
%if XB_CID_MAX != EXB_RUNWAY + 2 * EXB_LAP_MAX
%error "const.inc XB_CID_MAX disagrees with the compiler RUNWAY/LAP_MAX"
%endif
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"
%include "os88alt.inc"

%assign XB_BSS 0
%macro VAR 2
 %1 equ os88_image_end + XB_BSS
 %assign XB_BSS XB_BSS + %2
%endmacro
VAR xb_win,2
VAR xb_fs,1
VAR xb_cga,1
VAR xb_herc,1                    ; 1 = Hercules (xb_cga is 1 too: the CGA back end is shared, herc.inc is its card side)
VAR xb_wcols,2                  ; the picture's tile columns: 40, or 45 on the Hercules
VAR xb_xlo,2                    ; a bike's x + this, unsigned, is below xb_xspan when it is on the picture (ai.inc xa_record)
VAR xb_xspan,2
VAR xb_xoff,2                   ; bytes the 40-cell text grid is moved right (Hercules: 4)
VAR xa_ahead,2                  ; where a respawned opponent reappears ahead of the player (pixels)
VAR xb_abon,1
VAR xb_help,1
VAR xb_error,1
VAR xb_nomem,1                  ; a MEM_CLAIM of the bracket was refused (video.inc xb_fsassetfail names it)
VAR xb_frontx,2
VAR xb_fronty,2
VAR xb_frontscale,1
VAR xb_frontcolor,1
VAR xb_frontplay,1
VAR xb_frontheight,2
VAR xb_frontw,2
VAR xb_fronth,2
VAR xb_frontrow,2
VAR xb_frontend,2
VAR xb_fronttextfrom,2
VAR xb_fronttextto,2
VAR xb_frontbatch,2
VAR xb_frontstride,2
VAR xb_frontptr,2
VAR xb_reveal,2
VAR xb_artseg,2
VAR xb_artsize,2
VAR xb_artkind,1
VAR xb_rows,3456                ; 16 decoded splash rows (16 x 216)
VAR xb_queue,432                ; clipped-fallback row conversion
VAR xb_fsi,FSI_SIZE
VAR xb_gfxseg,2
VAR xb_gfxsize,2
VAR xb_recs,20                  ; the 10 record offsets of the loaded GFX file
VAR xb_theme,1
VAR xb_profile,1
VAR xb_plane,1
VAR xb_tileoff,2
VAR xb_x0,2
VAR xb_ncols,2
VAR xb_gshift,1                 ; log2 of a font glyph's bytes: 3 (VGA) or 4 (CGA)
VAR xb_cell,2                   ; VRAM bytes a text cell advances: 1 (VGA) or 2 (CGA)
VAR xb_font,1024
VAR xb_cid,XB_CID_MAX
; the frame clock, governor and the scripted scroll test (game.inc)
VAR xb_subs,2
VAR xb_pitl,2
VAR xb_f0,2
VAR xb_u0,2
VAR xb_n,2
VAR xb_quiet,1
VAR xb_work,2
VAR xb_bigframe,1
VAR xb_wl,2
VAR xb_wh,2
VAR xb_swl,2
VAR xb_swh,2
VAR xb_period,2
VAR xb_acc,4
VAR xb_sacc,4
VAR xb_pcl,2
VAR xb_pch,2
VAR xb_f0p,2
VAR xb_u0p,2
VAR xb_pos,4
VAR xb_S,2
VAR xb_Smax,2
VAR xb_tspeed,2
VAR xb_tpause,1
VAR xb_tframes,2
VAR xb_tset,1
VAR xb_tcol,2
VAR xb_tburst,2
VAR xb_cslast,2
VAR xb_steps,2
VAR xb_frames,2
VAR xb_hudstr,XB_HUDLEN
VAR xb_hudcur,XB_HUDLEN
VAR xb_huddirty,1
VAR xb_hudfrom,2
VAR xb_hudto,2
VAR xb_bikes,XB_MAXBIKES * XB_BIKE_SIZE
VAR xb_shown,2
VAR xb_shownpg,1
VAR xb_shownS,2
; the simulation (sim.inc): state cleared as a block by xm_reset
VAR xm_state,0
VAR xb_cs,2                     ; the game clock is part of a rider's state (an opponent has its own)
VAR xb_ct10,2
VAR xm_trig1,2                  ; lap 1's trigger list (an opponent that respawns needs it)
VAR xm_pos,4
VAR xm_speed,2
VAR xm_A,2
VAR xm_vy,2
VAR xm_gh,2
VAR xm_th,2
VAR xm_vg,2
VAR xm_temp,2
VAR xm_mtimer,2
VAR xm_stepn,2
VAR xm_col,2
VAR xm_lastcol,2
VAR xm_lastfire,2
VAR xm_scr,2
VAR xm_sbase,2
VAR xm_trigp,2
VAR xm_trig2,2
VAR xm_tbase,2
VAR xm_lap2col,2
VAR xm_posmax,4
VAR xm_sc,1
VAR xm_pitch,1
VAR xm_ptimer,1
VAR xm_wcount,1
VAR xm_sqt,1
VAR xm_bounced,1
VAR xm_lane,1
VAR xm_ldir,1
VAR xm_mode,1
VAR xm_cls,1
VAR xm_lap,1
VAR xm_fin,1
VAR xm_sdefer,1
VAR xm_inp,1
VAR xm_n,1
VAR xm_ev,1
VAR xm_stateend,0
XM_STATE_SZ equ xm_stateend - xm_state
%if XM_STATE_SZ & 1
%error "XM_STATE_SZ must be even: the swap is rep movsw"
%endif
VAR xm_hudtmp,8
VAR xb_track,1                  ; which course (0 = the first)
; the flow (flow.inc) and the opponents (ai.inc)
VAR xb_noflow,1                 ; the harness: 1 = the raw race loop of waves 2-3 (no menus, no countdown)
VAR xb_state,1
VAR xb_sel,1                    ; 0 = Selection A (solo), 1 = Selection B (three opponents)
VAR xb_flag,1                   ; 1 = the second pass round the five courses: the harder obstacle set
VAR xb_rep,1                    ; repeats of the last course so far (the qualify window shrinks with it)
VAR xb_attract,1
VAR xb_nai,1
VAR xb_paused,1
VAR xb_result,1
VAR xb_end,1                     ; the frame loop's exit request: result + 1
VAR xb_rank,1
VAR xb_qual,1
VAR xb_newbest,1
VAR xb_posn,1                   ; the finishing position (Selection B)
VAR xb_custom,1                 ; 1 = EXBTRACK.DAT was found and is course 6
VAR xb_cur,1                    ; the menu cursor
VAR xb_key,2                    ; the last typed key (AH scancode, AL ascii)
VAR xb_ink,1                    ; the colour of xb_puts (a palette slot; ignored by the CGA)
VAR xb_cd,2                     ; steps of countdown left
VAR xb_flash,2
VAR xb_bfl,1                    ; the banner flash's state: 0 = the theme's own DAC 13, 1 = flashed (video.inc xb_banner)
VAR xb_flashk,1                  ; 0 = GO!, 1 = the lap flash
VAR xb_fint,2
VAR xb_ksteps,2                 ; the steps the player really made this frame
VAR xb_idle,2
VAR xb_attn,2
VAR xb_attrk,1
VAR xb_ftime,2                  ; the finish time in hundredths, and the par it is judged by
VAR xb_par,2
VAR xb_diff,2
VAR xb_rng,2
VAR xb_best,2 * XB_NTRACKS_MAX * 2   ; per course and pass: the best time in hundredths (0 = none)
VAR xb_tbuf,16
VAR xb_line,48
VAR xb_cseg,2
VAR xb_cn,2
VAR xb_clap,2
VAR xb_chdr,20                  ; the custom course's header, in the layout of exbtracks.inc
VAR xb_cbuf,256                 ; EXBTRACK.DAT
VAR xb_ctrig,64                 ; ...and its trigger list
VAR xa_ps,XM_STATE_SZ
VAR xa_st,XB_NAI_MAX * XM_STATE_SZ
VAR xa_recs,4 * XA_REC_SZ
VAR xa_hz,4 * EXB_NCOLS         ; the lane look-ahead's trouble tables (xa_tables)
VAR xa_sum,4
VAR xa_homel,1
VAR xa_sp,2
VAR xa_rp,2
VAR xa_i,1
VAR xa_cur,2
VAR xa_ppx,2
VAR xa_pscr,2
VAR xa_pfin,1
VAR xa_d,2
VAR xa_best,1
VAR xa_bc,2
VAR xa_nb,1
VAR xa_bases,8
VAR xb_simon,1                  ; 1 = the race, 0 = the wave-2 scripted scroll (the video tests)
VAR xb_inp,1
VAR xb_tmode,1                  ; the harness feeds xb_tscript to the steps and reads xb_ttrace
VAR xb_treset,1
VAR xb_tdone,1
VAR xb_tsn,2
VAR xb_tsi,2
VAR xb_tscript,XB_TSTEPS
VAR xb_ttrace,XB_TSTEPS * 16
; sprites (sprite.inc)
VAR xb_nspr,1
VAR xb_sprseg,2
VAR xb_sprkb,2
VAR xb_sprused,2
VAR xs_p,2
VAR xs_ctab,EXB_NPOSES * XB_PHASES_V * 2   ; a compiled pose's code offset in the sprite claim (0 = interpreted)
VAR xs_cfar,4                   ; ...and the far pointer the draw calls through
VAR xs_hp,2
VAR xs_vink,4
VAR xs_cp,2
VAR xs_ck,2
VAR xs_crow,1
VAR xs_w0,2
VAR xs_w1,2
VAR xs_k,2
VAR xs_ph,1
VAR xs_cnt,2
VAR xs_row,1
VAR xs_tmp,384
; the VGA back end (vga.inc)
VAR xv_pg,1
VAR xv_pbase,2
VAR xv_pgS,6
VAR xv_fpn,3
VAR xv_fp,96
VAR xv_skip,1
VAR xv_x,2
VAR xv_x0,2
VAR xv_c0,2
VAR xv_edge,1
VAR xv_msk,1
VAR xv_xlast,2
VAR xv_y,2
VAR xv_nw,1
VAR xv_left,1
VAR xv_fpp,2
VAR xv_cnt,2
VAR xv_b0,2
VAR xv_mm,1
VAR xv_po,2
VAR xv_ink,4
VAR xv_lay,1
VAR xv_hto,2
VAR xv_nt,2
VAR xv_nb,2
VAR xv_dt,2
VAR xv_db,2
; the CGA back end (cga.inc) and its sprite loader
VAR xc_dseg,2
VAR xc_wb,2                     ; the picture's bytes a row: 80, or 90 (Hercules)
VAR xc_wc1,2                    ; its last column: 39 or 44
VAR xc_crtc,2                   ; the 6845's index port: 3D4h, or 3B4h
VAR xc_vseg,2                   ; the card's segment (FSI_SEG)
VAR xc_colp,2                   ; the card side, by adapter: an entering column, the HUD's two painters,
VAR xc_hudp,2                   ;   the box writer and the retrace wait (cga.inc / herc.inc)
VAR xc_hudcp,2
VAR xc_xferp,2
VAR xc_vrp,2
VAR xh_hrow,2
VAR xh_hbank,2
VAR xc_S,2
VAR xc_new,2
VAR xc_k,2
VAR xc_full,1
VAR xc_xl,2
VAR xc_x,2
VAR xc_x2,2
VAR xc_rw,2
VAR xc_bank,2
VAR xc_tb,2
VAR xc_bb,2
VAR xc_hall,1
VAR xc_hany,1
VAR xc_hflag,XB_HUDLEN
VAR xc_hn,2
VAR xc_hlist,XB_HUDLEN * 2
VAR xc_hudimg,8 * (XB_HUDLEN + 2) * 2
VAR xc_hw,2
VAR xc_s0,2
VAR xc_s1,2
VAR xc_fp,XB_MAXBIKES * XB_BIKE_SIZE
VAR xc_last,XB_MAXBIKES * XB_BIKE_SIZE
VAR xc_fp2,XB_MAXBIKES * XB_BIKE_SIZE
VAR xc_last2,XB_MAXBIKES * XB_BIKE_SIZE
VAR xc_to,1
VAR xc_after,1
VAR xc_snap,2
VAR xv_hi,1
VAR xc_bd,XC_NBOX * 16
VAR xc_cj,2
VAR xc_dy,2
VAR xc_pedge,1
VAR xc_edge,1
VAR xc_gedge,1
VAR xc_db,2
VAR xc_dp,2
VAR xc_gy,2
VAR xc_gb,2
VAR xc_gp,2
VAR xc_gx0,2
VAR xc_gx1,2
VAR xc_dlt,2
VAR xc_dr,2
VAR xc_te,2
VAR xc_pi,2
VAR xc_np,2
VAR xc_plist,XB_MAXBIKES * 2
VAR xc_cnt,2
VAR xc_nbx,2
VAR xc_boxes,4096
VAR xc_bx0,2
VAR xc_by0,2
VAR xc_bw,2
VAR xc_bh,2
VAR xc_bbuf,2
VAR xc_bdw,2
VAR xc_stride,2
VAR xc_nt,2
VAR xc_nb,2
VAR xc_b0,2
VAR xc_bo,2
VAR xc_bi,2
VAR xc_act,2
VAR xc_ov,2
VAR xc_nx0,2
VAR xc_nx1,2
VAR xc_ny,2
VAR xc_ox0,2
VAR xc_ox1,2
VAR xc_oy,2
VAR xc_byte0,2
VAR xc_phase,2
VAR xs_rowi,2
VAR xs_ink55,4
VAR xs_exp,512
VAR xs_w,2
VAR xs_opb,8
VAR xs_orb,8
; sound (audio.inc)
VAR xu_mute,1                   ; 1 = the M key silenced it (0 = sound on: a fresh bss is ON)
VAR xu_live,1
VAR xu_run,1                     ; live AND racing: the race frame's one compare
VAR xu_heavy,1                   ; this race frame ran events or an effect step: its tone call may wait (SPEC.md 102.5)
VAR xu_defer,1                   ; frames a tone call has waited
VAR xu_hold,1                    ; frames left before the speaker's engine pitch may be asked for again
VAR xu_ev,1                      ; the frame's events (xm_ev is cleared at the end of xb_sim_frame), for xu_race_frame
VAR xu_fm,1
VAR xu_pat,1
VAR xu_race,1
VAR xu_fin,1
VAR xu_cur,1
VAR xu_phase,1
VAR xu_sub,1                    ; sub-ticks a tick covers: 1 unless a race frame set it
VAR xu_dt,1
VAR xu_v,3 * XU_VSIZE
VAR xu_pend,3
VAR xu_fxp,2
VAR xu_fxw,2
VAR xu_fxhz,2
VAR xu_fxpri,1
VAR xu_fxid,1
VAR xu_fxn,1
VAR xu_fxchg,1
VAR xu_k,1
VAR xu_enghz,2
VAR xu_enghz2,2
VAR xu_ksent,2
VAR xu_sent,2
VAR xu_lease,1
VAR xu_ntone,2                  ; the harness's: driver calls, all told
; the retrace wait's idle time, kept out of the governor's work figure
VAR xb_sp0,2
VAR xb_sp1,2
VAR xb_spinl,2
VAR xb_spinh,2
xa_hz0 equ xa_hz
xa_hz1 equ xa_hz + EXB_NCOLS
xa_hz2 equ xa_hz + 2 * EXB_NCOLS
xa_hz3 equ xa_hz + 3 * EXB_NCOLS
OS88_BSS XB_BSS
OS88_IMAGE_END
