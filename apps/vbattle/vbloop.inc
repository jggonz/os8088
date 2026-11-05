; =============================================================================
; os8088 - apps/vbattle/vbloop.inc - THE TITLE LOOP, THE FADE AND THE ENDINGS
; (SPEC.md 125.3.1). A loop is a clip played out of one ring, read whole,
; whose lap goes round in it (vb_lrewind, vb_lcur, in vbattle.asm). The
; title is left on Enter or Space by a FADE (the UI task's, vb_fadetick) or
; by opening 0 ENTERED at one of the title's frames (the hook's, vb_lanchor
; and vb_leave_go). An ending fades to its end loop, joins it, or fades
; into the play again (vb_again), which starts over inside the bracket.
; =============================================================================

; vb_lanchor - the title is to be left by opening 0: once its head is in
; ring 0, the nearest of its anchors (title frames) at least [vb_dmin]
; frames on, as vb_anchor takes an action's at the idle loop. Its sound is
; booked for the frame its entry takes. The hook's
vb_lanchor:
    cmp word [vb_cfr], 0            ; (nothing of the title up yet)
    je .out
    mov bx, vb_rg0
    mov al, [vb_meta+VH_OPENS]
    cmp al, [bx+RG_CLIP]
    jne .out
    call vb_rhead                   ; CF=1 its head is in
    jnc .out
    mov al, [vb_meta+VH_OPENS]
    xor ah, ah
    push bx
    mov bx, ax
    call vb_crec
    pop bx
    mov ax, [vb_cfr]                ; the title's frame on the glass
    dec ax
    mov [vb_lnow], ax
    mov word [vb_bestd], 0xFFFF
    mov di, [si+VC_ANCH]
    mov cx, [si+VC_NANCH]
    jcxz .out
    mov es, [vb_vres]
.a:
    mov ax, [es:di]                 ; d = (k - now) mod n, at least dmin
    sub ax, [vb_lnow]
    jae .p
    add ax, [vb_lnrec]
.p:
    cmp ax, [vb_dmin]
    jae .d
    add ax, [vb_lnrec]
.d:
    cmp ax, [vb_bestd]
    jae .n
    mov [vb_bestd], ax
    mov dx, [es:di]
    mov [vb_lk], dx
    mov dx, [es:di+2]
    mov [vb_lent], dx
.n:
    add di, 4
    loop .a
    cmp word [vb_bestd], 0xFFFF
    je .out
    mov byte [bx+RG_PLAY], 1        ; the reader streams it from here
    mov ax, [vb_bestd]              ; its sound with its frame 0
    mul word [vb_meta+VH_SPF]
    add ax, [vb_due]
    adc dx, [vb_due+2]
    mov [vb_apat], ax
    mov [vb_apat+2], dx
    mov [vb_apring], bx
    mov al, [vb_meta+VH_OPENS]
    mov [vb_apclip], al
.out:
    ret

; vb_leave_go - the title's frame [vb_lk] is up: its way out's ENTRY drawn
; in place of the next frame, and opening 0 played on from its record 1
; (record 0 is its sound alone), the openings after it in turn. The title's
; ring is the reader's once its sound has given way. The hook's; CF=0
vb_leave_go:
    mov dx, [vb_eseg]
    add dx, [vb_lent]
    xor si, si
    call vb_decode
    mov bx, [vb_lring]              ; the title is over
    mov byte [bx+RG_VDONE], 1
    mov byte [vb_tplay], 0
    mov bx, vb_rg0
    call vb_rrec                    ; opening 0's record 0: past it
    call vb_radv
    mov al, [vb_meta+VH_OPENS]
    mov bx, vb_rg0
    call vb_start_clip
    dec word [vb_cleft]
    mov word [vb_cfr], 1
    mov word [vb_lk], 0xFFFF
    mov byte [vb_lwant], 0
    call vb_onextset                ; (opening 0 plays: the next is 1)
    test word [vb_cflags], CF_FULL  ; an opening with the menu: the band
    jnz .x                          ; may come (a full-screen one: once it
    mov byte [vb_bandok], 1         ; is over, 125.3)
.x:
    clc
    ret

