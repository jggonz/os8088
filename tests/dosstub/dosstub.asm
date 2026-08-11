; =============================================================================
; tests/dosstub - just enough DOS to RUN a .COM, on a machine that has none
;
; WHY THIS EXISTS. `OS88NET.COM` is the DOS half of SPEC.md 62's parallel link,
; and this container has no DOS - MartyPC boots os8088 and 86Box boots os8088,
; and neither has a copy of DOS this tree may ship. So the DOS end was written,
; assembled, packaged and SENT TO THE FIELD TWICE without one instruction of it
; ever being executed. The second time it came back with the whole answer:
; "returns to prompt instantly with nothing printed, whatever parameters".
;
; The bug was that its two %includes sat above `start:`, and DOS enters a .COM
; at offset 0x100 - the FIRST byte of the file. So DOS ran the transport's
; first routine on a garbage port, hit its `ret`, popped the word DOS pushes at
; PSP:0000 and terminated on the int 20h sitting there. Both .COM sources
; assert their entry is at 0x100 now, which stops THAT bug. This stops the
; class: it is a machine to run the program on.
;
; WHAT IT IS. A bootable 360KB floppy carrying an int 21h stub and the .COM
; embedded in its own image. It boots on the same 4.77MHz 8088 MartyPC runs
; everything else on, with a REAL parallel port at 0x378, so the latch probe,
; the port scan and the dwell all execute for real - which is the same
; boundary NET.DRV has and for the same reason: MartyPC has a port and cannot
; be a PARTNER, because its status lines read a constant.
;
; WHAT IT IS NOT. It is not DOS and must never grow towards being DOS. It
; implements the seven int 21h functions os88net.com actually calls and
; refuses the rest LOUDLY - an unimplemented call prints its AH and halts,
; rather than returning a plausible zero, because a stub that silently
; succeeds is how a harness starts lying about the thing it is testing.
;
;   make dosstub          build/dosstub.img, and how to run it
;
; The "file" it serves is RAM: DSF_SEG holds the first DSF_KB, and the SIZE it
; reports is a pair of constants, so the sector-count arithmetic in
; open_image - a 32-bit byte count folded into a 16-bit sector count with no
; 32-bit registers - is exercised over its whole range without a 32MB image.
;   make dosstub                     10MB, the ordinary path (20,480 sectors)
;   make dosstub FSIZE=64M           past os8088's cap: 65,535, and it says so
;   make dosstub FSIZE=256           under one sector: the refusal
;   make dosstub ARGS='/RO /P:378'   the command tail, which is code nothing
;                                    else in this tree executes
; =============================================================================

    cpu 8086
    org 0                       ; loaded at KERNEL_SEG:0000 by boot/boot.asm

com_entry:
    times ($$ - com_entry) db 0 ; the entry is the first byte here too

; -----------------------------------------------------------------------------
; The boot sector's handoff contract (boot/boot.asm), so the shipped loader can
; carry this instead of the kernel: it far-calls SPLASH_OFF = 0x0008 once the
; first sectors are aboard, writes its t=0 word at 0x000C, and far-jumps to
; 0x0000 with DL = the boot drive. tests/lptlink/lptlink.asm has the same
; three lines and for the same reason.
;
; LEAVING THEM OUT IS NOT A CRASH, WHICH IS WHY IT COSTS A SESSION: the loader
; far-called offset 8 - four bytes into this file's own first instruction -
; ran whatever that decoded as, and returned. The machine then booted and ran
; the program, and the only symptom was that the screen had one line on it.
; -----------------------------------------------------------------------------
    jmp start
    times 0x08-($-$$) db 0
    retf                        ; 0x0008 - the splash tick, which we have none
    times 0x0C-($-$$) db 0      ; of, so it just returns
    dw 0                        ; 0x000C - the boot timer's t=0

PSP_SEG     equ 0x2000          ; 128KB: above us, below the RAM file
DSF_SEG     equ 0x4000          ; 256KB: the backing store's first DSF_KB
DSF_KB      equ 32              ; ...and how much of it is real. 32 and not
                                ; 64 because clamp's `DSF_KB * 1024` is a
                                ; word and 64KB is 0x10000: the one place a
                                ; round number does not fit the machine

%ifndef FSIZE_LO
%define FSIZE_LO 0x0000         ; default 10MB = 0x00A00000 -> 20,480 sectors
%endif
%ifndef FSIZE_HI
%define FSIZE_HI 0x00A0
%endif

FH          equ 5               ; the one handle we hand out

