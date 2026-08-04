; =============================================================================
; os8088 - driver.inc
;
; Loadable drivers (SPEC.md 51): the table, the loader, the settings file and
; the deferred boot notice. Prefix `drv_`.
;
; A driver is a package that is not an application. Same 32-byte header, same
; org 0, same paragraph-aligned heap claim, same three-byte dispatcher at
; PKG_DISP - so `drv_call` is `wm_pkgcall` with the far pointer taken out of
; a driver row instead of a window record, and a driver author writes near
; procs with near `ret`s exactly as a package author does. Four things
; differ:
;
;   - It is a **.DRV file**, and the mount types an entry as an application
;     only when its extension is O88 (SPEC.md 19), so a driver can never be
;     double-clicked into the loader. To the file manager it is data.
;   - Its header version is **4**, so if one ever did reach ld_check_hdr it
;     would be refused there too. Two independent gates, because "the kernel
;     ran a driver as an application" is not a failure mode worth one gate.
;   - It has **no instance record**: no dock tile, no Task Manager row, no
;     window, no I_CYC billing. Its memory is a kernel claim (MEM_K_DRV), so
;     the Task Manager counts it as system RAM, which is what it is.
;   - **Its bss ships inside its image**, zero-filled on the floppy by
;     tools/os88drv.py. A package's bss is claimed by the loader because a
;     package's is large and its file is read by a peek-then-size dance; a
;     driver's is a few hundred bytes, and paying for them on disk buys a
;     load path with exactly one claim in it - claimed at the size the
;     directory entry already reported, before a byte is read. Bulk buffers
;     are a driver's own OSAPI_MEM_CLAIM at attach, not bss.
;
; THE OS DISK MAY BE ABSENT. Every path here treats "no readable system disk"
; as an ordinary answer and never as a failure to recover from: at boot the
; machine comes up without the driver and says so afterwards (drv_notice), and
; at runtime the Control Panel refuses the click with the reason in prose.
; That is the bb_avail idiom (SPEC.md 32) pointed at a floppy drive.
;
; Boot ordering, binding: drv_boot runs BEFORE the desktop's first paint, so a
; machine whose sound driver loads has sound from the first frame - and
; drv_notice runs AFTER it, because a window cannot go up on a screen that has
; not been painted. A failure is banked in the row, never reported where it
; happens.
; =============================================================================

DRV_MAX     equ 2               ; rows in drv_tab. The settings word carries
                                ; one bit per row, so this may grow to 16
                                ; without touching the file format.

; --- driver classes (SPEC.md 51.2) -------------------------------------------
DRVC_SOUND  equ 1               ; fills the FM and STREAM tiers (SPEC.md 34)

; --- driver table row, stride DRVR_SIZE --------------------------------------
; DRVR_DISP and DRVR_SEG are adjacent, in that order, ON PURPOSE: together
; they are the far pointer `call far [bx+DRVR_DISP]` reads, so arming the row
; is one word written and disarming it is one word cleared.
DRVR_DISP   equ 0               ; dw: PKG_DISP - the dispatcher's offset
DRVR_SEG    equ 2               ; dw: the segment it runs in, 0 = not loaded
DRVR_FILE   equ 4               ; dw: -> its NUL 8.3 file name (.text)
DRVR_TITLE  equ 6               ; dw: -> its display name (.text)
DRVR_KB     equ 8               ; dw: KB its claim took, 0 = not loaded
DRVR_ENT    equ 10              ; dw: its entry offset, banked at load
DRVR_CLASS  equ 12              ; db: the DRVC_* this row expects
DRVR_WANT   equ 13              ; db: 1 = the user wants it loaded
DRVR_ERR    equ 14              ; db: DRVE_* from the last attempt
DRVR_PAD    equ 15
DRVR_SIZE   equ 16

; --- load failures (SPEC.md 51.3). Every one of them is survivable. ----------
DRVE_OK     equ 0
DRVE_DISK   equ 1               ; no readable system disk in A:
DRVE_NOENT  equ 2               ; it is not on that disk
DRVE_BAD    equ 3               ; not a driver image
DRVE_MEM    equ 4               ; the heap cannot fund it
DRVE_HW     equ 5               ; it loaded and found no hardware
DRVE_NERR   equ 6               ; how many strings drv_errstr has

