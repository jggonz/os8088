; =============================================================================
; os8088 - VESA.DRV
;
; The packed-pixel display's renderer (SPEC.md 120): a VBE card's 800x600 or
; 1024x768 at 8 or 16 bits a pixel, drawn into the card's 64KB window A at
; A000 and banked through VBE function 4F05h. The kernel's planar drawing
; bodies each far-call this image through one compare (kernel/vidext.inc's
; VIDEXT), so everything that knows what a VBE is lives here and the XT this
; project is calibrated against never reads a sector of it.
;
; **IT IS AN OVERLAY WITH A drv_tab ROW, VMMOUSE.DRV's shape (SPEC.md 9.11.1)**:
; no class, because the kernel is its only caller and holds the far pointer;
; a row, because whether the desktop goes onto the VBE is the user's decision
; and the Drivers-page tick is both the request and the record of it.
;
; --- WHAT IT HOOKS: NOTHING -------------------------------------------------
; No vector, no IRQ, no port it keeps. Attach PROBES and CHOOSES and sets no
; mode (that is vid_switch's, SPEC.md 120.4); detach disarms the kernel's far
; pointer and the VID_A_VESA bit through the block it was handed, and nothing
; else, so a refusal has nothing to undo.
;
; --- 8086 CODE THROUGHOUT -----------------------------------------------------
; A 286 with a VBE 1.2 card qualifies. The 32-bit byte offset of a pixel is
; one `mul` (DX:AX), the window number is DX shifted by the granularity, and a
; row never straddles more than one window edge (SPEC.md 120.8). No 386
; instruction, no unreal mode, no linear framebuffer.
;
; --- THE DESKTOP STAYS 16 COLOURS (SPEC.md 120.1) -------------------------------
; Every colour that reaches this image is a 4-bit index. At 8bpp the pixel
; byte IS the index and the DAC's first sixteen entries are the VGA's own
; colours (vx_dac16); at 16bpp the index goes through vx_pal16, an RGB word a
; colour, and a read-back goes through the same table the other way.
;
; --- REGISTER DISCIPLINE ---------------------------------------------------------
; vx_prim saves every register and restores every register; a body that owes
; an OUTPUT (gfx_save's DI, gfx_restore's SI) writes it into the frame. DS = CS
; for every body, ES = whatever the caller passed until a body points it at
; the window, SS = the caller's. CF out of a body is CF out of the primitive.
; =============================================================================

%include "os88drv.inc"
%include "vxabi.inc"

    OS88_OVERLAY 'VESA Display', VX_ABI_VER, vx_entry

; --- the VBE (VESA BIOS Extensions 1.2+) ----------------------------------------
VBE_INFO    equ 0x4F00          ; AX: controller information, ES:DI = 512
VBE_MINFO   equ 0x4F01          ; AX: mode information, CX = mode, ES:DI = 256
VBE_SET     equ 0x4F02          ; AX: set mode, BX = mode
VBE_WIN     equ 0x4F05          ; AX: window control, BH = 0 set, BL = window
VBE_OK      equ 0x004F          ; the AX every one of them answers with

; the controller information block
VI_SIG      equ 0               ; 'VESA'
VI_VER      equ 4               ; dw: 0102h and up is what this image takes
VI_MODES    equ 14              ; dd: far pointer to the mode list (0FFFFh ends)

; the mode information block
MI_ATTR     equ 0               ; dw: bit 0 supported, 3 colour, 4 graphics
MI_WINA     equ 2               ; db: window A: bit 0 exists, 2 writable
MI_GRAN     equ 4               ; dw: window granularity, KB
MI_WSIZE    equ 6               ; dw: window size, KB
MI_SEGA     equ 8               ; dw: window A's segment
MI_BPSL     equ 16              ; dw: bytes per scan line
MI_XRES     equ 18              ; dw
MI_YRES     equ 20              ; dw
MI_BPP      equ 25              ; db: bits per pixel
MI_MODEL    equ 27              ; db: 4 packed pixel, 6 direct colour
MI_RSIZE    equ 31              ; db: red mask size, then position; green, blue
MI_RPOS     equ 32
MI_GSIZE    equ 33
MI_GPOS     equ 34
MI_BSIZE    equ 35
MI_BPOS     equ 36

; --- the mode table (SPEC.md 120.2.2): one 16-byte record an offered mode -------
; IN THE MODE INFORMATION BLOCK'S OWN ORDER where it has one (driver size
; pass 1): BytesPerScanLine, X and Y are three words there and here, and the
; three fields' size-then-position six bytes, so vx_offer copies two runs
; with `movsw` where it moved a field at a time, and vx_mkpal reads a
; field's size and position as one word
VM_MODE     equ 0               ; dw: the VBE mode number
VM_BPSL     equ 2               ; dw: bytes per scan line   } MI_BPSL's
VM_X        equ 4               ; dw                        } three words,
VM_Y        equ 6               ; dw                        } in order
VM_BPP      equ 8               ; db: 8 or 16 (15 is stored as 15, drawn as 16)
VM_WSH      equ 9               ; db: window shift: a 64KB page number << this
                                ;     is the window number 4F05h wants
VM_RSIZE    equ 10              ; db: the red field's size and position...
VM_RPOS     equ 11              ;     } MI_RSIZE's six bytes, in order
VM_GSIZE    equ 12              ; ...green...
VM_GPOS     equ 13
VM_BSIZE    equ 14              ; ...and blue, for the 16bpp packing
VM_BPOS     equ 15
VM_SIZE     equ 16
%if VM_X - VM_BPSL != MI_XRES - MI_BPSL || VM_Y - VM_BPSL != MI_YRES - MI_BPSL
  %error "vx_offer copies MI_BPSL's three words as one run"
%endif
%if VM_BPOS - VM_RSIZE != MI_BPOS - MI_RSIZE || VM_RPOS != VM_RSIZE + 1
  %error "vx_offer copies MI_RSIZE's six bytes as one run, vx_mkpal reads size and position as a word"
%endif
VX_NMODES   equ 16              ; the most this image offers
VX_LISTMAX  equ 64              ; the most mode numbers it walks

VX_XMAX     equ 1024            ; the geometry it accepts (SPEC.md 120.2.2)
VX_YMAX     equ 768
VX_XMIN     equ 640
VX_YMIN     equ 400
VX_BPSLMAX  equ 4096

VX_WSEG     equ 0xA000          ; window A, and the only segment it will take
VX_KCODE    equ 0x0060          ; the kernel's code segment - KERNEL_SEG, and on
                                ; kern_mega the same value as a CODE selector -
                                ; where the detach points the hook (vx_null)
VX_DISPI_IX equ 0x01CE          ; the Bochs/QEMU VBE's index port...
VX_DISPI_ID equ 0               ; ...its ID register (B0C0h-B0CFh)...
VX_DISPI_BANK equ 5             ; ...and its bank, in 64KB units

CUR_GW      equ 8               ; the arrow's cell (kernel/mouse.inc's)
CUR_GH      equ 12
CUR_TABSZ   equ 12

; =============================================================================
; vx_entry - the dispatcher's landing site
; in:  AL = a DRVV_* or VXV_*; DS = CS = ours, ES = KERNEL_SEG
; out: per the verb
; =============================================================================
vx_entry:
    cmp al, DRVV_DETACH
    je vx_detach
    cmp al, VXV_INIT
    je vx_init
    cmp al, VXV_PICK
    je vx_pick
    cmp al, VXV_NEXT
    je vx_next_x
    cmp al, VXV_PM
    je vx_pm
    cmp ax, (VX_ABI_VER << 8) | DRVV_ATTACH
    jne .no                     ; ATTACH from a kernel of this vintage (AH),
    cmp byte [vx_nmodes], 0     ; ONCE PER IMAGE: a successful attach leaves
    jne .no                     ; a mode in the table and its own code zeroed
    jmp vx_attach               ; (vx_once), and a refused one is freed
.no:
    stc                         ; everything else - a newer kernel's verb, or
    ret                         ; an ATTACH from another vintage - refused

; -----------------------------------------------------------------------------
; vx_detach - DRVV_DETACH: disarm the kernel's pointer and the availability bit.
;             Cannot fail (SPEC.md 51.2). The caller has already put the
;             desktop back on the VGA (SPEC.md 120.4); if it has not (the
;             shutdown's drv_shutdown_x), the hook reaches the kernel's
;             vx_null, which draws nothing, rather than a freed image.
; -----------------------------------------------------------------------------
vx_detach:
    cmp word [vx_kseg], 0
    je .out                     ; never initialised: nothing was armed
    push es
    push bx
    push ax
    mov es, [vx_kseg]
    mov bx, [vx_p_ep]
    mov ax, [vx_p_null]         ; the far pointer, to the kernel's vx_null:
    mov [es:bx], ax             ; a hook that fires after this draws nothing
    mov word [es:bx+2], VX_KCODE ; and goes back the way this image would have
    mov bx, [vx_p_avail]
    and byte [es:bx], ~VXA_BIT
    pop ax
    pop bx
    pop es
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; vx_attok - vx_attach's tail once a mode is offered, RESIDENT: it zeroes the
; region attach ran from (vx_once) and lays the int 10h stack over it
; -----------------------------------------------------------------------------
vx_attok:
    mov word [vx_stktop], vx_stkend ; the probe's code and its VBE buffer are
    mov di, vx_once             ; dead now: every later BIOS call (4F02h,
    mov cx, VX_STK / 2          ; 4F05h) runs on them instead of on the
    xor ax, ax                  ; scroll's bounce, zeroed so a watermark can
    cld                         ; be read off them (tests/megavesa.py). ES is
    rep stosw                   ; still ours: it has been since the top
    call vx_select              ; [vx_cur] = the record
    call vx_publish             ; ...and vx_tab, the geometry, the palette
    mov si, vx_tab
    pop bx
    pop es
    clc
    ret

; -----------------------------------------------------------------------------
; vx_select - the record for the class in [vx_want], else Auto's
; out: [vx_cur] = the record
; clobbers: ax, bx, cx, dx, si, di
; -----------------------------------------------------------------------------
vx_select:
    mov cl, [vx_want]
    call vx_find                ; SI = the record for the class, CF = 1 none
    jc vx_choose
    mov [vx_cur], si
    ret

; vx_decode - CL = a class byte -> DL = X/8, DH = Y/8, CL = bpp (8 or 16);
;             CF = 1 for Auto (0), with the three 0
; clobbers: ax, bx, flags
vx_decode:
    test cl, VXC_SET
    jz .auto
    mov bx, vxc_x8tab
    mov al, cl
    and al, VXC_XMASK
    xlat
    mov dl, al
    mov al, cl
    and al, VXC_YMASK
    shr al, 1
    shr al, 1
    add al, vxc_y8tab - vxc_x8tab
    xlat
    mov dh, al
    and cl, VXC_D16             ; 0 or 16...
    shr cl, 1
    add cl, 8                   ; ...8 or 16, and CF = 0
    ret
.auto:
    xor dx, dx
    xor cl, cl
    stc
    ret

; vx_encode - SI = a record -> CL = its class byte; CF = 1 for a geometry the
;             byte cannot name (not offered: vx_offer refuses it first)
; clobbers: ax, bx, flags
vx_encode:
    mov bx, vxc_x8tab
    mov ax, [si+VM_X]
    call .idx
    jc .no
    mov cl, al
    mov bx, vxc_y8tab
    mov ax, [si+VM_Y]
    call .idx
    jc .no
    shl al, 1
    shl al, 1
    or al, VXC_SET
    or cl, al
    cmp byte [si+VM_BPP], 8
    je .no                      ; CF = 0 off the equal compare...
    or cl, VXC_D16              ; ...and off the `or`
.no:
    ret
.idx:                           ; AX = X or Y against the four X/8 or Y/8 at
    shr ax, 1                   ; BX -> AL = its index, CF = 1 none. X/8 and
    shr ax, 1                   ; Y/8 are under 256 (X <= 1024, Y <= 768),
    shr ax, 1                   ; so AH is 0 here and counts the index
.i:
    cmp al, [bx]
    je .hit                     ; (CF = 0 off the equal compare)
    inc bx
    inc ah
    cmp ah, 4
    jb .i
    stc
    ret
.hit:
    mov al, ah
    ret

    VXC_TABLES                  ; vxc_x8tab / vxc_y8tab, vxabi.inc's
%if vxc_y8tab - vxc_x8tab != 4
  %error "vx_decode reads the Y table as four bytes past the X table's start"
%endif

; vx_find - the offered record whose class byte is CL: out SI, or CF = 1
;           (Auto included: it names no record). A 15bpp record answers for
;           the 16 bit - which vx_encode's D16 already says, so a record
;           matches when its encoding is CL's set bits (bits 5 and 6 are no
;           field and vx_decode never read them). A record the byte cannot
;           name matches no class
; clobbers: ax, bx, cx, dx, si, di, flags
vx_find:
    test cl, VXC_SET
    jz .none
    mov dl, cl
    and dl, VXC_SET | VXC_D16 | VXC_YMASK | VXC_XMASK
    mov si, vx_modes
    mov di, [vx_nmodes_w]       ; (the count as a word: the byte and a 0)
