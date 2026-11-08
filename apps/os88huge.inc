; =============================================================================
; os8088 - apps/os88huge.inc - WALKING A HUGE CLAIM BY ITS STEP (SPEC.md 50.3.8)
;
; OSAPI_MEM_HUGE (SPEC.md 50.3.5) answers a claim of any size as a FIRST
; WINDOW (DX) and a STEP (CX): byte X of it is at offset X & FFFFh of window
; DX + CX x (X >> 16), on every kernel - CX = 1000h for a conventional claim,
; 8 for a run of selectors on kern_mega in protected mode, whose claim may be
; a BRIDGE of two runs of memory with a cut between them (KERN-MEGA-PLAN
; 14.169). The cut is at a 64KB boundary of the claim, and no window reaches
; past one, so a walk that splits every span where a window ends splits it at
; the cut too, and needs no test of where it runs. This is that walk, written
; once (docs/plans/MEGA-HUGE-PLAN.md F5): the claim described by a RECORD in
; your DS, and every routine takes BX -> it.
;
;   HW_SEG   the first window (the cell's DX)
;   HW_STEP  the step to the window 64KB on - the cell's CX, doubled for a
;            claim given a window every 32KB (BX = 8000h asked, 0 answered)
;            and quadrupled for one every 16KB (4000h), whose 64KB-apart
;            windows are every second / fourth one
;   HW_LEN   the bytes the walk may reach (dword): nothing at or past it is
;            touched, so set it to what you hold if that is less than the
;            claim (hw_claim writes the claim's KB x 1024)
;   HW_CUT   the bridge's pool windows (the cell's out BX for a 64KB step
;            asked with BX = 0), 0 = one run. The walk never reads it: it is
;            for a caller that wants to know whether conventional memory
;            holds part of the claim
;   HW_FOFF  hw_fread's FILE offset of the claim's byte 0 (dword, added
;            modulo 2^32, so a negative one puts a file's start part-way
;            into the claim): 44 holds a WAV's data from the claim's start.
;            A cluster multiple, as READ_AT's offset is. hw_claim writes 0
;
; Take what you call, each a %define before the %include (only hw_aim and
; hw_span are always there):
;
;   HW_CLAIM  hw_claim  AX = KB, CX = the cell's BX -> the record filled,
;                       DX = the claim (OSAPI_MEM_FREE's, OSAPI_MEM_MOVABLE's)
;             hw_aim    DX:AX = an offset, CX = bytes wanted -> ES:DI there,
;                       CX = how many of them lie in that window
;   HW_GET    hw_get    DX:AX, CX bytes out of the claim to ES:DI (any segment)
;   HW_PUT    hw_put    ...and in from ES:DI
;   HW_RSEQ   hw_fread  a file read into the claim (file offset = claim
;                       offset + HW_FOFF), one os88_rseq a window
;                       (apps/os88rseq.inc)
;   HW_WSEQ   hw_fwrite DX:AX, CX bytes of the claim onto an os88_wseq stream,
;                       one os88_wseq_put per window (apps/os88wseq.inc)
;
; Every routine is near, 8086, keeps every register but its documented
; outputs, and copies with movsw (a byte for an odd count) - cld is its own.
; None calls the kernel but hw_claim (OSAPI_MEM_HUGE) and the two file
; routines (through the include they name), so any task may walk; the file
; routines are the UI task's (SPEC.md 20.6 rule 7). A window is a segment
; register loaded for the walk: never keep ES across a call of yours that
; could move the claim - aim again (SPEC.md 50.3.5: the run follows a move,
; a register loaded before it does not).
;
; tests/os88huge.py runs every routine under Unicorn against a Python model
; (windows 96KB apart, so a reach past a window's end reads a guard, never
; the claim's next byte); tests/megahw (MEGAHW.O88) runs them in a guest,
; across a real bridge's cut on kern_mega and on kern_big's paragraph step.
; =============================================================================
%ifndef OS88HUGE_INC
%define OS88HUGE_INC

HW_SEG      equ 0                   ; word: window 0
HW_STEP     equ 2                   ; word: the step to the window 64KB on
HW_LEN      equ 4                   ; dword: the bytes the walk may reach
HW_CUT      equ 8                   ; word: the bridge's pool windows, 0 one run
HW_FOFF     equ 10                  ; dword: hw_fread's file offset of byte 0
HW_SIZE     equ 14

%ifdef HW_CLAIM
; -----------------------------------------------------------------------------
; hw_claim - OSAPI_MEM_HUGE, and the record made from its answer
; in:  AX = KB (1..65535), CX = the cell's BX (0: a window every 64KB, a
;      bridge allowed; HUGE_NOBRIDGE; 8000h, 4000h), BX -> the record
; out: CF = 0 claimed: the record filled, DX = the claim's first window (free
;      it with OSAPI_MEM_FREE, declare it with OSAPI_MEM_MOVABLE)
;      CF = 1 refused: HW_SEG = 0, DX undefined
; keeps everything but DX
; -----------------------------------------------------------------------------
hw_claim:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, bx                      ; SI -> the record
    mov di, cx                      ; DI = the shape asked
    mov bx, cx
    push ax
    call OSAPI_MEM_HUGE             ; DX = window 0, CX = the step, BX = the
    pop ax                          ; cut (BX = 0 asked) or the shape back
    jc .no
    mov [si+HW_SEG], dx
    test di, 0xC000                 ; a window every 32KB / 16KB asked, and
    jz .one                         ; GIVEN (BX = 0 back): the window 64KB on
    or bx, bx                       ; is the second / the fourth
    jnz .one
    shl cx, 1
    test di, 0x4000
    jz .one
    shl cx, 1
