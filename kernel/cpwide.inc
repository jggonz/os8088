; =============================================================================
; os8088 - cpwide.inc
;
; CPWIDE.DRV (SPEC.md 135): the wide Control Panel's second FACE, an on-demand
; module (SPEC.md 2.8, MOD_CPW) loaded BESIDE CTRL.DRV when the panel opens on
; a colour primary of at least 640x480. Prefix `cpw_`. kern_big, kern_emu and
; kern_mega only: kern_small never assembles a byte of SPEC.md 135.
;
; WHAT IS HERE AND WHAT IS NOT (SPEC.md 135.1, decision D2). CTRL.DRV keeps
; every setting, every state change and every compact page; this image draws
; the wide chrome - the grouped page list, the header strip, the status line -
; and HOSTS the compact pages in its page area by asking CTRL.DRV to paint and
; click them at an origin (CPE_GATE, SPEC.md 135.4). The shared widget kit of
; SPEC.md 135.5 is cpwkit.inc, %included at the foot of this file into the
; same image, so the wave-2 pages that join it draw with the same procs.
;
; HOW IT IS ENTERED. One far entry, cpw_ev, with the event in BP (CWV_*,
; cpwlay.inc): CTRL.DRV's far entries forward to it while [cp_wide] is 1
; (cp_wfwd), and its open and close paths call it for CWV_OPEN / CWV_CLOSE.
; DS = KERNEL_SEG on every entry, so the kernel's data is plain references,
; and kernel code is reached by far calls through cells and shims (cw_*,
; apic_*) or this image's own KGO hop (CPWC/CPWK, below).
;
; ITS STATE IS NOT IN ITS IMAGE. CPWIDE.DAT is read into ONE heap claim at
; open (SPEC.md 135.2), and that claim's first CPW_WSZ bytes are this image's
; working memory - the origin, the list's entries, the status line's cache,
; the icon staging buffer - with the file after them. So every handler runs
; with ES = that claim, its state is `[es:CPW_W_*]` and needs no CSDO on
; kern_mega, the icon blitter's source is ES:SI as OSAPI_GFX_BLIT4 wants it,
; and the image's own bss is the one word naming the claim (cpw_dseg).
; =============================================================================

%ifdef KERN_BIG
%include "cpwfmt.inc"

CPW_NENT    equ 1               ; CPWIDE.DRV's mod_fp entries, in CPWFP: one,
                                ; cpw_ev, the event in BP (SPEC.md 135.4)

; --- the working area: the asset claim's first CPW_WSZ bytes ------------------
CPW_W_OX    equ 0               ; dw  the content box's left, SCREEN x
CPW_W_OY    equ 2               ; dw  ...its top
CPW_W_CW    equ 4               ; dw  ...its width (W_W - 2)
CPW_W_CH    equ 6               ; dw  ...its height (W_H - TITLE_H - 1)
CPW_W_WIN   equ 8               ; dw  the window, from the last window event
CPW_W_ES0   equ 10              ; dw  the caller's ES at this event, which the
                                ;     gate is called with (cpw_gate)
CPW_W_SKB   equ 12              ; dw  the status line's cache: free KB...
CPW_W_SLD   equ 14              ; db  ...drivers loaded...
CPW_W_STO   equ 15              ; db  ...of how many rows
CPW_W_NL    equ 16              ; db  list entries built
CPW_W_LSEL  equ 17              ; db  the record the header and page last drew
CPW_W_FULL  equ 18              ; db  1 inside a full list paint (cpw_lrow)
CPW_W_KIT   equ 20              ; 12 bytes: the kit's scratch (cpwkit.inc)
CPW_W_LIST  equ 32              ; the list's entries, CPW_LMAX x 4 bytes:
CPW_LMAX    equ 28              ;   +0 db 80h|heading or 0, +1 db the record,
                                ;   +2 dw the entry's top, content-relative
CPW_W_STR   equ CPW_W_LIST + CPW_LMAX * 4   ; 144: a composed line, 96 bytes
CPW_W_PAGE  equ CPW_W_STR + 96  ; 240: THE PAGE BLOCK, 80 bytes, the page
CPW_W_PAGEN equ 80              ;   SHOWING owns whole: each wide page lays its
                                ;   own map over all of it (SPEC.md 135.5.1),
                                ;   so leaving a page is losing its block - a
                                ;   page rebuilds it on a fresh entry, and what
                                ;   must outlive a visit lives in KEEP below
CPW_W_KEEP  equ CPW_W_PAGE + CPW_W_PAGEN    ; 320: bytes kept across visits,
CPW_W_DFLT  equ CPW_W_KEEP + 0  ; db  the Drivers page's filter (0 All, CWC_*)
CPW_W_DSEL  equ CPW_W_KEEP + 1  ; db  ...its selected drv_tab row, 0FFh none
CPW_W_DON   equ CPW_W_KEEP + 2  ; db  1 = the page block is the Drivers page's
                                ;     (cpw_page clears it for any other page)
CPW_W_KEEPN equ 16              ; (13 spare: a paragraph, so the file stays on
                                ;  one - below)
CPW_W_ICON  equ CPW_W_KEEP + CPW_W_KEEPN    ; 336: the icon staging buffer,
CPW_WSZ     equ CPW_W_ICON + CWD_I32_PIX    ; one 32x32; CPWIDE.DAT at 848
%if CPW_W_DON >= CPW_W_ICON || CPW_W_DFLT < CPW_W_PAGE + CPW_W_PAGEN
  %error "cpwide: the kept bytes must lie between the page block and the icons"
%endif
%if CPW_WSZ & 15                ; a PACKED read (PKGZ, SPEC.md 20.13.5) expands
  %error "cpwide: CPWIDE.DAT must start on a paragraph of the claim"
%endif                          ; at a paragraph (lz wants DI = 0, diskw.inc):
                                ; at 836 the open was refused, silently compact
CPW_DATMAX  equ 16384           ; the file's bound (SPEC.md 135.6)

