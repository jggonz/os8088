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
%if SV_HRMAX * 2 + 8 > 255
 %error "svshape.inc: an edge of a figure SV_HRMAX across no longer fits sv_hln's byte"
%endif
%if SV_HMR > 64
 %error "svshape.inc: SV_HMR is past sv_rot3's ceiling - see its header"
%endif

; -----------------------------------------------------------------------------
; sv_shape_init - both figures, decorrelated
; in:  the gfx lock is HELD and the screen has just been cleared
;
; ...and the SECOND slot waits half a figure's life before it starts, because
; two spawned a dozen frames apart bloom together for minutes. Everything after
; this is decorrelated by their own lengths, which now differ by shape as well
; as by size. SV_HNF is two, so the walk over the figures is written out: one
; call of the tail below for figure 0, and a fall into it for figure 1 - which
; sv_shape_step does too (driver size pass 1).
; -----------------------------------------------------------------------------
%if SV_HNF != 2
 %error "svshape.inc: sv_shape_init and sv_shape_step are written for two figures"
%endif
sv_shape_init:
    mov byte [sv_ival], SV_HIVAL
    xor ax, ax
    call .fig
    mov al, 1
    call .fig
    mov byte [sv_hhd + 1], SV_HSTAG
    ret
.fig:
    call sv_hbase
    jmp sv_hspawn

; -----------------------------------------------------------------------------
; sv_shape_step - one frame of every figure
; in:  the gfx lock is HELD
; -----------------------------------------------------------------------------
sv_shape_step:
    xor ax, ax
    call .fig
    mov al, 1
.fig:
    call sv_hbase
    ; ...and FALLS INTO sv_hfig, whose ret is this one's

; -----------------------------------------------------------------------------
; sv_hfig - one frame of the selected figure (module internal)
; -----------------------------------------------------------------------------
sv_hfig:
    mov bx, [sv_hf]
    mov al, [sv_hph + bx]
    cmp al, SV_HP_HOLD
    je .hold
    ja .erase
    or al, al
    jnz .grow
    dec byte [sv_hhd + bx]      ; WAIT: nothing is on the glass yet
    jnz .out
    mov byte [sv_hph + bx], SV_HP_GROW
.out:
    ret
.grow:
    mov al, [sv_hik + bx]
    call sv_hrun
    jnc .out
    mov bx, [sv_hf]
    mov byte [sv_hph + bx], SV_HP_HOLD
    mov byte [sv_hhd + bx], SV_HHOLD
    ret
.hold:
    dec byte [sv_hhd + bx]
    jnz .out
    call sv_hinitw              ; the SAME endpoints in the SAME order, which
    mov bx, [sv_hf]             ; is all 5.6.7 asks for the erase to agree
    mov byte [sv_hph + bx], SV_HP_ERASE
    ret
.erase:
    mov al, SV_GROUND
    call sv_hrun
    jnc .out
    jmp sv_hspawn               ; ...and somewhere else entirely
%if SV_HP_WAIT != 0 || SV_HP_GROW != 1 || SV_HP_HOLD != 2 || SV_HP_ERASE != 3
 %error "svshape.inc: sv_hfig's dispatch is written for the phases 0..3 in order"
%endif

; -----------------------------------------------------------------------------
; sv_hbase - select figure AL (module internal)
; in:  AL = the figure; out: [sv_hf] and the three bases; clobbers AX, BX, DX
;
; THREE BASES AND NOT ONE, because the three array families are indexed
; differently: the vertices by word, the per-edge bytes by byte, and the walk
; blocks by GLS_SZ. Computing them once a figure is what keeps every loop
; below down to an add. [sv_hf] is a WORD whose high byte is 0, so every
; per-figure array is indexed by one load of it.
; -----------------------------------------------------------------------------
sv_hbase:
    mov ah, 0
    mov [sv_hf], ax
    mov bx, ax
    mov ax, SV_HNV * 2
    mul bx
    mov [sv_hbv], ax
    mov ax, SV_HNE
    mul bx
    mov [sv_hbe], ax            ; ...which the walk counters use too: a
                                ; counter is a BYTE (sv_hln, below)
    mov ax, SV_HNE * GLS_SZ
    mul bx
    mov [sv_hbw], ax
    ret

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
;
; IN REGISTERS (driver size pass 1): DL is the edge in hand and DH how many the
; figure has, SI this figure's run of sv_hln and BX the edge within it, DI the
; descriptor being built and BP the walk block it names.
; -----------------------------------------------------------------------------
sv_hrun:
    call sv_pen
    mov bx, [sv_hf]
    mov dh, [sv_hne + bx]
    mov dl, [sv_hcur + bx]
    mov si, sv_hln
    add si, [sv_hbe]
    mov bh, 0

