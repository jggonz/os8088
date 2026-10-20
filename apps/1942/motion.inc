; Native translation of cartridge actor motion. No 6502 runtime is shipped.
; Sidecar records preserve the renderer/collision actor layout (ESIZE=24).
%if N_ROM
MFX equ 0
MFY equ 2
MH equ 4
MS equ 6
MT equ 8
MP equ 10
MCX equ 12
MCY equ 14
MR equ 16
MD equ 18
MN equ 20
MG equ 22

n_motioninit:                       ; SI=actor, preserve scheduler DI/BP
    push di
    push bp
    lea di,[si+n_motion-n_enemies]
    xor ax,ax
    mov bx,22
.clear:
    mov [di+bx],ax
    sub bx,2
    jns .clear
    mov word [di+MS],18
    mov ax,[si+ETYPE]
    cmp ax,10
    jb .small
    mov word [di+MS],21             ; $EF49, orange formations
    cmp ax,23
    jb .path
    mov word [di+MS],10             ; $EF6E, rotating fighters
    mov word [si+EKIND],0
    mov word [si+EHP],1
    cmp ax,32
    jb .path
    mov word [si+EKIND],5
    mov word [si+EHP],3
    cmp ax,34
    jb .path
    mov word [si+EKIND],6
    mov word [si+EHP],5
    mov word [di+MS],6              ; $EF98, medium aircraft
    cmp ax,41
    jb .path
    mov word [si+EKIND],7
    mov word [si+EHP],15
    mov word [di+MS],11             ; $EFDA/$F6E8, large bomber
    mov word [si+EX],64
    cmp word [n_px],128
    jb .big
    mov word [si+EX],192
.big:
    mov word [di+MP],1              ; vertical approach before shared path
    mov word [di+MH],0
    jmp .out
.path:
    mov bx,[si+EPATH]
    mov ax,[bx]
    sub ax,[si+EX]
    mov bx,[bx+2]
    sub bx,[si+EY]
    call n_fireangle
    mov [di+MH],ax
    jmp .out
.small:
    mov word [si+EKIND],0
    mov bx,2                       ; diving entries, $F054
    cmp ax,3
    jb .sweeper
    cmp ax,5
    jb .entry
    cmp ax,7
    je .sweeper
    cmp ax,8
    je .entry
    mov bx,4                       ; descending attackers, $F42C
    jmp .entry
.sweeper:
    xor bx,bx                      ; side entries, $F176/$F34C
    mov word [si+EKIND],4
    cmp ax,1
    jne .entry
    mov word [si+EKIND],3
.entry:
    mov ax,[n_entryseq]
    inc word [n_entryseq]
    and word [n_entryseq],255
    xor dx,dx
    div word [n_startcounts+bx]
    cmp word [si+ETYPE],1
    je .four
    cmp word [si+ETYPE],7
    jne .position
.four:
    and dx,3
.position:
    mov bx,[n_smallstarts+bx]
    shl dx,1
    shl dx,1
    add bx,dx
    mov ax,[bx]
    mov [si+EX],ax
    mov ax,[bx+2]
    mov [si+EY],ax
    mov ax,[si+ETYPE]
    cmp ax,3
    jb .circle
    cmp ax,7
    je .circle
    mov word [di+MH],8
    cmp ax,5
    je .out
    cmp ax,6
    je .out
    mov ax,[n_px]
    sub ax,[si+EX]
    mov bx,[n_py]
    sub bx,[si+EY]
    call n_fireangle
    mov [di+MH],ax
    jmp .out
.circle:
    mov word [di+MS],17
    mov word [di+MR],68
    mov bx,[n_entryseq]
    and bx,7
    cmp word [n_px],128
    jb .cy
    add bx,8
.cy:
    cmp word [n_py],128
    jb .variant
    add bx,16
.variant:
    cmp word [si+ETYPE],1
    je .wide
    cmp word [si+ETYPE],7
    jne .center
    mov word [di+MS],18
    mov word [di+MR],72
    jmp .sidecenter
.wide:
    mov word [di+MS],19
    mov word [di+MR],92
.sidecenter:
    mov bx,3
    cmp word [si+EX],128
    jl .center
    mov bx,11
.center:
    shl bx,1
    shl bx,1
    mov ax,[n_centers+bx]
    mov [di+MCX],ax
    mov ax,[n_centers+bx+2]
    mov [di+MCY],ax
    call n_circletarget
    call n_fireangle
    mov [di+MH],ax
.out:
    pop bp
    pop di
    ret

n_cartmove:                         ; SI=actor, CF=left the actor bounds
    push di
    lea di,[si+n_motion-n_enemies]
    cmp word [di+MT],0
    je .step
    dec word [di+MT]
    jnz .stay
    mov ax,[di+MG]
    mov [di+MH],ax
