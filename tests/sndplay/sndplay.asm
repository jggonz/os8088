; =============================================================================
; os8088 - tests/sndplay/sndplay.asm
;
; SNDPLAY: the PWM clip's probe (SPEC.md 34.4) - apps/os88pcm.inc's player on
; the OSAPI_SND_PLAY door: the grant, its
; refusals, the clip itself and its release, on the desktop and inside an
; FSXF_FASTTICK bracket (whose sub-tick the door parks and hands back,
; SPEC.md 53.2.1). NEVER shipped: tests/sndplay.py assembles it into a
; scratch floppy of its own and drives it on MartyPC.
;
;   key 'p'  on the desktop: a tone the clip must steal (34.3), the two range
;            refusals (a rate below 4,679 and above 16,124 Hz: AX = 2), the
;            clip at 8,000 Hz, then a tone AFTER it - which is only granted
;            if the release left channel 2 free (snd_ch2mode 0) - and off.
;   key 'f'  the clip again inside an OSAPI_FSX_RUN bracket with
;            FSXF_FASTTICK; sp_fmark is where the harness reads the sub-tick
;            the clip must have handed back.
;
; Every answer lands in sp_res as a word - AL, and CF in AH - and sp_done
; counts the legs finished, so the harness reads results rather than pixels.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SNDPLAY', sp_entry

SP_N     equ 300                    ; samples in the clip
SP_RATE  equ 8000                   ; N = 1193182 / 8000 = 149

sp_entry:
    push si
    mov si, sp_tpl
    call OSAPI_WM_CREATE
    pop si
    ret

sp_paint:
    ret

; sp_put - bank AL and CF (as AH) at [sp_res + BX], BX += 2. Flags spent
sp_put:
    mov ah, 0
    adc ah, 0
    mov [sp_res+bx], ax
    inc bx
    inc bx
    ret

; sp_clip - os88pcm_play the clip at DX Hz. out: as the library
sp_clip:
    push es
    push si
    push cx
    push ds
    pop es
    mov si, sp_samp
    mov cx, SP_N
    call os88pcm_play
    pop cx
    pop si
    pop es
    ret

sp_onkey:
    push ax
    push bx
    push cx
    push dx
    cmp al, 'p'
    je .desk
    cmp al, 'f'
    je .fsx
    jmp .out
.desk:
    xor bx, bx
    mov ax, 880                     ; [0] a tone the clip will steal
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
    call sp_put
    mov dx, 1000                    ; [1] below the range: AX = 2
    call sp_clip
    call sp_put
    mov dx, 20000                   ; [2] above it: AX = 2
    call sp_clip
    call sp_put
    mov dx, SP_RATE                 ; [3] THE CLIP: AX = 0
    call sp_clip
    call sp_put
    mov ax, 440                     ; [4] a tone after it: granted only if
    xor cx, cx                      ; channel 2 came back free
    mov dl, 0x40
    call OSAPI_SND_TONE
    call sp_put
    xor ax, ax                      ; [5] and off
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
    call sp_put
    inc byte [sp_done]
    jmp short .out
.fsx:
    mov bx, si                      ; the bracket, with the sub-tick armed
    mov ax, sp_main
    mov cx, FSXF_FASTTICK
    call OSAPI_FSX_RUN
    mov bx, 14                      ; [7] the bracket's own answer
    call sp_put
    inc byte [sp_done]
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sp_main:                            ; the exclusive main, same mode
    push ax
    push bx
    push dx
    mov dx, SP_RATE                 ; [6] the clip, inside
    call sp_clip
    mov bx, 12
    call sp_put
sp_fmark:                           ; the harness reads [sch_fast] HERE
    nop
    pop dx
    pop bx
    pop ax
    ret

sp_tpl:
    dw 40, 40, 160, 40
    dw sp_ttl, sp_paint, sp_onkey, 0
sp_ttl: db 'SndPlay', 0

sp_samp:                            ; the clip: (i * 37) & 0xFF, a pattern
%assign i 0                         ; that visits the whole sample range
%rep SP_N
    db (i * 37) & 0xFF
%assign i i + 1
%endrep

%include "os88pcm.inc"

SP_BSS equ 17
    OS88_BSS SP_BSS
    OS88_IMAGE_END

sp_res  equ os88_image_end + 0      ; 8 words
sp_done equ os88_image_end + 16     ; byte
