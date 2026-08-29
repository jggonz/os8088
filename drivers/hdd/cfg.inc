; =============================================================================
; os8088 - HDD.DRV / cfg.inc
;
; The driver's own settings, carried inside SYSTEM.CFG (SPEC.md 51.9/52.6) as
; an opaque 34-byte blob the kernel moves and never reads. It holds two things,
; and both are things the driver cannot re-derive:
;
;   - the geometry of a drive the PROBE could not determine and the user typed
;     in. Losing it means typing it again at every boot.
;   - which drives were MOUNTED. A hard disk the user mounted is a hard disk
;     the user expects to find on the desktop next time.
;
; **It rides SYSTEM.CFG because the boot already reads that file.** A file of
; its own cost a second directory search and a second read - and worse, the
; volume dance around them: every file slot resolves in the current volume and
; directory (SPEC.md 19.2), so saving from a session that had just mounted C:
; meant OSAPI_FILE_HERE, an OSAPI_FILE_GOTO back to A: - which is a full
; REMOUNT - the read, and another remount to put the volume back. Two remounts
; and a read, at every boot, for 34 bytes that were about to travel next to
; SYSTEM.CFG's other 40. OSAPI_DRV_CFG is one memcpy.
;
; What the kernel gives up for it is only that DRV_BLOB_SZ is reserved on every
; machine, loaded driver or not - 34 bytes of .bss and 38 of the file. What it
; does NOT give up is the separation: the kernel knows this key's name and its
; length and nothing whatever about its contents, so every byte below is still
; the driver's business, and a machine that never loads HDD.DRV round-trips the
; blob untouched instead of dropping it the way SPEC.md 51.5 rule 1 drops a key
; it does not know.
;
;   +0   db  version       HDC_VER; anything else (0 included, which is what a
;                          blob nobody has written reads back as) = defaults
;   +1   db  count         devices described below, 0..HD_MAXDEV
;   +2   count x HDC_ESZ:
;          db kind         HDK_*      } the three that IDENTIFY a device
;          db unit         80h/81h, or 0/1 on its channel
;          db base>>4      the IDE task file's base port, 0 for a BIOS drive.
;                          Every ISA task file is 16-byte aligned, so the shift
;                          is lossless and the port fits a byte
;          db flags        bit 0 = mount it at boot, bit 1 = the geometry
;                          below was TYPED IN rather than probed
;          dw cyl
;          db heads        1..255 }  a word each in the device row, because
;          db spt          1..63  }  that is what the arithmetic wants
;
; Eight bytes a record and four records, so the whole thing is 34 - which is
; where DRV_BLOB_SZ comes from, and the reason the kernel's constant is the
; size of what the disk driver needs rather than a round number.
;
; **A device is matched by kind+unit+base, never by its row index.** The probe
; runs again at every attach and a machine can gain or lose a drive between
; boots; an index would then restore one drive's geometry onto another's, and
; mount a partition table that belongs to somebody else.
; =============================================================================

HDC_VER      equ 1
HDC_COUNT    equ 1
HDC_ENT0     equ 2
HDC_E_KIND   equ 0
HDC_E_UNIT   equ 1
HDC_E_BASE   equ 2              ; the port >> 4
HDC_E_FLAGS  equ 3
HDC_E_CYL    equ 4
HDC_E_HEADS  equ 6
HDC_E_SPT    equ 7
HDC_ESZ      equ 8
HDC_F_MOUNT  equ 1              ; bit 0: this drive was mounted
HDC_F_TYPED  equ 2              ; bit 1: the user typed this geometry in, so
                                ; it is the one to restore (SPEC.md 52.6)
HDC_FBUF     equ HDC_ENT0 + HD_MAXDEV * HDC_ESZ     ; 34 = DRV_BLOB_SZ

%if HDC_FBUF != DRV_BLOB_SZ
%error "HDD.DRV's settings blob must be exactly DRV_BLOB_SZ bytes"
%endif

