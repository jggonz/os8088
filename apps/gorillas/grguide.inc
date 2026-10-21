; Aim guide (a setup option): the angle a throw leaves at, drawn as a ray of
; dots from the thrower's hand. It is straight - no gravity, wind or
; velocity in it - and it degrades as it goes: the gaps widen, the dots stray
; further off the line, the last four fade to grey and the ray stops after
; 33 pixels. The first dots show the exit angle; nothing shows the landing.
; Drawn into the scene with the inks underneath saved, and taken out again
; before any banana can meet it (gr_fire), so collisions never see a dot.
GR_GUIDEN equ 8
gr_guidedist: db 4, 6, 8, 11, 15, 20, 26, 33
gr_guidestray: db 0, 0, 0, 1, -1, 2, -2, 3   ; signed pixels across the ray

; Bring the guide in line with what aiming shows: the active player's angle
; in state 0 with the option on, else nothing. Redraws only on a change, so a
; HUD update that moves no angle costs one compare. Preserves all registers.
gr_guideshow:
    SAVE
    mov ax, 0ffffh              ; key: no guide wanted
    cmp byte [gr_guide], 0
    je .want
    cmp byte [gr_state], 0
    jne .want
    mov ax, [gr_angle]          ; 0..360, so the turn goes in bit 9
    cmp byte [gr_turn], 0
    je .want
    add ax, 512
.want:
    cmp ax, [gr_guideat]
    je .done
    push ax
    call gr_guideerase
    pop ax
    mov [gr_guideat], ax
    cmp ax, 0ffffh
    je .done
    call gr_guidedraw
    call gr_guideblit
.done:
    RESTORE
    ret

; The whole scene is about to be painted: draw without a blit of our own.
gr_guidescene:
    mov byte [gr_guidequiet], 1
    call gr_guideshow
    mov byte [gr_guidequiet], 0
    ret

; Take the guide out before a throw. Preserves all registers.
gr_guidehide:
    SAVE
    call gr_guideerase
    call gr_guideblit
    mov word [gr_guideat], 0ffffh
    RESTORE
    ret

; Restore the saved inks newest first (a dot may sit on another). The box
; they covered stays owed, so a redraw repaints old and new in one blit.
gr_guideerase:
    xor bx, bx
    mov bl, [gr_guidedots]
    or bx, bx
    jz .out
.undo:
    dec bx
    mov si, bx
    shl si, 1
    shl si, 1
    mov cx, [gr_guidesave+si]
    xor dx, dx
    mov dl, [gr_guidesave+si+2]
    mov al, [gr_guidesave+si+3]
    call gr_put
    or bx, bx
    jnz .undo
    mov [gr_guidedots], bl
    mov byte [gr_guideowed], 1  ; the box still spans the dots just restored
.out:
    ret

; Repaint the owed box, unless a whole-scene paint is about to.
gr_guideblit:
    cmp byte [gr_guideowed], 0
    je .out
    mov byte [gr_guideowed], 0
    cmp byte [gr_guidequiet], 0
    jne .out
    mov ax, [gr_guidebox]
    and ax, 0fff8h
    mov cx, [gr_guidebox+4]
    or cx, 7
    inc cx
    sub cx, ax
    mov bx, [gr_guidebox+2]
    mov dx, [gr_guidebox+6]
    sub dx, bx
    inc dx
    jmp gr_blit
.out:
    ret

; Plot the dots for [gr_turn] at [gr_angle] into the scene (none drawn
; yet), saving each ink underneath and growing the owed box over them. Q8
; direction: x along cos, mirrored for the right gorilla; y along sin at the
; scene's 15/16 aspect. The ray starts beside the thrower's shoulder rather
; than at the release point above its head: on the tallest roof that point
; is inside the HUD rows, and the first dots - the ones that ARE the angle -
; would be clipped away. From the shoulder (at least row 32) they never are.
gr_guidedraw:
    xor bx, bx
    mov bl, [gr_turn]
    shl bx, 1
    mov ax, [gr_gx+bx]
    add ax, 17                  ; one clear of the 16-wide art, either side
    or bx, bx
    jz .x
    sub ax, 19
.x:
    mov [gr_guidex], ax
    mov ax, [gr_gy+bx]
    add ax, 4
    mov [gr_guidey], ax
    mov ax, [gr_angle]
    push ax
    call gr_sine
    mov cl, 6
    sar ax, cl
    mov [gr_guides], ax
    pop ax
    add ax, 90
    cmp ax, 360
    jb .cos
    sub ax, 360
.cos:
    call gr_sine
    sar ax, cl
    or bx, bx
    jz .c
    neg ax
.c:
    mov [gr_guidec], ax
    xor si, si
.dot:
    mov al, [gr_guidestray+si]
    cbw
    mov di, ax                  ; DI = stray across the ray
    xor ax, ax
    mov al, [gr_guidedist+si]
    mov bp, ax                  ; BP = distance along it
    mov ax, [gr_guidec]
    imul bp
    mov bx, ax
    mov ax, [gr_guides]
    imul di
    add ax, bx
    mov cl, 8
    sar ax, cl
    add ax, [gr_guidex]
    push ax
    mov ax, [gr_guides]
    imul bp
    mov bx, ax
    mov cl, 4
    sar bx, cl
    sub ax, bx
    mov bx, ax
    mov ax, [gr_guidec]
    imul di
    sub ax, bx
    mov cl, 8
    sar ax, cl
    add ax, [gr_guidey]
    mov dx, ax
    pop cx
    cmp cx, 256
    jae .skip
    cmp dx, 24
    jl .skip
    cmp dx, 116
    jge .skip
    call gr_get
    xor bx, bx
    mov bl, [gr_guidedots]
    shl bx, 1
    shl bx, 1
    mov [gr_guidesave+bx], cx
    mov [gr_guidesave+bx+2], dl
    mov [gr_guidesave+bx+3], al
    inc byte [gr_guidedots]
    mov al, 15
    cmp si, 4
    jb .ink
    mov al, 7
.ink:
    call gr_put
    cmp byte [gr_guideowed], 0
    jne .grow
    mov byte [gr_guideowed], 1
    mov [gr_guidebox], cx
    mov [gr_guidebox+4], cx
    mov [gr_guidebox+2], dx
    mov [gr_guidebox+6], dx
    jmp short .skip
.grow:
    cmp cx, [gr_guidebox]
    jae .right
    mov [gr_guidebox], cx
.right:
    cmp cx, [gr_guidebox+4]
    jbe .top
    mov [gr_guidebox+4], cx
.top:
    cmp dx, [gr_guidebox+2]
    jae .bottom
    mov [gr_guidebox+2], dx
.bottom:
    cmp dx, [gr_guidebox+6]
    jbe .skip
    mov [gr_guidebox+6], dx
.skip:
    inc si
    cmp si, GR_GUIDEN
    jb .dot
    ret
