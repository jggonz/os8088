; =============================================================================
; os8088 - vidext.inc
;
; THE EXTERNAL RENDERER HOOK (SPEC.md 120.3): the one compare each planar
; drawing body spends so that a fifth adapter kind, VID_VESA, can be drawn
; by a loadable driver. Everything that knows what a VBE is lives in
; drivers/vesa/vesa.asm; what this file carries resident is a far pointer,
; a null entry, a table of pointers into the kernel's own state and the
; boot-time attach that arms them.
;
; `VID_EXT` is defined for KERN_BIG in kernel.asm unless NOVIDEXT=1, which is
; the A/B that assembles the kernel before any of this existed. kern_small
; never defines it: that build has no driver layer (SPEC.md 51.0) and no
; planar renderer (39.27), so every macro here expands to nothing there.
;
; Included BEFORE vga12.inc, because the macros below are used in it; the
; data words it names (gfx_color, cur_tabp, ico_ww ...) are forward
; references NASM resolves across passes.
; =============================================================================

%ifdef VID_EXT
%include "vxabi.inc"

; -----------------------------------------------------------------------------
; VIDEXT n - a hooked arm at the TOP of a body, before any push: the driver
;            does the body's work and returns to the body's caller
; VIDEXTC n, done - a hooked arm INSIDE a frame: the driver does the work and
;            execution continues at `done`, the body's own epilogue
; VIDEXTR label - a body VESA REFUSES: branch to the body's refusal (CF = 1)
; VXGO n / VXCALL n - the far call alone, for a hand-written arm: VXGO is
;            VIDEXT's (back to the body's caller), VXCALL VIDEXTC's (on past
;            the selector byte)
;
; Each costs an XT one compare and one taken short jump, GFXDISP's price -
; `cmp byte [vid_planes], 4 / jbe`, because the depth IS the kind's answer:
; vid_depth_set publishes the bpp, 8 or 16, as [vid_planes] on VID_VESA and
; 4 or 1 on every other kind, all three bytes in one word store with
; [vid_mono]. (A byte of its own, [vid_ext], said the same thing until kernel
; size pass 14, for two stores in vid_depth_set and a byte of .text.) The
; hook is the far call ITSELF, `call far [vx_ep]`, with the selector byte
; after it (SPEC.md 120.3.1): the far return the call pushes IS the
; selector's address, so no register carries it - every register is a
; parameter to some primitive (gfx_blit4 uses all eight) - and there is no
; kernel trampoline between the hook and the driver. Bit 7 of the byte
; (VX_CONT) says where the driver goes back to: set, the byte after the
; selector; clear, `vx_kret` below, a kernel `ret` that pops the body's own
; return address - so a VIDEXT body returns to ITS caller.
; -----------------------------------------------------------------------------
%macro VXGO 1
    call far [vx_ep]
    db %1
%endmacro

%macro VXCALL 1
    call far [vx_ep]
    db (%1) | VX_CONT
%endmacro

%macro VIDEXT 1
    cmp byte [vid_planes], 4    ; a packed display's depth is 8 or 16, every
    jbe %%planar                ; other kind's 4 or 1
    VXGO %1                     ; twelve bytes, so the hot routines after a
%%planar:                       ; hook keep their entry parity
%endmacro                       ; (tests/unit/t_hotpar.py)

