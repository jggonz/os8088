; DISKCOST.COM - what ONE open and ONE read cost the DRIVE.
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; It does a measured number of opens and reads of a named file and then waits
; for a key, so the HOST can bracket it with MartyPC's floppy-controller
; counters (os88marty.Marty.disk) and read the cost off from outside.  The
; same binary runs under a real DOS, which is the whole point: "we make seven
; times the int 13h calls" is a ratio nobody can act on, and "one open costs
; us N calls and DOS M" is a defect with an address.
;
; ASSEMBLE ONE PER POINT, and take the DIFFERENCE between two points rather
; than either number:
;
;   nasm -f bin -DNOPEN=0                        -o D0.COM   diskcost.asm
;   nasm -f bin -DNOPEN=1 -DNREAD=0              -o D1.COM   diskcost.asm
;   nasm -f bin -DNOPEN=4 -DNREAD=0              -o D2.COM   diskcost.asm
;   nasm -f bin -DNOPEN=1 -DNREAD=1  -DCHUNK=8192 -o D3.COM  diskcost.asm
;   nasm -f bin -DNOPEN=1 -DNREAD=4  -DCHUNK=8192 -o D4.COM  diskcost.asm
;
; Every run carries the same fixed cost - the shell or the file manager
; loading this program, the DOS box claiming its arena - so that cost cancels
; in (D2-D1)/3 and (D4-D3)/3 and neither has to be known.
;
; IT WAITS FOR A KEY at the end, and that is not a courtesy: under os8088 the
; fsx bracket ends when the program does and the desktop comes straight back,
; taking its own disk traffic with it.

    org 0x100
    cpu 8086

%ifndef NOPEN
%define NOPEN 1
%endif
%ifndef NREAD
%define NREAD 1
%endif
%ifndef CHUNK
%define CHUNK 8192
%endif

start:
%if NOPEN > 0
    mov word [opens], NOPEN
.oloop:
    mov ax, 0x3D00
    mov dx, name
    int 0x21
    jc .failed
    mov [fh], ax

%if NREAD > 0
    mov word [reads], NREAD
.rloop:
    mov bx, [fh]
    mov cx, CHUNK
    mov dx, buf
    mov ah, 0x3F
    int 0x21
    jc .failed
    or ax, ax
    jz .rdone                   ; end of file: a short run still counts, and
                                ; the host's own numbers say how short
    dec word [reads]
    jnz .rloop
.rdone:
%endif

    mov bx, [fh]
    mov ah, 0x3E
    int 0x21
    dec word [opens]
    jnz .oloop
%endif

    mov si, s_done
    call puts
    jmp short .wait
.failed:
    mov si, s_fail
    call puts
.wait:
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

puts:
    lodsb
    or al, al
    jz .o
    push si
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop si
    jmp short puts
.o:
    ret

s_done: db 'DISKCOST READY', 13, 10, 0
s_fail: db 'DISKCOST FAILED', 13, 10, 0
; THE FILE UNDER TEST.  Edit for the disk in the drive; it wants to be big
; enough that NREAD chunks of CHUNK fit inside it, or the run measures an
; end-of-file instead of a read.
name:   db 'BIG.DAT', 0
fh:     dw 0
opens:  dw 0
reads:  dw 0
; THE BUFFER IS NOT EMITTED.  A .COM owns every byte after its image, so
; `times CHUNK db 0` would only make the FILE bigger - and a bigger file costs
; more to LOAD, which is a confound in the one measurement this program
; exists to take: every point must cost the same to start.
buf:
