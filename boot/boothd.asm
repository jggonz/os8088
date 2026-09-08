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
;   1. THE GEOMETRY COMES FROM THE BPB, not from -DSPT/-DHEADS. A floppy build
;      knows its geometry because the Makefile builds one image per geometry;
;      a hard disk's belongs to the VOLUME, and BPB_SecPerTrk/BPB_NumHeads at
;      +24/+26 are what the formatter wrote there.
;
;      IT USED TO ASK int 13h AH=08h, reasoning that "we read with CHS calls
;      that THIS BIOS will service, so the geometry that matters is the one it
;      translates with". That is the wrong invariant and docs/FIELD-NOTES.md 33
;      is what it cost. int 13h TAKES CHS: the same conversion applied to the
;      write and to the read reaches the same sector, whatever the drive
;      privately believes - so the geometry that must be used is THE ONE THAT
;      WROTE THE VOLUME. DOS's boot sector reads the BPB for exactly this
;      reason, and so does os8088's own kernel: dsk_bpb_check loads
;      [disk_spt]/[disk_heads] from those two fields and dsk_xfer derives every
;      CHS from them. This sector was the only thing in the system using a
;      different source, so the system disagreed with itself about where its
;      own sectors were.
;
;      It is also EIGHT BYTES SMALLER than the probe it replaces, and it makes
;      a drive whose ROM will not answer AH=08h bootable - which such a drive
;      was not, while still being installable.
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
cpu 8086                        ; SPEC.md 1: an 8088 runs this before
                                ; anything has probed anything.

bits 16
org 0x7C00

KERNEL_SEG   equ 0x0060
STACK_TOP    equ 0x7C00
BOOT_STACK   equ 2048
RELOC_ADJ    equ 0x07E0         ; boot/boot.asm's, and for its reasons
; SPLASH_OFF and SPL_RESIDENT WERE HERE, and SPEC.md 2.9.9 is why they are not.
; This sector used to tick the loading screen itself, through a far entry
; pinned at KERNEL_SEG:0008. SPEC.md 2.9.4 moved that screen out of `.text`
; into stage 2's BLOB, so the offset is now the middle of an instruction -
; and the blob belongs at the heap's floor rather than at KERNEL_SEG.
;
; So this sector does what stage 2's handover does and nothing more: it loads
; the blob to HEAP_SEG, publishes the segment in [spl_fseg], and lets the
; KERNEL make the first call. Three constants come from the kernel's own build
; rather than being written here, the way KERNEL_SECTORS always has - BLOB_SEG,
; SPL_FSEG and BOOT2_SECS - because every one of them is a thing this file
; cannot know and must not guess.

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
bpb_spt:     dw 0                ; +24  the geometry THIS VOLUME WAS WRITTEN
bpb_heads:   dw 0                ; +26  WITH, and what we read it back with -
                                 ;      see note 1 above
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

    mov [boot_drive], dl        ; the MBR handed us the drive it came from,
                                ; and boot/mbr.asm clamps it to 0x80 or above
                                ; before its own first read (SPEC.md 2.9.11) -
                                ; so a ROM that never set DL is already dealt
                                ; with by the time we are running, and this
                                ; sector spends none of its 25 spare bytes on
                                ; asking the question twice

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

    ; --- the geometry the VOLUME WAS WRITTEN WITH, from its own BPB --------
    ; Note 1 above is the whole argument. What is left here is that the two
    ; fields have to be SANE before chs divides by them, and that there is
    ; nothing else on the disk to fall back to if they are not.
    mov ax, [bpb_spt]
    or ax, ax
    jz .nogeom                  ; a zero would divide by zero in chs
    mov [spt], ax
    mul word [bpb_heads]        ; AX = spt * heads, and DX catches a pair that
    or dx, dx                   ; multiplies past a word - 63 x 255 is 16,065,
    jnz .nogeom                 ; so anything over that is corrupt
    or ax, ax
    jz .nogeom                  ; heads = 0
    mov [spt_heads], ax

    ; **AND NO int 13h AH=08h AT ALL.** A range check against the drive's own
    ; answer - refuse when its tracks are shorter than the volume's - was
    ; written and measured at EIGHTEEN BYTES more than this sector has. What it
    ; would have bought is a better letter: such a drive fails its reads anyway
    ; and stops on 'D' three sectors in, which is a diagnosable end rather than
    ; a silent one. What dropping the call buys is the case in note 33 - a
    ; drive whose ROM will not answer is now bootable.
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
    mov [lba], bx               ; ...the FIRST sector of KERNEL.SYS, which is
                                ; the blob's, not `.text`'s: the two passes
                                ; below read the file straight through and
                                ; [lba] carries from one to the other
    mov [lba+2], si

    ; --- PASS 1: the blob, to the heap's floor (SPEC.md 2.9.5, 2.9.9) -------
    ; It is the first BOOT2_SECS sectors of the file and it is where the
    ; loading screen AND the boot overlay live, so a kernel that does not get
    ; it boots with no clock, no font, no SYSTEM.CFG and no drivers - looking,
    ; from the outside, like a machine that works.
    mov ax, BOOT2_SECS          ; AX = sectors, DX = where. In REGISTERS,
    mov dx, BLOB_SEG            ; because two `mov word [mem], imm` a pass is
    call load_all               ; six bytes this sector has not got
    ; --- PASS 2: `.text` onward, to KERNEL_SEG, which [lba] already names ---
    ; ...OR R PARAGRAPHS ABOVE IT, when the kernel is PACKED (SPEC.md
    ; 2.9.13.5). This sector has its own reader and never enters stage 2, so a
    ; compressed KERNEL.SYS read to KERNEL_SEG would simply be JUMPED INTO -
    ; and there are 23 spare bytes here to do something about that in. Five is
    ; what it takes, because the two loaders' arithmetic agrees: one run to
    ; KERNEL_SEG + R puts the plain head at the bottom of it and the packed
    ; blocks at KERNEL_SEG + R + head/16, which is exactly where the floppy's
    ; two separate reads put them. So the blob's own kz_hd moves the head down
    ; and expands, on the same addresses, with no second copy of anything.
    ;
    ; KZ_RPARA is an IMMEDIATE here where [ksecs] is patched, and that is the
    ; same assumption BLOB_SEG and SPL_FSEG already make: this sector ships
    ; inside HDD.DRV, built from the kernel it will boot.
    mov ax, [ksecs]
    sub ax, BOOT2_SECS
