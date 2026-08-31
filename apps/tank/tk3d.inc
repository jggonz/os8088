; =============================================================================
; os8088 - apps/tank/tk3d.inc
;
; THE GEOMETRY (SPEC.md 85.5). Fixed point throughout, 8086 only, and the two
; expensive instructions on this machine - imul at ~140 clocks and idiv at
; ~170 - are what the whole shape of this file is arranged around.
;
; ANGLES ARE BYTES. 256 units to the turn, so a heading wraps by being a byte
; and the sine table is indexed with no masking at all; cos(a) is sin(a+64).
; tksin.inc is sin * 16384, which is the largest scale that keeps a signed
; word, and MUL14 below is the matching multiply.
;
; THE WORLD IS A TORUS 8192 UNITS ROUND and the wrap is three instructions:
; mask a difference to 13 bits, fold the top half negative, and every
; distance in the game is already the short way round. There is no "which
; copy of the world is nearer" anywhere in this file.
;
; ONE ROTATION, NOT TWO. A vertex has to be turned by the object's heading and
; then by the negative of the camera's; those compose, so it is turned once by
; the DIFFERENCE and the object's origin is translated in camera space
; afterwards. That halves the multiplies, and it is what makes the shape cache
; below possible: every cube in the world has the same relative angle, so the
; eight rotated offsets are computed ONCE a frame and every cube instance is
; then two adds a vertex.
; =============================================================================

; AX = (AX * BX) >> 14, signed. Clobbers DX. The shift is done by moving the
; 32-bit product LEFT two and keeping DX, which is four cheap instructions
; against the ten a right shift by 14 would take.
%macro MUL14 0
    imul bx
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov ax, dx
%endmacro

; -----------------------------------------------------------------------------
; tk_sincos - AL = angle; out AX = sin, BX = cos.  Clobbers nothing else.
; -----------------------------------------------------------------------------
tk_sincos:
    push si
    xor ah, ah
    mov si, ax
    shl si, 1
    mov bx, [tk_sintab + si]        ; sin
    add al, 64
    xor ah, ah
    mov si, ax
    shl si, 1
    mov ax, [tk_sintab + si]        ; cos
    xchg ax, bx
    pop si
    ret

; -----------------------------------------------------------------------------
; tk_camsetup - the camera's sine and cosine, once a frame
; -----------------------------------------------------------------------------
tk_camsetup:
    push ax
    push bx
    mov al, [tk_pa]
    call tk_sincos
    mov [tk_csin], ax
    mov [tk_ccos], bx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_pdiv - AX = a projected offset: (DX:AX) / BX, clamped to +-4000
; in:  DX:AX = a signed 32-bit product, BX = the depth (positive, >= TK_NEARZ)
; out: AX.  clobbers CX, DX, SI
;
; THE CLAMP IS NOT COSMETIC. idiv raises int 0 when the quotient will not fit
; a word, and a vertex a hand's breadth past the near plane projects to tens
; of thousands - so the overflow case is the NORMAL one for anything close,
; and an unguarded idiv here is a machine that reboots when a tank gets near.
; The test is |DX| < BX/2, which is sufficient because |DX:AX| < (|DX|+1)*65536.
; -----------------------------------------------------------------------------
tk_pdiv:
    mov cx, bx
    shr cx, 1
    mov si, dx
    or si, si
    jns .p
    not si
.p:
    cmp si, cx
    jb .safe
    or dx, dx
    js .neg
    mov ax, 4000
    ret
.neg:
    mov ax, -4000
    ret
.safe:
    idiv bx
    cmp ax, 4000
    jle .lo
    mov ax, 4000
    ret
.lo:
    cmp ax, -4000
    jge .done
    mov ax, -4000
.done:
    ret

; -----------------------------------------------------------------------------
; tk_project - one camera-space point onto the viewport
; in:  AX = cx, BX = cy, CX = cz (cz >= TK_NEARZ)
; out: AX = screen x, BX = screen y (viewport-relative, clamped)
; clobbers CX, DX, SI, DI
; -----------------------------------------------------------------------------
tk_project:
    push cx
    mov di, bx                      ; keep cy
    mov bx, [tk_sclx]
    imul bx                         ; DX:AX = cx * sclx
    pop bx                          ; BX = cz
    push bx
    call tk_pdiv
    add ax, [tk_vcx]
    push ax
    mov ax, di
    mov bx, [tk_scly]
    imul bx
    pop di                          ; DI = screen x
    pop bx                          ; BX = cz
    call tk_pdiv
    neg ax                          ; screen y grows DOWNWARD
    add ax, [tk_vcy]
    mov bx, ax
    mov ax, di
    ret

