; =============================================================================
; os8088 - apps/os88spkfx.inc
;
; THE SPEAKER SHAPER (docs/plans/completed/SPEAKER-PCM-PLAN.md): 8-bit PCM made into the
; PC speaker's pulse counts the way a 5150's 2 1/4-inch cone can PLAY them. A
; straight wave through 34.11's pulse width is the carrier and nothing else
; on the real machine (SPEC.md 98.2.15.1) - the width is spent on bass the
; cone cannot move - and the Video Player's encoder shapes on the host. A
; package handed a WAV or a module shapes it HERE, for about what the plain
; table translation already cost. tools/os88spkfx.py is the reference, to
; the byte: its Shaper is this file, and the gates hold the two equal.
;
; PER SAMPLE:
;   1. PRE-EMPHASIS (SPKFX_PRE_DIFF): the first difference, i = (prev - x +
;      256) >> 1 - the high-pass and the tilt in one (and the wave upside
;      down, which no ear hears), `xchg`/`sub`/`rcr` with the previous
;      sample kept in AH.
;      SPKFX_PRE_NONE takes x as it is (Tracker filters its SAMPLES at load).
;   2. ONE `xlatb` through a row of the LEVEL FAMILY: gain 2^(l/3), the soft
;      clip, and os88spk.inc's count table, composed at init.
;   3. THE CARRIER PUT AWAY IN THE QUIET: d subtracted, saturating at 1; d
;      moves once a sub-block of SPKFX_SUB samples toward the span's target.
; PER SPAN (os88spkfx_level, before a sample of it is emitted): the peak, one
; sample in SPKFX_SPARSE; the level it asks for (one step up a span at most,
; any number down); the carrier's target.
;
;   os88spkfx_init   after os88spk_init. DI = 256 x SPKFX_NLEV bytes in the
;                    package's own segment for the family, AL = SPKFX_PRE_*,
;                    AH = 1 to put the carrier away in the quiet (0 not).
;                    BOTH ARE IGNORED for a Covox (os88spk_lpt): PRE_NONE
;                    and no carrier, whatever was asked (SPEC.md 34.14).
;                    Builds the family (~0.2 s on a 5150); os88spk_sil
;                    becomes 1 with AH = 1 (a dry ring is then silent AND
;                    quiet). Any time outside a play
;   os88spkfx_level  DS:SI = a span, CX its length (1..65535): decides it.
;                    out AL = its peak. Call it once a span, before emitting
;   os88spkfx_emit   DS:SI -> ES:DI, CX samples of the span as counts, in
;                    any number of pieces. SI and DI advance, CX = 0
;
; All preserve every register but their outputs. Labels os88spkfx_; the state
; is in the including package's image, through CS - the family too, which is
; why it must be in the package's own segment (`cs xlatb`: DS is the source
; and ES the ring). Needs apps/os88spk.inc included FIRST.
; =============================================================================

SPKFX_PRE_NONE  equ 0
SPKFX_PRE_DIFF  equ 1

%include "os88spkfx_t.inc"

; -----------------------------------------------------------------------------
os88spkfx_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    cmp word [cs:os88spk_lptp], 0   ; ON A COVOX IT IS A LEVELLER AND NOTHING
    je .spk                         ; ELSE (SPEC.md 34.14): no pre-emphasis -
    xor ax, ax                      ; the tilt is for a cone that cannot move
.spk:                               ; bass, and a DAC can - and no carrier to
                                    ; put away. The table it composes with is
                                    ; os88spk's identity, so a row is the gain
                                    ; and the clip on the sample itself
    mov [cs:os88spkfx_pre], al
    mov [cs:os88spkfx_idle], ah
    mov [cs:os88spkfx_fam], di
    xor ah, ah                      ; THE ROW IS STORED BY WHAT THE LOOP
    mov dx, os88spkfx_en            ; HOLDS: PRE_DIFF's rcr leaves the index
    mov cx, os88spkfx_enz           ; with its top bit flipped (no cmc), so
    cmp al, SPKFX_PRE_NONE          ; its rows are stored at i ^ 80h
    je .xm
    mov ah, 0x80
    mov dx, os88spkfx_ed
    mov cx, os88spkfx_edz
