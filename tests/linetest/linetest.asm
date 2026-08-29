; =============================================================================
; os8088 - tests/linetest/linetest.asm
;
; LINETEST: the gate for SPEC.md 5.6.6, the 1bpp three-column walk. That
; optimisation claims a dilated STEEP line is the identical pixel set drawn a
; third as often, and "identical" is the whole of it - so this package draws a
; deterministic fan of dilated lines and nothing else, and the gate is a
; framebuffer dump compared BYTE FOR BYTE against the same fan drawn by a
; kernel built without the change.
;
;   make bench   (or just: make test ... TESTAPPS=build/linetest.img)
;   make test VIDEO=herc HERCSEG=0x7000 TESTAPPS=build/linetest.img
;   ...launch it, then
;   python3 tools/qmp.py build/qmp.sock 'pmemsave 0x70000 0x8000 "/abs/a.bin"'
;
; Nothing here is random and nothing depends on the clock: the same build
; draws the same pixels every run, which is what makes the comparison mean
; something. The fan sweeps every steep slope the walk can take and BOTH
; directions of x, and its feet step one pixel at a time across several byte
; boundaries - the three columns can straddle two framebuffer bytes, and that
; is the case a mask-based walk gets wrong.
;
; Every proc is a near proc with a near ret: the kernel reaches a callback
; through the dispatcher in the package's own header (SPEC.md 20.1).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'LINETEST', lt_entry

LT_APEXY  equ 4                     ; the fan's top row, in content coordinates
LT_FOOTX  equ 20                    ; ...and where its feet start
LT_FOOTD  equ 11                    ; the gap between feet: coprime with 8, so
                                    ; 24 of them land on every residue mod the
                                    ; framebuffer byte - and wide enough that
                                    ; no two lines touch, or a wrong pixel in
                                    ; one could be covered by its neighbour
LT_APEXD  equ 3                     ; the apex walks too: a sweep of slopes
LT_NFAN   equ 24
LT_NVEC   equ 4                     ; walks handed to LSTEPV at once
LT_VSTR   equ 16                    ; ...at this stride: GLS_SZ up to a shift
%if GLS_SZ > LT_VSTR
  %error "GLS_SZ no longer fits LT_VSTR"
%endif
LT_REPS   equ 20                    ; fans per timed run

; -----------------------------------------------------------------------------
; lt_entry - package entry point (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, from wm_create)
; -----------------------------------------------------------------------------
lt_entry:
    push si
    mov si, lt_tpl
    call OSAPI_WM_CREATE
    pop si
    ret

; -----------------------------------------------------------------------------
; lt_paint - W_PAINT: the whole fan, every time
; in:  SI = window ptr; gfx lock held; content already filled
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
lt_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [lt_win], si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [lt_ox], ax
    mov [lt_oy], dx
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    mov [lt_cw], cx
    mov [lt_ch], dx

    mov al, CBLACK                  ; black behind, so a line and its erase are
    call OSAPI_SET_COLOR            ; both changes a dump can see
    mov ax, [lt_ox]
    mov bx, [lt_oy]
    mov cx, ax
    add cx, [lt_cw]
    dec cx
    mov dx, bx
    add dx, [lt_ch]
    dec dx
    call OSAPI_GFX_FILL

    ; --- the fan ------------------------------------------------------------
    ; dy is the content height and dx at most LT_NFAN, so every line is STEEP:
    ; the case 5.6.6 claims. The apex walks as well as the foot, so this is a
    ; sweep of slopes rather than one pencil through a single pixel.
    call lt_fan

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; lt_fan - the fan alone, so lt_onkey can time exactly what the gate compares
lt_fan:
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    xor di, di
.fan:
    call lt_ends                    ; left-leaning: apex left of foot
    call lt_line
    inc di
    cmp di, LT_NFAN
    jb .fan

    xor di, di
.fan2:
    call lt_ends                    ; ...and the mirror, so the walk takes
    xchg ax, cx                     ; sx = -1 as well
    call lt_line
    inc di
    cmp di, LT_NFAN
    jb .fan2
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; lt_ends - fan line DI's endpoints
; out: AX,BX = apex   CX,DX = foot; clobbers AX/BX/CX/DX
lt_ends:
    mov ax, di
    mov cl, LT_APEXD
    mul cl                          ; DI < 256, so AL * CL is the whole of it
    add ax, 6
    mov bx, LT_APEXY
    push ax
    mov ax, di
    mov cl, LT_FOOTD
    mul cl
    add ax, LT_FOOTX
    mov cx, ax
    pop ax
    mov dx, [lt_ch]
    sub dx, 5
    ret