; -----------------------------------------------------------------------------
; tk_wrap - AX = a world-axis difference; out AX = it, the short way round
; -----------------------------------------------------------------------------
tk_wrap:
    and ax, TK_WMASK
    cmp ax, TK_WHALF
    jb .o
    sub ax, TK_WORLD
.o:
    ret

; -----------------------------------------------------------------------------
; tk_objcam - where object BX (a slot index x2) stands, in camera space
; out: [tk_ocx], [tk_ocz]; CF=1 the object is not worth drawing
; clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
tk_objcam:
    call tk_objcam_raw
    mov cx, [tk_ocz]
    cmp cx, TK_FARZ                 ; too far to be worth a vertex
    jg .no
    cmp cx, -600                    ; wholly behind the eye
    jl .no
    mov ax, [tk_ocx]                ; ...and roughly outside the cone: the
    or ax, ax                       ; half-width at depth z is 0.577z, so
    jns .abs                        ; z + 600 keeps every marginal case
    neg ax
.abs:
    mov dx, cx
    add dx, 600
    cmp ax, dx
    jg .no
    clc
    ret
.no:
    stc
    ret

; tk_objcam_raw - the same, with no cull: the radar needs objects the frustum
; has thrown away, which is the whole point of having a radar
tk_objcam_raw:
    mov ax, [tk_ox + bx]
    sub ax, [tk_px]
    call tk_wrap
    mov si, ax                      ; SI = rx
    mov ax, [tk_oz + bx]
    sub ax, [tk_pz]
    call tk_wrap
    mov di, ax                      ; DI = rz

    push bx
    mov ax, si                      ; ocz = rx*sin + rz*cos
    mov bx, [tk_csin]
    MUL14
    mov cx, ax
    mov ax, di
    mov bx, [tk_ccos]
    MUL14
    add ax, cx
    mov [tk_ocz], ax
    mov cx, ax

    mov ax, si                      ; ocx = rx*cos - rz*sin
    mov bx, [tk_ccos]
    MUL14
    mov si, ax
    mov ax, di
    mov bx, [tk_csin]
    MUL14
    neg ax
    add ax, si
    mov [tk_ocx], ax
    pop bx
    ret

; -----------------------------------------------------------------------------
; tk_rotshape - turn a model's vertices by ONE relative angle, once
; in:  [tk_mdl] = the model, AL = the object's heading minus the camera's
; out: tk_rotx/tk_roty/tk_rotz filled, indexed by vertex * 2
; clobbers everything
;
; This is the cache the file header talks about. Every cube in the world is at
; the same relative angle, so its eight rotated offsets are computed once and
; each instance then costs two adds a vertex instead of four multiplies.
; -----------------------------------------------------------------------------
tk_rotshape:
    call tk_sincos
    mov [tk_osin], ax
    mov [tk_ocos], bx
    mov si, [tk_mdl]
    mov cl, [si]
    xor ch, ch
    mov si, [si+2]                  ; SI = the vertex table
    xor bp, bp
.v:
    push cx
    mov ax, [si]                    ; rx' = mx*cos + mz*sin
    mov bx, [tk_ocos]
    MUL14
    mov cx, ax
    mov ax, [si+4]
    mov bx, [tk_osin]
    MUL14
    add ax, cx
    mov bx, bp
    mov [tk_rotx + bx], ax
    mov ax, [si+4]                  ; rz' = mz*cos - mx*sin
    mov bx, [tk_ocos]
    MUL14
    mov cx, ax
    mov ax, [si]
    mov bx, [tk_osin]
    MUL14
    neg ax
    add ax, cx
    mov bx, bp
    mov [tk_rotz + bx], ax
    mov ax, [si+2]                  ; y is not turned - there is no pitch and
    sub ax, TK_EYE                  ; no roll in this game
    mov bx, bp
    mov [tk_roty + bx], ax
    add si, 6
    add bp, 2
    pop cx
    loop .v
    ret

; -----------------------------------------------------------------------------
; tk_emit - project [tk_mdl]'s cached vertices about [tk_ocx]/[tk_ocz] and
;           hand every edge to the raster
; clobbers everything
; -----------------------------------------------------------------------------
tk_emit:
    mov si, [tk_mdl]
    mov cl, [si]
    xor ch, ch
    xor bp, bp