.n:
    or di, di
    jz .none
    call vx_encode              ; CL = the record's class
    jc .next
    cmp cl, dl
    je .out                     ; (CF = 0 off the equal compare)
.next:
    add si, VM_SIZE
    dec di
    jmp short .n
.none:
    stc
.out:
    ret

; vx_publish - [vx_cur] into vx_tab and the geometry this image draws by
; clobbers: ax, bx, cx, dx, si, di
vx_publish:
    mov si, [vx_cur]
    cld
    lodsw                       ; VM_MODE
    mov [vx_tab+VXS_MODE], ax
    lodsw                       ; VM_BPSL
    mov [vx_bpsl], ax
    lodsw                       ; VM_X
    mov [vx_tab+VXS_X], ax
    mov [vx_x], ax
    lodsw                       ; VM_Y
    mov [vx_tab+VXS_Y], ax
    mov [vx_y], ax
    lodsw                       ; VM_BPP, VM_WSH: the mode's own depth (8,
    mov [vx_depth], ax          ; 15 or 16) and the window shift, one word
    cmp al, 8
    je .d8
    mov al, 16                  ; ...and what the kernel is told: 15 draws as 16
.d8:
    mov [vx_tab+VXS_BPP], al
    mov [vx_bpp], al
    jmp vx_mkpal                ; the 16-entry RGB table for a direct mode

; -----------------------------------------------------------------------------
; vx_choose - Auto: the record with the largest X*Y, and at that size the
;             deepest; ties keep the earlier record (SPEC.md 120.2.3)
; out: [vx_cur] = the record
; clobbers: ax, bx, cx, dx, si, di
; -----------------------------------------------------------------------------
vx_choose:
    mov si, vx_modes
    mov di, si                  ; DI = the pick so far
    mov cx, [vx_nmodes_w]
.n:
    push cx
    mov ax, [di+VM_X]
    mul word [di+VM_Y]
    xchg ax, bx
    mov cx, dx                  ; CX:BX = the pick's area, <= 1024 x 768
    mov ax, [si+VM_X]
    mul word [si+VM_Y]          ; DX:AX = SI's
    cmp dx, cx
    jb .keep
    ja .take
    cmp ax, bx
    jb .keep
    ja .take
    mov al, [si+VM_BPP]         ; the same size: the deeper one
    cmp al, [di+VM_BPP]
    jbe .keep
.take:
    mov di, si
.keep:
    pop cx
    add si, VM_SIZE
    loop .n
    mov [vx_cur], di
    ret
; vx_mkpal - the 16-entry RGB word table for a direct-colour mode, from the
;            DAC table and the mode's field layout. At 8bpp nothing to do.
; clobbers: ax, bx, cx, dx, si, di
; -----------------------------------------------------------------------------
vx_mkpal:
    cmp byte [vx_depth], 8
    je .out
    mov si, vx_dac16            ; 16 x (r, g, b), 6-bit
    mov di, vx_pal16
    cld
.e:
    xor dx, dx                  ; DX = the word being built
    mov bx, [vx_cur]
    add bx, VM_RSIZE            ; the record's (size, position) words
    call .field                 ; red
    call .field                 ; green
    call .field                 ; blue
    mov [di], dx
    inc di
    inc di
    cmp di, vx_pal16 + 32
    jb .e
.out:
    ret
; .field - [SI] = a 6-bit gun, [BX] = the field's size then its position:
;          OR the gun's top `size` bits into DX at bit `position`; SI and
;          BX step on to the next gun and field
.field:
    lodsb
    cbw                         ; (a 6-bit gun: AH = 0)
    mov cx, [bx]                ; CL = the size, CH = the position
    inc bx
    inc bx
    sub cl, 6                   ; a 5- or 6-bit field keeps the gun's top
    jnc .pos                    ; bits; a wider one (none offered) takes it
    neg cl                      ; whole
    shr ax, cl
.pos:
    mov cl, ch
    shl ax, cl
    or dx, ax
    ret

; -----------------------------------------------------------------------------
; vx_init - VXV_INIT: DX:SI = the kernel's block, which lives in its `.boot3`
;           (DX = B3_SEG; the boot blob until BOOT3-PLAN wave 4), ES =
;           KERNEL_SEG. Bank the kernel's segment and every pointer - the
;           block is gone once the boot ends - and fill vid_tab row
;           VID_VESA.
; out: CF = 0; CF = 1 = a block of another vintage, nothing banked
; -----------------------------------------------------------------------------
vx_init:
    push es
    push ds
    pop es                      ; ES = ours: the bank
    push ds
    mov ds, dx                  ; DS:SI = the block, in the kernel's `.boot3`
    cld
    lodsw                       ; VXB_VER, and SI on to the pointers
    cmp ax, VX_ABI_VER
    jne .bad
    mov di, vx_p_tabrow         ; the pointer words, in VXB_* order from +2
    mov cx, (VXB_SIZE - 2) / 2
    rep movsw
    pop ds
    pop es
    mov [vx_kseg], es
    call vx_geom                ; ([vx_win] is no window and [vx_cursaved]
    clc                         ; 0 already: the image's own, which nothing
    ret                         ; before the first body writes)
.bad:
    pop ds
    pop es
