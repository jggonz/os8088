; =============================================================================
; os8088 - tests/sndcap/sndcap.asm
;
; SNDCAP: the gate package for SPEC.md 34.6.4's EXTERNAL RECORD RING - verb 4
; with SND_OPENF_EXT, the card capturing by auto-init DMA into a ring in the
; package's own claim and publishing its count there. It is NEVER shipped:
; `make sndcaptest` builds it onto a scratch disk of its own, and
; tests/sndcap.py drives it. It needs no input: the entry arms the window
; timer and every step after that is a timer callback (UI-task context, which
; the open verbs require - SPEC.md 20.3).
;
; What it does, in order, writing every answer into the RESULTS block below
; (found by the host through its magic, at a fixed offset in the image):
;
;   caps   OSAPI_SND_CAPS
;   R1     verb 4 EXT on a 4KB ring at 0FF0:0000 - physical FF00h..10EFFh,
;          across a 64KB page                                    -> 7
;          (never written: the refusal comes before the card is touched)
;   X      a 17KB OSAPI_MEM_CLAIM_DMA_HI claim - a 16KB ring and its control
;          words - the ring filled with 00h and SND_EXT_CONS seeded BEEFh;
;          verb 4 EXT at 11,025 Hz, size code 2. CONS read straight after
;          the open (0: the open zeroes it), then 36 ticks (~1.98 s) of
;          nothing, then CONS (the count), verb 3, verb 2, CONS again, and
;          the ring scanned: how many bytes are 80h - what MartyPC's card
;          records - and how many are still 00h
;   Q      the ring refilled with 00h and CONS read, then 10 ticks later
;          CONS and the ring again: unchanged, or the close left the DMA or
;          the IRQ running
;   L      the OLD record stream, which SPEC.md 34.6.4 retired: a 6,000
;          byte grant and verb 4 at 8,000 Hz into it, which must answer 7
;          and open nothing (the run below is what it did when it opened)
;   S      the host makes the driver a SINGLE-CYCLE DSP ([sbl_auto] = 0) and
;          sets the go byte; verb 4 EXT on the ring must then answer 7. Not
;          run (EEEEh) if no go arrives within 15 s
;
; Window procs preserve every register (SPEC.md 20.5). The kernel reaches a
; callback through the package header's dispatcher, so each is a near proc.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SNDCAP', sc_entry

SC_RL      equ 16384                ; the ring, size code 2
SC_CODE    equ 2
SC_RATE    equ 11025
SC_LRATE   equ 8000                 ; the legacy leg's, as sbtest's 'r'
SC_LCAP    equ 6000
SC_RUN     equ 36                   ; ticks the external capture runs
SC_IDLE    equ 10                   ; ticks of quiet after its close
SC_LRUN    equ 20                   ; ticks the legacy capture runs (0.75 s
                                    ; fills it)
SC_GOWAIT  equ 273                  ; ticks to wait for the host's go (15 s)
%if (4096 << SC_CODE) != SC_RL
  %error "size code 2 is a 16KB ring"
%endif
SC_XFLAGS  equ SND_OPENF_EXT | (SC_CODE << SND_OPENF_RLSH)

; the timer's states
ST_START   equ 0
ST_RUN     equ 1
ST_IDLE    equ 2
ST_LEG     equ 3
ST_LRUN    equ 4
ST_WAIT    equ 5
ST_DONE    equ 6

; -----------------------------------------------------------------------------
; sc_entry - package entry (SPEC.md 20.2): the window, its timer, armed
; -----------------------------------------------------------------------------
sc_entry:
    push si
    push ax
    mov si, sc_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out
    mov ax, sc_tick
    call OSAPI_WM_ONTIMER
    mov ax, 9                       ; half a second for the desktop to settle
    call OSAPI_WM_TIMER
    clc
.out:
    pop ax
    pop si
    ret

; -----------------------------------------------------------------------------
; sc_paint - W_PAINT: one line saying which step the run is on
; -----------------------------------------------------------------------------
sc_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 8
    mov al, [sc_state]
    add al, '0'
    mov [sc_line+6], al
    mov si, sc_line
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sc_nop:
    ret

