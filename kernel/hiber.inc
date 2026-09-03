; =============================================================================
; os8088 - hiber.inc
;
; Hibernate the machine to a file on the hard disk, and resume it at the next
; boot (SPEC.md 86). Prefix `hb_` for the resident half, `hbf_` for its .cold
; thunks, `hbk_` for the far entries the module reaches cold code through,
; `hbm_` for the on-demand module and `hbs_` for the resume stub.
;
; WHAT IS RESIDENT is the least that has to be: the System menu's three
; strings live in menu.inc beside their siblings; here are the window
; template and title, the state block the image carries, the .text thunks a
; kernel window's callbacks need (a W_SEG-0 window is dispatched NEAR in
; KERNEL_SEG), and in .cold the boot probe, the greying predicate, the far
; entries, and the thunks that load the module before calling into it -
; cpf_cp_paint's shape (SPEC.md 2.8).
;
; EVERYTHING ELSE IS HIBER.DRV, MOD_HIBER: the window, the write, the
; boot-time question, the extent walk, the stub
; that puts memory back, and the routine that wakes the restored kernel. It
; is .cold code with the address changed (SPEC.md 2.8): CS = its own claim,
; DS = KERNEL_SEG, every kernel address the one this assembly gave it, and
; its own strings read through CS (SPEC.md 2.8.6) - nothing can draw them
; while the image is not loaded, because every path in is a thunk below that
; runs mod_need first.
;
; kern_small has none of it (SPEC.md 62.9.15's reason: the 128KB machine is
; not the one with a hard disk to hibernate to), and ships a one-entry stub
; module so that os88mod.py cuts the same four files out of every kernel.
; =============================================================================

HB_M_ASK    equ 0               ; the window offers Hibernate... / Cancel
HB_M_BUSY   equ 1               ; the image is being written (or read)
HB_M_RESUME equ 2               ; the boot-time question: Resume / Discard
HB_M_GONE   equ 3               ; the window closed: the thunk drops the image

; --- the module's entry slots (SPEC.md 2.8.1) - its ABI with the thunks -----
HBE_PAINT   equ 0
HBE_ONCLICK equ 1
HBE_ONUP    equ 2
HBE_ONKEY   equ 3
HBE_KINIT   equ 4
HBE_PERFORM equ 5
HBE_OPEN    equ 6
HB_NENT     equ 7
%if HB_NENT > MOD_NENT
  %error "hiber: more entries than mod_fp has slots for"
%endif
HBFP        equ mod_fp + MOD_HIBER * MODFP_STRIDE

; --- API cells the module calls (SPEC.md 20.3); the kernel has no copy of
; --- apps/os88api.inc, so the four it needs are named here ------------------
HB_API_FILL   equ 0x0038        ; AX=x1, BX=y1, CX=x2, DX=y2 in [gfx_color]
HB_API_FONTX  equ 0x0068        ; CX=x, DX=y, the string at the CALLER's DS:SI
HB_API_COLOR  equ 0x00C0        ; AL -> [gfx_color]
HB_API_XCAPS  equ 0x0190        ; out BL = free block-table entries (SPEC.md 41)

; --- the pointer file, HIBERNAT.PTR (SPEC.md 86.3) ---------------------------
HBP_MAGIC   equ 0               ; 'HIB1'
HBP_BUILD   equ 4               ; dw BUILD_NUM
HBP_STAMP   equ 6               ; dw MOD_STAMP
HBP_MEMTOP  equ 8               ; dw [mem_top]
HBP_VID     equ 10              ; db [vid_kind]
HBP_SP      equ 12              ; dw the banked SP...
HBP_SS      equ 14              ; ...and SS
HBP_WAKE    equ 16              ; dd hbm_wake, off:seg
HBP_DRVS    equ 20              ; dw the rows detached before the image
HBP_PATH    equ 32              ; db[32] the folder from the root (PTH_BUF)
HBP_NAME    equ 64              ; db[13] the image's name
HBP_SIZE    equ 80

; --- the staging area in the text framebuffer (SPEC.md 86.5) -----------------
; B800:0000 (or B000:0000 on a Hercules primary), which vid_reboot has just
; made a linear 16KB+ of RAM that no rung of the ladder owns. The stub's CS,
; DS and SS are all this segment, and every datum it reads is at one of
; these offsets - numeric, so the stub is position-independent.
HS_CODE     equ 0x0000          ; the stub, up to HS_CODE_MAX bytes
HS_CODE_MAX equ 0x0400
HS_UNIT     equ 0x0400          ; dw the int 13h drive
HS_SPT      equ 0x0402          ; dw sectors per track
HS_HEADS    equ 0x0404          ; dw heads
HS_NX       equ 0x0406          ; dw extents
HS_SS       equ 0x0408          ; dw the banked SS...
HS_SP       equ 0x040A          ; ...and SP
HS_WAKE     equ 0x040C          ; dd hbm_wake, off:seg
HS_MASKM    equ 0x0410          ; db the master PIC's mask before the stub
HS_MASKS    equ 0x0411          ; db ...and the slave's
HS_CLK      equ 0x0412          ; db[10] clk_sec..clk_year of the fresh boot
HS_LBA      equ 0x0420          ; dd the run's first LBA (scratch)
HS_LEFT     equ 0x0424          ; dw sectors left in this extent
HS_K        equ 0x0426          ; dw the file sector index = linear / 512
HS_N        equ 0x0428          ; dw sectors in the call being issued
HS_CHS      equ 0x042A          ; dw CX, dw DX as int 13h wants them
HS_SLAVE    equ 0x042E          ; db 1 = there is a second 8259 to mask and
                                ; put back; 0 on an XT, whose port 0xA0 is
                                ; the NMI mask register (SPEC.md 9.9.2)
HS_IVT      equ 0x0500          ; db[1024] file sectors 0..1, the IVT
HS_EXT      equ 0x0A00          ; {dd lba, dw count, dw 0} x HS_XMAX
HS_XMAX     equ 1280            ; one per sector of a 640KB image at worst
HS_STACK    equ 0x3FF0          ; the stub's stack top: under a CGA's 16KB
%if HS_EXT + HS_XMAX*8 > HS_STACK - 512
  %error "hiber: the extent list would run into the stub's stack"
%endif
HB_XKB      equ (HS_XMAX*8 + 1023) / 1024   ; the claim the walk builds it in

%ifdef KERN_BIG

section .text
; --- resident data: the template and the title (read through DS by wm) -----
hb_tpl:     dw 160, 150, 320, 100, hb_ttl, hb_paint, hb_onkey, hb_onclick
hb_ttl:     db 'Hibernate', 0
hb_s_ptr:   db 'HIBERNAT.PTR', 0    ; read by the probe (cold) and the module
hb_s_img:   db 'HIBERNAT.IMG', 0    ; the image, beside it in the root

; --- the .text thunks: a kernel window's callbacks are near procs here ------
hb_paint:   call COLD_SEG:hbf_paint
            ret
hb_onclick: call COLD_SEG:hbf_onclick
            ret
hb_onup:    call COLD_SEG:hbf_onup
            ret
hb_onkey:   call COLD_SEG:hbf_onkey
            ret
hb_kinit:   call COLD_SEG:hbf_kinit     ; KD_INIT: preserves every register
            ret

section .bss
; The state block. Part of the image, which is the point: what hb_perform
; banks before the write is what hbm_wake reads after the restore.
hb_mode:    resb 1              ; HB_M_*
hb_vol:     resb 1              ; the volume the image is (or will be) on
hb_down:    resb 1              ; the button the press armed, 1/2, 0 = none
hb_pad:     resb 1
hb_drvmask: resw 1              ; drv_tab rows detached before the image
hb_sp:      resw 1              ; hbm_perform's entry SP (SPEC.md 86.4)
hb_wakep:   resw 2              ; hbm_wake as off:seg, in the image's module
hb_resumes: resw 1              ; how many times this machine has woken
hb_unit:    resw 1              ; the transport facts (SPEC.md 86.5 step 1)
hb_spt:     resw 1
hb_heads:   resw 1
hb_base:    resd 1
hb_xseg:    resw 1              ; the extent list's claim, 0 = none
hb_nx:      resw 1              ; ...and how many it holds
hb_lbl:     resb 16             ; a module string staged where DS can see it
hb_rect:    resw 4              ; a button rect staged for os88ui_btn