.stc:                           ; (vx_pick's refusal too)
    stc
    ret

; -----------------------------------------------------------------------------
; vx_pm - VXV_PM (KERN-MEGA-PLAN 14.115, SPEC.md 120.3.4): kern_mega's switch
;         into protected mode. DX = 0 is the boot's QUESTION, asked in real
;         mode right after INIT: CF = 0 says this image draws in protected
;         mode (an image that predates the verb refuses it, and the kernel
;         keeps the VGA). DX != 0 ARMS it, at mega_enter: DX = a data
;         selector over window A, ES = the kernel's data selector, DS = ours
;         as a selector. A Bochs/QEMU VBE (its 'dispi' ID register answers
;         B0C0h-B0CFh) has its bank register written directly from then on,
;         in the window units 4F05h takes; every other card's window goes through
;         4F05h, reflected - a CPU reset each on a 286
; out: CF = 0
; -----------------------------------------------------------------------------
vx_pm:
    or dx, dx
    jz .ok                      ; the question: yes
    mov [vx_kseg], es           ; the kernel, as its data selector...
    mov [vx_wsel], dx           ; ...window A as one, and the bank: the
    push ax                     ; dispi register takes 4F05h's DX as it is,
                                ; in the granularity the BIOS reported (Bochs
                                ; 2.7's is 32KB, vx_wsh 1)
    push dx
    mov dx, VX_DISPI_IX
    mov ax, VX_DISPI_ID
    out dx, ax
    inc dx
    in ax, dx
    and al, 0xF0
    cmp ax, 0xB0C0              ; B0C0h..B0CFh: a dispi interface
    jne .nod
    mov byte [vx_dispi], 1
.nod:
    pop dx
    pop ax
.ok:
    clc
    ret

; vx_geom - vid_tab row VID_VESA (SPEC.md 39.2's nine words) and the kernel's
;           vx_bpp, from the chosen record (after INIT banked the kernel)
; clobbers: ax, bx, cx, si, di, es, flags (DF clear)
vx_geom:
    mov es, [vx_kseg]
    mov di, [vx_p_tabrow]
    cld
    mov ax, [vx_wsel]           ; seg: A000h, or kern_mega's selector over
    stosw                       ; it once VXV_PM has run
    mov ax, [vx_bpsl]
    stosw                       ; stride
    xor ax, ax
    stosw                       ; bmask
    stosw                       ; bshift
    mov ax, [vx_bpsl]
    stosw                       ; rowadd
    xor ax, ax
    stosw                       ; wrapbit
    stosw                       ; wrapfix
    mov ax, [vx_x]
    stosw                       ; cw
    mov ax, [vx_y]
    stosw                       ; ch
    mov di, [vx_p_ep]
    mov al, [vx_bpp]
    xor ah, ah
    mov [es:di+4], ax           ; ...and the kernel's vx_bpp, after vx_ep
    mov si, [vx_cur]
    call vx_encode              ; ...and vx_class, the LIVE record's class
    mov [es:di+6], cl           ; byte, for the Display page's greying
    ret

; -----------------------------------------------------------------------------
; vx_pick - VXV_PICK: DL/DH/CL = a class; choose again and re-publish the
;           geometry. After INIT only. Never refuses (120.2.3): a class the
;           card lacks is Auto
; -----------------------------------------------------------------------------
vx_pick:
    cmp word [vx_kseg], 0
    je vx_init.stc
    push ax                     ; the caller is CTRL.DRV's page handler, whose
    push bx                     ; DI and BP are the pane it goes on to paint
    push cx                     ; (SPEC.md 1's register discipline: nothing
    push dx                     ; but the documented outputs - here none)
    push si
    push di
    mov [vx_want], cl
    call vx_select
    call vx_publish
    call vx_geom
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; vx_next - VXV_NEXT: CL = a class; out CL/CH/DL/DH = the next offered class
;           in the table's order, its depth and its X/8, Y/8 - Auto (0) ->
;           the first record, the last -> Auto. CF = 1 when the class given
;           matched no record (the first is answered then, as for Auto).
;           AX, BX, SI and DI are KEPT: the walk spends SI and DI on the
;           table, and the caller's DI is the pane it paints the answer on
;           (the second cycle of a row drew its label off the pane)
; -----------------------------------------------------------------------------
vx_next_x:
    push ax
    push bx
    push si
    push di
    call vx_next
    pop di                      ; (pop writes no flags: CF is the answer's)
    pop si
    pop bx
    pop ax
    ret
vx_next:
    cmp byte [vx_nmodes], 0
    je .none
    or cl, cl
    jz .first                   ; Auto: the first record
    call vx_find
    jc .firstc                  ; not offered: the first, and say so
    add si, VM_SIZE             ; the one after it...
    mov al, VM_SIZE
    mul byte [vx_nmodes]
    add ax, vx_modes            ; AX = the table's end
    cmp si, ax
    jb .answer                  ; ...or Auto after the last
.none:
    xor dx, dx
    xor cx, cx                  ; (CF = 0)
    ret
.firstc:
    call .first                 ; the first record's answer...
    stc                         ; ...and CF = 1
    ret
.first:
    mov si, vx_modes
.answer:
    call .class
    clc
    ret
; .class - SI = a record -> CL = its class byte, DL/DH = X/8, Y/8, CH = bpp
.class:
    call vx_encode              ; CL (CF = 1 cannot happen: vx_offer refused it)
    push cx
    call vx_decode              ; DL/DH = X/8, Y/8, CL = bpp
    mov ch, cl
    pop ax
    mov cl, al                  ; CL = the class byte, CH = the depth
    ret

; =============================================================================
; THE PRIMITIVE ENTRY (SPEC.md 120.3.1)
;
; Far-called by the kernel's hook itself, `call far [vx_ep]` with the selector
; byte after it, so the FAR RETURN IS THE SELECTOR'S ADDRESS. Every register is
; saved into a frame BP addresses (SS), the selector is read through the
; return, DS becomes ours, and the body runs with the caller's registers
; intact. Before the body runs, the return is pointed where the kernel goes
; on: past the byte for a VX_CONT selector, and to VXB_KRET's kernel `ret`
; otherwise - which pops the hooked body's own return, so a VIDEXT body
; returns to its caller. Nothing after the body touches a flag but the pops,
; so its CF is the answer. On the way out every register comes back from the
; frame, so a body may clobber anything and a body that owes an output writes
; the frame.
;
;   [bp+0]  caller's BP     [bp-2]  DS   [bp-4]  ES   [bp-6]  AX   [bp-8]  BX
;   [bp+2]  ret off = the   [bp-10] CX   [bp-12] DX   [bp-14] SI   [bp-16] DI
;           selector's
;   [bp+4]  ret seg
; =============================================================================
F_DS    equ -2
F_ES    equ -4
F_AX    equ -6
F_BX    equ -8
F_CX    equ -10
F_DX    equ -12
F_SI    equ -14
F_DI    equ -16

vx_prim:
    push bp
    mov bp, sp
    push ds
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    lds bx, [bp+2]              ; DS:BX = the selector: the far return
    mov al, [bx]
    mov bx, cs                  ; DS = ours from here: CS in real mode (an
    test bl, 4                  ; image's base is 512-aligned, so bit 2 of
    jz .rm                      ; its segment is clear), and on kern_mega in
    add bx, 8                   ; protected mode CS's DATA twin, the LDT entry
.rm:                            ; after the code alias (KERN-MEGA-PLAN 14.115)
    mov ds, bx
    mov [vx_dsel], ds           ; ...banked for a body whose DS is elsewhere
    inc word [bp+2]             ; a VX_CONT hook goes on past the byte...
    test al, VX_CONT
    jnz .on
    mov bx, [vx_p_kret]         ; ...any other to the kernel's `ret`, which
    mov [bp+2], bx              ; pops the hooked body's own return
.on:
    and al, VX_SELM
    cmp al, VX_NSEL
    jae .refuse
    cbw                         ; (VX_SELM: AH = 0)
    add ax, ax
    xchg ax, bx
    mov bx, [vx_jt+bx]
    mov [vx_tgt], bx
    mov ax, [bp+F_AX]           ; the two registers the read spent, back
    mov bx, [bp+F_BX]
    call [vx_tgt]               ; the body: CF is its answer
    jmp short .out
.refuse:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    pop ds
    pop bp
    retf

vx_jt:
    dw vxp_fill                 ; VX_FILL
    dw vxp_gray                 ; VX_GRAY
    dw vxp_pat                  ; VX_PAT
    dw vxp_save                 ; VX_SAVE
    dw vxp_restore              ; VX_RESTORE
    dw vxp_fontchar             ; VX_FONTCHAR
    dw vxp_icon                 ; VX_ICON
    dw vxp_curput               ; VX_CURPUT
    dw vxp_curget               ; VX_CURGET
    dw vxp_curmove              ; VX_CURMOVE
    dw vxp_setmode              ; VX_SETMODE
    dw vxp_blit4                ; VX_BLIT4
    dw vxp_blit1                ; VX_BLIT1
    dw vxp_scroll               ; VX_SCROLL
    dw vxp_spans                ; VX_SPANS
    dw vxp_fontrun              ; VX_FONTRUN
    dw vxp_blitd                ; VX_BLITD
%if ($ - vx_jt) / 2 != VX_NSEL
  %error "vx_jt has drifted from vxabi.inc's selectors"
%endif

; =============================================================================
; THE WINDOW (SPEC.md 120.8)
; =============================================================================

; -----------------------------------------------------------------------------
; vx_setwin - make window DX current (a 64KB page number << [vx_wsh])
; in:  DX = the window number
; clobbers: flags only
; -----------------------------------------------------------------------------
vx_setwin:
    cmp dx, [cs:vx_win]         ; CS: the read loops run with DS on the window
    je .out
    push ax
    push bx
    push dx
    push ds
    mov ds, [cs:vx_dsel]        ; (DS may be VRAM: CS is read-only on kern_mega)
    mov [vx_win], dx
    cmp byte [vx_dispi], 0
    jne .port
    mov ax, VBE_WIN
    xor bx, bx                  ; BH = 0 set, BL = 0 window A
    call vx_int10
.done:
    pop ds
    pop dx
    pop bx
    pop ax
.out:
    ret
.port:                          ; kern_mega on a dispi VBE (vx_pm): the bank
    mov bx, dx                  ; register itself, no reflection
    mov dx, VX_DISPI_IX
    mov ax, VX_DISPI_BANK
    out dx, ax
    inc dx
    mov ax, bx
    out dx, ax
    jmp short .done

; -----------------------------------------------------------------------------
; vx_int10 - int 10h, ON THIS IMAGE'S OWN STACK (KERN-MEGA-PLAN 14.115)
; in/out: int 10h's; DS = ours (every caller has it: attach, vx_setwin,
;         VX_SETMODE). Clobbers what int 10h does and nothing more
;
; A VBE BIOS IS A DEEP CALL. Bochs's vgabios - compiled C - took 4F01h ~650
; bytes below the kernel's frame at boot, and task 0's stack is 512 bytes
; with mem_tab's tail and the menu bar's cells under it in `.lowbss`: the
; probe overwrote the last five heap records, the next compaction called a
; "relocation proc" at a segment that was a pushed register, and kern_mega's
; boot ran zeros to EIP 10000h - a #GP the ROM's int 0Dh answered as IRQ5,
; forever (17GB of log in a minute). The kernel was never the one to fix:
; QEMU's SeaVGABIOS hops to a stack of its own for exactly this reason, and
; nothing promises a real card does. So this image hops: SS:SP onto the top
; of a buffer of its own - the scroll's 2KB bounce while attach probes (it is
; unused until a mode is set), and the 768 bytes of the probe's two buffers
; after it (dead from then on) - and back. The kernel already treats a
; foreign SS as a ROM's (sch_switch does not switch, the mouse and chain ISRs
; load their own SS). NOT NESTED: an ISR's hook that lands here while a call
; is out on this stack goes through on the stack it has. NOT IN PROTECTED
; MODE: kern_mega reflects int 10h onto the reflection's own stack, and a
; dispi card is never asked at all (vx_setwin)
; -----------------------------------------------------------------------------
vx_int10:
    cmp byte [cs:vx_onstk], 0
    jne .direct                 ; a hook inside a hop: the stack it has
    cmp word [cs:vx_wsel], VX_WSEG
    jne .direct                 ; kern_mega, protected: reflected
    pushf
    pop word [vx_ofl]           ; the caller's IF, for the call and after it
    cli
    mov [vx_oss], ss
    mov [vx_osp], sp
    inc byte [vx_onstk]
    push ds
    pop ss                      ; (`pop ss` holds off an interrupt one
    mov sp, [vx_stktop]         ; instruction, and IF is clear anyway)
    push word [vx_ofl]
    popf
    int 0x10                    ; (a BIOS keeps DS)
    cli
    mov ss, [vx_oss]
    mov sp, [vx_osp]
    dec byte [vx_onstk]
    push word [vx_ofl]
    popf
    ret
.direct:
    push bp                     ; (kern_mega's reflection of it hands BP back
    int 0x10                    ; changed - measured, 916Dh - and every body
    pop bp                      ; reads its frame through BP)
    ret

; vx_nextwin - the window after the current one (a run crossed its edge)
vx_nextwin:
    push dx
    push cx
    mov dx, 1
    mov cl, [cs:vx_wsh]
    shl dx, cl                  ; one 64KB page's worth of windows
    add dx, [cs:vx_win]
    call vx_setwin
    pop cx
    pop dx
    ret

; -----------------------------------------------------------------------------
; vx_addr - the window and the in-window offset of pixel (AX, BX)
; in:  AX = x, BX = y (both on the display)
; out: DI = the offset, ES = the window's segment, the window set
; clobbers: ax, dx, flags
; -----------------------------------------------------------------------------
vx_addr:
    push cx
    push bx
    cmp byte [cs:vx_bpp], 8
    je .b8
    add ax, ax                  ; two bytes a pixel
.b8:
    xchg ax, bx                 ; AX = y, BX = x in bytes
    mul word [cs:vx_bpsl]       ; DX:AX = y * stride
    add ax, bx
    adc dx, 0                   ; DX = the 64KB page
    mov cl, [cs:vx_wsh]
    shl dx, cl
    mov di, ax
    call vx_setwin
    mov es, [cs:vx_wsel]        ; window A: A000h, or kern_mega's selector
    pop bx
    pop cx
    ret

; VXW - after one stosb/stosw: DI wrapped to 0 means the next byte is in the
;       next window (the row is at most 4096 bytes, so at most one wrap a row)
%macro VXW 0
    or di, di
    jnz %%ok
    call vx_nextwin
%%ok:
%endmacro

; ...and the same for a READ cursor in SI (DS = the window there)
%macro VXWS 0
    or si, si
    jnz %%ok
    call vx_nextwin
%%ok:
%endmacro

; -----------------------------------------------------------------------------
; vx_rowfill - CX pixels of colour AL from ES:DI, with the window edge handled
; in:  ES:DI set by vx_addr, CX = pixels (> 0), AL = the colour index
; clobbers: ax, cx, di, flags
; -----------------------------------------------------------------------------
vx_rowfill:
    push bx
    cmp byte [cs:vx_bpp], 8
    jne .w16
    mov bx, di                  ; bytes: does DI + CX carry?
    add bx, cx
    jnc .one8
    ; straddle: BX = bytes past the edge, CX - BX = bytes before it. AL is
    ; the colour throughout: `rep stosb` and vx_nextwin keep it
    push bx
    sub cx, bx
    rep stosb
    call vx_nextwin
    xor di, di
    pop cx
.one8:
    rep stosb
    pop bx
    ret
.w16:
    xor ah, ah
    mov bx, ax
    add bx, bx
    mov ax, [cs:vx_pal16+bx]    ; AX = the RGB word
    mov bx, cx                  ; DI + 2*CX, as 2*CX first: CX is at most
    add bx, bx                  ; 1,024 pixels, so only the add of DI can
    add bx, di                  ; carry. (DI + CX first lost the carry of a
    jnc .one16                  ; run more than half past the edge, which
    push bx                     ; then wrapped onto the window's own first
    shr bx, 1                   ; bytes - until driver size pass 1)
    sub cx, bx                  ; words past the edge, and before it
    rep stosw
    call vx_nextwin
    xor di, di
    pop cx
    shr cx, 1
.one16:
    rep stosw
    pop bx
    ret

; -----------------------------------------------------------------------------
; vx_rowcopy - CX BYTES from DS:SI to ES:DI, with the window edge handled:
;              one `rep movsb` to the edge, the next window, the rest. The
;              per-byte `VXW` test it replaces halved the depth blit and the
;              restore against the fill on a 486 (SPEC.md 120.8: 246 us a
;              1,248-byte row against the fill's 117)
; in:  ES:DI set by vx_addr, DS:SI the source, CX = bytes (> 0)
; clobbers: cx, si, di, flags
; -----------------------------------------------------------------------------
vx_rowcopy:
    push bx
    mov bx, di
    add bx, cx
    jnc .one                    ; DI + CX does not carry: one run
    push bx                     ; BX = bytes past the edge
    sub cx, bx
    call .run
    call vx_nextwin
    xor di, di
    pop cx
.one:
    call .run
    pop bx
    ret
.run:                           ; CX bytes: words, then the odd byte (a 486
    shr cx, 1                   ; moves a word a cycle under `rep movsw` and a
    rep movsw                   ; byte a cycle under `rep movsb`)
    adc cx, cx
    rep movsb
    ret

; -----------------------------------------------------------------------------
; vx_rowread - vx_rowcopy for a READ: CX BYTES from DS:SI, the window, to
;              ES:DI, with the window edge the SOURCE's (driver size pass 1:
;              the save and the scroll's read half moved a byte or word and
;              tested the edge after each, which crossed it at the same byte -
;              the same bytes, the same window switches, an edge ending the
;              row included). NOT the arrow's: its save-under and restore
;              keep their byte loops, which reach vx_nextwin six bytes
;              shallower on the mouse ISR's private stack (SPEC.md 120.3.3)
; in:  DS:SI set off vx_addr, ES:DI the destination, CX = bytes (> 0)
; clobbers: cx, si, di, flags
; -----------------------------------------------------------------------------
vx_rowread:
    push bx
    mov bx, si
    add bx, cx
    jnc .one                    ; SI + CX does not carry: one run
    push bx                     ; BX = bytes past the edge
    sub cx, bx
    call vx_rowcopy.run
    call vx_nextwin             ; (DS is the window: it reads through CS)
    xor si, si
    pop cx
.one:
    call vx_rowcopy.run
    pop bx
    ret

; -----------------------------------------------------------------------------
; vx_px - put colour AL at ES:DI, advancing DI (and the window) one pixel
; in:  AL = the colour index; ES:DI the pixel
; clobbers: flags (AX kept)
; -----------------------------------------------------------------------------
vx_px:
    cmp byte [cs:vx_bpp], 8
    jne .w16
    stosb
    VXW
    ret
.w16:
    push ax
    push bx
    xor ah, ah
    mov bx, ax
    add bx, bx
    mov ax, [cs:vx_pal16+bx]
    stosw
    VXW
    pop bx
    pop ax
    ret

; vx_skip - step DI past one pixel without writing it
vx_skip:
    inc di
    cmp byte [cs:vx_bpp], 8
    je .k
    inc di
.k:
    VXW
    ret

; -----------------------------------------------------------------------------
; vx_clip - clip the rect AX/BX/CX/DX (x1,y1,x2,y2 inclusive, signed) to the
;           display
; out: CF = 1 nothing left; else CF = 0, AX/BX = the clipped x1/y1, and
;      DX = [vx_rows] = its rows, CX = [vx_w] = its pixels a row - the six
;      instructions each of its four callers ran after it, run here once
; -----------------------------------------------------------------------------
vx_clip:
    or ax, ax
    jns .x1
    xor ax, ax
.x1:
    or bx, bx
    jns .y1
    xor bx, bx
.y1:
    cmp cx, [vx_x]
    jl .x2
    mov cx, [vx_x]
    dec cx
.x2:
    cmp dx, [vx_y]
    jl .y2
    mov dx, [vx_y]
    dec dx
.y2:
    cmp ax, cx
    jg .none
    cmp bx, dx
    jg .none
    sub dx, bx
    inc dx
    mov [vx_rows], dx           ; DX = rows
    sub cx, ax                  ; (CF = 0: 0 <= x1 <= x2, so no borrow)
    inc cx
    mov [vx_w], cx              ; CX = pixels a row
    ret
.none:
    stc
    ret

; =============================================================================
; THE BODIES
; =============================================================================

; -----------------------------------------------------------------------------
; VX_FILL - vga_solid_rect: AX/BX/CX/DX the rect, [gfx_color] the ink,
;           [vga_xorm] 0 = replace, else XOR the index with it
; -----------------------------------------------------------------------------
vxp_fill:
    call vx_clip
    jc .out
    push ax
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_p_color]
    mov al, [si]                ; the ink
    mov si, [cs:vx_p_xorm]
    mov ah, [si]                ; the XOR mask
    pop ds
    and al, 15
    mov [vx_ink], ax
    pop ax
