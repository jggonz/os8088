; =============================================================================
; os8088 - tests/facetest/facetest.asm
;
; FACETEST: does apps/os88type.inc actually set type? It asks ty_scan what the
; MACHINE has - SYSTEM/FONTS on the system disk, reached through OSAPI_VOL_SYS
; and never named here (SPEC.md 19.8) - opens the first family it listed,
; reports what the header said, and draws the same sentence four ways, one
; under the other, so a single screendump answers every question the library
; raises:
;
;   1  the kernel's 8x8 face through OSAPI_FONT_RUN      - the reference
;   2  face 0 through this library                       - the identity path
;   3  Charter 12, composed the GENERAL way              - ty_put_slow
;   4  Charter 12, composed from the pre-shifted table   - ty_put_fast
;
; ROWS 3 AND 4 MUST BE PIXEL-IDENTICAL. They are two different inner loops
; over the same glyphs (SPEC.md 6.5.2), one of them building its words at
; ty_cache time, and nothing else in this system would notice if they drifted
; apart - a wrong shift in the pre-shifted table is a face that letters
; *almost* right, which is exactly the kind of defect a person stops seeing
; after ten minutes. Comparing them here makes it a diff instead of a
; judgement.
;
; And row 2 against row 1 is the same assertion one level down: face 0 IS the
; kernel's face (SPEC.md 6.5), so lettering it through this library and
; through font_run must put the same pixels on the screen. If it does not, the
; library's compose is wrong and every face it ever sets will be wrong with it.
;
; ...and the SCAN's own answer is not on the screen in a form anything but a
; person can read, so it is also published as four bytes at a fixed offset in
; this package's bss (FT_ST_* at the bottom). tests/facescan.py is the row that
; reads them: `families 10` and `families 1` are the same picture.
;
; Nothing here ships: `make bench` builds it, `all` never does.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FACETEST', ft_entry

FT_ROWY     equ 18                ; pixels between the four specimen rows: the
                                  ; tallest legal face plus a gap, so the rows
                                  ; cannot overlap whatever face is loaded