; --- .drv header: the .o88 header's shape with a different version -----------
DRV_H_MAGIC equ 0               ; word: 0x384F ('O','8')
DRV_H_VER   equ 2               ; byte: 4 = driver (a package is 3)
DRV_H_CLASS equ 3               ; byte: DRVC_*
DRV_H_LINK  equ 4               ; word: 0 (org 0, like every v3 package)
DRV_H_ENTRY equ 6               ; word: entry offset
DRV_H_IMG   equ 8               ; word: image size = the file's whole size
DRV_H_RSV   equ 10              ; word: reserved (the bss is inside the image)
DRV_H_DISP  equ PKG_DISP        ; 12: `call bp / retf`, exactly as a package
DRV_HDR_SZ  equ 0x20
DRV_VER     equ 4
DRV_MAX_KB  equ 40              ; the biggest driver image we will claim

; --- entry verbs (SPEC.md 51.2) ----------------------------------------------
DRVV_ATTACH equ 0               ; probe + hook. out CF=1 = no hardware, and
                                ; the contract is that NOTHING was hooked
DRVV_DETACH equ 1               ; silence, unhook, restore. Cannot fail.

; --- the service table an attached driver returns in SI ----------------------
; Offsets into the DRIVER'S segment. This is the whole of what a driver
; publishes; the kernel takes a copy at attach so every later dispatch is a
; near read plus one far call.
DSV_CAPS    equ 0               ; dw: capability bits it ADDS (SND_CAP_*)
DSV_FM      equ 2               ; dw: near proc behind slot 0x00F8, 0 = none
DSV_STREAM  equ 4               ; dw: near proc behind slot 0x0100, 0 = none
DSV_TICK    equ 6               ; dw: near proc called from snd_tick, 0 = none
DSV_RELINST equ 8               ; dw: near proc, release one instance's grants
DSV_NAME    equ 10              ; dw: -> a NUL sink name, in ITS segment
DSV_TONE    equ 12              ; dw: near proc, the tone tier's sink while
                                ; this driver is loaded (0 = leave it on the
                                ; speaker). An OPL2 publishes it, a Sound
                                ; Blaster does not.
DSV_SIZE    equ 14

; --- SYSTEM.CFG, 32 bytes in the system disk's root (SPEC.md 51.5) -----------
; It is the whole Control Panel's file, not just the driver list: every
; setting the panel offers is here, so a machine boots in the state it was
; left in.
DRV_CFG_VER  equ 8              ; word, after the 8-byte signature
DRV_CFG_WANT equ 10             ; word: bit n = drv_tab row n is wanted
DRV_CFG_RT   equ 12             ; db: [snd_route]  (SPEC.md 34.8)
DRV_CFG_H12  equ 13             ; db: [clk_h12]    (SPEC.md 31.5)
DRV_CFG_SEC  equ 14             ; db: [clk_secs]
DRV_CFG_SCH  equ 15             ; db: the scheduler mode (SPEC.md 8.2)
DRV_CFG_BB   equ 16             ; db: double buffering  (SPEC.md 32)
DRV_CFG_RSV  equ 17             ; ...and 15 bytes for what comes next
DRV_CFG_SZ   equ 32
DRV_CFG_V    equ 2              ; the version this kernel writes

; The three plain bytes, as (file offset, kernel byte) pairs: one loop packs
; and another unpacks, so a new setting is three bytes of table rather than
; two more hand-written copies that can drift apart. The scheduler and the
; back buffer are NOT here - neither is a plain store (SPEC.md 8.2/32).
drv_cfg_map:
    db DRV_CFG_RT
    dw snd_route
    db DRV_CFG_H12
    dw clk_h12
    db DRV_CFG_SEC
    dw clk_secs
DRV_CFG_MAPN equ 3

; =============================================================================
; Initialised .text data. The table is .text and not .bss for the reason
; every other boot-reachable table is: -f bin zeroes nothing, and the defaults
; below have to be right on a machine with no settings file at all.
; =============================================================================

drv_f_sound: db 'SOUND.DRV', 0
drv_t_sound: db 'Sound', 0