.row:
    push ax                     ; (vx_addr keeps BX and CX)
    call vx_addr                ; ES:DI = (x1, y)
    pop ax
    mov cx, [vx_w]
    cmp byte [vx_ink+1], 0
    jne .xor
    push ax
    mov al, [vx_ink]
    call vx_rowfill
    pop ax
    jmp short .next
.xor:
    cmp byte [vx_bpp], 8
    jne .xor16
.xpx8:
    mov dl, [es:di]
    xor dl, [vx_ink+1]
    mov [es:di], dl
    inc di
    VXW
    loop .xpx8
    jmp short .next
.xor16:                         ; a word's index is found in the table, XORed
    mov dx, [es:di]             ; and looked up again (SPEC.md 120.1)
    call vx_rgb2ix              ; DL = the index of the word in DX
    xor dl, [vx_ink+1]
    push ax
    mov al, dl
    call vx_px
    pop ax
    loop .xor16
.next:
    inc bx
    dec word [vx_rows]
    jnz .row
.out:
    clc
    ret

; vx_rgb2ix - DX = an RGB word -> DL = the desktop index that packs to it
;             (0 when none does)
vx_rgb2ix:
    push bx
    push cx
    mov bx, vx_pal16
    mov cx, 16
.n:
    cmp dx, [bx]
    je .hit
    inc bx
    inc bx
    loop .n
    xor dl, dl
    jmp short .o
.hit:
    mov dl, 16
    sub dl, cl
.o:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; VX_GRAY - gfx_fill_gray_raw: (x+y) even = white, odd = black (SPEC.md 5)
; -----------------------------------------------------------------------------
vxp_gray:
    call vx_clip                ; [vx_rows], [vx_w]
    jc .out
.row:
    push ax                     ; (vx_addr keeps BX and CX)
    call vx_addr
    pop ax
    mov cx, [vx_w]
    mov dx, ax
    add dx, bx                  ; DL bit 0 = the first pixel's parity
.px:
    push ax
    mov al, 15
    test dl, 1
    jz .white
    xor al, al
.white:
    call vx_px
    pop ax
    inc dx
    loop .px
    inc bx
    dec word [vx_rows]
    jnz .row
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; VX_PAT - gfx_fill_pat_raw: the eight bytes at the kernel's [gfx_pat]; row y
;          takes byte (y & 7), bit 7 is x = 0 mod 8, a set bit is WHITE
; -----------------------------------------------------------------------------
vxp_pat:
    call vx_clip                ; [vx_rows], [vx_w] - BEFORE the copy spends
    jc .out                     ; CX: a width cut from its leftover 0 was -103
    push ds                     ; pixels, which the row loop drew as 65,433
    push es                     ; across the desktop
    push ds
    pop es                      ; ES = ours, the copy's destination
    mov si, [vx_p_pat]
    mov ds, [vx_kseg]
    mov si, [si]                ; SI = the kernel's eight bytes
    mov di, vx_patbuf
    mov cx, 4
    cld
    rep movsw                   ; copied, so the row loop reads them through CS
    pop es
    pop ds
.row:
    push ax                     ; (vx_addr keeps BX and CX)
    call vx_addr
    pop ax
    mov cx, [vx_w]
    mov si, bx
    and si, 7
    mov dh, [vx_patbuf+si]      ; this row's byte
    mov dl, al
    and dl, 7                   ; the first pixel's bit position
.px:
    push cx
    mov cl, dl
    mov ch, 0x80
    shr ch, cl
    test dh, ch
    pop cx
    push ax
    mov al, 15
    jnz .white
    xor al, al