%ifdef KZIP
    mov dx, KERNEL_SEG + KZ_RPARA
    call load_all
    call BLOB_SEG:KZ_HD         ; ...and the blob puts it where it belongs
%else
    mov dx, KERNEL_SEG
    call load_all
%endif

    ; --- hand off -----------------------------------------------------------
    ; DL is the BIOS drive and BX:CX the partition base, which is what the
    ; kernel needs to make this volume a DVK_BIOS row it can read from before
    ; any driver exists (SPEC.md 52.10.3).
    mov ax, KERNEL_SEG
    mov es, ax
    mov [es:0x000C], bp         ; the boot timer, AFTER the load
    mov ax, BLOB_SEG            ; ...and WHERE THE BLOB IS, which is stage 2's
    mov [es:SPL_FSEG], ax       ; own last act before its jump and is what
                                ; unguards every SPLCALL in the kernel (SPEC.md
                                ; 2.9.5.3). The kernel makes the first call,
                                ; draws its own loading screen and gives these
                                ; bytes back at spl_finish. Through AX because
                                ; `mov word [es:mem], imm` is a byte dearer and
                                ; AX is dead the instant ES is loaded
    mov dl, [boot_drive]
    mov bx, [bpb_hidd]
    mov cx, [bpb_hidd+2]
    jmp KERNEL_SEG:0x0000

; -----------------------------------------------------------------------------
; load_all - read [left] sectors from [lba] to [dest_seg], as many runs as it
;            takes. Both passes above are this, and [lba] carries across them
;            because the blob and `.text` are consecutive in the file.
; -----------------------------------------------------------------------------
load_all:
    mov [left], ax
    mov [dest_seg], dx
.run:
    mov es, [dest_seg]          ; (BX is read_run's own business - it zeroes it
    call read_run               ; itself at every attempt)
    add [lba], ax               ; AX = sectors this call actually moved
    adc word [lba+2], 0
    mov cl, 5
    shl ax, cl                  ; ...32 paragraphs a sector, so the destination
    add [dest_seg], ax          ; follows the run...
    shr ax, cl                  ; ...and back to SECTORS, which costs two bytes
    sub [left], ax              ; against the five a `cmp word [left], 0` does:
    jnz .run                    ; this way the subtract's own ZF is the test
    ret

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
    mov di, ax                  ; ...and NEVER ZERO, which used to need a
                                ; branch: every destination here is 512-ALIGNED
                                ; (both passes start on a 32-paragraph boundary
                                ; and advance 32 a sector), so the remainder is
                                ; a multiple of 512 and the shift can only
                                ; reach 0 from an AX the `jz` above has taken.
                                ; boot/boot2.asm's read_run says the same
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
; ONE div pair and no 32/16 two-step, and the two guards that make it one.
; hd_chs (drivers/hdd/hdcom.inc) carries the same pair, and the header there
; calls them a proof - which is what they are for a geometry hd_geom_ok has
; already bounded. THIS routine's geometry is BYTES OFF A DISK (note 1), and
; the validation above accepts any non-zero spt/heads pair whose product fits
; a word: spt = 1, heads = 1 passes every test it makes. So the pair is
; ENFORCED here rather than assumed, on a sector 0 nobody in this system
; necessarily wrote:
;
;   cmp dx, cx    a 32-bit dividend over a tiny divisor overflows the div, and
;                 an INT 0 taken before the kernel exists lands on whatever
;                 POST left in the vector. 'G' is the letter the header
;                 promises for this class of BPB and it did not get it
;   cmp ax, 1024  read_run packs the cylinder into ten bits - [cylhi] shifted
;                 into CL bits 6..7 - so cylinder 1024 was issued as cylinder
;                 0. The read SUCCEEDS, 207 sectors of something else land at
;                 0060:0000, and this sector jumps into them: no letter, no
;                 'D', nothing
; -----------------------------------------------------------------------------
chs:
    mov ax, [lba]
    mov dx, [lba+2]
    mov cx, [spt_heads]
    cmp dx, cx                  ; the quotient would not fit a word
    jae .nogeom
    div cx                      ; AX = cylinder, DX = the rest
    cmp ax, 1024                ; ...and CHS has ten bits for it
    jae .nogeom
    mov [cyllo], al
    mov [cylhi], ah
    mov ax, dx
    xor dx, dx
    div word [spt]              ; AX = head, DX = sector - 1
    mov [head], al
    ret
.nogeom:
    jmp entry.nogeom            ; the 'G' refusal, shared with the validation
                                ; above - one letter, one meaning, and the
                                ; three bytes of a near jmp rather than a
                                ; second copy of it

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
;   G  the BPB's geometry is unusable - no sectors per track, no heads, a
;      spt x heads that does not fit a word, or a pair that puts KERNEL.SYS
;      past what CHS can name (a quotient over a word, or a cylinder at or
;      above 1024). Not recoverable: nothing else on the disk says where its
;      sectors are
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