.retire:                        ; the front walks past every spent edge, so
    cmp dl, dh                  ; the window below is always live work
    jae .fin
    mov bl, dl
    cmp byte [bx + si], 0
    jne .front
    inc dl
    jmp short .retire
.fin:
    stc
    ret

.front:
    mov bx, [sv_hf]             ; bank it: next frame starts here
    mov [sv_hcur + bx], dl
    mov al, GLS_SZ
    mul dl                      ; < 48 * 14, and the walk pointer can simply
    add ax, [sv_hbw]            ; run forward from here
    add ax, sv_hwk
    mov bp, ax

    mov di, sv_hdsc
    xor cx, cx                  ; CX = descriptors built
    mov bh, 0
.scan:
    cmp cx, SV_HACT
    jae .spend
    cmp dl, dh
    jae .spend
    mov bl, dl
    mov al, [bx + si]           ; pixels this edge still owes
    or al, al
    jz .next
    cmp al, SV_HPX
    jbe .cap                    ; ...the last of it, and no further
    mov al, SV_HPX
.cap:
    sub [bx + si], al
    cbw                         ; 1..SV_HPX, so AH = 0 - the count is a word
    mov [di], bp                ; BP is DATA here, not an address: DI does the
    mov [di + 2], ax            ; addressing and BP addresses SS (SPEC.md 1)
    add di, 4
    inc cx
.next:
    add bp, GLS_SZ
    inc dl
    jmp short .scan

.spend:
    jcxz .none
    mov si, sv_hdsc             ; the array, in our own segment, and the WALK
    call gfxe_wstepv            ; is ours now (SPEC.md 5.12.5), so what reaches
    call gfxe_pput              ; the kernel is the pixels it stepped
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; sv_hinitw - arm a walk for every edge, and count what each owes
; in:  the figure selected; out: nothing; clobbers AX, BX, CX, DX, SI, DI, BP
;
; Called twice a figure - once to draw it and once to take it up - and the
; second call is why it takes its endpoints from the stored vertices rather
; than from anything the first call left behind.
;
; The count is the walk's length, max(|dx|, |dy|) + 1, which is Bresenham's
; major axis - and gfxe_winit has just written |dx| and |dy| into the block,
; so it is read back out of it rather than worked out a second time. A figure
; always has edges (K >= 4 in three or more waves), so the walk tests its
; count at the bottom.
; -----------------------------------------------------------------------------
sv_hinitw:
    mov bx, [sv_hf]
    mov al, [sv_hne + bx]
    mov [sv_hci], al            ; the edges left to arm
    mov byte [sv_hcur + bx], 0
    mov si, [sv_hbe]            ; SI = the edge, in the per-edge byte arrays
    mov bp, sv_hwk              ; BP = its walk block
    add bp, [sv_hbw]
.one:
    mov al, [sv_hea + si]
    call sv_hvpt
    push ax
    push dx
    mov al, [sv_heb + si]
    call sv_hvpt
    mov cx, ax                  ; CX/DX = the far end...
    pop bx                      ; ...AX/BX the near one
    pop ax
    mov di, bp
    call gfxe_winit             ; SPEC.md 5.12.5: the block is ours, and every
    mov ax, [di + GLS_DX]       ; register comes back
    mov cx, [di + GLS_DY]
    cmp ax, cx
    jae .have
    mov ax, cx
.have:
    inc ax                      ; < 256 - see sv_hln
    mov [sv_hln + si], al
    add bp, GLS_SZ
    inc si
    dec byte [sv_hci]
    jnz .one
    ret

