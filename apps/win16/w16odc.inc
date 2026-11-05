; =============================================================================
; os8088 - apps/win16/w16odc.inc
;
; OWN DCs (SPEC.md 124.25.11). A window of a CS_OWNDC class has a DC of its
; own, with a handle of its own, W16_ODC0 + 2 * slot, and its state kept from
; one use to the next in a record of a block in the global heap: a word
; naming the window, then the state as w16_dchide holds it. A call that names
; one makes it the open DC first (w16od_res); the open DC it displaces goes
; back to its record, or onto the stack of DCs under (the shared DC's).
; THE CODE IS THE LIBRARY PART'S (SPEC.md 124.19.3): reached from the image by
; LCALL, and its calls into the image are ICALL
; =============================================================================

CS_OWNDC    equ 0x0020
CS_CLASSDC  equ 0x0040
W16_NODC    equ 4                   ; own DCs at once: Write's four windows
W16_ODC0    equ 0x3040              ; an own DC: W16_ODC0 + 2 * slot
W16OD_SZ    equ 2 + W16_DCSZ        ; a record: its window, then its state

section .w16dlg

; w16od_slot - AL = a slot -> ES:DI = its record. Keeps the rest. AL ALONE:
; w16od_in and w16od_out load it from the byte [w16_odcur] and leave AH as
; their caller had it, and a word multiply then aimed the record 8 KB past
; the block (AH = 10h), over Write 3.0's code segment (SPEC.md 124.25.11)
%if W16OD_SZ > 255
  %error "w16od_slot: a record is a byte multiply's"
%endif
w16od_slot:
    push ax
    push dx
    mov dl, W16OD_SZ
    mul dl
    mov di, ax
    W16SEG es, [w16_odseg]
    pop dx
    pop ax
    ret

; w16od_put - ES:DI = a record: the open DC's state into it. Keeps all.
w16od_put:
    push cx
    push si
    push di
    mov si, w16_dchide
    inc di
    inc di
    mov cx, W16_DCSZ
    cld
    rep movsb
    pop di
    pop si
    pop cx
    ret

; w16od_take - ES:DI = a record: its state the open DC's. Keeps all.
w16od_take:
    push cx
    push si
    push di
    push ds
    push es
    lea si, [di + 2]
    mov di, w16_dchide
    push es
    push ds
    pop es                          ; ES = WIN16's image,
    pop ds                          ;   DS = the block
    mov cx, W16_DCSZ
    cld
    rep movsb
    pop es
    pop ds
    pop di
    pop si
    pop cx
    ret

; w16od_find - BX = a window. CF=0: it has an own DC, AX = its slot, made now
; with Windows' defaults if it had none. CF=1: it has not - its class is not
; CS_OWNDC or CS_CLASSDC, or every slot is taken, or there is no block.
; Keeps the rest.
w16od_find:
    push cx
    push di
    push es
    cmp byte [bx + W16W_CLS], 0xFF  ; WIN16's own classes have none
    je .no
    mov al, [bx + W16W_CLS]
    ICALL w16_clsget
    mov di, ax
    test byte [di + W16C_STYLE], CS_OWNDC | CS_CLASSDC
    jz .no
    cmp word [w16_odseg], 0
    jne .have
    push bx
    push dx
    mov ax, W16_NODC * W16OD_SZ
    xor dx, dx
    mov cl, GMEM_ZEROINIT           ; a fixed block, every record free
    ICALL w16_gnew
    or ax, ax
    jz .nob
    mov [w16_odseg], dx
.nob:
    pop dx
    pop bx
    jz .no
.have:
    xor ax, ax                      ; its own already?
.s:
    call w16od_slot
    cmp [es:di], bx
    je .yes
    inc ax
    cmp ax, W16_NODC
    jb .s
    xor ax, ax                      ; ...else a free one
.f:
    call w16od_slot
    cmp word [es:di], 0
    je .new
    inc ax
    cmp ax, W16_NODC
    jb .f
.no:
    stc
    jmp short .out
.new:
    mov [es:di], bx
    push si
    push di                         ; Windows' defaults, made in the open
    mov si, w16_dchide              ;   DC and the open DC's put back
    mov di, w16_mdsave
    ICALL w16_dccopy
    pop di
    ICALL w16_dcdef
    mov [w16_dcwnd], bx
    mov byte [w16_dcpaint], 0
    mov byte [w16_pfmode], ALTERNATE
    mov byte [w16_sbmode], 1        ; BLACKONWHITE
    call w16od_put
    push di
    mov si, w16_mdsave
    mov di, w16_dchide
    ICALL w16_dccopy
    pop di
    pop si
.yes:
    clc
.out:
    pop es
    pop di
    pop cx
    ret

; w16od_in - AX = a slot, DL = 1 with BeginPaint's clip, 0 the whole client:
; its DC the open one, armed. CF=1: no room for the DC it displaces, and
; nothing changed. Keeps every register.
w16od_in:
    push ax
    push bx
    push di
    push es
    mov bl, al
    inc bl                          ; BL = [w16_odcur]'s value for it
    cmp [w16_odcur], bl
    jne .switch
    or dl, dl                       ; open already: only a paint arms again
    jz .ok
    jmp short .arm
.switch:
    push ax
    mov al, [w16_odcur]
    or al, al
    jz .shared
    dec al                          ; another own DC's: back to its record
    call w16od_slot
    call w16od_put
    pop ax
    jmp short .load
.shared:
    pop ax
    mov byte [w16_odson], 0
    cmp byte [w16_dcon], 0
    je .none
    ICALL w16_dcpush                ; the shared DC's: under it
    jc .fail
    mov byte [w16_odson], 1
    jmp short .load
.none:
    mov byte [w16_dcon], 1
.load:
    call w16od_slot
    call w16od_take
    mov [w16_odcur], bl
.arm:
    mov [w16_dcpaint], dl           ; the lock and its window's clip as it
    mov byte [w16_dcpkd], 0         ;   is now, as a parked DC is unparked
    mov bx, [w16_dcwnd]             ;   (SPEC.md 124.25.5)
    mov byte [w16_dcre], 1
    ICALL w16_dcopen
    mov byte [w16_dcre], 0
.ok:
    clc
.out:
    pop es
    pop di
    pop bx
    pop ax
    ret
.fail:
    stc
    jmp short .out

; w16od_out - the open own DC, if one is, back in its record, and the shared
; DC under it the open one again, armed as unparked - or none open. Keeps
; every register.
w16od_out:
    cmp byte [w16_odcur], 0
    je .ret
    push ax
    push bx
    push di
    push es
    mov al, [w16_odcur]
    dec al
    call w16od_slot
    call w16od_put
    mov byte [w16_odcur], 0
    cmp byte [w16_odson], 0
    je .close
    ICALL w16_dcpop
    mov al, [w16_dcpaint]           ; a BeginPaint DC's clip is the whole
    push ax                         ;   client then, as when it is unparked
    mov byte [w16_dcpaint], 0
    mov bx, [w16_dcwnd]
    mov byte [w16_dcre], 1
    ICALL w16_dcopen
    mov byte [w16_dcre], 0
    pop ax
    mov [w16_dcpaint], al
    jmp short .done
.close:
    ICALL w16_dcclose
.done:
    pop es
    pop di
    pop bx
    pop ax
.ret:
    ret

; w16od_res - AX = the hDC a call names -> AX = W16_HDC when it is an own
; DC's, which is the open DC now; W16_HDC itself puts an open own DC back.
; Anything else is left. Keeps the rest.
w16od_res:
    cmp ax, W16_HDC
    je w16od_out
    push ax
    sub ax, W16_ODC0
    jb .not
    test al, 1
    jnz .not
    shr ax, 1
    cmp ax, W16_NODC
    jae .not
    cmp word [w16_odseg], 0
    je .not
    push di
    push es
    call w16od_slot
    cmp word [es:di], 0
    pop es
    pop di
    je .not
    push dx
    xor dl, dl
    call w16od_in                    ; (no room: the open DC draws, as before)
    pop dx
    pop ax
    mov ax, W16_HDC
    ret
.not:
    pop ax
    ret

; w16od_rel - AX = a DC given back (ReleaseDC, EndPaint): an open own DC goes
; back to its record. CF=0 AX was an own DC's, CF=1 it was not. Keeps all.
w16od_rel:
    call w16od_out
    push ax
    sub ax, W16_ODC0
    cmp ax, 2 * W16_NODC            ; CF = it is not one
    cmc
    pop ax
    ret

; w16od_get - BX = a window (GetDC): CF=0 it has an own DC, AX = its handle.
; CF=1 it has not. Keeps the rest.
w16od_get:
    call w16od_find
    jc .ret
.h:
    shl ax, 1
    add ax, W16_ODC0
.ret:
    ret

; w16od_paint - BX = a window (BeginPaint): CF=0 its own DC is the open one
; with the paint's clip, AX = its handle. CF=1 it has none. Keeps the rest.
w16od_paint:
    call w16od_find
    jc w16od_get.ret
    push dx
    mov dl, 1
    call w16od_in
    pop dx
    jc w16od_get.ret
    jmp short w16od_get.h

; w16od_free - BX = a window being freed: its own DC, if it has one, given
; back. Keeps every register.
w16od_free:
    push ax
    push di
    push es
    xor ax, ax
.s:
    call w16od_slot
    cmp [es:di], bx
    je .hit
    inc ax
    cmp ax, W16_NODC
    jb .s
    jmp short .out
.hit:
    inc al
    cmp [w16_odcur], al
    jne .free
    call w16od_out
.free:
    mov word [es:di], 0
.out:
    pop es
    pop di
    pop ax
    ret

; w16od_last - the call's hDC, its last argument, resolved in place as
; w16od_res does: w16_odarg's, for a call that is not a W16B_MDC row. Keeps
; every register.
w16od_last:
    cmp word [w16_odseg], 0
    je .ret
    push ax
    push bx
    push es
    ICALL w16_args
    add bx, [w16_nargb]
    mov ax, [es:bx - 2]
    call w16od_res
    mov [es:bx - 2], ax
    pop es
    pop bx
    pop ax
.ret:
    ret

; w16od_blt - BitBlt's, PatBlt's and StretchBlt's DCs: AX = the source, CX =
; the destination, [w16b_xs]/[w16b_ys] the source's corner. The destination
; resolved; a source that is the same own DC is W16_HDC with it, and another
; own DC's is read from the screen at its own window's origin. Keeps the rest.
w16od_blt:
    push dx
    mov dx, ax                      ; DX = the source, as named
    mov ax, cx
    call w16od_res
    xchg ax, cx                     ; CX = the destination, resolved
    cmp ax, dx
    jne .src
    mov dx, cx                      ; the same DC both sides
.src:
    mov ax, dx
    cmp cx, W16_HDC
    jne .out
    sub dx, W16_ODC0
    jb .out
    test dl, 1
    jnz .out
    shr dx, 1
    cmp dx, W16_NODC
    jae .out
    cmp word [w16_odseg], 0
    je .out
    push di
    push es
    mov ax, dx
    call w16od_slot
    mov ax, [es:di + 2 + W16DC_OX]  ; its window's origin, by the open DC's
    sub ax, [w16_ox]
    add [w16b_xs], ax
    mov ax, [es:di + 2 + W16DC_OY]
    sub ax, [w16_oy]
    add [w16b_ys], ax
    pop es
    pop di
    mov ax, W16_HDC
.out:
    pop dx
    ret

section .text
