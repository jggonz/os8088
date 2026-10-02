; =============================================================================
; tests/deskitem/deskitem.asm - OSAPI_DESK_ITEM from a PACKAGE (SPEC.md 26.9)
;
; Not shipped software: a gate, riding its own scratch disk in B:.
;
;   make deskitem && python3 tests/deskitem.py
;
; A package puts an item on the desktop by handing the kernel a link record -
; the same 128 bytes a shortcut dragged out of a Disk window is (SPEC.md
; 26.8.1) - and asking for a CELL. This one links to ITSELF, B:\DESKITEM.O88,
; and asks for cell 5; its File menu has Add and Remove, so the host can drive
; both edges and read the answers out of guest memory:
;
;   di_zone   the zone the add answered, 0xFF before it or on a refusal
;   di_res    'A' the add answered CF = 0, 'a' it refused; 'R' / 'r' remove
;
; The record is HOSTILE to the kernel - it is the caller's bytes - so this one
; leaves its caption without a NUL and junk where the picture's header goes,
; and the host asserts the kernel terminated the one and stamped the other.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'DESKITEM', di_entry

di_entry:
    push si
    mov si, di_tpl
    call OSAPI_WM_CREATE
    jc .out
    OS88_REGION_MOVABLE
    mov si, di_menus
    call OSAPI_MENU_SET
.out:
    pop si
    ret

; File > Add (AL = 0) or Remove (AL = 1). A window callback: the UI task, the
; gfx lock held - which is what the slot asks of a package (it writes
; SYSTEM.CFG, through CTRL.DRV).
di_oncmd:
    push ax
    push si
    or al, al
    jnz .remove
    mov si, di_rec
    mov al, 1                       ; add
    call OSAPI_DESK_ITEM            ; CF = 0: AL = the zone
    mov byte [di_res], 'a'
    jc .out
    mov [di_zone], al
    mov byte [di_res], 'A'
    jmp short .out
.remove:
    mov si, di_rec                  ; the LINK, not only its zone: a zone is
    mov ah, [di_zone]               ; reused, so the kernel removes it only
    xor al, al                      ; while it still holds this record
    call OSAPI_DESK_ITEM
    mov byte [di_res], 'r'
    jc .out
    mov byte [di_res], 'R'
    mov byte [di_zone], 0xFF
.out:
    pop si
    pop ax
    ret

di_paint:
    ret

di_tpl:
    dw 120, 120, 200, 60
    dw di_ttl, di_paint, 0, 0

    OS88_MENUSET di_menus, di_name, di_oncmd
        OS88_MENU di_m_file, di_i_file, 2
    OS88_MENUSET_END di_menus
di_name:    db 'DeskItem', 0
di_m_file:  db 'File', 0
di_i_file:  dw di_it_add, di_it_rem
di_it_add:  db 'Add', 0
di_it_rem:  db 'Remove', 0
di_ttl:     db 'DeskItem', 0

di_zone:    db 0xFF
di_res:     db '-'

; --- the LINK record (SPEC.md 26.8.1), exactly a shortcut's -------------------
di_rec:
    db 1                            ; OSAPI_DI_VOL   B:
    db 5                            ; OSAPI_DI_CELL  the cell it would like
    db 1                            ; OSAPI_DI_KIND  a package
    db 0
    db 'DESKITEM.O88'               ; OSAPI_DI_PATH  below the root
    times OSAPI_DI_CAP - ($ - di_rec) db 0
    db 'DESK ITEM ABC'              ; OSAPI_DI_CAP, all 13 bytes and NO NUL
    times OSAPI_DI_BODY - ($ - di_rec) db 0xEE  ; and junk where the header
                                    ; goes: the kernel terminates the caption
                                    ; and stamps the header itself
    times 16 dw 0xFFFF              ; the 16x16 picture: a white square...
    dw 0xFFFF
    times 14 dw 0x8001              ; ...with a black frame
    dw 0xFFFF
%if ($ - di_rec) != OSAPI_DI_SIZE
  %error "deskitem: the link record is OSAPI_DI_SIZE bytes (SPEC.md 26.8.1)"
%endif

    OS88_IMAGE_END
