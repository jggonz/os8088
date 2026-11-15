; =============================================================================
; os8088 - apps/wirearena/wrpart.asm - WireArena's RENDERER PART
;
; Part 0 of WIRERENA.O88 (docs/plans/completed/WIREARENA-PARTS-PLAN.md section 7; the
; parts standard is SPEC.md 20.12). An OP_SEG part assembled at org 0 that
; carries its own data and is entered with DS = its data segment - XTMP3's
; FRONT shape (apps/xtmp3/x3front.asm): a header naming its verbs, the FRAME
; BLOCK the primary stages into (apps/wirearena/wafb.inc), then the renderer
; (wrender.inc, included UNCHANGED), its tables, the sine table and the
; keyframes it reads in place, and its BSS - the arenas are parts of their
; own (rows 1.., WIREARENA-PARTS-PLAN 7.5), copied in by ARENA - shipped as zeros that OP_ZFILL
; neither stores nor decodes (SPEC.md 20.12.7.5).
;
; THE RULES THIS PART KEEPS, because the primary declares its claim movable
; and kern_mega runs it through a code alias (SPEC.md 20.12.5, 20.12.12):
;   - it calls NO kernel slot, so nothing claims under a far call into it and
;     the carve may move whenever the primary is not inside it;
;   - it reads and writes nothing through CS: every table is read through DS
;     (under kern_mega the CS of a part is an execute-only alias);
;   - it does no segment arithmetic: the only segments it holds are DS (its
;     own, from the primary) and the video segment out of the frame block.
; Labels are wa_ (the renderer's own, as in the primary) and wr_ (this file's).
; =============================================================================

    cpu 8086
    bits 16
    org 0

%include "waconst.inc"

; --- the frame block's variables live in the image, at WR_FB ----------------
%assign WR_FBA 0
%macro WA_FBV 2
%1 equ WR_FB + WR_FBA
%assign WR_FBA WR_FBA+%2
%endmacro
%include "watab.inc"               ; its constants only: no WA_TAB_* yet
%include "wafb.inc"
WA_FBLOCK

