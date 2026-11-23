; =============================================================================
; os8088 - cpwlay.inc
;
; The wide Control Panel's shared numbers (SPEC.md 134.3, 134.4): the layout
; both images agree on, the events CTRL.DRV forwards to CPWIDE.DRV, and the
; gate's opcodes CPWIDE.DRV calls CTRL.DRV with. Constants only - no code, no
; data, no section - %included ONCE, by ctrl.inc (kern_big, kern_emu and
; kern_mega; kern_small never assembles a byte of SPEC.md 134), after CP_DIVX
; and before cpwide.inc, which reads them too.
; =============================================================================

; --- the layout, CONTENT-relative (SPEC.md 134.3) -----------------------------
; Every inner dimension derives from the content box, so a third window size is
; a row in cpw_open's table and not a second layout.
CPW_LW    equ 120               ; the page list's width: x 0..CPW_LW-1
CPW_PX    equ CPW_LW + 1        ; the page area's left edge (the rule is CPW_LW)
CPW_HDRH  equ 44                ; the header strip: y 0..43, its rule at 41
CPW_BY    equ CPW_HDRH          ; ...and the page BODY starts under it
CPW_STH   equ 14                ; the status line: the content's last 14 rows,
                                ; with a 1px rule above them

; A HOSTED compact page (SPEC.md 134.3) is drawn by CTRL.DRV's own procs at an
; ORIGIN: cp_origin answers the content origin shifted by these two, so every
; `add di, CP_RX` in the compact code lands the pane where the wide face wants
; it - and cp_page's own erase, from DI + CP_DIVX + 1, starts exactly at the
; page area's left edge.
CPW_HX    equ CPW_PX - CP_DIVX - 1
CPW_HY    equ CPW_BY

; the two window sizes (SPEC.md 134.2): FRAME width and height
CPW_W1    equ 616               ; on a 640x480 primary
CPW_H1    equ 408
CPW_W2    equ 760               ; on one of at least 800x600
CPW_H2    equ 520
CPW_FITX  equ 16                ; the open's predicate (SPEC.md 134.2): the work
                                ; area must be CPW_W1 + this wide, because
                                ; wm_fit's snap (SPEC.md 11.94) may move the
                                ; frame up to 7 px and widen it by 2, and a
                                ; frame snapped DOWN instead is under the face

; --- the events CTRL.DRV forwards (SPEC.md 134.4): BP = one of these at
; --- CPWIDE.DRV's one entry. The first five carry SI = the window
CWV_PAINT equ 0                 ; W_PAINT
CWV_CLICK equ 1                 ; W_ONCLICK   CX/DX = the SCREEN point
CWV_UP    equ 2                 ; W_ONMOUSEUP CX/DX = the SCREEN point
CWV_DRAG  equ 3                 ; W_ONDRAG    CX/DX = the SCREEN point
CWV_KEY   equ 4                 ; W_ONKEY     AX = the key word
CWV_WIN   equ 5                 ; (the first event that names no window)
CWV_TICK  equ 5                 ; CPE_TICK: the clock text, or a second
CWV_LIST  equ 6                 ; the page list's membership changed (cp_list)
CWV_CLOSE equ 7                 ; the panel is closing: free the asset claim
CWV_OPEN  equ 8                 ; the panel is opening: read CPWIDE.DAT, size
CWV_N     equ 9                 ; the window. out CF = 1 refused (compact)

; --- CPE_GATE's opcodes (SPEC.md 134.4): AH = one of these -------------------
; The first CPG_NFAR are the compact face's own FAR bodies, entered past their
; forward with the edge's registers; the rest are near procs and return
; through the gate's `retf`.
CPG_HCLICK  equ 0               ; host: W_ONCLICK's body, CX/DX screen, SI win
CPG_HUP     equ 1               ; host: W_ONMOUSEUP's
CPG_HDRAG   equ 2               ; host: W_ONDRAG's
CPG_HTICK   equ 3               ; host: CPE_TICK's (the Date/Time page's clock)
CPG_HKEY    equ 4               ; host: W_ONKEY's, BX = the key word, SI = win
CPG_NFAR    equ 5
CPG_OPEN    equ 5               ; the resident open: choose the face
CPG_HPAINT  equ 6               ; host: paint the selected page, BX = window
CPG_NAME    equ 7               ; AL = a record -> SI = its list name, staged
CPG_DRVON   equ 8               ; AL = a drv_tab row: load it (31.6's click)
CPG_DRVOFF  equ 9               ; AL = a row: unload it (and VESA's switch-back)
CPG_DRVWANT equ 10              ; AL = a row, CL = 0/1: the want bit ALONE
CPG_DRVTTL  equ 11              ; AL = a row -> SI = its title, staged
CPG_DRVSTAT equ 12              ; AL = a row -> SI = its status sentence, staged
CPG_DRVMEM  equ 13              ; AL = a row -> AX = its drv_memk word
CPG_VIDSET  equ 14              ; AL = a VID_* kind, CL = a VESA class: Set
                                ; Primary's meaning (SPEC.md 31.10, 120.6)
CPG_VXCALL  equ 15              ; AL = a VXV_* verb, CL = its class byte
CPG_VGC     equ 16              ; Force Color applied now (SPEC.md 39.28)
CPG_THMSET  equ 17              ; AL = a THM_* kind: the Theme page's radio
CPG_THMCUST equ 18              ; Customize... (THMPAL.DRV, SPEC.md 76.14.14)
CPG_SAVER   equ 19              ; the screen saver's settings (SPEC.md 79.7)
CPG_WDSET   equ 20              ; SYSTEM.CFG owed at the close (SPEC.md 31.8)
CPG_REPOST  equ 21              ; ...and every window repainted (cp_repost)
                                ; 22-24 were reserved for S7 (SPEC.md 134.8),
                                ; which needed none: refused (cpg_none)
CPG_VIDARR  equ 25              ; AL = 0 Single / 1 Right / 2 Below: the
                                ; desktop arrangement, now (SPEC.md 134.9.5)
CPG_GLYPH   equ 26              ; os88ui_glyph: CX/DX screen, AL = OS88UI_G*,
                                ; BL = 1 greyed - CTRL.DRV's own (134.9.5)
                                ; 27, 28 reserved for S8, unused: refused
CPG_THMPRE  equ 29              ; a preset row was installed: apply Color
                                ; (SPEC.md 134.11)
CPG_N       equ 30