lt_chunks:  dw 0, 1, 2, 3, 7

; lt_segfan - the whole fan again, through the resumable walk
lt_segfan:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [lt_erase], 0      ; an erase pass must NOT clear first - the
    jne .noclr                  ; whole point is that the replay does it
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [lt_ox]
    mov bx, [lt_oy]
    mov cx, ax
    add cx, [lt_cw]
    dec cx
    mov dx, bx
    add dx, [lt_ch]
    dec dx
    call OSAPI_GFX_FILL
.noclr:
    mov al, CWHITE
    cmp byte [lt_erase], 0
    je .pen
    mov al, CBLACK              ; the replay puts the sky back
.pen:
    call OSAPI_SET_COLOR
    xor di, di
.f:
    call lt_ends
    call lt_seg
    inc di
    cmp di, LT_NFAN
    jb .f
    xor di, di
.f2:
    call lt_ends
    xchg ax, cx
    call lt_seg
    inc di
    cmp di, LT_NFAN
    jb .f2
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; lt_seg - the same line through SPEC.md 5.6.7's resumable walk, drawn in
; chunks of CH pixels. CH = 0 means one call for the whole thing - so the
; gate is "every chunk size draws the identical pixel set", which is the
; contract's whole content.
; in:  AX,BX = x1,y1  CX,DX = x2,y2; [lt_chunk] = the chunk; preserves all
lt_seg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    add ax, [lt_ox]
    add cx, [lt_ox]
    add bx, [lt_oy]
    add dx, [lt_oy]
    mov si, cx                  ; the walk length is max(|dx|,|dy|) + 1
    sub si, ax
    jns .dxa
    neg si
.dxa:
    mov di, dx
    sub di, bx
    jns .dya
    neg di
.dya:
    cmp si, di
    jae .n
    mov si, di
.n:
    inc si                      ; SI = pixels in the whole walk
    push ds
    pop es
    mov di, lt_gls
    call OSAPI_GFX_LINIT
.chunks:
    or si, si
    jz .done
    mov cx, [lt_chunk]
    or cx, cx
    jz .all
    cmp cx, si
    jbe .go
.all:
    mov cx, si
.go:
    sub si, cx
    push si
    push ds
    pop es
    mov di, lt_gls
    call OSAPI_GFX_LSTEP
    pop si
    jmp short .chunks
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; lt_vecfan - the whole fan again, LT_NVEC walks at a time through
;             OSAPI_GFX_LSTEPV (SPEC.md 5.6.8)
;
; The gate for the vector step, and it is the same gate as everything else
; here: identical pixels. It has to run several walks CONCURRENTLY, because a
; vector call with one descriptor only proves the scalar case - what the
; contract actually claims is that N walks handed over together are the N
; separate calls, unaffected by each other's staging.
; -----------------------------------------------------------------------------
lt_vecfan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [lt_erase], 0
    jne .noclr
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [lt_ox]
    mov bx, [lt_oy]
    mov cx, ax
    add cx, [lt_cw]
    dec cx
    mov dx, bx
    add dx, [lt_ch]
    dec dx
    call OSAPI_GFX_FILL
.noclr:
    mov al, CWHITE
    cmp byte [lt_erase], 0
    je .pen
    mov al, CBLACK
.pen:
    call OSAPI_SET_COLOR
    mov word [lt_vidx], 0
.group:
    call lt_vlay                    ; lay up to LT_NVEC walks; CX = how many
    jcxz .out
    mov [lt_vn], cx
.round:
    call lt_vbuild                  ; CX = descriptors with work left
    jcxz .group
    push ds
    pop es
    mov di, lt_vdsc
    call OSAPI_GFX_LSTEPV
    jmp short .round
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; lt_vlay - lay the next LT_NVEC fan lines into the walk blocks
; out: CX = how many were laid (0 = the fan is finished)
lt_vlay:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    xor si, si                      ; SI = the block index
.each:
    cmp si, LT_NVEC
    jae .done
    mov di, [lt_vidx]
    cmp di, LT_NFAN * 2
    jae .done
    inc word [lt_vidx]
    cmp di, LT_NFAN                 ; the second half is the mirror, so the
    jb .ends                        ; walk takes sx = -1 as well
    sub di, LT_NFAN
    call lt_ends
    xchg ax, cx
    jmp short .have
