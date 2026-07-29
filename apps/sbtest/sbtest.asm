; =============================================================================
; os8088 - apps/sbtest/sbtest.asm
;
; SBTEST: the sound Phase 4 gate package (docs/SOUND-PLAN.md). Exercises the
; stream + staging surface (slot 0x0088, SPEC.md 20.3/34.5/34.6) end to end
; from a real package: grant -> synthesise -> stage -> open -> poll -> close,
; exactly the model SPEC.md 34.5 prescribes (the package never holds an ES
; pointer into SND_SEG; the kernel refill task paces the card). It is NEVER
; shipped on the apps disks (their directory order is pinned) - the Makefile
; builds it into its own scratch image, build/sbtest.img, mounted with
;   make test-snd SB16=1 TESTAPPS=build/sbtest.img
;
; What each input proves:
;   click    closed: verb 7 grant (16,000 B), synthesise a 1 kHz square
;            (8 samples/period at 8,000 Hz), verb 6 stage in 20 chunks,
;            verb 0 open fully staged - 2 s of tone play while the GUI
;            runs (drag a window meanwhile: the no-gaps assertion).
;            open: close + free (the click toggles).
;   key 'u'  closed: the same but only 2,400 B granted+staged and never
;            fed - after ~0.3 s the ISR must pause output and verb 3 must
;            read underrun-paused, with NO stale audio looping (SPEC.md
;            34.5).
;   key 'p'  closed: progressive open - a 16,000 B grant but only 2,400 B
;            staged (the caller explicitly accepting progressive-feed
;            risk); it underruns like 'u' until fed.
;   key 'f'  open: stage one more 800 B chunk past the valid end and
;            verb 1 feed the new total - an underrun-paused stream must
;            RESUME (D4h) and play it (SPEC.md 34.5).
;   key 's'  verb 3 status into line 2: state digit + bytes consumed
;            ('S' = stale handle).
;   key 'c'  close + free, like the click.
;   close box mid-stream: the snd_release_inst -> sbl_release_inst teardown
;            leg must silence the stream and free the grant.
;
; Window procs run with the gfx lock held (SPEC.md 11) and preserve all
; registers, like every package.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SBTEST', sb_entry

SB_CONT_W equ 188                   ; content: 190 outer - 2px borders
SB_CONT_H equ 81                    ; 100 outer - TITLE_H - 1px border

SB_CHUNK  equ 800                   ; one staged chunk (100 square periods)

; -----------------------------------------------------------------------------
; sb_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, from wm_create)
; -----------------------------------------------------------------------------
sb_entry:
    push si
    mov si, sb_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    ret

; -----------------------------------------------------------------------------
; sb_paint - W_PAINT: the three status lines
; in:  SI = window ptr; caller holds the gfx lock; content already white
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sb_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov cx, ax
    add cx, 8
    mov al, CBLACK
    call OSAPI_SET_COLOR
    add dx, 12
    mov si, sb_s1                   ; 'g:<offset> o:<result>'
    call OSAPI_FONT_STR
    add dx, 16
    mov si, sb_s2                   ; 'st:<state> c:<consumed>'
    call OSAPI_FONT_STR
    add dx, 16
    mov si, sb_s3                   ; the legend
    call OSAPI_FONT_STR
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_onclick - W_ONCLICK: toggle the full 2s stream open/closed
; in:  CX = x, DX = y (unused), SI = window ptr; lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [sb_openf], 0
    jne .close
    mov cx, 0x1414                  ; CH = CL = 20 chunks: 16,000 B granted
    call sb_start                   ; and staged = 2 s at 8 kHz
    jmp .repaint
.close:
    call sb_close
.repaint:                           ; white-fill own content + redraw (the
    mov al, CWHITE                  ; files.inc idiom - lock already held)
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov bx, dx                      ; y1 = top
    mov cx, ax
    add cx, SB_CONT_W-1             ; x2
    add dx, SB_CONT_H-1             ; y2
    call OSAPI_GFX_FILL
    call sb_paint                   ; SI = window ptr still
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_onkey - W_ONKEY: 'u' underrun, 'p' progressive, 'f' feed, 's' status,
;            'c' close
; in:  AL = ascii, AH = scan, SI = window ptr; lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp al, 'u'
    jne .notu
    cmp byte [sb_openf], 0
    jne .out
    mov cx, 0x0303                  ; 2,400 B granted+staged, never fed: the
    call sb_start                   ; ISR must pause + report underrun (34.5)
    jmp .repaint
.notu:
    cmp al, 'p'
    jne .notp
    cmp byte [sb_openf], 0
    jne .out
    mov cx, 0x1403                  ; 16,000 B grant, 2,400 B staged: the
    call sb_start                   ; progressive open ('f' extends it)
    jmp .repaint
.notp:
    cmp al, 'f'
    jne .notf
    cmp byte [sb_openf], 0
    je .out
    call sb_feed                    ; stage one more chunk + verb 1 feed
    jmp .repaint
.notf:
    cmp al, 's'
    jne .nots
    call sb_stat
    jmp .repaint
.nots:
    cmp al, 'c'
    jne .out
    cmp byte [sb_openf], 0
    je .out
    call sb_close