; The published driver's dispatcher, as a FAR POINTER. Adjacent and in this
; order because together they ARE the dword `call far [drv_fptr]` reads, and
; in .text because drv_svc_call tests the segment half on the machine's first
; beep. drv_publish arms it, drv_release clears it - so "is a service
; dispatchable" is one word test and the call itself needs NO REGISTER, which
; is the whole point: every general register is an argument to something in
; the sound ABI (SPEC.md 34.2), and BX in particular is the FM verb's
; frequency.
drv_fptr:    dw PKG_DISP
drv_fseg:    dw 0

drv_cfgname: db 'SYSTEM.CFG', 0
drv_cfgsig:  db 'O88CFG', 0, 0  ; 8 bytes, byte for byte the file's head

drv_tab:
    dw PKG_DISP, 0              ; row 0: the sound driver (SPEC.md 34/51.4)
    dw drv_f_sound, drv_t_sound
    dw 0, 0
    db DRVC_SOUND
    db 1                        ; wanted by default: a fresh disk loads it,
                                ; and a machine with no card simply reports
                                ; DRVE_HW once and carries on
    db DRVE_OK
    db 0

    dw PKG_DISP, 0              ; row 1: spare, and deliberately kept. A row
    dw 0, 0                     ; costs 16 bytes, and an empty one is how the
    dw 0, 0                     ; settings word's bit assignment stays stable
    db 0                        ; when the second driver arrives
    db 0
    db DRVE_OK
    db 0

drv_errstr:                     ; DRVE_* -> what the user is told
    dw drv_e0, drv_e1, drv_e2, drv_e3, drv_e4, drv_e5
drv_e0: db 'Loaded', 0
drv_s_off: db 'Not loaded', 0   ; ...and what a row that never failed and is
                                ; not up says instead (drv_status)
drv_e1: db 'No system disk in A:', 0
drv_e2: db 'Not on the system disk', 0
drv_e3: db 'Not a valid driver', 0
drv_e4: db 'Not enough memory', 0
drv_e5: db 'No hardware found', 0

; -----------------------------------------------------------------------------
; drv_init - the state the table cannot carry as initialised data
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
drv_init:
    push ax
    push bx
    push cx
    mov byte [drv_nerr], 0
    call drv_svc_clear
    xor cx, cx                  ; nothing is loaded on a cold boot, and .bss
    mov bx, drv_tab             ; is not what these live in - clear the two
.next:                          ; per-load words the table ships non-zero
    mov word [bx+DRVR_SEG], 0
    mov word [bx+DRVR_KB], 0
    mov word [bx+DRVR_ENT], 0
    add bx, DRVR_SIZE
    inc cx
    cmp cx, DRV_MAX
    jb .next
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_status - the sentence describing one row's state right now
; in:  BX = row ptr
; out: SI -> a NUL string
; clobbers: SI (the output), flags
;
; ONE answer, so the Control Panel's caption and the boot notice can never
; disagree with the checkbox next to them. It is derived and not stored,
; because two of the three cases are: "loaded" is DRVR_SEG, and "not loaded,
; nothing went wrong" is the absence of both. Only a FAILURE has a stored
; code, which is the only one of the three that could not be re-derived.
; -----------------------------------------------------------------------------
drv_status:
    push ax
    cmp word [bx+DRVR_SEG], 0
    jne .loaded
    mov al, [bx+DRVR_ERR]
    cmp al, DRVE_OK
    je .off
    cmp al, DRVE_NERR
    jae .off                    ; a code from a future version: say nothing
    mov ah, 0                   ; rather than index past the table
    shl ax, 1
    mov si, ax
    mov si, [si+drv_errstr]
    pop ax
    ret
.loaded:
    mov si, drv_e0
    pop ax
    ret
