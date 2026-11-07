# Kernel size pass 18: the fresh routines and who owns them

Every routine `tools/os88rdiff.py kernel d323a9a6 aede17b1` (kern_big) reports
`changed`, `new`, `gone` or `relaxed`, with the fresh agent that owns it
(BRIEF.md section 2). Assigned by which work moved it; a routine more than one
source touched goes to the one that moved it most. A string-only change
(the TOASTSTR sweep's `*_s_*`, `drv_e*`) is `dir`'s: a reworded message is a
function change and goes to WANTS THE OWNER, so those rows are mostly to read,
not to cut. `gone` rows are listed so the bytes add up; nothing to take there.
If you find a routine here assigned to the wrong agent, say so under CROSS-FILE
rather than editing it.

| owner | routine | status | bytes | delta |
|---|---|---|---|---:|
| dir | `fm_draw_rows` | changed | 182 -> 10 | -172 |
| dir | `fm_draw_rcore` | new | 163 | +163 |
| dir | `fmv_load` | changed | 89 -> 152 | +63 |
| dir | `dsk_sdcmp` | new | 51 | +51 |
| dir | `fmv_rsz` | new | 31 | +31 |
| dir | `fmv_need` | new | 26 | +26 |
| dir | `dsk_ioffx` | new | 25 | +25 |
| dir | `fm_many` | changed | 15 -> 40 | +25 |
| dir | `fmv_take` | changed | 51 -> 66 | +15 |
| dir | `dsk_dest_x` | changed | 26 -> 13 | -13 |
| dir | `hbm_s_serial` | changed | 37 -> 24 | -13 |
| dir | `fm_mzero` | changed | 17 -> 5 | -12 |
| dir | `fmv_cap` | new | 12 | +12 |
| dir | `fm_draw_isel` | new | 11 | +11 |
| dir | `fm_mdiff` | new | 11 | +11 |
| dir | `fmv_store` | changed | 82 -> 92 | +10 |
| dir | `fm_mxor` | new | 9 | +9 |
| dir | `fmv_icorefs` | changed | 66 -> 74 | +8 |
| dir | `fmv_iofs` | changed | 15 -> 22 | +7 |
| dir | `sc_s_self` | changed | 29 -> 22 | -7 |
| dir | `osapi_fs_ent_x` | changed | 60 -> 66 | +6 |
| dir | `fm_armset` | changed | 56 -> 61 | +5 |
| dir | `fm_mdelarm` | changed | 38 -> 43 | +5 |
| dir | `fmv_bcast` | changed | 48 -> 44 | -4 |
| dir | `fm_mbit` | changed | 18 -> 15 | -3 |
| dir | `fm_menter` | changed | 26 -> 29 | +3 |
| dir | `fm_misin` | changed | 8 -> 11 | +3 |
| dir | `fm_onclick_x` | changed | 302 -> 305 | +3 |
| dir | `fmv_get_dir` | changed | 25 -> 28 | +3 |
| dir | `ld_pkg_byname` | changed | 81 -> 84 | +3 |
| dir | `drv_errstr` | changed | 20 -> 22 | +2 |
| dir | `dsk_dest_b0` | gone | 2 | -2 |
| dir | `dsk_ico_stage` | changed | 67 -> 65 | -2 |
| dir | `fm_clamp_scroll` | changed | 44 -> 42 | -2 |
| dir | `fm_mw_cnt` | changed | 6 -> 8 | +2 |
| dir | `toast_now` | changed | 32 -> 30 | -2 |
| dir | `cpc_bit` | changed | 10 -> 11 | +1 |
| dir | `fm_draw_none` | gone | 1 | -1 |
| dir | `fm_mto` | changed | 27 -> 26 | -1 |
| dir | `fm_open_sel` | changed | 98 -> 99 | +1 |
| dir | `clo_s_mfrag` | changed | 24 | +0 |
| dir | `clo_s_mfull` | changed | 21 | +0 |
| dir | `clo_s_mhost` | changed | 23 | +0 |
| dir | `cp_dock_fail` | changed | 24 | +0 |
| dir | `cp_drv_arrow1` | changed | 56 | +0 |
| dir | `cp_drv_arrowok` | changed | 20 | +0 |
| dir | `cp_drv_click` | changed | 225 | +0 |
| dir | `cp_drv_ctl1` | changed | 27 | +0 |
| dir | `cp_drv_end` | changed | 12 | +0 |
| dir | `cp_drv_mempar` | changed | 124 | +0 |
| dir | `cp_drv_pbtn` | changed | 42 | +0 |
| dir | `cp_s_dserr` | changed | 22 | +0 |
| dir | `cp_s_dsok` | changed | 15 | +0 |
| dir | `cp_s_fdno` | changed | 23 | +0 |
| dir | `cp_s_tnowin` | changed | 21 | +0 |
| dir | `cp_snd_note` | changed | 34 | +0 |
| dir | `cp_thm_click` | changed | 275 | +0 |
| dir | `drv_e0` | changed | 7 | +0 |
| dir | `drv_e1` | changed | 21 | +0 |
| dir | `drv_e2` | changed | 23 | +0 |
| dir | `drv_e4` | changed | 18 | +0 |
| dir | `drv_e5` | changed | 18 | +0 |
| dir | `drv_e7` | changed | 21 | +0 |
| dir | `drv_e8` | changed | 17 | +0 |
| dir | `drv_s_off` | changed | 11 | +0 |
| dir | `fdlg_open_x` | changed | 197 | +0 |
| dir | `files_init_x` | changed | 56 | +0 |
| dir | `fm_edit_end` | changed | 104 | +0 |
| dir | `fm_mset` | changed | 47 | +0 |
| dir | `fm_s_clbusy` | changed | 22 | +0 |
| dir | `fm_s_clsame2` | changed | 18 | +0 |
| dir | `fm_s_clsml` | changed | 17 | +0 |
| dir | `fm_s_dskerr` | changed | 11 | +0 |
| dir | `fm_s_edfull` | changed | 12 | +0 |
| dir | `fm_s_eexist` | changed | 12 | +0 |
| dir | `fm_s_ename` | changed | 9 | +0 |
| dir | `fm_s_enodisk` | changed | 8 | +0 |
| dir | `fm_s_enoent` | changed | 13 | +0 |
| dir | `fm_s_ewprot` | changed | 16 | +0 |
| dir | `fm_s_ldfail` | changed | 12 | +0 |
| dir | `fm_s_ldver` | changed | 21 | +0 |
| dir | `fm_s_nomem` | changed | 14 | +0 |
| dir | `fm_s_notimg` | changed | 17 | +0 |
| dir | `fmv_fit` | changed | 46 | +0 |
| dir | `fmv_get_icon` | changed | 59 | +0 |
| dir | `hbm_s_d6` | changed | 23 | +0 |
| dir | `hbm_s_dimg` | changed | 24 | +0 |
| dir | `hbm_s_failed` | changed | 17 | +0 |
| dir | `hbm_s_lost` | changed | 24 | +0 |
| dir | `hbm_s_nobios` | changed | 23 | +0 |
| dir | `hbm_s_nomem` | changed | 18 | +0 |
| dir | `hbm_s_noroom` | changed | 24 | +0 |
| dir | `hbm_s_stale` | changed | 25 | +0 |
| dir | `hbs_stub_end` | changed | 23 | +0 |
| dir | `modc_e_drvup` | changed | 53 | +0 |
| dir | `sc_s_nf` | changed | 24 | +0 |
| dir | `sc_s_path` | changed | 14 | +0 |
| dir | `sc_s_room` | changed | 23 | +0 |
| hot | `cur_move_mono` | changed | 303 -> 396 | +93 |
| hot | `lz_lz4` | changed | 96 -> 178 | +82 |
| hot | `lz_lzb` | changed | 98 -> 171 | +73 |
| hot | `cur_mvcols` | gone | 61 | -61 |
| hot | `disk_mount_x` | changed | 708 -> 761 | +53 |
| hot | `cur_mvrow` | gone | 36 | -36 |
| hot | `task_exit` | changed | 24 -> 50 | +26 |
| hot | `cur_mvbg` | gone | 24 | -24 |
| hot | `lz_arm` | new | 22 | +22 |
| hot | `font_run_x` | changed | 996 -> 976 | -20 |
| hot | `menu_logo_bmp` | gone | 18 | -18 |
| hot | `sw_xfer` | changed | 132 -> 148 | +16 |
| hot | `menu_draw_bar` | changed | 137 -> 151 | +14 |
| hot | `cur_put_mono` | changed | 101 -> 88 | -13 |
| hot | `dsk_rah_fill` | changed | 163 -> 175 | +12 |
| hot | `lz_sync` | new | 12 | +12 |
| hot | `mou_apply` | changed | 191 -> 179 | -12 |
| hot | `task_spawn` | changed | 145 -> 157 | +12 |
| hot | `dsk_fatw_demote` | changed | 86 -> 76 | -10 |
| hot | `mou_whl_cl` | new | 9 | +9 |
| hot | `sch_isr` | changed | 272 -> 280 | +8 |
| hot | `wm_dg_precover` | changed | 15 -> 7 | -8 |
| hot | `sch_switch` | changed | 143 -> 150 | +7 |
| hot | `fm_scroll_by` | changed | 101 -> 107 | +6 |
| hot | `font_char_bb` | changed | 225 -> 219 | -6 |
| hot | `font_width_x` | changed | 17 -> 23 | +6 |
| hot | `gfx_rect_setup` | changed | 117 -> 123 | +6 |
| hot | `vga_blit_span` | changed | 148 -> 142 | -6 |
| hot | `wm_su_bank` | changed | 41 -> 35 | -6 |
| hot | `cur_get_mono` | changed | 58 -> 63 | +5 |
| hot | `fm_c_up` | changed | 42 -> 47 | +5 |
| hot | `mou_apply_w` | new | 5 | +5 |
| hot | `os88ui_btn` | changed | 222 -> 217 | -5 |
| hot | `os88ui_sbar` | changed | 133 -> 128 | -5 |
| hot | `os88ui_sbmove` | changed | 101 -> 96 | -5 |
| hot | `sch_idle_body` | changed | 30 -> 34 | +4 |
| hot | `vga_solid_rect_v` | changed | 153 -> 157 | +4 |
| hot | `wm_draw_win` | changed | 246 -> 250 | +4 |
| hot | `wm_su_merge` | changed | 50 -> 46 | -4 |
| hot | `os88ui_sbpos` | changed | 98 -> 95 | -3 |
| hot | `assoc_scan` | changed | 37 -> 35 | -2 |
| hot | `clip_drop` | changed | 33 -> 31 | -2 |
| hot | `dsk_rah_hint` | new | 2 | +2 |
| hot | `font_char` | changed | 246 -> 248 | +2 |
| hot | `gfx_sub_off` | changed | 14 -> 12 | -2 |
| hot | `mem_bcopy` | changed | 51 -> 49 | -2 |
| hot | `os88ui_sbthdraw` | changed | 56 -> 54 | -2 |
| hot | `vid_pt_clamp` | changed | 43 -> 41 | -2 |
| hot | `wm_rz_swap` | changed | 22 -> 20 | -2 |
| hot | `lz_decomp_big` | changed | 119 -> 120 | +1 |
| hot | `sch_resume` | changed | 10 -> 11 | +1 |
| hot | `sched_init` | changed | 190 -> 191 | +1 |
| hot | `font_ink` | changed | 45 | +0 |
| hot | `gfx_blit4` | changed | 611 | +0 |
| hot | `gfx_hline` | changed | 12 | +0 |
| hot | `kbld_cold` | changed | 2 | +0 |
| hot | `mem_rr_tab` | changed | 72 | +0 |
| hot | `osapi_mouse_feed` | changed | 41 | +0 |
| hot | `ui_task` | changed | 1115 | +0 |
| main | `spl_msg_drv` | gone | 60 | -60 |
| main | `ovl_spl_msg_drv` | changed | 4 -> 60 | +56 |
| main | `kbm_isr` | changed | 95 -> 121 | +26 |
| main | `drv_e9` | new | 21 | +21 |
| main | `assoc_ownex` | new | 20 | +20 |
| main | `assoc_run_x` | changed | 262 -> 282 | +20 |
| main | `vesa_row` | changed | 16 -> 32 | +16 |
| main | `drv_f_pcieth` | new | 13 | +13 |
| main | `bdt_pcieth` | new | 4 | +4 |
| main | `drvt_pcieth` | new | 4 | +4 |
| main | `drv_memk` | changed | 20 -> 22 | +2 |
| main | `mod_cfix` | changed | 57 -> 59 | +2 |
| main | `ovc_bit` | changed | 10 -> 11 | +1 |
| main | `as_s_skip` | changed | 18 | +0 |
| main | `bdl_att` | changed | 98 | +0 |
| main | `cdl_att` | changed | 98 | +0 |
| main | `cfx_slide` | changed | 71 | +0 |
| main | `drv_boot_x` | changed | 248 | +0 |
| main | `drv_init_x` | changed | 57 | +0 |
| main | `drv_load_at` | changed | 177 | +0 |
| main | `drv_shutdown_x` | changed | 13 | +0 |
| main | `drv_status_x` | changed | 27 | +0 |
| main | `hbm_reload_m` | changed | 29 | +0 |
| main | `hbm_sweep` | changed | 157 | +0 |
| main | `hdl_att` | changed | 98 | +0 |
| main | `mem_floor_ax` | changed | 46 | +0 |
| main | `mem_init_x` | changed | 78 | +0 |
| main | `mod_fpr` | changed | 19 | +0 |
| main | `osapi_drv_classk_x` | changed | 55 | +0 |

Net routine delta by owner (kern_big; `.bss` -35 not attributed): dir +270, hot +235, main +125
