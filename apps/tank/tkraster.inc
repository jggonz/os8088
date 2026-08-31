; =============================================================================
; os8088 - apps/tank/tkraster.inc
;
; THE RASTER (SPEC.md 85.3). Three backends behind four entries - tk_r_setup,
; tk_r_begin, tk_seg, tk_r_end - and NOT ONE kernel drawing slot among them,
; because SPEC.md 53.7 makes every one of them illegal the moment fsx_mode
; returns. What is here is what the kernel would have had to lend and cannot.
;
; ONE STRIDE FOR ALL THREE, and it is not a coincidence worth passing over: a
; CGA 320x200x4 row is 80 bytes, a Hercules 640-pixel 1bpp row is 80 bytes,
; and a Mode X 320-pixel PLANE row is 80 bytes. So `add di, 80` steps a row on
; every backend, and the walk below is one body with three plots rather than
; three walks.
;
; THE DIRTY SPAN IS THE WHOLE DESIGN. Every backend keeps, per row of the
; viewport, the first and last BYTE the frame touched - one byte each, so a
; span pair is a word and a whole screen's worth is 480 bytes. The walk
; maintains it as it goes: a shallow line marks twice per ROW (the run inside
; a row is monotone, so its ends are its first and last pixel) and a steep one
; marks twice per pixel, which is what a steep line's row IS. Clearing the
; next frame is then a rep stos over exactly those runs, and on the two
; adapters that cannot page-flip the blit is over exactly those runs too.
;
; WHAT EACH BACKEND DOES WITH THE FRAME ONCE IT IS BUILT:
;
;   MODEX  three pages, and it uses two. The frame is drawn straight into the
;          page nobody is looking at and the CRTC start address is moved at
;          the retrace. Nothing is copied, and the erase is invisible because
;          it happens on the hidden page.
;   CGA    mode 4 is 16,000 bytes of a CGA's 16,384: there is no second page
;          and there never will be. So the frame is composed in a 16,000-byte
;          RAM shadow and the dirty spans are blitted. Every byte of VRAM is
;          written at most ONCE per frame, and only where the picture changed.
;   HERC   the same shadow path. A Hercules HAS a second page, but the kernel
;          leaves 3BFh's page bit clear (SPEC.md 39.6) and the app enabling it
;          behind the kernel's back is a poor trade for a copy this small -
;          the 640x200 viewport is 16,000 bytes, the same shadow as CGA's.
; =============================================================================

; --- per-backend geometry, indexed by TKB_* - 1 -------------------------------
;      vx,  vy,  vw,  vh,  sclx, scly
tk_vptab:
    dw   0,   0, 320, 240,  277, 277     ; MODEX: square pixels, so scly = sclx
    dw   0,   0, 320, 200,  277, 231     ; CGA:   4:3 in 320x200
    dw  40,  74, 640, 200,  554, 357     ; HERC:  a 640x200 box in 720x348
; scly is not a taste: for the same horizontal field of view on every adapter,
; scly = sclx * A * vh / vw with A the viewport's PHYSICAL aspect. That is
; what keeps the enemy tank the same shape on all three.

; -----------------------------------------------------------------------------
; tk_r_setup - adopt a backend for the length of one bracket
; in:  [tk_back] set, [tk_fsi] filled by OSAPI_FSX_MODE
; out: CF=1 if the raster cannot be had (no shadow); every register clobbered
; -----------------------------------------------------------------------------
tk_r_setup:
    mov al, [tk_back]
    dec al
    mov ah, 12
    mul ah
    mov word [tk_spanp], tk_spans0
    mov word [tk_spanp+2], tk_spans1
    mov word [tk_inktab], tk_inkmodex
    cmp byte [tk_back], TKB_MODEX
    je .inkok
    mov word [tk_inktab], tk_inkcga
    cmp byte [tk_back], TKB_CGA
    je .inkok
    mov word [tk_inktab], tk_inkherc
.inkok:
    mov si, tk_vptab
    add si, ax
    mov ax, [si]
    mov [tk_vx], ax
    mov ax, [si+2]
    mov [tk_vy], ax
    mov ax, [si+4]
    mov [tk_vw], ax
    mov ax, [si+6]
    mov [tk_vh], ax
    mov ax, [si+8]
    mov [tk_sclx], ax
    mov ax, [si+10]
    mov [tk_scly], ax

    mov ax, [tk_vw]                 ; the projection centre, and the horizon
    shr ax, 1                       ; with it: level camera, so a point at
    mov [tk_vcx], ax                ; infinity lands exactly on vcy
    mov ax, [tk_vh]
    mov bx, 55                      ; 55% down, which is where the 1983 port
    mul bx                          ; puts its horizon
    mov bx, 100
    div bx
    mov [tk_vcy], ax

    mov ax, [tk_scly]               ; the ridge is at infinity, so its height
    mov bl, 3                       ; is a fixed number of pixels and scales
    div bl                          ; with the vertical scale and nothing else
    xor ah, ah
    mov [tk_ridgek], ax

    call tk_hud_layout

    ; --- the target: two VRAM pages, or one RAM shadow --------------------
    cmp byte [tk_back], TKB_MODEX
    jne .shadow
    mov ax, [tk_fsi+FSI_SEG]
    mov [tk_tseg], ax
    mov word [tk_page0], 0
    mov word [tk_page1], 19200      ; 240 rows x 80 bytes a plane
    mov byte [tk_npage], 2
    call tk_modex_pal
    clc
    ret
