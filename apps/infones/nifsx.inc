; =============================================================================
; os8088 - apps/infones/nifsx.inc      SPEC.md 53's exclusive bracket
;
; Part of INFONES (SPEC.md 91.6). Derived from InfoNES fe3295c0 under
; Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
; InfoNES fe3295c0, restructured for 8086 real mode. THIS FILE carries no
; InfoNES material: InfoNES's front ends are Win32, SDL, GTK and a Zaurus
; framebuffer, and none of them is this.
;
; %included by apps/infones/infones.asm AFTER the compiled C.
;
; -----------------------------------------------------------------------------
; WHY THE BRACKET IS HERE AND NOT IN THE SDK
; -----------------------------------------------------------------------------
; OSAPI_FSX_* is DELIBERATELY not wrapped for C: apps/cc/os88.h says "the
; rules are the whole feature and none of them is checkable from C". So the
; whole of it is written in the package, which costs no bytes in any other C
; package and needs no crt0 change - and exactly ONE cdecl helper crosses back
; into the C, so that nimenu.c can grey the mode and clip items before entry.
; apps/tank/tank.asm:431-475 and apps/cyclone/cyclone.asm:8086-8190 are the
; shapes this copies.
;
; -----------------------------------------------------------------------------
; WAVE 1 SCOPE, AND WHY IT IS THE CAPS READ AND NOTHING ELSE
; -----------------------------------------------------------------------------
; The bracket runs a GAME, and there is no composer until wave 2 (niband.inc).
; What wave 1 needs of this file is the ONE thing the menu already asks:
; whether this window's display offers a fullscreen mode at all, which is what
; greys `Full screen` with a true fact rather than a guess (SPEC.md 47).
;
; Wave 2 adds, in this file and in this order: OSAPI_FSX_RUN with
; FSXF_FASTTICK and the near entry it names, OSAPI_FSX_MODE per adapter,
; OSAPI_FSX_SURF, OSAPI_FSX_WAIT on FSXW_FRAME as the FRAME BUDGET (never
; FSXW_VSYNC, which is the adapter's rate and not the NES's - SPEC.md 91.6.3),
; OSAPI_FSX_PAGE for Mode X's flip, the DAC programming (64 colours mirrored
; four times), the four present routines, and the int 16h typed-key poll
; beside the os88_key_down sweep.
;
; ONE RULE THAT IS ALREADY TRUE AND MUST STAY TRUE: **no ovl_* is called from
; inside the bracket, ever** (SPEC.md 91.6.5). It would be legal by 73.14's
; rules and by the SP gate - the bracket runs on the UI task - and it would
; read a floppy while the machine is in a foreign video mode, with any refusal
; toasted onto a bar the user cannot see.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], ...; the answer in AX; BP,
; DS, SS:SP and DF preserved.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; int ni_fsx_caps(void *win) - OSAPI_FSX_CAPS (slot 0x02C0) for the display
; THIS window is on.
;
; in:  [bp+4] = the window pointer os88_main was given
; out: AX = the FSXM bitmask (bit n = mode id n is settable on that display),
;      0 = this display offers no fullscreen mode at all
;
; IT IS CALLED AT USE AND ITS ANSWER IS NEVER BANKED. os88api.inc:2438-2449 is
; explicit: on a two-display machine this is a question about a DISPLAY and a
; window moves between them, so an answer banked in an entry proc describes
; the window you were LAUNCHED FROM, because yours does not exist yet
; (SPEC.md 39.18.2). The C calls it from ni_menu_state, which runs on every
; state change.
;
; DL comes back holding that display's VID_* kind and this helper drops it:
; SPEC.md 39.24's rule is to branch on BPP and not on the adapter's name, and
; the mask above already answers the only question the menu has.
; -----------------------------------------------------------------------------
_ni_fsx_caps:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [bp+4]
    call OSAPI_FSX_CAPS             ; AX = the settable ids, DL = the
                                    ; display's VID_* kind, which this helper
                                    ; drops with the pop below
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; =============================================================================
; WAVE 2 - THE BRACKET (SPEC.md 91.6)
;
; Six routines and a table. The frame loop itself is C - nirun.c's
; `ni_bracket` - because what it does is arithmetic on a wall clock and a
; scanline counter, and because the hot part of a frame is not the loop but
; the core and the composer, which are both assembly already (SPEC.md 73.11's
; own test). What is HERE is exactly the part C cannot express: the bracket
; call itself, the mode set, the DAC, `int 16h`, and the stack sentinel.
;
; ONE RULE THAT IS ALREADY TRUE AND MUST STAY TRUE: **no ovl_* is called from
; inside the bracket, ever** (SPEC.md 91.6.5).
; =============================================================================