.off:
    mov si, drv_s_off
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_row - row index to row pointer (module internal)
; in:  AL = row index
; out: BX = drv_tab + index*DRVR_SIZE
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
drv_row:
    push ax
    push cx
    mov ah, 0
    mov cl, 4                   ; DRVR_SIZE = 16
    shl ax, cl
    mov bx, ax
    add bx, drv_tab
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_call - call into a loaded driver (SPEC.md 51.2)
; in:  BX = row ptr (loaded), BP = the near offset in ITS segment, every
;      other register = that proc's own arguments
; out: whatever it returns, CF included
; clobbers: BP (it is the input); DS and ES are restored
;
; wm_pkgcall's shape, and for wm_pkgcall's reasons: DS becomes the driver's
; segment, ES is pointed at KERNEL_SEG (everything the kernel hands it lives
; there), and the far call lands on the three-byte dispatcher inside the
; driver's own header. The far pointer comes out of the ROW, so this is
; re-entrant across drivers, and clearing DRVR_SEG disarms it.
; -----------------------------------------------------------------------------
drv_call:
    push ds
    push es
    push cs
    pop es
    mov ds, [es:bx+DRVR_SEG]
    call far [es:bx+DRVR_DISP]
    pop es
    pop ds
    ret

; -----------------------------------------------------------------------------
; drv_svc_call - dispatch one of the published service procs
; in:  DI = a DSV_* offset, every other register = that proc's own arguments
; out: CF = 1 and AX = 0 when nothing publishes it; else whatever it returns
; clobbers: BP; DS and ES restored
;
; The one place that turns "a service exists" into a far call, so each API
; slot and snd_tick stay one test and one jump.
;
; **It takes no register but DI, and that is a contract, not a convenience.**
; Every other general register is an argument to something in the sound ABI
; (SPEC.md 34.2/34.5) - AL is the verb, BX the FM frequency, CL the channel,
; DH the requesting instance, SI and ES a staged buffer. This routine used to
; load the driver ROW into BX for drv_call, which quietly ate the frequency:
; a note-on arrived at the driver with BX = a kernel offset, no OPL2 block
; fits 15,000 Hz, and every FM call came back refused while tones - which
; pass AX - worked perfectly. The far pointer lives in memory for exactly
; that reason.
; -----------------------------------------------------------------------------
drv_svc_call:
    cmp word [drv_fseg], 0
    je .none
    push di
    add di, drv_svc
    mov bp, [di]
    pop di
    or bp, bp
    jz .none
    push ds
    push es
    push cs
    pop es                      ; ES = KERNEL_SEG: what the driver reads the
    mov ds, [es:drv_fseg]       ; things it was handed through
    call far [es:drv_fptr]
    pop es
    pop ds
    ret
.none:
    xor ax, ax
    stc
    ret

