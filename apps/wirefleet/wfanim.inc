; Missiles and impacts as cell stamps. SPEC.md 114.11.
; A flight runs in VIRTUAL columns: the left board is 0..9, the right 12..21
; and 10..11 are the gap, where nothing is drawn. A sprite is a cell key -
; the cell plus 100 on the right board, which is wf_cache's own index - and
; a stamp ID; four of them are live at most.
WF_FXSPR equ 4
WF_FX_HOVER equ 36              ; ticks a landed missile waits for its result

; The stamp bank, once, at window open: only when the heap holds it. It was
; fetched at the first tick in battle, and on a 4.77 MHz XT that read held
; the UI task - the Ready cue, the link, the window - for over a second at
; exactly the moment a captain pressed Ready. At open the disk is the one
; the package just came off. Without it shots are not animated and play as
; before.
wf_fx_need:
    cmp byte [wf_fxtried], 0
    jne .out
    mov byte [wf_fxtried], 1
    call op_clbget
    jc .out
    mov al, WF_P_FX
    call op_row
    call op_lazykb
    jc .out
    add ax, 8                   ; and leave the heap something
    jc .out
    xchg ax, bx
    call OSAPI_MEM_AVAIL
    cmp ax, bx
    jb .out
    mov al, WF_P_FX
    call op_fetch
    jc .out
    mov al, WF_P_FX
    call op_seg
    mov [wf_fxseg], ax
    cmp byte [wf_monochrome], 0
    jne .out
    call wf_fxplanarinit
.out:
    push ds
    pop es
    ret

; A colour window's stamps, spread to planes ONCE into a claim of their own:
; spreading two rows a scanline in wf_fxplanar is 1.3 ms, 31 ms a stamped
; cell, on a 4.77 MHz XT - more than the whole frame it is part of. Same
; offsets as the bank's colour form (WF_FX_COLOR is 0), each 12-byte row
; as plane 0's three bytes then planes 1-3, wf_ptiles' layout. Asked once;
; without the 9KB the on-the-fly spread still draws.
WF_FXP_KB equ (WF_FX_MONO + 1023) / 1024
wf_fxplanarinit:
    cmp word [wf_fxseg], 0
    je .out
    cmp byte [wf_fxptried], 0
    jne .out
    mov byte [wf_fxptried], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, WF_FXP_KB
    call OSAPI_MEM_CLAIM
    jc .done
    mov [wf_fxpseg], dx
    call wf_spreadinit
    push word [wf_pdest]
    xor si, si
    mov cx, WF_FX_MONO / 12