section .bss

ni_fsi:      resb FSI_SIZE          ; what OSAPI_FSX_MODE filled in
ni_entered:  resw 1                 ; THE MODE WAS SET. OSAPI_FSX_RUN's own CF
                                    ;   answers "the bracket was entered", and
                                    ;   that is NOT the question the caller
                                    ;   has: a refused OSAPI_FSX_MODE returns
                                    ;   from ni_fsx_main normally, so CF is
                                    ;   clear for a session that never set a
                                    ;   mode and never drew a pixel, and
                                    ;   infones.c's refusal sentence was
                                    ;   unreachable - the silently-ignored
                                    ;   menu pick LESSONS.md 1 names
%ifdef NI_STKPROBE
ni_stk_hi:   resw 1                 ; the stack high-water the sentinel read
_ni_stk_lo:  resw 1                 ; ...and the same number, for the C
%endif

section .text

%ifdef NI_STKPROBE
NI_STK_PROBE equ 128                ; how far below SP the sentinel is laid.
                                    ; THE PROBE IS A GATE INSTRUMENT AND NOT
                                    ; IN THE SHIPPING BUILD, and this is the
                                    ; whole reason: [SP - n, SP) is free only
                                    ; down to the STACK'S OWN BASE, and this
                                    ; package cannot see where that is.
                                    ; STK0_SIZE is 512 (kernel/kernel.asm) and
                                    ; the kernel's own measured task-0 water is
                                    ; 238 of it (tests/stk0water.py), so an
                                    ; ordinary deep UI chain leaves ~266 bytes
                                    ; under SP - and the chain that reaches
                                    ; HERE is one of the deepest the package
                                    ; has. A 256-byte fill under it would write
                                    ; into .lowbss, where the mouse ISR's and
                                    ; the tick chain's own private stacks live
                                    ; (SPEC.md 9.10, 8.5), and the water
                                    ; reading cannot detect its own overrun: it
                                    ; would read the same number either way.
                                    ; So the fill is behind -dNI_STKPROBE,
                                    ; which only the NITEST build passes, and
                                    ; 128 is what covers the measured 98 with
                                    ; margin. A reading OF 128 means the probe
                                    ; was exhausted and the number is a FLOOR,
                                    ; which is what nisystest prints when it
                                    ; happens
NI_STK_PAT   equ 0x5A5A
%endif

