; Desktop splash and help (SPEC.md 102.4).  The window paints a disk-resident
; picture - 8BITBIKE.VGA, .HRC or .CGA, EXF1 rows, compiled by
; tools/excitebike_assets.py - as native desktop pixels, decoded one bounded
; 16-row batch at a time and blitted with OSAPI_GFX_BLITP / BLIT1 (no per-pixel
; conversion, no text baked into the art: the menu is font_run over a reserved
; black area).  The window timer reveals it a band a tick so the first paint
; never waits on the disk.  The claim is instance-owned and MOVABLE (SPEC.md
; 66) and stays across a fullscreen visit, so leaving fullscreen repaints from
; memory and the heap returns to exactly where it was.  The row grammar is the
; one DrMarco's DMF1 uses (tools/excitebike_assets.py restates it).
xb_frontgeom:
    mov bx, [xb_win]
    call OSAPI_WM_DISPLAY
    mov byte [xb_frontcolor], 0
    cmp dh, 1
    jbe .mono
    inc byte [xb_frontcolor]
.mono:
    ; Playable on a VGA that offers 0Dh and on a real CGA that offers the
    ; 320x200x4 mode - a fact from OSAPI_FSX_CAPS, never a guess (SPEC.md 47).
    ; EGA sees the splash and a sentence: the race is VGA/CGA/Hercules.
    mov bx, [xb_win]
    call OSAPI_FSX_CAPS
    mov byte [xb_frontplay], 0
    cmp dl, VID_VGA
    jne .cgakind
    test ax, 1 << FSXM_VGA0D
    jz .caps
    inc byte [xb_frontplay]
    jmp .caps
.cgakind:
    cmp dl, VID_CGA
    jne .herckind
    test ax, 1 << FSXM_CGA320
    jz .caps
    inc byte [xb_frontplay]
    jmp .caps
.herckind:
    cmp dl, VID_HERC
    jne .caps
    test ax, 1 << FSXM_HERC         ; Hercules plays too (SPEC.md 102.7): 720x348 mono, 2 card pixels a game pixel
    jz .caps
    inc byte [xb_frontplay]
.caps:
    call OSAPI_WM_CONTENT
    mov [xb_frontx], ax
    mov [xb_fronty], dx
    call OSAPI_WM_GEOM
    mov [xb_frontw], cx
    mov [xb_fronth], dx
    mov byte [xb_frontscale], 1
    mov word [xb_frontheight], 132
    cmp dx, 260
    jb .out
    inc byte [xb_frontscale]
    shl word [xb_frontheight], 1
.out: ret

xb_frontpaint:
    call xb_frontgeom
    clc
    call OSAPI_GFX_PEN
    ; Fill only what the art will not cover (SPEC.md 102.4, rule 2: nothing
    ; writes a pixel twice): the two side margins, the strip under the art and
    ; the rows the reveal has not reached yet.
    mov ax, [xb_frontx]
    mov bx, [xb_fronty]
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, [xb_fronth]
    dec dx
    call OSAPI_GFX_FILL             ; left margin
    mov ax, [xb_frontx]
    add ax, 440
    mov cx, [xb_frontx]
    add cx, [xb_frontw]
    dec cx
    cmp ax, cx
    ja .below
    mov bx, [xb_fronty]
    mov dx, bx
    add dx, [xb_fronth]
    dec dx
    call OSAPI_GFX_FILL             ; right margin
.below:
    mov bx, [xb_fronty]
    add bx, [xb_frontheight]
    mov dx, [xb_fronty]
    add dx, [xb_fronth]
    dec dx
    cmp bx, dx
    ja .rows
    mov ax, [xb_frontx]
    add ax, 8
    mov cx, [xb_frontx]
    add cx, 439
    call OSAPI_GFX_FILL             ; strip under the art
.rows:
    xor ax, ax
    cmp word [xb_artseg], 0
    je .from
    mov ax, [xb_reveal]
    cmp ax, [xb_frontheight]
    jae .sized
.from:
    mov bx, [xb_fronty]
    add bx, ax
    mov dx, [xb_fronty]
    add dx, [xb_frontheight]
    dec dx
    mov ax, [xb_frontx]
    add ax, 8
    mov cx, ax
    add cx, 431
    call OSAPI_GFX_FILL             ; rows not yet revealed
.sized:
    ; Let the first paint return before disk I/O: controls appear immediately.
    cmp byte [xb_artkind], 0
    je .pending
    call xb_frontload
.pending:
    mov word [xb_fronttextfrom], 0
    xor ax, ax
    mov cx, [xb_reveal]
    cmp cx, [xb_frontheight]
    jbe .band
    mov cx, [xb_frontheight]
