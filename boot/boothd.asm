; =============================================================================
; os8088 - the HARD DISK boot sector (SPEC.md 52.10.2)
;
; The volume boot record of an installed partition. The MBR (boot/mbr.asm)
; chain-loads it to 0000:7C00 with DL = the BIOS drive; from there its job is
; boot/boot.asm's job exactly - pull KERNEL.SYS off the volume into
; 0060:0000, ticking the splash, and jump to it.
;
; **THIS IS A SECOND SECTOR AND NOT A KNOB, AND THAT IS MEASURED.** The floppy
; sector's last non-zero byte is at offset 492, so there are seventeen bytes
; between it and the signature, and what a hard disk needs does not fit in
; seventeen bytes. SPEC.md 52.9 predicted a second variant and this is it.
;
; Three things differ from boot/boot.asm and NOTHING ELSE does:
;
;   1. THE GEOMETRY COMES FROM int 13h AH=08h, not from -DSPT/-DHEADS. A
;      floppy build knows its geometry because the Makefile builds one image
;      per geometry; a hard disk's belongs to the drive. And it must be the
;      BIOS's answer rather than the BPB's, which is the subtle half: we read
;      with CHS calls that THIS BIOS will service, so the geometry that
;      matters is the one it translates with. A disk formatted on a machine
;      that reported 615/4/17 and moved to a controller that translates
;      977/5/17 has a correct BPB and a useless one - the sectors are where
;      the running BIOS says they are.
;   2. EVERY LBA HAS THE PARTITION BASE ADDED, from BPB_HiddSec at offset 28.
;      The formatter has written that field since it was written
;      (drivers/hdd/fmt.inc), commented "DOS reads it, os8088 does not". It
;      does now. Taking it from our own BPB rather than from the MBR's DS:SI
;      is deliberate: a sector that can only boot when chain-loaded by its own
;      MBR cannot be tested on its own.
;   3. IT DOES NOT TOUCH int 1Eh. SPEC.md 18.92's diskette parameter table is
;      the floppy controller's EOT and means nothing here.
;
; What is deliberately NOT different is the part that looks like it should be:
; the data-area arithmetic is already generic over FAT12 and FAT16, because it
; is computed from BPB_RsvdSecCnt, BPB_NumFATs, BPB_FATSz16 and
; BPB_RootEntCnt rather than from a format. A FAT16 partition needs no new
; code in it at all.
;
; And KERNEL.SYS must still be FIRST and CONTIGUOUS from cluster 2, because
; this reads it as a flat run rather than walking a cluster chain - which is
; what makes it fit in 512 bytes (SPEC.md 19.3). os88disk.py guarantees that
; on a floppy at image-build time; the installer guarantees it here by
; formatting the partition and writing KERNEL.SYS before anything else
; (SPEC.md 52.10.4).
;
; Assembled with -DKERNEL_SECTORS=<n> by the Makefile, exactly as the floppy
; sector is.
; =============================================================================

bits 16
org 0x7C00

KERNEL_SEG   equ 0x0060
STACK_TOP    equ 0x7C00
BOOT_STACK   equ 2048
RELOC_ADJ    equ 0x07E0         ; boot/boot.asm's, and for its reasons
SPLASH_OFF   equ 0x0008
SPL_RESIDENT equ 8              ; THE THIRD MIRROR of this constant, and the
                                ; one no assertion can see: splash.inc's
                                ; %if compares the module against the value
                                ; IT holds, and boot/boot.asm's copy is the
                                ; one anybody raising it notices. Left behind
                                ; at 7 this sector ticks the splash a sector
                                ; EARLY - into a module that is not aboard -
                                ; which is precisely the failure that
                                ; assertion exists to prevent, on the one
                                ; path it cannot check. Raise all three.

BPB_END      equ 62

    jmp short entry
    nop
bpb_oem:     times 8 db 0        ; +3
bpb_bps:     dw 0                ; +11
bpb_spc:     db 0                ; +13
bpb_rsvd:    dw 0                ; +14
bpb_nfat:    db 0                ; +16
bpb_rootent: dw 0                ; +17
bpb_tot16:   dw 0                ; +19
bpb_media:   db 0                ; +21
bpb_fatsz:   dw 0                ; +22
bpb_spt:     dw 0                ; +24  the FORMATTER's geometry: read by DOS,
bpb_heads:   dw 0                ; +26  and NOT by us - see note 1 above
bpb_hidd:    dd 0                ; +28  the partition base. THIS one we read
    times BPB_END - ($ - $$) db 0

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, STACK_TOP

    mov bp, [0x046C]            ; the boot timer's t=0 (SPEC.md 15.4)

    ; --- relocate: same offset, new segment (SPEC.md 2.7) -------------------
    mov si, STACK_TOP
    mov di, STACK_TOP
%ifdef RAM_KB
    mov ax, RAM_KB
%else
    int 0x12
%endif
    mov cl, 6
    shl ax, cl
    sub ax, RELOC_ADJ
    mov bx, [ksecs]             ; where the kernel's own read would END...
    mov cl, 5
    shl bx, cl                  ; ...32 paragraphs a sector...
    add bx, KERNEL_SEG + BOOT_STACK/16      ; ...clear of our stack too
    cmp ax, bx
    jb .nomem
    mov es, ax
    mov cx, 256
    cld
    rep movsw
    push es
    mov ax, .moved
    push ax
    retf
