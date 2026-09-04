; =============================================================================
; os8088 - apps/wire/wire.asm
;
; WIREFRAME (SPEC.md 78): a rotating wireframe solid, drawn with nothing but
; OSAPI_GFX_LINE, one frame a tick.
;
; IT EXISTS TO BE THE THING SPEC.md 5.6.4.1 WAS BUILT FOR, and to say out loud
; whether it worked: the status strip reports the measured frame rate and how
; many line calls each frame costs, so the answer is on the glass rather than
; in a document. On the 4.77MHz 8088 this targets, the same twelve edges drawn
; and erased through 5.6.4's general walk are about 7 frames a second and
; through 5.6.4.1's about 18 - which is the tick, and therefore the ceiling.
;
; WHY IT ERASES WITH LINES AND NOT WITH A FILL. A fill of the object's box is
; cheaper - about 10ms against 23 - and it would make this a benchmark of
; gfx_fill. The point here is the line primitive, and erasing the way SPEC.md
; 48's missile trails erase is also the honest shape: only the pixels that
; were drawn are touched, which is PERFORMANCE.md rule 1.
;
; ...AND THE ORDER IT ERASES IN IS A MENU (SPEC.md 78.5), because it turned
; out to be the whole of the flicker and to be free. Erasing the whole figure
; and then drawing it leaves the window EMPTY - measured, literally zero ink,
; on 56% of displayed frames. Doing it an edge at a time leaves eleven of
; twelve edges up, never drops below 72% of the figure, and costs nothing at
; all: the same twenty-four line calls, twenty-two more SET_COLOR. It is the
; default for that reason and not as a taste.
;
; NO DILATION on the erase (SPEC.md 5.6.5). A trail is drawn in per-frame
; SEGMENTS and erased as one long line, so the two rasterisations differ; here
; every edge is drawn whole and erased whole from the same endpoint pair, so
; 5.6.2's contract makes the pixel sets identical and SI = 0 is correct. That
; is worth stating because passing 1 would cost three walks for nothing.
;
; The maths is integer and the projection is ORTHOGRAPHIC on purpose: a
; perspective divide is affordable (16 idivs a frame, ~0.6ms) but it makes the
; near corner of a rotating cube swing outside any box you size for it, and a
; cube with its corners clipped off looks broken rather than fast.
;
; Every proc is a near proc with a near ret: the kernel reaches a callback
; through the dispatcher in the package's own header (SPEC.md 20.1).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'WIRE', wr_entry, 0, OS88_STACK_192
                                ; THE WORKER'S STACK, declared
                                ; rather than defaulted (SPEC.md 8.7):
                                ; static 40 for wr_worker
                                ; over the 64-byte interrupt floor
                                ; that is 104, and 192 gives 1.85x

WR_W        equ 162                 ; window, outer - near enough square,
WR_H        equ 150                 ; because the figure is sized off the
                                    ; SHORTER side and a wide box wastes the
                                    ; rest
WR_STRIP    equ 11                  ; the status strip along the bottom
WR_MARGIN   equ 2                   ; ...and the slack the object keeps from
                                    ; every edge of what is left
WR_MAXV     equ 8                   ; the biggest shape here
WR_MAXE     equ 12
WR_ASTEP    equ 3                   ; angle steps a frame, of 256 to the turn:
WR_BSTEP    equ 2                   ; coprime, so the tumble does not repeat
                                    ; every few seconds
; --- the COMPOSITE, SPEC.md 78.8's fourth order ------------------------------
; The figure is rasterised into a private 1bpp mask and the whole band is put
; down with one OSAPI_GFX_BLIT1, which arrives in FINAL SCREEN POLARITY - so
; it carries this frame's figure AND the absence of last frame's, and no pixel
; is read or written twice.
;
; **IT IS BLACK ON WHITE HERE**, which is the one thing that does not carry
; over from the screen saver's version of this (SPEC.md 79.5.6). The band's
; 1 bits are LIT, so the paper is a mask of 0xFF and an edge is ANDed out of
; it - where the saver, drawing white on black, clears to zero and ORs in.
;
; THE BAND IS THE FIGURE'S BOUNDING BOX AND NOT THE WINDOW'S (SPEC.md 78.8.1),
; because the sweep is what the flicker is made of: it was 128 columns of the
; object area's whole height - 1,920 bytes - around a figure that measures 38
; to 54 px across, and compose plus blit came to 52.1 ms of a 54.9 ms frame.
; Sized to the figure it is 426 bytes and 21.2 ms, and the blit - the only span
; in which the glass is INCOHERENT - falls from 26% of the frame to 10%.
;
; It is the UNION of this frame's box and the one already on the glass, which
; is not an optimisation to skip: the band erases by COVERING, so a previous
; figure outside it is a previous figure for ever. At WR_ASTEP/WR_BSTEP the two
; boxes overlap almost exactly, so the union costs nearly nothing.
;
; x is forced onto the byte grid because BLIT1 refuses one that is not, and the
; whole band is checked against the object area before the blit - a band that
; would leave it sends the frame down mode 0's path instead, COVERING the mask
; frame with a fill first, because a white kernel line does not cancel a mask
; one (SPEC.md 78.8.2). WR_BW/WR_BH are
; now the mask's CAPACITY rather than the band's size, and the STRIDE stays
; WR_BST so wr_mline's row step is still a shift by 4.
WR_BW       equ 128                 ; the most COLUMNS the mask can hold, and a
WR_BST      equ WR_BW / 8           ; multiple of 8; also the mask's stride
WR_BH       equ 128                 ; ...and the most rows
WR_BSZ      equ WR_BST * WR_BH
WR_LAGMAX   equ 4                   ; ticks behind before the deadline is
                                    ; re-anchored rather than caught up
WR_FPSTICK  equ 18                  ; ...and how often the strip is re-lettered
WR_FPSCOL   equ 10                  ; ...and the CELL the number starts in:
                                    ; "NN" plus wr_s_lines' eight. SPEC.md 78.6
                                    ; re-letters four cells here and nothing
                                    ; else, so this and the field widths in
                                    ; wr_pad2/wr_fpstxt are one fact