; -----------------------------------------------------------------------------
; drv_svc_clear - forget an unloaded driver's services (module internal)
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
drv_svc_clear:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov word [drv_owner], 0
    mov word [drv_fseg], 0
    mov di, drv_svc
    mov cx, DSV_SIZE + 16       ; the table and the staged sink name
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_mounted - make drive A: the mounted volume, if it can be
; in:  nothing
; out: CF = 0 mounted; CF = 1 no readable system disk
; clobbers: nothing else (flags)
;
; A full remount every time, deliberately: the disk can be swapped between
; two Control Panel clicks, and every other mount in this OS is a full
; remount for exactly that reason (SPEC.md 18.3). A driver load is not the
; place to invent a caching rule.
; -----------------------------------------------------------------------------
drv_mounted:
    push ax
    push dx
    xor dl, dl                  ; drive A: - the disk we booted from
    call disk_mount
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_find - a file's byte size, out of the mount snapshot
; in:  SI -> a NUL display name (upper case, as dsk_synth writes them);
;      drive A: already mounted
; out: CF = 0 and AX = its size; CF = 1 not found
; clobbers: AX (the output), flags
;
; The snapshot, not a directory walk: disk_mount has already synthesized every
; root entry and dsk_get_dir stages one back into the kernel segment, so this
; is a string compare against memory we own. It is also what lets the claim be
; made at exactly the right size before a byte of the file is read.
; -----------------------------------------------------------------------------
drv_find:
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, si                  ; DX = the name wanted (SI is the walker's)
    xor bx, bx                  ; BX = entry index
.next:
    cmp bx, [disk_nfiles]
    jae .no
    mov ax, bx
    call dsk_get_dir            ; SI -> dsk_ent, in our own segment
    mov di, dx
.cmp:
    mov al, [si]
    mov ah, [di]
    cmp al, ah
    jne .nextent
    inc si
    inc di
    or al, al
    jnz .cmp
    mov si, dsk_ent             ; matched to the NUL
    mov ax, [si+LD_DE_SIZE]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.nextent:
    inc bx
    jmp short .next
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; drv_load - bring one driver up (SPEC.md 51.3)
; in:  AL = row index
; out: CF = 0 loaded (or already was); CF = 1 and [row+DRVR_ERR] = DRVE_*
; clobbers: nothing else (flags)
;
; The loader's order (SPEC.md 21) with the instance half removed: mount, size
; the file, claim exactly that, read, validate the header against the image
; that actually arrived, attach. Anything that fails after the claim frees it,
; so a refused driver costs nothing but the time.
;
; There is no peek-then-size dance and no second claim, because a driver's bss
; is inside its image (see the header comment). One claim, one read.
; -----------------------------------------------------------------------------
drv_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push es

    call drv_row                ; BX = the row for the whole routine
    cmp word [bx+DRVR_SEG], 0
    jne .already
    cmp word [bx+DRVR_FILE], 0
    je .bad                     ; an empty table row

    call drv_mounted
    jc .nodisk
    mov si, [bx+DRVR_FILE]
    call drv_find               ; AX = the file's size
    jc .noent
    cmp ax, DRV_HDR_SZ
    jb .bad
    mov [drv_fsz], ax

    add ax, 1023                ; -> whole KB (the size is well under 64KB,
    mov cl, 10                  ; so this cannot carry out)
    shr ax, cl
    cmp ax, DRV_MAX_KB
    ja .big
    push bx
    mov bx, MEM_K_DRV
    call mem_claim_hi           ; a driver's base IS its CS, so it can never
    pop bx                      ; move: top-down, like a package's region
    jc .nomem
    mov [bx+DRVR_SEG], dx       ; ...which also arms drv_call
    mov [bx+DRVR_KB], ax

    push bx                     ; the row - BX is the read's own destination
    mov es, dx                  ; offset, and dskw_read hands it back as 0
    mov si, [bx+DRVR_FILE]
    mov cx, [drv_fsz]
    xor bx, bx
    call dskw_read              ; the whole file, by name, in A:'s root
    pop bx
    jc .ioerr
    cmp ax, [drv_fsz]
    jne .ioerr

    call drv_check              ; the header, against the image that arrived
    jc .badfree

    call drv_attach             ; the far call into the driver itself
    jc .nohw

    mov byte [bx+DRVR_ERR], DRVE_OK
    clc
    jmp short .out

.already:
    clc
    jmp short .out
.nodisk:
    mov al, DRVE_DISK
    jmp short .fail
.noent:
    mov al, DRVE_NOENT
    jmp short .fail
.bad:
    mov al, DRVE_BAD
    jmp short .fail
.nomem:
    mov al, DRVE_MEM
    jmp short .fail
.big:
    mov al, DRVE_MEM
    jmp short .fail
.ioerr:
    mov al, DRVE_BAD
    jmp short .failfree
.badfree:
    mov al, DRVE_BAD
    jmp short .failfree
.nohw:
    mov al, DRVE_HW
.failfree:
    push ax
    call drv_release            ; the claim goes back; nothing was hooked
    pop ax
.fail:
    mov [bx+DRVR_ERR], al
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_check - validate a loaded driver image (module internal)
; in:  BX = row ptr with DRVR_SEG live, [drv_fsz] = the bytes that arrived
; out: CF = 0 and [bx+DRVR_ENT] filled; CF = 1 malformed
; clobbers: AX, flags
;
; ld_check_hdr's tests, on a driver: version 4 instead of 3, the row's own
; class, and the same dispatcher bytes - a driver whose header does not carry
; `call bp / retf` would jump the kernel into its data on the first call.
; -----------------------------------------------------------------------------
drv_check:
    push es
    mov es, [bx+DRVR_SEG]
    cmp word [es:DRV_H_MAGIC], 0x384F
    jne .bad
    cmp byte [es:DRV_H_VER], DRV_VER
    jne .bad
    mov al, [bx+DRVR_CLASS]
    cmp [es:DRV_H_CLASS], al
    jne .bad
    cmp word [es:DRV_H_LINK], 0
    jne .bad
    cmp word [es:DRV_H_DISP], 0D5FFh        ; call bp
    jne .bad
    cmp byte [es:DRV_H_DISP+2], 0CBh        ; retf
    jne .bad
    mov ax, [es:DRV_H_IMG]
    cmp ax, [drv_fsz]                       ; the image IS the file
    jne .bad
    mov ax, [es:DRV_H_ENTRY]
    cmp ax, DRV_HDR_SZ
    jb .bad
    cmp ax, [drv_fsz]
    jae .bad
    mov [bx+DRVR_ENT], ax                   ; banked: an unload must not be
    pop es                                  ; steerable by a driver that has
    clc                                     ; since scribbled on its own
    ret                                     ; header
.bad:
    pop es
    stc
    ret

; -----------------------------------------------------------------------------
; drv_attach - the far call that lets a driver claim its hardware
; in:  BX = row ptr with DRVR_SEG and DRVR_ENT live
; out: CF = 0 attached and its services published; CF = 1 refused - and the
;      contract is that it hooked NOTHING
; clobbers: flags
; -----------------------------------------------------------------------------
drv_attach:
    push ax
    push si
    push bp
    push es
    mov bp, [bx+DRVR_ENT]
    mov al, DRVV_ATTACH
    call drv_call               ; out: CF, and SI = its service table
    jc .out
    mov es, [bx+DRVR_SEG]
    call drv_publish            ; SI is an offset in ES, so copy it now
    clc
.out:
    pop es
    pop bp
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_publish - take a copy of an attached driver's service table
; in:  ES = the driver's segment, SI = its service table offset, BX = the row
; out: nothing (all registers preserved)
;
; A COPY, in the kernel's own .bss, for the reason dsk_get_dir stages a
; directory entry: every consumer downstream is then an ordinary near read
; with DS = KERNEL_SEG, and snd_tick - which runs inside IRQ0 - does not have
; to point a segment register anywhere to find out whether it has work.
;
; The sink's NAME is staged too, into a fixed buffer, because the Control
; Panel prints it with font_str and font_str reads DS.
; -----------------------------------------------------------------------------
drv_publish:
    push ax
    push cx
    push si
    push di
    mov di, drv_svc
    mov cx, DSV_SIZE / 2
.copy:
    mov ax, [es:si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .copy

    mov [drv_owner], bx         ; which row these belong to, so a detach can
    mov ax, [bx+DRVR_SEG]       ; take them away again - and the far pointer
    mov [drv_fseg], ax          ; drv_svc_call dispatches through

    mov byte [drv_sname], 0     ; the sink's name (0 = it named none)
    mov si, [drv_svc+DSV_NAME]
    or si, si
    jz .out
    mov di, drv_sname
    mov cx, 15
.nm:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .out
    loop .nm
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_unload - take one driver back down (SPEC.md 51.3)
; in:  AL = row index
; out: nothing (all registers preserved)
;
; Detach first, free second, and never the other way round: the detach verb is
; what silences the chip and unhooks the IRQ, and freeing the claim under a
; live interrupt vector points that vector at whatever claims the memory next.
; Detach cannot fail - a driver able to refuse to let go would be a driver the
; user cannot turn off.
; -----------------------------------------------------------------------------
drv_unload:
    push ax
    push bx
    push bp
    call drv_row
    cmp word [bx+DRVR_SEG], 0
    je .out
    mov bp, [bx+DRVR_ENT]
    or bp, bp
    jz .free
    mov al, DRVV_DETACH
    call drv_call
.free:
    call drv_release
.out:
    pop bp
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_release - give the image's claim back and disarm the row (internal)
; in:  BX = row ptr
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
drv_release:
    push ax
    push bx
    push dx
    mov dx, [bx+DRVR_SEG]
    or dx, dx
    jz .out
    push bx
    mov bx, MEM_K_DRV
    call mem_free
    pop bx
    mov word [bx+DRVR_SEG], 0   ; ...and drv_call is disarmed with it
    mov word [bx+DRVR_KB], 0
    cmp bx, [drv_owner]
    jne .out
    mov word [drv_fseg], 0      ; disarm the dispatch before the table goes
    call drv_svc_clear
.out:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; The settings file (SPEC.md 51.5)
;
; SYSTEM.CFG in the system disk's root: 32 bytes, signature + version + the
; wanted-driver bitmap. It goes through the ordinary file API, so it is an
; ordinary file - deletable, copyable and readable from DOS - and a missing or
; malformed one means "the defaults in drv_tab", never an error.
; =============================================================================

; -----------------------------------------------------------------------------
; drv_cfg_pack / drv_cfg_unpack - the record and the live settings (51.5)
; in:  nothing
; out: nothing (all registers preserved)
;
; The back buffer is unpacked SEPARATELY, by drv_boot, because turning it on
; is not a store: bb_set claims 150KB and seeds the buffer from VRAM under the
; gfx lock, and it has to happen after the screen exists.
; -----------------------------------------------------------------------------
drv_cfg_pack:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, drv_cfg_map
    mov cx, DRV_CFG_MAPN
.p:
    mov bl, [si]
    xor bh, bh
    mov di, [si+1]
    mov al, [di]
    mov [bx+drv_cfg], al
    add si, 3
    loop .p
    call drv_want_get
    mov [drv_cfg+DRV_CFG_WANT], ax
    call sched_mode_get
    mov [drv_cfg+DRV_CFG_SCH], al
    mov al, [bb_dbl]
    mov [drv_cfg+DRV_CFG_BB], al
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

drv_cfg_unpack:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, drv_cfg_map
    mov cx, DRV_CFG_MAPN
.u:
    mov bl, [si]
    xor bh, bh
    mov di, [si+1]
    mov al, [bx+drv_cfg]
    mov [di], al
    add si, 3
    loop .u
    mov ax, [drv_cfg+DRV_CFG_WANT]
    call drv_want_set
    mov al, [drv_cfg+DRV_CFG_SCH]
    cmp al, 1
    ja .out                     ; a byte from a future version: leave the
    call sched_mode_set         ; mode alone rather than pick nonsense
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_cfg_load - read SYSTEM.CFG and apply everything in it but the buffer
; in:  drive A: already mounted
; out: CF = 0 a valid record was read and applied; CF = 1 the defaults stand
; clobbers: flags
; -----------------------------------------------------------------------------
drv_cfg_load:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push cs
    pop es
    mov bx, drv_cfg
    mov cx, DRV_CFG_SZ
    mov si, drv_cfgname
    call dskw_read
    jc .out                     ; no file: the defaults stand
    cmp ax, DRV_CFG_SZ
    jne .out
    mov si, drv_cfg
    mov di, drv_cfgsig
    mov cx, 8
.sig:
    mov al, [si]
    mov ah, [di]
    cmp al, ah
    jne .out                    ; a foreign file of that name: left alone
    inc si
    inc di
    loop .sig
    call drv_cfg_unpack
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; drv_want_set / drv_want_get - the WANT bytes as one bitmap word
; in / out: AX = bit n set = row n is wanted
; clobbers: nothing else (AX is drv_want_get's output; flags)
; -----------------------------------------------------------------------------
drv_want_set:
    push ax
    push bx
    push cx
    mov cx, DRV_MAX
    mov bx, drv_tab
.next:
    mov byte [bx+DRVR_WANT], 0
    test al, 1
    jz .no
    mov byte [bx+DRVR_WANT], 1
.no:
    shr ax, 1
    add bx, DRVR_SIZE
    loop .next
    pop cx
    pop bx
    pop ax
    ret

drv_want_get:
    push bx
    push cx
    push dx
    xor ax, ax
    mov dx, 1
    mov cx, DRV_MAX
    mov bx, drv_tab
.next:
    cmp byte [bx+DRVR_WANT], 0
    je .no
    or ax, dx
.no:
    shl dx, 1
    add bx, DRVR_SIZE
    loop .next
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; drv_cfg_save - write the table's WANT bytes back to SYSTEM.CFG
; in:  nothing
; out: CF = 0 written; CF = 1 and AX = FERR_* (the caller says so in prose)
; clobbers: AX (the output), flags
;
; Mounts first, because this is reached from a Control Panel click and the
; disk in A: may not be the one that booted. UI-task context only, like every
; other write (SPEC.md 18.4).
; -----------------------------------------------------------------------------
drv_cfg_save:
    push bx
    push cx
    push si
    push di
    push es
    call drv_mounted
    jc .nodisk

    push ds                     ; rebuild the whole record: it is 32 bytes,
    pop es                      ; and a partial write of a settings file is a
    mov si, drv_cfgsig          ; bug waiting for the next version bump
    mov di, drv_cfg
    mov cx, 8
    cld
    rep movsb
    mov word [drv_cfg+DRV_CFG_VER], DRV_CFG_V
    mov di, drv_cfg + DRV_CFG_RSV
    mov cx, DRV_CFG_SZ - DRV_CFG_RSV
    xor al, al
    rep stosb
    call drv_cfg_pack

    mov bx, drv_cfg
    mov cx, DRV_CFG_SZ
    mov si, drv_cfgname
    call dskw_write             ; ES is already ours, which is where it reads
    jmp short .out
.nodisk:
    mov ax, FERR_NODISK
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; =============================================================================
; Boot (SPEC.md 51.3) and the notice that follows it
; =============================================================================

; -----------------------------------------------------------------------------
; drv_boot - load every driver the settings file asks for
; in:  nothing
; out: nothing (all registers preserved)
;
; Runs before the desktop's first paint. Nothing here can stop the boot: a
; disk that is not there, a file that is not on it and a card that is not in
; the machine are all recorded in the row and reported afterwards.
; -----------------------------------------------------------------------------
drv_boot:
    push ax
    push bx
    push cx
    call drv_mounted
    jc .nodisk
    call drv_cfg_load
    jc .load                    ; no record: every default stands
    cmp byte [drv_cfg+DRV_CFG_BB], 0
    je .load
    call gfx_lock               ; the one setting that is not a store: it
    mov al, 1                   ; claims 150KB and seeds the buffer from
    call bb_set                 ; VRAM, and refuses itself on an adapter or
    call gfx_unlock             ; a heap that cannot fund it (SPEC.md 32)
.load:
    xor cx, cx
.next:
    mov al, cl
    call drv_row
    cmp word [bx+DRVR_FILE], 0
    je .skip
    cmp byte [bx+DRVR_WANT], 0
    je .skip
    mov al, cl
    call drv_load
    jnc .skip
    mov byte [drv_nerr], 1      ; something to say once there is a screen
.skip:
    inc cx
    cmp cx, DRV_MAX
    jb .next
    jmp short .out
.nodisk:
    ; No system disk at all. Only a row the user actually wanted counts as a
    ; failure: a machine that has never enabled a driver must not be told
    ; about a floppy it does not need.
    xor cx, cx
.dnext:
    mov al, cl
    call drv_row
    cmp word [bx+DRVR_FILE], 0
    je .dskip
    cmp byte [bx+DRVR_WANT], 0
    je .dskip
    mov byte [bx+DRVR_ERR], DRVE_DISK
    mov byte [drv_nerr], 1
.dskip:
    inc cx
    cmp cx, DRV_MAX
    jb .dnext
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_notice - tell the user, once the screen exists (SPEC.md 51.3)
; in:  nothing
; out: nothing (all registers preserved)
;
; It opens the CONTROL PANEL on its Drivers page rather than putting up a
; notice of its own, and that is not a saving dressed up as a design: the
; page already names every driver, already says what its last load attempt
; answered - out of the same drv_errstr the notice would have used - and is
; the one place the user can actually DO something about it. A notice window
; would have said the same words and then made them go and find this page.
;
; The [cp_sel]-before-KIND_CTRL precedent is the menu bar clock's
; (SPEC.md 31.5): app_launch takes its own locks, so this must run with none
; held - which is why it is the last thing kmain does before ui_task.
; -----------------------------------------------------------------------------
drv_notice:
    push ax
    cmp byte [drv_nerr], 0
    je .out
    mov byte [drv_nerr], 0
    mov byte [cp_sel], CP_IDRV
    mov al, KIND_CTRL
    call app_launch
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

drv_nerr:   resb 1              ; 1 = drv_notice owes the user a window
drv_pad0:   resb 1
drv_fsz:    resw 1              ; the driver file's byte size
drv_owner:  resw 1              ; the row whose services are published, or 0
drv_svc:    resb DSV_SIZE       ; a copy of that row's service table...
drv_sname:  resb 16             ; ...and its sink's name, staged for font_str
drv_cfg:    resb DRV_CFG_SZ     ; SYSTEM.CFG, read and written whole

section .text