; --- the page list (SPEC.md 135.3), content-relative --------------------------
CPW_LT0     equ 4               ; the first heading's top
CPW_HH      equ 13              ; a heading's height: 3 rows, its words, 2
CPW_HCH     equ 14              ; ...its words padded to this many characters
CPW_HG      equ 3               ; ...and the air under it
CPW_RH      equ 20              ; a row's pitch: a 19-row bar and one of air
CPW_GG      equ 6               ; the air between one group and the next
CPW_LIX     equ 6               ; a row's 16x16 icon x (its y is the top + 2)
CPW_LTX     equ 26              ; ...and its name's x (the top + 6)

; --- the header strip (SPEC.md 135.3), x from CPW_PX --------------------------
CPW_HIX     equ 8               ; the 32x32 icon, PX + 8
CPW_HIY     equ 5
CPW_HTX     equ 48              ; the title and the description, PX + 48
CPW_HTY     equ 10
CPW_HDY     equ 24
CPW_HRY     equ CPW_HDRH - 3    ; 41: the rule under it

; the hosted rect cp_page erases, content-relative (cp_page, ctrl.inc)
CPW_HW      equ CP_CW - CP_DIVX - 1 ; 229: x PX .. PX + 228
CPW_HB      equ CPW_BY + CP_CH  ; 176: its first row BELOW

; KGO_PAIR's sites for this image (kernel.asm): CPWK = a near `.text`
; routine, CPWC = a near `.cold` one, both through cpw_kgo / cpw_cgo below
%macro CPWK 1
    call cpw_kgo
    dw %1
%endmacro
%macro CPWC 1
    call cpw_cgo
    dw %1
%endmacro

; =============================================================================
; CPWIDE.DRV - the module image
; =============================================================================
section .modv
modv_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER, MOD_CPW         ; MOD_H_VER, MOD_H_ID
    dw BUILD_NUM, MOD_STAMP     ; MOD_H_BUILD, MOD_H_STAMP (SPEC.md 2.8.2)
    dw modv_end, CPW_NENT       ; MOD_H_IMG, MOD_H_NENT
    dw MODV_BSS                 ; MOD_H_SCR: the whole bss tail (cpw_dseg) -
                                ; KEPT at its drop, that word zeroed again
    dw cpw_ev

