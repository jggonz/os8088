; =============================================================================
; os8088 - apps/sbtest/sbtest.asm
;
; SBTEST: the sound Phase 4+5 gate package (docs/SOUND-PLAN.md). Exercises
; the stream + staging surface (slot 0x0100, SPEC.md 20.3/34.5/34.6) end to
; end from a real package: grant -> synthesise -> stage -> open -> poll ->
; close, exactly the model SPEC.md 34.5 prescribes (the package never holds
; an ES pointer into SND_SEG; the kernel refill/drain tasks pace the card).
; It is NEVER shipped on the apps disks (their directory order is pinned) -
; the Makefile builds it into its own scratch image, build/sbtest.img,
; mounted with
;   make test-snd SB16=1 TESTAPPS=build/sbtest.img
;
; What each input proves:
;   click    closed: verb 7 grant (16,000 B), synthesise a 1 kHz square
;            (8 samples/period at 8,000 Hz), verb 6 stage in 20 chunks,
;            verb 0 open fully staged - 2 s of tone play while the GUI
;            runs (drag a window meanwhile: the no-gaps assertion).
;            open: close + free (the click toggles).
;   key 'u'  the same but only 2,400 B granted+staged and never fed -
;            after ~0.3 s the ISR must pause output and verb 3 must read
;            underrun-paused, with NO stale audio looping (SPEC.md 34.5).
;            While a stream is already open (either direction) the open is
;            refused: the error digit '1' - the half-duplex/busy leg.
;   key 'p'  progressive open - a 16,000 B grant but only 2,400 B staged
;            (the caller explicitly accepting progressive-feed risk); it
;            underruns like 'u' until fed.
;   key 'f'  open: stage one more 800 B chunk past the valid end and
;            verb 1 feed the new total - an underrun-paused stream must
;            RESUME (D4h) and play it (SPEC.md 34.5). On an INPUT stream
;            the feed must refuse with err 7 (SPEC.md 34.6) - the digit
;            lands on line 1.
;   key 'r'  verb 4 open-in (Phase 5): grant 6,000 B, open a capture at
;            8,000 Hz - 'R' on success; while an out-stream is open the
;            open-in must refuse '1' (half-duplex, SPEC.md 34.3/34.6).
;   key 'd'  the verb 6 -> verb 5 staging round-trip (Phase 5): stage a
;            known 16-byte pattern into a scratch grant, read it back via
;            verb 5, compare - 'Y' good / 'N' mismatch on line 2. Proves
;            the kernel-staged copy out with no card dependence.
;   key 's'  verb 3 status into line 2: state digit + bytes consumed
;            (input: captured) ('S' = stale handle).
;   key 'c'  close + free, like the click.
;   close box mid-stream: the snd_release_inst -> sbl_release_inst teardown
;            leg must silence the stream and free the grant.
;
; Window procs run with the gfx lock held (SPEC.md 11) and preserve all
; registers, like every package.
;
; Ported from the other fork with the one edit every such port needs: over
; there a callback is far-called and ends in `retf`; here the kernel reaches
; it through the three-byte dispatcher in the package's own header
; (SPEC.md 20.1), so every proc below - the entry included - is an ordinary
; near proc with a near `ret`, and the `push cs` around sb_paint goes with it.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SBTEST', sb_entry

SB_CONT_W equ 188                   ; content: 190 outer - 2px borders
SB_CONT_H equ 81                    ; 100 outer - TITLE_H - 1px border

SB_CHUNK  equ 800                   ; one staged chunk (100 square periods)