.repaint:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT
    mov bx, dx
    mov cx, ax
    add cx, SB_CONT_W-1
    add dx, SB_CONT_H-1
    call OSAPI_GFX_FILL
    call sb_paint
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_start - grant + synthesise + stage + open one stream
; in:  CL = chunks to stage (valid data), CH = chunks to grant (CL <= CH;
;      CL < CH is the progressive open)
; out: nothing; sb_s1 patched ('K' open, or the error digit), sb_openf set
;      on success; preserves all registers
; -----------------------------------------------------------------------------
sb_start:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, SB_CHUNK
    mov al, cl
    mov ah, 0
    mul bx                          ; AX = staged bytes (chunks are small)
    mov [sb_tot], ax
    mov al, ch
    mov ah, 0
    mul bx                          ; AX = granted bytes
    push cx
    mov cx, ax
    mov ax, 0x0007                  ; verb 7 grant, sub-op 0: alloc CX bytes
    call OSAPI_SND_STREAM           ; out AX = 0 + SI = grant offset
    pop cx
    or ax, ax
    jnz .fail
    mov [sb_goff], si
    mov ax, si                      ; show the grant offset on line 1
    mov bx, sb_s1+2
    call sb_putu5
    call sb_synth                   ; build the 1 kHz square chunk
    mov di, si                      ; DI = staging cursor (grant offset)
    mov dx, [sb_tot]
.stage:
    mov ax, 0x0006                  ; verb 6 stage: DS:SI -> grant at DI
    mov si, sb_chunk
    mov cx, SB_CHUNK
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .freefail
    add di, SB_CHUNK
    sub dx, SB_CHUNK
    jnz .stage
    mov ax, 0x0000                  ; verb 0 open-out: fully staged
    mov si, [sb_goff]
    mov cx, [sb_tot]
    mov dx, 8000
    call OSAPI_SND_STREAM           ; out AL = err, AH = handle
    or al, al
    jnz .freefail
    mov [sb_hand], ah
    mov byte [sb_openf], 1
    mov byte [sb_s1+10], 'K'
    jmp .out
.freefail:                          ; refused after the grant: give it back
    push ax
    mov si, [sb_goff]
    mov ax, 0x0107                  ; verb 7, sub-op 1: free
    call OSAPI_SND_STREAM
    pop ax
.fail:
    add al, '0'                     ; the error digit (1..8)
    mov [sb_s1+10], al
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_close - close the stream and free its grant
; in:  nothing ([sb_hand]/[sb_goff] live)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sb_close:
    push ax
    push cx
    push si
    mov ah, [sb_hand]
    mov al, 2                       ; verb 2: close
    call OSAPI_SND_STREAM
    mov si, [sb_goff]
    mov ax, 0x0107                  ; verb 7, sub-op 1: free
    call OSAPI_SND_STREAM
    mov byte [sb_openf], 0
    mov byte [sb_s1+10], '-'
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_feed - stage one more chunk past the valid end, then verb 1 feed
; in:  nothing (a stream is open; the grant must have room)
; out: nothing; [sb_tot] extended on success; preserves all registers
; -----------------------------------------------------------------------------
sb_feed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, [sb_goff]               ; stage at the current valid end
    add di, [sb_tot]
    mov ax, 0x0006                  ; verb 6 stage
    mov si, sb_chunk
    mov cx, SB_CHUNK
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .out                        ; grant full (or gone): nothing to feed
    mov cx, [sb_tot]
    add cx, SB_CHUNK
    mov [sb_tot], cx
    mov ah, [sb_hand]
    mov al, 1                       ; verb 1 feed: CX = the new valid total
    call OSAPI_SND_STREAM           ; - an underrun-paused stream resumes
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_stat - poll verb 3 into line 2
; in:  nothing
; out: nothing; sb_s2 patched: state digit ('S' = stale) + bytes consumed
; -----------------------------------------------------------------------------
sb_stat:
    push ax
    push bx
    push dx
    mov ah, [sb_hand]
    mov al, 3                       ; verb 3: status - THE notification
    call OSAPI_SND_STREAM           ; mechanism (SPEC.md 20.3); out AX =
    cmp ax, 0xFFFF                  ; state, DX = consumed
    jne .num
    mov byte [sb_s2+3], 'S'
    jmp .con
.num:
    add al, '0'                     ; 0 playing / 1 underrun / 2 ended
    mov [sb_s2+3], al
.con:
    mov ax, dx
    mov bx, sb_s2+8
    call sb_putu5
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_synth - fill sb_chunk with 100 periods of a 1 kHz square at 8 kHz
; in:  nothing
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sb_synth:
    push ax
    push cx
    push di
    mov di, sb_chunk
    mov cx, SB_CHUNK / 8
.rep:
    mov ax, 0xD8D8                  ; 4 samples high...
    mov [di], ax
    mov [di+2], ax
    mov ax, 0x2828                  ; ...4 samples low: 8-bit unsigned
    mov [di+4], ax
    mov [di+6], ax
    add di, 8
    loop .rep
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_putu5 - AX as 5 zero-padded decimal chars at DS:BX
; in:  AX = value, BX -> destination
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sb_putu5:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, 10
    add bx, 4                       ; write the digits backwards
    mov cx, 5
.d:
    xor dx, dx
    div si
    add dl, '0'
    mov [bx], dl
    dec bx
    loop .d
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) -------------------------
sb_tpl:
    dw 340, 290, 190, 100           ; x, y, w, h -> content 188 x 81
    dw sb_ttl, sb_paint, sb_onkey, sb_onclick

sb_ttl:     db 'SB Test', 0
sb_s1:      db 'g:----- o:-', 0     ; [+2..6] grant offset, [+10] result
sb_s2:      db 'st:-  c:-----', 0   ; [+3] state, [+8..12] consumed
sb_s3:      db 'clk=2s u p f s c', 0

; --- bss (zeroed by the loader, SPEC.md 20.2) --------------------------------
sb_goff  equ os88_image_end + 0     ; word: grant offset
sb_tot   equ os88_image_end + 2     ; word: staged total, bytes
sb_hand  equ os88_image_end + 4     ; byte: stream handle
sb_openf equ os88_image_end + 5     ; byte: a stream is open
sb_chunk equ os88_image_end + 8     ; 800 B: the synthesised chunk

    OS88_BSS 808
    OS88_IMAGE_END
