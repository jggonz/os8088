; =============================================================================
; svshape.inc - MODE 3: 3D figures that BLOOM in and then out (SPEC.md 79.5)
;
; A figure is a HUB, two rings of points around it in three dimensions, and
; six waves of edges tying them together - spokes out to the inner ring, the
; inner ring closed, two sets of struts out to the outer ring, and the outer
; ring closed and then chorded. Six waves of K points is up to 48 edges, and
; the chords are what make it read as a figure rather than as a wire cage: at
; K = 5 the last wave IS a pentagram.
;
; IT IS DRAWN BY GROWING EVERY EDGE AT ONCE, which is the whole difference
; from the first draft. That one laid down one WHOLE edge a frame with
; `gfx_line` - correct, cheap, and it read as a machine ticking round a
; perimeter. Cyclone 88's warp (SPEC.md 67.4) does the other thing: it holds a
; SPEC.md 5.6.7 resumable walk per spoke and advances all of them a few pixels
; a frame in one `OSAPI_GFX_LSTEPV`, so the web arrives everywhere at once.
; That is a bloom, and it is what this mode does now.
;
; The erase is the same walks replayed in the ground pen, which is 5.6.7's own
; reason for existing: two inits from the same endpoints in the same order
; visit the same pixels, so a figure taken up leaves nothing behind - and,
; because a walk stops and resumes anywhere, the taking-up is a bloom in
; reverse rather than a lump.
;
; WHAT IT COSTS, and the numbers are SPEC.md 5.6.8's rather than 5.7's, which
; is the trap this mode was sized out of: `gfx_lstep` is NOT a rect primitive,
; so the ~756us floor does not apply to it. Its three terms are a 128.7us
; arrival, ~480us of BLOCK SETUP per descriptor, and ~175us a pixel.
;
;   ...so the block setup dominates and the pixels are nearly free.
;
; That single fact sizes the mode. SV_HACT caps the descriptors a figure hands
; over in a frame, SV_HPX is how far each goes, and the frame is
;
;   SV_HNF * (128.7 + SV_HACT * (480 + SV_HPX * 175)) us
;
; = 2 * (128.7 + 10 * 1180) = 23.9ms of a 55ms tick, predicted.
;
; MEASURED, on MartyPC's cycle-accurate 4.77MHz 8088, breakpointing this
; module's entry and its `ret` over 60 frames: **median 84,748 cycles = 17.8ms,
; worst 155,517 = 32.6ms**. The prediction was high because it assumed both
; figures busy every frame and they are not - the worst case, which is both of
; them growing with a full window, is the 32.6. Entry to entry measures
; 262,259 cycles, which is 55.0ms: one tick exactly, so nothing is being
; dropped and the bloom runs at the full 18.2 frames a second.
;
; Raising SV_HPX is nearly free and raising SV_HACT is not, which is the
; opposite of the intuition and is why the terms are written out here.
;
; THE WAVE IS A WINDOW AND NOT A PHASE. The edges are in bloom order, and the
; SV_HACT descriptors are the first live ones from the front - so the spokes
; go out together, and the ring behind them starts before the last of them has
; landed. A strict wave-at-a-time would stall on its slowest edge every time.
;
; TWO FIGURES CAN OVERLAP, and where they do the first one's erase nicks the
; second. It is the cube's nick (SPEC.md 78.5) with the same answer: a
; handful of pixels, gone when the second figure is taken up in its turn, and
; the alternative is keeping two random boxes apart, which is more code than
; the defect is worth.
; =============================================================================

SV_HNF      equ 2               ; figures on the glass at once
SV_HKMIN    equ 5               ; points in a ring
SV_HKMAX    equ 8
SV_HNL      equ 2               ; rings
SV_HNV      equ 3 + SV_HNL * SV_HKMAX      ; hub, two POLES, and the two rings
SV_HNW      equ 6               ; the most waves any shape has
SV_HNE      equ SV_HNW * SV_HKMAX