; sv_hvpt - vertex AL of the selected figure -> AX = x, DX = y
; clobbers AX, DX, DI, flags. AL < SV_HNV, so `cbw` is the zero extension
sv_hvpt:
    cbw
    add ax, ax
    xchg ax, di
    add di, [sv_hbv]
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
    call sv_rndn                ; (sv_rndn keeps BX, which every step below
    mov bl, SV_HKD_SZ           ; leans on)
    mul bl
    add ax, sv_hkd
    mov [sv_hkp], ax
    mov bx, ax
    mov ax, [bx + SV_HKD_WAV]
    mov [sv_hwp], ax
    mov al, [bx + SV_HKD_NW]
    mov [sv_hnw], al

    mov cl, [bx + SV_HKD_KMAX]  ; ...and the ring size IT allows, which for a
    sub cl, [bx + SV_HKD_KMIN]  ; cube is four and only four
    mov ch, 0
    inc cx
    call sv_rndn
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

    mov bx, ax                  ; a centre the whole figure fits at
    mov di, sv_hcx
    call sv_rndxy

    call OSAPI_RAND             ; the attitude, both angles at once
    call sv_rotset

    mov cx, 3                   ; one of the three inks (SPEC.md 39.4)
    call sv_rndn
    inc ax
    mov bx, [sv_hf]
    mov [sv_hik + bx], al

    mov al, [sv_hkk]            ; K edges in each of this shape's waves
    mul byte [sv_hnw]
    mov [sv_hne + bx], al
    mov byte [sv_hph + bx], SV_HP_WAIT

    mov cx, SV_HWAIT
    call sv_rndn
    inc ax                      ; never zero: the counter is a `dec`/`jnz`
    mov [sv_hhd + bx], al

    call sv_hverts
    call sv_hedges
    jmp sv_hinitw

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
;
; THE WALK IS IN REGISTERS where it can be (driver size pass 1): BP counts the
; vertex - 3 + l*K + p is one more each point - and DX is the ring's offset
; into the shape's row on entry to .ring, which is called for ring A and
; fallen into for ring B. CL carries theta and CH the spike across the
; multiplies, which keep CX; sv_hput spends the rest.
; -----------------------------------------------------------------------------
sv_hverts:
    mov di, [sv_hbv]            ; --- the hub, which IS the rotated origin
    mov ax, [sv_hcx]
    mov [sv_hvx + di], ax
    mov ax, [sv_hcy]
    mov [sv_hvy + di], ax

    mov word [sv_hm], (-SV_HMR & 0FFh) << 8  ; --- the two poles, at -R and +R
    mov byte [sv_hm + 2], 0     ; on y. A BYTE at +2, because sv_hm is THREE
                                ; bytes and a word store at +2 reaches +3,
                                ; which is another variable: slot 0 once had
                                ; nought edges for ever that way and only slot
                                ; 1 ever drew
    mov al, 1
    call sv_hput
    mov byte [sv_hm + 1], SV_HMR
    mov al, 2
    call sv_hput

    mov bp, 3                   ; --- and the two rings
    xor dx, dx
    call .ring                  ; ring A...
    mov dx, 2                   ; ...and B, whose pair is 2 bytes on in the row
.ring:
    mov bx, [sv_hkp]            ; the shape's row: ring A's pair, then B's
    add bx, dx
    mov al, [bx + SV_HKD_RAP]   ; the phase first, while BX still names the
    mov [sv_hlph], al           ; row - the scale's load spends BL
    mov al, SV_HMR
    mov bl, [bx + SV_HKD_RAS]
    call sv_mulsh
    mov [sv_hlrr], al

    xor al, al                  ; the twist: none on ring A, the shape's own
    or dx, dx                   ; half-step on ring B
    jz .tw
    mov bx, [sv_hkp]
    cmp byte [bx + SV_HKD_TW], 0
    je .tw
    mov ax, 128
    div byte [sv_hkk]
.tw:
    mov [sv_hltw], al

    mov byte [sv_hpi], 0