.v:
    push cx
    mov bx, bp
    mov cx, [tk_rotz + bx]
    add cx, [tk_ocz]
    mov [tk_czv + bx], cx
    mov ax, [tk_rotx + bx]
    add ax, [tk_ocx]
    mov [tk_cxv + bx], ax
    mov dx, [tk_roty + bx]
    mov [tk_cyv + bx], dx
    cmp cx, TK_NEARZ
    jl .behind
    mov bx, dx
    call tk_project                 ; AX = sx, BX = sy
    mov di, bp
    mov [tk_sxv + di], ax
    mov [tk_syv + di], bx
    mov word [tk_fv + di], 1
    jmp short .next
.behind:
    mov di, bp
    mov word [tk_fv + di], 0
.next:
    add bp, 2
    pop cx
    loop .v

    mov si, [tk_mdl]
    mov cl, [si+1]                  ; the edges
    xor ch, ch
    mov si, [si+4]
.e:
    push cx
    mov al, [si]
    xor ah, ah
    shl ax, 1
    mov [tk_na], ax
    mov al, [si+1]
    xor ah, ah
    shl ax, 1
    mov [tk_nb], ax
    push si
    mov bx, [tk_na]
    mov cx, [tk_fv + bx]
    mov bx, [tk_nb]
    add cx, [tk_fv + bx]
    or cx, cx
    jz .skip                        ; both behind the near plane
    cmp cx, 2
    je .both
    ; --- one end is behind: pull it up to the near plane ------------------
    mov bx, [tk_na]
    cmp word [tk_fv + bx], 0
    jne .aisfront
    mov ax, [tk_na]                 ; make tk_na the one in FRONT
    mov bx, [tk_nb]
    mov [tk_na], bx
    mov [tk_nb], ax
.aisfront:
    call tk_nclip                   ; AX/BX = the clipped end, projected
    mov [tk_ex2], ax
    mov [tk_ey2], bx
    mov bx, [tk_na]
    mov ax, [tk_sxv + bx]
    mov [tk_ex1], ax
    mov ax, [tk_syv + bx]
    mov [tk_ey1], ax
    jmp short .draw
.both:
    mov bx, [tk_na]
    mov ax, [tk_sxv + bx]
    mov [tk_ex1], ax
    mov ax, [tk_syv + bx]
    mov [tk_ey1], ax
    mov bx, [tk_nb]
    mov ax, [tk_sxv + bx]
    mov [tk_ex2], ax
    mov ax, [tk_syv + bx]
    mov [tk_ey2], ax
.draw:
    mov ax, [tk_ex1]
    mov bx, [tk_ey1]
    mov cx, [tk_ex2]
    mov dx, [tk_ey2]
    cmp bx, dx
    jne .slope
    call tk_hseg                    ; SPEC.md 5.6.1's deferral, ours: a flat
    jmp short .skip                 ; edge is a run and never a walk
.slope:
    call tk_seg
.skip:
    pop si
    add si, 2
    pop cx
    dec cx
    jz .done
    jmp .e
.done:
    ret

; -----------------------------------------------------------------------------
; tk_nclip - the point where edge [tk_na]->[tk_nb] crosses the near plane
; in:  [tk_na] = the end in FRONT (x2 index), [tk_nb] = the end behind
; out: AX = screen x, BX = screen y
; -----------------------------------------------------------------------------
tk_nclip:
    mov bx, [tk_nb]
    mov di, [tk_czv + bx]           ; DI = czB (below the near plane)
    mov bx, [tk_na]
    mov si, [tk_czv + bx]           ; SI = czA
    sub si, di                      ; SI = den, strictly positive
    mov cx, TK_NEARZ
    sub cx, di                      ; CX = num, 0 < num <= den
    mov [tk_nnum], cx
    mov [tk_nden], si

    mov bx, [tk_na]                 ; x
    mov ax, [tk_cxv + bx]
    mov bx, [tk_nb]
    sub ax, [tk_cxv + bx]
    mov bx, [tk_nnum]
    imul bx
    idiv word [tk_nden]
    mov bx, [tk_nb]
    add ax, [tk_cxv + bx]
    mov [tk_ncx], ax

    mov bx, [tk_na]                 ; y
    mov ax, [tk_cyv + bx]
    mov bx, [tk_nb]
    sub ax, [tk_cyv + bx]
    mov bx, [tk_nnum]
    imul bx
    idiv word [tk_nden]
    mov bx, [tk_nb]
    add ax, [tk_cyv + bx]

    mov bx, ax
    mov ax, [tk_ncx]
    mov cx, TK_NEARZ
    call tk_project
    ret

