; =============================================================================
; THE MAP (SPEC.md 88.16) - M in flight, the way Flight Simulator 1 had one
;
; North up and centred on the aeroplane, at two scales: M steps OFF -> NEAR ->
; FAR -> OFF. It is the same world drawn by the same raster through the same
; face loop, from straight above:
;
;   - THE GROUND is cs_skyground's, with the world's up vector set to point
;     straight back at the eye, so every row is the ground side of a horizon
;     that is not in the view. That is the whole erase: the sky/ground pass
;     already refills exactly what the last frame drew and nothing else, so
;     the map costs a 3D frame's erase and not a full-view fill.
;   - THE OBJECTS are projected by a SHIFT, not a matrix - x right and z up
;     the screen, in 2^CS_MAPSH metres a pixel, with the view's own pixel
;     aspect off cs_sclx/cs_scly - into the very buffers cs_projall fills,
;     and then cs_faces and cs_edges draw them. Every vertex is "in front and
;     inside" (cs_fv = 1), so nothing is near-clipped.
;   - THE EYE IS ABOVE EVERYTHING, which is what cs_axcull is told: a cap
;     faces it and a vertical wall is edge-on and culled, while a TAPERED face
;     - a pyramid's, a hill's - is left to the winding test and faces up. So a
;     hill is its facets in their own inks on the map, and a building its
;     roof, for no code of the map's own.
;   - THE AEROPLANE is a filled arrowhead on the centre, along its heading.
;
; Flat things go first and solids second, as cs_scene does, and the Detail
; Level's ladder (CS_LADDER) thins the map exactly as it thins the view.
; =============================================================================

CS_MAPSH1  equ 4                    ; NEAR: 16 m a pixel
CS_MAPSH2  equ 6                    ; FAR: 64 m a pixel
CS_MAPAL   equ 9                    ; the arrowhead: tip, in pixels...
CS_MAPAB   equ 6                    ; ...and its two back corners
CS_MAPAW   equ 0x6AAB               ; ...at 150 degrees either side of it

; -----------------------------------------------------------------------------
; cs_mapview - the frame's view, as the map. In: cs_r_begin done, [cs_ex] and
; [cs_ez] the eye. Clobbers everything
; -----------------------------------------------------------------------------
cs_mapview:
    ; --- THE GROUND: up points at the eye, so the horizon is nowhere ------
    push word [cs_nx]
    push word [cs_ny]
    push word [cs_nz]
    xor ax, ax
    mov [cs_nx], ax
    mov [cs_ny], ax
    mov word [cs_nz], -32767
    call cs_skyground
    pop word [cs_nz]
    pop word [cs_ny]
    pop word [cs_nx]
    ; --- THE SCALE, and the view's half-extents in it ----------------------
    mov cl, CS_MAPSH1
    cmp byte [cs_map], 1
    je .sh
    mov cl, CS_MAPSH2