; VIDEXTM n, mono - the planar body's WHOLE dispatch in one compare: 1bpp to
;            `mono`, a packed display to the driver (VIDEXT's), planar on.
;            [vid_planes] is 1, 4 or the depth, so `cmp [vid_mono], 0 / jne
;            mono` and VIDEXT's compare are one compare and two jumps - five
;            bytes a site, and a planar body a compare faster
%macro VIDEXTM 2-3
    cmp byte [vid_planes], 4
    jb %2                       ; 1bpp
    je %%planar                 ; planar
    VXGO %1                     ; packed: the driver's, back to our caller
%if %0 > 2
    db 0                        ; PARITY, never executed (the driver returns
%endif                          ; to our caller): a third argument asks for it
%%planar:                       ; where the hot routines after a site would
%endmacro                       ; otherwise move to an odd shift

; VIDEXTF n - VIDEXTM split across an entry: VXMONO's compare and 1bpp jump
;            in the CALLER, this at the body's head, entered with those flags
;            (only `mov`, `pop` and `jmp` between). vga_solid_rect's, whose two
;            callers each owe the 1bpp test anyway
%macro VXMONO 1
    cmp byte [vid_planes], 4
    jb %1                       ; 1bpp
%endmacro
%macro VIDEXTF 1
    je %%planar                 ; planar
    VXGO %1                     ; packed: the driver's, back to our caller
    db 0                        ; PARITY, never executed: the five bytes
%%planar:                       ; this saves would move every hot routine
%endmacro                       ; after it to an odd shift

%macro VIDEXTC 2
%%planar0:
    cmp byte [vid_planes], 4
    jbe %%planar
    VXCALL %1
    jmp %2
%if ($ - %%planar0) & 1
    db 0                        ; PARITY, never executed: the shape is an even
%endif                          ; number of bytes whichever jmp NASM picks, so
%%planar:                       ; the hot routines after it keep their entry
%endmacro                       ; parity (tests/unit/t_hotpar.py)

%macro VIDEXTR 1
    cmp byte [vid_planes], 4
    ja %1
%endmacro

; .text with real initialisers: -f bin zeroes nothing
vx_ep       dd 0                ; the driver's primitive entry, seg:off. Armed
                                ; by vx_boot_x; the driver's detach points it
                                ; at vx_null
vx_bpp      dw 8                ; the chosen depth: what vid_depth_set
                                ; publishes as [vid_planes] on VID_VESA
vx_class    db 0                ; the LIVE record's class byte (vxabi.inc's
                                ; VXC_*), written by the driver's geometry
                                ; call: the Display page greys Set Primary
                                ; against it. (No parity pad: viddet.inc's
                                ; vid_depth_set is odd since kernel size pass
                                ; 14, and this byte evens the two out for the
                                ; hot routines after - tests/unit/t_hotpar.py)

; -----------------------------------------------------------------------------
; vx_null - [vx_ep] once the image has gone (SPEC.md 120.3.2): DRAWS NOTHING.
;
; The driver's detach points the hook here rather than at nothing, because a
; hook can still fire after it - drv_shutdown_x detaches every driver while
; [vid_kind] is still VID_VESA, and the arrow, cur_unlazy and vid_setmode all
; run before the machine leaves the GUI. It is the driver's exit protocol and
; nothing else: on past a VX_CONT selector, back through vx_kret otherwise,
; CF = 0 either way (ZF = 1 on the second, as the guard it replaces left it).
; DS is the kernel's at every hook, which is where the selector is.
; -----------------------------------------------------------------------------
vx_null:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+2]              ; the selector's address: the far return
    inc word [bp+2]             ; ...past it, for a VX_CONT site
    test byte [bx], VX_CONT     ; CF = 0, ZF = 1 for a VIDEXT site
    jnz .on
    mov word [bp+2], vx_kret    ; ...and a VIDEXT site goes back to its caller
.on:
    pop bx
    pop bp
    retf
vx_kret:
    ret                         ; THE TAIL: a far return lands here, and this
                                ; pops the hooked body's own return address

; vx_b1 - gfx_blit1_x's hook (SPEC.md 120.3.1). That body is in `.cold`, which
;         carries no data (SPEC.md 2.6), so the selector byte cannot follow a
;         call there: the cold stub far-calls this, and the far return below
;         goes back to it
vx_b1:
    VXCALL VX_BLIT1
    retf

; VX_LAYOUT_ASSERT - the adjacency every multi-cell pointer in vx_blk relies
; on, as `%if`s over symbols declared in files included AFTER this one - so it
; is a macro kernel.asm expands at its foot, where every one of them is known
%macro VX_LAYOUT_ASSERT 0
%if gfx_blit_w - gfx_blit_x != 2 || gfx_blit_h - gfx_blit_x != 4 || gfx_blit_sv - gfx_blit_x != 6
  %error "VXB_BLIT reads gfx_blit_x, _w, _h, _sv as four consecutive words"
