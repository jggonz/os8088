; SHRINK.COM - DOES A DOS CALL SCRIBBLE ON THE BLOCK THE PROGRAM GAVE BACK?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; `AH=4Ah` with `BX = SS + 2 - PSP` is the standard shrink idiom - every
; launcher and every C runtime start-up does it - and the free MCB DOS then
; cuts sits at the paragraph PAST the block.  For a program whose SP is a
; paragraph or two above SS, that is the sixteen bytes DIRECTLY BELOW its own
; stack pointer.
;
; A real DOS switches to an internal stack at its first instruction, so all
; that ever lands there is the three words the `int` pushed - which fall in
; the new block's RESERVED bytes, at +0A..+0F, where nothing reads them.  This
; box used to build its whole INT 21h frame on the program's stack instead,
; six words deeper, straight onto the SIGNATURE, the OWNER and the SIZE
; (SPEC.md 96.7.2).
;
; The Playroom is what reported it: `PLAYROOM.EXE` shrinks, asks its video
; question, and `AH=4B00`s `PLAYEGA.EXE` - which answered `AX=0008`, "not
; enough memory", with 434 KB free and a chain whose last MCB read
; `sig=00 own=6C8D size=F323`.
;
;   nasm -f bin -o SHRINK.COM tests/dostrap/shrink.asm
;
; It runs under os8088 AND under a real DOS unchanged, which is the point of
; everything in this directory: `MCB INTACT` is what a DOS answers.
;
; **THE GEOMETRY IS THE TEST.**  `MCB_AT` is where the free header lands and
; `SP` is set SIXTEEN BYTES ABOVE IT, so the very first thing pushed below
; that point is on the header.  Keep this program under `MCB_AT` bytes long.
    org 0x100
    cpu 8086

KEEP    equ 0x100                   ; paragraphs kept: 4,096 bytes
MCB_AT  equ KEEP * 16               ; ...so the free MCB is at 0x1000, and
SP_AT   equ MCB_AT + 16             ; the stack starts ONE PARAGRAPH past its
                                    ; end, which is Playroom's own geometry:
                                    ; SS + 2 kept, SP at SS + 3
SP_SAFE equ 0x0F00                  ; ...and where the stack goes once the
                                    ; measurement is over (see below)

start:
    mov [pspseg], cs
    mov [pbc+4], cs                 ; the parameter block's tail SEGMENT
    cli
    mov sp, SP_AT                   ; INSIDE the block we are about to keep,
    sti                             ; with the free header directly below

    ; --- the shrink, and NOTHING between it and the first snapshot ----------
    mov es, [pspseg]
    mov bx, KEEP
    mov ah, 0x4A
    int 0x21
    mov word [shrcf], 0
    jnc .kept
    mov word [shrcf], 1
.kept:
    mov si, MCB_AT                  ; snapshot A, taken before ANY other call:
    mov al, [si]                    ; the header as dos_mcb_split left it
    mov [asig], al
    mov ax, [si+1]
    mov [aown], ax
    mov ax, [si+3]
    mov [asz], ax

    ; --- five ordinary DOS calls, which is the whole of the experiment ------
    ; **NOT ONE PUSH OF OUR OWN IN HERE**, and that is the test rather than
    ; tidiness. A stack grows DOWN, so with SP one paragraph above the header
    ; anything this program pushes lands on it too - and then the row would go
    ; red on a box that is behaving. Playroom pushes three words at most
    ; before its own calls, which is what fits in the header's RESERVED bytes;
    ; so the counter is a memory cell and `loop`'s CX would be eaten by AH=30h
    ; anyway.
    mov byte [spin], 5
.spin:
    mov ah, 0x30                    ; GET VERSION: it reads nothing, writes
    int 0x21                        ; nothing and touches no disk, so the only
    dec byte [spin]                 ; thing it can leave behind is a frame
    jnz .spin

    mov si, MCB_AT                  ; ...and snapshot B
    mov al, [si]
    mov [bsig], al
    mov ax, [si+1]
    mov [bown], ax
    mov ax, [si+3]
    mov [bsz], ax

    ; --- and now say what happened ------------------------------------------
    ; THE STACK MOVES FIRST. Everything below here calls, and `putc` alone
    ; pushes eight registers - so leaving SP where the measurement needed it
    ; would have this program scribble on the very header it has just read.
    cli
    mov sp, SP_SAFE
    sti

    mov si, s_head
    call puts
    mov si, s_shr
    call puts
    mov ax, [shrcf]
    call hex4
    call eol

    mov si, s_a
    call puts
    mov al, [asig]
    call sigout
    mov ax, [aown]
    call hex4
    mov al, ' '
    call putc
    mov ax, [asz]
    call hex4
    call eol

    mov si, s_b
    call puts
    mov al, [bsig]
    call sigout
    mov ax, [bown]
    call hex4
    mov al, ' '
    call putc
    mov ax, [bsz]
    call hex4
    call eol

    mov al, [asig]
    cmp al, [bsig]
    jne .smashed
    mov ax, [aown]
    cmp ax, [bown]
    jne .smashed
    mov ax, [asz]
    cmp ax, [bsz]
    jne .smashed
    mov si, s_ok
    jmp short .verdict