.shadow:
    mov byte [tk_npage], 1
    mov dx, [tk_shseg]              ; ...AND ONCE FOR THE INSTANCE, NOT ONCE
    or dx, dx                       ; PER BRACKET. This routine runs at every
    jnz .haveshadow                 ; Play, and a claim taken there and never
                                    ; given back is 16KB a game: eight rounds
                                    ; on a 1bpp machine - which is the only
                                    ; kind that takes this path, Mode X having
                                    ; two VRAM pages - and the ninth is refused
                                    ; for the life of the app, on the machines
                                    ; with the least heap to lose. The claim is
                                    ; not movable (nothing declares it so), so
                                    ; the segment is still good next time
    mov ax, TK_SHKB
    call OSAPI_MEM_CLAIM            ; 16KB, and the kernel frees it with the
    jc .noshadow                    ; instance (SPEC.md 50.3) - no teardown
    mov [tk_shseg], dx              ; hook of our own, which is what makes the
.haveshadow:                        ; reuse above the whole of the lifetime
    mov [tk_tseg], dx
    mov word [tk_page0], 0
    mov word [tk_page1], 0
    cmp byte [tk_back], TKB_CGA
    jne .ok
    call tk_cga_pal
.ok:
    clc
    ret
.noshadow:
    stc
    ret

; -----------------------------------------------------------------------------
; tk_cga_pal - CGA palette 1, high intensity: cyan / magenta / white on black
;
; The 1983 port's own colours, and the reason the three logical inks in
; tank.asm are named for what they mean: this register IS the mapping.
; Inside a foreign mode the video hardware is the app's (SPEC.md 53.7), and
; 3D9h is the colour-select register the BIOS mode set leaves on palette 0.
; -----------------------------------------------------------------------------
tk_cga_pal:
    mov dx, 0x3D9
    mov al, 0x30                    ; bit 5 palette 1, bit 4 intensity,
    out dx, al                      ; background/border black
    ret

; -----------------------------------------------------------------------------
; tk_modex_pal - the same three colours in the DAC, so one game looks like one
;                game on both colour adapters
; -----------------------------------------------------------------------------
tk_modex_pal:
    push si
    mov si, tk_dactab
    mov cx, TKI_NINK
    xor bl, bl
.e:
    mov dx, 0x3C8
    mov al, bl
    out dx, al
    inc dx
    lodsb
    out dx, al
    lodsb
    out dx, al
    lodsb
    out dx, al
    inc bl
    loop .e
    pop si
    ret

; The DAC, one row per TKI_*. It is a VECTOR DISPLAY'S palette rather than a
; picture's: three hues, each with a depth ramp, and nothing saturated except
; the two things that must be seen first - the gunsight and a shell.
tk_dactab:                          ; 6-bit DAC values
    db  0,  0,  0                   ;  0 the ground
    db  0, 58, 58                   ;  1 scenery, near   - cyan
    db  0, 40, 42                   ;  2 scenery, mid
    db  0, 25, 28                   ;  3 scenery, far
    db 56, 10, 52                   ;  4 the panel       - magenta
    db 63, 63, 63                   ;  5 the gunsight    - white
    db 20, 30, 50                   ;  6 the mountains   - steel
    db 30, 44, 60                   ;  7 the horizon     - pale
    db 63, 42, 10                   ;  8 a tank, near    - amber
    db 48, 30,  6                   ;  9 a tank, mid
    db 32, 19,  4                   ; 10 a tank, far
    db 63, 60, 24                   ; 11 a shell         - bright
    db 34,  6, 32                   ; 12 the dial's rim  - dim magenta

; ...and what each MEANING is worth on the two adapters that cannot carry it.
; A row is indexed by the TKI_*, so index 0 is never used.
; The last entry is TKI_MASK and is not a colour: it is EVERY BIT OF ONE PIXEL
; on the adapter, which is what the erase walk needs in AL to clear one. Mode X
; stores a zero byte and never reads it.
tk_inkcga:                          ; palette 1: 1 cyan, 2 magenta, 3 white
    db 0, 1, 1, 1, 2, 3, 1, 1, 1, 1, 1, 3, 2, 3
tk_inkherc:                         ; one bit, and the contrast comes from
    db 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
tk_inkmodex:                        ; the DAC above, one for one
    db 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0

; -----------------------------------------------------------------------------
; tk_devrows - the device offset of every viewport row, once per bracket
;
; The two shadow backends blit row by row and both rasters are BANKED - CGA
; interleaves two ways at 0x2000, Hercules four - so the offset of row r+1 is
; not the offset of row r plus anything. A 480-byte table turns the blit's
; inner loop into a lodsw.
; -----------------------------------------------------------------------------
tk_devrows:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, tk_devoff
    xor bx, bx                      ; BX = r