.xm:
    mov [cs:os88spkfx_xm], ah
    mov [cs:os88spkfx_et], dx
    mov [cs:os88spkfx_etz], cx
    push cs
    pop es                          ; the family: our own segment
    mov ds, [cs:os88spk_rseg]       ; the count table t[]: the ring's,
    mov si, [cs:os88spk_rl]         ; at RL + SND_EXT_TAB
    add si, SND_EXT_TAB
    cld
    xor bx, bx                      ; the level, x 2: the gain's index
.lev:
    xor cl, cl                      ; CL = where it is stored, 0..255
.ent:
    mov ch, cl                      ; CH = the index i it stands for
    xor ch, [cs:os88spkfx_xm]
    mov al, ch                      ; a = |i - 128|, 0..128 (0 is 128: the
    sub al, 0x80                    ; neg leaves 80h, read unsigned)
    jns .pos
    neg al
.pos:
    xor ah, ah
    mul word [cs:os88spkfx_gf+bx]   ; m = a x GF[l] >> 8, at most 128
    mov al, ah
    mov ah, dl
    or dh, dh
    jnz .big
    cmp ax, CURVEMAX
    jbe .m
.big:
    mov ax, CURVEMAX
.m:
    push bx
    mov bx, ax
    mov al, [cs:os88spkfx_curve+bx] ; y, 0..127
    test ch, 0x80
    jnz .hi
    neg al                          ; below the centre: 128 - y
.hi:
    add al, 0x80
    mov bl, al                      ; the count t[128 +- y]
    xor bh, bh
    mov al, [si+bx]
    pop bx
    stosb
    inc cl
    jnz .ent
    inc bx
    inc bx
    cmp bx, SPKFX_NLEV * 2
    jb .lev
    mov bl, [cs:os88spkfx_xm]       ; c0, the centre: row 0 at i = 128
    xor bl, 0x80
    xor bh, bh
    add bx, [cs:os88spkfx_fam]
    mov al, [cs:bx]
    mov [cs:os88spkfx_c0], al
    xor ah, ah
    cmp byte [cs:os88spkfx_idle], 0
    je .noidle
    dec al                          ; a play starts with the carrier away:
    mov ah, al                      ; d = c0 - 1, every count 1
    mov byte [cs:os88spk_sil], 1    ; ...and a dry ring the same
.noidle:
    mov [cs:os88spkfx_d], ah
    mov [cs:os88spkfx_h], ah
    xor ax, ax
    mov [cs:os88spkfx_lev], al
    mov [cs:os88spkfx_sub], al
    mov byte [cs:os88spkfx_gate], 1 ; a play starts as after silence
    mov [cs:os88spkfx_rat], al      ; ...and levelled, not ratcheted
    mov [cs:os88spkfx_pk1], ax      ; ...with no peaks held (pk1, pk2)
    mov byte [cs:os88spkfx_prev], 0x80
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

CURVEMAX equ 128

; -----------------------------------------------------------------------------
; os88spkfx_ratchet - AL = a level (0..SPKFX_NLEV-1): after os88spkfx_init,
; play at ONE level instead of levelling each span. The level only ever steps
; down, a step for a span that overdrives it by more than SPKFX_RTOL levels,
; so it settles on the piece's loud parts and then holds (SPEC.md 34.11.9.1).
; A player keeps the settled level across its own re-inits (os88spkfx_lev is
; readable) and hands it back here. Preserves all.
; -----------------------------------------------------------------------------
os88spkfx_ratchet:
    mov [cs:os88spkfx_lev], al
    mov byte [cs:os88spkfx_rat], 1
    ret

; -----------------------------------------------------------------------------
os88spkfx_level:
    push bx
    push cx
    push dx
    push si
    xor dh, dh                      ; DH = the peak, a = |i - 128|
    cmp byte [cs:os88spkfx_pre], SPKFX_PRE_NONE
    je .none
    mov dl, [cs:os88spkfx_prev]     ; the sample before the span's first