; -----------------------------------------------------------------------------
; hd_cfg_mark - hand the current state to the kernel. NO DISK I/O.
; in:  UI-task context (a Control Panel click), or DRVV_READY
; out: nothing - the store cannot fail for a blob of the kernel's own size
; clobbers: flags
;
; **This is the only way out, and that is the rule rather than a shortage of
; ideas** (SPEC.md 31.8/51.9). The blob is staged into the kernel's settings
; struct on the spot - a 34-byte rep movsb - and SYSTEM.CFG is written when
; the Control Panel closes or Special > Restart is picked, which is where
; every other setting in the machine is written and for the same reason: the
; write is a mount of A:, a data write, two FAT flushes, a directory write and
; a coherence remount, 2+ seconds of frozen UI on the floor machine, landing
; in the middle of a click.
;
; Mount and Unmount used to write on the spot (the retired verb 2), and the
; argument for it - a machine switched off with the panel open comes back
; without the drive it was told to mount - was the same argument every page
; makes for being the exception. It also cost more than it looked: the write
; remounts A: to get at the file, so a mount click silently left A: current
; immediately after making C: current.
;
; No volume dance and no file name: the kernel already owns the file and the
; drive it lives on.
; -----------------------------------------------------------------------------
hd_cfg_mark:
    push ax
    push bx
    push cx
    push si
    push es
    call hd_cfg_build           ; CX = the blob's length (always HDC_FBUF)
    push ds
    pop es
    mov si, hd_cfgbuf
    mov al, 1                   ; set: stored now, on the disk at teardown
    call OSAPI_DRV_CFG
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_cfg_build - lay the blob out in hd_cfgbuf (module internal)
; in:  nothing
; out: CX = its length
; clobbers: CX (the output), flags
;
; A device earns a record if its geometry was typed in OR it is mounted -
; there is nothing to remember about a drive the probe describes correctly and
; the user has not mounted. The blob is zero-filled first, so the records that
; are not written are not last session's.
; -----------------------------------------------------------------------------
hd_cfg_build:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, hd_cfgbuf
    mov cx, HDC_FBUF
    xor al, al
    rep stosb
    mov di, hd_cfgbuf
    mov al, HDC_VER
    stosb
    xor al, al
    stosb                       ; the count, filled in at the end
    mov byte [hd_cfgi], 0
    mov byte [hd_cfgn], 0
.dev:
    mov bl, [hd_cfgi]
    cmp bl, [hd_ndev]
    jae .done
    xor dl, dl
    call hd_dev_mounted         ; BL = the index
    jc .nomount
    mov dl, HDC_F_MOUNT
.nomount:
    mov al, [hd_cfgi]
    push di                     ; DI is the WRITE CURSOR and hd_dev_row
    call hd_dev_row             ; answers in it, so the two have to take
    mov bx, di                  ; turns - BX carries the row from here
    pop di
    test byte [bx+HDD_FLAGS], 2 ; typed in by hand?
    jz .probed
    or dl, HDC_F_TYPED          ; ...and the RECORD has to say so, or the
    jmp short .write            ; restore cannot tell a geometry the user
.probed:                        ; typed from one the probe answered with
    test dl, dl
    jz .next                    ; probed geometry and not mounted: there is
                                ; nothing about this drive worth a record
.write:
    mov al, [bx+HDD_KIND]
    stosb
    mov al, [bx+HDD_UNIT]
    stosb
    mov ax, [bx+HDD_BASE]       ; a 16-byte-aligned port fits a byte shifted
    mov cl, 4                   ; down four - 8086: a variable shift only
    shr ax, cl                  ; goes through CL
    stosb
    mov al, dl
    stosb
    mov ax, [bx+HDD_CYL]
    stosw
    mov al, [bx+HDD_HEADS]      ; 1..255 and 1..63: the high byte of each is
    stosb                       ; zero by hd_geom_ok's own limits
    mov al, [bx+HDD_SPT]
    stosb
    inc byte [hd_cfgn]