.nomem:
    mov al, 'R'
    jmp fail
.moved:
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, STACK_TOP
    sti
    cld

    mov [boot_drive], dl        ; the MBR handed us the drive it came from

    ; NO VIDEO SETUP, which the floppy sector can afford and this one cannot.
    ; boot/boot.asm reads the equipment word and sets mode 3 or mode 7 for a
    ; clean, cursorless screen while the kernel lands; that costs fifteen
    ; bytes, and every one of them is spent here on the geometry probe and
    ; 32-bit arithmetic a hard disk needs.
    ;
    ; Nothing is lost but tidiness. The BIOS has just run POST and left a text
    ; mode appropriate to the card, which is all int 10h AH=0Eh needs for the
    ; one failure letter below; and the kernel's own vid_setmode takes the
    ; screen in graphics as soon as the splash is resident (SPEC.md 15.3),
    ; which on a hard disk is a fraction of a second. What shows until then is
    ; the POST's own text rather than a cleared screen.

    ; --- the drive's geometry, from the BIOS (note 1) -----------------------
    ; AH=08h answers CL bits 0..5 = sectors per track, DH = the last head, and
    ; it is the only honest source: these are the numbers the CHS reads below
    ; will be serviced with. A drive that refuses the call cannot be read from
    ; by this sector at all, so it is a hard stop rather than a fallback to
    ; the BPB's - a wrong geometry reads the wrong sectors and says nothing.
    mov ah, 0x08
    mov dl, [boot_drive]
    int 0x13
    jc .nogeom
    and cl, 0x3F                ; sectors per track
    xor ch, ch
    jcxz .nogeom                ; a zero would divide by zero below
    mov [spt], cx
    mov al, dh
    xor ah, ah
    inc ax                      ; heads = last head + 1
    jz .nogeom                  ; DH = 0xFF is not a geometry
    mul cx                      ; AX = spt * heads, and it fits: 63 * 256
    mov [spt_heads], ax
    jmp short .geom_ok
.nogeom:
    mov al, 'G'
    jmp fail
.geom_ok:

    xor ah, ah                  ; reset the drive before the first read
    mov dl, [boot_drive]
    int 0x13

    ; --- KERNEL.SYS's first sector, as an ABSOLUTE LBA ----------------------
    ; The data area, by the BPB's own arithmetic (SPEC.md 19.3):
    ;     rsvd + nfat * fatsz + ceil(rootent * 32 / 512)
    ; ...plus the partition base, which is what makes it absolute (note 2).
    mov al, [bpb_nfat]
    xor ah, ah
    mul word [bpb_fatsz]        ; DX:AX - a FAT16 volume's FATs can exceed a
                                ; word, so the high half is kept this time
    add ax, [bpb_rsvd]
    adc dx, 0
    mov bx, ax
    mov si, dx                  ; SI:BX = rsvd + both FATs
    mov ax, [bpb_rootent]
    add ax, 15
    mov cl, 4
    shr ax, cl                  ; the root directory's sectors
    add bx, ax
    adc si, 0
    add bx, [bpb_hidd]          ; ...and the partition base
    adc si, [bpb_hidd+2]
    mov [lba], bx
    mov [lba+2], si
    mov ax, [ksecs]
    mov [left], ax

.load_next:
    mov es, [dest_seg]
    xor bx, bx
    call read_run               ; AX = sectors this call actually moved
    add [lba], ax
    adc word [lba+2], 0
    sub [left], ax
    mov cl, 5
    shl ax, cl
    add [dest_seg], ax

    mov ax, [ksecs]             ; sectors DONE, derived rather than counted:
    sub ax, [left]              ; one fewer word of state, and this sector
    cmp ax, SPL_RESIDENT        ; has none to spare
    jb .no_tick
    mov dx, [ksecs]
    call KERNEL_SEG:SPLASH_OFF
.no_tick:
    cmp word [left], 0
    jne .load_next

    ; --- hand off -----------------------------------------------------------
    ; DL is the BIOS drive and BX:CX the partition base, which is what the
    ; kernel needs to make this volume a DVK_BIOS row it can read from before
    ; any driver exists (SPEC.md 52.10.3).
    mov ax, KERNEL_SEG
    mov es, ax
    mov [es:0x000C], bp         ; the boot timer, AFTER the load
    mov dl, [boot_drive]
    mov bx, [bpb_hidd]
    mov cx, [bpb_hidd+2]
    jmp KERNEL_SEG:0x0000

; -----------------------------------------------------------------------------
; read_run - read as many sectors from [lba] to ES:0000 as one int 13h may
;            carry. Three attempts, resetting between them.
; in:  ES = destination, [left] = sectors still wanted
; out: AX = sectors transferred, 1..run
; clobbers: ax, bx, cx, dx, si, di
;
; The same three bounds boot/boot.asm applies (SPEC.md 18.93) - the track, the
; sectors wanted, and the 64KB DMA page - because all three are properties of
; int 13h and not of the medium. What differs is only that the LBA is 32-bit
; and the cylinder can exceed 255, so it is packed into CL bits 7:6.
; -----------------------------------------------------------------------------
read_run:
    call chs                    ; DX = the sector's index within its track
    mov di, [spt]
    sub di, dx                  ; DI = sectors to the end of this track
    cmp di, [left]
    jbe .page
    mov di, [left]
