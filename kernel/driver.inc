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
; That is the sw_avail idiom (SPEC.md 32) pointed at a floppy drive.
;
; Boot ordering, binding: drv_boot runs BEFORE the desktop's first paint, so a
; machine whose sound driver loads has sound from the first frame - and
; drv_notice runs AFTER it, because a window cannot go up on a screen that has
; not been painted. A failure is banked in the row, never reported where it
; happens.
; =============================================================================

DRV_MAX     equ 4               ; rows in drv_tab. The settings word carries
                                ; one bit per row, so the FILE FORMAT allows 16
                                ; - but the CONTROL PANEL does not: the Drivers
                                ; page fits exactly four (see row 2), and a
                                ; fifth wants SPEC.md 31.1's scrolling list.

; --- driver classes (SPEC.md 51.2) -------------------------------------------
; A class is also a PUBLICATION SLOT. Every table below that says "the driver
; whose services are published" is per class, because two drivers of different
; classes are attached at once the moment a machine has a sound card and a hard
; disk - and a single publication made the second attach silently disconnect
; the first (SPEC.md 51.2.1).
DRVC_SOUND  equ 1               ; fills the FM and STREAM tiers (SPEC.md 34)
DRVC_DISK   equ 2               ; block volumes: hard disks (SPEC.md 51.8)
DRVC_NET    equ 4               ; the parallel link (docs/NET-PLAN.md). A
                                ; SECOND block-serving class beside DRVC_DISK,
                                ; which is what DV_CLASS exists for
DRVC_DEBUG  equ 3               ; the serial monitor (SPEC.md 58). It publishes
                                ; no service the kernel dispatches - only a
                                ; Control Panel page - so it needs a class for
                                ; one reason: a class IS the publication slot,
                                ; and giving it somebody else's would
                                ; disconnect a sound card or a hard disk the
                                ; moment the user ticked it (SPEC.md 51.2.1)
DRVC_FILE   equ 5               ; a volume served by somebody ANSWERING
                                ; QUESTIONS ABOUT FILES rather than about
                                ; sectors (SPEC.md 62.9). Deliberately not
                                ; named for the cable: a serial redirector, a
                                ; RAM disk and a host-filesystem passthrough
                                ; under an emulator are the same shape, and
                                ; the last of those is the only one of the
                                ; four that can be tested in a container
DRVC_MAX    equ 5               ; classes, and so publication slots

DRVC_OVL    equ 0x40            ; NOT A CLASS, and mirrored from
                                ; drivers/os88drv.inc: a driver-shaped image
                                ; its OWNER loads and attaches, with no row in
                                ; drv_tab, no publication slot and no Control
                                ; Panel tick (SPEC.md 52.11). Well clear of
                                ; DRVC_MAX, so drv_cls_idx refuses it by range
                                ; alone and it can never take a class by
                                ; accident. HDDTOOL.DRV's owner is HDD.DRV;
                                ; XMEM.DRV's is the kernel (SPEC.md 41.12)

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
DRVE_BUSY   equ 6               ; ...or found it and cannot have it, because
                                ; something the kernel owns is already using
                                ; the line. DISTINCT from DRVE_HW on purpose:
                                ; "no hardware found" sends the user looking
                                ; for a card that is sitting right there, and
                                ; the fix for the two is not the same one -
                                ; DRVE_HW wants a card, this wants a jumper
                                ; or a different port (SPEC.md 58.2)
DRVE_TWICE  equ 7               ; a driver was asked to attach while it was
                                ; already attached. THE ONLY CODE HERE THAT
                                ; BLAMES THE SOFTWARE: every other value is a
                                ; fact about the machine the user might act on,
                                ; and this one says the machine is fine and the
                                ; code is wrong. It exists because that bug is
                                ; otherwise SILENT and expensive - snd_entry
                                ; let DRVV_READY fall into its attach body and
                                ; re-attached on every single load, which
                                ; succeeded (the hardware is still there and
                                ; answers the second probe) while orphaning a
                                ; 12KB DMA claim, so the only symptom was a
                                ; heap 12KB short. A second attach now refuses
                                ; and SAYS SO, on the Drivers page and in the
                                ; boot notice, next to the driver's own name
                                ; (SPEC.md 51.2.2)
DRVE_NERR   equ 8               ; how many strings drv_errstr has

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
DRV_BLOB_SZ equ 34              ; a driver's own settings, carried in
                                ; SYSTEM.CFG (SPEC.md 51.9). 34 bytes is what
                                ; the disk driver needs for four devices, and
                                ; it is reserved on every machine - which is
                                ; the price of not making the ONE file the
                                ; boot already reads into two

; --- entry verbs (SPEC.md 51.2) ----------------------------------------------
DRVV_ATTACH equ 0               ; probe + hook. out CF=1 = no hardware, and
                                ; the contract is that NOTHING was hooked
DRVV_DETACH equ 1               ; silence, unhook, restore. Cannot fail.
DRVV_READY  equ 3               ; "your services are published and the kernel
                                ; can take your calls now". Sent once, right
                                ; after drv_publish, and the ONLY point at
                                ; which a driver may do anything that comes
                                ; back through the API - a slot's fence tests
                                ; the publication slot, and attach runs before
                                ; it is armed (SPEC.md 51.2.2). Optional: a
                                ; driver that does not implement it returns.
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
DSV_TIERS   equ 14              ; dw: bitmap of 1 << SND_RT_* saying which
                                ; tiers this driver COULD provide. Every cell
                                ; above says what the driver IS; this one says
                                ; what it could BE, so DRVV_TIER never touches
                                ; it - only attach and detach do. 0 = the
                                ; driver did not answer the question, and the
                                ; Control Panel then assumes it can (SPEC.md
                                ; 34.8/31.7)
DSV_BLK     equ 16              ; dw: DRVC_DISK only - the block transport
                                ; behind a driver-backed volume (SPEC.md
                                ; 51.8). in AL = 0 read / 1 write, AH = the
                                ; driver's own volume handle, SI = the
                                ; VOLUME-RELATIVE LBA, CX = sectors,
                                ; DX:BX = the buffer. DX and not ES, because
                                ; the buffer is in LOW_SEG, the FAT window or
                                ; a heap claim and never in KERNEL_SEG - so
                                ; ES cannot carry its usual meaning here and
                                ; pretending otherwise is how a driver reads
                                ; its own image as a sector buffer.
                                ; out CF=0 done; CF=1 and AL = the int 13h
                                ; status byte convention: 03h = write
                                ; protected, anything else = a hard error
DSV_CPNAME  equ 18              ; dw: -> a NUL Control Panel page name in ITS
                                ; segment, 0 = this driver has no page. The
                                ; kernel STAGES it (SPEC.md 31.9), because
                                ; cp_list draws it with font_str and font_str
                                ; reads through DS
DSV_CPPAINT equ 20              ; dw: near proc, that page's paint - in
                                ; DI = pane left, DX = pane top, lock held
DSV_CPCLICK equ 22              ; dw: near proc, that page's click - in
                                ; DI/DX as above, CX/BX = pane-relative x/y