.row:
    mov ax, bx
    add ax, [tk_vy]                 ; AX = the device row
    cmp byte [tk_back], TKB_HERC
    je .herc
    mov dx, ax                      ; CGA: bank = y & 1, 80 bytes a row
    and dx, 1
    push dx                         ; ...and `mul` writes DX, which is where
    shr ax, 1                       ; the bank was: without this every ODD
    mov cx, 80                      ; row lands on the EVEN row's address and
    mul cx                          ; half the picture is overwritten by the
    pop dx                          ; other half - dashes on every line
    jmp short .bank
.herc:
    mov dx, ax                      ; HERC: bank = y & 3, 90 bytes a row
    and dx, 3
    push dx
    mov cl, 2
    shr ax, cl
    mov cx, 90
    mul cx
    pop dx
    add ax, 5                       ; ...and the viewport starts at x = 40
.bank:
    mov cl, 5                       ; bank * 0x2000
    shl dx, cl
    mov cl, 8
    shl dx, cl
    add ax, dx
    mov [di], ax
    add di, 2
    inc bx
    cmp bx, [tk_vh]
    jb .row
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_r_begin - open a frame: pick the target, clear what the last frame drew
;              on it, and arm an empty span set
; clobbers everything
; -----------------------------------------------------------------------------
tk_r_begin:
    mov al, [tk_par]                ; the frame's parity picks BOTH the page
    and al, 1                       ; and the span set, which is what makes a
    xor ah, ah                      ; two-page flip need no third array
    mov bx, ax
    shl bx, 1
    mov si, [tk_spanp + bx]
    mov [tk_spcur], si
    cmp byte [tk_back], TKB_MODEX
    jne .shadow
    ; --- page flip: the set we are about to overwrite describes THIS page ---
    mov [tk_spprv], si
    mov cx, [tk_page0]
    or al, al
    jz .p0
    mov cx, [tk_page1]
.p0:
    mov [tk_tbase], cx
    mov dx, 0x3C4                   ; every plane, for the clear: a Mode X
    mov ax, 0x0F02                  ; byte is four pixels and the erase wants
    out dx, ax                      ; all four
    jmp short .clear
.shadow:
    ; --- one shadow: the OTHER set is the frame now standing in it ---------
    xor bx, 2
    mov si, [tk_spanp + bx]
    mov [tk_spprv], si
    mov word [tk_tbase], 0
.clear:
    call tk_clearspans
    mov di, [tk_spcur]              ; arm the new set empty: lo = FF, hi = 00
    push ds
    pop es
    mov cx, [tk_vh]
    mov ax, 0x00FF
    cld
    rep stosw
    ret

; -----------------------------------------------------------------------------
; tk_clearspans - zero every run [tk_spprv] recorded, in the current target
;
; Rounded out to WORDS: rep stosw moves a byte in about 7 clocks against
; stosb's 10 on an 8088, and the two bytes that rounding adds are cheaper
; than the ones it saves on every row wider than about six.
; -----------------------------------------------------------------------------
tk_clearspans:
    mov es, [tk_tseg]
    mov si, [tk_spprv]
    mov di, [tk_tbase]
    mov bp, [tk_vh]
    cld
.row:
    mov al, [si]                    ; lo
    mov ah, [si+1]                  ; hi
    cmp al, ah
    ja .next
    and al, 0xFE                    ; even..odd, so the run is whole words
    or ah, 1
    mov bl, ah
    sub bl, al
    shr bl, 1
    mov cl, bl
    xor ch, ch
    inc cx                          ; CX = words
    mov bl, al
    xor bh, bh
    push di
    add di, bx
    xor ax, ax
    rep stosw
    pop di
.next:
    add si, 2
    add di, 80
    dec bp
    jnz .row
    ret

; -----------------------------------------------------------------------------
; tk_seg - ONE screen-space segment: clip it to the viewport and walk it
; in:  AX = x1, BX = y1, CX = x2, DX = y2, viewport-relative, any magnitude
;      up to +-4000 (tk_project clamps to that so the clipper's own products
;      stay inside a 32-bit imul); [tk_ink] = the logical colour
; out: nothing; preserves every register
;
; ENDPOINT CLIPPING IS LEGAL HERE and it is worth saying why, because SPEC.md
; 5.6.3 refuses to do it in the kernel: clipping an endpoint moves the
; Bresenham lattice, so a line DRAWN clipped and ERASED unclipped leaves a
; scatter behind. Nothing here erases a line. The frame is erased by its
; recorded SPANS, which contain whatever lattice the walk actually laid, so
; the walk owes the next frame no promises at all.
; -----------------------------------------------------------------------------
tk_seg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [tk_cx1], ax
    mov [tk_cy1], bx
    mov [tk_cx2], cx
    mov [tk_cy2], dx
    ; --- the common case is a segment wholly inside, and it was paying two
    ;     tk_outcode CALLS to find that out. Eight compares instead.
    mov si, [tk_vw]
    mov di, [tk_vh]
    or ax, ax
    js .slow
    cmp ax, si
    jge .slow
    or bx, bx
    js .slow
    cmp bx, di
    jge .slow
    or cx, cx
    js .slow
    cmp cx, si
    jge .slow
    or dx, dx
    js .slow
    cmp dx, di
    jl .accept
