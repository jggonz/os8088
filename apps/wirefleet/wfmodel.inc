; WireFleet's private board. SPEC.md 114.1. All indices are row-major.
; Internal routines spend AX/BX/CX/DX/DI; DS is always the package.
WF_CELLS equ 100
WF_SHIPS equ 5
WF_ABSENT equ 255

wf_new:
    call wf_link_new
    mov word [wf_buttons+OS88UI_BT_N], 5
    mov byte [wf_abon], 0
wf_clearfleet:
    push es
    push ds
    pop es
    xor ax, ax
    mov di, wf_board
    mov cx, 300                 ; own fleet, incoming shots, outgoing shots
    rep stosb
    mov di, wf_anchors
    mov al, WF_ABSENT
    mov cx, WF_SHIPS
    rep stosb
    mov di, wf_dirs
    xor ax, ax
    mov cx, WF_SHIPS
    rep stosb
    mov [wf_ready], al
    mov [wf_selected], al
    mov [wf_direction], al
    mov [wf_target], al
    mov [wf_placed], al
    mov word [wf_message], wf_placehelp
    pop es
    ret

; AL=ship index 0..4. Remove its cells, leaving the rest alone.
wf_remove:
    xor ah, ah
    mov bx, ax
    cmp byte [wf_anchors+bx], WF_ABSENT
    je .out
    mov byte [wf_anchors+bx], WF_ABSENT
    dec byte [wf_placed]
    inc al
    mov bx, wf_board
    mov cx, WF_CELLS
.cell:
    cmp [bx], al
    jne .next
    mov byte [bx], 0
.next:
    inc bx
    loop .cell
.out:
    ret

; Selected ship, target and direction are globals. Validate BEFORE removing
; the old placement, so an impossible move cannot eat a ship. CF=refused.
wf_place:
    cmp byte [wf_ready], 0
    jne .bad
    xor bx, bx
    mov bl, [wf_selected]
    cmp bl, WF_SHIPS
    jae .bad
    mov cl, [wf_lengths+bx]
    xor ch, ch
    mov al, [wf_target]
    cmp al, WF_CELLS
    jae .bad
    xor ah, ah
    mov dl, 10
    div dl                      ; AL row, AH column
    mov dl, 1
    cmp byte [wf_direction], 0
    je .edge
    mov ah, al
    mov dl, 10
.edge:
    add ah, cl
    cmp ah, 10
    ja .bad
    mov [wf_step], dl
    mov di, wf_board
    xor ax, ax
    mov al, [wf_target]
    add di, ax
    mov dh, [wf_selected]
    inc dh
    push cx
    push di
.check:
    mov al, [di]
    or al, al
    jz .vacant
    cmp al, dh
    jne .occupied
.vacant:
    xor ax, ax
    mov al, dl
    add di, ax
    loop .check
    pop di
    pop cx
    push cx
    push di
    mov al, [wf_selected]
    call wf_remove
    pop di
    pop cx
    xor bx, bx
    mov bl, [wf_selected]
    mov al, [wf_target]
    mov [wf_anchors+bx], al
    mov al, [wf_direction]
    mov [wf_dirs+bx], al
    inc byte [wf_placed]
    mov al, bl
    inc al
    xor dx, dx
    mov dl, [wf_step]
.write:
    mov [di], al
    add di, dx
    loop .write
    clc
    ret
.occupied:
    pop di
    pop cx
.bad:
    stc
    ret

; At most 64 attempts per ship, then the simple legal fallback. There is
; no unbounded retry, even with a hostile or degenerate random stream.
wf_auto:
    cmp byte [wf_ready], 0
    jne .out
    call wf_entropy            ; private gesture timing, never sent to the peer
    call OSAPI_SRAND           ; fresh PCs must not share the default fleet
    call wf_clearfleet
.ship:
    mov byte [wf_attempts], 64
.try:
    call OSAPI_RAND
    mov bx, 100
    xor dx, dx
    div bx
    mov [wf_target], dl
    call OSAPI_RAND
    mov cl, 7
    shr ax, cl                 ; do not reuse the LCG's alternating low bit
    and al, 1
    mov [wf_direction], al
    call wf_place
    jnc .placed
    dec byte [wf_attempts]
    jnz .try
    call wf_clearfleet
.fallback:
    mov al, [wf_selected]
    mov dl, 20
    mul dl
    mov [wf_target], al
    call wf_place
    inc byte [wf_selected]
    cmp byte [wf_selected], WF_SHIPS
    jb .fallback
    jmp .done
.placed:
    inc byte [wf_selected]
    cmp byte [wf_selected], WF_SHIPS
    jb .ship
.done:
    mov byte [wf_selected], 0
    mov byte [wf_target], 0
    mov byte [wf_direction], 0
    mov word [wf_message], wf_allplaced
.out:
    ret

; The next absent ship is selected after a successful placement.
wf_nextship:
    xor bx, bx
.scan:
    cmp byte [wf_anchors+bx], WF_ABSENT
    je .found
    inc bx
    cmp bx, WF_SHIPS
    jb .scan
    mov word [wf_message], wf_allplaced
    ret
.found:
    mov [wf_selected], bl
    ret

wf_lengths: db 5,4,3,3,2
wf_tilebases: db 4,9,13,16,19
wf_shipnames: dw wf_carrier,wf_battleship,wf_cruiser,wf_submarine,wf_destroyer
wf_carrier: db 'Carrier (5)',0
wf_battleship: db 'Battleship (4)',0
wf_cruiser: db 'Cruiser (3)',0
wf_submarine: db 'Submarine (3)',0
wf_destroyer: db 'Destroyer (2)',0