.white:
    call vx_px
    pop ax
    inc dl
    and dl, 7
    loop .px
    inc bx
    dec word [vx_rows]
    jnz .row
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; VX_SAVE - vga_save_vram: AX/BX/CX/DX the rect, ES:DI the buffer; DI advanced
;           past the data (SPEC.md 5.3). x1 is rounded down and x2 up to the
;           byte column, as the kernel's is, and the buffer is sized by the
;           kernel at (span bytes) x rows x [vga_pn]: full depth when vga_pn is
;           the bpp - one byte or word a pixel, row-major - and ONE BIT a pixel
;           when vga_pn is 1 (SPEC.md 11.96.17's one-plane window: only colours
;           0 and 15, so a bit says which)
; -----------------------------------------------------------------------------
vxp_save:
    call vx_sr_setup            ; clips, aligns, [vx_rows], [vx_w] (pixels),
    jc .out                     ; [vx_one] = the one-plane arm; AX/BX = x1/y1
    mov si, [bp+F_DI]           ; the buffer cursor (ES = the caller's)
    mov [vx_buf], si
.row:
    push ax                     ; (vx_addr keeps BX, and the row's loops
    push es                     ; do not touch it)
    call vx_addr                ; ES:DI = the row's first pixel in VRAM
    mov si, di
    mov ax, es
    pop es                      ; ES:DI = the buffer again
    mov di, [vx_buf]
    push ds
    mov ds, ax                  ; DS:SI = VRAM
    mov cx, [cs:vx_w]
    cmp byte [cs:vx_one], 0
    jne .one
    cmp byte [cs:vx_bpp], 8
    je .cb
    add cx, cx                  ; bytes a row
.cb:
    call vx_rowread             ; one rep to the edge, the next window, the rest
    jmp short .rdone
.one:                           ; 8 pixels -> 1 byte, bit 7 the leftmost
    mov dl, 0
    mov dh, 8
.op:
    shl dl, 1
    cmp byte [cs:vx_bpp], 8
    jne .o16
    lodsb
    VXWS
    or al, al
    jz .oz
    or dl, 1
    jmp short .oz
.o16:
    lodsw
    VXWS
    or ax, ax
    jz .oz
    or dl, 1
.oz:
    dec dh
    jnz .onext
    mov al, dl
    stosb
    mov dh, 8
    mov dl, 0
.onext:
    loop .op
.rdone:
    pop ds
    mov [vx_buf], di
    pop ax
    inc bx
    dec word [vx_rows]
    jnz .row
    mov ax, [vx_buf]
    mov [bp+F_DI], ax           ; the output: DI past the data
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; VX_RESTORE - vga_restore_vram: the rect, ES:SI the buffer; SI advanced.
;              Honours the SUB-RECT restore (SPEC.md 5.8): the kernel's
;              [gfx_sub_st]/[gfx_sub_rs] are in bytes of its 1bpp-per-plane
;              layout, so they scale by the bytes a saved byte column takes
;              here - [vx_bpp] at full depth, 1 on the one-plane arm
; -----------------------------------------------------------------------------
vxp_restore:
    call vx_sr_setup
    jc .out
    mov si, [bp+F_SI]
    ; --- the sub-rect offsets, scaled ----------------------------------------------
    push bx                     ; (BX is y1 from here to the row loop)
    push ds
    mov ds, [vx_kseg]
    mov bx, [cs:vx_p_sub]
    mov cx, [bx]                ; gfx_sub_st
    mov dx, [bx+2]              ; gfx_sub_rs
    pop ds
    pop bx
    mov ax, dx
    mul word [vx_sc1]           ; AX = rs * scale ([vx_sc1] is vx_sr_setup's)
    mov [vx_rs], ax
    mov ax, cx
    mul word [vx_sc1]
    add si, ax                  ; SI += st * scale
    mov [vx_buf], si
    mov ax, [vx_x1]             ; x1 again: the arithmetic above spent AX
.row:
    push ax                     ; (vx_addr, vx_rowcopy and vx_px keep BX)
    call vx_addr                ; ES:DI = VRAM
    push ds
    mov ds, [bp+F_ES]           ; DS:SI = the buffer
    mov si, [cs:vx_buf]
    mov cx, [cs:vx_w]
    cmp byte [cs:vx_one], 0
    jne .one
    cmp byte [cs:vx_bpp], 8
    je .cb
    add cx, cx                  ; bytes a row
.cb:
    call vx_rowcopy             ; one rep to the edge, the next window, the rest
    jmp short .rdone
.one:
    mov dh, 0                   ; bits left in DL
.op:
    or dh, dh
    jnz .bit
    lodsb
    mov dl, al
    mov dh, 8
.bit:
    shl dl, 1
    mov al, 0
    jnc .put
    mov al, 15
.put:
    call vx_px
    dec dh
    loop .op
.rdone:
    add si, [cs:vx_rs]
    pop ds
    mov [vx_buf], si
    pop ax
    inc bx
    dec word [vx_rows]
    jnz .row
    mov ax, [vx_buf]
    mov [bp+F_SI], ax
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; vx_sr_setup - the save/restore geometry: clip, round x1 down and x2 up to
;               the byte column, and read the one-plane arm
; in:  AX/BX/CX/DX the rect
; out: CF = 1 empty; else AX/BX = x1/y1, [vx_w] pixels a row (a multiple of
;      8), [vx_rows], [vx_one] = 1 when [vga_pn] is 1, [vx_sc1] = the scale
; -----------------------------------------------------------------------------
vx_sr_setup:
    and al, 0xF8
    or cl, 7
    call vx_clip                ; [vx_rows], [vx_w]
    jc .out
    mov [vx_x1], ax
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_p_pn]
    mov cl, [si]                ; vga_pn: the planes a save walks
    pop ds
    mov byte [vx_one], 0
    mov al, [vx_bpp]
    cmp cl, 1
    jne .full
    mov byte [vx_one], 1
    mov al, 1
.full:
    xor ah, ah
    mov [vx_sc1], ax
    mov ax, [vx_x1]             ; the clipped x1, on the grid (a clip to 0 is)
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; VX_FONTCHAR - font_char's planar arm: CX = x, DX = y + r0, SI = the glyph's
;               row r0 in the kernel's [font_seg], [wm_clip_r0] = (r0, rows),
;               [wm_clip_cm] the column mask, [gfx_color] the ink. The kernel
;               has clipped: x <= cw-8 and the rows fit (SPEC.md 6)
; -----------------------------------------------------------------------------
vxp_fontchar:
    push ds
    mov ds, [vx_kseg]
    mov bx, [cs:vx_p_clipr0]
    mov ax, [bx]                ; AL = r0, AH = rows
    mov bx, [cs:vx_p_clipcm]
    mov bl, [bx]                ; BL = the column mask
    mov di, [cs:vx_p_color]
    mov bh, [di]                ; BH = the ink
    mov di, [cs:vx_p_font]
    mov di, [di]                ; DI = font_seg
    pop ds
    mov [vx_ink], bx            ; mask, ink
    mov [vx_fseg], di
    mov [vx_rows], ax           ; (AL = r0 unused, AH = rows)
    or ah, ah
    jz .out
    mov [vx_glyph], si
    mov ax, cx
    mov bx, dx
.row:
    push ax                     ; (vx_addr keeps BX and CX)
    call vx_addr
    pop ax
    push ds
    mov ds, [vx_fseg]
    mov si, [cs:vx_glyph]
    mov dl, [si]
    pop ds
    inc word [vx_glyph]
    and dl, [vx_ink]            ; the column mask
    mov cx, 8
.px:
    shl dl, 1
    jnc .skip
    push ax
    mov al, [vx_ink+1]
    call vx_px
    pop ax
    jmp short .n
.skip:
    call vx_skip
.n:
    loop .px
    inc bx
    dec byte [vx_rows+1]
    jnz .row
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; VX_ICON - ico_core's planar arm: [ico_rows] rows of [ico_ww] words at
;           ([ico_x], [ico_y]); the mask rows at [ico_maskp] in colour
;           [ico_c1], then the data rows at [ico_datap] in [ico_c2]; a word's
;           MSB is its leftmost pixel; x may be negative and may run off the
;           right edge (SPEC.md 25). y is already clipped by the kernel
; -----------------------------------------------------------------------------
vxp_icon:
    push es                     ; the kernel's cells through ES, so DS stays
    mov es, [vx_kseg]           ; ours for the stores
    mov si, [vx_p_icog]
    mov ax, [es:si]             ; ico_ww
    mov [vx_iww], ax
    mov ax, [es:si+4]           ; ico_rows
    mov [vx_irows], ax
    mov si, [vx_p_icoxy]
    mov ax, [es:si]             ; ico_x
    mov [vx_ix], ax
    mov ax, [es:si+2]           ; ico_y
    mov [vx_iy], ax
    mov si, [vx_p_icop]
    mov ax, [es:si]             ; ico_maskp
    mov [vx_ip], ax
    mov ax, [es:si+2]           ; ico_datap
    mov [vx_ip2], ax
    mov si, [vx_p_icoc]
    mov ax, [es:si]             ; c1, c2
    mov [vx_ink], ax
    pop es
    cmp word [vx_irows], 0
    je .out
    mov al, [vx_ink]
    mov si, [vx_ip]
    call .pass                  ; the underlay
    mov al, [vx_ink+1]
    mov si, [vx_ip2]
    call .pass                  ; the picture
.out:
    clc
    ret
; .pass - AL = the colour, SI = the rows (kernel offset)
.pass:
    mov [vx_pcol], al
    mov [vx_glyph], si
    mov cx, [vx_irows]
    mov bx, [vx_iy]
.row:
    push cx
    mov ax, [vx_ix]             ; AX = x of this word's first pixel
    mov cx, [vx_iww]
.word:
    push cx
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_glyph]
    mov dx, [si]                ; the row word, MSB leftmost
    pop ds
    add word [vx_glyph], 2
    mov cx, 16
.px:
    shl dx, 1
    jnc .next
    or ax, ax                   ; on the display?
    js .next
    cmp ax, [vx_x]
    jae .next
    push ax                     ; (vx_addr and vx_px keep BX)
    push dx
    call vx_addr
    pop dx
    mov al, [vx_pcol]
    call vx_px
    pop ax
.next:
    inc ax
    loop .px
    pop cx
    loop .word
    inc bx
    pop cx
    loop .row
    ret

; -----------------------------------------------------------------------------
; THE ARROW (SPEC.md 120.3.3): the driver's whole cycle
;
; The cell is CUR_GW x CUR_GH at (cur_drawn - hot spot), clamped to the origin
; and cut at the right and bottom edges; what was under it is banked in
; vx_cursave at the display's depth and put back by VX_CURGET. The two mask
; tables are the kernel's, read through cur_tabp: a black-table bit is black,
; a white-table bit not black is white, anything else is left alone.
; -----------------------------------------------------------------------------
vxp_curput:
    call vx_curcell             ; [vx_cx], [vx_cy], [vx_cw], [vx_ch]
    jc .out
    call vx_cursave
    call vx_curdraw
.out:
    clc
    ret

vxp_curget:
    cmp byte [vx_cursaved], 0
    je .out
    call vx_currest
.out:
    clc
    ret

vxp_curmove:                    ; AX/BX = the new position
    push ax
    push bx
    call vxp_curget
    pop bx
    pop ax
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_p_curpos]
    mov [si], ax                ; cur_drawn_x
    mov [si+2], bx              ; cur_drawn_y
    pop ds
    jmp vxp_curput

; vx_curcell - the cell's geometry from the kernel's cur_drawn and hot spot
; out: CF = 1 wholly off the display; else [vx_cx]/[vx_cy]/[vx_cw]/[vx_ch]
vx_curcell:
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_p_curpos]
    mov ax, [si]
    mov bx, [si+2]
    mov si, [cs:vx_p_curhot]
    sub ax, [si]
    sub bx, [si+2]
    mov si, [cs:vx_p_curtab]
    mov si, [si]                ; -> the white table (the black one 12 past)
    pop ds
    mov [vx_ctab], si
    or ax, ax
    jns .x
    xor ax, ax
.x:
    or bx, bx
    jns .y
    xor bx, bx
.y:
    mov [vx_cx], ax
    mov [vx_cy], bx
    mov cx, [vx_x]
    sub cx, ax                  ; columns left of the right edge
    jle .none
    cmp cx, CUR_GW
    jbe .w
    mov cx, CUR_GW
.w:
    mov [vx_cw], cx
    mov cx, [vx_y]
    sub cx, bx
    jle .none
    cmp cx, CUR_GH
    jbe .h
    mov cx, CUR_GH
.h:
    mov [vx_ch], cx
    clc
    ret
.none:
    stc
    ret

vx_cursave:
    mov di, vx_cursave_b
    mov [vx_buf], di
    mov bx, [vx_cy]
    mov dx, [vx_ch]
.row:
    mov ax, [vx_cx]
    push dx                     ; (vx_addr keeps BX)
    call vx_addr                ; ES:DI = VRAM
    mov si, di
    push ds
    mov ax, es
    mov ds, ax                  ; DS:SI = VRAM
    mov es, [cs:vx_dsel]
    mov di, [es:vx_buf]         ; ES:DI = our buffer
    mov cx, [es:vx_cw]
    cmp byte [cs:vx_bpp], 8
    jne .w
