; =============================================================================
; os8088 - apps/dos/doswcell.inc
;
; **THE WINDOWED LAYER'S OWN CELLS, AS THE PART LAYS THEM OUT** (SPEC.md
; 96.54.9). Included by apps/dos/doswp.asm at the end of the part's image -
; zeros, which cost the packed stream a few bytes and the part no bss of its
; own, since op_self expands the image whole. The INLINE layer keeps the
; layout it always had, in dos.asm's HBSS chain; this file is the split's.
;
; OWNERSHIP IS THE WHOLE POINT OF THIS LIST. A cell is the PART's when only
; the layer touches it; it is the HOST's (dos.asm, under DVW_SPLIT) when host
; code reads or writes it while the part may be absent - the menu's ask, the
; run flag every host path tests, the worker's go/done (its loop is resident),
; the spawned-once flag that must outlive a dropped part, the console's dirty
; bit (dos_hk_tty sets it on every INT 21h character), the arena cap, the
; posted door's target (HICALL's thunk reads it through the host's CS) and
; the part's own segment. tools/os88dvwexp.py reads THIS file to know which
; dvw_/dwm_ names are the part's: a part cell named without CS fails the build.
;
; THE PAIRS dvw_start CLEARS AS WORDS stay adjacent (kh/kt, closeq/fs,
; frz/fsreq, parked/gfx), and fs/frz is one word too (dvw_cpace, dvw_capt).
; =============================================================================

    align 2, db 0
dvw_pcells:
dvw_hseg    dw 0                ; the HOST's segment, written by dvw_hbind
dvw_cseg    dw 0                ; the capture's card: B800h or B000h
dvw_csview  dw 0                ; ...and the segment TopView was told
dvw_lss     dw 0                ; LOW_SEG, the worker's stack segment
dvw_fss     dw 0                ; a foreign stack, banked by dvw_tolow
dvw_fsp     dw 0
dvw_tax     dw 0                ; dvw_irq0's AX
dvw_narg    dw 0                ; dvw_nap's: 0 yield, else ticks to sleep
dvw_k08     dd 0                ; the kernel's int 08h, far
dvw_r_ax    dw 0                ; a posted door's ten registers, AX lowest
dvw_r_bx    dw 0
dvw_r_cx    dw 0
dvw_r_dx    dw 0
dvw_r_si    dw 0
dvw_r_di    dw 0
dvw_r_bp    dw 0
dvw_r_es    dw 0
dvw_r_ds    dw 0
dvw_r_fl    dw 0
dvw_kill    db 0                ; the window is closing: stop it
dvw_in      db 0                ; the instance's world is in the IVT
dvw_rq      db 0                ; a door: 1 posted, 2 served, 0 done
dvw_chp     db 0                ; the capture is a lone Hercules's PAGE 1
dvw_poll    db 0                ; empty AH=01h polls, for the yield
dvw_kh      db 0                ; the key ring's head...
dvw_kt      db 0                ; ...and tail
dvw_closeq  db 0                ; a refused close, owed when the run ends
dvw_fs      db 0                ; the program is on the FULL SCREEN
dvw_frz     db 0                ; FROZEN in the window (96.54.1)
dvw_fsreq   db 0                ; ...asked for: Alt+Enter, or a graphics mode
dvw_parked  db 0                ; ...and the worker has stopped for it
dvw_gfx     db 0                ; it set a graphics mode: no way back
dvw_cmode   db 0                ; 0, DVWC_TV, _SEG or _HP
dvw_ctick   db 0                ; ...and the capture's tick count
dvw_dkind   db 0                ; the desktop's VID_* kind (96.54.7)
dvw_hprow   db 0                ; DVWC_HP's probe row, FFh once promoted
dvw_owe     db 0                ; the band, owed whole (96.54.10)
dvw_kbusy   db 0                ; ticks a poll stays awake after a key
dvw_ptk     db 0                ; dvw_xpost was reached from a tick...
dvw_pgap    db 0                ; ...and the ticks since its last post
dwm_own     db 0                ; INT 33h in the window (doswm.inc): the
dwm_last    db 0                ; buttons the program has, the live mask at
dwm_wasf    db 0                ; the last read, in front at the sample
dwm_frnt    db 0                ; before and now,
dwm_tick    dw 0                ; the tick of that sample,
dwm_bx      dw 0                ; and the band's left and top on the screen
dwm_by      dw 0
dvw_sums    times CON_ROWS dw 0 ; TopView's and page 1's row sums
dvw_kq      times 16 dw 0       ; the window's keys
dvw_xblk    times DVW_XB_SIZE db 0  ; the program's vectors and BDA...
dvw_xdsk    times DVW_XB_SIZE db 0  ; ...and the desktop's, while it runs
dvw_pcells_end:
%if dvw_kt != dvw_kh + 1 || dvw_fs != dvw_closeq + 1 || dvw_frz != dvw_fs + 1 || dvw_fsreq != dvw_frz + 1 || dvw_gfx != dvw_parked + 1
 %error "dvw_start clears these in word PAIRS, and fs/frz is one word: keep them adjacent"
%endif