.point:
    mov bx, [sv_hkp]            ; --- theta = p * 256 / K, plus the ring's
    mov ah, [sv_hpi]            ; twist and the shape's own offset - which is
    xor al, al                  ; what puts a four-point ring's corners at 45
    div byte [sv_hkk]           ; degrees, where a cube's are. p < K, so the
    add al, [sv_hltw]           ; quotient is under 256 and a byte divide
    add al, [bx + SV_HKD_TH0]   ; holds it
    mov cl, al                  ; CL = theta
    mov ch, [bx + SV_HKD_SPK]   ; CH = the spike, before BL is spent

    mov ax, [sv_hlrr]           ; --- this point's own radius and latitude:
    test byte [sv_hpi], 1       ; AL = [sv_hlrr], AH = [sv_hlph]. Every other
    jz .full                    ; one is pulled IN, which is what makes a ring
    dec ch                      ; a star...
    jnz .zig
    mov bl, SV_HSPK
    call sv_mulsh
    jmp short .full
.zig:
    dec ch                      ; ...or RAISED, which is what makes a rim a
    jnz .full                   ; three-dimensional star
    add ah, SV_HZIG
.full:
    xchg al, ah                 ; AL = phi, AH = r
    push ax
    call sv_rsin                ; y = r * sin(phi)
    mov [sv_hm + 1], al
    pop ax
    call sv_rcos                ; ...and the radius left in the xz plane
    mov ah, al
    mov al, cl                  ; AL = theta, AH = that radius
    push ax
    call sv_rcos                ; x = rc * cos(theta)
    mov [sv_hm], al
    pop ax
    call sv_rsin                ; z = rc * sin(theta)
    mov [sv_hm + 2], al

    mov ax, bp                  ; vertex 3 + l*K + p
    call sv_hput
    inc bp

    inc byte [sv_hpi]
    mov al, [sv_hpi]
    cmp al, [sv_hkk]
    jb .point
    ret

; -----------------------------------------------------------------------------
; sv_hput - the model vertex in [sv_hm] through the attitude, onto the glass,
;           and stored as vertex AL
; in:  AL = the vertex index, [sv_hm] = three signed bytes
; out: nothing; clobbers AX, BX, CX, DX, SI, DI, flags
; -----------------------------------------------------------------------------
sv_hput:
    push ax
    mov si, sv_hm
    call sv_rot3
    pop ax
    cbw                         ; the index is < SV_HNV
    add ax, ax
    xchg ax, di
    add di, [sv_hbv]
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
    sar ax, cl                  ; CL is still 6: `imul` keeps CX
    mov bx, [sv_hcy]
    sub bx, ax                  ; screen y grows DOWN and the model's up
    mov [sv_hvy + di], bx
    ret

; -----------------------------------------------------------------------------
; sv_hedges - the six waves, in the order they bloom
; in:  the figure selected and [sv_hkk] set; out: [sv_hea]/[sv_heb] filled
;
; SI walks the shape's wave table four bytes a wave and [sv_hwi] counts the
; waves down; sv_hvi keeps both.
; -----------------------------------------------------------------------------
sv_hedges:
    mov di, [sv_hbe]
    mov si, [sv_hwp]
    mov al, [sv_hnw]
    mov [sv_hwi], al
.wave:
    mov byte [sv_hpi], 0
.point:
    mov ax, [si]                ; the wave's two descriptors
    call sv_hvi
    mov [sv_hea + di], al
    mov ax, [si + 2]
    call sv_hvi
    mov [sv_heb + di], al
    inc di

    inc byte [sv_hpi]
    mov al, [sv_hpi]
    cmp al, [sv_hkk]
    jb .point
    add si, 4
    dec byte [sv_hwi]
    jnz .wave
    ret

; -----------------------------------------------------------------------------
; sv_hvi - a wave descriptor and [sv_hpi] -> AL = a vertex index
; in:  AL = the ring (0 = the hub, 1 or 2 = a ring), AH = the step around it
; out: AL; clobbers AX, DL, flags
; -----------------------------------------------------------------------------
sv_hvi:
    cmp al, SV_HV_RA            ; hub, bottom pole, top pole: AL already IS
    jb .out                     ; the index, and the step says nothing
    mov dl, al                  ; DL = which ring
    mov al, ah                  ; (p + step) mod K. The step is signed and the
    add al, [sv_hpi]            ; sum is done in a BYTE, so -1 + p + K lands
    add al, [sv_hkk]            ; clear of zero before the fold
    mov ah, 0
    div byte [sv_hkk]
    mov al, ah
    cmp dl, SV_HV_RB            ; ring A starts at 3, ring B at 3 + K
    jne .base
    add al, [sv_hkk]