; -----------------------------------------------------------------------------
; ft_entry - package entry (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment; gfx lock NOT held
; out: BX = window ptr, CF set = refused
; -----------------------------------------------------------------------------
ft_entry:
    push si
    call ty_init                    ; face 0, before anything can ask for it

    call ty_scan                    ; ...then what the MACHINE has (SPEC.md
    mov al, [ty_nfam]               ; 19.8): SYSTEM/FONTS on the system disk,
    mov [ft_nfam], al               ; found through OSAPI_VOL_SYS rather than
    or al, al                       ; named by this harness
    jnz .some
    mov byte [ft_err], 0xFF         ; nothing on the disk at all
    jmp short .nofile
.some:
    xor al, al                      ; the first family it listed
    call ty_openfam
    mov [ft_err], al
    jc .nofile
    mov [ft_face], al
    mov byte [ft_err], 0
    xor ah, ah
    call ty_use
    call ty_cache                   ; the pre-shifted table, or not - either
    mov byte [ft_cached], 1         ; way row 4 draws, and this says which
    jnc .cached
    mov byte [ft_cached], 0
.cached:
    mov al, [ft_face]
    xor ah, ah
    call ty_use
.nofile:
    mov si, ft_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [ft_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; the content origin on a multiple of 8,
    clc                             ; which is what ty_flush's x wants
.out:
    pop si
    ret                             ; NEAR: the kernel reaches every proc here
                                    ; through our own dispatcher (SPEC.md 20.1)

; -----------------------------------------------------------------------------
; ft_paint - W_PAINT: the header lines, then the four specimen rows
; in:  SI = window ptr; gfx lock held; content already white
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov bx, si
    call OSAPI_WM_CONTENT
    mov [ft_cx], ax
    mov [ft_cy], dx
    add ax, 7                       ; the pen: content left rounded UP to a
    and ax, 0xFFF8                  ; multiple of 8, whatever pixel the window
    mov [ft_px], ax                 ; was dragged to

    mov al, CBLACK
    call OSAPI_SET_COLOR

    call ft_head                    ; two lines of plain 8x8 status

    ; --- 1: the kernel's face, through the kernel ---------------------------
    mov cx, [ft_px]
    mov dx, [ft_cy]
    add dx, 24
    mov [ft_ry], dx
    mov si, ft_text
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    ; --- 2: face 0, through this library ------------------------------------
    xor ax, ax
    xor ah, ah
    call ty_use
    call ft_row

    ; --- 3: Charter, the general compose ------------------------------------
    cmp byte [ft_err], 0
    jne .done
    mov al, [ft_face]
    xor ah, ah
    call ty_use
    mov byte [ft_noc], 1            ; force ty_put_slow for this row
    call ft_row

    ; --- 4: ...and the pre-shifted one --------------------------------------
    mov byte [ft_noc], 0
    call ft_row
.done:
    mov byte [ft_noc], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_row - one specimen row of the CURRENT face at the next y
; in:  [ft_px], [ft_ry]; gfx lock held
; out: [ft_ry] advanced; preserves all registers
;
; ty_band, ty_put, ty_flush - the whole of SPEC.md 6.3's method, and the only
; three calls a caller makes to set a line.
; -----------------------------------------------------------------------------
ft_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    add word [ft_ry], FT_ROWY

    call ty_getrows
    xor ah, ah
    mov dx, ax
    mov [ft_rows], dx
    call ty_band                    ; paper, a whole stride wide

    push ds                         ; ES:SI = the text, which is ours here but
    pop es                          ; is a caller's own segment in general
    mov si, ft_text
    xor ax, ax
    cmp byte [ft_noc], 0            ; the forced-general row asks for an ODD
    je .even                        ; pen, which is the documented way to miss
    mov ax, 1                       ; the table (SPEC.md 6.5.2) without
.even:                              ; tearing the cache down and rebuilding it
    call ty_put

    mov ax, [ft_px]
    mov bx, [ft_ry]
    mov cx, TY_STRIDE * 8           ; the whole band: it is paper where the
    cmp cx, 624                     ; text is not, so this both draws the line
    jbe .w                          ; and erases what was under it
    mov cx, 624
.w:
    mov dx, [ft_rows]
    call ty_flush

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_head - two status lines in the 8x8 face: what opened, and what it said
; -----------------------------------------------------------------------------
ft_head:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov cx, [ft_cx]
    add cx, 4
    mov dx, [ft_cy]
    add dx, 3
    mov si, ft_s_t1
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN

    mov di, ft_s_t2 + 6             ; "face: " - the family, or the error.
                                    ; BUILT LEFT TO RIGHT and not written at
                                    ; fixed offsets: the family name is
                                    ; whatever the scan found, so nothing
                                    ; after it sits at a known column
    cmp byte [ft_err], 0
    je .ok
    mov si, ft_s_bad
    call ft_copy
    mov al, [ft_err]
    add al, '0'
    mov [di], al
    inc di
    jmp short .draw
.ok:
    xor al, al
    call ty_famname                 ; the name the SCAN found, not one built
    call ft_copy                    ; into this program
    mov si, ft_s_rows
    call ft_copy
    call ty_getrows
    call ft_num
    mov si, ft_s_cach
    call ft_copy
    mov al, [ft_cached]
    add al, '0'
    mov [di], al
    inc di
    mov si, ft_s_of
    call ft_copy
    mov al, [ft_nfam]
    call ft_num
.draw:
    mov cx, [ft_cx]
    add cx, 4
    mov dx, [ft_cy]
    add dx, 12
    mov si, ft_s_t2
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ft_num - AL = 0..99 appended at DI as decimal; DI advances. Preserves AX.
ft_num:
    push ax
    push bx
    xor ah, ah
    mov bl, 10
    div bl                          ; AL = tens, AH = units
    or al, al
    jz .u
    add al, '0'
    mov [di], al
    inc di
.u:
    mov al, ah
    add al, '0'
    mov [di], al
    inc di
    pop bx
    pop ax
    ret

; ft_copy - SI = a NUL string, DI = where; copies the text without its NUL
ft_copy:
    push ax
.c:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc si
    inc di
    jmp .c
.out:
    pop ax
    ret

ft_onkey:
    ret
ft_onclick:
    ret

%include "os88type.inc"

; --- data --------------------------------------------------------------------

ft_tpl:
    dw 7, 40, 632, 120
    dw ft_ttl, ft_paint, ft_onkey, ft_onclick

ft_ttl:     db 'Face Test', 0
ft_fname:   db 'CHARTER.F88', 0

; The specimen. Ascenders, descenders, capitals, figures and the pairs a
; proportional face gets wrong first - 'rn' against 'm', 'l' against '1'.
ft_text:    db 'Hamburgefonstiv 0123 rn/m Il1 - The quick brown fox.', 0

ft_s_t1:    db 'FACETEST: 1 font_run 8x8   2 face 0   3 general   4 preshift', 0
ft_s_t2:    db 'face:                                     ', 0
ft_s_rows:   db ', rows ', 0
ft_s_cach:   db ', cached ', 0
ft_s_of:     db ', families ', 0
ft_s_bad:   db 'NOT OPEN, ty said ', 0

ft_win:     dw 0
ft_cx:      dw 0
ft_cy:      dw 0
ft_px:      dw 0
ft_ry:      dw 0
ft_rows:    dw 8
ft_noc:     db 0

FT_BSS_OWN  equ 8

    OS88_BSS FT_BSS_OWN + TY_BSS_SIZE
    OS88_IMAGE_END

; --- THE STATUS BLOCK, at a fixed offset from os88_image_end -----------------
; What ty_scan found is the whole question this package exists to ask, and a
; screendump cannot answer it: "families 10" and "families 1" are the same
; picture to anything but a person reading the line. So the four bytes live at
; a CONSTANT offset in this package's own bss - the tests/ftpd.py idiom - and
; tests/facescan.py reads them out of the guest through the window's segment.
;
; BSS AND NOT `db 0` IN THE IMAGE, which is what they were: the assembler puts
; a db wherever the code above it ended, so the offset moved with every edit to
; this file and a host-side reader had no way to name it. Nothing is lost -
; bss arrives zeroed (SPEC.md 20.3), which is what each of them was.
FT_ST_NFAM  equ 0               ; byte: families ty_scan listed
FT_ST_ERR   equ 1               ; byte: ty_openfam's answer; 0 = a face is open,
                                ; 0xFF = the scan found nothing at all
FT_ST_FACE  equ 2               ; byte: the handle it opened
FT_ST_CACHE equ 3               ; byte: ty_cache built the pre-shifted table

ft_own      equ os88_image_end + 0
ft_nfam     equ ft_own + FT_ST_NFAM
ft_err      equ ft_own + FT_ST_ERR
ft_face     equ ft_own + FT_ST_FACE
ft_cached   equ ft_own + FT_ST_CACHE
    TY_BSS os88_image_end + FT_BSS_OWN