%endif
%if gfx_b1pap - gfx_b1ink != 1
  %error "VXB_B1 reads gfx_b1ink then gfx_b1pap"
%endif
%if vgas_nb - vgas_col != 2 || vgas_sdy - vgas_col != 4 || vgas_ydst - vgas_col != 6 || vgas_step - vgas_col != 8 || vgas_rows - vgas_col != 10
  %error "VXB_SCROLL reads the six vgas_* words in declaration order"
%endif
%if gfx_sp_si - gfx_sp_seg != 2 || gfx_sp_n - gfx_sp_seg != 4
  %error "VXB_SPANS reads gfx_sp_seg, _si, _n as three consecutive words"
%endif
%if font_rn_bg - font_rn_fg != 1
  %error "VXB_FONTFG reads font_rn_fg then font_rn_bg"
%endif
%if FONT_FIRST != VX_FONT_FIRST || FONT_LAST != VX_FONT_LAST
  %error "vxabi.inc's glyph range has drifted from font.inc's"
%endif
%endmacro

; -----------------------------------------------------------------------------
; vx_boot_x - attach VESA.DRV and switch the desktop onto it (SPEC.md 120.4)
;
; in:  nothing.  out: nothing (all registers preserved)
;
; vmm_boot_x's shape (SPEC.md 9.11.2): drv_boot_x has already read the row if
; SYSTEM.CFG asked for it - a DRVC_OVL row stops at drv_check with DRVR_SEG
; and DRVR_ENT live and nothing called - and this is the owner that attaches
; it. Called by kmain between kmain_o's return and mem_unblob_x, so the
; loading screen is already down when the mode changes, the blob's vid_switch
; is still aboard, and the desktop's first paint - kmain's next act - is the
; first thing drawn in the new mode.
;
; **IN THE BLOB (`.boot2`), beside vmm_boot_x**, where it was first written:
; it was resident `.cold` while the blob had two bytes of room, and kernel
; size pass 14 gave the blob its thirteenth sector to take it and vx_blk back
; out of resident memory (docs/plans/completed/ksp14/vesa.md).
;
; CS IS THE BLOB'S here, so every edge out is far; DS is the kernel's.
; -----------------------------------------------------------------------------
section .boot2
vx_boot_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bx, vesa_row
    cmp word [bx+DRVR_SEG], 0
    je .out                     ; not wanted, or the load failed and the row
                                ; already carries the DRVE_*
    mov bp, [bx+DRVR_ENT]
    mov cl, [drv_cfg+CFG_VE]    ; the class SYSTEM.CFG asks for (SPEC.md
                                ; 120.2.3), 0 = Auto, which a fresh file
                                ; carries
    mov ax, (VX_ABI_VER << 8) | DRVV_ATTACH ; AH: the ABI the image checks
    call bvs_cgo                ; SI = the VXS_* table in the driver's segment
    dw drv_call
    jc .refuse
    mov bx, vesa_row            ; RE-ESTABLISHED, vmm_boot_x's reason: the
                                ; callee is asked to keep BX and this does
                                ; not depend on it having done so
    mov es, [bx+DRVR_SEG]
    mov ax, [es:si+VXS_ENT]
    mov [vx_ep], ax
    mov [vx_ep+2], es
    mov al, [es:si+VXS_BPP]
    xor ah, ah
    mov [vx_bpp], ax
    push ds
    pop es                      ; ES = KERNEL_SEG for the verb (SPEC.md 51.2):
                                ; drv_call hands the driver the caller's ES,
                                ; and the line above pointed it at the image
    mov bp, [bx+DRVR_ENT]
    mov si, vx_blk
    mov dx, cs                  ; ...the block is HERE, in the blob
    mov al, VXV_INIT
    call bvs_cgo                ; the driver banks the block and fills row 4
    dw drv_call
    jc .refuse
%ifdef KERN_MEGA
    mov bx, vesa_row            ; kern_mega switches into protected mode at
    mov bp, [bx+DRVR_ENT]       ; the end of kmain, and mega_vxpm hands the
    xor dx, dx                  ; image its selectors there (KERN-MEGA-PLAN
    mov al, VXV_PM              ; 14.115): an image that cannot take them
    call bvs_cgo                ; refuses this QUESTION, here, while the
    dw drv_call
    jc .refuse                  ; desktop is still the VGA's
