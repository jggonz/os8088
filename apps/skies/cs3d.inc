; =============================================================================
; os8088 - apps/skies/cs3d.inc
;
; THE GEOMETRY (SPEC.md 88.5). Fixed point throughout, 8086 only, and the two
; expensive instructions on this machine - imul at ~140 clocks and idiv at
; ~170 - are what the whole shape of this file is arranged around.
;
; ANGLES ARE SIXTEEN BITS, 65536 to the turn, and the sine table has 1,024
; entries indexed by the top ten bits: Tank's byte angles were enough for a
; tank that yaws, but a horizon that moves in 1.4-degree steps jumps eight
; pixels of pitch at a time on Hercules, and 0.35 degrees is two.
;
; ONE MATRIX A FRAME, NINE MULTIPLIES A VERTEX. The camera's right, up and
; forward vectors are built once a frame from heading, pitch and roll, and a
; world point is M (w - p). The eye is never more than CS_CEIL up and nothing
; further than CS_FAR is transformed, so no partial sum overflows a word.
;
; A BOX IS EIGHTEEN MULTIPLIES, NOT SEVENTY-TWO. Every solid is a STACK of
; levels (wx, h, wz), a level being the four points (+-wx, h, +-wz). M is
; linear, so a level's corners are C + h M1 +- wx M0 +- wz M2: three scaled
; COLUMNS - nine multiplies - and then adds. Tank's shape cache (SPEC.md 85.5)
; generalised to a camera that pitches and rolls.
;
; SS != DS (CLAUDE.md): nothing here indexes with BP.
; =============================================================================

; AX = (AX * BX) >> 15, signed. Clobbers DX. Tank's MUL14 one bit over: the
; sine table and every matrix entry are Q15 here (SPEC.md 88.5.3), so the
; product is moved LEFT once and DX kept - two shifts fewer than Q14 on a
; machine that does five hundred of these a frame. The name is kept because
; every caller means "a fraction times a value" by it.
%macro MUL14 0
    imul bx
    shl ax, 1
    rcl dx, 1
    mov ax, dx
%endmacro

; -----------------------------------------------------------------------------
; cs_sin / cs_cos - AX = an angle; out AX = sin / cos x 32768, clamped to
;                   32767. Clobbers BX, AND NOTHING ELSE.
;
; It used to clobber CX as well, and the quarter table made that expensive:
; the old body wrote CL alone (a shift count), so cs_d_adi holding cos in CX
; across a `call cs_sin` merely had its low byte replaced by 5 and nobody
; ever saw it. The new one writes the whole word, so the same line put a
; DIVISOR OF ZERO into the guarded divide and the horizon stood vertical at
; wings level. The call site is fixed (SPEC.md 88.9.2.4) and so is the
; contract: two bytes of push and pop, on a routine called twenty-five times
; a frame, buys a register nobody has to remember.
;
; A QUARTER TABLE (SPEC.md 88.5.9). The index is the angle's top ten bits, so
; its top two are the quadrant: bit 8 says the quarter runs BACKWARDS, and bit
; 9 says the result is negative. Two tests and a neg for 1,534 bytes, on a
; routine with twenty-five call sites in the whole package - the matrix is
; built ONCE a frame (88.5) and this is not the per-vertex path.
; -----------------------------------------------------------------------------
cs_cos:
    add ax, 16384
cs_sin:
    push cx
    mov bx, ax
    mov cl, 6                       ; the top ten bits: 0..1023 of the turn
    shr bx, cl
    mov cx, bx
    and bx, 255                     ; ...the offset in its quarter
    test ch, 1
    jz .fwd
    neg bx                          ; the odd quarters run backwards, and 256
    add bx, 256                     ; is the entry sin 90 is in
.fwd:
    shl bx, 1
    mov ax, [cs_sintab + bx]
    test ch, 2
    jz .pos
    neg ax                          ; ...and the far half is below the axis
.pos:
    pop cx
    ret

; -----------------------------------------------------------------------------
; cs_matrix - the camera matrix, once a frame, from [cs_hdg]/[cs_pitch]/
;             [cs_roll]; and the world's up vector in camera space out of it
; clobbers everything
;
; Rows are the camera's right, up and forward vectors in world coordinates,
; so that c = M d is (d.right, d.up, d.forward). With heading h, pitch p and
; roll r (right wing down positive):
;   right   = ( cr ch + sr sh sp,  -sr cp,  -cr sh + sr ch sp )
;   up      = ( sr ch - cr sh sp,   cr cp,  -sr sh - cr ch sp )
;   forward = ( sh cp,              sp,      ch cp            )
; -----------------------------------------------------------------------------
cs_matrix:
    mov ax, [cs_hdg]
    call cs_sin
    mov [cs_sinh], ax
    mov ax, [cs_hdg]
    call cs_cos
    mov [cs_cosh], ax
    mov ax, [cs_pitch]
    call cs_sin
    mov [cs_sinp], ax
    mov ax, [cs_pitch]
    call cs_cos
    mov [cs_cosp], ax
    mov ax, [cs_roll]
    call cs_sin
    mov [cs_sinr], ax
    mov ax, [cs_roll]
    call cs_cos
    mov [cs_cosr], ax

    ; --- THE GROUND TRACK'S FACING (SPEC.md 88.7.8.2). The forward vector is
    ;     (sh cp, sp, ch cp), so its horizontal part is the heading SCALED BY
    ;     cos(pitch) - and past the vertical that is negative and the
    ;     aeroplane is pointed the other way. Everything that works in the
    ;     heading's frame rather than through the matrix - the cull's `along`
    ;     and `across`, the occluder's, and which runway threshold is ahead -
    ;     wants THIS pair and not [cs_sinh]/[cs_cosh]. 88.7.8.1's sign, one
    ;     layer out: the magnitude is the matrix's business, the direction is
    ;     everyone's ------------------------------------------------------
    mov ax, [cs_sinh]
    mov bx, [cs_cosh]
    cmp word [cs_cosp], 0
    jns .face
    neg ax
    neg bx
.face:
    mov [cs_fsinh], ax
    mov [cs_fcosh], bx

    mov al, [cs_conebase]             ; the cull's cone: the backend's
    mov [cs_cone], al                 ; factor, or all of the depth past 15
    mov ax, [cs_pitch]                ; degrees of pitch or roll
    or ax, ax
    jns .p
    neg ax
.p:
    cmp ax, 2730
    jg .wide
    mov ax, [cs_roll]
    or ax, ax
    jns .r
    neg ax
.r:
    cmp ax, 2730
    jle .cone
.wide:
    mov byte [cs_cone], 2
.cone:
    mov ax, [cs_sinh]                 ; t1 = sh sp, t2 = ch sp
    mov bx, [cs_sinp]
    MUL14
    mov [cs_t1], ax
    mov ax, [cs_cosh]
    mov bx, [cs_sinp]
    MUL14
    mov [cs_t2], ax

    mov ax, [cs_cosr]                 ; m00 = cr ch + sr t1
    mov bx, [cs_cosh]
    MUL14
    mov si, ax
    mov ax, [cs_sinr]
    mov bx, [cs_t1]
    MUL14
    add ax, si
    mov [cs_m + 0], ax
    mov ax, [cs_sinr]                 ; m01 = -sr cp
    mov bx, [cs_cosp]
    MUL14
    neg ax
    mov [cs_m + 2], ax
    mov ax, [cs_sinr]                 ; m02 = sr t2 - cr sh
    mov bx, [cs_t2]
    MUL14
    mov si, ax
    mov ax, [cs_cosr]
    mov bx, [cs_sinh]
    MUL14
    sub si, ax
    mov [cs_m + 4], si
    mov ax, [cs_sinr]                 ; m10 = sr ch - cr t1
    mov bx, [cs_cosh]
    MUL14
    mov si, ax
    mov ax, [cs_cosr]
    mov bx, [cs_t1]
    MUL14
    sub si, ax
    mov [cs_m + 6], si
    mov ax, [cs_cosr]                 ; m11 = cr cp
    mov bx, [cs_cosp]
    MUL14
    mov [cs_m + 8], ax
    mov ax, [cs_sinr]                 ; m12 = -sr sh - cr t2
    mov bx, [cs_sinh]
    MUL14
    mov si, ax
    mov ax, [cs_cosr]
    mov bx, [cs_t2]
    MUL14
    add ax, si
    neg ax
    mov [cs_m + 10], ax
    mov ax, [cs_sinh]                 ; m20 = sh cp
    mov bx, [cs_cosp]
    MUL14
    mov [cs_m + 12], ax
    mov ax, [cs_sinp]                 ; m21 = sp
    mov [cs_m + 14], ax
    mov ax, [cs_cosh]                 ; m22 = ch cp
    mov bx, [cs_cosp]
    MUL14
    mov [cs_m + 16], ax

    mov ax, [cs_m + 2]              ; the world's up vector in camera space is
    mov [cs_nx], ax                 ; the second COLUMN (SPEC.md 88.4.1)
    mov ax, [cs_m + 8]
    mov [cs_ny], ax
    mov ax, [cs_m + 14]
    mov [cs_nz], ax
    call cs_eyeshift                ; ...and the eye, pre-shifted (88.5.6.3)
    ret

; -----------------------------------------------------------------------------
; cs_rot - AX/BX/CX = a vector; out AX/BX/CX = M times it. Clobbers DX, SI,
;          DI and BP - nothing from cs_scene down holds a value in BP.
;
; The vector used to be parked in cs_rvx/y/z and a cs_dot call read it back
; three times a row, three rows: NINE loads, three call/rets and an
; `add si, 6` a row around nine MUL14 (88.5.6.4).
; -----------------------------------------------------------------------------
; ONE ROW OF IT (88.5.6.4): %1 = the row's byte offset into cs_m. The vector
; is in CX/SI/BP and stays there for all three rows - MUL14 is `imul bx`, so
; the MULTIPLICAND has to be AX and the matrix element is what varies, which
; is the opposite of cs_flatverts' arrangement and is why the vector wants
; three registers of its own rather than one.
%macro CS_ROTROW 1
    mov ax, cx
    mov bx, [cs_m + %1]
    MUL14
    mov di, ax
    mov ax, si
    mov bx, [cs_m + %1 + 2]
    MUL14
    add di, ax
    mov ax, bp
    mov bx, [cs_m + %1 + 4]
    MUL14
    add ax, di
%endmacro

cs_rot:
    mov si, bx                      ; SI = y
    mov bp, cx                      ; BP = z
    mov cx, ax                      ; CX = x
    CS_ROTROW 0
    push ax                         ; the two finished rows park on the STACK,
    CS_ROTROW 6                     ; a byte and a clock cheaper each way than
    push ax                         ; the bss words they used to
    CS_ROTROW 12
    mov cx, ax
    pop bx
    pop ax
    ret

; cs_colscale - AX = a scalar, SI = the matrix column's first word, DI = three
; words to fill with the scaled column. Clobbers AX, BX, CX, DX.
cs_colscale:
    mov cx, ax
    mov bx, [si]
    MUL14
    mov [di], ax
    mov ax, cx
    mov bx, [si+6]
    MUL14
    mov [di+2], ax
    mov ax, cx
    mov bx, [si+12]
    MUL14
    mov [di+4], ax
    ret

; -----------------------------------------------------------------------------
; cs_ktabs - the two projection tables, once a bracket (SPEC.md 85.5.3,
;            rebucketed for a 16 km world - 88.5)
;
; Row i stands for a depth: i itself below 1,024; 1,024 + 4(i - 1,024) + 2 to
; row 1,791; and 4,096 + 64(i - 1,792) + 32 beyond - the bucket's centre, so
; the error is signed both ways. Depths under CS_NEAR take its value.
; -----------------------------------------------------------------------------
cs_ktabs:
    xor si, si
.e:
    mov ax, si
    shr ax, 1                       ; AX = i
    cmp ax, 1024
    jb .z1
    cmp ax, 1792
    jb .z2
    sub ax, 1792
    mov cl, 6
    shl ax, cl
    add ax, 4096 + 32
    jmp short .zz
.z2:
    sub ax, 1024
    shl ax, 1
    shl ax, 1
    add ax, 1024 + 2
    jmp short .zz
.z1:
    cmp ax, CS_NEAR
    jae .zz
    mov ax, CS_NEAR
.zz:
    mov cx, ax                      ; CX = z
    mov ax, [cs_sclx]
    mov dx, 2048
    mul dx                          ; DX:AX = sclx x 2048
    div cx
    mov [cs_kx + si], ax
    mov ax, [cs_scly]
    mov dx, 2048
    mul dx
    div cx
    mov [cs_ky + si], ax
    add si, 2
    cmp si, 4096
    jb .e
    ret

; -----------------------------------------------------------------------------
; cs_project0 / cs_project2 / cs_project4 - one camera-space point onto the
;            box, at the object's transform scale (88.5.6): whole metres,
;            quarters, sixteenths. [cs_projp] is the one for [cs_pshr]
; in:  AX = cx, BX = cy, CX = cz (cz >= the near plane at the scale)
; out: AX = screen x, BX = screen y (box coords, clamped to +-4000 about the
;      centre).  clobbers CX, DX, SI, DI
;
; One routine served every scale through a `shr bx, cl` and two indirect
; calls to the scale's shift, which was ~110 cycles a vertex in calls and
; returns; here the depth shift and the product shift are the scale's own
; instructions, and the divide path under the floor is shared
; -----------------------------------------------------------------------------
; CS_PSH - AX = (DX:AX) >> %2, clamped to +-4000 by the high word against
;          %1 first (SPEC.md 85.5.3); %3/%4 are the clamps, out of line
%macro CS_PSH 4
    cmp dx, %1
    jge %3
    cmp dx, -(%1 + 1)
    jl %4
  %if %2 == 15
    shl ax, 1
    rcl dx, 1
    mov ax, dx
  %elif %2 == 13
    shl ax, 1                       ; the PAIR left three: DX = product >> 13.
    rcl dx, 1                       ; The middle-word form below holds only
    shl ax, 1                       ; while |product| < 2^23 - DX within
    rcl dx, 1                       ; +-127 - and this clamp lets 500 through,
    shl ax, 1                       ; so a point 1,024 to 4,000 pixels from
    rcl dx, 1                       ; the centre came back with the wrong
    mov ax, dx                      ; sign: every side-clip crossing (88.5.6.1)
  %else
    mov al, ah                      ; bits 8..23 as a word, then the rest:
    mov ah, dl                      ; sound because 125 x 65536 < 2^23
    %rep %2 - 8
    sar ax, 1
    %endrep
  %endif
%endmacro

; CS_PROJ - %1 = the depth shift (cs_pshr), %2 = the product's high-word
;           bound, %3 = the product shift (11, 13 or 15)
%macro CS_PROJ 3
    mov si, bx                      ; SI = cy
    mov bx, cx                      ; the depth picks the row...
    cmp bx, CS_NEAR << %1
    jae %%table
    jmp cs_pnear
%%table:
  %rep %1
    shr bx, 1                       ; ...in whole metres (88.5.6)
  %endrep
    cmp bx, 1024
    jb %%row
    cmp bx, 4096
    jb %%mid
    sub bx, 4096                    ; 4096.. : every 64 units, from row 1792
    mov cl, 6
    shr bx, cl
    add bx, 1792
    cmp bx, 2047
    jbe %%row
    mov bx, 2047
    jmp short %%row
%%mid:
    sub bx, 1024                    ; 1024..4095: every 4 units, from row 1024
    shr bx, 1
    shr bx, 1
    add bx, 1024
%%row:
    shl bx, 1
    imul word [cs_kx + bx]          ; DX:AX = cx x kx
    CS_PSH %2, %3, %%hix, %%lox
%%bx:
    add ax, [cs_vcx]
    mov di, ax                      ; DI = screen x
    mov ax, si
    imul word [cs_ky + bx]
    CS_PSH %2, %3, %%hiy, %%loy
%%by:
    neg ax                          ; screen y grows DOWNWARD
    add ax, [cs_vcy]
    mov bx, ax
    mov ax, di
    ret
%%hix:
    mov ax, 4000
    jmp short %%bx
%%lox:
    mov ax, -4000
    jmp short %%bx
%%hiy:
    mov ax, 4000
    jmp short %%by
%%loy:
    mov ax, -4000
    jmp short %%by
%endmacro

cs_project0:
    CS_PROJ 0, 125, 11
cs_project2:
    CS_PROJ 2, 500, 13
cs_project4:
    CS_PROJ 4, 2000, 15
cs_projtab: dw cs_project0, cs_project2, cs_project4
cs_pshtab:  dw cs_psh, cs_psh + 6, cs_psh + 12   ; the eye pre-shifted (88.5.6.3)

    ; --- under the table's floor: a ground polygon's near plane is 4 m
    ;     (88.5.5), where k would not fit a word, so the two divides are
    ;     real ones - clamped first, because a quotient past +-4000 would
    ;     not fit either and an 8088 faults on that. A ratio: the scale
    ;     needs no telling. In: SI = cy, CX = cz, AX = cx --------------------
