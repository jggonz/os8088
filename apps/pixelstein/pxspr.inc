; =============================================================================
; os8088 - apps/pixelstein/pxspr.inc
;
; THE SPRITES AND THE WEAPON (SPEC.md 97.6): what the frame draws after the
; walls. TWO HALVES around the compose (px_frame_begin): px_spr_gather after
; the cast - the candidates, the sort, the cap, the match against what stood
; on this page's glass last frame and the ERASE of what no longer stands -
; and px_spr_draw after it - the posts, the weapon, the record of what stands
; now, the second far call. DS = ours in both.
;
; CANDIDATES are the statics whose own cell, and the actors whose own cell or
; any of its eight neighbours (an open one), carries this frame's spotvis
; generation in EITHER map layout (97.2: the walker that crossed a cell
; marked the array of its own layout). Each is TRANSFORMED into the view -
; nx forward, ny to the right, both Q8.8 - and dropped if it is nearer than
; PX_MINDIST or wider of the axis than 45 degrees (|ny| >= nx: outside any
; Size's field, and the bound that keeps the projection's idiv in range);
; h = PX_HEIGHTK / nx is its height in the wall's own units, and its centre
; column is cols / 2 + (ny * PX_FOCAL / nx) >> (2 at Full, 3 at Low res).
; The candidate goes into a list SORTED FAR TO NEAR of PX_MAXSPR = 8 - a
; ninth that is farther than the farthest is dropped, one that is nearer
; pushes the farthest out - and every one is priced (its visible columns x
; its rows): past PX_SPRCAP stores the farthest are marked to draw EVERY
; SECOND column until the sum fits.
;
; A SPRITE IS POSTS (97.3, 97.6): per covered screen column the z-test is
; one word compare of its h against wallh[c] - the wall's true height, the
; same units - and a column it wins takes source column col2tex[j] of its
; frame (31 - that when mirrored, the pair j >> 1 on C160), whose RUN TABLE
; in the sprite set names its opaque spans; each span is one draw-queue
; entry that enters the column's compiled scaler at codeofs[v0] and carries
; codeofs[v1] as THE PATCH - the driver's one patch site, a near ret over
; that texel's load for the length of the call (pxgen.inc). Under the Flat
; and Wire rungs, or with the set refused, a sprite is a SILHOUETTE - its
; runs through the static ladder in one tone - or, with no run table to
; read, a box.
;
; A SPRITE THAT STANDS IS NOT REDRAWN (review, wave 3): the first cut
; px_touched every drawn column - the column's memory forgotten, so the
; next frame re-laid it whole, drew the sprite again and touched it again,
; for as long as anything was in view, on a frame that moved nothing (a
; still windowed frame with a static in view read 85-88 ms against wave
; 2's 67-70). Now a drawn column's memory is left alone - the wall and
; ground under the sprite are what the memory says, the sprite's pixels
; over them - and each page keeps THE RECORD of the sprites on its glass
; (px_lsc: h, c0, w, frame, mirror, actor, the rows). At the next frame's
; gather every candidate is matched against the page's records; one that
; matches STANDS (PXS_CF_STAND) and its columns are drawn only where the
; compose re-laid them this frame (px_cwrote[c] == the generation: a door
; slid behind it, a wall moved under it); a record no candidate matches
; is ERASED - its columns px_touched BEFORE the compose, so the compose
; re-lays the wall and ground where it was. A sprite that moved is both:
; the old record erased, the new place drawn. The weapon follows the same
; rule on its sixteen columns, per page.
;
; THE WEAPON is sixteen posts, no depth test: PX_WPNCOLS byte columns at
; the band's middle through the Full set's 24-row scaler, DI biased by
; (PX_WPNROW0 - 28) rows so it lands on the view's bottom 24.
; =============================================================================

PXS_CF_MIR   equ 1                  ; a candidate's flags (PXS_C_FL): mirrored
PXS_CF_HALF  equ 2                  ; ...every second column (the cap)
PXS_CF_STAND equ 4                  ; ...it stood on this page's glass last
                                    ; frame at this very place (the match)
PXS_CF_KEPT  equ 8                  ; a RECORD's scratch bit: matched by a
                                    ; candidate this frame, so not erased
PXS_CF_GLASS equ 16                 ; a column of it passed the z-test this
                                    ; frame: it is on the glass, recorded
PXS_CF_PART  equ 32                 ; the queue filled under it: what was
                                    ; queued is drawn and the rest is not,
                                    ; so its record never MATCHES and the
                                    ; next frame erases and redraws it whole
                                    ; (review r1: a record made whole of a
                                    ; cut sprite stood with a hole in it)
PXS_C_TOP    equ 4                  ; a RECORD reuses the cost word for the
PXS_C_BOT    equ 5                  ; frame's first and last view row
PXS_LSCSZ    equ PX_MAXSPR * PXS_C_SIZE   ; a page's records
PXS_QLAST    equ PXG_QLAST - PX_WPNCOLS * PXS_MAXRUNS * PXG_QSZ
                                    ; the sprites stop here: the weapon's
                                    ; sixteen columns keep their entries

; -----------------------------------------------------------------------------
; PX_TIDX - BX = a cell (y * 64 + x) -> AX = its index in the transposed
;           layout (x * 64 + y; 97.2). Twelve one-bit shifts: a cell is
;           twelve bits, so cell << 2 puts y in AH and x << 2 in AL. clobbers
;           DX (review r1: the first cut shifted by CL = 6 twice, 32 clk each)
; -----------------------------------------------------------------------------
%macro PX_TIDX 0
    mov ax, bx
    shl ax, 1
    shl ax, 1                       ; AH = y, AL = x << 2
    mov dl, ah
    xor dh, dh                      ; DX = y
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; AX = x << 6
    or ax, dx
%endmacro

; -----------------------------------------------------------------------------
; px_spr_gather - after the cast, before the compose: the candidates, sorted
;                 and capped; the match against this page's records; the
;                 erase of the records that no longer stand; the weapon's
;                 frame change
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_spr_gather:
    mov byte [px_nsc], 0
    xor al, al                      ; the page whose glass this frame is
    cmp word [px_pgofs], 0
    je .pg
    inc al
.pg:
    mov [px_spg], al
    ; THE SPRITE SET IS BUILT WHEN A FRAME FIRST WANTS IT for this backend's
    ; ink class (97.6): px_tex_setup builds it beside the wall set for the
    ; Textured rung, and the Flat and Wire rungs - which never call
    ; px_tex_setup - build it here, once, so their silhouettes have run
    ; tables to draw from. ~1.7 s on the 5150, on the first frame with a
    ; candidate in it; nothing when the set or the art was refused
    cmp byte [px_sprok], 0
    jne .built
    mov bl, [px_back]
    xor bh, bh
    mov al, [px_inkcls + bx]
    cmp al, [px_sprback]
    je .built                       ; asked once for this class
    mov [px_sprback], al
    call px_spr_build
.built:
    ; --- the statics: their own cell -------------------------------------
    ; THE PROBES ARE INLINE (review r1): the first cut called px_spot_seen
    ; for every static and nine times for every actor - three push/pop
    ; pairs and two shifts by CL = 6 a call, ~300 clk - and scene A, with
    ; nothing in view, paid ~44,000 clk = 9.2 ms a frame for it. A static's
    ; transposed index is computed once, at load (px_statT); an actor's
    ; once a frame (PX_TIDX, twelve short shifts) and its neighbours' by
    ; the second offset table (px_spr_nbrT)
    mov cl, [px_nstat]
    or cl, cl
    jz .actors
    mov si, px_stat
.st:
    mov al, [si + PXT_KIND]
    cmp al, 0xFF
    je .stn                         ; taken
    mov bx, [si + PXT_CELL]
    mov dl, [px_gen]
    cmp [px_spot + bx], dl
    je .st_seen
    mov bx, si
    sub bx, px_stat
    shr bx, 1                       ; the static's index x 2
    mov bx, [px_statT + bx]
    cmp [px_spotT + bx], dl
    jne .stn
    mov bx, [si + PXT_CELL]
.st_seen:
    push cx
    push si
    mov ah, PXS_PICK0               ; the frame: pickup kind + PXS_PICK0,
    cmp al, PXK_DECO0               ; or decoration kind - 8 + PXS_DECO0
    jb .pk
    sub al, PXK_DECO0
    mov ah, PXS_DECO0
.pk:
    add al, ah
    mov [px_scn + PXS_C_FR], al
    mov byte [px_scn + PXS_C_FL], 0
    mov byte [px_scn + PXS_C_ACT], 0xFF
    mov ax, bx                      ; the cell's centre, Q8.8
    and al, 63
    mov ah, al
    mov al, 128
    mov dx, bx
    mov cl, 6
    shr dx, cl
    mov dh, dl
    mov dl, 128
    call px_spr_add
    pop si
    pop cx
.stn:
    add si, PXT_SIZE
    dec cl
    jnz .st
    ; --- the actors: their own cell or a neighbour ---------------------------
.actors:
    mov cl, [px_nact]
    or cl, cl
    jz .sorted
    mov si, px_act
    xor ch, ch                      ; CH = the actor's index
.ac:
    cmp byte [si + PXAC_STATE], PXAS_NONE
    je .acn
    and byte [si + PXAC_FLAGS], ~PXAF_SEEN
    mov bx, [si + PXAC_CELL]
    PX_TIDX                         ; AX = its cell in the transposed layout
    mov di, ax
    push si
    push cx
    mov cx, bx                      ; CX = its cell
    mov dl, [px_gen]
    cmp [px_spot + bx], dl          ; ITS OWN CELL FIRST, WHATEVER IT IS
    je .ac_seen                     ; (review r1: the first cut put the own
    cmp [px_spotT + di], dl         ; cell through the wall-or-door test
    je .ac_seen                     ; below, so a guard in a doorway was
    mov si, px_spr_nbr + 2          ; never a candidate)
.ac_n:                              ; ...then the eight neighbours that are
    mov bx, cx                      ; open (graft 4)
    add bx, [si]
    test byte [px_map + bx], PXC_SOLID | PXC_DOOR
    jnz .ac_next
    cmp [px_spot + bx], dl
    je .ac_seen
    mov bx, di
    add bx, [si + 18]               ; the same neighbour, transposed
    cmp [px_spotT + bx], dl
    je .ac_seen
.ac_next:
    add si, 2
    cmp si, px_spr_nbr + 18
    jb .ac_n
    pop cx
    pop si
    jmp short .acn
.ac_seen:
    pop cx
    pop si
    or byte [si + PXAC_FLAGS], PXAF_SEEN
    push cx
    push si
    mov [px_scn + PXS_C_ACT], ch
    mov ax, [si + PXAC_X]
    mov dx, [si + PXAC_Y]
    call px_spr_add
    pop si
    pop cx
.acn:
    add si, PXAC_SIZE
    inc ch
    dec cl
    jnz .ac
.sorted:
    ; --- the cap (97.6): the farthest draw every second column past it ------
    mov cl, [px_nsc]
    or cl, cl
    jz .match
    xor ax, ax
    mov si, px_sc
    xor ch, ch
.sum:
    add ax, [si + PXS_C_COST]
    add si, PXS_C_SIZE
    inc ch
    cmp ch, cl
    jb .sum
    mov si, px_sc
.cap:
    cmp ax, PX_SPRCAP
    jbe .match
    or byte [si + PXS_C_FL], PXS_CF_HALF
    mov bx, [si + PXS_C_COST]
    shr bx, 1
    sub ax, bx
    add si, PXS_C_SIZE
    dec cl
    jnz .cap
.match:
    call px_spr_match
    call px_spr_erase
    call px_weapon_check
    ret

; -----------------------------------------------------------------------------
; px_spr_draw - after the compose: the sprites far to near (a standing one
;               only where the compose wrote), the weapon, the record of
;               what stands on this page, the second far call
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_spr_draw:
    mov byte [px_aim], 0xFF
    mov word [px_qp], PXG_Q         ; the queue, emptied for this pass (the
                                    ; compose's walls went through it already)
    mov al, [px_cols]               ; the centre column: the aim's (97.8)
    xor ah, ah
    shr ax, 1
    mov [px_ccen], ax
    call px_weapon_cols             ; the weapon's ray columns, for the
    mov [px_wc0], bx                ; stamp (px_spr_stamp)
    mov [px_wc1], ax
    mov byte [px_wpnhit], 0
    mov cl, [px_nsc]
    or cl, cl
    jz .weapon
    mov si, px_sc
.each:
    push cx
    push si
    cmp byte [px_rung], PXR_TEX
    jne .flat
    cmp byte [px_sprok], 0
    je .flat
    call px_spr_posts
    jmp short .drawn
.flat:
    call px_spr_flat
.drawn:
    pop si
    pop cx
    add si, PXS_C_SIZE
    dec cl
    jnz .each
.weapon:
    call px_weapon_draw
    call px_spr_record
    ; --- THE SECOND FAR CALL (97.3, 97.6): the posts and the weapon ---------
    mov di, [px_qp]
    cmp di, PXG_Q
    je .noq
    mov es, [px_hand + PXH_GEN]
    mov [es:PXG_QEND], di
    push ds
    mov ds, [px_dseg]
    call far [cs:px_drv2p]
    pop ds
.noq:
    ret

; px_spr_recs - BX = this page's records, AL = their count, DI = the count's
;               address. clobbers AH
px_spr_recs:
    mov bx, px_lsc
    mov di, px_lnsc
    cmp byte [px_spg], 0
    je .p0
    add bx, PXS_LSCSZ
    inc di
.p0:
    mov al, [di]
    ret

; -----------------------------------------------------------------------------
; px_spr_match - every candidate against this page's records: the same h,
;                first column, width, frame, mirror and actor is a sprite
;                that STANDS (PXS_CF_STAND on the candidate, PXS_CF_KEPT on
;                the record). clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
px_spr_match:
    call px_spr_recs
    mov dl, al                      ; DL = the records
    mov cl, [px_nsc]
    or cl, cl
    jz .out
    mov si, px_sc
.cand:
    mov ch, dl
    or ch, ch
    jz .cnext
    mov di, bx
.old:
    test byte [di + PXS_C_FL], PXS_CF_PART
    jnz .onext                      ; cut short by a full queue: never whole
    mov ax, [si + PXS_C_H]
    cmp ax, [di + PXS_C_H]
    jne .onext
    mov ax, [si + PXS_C_C0]
    cmp ax, [di + PXS_C_C0]
    jne .onext
    mov al, [si + PXS_C_W]
    cmp al, [di + PXS_C_W]
    jne .onext
    mov al, [si + PXS_C_FR]
    cmp al, [di + PXS_C_FR]
    jne .onext
    mov al, [si + PXS_C_ACT]
    cmp al, [di + PXS_C_ACT]
    jne .onext
    mov al, [si + PXS_C_FL]
    xor al, [di + PXS_C_FL]
    test al, PXS_CF_MIR | PXS_CF_HALF
    jnz .onext
    or byte [si + PXS_C_FL], PXS_CF_STAND
    or byte [di + PXS_C_FL], PXS_CF_KEPT
    jmp short .cnext
.onext:
    add di, PXS_C_SIZE
    dec ch
    jnz .old
.cnext:
    add si, PXS_C_SIZE
    dec cl
    jnz .cand
.out:
    ret

; -----------------------------------------------------------------------------
; px_spr_erase - every record of this page that no candidate matched is
;                erased: its columns px_touched over its rows, so the
;                compose that follows re-lays them. clobbers AX, BX, CX,
;                DX, DI
; -----------------------------------------------------------------------------
px_spr_erase:
    call px_spr_recs
    mov di, bx
    mov dl, al
    or dl, dl
    jz .out
.rec:
    test byte [di + PXS_C_FL], PXS_CF_KEPT
    jnz .rnext
    mov cx, [di + PXS_C_C0]         ; [max(c0, 0), min(c0 + w, cols))
    mov al, [di + PXS_C_W]
    xor ah, ah
    mov bx, cx
    add bx, ax
    mov al, [px_cols]
    cmp bx, ax
    jle .e1
    mov bx, ax
.e1:
    or cx, cx
    jns .s1
    xor cx, cx
.s1:
    cmp cx, bx
    jge .rnext
.col:
    push bx
    mov bx, cx
    mov al, [di + PXS_C_TOP]
    mov ah, [di + PXS_C_BOT]
    call px_touch
    pop bx
    inc cx
    cmp cx, bx
    jl .col
.rnext:
    add di, PXS_C_SIZE
    dec dl
    jnz .rec
.out:
    ret

; -----------------------------------------------------------------------------
; px_spr_record - this page's records become this frame's candidates that
;                 reached the glass (PXS_CF_GLASS): the list as drawn, the
;                 rows of the frame's whole extent in the cost word, the
;                 flags reduced to the mirror and the cap.
; clobbers AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
px_spr_record:
    call px_spr_recs
    push di                         ; the count's address
    mov di, bx
    xor dh, dh                      ; DH = records written
    mov cl, [px_nsc]
    or cl, cl
    jz .done
    mov si, px_sc
.c:
    test byte [si + PXS_C_FL], PXS_CF_GLASS
    jz .cnext
    push cx
    push si
    push di
    call px_spr_move                ; the twelve bytes (DI advances)
    pop di
    pop si
    pop cx
    and byte [di + PXS_C_FL], PXS_CF_MIR | PXS_CF_HALF | PXS_CF_PART
    mov ax, [si + PXS_C_H]          ; the rows: top = (80 - hq) / 2, signed,
    cmp ax, PXG_HMAX                ; bot = top + hq - 1, both clipped
    jbe .hq
    mov ax, PXG_HMAX
.hq:
    mov bx, ax
    mov al, [px_hq + bx]
    xor ah, ah
    mov bx, PX_ROWS
    sub bx, ax
    sar bx, 1                       ; BX = top, AX = hq
    add ax, bx
    dec ax                          ; AX = bot
    or bx, bx
    jns .t
    xor bx, bx
.t:
    cmp ax, PX_ROWS - 1
    jle .b
    mov ax, PX_ROWS - 1
.b:
    mov [di + PXS_C_TOP], bl
    mov [di + PXS_C_BOT], al
    add di, PXS_C_SIZE
    inc dh
.cnext:
    add si, PXS_C_SIZE
    dec cl
    jnz .c
.done:
    pop di
    mov [di], dh
    ret

; -----------------------------------------------------------------------------
; px_spot_seen - BX = a cell: CF = 1 if a ray crossed it this frame, in either
;                layout (97.2). Preserves every register. Off the gather's
;                path since review r1 (the gather probes inline, above): the
;                sight test, the dirty rules (px_act_dirty, px_door_dirty)
; -----------------------------------------------------------------------------
px_spot_seen:
    push ax
    push dx
    mov al, [px_gen]
    cmp [px_spot + bx], al
    je .yes
    PX_TIDX                         ; AX = the transposed layout's index
    push bx
    mov bx, ax
    mov al, [px_gen]
    cmp [px_spotT + bx], al
    pop bx
    je .yes
    pop dx
    pop ax
    clc
    ret
.yes:
    pop dx
    pop ax
    stc
    ret

; px_spot_seen9 - BX = an actor's cell: CF = 1 if it, whatever it is (a
;                 doorway too), or any of its eight neighbours that is open
;                 was crossed (graft 4: a guard beside a corner stands in a
;                 cell no ray entered while half of it hangs into the one
;                 that was). The gather's probe (above), as a routine: the
;                 transposed index once, the neighbours by both offset
;                 tables - it is on every chaser's tick now (px_act_dirty).
;                 Preserves every register
px_spot_seen9:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    PX_TIDX                         ; AX = the cell, transposed
    mov di, ax
    mov cx, bx                      ; CX = the cell
    mov dl, [px_gen]
    cmp [px_spot + bx], dl          ; its own cell first, whatever it is
    je .yes
    cmp [px_spotT + di], dl
    je .yes
    mov si, px_spr_nbr + 2
.n:
    mov bx, cx
    add bx, [si]
    test byte [px_map + bx], PXC_SOLID | PXC_DOOR
    jnz .next                       ; a wall or a door: not through it
    cmp [px_spot + bx], dl
    je .yes
    mov bx, di
    add bx, [si + 18]
    cmp [px_spotT + bx], dl
    je .yes
.next:
    add si, 2
    cmp si, px_spr_nbr + 18
    jb .n
    clc
    jmp short .out
.yes:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

px_spr_nbr:  dw 0, -1, 1, -64, 64, -65, -63, 63, 65     ; the plain layout...
px_spr_nbrT: dw 0, -64, 64, -1, 1, -65, 63, -63, 65     ; ...and the same
                                    ; eight in the transposed one: 18 bytes
                                    ; after (the gather's [si + 18])

; -----------------------------------------------------------------------------
; px_spr_add - a candidate at AX, DX (Q8.8) with px_scn's frame, flags and
;              actor filled (an actor's frame is decided here, since its
;              facing wants the column): transform, price, insert sorted
; clobbers AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
px_spr_add:
    call px_spr_xform
    jc .out
    cmp byte [px_scn + PXS_C_ACT], 0xFF
    je .framed
    call px_act_frame               ; pxact.inc: the frame and the mirror
.framed:
    ; the quantised height, the width, the first column, the cost
    mov ax, [px_scn + PXS_C_H]
    cmp ax, PXG_HMAX
    jbe .hq
    mov ax, PXG_HMAX
.hq:
    mov bx, ax
    mov al, [px_hq + bx]
    xor ah, ah                      ; AX = hq
    push ax
    mov cx, 71                      ; w = (hq * 71 + 128) >> 8, at Low res
    mul cx                          ; (hq * 71 + 256) >> 9, at least 1
    add ax, 128                     ; (tools/pxsgen.py's width())
    cmp byte [px_bpc], 1
    je .wf
    add ax, 128
    shr ax, 1
.wf:
    mov al, ah
    xor ah, ah
    or al, al
    jnz .w1
    inc al
.w1:
    mov [px_scn + PXS_C_W], al
    mov bx, ax
    shr bx, 1
    mov cx, [px_scn + PXS_C_C]
    sub cx, bx                      ; c0 = c - w / 2
    mov [px_scn + PXS_C_C0], cx
    ; the visible columns: [max(c0, 0), min(c0 + w, cols))
    mov bx, cx
    add bx, ax                      ; BX = c0 + w
    mov al, [px_cols]
    xor ah, ah
    cmp bx, ax
    jle .e1
    mov bx, ax
.e1:
    or cx, cx
    jns .s1
    xor cx, cx
.s1:
    sub bx, cx                      ; BX = the visible columns, or <= 0
    pop ax                          ; AX = hq
    jle .out                        ; nothing of it on the view
    cmp ax, PX_ROWS
    jbe .rows
    mov ax, PX_ROWS
.rows:
    mul bx                          ; the cost: columns x rows
    mov [px_scn + PXS_C_COST], ax
    ; --- insert, far (index 0) to near --------------------------------------
    mov ax, [px_scn + PXS_C_H]
    mov cl, [px_nsc]
    xor ch, ch
    cmp cl, PX_MAXSPR
    jb .room
    cmp ax, [px_sc + PXS_C_H]       ; full: farther than the farthest is
    jbe .out                        ; dropped, else the farthest goes
    xor bx, bx                      ; shift [1..i) down while sc[i+1].h <= h
.dn:                                ; (a tie goes AFTER its equals, as the
    cmp bx, (PX_MAXSPR - 1) * PXS_C_SIZE    ; non-full insert and the host's
    jae .put                        ; bisect_right put it; review, wave 3)
    cmp [px_sc + PXS_C_SIZE + bx + PXS_C_H], ax
    ja .put
    lea si, [px_sc + PXS_C_SIZE + bx]
    lea di, [px_sc + bx]
    call px_spr_move
    add bx, PXS_C_SIZE
    jmp short .dn
.room:
    inc byte [px_nsc]
    mov bx, cx
    shl bx, 1
    shl bx, 1
    mov si, bx
    shl bx, 1
    add bx, si                      ; BX = n * 12
.up:                                ; shift up while sc[i-1].h > h
    or bx, bx
    jz .put
    cmp [px_sc - PXS_C_SIZE + bx + PXS_C_H], ax
    jbe .put
    lea si, [px_sc - PXS_C_SIZE + bx]
    lea di, [px_sc + bx]
    call px_spr_move
    sub bx, PXS_C_SIZE
    jmp short .up
.put:
    mov si, px_scn
    lea di, [px_sc + bx]
    call px_spr_move
.out:
    ret

; px_spr_move - the 12-byte record at SI to DI (both ours). clobbers AX, SI,
;               DI, ES
px_spr_move:
    push cx
    push ds
    pop es
    mov cx, PXS_C_SIZE / 2
    cld
    rep movsw
    pop cx
    ret

; -----------------------------------------------------------------------------
; px_spr_xform - AX, DX = a point (Q8.8) -> px_scn's h and column, or CF = 1
; clobbers AX, BX, CX, DX
; -----------------------------------------------------------------------------
px_spr_xform:
    sub ax, [px_px]
    mov [px_sdx], ax
    sub dx, [px_py]
    mov [px_sdy], dx
    mov bx, [px_hcos]               ; nx = dx cos + dy sin
    PX_MUL14
    mov cx, ax
    mov ax, [px_sdy]
    mov bx, [px_hsin]
    PX_MUL14
    add ax, cx
    cmp ax, PX_MINDIST
    jl .skip
    mov [px_snx], ax
    mov ax, [px_sdy]                ; ny = dy cos - dx sin: to the right
    mov bx, [px_hcos]
    PX_MUL14
    mov cx, ax
    mov ax, [px_sdx]
    mov bx, [px_hsin]
    PX_MUL14
    sub cx, ax
    mov ax, cx                      ; |ny| < nx, or it is off every view
    cwd
    xor ax, dx
    sub ax, dx
    cmp ax, [px_snx]
    jae .skip
    mov ax, PX_HEIGHTK
    xor dx, dx
    div word [px_snx]               ; h, the true height
    mov [px_scn + PXS_C_H], ax
    mov ax, cx
    mov bx, PX_FOCAL
    imul bx                         ; ny * focal
    idiv word [px_snx]              ; / nx: device pixels off the axis,
                                    ; |ax| < PX_FOCAL by the test above
    mov cl, 2                       ; ...four a column at Full, eight at
    cmp byte [px_bpc], 1            ; Low res
    je .sh
    inc cl
.sh:
    sar ax, cl
    mov bl, [px_cols]
    xor bh, bh
    shr bx, 1
    add ax, bx
    mov [px_scn + PXS_C_C], ax
    clc
    ret
.skip:
    stc
    ret

; -----------------------------------------------------------------------------
; px_spr_setup - the draw's per-sprite words from the candidate at SI: the
;                frame's base in the sprite set, the scaler's codeofs table
;                and the col2tex block for its quantised height, its top
;                row, its first and last visible column (CX .. BX exclusive)
; out: CF = 1 nothing visible. clobbers AX, BX, CX, DX, ES
; -----------------------------------------------------------------------------
px_spr_setup:
    mov ax, [si + PXS_C_H]
    cmp ax, PXG_HMAX
    jbe .hq
    mov ax, PXG_HMAX
.hq:
    mov bx, ax
    mov al, [px_hq + bx]
    mov [px_shq], al
    xor ah, ah
    mov bx, ax
    shl bx, 1
    push bx
    add bx, [px_gsc]                ; this resolution's scaler directory
    mov ax, [bx]
    sub ax, PXG_COTSZ
    mov [px_scot], ax               ; the codeofs table before it
    pop bx
    add bx, [px_gc2t]               ; ...and the col2tex directory
    mov ax, [bx]
    mov [px_sc2t], ax
    mov al, [px_shq]
    xor ah, ah
    mov bx, PX_ROWS
    sub bx, ax
    sar bx, 1
    mov [px_stop], bx               ; top, signed
    call px_spr_wpnrows             ; ...and whether it reaches the weapon's
    mov al, [si + PXS_C_FR]
    xor ah, ah
    mov bx, PXS_FRSZ
    mul bx
    mov [px_sfr], ax                ; the frame's base in the set
    mov al, [si + PXS_C_FL]
    mov [px_sflags], al
    mov cx, [si + PXS_C_C0]
    mov bl, [si + PXS_C_W]
    xor bh, bh
    add bx, cx                      ; BX = one past the last column
    mov al, [px_cols]
    xor ah, ah
    cmp bx, ax
    jle .e1
    mov bx, ax
.e1:
    or cx, cx
    jns .s1
    xor cx, cx
.s1:
    cmp cx, bx
    jge .none
    clc
    ret
.none:
    stc
    ret

; px_spr_wpnrows - does the sprite set up (px_stop, px_shq) reach the
;                  weapon's rows? px_swpn = 1 when its last row, top + hq - 1,
;                  is at or under PX_WPNROW0. clobbers AX
px_spr_wpnrows:
    mov al, [px_shq]
    xor ah, ah
    add ax, [px_stop]               ; one past its last row, signed
    mov byte [px_swpn], 0
    cmp ax, PX_WPNROW0 + 1
    jl .out
    mov byte [px_swpn], 1
.out:
    ret

; px_spr_aim - column CX of the sprite at SI has something in it: if it is
;              the centre column the hitscan's aim is this sprite (97.8: the
;              last sprite at the centre is the nearest there, standing or
;              drawn). clobbers AX
px_spr_aim:
    cmp cx, [px_ccen]
    jne .out
    mov al, [si + PXS_C_ACT]
    mov [px_aim], al
    mov ax, [si + PXS_C_H]
    mov [px_aimh], ax
.out:
    ret

; px_spr_stamp - column CX of a sprite is DRAWN this frame (review r1): a
;                nearer STANDING sprite is redrawn over it (px_swrote, read
;                by px_spr_stands), and the weapon is redrawn when the
;                column is one of its own and the sprite reaches its rows
;                (px_wpnhit) - a far sprite crossing the centre above row
;                56 touched none of the weapon's pixels. clobbers AX, BX
px_spr_stamp:
    mov bx, cx
    mov al, [px_gen]
    mov [px_swrote + bx], al
    cmp byte [px_swpn], 0
    je .out
    cmp bx, [px_wc0]
    jb .out
    cmp bx, [px_wc1]
    jae .out
    mov byte [px_wpnhit], 1
.out:
    ret

; px_spr_src - the source column of screen column CX for the sprite set up:
;              col2tex[c - c0], mirrored, paired on C160 -> px_ssrc; its
;              texel column's base in the set -> px_ssi (once a column: the
;              first cut multiplied it out for every run, review, wave 3);
;              and its DI -> px_sdi. clobbers AX, BX, ES
px_spr_src:
    mov es, [px_hand + PXH_GEN]
    mov bx, cx
    sub bx, [si + PXS_C_C0]         ; j
    add bx, [px_sc2t]
    mov al, [es:bx + 1]             ; col2tex[j]
    test byte [px_sflags], PXS_CF_MIR
    jz .nomir
    xor al, PXA_TEX - 1             ; 31 - the column
.nomir:
    cmp byte [px_back], PXB_C160
    jne .nopair
    shr al, 1
.nopair:
    mov [px_ssrc], al
    mov ah, PXA_TEX
    mul ah
    add ax, [px_sfr]
    mov [px_ssi], ax                ; SI for the scaler: frame + column * 32
    mov ax, cx                      ; DI = the column's base byte
    cmp byte [px_bpc], 1
    je .b1
    shl ax, 1
.b1:
    add ax, [px_cbase]
    add ax, 256
    mov [px_sdi], ax
    ret

; px_spr_runs - the run table of source column [px_ssrc] of the frame at
;               [px_sfr] into px_sruns (PXS_RUNSZ bytes). clobbers AX, BX, ES
px_spr_runs:
    mov es, [px_hand + PXH_SPR]
    mov al, [px_ssrc]
    mov ah, PXS_RUNSZ
    mul ah
    mov bx, ax
    add bx, [px_sfr]
    add bx, PXS_RUNOFS
    mov ax, [es:bx]
    mov [px_sruns], ax
    mov ax, [es:bx + 2]
    mov [px_sruns + 2], ax
    mov ax, [es:bx + 4]
    mov [px_sruns + 4], ax
    mov ax, [es:bx + 6]
    mov [px_sruns + 6], ax
    mov al, [es:bx + 8]
    mov [px_sruns + 8], al
    cmp byte [px_sruns], PXS_MAXRUNS    ; a floppy's byte: bounded
    jbe .ok
    mov byte [px_sruns], PXS_MAXRUNS
.ok:
    ret

; px_spr_rows - AL = a texel v -> AX = its view row, top + (v * hq) >> 5,
;               signed (the caller clips). clobbers AX, DX
px_spr_rows:
    xor ah, ah
    mov dl, [px_shq]
    mul dl
    shr ax, 1                       ; >> 5 without touching CL, which the
    shr ax, 1                       ; callers keep a count in
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add ax, [px_stop]
    ret

; px_spr_stands - a column of a STANDING sprite is drawn only where the
;                 compose re-laid it this frame, or where a farther sprite
;                 (drawn before it, the pass being far to near) drew over it
;                 (px_swrote; review r1 - without it a guard walking behind
;                 a barrel showed through it): CX = the column, CF = 1 skip
;                 it. clobbers AX, BX
px_spr_stands:
    test byte [px_sflags], PXS_CF_STAND
    jz .draw
    mov bx, cx
    mov al, [px_gen]
    cmp [px_cwrote + bx], al
    je .draw
    cmp [px_swrote + bx], al
    je .draw
    stc
    ret
.draw:
    clc
    ret

; -----------------------------------------------------------------------------
; px_spr_posts - the sprite at SI as posts into the draw queue (97.6)
; clobbers everything but DS, SI
; -----------------------------------------------------------------------------
px_spr_posts:
    call px_spr_setup
    jc .out
.col:                               ; CX = the column, BX = the end
    push bx
    push cx
    test byte [px_sflags], PXS_CF_HALF  ; the cap: every second column
    jz .whole
    mov ax, cx
    sub ax, [si + PXS_C_C0]
    test al, 1
    jnz .next
.whole:
    mov bx, cx
    shl bx, 1
    mov ax, [si + PXS_C_H]
    cmp [px_wallh + bx], ax         ; THE Z-TEST (97.6): the wall's true
    jae .next                       ; height against the sprite's
    or byte [si + PXS_C_FL], PXS_CF_GLASS
    ; a standing sprite's column that nothing re-laid is skipped BEFORE its
    ; source and runs are looked up (review r1: an 8-bit mul and a nine-byte
    ; copy a column, ~1.2 ms on a twenty-column sprite) - except the centre
    ; column, whose runs the aim needs whatever stands
    cmp cx, [px_ccen]
    je .runs
    call px_spr_stands
    jc .next
.runs:
    call px_spr_src
    call px_spr_runs
    cmp byte [px_sruns], 0
    je .next
    call px_spr_aim                 ; the aim (97.8), at the centre column
    call px_spr_stands              ; standing: only where something wrote
    jc .next
    call px_spr_stamp               ; drawn: nearer standing sprites and the
                                    ; weapon redraw over it (review r1)
    mov cl, [px_sruns]
    xor ch, ch
    mov bx, px_sruns + 1
    mov es, [px_hand + PXH_GEN]
.run:
    mov di, [px_qp]
    cmp di, PXS_QLAST
    jb .room
    or byte [si + PXS_C_FL], PXS_CF_PART
    jmp short .widen                ; the queue is full: what is queued is
                                    ; drawn, and the rows are still owed
                                    ; to the present (the cap keeps it rare)
.room:
    mov al, [bx]                    ; v0
    xor ah, ah
    push bx
    mov bx, ax
    shl bx, 1
    add bx, [px_scot]
    mov ax, [es:bx]                 ; codeofs[v0]
    pop bx
    or ax, ax
    jz .rnext                       ; no texel from v0 on has a row
    mov [es:di], ax                 ; the entry
    mov ax, [px_ssi]
    mov [es:di + 2], ax             ; SI: the texel column
    mov ax, [px_sdi]
    mov [es:di + 4], ax             ; DI: the screen column
    mov al, [bx + 1]                ; v1
    xor ah, ah
    cmp al, PXA_TEX
    jb .cot1
    xor ax, ax                      ; v1 = 32: no patch, the scaler's own
    jmp short .nopatch              ; ret ends it (the first cut stored the
                                    ; 32 and the driver wrote a ret into
                                    ; body 0's H pass, 97.2.1's offset 32)
.cot1:
    push bx
    mov bx, ax
    shl bx, 1
    add bx, [px_scot]
    mov ax, [es:bx]                 ; codeofs[v1]: the patch
    pop bx
.nopatch:
    mov [es:di + 6], ax
    add di, PXG_QSZ
    mov [px_qp], di
.rnext:
    inc bx
    inc bx
    inc ch
    cmp ch, cl
    jb .run
.widen:
    ; the present's rows: from the first run's v0 to the last run's v1
    mov al, [px_sruns + 1]
    call px_spr_rows
    or ax, ax
    jns .r0
    xor ax, ax
.r0:
    push ax
    mov bl, [px_sruns]              ; the count (CL is spent by now)
    xor bh, bh
    shl bx, 1
    mov al, [px_sruns + bx]         ; the last run's v1
    call px_spr_rows
    dec ax
    cmp ax, PX_ROWS - 1
    jle .r1
    mov ax, PX_ROWS - 1
.r1:
    pop bx
    mov ah, al
    mov al, bl                      ; AL = top, AH = bot
    cmp al, ah
    ja .next
    call px_rows_widen
.next:
    pop cx
    pop bx
    inc cx
    cmp cx, bx
    jl .col
.out:
    ret

; -----------------------------------------------------------------------------
; px_spr_flat - the sprite at SI as a silhouette (its runs, one tone) through
;               the ladder, or a box when there is no run table (97.6).
;               DS switches to the destination for the ladder calls
; clobbers everything but DS, SI
; -----------------------------------------------------------------------------
px_spr_flat:
    mov ax, [si + PXS_C_H]          ; hq, top, the columns - without the
    cmp ax, PXG_HMAX                ; scaler tables (the Textured rung may
    jbe .hq                         ; never have been built)
    mov ax, PXG_HMAX
.hq:
    mov bx, ax
    mov al, [px_hq + bx]
    mov [px_shq], al
    xor ah, ah
    mov bx, PX_ROWS
    sub bx, ax
    sar bx, 1
    mov [px_stop], bx
    call px_spr_wpnrows
    mov al, [si + PXS_C_FR]
    xor ah, ah
    mov bx, PXS_FRSZ
    mul bx
    mov [px_sfr], ax
    mov al, [si + PXS_C_FL]
    mov [px_sflags], al
    mov cx, [si + PXS_C_C0]
    mov bl, [si + PXS_C_W]
    xor bh, bh
    add bx, cx
    mov al, [px_cols]
    xor ah, ah
    cmp bx, ax
    jle .e1
    mov bx, ax
.e1:
    or cx, cx
    jns .s1
    xor cx, cx
.s1:
    cmp cx, bx
    jge .out
.col:
    push bx
    push cx
    test byte [px_sflags], PXS_CF_HALF
    jz .whole
    mov ax, cx
    sub ax, [si + PXS_C_C0]
    test al, 1
    jnz .next
.whole:
    mov bx, cx
    shl bx, 1
    mov ax, [si + PXS_C_H]
    cmp [px_wallh + bx], ax
    jae .next
    or byte [si + PXS_C_FL], PXS_CF_GLASS
    cmp byte [px_sprok], 0
    je .boxaim
    ; the runs FIRST, then the aim (review r1: the first cut aimed before it
    ; read the runs, so a nearer static whose centre column is empty took a
    ; guard's aim under Flat and Wire and the shot missed there and hit
    ; under Textured): the source column without col2tex (w is the
    ; formula's), j * 32 / w by a mul and a div once a column - cold beside
    ; the posts
    mov ax, cx
    sub ax, [si + PXS_C_C0]
    mov dl, PXA_TEX
    mul dl
    mov dl, [si + PXS_C_W]
    div dl                          ; AL = (j * 32) / w
    test byte [px_sflags], PXS_CF_MIR
    jz .nomir
    xor al, PXA_TEX - 1
.nomir:
    cmp byte [px_back], PXB_C160
    jne .nopair
    shr al, 1
.nopair:
    mov [px_ssrc], al
    call px_spr_runs
    cmp byte [px_sruns], 0
    je .next                        ; nothing in this column: no aim, no draw
    call px_spr_aim
    call px_spr_stands              ; standing: only where something wrote
    jc .next
    call px_spr_stamp
    call px_spr_cdi
    mov cl, [px_sruns]
    xor ch, ch
    mov bx, px_sruns + 1
    mov byte [px_srow0], 0xFF
.run:
    push bx
    push cx
    mov al, [bx]
    call px_spr_rows
    or ax, ax
    jns .r0
    xor ax, ax
.r0:
    mov dx, ax                      ; DL = r0
    pop cx
    pop bx
    push bx
    push cx
    push dx
    mov al, [bx + 1]
    call px_spr_rows
    dec ax
    cmp ax, PX_ROWS - 1
    jle .r1
    mov ax, PX_ROWS - 1
.r1:
    pop dx
    pop cx
    pop bx
    cmp ax, dx
    jl .rnext                       ; nothing of it on the view
    cmp byte [px_srow0], 0xFF
    jne .r0k
    mov [px_srow0], dl              ; the first run's top...
.r0k:
    mov [px_srow1], al              ; ...and the last's bottom
    push bx
    push cx
    push si                         ; (the ladder clobbers SI, the record)
    mov cx, ax
    sub cx, dx
    inc cx                          ; CX = the rows
    mov al, dl
    mov bx, [px_inks]
    call px_spr_lad
    pop si
    pop cx
    pop bx
.rnext:
    inc bx
    inc bx
    inc ch
    cmp ch, cl
    jb .run
    cmp byte [px_srow0], 0xFF
    je .next
    mov al, [px_srow0]
    mov ah, [px_srow1]
    jmp short .widen
.boxaim:                            ; no run table: the aim as the box's
    call px_spr_aim                 ; whole extent, then the box
    call px_spr_stands
    jc .next
    call px_spr_stamp
    call px_spr_cdi
    mov ax, [px_stop]               ; the frame's whole extent
    or ax, ax
    jns .b0
    xor ax, ax
.b0:
    mov dx, ax
    mov al, [px_shq]
    xor ah, ah
    add ax, [px_stop]
    dec ax
    cmp ax, PX_ROWS - 1
    jle .b2
    mov ax, PX_ROWS - 1
.b2:
    mov cx, ax
    sub cx, dx
    inc cx
    push ax
    push dx
    push si
    mov al, dl
    mov bx, [px_inks]
    call px_spr_lad
    pop si
    pop dx
    pop ax
    mov ah, al
    mov al, dl
.widen:
    call px_rows_widen              ; the present sends its rows
.next:
    pop cx
    pop bx
    inc cx
    cmp cx, bx
    jl .col
.out:
    ret

; px_spr_cdi - column CX's base for px_lad: px_cdi = the frame's column base
;              (px_cbase: the page's base and the band's first byte less
;              256) plus a byte a column at Full and two at Low res.
;              clobbers AX
px_spr_cdi:
    mov ax, cx
    cmp byte [px_bpc], 1
    je .b1
    shl ax, 1
.b1:
    add ax, [px_cbase]
    mov [px_cdi], ax
    ret

; px_spr_lad - px_lad with DS = the destination: AL = the first row, CX = the
;              count, BX = the tone pair, [px_cdi] set. clobbers AX, BX, DX,
;              SI, DI
px_spr_lad:
    push ds
    mov ds, [px_dseg]
    call px_lad
    pop ds
    ret

; -----------------------------------------------------------------------------
; THE WEAPON (97.6): px_weapon_check at the gather names the frame this page
; is to show (px_wnext: weapon x 3 + px_wframe, the knife's when the ammo is
; out, 0xFF for none) and, when the page's glass holds ANOTHER frame,
; touches the sixteen columns over rows 56..79 so the compose re-lays under
; it; px_weapon_draw after the compose draws the frame when the page holds
; none yet or when any of its columns was re-laid this frame - which the
; erase above guarantees for a change, and which a wall moving under it
; asks for on its own. The page's frame is banked in px_wdrawn[page]. The
; first cut of this wave touched the sixteen columns on every drawn frame
; (+55 ms on a still windowed Flat Full frame, tests/pxsauto.py's control),
; the second drew on the whole frame's dirty range - a sprite at the view's
; edge redrew it, and a frame change over columns nobody re-laid ghosted
; the old frame under the new - and walked both pages with a loop that
; never ended when entered on Mode X's second page (review, wave 3)
; -----------------------------------------------------------------------------
px_weapon_check:
    mov byte [px_wtouch], 0         ; (97.6, wave 5: nothing erased yet)
    mov al, 0xFF
    cmp byte [px_sprok], 0
    je .set
    cmp byte [px_state], PXST_PLAY
    je .play
    cmp byte [px_state], PXST_DEMO  ; THE TIMEDEMO DRAWS IT TOO (review r2):
    jne .set                        ; without it the demo's turn frame was
.play:                              ; 5 ms of compose lighter than the one a
    mov al, [px_weapon]             ; player sees, and its fps flattered
    cmp byte [px_ammo], 0
    jne .have
    xor al, al                      ; no rounds: the knife
.have:
    mov ah, 3
    mul ah
    add al, [px_wframe]
.set:
    mov [px_wnext], al
    mov bl, [px_spg]
    xor bh, bh
    mov ah, [px_wdrawn + bx]
    cmp ah, 0xFF
    je .out                         ; nothing of it on this page's glass
    cmp al, ah
    je .out                         ; the same frame stands
    mov byte [px_wtouch], 1         ; another: erased, re-laid, drawn over -
    call px_weapon_touch            ; and DRAWN whatever the compose re-laid
.out:                               ; (px_weapon_draw)
    ret

; px_weapon_cols - BX = the weapon's first RAY column, AX = one past its last
;                  (its sixteen byte columns at the band's middle, a byte a
;                  column at Full and two at Low res). clobbers AX, BX
px_weapon_cols:
    mov al, [px_size]
    xor ah, ah
    shr ax, 1
    sub ax, PX_WPNCOLS / 2          ; the first byte column
    mov bx, ax
    add ax, PX_WPNCOLS              ; ...and past the last
    cmp byte [px_bpc], 1
    je .out
    shr bx, 1
    shr ax, 1
.out:
    ret

; px_weapon_touch - the columns under the weapon on THIS page erased over
;                   rows 56..79. A column whose wall ends above row 56 (the
;                   page's memory, px_lbot) has only GROUND under the
;                   weapon, so the ground is laid back over those rows here
;                   - the floor tone through the ladder, the rows widened -
;                   and its memory is left alone (review r1: px_touch on it
;                   forgot the wall's tone, and the Textured arm then
;                   re-scaled eight unchanged walls and presented the rows
;                   from their tops, ~5-6 ms a weapon frame). A column whose
;                   wall reaches the weapon's rows, and every column under
;                   Wire (whose ground is not a tone), goes through px_touch
;                   as before, and the compose re-lays it. The frame's page
;                   base is summed here: px_cbase is the COMPOSE's, and this
;                   runs before it
;                   clobbers AX, BX, CX, DX, SI, DI
px_weapon_touch:
    call px_weapon_cols
.tc:
    push ax
    push bx
    mov si, bx
    add si, [px_pgofs]              ; this page's memory
    cmp byte [px_rung], PXR_WIRE
    je .whole
    cmp byte [px_lbot + si], PX_WPNROW0
    jae .whole
    mov ax, bx                      ; the ground alone: the column's base
    cmp byte [px_bpc], 1            ; for px_lad, this page's
    je .b1
    shl ax, 1
.b1:
    add ax, [px_dbase]
    add ax, [px_cbias]
    mov [px_cdi], ax
    mov al, PX_WPNROW0
    mov cx, PX_ROWS - PX_WPNROW0
    mov bx, [px_inkf]
    call px_spr_lad
    mov al, PX_WPNROW0
    mov ah, PX_ROWS - 1
    call px_rows_widen
    jmp short .tn
.whole:
    mov al, PX_WPNROW0
    mov ah, PX_ROWS - 1
    call px_touch
.tn:
    pop bx
    pop ax
    inc bx
    cmp bx, ax
    jb .tc
    ret

; -----------------------------------------------------------------------------
; px_weapon_draw - the weapon's frame over the bottom of the view (97.6):
;                  sixteen posts through the Full set's 24-row scaler, no
;                  depth test; a silhouette under the Flat and Wire rungs;
;                  nothing without the sprite set
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_weapon_draw:
    mov al, [px_wnext]
    cmp al, 0xFF
    je .out
    mov bl, [px_spg]
    xor bh, bh
    cmp byte [px_wdrawn + bx], 0xFF
    je .draw                        ; nothing on this page yet: drawn
    cmp byte [px_wtouch], 0         ; THE CHECK ERASED ANOTHER FRAME (wave 5,
    jne .draw                       ; the defect wave 4's verifier found on
                                    ; Mode X): px_weapon_touch lays the
                                    ; ground back itself over a column whose
                                    ; wall ends above row 56 and does NOT
                                    ; mark px_cwrote - so with every column
                                    ; that shape (a still eye in a hall) the
                                    ; test below found nothing re-laid and
                                    ; the new frame was never drawn: the
                                    ; weapon erased, and gone until the eye
                                    ; moved
    cmp byte [px_wpnhit], 0         ; a sprite drawn over its rows this
    jne .draw                       ; frame (px_spr_stamp; review r1)
    call px_weapon_cols             ; else only over a column re-laid
    mov cl, [px_gen]
.ck:
    cmp [px_cwrote + bx], cl
    je .draw
    inc bx
    cmp bx, ax
    jb .ck
    ret
.draw:
    mov al, [px_wnext]
    mov bl, [px_spg]
    xor bh, bh
    mov [px_wdrawn + bx], al
    cmp byte [px_back], PXB_MODEX   ; ...AND THE OTHER PAGE CATCHES UP (wave
    jne .one                        ; 5): a shot's three frames land on the
    cmp al, [px_wdrawn + 1]         ; two pages in turn, so when the eye
    jne .owe                        ; stops the page not drawn last keeps
    cmp al, [px_wdrawn]             ; the fire or the recoil frame - shown
    je .one                         ; by the next flip, which a bar change
.owe:                               ; brings without the eye moving. One
    mov byte [px_dirty], 1          ; frame more is owed, on the other page:
.one:                               ; its check erases, this draws, and the
                                    ; pages agree (px_dirty alone, not
                                    ; px_vcatch: the pose may move before it)
    xor ah, ah                      ; the frame's base in the set
    mov bx, PXS_WFRSZ
    mul bx
    add ax, PXS_WPN0
    mov [px_sfr], ax
    mov word [px_stop], (PX_ROWS - PX_WPNH) / 2 + (PX_WPNROW0 - (PX_ROWS - PX_WPNH) / 2)
    mov byte [px_shq], PX_WPNH
    mov bx, PX_WPNH * 2
    add bx, px_sctab                ; the FULL set's directory, whichever
    mov ax, [bx]                    ; resolution is in force: a byte a column
    sub ax, PXG_COTSZ
    mov [px_scot], ax
    mov byte [px_sflags], 0
    ; the first byte column: the band's middle less eight
    mov al, [px_size]
    xor ah, ah
    shr ax, 1
    sub ax, PX_WPNCOLS / 2
    add ax, [px_cbase]
    add ax, 256 + (PX_WPNROW0 - (PX_ROWS - PX_WPNH) / 2) * PX_STRIDE
    mov [px_sdi], ax                ; ...biased to rows 56..79
    xor cx, cx                      ; CX = the byte column k
.col:
    push cx
    mov al, cl
    cmp byte [px_back], PXB_C160
    jne .nopair
    shr al, 1
.nopair:
    mov [px_ssrc], al
    mov ah, PXA_TEX
    mul ah
    add ax, [px_sfr]
    mov [px_ssi], ax                ; the texel column, once a column
    push ds
    pop es
    mov es, [px_hand + PXH_SPR]     ; the run table of a WEAPON column: 16
    mov al, [px_ssrc]               ; columns, PXS_WRUNOFS in
    mov ah, PXS_RUNSZ
    mul ah
    mov bx, ax
    add bx, [px_sfr]
    add bx, PXS_WRUNOFS
    mov ax, [es:bx]
    mov [px_sruns], ax
    mov ax, [es:bx + 2]
    mov [px_sruns + 2], ax
    mov ax, [es:bx + 4]
    mov [px_sruns + 4], ax
    mov ax, [es:bx + 6]
    mov [px_sruns + 6], ax
    mov al, [es:bx + 8]
    mov [px_sruns + 8], al
    cmp byte [px_sruns], PXS_MAXRUNS
    jbe .ok
    mov byte [px_sruns], PXS_MAXRUNS
.ok:
    mov cl, [px_sruns]
    or cl, cl
    jz .next
    cmp byte [px_rung], PXR_TEX
    jne .flat
    xor ch, ch
    mov bx, px_sruns + 1
    mov es, [px_hand + PXH_GEN]
.run:
    mov di, [px_qp]
    cmp di, PXG_QLAST
    jae .next
    mov al, [bx]
    xor ah, ah
    push bx
    mov bx, ax
    shl bx, 1
    add bx, [px_scot]
    mov ax, [es:bx]
    pop bx
    or ax, ax
    jz .rnext
    mov [es:di], ax
    mov ax, [px_ssi]
    mov [es:di + 2], ax
    mov ax, [px_sdi]
    mov [es:di + 4], ax
    mov al, [bx + 1]
    xor ah, ah
    cmp al, PXA_TEX
    jb .cot1
    xor ax, ax                      ; v1 = 32: no patch
    jmp short .nopatch
.cot1:
    push bx
    mov bx, ax
    shl bx, 1
    add bx, [px_scot]
    mov ax, [es:bx]
    pop bx
.nopatch:
    mov [es:di + 6], ax
    add di, PXG_QSZ
    mov [px_qp], di
.rnext:
    inc bx
    inc bx
    inc ch
    cmp ch, cl
    jb .run
    jmp short .next
.flat:                              ; the silhouette: each run through the
    xor ch, ch                      ; ladder in the sprite tone
    mov bx, px_sruns + 1
.frun:
    push bx
    push cx
    mov al, [bx]
    call px_spr_rows
    mov dx, ax
    pop cx
    pop bx
    push bx
    push cx
    push dx
    mov al, [bx + 1]
    call px_spr_rows
    dec ax
    pop dx
    pop cx
    pop bx
    cmp ax, PX_ROWS - 1
    jle .f1
    mov ax, PX_ROWS - 1
.f1:
    cmp ax, dx
    jl .fnext
    push bx
    push cx
    mov cx, ax
    sub cx, dx
    inc cx
    mov ax, [px_sdi]
    sub ax, 256 + (PX_WPNROW0 - (PX_ROWS - PX_WPNH) / 2) * PX_STRIDE
    mov [px_cdi], ax
    mov al, dl
    mov bx, [px_inks]
    push word [px_ladp]             ; a BYTE column whatever the resolution:
    mov dh, [px_bpc]                ; the byte ladder, for this call (the
    push dx                         ; byte saved through DH: a word push of
    mov word [px_ladp], px_ladb     ; it would carry px_size with it)
    mov byte [px_bpc], 1
    call px_spr_lad
    pop dx
    mov [px_bpc], dh
    pop word [px_ladp]
    pop cx
    pop bx
.fnext:
    inc bx
    inc bx
    inc ch
    cmp ch, cl
    jb .frun
.next:
    inc word [px_sdi]
    pop cx
    inc cx
    cmp cx, PX_WPNCOLS
    jb .col
    ; the present sends its rows: the frame's dirty range reaches the
    ; view's bottom (the columns' memory is left alone - the weapon sits in
    ; rows the compose has already accounted for)
    mov al, PX_WPNROW0
    mov ah, PX_ROWS - 1
    call px_rows_widen
.out:
    ret

; =============================================================================
; THE TAB MAP (wave 6, SPEC.md 97.6): the cells the player has SEEN, from
; above, drawn into the view band as a frame of its own - and the world
; stops under it (px_pause), so it is one picture drawn once (twice on Mode
; X, a page each) and nothing after it until Tab, P or the state moves on.
; Leaving it is TOUCHED OFF: px_force_all puts every column's memory at "the
; whole view", so the next world frame re-lays everything the map covered.
;
; SEEN is what a ray has crossed: the walkers mark every open or door cell
; they pass with the frame's generation (97.2.2), and the generation wraps
; every 255 frames - so at the wrap both arrays are FOLDED into px_seen, a
; bit a cell (px_seen_fold, before px_spot_clear zeroes them), and a cell is
; seen when its bit, its plain mark or its transposed mark says so. A WALL is
; never marked (a walker stops at it), so a wall is drawn when one of its
; four neighbours was seen. A new floor clears the bits with the marks. The
; draw walks three rows of SEEN FLAGS (px_mrows, the row above, this one and
; the one below, 66 bytes each), each map row's worked out ONCE from its
; three sources (px_map_row) - the first cut asked all three for every
; cell and again for a wall's four neighbours, and read 4.28M clk = 896 ms
; on the 5150 for one map; this one is priced in SPEC.md 97.6.
;
; A cell is a byte wide and TWO ROWS tall (its tone word: the even row's
; byte, then the odd's, the ladders' own pair), so the 80-row band holds 40
; cells of the 64 down, the player's row centred and clamped, and Size
; across, the player's column centred and clamped (the 64 centred, black
; either side, at Size 72 and 80). The tones are the Flat rung's own
; (px_inkt: a wall's lit tone, a door's slab - a secret door is its wall's
; material, so it is not given away - px_inkf the floor), black unseen, and
; the player's cell a marker (red on CGA 320x200, white elsewhere).
; =============================================================================
PX_MAPCELLS equ PX_ROWS / 2         ; cells down: two rows a cell
PX_MRW      equ 66                  ; a row of seen flags: the map's 64 and
                                    ; a 0 either side (px_map_row)

; px_seen_fold - both spotvis arrays OR'd into px_seen (the generation is
;                about to wrap). The plain array is 8 cells a bitmap byte in
;                order, eight at a look; the transposed one is walked a map
;                COLUMN at a time (its index is x * 64 + y), one bit mask a
;                column and a bitmap byte every 8, two cells at a look -
;                the usual cell was never crossed, and costs a share of one
;                load and one jz. Priced in SPEC.md 97.6 (the first cut's
;                byte-at-a-time loops read 450,000 clk on the 5150).
;                clobbers AX, BX, CX, DX, SI, DI
%macro PX_FOLDW 1                  ; two transposed cells (rows y, y + 1 of
    lodsw                           ; one column): their bits OR'd in when
    or ax, ax                       ; crossed - the usual pair is not, and
    jz %%n                          ; falls through the first jz
    or al, al
    jz %%h
    or [di + %1], ch
%%h:
    or ah, ah
    jz %%n
    or [di + %1 + 8], ch
%%n:
%endmacro
px_seen_fold:
    mov si, px_spot
    mov di, px_seen
    mov cx, 512
    cld
.p:
    lodsw                           ; eight cells at a look: the usual eight
    mov dx, ax                      ; were not crossed, and cost four word
    lodsw                           ; loads and a jz
    or dx, ax
    lodsw
    or dx, ax
    lodsw
    or dx, ax
    jz .pz
    sub si, 8
%rep 8
    lodsb
    neg al                          ; CF = the cell was crossed
    rcl ah, 1
%endrep
    or [di], ah
.pz:
    inc di
    loop .p
    mov si, px_spotT
    xor dx, dx                      ; DX = x
.t:
    mov bx, dx
    and bx, 7
    mov ch, [px_bitm + bx]          ; the column's bit
    mov di, dx
    shr di, 1
    shr di, 1
    shr di, 1
    add di, px_seen                 ; ...and its byte in row 0
    mov cl, 4                       ; 64 rows, sixteen at a pass (the bitmap
.ty:                                ; bytes 8 apart: displacements, no add)
%assign PX_K 0
%rep 8
    PX_FOLDW PX_K
%assign PX_K PX_K + 16
%endrep
    add di, 128
    dec cl
    jnz .ty
    inc dx
    cmp dx, 64
    jb .t
    ret

px_bitm:    db 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

; px_map_owe - the map is owed: once, or on both Mode X pages
px_map_owe:
    mov byte [px_mapd], 1
    cmp byte [px_back], PXB_MODEX
    jne .out
    mov byte [px_mapd], 2
.out:
    ret

; px_map_off - the map down (Tab or P again, or the state moved on): the
;              world runs again and the next frame re-lays the whole view -
;              px_force_all owed BETWEEN frames (px_mapfa, px_frame_begin),
;              since this may be the UI task while the worker composes.
;              preserves every register
px_map_off:
    cmp byte [px_mapon], 0
    je .out
    mov byte [px_mapon], 0
    mov byte [px_mapd], 0
    mov byte [px_pause], 0
    mov byte [px_mapfa], 1
    mov byte [px_hmsg], PXM_NONE
    mov byte [px_hudd], 1
.out:
    ret

; -----------------------------------------------------------------------------
; px_map_row - one map row's SEEN flags into 66 bytes at DS:DI: [0] and [65]
;              0 (the map's sides), [1 + x] nonzero when cell (x, AL) was
;              seen - its plain mark, its transposed one (x * 64 + y) or its
;              px_seen bit. AL = y; a y off the map (0xFF, 64) is all 0.
;              ~120 clk a cell at the 8088's fetch floor (the first cut of this
;              comment said 60, counting execution alone - review, wave 6),
;              the three reads walked, no shift by CL. The whole map frame
;              is measured in SPEC 97.6 (the draw, the Tab frame, and the
;              Mode X price of drawing it once a page).
;              clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
px_map_row:
    push bp
    mov byte [di], 0
    mov byte [di + PX_MRW - 1], 0
    inc di
    cmp al, 63
    ja .zero
    xor ah, ah
    mov si, ax                      ; SI = y: (0, y) in the transposed layout
    mov bh, al
    xor bl, bl
    shr bx, 1
    shr bx, 1                       ; BX = y * 64: (0, y) in the plain one
    mov bp, ax
    shl bp, 1
    shl bp, 1
    shl bp, 1
    add bp, px_seen                 ; BP = the row's first px_seen byte (a
    mov ch, 8                       ; value: xchg'd into BX to be read)
.grp:
    xchg bx, bp
    mov dl, [bx]                    ; eight cells' bits, the leftmost high
    xchg bx, bp
    inc bp
    mov cl, 8
.x:
    mov al, [px_spot + bx]
    or al, [px_spotT + si]
    shl dl, 1
    sbb ah, ah                      ; the bit, as a byte
    or al, ah
    mov [di], al
    inc di
    inc bx
    add si, 64
    dec cl
    jnz .x
    dec ch
    jnz .grp
    pop bp
    ret
.zero:
    mov cx, 64
.z:
    mov byte [di], 0
    inc di
    loop .z
    pop bp
    ret

; -----------------------------------------------------------------------------
; px_map_draw - the map into this frame's destination, the whole band
;               (px_r0..px_r1 = the view). DS = ours; the destination
;               through ES. clobbers everything but DS
; -----------------------------------------------------------------------------
px_map_draw:
    mov byte [px_r0], 0
    mov byte [px_r1], PX_ROWS - 1
    mov es, [px_dseg]
    mov bp, [px_dbase]              ; BP = the cell row's first byte (a
    mov al, [px_x0]                 ; value, never an address: SS is not DS)
    xor ah, ah
    add bp, ax
    mov ax, [px_pcell]              ; the origin: the player's column...
    and al, 63
    mov ah, [px_size]
    cmp ah, 64
    ja .wide
    shr ah, 1
    sub al, ah
    jnc .ox1
    xor al, al