.next:
    inc byte [hd_cfgi]
    jmp short .dev
.done:
    mov al, [hd_cfgn]
    mov [hd_cfgbuf+HDC_COUNT], al
    mov cx, HDC_FBUF            ; always the whole blob: the kernel's key has
                                ; one length and a short write would be a
                                ; record the file no longer carries
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_dev_mounted - does device index BL have a volume up? (module internal)
; in:  BL = the device index
; out: CF = 0 yes, CF = 1 no
; clobbers: flags
; -----------------------------------------------------------------------------
hd_dev_mounted:
    push ax
    push cx
    push si
    mov cx, HD_MAXVOL
    mov si, hd_vols
.scan:
    cmp byte [si+HDV_USED], 0
    je .next
    mov al, [si+HDV_DEV]
    cmp al, bl
    je .yes
.next:
    add si, HDV_SIZE
    loop .scan
    pop si
    pop cx
    pop ax
    stc
    ret
.yes:
    pop si
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_cfg_load - read the blob back out of the kernel and apply it (52.6)
; in:  the probe has run; DRVV_READY context, so the API answers us
; out: nothing; geometry restored and [hd_wantmnt] set for the drives to mount
; clobbers: flags
;
; A blob nobody has written reads back as zeroes and a zero version is not
; HDC_VER, so a machine that has never saved gets the defaults - the probe's
; own answers and nothing mounted - and never an error.
; -----------------------------------------------------------------------------
hd_cfg_load:
    push ax
    push bx
    push cx
    push si
    push es

    mov byte [hd_wantmnt], 0
    push ds
    pop es
    mov si, hd_cfgbuf
    mov cx, HDC_FBUF
    xor al, al                  ; get
    call OSAPI_DRV_CFG
    jc .out                     ; not published: impossible here, but the
                                ; answer is the defaults either way
    cmp cx, HDC_ENT0
    jb .out
    cmp byte [hd_cfgbuf], HDC_VER
    jne .out                    ; another version's shape: the defaults, and
                                ; the next save rewrites it in this one

    mov cl, [hd_cfgbuf+HDC_COUNT]
    xor ch, ch
    jcxz .out
    cmp cl, HD_MAXDEV
    ja .out
    mov si, hd_cfgbuf + HDC_ENT0
.rec:
    push cx
    call hd_cfg_apply
    add si, HDC_ESZ
    pop cx
    loop .rec
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_cfg_apply - one saved record onto the device it describes (internal)
; in:  SI = the record
; out: the matching row's geometry restored, and its bit set in [hd_wantmnt]
; clobbers: flags
; -----------------------------------------------------------------------------
hd_cfg_apply:
    push ax
    push bx
    push cx
    push di
    xor bl, bl