.sh:
    mov [cs_mapsh], cl
    mov ax, [cs_scly]               ; ky = 256 scly / sclx: a metre of z is
    mov dl, ah                      ; this many 256ths of a metre of x on the
    mov ah, al                      ; glass, which is the pixel's own shape
    xor al, al
    xor dh, dh
    div word [cs_sclx]
    mov [cs_mapky], ax
    mov ax, [cs_vcx]
    sub ax, [cs_wx0]
    mov [cs_maphw], ax              ; half the view across, in pixels...
    mov ax, [cs_vcy]                ; ...and down, in pixels of x's size
    mov dl, ah
    mov ah, al
    xor al, al
    xor dh, dh
    div word [cs_mapky]
    mov [cs_maphz], ax
    ; --- THE EYE, ABOVE EVERYTHING (cs_axcull's three words) ---------------
    xor ax, ax
    mov [cs_odx], ax
    mov [cs_odz], ax
    mov word [cs_ody], -32000
    mov [cs_pshr], al
    call cs_mapair                  ; a sailplane's lift, under everything
    mov byte [cs_pass], CSM_FLAT
    call .pass
    mov byte [cs_pass], CSM_STACK
    call .pass
    jmp cs_mapplane
.pass:
    mov si, cs_rwobj                ; the runway first, as cs_scene does
    call cs_mapobj
    mov bx, [cs_airport]
    mov si, [bx + CSA_OBJS]
    mov cx, [bx + CSA_NOBJ]
    jcxz .done
.o:
    push cx
    push si
    call cs_mapobj
    call cs_sound_step              ; the engine between objects, for
    pop si                          ; cs_drawpass's reason (88.8.2.1.2)
    pop cx
    add si, CSO_SIZE
    loop .o
.done:
    ret

; -----------------------------------------------------------------------------
; cs_mapobj - one object, from above. SI = the object. Clobbers everything
; -----------------------------------------------------------------------------
cs_mapobj:
    CS_LADDER .out, .kept
.kept:
    mov di, [si + CSO_MODEL]
    mov al, [di + CSM_TYPE]
    cmp al, [cs_pass]
    jne .out
    mov [cs_obj], si
    mov [cs_mdl], di
    mov ax, [si + CSO_X]            ; --- the cull: its centre and radius
    sub ax, [cs_ex]                 ;     against the view, in pixels
    jo .out
    mov [cs_ddx], ax
    mov ax, [si + CSO_Z]
    sub ax, [cs_ez]
    jo .out
    mov [cs_ddz], ax
    mov cl, [cs_mapsh]
    mov bx, [di + CSM_RAD]
    shr bx, cl                      ; UNDER A PIXEL: not drawn, the view's own
    jz .out                         ; rule (88.5.4) - at FAR it is most of a
    inc bx                          ; city, and a dot each would cost a polygon
                                    ; BX = the radius in pixels, rounded up
    mov ax, [cs_ddx]
    sar ax, cl
    call cs_wabs
    sub ax, bx
    cmp ax, [cs_maphw]
    jg .out
    mov ax, [cs_ddz]
    sar ax, cl
    call cs_wabs
    sub ax, bx
    cmp ax, [cs_maphz]
    jg .out
    ; --- every vertex, by a shift ------------------------------------------
    mov si, [di + CSM_VERTS]
    mov bl, [di + CSM_NV]
    xor bh, bh
    xor di, di                      ; DI = the vertex index x 2
    cmp byte [cs_pass], CSM_FLAT
    jne .stack
    mov [cs_nv], bx
    mov bp, bx
.fv:
    mov ax, [si]
    mov dx, [si + 2]
    call cs_mapvert
    add si, 4
    dec bp
    jnz .fv
    jmp short .draw
.stack:
    mov bp, bx                      ; levels, four vertices each, in
    shl bx, 1                       ; cs_stackverts' order: +x+z, -x+z,
    shl bx, 1                       ; -x-z, +x-z
    mov [cs_nv], bx
.lv:
    mov ax, [si]                    ; +wx, +wz
    mov dx, [si + 4]
    call cs_mapvert
    mov ax, [si]                    ; -wx, +wz
    neg ax
    mov dx, [si + 4]
    call cs_mapvert
    mov ax, [si]                    ; -wx, -wz
    neg ax
    mov dx, [si + 4]
    neg dx
    call cs_mapvert
    mov ax, [si]                    ; +wx, -wz
    mov dx, [si + 4]
    neg dx
    call cs_mapvert
    add si, 6
    dec bp
    jnz .lv
.draw:
    ; --- ...and drawn by the view's own face loop, its marks the object's --
    xor ax, ax
    mov [cs_pwhole], al
    mov [cs_pinview], al
    call cs_mapbox
    call cs_wireclr
    call cs_faces
    mov si, [cs_mdl]                ; the outline is a LINE model's, or a flat
    cmp byte [si + CSM_TYPE], CSM_FLAT
    je .edg                         ; one's (roads, the runway's edges): a
    cmp byte [si + CSM_NF], 0       ; building's would be its roof drawn twice
    jne .mark
.edg:
    call cs_edges
.mark:
    jmp cs_mapmark
.out:
    ret

; cs_mappoly - CX corners at SI, filled in [cs_ink] by cs_poly and their
;              rows marked for the next frame's erase. Clobbers everything
cs_mappoly:
    mov byte [cs_pinview], 0
    mov byte [cs_pwhole], 0
    call cs_mapbox
    call cs_poly
    ; --- cs_mapmark - the box the fills grew, to the next frame's erase -----
cs_mapmark:
    mov si, [cs_oby0]
    mov di, [cs_oby1]
    cmp si, di
    jg .out
    mov ax, [cs_oblo]
    mov [cs_mklo], ax
    call cs_markrows
.out:
    ret

; cs_mapbox - an empty box for the fills to grow. Preserves every register
cs_mapbox:
    mov word [cs_oby0], 0x7FFF
    mov word [cs_oby1], -1
    mov word [cs_oblo], 0x00FF
    ret

; cs_mapvert - AX, DX = a vertex's x and z from the object's origin; into
;              cs_sxv/cs_syv/cs_fv at DI, which steps. CL = the scale.
;              Clobbers AX, DX
cs_mapvert:
    add ax, [cs_ddx]
    sar ax, cl
    add ax, [cs_vcx]
    mov [cs_sxv + di], ax
    xchg ax, dx
    add ax, [cs_ddz]
    call cs_mapy
    mov [cs_syv + di], ax
    mov word [cs_fv + di], 1        ; in front and inside: never clipped
    inc di
    inc di
    ret