.ox1:
    mov ah, 64
    sub ah, [px_size]
    cmp al, ah
    jbe .ox
    mov al, ah
    jmp short .ox
.wide:
    mov al, 64                      ; Size 72 / 80: the 64 centred
    sub al, ah
    sar al, 1
.ox:
    mov [px_mox], al
    mov ah, 64                      ; the band bytes that are on the map end
    sub ah, al                      ; at 64 - ox, or at Size
    cmp ah, [px_size]
    jbe .hi
    mov ah, [px_size]
.hi:
    mov [px_mhi], ah
    mov ax, [px_pcell]              ; ...and its row
    mov cl, 6
    shr ax, cl
    sub al, PX_MAPCELLS / 2
    jnc .oy1
    xor al, al
.oy1:
    cmp al, 64 - PX_MAPCELLS
    jbe .oy
    mov al, 64 - PX_MAPCELLS
.oy:
    mov [px_moy], al
    mov si, 0xAAAA                  ; the player's marker: RED on CGA
    cmp byte [px_back], PXB_CGA4    ; 320x200 (palette 0's brown is the
    je .mk1                         ; walls', its green the floor), WHITE
    mov si, 0x040F                  ; over RED where the byte is a colour
    cmp byte [px_back], PXB_MODEX   ; (Mode X, WIN4: every map tone is one
    je .mk1                         ; colour in both rows, so a two-colour
    cmp byte [px_back], PXB_WIN4    ; cell is none of them - 0x0F0F was
    je .mk1                         ; material 15's lit tone), WHITE on C160
    mov si, 0xFFFF                  ; (no material is 15 there) and on the
    cmp byte [px_back], PXB_C160    ; 1bpp two the FLOOR'S DITHER INVERTED:
    je .mk1                         ; 0xFFFF there is every wall's lit tone,
    mov si, 0xDD77                  ; a wall cell in the corridor (review,
.mk1:                               ; wave 6 r2)
    mov [px_mmark], si
    push bp                         ; THE THREE ROWS OF FLAGS: the one above
    mov al, [px_moy]                ; the first cell row, and the first
    dec al
    mov di, px_mrows
    call px_map_row
    mov al, [px_moy]
    mov di, px_mrows + PX_MRW
    call px_map_row
    pop bp
    xor dh, dh                      ; DH = the cell row
.row:
    push bp
    push dx
    mov al, [px_moy]                ; ...and the one below it
    add al, dh
    inc al
    mov di, px_mrows + 2 * PX_MRW
    call px_map_row
    pop dx
    pop bp
    mov es, [px_dseg]
    mov di, bp
    mov al, [px_moy]
    add al, dh
    mov bh, al
    xor bl, bl
    shr bx, 1
    shr bx, 1                       ; BX = y * 64
    mov ch, 0xFF                    ; CH = the player's band byte on this
    mov ax, [px_pcell]              ; row, or none
    sub ax, bx
    cmp ax, 64
    jae .nop
    sub al, [px_mox]
    mov ch, al
.nop:
    mov al, [px_mox]
    cbw
    add bx, ax                      ; BX = band byte 0's cell...
    mov si, ax
    add si, px_mrows + PX_MRW + 1   ; ...SI its flag
    xor dl, dl                      ; DL = the band byte
    or al, al
    jns .cell                       ; the wide Sizes' margins, black: left
    neg al
    mov cl, al
    xor ax, ax
.ml:
    mov [es:di], al
    mov [es:di + PX_STRIDE], ah
    inc di
    inc bx
    inc si
    inc dl
    dec cl
    jnz .ml
.cell:
    cmp dl, ch
    je .me
    mov cl, [px_map + bx]
    test cl, PXC_SOLID | PXC_DOOR   ; a door takes the wall's path: a ray
    jnz .wall                       ; that strikes a SHUT slab marks only
    cmp byte [si], 0                ; the pass before it, so a door looked
    je .black                       ; at is seen through its neighbours -
    mov ax, [px_inkf]               ; and a secret one shows its wall's
    jmp short .put                  ; material, never a hole (review, r2)
.wall:                              ; seen when it or a neighbour was: a
    mov al, [si]                    ; walker never marks the wall it stops
    or al, [si - 1]                 ; at
    or al, [si + 1]
    or al, [si - PX_MRW]
    or al, [si + PX_MRW]
    jz .black
.tone:
    push bx
    mov bl, cl
    and bl, 0xF0
    xor bh, bh
    shr bx, 1
    shr bx, 1                       ; material * 4: its (lit, dark) words
    mov ax, [px_inkt + bx]
    pop bx
    jmp short .put
.me:
    mov ax, [px_mmark]
    jmp short .put
.black:
    xor ax, ax
.put:
    mov [es:di], al                 ; the cell's two rows
    mov [es:di + PX_STRIDE], ah
    inc di
    inc bx
    inc si
    inc dl
    cmp dl, [px_mhi]
    jb .cell
    xor ax, ax                      ; ...and the right margin
.rm:
    cmp dl, [px_size]
    jae .rowend
    mov [es:di], al
    mov [es:di + PX_STRIDE], ah
    inc di
    inc dl
    jmp short .rm
.rowend:
    push ds                         ; the rows move up one: the row above is
    pop es                          ; the one drawn, the one drawn the one
    mov si, px_mrows + PX_MRW       ; below (132 bytes; ~1,650 clk a row
                                    ; on the 8088, ~25 a word: ~66k clk,
                                    ; ~14 ms of the map's draw)
    mov di, px_mrows
    mov cx, PX_MRW
    cld
    rep movsw
    add bp, 2 * PX_STRIDE
    inc dh
    cmp dh, PX_MAPCELLS
    jb .row
    ret