.scan:
    cmp bl, [hd_ndev]
    jae .out
    mov al, bl
    call hd_dev_row
    mov al, [si+HDC_E_KIND]     ; matched by WHAT IT IS, never by where it sat
    cmp al, [di+HDD_KIND]       ; in the table last time (see the header)
    jne .next
    mov al, [si+HDC_E_UNIT]
    cmp al, [di+HDD_UNIT]
    jne .next
    mov al, [si+HDC_E_BASE]     ; the stored port, back up four bits
    xor ah, ah
    mov cl, 4
    shl ax, cl
    cmp ax, [di+HDD_BASE]
    jne .next
    ; --- the geometry, and ONLY when the probe could not answer -------------
    ; This used to restore it unconditionally, reasoning that "a saved record
    ; only exists when the user typed it in or mounted with it, and either way
    ; it is the one that worked". That is true on ONE machine and false the
    ; moment the file travels - and a BIOS drive's match key is
    ; kind=BIOS/unit=80h/base=0, which is the SAME KEY ON EVERY MACHINE THERE
    ; IS. So the geometry typed on one box was restored, silently and with
    ; full confidence, onto a different box's drive.
    ;
    ; What that costs is not a refusal: the row now disagrees with int 13h, so
    ; hd_chs turns every LBA into a CHS the BIOS resolves to a DIFFERENT
    ; physical sector, and the partitioner, the formatter and the installer all
    ; write a whole volume somewhere it was never meant to go. Nothing errors.
    ; boot/boothd.asm then asks int 13h AH=08h - the truth - and reads the
    ; sectors the install BELIEVED it was writing, which hold either nothing or
    ; the previous install (docs/FIELD-NOTES.md 33).
    ;
    ; The probe's answer is a fact about the drive in front of us and the saved
    ; one is a fact about wherever this file was written, so the probe wins
    ; whenever it has one. That is also what this file's own header says the
    ; record is FOR - "the geometry of a drive the PROBE could not determine
    ; and the user typed in". The MOUNT bit below is untouched: which drives to
    ; mount is a preference and travels fine; a geometry is not and does not.
    ;
    ; UNLESS THE RECORD SAYS THE USER TYPED IT. The +/- editor is live on every
    ; drive, probed or not (page.inc: hd_page_adjust has no such gate), so
    ; "the probe answered" is not the same fact as "nobody typed one" - and
    ; reading it as if it were threw a typed geometry away at every boot and
    ; then wrote the probe's back over it. HDC_F_TYPED is the fact itself,
    ; recorded by hd_cfg_build off HDD_FLAGS bit 1. The field record that
    ; travelled between the 5150s carried flags=01 - mounted, not typed - so it
    ; still loses to the probe and docs/FIELD-NOTES.md 33 stays fixed.
    test byte [si+HDC_E_FLAGS], HDC_F_TYPED
    jnz .geom                   ; the user's own answer wins...
    test byte [di+HDD_FLAGS], 1 ; ...and where there is none, the probe's does
    jnz .nogeom
.geom:
    mov ax, [si+HDC_E_CYL]
    mov [di+HDD_CYL], ax
    mov al, [si+HDC_E_HEADS]
    xor ah, ah
    mov [di+HDD_HEADS], ax
    mov al, [si+HDC_E_SPT]
    xor ah, ah
    mov [di+HDD_SPT], ax
    and byte [di+HDD_FLAGS], 0xFC
    call hd_geom_ok
    jc .nogeom
    or byte [di+HDD_FLAGS], 3   ; bit 0 usable, AND BIT 1 - "not the BIOS's".
                                ; It used to set bit 0 alone, which made a
                                ; restored geometry indistinguishable from a
                                ; probed one the moment it landed. ONE THING
                                ; turns on telling them apart: the save above
                                ; drops any record that is "probed and not
                                ; mounted", so a restored geometry on an
                                ; unmounted drive used to erase itself at the
                                ; next write.
                                ; IT USED TO CITE A SECOND REASON - that
                                ; boot/boothd.asm has ONLY int 13h AH=08h, so a
                                ; geometry that did not come from that call is
                                ; one the loader can never reproduce. That
                                ; sentence is void: the loader reads the BPB now
                                ; and issues no AH=08h at all (SPEC.md 52.10.2),
                                ; and the geometry it reproduces is the one that
                                ; WROTE the volume - see docs/FIELD-NOTES.md 33,
                                ; which is what moved it. The flag stays,
                                ; because the save path above justifies it on
                                ; its own
    call hd_geom_push           ; the drive has to be told too: this runs before
                                ; the first paint, so a restored geometry that
                                ; only we know about would automount and then
                                ; read the wrong sectors with no user present
.nogeom:
    test byte [si+HDC_E_FLAGS], HDC_F_MOUNT
    jz .out
    mov cl, bl
    mov al, 1
    shl al, cl                  ; 8086: a variable shift goes through CL
    or [hd_wantmnt], al
    jmp short .out