; -----------------------------------------------------------------------------
; sc_tick - the window timer: one step of the run, then re-arm
; in:  SI = the window; UI task, gfx lock held (OSAPI_WM_ONTIMER)
; -----------------------------------------------------------------------------
sc_tick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push si                         ; the window, for the re-arm
    mov al, [sc_state]
    cmp al, ST_START
    jne .n0
    call sc_start
    jmp .again
.n0:
    cmp al, ST_RUN
    jne .n1
    call sc_run
    jmp .again
.n1:
    cmp al, ST_IDLE
    jne .n2
    call sc_idle
    jmp .again
.n2:
    cmp al, ST_LEG
    jne .n3
    call sc_leg
    jmp .again
.n3:
    cmp al, ST_LRUN
    jne .n4
    call sc_lrun
    jmp .again
.n4:
    cmp al, ST_WAIT
    jne .again
    call sc_wait1x
.again:
    pop bx
    cmp byte [sc_state], ST_DONE
    je .done
    mov ax, 1
    call OSAPI_WM_TIMER
    jmp short .out
.done:
    mov dx, [sc_xseg]               ; the ring's claim back, at the very end
    or dx, dx
    jz .nofree
    call OSAPI_MEM_FREE
    mov word [sc_xseg], 0
.nofree:
    mov byte [sc_done], 1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_start - caps, R1, then the external ring claimed, filled and opened
; -----------------------------------------------------------------------------
sc_start:
    call OSAPI_SND_CAPS
    mov [sc_caps], ax
    mov ax, (SND_OPENF_EXT << 8) | 4    ; R1: size code 0, a 4KB ring
    mov di, 0x0FF0                  ; at physical FF00h: across 10000h
    xor si, si
    xor cx, cx
    mov dx, SC_RATE
    call OSAPI_SND_STREAM
    mov [sc_r1], ax
    call sc_close_if_open           ; (only if it wrongly opened)
    mov ax, (SC_RL + 1023) / 1024 + 1   ; the ring and its control words,
    mov cx, ax                      ; page-safe whole
    call OSAPI_MEM_CLAIM_DMA_HI     ; DX = its segment
    jnc .claimed
    mov word [sc_xopen], 0xEE00     ; no claim: skip to the legacy leg
    mov byte [sc_state], ST_LEG
    ret
.claimed:
    mov [sc_xseg], dx
    xor al, al
    call sc_fill
    push es
    mov es, dx
    mov word [es:SC_RL + SND_EXT_CONS], 0xBEEF
    pop es
    mov ax, (SC_XFLAGS << 8) | 4    ; verb 4, an external record ring
    mov di, dx                      ; DI:SI = the ring
    xor si, si
    xor cx, cx                      ; not read: and a driver without the
    mov dx, SC_RATE                 ; extension refuses CX = 0 with 7
    call OSAPI_SND_STREAM
    mov [sc_xopen], ax
    call sc_cons
    mov [sc_cons0], ax
    mov ax, [sc_xopen]
    or al, al
    jnz .refused
    mov [sc_hand], ah
    call OSAPI_GET_TICKS
    mov [sc_t0], ax
    mov byte [sc_wait], SC_RUN
    mov byte [sc_state], ST_RUN
    ret
.refused:
    mov byte [sc_state], ST_LEG
    ret

; -----------------------------------------------------------------------------
; sc_run - let it capture; at the end the readings, the close and the scan
; -----------------------------------------------------------------------------
sc_run:
    dec byte [sc_wait]
    jnz .out
    call sc_cons
    mov [sc_count], ax
    mov ah, [sc_hand]
    mov al, 3                       ; verb 3: state, DX = the same count
    call OSAPI_SND_STREAM
    mov [sc_st], ax
    mov [sc_stdx], dx
    call OSAPI_GET_TICKS
    mov [sc_t1], ax
    mov ah, [sc_hand]
    mov al, 2                       ; verb 2: the DMA and the DSP stopped
    call OSAPI_SND_STREAM
    mov [sc_xclose], ax
    call sc_cons
    mov [sc_cons1], ax
    call sc_scan                    ; what the card wrote
    mov [sc_n80], ax
    mov [sc_n00], dx
    push es
    mov es, [sc_xseg]
    mov ax, [es:0]
    mov [sc_w0], ax
    mov ax, [es:SC_RL - 2]
    mov [sc_wn], ax
    pop es
    xor al, al                      ; then the quiet: zero it again and see
    call sc_fill                    ; that nothing writes it
    mov byte [sc_wait], SC_IDLE
    mov byte [sc_state], ST_IDLE