; -----------------------------------------------------------------------------
; wr_entry - package entry point (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, from wm_create)
;
; The worker is NOT hired here: OSAPI_TASK_SPAWN wants the gfx lock held and
; this runs without it, and the instance is not published yet either. The
; first paint hires it, which is arkanoid's shape (SPEC.md 44).
; -----------------------------------------------------------------------------
wr_entry:
    push si
    mov si, wr_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [wr_win], bx
    mov byte [wr_size], 4           ; Medium, and the two bss bytes whose zero
    mov byte [wr_mode], 1           ; is not the state we want - SPEC.md 78.5
    call wr_pick                    ; measured `Edge at a time` free
    mov si, wr_menus
    call OSAPI_MENU_SET
    mov si, wr_about
    call OSAPI_ABOUT_SET            ; ...and 'About Wire' above the Close the
                                    ; kernel already puts there (SPEC.md 12.2/
                                    ; 12.7). BX is still the window: both slots
                                    ; preserve every register and the flags,
                                    ; which is why they may sit between
                                    ; wm_create and the CF we owe the loader
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; wr_geom - the content rect and where the figure sits in it
; in:  BX = window ptr, ES = KERNEL_SEG
; out: [wr_ox0]/[wr_oy0]/[wr_ow]/[wr_oh] the OBJECT area, [wr_ccx]/[wr_ccy] its
;      centre, [wr_scale] the projection scale, [wr_sy] the strip's text row
; clobbers: AX, DX, flags
;
; Re-derived every frame rather than cached, because a window can be dragged
; and SPEC.md 11.98 is the class of bug that comes from deciding once. It is
; four adds and two shifts.
;
; The scale is 1.75 x the half-extent, and that constant is the shapes'
; rather than a taste: a vertex at (48,48,48) reaches |x'| = 68 after any
; rotation, and 68 * 1.75 / 128 is 0.93 - so the figure fills the area and its
; corners stay inside it. Change the vertex magnitude and this changes with it.
; -----------------------------------------------------------------------------
wr_geom:
    push bx
    push cx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [wr_ox0], ax
    mov [wr_oy0], dx
    mov ax, [es:bx + W_W]
    sub ax, 2
    mov [wr_ow], ax
    mov ax, [es:bx + W_H]
    sub ax, TITLE_H + 1
    mov dx, [wr_oy0]
    add dx, ax
    sub dx, WR_STRIP - 2            ; the strip's text baseline
    mov [wr_sy], dx
    sub ax, WR_STRIP
    mov [wr_oh], ax

    mov ax, [wr_ow]                 ; the centre
    shr ax, 1
    add ax, [wr_ox0]
    mov [wr_ccx], ax
    mov ax, [wr_oh]
    shr ax, 1
    add ax, [wr_oy0]
    mov [wr_ccy], ax

    mov ax, [wr_ow]                 ; r = min(w,h)/2 - margin
    mov cx, [wr_oh]
    cmp ax, cx
    jbe .min
    mov ax, cx
.min:
    shr ax, 1
    sub ax, WR_MARGIN
    jns .rok
    xor ax, ax
.rok:
    mov cx, ax                      ; scale = 1.75r, then the View menu's
    shr ax, 1                       ; quarter: 2, 3 or 4
    add cx, ax
    shr ax, 1
    add cx, ax
    mov al, [wr_size]               ; ...and the View menu's EIGHTHS of it
    xor ah, ah
    mul cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    cmp ax, 127                     ; the projection multiplies by a BYTE
    jbe .sok
    mov ax, 127
.sok:
    mov [wr_scale], al
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wr_sh7 - AL = DI >> 7, signed
; in:  DI = a signed 16-bit product sum, |DI| < 16384
; out: AL; AH is scratch. clobbers AX, flags
;
; `sar di,7` is 8 + 4*7 = 36 clocks on an 8088 and this is 5: one doubling
; leaves the wanted bits in AH. The bound holds because every caller sums two
; products of a +-127 byte with a +-64 one.
; -----------------------------------------------------------------------------
wr_sh7:
    mov ax, di
    add ax, ax
    mov al, ah
    ret

; -----------------------------------------------------------------------------
; wr_project - every vertex of the current shape, into wr_px / wr_py
; in:  the angles in [wr_a]/[wr_b], the geometry from wr_geom
; out: wr_px[i], wr_py[i] - screen words. clobbers everything but the segments
;
; Two rotations and an orthographic projection, all in signed bytes, with the
; two products of each term summed at 16 bits BEFORE the shift - which is
; where the precision is, and costs nothing.
; -----------------------------------------------------------------------------
wr_project:
    mov al, [wr_a]                  ; the four sines this frame needs, fetched
    xor ah, ah                      ; once rather than per vertex
    mov bx, ax
    mov al, [wr_sintab + bx]
    mov [wr_sina], al
    add bl, 64
    adc bh, 0
    and bx, 255
    mov al, [wr_sintab + bx]
    mov [wr_cosa], al
    mov al, [wr_b]
    xor ah, ah
    mov bx, ax
    mov al, [wr_sintab + bx]
    mov [wr_sinb], al
    add bl, 64
    adc bh, 0
    and bx, 255
    mov al, [wr_sintab + bx]
    mov [wr_cosb], al

    mov si, [wr_vp]                 ; the shape's vertex list
    mov di, 0                       ; ...and our index into wr_px / wr_py
    mov cl, [wr_nv]
    xor ch, ch
.vert:
    push cx
    push si
    push di
    call wr_rot                     ; -> [wr_rx], [wr_ry]
    pop di
    mov al, [wr_rx]
    imul byte [wr_scale]
    push di
    mov di, ax
    call wr_sh7
    pop di
    cbw
    add ax, [wr_ccx]
    mov [wr_px + di], ax
    mov al, [wr_ry]
    imul byte [wr_scale]
    push di
    mov di, ax
    call wr_sh7
    pop di
    cbw
    mov bx, [wr_ccy]
    sub bx, ax                      ; screen y grows DOWN and the model's up
    mov [wr_py + di], bx
    add di, 2
    pop si
    add si, 3
    pop cx
    dec cx
    jnz .vert
    ret

; -----------------------------------------------------------------------------
; wr_rot - one vertex through both rotations
; in:  SI -> three signed bytes (x, y, z)
; out: [wr_rx] = x after the Y rotation, [wr_ry] = y after the X one
; clobbers: AX, BX, DI, flags
;
; z is computed and then thrown away: an orthographic projection does not want
; it, and a depth sort is not what a wireframe is.
; -----------------------------------------------------------------------------
wr_rot:
    mov al, [si]                    ; x' = (x*cosA - z*sinA) >> 7
    imul byte [wr_cosa]
    mov di, ax
    mov al, [si+2]
    imul byte [wr_sina]
    sub di, ax
    call wr_sh7
    mov [wr_rx], al

    mov al, [si]                    ; z' = (x*sinA + z*cosA) >> 7
    imul byte [wr_sina]
    mov di, ax
    mov al, [si+2]
    imul byte [wr_cosa]
    add di, ax
    call wr_sh7
    mov bl, al

    mov al, [si+1]                  ; y' = (y*cosB - z'*sinB) >> 7
    imul byte [wr_cosb]
    mov di, ax
    mov al, bl
    imul byte [wr_sinb]
    sub di, ax
    call wr_sh7
    mov [wr_ry], al
    ret