.row:
    push cx
    push si
    push ds
    pop es
    mov di, wf_cellbuf
    mov ax, [wf_fxseg]
    push ds
    mov ds, ax
    mov cx, 6
    rep movsw
    pop ds
    mov word [wf_pdest], wf_fxtmp
    call wf_spread
    pop di                      ; this row's offset, in the planar claim too
    push di
    mov es, [wf_fxpseg]
    mov si, wf_fxtmp
    mov cx, 6
    rep movsw
    pop si
    add si, 12
    pop cx
    loop .row
    pop word [wf_pdest]
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; AL = the enemy cell the player fired at. The launch sound always; the
; flight only with the bank. The launch square is one of our afloat ship
; squares, scanned from a random start (the target's row, column J, if none).
wf_fx_launch:
    push ax
    mov al, WF_SND_LAUNCH
    call wf_fxsound
    pop ax
    cmp word [wf_fxseg], 0
    je .out
    mov [wf_fxcell], al
    mov byte [wf_fxboard], 1
    call wf_fxrandcell
    mov cx, 100
.scan:
    xor bh, bh
    mov bl, al
    cmp byte [wf_board+bx], 0
    je .next
    cmp byte [wf_inshots+bx], 2
    jb .found
.next:
    inc al
    cmp al, 100
    jb .wrap
    xor al, al
.wrap:
    loop .scan
    mov al, [wf_fxcell]
    call wf_fxrowcol
    mov al, 9
    jmp short .from
.found:
    call wf_fxrowcol            ; AL = column, AH = row
.from:
    mov [wf_fxlx], al
    mov [wf_fxly], ah
    mov al, [wf_fxcell]
    call wf_fxrowcol
    add al, 12
    jmp wf_fxstart
.out:
    ret

; AL = our cell the opponent's missile is coming for: from a random square
; of the enemy waters, which reveals nothing - their fleet is not drawn.
wf_fx_incoming:
    push ax
    mov al, WF_SND_LAUNCH
    call wf_fxsound
    pop ax
    cmp word [wf_fxseg], 0
    je .out
    mov [wf_fxcell], al
    mov byte [wf_fxboard], 0
    call wf_fxrandcell
    call wf_fxrowcol
    add al, 12
    mov [wf_fxlx], al
    mov [wf_fxly], ah
    mov al, [wf_fxcell]
    call wf_fxrowcol
    jmp wf_fxstart
.out:
    ret

; AL = 0..99 from the OS generator.
wf_fxrandcell:
    call OSAPI_RAND
    xor dx, dx
    mov bx, 100
    div bx
    mov al, dl
    ret
; AL = cell -> AL = column, AH = row.
wf_fxrowcol:
    xor ah, ah
    mov dl, 10
    div dl
    xchg al, ah
    ret

; AL/AH = the target's virtual column and row; the launch's are banked.
wf_fxstart:
    mov [wf_fxtx], al
    mov [wf_fxty], ah
    sub al, [wf_fxlx]
    mov [wf_fxdx], al
    sub ah, [wf_fxly]
    mov [wf_fxdy], ah
    mov bl, al                  ; |dx|, |dy|
    or bl, bl
    jns .ady
    neg bl
.ady:
    mov bh, ah
    or bh, bh
    jns .n
    neg bh
.n:
    mov cl, bl
    cmp bh, bl
    jbe .dir
    mov cl, bh
.dir:
    mov [wf_fxn], cl
    mov dl, WF_FX_MISSILE_E     ; 2|dy| < |dx| flies level; else diagonal
    mov ch, bh
    shl ch, 1
    cmp ch, bl
    jb .side
    mov dl, WF_FX_MISSILE_NE
    or ah, ah
    js .side
    mov dl, WF_FX_MISSILE_SE
.side:
    or al, al
    jns .set
    add dl, WF_FX_MISSILE_W - WF_FX_MISSILE_E
.set:
    mov [wf_fxdir], dl
    mov byte [wf_fxmode], 1
wf_fxphase:
    call OSAPI_GET_TICKS
    mov [wf_fxt0], ax
    jmp wf_fx_tick

; AL = 1 miss, 2 hit, 3 sunk; BL = the board (0 ours, 1 theirs); BH = the
; cell. The splash or explosion is heard whatever the bank; a sunk ship's
; cue waits for it to end (wf_sfx).
wf_fx_impact:
    push ax
    push bx
    mov ah, WF_SND_SPLASH
    cmp al, 1
    je .sound
    mov ah, WF_SND_EXPLODE
.sound:
    mov al, ah
    call wf_fxsound
    pop bx
    pop ax
    cmp al, 3
    jne .draw
    push ax
    mov al, 6
    call wf_sfx
    pop ax
.draw:
    cmp word [wf_fxseg], 0
    je .out
    mov [wf_fxboard], bl
    mov [wf_fxcell], bh
    mov ah, WF_FX_SPLASH_1
    cmp al, 1
    je .frames
    mov ah, WF_FX_EXPLOSION_1
.frames:
    mov [wf_fxdir], ah
    mov byte [wf_fxmode], 3
    jmp wf_fxphase
.out:
    ret

; New fleet, Load: no effect survives, and a stashed frame is dropped -
; its peer repeats it, and the state it was for is gone.
wf_fx_cancel:
    mov byte [wf_fxmode], 0
    mov byte [wf_fxstash], 0
    jmp wf_fx_tick

; The frame the landing is waiting for: CF=1 = it was stashed (or a repeat
; of one already stashed) and the caller returns; CF=0 = take it now. AL =
; the frame's type. Only while OUR flight (mode 1) is in the air.
wf_fx_defer:
    cmp byte [wf_fxreplay], 0
    jne .now
    cmp byte [wf_fxmode], 1
    jne .now
    cmp byte [wf_fxstash], 0
    jne .held
    mov [wf_fxstash], al
    push ds
    pop es
    mov si, wf_rxbuf
    mov di, wf_fxframe
    mov cx, 32
    rep movsw
.held:
    stc
    ret
.now:
    clc
    ret

; Landed: take the stashed frame as if it had just arrived. The receive
; buffer may hold a frame being decoded, so the two are SWAPPED and swapped
; back rather than one overwriting the other.
wf_fx_take:
    cmp byte [wf_fxstash], 0
    je .out
    call wf_fxswap
    mov byte [wf_fxreplay], 1
    mov al, [wf_fxstash]
    mov byte [wf_fxstash], 0
    cmp al, WF_SHOT
    jne .result
    call wf_shot_take
    jmp short .done
.result:
    call wf_result_take
.done:
    mov byte [wf_fxreplay], 0
    call wf_fxswap
.out:
    ret
wf_fxswap:
    mov si, wf_rxbuf
    mov di, wf_fxframe
    mov cx, 32
.word:
    mov ax, [si]
    xchg ax, [di]
    mov [si], ax
    inc si
    inc si
    inc di
    inc di
    loop .word
    ret

; Every tick: this frame's sprites, from the phase and the ticks since it
; began. Any change marks the window dirty; the board cache then redraws
; exactly the cells whose stamp moved (wf_buildrow).
wf_fx_tick:
    push ds
    pop es
    mov di, wf_fxnew
    mov al, 0xff
    mov cx, WF_FXSPR*2
    rep stosb
    mov al, [wf_fxmode]
    or al, al
    jz .compare
    call OSAPI_GET_TICKS
    sub ax, [wf_fxt0]
    cmp ax, 255
    jb .dt
    mov al, 255
.dt:
    mov [wf_fxdt], al
    mov di, wf_fxnew
    cmp byte [wf_fxmode], 1
    je .flight
    cmp byte [wf_fxmode], 2
    je .hover
    ; impact: four frames of three ticks
    xor ah, ah
    mov bl, 3
    div bl
    cmp al, 4
    jae .idle
    add al, [wf_fxdir]
    mov ah, al
    call .target
    jmp .compare
.hover:
    cmp al, WF_FX_HOVER
    jae .idle
    mov ah, [wf_fxdir]
    call .target
    jmp .compare
.idle:
    mov byte [wf_fxmode], 0
    jmp .compare
.flight:
    cmp al, 2
    jae .moving
    mov al, [wf_fxlx]
    mov ah, [wf_fxly]
    call wf_fxkey
    mov ah, WF_FX_LAUNCH_FLASH
    call .put
    jmp .compare
.moving:
    sub al, 2                   ; k = (dt-2)*3/2 + 1, at most n
    mov bl, 3
    mul bl
    shr ax, 1
    inc ax
    cmp ax, 255
    jb .k
    mov al, 255
.k:
    mov byte [wf_fxland], 0
    cmp al, [wf_fxn]
    jb .head
    mov al, [wf_fxn]
    mov byte [wf_fxland], 1
.head:
    mov [wf_fxk], al
    call wf_fxpos
    mov ah, [wf_fxdir]
    call .put
    mov al, [wf_fxk]
    dec al
    jz .launch
    call wf_fxpos
    mov ah, WF_FX_SMOKE
    call .put
.launch:
    cmp byte [wf_fxdt], 10
    jae .landed
    mov al, [wf_fxlx]
    mov ah, [wf_fxly]
    call wf_fxkey
    mov ah, WF_FX_SMOKE
    call .put
.landed:
    cmp byte [wf_fxland], 0
    je .compare
    mov byte [wf_fxmode], 2
    call OSAPI_GET_TICKS
    mov [wf_fxt0], ax
    call .compare
    jmp wf_fx_take              ; the result it was waiting for, if it came
.compare:
    push ds
    pop es
    mov si, wf_fxnew
    mov di, wf_fxspr
    mov cx, WF_FXSPR
    repe cmpsw
    je .same
    ; The board rows the old sprites and the new ones sit on, for
    ; wf_fx_draw: a frame rescans those rows, not all twenty (a whole-board
    ; scan is 71 ms on a 4.77 MHz XT before it draws a pixel).
    mov si, wf_fxspr
    call .rows
    mov si, wf_fxnew
    call .rows
    mov si, wf_fxnew
    mov di, wf_fxspr
    mov cx, WF_FXSPR
    rep movsw
.same:
    ret
; SI -> WF_FXSPR sprites: OR each one's row into wf_fxrows (a word a board).
.rows:
    mov cx, WF_FXSPR
.key:
    lodsw                       ; AL = the key, AH = the stamp
    cmp al, 0xff
    je .nextkey
    xor ah, ah
    mov dl, 100
    div dl                      ; AL = the board, AH = the cell
    xor bh, bh
    mov bl, al
    shl bx, 1
    mov al, ah
    xor ah, ah
    mov dl, 10
    div dl                      ; AL = the row
    push cx
    mov cl, al
    mov ax, 1
    shl ax, cl
    pop cx
    or [wf_fxrows+bx], ax
.nextkey:
    loop .key
    ret
.target:
    mov al, [wf_fxcell]
    cmp byte [wf_fxboard], 0
    je .put
    add al, 100
; AL = key (0FFh: off the boards), AH = stamp. A key already in the list
; keeps the stamp it has: the head is put first and wins.
.put:
    cmp al, 0xff
    je .full
    mov si, wf_fxnew
.dup:
    cmp si, di
    jae .append
    cmp [si], al
    je .full
    inc si
    inc si
    jmp .dup
.append:
    cmp di, wf_fxnew+WF_FXSPR*2
    jae .full
    mov [di], ax
    inc di
    inc di
.full:
    ret

; AL = step 0..n along the flight -> AL = its cell key. Rounded to the
; nearest cell: x = lx + (dx*k +- n/2) / n, and y the same way.
wf_fxpos:
    push di
    mov bl, al
    mov al, [wf_fxdx]
    call .axis
    add al, [wf_fxlx]
    push ax
    mov al, [wf_fxdy]
    call .axis
    add al, [wf_fxly]
    mov ah, al
    pop dx
    mov al, dl
    pop di
    jmp short wf_fxkey
.axis:
    cbw
    push bx
    xor bh, bh
    imul bx                     ; AX = d * k (|d| <= 21, k <= 21)
    pop bx
    xor ch, ch
    mov cl, [wf_fxn]
    push cx
    shr cx, 1
    or ax, ax
    jns .up
    neg cx
.up:
    add ax, cx
    cwd
    pop cx
    idiv cx
    ret

; AL = virtual column, AH = row -> AL = the cell key, 0FFh in the gap.
; Clobbers CX, DX.
wf_fxkey:
    xor dl, dl
    cmp al, 10
    jb .cell
    cmp al, 12
    jb .gap
    sub al, 12
    mov dl, 100
.cell:
    mov cl, al
    mov al, ah
    mov ch, 10
    mul ch
    add al, cl
    add al, dl
    ret
.gap:
    mov al, 0xff
    ret

; BX = a cell key (wf_cache's index) -> AL = its stamp, 0 for none.
; Preserves every other register.
wf_fxat:
    push si
    push cx
    mov si, wf_fxspr
    mov cx, WF_FXSPR
.find:
    cmp [si], bl
    je .hit
    inc si
    inc si
    loop .find
    xor al, al
    jmp short .out
.hit:
    mov al, [si+1]
.out:
    pop cx
    pop si
    ret

; The planar copier's stamp (wf_copytile_p): the colour form's AND and OR
; rows for source row BP spread to planes on the fly - the bank is not
; cached in planar form - then applied to the four plane rows at [wf_pdest].
wf_fxplanar:
    cmp word [wf_fxpseg], 0
    je .spreading
    push bp
    push es
    mov di, [wf_pdest]
    mov si, [wf_fxand]
    add si, bp
    mov bx, [wf_fxor]
    add bx, bp
    mov es, [wf_fxpseg]
    mov cx, 4
.pplane:
%rep 3
    mov al, [di]
    and al, [es:si]
    or al, [es:bx]
    mov [di], al
    inc si
    inc bx
    inc di
%endrep
    add di, WF_PSTEP-3
    loop .pplane
    pop es
    pop bp
    ret
.spreading:
    push dx
    push bp
    push word [wf_pdest]
    push ds
    pop es
    mov si, [wf_fxand]
    add si, bp
    mov word [wf_pdest], wf_fxtmp
    call .spread
    mov si, [wf_fxor]
    add si, bp
    mov word [wf_pdest], wf_fxtmp+12
    call .spread
    pop di
    mov [wf_pdest], di
    mov si, wf_fxtmp
    mov cx, 4
.plane:
%rep 3
    mov al, [di]
    and al, [si]
    or al, [si+12]
    mov [di], al
    inc si
    inc di
%endrep
    add di, WF_PSTEP-3
    loop .plane
    pop bp
    pop dx
    ret
.spread:
    mov ax, [wf_fxseg]
    mov di, wf_cellbuf
    push ds
    mov ds, ax
    mov cx, 6
    rep movsw
    pop ds
    jmp wf_spread

; Redraw only the rows wf_fx_tick marked, through the cell cache: what a
; frame of a flight costs instead of a whole update. The window's clip is
; armed as wf_update arms it; inside the CGA bracket there is none.
wf_fx_draw:
    mov ax, [wf_fxrows]
    or ax, [wf_fxrows+2]
    jz .out
    cmp byte [wf_ready], 0
    je .done
    cmp byte [wf_infs], 0
    jne .geometry
    mov bx, [wf_win]
    call OSAPI_WM_CLIP_SET
    jc .done
.geometry:
    call wf_geometry
    mov byte [wf_fullpaint], 0
    mov byte [wf_drawboard], 0
.board:
    mov ax, [wf_gx0]
    cmp byte [wf_infs], 0
    je .left
    mov ax, 20
.left:
    cmp byte [wf_drawboard], 0
    je .dest
    mov ax, [wf_gx1]
    cmp byte [wf_infs], 0
    je .dest
    mov ax, 176
.dest:
    mov [wf_destx], ax
    mov bx, [wf_gridy]
    mov cx, 240
    mov dx, [wf_gridh]
    call wf_planarprobe
    xor bx, bx
    mov bl, [wf_drawboard]
    shl bx, 1
    mov si, [wf_fxrows+bx]
    mov byte [wf_drawrow], 0
.row:
    shr si, 1
    jnc .next
    push si
    xor ax, ax
    mov al, [wf_drawrow]
    mul word [wf_cellh]
    add ax, [wf_gridy]
    mov [wf_desty], ax
    mov byte [wf_rowlimit], 10
    call wf_buildrow
    call wf_blitrow
    pop si
.next:
    inc byte [wf_drawrow]
    or si, si
    jnz .row
    inc byte [wf_drawboard]
    cmp byte [wf_drawboard], 2
    jb .board
.done:
    mov word [wf_fxrows], 0
    mov word [wf_fxrows+2], 0
.out:
    ret