section .cold
; =============================================================================
; The resident half's cold code: the probe, the predicate, the far entries
; and the thunks.
; =============================================================================

; -----------------------------------------------------------------------------
; hb_probe - is there a hibernation to offer? (SPEC.md 86.5; kmain, after
;            drv_boot and before the first paint)
; in:  nothing
; out: nothing (all registers preserved). A hit records the volume in [hb_vol]
;      and posts UI_RBQ_ASK, which ui_task's first pass spends.
;
; The ONE root a pointer can be in - hb_pick's volume, the one a hibernate
; writes to - in a drv_vol_bank/back bracket (SPEC.md 51.5.2) so the machine
; is left standing where drv_boot left it, and reached the way drv_load
; reaches a driver: a QUIET navigation (18.9: no listing, no icon harvest)
; and a walk of the directory sectors, not the snapshot. A floppy-only
; machine pays eight compares and no I/O; a machine with a hard disk pays
; one quiet mount of its root.
; -----------------------------------------------------------------------------
hb_probe_x:
    push ax
    push dx
    push si
    mov byte [hb_mode], HB_M_GONE   ; .bss is whatever the LAST kernel left
    mov word [hb_resumes], 0    ; (a warm boot clears nothing), and two of
    mov word [hb_xseg], 0       ; these are read before anything writes them
    call COLD_SEG:hb_pick_x     ; AL = the volume
    jc .out
    mov [hb_vol], al
    call drv_vol_bank
    mov dl, al
    xor ax, ax                  ; the root
    call dsk_chdir_q_x
    jc .back
    mov si, hb_s_ptr
    call drv_find               ; the directory sectors themselves
    jc .back
    mov byte [ui_rebootq], UI_RBQ_ASK
.back:
    call drv_vol_back
.out:
    pop si
    pop dx
    pop ax
    retf

; -----------------------------------------------------------------------------
; hb_pick - the disk a hibernation goes to, or is looked for on (SPEC.md 86.2):
;           the one the machine BOOTED from when that is a fixed disk, else
;           the first fixed volume there is - and never a DVK_FILE row, which
;           dsk_vol_fixed calls fixed and which has no sectors the stub could
;           read. THE predicate: the menu item, the probe and the window all
;           ask it, so what greys, what is probed and what is written to
;           cannot disagree (SPEC.md 47 rule 5)
; in:  nothing
; out: CF = 0 and AL = its index; CF = 1 no such volume
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
hb_pick_x:
    push bx
    push cx
    mov cl, [dsk_bootvol]
    call hb_pick1
    jnc .out
    xor cx, cx
.vol:
    call hb_pick1
    jnc .out
    inc cx
    cmp cx, DVOL_MAX
    jb .vol
    stc
.out:
    pop cx
    pop bx
    retf

hb_pick1:                       ; is volume CL one? out: CF = 0 and AL = CL
    mov bl, cl
    call dsk_vol_fixed_x        ; AL = 1 fixed, AH = the kind; CF = no row
    jc .no
    or al, al
    jz .no
    cmp ah, DVK_FILE
    je .no
    mov al, cl
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; hb_ok - may the machine hibernate? (SPEC.md 86.2, 47 rule 5)
; in:  nothing
; out: CF = 0 yes; CF = 1 and AL = 1 no fixed volume / 2 extended memory held
; clobbers: AL (the output), flags
;
; Called on the press that opens the menu bar, so it is at most eight row
; tests and one far call into a table - no I/O, no probe.
; -----------------------------------------------------------------------------
hb_ok_x:
    push bx
    call COLD_SEG:hb_pick_x     ; the volume a hibernate would go to
    mov al, 1
    jc .no
    call KERNEL_SEG:HB_API_XCAPS    ; BL = free block-table entries; on a
    cmp bl, XM_MAX_BLKS         ; machine with no store, all of them
    jb .xms
    clc
    jmp short .out
.xms:
    mov al, 2
.no:
    stc
.out:
    pop bx
    retf

; --- far entries for the module (SPEC.md 2.6): cold bodies that end in a near
; --- ret, reached from a segment of their own. Four bytes each.
hbk_dsk_chdir:      call dsk_chdir_x
                    retf
hbk_dsk_find_name:  call dsk_find_name_x
                    retf
hbk_dsk_get_dir:    call dsk_get_dir_x
                    retf
hbk_dsk_clus2lba:   call dsk_clus2lba_x
                    retf
hbk_dsk_next_clus:  call dsk_next_clus_x
                    retf
hbk_dsk_fatw_drop:  call dsk_fatw_drop
                    retf
hbk_dsk_rah_flush:  call dsk_rah_flush
                    retf
hbk_drv_blk_call:   call drv_blk_call_x
                    retf
hbk_mem_claim:      call mem_claim_x
                    retf
hbk_mem_free:       call mem_free_x
                    retf

