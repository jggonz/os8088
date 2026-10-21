; =============================================================================
; os8088 - vidext.inc
;
; THE EXTERNAL RENDERER HOOK (SPEC.md 119.3): the one compare each planar
; drawing body spends so that a fifth adapter kind, VID_VESA, can be drawn
; by a loadable driver. Everything that knows what a VBE is lives in
; drivers/vesa/vesa.asm; what this file carries resident is a far pointer,
; two trampolines, a table of pointers into the kernel's own state and the
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
;            does the body's work and vx_go returns to the body's caller
; VIDEXTC n, done - a hooked arm INSIDE a frame: the driver does the work and
;            execution continues at `done`, the body's own epilogue
; VIDEXTR label - a body VESA REFUSES: branch to the body's refusal (CF = 1)
;
; Each costs an XT one compare and one taken short jump, GFXDISP's price. The
; selector byte follows the call and is read by the driver through the return
; address, so no register carries it: every register is a parameter to some
; primitive (gfx_blit4 uses all eight).
; -----------------------------------------------------------------------------
%macro VIDEXT 1
%%top:
    cmp byte [vid_ext], 0
    jz %%planar
    call vx_go
    db %1
    db 0                        ; PARITY, never executed (vx_go returns to the
                                ; primitive's caller): twelve bytes, so the hot
                                ; routines after a hook keep their entry parity
                                ; (tests/unit/t_hotpar.py)
%%planar:
%endmacro

%macro VIDEXTC 2
%%planar0:
    cmp byte [vid_ext], 0
    jz %%planar
    call vx_call
    db %1
    jmp %2
    db 0                        ; PARITY, never executed: the shape is an even
                                ; number of bytes whichever jmp NASM picks, so
                                ; the hot routines after it keep their entry
                                ; parity (tests/unit/t_hotpar.py)
%if ($ - %%planar0) & 1
    db 0
%endif
%%planar:
%endmacro

%macro VIDEXTR 1
    cmp byte [vid_ext], 0
    jne %1
%endmacro

; .text with real initialisers: -f bin zeroes nothing, and [vx_ep]'s segment
; half is the one word the guard in vx_go reads before anything armed it on
; a warm boot that kept the old .bss (int 19h does)
vx_ep       dd 0                ; the driver's primitive entry, seg:off. Armed
                                ; by vx_boot_x, cleared by the driver's detach
vx_bpp      dw 8                ; the chosen depth: what vid_depth_set
                                ; publishes as [vid_planes] on VID_VESA
vx_tmp      dw 0                ; the trampolines' scratch (pop/push targets)
vx_tmp2     dw 0

; -----------------------------------------------------------------------------
; vx_go - tail trampoline: [sp] = the selector's address (a .text stub)
;
; Turns the near return into the far pointer the driver reads the selector
; through, far-calls the driver, discards that pointer and RETURNS TO THE
; PRIMITIVE'S CALLER - the driver did the body's whole work. `pop word [m]`
; and `ret` touch no flag, so a body's CF answer comes back out.
; -----------------------------------------------------------------------------
vx_go:
    ; STKBALANCE-OK: a TAIL TRAMPOLINE. It consumes its own return address
    ; (the stub's, which is the selector's address), far-calls the driver
    ; with that address rebuilt as a far pointer, and its `ret` returns to the
    ; PRIMITIVE's caller - one frame up by design (SPEC.md 119.3.1)
    pop word [vx_tmp]
    push cs
    push word [vx_tmp]
    cmp word [vx_ep+2], 0       ; the safety net: [vid_ext] can be 1 only
    je .none                    ; after an attach armed this, by construction
    call far [vx_ep]
.none:
    pop word [vx_tmp]
    pop word [vx_tmp]
    ret

; vx_call - the same, but execution CONTINUES after the selector byte: for an
;           arm inside a frame whose epilogue the body still owes (font_char's)
vx_call:
    pop word [vx_tmp]
    push cs
    push word [vx_tmp]
    cmp word [vx_ep+2], 0
    je .none
    call far [vx_ep]
.none:
    pop word [vx_tmp]
    pop word [vx_tmp2]
    inc word [vx_tmp]           ; past the selector byte; `inc` keeps CF
    jmp word [vx_tmp]

; vx_goc - FAR entry for a body in `.cold`: the cold stub is
;          `call KERNEL_SEG:vx_goc / db n`, so the far return address on the
;          stack IS the selector pointer, and the exit is a `retf` to the
;          cold body's own caller
vx_goc:
    ; STKBALANCE-OK: vx_go's shape for a cold body - the far return address
    ; on entry IS the selector pointer, and the `retf` after discarding it
    ; returns to the cold body's own far caller (SPEC.md 119.3.1)
    cmp word [vx_ep+2], 0
    je .none
    call far [vx_ep]
.none:
    pop word [vx_tmp]
    pop word [vx_tmp]
    retf

; -----------------------------------------------------------------------------
; vx_blk - the kernel's state, by POINTER (SPEC.md 119.3.2, vxabi.inc's VXB_*)
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
%if $ - vx_blk != VXB_SIZE
  %error "vx_blk has drifted from vxabi.inc's VXB_* layout"
%endif

; -----------------------------------------------------------------------------
; vx_boot_x - attach VESA.DRV and switch the desktop onto it (SPEC.md 119.4)
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
; **IN `.cold`, NOT IN THE BLOB, AND THAT IS A COST THE PLAN RECORDS**: it was
; written for `.boot2` beside vmm_boot_x, and the blob measured 6,142 of
; 6,144 bytes on the day - two bytes of room. So these ~110 bytes are resident
; for the life of the machine (inside the cold rung's slack, no rung crossed),
; until the blob sheds a sector's worth and they can go where vmm_boot_x is.
;
; CS IS COLD_SEG here, so every edge out is far; DS is the kernel's. The one
; edge INTO the blob is registered in tests/ovlrefs.txt with its deadline.
; -----------------------------------------------------------------------------
section .cold
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
    mov ax, (VX_ABI_VER << 8) | DRVV_ATTACH ; AH: the ABI the image checks
    COLDCALL drvf_drv_call      ; (its own segment, unnamed - ROM-PLAN 3.2)
                                ; SI = the VXS_* table in the driver's segment
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
    mov al, VXV_INIT
    COLDCALL drvf_drv_call      ; the driver banks the block and fills row 4
    jc .refuse
    or byte [vid_avail], VID_A_VESA
    mov al, VID_VESA
    call BLOB_SEG_AT:bvs_switch ; the blob's own vid_switch (KSW_SECT), still
                                ; aboard - mem_unblob_x is kmain's NEXT call;
                                ; the caller owes the repaint and kmain makes it
    jmp short .out
.refuse:
    mov bx, vesa_row            ; RE-ESTABLISHED: the callee may spend BX
    mov byte [bx+DRVR_ERR], DRVE_HW ; 'No hardware found': no VBE here
    mov word [vx_ep+2], 0
    cmp word [bx+DRVR_SEG], 0
    je .out
%ifndef VX_DEBUG_KEEP           ; a debugging define, never a build knob: with
    COLDCALL drvf_drv_release   ; it the refused image stays loaded, so its
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
section .text

%else                           ; no external renderer in this build
%macro VIDEXT 1
%endmacro
%macro VIDEXTC 2
%endmacro
%macro VIDEXTR 1
%endmacro
%endif