; -----------------------------------------------------------------------------
; tk_ridgesetup - the 25 columns the horizon silhouette is sampled at
;
; The ridge is at infinity, so its screen x depends on the ANGLE alone and
; nothing in the frame moves it but a turn. Sampling at fixed angles and
; solving for x once a bracket is what turns "an arctangent per column" into a
; table lookup per frame.
; -----------------------------------------------------------------------------
tk_ridgesetup:
    xor bx, bx
    mov cx, 25
.p:
    push cx
    mov ax, [tk_tantab + bx]
    imul word [tk_sclx]             ; DX:AX = tan * 256 * sclx
    mov al, ah                      ; ...>> 8
    mov ah, dl
    add ax, [tk_vcx]
    mov [tk_ridgex + bx], ax
    add bx, 2
    pop cx
    loop .p
    ret

; -----------------------------------------------------------------------------
; tk_ridge - the horizon and the mountains standing on it
; clobbers everything
; -----------------------------------------------------------------------------
tk_ridge:
    mov al, TKI_HORIZ               ; the line the world stands on, and the
    call tk_setink                  ; ridge behind it, are different distances
    xor ax, ax                      ; the horizon is the viewport's whole
    mov cx, [tk_vw]                 ; width, so it is a RUN and never a walk
    dec cx
    mov bx, [tk_vcy]
    call tk_hseg
    mov al, TKI_RIDGE
    call tk_setink

    mov al, [tk_pa]
    sub al, 24
    mov [tk_ra], al
    xor bx, bx
    mov cx, 25
.pt:
    push cx
    mov al, [tk_ra]
    xor ah, ah
    mov si, ax
    mov al, [tk_ridgetab + si]
    xor ah, ah
    mul word [tk_ridgek]
    mov al, ah
    mov ah, dl                      ; AX = height * k >> 8
    neg ax
    add ax, [tk_vcy]
    mov [tk_rpy + bx], ax
    mov al, [tk_ra]
    add al, 2
    mov [tk_ra], al
    add bx, 2
    pop cx
    loop .pt

    xor bx, bx
    mov cx, 24
.sg:
    push cx
    push bx
    mov ax, [tk_ridgex + bx]
    mov cx, [tk_ridgex + bx + 2]
    push cx
    mov bx, [tk_rpy + bx]
    pop cx
    mov si, bx
    pop bx
    push bx
    mov dx, [tk_rpy + bx + 2]
    mov bx, si
    call tk_seg
    pop bx
    add bx, 2
    pop cx
    loop .sg
    ret

; -----------------------------------------------------------------------------
; tk_modelfor - AL = an OT_*; out [tk_mdl].  preserves every register
; -----------------------------------------------------------------------------
tk_modelfor:
    push ax
    push bx
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov bx, [tk_modtab + bx]
    mov [tk_mdl], bx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_drawtype - every visible instance of one SCENERY type
; in:  AL = the OT_*.  clobbers everything
;
; By type rather than by slot, because that is what makes the rotation cache
; pay: a type's vertices are turned once and every instance of it after that
; is two adds a vertex.
; -----------------------------------------------------------------------------
tk_drawtype:
    mov [tk_dtype], al
    mov byte [tk_rotdone], 0
    xor bx, bx
    mov cx, TK_NOBJ
.o:
    push cx
    push bx
    mov si, bx
    shr si, 1
    mov al, [tk_otype + si]
    cmp al, [tk_dtype]
    jne .next
    call tk_objcam
    jc .next
    cmp byte [tk_rotdone], 0
    jne .have
    mov al, [tk_dtype]
    call tk_modelfor
    xor al, al                      ; scenery does not turn: its relative
    sub al, [tk_pa]                 ; angle is the camera's, negated
    call tk_rotshape
    mov byte [tk_rotdone], 1
.have:
    mov cx, [tk_ocz]                ; the ramp: this instance's own distance
    mov al, TKI_W0
    call tk_depthink
    call tk_setink
    mov al, [tk_dtype]
    call tk_modelfor
    call tk_emit
.next:
    pop bx
    add bx, 2
    pop cx
    loop .o
    ret

; -----------------------------------------------------------------------------
; tk_drawmovers - the tanks and the shells, each turned by its own heading
; clobbers everything
; -----------------------------------------------------------------------------
tk_drawmovers:
    xor bx, bx
    mov cx, TK_NOBJ
