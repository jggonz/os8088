; WireArena's angle lookup: the game's alone. The renderer part takes the
; camera's sin and cos in the frame block (wa_fsn, wa_fcs: wafb.inc), so the
; table is not in it.

; wa_sincos - AX = an angle (any; taken mod 1024) -> AX = sin, DX = cos, Q14.
; wa_sin is a QUARTER wave, sin(0..256): the other three are its mirror
; (256 - i in the odd quadrants) and its negative (the lower half). Every
; angle comes out exactly as the whole table's entry (arenagen checks the
; mirror; 1,536 bytes fewer). Both come from one pair of loads - u = sin i
; and v = sin(256 - i), i the angle's place in its quadrant - which the
; quadrant swaps and signs: (u, v), (v, -u), (-u, -v), (-v, u). ~230 clk
; (it was two folds through a subroutine, ~535). Keeps all but AX, DX.
wa_sincos:
    push bx
    push si
    mov bl, al
    xor bh, bh
    shl bx, 1                   ; BX = i x 2
    mov si, 512
    sub si, bx                  ; SI = (256 - i) x 2
    mov dx, [wa_sin+si]         ; DX = v
    mov bx, [wa_sin+bx]         ; BX = u
    test ah, 1                  ; the second and fourth quadrants: (v, -u)
    jz .e
    xchg bx, dx
    neg dx
.e:
    test ah, 2                  ; the lower half: both negative
    jz .p
    neg bx
    neg dx
.p:
    mov ax, bx
    pop si
    pop bx
    ret