; cs_mapy - AX = metres of z from the eye; out AX = the row. CL = the scale.
;           Clobbers DX
cs_mapy:
    sar ax, cl
    imul word [cs_mapky]
    mov al, ah
    mov ah, dl
    neg ax                          ; north is UP the glass
    add ax, [cs_vcy]
    ret

; -----------------------------------------------------------------------------
; cs_mapair - IN A SAILPLANE, THE LIFT (SPEC.md 88.16.1). Clobbers everything
;
; Every rect of cs_lifts that LIFTS, in every tile the view reaches, as a
; quarter-dense dither of CSI_LIFT under the world - so a pilot who cannot
; find a thermal can look for one, and an aeroplane with an engine, which the
; air does not move (88.7.6.3), is not shown weather it cannot use. Sink is
; not drawn: it is what a pilot is told by the variometer and the swoop, and
; a map of where NOT to go doubles the polygons for half the use.
;
; A rect is a box on a north-up map, so it is filled by rows (cs_maptile),
; and the tile's variant is the INVERSE of cs_airtile's - the two mirrors
; first, then the transpose - which puts each rect where the air is.
; -----------------------------------------------------------------------------
CS_MAPRM   equ 640                  ; the widest rect's half-extent, either way

cs_mapair:
    mov di, [cs_plane]
    cmp word [di + CSP_THRUST], 0
    jne .out
    mov al, CSI_LIFT
    call cs_setink
    ; --- the tiles the view reaches, in the tile's frame (cs_airtile's) ---
    mov cl, [cs_mapsh]
    mov ax, [cs_maphw]
    shl ax, cl
    add ax, CS_MAPRM
    mov bx, [cs_ex]
    sub bx, [cs_airx]
    add bx, CS_AIRTILE / 2
    mov [cs_maex], bx
    call .rng
    mov [cs_matx0], si
    mov [cs_matx1], di
    mov cl, [cs_mapsh]
    mov ax, [cs_maphz]
    shl ax, cl
    mov [cs_mahz], ax               ; (the cull's, in metres)
    add ax, CS_MAPRM
    mov bx, [cs_ez]
    sub bx, [cs_airz]
    add bx, CS_AIRTILE / 2
    mov [cs_maez], bx
    call .rng
    mov [cs_matz], si
    mov [cs_matz1], di
    call cs_mapbox                  ; ...and ONE mark for every box, at the end
.tz:
    mov ax, [cs_matx0]
    mov [cs_matx], ax
.tx:
    call cs_maptile
    call cs_sound_step              ; the swoop between tiles, for
    inc word [cs_matx]              ; cs_drawpass's reason (88.8.2.1.2)
    mov ax, [cs_matx]
    cmp ax, [cs_matx1]
    jle .tx
    inc word [cs_matz]
    mov ax, [cs_matz]
    cmp ax, [cs_matz1]
    jle .tz
    jmp cs_mapmark
.out:
    ret
.rng:                               ; AX = half the view, BX = its centre:
    mov cl, 12                      ; out SI, DI = the first and last tile
    mov si, bx
    sub si, ax
    sar si, cl
    mov di, bx
    add di, ax
    sar di, cl
    ret

; cs_maptile - tile ([cs_matx], [cs_matz])'s lift. Clobbers everything
cs_maptile:
    mov al, [cs_matx]               ; cs_airtile's variant
    mov bl, [cs_matz]
    call cs_airvar
    mov [cs_mavar], al
    mov cl, 12                      ; the tile's corner, from the eye
    mov ax, [cs_matx]
    shl ax, cl
    sub ax, [cs_maex]
    mov [cs_mabx], ax
    mov ax, [cs_matz]
    shl ax, cl
    sub ax, [cs_maez]
    mov [cs_mabz], ax
    mov si, cs_lifts
    mov cx, CS_NLIFT
.r:
    push cx
    push si
    cmp word [si + CSAIR_RATE], 0
    jle .n
    mov ax, [si + CSAIR_DX]
    mov bx, [si + CSAIR_DZ]
    mov cx, [si + CSAIR_HW]
    mov dx, [si + CSAIR_HD]
    test byte [cs_mavar], 4         ; the inverse: the mirrors...
    jz .fz
    neg bx
    add bx, CS_AIRTILE
.fz:
    test byte [cs_mavar], 2
    jz .fx
    neg ax
    add ax, CS_AIRTILE
.fx:
    test byte [cs_mavar], 1         ; ...then the transpose
    jz .t
    xchg ax, bx
    xchg cx, dx
.t:
    add ax, [cs_mabx]               ; AX, BX = the centre from the eye
    add bx, [cs_mabz]
    mov si, ax
    mov di, cx                      ; SI +- DI across, BX +- BP up
    mov bp, dx
    mov cl, [cs_mapsh]
    sub ax, di                      ; --- the west side, culled and clamped
    sar ax, cl
    add ax, [cs_vcx]
    cmp ax, [cs_wx1]
    jg .n
    cmp ax, [cs_wx0]
    jge .x0
    mov ax, [cs_wx0]
.x0:
    mov [cs_rx1], ax
    mov ax, si                      ; --- ...the east
    add ax, di
    sar ax, cl
    add ax, [cs_vcx]
    cmp ax, [cs_wx0]
    jl .n
    cmp ax, [cs_wx1]
    jle .x1
    mov ax, [cs_wx1]
.x1:
    mov [cs_rx2], ax
    mov ax, bx                      ; --- above or below the view, in metres,
    sub ax, bp                      ; before the multiplies
    cmp ax, [cs_mahz]
    jg .n
    mov ax, bx
    add ax, bp
    neg ax
    cmp ax, [cs_mahz]
    jg .n
    mov ax, bx                      ; --- the north, which is the top...
    add ax, bp
    call cs_mapy
    mov si, ax
    mov ax, bx                      ; ...and the south
    sub ax, bp
    call cs_mapy
    mov di, ax
    or di, di
    js .n
    cmp si, [cs_wh]
    jge .n
    or si, si
    jns .y0
    xor si, si
.y0:
    mov ax, [cs_wh]
    dec ax
    cmp di, ax
    jle .y1
    mov di, ax
.y1:
    ; --- ITS ROWS TO THE ONE MARK, and then the box, EVEN ROWS ONLY: the odd
    ;     ones keep the ground the sky/ground pass laid, so the dither is
    ;     see-through on every adapter and costs half the rows. A box is all
    ;     cs_poly would trace for it, at a fifth of the cycles, and a mark a
    ;     box was a fifth of what was left - at FAR the boxes cover the view's
    ;     width anyway, so the union's bytes are what the rows' would be
    mov cl, [cs_kshift]
    mov ax, [cs_rx1]
    shr ax, cl
    mov [cs_mklo], al
    mov ax, [cs_rx2]
    shr ax, cl
    mov [cs_mkhi], al
    call cs_markacc
    inc si                          ; the first EVEN row
    and si, ~1
.row:
    cmp si, di
    jg .n
    push si
    push di
    mov ax, [cs_rx1]
    mov cx, [cs_rx2]
    mov bx, si
    call cs_hrun
    pop di
    pop si
    inc si
    inc si
    jmp short .row
.n:
    pop si
    pop cx
    add si, CSAIR_SIZE
    dec cx
    jz .done
    jmp .r
.done:
    ret

; -----------------------------------------------------------------------------
; cs_mapplane - the aeroplane: an arrowhead on the centre, along the heading.
; Clobbers everything
; -----------------------------------------------------------------------------
cs_mapplane:
    mov di, cs_pv
    mov ax, [cs_hdg]
    mov cx, CS_MAPAL
    call .pt
    mov ax, [cs_hdg]
    add ax, CS_MAPAW
    mov cx, CS_MAPAB
    call .pt
    mov ax, [cs_hdg]
    sub ax, CS_MAPAW
    mov cx, CS_MAPAB
    call .pt
    mov ax, [cs_pv + 4]             ; THE WINDING, off the triangle's own
    sub ax, [cs_pv]                 ; signed area: screen y down, so a
    mov bx, [cs_pv + 10]            ; negative one has its left edges
    sub bx, [cs_pv + 2]             ; descending (cs_faces' convention)
    imul bx
    mov cx, ax
    mov si, dx
    mov ax, [cs_pv + 6]
    sub ax, [cs_pv + 2]
    mov bx, [cs_pv + 8]
    sub bx, [cs_pv]
    imul bx
    sub cx, ax
    sbb si, dx
    mov byte [cs_pwind], 0
    or si, si
    js .w
    mov byte [cs_pwind], 1
.w:
    mov al, CSI_PHI                 ; the panel's warning ink: the one thing
    call cs_setink                  ; on the map that is not the world
    mov si, cs_pv
    mov cx, 3
    jmp cs_mappoly
.pt:                                ; AX = a bearing, CX = pixels; into [DI]
    push ax
    call cs_sin
    mov bx, cx
    MUL14
    add ax, [cs_vcx]
    mov [di], ax
    pop ax
    call cs_cos
    mov bx, cx
    MUL14                           ; ...along z, so the pixel's aspect too
    imul word [cs_mapky]
    mov al, ah
    mov ah, dl
    neg ax
    add ax, [cs_vcy]
    mov [di + 2], ax
    add di, 4
    ret