cs_pnear:
    mov di, cx                      ; DI = z, 1..39
    mov cx, ax                      ; CX = cx
    mov ax, di
    mov bx, 9
    mul bx                          ; AX = 9 z: |cx| past it clamps (4000/443)
    mov bx, cx
    or bx, bx
    jns .ax
    neg bx
.ax:
    cmp bx, ax
    ja .xcl
    mov ax, cx
    imul word [cs_sclx]
    idiv di
    jmp short .xd
.xcl:
    mov ax, 4000
    or cx, cx
    jns .xd
    neg ax
.xd:
    add ax, [cs_vcx]
    mov cx, ax                      ; CX = screen x
    mov ax, di
    mov bx, 14
    mul bx                          ; AX = 14 z (4000/285)
    mov bx, si
    or bx, bx
    jns .ay
    neg bx
.ay:
    cmp bx, ax
    ja .ycl
    mov ax, si
    imul word [cs_scly]
    idiv di
    jmp short .yd
.ycl:
    mov ax, 4000
    or si, si
    jns .yd
    neg ax
.yd:
    neg ax
    add ax, [cs_vcy]
    mov bx, ax
    mov ax, cx
    ret

; -----------------------------------------------------------------------------
; cs_scale - the object's transform scale (SPEC.md 88.5.6): sixteenths of a
;            metre when its reach from the eye - the world offset's Manhattan
;            length, which no component of it rotated can exceed, plus the
;            radius - is under 2,048 m AND the radius under 1,024 (so that
;            the difference of two vertices fits a word as well: the
;            clippers subtract them), quarters under 8,192 and 4,096, whole
;            metres beyond: the finest that cannot overflow. The origin is ONE
;            rotation, at that scale, FROM THE EYE'S 16.8 POSITION, and its
;            whole-metre form for the size tests is that shifted back. The
;            first build rotated whole metres from a whole-metre eye, and a
;            runway transformed that way jumped a metre across between
;            frames: 20 pixels at 22 m.
; in:  SI = the cs_vis record (ptr, reach, along), [cs_obj]
; out: [cs_pshr], [cs_projp], cs_scx/y/z, cs_ocx/y/z.
;      Clobbers everything
; -----------------------------------------------------------------------------
cs_scale:
    mov ax, [si + 2]                ; the world offset's Manhattan length,
    push si                         ; the cull's, which no component of the
    mov si, [si]                    ; rotated offset can exceed (it is over
    mov si, [si + CSO_MODEL]        ; the Euclidean length)...
    mov bx, [si + CSM_RAD]
    add ax, bx                      ; ...plus the radius: no vertex is further
    jc .s0
    mov cl, 4
    cmp ax, 2048                    ; sixteenths: every coordinate under 2^15,
    jae .q                          ; and every DIFFERENCE of two, which the
    cmp bx, 1024                    ; clippers subtract: twice the radius
    jb .have
.q:
    mov cl, 2
    cmp ax, 8192
    jae .s0
    cmp bx, 4096
    jb .have
.s0:
    xor cl, cl
.have:
    pop si
    mov [cs_pshr], cl
    mov bl, cl
    xor bh, bh
    mov ax, [cs_projtab + bx]       ; 0, 2, 4 are the table's byte offsets
    mov [cs_projp], ax
    mov ax, [cs_pshtab + bx]        ; ...and the eye already shifted for it
    mov [cs_pshp], ax
    ; --- the origin from the 16.8 eye, at the scale. THE SHIFT DISTRIBUTES
    ;     (88.5.6.3), so what used to be three cs_sdiff calls - each building
    ;     a 32-bit `coordinate * 256`, subtracting the 32-bit position and
    ;     shifting the pair down 8 - pshr - is a `shl` and a word subtract
    ;     from the eye cs_eyeshift pre-shifted once for the whole frame ----
    mov cl, [cs_pshr]               ; ...and the count is loaded ONCE for the
    mov si, [cs_pshp]               ; three, where the ladder inside cs_sdiff
    mov bx, [cs_obj]                ; ran per call for a per-OBJECT answer
    mov ax, [bx + CSO_X]
    shl ax, cl
    sub ax, [si]
    push ax
    mov ax, [bx + CSO_Z]
    shl ax, cl
    sub ax, [si + 4]
    push ax
    mov ax, [bx + CSO_Y]
    mov bx, [cs_plane]              ; (the object's fields are all read now)
    sub ax, [bx + CSP_EYE]          ; the eye is that much above the wheels
    shl ax, cl
    sub ax, [si + 2]
    mov bx, ax
    pop cx
    pop ax
    mov [cs_odx], ax                ; the offset in WORLD axes, before the
    mov [cs_ody], bx                ; rotation: cs_axcull's input (88.5.12)
    mov [cs_odz], cx
    call cs_rot
    mov [cs_scx], ax
    mov [cs_scy], bx
    mov [cs_scz], cx
    mov cl, [cs_pshr]               ; ...and in whole metres, for the tests
    sar ax, cl
    mov [cs_ocx], ax
    sar bx, cl
    mov [cs_ocy], bx
    sar cx, cl
    mov [cs_ocz], cx
    ret

; cs_nearat - AX = a near plane in metres: [cs_near] at the object's scale
cs_nearat:
    mov cl, [cs_pshr]
    shl ax, cl
    mov [cs_near], ax
    ret

; -----------------------------------------------------------------------------
; cs_eyeshift - the eye's position pre-shifted for each of the three scales
;               (SPEC.md 88.5.6.3), nine words, once a frame
;
; cs_scale used to build `coordinate * 256` as a 32-bit value, subtract the
; 16.8 position from it and shift the pair down 8 - pshr, three times an
; OBJECT. The shift DISTRIBUTES, and exactly rather than approximately:
; coordinate * 256 is divisible by 2^s, so
;
;     (c * 256 - p) >> s  ==  (c << (8 - s))  -  ceil(p / 2^s)
;
; for every c and p, and `ceil(p / 2^s)` is `sar(p + 2^s - 1, s)` - which
; depends on the FRAME and not on the object. Nine of those a frame here
; against three 32-bit builds and shifts per object there, and what is left
; at the call site is `shl ax, cl` and a word subtract.
;
; `c << pshr` can overflow a word and that is allowed: the ladder guarantees
; the DIFFERENCE fits, and two's complement arithmetic mod 65536 gives the
; same low word either way - which is exactly what the 32-bit form did when
; it took AX and threw DX away.
; -----------------------------------------------------------------------------
%macro CS_EYESH 1                   ; %1 = s, the shift for this scale
    mov si, cs_px
    mov cx, 3                       ; x, y, z - three consecutive ZDWORDs
%%a:
    mov ax, [si]
    mov dx, [si + 2]
    add ax, (1 << %1) - 1           ; ...the ceiling, before the shift
    adc dx, 0
%if %1 == 8
    mov al, ah                      ; a shift of eight is the middle two
    mov ah, dl                      ; bytes, which cs_sdiff knew as well
%else
%rep %1
    sar dx, 1
    rcr ax, 1
%endrep
%endif
    mov [di], ax
    add di, 2
    add si, 4
    loop %%a
%endmacro

cs_eyeshift:
    mov di, cs_psh
    CS_EYESH 8                      ; whole metres...
    CS_EYESH 6                      ; ...quarters...
    CS_EYESH 4                      ; ...sixteenths
    ret

; =============================================================================
; The scene: cull, sort, draw (SPEC.md 88.5)
; =============================================================================

; -----------------------------------------------------------------------------
; cs_scene - every object worth drawing this frame, far to near
; clobbers everything
; -----------------------------------------------------------------------------
cs_scene:
    mov word [cs_nvisn], 0
    mov bl, [cs_setlod]             ; the Detail rung's SCALE is the frame's,
    xor bh, bh                      ; and cs_range looked it up once an object
    shl bx, 1                       ; - a table index and a push pair for an
    mov bx, [cs_lodscl + bx]        ; answer that cannot change inside a frame
    mov [cs_lodsc], bx
    mov si, cs_rwobj                ; the runway first...
    call cs_consider
    mov bx, [cs_airport]            ; ...then the PICKED LOCATION'S world
    mov si, [bx + CSA_OBJS]         ; (88.6.4), which is a pair of words in
    mov cx, [bx + CSA_NOBJ]         ; its record and not a global table
    jcxz .done
.o:
    push cx
    push si
    call cs_consider
    pop si
    pop cx
    add si, CSO_SIZE
    loop .o
.done:
    call cs_occlude                 ; what stands wholly behind something else
    ; --- the ground shapes, far to near, then the solids ---------------------
    mov byte [cs_pass], CSM_FLAT
    call cs_drawpass
    mov byte [cs_pass], CSM_STACK
    call cs_drawpass
    ret

; cs_skipclr - every object looked at on the next frame: the aeroplane has
;              been moved by something other than flying
cs_skipclr:
    push bx
    mov bx, [cs_airport]            ; the PICKED location's world (88.6.4):
    mov si, [bx + CSA_OBJS]         ; a switch leaves the last one's skips
    mov cx, [bx + CSA_NOBJ]         ; and SEEN bits where they were, and
    jcxz .out                       ; this is what makes them strangers
.o:
    mov word [si + CSO_SKIP], 0
    and byte [si + CSO_FLAGS + 1], ~((CSO_SEEN | CSO_BOXED) >> 8)
    add si, CSO_SIZE
    loop .o
.out:
    pop bx
    ret

cs_drawpass:
    mov si, cs_vkey
    mov cx, [cs_nvisn]
    jcxz .out
.v:
    push cx
    push si
    mov bx, si                      ; hidden behind something nearer (88.13.7)
    sub bx, cs_vkey
    shr bx, 1
    shr bx, 1
    cmp byte [cs_occ + bx], 0
    jne .next
    mov si, [si + 2]                ; the key's record
    mov bx, [si]
    mov bx, [bx + CSO_MODEL]
    mov al, [bx + CSM_TYPE]
    cmp al, [cs_pass]
    jne .next
    call cs_drawobj
    call cs_sound_step              ; ...AND THE ENGINE, between objects
                                    ; (SPEC.md 88.8.2.1.2): cs_scene is 78% of
                                    ; a frame, so this is where the ticks
                                    ; actually elapse. It is one far call to
                                    ; OSAPI_GET_TICKS on the object that has
                                    ; not crossed one - 46.7 us against a
                                    ; scene of 128 ms - and CX and SI are the
                                    ; loop's own, saved either side of here
.next:
    pop si
    pop cx
    add si, 4
    loop .v
.out:
    ret


; =============================================================================
; THE OCCLUSION PASS (SPEC.md 88.13.7) - only for a location that asks
; =============================================================================

; -----------------------------------------------------------------------------
; cs_occlude - mark the visible objects that stand WHOLLY BEHIND another one
;
; The test is ANGULAR and in the WORLD, never on the screen. `along` is
; already in every cs_vkey slot and `across` is two multiplies, so an object
; found hidden is refused before cs_scale rotates anything - the same place
; the Detail Level refuses one, which is the cheapest place there is. Nothing
; is projected and no framebuffer is read.
;
; An occluder A hides an occludee B when B's angular interval lies inside A's
; AT BOTH OF B'S OWN LEVELS - its base, which is its widest and lowest, and
; its top, which is its narrowest and highest. Both intervals are linear in
; elevation, so their difference is linear and containment at the two ends is
; containment throughout: two tests settle a frustum exactly.
;
; A's half-width is taken AT THE HEIGHT WHERE A'S SURFACE MEETS B'S ELEVATION,
; and that is what makes the whole thing work. The first draft compared B's
; BASE width at B's TOP elevation and missed a ridge the eye can see is
; hidden, because a frustum is narrow where it is tall.
;
; The half-extent is measured ACROSS THE HEADING - wx |cos h| + wz |sin h| -
; which is exact for an object dead ahead and errs either way for one off to
; the side. A rule that is conservative for every bearing was measured first
; and is USELESS: it stops finding Nepal's spurs 400 m off the threshold,
; where this one goes on finding them to 2,000 m and agrees with the pixels
; at every point sampled (88.13.7.1). tests/skiesocc.py is what holds it to
; that, by reading this routine's own verdicts back and checking each one
; against the glass.
;
; in:  [cs_nvisn] filed, cs_vkey sorted far to near
; out: cs_occ[i] non-zero for a hidden slot, [cs_occn] how many
; clobbers everything
; -----------------------------------------------------------------------------
cs_occlude:
    push ds                         ; ES IS THE KERNEL'S in a package (SPEC.md
    pop es                          ; 20), so a string op that forgets this
    mov word [cs_occn], 0           ; writes into the KERNEL'S SEGMENT - which
    mov cx, [cs_nvisn]              ; is what the first build of this did,
    mov di, cs_occ                  ; every frame, on every world, and the
    xor al, al                      ; symptom was only that no occluder ever
    cld                             ; held anything. cs_clearall is the same
    push cx                         ; two instructions for the same reason
    rep stosb                       ; nothing hidden until it is shown to be
    pop cx
    cmp cx, 2
    jb .out                         ; one object hides nothing
    mov bx, [cs_airport]
    test word [bx + CSA_FLAGS], CSA_OCC
    jz .out                         ; ...and a location that did not ask pays
    mov ax, [cs_sinh]               ;    two compares and the clear, no more
    or ax, ax                       ; (|sh| and |ch| are a box's WIDTH, so
    jns .sp                         ;  the facing pair would read the same:
    neg ax                          ;  88.7.8.2 is about direction)
.sp:
    mov [cs_asinh], ax
    mov ax, [cs_cosh]
    or ax, ax
    jns .cp
    neg ax
.cp:
    mov [cs_acosh], ax
    mov byte [cs_occk], 0
    mov [cs_occi], cx
    mov si, cx                      ; NEAR TO FAR, which is cs_vkey backwards
    dec si
    shl si, 1
    shl si, 1
    add si, cs_vkey
.slot:
    dec word [cs_occi]
    push si
    mov bp, [si]                    ; BP = along
    cmp bp, 200
    jle .next                       ; at or behind the eye: neither role
    mov di, [si + 2]
    mov di, [di]                    ; the object
    call cs_occbox
    jc .next                        ; not a frustum this pass can reason about
    call cs_occtest
    jnc .keep
    mov bx, [cs_occi]
    mov byte [cs_occ + bx], 1
    inc word [cs_occn]
    jmp short .next
.keep:
    call cs_occadd
.next:
    pop si
    sub si, 4
    cmp si, cs_vkey
    jae .slot
.out:
    ret

; -----------------------------------------------------------------------------
; cs_occbox - the candidate's six words, from object DI at along BP
; out: cs_ob_*, CF = 1 if the model is not a two-or-more-level STACK
; clobbers everything but BP
; -----------------------------------------------------------------------------
cs_occbox:
    mov [cs_occo], di               ; DI IS THE OBJECT and MUL14 wants a
    mov si, [di + CSO_MODEL]        ; register: the first draft kept it in DI,
    cmp byte [si + CSM_TYPE], CSM_STACK  ; overwrote it with a product and
    jne .no                         ; then re-read a cs_obj cs_consider had
    mov al, [si + CSM_NV]           ; left behind - a different object's Y
    cmp al, 2                       ; and position, silently
    jb .no
    xor ah, ah
    dec ax
    mov bx, 6
    mul bx                          ; AX = the last level's offset
    mov si, [si + CSM_VERTS]
    mov cx, si
    add cx, ax                      ; CX = the top level
    mov [cs_ob_l], bp
    push cx
    mov ax, [si]                    ; the base: wx |cos h| + wz |sin h|
    mov bx, [cs_acosh]
    MUL14
    mov cx, ax
    mov ax, [si + 4]
    mov bx, [cs_asinh]
    MUL14
    add ax, cx
    mov [cs_ob_w0], ax
    pop si                          ; ...and the top's
    push si
    mov ax, [si]
    mov bx, [cs_acosh]
    MUL14
    mov cx, ax
    mov ax, [si + 4]
    mov bx, [cs_asinh]
    MUL14
    add ax, cx
    mov [cs_ob_w1], ax
    pop si
    mov di, [cs_occo]
    mov ax, [di + CSO_Y]
    mov [cs_ob_y0], ax
    add ax, [si + 2]                ; ...plus the top level's height
    mov [cs_ob_y1], ax
    mov ax, [di + CSO_X]            ; across, in the FACING's frame - the
    sub ax, [cs_ex]                 ; same one cs_vkey's `along` is in, which
    mov bx, [cs_fcosh]              ; is what cs_occlev multiplies it against
    MUL14
    mov cx, ax
    mov ax, [di + CSO_Z]
    sub ax, [cs_ez]
    mov bx, [cs_fsinh]
    MUL14
    sub cx, ax
    mov [cs_ob_c], cx
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cs_occadd - keep the candidate as an occluder
;
; RANKED BY ANGULAR SIZE and not by distance. Keeping the three NEAREST fills
; the slots with a hangar twelve metres tall and the two valley walls, and
; never reaches the gate peaks that are the only things in the world that
; hide anything - measured, one spur of two found instead of both.
; -----------------------------------------------------------------------------
cs_occadd:
    mov ax, [cs_ob_w0]              ; (half-extent + height) / along, in
    add ax, [cs_ob_y1]              ; sixty-fourths: an angular size, and
    sub ax, [cs_ob_y0]              ; along is over 200 so it fits a word
    xor dx, dx
    mov bx, 64
    mul bx
    div word [cs_ob_l]
    mov [cs_ob_ang], ax
    mov cl, [cs_occk]
    xor ch, ch
    cmp cl, CS_OCCN
    jb .append
    mov si, cs_occa                 ; ...else displace the WEAKEST, if this
    mov di, si                      ; one beats it
    mov bx, [si + 12]
    mov cx, CS_OCCN - 1