; -----------------------------------------------------------------------------
; wr_edges - draw the shape's edges from one coordinate pair table
; in:  SI = the x table, DI = the y table, the pen already set
; out: nothing; clobbers everything but the segments
;
; ONE OSAPI_GFX_LINE PER EDGE and nothing else - which is the whole of this
; package's inner loop, and what the fps figure below is measuring.
; -----------------------------------------------------------------------------
wr_edges:
    mov bp, [wr_ep]                 ; the shape's edge list: index pairs
    mov cl, [wr_ne]
    xor ch, ch
.edge:
    push cx
    push bp
    call wr_edge1
    pop bp
    add bp, 2
    pop cx
    dec cx
    jnz .edge
    ret

; -----------------------------------------------------------------------------
; wr_compose - the whole figure into the mask (SPEC.md 78.8)
; out: CF = 1 = it does not fit the band, nothing composed
; clobbers: everything but the segments
; -----------------------------------------------------------------------------
wr_compose:
    mov ax, [wr_px]                 ; --- the bounds, seeded from vertex 0 and
    mov [wr_bmnx], ax               ; folded over BOTH figures below
    mov [wr_bmxx], ax
    mov ax, [wr_py]
    mov [wr_bmny], ax
    mov [wr_bmxy], ax
    mov si, wr_px
    mov di, wr_py
    call wr_bspan
    cmp byte [wr_shown], 0
    je .bounded                     ; nothing on the glass to have to cover
    mov si, wr_ex                   ; THE BAND ERASES BY COVERING, so the frame
    mov di, wr_ey                   ; already down has to be inside it too -
    call wr_bspan                   ; see the header (SPEC.md 78.8.1)
.bounded:
    mov ax, [wr_bmnx]               ; --- x, a pixel of margin and onto the
    dec ax                          ; byte grid BLIT1 insists on
    and ax, 0FFF8h
    mov [wr_bx0], ax
    mov ax, [wr_bmxx]
    add ax, 8                       ; +1 of margin, then rounded UP to the byte
    and ax, 0FFF8h
    sub ax, [wr_bx0]
    cmp ax, WR_BW
    ja .no                          ; wider than the mask: refuse
    mov [wr_bw], ax

    mov ax, [wr_bmny]               ; --- ...and y, which needs no grid
    dec ax
    mov [wr_by0], ax
    mov ax, [wr_bmxy]
    add ax, 2                       ; +1 of margin, +1 to make it a COUNT
    sub ax, [wr_by0]
    cmp ax, WR_BH
    ja .no
    mov [wr_bh], ax

    mov ax, [wr_bx0]                ; --- and INSIDE the object area, both ways.
    cmp ax, [wr_ox0]                ; The clip would stop a band that was not,
    jl .no                          ; but it would stop it having already spent
    mov bx, [wr_bw]                 ; the blit - and the strip lives directly
    add ax, bx                      ; under these rows
    mov bx, [wr_ox0]
    add bx, [wr_ow]
    cmp ax, bx
    jg .no
    mov ax, [wr_by0]
    cmp ax, [wr_oy0]
    jl .no
    mov bx, [wr_bh]
    add ax, bx
    mov bx, [wr_oy0]
    add bx, [wr_oh]
    cmp ax, bx
    jg .no

    push es                         ; --- the paper, which is WHITE here. Only
    push ds                         ; the rows this band HAS: the mask is sized
    pop es                          ; for WR_BH and this frame rarely wants them
    mov di, wr_mask
    mov ax, [wr_bh]
    mov cl, 3                       ; bh * WR_BST bytes = bh * 8 words
    shl ax, cl
    mov cx, ax
    mov ax, 0FFFFh
    cld
    rep stosw
    pop es

    mov bp, [wr_ep]                 ; --- and the figure ANDed out of it
    mov cl, [wr_ne]
    xor ch, ch
.edge:
    push cx
    push bp
    mov al, [ds:bp]
    call wr_mpt
    mov [wr_mx1], ax
    mov [wr_my1], dx
    pop bp
    push bp
    mov al, [ds:bp+1]
    call wr_mpt
    mov [wr_mx2], ax
    mov [wr_my2], dx
    call wr_mline
    pop bp
    add bp, 2
    pop cx
    dec cx
    jnz .edge
    clc
    ret
.no:
    stc
    ret

; wr_bspan - fold one frame's vertex arrays into the running band bounds
; in:  SI = the x array, DI = the y array, [wr_nv] vertices in each
; out: [wr_bmnx]/[wr_bmxx]/[wr_bmny]/[wr_bmxy] widened to include them
; clobbers: AX, BX, CX, flags
;
; SIGNED compares throughout: wr_project can put a vertex off the left of the
; object area for a frame, and an unsigned MIN would take -3 for 65533 and
; size the band to the whole screen.
wr_bspan:
    mov cl, [wr_nv]
    xor ch, ch
    xor bx, bx
.v:
    mov ax, [si + bx]
    cmp ax, [wr_bmnx]
    jge .nx
    mov [wr_bmnx], ax
.nx:
    cmp ax, [wr_bmxx]
    jle .xx
    mov [wr_bmxx], ax
.xx:
    mov ax, [di + bx]
    cmp ax, [wr_bmny]
    jge .ny
    mov [wr_bmny], ax
.ny:
    cmp ax, [wr_bmxy]
    jle .xy
    mov [wr_bmxy], ax
.xy:
    add bx, 2
    dec cx
    jnz .v
    ret

; wr_mpt - vertex AL -> AX/DX = its place in the mask; clobbers AX, DX, DI
wr_mpt:
    xor ah, ah
    add ax, ax
    mov di, ax
    mov dx, [wr_py + di]
    sub dx, [wr_by0]
    mov ax, [wr_px + di]
    sub ax, [wr_bx0]
    ret