.b:
    movsb
    VXWS
    loop .b
    jmp short .d
.w:
    movsw
    VXWS
    loop .w
.d:
    mov [es:vx_buf], di
    pop ds
    pop dx
    inc bx
    dec dx
    jnz .row
    mov byte [vx_cursaved], 1
    mov ax, [vx_cx]
    mov [vx_scx], ax
    mov ax, [vx_cy]
    mov [vx_scy], ax
    mov ax, [vx_cw]
    mov [vx_scw], ax
    mov ax, [vx_ch]
    mov [vx_sch], ax
    ret

vx_currest:
    mov si, vx_cursave_b
    mov bx, [vx_scy]
    mov dx, [vx_sch]
.row:
    mov ax, [vx_scx]
    push dx                     ; (vx_addr keeps BX)
    push si
    call vx_addr                ; ES:DI = VRAM
    pop si
    mov cx, [vx_scw]
    cmp byte [vx_bpp], 8
    jne .w
.b:
    movsb                       ; DS = CS: our buffer
    VXW
    loop .b
    jmp short .d
.w:
    movsw
    VXW
    loop .w
.d:
    pop dx
    inc bx
    dec dx
    jnz .row
    mov byte [vx_cursaved], 0
    ret

vx_curdraw:
    mov bx, [vx_cy]
    mov dx, [vx_ch]
    mov si, [vx_ctab]
    mov [vx_glyph], si
.row:
    mov ax, [vx_cx]
    push dx                     ; (vx_addr, vx_px and vx_skip keep BX)
    call vx_addr
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_glyph]
    mov dl, [si]                ; white
    mov dh, [si+CUR_TABSZ]      ; black
    pop ds
    inc word [vx_glyph]
    mov cx, [vx_cw]
.px:
    shl dh, 1
    jc .black
    shl dl, 1
    jnc .skip
    mov al, 15
    call vx_px
    jmp short .n
.black:
    shl dl, 1
    xor al, al
    call vx_px
    jmp short .n
.skip:
    call vx_skip
.n:
    loop .px
    pop dx
    inc bx
    dec dx
    jnz .row
    ret

; -----------------------------------------------------------------------------
; VX_SETMODE - vid_setmode's arm: enter the chosen mode and load the DAC's
;              first sixteen (8bpp). The kernel's spl_busy bracket is around
;              us; the caller owes the repaint
; -----------------------------------------------------------------------------
vxp_setmode:
    mov ax, VBE_SET
    mov bx, [vx_tab+VXS_MODE]
    call vx_int10
    mov word [vx_win], 0xFFFF
    mov byte [vx_cursaved], 0
    cmp byte [vx_depth], 8
    jne .out
    pushf
    cli
    mov dx, 0x3C8
    xor al, al
    out dx, al
    inc dx
    mov si, vx_dac16
    mov cx, 48
.d:
    lodsb
    out dx, al
    loop .d
    popf
.out:
    clc
    ret


; =============================================================================
; THE S2 BODIES: the blits, the scroll, the span list and the one-pass run
; =============================================================================

; -----------------------------------------------------------------------------
; VX_BLIT4 - gfx_blit4: BX = y, [gfx_blit_x]/[gfx_blit_w]/[gfx_blit_h]/
;            [gfx_blit_sv] through the block, ES = the source segment (the
;            frame's), BP = its stride (the caller's). Two pixels a byte, the
;            high nibble leftmost. Unclipped in both axes (SPEC.md 5.4)
; -----------------------------------------------------------------------------
vxp_blit4:
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_p_blit]
    mov ax, [si]                ; x
    mov cx, [si+2]              ; w
    mov dx, [si+4]              ; h
    mov di, [si+6]              ; sv
    pop ds
    mov [vx_bx0], ax
    mov [vx_bsrc], di
    mov di, [bp]                ; the CALLER's BP, the source stride (BP is
    mov [vx_bstride], di        ; our frame pointer here). NOT through AX,
    mov [vx_rows], dx           ; which is still x: loaded there once, the
                                ; stride became the row's left edge, 96 pixels
                                ; off, and the source phase it implied read
                                ; 16KB past the block - a window of noise
    ; --- the visible x range: [xs, xe) --------------------------------------
    mov di, ax
    add di, cx                  ; DI = x + w = xe
    cmp di, [vx_x]
    jle .xe
    mov di, [vx_x]
.xe:
    or ax, ax
    jns .xs
    xor ax, ax
.xs:
    cmp ax, di
    jge .out                    ; nothing of it on the display
    mov [vx_bxs], ax
    sub di, ax
    mov [vx_w], di              ; pixels a row
    ; --- the visible rows: skip those above the top --------------------------
    or bx, bx
    jns .ytop
    neg bx
    cmp bx, [vx_rows]
    jae .out
    sub [vx_rows], bx
    mov ax, bx
    mul word [vx_bstride]
    add [vx_bsrc], ax
    xor bx, bx
.ytop:
.row:
    cmp bx, [vx_y]
    jae .out                    ; off the bottom: the rest is too
    mov ax, [vx_bxs]
    call vx_addr                ; ES:DI = (xs, y) (BX kept)
    mov si, [vx_bxs]
    sub si, [vx_bx0]            ; SI = pixels into the block
    mov cx, si
    shr si, 1                   ; ...as bytes
    add si, [vx_bsrc]
    and cl, 1                   ; CL = 1 when the first pixel is a LOW nibble
    push ds
    mov ds, [bp+F_ES]           ; DS:SI = the source row
    mov dx, [cs:vx_w]
.px:
    lodsb
    mov ah, al
    or cl, cl
    jnz .low
    mov ch, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1                   ; the high nibble, leftmost
    call vx_px
    dec dx
    jz .rdone
    mov al, ch
.low:
    and al, 0x0F                ; the low nibble
    call vx_px
    xor cl, cl
    dec dx
    jnz .px
.rdone:
    pop ds
    mov ax, [vx_bstride]
    add [vx_bsrc], ax
    inc bx
    dec word [vx_rows]
    jnz .row
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; VX_BLITD - OSAPI_GFX_BLITD (SPEC.md 120.7): the package's block AT THE
;            DEPTH - ES:SI = pixels as this mode packs them, BP (the
;            caller's) = the stride in bytes, AX/BX = x/y (signed), CX/DX =
;            w/h - clipped to the display in both axes, vxp_blit4's way, and
;            moved a row at a time with the window edge handled. CX = 0 is
;            the PALETTE FORM: DX triples at ES:SI into DAC entries AX..,
;            AX >= 16, AX + DX <= 256, 8bpp only. CF = 1 refused
; -----------------------------------------------------------------------------
vxp_blitd:
    or cx, cx
    jnz .blit
    jmp .pal                    ; (an 8086 has no near conditional jump)
.blit:
    mov [vx_bx0], ax
    mov [vx_bsrc], si
    mov [vx_rows], dx
    mov si, [bp]                ; the CALLER's BP: the source stride
    mov [vx_bstride], si
    or dx, dx
    jz .out                     ; no rows
    ; --- the visible x range: [xs, xe) --------------------------------------
    mov di, ax
    add di, cx                  ; DI = x + w = xe
    cmp di, [vx_x]
    jle .xe
    mov di, [vx_x]
.xe:
    or ax, ax
    jns .xs
    xor ax, ax
.xs:
    cmp ax, di
    jge .out                    ; nothing of it on the display
    mov [vx_bxs], ax
    sub di, ax
    mov [vx_w], di              ; pixels a row
    ; --- the visible rows: skip those above the top --------------------------
    or bx, bx
    jns .row
    neg bx
    cmp bx, [vx_rows]
    jae .out
    sub [vx_rows], bx
    mov ax, bx
    mul word [vx_bstride]
    add [vx_bsrc], ax
    xor bx, bx
.row:
    cmp bx, [vx_y]
    jae .out                    ; off the bottom: the rest is too
    mov ax, [vx_bxs]
    call vx_addr                ; ES:DI = (xs, y) (BX kept)
    mov si, [vx_bxs]
    sub si, [vx_bx0]            ; SI = pixels into the block...
    cmp byte [vx_bpp], 8
    je .s8
    add si, si                  ; ...as bytes
.s8:
    add si, [vx_bsrc]
    mov cx, [vx_w]
    cmp byte [vx_bpp], 8
    je .cb
    add cx, cx                  ; bytes a row
.cb:
    push ds
    mov ds, [bp+F_ES]           ; DS:SI = the source row
    call vx_rowcopy             ; one rep to the edge, the next window, the rest
    pop ds
    mov ax, [vx_bstride]
    add [vx_bsrc], ax
    inc bx
    dec word [vx_rows]
    jnz .row
.out:
    clc
    ret
.pal:                           ; --- the palette form ----------------------
    cmp byte [vx_bpp], 8
    jne .refuse                 ; no DAC behind a 16bpp pixel
    cmp ax, 16
    jb .refuse                  ; the desktop's sixteen are not for loading
    mov di, ax
    add di, dx
    cmp di, 256
    ja .refuse
    or dx, dx
    jz .out                     ; nothing to load
    mov cx, dx
    push ds
    mov ds, [bp+F_ES]           ; DS:SI = the triples
    mov dx, 0x3C8
    out dx, al                  ; the first entry (AX < 256: AL is it)
    inc dx                      ; 3C9h, auto-incrementing R, G, B
.t:
    lodsb
    shr al, 1
    shr al, 1                   ; 8 bits a gun to the DAC's 6
    out dx, al
    lodsb
    shr al, 1
    shr al, 1
    out dx, al
    lodsb
    shr al, 1
    shr al, 1
    out dx, al
    loop .t
    pop ds
    clc
    ret
.refuse:
    stc
    ret

; -----------------------------------------------------------------------------
; VX_BLIT1 - gfx_blit1_x at .ybot: AX = x (on the byte grid), BX = y, CX =
;            width in pixels, DX = rows, ES:SI = the band's first byte, BP =
;            its stride - all clipped to the display by the kernel. The pen
;            through VXB_B1 (ink, paper, adp: 2 = a set bit is the PAPER), the
;            head column's mask through VXB_B1HM (nonzero: ONE byte column,
;            and only the mask's set positions are drawn - SPEC.md 5.4.2.8)
; -----------------------------------------------------------------------------
vxp_blit1:
    push es
    mov es, [vx_kseg]
    mov di, [vx_p_b1]
    mov di, [es:di]             ; DL = ink, DH = paper... in DI for a moment
    mov [vx_ink], di
    mov di, [vx_p_b1hm]
    mov di, [es:di]
    mov [vx_b1hm], di           ; (the low byte)
    pop es
    mov ax, [bp+F_AX]
    mov [vx_bx0], ax            ; x
    mov [vx_rows], dx
    mov ax, [bp]                ; the CALLER's BP: the band's stride
    mov [vx_bstride], ax
    mov [vx_bsrc], si
    mov di, cx
    shr di, 1
    shr di, 1
    shr di, 1
    mov [vx_bnb], di            ; whole bytes a row
    and cx, 7
    mov ch, 0FFh                ; the tail's mask: its top CL bits
    jz .nomask
    shr ch, cl
    not ch
    jmp short .maskok
.nomask:
    xor ch, ch
.maskok:
    cmp byte [vx_b1hm], 0
    je .mset
    mov ch, [vx_b1hm]           ; ...or the head column's exact mask
    mov word [vx_bnb], 0
.mset:
    mov [vx_bmask], ch
    cmp word [vx_rows], 0
    je .out
.row:
    mov ax, [vx_bx0]
    call vx_addr                ; ES:DI = (x, y) (BX kept)
    mov si, [vx_bsrc]
    mov cx, [vx_bnb]
    push ds
    mov ds, [bp+F_ES]           ; DS:SI = the band row
    jcxz .tail
.byte:
    lodsb
    mov dl, al
    mov dh, 8
.bit:
    shl dl, 1
    mov al, [cs:vx_ink+1]       ; paper
    jnc .put
    mov al, [cs:vx_ink]         ; ink
.put:
    call vx_px
    dec dh
    jnz .bit
    loop .byte
.tail:
    mov dh, [cs:vx_bmask]
    or dh, dh
    jz .rdone
    lodsb
    mov dl, al
.tbit:
    shl dh, 1
    jnc .tskip
    shl dl, 1
    mov al, [cs:vx_ink+1]
    jnc .tput
    mov al, [cs:vx_ink]
.tput:
    call vx_px
    jmp short .tnext
.tskip:
    shl dl, 1
    call vx_skip
.tnext:
    or dh, dh
    jnz .tbit
.rdone:
    pop ds
    mov ax, [vx_bstride]
    add [vx_bsrc], ax
    inc bx
    dec word [vx_rows]
    jnz .row
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; VX_SCROLL - gfx_scroll: the six vgas_* words through VXB_SCROLL - the left
;             byte column, bytes a row, the signed dy (positive = content UP),
;             the first destination row, the step (+1 top-down, -1 bottom-up)
;             and the rows to move. Each row goes through a bounce buffer, so
;             a window edge inside either row is the helpers' business
; -----------------------------------------------------------------------------
vxp_scroll:
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_p_scroll]
    mov ax, [si]                ; col
    mov cx, [si+2]              ; nb
    mov dx, [si+4]              ; sdy
    mov bx, [si+6]              ; ydst
    mov di, [si+8]              ; step
    mov si, [si+10]             ; rows
    pop ds
    mov [vx_sstep], di
    mov [vx_rows], si
    mov [vx_sdy], dx
    mov dx, cx                  ; DX = nb, out of CX before CL is the shift:
    mov cl, 3                   ; reading nb back out of CX after this took
    shl ax, cl                  ; its low byte as 3, so every row moved 24
    mov [vx_bx0], ax            ; pixels and the rest of the band stayed put
    mov ax, dx
    shl ax, cl                  ; pixels a row = nb * 8
    mov [vx_w], ax
    cmp word [vx_rows], 0
    je .out