; -----------------------------------------------------------------------------
; cpw_ev - CPWIDE.DRV's one entry (SPEC.md 135.4), entered FAR
; in:  BP = CWV_*; the event's own registers (cpwlay.inc); DS = KERNEL_SEG
; out: CF (CWV_OPEN's answer; nothing for the rest); every register but BP
;      preserved - the forwarder in CTRL.DRV (cp_wfwd) owns BP
;
; Every event but CWV_OPEN finds the asset claim already there; one that does
; not (a stale forward after a refused open) is dropped. The handlers are
; near procs entered with ES = the claim and every input register as it came.
; -----------------------------------------------------------------------------
cpw_ev:
    push es
    push di
    push si
    push dx
    push cx
    push bx
    push ax
    mov di, sp                  ; [ss:di+12] = the caller's ES
    cmp bp, CWV_OPEN
    je .go
    cmp word [cs:cpw_dseg], 0
    je .out                     ; no face: nothing to do (CF = 0 from the cmp)
    mov es, [cs:cpw_dseg]
    push word [ss:di+12]
    pop word [es:CPW_W_ES0]
    cmp bp, CWV_WIN
    jae .go
    mov [es:CPW_W_WIN], si
.go:
    add bp, bp
    call [cs:bp + cpw_evt]
.out:
    pop ax
    pop bx
    pop cx
    pop dx
    pop si
    pop di
    pop es
    retf

cpw_evt:
    dw cpw_paint, cpw_click, cpw_up, cpw_drag, cpw_key
    dw cpw_tick, cpw_listev, cpw_close, cpw_open
%if ($ - cpw_evt) / 2 != CWV_N
  %error "cpwide: cpw_evt and the CWV_* events disagree"
%endif

; -----------------------------------------------------------------------------
; cpw_gate - call CTRL.DRV's CPE_GATE (SPEC.md 135.4) with the caller's ES
; in:  AH = CPG_*, the op's registers; ES = the claim
; out: the op's; ES = the claim again
; -----------------------------------------------------------------------------
cpw_gate:
    push es
    mov es, [es:CPW_W_ES0]
    call far [CPFP + CPE_GATE*4]
    pop es
    ret

; =============================================================================
; CWV_OPEN / CWV_CLOSE - the asset claim (SPEC.md 135.2)
; =============================================================================

; -----------------------------------------------------------------------------
; cpw_open - read CPWIDE.DAT into a claim of its own, and size the window
; in:  nothing (CTRL.DRV's open, before app_launch); DS = KERNEL_SEG
; out: CF = 0 the face is ready and cp_tpl holds the wide size; CF = 1 refused
;      and nothing is held - the caller drops this image and opens compact
;
; mod_need's own order (mod.inc): bank the user's volume, stand on the boot
; volume, size the file from its directory entry, claim it top-down (a
; module's data beside the module's image, pinned while the panel is open -
; tests/pins.txt), read it whole, put the volume back. The file is checked
; before anything trusts an offset in it: the magic, the version, its own
; size field, and every block inside the file.
; -----------------------------------------------------------------------------
cpw_open:
    CPWC drv_vol_bank
    CPWC drv_mounted
    jc .back
    mov si, cpw_datname         ; the name through DS: staged (cp_sbuf)
    call cpw_stage
    CPWC drv_find               ; AX = its size
    jc .back
    cmp ax, CWD_HDR
    jb .no
    cmp ax, CPW_DATMAX
    ja .no
    mov bp, ax                  ; BP = the file's bytes
    add ax, CPW_WSZ
    CPWC mem_bytes_gp_x         ; AX = paragraphs, whole sectors
    mov bx, MEM_K_MOD
    CPWC mem_claimp_hi_x        ; DX = the claim, top-down
    jc .no
    mov es, dx
    push dx
    mov bx, CPW_WSZ             ; ES:BX = where the file goes
    mov cx, bp
    xor dx, dx                  ; DX:CX = the capacity
    CPWC dskw_read_x            ; SI is still the staged name
    pop dx
    jc .free
    cmp ax, bp
    jne .free
    cmp word [es:CPW_WSZ + CWD_H_MAGIC], CWD_MAGIC
    jne .free
    cmp byte [es:CPW_WSZ + CWD_H_VER], CWD_VERSION
    jne .free
    cmp [es:CPW_WSZ + CWD_H_SIZE], bp
    jne .free
    cmp byte [es:CPW_WSZ + CWD_H_N16], CWI16_N
    jb .free
    cmp byte [es:CPW_WSZ + CWD_H_N32], CWI32_N
    jb .free
    mov ax, CWI16_N * CWD_I16_SZ    ; every block inside the file
    mov bx, [es:CPW_WSZ + CWD_H_OFF16]
    call .fits
    jc .free
    mov ax, CWI32_N * CWD_I32_SZ
    mov bx, [es:CPW_WSZ + CWD_H_OFF32]
    call .fits
    jc .free
    mov al, CWD_CAT_SZ
    mul byte [es:CPW_WSZ + CWD_H_NCAT]
    mov bx, [es:CPW_WSZ + CWD_H_OFFCAT]
    call .fits
    jc .free
    xor di, di                  ; the working area, zeroed
    mov cx, CPW_WSZ / 2
    xor ax, ax
    cld
    rep stosw
    CSDO mov [CSEG cpw_dseg], es
    mov ax, CPW_W1              ; the size (SPEC.md 135.2): 616x408 on a
    mov dx, CPW_H1              ; 640x480 primary, 760x520 from 800x600 up
    cmp word [vid_pw], 800
    jb .sz
    cmp word [vid_ph], 600
    jb .sz
    mov ax, CPW_W2
    mov dx, CPW_H2
.sz:
    mov [cp_tpl + 4], ax
    mov [cp_tpl + 6], dx
    neg ax                      ; ...centred on the primary
    add ax, [vid_pw]
    shr ax, 1
    mov [cp_tpl], ax
    neg dx
    add dx, [vid_ph]
    shr dx, 1
    cmp dx, MBAR_H
    jge .y
    mov dx, MBAR_H
.y:
    mov [cp_tpl + 2], dx
    clc
    jmp short .back
.free:
    mov bx, MEM_K_MOD           ; DX = the claim
    CPWC mem_free_x
.no:
    stc
.back:
    CPWC drv_vol_back           ; keeps the flags
    ret
; .fits - CF = 1 unless [BX, BX + AX) lies inside the file (BP bytes)
.fits:
    cmp bx, CWD_HDR
    jb .nf
    add ax, bx
    jc .nf
    cmp bp, ax                  ; CF = 1 when the block runs past the end
    ret
.nf:
    stc
    ret

; -----------------------------------------------------------------------------
; cpw_close - CWV_CLOSE: the asset claim goes back, and the compact template
; in:  ES = the claim. out: nothing
; -----------------------------------------------------------------------------
cpw_close:
    call cpv_quit               ; a Test... running reverts first (135.9.3)
    mov dx, es
    mov bx, MEM_K_MOD
    CPWC mem_free_x
    xor ax, ax
    CSDO mov [CSEG cpw_dseg], ax
    mov word [cp_tpl], CP_TX    ; the compact face's template back, so a
    mov word [cp_tpl + 2], CP_TY ; panel opened next on CGA (or after a
    mov word [cp_tpl + 4], CP_TW ; refusal) is the window SPEC.md 31.1 pins
    mov word [cp_tpl + 6], CP_TH
    ret

; -----------------------------------------------------------------------------
; cpw_stage - an image string at CS:SI into cp_sbuf (cp_stage's shape, this
;             image's copy). out: SI = cp_sbuf; every other register kept
; -----------------------------------------------------------------------------
cpw_stage:
    push ax
    push cx
    push di
    mov di, cp_sbuf
    mov cx, CP_SBUF - 1
.c:
    mov al, [cs:si]
    inc si
    mov [di], al
    inc di
    or al, al
    loopnz .c
    mov byte [di], 0
    mov si, cp_sbuf
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; CWV_PAINT and the chrome (SPEC.md 135.3)
; =============================================================================

; -----------------------------------------------------------------------------
; cpw_org - the window's content box into the working area
; in:  [es:CPW_W_WIN]. out: nothing; every register kept
; -----------------------------------------------------------------------------
cpw_org:
    push ax
    push bx
    push dx
    mov bx, [es:CPW_W_WIN]
    call KERNEL_SEG:cw_wm_content   ; AX/DX = the content's top left
    mov [es:CPW_W_OX], ax
    mov [es:CPW_W_OY], dx
    mov ax, [bx+W_W]
    dec ax
    dec ax
    mov [es:CPW_W_CW], ax
    mov ax, [bx+W_H]
    sub ax, TITLE_H + 1
    mov [es:CPW_W_CH], ax
    pop dx
    pop bx
    pop ax
    ret

; cpw_sty - the status line's top, content-relative, in AX
cpw_sty:
    mov ax, [es:CPW_W_CH]
    sub ax, CPW_STH
    ret

; -----------------------------------------------------------------------------
; cpw_paint - CWV_PAINT: the whole window
; in:  SI = the window (gfx lock held). out: nothing
; -----------------------------------------------------------------------------
cpw_paint:
    call cpw_org
    call cpw_big
    jb cpw_small
    call cpw_lbuild
    call cpw_list
    call cpw_sty                ; the rule between the list and the page
    mov dx, ax
    dec dx
    dec dx
    mov byte [gfx_color], CBLACK
    mov ax, CPW_LW
    xor bx, bx
    mov cx, ax
    call cpw_fill
    mov al, 1
    call cpw_hdr
    call cpw_page
    mov al, 1
    jmp cpw_status

; cpw_big - CF = 0 the window holds the face; CF = 1 it is in the SMALL state
; (cpw_small), where nothing but its line - and the Display page's Keep group
; during a Test, which that page guards itself (cpv_big) - is drawn or
; pressed (SPEC.md 135.2). Reads the content box cpw_org last wrote; keeps all
cpw_big:
    cmp word [es:CPW_W_CW], CPW_W1 - 2
    jb .r
    cmp word [es:CPW_W_CH], CPW_H1 - TITLE_H - 1
.r:
    ret

; -----------------------------------------------------------------------------
; cpw_small - the window is smaller than the face (a vid_switch while it was
; open, SPEC.md 135.2): ground and one line, and nothing that would draw past
; its edge. Closing and opening the panel again chooses the face again.
; -----------------------------------------------------------------------------
cpw_small:
    cmp byte [cp_sel], CP_IVID  ; the Display page in a test: its Keep group
    jne .line                   ; (SPEC.md 135.9.3)
    call cpv_small
    jnc .r
.line:
    mov byte [gfx_color], CWHITE
    xor ax, ax
    xor bx, bx
    mov cx, [es:CPW_W_CW]
    dec cx
    mov dx, [es:CPW_W_CH]
    dec dx
    call cpw_fill
    mov cx, 8
    mov dx, cx
    mov si, cpw_s_small
    mov ax, (CWHITE << 8) | CBLACK
    xor bl, bl
    jmp cpw_run
.r:
    ret

; -----------------------------------------------------------------------------
; cpw_lbuild - lay the page list out: its entries into [es:CPW_W_LIST]
; in:  nothing. out: [es:CPW_W_NL]; every register kept
;
; The groups are cpw_lay's, in the image; a record the compact face hides
; (SPEC.md 31.10.1 - [cp_hide]) is hidden here too, and the driver-owned pages
; (SPEC.md 31.9) go where cpw_lay says, under HARDWARE. A row that would run
; into the status line is not laid out (SPEC.md 135.3: the list does not
; scroll; it holds 13 static rows and six driver pages at 640x480).
; -----------------------------------------------------------------------------
cpw_lbuild:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call cpw_sty
    sub ax, CPW_RH + 1
    mov bp, ax                  ; BP = the lowest top a row may have
    mov di, CPW_W_LIST
    mov dx, CPW_LT0             ; DX = the next entry's top
    xor cx, cx                  ; CL = entries, CH = 1 once one is laid
    mov si, cpw_lay
.next:
    mov al, [cs:si]
    inc si
    cmp al, 0xFF
    je .done
    cmp al, 0xFE
    je .drv
    test al, 0x80
    jz .rec
    or ch, ch                   ; --- a heading: the gap above all but the first
    jz .h1
    add dx, CPW_GG
.h1:
    mov ah, 0
    call .put
    add dx, CPW_HH + CPW_HG
    jmp short .next
.rec:
    cmp al, 8                   ; --- a static row, unless the compact face
    jae .show                   ; hides it (records 8+ never are)
    push cx
    mov cl, al
    mov bl, 1
    shl bl, cl
    pop cx
    test [cp_hide], bl
    jnz .next
.show:
    call .row
    jmp short .next
.drv:
    push ax                     ; --- the driver-owned pages: records
    mov al, 0xFF                ; CP_ITEMS + k for each class that publishes
    CPWC drv_cp_class_x         ; one (cp_nitems' count, SPEC.md 31.9)
    mov bl, al                  ; BL = how many
    pop ax
    mov al, CP_ITEMS
.dl:
    or bl, bl
    jz .next
    call .row
    inc al
    dec bl
    jmp short .dl
.done:
    mov [es:CPW_W_NL], cl
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.row:                           ; AL = the record: one row, if it fits
    cmp dx, bp
    jg .full
    mov ah, al
    mov al, 0
    call .put
    mov al, ah
    add dx, CPW_RH
.full:
    ret
.put:                           ; AL = 80h|heading or 0, AH = the record
    cmp cl, CPW_LMAX
    jae .pr
    mov [es:di], ax
    mov [es:di+2], dx
    add di, 4
    inc cl
    mov ch, 1
.pr:
    ret

; the static layout: 80h|n is heading n, a byte below 80h a record, FEh the
; driver pages and FFh the end (SPEC.md 135.3)
cpw_lay:
    db 0x80, 0, CP_ITIME, CP_IKEY, CP_IFDD
    db 0x81, CP_IDRV, CP_IVID, CP_ISND, 0xFE
    db 0x82, CP_ITHM, CP_IDOCK, 0xFF
cpw_heads:  dw cpw_s_h0, cpw_s_h1, cpw_s_h2

; -----------------------------------------------------------------------------
; cpw_list - the whole page list: its ground, headings, bars, icons and names
; in:  the entries built. out: nothing
;
; The ground is laid in the spans BETWEEN the headings and the selected bar,
; so no pixel of either is written twice (CLAUDE.md, Performance rule 2);
; then each row's icon and name go down on what is under them.
; -----------------------------------------------------------------------------
cpw_list:
    xor bx, bx                  ; BX = the first row not yet grounded
    mov si, CPW_W_LIST
    mov cl, [es:CPW_W_NL]
    xor ch, ch
    jcxz .tail
.g:
    mov dx, [es:si+2]           ; DX = this entry's top
    mov al, [es:si]
    test al, 0x80
    jnz .head
    mov al, [es:si+1]
    cmp al, [cp_sel]
    jne .gn
    call .ground                ; the selected bar: the ground above it,
    mov byte [gfx_color], CBLUE ; then the bar
    push dx
    mov bx, dx
    add dx, CPW_RH - 2
    call .band
    pop bx
    add bx, CPW_RH - 1
    jmp short .gn
.head:                          ; a heading: dark grey words on the ground,
    add dx, 3                   ; its text rows laid as the run and its two
    call .ground                ; margins, the rows round them the ground's
    push si
    push cx
    and al, 0x7F
    cbw
    xchg bx, ax
    add bx, bx
    mov si, [cs:bx + cpw_heads]
    mov cl, CPW_HCH
    call cpw_pad                ; ES:CPW_W_STR = it, padded
    mov byte [gfx_color], CWHITE
    xor ax, ax
    mov bx, dx
    mov cx, 3
    add dx, 7
    call cpw_fill               ; the margin left of the run...
    mov ax, 4 + CPW_HCH * 8
    mov cx, CPW_LW - 1
    call cpw_fill               ; ...and right of it
    mov cx, 4
    mov dx, bx
    mov si, CPW_W_STR
    mov ax, (CWHITE << 8) | CDGRAY
    mov bl, 2
    call cpw_run
    pop cx
    pop si
    mov bx, [es:si+2]
    add bx, CPW_HH - 2          ; the ground resumes under the words
.gn:
    add si, 4
    loop .g
.tail:
    call cpw_sty                ; ...and the last span, to the rule above
    mov dx, ax                  ; the status line
    dec dx
    dec dx
    mov ax, bx
    cmp ax, dx
    jg .rows
    mov byte [gfx_color], CWHITE
    mov bx, ax
    call .band
.rows:
    mov byte [es:CPW_W_FULL], 1 ; --- and every row's icon and name
    mov si, CPW_W_LIST
    mov cl, [es:CPW_W_NL]
    xor ch, ch
    jcxz .out
.r:
    test byte [es:si], 0x80
    jnz .rn
    call cpw_lrow
.rn:
    add si, 4
    loop .r
.out:
    mov byte [es:CPW_W_FULL], 0
    ret
.ground:                        ; BX = the first ungrounded row, DX = the
    cmp bx, dx                  ; next thing's top: white between
    jge .gr
    push dx
    mov byte [gfx_color], CWHITE
    dec dx
    call .band
    pop dx
.gr:
    ret
.band:                          ; the list's full width, rows BX..DX
    push ax
    push cx
    xor ax, ax
    mov cx, CPW_LW - 1
    call cpw_fill
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpw_lrow - one row of the list: its bar (unless a full paint laid it), its
; icon and its name
; in:  ES:SI = the entry. out: nothing; every register kept
; -----------------------------------------------------------------------------
cpw_lrow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [es:si+2]           ; DX = the row's top
    mov al, [es:si+1]           ; AL = its record
    mov di, (CWHITE << 8) | CBLUE   ; DI = ink : ground, selected...
    cmp al, [cp_sel]
    je .bar
    mov di, (CBLACK << 8) | CWHITE  ; ...or not
    cmp byte [es:CPW_W_FULL], 0
    jne .icon                   ; the ground is down already
.bar:
    cmp byte [es:CPW_W_FULL], 0
    jne .icon                   ; (a full paint laid the selected bar too)
    push ax
    push dx
    mov ax, di
    mov [gfx_color], al
    mov bx, dx
    add dx, CPW_RH - 2
    xor ax, ax
    mov cx, CPW_LW - 1
    call cpw_fill
    pop dx
    pop ax
.icon:
    push ax
    call cpw_recicon            ; AL = its 16x16 icon
    mov bx, di                  ; BL = the ground
    mov cx, CPW_LIX
    push dx
    add dx, 2
    xor ah, ah
    call cpw_icon
    pop dx
    pop ax
    mov ah, CPG_NAME            ; ...and its name, staged by CTRL.DRV
    call cpw_gate               ; SI = cp_sbuf
    mov cx, CPW_LTX
    add dx, 6
    mov ax, di
    xchg al, ah                 ; AL = ink, AH = ground
    mov bl, 1
    call cpw_run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpw_recicon - a record's icons and description
; in:  AL = a record
; out: AL = its 16x16 icon, AH = its 32x32 one, SI -> its one-line
;      description in the image (CS); nothing else
;
; A static page's are cpw_rtab's row; a driver's page (SPEC.md 31.9) is named
; by its CLASS - a disk's, a network's, or the generic gear.
; -----------------------------------------------------------------------------
cpw_recicon:
    push bx
    cmp al, CP_ITEMS
    jae .drv
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [cs:bx + cpw_rtab]
    mov si, [cs:bx + cpw_rtab + 2]
    pop bx
    ret
.drv:
    sub al, CP_ITEMS
    CPWC drv_cp_class_x         ; AL = its class, CF = 1 gone
    mov si, cpw_s_ddrv
    mov bx, (CWI32_GEAR << 8) | CWI16_GEAR
    jc .have
    cmp al, DRVC_DISK
    jne .nd
    mov bx, (CWI32_HDD << 8) | CWI16_HDD
.nd:
    cmp al, DRVC_NET
    jne .nn
    mov bx, (CWI32_NET << 8) | CWI16_NET
.nn:
    cmp al, 11                  ; DRVC_JOY (SPEC.md 136; the literal until os88api.inc has it)
    jne .have
    mov bx, (CWI32_JOY << 8) | CWI16_JOY
.have:
    xchg ax, bx
    pop bx
    ret

; one row per static record, in record order: 16x16, 32x32, description
%macro CPWREC 4                 ; record, icon16, icon32, description
  %if %1 != ($ - cpw_rtab) / 4
    %error "cpwide: cpw_rtab is out of record order at %1"
  %endif
    db %2, %3
    dw %4
%endmacro
cpw_rtab:
    CPWREC 0,         CWI16_SCHED,  CWI32_SCHED,  cpw_s_dsch
    CPWREC CP_ITIME,  CWI16_TIME,   CWI32_TIME,   cpw_s_dtim
    CPWREC CP_IDRV,   CWI16_DRV,    CWI32_DRV,    cpw_s_ddrv2
    CPWREC CP_IVID,   CWI16_DISP,   CWI32_DISP,   cpw_s_dvid
    CPWREC CP_ISND,   CWI16_SOUND,  CWI32_SOUND,  cpw_s_dsnd
    CPWREC CP_ITHM,   CWI16_THEME,  CWI32_THEME,  cpw_s_dthm
    CPWREC CP_IDOCK,  CWI16_DOCK,   CWI32_DOCK,   cpw_s_ddck
    CPWREC CP_IFDD,   CWI16_FLOPPY, CWI32_FLOPPY, cpw_s_dfdd
    CPWREC CP_IKEY,   CWI16_KEYB,   CWI32_KEYB,   cpw_s_dkey
%if ($ - cpw_rtab) / 4 != CP_ITEMS
  %error "cpwide: cpw_rtab needs one row per static record (CP_ITEMS)"
%endif

; -----------------------------------------------------------------------------
; cpw_hdr - the header strip: the page's 32x32 icon, its title, its line
; in:  AL = 1 a full paint (the strip's ground and rule first), 0 a page change
; out: nothing
;
; On a page change nothing is erased: the icon is opaque, and each line is a
; font_run padded with its own paper to the right edge (cpw_runp), so the new
; words replace the old in one pass (CLAUDE.md, Performance rules 1-2).
; -----------------------------------------------------------------------------
cpw_hdr:
    or al, al
    jz .text
    mov byte [gfx_color], CWHITE    ; --- the strip's ground, the rule and the
    mov ax, CPW_PX                  ; two rows of air under it
    xor bx, bx
    mov cx, [es:CPW_W_CW]
    dec cx
    mov dx, CPW_HRY - 1
    call cpw_fill
    mov byte [gfx_color], CBLACK
    mov bx, CPW_HRY
    mov dx, bx
    call cpw_fill
    mov byte [gfx_color], CWHITE
    inc bx
    mov dx, CPW_HDRH - 1
    call cpw_fill
.text:
    mov al, [cp_sel]
    mov [es:CPW_W_LSEL], al
    push ax
    call cpw_recicon            ; AH = the 32x32 icon, SI = the line
    mov al, ah
    mov ah, 1
    mov bl, CWHITE
    mov cx, CPW_PX + CPW_HIX
    mov dx, CPW_HIY
    call cpw_icon
    mov di, si                  ; the line, for after the title
    pop ax
    mov ah, CPG_NAME
    call cpw_gate               ; SI = the title, staged
    mov cx, CPW_PX + CPW_HTX
    mov dx, CPW_HTY
    mov ax, (CWHITE << 8) | CBLACK
    mov bl, 1
    call cpw_runp
    mov si, di
    mov cx, CPW_PX + CPW_HTX
    mov dx, CPW_HDY
    mov ax, (CWHITE << 8) | CBLACK
    xor bl, bl
    jmp cpw_runp

; -----------------------------------------------------------------------------
; cpw_page - the page area: a wide page's own paint, or the HOSTED compact one
; in:  nothing. out: nothing
;
; The Drivers list is cpd_paint's (SPEC.md 135.8). Hosted (SPEC.md 135.3): cp_page's own erase covers the compact pane's rect,
; CPW_HW x CP_CH from the page area's top left, so this lays the ground only
; to its right and below it, and CTRL.DRV paints the page at the hosted origin
; through the gate. Every page is hosted until a wave-2 page takes its record
; (SPEC.md 135.9, 135.11).
; -----------------------------------------------------------------------------
cpw_page:
    cmp byte [cp_sel], CP_IVID  ; the Display page draws itself (135.9)...
    je .vid
    mov byte [es:CPV_LIVE], 0   ; ...and builds itself afresh after another
    call cpw_dwant              ; CF = 0: the Drivers list - its own page,
    jnc cpd_paint               ; which asks CPW_D_ON whether this is a fresh
    mov byte [es:CPW_D_ON], 0   ; entry (135.5.1): any other page clears it
    cmp byte [cp_sel], CP_ITHM
    je cpw_thm_paint            ; the Theme page is drawn natively (135.11)
    mov byte [gfx_color], CWHITE
    mov ax, CPW_PX + CPW_HW     ; right of the hosted rect
    mov bx, CPW_BY
    mov cx, [es:CPW_W_CW]
    dec cx
    mov dx, CPW_HB - 1
    call cpw_fill
    call cpw_under              ; ...and the ground under it
    mov bx, [es:CPW_W_WIN]
    mov ah, CPG_HPAINT
    jmp cpw_gate
.vid:
    mov byte [es:CPW_D_ON], 0
    jmp cpv_page

; cpw_under - the ground under the hosted rect, to the status line's rule
cpw_under:
    call cpw_sty
    mov dx, ax
    dec dx
    dec dx
    mov byte [gfx_color], CWHITE
    mov ax, CPW_PX
    mov bx, CPW_HB
    mov cx, [es:CPW_W_CW]
    dec cx
    jmp cpw_fill

; -----------------------------------------------------------------------------
; cpw_dsync - after a hosted page acted: the Drivers page comes back when its
; Ports view went Back (SPEC.md 31.15) - the hosted pane is replaced whole
; in:  nothing. out: nothing
; -----------------------------------------------------------------------------
cpw_dsync:
    cmp byte [es:CPW_D_ON], 0
    jne .r
    call cpw_dwant              ; CF = 0 wanted
    jc .r
    jmp cpd_paint
.r:
    ret

; -----------------------------------------------------------------------------
; cpw_status - the status line (SPEC.md 135.3): free memory, drivers loaded of
; how many, and that the settings are saved at the close
; in:  AL = 1 a full paint (always drawn, its rule and margins too), 0 only if
;      a number changed. out: nothing
; -----------------------------------------------------------------------------
cpw_status:
    push ax
    push bx
    call COLD_SEG:mmf_osapi_mem_avail   ; AX = the heap's largest claim, KB
    mov cx, ax
    pop bx
    pop ax
    xor dx, dx                  ; DL = loaded, DH = rows
    mov bx, drv_tab
    mov ah, DRV_MAX
.d:
    cmp word [bx+DRVR_FILE], 0
    je .dn
    inc dh
    cmp word [bx+DRVR_SEG], 0
    je .dn
    inc dl
.dn:
    add bx, DRVR_SIZE
    dec ah
    jnz .d
    or al, al
    jnz .draw
    cmp cx, [es:CPW_W_SKB]
    jne .draw
    cmp dx, [es:CPW_W_SLD]
    je .out
.draw:
    mov [es:CPW_W_SKB], cx
    mov [es:CPW_W_SLD], dx
    push dx
    mov di, CPW_W_STR           ; compose: "412K free - 3 of 14 drivers loaded
    mov ax, cx                  ; - settings saved when the panel closes"
    call cpw_utoa
    mov si, cpw_s_st1
    call cpw_strcat
    pop dx
    push dx
    mov al, dl
    xor ah, ah
    call cpw_utoa
    mov si, cpw_s_st2
    call cpw_strcat
    pop dx
    mov al, dh
    xor ah, ah
    call cpw_utoa
    mov si, cpw_s_st3
    call cpw_strcat
    call cpw_sty
    mov bx, ax                  ; BX = the band's top
    mov byte [gfx_color], CBLACK    ; the rule above it
    push bx
    dec bx
    xor ax, ax
    mov cx, [es:CPW_W_CW]
    dec cx
    mov dx, bx
    call cpw_fill
    pop bx
    mov byte [gfx_color], CLGRAY    ; the band's rows above and below the text
    mov dx, bx
    inc dx
    inc dx
    call cpw_fill
    push bx
    add bx, 11
    mov dx, [es:CPW_W_CH]
    dec dx
    call cpw_fill
    pop bx
    add bx, 3
    xor ax, ax                  ; ...and its left margin
    mov cx, 3
    mov dx, bx
    add dx, 7
    call cpw_fill
    mov cx, 4
    mov dx, bx
    mov si, CPW_W_STR
    mov ax, (CLGRAY << 8) | CBLACK
    mov bl, 2
    call cpw_runp
.out:
    ret

; cpw_utoa - AX as decimal at ES:DI, DI advanced; AX, CX, DX spent
cpw_utoa:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push bx
    mov bx, 10
    xor cx, cx
.dv:
    xor dx, dx
    div bx
    add dl, '0'
    push dx
    inc cx
    or ax, ax
    jnz .dv
.em:
    pop ax
    stosb
    loop .em
    mov byte [es:di], 0
    pop bx
    ret

; cpw_strcat - the image string at CS:SI onto ES:DI, NUL kept; DI at the NUL
cpw_strcat:
    push ax
.c:
    mov al, [cs:si]
    inc si
    mov [es:di], al
    or al, al
    jz .z
    inc di
    jmp short .c
.z:
    pop ax
    ret

; cpw_pad - the image string at CS:SI into ES:CPW_W_STR, padded with spaces
; to CL characters (cut at CL). Keeps every register but CL
cpw_pad:
    push ax
    push si
    push di
    mov di, CPW_W_STR
.c:
    or cl, cl
    jz .z
    mov al, [cs:si]
    or al, al
    jz .sp
    inc si
    jmp short .put
.sp:
    mov al, ' '
.put:
    mov [es:di], al
    inc di
    dec cl
    jmp short .c
.z:
    mov byte [es:di], 0
    pop di
    pop si
    pop ax
    ret

; =============================================================================
; The edges (SPEC.md 135.3): the list's are this image's, the page area's go
; to the hosted page through the gate
; =============================================================================

; -----------------------------------------------------------------------------
; cpw_click - CWV_CLICK: CX/DX = the SCREEN point, SI = the window
; -----------------------------------------------------------------------------
cpw_click:
    call cpw_org
    mov ax, cx
    sub ax, [es:CPW_W_OX]
    mov bx, dx
    sub bx, [es:CPW_W_OY]
    cmp byte [cp_sel], CP_IVID  ; the Display page takes the whole window's
    je cpv_click                ; press: its test holds the list (135.9.3)
    call cpw_big                ; the small state: nothing to press (135.2)
    jb .none
    cmp ax, CPW_LW
    jl .list
    cmp bx, CPW_BY
    jl .none                    ; the header: nothing to press
    push ax
    call cpw_sty
    dec ax
    cmp bx, ax
    pop ax
    jge .none                   ; the status line
    cmp byte [cp_sel], CP_ITHM
    jne .nthm
    jmp cpw_thm_click           ; the Theme page's own press (135.11)
.nthm:
    cmp byte [es:CPW_D_ON], 0   ; the Drivers page's own press (135.8)
    je .host
    jmp cpd_click
.host:
    mov ah, CPG_HCLICK          ; the page: the hosted page's own press
    call cpw_gate
    jmp cpw_dsync
.list:
    or ax, ax
    jl .none                    ; the border
    mov si, CPW_W_LIST          ; which row?
    mov cl, [es:CPW_W_NL]
    xor ch, ch
    jcxz .none
.l:
    test byte [es:si], 0x80
    jnz .ln
    mov ax, [es:si+2]
    cmp bx, ax
    jl .ln
    add ax, CPW_RH - 1
    cmp bx, ax
    jl .hit
.ln:
    add si, 4
    loop .l
.none:
    ret
.hit:
    mov al, [es:si+1]
    cmp al, [cp_sel]
    je .none                    ; already showing
    ; fall into cpw_select

; -----------------------------------------------------------------------------
; cpw_select - make record AL the page, redrawing what changed (SPEC.md
;              135.3): the row losing the bar, the row gaining it, the header
;              strip and the page area - never the chrome around them
; in:  AL = a record; the entries built. out: nothing
; -----------------------------------------------------------------------------
cpw_select:
    mov ah, [cp_sel]
    mov [cp_sel], al
%ifdef OS88_DRIVERS
    mov byte [cp_dview], 0      ; another page: Drivers comes back as its list
%endif
    xchg al, ah
    call cpw_lfind              ; the row the bar leaves...
    jc .new
    call cpw_lrow
.new:
    mov al, ah
    call cpw_lfind              ; ...and the one it arrives at
    jc .hdr
    call cpw_lrow
.hdr:
    xor al, al
    call cpw_hdr
    jmp cpw_page

; cpw_lfind - record AL -> ES:SI = its list entry; CF = 1 not shown
cpw_lfind:
    push cx
    mov si, CPW_W_LIST
    mov cl, [es:CPW_W_NL]
    xor ch, ch
    jcxz .no
.l:
    test byte [es:si], 0x80
    jnz .n
    cmp [es:si+1], al
    je .yes
.n:
    add si, 4
    loop .l
.no:
    stc
.yes:                           ; (`cmp` equal leaves CF = 0)
    pop cx
    ret

; -----------------------------------------------------------------------------
; cpw_up / cpw_drag - the release and the drag (SPEC.md 13.8.3) belong to
; whatever took the press: a wide control's arm is CPW_ID0 and up (the kit's,
; cpwkit.inc), and anything else is the hosted page's own, so its body runs
; through the gate with the edge's registers
; -----------------------------------------------------------------------------
cpw_up:
    mov ah, CPG_HUP
    jmp short cpw_edge
cpw_drag:
    mov ah, CPG_HDRAG
cpw_edge:
    call cpw_org
    cmp byte [cp_sel], CP_IVID  ; (the Display page guards its own, cpv_up)
    je .ok
    call cpw_big                ; the small state: no edge acts or draws
    jb .r
.ok:
    push ax
    CPWC os88ui_armed           ; AX = the armed control, 0 = none
    cmp ax, CPW_ID0
    pop ax
    jae .wide
    call cpw_gate
    jmp cpw_dsync
.wide:
    cmp ah, CPG_HUP
    jne .r
    cmp byte [cp_sel], CP_IVID  ; the Display page's controls (135.9.5)
    je cpv_up
    CPWC os88ui_fire            ; a wide control's release: disarmed, and the
    cmp byte [cp_sel], CP_ITHM  ; page's to act on - the Theme page's buttons
    je cpw_thm_up               ; (135.11), else the Drivers page's (135.8)
    jmp cpd_rel
.r:
    ret

; cpw_key - CWV_KEY: AX = the key, SI = the window. The Drivers page's, or the
; hosted page's.
cpw_key:
    cmp byte [cp_sel], CP_IVID  ; the Display page's: Esc reverts a test
    je cpv_key
    call cpw_org
    call cpw_big                ; the small state: no key acts or draws
    jb cpw_tick.r
    cmp byte [es:CPW_D_ON], 0
    jne cpd_key                 ; the Drivers page: Up and Down
    mov bx, ax
    mov ah, CPG_HKEY
    jmp cpw_gate

; cpw_tick - CWV_TICK: the hosted Date/Time page's clock, when it is showing
cpw_tick:
    cmp byte [cp_sel], CP_IVID  ; a Test... counting down (SPEC.md 135.9.4)
    je cpv_tick
    cmp byte [cp_sel], CP_ITIME
    jne .r
    call cpw_org                ; (the last window event's window)
    call cpw_big                ; the small state: no clock drawn over it
    jb .r
    mov ah, CPG_HTICK
    jmp cpw_gate
.r:
    ret

; -----------------------------------------------------------------------------
; cpw_listev - CWV_LIST: a driver came up or went down (cp_list's call in
; CTRL.DRV), so the list's membership may have moved: the list, and the
; status line's numbers. A selection that went with it (cp_drv_gone_x) brings
; the header and the page.
; -----------------------------------------------------------------------------
cpw_listev:
    mov al, KIND_CTRL
    push di
    KDOOR inst_find_kind        ; DI = the panel's record
    mov bx, [di+I_WIN]
    pop di
    jc .r
    mov [es:CPW_W_WIN], bx
    call cpw_org
    call cpw_big                ; the small state: nothing drawn (135.2); the
    jb .r                       ; next full paint builds the list anyway
    call cpw_lbuild
    call cpw_list
    xor al, al
    call cpw_status
    mov al, [cp_sel]
    cmp al, [es:CPW_W_LSEL]
    je .same
    xor al, al
    call cpw_hdr
    jmp cpw_page
.same:
    cmp byte [es:CPW_D_ON], 0   ; a driver came up or went by itself: the
    je .r                       ; Drivers page's rows and its DETAILS
    call cpd_build
    mov bx, CPD_TB
    call cpk_table
    jmp cpd_det
.r:
    ret

; =============================================================================
; The Drivers page (SPEC.md 135.8) is cpw_drv.inc, below
; =============================================================================
; cpw_dwant - CF = 0 the catalog belongs on the screen: the Drivers page
; showing its list. Keeps all
cpw_dwant:
    cmp byte [cp_sel], CP_IDRV
    jne .no
    cmp byte [cp_dview], 0
    jne .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cpw_cat - a drv_tab row's record in CPWIDE.DAT's catalog, by its FILE name
; in:  AL = the row. out: CF = 0 and ES:DI = the record; CF = 1 none (DI
;      spent). Every other register kept
; -----------------------------------------------------------------------------
cpw_cat:
    push ax
    push bx
    push cx
    push si
    xor ah, ah
    mov bx, ax
    mov cl, 4
    shl bx, cl
    mov si, [bx + drv_tab + DRVR_FILE]  ; DS:SI = its name
    mov di, [es:CPW_WSZ + CWD_H_OFFCAT]
    add di, CPW_WSZ
    mov cl, [es:CPW_WSZ + CWD_H_NCAT]
    xor ch, ch
    jcxz .no
.r:
    xor bx, bx
.c:
    mov al, [si + bx]
    cmp al, [es:di + bx]
    jne .n
    or al, al
    jz .yes
    inc bx
    cmp bx, 12
    jb .c
.yes:
    clc
    jmp short .out
.n:
    add di, CWD_CAT_SZ
    loop .r
.no:
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

%include "cpw_drv.inc"

; --- the image's strings ------------------------------------------------------
cpw_datname: db 'CPWIDE.DAT', 0
cpw_s_h0:   db 'SYSTEM', 0
cpw_s_h1:   db 'HARDWARE', 0
cpw_s_h2:   db 'APPEARANCE', 0
cpw_s_dsch: db 'How programs share the processor.', 0
cpw_s_dtim: db 'Set the clock, and how the menu bar shows it.', 0
cpw_s_ddrv2: db 'Browse and configure device drivers.', 0
cpw_s_dvid: db 'Choose the display adapter and its mode.', 0
cpw_s_dsnd: db 'Where tones go, and a test of the card.', 0
cpw_s_dthm: db 'Choose colours and desktop effects.', 0
cpw_s_ddck: db 'Where the dock sits, and whether it hides.', 0
cpw_s_dfdd: db 'The floppy drives this machine has.', 0
cpw_s_dkey: db 'The keyboard layout, and a place to try it.', 0
cpw_s_ddrv: db 'Settings this driver publishes.', 0
cpw_s_small: db 'Close and reopen the Control Panel.', 0
cpw_s_st1:  db 'K free - ', 0
cpw_s_st2:  db ' of ', 0
cpw_s_st3:  db ' drivers loaded - settings saved when the panel closes', 0

%include "cpw_thm.inc"          ; the Theme page (SPEC.md 135.11), same image

; KGO_PAIR's hop for this image (kernel.asm): cpw_kgo / cpw_cgo
KGO_PAIR cpw

%include "cpwkit.inc"           ; the widget kit (SPEC.md 135.5), same image
%include "cpw_vid.inc"          ; the Display page (SPEC.md 135.9), same image

section .modvb                  ; ...and its bss: the claim's one name
cpw_bss:
cpw_dseg:   resw 1              ; the asset claim (0 = no face). Written at
                                ; open and close through CSDO; read [cs:]
section .text
%endif