; vb_eloop_go - an ending's END loop, [vb_eloop], once its head is in a
; ring: from record 0 (after a fade: from black), or from record 1 when the
; ending JOINED it ([vb_estart] = 2: its seam drew frame 0). The hook's
vb_eloop_go:
    mov al, [vb_eloop]
    mov bx, vb_rg0
    cmp al, [bx+RG_CLIP]
    je .r
    mov bx, vb_rg1
    cmp al, [bx+RG_CLIP]
    jne .no
.r:
    call vb_rhead
    jnc .no
    mov al, [vb_eloop]
    call vb_start_clip              ; (RG_PLAY, the lap's bookkeeping)
    mov byte [vb_eplay], 1
    test word [vb_cflags], CF_LOOP  ; ONE THAT PLAYS ONCE (125.3.1): its end
    jnz .lp                         ; goes on as its "then" says
    push ax
    mov al, [vb_ethen]
    mov [vb_ehow], al
    pop ax
    test word [vb_cflags], CF_NOMUS ; (no music after its own sound: its
    jz .lp                          ; fade is silent - the owner's, 125.3.1)
    mov byte [vb_mon], 0
.lp:
    test word [vb_cflags], CF_FULL  ; FULL SCREEN: the band down, and not
    jz .bd                          ; drawn again over it
    mov byte [vb_banddone], 0
    mov byte [vb_bandok], 0
.bd:
    mov word [vb_cfr], 0
    mov [vb_apring], bx             ; its sound from the next frame
    mov [vb_apclip], al
    mov ax, [vb_due]
    mov dx, [vb_due+2]
    mov [vb_apat], ax
    mov [vb_apat+2], dx
    mov al, [vb_estart]
    mov byte [vb_estart], 0
    cmp al, 2
    jne .no
    call vb_rrec                    ; JOINED: on from its record 1
    mov [vb_askipn], cx             ; (its sound past the same record: a
    call vb_radv                    ; once-clip has no [vb_lbase])
    dec word [vb_cleft]
    mov word [vb_cfr], 1
    mov byte [vb_askip], 1
.no:
    ret

; vb_fadetick - THE FADE (the UI task's): begun when one is wanted, then a
; step for each frame of the hook's clock that has passed, the sound a shift
; lower each quarter of it; at its last step, what follows it - opening 0
; after the title, the end loop after an ending, or the play again
vb_fadetick:
    mov al, [vb_fwant]
    or al, al
    jz .on
    mov byte [vb_fwant], 0
    cmp byte [vb_fon], 0
    jne .on
    mov cx, 200                     ; the whole screen...
    cmp byte [vb_kind], VK_HERC
    jne .b0
    mov cx, 348
.b0:
    cmp al, 4                       ; (4: the whole screen, before an end
    jne .b1                         ; clip that is full screen - then as 2)
    mov al, 2
    jmp short .b
.b1:
    cmp al, 2
    jne .b
    mov cl, [vb_meta+9]             ; ...or, before an end loop, the canvas
    xor ch, ch
.b:
    pushf
    cli
    mov [vb_fnext], al
    mov [vb_frows], cx
    mov byte [vb_fstep], 0
    mov word [vb_fticks], 0
    mov byte [vb_fon], 1
    mov byte [vb_mode], VM_FADE
    cmp al, 1                       ; THE TITLE FADED: its picture is over
    jne .b2                         ; now (the fade only dims the glass), so
    mov bx, [vb_lring]              ; the reader no longer reads it round -
    mov byte [bx+RG_VDONE], 1       ; the fade's time is opening 0's
.b2:
    cmp cl, [vb_meta+9]
    je .bk
    mov byte [vb_banddone], 0       ; (the band fades with it, and stays down)
    mov byte [vb_bandok], 0
.bk:
    popf
.on:
    cmp byte [vb_fon], 0
    je .out
    mov ax, [vb_fticks]             ; THE STEP THE CLOCK SAYS: a pass of the
    or ah, ah                       ; loop is the reader's and a frame's
    jnz .all                        ; wait too - on an XT ~0.5 s, on
    cmp al, [vb_fstep]              ; Hercules ~1.4 - and a step a pass
    jbe .out                        ; made a fade of 16 frames 8 and 23 s.
    cmp al, [vb_fsteps]             ; A step's mask holds every one before
    jbe .st                         ; it, so the steps skipped are ANDed in
.all:                               ; with it
    mov al, [vb_fsteps]
.st:
    mov [vb_fstep], al
    call vb_fmask
    mov al, [vb_fstep]              ; the sound: shifts 0..3, then silence
    xor ah, ah
    shl ax, 1
    shl ax, 1
    div byte [vb_fsteps]
    mov [vb_fvol], al
    mov al, [vb_fstep]
    cmp al, [vb_fsteps]
    jb .out
    mov byte [vb_fon], 0            ; THE FADE IS OVER
    mov al, [vb_fnext]
    cmp al, 1
    je vb_ftitle
    cmp al, 2
    je .end
    jmp vb_again
.end:
    pushf                           ; the end loop, from black, when its
    cli                             ; head is in
    mov byte [vb_fvol], 0
    mov byte [vb_mode], VM_HOLD
    mov byte [vb_estart], 1
    popf
.out:
    ret

; vb_ftitle - the title faded: its sound stopped, its ring the reader's,
; and opening 0 played from its own ring now (vb_play_init's way)
vb_ftitle:
    pushf
    cli
    mov byte [vb_fvol], 0
    mov bx, [vb_lring]
    mov byte [bx+RG_VDONE], 1
    mov byte [bx+RG_ADONE], 1
    mov word [vb_aring], 0
    mov byte [vb_tplay], 0
    mov byte [vb_owsnd], 1          ; (its sound with its frame 0)
    mov byte [vb_mode], VM_OWAIT    ; once its head is in its ring (vb_ogo:
    call vb_ogo                     ; it is not always, as the fade ends)
    popf
    ret

; vb_fmask - the fade's step [vb_fstep]: the first [vb_frows] rows of the
; screen ANDed with the step's 4 x 4 ordered threshold (VB_FMASK), so each
; step puts more of the frame to colour 0 and the last puts all of it
vb_fmask:
    mov al, [vb_fstep]              ; the threshold: 1..16 over the steps
    xor ah, ah
    mov cl, 4
    shl ax, cl
    div byte [vb_fsteps]
    dec al
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    add bx, vb_fmasks
    mov es, [vb_vseg]
    xor si, si
    cmp byte [vb_kind], VK_VGA4
    jne .nv
    jmp vb_fmaskv
.nv:
    cmp byte [vb_kind], VK_HERC
    jne .row
    jmp vb_fmaskh
.row:
    cmp si, [vb_frows]
    jae .done
    mov di, si                      ; row y: bank y & 1, 80 bytes a row
    shr di, 1
    mov ax, di
    mov cl, 4
    shl di, cl
    shl ax, cl
    shl ax, 1
    shl ax, 1
    add di, ax
    test si, 1
    jz .b0
    add di, 0x2000
.b0:
    mov ax, si
    and ax, 3
    push bx
    add bx, ax
    mov al, [bx]
    pop bx
    mov ah, al
    mov cx, 10
.w:
%rep 4
    and [es:di], ax
    inc di
    inc di
%endrep
    loop .w
    inc si
    jmp .row
.done:
    ret

; vb_fbits - AL = the CGA mask byte at [BX + y & 3] (SI = y) as ONE bit a
; pixel, eight pixels: the 4 x 4 pattern's row twice over. CX spent
vb_fbits:
    push bx
    mov ax, si
    and ax, 3
    add bx, ax
    mov ah, [bx]
    pop bx
    xor al, al
    mov cx, 4
.b:
    shl al, 1
    test ah, 0x80                   ; (a pixel's two bits are equal)
    jz .z
    or al, 1
.z:
    shl ah, 1
    shl ah, 1
    loop .b
    mov ah, al
    mov cl, 4
    shl al, cl
    or al, ah
    ret

; vb_fmaskh - the fade's step on a Hercules screen: [vb_frows] rows of 90
; bytes in its four banks. A row's mask is its y & 3's - its BANK's - so
; each bank is one run of words ANDed with one mask (not 348 rows, each
; with its own address and mask worked out)
vb_fmaskh:
    push bp
    push dx
    xor bp, bp                      ; the bank, 0..3
.bank:
    mov si, bp                      ; (y & 3 = the bank: its mask)
    call vb_fbits
    mov ah, al
    mov di, bp
    mov cl, 13
    shl di, cl
    mov cx, [vb_frows]              ; its rows: y = bank, bank + 4, ...
    sub cx, bp
    jbe .next
    add cx, 3
    shr cx, 1
    shr cx, 1
    push ax
    mov ax, 45
    mul cx
    mov cx, ax
    pop ax
.w:
    and [es:di], ax
    inc di
    inc di
    loop .w
.next:
    inc bp
    cmp bp, 4
    jb .bank
    pop dx
    pop bp
    ret

; vb_fmaskv - the fade's step on a VGA4 screen (mode 0Dh, 40 bytes a row):
; a byte read loads the latches, and the write puts Set/Reset's colour 0,
; in every plane, on the bits its Bit Mask names - the pixels this step
; takes - and the latches' on the rest. The GC put back as the BIOS left it
vb_fmaskv:
    mov dx, 0x3CE
    mov ax, 0x0000                  ; Set/Reset: colour 0
    out dx, ax
    mov ax, 0x0F01                  ; Enable Set/Reset: every plane
    out dx, ax
.row:
    cmp si, [vb_frows]
    jae .done
    call vb_fbits
    not al                          ; the Bit Mask: the pixels to put out
    mov ah, al
    mov al, 8
    mov dx, 0x3CE
    out dx, ax
    mov ax, 40
    mul si
    mov di, ax
    mov cx, 40
.w:
    mov al, [es:di]
    mov [es:di], al
    inc di
    loop .w
    inc si
    jmp .row
.done:
    mov dx, 0x3CE
    mov ax, 0xFF08                  ; Bit Mask: every bit
    out dx, ax
    mov ax, 0x0001                  ; Enable Set/Reset: none
    out dx, ax
    ret

; vb_atten - the chunk at ES:DI, CX samples, quieter by [vb_fvol] shifts
; (4: silent). From the hook (vb_chunk); keeps CX, DI
vb_atten:
    push bx
    push cx
    push di
    mov bx, cx
    mov cl, [vb_fvol]
    cmp cl, 4
    jae .mute
.l:
    mov al, [es:di]
    sub al, 128
    sar al, cl
    add al, 128
    stosb
    dec bx
    jnz .l
    jmp short .out
.mute:
    mov cx, bx
    mov al, 128
    rep stosb
.out:
    pop di
    pop cx
    pop bx
    ret

; vb_again - THE PLAY OVER (the UI task's, over a black screen): the hook
; stopped, the music's ring filled from its join again (vb_load's second
; play), the rings emptied, the battle from the bundle, the first head read,
; and the hook live again. A read that fails leaves the play
vb_again:
    mov byte [vb_ready], 0
    call vb_snd_close
    mov al, 0xFF
    mov bx, vb_rg0
    call vb_rreset
    mov al, 0xFF
    mov bx, vb_rg1
    call vb_rreset
    call vb_load
    jc .fail
    call vb_rstart
    jc .fail
    call vb_game_init
    call vb_play_init
    call vb_snd_open
    mov byte [vb_ready], 1
    ret
.fail:
    mov byte [vb_quit], 1
    ret

; the fade's thresholds: for each of 16, the byte mask a row keeps, by the
; row's y & 3 (2 bits a pixel, its first pixel the high two); a pixel goes
; to colour 0 once the threshold passes its 4 x 4 Bayer rank
vb_fmasks:
    db 0x3F, 0xFF, 0xFF, 0xFF
    db 0x3F, 0xFF, 0xF3, 0xFF
    db 0x33, 0xFF, 0xF3, 0xFF
    db 0x33, 0xFF, 0x33, 0xFF
    db 0x33, 0xCF, 0x33, 0xFF
    db 0x33, 0xCF, 0x33, 0xFC
    db 0x33, 0xCC, 0x33, 0xFC
    db 0x33, 0xCC, 0x33, 0xCC
    db 0x03, 0xCC, 0x33, 0xCC
    db 0x03, 0xCC, 0x30, 0xCC
    db 0x00, 0xCC, 0x30, 0xCC
    db 0x00, 0xCC, 0x00, 0xCC
    db 0x00, 0x0C, 0x00, 0xCC
    db 0x00, 0x0C, 0x00, 0xC0
    db 0x00, 0x00, 0x00, 0xC0
    db 0x00, 0x00, 0x00, 0x00