; -----------------------------------------------------------------------------
; wr_put - the band onto the glass, one arrival (SPEC.md 5.4.2)
; out: CF = 1 = REFUSED (a kern_small kernel carries the slot and not the body)
; -----------------------------------------------------------------------------
wr_put:
    push es
    push ds
    pop es
    mov si, wr_mask
    mov bp, WR_BST                  ; the STRIDE stays the mask's, so wr_mline's
                                    ; row step is still a shift by 4 - only the
                                    ; COLUMNS blitted shrink (SPEC.md 78.8.1)
    mov ax, [wr_bx0]
    mov bx, [wr_by0]
    mov cx, [wr_bw]
    mov dx, [wr_bh]
    call OSAPI_GFX_BLIT1
    pop es                          ; `pop` writes no flag, so BLIT1's CF is
    jc .out                         ; still the caller's answer
    mov ax, [wr_bx0]                ; THE BAND THAT IS ON THE GLASS, kept here
    mov [wr_gbx0], ax               ; and not read back off wr_bx0: a later
    mov ax, [wr_by0]                ; wr_compose stages x0/y0 before the tests
    mov [wr_gby0], ax               ; that can refuse, so after a refusal those
    mov ax, [wr_bh]                 ; four words are half this frame's and half
    mov [wr_gbh], ax                ; the last one's (SPEC.md 78.8.2)
    mov ax, [wr_bw]
    mov [wr_gbw], ax                ; ...and this one LAST: it is the flag