.out:
    ret

; sc_idle - the quiet after the close: CONS and the ring must be as left
sc_idle:
    dec byte [sc_wait]
    jnz .out
    call sc_cons
    mov [sc_cons2], ax
    call sc_scan
    mov [sc_q00], dx
    mov byte [sc_state], ST_LEG
.out:
    ret

; -----------------------------------------------------------------------------
; sc_leg - verb 4 into a grant, the retired record stream: 7 (SPEC.md 34.6.4)
; -----------------------------------------------------------------------------
sc_leg:
    mov cx, SC_LCAP
    mov ax, 0x0007                  ; verb 7 grant
    call OSAPI_SND_STREAM
    or ax, ax
    jz .granted
    mov [sc_lopen], ax
    or byte [sc_lopen+1], 0xEE
    jmp short .skip
.granted:
    mov [sc_lg], si
    mov ax, 0x0004                  ; verb 4, AH = 0: into the grant
    mov cx, SC_LCAP
    mov dx, SC_LRATE
    call OSAPI_SND_STREAM
    mov [sc_lopen], ax
    or al, al
    jnz .free
    mov [sc_hand], ah
    mov byte [sc_wait], SC_LRUN
    mov byte [sc_state], ST_LRUN
    ret
.free:
    mov si, [sc_lg]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
.skip:
    mov word [sc_gowait], SC_GOWAIT
    mov byte [sc_state], ST_WAIT
    ret

sc_lrun:
    dec byte [sc_wait]
    jnz .out
    mov ah, [sc_hand]
    mov al, 3
    call OSAPI_SND_STREAM
    mov [sc_lst], ax
    mov [sc_ldx], dx
    mov si, [sc_lg]                 ; verb 5: the first two captured bytes
    mov di, sc_lw
    mov cx, 2
    mov ax, 0x0005
    call OSAPI_SND_STREAM
    mov [sc_lrd], ax
    mov ah, [sc_hand]
    mov al, 2
    call OSAPI_SND_STREAM
    mov [sc_lclose], ax
    mov si, [sc_lg]
    mov ax, 0x0107
    call OSAPI_SND_STREAM
    mov word [sc_gowait], SC_GOWAIT
    mov byte [sc_state], ST_WAIT
.out:
    ret

; -----------------------------------------------------------------------------
; sc_wait1x - wait for the host's go (it has made the DSP single-cycle), then
;             verb 4 EXT on the real ring: 7
; -----------------------------------------------------------------------------
sc_wait1x:
    cmp byte [sc_go], 0
    jne .go
    dec word [sc_gowait]
    jnz .out
    mov byte [sc_state], ST_DONE    ; no go: S stays EEEEh
    ret
.go:
    mov ax, 0xEE01                  ; no ring to try it on
    mov dx, [sc_xseg]
    or dx, dx
    jz .rec
    mov ax, (SC_XFLAGS << 8) | 4
    mov di, dx
    xor si, si
    xor cx, cx
    mov dx, SC_RATE
    call OSAPI_SND_STREAM
    push ax
    call sc_close_if_open
    pop ax
.rec:
    mov [sc_r1x], ax
    mov byte [sc_state], ST_DONE
.out:
    ret

; sc_close_if_open - a refusal test that OPENED (AL = 0): close it, so the
; next step is not refused busy. In: AX = the open's answer. Clobbers AX.
sc_close_if_open:
    or al, al
    jnz .out
    mov al, 2                       ; AH = the handle the open answered
    call OSAPI_SND_STREAM