.row:
    push bx
    add bx, [vx_sdy]            ; the source row
    mov ax, [vx_bx0]
    call vx_addr                ; ES:DI = the source
    mov si, di
    push ds
    mov ax, es
    mov ds, ax                  ; DS:SI = VRAM
    mov es, [cs:vx_dsel]
    mov di, vx_rowbuf           ; ES:DI = the bounce
    mov cx, [cs:vx_w]
    cmp byte [cs:vx_bpp], 8
    je .rb
    add cx, cx                  ; bytes a row
.rb:
    call vx_rowread
    pop ds
    pop bx
    mov ax, [vx_bx0]
    call vx_addr                ; ES:DI = the destination (BX kept)
    mov si, vx_rowbuf           ; DS:SI = the bounce (DS = CS)
    mov cx, [vx_w]
    cmp byte [vx_bpp], 8
    je .wb
    add cx, cx                  ; bytes a row
.wb:
    call vx_rowcopy
    add bx, [vx_sstep]
    dec word [vx_rows]
    jnz .row
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; VX_SPANS - gfx_spans: AX = the first row (clipped), [gfx_sp_seg]:[gfx_sp_si]
;            = [gfx_sp_n] records of x1, x2 (inclusive; x1 > x2 = an empty
;            row) through VXB_SPANS, [gfx_color] the ink
; -----------------------------------------------------------------------------
vxp_spans:
    mov bx, ax                  ; BX = y
    push ds
    mov ds, [vx_kseg]
    mov si, [cs:vx_p_spans]
    mov ax, [si]                ; the list's segment
    mov cx, [si+4]              ; rows
    mov si, [si+2]              ; ...and offset
    mov di, [cs:vx_p_color]
    mov dl, [di]
    pop ds
    mov [vx_bsrc], si
    mov [vx_bstride], ax        ; (the list's segment, banked here)
    mov [vx_rows], cx
    mov [vx_ink], dl
    jcxz .out
.row:
    push ds
    mov ds, [vx_bstride]
    mov si, [cs:vx_bsrc]
    lodsw
    mov cx, ax                  ; x1
    lodsw
    mov dx, ax                  ; x2
    pop ds
    mov [vx_bsrc], si
    cmp cx, dx
    jg .next                    ; empty
    or cx, cx
    jns .x1
    xor cx, cx
.x1:
    cmp dx, [vx_x]
    jl .x2
    mov dx, [vx_x]
    dec dx
.x2:
    cmp cx, dx
    jg .next
    sub dx, cx
    inc dx                      ; DX = pixels
    mov ax, cx
    mov cx, dx                  ; ...into CX BEFORE vx_addr, which spends DX
    call vx_addr                ; (on the window number: the width was that,
    mov al, [vx_ink]            ; until driver size pass 1) and keeps BX, CX
    call vx_rowfill
.next:
    inc bx
    dec word [vx_rows]
    jnz .row
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; VX_FONTRUN - font_run's planar arm, no region armed and not disabled: CX =
;              x, DX = y, ES:SI the ASCIIZ string, [font_rn_fg]/[font_rn_bg]
;              through VXB_FONTFG; the glyphs at [font_seg]:[font_base] +
;              (c - 32) * 8, an unprintable cell painted blank. A cell off
;              the display's edges is skipped and the run goes on (SPEC.md
;              6.1); [gfx_color] is left at the foreground, the contract's
;              "pen spent"
; -----------------------------------------------------------------------------
vxp_fontrun:
    push es                     ; the string's ES is the frame's, F_ES
    mov es, [vx_kseg]
    mov di, [vx_p_fontfg]
    mov ax, [es:di]             ; AL = fg, AH = bg
    mov [vx_ink], ax
    mov di, [vx_p_color]
    mov [es:di], al             ; the pen, spent
    mov di, [vx_p_font]
    mov ax, [es:di]             ; font_seg
    mov [vx_fseg], ax
    mov ax, [es:di+2]           ; font_base
    mov [vx_fbase], ax
    pop es
    mov ax, [vx_y]
    sub ax, 8
    cmp dx, ax
    ja .out                     ; the run's y does not fit (unsigned: < 0 too)
    mov [vx_bsrc], si           ; the string cursor
    mov bx, dx                  ; BX = y
    mov [vx_bx0], cx            ; ...and the cell's x, which the pixel loop's
                                ; count spends CX on
.cell:
    push ds
    mov ds, [bp+F_ES]
    mov si, [cs:vx_bsrc]
    lodsb
    pop ds
    mov [vx_bsrc], si
    or al, al
    jz .out
    mov ah, al
    mov ax, [vx_x]
    sub ax, 8
    cmp cx, ax
    ja .skip                    ; off either edge: the cell is not drawn
    push ds
    mov ds, [bp+F_ES]
    mov si, [cs:vx_bsrc]
    mov al, [si-1]              ; the character again
    pop ds
    sub al, VX_FONT_FIRST
    cmp al, VX_FONT_LAST - VX_FONT_FIRST
    jbe .glyph
    xor al, al                  ; unprintable: the blank cell, which is SPACE
.glyph:
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [vx_fbase]
    mov [vx_glyph], ax
    push cx
    mov dh, 8                   ; rows
.grow:
    mov ax, [vx_bx0]
    push dx                     ; (vx_addr keeps BX)
    call vx_addr
    pop dx
    push ds
    mov ds, [vx_fseg]
    mov si, [cs:vx_glyph]
    mov dl, [si]
    pop ds
    inc word [vx_glyph]
    mov cx, 8
.gpx:
    shl dl, 1
    mov al, [vx_ink+1]          ; bg
    jnc .gput
    mov al, [vx_ink]            ; fg
.gput:
    call vx_px
    loop .gpx
    inc bx
    dec dh
    jnz .grow
    sub bx, 8                   ; the run's y again
    pop cx
.skip:
    add cx, 8
    mov [vx_bx0], cx
    jmp .cell
.out:
    clc
    ret

; --- the DAC's first sixteen: the VGA's own sixteen colours, 6-bit guns ------
vx_dac16:
    db  0, 0, 0,   0, 0,42,   0,42, 0,   0,42,42
    db 42, 0, 0,  42, 0,42,  42,21, 0,  42,42,42
    db 21,21,21,  21,21,63,  21,63,21,  21,63,63
    db 63,21,21,  63,21,63,  63,63,21,  63,63,63

; =============================================================================
; STATE - zero-only at the end (OS88_STATE), the rest first
; =============================================================================
vx_tab:                         ; the attach answer (VXS_*)
    dw vx_prim                  ; VXS_ENT
    db 8                        ; VXS_BPP
    db 0
    dw 0                        ; VXS_X
    dw 0                        ; VXS_Y
    dw 0                        ; VXS_MODE
%if $ - vx_tab != VXS_SIZE
  %error "vx_tab has drifted from vxabi.inc's VXS_* layout"
%endif

vx_win      dw 0xFFFF           ; the window 4F05h last set (none)
vx_bpp      db 8                ; what the kernel is told: 8 or 16
vx_wsel     dw VX_WSEG          ; window A as the CPU loads it: A000h, or the
                                ; selector kern_mega hands VXV_PM
vx_dispi    db 0                ; 1: banked through the dispi port (vx_pm)
vx_onstk    db 0                ; 1: an int 10h is out on our stack (vx_int10)
vx_stktop   dw vx_rowbuf + 2048 ; ...whose top: the bounce while attach
                                ; probes, vx_stkend after it