.step:
    ; $AF7A/$B048: signed 8.8 vectors multiplied by the per-type speed.
    mov bx,[di+MH]
    and bx,15
    shl bx,1
    shl bx,1
    mov ax,[n_velocity+bx+2]
    imul word [di+MS]
    add ax,[di+MFX]
    mov cx,ax
    and cx,255
    mov [di+MFX],cx
    mov cl,8
    sar ax,cl
    mov [si+EVX],ax
    add [si+EX],ax
    mov ax,[n_velocity+bx]
    imul word [di+MS]
    add ax,[di+MFY]
    mov cx,ax
    and cx,255
    mov [di+MFY],cx
    mov cl,8
    sar ax,cl
    mov [si+EDY],ax
    add [si+EY],ax
    ; $B37B: invisible actors keep their heading and leave the outer box.
    cmp word [si+EX],-64
    jl .end
    cmp word [si+EX],320
    jg .end
    cmp word [si+EY],-64
    jl .end
    cmp word [si+EY],304
    jg .end
    cmp word [si+EX],0
    jl .stay
    cmp word [si+EX],256
    jg .stay
    cmp word [si+EY],0
    jl .stay
    cmp word [si+EY],240
    jg .stay
    cmp word [si+ETYPE],10
    jb .small
    mov bx,[si+EPATH]
    cmp word [bx],32767
    je .end
    mov ax,[bx]
    sub ax,[si+EX]
    cmp word [di+MP],1
    jne .pathy
    xor ax,ax                      ; bomber's player-relative approach
.pathy:
    mov bx,[bx+2]
    sub bx,[si+EY]
    call n_atpoint
    jnc .steer
    add word [si+EPATH],4
    mov word [di+MP],0
    jmp .stay
.steer:
    call n_fireangle
    mov [di+MG],ax
    sub ax,[di+MH]
    and ax,15
    jz .stay
    cmp ax,8
    je .reversepath
    ja .left
    inc word [di+MH]
    jmp .mask
.left:
    dec word [di+MH]
.mask:
    and word [di+MH],15
    jmp .stay
.reversepath:
    mov word [di+MT],8             ; $F69B reversal animation
    jmp .stay
.small:
    call n_cartsmall
.stay:
    pop di
    clc
    ret
.end:
    pop di
    stc
    ret

n_atpoint:                         ; signed AX/BX deltas, ROM +/-6 rectangle
    push ax
    add ax,6
    cmp ax,12
    jae .no
    mov ax,bx
    add ax,6
    cmp ax,12
    jae .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

n_cartsmall:
    mov ax,[si+ETYPE]
    cmp ax,3
    jb .circle
    cmp ax,7
    je .circle
    cmp ax,5
    je .cross
    cmp ax,6
    je .cross
    ; $F054/$F0CE: aimed approach, reversal near player, then climb out.
    cmp word [di+MP],0
    jne .second
    mov ax,[n_py]
    cmp [si+EY],ax
    jge .reverse
    mov ax,[n_py]
    sub ax,32
    cmp [si+EY],ax
    jl .out
    mov ax,[n_px]
    sub ax,[si+EX]
    add ax,80
    cmp ax,160
    jae .out
    jmp .reverse
.second:
    cmp word [si+ETYPE],3
    je .out
    cmp word [di+MP],1
    jne .out
    cmp word [si+EY],48
    jae .out
.reverse:
    inc word [di+MP]
    mov bx,[di+MH]
    shl bx,1
    mov ax,[n_reverse+bx]
    mov [di+MG],ax
    mov word [di+MT],8
    ret
.cross:
    cmp word [di+MP],0
    jne .out
    mov ax,[n_py]
    sub ax,64
    cmp [si+EY],ax
    jl .out
    add ax,32
    cmp [si+EY],ax
    jge .out
    mov ax,[n_px]
    sub ax,[si+EX]
    add ax,80
    cmp ax,160
    ja .out
    mov word [di+MP],1
    mov word [di+MS],20
    mov word [di+MH],6
    mov ax,[si+EX]
    cmp ax,[n_px]
    jl .out
    mov word [di+MH],10
.out:
    ret
.circle:
    cmp word [di+MP],2
    je .out
    call n_circletarget
    call n_atpoint
    jnc .aim
    cmp word [si+ETYPE],7
    jne .circlephase
    cmp word [si+EY],120
    jb .circlephase
    mov word [di+MH],6
    mov ax,[si+EX]
    cmp ax,[n_px]
    jl .leave
    mov word [di+MH],10
    jmp .leave
.circlephase:
    cmp word [di+MP],0
    jne .next
    mov word [di+MP],1
    mov word [di+MD],1
    cmp word [di+MH],8
    jb .next
    mov word [di+MD],-1
.next:
    mov ax,[di+MD]
    add [di+MN],ax
    and word [di+MN],15
    cmp word [di+MD],1
    jne .negative
    cmp word [di+MN],0
    jne .target
    mov word [di+MH],6
    jmp .leave
.negative:
    cmp word [di+MN],15
    jne .target
    cmp word [di+MP],1
    jne .target
    ; The initial negative step is also 15; only exit after a full circle.
    cmp word [di+MG],1
    jne .target
    mov word [di+MH],10
.leave:
    mov word [di+MP],2
    cmp word [si+ETYPE],7
    jne .out
    mov word [di+MS],20
    ret
.target:
    cmp word [di+MN],14
    jne .point
    mov word [di+MG],1
.point:
    call n_circletarget
.aim:
    call n_fireangle
    mov [di+MH],ax
    ret

n_circletarget:                     ; DI=motion; AX/BX target relative to SI
    mov bx,[di+MN]
    shl bx,1
    shl bx,1
    mov ax,[n_circle+bx]
    imul word [di+MR]
    mov cl,8
    sar ax,cl
    add ax,[di+MCX]
    sub ax,[si+EX]
    push ax
    mov ax,[n_circle+bx+2]
    imul word [di+MR]
    mov cl,8
    sar ax,cl
    add ax,[di+MCY]
    sub ax,[si+EY]
    mov bx,ax
    pop ax
    ret
%endif