.out:                               ; (`mov` writes no flag, so CF is still
    ret                             ; BLIT1's on both arms)

; -----------------------------------------------------------------------------
; wr_mline - one edge into the mask, in MASK coordinates
; in:  [wr_mx1]/[wr_my1] -> [wr_mx2]/[wr_my2]; clobbers AX, BX, CX, DX, SI, DI
;
; Ordered left to right first, so the x step is always +1 and only the y step
; carries a sign. The bit is an AND MASK and not a set bit - see the header at
; the top of this file - so it walks by rotating a zero along with `stc`/`rcr`,
; and the carry OUT of that is the wrap into the next byte.
; -----------------------------------------------------------------------------
wr_mline:
    mov ax, [wr_mx1]
    mov cx, [wr_mx2]
    cmp ax, cx
    jle .ord
    mov [wr_mx1], cx
    mov [wr_mx2], ax
    mov ax, [wr_my1]
    mov cx, [wr_my2]
    mov [wr_my1], cx
    mov [wr_my2], ax
.ord:
    mov ax, [wr_mx2]
    sub ax, [wr_mx1]
    mov [wr_mdx], ax
    mov ax, [wr_my2]
    sub ax, [wr_my1]
    mov word [wr_msy], WR_BST
    jns .dy
    neg ax
    mov word [wr_msy], -WR_BST
.dy:
    mov [wr_mdy], ax

    mov ax, [wr_my1]                ; the first byte, and the bit in it
    mov cl, 4
    shl ax, cl                      ; y * WR_BST, which is why the stride is 16
    mov di, ax
    mov ax, [wr_mx1]
    mov bx, ax
    mov cl, 3
    shr bx, cl
    add di, bx
    and al, 7
    mov cl, al
    mov bl, 07Fh
    ror bl, cl                      ; the ZERO walks, not the one

    mov ax, [wr_mdx]
    cmp ax, [wr_mdy]
    jl .majy

    mov si, ax                      ; --- x is the major axis
    inc si
    mov bp, [wr_mdy]
    add bp, bp
    mov [wr_me1], bp
    sub bp, ax
    add ax, ax
    mov [wr_me2], ax
.mx:
    and [wr_mask + di], bl
    stc
    rcr bl, 1
    jc .mx1
    mov bl, 07Fh
    inc di
.mx1:
    add bp, [wr_me1]
    jle .mx2
    sub bp, [wr_me2]
    add di, [wr_msy]
.mx2:
    dec si
    jnz .mx
    ret

.majy:                              ; --- ...or y is
    mov si, [wr_mdy]
    inc si
    mov bp, [wr_mdx]
    add bp, bp
    mov [wr_me1], bp
    sub bp, [wr_mdy]
    mov ax, [wr_mdy]
    add ax, ax
    mov [wr_me2], ax
.my:
    and [wr_mask + di], bl
    add di, [wr_msy]
    add bp, [wr_me1]
    jle .my2
    sub bp, [wr_me2]
    stc
    rcr bl, 1
    jc .my2
    mov bl, 07Fh
    inc di
.my2:
    dec si
    jnz .my
    ret

; -----------------------------------------------------------------------------
; wr_pairs - SPEC.md 78.5: erase old[i], draw new[i], EDGE BY EDGE
; in:  the pen is set per edge here; clobbers everything but the segments
;
; The figure is then missing one edge of twelve at a time instead of all
; twelve at once, which is the whole of the flicker. What it costs is a NICK:
; erasing old[j] cuts new[i] wherever the two cross, for every j drawn after
; i, and on a cube in projection that is a handful of pixels a frame. Mode 2
; buys them back with a repair pass.
; -----------------------------------------------------------------------------
wr_pairs:
    mov bp, [wr_ep]
    mov cl, [wr_ne]
    xor ch, ch
.edge:
    push cx
    push bp
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov si, wr_ex
    mov di, wr_ey
    call wr_edge1
    pop bp
    push bp
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, wr_px
    mov di, wr_py
    call wr_edge1
    pop bp
    add bp, 2
    pop cx
    dec cx
    jnz .edge
    ret

; -----------------------------------------------------------------------------
; wr_edge1 - ONE edge, from one coordinate pair table
; in:  SI = the x table, DI = the y table, BP -> the edge's two indices
; out: nothing; SI/DI/BP survive, AX/BX/CX/DX do not
; -----------------------------------------------------------------------------
wr_edge1:
    mov bx, bp
    mov al, [bx]                    ; the two vertex indices, as word offsets
    xor ah, ah
    add ax, ax
    mov bx, ax
    mov al, [bx + si]               ; ...and NOT [si+bx]: one is a base+index
    mov ah, [bx + si + 1]           ; addressing mode either way, and this is
    push ax                         ; the one NASM assembles without a warning
    mov ax, [bx + di]
    push ax
    mov bx, bp
    mov al, [bx+1]
    xor ah, ah
    add ax, ax
    mov bx, ax
    mov cx, [bx + si]
    mov dx, [bx + di]
    pop bx                          ; y1
    pop ax                          ; x1
    push si
    push di
    push bp
    xor si, si                      ; SPEC.md 5.6.5: thin, both ways - see the
    call OSAPI_GFX_LINE             ; header
    pop bp
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; wr_draw - erase the frame on the glass, then draw the new one
; in:  ES = KERNEL_SEG, the gfx lock held, the clip armed, wr_px/wr_py current
; out: nothing; clobbers everything but the segments
;
; The order is erase-then-draw and the two are the SAME pixel set at the same
; endpoints, so a pixel the new frame keeps is written twice - which is
; PERFORMANCE.md rule 2's violation, and is the price of not keeping a second
; buffer on a machine with 640KB and no blitter. It is what SPEC.md 48's
; trails do for the same reason.
; -----------------------------------------------------------------------------
wr_draw:
    cmp byte [wr_mode], 3           ; --- SPEC.md 78.8: composed and put down
    jne .orders                     ; in one call
    call wr_compose
    jc .orders                      ; ...the figure does not fit the band
    call wr_put
    jnc .keep
.orders:
    cmp byte [wr_shown], 0
    je .fresh                       ; nothing on the glass to take off
    cmp word [wr_gbw], 0
    jne .cover                      ; A COMPOSED FRAME IS ON THE GLASS, and a
                                    ; white kernel line does not cancel a mask
                                    ; one: the two rasterisations differ for
                                    ; most endpoint pairs (SPEC.md 78.8.2)
    cmp byte [wr_mode], 0
    je .whole
    cmp byte [wr_mode], 3
    je .whole                       ; the composite refused: this frame goes
                                    ; down whole, since nothing was erased
    call wr_pairs                   ; 78.5's two other orders
    cmp byte [wr_mode], 2
    jne .keep
    mov al, CBLACK                  ; ...and the repair pass, which buys back
    call OSAPI_SET_COLOR            ; the nicks at the cost of a third walk
    mov si, wr_px
    mov di, wr_py
    call wr_edges
    jmp short .keep
.cover:
    mov al, CWHITE                  ; ...so it comes off the way it went down,
    call OSAPI_SET_COLOR            ; in ONE call rather than one per edge. The
    mov ax, [wr_gbx0]               ; band passed the object-area test before it
    mov bx, [wr_gby0]               ; was blitted, so this fill is inside it
    mov cx, ax
    add cx, [wr_gbw]
    dec cx
    mov dx, bx
    add dx, [wr_gbh]
    dec dx
    call OSAPI_GFX_FILL
    jmp short .fresh
.whole:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov si, wr_ex
    mov di, wr_ey
    call wr_edges
.fresh:
    mov word [wr_gbw], 0            ; this frame goes down as kernel lines, so
                                    ; kernel lines are what will take it off
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, wr_px
    mov di, wr_py
    call wr_edges
.keep:
    call wr_keep
    ret

; wr_keep - the frame now on the glass is the one to erase next time
wr_keep:
    push cx
    push si
    push di
    mov si, wr_px
    mov di, wr_ex
    mov cx, WR_MAXV * 2
    call wr_copy
    mov si, wr_py
    mov di, wr_ey
    mov cx, WR_MAXV * 2
    call wr_copy
    mov byte [wr_shown], 1
    pop di
    pop si
    pop cx
    ret

; wr_copy - CX bytes from SI to DI, both in our own segment
; movsb IS NOT AVAILABLE: ES is the kernel's on every callback (SPEC.md 20.1)
wr_copy:
    push ax
.b:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .b
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_strip - the whole status line, laid down once (SPEC.md 78.6)
; in:  the gfx lock held, the geometry derived
; out: nothing; preserves nothing but the segments
;
; ONE OSAPI_FONT_RUN, AND NO FILL IN FRONT OF IT. What this used to be - fill
; the band white, then letter over it - is PERFORMANCE.md rule 2's canonical
; violation, and on this window it is the only thing wrong with an otherwise
; still frame: the strip went blank for the width of the whole line, once a
; second, next to a figure that is deliberately never blank (78.5). FONT_RUN
; makes ONE decision per cell (SPEC.md 6.1), so no cell is ever momentarily
; paper.
;
; EVERY FIELD IS FIXED WIDTH and the run starts on a CELL BOUNDARY. The width
; is what lets wr_fpsdraw re-letter four cells without moving the rest; the
; boundary is what makes a cell row a single store on a 1bpp adapter, which is
; the two adapters of three this is looked at on.
; -----------------------------------------------------------------------------
wr_strip:
    mov di, wr_line                 ; "NN lines  NN.N fps"
    mov al, [wr_ne]
    xor ah, ah
    call wr_pad2
    mov si, wr_s_lines
    call wr_cat
    call wr_fpstxt
    mov si, wr_s_fps
    call wr_cat
    mov byte [di], 0
    call wr_sxal                    ; CX = the aligned left edge
    mov dx, [wr_sy]
    mov si, wr_line
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; -----------------------------------------------------------------------------
; wr_fpsdraw - re-letter ONLY the four cells the number lives in (SPEC.md 78.6)
; in:  the gfx lock held, the geometry derived
; out: nothing; preserves nothing but the segments
;
; PERFORMANCE.md rule 1, at the smallest scale this package has: ' lines  ' and
; ' fps' are the same pixels a second later, and the count only moves when the
; Shape menu does. Putting them down again is work that changes nothing and a
; flash that changes nothing twice.
; -----------------------------------------------------------------------------
wr_fpsdraw:
    mov di, wr_line                 ; the composed line is not live between
    call wr_fpstxt                  ; strip updates, so the field borrows it
    mov byte [di], 0
    call wr_sxal
    add cx, WR_FPSCOL * 8
    mov dx, [wr_sy]
    mov si, wr_line
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; wr_sxal - CX = the strip's text x, rounded UP onto the 8-pixel cell grid.
; The content's left edge follows the window and is on no grid at all; SPEC.md
; 6.1's single-store path is. An 18-cell line leaves 16 pixels of slack in the
; object area, so rounding up by at most 7 cannot reach the right edge.
; Preserves everything but CX.
wr_sxal:
    mov cx, [wr_ox0]
    add cx, 7
    and cx, -8
    ret

; wr_pad2 - AX (0..99) as TWO chars at DI, space-padded. DI advances.
; Fixed width is the whole of why wr_fpsdraw can exist: a field that changes
; width moves the ones after it, and then only the whole line is redrawable.
wr_pad2:
    push ax
    push bx
    push dx
    xor dx, dx
    mov bx, 10
    div bx                          ; AX = tens, DX = units
    or al, al
    jnz .tens
    mov al, ' ' - '0'               ; a leading zero is a space, and the add
.tens:                              ; below is the only place either becomes
    add al, '0'                     ; a character
    mov [di], al
    inc di
    mov al, dl
    add al, '0'
    mov [di], al
    inc di
    pop dx
    pop bx
    pop ax
    ret

; wr_fpstxt - [wr_fps] as the four chars "NN.N" at DI. DI advances.
; FOUR ALWAYS: 18.2 is the tick and therefore the ceiling (SPEC.md 78), and a
; field that changes width is a field wr_fpsdraw cannot redraw on its own.
wr_fpstxt:
    push ax
    push bx
    push dx
    mov ax, [wr_fps]
    xor dx, dx
    mov bx, 10
    div bx                          ; AX = whole, DX = tenths
    push dx
    call wr_pad2
    mov byte [di], '.'
    inc di
    pop ax
    add al, '0'
    mov [di], al
    inc di
    pop dx
    pop bx
    pop ax
    ret

; wr_cat - the NUL string at SI onto DI (the NUL is not copied). DI advances.
wr_cat:
    push ax
.c:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc di
    inc si
    jmp short .c
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_paint - W_PAINT: the whole content, from scratch
; in:  SI = window ptr; the gfx lock held, ES = KERNEL_SEG
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
wr_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    call wr_geom
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wr_ox0]
    mov bx, [wr_oy0]
    mov cx, ax
    add cx, [wr_ow]
    dec cx
    mov dx, bx
    add dx, [wr_oh]
    add dx, WR_STRIP - 1
    call OSAPI_GFX_FILL
    mov byte [wr_shown], 0          ; the window may have moved since the last
                                    ; frame, so the kept coordinates are stale
    cmp byte [wr_abon], 0           ; W_PAINT MUST BE ABLE TO REPRODUCE WHAT IS
    je .figure                      ; ON THE GLASS, and while the credit card
    call wr_abdraw                  ; is up that is the card (SPEC.md 78.7).
    jmp short .strip                ; A menu closing over this window is a