.slow:
    mov byte [tk_ctry], 8           ; THE CAP IS LOAD-BEARING. Cohen-Sutherland
                                    ; terminates in exact arithmetic; in
                                    ; integers the interpolation TRUNCATES, so
                                    ; an endpoint pulled onto one edge can come
                                    ; to rest outside another and the pair can
                                    ; cycle. Four clips is the most a correct
                                    ; run needs; the eighth drops the segment,
                                    ; which costs one line of one frame and
                                    ; cannot cost the machine
.again:
    dec byte [tk_ctry]
    jz .out
    mov ax, [tk_cx1]
    mov bx, [tk_cy1]
    call tk_outcode
    mov cl, al                      ; CL = outcode 1
    mov ax, [tk_cx2]
    mov bx, [tk_cy2]
    call tk_outcode
    mov ch, al                      ; CH = outcode 2
    mov al, cl
    or al, ch
    jz .accept
    mov al, cl
    and al, ch
    jnz .out                        ; both outside one edge: gone
    xor si, si                      ; SI = 0 -> move endpoint 1
    mov al, cl
    or al, al
    jnz .pick
    mov si, 4                       ; ...else endpoint 2
    mov al, ch
.pick:
    test al, 8
    jnz .bot
    test al, 4
    jnz .top
    test al, 2
    jnz .right
    ; --- left: x = 0 --------------------------------------------------------
    xor bp, bp
    call tk_clip_x
    jmp short .again
.right:
    mov bp, [tk_vw]
    dec bp
    call tk_clip_x
    jmp short .again
.top:
    xor bp, bp
    call tk_clip_y
    jmp short .again
.bot:
    mov bp, [tk_vh]
    dec bp
    call tk_clip_y
    jmp short .again

.accept:
    mov ax, [tk_cx1]
    mov bx, [tk_cy1]
    mov cx, [tk_cx2]
    mov dx, [tk_cy2]
    call tk_lprep
    jc .out
    call [tk_lineproc]
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_outcode - in AX = x, BX = y; out AL = the code. Preserves BX, CX, DX.
tk_outcode:
    push dx
    xor dl, dl
    or ax, ax
    jns .r
    or dl, 1
    jmp short .v
.r:
    cmp ax, [tk_vw]
    jl .v
    or dl, 2
.v:
    or bx, bx
    jns .b
    or dl, 4
    jmp short .d
.b:
    cmp bx, [tk_vh]
    jl .d
    or dl, 8
.d:
    mov al, dl
    pop dx
    ret

; tk_clip_x - move endpoint SI (0 or 4) onto the vertical line x = BP
; y' = y1 + (y2-y1)*(BP-x1)/(x2-x1)
tk_clip_x:
    push si
    mov ax, [tk_cy2]
    sub ax, [tk_cy1]
    mov bx, bp
    sub bx, [tk_cx1]
    imul bx                         ; DX:AX = (y2-y1)*(BP-x1)
    mov bx, [tk_cx2]
    sub bx, [tk_cx1]
    idiv bx
    add ax, [tk_cy1]
    pop si
    mov [tk_cx1 + si], bp
    mov [tk_cy1 + si], ax
    ret

; tk_clip_y - move endpoint SI onto the horizontal line y = BP
tk_clip_y:
    push si
    mov ax, [tk_cx2]
    sub ax, [tk_cx1]
    mov bx, bp
    sub bx, [tk_cy1]
    imul bx
    mov bx, [tk_cy2]
    sub bx, [tk_cy1]
    idiv bx
    add ax, [tk_cx1]
    pop si
    mov [tk_cx1 + si], ax
    mov [tk_cy1 + si], bp
    ret

; -----------------------------------------------------------------------------
; tk_lprep - one line's walk parameters, shared by all three backends
; in:  AX/BX = x1/y1, CX/DX = x2/y2, all inside the viewport
; out: tk_lx/tk_ly = where the walk starts, tk_err/tk_e1/tk_e2/tk_cnt,
;      tk_ystep (+-80), tk_sistep (+-2), tk_xdir, tk_steep. CF=0 always.
;
; SHALLOW LINES NORMALISE X INCREASING and steep ones normalise Y INCREASING,
; which is what puts each walk's RARE step on the branchy side: a shallow
; line's y moves seldom, so it can afford a memory-resident step, and a steep
; line's x moves seldom, so it can afford a direction test. The frequent step
; in both is a register add with no test in it at all.
; -----------------------------------------------------------------------------
tk_lprep:
    mov si, cx                      ; SI = |dx|
    sub si, ax
    jge .dxp
    neg si