.out:
    ret

; sc_cons - AX = the ring's SND_EXT_CONS word (0 with no claim)
sc_cons:
    push es
    xor ax, ax
    cmp [sc_xseg], ax
    je .out
    mov es, [sc_xseg]
    mov ax, [es:SC_RL + SND_EXT_CONS]
.out:
    pop es
    ret

; sc_fill - the ring, every byte AL
sc_fill:
    push cx
    push di
    push es
    mov es, [sc_xseg]
    xor di, di
    mov cx, SC_RL
    cld
    rep stosb
    pop es
    pop di
    pop cx
    ret

; sc_scan - AX = the ring's bytes that are 80h, DX = those that are 00h
sc_scan:
    push bx
    push cx
    push si
    push ds
    mov ds, [sc_xseg]
    xor si, si
    xor ax, ax
    xor dx, dx
    mov cx, SC_RL
.b:
    mov bl, [si]
    inc si
    cmp bl, 0x80
    jne .n80
    inc ax
.n80:
    or bl, bl
    jnz .n00
    inc dx
.n00:
    loop .b
    pop ds
    pop si
    pop cx
    pop bx
    ret

; --- the window ------------------------------------------------------------------
sc_tpl:
    dw 340, 88, 190, 60
    dw sc_ttl, sc_paint, sc_nop, sc_nop
sc_ttl:     db 'Snd Capture', 0
sc_line:    db 'step: -', 0

; --- THE RESULTS, read by tests/sndcap.py through the magic ---------------------
sc_res:     db 'SCAPRES1'           ; +0
sc_caps:    dw 0                    ; +8   OSAPI_SND_CAPS
sc_done:    db 0                    ; +10  1 = the run finished
sc_state:   db 0                    ; +11  the timer's state
sc_r1:      dw 0                    ; +12  R1: the page-crossing ring (7)
sc_xopen:   dw 0                    ; +14  X: verb 4 EXT's answer
sc_cons0:   dw 0                    ; +16  CONS straight after the open (0)
sc_count:   dw 0                    ; +18  CONS after the run
sc_st:      dw 0                    ; +20  verb 3's state...
sc_stdx:    dw 0                    ; +22  ...and its count
sc_t0:      dw 0                    ; +24  ticks at the open
sc_t1:      dw 0                    ; +26  ...and at the reading
sc_xclose:  dw 0                    ; +28  verb 2's answer
sc_cons1:   dw 0                    ; +30  CONS straight after the close
sc_n80:     dw 0                    ; +32  ring bytes that are 80h
sc_n00:     dw 0                    ; +34  ...and still 00h
sc_w0:      dw 0                    ; +36  the ring's first word
sc_wn:      dw 0                    ; +38  ...and its last
sc_cons2:   dw 0                    ; +40  Q: CONS 10 ticks after the close
sc_q00:     dw 0                    ; +42  Q: ring bytes still 00h then
sc_lopen:   dw 0                    ; +44  L: verb 4's answer
sc_lst:     dw 0                    ; +46  L: verb 3's state (1)...
sc_ldx:     dw 0                    ; +48  ...and count (6,000)
sc_lrd:     dw 0                    ; +50  L: verb 5's answer
sc_lw:      dw 0                    ; +52  L: the first two captured bytes
sc_lclose:  dw 0                    ; +54  L: verb 2's answer
sc_r1x:     dw 0xEEEE               ; +56  S: verb 4 EXT, single-cycle (7)
sc_go:      db 0                    ; +58  the host's go for S
            db 0                    ; +59

; --- bss --------------------------------------------------------------------------
sc_wait   equ os88_image_end + 0    ; byte: ticks left in a step
sc_hand   equ os88_image_end + 1    ; byte: the stream handle
sc_xseg   equ os88_image_end + 2    ; word: the ring's claim
sc_lg     equ os88_image_end + 4    ; word: the legacy leg's grant
sc_gowait equ os88_image_end + 6    ; word: ticks left to wait for the go

    OS88_BSS 8
    OS88_IMAGE_END