.band:
    call xb_frontband
    mov word [xb_fronttextfrom], 0
    mov ax, [xb_frontheight]
    mov [xb_fronttextto], ax
    jmp xb_frontlabels

xb_frontreset:
    mov word [xb_reveal], 0
    mov bx, [xb_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    ret

; One window-shade step per existing music timer. No worker or blocking wait.
xb_fronttick:
    cmp byte [xb_fs], 0
    jne .out
    cmp byte [xb_abon], 0
    jne .out
    mov ax, [xb_reveal]
    cmp ax, [xb_frontheight]
    jae .out
    mov bx, [xb_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call xb_frontgeom
    call xb_frontload
    cmp word [xb_artseg], 0
    jne .ready
    mov ax, [xb_frontheight]
    mov [xb_reveal], ax
    jmp xb_frontpaint
.ready:
    mov ax, [xb_reveal]
    mov [xb_fronttextfrom], ax
    add ax, 12
    cmp ax, [xb_frontheight]
    jbe .step
    mov ax, [xb_frontheight]
.step:
    mov [xb_fronttextto], ax
    mov cx, ax
    sub cx, [xb_reveal]
    xchg ax, [xb_reveal]
    call xb_frontband
    call xb_frontlabels
.out: ret

xb_frontload:
    xor bx, bx
    cmp byte [xb_frontcolor], 0
    jne .kind
    inc bx
    cmp byte [xb_frontscale], 2
    je .kind
    inc bx
.kind:
    inc bx
    cmp bl, [xb_artkind]
    je .out
    mov [xb_artkind], bl
    dec bx
    shl bx, 1
    shl bx, 1
    mov si, [xb_artfiles+bx]
    mov cx, [xb_artfiles+bx+2]
    mov [xb_artsize], cx
    mov dx, [xb_artseg]
    or dx, dx
    jz .claim
    call OSAPI_MEM_FREE
.claim:
    mov word [xb_artseg], 0
    mov ax, cx
    add ax, 1023
    push cx
    mov cl, 10
    shr ax, cl
    pop cx
    call OSAPI_MEM_CLAIM
    jc .out
    mov [xb_artseg], dx
    mov es, dx
    xor bx, bx
    xor dx, dx
    call OSAPI_FILE_READ
    jc .bad
    or dx, dx
    jnz .bad
    cmp ax, [xb_artsize]
    jne .bad
    cmp word [es:0], 'EX'
    jne .bad
    cmp word [es:2], 'F1'
    jne .bad
    cmp word [es:4], 432
    jne .bad
    mov ax, [xb_frontheight]
    cmp byte [xb_frontcolor], 0
    je .height
    mov ax, 264
.height:
    cmp ax, [es:6]
    jne .bad
    mov ax, 1
    cmp byte [xb_frontcolor], 0
    je .depth
    mov al, 4
.depth:
    cmp ax, [es:8]
    jne .bad
    mov dx, [xb_artseg]
    mov ax, xb_artreloc
    call OSAPI_MEM_MOVABLE
.out: ret
.bad:
    mov dx, [xb_artseg]
    call OSAPI_MEM_FREE
    mov word [xb_artseg], 0
    ret
xb_artreloc:
    mov [xb_artseg], dx
    ret

; AX=first output row, CX=count; the file directory addresses compressed rows.
xb_frontband:
    cmp word [xb_artseg], 0
    je .ret
    or cx, cx
    jz .ret
    mov [xb_frontrow], ax
    add ax, cx
    mov [xb_frontend], ax
    mov bx, [xb_win]
    call OSAPI_WM_CLIP_SET   ; real ownership clip, including during W_PAINT
    jc .ret
    ; Native planar copying refuses a region clip. Only disarm it after
    ; proving this entire band belongs to us; covered bands keep clipping.
    mov dx, ax
    add dx, [xb_fronty]
    dec dx
    mov bx, [xb_frontrow]
    add bx, [xb_fronty]
    mov ax, [xb_frontx]
    add ax, 8
    mov cx, ax
    add cx, 431
    call OSAPI_WM_CLIP_TEST
    jc .row
    call OSAPI_WM_CLIP_CLEAR
.row:
    mov ax, [xb_frontrow]
    cmp ax, [xb_frontend]
    jae .out
    mov [xb_frontbatch], ax
    add ax, 16
    cmp ax, [xb_frontend]
    jbe .limit
    mov ax, [xb_frontend]
.limit:
    mov [xb_frontptr], ax       ; exclusive row limit while decoding
    mov word [xb_frontstride], 54
    cmp byte [xb_frontcolor], 0
    je .decodebegin
    mov word [xb_frontstride], 216
.decodebegin:
    mov di, xb_rows
.decode:
    mov ax, [xb_frontrow]
    mov es, [xb_artseg]
    cmp byte [xb_frontcolor], 0
    je .scene
    cmp byte [xb_frontscale], 2
    je .scene
    shl ax, 1                  ; compact color window samples alternate rows
.scene:
    cmp byte [xb_help], 0
    je .index
    add ax, [es:6]
.index:
    shl ax, 1
    mov bx, ax
    mov si, [es:bx+10]
    call xb_frontdecode
    jc .out
    inc word [xb_frontrow]
    mov ax, [xb_frontrow]
    cmp ax, [xb_frontptr]
    jb .decode
    push ds
    pop es
    mov si, xb_rows
    mov ax, [xb_frontx]
    add ax, 8
    mov bx, [xb_fronty]
    add bx, [xb_frontbatch]
    mov cx, 432
    mov dx, [xb_frontrow]
    sub dx, [xb_frontbatch]
    mov bp, [xb_frontstride]
    cmp byte [xb_frontcolor], 0
    je .mono
    mov di, 54
    call OSAPI_GFX_BLITP
    jnc .row
    ; Region clipping/display seams: convert and draw one row at a time.
    mov word [xb_frontptr], xb_rows
.fallback:
    mov si, [xb_frontptr]
    mov di, xb_queue
    mov cx, 108
    rep movsw
    mov [xb_frontptr], si
    call xb_frontpacked
    mov si, xb_queue+216
    mov ax, [xb_frontx]
    add ax, 8
    mov bx, [xb_fronty]
    add bx, [xb_frontbatch]
    mov cx, 432
    mov dx, 1
    mov bp, 216
    call OSAPI_GFX_BLIT4
    inc word [xb_frontbatch]
    mov ax, [xb_frontbatch]
    cmp ax, [xb_frontrow]
    jb .fallback
    jmp .row
.mono:
    call OSAPI_GFX_BLIT1
    jmp .row
.out:
    mov bx, [xb_win]
    call OSAPI_WM_CLIP_SET
.ret: ret

; Native four-plane row -> packed fallback, using the spare half of xb_queue.
xb_frontpacked:
    SAVE
    mov si, xb_queue
    mov di, xb_queue+216
    mov dx, 54
.byte:
    ; the four plane bytes go into registers once; every shift is then a
    ; register op (BL=plane 0 .. CH=plane 3), not a read-modify-write
    mov bl, [si]
    mov bh, [si+54]
    mov cl, [si+108]
    mov ch, [si+162]
%rep 4
%rep 2
    shl ch, 1
    rcl ah, 1
    shl cl, 1
    rcl ah, 1
    shl bh, 1
    rcl ah, 1
    shl bl, 1
    rcl ah, 1
%endrep
    mov al, ah
    stosb
%endrep
    inc si
    dec dx
    jnz .byte
    RESTORE
    ret

; Bounds-check input and output for every packet from the supporting file.
xb_frontunpack:
    mov di, xb_queue
; ES:SI input, DS:DI output. Exchange segments once per row so the 8088 can
; use REP for literal/repeated spans, retaining both input and output bounds.
xb_frontdecode:
    mov bx, [xb_artsize]
    mov bp, 54
    cmp byte [xb_frontcolor], 0
    je .segments
    mov bp, 216
.segments:
    push ds
    push es
    push ds
    push es
    pop ds
    pop es
    cld
.packet:
    cmp si, bx
    jae .bad
    lodsb
    or al, al
    jz .end
    xor cx, cx
    mov cl, al
    test al, 80h
    jz .repeat
    and cl, 7fh
    inc cx
    cmp cx, bp
    ja .bad
    sub bp, cx
    mov ax, si
    add ax, cx
    jc .bad
    cmp ax, bx
    ja .bad
    rep movsb
    jmp .packet
.repeat:
    cmp cx, bp
    ja .bad
    sub bp, cx
    cmp si, bx
    jae .bad
    lodsb
    rep stosb
    jmp .packet
.end:
    or bp, bp
    jnz .bad
    clc
    jmp .out
.bad:
    stc
.out:
    pop es
    pop ds
    ret

xb_frontlabels:
    cmp word [xb_artseg], 0
    jne .page
    mov si, xb_artmissing
    cmp byte [xb_artkind], 0
    jne .message
    mov si, xb_artloading
.message:
    mov cx, 24
    mov dx, 12
    mov ax, CYELLOW
    call xb_fronttext
.page:
    cmp byte [xb_help], 0
    jne .help
    mov di, xb_splashlabels
    jmp .labels
.help:
    mov di, xb_helplabels
.labels:
    mov cx, [di]
    cmp cx, -1
    je .out
    mov dx, [di+2]
    mov ax, [di+4]
    mov si, [di+6]
    cmp byte [xb_frontplay], 0
    jne .draw
    cmp si, xb_helpback
    jne .start
    mov si, xb_helpunsupported
    jmp .draw
.start:
    cmp si, xb_frontstart
    jne .draw
    mov si, xb_frontunsupported
.draw:
    cmp si, xb_frontnote        ; the last splash line doubles as the error line
    jne .paint
    cmp byte [xb_error], 0
    je .paint
    mov si, xb_errmode
    cmp byte [xb_error], XB_ERR_MODE
    je .paint
    mov si, xb_errmem
    cmp byte [xb_error], XB_ERR_MEM
    je .paint
    mov si, xb_errfiles
.paint:
    call xb_fronttext
    add di, 8
    jmp .labels
.out: ret

xb_fronttext:
    SAVE
    cmp byte [xb_frontscale], 2
    jne .position
    shl dx, 1
.position:
    cmp dx, [xb_fronttextto]
    jae .out
    mov bx, dx
    add bx, 8
    cmp bx, [xb_fronttextfrom]
    jbe .out
    add cx, [xb_frontx]
    add dx, [xb_fronty]
    cmp byte [xb_frontcolor], 0
    jne .ink
    mov ax, CWHITE
.ink:
    call OSAPI_FONT_RUN
.out:
    RESTORE
    ret

xb_artfiles: dw xb_artvga,EXB_FRONT_VGA_SIZE,xb_artherc,EXB_FRONT_HRC_SIZE,xb_artcga,EXB_FRONT_CGA_SIZE
xb_artvga: db '8BITBIKE.VGA',0
xb_artherc: db '8BITBIKE.HRC',0
xb_artcga: db '8BITBIKE.CGA',0
xb_artmissing: db 'Graphics unavailable: keep 8BITBIKE.* together',0
xb_artloading: db 'LOADING ARTWORK...',0
; label table: x (pixels), y (rows of the 132-row art; doubled on tall
; displays), ink, string.  The menu sits in the black box the art reserves.
xb_splashlabels:
    dw 156,85,CWHITE,xb_frontstart
    dw 156,99,CYELLOW,xb_fronthelp
    dw 136,113,CLCYAN,xb_frontfs
    dw 128,121,CLGREEN,xb_frontnote
    dw -1
xb_helplabels:
    dw 24,12,CYELLOW,xb_helptitle
    dw 24,29,CLRED,xb_helpgoal
    dw 24,40,CWHITE,xb_helplane
    dw 24,51,CLCYAN,xb_helplean
    dw 24,62,CWHITE,xb_helpgo
    dw 24,73,CLGREEN,xb_helpkeys
    dw 24,84,CWHITE,xb_helpmisc
    dw 24,95,CYELLOW,xb_helpfs
    dw 24,106,CLRED,xb_helpprev
    dw 24,118,CLCYAN,xb_helpback
    dw -1
xb_frontstart: db 'ENTER  START RACE',0
xb_fronthelp: db 'H      HOW TO PLAY',0
xb_frontfs: db 'ALT+ENTER  FULL SCREEN',0
xb_frontnote: db 'FIVE COURSES, TWO LAPS',0
xb_frontunsupported: db 'VGA, CGA OR HERC ONLY',0
xb_errmode: db 'FULL SCREEN REFUSED',0
xb_errfiles: db '8BB?.GFX MISSING',0
xb_errmem: db 'NOT ENOUGH MEMORY',0
xb_helptitle: db 'H O W  T O  R I D E',0
xb_helpgoal: db 'BEAT THE CLOCK. WATCH THE HEAT.',0
xb_helplane: db 'UP / DOWN   CHANGE LANE',0
xb_helplean: db 'LEFT / RIGHT   LEAN BACK / FORWARD',0
xb_helpgo: db 'Z  THROTTLE     X  TURBO (HEATS)',0
xb_helpkeys: db 'ENTER  PAUSE     ESC  LEAVE',0
xb_helpmisc: db 'M  SOUND         C  COLOURS',0
xb_helpfs: db 'ALT+ENTER  FULL SCREEN',0
xb_helpprev: db 'A: ALONE  B: CPU RIVALS',0
xb_helpback: db 'H / ESC: BACK    ENTER: PLAY',0
xb_helpunsupported: db 'H / ESC: BACK   VGA/CGA/HERC',0