.figure:                            ; repaint, so without this arm the pick
    call wr_project                 ; that RAISES the card is also what paints
    call wr_draw                    ; the figure straight back over it
.strip:
    call wr_strip
    call wr_hire
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_hire - spawn the worker, once. The gfx lock must be held (SPEC.md 20.6).
; A refusal is transient - the task table holds twelve - so nothing is latched
; and the next paint tries again.
; -----------------------------------------------------------------------------
wr_hire:
    push ax
    push bx
    cmp byte [wr_hired], 0
    jne .out
    mov ax, wr_worker
    mov bx, [wr_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [wr_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_worker - THE background task (SPEC.md 20.6)
; in:  DX = our instance index, DS = ES = CS = our segment, IF = 1, gfx lock
;      free. Never returns: OSAPI_TASK_ALIVE not coming back is the way out.
;
; One frame a tick, arkanoid's deadline shape exactly - and on this machine the
; tick is also the ceiling, so a frame that fits it is a frame that shows.
; -----------------------------------------------------------------------------
wr_worker:
    call OSAPI_GET_TICKS
    mov [wr_due], ax
    mov [wr_ft0], ax
.loop:
    mov bx, [wr_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here (rule 4)
    inc word [wr_due]
    call OSAPI_GET_TICKS
    mov bx, [wr_due]
    sub bx, ax                      ; signed, and wrap-safe by subtraction
    jle .behind
    mov ax, bx
    call OSAPI_TASK_SLEEP
    jmp short .frame
.behind:
    cmp bx, -WR_LAGMAX
    jg .frame
    mov [wr_due], ax                ; hopelessly late: re-anchor, or the
.frame:                             ; deadline runs away and this never sleeps
    add byte [wr_a], WR_ASTEP
    add byte [wr_b], WR_BSTEP
    call wr_render
    jmp .loop

; -----------------------------------------------------------------------------
; wr_render - one frame on the glass
; in:  nothing; the gfx lock free. Preserves all registers.
; -----------------------------------------------------------------------------
wr_render:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call OSAPI_GFX_LOCK
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [wr_win]
    test word [es:bx + W_FLAGS], 2  ; still visible?
    jz .unlock
    cmp byte [wr_abon], 0           ; ...and is the credit card up (78.7)? Then
    jne .unlock                     ; this frame draws NOTHING: the card is in
                                    ; the object area and a figure over it is
                                    ; what "a package cannot put up a second
                                    ; window" costs when the package also has
                                    ; a worker
    call wr_geom
    mov bx, [wr_win]
    call OSAPI_WM_CLIP_SET          ; over 16 fragments: skip the frame rather
    jc .unlock                      ; than draw over whatever is on top
    call wr_project
    call wr_draw
    inc word [wr_frames]
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [wr_ft0]
    cmp ax, WR_FPSTICK
    jb .done
    call wr_fpsup                   ; AX = ticks elapsed, BX = now
.done:
    call OSAPI_WM_CLIP_CLEAR
.unlock:
    call OSAPI_GFX_UNLOCK
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_fpsup - frames over ticks, as tenths, and re-letter the strip
; in:  AX = ticks elapsed (>= WR_FPSTICK), BX = the tick count now
; out: nothing; clobbers everything but the segments
;
; 18.2 ticks a second (SPEC.md 8), so tenths = frames * 182 / ticks. The
; multiply cannot overflow: a frame a tick is the ceiling, so frames <= ticks
; and the product is at most 182 * ticks with ticks small.
; -----------------------------------------------------------------------------
wr_fpsup:
    mov cx, ax
    mov [wr_ft0], bx
    mov ax, [wr_frames]
    mov word [wr_frames], 0
    mov bx, 182
    mul bx
    div cx
    mov [wr_fps], ax
    call wr_fpsdraw                 ; the NUMBER, not the line (SPEC.md 78.6)
    ret

; -----------------------------------------------------------------------------
; wr_oncmd - AM_ONCMD: the Shape and View menus (SPEC.md 12.2)
; in:  AL = item, AH = menu (0 = Shape, 1 = View), SI = window, BX = menu set
; out: nothing
;
; On the UI task with the lock held, exactly like a click. Both menus end in a
; full repaint because the figure changes size or shape and the kept
; coordinates no longer describe what is on the glass.
; -----------------------------------------------------------------------------
wr_oncmd:
    push si
    call wr_abdown                  ; a pick takes the card down (78.7); every
                                    ; arm below ends at .redraw, which puts the
                                    ; content back whole
    cmp ah, 1
    jb .shape
    je .view
    cmp al, 4                       ; --- Draw: 78.5's three orders and 78.8's
    jae .out                        ;     COMPOSED, which shipped as item 3 and
    mov [wr_mode], al               ;     a `cmp al, 3` here kept unpickable
    jmp short .redraw
.shape:
    cmp al, WR_NSHAPE
    jae .out
    mov [wr_shape], al
    call wr_pick
    jmp short .redraw
.view:
    cmp al, 3
    jae .out
    xor ah, ah                      ; Small / Medium / Large, in eighths of
    mov bx, ax                      ; wr_geom's 1.75r. MEDIUM IS THE TICK: it
    mov al, [wr_eighths + bx]       ; is chosen so a frame fits 55ms on the
    mov [wr_size], al               ; 4.77MHz 8088 (SPEC.md 78), and Large is
                                    ; there to push past it and watch the
                                    ; number in the strip move
.redraw:
    call wr_paint                   ; SI is still the window
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; wr_about - the OSAPI_ABOUT_SET handler (SPEC.md 78.7, slot 0x01E0)
; in:  SI = our window; the UI task, gfx lock HELD, far-called at our segment -
;      a window callback in every respect that matters
; out: nothing; preserves all registers
;
; STATE AND NOT A MODAL LOOP, which is calc's cal_about and solitaire's before
; it: [wr_abon] goes up, the card is drawn, and the next click or menu pick
; takes it down. A loop would hold the gfx lock for as long as the reader left
; the credits up.
;
; The point is sharper here than in either precedent, because this package has
; a BACKGROUND TASK: wr_render reads the same flag under the same lock and
; draws nothing while it is set, so the card is not painted over by the figure
; it covered a tick later.
; -----------------------------------------------------------------------------
wr_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call wr_geom                    ; the window may have moved since the last
    mov byte [wr_abon], 1           ; frame, so the card is placed from the
    mov byte [wr_shown], 0          ; LIVE box - and it covers every edge that
    call wr_abdraw                  ; was on the glass, so there is nothing
    pop di                          ; left for the next frame to erase
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_abdraw - the credit card, over the object area
; in:  the geometry derived; the gfx lock held
; out: nothing; clobbers everything but the segments
;
; The OBJECT AREA and not the whole content: the strip below it is still true,
; and drawing it again would be a second layer over pixels that did not change
; (PERFORMANCE.md rule 1) - the very thing 78.6 just took out of the strip.
; -----------------------------------------------------------------------------
wr_abdraw:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wr_ox0]
    mov bx, [wr_oy0]
    mov cx, ax
    add cx, [wr_ow]
    dec cx
    mov dx, bx
    add dx, [wr_oh]
    dec dx
    call OSAPI_GFX_FILL
    mov dx, [wr_ccy]                ; three 8-pixel rows, centred on the box
    sub dx, 12
    mov si, wr_ab1
    call wr_abline
    add dx, 12
    mov si, wr_ab2
    call wr_abline
    add dx, 10
    mov si, wr_ab3
    call wr_abline
    ret

; wr_abline - one CENTRED row of the card, in the 8x8 face
; in:  SI = NUL string, DX = its text row; the gfx lock held
; out: nothing; SI and DX survive, AX/CX/DI do not
;
; Centred on the cell grid rather than on the pixel: SPEC.md 6.1's single-store
; path wants x a multiple of 8, and half a cell of asymmetry in a credit line
; is not worth two adapters' worth of shifting.
wr_abline:
    push si
    push dx
    mov di, si
    xor cx, cx
.len:
    cmp byte [di], 0
    je .got
    inc di
    inc cx
    jmp short .len
.got:
    add cx, cx                      ; half the run's width: 4 pixels a cell
    add cx, cx
    mov ax, [wr_ccx]
    sub ax, cx
    add ax, 7
    and ax, -8
    mov cx, ax
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop dx
    pop si
    ret

; -----------------------------------------------------------------------------
; wr_onclick - W_ONCLICK: the credit card's only way down (SPEC.md 13, 78.7)
; in:  CX/DX = the point, SI = our window; the gfx lock held, ES = KERNEL_SEG
; out: nothing; preserves all registers
;
; There is nothing else in this window to click, so with the card down this is
; a compare and a return - which is what it costs on every frame the reader is
; only watching.
; -----------------------------------------------------------------------------
wr_onclick:
    cmp byte [wr_abon], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call wr_abdown
    call wr_paint                   ; SI is still the window, which is what
    pop bp                          ; wr_paint takes
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; wr_abdown - take the credit card down, and re-anchor the frame counter
; preserves all registers
;
; The counter matters: nothing is rendered while the card is up (78.7), so the
; frames-over-ticks window that was open when it went up would come back with
; a real elapsed time and almost no frames in it - one wrong number in the
; strip, for a second, blamed on whatever the reader did next.
wr_abdown:
    push ax
    mov byte [wr_abon], 0
    call OSAPI_GET_TICKS
    mov [wr_ft0], ax
    mov word [wr_frames], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_pick - point wr_vp/wr_ep/wr_nv/wr_ne at [wr_shape]'s record
; preserves all registers
; -----------------------------------------------------------------------------
wr_pick:
    push ax
    push bx
    mov al, [wr_shape]
    xor ah, ah
    mov bx, ax
    add ax, ax
    add ax, ax
    add ax, bx
    add ax, bx                      ; 6 bytes a record
    mov bx, ax
    mov ax, [wr_shapes + bx]
    mov [wr_vp], ax
    mov ax, [wr_shapes + bx + 2]
    mov [wr_ep], ax
    mov al, [wr_shapes + bx + 4]
    mov [wr_nv], al
    mov al, [wr_shapes + bx + 5]
    mov [wr_ne], al
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) --------------------------
wr_tpl:
    dw 96, 32, WR_W, WR_H
    dw wr_ttl, wr_paint, 0, wr_onclick   ; no onkey; the click is the credit
                                    ; card's dismiss and nothing else (78.7)

; --- the app menu set (SPEC.md 12.2) ------------------------------------------
    OS88_MENUSET wr_menus, wr_name, wr_oncmd
        OS88_MENU wr_m_shape, wr_i_shape, WR_NSHAPE
        OS88_MENU wr_m_view,  wr_i_view,  3
        OS88_MENU wr_m_draw,  wr_i_draw,  4
    OS88_MENUSET_END wr_menus

wr_name:    db 'Wire', 0
wr_m_shape: db 'Shape', 0
wr_i_shape: dw wr_it_cube, wr_it_oct, wr_it_tet
wr_it_cube: db 'Cube', 0
wr_it_oct:  db 'Octahedron', 0
wr_it_tet:  db 'Tetrahedron', 0
wr_m_view:  db 'View', 0
wr_i_view:  dw wr_it_sm, wr_it_md, wr_it_lg
wr_it_sm:   db 'Small', 0
wr_it_md:   db 'Medium', 0
wr_it_lg:   db 'Large', 0
wr_m_draw:  db 'Draw', 0
wr_i_draw:  dw wr_it_whole, wr_it_pair, wr_it_rep, wr_it_comp
wr_it_whole: db 'Whole figure', 0
wr_it_pair:  db 'Edge at a time', 0
wr_it_rep:   db 'Edge, then repair', 0
wr_it_comp:  db 'Composed', 0

wr_eighths: db 3, 4, 6          ; Small, Medium, Large
wr_ttl:     db 'Wireframe', 0
wr_s_lines: db ' lines  ', 0    ; EIGHT, and WR_FPSCOL counts on it
wr_s_fps:   db ' fps', 0
wr_ab1:     db 'Wireframe', 0   ; the credit card (SPEC.md 78.7)
wr_ab2:     db 'Contributed by', 0
wr_ab3:     db 'Elendilon', 0

; --- the shapes ----------------------------------------------------------------
; Vertices are three SIGNED BYTES each and every magnitude here is 48 or 64;
; wr_geom's 1.75 scale is derived from the 48, so a new shape wanting a bigger
; one has to say so there too.
WR_NSHAPE   equ 3

wr_shapes:
    dw wr_cube_v, wr_cube_e
    db 8, 12
    dw wr_oct_v,  wr_oct_e
    db 6, 12
    dw wr_tet_v,  wr_tet_e
    db 4, 6

wr_cube_v:
    db -48,-48,-48
    db  48,-48,-48
    db  48, 48,-48
    db -48, 48,-48
    db -48,-48, 48
    db  48,-48, 48
    db  48, 48, 48
    db -48, 48, 48
wr_cube_e:
    db 0,1, 1,2, 2,3, 3,0
    db 4,5, 5,6, 6,7, 7,4
    db 0,4, 1,5, 2,6, 3,7

wr_oct_v:
    db  64,  0,  0
    db -64,  0,  0
    db   0, 64,  0
    db   0,-64,  0
    db   0,  0, 64
    db   0,  0,-64
wr_oct_e:
    db 0,2, 0,3, 0,4, 0,5
    db 1,2, 1,3, 1,4, 1,5
    db 2,4, 4,3, 3,5, 5,2

wr_tet_v:
    db  48, 48, 48
    db  48,-48,-48
    db -48, 48,-48
    db -48,-48, 48
wr_tet_e:
    db 0,1, 0,2, 0,3, 1,2, 1,3, 2,3

; --- sin(2*pi*i/256) * 127, signed. cos(a) is sin(a+64). ----------------------
wr_sintab:
%include "wiresin.inc"

    OS88_BSS 184 + WR_BSZ
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) ------------------------------------
; ZERO IS A WORKING STATE for all of it but the shape pointers and the size,
; which wr_first fixes before anything reads them.
wr_win      equ os88_image_end + 0    ; word
wr_hired    equ os88_image_end + 2    ; byte
wr_shown    equ os88_image_end + 3    ; byte: is there a frame to erase?
wr_shape    equ os88_image_end + 4    ; byte
wr_size     equ os88_image_end + 5    ; byte: quarters, 2..4
wr_a        equ os88_image_end + 6    ; byte: the two angles, 256 to the turn
wr_b        equ os88_image_end + 7    ; byte
wr_due      equ os88_image_end + 8    ; word
wr_ft0      equ os88_image_end + 10   ; word: the fps window's start tick
wr_frames   equ os88_image_end + 12   ; word
wr_fps      equ os88_image_end + 14   ; word: TENTHS
wr_ox0      equ os88_image_end + 16   ; word: the object area
wr_oy0      equ os88_image_end + 18
wr_ow       equ os88_image_end + 20
wr_oh       equ os88_image_end + 22
wr_ccx      equ os88_image_end + 24   ; word: its centre
wr_ccy      equ os88_image_end + 26
wr_sy       equ os88_image_end + 28   ; word: the strip's text row
wr_scale    equ os88_image_end + 30   ; byte
wr_rx       equ os88_image_end + 31   ; byte: wr_rot's two outputs
wr_ry       equ os88_image_end + 32
wr_sina     equ os88_image_end + 33   ; byte: this frame's four sines
wr_cosa     equ os88_image_end + 34
wr_sinb     equ os88_image_end + 35
wr_cosb     equ os88_image_end + 36
wr_nv       equ os88_image_end + 37   ; byte
wr_ne       equ os88_image_end + 38   ; byte
wr_mode     equ os88_image_end + 39   ; byte: 78.5's draw order; wr_entry sets
                                  ; it to 1, `Edge at a time`