start:
    cli
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, stack_top
    sti
    cld

    mov si, s_banner
    call puts

    ; --- the two vectors a .COM can leave through ---------------------------
    xor ax, ax
    mov es, ax
    cli
    mov word [es:0x20*4], int20
    mov [es:0x20*4+2], cs
    mov word [es:0x21*4], int21
    mov [es:0x21*4+2], cs
    sti

    ; --- a PSP, and the command tail argument parsing needs -----------------
    mov ax, PSP_SEG
    mov es, ax
    xor di, di
    mov cx, 128
    xor ax, ax
    rep stosw                   ; 256 bytes of PSP, cleared
    mov word [es:0x0000], 0x20CD ; int 20h, which is what a bare `ret` lands on
    mov si, cmdtail_txt         ; the text first, COUNTING it: the length byte
    mov di, 0x0081              ; at 0x80 is a forward reference if it is
    xor cx, cx                  ; emitted with the string, and nasm is right
.tl:                            ; to refuse that
    lodsb
    or  al, al
    jz  .tdone
    stosb
    inc cx
    jmp short .tl
.tdone:
    mov al, 13                  ; ...and DOS's own terminator
    stosb
    mov [es:0x0080], cl

    ; --- the program itself, to PSP:0100 ------------------------------------
    mov si, com_image
    mov di, 0x0100
    mov cx, com_len
    rep movsb

    mov si, s_go
    call puts

    ; --- and away: DS = ES = SS = the PSP, a word of 0 on the stack ---------
    mov ax, PSP_SEG
    mov ds, ax
    mov es, ax
    cli
    mov ss, ax
    mov sp, 0xFFFE
    sti
    xor ax, ax
    push ax                     ; so a bare `ret` reaches PSP:0000's int 20h
    push ds                     ; ...and the far jump in
    mov ax, 0x0100
    push ax
    retf

; =============================================================================
; int 20h / int 21h
;
; THE IRET FRAME IS [bp+2] IP, [bp+4] CS, [bp+6] FLAGS after `push bp` /
; `mov bp, sp`, and CF has to be written THERE rather than left in the live
; flags - an `iret` reloads them from the stack, so a `clc` before it is a
; `clc` the caller never sees. Every DOS call here answers in CF.
; =============================================================================
int20:
    xor al, al
    jmp term

int21:
    sti
    cmp ah, 0x02
    je  f_putc
    cmp ah, 0x3D
    je  f_open
    cmp ah, 0x3E
    je  f_close
    cmp ah, 0x3F
    je  f_read
    cmp ah, 0x40
    je  f_write
    cmp ah, 0x42
    je  f_seek
    cmp ah, 0x4C
    je  term
    jmp f_unimp

; --- AH=02h: DL to the screen -------------------------------------------------
f_putc:
    push ax
    mov al, dl
    call putc
    pop ax
    iret

; --- AH=3Dh: open. Any name, one handle, position 0. --------------------------
; The NAME is at the CALLER's DS:DX and is printed before anything moves,
; which is the point of a harness: "the open failed" and "the open failed on
; THAT name" are different sentences.
f_open:
    push si
    push ds
    mov si, dx
    call farputs                ; DS:SI, still the caller's segment
    push cs
    pop ds
%ifdef FAILOPEN
    mov si, s_nofile            ; ...and the OTHER answer, which the program
    call puts                   ; has an error path for that nothing else here
    pop ds                      ; can reach: DOS says no.
    pop si
    mov ax, 2                   ; file not found
    jmp cf_set
%else
    mov si, s_opened
    call puts
    pop ds
    pop si
    mov word [cs:fpos], 0
    mov word [cs:fpos+2], 0
    mov ax, FH
    jmp cf_clear
%endif

; --- AH=3Eh: close ------------------------------------------------------------
f_close:
    jmp cf_clear

; --- AH=3Fh: read into the caller's DS:DX ------------------------------------
; ONLY THE FIRST DSF_KB IS REAL. A request past it is answered SHORT rather
; than wrongly - a caller that asked for 512 and got 0 finds out, where one
; handed 512 bytes of whatever was in RAM does not.
f_read:
    push bx
    push cx
    push si
    push di
    push ds
    push es
    call clamp                  ; CX = how much of it is real, from [fpos]
    mov [cs:xfer], cx
    jcxz .none
    mov ax, ds                  ; the caller's DS:DX is the DESTINATION
    mov es, ax
    mov di, dx
    mov ax, DSF_SEG
    mov ds, ax
    mov si, [cs:fpos]           ; DSF_KB <= 64, so the offset is one word
    rep movsb
.none:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    mov ax, [cs:xfer]
    call fadv
    jmp cf_clear