SV_HNK      equ 4               ; SHAPES, and each is a row of sv_hkd
SV_HK_FLOW  equ 0               ; hub, two rings, chords - the first one
SV_HK_DIA   equ 1               ; a diamond: every runner leaves the bottom tip
SV_HK_CUBE  equ 2               ; a cube: four corners start together
SV_HK_STAR  equ 3               ; tips inward, round the core, back out
SV_HKD_SZ   equ 12              ; bytes a row

SV_HV_HUB   equ 0               ; what a wave descriptor's first byte selects
SV_HV_BOT   equ 1
SV_HV_TOP   equ 2
SV_HV_RA    equ 3
SV_HV_RB    equ 4

SV_HSP_IN   equ 1               ; sv_hkd's spike byte: alternate points pulled
SV_HSP_ZIG  equ 2               ; in, or alternately raised and lowered
SV_HZIG     equ 30              ; ...by this, of 256 to the turn
SV_HACT     equ 10              ; descriptors a figure spends in a frame
SV_HPX      equ 4               ; pixels each: see the header
SV_HMR      equ 64              ; the figure's radius IN THE MODEL, fixed, and
                                ; sitting exactly on sv_rot3's ceiling - see
                                ; that routine's header for why 65 would be a
                                ; figure that turns inside out
SV_HRMIN    equ 52              ; ...and on the GLASS, which is a separate
SV_HRMAX    equ 118             ; number now: the rotation is done at model
                                ; scale and the result is scaled after, so the
                                ; figure's size is not bounded by the
                                ; arithmetic and the arithmetic keeps its full
                                ; precision at every size
SV_HSPK     equ 96              ; alternate points pulled in to 96/128, which
                                ; is what makes a ring a star

SV_HHOLD    equ 14              ; frames a finished figure stands
SV_HSTAG    equ 46              ; the SECOND slot's opening wait, which is
                                ; about half a figure's life - the two were
                                ; seeded a dozen frames apart and spent the
                                ; first minutes blooming together
SV_HWAIT    equ 12              ; ...and the most it waits before starting.
                                ; It is what keeps the two slots decorrelated,
                                ; and it is SHORT because a figure in WAIT is
                                ; the only state with nothing on the glass:
                                ; both slots waiting at once is a blank screen,
                                ; and the odds of that are this over the cycle
                                ; length, squared
SV_HIVAL    equ 1

SV_HP_WAIT  equ 0               ; the phases, in the order they run
SV_HP_GROW  equ 1
SV_HP_HOLD  equ 2
SV_HP_ERASE equ 3

; The walk counters are WORDS, which they were not: at the old size an edge
; fitted a byte and at this one a chord of the outer ring on a VGA does not.
; What is still asserted is the model radius, because that one IS arithmetic.
%if SV_HMR > 64
 %error "svshape.inc: SV_HMR is past sv_rot3's ceiling - see its header"
%endif

; -----------------------------------------------------------------------------
; sv_shape_init - both figures, decorrelated
; in:  the gfx lock is HELD and the screen has just been cleared
; -----------------------------------------------------------------------------
sv_shape_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov byte [sv_ival], SV_HIVAL
    xor al, al
.fig:
    push ax
    call sv_hbase
    call sv_hspawn
    pop ax
    push ax                     ; ...and the SECOND slot waits half a figure's
    or al, al                   ; life before it starts, because two spawned a
    jz .nostag                  ; dozen frames apart bloom together for
    mov bl, al                  ; minutes. Everything after this is decorrelated
    xor bh, bh                  ; by their own lengths, which now differ by
    mov byte [sv_hhd + bx], SV_HSTAG    ; shape as well as by size
