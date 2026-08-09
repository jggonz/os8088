; =============================================================================
; os8088 - the master boot record's boot code (SPEC.md 52.10.1)
;
; 446 bytes at the front of LBA 0, in front of the four partition entries.
; The BIOS loads the whole sector to 0000:7C00 and jumps here with DL set to
; the drive it came from; this finds the active partition, loads ITS first
; sector to the same address and hands over.
;
; It replaces a stub that printed `Not bootable` and halted, which was the
; truth for as long as os8088 could not boot from a hard disk (SPEC.md 52.9,
; retired). The stub was hand-assembled bytes in drivers/hdd/part.inc; this
; is assembled by nasm and `incbin`ed there instead, because a chain-loader is
; too much to write as a `db` list and far too much to review as one.
;
; **WE MOVE OUT OF THE WAY FIRST**, and that is the whole reason this file has
; any structure at all. The volume boot record we are about to load wants
; 0000:7C00 - it is a boot sector, and boot sectors are written for the
; address the BIOS uses - so we cannot still be sitting there when it lands.
; The 512 bytes are copied to 0000:0600 and execution continues there, which
; is where every DOS-era MBR goes and is safe for the same reason
; boot/boot.asm's own relocation target is: it is above the BIOS data area
; and below anything the loaded sector will touch.
;
; The copy keeps the SAME OFFSET WITHIN THE SEGMENT it is assembled for, so
; unlike boot/boot.asm - which relocates by segment and keeps org 0x7C00 -
; this one is assembled at org 0x0600 and jumps to a label that is only
; correct after the move. Everything before that jump is position-independent
; by inspection: it touches no label.
;
; What we hand the volume boot record is the era convention, and
; boot/boothd.asm reads none of it - it takes its partition base from its own
; BPB_HiddSec (SPEC.md 52.10.2) rather than from us, because a sector that
; can only boot when chain-loaded by its own MBR is a sector that cannot be
; tested on its own. It is set anyway, because other people's boot sectors
; expect it and a disk this tool partitions should chain-load them too:
;
;   DL    = the BIOS drive number, untouched from what we were given
;   DS:SI = the 16-byte partition entry we booted
;
; Two failures print and halt rather than falling through into whatever
; happens to be in memory: no active entry, and a read that would not
; complete. The old stub existed because 446 blank bytes is a machine that
; hangs with no explanation, and that reasoning outlives it.
; =============================================================================

bits 16
org 0x0600

LOAD_AT      equ 0x7C00         ; where the BIOS put us, and where the volume
                                ; boot record must end up
RELOC_AT     equ 0x0600         ; ...and where we continue from
TBL          equ 0x01BE         ; the partition table's offset in the sector
ENT_SZ       equ 16
NENT         equ 4
STATUS_ACT   equ 0x80
RETRIES      equ 3

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, LOAD_AT             ; a stack below the address we are clearing
    cld
    sti

    ; --- move ourselves to 0000:0600 ----------------------------------------
    ; Nothing above this line touches a label, so it runs correctly at the
    ; 0x7C00 the BIOS loaded us at. The far jump below is the first thing that
    ; needs the org to be true.
    mov si, LOAD_AT
    mov di, RELOC_AT
    mov cx, 256
    rep movsw
    jmp 0x0000:moved

moved:
    ; --- find the active entry ----------------------------------------------
    ; Exactly one status byte should be 80h. We take the FIRST and do not
    ; police the rest: a table with two actives is a table somebody else's
    ; tool wrote, and refusing to boot a disk that another MBR would boot is
    ; a worse answer than picking the same one it would.
    mov si, RELOC_AT + TBL
    mov cx, NENT
.scan:
    cmp byte [si+HP_STATUS], STATUS_ACT
    je .found
    add si, ENT_SZ
    loop .scan
    mov si, msg_noact
    jmp fail

.found:
    ; SI -> the entry, and it stays there: it is what we hand over in DS:SI.
    ; The CHS in the entry is what we read with, not the LBA - this is an
    ; 8088-era disk with an option ROM answering int 13h (SPEC.md 52.1's rung
    ; 0), and AH=02h takes CHS. The entry carries both and they describe the
    ; same sector, so using the one the interface wants costs no arithmetic
    ; and no assumption about the drive's geometry.
    mov dh, [si+HP_SHEAD]
    mov cx, [si+HP_SSEC]        ; CL = sector | cylinder high, CH = cyl low -
                                ; the entry stores the two bytes in exactly
                                ; the order int 13h wants them in CX
    mov bx, LOAD_AT
    mov di, RETRIES
.read:
    mov ax, 0x0201              ; AH=02 read, AL=1 sector
    push cx                     ; int 13h is entitled to spend any of these
    push dx
    push di
    int 0x13
    pop di
    pop dx
    pop cx
    jnc .loaded
    xor ah, ah                  ; reset the controller and try again: a first
    push cx                     ; read off a drive that has just spun up
    push dx                     ; fails often enough to be worth three goes,
    push di                     ; which is boot/boot.asm's own reasoning
    int 0x13
    pop di
    pop dx
    pop cx
    dec di
    jnz .read
    mov si, msg_ioerr
    jmp fail

.loaded:
    ; A volume boot record must carry the signature, and checking it is what
    ; turns "the active partition has never been formatted" from a jump into
    ; unformatted space into a message. The read succeeded, so this is a
    ; statement about the CONTENT and deserves its own answer.
    cmp word [LOAD_AT + 510], 0xAA55
    jne .nosig

    ; DL is still the BIOS drive - nothing above changed it - and SI still
    ; points at the entry we chose.
    jmp 0x0000:LOAD_AT

.nosig:
    mov si, msg_nosig
    ; fall through

; -----------------------------------------------------------------------------
; fail - print DS:SI and stop. There is nowhere to return to.
; -----------------------------------------------------------------------------
fail:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp short fail
.halt:
    hlt                         ; not `jmp $`: an NMI or a stray IRQ should
    jmp short .halt             ; not turn a halted machine into a spinning
                                ; one, so the loop is round the hlt

HP_STATUS    equ 0              ; the entry fields this code reads, named
HP_SHEAD     equ 1              ; rather than open-coded. part.inc carries the
HP_SSEC      equ 2              ; same equs for the driver side; these three
                                ; are all that is read here

msg_noact:   db 'No active partition', 13, 10, 0
msg_ioerr:   db 'Disk error', 13, 10, 0
msg_nosig:   db 'Not bootable', 13, 10, 0

; The code must fit in front of the partition table. Asserted rather than
; trusted: overrunning it corrupts the table this sector exists to describe,
; and the failure is a disk that stops mounting rather than a build error.
%if $ - $$ > TBL
%error "MBR boot code overruns the partition table"
%endif

    times TBL - ($ - $$) db 0