.one:
    or di, di                       ; only a 64KB step asked with BX = 0 can
    jz .cut                         ; be a bridge: its BX is the cut
    xor bx, bx
.cut:
    mov [si+HW_CUT], bx
    mov [si+HW_STEP], cx
    mov bx, ax                      ; the length: KB x 1024
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr bx, cl
    mov [si+HW_LEN], ax
    mov [si+HW_LEN+2], bx
    xor ax, ax
    mov [si+HW_FOFF], ax
    mov [si+HW_FOFF+2], ax
    clc
    jmp short .o
.no:
    mov word [si+HW_SEG], 0
    stc
.o:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; hw_aim - a 32-bit offset as a pointer, and the reach the kernel guarantees
; in:  BX -> the record, DX:AX = an offset into the claim, CX = the most bytes
;      wanted from it (1..0FFFFh; 0FFFFh asks how far the window reaches, a
;      whole 64KB answering 0FFFFh)
; out: CF = 0: ES:DI = that byte, CX = how many of the wanted lie in its
;      window from there (1..wanted) - never past the window's 64KB, so never
;      across a bridge's cut, and never at or past HW_LEN
;      CF = 1: DX:AX at or past HW_LEN, or CX = 0 - ES, DI and CX kept
; keeps AX, BX, DX, SI, BP, DS
; -----------------------------------------------------------------------------
hw_aim:
    cmp dx, [bx+HW_LEN+2]
    jb .in
    ja .no
    cmp ax, [bx+HW_LEN]
    jae .no
.in:
    jcxz .no
    push ax
    push dx
    push si
    mov si, [bx+HW_LEN]             ; DI:SI = the bytes left in the claim
    sub si, ax
    mov di, [bx+HW_LEN+2]
    sbb di, dx
    jnz .c1                         ; 64KB or more: no cap from the length
    cmp cx, si
    jbe .c1
    mov cx, si
.c1:
    mov di, ax                      ; DI = 10000h - the offset in the window:
    neg di                          ; what is left of it (0: all 64KB, which
    jz .c2                          ; no 16-bit CX passes)
    cmp cx, di
    jbe .c2
    mov cx, di
.c2:
    mov si, ax                      ; SI = the offset in the window
    mov ax, [bx+HW_STEP]
    mul dx                          ; AX = the step x the window's index
    add ax, [bx+HW_SEG]
    mov es, ax
    mov di, si
    pop si
    pop dx
    pop ax
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; hw_span - CF = 1 when DX:AX + CX runs past HW_LEN (BX -> the record).
; keeps every register
; -----------------------------------------------------------------------------
hw_span:
    push ax
    push dx
    push si
    push di
    add ax, cx
    adc dx, 0
    mov si, [bx+HW_LEN]
    mov di, [bx+HW_LEN+2]
    sub si, ax                      ; CF = 1: the span's end is past it
    sbb di, dx
    pop di
    pop si
    pop dx
    pop ax
    ret

