; SHRKID.COM - SHRINK.COM's EXEC target.  Says so, and hands back a code.
; OURS, MIT with the rest of the tree.
    org 0x100
    cpu 8086
    mov dx, msg
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C07
    int 0x21
msg: db 'CHILD ran', 13, 10, '$'