.base:
    add al, 3
.out:
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

; The mode's state is a macro emitted into saver.asm's mode union (see
; SV_CUBE_STATE). sv_hspawn writes every per-figure field - the kind, its
; wave table, K, the radius, the centre, the vertices, the edges and the walk
; - for both figures before the first frame reads one, so nothing here needs
; a load-time value.
%macro SV_SHAPE_STATE 0
sv_hf:      resw 1               ; the figure being worked - a WORD, high byte
sv_hbv:     resw 1               ; 0, so it indexes - and its three bases
sv_hbe:     resw 1
sv_hbw:     resw 1

sv_hci:     resb 1               ; sv_hinitw: the edges left to arm

sv_hcx:     resw 1               ; sv_hspawn / sv_hverts: ONE figure at a time,
sv_hcy:     resw 1               ; which is what lets these be single copies
sv_hrr:     resw 1               ; the radius ON THE GLASS
sv_hkk:     resb 1
sv_hpi:     resb 1
sv_hwi:     resb 1
sv_hkp:     resw 1           ; the shape in hand, and its wave table
sv_hwp:     resw 1
sv_hnw:     resb 1
sv_hlrr:    resb 1           ; a ring's radius and latitude, IN THAT ORDER:
sv_hlph:    resb 1           ; sv_hverts loads the pair as one word
sv_hltw:    resb 1
sv_hm:      resb 3             ; the model vertex handed to sv_rot3

sv_hne:     resb SV_HNF       ; per figure
sv_hcur:    resb SV_HNF
sv_hph:     resb SV_HNF
sv_hhd:     resb SV_HNF
sv_hik:     resb SV_HNF

sv_hvx:     resw SV_HNF * SV_HNV
sv_hvy:     resw SV_HNF * SV_HNV
sv_hea:     resb SV_HNF * SV_HNE
sv_heb:     resb SV_HNF * SV_HNE
sv_hln:     resb SV_HNF * SV_HNE    ; pixels each edge still owes. A BYTE:
                                    ; an edge is max(|dx|, |dy|) + 1 and both
                                    ; its ends are within sv_hrr <= SV_HRMAX
                                    ; of the centre, so it is at most
                                    ; 2 * SV_HRMAX + 1 (asserted at
                                    ; SV_HRMAX). It was a word, and 96 bytes
                                    ; of it set the size of the mode union
sv_hwk:     resb SV_HNF * SV_HNE * GLS_SZ
sv_hdsc:    resw SV_HACT * 2
%endmacro

; ...and the points those descriptors step into. SV_HACT descriptors of at most
; SV_HPX pixels each is the exact bound, so this list is never forced to commit
; itself early (gfxe_pflush stays 0) - which is the one place in the tree where
; the buffer can be sized by arithmetic rather than by measurement.
; THE POINT LIST IS SHARED WITH THE STARFIELD (SPEC.md 79.5.11), which since
; it went to OSAPI_GFX_POINTS is the bigger of the two askers: a band is up to
; ~1.5 points a star at SV_NSTAR = 40. Overflowing it is not a defect - the
; library's gfxe_padd commits the full list and carries on - it is one extra
; ~756us arrival, so this is a tuning number and not a bound. The %if is the
; half that IS a bound: this mode's own need is exact, and a later edit that
; sized the buffer for the stars alone would silently cost the shapes an
; arrival an edge.
SV_PTMAX    equ 64
%if SV_PTMAX < SV_HACT * SV_HPX
 %error "SV_PTMAX is under the shapes mode's own need (SV_HACT * SV_HPX)"
%endif
; It is in the union too, at an offset past BOTH of its users' own state
; (saver.asm), so neither mode's walk can reach it.
%macro SV_PTS_STATE 0
sv_pts:     resw SV_PTMAX * 2
%endmacro