.dxp:
    mov di, dx                      ; DI = |dy|
    sub di, bx
    jge .dyp
    neg di
.dyp:
    cmp si, di
    jb .steep

    ; ---- shallow: x increasing --------------------------------------------
    mov byte [tk_steep], 0
    cmp ax, cx
    jbe .noswap
    xchg ax, cx
    xchg bx, dx
.noswap:
    mov [tk_lx], ax
    mov [tk_ly], bx
    mov word [tk_ystep], 80
    mov word [tk_sistep], 2
    cmp dx, bx
    jae .yd
    mov word [tk_ystep], -80
    mov word [tk_sistep], -2
.yd:
    mov ax, di                      ; e1 = 2|dy|
    shl ax, 1
    mov [tk_e1], ax
    sub ax, si                      ; err = 2|dy| - |dx|
    mov [tk_err], ax
    mov ax, si
    shl ax, 1
    mov [tk_e2], ax                 ; e2 = 2|dx|
    inc si
    mov [tk_cnt], si
    clc
    ret

    ; ---- steep: y increasing ----------------------------------------------
.steep:
    mov byte [tk_steep], 1
    cmp bx, dx
    jbe .noswap2
    xchg ax, cx
    xchg bx, dx
.noswap2:
    mov [tk_lx], ax
    mov [tk_ly], bx
    mov byte [tk_xdir], 0
    cmp cx, ax
    jae .xd
    mov byte [tk_xdir], 1
.xd:
    mov ax, si                      ; e1 = 2|dx|
    shl ax, 1
    mov [tk_e1], ax
    sub ax, di                      ; err = 2|dx| - |dy|
    mov [tk_err], ax
    mov ax, di
    shl ax, 1
    mov [tk_e2], ax                 ; e2 = 2|dy|
    inc di
    mov [tk_cnt], di
    clc
    ret

; =============================================================================
; The walk itself - one body, three plots (SPEC.md 85.3.2)
; =============================================================================

; --- mark this row's first / last touched byte --------------------------------
%macro TK_MLO 0
    cmp [si], dl
    jbe %%o
    mov [si], dl
%%o:
%endmacro

%macro TK_MHI 0
    cmp [si+1], dl
    jae %%o
    mov [si+1], dl
%%o:
%endmacro

; --- lay one pixel ------------------------------------------------------------
; The two shadow backends OR, and may: the shadow was cleared to 0 by the span
; pass, so there is nothing under the ink to preserve, and where two inks
; cross they add - which on a vector display is what crossing beams do
; anyway. Mode X cannot read cheaply and does not need to: it stores.
;
; The second parameter is the ERASE form (SPEC.md 85.8.1), and it exists
; because an OR cannot take ink away: on a 1bpp adapter black is the ABSENCE
; of a bit, so a halo drawn under the GAME OVER lettering has to clear what
; the scene put there. The walk is otherwise identical - same recurrence, same
; pixels - and the caller reaches it by swapping [tk_lineproc], so the drawing
; path pays nothing for its existence.
%macro TK_PLOT 2
  %if %1 == 3
    %if %2
    mov byte [es:di], 0
    %else
    mov [es:di], al
    %endif
  %else
    %if %2
    not al                          ; AL is the PIXEL's mask here, not an ink
    and [es:di], al                 ; pattern - see TKI_MASK
    not al
    %else
    or [es:di], al
    %endif
  %endif
%endmacro

; --- x + 1, the frequent step of a shallow line -------------------------------
%macro TK_XFWD 1
  %if %1 == 1                       ; 1bpp: 8 pixels to a byte
    shr al, 1
    jnz %%o
    mov al, ah
    inc di
    inc dl
%%o:
  %elif %1 == 2                     ; 2bpp: 4 pixels, 2 bits each
    shr al, 1
    shr al, 1
    jnz %%o
    mov al, ah
    inc di
    inc dl
%%o:
  %else                             ; Mode X: the next pixel is the next PLANE
    shl ah, 1                       ; ...and a map mask is FOUR bits, so this
    cmp ah, 16                      ; wraps at 16 and not at 256: `rol` here
    jne %%same                      ; selects no plane for half of every byte
    mov ah, 1
    inc di
    inc dl
%%same:
    push dx
    push ax
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    pop ax
    pop dx
  %endif
%endmacro

; --- x +- 1, the rare step of a steep line ------------------------------------
%macro TK_XSTEP 1
    cmp byte [tk_xdir], 0
    jne %%back
  %if %1 == 3
    shl ah, 1
    cmp ah, 16
    jne %%setf
    mov ah, 1
    inc di
    inc dl
%%setf:
    jmp short %%mask
  %else
    %if %1 == 1
    shr al, 1
    %else
    shr al, 1
    shr al, 1
    %endif
    jnz %%done
    mov al, [tk_pat0]
    inc di
    inc dl
    jmp short %%done
  %endif
%%back:
  %if %1 == 3
    shr ah, 1
    jnz %%mask
    mov ah, 8
    dec di
    dec dl
