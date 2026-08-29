; =============================================================================
; os8088 - tests/fmtest/fmtest.asm
;
; FMTEST: the committed sound Phase 3 gate package (docs/SOUND-PLAN.md).
; Exercises the public FM surface (slot 0x00F8, SPEC.md 20.3/34.2) end to
; end, so the AdLib gate can be re-run mechanically. It is NEVER shipped on
; the apps disks (their directory order is pinned) - the Makefile builds it
; into its own scratch image, build/fmtest.img, mounted with
; `make test-snd ADLIB=1 TESTAPPS=build/fmtest.img`.
;
; The kernel reaches a callback through the three-byte
; dispatcher in the package's own header (SPEC.md 20.1), so every proc below
; - the entry included - is an ordinary near proc with a near `ret`. A `retf`
; here returns into the loader's stack frame and hangs the machine
; at the first paint.
;
; What each input proves:
;   click 1  verb 2 patch-load (DS:SI, carrier MULT=2) on channel 0, then
;            verb 0 note-on 440 Hz. The carrier multiplier doubles the
;            sounding frequency, so sndcheck reading a dominant of 880 Hz
;            proves the CALLER'S patch bytes reached the chip - the exact
;            path the P3 review found severed (osapi_snd_fm destroyed SI).
;            The window shows 'FM patch: K' (both CF=0), 'P' (the patch
;            verb was refused) or 'N' (the note-on was). Splitting the two
;            Splitting the two earned itself immediately: the first
;            failure was N, which said the
;            frequency never reached the driver - drv_svc_call was eating BX.
;   click 2  patch + note-on 660 Hz on channel 1 -> the sustained chord
;            (sounding 880 + 1320).
;   click 3  verb 3 all-off: both channels keyed off and released.
;   key 'b'  OSAPI_SND_TONE 880 Hz for 3 ticks - with an AdLib present the
;            auto route sounds it on OPL2 channel 8 and snd_tick's
;            sanctioned single-write key-off expires it (SPEC.md 8.2/34.3).
;   close box mid-chord: the snd_release_inst -> opl_release_inst teardown
;            leg must silence the chord.
;
; Window procs run with the gfx lock held (SPEC.md 11) and preserve all
; registers, like every package.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FMTEST', ft_entry

FT_CONT_W equ 168                   ; content: 170 outer - 2px borders
FT_CONT_H equ 59                    ; 78 outer - TITLE_H - 1px border

; -----------------------------------------------------------------------------
; ft_entry - package entry point (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, from wm_create)
; -----------------------------------------------------------------------------
ft_entry:
    push si
    mov si, ft_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    ret                             ; NEAR: the kernel reaches every proc in a
                                    ; package through the dispatcher in its
                                    ; own header (SPEC.md 20.1), so a package
                                    ; author never writes retf

; -----------------------------------------------------------------------------
; ft_paint - W_PAINT: the patch result line and the stage line
; in:  SI = window ptr; caller holds the gfx lock; content already white
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov cx, ax                      ; x = left + 8 (before AL is repurposed)
    add cx, 8
    mov al, CBLACK
    call OSAPI_SET_COLOR
    add dx, 12
    mov si, ft_s_line1
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    add dx, 16
    mov al, [ft_stage]              ; line 2: the stage name
    mov si, ft_s_idle
    or al, al
    jz .draw
    mov si, ft_s_one
    cmp al, 1
    je .draw
    mov si, ft_s_two
.draw:
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                             ; near, like every callback here

; -----------------------------------------------------------------------------
; ft_onclick - W_ONCLICK: advance the gate stage (see file header)
; in:  CX = x, DX = y (unused), SI = window ptr; lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [ft_stage]
    or al, al
    jz .start
    cmp al, 1
    je .chord
    mov al, 3                       ; --- click 3: all-off ----------------
    call OSAPI_SND_FM
    mov byte [ft_stage], 0
    jmp .repaint
.start:                             ; --- click 1: patch ch0 + 440 --------
    mov cl, 0
    mov bx, 440                     ; sounds 880 through the MULT=2 patch
    call ft_voice
    mov byte [ft_stage], 1
    jmp .repaint
.chord:                             ; --- click 2: patch ch1 + 660 --------
    mov cl, 1
    mov bx, 660                     ; sounds 1320: the chord
    call ft_voice
    mov byte [ft_stage], 2
.repaint:                           ; white-fill own content + redraw (the
    mov al, CWHITE                  ; files.inc idiom - lock already held)
    call OSAPI_SET_COLOR
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov bx, dx                      ; y1 = top
    mov cx, ax
    add cx, FT_CONT_W-1             ; x2
    add dx, FT_CONT_H-1             ; y2
    call OSAPI_GFX_FILL
    call ft_paint                   ; a NEAR call here: SI = win ptr still
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_voice - patch-load ft_patch onto one channel, then key a note
; in:  CL = channel, BX = frequency Hz
; out: nothing; ft_s_line1's result char = 'K' (both CF=0) or 'E'
; -----------------------------------------------------------------------------
ft_voice:
    push ax
    push si
    mov al, 2                       ; verb 2: patch-load, DS:SI = the patch
    mov si, ft_patch                ; (the P3 blocker's severed input)
    call OSAPI_SND_FM
    jc .errp
    mov al, 0                       ; verb 0: note-on
    call OSAPI_SND_FM
    jc .errn
    mov byte [ft_s_line1+10], 'K'
    jmp .out
.errp:
    mov byte [ft_s_line1+10], 'P'   ; the patch verb refused
    jmp .out
.errn:
    mov byte [ft_s_line1+10], 'N'   ; ...or the note-on
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_onkey - W_ONKEY: 'b' = the tone-expiry leg (3-tick 880 Hz)
; in:  AL = ascii, AH = scan, SI = window ptr; lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_onkey:
    push ax
    push cx
    push dx
    cmp al, 'b'
    jne .out
    mov ax, 880
    mov cx, 3                       ; 3 ticks: snd_tick expires it (8.2)
    mov dl, 0x40                    ; the documented package priority
    call OSAPI_SND_TONE
.out:
    pop dx
    pop cx
    pop ax
    ret

; --- the gate patch (11 bytes, SPEC.md 34.2 layout) --------------------------
; A near-sine voice whose carrier runs at TWICE the keyed frequency: the
; modulator sits at maximum attenuation (-47.25 dB - no audible sidebands)
; and the carrier is EGT|MULT=2, full level, instant attack, release 7.
; Keying 440 Hz must read as 880 Hz dominant - only possible if these
; bytes, not driver-table garbage, reached the operator registers.
ft_patch:
    db 0x21, 0x3F, 0xF0, 0x07, 0x00 ; modulator: EGT|MULT1, -47.25 dB
    db 0x22, 0x00, 0xF0, 0x07, 0x00 ; carrier: EGT|MULT2, full level, sine
    db 0x00                         ; C0h: FM connection, no feedback

; --- window template (SPEC.md 11: 16 bytes, 8 words) -------------------------
ft_tpl:
    dw 360, 300, 170, 78            ; x, y, w, h -> content 168 x 59
    dw ft_ttl, ft_paint, ft_onkey, ft_onclick

ft_ttl:     db 'FM Test', 0
ft_stage:   db 0                    ; 0 idle / 1 one note / 2 chord
ft_s_line1: db 'FM patch: -', 0     ; [+10] = 'K' ok / 'P' / 'N' refused
ft_s_idle:  db 'idle - click me', 0
ft_s_one:   db '440 keyed (880)', 0
ft_s_two:   db 'chord 880+1320', 0

    OS88_IMAGE_END