.nostag:
    pop ax
    inc al
    cmp al, SV_HNF
    jb .fig

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_shape_step - one frame of every figure
; in:  the gfx lock is HELD
; -----------------------------------------------------------------------------
sv_shape_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    xor al, al
.fig:
    push ax
    call sv_hbase
    call sv_hfig
    pop ax
    inc al
    cmp al, SV_HNF
    jb .fig

    mov byte [sv_shown], 1
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sv_hbase - select figure AL (module internal)
; in:  AL = the figure; out: [sv_hf] and the three bases; clobbers AX, BX, DX
;
; THREE BASES AND NOT ONE, because the three array families are indexed
; differently: the vertices by word, the per-edge bytes by byte, and the walk
; blocks by GLS_SZ. Computing them once a figure is what keeps every loop
; below down to an add.
; -----------------------------------------------------------------------------
sv_hbase:
    mov [sv_hf], al
    mov ah, 0
    mov bx, ax
    mov ax, SV_HNV * 2
    mul bx
    mov [sv_hbv], ax
    mov ax, SV_HNE
    mul bx
    mov [sv_hbe], ax
    add ax, ax                  ; ...and the same base doubled, because the
    mov [sv_hbl], ax            ; walk counters are words
    mov ax, SV_HNE * GLS_SZ
    mul bx
    mov [sv_hbw], ax
    ret

; -----------------------------------------------------------------------------
; sv_hfig - one frame of the selected figure (module internal)
; -----------------------------------------------------------------------------
sv_hfig:
    mov bl, [sv_hf]
    xor bh, bh
    mov al, [sv_hph + bx]
    cmp al, SV_HP_GROW
    je .grow
    cmp al, SV_HP_HOLD
    je .hold
    cmp al, SV_HP_ERASE
    je .erase
    dec byte [sv_hhd + bx]      ; WAIT: nothing is on the glass yet
    jnz .out
    mov byte [sv_hph + bx], SV_HP_GROW
.out:
    ret
.grow:
    mov al, [sv_hik + bx]
    call sv_hrun
    jnc .out
    mov bl, [sv_hf]
    xor bh, bh
    mov byte [sv_hph + bx], SV_HP_HOLD
    mov byte [sv_hhd + bx], SV_HHOLD
    ret
.hold:
    dec byte [sv_hhd + bx]
    jnz .out
    call sv_hinitw              ; the SAME endpoints in the SAME order, which
    mov bl, [sv_hf]             ; is all 5.6.7 asks for the erase to agree
    xor bh, bh
    mov byte [sv_hph + bx], SV_HP_ERASE
    ret
.erase:
    mov al, SV_GROUND
    call sv_hrun
    jnc .out
    jmp sv_hspawn               ; ...and somewhere else entirely

; -----------------------------------------------------------------------------
; sv_hrun - spend this figure's frame in pen AL (module internal)
; in:  AL = the ink, the figure selected
; out: CF = 1 = every edge is spent, so the phase is over
; clobbers: AX, BX, CX, DX, SI, DI, BP, flags
;
; ONE ARRIVAL FOR THE WHOLE FIGURE (SPEC.md 5.6.8): the descriptors are built
; into `sv_hdsc` and handed over together. A descriptor carries no colour, so
; the pen is set once above the batch - which is also why a growing figure and
; an erasing one cannot share a call.
; -----------------------------------------------------------------------------
sv_hrun:
    push es
    call sv_pen

    mov bl, [sv_hf]
    xor bh, bh
    mov al, [sv_hne + bx]
    mov [sv_hnn], al
    mov al, [sv_hcur + bx]
    mov [sv_hci], al

.retire:                        ; the front walks past every spent edge, so
    mov al, [sv_hci]            ; the window below is always live work
    cmp al, [sv_hnn]
    jae .fin
    mov bl, al
    xor bh, bh
    add bx, bx
    add bx, [sv_hbl]
    cmp word [sv_hln + bx], 0
    jne .front
    inc byte [sv_hci]
    jmp short .retire

.front:
    mov al, [sv_hci]            ; bank it: next frame starts here
    mov bl, [sv_hf]
    xor bh, bh
    mov [sv_hcur + bx], al
    mov al, [sv_hci]
    xor ah, ah
    mov bx, GLS_SZ
    mul bx                      ; < 48 * 14, so DX is 0 and the walk pointer
    add ax, [sv_hbw]            ; can simply run forward from here
    add ax, sv_hwk
    mov bp, ax

    mov si, sv_hdsc
    xor cx, cx                  ; CX = descriptors built
    mov dl, [sv_hci]
    xor dh, dh
