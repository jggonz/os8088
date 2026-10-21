; =============================================================================
; vxabi.inc - the private kernel <-> VESA.DRV ABI (SPEC.md 119.3)
;
; INCLUDED BY BOTH SIDES, as source and never as a copy: kernel/vidext.inc and
; drivers/vesa/vesa.asm. vmmabi.inc's shape and in kernel/ for its reason -
; every tool that re-assembles kernel.asm already has kernel/ on its include
; path, and the driver's own rule carries the one extra `-I kernel/`.
;
; The kernel sends VX_ABI_VER in AH at DRVV_ATTACH and the image refuses a
; mismatch, so a stale VESA.DRV beside a new kernel is a refused load with a
; Drivers-page line and not a wrong pixel.
; =============================================================================

VX_ABI_VER  equ 2               ; bump on ANY change to the contract below
                                ; (2: the S2 block pointers and selectors)

; --- the verbs, past DRVV_* -------------------------------------------------
; DRVV_ATTACH (0) probes and chooses a mode and answers SI = the VXS_* table
; below, in the driver's segment; DRVV_DETACH (1) disarms the kernel's far
; pointer and the VID_A_VESA bit through the block it was handed. Neither
; sets a video mode: that is vid_switch's (SPEC.md 119.4).
VXV_INIT    equ 0x40            ; in SI = the kernel's vx_blk (ES = KERNEL_SEG):
                                ; bank the kernel's segment and its pointers,
                                ; fill vid_tab row 4 from the chosen mode.
                                ; Well clear of the DRVV_* range

; --- the attach answer: SI -> this, in the driver's segment -----------------
VXS_ENT     equ 0               ; dw: the near proc vx_go far-calls per primitive
VXS_BPP     equ 2               ; db: 8 or 16 (15 is rendered as 16)
VXS_RSV     equ 3               ; db
VXS_X       equ 4               ; dw: the chosen mode's width
VXS_Y       equ 6               ; dw: ...and height
VXS_MODE    equ 8               ; dw: the VBE mode number
VXS_SIZE    equ 10

; --- the kernel's block: pointer WORDS into the kernel's segment ------------
; vid_dbg_blk's idiom (SPEC.md 57.4): every word names state that already
; exists and nothing is copied. Handed over once, at VXV_INIT.
VXB_VER     equ 0               ; dw VX_ABI_VER - the driver's second check
VXB_TABROW  equ 2               ; -> vid_tab row VID_VESA, 18 bytes: seg,
                                ;    stride, bmask, bshift, rowadd, wrapbit,
                                ;    wrapfix, cw, ch (SPEC.md 39.2)
VXB_EP      equ 4               ; -> vx_ep (dd far entry), then vx_bpp (dw)
VXB_AVAIL   equ 6               ; -> vid_avail (db), bit VXA_BIT is ours
VXB_COLOR   equ 8               ; -> gfx_color (db)
VXB_XORM    equ 10              ; -> vga_xorm (db): 0 = replace, else XOR mask
VXB_PN      equ 12              ; -> vga_pn (db planes a save walks), vga_pm
VXB_SUB     equ 14              ; -> gfx_sub_st, gfx_sub_rs, gfx_sub_ps (3 dw)
VXB_FONT    equ 16              ; -> font_seg, font_base (2 dw)
VXB_CLIPR0  equ 18              ; -> wm_clip_r0 (db first row), rows (db)
VXB_CLIPCM  equ 20              ; -> wm_clip_cm (db column mask, bit 7 left)
VXB_CURTAB  equ 22              ; -> cur_tabp (dw -> white rows; +12 = black)
VXB_CURHOT  equ 24              ; -> cur_hx, cur_hy (2 dw)
VXB_CURPOS  equ 26              ; -> cur_drawn_x, cur_drawn_y (2 dw)
VXB_ICOG    equ 28              ; -> ico_ww, ico_h, ico_rows, ico_rb (4 dw)
VXB_ICOP    equ 30              ; -> ico_maskp, ico_datap (2 dw, kernel offs)
VXB_ICOXY   equ 32              ; -> ico_x, ico_y (2 dw, x signed)
VXB_ICOC    equ 34              ; -> ico_c1 (db), ico_c2 (db)
VXB_KIND    equ 36              ; -> vid_kind (db)
VXB_PAT     equ 38              ; -> gfx_pat (dw -> 8 pattern bytes, kernel)
VXB_BLIT    equ 40              ; -> gfx_blit_x, gfx_blit_w, gfx_blit_h,
                                ;    gfx_blit_sv (4 dw): gfx_blit4's block
