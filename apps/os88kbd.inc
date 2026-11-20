; Optional native CP850 glyph holds (SPEC.md 83.7 / 9.16.1).
; Include with code, after the package header. No private storage is needed:
; the kernel owns one idempotent hold per package instance and releases it
; at teardown. Call only from task/key/file-load context, never a painter.
%ifndef OS88KBD_INCLUDED
%define OS88KBD_INCLUDED
; OS88KBD_CALL is the slot call itself. A HOLD can load KEYBOARD.DRV and so
; claim; an includer whose copy sits in a MOVABLE module routes it through
; resident code (SHEET: `SHK keyboard`, SPEC.md 82.16.11)
%ifndef OS88KBD_CALL
%define OS88KBD_CALL call OSAPI_KEYBOARD
%endif

; Every register kept; CF=1 if the optional provider could not be acquired.
os88kbd_hold:
    push ax
    mov ax, 2
    OS88KBD_CALL
    pop ax
    ret

os88kbd_release:
    push ax
    mov ax, 3
    OS88KBD_CALL
    pop ax
    ret

; ES:DI = CX bytes. CF=1 iff a byte 128..255 is present; all registers kept.
; DEL (127) is a control byte and does not request the provider.
os88kbd_scan:
    push ax
    push cx
    push di
.next:
    jcxz .ascii
    mov al, [es:di]
    inc di
    test al, 0x80
    jnz .high
    loop .next
.ascii:
    clc
    jmp short .out
.high:
    stc
.out:
    pop di
    pop cx
    pop ax
    ret

; ES:DI = CX bytes. Acquire only if those bytes contain extended text.
; CF=1 refusal; otherwise CF=0. All registers preserved.
os88kbd_prepare:
    call os88kbd_scan
    jc os88kbd_hold
    clc
    ret
%endif