.ends:
    call lt_ends
.have:
    add ax, [lt_ox]
    add cx, [lt_ox]
    add bx, [lt_oy]
    add dx, [lt_oy]
    push cx
    push dx
    mov di, cx                      ; the walk length is max(|dx|,|dy|) + 1
    sub di, ax
    jns .dxa
    neg di
.dxa:
    push di
    mov di, dx
    sub di, bx
    jns .dya
    neg di
.dya:
    pop cx
    cmp cx, di
    jb .n
    mov di, cx
.n:
    inc di
    mov cx, si
    add cx, cx
    push bx
    mov bx, cx
    mov [lt_vlen + bx], di
    pop bx
    pop dx
    pop cx
    push cx
    mov di, si                      ; DI = this walk's block
    mov cl, 4
    shl di, cl
    add di, lt_vgls
    pop cx
    push ds
    pop es
    call OSAPI_GFX_LINIT
    inc si
    jmp short .each
.done:
    mov cx, si
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; lt_vbuild - one round of descriptors for the walks that still owe pixels
; out: CX = descriptors written to lt_vdsc
lt_vbuild:
    push ax
    push bx
    push dx
    push si
    push di
    xor si, si                      ; SI = walk index
    xor di, di                      ; DI = descriptor byte offset
.each:
    cmp si, [lt_vn]
    jae .done
    mov bx, si
    add bx, bx
    mov ax, [lt_vlen + bx]
    or ax, ax
    jz .next
    mov dx, [lt_chunk]              ; 0 = the whole remaining walk
    or dx, dx
    jz .all
    cmp dx, ax
    jb .take
.all:
    mov dx, ax
.take:
    sub ax, dx
    mov [lt_vlen + bx], ax
    mov bx, si
    mov cl, 4
    shl bx, cl
    add bx, lt_vgls
    mov [lt_vdsc + di], bx
    mov [lt_vdsc + di + 2], dx
    add di, 4
.next:
    inc si
    jmp short .each
.done:
    mov cl, 2                       ; DI bytes / 4 = descriptors
    shr di, cl
    mov cx, di
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; lt_line - one DILATED line in content coordinates
; in:  AX,BX = x1,y1  CX,DX = x2,y2; preserves every register
lt_line:
    push ax
    push bx
    push cx
    push dx
    push si
    add ax, [lt_ox]
    add cx, [lt_ox]
    add bx, [lt_oy]
    add dx, [lt_oy]
    mov si, 1                       ; DILATED - the whole point of the gate
    call OSAPI_GFX_LINE
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; lt_onkey - any key: draw the fan LT_REPS times and report the PIT counts it
; took. The gate answers "same pixels"; this answers "how much cheaper", and
; the two have to be asked of the same drawing or neither means anything.
; -----------------------------------------------------------------------------
lt_onkey:
    cmp al, '0'                 ; 0..4: redraw the fan through the RESUMABLE
    jb .bench                   ; walk, in chunks of 0/1/2/3/7 pixels. Every
    cmp al, '7'                 ; one must produce the identical framebuffer;
    ja .bench                   ; 5 draws the fan and then ERASES it by
                                ; replaying the same walks in the background
                                ; colour, and the content must come back
                                ; completely blank - the property a trail
                                ; erase actually depends on. 6 and 7 are 3 and
                                ; 1 again through OSAPI_GFX_LSTEPV, LT_NVEC
                                ; walks to a call (SPEC.md 5.6.8) - so 6 must
                                ; match 3 and 7 must match 1, byte for byte
    push ax
    push bx
    push si
    sub al, '0'
    mov bl, al
    xor bh, bh
    cmp bl, 5
    je .erase
    ja .vec
    mov ax, [lt_chunks + bx]
    mov [lt_chunk], ax
    call lt_segfan
    jmp short .kdone
.vec:
    mov word [lt_chunk], 3
    cmp bl, 7
    jne .vgo
    mov word [lt_chunk], 1
.vgo:
    call lt_vecfan
    jmp short .kdone
.erase:
    mov word [lt_chunk], 3      ; drawn in chunks, erased in different ones:
    call lt_segfan              ; if the two disagreed anywhere, a pixel
    mov word [lt_chunk], 5      ; would survive
    mov byte [lt_erase], 1
    call lt_segfan
    mov byte [lt_erase], 0