%%mask:
    push dx
    push ax
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    pop ax
    pop dx
  %else
    %if %1 == 1
    shl al, 1
    %else
    shl al, 1
    shl al, 1
    %endif
    jnz %%done
    mov al, [tk_pat3]
    dec di
    dec dl
  %endif
%%done:
%endmacro

; --- the body -----------------------------------------------------------------
%macro TK_LINEBODY 2
  %if %1 == 1
    %assign %%K 3
  %else
    %assign %%K 2
  %endif
    ; DI = tbase + 80*ly + (lx >> K); DL = that byte's index within the row
    mov ax, [tk_ly]
    mov cl, 4
    shl ax, cl
    mov di, ax
    shl ax, 1
    shl ax, 1
    add di, ax
    add di, [tk_tbase]
    mov ax, [tk_lx]
    mov bx, ax
    mov cl, %%K
    shr bx, cl
    mov dl, bl
    add di, bx
    ; SI = the span pair for this row
    mov si, [tk_ly]
    shl si, 1
    add si, [tk_spcur]
    ; AL/AH = the pixel pattern and its wrap value
  %if %1 == 1
    and al, 7
    mov cl, al
    mov al, 0x80
    shr al, cl
    mov ah, 0x80
  %elif %1 == 2
    and al, 3
    mov cl, 3
    sub cl, al
    shl cl, 1
    mov al, [tk_ink8]
    shl al, cl
    mov ah, [tk_ink8]
    mov cl, 6
    shl ah, cl
  %else
    and al, 3
    mov cl, al
    mov ah, 1
    shl ah, cl
    mov al, [tk_ink8]
    push dx
    push ax
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    pop ax
    pop dx
  %endif
    mov es, [tk_tseg]
    mov bx, [tk_err]
    mov cx, [tk_cnt]
    mov bp, [tk_e1]                 ; the error step is the ONLY thing added
    cld                             ; every pixel, so it is the one that gets
    cmp byte [tk_steep], 0          ; the register
    jne %%steep

    ; ---- shallow: one run a row, so its ends are its first and last pixel --
    TK_MLO
%%spx:
    TK_PLOT %1, %2
    or bx, bx
    jle %%noy
    TK_MHI
    add di, [tk_ystep]
    add si, [tk_sistep]
    sub bx, [tk_e2]
    TK_MLO
%%noy:
    add bx, bp
    TK_XFWD %1
    loop %%spx
    ; THE LAST STEP HAS ALREADY HAPPENED. TK_XFWD runs after the final plot,
    ; so if that pixel was the last in its byte, DL now names the byte AFTER
    ; the row - and marking it puts a span end one past the row's 80. The
    ; clear then wipes the next row's first byte and the blit copies it to the
    ; device, which on a CGA lands on a row about to be redrawn and is
    ; invisible, and on a HERCULES lands in the 10 bytes of margin either side
    ; of the 640-pixel viewport, where nothing ever erases it: reported from
    ; the field as stale ink collecting down the right-hand edge.
    cmp dl, TK_LASTB
    jbe %%last
    mov dl, TK_LASTB
%%last:
    TK_MHI
    ret

    ; ---- steep: a row IS a pixel, so both ends are the same byte -----------
%%steep:
    TK_PLOT %1, %2
    TK_MLO
    TK_MHI
    add di, 80
    add si, 2
    or bx, bx
    jle %%nox
    TK_XSTEP %1
    sub bx, [tk_e2]
%%nox:
    add bx, bp
    loop %%steep
    ret
%endmacro

; in: tk_lprep's output and [tk_ink8]; ES and every register clobbered
tk_line_herc:
    TK_LINEBODY 1, 0
tk_line_cga:
    TK_LINEBODY 2, 0
tk_line_modex:
    TK_LINEBODY 3, 0

; ...and the same three walks taking ink AWAY. Reached only by swapping
; [tk_lineproc], and always after tk_setink TKI_MASK, which is the ink whose
; device value is every bit of one pixel rather than a colour: the walk then
; builds the right mask in AL and TK_PLOT's erase form ANDs its complement.
tk_eline_herc:
    TK_LINEBODY 1, 1
tk_eline_cga:
    TK_LINEBODY 2, 1
tk_eline_modex:
    TK_LINEBODY 3, 1