.d:
    mov al, dl                      ; prev - x, and the borrow rotated in:
    sub al, [si]                    ; i - 128 as a signed byte, exactly
    rcr al, 1                       ; (os88spkfx_emit's own index, 80h
    or al, al                       ; flipped)
    jns .dp
    neg al
.dp:
    cmp al, dh
    jbe .dn
    mov dh, al
.dn:
    add si, SPKFX_SPARSE
    sub cx, SPKFX_SPARSE
    jbe .done
    mov dl, [si-1]
    jmp short .d
.none:
    mov al, [si]
    xor al, 0x80
    jns .np
    neg al
.np:
    cmp al, dh
    jbe .nn
    mov dh, al
.nn:
    add si, SPKFX_SPARSE
    sub cx, SPKFX_SPARSE
    ja .none
.done:
    cmp byte [cs:os88spkfx_rat], 0  ; THE RATCHET (os88spkfx_ratchet): one
    je .adapt                       ; level for the piece. It only ever steps
    mov al, [cs:os88spkfx_lev]      ; DOWN, one a span, and only for a span
    cmp byte [cs:os88spkfx_rat], 2  ; 2: FROZEN - the user's hand is on the
    je .set                         ; level (Tracker's volume bar, 45.25.3)
    cmp dh, SPKFX_PGATE             ; that overdrives it by more than
    jb .set                         ; SPKFX_RTOL levels - so it settles on
    mov bl, dh                      ; the piece's loud parts in its first
    xor bh, bh                      ; seconds and then does not move at all
    mov ah, [cs:os88spkfx_lt+bx]    ; (34.11.9.1)
    add ah, SPKFX_RTOL
    cmp ah, al
    jae .set
    dec ax
    jmp short .set
.adapt:
    mov al, dh                      ; the level the peak asks for - HELD: the
    mov bl, al                      ; largest of this span's and the last two
    cmp bl, [cs:os88spkfx_pk1]      ; spans' peaks, so a loud hit keeps the
    jae .k1                         ; level down for ~64 ms and the leveller
    mov bl, [cs:os88spkfx_pk1]      ; does not pump between beats (34.11.9)
.k1:
    cmp bl, [cs:os88spkfx_pk2]
    jae .k2
    mov bl, [cs:os88spkfx_pk2]
.k2:
    mov ah, [cs:os88spkfx_pk1]
    mov [cs:os88spkfx_pk2], ah
    mov [cs:os88spkfx_pk1], al
    cmp dh, SPKFX_PGATE             ; ...but silence asks by its own peak
    jae .k3
    mov bl, dh
.k3:
    xor bh, bh
    mov al, [cs:os88spkfx_lt+bx]
    mov ah, [cs:os88spkfx_lev]
    cmp al, ah                      ; up one step a span at most, down at once
    jbe .set
    cmp byte [cs:os88spkfx_gate], 0 ; ...but after SILENCE its own level at
    jne .set                        ; once: there is no gain to step from,
    mov al, ah                      ; and a re-entry held 20 dB down for
    inc ax                          ; ~7 spans is the dip the field heard
.set:
    mov [cs:os88spkfx_lev], al
    cmp dh, SPKFX_PGATE             ; this span silent: 0FFh, else 0
    sbb ah, ah
    mov [cs:os88spkfx_gate], ah
    mov byte [cs:os88spkfx_sub], 0
    xor cl, cl                      ; the carrier's target, h
    cmp byte [cs:os88spkfx_idle], 0
    je .h
    mov cl, [cs:os88spkfx_c0]
    dec cl                          ; silence: all the way, every count 1
    cmp dh, SPKFX_PGATE
    jb .h
    mov ah, al                      ; the row
    xor al, al
    add ax, [cs:os88spkfx_fam]
    mov si, ax
    mov bl, dh                      ; q = row[min(255, 128 + p)] - row[128],
    add bl, 0x80                    ; each index as the row stores it
    jnc .in
    mov bl, 0xFF
.in:
    mov ch, [cs:os88spkfx_xm]
    xor bl, ch
    xor bh, bh
    mov al, [cs:si+bx]
    mov bl, ch
    xor bl, 0x80
    sub al, [cs:si+bx]
    mov ah, al                      ; q += q / 4 + 2
    shr ah, 1
    shr ah, 1
    add al, ah
    add al, 2
    sub cl, al                      ; h = c0 - 1 - q, at least 0
    jae .h
    xor cl, cl
.h:
    mov [cs:os88spkfx_h], cl
    mov al, dh
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88spkfx_emit. In the loop: BX the span's row, AH the previous sample, DH
; the carrier's shift d and DL its target h, CX the samples left; SUB samples
; at a time through the unrolled copies, d moved between them
; -----------------------------------------------------------------------------
os88spkfx_emit:
%ifdef SPKFX_PAD                    ; A MEASURING KNOB, never shipped: a
    push cx                         ; known extra cost a sample (a `loop` of
    jcxz .padn                      ; SPKFX_PAD `nop`s), for how much a
.pad:                               ; heavier shaper this machine has room
    times SPKFX_PAD nop             ; for (SPEC.md 34.11.9)
    loop .pad
.padn:
    pop cx
%endif
    push ax
    push bx
    push dx
    push bp
    mov bh, [cs:os88spkfx_lev]      ; BX -> the span's row
    xor bl, bl
    add bx, [cs:os88spkfx_fam]
    mov ah, [cs:os88spkfx_prev]
    mov dh, [cs:os88spkfx_d]
    mov dl, [cs:os88spkfx_h]
    cld
    jcxz .end
    cmp byte [cs:os88spkfx_sub], 0  ; A SUB-BLOCK PART DONE (a piece ended
    je .full                        ; inside it): finished with the same d
    push ax
    mov al, SPKFX_SUB
    sub al, [cs:os88spkfx_sub]
    xor ah, ah                      ; k = min(SUB - sub, CX)
    cmp ax, cx
    jbe .k
    mov ax, cx
.k:
    mov bp, ax
    add [cs:os88spkfx_sub], al
    and byte [cs:os88spkfx_sub], SPKFX_SUB - 1
    pop ax
    sub cx, bp
    jmp short .go_k
.full:
    jcxz .end
    cmp dh, dl                      ; A SUB-BLOCK: d toward h, up by
    je .go                          ; SPKFX_DUP, down by SPKFX_DDOWN
    jb .up
    sub dh, SPKFX_DDOWN
    jb .toh                         ; (past it, or below 0: h)
    cmp dh, dl
    jae .go
.toh:
    mov dh, dl
    jmp short .go
.up:
    add dh, SPKFX_DUP
    cmp dh, dl
    ja .toh
.go:
    cmp cx, SPKFX_SUB
    jb .tail
    sub cx, SPKFX_SUB
    mov bp, (SPKFX_SUB - 1) * 2     ; a whole one: the first copy
    jmp short .pick
.tail:
    mov bp, cx                      ; the last piece of the span: it ends
    push ax                         ; with the sub-block part done
    mov al, cl
    mov [cs:os88spkfx_sub], al
    pop ax
    xor cx, cx
.go_k:
    dec bp                          ; the entry for k left: copy SUB - k
    shl bp, 1
.pick:
    or dh, dh                       ; d = 0 (the loud part of anything):
    jz .z                           ; the copies with no shift to take
    add bp, [cs:os88spkfx_et]
    jmp [cs:bp+0]
.z:
    add bp, [cs:os88spkfx_etz]
    jmp [cs:bp+0]
.end:
    mov [cs:os88spkfx_prev], ah
    mov [cs:os88spkfx_d], dh
    pop bp
    pop dx
    pop bx
    pop ax
    ret

; THE UNROLLED SUB-BLOCKS. A copy is one sample; the saturation's fix is out
; of line, so the common path is one untaken `jbe`, and copies go in groups
; of four with their four fixes after them, inside a short jump's reach.
%macro SPKFX_ZERO 1                 ; d = 0: nothing to subtract, and no
os88spkfx_%1z %+ c:                 ; count below 1 to fix
    lodsb
%ifidn %1, d
    xchg al, ah
    sub al, ah
    rcr al, 1
%endif
    cs xlatb
    stosb
%endmacro
%macro SPKFX_ONE 1                  ; %1 = d (diff) or n; the copy is c
os88spkfx_%1 %+ c:
    lodsb                           ; x
%ifidn %1, d
    xchg al, ah                     ; AH = x, the next one's prev, and
    sub al, ah                      ; prev - x rotated with its borrow: the
    rcr al, 1                       ; index with its top bit flipped, which
%endif                              ; is how PRE_DIFF's rows are stored
    cs xlatb                        ; the level's count
    sub al, dh                      ; the carrier's shift, saturating at 1
    jbe os88spkfx_%1f %+ c
os88spkfx_%1r %+ c:
    stosb
%endmacro
%macro SPKFX_FIX 1
os88spkfx_%1f %+ c:
    mov al, 1
    jmp short os88spkfx_%1r %+ c
%endmacro
%macro SPKFX_BODY 1
%assign g 0
%rep SPKFX_SUB / 4
%assign c g
%rep 4
    SPKFX_ONE %1
%assign c c+1
%endrep
    jmp short os88spkfx_%1g %+ g
%assign c g
%rep 4
    SPKFX_FIX %1
%assign c c+1
%endrep
os88spkfx_%1g %+ g:
%assign g g+4
%endrep
    jmp os88spkfx_emit.full
%endmacro

    SPKFX_BODY d
    SPKFX_BODY n
%macro SPKFX_BODYZ 1
%assign c 0
%rep SPKFX_SUB
    SPKFX_ZERO %1
%assign c c+1
%endrep
    jmp os88spkfx_emit.full
%endmacro
    SPKFX_BODYZ d
    SPKFX_BODYZ n

os88spkfx_ed:                       ; the entry for k samples left, k 1..SUB
%assign c SPKFX_SUB - 1
%rep SPKFX_SUB
    dw os88spkfx_d %+ c
%assign c c-1
%endrep
os88spkfx_en:
%assign c SPKFX_SUB - 1
%rep SPKFX_SUB
    dw os88spkfx_n %+ c
%assign c c-1
%endrep
os88spkfx_edz:
%assign c SPKFX_SUB - 1
%rep SPKFX_SUB
    dw os88spkfx_dz %+ c
%assign c c-1
%endrep
os88spkfx_enz:
%assign c SPKFX_SUB - 1
%rep SPKFX_SUB
    dw os88spkfx_nz %+ c
%assign c c-1
%endrep

os88spkfx_fam:  dw 0                ; the family, in our segment
os88spkfx_pre:  db 0                ; SPKFX_PRE_*
os88spkfx_idle: db 0                ; the carrier put away in the quiet
os88spkfx_c0:   db 0                ; the centre count
os88spkfx_lev:  db 0                ; the span's level
os88spkfx_gate: db 0                ; the last span was silent (under PGATE)
os88spkfx_rat:  db 0                ; 1: the ratchet (os88spkfx_ratchet)
os88spkfx_pk1:  db 0                ; the last span's peak...
os88spkfx_pk2:  db 0                ; ...and the one before it (HOLD 2)
os88spkfx_d:    db 0                ; the carrier's shift now...
os88spkfx_h:    db 0                ; ...and its target
os88spkfx_sub:  db 0                ; samples into the sub-block
os88spkfx_prev: db 0x80             ; the last sample emitted
os88spkfx_xm:   db 0                ; 80h: rows stored at i ^ 80h (PRE_DIFF)
os88spkfx_et:   dw 0                ; the mode's entry table...
os88spkfx_etz:  dw 0                ; ...and its table for d = 0