; -----------------------------------------------------------------------------
; int ni_fsx_go(void *win) - take the machine (SPEC.md 53.1).
;
; in:  [bp+4] = our window.  out: AX = 1 entered, 0 refused
;
; FSXF_FASTTICK IS ARMED AND IT IS THE WHOLE PACING DESIGN (SPEC.md 91.6.3):
; with it the scheduler's sub-tick is FSX_SUBTICK = 3, so OSAPI_FSX_WAIT on
; FSXW_FRAME returns 54.6195 times a second instead of 18.2065, and the
; accumulator's 60.0988 / 54.6195 = 1.1002 is the 110/100 this port paces on.
; FSXW_VSYNC is never the budget: it is the CURRENT MODE's retrace - 70 Hz in
; mode 13h - and pacing on it runs the game 16.5% fast on a machine quick
; enough to notice.
;
; THE KERNEL REPAINTS THE WHOLE DESKTOP ON THE WAY OUT (SPEC.md 53.6) and that
; repaint is our own W_PAINT, so the panel is already back before this returns.
; -----------------------------------------------------------------------------
_ni_fsx_go:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    mov word [ni_entered], 0
    mov ax, ni_fsx_main
    mov bx, [bp+4]
    mov cx, FSXF_FASTTICK
    call OSAPI_FSX_RUN
    mov ax, 0
    jc .out                         ; the KERNEL refused the bracket
    mov ax, [ni_entered]            ; ...and this is the other refusal: a mode
                                    ;   the display does not offer, which
                                    ;   ni_fsx_main answers by returning
                                    ;   NORMALLY - so CF alone would report a
                                    ;   session that never drew as entered
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; ni_fsx_main - the exclusive main (SPEC.md 53.1)
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours, the gfx lock held for
;      the whole session and every other task frozen
; out: near ret; the kernel restores the desktop
; -----------------------------------------------------------------------------
ni_fsx_main:
    push si
    push di
    push ds
    pop es                          ; cc8086:allow OSAPI_FSX_MODE wants the
                                    ;   buffer at ES:DI and it is ours
    mov di, ni_fsi
    mov al, FSXM_VGA13
    call OSAPI_FSX_MODE
    jc .out                         ; refused: the desktop is untouched and
                                    ;   the bracket ends without drawing
    inc word [ni_entered]           ; the mode IS set: everything below draws
    mov ax, [ni_fsi + FSI_SEG]
    mov [_ni_fbseg], ax
    ; THE ORDER HERE IS THE WHOLE OF "the screen never leaves black", and it
    ; is a defect no emulator can show. OSAPI_FSX_MODE leaves 64,000 bytes of
    ; index 0 with the BIOS mode-13h DAC, where index 0 IS black; ni_dac then
    ; reprograms index 0 to ni_dactab's first entry, which is InfoNES's own
    ; NesPaletteRGB[0] = 112,112,112, and at that instant the whole 320x200
    ; turns LIGHT GREY - followed by _ni_fsx_border's 32,000-word fill wiping
    ; it back to black top to bottom, ~150 ms on a 4.77 MHz 8088 and
    ; sub-millisecond here. So the four mirrors of NES $0F are blacked FIRST
    ; (they are white in the BIOS palette), then the fill, then the full DAC:
    ; every pixel on the screen is $0F before any index changes colour.
    call ni_dacblack
    call _ni_fsx_border             ; ...and the fill is here, not in
                                    ;   ni_bracket, so it lives in the routine
                                    ;   that owns the mode set and runs once
    call ni_dac                     ; the 64 colours, FOUR TIMES (91.5.3)
%ifdef NI_STKPROBE
    call _ni_stk_fill
%endif
    call _ni_bracket                ; nirun.c: the pacer, the frame loop and
                                    ;   the keys, for as long as the user
                                    ;   stays
%ifdef NI_STKPROBE
    call _ni_stk_water
    mov [ni_stk_hi], ax
    mov [_ni_stk_lo], ax