.scan:
    add si, CS_OCCZ
    cmp [si + 12], bx
    jae .keep
    mov bx, [si + 12]
    mov di, si
.keep:
    loop .scan
    cmp ax, bx
    jbe .out
    jmp short .copy
.append:
    mov ax, cx
    mov bx, CS_OCCZ
    mul bx
    mov di, cs_occa
    add di, ax
    inc byte [cs_occk]
.copy:
    push ds                         ; ES is the kernel's (SPEC.md 20)
    pop es
    mov si, cs_ob_l
    mov cx, 7
    cld
    rep movsw
.out:
    ret

; cs_occtest - CF = 1 if any kept occluder hides the candidate
cs_occtest:
    mov cl, [cs_occk]
    xor ch, ch
    jcxz .no
    mov si, cs_occa
.one:
    push cx
    push si
    call cs_occpair
    pop si
    pop cx
    jc .yes
    add si, CS_OCCZ
    loop .one
.no:
    clc
    ret
.yes:
    stc
    ret

; cs_occpair - CF = 1 if occluder SI contains the candidate at BOTH its levels
cs_occpair:
    mov ax, [cs_ob_y0]
    mov bx, [cs_ob_w0]
    call cs_occlev
    jnc .no
    mov ax, [cs_ob_y1]
    mov bx, [cs_ob_w1]
    call cs_occlev
    jnc .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; cs_occlev - one level of the candidate against occluder SI
; in:  AX = the level's height, BX = its half-extent, SI = the occluder
; out: CF = 1 if that level's angular interval is inside the occluder's
; -----------------------------------------------------------------------------
cs_occlev:
    push bx                         ; the candidate's half-extent
    sub ax, [cs_ey]                 ; the elevation, over the candidate's along
    imul word [si]                  ; ...times the OCCLUDER's along
    idiv word [cs_ob_l]
    add ax, [cs_ey]                 ; AX = the height A's surface meets it at
    cmp ax, [si + 6]
    jg .no                          ; A does not reach that high
    sub ax, [si + 4]
    jge .ge
    xor ax, ax                      ; below A's base: A is at its widest
.ge:
    mov cx, [si + 6]
    sub cx, [si + 4]
    jle .flat
    mov bx, [si + 10]
    sub bx, [si + 8]
    imul bx
    idiv cx
    add ax, [si + 8]
    jmp short .have
.flat:
    mov ax, [si + 8]
.have:
    ; --- AND SHRINK IT BY AN EIGHTH. The extent is measured across the
    ;     HEADING and the covering happens across the LINE OF SIGHT to the
    ;     occludee, which for an object off to the side is not the same
    ;     angle; the difference is what let this hide a spur at 2,000 m that
    ;     tests/skiesocc.py then found on the glass, 654 pixels of it. The
    ;     margin is the smallest that stands up to that row's viewpoints and
    ;     it is the OCCLUDER that pays it, so the error is always towards
    ;     drawing (88.13.7.1)
    mov bx, ax
    sar bx, 1
    sar bx, 1
    sar bx, 1
    sub ax, bx
    mov [cs_occwa], ax
    mov ax, [si + 2]                ; (ca - wa) lb <= (cb - wb) la
    sub ax, [cs_occwa]
    imul word [cs_ob_l]
    mov [cs_p0], ax
    mov [cs_p0 + 2], dx
    pop bx
    push bx
    mov ax, [cs_ob_c]
    sub ax, bx
    imul word [si]
    sub ax, [cs_p0]
    sbb dx, [cs_p0 + 2]
    js .no
    pop bx                          ; (cb + wb) la <= (ca + wa) lb
    push bx
    mov ax, [cs_ob_c]
    add ax, bx
    imul word [si]
    mov [cs_p0], ax
    mov [cs_p0 + 2], dx
    mov ax, [si + 2]
    add ax, [cs_occwa]
    imul word [cs_ob_l]
    sub ax, [cs_p0]
    sbb dx, [cs_p0 + 2]
    js .no
    pop bx
    stc
    ret