DSV_CPCLOSE equ 24              ; dw: near proc, "the Control Panel has been
                                ; closed" - no arguments, no answer, and the
                                ; gfx lock is HELD (app_close_win's contract),
                                ; so a driver may draw and may destroy its own
                                ; windows here. 0 = do not tell me.
                                ;
                                ; For page-only state that is worth holding
                                ; while the panel is up and not after it:
                                ; HDD.DRV frees the 11KB disk-tool image here
                                ; (SPEC.md 52.11.4). It is NOT a detach and
                                ; must not be treated as one - the driver is
                                ; still loaded, its volumes are still mounted,
                                ; and the panel may be opened again in a
                                ; second
DSV_FS      equ 26              ; dw: DRVC_FILE only - the FSV_* verb table,
                                ; an offset IN THE DRIVER'S OWN SEGMENT
                                ; (SPEC.md 62.9.1). A pointer and not thirteen
                                ; more cells here: thirteen would cost
                                ; 13 x 2 x DRVC_MAX bytes of .bss on every
                                ; machine including the ones with no driver at
                                ; all, and this costs 2 x DRVC_MAX = 10
DSV_SIZE    equ 28

; --- the FSV_* verbs behind DSV_FS (SPEC.md 62.9.1) --------------------------
; A DRVC_FILE volume has NO SECTORS - dsk_xfer refuses one outright - so what
; the kernel asks instead is these, through drv_fs_call with the verb in BP.
; The staged SPEC.md 19.1 entry's first-cluster word at offset 18 is AN OPAQUE
; HANDLE THE DRIVER ASSIGNS: only dsk_chdir and the loader ever interpret it,
; and both were already redirect points.
;
; Mirrored in drivers/os88drv.inc, which is the DRIVER's copy of the same
; numbers - the two are a published ABI and a %error would be better than a
; mirror, except that a driver never includes a kernel header and the kernel
; never includes a driver one. Change both.
FSV_LIST    equ 0               ; append this folder's entries through
                                ; OSAPI_FS_ENT. The kernel SORTS and
                                ; synthesizes '..' itself (19.4/19.5)
FSV_CHDIR   equ 2               ; AX = a handle out of an entry, 0 = the root.
                                ; out CF=1 refuses; CF=0 and DX = THE PARENT'S
                                ; HANDLE, which is what pays for '..' - there
                                ; is no directory sector here to read one from
FSV_STAT    equ 4               ; SI = a NUL 8.3 name, in KERNEL_SEG. out
                                ; CF=0, AX = THE HANDLE, DX:CX = size, BL = a
                                ; FAT-style attribute byte (0x10 = directory,
                                ; so dskw_rbody's existing 0x18 test stands)
FSV_READ    equ 6               ; AX = a handle, DX:BX = buffer (DSV_BLK's
                                ; rule: ES is KERNEL_SEG on entry so it cannot
                                ; also be the buffer's segment, SPEC.md 51.8),
                                ; DI:CX = the 32-bit capacity.
                                ; out CF=0 and DX:AX = bytes read
FSV_WRITE   equ 8               ; SI = name, DX:BX = bytes
FSV_APPEND  equ 10              ; the chunked pair (SPEC.md 18.4.4)
FSV_READAT  equ 12              ; AX = a handle, DX:BX = buffer, CX = capacity,
                                ; DI:SI = the 32-bit offset. out DX:AX =
                                ; delivered, 0 = at or past the end
FSV_DELETE  equ 14              ; SI = name
FSV_RENAME  equ 16              ; SI = old, DI = new
FSV_MKDIR   equ 18              ; SI = name
FSV_RMDIR   equ 20              ; SI = name
FSV_DFREE   equ 22              ; -> DX:AX = free BYTES, BX = the granule
FSV_ENUM    equ 24              ; AX = a folder's handle, CX = the ordinal,
                                ; DX:BX = a 32-byte buffer -> one SPEC.md
                                ; 19.1 staged entry (SPEC.md 62.9.7). NOT
                                ; FSV_LIST: that appends into the kernel's
                                ; one global listing, and a folder COPY walks
                                ; a folder that is not the one on screen
FSV_COPY    equ 26              ; AX = source folder, DX = destination folder,
                                ; SI = the name (both ends), in KERNEL_SEG.
                                ; out CF=0; CF=1 and AX = FERR_*
                                ;
                                ; BOTH ENDS ON ONE VOLUME, so the bytes never
                                ; cross the transport (SPEC.md 62.9.8). A
                                ; driver that does not publish it is refused
                                ; by drv_fs_call like any other absent verb,
                                ; and fcp_xfer streams as before - which is
                                ; what makes this an optimisation rather than
                                ; a contract every DRVC_FILE driver owes
FSV_SIZE    equ 28

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
CFG_VID     equ 6               ; db: the display adapter (SPEC.md 39.11.3) -
                                ; a VID_* kind, not a bitmap. Applied by
                                ; drv_boot and NOT by drv_cfg_unpack, because
                                ; it is not a store: it sets a video mode, and
                                ; it is
                                ; refused outright when the machine no longer
                                ; has that card - which is what stops a disk
                                ; carried from a machine with a Hercules in it
                                ; booting this one to a dead monitor
CFG_VDM     equ 7               ; db: [vid_dmode], Single or Extend (39.19)
CFG_VDL     equ 8               ; db: [vid_dlay], where display 1 sits. Both
                                ; range-checked before they are stored, on
                                ; CFG_SCH's terms: a byte from a future
                                ; version leaves the default alone rather
                                ; than selecting nonsense
CFG_DBLOB   equ 9               ; DRV_BLOB_SZ bytes: A DRIVER'S OWN SETTINGS,
                                ; and the kernel does not know what is in
                                ; them (SPEC.md 51.9). It knows the NAME and
                                ; the LENGTH, carries them to disk and back,
                                ; and hands them over when a driver asks -
                                ; which is why they round-trip untouched on
                                ; a machine whose driver never loads, rather
                                ; than being dropped like a key this kernel
                                ; does not know (rule 1 above)
CFG_NB      equ CFG_DBLOB + DRV_BLOB_SZ     ; ...the struct's size

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
    dw 'VM'                     ; Video Mode - which adapter, then SPEC.md
    db 2, 3, CFG_VID            ; 39.19's two arrangement bytes. ver 2 because
                                ; it WAS one byte: an old record's VM stops
                                ; matching and the defaults stand, which is
                                ; this table's whole compatibility story used
                                ; as written - and every other key in the file
                                ; still loads, keys being matched by name one
                                ; at a time
    dw 'HD'                     ; Hard Disk - the DRIVER's own blob (51.9)
    db 1, DRV_BLOB_SZ, CFG_DBLOB
CFG_NKEY    equ 7
CFG_KESZ    equ 5               ; bytes per table entry

; The file buffer, DERIVED rather than guessed: drv_cfg_ser emits the header,
; one CFR_HDR-byte header per key, that key's bytes, and a 2-byte terminator -
; and the keys TILE THE STRUCT EXACTLY (every byte of drv_cfg belongs to one
; key and no key overlaps another), so the bytes come to CFG_NB. 81 today, and
; it follows a new key automatically. It used to be a round number chosen by
; hand with slack under it, and slack in the one buffer that is sized by an
; expression anyone can evaluate is just .bss nothing can use - which, with
; KERN_CODE_MAX as tight as it now is, is memory somebody else needs.
CFG_FBUF    equ CFG_REC0 + CFG_NKEY * CFR_HDR + CFG_NB + 2

; The three plain bytes, as (struct offset, kernel byte) pairs: one loop packs
; and another unpacks, so a new setting is three bytes of table rather than
; two more hand-written copies that can drift apart. The scheduler and the
; adapter is NOT here - it is not a plain store (SPEC.md 8.2/39.11.3).
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
drv_f_hdd:   db 'HDD.DRV', 0
drv_t_hdd:   db 'Hard Drive', 0
                                ; DEBUG.DRV's pair is gone with its row: the
                                ; monitor's source is still in drivers/debug/
                                ; and SPEC.md 58 still stands, but nothing in
                                ; the kernel names it and nothing ships it
drv_f_net:   db 'NET.DRV', 0
drv_t_net:   db 'os88net', 0
drv_f_ram:   db 'RAMDISK.DRV', 0
drv_t_ram:   db 'Ram Disk', 0

; The published drivers' dispatchers, as FAR POINTERS - ONE PAIR PER CLASS.
; Adjacent and in this order because together they ARE the dword
; `call far [drv_fptr]` reads, and in .text because drv_svc_call tests the
; segment half on the machine's first beep. drv_publish arms one, drv_release
; clears one - so "is a service dispatchable" is one word test and the call
; itself needs NO REGISTER, which is the whole point: every general register
; is an argument to something in the sound ABI (SPEC.md 34.2), and BX in
; particular is the FM verb's frequency.
;
; **Per class, and that is a fix rather than a generalisation.** There used to
; be one pair, one [drv_owner] and one drv_svc, overwritten by whichever
; driver attached last - so on a machine with a sound card AND a hard disk the
; second attach silently disconnected the first, and the symptom (sound stops
; when the disk driver is enabled) is nowhere near the cause. Class 1 is index
; 0, so every `[drv_svc+DSV_*]` in snd.inc still names the sound driver's copy
; and not one line of it changed.
DRV_FPSZ    equ 4               ; bytes per class: offset + segment
DRV_CPNSZ   equ 12              ; a staged Control Panel page name: the list
                                ; column is 9 glyphs wide (SPEC.md 31.1)
drv_fptr:    dw PKG_DISP        ; class 1 (DRVC_SOUND)
drv_fseg:    dw 0
drv_fptr2:   dw PKG_DISP        ; class 2 (DRVC_DISK)
drv_fseg2:   dw 0
drv_fptr3:   dw PKG_DISP        ; class 3 (DRVC_DEBUG). drv_cls_fp indexes
drv_fseg3:   dw 0               ; these contiguously, so a class is appended
drv_fptr4:   dw PKG_DISP        ; here and nowhere else
drv_fseg4:   dw 0               ; class 4 (DRVC_NET)
drv_fptr5:   dw PKG_DISP
drv_fseg5:   dw 0               ; class 5 (DRVC_FILE)

drv_cfgname: db 'SYSTEM.CFG', 0
drv_sysname: db 'KERNEL.SYS', 0 ; what makes a disk THE system disk (19.6): the
                                ; settings are written beside it or not at all
drv_cfgsig:  db 'O88CFG', 0, 0  ; 8 bytes, byte for byte the file's head

drv_tab:
    dw PKG_DISP, 0              ; row 0: the sound driver (SPEC.md 34/51.4)
    dw drv_f_sound, drv_t_sound
    dw 0, 0
    db DRVC_SOUND
    db 0                        ; NOT wanted by default, exactly like the disk
                                ; driver below. It was 1, so a fresh image -
                                ; which carries no SYSTEM.CFG at all - read
                                ; the whole 5.5KB driver off the floppy on
                                ; every boot to be told there was no card:
                                ; 27 sectors, and 512 bytes cost ~24 ms on the
                                ; machine this is for (PERFORMANCE.md Part 2,
                                ; Set 24), so about two thirds of a second -
                                ; it was ~6.4 s under the pre-AL-fix 238 ms,
                                ; which is the figure this decision was taken
                                ; against and the reason it reads as large. Then it
                                ; opened the Control Panel to report a failure
                                ; nobody had asked for. NOTHING LOADS UNLESS
                                ; SYSTEM.CFG ASKS FOR IT (SPEC.md 51.3), so
                                ; the tick in the Drivers page is both the
                                ; request and the record of it
    db DRVE_OK
    db 0

    dw PKG_DISP, 0              ; row 1: the hard-disk driver (SPEC.md 51.8)
    dw drv_f_hdd, drv_t_hdd
    dw 0, 0
    db DRVC_DISK
    db 0                        ; NOT wanted by default, unlike sound. This
                                ; one PROBES CONTROLLERS - an IDE task file
                                ; and, later, an XT DCB - and a machine with
                                ; neither should not be poking at 1F0h every
                                ; boot to be told so. The Drivers page turns
                                ; it on and SYSTEM.CFG remembers
    db DRVE_OK
    db 0

    dw PKG_DISP, 0              ; row 2: the RAM disk (SPEC.md 62.9), which is
    dw drv_f_ram, drv_t_ram     ; the FILE REDIRECTOR'S HARNESS as much as it
    dw 0, 0                     ; is software: a DRVC_FILE volume backed by no
    db DRVC_FILE                ; hardware at all, so every branch site the
    db 0                        ; redirector added to the kernel runs on a
                                ; cycle-accurate 8088 in a container - which
                                ; block mode never had, and which is why two of
                                ; ITS bugs reached the field instead of the
                                ; build. NOT wanted by default (SPEC.md 51.3).
                                ;
                                ; IT TOOK THE SERIAL MONITOR'S ROW, and that is
                                ; the one thing here that is not free. SPEC.md
                                ; 58's DEBUG.DRV was row 2; its source is still
                                ; in drivers/debug/ and its section still
                                ; stands, but it is no longer listed and no
                                ; longer ships. THE ROW HAD TO BE REUSED RATHER
                                ; THAN APPENDED because the Drivers page is
                                ; FULL: rows are CP_DROWH (26) apart from
                                ; CP_DR0Y (24) in a CP_CH (132) pane, so a
                                ; FIFTH row's glyphs start at 128 and its
                                ; caption falls off the bottom - and the panel
                                ; cannot grow, because CGA is 200 rows and
                                ; wm_fit's ceiling is 155 against a frame that
                                ; is already 151 (SPEC.md 31.1). A fifth driver
                                ; wants the scrolling list that section already
                                ; names as the answer, not a taller window.
                                ;
                                ; The cost of reusing an index rather than
                                ; appending is SYSTEM.CFG's driver bitmap,
                                ; which is one bit per row BY INDEX: a settings
                                ; file written before this comes up with the
                                ; RAM disk ticked where Debug was. That is
                                ; survivable in a way the reverse would not be
                                ; - this row probes nothing, writes to no port
                                ; and hooks no vector, so the worst it does is
                                ; mount a volume nobody asked for - and it is
                                ; why the RAM DISK took the free index rather
                                ; than the parallel link being shuffled down
                                ; into it
    db DRVE_OK
    db 0

    dw PKG_DISP, 0              ; row 3: the parallel link (docs/NET-PLAN.md).
    dw drv_f_net, drv_t_net     ; APPENDED, because the settings word carries
    dw 0, 0                     ; one bit per row by INDEX and inserting one
    db DRVC_FILE                ; would renumber every saved SYSTEM.CFG.
                                ; DRVC_FILE and no longer DRVC_NET: the cable
                                ; answers questions about FILES now (SPEC.md
                                ; 62.10), and drv_check compares this byte
                                ; against the image header's, so the row and
                                ; net.asm's OS88_DRIVER move together or the
                                ; driver is refused at load
    db 0                        ; NOT wanted by default, like every other row
                                ; (SPEC.md 51.3). This one WRITES to parallel
                                ; ports to find its cable, and a port with a
                                ; printer on it is a real machine
    db DRVE_OK
    db 0

drv_errstr:                     ; DRVE_* -> what the user is told
    dw drv_e0, drv_e1, drv_e2, drv_e3, drv_e4, drv_e5, drv_e6, drv_e7
drv_e0: db 'Loaded', 0
drv_s_off: db 'Not loaded', 0   ; ...and what a row that never failed and is
                                ; not up says instead (drv_status)
drv_e1: db 'No system disk in '
drv_e1_l: db 'A:', 0            ; the letter, stamped (SPEC.md 52.10.3): this
                                ; is the string an installed machine showed
                                ; as "No system disk in A:" about a driver
                                ; sitting on C:
drv_e2: db 'Not on the system disk', 0
drv_e3: db 'Not a valid driver', 0
drv_e4: db 'Not enough memory', 0
drv_e5: db 'No hardware found', 0
drv_e6: db 'Port or IRQ in use', 0
drv_e7: db 'Attached twice (bug)', 0    ; 20 chars, drv_e1's width - the widest
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle

                                ; the Drivers page caption takes

; -----------------------------------------------------------------------------
; drv_init - the state the table cannot carry as initialised data
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
drv_init_x:
    push ax
    push bx
    push cx
    mov byte [drv_nerr], 0
    mov byte [drv_wcnt], 0
    push di                     ; the driver blob starts EMPTY: -f bin zeroes
    push es                     ; no .bss, and a garbage blob is one a driver
    push ds                     ; would be entitled to believe (SPEC.md 51.9)
    pop es
    mov di, drv_cfg + CFG_DBLOB
    mov cx, DRV_BLOB_SZ
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    call drv_svc_clear_all
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
; drv_cls_fp / drv_cls_svc - one class's publication slot (module internal)
; in:  AL = class (1..DRVC_MAX)
; out: drv_cls_fp:  DI = &drv_fptr for that class
;      drv_cls_svc: DI = &drv_svc  for that class
;      CF = 1 and DI = 0 if the class is out of range (an empty table row)
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
drv_cls_fp_x:
    push ax
    call drv_cls_idx            ; DI = class-1, CF set if bad
    jc .bad
    shl di, 1
    shl di, 1                   ; * DRV_FPSZ
    add di, drv_fptr
    clc
.out:
    pop ax
    ret
.bad:
    xor di, di
    stc
    jmp short .out

drv_cls_svc_x:
    push ax
    push cx
    call drv_cls_idx
    jc .bad
    mov ax, DSV_SIZE
    push dx
    mul di                      ; DSV_SIZE is not a power of two
    pop dx
    mov di, ax
    add di, drv_svc
    clc
.out:
    pop cx
    pop ax
    ret
.bad:
    xor di, di
    stc
    jmp short .out

; drv_cls_idx - AL = class -> DI = class-1, CF=1 if out of range (internal)
drv_cls_idx:
    xor di, di
    cmp al, 1
    jb .bad
    cmp al, DRVC_MAX
    ja .bad
    mov di, ax                  ; AH is the caller's and must never reach the
    and di, 0x00FF              ; index, so zero-extend AL by hand
    dec di
    clc
    ret
.bad:
    stc
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
; drv_pub_seg - is BX the segment of a driver whose services are PUBLISHED?
; in:  BX = a segment
; out: CF = 0 yes (some class's publication slot names it), CF = 1 no
; clobbers: nothing else (flags)
;
; drv_owns_seg's stricter sibling, and the spawn fence (SPEC.md 51.7): loaded
; is not enough, because drv_publish arms a slot only after DRVV_ATTACH
; returns and a worker spawned from inside attach would outlive a refusal.
drv_pub_seg:
    push ax
    push cx
    push di
    mov al, 1
.scan:
    push ax
    call drv_cls_fp_x
    pop ax
    jc .next
    mov cx, [di+2]
    or cx, cx
    jz .next
    cmp cx, bx
    je .yes
.next:
    inc al
    cmp al, DRVC_MAX
    jbe .scan
    stc
    jmp short .out
.yes:
    clc
.out:
    pop di
    pop cx
    pop ax
    ret

drv_owns_seg_x:
    push ax
    push bx                     ; BX is banked because the second pass BELOW
    push cx                     ; rewrites it, and this is an input-only
    push si                     ; register to every caller
    call drv_seg_scan
    jnc .out
    call mem_owner_of_x           ; SPEC.md 52.11.6: not a driver image itself -
    jc .out                     ; is it a claim OWNED by one? A driver's SECOND
    call drv_seg_scan           ; image (52.11) is exactly that, and it is not
.out:                           ; in drv_tab by design. ONE level, like
    pop si                      ; mem_own_drv's
    pop cx
    pop bx
    pop ax
    ret

; drv_seg_scan - the drv_tab walk itself. in BX = segment; out CF=0 if a
; loaded row's image starts there. Clobbers flags.
drv_seg_scan:
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
drv_status_x:
    push ax
    mov al, [bx+DRVR_ERR]       ; A REPORTED REASON OUTRANKS 'Loaded', which is
    cmp al, DRVE_OK             ; the one thing that changed here: a driver can
    je .noerr                   ; now be up AND have said something went wrong
    cmp al, DRVE_NERR           ; (DRVE_TWICE, banked by drv_attach off the
    jae .noerr                  ; DRVV_READY call), and 'Loaded' would be true
    mov ah, 0                   ; and useless. Nothing else reaches that state
    shl ax, 1                   ; - every success path stores DRVE_OK - so no
    mov si, ax                  ; existing row's caption moves. A code from a
    mov si, [si+drv_errstr]     ; future version still says nothing rather than
    pop ax                      ; indexing past the table
    ret
.noerr:
    cmp word [bx+DRVR_SEG], 0
    jne .loaded
    mov si, drv_s_off
    pop ax
    ret
.loaded:
    mov si, drv_e0
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_row - row index to row pointer (module internal)
; in:  AL = row index
; out: BX = drv_tab + index*DRVR_SIZE
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
drv_row_x:
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
; drv_nostamp - "the DRIVER is asking, not the app that happened to be
;               dispatching" (SPEC.md 51.2.3)
; in:  nothing
; out: nothing (every register preserved)
; clobbers: flags
;
; A driver is not an instance. inst_caller answers with the dispatched
; callback's stamp, though, and the kernel enters a driver from inside one:
; ticking a row on the Drivers page is a Control Panel CLICK, so DRVV_READY -
; and every API cell the driver calls from it - was answered as the CONTROL
; PANEL. That is a lie in the one direction that costs a disk: osapi_file_here
; then reports where the PANEL thinks it is standing rather than where the
; machine is, and drv_load has just stood the machine on the system volume
; precisely so the driver can read from it (SPEC.md 51.2.2/52.11).
;
; So the stamp is cleared for the length of the call, which is what makes
; osapi_file_here's own sentence - "a caller with no instance behind it, the
; kernel, a driver, gets the machine's own position" - true rather than
; merely intended. The caller banks [snd_inst] and puts it back, which is
; ui_task's and loader_run's bracket (SPEC.md 34.3), so nested dispatches
; unwind correctly: a driver's page click that repaints a package's window
; restores that package's stamp on the way out.
;
; DI is saved because it is snd_disp_set's argument AND drv_cp_call's (the
; pane's left edge), and this routine is documented to preserve everything.
; -----------------------------------------------------------------------------
drv_nostamp:
    push di
    xor di, di                  ; 0 = no instance record, which snd_disp_set
    call KERNEL_SEG:cw_snd_disp_set  ; stores as 0xFF - inst_caller's "the
                                ; kernel". FAR, because this module is .cold
                                ; and snd.inc is resident (SPEC.md 2.6)
    pop di
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
;
; The stamp comes off for the duration (SPEC.md 51.2.3). `pop word [mem]`
; touches no flag, so CF still comes back out of the driver.
;
; **THE STAMP IS PUSHED FIRST AND POPPED LAST, BECAUSE `pop word [mem]` GOES
; THROUGH DS AND DS IS THE DRIVER'S UNTIL `pop ds` RUNS** (SPEC.md 51.2.4).
; It used to sit inside the pair, so the restore wrote `[snd_inst]`'s offset
; into the DRIVER'S segment: two bytes 0xD5F9 past the driver's base, which
; on a 640KB machine is past the top of the heap and lands in the VGA
; framebuffer - a 16px black run on one scan line of the desktop, redrawn on
; every dispatch. On the two mono adapters the same address is the unused
; hole below B000 and nothing shows at all, which is why it was reported as
; a VGA defect; on a smaller machine it is two bytes of somebody else's claim
; and shows as nothing until it shows as something else entirely. The stamp
; was not restored either, so every sound grant after a driver call was
; attributed to the kernel. loader.inc's package entry is the shape that was
; always right: restore the segment, THEN the word.
; -----------------------------------------------------------------------------
drv_call:
    push word [snd_inst]        ; ...while DS still names the kernel
    push ds
    push es
    call drv_nostamp            ; ...and it preserves DS, which the next two
    push ds                     ; lines depend on: ES = KERNEL_SEG is taken
                                ; from DS and NOT from CS, because this module
                                ; is .cold now and CS is COLD_SEG there
                                ; (SPEC.md 2.6 rule 2)
    pop es
    mov ds, [es:bx+DRVR_SEG]
    call far [es:bx+DRVR_DISP]
    pop es
    pop ds                      ; DS names the kernel again...
    pop word [snd_inst]         ; ...so the stamp lands in the kernel
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
drv_svc_call_x:
    or bp, bp
    jz drv_svc_none
    cmp word [drv_fseg], 0
    je drv_svc_none
    push ds
    push es
    push ds
    pop es                      ; ES = KERNEL_SEG: what the driver reads the
    mov ds, [es:drv_fseg]       ; things it was handed through
    call far [es:drv_fptr]
    pop es
    pop ds
    ret

; -----------------------------------------------------------------------------
; drv_blk_call - the same, for the DRVC_DISK publication slot
; in / out: exactly drv_svc_call's, on the disk class's far pointer
;
; A SECOND BODY rather than a class argument, and that is the register
; contract above taken seriously: the disk ABI (SPEC.md 51.8) spends AL, AH,
; BX, CX, DX and SI on the transfer itself, so there is no register left to
; say which class this is.
;
; SO THE CLASS ARRIVES IN MEMORY, in [drv_blkcls], written by dsk_xfer out of
; the volume row while BX still points at it. It was drv_fptr2/drv_fseg2 -
; the DISK class - named outright, which is right while one class serves
; blocks and silently wrong the moment two do: a network volume's sectors
; would have gone to the hard-disk driver (docs/NET-PLAN.md 2.1).
; -----------------------------------------------------------------------------
drv_blk_call_x:
    or bp, bp
    jz drv_svc_none
    push di
    push ax
    mov al, [drv_blkcls]
    call drv_cls_fp_x             ; DI = &drv_fptr for that class
    jc .none
    mov di, [di+2]              ; ...of which only the SEGMENT varies: the
    or di, di                   ; offset is always PKG_DISP, the three-byte
    jz .none                    ; dispatcher, and BP is the near proc it calls
    mov [drv_blkseg], di        ; stage it where a far call can reach it
    pop ax
    pop di
    push ds
    push es
    push ds
    pop es
    mov ds, [es:drv_blkseg]
    call far [es:drv_blkfp]
    pop es
    pop ds
    ret
.none:
    pop ax
    pop di
    jmp drv_svc_none
section .text                   ; DATA, so back to the kernel segment
                                ; (SPEC.md 2.6 rule 1)


; The staged far pointer drv_blk_call assembles per call. Adjacent and in this
; order because `call far [mem]` takes the offset from the word and the segment
; from the one after it.
drv_blkfp:   dw PKG_DISP
drv_blkseg:  dw 0
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


drv_svc_none:
    xor ax, ax
    stc
    ret

; -----------------------------------------------------------------------------
; drv_fs_call - dispatch one FSV_* verb of the DRVC_FILE driver (SPEC.md 62.9)
; in:  BP = the FSV_* verb; every other register = that verb's own arguments
; out: CF = 1 and AX = 0 when no DRVC_FILE driver is published, when it
;      publishes no DSV_FS table, or when that table's cell for this verb is 0;
;      otherwise whatever the verb answers
; clobbers: BP; DS and ES restored, and every other register is the verb's
;
; A THIRD BODY beside drv_svc_call and drv_blk_call, and it NAMES ITS CLASS
; the way drv_svc_call does rather than taking one the way drv_blk_call does.
; That is not an inconsistency and the distinction is worth keeping straight:
; drv_blk_call has to be told, because TWO classes serve blocks - DRVC_DISK
; and DRVC_NET, which is the whole reason DV_CLASS exists (SPEC.md 18.7.3) -
; and exactly ONE class serves files. The day a second one does, this grows a
; [drv_fscls] written out of the volume row, not an argument: BP is the only
; register the FSV ABI leaves free and it is already the verb.
;
; THE VERB TABLE LIVES IN THE DRIVER'S SEGMENT, and that is what DSV_FS being
; a POINTER costs here (SPEC.md 62.9.1: thirteen cells would be charged to
; every machine, including the ones with no driver at all). So the cell cannot
; be read with a plain [drv_svc+DSV_*] the way every other published service
; is - it takes a segment register for one word. ES is borrowed and put
; straight back, which is what makes it safe to borrow at all: ES is an
; ARGUMENT to the verbs that move bytes, and DX:BX rather than ES:BX is only
; the BUFFER's half of that rule (SPEC.md 51.8).
; -----------------------------------------------------------------------------
drv_fs_call:
    cmp word [drv_fseg5], 0     ; class 5 = DRVC_FILE. Nothing attached, or
    je drv_svc_none             ; attached and not yet published (SPEC.md
                                ; 51.2.1: the slot is armed after DRVV_ATTACH)
    push es
    push di
    mov di, [drv_svc + (DRVC_FILE-1)*DSV_SIZE + DSV_FS]
    or di, di
    jz .none                    ; a DRVC_FILE driver that published no table
    add di, bp                  ; ...+ the verb
    mov es, [drv_fseg5]
    mov bp, [es:di]             ; BP = the near proc, in the driver's segment
    pop di
    pop es
    or bp, bp
    jz drv_svc_none             ; a verb this driver does not implement, which
                                ; is an ordinary answer and not a fault: a
                                ; read-only volume publishes no FSV_WRITE
    push ds
    push es
    push ds
    pop es                      ; ES = KERNEL_SEG: what the driver reads the
                                ; things it was handed through. From DS and
                                ; NOT from CS - driver.inc is .cold, so CS is
                                ; COLD_SEG here (SPEC.md 2.6 rule 1), and the
                                ; push has to come before DS is spent below
    mov ds, [es:drv_fseg5]
    call far [es:drv_fptr5]     ; drv_fptr5/drv_fseg5 are adjacent and in that
    pop es                      ; order, so they ARE the far pointer - no
    pop ds                      ; staging like drv_blk_call's is needed
    ret
.none:
    pop di
    pop es
    jmp drv_svc_none

; -----------------------------------------------------------------------------
; drv_fs_has - does the DRVC_FILE driver publish this verb? (SPEC.md 62.9.1)
; in:  BP = the FSV_* verb
; out: CF = 0 when drv_fs_call would reach a proc for it, CF = 1 when it
;      would answer drv_svc_none. Every register preserved.
;
; THE SAME DSV_FS READ AS ABOVE, WITHOUT THE FAR CALL, and it exists because
; drv_fs_call's refusal is ambiguous by design: an absent verb answers CF=1
; with AX=0, and so does FSV_ENUM at the end of a folder (SPEC.md 62.9.1's
; table). A caller that RECURSES cannot tell those apart - "this folder is
; empty" and "this driver cannot enumerate" build the same empty tree - so
; the copy engine asks BEFORE it believes the first one (SPEC.md 62.9.7).
; -----------------------------------------------------------------------------
drv_fs_has:
    push es
    push di
    cmp word [drv_fseg5], 0     ; class 5 = DRVC_FILE, exactly as above
    je .no
    mov di, [drv_svc + (DRVC_FILE-1)*DSV_SIZE + DSV_FS]
    or di, di
    jz .no                      ; a DRVC_FILE driver that published no table
    add di, bp                  ; ...+ the verb
    mov es, [drv_fseg5]
    cmp word [es:di], 0
    je .no
    pop di
    pop es
    clc
    ret
.no:
    pop di
    pop es
    stc
    ret

; -----------------------------------------------------------------------------
; drv_cp_call - dispatch one class's Control Panel page proc (SPEC.md 31.9)
; in:  AL = class, BP = the published near offset, and DI/DX (+ CX/BX for a
;      click) = the page ABI's own arguments
; out: whatever the page returns
; clobbers: AX, BP; DS and ES restored
;
; A CLASS ARGUMENT is affordable here and nowhere else: the page ABI spends
; DI, DX, CX and BX and leaves AX alone, where the sound ABI spends every
; register there is. That is why drv_svc_call and drv_blk_call are two bodies
; and this is one.
;
; The stamp comes off here for drv_call's reason (SPEC.md 51.2.3), and this is
; the site that matters most: a driver's page is drawn and clicked from inside
; the Control Panel's own callback, so it is where a page that opens a file -
; the hard disk's Format and Install, which read HDDTOOL.DRV off the system
; volume (SPEC.md 52.11) - would otherwise resolve names in whatever folder
; the PANEL was launched in.
; -----------------------------------------------------------------------------
drv_cp_call_x:
    push si
    push di                     ; DI is the PAGE's argument (the pane left)
    or bp, bp                   ; and drv_cls_fp answers in it, so it has to
    jz .out                     ; come back before the call
    call drv_cls_fp_x
    jc .out
    cmp word [di+2], 0
    je .out
    mov si, di                  ; SI = the far pointer
    pop di
    push di                     ; ...and DI is the pane left again
    push word [snd_inst]        ; FIRST in and LAST out, for drv_call's
                                ; reason: the restore addresses DS and DS is
                                ; the driver's until `pop ds` (SPEC.md 51.2.4)
    push ds
    push es
    call drv_nostamp            ; ...and it preserves DS, which the next two
    push ds                     ; lines depend on: ES = KERNEL_SEG is taken
                                ; from DS and NOT from CS, because this module
                                ; is .cold now and CS is COLD_SEG there
                                ; (SPEC.md 2.6 rule 2)
    pop es
    mov ds, [es:si+2]
    call far [es:si]
    pop es
    pop ds
    pop word [snd_inst]
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; drv_cp_closed - tell every loaded driver the Control Panel has closed
; in:  nothing; the gfx lock is HELD and this is the UI task
; out: nothing (all registers preserved)
;
; One call, from the panel instance's own teardown (SPEC.md 31.9.1). It walks
; the classes rather than the selected page, because "the panel is gone" is
; true for every driver that has one and not only for the row on screen.
;
; A class with no driver has a zeroed table, so the published-or-not test is
; the cell itself and there is no second thing to keep in step.
;
; It is deliberately NOT sent on the reboot path, which is the other way
; cp_flush_close is reached: drv_shutdown detaches every driver a few
; instructions later, and a detach frees strictly more than this does.
; -----------------------------------------------------------------------------
drv_cp_closed_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov cl, 1                   ; classes are 1..DRVC_MAX
.cls:
    mov al, cl
    call drv_cls_svc_x            ; DI = that class's copied table
    jc .next
    mov bp, [di+DSV_CPCLOSE]
    or bp, bp
    jz .next                    ; no driver, or one that does not want telling
    mov al, cl
    xor di, di                  ; the page ABI's pane origin means nothing
    xor dx, dx                  ; here, and a driver must not read it
    call drv_cp_call_x
.next:
    inc cl
    cmp cl, DRVC_MAX
    jbe .cls
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; osapi_drv_cfg - a driver's own settings, in the kernel's file (SPEC.md 51.9)
; in:  AL = 0 get / anything else set
;      ES:SI = the driver's buffer, CX = its size
; out: get: CX = bytes handed over (never more than either side's size)
;      set: CF = 1 = CX is bigger than DRV_BLOB_SZ and nothing was stored
; clobbers: CX (an output), flags
;
; **The kernel does not know what is in it.** It knows the key, the length and
; how to get it to the disk and back; the driver owns the content and versions
; it itself, which is the only split that lets a setting belong to a driver
; and still ride in the one file the boot already reads.
;
; That matters most on the machine where the driver is NOT loaded: the blob
; comes off the disk at boot, sits in the struct, and is written back out
; unchanged. It is not dropped the way an unknown key is (SPEC.md 51.5 rule
; 1), and the difference is that this key is known - only its meaning is not.
;
; **A set never touches the disk**, and there is no verb that does. There was
; one - verb 2, "set and write NOW" - taken because a MOUNT is not a
; preference: a machine powered off with the panel still open comes back with
; a hard disk it had been told to mount and has not. That is the argument
; every page makes for being SPEC.md 31.8's exception, and it is refused here
; for the same reasons it is refused there, plus one this cell had to itself -
; the write remounts A: to reach the file, so a driver that had just made its
; own volume current silently lost it mid-click.
;
; AL = 2 is still ACCEPTED and means exactly what AL = 1 means, so a .DRV
; built against the old contract stores its blob and defers rather than
; failing (SPEC.md 20.8 rule 4: a shipped slot keeps its contract; this is a
; narrowing of when the bytes reach the disk, not of what the call does).
; -----------------------------------------------------------------------------
osapi_drv_cfg_x:
    push ax
    push bx
    push si
    push di
    mov bx, es
    call drv_pub_seg            ; a published DRIVER's slot, and nobody
    jc .no                      ; else's - the drv_task fence exactly
    cmp cx, DRV_BLOB_SZ
    jbe .size
    or al, al
    jnz .no                     ; a set bigger than the blob stores nothing
    mov cx, DRV_BLOB_SZ         ; ...a get just hands over what there is
.size:
    cld
    push cx                     ; rep eats it, and it is the get's answer
    or al, al
    jnz .set
    mov di, si                  ; get: the store -> the caller's buffer. DS is
    mov si, drv_cfg + CFG_DBLOB ; the kernel and ES the caller already, so this
    rep movsb                   ; is one rep movsb with no override
    pop cx
    jmp short .ok
.set:                           ; ...and a set is the SAME copy with the two
    mov di, drv_cfg + CFG_DBLOB ; segments swapped, rather than a second loop
    push ds
    push es
    push ds
    pop es                      ; ES = the kernel...
    mov ds, bx                  ; ...DS = the caller, whose segment is BX
    rep movsb
    pop es
    pop ds
    pop cx
    mov byte [cp_wdirty], 1     ; the file is OWED a write, and the panel's
                                ; teardown is the only thing that pays it -
                                ; there is no reachable flush from here now
                                ; (SPEC.md 31.8)
.ok:
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret
.no:
    xor cx, cx
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; drv_cp_count / drv_cp_class - the pages loaded drivers publish
; drv_cp_count  out: AL = how many classes publish a page
; drv_cp_class  in:  AL = a 0-based ordinal; out CF = 0 and AL = its class
; clobbers: AL (the output), flags
;
; Derived and not stored, so a page can never outlive the driver that owns it:
; the answer is a walk of the publication slots, and drv_release clears one.
; -----------------------------------------------------------------------------
drv_cp_count_x:
    push bx
    push di
    xor bx, bx                  ; BL = the count, BH = the class
    mov bh, 1
.next:
    mov al, bh
    call drv_cls_svc_x
    jc .done
    cmp word [di+DSV_CPNAME], 0
    je .skip
    inc bl
.skip:
    inc bh
    cmp bh, DRVC_MAX
    jbe .next
.done:
    mov al, bl
    pop di
    pop bx
    ret

drv_cp_class_x:
    push bx
    push di
    mov bl, al                  ; BL = the ordinal wanted
    mov bh, 1
.next:
    mov al, bh
    call drv_cls_svc_x
    jc .no
    cmp word [di+DSV_CPNAME], 0
    je .skip
    or bl, bl
    jz .hit
    dec bl
.skip:
    inc bh
    cmp bh, DRVC_MAX
    jbe .next
.no:
    pop di
    pop bx
    stc
    ret
.hit:
    mov al, bh
    pop di
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; drv_cp_name - a class's staged page name
; in:  AL = class
; out: SI -> a NUL string in the kernel segment ('' if none)
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
drv_cp_name_x:
    push ax
    push dx
    mov ah, DRV_CPNSZ
    mul ah
    sub ax, DRV_CPNSZ           ; class 1 is slot 0
    add ax, drv_cpname
    mov si, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_cp_stage - copy one class's page name into the kernel (module internal)
; in:  BX = the row, ES = the driver's segment, DI = its published table copy
; out: nothing (all registers preserved)
;
; The dsk_get_dir idiom in a new place, and the reason is font_str: it reads
; its string through DS, and DS is the KERNEL's everywhere the Control Panel
; draws. A pointer into the driver's segment would render its own image.
; -----------------------------------------------------------------------------
drv_cp_stage:
    push ax
    push cx
    push si
    push di
    push di
    mov al, [bx+DRVR_CLASS]
    call drv_cp_name_x            ; SI = the staged slot
    pop di
    mov cx, [di+DSV_CPNAME]
    mov di, si
    mov byte [di], 0
    jcxz .out                   ; no page: an empty name, and drv_cp_count
    mov si, cx                  ; then skips this class
    mov cx, DRV_CPNSZ - 1
.copy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_svc_clear - forget an unloaded driver's services (module internal)
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
drv_svc_clear:
    push ax
    push bx
    push cx
    push di
    push es
    push ds
    pop es
    mov bl, al                  ; BL = the class, for the second lookup
    call drv_cls_fp_x
    jc .out
    mov word [di+2], 0          ; the segment half: dispatch disarmed
    mov al, bl
    call drv_cls_idx
    shl di, 1
    mov word [di+drv_owner], 0
    mov al, bl
    call drv_cls_svc_x
    mov cx, DSV_SIZE            ; the table alone: the 16-byte staged sink
    xor al, al                  ; name this once also cleared is gone, and
                                ; drv_cfg now sits right behind drv_svc
    cld
    rep stosb
    mov al, bl                  ; ...and the staged page name with it, which
    call drv_cp_name_x            ; is what makes drv_cp_count stop counting
    mov byte [si], 0            ; this class
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_svc_clear_all - forget every class's services (module internal)
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
drv_svc_clear_all:
    push ax
    mov al, 1
.next:
    call drv_svc_clear
    inc al
    cmp al, DRVC_MAX
    jbe .next
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
    mov dl, [dsk_bootvol]       ; the disk we booted from - A: on a floppy
                                ; machine, where this byte is 0 and this is
                                ; the `xor dl, dl` it replaced, and the
                                ; installed PARTITION on a machine that boots
                                ; from its hard disk (SPEC.md 52.10.3)
    xor ax, ax                  ; ...at its root, which is where 19.6 puts
                                ; KERNEL.SYS, SYSTEM.CFG and every *.DRV
    call dsk_chdir_q_x            ; QUIET (SPEC.md 18.9): all three callers here
                                ; read or write a file BY NAME - drv_find is
                                ; dskw_stat and drv_cfg_* are dskw_read /
                                ; dskw_write_sys, none of which look at the
                                ; listing - so the scan, the sort and one
                                ; icon-harvest read per file in A:'s root are
                                ; all bought for nothing. ui_tm_open is the
                                ; caller that DOES need a listing (it hands
                                ; the loader a directory index) and it mounts
                                ; A: itself rather than coming through here
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; drv_vol_bank / drv_vol_back - put the user's volume back (SPEC.md 51.5.2)
; in (bank):  nothing.  in (back): the banked pair
; out: nothing. BOTH PRESERVE EVERY REGISTER AND THE FLAGS.
;
; drv_mounted goes to A: because that is where the system files are, and for
; a long time nothing put the volume back - so a user working on B: was
; silently moved to A: by a Control Panel click. Nothing said so: the next
; Standard File dialog simply opened on the wrong disk, and a package
; resolving a file name resolved it there. It is the same shape of bug the
; hard disk's Mount had (SPEC.md 52.6) and the same answer ui_tm_open already
; used for the Task Manager (SPEC.md 28.3) - which is why this is a routine
; and not a third open-coded dance.
;
; FLAGS ARE PRESERVED, and that is the load-bearing part rather than
; politeness: both callers return a result in CF and in AX, the restore runs
; at their common exit, and a remount that failed must not be mistaken for a
; driver that failed to load. A failed restore is therefore SWALLOWED - the
; volume is left at its root with the write gate shut, exactly as after any
; failed mount, which is ui_tm_back's answer too and a survivable place to be.
;
; THE COMPARE IS WHAT MAKES IT FREE. A remount is a floppy's worth of sectors
; (SPEC.md 19.2), so putting back a volume that never moved would put one on
; the boot path: drv_boot calls drv_load once per wanted driver, and each of
; those would have paid a mount to arrive where it already was. Banked equal
; to current means nothing moved and there is nothing to undo.
;
; One static pair, like ui_tm_cwd and for the same reason: this is UI-task
; context and the two brackets never nest - cp_flush_x is the only caller of
; drv_cfg_save, and drv_load reaches no path that writes the settings file.
; In .text with real initialisers, because -f bin zeroes no .bss and A:'s root
; is the right answer before anything has been banked.
; -----------------------------------------------------------------------------
drv_vol_bank:
    push ax
    push bx
    push dx
    call KERNEL_SEG:cw_osapi_file_here        ; DX = the directory cluster, BL = the drive
    mov [drv_vol_cwd], dx
    mov [drv_vol_drv], bl
    pop dx
    pop bx
    pop ax
    ret

drv_vol_back:
    pushf
    push ax
    push dx
    mov ax, [drv_vol_cwd]       ; dsk_chdir's arguments, loaded before the
    mov dl, [drv_vol_drv]       ; compare because they ARE the compare
    cmp dl, [disk_drive]        ; still where the user left us? then there is
    jne .go                     ; nothing to undo, and the remount is the
    cmp ax, [dsk_cwd]           ; whole cost
    je .out
.go:
    call dsk_chdir_q_x            ; CF = the remount failed; swallowed by the
                                ; popf below, deliberately.
                                ;
                                ; QUIET (SPEC.md 18.9), because putting the
                                ; user's VOLUME back is not the same as
                                ; putting a LISTING on screen: every Disk
                                ; window paints from its own view cache
                                ; (22.1), so nothing here is drawn from the
                                ; global snapshot, and whoever reads it next
                                ; pays for the rebuild if it is ever needed
.out:
    pop dx
    pop ax
    popf
    ret
section .text                   ; DATA, so back to the kernel segment
                                ; (SPEC.md 2.6 rule 1)


drv_vol_cwd: dw 0               ; the directory and drive to go back to
drv_vol_drv: db 0
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


; -----------------------------------------------------------------------------
; drv_find - a file's byte size, straight out of the directory
; in:  SI -> a NUL display name (upper case, as dsk_synth writes them);
;      drive A: already mounted
; out: CF = 0 and AX = its size; CF = 1 not found
; clobbers: AX (the output), flags
;
; **dskw_stat, NOT dsk_find_name** (SPEC.md 19.6). The latter walks the mount
; SNAPSHOT, and the snapshot is a display listing: dsk_synth filters the
; hidden and system entries out of it, which is exactly what a .DRV is now.
; Through it every driver on the disk reads as "not on the system disk" -
; and the Control Panel says so on its Drivers page, which is a long way
; from the cause. dskw_stat walks the directory sectors themselves and so
; sees a file whether or not it is meant to be shown.
;
; What stays here is the one thing a DRIVER wants out of the entry: its size,
; which is what lets the claim be made at exactly the right size before a
; byte of the file is read. DX:CX is a 32-bit size and a driver is capped at
; DRV_MAX_KB, so a non-zero high word is simply not one of ours.
; -----------------------------------------------------------------------------
drv_find:
    push bx
    push cx
    push dx
    push si
    call dskw_stat_x              ; AL = attribute, BX = first cluster,
    jc .no                      ; DX:CX = the size
    test dx, dx
    jnz .no
    mov ax, cx
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
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
drv_load_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    call drv_vol_bank           ; this goes to A: to find the .DRV, and the
                                ; user may have been anywhere (SPEC.md 51.5.2)
    call drv_row_x              ; BX = the row for the whole routine
%ifdef KERN_BIG                 ; ...and everything below is shared with
    jmp short drv_load_row      ; drv_load_at, which is kern_big's alone

; -----------------------------------------------------------------------------
; drv_load_at - load into a row-shaped record that is NOT in drv_tab
; in:  BX = a 16-byte DRVR_-shaped record, DRVR_FILE and DRVR_CLASS live
; out: exactly drv_load's - CF = 0 loaded, CF = 1 and DRVR_ERR = the DRVE_*
; clobbers: nothing else (flags)
;
; The whole of drv_load below the row lookup, reached with the row supplied
; instead of indexed: the volume bracket, the mount, drv_find, the ONE
; MEM_K_DRV claim, the read, drv_check and the shared failure tail. It exists
; so an OVERLAY the kernel owns (SPEC.md 52.11) is loaded by this body and not
; by a second copy of it - the copy being the shape that drifts, and the
; volume bracket being exactly what went missing the last time this path was
; duplicated in spirit (SPEC.md 51.5.2).
;
; A DRVC_OVL row stops after drv_check: an overlay has no class, so it can
; neither publish nor be sent DRVV_TIER or DRVV_READY, and ITS OWNER ATTACHES
; IT. That is the same division HDD.DRV and HDDTOOL.DRV already run on; here
; the owner is the kernel. On that path DRVR_SEG and DRVR_ENT come back live
; with nothing yet called, and the owner owes drv_release if it refuses.
;
; BEHIND %ifdef KERN_BIG, and it has to be: XMEM.DRV is its only caller and
; kern_small has no store above 1MB (SPEC.md 41.11), so an unguarded
; drv_load_at would be twenty-odd bytes of kern_big's feature charged to the
; 128KB-floor product - docs/KERN-SPLIT-PLAN.md 2's first leak, "a hook left
; outside its %ifdef", which is silent and which `make kernsplit` is the check
; for. HDD.DRV loads HDDTOOL.DRV through the file API and not through this, so
; nothing in kern_small wants it.
; -----------------------------------------------------------------------------
drv_load_at:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call drv_vol_bank
drv_load_row:
%endif                          ; ...and the SHARED body starts here. The
                                ; %endif belongs on this line and nowhere
                                ; lower: it sat 88 lines down for one build,
                                ; which took the whole middle of drv_load -
                                ; the mount, drv_find, the claim, the read and
                                ; drv_check - out of kern_small. It assembled
                                ; without a word, because every label the
                                ; guarded region defined was also inside it
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
    call mem_claim_hi_x           ; a driver's base IS its CS, so it can never
    pop bx                      ; move: top-down, like a package's region
    jc .nomem
    mov [bx+DRVR_SEG], dx       ; ...which also arms drv_call
    mov [bx+DRVR_KB], ax

    push bx                     ; the row - BX is the read's own destination
    mov es, dx                  ; offset, and the claim starts at ES:0000
    mov si, [bx+DRVR_FILE]
    mov cx, [drv_fsz]
    xor bx, bx
    xor dx, dx                  ; DX:CX = the capacity, DX:AX the answer -
    call dskw_read_x              ; the whole file, by name, in A:'s root
    pop bx
    jc .ioerr
    cmp ax, [drv_fsz]           ; DX is 0 or the read would have refused: a
    jne .ioerr                  ; driver is DRV_MAX_KB at the outside

    call drv_check              ; the header, against the image that arrived
    jc .badfree

%ifdef KERN_BIG
    cmp byte [bx+DRVR_CLASS], DRVC_OVL
    je .ovl                     ; an OVERLAY stops here: no class, so nothing
                                ; to publish and no verb the kernel may send.
                                ; Its owner attaches it (drv_load_at above)
%endif

    mov al, [bx+DRVR_CLASS]     ; ONE DRIVER PER CLASS AT A TIME, and this is
    call drv_cls_idx            ; the only place that can say so: a class IS
    jc .clsfree                 ; the publication slot (SPEC.md 51.2.1), so a
    shl di, 1                   ; second driver in one class does not share the
    mov ax, [di+drv_owner]      ; slot - drv_publish OVERWRITES it, and every
    or ax, ax                   ; later dispatch goes to whoever attached last
    jz .clsfree                 ; while the first driver's volumes stay mounted
    cmp ax, bx                  ; and browsable. RAMDISK.DRV and NET.DRV are
    je .clsfree                 ; both DRVC_FILE, so the user can ask for it
    mov al, DRVE_TWICE          ; from the Drivers page. REFUSE AND SAY SO
    jmp short .failfree         ; instead: 'Attached twice (bug)' beside the
                                ; second row, and the image handed straight
                                ; back. Genuine coexistence is a [drv_fscls]
                                ; resolved from the VOLUME's row, which
                                ; drv_fs_call's own header already names
.clsfree:
    mov byte [bx+DRVR_ERR], DRVE_OK ; the previous attempt's code is cleared
                                ; BEFORE the attach and not after it, because
                                ; drv_attach may BANK one of its own off the
                                ; DRVV_READY call (DRVE_TWICE) - stamped
                                ; afterwards, this line wiped it and the row
                                ; read 'Loaded' about a driver that had just
                                ; reported a bug. Every earlier failure path
                                ; returns before here with its own code, and
                                ; .nohwal below still overwrites this one
    call drv_attach             ; the far call into the driver itself
    jc .nohwal                  ; ...which may say WHY in AL (SPEC.md 51.2)
    clc
    jmp short .out

%ifdef KERN_BIG
.ovl:
    mov byte [bx+DRVR_ERR], DRVE_OK
%endif
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
.nohwal:
    ; A refusing attach MAY hand back a DRVE_* in AL, and most do not: the
    ; verb's contract was CF alone for a long time, so anything outside the
    ; table - DRVE_OK included, since a refusal is not OK - means "no
    ; hardware" and the driver simply did not say. That is the whole
    ; compatibility rule, and it is why the two drivers that predate this now
    ; set AL explicitly rather than leaving it to whatever their last compare
    ; happened to load.
    cmp al, DRVE_OK
    je .nohw
    cmp al, DRVE_NERR
    jb .failfree
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
    call drv_vol_back           ; every exit, including .already, which never
                                ; mounted - the compare makes that free. It
    pop es                      ; preserves CF, so the result above survives
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
    cmp byte [bx+DRVR_CLASS], DRVC_SOUND
    jne .notier                 ; a tier is SND_RT_*: it means nothing to a
                                ; class that is not sound, and sending it
                                ; would hand the SOUND driver's own row a
                                ; verb on the disk driver's attach
    mov ah, [snd_route]         ; ...then hand it the persisted tier, which
    call drv_tier_x               ; may take memory back off it (SPEC.md 34.8).
.notier:
    mov bp, [bx+DRVR_ENT]       ; ...and only now, when the slot is armed and
    mov al, DRVV_READY          ; every API cell will answer it, tell the
    call drv_call               ; driver so (SPEC.md 51.2.2). This is where a
                                ; disk driver re-mounts what its settings say
                                ; it had mounted last time
    jnc .rdy                    ; ...and if it complained, BANK WHAT IT SAID.
    mov [bx+DRVR_ERR], al       ; This answer used to be dropped on the floor,
                                ; which is fine for "I could not re-mount" and
                                ; useless for DRVE_TWICE: a driver reporting
                                ; that it was attached twice would have been
                                ; told nobody, and the row would read 'Loaded'
                                ; because it IS loaded. drv_status prefers a
                                ; reported reason over 'Loaded' now, so the
                                ; sentence arrives beside the driver's name
.rdy:
    clc                         ; A refusal is STILL not a failed attach: the
                                ; driver is up at whatever tier it could reach,
                                ; and the Sound page shows which
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
drv_tier_x:
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
    mov al, [bx+DRVR_CLASS]
    call drv_cls_svc_x            ; DI = this CLASS's copy, not the one copy
    jc .out
    mov cx, DSV_SIZE / 2
.copy:
    mov ax, [es:si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .copy

    mov al, [bx+DRVR_CLASS]     ; which row these belong to, so a detach can
    call drv_cls_idx            ; take them away again...
    shl di, 1
    mov [di+drv_owner], bx
    mov al, [bx+DRVR_CLASS]     ; ...and the far pointer this class's
    call drv_cls_fp_x             ; dispatcher goes through
    mov ax, [bx+DRVR_SEG]
    mov [di+2], ax
    mov al, [bx+DRVR_CLASS]     ; the Control Panel page's NAME is staged
    call drv_cls_svc_x            ; here and its two procs dispatched through
    call drv_cp_stage           ; the far pointer above (SPEC.md 31.9)
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
;
; Everything is saved for drv_tier's reason: the detach verb is a far call into
; code the kernel did not write, and the Drivers page calls this holding DI and
; BP as its pane origin.
; -----------------------------------------------------------------------------
drv_unload_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call drv_row_x
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
    call KERNEL_SEG:cw_task_yield
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
drv_shutdown_x:
    push ax
    push bx
    push cx
    xor cx, cx
.next:
    mov al, cl
    call drv_row_x                ; BX = the row
    cmp word [bx+DRVR_SEG], 0
    je .skip
    mov al, cl
    call drv_unload_x
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
    push di
    mov dx, [bx+DRVR_SEG]
    or dx, dx
    jz .out
    mov al, [bx+DRVR_CLASS]     ; disarm DISPATCH before a byte is freed:
    call drv_cls_idx            ; snd_tick far-calls through drv_fseg inside
    jc .novol                   ; IRQ0, and a tick landing between the free
    shl di, 1                   ; and a later disarm ran freed code. The
    cmp bx, [di+drv_owner]      ; single word store is atomic against it
    jne .novol                  ; ...AND A ROW THAT DOES NOT OWN THE SLOT HAS
                                ; NO VOLUMES TO DROP: osapi_vol_fence only
                                ; answers the driver whose class is published,
                                ; so a row that never published can never have
                                ; registered one - while the class ladder
                                ; below, reached with the row's class in AL,
                                ; would drop the volumes of whoever DOES own
                                ; it. That is a refused second DRVC_FILE
                                ; driver (51.2.1) unmounting the RAM disk on
                                ; its way back out, and it was visible: the
                                ; drive zone left the desktop the moment the
                                ; cable was ticked
    mov al, [bx+DRVR_CLASS]
    call drv_cls_fp_x
    mov word [di+2], 0
    mov al, [bx+DRVR_CLASS]
    call drv_svc_clear
    call cp_drv_gone_x            ; a page this driver owned is on screen no
                                ; longer, and [cp_sel] must not name it
                                ; (SPEC.md 31.9)
    mov al, [bx+DRVR_CLASS]     ; A VOLUME BELONGS TO A CLASS, and this is
    cmp al, DRVC_DISK           ; where it is named. dsk_vol_drop_drv used to
    je .dropvol                 ; drop EVERY driver-backed row - no argument
    cmp al, DRVC_NET            ; and it wanted none, because every kind-1
    je  .dropvol                ; volume belonged to the one published
    cmp al, DRVC_FILE           ; ...AND THE THIRD WAS MISSING, which is the
    jne .novol                  ; same bug one class later: a DRVC_FILE driver
                                ; registers a DVK_FILE volume and neither the
                                ; RAM disk nor the link deletes it on detach -
                                ; they leave it to here, and here did not know
                                ; the class. dsk_vol_drop_drv had the DVK_FILE
                                ; arm all along (disk.inc), so unticking the
                                ; RAM disk left a mounted volume whose driver
                                ; image had been freed: a desktop zone, a
                                ; browsable drive letter and a Disk window all
                                ; dispatching through drv_fs_call into memory
                                ; the next claim owns. A list of classes is a
                                ; second opinion about which drivers serve
                                ; volumes, and it went stale the way a second
                                ; opinion does
.dropvol:                       ; DRVC_DISK driver (51.2.1, dsk_vtab's old
                                ; DV_PAD). A SECOND block-serving class ends
                                ; that: it takes the class in AL now and drops
                                ; only that driver's rows, or unticking the
                                ; network driver would unmount the hard disk.
                                ; Unqualified it was correct for the disk
                                ; driver and pure damage for any other. On a
                                ; machine with a sound card AND a hard disk,
                                ; unticking SOUND in the Drivers page unmounted
                                ; every hard-disk partition: the zones went off
                                ; the desktop, C:'s FAT window was freed, and
                                ; the disk driver stayed loaded and published
                                ; the whole time, so nothing on screen pointed
                                ; anywhere near the cause. The sibling of the
                                ; bug 51.2.1's per-class publication fixed, in
                                ; the one place that was still class-blind
    call dsk_vol_drop_drv_x       ; every volume this driver registered goes
                                ; BEFORE its claims do (SPEC.md 51.8): a
                                ; mounted volume whose transport has been
                                ; freed is a read into whatever claims that
                                ; memory next
.novol:
    mov word [bx+DRVR_SEG], 0   ; ...and drv_call is disarmed the same way
    mov word [bx+DRVR_KB], 0
    push bx                     ; anything the DRIVER itself claimed is owned
    mov bx, dx                  ; by its segment (mem_own), so sweep those
    call mem_free_owner_x         ; FIRST - the image's own free below takes
    mov bx, MEM_K_DRV           ; that segment out of existence, and a claim
    call mem_free_x               ; whose owner word no longer names anything
    pop bx                      ; could never be found again. The mem_free_rec
.out:                           ; fence (SPEC.md 50.4), for a driver
    pop di
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
drv_task_x:
    push bx
    push dx
    mov bx, es
    or bx, bx
    jz .fail
    or ax, ax
    jz .exit
    call drv_pub_seg            ; spawn: only a PUBLISHED driver, and only its
    jc .fail                    ; own segment, may ask - published and not
                                ; merely loaded, because drv_publish arms the
                                ; fence after attach returns and a worker
                                ; spawned from attach outlives a refusal
    cmp ax, DRV_HDR_SZ          ; an entry inside its own image, past the
    jb .fail                    ; header - the ld_check_hdr test
    mov dl, 0xFF                ; DL -> T_INST: no instance (SPEC.md 8/28);
                                ; DH is the caller's, and reaches the task
    pushf
    cli                         ; count-then-spawn in one IF=0 window: counted
    inc byte [drv_wcnt]         ; BEFORE the task can run, so the count never
    call KERNEL_SEG:cw_task_spawn             ; lags a worker that spawned and died at once
    jc .spfail                  ; (task_spawn preserves IF, so nothing runs
    push bx                     ; until our popf)
    mov bl, al                  ; AL = the slot: a driver's worker is service
    xor bh, bh                  ; plumbing and survives the fsx freeze
    shl bx, 1                   ; (SPEC.md 53.2) - stamped inside the same
    shl bx, 1                   ; IF=0 window, so no tick can scan the record
    shl bx, 1                   ; before the bit is on
    or byte [bx+sch_tasks+T_FLAGS], TF_SERVICE
    pop bx
    popf
    clc
    jmp short .out
.spfail:
    dec byte [drv_wcnt]         ; refused: no task was made
    popf
    stc
    jmp short .out
.exit:
    call drv_owns_seg_x           ; CF = 0: ES is some loaded row's image
    jc .fail
.die:
    cli                         ; IF=0 through task_exit (it would cli again
    dec byte [drv_wcnt]         ; anyway): the count moves in the SAME window
    xor bx, bx                  ; as the death publish, and this NEVER
    call KERNEL_SEG:cw_task_exit ; RETURNS - the two pushes above die with the
                                ; task's stack, and so does the return address
                                ; this call leaves. A `jmp` to a cw_ shim
                                ; happens to work only while the target never
                                ; comes back; a call works either way
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
; is not a store: sw_set claims 150KB and seeds the buffer from VRAM under the
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
    call KERNEL_SEG:cw_sched_mode_get
    mov [drv_cfg+CFG_SCH], al
    mov al, [vid_kind]          ; the adapter the machine is RUNNING as, which
    mov [drv_cfg+CFG_VID], al   ; on a machine that has never been asked is
                                ; exactly what vid_detect chose - so the file
                                ; records the probe's answer and a first save
                                ; changes nothing about the next boot
%ifdef KERN_BIG
    mov al, [vid_dmode]         ; ...and SPEC.md 39.19's arrangement with it
    mov [drv_cfg+CFG_VDM], al
    mov al, [vid_dlay]
    mov [drv_cfg+CFG_VDL], al
%endif
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
    call KERNEL_SEG:cw_sched_mode_set         ; mode alone rather than pick nonsense
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
    push dx                     ; DX is the read's capacity high word going in
    push si                     ; and its size high word coming back (18.4.1)
    push di
    push es
    push ds
    pop es                      ; a 64-byte .bss buffer, not a heap claim: this
    mov bx, drv_cfgbuf          ; runs at boot and a claim can be refused, and
    mov cx, CFG_FBUF            ; the read takes ES:BX for exactly that reason
    xor dx, dx
    mov si, drv_cfgname
    call dskw_read_x              ; AX = bytes actually read (DX = 0: CFG_FBUF)
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
    pop dx
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
;
; **And it may not be the SYSTEM disk at all** (SPEC.md 51.5.1), which is the
; other half of that sentence and used to go unsaid. A single-floppy machine
; swaps to the apps disk to launch anything - `ui_tm_open` is built around
; exactly that - so a Control Panel change made afterwards wrote SYSTEM.CFG to
; whatever was in the drive. Two things went wrong at once: the setting never
; reached the system disk, and the user's data disk gained a hidden + system
; file that SPEC.md 19.6's own protection makes IMPOSSIBLE to list or delete
; from inside os8088.
;
; So the volume is identified before it is written: KERNEL.SYS in the root is
; what makes a disk the system disk, and dskw_stat answers that off the
; directory sectors, which is where a hidden + system entry can be seen at all.
;
; **dskw_stat and not drv_find**, deliberately: drv_find answers a SIZE and
; refuses one whose high word is non-zero, and KERNEL.SYS is 63,944 bytes
; today - so the day the kernel passes 64KB, the check would start reporting
; that the system disk is not the system disk. Existence is the question here.
; -----------------------------------------------------------------------------
drv_cfg_save_x:
    push bx
    push cx
    push dx                     ; DX is the write's count high word (18.4.1)
    push si
    push di
    push es
    call drv_vol_bank           ; the panel is closing on top of whatever the
                                ; user was doing; this write goes to the BOOT
                                ; VOLUME - A: on a floppy machine and the
                                ; installed partition otherwise (SPEC.md
                                ; 52.10.3) - and must not move them (51.5.2)
    call drv_mounted
    jc .nodisk
    mov si, drv_sysname
    call dskw_stat_x              ; the disk in A: is not guaranteed to be ours
    jc .notsys

    push ds                     ; rebuild the whole file: a settings file is
    pop es                      ; small and a partial one is a bug waiting for
    call drv_cfg_pack           ; the next format change
    call drv_cfg_ser            ; CX = its length, records and all

    mov bx, drv_cfgbuf
    xor dx, dx                  ; DX:CX = the count; a settings file is bytes
    mov si, drv_cfgname
    call dskw_write_sys_x         ; ES is already ours, which is where it reads.
                                ; _sys: SYSTEM.CFG is hidden + system, so it
                                ; is neither listed nor deletable, and only
                                ; this entry point may rewrite one (19.6)
    jmp short .out
.notsys:
    mov ax, FERR_NOENT          ; a disk, but not ours. [cp_dsave] carries it
    stc                         ; to the Drivers page, whose caption already
    jmp short .out              ; says 'Cannot save to the system disk' - which
.nodisk:                        ; is exactly the truth in this case
    mov ax, FERR_NODISK
    stc
.out:
    call drv_vol_back           ; CF and AX are this routine's result and
                                ; drv_vol_back preserves both - a remount
    pop es                      ; that failed must not read as a write that did
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; Boot (SPEC.md 51.3) and the notice that follows it
; =============================================================================

section .ovl                    ; boot overlay (SPEC.md 2.5): this runs ONCE,
                                ; from kmain, before the first mount overwrites
                                ; it - so it costs no RAM and nothing at all
                                ; against KERN_BUDGET. Same shape and the same
                                ; home as clk_init's four-rung RTC ladder
                                ; (SPEC.md 37.90), for the same reason: a
                                ; hardware probe that answers once at boot has
                                ; no business being resident.

DRVP_OPL    equ 0x388           ; the OPL2's address/status port; +1 is data

; -----------------------------------------------------------------------------
; drv_snd_sniff - is there an FM chip in this machine? (SPEC.md 51.3.1)
; in:  nothing (kmain, after drv_init and BEFORE drv_boot's first mount)
; out: nothing (all registers preserved). On a hit, row 0's DRVR_WANT is set
;
; Called before drv_cfg_load rather than after, because the overlay is dead by
; then - drv_boot's own mount is what writes over it. Setting DRVR_WANT is
; therefore a DEFAULT and not an override, and no test of "did the file exist"
; is needed anywhere: a SYSTEM.CFG that deserialises calls drv_want_set, which
; overwrites all three WANT bytes with the file's bitmap, so the user's answer
; always wins. A missing or malformed file leaves the defaults standing, and
; on a machine with a card the default is now "load it".
;
; SPEC.md 51.3 says the kernel does not GUESS, and it still does not: what it
; refused to do was read 5.5KB of driver off a floppy - 27 sectors, ~6.4 s on
; the field machine - to be told there was no card. This costs about a
; millisecond of port I/O and only ever asks for the read on a machine that
; answered.
;
; ONE probe covers both cards. Every Sound Blaster ever made carries an OPL2
; of its own at 388h - which is why the driver's own attach probes the FM half
; FIRST and lets the SB rename the Control Panel row afterwards (SPEC.md 34.2).
; The DSP reset scan is six bases x ~10ms plus a ~100ms slow retry, all of it
; paid on every boot of every machine with no card, and it would find nothing
; the timer-flag dance below has not already found on any real hardware.
;
; The dance is SPEC.md 34.2's, exactly: mask + reset the timers, read the flags
; (they must be clear), run timer 1 for >= 80us, read them again (they must
; have fired), and clean up so the chip is left as it was found. That last part
; is what makes this safe to run ahead of the driver's own probe.
; -----------------------------------------------------------------------------
drv_snd_sniff:
    push ax
    push bx
    push cx
    push dx
    mov ax, 0x0460              ; 04h <- 60h: mask both timers
    call drvp_wr
    mov ax, 0x0480              ; 04h <- 80h: reset the IRQ flags
    call drvp_wr
    mov dx, DRVP_OPL
    in al, dx
    mov bl, al                  ; s1: the flags must read clear
    mov ax, 0x02FF              ; timer 1 count FFh (overflows in 80us)
    call drvp_wr
    mov ax, 0x0421              ; start timer 1, timer 2 masked
    call drvp_wr
    mov cx, 200                 ; >= 80us of counted status reads on any bus
.wait:
    in al, dx
    loop .wait
    in al, dx
    mov bh, al                  ; s2: overflow + timer-1 flags must be set
    mov ax, 0x0460              ; clean up: mask + reset, leaving the flags
    call drvp_wr                ; clear for the driver's own probe
    mov ax, 0x0480
    call drvp_wr
    and bl, 0xE0
    jnz .nofm                   ; an absent bus floats, so a NON-zero s1 is
    and bh, 0xE0                ; the commonest way this ends
    cmp bh, 0xC0
    jne .nofm
    mov byte [drv_tab+DRVR_WANT], 1   ; row 0 is the sound driver. Through DS,
                                ; which is KERNEL_SEG in here (SPEC.md 2.5)
    jmp short .out
.nofm:                          ; No OPL2. On real hardware that settles it -
                                ; every Sound Blaster has one - so what follows
                                ; is built only under SNDSNIFF=sb, and with the
                                ; knob unset this label simply falls into .out
%ifdef SND_SNIFF_SB
    push si                     ; --- the DSP reset scan (SPEC.md 34.2) ------
    mov si, drvp_sbbase         ; ...read through CS: the overlay may not reach
    cld                         ; its own data through DS (SPEC.md 2.5)
.base:
    cs lodsw
    or ax, ax
    jz .sbno                    ; list exhausted: no card
    mov bx, ax
    mov cx, 2000                ; ~10ms of ISA status reads per base
    call drvp_reset
    jc .base
    mov byte [drv_tab+DRVR_WANT], 1
.sbno:
    pop si
%endif
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    retf                        ; kmain reaches this as FAT_SEG:drv_snd_sniff

; -----------------------------------------------------------------------------
; drvp_wr - write one OPL2 register (overlay-local; SPEC.md 34.1)
; in:  AH = register index, AL = value
; out: nothing (all registers preserved)
;
; The driver owns 388h/389h and opl_wr is the only routine allowed to touch
; them (SPEC.md 34.1) - this is the one exception, and it is bounded: it runs
; before any driver exists, it is a byte-for-byte copy of that protocol, and
; the overlay it lives in is gone before the driver can be loaded. Select via
; 388h, 6 counted status reads, data to 389h, 35 more.
; -----------------------------------------------------------------------------
drvp_wr:
    push ax
    push cx
    push dx
    pushf
    cli
    mov dx, DRVP_OPL
    xchg al, ah                 ; AL = register (value parked in AH)
    out dx, al
    mov cx, 6
.post_addr:
    in al, dx
    loop .post_addr
    mov al, ah
    inc dx                      ; 389h
    out dx, al
    popf                        ; the long delay runs at the caller's IF
    dec dx
    mov cx, 35
.post_data:
    in al, dx
    loop .post_data
    pop dx
    pop cx
    pop ax
    ret

%ifdef SND_SNIFF_SB
drvp_sbbase: dw 0x220, 0x240, 0x210, 0x230, 0x250, 0x260, 0
                                ; the pinned scan order (SPEC.md 34.2), read
                                ; through CS - see the overlay contract

; -----------------------------------------------------------------------------
; drvp_reset - reset the DSP at BX and wait for 0AAh (overlay-local)
; in:  BX = the card's base, CX = poll budget
; out: CF = 0 a DSP answered; CF = 1 nothing there
; clobbers: AL, CX (flags)
; -----------------------------------------------------------------------------
drvp_reset:
    push dx
    mov dx, bx
    add dx, 6                   ; 2x6h: reset
    mov al, 1
    out dx, al
    push cx
    add dx, 8                   ; hold >= 3us: 8 ISA reads of 2xEh
    mov cx, 8
.dly:
    in al, dx
    loop .dly
    sub dx, 8
    pop cx
    xor al, al
    out dx, al
    add dx, 8                   ; 2xEh: data-available status
.poll:
    in al, dx
    test al, 0x80
    jz .next
    sub dx, 4                   ; 2xAh: read the identity byte
    in al, dx
    add dx, 4
    cmp al, 0xAA
    je .yes
.next:
    loop .poll
    stc
    jmp short .ret
.yes:
    clc
.ret:
    pop dx
    ret
%endif

section .cold                   ; ...and back to cold code: drv_boot and
                                ; drv_notice are the boot-time half, run once
                                ; from kmain, and could not be more cold

; -----------------------------------------------------------------------------
; drv_boot - load every driver the settings file asks for
; in:  nothing
; out: nothing (all registers preserved)
;
; Runs before the desktop's first paint. Nothing here can stop the boot: a
; disk that is not there, a file that is not on it and a card that is not in
; the machine are all recorded in the row and reported afterwards.
; -----------------------------------------------------------------------------
drv_boot_x:
    push ax
    push bx
    push cx
    xor ch, ch                  ; CH = 1 once a settings RECORD has been read
    call drv_mounted
    jc .nodisk
    call drv_cfg_load           ; CF = 1: no file, so every default stands -
    jc .load                    ; and every default is now "not wanted", so
    mov ch, 1                   ; the loop below correctly loads nothing

    ; --- the display adapter, FIRST (SPEC.md 39.11.3) ------------------------
    ; Before the driver loads, at the bottom of
    ; this routine, because everything after it draws: the splash bar is
    ; ticked once per sector by dsk_xfer (SPEC.md 15.3), and vid_setmode
    ; CLEARS the framebuffer - so the switch has to happen while there is the
    ; least possible loading screen to put back, and spl_chrome puts that back
    ; immediately. vid_switch refuses a card this machine does not have, which
    ; is the whole safety story for a settings disk carried between machines.
%ifdef KERN_BIG
    ; SPEC.md 39.19's two bytes go in FIRST and survive a refused switch:
    ; vid_switch calls vid_disp_init, so a switch that takes carries them for
    ; free - and one that does not still has to honour them, which is what the
    ; .vnosw path below is. Both are range-checked on CFG_SCH's terms.
    mov al, [drv_cfg+CFG_VDM]
    cmp al, 1
    ja .vdone
    mov [vid_dmode], al
.vdone:
    mov al, [drv_cfg+CFG_VDL]
    cmp al, VID_L_BELOW
    ja .vdone2
    mov [vid_dlay], al
.vdone2:
%endif
    ; THE ADAPTER, AND THEN WHATEVER THE SWITCH DID NOT DO. vid_switch answers
    ; CF = 0 both for a switch that HAPPENED and for one that was not needed,
    ; and only the first re-runs vid_disp_init - so [vid_kind] is banked and
    ; compared afterwards, which asks the machine what changed instead of
    ; predicting it. A second copy of vid_switch's own "am I already that
    ; adapter" test here could only ever disagree with it; this cannot.
    mov al, [drv_cfg+CFG_VID]
%ifdef KERN_BIG
    mov ah, [vid_kind]
%endif
    call KERNEL_SEG:cw_vid_switch
    jc .vnosw                   ; no such adapter here: stay on the probe's
%ifdef KERN_BIG                 ; answer
    cmp ah, [vid_kind]
    je .vnosw                   ; already that adapter: nothing was cleared
%endif
    call KERNEL_SEG:cw_spl_reset              ; ...otherwise redraw the loading screen onto
    jmp short .load             ; the framebuffer the mode set just wiped
.vnosw:
%ifdef KERN_BIG
    ; ...and SPEC.md 39.19's ARRANGEMENT, which nothing else on this path
    ; applies: the adapter was already right, or was refused, and neither is a
    ; statement about the desktop. No spl_reset - the primary's card is never
    ; re-programmed here, so the loading screen is untouched.
    call KERNEL_SEG:cw_vid_disp_init
%endif
.load:
    xor cl, cl
.next:
    mov al, cl
    call drv_row_x
    cmp word [bx+DRVR_FILE], 0
    je .skip
    cmp byte [bx+DRVR_WANT], 0
    je .skip
    mov al, cl
    call drv_load_x
    jnc .skip
    mov byte [drv_nerr], 1      ; something to say once there is a screen
.skip:
    inc cl
    cmp cl, DRV_MAX
    jb .next
    jmp short .out              ; THE LOADS ARE DONE - and .nodisk is below,
                                ; so without this the successful path walks
                                ; straight into the failure path and stamps
                                ; DRVE_DISK over every row it just loaded.
                                ; That is exactly what happened: SPEC.md 32's
                                ; double-buffering removal took out a bb_set
                                ; call whose `jmp short .out` was this one,
                                ; and the orphaned comment it left behind -
                                ; about a 150KB buffer seeded from VRAM - was
                                ; the tell. Anything added between the loop
                                ; and .nodisk needs this to stay last

.nodisk:
    ; No system disk at all. Only a row the user actually wanted counts as a
    ; failure: a machine that has never enabled a driver must not be told
    ; about a floppy it does not need.
    xor cx, cx
.dnext:
    mov al, cl
    call drv_row_x
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
drv_notice_x:
    push ax
    cmp byte [drv_nerr], 0
    je .out
    mov byte [drv_nerr], 0
    mov byte [cp_sel], CP_IDRV
    call cp_open_x              ; SPEC.md 2.8.4. drv_notice runs BEFORE
                                ; ui_task starts, so the system disk is still
                                ; the one the machine booted from and this
                                ; load is as reliable as the boot was - which
                                ; is what makes the panel affordable as a
                                ; module at all (docs/ONDEMAND-PLAN.md 2.2).
                                ;
                                ; The _x BODY, near: driver.inc is cold now
                                ; and so is ctrl.inc, so routing this out to
                                ; the .text thunk and straight back in would
                                ; be SPEC.md 2.6's double crossing
.out:
    pop ax
    ret

; =============================================================================
; Far entries (SPEC.md 2.6): the resident thunks in kernel.asm far-call these,
; and these near-call the bodies above, so every routine here keeps its
; ordinary near `ret`.
; =============================================================================
dvf_drv_blk_call:  call drv_blk_call_x
               retf
dvf_drv_boot:      call drv_boot_x
               retf
dvf_drv_cfg_save:  call drv_cfg_save_x
               retf
dvf_drv_cls_fp:    call drv_cls_fp_x
               retf
dvf_drv_cls_svc:   call drv_cls_svc_x
               retf
dvf_drv_cp_call:   call drv_cp_call_x
               retf
dvf_drv_cp_class:  call drv_cp_class_x
               retf
dvf_drv_cp_closed: call drv_cp_closed_x
               retf
dvf_drv_cp_count:  call drv_cp_count_x
               retf
dvf_drv_cp_name:   call drv_cp_name_x
               retf
dvf_drv_init:      call drv_init_x
               retf
dvf_drv_load:      call drv_load_x
               retf
dvf_drv_notice:    call drv_notice_x
               retf
dvf_drv_owns_seg:  call drv_owns_seg_x
               retf
dvf_drv_row:       call drv_row_x
               retf
dvf_drv_shutdown:  call drv_shutdown_x
               retf
dvf_drv_status:    call drv_status_x
               retf
dvf_drv_svc_call:  call drv_svc_call_x
               retf
dvf_drv_task:      call drv_task_x
               retf
dvf_drv_tier:      call drv_tier_x
               retf
dvf_drv_unload:    call drv_unload_x
               retf
dvf_osapi_drv_cfg: call osapi_drv_cfg_x
               retf


; -----------------------------------------------------------------------------
section .bss

drv_nerr:   resb 1              ; 1 = drv_notice owes the user a window
drv_blkcls: resb 1              ; the class whose DSV_BLK drv_blk_call is to
                                ; dispatch, set by dsk_xfer from DV_CLASS
drv_wcnt:   resb 1              ; live driver worker tasks: +1 inside the
                                ; spawn's IF=0 window, -1 inside task_exit's
                                ; (drv_task). drv_unload waits for 0 before
                                ; the image is freed (SPEC.md 51.7)
drv_fsz:    resw 1              ; the driver file's byte size
drv_owner:  resw DRVC_MAX       ; per class: the row whose services are
                                ; published, or 0. Class 1 is index 0
drv_cpname: resb DRV_CPNSZ*DRVC_MAX ; each class's staged Control Panel page
                                ; name; empty = that class has no page
drv_svc:    resb DSV_SIZE*DRVC_MAX  ; ...and a copy of each one's table, so
                                ; every `[drv_svc+DSV_*]` in snd.inc still
                                ; names the SOUND driver's and did not change
drv_cfg:    resb CFG_NB         ; the live settings, staged (layout above)
drv_cfgbuf: resb CFG_FBUF       ; the file itself, records and all

section .text

section .cold                   ; ...the far entries themselves live HERE, or
                                ; their `call <body>` would cross too
; -----------------------------------------------------------------------------
; Far entries for the ON-DEMAND MODULES (SPEC.md 2.8)
;
; The Control Panel is a module now, and its Drivers page is most of what
; reaches in here - eleven bodies, twenty-one call sites. Every one of them
; was a near call the day driver.inc went cold and ctrl.inc was still cold
; with it; the module boundary is what made them far again.
;
; A module is `.cold` in every respect but its address: it runs from a heap
; claim with a CS of its own, so a near call from `.modc`/`.modf` into this
; segment is the bug tools/os88ovlchk.py exists to refuse - the same one
; SPEC.md 2.6 refuses between `.text` and `.cold`. These are `dwf_*`'s idiom
; with a module on the other end instead of `.text`, and they are four bytes
; each because the bodies keep the near `ret` they have always had.
; -----------------------------------------------------------------------------
drvf_drv_row:            call drv_row_x
                    retf
drvf_drv_status:         call drv_status_x
                    retf
drvf_drv_cp_call:        call drv_cp_call_x
                    retf
drvf_drv_cls_svc:        call drv_cls_svc_x
                    retf
drvf_drv_unload:         call drv_unload_x
                    retf
drvf_drv_tier:           call drv_tier_x
                    retf
drvf_drv_load:           call drv_load_x
                    retf
drvf_drv_cp_name:        call drv_cp_name_x
                    retf
drvf_drv_cp_count:       call drv_cp_count_x
                    retf
drvf_drv_cp_class:       call drv_cp_class_x
                    retf
drvf_drv_cfg_save:       call drv_cfg_save_x
                    retf

section .text