.smashed:
    mov si, s_bad
.verdict:
    call puts

    ; --- what the allocator says a moment before the EXEC asks it ----------
    ; `dos_exec_load` asks for 0FFFFh to learn the largest free block and then
    ; asks for exactly that, so these two lines are its own two calls made by
    ; hand. A refusal here and a refusal there are the same refusal; a grant
    ; here and a refusal there is something else in `.exec`.
    mov si, s_max
    call puts
    mov bx, 0xFFFF
    mov ah, 0x48
    int 0x21
    mov [most], bx
    mov [gax], ax
    mov ax, [gax]
    call hex4
    mov al, ' '
    call putc
    mov ax, [most]
    call hex4
    call eol

    mov si, s_take
    call puts
    mov bx, [most]
    mov ah, 0x48
    int 0x21
    jc .notook
    mov [gax], ax
    mov si, s_got
    call puts
    mov ax, [gax]
    call hex4
    call eol
    mov es, [gax]                   ; ...and give it straight back, or the
    mov ah, 0x49                    ; EXEC below has nothing to be granted
    int 0x21
    jmp short .exec
.notook:
    mov [gax], ax
    mov si, s_nogot
    call puts
    mov ax, [gax]
    call hex4
    call eol
.exec:

    ; --- ...and the call the whole thing exists to protect -------------------
    mov si, s_exec
    call puts
    push cs
    pop ds
    push cs
    pop es
    mov dx, n_com
    mov bx, pbc
    mov ax, 0x4B00
    int 0x21
    push cs                         ; INLINE: DOS has already put the parent's
    pop ds                          ; SS:SP back, and a routine that reset SP
    push cs                         ; would destroy its own return address
    pop es
    jnc .ran
    mov [gax], ax
    mov si, s_efail
    call puts
    mov ax, [gax]
    call hex4
    call eol
.ran:
    mov si, s_key
    call puts
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

sigout:
    call hex2
    mov al, ' '
    jmp putc

n_com:  db 'SHRKID.COM', 0
pbc:    dw 0
        dw tailc, 0
        dw 0, 0
        dw 0, 0
tailc:  db 0, 13

s_head: db 'SHRINK', 13, 10, 0
s_shr:  db '4Ah cf=', 0
s_a:    db 'A ', 0
s_b:    db 'B ', 0
s_ok:   db 'MCB INTACT', 13, 10, 0
s_bad:  db 'MCB SMASHED', 13, 10, 0
s_max:  db '48h FFFF -> ', 0
s_take: db '48h largest: ', 0
s_got:  db 'GRANTED seg=', 0
s_nogot: db 'REFUSED ax=', 0
s_exec: db 'exec: ', 0
s_efail: db 'REFUSED ax=', 0
s_key:  db 'KEY', 13, 10, 0

putc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
puts:
    push ax
.l:
    mov al, [si]
    inc si
    or al, al
    jz .o
    call putc
    jmp short .l
.o:
    pop ax
    ret
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp putc
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
    call hex2
    ret
hex2:
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call hexd
    pop ax
    and al, 0x0F
    call hexd
    pop cx
    pop ax
    ret
hexd:
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
    jmp putc

; A .COM owns every byte after its image, and every cell here has to sit
; BELOW the free header - which the assertion at the end of the file checks
; rather than leaving to whoever next adds a string.
pspseg: equ $
shrcf:  equ $ + 2
asig:   equ $ + 4
aown:   equ $ + 6
asz:    equ $ + 8
bsig:   equ $ + 10
bown:   equ $ + 12
bsz:    equ $ + 14
gax:    equ $ + 16
most:   equ $ + 18
spin:   equ $ + 20
VARS_END equ $ + 21

%if (VARS_END - $$) > (MCB_AT - 0x100)     ; both sides SCALAR: an address
                                           ; minus the section base is a
                                           ; number, an address alone is not
  %error "SHRINK.COM has grown past the free MCB it is watching"
%endif