.next:
    inc bl
    jmp short .scan
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_want_found - a drive that is THERE is a drive to mount (SPEC.md 52.6.1)
; in:  [hd_ndev], hd_devs
; out: [hd_wantmnt] gains a bit per probed device with a usable geometry
; clobbers: flags
;
; This ADDS to the set hd_cfg_load restored and never replaces it, so the
; automount can only ever attempt MORE than it used to - which is what makes
; this a re-decision with no case underneath it that stops working.
;
; hd_geom_ok is the page's own predicate (SPEC.md 47), so what is attempted
; here is exactly what the page would have let the user click - a device with
; no addressable geometry is skipped rather than handed to hd_mount, which
; would read a partition table through a CHS conversion that cannot name a
; sector.
; -----------------------------------------------------------------------------
hd_want_found:
    push ax
    push bx
    push cx
    push di
    xor bl, bl
.dev:
    cmp bl, [hd_ndev]
    jae .out
    mov al, bl
    call hd_dev_row             ; DI = the row, which is hd_geom_ok's argument
    call hd_geom_ok
    jc .next
    mov cl, bl
    mov al, 1
    shl al, cl                  ; 8086: a variable shift only goes through CL
    or [hd_wantmnt], al
.next:
    inc bl
    jmp short .dev
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_cfg_automount - mount what is there, and what the settings said was
;                    mounted (SPEC.md 52.6/52.6.1)
; in:  [hd_wantmnt]; DRVV_READY context
; out: nothing
; clobbers: flags
;
; **The volume is put back afterwards.** Every file slot resolves in the
; CURRENT volume and directory (SPEC.md 19.2), and a mount changes both - but
; this runs inside drv_boot, before the desktop's first paint, and the rest of
; the boot expects the system disk to be the current volume. The desktop icons
; appear anyway: osapi_vol_add posts the kernel's own deferred repaint
; (SPEC.md 31.2), which is drained after the first paint.
;
; DRVV_READY is also where a Control Panel TICK arrives (SPEC.md 51.2.2), so
; this is the boot's path and the tick's path both.
; -----------------------------------------------------------------------------
hd_cfg_automount:
    push ax
    push bx
    push cx
    push dx
    call hd_want_found          ; ...and the found set is added to the saved
                                ; one FIRST, so the zero test below is "no
                                ; drive worth mounting" rather than "the
                                ; settings said nothing" (SPEC.md 52.6.1)
    cmp byte [hd_wantmnt], 0
    je .out
    call OSAPI_FILE_HERE        ; DX = the current directory, BL = its drive
    mov [hd_cwd], dx
    mov [hd_cdrv], bl
    mov al, [hd_sel]
    mov [hd_selsave], al
    xor bl, bl
.dev:
    cmp bl, [hd_ndev]
    jae .done
    mov cl, bl
    mov al, 1
    shl al, cl
    test [hd_wantmnt], al
    jz .next
    mov [hd_sel], bl            ; hd_mount works on the SELECTED device, and
    call hd_mount               ; this is the one place that is not a click
.next:
    inc bl
    jmp short .dev
.done:
    mov al, [hd_selsave]
    mov [hd_sel], al
    mov dx, [hd_cwd]            ; the boot goes on with the system disk as
    mov bl, [hd_cdrv]           ; the current volume, exactly as it would
    call OSAPI_FILE_GOTO        ; have without us
    mov word [hd_msg], hd_s_pick
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; One way out, and the reason there is only one
;
; There were three - stage, stage-and-write-if-owed, stage-and-write-always -
; and the two that wrote are gone with the kernel verb they used. Everything
; that changes the blob now calls hd_cfg_mark and nothing in this driver can
; reach the disk: the geometry editor on every +, the page's Mount and Unmount,
; the disk tool's Delete when it unmounts the volume it is about to strand
; (SPEC.md 52.2.4), and detach. Opening the tool stages nothing of its own -
; the geometry is all it reads, and the editor staged that already.
; SPEC.md 31.8 is the whole argument.
; -----------------------------------------------------------------------------