.scan:
    cmp cx, SV_HACT
    jae .spend
    mov al, [sv_hnn]
    cmp dl, al
    jae .spend
    mov bl, dl
    xor bh, bh
    add bx, bx
    add bx, [sv_hbl]
    mov ax, [sv_hln + bx]       ; pixels this edge still owes
    or ax, ax
    jz .next
    cmp ax, SV_HPX
    jbe .cap                    ; ...the last of it, and no further
    mov ax, SV_HPX
.cap:
    sub [sv_hln + bx], ax
    mov [si], bp                ; BP is DATA here, not an address: SI does the
    mov [si+2], ax              ; addressing and BP addresses SS (SPEC.md 1)
    add si, 4
    inc cx
.next:
    add bp, GLS_SZ
    inc dl
    jmp short .scan

.spend:
    jcxz .none
    push ds
    pop es                      ; ES:DI = the array, in OUR segment
    mov di, sv_hdsc
    call OSAPI_GFX_LSTEPV
.none:
    clc
    pop es
    ret
.fin:
    stc
    pop es
    ret

; -----------------------------------------------------------------------------
; sv_hinitw - arm a walk for every edge, and count what each owes
; in:  the figure selected; out: nothing; clobbers AX, BX, CX, DX, SI, DI, BP
;
; Called twice a figure - once to draw it and once to take it up - and the
; second call is why it takes its endpoints from the stored vertices rather
; than from anything the first call left behind.
; -----------------------------------------------------------------------------
sv_hinitw:
    push es
    push ds
    pop es

    mov bl, [sv_hf]
    xor bh, bh
    mov al, [sv_hne + bx]
    mov [sv_hnn], al
    mov byte [sv_hcur + bx], 0
    mov byte [sv_hci], 0
    mov bp, sv_hwk
    add bp, [sv_hbw]
.one:
    mov al, [sv_hci]
    cmp al, [sv_hnn]
    jae .done
    mov bl, al
    xor bh, bh
    add bx, [sv_hbe]

    mov al, [sv_hea + bx]
    call sv_hvpt
    mov [sv_hx1], ax
    mov [sv_hy1], dx
    mov al, [sv_heb + bx]
    call sv_hvpt
    mov [sv_hx2], ax
    mov [sv_hy2], dx

    mov ax, [sv_hx2]            ; the walk's length is max(|dx|,|dy|) + 1,
    sub ax, [sv_hx1]            ; which is Bresenham's major axis
    jns .adx
    neg ax
.adx:
    mov cx, [sv_hy2]
    sub cx, [sv_hy1]
    jns .ady
    neg cx
.ady:
    cmp ax, cx
    jae .have
    mov ax, cx
.have:
    inc ax
    push ax
    mov al, [sv_hci]            ; the counter is indexed by WORD
    xor ah, ah
    add ax, ax
    add ax, [sv_hbl]
    mov bx, ax
    pop ax
    mov [sv_hln + bx], ax

    mov ax, [sv_hx1]
    mov bx, [sv_hy1]
    mov cx, [sv_hx2]
    mov dx, [sv_hy2]
    mov di, bp
    call OSAPI_GFX_LINIT

    add bp, GLS_SZ
    inc byte [sv_hci]
    jmp short .one
.done:
    pop es
    ret

; sv_hvpt - vertex AL of the selected figure -> AX = x, DX = y
; clobbers AX, DX, DI, flags
sv_hvpt:
    xor ah, ah
    add ax, ax
    add ax, [sv_hbv]
    mov di, ax
    mov dx, [sv_hvy + di]
    mov ax, [sv_hvx + di]
    ret