%ifdef HW_GET
%define HW_MOVE
; -----------------------------------------------------------------------------
; hw_get - CX bytes OUT of the claim
; in:  BX -> the record, DX:AX = the offset in the claim, CX = how many (0
;      copies nothing), ES:DI = where they go (any segment: yours, a claim's)
; out: CF = 0 copied; CF = 1 the span runs past HW_LEN, nothing copied
; keeps every register. A FRAME ON THE STACK: BP addresses SS, which is
; where the frame is (CLAUDE.md's SS != DS: nothing here reads DS through BP)
; -----------------------------------------------------------------------------
hw_get:
    call hw_span
    jc .r
    jcxz .r                         ; (CF = 0)
    push ax
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es                         ; [bp+2] = the buffer's segment
    push cx                         ; [bp+0] = the bytes left
    mov bp, sp
.lp:
    mov cx, [bp]
    mov si, di                      ; SI = the buffer's offset, for now
    call hw_aim                     ; ES:DI = the claim's byte, CX = the piece
    sub [bp], cx
    add ax, cx
    adc dx, 0
    xchg si, di                     ; DS:SI the claim, ES:DI the buffer
    push ds
    push es
    pop ds
    mov es, [bp+2]
    call hw_mov
    pop ds
    cmp word [bp], 0
    jne .lp
    pop cx
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    clc
.r:
    ret
%endif

%ifdef HW_PUT
%define HW_MOVE
; -----------------------------------------------------------------------------
; hw_put - CX bytes INTO the claim
; in:  BX -> the record, DX:AX = the offset in the claim, CX = how many (0
;      copies nothing), ES:DI = where they come from (any segment)
; out: CF = 0 copied; CF = 1 the span runs past HW_LEN, nothing copied
; keeps every register (a frame on the stack, as hw_get's)
; -----------------------------------------------------------------------------
hw_put:
    call hw_span
    jc .r
    jcxz .r
    push ax
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es                         ; [bp+2] = the source's segment
    push cx                         ; [bp+0] = the bytes left
    mov bp, sp
.lp:
    mov cx, [bp]
    mov si, di                      ; SI = the source's offset
    call hw_aim                     ; ES:DI = the claim's byte, CX = the piece
    sub [bp], cx
    add ax, cx
    adc dx, 0
    push ds
    mov ds, [bp+2]                  ; DS:SI the source, ES:DI the claim
    call hw_mov
    pop ds
    mov di, si                      ; the source's next byte, for the aim
    cmp word [bp], 0
    jne .lp
    pop cx
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    clc
.r:
    ret
%endif

%ifdef HW_MOVE
; hw_mov - CX bytes DS:SI -> ES:DI, words then an odd byte. SI, DI advanced;
; CX = 0 after
hw_mov:
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    ret
%endif

%ifdef HW_RSEQ
; -----------------------------------------------------------------------------
; hw_fread - a file read INTO the claim: claim byte X is the file's byte X +
;            HW_FOFF (0 unless you set it: the file at its own offsets). One
;            os88_rseq a window the span touches, each straight into the
;            window (the kernel's file doors take a selector, SPEC.md 50.3.5)
; in:  SI -> a NUL 8.3 name, DS:DI -> an os88_rseq cursor (FSEQ_SIZE), BX ->
;      the record, DX:AX = the CLAIM offset, CX = the most bytes (READ_AT's
;      rule: a cluster multiple, and so is every piece - a window ends on
;      64KB - but the last, which HW_LEN may cut)
; out: CF = 0, DX:AX = the bytes delivered (fewer than CX: the file's end, or
;      HW_LEN reached); CF = 1, AX = FERR_* (what came before it is in)
; keeps BX, CX, SI, DI, BP, DS, ES
; -----------------------------------------------------------------------------
hw_fread:
    push bx
    push cx
    push si
    push di
    push bp
    push es
    push cx                         ; [bp+4] = the bytes left
    xor bp, bp
    push bp                         ; [bp+2]:[bp+0] = the bytes delivered
    push bp
    mov bp, sp
.lp:
    mov cx, [bp+4]
    mov bx, [bp+16]                 ; the record
    call hw_aim                     ; ES:DI = the window's byte, CX = the piece
    jc .done                        ; (CX = 0 left, or HW_LEN reached)
    push dx
    push ax
    add ax, [bx+HW_FOFF]            ; DX:AX = the file's offset
    adc dx, [bx+HW_FOFF+2]
    mov bx, di                      ; ES:BX = the buffer
    mov di, [bp+10]                 ; DS:DI = the cursor
    call os88_rseq                  ; DX:AX = delivered; keeps CX, the piece
    jc .err
    mov di, ax                      ; DI = the piece's bytes (no more than CX)
    pop ax
    pop dx
    add ax, di
    adc dx, 0
    add [bp], di
    adc word [bp+2], 0
    sub [bp+4], di
    cmp di, cx
    je .lp                          ; the piece whole: on to the next
.done:
    mov ax, [bp]
    mov dx, [bp+2]
    clc
    jmp short .out
.err:
    pop dx                          ; the offset pushed (AX is the FERR_*)
    pop dx
    stc
.out:
    lea sp, [bp+6]                  ; (lea: the flags are the answer)
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret
%endif

%ifdef HW_WSEQ
; -----------------------------------------------------------------------------
; hw_fwrite - CX bytes of the claim onto an os88_wseq stream, one
;             os88_wseq_put a window the span touches
; in:  DS:DI -> the stream (open), BX -> the record, DX:AX = the offset in the
;      claim, CX = how many (0 writes nothing)
; out: CF = 0 put; CF = 1 AX = FERR_* - the stream's, or FERR_BIG for a span
;      past HW_LEN (nothing put)
; keeps everything but AX
; -----------------------------------------------------------------------------
hw_fwrite:
    call hw_span
    jnc .go
    mov ax, FERR_BIG
    ret
.go:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ax                         ; [bp+2] = the offset's low word
    push cx                         ; [bp+0] = the bytes left
    mov bp, sp
.lp:
    mov cx, [bp]
    jcxz .ok
    mov ax, [bp+2]
    call hw_aim                     ; ES:DI = the window's byte, CX = the piece
    sub [bp], cx
    add [bp+2], cx
    adc dx, 0
    mov si, di                      ; ES:SI = the bytes
    mov di, [bp+8]                  ; DS:DI = the stream
    call os88_wseq_put              ; keeps everything but AX
    jnc .lp
    jmp short .out                  ; (CF = 1, AX = FERR_*)
.ok:
    clc
.out:
    lea sp, [bp+4]
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%endif
