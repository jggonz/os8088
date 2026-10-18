; Browser 1.6b Download Manager transient/per-instance state.
; Keep buffers bounded and the one-cluster transfer storage dynamically claimed.
br_dl_state       equ br_dl_bss
br_dl_cancel_req  equ br_dl_state + 1
br_dl_close_after equ br_dl_cancel_req + 1
br_dl_mgr_close_after equ br_dl_close_after + 1
br_dl_pending     equ br_dl_mgr_close_after + 1
br_dl_final       equ br_dl_pending + 1
br_dl_fail_reason equ br_dl_final + 1
br_dl_prevstate   equ br_dl_fail_reason + 1
br_dl_have_len    equ br_dl_prevstate + 1
br_dl_headers_done equ br_dl_have_len + 1
br_dl_transfer_encoding equ br_dl_headers_done + 1
br_dl_status_seen equ br_dl_transfer_encoding + 1
br_dl_redirs      equ br_dl_status_seen + 1
br_dl_hnd         equ br_dl_redirs + 1
br_dl_stream      equ br_dl_hnd + 1
br_dl_seq         equ br_dl_stream + 1
br_dl_drive       equ br_dl_seq + 1
br_dl_anim        equ br_dl_drive + 1
br_dl_location    equ br_dl_anim + 1
br_dl_fill        equ br_dl_location + 2
br_dl_clustersz   equ br_dl_fill + 2
br_dl_bufkb       equ br_dl_clustersz + 2
br_dl_needkb      equ br_dl_bufkb + 2
br_dl_largest     equ br_dl_needkb + 2
br_dl_membase     equ br_dl_largest + 2
br_dl_bufseg      equ br_dl_membase + 2
br_dl_dir         equ br_dl_bufseg + 2
br_dl_port        equ br_dl_dir + 2
br_dl_portbak     equ br_dl_port + 2
br_dl_index       equ br_dl_portbak + 2
br_dl_token       equ br_dl_index + 2
br_dl_sent        equ br_dl_token + 2
br_dl_reqn        equ br_dl_sent + 2
br_dl_linelen     equ br_dl_reqn + 2
br_dl_chunkn      equ br_dl_linelen + 2
br_dl_code        equ br_dl_chunkn + 2
br_dl_recvbytes   equ br_dl_code + 2
br_dl_total       equ br_dl_recvbytes + 4
br_dl_written     equ br_dl_total + 4
br_dl_starttick   equ br_dl_written + 4
br_dl_ticks       equ br_dl_starttick + 2
br_dl_speed       equ br_dl_ticks + 2
br_dl_pct         equ br_dl_speed + 2
br_dl_numlo       equ br_dl_pct + 2
br_dl_numhi       equ br_dl_numlo + 2
br_dl_worklo      equ br_dl_numhi + 2
br_dl_workhi      equ br_dl_worklo + 2
br_dl_divlo       equ br_dl_workhi + 2
br_dl_divhi       equ br_dl_divlo + 2
br_dl_x           equ br_dl_divhi + 2
br_dl_y           equ br_dl_x + 2
br_dl_w           equ br_dl_y + 2
br_dl_h           equ br_dl_w + 2
br_dl_ix          equ br_dl_h + 2
br_dl_iy          equ br_dl_ix + 2
br_dl_pc          equ br_dl_iy + 2
br_dl_mx          equ br_dl_pc + 2
br_dl_my          equ br_dl_mx + 2
br_dl_barx        equ br_dl_my + 2
br_dl_barx2       equ br_dl_barx + 2
br_dl_bary        equ br_dl_barx2 + 2
br_dl_layout      equ br_dl_bary + 2
br_dl_screenh     equ br_dl_layout + 1
br_dl_iconw       equ br_dl_screenh + 2
br_dl_iconh       equ br_dl_iconw + 2
br_dl_shelfstep   equ br_dl_iconh + 2
br_dl_pcw         equ br_dl_shelfstep + 2
br_dl_pch         equ br_dl_pcw + 2
br_dl_filew       equ br_dl_pch + 2
br_dl_fileh       equ br_dl_filew + 2
; Reuse geometry-profile words from the former icon sketch. They are assigned
; while choosing the layout, then safely become drawing scratch after create.
br_dl_gx          equ br_dl_iconw
br_dl_gy          equ br_dl_iconh
br_dl_scale       equ br_dl_shelfstep
br_dl_curvey      equ br_dl_pcw
br_dl_curvex1     equ br_dl_pch
br_dl_curvex2     equ br_dl_screenh
br_dl_yicon       equ br_dl_fileh + 2
br_dl_ycaption    equ br_dl_yicon + 2
; Reused by the line painter as the last dash start (path extent).
br_dl_ypath       equ br_dl_ycaption + 2
br_dl_yprogress   equ br_dl_ypath + 2
br_dl_barh        equ br_dl_yprogress + 2
br_dl_ydetail     equ br_dl_barh + 2
br_dl_detailstep  equ br_dl_ydetail + 2
br_dl_ystats      equ br_dl_detailstep + 2
br_dl_yspeed      equ br_dl_ystats + 2
br_dl_ybutton     equ br_dl_yspeed + 2
br_dl_rowy        equ br_dl_ybutton + 2
br_dl_etax        equ br_dl_rowy + 2
br_dl_etay        equ br_dl_etax + 2
br_dl_msg         equ br_dl_etay + 2
br_dl_win         equ br_dl_msg + 2
br_dl_ontimer     equ br_dl_win + 2
br_dl_url         equ br_dl_ontimer + 2
br_dl_host        equ br_dl_url + BR_DL_URLMAX
br_dl_path        equ br_dl_host + BR_DL_HOSTMAX
br_dl_name        equ br_dl_path + BR_DL_PATHMAX
br_dl_dest        equ br_dl_name + 14
br_dl_barbak      equ br_dl_dest + BR_DL_DEST
br_dl_hostbak     equ br_dl_barbak + BR_UBUF
br_dl_pathbak     equ br_dl_hostbak + BR_HOSTMAX
br_dl_loc         equ br_dl_pathbak + BR_PATHMAX
br_dl_reqb        equ br_dl_loc + BR_DL_URLMAX
br_dl_line        equ br_dl_reqb + BR_DL_REQMAX
br_dl_numtxt      equ br_dl_line + BR_DL_LINE
br_dl_disptxt     equ br_dl_numtxt + 12
br_dl_dirbak      equ br_dl_disptxt + 128
br_dl_drivebak    equ br_dl_dirbak + 2
br_dl_lastmsg     equ br_dl_drivebak + 1
br_dl_liveflag    equ br_dl_lastmsg + 2
br_dl_livewidth   equ br_dl_liveflag + 1
br_dl_barfill     equ br_dl_livewidth + 1
br_dl_barnew      equ br_dl_barfill + 2
br_dl_barstate    equ br_dl_barnew + 2
br_dl_runbuf      equ br_dl_barstate + 1
br_dl_bss_end     equ br_dl_runbuf + 16