.no:
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; cs_consider - cull object SI, and file it in cs_vis by depth if it survives
; clobbers everything but SI... no: everything (SI is the caller's to keep)
;
; A 32-bit distance box first, so the 16-bit arithmetic is never entered on
; a difference that would not fit it; then the Manhattan range the object
; asks for; then the transformed origin against the frustum, the model's
; radius folded in.
; -----------------------------------------------------------------------------
cs_consider:
    mov [cs_obj], si
    mov ax, [si + CSO_FLAGS]        ; --- WHAT THE PLAYER ASKED FOR (88.13.1),
    mov bl, [cs_setbld]             ;     a LADDER: the DENSE CITY goes first,
    cmp bl, CSBL_HIGH               ;     then everything but the points of
    je .tier                        ;     interest, then all but the roads,
    test ax, CSO_DENSE              ;     then those too.
    jnz .out                        ; HIGH'S OWN CONTENT, AND ONLY HIGH'S, and
                                    ; it is tested FIRST so that no exemption
                                    ; below can undo it: this sat under
                                    ; TERRAIN's, and 88.13.1.1 is the record
                                    ; of what that costs - Nepal's CSO_FILLER
                                    ; ridges were terrain too, so the flag had
                                    ; never filtered one of them, and its own
                                    ; High tier (88.13.1.3) drew at every rung
                                    ; for the same reason until this moved
    test ax, CSO_TERRAIN            ; TERRAIN is exempt from what is left -
    jnz .tier                       ; the hills, the water and the runway are
    cmp bl, CSBL_NONE               ; the world's own surface and not
    je .out                         ; buildings to thin out - and the
    test ax, CSO_ROAD               ; ROADS come back one rung up, being
    jnz .tier                       ; the shape of a city with no city on
    cmp bl, CSBL_ROADS              ; it and a line model each
    je .out
    cmp bl, CSBL_LOW
    jne .tier
    test ax, CSO_POI
    jz .out
.tier:
    mov ax, [si + CSO_SKIP]         ; out of range last time by enough that
    sub ax, [cs_last]               ; it cannot be back yet (88.5.2)
    jg .out
    mov ax, [si + CSO_X]            ; the world is 16-bit metres and so is
    sub ax, [cs_ex]                 ; the eye's whole part, so the difference
    jo .far                         ; fits a word unless the subtraction
    mov [cs_ddx], ax                ; OVERFLOWS - and that is a flag
    mov ax, [si + CSO_Z]
    sub ax, [cs_ez]
    jo .far
    mov [cs_ddz], ax
    ; --- the Manhattan range ------------------------------------------------
    mov ax, [cs_ddx]
    or ax, ax
    jns .a1
    neg ax
.a1:
    mov bx, [cs_ddz]
    or bx, bx
    jns .a2
    neg bx
.a2:
    cmp ax, CS_FAR
    jae .far
    cmp bx, CS_FAR
    jae .far
    add ax, bx
    mov [cs_ddm], ax                ; the reach cs_scale wants is this + |dy|
    call cs_range                   ; AX = this object's range AT THIS DETAIL
    mov bx, ax                      ; (88.13.2), which is its own at Moderate
    mov ax, [cs_ddm]                ; (it was pushed round the call; the word
    sub ax, bx                      ;  it was just stored in is cheaper)
    jbe .inrange
    ; --- out of range by AX metres: not looked at again for AX/16 ticks.
    ;     The aeroplane covers under 5 m a tick at VMAX, so the margin is
    ;     threefold; a reset clears every skip (cs_skipclr) -----------------
    mov cl, 4
    shr ax, cl
    add ax, [cs_last]
    mov [si + CSO_SKIP], ax
    jmp .out
.far:                               ; sixteen kilometres or more away
    mov ax, [cs_last]
    add ax, 256
    mov [si + CSO_SKIP], ax
    jmp .out
.inrange:
    mov ax, [si + CSO_Y]
    sub ax, [cs_ey]
    mov [cs_ddy], ax
    ; --- drawn last frame: it is filed without the cone, which cs_drawobj's
    ;     frustum repeats exactly after the rotation it has to do anyway;
    ;     the bit goes and comes back if the frustum still has it (88.5.1)
    test byte [si + CSO_FLAGS + 1], CSO_SEEN >> 8
    jz .cone
    and byte [si + CSO_FLAGS + 1], ~(CSO_SEEN >> 8)
    mov ax, [cs_ddx]
    mov bx, [cs_fsinh]
    MUL14
    mov cx, ax
    mov ax, [cs_ddz]
    mov bx, [cs_fcosh]
    MUL14
    add cx, ax                      ; CX = along, the sort key
    jmp .file
.cone:
    ; --- the cheap cone first: along the FACING and across it (88.7.8.2 -
    ;     the heading is only the facing while cos(pitch) is positive), two
    ;     multiplies each, so an object behind the aeroplane costs no
    ;     rotation. |across| <= 0.75 along + r + |dy| is the frustum's own
    ;     x test with the height folded into the margin; past 15 degrees of
    ;     pitch or roll the factor goes to 1 (cs_matrix's [cs_cone]), a
    ;     right angle being wider than the view's diagonal at any attitude.
    ;     (The first build kept the margin in DX across the multiplies,
    ;     which write DX: the cone read twice its angle and every frame
    ;     rotated six objects it then refused - 88.5.1.)
    mov ax, [cs_ddx]
    mov bx, [cs_fsinh]
    MUL14
    mov cx, ax
    mov ax, [cs_ddz]
    mov bx, [cs_fcosh]
    MUL14
    add cx, ax                      ; CX = along
    mov si, [cs_obj]
    mov si, [si + CSO_MODEL]
    mov dx, [si + CSM_RAD]          ; DX = the radius...
    mov ax, [cs_ddy]
    or ax, ax
    jns .dy
    neg ax
.dy:
    add dx, ax                      ; ...plus |dy|
    mov ax, cx
    add ax, dx
    js .out                         ; wholly behind
    mov si, cx
    sar si, 1                       ; SI = 0.5 along...
    cmp byte [cs_cone], 0
    je .f
    mov ax, cx
    sar ax, 1
    sar ax, 1
    add si, ax                      ; ...or 0.75...
    cmp byte [cs_cone], 1
    je .f
    add si, ax                      ; ...or all of it
.f:
    add si, dx                      ; ...+ r + |dy|
    mov ax, [cs_ddx]
    mov bx, [cs_fcosh]
    MUL14
    mov di, ax
    mov ax, [cs_ddz]
    mov bx, [cs_fsinh]
    MUL14
    sub di, ax                      ; DI = across
    jns .ac
    neg di
.ac:
    cmp di, si
    jg .out
    ; --- file it, far to near: the record in filing order, and a KEY
    ;     (along, record) inserted into cs_vkey by `along`, which is the
    ;     depth before pitch and roll - the facing's SIGN is in it, its
    ;     magnitude is not - and orders the same objects the same
    ;     way. A key is four bytes, so the insertion shifts four and not
    ;     the record's six. The record is the object, its REACH - the
    ;     offset's Manhattan length, which is what cs_scale wants of it and
    ;     was three words and a loop before - and `along`, which stands in
    ;     for the depth in cs_drawobj's size test. The rotation itself
    ;     waits for cs_drawobj, so an object the frustum refuses there has
    ;     cost the cone's four multiplies and not nine more (SPEC.md 88.5.1)
.file:
    mov di, [cs_nvisn]
    cmp di, CS_NVIS
    jae .out
    inc word [cs_nvisn]
    mov ax, di
    shl ax, 1
    add ax, di
    shl ax, 1                       ; AX = 6 x (CS_VISZ bytes a slot)...
    add ax, cs_vis                  ; ...the new record
    mov bx, ax
    mov ax, [cs_obj]
    mov [bx], ax
    mov ax, [cs_ddy]
    or ax, ax
    jns .fy
    neg ax
.fy:
    add ax, [cs_ddm]
    mov [bx+2], ax                  ; the reach: |dx| + |dz| + |dy|
    mov [bx+4], cx                  ; along
    shl di, 1
    shl di, 1
    add di, cs_vkey                 ; DI = the new key's slot
    ; The shift walks DOWN, so the source is the destination less four and
    ; needs no register of its own: [di-4] and [di-2] address it, the load
    ; the compare already makes IS the word the shift stores, and `sub di, 4`
    ; at the bottom replaces the pair that maintained SI. 26 bytes against 29
    ; and 116 cycles a shift against 131 - a held bank does 67 of them over
    ; 15 filed objects, so it is 0.21 ms a frame (SPEC.md 88.5.2.3).
.ins:
    cmp di, cs_vkey
    je .place
    mov ax, [di-4]                  ; the key above holds a NEARER object:
    cmp ax, cx                      ; it stays; else it moves down, and the
    jge .place                      ; word compared is the word moved
    mov [di], ax
    mov ax, [di-2]
    mov [di+2], ax
    sub di, 4
    jmp short .ins
.place:
    mov [di], cx
    mov [di+2], bx
.out:
    ret

; -----------------------------------------------------------------------------
; cs_range - object SI's draw range at the player's Detail (SPEC.md 88.13.2)
; cs_lodat - ...and its far-model threshold, the same way
; in:  SI = the object; out: AX. Clobbers DX only
;
; Near is 0.6 of what shipped and Far 1.6, in 8.8 - so Far holds the tower's
; near model out past four kilometres and Near lets the anonymous city come
; up close before it is drawn. A POINT OF INTEREST NEVER LOSES RANGE: Near
; is for thinning the world out, and thinning out the things you navigate by
; would be a different feature. Far still lengthens it.
; -----------------------------------------------------------------------------
cs_lodscl: dw 154, 256, 410, 512
cs_range:
    mov ax, [si + CSO_RANGE]
    cmp byte [cs_setlod], CSL_NEAR
    jne .scale
    test word [si + CSO_FLAGS], CSO_POI
    jnz .out                        ; a landmark keeps every metre of it
.scale:
    cmp word [cs_lodsc], 256        ; MODERATE is the rung the simulator ships
    je .out                         ; on and its scale is ONE: the multiply is
    mul word [cs_lodsc]             ; an identity, and an 8086 imul is ~130
    mov al, ah                      ; (DX:AX) >> 8, and no range x 410 can
    mov ah, dl                      ; overflow it - 16,000 is the largest
.out:
    ret

cs_lodat:
    mov ax, [si + CSO_LOD]
    or ax, ax
    jz .none                        ; 0 means "no far model", not "at zero"
    cmp word [cs_lodsc], 256        ; ...the same identity (88.5.2.1)
    je .none
    mul word [cs_lodsc]
    mov al, ah
    mov ah, dl
.none:
    ret

; -----------------------------------------------------------------------------
; cs_drawobj - one filed object: its vertices, faces and edges
; in:  SI = the cs_vis entry.  clobbers everything
; -----------------------------------------------------------------------------
cs_drawobj:
    mov bx, [si]
    mov [cs_obj], bx
    ; --- under two pixels across: not drawn, and NOT ROTATED FIRST. The
    ;     cull's `along` - the depth before pitch and roll - stands in for
    ;     cz, the threshold being a bound and not a measurement; the first
    ;     build spent the nine multiplies on every object it then refused,
    ;     five of fourteen in the tower frame (88.5.4). Behind or at the
    ;     eye the frustum decides -------------------------------------------
    mov cx, [si + 4]                ; CX = along
    or cx, cx
    jle .go
    push si
    mov si, [bx + CSO_MODEL]
    call cs_sizepx                  ; DX:AX = 0.75 rad scly
    pop si
    or dx, dx
    jnz .go
    shl cx, 1                       ; 2 along, under CS_FAR: it fits
    cmp ax, cx
    jb .no
.go:
    call cs_scale                   ; the origin at the object's scale, from
    mov si, [cs_obj]                ; the 16.8 eye (88.5.6), and in metres
    mov si, [si + CSO_MODEL]
    mov dx, [si + CSM_RAD]          ; DX = the radius
    mov si, cx
    add si, dx
    js .no                          ; wholly behind the eye
    mov si, cx                      ; the frustum: |cx| <= f cz + r, f the
    sar si, 1                       ; backend's 0.5 or 0.75 (over the field's
    cmp byte [cs_conebase], 0       ; 0.45 and 0.58)...
    je .fx
    mov di, cx
    sar di, 1
    sar di, 1
    add si, di
.fx:
    add si, dx
    or ax, ax
    jns .b1
    neg ax
.b1:
    cmp ax, si
    jg .no
    mov si, cx                      ; ...and |cy| <= 0.375 cz + r (the
    sar si, 1                       ; tallest view is 0.26)
    sar si, 1
    mov di, si
    sar di, 1
    add si, di
    add si, dx
    or bx, bx
    jns .b2
    neg bx
.b2:
    cmp bx, si
    jg .no
    mov bx, [cs_obj]
    or byte [bx + CSO_FLAGS + 1], CSO_SEEN >> 8
    mov word [cs_oby0], 0x7FFF      ; nothing marked yet (cs_markacc)
    mov word [cs_oby1], -1
    mov word [cs_oblo], 0x00FF      ; ...cs_oblo = 0xFF, cs_obhi = 0
    mov si, [bx + CSO_MODEL]
    mov cx, [bx + CSO_FAR]
    jcxz .mdl
    push si                         ; the NEAR model, which cs_lodat's
    push cx                         ; argument register would eat
    mov si, bx
    call cs_lodat                   ; the threshold at this Detail (88.13.2)
    mov bx, ax
    pop cx
    pop si
    cmp bx, [si + CSM_RAD]          ; ...AND NEVER INSIDE THE NEAR MODEL
    jge .thr                        ; (88.6.1.1): Near scales the threshold
    mov bx, [si + CSM_RAD]          ; to 0.6 and four of the water strips are
.thr:                               ; inside a river wider than that, so the
    mov ax, [cs_ocz]                ; A5 sat on the bay and saw the horizon
    cmp ax, bx                      ; band's ground with a line across it.
    jl .mdl                         ; The far model stands in beyond BX (dead
    mov si, cx                      ; past here: cs_sizepx below clobbers it
.mdl:                               ; anyway)
    mov [cs_mdl], si
    call cs_sizepx                  ; under two pixels across: not drawn -
    mov cx, [cs_ocz]                ; the far model's radius, and the true
    or cx, cx                       ; depth, where the test above had bounds
    jle .big                        ; (an origin behind the eye compared
    shl cx, 1                       ; unsigned as a huge depth in the first
    jc .big                         ; build, and a building whose front half
    or dx, dx                       ; was still beside the aeroplane vanished)
    jnz .big
    cmp ax, cx
    jb .out
.big:
    mov al, [si + CSM_NF]
    cmp al, 1
    sbb al, al                      ; AL = -1 for a wireframe (no faces)
    cmp byte [si + CSM_TYPE], CSM_FLAT
    je .flat
    ; --- a solid under CS_LODPX pixels across is its box, filled: one
    ;     rectangle off THREE projected points for a dozen transformed
    ;     vertices and five faces, and nobody can tell at that size.
    ;     (The first build tested this after cs_projall, through an SI that
    ;     routine had spent, so it compared a random word and no solid was
    ;     ever boxed: a basilica five kilometres off was four polygons and
    ;     ten milliseconds - 88.5.4) ---------------------------------------
    cmp byte [cs_setlod], CSL_ULTRA ; ULTRA DRAWS THE POLYGONS AT EVERY SIZE
    je .stack                       ; (88.13.2.1): the impostor is the one
                                    ; thing on this ladder that is a
                                    ; SUBSTITUTION rather than a distance, so
                                    ; the rung that wants all of it turns the
                                    ; substitution off and falls through
    or al, al
    jnz .stack                      ; a wireframe keeps its edges
    call cs_sizepx
    or dx, dx                       ; over a word: larger than any 11 cz, so
    jnz .stack                      ; the full path
    mov cx, [cs_ocz]
    ; --- ELEVEN CZ DOES NOT FIT A WORD past 5,957 m, and AX does (DX is
    ;     zero above), so beyond that the box always wins and the multiply
    ;     must not be taken at all. Without this the product WRAPPED and the
    ;     gate failed open: every solid between 5,958 m and the two-pixel
    ;     refusal drew its whole model - twelve towers four pixels across
    ;     cost 194.5 ms of an 8088's frame against 87.9 (88.5.4.2). The
    ;     CS_EDGEPX gate below guards its own 24 cz with `cmp cx, 2600`;
    ;     this one never did ------------------------------------------------
    cmp cx, 5958
    jae .lod
    mov bx, cx
    shl cx, 1
    shl cx, 1
    add cx, bx
    shl cx, 1
    add cx, bx                      ; CX = 11 cz... and the radius is nearer
    cmp ax, cx                      ; the half-height than the height, so
    jae .stack                      ; this is about 6 px
.lod:
    mov ax, [cs_ocz]
    sub ax, [si + CSM_RAD]
    cmp ax, CS_NEAR
    jl .stack                       ; reaching the near plane: the full path
    call cs_boxlod
    jnc .out                        ; ...unless it refused (88.5.4.1)
.stack:
    mov ax, CS_NEAR
    call cs_nearat
    call cs_stackverts
    call cs_projall
    jmp short .full
.flat:
    mov ax, CS_NEARG                ; the ground goes under the wheels (88.5.5)
    call cs_nearat
    call cs_flatverts
    call cs_projall
.full:
    call cs_wireclr                 ; the edges-once marks are an OBJECT's, and
%ifdef CSPROBE
    mov byte [cs_dbg_fobj], 0
%endif
    call cs_faces                   ; a vertex index means nothing across two
%ifdef CSPROBE
    call cs_probe_objend            ; ...and how many vertices went unwanted?
%endif
    ; --- a solid under CS_EDGEPX pixels across keeps no outline: twelve
    ;     segments round a fifteen-pixel box cost more than the box, and at
    ;     that size the walls' patterns already tell them apart. A wireframe
    ;     is its edges and always draws them (88.4.7) --------------------
    mov si, [cs_mdl]
    cmp byte [si + CSM_NF], 0
    je .edg
    cmp byte [cs_setfill], CSFL_ALL  ; a fill the player turned off leaves the
    jne .edg                        ; OUTLINE as the whole of the object, so
    mov cx, [cs_ocz]                ; the size test that drops it cannot run
    or cx, cx                       ; AT OR BEHIND THE EYE it is not far and
    jle .edg                        ; it is not small, it is UNDER YOU: the
                                    ; compare below is unsigned, so a negative
                                    ; cz read as 65,000-odd and took .out -
                                    ; and the runway's origin is its MIDDLE,
                                    ; so taxi past that and the strip lost its
                                    ; outline AND its centreline together.
                                    ; THE SAME LINE IS IN cs_boxlod ABOVE and
                                    ; carries the same story about a building:
                                    ; one of the two got the guard and the
                                    ; other did not (SPEC.md 88.6.2.1)
    cmp cx, 2600
    ja .out                         ; 24 cz would not fit: far and small
    mov ax, [si + CSM_RAD]
    mul word [cs_scly]
    or dx, dx
    jnz .edg
    mov bx, cx
    shl cx, 1
    add cx, bx
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = 24 cz
    cmp ax, cx
    jb .out
.edg:
    call cs_edges
    cmp word [cs_obj], cs_rwobj     ; the runway's centreline is its own
    jne .out                        ; (88.6.2)
    call cs_rwline
.out:
    mov si, [cs_oby0]               ; what the object drew goes to the glass,
    mov di, [cs_oby1]               ; marked ONCE over its rows
    cmp si, di
    jg .none
    mov ax, [cs_oblo]
    mov [cs_mklo], ax               ; ...both bytes
    call cs_markrows
.none:
.no:
    ret

; cs_sizepx - DX:AX = 0.75 x the model's radius x scly, the size tests'
;             measure: CSM_RAD is max(|x| + |y| + |z|) over the model's own
;             vertices (SPEC.md 88.5.11), a Manhattan bound that the cull
;             needs (IT MUST NEVER BE UNDER THE TRUE RADIUS) and that
;             overstates a box's real extent by up to 3/1.73; three quarters
;             of it is still over. SI = the model. Clobbers BX.
;             That sentence in brackets was here, and was false of the data
;             for as long as it has been written down - the macros halved the
;             height, 36 of 122 models were under their own radius, and
;             tests/unit/t_csrad.py is what makes the claim checkable rather
;             than repeated.
cs_sizepx:
    mov ax, [si + CSM_RAD]
    mov bx, ax
    shr bx, 1
    shr bx, 1
    sub ax, bx
    mul word [cs_scly]
    ret

; -----------------------------------------------------------------------------
; cs_boxlod - the object as its box (88.5.4): the base centre C, the top
;             centre C + h M1 with h the last level's, and the point half
;             the base's width to the right of C, projected - three points
;             and three multiplies where the full path transforms every
;             vertex - and the rectangle they span filled in the wall ink
;             through cs_rect. The model is [cs_mdl]; C is in front by its
;             radius (the caller checked)
; -----------------------------------------------------------------------------
cs_boxlod:
    mov al, [cs_pshr]               ; --- AND GIVE IT BACK (88.5.4.3). This
    mov [cs_bshr], al               ;     routine works in whole metres, but
    mov byte [cs_pshr], 0           ;     it can REFUSE, and the caller then
                                    ;     takes the full path with cs_nearat,
                                    ;     cs_stackverts and cs_flatverts all
                                    ;     reading the object's scale out of
                                    ;     this byte. Leaving 0 behind ran the
                                    ;     whole model at the wrong scale: it
                                    ;     drew at a fraction of its size, and
                                    ;     because the refusal turns on the
                                    ;     rectangle crossing CS_LODPX it did
                                    ;     that only over part of the approach
                                    ;     - which is what a building
                                    ;     FLICKERING between two sizes as the
                                    ;     aeroplane taxis actually was
    mov si, [cs_mdl]
    mov cl, [si + CSM_NV]           ; levels
    xor ch, ch
    mov si, [si + CSM_VERTS]
    mov ax, [si]                    ; the base's wx + wz, halved: near
    add ax, [si + 4]                ; enough to its width from any side
    shr ax, 1
    mov [cs_bw], ax
    dec cx
    mov ax, cx
    mov bx, 6
    mul bx
    add si, ax
    mov ax, [si + 2]                ; h of the top level
    mov si, cs_m + 2
    mov di, cs_col1
    call cs_colscale                ; col1 = h M1
    mov ax, [cs_ocx]
    mov bx, [cs_ocy]
    mov cx, [cs_ocz]
    call cs_project0                ; the base
    mov [cs_bx0], ax
    mov [cs_by1], bx
    mov ax, [cs_ocx]
    add ax, [cs_col1]
    mov bx, [cs_ocy]
    add bx, [cs_col1 + 2]
    mov cx, [cs_ocz]
    add cx, [cs_col1 + 4]
    cmp cx, CS_NEAR
    jl .out
    call cs_project0                ; the top - BOTH of it (SPEC.md 88.5.4.6),
    mov [cs_bx1], ax                ; because under bank the up axis leans
    mov [cs_by0], bx                ; and its x is where the lean is
    mov ax, [cs_ocx]
    add ax, [cs_bw]
    mov bx, [cs_ocy]
    mov cx, [cs_ocz]
    call cs_project0                ; the right edge
    sub ax, [cs_bx0]
    jg .w
    mov ax, 1                       ; a pixel at least
.w:
    ; --- THE IMPOSTOR BANKS WITH THE WORLD (SPEC.md 88.5.4.6). The width is
    ;     projected along the CAMERA's x, so its MAGNITUDE is right at any
    ;     bank and only its direction is wrong; the screen-space horizontal
    ;     is (cos r, -sin r) EXACTLY, because the up vector in camera space
    ;     is the matrix's second column (m01, m11) = cos p (-sin r, cos r)
    ;     and its perpendicular's own cos p divides out. So it is the
    ;     frame's own sine and cosine and one multiply an axis - no square
    ;     root, and nothing per object that is not already here.
    mov [cs_bwp], ax
    imul word [cs_cosr]             ; R = w (cos r, -sin r), ROUNDED: a
    add ax, 1 << 14                 ; half-width is six pixels and a
    adc dx, 0                       ; truncated 6 x 32767 >> 15 is five
    shl ax, 1
    rcl dx, 1
    mov [cs_brx], dx
    mov ax, [cs_bwp]
    imul word [cs_sinr]
    add ax, 1 << 14
    adc dx, 0
    shl ax, 1
    rcl dx, 1
    neg dx                          ; screen y grows DOWNWARD (88.5.4)
    mov [cs_bry], dx
    ; --- AND REFUSE ONE THAT IS NOT SMALL (SPEC.md 88.5.4.1). The gate that
    ;     sent us here is on CSM_RAD, an ESTIMATE of the object's reach and
    ;     not the height of the rectangle actually drawn - which is the whole
    ;     of h projected. Measured over 210 poses it let rectangles of 22 x 3
    ;     through on a 400-wide view, where its own comment claims six
    ;     pixels. So the test is on the RECTANGLE and not on an estimate of
    ;     it, and a refusal costs three projections and takes the full path -
    ;     which is what the object wanted anyway.
    ;     (The estimate used to UNDER-state a tall building, h/2 where the
    ;     origin is the base; 88.5.11 fixed that and this test still stands,
    ;     because a sound bound is still not the drawn rectangle.)
    ;     THE HEIGHT GETS ITS OWN BOUND (88.5.4.4). The poses that made the
    ;     complaint were WIDE and flat - 22x3, 20x2, 18x2 - and holding the
    ;     height to the same eight put a two-pixel-wide tower back on the
    ;     polygon path, where its faces are sub-pixel: the Empire State's one
    ;     visible face read a cross product of 17, a pixel of rounding took
    ;     it to 0, which the winding test reads as `away', and the whole
    ;     building vanished. AND BOTH BOUNDS HAVE HYSTERESIS: an object
    ;     already drawn as the impostor keeps it until it grows CS_LODHYST
    ;     past them, so one sitting on the boundary does not change shape
    ;     every few metres.
    ;     AND BOTH BOUNDS ARE THE BOX'S OWN, not the rectangle's bounding
    ;     box (88.5.4.6): a banked impostor's box grows with the bank - a
    ;     6 x 26 one is 23 x 27 at 45 degrees - so testing that would send a
    ;     building to the polygon path for rolling, and back for levelling
    ;     out. The width is the projected half-width doubled, and the height
    ;     the LENGTH of the up axis rather than its vertical part, which is
    ;     what shrank by cos r and made the building squat as it banked.
    mov di, [cs_obj]
    test word [di + CSO_FLAGS], CSO_BOXED
    mov di, 0                       ; ...the slack it has earned
    jz .lim
    mov di, CS_LODHYST
.lim:
    mov si, [cs_bwp]
    shl si, 1
    sub si, di
    cmp si, CS_LODPX
    jg .out                         ; too wide...
    mov ax, [cs_bx1]
    sub ax, [cs_bx0]
    jge .dx
    neg ax
.dx:
    mov bx, [cs_by1]
    sub bx, [cs_by0]
    jge .dy
    neg bx
.dy:
    cmp ax, bx                      ; |up2| as max + min/2: 0 to +6% of a
    jae .mx                         ; length, against a bound that carries
    xchg ax, bx                     ; CS_LODHYST of slack anyway - where a
.mx:                                ; square root would be 200 cycles for a
    shr bx, 1                       ; threshold nobody measures
    add ax, bx
    sub ax, di
    cmp ax, CS_LODTALL
    jg .out                         ; ...or too tall
    ; --- WINGS LEVEL: THE OLD RECTANGLE, and BYTE FOR BYTE the old picture
    ;     (SPEC.md 88.5.4.7). With R horizontal the impostor's two caps are
    ;     horizontal too, and the shape cs_rect draws differs from the quad
    ;     only by the top's own LEAN - one or two pixels of the perspective
    ;     the polygon path shows and this one has always squared off. The
    ;     reported defect is a BANKED one, so level flight keeps the
    ;     rectangle and pays nothing at all: no four edges traced, and none
    ;     of cs_poly's own per-polygon setup.
    mov cx, [cs_brx]
    mov dx, [cs_bry]
    or dx, dx
    jnz .quad
    mov al, CSI_WALL
    call cs_setink
    mov ax, [cs_bx0]
    mov cx, ax
    sub ax, [cs_brx]
    add cx, [cs_brx]
    mov bx, [cs_by0]
    mov dx, [cs_by1]
    cmp bx, dx
    jle .rect
    xchg bx, dx                     ; seen from below: the top is lower
.rect:
    call cs_rect
    jmp short .done
    ; --- the four corners, into cs_pv. That is the FACE buffer and it is
    ;     dead here: an impostor returns before cs_stackverts and a refusal
    ;     goes to cs_faces, which rebuilds it per face. The order puts the
    ;     rising edge on the left, which is cs_pwind = 0.
.quad:
    mov ax, [cs_bx0]
    mov bx, [cs_by1]                ; the base
    mov si, ax
    add si, cx
    mov [cs_pv + 0], si             ; v0 = base + R
    mov si, bx
    add si, dx
    mov [cs_pv + 2], si
    sub ax, cx
    sub bx, dx
    mov [cs_pv + 12], ax            ; v3 = base - R
    mov [cs_pv + 14], bx
    mov ax, [cs_bx1]
    mov bx, [cs_by0]                ; the top
    mov si, ax
    add si, cx
    mov [cs_pv + 4], si             ; v1 = top + R
    mov si, bx
    add si, dx
    mov [cs_pv + 6], si
    sub ax, cx
    sub bx, dx
    mov [cs_pv + 8], ax             ; v2 = top - R
    mov [cs_pv + 10], bx
    mov byte [cs_pwind], 0
    mov ax, [cs_by0]
    cmp ax, [cs_by1]
    jle .wind
    mov byte [cs_pwind], 1          ; seen from below: the top is lower, and
.wind:                              ; the list runs the other way round
    mov byte [cs_pwhole], 0         ; the impostor marks its own rows, where
                                    ; cs_rect always did (88.4.4)
    ; --- (this shape's own exemption from the sentinel pass is gone with
    ;     the pass itself - SPEC.md 88.4.2.2 retires it for every polygon)
    mov al, CSI_WALL
    call cs_setink
    mov si, cs_pv
    mov cx, 4
    call cs_poly
.done:
    mov di, [cs_obj]
    or word [di + CSO_FLAGS], CSO_BOXED
    mov al, [cs_bshr]
    mov [cs_pshr], al
    clc                             ; drawn
    ret
.out:
    mov di, [cs_obj]
    and word [di + CSO_FLAGS], ~CSO_BOXED
    mov al, [cs_bshr]               ; the object's own scale, back
    mov [cs_pshr], al
    stc                             ; REFUSED: the caller takes the full path
    ret

; -----------------------------------------------------------------------------
; cs_stackverts - a STACK's levels into cs_cxv/cyv/czv, four a level
; -----------------------------------------------------------------------------
cs_stackverts:
    mov si, [cs_mdl]
    mov cl, [si + CSM_NV]
    xor ch, ch
    mov ax, cx
    shl ax, 1
    shl ax, 1
    mov [cs_nv], ax                 ; four vertices a level
    mov si, [si + CSM_VERTS]
    xor di, di                      ; DI = the vertex index x 2
.lvl:
    push cx
    push si
    push di
    mov cl, [cs_pshr]               ; the level at the object's scale (88.5.6)
    mov ax, [si]                    ; wx
    shl ax, cl
    mov [cs_lwx], ax
    mov ax, [si+4]                  ; wz
    shl ax, cl
    mov [cs_lwz], ax
    mov ax, [si+2]                  ; h -> ey = h M1
    shl ax, cl
    mov si, cs_m + 2
    mov di, cs_col1
    call cs_colscale
    mov ax, [cs_scy]                ; the level's centre: C + ey
    add ax, [cs_col1 + 2]
    mov [cs_lcy], ax
    mov ax, [cs_scx]
    add ax, [cs_col1]
    mov [cs_lcx], ax
    mov ax, [cs_scz]
    add ax, [cs_col1 + 4]
    mov [cs_lcz], ax
    mov ax, [cs_lwx]
    mov si, cs_m
    mov di, cs_col0
    call cs_colscale                ; ex = wx M0
    mov ax, [cs_lwz]
    mov si, cs_m + 4
    mov di, cs_col2
    call cs_colscale                ; ez = wz M2
    pop di
    ; v0 = c + ex + ez, v1 = c - ex + ez, v2 = c - ex - ez, v3 = c + ex - ez
    mov ax, [cs_lcx]
    add ax, [cs_col0]
    add ax, [cs_col2]
    mov [cs_cxv + di], ax
    sub ax, [cs_col0]
    sub ax, [cs_col0]
    mov [cs_cxv + di + 2], ax
    sub ax, [cs_col2]
    sub ax, [cs_col2]
    mov [cs_cxv + di + 4], ax
    add ax, [cs_col0]
    add ax, [cs_col0]
    mov [cs_cxv + di + 6], ax
    mov ax, [cs_lcy]
    add ax, [cs_col0 + 2]
    add ax, [cs_col2 + 2]
    mov [cs_cyv + di], ax
    sub ax, [cs_col0 + 2]
    sub ax, [cs_col0 + 2]
    mov [cs_cyv + di + 2], ax
    sub ax, [cs_col2 + 2]
    sub ax, [cs_col2 + 2]
    mov [cs_cyv + di + 4], ax
    add ax, [cs_col0 + 2]
    add ax, [cs_col0 + 2]
    mov [cs_cyv + di + 6], ax
    mov ax, [cs_lcz]
    add ax, [cs_col0 + 4]
    add ax, [cs_col2 + 4]
    mov [cs_czv + di], ax
    sub ax, [cs_col0 + 4]
    sub ax, [cs_col0 + 4]
    mov [cs_czv + di + 2], ax
    sub ax, [cs_col2 + 4]
    sub ax, [cs_col2 + 4]
    mov [cs_czv + di + 4], ax
    add ax, [cs_col0 + 4]
    add ax, [cs_col0 + 4]
    mov [cs_czv + di + 6], ax
    add di, 8
    pop si
    add si, 6
    pop cx
    LOOPF .lvl
    ret

; -----------------------------------------------------------------------------
; cs_flatverts - a FLAT model's (x, z) pairs: C + x M0 + z M2, six multiplies
;
; The six are all that has to be here, and for a long time they were about
; half of what it cost: the columns went through cs_colscale, which WRITES
; ITS THREE WORDS TO MEMORY, and the caller then read all six back and added
; them - eight push/pops, two call/rets and twelve memory round-trips a
; vertex around 900 cycles of arithmetic (88.5.6.2).
;
; What deletes the lot is which register the scalar sits in. MUL14 is
; `imul bx`, so it clobbers AX and DX and LEAVES BX ALONE - put the scalar
; in BX and the matrix element in AX (a multiply is commutative, so the
; product is the same bit for bit) and one scalar serves all three of its
; multiplies with no save at all. CL then holds [cs_pshr] for the whole
; routine rather than being reloaded twice a vertex, BP is the counter, and
; each product lands straight in its output word: the x column STORES and
; the z column ADDS INTO the same word. cs_col0/cs_col2 are not touched.
; -----------------------------------------------------------------------------
cs_flatverts:
    mov si, [cs_mdl]
    mov bl, [si + CSM_NV]
    xor bh, bh
    mov [cs_nv], bx
    mov bp, bx                      ; BP = the vertices left
    mov si, [si + CSM_VERTS]
    xor di, di
    mov cl, [cs_pshr]               ; the pair at the object's scale (88.5.6)
%ifdef CSPROBE
    mov byte [cs_dbg_fvfirst], 1    ; the first vertex has no predecessor
%endif
.v:
%ifdef CSPROBE
    push cx
    call cs_probe_fv
    pop cx
%endif
    mov bx, [si]                    ; --- x M0, into the three outputs -------
    shl bx, cl
    mov ax, [cs_m]
    MUL14
    add ax, [cs_scx]
    mov [cs_cxv + di], ax
    mov ax, [cs_m + 6]
    MUL14
    add ax, [cs_scy]
    mov [cs_cyv + di], ax
    mov ax, [cs_m + 12]
    MUL14
    add ax, [cs_scz]
    mov [cs_czv + di], ax
    mov bx, [si + 2]                ; --- z M2, added into them --------------
    shl bx, cl
    mov ax, [cs_m + 4]
    MUL14
    add [cs_cxv + di], ax
    mov ax, [cs_m + 10]
    MUL14
    add [cs_cyv + di], ax
    mov ax, [cs_m + 16]
    MUL14
    add [cs_czv + di], ax
    add di, 2
    add si, 4
    dec bp
    jnz .v
    ret

; -----------------------------------------------------------------------------
; cs_projall - project every vertex in front of the near plane, flag the rest
; -----------------------------------------------------------------------------
cs_projall:
%ifdef CSPROBE
    call cs_probe_pv                ; ...and could ANY of these be skipped?
%endif
    mov byte [cs_pwhole], 0
    mov byte [cs_pinview], 0
    mov cx, [cs_nv]
    jcxz .out0
    ; --- can any vertex be past a side at all? Every one is within the
    ;     radius of the origin on each axis, so when |ox| + r and |oy| + r
    ;     are both under 4 (oz - r) none is, and the per-vertex test below
    ;     - 180 cycles of the 1,200 a vertex cost - is skipped for the
    ;     object (88.5.7). At the object's scale: r << pshr fits, the scale
    ;     rule bounding it -------------------------------------------------
    mov si, [cs_mdl]
    mov ax, [si + CSM_RAD]
    mov cl, [cs_pshr]
    shl ax, cl                      ; AX = r at the scale
    mov bx, [cs_scz]
    sub bx, ax                      ; BX = oz - r
    jle .sides                      ; reaching the eye: test every vertex
    mov cx, [cs_scx]
    or cx, cx
    jns .ax
    neg cx
.ax:
    add cx, ax                      ; CX = |ox| + r
    jc .sides
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; 88.5.7.3: the exemption cone is the
    cmp cx, bx                      ; CLAMP's (8z, under 9) and not the clip
    ja .sides                       ; planes' 4z - twice the objects exempt
    mov cx, [cs_scy]
    or cx, cx
    jns .ay
    neg cx
.ay:
    add cx, ax
    jc .sides
    shr cx, 1
    shr cx, 1
    shr cx, 1
    cmp cx, bx
    ja .sides
    mov byte [cs_pinside], 1
    ; --- ...and wholly in front of the near plane too: then it is WHOLE,
    ;     nothing of it is clipped, and its box for the blit is the box of
    ;     its projected vertices, taken here once, instead of each face
    ;     and edge widening it on its own (88.3.2) ---------------------------
    cmp bx, [cs_near]
    jl .go
    mov byte [cs_pwhole], 1
    mov word [cs_obx0], 0x7FFF
    mov word [cs_obx1], -32768
    jmp short .go
.sides:
    mov al, [cs_noside]             ; 0 normally; with the A/B knob on, the
    mov [cs_pinside], al            ; object counts as inside every side plane
.go:
    mov cx, [cs_nv]
    xor di, di
    jmp short .v
.out0:
    ret
.v:
    push cx
    mov cx, [cs_czv + di]
    cmp cx, [cs_near]
    jl .behind
    mov ax, [cs_cxv + di]
    mov bx, [cs_cyv + di]
    push di
    call [cs_projp]
    pop di
    mov [cs_sxv + di], ax
    mov [cs_syv + di], bx
    mov word [cs_fv + di], 1
    cmp byte [cs_pwhole], 0
    jne .box
    cmp byte [cs_pinside], 0
    jne .next
    ; --- in front, but outside the frustum's SIDES (|x| or |y| over 4z)?
    ;     Its projection was clamped, and a line through a clamped point is
    ;     bent: the face or edge it is on gets clipped in camera space
    ;     instead (88.5.7)
    mov cx, [cs_czv + di]
    mov ax, [cs_cxv + di]
    or ax, ax
    jns .px
    neg ax
.px:
    sar ax, 1
    sar ax, 1
    sar ax, 1                       ; 88.5.7.3: EIGHT z, not four - the
    cmp ax, cx                      ; projection CLAMPS at 9z, so a vertex
    jg .side                        ; inside 8z projects exactly and no line
    mov ax, [cs_cyv + di]           ; through it can bend
    or ax, ax
    jns .py
    neg ax
.py:
    sar ax, 1
    sar ax, 1
    sar ax, 1                       ; ...and y clamps at 14z, so 8 is doubly
    cmp ax, cx                      ; conservative on this axis
    jle .next
.side:
    mov word [cs_fv + di], 2
    jmp short .next
.box:                               ; a whole object's box, vertex by vertex
    cmp ax, [cs_obx0]
    jge .x0
    mov [cs_obx0], ax
.x0:
    cmp ax, [cs_obx1]
    jle .x1
    mov [cs_obx1], ax
.x1:
    cmp bx, [cs_oby0]
    jge .y0
    mov [cs_oby0], bx
.y0:
    cmp bx, [cs_oby1]
    jle .next
    mov [cs_oby1], bx
    jmp short .next
.behind:
    mov word [cs_fv + di], 0
    ; --- ...AND THE OBJECT WAS NOT WHOLE AFTER ALL (SPEC.md 88.5.11.1).
    ;     cs_pwhole is a PREDICTION off CSM_RAD, and this is the moment it
    ;     is found out: a vertex behind the near plane has no cs_sxv this
    ;     frame, and cs_edge1's `jne .both` would draw an edge to whatever
    ;     the last object left in that slot - unclipped, because .conv would
    ;     have set cs_pinview off a box that never saw this vertex. Clearing
    ;     it here costs nothing on the fast path (this branch is the slow
    ;     case already), skips .conv, and puts every face and edge back on
    ;     the per-vertex path, which is CORRECT for any radius at all. With
    ;     88.5.11's bound sound it cannot fire; it is what makes a wrong
    ;     bound a slow frame instead of a line across the cockpit.
    ;     What it leaves behind is cs_oby0/cs_oby1 already widened by the
    ;     vertices .box took before this fired, so cs_markacc accumulates
    ;     onto a box instead of onto cs_drawobj's sentinel and the object
    ;     marks MORE rows than it drew on. That is the safe direction - rows
    ;     blitted that did not change - and it is not worth 12 bytes of
    ;     sentinel to tidy up a path a sound bound never reaches.
    mov byte [cs_pwhole], 0
.next:
    add di, 2
    pop cx
    LOOPF .v
    cmp byte [cs_pwhole], 0
    jne .conv
    ret
.conv:
    ; --- the box to the view, and its bytes: inside the view outright and
    ;     every segment of the object skips the clip test (cs_pinview) ------
    mov ax, [cs_obx0]
    mov cx, [cs_obx1]
    mov bx, [cs_oby0]
    mov dx, [cs_oby1]
    cmp ax, [cs_wx0]
    jl .clip
    cmp cx, [cs_wx1]
    jg .clip
    or bx, bx
    js .clip
    cmp dx, [cs_wh]
    jge .clip
    mov byte [cs_pinview], 1
    jmp short .bytes
.clip:
    cmp ax, [cs_wx0]
    jge .cx0
    mov ax, [cs_wx0]
.cx0:
    cmp cx, [cs_wx1]
    jle .cx1
    mov cx, [cs_wx1]
.cx1:
    cmp ax, cx
    jg .none                        ; beside the view: nothing to mark
    or bx, bx
    jns .cy0
    xor bx, bx
.cy0:
    cmp dx, [cs_wh]
    jl .cy1
    mov dx, [cs_wh]
    dec dx
.cy1:
    mov [cs_oby0], bx
    mov [cs_oby1], dx
.bytes:
    mov dx, cx
    mov cl, [cs_kshift]
    shr ax, cl
    shr dx, cl
    mov [cs_oblo], al
    mov [cs_obhi], dl
    ret
.none:
    mov word [cs_oby0], 0x7FFF      ; cs_drawobj marks nothing
    mov word [cs_oby1], -1
.out:
    ret

; -----------------------------------------------------------------------------
; cs_axcull - is this STACK face pointing AWAY from the eye? (SPEC.md 88.5.12)
;
; The winding test (88.5.10) is exact and stays the authority, but it is taken
; from the face's PROJECTED points - so a face it throws away has already been
; counted, gathered into cs_pv and paid two `imul`s. 29-60% of walked faces are
; thrown away that way.
;
; This decides the same thing FIRST and with NO MULTIPLY AT ALL. A side face of
; an untapered level pair is an axis-aligned plane in WORLD space, so "the eye
; is behind it" is one compare against the eye's own world position - and
; cs_scale already has that offset, because the rotation's input IS it. (The
; roundabout way to see the same thing: the face's outward normal in camera
; space is a column of M, and dot(M0, C) = dot(M x, M d) = d.x for an
; orthonormal M, so the camera-space dot product a normal test would take
; reduces to the world offset that was there before the rotation.)
;
; It REFUSES rather than guesses, and every refusal falls through to the
; winding: a TAPERED level pair tilts the plane (a pyramid, a dome's cap, a
; setback), a face with no axis flag is not a stack side or cap, and CSF_NOCULL
; is left alone.
;
; in:  [cs_fflags], [cs_fidx], [cs_mdl], [cs_odx/ody/odz], [cs_pshr]
; out: CF = 1 the face points away and may be skipped whole. Clobbers
;      AX, BX, CX, DX, DI - and PRESERVES SI, which is load-bearing: the
;      caller falls through to `.cnt`, and that arm walks the face's indices
;      with `lodsb` FROM THE SI IT ALREADY HAS. `.plain` reloads SI from
;      [cs_fidx] and `.cnt` does not, so clobbering it here is invisible on
;      every object that is WHOLE (88.3.2) and corrupts the near/side count
;      on every object that is not - which is a handful of river pieces in
;      one scene, drawn wrong in a 32x6 patch. The pixel A/B caught it; the
;      verdict audit could not, both tests agreeing about a face whose
;      indices were then read from the wrong place.
; -----------------------------------------------------------------------------
cs_axcull:
    push si
    mov dl, [cs_fflags]
    test dl, CSF_NOCULL             ; a ground polygon is drawn either way
    jnz .no
%ifdef CSPROBE
    and dl, [cs_axmask]             ; BISECT: one family of faces at a time
%endif
    test dl, CSF_AXES
    jz .no
    mov si, [cs_fidx]               ; the level: every one of the five faces
    mov bl, [si]                    ; leads with an index in its own base
    xor bh, bh                      ; level, so index >> 2 names it
    shr bx, 1
    shr bx, 1
    mov si, [cs_mdl]
    mov si, [si + CSM_VERTS]
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1                       ; BX = level x 6: (wx, h, wz)
    add si, bx
    mov cl, [cs_pshr]
    test dl, CSF_TOP
    jnz .top
    xor di, di                      ; the x faces read wx, the z faces wz
    test dl, CSF_PZ | CSF_MZ
    jz .ax
    mov di, 4
.ax:
    mov bx, si
    add bx, di
    mov ax, [bx]
    cmp ax, [bx + 6]                ; TAPERED: the plane is not axis-aligned,
    jne .no                         ; so this test does not apply to it
    shl ax, cl                      ; the half-width at the object's scale -
    mov cx, [cs_odx]                ; the scale rule (88.5.6) bounds it by the
    test dl, CSF_PZ | CSF_MZ        ; radius, so this cannot overflow
    jz .haved
    mov cx, [cs_odz]
.haved:
    test dl, CSF_PX | CSF_PZ
    jz .minus
    neg ax                          ; +face: the eye is in front iff d < -w
    cmp cx, ax
    jl .no
    pop si
    stc
    ret
.minus:
    cmp cx, ax                      ; -face: ...iff d > w
    jg .no
    pop si
    stc
    ret
.top:
    mov ax, [si + 2]                ; the cap: the eye is above it iff dy < -h
    shl ax, cl
    neg ax
    cmp [cs_ody], ax
    jl .no
    pop si
    stc
    ret
.no:
    pop si
    clc
    ret

; -----------------------------------------------------------------------------
; cs_faces - every face of [cs_mdl]: near-clipped where it has to be, culled
;            by the sign of one cross product, filled
; -----------------------------------------------------------------------------
cs_faces:
    mov si, [cs_mdl]
    mov cl, [si + CSM_NF]
    xor ch, ch
    JCXZF .out
    mov si, [si + CSM_FACES]
.f:
    push cx
    push si
    mov al, [si]                    ; n
    xor ah, ah
    mov [cs_fn], ax
    mov al, [si+1]
    mov [cs_fink], al
    mov al, [si+2]
    mov [cs_fflags], al
    add si, 3
    mov [cs_fidx], si
    call cs_axcull                  ; pointing AWAY in WORLD space: skipped
%ifdef CSPROBE                      ; before it is counted, gathered or
    mov byte [cs_dbg_ax], 0         ; wound at all (88.5.12)
    jnc .nax
    mov byte [cs_dbg_ax], 1
    inc word [cs_dbg_axcull]
.nax:
    cmp byte [cs_axoff], 0          ; AUDIT: compute, do not act
    jne .noax
    cmp byte [cs_dbg_ax], 0
    jne .skip
.noax:
%else
    jc .skip
%endif
%ifdef CSPROBE
    cmp byte [cs_dbg_ax], 0         ; a face axcull would have KEPT: its
    jne .nomark                     ; vertices are wanted, and the mark is
    call cs_probe_used              ; taken even when the cull is not ACTING,
.nomark:                            ; so an audit run counts the same thing
%endif
    cmp byte [cs_pwhole], 0         ; a whole object: no end is behind or
    jne .plain                      ; past a side, so the count is skipped
                                    ; (and .plain clears cs_fcut for it)
    ; --- how many ends are behind the near plane, how many past a side ------
    mov cx, [cs_fn]
    xor dx, dx                      ; DL = behind, DH = outside
.cnt:
    lodsb
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [cs_fv + bx]
    or ax, ax
    jnz .c1
    inc dl
.c1:
    cmp al, 2
    jne .c2
    inc dh
.c2:
    loop .cnt
    cmp dl, [cs_fn]
    je .skip                        ; all behind
    or dx, dx
    jz .plain                       ; all in front and inside: the projected
    mov byte [cs_fcut], 1           ; points as they are; else clip in
    call cs_fclip                   ; camera space and project the rest -
    jmp short .fill                 ; and cs_pv is then NOT the model's
                                    ; vertices, which cs_wire has to know
.plain:
    mov byte [cs_fcut], 0
    mov si, [cs_fidx]
    mov cx, [cs_fn]
    mov [cs_pn], cx
    mov di, cs_pv
.cp:
    lodsb
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [cs_sxv + bx]
    mov [di], ax
    mov ax, [cs_syv + bx]
    mov [di+2], ax
    add di, 4
    loop .cp
.fill:
%ifdef CSPROBE
    inc word [cs_dbg_fwalk]
    call cs_probe_face
%endif
    mov cx, [cs_pn]
    cmp cx, 3
    jb .skip
    ; --- THE WHOLE POLYGON'S SIGNED AREA (88.5.10): screen y down, so a face
    ;     toward us is negative, and a ground polygon that may be either way
    ;     round still needs the sign, because it says which chain an edge is
    ;     on (cs_pwind).
    ;
    ;     It was ONE triangle - (v1 - v0) x (v2 - v0) - and that is a
    ;     fraction of a trapezoid's area: the Empire State's crown at 4.2 km
    ;     read 25 against the polygon's 175, on vertices rounded to whole
    ;     pixels, so the sign was noise and the two crown faces went on and
    ;     off a frame at a time, each on its own.
    ;
    ;     A QUADRILATERAL IS ITS DIAGONALS' CROSS, which is the whole area
    ;     in the SAME TWO MULTIPLIES the one triangle took - the eight terms
    ;     of (v2 - v0) x (v3 - v1) are the shoelace's eight, so it is exact
    ;     for a folded quad as well as a convex one. Every face a model
    ;     declares is a quad (CS_SIDES, CS_TOP), so the fix is free on all of
    ;     them; the fan below is for the 3 to 7 points a clip leaves.
    cmp cx, 4
    jne .fan
    mov ax, [cs_pv + 8]             ; (x2 - x0)(y3 - y1)...
    sub ax, [cs_pv]
    mov bx, [cs_pv + 14]
    sub bx, [cs_pv + 6]
    imul bx
    mov cx, ax
    mov si, dx
    mov ax, [cs_pv + 10]            ; ...minus (y2 - y0)(x3 - x1)
    sub ax, [cs_pv + 2]
    mov bx, [cs_pv + 12]
    sub bx, [cs_pv + 4]
    imul bx
    sub cx, ax
    sbb si, dx
    jmp short .wound
.fan:
    xor cx, cx
    xor si, si                      ; SI:CX = twice the signed area
    mov ax, [cs_pn]
    dec ax
    dec ax
    mov [cs_pfan], ax               ; n - 2 triangles in the fan
    mov di, cs_pv + 4               ; ...from v1
.tri:
    push di
    mov ax, [di]                    ; (xk - x0)(y(k+1) - y0)
    sub ax, [cs_pv]
    mov bx, [di + 6]
    sub bx, [cs_pv + 2]
    imul bx
    add cx, ax
    adc si, dx
    pop di
    push di
    mov ax, [di + 2]                ; ...minus (yk - y0)(x(k+1) - x0)
    sub ax, [cs_pv + 2]
    mov bx, [di + 4]
    sub bx, [cs_pv]
    imul bx
    sub cx, ax
    sbb si, dx
    pop di
    add di, 4
    dec word [cs_pfan]
    jnz .tri
.wound:
    or si, si                       ; TEST THE SUM, not the loop counter: the
    mov byte [cs_pwind], 0          ; one triangle this replaced left its own
    js .draw                        ; flags in the `sbb` and the `js` read
                                    ; them, and `dec` clobbers them - which
                                    ; culls every face there is
    mov byte [cs_pwind], 1
    test byte [cs_fflags], CSF_NOCULL
%ifdef CSPROBE
    jnz .draw
    inc word [cs_dbg_fcull]
    cmp byte [cs_dbg_ax], 0         ; the winding culls it: did the axis test?
    je .axmiss
    inc word [cs_dbg_axagree]
    jmp short .skip
.axmiss:
    inc word [cs_dbg_axmiss]
    jmp short .skip
%else
    jz .skip                        ; away, or edge-on
%endif
.draw:
%ifdef CSPROBE
    cmp byte [cs_dbg_ax], 0         ; the winding DRAWS it: the axis test must
    je .axok                        ; not have culled it, or the picture moves
    inc word [cs_dbg_axbad]
    push ax
    mov ax, [cs_mdl]
    mov [cs_dbg_axmdl], ax
    mov al, [cs_fflags]
    xor ah, ah
    mov [cs_dbg_axflg], ax
    pop ax
.axok:
%endif
    mov al, [cs_fink]               ; --- IS THIS FILL WANTED? (88.13.3) The
    call cs_fillon                  ;     ink says what the face is made of;
    jc .wire                        ;     with it off the face is its OUTLINE
    mov al, [cs_fink]
    call cs_setink
    mov si, cs_pv
    mov cx, [cs_pn]
%ifdef CSPROBE
    inc word [cs_dbg_fpoly]
    cmp byte [cs_dbg_fobj], 0
    jne .nfo
    mov byte [cs_dbg_fobj], 1
    inc word [cs_dbg_ftr]
.nfo:
%endif
    call cs_poly
    jmp short .skip
.wire:
    call cs_wire
.skip:
    pop si
    mov al, [si]
    xor ah, ah
    add si, ax
    add si, 3
    pop cx
    LOOPF .f
.out:
    ret

; -----------------------------------------------------------------------------
; cs_wire - the face at cs_pv/cs_pn as an OUTLINE, EACH EDGE ONCE (88.13.3)
;
; Every edge of a closed solid is shared by exactly two faces, so outlining
; each front face in turn draws every shared edge TWICE - the same pixels, at
; the same cost, for no picture. This marks each edge by its two VERTEX
; INDICES as it draws it and skips one already marked; the marks are cleared
; once an object (cs_wireclr), because indices are the model's and mean
; nothing across models.
;
; A face the near plane CUT has no such indices - cs_pv holds points the
; clipper made - so that one is drawn whole. It is the near case only, and
; the alternative is not drawing an edge that is there.
;
; in:  cs_pv/cs_pn projected, cs_fidx = the face's vertex indices, cs_fcut
;      set when the clipper ran.  Clobbers everything.
; -----------------------------------------------------------------------------
cs_wire:
    mov cx, [cs_pn]
    cmp cx, 2
    jb .out
    mov [cs_wn], cx
    mov al, [cs_fink]               ; the outline is the FACE's own ink, so a
    call cs_setink                  ; hill's ridge is a hill and a wall a wall
    xor di, di                      ; DI = the vertex, 0..n-1
.e:
    mov si, di                      ; the segment i -> i + 1, wrapping
    shl si, 1
    shl si, 1
    add si, cs_pv
    mov ax, [si]
    mov bx, [si + 2]
    mov si, di
    inc si
    cmp si, [cs_wn]
    jb .j
    xor si, si                      ; the last edge closes on vertex 0
.j:
    mov [cs_wj], si
    shl si, 1
    shl si, 1
    add si, cs_pv
    mov cx, [si]
    mov dx, [si + 2]
    cmp byte [cs_fcut], 0
    jne .draw                       ; a cut face has no indices: draw it whole
    push ax
    push bx
    push cx
    push dx
    mov si, [cs_fidx]               ; the face's vertex indices
    mov bx, di
    mov al, [si + bx]
    mov bx, [cs_wj]
    mov ah, [si + bx]
    call cs_edgemark
    pop dx
    pop cx
    pop bx
    pop ax
    jc .next                        ; the other face drew it already
.draw:
    push di
    call cs_seg
    pop di
.next:
    inc di
    cmp di, [cs_wn]
    jb .e
.out:
    ret

; -----------------------------------------------------------------------------
; cs_edgemark - has the edge between vertices AL and AH been drawn already?
;               Marks it if not.  out: CF = 1 it had been. Clobbers AX, BX, CX.
;
; A bit per unordered pair, four bytes a row: the LOW index picks the row and
; the HIGH one the bit in it, so an edge has one address whichever face
; reaches it first.
; -----------------------------------------------------------------------------
cs_edgemark:
    cmp al, ah                      ; the pair, low end first
    jbe .ord
    xchg al, ah
.ord:
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1                       ; BX = the row, four bytes each
    mov cl, ah
    shr cl, 1
    shr cl, 1
    shr cl, 1
    xor ch, ch
    add bx, cx                      ; ...and the byte within it
    mov cl, ah
    and cl, 7
    mov ch, 1
    shl ch, cl                      ; CH = the bit
    mov al, [cs_eseen + bx]
    test al, ch
    jnz .seen
    or al, ch
    mov [cs_eseen + bx], al
    clc
    ret
.seen:
    stc
    ret

%ifdef CSPROBE
; -----------------------------------------------------------------------------
; cs_probe_face - PROBE ONLY, A/B 4: with [cs_dupface] set, repeat the per-face
;                 PREAMBLE - the gather of the face's projected vertices by
;                 index, and the quad's diagonal cross that decides its winding
;                 - into scratch, once more. The same instructions on the same
;                 data, discarded, so the picture is untouched and the frame's
;                 difference over the faces walked is what one face's preamble
;                 costs. 29-40% of them are then thrown away by the winding,
;                 which is what that number is wanted for.
;                 Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_face:
    mov al, [cs_dupface]
    or al, [cs_dupgath]
    or al, [cs_duparea]
    jnz .go
    ret
.go:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [cs_fcut], 0
    jne .out                        ; a clipped face's points are the
    mov al, [cs_dupface]            ; clipper's, not the model's
    or al, [cs_dupgath]
    jz .area                        ; ...cs_duparea prices the CROSS alone
    mov si, [cs_fidx]
    mov cx, [cs_fn]
    mov di, cs_dbg_pv
    cld
.cp:
    lodsb
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [cs_sxv + bx]
    mov [di], ax
    mov ax, [cs_syv + bx]
    mov [di+2], ax
    add di, 4
    loop .cp
.area:
    mov al, [cs_dupface]
    or al, [cs_duparea]
    jz .out                         ; ...and cs_dupgath the GATHER alone
    cmp word [cs_fn], 4
    jne .out
    mov ax, [cs_dbg_pv + 8]         ; (x2 - x0)(y3 - y1)...
    sub ax, [cs_dbg_pv]
    mov bx, [cs_dbg_pv + 14]
    sub bx, [cs_dbg_pv + 6]
    imul bx
    mov cx, ax
    mov si, dx
    mov ax, [cs_dbg_pv + 10]        ; ...minus (y2 - y0)(x3 - x1)
    sub ax, [cs_dbg_pv + 2]
    mov bx, [cs_dbg_pv + 12]
    sub bx, [cs_dbg_pv + 4]
    imul bx
    sub cx, ax
    sbb si, dx
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; cs_probe_sh - PROBE ONLY: a SHALLOW segment about to be walked. Says whether
;               it takes the run slice (six or more pixels a row) or the
;               per-pixel arm, and for the per-pixel arm accumulates its
;               pixels AND the DISTINCT BYTES they land in - which is
;                   1 + |dy| + ((x0 & 7) + |dx|) / 8
;               because DI moves on a y-step and on a mask wrap and nowhere
;               else, and never revisits a byte. **The difference between the
;               two is what a byte ACCUMULATOR could merge**, and it is the
;               only arm where anything can: a steep or vertical line puts
;               every pixel in a different row, 80 bytes apart.
;               in: AX = x0, CX = |dx|+1, BP = 2|dy|.  Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_sh:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov dx, bp                      ; the slice's own test: CX > 6|dy|
    shl dx, 1
    add dx, bp
    cmp cx, dx
    ja .slice
    inc word [cs_dbg_wsh]
    add [cs_dbg_wshpx], cx
    mov si, bp
    shr si, 1                       ; SI = |dy|
    inc si                          ; ...+ 1 for the first byte
    mov dx, ax
    and dx, 7                       ; the start's bit within its byte
    mov di, cx
    dec di                          ; DI = |dx|
    add dx, di
    shr dx, 1
    shr dx, 1
    shr dx, 1                       ; DX = the mask wraps
    add si, dx
    add [cs_dbg_wshby], si
    jmp short .out
.slice:
    ; ...AND THE SLICE'S OWN ROWS AND PIXELS, because the run body is the
    ; expensive half and nothing counted it. A sliced row is one pass of
    ; ~130 bytes whatever it lays down (SPEC.md 85.3.6), so a segment's
    ; cost is its ROWS and not its pixels - and a bank is what turns a
    ; FLAT line (one row, one run) into forty of them.
    inc word [cs_dbg_wsl]
    add [cs_dbg_wslpx], cx          ; |dx|+1 pixels...
    mov si, bp
    shr si, 1
    inc si                          ; ...laid in |dy|+1 runs
    add [cs_dbg_wslrow], si
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; cs_probe_pv - PROBE ONLY: this object's vertices, split by whether its
;               model draws EDGES. cs_projall runs BEFORE cs_faces, so every
;               vertex is projected before cs_axcull (88.5.12) has culled a
;               single face - and the obvious thought is to cull first and
;               project only what survives. It is blocked for any model with
;               edges: cs_edges walks EVERY entry of CSM_EDGES and cs_edge1
;               reads both endpoints' cs_sxv, so an edge-bearing model needs
;               all of them however many faces are culled. This counts which
;               side of that line the frame's vertices fall on.
;               Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_pv:
    pushf
    push ax
    push bx
    push si
    mov si, [cs_mdl]
    mov byte [cs_dbg_pvne], 1       ; ...does this model draw edges?
    cmp byte [si + CSM_NE], 0
    jne .hase
    mov byte [cs_dbg_pvne], 0
.hase:
    push cx                         ; and the USED map starts empty
    push di
    push es
    push ds
    pop es
    mov di, cs_dbg_used
    mov cx, CS_MAXV
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    mov ax, [cs_nv]                 ; ...which is the vertex COUNT: both
                                    ; cs_stackverts (levels x 4) and
                                    ; cs_flatverts (CSM_NV) store it that way,
                                    ; and the first draft of this probe halved
                                    ; it as if it were an index stride and
                                    ; reported 31.8 vertices a frame where the
                                    ; projection's own bracket counts 53 in
                                    ; front of the near plane alone
    inc word [cs_dbg_pvobj]
    cmp byte [si + CSM_NE], 0
    je .free
    add [cs_dbg_pvedge], ax
    inc word [cs_dbg_pvobje]
    jmp short .out
.free:
    add [cs_dbg_pvfree], ax
.out:
    mov [cs_dbg_pvnv], ax
    pop si
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; cs_probe_used - PROBE ONLY: this face PASSED cs_axcull, so its vertices are
;                 wanted. Marks them in cs_dbg_used. The mark is set by the
;                 AXIS cull and not by the winding on purpose: axcull is the
;                 only one of the two that could run before cs_projall, the
;                 winding needing the projected points it would be gating
;                 (88.5.10). in: [cs_fidx] -> the indices, [cs_fn] = how many.
;                 Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_used:
    pushf
    push ax
    push bx
    push cx
    push si
    mov si, [cs_fidx]
    mov cx, [cs_fn]
    jcxz .out
    cld
.m:
    lodsb
    mov bl, al
    xor bh, bh
    mov byte [cs_dbg_used + bx], 1
    loop .m
.out:
    pop si
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; cs_probe_objend - PROBE ONLY: the object's faces are done. How many of its
;                   vertices did NO surviving face want? Those are the ones a
;                   cull-before-project could have skipped - and only in a
;                   model with no edges, cs_edges needing every vertex of one
;                   that has them (88.5.13.4). Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_objend:
    pushf
    push ax
    push bx
    push cx
    cmp byte [cs_dbg_pvne], 0       ; an edge-drawing model: nothing to skip
    jne .out
    mov cx, [cs_dbg_pvnv]
    jcxz .out
    add [cs_dbg_vcand], cx
    xor bx, bx
.v:
    cmp byte [cs_dbg_used + bx], 0
    jne .n
    inc word [cs_dbg_vunused]
.n:
    inc bx
    loop .v
.out:
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; cs_probe_mstab - PROBE ONLY: is cs_m the SAME MATRIX as last frame?
;                  SPEC.md 88.5.13.2's whole question. A per-frame
;                  partial-product table pays from the SECOND frame of a
;                  stable matrix and loses on the first, so what decides it
;                  is not the table's cost but how often the answer is
;                  "unchanged". The matrix is a function of (roll, pitch,
;                  heading) alone, and this counts how often all three hold.
;                  Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_mstab:
    pushf
    push ax
    push cx
    push si
    push di
    push ds
    push es
    inc word [cs_dbg_mfr]
    push ds
    pop es
    mov si, cs_m
    mov di, cs_dbg_mprev
    mov cx, 9
    cld
    repe cmpsw
    jne .moved
    inc word [cs_dbg_mstab]
.moved:
    mov si, cs_m
    mov di, cs_dbg_mprev
    mov cx, 9
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; cs_probe_fv - PROBE ONLY: a FLAT model's vertex about to be transformed.
;               cs_flatverts spends SIX multiplies a vertex - x M0 into three
;               components and z M2 into three - where cs_stackverts' column
;               trick spends nine a LEVEL and gets four vertices out of it by
;               adding (88.5.6.2). The cheap version of that here is reuse:
;               if this vertex's x equals the last one's, its three products
;               are the last one's too. This counts how often that is true,
;               because a compare costs 4 cycles and three imuls cost ~420.
;               in: SI = the vertex pair. Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_fv:
    pushf
    push ax
    inc word [cs_dbg_fvn]
    cmp byte [cs_dbg_fvfirst], 0
    je .cmp
    mov byte [cs_dbg_fvfirst], 0
    jmp short .save
.cmp:
    mov ax, [si]
    cmp ax, [cs_dbg_fvpx]
    jne .zx
    inc word [cs_dbg_fvx]
.zx:
    mov ax, [si + 2]
    cmp ax, [cs_dbg_fvpz]
    jne .save
    inc word [cs_dbg_fvz]
.save:
    mov ax, [si]
    mov [cs_dbg_fvpx], ax
    mov ax, [si + 2]
    mov [cs_dbg_fvpz], ax
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; cs_probe_prow - PROBE ONLY: ONE POLYGON ROW about to be laid, and how wide.
;                 The line's slice was priced per RUN and not per pixel
;                 (SPEC.md 88.4.6.3), and the fill has never been asked the
;                 same question: cs_polyrows_herc's row is ~90 bytes of
;                 preamble and two masked ends around a `rep stosw`, so what
;                 decides whether that shape is right is the BYTES a row
;                 spans, not its pixels. A row of one or two bytes is all
;                 preamble.
;                 in: AX = x1, CX = x2, both clamped, x1 <= x2.
;                 Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_prow:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    inc word [cs_dbg_prow]
    mov bx, cx
    sub bx, ax
    inc bx                          ; BX = pixels
    add [cs_dbg_ppx], bx
    mov si, cx
    shr si, 1
    shr si, 1
    shr si, 1                       ; SI = the last byte
    mov bx, ax
    shr bx, 1
    shr bx, 1
    shr bx, 1                       ; BX = the first
    sub si, bx
    inc si                          ; SI = the bytes it spans
    add [cs_dbg_pby], si
    cmp si, 8                       ; ...and the shape, because a mean hides
    ja .out                         ; a bimodal one: a fill of many thin rows
    inc word [cs_dbg_pby8]          ; and a few wide ones reads the same mean
    cmp si, 4                       ; as one of uniform middling rows, and
    ja .out                         ; wants a completely different answer
    inc word [cs_dbg_pby4]
    cmp si, 2
    ja .out
    inc word [cs_dbg_pby2]
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; cs_probe_edge - PROBE ONLY: count the traces cs_poly makes and how many of
;                 them another face of the SAME object has already made, and
;                 carry the two A/Bs that price the dedup test and its copy.
;                 Called with the edge's ends in AX/BX and CX/DX, immediately
;                 before `call cs_edge`. Preserves everything.
; -----------------------------------------------------------------------------
cs_probe_edge:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    inc word [cs_dbg_etr]
    mov si, bx                      ; the rows this trace covers, view-clipped
    mov di, dx
    cmp si, di
    jle .ord
    xchg si, di
.ord:
    or di, di
    js .norow
    cmp si, [cs_wh]
    jge .norow
    or si, si
    jns .r1
    xor si, si
.r1:
    cmp di, [cs_wh]
    jl .r2
    mov di, [cs_wh]
    dec di
.r2:
    mov [cs_dbg_y0], si
    sub di, si
    inc di
    add [cs_dbg_erow], di
    jmp short .idx
.norow:
    xor di, di
.idx:
    cmp byte [cs_fcut], 0
    je .look
    inc word [cs_dbg_ecut]          ; a cut face has no model indices
    jmp short .out
.look:
    ; --- A/B 2: with [cs_nomark] the index lookup and cs_edgemark are
    ;     skipped, so the difference is what the DEDUP TEST costs - and a
    ;     production version pays it on EVERY edge to save on one in seven
    cmp byte [cs_nomark], 0
    jne .out
    mov bp, [cs_pn]                 ; the edge's index: i = pn - pei
    mov si, bp
    sub si, [cs_pei]
    mov bx, si
    inc bx
    cmp bx, bp
    jb .j
    xor bx, bx
.j:
    mov bp, [cs_fidx]
    add si, bp
    mov al, [si]
    mov si, bp
    add si, bx
    mov ah, [si]
    push di
    call cs_edgemark
    pop di
    jnc .out
    inc word [cs_dbg_edup]
    add [cs_dbg_edrow], di
    ; --- A/B 3: with [cs_cpy] a duplicate ALSO pays the copy that would stand
    ;     in for the trace it skips. Done INTO SCRATCH with the real trace
    ;     still running after it, so the picture is untouched
    cmp byte [cs_cpy], 0
    je .out
    or di, di
    jz .out
    push es
    push ds
    pop es
    mov cx, di
    cmp cx, CS_DBGSCR               ; the scratch is 112 rows (see its note)
    jbe .cx
    mov cx, CS_DBGSCR
.cx:
    mov si, [cs_dbg_y0]
    shl si, 1
    mov di, si
    add si, cs_xr
    add di, cs_dbg_scr
    cld
    rep movsw
    pop es
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret
%endif

; cs_wireclr - the object's edge marks, cleared. Preserves everything
cs_wireclr:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, cs_eseen
    mov cx, CS_ESEEN / 2
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_fillon - may a face in ink AL be filled? (SPEC.md 88.13.3)
; out: CF = 1 the player turned this fill off. Preserves every register
;
; TERRAIN is the water and the hills - the world's own surface - and the
; ground under the horizon, which is cs_skyground's business rather than a
; face's. Everything else is built: walls, roofs, the runway. A fill turned
; off leaves the face's OUTLINE (cs_wire), which is the wireframe world the
; option exists for, and not an empty sky where the thing used to be.
; -----------------------------------------------------------------------------
cs_fillon:
    push ax
    mov ah, CSFL_TERRAIN
    cmp al, CSI_RIVER
    je .test
    cmp al, CSI_HILL
    je .test
    mov ah, CSFL_BLDG
.test:
    test [cs_setfill], ah
    jz .off
    pop ax
    clc
    ret
.off:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; cs_nearclip - Sutherland-Hodgman against z = [cs_near], in camera space, then
;               project: the face at [cs_fidx] (cs_fn ends) into cs_pv/cs_pn
; -----------------------------------------------------------------------------
cs_fclip:
    mov word [cs_ncl], 0
    mov di, cs_clipx
    mov cx, [cs_fn]
    mov si, [cs_fidx]
.e:
    push cx
    push si
    mov al, [si]                    ; a
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov [cs_na], bx
    inc si
    dec cx
    jnz .nb
    mov si, [cs_fidx]               ; ...the last edge closes on the first
.nb:
    mov al, [si]                    ; b
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov [cs_nb], bx
    mov bx, [cs_na]
    cmp word [cs_fv + bx], 0
    je .aback
    ; a is in front: output it
    mov ax, [cs_cxv + bx]
    mov [di], ax
    mov ax, [cs_cyv + bx]
    mov [di+2], ax
    mov ax, [cs_czv + bx]
    mov [di+4], ax
    add di, 6
    inc word [cs_ncl]
    mov bx, [cs_nb]
    cmp word [cs_fv + bx], 0
    jne .next                       ; both in front: no crossing
    call cs_ncross
    jmp short .next
.aback:
    mov bx, [cs_nb]
    cmp word [cs_fv + bx], 0
    je .next                        ; both behind
    call cs_ncross
.next:
    pop si
    inc si
    pop cx
    loop .e
    ; --- then the four sides, each a Sutherland-Hodgman pass between the two
    ;     buffers (88.5.7) -----------------------------------------------------
    mov word [cs_csrc], cs_clipx
    mov word [cs_cdst], cs_clipy
    cmp byte [cs_noside], 0         ; the A/B: near-clipped only, the sides
    jne .proj                       ; left to the projection's clamp
    mov word [cs_cpln], 0
.pass:
    call cs_sidepass
    jnc .same                       ; nothing clipped: the same buffer
    mov ax, [cs_csrc]               ; swap the buffers
    xchg ax, [cs_cdst]
    mov [cs_csrc], ax
.same:
    add word [cs_cpln], 2
    cmp word [cs_cpln], 8
    jb .pass
    ; --- project the result ------------------------------------------------
.proj:
    mov cx, [cs_ncl]
    mov [cs_pn], cx
    jcxz .out
    mov si, [cs_csrc]
    mov di, cs_pv
.p:
    push cx
    push si
    push di
    mov ax, [si]
    mov bx, [si+2]
    mov cx, [si+4]
    call [cs_projp]
    pop di
    mov [di], ax
    mov [di+2], bx
    add di, 4
    pop si
    add si, 6
    pop cx
    loop .p
.out:
    ret

; cs_ncross - the point where edge [cs_na]->[cs_nb] crosses z = [cs_near],
; appended at DI (three words, DI advanced). Clobbers AX, BX, CX, DX.
cs_ncross:
    push si
    mov bx, [cs_na]
    mov si, [cs_nb]
    mov cx, [cs_czv + si]
    sub cx, [cs_czv + bx]           ; den = zb - za, never 0 (opposite sides)
    mov [cs_nden], cx
    mov ax, [cs_near]
    sub ax, [cs_czv + bx]           ; num = NEAR - za, |num| <= |den|
    mov [cs_nnum], ax
    mov ax, [cs_cxv + si]
    sub ax, [cs_cxv + bx]
    imul word [cs_nnum]
    idiv word [cs_nden]
    add ax, [cs_cxv + bx]
    mov [di], ax
    mov ax, [cs_cyv + si]
    sub ax, [cs_cyv + bx]
    imul word [cs_nnum]
    idiv word [cs_nden]
    add ax, [cs_cyv + bx]
    mov [di+2], ax
    mov ax, [cs_near]
    mov [di+4], ax
    add di, 6
    inc word [cs_ncl]
    pop si
    ret

; -----------------------------------------------------------------------------
; The frustum's sides (SPEC.md 88.5.7): the planes |x| = 4z and |y| = 4z in
; camera space, well inside where the projection would clamp (+-4000 is
; |x| = 9z), because a crossing is only exact to a distance UNIT - 16 of x
; or 4 of z - and at the near plane (z = 4) a unit of x is 444 projected
; pixels. 4z leaves the whole 5z between the plane and the clamp for that:
; a point on one projects to +-1778, and one 16 past it at z = 4 to 3554,
; still under the clamp, where the 8z the clipper first shipped with put
; the same point at 7104 and a road was seen bending at (4275, 2112).
; cs_cplanes: the component (x, y) and the sign each plane takes
; -----------------------------------------------------------------------------
; cs_cdist - AX = a point's distance from plane [cs_cpln], as z/4 - s c/16
;            (the quarter keeps the difference of two under a word), through
;            the plane's own routine (cs_cdtab, set by cs_cplane). SI -> the
;            point (x, y, z). Preserves SI, DI, CX, DX; clobbers BX
cs_cdist:
    jmp [cs_cdistp]
cs_cd_xp:
    mov ax, [si]
    jmp short cs_cd_p
cs_cd_xm:
    mov ax, [si]
    jmp short cs_cd_m
cs_cd_yp:
    mov ax, [si + 2]
    jmp short cs_cd_p
cs_cd_ym:
    mov ax, [si + 2]
cs_cd_m:
    neg ax
cs_cd_p:
    sar ax, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov bx, [si + 4]
    sar bx, 1
    sar bx, 1
    sub bx, ax
    mov ax, bx
    ret
cs_cdtab: dw cs_cd_xp, cs_cd_xm, cs_cd_yp, cs_cd_ym

; cs_cplane - the plane [cs_cpln] (0, 2, 4, 6) selects its distance routine
cs_cplane:
    mov bx, [cs_cpln]
    mov ax, [cs_cdtab + bx]
    mov [cs_cdistp], ax
    ret

; cs_cxing - the point where A (SI ->) to B (DI ->) crosses plane [cs_cpln],
;            AX and BX its two distances of opposite sign, into cs_xpt:
;            t = da / (da - db), P = A + t (B - A). Preserves SI, DI
cs_cxing:
    push si
    push di
    mov cx, ax
    sub cx, bx                      ; CX = da - db, never 0
    mov bx, 32767
    imul bx                         ; DX:AX = da x 32767
    idiv cx                         ; AX = t, Q15, in 0..1
    mov [cs_ct], ax
    mov ax, [di]
    sub ax, [si]
    mov bx, [cs_ct]
    MUL14
    add ax, [si]
    mov [cs_xpt], ax
    mov ax, [di + 2]
    sub ax, [si + 2]
    mov bx, [cs_ct]
    MUL14
    add ax, [si + 2]
    mov [cs_xpt + 2], ax
    mov ax, [di + 4]
    sub ax, [si + 4]
    mov bx, [cs_ct]
    MUL14
    add ax, [si + 4]
    mov [cs_xpt + 4], ax
    pop di
    pop si
    ret

; cs_sidepass - one Sutherland-Hodgman pass of the [cs_ncl] points at
;               [cs_csrc] against plane [cs_cpln], into [cs_cdst]; [cs_ncl]
;               becomes the count out. Clobbers everything
cs_sidepass:
    mov cx, [cs_ncl]
    or cx, cx
    jz .none0
    call cs_cplane
    mov si, [cs_csrc]               ; --- every point's distance, once, and
    mov di, cs_cdists               ;     whether any is past the plane -----
    xor dx, dx
.d:
    call cs_cdist
    mov [di], ax
    or ax, ax
    jns .d1
    inc dx
.d1:
    add di, 2
    add si, 6
    loop .d
    or dx, dx
    jnz .clip                       ; all inside: the pass changes nothing
.none0:
    jmp .none
.clip:
    mov cx, [cs_ncl]
    mov word [cs_cn2], 0
    mov si, [cs_csrc]
    mov ax, [cs_cdst]
    mov [cs_cout], ax
    mov bp, cs_cdists               ; BP -> A's distance (a plain register:
.e:                                 ; SS != DS, so never [bp + n])
    push cx
    mov di, si                      ; DI -> B: the next point, or the first
    add di, 6                       ; after the last
    mov bx, bp
    add bx, 2                       ; BX -> B's distance...
    cmp cx, 1
    jne .b
    mov di, [cs_csrc]
    mov bx, cs_cdists               ; ...the first's, after the last
.b:
    mov ax, [bx]
    mov bx, ax                      ; BX = B's distance
    xchg bx, bp
    mov ax, [bx]                    ; AX = A's
    xchg bx, bp
    inc bp
    inc bp
    or ax, ax
    js .aout
    or bx, bx
    jns .ain                        ; A inside and B too: A alone
    call cs_cxing                   ; A inside, B past the plane: the
    call .emita                     ; crossing off the two distances FIRST -
    call .emitx                     ; .emit leaves A's z in AX, and the first
    jmp short .next                 ; build computed it after, so every such
                                    ; crossing sat at t = z/(z - db), which is
                                    ; almost ON the outside point (88.5.7.1)
.ain:
    call .emita
    jmp short .next
.aout:
    or bx, bx
    js .next                        ; both outside
    call cs_cxing                   ; A outside, B in: the crossing
    call .emitx
.next:
    pop cx
    add si, 6
    loop .e
    mov ax, [cs_cn2]
    mov [cs_ncl], ax
    stc                             ; CF = the buffers changed hands
    ret
.none:
    clc
    ret
.emita:
    push bx
    push si
    jmp short .emit
.emitx:
    push bx
    push si
    mov si, cs_xpt
.emit:
    cmp word [cs_cn2], CS_MAXPV
    jae .full
    push di
    mov di, [cs_cout]
    mov ax, [si]
    mov [di], ax
    mov ax, [si + 2]
    mov [di + 2], ax
    mov ax, [si + 4]
    mov [di + 4], ax
    add word [cs_cout], 6
    inc word [cs_cn2]
    pop di
.full:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; cs_edges - every edge of [cs_mdl], in the model's line ink
; -----------------------------------------------------------------------------
cs_edges:
    mov si, [cs_mdl]
    mov cl, [si + CSM_NE]
    xor ch, ch
    jcxz .out
    mov al, [si + CSM_EINK]
    call cs_setink
    mov si, [si + CSM_EDGES]
    cld
.e:
    lodsw                           ; AL/AH = the edge's two vertex indices
    push cx
    push si
    call cs_edge1
    pop si
    pop cx
    loop .e
.out:
    ret

; cs_edge1 - the edge between vertices AL and AH, near-clipped where it has
;            to be, in the current ink. Clobbers everything
cs_edge1:
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov dl, ah
    xor dh, dh
    shl dx, 1
    mov di, dx
    cmp byte [cs_pwhole], 0
    jne .both                       ; a whole object: every end as projected
    mov cx, [cs_fv + bx]
    add cx, [cs_fv + di]
    jcxz .skip                      ; both behind the near plane
    cmp cx, 2
    jne .gen
    cmp word [cs_fv + bx], 1
    jne .gen
.both:                              ; both in front and inside: as projected
    mov ax, [cs_sxv + bx]
    mov bx, [cs_syv + bx]
    mov cx, [cs_sxv + di]
    mov dx, [cs_syv + di]
.draw:
    call cs_seg
.skip:
    ret
    ; --- one end behind the near plane or past a side (88.5.7): the two
    ;     ends in camera space, the near crossing for a behind one, then the
    ;     four sides, an end past one brought to it along the edge ----------
.gen:
    mov [cs_na], bx
    mov [cs_nb], dx
    mov si, bx
    mov di, cs_ea
    call cs_ecopy                   ; A -> cs_ea
    mov si, [cs_nb]
    mov di, cs_eb
    call cs_ecopy                   ; B -> cs_eb
    mov bx, [cs_na]
    cmp word [cs_fv + bx], 0
    jne .bnear
    mov ax, [cs_nb]                 ; A behind: the crossing from B replaces it
    mov [cs_na], ax
    mov [cs_nb], bx
    mov di, cs_ea
    call cs_ncross
    jmp short .sides
.bnear:
    mov bx, [cs_nb]
    cmp word [cs_fv + bx], 0
    jne .sides
    mov di, cs_eb                   ; B behind: the crossing from A replaces it
    call cs_ncross
.sides:
    cmp byte [cs_noside], 0         ; the A/B, as in cs_fclip: an end past a
    jne .eproj                      ; side is projected clamped and wanders
    mov word [cs_cpln], 0
.side:
    call cs_cplane
    mov si, cs_ea
    call cs_cdist
    mov [cs_cda], ax
    mov si, cs_eb
    call cs_cdist
    mov bx, ax                      ; BX = B's distance
    mov ax, [cs_cda]                ; AX = A's
    mov si, cs_ea
    mov di, cs_eb
    or ax, ax
    jns .ain
    or bx, bx
    js .skip                        ; both past this side: nothing to draw
    call cs_cxing                   ; A past it: to the crossing
    mov si, cs_xpt
    mov di, cs_ea
    call cs_ecopy
    jmp short .nside
.ain:
    or bx, bx
    jns .nside
    call cs_cxing                   ; B past it: to the crossing
    mov si, cs_xpt
    mov di, cs_eb
    call cs_ecopy
.nside:
    add word [cs_cpln], 2
    cmp word [cs_cpln], 8
    jb .side
.eproj:
    mov ax, [cs_ea]                 ; --- both ends inside: project and draw --
    mov bx, [cs_ea + 2]
    mov cx, [cs_ea + 4]
    call [cs_projp]
    mov [cs_ex1], ax
    mov [cs_ey1], bx
    mov ax, [cs_eb]
    mov bx, [cs_eb + 2]
    mov cx, [cs_eb + 4]
    call [cs_projp]
    mov cx, ax
    mov dx, bx
    mov ax, [cs_ex1]
    mov bx, [cs_ey1]
    jmp .draw

; cs_ecopy - the camera-space point at SI (a triple, or a vertex's byte
;            index into cs_cxv/cyv/czv when SI < 64) to the triple at DI
cs_ecopy:
    cmp si, 64
    jae .triple
    mov ax, [cs_cxv + si]
    mov [di], ax
    mov ax, [cs_cyv + si]
    mov [di + 2], ax
    mov ax, [cs_czv + si]
    mov [di + 4], ax
    ret
.triple:
    mov ax, [si]
    mov [di], ax
    mov ax, [si + 2]
    mov [di + 2], ax
    mov ax, [si + 4]
    mov [di + 4], ax
    ret

; -----------------------------------------------------------------------------
; cs_rwline - the runway's centreline (SPEC.md 88.6.2): DASHED from the stripe
;             the aeroplane is on, RW_NDASH stripes of RW_DASHM metres with as
;             much gap, then solid to the far end - so a take-off roll can be
;             seen moving. Every endpoint is a point along the transformed
;             centreline (vertices 5 -> 4, at the object's scale), put into
;             scratch vertices 6 and 7 and drawn through cs_edge1, which
;             near-clips it. Away from the runway - above RW_DASHH or beyond
;             RW_DASHW of its axis - the line is solid from end to end and
;             costs one segment, as it did before. PAST THE FAR END but
;             inside those two, the stripes go at THAT threshold instead
;             (SPEC.md 88.6.2.3): the mirror of the near end's `js .zero`.
;             It all runs in FACING SPACE (88.6.2.4) - u = 0 at the threshold
;             behind the aeroplane - so the stripes are the ones ahead of it
;             whichever way round it landed.
;             Runs after cs_edges, in its ink.
; -----------------------------------------------------------------------------
cs_rwline:
    call cs_runway_xy               ; [cs_along], [cs_across]
    ; --- WHICH THRESHOLD IS AHEAD (SPEC.md 88.6.2.4). Everything below works
    ;     in FACING SPACE: u = 0 at the threshold behind the aeroplane and
    ;     32767 at the one it is pointed at, so the stripes are always the
    ;     ones it is about to drive over and the solid part is always the
    ;     runway behind it. Land from the far side and roll back, and the old
    ;     code drew the whole line BEHIND the aeroplane - measured at 300 m
    ;     inside the strip, 11 pixels against 571 the other way round.
    ;     cos(facing - runway) is the whole of the test, and both sines and
    ;     cosines are already computed for the frame; cs_rwsegu mirrors each
    ;     segment back into the model's own u at the end. THE FACING AND NOT
    ;     THE HEADING (88.7.8.2): past the vertical they are opposite, and
    ;     this read the heading until a loop showed it up.
    mov ax, [cs_fsinh]
    imul word [cs_rwsin]
    mov cx, dx
    mov ax, [cs_fcosh]
    imul word [cs_rwcos]
    add dx, cx
    mov byte [cs_rwrev], 0
    jns .ahead
    mov byte [cs_rwrev], 1
.ahead:
    mov si, [cs_airport]
    mov ax, [cs_py+1]
    sub ax, [si + CSA_ELEV]
    cmp ax, RW_DASHH
    jg .solid
    mov ax, [cs_across]
    or ax, ax
    jns .w
    neg ax
.w:
    cmp ax, RW_DASHW
    jg .solid
    mov ax, [cs_along]
    cmp byte [cs_rwrev], 0          ; ...from the end BEHIND it (88.6.2.4)
    je .fwd
    neg ax
.fwd:
    add ax, [si + CSA_HLEN]         ; AX = metres from the end behind
    js .zero
    ; --- PAST THE FAR END, BEFORE THE DIVIDE (SPEC.md 88.6.2.2). The test
    ;     for it used to be on the QUOTIENT, and the quotient is
    ;     metres x 16384 / hlen: it leaves a word at 4 hlen - ONE whole
    ;     runway length past the far end - and an 8086 answers a divide
    ;     that does not fit with INT 0, having never produced the operand
    ;     the compare below wanted. The window is a CIRCUIT: this code is
    ;     only reached below RW_DASHH and within RW_DASHW of the axis, so
    ;     overfly the strip, turn back, and come down the extended
    ;     centreline from more than a runway length out. The test moves in
    ;     front of the multiply, where it costs three instructions and
    ;     cannot fault.
    mov bx, [si + CSA_HLEN]
    shl bx, 1                       ; the runway's whole length
    cmp ax, bx
    jae .far
    mov dx, 16384
    mul dx
    div word [si + CSA_HLEN]        ; AX = the aeroplane's u, Q15 of 2 hlen
    cmp ax, [cs_rwfar]
    jae .far                        ; ...and the last stripe's worth of it
    jmp short .u
.zero:
    xor ax, ax
.u:
    xor dx, dx                      ; the stripe it is on: u rounded down
    mov cx, [cs_rwdu]               ; to a pitch
    shl cx, 1
    div cx
    mul cx
    mov cx, RW_NDASH
.dash:
    push cx
    push ax
    mov cx, ax
    add cx, [cs_rwdu]               ; CX = the stripe's far end
    cmp cx, 32767
    jae .last                       ; past the runway's: nothing more
    call cs_rwsegu                  ; the stripe (AX..CX)
    pop ax
    add ax, [cs_rwdu]
    add ax, [cs_rwdu]               ; the next stripe
    pop cx
    loop .dash
    mov cx, 32767                   ; ...then solid to the far end
    cmp ax, cx
    jae .out
    jmp short cs_rwsegu
.last:
    pop ax
    pop cx
.out:
    ret
    ; --- PAST THE FAR END, LOW AND ON THE AXIS: DASHED AT THAT THRESHOLD
    ;     (SPEC.md 88.6.2.3). The near end has been clamped since the first
    ;     build - `js .zero` puts the stripes at the threshold you are
    ;     aiming at - and the far end had no such case, so an approach to it
    ;     drew ONE SOLID LINE from a runway length out to the flare. The
    ;     field reported exactly that, and reported the state it needs:
    ;     leave and come back, because at reset you are stood at the NEAR
    ;     end and the only way to meet the far one is to fly a circuit.
    ;     Solid from the near end up to [cs_rwfar], then the same four
    ;     stripes ending ON the far threshold - five segments either way,
    ;     so the two approaches cost the same
.far:
    xor ax, ax
    mov cx, [cs_rwfar]
    call cs_rwsegu
    mov ax, [cs_rwfar]
    mov cx, RW_NDASH
    jmp short .dash
.solid:
    xor ax, ax
    mov cx, 32767
    ; falls into cs_rwsegu

; cs_rwsegu - the centreline from u = AX to u = CX (Q15): scratch vertices
;             6 and 7, then the edge. Clobbers everything
cs_rwsegu:
    cmp byte [cs_rwrev], 0          ; facing space back into the model's own
    je .fwd                         ; u: [a, b] -> [32767 - b, 32767 - a]
    xchg ax, cx
    neg ax
    add ax, 32767
    neg cx
    add cx, 32767
.fwd:
    push cx
    mov di, 12                      ; vertex 6
    call cs_rwpt
    pop ax
    mov di, 14                      ; vertex 7
    call cs_rwpt
    mov ax, 0x0706                  ; AL = 6, AH = 7
    jmp cs_edge1

; cs_rwpt - the point at u = AX (Q15) along the centreline, vertex 5 -> 4,
;           into vertex DI (its byte index): camera space, the near flag
;           and, in front, the projection. Clobbers everything but DI
cs_rwpt:
    mov [cs_rwu], ax
    mov ax, [cs_cxv + 8]
    sub ax, [cs_cxv + 10]
    mov bx, [cs_rwu]
    MUL14
    add ax, [cs_cxv + 10]
    mov [cs_cxv + di], ax
    mov ax, [cs_cyv + 8]
    sub ax, [cs_cyv + 10]
    mov bx, [cs_rwu]
    MUL14
    add ax, [cs_cyv + 10]
    mov [cs_cyv + di], ax
    mov ax, [cs_czv + 8]
    sub ax, [cs_czv + 10]
    mov bx, [cs_rwu]
    MUL14
    add ax, [cs_czv + 10]
    mov [cs_czv + di], ax
    mov word [cs_fv + di], 0
    cmp ax, [cs_near]
    jl .out                         ; behind: cs_edge1 clips to it
    mov word [cs_fv + di], 1
    mov cx, ax
    mov ax, [cs_cxv + di]
    mov bx, [cs_cyv + di]
    push di
    call [cs_projp]
    pop di
    mov [cs_sxv + di], ax
    mov [cs_syv + di], bx
.out:
    ret

cs_sintab:
%include "cssin.inc"