; -----------------------------------------------------------------------------
; sv_hspawn - a whole new figure for the selected slot
; in:  the figure selected; out: nothing
;
; ROLLED WHOLE - points, radius, place, attitude and ink - so a session does
; not settle into one figure at one size wandering about. The WAIT it starts
; in is what keeps the two slots out of step: they are seeded apart, and a
; random wait at every respawn stops them drifting back together.
; -----------------------------------------------------------------------------
sv_hspawn:
    mov cx, SV_HNK              ; --- the SHAPE, and everything else off it
    call sv_rndn
    mov bl, SV_HKD_SZ
    mul bl
    add ax, sv_hkd
    mov [sv_hkp], ax
    mov bx, ax
    mov ax, [bx + SV_HKD_WAV]
    mov [sv_hwp], ax
    mov al, [bx + SV_HKD_NW]
    mov [sv_hnw], al

    mov al, [bx + SV_HKD_KMAX]  ; ...and the ring size IT allows, which for a
    sub al, [bx + SV_HKD_KMIN]  ; cube is four and only four
    xor ah, ah
    inc ax
    mov cx, ax
    call sv_rndn
    mov bx, [sv_hkp]
    add al, [bx + SV_HKD_KMIN]
    mov [sv_hkk], al

    mov cx, SV_HRMAX - SV_HRMIN + 1
    call sv_rndn
    add ax, SV_HRMIN            ; AX = the radius wanted, on the glass
    mov bx, [sv_h]              ; ...capped to the SHORT axis, so a figure is
    shr bx, 1                   ; never taller than the screen it is on - which
    sub bx, 2                   ; is what makes SV_HRMAX an ambition rather
    cmp ax, bx                  ; than a size, and lets the same constant suit
    jbe .rok                    ; a 200-row CGA and a 480-row VGA
    mov ax, bx
.rok:
    mov [sv_hrr], ax

    mov bx, [sv_hrr]            ; a centre the whole figure fits at
    add bx, bx                  ; BX = the diameter
    mov cx, [sv_w]
    sub cx, bx
    call sv_rndn
    add ax, [sv_hrr]
    mov [sv_hcx], ax
    mov bx, [sv_hrr]
    add bx, bx
    mov cx, [sv_h]
    sub cx, bx
    call sv_rndn
    add ax, [sv_hrr]
    mov [sv_hcy], ax

    call OSAPI_RAND             ; the attitude, both angles at once
    call sv_rotset

    mov cx, 3                   ; one of the three inks (SPEC.md 39.4)
    call sv_rndn
    inc al
    mov bl, [sv_hf]
    xor bh, bh
    mov [sv_hik + bx], al

    mov al, [sv_hkk]            ; K edges in each of this shape's waves
    xor ah, ah
    mov bl, [sv_hnw]
    xor bh, bh
    mul bx
    mov bl, [sv_hf]
    xor bh, bh
    mov [sv_hne + bx], al
    mov byte [sv_hph + bx], SV_HP_WAIT

    push bx
    mov cx, SV_HWAIT
    call sv_rndn
    inc al                      ; never zero: the counter is a `dec`/`jnz`
    pop bx
    mov [sv_hhd + bx], al

    call sv_hverts
    call sv_hedges
    call sv_hinitw
    ret

; -----------------------------------------------------------------------------
; sv_hverts - every vertex, in three dimensions and then on the glass
; in:  the figure selected, [sv_hcx]/[sv_hcy]/[sv_hkk]/[sv_hrr] set and the
;      four sines staged; out: [sv_hvx]/[sv_hvy] filled
;
; The hub is vertex 0 and sits at the centre, which is exactly what a rotation
; of the origin gives, so it is written rather than computed. Ring l point p
; is vertex 1 + l*K + p, and the second ring is TWISTED half a step so its
; points sit between the first ring's - which is what makes the struts between
; them slant instead of lying flat along a meridian.
; -----------------------------------------------------------------------------
sv_hverts:
    mov di, [sv_hbv]            ; --- the hub, which IS the rotated origin
    mov ax, [sv_hcx]
    mov [sv_hvx + di], ax
    mov ax, [sv_hcy]
    mov [sv_hvy + di], ax

    mov word [sv_hm], 0         ; --- the two poles, at -R and +R on y. BYTE,
    mov byte [sv_hm + 2], 0     ; because sv_hm is THREE bytes and a word store
                                ; at +2 reaches +3, which is sv_hne's first
                                ; entry - slot 0 then had nought edges for ever
                                ; and only slot 1 ever drew
    mov byte [sv_hm + 1], -SV_HMR
    mov al, 1
    call sv_hput
    mov byte [sv_hm + 1], SV_HMR
    mov al, 2
    call sv_hput

    mov byte [sv_hli], 0        ; --- and the two rings