; --- the header (wafb.inc's layout) -----------------------------------------
wr_hdr:
    db 'WRPART', WR_ABI, WR_NVERB
    dw wr_v_init, wr_v_geom, wr_v_arena, wr_v_render, wr_v_present, wr_v_trace
wr_hpose:
    dw wa_pose0                     ; (WR_H_POSE: the hot keyframes, which
                                    ; the file holds coded and the
                                    ; primary's wa_rbind decodes once)
%if wr_hpose - wr_hdr != WR_H_POSE || WR_H_POSE != WR_H_VERB + WR_NVERB*2
    %error "wrpart: WR_H_POSE is the word after the verbs"
%endif
%if ($ - $$) > WR_FB
    %error "wrpart: the header runs into the frame block"
%endif
    times WR_FB - ($ - $$) db 0
wr_fb:
    times WR_FBA db 0               ; the frame block, staged by the primary

; --- the verbs: near bodies behind far returns --------------------------------
wr_v_render:
    call wa_render
    mov al, [wa_frame]
    retf

wr_v_present:
    call wa_present
    mov ax, [wa_nwr]
    retf

wr_v_geom:
    call wa_geominit
    retf

wr_v_arena:                     ; DX = the arena part's segment
    call wa_loadarena
    retf

; INIT - after a mode set (wa_setmode): the row tables in cell OFFSETS, which
; is what the renderer indexes everything by, the stamps and the vertex cache
; cleared (the frame number restarts, so an old stamp could match), the lists
; emptied
wr_v_init:
    push ds
    pop es
    xor bx, bx
    mov si, wa_rowo2
    mov di, wa_hroff
    mov cx, WA_ROWS + 1
.rows:
    mov ax, bx
    shl ax, 1
    mov [si], ax
    cmp cx, 1
    je .last                    ; (the HUD row has no half-rows)
    mov [di], ax
    mov [di+2], ax
    add di, 4
.last:
    add bx, [wa_w]
    inc si
    inc si
    loop .rows
    mov si, wa_hroff            ; ...and in wa_fbuf (wa_flgo's start), a
    mov di, wa_hrfb             ; byte a cell
    mov cx, WA_HR
.fb:
    lodsw
    shr ax, 1
    add ax, wa_fbuf
    stosw
    loop .fb
    mov di, wa_stamp
    xor ax, ax
    mov cx, WA_CELLS
    rep stosw
    call wa_vstclr
    mov [wl_pvc], ax            ; (no corner of last frame's stands)
    mov di, wa_fqps             ; (and the floor lines' spans' feet's)
    mov cx, WA_MAXSP
    rep stosw
    mov [wa_frame], al
    mov [wa_newn], ax
    mov [wa_oldn], ax
    mov [wa_ovoldn], ax         ; (the overlay comes again: no old cells)
    mov byte [wa_kmono], 0xFF   ; (and the attributes are made again)
    mov word [wa_newp], wa_lista
    mov word [wa_oldp], wa_listb
    retf

; TRACE - the crosshair's ray, or a CONE round it (the shotgun's: WEAPONS-
; PLAN 2.2), against the figures as the last frame drew them (the primary's
; wa_fire applies the hits). In CX = the cone's half width in sub-columns,
; DX its half height in half-rows (0, 0: the pistol's ray). A figure is hit
; when its extent overlaps the widened crosshair and nothing nearer covers
; the point of it nearest the crosshair - a wall, or a platform's lip;
; DIRECTLY when it covers the crosshair itself. Out AX = the nearest figure
; hit, its offset in wa_figs, or -1; DX = a bit a figure hit, bit 8 + i
; when directly.
wr_v_trace:
    mov [wa_tcw], cx
    mov [wa_tch], dx
    xor si, si                  ; best so far
    xor bp, bp                  ; ...and its inverse depth
    mov word [wa_tmask], 0
    mov word [wa_tbit], 0x0101
    mov di, wa_figs
    mov cx, WA_NFIG
.f:
    cmp word [di+F_PZ], 0
    je .n
    cmp byte [di+F_DEAD], 0
    jne .n
    mov bx, [wa_w]              ; BX = the point's sub-column: the
    mov ax, [di+F_PCX]          ; crosshair's, or the figure's edge nearest
    sub ax, bx                  ; it
    mov dx, [di+F_PWH]
    mov byte [wa_tdir], 1       ; (direct, so far)
    or ax, ax
    jns .right
    neg ax
    cmp ax, dx
    jle .hx
    sub ax, [wa_tcw]
    cmp ax, dx
    jg .n
    mov bx, [di+F_PCX]          ; left of the crosshair: its right edge
    add bx, dx
    jmp .off
.right:
    cmp ax, dx
    jle .hx
    sub ax, [wa_tcw]
    cmp ax, dx
    jg .n
    mov bx, [di+F_PCX]          ; right of it: its left edge
    sub bx, dx
.off:
    mov byte [wa_tdir], 0
.hx:
    mov dx, WA_HR/2             ; DX = the point's half-row: the
    mov ax, [di+F_PTOP]         ; crosshair's, or the figure's end
    cmp ax, dx                  ; nearest it
    jle .top
    sub ax, dx
    cmp ax, [wa_tch]
    jg .n
    mov dx, [di+F_PTOP]
    mov byte [wa_tdir], 0
    jmp .vy
.top:
    mov ax, [di+F_PBOT]
    cmp ax, dx
    jge .vy
    neg ax
    add ax, dx
    cmp ax, [wa_tch]
    jg .n
    mov dx, [di+F_PBOT]
    mov byte [wa_tdir], 0
.vy:
    cmp bx, [wa_w2]             ; (unsigned: the point off the screen -
    jb .col                     ; the crosshair's own column then)
    mov bx, [wa_w]
.col:
    and bx, 0xFFFE              ; its column x 2 (wa_occl)
    mov ax, [di+F_PIZ]
    push ax
    call wa_occl                ; AL = the first half-row hidden at its
    xor ah, ah                  ; depth: the point's at or below it, and
    cmp ax, dx                  ; something nearer stops the shot
    pop ax
    jbe .n
    mov dx, [wa_tbit]           ; hit: its bit, and its direct bit when so
    cmp byte [wa_tdir], 0
    jne .dir
    xor dh, dh
.dir:
    or [wa_tmask], dx
    cmp ax, bp
    jbe .n
    mov bp, ax
    mov si, di
.n:
    shl word [wa_tbit], 1
    add di, F_SIZE
    dec cx
    jz .end
    jmp .f
.end:
    mov dx, [wa_tmask]
    mov ax, -1
    or si, si
    jz .out
    mov ax, si
    sub ax, wa_figs
.out:
    retf

; wa_loadarena - DX = an arena part's segment (WIREARENA-PARTS-PLAN 7.5):
; its coordinate tables, vertices, corner rows and boxes into the working
; tables (the renderer reads those, and the primary takes the boxes and the
; start back for its collision and reset), its counts, its start, and its
; tree - copied into wa_bsp and REBASED, each link and the root moved by
; wa_bsp less the part's own tree, which is what they were assembled against.
; DS is the arena's for the copies and ours again before the rebase.
wa_loadarena:
    push ds
    pop es                      ; ES = ours, DS = the arena's, BX = its
    mov ds, dx                  ; descriptor at offset 0
    xor bx, bx
    mov si, [bx]                ; x values
    mov cx, [bx+2]
    mov ax, cx
    shl ax, 1
    mov [es:wa_nxv2], ax
    mov di, wa_xv
    rep movsw
    mov si, [bx+4]              ; y values
    mov cx, [bx+6]
    mov ax, cx
    shl ax, 1
    mov [es:wa_nyv2], ax
    mov di, wa_yv
    rep movsw
    mov si, [bx+8]              ; vertices, two bytes each
    mov cx, [bx+10]
    mov di, wa_vtx
    rep movsw
    mov cx, [bx+16]             ; boxes
    mov [es:wa_nbox], cx
    mov si, [bx+12]             ; corner rows, the room's and one a box
    inc cx
    shl cx, 1
    mov di, wa_corn
    rep movsw
    mov si, [bx+14]
    mov cx, [bx+16]
    shl cx, 1
    shl cx, 1
    mov di, wa_boxes
    rep movsw
    mov ax, [bx+20]
    mov [es:wa_sx], ax
    mov ax, [bx+22]
    mov [es:wa_sy], ax
    mov ax, [bx+24]
    mov [es:wa_sa], ax
    mov si, [bx+26]             ; the boxes' colours, the room's first
    mov cx, [bx+16]
    inc cx
    mov di, wa_bcol
    rep movsb
    mov ax, [bx+34]             ; HEIGHTS (HEIGHTS-PLAN 7.2.1): the tallest
    mov [es:wa_hmax], ax        ; height, and FLAT when no box is below it
    mov [es:wa_ahmax], ax
    mov byte [es:wa_hflat], 0   ; and no lift moves
    mov si, [bx+36]
    cmp word [si], 0
    jne .hdone
    mov si, [bx+30]             ; the box records, 4 bytes each
    mov cx, [bx+16]
    jcxz .flat
.hb:
    cmp [si], ax
    jb .hdone
    add si, 4
    loop .hb
.flat:
    mov byte [es:wa_hflat], 1
.hdone:
    mov ax, [bx+32]             ; the room's height, and each box's record
    mov [es:wa_roomh], ax       ; (height, kind, faces: HEIGHTS-PLAN 7.3.1)
    mov si, [bx+30]
    mov cx, [bx+16]
    shl cx, 1
    mov di, wa_bxh
    rep movsw
    mov al, [es:wa_hflat]       ; ...the flat verdict, for the sim
    mov [es:wa_aflat], al
    mov word [es:wa_npk], 0     ; ITS PICKUPS (WEAPONS-PLAN 2.4): the
    mov word [es:wa_pkvp], wa_pkall ; records into ARENA, and their PVS
    mov byte [es:wa_pkvh], 0    ; bytes (none: every pickup is tested -
    mov si, [bx+44]             ; wa_pkall's 0xFF)
    or si, si
    jz .nopk
    lodsw
    mov [es:wa_npk], ax
    mov cx, ax
    shl cx, 1
    shl cx, 1                   ; (WA_PKSZ / 2 words each)
    mov di, wa_pks
    rep movsw
    mov si, [bx+46]
    or si, si
    jz .nopk
    mov byte [es:wa_pkvh], 1
    mov cx, WA_PVSN / 2
    mov di, wa_pkv
    rep movsw
.nopk:
    mov si, [bx+36]             ; ITS LIFTS (ARENAS-PLAN 4.1): the sim
    lodsw                       ; moves each - count; then box, 0, rest,
    mov [es:wa_nlift], ax       ; raised (arenagen's, at most WA_MLIFT)
    mov cx, ax
    jcxz .nolift
    xor di, di
.lift:
    lodsb
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov [es:wa_lift+di], ax
    lodsb
    lodsw
    mov [es:wa_lrest+di], ax
    lodsw
    mov [es:wa_ltop+di], ax
    inc di
    inc di
    loop .lift
.nolift:
    mov word [es:wa_nfl], 0     ; ITS FLOOR LINES (ARENAS-PLAN 4.5): each
    mov si, [bx+48]             ; one's ends - the game's, in the frame
    or si, si                   ; block - and its two vertices, the
    jz .nofl                    ; renderer's (wa_flines)
    lodsw
    mov [es:wa_nfl], ax
    mov cx, ax
    jcxz .nofl
    mov di, wa_fl
    mov dx, wa_fqv
.fl:
    movsw
    movsw
    movsw
    movsw
    lodsw
    xchg di, dx
    stosw
    xchg di, dx
    loop .fl
.nofl:
    mov byte [es:wa_pvsh], 0    ; ITS PVS (HEIGHTS-PLAN 9.8): the grid's
    mov si, [bx+42]             ; shift and cells a column, and the cells'
    or si, si                   ; masks (arenagen bounds them by WA_PVSN x
    jz .nopvs                   ; WA_PVSB); none - shift 0 - every box
    lodsw
    mov [es:wa_pvsh], ax        ; (and wa_pvsn)
    lodsw
    mov cx, ax
    mov di, wa_pvs
    rep movsb
.nopvs:
    mov si, [bx+18]             ; the tree, WA_ROW bytes a node
    mov dx, wa_bsp
    sub dx, si                  ; DX = what each link moves by
    mov cx, [bx+28]
    mov bx, cx                  ; BX = its nodes
    shl cx, 1
    add cx, bx
    shl cx, 1                   ; x 6: words
    mov di, wa_bsp
    rep movsw
    push es
    pop ds                      ; ours again
    mov word [wa_root], wa_bsp  ; the root is the first node
    mov si, wa_bsp
    mov cx, bx
.node:
    cmp word [si+4], 0          ; low and high, 0 = none
    je .lo
    add [si+4], dx
.lo:
    cmp word [si+6], 0
    je .hi
    add [si+6], dx
.hi:
    add si, WA_BSPROW
    loop .node
    ret
WA_BSPROW equ 12                ; a node: db kind, box; dw value, low, high;
                                ; db its boxes' bounds x 4

%include "wrender.inc"
%include "wrlow.inc"
%include "wrtab.inc"

; the tables this half reads, out of build/watab.inc again: the constants
; are guarded to once, and the data blocks are asked for now - each
; keyframe's bound alone; the keyframes are the POSE part's (wapose.asm,
; WIREARENA-PARTS-PLAN 8.6)
%define WA_TAB_KBND
%define WA_TAB_POSE0            ; (the hot keyframes: read in place)
%include "watab.inc"

wr_image_end:

; --- bss: the renderer's own, past the image ---------------------------------
%assign WA_BSS 0
%macro WA_VAR 2
%1 equ wr_image_end+WA_BSS
%assign WA_BSS WA_BSS+%2
%endmacro
WA_VAR wa_frame,1
WA_VAR wa_nxv2,2
WA_VAR wa_nyv2,2
WA_VAR wa_root,2
WA_VAR wa_xv,WA_MXV*2
WA_VAR wa_yv,WA_MYV*2
WA_VAR wa_vtx,WA_MVTX*2
WA_VAR wa_corn,(WA_MBOX+1)*4
WA_VAR wa_bcol,WA_MBOX+2
WA_VAR wa_bsp,WA_MNODE*WA_BSPROW  ; the tree, rebased (wa_loadarena)
WA_VAR wa_pvsh,1                 ; the PVS: the grid's shift (0: none)...
WA_VAR wa_pvsn,1                 ; ...its cells a column...
WA_VAR wa_pvp,2                  ; ...this cell's mask (wa_order)...
WA_VAR wa_pvs,WA_PVSN*WA_PVSB    ; ...and every cell's, a bit a box
WA_VAR wa_pkvh,1                 ; the pickups' PVS: carried...
WA_VAR wa_pkvp,2                 ; ...this cell's byte (wa_order), or
WA_VAR wa_pkv,WA_PVSN            ; wa_pkall... and every cell's
WA_VAR wa_tcw,2                  ; TRACE: the cone's half width, half
WA_VAR wa_tch,2                  ;   height, the figures hit, this
WA_VAR wa_tmask,2                ;   figure's bits, and whether it was hit
WA_VAR wa_tbit,2                 ;   directly
WA_VAR wa_tdir,1
WA_VAR wa_tmp,2
WA_VAR wa_rowo2,(WA_ROWS+1)*2
WR_BSS
WL_BSS
WA_VAR wa_newp,2
WA_VAR wa_oldp,2
WA_VAR wa_newn,2
WA_VAR wa_oldn,2
WA_VAR wa_stamp,WA_CELLS*2       ; by cell OFFSET: the stamp is the even
wa_fmask equ wa_stamp+1          ; byte, a figure cell's sub-pixel mask the odd
WA_VAR wa_cur,WA_CELLS*2
WA_VAR wa_lista,WA_CELLS*2
WA_VAR wa_listb,WA_CELLS*2

; the part's bytes include its BSS - a part has no loader-zeroed tail - as
; zeros the packer cuts off (OP_ZFILL) and op_unpack fills back
    times WA_BSS db 0
wr_end:
%if wa_jsy != wa_jsx + WA_NJ*2
    %error "wrpart: wa_fjl's narrow loops store y at wa_jsx + 22"
%endif
%if wa_pose0 + WA_KHOT*WA_FRAMESZ - wr_hdr > 0x7FFF
    %error "wrpart: the hot keyframes past 32K - wa_kfo's bit 15 says the pose part"
%endif
%if WR_FKA < F_PWH + 2 || WR_FKA < WA_FIGHEAD || WR_FKB + 2 > F_SIZE
    %error "wrpart: a figure's keyframe-row keys overlap what the game sends or the projection"
%endif
%if (wr_end - wr_hdr) > 65024
    %error "wrpart: the renderer part passed the 65,024 bytes one part may hold"
%endif