; --- the thunks that load the module and call in (SPEC.md 2.8, cpf_'s shape)
hbf_paint:
    push ax
    mov al, MOD_HIBER
    call mod_need
    pop ax
    jc .gone
    call far [HBFP + HBE_PAINT*4]
.gone:
    retf

hbf_onclick:
    push ax
    mov al, MOD_HIBER
    call mod_need
    pop ax
    jc .gone
    call far [HBFP + HBE_ONCLICK*4]
.gone:
    retf

hbf_onup:                       ; a release can only follow a press this
    push ax                     ; window took, so mod_live, not mod_need
    mov al, MOD_HIBER           ; (SPEC.md 13.8.3)
    call mod_live
    pop ax
    jc .gone
    call far [HBFP + HBE_ONUP*4]
    call hbf_dropck
.gone:
    retf

hbf_onkey:
    push ax
    mov al, MOD_HIBER
    call mod_need
    pop ax
    jc .gone
    call far [HBFP + HBE_ONKEY*4]
    call hbf_dropck
.gone:
    retf

hbf_kinit:
    push ax
    mov al, MOD_HIBER
    call mod_need
    pop ax
    jc .gone
    call far [HBFP + HBE_KINIT*4]
.gone:
    retf

; hbf_open - the System menu's Hibernate... (ui_cmd, no lock held)
; A refusal here is about the DISK - the module could not be read - and the
; toast names it the way the Control Panel's does, out of the same table.
hbf_open:
    push ax
    mov al, MOD_HIBER
    call mod_need
    pop ax
    jc .no
    call far [HBFP + HBE_OPEN*4]
    retf
.no:
    call hbf_nodisk
    retf

hbf_nodisk:
    push ax
    push bx
    push cx
    mov al, 1
    mov bx, cp_opentab
    mov cx, CP_NOPEN
    call KERNEL_SEG:toast_say
    pop cx
    pop bx
    pop ax
    ret

; hbf_perform - a posted UI_RBQ_HIBER / UI_RBQ_RESUME / UI_RBQ_ASK, spent by
;               ui_task with nothing held (SPEC.md 86.4, 86.5)
; in:  AL = which
; out: nothing. Returns only on a refusal or a discard: a hibernate ends in
;      int 19h and a resume ends in the stub.
hbf_perform:
    push ax
    mov al, MOD_HIBER
    call mod_need
    pop ax
    jc .no
    call far [HBFP + HBE_PERFORM*4]
    call hbf_dropck
    retf
.no:
    cmp al, UI_RBQ_ASK          ; a question that cannot be asked is not
    je .quiet                   ; worth a toast: the pointer stays for a
    call hbf_nodisk             ; boot that has the system disk in
.quiet:
    retf

; hbf_dropck - the window closed inside the call that just returned, so the
;              image goes back now, from OUTSIDE it (SPEC.md 86.1)
hbf_dropck:
    cmp byte [hb_mode], HB_M_GONE
    jne .out
    push ax
    mov al, MOD_HIBER
    call mod_drop
    pop ax
.out:
    ret

; hbf_closed - app_close_win's KIND_HIBER hook (SPEC.md 86.1): the window went
;              by a route the module did not take - the close box, Close
;              Window, the Task Manager, the restart sweep - so nothing of the
;              module is on the stack and the image can go now. The module's
;              own closes set HB_M_GONE FIRST, and that is the test: a GONE
;              window is the module closing itself, and hbf_dropck drops the
;              image once it has returned
hbf_closed:
    cmp byte [hb_mode], HB_M_GONE
    je .out
    mov byte [hb_mode], HB_M_GONE
    push ax
    mov al, MOD_HIBER
    call mod_drop
    pop ax
.out:
    retf

; =============================================================================
; THE MODULE - HIBER.DRV (SPEC.md 2.8, 86)
;
; CS = the image's own claim, DS = KERNEL_SEG. Kernel data by name through
; DS; this file's own strings through CS; kernel code through KERNEL_SEG:cw_*
; shims (.text), COLD_SEG:*_x / hbk_* far entries (.cold) and the API cells.
; =============================================================================
section .modh
modh_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER                  ; MOD_H_VER
    db MOD_HIBER                ; MOD_H_ID
    dw BUILD_NUM                ; MOD_H_BUILD  (SPEC.md 2.8.2)
    dw MOD_STAMP                ; MOD_H_STAMP
    dw modh_end                 ; MOD_H_IMG
    dw HB_NENT                  ; MOD_H_NENT
    dw modh_e_paint             ; HBE_PAINT
    dw modh_e_onclick           ; HBE_ONCLICK
    dw modh_e_onup              ; HBE_ONUP
    dw modh_e_onkey             ; HBE_ONKEY
    dw modh_e_kinit             ; HBE_KINIT
    dw modh_e_perform           ; HBE_PERFORM
    dw modh_e_open              ; HBE_OPEN
    times MOD_NENT - HB_NENT dw 0

modh_e_paint:   call hbm_paint
                retf
modh_e_onclick: call hbm_onclick
                retf
modh_e_onup:    call hbm_onup
                retf
modh_e_onkey:   call hbm_onkey
                retf
modh_e_kinit:   call hbm_kinit
                retf
modh_e_perform: call hbm_perform
                retf
modh_e_open:    call hbm_open
                retf

; --- geometry of the window's content (SPEC.md 86.1) -------------------------
HBW_LX      equ 8               ; text left, content-relative
HBW_LY      equ 6               ; first line's top
HBW_LH      equ 12              ; line pitch
HBW_BY      equ 54              ; the button band's top
HBW_BH      equ 16              ; ...and height
HBW_B1X     equ 8               ; the first button
HBW_B1W     equ 120
HBW_B2X     equ 140             ; the second
HBW_B2W     equ 80
HBW_CW      equ 320 - 2         ; content width: the template's W_W less the
HBW_CH      equ 100 - TITLE_H - 1   ; borders; height less the title bar

; -----------------------------------------------------------------------------
; hbm_str - draw one of THIS image's strings (SPEC.md 2.8.6)
; in:  CX = x, DX = y, SI = the string's offset in this image; the pen is set
; out: nothing (all registers preserved)
;
; The transparent string cell is an X stub: ES becomes the CALLER's DS and DS
; the kernel's, so with DS = CS for the call the string is read from here and
; [gfx_color] from where it lives.
; -----------------------------------------------------------------------------
hbm_str:
    push ds
    push cs
    pop ds
    call KERNEL_SEG:HB_API_FONTX
    pop ds
    ret

; -----------------------------------------------------------------------------
; hbm_stage - copy one of this image's NUL strings to DS:DI (kernel memory)
; in:  SI = the string's offset here, DI = the destination, CX = its size
; out: nothing (all registers preserved). Always NUL-terminated.
; -----------------------------------------------------------------------------
hbm_stage:
    push ax
    push cx
    push si
    push di
    dec cx                      ; room for the NUL
.copy:
    mov al, [cs:si]
    mov [di], al
    or al, al
    jz .done
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_toast - say one of this image's strings in the menu bar (SPEC.md 59)
; in:  SI = the string's offset here
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_toast:
    push cx
    push es
    push cs
    pop es                      ; toast_show takes ES:SI
    xor cx, cx
    call KERNEL_SEG:cw_toast_show
    pop es
    pop cx
    ret

; -----------------------------------------------------------------------------
; hbm_origin - the window's content origin
; in:  SI = window ptr
; out: AX = content left, DX = content top
; -----------------------------------------------------------------------------
hbm_origin:
    push bx
    mov bx, si
    call KERNEL_SEG:cw_wm_content
    pop bx
    ret

; -----------------------------------------------------------------------------
; hbm_btnrect - one button's rect, staged in hb_rect
; in:  AL = 1 or 2, SI = window ptr
; out: BX = hb_rect, filled; SI = the label's offset here (for this mode)
; clobbers: AX, BX, CX, DX, SI, flags
; -----------------------------------------------------------------------------
hbm_btnrect:
    push ax
    call hbm_origin             ; AX = left, DX = top
    mov bx, hb_rect
    add dx, HBW_BY
    mov [bx+2], dx
    add dx, HBW_BH - 1
    mov [bx+6], dx
    mov cx, ax
    pop ax
    cmp al, 1
    jne .two
    add cx, HBW_B1X
    mov [bx], cx
    add cx, HBW_B1W - 1
    mov [bx+4], cx
    mov si, hbm_s_bhib
    cmp byte [hb_mode], HB_M_RESUME
    jne .out
    mov si, hbm_s_bres
    jmp short .out
.two:
    add cx, HBW_B2X
    mov [bx], cx
    add cx, HBW_B2W - 1
    mov [bx+4], cx
    mov si, hbm_s_bcan
    cmp byte [hb_mode], HB_M_RESUME
    jne .out
    mov si, hbm_s_bdis
.out:
    ret

; -----------------------------------------------------------------------------
; hbm_btn - draw one button, upright or pressed as [hb_down] says
; in:  AL = 1 or 2, SI = window ptr; the gfx lock is held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, OS88UI_FILL         ; the interior is erased first, so a redraw
    cmp al, 1                   ; between the two states never leaves a mark
    jne .flags
    or di, OS88UI_DEF           ; Enter is the first button
.flags:
    cmp al, [hb_down]
    jne .up
    or di, OS88UI_DOWN
.up:
    call hbm_btnrect            ; BX = the rect, SI = the label here
    push di
    mov di, hb_lbl
    mov cx, 16
    call hbm_stage              ; ...staged where the button drawer reads it
    pop di
    mov si, hb_lbl
    call COLD_SEG:os88ui_btn_f
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_hit - which button is a point in?
; in:  CX = x, DX = y (screen), SI = window ptr
; out: AL = 1, 2 or 0
; clobbers: AL (the output), flags
; -----------------------------------------------------------------------------
hbm_hit:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, cx                  ; DI = x, BX = y, across hbm_btnrect
    mov bx, dx
    mov al, 1
    call hbm_hitone
    jc .yes
    mov al, 2
    call hbm_hitone
    jc .yes
    xor al, al
.yes:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; hbm_hitone - is the point DI,BX inside button AL? (out CF = 1 yes)
hbm_hitone:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bx                     ; y, across hbm_btnrect - which clobbers CX,
    call hbm_btnrect            ; BX = hb_rect. (It was `mov cx, bx` BEFORE
    pop cx                      ; the call, and no click ever landed)
    cmp di, [bx]
    jb .no
    cmp di, [bx+4]
    ja .no
    cmp cx, [bx+2]
    jb .no
    cmp cx, [bx+6]
    ja .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_paint - W_PAINT (SPEC.md 86.1)
; in:  SI = window ptr; content already white; the gfx lock is held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call KERNEL_SEG:HB_API_COLOR
    call hbm_origin             ; AX = left, DX = top
    mov cx, ax
    add cx, HBW_LX
    add dx, HBW_LY
    mov bx, hbm_lines_ask
    mov al, [hb_mode]
    cmp al, HB_M_BUSY
    jne .notbusy
    mov bx, hbm_lines_busy
.notbusy:
    push ax
    mov al, [hb_vol]            ; the letter, patched into both faces' lines
    add al, 'A'
    mov [cs:hbm_s_r1 + HBM_R1_LTR], al
    mov [cs:hbm_s_a2 + HBM_A2_LTR], al
    pop ax
    cmp al, HB_M_RESUME
    jne .lines
    mov bx, hbm_lines_res
.lines:
    push si
.line:
    mov si, [cs:bx]
    or si, si
    jz .lined
    call hbm_str
    add dx, HBW_LH
    add bx, 2
    jmp short .line
.lined:
    pop si
    cmp byte [hb_mode], HB_M_BUSY
    je .out                     ; no buttons while it is happening
    mov al, 1
    call hbm_btn
    mov al, 2
    call hbm_btn
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; hbm_repaint - erase the content and paint it again, for a mode change
; in:  SI = window ptr; the gfx lock is held
hbm_repaint:
    push ax
    push bx
    push cx
    push dx
    call hbm_origin             ; AX = left, DX = top
    mov bx, dx
    mov cx, ax
    add cx, HBW_CW - 1
    mov dx, bx
    add dx, HBW_CH - 1
    push ax
    mov al, CWHITE
    call KERNEL_SEG:HB_API_COLOR
    pop ax
    call KERNEL_SEG:HB_API_FILL ; AX=x1, BX=y1, CX=x2, DX=y2
    call hbm_paint
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; hbm_win - this kind's window, if it is up
; out: CF = 0 and SI = window ptr; CF = 1 none
hbm_win:
    push ax
    push di
    mov al, KIND_HIBER
    call KERNEL_SEG:cw_inst_find_kind   ; DI = the record
    jc .no
    mov si, [di+I_WIN]
    or si, si
    jz .no
    pop di
    pop ax
    clc
    ret
.no:
    pop di
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hbm_onclick - W_ONCLICK: arm the button the press landed on (SPEC.md 13.8)
; in:  CX = x, DX = y, SI = window ptr; the gfx lock is held
; -----------------------------------------------------------------------------
hbm_onclick:
    push ax
    cmp byte [hb_mode], HB_M_BUSY
    je .out
    call hbm_hit
    mov [hb_down], al
    or al, al
    jz .out
    call hbm_btn                ; ...drawn pressed
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_onup - W_ONMOUSEUP: fire if the release is on the armed button
; in:  CX = x, DX = y, SI = window ptr; the gfx lock is held
; -----------------------------------------------------------------------------
hbm_onup:
    push ax
    push bx
    mov bl, [hb_down]
    mov byte [hb_down], 0
    or bl, bl
    jz .out
    cmp byte [hb_mode], HB_M_BUSY
    je .out
    mov al, bl
    call hbm_btn                ; upright again, whatever happens next
    call hbm_hit
    cmp al, bl
    jne .out
    call hbm_act
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_onkey - W_ONKEY: Enter is the first button, Esc the second
; in:  AL = ascii, AH = scan, SI = window ptr; the gfx lock is held
; -----------------------------------------------------------------------------
hbm_onkey:
    push ax
    cmp byte [hb_mode], HB_M_BUSY
    je .out
    cmp al, 13
    jne .notcr
    mov al, 1
    jmp short .act
.notcr:
    cmp al, 27
    jne .out
    mov al, 2
.act:
    call hbm_act
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_act - one of the two buttons, in this mode (SPEC.md 86.1)
; in:  AL = 1 or 2, SI = window ptr; the gfx lock is held, UI task
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_act:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [hb_mode], HB_M_RESUME
    je .resume
    cmp al, 2
    je .close
    call hbm_arm                ; Hibernate: check the disk, then post
    jmp short .out
.resume:
    cmp al, 2
    je .discard
    ; --- Resume: say so, and post the restore for the top of the pass -------
    mov byte [hb_mode], HB_M_BUSY
    call hbm_repaint
    cmp byte [ui_rebootq], 0
    jne .out                    ; a restart already posted wins (SPEC.md 20.10)
    mov byte [ui_rebootq], UI_RBQ_RESUME
    jmp short .out
.discard:
    call COLD_SEG:drvf_drv_vol_bank
    call hbm_forget             ; the pointer and the image go
    call COLD_SEG:drvf_drv_vol_back
.close:
    mov byte [hb_mode], HB_M_GONE       ; BEFORE the close: app_close_win's
    mov bx, si                          ; KIND_HIBER hook drops the image on
    call KERNEL_SEG:cw_app_close_win    ; every OTHER route (SPEC.md 86.1), and
                                        ; GONE is how it knows this is not one.
                                        ; Synchronous: task-less kind; the
                                        ; thunk drops the image on the way back
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_forget - delete the pointer and the image (SPEC.md 86.6 step 5, and
;              86.4 step 3, before a new pair is written)
; in:  [hb_vol] = the volume; both files are in its root
; out: nothing (all registers preserved); failures are ignored - there is
;      nothing further to do about a file that is already gone. The volume is
;      left at the root of [hb_vol]: a caller standing elsewhere brackets
;      this with drv_vol_bank/back.
; -----------------------------------------------------------------------------
hbm_forget:
    push ax
    push dx
    push si
    mov dl, [hb_vol]
    xor ax, ax                  ; both live in the root (SPEC.md 86.3)
    call COLD_SEG:hbk_dsk_chdir
    jc .out
    mov si, hb_s_ptr            ; the pointer FIRST: with it gone there is
    call COLD_SEG:dwf_dskw_delete   ; nothing to resume whatever becomes of
    mov si, hb_s_img            ; the image (SPEC.md 86.3)
    call COLD_SEG:dwf_dskw_delete
.out:
    pop si
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_kinit - KD_INIT (SPEC.md 29.3): the release hook and the snap
; in:  BX = window ptr, DI = 0, SI = the record; no lock; not yet visible
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_kinit:
    push ax
    mov byte [hb_down], 0
    mov ax, hb_onup
    call KERNEL_SEG:cw_wm_onmouseup
    mov al, 1
    call KERNEL_SEG:cw_wm_snap  ; text on cell boundaries (SPEC.md 11.94)
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_open - the System menu's Hibernate... (SPEC.md 86.1): open the window
; in:  nothing; UI task, no lock
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_open:
    push ax
    push si
    call hbm_win                ; already up, in whatever mode: app_launch
    jnc .launch                 ; fronts it and the mode stays what it is
    call COLD_SEG:hb_pick_x     ; the disk (SPEC.md 86.2): the item was
    jc .out                     ; live, so there is one
    mov [hb_vol], al            ; ...and the window names it
    mov byte [hb_mode], HB_M_ASK
    mov byte [hb_down], 0
.launch:
    mov al, KIND_HIBER
    call KERNEL_SEG:cw_app_launch
    jnc .out
    call KERNEL_SEG:cw_snd_beep
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_geom - the transport facts for a volume (SPEC.md 86.5 step 1)
; in:  AL = the volume index
; out: CF = 0 and [hb_unit], [hb_spt], [hb_heads], [hb_base] filled;
;      CF = 1: not a fixed disk the ROM's int 13h reaches
; clobbers: flags
; -----------------------------------------------------------------------------
hbm_geom:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bl, al
    call COLD_SEG:dskf_dsk_vol_row  ; BX = the row
    jc .no
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .drv
    mov dl, [bx+DV_UNIT]        ; the boot partition (SPEC.md 52.10.3): the
    test dl, 0x80               ; kernel holds every fact itself
    jz .no
    xor dh, dh
    mov [hb_unit], dx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov dx, [dsk_vbase + bx]
    mov [hb_base], dx
    mov dx, [dsk_vbase + bx + 2]
    mov [hb_base + 2], dx
    mov dx, [dsk_bootspt]
    mov [hb_spt], dx
    mov dx, [dsk_boothds]
    mov [hb_heads], dx
    jmp short .check
.drv:
    cmp byte [bx+DV_KIND], DVK_DRV
    jne .no
    mov ah, [bx+DV_UNIT]        ; the driver's handle
    mov al, [bx+DV_CLASS]
    mov [drv_blkcls], al
    call COLD_SEG:drvf_drv_cls_svc  ; DI = the class's service table
    jc .no
    mov bp, [di+DSV_GEOM]
    call COLD_SEG:hbk_drv_blk_call  ; DL = unit, CX:BX = base, SI = spt,
    jc .no                      ; DI = heads; CF = 1 not int 13h
    xor dh, dh
    mov [hb_unit], dx
    mov [hb_base], bx
    mov [hb_base + 2], cx
    mov [hb_spt], si
    mov [hb_heads], di
.check:
    cmp word [hb_spt], 0
    je .no
    cmp word [hb_spt], 63
    ja .no
    cmp word [hb_heads], 0
    je .no
    cmp word [hb_heads], 255
    ja .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_arm - the Hibernate button (SPEC.md 86.2, 86.4): the disk in [hb_vol]
;           has to be one the ROM reaches and have the room, and then the
;           write is POSTED for the top of the pass
; in:  SI = the window; the gfx lock is held
; out: nothing (all registers preserved); a refusal is a toast
; -----------------------------------------------------------------------------
hbm_arm:
    push ax
    push bx
    push cx
    push dx
    push si
    call COLD_SEG:drvf_drv_vol_bank ; the user may be standing anywhere
    mov al, [hb_vol]
    call hbm_geom
    jc .nobios
    mov dl, [hb_vol]
    xor ax, ax
    call COLD_SEG:hbk_dsk_chdir ; the root: where the image goes, and where
    jc .failed                  ; dfree answers for
    call COLD_SEG:dwf_dskw_dfree    ; DX:AX = free bytes
    jc .failed
    push ax
    push dx
    push si
    mov si, hb_s_img            ; ...plus what replacing an old image frees
    call COLD_SEG:hbk_dsk_find_name
    jc .noold
    call COLD_SEG:hbk_dsk_get_dir   ; SI = the staged entry, its size at 20
    pop bx
    pop dx
    pop ax
    add ax, [si+20]
    adc dx, [si+22]
    push ax
    push dx
    push bx
.noold:
    pop si
    pop dx
    pop ax
    push ax
    push dx
    call hbm_imgsize            ; DX:AX = the image's bytes
    add ax, 0x1000              ; ...and the pointer, with room to spare
    adc dx, 0
    mov cx, ax
    mov bx, dx                  ; BX:CX = the need
    pop dx
    pop ax                      ; DX:AX = the room
    cmp dx, bx
    jb .noroom
    ja .room
    cmp ax, cx
    jb .noroom
.room:
    call COLD_SEG:drvf_drv_vol_back
    mov byte [hb_mode], HB_M_BUSY
    call hbm_repaint
    cmp byte [ui_rebootq], 0
    jne .out                    ; a restart already posted wins (SPEC.md 20.10)
    mov byte [ui_rebootq], UI_RBQ_HIBER
    jmp short .out
.nobios:
    mov si, hbm_s_nobios
    jmp short .refuse
.failed:
    mov si, hbm_s_failed        ; the disk could not be read, which is not
    jmp short .refuse           ; "no room" (LESSONS: the right refusal with
.noroom:                        ; the wrong reason sends the user deleting
    mov si, hbm_s_noroom        ; files that were never the problem)
.refuse:
    call hbm_toast
    call COLD_SEG:drvf_drv_vol_back
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_perform - a posted request, spent with nothing held (SPEC.md 86.4/86.5)
; in:  AL = UI_RBQ_HIBER / UI_RBQ_RESUME / UI_RBQ_ASK; UI task, no lock
; out: nothing; returns only from a refusal, a discard, or the question
;
; [hb_sp] is this routine's entry SP - the word under it is the return into
; modh_e_perform - and it is the frame hbm_wake comes back to after a
; resume, and the one every exit below returns through.
; -----------------------------------------------------------------------------
hbm_perform:
    mov [hb_sp], sp
    cld
    cmp al, UI_RBQ_HIBER
    je hbm_hib
    cmp al, UI_RBQ_RESUME
    je hbm_res
    jmp hbm_ask

; --- hbm_hib: write the image and stop (SPEC.md 86.4) ------------------------
hbm_hib:
    call COLD_SEG:drvf_drv_vol_bank ; where the user stands, for a refusal
    call hbm_detach             ; 1. the hardware drivers go, and are noted
    call KERNEL_SEG:cw_gfx_lock ; 2. held from here: the machine is one
    inc byte [sch_lock]         ;    instant from here on - and only NOW
    mov dl, [hb_vol]            ; 3. the root of the disk the boot picked:
    xor ax, ax                  ;    the detach yields, and a task that took
    call COLD_SEG:hbk_dsk_chdir ;    the turn could have moved the current
    jc .fail                    ;    directory under the write
    call hbm_forget             ;    ...and the OLD pointer and image go
                                ;    before a byte is written: a pointer that
                                ;    outlived its question would otherwise
                                ;    name the NEW image with the old session's
                                ;    wake address (SPEC.md 86.3)
    mov word [hb_wakep], hbm_wake   ; 4. where the restored kernel enters
    mov [hb_wakep + 2], cs
    mov si, hb_s_img            ; 5. the image: all of memory, one write
    xor bx, bx
    mov es, bx
    call hbm_imgsize            ; DX:AX = bytes
    mov cx, ax
    call COLD_SEG:dwf_dskw_write
    jc .fail
    call hbm_ptr_build          ; 6. the pointer, beside it
    mov si, hb_s_ptr
    push cs
    pop es
    mov bx, hbm_ptrbuf
    xor dx, dx
    mov cx, HBP_SIZE
    call COLD_SEG:dwf_dskw_write
    jc .fail
    call hbm_kbdrain            ; 7. a key typed during the write is not the
    call KERNEL_SEG:cw_vid_reboot   ;    answer; text mode, the sentence, a key
    mov si, hbm_s_off
    call hbm_tty
    xor ah, ah
    int 0x16
    call COLD_SEG:drv_shutdown_x    ; ...and the reboot path's tail (20.10)
    call KERNEL_SEG:cw_sched_unhook
    int 0x19
    jmp $
.fail:
    dec byte [sch_lock]
    call KERNEL_SEG:cw_gfx_unlock
    call hbm_reload             ; the drivers come back
    call COLD_SEG:drvf_drv_vol_back
    mov byte [hb_mode], HB_M_ASK
    mov si, hbm_s_failed
    call hbm_toast
    call hbm_winback
    mov sp, [hb_sp]
    ret

; hbm_winback - the window, if it is up, painted for the mode it is in now
; in:  nothing; no lock held
hbm_winback:
    push si
    call hbm_win
    jc .out
    call KERNEL_SEG:cw_gfx_lock
    call hbm_repaint
    call KERNEL_SEG:cw_gfx_unlock
.out:
    pop si
    ret

; hbm_kbdrain - empty the ROM's keyboard buffer (SPEC.md 86.4 step 7, 86.5)
; in:  nothing; out: nothing (all registers preserved)
hbm_kbdrain:
    push ax
.more:
    mov ah, 0x01
    int 0x16
    jz .out
    xor ah, ah
    int 0x16
    jmp short .more
.out:
    pop ax
    ret

; hbm_tty - print one of this image's strings through the ROM's teletype
; in:  SI = the string's offset here; text mode
hbm_tty:
    push ax
    push bx
    push si
.ch:
    mov al, [cs:si]
    or al, al
    jz .out
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    inc si
    jmp short .ch
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_detach / hbm_reload - the drivers that own hardware (SPEC.md 86.4 step 2)
; in:  nothing; UI task, NO lock held (an unload waits on its worker)
; out: nothing (all registers preserved); [hb_drvmask] says which rows
; -----------------------------------------------------------------------------
hbm_detach:
    push ax
    push bx
    push cx
    mov word [hb_drvmask], 0
    xor cx, cx
.row:
    mov al, cl
    call COLD_SEG:drvf_drv_row  ; BX = the row
    cmp word [bx+DRVR_SEG], 0
    je .next
    mov al, [bx+DRVR_CLASS]
    cmp al, DRVC_DISK
    je .next                    ; memory and the ROM's int 13h: it stays
    cmp al, DRVC_FILE
    je .next                    ; the RAM disk: memory, and it stays
    mov ax, 1
    shl ax, cl
    or [hb_drvmask], ax
    mov al, cl
    call COLD_SEG:drvf_drv_unload
.next:
    inc cx
    cmp cx, DRV_MAX
    jb .row
    pop cx
    pop bx
    pop ax
    ret

hbm_reload:
    push ax
    push cx
    push dx
    xor cx, cx
    mov dx, [hb_drvmask]
.row:
    shr dx, 1
    jnc .next
    mov al, cl
    call COLD_SEG:drvf_drv_load ; failures show on the Drivers page, as at boot
.next:
    inc cx
    cmp cx, DRV_MAX
    jb .row
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_ptr_build - compose the pointer file in this image's buffer (86.3)
; -----------------------------------------------------------------------------
hbm_ptr_build:
    push ax
    push cx
    push si
    push di
    push es
    push cs
    pop es
    cld
    mov di, hbm_ptrbuf
    xor ax, ax
    mov cx, HBP_SIZE / 2
    rep stosw
    mov di, hbm_ptrbuf
    mov word [es:di+HBP_MAGIC], 'HI'
    mov word [es:di+HBP_MAGIC+2], 'B1'
    mov word [es:di+HBP_BUILD], BUILD_NUM
    mov word [es:di+HBP_STAMP], MOD_STAMP
    mov ax, [mem_top]
    mov [es:di+HBP_MEMTOP], ax
    mov al, [vid_kind]
    mov [es:di+HBP_VID], al
    mov ax, [hb_sp]
    mov [es:di+HBP_SP], ax
    mov [es:di+HBP_SS], ss
    mov ax, [hb_wakep]
    mov [es:di+HBP_WAKE], ax
    mov ax, [hb_wakep + 2]
    mov [es:di+HBP_WAKE + 2], ax
    mov ax, [hb_drvmask]
    mov [es:di+HBP_DRVS], ax
    mov si, hb_s_img            ; HBP_PATH stays zero: the root (SPEC.md 86.3)
    mov di, hbm_ptrbuf + HBP_NAME
    mov cx, 13
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_ask - the boot-time question (SPEC.md 86.5): validate the pointer,
;           find the image, and open the window in HB_M_RESUME
; in:  [hb_vol] = the volume the probe found the pointer on; no lock
; -----------------------------------------------------------------------------
hbm_ask:
    call COLD_SEG:drvf_drv_vol_bank
    mov dl, [hb_vol]
    xor ax, ax
    call COLD_SEG:hbk_dsk_chdir
    jc .gone
    mov si, hb_s_ptr
    push cs
    pop es
    mov bx, hbm_ptrbuf
    xor dx, dx
    mov cx, HBP_SIZE
    call COLD_SEG:dwf_dskw_read ; DX:AX = bytes
    jc .gone
    cmp ax, HBP_SIZE
    jne .stale
    mov bx, hbm_ptrbuf
    cmp word [cs:bx+HBP_MAGIC], 'HI'
    jne .stale
    cmp word [cs:bx+HBP_MAGIC+2], 'B1'
    jne .stale
    cmp word [cs:bx+HBP_BUILD], BUILD_NUM
    jne .stale
    cmp word [cs:bx+HBP_STAMP], MOD_STAMP
    jne .stale
    mov ax, [mem_top]
    cmp [cs:bx+HBP_MEMTOP], ax
    jne .stale
    mov al, [vid_kind]
    cmp [cs:bx+HBP_VID], al
    jne .stale
    cmp byte [cs:bx+HBP_PATH], 0    ; a pointer with a path is not one this
    jne .stale                  ; build wrote: the image lives in the root
    mov si, hb_s_img            ; ...which is mounted: the pointer came from it
    call COLD_SEG:hbk_dsk_find_name
    jc .lost
    call COLD_SEG:hbk_dsk_get_dir   ; SI = the staged entry
    call hbm_imgsize            ; DX:AX = what the image must be
    cmp [si+20], ax
    jne .lost
    cmp [si+22], dx
    jne .lost
    mov al, [hb_vol]
    call hbm_geom
    jc .nobios
    call COLD_SEG:drvf_drv_vol_back
    mov byte [hb_mode], HB_M_RESUME
    mov byte [hb_down], 0
    call hbm_kbdrain            ; a key held at the hibernate's own prompt
                                ; repeats through int 19h into this boot, and
                                ; ui_task's next step hands it to the front
                                ; window - where Esc is Discard
    mov al, KIND_HIBER
    call KERNEL_SEG:cw_app_launch
    jmp short .out
.stale:
    mov si, hbm_s_stale
    jmp short .drop
.lost:
    mov si, hbm_s_lost
    jmp short .drop
.nobios:
    mov si, hbm_s_nobios
.drop:
    call hbm_toast
    call hbm_forget             ; a pointer nothing can act on
.gone:
    call COLD_SEG:drvf_drv_vol_back
    mov byte [hb_mode], HB_M_GONE   ; ...and the image goes back too
.out:
    mov sp, [hb_sp]
    ret

; hbm_imgsize - out: DX:AX = [mem_top] * 16, the bytes an image must have
hbm_imgsize:
    push cx
    mov ax, [mem_top]
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; hbm_res - put the image back (SPEC.md 86.5): the extents, then the stub
; in:  the question was answered Resume; no lock held
; -----------------------------------------------------------------------------
hbm_res:
    call COLD_SEG:drvf_drv_vol_bank ; where the user stands, for a refusal
    mov dl, [hb_vol]
    xor ax, ax
    call COLD_SEG:hbk_dsk_chdir
    jc .fail
    mov si, hb_s_img
    call COLD_SEG:hbk_dsk_find_name
    jc .fail
    call COLD_SEG:hbk_dsk_get_dir   ; SI = the entry: [si+18] its first cluster
    mov ax, [si+18]
    push ax
    mov ax, HB_XKB              ; 2. the extent list's claim
    mov bx, MEM_K_HIB
    call COLD_SEG:hbk_mem_claim ; DX = its segment
    pop ax
    jc .fail
    mov [hb_xseg], dx
    call hbm_extents            ; AX = first cluster in; [hb_nx] out
    jc .failx
    ; 3. the panel's settings, the fresh boot's drivers go, the lock is taken,
    ;    text mode - ui_cmd_reboot's order (SPEC.md 20.10, 31.8) -----------
    call COLD_SEG:cpf_cp_flush_close
    call COLD_SEG:drv_shutdown_x
    call KERNEL_SEG:cw_gfx_lock
    call KERNEL_SEG:cw_vid_reboot
    ; 4. the staging area ---------------------------------------------------
    call hbm_stageseg           ; ES = the text framebuffer
    push ds
    push cs
    pop ds
    mov si, hbs_stub            ; the stub, to offset 0
    mov di, HS_CODE
    mov cx, hbs_stub_end - hbs_stub
    rep movsb
    pop ds
    mov ax, [hb_unit]
    mov [es:HS_UNIT], ax
    mov ax, [hb_spt]
    mov [es:HS_SPT], ax
    mov ax, [hb_heads]
    mov [es:HS_HEADS], ax
    mov ax, [hb_nx]
    mov [es:HS_NX], ax
    mov bx, hbm_ptrbuf
    mov ax, [cs:bx+HBP_SS]
    mov [es:HS_SS], ax
    mov ax, [cs:bx+HBP_SP]
    mov [es:HS_SP], ax
    mov ax, [cs:bx+HBP_WAKE]
    mov [es:HS_WAKE], ax
    mov ax, [cs:bx+HBP_WAKE+2]
    mov [es:HS_WAKE+2], ax
    mov si, clk_sec             ; the fresh boot's clock (SPEC.md 86.6): ten
    mov di, HS_CLK              ; bytes, clk_sec..clk_year, and NOT the two
    mov cx, 5                   ; display toggles after them, which are the
    rep movsw                   ; image's to keep
    push ds
    mov ds, [hb_xseg]           ; the extents
    xor si, si
    mov di, HS_EXT
    mov cx, [es:HS_NX]
    shl cx, 1
    shl cx, 1                   ; words: 4 per extent
    rep movsw
    pop ds
    ; 5. the ROM's int 08h back, the kernel's IRQs masked, and go ----------
    cli
    push es
    xor ax, ax
    mov es, ax
    mov ax, [sch_old08]
    mov [es:0x20], ax
    mov ax, [sch_old08 + 2]
    mov [es:0x22], ax
    pop es
    in al, 0x21
    mov [es:HS_MASKM], al
    or al, 0x1A                 ; IRQ 1, 3, 4: the keyboard and the mice
    out 0x21, al
    mov byte [es:HS_SLAVE], 0
    cmp byte [cpu_tier], CPU_8086   ; an XT has no second 8259, and its port
    je .noslave                 ; 0xA0 is the NMI mask register with 0xA1 an
    mov byte [es:HS_SLAVE], 1   ; alias of it - mou_p2_init's refusal (9.9.2)
    in al, 0xA1
    mov [es:HS_MASKS], al
    or al, 0x10                 ; IRQ 12: the auxiliary port
    out 0xA1, al
.noslave:
    push es
    mov ax, HS_CODE
    push ax
    retf                        ; into the stub, IF = 0
.failx:
    mov dx, [hb_xseg]
    mov bx, MEM_K_HIB
    call COLD_SEG:hbk_mem_free
    mov word [hb_xseg], 0
.fail:
    call COLD_SEG:drvf_drv_vol_back
    mov byte [hb_mode], HB_M_RESUME
    mov si, hbm_s_lost
    call hbm_toast
    call hbm_winback
    mov sp, [hb_sp]
    ret

; hbm_stageseg - out: ES = the text framebuffer the stub will run in
hbm_stageseg:
    push ax
    mov ax, 0xB800
    cmp byte [vid_kind], VID_HERC
    jne .set
    mov ax, 0xB000
.set:
    mov es, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_extents - the image's chain as absolute sector runs (SPEC.md 86.5 step 2)
; in:  AX = the first cluster; [hb_xseg] = where to build; the volume mounted
; out: CF = 0 and [hb_nx] runs of {dd lba, dw count, dw 0} at [hb_xseg]:0;
;      CF = 1 the chain is broken or too fragmented
; -----------------------------------------------------------------------------
hbm_extents:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [hb_xseg]
    xor di, di                  ; the next entry
    mov word [hb_nx], 0
    mov si, ax                  ; SI = the cluster in hand
    mov cx, [mem_top]
    mov bl, 5
    xchg cx, bx
    shr bx, cl                  ; BX = sectors the image has: paragraphs / 32
    mov cx, bx                  ; CX = sectors still to place
.clus:
    mov ax, si
    call COLD_SEG:hbk_dsk_clus2lba  ; AX = its first LBA, volume-relative
    jc .bad
    xor dx, dx
    add ax, [hb_base]
    adc dx, [hb_base + 2]       ; DX:AX = absolute
    mov bl, [dsk_spc]
    xor bh, bh                  ; BX = this cluster's sectors...
    cmp bx, cx
    jbe .whole
    mov bx, cx                  ; ...capped at what the image still needs
.whole:
    cmp word [hb_nx], 0
    je .new
    cmp ax, [cs:hbm_nextlo]     ; does this cluster follow the run in hand?
    jne .new
    cmp dx, [cs:hbm_nexthi]
    jne .new
    add [es:di-4], bx           ; contiguous: the run grows
    jmp short .grown
.new:
    cmp word [hb_nx], HS_XMAX
    jae .bad
    mov [es:di], ax
    mov [es:di+2], dx
    mov [es:di+4], bx
    mov word [es:di+6], 0
    add di, 8
    inc word [hb_nx]
    mov [cs:hbm_nextlo], ax
    mov [cs:hbm_nexthi], dx
.grown:
    add [cs:hbm_nextlo], bx
    adc word [cs:hbm_nexthi], 0
    sub cx, bx
    jz .done
    mov ax, si
    call COLD_SEG:hbk_dsk_next_clus ; AX = the next
    jc .bad
    mov si, ax
    jmp short .clus
.done:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hbm_nextlo: dw 0                ; the LBA the run in hand ends at
hbm_nexthi: dw 0

; -----------------------------------------------------------------------------
; hbm_wake - the restored kernel wakes up here (SPEC.md 86.6)
; in:  CS = this image's segment IN THE RESTORED MEMORY, DS = KERNEL_SEG,
;      SS:SP = hbm_perform's entry frame, IF = 0, the gfx lock held and
;      [sch_lock] up - all of it as the write left it
; out: does not return: it RETURNS THROUGH THE FRAME, into modh_e_perform
; -----------------------------------------------------------------------------
hbm_wake:
    sti                         ; 1. the IVT is the image's; nobody switches
    cld
    call hbm_stageseg           ; 2. the clock the boot just read
    mov si, HS_CLK
    mov di, clk_sec
    mov cx, 10
.clk:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .clk
    mov ax, [ticks]
    mov [clk_last], ax
    mov word [clk_acc], 0
    mov byte [clk_barq], 1
    ; 3. the disk caches are the write's, not the disk's (SPEC.md 86.4) -----
    xor al, al
.drop:
    push ax
    call COLD_SEG:hbk_dsk_fatw_drop
    pop ax
    inc al
    cmp al, DVOL_MAX
    jb .drop
    mov word [dsk_fatd0], 0xFFFF
    mov word [dsk_fatw0], 0xFFFF
    mov byte [dsk_mntok], 0
    mov word [disk_nfiles], 0
    mov byte [dsk_batch], 0
    mov byte [dskw_batch], 0
    mov byte [dskw_syswr], 0
    mov byte [dskw_isdir], 0
    call COLD_SEG:hbk_dsk_rah_flush
    mov word [mem_pinseg], 0    ; dsk_xfer's pin, mid-transfer in the image
    call KERNEL_SEG:fpg_end     ; ...and the progress widget it had up
    ; 4. the desktop, the way fsx_restore gets it back (SPEC.md 53) - before
    ;    any disk work, so the progress widget has a graphics mode to draw in
    call KERNEL_SEG:cw_vid_setmode
    mov byte [hb_mode], HB_M_GONE   ; before the close: app_close_win's hook
    call hbm_win                ; leaves a GONE window's image to the thunk
    jc .nowin                   ; (SPEC.md 86.1), and this code IS the image
    mov bx, si
    call KERNEL_SEG:cw_app_close_win
.nowin:
    call KERNEL_SEG:cw_menu_force
    call KERNEL_SEG:cw_dock_force
    call KERNEL_SEG:cw_wm_paint_all
    ; 5. the two files are spent, and the drivers come back -----------------
    call hbm_forget
    call hbm_reload
    ; 6. and the machine is the user's again --------------------------------
    call KERNEL_SEG:cw_blk_wake
    inc word [hb_resumes]
    call KERNEL_SEG:cw_gfx_unlock
    mov byte [sch_lock], 0      ; NOT a dec: the image holds the count as the
                                ; write left it, and dsk_xfer raises it around
                                ; every int 13h - so it reads 2, and one down
                                ; from that is a machine that never switches
    mov sp, [hb_sp]
    ret

; =============================================================================
; THE STUB (SPEC.md 86.5). Copied to HS_CODE in the text framebuffer and
; entered by a far jump with IF = 0. Position-independent: every jump is
; relative and every datum is at a numeric HS_* offset in its own segment.
; =============================================================================
hbs_stub:
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, HS_STACK
    cld
    mov word [HS_K], 0
    mov si, HS_EXT
    mov cx, [HS_NX]
.extent:
    or cx, cx
    jnz .have
    jmp .done
.have:
    push cx
    mov ax, [si]
    mov [HS_LBA], ax
    mov ax, [si+2]
    mov [HS_LBA+2], ax
    mov ax, [si+4]
    mov [HS_LEFT], ax
.run:
    cmp word [HS_LEFT], 0
    je .nextext
    ; --- CHS of [HS_LBA] (hd_chs's one div pair), and the track's remainder --
    mov ax, [HS_LBA+2]
    xor dx, dx
    div word [HS_SPT]
    mov bx, ax                  ; BX:AX = track after the second div...
    mov ax, [HS_LBA]
    div word [HS_SPT]           ; ...and DX = sector - 1
    mov cx, [HS_SPT]
    sub cx, dx                  ; CX = sectors from this one to the track end
    mov [HS_N], cx
    inc dx
    push dx                     ; the sector, 1-based
    mov dx, bx
    div word [HS_HEADS]         ; AX = cylinder, DX = head
    pop bx                      ; BL = the sector
    mov dh, dl                  ; DH = head
    mov dl, [HS_UNIT]           ; DL = drive
    mov ch, al                  ; CH = cylinder low 8, CL = sector | high 2
    mov cl, ah
    ror cl, 1
    ror cl, 1
    and cl, 0xC0
    or cl, bl
    mov [HS_CHS], cx
    mov [HS_CHS+2], dx
    ; --- the run: to the track's end, the extent's end, the DMA page ---------
    mov cx, [HS_N]
    cmp cx, [HS_LEFT]
    jbe .cap1
    mov cx, [HS_LEFT]
.cap1:
    mov bx, [HS_K]
    cmp bx, 3
    jae .high
    mov cx, 1                   ; file sectors 0..2 go to 0060:0000, singly
    mov bx, 0x0060
    jmp short .go
.high:
    push cx
    mov cl, 5
    shl bx, cl                  ; ES = k * 32: linear k * 512
    mov ax, bx
    and ax, 0x0FFF
    neg ax
    add ax, 0x1000              ; paragraphs to the 64KB page's end...
    shr ax, cl                  ; ...in sectors
    pop cx
    cmp cx, ax
    jbe .go
    mov cx, ax
.go:
    mov [HS_N], cx
    mov es, bx
    xor bx, bx
    mov di, 3                   ; tries
.try:
    mov ax, [HS_N]
    mov ah, 0x02
    mov cx, [HS_CHS]
    mov dx, [HS_CHS+2]
    sti                         ; the ROM wants its interrupts on an AT
    int 0x13
    jnc .ok
    xor ah, ah
    mov dl, [HS_UNIT]
    int 0x13                    ; reset, and again
    dec di
    jnz .try
    jmp short .fail
.ok:
    mov bx, [HS_K]
    cmp bx, 2
    jae .advance
    ; --- file sector 0 or 1: the IVT, into the staging area for later -------
    push ds
    push si
    mov cl, 9
    shl bx, cl
    add bx, HS_IVT
    mov di, bx
    mov ax, cs
    mov es, ax
    mov ax, 0x0060
    mov ds, ax
    xor si, si
    mov cx, 256
    rep movsw
    pop si
    pop ds
.advance:
    mov ax, [HS_N]
    add [HS_K], ax
    add [HS_LBA], ax
    adc word [HS_LBA+2], 0
    sub [HS_LEFT], ax
    jmp .run
.nextext:
    add si, 8
    pop cx
    dec cx
    jmp .extent
.done:
    cli                         ; --- the IVT goes back last, under cli ------
    xor ax, ax
    mov es, ax
    xor di, di
    mov si, HS_IVT
    mov cx, 512
    rep movsw
    mov al, [HS_MASKM]
    out 0x21, al
    cmp byte [HS_SLAVE], 0
    je .noslave
    mov al, [HS_MASKS]
    out 0xA1, al
.noslave:
    mov ax, KERNEL_SEG
    mov ds, ax
    mov ss, [cs:HS_SS]
    mov sp, [cs:HS_SP]
    jmp far [cs:HS_WAKE]
.fail:
    mov ah, 0x02                ; the cursor to row 8 first: rows 0-2 of this
    xor bh, bh                  ; page ARE the stub, and the ROM's teletype
    mov dx, 0x0800              ; writes at the cursor the mode set homed. Row
    int 0x10                    ; 8 is the IVT copy, dead on this path, and
    mov si, hbs_s_fail - hbs_stub   ; three lines from there never scroll
.ch:
    mov al, [si]
    or al, al
    jz .key
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    inc si
    jmp short .ch
.key:
    xor ah, ah
    int 0x16
    int 0x19
    jmp $
hbs_s_fail: db 13, 10, 'Resume failed: the disk could not be read.', 13, 10
            db 'Press any key to restart.', 13, 10, 0
hbs_stub_end:
%if hbs_stub_end - hbs_stub > HS_CODE_MAX
  %error "hiber: the resume stub is bigger than its staging slot"
%endif

; =============================================================================
; The image's own data (SPEC.md 2.8.6): strings read through CS, and the
; pointer's buffer, which dskw_write and dskw_read reach as ES:BX.
; =============================================================================
hbm_ptrbuf:     times HBP_SIZE db 0

hbm_lines_ask:  dw hbm_s_a1, hbm_s_a2, hbm_s_a3, 0
hbm_lines_busy: dw hbm_s_b1, 0
hbm_lines_res:  dw hbm_s_r1, hbm_s_r2, hbm_s_r3, 0

hbm_s_a1:   db 'Save everything in memory to', 0
hbm_s_a2:   db 'C:\HIBERNAT.IMG and stop the machine?'
HBM_A2_LTR  equ 0
            db 0
hbm_s_a3:   db 'Restarting offers to resume it.', 0
hbm_s_b1:   db 'Hibernating...', 0
hbm_s_r1:   db 'A hibernation file is on drive '
HBM_R1_LTR  equ $ - hbm_s_r1
            db 'C:.', 0        ; 34 cells: the content is 39 wide
hbm_s_r2:   db 'Resume where you left off, or', 0
hbm_s_r3:   db 'discard it and start normally?', 0

hbm_s_bhib: db 'Hibernate', 0
hbm_s_bcan: db 'Cancel', 0
hbm_s_bres: db 'Resume', 0
hbm_s_bdis: db 'Discard', 0

hbm_s_nobios: db 'Hibernate needs a BIOS-known hard disk', 0
hbm_s_noroom: db 'Not enough room for the hibernation file', 0
hbm_s_failed: db 'Hibernate failed', 0
hbm_s_stale:  db 'Hibernation file is from another build', 0
hbm_s_lost:   db 'Hibernation file could not be read', 0

hbm_s_off:  db 13, 10, 'os8088 has hibernated.', 13, 10, 13, 10
            db 'It is now safe to switch the machine off.', 13, 10
            db 'Press any key to restart it.', 13, 10, 0

%else                           ; --- kern_small: a stub module, so every
                                ; kernel cuts the same four files ----------
section .modh
modh_hdr:
    dw 0x384F
    db MOD_VER
    db MOD_HIBER
    dw BUILD_NUM
    dw MOD_STAMP
    dw modh_end
    dw 1
    dw modh_e_none
    times MOD_NENT - 1 dw 0
modh_e_none:    stc
                retf
%endif

section .text                   ; every module ends where the next include
                                ; expects to begin (SPEC.md 1)