.layer:
    mov bl, [sv_hli]
    xor bh, bh
    add bx, bx
    add bx, [sv_hkp]            ; the shape's row: ring A's pair, then B's
    mov al, SV_HMR
    mov bl, [bx + SV_HKD_RAS]
    call sv_mulsh
    mov [sv_hlrr], al
    mov bl, [sv_hli]
    xor bh, bh
    add bx, bx
    add bx, [sv_hkp]
    mov al, [bx + SV_HKD_RAP]
    mov [sv_hlph], al

    xor al, al                  ; the twist: none on ring A, the shape's own
    cmp byte [sv_hli], 0        ; half-step on ring B
    je .tw
    mov bx, [sv_hkp]
    cmp byte [bx + SV_HKD_TW], 0
    je .tw
    mov al, 128
    xor ah, ah
    mov bl, [sv_hkk]
    xor bh, bh
    div bl
.tw:
    mov [sv_hltw], al

    mov byte [sv_hpi], 0
.point:
    mov bx, [sv_hkp]            ; --- this point's own radius and latitude
    mov al, [sv_hlrr]
    mov ah, [sv_hlph]
    cmp byte [bx + SV_HKD_SPK], SV_HSP_IN
    jne .zig
    test byte [sv_hpi], 1       ; every other one pulled IN, which is what
    jz .full                    ; makes a ring a star
    mov bl, SV_HSPK
    call sv_mulsh
    jmp short .full
.zig:
    cmp byte [bx + SV_HKD_SPK], SV_HSP_ZIG
    jne .full
    test byte [sv_hpi], 1       ; ...or every other one RAISED, which is what
    jz .full                    ; makes a rim a three-dimensional star
    add ah, SV_HZIG
.full:
    mov [sv_hpr], al
    mov [sv_hlph2], ah

    mov al, [sv_hlph2]          ; y = r * sin(phi)
    call sv_sin
    mov bl, al
    mov al, [sv_hpr]
    call sv_mulsh
    mov [sv_hm + 1], al

    mov al, [sv_hlph2]          ; ...and the radius left in the xz plane
    call sv_cos
    mov bl, al
    mov al, [sv_hpr]
    call sv_mulsh
    mov [sv_hpc], al

    mov al, [sv_hpi]            ; theta = p * 256 / K, plus the ring's twist
    mov ah, al                  ; and the shape's own offset - which is what
    xor al, al                  ; puts a four-point ring's corners at 45
    xor dx, dx                  ; degrees, where a cube's are
    mov bl, [sv_hkk]
    xor bh, bh
    div bx
    add al, [sv_hltw]
    mov bx, [sv_hkp]
    add al, [bx + SV_HKD_TH0]
    mov [sv_hth], al

    call sv_cos                 ; x = rc * cos(theta)
    mov bl, al
    mov al, [sv_hpc]
    call sv_mulsh
    mov [sv_hm], al
    mov al, [sv_hth]            ; z = rc * sin(theta)
    call sv_sin
    mov bl, al
    mov al, [sv_hpc]
    call sv_mulsh
    mov [sv_hm + 2], al

    mov al, [sv_hli]            ; vertex index = 3 + l*K + p
    xor ah, ah
    mov bl, [sv_hkk]
    xor bh, bh
    mul bx
    add al, [sv_hpi]
    adc ah, 0
    add ax, 3
    call sv_hput

    inc byte [sv_hpi]
    mov al, [sv_hpi]
    cmp al, [sv_hkk]
    jb .point
    inc byte [sv_hli]
    cmp byte [sv_hli], SV_HNL
    jb .layer
    ret