VXB_B1      equ 42              ; -> gfx_b1ink, gfx_b1pap (2 db). NOT the
                                ;    composer's polarity byte, which only a
                                ;    BAND=1 knob kernel declares: a band is
                                ;    taken in SCREEN polarity, set = ink
VXB_B1HM    equ 44              ; -> gfx_b1hm (db): the head column's mask
VXB_SCROLL  equ 46              ; -> vgas_col, vgas_nb, vgas_sdy, vgas_ydst,
                                ;    vgas_step, vgas_rows (6 dw)
VXB_SPANS   equ 48              ; -> gfx_sp_seg, gfx_sp_si, gfx_sp_n (3 dw)
VXB_FONTFG  equ 50              ; -> font_rn_fg (db fg), font_rn_bg (db)
VXB_SIZE    equ 52

; font_char's glyph range, which the run body paints a blank cell for: the
; kernel's FONT_FIRST/FONT_LAST, asserted equal in vidext.inc
VX_FONT_FIRST equ 32
VX_FONT_LAST  equ 126

VXA_BIT     equ 0x10            ; VID_A_VESA: 1 << VID_VESA, VID_VESA being 4

; --- the selectors: the byte after `call vx_go` / `call vx_call` ------------
; Each names a kernel body and takes that body's registers (SPEC.md 119.3.1).
VX_FILL     equ 0               ; vga_solid_rect: AX/BX/CX/DX, [gfx_color],
                                ; [vga_xorm]
VX_GRAY     equ 1               ; gfx_fill_gray_raw: the rect
VX_PAT      equ 2               ; gfx_fill_pat_raw: the rect, [gfx_pat]
VX_SAVE     equ 3               ; vga_save_vram: rect, ES:DI; DI advanced
VX_RESTORE  equ 4               ; vga_restore_vram: rect, ES:SI; SI advanced
VX_FONTCHAR equ 5               ; font_char .vram: CX = x, DX = y, SI = glyph
                                ; rows (+r0), [wm_clip_r0] rows, [wm_clip_cm]
VX_ICON     equ 6               ; ico_core's planar arm: the ico_* cells
VX_CURPUT   equ 7               ; cur_put_1: save under and draw at cur_drawn
VX_CURGET   equ 8               ; cur_get_1: put the save-under back
VX_CURMOVE  equ 9               ; cur_move_1: AX/BX = the new position
VX_SETMODE  equ 10              ; vid_setmode: enter the chosen mode
VX_BLIT4    equ 11              ; gfx_blit4: BX = y, the gfx_blit_* block,
                                ; ES = the source segment, BP = its stride;
                                ; unclipped in x and y
VX_BLIT1    equ 12              ; gfx_blit1_x (cold, through vx_callf) at
                                ; .ybot: AX/BX clipped x/y, CX width, DX rows,
                                ; ES:SI the band, BP its stride, the pen and
                                ; the head mask through the block
VX_SCROLL   equ 13              ; gfx_scroll: the vgas_* block
VX_SPANS    equ 14              ; gfx_spans: AX = the first row, the gfx_sp_*
                                ; block (already clipped in y), [gfx_color]
VX_FONTRUN  equ 15              ; font_run's planar arm, no region armed and
                                ; not disabled: CX = x, DX = y, ES:SI the
                                ; string, font_rn_fg/bg; leaves [gfx_color]
                                ; = fg
VX_NSEL     equ 16