.o:
    push cx
    push bx
    mov si, bx
    shr si, 1
    mov al, [tk_otype + si]
    cmp al, OT_TANK
    jb .next
    mov [tk_dtype], al
    call tk_objcam
    jc .next
    mov cx, [tk_ocz]
    mov al, TKI_T0                  ; a tank fades with distance like the
    cmp byte [tk_dtype], OT_TANK    ; scenery, on its own ramp - amber, so it
    je .ramp                        ; is never mistaken for a slab
    mov al, TKI_SHELL               ; ...and a shell in flight is the brightest
    jmp short .ink                  ; ink there is, because the thing about to
.ramp:                              ; kill you is the thing that must stand out
    call tk_depthink
.ink:
    call tk_setink
    mov al, [tk_dtype]
    call tk_modelfor
    mov si, bx
    shr si, 1
    mov al, [tk_oa + si]
    sub al, [tk_pa]
    call tk_rotshape
    call tk_emit
.next:
    pop bx
    add bx, 2
    pop cx
    loop .o
    ret

; =============================================================================
; The models (SPEC.md 85.5.2)
;
; db nverts, db nedges, dw the vertex table, dw the edge table. A vertex is
; three words - x right, y up, z forward - in world units, with y = 0 the
; ground plane; an edge is two vertex indices. Nothing here is a face: this
; is a vector game and a wireframe is what it draws.
; =============================================================================
tk_modtab:
    dw 0, tk_m_cube, tk_m_pyr, tk_m_block, tk_m_tank, tk_m_shell, tk_m_shell

tk_m_cube:  db 8, 12
            dw tk_v_cube, tk_e_box
tk_m_block: db 8, 12
            dw tk_v_block, tk_e_box
tk_m_pyr:   db 5, 8
            dw tk_v_pyr, tk_e_pyr
tk_m_tank:  db 13, 22
            dw tk_v_tank, tk_e_tank
tk_m_shell: db 6, 3
            dw tk_v_shell, tk_e_shell

tk_v_cube:
    dw  -90,   0,  -90
    dw   90,   0,  -90
    dw   90,   0,   90
    dw  -90,   0,   90
    dw  -90, 180,  -90
    dw   90, 180,  -90
    dw   90, 180,   90
    dw  -90, 180,   90

tk_v_block:                         ; the low wide slabs the 1983 port stands
    dw -150,   0,  -60              ; in threes
    dw  150,   0,  -60
    dw  150,   0,   60
    dw -150,   0,   60
    dw -150,  90,  -60
    dw  150,  90,  -60
    dw  150,  90,   60
    dw -150,  90,   60

tk_e_box:
    db 0,1, 1,2, 2,3, 3,0
    db 4,5, 5,6, 6,7, 7,4
    db 0,4, 1,5, 2,6, 3,7

tk_v_pyr:
    dw -110,   0, -110
    dw  110,   0, -110
    dw  110,   0,  110
    dw -110,   0,  110
    dw    0, 260,    0

tk_e_pyr:
    db 0,1, 1,2, 2,3, 3,0
    db 0,4, 1,4, 2,4, 3,4

tk_v_tank:                          ; hull, turret, and a barrel that says
    dw -130,   0, -150              ; which way it is about to shoot
    dw  130,   0, -150
    dw  130,   0,  150
    dw -130,   0,  150
    dw -130,  70, -150
    dw  130,  70, -150
    dw  130,  70,  150
    dw -130,  70,  150
    dw  -60, 130,  -80
    dw   60, 130,  -80
    dw   60, 130,   60
    dw  -60, 130,   60
    dw    0, 105,  330

tk_e_tank:
    db 0,1, 1,2, 2,3, 3,0
    db 4,5, 5,6, 6,7, 7,4
    db 0,4, 1,5, 2,6, 3,7
    db 8,9, 9,10, 10,11, 11,8
    db 4,8, 5,9, 6,10, 7,11
    db 10,12, 11,12

tk_v_shell:                         ; a three-axis cross: small, bright, and
    dw  -40,  90,    0              ; unmistakably not scenery
    dw   40,  90,    0
    dw    0,  50,    0
    dw    0, 130,    0
    dw    0,  90,  -40
    dw    0,  90,   40

tk_e_shell:
    db 0,1, 2,3, 4,5

tk_sintab:
%include "tksin.inc"

tk_ridgetab:
%include "tkridge.inc"

tk_tantab:
%include "tktan.inc"