; -----------------------------------------------------------------------------
; sv_hput - the model vertex in [sv_hm] through the attitude, onto the glass,
;           and stored as vertex AL
; in:  AX = the vertex index, [sv_hm] = three signed bytes
; out: nothing; clobbers AX, BX, DI, flags
; -----------------------------------------------------------------------------
sv_hput:
    push si
    push ax
    mov si, sv_hm
    call sv_rot3
    pop ax
    xor ah, ah
    add ax, ax
    add ax, [sv_hbv]
    mov di, ax
    mov al, [sv_rx]             ; ...and to the size it is drawn at, in 16 bits
    cbw                         ; so a big figure is not a coarse one
    imul word [sv_hrr]
    mov cl, 6
    sar ax, cl
    add ax, [sv_hcx]
    mov [sv_hvx + di], ax
    mov al, [sv_ry]
    cbw
    imul word [sv_hrr]
    mov cl, 6
    sar ax, cl
    mov bx, [sv_hcy]
    sub bx, ax                  ; screen y grows DOWN and the model's up
    mov [sv_hvy + di], bx
    pop si
    ret

; -----------------------------------------------------------------------------
; sv_hedges - the six waves, in the order they bloom
; in:  the figure selected and [sv_hkk] set; out: [sv_hea]/[sv_heb] filled
; -----------------------------------------------------------------------------
sv_hedges:
    mov di, [sv_hbe]
    mov byte [sv_hwi], 0
.wave:
    mov byte [sv_hpi], 0
.point:
    mov bl, [sv_hwi]            ; the wave's two descriptors
    xor bh, bh
    add bx, bx
    add bx, bx
    add bx, [sv_hwp]            ; ...out of THIS shape's table
    mov al, [bx]
    mov ah, [bx + 1]
    push bx
    call sv_hvi
    pop bx
    mov [sv_hea + di], al
    mov al, [bx + 2]
    mov ah, [bx + 3]
    call sv_hvi
    mov [sv_heb + di], al
    inc di

    inc byte [sv_hpi]
    mov al, [sv_hpi]
    cmp al, [sv_hkk]
    jb .point
    inc byte [sv_hwi]
    mov al, [sv_hwi]
    cmp al, [sv_hnw]
    jb .wave
    ret

; -----------------------------------------------------------------------------
; sv_hvi - a wave descriptor and [sv_hpi] -> AL = a vertex index
; in:  AL = the ring (0 = the hub, 1 or 2 = a ring), AH = the step around it
; out: AL; clobbers AX, BX, CX, flags
; -----------------------------------------------------------------------------
sv_hvi:
    cmp al, SV_HV_RA
    jae .ring
    xor ah, ah                  ; hub, bottom pole, top pole: AL already IS
    ret                         ; the index, and the step says nothing
.ring:
    push dx
    mov dl, al                  ; DL = which ring
    mov al, ah                  ; (p + step) mod K. The step is signed and the
    add al, [sv_hpi]            ; sum is done in a BYTE, so -1 + p + K lands
    add al, [sv_hkk]            ; clear of zero before the fold
    mov ah, 0
    mov bl, [sv_hkk]
    xor bh, bh
    div bl
    mov al, ah
    cmp dl, SV_HV_RB            ; ring A starts at 3, ring B at 3 + K
    jne .base
    add al, [sv_hkk]
.base:
    add al, 3
    xor ah, ah
    pop dx
    ret

; The waves, four bytes each: the vertex class and step of the first end, then
; of the second. ORDER IS THE BLOOM, and the FIRST end is where an edge grows
; FROM - which is the whole of how a shape chooses where its runners start.
sv_hw_flow:                     ; hub, out to a ring, out to another, chorded
    db SV_HV_HUB, 0,  SV_HV_RA, 0
    db SV_HV_RA,  0,  SV_HV_RA, 1
    db SV_HV_RA,  0,  SV_HV_RB, 0
    db SV_HV_RA,  0,  SV_HV_RB, -1
    db SV_HV_RB,  0,  SV_HV_RB, 1
    db SV_HV_RB,  0,  SV_HV_RB, 2

sv_hw_dia:                      ; EVERY runner leaves the bottom tip together
    db SV_HV_BOT, 0,  SV_HV_RA, 0
    db SV_HV_RA,  0,  SV_HV_RA, 1
    db SV_HV_RA,  0,  SV_HV_TOP, 0

