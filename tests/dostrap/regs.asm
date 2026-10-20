; REGS.COM - WHICH REGISTERS DOES `INT 21h` GIVE BACK?  OURS, MIT with the
; rest of the tree.
;
; THE POINT IS THAT ONE BINARY RUNS ON BOTH (tests/dostrap/dosref.asm's shape,
; and docs/DOS-DEBUGGING.md's method).  "DOS preserves every register it does
; not answer in" is a sentence out of a reference book; the same program run
; under IBM DOS 3.30 and under the box, printing the same table, is a
; MEASUREMENT - and SPEC.md 96.7.1.1 is what happens when the sentence is
; applied by reasoning instead: the argument was made for SI, DI and ES, DX
; was left out of it because five functions answer in DX, and DX turned out to
; be destroyed on 37 opens out of 37.  BX and CX have never been measured at
; all, which is what this is for.
;
; HOW IT WORKS.  Every register the call does not need is loaded with a
; sentinel, the ones it does need with real arguments, and the whole set is
; pushed the instruction after the `int` - before anything else can touch it,
; and in particular before the AH=02h that prints the answer, which is itself
; one of the calls under test.  Each register is then compared with what went
; in.  `.` is what went in; the register's own letter is a change.
;
;   B C D S I P E G   =  BX CX DX SI DI BP ES DS
;
; **A CHANGE IS NOT A DEFECT AND THIS PROGRAM DOES NOT SAY IT IS.**  Five
; functions answer in DX, `AH=30h` answers in BX and CX, `AH=2Fh` and `AH=35h`
; answer in ES:BX: those read `X` on a correct DOS.  The finding is the DIFF
; between the two columns, which is why the mask is printed raw and judged
; nowhere in here.
;
; CF IS PRINTED WITH EVERY ROW, and it is not decoration: a call that fails on
; one machine and succeeds on the other has a different set of outputs, so a
; mask compared without it is comparing two different questions.
;
; THE DEVICE WORD IS ASKED THREE TIMES because SPEC.md 96.7.1.1's third row is
; exactly one bit of it: `AH=44h AL=00h` on a file handle answers a word whose
; bit 6 means THIS HANDLE HAS NOT BEEN WRITTEN THROUGH, so the answer must
; change when the program writes.  One reading cannot show that and two cannot
; say which way round it is; three - just opened, just created, and after a
; write - pin it.
;
; It opens ITSELF (`REGS.COM` in the current directory), so it needs no
; fixture and cannot be run against the wrong file, and it creates, writes and
; deletes `REGTMP.$$$` beside it.  Run it from the drive under test.
    org 0x100
    cpu 8086

; --- the sentinels ----------------------------------------------------------
; Distinctive, non-zero, and nothing a handler could plausibly leave behind by
; accident.  ES's is a segment and merely LOADING one is harmless on an 8086;
; BP's is never dereferenced by us, and a handler that wants a frame pointer
; sets up its own.
SBX     equ 0xB1B1
SCX     equ 0xC1C1
SDX     equ 0xD1D1
SSI     equ 0x5151
SDI     equ 0xD151
SBP     equ 0xB9B9
SES     equ 0xE1E1

; --- the row flags ----------------------------------------------------------
F_FH    equ 0x01                    ; BX = the handle the last open/create gave
F_SI    equ 0x02                    ; SI in the row is an ARGUMENT, not a sentinel
F_KEEP  equ 0x04                    ; ...and this row's AX is a handle: bank it
F_ES    equ 0x08                    ; ES = OUR segment: the three calls that take
                                    ; an ES:DI destination (29h, 56h) cannot be
                                    ; given the sentinel
ROWSZ   equ 18

start:
    mov ah, 0x1A                    ; our own DTA, so the PSP's 80h stays the
    mov dx, dta                     ; command tail (dosref.asm's reason)
    int 0x21
    mov si, s_head
    call puts

    mov si, tab
    mov word [ncell], 0
.row:
    mov ax, [si + 14]               ; AX=0 ends the table - no INT 21h function
    or ax, ax                       ; is 0000h here, AH=00h being terminate
    jz .done
    mov [cur], si
    call do_row
    add word [cur], ROWSZ
    mov si, [cur]
    jmp short .row
.done:
    call eol
    mov si, s_dev
    call puts
    mov ax, [dw_open]
    call hex4
    mov si, s_dev2
    call puts
    mov ax, [dw_new]
    call hex4
    mov si, s_dev3
    call puts
    mov ax, [dw_writ]
    call hex4
    mov si, s_dev4
    call puts
    mov ax, [dw_cross]
    call hex4
    call eol
    mov si, s_done
    call puts
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

; -----------------------------------------------------------------------------
; do_row - load [cur]'s registers, make the call, print the mask
;
; **NOTHING IS KEPT IN A REGISTER ACROSS THE `int`**, which is the whole
; instrument: everything this file does afterwards goes through AH=02h, so a
; value banked in a register would be banked across one of the very calls
; being measured.  [cur] is a static for the same reason.
; -----------------------------------------------------------------------------
do_row:
    mov si, [cur]
    mov al, [si + 3]                ; the flags
    mov [flags], al
    mov ax, [si + 4]
    mov [i_bx], ax
    test byte [flags], F_FH
    jz .nofh
    mov ax, [fh]
    mov [i_bx], ax
.nofh:
    mov ax, [si + 6]
    mov [i_cx], ax
    mov ax, [si + 8]
    mov [i_dx], ax
    mov ax, [si + 10]
    mov [i_si], ax
    mov ax, [si + 12]
    mov [i_di], ax
    mov ax, [si + 14]
    mov [i_ax], ax
    mov ax, SES                     ; ES is a sentinel unless the row asks for
    test byte [flags], F_ES         ; a real one, which only an ES:DI
    jz .noes                        ; destination does
    mov ax, cs
.noes:
    mov [i_es], ax

    mov es, ax
    mov bx, [i_bx]
    mov cx, [i_cx]
    mov dx, [i_dx]
    mov si, [i_si]
    mov di, [i_di]
    mov bp, SBP
    mov ax, [i_ax]
    int 0x21
    ; --- the instruction after the int, and nothing between ----------------
    pushf
    push ds
    push es
    push bp
    push di
    push si
    push dx
    push cx
    push bx
    push ax
    push cs                         ; ...OUR DS back, whatever the call left
    pop ds                          ; in it - the stores below need it, and
    pop ax                          ; `o_ds` is how the change gets recorded
    mov [o_ax], ax
    pop ax
    mov [o_bx], ax
    pop ax
    mov [o_cx], ax
    pop ax
    mov [o_dx], ax
    pop ax
    mov [o_si], ax
    pop ax
    mov [o_di], ax
    pop ax
    mov [o_bp], ax
    pop ax
    mov [o_es], ax
    pop ax
    mov [o_ds], ax
    pop ax
    mov [o_fl], ax

    mov si, [cur]                   ; **THE DEVICE WORD, WHERE THE ROW SAYS**
    mov si, [si + 16]               ; (SPEC.md 96.7.1.1's third row): AH=44h
    or si, si                       ; answers in DX, so the mask records only
    jz .nobank                      ; THAT it changed - the value is the whole
    mov ax, [o_dx]                  ; finding and has to be kept somewhere the
    mov [si], ax                    ; end of the run can print
.nobank:
    test byte [flags], F_KEEP       ; an open or a create: bank the handle for
    jz .nokeep                      ; the rows that name it, but only if it
    test byte [o_fl], 1             ; WORKED - a failed open answers an error
    jnz .nokeep                     ; code in AX, and using that as a handle
    mov ax, [o_ax]                  ; would make every row after it nonsense
    mov [fh], ax
.nokeep:

    ; --- the row: LLL=BCDSIPEG/C -------------------------------------------
    mov si, [cur]
    call puts3
    mov al, '='
    call putc
    mov ax, [o_bx]
    mov cx, [i_bx]
    mov bl, 'B'
    call mark
    mov ax, [o_cx]
    mov cx, [i_cx]
    mov bl, 'C'
    call mark
    mov ax, [o_dx]
    mov cx, [i_dx]
    mov bl, 'D'
    call mark
    mov ax, [o_si]
    mov cx, [i_si]
    mov bl, 'S'
    call mark
    mov ax, [o_di]
    mov cx, [i_di]
    mov bl, 'I'
    call mark
    mov ax, [o_bp]
    mov cx, SBP
    mov bl, 'P'
    call mark
    mov ax, [o_es]
    mov cx, [i_es]
    mov bl, 'E'
    call mark
    mov ax, [o_ds]
    mov cx, cs
    mov bl, 'G'
    call mark
    mov al, '/'
    call putc
    mov al, [o_fl]
    and al, 1
    add al, '0'
    call putc
    mov al, ' '
    call putc
    inc word [ncell]                ; THREE TO A LINE: 24 rows is more than a
    mov ax, [ncell]                 ; 25-line screen can hold one to a line,
    mov cl, 3                       ; and this program is read off the glass
    xor ah, ah                      ; on both machines
    div cl
    or ah, ah
    jnz .out
    call eol
.out:
    ret

; --- mark - AX against CX: '.' if it came back, BL if it did not -------------
mark:
    cmp ax, cx
    jne .no
    mov al, '.'
    jmp short putc
.no:
    mov al, bl
    jmp short putc

; --- helpers (tests/dostrap/dosref.asm's, and through statics for its reason)
putc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
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
    jmp short putc
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp short putc
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
puts3:                              ; the row label, exactly three characters
    push ax
    push cx
    mov cx, 3
.l:
    mov al, [si]
    inc si
    call putc
    dec cx
    jnz .l
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE TABLE.  Each row: 3 label bytes, a flag byte, then BX, CX, DX, SI, AX
; and a place to bank the answer's DX - sixteen bytes, and the last field is
; what makes the three AH=44h rows readable at the end instead of scrolling
; past.  AX goes second to last because AX=0 is the terminator and no row here
; is AH=00h.  ES is a sentinel on every row and DI and BP on all of them -
; nothing in this set takes an argument in any of the three.
; -----------------------------------------------------------------------------
%macro ROW 9                        ; label, flags, bx, cx, dx, si, di, ax, bank
    db %1
    db %2
    dw %3, %4, %5, %6, %7, %8, %9
%endmacro

tab:
    ROW '19 ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x1900, 0        ; current drive -> AL
    ROW '2A ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x2A00, 0        ; date -> CX, DX
    ROW '2C ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x2C00, 0        ; time -> CX, DX
    ROW '30 ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x3000, 0        ; version -> AX, BX, CX
    ROW '2F ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x2F00, 0        ; get DTA -> ES:BX
    ROW '1A ', 0,      SBX, SCX, dta,    SSI, SDI, 0x1A00, 0        ; set DTA (DS:DX)
    ROW '35 ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x3533, 0        ; get vector 33h -> ES:BX
    ROW '47 ', F_SI,   SBX, SCX, 0,      cwd, SDI, 0x4700, 0        ; get cwd (DL=0, DS:SI)
    ROW '3D ', F_KEEP, SBX, SCX, n_self, SSI, SDI, 0x3D00, 0        ; open ourselves -> AX
    ROW '44o', F_FH,   0,   SCX, SDX,    SSI, SDI, 0x4400, dw_open  ; ...the device word -> DX
    ROW '3F ', F_FH,   0,   16,  buf,    SSI, SDI, 0x3F00, 0        ; read 16 -> AX
    ROW '42 ', F_FH,   0,   0,   0,      SSI, SDI, 0x4200, 0        ; seek to 0 -> DX:AX
    ROW '57 ', F_FH,   0,   SCX, SDX,    SSI, SDI, 0x5700, 0        ; file date/time -> CX, DX
    ROW '3Ea', F_FH,   0,   SCX, SDX,    SSI, SDI, 0x3E00, 0        ; close
    ROW '43 ', 0,      SBX, SCX, n_self, SSI, SDI, 0x4300, 0        ; attributes -> CX
    ROW '4E ', 0,      SBX, 0,   n_all,  SSI, SDI, 0x4E00, 0        ; findfirst
    ROW '4F ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x4F00, 0        ; findnext
    ROW '36 ', 0,      SBX, SCX, 0,      SSI, SDI, 0x3600, 0        ; free space -> AX BX CX DX
    ROW '3C ', F_KEEP, SBX, 0,   n_tmp,  SSI, SDI, 0x3C00, 0        ; create -> AX
    ROW '44c', F_FH,   0,   SCX, SDX,    SSI, SDI, 0x4400, dw_new   ; ...its device word
    ROW '40 ', F_FH,   0,   16,  buf,    SSI, SDI, 0x4000, 0        ; write 16 -> AX
    ROW '44w', F_FH,   0,   SCX, SDX,    SSI, SDI, 0x4400, dw_writ  ; ...AND AGAIN, AFTER IT
    ROW '3Eb', F_FH,   0,   SCX, SDX,    SSI, SDI, 0x3E00, 0        ; close
    ROW '56 ', F_ES,   SBX, SCX, n_tmp,  SSI, n_tmp2, 0x5600, 0     ; rename (DS:DX -> ES:DI)
    ; --- 24, so this one starts a LINE: AH=02h puts a character of its own out
    ROW '02 ', 0,      SBX, SCX, '*',    SSI, SDI, 0x0200, 0        ; TTY out (DL)
    ROW '0B ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x0B00, 0        ; keyboard status -> AL
    ROW '06 ', 0,      SBX, SCX, 0x00FF, SSI, SDI, 0x0600, 0        ; direct console in, no wait
    ; --- 27, a line start again: AH=09h puts a STRING of its own out
    ROW '09 ', 0,      SBX, SCX, s_star, SSI, SDI, 0x0900, 0        ; string out (DS:DX)
    ROW '0C ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x0C00, 0        ; flush input, AL=0 = no more
    ROW '0E ', 0,      SBX, SCX, 1,      SSI, SDI, 0x0E00, 0        ; select drive B: -> AL = count
    ROW '29 ', F_SI|F_ES, SBX, SCX, SDX, n_self, fcb, 0x2900, 0     ; parse to FCB (DS:SI -> ES:DI)
    ROW '25 ', 0,      SBX, SCX, stub,   SSI, SDI, 0x2560, 0        ; set vector 60h (DS:DX)
    ROW '35b', 0,      SBX, SCX, SDX,    SSI, SDI, 0x3560, 0        ; ...and read it back -> ES:BX
    ROW '39 ', 0,      SBX, SCX, n_dir,  SSI, SDI, 0x3900, 0        ; mkdir
    ROW '3B ', 0,      SBX, SCX, n_dir,  SSI, SDI, 0x3B00, 0        ; chdir into it
    ROW '47b', F_SI,   SBX, SCX, 0,      cwd, SDI, 0x4700, 0        ; ...cwd from a SUBDIRECTORY
    ROW '3Bb', 0,      SBX, SCX, n_root, SSI, SDI, 0x3B00, 0        ; chdir back to the root
    ROW '3A ', 0,      SBX, SCX, n_dir,  SSI, SDI, 0x3A00, 0        ; rmdir
    ROW '4D ', 0,      SBX, SCX, SDX,    SSI, SDI, 0x4D00, 0        ; child return code -> AX
    ROW '41 ', 0,      SBX, SCX, n_tmp2, SSI, SDI, 0x4100, 0        ; delete what 56h renamed
    ; --- THE DEVICE WORD'S DRIVE BITS, ACROSS A DRIVE (SPEC.md 96.7.1.2) ---
    ; Bits 0-5 are the drive the FILE is on, which is only the drive the
    ; program is STANDING on by coincidence - and every row above is that
    ; coincidence, both machines being on B: throughout.  So: stand on A:,
    ; open a file on B: BY NAME, and ask.  `B:REGS.COM` is on the gate disk
    ; under either machine, and the last row puts the drive back.
    ROW '0Ea', 0,      SBX, SCX, 0,      SSI, SDI, 0x0E00, 0        ; select A:
    ROW '3Dx', F_KEEP, SBX, SCX, n_bself, SSI, SDI, 0x3D00, 0       ; open B:REGS.COM
    ROW '44x', F_FH,   0,   SCX, SDX,    SSI, SDI, 0x4400, dw_cross ; ...whose drive is it?
    ROW '3Ex', F_FH,   0,   SCX, SDX,    SSI, SDI, 0x3E00, 0        ; close
    ROW '0Eb', 0,      SBX, SCX, 1,      SSI, SDI, 0x0E00, 0        ; ...and back to B:
    dw 0, 0, 0, 0, 0, 0, 0, 0, 0                                    ; ...AX = 0 ends it

; --- what AH=25h points INT 60h at.  Nothing calls it; the vector is the
; user-reserved one precisely so that nothing can, and the box banks the whole
; IVT across the bracket anyway (SPEC.md 96.13).
stub:
    iret

s_head:  db 'REGS 1 BCDSIPEG/CF', 13, 10, 0
s_dev:   db '44 devword: open=', 0
s_dev2:  db ' create=', 0
s_dev3:  db ' written=', 0
s_dev4:  db ' onB-from-A=', 0
s_done:  db 13, 10, 'REGS READY - press a key', 13, 10, 0
n_self:  db 'REGS.COM', 0
n_bself: db 'B:REGS.COM', 0
n_tmp:   db 'REGTMP.$$$', 0
n_tmp2:  db 'REGTMP2.$$$', 0
n_dir:   db 'REGDIR', 0
n_root:  db '\', 0                 ; ONE backslash: nasm does not process an
                                    ; escape in a single-quoted string, so '\\'
                                    ; is the two-character path DOS refuses -
                                    ; which read as `3Bb`, `3A` and `41` all
                                    ; failing on the reference and passing here
n_all:   db '*.*', 0
s_star:  db '*$'

cur:     dw 0
ncell:   dw 0
fh:      dw 0
flags:   db 0
i_ax:    dw 0
i_bx:    dw 0
i_cx:    dw 0
i_dx:    dw 0
i_si:    dw 0
i_di:    dw 0
i_es:    dw 0
o_ax:    dw 0
o_bx:    dw 0
o_cx:    dw 0
o_dx:    dw 0
o_si:    dw 0
o_di:    dw 0
o_bp:    dw 0
o_es:    dw 0
o_ds:    dw 0
o_fl:    dw 0
dw_open: dw 0
dw_new:  dw 0
dw_writ: dw 0
dw_cross: dw 0
fcb:     times 44 db 0
cwd:     times 68 db 0
buf:     times 16 db 'R'
dta:     times 128 db 0
