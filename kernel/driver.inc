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
DRVV_TIER   equ 2               ; in AH = SND_RT_*: how much of itself the
                                ; user wants (SPEC.md 34.8). out CF=0 and
                                ; SI = its service table, RE-COPIED because
                                ; the tier changes what it publishes; CF=1
                                ; and AL = a DRVE_* saying why not, with
                                ; nothing moved. Optional - sent only when a
                                ; setting asks for it

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

; --- SYSTEM.CFG: a KEYED record file (SPEC.md 51.5) --------------------------
; It is the whole Control Panel's file, not just the driver list: every
; setting the panel offers is here, so a machine boots in the state it was
; left in.
;
; **Nothing in it is positional.** It was a fixed 32-byte struct, which is
; the cheapest thing that works and the wrong thing to have: adding a setting
; is fine, but REMOVING one leaves a hole every later version has to keep
; reserving, and any rearrangement silently reinterprets an old file's bytes
; as the wrong settings. So every value now travels with a KEY that says what
; it is, and the file is a list of records rather than a layout:
;
;     0..7    the signature, byte for byte (drv_cfgsig)
;     8       word: the CONTAINER generation - the shape BELOW, not the
;             settings above it
;     10..    records: key(word) ver(byte) len(byte) data[len]
;             ...terminated by a key of 0
;
; Three properties follow, and they are the point:
;
;   1. **A key this kernel does not know is skipped**, by its own len, and is
;      NOT carried over - drv_cfg_ser writes what the table holds and nothing
;      else, so a setting written by a newer kernel survives being read by an
;      older one exactly until that older one saves. That is the intended
;      behaviour and not a bug: the alternative is preserving bytes whose
;      meaning we cannot check.
;   2. **A key whose MEANING changed carries a new ver**, so an old record
;      does not match and the DEFAULT stands. This is the case a plain struct
;      cannot express at all - same name, different encoding, and no way to
;      tell the two apart.
;   3. **Anything absent falls back to the default**, because drv_cfg_load
;      packs the LIVE state into the struct before deserializing over it -
;      and at boot the live state is exactly the defaults.
;
; The keys are two ASCII characters each so a hex dump of the file reads as
; the list of settings it is.
CFG_SIGN    equ 8               ; the signature, and where the generation sits
CFG_GEN     equ 8               ; word: the container generation
CFG_REC0    equ 10              ; ...and where the first record begins
CFG_GEN_V   equ 3               ; what this kernel writes, and the only one it
                                ; reads: generation 2 was the positional
                                ; struct, and a struct read as records is
                                ; nonsense, so it falls back to the defaults
CFG_FBUF    equ 64              ; the file buffer. The table below emits 10
                                ; bytes of header, 31 of records and a 2-byte
                                ; terminator = 43, so this is the next round
                                ; number up with room for a few more keys

CFR_KEY     equ 0               ; one record
CFR_VER     equ 2
CFR_LEN     equ 3
CFR_DAT     equ 4
CFR_HDR     equ 4

; The settings themselves, staged in a struct that is KERNEL-PRIVATE: only
; the FILE has to be self-describing, and a positional struct in RAM costs
; nothing and is never written to disk in this shape.
CFG_WANT    equ 0               ; word: bit n = drv_tab row n is wanted
CFG_RT      equ 2               ; db: [snd_route]  (SPEC.md 34.8)
CFG_H12     equ 3               ; db: [clk_h12]    (SPEC.md 31.5)
CFG_SEC     equ 4               ; db: [clk_secs]
CFG_SCH     equ 5               ; db: the scheduler mode (SPEC.md 8.2)
CFG_BB      equ 6               ; db: double buffering  (SPEC.md 32)
CFG_NB      equ 7               ; ...the struct's size

; key, ver, len, offset into the struct. ONE table drives the reader and the
; writer both, so a new setting is five bytes here and nothing else - and the
; reader can never disagree with the writer about what a key means.
;
; TO CHANGE WHAT A KEY MEANS, BUMP ITS ver. That is the whole compatibility
; story: old records stop matching and the default stands, which is the
; documented outcome rather than a misread value.
drv_cfg_keys:
    dw 'DW'                     ; Driver Wanted, the row bitmap
    db 1, 2, CFG_WANT
    dw 'SR'                     ; Sound Route
    db 1, 1, CFG_RT
    dw 'CH'                     ; Clock Hours, 12/24
    db 1, 1, CFG_H12
    dw 'CS'                     ; Clock Seconds
    db 1, 1, CFG_SEC
    dw 'SM'                     ; Scheduler Mode
    db 1, 1, CFG_SCH
    dw 'BB'                     ; Back Buffer
    db 1, 1, CFG_BB