.kdone:
    pop si
    pop bx
    pop ax
    ret
.bench:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [lt_win]
    call lt_now
    mov [lt_t0], ax
    mov [lt_t0+2], dx
    mov cx, LT_REPS
.rep:
    push cx
    call lt_fan
    pop cx
    loop .rep
    call lt_now
    sub ax, [lt_t0]
    sbb dx, [lt_t0+2]
    mov [lt_res], ax
    mov [lt_res+2], dx
    call lt_report
    call lt_write
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; lt_write - the count to LINE.TXT on the current volume. tests/gfxbench and
; tests/sysbench write their reports to a file for the room; this one does it
; because a number READ BACK OFF THE FRAMEBUFFER is a number you can misread,
; and this harness produced a 10x-wrong figure that way before it wrote one.
lt_write:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, ds
    mov es, ax
    mov di, lt_buf
    mov ax, [lt_res]
    mov dx, [lt_res+2]
    call lt_dec
    mov al, 13
    stosb
    mov al, 10
    stosb
    mov cx, di
    sub cx, lt_buf
    mov bx, lt_buf
    xor dx, dx
    mov si, lt_s_name
    call OSAPI_FILE_WRITE
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; lt_dec - DX:AX as decimal at ES:DI, DI left past the last digit
lt_dec:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, lt_num + 11
    mov byte [si], 0
    mov bx, 10
.d:
    mov cx, ax
    mov ax, dx
    xor dx, dx
    div bx
    push ax
    mov ax, cx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    pop dx
    mov cx, ax
    or ax, dx
    mov ax, cx
    jnz .d
.c:
    mov al, [si]
    or al, al
    jz .e
    stosb
    inc si
    jmp short .c
.e:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; lt_now - DX:AX = ticks:position, the missile-logger clock (PERFORMANCE.md)
lt_now:
    push cx
.retry:
    call OSAPI_GET_TICKS
    mov cx, ax
    call lt_pit
    neg ax
    push ax
    call OSAPI_GET_TICKS
    pop dx
    cmp ax, cx
    jne .retry
    xchg ax, dx
    pop cx
    ret

lt_pit:
    push dx
    mov al, 0
    out 0x43, al
    jmp short $+2
    in al, 0x40
    mov dl, al
    jmp short $+2
    in al, 0x40
    mov ah, al
    mov al, dl
    pop dx
    ret

; lt_report - the count, as decimal, over the fan
lt_report:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [lt_ox]
    mov bx, [lt_oy]
    mov cx, ax
    add cx, 130
    mov dx, bx
    add dx, 9
    call OSAPI_GFX_FILL
    mov ax, [lt_res]
    mov dx, [lt_res+2]
    mov di, lt_buf + 11
    mov byte [di], 0
    mov bx, 10
.dig:
    mov cx, ax
    mov ax, dx
    xor dx, dx
    div bx
    mov si, ax
    mov ax, cx
    div bx
    add dl, '0'
    dec di
    mov [di], dl
    mov dx, si
    mov cx, ax
    or ax, dx
    mov ax, cx
    jnz .dig
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov si, di
    mov cx, [lt_ox]
    add cx, 2
    mov dx, [lt_oy]
    add dx, 1
    mov ax, (CBLACK << 8) | CWHITE  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
lt_onclick:
    ret

lt_tpl:
    dw 40, 40, 320, 200
    dw lt_ttl, lt_paint, lt_onkey, lt_onclick

lt_erase:   db 0                ; the fan pass is a replay in the background
lt_chunk:   dw 0                ; pixels per LSTEP call; 0 = the whole walk
lt_gls:     times GLS_SZ db 0
lt_vgls:    times LT_VSTR * LT_NVEC db 0    ; the concurrent walks, and the
lt_vdsc:    times 4 * LT_NVEC db 0          ; array handed to LSTEPV
lt_vlen:    times 2 * LT_NVEC db 0          ; pixels each still owes
lt_vidx:    dw 0                            ; the fan line laid next
lt_vn:      dw 0
lt_ttl:     db 'Line Test', 0
lt_ox:      dw 0
lt_oy:      dw 0
lt_cw:      dw 0
lt_ch:      dw 0
lt_win:     dw 0
lt_t0:      dw 0, 0
lt_res:     dw 0, 0
lt_buf:     times 24 db 0
lt_num:     times 12 db 0
lt_s_name:  db 'LINE.TXT', 0

    OS88_IMAGE_END