; -----------------------------------------------------------------------------
; tk_setink - adopt a logical ink (TKI_*) for the walks that follow
; in:  AL = TKI_*.  preserves every register
;
; Hercules has no colours, so all three inks are the one lit bit there and the
; game's own contrast comes from what it draws rather than from what colour it
; draws it in (SPEC.md 39.4's rule, taken at the source).
; -----------------------------------------------------------------------------
tk_setink:
    push ax
    push bx
    push cx
    mov [tk_ink], al                ; the MEANING, for anyone who asks...
    xor ah, ah
    mov bx, ax
    add bx, [tk_inktab]             ; ...and the live backend's own value
    mov al, [bx]
    mov [tk_ink8], al
    cmp byte [tk_back], TKB_HERC
    jne .colour
    mov byte [tk_pat0], 0x80        ; leftmost pixel of the next byte...
    mov byte [tk_pat3], 0x01        ; ...and rightmost of the previous
    mov byte [tk_inkpat], 0xFF      ; ...and a whole byte of it, for a run
    jmp short .out
.colour:
    cmp byte [tk_back], TKB_CGA
    jne .out                        ; Mode X plots a byte, not a pattern
    xor ah, ah                      ; the glyph expander for this ink: three
    mov bx, ax                      ; 16-byte tables, one per 2bpp colour
    dec bx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, tk_nib1
    mov [tk_nibp], bx
    mov ah, al
    mov cl, 6
    shl al, cl
    mov [tk_pat0], al
    mov [tk_pat3], ah
    mov al, ah                      ; four pixels of it, for a run
    mov cl, 2
    shl ah, cl
    or al, ah
    mov ah, al
    mov cl, 4
    shl ah, cl
    or al, ah
    mov [tk_inkpat], al
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_depthink - AL = the NEAR ink of a ramp, CX = the depth; out AL = the one
;               to draw at. Preserves everything else.
;
; A vector display has no shading, so distance has to be carried by the ink
; itself - which is what a 256-colour mode is FOR here, and what the four
; colours CGA has cannot do. On the two adapters that cannot, the ramp's three
; entries all resolve to the same value and this costs one compare.
; -----------------------------------------------------------------------------
tk_depthink:
    cmp cx, 1600
    jl .o
    inc al
    cmp cx, 3400
    jl .o
    inc al
.o:
    ret

; -----------------------------------------------------------------------------
; tk_r_end - put the frame on the glass
; clobbers everything
; -----------------------------------------------------------------------------
tk_r_end:
    cmp byte [tk_back], TKB_MODEX
    je .flip
    call tk_blit
    inc byte [tk_par]
    ret
.flip:
    ; ONE CALL, and it is the kernel's now (SPEC.md 53.10). This used to be the
    ; CRTC start-address pair written here by hand, and getting the ORDER
    ; wrong - writing before the retrace rather than waiting for the one that
    ; latches it - shows as a page being drawn on while it is still the one
    ; being scanned, which is the flicker the flip exists to remove.
    mov al, [tk_par]
    and al, 1
    call OSAPI_FSX_PAGE
    inc byte [tk_par]
    ret

; -----------------------------------------------------------------------------
; tk_blit - the shadow's dirty runs onto the glass (CGA and Hercules)
;
; The run per row is the UNION of what this frame drew and what the last one
; did, because a pixel the last frame lit and this one does not still has to
; be taken off the glass. Both span sets are live at this moment, which is
; exactly why there are two of them.
;
; DS is the shadow for the whole loop, so the copy is a rep movsw; everything
; this package owns is reached through CS, which is the same segment as its
; DS (SPEC.md 20.1) and is free at that moment.
; -----------------------------------------------------------------------------
tk_blit:
    push ds
    mov es, [tk_fsi+FSI_SEG]
    mov bp, [tk_vh]
    xor bx, bx                      ; BX = the row's span offset
    xor dx, dx                      ; DX = the row's base in the shadow
    mov ax, [tk_shseg]
    mov ds, ax
    cld
.row:
    mov si, [cs:tk_spcur]
    mov di, [cs:tk_spprv]
    mov cl, [cs:si+bx]              ; CL = min(lo, lo')
    mov al, [cs:di+bx]
    cmp al, cl
    jae .l1
    mov cl, al
.l1:
    mov ch, [cs:si+bx+1]            ; CH = max(hi, hi')
    mov al, [cs:di+bx+1]
    cmp al, ch
    jbe .h1
    mov ch, al
.h1:
    cmp cl, ch
    ja .next
    and cl, 0xFE                    ; whole words, as the clear does
    or ch, 1
    mov al, cl
    xor ah, ah
    mov si, dx
    add si, ax
    mov di, [cs:tk_devoff+bx]
    add di, ax
    mov al, ch
    sub al, cl
    shr al, 1
    xor ah, ah
    inc ax
    xchg ax, cx
    rep movsw
.next:
    add bx, 2
    add dx, 80
    dec bp
    jnz .row
    pop ds
    ret

; -----------------------------------------------------------------------------
; tk_hseg - a HORIZONTAL run, whole bytes at a time (SPEC.md 85.3.3)
; in:  AX = x1, CX = x2 (x1 <= x2 not required), BX = y; viewport coords,
;      clipped here.  [tk_ink] as usual.  preserves every register
;
; SPEC.md 5.6.1's argument, on the other side of the fence: a walk lays one
; pixel per iteration and a fill lays four or eight per STORE, so an
; axis-aligned run belongs to the fill at any angle of the argument. The
; horizon line alone is the viewport's whole width every frame - 640 pixels on
; Hercules - and as a walk it is 12,800 cycles against about 800 as a run.
; -----------------------------------------------------------------------------
tk_hseg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp ax, cx
    jbe .ord
    xchg ax, cx
.ord:
    or bx, bx
    js .out
    cmp bx, [tk_vh]
    jge .out
    or ax, ax
    jns .l
    xor ax, ax
.l:
    cmp cx, [tk_vw]
    jl .r
    mov cx, [tk_vw]
    dec cx
.r:
    cmp ax, cx
    jg .out

    mov bp, ax                      ; BP = x1, DX = x2 for the sub-byte masks
    mov dx, cx
    mov cl, 3
    cmp byte [tk_back], TKB_HERC
    je .k
    mov cl, 2
.k:
    mov si, bp
    shr si, cl                      ; SI = first byte
    mov di, dx
    shr di, cl                      ; DI = last byte
    ; --- the span for this row ------------------------------------------
    push bx
    shl bx, 1
    add bx, [tk_spcur]
    mov ax, si
    cmp [bx], al
    jbe .sl
    mov [bx], al
.sl:
    mov ax, di
    cmp [bx+1], al
    jae .sh
    mov [bx+1], al
.sh:
    pop bx
    ; --- the row's base address -----------------------------------------
    mov ax, bx
    mov cl, 4
    shl ax, cl
    mov bx, ax
    shl ax, 1
    shl ax, 1
    add bx, ax
    add bx, [tk_tbase]              ; BX = the row base
    mov es, [tk_tseg]
    call [tk_hrunproc]
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the sub-byte masks the run's two ends need -------------------------------
tk_hlm:  db 0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01   ; 1bpp, pixels i..7
tk_hrm:  db 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF   ; 1bpp, pixels 0..i
tk_clm:  db 0xFF, 0x3F, 0x0F, 0x03                           ; 2bpp, pixels i..3
tk_crm:  db 0xC0, 0xF0, 0xFC, 0xFF                           ; 2bpp, pixels 0..i
tk_pml:  db 0x0F, 0x0E, 0x0C, 0x08                           ; planes i..3
tk_pmr:  db 0x01, 0x03, 0x07, 0x0F                           ; planes 0..i

; in: BX = the row's base offset, SI/DI = first/last byte, BP = x1, DX = x2,
;     ES = the target segment.  clobbers AX CX SI DI, keeps BX/BP/DX
tk_hrun_herc:
    push bx
    mov bx, bp
    and bx, 7
    mov al, [tk_hlm + bx]           ; the first byte's pixels x1&7 .. 7
    mov bx, dx
    and bx, 7
    mov ah, [tk_hrm + bx]           ; the last byte's pixels 0 .. x2&7
    pop bx
    cmp si, di
    jne .multi
    and al, ah                      ; one byte carries both ends
    or [es:bx+si], al
    ret
.multi:
    or [es:bx+si], al
    or [es:bx+di], ah
    mov cx, di
    sub cx, si
    dec cx
    jz .done
    push di
    mov di, bx
    add di, si
    inc di
    mov al, 0xFF
    cld
    rep stosb
    pop di
.done:
    ret

tk_hrun_cga:
    push bx
    mov bx, bp
    and bx, 3
    mov al, [tk_clm + bx]
    mov bx, dx
    and bx, 3
    mov ah, [tk_crm + bx]
    pop bx
    cmp si, di
    jne .multi
    and al, ah
    and al, [tk_inkpat]
    or [es:bx+si], al
    ret
.multi:
    and al, [tk_inkpat]
    or [es:bx+si], al
    and ah, [tk_inkpat]
    or [es:bx+di], ah
    mov cx, di
    sub cx, si
    dec cx
    jz .done
    push di
    mov di, bx
    add di, si
    inc di
    mov al, [tk_inkpat]
    cld
    rep stosb
    pop di
.done:
    ret

; Mode X's "mask" is a set of PLANES rather than a set of bits, so the two end
; bytes are a map-mask write and a store - which is the same shape as the
; middle, with 0x0F for the mask and a rep stosb for the store.
tk_hrun_modex:
    push bx
    mov bx, bp
    and bx, 3
    mov al, [tk_pml + bx]
    mov bx, dx
    and bx, 3
    mov ah, [tk_pmr + bx]
    pop bx
    cmp si, di
    jne .multi
    and al, ah
    call tk_mxmask
    mov al, [tk_ink8]
    mov [es:bx+si], al
    ret
.multi:
    push ax
    call tk_mxmask                  ; AL = the first byte's planes
    mov al, [tk_ink8]
    mov [es:bx+si], al
    pop ax
    mov al, ah
    call tk_mxmask                  ; ...and the last byte's
    mov al, [tk_ink8]
    mov [es:bx+di], al
    mov cx, di
    sub cx, si
    dec cx
    jz .done
    mov al, 0x0F
    call tk_mxmask
    push di
    mov di, bx
    add di, si
    inc di
    mov al, [tk_ink8]
    cld
    rep stosb
    pop di
.done:
    ret

; tk_mxmask - AL = the plane mask to arm. Preserves everything but flags.
tk_mxmask:
    push ax
    push dx
    mov ah, al
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    pop dx
    pop ax
    ret
