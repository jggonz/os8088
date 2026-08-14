; =============================================================================
; os8088 - tests/fsxtest/fsxtest.asm
;
; FSXTEST: the fullscreen-exclusive gate package (SPEC.md 53.9). NEVER
; shipped - the Makefile builds it into its own scratch image,
; build/fsxtest.img, mounted with `make test TESTAPPS=build/fsxtest.img`.
;
; What each input proves:
;   keys '0'..'8'  enter the bracket, set that FSXM mode, draw an
;                  identifying pattern out of the FSI info block alone
;                  (nothing here hard-codes a geometry), wait for a key,
;                  return - the desktop must come back pixel-identical.
;                  A mode the adapter lacks comes straight back with 'R'
;                  in the window: the caps bit refused it, SPEC.md 47's
;                  one-predicate rule exercised end to end.
;   key 'x'        the bracket with NO mode switch: the drawing slots stay
;                  legal (SPEC.md 53.7), so a black rect goes up via
;                  OSAPI_GFX_FILL, a key returns, and the restore repaint
;                  must erase it.
;   key 'v'        inside is implicit: every graphics mode calls
;                  OSAPI_FSX_WAIT AL=1 once before drawing, so a hung
;                  retrace poll would hang the gate visibly.
;   the window     shows the OSAPI_FSX_CAPS mask in hex - compare against
;                  SPEC.md 53.4's table for the booted adapter.
;
; Result char in the window: '-' idle, 'K' the last bracket ran a mode,
; 'R' fsx_mode refused, 'F' fsx_run itself refused, 'S' same-mode ran.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FSXTEST', fx_entry

FX_CONT_W equ 198                   ; content: 200 outer - 2px borders
FX_CONT_H equ 59

; -----------------------------------------------------------------------------
; fx_entry - create the window, bank the caps answer for the painter
; -----------------------------------------------------------------------------
fx_entry:
    push si
    xor bx, bx                      ; no window yet, so the frontmost - which
    call OSAPI_FSX_CAPS             ; on a one-display machine is every
    mov [fx_caps], ax               ; window there is. AX = mask, DL = the
    mov [fx_kind], dl               ; answering display's kind (39.18.2)
    mov byte [fx_res], '-'          ; bss arrives zeroed and a NUL here
    mov si, fx_tpl                  ; would truncate the caps line
    call OSAPI_WM_CREATE
    pop si
    ret

; -----------------------------------------------------------------------------
; fx_paint - W_PAINT: caps mask, result char, key legend
; in:  SI = window ptr; lock held; content already white
; -----------------------------------------------------------------------------
fx_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [fx_caps]               ; render the mask into the caps line
    mov bx, fx_s_caps+5
    call fx_hex4
    mov al, [fx_res]
    mov [fx_s_caps+12], al
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 6
    mov al, CBLACK
    call OSAPI_SET_COLOR
    add dx, 10
    mov si, fx_s_caps
    call OSAPI_FONT_STR
    add dx, 16
    mov si, fx_s_key1
    call OSAPI_FONT_STR
    add dx, 12
    mov si, fx_s_key2
    call OSAPI_FONT_STR
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fx_hex4 - AX as four hex chars at DS:BX
fx_hex4:
    push ax
    push cx
    push dx
    mov dx, ax
    mov cx, 4
.dig:
    push cx
    mov cl, 4                       ; roll the top nibble around to the low
    rol dx, cl
    pop cx
    mov al, dl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .st
    add al, 'A'-'0'-10
.st:
    mov [bx], al
    inc bx
    loop .dig
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fx_onkey - W_ONKEY: '0'..'8' = bracket + that mode; 'x' = same-mode bracket
; in:  AL = ascii, AH = scan, SI = window ptr; lock held
; -----------------------------------------------------------------------------
fx_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp al, 't'
    je .tone
    cmp al, 'x'
    je .same
    cmp al, '0'
    jb .out
    cmp al, '8'
    ja .out
    sub al, '0'
.go:
    mov [fx_want], al
    mov bx, si                      ; BX = our window, AX = the exclusive
    mov ax, fx_main                 ; main, CX = no flags (SPEC.md 53.1)
    xor cx, cx
    call OSAPI_FSX_RUN
    jnc .repaint
    mov byte [fx_res], 'F'          ; the bracket itself refused
    jmp short .repaint
.same:
    mov al, 0xFF                    ; 0xFF = no mode switch (the 'x' leg)
    jmp short .go
.tone:
    mov ax, 880                     ; a DURATION-0 tone: the SPEC.md 53.3
    xor cx, cx                      ; legs - another instance's bracket must
    mov dl, 0x40                    ; silence it at entry; our own must
    call OSAPI_SND_TONE             ; carry it through unbroken
    jmp short .out