; --- AH=40h: write from the caller's DS:DX -----------------------------------
f_write:
    push bx
    push cx
    push si
    push di
    push ds
    push es
    call clamp
    mov [cs:xfer], cx
    jcxz .none
    mov si, dx                  ; the caller's DS:DX is the SOURCE
    mov ax, DSF_SEG
    mov es, ax
    mov di, [cs:fpos]
    rep movsb
.none:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    mov ax, [cs:xfer]
    call fadv
    jmp cf_clear

; --- AH=42h: seek. AL = 0 start / 1 current / 2 end. CX:DX in, DX:AX out. ----
f_seek:
    cmp al, 2
    je  .end
    cmp al, 1
    je  .cur
    mov [cs:fpos], dx           ; from the start
    mov [cs:fpos+2], cx
    jmp short .out
.cur:
    add [cs:fpos], dx
    adc [cs:fpos+2], cx
    jmp short .out
.end:
    mov ax, FSIZE_LO
    add ax, dx
    mov [cs:fpos], ax
    mov ax, FSIZE_HI
    adc ax, cx
    mov [cs:fpos+2], ax
.out:
    mov ax, [cs:fpos]
    mov dx, [cs:fpos+2]
    jmp cf_clear

; --- anything else: SAY SO AND STOP ------------------------------------------
; Never a plausible zero. A stub that quietly succeeds at a call it does not
; implement is a harness that has started lying about the thing under test.
f_unimp:
    push cs
    pop ds
    mov al, ah
    push ax
    mov si, s_unimp
    call puts
    pop ax
    call puthex8
    mov si, s_halt
    call puts
    jmp hang

; --- the CF answer, written into the stacked FLAGS ---------------------------
cf_clear:
    push bp
    mov bp, sp
    and word [bp+6], 0xFFFE
    pop bp
    iret

cf_set:
    push bp
    mov bp, sp
    or  word [bp+6], 1
    pop bp
    iret

; =============================================================================
; helpers the handlers share
; =============================================================================
; clamp - CX = how many of the requested bytes are inside the real DSF_KB
; in:  CX = wanted, [cs:fpos] = position;  out: CX = allowed
clamp:
    push ax
    cmp word [cs:fpos+2], 0
    jne .zero                   ; past the real part: none of it is
    mov ax, DSF_KB * 1024
    sub ax, [cs:fpos]
    jbe .zero
    cmp cx, ax
    jbe .out
    mov cx, ax
    jmp short .out
.zero:
    xor cx, cx
.out:
    pop ax
    ret

; fadv - advance [cs:fpos] by AX, AX preserved
fadv:
    push ax
    add [cs:fpos], ax
    adc word [cs:fpos+2], 0
    pop ax
    ret

term:
    push cs
    pop ds
    push ax
    mov si, s_term
    call puts
    pop ax
    call puthex8
    mov si, s_halt
    call puts
hang:
    hlt
    jmp short hang

; =============================================================================
; Console, through the BIOS - nothing here may use int 21h
; =============================================================================
putc:
    push ax
    push bx
    push bp
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop bp
    pop bx
    pop ax
    ret

puts:
    push ax
    push si
.l:
    lodsb
    or  al, al
    jz  .out
    call putc
    jmp short .l
.out:
    pop si
    pop ax
    ret

; farputs - the same string walk, through the CALLER's DS
farputs:
    push ax
    push si
.l:
    lodsb
    or  al, al
    jz  .out
    call putc
    jmp short .l
.out:
    pop si
    pop ax
    ret

puthex8:
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call .nib
    pop ax
    push ax
    and al, 0x0F
    call .nib
    pop ax
    pop cx
    pop ax
    ret
.nib:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    call putc
    ret

; =============================================================================
s_banner:   db 13,10,'dosstub: just enough DOS to run a .COM',13,10,0
s_go:       db 'dosstub: handing over',13,10,13,10,0
s_opened:   db ' <- open',13,10,0
s_nofile:   db ' <- NOT FOUND (FAILOPEN)',13,10,0
s_term:     db 13,10,13,10,'dosstub: program exited, AL=',0
s_unimp:    db 13,10,13,10,'dosstub: UNIMPLEMENTED int 21h AH=',0
s_halt:     db 13,10,0

; The command tail, as DOS builds it: a length byte, then the text, and this
; stub appends the CR. Argument parsing is code nothing else in this tree
; executes, so the tail is a build knob rather than a constant.
cmdtail_txt:
%ifdef ARGS
    db ARGS
%endif
    db 0

xfer:       dw 0
fpos:       dd 0

    align 2
com_image:
%ifndef COMFILE
%define COMFILE "build/os88net.com"
%endif
    incbin COMFILE
com_len     equ $ - com_image

    times 512 db 0
stack_top:
