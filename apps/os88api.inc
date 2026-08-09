; =============================================================================
; os8088 - apps/os88api.inc
;
; The program-side SDK for .o88 packages (SPEC.md 20.5). Included by package
; sources only - never by the kernel. Provides the pinned os8088 API jump-table
; offsets (SPEC.md 20.3), the window record and wm_create template layouts
; (SPEC.md 11), the 16 EGA colour indices, the loaded-program region
; constants (SPEC.md 3/20.1), and the packaging macros, so a package source
; can be just:
;
;   %include "os88api.inc"
;   OS88_HEADER 'NAME', entry        ; emits cpu/bits/org + the 32-byte header
;                                   ; (optional 3rd arg = flags byte, SPEC.md
;                                   ; 20.2: bit 0 = embedded icon follows)
;   OS88_ICON16                      ; only with flags bit 0: 32 dw rows
;   ... 16 mask rows, 16 data rows  ; (bit 15 = leftmost pixel)
;   OS88_ICON16_END
;   ... code and data ...
;   OS88_BSS totalbytes              ; optional; must precede OS88_IMAGE_END
;   OS88_IMAGE_END                   ; defines OS88_IMAGE_SIZE (and bss = 0
;                                   ; when no OS88_BSS was given)
;
; OS88_BSS must come before OS88_IMAGE_END because the "default to 0" choice
; is made by OS88_IMAGE_END at preprocess time; the loader zeroes that many
; bytes after the image, so a package addresses its bss with equ offsets
; from the os88_image_end label that OS88_IMAGE_END defines.
;
; Packages run in the kernel's near model: CS = DS = KERNEL_SEG, but
; SS = LOW_SEG - every task stack lives OUTSIDE the kernel segment (just
; ABOVE it since SPEC.md 2), so SS != DS and `[bp+disp]` addresses SS, not
; your data. A pointer held in BP needs an explicit `ds:` override. That has
; always been true inside a callback (you run on the UI task's stack) and
; matters more now that a package can own a worker task of its own
; (OSAPI_TASK_SPAWN below), whose 256-byte stack is likewise in LOW_SEG -
; and 256 is a budget to respect, not a suggestion: the tick, the mouse and
; any sound IRQ land their frames on it while your worker runs (SPEC.md
; 20.6 rule 6; tests/stackprobe measures the real cost on real hardware).
;
; A package is assembled at org 0 and OWNS A SEGMENT (SPEC.md 20.1): the
; loader puts it on a paragraph boundary and hands it its own CS = DS, so
; nothing in the image depends on where it landed. There is no relocation
; table, no dual assembly, and no rule about how you may embed an address -
; all three were the price of the shared pool and went with it. Several
; packages, or several instances of one, can be resident at once; each is
; its own copy with its own bss.
;
; Every OSAPI_* symbol below is a %define of KERNEL_SEG:offset, so
; `call OSAPI_X` is a FAR call and you write it exactly as you always did.
; Register contracts are the target routines' own (SPEC.md 5, 6, 8, 11,
; 20.4); all of them preserve every register except their documented
; outputs, and all of them restore DS.
; =============================================================================

; --- environment ---------------------------------------------------------------
; A package owns a SEGMENT (SPEC.md 20.1). CS = DS = that segment, which the
; loader picks out of the pool; SS = LOW_SEG, shared with every task, so
; `[bp+disp]` still addresses SS and a BP-held pointer still needs a `ds:`
; override. ES is YOURS to use - but on entry to any callback the kernel
; points it at KERNEL_SEG, because the two things it hands you live there:
; the window record OSAPI_WM_CREATE returned, and the file dialog's name
; buffer. Read them with an `es:` override; clobber ES freely afterwards.
;
; Your image is linked at org 0 and loaded at a paragraph boundary, so it is
; position-independent: there are no relocations, and two instances of one
; package differ only in the segment they run in.
KERNEL_SEG   equ 0x0060     ; where the kernel is, and what ES holds on entry.

; The debug registry's tags (SPEC.md 57) - for a TEST package only. One word
; at 0060:000E names a list of (tag, offset) pairs ended by tag 0; the tag is
; also the block's own first word. Shipped software has no business here: a
; block is kernel internals with no compatibility promise, which is exactly
; why it is not an API slot.
DBG_TAG_MOUSE equ 0x4F4D          ; 'MO' - SPEC.md 9.4.2, unconditional
DBG_TAG_DISK  equ 0x4444          ; 'DD' - SPEC.md 18.94, `make DISKCNT=1`
DBG_TAG_CLOCK equ 0x4B43          ; 'CK' - SPEC.md 37.92, unconditional
                            ; It is baked into every OSAPI_* far call, so a
                            ; package built against a different value calls
                            ; into empty memory: rebuild EVERY .o88 (and the
                            ; apps floppies) whenever the kernel moves
APP_MAX_SIZE equ 0xF000     ; image + bss budget: 60KB. The ceiling is the
                            ; SEGMENT, not a pool: a package links at org 0
                            ; and addresses itself with 16-bit offsets. Its
                            ; region is an ordinary heap claim, so what it
                            ; can ACTUALLY get also depends on the heap
; The REFERENCE screen, not a promise. Since SPEC.md 39 the kernel drives
; three adapters and picks one at boot: VGA 640x480x16, Hercules 720x348
; mono, or CGA 640x200 mono. Ask OSAPI_VIDEO for the one you actually got.
; A window template is still authored against these numbers - wm_create
; clamps the frame onto the live screen, so an oversized window is placed
; and clipped rather than lost - but anything that reads the screen edge at
; RUN time (a right-anchored control, a bottom-anchored status line) must
; use OSAPI_VIDEO or it will be wrong on two adapters out of three.
SCREEN_W     equ 640
SCREEN_H     equ 480
MBAR_H       equ 20         ; menu bar height, px
TITLE_H      equ 18         ; window title bar height, px

; --- colours (standard EGA/VGA 16-colour indices) ------------------------------
CBLACK    equ 0
CBLUE     equ 1
CGREEN    equ 2
CCYAN     equ 3
CRED      equ 4
CMAGENTA  equ 5
CBROWN    equ 6
CLGRAY    equ 7             ; DECORATION grey - Minesweeper's covered cells,
                            ; Arkanoid's rails, Recorder's centre line
CDGRAY    equ 8             ; the disabled pen (SPEC.md 47). GREY THE WHOLE
                            ; CONTROL - frame, ring, box, icon AND label -
                            ; never just the caption.
                            ;
                            ; **Set it with OSAPI_GFX_PEN and not with
                            ; OSAPI_SET_COLOR**, because a disabled control is
                            ; two pieces of state: this colour AND the
                            ; [gfx_dis] flag. The flag is what makes your
                            ; LABEL a checkerboard on Hercules and CGA -
                            ; legible, and plainly different from a live one.
                            ; The colour alone rounds to solid black there, so
                            ; a button set with OSAPI_SET_COLOR comes out with
                            ; a dotted frame around a black caption: the two
                            ; halves of one control disagreeing, which is
                            ; exactly what rule 2 forbids. Every package in
                            ; this tree shipped that until slot 0x0310 was
                            ; published. rc_btn in apps/recorder is the
                            ; reference
CLBLUE    equ 9
CLGREEN   equ 10
CLCYAN    equ 11
CLRED     equ 12
CLMAGENTA equ 13
CYELLOW   equ 14
CWHITE    equ 15

; --- the os8088 API jump table (SPEC.md 20.3, pinned offsets) ---------------------
; Each slot is an 8-byte cell at 0x0010 + 8n, and `call OSAPI_*` is a FAR
; call: the %defines below expand to `call KERNEL_SEG:0xNNNN`, so package
; source is written exactly as it always was and the segment change is
; invisible. Every cell switches DS to the kernel's on the way in and back to
; yours on the way out; every register except a documented output is
; preserved, flags included.
%define OSAPI_GFX_LOCK      KERNEL_SEG:0x0010   ; acquire drawing mutex + hide cursor
%define OSAPI_GFX_UNLOCK    KERNEL_SEG:0x0018   ; show cursor + release mutex
%define OSAPI_GFX_PIXEL     KERNEL_SEG:0x0020   ; CX=x, DX=y
%define OSAPI_GFX_HLINE     KERNEL_SEG:0x0028   ; AX=x1, BX=x2, DX=y
%define OSAPI_GFX_VLINE     KERNEL_SEG:0x0030   ; AX=x, BX=y1, DX=y2
%define OSAPI_GFX_FILL      KERNEL_SEG:0x0038   ; AX=x1, BX=y1, CX=x2, DX=y2
%define OSAPI_GFX_FRAME     KERNEL_SEG:0x0040   ; AX=x1, BX=y1, CX=x2, DX=y2
%define OSAPI_GFX_FILL_GRAY KERNEL_SEG:0x0048   ; AX=x1, BX=y1, CX=x2, DX=y2 (50% dither)
%define OSAPI_GFX_XOR_RECT  KERNEL_SEG:0x0050   ; AX=x1, BX=y1, CX=x2, DX=y2
%define OSAPI_GFX_XOR_FILL  KERNEL_SEG:0x0058   ; AX=x1, BX=y1, CX=x2, DX=y2
%define OSAPI_FONT_CHAR     KERNEL_SEG:0x0060   ; CX=x, DX=y, AL=char
%define OSAPI_FONT_STR      KERNEL_SEG:0x0068   ; CX=x, DX=y, SI=NUL string
%define OSAPI_FONT_WIDTH    KERNEL_SEG:0x0070   ; SI=NUL string; out AX = pixel width
%define OSAPI_WM_CREATE     KERNEL_SEG:0x0078   ; SI=template; out BX = win ptr, CF on full
%define OSAPI_WM_SHOW       KERNEL_SEG:0x0080   ; BX=win ptr
%define OSAPI_WM_HIDE       KERNEL_SEG:0x0088   ; BX=win ptr
%define OSAPI_WM_FRONT      KERNEL_SEG:0x0090   ; BX=win ptr
%define OSAPI_WM_CONTENT    KERNEL_SEG:0x0098   ; BX=win ptr; out AX=content left, DX=top
%define OSAPI_WM_OBSCURED   KERNEL_SEG:0x00A0   ; BX=win ptr; out CF=1 if covered
%define OSAPI_TASK_YIELD    KERNEL_SEG:0x00A8   ; give up the time slice
%define OSAPI_TASK_SLEEP    KERNEL_SEG:0x00B0   ; AX = ticks to sleep (18 ~ 1 second)
%define OSAPI_GET_TICKS     KERNEL_SEG:0x00B8   ; out AX = [ticks]
%define OSAPI_SET_COLOR     KERNEL_SEG:0x00C0   ; AL -> [gfx_color]
%define OSAPI_MOUSE         KERNEL_SEG:0x00C8   ; out CX=mouse_x, DX=mouse_y, AL=mouse_btn
%define OSAPI_SRAND         KERNEL_SEG:0x00D0   ; AX -> [osapi_seed]
%define OSAPI_RAND          KERNEL_SEG:0x00D8   ; out AX = next pseudo-random word

; --- sound (SPEC.md 20.3/34): the PC speaker, which is the whole of it.
%define OSAPI_SND_CAPS      KERNEL_SEG:0x00E0   ; out AX = caps word (SND_CAP_*),
                                 ; BL = 0 (the speaker), DX = 1 (present)
%define OSAPI_SND_TONE      KERNEL_SEG:0x00E8   ; AX = freq Hz (0 = off), CX =
                                 ; duration ticks (0 = until off), DL =
                                 ; priority (default 0x40); out CF=1 refused,
                                 ; else AL = owner generation
%define OSAPI_SND_PLAY      KERNEL_SEG:0x00F0   ; PCM_EXCL clip (SPEC.md 34.4):
                                 ; ES:SI = your 8-bit unsigned samples, CX =
                                 ; count, DX = rate 4,679..16,124 Hz; out
                                 ; AX = 0 ok / 1..5 err. Runs on your task
                                 ; with the scheduler locked: it FREEZES the
                                 ; desktop for the length of the clip, and a
                                 ; mouse click aborts it (err 5)

%define OSAPI_SND_FM        KERNEL_SEG:0x00F8   ; FM verbs (SPEC.md 34.2), live
                                 ; only while a sound DRIVER is loaded
                                 ; (SPEC.md 51.4) - CF=1 when none is.
                                 ; AL = verb: 0 note-on (CL = channel 0..7,
                                 ; BX = Hz 19..6208), 1 note-off (CL),
                                 ; 2 patch-load (CL, DS:SI -> 11 bytes: 5
                                 ; modulator regs, 5 carrier, then C0h -
                                 ; STAGED for you, so SI is yours), 3 all-off.
                                 ; A channel is claimed by its first note-on
                                 ; or patch-load and is yours until you call
                                 ; verb 3 or the instance is torn down;
                                 ; CHANNEL 8 IS NOT AVAILABLE - it is the tone
                                 ; tier's voice (SPEC.md 34.8).
; OSAPI_SND_STREAM's context rule is PER-VERB (SPEC.md 20.3). Verbs 1
; (feed), 3 (status), 5 (read), 6 (stage) and 7 (grant) are ANY-TASK: a
; window callback or YOUR OWN worker may call them - the driver runs their
; check-then-act sequences under single cli windows, threads the verb-5/6
; segment per call, and attributes grants task-correctly, so a worker can
; mix -> stage -> feed -> poll a stream it owns. Verbs 0 (open-out), 2
; (close) and 4 (open-in) stay UI-CALLBACK-ONLY: their busy/half-duplex
; refusals rest on every OPENER being serialized on the UI task. Open and
; close from W_ONKEY, a menu handler or an fdlg completion proc.
; One author rule (SPEC.md 34.6): NEVER verb-7-free a grant your worker may
; be mid-stage into - free it from the worker itself, or only after your own
; handshake says it is parked.
%define OSAPI_SND_STREAM    KERNEL_SEG:0x0100   ; PCM_BG streams + staging: the
                                 ; driver's too, and likewise CF=1 without
                                 ; one. AL = verb (0 open-out / 1 feed /
                                 ; 2 close / 3 status / 4 open-in / 5 read /
                                 ; 6 stage / 7 grant), AH = handle or sub-op
                                 ; (verb 0: OPEN FLAGS - bit 0 =
                                 ; SND_OPENF_RING below; every linear open
                                 ; passes AH = 0); out AX = 0 ok / err /
                                 ; SND_ST_* state (verb 3). Verb 6's SI
                                 ; source and verb 5's DI dest are in YOUR
                                 ; segment - STAGED for you, like verb 2 of
                                 ; OSAPI_SND_FM.

; capability bits returned by OSAPI_SND_CAPS (SPEC.md 34.2). TONE and
; PCM_EXCL are the resident PC speaker's and are always there; the rest
; appear only while a sound driver is loaded (SPEC.md 51.4).
SND_CAP_TONE     equ 01h
SND_CAP_FM       equ 02h
SND_CAP_PCM_BG   equ 04h
SND_CAP_PCM_EXCL equ 08h
SND_CAP_PCM_IN   equ 10h

; stream states returned by OSAPI_SND_STREAM verb 3 (SPEC.md 20.3/34.5)
SND_ST_PLAYING   equ 0
SND_ST_UNDER     equ 1           ; underrun-paused: resumable by a feed
SND_ST_ENDED     equ 2           ; stopped by the stream watchdog. A feed
                                 ; is ACCEPTED but the stream NEVER
                                 ; RESUMES - its refill task is gone. On
                                 ; state 2, close, then reopen.
SND_ST_STALE     equ 0FFFFh      ; the handle's stream is gone

; verb-0 open flags, passed in AH (SPEC.md 20.3 verb 0 - verb 0 carries no
; handle, so the byte is free; every pre-ring caller already loads AH = 0)
SND_OPENF_RING   equ 1           ; ring mode (SPEC.md 34.5): SI = ring base,
                                 ; ring length RL = grant end - SI, a POWER
                                 ; OF TWO in 4096..32768; CX = initial valid
                                 ; total, one HALF <= CX <= RL (2,048, or
                                 ; 4,096 above 22,222 Hz). total/fed/
                                 ; consumed become free-running 16-bit
                                 ; (mod 65536): stage stream byte n at
                                 ; ringbase + (n & (RL-1)), and verb 1 still
                                 ; feeds the NEW TOTAL - free-running, so the
                                 ; driver reads it as a wrapping counter, not
                                 ; as a delta.

; --- window management (SPEC.md 11.1/11.2) -------------------------------------
%define OSAPI_WM_SIZABLE    KERNEL_SEG:0x0108   ; BX=win ptr, AL = 0 clear / non-zero set
                                 ; WF_SIZABLE; call from the entry proc
                                 ; after OSAPI_WM_CREATE. A resizable
                                 ; window's paint/click procs MUST lay out
                                 ; from the live W_W/W_H every call.
%define OSAPI_FULLSCREEN    KERNEL_SEG:0x0110   ; AL = 1 enter (BX=win ptr) / 0 exit;
                                 ; caller holds the gfx lock (window
                                 ; callbacks do); out CF=1 enter refused
                                 ; (screen already owned). Exiting is the
                                 ; app's job (Esc in its onkey is the
                                 ; convention) - the menu bar is
                                 ; unreachable while it holds the screen.
%define OSAPI_WM_GROW       KERNEL_SEG:0x0118   ; BX=win ptr; caller holds the gfx lock.
                                 ; A resizable window's self-initiated
                                 ; content repaint must END with this:
                                 ; the white-fill idiom erases the grow
                                 ; box. No-op unless BX is the frontmost
                                 ; visible resizable window - always safe.

; --- files (SPEC.md 18.4/20.3) -------------------------------------------------
; Whole files, by name, in the volume's CURRENT DIRECTORY (SPEC.md 19.2 -
; wherever the Disk window or the file dialog last navigated), not the root.
; UI-task context only: the entry proc, a window callback, a menu handler or
; a file-dialog completion proc. NEVER from your worker task
; (OSAPI_TASK_SPAWN). The kernel cannot enforce this and a second concurrent
; caller corrupts the FAT - the write path shares one sector buffer and the
; FAT snapshot with every read and with the Disk window's mount.
; The buffer is ES:BX (like OSAPI_SND_PLAY), so staged
; sound or an app's own bss both work; set ES = DS for the latter. A write
; stalls painters for its duration: never call these from a paint proc.
;
; ONE read and ONE write, and NEITHER has a 64KB ceiling (SPEC.md 18.4.1).
; The count is DX:CX - 32-bit, DX high - and the buffer's ES:BX is reduced to
; a segment plus an offset under 16 before the transfer, which then walks the
; SEGMENT. So a 1KB staging buffer inside your own bss and a 128KB heap claim
; (ES = OSAPI_MEM_CLAIM's answer, BX = 0) are the same call, and the second
; is what you want for anything that does not fit your region: your image and
; bss together cap at APP_MAX_SIZE.
;
; DX IS AN ARGUMENT TO BOTH, AND AN OUTPUT OF THE READ. `xor dx, dx` before
; every call whose count fits 16 bits, and do not expect DX to survive a
; read - it comes back as the file's size high word.
%define OSAPI_FILE_WRITE    KERNEL_SEG:0x0120   ; SI = NUL 8.3 name, ES:BX = bytes, DX:CX
                                 ; = count (0 = empty file); creates or
                                 ; REPLACES. out CF=0 AX=0, else AX = FERR_*
                                 ; (FERR_BIG = a count no FAT volume this
                                 ; kernel mounts could hold). DX preserved.
%define OSAPI_FILE_READ     KERNEL_SEG:0x0128   ; SI = name, ES:BX = buffer, DX:CX = its
                                 ; capacity; out CF=0 and DX:AX = bytes read
                                 ; (the file's 32-bit size), else AX = FERR_*.
                                 ; FERR_BIG is decided from the directory
                                 ; entry BEFORE any data I/O, so it leaves the
                                 ; buffer alone.
%define OSAPI_FILE_DELETE   KERNEL_SEG:0x0130   ; SI = name; out CF=0 AX=0, else FERR_*
%define OSAPI_FILE_RENAME   KERNEL_SEG:0x0138   ; SI = old name, DI = new name; out as above
%define OSAPI_FILE_DFREE    KERNEL_SEG:0x0140   ; out CF=0, DX:AX = free bytes, BX =
                                 ; SECTORS per cluster; no disk I/O

; --- menus (SPEC.md 12.2) ------------------------------------------------------
; Every application owns the menu bar while its window is frontmost: the
; System (chip) menu stays put, the app's NAME is drawn to its right, and
; the app's own menus follow. Register the set from the entry proc, with
; BX = the window OSAPI_WM_CREATE just returned - the loader shows the
; window afterwards, so the first bar drawn is already yours.
%define OSAPI_MENU_SET      KERNEL_SEG:0x0148   ; BX = win ptr, SI = menu set ptr (0 =
                                 ; none). Draws nothing, takes no lock,
                                 ; preserves every register AND the flags -
                                 ; so it can sit between OSAPI_WM_CREATE
                                 ; and the entry proc's ret without eating
                                 ; the CF that ret owes the loader.

; --- the Standard File dialog (SPEC.md 38) -------------------------------------
; The other half of the file slots above: they operate on a name, and this is
; how you get one from the user. The kernel puts up a modal window listing the
; mounted disk, and calls you back when a name has been chosen.
;
; It does NOT block. A window callback runs under the gfx lock on the UI task,
; so a dialog that pumped events inside your `ret` would have to release a lock
; it does not own. Instead this returns immediately - with the dialog already
; on screen, because you were holding the lock it needed - and your completion
; proc is called much later. Cancel calls nothing at all.
;
; Call it from a menu command or a key handler, never from a paint proc.
%define OSAPI_FILE_DLG      KERNEL_SEG:0x0150   ; AL = 0 Open / 1 Save, BX = your window ptr,
                                 ; DI = completion proc, SI = default name
                                 ; (NUL, <= 12) or 0. out CF=0 accepted, CF=1
                                 ; refused (one is already up, or no room)
FDLG_OPEN  equ 0                 ; the two modes, in AL going in and in AL
FDLG_SAVE  equ 1                 ; coming back
;
%define OSAPI_VIDEO         KERNEL_SEG:0x0158   ; the screen this machine actually has
                                 ; (SPEC.md 39.2). No inputs. Out: AX = width,
                                 ; BX = height, CX = the first row the dock
                                 ; owns (so the usable desktop is rows MBAR_H
                                 ; .. CX-1), DL = 0 VGA / 1 Hercules / 2 CGA,
                                 ; DH = bits per pixel, 4 or 1. Callable from
                                 ; any context, lock held or not.
VID_VGA    equ 0                 ; the DL values
VID_HERC   equ 1
VID_CGA    equ 2
;
; On a 1bpp adapter every colour you pass is reduced to black, white or a
; 50% dither (SPEC.md 39.4): 0..6 go black, 12/14/15 go white, and 7..11
; plus 13 dither. Two colours from the same class are indistinguishable, so
; if a control's state is carried by colour alone, check DH and carry it
; some other way - the shipped apps' palettes were chosen to survive this.
;
; Your completion proc is called on the UI task with the gfx lock HELD, after
; the dialog window is gone:
;
;     in   AL = the mode it ran in, SI = your window ptr, DI = the chosen
;          name (NUL, <= 12 chars), DX:CX = that file's SIZE IN BYTES (0 =
;          this listing has no such file: a typed Save name, a folder).
;          No register need be preserved.
;
; USE THE SIZE TO REFUSE BEFORE YOU TOUCH THE DISK. It is free - the dialog
; reads it out of the mount snapshot already in RAM, the same figure its
; right-hand column shows - and refusing after a read is not: on a 4.77MHz
; machine reading 116KB off a floppy to then say "wrong format" is about ten
; seconds of motor, and the user cannot tell that from a load that works.
; So: check your extension, check the size against what you can hold, check
; it against OSAPI_MEM_AVAIL's LARGEST RUN, and only then claim - EXACTLY
; the size, not the largest run - and read. apps/tracker is the worked
; example, and its refusals stop nothing and free nothing, so a mis-picked
; file does not interrupt what is already playing.
;
; Same rules as a menu command (SPEC.md 12.2): you may draw, you may call the
; file slots above, you must NOT take the lock, and you MUST repaint your own
; content - the kernel does not repaint for you. The name buffer belongs to the
; kernel and is valid for the duration of the call only: copy it.
;
; The dialog navigates the volume, so by the time you are called the current
; directory IS the folder the user chose in, which is where the file slots
; resolve names (SPEC.md 19.2). Just pass the name straight through.

; --- your worker task (SPEC.md 20.6) -------------------------------------------
; Your package may own EXACTLY ONE background task, so the work you do
; between clicks is pre-empted instead of crammed into a callback. This is
; the only place package code runs off the UI task.
;
%define OSAPI_TASK_SPAWN    KERNEL_SEG:0x0160   ; AX = your worker's near entry (a plain
                                 ; `mov ax, my_worker` - a whole-word
                                 ; package address, relocated for you),
                                 ; BX = YOUR window ptr (what
                                 ; OSAPI_WM_CREATE gave you). Call it from
                                 ; a window callback, a menu handler or a
                                 ; file-dialog completion proc, i.e. with
                                 ; the gfx lock HELD - that is required,
                                 ; not merely allowed. BX must be YOUR OWN
                                 ; window and AX an entry inside YOUR OWN
                                 ; image: the kernel checks that the entry
                                 ; falls in the region of the instance BX
                                 ; belongs to, and refuses otherwise, so
                                 ; a borrowed window ptr buys nothing.
                                 ; out CF=1 refused and nothing was
                                 ; created (you already have a worker, or
                                 ; the 12-slot task table is full - a
                                 ; normal outcome: degrade, do not abort,
                                 ; and DO retry later, it is transient);
                                 ; else CF=0 and AL = the task slot. Every
                                 ; other register, AH included, is
                                 ; preserved. NOT callable from your entry
                                 ; proc: the loader has not published your
                                 ; instance yet, so it would simply refuse.
%define OSAPI_TASK_ALIVE    KERNEL_SEG:0x0168   ; BX = YOUR window ptr; the gfx lock must
                                 ; NOT be held, and it must be YOUR WORKER
                                 ; calling - never a window callback.
                                 ; Returns with every register AND the
                                 ; flags preserved while you are still
                                 ; alive. If your close box was clicked it
                                 ; NEVER RETURNS: the kernel destroys your
                                 ; window, frees your instance and
                                 ; releases your region, and your task
                                 ; ends inside this call.

; --- the clip region (SPEC.md 11.3) -------------------------------------------
; What replaced the wm_obscured veto for background painters. Arm it under
; the lock and every gfx_*/font_* call you make is cut to the part of YOUR
; content nothing is covering; a window with one corner buried keeps drawing
; the rest instead of skipping the frame.
%define OSAPI_WM_CLIP_SET   KERNEL_SEG:0x0170   ; BX = YOUR window ptr; the gfx lock must be
                                 ; HELD. out CF = 1: not one pixel of your
                                 ; content is visible - draw nothing this
                                 ; frame (the answer wm_obscured used to
                                 ; give for a single covered pixel). CF = 0:
                                 ; clipping is armed. Preserves every
                                 ; register. It dies at your next
                                 ; OSAPI_GFX_UNLOCK, so it is one call per
                                 ; lock hold and there is nothing to undo.
%define OSAPI_WM_CLIP_CLEAR KERNEL_SEG:0x0178   ; disarm early, to draw unclipped again
                                 ; inside the same lock hold. Preserves
                                 ; every register.
; --- About, under the application's own name (SPEC.md 12.2) --------------------
; Your name in the menu bar is ALWAYS a pull-down (SPEC.md 12.7) and its
; bottom item is always 'Close', which the kernel answers itself - it is the
; close box reached from the bar, and you neither see it nor can decline it.
; What this slot adds is a SECOND item above it: register a handler and the
; menu reads 'About <Name>' then 'Close'. Picking the About calls your
; handler exactly like W_ONCLICK - on the UI task, UNDER THE GFX LOCK, billed
; to your instance - with SI = your window and no selection to decode. Same
; rules, therefore: it may draw and may call the file slots, it must never
; take the lock or block, and it must repaint whatever it drew over.
;
; Call it from your entry proc, after OSAPI_WM_CREATE, like OSAPI_MENU_SET
; (whose flag-preserving contract it shares, so it is safe between wm_create
; and the CF you owe the loader). An application that never calls it gets the
; one-item menu and needs no code at all for it.
%define OSAPI_ABOUT_SET     KERNEL_SEG:0x01E0   ; BX = win ptr, SI = your About
                                 ; handler's offset (0 = none). Preserves every
                                 ; register and the flags

; 0x01E8 is RETIRED and has NO %define (SPEC.md 18.4.1/20.8). It was
; OSAPI_FILE_READBIG, the one file op with no 64KB ceiling; OSAPI_FILE_READ
; has none either now, and the two contracts differed only in where the
; destination's offset came from. The cell is still there and still answers -
; CF=1 with AX = FERR_NAME - because a RELEASED slot keeps its contract and
; "we no longer implement this" is a refusal, never a different routine at
; the same address. Nothing above it moved. A source that still names
; OSAPI_FILE_READBIG fails to assemble, which is the intended way to find
; out: pass your base segment in ES with BX = 0 and call OSAPI_FILE_READ.

%define OSAPI_GFX_DBUF      KERNEL_SEG:0x01F0   ; arm/disarm double buffering
                                 ; for your own drawing (SPEC.md 32): AL = 1
                                 ; on / 0 off, lock HELD. Out CF=0 and AL =
                                 ; the state before, which you hand straight
                                 ; back when you are done so the user's own
                                 ; Control Panel setting survives. CF=1 = you
                                 ; did not get it: a mono adapter (which
                                 ; renders through the same code and has
                                 ; nothing to double) or a heap too small.
                                 ; Refusal is normal - draw unbuffered.
%define OSAPI_GFX_SCROLL    KERNEL_SEG:0x01F8   ; move a rect's contents up or
                                 ; down instead of redrawing them (SPEC.md
                                 ; 5.5): AX/BX/CX/DX = x1/y1/x2/y2 inclusive
                                 ; ABSOLUTE screen coords, SI = signed dy,
                                 ; positive scrolls the content UP. Lock held.
                                 ; x1 and x2+1 must be multiples of 8 - the
                                 ; blit is byte-column granular on every
                                 ; adapter. Out CF=0 moved, and the |dy| rows
                                 ; it vacated are YOURS to repaint; CF=1
                                 ; refused and nothing moved, so fall back to
                                 ; a full repaint. A clip region that does not
                                 ; wholly contain the rect refuses: a blit
                                 ; cannot be cut per pixel (SPEC.md 11.3).

; --- the KB-counting memory slots (SPEC.md 50.3) --------------------------------
; EVERY slot main published keeps main's number and contract - including the
; three paragraph-counting arena slots at 0x01B8..0x01C8 (OSAPI_MEM_ALLOC /
; OSAPI_MEM_FREE / OSAPI_MEM_AVAIL in main's SDK), which are answered by the
; claim heap through converting wrappers, so a package built against main's
; os88api.inc runs on this kernel unchanged. New code should prefer the KB
; slots below: they are the native shape, they work from the entry proc, and
; only they carry the DMA-page and regrow contracts. Everything this branch
; ADDED starts at 0x0200, main's next free number.
%define OSAPI_MEM_CLAIM     KERNEL_SEG:0x0200   ; the claim heap (SPEC.md 50.3):
                                 ; AX = KB wanted; out CF=0 and DX = the base
                                 ; segment, CF=1 refused. This is how you get
                                 ; memory your region cannot hold - a canvas,
                                 ; a sound clip, a decoder's tables. You are
                                 ; identified by the segment you run in, so
                                 ; there is nothing to pass and nothing to
                                 ; forge, and it works from your ENTRY PROC
                                 ; (where you have no window yet) - which is
                                 ; where an app sizes itself. The kernel frees
                                 ; your claims when your instance dies, so you
                                 ; need no teardown hook; free them yourself
                                 ; only to hand memory back mid-session.
%define OSAPI_MEM_CLAIM_DMA KERNEL_SEG:0x0250   ; the same claim, for a buffer
                                 ; an ISA DMA CONTROLLER will read or write:
                                 ; AX = KB wanted, CX = KB of the block's HEAD
                                 ; that must not cross a 64KB PHYSICAL
                                 ; boundary; out CF and DX as above. The 8237
                                 ; has no register for address bits 16..19 -
                                 ; the page port holds them and the chip never
                                 ; carries into them - so a transfer that
                                 ; crosses one wraps to the start of its page
                                 ; and moves the wrong memory. CX is a HEAD and
                                 ; not the whole block because usually only
                                 ; part of a buffer is the chip's; pass CX = AX
                                 ; if all of it is, and CX = 0 for the plain
                                 ; claim. OSAPI_MEM_REGROW is safe on one of
                                 ; these: the record carries the constraint,
                                 ; and a move re-honours it (SPEC.md 50.3).
%define OSAPI_MEM_FREE      KERNEL_SEG:0x0208   ; DX = the segment you were
                                 ; given; out CF=0 released, CF=1 not yours
%define OSAPI_MEM_AVAIL     KERNEL_SEG:0x0210   ; out AX = largest free run in
                                 ; KB, BX = total free KB. Size yourself from
                                 ; THIS, not from int 12h: it is the only
                                 ; number that accounts for what the kernel
                                 ; and every other package already hold.
%define OSAPI_WM_CLIP_TEST  KERNEL_SEG:0x0180   ; AX=x1, BX=y1, CX=x2, DX=y2 (inclusive);
                                 ; out CF=0 the whole rect is drawable (also
                                 ; when no clip is armed), CF=1 it is not.
                                 ; Preserves every register. Ask this before
                                 ; you ERASE a rect you are about to draw
                                 ; text into: the gfx_* fills clip per pixel
                                 ; but font_char clips per whole 8x8 cell, so
                                 ; a wide erase followed by glyphs can white
                                 ; out a band the glyphs then refuse to
                                 ; repaint. Erase per cell, or skip the pair.
%define OSAPI_GFX_BLIT4     KERNEL_SEG:0x01D8   ; ES:SI = packed 4bpp pixels (two
                                 ; per byte, high nibble leftmost), BP = source
                                 ; stride in BYTES, AX/BX = destination x/y,
                                 ; CX/DX = width/height in pixels. Draws the
                                 ; block, coalescing equal-coloured runs into
                                 ; horizontal lines. ES is yours here - no stub
                                 ; is involved, so point it at your own data.
                                 ; This is the call to reach for when you have
                                 ; an image in memory: your gfx calls are FAR
                                 ; calls now, and one of these replaces the
                                 ; hundreds of OSAPI_GFX_HLINE calls a
                                 ; hand-rolled run-length blitter would make.
                                 ; Honours the clip region and the back buffer.
; --- CPU tier and memory above 1MB (SPEC.md 41) --------------------------------
; On a 286 or a 386 the machine has RAM above linear 0x0FFFFF that the 640KB
; conventional map cannot name. These five slots lend you some of it.
;
; READ THIS FIRST, because it is the rule the whole group is shaped around:
; EXTENDED MEMORY IS A DATA STORE ONLY. Real mode forms an address as
; seg<<4 + off, so the highest byte any CS:IP can name is 0x10FFEF - no code
; up there, no jump table, no callback, no second copy of your image, on any
; tier. And you never address it DIRECTLY at all: the 32-bit base
; OSAPI_XMEM_ALLOC hands you is an OPAQUE TOKEN, not a pointer you can build a
; segment from. Every byte you move crosses through OSAPI_XMEM_COPY, which is
; exactly why that slot carries one ABI over two transports - direct
; addressing needs unreal mode, which is tier 2 only and lives entirely inside
; the kernel. YOU are 8086 code: no 32-bit register, no FS, no GS, no
; `cpu 386` island of your own (SPEC.md 1 rule 1, no exception).
;
; BRANCH ON THE CAPS, NEVER ON THE TIER. OSAPI_XMEM_CAPS returning 0 KB is the
; only answer that means "do not try", and it is the answer on a 386 whose A20
; gate never verified just as much as on an 8088.
CPU_8086            equ 0        ; tier 0: 8086/8088. No A20 line, no memory
                                 ; above 1MB - the floor machine, and the
                                 ; answer you must still work on.
CPU_286             equ 1        ; tier 1: A20 + BIOS block move
CPU_386             equ 2        ; tier 2: the above, plus unreal mode
;
; --- volumes (SPEC.md 18.7/51.8) --------------------------------------------
; A DRVC_DISK DRIVER's slots, not an application's: all four are fenced on the
; caller being the driver whose disk services are published, so a package gets
; CF=1 and nothing else. They are published here because drivers include this
; file (drivers/os88drv.inc does), and because a slot number belongs in one
; place (SPEC.md 20.8).
%define OSAPI_VOL_ADD       KERNEL_SEG:0x0270   ; AL = the driver's own volume
                                 ; handle, CX = the volume's sector count,
                                 ; DX = a listing claim's segment (0 = the
                                 ; kernel's 32-entry floor), SI = a NUL
                                 ; desktop label in the CALLER's segment (0 =
                                 ; derive 'Disk X'). out CF=0 and AL = the
                                 ; VOLUME INDEX - 2 is C:, and the desktop
                                 ; zone and the drive letter both fall out of
                                 ; it
%define OSAPI_VOL_DEL       KERNEL_SEG:0x0278   ; AL = a volume index this
                                 ; driver registered. Cannot fail
%define OSAPI_VOL_MOUNT     KERNEL_SEG:0x0280   ; AL = a volume index; mount
                                 ; and list it. out CF=1 = not a readable
                                 ; FAT12/16 volume. UI-TASK CONTEXT ONLY,
                                 ; like every other file slot
%define OSAPI_VOL_PAINT     KERNEL_SEG:0x0288   ; repaint the desktop's drive
                                 ; zones. It takes the gfx lock ITSELF, so
                                 ; never from a callback that holds one

; --- a driver's settings, inside SYSTEM.CFG (SPEC.md 51.9) ------------------
; A DRIVER's slot, fenced like the four above. The kernel carries DRV_BLOB_SZ
; opaque bytes to disk and back with the rest of the Control Panel; it never
; reads them, and it round-trips them untouched on a machine where the driver
; never loads. Versioning them is the driver's job.
%define DRV_BLOB_SZ         34
%define OSAPI_DRV_CFG       KERNEL_SEG:0x0290   ; AL = 0 read / anything else
                                 ; write, ES:SI = the driver's buffer, CX =
                                 ; bytes (<= DRV_BLOB_SZ).
                                 ; out CF=1 = not the published driver, or a
                                 ; WRITE past DRV_BLOB_SZ; a never-written blob
                                 ; reads back as zeroes, so put a version byte
                                 ; in it and treat anything else as the
                                 ; defaults (SPEC.md 51.5). A WRITE NEVER
                                 ; TOUCHES THE DISK: the bytes ride the next
                                 ; Control Panel flush, which is the panel's
                                 ; close or Special > Restart and nothing else
                                 ; (SPEC.md 31.8). The old verb 2, "write the
                                 ; file NOW", is retired and reads as 1

; --- the three snapshots, and a patterned fill (SPEC.md 20.9) ----------------
; The first three answer questions about the SYSTEM rather than about you: the
; scheduler and the instance table, the claim map, and how big the kernel is.
; Every one of them fills a buffer YOU own, through ES:DI, and returns a count
; so a package built against an older SDK finds out that a table has grown
; rather than walking off the end of its own buffer. Any context - they take
; no lock and touch no VRAM.
;
; These exist because a task monitor cannot be written without them, and one
; had to be liftable out of the kernel. Nothing stops another app using them,
; but note what they cost: SYS_SNAPSHOT runs with interrupts off for the length
; a table copy, so it is a twice-a-second call, not a per-frame one.
%define OSAPI_SYS_SNAPSHOT      KERNEL_SEG:0x0298   ; ES:DI = a SYS_SNAPSHOT_SIZE
                                 ; buffer; out AX = MAX_TASKS, BX = INST_MAX.
                                 ; The scheduler header (SS_*) then INST_MAX
                                 ; records of SSI_RECSZ (SSI_*), all inside ONE
                                 ; cli window - which is the point: an instance
                                 ; record is freed atomically with its task
                                 ; slot, so the two halves have to be read
                                 ; together or a cycle diff lands on the wrong
                                 ; row
%define OSAPI_CLAIM_SNAPSHOT    KERNEL_SEG:0x02A0   ; ES:DI = a CLAIM_SNAPSHOT_SIZE
                                 ; buffer; out AX = MEM_MAX. MEM_MAX records of
                                 ; CLS_RECSZ: base segment (0 = a free record),
                                 ; paragraphs, owner word (SPEC.md 50.2)
%define OSAPI_SYS_KB        KERNEL_SEG:0x02A8   ; ES:DI = a SYSKB_SIZE buffer;
                                 ; every register preserved. The kernel's own
                                 ; footprint in KB broken into its parts, which
                                 ; sum to SK_KERN exactly, then the heap's
                                 ; totals and the pool above 1MB. The footprint
                                 ; terms move with every kernel build - that is
                                 ; why they are a call and not a constant
%define OSAPI_GFX_FILL_PAT  KERNEL_SEG:0x02B0   ; AX/BX/CX/DX = the rect, SI = 8
                                 ; pattern bytes in YOUR segment: row y takes
                                 ; byte [SI + (y & 7)], a set bit is WHITE and
                                 ; bit 7 is the leftmost pixel of each screen
                                 ; byte. The pattern is SCREEN-aligned, so two
                                 ; rects that abut tile seamlessly. Ignores the
                                 ; pen colour and writes only 0/15, so it never
                                 ; costs you the mono flush (SPEC.md 32). Lock
                                 ; held, like every other draw

; --- OSAPI_SYS_SNAPSHOT's buffer -------------------------------------------------
SS_TICKS   equ 0                ; word: the tick counter at the snapshot
SS_CUR     equ 2                ; byte: the running task slot
SS_COOP    equ 3                ; byte: 0 pre-emptive, 1 cooperative
SS_NTASK   equ 4                ; byte: MAX_TASKS, the same figure AX answers
SS_NINST   equ 5                ; byte: INST_MAX, likewise BX
SS_TSTATE  equ 6                ; MAX_TASKS bytes, the kernel's T_STATE
                                ; verbatim: 0 free, 1 ready, 2 SLEEPING
                                ; (SPEC.md 8). There is no "running" state -
                                ; the running task is SS_CUR, and its T_STATE
                                ; is 1 like any other ready task
SS_TCYC    equ SS_TSTATE + MAX_TASKS       ; MAX_TASKS dwords: cycles per task
SS_INST    equ SS_TCYC + MAX_TASKS * 4     ; ...then the instance records
SSI_STATE  equ 0                ; byte: 0 free, 1 live, 2 dying
SSI_TASK   equ 1                ; byte: its task slot, 0xFF = task-less
SSI_KIND   equ 2                ; byte: bit 7 (KIND_PKG) = a loaded package
SSI_RSVD   equ 3                ; byte: pad
SSI_SIZE   equ 4                ; word: its region in BYTES, 0 for a built-in
SSI_SEG    equ 6                ; word: the region's base segment
SSI_CYC    equ 8                ; dword: cycles billed to its callbacks
SSI_KB     equ 12               ; word: KB it holds off the claim heap
SSI_NAME   equ 14               ; 16 bytes: its name, NUL-terminated
SSI_RECSZ  equ 30
SYS_SNAPSHOT_SIZE equ SS_INST + INST_MAX * SSI_RECSZ

; --- OSAPI_CLAIM_SNAPSHOT's buffer -----------------------------------------------
CLS_SEG    equ 0                ; word: base segment, 0 = a free record
CLS_PARA   equ 2                ; word: size in paragraphs (KB << 6)
CLS_OWN    equ 4                ; word: owner - an instance slot, a package's
                                ; own segment, or 0xFFxx for a kernel tag
CLS_RECSZ  equ 6
CLAIM_SNAPSHOT_SIZE equ MEM_MAX * CLS_RECSZ

; --- OSAPI_SYS_KB's buffer, all in KB ----------------------------------------
SK_KERN    equ 0                ; word: the whole kernel span
SK_IMG     equ 2                ; word: image + scratch + cold code
SK_FAT     equ 4                ; word: the mount-time FAT window
SK_STK     equ 6                ; word: the background task stacks
SK_STK0    equ 8                ; word: task 0's - the UI task's
SK_DSK     equ 10               ; word: the disk buffers
SK_BUF     equ 12               ; word: FAT + stacks + disk buffers together
SK_HEAP    equ 14               ; word: the claim heap's size
SK_CLAIM   equ 16               ; word: claimed out of it, every owner
SK_KCLAIM  equ 18               ; word: ...of which the kernel's own
SK_XMS     equ 20               ; word: the pool above 1MB, 0 with no store
SYSKB_SIZE equ 22

MAX_TASKS  equ 12               ; the scheduler's slots (SPEC.md 8) - a bound
INST_MAX   equ 12               ; running instances (SPEC.md 29.1)   on the
MEM_MAX    equ 32               ; claim records (SPEC.md 50)         buffers
                                ; above, and each call answers the live figure
                                ; so a stale SDK is a caught mismatch and not
                                ; an overrun
KIND_PKG   equ 0x80             ; SSI_KIND bit 7: this instance is a package

%define OSAPI_CPU_INFO      KERNEL_SEG:0x0188   ; no inputs; out AL = CPU_8086 /
                                 ; CPU_286 / CPU_386, AH = feature bits: bit 0
                                 ; A20 verified open, bit 1 HMA claimed, bit 2
                                 ; unreal mode armed. Preserves every register
                                 ; but AX. Any context - it reads two bytes.
%define OSAPI_XMEM_CAPS     KERNEL_SEG:0x0190   ; no inputs; out AX = extended-
                                 ; memory KB the pool can still hand out (0 =
                                 ; none, and the three below will all refuse),
                                 ; DX:CX = the pool's 32-bit linear base, BL =
                                 ; free block-table entries. Any context.
%define OSAPI_XMEM_ALLOC    KERNEL_SEG:0x0198   ; DX:AX = bytes wanted, rounded
                                 ; up to 1KB; out CF=0 and DX:AX = the block's
                                 ; 32-bit linear base (opaque - hand it back to
                                 ; COPY and FREE, never dereference it), else
                                 ; CF=1 and AX = 0 no store / 1 no free table
                                 ; entry / 2 no contiguous run that big. The
                                 ; block is stamped with YOUR instance and
                                 ; force-freed when you close - but the table
                                 ; is small, so take one big block and
                                 ; subdivide it yourself rather than many.
%define OSAPI_XMEM_FREE     KERNEL_SEG:0x01A0   ; DX:AX = a base YOU own; out
                                 ; CF=0 freed (and merged with the neighbouring
                                 ; free runs), CF=1 not a base or not yours.
%define OSAPI_XMEM_COPY     KERNEL_SEG:0x01A8   ; ES:SI = the conventional end,
                                 ; DX:AX = the 32-bit linear extended end, CX =
                                 ; bytes (EVEN, and <= 32768 - move more with
                                 ; two calls), DI = 0 out to extended / 1 in
                                 ; from extended. out CF=0 done; CF=1 and AX =
                                 ; 1 no store / 2 the range is not inside a
                                 ; block you own / 3 bad length. ES is restored.
;
; CONTEXT, BINDING: these five are UI-task context - the entry proc, a window
; callback, a menu handler or a file-dialog completion proc. THE GFX LOCK MAY
; BE HELD. A window callback always holds it, and copying a render buffer out
; to extended memory or back is exactly what you do there - the copy touches
; no VRAM and takes no drawing lock of its own, so it is just a slow-ish frame,
; never a hazard. NEVER from your worker task, though: on tier 1 the copy is a
; BIOS call (int 15h AH=87h) and the kernel keeps BIOS calls on the UI task
; (SPEC.md 7). Each call is bounded to 32KB so its interrupts-off window stays
; short, and where a 286 BIOS implements AH=87h by a CPU reset, the reset
; returns transparently (the himem.sys mechanism), conventional RAM intact.

; --- window geometry (SPEC.md 11) ----------------------------------------------
%define OSAPI_WM_GEOM       KERNEL_SEG:0x01B0   ; BX = win ptr; out CX = content
                                 ; width, DX = content height (inner px - the
                                 ; box whose origin OSAPI_WM_CONTENT answers),
                                 ; CF=1 the window is not visible. Preserves
                                 ; everything else. Callable from any context,
                                 ; lock held or not; the answer is only STABLE
                                 ; under the lock, which is when painters ask.
                                 ;
                                 ; You may still read [es:bx+W_W]/[es:bx+W_H]
                                 ; directly (ES = KERNEL_SEG in
                                 ; every callback) - but those are FRAME size,
                                 ; and this is the same subtraction done once.
                                 ; Prefer it over repeating the arithmetic.

%define OSAPI_WM_RESIZE     KERNEL_SEG:0x01D0   ; BX = win ptr, CX = new width,
                                 ; DX = new height, in pixels, of the WHOLE
                                 ; window (frame included). Clamps to the
                                 ; kernel's minimum and to the live screen,
                                 ; nudges x/y so the window still fits, and
                                 ; repaints everything. Call it INSTEAD of
                                 ; writing W_W/W_H yourself: the fields are
                                 ; readable by contract, not writable, and an
                                 ; app that sets them leaves the shadow, the
                                 ; z-order damage and the dock out of step.
                                 ; Do NOT hold the gfx lock.
%define OSAPI_WM_ONSIZE     KERNEL_SEG:0x0220   ; BX = win ptr, AX = a near proc
                                 ; in YOUR segment (0 clears it). The kernel
                                 ; calls it BEFORE it commits a size the user
                                 ; dragged out of your grow box, with
                                 ; SI = your window, CX/DX = the proposed
                                 ; frame size; you answer with the size you
                                 ; will accept, in CX/DX. Answer with the size
                                 ; you already have to refuse outright, or
                                 ; change one axis and leave the other, which
                                 ; is what an editor refusing to crop wants.
                                 ; It runs under the gfx lock with nothing yet
                                 ; drawn at either size, so DO NOT draw in it -
                                 ; decide, return, and draw in the W_PAINT the
                                 ; kernel is about to make.
%define OSAPI_FILE_HERE     KERNEL_SEG:0x0228   ; out DX = YOUR instance's
                                 ; current directory (0 = the root), BL = its
                                 ; drive. No disk I/O.
%define OSAPI_FILE_GOTO     KERNEL_SEG:0x0230   ; in DX = a cluster from
                                 ; OSAPI_FILE_HERE, BL = its drive; moves YOUR
                                 ; instance there. out CF=1 it could not be
                                 ; listed and the volume is back at the root
                                 ; with the write gate shut. A REMOUNT: real
                                 ; floppy I/O, UI-task context only, like the
                                 ; other file slots.
                                 ;
                                 ; YOU DO NOT NEED THESE TO MAKE SAVE WORK,
                                 ; and that is a change (SPEC.md 19.2.1).
                                 ; Every file name you pass the file API
                                 ; resolves in YOUR INSTANCE's directory - the
                                 ; folder you were launched from until the
                                 ; user picks another in a dialog, and that
                                 ; one afterwards. A Disk window opened on C:,
                                 ; another app saving, a driver reading its
                                 ; settings: none of them can move you. The
                                 ; kernel banks and restores underneath every
                                 ; file cell, so a Save that means "where my
                                 ; Save As went" is just a Save.
                                 ;
                                 ; They are still here, and four packages
                                 ; still call them, because a slot that has
                                 ; shipped keeps its contract (SPEC.md 20.8
                                 ; rule 4) - that code is now redundant rather
                                 ; than wrong. Reach for them when you want to
                                 ; visit a folder DELIBERATELY, not to defend
                                 ; against the machine moving under you.
%define OSAPI_MEM_REGROW    KERNEL_SEG:0x0238   ; DX = a claim YOU hold, AX =
                                 ; the new size in KB. out CF=0 and DX = the
                                 ; claim's base NOW - the SAME segment when it
                                 ; grew where it stood, a different one when it
                                 ; had to move, and your bytes came with it
                                 ; either way. CF=1 and the old claim is
                                 ; untouched at its old base.
                                 ;
                                 ; USE THIS INSTEAD OF claim-copy-free. That
                                 ; sequence needs old + new free AT ONCE and
                                 ; contiguously, so a heap with plenty of total
                                 ; room refuses a resize it can afford. This
                                 ; extends in place when the paragraphs above
                                 ; are free - needing only the DIFFERENCE - and
                                 ; only moves when it must, in which case it
                                 ; picks the HIGHEST block that fits so you end
                                 ; up at the top with room to grow again.
                                 ;
                                 ; Shrinking always succeeds in place, and the
                                 ; tail is free for everyone else immediately.
                                 ;
                                 ; ALWAYS take the answer from DX. A grow that
                                 ; moved leaves your old segment pointing at
                                 ; memory that is no longer yours.

%define OSAPI_WM_TITLE      KERNEL_SEG:0x0240   ; BX = win ptr, AX = a NUL
                                 ; string IN YOUR OWN SEGMENT, or 0 to mean
                                 ; "the bytes W_TITLE already names changed
                                 ; underneath it". Redraws the caption strip
                                 ; and NOTHING else: no content fill, no
                                 ; W_PAINT, no other window - so a document
                                 ; name in the title bar costs 17 rows.
                                 ;
                                 ; Call it from a window callback, which is
                                 ; where the gfx lock is already held. It is
                                 ; safe on a hidden window (it draws nothing)
                                 ; and safe on a covered one (likewise), and
                                 ; the string must OUTLIVE the call - the
                                 ; record keeps the pointer, not the bytes.

%define OSAPI_FONT_GLYPHS   KERNEL_SEG:0x0218   ; out DX:SI = the kernel's
                                 ; 8x8 glyph table, AL = first character code
                                 ; it covers, AH = last, CX = bytes per glyph
                                 ; (8). Read it with ES = DX - the table is
                                 ; NOT in KERNEL_SEG, and DX is a recorded
                                 ; amendment to this slot (SPEC.md 20.8): it
                                 ; used to answer with SI alone. For an app that
                                 ; needs the BITMAP of a character rather than
                                 ; a drawn one - scaling it, stamping it into
                                 ; a canvas, measuring a cell. It is the same
                                 ; table OSAPI_FONT_CHAR draws from, which is
                                 ; the point: no int 10h ROM probe, no second
                                 ; typeface, and it works on all three
                                 ; adapters.
;
; Your worker gets DX = your instance index, DS = ES = CS = KERNEL_SEG,
; IF = 1 and the gfx lock free. The binding rules, none of them enforced:
;
;  1. ONE worker per instance. A second OSAPI_TASK_SPAWN just refuses.
;  2. NEVER return and NEVER exit on your own. Call OSAPI_TASK_ALIVE at
;     least once per outer-loop iteration and sleep when idle. This is the
;     sharp edge: a worker that exits by itself leaves the kernel holding a
;     dead task slot, and your close box then hides the window but never
;     frees the instance record - your region leaks for the rest of the
;     session. A worker that falls off the end and executes `ret` is worse:
;     it jumps to a near-random kernel offset.
;  3. NEVER hold the gfx lock across a long computation. Compute lock-free,
;     then take the lock for a short burst of drawing and release it. A
;     worker spinning under the lock wedges the whole machine, in either
;     scheduler mode, and no watchdog can break it.
;  4. gfx_lock is NOT reentrant: calling OSAPI_TASK_ALIVE while holding it
;     deadlocks - nothing ever draws again.
;  5. Under the lock, re-check that your window is still visible
;     (test word [bx+W_FLAGS], 2) and then call OSAPI_WM_CLIP_SET before
;     drawing. Windows move and get buried while you sleep, and the gfx_*
;     primitives take absolute screen coordinates: without one of these two
;     answers you paint over whatever is on top of you. CF = 1 means draw
;     nothing this frame; CF = 0 means draw normally and the kernel cuts
;     every call to the part of your content that shows. OSAPI_WM_OBSCURED
;     is still there and still correct, but it vetoes the whole frame for
;     one covered pixel - prefer the clip.
;  6. Your stack is SCH_STACK (512) bytes in LOW_SEG and SS != DS (see the header of
;     this file). No deep recursion, no big stack buffers, and BP-relative
;     operands address SS.
;  7. You may call: the gfx_* and font_* slots, OSAPI_SET_COLOR, OSAPI_GFX_PEN,
;     OSAPI_WM_CONTENT, OSAPI_WM_OBSCURED, OSAPI_WM_CLIP_SET,
;     OSAPI_WM_CLIP_CLEAR, OSAPI_WM_CLIP_TEST, OSAPI_VIDEO, OSAPI_GET_TICKS,
;     OSAPI_MOUSE, OSAPI_SRAND/RAND, OSAPI_SND_CAPS, OSAPI_SND_TONE,
;     OSAPI_TASK_SLEEP, OSAPI_TASK_YIELD and OSAPI_TASK_ALIVE.
;     OSAPI_SET_COLOR writes ONE global pen shared by the whole machine, so
;     set it only inside the same lock hold as the drawing it colours -
;     lock-free, it repaints someone else's window in your colour.
;     OSAPI_SND_TONE is worker-safe by construction, not by luck: the grant
;     is stamped with the instance `snd_req_inst` resolves, and outside a
;     dispatched callback that is the RUNNING TASK's own instance (SPEC.md
;     34.3) - which is yours - so the tone is attributed to you and released
;     by `snd_release_inst` when you are torn down. Ask for a DURATION and it
;     self-expires through `snd_tick`, so a worker need never come back to
;     silence it. You may NOT call the file slots, OSAPI_FILE_DLG or
;     OSAPI_SND_PLAY from a worker - those are UI-task-only and the kernel
;     cannot stop you. SND_PLAY is not on that list for the tone's reasons:
;     it runs the whole clip with the scheduler locked, so a worker calling
;     it freezes the desktop instead of merely misattributing something.
;
; The kernel's own app_bounce_task is the reference implementation of all
; of this.

; file error codes, pinned (SPEC.md 18.4): returned in AX with CF set
FERR_OK      equ 0
FERR_NODISK  equ 1               ; no mounted data disk
FERR_IO      equ 2               ; unrecoverable read/write error
FERR_NAME    equ 3               ; malformed 8.3 name / bad argument
FERR_NOENT   equ 4               ; no such file
FERR_EXIST   equ 5               ; the target name already exists
FERR_FULL    equ 6               ; no free clusters
FERR_DIRFULL equ 7               ; the root directory is full
FERR_PROT    equ 8               ; read-only/hidden/system/label/directory
FERR_WPROT   equ 9               ; the media is write-protected
FERR_BIG     equ 10              ; larger than the buffer (or than 64KB)

WF_SIZABLE equ 4                 ; W_FLAGS bit2: resizable (SPEC.md 11.1)
WF_FULL    equ 8                 ; W_FLAGS bit3: fullscreen (SPEC.md 11.2)
WF_SNAP    equ 16                ; W_FLAGS bit4: content origin snapped to a
                                 ; multiple of 8 (SPEC.md 11.94) - set it with
                                 ; OSAPI_WM_SNAP, never by hand
WMIN_W     equ 96                ; smallest frame a resize can leave
WMIN_H     equ 64

; --- app menu set layout (SPEC.md 12.2) ----------------------------------------
; Built with the OS88_MENUSET / OS88_MENU / OS88_MENUSET_END macros below;
; these offsets are here for code that walks its own set at run time.
AM_NAME     equ 0           ; word: NUL app name for the bar label (<= 15
                            ; chars); 0 = fall back to the instance name
AM_ONCMD    equ 2           ; word: near ptr to the command handler
AM_COUNT    equ 4           ; word: menu count, 0..MENU_APPMAX
AM_LIST     equ 6           ; AM_COUNT entries of:
AMENU_TITLE equ 0           ; word: NUL menu title (the bar cell's text)
AMENU_ITEMS equ 2           ; word: array of near ptrs to NUL item strings
AMENU_NITEM equ 4           ; word: item count
AMENU_ENTSZ equ 6
MENU_APPMAX equ 4           ; menus the bar can host per app; extras are
                            ; silently dropped by the kernel's layout pass
MENU_MAXCH  equ 24          ; glyphs of item text a pull-down shows; longer
                            ; items are TRUNCATED, not wrapped
MENU_DIS    equ 1           ; an item string that BEGINS with this byte is
                            ; DISABLED: the kernel draws the rest of it grey
                            ; and never highlights or selects it. Point the
                            ; item at a different string to switch, exactly
                            ; as you already would to relabel it:
                            ;
                            ;   it_edit: dw i_paste
                            ;   i_paste:  db 'Paste', 0
                            ;   i_paste0: db MENU_DIS, 'Paste (NoRam)', 0
                            ;   ...
                            ;   mov word [it_edit + 2*ME_PASTE], i_paste0
                            ;
                            ; Still answer the command itself: a keyboard
                            ; shortcut never goes near a menu.
                            ;
                            ; The grey says THAT the item is unavailable, on
                            ; every adapter: the KERNEL draws your menu, so a
                            ; MENU_DIS item goes through the disabled pen and
                            ; is masked to a checkerboard on mono - the way
                            ; the 1bpp Macintosh drew a greyed-out item. You
                            ; need do nothing for that. (This is the one
                            ; place a package gets 1bpp-visible disabled TEXT
                            ; for free; anything you draw yourself follows
                            ; the CDGRAY note above.) The '(NoRam)' says WHY,
                            ; which the grey cannot, and is worth the bytes
                            ; whenever there is a reason a user could act on.
                            ; SPEC.md 47 is the standard for every disabled
                            ; control in the system.

; --- window record layout (SPEC.md 11, 20 bytes) -------------------------------
W_FLAGS   equ 0             ; word: bit0 = used, bit1 = visible, bit2 =
                            ; resizable, bit3 = fullscreen
W_X       equ 2             ; word: frame left (screen coords)
W_Y       equ 4             ; word: frame top (y >= MBAR_H - except under
                            ; WF_FULL, SPEC.md 11.2)
W_W       equ 6             ; word: frame width, outer
W_H       equ 8             ; word: frame height, includes title bar
W_TITLE   equ 10            ; word: near ptr to NUL title string
W_PAINT   equ 12            ; word: near ptr, SI = win ptr; content only
W_ONKEY   equ 14            ; word: near ptr or 0; AL=ascii, AH=scan, SI=win
W_ONCLICK equ 16            ; word: near ptr or 0; CX=x, DX=y, SI=win
W_MENUS   equ 18            ; word: app menu set ptr, 0 = none. NOT a
                            ; template word - set it with OSAPI_MENU_SET
W_DISP    equ 20            ; dword: the kernel's far pointer to your
W_SEG     equ 22            ; dispatcher, and the segment every near
                            ; pointer in this record belongs to (yours).
                            ; Both are the kernel's business, not yours.
W_ONSIZE  equ 24            ; word: near ptr or 0 - your resize negotiator.
                            ; NOT a template word - set it with
                            ; OSAPI_WM_ONSIZE
WIN_SIZE  equ 26
MAX_WIN   equ 12

; THE RECORD LIVES IN KERNEL_SEG. On entry to any callback ES points there
; (SPEC.md 20.1), so read and write it with an override:
;
;     test word [es:bx + W_FLAGS], 2      ; still visible?
;     mov ax, [es:bx + W_W]               ; the live width
;
; Without the override you are reading your own image at that offset, which
; assembles cleanly and runs wrong - the one sharp edge of owning a segment.

; --- wm_create template layout (SPEC.md 11, 16 bytes, 8 words) -----------------
WT_X       equ 0
WT_Y       equ 2
WT_W       equ 4
WT_H       equ 6
WT_TITLE   equ 8
WT_PAINT   equ 10
WT_ONKEY   equ 12
WT_ONCLICK equ 14
WT_SIZE    equ 16

; -----------------------------------------------------------------------------
; OS88_HEADER 'NAME', entry [, flags] - open the package and emit the 32-byte
; header (SPEC.md 20.2). Emits cpu 8086 / bits 16 / org 0 first, so this must
; be the first thing after the %include. It also emits the three-byte
; DISPATCHER at offset 12 that the kernel far-calls to reach any of your
; callbacks - that is why your paint/key/click procs stay ordinary near
; procs ending in a near `ret`, and why you never write `retf`. The image-size and
; bss-size words are forward references resolved by OS88_IMAGE_END / OS88_BSS
; at the end of the source. The name is NUL-padded to exactly 16 bytes.
; The optional third parameter is the flags byte (default 0); flags bit 0
; declares an embedded icon, which must follow immediately as a
; OS88_ICON16 .. OS88_ICON16_END block.
; -----------------------------------------------------------------------------
%macro OS88_HEADER 2-3
    cpu 8086
    bits 16
    org 0                   ; v3: you own a segment, so you link at zero and
                            ; the loader puts you on a paragraph boundary.
                            ; No relocation table, no dual assembly, no rule
                            ; about whole-word addresses (SPEC.md 20.2)
    db 'O', '8'             ; +0  magic (word 0x384F)
    db 3                    ; +2  format version 3: segment-per-package
    %if %0 >= 3
    db %3                   ; +3  flags (bit 0 = embedded icon)
    %else
    db 0                    ; +3  flags
    %endif
    dw 0                    ; +4  link base: 0, and the loader checks it
    dw (%2) - $$            ; +6  entry offset
    dw OS88_IMAGE_SIZE       ; +8  image size (the whole file)
    dw OS88_BSS_SIZE         ; +10 bytes the loader zeroes after the image
    db 0FFh, 0D5h           ; +12 THE DISPATCHER: `call bp`...
    db 0CBh                 ; +14 ...`retf`. The kernel far-calls this fixed
    db 0                    ; +15 offset with BP = the callback it wants,
                            ;     which is why your paint/key/click procs are
                            ;     ordinary near procs ending in `ret` and you
                            ;     never write `retf` (SPEC.md 20.1/20.2)
    %strlen %%len %1
    %if %%len > 15
        %error "OS88_HEADER: program name must be at most 15 characters (the 16-byte field is NUL-padded)"
    %endif
    db %1                   ; +16 program name...
    times 16 - %%len db 0   ;     ...NUL-padded to 16 bytes
%endmacro

; -----------------------------------------------------------------------------
; OS88_ASSOC16 / OS88_ASSOC16_END - bracket the association block (SPEC.md
; 54.6). Only valid when OS88_HEADER was given flags bit 1, and it goes
; IMMEDIATELY after the icon (offset 96) or, with no icon, immediately after
; the header (offset 32). Between the two macros write a count byte and up to
; five 3-byte extensions, uppercase and space-padded - OS88_ASSOC_EXT lays one
; out. The block is a fixed 16 bytes and the macros assert its start and end,
; so a miscounted db fails at ASSEMBLY rather than at mount.
;
; Declaring costs nothing at runtime: the mount's icon harvest already reads
; this sector, so a document beside your program opens on the FIRST
; double-click, with no prior run and no search.
; -----------------------------------------------------------------------------
%macro OS88_ASSOC16 0
    %if ($ - $$) != 96 && ($ - $$) != 32
        %error "OS88_ASSOC16 must sit at offset 96 (with an icon) or 32 (without)"
    %endif
    %assign OS88_ASSOC_BASE ($ - $$)
%endmacro

%macro OS88_ASSOC_EXT 1
    %strlen %%xl %1
    %if %%xl > 3
        %error "OS88_ASSOC_EXT: an extension is at most 3 characters"
    %endif
    db %1
    times 3 - %%xl db ' '
%endmacro

%macro OS88_ASSOC16_END 0
    %if ($ - $$) > OS88_ASSOC_BASE + 16
        %error "the association block is 16 bytes: a count byte and up to five extensions"
    %endif
    times OS88_ASSOC_BASE + 16 - ($ - $$) db 0
%endmacro

; -----------------------------------------------------------------------------
; OS88_ICON16 / OS88_ICON16_END - bracket the embedded 16x16 icon (SPEC.md
; 20.2/20.5). Only valid when OS88_HEADER was given flags bit 0. The author
; writes exactly 32 hand-authored dw rows between the two macros: 16 mask
; words (white underlay) then 16 data words (black pixels), bit 15 =
; leftmost pixel, row-major. OS88_ICON16 asserts the block starts right
; after the header (file offset 32); OS88_ICON16_END asserts it ends at
; offset 96, i.e. exactly 64 bytes of rows were written.
; -----------------------------------------------------------------------------
%macro OS88_ICON16 0
    %if ($ - $$) != 32
        %error "OS88_ICON16 must immediately follow OS88_HEADER (file offset 32)"
    %endif
%endmacro

%macro OS88_ICON16_END 0
    %if ($ - $$) != 96
        %error "OS88_ICON16 block must be exactly 32 dw rows (16 mask + 16 data, ending at offset 96)"
    %endif
%endmacro

; -----------------------------------------------------------------------------
; OS88_MENUSET label, namestr, handler
; OS88_MENU    titlestr, itemsptr, itemcount        (one per menu, in order)
; OS88_MENUSET_END label
;
; Emit an app menu set (SPEC.md 12.2). AM_COUNT is computed from the label
; pair, so adding or removing an OS88_MENU line is the whole edit:
;
;   OS88_MENUSET my_menus, my_name, my_oncmd
;       OS88_MENU s_file, items_file, 3
;       OS88_MENU s_edit, items_edit, 2
;   OS88_MENUSET_END my_menus
;
;   my_name:    db 'Notes', 0
;   s_file:     db 'File', 0
;   items_file: dw s_open, s_save, s_quit          ; NUL item strings
;
; and, in the entry proc, after OSAPI_WM_CREATE returned BX:
;
;   mov si, my_menus
;   call OSAPI_MENU_SET
;
; The handler is called by the kernel UNDER THE GFX LOCK on the UI
; task, with AL = item index, AH = menu index (0-based), SI = the owning
; window, BX = the set. It obeys the W_ONCLICK rules (SPEC.md 11): it may
; draw and may call the file slots, but must never take the gfx lock or
; block. It MAY call OSAPI_TASK_SPAWN - a menu command is the natural place
; to start your worker.
;
; Every string a set points at - the app name, the menu titles, the items -
; is an offset in YOUR segment. The kernel reads them through the segment
; the owning window carries, so there is nothing for you to do about it;
; it is worth knowing only because it means a set may not point at a
; string that lives anywhere else.
; -----------------------------------------------------------------------------
%macro OS88_MENUSET 3
%1:
    dw %2                       ; AM_NAME
    dw %3                       ; AM_ONCMD
    dw (%1 %+ _end - %1 %+ _list) / AMENU_ENTSZ      ; AM_COUNT
%1 %+ _list:                    ; AM_LIST
%endmacro

%macro OS88_MENU 3
    dw %1                       ; AMENU_TITLE
    dw %2                       ; AMENU_ITEMS
    dw %3                       ; AMENU_NITEM
%endmacro

%macro OS88_MENUSET_END 1
%1 %+ _end:
    %if (%1 %+ _end - %1 %+ _list) / AMENU_ENTSZ > MENU_APPMAX
        %error "OS88_MENUSET: at most MENU_APPMAX (4) menus fit in the bar"
    %endif
%endmacro

; -----------------------------------------------------------------------------
; OS88_BSS totalbytes - declare the package's zeroed-bss size (optional).
; Emits nothing; must appear before OS88_IMAGE_END (which supplies the 0
; default when this macro is absent). The argument must be a plain constant
; expression with no forward references.
; -----------------------------------------------------------------------------
%macro OS88_BSS 1
    %ifdef OS88__HAVE_END
        %error "OS88_BSS must appear before OS88_IMAGE_END"
    %endif
    %define OS88__HAVE_BSS
OS88_BSS_SIZE equ (%1)
%endmacro

; -----------------------------------------------------------------------------
; OS88_IMAGE_END - close the package. Defines os88_image_end (the label a
; package bases its bss offsets on), OS88_IMAGE_SIZE = $-$$ for the header,
; defaults OS88_BSS_SIZE to 0 when no OS88_BSS was given, and enforces the
; APP_MAX_SIZE budget at assembly time.
; -----------------------------------------------------------------------------
%macro OS88_IMAGE_END 0
    %define OS88__HAVE_END
os88_image_end:
OS88_IMAGE_SIZE equ os88_image_end - $$
    %ifndef OS88__HAVE_BSS
OS88_BSS_SIZE equ 0
    %endif
    %if (OS88_IMAGE_SIZE + OS88_BSS_SIZE) > APP_MAX_SIZE
        %error "package too big: image + bss must fit in APP_MAX_SIZE (0x4E00)"
    %endif
%endmacro

%define OSAPI_FONT_RUN      KERNEL_SEG:0x0258   ; CX=x, DX=y, SI=NUL string,
                                                ; AL=ink, AH=background. ONE
                                                ; OPAQUE text run (SPEC.md 6.1):
                                                ; the cells' background and
                                                ; their glyphs in a single
                                                ; pass, which is the
                                                ; erase-then-letter pair every
                                                ; text element writes by hand.
                                                ; Two things it buys. It cannot
                                                ; produce SPEC.md 11.3's
                                                ; granularity failure - fill
                                                ; and glyph are one decision
                                                ; about one cell - so a
                                                ; background task needs no
                                                ; OSAPI_WM_CLIP_TEST gate
                                                ; around it. And on a 1bpp
                                                ; adapter with x a multiple of
                                                ; 8, a cell row is a single
                                                ; store: no shift, no read, no
                                                ; second byte, no fill pass.
                                                ; Off that path it falls back
                                                ; to fill+FONT_STR, so it is
                                                ; never slower than doing it
                                                ; by hand

%define OSAPI_WM_TOP        KERNEL_SEG:0x0260   ; out BX = the frontmost
                                                ; VISIBLE window, 0 if none.
                                                ; Preserves everything else,
                                                ; takes no lock, callable from
                                                ; a worker.
                                                ;
                                                ; "Do I have focus?" - compare
                                                ; it against your own window
                                                ; ptr. You learn that you HAVE
                                                ; focus (W_ONCLICK fires) and
                                                ; never that you LOST it, so
                                                ; without this a real-time app
                                                ; could not pause when another
                                                ; window came forward.
                                                ; W_FLAGS bit1 is not the same
                                                ; question: it says VISIBLE,
                                                ; which a wholly covered
                                                ; window still is.
                                                ;
                                                ; It is not the same question
                                                ; as OSAPI_MENU_OWNER either -
                                                ; see there.

%define OSAPI_MENU_OWNER    KERNEL_SEG:0x02B8   ; out BX = the window owning
                                                ; the menu bar, 0 = Locator
                                                ; (SPEC.md 12). Preserves
                                                ; everything else and FLAGS,
                                                ; takes no lock, callable from
                                                ; a worker.
                                                ;
                                                ; "Am I the ACTIVE
                                                ; APPLICATION?" - compare it
                                                ; against your own window ptr.
                                                ; Different from OSAPI_WM_TOP
                                                ; in exactly one case, and it
                                                ; is the case that matters:
                                                ; clicking the BARE DESKTOP
                                                ; hands the bar to Locator and
                                                ; moves nothing in the
                                                ; z-order, so the frontmost
                                                ; visible window is still
                                                ; yours while the menu bar has
                                                ; stopped saying your name.
                                                ; A real-time app that must
                                                ; stop when the player walks
                                                ; away wants BOTH tests: this
                                                ; one for the desktop click,
                                                ; WM_TOP for a window raised
                                                ; over you (SPEC.md 44.8).

%define OSAPI_WM_SNAP       KERNEL_SEG:0x0268   ; BX = window ptr, AL = 0
                                                ; clear / non-zero set. Out:
                                                ; nothing - every register
                                                ; preserved, FLAGS INCLUDED,
                                                ; so it is safe in an entry
                                                ; proc after the CF you owe
                                                ; the loader is already in the
                                                ; flags.
                                                ;
                                                ; "Keep my CONTENT ORIGIN on a
                                                ; multiple of 8." Ask for this
                                                ; if you redraw a lot of text
                                                ; and can put your own text at
                                                ; content-relative x values
                                                ; that are multiples of 8 -
                                                ; then every OSAPI_FONT_RUN
                                                ; you make takes the
                                                ; single-store path (SPEC.md
                                                ; 6.1) instead of the
                                                ; erase-and-letter fallback.
                                                ;
                                                ; CONTENT origin, not frame:
                                                ; OSAPI_WM_CONTENT already
                                                ; answers W_X + 1 for the
                                                ; border, and the kernel owns
                                                ; that pixel so you never have
                                                ; to think about it.
                                                ;
                                                ; It costs your window 8-pixel
                                                ; horizontal steps when the
                                                ; user drags it, and it cannot
                                                ; sit flush against the left
                                                ; edge (x = 7 is as far as it
                                                ; goes). MONO ONLY - a no-op
                                                ; on VGA, so set it
                                                ; unconditionally rather than
                                                ; asking OSAPI_VIDEO first.
                                                ;
                                                ; Call it from your entry proc,
                                                ; before the window is shown.
                                                ; UI-task context only, like
                                                ; OSAPI_WM_SIZABLE.

; --- fullscreen exclusive (SPEC.md 53) ---------------------------------------

%define OSAPI_FSX_CAPS      KERNEL_SEG:0x02C0   ; out AX = bitmask (bit n =
                                                ; FSXM id n settable on the
                                                ; live adapter), DL =
                                                ; vid_kind. Any context, lock
                                                ; held or not. Grey your mode
                                                ; menu with this (SPEC.md 47)
                                                ; - fsx_mode refuses with the
                                                ; same bit, one predicate for
                                                ; both.

%define OSAPI_FSX_RUN       KERNEL_SEG:0x02C8   ; the bracket (SPEC.md 53.1).
                                                ; In AX = a near proc in your
                                                ; image, BX = your window
                                                ; ptr, CX = flags (bit 0 =
                                                ; FSXF_KEEPWORKER; all others
                                                ; must be 0). UI-task window
                                                ; callback only, lock held -
                                                ; the wm_fullscreen contract.
                                                ; DOES NOT RETURN until your
                                                ; proc does: the kernel
                                                ; freezes every other task,
                                                ; silences their sound, calls
                                                ; your proc (SI = your
                                                ; window, ES = KERNEL_SEG,
                                                ; DS = CS = your segment - a
                                                ; W_ONKEY's environment, near
                                                ; ret), and on its return
                                                ; restores the desktop whole.
                                                ; Out CF=1 refused. Inside:
                                                ; poll int 16h for keys and
                                                ; OSAPI_MOUSE for the mouse -
                                                ; no events are dispatched;
                                                ; the file API is legal (this
                                                ; IS the UI task); pace with
                                                ; OSAPI_FSX_WAIT, never
                                                ; task_sleep (nothing else is
                                                ; eligible, so a sleep
                                                ; returns at once). After
                                                ; your first OSAPI_FSX_MODE
                                                ; call every drawing slot is
                                                ; off-limits until you return
                                                ; - they render DESKTOP
                                                ; geometry. Never touch PIT
                                                ; channel 0, the sound ports
                                                ; (SPEC.md 34.1) or int 10h
                                                ; mode sets; everything else
                                                ; on the video card is yours
                                                ; while a foreign mode is up.

%define OSAPI_FSX_MODE      KERNEL_SEG:0x02D0   ; in AL = FSXM_*, ES:DI = an
                                                ; FSI_SIZE buffer of yours
                                                ; (set ES = DS). Bracket-only.
                                                ; Out CF=0: mode set, screen
                                                ; cleared, buffer filled;
                                                ; CF=1: bad id, not on this
                                                ; adapter, or your back
                                                ; buffer is armed - call
                                                ; OSAPI_GFX_DBUF off first.

%define OSAPI_FSX_WAIT      KERNEL_SEG:0x02D8   ; in AL = FSXW_TICK (0) the
                                                ; next tick / FSXW_VSYNC (1)
                                                ; vertical retrace (bounded -
                                                ; a dead port times out
                                                ; rather than hangs) /
                                                ; FSXW_FRAME (2) the next
                                                ; IRQ0, YIELDING while it
                                                ; waits - the one to use with
                                                ; a kept worker (53.5.1).
                                                ; Bracket-only, CF=1 outside
                                                ; or on an unknown AL.
                                                ; THE PRESENT: while your
                                                ; back buffer is armed and no
                                                ; mode switch has happened,
                                                ; it flushes before waiting -
                                                ; the bracket never unlocks,
                                                ; so this is the only flush a
                                                ; buffered renderer gets.
                                                ; Draw, wait, repeat.
%define OSAPI_ARG_FILE      KERNEL_SEG:0x02E8   ; the document this instance was
                                 ; launched to open (SPEC.md 54.5). No
                                 ; inputs; out CF=1 = launched empty, the
                                 ; ordinary case. CF=0 with SI -> its NUL 8.3
                                 ; name IN THE KERNEL SEGMENT - read it
                                 ; through ES, which is KERNEL_SEG on entry
                                 ; to every package proc (20.2), and copy it
                                 ; out before you call anything else - plus
                                 ; DX = the directory cluster it lives in and
                                 ; BL = its volume, which is exactly the pair
                                 ; OSAPI_FILE_GOTO takes. So the whole of
                                 ; opening it is: copy the name, GOTO, READ.
                                 ; READ-AND-CLEAR - the first proc to ask
                                 ; gets it, so a second instance launched
                                 ; later can never inherit a document
%define OSAPI_ASSOC_SET     KERNEL_SEG:0x02F0   ; claim an extension for a
                                 ; program (SPEC.md 54.5). ES:SI -> 3
                                 ; extension bytes then 8 stem bytes, both
                                 ; space-padded - the OS88_ASSOC macro below
                                 ; lays that out. Out CF=1 = the tables are
                                 ; full and nothing was stored. It REPOINTS
                                 ; an extension somebody else had claimed:
                                 ; that is the feature, not an oversight, and
                                 ; registering grants nothing - the worst
                                 ; outcome is that the wrong program opens a
                                 ; file. Your own icon becomes the mark its
                                 ; documents carry. Call it from your entry
                                 ; proc; a package that ships a header
                                 ; declaration (54.6) needs no call at all
%define OSAPI_GFX_LINE      KERNEL_SEG:0x02E0   ; an arbitrary-angle line (SPEC.md
                                 ; 5.6): AX/BX = x1/y1, CX/DX = x2/y2,
                                 ; inclusive, absolute screen coords;
                                 ; SI = 0 thin / 1 dilated a pixel either
                                 ; side of the MINOR axis, which is what an
                                 ; erase owes a line that was DRAWN in short
                                 ; per-frame segments (5.6.5) - the two
                                 ; rasterizations differ by a pixel and the
                                 ; ghost is half the trail. Pen in
                                 ; SET_COLOR, gfx lock held. Clobbers flags
                                 ; only. It does NOT replace the area
                                 ; primitives and defers to them: an
                                 ; axis-aligned pair tail-calls GFX_HLINE /
                                 ; GFX_VLINE, and a long axis-aligned run is
                                 ; theirs at any angle of the argument - a
                                 ; fill writes eight pixels a store on 1bpp
                                 ; where a walk writes one (5.6.1). The walk
                                 ; runs DOWNWARD whichever way you hand it
                                 ; the ends, so drawing (A,B) and erasing
                                 ; (B,A) is the same pixel set (5.6.2) - but
                                 ; a line drawn in per-frame SEGMENTS and
                                 ; erased whole still is not, and that is
                                 ; still the caller's to dilate

; --- the boot timer (SPEC.md 15.4) --------------------------------------------

; --- SPEC.md 5.6.7: the RESUMABLE walk ---------------------------------------
; gfx_line draws a whole line. These draw the NEXT N PIXELS of one, and that
; difference is what lets you ERASE EXACTLY WHAT YOU DREW.
;
; If you draw a line incrementally - a few pixels a frame, as a moving object's
; trail - and then erase it with one whole-line call, the two Bresenhams do
; NOT visit the same pixels, and the erase leaves a dashed remnant. The old
; answer was to dilate the erase by a pixel either side, which is an
; approximation in both directions: it can still miss, and it erases pixels
; your trail never drew out of whatever was underneath. It also costs three
; walks - 500us a trail pixel on a 4.77MHz machine.
;
; Carry the state instead. Init once; advance it to draw; keep a COPY of the
; freshly-initialised block, and replay that copy in the background colour to
; erase. Same recurrence, same pixels, one walk each way.
;
;   GLS_SZ bytes of your own bss per line. The layout is not yours to read:
;   pass the block, copy it whole (rep movsw) if you want a replay cursor.
;
; Because you may stop and resume anywhere, an erase does NOT have to land in
; one frame: advance the replay cursor a few pixels a frame and a spent trail
; DRAINS instead of popping, with no frame paying for the whole thing.
;
; THE WALK RUNS IN YOUR DIRECTION. gfx_line normalises its endpoints so its
; pixel set is a property of the pair; this cannot, because step 1 has to be
; next to the point you started from. Init draw and erase from the SAME
; endpoints in the SAME order and they agree by construction.
%define OSAPI_GFX_LINIT     KERNEL_SEG:0x0300   ; AX/BX = x1/y1, CX/DX = x2/y2,
                                 ; ES:DI = a GLS_SZ block (X - it is yours).
                                 ; Sets the walk up at (x1,y1). Preserves all.
%define OSAPI_GFX_LSTEP     KERNEL_SEG:0x0308   ; ES:DI = the block, CX = how
                                 ; many pixels to draw in [gfx_color]; the
                                 ; block advances by exactly that many. CX = 0
                                 ; is legal and does nothing. Lock held.
                                 ; Preserves all. N then M == one call of N+M.
; STEP EVERY LINE IN ONE CALL. A walk step draws two or three pixels, and
; PERFORMANCE.md prices the ARRIVING at a drawing call - not the drawing - at
; ~756us on the target machine. So a caller with eight live lines that steps
; them one call each is paying eight floors a frame to move twenty pixels,
; and that, not the walk, is what a moving trail costs. Hand them over
; together: same pixels, one arrival.
;
;   dw block1, count1            ; an array of pairs in YOUR segment,
;   dw block2, count2            ; `block` an offset in it. A zero count is
;   ...                          ; legal and skipped, so build it once and
;                                ; vary what you ask for.
%define OSAPI_GFX_LSTEPV    KERNEL_SEG:0x0318   ; ES:DI = the array, CX = how
                                 ; many pairs. Identical to CX separate
                                 ; OSAPI_GFX_LSTEP calls in one pen. Lock
                                 ; held. Preserves all.
GLS_SZ    equ 14                 ; the walk state, in YOUR bss
; The DISABLED pen (SPEC.md 47 rule 3). A disabled control is CDGRAY *and* the
; [gfx_dis] flag, and this is the only way to set both - which matters most on
; the two 1bpp adapters, where the flag is the entire difference between a
; greyed label and a live one: font_ink masks a flagged glyph to a
; checkerboard, while the colour on its own rounds to solid black.
;
; CF IS THE ARGUMENT, and that is not an oddity - it is the shape every
; greying predicate already answers in, so a call site is `call <your ok-test>`
; then `call OSAPI_GFX_PEN` and nothing between them. The cell is
; push/pop/call/retf and touches no flag, so your CF crosses it unchanged. Use
; `clc` or `stc` first for the unconditional cases.
;
; It is one lock hold long: OSAPI_GFX_UNLOCK clears the flag the way it clears
; the clip region, so you cannot leak dithered text into the next painter. Put
; the pen back with a `clc` call when the control is drawn, all the same - the
; next thing YOU draw in this hold would otherwise come out disabled.
%define OSAPI_GFX_PEN       KERNEL_SEG:0x0310   ; CF = 0 live (CBLACK, flag
                                 ; clear) / CF = 1 disabled (CDGRAY, flag
                                 ; set). Preserves every register.

; --- the system clipboard (SPEC.md 55) ---------------------------------------
; One TEXT buffer for the whole machine, living in the claim heap and sized to
; what is in it. It outlives the app that filled it - the owner is a kernel
; tag, not your instance - so copy in one program, close it, paste in another.
; All three take no lock, touch no VRAM and do no I/O: legal from a window
; callback AND from a worker (SPEC.md 20.6 rule 7).
%define OSAPI_CLIP_PUT      KERNEL_SEG:0x0320   ; ES:SI = the text, CX = its
                                 ; length. CX = 0 EMPTIES the clipboard and
                                 ; is not an error. Out CF=1 = refused (over
                                 ; CLIP_MAXKB, or the heap could not fund it),
                                 ; and then the clipboard is EMPTY rather than
                                 ; stale. Preserves every register.
                                 ; ES:SI, not your DS: a document is usually a
                                 ; claim of its own, so set ES yourself.
%define OSAPI_CLIP_GET      KERNEL_SEG:0x0328   ; ES:DI = your buffer, CX = its
                                 ; capacity. Out CF=1 = empty (AX = CX = 0);
                                 ; CF=0 with AX = the clipboard's WHOLE length
                                 ; and CX = the bytes actually copied. AX > CX
                                 ; means your buffer was too small and you got
                                 ; a prefix. Preserves all but AX and CX.
%define OSAPI_CLIP_SIZE     KERNEL_SEG:0x0330   ; out CF=1 and AX=0 when empty,
                                 ; else CF=0 and AX = the length. Ask this
                                 ; FIRST and grow your own buffer before
                                 ; OSAPI_CLIP_GET - on a machine with a claim
                                 ; heap that is the difference between a paste
                                 ; that works and one that half-arrives.
                                 ; Preserves all but AX.
%define OSAPI_WM_SAVEU      KERNEL_SEG:0x0378   ; BX = window ptr, AL = 0 clear
                                                 ; / non-0 set. A PROMISE: this
                                                 ; window's content does not
                                                 ; change while it is not
                                                 ; drawing, so a raise may put
                                                 ; its old pixels back rather
                                                 ; than call W_PAINT (SPEC.md
                                                 ; 11.96.1). Costs nothing if
                                                 ; refused; wrong if the
                                                 ; promise is
%define OSAPI_EVQ_PENDING   KERNEL_SEG:0x0338   ; out AX = events still queued
                                 ; behind the one being dispatched (SPEC.md
                                 ; 13.4). "Am I about to be asked to do this
                                 ; again?" - for a handler whose work is
                                 ; SUPERSEDED by the next event rather than
                                 ; added to by it, which is every scroll: a
                                 ; held arrow or a spammed track queues a burst
                                 ; whose repaints then play out long after the
                                 ; clicking stopped, and only the last position
                                 ; was ever worth drawing.
                                 ; It answers about the QUEUE, not about whose
                                 ; event is next - so a handler that skips a
                                 ; redraw on it MUST still guarantee the redraw
                                 ; happens, because the next event may belong
                                 ; to another window. Owe it to your worker.
                                 ; Takes no lock; preserves all but AX.
CLIP_MAXKB equ 32                ; the clipboard's ceiling, KB. A put longer
                                 ; than this is refused whole, never truncated

; --- files bigger than your buffer (SPEC.md 18.4.4) ---------------------------
; OSAPI_FILE_READ and OSAPI_FILE_WRITE move a WHOLE file, so the largest file
; a package could touch was the largest claim it could take - a 116KB module
; on a small machine simply could not be opened. These two are the same pair
; in chunks, and they are what the file manager's copy has used internally
; since it was written (SPEC.md 22.5); they are published now because an
; installer needs them and because every package has needed them all along.
;
; ONE RULE GOVERNS BOTH, and it is the same rule at each end: **a chunk is a
; whole number of CLUSTERS**, except the last, which is whatever is left.
; That is what makes an offset land exactly where the previous chunk stopped
; and lets an append start on a fresh cluster instead of filling a partial
; sector inside a chain. Ask OSAPI_FILE_DFREE for the cluster size - it
; answers SECTORS per cluster in BX, and has always done so.
;
; **AND THE CLUSTER SIZE IS THE DESTINATION'S AS WELL AS THE SOURCE'S.** Two
; volumes need not agree - a hard disk partition picks its cluster size from
; its capacity (SPEC.md 52.3) while a floppy is 512 bytes - so a copy chunks
; at a multiple of BOTH. Getting that wrong loses the tail of every cluster.
;
; The read is STATELESS: the offset is the whole argument, there is no handle
; and no cursor, so you may write between two reads. That is the point - the
; loop this exists for is read -> write -> read, and the write walks
; directories and the FAT.
%define OSAPI_FILE_APPEND   KERNEL_SEG:0x0350   ; SI = NUL 8.3 name, ES:BX =
                                 ; bytes, CX = count (>= 1); out CF=0 AX=0,
                                 ; else AX = FERR_*. The file must already
                                 ; exist and its size must be a whole number
                                 ; of clusters - so create it with
                                 ; OSAPI_FILE_WRITE and the first chunk, then
                                 ; append the rest. FERR_NAME if the size is
                                 ; not a cluster multiple - refused, never
                                 ; mis-written - and FERR_PROT on a read-only,
                                 ; hidden or system file, which is SPEC.md
                                 ; 19.6's rule reaching this slot too
%define OSAPI_FILE_READ_AT  KERNEL_SEG:0x0358   ; SI = name, ES:BX = buffer,
                                 ; CX = its capacity in BYTES, DX:AX = the
                                 ; offset to read from; out CF=0 and DX:AX =
                                 ; the bytes delivered, 0 meaning you are at
                                 ; or past the end - which is how the loop
                                 ; terminates. Offset AND capacity must each
                                 ; be a cluster multiple; FERR_NAME if not

%define OSAPI_FILE_MKDIR    KERNEL_SEG:0x0360   ; SI = a NUL 8.3 name; creates a
                                 ; FOLDER in your current directory (SPEC.md
                                 ; 18.5). out CF=0 AX=0, else AX = FERR_*
                                 ; (FERR_EXIST if the name is taken,
                                 ; FERR_DIRFULL if the directory has no free
                                 ; slot, FERR_FULL if the disk has no cluster).
                                 ; There is deliberately no rmdir here: the
                                 ; kernel has one, nothing outside has wanted
                                 ; it, and an unused slot is a promise for
                                 ; nothing (SPEC.md 20.8 rule 4)

%define OSAPI_FILE_GOTO_Q   KERNEL_SEG:0x0370   ; DX = a folder's first cluster
                                 ; (0 = the root), BL = the volume. out CF=0
                                 ; and AX=0, else AX = FERR_*.
                                 ;
                                 ; OSAPI_FILE_GOTO's QUIET twin (SPEC.md
                                 ; 19.2.2), for standing somewhere to READ OR
                                 ; WRITE BY NAME rather than to show it. Inside
                                 ; the volume you are already on it is a WORD -
                                 ; no I/O at all - and crossing to another one
                                 ; keeps the BPB and the FAT window and skips
                                 ; the directory scan, the sort and the icon
                                 ; harvest.
                                 ;
                                 ; Use it in a copy loop, where the difference
                                 ; is the whole cost: nothing you can do between
                                 ; two of these reads the listing, because file
                                 ; names resolve through the raw directory
                                 ; sectors and so does OSAPI_FILE_FIND.
                                 ;
                                 ; It leaves the global listing EMPTY and owed,
                                 ; so it is NOT the one to call before showing a
                                 ; folder, and it does not move your app's own
                                 ; current folder the way GOTO does.