.repaint:
    mov al, CWHITE                  ; white-fill own content + redraw (the
    call OSAPI_SET_COLOR            ; files.inc idiom - lock already held)
    mov bx, si
    call OSAPI_WM_CONTENT
    mov bx, dx
    mov cx, ax
    add cx, FX_CONT_W-1
    add dx, FX_CONT_H-1
    call OSAPI_GFX_FILL
    call fx_paint
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fx_main - the exclusive main (SPEC.md 53.1): SI = our window, ES =
;           KERNEL_SEG, DS = CS = our segment, gfx lock held, world frozen.
;           Returns (near) when the test pattern has been acknowledged.
; -----------------------------------------------------------------------------
fx_main:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov al, [fx_want]
    cmp al, 0xFF
    je .samemode
    push ds                         ; the info block is ours: ES = DS
    pop es
    mov di, fx_info
    call OSAPI_FSX_MODE
    jnc .have
    mov byte [fx_res], 'R'          ; refused - straight home, and the
    jmp .done                       ; restore must repaint an untouched
.have:                              ; desktop
    mov byte [fx_res], 'K'
    test byte [fx_info+FSI_FLAGS], FSIF_TEXT
    jnz .text
    mov al, 1                       ; graphics: one bounded retrace wait
    call OSAPI_FSX_WAIT             ; first - a hang here IS the finding
    test byte [fx_info+FSI_FLAGS], FSIF_PLANAR
    jz .nomask
    mov dx, 0x3C4                   ; planar: write through all four planes
    mov ax, 0x0F02
    out dx, ax
.nomask:
    mov es, [fx_info+FSI_SEG]
    cmp byte [fx_info+FSI_BPP], 8
    je .grad
    ; 1/2/4 bpp: a flat texture the whole surface long - banked sizes are
    ; banks*bstep (the interleave holes included), linear ones h*stride
    mov al, [fx_info+FSI_BANKS]
    cmp al, 1
    jbe .linsz
    xor ah, ah
    mov cx, [fx_info+FSI_BSTEP]
    mul cx                          ; AX = banks * bstep
    jmp short .fill
.linsz:
    mov ax, [fx_info+FSI_H]
    mov cx, [fx_info+FSI_STRIDE]
    mul cx                          ; AX = h * stride (all < 64KB here)
.fill:
    mov cx, ax
    shr cx, 1                       ; words
    mov al, [fx_want]
    mov ah, 0x11
    mul ah
    add al, 0x33                    ; a per-mode byte: distinct textures
    mov ah, al
    xor di, di
    cld
    rep stosw
    mov cx, [fx_info+FSI_STRIDE]    ; and a solid band up top (rows 0..3 of
    shl cx, 1                       ; the first bank when interleaved)
    shl cx, 1
    xor di, di
    mov al, 0xFF
    rep stosb
    jmp short .wait
.grad:                              ; 8 bpp: a row gradient - the palette
    xor di, di                      ; itself on 13h, colour quads on Mode X
    xor bx, bx                      ; BX = y
    mov dx, [fx_info+FSI_H]
.grow:
    mov al, bl
    mov cx, [fx_info+FSI_STRIDE]
    cld
    rep stosb
    inc bx
    cmp bx, dx
    jb .grow
    jmp short .wait
.text:
    mov es, [fx_info+FSI_SEG]       ; text: the id character over every
    mov ax, [fx_info+FSI_W]         ; cell, one attribute per 16 columns
    mov cx, [fx_info+FSI_H]
    mul cx
    mov cx, ax                      ; CX = cells
    mov al, [fx_want]
    add al, '0'
    mov ah, 0x1E                    ; yellow on blue - reduces fine on mono
    xor di, di
    cld
    rep stosw
.wait:
    xor ah, ah                      ; any key acknowledges the pattern -
    int 0x16                        ; blocking BIOS read, UI-task context
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.samemode:
    mov byte [fx_res], 'S'          ; no mode switch: the drawing slots are
    mov al, CBLACK                  ; still legal (SPEC.md 53.7) - a black
    call OSAPI_SET_COLOR            ; rect the restore repaint must erase
    mov ax, 60
    mov bx, 60
    mov cx, 260
    mov dx, 160
    call OSAPI_GFX_FILL
    jmp short .wait

; --- window template (SPEC.md 11: 16 bytes, 8 words) -------------------------
fx_tpl:
    dw 300, 60, 200, 78             ; x, y, w, h -> content 198 x 59
    dw fx_ttl, fx_paint, fx_onkey, 0

fx_ttl:     db 'FSX Test', 0
fx_s_caps:  db 'CAPS=....  [-]', 0  ; +5 the mask, +12 the result char
fx_s_key1:  db '0-8: mode bracket', 0
fx_s_key2:  db 'x: same-mode', 0

FX_BSS_TOTAL equ 21

    OS88_BSS FX_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
fx_caps equ os88_image_end + 0      ; word: the OSAPI_FSX_CAPS answer
fx_kind equ os88_image_end + 2      ; byte: vid_kind
fx_want equ os88_image_end + 3      ; byte: the id the next bracket sets
fx_res  equ os88_image_end + 4      ; byte: '-' / 'K' / 'R' / 'F' / 'S'
fx_info equ os88_image_end + 5      ; FSI_SIZE bytes
                                    ; total 21 = FX_BSS_TOTAL