wr_vp       equ os88_image_end + 40   ; word
wr_ep       equ os88_image_end + 42   ; word
wr_px       equ os88_image_end + 44   ; WR_MAXV words: this frame
wr_py       equ os88_image_end + 60
wr_ex       equ os88_image_end + 76   ; ...and the one on the glass
wr_ey       equ os88_image_end + 92
wr_line     equ os88_image_end + 108  ; the strip's composed text, 32 bytes -
                                  ; and wr_fpsdraw's four-cell field, which is
                                  ; never live at the same time (78.6)
wr_abon     equ os88_image_end + 140  ; byte: the credit card is up (78.7)
wr_bx0      equ os88_image_end + 142  ; SPEC.md 78.8: the band's origin, and
wr_by0      equ os88_image_end + 144  ; the rows of it this window has
wr_bh       equ os88_image_end + 146
wr_mx1      equ os88_image_end + 148  ; wr_mline's endpoints, in the mask
wr_my1      equ os88_image_end + 150
wr_mx2      equ os88_image_end + 152
wr_my2      equ os88_image_end + 154
wr_mdx      equ os88_image_end + 156
wr_mdy      equ os88_image_end + 158
wr_msy      equ os88_image_end + 160  ; the row step, which carries the sign
wr_me1      equ os88_image_end + 162
wr_me2      equ os88_image_end + 164
wr_bw       equ os88_image_end + 166  ; SPEC.md 78.8.1: the band's WIDTH, which
                                  ; is the figure's and not the window's
wr_bmnx     equ os88_image_end + 168  ; the union bounds wr_bspan folds into
wr_bmxx     equ os88_image_end + 170
wr_bmny     equ os88_image_end + 172
wr_bmxy     equ os88_image_end + 174
wr_gbx0     equ os88_image_end + 176  ; SPEC.md 78.8.2: the band that is ON THE
wr_gby0     equ os88_image_end + 178  ; GLASS, saved by wr_put once BLIT1 has
wr_gbh      equ os88_image_end + 180  ; taken it - and wr_gbw = 0 says the frame
wr_gbw      equ os88_image_end + 182  ; there was drawn with kernel lines
wr_mask     equ os88_image_end + 184  ; ...and the band itself