%define OSAPI_REBOOT        KERNEL_SEG:0x0368   ; no arguments, no answer, every
                                 ; register preserved. POSTS a restart (SPEC.md
                                 ; 20.10): the System menu's own Restart, but
                                 ; performed by ui_task at the top of its next
                                 ; pass rather than here. You are holding the
                                 ; gfx lock and that path takes it, so an
                                 ; inline restart would deadlock with the
                                 ; machine already half torn down.
                                 ;
                                 ; So this RETURNS, and your callback should
                                 ; finish normally afterwards - draw what it
                                 ; was going to draw, return to the kernel.
                                 ; The restart happens a moment later, after
                                 ; the Control Panel's settings are flushed and
                                 ; every driver is detached

; --- listing a directory (SPEC.md 19.7.1) -------------------------------------
; The operation this API had no way to express: a package could read a file by
; name and never find out what names were there. Everything in the tree that
; enumerates - the Disk window, the file dialog, the copy engine - is kernel
; code reading the mount snapshot directly, which a package cannot reach.
;
; It is BY ORDINAL and keeps no cursor in the kernel, deliberately. The
; find-first / find-next shape would need kernel state, and the loop this
; exists for is find -> read -> write -> find: every one of those middle steps
; walks directories itself, so a shared cursor would be destroyed by the very
; work the walk is feeding. Ask for 0, then for whatever CX comes back as.
;
; Hidden and system entries are reported to a DRIVER only, the same boundary
; OSAPI_FILE_WRITE_SYS draws in the other direction - so a package can neither
; find a system file nor create one.
;
; The buffer is DSK_FIND_SZ bytes:
;   +0   char[13]  the display name, NUL-terminated (SPEC.md 19's 8.3 form)
;   +13  byte      the raw attribute
;   +14  word      type: OSAPI_FT_* below. READ THAT LIST BEFORE USING IT -
;                  type 1 is not "a file", it is a PACKAGE, and "is this a
;                  file" is `type < OSAPI_FT_DIR`
;   +16  word      first cluster - what OSAPI_FILE_GOTO takes for a folder
;   +18  dword     the size in bytes, all 32 bits (unlike the listing's, which
;                  is clamped to a word)
;   +22  word      reserved, currently 0
OSAPI_FIND_SZ equ 24

; The type word at +14, and the one thing about this record that has already
; been got wrong: TYPE 1 IS NOT "A FILE". dsk_synth (SPEC.md 19) gives 1 only
; to a file the LOADER could run - extension O88, size inside 16 bits, first
; cluster in range - because the listing's consumers are the Disk window and
; the loader. Every other ordinary file is type 0, so a walk that copies
; "type 1" copies the packages and silently skips the documents.
;
; Ask the question you mean:
;   a file   -> type <  OSAPI_FT_DIR
;   a folder -> type == OSAPI_FT_DIR   (and +16 is what FILE_GOTO takes)
;   skip '..'-> type == OSAPI_FT_UP
; The raw attribute is at +13 if you want the FAT byte itself.
OSAPI_FT_FILE equ 0             ; an ordinary file
OSAPI_FT_PKG  equ 1             ; ...that the loader would accept: *.O88
OSAPI_FT_DIR  equ 2             ; a subdirectory
OSAPI_FT_UP   equ 3             ; the synthesized '..' (SPEC.md 19.5)
%define OSAPI_FILE_FIND     KERNEL_SEG:0x0348   ; CX = ordinal (0 to start),
                                 ; ES:DI = an OSAPI_FIND_SZ buffer; out CF=0
                                 ; filled with CX = the next ordinal, else
                                 ; CF=1 and AX = FERR_NOENT at the end

; --- the one cell a PACKAGE may not call (SPEC.md 19.6.1) ---------------------
; It is published here because a DRIVER assembles against this same SDK, and
; refused here in the text because an application author reading the list is
; owed the reason rather than a silent CF=1.
;
; OSAPI_FILE_WRITE with the system attributes: hidden + system, and read-only
; for everything except SYSTEM.CFG, which the kernel rewrites. That is what
; makes an installed volume a SYSTEM volume - KERNEL.SYS and every *.DRV on
; it are then exactly what SPEC.md 19.6 puts on the shipped boot floppy, so
; DOS's DIR hides them and disk_mount's species filter keeps them out of every
; listing, icon grid and file dialog.
;
; The kernel fences it on the caller's segment being a LOADED DRIVER's
; (drv_owns_seg, the SPEC.md 51.7 idiom). From an application it answers
; CF=1 / AX = FERR_PROT and writes nothing - which is the same answer naming
; a system file through OSAPI_FILE_WRITE has always given, and it is the
; whole of SPEC.md 19.6's rule: a package cannot make a file the user can
; neither see nor delete.
%define OSAPI_FILE_WRITE_SYS KERNEL_SEG:0x0340  ; SI = NUL 8.3 name, ES:BX =
                                 ; bytes, DX:CX = count; out as
                                 ; OSAPI_FILE_WRITE. DRIVERS ONLY.
%define OSAPI_BOOT_TICKS    KERNEL_SEG:0x02F8   ; out AX = how long this machine
                                 ; took to boot, in SYSTEM TICKS (18.2065 Hz,
                                 ; 54.925 ms each): the boot sector's first
                                 ; instruction to the first desktop frame
                                 ; being on the glass. It spans the kernel
                                 ; read, the video probe, the mount and every
                                 ; driver SYSTEM.CFG asked for, which is most
                                 ; of what a boot IS on a floppy machine -
                                 ; 65 ms a sector inside a run, and ~400 ms per int 13h
                                 ; CALL, which is the unit that matters
                                 ; (PERFORMANCE.md Part 2).
                                 ; AX = 0xFFFF means the boot sector never
                                 ; stamped it: an image built before the timer
                                 ; existed. Report unknown, do not print it as
                                 ; a duration. Takes no lock, touches no disk.

FSXF_FASTTICK   equ 2           ; OSAPI_FSX_RUN CX bit 1 (SPEC.md 53.2.1): the
                                ; scheduler's quantum drops from 55 ms to 18
                                ; for the bracket, so a worker becoming ready
                                ; costs the drawing side a third of what it
                                ; did. [ticks] keeps its rate, so nothing you
                                ; time in ticks changes
FSX_SUBTICK     equ 3           ; ...and this is the divider, so with FASTTICK
                                ; armed FSXW_FRAME returns 3 x 18.2065 = 54.6
                                ; times a second. A constant, not a
                                ; measurement: it is a crystal over an integer
FSXF_KEEPWORKER equ 1           ; OSAPI_FSX_RUN CX bit 0: your worker stays
                                ; scheduled through the bracket. For workers
                                ; that FEED - an audio ring, a state word.
                                ; A kept worker must never take the gfx lock
                                ; or call a drawing slot: it parks safely on
                                ; the held lock, but a parked feeder is a
                                ; starved ring (SPEC.md 53.2).

; --- OSAPI_FSX_WAIT's clock (SPEC.md 53.5/53.5.1) ---------------------------
FSXW_TICK   equ 0               ; the next [ticks] change - 18.2 Hz, hlt
FSXW_VSYNC  equ 1               ; vertical retrace - and it SPINS on the status
                                ; port, so it takes its whole round-robin
                                ; share whether it needs it or not. Fine for a
                                ; bracket that is alone in the machine; with a
                                ; FSXF_KEEPWORKER it is what starves the
                                ; worker, which for an audio feeder means
                                ; dropouts (SPEC.md 53.5.1)
FSXW_FRAME  equ 2               ; the next IRQ0 - the SUB-TICK while
                                ; FSXF_FASTTICK is armed (54.6 Hz, faster than
                                ; a Hercules' retrace and evenly spaced by the
                                ; crystal), the tick otherwise. It waits by
                                ; YIELDING, so the frame time you do not spend
                                ; goes to your worker instead of to a poll
                                ; loop. USE THIS ONE WITH A KEPT WORKER

FSXM_TEXT80 equ 0               ; 80x25 text        VGA CGA HERC
FSXM_TEXT40 equ 1               ; 40x25 text        VGA CGA
FSXM_CGA320 equ 2               ; 320x200x4         VGA CGA
FSXM_CGA640 equ 3               ; 640x200x2         VGA CGA
FSXM_HERC   equ 4               ; 720x348 mono           HERC
FSXM_VGA0D  equ 5               ; 320x200x16 planar VGA
FSXM_VGA13  equ 6               ; 320x200x256       VGA
FSXM_VGA12  equ 7               ; 640x480x16        VGA
FSXM_MODEX  equ 8               ; 320x240x256, 3 pages, planar addressing VGA

; the block OSAPI_FSX_MODE fills (SPEC.md 53.4). Text modes take the bare
; contract: FSI_W/FSI_H are COLUMNS/ROWS there, the CRTC cursor, the pages
; and the blink bit are yours through the BIOS the bracket may already call
; (AH=02h move, AH=01h shape/hide, AH=05h page, AH=0Eh teletype, AX=1003h
; blink-off - a port bit on a real CGA) or the CRTC ports directly.
FSI_SEG    equ 0                ; word: framebuffer segment
FSI_W      equ 2                ; word: width  - pixels; COLUMNS in text
FSI_H      equ 4                ; word: height - pixels; ROWS in text
FSI_STRIDE equ 6                ; word: bytes per row (Mode X: per PLANE row)
FSI_FLAGS  equ 8                ; byte: FSIF_*
FSI_BPP    equ 9                ; byte: bits per pixel (text: 0)
FSI_BANKS  equ 10               ; byte: interleaved banks (1 = linear)
FSI_PAGES  equ 11               ; byte: display pages
FSI_BSTEP  equ 12               ; word: bank step in bytes (0 when linear)
FSI_MODE   equ 14               ; byte: the FSXM_* id, echoed back
FSI_RSVD   equ 15               ; byte: 0
FSI_SIZE   equ 16

FSIF_TEXT   equ 1
FSIF_PLANAR equ 2
FSIF_BANKED equ 4

; -----------------------------------------------------------------------------
; OS88_ASSOC - lay out one OSAPI_ASSOC_SET argument block (SPEC.md 54.5)
;   my_assoc:  OS88_ASSOC 'BMP', 'PAINT'
; then, from the entry proc:
;   mov si, my_assoc
;   push ds
;   pop es                      ; ES:SI, and ES must be YOUR segment
;   call OSAPI_ASSOC_SET
; Both fields are space-padded here so the caller never has to think about it.
; -----------------------------------------------------------------------------
%macro OS88_ASSOC 2
    %strlen %%elen %1
    %if %%elen > 3
        %error "OS88_ASSOC: an extension is at most 3 characters"
    %endif
    db %1
    times 3 - %%elen db ' '
    %strlen %%slen %2
    %if %%slen > 8
        %error "OS88_ASSOC: an 8.3 stem is at most 8 characters"
    %endif
    db %2
    times 8 - %%slen db ' '
%endmacro