CFG_NKEY    equ 6
CFG_KESZ    equ 5               ; bytes per table entry

; The three plain bytes, as (struct offset, kernel byte) pairs: one loop packs
; and another unpacks, so a new setting is three bytes of table rather than
; two more hand-written copies that can drift apart. The scheduler and the
; back buffer are NOT here - neither is a plain store (SPEC.md 8.2/32).
drv_cfg_map:
    db CFG_RT
    dw snd_route
    db CFG_H12
    dw clk_h12
    db CFG_SEC
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
    mov byte [drv_wcnt], 0
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
; drv_owns_seg - is BX the image segment of a driver that is loaded RIGHT NOW?
; in:  BX = a claim's owner word
; out: CF = 0 yes (a loaded driver's segment), CF = 1 no
; clobbers: nothing else (flags)
;
; The Task Manager's question, and only its question (SPEC.md 50.5/51.3). A
; driver's IMAGE is claimed under MEM_K_DRV and so is counted with the
; kernel's own tags; the bulk buffers the driver then claims for itself are
; owned by the segment it runs in, exactly as a package's data claims are
; owned by its region - because mem_own answers with ES either way and the
; driver has no instance to name instead.
;
; That left them billed to NOBODY: not a 0xFFxx tag, not an instance slot, so
; every per-row column skipped them while the HEAP and RAM totals counted them
; correctly. On a 128KB machine with a Sound Blaster the effect was 32KB of
; heap - the DMA buffer - visible in the totals and in the map's bands, and
; attributable to no row in the list. This is what closes that hole.
;
; A zero owner word can never match, because DRVR_SEG is 0 exactly when the
; row is not loaded, and the test skips those rows.
; -----------------------------------------------------------------------------
drv_owns_seg:
    push ax
    push cx
    push si
    mov si, drv_tab
    mov cx, DRV_MAX
.scan:
    mov ax, [si+DRVR_SEG]
    or ax, ax
    jz .next                    ; row not loaded: its stale segment is not an
    cmp ax, bx                  ; owner, and 0 would match a zeroed record
    je .yes
.next:
    add si, DRVR_SIZE
    loop .scan
    stc
    jmp short .out
.yes:
    clc
.out:
    pop si
    pop cx
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
; in:  BP = the published near offset (0 = nothing publishes it), every other
;      register = that proc's own arguments
; out: CF = 1 and AX = 0 when nothing publishes it; else whatever it returns
; clobbers: BP; DS and ES restored
;
; The one place that turns "a service exists" into a far call, so each API
; slot and snd_tick stay one test and one jump.
;
; **It takes NO GENERAL REGISTER, and that is a contract, not a convenience.**
; Every one of them is an argument to something in the sound ABI (SPEC.md
; 34.2/34.5) - AL the verb, AH a handle, BX the FM frequency or the caller's
; segment, CX a length, DH the requesting instance, DX a rate, SI and DI the
; two ends of a staging copy. This routine ate two of them in turn before the
; gate packages caught it: the driver ROW in BX, which silently became the FM
; frequency (every note refused, while tones - which pass AX - worked
; perfectly), and then the service SELECTOR in DI, which silently became the
; staging destination (every verb-5/6 copy refused as out of range). So the
; far pointer lives in memory and the selector arrives in BP, which nothing
; in the ABI uses and which this routine is documented to clobber anyway.
; -----------------------------------------------------------------------------
drv_svc_call:
    or bp, bp
    jz .none
    cmp word [drv_fseg], 0
    je .none
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
    mov cx, DSV_SIZE            ; the table alone: the 16-byte staged sink
    xor al, al                  ; name this once also cleared is gone, and
                                ; drv_cfg now sits right behind drv_svc
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
;
; DI and BP are saved for drv_tier's reason: the attach at the end of this is a
; far call into code the kernel did not write, and the Control Panel's Drivers
; page calls this holding both as its pane origin.
; -----------------------------------------------------------------------------
drv_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
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
    pop bp
    pop di
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
    mov ah, [snd_route]         ; ...then hand it the persisted tier, which
    call drv_tier               ; may take memory back off it (SPEC.md 34.8).
    clc                         ; A refusal is not a failed attach: the driver
                                ; is up at whatever tier it could reach, and
                                ; the Sound page shows which
.out:
    pop es
    pop bp
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_tier - tell the published driver which tier the user wants (SPEC.md 34.8)
; in:  AH = SND_RT_*
; out: CF = 0 it took (or there was nothing to tell); CF = 1 it refused, and
;      AL = the DRVE_* saying why
; clobbers: AL (flags)
;
; The tier arrives in AH rather than being read out of [snd_route], because the
; two callers disagree about what that byte means at the moment they call.
; Boot has already loaded the setting and is telling the driver about it;
; the Control Panel is asking WHETHER a tier can be had and only stores it if
; the answer is yes. Passing the candidate keeps the panel from having to set
; the setting and roll it back - a rollback that has to run on a path that
; already failed.
;
; SND_RT_AUTO means a fresh SYSTEM.CFG and "whatever answered", which is what
; attach already left, so it is not sent. A driver that does not implement
; DRVV_TIER answers CF=1 and its caller reports that - there is nothing to
; undo, since the verb's contract is that a refusal changes nothing.
;
; The row comes from [drv_owner] - the row whose services are published, which
; is the only driver a tier could be about. At boot that is the row drv_publish
; has just armed, so the attach path needs to pass nothing.
;
; The re-publish is not optional: a tier change alters the service table, and
; the kernel's copy is what every later dispatch reads.
;
; EVERYTHING IS SAVED, AH INCLUDED, because drv_call is a far call into code
; the kernel did not write: a driver may clobber any register it likes and the
; two callers here are mid-something. The Control Panel calls this holding DI
; and BP as its pane origin and then redraws with them; with DI eaten by the
; driver's DMA claim the redraw went to an off-screen x and the page simply did
; not change, which reads exactly like a click that never arrived. AH is saved
; for the same reason and gives this the clobber list its header claims - the
; caller's requested tier is what it stores on the way out.
; -----------------------------------------------------------------------------
drv_tier:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ax                     ; ...and the tier, which is AH
    cmp ah, SND_RT_AUTO
    je .ok                      ; unset: leave attach's answer alone
    mov bx, [drv_owner]
    or bx, bx
    jz .ok                      ; nothing is published: nothing to tell
    mov bp, [bx+DRVR_ENT]
    mov al, DRVV_TIER
    call drv_call               ; out: CF, AL = why not, SI = the table it
    jc .no                      ; now has
    mov es, [bx+DRVR_SEG]
    call drv_publish
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop bx                      ; the saved AX: AL is the output, so only AH
    mov ah, bh                  ; comes back
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
; DSV_NAME is copied as a POINTER and left at that. It used to be staged into
; a 16-byte kernel buffer as well, so the Control Panel could print it with
; font_str - and that was the Sound page's card row, which now names fixed
; TIERS instead (SPEC.md 34.8). Nothing reads a staged copy any more, so there
; is not one; the pointer is still published, and [drv_fseg] still says which
; segment it is an offset into, so a later consumer has everything it needs to
; stage its own.
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
;
; Everything is saved for drv_tier's reason: the detach verb is a far call into
; code the kernel did not write, and the Drivers page calls this holding DI and
; BP as its pane origin.
; -----------------------------------------------------------------------------
drv_unload:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call drv_row
    cmp word [bx+DRVR_SEG], 0
    je .out
    mov bp, [bx+DRVR_ENT]
    or bp, bp
    jz .free                    ; never attached: nothing could have spawned
    mov al, DRVV_DETACH
    call drv_call
    ; Detach told the workers to die; now WAIT for the deaths themselves
    ; before a byte of the image is freed. [drv_wcnt] falls inside
    ; task_exit's IF=0 window (drv_task), so zero means no task's CS points
    ; into the image. The bound is in TICKS, not yields - a worker sleeps a
    ; tick between passes, and 40 yields pass in microseconds.
    mov cx, 91                  ; ~5s: dozens of wakes for a worker that
.wait:                          ; needs one
    cmp byte [drv_wcnt], 0
    je .free
    mov dx, [ticks]
.tk:
    call task_yield
    cmp byte [drv_wcnt], 0
    je .free
    cmp dx, [ticks]
    je .tk                      ; same tick: let the others run again
    loop .wait
    jmp short .out              ; a worker that never died is a driver bug.
                                ; The row stays loaded and the image stays
                                ; claimed - a leak, never a free under a CS
                                ; that still points into it
.free:
    call drv_release
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_shutdown - take every loaded driver down, for reboot (SPEC.md 51.2)
; in:  nothing
; out: nothing (all registers preserved)
;
; int 19h resets no hardware: a hooked Sound Blaster would ride across it
; with its IRQ vector pointing into a heap segment the next boot reuses, the
; line unmasked at the 8259, and an auto-init DSP+8237 transfer still moving
; memory. The full drv_unload per row rather than a bare quiesce verb, so
; there is exactly ONE teardown path - main's snd_unhook did this for the
; resident card code, and the obligation moved here with the code.
; -----------------------------------------------------------------------------
drv_shutdown:
    push ax
    push bx
    push cx
    xor cx, cx
.next:
    mov al, cl
    call drv_row                ; BX = the row
    cmp word [bx+DRVR_SEG], 0
    je .skip
    mov al, cl
    call drv_unload
.skip:
    inc cx
    cmp cx, DRV_MAX
    jb .next
    pop cx
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
    cmp bx, [drv_owner]         ; disarm DISPATCH before a byte is freed:
    jne .norow                  ; snd_tick far-calls through drv_fseg inside
    mov word [drv_fseg], 0      ; IRQ0, and a tick landing between the free
    call drv_svc_clear          ; and a later disarm ran freed code. The
.norow:                         ; single word store is atomic against it
    mov word [bx+DRVR_SEG], 0   ; ...and drv_call is disarmed the same way
    mov word [bx+DRVR_KB], 0
    push bx                     ; anything the DRIVER itself claimed is owned
    mov bx, dx                  ; by its segment (mem_own), so sweep those
    call mem_free_owner         ; FIRST - the image's own free below takes
    mov bx, MEM_K_DRV           ; that segment out of existence, and a claim
    call mem_free               ; whose owner word no longer names anything
    pop bx                      ; could never be found again. The mem_free_rec
.out:                           ; fence (SPEC.md 50.4), for a driver
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_task - a driver's worker task, spawned and ended (SPEC.md 51.7)
;
; in:  AX = the near entry offset in the driver's OWN segment, or 0 meaning
;      "this IS the worker, and it is exiting"; DH = an argument word's high
;      half, which lands in the new task's DH; ES = the caller's DS, stamped
;      by the slot's X stub
; out: spawn: CF = 0 and AL = the task slot; CF = 1 refused.
;      exit:  NEVER RETURNS.
; clobbers: AX (an output), flags
;
; A driver cannot use OSAPI_TASK_SPAWN, and the reason is the thing that makes
; that call safe: inst_pkg_spawn's fence is a chain of five tests all keyed on
; an INSTANCE RECORD (SPEC.md 20.6), and a driver has none - no window, no
; record, no I_SPTR to be identical to. So this is its own slot with its own
; fence, rather than a sixth branch blunting that one.
;
; The fence is the same SHAPE: the caller's segment must be the segment of the
; driver whose services are currently published. ES is the caller's DS and a
; driver's DS is its own segment, so `ES == [drv_fseg]` is an identity test,
; not an approximation. One consequence to know: a driver cannot spawn from
; ATTACH, because drv_publish arms [drv_fseg] only after attach returns. Spawn
; from a service call, which is where a stream opens anyway.
;
; T_INST is 0xFF - a driver has no instance, so the Task Manager bills its
; worker to System, where its memory already is (MEM_K_DRV). That is why DL
; is not the caller's to set and DH is: task_spawn's argument word puts DL in
; T_INST and hands the whole of DX to the task. DH is how a driver gives its
; worker something PER-TASK - a stream generation, say - which a variable
; cannot do: a close and a re-open between the spawn and the worker's first
; instruction would leave both workers reading the second stream's value.
;
; **The exit half is not a convenience.** drv_unload FREES THE DRIVER'S IMAGE,
; so a worker still on the run queue would leave the scheduler holding a stack
; frame whose CS points at memory the next claim takes. A driver's DRVV_DETACH
; tells its workers to die, and AX = 0 is how each one says it has - but the
; authoritative wait is the KERNEL'S, in drv_unload, on [drv_wcnt]: the count
; goes up inside the spawn's IF=0 window and comes down inside task_exit's, so
; zero means no task's CS points into the image - there is no instant at which
; a worker has "said" it is gone and still has instructions left to run there,
; which is exactly the gap a driver-side flag cannot close. task_exit takes no
; release byte (BX = 0): a driver owns no instance record for one to live in.
;
; The exit half must NEVER be refusable for a real worker: a never-returns
; call that returns falls off the end of the caller's die path into whatever
; bytes follow it. So exit is fenced on "ES is a loaded driver's image
; segment" (drv_owns_seg), not on [drv_fseg] alone - a second driver's worker
; exiting while another driver's services are published still dies cleanly.
; -----------------------------------------------------------------------------
drv_task:
    push bx
    push dx
    mov bx, es
    or bx, bx
    jz .fail
    or ax, ax
    jz .exit
    cmp bx, [drv_fseg]          ; spawn: only the loaded driver, and only its
    jne .fail                   ; own segment, may ask
    cmp ax, DRV_HDR_SZ          ; an entry inside its own image, past the
    jb .fail                    ; header - the ld_check_hdr test
    mov dl, 0xFF                ; DL -> T_INST: no instance (SPEC.md 8/28);
                                ; DH is the caller's, and reaches the task
    pushf
    cli                         ; count-then-spawn in one IF=0 window: counted
    inc byte [drv_wcnt]         ; BEFORE the task can run, so the count never
    call task_spawn             ; lags a worker that spawned and died at once
    jc .spfail                  ; (task_spawn preserves IF, so nothing runs
    popf                        ; until our popf)
    clc
    jmp short .out
.spfail:
    dec byte [drv_wcnt]         ; refused: no task was made
    popf
    stc
    jmp short .out
.exit:
    cmp bx, [drv_fseg]
    je .die
    call drv_owns_seg           ; CF = 0: ES is some loaded row's image
    jc .fail
.die:
    cli                         ; IF=0 through task_exit (it would cli again
    dec byte [drv_wcnt]         ; anyway): the count moves in the SAME window
    xor bx, bx                  ; as the death publish, and this NEVER
    jmp task_exit               ; RETURNS - the two pushes above die with the
                                ; task's stack
.fail:
    stc
.out:
    pop dx
    pop bx
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
    mov [drv_cfg+CFG_WANT], ax
    call sched_mode_get
    mov [drv_cfg+CFG_SCH], al
    mov al, [bb_dbl]
    mov [drv_cfg+CFG_BB], al
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
    mov ax, [drv_cfg+CFG_WANT]
    call drv_want_set
    mov al, [drv_cfg+CFG_SCH]
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
    mov bx, drv_cfgbuf
    mov cx, CFG_FBUF
    mov si, drv_cfgname
    call dskw_read              ; AX = bytes actually read
    jc .out                     ; no file: the defaults stand
    call drv_cfg_pack           ; the LIVE state into the struct first: that
                                ; is what a key the file does not carry - or
                                ; carries at a ver we do not accept - falls
                                ; back to, and at boot the live state IS the
                                ; defaults (SPEC.md 51.5). No second copy of
                                ; them to keep in step
    call drv_cfg_deser
    jc .out
    call drv_cfg_unpack
    clc
    jmp short .done
.out:
    stc
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_cfg_deser - the file's records into the settings struct (SPEC.md 51.5)
; in:  AX = bytes read into drv_cfgbuf
; out: CF = 0 the struct holds this file's settings; CF = 1 nothing usable
;      was found and the struct is untouched
; clobbers: flags
;
; **Defaults first, records second.** The struct is seeded from drv_cfg_def
; before a record is looked at, so a key that is absent - or present with a
; ver this kernel does not accept - leaves the DEFAULT in place rather than
; whatever the struct happened to hold.
;
; Every step is bounded by the byte count the read actually returned: a record
; header that would run past the end, or a len that would, ends the walk. A
; truncated or corrupt file therefore costs the settings it did not carry and
; nothing else - the same shape as every other hostile-input path here.
; -----------------------------------------------------------------------------
drv_cfg_deser:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cmp ax, CFG_REC0            ; too short to hold even a header
    jb .bad
    mov dx, ax                  ; DX = the file's length, the only bound
    mov si, drv_cfgbuf
    mov di, drv_cfgsig
    mov cx, CFG_SIGN
    cld
.sig:
    mov al, [si]
    cmp al, [di]
    jne .bad                    ; a foreign file of that name: left alone
    inc si
    inc di
    loop .sig
    cmp word [drv_cfgbuf+CFG_GEN], CFG_GEN_V
    jne .bad                    ; another generation's SHAPE: the records are
                                ; not records. Defaults, and the next save
                                ; rewrites the file in this one
    mov si, CFG_REC0            ; SI = offset of the next record
.rec:
    mov ax, si
    add ax, CFR_HDR
    cmp ax, dx
    ja .done                    ; no room for another header: end of file
    mov bx, si
    cmp word [bx+drv_cfgbuf+CFR_KEY], 0
    je .done                    ; the terminator. Tested IN MEMORY, because AX
                                ; is still si+CFR_HDR from the bound check
                                ; above and the next one needs it
    mov cl, [bx+drv_cfgbuf+CFR_LEN]
    xor ch, ch
    add ax, cx                  ; AX = where the NEXT record starts
    cmp ax, dx
    ja .done                    ; its data would run past the end
    push ax
    mov ax, [bx+drv_cfgbuf+CFR_KEY]
    call drv_cfg_key            ; DI = table entry, or CF=1 = not ours
    jc .next
    mov cl, [di+3]              ; the length THIS kernel expects
    xor ch, ch
    cmp cl, [bx+drv_cfgbuf+CFR_LEN]
    jne .next                   ; right key, wrong size: not our record
    mov al, [di+2]
    cmp al, [bx+drv_cfgbuf+CFR_VER]
    jne .next                   ; right key, DIFFERENT MEANING: the default
                                ; stands, which is the whole point of ver
    push si
    mov al, [di+4]              ; where it lives in the struct
    xor ah, ah
    mov di, ax
    add di, drv_cfg
    lea si, [bx+drv_cfgbuf+CFR_DAT]
    rep movsb
    pop si
.next:
    pop si                      ; ...the next record, bounded above
    jmp .rec
.done:
    clc
    jmp short .dout
.bad:
    stc
.dout:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_cfg_key - find a key in drv_cfg_keys (module internal)
; in:  AX = the key
; out: CF = 0 and DI = the entry; CF = 1 = this kernel does not know it
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
drv_cfg_key:
    push cx
    mov di, drv_cfg_keys
    mov cx, CFG_NKEY
.scan:
    cmp [di], ax
    je .hit
    add di, CFG_KESZ
    loop .scan
    pop cx
    stc
    ret
.hit:
    pop cx
    clc
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
; drv_cfg_ser - the settings struct out as records (SPEC.md 51.5)
; in:  nothing (drv_cfg current)
; out: CX = the file's length in bytes
; clobbers: CX (the output), flags
;
; Writes the signature, this kernel's container generation, one record per
; table entry and a zero terminator - so what it emits is exactly what this
; kernel understands. A record read from a NEWER kernel's file is therefore
; gone after the first save, which is the documented behaviour: keeping bytes
; whose meaning cannot be checked is worse than losing them.
; -----------------------------------------------------------------------------
drv_cfg_ser:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, drv_cfgsig
    mov di, drv_cfgbuf
    mov cx, CFG_SIGN
    rep movsb
    mov ax, CFG_GEN_V
    stosw
    mov si, drv_cfg_keys
    mov dx, CFG_NKEY
.rec:
    mov ax, [si]                ; key
    stosw
    mov al, [si+2]              ; ver
    stosb
    mov al, [si+3]              ; len
    stosb
    mov bl, [si+4]              ; ...and the bytes themselves
    xor bh, bh
    add bx, drv_cfg
    mov cl, [si+3]
    xor ch, ch
.cp:
    mov al, [bx]
    stosb
    inc bx
    loop .cp
    add si, CFG_KESZ
    dec dx
    jnz .rec
    xor ax, ax
    stosw                       ; the terminator
    mov cx, di
    sub cx, drv_cfgbuf          ; CX = what to write
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
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

    push ds                     ; rebuild the whole file: a settings file is
    pop es                      ; small and a partial one is a bug waiting for
    call drv_cfg_pack           ; the next format change
    call drv_cfg_ser            ; CX = its length, records and all

    mov bx, drv_cfgbuf
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
    cmp byte [drv_cfg+CFG_BB], 0
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
drv_wcnt:   resb 1              ; live driver worker tasks: +1 inside the
                                ; spawn's IF=0 window, -1 inside task_exit's
                                ; (drv_task). drv_unload waits for 0 before
                                ; the image is freed (SPEC.md 51.7)
drv_fsz:    resw 1              ; the driver file's byte size
drv_owner:  resw 1              ; the row whose services are published, or 0
drv_svc:    resb DSV_SIZE       ; a copy of that row's service table
drv_cfg:    resb CFG_NB         ; the live settings, staged (layout above)
drv_cfgbuf: resb CFG_FBUF       ; the file itself, records and all

section .text
