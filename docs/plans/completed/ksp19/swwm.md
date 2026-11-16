# Kernel size pass 19 - `swwm`: wave C sweep of wm/events/instance/clip/ui/menu/dock/toast/apps

Branch `ksp19-swwm`, cut from `kernel-size-19` at `f49e64a3`.

## TAKEN (resident, measured `kernsize --json`, base f49e64a3)

| kernel | base | tip | delta |
|---|---:|---:|---:|
| kern_big | 90,081 | 90,063 | **-18** (.text) |
| kern_small | 56,257 | 56,241 | **-16** (.text) |
| kern_emu, kern_mega | - | - | same source changes; builds were in flight at hand-back (re-measure at merge) |

* `menu_relayout`/menu_ab_cell: kent_bp + `jmp kret_es`; wm_hnd's ZF is the About answer (no xchg/or, no push/pop bp): -6.
* `wm_draw_win`: SI is still the window at the paint dispatch (`mov si, bx` gone): -2, 2 clocks faster per paint.
* `ui_task` (kern_big): key/wheel slot loaded straight into BP (`mov bp, di` gone): -2, faster.
* `app_launch`: cascade carried in CX across wm_create (push/pop AX gone): -1.
* `wm_lk` written into `wm_lk_go`, `mov al, 0` before the branch: -5, faster on all three lock states.
* `wm_flag`: WF_KEEPH asked as DL's sign (`or dl,dl / jns`): -1.
* wm_close_req's PARITY nop removed (+... -1): pairs with wm_lk's odd -5 ahead of wm_paint_all; [wm_paint_all, ui_task) is -10. `hotpar` green.
* DOCK_GEOM `add ax,2` -> two `inc` (-1 in the boot overlay and CTRL.DRV each; not resident).
* **Sticky App descriptor**: STKL_D_CLICK/MUP/TICK/CLOSE/WAKE withdrawn (-10 bytes in each Sticky App's block; STKL_XCP.. move down 10; os88stik.py, deskcalperf.py, SPEC.md 110.15 follow; abilock updated, no bump).
* wm_pref_now's header corrected: it returns AL = WC_NDISP (wm_create's shared tail since R14); all three callers bank AX, so no behaviour defect.

## REFUSED / RECORDED

* **wm_hnd smaller-and-faster**: no form found without a record-layout change. Using a register for W_SEG is -16 clocks for +1 byte; `lds` needs W_SEG adjacent to W_CLASS (ABI). Recorded.
* wm_draw_win billing through ui_bill (smaller, ~+100 clocks per paint): refused, hot path.
* wm_wake_call's `mov bx, si` (-2): kept - packages may read BX in a wake handler.
* BANDCOMP title-band `add r,2` sites: knob build only.

## ROWS

fast tier green (78/0/1) on kern_big incl. hotpar, stik, abilock. Reached: stikboot stikswap stikmenu stiknote stiknotesmall stiksmall deskclock deskcalperf deskhover deskclose deskmodal desklayer; menu/About: dispapp alertbtn; wheel mouwheel; timerrace; dispclose dispapps dispcalc dmgcull; installabi abicompat.