; =============================================================================
; vx_once - THE ATTACH-ONLY CODE, and the int 10h stack that replaces it
; (driver size pass 1; XMEM.DRV's xm_once and USBMOUSE.DRV's um_buf shape)
;
; vx_attach and vx_offer run once per image - vx_entry admits an ATTACH only
; while [vx_nmodes] is 0, a success leaves it 1 or more and a refusal frees
; the image - and then vx_attok, outside the region, zeroes VX_STK bytes from
; here and points [vx_stktop] at their end: the stack every later int 10h
; hops onto (vx_int10). VX_STK is the 768 bytes of the probe's two buffers
; that stack used to be plus the 130 of the mode list's copy below them,
; which it could run into before reaching anything live - the same depth,
; and nothing live under it.
;
; While attach runs, the VBE info block is the zeroed tail of the region and
; the mode-information block and the list's copy are the foot of the
; scroll's bounce, whose top is the stack the probe's int 10h calls hop onto
; (1,662 bytes of it): the bounce holds nothing until a mode is set.
; =============================================================================
VX_STK      equ 898
vx_once:
; -----------------------------------------------------------------------------
; vx_attach - DRVV_ATTACH: is there a VBE, and which of its modes is ours?
; out: CF = 0 and SI = vx_tab (the VXS_* answer, in this segment); CF = 1 and
;      AL = DRVE_HW when there is no VBE 1.2+, or no mode this image accepts
; clobbers: everything but the segment registers it does not own
;
; Nothing is hooked and no mode is set - the kernel switches after it has
; armed the far pointer, and a refusal frees this image with nothing to undo.
; -----------------------------------------------------------------------------
vx_attach:
    push es
    push bx                     ; PRESERVE BX: the kernel reads the row through
                                ; it on return (SPEC.md 51.2), and the probe
                                ; below spends it
    mov [vx_want], cl           ; the class SYSTEM.CFG asks for (120.2.3),
                                ; one byte, 0 = Auto
    push ds
    pop es                      ; ES = ours for the BIOS's buffers
    mov di, vx_vbe
    mov word [di], 'VB'         ; the pre-signature a VBE 2.0 BIOS answers
    mov word [di+2], 'E2'       ; with the 512-byte form
    mov ax, VBE_INFO
    call vx_int10               ; on our own stack (vx_int10)
    cmp ax, VBE_OK
    jne .nohw
    mov si, vx_vbe              ; (no register but AX is trusted across it)
    cmp word [si+VI_SIG], 'VE'
    jne .nohw
    cmp word [si+VI_SIG+2], 'SA'
    jne .nohw
    cmp word [si+VI_VER], 0x0102
    jb .nohw                    ; 1.0 and 1.1: no direct-colour fields, no
                                ; reliable window attributes (SPEC.md 120.2.1)

    ; --- copy the mode list OUT before any 4F01h can overwrite it ------------
    ; (a 1.x BIOS may point it inside the very buffer just filled)
    push ds
    lds si, [si+VI_MODES]       ; DS:SI = the BIOS's list
    mov di, vx_list             ; ES:DI = our copy (ES = CS)
    mov cx, VX_LISTMAX
    cld
.lcopy:
    lodsw
    stosw
    cmp ax, 0xFFFF
    je .lend
    loop .lcopy
    mov ax, 0xFFFF              ; a list longer than we walk ends here
    stosw                       ; (the copy has VX_LISTMAX+1 words for this)
.lend:
    pop ds

    ; --- ask about each, and keep the ones this image can draw ---------------
    mov si, vx_list             ; ([vx_nmodes] is 0: vx_entry's guard)
.mode:
    lodsw
    cmp ax, 0xFFFF
    je .chosen
    mov cx, ax
    push si
    mov ax, VBE_MINFO
    mov di, vx_minfo
    call vx_int10
    cmp ax, VBE_OK
    jne .skip
    call vx_offer               ; CX = the mode: appended if acceptable
.skip:
    pop si                      ; (vx_offer spends SI on its copy)
    cmp byte [vx_nmodes], VX_NMODES
    jb .mode

    ; --- the class asked for, else Auto (SPEC.md 120.2.3) ------------------------
.chosen:
    cmp byte [vx_nmodes], 0
    jne vx_attok                ; out of the region, which it zeroes
.nohw:
    pop bx
    pop es
    mov al, DRVE_HW
    stc
    ret

; -----------------------------------------------------------------------------
; vx_offer - is the mode in vx_minfo one this image draws? Append it if so.
; in:  CX = the mode number, [vx_minfo] filled by 4F01h, ES = ours
; clobbers: ax, bx, cx, dx, si, di
; -----------------------------------------------------------------------------
vx_offer:
    mov ax, [vx_minfo+MI_ATTR]
    and al, 0x19                ; supported, colour, graphics
    cmp al, 0x19
    jne .no
    mov al, [vx_minfo+MI_WINA]
    and al, 0x05                ; window A exists and is writable
    cmp al, 0x05
    jne .no
    cmp word [vx_minfo+MI_SEGA], VX_WSEG
    jne .no
    cmp word [vx_minfo+MI_WSIZE], 64
    jne .no
    ; the granularity: a power of two, 1..64 KB, as the window shift BH =
    ; 6 - log2(granularity KB) - a 64KB page is 64/gran windows. The lowest
    ; set bit is shifted out at BH's count; a bit above it (ZF clear) is not
    ; a power of two, and none by bit 6 (BH below 0) is 0 or past 64
    mov ax, [vx_minfo+MI_GRAN]
    mov bh, 6
.g:
    shr ax, 1
    jc .gbit
    dec bh
    jns .g
.no:
    ret
.gbit:
    jnz .no                     ; not a power of two
    ; the depth and the model: 8 with packed pixel (4), 15 or 16 with direct
    ; colour (6), as one word each
    mov al, [vx_minfo+MI_BPP]
    mov ah, [vx_minfo+MI_MODEL]
    cmp ax, 0x0408
    je .depthok
    cmp ax, 0x060F
    je .depthok
    cmp ax, 0x0610
    jne .no
.depthok:
    mov dx, [vx_minfo+MI_XRES]  ; DX = the bytes a row needs (AL = the depth)
    cmp al, 8
    je .bpsl8
    add dx, dx                  ; two bytes a pixel
.bpsl8:
    ; the geometry
    mov ax, [vx_minfo+MI_XRES]
    test al, 7
    jnz .no                     ; a multiple of 8 (SPEC.md 11.94)
    sub ax, VX_XMIN
    cmp ax, VX_XMAX - VX_XMIN
    ja .no                      ; (unsigned: below VX_XMIN wraps above it)
    mov ax, [vx_minfo+MI_YRES]
    sub ax, VX_YMIN
    cmp ax, VX_YMAX - VX_YMIN
    ja .no
    mov ax, [vx_minfo+MI_BPSL]
    cmp ax, VX_BPSLMAX
    ja .no
    cmp ax, dx
    jb .no                      ; a stride shorter than a row
    ; --- accept: append the record, the two runs vx_minfo has in our order --
    mov al, VM_SIZE
    mul byte [vx_nmodes]
    add ax, vx_modes
    xchg ax, di                 ; DI = the record
    xchg ax, cx                 ; AX = the mode number
    cld
    stosw                       ; VM_MODE
    mov si, vx_minfo+MI_BPSL
    movsw                       ; VM_BPSL
    movsw                       ; VM_X
    movsw                       ; VM_Y
    mov al, [vx_minfo+MI_BPP]
    mov ah, bh
    stosw                       ; VM_BPP, VM_WSH
    mov si, vx_minfo+MI_RSIZE
    movsw                       ; VM_RSIZE, VM_RPOS
    movsw                       ; VM_GSIZE, VM_GPOS
    movsw                       ; VM_BSIZE, VM_BPOS
    inc byte [vx_nmodes]
    ret

vx_once_end:

    OS88_STATE
vx_vbe      times 512 db 0      ; 4F00h's buffer while attach runs (above)
            times VX_STK - ($ - vx_once) db 0
vx_stkend:
%if vx_stkend - vx_once != VX_STK || (VX_STK & 1)
  %error "vx_once's code and vx_vbe outgrew VX_STK"
%endif
vx_oss      dw 0                ; vx_int10's banked SS:SP and FLAGS
vx_osp      dw 0
vx_ofl      dw 0
vx_dsel     dw 0                ; OUR data segment, banked by vx_prim on every
                                ; entry: a body whose DS is the window or the
                                ; kernel stores through it, never through CS
vx_kseg     dw 0                ; the kernel's segment, from VXV_INIT
; the kernel's pointers, in VXB_* order from VXB_TABROW (vx_init copies them)
vx_p_tabrow dw 0
vx_p_ep     dw 0
vx_p_avail  dw 0
vx_p_color  dw 0
vx_p_xorm   dw 0
vx_p_pn     dw 0
vx_p_sub    dw 0
vx_p_font   dw 0
vx_p_clipr0 dw 0
vx_p_clipcm dw 0
vx_p_curtab dw 0
vx_p_curhot dw 0
vx_p_curpos dw 0
vx_p_icog   dw 0
vx_p_icop   dw 0
vx_p_icoxy  dw 0
vx_p_icoc   dw 0
vx_p_kind   dw 0
vx_p_pat    dw 0
vx_p_blit   dw 0
vx_p_b1     dw 0
vx_p_b1hm   dw 0
vx_p_scroll dw 0
vx_p_spans  dw 0
vx_p_fontfg dw 0
vx_p_cfgve  dw 0
vx_p_kret   dw 0                ; a kernel `ret`: where a tail hook goes back
vx_p_null   dw 0                ; ...and the kernel's draw-nothing entry
%if $ - vx_p_tabrow != VXB_SIZE - 2
  %error "the pointer bank has drifted from vxabi.inc's VXB_* layout"
%endif
vx_x        dw 0                ; the chosen mode's geometry
vx_y        dw 0
vx_bpsl     dw 0
vx_depth    db 0                ; the mode's own depth: 8, 15 or 16 } ONE WORD:
vx_wsh      db 0                ; ...and the window shift          } vx_publish
%if vx_wsh != vx_depth + 1 || VM_WSH != VM_BPP + 1
  %error "vx_publish copies VM_BPP and VM_WSH into vx_depth and vx_wsh as one word"
%endif
vx_cur      dw 0                ; -> the chosen record
vx_want     db 0                ; the class byte asked for (0 = Auto)
vx_nmodes_w:                    ; ...the count, as a word (the byte then a 0)
vx_nmodes   db 0
            db 0
vx_cursaved db 0
vx_one      db 0
vx_pcol     db 0
vx_tgt      dw 0
vx_ink      dw 0
vx_rows     dw 0
vx_w        dw 0
vx_buf      dw 0
vx_rs       dw 0
vx_sc1      dw 0
vx_fseg     dw 0
vx_glyph    dw 0
vx_iww      dw 0
vx_irows    dw 0
vx_ix       dw 0
vx_iy       dw 0
vx_ip       dw 0
vx_ip2      dw 0
vx_cx       dw 0
vx_cy       dw 0
vx_cw       dw 0
vx_ch       dw 0
vx_scx      dw 0
vx_scy      dw 0
vx_scw      dw 0
vx_sch      dw 0
vx_ctab     dw 0
vx_x1       dw 0
vx_bx0      dw 0                ; the S2 bodies' scratch
vx_bxs      dw 0
vx_bsrc     dw 0
vx_bstride  dw 0
vx_bnb      dw 0
vx_bmask    db 0
vx_b1adp    db 0
vx_b1hm     db 0
            db 0
vx_sstep    dw 0
vx_sdy      dw 0
vx_fbase    dw 0
vx_patbuf   times 8 db 0
vx_pal16    times 16 dw 0       ; the 16 desktop colours as RGB words (16bpp)
vx_cursave_b times CUR_GW * CUR_GH * 2 db 0
vx_modes    times VX_NMODES * VM_SIZE db 0
vx_rowbuf   times 2048 db 0     ; one row at the widest and deepest mode, the
                                ; scroll's bounce - and while attach runs,
vx_minfo    equ vx_rowbuf       ; 4F01h's 256 bytes at its foot,
vx_list     equ vx_rowbuf + 256 ; the mode list's copy over them (vx_once)
%if (vx_list + (VX_LISTMAX + 1) * 2) - vx_rowbuf > 2048 - 1024
  %error "the probe's buffers leave the attach's int 10h too little of the bounce"
%endif
    OS88_DRV_END
