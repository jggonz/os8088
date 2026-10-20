; Solo opponent. It never aims at where the human stands: its first throw on
; a skyline is a guess, and after each one it is told only what anyone
; watching would see - the banana came down short of its rival or flew past.
; It brackets the release power between its strongest short throw and its
; weakest long one, so it closes in over a few turns rather than oneshotting.
; A throw that craters its own rooftop raises the arc and starts the bracket
; again. A new skyline forgets it all (gr_city), the scene it learned gone.
; No scores, terrain or live shot are poked.
GR_AITHINK equ 24               ; worker ticks of visible thought, ~1.3 s

gr_aitick:
    call gr_aimine
    jne .out
    cmp word [gr_aipower], 0
    jne .think
    ; Choose first and show it: the HUD (and the aim guide, when chosen)
    ; carry this throw for the whole of the thought before it.
    call gr_aichoose
    call gr_hudpaint
.think:
    inc word [gr_aipower]
    cmp word [gr_aipower], GR_AITHINK
    jae .fire
.out:
    ret
.fire:
    mov word [gr_aipower], 0
    call gr_fire
    jmp gr_hudpaint

; Set gr_angle and gr_power to the next throw from what is learned.
gr_aichoose:
    cmp word [gr_aiangle], 0
    jne .aimed
    mov ax, 26
    call gr_random
    add ax, 45                  ; 45..70 for the whole skyline
    mov [gr_aiangle], ax
.aimed:
    mov ax, [gr_ailo]
    mov bx, [gr_aihi]
    or ax, ax
    jz .nolo
    or bx, bx
    jz .harder
    ; Both ends known: the middle, give or take a quarter of the gap.
    mov cx, bx
    sub cx, ax
    shr cx, 1
    add ax, cx
    shr cx, 1
    jmp short .jitter
.harder:
    ; Only short throws so far: half as hard again.
    mov cx, ax
    shr cx, 1
    add ax, cx
    jmp short .eighth
.nolo:
    or bx, bx
    jz .guess
    ; Only long throws so far: a third softer.
    mov ax, bx
    xor dx, dx
    mov cx, 3
    div cx
    sub bx, ax
    mov ax, bx
.eighth:
    mov cx, ax
    shr cx, 1
    shr cx, 1
    shr cx, 1
    jmp short .jitter
.guess:
    ; Nothing learned: repeat the last release after an arc change, or on
    ; the skyline's first throw a wild 40..100.
    mov ax, [gr_ailast]
    xor cx, cx
    or ax, ax
    jnz .jitter
    mov ax, 70
    mov cx, 30
.jitter:
    ; AX = estimate, CX = spread: release anywhere in AX-CX..AX+CX.
    cmp cx, 2
    jae .spread
    mov cx, 2
.spread:
    mov bx, ax
    mov ax, cx
    shl ax, 1
    inc ax
    call gr_random
    sub ax, cx
    add ax, bx
    cmp ax, 5
    jge .upper
    mov ax, 5
.upper:
    cmp ax, 360
    jle .power
    mov ax, 360
.power:
    mov [gr_ailast], ax
    mov [gr_power], ax
    mov ax, [gr_aiangle]
    mov [gr_angle], ax
    ret

; ZF set when the throw in the air (or about to be) is the computer's.
gr_aimine:
    cmp byte [gr_players], 1
    jne .out
    cmp byte [gr_turn], 1
.out:
    ret

; CX = logical x where a throw cratered the terrain. Its own rooftop or the
; wall beside it says the arc is too flat for this skyline, whatever the
; power; at the steepest arc it can only throw harder. Preserves all but AX.
gr_aiterrain:
    call gr_aimine
    jne .out
    mov ax, [gr_gx+2]           ; its own gorilla, never the rival's
    sub ax, 16
    cmp cx, ax
    jl gr_ailearn.judge
    cmp word [gr_aiangle], 80
    jae gr_ailearn.judge
    add word [gr_aiangle], 10
    xor ax, ax
    mov [gr_ailo], ax
    mov [gr_aihi], ax
.out:
    ret

; CX = signed logical x where the computer's throw came down or left the
; scene (off the left edge is negative). Preserves all but AX.
gr_ailearn:
    call gr_aimine
    jne .out
.judge:
    ; The one fact taken from the rival: was the banana short of it or past.
    mov ax, [gr_gx]
    add ax, 8
    cmp cx, ax
    mov ax, [gr_ailast]
    jl .long
    cmp ax, [gr_ailo]
    jbe .lokept
    mov [gr_ailo], ax
.lokept:
    ; A long verdict at or below a short throw was terrain luck: drop it.
    cmp [gr_aihi], ax
    ja .out
    mov word [gr_aihi], 0
.out:
    ret
.long:
    cmp word [gr_aihi], 0
    je .hiset
    cmp ax, [gr_aihi]
    jae .hikept
.hiset:
    mov [gr_aihi], ax
.hikept:
    cmp [gr_ailo], ax
    jb .out
    mov word [gr_ailo], 0
    ret