sv_hw_cube:                     ; the four top corners start together, and the
    db SV_HV_RA,  0,  SV_HV_RA, 1   ; four verticals leave those same corners
    db SV_HV_RA,  0,  SV_HV_RB, 0
    db SV_HV_RB,  0,  SV_HV_RB, 1

sv_hw_star:                     ; the tips run INWARD, round the core, and back
    db SV_HV_RA,  0,  SV_HV_RB, 0   ; out to the NEXT tip
    db SV_HV_RB,  0,  SV_HV_RB, 1
    db SV_HV_RB,  0,  SV_HV_RA, 1

; --- the shapes themselves, SV_HKD_SZ bytes each -----------------------------
SV_HKD_WAV  equ 0               ; dw: its wave table
SV_HKD_NW   equ 2               ; db: how many waves
SV_HKD_KMIN equ 3               ; db: and the ring sizes it allows
SV_HKD_KMAX equ 4
SV_HKD_RAS  equ 5               ; db: ring A's radius, of 128, and latitude
SV_HKD_RAP  equ 6
SV_HKD_RBS  equ 7               ; db: ring B's, at SV_HKD_RAS + 2 - which is
SV_HKD_RBP  equ 8               ; why sv_hverts indexes the pair by the layer
SV_HKD_TW   equ 9               ; db: ring B twisted half a step?
SV_HKD_TH0  equ 10              ; db: a theta offset for BOTH rings
SV_HKD_SPK  equ 11              ; db: SV_HSP_*

sv_hkd:
    dw sv_hw_flow
    db 6, 5, 8,  72, 44, 127,  0,  1,  0, SV_HSP_IN
    dw sv_hw_dia
    db 3, 5, 8, 127,  0, 127,  0,  0,  0, 0
    dw sv_hw_cube               ; K is FORCED to 4 and the corners sit at 45
    db 3, 4, 4, 127, 25, 127, -25, 0, 32, 0   ; degrees of latitude 35.26, which
                                              ; is what makes it a cube and not
                                              ; a square antiprism
    dw sv_hw_star
    db 3, 5, 8, 127,  0,  52,  0,  1,  0, SV_HSP_ZIG

sv_hf:      db 0                ; the figure being worked, and its three bases
sv_hbv:     dw 0
sv_hbe:     dw 0
sv_hbl:     dw 0
sv_hbw:     dw 0

sv_hci:     db 0                ; sv_hrun / sv_hinitw: the edge in hand
sv_hnn:     db 0                ; ...and how many the figure has
sv_hx1:     dw 0
sv_hy1:     dw 0
sv_hx2:     dw 0
sv_hy2:     dw 0

sv_hcx:     dw 0                ; sv_hspawn / sv_hverts: ONE figure at a time,
sv_hcy:     dw 0                ; which is what lets these be single copies
sv_hrr:     dw 0                ; the radius ON THE GLASS
sv_hkk:     db SV_HKMIN
sv_hli:     db 0
sv_hpi:     db 0
sv_hwi:     db 0
sv_hkp:     dw sv_hkd           ; the shape in hand, and its wave table
sv_hwp:     dw sv_hw_flow
sv_hnw:     db 6
sv_hlph2:   db 0
sv_hlrr:    db 0
sv_hlph:    db 0
sv_hltw:    db 0
sv_hpr:     db 0
sv_hpc:     db 0
sv_hth:     db 0
sv_hm:      db 0, 0, 0          ; the model vertex handed to sv_rot3

sv_hne:     times SV_HNF db 0   ; per figure
sv_hcur:    times SV_HNF db 0
sv_hph:     times SV_HNF db SV_HP_WAIT
sv_hhd:     times SV_HNF db 1
sv_hik:     times SV_HNF db SV_MAIN

sv_hvx:     times SV_HNF * SV_HNV dw 0
sv_hvy:     times SV_HNF * SV_HNV dw 0
sv_hea:     times SV_HNF * SV_HNE db 0
sv_heb:     times SV_HNF * SV_HNE db 0
sv_hln:     times SV_HNF * SV_HNE dw 0
sv_hwk:     times SV_HNF * SV_HNE * GLS_SZ db 0
sv_hdsc:    times SV_HACT * 2 dw 0