.page:
    mov ax, es
    mov cl, 4
    shl ax, cl
    neg ax                      ; bytes to the end of the 64KB page
    jz .have
    mov cl, 9
    shr ax, cl
    cmp ax, di
    jae .have
    or ax, ax
    jz .one
    mov di, ax
    jmp short .have
.one:
    mov di, 1
.have:
    mov [run], di
    mov si, 3

.attempt:
    call chs                    ; rebuilt every attempt: a controller reset
                                ; owes us nothing
    inc dx                      ; DX = sector, 1-based
    mov al, [cylhi]             ; cylinder bits 9:8 ride in CL bits 7:6 - the
    mov cl, 6                   ; standard CHS packing, and the count goes
    shl al, cl                  ; through CL because an 8086 has no
                                ; `shl reg, imm` other than 1
    or al, dl
    mov cl, al                  ; CL = sector | cylinder high
    mov ch, [cyllo]
    mov dh, [head]
    xor bx, bx
    mov dl, [boot_drive]
    mov ax, [run]
    mov ah, 0x02
    int 0x13
    jnc .done
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec si
    jnz .attempt
    mov al, 'D'
    jmp fail
.done:
    mov ax, [run]               ; CF=0 IS the BIOS saying the whole request
    ret                         ; completed, and AL is not (SPEC.md 18.91)

; -----------------------------------------------------------------------------
; chs - [lba] as cylinder / head / sector for this drive's geometry
; out: [cyllo], [cylhi], [head]; DX = the sector's 0-based index in its track
; clobbers: ax, cx, dx
;
; ONE div pair and no 32/16 two-step, on hd_chs's proof (SPEC.md 52.10.3): the
; highest LBA a CHS call can name puts the cylinder under 1024, so dividing by
; spt*heads leaves a quotient far inside a word and DX below the divisor.
; -----------------------------------------------------------------------------
chs:
    mov ax, [lba]
    mov dx, [lba+2]
    mov cx, [spt_heads]
    div cx                      ; AX = cylinder, DX = the rest
    mov [cyllo], al
    mov [cylhi], ah
    mov ax, dx
    xor dx, dx
    div word [spt]              ; AX = head, DX = sector - 1
    mov [head], al
    ret

; -----------------------------------------------------------------------------
; fail - print AL and stop. Three ways to get here and each names itself with
; ONE LETTER, because a dark screen is what the MBR's old stub existed to
; prevent and because three words do not fit - this sector has 512 bytes and
; a hard disk's geometry probe and 32-bit arithmetic to pay for out of them.
; The same trade BOOTDIAG=1 makes on the floppy side, which spends its four
; spare bytes on two hex digits (SPEC.md 18.94):
;
;   R  int 12h says this machine has too little RAM to hold the kernel
;      clear of this sector's own relocated stack (SPEC.md 2.7)
;   G  the drive would not answer int 13h AH=08h, so there is no geometry to
;      issue CHS reads with. Not recoverable: the BPB's geometry is the
;      formatter's belief and this BIOS's translation is what the reads obey
;   D  three attempts at one run of sectors all failed, reset between each
; -----------------------------------------------------------------------------
fail:
    mov ah, 0x0E
    xor bx, bx                  ; page 0; BL is the colour in graphics modes
    int 0x10                    ; only, and we set a text mode above
.halt:
    hlt                         ; not `jmp $`: a stray IRQ should not turn a
    jmp short .halt             ; halted machine into a spinning one

boot_drive:  db 0x80
dest_seg:    dw KERNEL_SEG
lba:         dd 0
left:        dw 0
run:         dw 0
spt:         dw 0
spt_heads:   dw 0
cyllo:       db 0
cylhi:       db 0
head:        db 0

; --- the one word the INSTALLER patches, at a FIXED offset ---------------------
; The floppy sector takes KERNEL_SECTORS as a -D at build time, because the
; Makefile measures the kernel it is about to put on the image. This sector is
; built long before it knows which kernel it will boot - it ships inside
; HDD.DRV - so the count is a word the installer writes when it writes the
; VBR, having just written KERNEL.SYS and knowing exactly how big it was
; (SPEC.md 52.10.4).
;
; PINNED AT OFFSET 508, immediately below the signature, and that is the whole
; reason it is placed by hand rather than left with the other variables: a
; patch site the driver locates by a documented constant cannot drift when
; this file is edited, and four separate immediates - which is what the count
; used to be - could not have been patched at all.
BOOTHD_KSECS equ 508

%if $ - $$ > BOOTHD_KSECS
%error "the hard disk boot sector does not fit 512 bytes"
%endif
    times BOOTHD_KSECS - ($ - $$) db 0
ksecs:       dw 0
    dw 0xAA55