; -----------------------------------------------------------------------------
; sb_entry - package entry point (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, from wm_create)
; -----------------------------------------------------------------------------
sb_entry:
    push si
    mov si, sb_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    ret                            ; far-called by the loader (SPEC.md 20.5)

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
    ret                            ; far-called W_PAINT (SPEC.md 20.5)

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
    call sb_paint                   ; a NEAR call: SI = win ptr still
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                            ; far-called W_ONCLICK (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; sb_onkey - W_ONKEY: 'u' underrun, 'p' progressive, 'f' feed, 'r' open-in,
;            'd' verb 6->5 round-trip, 's' status, 'c' close
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
    mov cx, 0x0303                  ; 2,400 B granted+staged, never fed: the
    call sb_start                   ; ISR must pause + report underrun (34.5).
    jmp .repaint                    ; Attempted even while open - the busy /
                                    ; half-duplex refusal digit '1' (34.3)
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
    jmp .repaint                    ; (on an INPUT stream: the err-7 digit)
.notf:
    cmp al, 'r'
    jne .notr
    call sb_rec                     ; verb 4 open-in - or the half-duplex
    jmp .repaint                    ; refusal '1' while an out-stream runs
.notr:
    cmp al, 'd'
    jne .notd
    call sb_rd5                     ; the verb 6 -> verb 5 round-trip
    jmp .repaint
.notd:
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
    call sb_paint                   ; near, like every proc here
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                            ; far-called W_ONKEY (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; sb_start - grant + synthesise + stage + open one stream
; in:  CL = chunks to stage (valid data), CH = chunks to grant (CL <= CH;
;      CL < CH is the progressive open)
; out: nothing; sb_s1 patched ('K' open, or the error digit), sb_openf set
;      on success; preserves all registers
;
; The candidate grant rides in sb_gtmp until the open succeeds - a refused
; attempt (e.g. the deliberate busy/half-duplex probes) frees its scratch
; grant and leaves sb_goff/sb_tot - the OPEN stream's record - untouched.
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
    mov [sb_ttmp], ax
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
    mov [sb_gtmp], si
    mov ax, si                      ; show the grant offset on line 1
    mov bx, sb_s1+2
    call sb_putu5
    call sb_synth                   ; build the 1 kHz square chunk
    mov di, si                      ; DI = staging cursor (grant offset)
    mov dx, [sb_ttmp]
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
    mov si, [sb_gtmp]
    mov cx, [sb_ttmp]
    mov dx, 8000
    call OSAPI_SND_STREAM           ; out AL = err, AH = handle
    or al, al
    jnz .freefail
    mov [sb_hand], ah
    mov ax, [sb_gtmp]               ; commit the record only on success
    mov [sb_goff], ax
    mov ax, [sb_ttmp]
    mov [sb_tot], ax
    mov byte [sb_openf], 1
    mov byte [sb_s1+10], 'K'
    jmp .out
.freefail:                          ; refused after the grant: give it back
    push ax
    mov si, [sb_gtmp]
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
; out: nothing; [sb_tot] extended on success; the verb-1 result lands on
;      line 1 ('K', the error digit, or 'S' stale); preserves all registers
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
    jnz .digit                      ; grant full (or gone): nothing to feed
    mov cx, [sb_tot]
    add cx, SB_CHUNK
    mov ah, [sb_hand]
    mov al, 1                       ; verb 1 feed: CX = the new valid total
    call OSAPI_SND_STREAM           ; - an underrun-paused stream resumes;
    or ax, ax                       ; an INPUT stream refuses err 7 (34.6)
    jnz .digit
    mov [sb_tot], cx                ; committed: the kernel took the total
    mov byte [sb_s1+10], 'K'
    jmp .out
.digit:
    cmp ax, 0xFFFF                  ; stale handle shows as 'S'
    jne .num
    mov byte [sb_s1+10], 'S'
    jmp .out
.num:
    add al, '0'
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
; sb_rec - verb 4 open-in: grant 6,000 B and open a capture at 8,000 Hz
; in:  nothing
; out: nothing; sb_s1 patched ('R' recording, or the error digit - '1' is
;      the half-duplex refusal while an out-stream is open); sb_openf set
;      and sb_tot = 0 on success (so 'f' probes the input-feed err 7);
;      preserves all registers
; -----------------------------------------------------------------------------
sb_rec:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, 6000
    mov ax, 0x0007                  ; verb 7 grant, sub-op 0: alloc
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .fail
    mov [sb_gtmp], si
    mov ax, si                      ; show the grant offset on line 1
    mov bx, sb_s1+2
    call sb_putu5
    mov ax, 0x0004                  ; verb 4 open-in: capture capacity CX
    mov si, [sb_gtmp]               ; into the grant at SI (SPEC.md 20.3)
    mov cx, 6000
    mov dx, 8000
    call OSAPI_SND_STREAM           ; out AL = err, AH = handle
    or al, al
    jnz .freefail
    mov [sb_hand], ah
    mov ax, [sb_gtmp]
    mov [sb_goff], ax
    mov word [sb_tot], 0            ; no valid length: 'f' now probes the
    mov byte [sb_openf], 1          ; input-feed refusal (SPEC.md 34.6)
    mov byte [sb_s1+10], 'R'
    jmp .out
.freefail:                          ; refused after the grant: give it back
    push ax
    mov si, [sb_gtmp]
    mov ax, 0x0107                  ; verb 7, sub-op 1: free
    call OSAPI_SND_STREAM
    pop ax
.fail:
    add al, '0'                     ; the error digit (1..8)
    mov [sb_s1+10], al
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sb_rd5 - the verb 6 -> verb 5 staging round-trip (SPEC.md 34.6)
; in:  nothing
; out: nothing; line 2's tail shows 'Y' (pattern survived the round-trip),
;      'N' (mismatch), 'E' (a copy verb refused) or 'G' (no grant);
;      preserves all registers
;
; Proves the kernel-staged copy OUT: a known 16-byte pattern goes in via
; verb 6, comes back via verb 5 into different package memory, and must
; compare equal. Uses its own scratch grant, so it runs any time - even
; beside an open stream - and frees it before returning.
; -----------------------------------------------------------------------------
sb_rd5:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, 32
    mov ax, 0x0007                  ; scratch grant
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .nogrant
    mov [sb_gtmp], si
    mov di, sb_pat                  ; the pattern: 16 distinct bytes
    mov cx, 16
    mov al, 0xA5
.p:
    mov [di], al
    inc di
    add al, 7
    loop .p
    mov di, [sb_gtmp]               ; verb 6: pattern -> grant
    mov si, sb_pat
    mov cx, 16
    mov ax, 0x0006
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .everb
    mov di, sb_pat2                 ; scrub the landing zone first
    mov cx, 16
    mov al, 0
.z:
    mov [di], al
    inc di
    loop .z
    mov si, [sb_gtmp]               ; verb 5: grant -> package memory
    mov di, sb_pat2
    mov cx, 16
    mov ax, 0x0005
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .everb
    mov si, sb_pat                  ; compare the two copies
    mov di, sb_pat2
    mov cx, 16
.c:
    mov al, [si]
    cmp al, [di]
    jne .bad
    inc si
    inc di
    loop .c
    mov al, 'Y'
    jmp .mark
.bad:
    mov al, 'N'
    jmp .mark
.everb:
    mov al, 'E'
.mark:
    mov [sb_s2+15], al
    mov si, [sb_gtmp]               ; free the scratch grant
    mov ax, 0x0107
    call OSAPI_SND_STREAM
    jmp .out
.nogrant:
    mov byte [sb_s2+15], 'G'
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
    cmp ax, 0xFFFF                  ; state, DX = consumed/captured
    jne .num
    mov byte [sb_s2+3], 'S'
    jmp .con
.num:
    add al, '0'                     ; 0 playing / 1 paused / 2 ended
    mov [sb_s2+3], al
.con:
    mov ax, dx
    mov bx, sb_s2+7
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
sb_s2:      db 'st:- c:----- 5:-', 0 ; [+3] state, [+7..11] consumed,
                                    ; [+15] verb 6->5 round-trip result
sb_s3:      db 'clk u p f r d s c', 0

; --- bss (zeroed by the loader, SPEC.md 20.2) --------------------------------
sb_goff  equ os88_image_end + 0     ; word: the OPEN stream's grant offset
sb_tot   equ os88_image_end + 2     ; word: its staged total, bytes
sb_hand  equ os88_image_end + 4     ; byte: stream handle
sb_openf equ os88_image_end + 5     ; byte: a stream is open
sb_gtmp  equ os88_image_end + 6     ; word: candidate grant (committed to
                                    ;   sb_goff only when an open succeeds)
sb_ttmp  equ os88_image_end + 8     ; word: candidate staged total
sb_chunk equ os88_image_end + 10    ; 800 B: the synthesised chunk
sb_pat   equ os88_image_end + 810   ; 16 B: the round-trip pattern out...
sb_pat2  equ os88_image_end + 826   ; 16 B: ...and what verb 5 brought back

    OS88_BSS 842
    OS88_IMAGE_END
