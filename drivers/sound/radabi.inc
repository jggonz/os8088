; =============================================================================
; os8088 - drivers/sound/radabi.inc
;
; The private interface between SOUND.DRV and RADPLAY.DRV, its on-demand half
; (SPEC.md 34.12.8). Both images assemble their own copy of everything here,
; which is SPEC.md 52.11.1's rule for HDD.DRV and HDDTOOL.DRV: a constant costs
; nothing to duplicate, and a verb number is an ABI that must never be reused.
; RAD_ABI_VER rides the overlay header's spare word at +10 and the resident
; refuses an image built against a different copy of this file - a half-copied
; floppy is exactly that.
; =============================================================================

RAD_ABI_VER     equ 3           ; 2: RADV_DISARM (SPEC.md 34.12.2's windows)
                                ; 3: RADS_HOOK / RADS_UNHOOK (34.13.3)

; --- the claim (SPEC.md 34.12.5) ---------------------------------------------
; One pinned block from the top: the overlay image at +0 with its working set
; after it, the tune at RAD_TUNE, the private stack at the end.
RAD_TUNE        equ 13312       ; where the file starts in the claim. A
                                ; CEILING on image + working set that
                                ; radplay.asm asserts at assembly time - less
                                ; RAD_READ_HEAD, below
RAD_READ_HEAD   equ 128         ; the head room OSAPI_FILE_READ needs ABOVE the
                                ; file it expands (apps/os88api.inc: ~80 bytes,
                                ; the packed bytes read high and expanded
                                ; downwards). rad_read hands it RAD_TUNE as the
                                ; capacity, so an image within RAD_TUNE but
                                ; inside those bytes assembles silently and
                                ; answers FERR_BIG -> RADE_NOPLAYER off every
                                ; PKGZ=lz4 disk, which is every shipped one
RAD_STK         equ 1024        ; the replay step's own stack
RAD_WORK        equ RAD_TUNE + RAD_STK
RAD_FRMAX       equ 7           ; frames one pacer interrupt may run (34.13.5)

; --- verbs, resident -> overlay: AL, through the overlay's dispatcher --------
; BP = the entry offset out of the overlay header, DS = the claim. Before
; RADV_LOAD the resident fills the RO_* block below; nothing else is passed.
RADV_LOAD       equ 1           ; validate the COPY at RAD_TUNE (RO_LEN bytes)
                                ; and parse it. out CF = 0 AL = version,
                                ; CX = tenths; CF = 1 AL = RADE_*, AH =
                                ; RADC_*, CX = offset
RADV_START      equ 2           ; out CF = 0, or CF = 1 AL = RADE_PACER (the
                                ; overlay has written the stop sequence)
RADV_PAUSE      equ 3           ; out CF = 0
RADV_RESUME     equ 4           ; out CF = 0 or CF = 1 AL = RADE_PACER
RADV_STOP       equ 5           ; stop sequence + default patch, at the
                                ; caller's IF. A no-op when stopped. CF = 0
RADV_STAT       equ 6           ; ES:DI -> buffer, CX = bytes (tested by the
                                ; resident); copies under cli, clears the
                                ; burst, re-asserts PIE on the RTC class
RADV_KILL       equ 7           ; DSV_RELINST / detach at IF = 0: disarm,
                                ; silence through the shadow, 104h/105h, the
                                ; default patch - opl_wrf throughout
RADV_TICK       equ 8           ; inside IRQ0 at IF = 0: the tick-class pacer.
                                ; DS is the RESIDENT's here; AX, DX free
RADV_DISARM     equ 9           ; the pacer disarmed inside one cli window, so
                                ; no HALT can follow: verb 5 start's step 0,
                                ; before the resident claims (34.12.2). CF = 0

; --- services, overlay -> resident: AL, far call to RO_RSEG:PKG_DISP with
; BP = RO_RSVC -----------------------------------------------------------------
RADS_HALTED     equ 0           ; the pacer halted the tune at interrupt time:
                                ; release channels 0..7 and the chip. IF = 0
RADS_HOOK       equ 1           ; int 70h <- the RESIDENT's rad_i70, IF = 0.
                                ; in:  DS = the claim, BX = its handler, CX =
                                ;      its post-chain entry
                                ; out: BX = the resident's chain entry, CX =
                                ;      its exit (segment: RO_RSEG)
RADS_UNHOOK     equ 2           ; int 70h restored, the armed byte cleared.
                                ; IF = 0

; --- the fixed block after the overlay's 32-byte header ----------------------
RO_STATE        equ 32          ; byte, overlay-written: 0 none / 1 stopped /
                                ;       2 playing / 3 paused (RST_STATE's)
RO_CHIP         equ 33          ; byte, overlay-written: 1 = holds the chip
RO_CLASS        equ 34          ; byte, resident-written: 0 tick / 1 RTC
RO_IS3          equ 35          ; byte, resident-written: [opl_is3]
RO_RSEG         equ 36          ; word, resident-written: SOUND.DRV's segment
RO_RSVC         equ 38          ; word, resident-written: rad_svc's offset
RO_TOP          equ 40          ; word, resident-written: the claim's end
                                ;       (KB x 1024; 0 = 64KB) - the stack top
RO_LOGSEG       equ 42          ; word, resident-written: RADLOG buffer or 0
RO_LEN          equ 44          ; word, resident-written: the file's length
RO_END          equ 46

; A RADLOG buffer (a -DRADLOG build only, SPEC.md 96.8): word +0 = bytes of
; records written (bit 15 set = the buffer filled and a record was dropped),
; then 3-byte .RLG records from +2.
RAD_LOGKB       equ 32