%endif
    or byte [vid_avail], VID_A_VESA
    mov al, VID_VESA
    BLOBCALL bvs_switch         ; the blob's own vid_switch (KSW_SECT); the
                                ; caller owes the repaint and kmain makes it
    jmp short .out
.refuse:
    mov bx, vesa_row            ; RE-ESTABLISHED: the callee may spend BX
    mov byte [bx+DRVR_ERR], DRVE_HW ; 'No hardware found': no VBE here
    mov word [vx_ep+2], 0
    cmp word [bx+DRVR_SEG], 0
    je .out
%ifndef VX_DEBUG_KEEP           ; a debugging define, never a build knob: with
    call bvs_cgo  ; it the refused image stays loaded, so its
    dw drv_release              ; (the blob's hop: drvf_drv_release went)
%endif                          ; probe buffers and mode table can be read out
                                ; of the guest (how the BX contract was caught)
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf
; -----------------------------------------------------------------------------
; vx_blk - the kernel's state, by POINTER (SPEC.md 120.3.2, vxabi.inc's VXB_*)
;
; IN THE BOOT BLOB, beside its one reader: VXV_INIT is sent once, by vx_boot_x
; below, and the driver COPIES the words - so the table is dead the moment
; that call returns, and mem_unblob gives it back with the rest of `.boot2`.
; It reaches the driver as DX:SI, DX being the blob's segment (CS here).
; -----------------------------------------------------------------------------
vx_blk:
    dw VX_ABI_VER                   ; VXB_VER
    dw vid_tab + VID_VESA * VID_REC ; VXB_TABROW
    dw vx_ep                        ; VXB_EP (vx_bpp follows it)
    dw vid_avail                    ; VXB_AVAIL
    dw gfx_color                    ; VXB_COLOR
    dw vga_xorm                     ; VXB_XORM
    dw vga_pn                       ; VXB_PN
    dw gfx_sub_st                   ; VXB_SUB
    dw font_seg                     ; VXB_FONT
    dw wm_clip_r0                   ; VXB_CLIPR0
    dw wm_clip_cm                   ; VXB_CLIPCM
    dw cur_tabp                     ; VXB_CURTAB
    dw cur_hx                       ; VXB_CURHOT
    dw cur_drawn_x                  ; VXB_CURPOS
    dw ico_ww                       ; VXB_ICOG
    dw ico_maskp                    ; VXB_ICOP
    dw ico_x                        ; VXB_ICOXY
    dw ico_c1                       ; VXB_ICOC
    dw vid_kind                     ; VXB_KIND
    dw gfx_pat                      ; VXB_PAT
    dw gfx_blit_x                   ; VXB_BLIT
    dw gfx_b1ink                    ; VXB_B1
    dw gfx_b1hm                     ; VXB_B1HM
    dw vgas_col                     ; VXB_SCROLL
    dw gfx_sp_seg                   ; VXB_SPANS
    dw font_rn_fg                   ; VXB_FONTFG
    dw drv_cfg + CFG_VE             ; VXB_CFGVE (a byte)
    dw vx_kret                      ; VXB_KRET (an address, not a cell)
    dw vx_null                      ; VXB_NULL (likewise)
%if $ - vx_blk != VXB_SIZE
  %error "vx_blk has drifted from vxabi.inc's VXB_* layout"
%endif

section .text

%else                           ; no external renderer in this build
%macro VX_LAYOUT_ASSERT 0
%endmacro
%macro VXMONO 1
    cmp byte [vid_mono], 0
    jne %1
%endmacro
%macro VIDEXTF 1
%endmacro
%macro VIDEXTM 2-3              ; ...so the dispatch is the 1bpp test alone,
    cmp byte [vid_mono], 0      ; byte for byte the two lines VIDEXTM stands
    jne %2                      ; for on kern_big
%endmacro
%macro VIDEXT 1
%endmacro
%macro VIDEXTC 2
%endmacro
%macro VIDEXTR 1
%endmacro
%endif