%endif
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; ni_dacblack - the four mirrors of NES $0F (0x0F, 0x4F, 0x8F, 0xCF) set to
; 0,0,0 before anything is drawn with them.
;
; $0F is the border's colour and it is WHITE in the BIOS mode-13h palette, so
; the fill has to be invisible before it happens; ni_dac writes the same four
; entries to the same 0,0,0 a moment later (ni_dactab's $0F), which is why
; this is four writes and not a saved state.
; -----------------------------------------------------------------------------
ni_dacblack:
    push ax
    push cx
    push dx
    push bx
    xor bl, bl
.one:
    mov dx, 0x3C8
    mov al, bl
    add al, 0x0F                    ; 0x0F, 0x4F, 0x8F, 0xCF
    out dx, al
    inc dx                          ; 0x3C9
    xor al, al
    mov cx, 3                       ; R, G, B
.ch:
    out dx, al
    loop .ch
    add bl, 0x40
    jnz .one
    pop bx
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ni_dac - the 64 NES colours written FOUR TIMES into the DAC, at 0, 0x40,
; 0x80 and 0xC0 (SPEC.md 91.5.3).
;
; That is what makes the composer's tag bits free: a pixel byte carrying the
; backdrop tag (0x80) or a sprite tag indexes a CORRECT colour, so the present
; is one `rep movsw` and there is no masking pass anywhere. Inside a foreign
; mode the DAC is the app's (SPEC.md 53.7) and the exit mode set reprograms
; everything it touches, so this costs the system nothing.
;
; The RGB is InfoNES's own NesPaletteRGB (src/sdl/InfoNES_System_SDL.cpp:81-96)
; at 8 bits a channel, divided by four for the VGA's 6-bit DAC.
;
; The index register auto-increments, so one write of 0x3C8 and 192 of 0x3C9
; is a whole mirror.
; -----------------------------------------------------------------------------
ni_dac:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bl, bl
.mirror:
    mov dx, 0x3C8
    mov al, bl
    out dx, al
    inc dx                          ; 0x3C9, which auto-increments
    mov si, ni_dactab
    mov cx, 192
.byte:
    mov al, [cs:si]
    inc si
    out dx, al
    loop .byte
    add bl, 0x40                    ; 0, 0x40, 0x80, 0xC0 - and the fourth
    jnz .mirror                     ;   add wraps to 0, which is the end
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; int ni_getkey(void) - ONE typed key, or -1.
;
; The bracket dispatches no events (SPEC.md 53.1), so this is the whole of
; "what was TYPED" - the system keys - and os88_key_down is the whole of "what
; is HELD" - the D-pad (SPEC.md 91.6.4). `int 16h` is legal here because the
; bracket IS the UI task, and the caller DRAINS it rather than sampling it:
; one key a frame at half a frame a second is a machine that ignores you.
;
; out: AX = (scan << 8) | ascii, or -1 when the queue is empty
; -----------------------------------------------------------------------------
_ni_getkey:
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov ah, 1
    int 0x16
    jz .none
    xor ah, ah
    int 0x16
    jmp short .out
.none:
    mov ax, -1
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; void ni_fsx_wait(int clock) - OSAPI_FSX_WAIT (slot 0x02D8), for the C.
; -----------------------------------------------------------------------------
_ni_fsx_wait:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [bp+4]
    call OSAPI_FSX_WAIT
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; _ni_fsx_border - the 32 columns each side of the picture, once. NOT called
; from C any more (the prototype is gone with the call): ni_fsx_main calls it,
; and it keeps the C name because it is the same routine.
;
; The mode set clears the screen (os88api.inc), so this is not about the first
; frame: it is about the ROW DROP. 200 destination rows are written every
; frame and the border columns are never touched again, so nothing here has to
; be repeated - but a mode set that ever stopped clearing would leave the
; desktop's bytes in the border, and one fill is cheaper than finding that out
; on somebody's 5150.
;
; CALLED FROM ni_fsx_main AND NOT FROM C, between ni_dacblack and ni_dac: the
; fill's colour has to be black at the moment it is written, and only the
; routine that owns the mode set knows where in the sequence that is.
; -----------------------------------------------------------------------------
_ni_fsx_border:
    push di
    push cx
    push ax
    push es
    mov es, [_ni_fbseg]             ; cc8086:allow the framebuffer the bracket
                                    ;   owns, restored below
    xor di, di
    mov ax, 0x0F0F                  ; NES $0F, THE CANONICAL BLACK - and index
                                    ;   0 is not black at all: ni_dactab's
                                    ;   first entry is InfoNES's own
                                    ;   NesPaletteRGB[0], 112,112,112, so a
                                    ;   zero fill leaves 32 columns of LIGHT
                                    ;   GREY each side of the picture, which
                                    ;   reads as part of the emulated screen
                                    ;   rather than as the machine's border.
                                    ;   No InfoNES front end paints a grey
                                    ;   surround
    mov cx, 32000
    cld
    rep stosw                       ; cc8086:allow 64,000 bytes, the whole of
                                    ;   mode 13h
    pop es
    pop ax
    pop cx
    pop di
    ret

%ifdef NI_STKPROBE
; -----------------------------------------------------------------------------
; THE STACK SENTINEL (SPEC.md 91.4.4, the port plan's R1)
;
; void ni_stk_fill(void)  - lay NI_STK_PROBE bytes of pattern below SP
; int  ni_stk_water(void) - ...and answer how deep the deepest scrub went
;
; A budget nobody measured is an estimate wearing a table's clothes. The whole
; bracket runs on task 0's 512-byte stack, under the kernel's own frames, with
; interrupts landing on top of whichever frame is deepest - so what the table
; in SPEC.md 91.4.4 needs is not the sum of the frames it can see but the
; deepest address anything actually reached, ISRs included.
;
; BOTH ROUTINES PUSH EXACTLY FOUR WORDS BEFORE READING SP, so the two agree
; about where the probe starts without either of them being told.
; -----------------------------------------------------------------------------
_ni_stk_fill:
    push ax
    push cx
    push di
    push es
    mov ax, ss
    mov es, ax                      ; cc8086:allow ES is the STACK segment -
                                    ;   the only place this package ever puts
                                    ;   it there - and it is restored below
    mov di, sp
    sub di, NI_STK_PROBE            ; [SP - NI_STK_PROBE, SP) is free of
                                    ;   anything LIVE - but not free of the
                                    ;   stack's own BASE, which this package
                                    ;   cannot see, which is why the whole
                                    ;   routine is behind NI_STKPROBE and the
                                    ;   probe is 128 rather than 256
    mov cx, NI_STK_PROBE / 2
    mov ax, NI_STK_PAT
    cld
    rep stosw                       ; cc8086:allow
    pop es
    pop di
    pop cx
    pop ax
    ret

_ni_stk_water:
    push cx
    push si
    push ds
    push bx
    mov ax, ss
    mov ds, ax                      ; cc8086:allow the stack segment, restored
                                    ;   below
    mov si, sp
    sub si, NI_STK_PROBE
    mov bx, si
    mov cx, NI_STK_PROBE / 2
.scan:
    cmp word [si], NI_STK_PAT
    jne .found
    add si, 2
    loop .scan
    xor ax, ax                      ; nothing below SP was ever written: the
    jmp short .out                  ;   bracket refused before it ran
.found:
    mov ax, bx
    add ax, NI_STK_PROBE
    sub ax, si                      ; bytes from the deepest scrub up to SP
.out:
    pop bx
    pop ds
    pop si
    pop cx
    ret
%endif

; The DAC image: 64 colours of 6-bit R, G, B, InfoNES's NesPaletteRGB / 4.
ni_dactab:
    db 28, 28, 28                   ; $00
    db  8,  6, 34                   ; $01
    db  0,  0, 42                   ; $02
    db 16,  0, 38                   ; $03
    db 34,  0, 28                   ; $04
    db 42,  0,  4                   ; $05
    db 40,  0,  0                   ; $06
    db 30,  2,  0                   ; $07
    db 16, 10,  0                   ; $08
    db  0, 16,  0                   ; $09
    db  0, 20,  0                   ; $0A
    db  0, 14,  4                   ; $0B
    db  6, 14, 22                   ; $0C
    db  0,  0,  0                   ; $0D
    db  0,  0,  0                   ; $0E
    db  0,  0,  0                   ; $0F
    db 46, 46, 46                   ; $10
    db  0, 28, 58                   ; $11
    db  8, 14, 58                   ; $12
    db 32,  0, 60                   ; $13
    db 46,  0, 46                   ; $14
    db 56,  0, 22                   ; $15
    db 54, 10,  0                   ; $16
    db 50, 18,  2                   ; $17
    db 34, 28,  0                   ; $18
    db  0, 36,  0                   ; $19
    db  0, 42,  0                   ; $1A
    db  0, 36, 14                   ; $1B
    db  0, 32, 34                   ; $1C
    db  0,  0,  0                   ; $1D
    db  0,  0,  0                   ; $1E
    db  0,  0,  0                   ; $1F
    db 62, 62, 62                   ; $20
    db 14, 46, 62                   ; $21
    db 22, 36, 62                   ; $22
    db 16, 34, 62                   ; $23
    db 60, 30, 62                   ; $24
    db 62, 28, 44                   ; $25
    db 62, 28, 24                   ; $26
    db 62, 38, 14                   ; $27
    db 60, 46, 14                   ; $28
    db 32, 52,  4                   ; $29
    db 18, 54, 18                   ; $2A
    db 22, 62, 38                   ; $2B
    db  0, 58, 54                   ; $2C
    db  0,  0,  0                   ; $2D
    db  0,  0,  0                   ; $2E
    db  0,  0,  0                   ; $2F
    db 62, 62, 62                   ; $30
    db 42, 56, 62                   ; $31
    db 48, 52, 62                   ; $32
    db 52, 50, 62                   ; $33
    db 62, 48, 62                   ; $34
    db 62, 48, 54                   ; $35
    db 62, 46, 44                   ; $36
    db 62, 54, 42                   ; $37
    db 62, 56, 40                   ; $38
    db 56, 62, 40                   ; $39
    db 42, 60, 46                   ; $3A
    db 44, 62, 50                   ; $3B
    db 38, 62, 60                   ; $3C
    db  0,  0,  0                   ; $3D
    db  0,  0,  0                   ; $3E
    db  0,  0,  0                   ; $3F
