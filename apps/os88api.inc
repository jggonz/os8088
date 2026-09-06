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
; (OSAPI_TASK_SPAWN below), whose 384-byte stack is likewise in LOW_SEG -
; and 384 is a budget to respect, not a suggestion: the tick, the mouse and
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
DBG_TAG_VIDEO equ 0x4456          ; 'VD' - SPEC.md 57.4, unconditional
DBG_TAG_FDD   equ 0x4446          ; 'FD' - SPEC.md 57.5, unconditional
DBG_TAG_BUILD equ 0x4449          ; 'ID' - SPEC.md 57.6, unconditional
DBG_TAG_BPROF equ 0x5042          ; 'BP' - SPEC.md 15.5, `make BOOTPROF=1`

; What OSAPI_VOL_KIND answers (SPEC.md 18.7.2). The MEDIUM and the TRANSPORT
; are two questions: "is this a hard disk" is the first, and inferring it from
; the second is the bug that section exists to end - a hard disk booted
; through the BIOS is int 13h transport and fixed medium at the same time.
VK_REMOVABLE equ 0               ; AL: a floppy
VK_FIXED     equ 1               ; AL: a hard disk, however it is reached
VT_BIOS      equ 0               ; AH: int 13h
VT_DRIVER    equ 1               ; AH: a DRVC_DISK driver (SPEC.md 52)
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
; --- TEXT, and READ THIS BEFORE PICKING ONE (SPEC.md 6.6) --------------------
; OSAPI_FONT_RUN is the one you want. It draws the background and the glyph in
; ONE pass, so no pixel is written twice and no cell is ever momentarily blank.
;
; The two below are TRANSPARENT: they leave whatever is underneath. That is
; only right when what is underneath is already what you want. A caller that
; fills a rect and then letters it has written every one of those pixels
; twice, and on the machine this OS is for the gap between the two is VISIBLE
; - PERFORMANCE.md Part 1 calls it the double-draw flash and rates it one of
; the three defects an emulator cannot show you.
;
; They are named _XPARENT so that choosing one is a thing you typed rather
; than a thing you defaulted to, and tests/unit/t_textrules.py is the gate:
; a new call site fails the build until it is registered in
; tests/textsites.txt with a reason. SPEC.md 6.6 is the closed list of what
; a good reason looks like - there are five kinds and "it was shorter to
; write" is not one of them.
%define OSAPI_FONT_CHAR_XPARENT     KERNEL_SEG:0x0060   ; CX=x, DX=y, AL=char
%define OSAPI_FONT_STR_XPARENT      KERNEL_SEG:0x0068   ; CX=x, DX=y, SI=NUL string
%define OSAPI_FONT_WIDTH    KERNEL_SEG:0x0070   ; SI=NUL string; out AX = pixel width
; -----------------------------------------------------------------------------
; A masked 1bpp SPRITE, at any x (SPEC.md 25.6). OSAPI_GFX_BLIT1 above is the
; fast one and it wants an x and a width on the byte grid; this one takes a
; MASK instead, so it may land anywhere and it leaves every pixel outside that
; mask exactly as it found it.
;
; The record, in YOUR segment:
;     db wwords, rows          ; wwords = 1 (16 px a row), rows = 1..16
;     dw mask[rows]            ; 1 = this pixel is mine
;     dw data[rows]            ; 1 = draw it in the DATA colour
;   bit 15 of a word is its LEFTMOST pixel.
;
; Reach for it for a control, a small badge, a cursor-sized picture - anything
; whose pen you do not control and whose surroundings are not yours to paint.
; One call replaces one drawing call per set pixel: os88ui.inc's own 12x12
; control went from 45-65 calls to this one, 33.6 ms to 6.7 ms on a 4.77MHz
; 8088 (PERFORMANCE.md Set 84).
;
; A 1bpp adapter has nowhere to put a middle grey, so a DITHERED picture is
; yours to compose - and the dither gfx_ink uses is screen-absolute in (x + y)
; parity (SPEC.md 39.4). os88ui_glyph is the worked example.
%define OSAPI_ICON_PEN      KERNEL_SEG:0x04B0   ; AL = the colour the next
                                 ; OSAPI_ICON_DRAW's MASK rows lay down, AH =
                                 ; the colour its DATA rows draw over them.
                                 ; ONE-SHOT: the draw puts CWHITE/CBLACK back
%define OSAPI_ICON_DRAW     KERNEL_SEG:0x04B8   ; CX = x, DX = y, SI -> the
                                 ; record. out CF = 1 refused (a record wider
                                 ; than one word a row, or taller than 16)
%define OSAPI_GFX_BLIT1_PEN KERNEL_SEG:0x04A8   ; AL = ink (what a SET bit in the
                                 ; next OSAPI_GFX_BLIT1's band becomes), AH =
                                 ; paper (a CLEAR one). SPEC.md 5.4.2.2. The
                                 ; default is CWHITE on CBLACK - what every
                                 ; band got before this existed - and
                                 ; OSAPI_GFX_UNLOCK puts it back, so a pen is
                                 ; valid for exactly the lock hold you set it
                                 ; in. Refuses NOTHING itself; the BLIT then
                                 ; answers CF=1 for a pair whose two colours
                                 ; share no plane in either direction.
                                 ;
                                 ; NOT READ on a 1bpp adapter - a band there
                                 ; already means lit and unlit - so a package
                                 ; drawing a COLOURED sprite sets it per
                                 ; adapter off OSAPI_VIDEO: your colour on
                                 ; VGA, CWHITE on mono. apps/arkanoid is the
                                 ; worked example (SPEC.md 44.10.6)
%define OSAPI_WM_CREATE     KERNEL_SEG:0x0078   ; SI=template; out BX = win ptr, CF on full
%define OSAPI_WM_SHOW       KERNEL_SEG:0x0080   ; BX=win ptr
%define OSAPI_WM_HIDE       KERNEL_SEG:0x0088   ; BX=win ptr
%define OSAPI_WM_DESTROY    KERNEL_SEG:0x0398   ; BX = a window of YOURS; the gfx
                                 ; lock is held, as OSAPI_WM_HIDE wants it. Frees
                                 ; the RECORD - hide only takes the pixels down,
                                 ; and the slot stays used with your segment in
                                 ; its W_SEG. That is fine for a window you keep
                                 ; and fatal for one you close while your IMAGE
                                 ; is about to be unloaded, which is the unowned
                                 ; species (SPEC.md 38.1): a driver's windows,
                                 ; and a package's second one. An app's own
                                 ; window needs neither - closing it tears the
                                 ; instance down and the kernel frees the record
%define OSAPI_WM_FRONT      KERNEL_SEG:0x0090   ; BX=win ptr
%define OSAPI_WM_CONTENT    KERNEL_SEG:0x0098   ; BX=win ptr; out AX=content left, DX=top
%define OSAPI_WM_OBSCURED   KERNEL_SEG:0x00A0   ; BX=win ptr; out CF=1 if covered -
                                 ; which INCLUDES your window being hidden, so a
                                 ; worker gating a draw on this needs no
                                 ; visibility test of its own (SPEC.md 11.3.1)

; --- WHO owns window slot n? (SPEC.md 11.96.3.1) -----------------------------
; A raise cache's owner tag is MEM_P_WSAVE plus the window's SLOT (11.96.3),
; and until this cell existed a slot could not be turned back into anything -
; so a claim walker could say `WinSave` and never whose. Ask this with the
; slot and you get the SEGMENT that owns that window, which is enough for both
; questions worth asking: `is that cache mine` (compare it with your own DS)
; and `whose is it` (match it against the instances you already know about).
;
; It answers a segment and NOT the window record, deliberately: a segment is
; the whole of what the question needs, and a package holding another window's
; pointer could pass it back through any wm_* cell.
%define OSAPI_WM_OWNSEG     KERNEL_SEG:0x04E0   ; AL = a window slot; out CF=1
                                 ; no live window in that slot, else CF=0 and
                                 ; DX = the owning segment - KERNEL_SEG for a
                                 ; window the kernel itself made (the Disk
                                 ; window, the About box). Preserves every
                                 ; register but DX. A free slot answers CF=1
                                 ; rather than its last tenant's segment
%define OSAPI_TASK_YIELD    KERNEL_SEG:0x00A8   ; give up the time slice
%define OSAPI_TASK_SLEEP    KERNEL_SEG:0x00B0   ; AX = ticks to sleep (18 ~ 1 second)
%define OSAPI_GET_TICKS     KERNEL_SEG:0x00B8   ; out AX = [ticks]
%define OSAPI_SET_COLOR     KERNEL_SEG:0x00C0   ; AL -> [gfx_color]
%define OSAPI_MOUSE         KERNEL_SEG:0x00C8   ; out CX=mouse_x, DX=mouse_y, AL=mouse_btn
%define OSAPI_SRAND         KERNEL_SEG:0x00D0   ; AX -> [osapi_seed]
%define OSAPI_RAND          KERNEL_SEG:0x00D8   ; out AX = next pseudo-random word

; --- is this key DOWN right now? (SPEC.md 9.7) -------------------------------
; int 16h answers what was TYPED. There is no key-up in it, and a typematic
; repeat is byte-identical to a fresh press, so "is left held" cannot be asked
; of it - which is why every real-time app here used to infer a hold from the
; INTERVAL between events, and why all of them had the same two bugs: the
; paddle stops dead for the ~9-tick typematic DELAY, in which a held key
; produces no evidence at all, and pressing a SECOND key (fire) ends the
; first key's repeats while the finger is still on it.
;
; This reads the scancodes instead, which carry make AND break. Legal from a
; worker task: no lock, no VRAM, no port.
;
; ASKING IS WHAT ARMS IT, so the first call always answers "up" and a machine
; whose software never asks never touches the keyboard hardware for it. Call
; it once in your entry proc and ignore that answer.
;
; It is ADVICE, not an oracle: a break code lost inside a long interrupts-off
; window leaves that key reading down until it is pressed again, so bound what
; you do with a "yes" (apps/arkanoid clamps at its rails) rather than trusting
; it to end on its own.
%define OSAPI_KEY_DOWN      KERNEL_SEG:0x03F0   ; AL = a make scancode (KSC_*).
                                 ; out CF=1 down, CF=0 up; every register kept
KSC_LEFT    equ 0x4B             ; the scancodes an app is likely to want; the
KSC_RIGHT   equ 0x4D             ; extended arrows land on these too, the E0
KSC_UP      equ 0x48             ; prefix naming no key of its own
KSC_DOWN    equ 0x50
KSC_SPACE   equ 0x39
KSC_ENTER   equ 0x1C
KSC_ESC     equ 0x01

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
%define OSAPI_WM_NOANIM     KERNEL_SEG:0x04F0   ; BX = win ptr; NO other argument and
                                 ; no answer: this window does not zoom
                                 ; open (SPEC.md 11.99.2.1). Call it
                                 ; between OSAPI_WM_CREATE and
                                 ; OSAPI_WM_SHOW. For a DIALOG - something
                                 ; the user opens and dismisses over and
                                 ; over, or that arrives in answer to a
                                 ; click they meant as something else - a
                                 ; third of a second of outline in front
                                 ; of it reads as the machine getting in
                                 ; the way; the Standard File dialog, the
                                 ; notice window and os88ui.inc's alert
                                 ; all decline. Not for your MAIN window:
                                 ; whether the system animates at all is
                                 ; the user's choice (Control Panel ->
                                 ; Theme), not an application's. There is
                                 ; no clear, and no AL is read - see
                                 ; 11.99.2.1 for why.
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
                                 ; SECTORS per cluster.
                                 ;
                                 ; NO DISK I/O - AND THAT IS NOT THE SAME AS
                                 ; CHEAP (SPEC.md 18.4.5). It is answered from
                                 ; the resident FAT snapshot by COUNTING EVERY
                                 ; ENTRY IN IT, so the cost is O(clusters) of
                                 ; CPU: about 105ms per call on a 20MB hard
                                 ; disk on a 4.77MHz 8088. Ask it when you are
                                 ; about to decide something, NOT once per
                                 ; chunk of a copy loop - apps/ftpd did the
                                 ; latter and spent 44% of an upload here
                                 ; (SPEC.md 77.40).

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
                                 ; .. CX-1), DL = 0 VGA / 1 Hercules / 2 CGA /
                                 ; 3 EGA, DH = bits per pixel, 4 or 1. Callable
                                 ; from any context, lock held or not.
                                 ;
                                 ; DON'T switch on DL as a fourth case: EGA is
                                 ; VGA's planar path at 640x350 (DH = 4). The
                                 ; two real questions are DH (depth) and your
                                 ; window size - test `cmp dh, 1` / `ja` for
                                 ; "colour", never `cmp dl, VID_VGA`.
VID_VGA    equ 0                 ; the DL values
VID_HERC   equ 1
VID_CGA    equ 2
VID_EGA    equ 3                 ; 640x350x16 - a VGA-path adapter (SPEC.md 39.24)
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
                                 ; the task table is full - MAX_TASKS slots,
                                 ; defined below, so this prose cannot drift
                                 ; from the constant again - a normal
                                 ; outcome: degrade, do not abort, and DO
                                 ; retry later, it is transient);
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

; --- running a package you are HOLDING (SPEC.md 21.5) -------------------------
; The loader's back half with the disk read replaced by a copy. It exists for
; the Wire (SPEC.md 26.7), which fetches a .O88 over the network into a claim
; and has nowhere to put it: everything else in this system launches a package
; by naming a FILE, and there is no file here.
%define OSAPI_PKG_RUN       KERNEL_SEG:0x04F8   ; ES:SI = a package image, byte for byte
                                 ; what the .O88 file holds, in a claim of
                                 ; YOURS. ANY segment - it is COPIED into a
                                 ; region of the kernel's own, never
                                 ; adopted, so you may free your claim the
                                 ; moment this returns. DX:CX = its length
                                 ; in bytes, DX the high word. DI = a
                                 ; NUL-terminated 8.3 name, at most 12
                                 ; characters, in YOUR OWN segment (not
                                 ; ES): what the new instance is told it
                                 ; was launched from, which is its entry
                                 ; proc's SI (SPEC.md 20.2).
                                 ;
                                 ; Out CF=0 with AX = 0: it is registered,
                                 ; published and its window shown, exactly
                                 ; as a Disk-window double-click leaves
                                 ; one. CF=1 with AL = an LD_* code
                                 ; (SPEC.md 21.4): 2 the header is not a
                                 ; package's, or it carries PARTS (header
                                 ; flags bit 2 - parts are read from the
                                 ; package's own FILE and there is none
                                 ; here); 3 too large; 4 its entry proc
                                 ; declined; 5 no region or no instance
                                 ; record.
                                 ;
                                 ; UI TASK ONLY - a window callback or an
                                 ; OSAPI_WM_ONWAKE handler - with the gfx
                                 ; lock NOT held. It takes the lock itself
                                 ; around the new window's first paint, and
                                 ; the entry proc it calls runs with the
                                 ; lock free like every other package's.
                                 ;
                                 ; The new instance's CURRENT DIRECTORY is
                                 ; YOURS at the time of the call (SPEC.md
                                 ; 19.2.1), so a package with an overlay or
                                 ; sidecars run this way looks for them
                                 ; where you are standing. Run one that has
                                 ; them from a disk instead.

; --- the desktop SERVICE zone, DRIVERS ONLY (SPEC.md 26.7) --------------------
; A driver puts an icon on the desktop. It is here rather than in
; drivers/os88drv.inc for OSAPI_FILE_WRITE_SYS's reason: a driver assembles
; against this same file, and an application author reading the list is owed
; the refusal's reason rather than a silent CF=1.
;
; The kernel keeps no glyph and no name: it holds the record, draws the
; caption, hit-tests the zone and launches the file, and calls the DRIVER back
; to paint the picture. That is the whole design - most XTs have no network
; card, and a desktop icon whose art sat in every kernel would be carried by
; every machine that cannot use it.
%define OSAPI_DESK_SVC      KERNEL_SEG:0x0500   ; AL = 1 add / 0 withdraw;
                                 ; ES:SI = a 65-byte record in YOUR segment
                                 ; (add only):
                                 ;   +0  12  the caption, NUL (<= 11 chars)
                                 ;   +12 13  the 8.3 file the zone launches,
                                 ;           from the BOOT volume's SYSTEM/
                                 ;   +25  1  your DRVC_* class
                                 ;   +26  1  the verb the kernel calls to
                                 ;           PAINT the icon: CX,DX = its
                                 ;           top-left, the gfx lock HELD, and
                                 ;           it MUST NOT touch your hardware -
                                 ;           a desktop repaint can land while
                                 ;           another task is inside one of
                                 ;           your other verbs
                                 ;   +27 12  the package's HEADER name, NUL -
                                 ;           what makes the zone a singleton
                                 ;   +39 26  the failure notice's lead line,
                                 ;           NUL ('Cannot open Wire:')
                                 ; out CF=1 refused: not a published driver
                                 ; (OSAPI_VOL_ADD's own fence), a second
                                 ; registration - there is ONE zone and
                                 ; whoever attaches first has it - or a
                                 ; withdraw of somebody else's.
                                 ;
                                 ; The record is COPIED, so it may be
                                 ; anything of yours; the kernel terminates
                                 ; the caption itself and trusts the other
                                 ; three the way it trusts your DSV table.
                                 ; WITHDRAW AT DETACH: the kernel is about to
                                 ; free the image your paint verb lives in.
                                 ; drivers/wirezone.inc is the worked example.

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

%define OSAPI_VOL_KIND      KERNEL_SEG:0x01E8   ; AL = a volume index (0 = A:).
                                 ; CF=1 = there is no such volume; CF=0 with
                                 ; AL = VK_REMOVABLE / VK_FIXED and AH =
                                 ; VT_BIOS / VT_DRIVER (SPEC.md 18.7.2). The
                                 ; first thing a package can ask about a
                                 ; volume without USING it: OSAPI_VOL_* are
                                 ; the driver's and refuse you. Ask the MEDIUM
                                 ; when you mean "is this a hard disk" - a
                                 ; boot partition is VT_BIOS and VK_FIXED at
                                 ; once.
                                 ;
                                 ; THIS NUMBER WAS OSAPI_FILE_READBIG, retired
                                 ; at SPEC.md 18.4.1 when OSAPI_FILE_READ lost
                                 ; its 64KB ceiling, and taken back here under
                                 ; SPEC.md 20.3.1: a withdrawn cell has no
                                 ; contract to collide with, its %define was
                                 ; deleted, and nothing in the tree names it -
                                 ; so a stale caller still fails to assemble
                                 ; rather than calling this by accident. If
                                 ; you were using readbig: pass your base
                                 ; segment in ES with BX = 0 and call
                                 ; OSAPI_FILE_READ.
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
; Main's three paragraph-counting arena slots at 0x01B8..0x01C8 (OSAPI_MEM_ALLOC
; / OSAPI_MEM_FREE / OSAPI_MEM_AVAIL in main's SDK) keep main's numbers and
; contracts, and are answered by the claim heap through converting wrappers. New
; code should prefer the KB slots below: they are the native shape, they work
; from the entry proc, and only they carry the DMA-page and regrow contracts.
; Every slot this branch APPENDED starts at 0x0200, main's next free number.
;
; ONE slot main published CHANGED CONTRACT: 0x01F0 was OSAPI_GFX_DBUF and is now
; OSAPI_WM_ONMOUSEUP, taken off SPEC.md 20.3.1's free list once 32's back buffer
; was withdrawn. So REBUILD every package against this SDK - a main-era .o88
; still loads (same format version, same KERNEL_SEG) and would far-call 0x01F0
; expecting the double buffer, so a main-era apps disk must not be booted
; against this kernel.
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
; ...AND THE SAME TWO FROM THE TOP OF THE HEAP (SPEC.md 50.3.2). For a buffer
; THE HARDWARE holds the address of - a sound card's DMA ring, a NIC's
; descriptors - which can therefore never be moved and has no business in the
; arena every data claim and the compactor share. SPEC.md 50.3 says why in its
; own words: "one long-lived data claim landing mid-heap permanently splits the
; space a package can be loaded into". If software is the only thing that reads
; your block, use OSAPI_MEM_CLAIM above and let it be compacted.
%define OSAPI_MEM_CLAIM_HI  KERNEL_SEG:0x04C8   ; AX = KB; out CF, DX = segment
%define OSAPI_MEM_CLAIM_DMA_HI KERNEL_SEG:0x04D0 ; ...and CX = the page-safe HEAD

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
; ...and the OTHER end of it (SPEC.md 5.4.3). OSAPI_GFX_BLIT4 takes PIXELS and
; works out what the card wants; this takes what the card wants and does
; nothing but put it there - one `rep movsb` per plane per row, which is the
; traffic OSAPI_GFX_SCROLL moves for the same area and the floor a 16-colour
; block has on this card.
;
; **IT REFUSES MORE THAN IT ACCEPTS AND YOU MUST READ CF.** No on: a 1bpp
; adapter (four planes is not a thing a Hercules has), an x that is not a
; multiple of 8, a block off either side of the screen, an armed clip region,
; a block straddling two displays, and a kernel built without the planes.
; The answer to every one of them is OSAPI_GFX_BLIT4, which still works and is
; still flat in run density - so the fallback is the code you already had.
;
; ALIGNING x IS FREE FOR A WINDOW: SPEC.md 11.94 already snaps the content
; origin to a multiple of 8, so what is left is your own inset from it.
%define OSAPI_GFX_BLITP     KERNEL_SEG:0x04C0   ; ES:SI = plane 0's first row,
                                 ; DI = the step to the next plane, BP = the
                                 ; row stride inside one, AX = x (A MULTIPLE
                                 ; OF 8), BX = y, CX = width in pixels, DX =
                                 ; rows. out CF = 1 refused, nothing drawn
                                 ;
                                 ; **DI BIT 15 SET IS A PROBE** (SPEC.md
                                 ; 5.4.3.2): every refusal above is decided
                                 ; against the rect you passed and CF comes
                                 ; back without a pixel being written. ES:SI
                                 ; and BP are not read on that path, so you can
                                 ; ask BEFORE you have any planes - which is
                                 ; the case it exists for. "Is this window
                                 ; wholly on a colour display?" has no other
                                 ; answer: OSAPI_WM_DISPLAY names a display and
                                 ; says nothing about a straddle, and SPEC.md
                                 ; 39.16.4.2 resolves a straddle by usable BAND
                                 ; rather than by colour. A refused probe
                                 ; changes nothing, so asking again next
                                 ; repaint is the intended use.
; -----------------------------------------------------------------------------
; A RUN OF SCAN LINES, one call (SPEC.md 5.10)
;
; Every gfx_* call costs about the same whatever it draws, and SPEC.md 5.7 is
; the account of why. Measured on a 4.77MHz 8088 for one 8x8 fill on a
; Hercules: 381 cycles of wrapper, 1,236 of rect setup and 3,197 of rows. So
; BATCHING RECTS BUYS ALMOST NOTHING - hoisting the wrapper is 7% - and this
; call is not that. It is for the shape a rect cannot say cheaply: ONE X
; INTERVAL PER CONSECUTIVE ROW, which is what a polygon rasteriser, a swept
; brush and a sprite silhouette all produce. The rows being consecutive is
; what pays: the framebuffer base steps by an add instead of a lookup and the
; vertical clip is done once for the whole run.
;
; Paint's 45-degree brush chord is the worked example: eight 8x8 dabs are 64
; row-writes for a shape that is 15 rows, and as 15 spans it is 15.
;
; x1 > x2 IS AN EMPTY ROW and costs the step alone, so a caller with a hole in
; its shape does not have to split the call. A row with TWO intervals does.
;
; **CF = 1 IS A REFUSAL AND YOU MUST READ IT**: an armed clip region, a second
; display, or a kern_small kernel - and nothing was drawn. The fallback is one
; OSAPI_GFX_FILL per span, which draws the identical pixels; keep it, it is
; also the only path on a 128KB machine.
%define OSAPI_GFX_SPANS     KERNEL_SEG:0x04D8   ; AX = the y of the FIRST span,
                                 ; ES:SI = CX records of two words each, x1
                                 ; then x2, both INCLUSIVE; CX = rows, row n
                                 ; being y+n. ES is yours - the stub puts your
                                 ; DS there - so the list lives in your own
                                 ; segment. Fills them in the current colour.
                                 ; out CF = 1 REFUSED, nothing drawn
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
                                 ; Honours the clip region.
%define OSAPI_GFX_BLIT1     KERNEL_SEG:0x0418   ; ES:SI = a 1bpp BAND in the
                                 ; framebuffer's own bit order (row-major, bit
                                 ; 7 leftmost, 1 = a LIT pixel), BP = its
                                 ; stride in BYTES per row, AX = destination x
                                 ; and CX = width in pixels - BOTH MUST BE
                                 ; MULTIPLES OF 8 - BX = y, DX = rows, 1..255.
                                 ; x and y are SIGNED: a window dragged half
                                 ; off the left edge has content at a negative
                                 ; x, and whole 8-pixel columns simply drop.
                                 ; out CF=1 = REFUSED and nothing drawn: the
                                 ; arguments broke one of the rules above, or
                                 ; this is a kern_small machine, which carries
                                 ; the slot and not the body. TEST CF AND HAVE
                                 ; A SECOND PATH - letter the run with
                                 ; OSAPI_FONT_RUN in the 8x8 face.
                                 ;
                                 ; This is the call for text your package has
                                 ; SET ITSELF (SPEC.md 5.4.2): compose the
                                 ; whole row of proportional glyphs into your
                                 ; own RAM and put it down in ONE drawing call
                                 ; rather than one per glyph at SPEC.md 5.7's
                                 ; ~756us floor. There is no ink or paper
                                 ; argument and [gfx_color] is not read - the
                                 ; band arrives in FINAL screen polarity, so
                                 ; SPEC.md 47's greying is YOURS to apply
                                 ; before you hand it over (read the flag with
                                 ; OSAPI_GFX_PEN_CF). Honours the clip region,
                                 ; to the exact pixel ROW.
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
; ...and OSAPI_CPU_INFO's AH, which this file described in prose and did not
; name. They are one contract with three readers now - the kernel publishes
; them, XMEM.DRV sets the three memory bits (SPEC.md 41.12.2) and the boot
; probe sets CPU_F_X87 - so XMEM.DRV must write ITS THREE and leave the byte's
; other bits alone, which it did not always do - and a package may test them -
; so they are spelled once here rather than three times. NONE OF THEM IS THE
; THING TO BRANCH ON: a package asks OSAPI_XMEM_CAPS for KB, always, because
; these say what the machine can do and that one says what is actually there.
CPU_F_A20           equ 0x01     ; the A20 line VERIFIED open - by a
                                 ; wraparound read, never by "we wrote to the
                                 ; gate" (SPEC.md 41.2)
CPU_F_HMA           equ 0x02     ; the High Memory Area is claimed (41.3)
CPU_F_UNREAL        equ 0x04     ; unreal mode armed: FS and GS carry a 4GB
                                 ; data limit (41.4). Tier 2 only
CPU_F_X87           equ 0x08     ; an 8087/287/387 IS IN THE SOCKET, probed
                                 ; at boot with FNINIT and a status-word read
                                 ; back - not INT 11h's equipment word, whose
                                 ; bit on a 5150 is a DIP switch reporting
                                 ; what the owner claimed. A 5150 shipped with
                                 ; that socket EMPTY and many were filled
                                 ; later, so this is a per-machine fact rather
                                 ; than a tier fact, which is why it lives
                                 ; here and not in CPU_8086/286/386.
                                 ; TEST IT BEFORE EVERY COPROCESSOR PATH, and
                                 ; understand what happens if you do not: on a
                                 ; machine with an empty socket an `fadd` does
                                 ; NOT fault, it HANGS - the WAIT in front of
                                 ; it blocks forever on a BUSY line nothing
                                 ; will lower. No `cpu` island is involved:
                                 ; the 8087's base instructions assemble at
                                 ; `cpu 8086` already, so rule 1 stands with
                                 ; no exception and the gate here is a
                                 ; RUN-TIME one (SPEC.md 1 rule 1, 84.6).
;
; --- volumes (SPEC.md 18.7/51.8) --------------------------------------------
; A DRVC_DISK DRIVER's slots, not an application's: the four that CHANGE
; something are fenced on the caller being the driver whose disk services are
; published, so a package gets CF=1 and nothing else. They are published here
; because drivers include this file (drivers/os88drv.inc does), and because a
; slot number belongs in one place (SPEC.md 20.8).
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
%define OSAPI_VOL_AT        KERNEL_SEG:0x03E8   ; DL = an int 13h drive number,
                                 ; BX:CX = a partition's 32-bit base LBA (BX =
                                 ; the high word). out CF=0 and AL = the volume
                                 ; index ALREADY covering it, CF=1 = none is.
                                 ; Preserves every other register.
                                 ;
                                 ; ASK THIS BEFORE OSAPI_VOL_ADD, for every
                                 ; partition you are about to mount. On a
                                 ; machine installed to its hard disk the
                                 ; KERNEL adopted the partition it booted from
                                 ; before your driver was even loaded - it had
                                 ; to, your image being a file on that volume
                                 ; (SPEC.md 52.10.3) - and nothing in your own
                                 ; table says so, so mounting it again puts a
                                 ; second drive icon on the same sectors.
                                 ;
                                 ; It answers about BIOS-transport volumes and
                                 ; only those. Your own rows are yours to
                                 ; recognise: the kernel never learns their
                                 ; partition base (it hands you a
                                 ; volume-relative LBA and you add it), and a
                                 ; DV_UNIT that is your own volume handle would
                                 ; collide with an int 13h drive number for
                                 ; free. Unfenced, because it changes nothing

; --- a REDIRECTED volume's listing (SPEC.md 62.9.1) -------------------------
; A DRVC_FILE DRIVER's slot, and the narrowest fence in the table: it is legal
; only from inside FSV_LIST, which is a gate on the MOMENT rather than on the
; caller. The kernel raises it around that one call and lowers it after.
%define OSAPI_FS_ENT        KERNEL_SEG:0x03C0   ; ES:SI -> a DSK_DE_SIZE-byte
                                 ; staged SPEC.md 19.1 entry in YOUR OWN
                                 ; segment: name at 0 (NUL-terminated 8.3),
                                 ; type at 16 (0 file / 1 package / 2 folder -
                                 ; NEVER 3, the parent link is the kernel's),
                                 ; YOUR OPAQUE HANDLE at 18, size dword at 20,
                                 ; bytes 24..31 zero.
                                 ; out CF=0 and AX = the index it landed at;
                                 ; CF=1 = the listing is full, or you called it
                                 ; from outside FSV_LIST.
                                 ; THE KERNEL STILL SORTS (19.4) AND STILL
                                 ; SYNTHESIZES '..' (19.5) - do neither, or a
                                 ; display index stops meaning what the
                                 ; hit-test thinks it means
%define DSK_DE_SIZE         32   ; ...the entry's stride, which is the
                                 ; LISTING's and not the meaningful prefix's

; --- a redirected transfer's PROGRESS (SPEC.md 12.8.1/62.9.1) ---------------
; The other half of the same driver's contract, and the one thing the kernel
; cannot do for you: it arms SPEC.md 12.8's file-activity bar from FSV_STAT's
; size and is then ONE FAR CALL DEEP inside your FSV_READ, blind, until you
; return. A floppy steps the bar from dsk_xfer's own per-sector loop; a cable
; has no such loop in kernel code, so a redirected read of a 116KB module
; reported nothing at all until this existed.
%define OSAPI_FS_PROG       KERNEL_SEG:0x03C8   ; AX = bytes moved SINCE YOUR
                                 ; LAST REPORT - a running total would advance
                                 ; the bar by the whole file every call.
                                 ; A short count is ordinary and not an error:
                                 ; the remainder below 512 bytes is carried,
                                 ; not rounded away, so reporting a wire's
                                 ; every 64-byte frame is exact.
                                 ; Unarmed it is a compare and a return, so
                                 ; call it unconditionally and never test.
                                 ; IT DRAWS - so the gfx lock must already be
                                 ; held, which it is inside any file operation
                                 ; (SPEC.md 18), and you must NEVER take it.
                                 ; Preserves every register and the flags

; --- a driver's settings, inside SYSTEM.CFG (SPEC.md 51.9) ------------------
; A DRIVER's slot, fenced like the four above. The kernel carries DRV_BLOB_SZ
; opaque bytes to disk and back with the rest of the Control Panel; it never
; reads them, and it round-trips them untouched on a machine where the driver
; never loads. Versioning them is the driver's job.
; **THERE ARE TWO BLOBS AND YOURS IS PICKED BY YOUR drv_tab ROW** (SPEC.md
; 51.9.1), so DRV_BLOB_SZ is not one number: the hard disk's is 34 bytes and
; the RAM disk's is 24. It is the ROW and not the class, because the RAM disk
; and the parallel link share DRVC_FILE and a class-keyed blob would hand each
; of them the other's settings. A driver with no row in the map is refused.
%define DRV_BLOB_SZ         34
%define DRV_RBLOB_SZ        24
%define OSAPI_DRV_CFG       KERNEL_SEG:0x0290   ; AL = 0 read / anything else
                                 ; write, ES:SI = the driver's buffer, CX =
                                 ; bytes (<= YOUR blob's size).
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

; --- a DRIVER page's file dialog (SPEC.md 51.10) -----------------------------
; OSAPI_FILE_DLG is unreachable from a driver and always was: fdlg_open fences
; on a live INSTANCE owning the window it is handed (SPEC.md 38.1), because
; the staleness triple it banks is what stops a commit calling into a region
; that has been freed. A driver has no instance and no such window.
;
; This is that call with the panel's own window supplied by the kernel - which
; is the honest answer rather than a way past the fence, your page being drawn
; inside that window in the first place. Everything else is OSAPI_FILE_DLG's:
; it does NOT block, Cancel calls nothing at all, and it must be called from a
; page CLICK and never from a page paint.
;
; Your completion proc is reached the way every other page cell is - a far
; call through your own dispatcher, DS = your segment, ES = KERNEL_SEG - with
;   AL = the mode it ran in, ES:DI = the chosen NUL name, DX:CX = its size.
; The gfx lock is held and the dialog is already off the screen, so you may
; draw. If your driver is unticked while its own dialog is up, the answer is
; dropped rather than dispatched into whatever took your image's memory.
%define OSAPI_DRV_DLG       KERNEL_SEG:0x0428   ; AL = FDLG_OPEN / FDLG_SAVE,
                                 ; ES:SI = a default name (NUL, <= 12) or 0,
                                 ; DI = your near completion proc. out CF=1 =
                                 ; refused: not a published driver, no Control
                                 ; Panel on screen, or a dialog already up

; --- the three snapshots, and a patterned fill (SPEC.md 20.9) ----------------
; The first three answer questions about the SYSTEM rather than about you: the
; scheduler and the instance table, the claim map, and how big the kernel is.
; Every one of them fills a buffer YOU own, through ES:DI, and returns a count
; so a package built against an older SDK finds out that a table has grown.
; THE COUNT DOES NOT SHORTEN THE WRITE: the kernel fills the whole layout, so
; what keeps an old buffer from being overrun is that the layout is pinned at
; a ceiling (SS_TMAX below) rather than at the live MAX_TASKS - it moved once,
; 8 -> 14, and a stale TASKMGR was written 30 bytes past its buffer. Any
; context - they take no lock and touch no VRAM.
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
SS_TSTATE  equ 6                ; MAX_TASKS bytes, the kernel's T_STATE - with
                                ; ONE VALUE THAT IS THE SNAPSHOT'S OWN: 3 is
                                ; the idle task (SPEC.md 8.1.2, 28.7), which
                                ; the kernel's own table records as 1/ready
                                ; like anything else. It is a fourth value
                                ; rather than a slot index in the header
                                ; because the header has no spare byte and
                                ; every field after one would move
                                ; verbatim: 0 free, 1 ready, 2 SLEEPING
                                ; (SPEC.md 8). There is no "running" state -
                                ; the running task is SS_CUR, and its T_STATE
                                ; is 1 like any other ready task
SS_TMAX    equ 16               ; the LAYOUT's ceiling on MAX_TASKS, so the
                                ; offsets below stop moving with the count:
                                ; 8 -> 14 moved SS_INST by 30 bytes, and a
                                ; TASKMGR built against the old SDK was
                                ; written past its own buffer by exactly that
SS_TCYC    equ SS_TSTATE + SS_TMAX         ; SS_TMAX dwords: MAX_TASKS dwords: cycles per task
SS_INST    equ SS_TCYC + SS_TMAX * 4       ; ...then the instance records
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

; --- decoding CLS_OWN (SPEC.md 50.2/50.6, mirrored from kernel/memory.inc) ----
; The owner word is a NAMESPACE, and these are the names in it. They are a
; DECODE AID for the snapshot above, not a promise: the kernel owns these
; numbers and may renumber them, and a renumber has to change both files - the
; same standing arrangement MAX_TASKS, INST_MAX and MEM_MAX are under, and the
; reason each of those carries a "mirrored" comment on the kernel side too.
;
; Nothing SHIPPED reads these - a package has no business knowing what the
; kernel cached. They are here for the Task Manager's heap page (SPEC.md 28.4),
; which is a debug view of exactly this table, and for anything else that later
; wants to explain the heap to a human rather than allocate out of it.
;
; 0..INST_MAX-1  an instance slot: a package's REGION, or a built-in's claim
; >= HEAP_SEG    a package's own segment: one of that package's data claims
; 0xFFxx         a kernel tag, unpurgeable
; 0xFBxx..0xFExx a kernel tag that is PURGEABLE, the HIGH BYTE being the tier
MEM_K_SAVE  equ 0xFF01          ; the menu save-under (SPEC.md 12)
MEM_K_DRV   equ 0xFF03          ; a loaded driver's image (SPEC.md 51.3)
MEM_K_COPY  equ 0xFF04          ; the file manager's copy buffer (SPEC.md 22.5)
MEM_K_ASC   equ 0xFF06          ; one volume's ASSOC.DAT cache (SPEC.md 54.7)
MEM_K_CLIP  equ 0xFF07          ; the system clipboard's text (SPEC.md 55)
MEM_K_MOD   equ 0xFF08          ; an on-demand kernel module's image (SPEC.md 2.8)
MEM_K_CLONE equ 0xFF09          ; the disk cloner's buffer (SPEC.md 18.99)
MEM_K_BAND  equ 0xFF0A          ; the 1bpp band composer's buffer (SPEC.md 5.9.2)
; 0xFF0B WAS MEM_K_OVL, the relocated boot overlay. It rides stage 2's blob
; now (SPEC.md 2.9.6) and is not a claim at all, so the tag is retired and
; LEFT UNUSED - a value that meant one thing in a shipped kernel and another
; in the next is a collision a debug read cannot see.
MEM_K_HIB   equ 0xFF0C          ; the resume's extent list (SPEC.md 87.5)

MEM_PG_TRIV equ 0xFB            ; the purge tiers (SPEC.md 50.6.4), low to
MEM_PG_LOW  equ 0xFC            ; high: what LOSING the block costs, which is
MEM_PG_MED  equ 0xFD            ; what decides which cache a claimant takes
MEM_PG_HIGH equ 0xFE            ; first
MEM_PG_MIN  equ MEM_PG_TRIV     ; the range: a record is a cache when its high
MEM_PG_MAX  equ MEM_PG_HIGH     ; byte is in [MIN, MAX]
MEM_LVL_TOP equ 0xFF            ; ...and an ordinary claim outranks all of them

MEM_P_WSAVE equ MEM_PG_TRIV<<8 | 0x10   ; a window's raise cache - a RANGE,
MEM_P_WSAVE_N equ 16            ; 0x10..0x1F, the window's slot added (11.96.3)
                                ; - and OSAPI_WM_OWNSEG above turns that slot
                                ; back into the segment that owns the window
MEM_P_FATW  equ MEM_PG_MED<<8 | 0x20    ; one volume's FAT window - a RANGE,
MEM_P_FATW_N equ 8              ; 0x20..0x27, the VOLUME added (SPEC.md 18.8.4)
MEM_P_DIRW  equ MEM_PG_HIGH<<8 | 0x02   ; the directory read-ahead window (18.95)

; --- OSAPI_SYS_KB's buffer, all in KB ----------------------------------------
SK_KERN    equ 0                ; word: the whole kernel span
SK_IMG     equ 2                ; word: image + scratch + cold code + the
                                ; kernel's own tables, which are .bss in every
                                ; sense but the segment that holds them
SK_FAT     equ 4                ; word: the mount-time FAT window
SK_STK     equ 6                ; word: the background task stacks
SK_STK0    equ 8                ; word: task 0's - the UI task's
SK_DSK     equ 10               ; word: the three disk buffers, and no longer
                                ; whatever else shared their segment
SK_BUF     equ 12               ; word: FAT + stacks + disk buffers together
SK_HEAP    equ 14               ; word: the claim heap's size
SK_CLAIM   equ 16               ; word: claimed out of it, every owner - and
                                ; no purgeable claim, which is in nobody's
                                ; total because the next claim can have it
                                ; (SPEC.md 50.6.3). OSAPI_MEM_AVAIL is the
                                ; other side of the same statement
SK_KCLAIM  equ 18               ; word: ...of which the kernel's own
SK_XMS     equ 20               ; word: the pool above 1MB, 0 with no store
SYSKB_SIZE equ 22

SCH_STACK  equ 384              ; THE LARGEST SLOT CLASS (SPEC.md 8.7), which
                                ; is the only slice size a package may assume
                                ; anything about. Your worker gets the smallest
                                ; free slice that is at least what you asked
                                ; for - so it can be BIGGER than your class,
                                ; and anything sizing a window against "my
                                ; stack" has to use this maximum or the window
                                ; comes out short. apps/cc/crt0.asm's cc_iswk
                                ; is the one thing in the tree that does.
                                ; Mirrored from kernel/sched.inc and checked by
                                ; tests/unit/t_mirror.py
MAX_TASKS  equ 14               ; the scheduler's slots (SPEC.md 8) - a bound
INST_MAX   equ 12               ; running instances (SPEC.md 29.1)   on the
MEM_MAX    equ 32               ; claim records (SPEC.md 50)         buffers
                                ; above, and each call answers the live figure
                                ; so a stale SDK can SEE the mismatch - the
                                ; snapshot's layout is pinned at SS_TMAX so it
                                ; is not also an overrun; the other two are
                                ; an overrun
KIND_PKG   equ 0x80             ; SSI_KIND bit 7: this instance is a package
%if MAX_TASKS > SS_TMAX
  %error "MAX_TASKS has outgrown the snapshot's layout: raise SS_TMAX in BOTH kernel/instance.inc and apps/os88api.inc, and every package built against the old SDK overruns its buffer"
%endif

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
                                 ; kernel is about to make. Its other half is
                                 ; OSAPI_WM_ONRESIZE, which is the NOTICE after
                                 ; a change nobody proposed - see below.
%define OSAPI_WM_ONDRAG     KERNEL_SEG:0x0430   ; BX = win ptr, AX = a near proc
                                 ; in YOUR segment (0 clears it). THE TRACKING
                                 ; EDGE (SPEC.md 13.8.2): the pointer moved
                                 ; while your press was armed. CX = x, DX = y,
                                 ; SI = window - W_ONCLICK's environment
                                 ; exactly, on the UI task with the gfx lock
                                 ; held and billed to you.
                                 ;
                                 ; Between the W_ONCLICK and the W_ONMOUSEUP of
                                 ; ONE gesture, only when the point CHANGED,
                                 ; and OUTSIDE the window as freely as the
                                 ; release - which is the whole point: a
                                 ; control cannot un-draw its pressed state as
                                 ; the pointer slides off it if nothing tells
                                 ; it the pointer left. Range-test CX/DX
                                 ; yourself; they may be negative once you
                                 ; subtract your content origin.
                                 ;
                                 ; REDRAW ONLY ON A CHANGE. This is called per
                                 ; pointer movement, so a handler that redraws
                                 ; its control every time is tens of ms of
                                 ; repaint per mouse packet on a 4.77MHz
                                 ; machine. Keep which control is down in a
                                 ; variable your W_PAINT reads (SPEC.md 13.8),
                                 ; compare, and draw the two that changed.
                                 ;
                                 ; Call it AFTER wm_create. Do NOT combine it
                                 ; with a polling OSAPI_MOUSE loop - SPEC.md
                                 ; 13.7's rule, and for the same reason.
                                 ;
                                 ; out CF=1 = REFUSED and nothing armed:
                                 ; this is a kern_small machine, which
                                 ; carries the slot and not the body. TEST
                                 ; CF AND HAVE A SECOND PATH - there is no
                                 ; tracking and no timer there, so draw the
                                 ; static down state SPEC.md 13.8.2
                                 ; describes and leave it down.
%define OSAPI_WM_TIMER      KERNEL_SEG:0x0438   ; BX = win ptr, AX = TICKS from
                                 ; now (0 cancels). CALL ME BACK IN N TICKS
                                 ; (SPEC.md 13.9) - 18.2 ticks a second, the
                                 ; clock the whole system already keeps.
                                 ;
                                 ; WHAT IT IS FOR IS UN-DRAWING. A control
                                 ; drawn down by a KEYSTROKE has no release
                                 ; edge to come back up on, and a callback
                                 ; cannot sleep - it holds the gfx lock. The
                                 ; only other answer was a worker task, which
                                 ; is one of MAX_TASKS task slots and
                                 ; SCH_STACK bytes of stack for a 165ms flash.
                                 ;
                                 ; ONE-SHOT, and it disarms itself BEFORE your
                                 ; handler runs - so re-arm inside the handler
                                 ; for a repeat (a blinking caret), and do
                                 ; nothing for a single shot.
                                 ;
                                 ; It fires while your window is MINIMIZED
                                 ; too: the kernel delivers and you decide
                                 ; (ask OSAPI_WM_GEOM). It does NOT fire after
                                 ; your window is destroyed.
                                 ;
                                 ; out CF=1 = REFUSED and nothing armed:
                                 ; this is a kern_small machine, which
                                 ; carries the slot and not the body. TEST
                                 ; CF AND HAVE A SECOND PATH - there is no
                                 ; tracking and no timer there, so draw the
                                 ; static down state SPEC.md 13.8.2
                                 ; describes and leave it down.
%define OSAPI_WM_ONTIMER    KERNEL_SEG:0x0440   ; BX = win ptr, AX = a near proc
                                 ; in YOUR segment (0 clears it): what the
                                 ; timer above calls. CX = DX = 0 - a timer has
                                 ; no point - SI = the window, on the UI task
                                 ; under the gfx lock and billed to you, which
                                 ; is W_ONCLICK's environment exactly. Call it
                                 ; AFTER wm_create.
                                 ;
                                 ; out CF=1 = REFUSED and nothing armed:
                                 ; this is a kern_small machine, which
                                 ; carries the slot and not the body. TEST
                                 ; CF AND HAVE A SECOND PATH - there is no
                                 ; tracking and no timer there, so draw the
                                 ; static down state SPEC.md 13.8.2
                                 ; describes and leave it down.
%define OSAPI_VOL_SYS       KERNEL_SEG:0x0420   ; out BL = the volume this
                                 ; machine BOOTED from - A: on a floppy
                                 ; machine, the installed partition on one
                                 ; that boots from its hard disk. No disk I/O,
                                 ; every other register preserved, and it
                                 ; cannot fail.
                                 ;
                                 ; It is the volume half of an
                                 ; OSAPI_FILE_GOTO, and it exists because
                                 ; there was no other way to name the system
                                 ; disk: your instance stands in the folder it
                                 ; was launched from, or in one a dialog gave
                                 ; it, and nothing told you where the SYSTEM's
                                 ; own files are. FONTS/ is the first of those
                                 ; (SPEC.md 19.7).
                                 ;
                                 ; Bank OSAPI_FILE_HERE first and GOTO back
                                 ; when you are done: a visit leaves your
                                 ; instance standing where you left it, and
                                 ; every unqualified name you pass the file
                                 ; API afterwards resolves THERE.
; **IT WAS 0x0428 AND MOVED HERE IN A MERGE**, which is the one hazard at this
; boundary that nothing in the toolchain catches: the two sides added different
; NAMES at the same address, so this file merged CLEAN with two cells pointing
; at one another and nothing would have said so until a package called one and
; got the other. The check is mechanical and belongs after every merge - sort
; the %defines by address and look for a duplicate (docs/UPSTREAM.md).
%define OSAPI_DRV_CALL_AT   KERNEL_SEG:0x04A0   ; OSAPI_DRV_CALL, EXCEPT ES IS
                                 ; YOURS (SPEC.md 20.11.2). Same class, same
                                 ; verbs, same answers, same routine - the only
                                 ; difference is that the cell below overwrites
                                 ; ES with your DS and this one leaves it
                                 ; exactly as you set it.
                                 ;
                                 ; FOR A BUFFER THAT IS NOT IN YOUR OWN
                                 ; SEGMENT: a heap claim, so an optional buffer
                                 ; can be claimed when the memory is there and
                                 ; done without when it is not, instead of
                                 ; growing your image for it.
                                 ;
                                 ; **YOU OWN THE ES YOU PASS, AND A WRONG ONE
                                 ; IS NOT CAUGHT.** ES on entry to every
                                 ; callback is KERNEL_SEG, so forgetting to set
                                 ; it points a RECEIVING verb at the kernel and
                                 ; nothing refuses. Use OSAPI_DRV_CALL unless
                                 ; you specifically need this; the claim must
                                 ; also be PINNED (the default, SPEC.md 66), or
                                 ; a compaction moves it under the driver.
%define OSAPI_DRV_CALL      KERNEL_SEG:0x0448   ; in BH = a DRVC_* class,
                                 ; BL = a verb THAT DRIVER defines; AX, CX, DX,
                                 ; SI and DI are the driver's to define too.
                                 ; out CF=1 with AX=0 when no driver of that
                                 ; class is loaded and published, or when the
                                 ; one that is publishes no package entry
                                 ; point; otherwise whatever the verb answers
                                 ; in AX, BX, CX, DX and SI. BP, DS and ES come
                                 ; back yours, so a verb cannot answer in
                                 ; those.
                                 ;
                                 ; **DI IS AN ARGUMENT AND NOT AN ANSWER**: it
                                 ; reaches the driver as you set it - a verb
                                 ; taking ES:DI is the ordinary shape for one
                                 ; that fills a buffer - and comes back yours.
                                 ; It did not, once: the dispatcher used DI as
                                 ; scratch and a receiving verb wrote into the
                                 ; caller's segment at an offset nobody chose,
                                 ; which looked like a working transfer with
                                 ; the wrong bytes in it (SPEC.md 20.11).
                                 ;
                                 ; THE KERNEL KNOWS NOTHING ABOUT WHAT IS
                                 ; BEHIND IT, and that is the whole design
                                 ; (SPEC.md 20.11). A sockets ABI in the API
                                 ; table would be a contract the kernel owes
                                 ; for ever and a driver ABI is one the driver
                                 ; owes - so the verbs live in
                                 ; drivers/<name>/<name>.inc beside the driver
                                 ; that answers them, and this cell is the
                                 ; door. os8088 gains no networking code at
                                 ; all.
                                 ;
                                 ; ES IS YOUR SEGMENT ON THE WAY IN, unlike
                                 ; every other driver entry point in the
                                 ; machine - which is what lets a verb read a
                                 ; buffer you hand it as ES:SI or ES:DI.
                                 ;
                                 ; A DRIVER MUST OPT IN. One that publishes no
                                 ; DSV_PKGCALL cannot be reached this way at
                                 ; all: HDD.DRV does not, so no .O88 on the
                                 ; floppy can name DRVC_DISK here and arrive at
                                 ; a raw sector write.
                                 ;
                                 ; WORKER-SAFETY IS THE DRIVER'S TO STATE,
                                 ; per verb (SPEC.md 20.6 rule 7). The cell
                                 ; itself takes no lock, raises no sch_lock and
                                 ; touches no disk, so it is legal from a
                                 ; worker - what you REACH through it may not
                                 ; be, and only the driver's own header can
                                 ; say which of its verbs are.

PKGV_IDENT   equ 0              ; **VERB 0 OF EVERY PACKAGE DOOR IS IDENTIFY**
                                ; (SPEC.md 20.11.1). out AX = a two-character
                                ; signature the driver's own header pins, CF=0.
                                ; ASK IT FIRST AND BELIEVE NOTHING ELSE UNTIL
                                ; IT ANSWERS.
                                ;
                                ; The reason is that BH is a CLASS and a class
                                ; is a publication SLOT, while a verb space
                                ; belongs to a DRIVER: NET.DRV and RAMDISK.DRV
                                ; are both DRVC_FILE, so `open a socket` and
                                ; `upper-case this string` are the same number
                                ; at the same address, and a package that
                                ; guessed would hand a hostname to the RAM disk
                                ; and read the length back as a handle. It
                                ; assembles, it runs, and it says nothing -
                                ; which is the failure this tree keeps naming.
                                ;
                                ; It is one verb and it is worth it: a driver
                                ; that does not answer a signature you know is
                                ; a driver you must not send a second verb to

; The classes OSAPI_DRV_CALL's BH takes. Mirrored from kernel/driver.inc -
; drivers/os88drv.inc has no copy, it includes this file. A class is also a
; PUBLICATION SLOT: one driver of each may be attached at a time - which is
; why PKGV_IDENT above exists: a class does not name a driver.
DRVC_SOUND   equ 1
DRVC_DISK    equ 2              ; block volumes: hard disks (SPEC.md 51.8).
                                ; Publishes no package entry point and is not
                                ; going to - see OSAPI_DRV_CALL above
                                ; 3 was the serial monitor's and is RETIRED,
                                ; not free: a class number is ABI, and a driver
                                ; built against the old header would attach as
                                ; whatever took it (SPEC.md 58)
DRVC_NET     equ 4              ; the parallel link (SPEC.md 62)
DRVC_FILE    equ 5              ; a volume served by somebody answering
                                ; questions about FILES rather than sectors
                                ; (SPEC.md 62.9)
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

%define OSAPI_MEM_MOVABLE   KERNEL_SEG:0x0400   ; DX = a claim YOU hold, AX =
                                 ; a near proc in YOUR segment, or 0 to pin the
                                 ; claim again. out CF=1 = no such claim, or
                                 ; not yours. Preserves every other register.
                                 ;
                                 ; DECLARES THE CLAIM RELOCATABLE. The heap
                                 ; fragments over a long session - a buffer
                                 ; freed between two live ones leaves a hole
                                 ; nothing big will fit in - and the kernel can
                                 ; slide the movable claims down against each
                                 ; other and hand the middle back in one piece
                                 ; (SPEC.md 66). It does that on DEMAND, inside
                                 ; a claim that would otherwise be refused, so
                                 ; the cost lands where a user has just asked
                                 ; for something and is expecting a pause.
                                 ;
                                 ; Your proc is called through your own
                                 ; dispatcher, exactly like W_PAINT - an
                                 ; ordinary near proc with a near `ret`, never
                                 ; a retf - with:
                                 ;
                                 ;     BX = the base segment it WAS at
                                 ;     DX = the base segment it is at NOW
                                 ;     DS = CS = your segment, ES = KERNEL_SEG
                                 ;
                                 ; THE BYTES HAVE ALREADY MOVED. Your job is
                                 ; the pointers, and it is ALL of them, not
                                 ; just the word you keep the base in: any
                                 ; segment you derived from it - a row pointer,
                                 ; a per-sample base, a seg:off pair - is stale
                                 ; the instant this is called and is yours to
                                 ; recompute. That is why this is a callback
                                 ; and not the kernel poking a word for you.
                                 ;
                                 ; Inside it you MUST NOT claim, free, resize,
                                 ; yield, draw, or touch a file slot. It runs
                                 ; with task switching paused, inside the
                                 ; allocator, and the map is mid-walk.
                                 ;
                                 ; THE DEFAULT IS PINNED. A claim you never
                                 ; declare behaves exactly as every claim did
                                 ; before this existed, so adopt it one buffer
                                 ; at a time. The kernel may ALSO pin a claim
                                 ; you declared - a DMA buffer, or anything
                                 ; owned by a package whose worker is running -
                                 ; and will not tell you; a relocation proc
                                 ; that is never called is a normal outcome.

%define OSAPI_TASK_PARK     KERNEL_SEG:0x0410   ; a DRIVER's worker parks here
                                 ; for a heap compaction (SPEC.md 66.5.5), the
                                 ; way a package's parks at OSAPI_TASK_ALIVE.
                                 ; Call it once per outer loop, where you hold
                                 ; no pointer derived from a claim of the
                                 ; driver's. Preserves every register and the
                                 ; flags; one compare when nothing is asking.
                                 ;
                                 ; WITHOUT IT NO CLAIM YOUR DRIVER OWNS CAN
                                 ; EVER MOVE. A driver has no instance record,
                                 ; so its worker carries T_INST = 0xFF and the
                                 ; kernel cannot tell a service task that is
                                 ; running from one that is idle; it therefore
                                 ; refuses to compact any driver-owned claim
                                 ; while any service task is unparked.

%define OSAPI_MEM_PARKSAFE  KERNEL_SEG:0x0408   ; AL = 1 declare / 0 withdraw.
                                 ; out CF=1 = you are not a live package
                                 ; instance. Preserves every register.
                                 ;
                                 ; "THE KERNEL MAY PARK ME WHILE I AM BLOCKED
                                 ; IN IT" (SPEC.md 66.5.4). Without it, a
                                 ; worker of yours parks for a compaction only
                                 ; at OSAPI_TASK_ALIVE - and a worker that
                                 ; DRAWS is usually sitting in OSAPI_GFX_LOCK
                                 ; exactly when a compaction wants it, because
                                 ; the claim that triggered one came from a
                                 ; callback holding that same lock. Undeclared,
                                 ; your movable claims then never move.
                                 ;
                                 ; WHAT YOU ARE ASSERTING is one thing: no
                                 ; register and no stack slot of yours holds a
                                 ; pointer DERIVED from one of your movable
                                 ; claims across a call that can yield. Take
                                 ; the lock before you address the block - not
                                 ; after - and re-read your base after any
                                 ; call that can yield, and it is true by
                                 ; construction. It is the same discipline
                                 ; OSAPI_MEM_MOVABLE already asks for, applied
                                 ; to the moment you are descheduled rather
                                 ; than to the moment you are called back.
                                 ;
                                 ; It is DECLARED and never assumed because
                                 ; what your stack holds is yours to know. The
                                 ; default is undeclared, and the cost of
                                 ; forgetting is a compaction that achieves
                                 ; less - never memory that moved under you.
                                 ;
                                 ; If a future version of your app really does
                                 ; need to carry a derived pointer across a
                                 ; yield, withdraw this (AL = 0) or pin the
                                 ; claim for that stretch with
                                 ; OSAPI_MEM_MOVABLE AX = 0.

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
                                 ; table OSAPI_FONT_CHAR_XPARENT draws from, which is
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
;  6. Your stack is SCH_STACK (384) bytes in LOW_SEG and SS != DS (see the header of
;     this file). No deep recursion, no big stack buffers, and BP-relative
;     operands address SS.
;  7. You may call: the gfx_* and font_* slots, OSAPI_SET_COLOR, OSAPI_GFX_PEN,
;     OSAPI_WM_CONTENT, OSAPI_WM_OBSCURED, OSAPI_WM_CLIP_SET,
;     OSAPI_WM_CLIP_CLEAR, OSAPI_WM_CLIP_TEST, OSAPI_VIDEO, OSAPI_GET_TICKS,
;     OSAPI_MOUSE, OSAPI_SRAND/RAND, OSAPI_SND_CAPS, OSAPI_SND_TONE,
;     OSAPI_TASK_SLEEP, OSAPI_TASK_YIELD, OSAPI_WM_WAKE and
;     OSAPI_TASK_ALIVE. OSAPI_WM_WAKE is not a new permission - its own
;     cell below has always said "any context - ISR-safe and worker-safe,
;     no lock needed" - and it is on this list because a list of what a
;     worker MAY call is read as exhaustive, so a worker-safe slot missing
;     from it reads as forbidden. It is the carrier of every
;     worker-stages/UI-task-commits handshake in the tree (SPEC.md 77,
;     WEAVE-SPEC 6.10.6).
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

WF_NOANIM  equ 0x4000            ; W_FLAGS bit14: this window does NOT zoom
                                 ; open (SPEC.md 11.99.2). Set it with
                                 ; OSAPI_WM_NOANIM, never by hand. Named here
                                 ; because the W_FLAGS comment below stops at
                                 ; bit 3 and the HIGH BYTE is not free: it is
                                 ; SPEC.md 7.2.1's cursor shape with this bit
                                 ; and two more kernel ones living in it, so
                                 ; a package reading the whole word gets a
                                 ; number it did not expect. The kernel masks
                                 ; them off both ways round, which is why
                                 ; OSAPI_WM_CURSOR does not clear this one
WF_SIZABLE equ 4                 ; W_FLAGS bit2: resizable (SPEC.md 11.1)
WF_FULL    equ 8                 ; W_FLAGS bit3: fullscreen (SPEC.md 11.2)
WF_NOSNAP  equ 16                ; W_FLAGS bit4: this window has OPTED OUT of
                                 ; content-origin alignment (SPEC.md 11.94).
                                 ; INVERTED SENSE, because alignment is the
                                 ; DEFAULT for every window - a clear bit
                                 ; means snapped. Change it with OSAPI_WM_SNAP
                                 ; (AL = 0 to opt out), never by hand
WF_KEEPH   equ 128               ; W_FLAGS bit7: this window's HEIGHT is not the
                                 ; kernel's to reduce for the dock's sake
                                 ; (SPEC.md 11.93). Set it with OSAPI_WM_KEEPH,
                                 ; never by hand - the slot re-fits, and the bit
                                 ; on its own would not take effect until the
                                 ; next adapter change
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
MENU_APPMAX equ 5           ; menus the bar can host per app; extras are
                            ; silently dropped by the kernel's layout pass.
                            ; It was 4 until SPEC.md 22.12 gave the Disk
                            ; window an Edit menu - a WIDENING, so a package
                            ; built against 4 is unaffected and nothing here
                            ; renumbered. What still bounds you is the BAR,
                            ; not this: menu_relayout stops at the first
                            ; title that would reach the clock's band, so
                            ; five SHORT titles fit where four long ones may
                            ; not (SPEC.md 12.2)
MENU_MAXW   equ 208         ; widest pull-down, px (frame + 8px padding).
                            ; Mirrored from kernel/menu.inc, and it is here
                            ; only so that MENU_MAXCH below can be DERIVED the
                            ; way the kernel derives it: the two were `24` and
                            ; `(MENU_MAXW - 16) / 8`, agreeing by arithmetic
                            ; accident, and the day the kernel's width moves
                            ; off 208 the kernel's glyph count follows and a
                            ; literal here does not. tests/unit/t_mirror.py
                            ; now compares this number, which is the half a
                            ; name-matching gate can actually check
MENU_MAXCH  equ (MENU_MAXW - 16) / 8    ; glyphs of item text a pull-down
                            ; shows; longer items are TRUNCATED, not wrapped
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
W_ONMOUSEUP equ 26          ; word: near ptr or 0 - the RELEASE half of a
                            ; content click (SPEC.md 13.7). NOT a template
                            ; word - set it with OSAPI_WM_ONMOUSEUP
W_ONDRAG  equ 28            ; word: near ptr or 0 - the pointer MOVED while
                            ; your press was armed (SPEC.md 13.8.2). NOT a
                            ; template word - set it with OSAPI_WM_ONDRAG
W_ONTIMER equ 30            ; word: near ptr or 0 - your one-shot timer's
                            ; handler (SPEC.md 13.9). NOT a template word -
                            ; set it with OSAPI_WM_ONTIMER
W_TIMER   equ 32            ; word: the kernel's deadline, 0 = disarmed. Arm
                            ; it with OSAPI_WM_TIMER; do not write it
MAX_WIN   equ 12

; THE LAST THREE WORDS ARE kern_big's ALONE. The 128KB kernel's record STOPS
; at 28 bytes (SPEC.md 13.8.2), so on that machine offset 28 is the NEXT
; window's W_FLAGS and offset 32 is its W_W - naming any of the three there
; reads, or writes, a window that is not yours. Install them with
; OSAPI_WM_ONDRAG / OSAPI_WM_ONTIMER / OSAPI_WM_TIMER, which answer CF = 1 on
; the kernel that has neither, and never name these offsets yourself. That is
; also why there is no WIN_SIZE here: the stride is 34 on one shipping kernel
; and 28 on the other, and a window INDEX never leaves the kernel anyway.

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
; The header FLAGS byte, by name (SPEC.md 20.2). OS88_F_PARTS is emitted by a
; package that carries parts and is the ONE thing the kernel learns about them
; (SPEC.md 20.12) - apps/os88parts.inc is the rest, and it is package code.
OS88_F_ICON   equ 1             ; an embedded 16x16 icon follows the header
OS88_F_ASSOC  equ 2             ; ...and an association block follows that (54.6)
OS88_F_PARTS  equ 4             ; the FILE is longer than the image, on purpose

; --- OS88_STACK_* : what to pass as OS88_HEADER's 4th argument (SPEC.md 8.7)
; Your worker's stack class. The kernel owns the sizes; you name the one you
; need and it gives you the smallest free slice that is at least that big, or
; refuses with CF=1 - which SPEC.md 20.6 already requires you to degrade on.
;
; SAY NOTHING AND YOU GET THE LARGEST, which is what every package built before
; this field got and still gets. Declaring is how a program that does not need
; 384 bytes stops holding one, so that the machine can run more programs at
; once - it buys you nothing on its own and it is not a micro-optimisation to
; skip: six slices was the whole supply.
;
; tools/stkdepth.py prints your worker's deepest static chain; add the kernel's
; interrupt floor, which docs/STACK-SLOTS-PLAN.md 7.1 puts at 64 on the worst
; real machine measured, and pick the class above the sum with margin.
OS88_STACK_DEFAULT equ 0        ; the largest. What you get by saying nothing
OS88_STACK_128     equ 1        ; a worker that sleeps and wakes: a close
                                ; watcher, a caret blink, a transport poll
OS88_STACK_192     equ 2        ; most workers. The modal class
OS88_STACK_256     equ 3        ; a worker that draws through a deep chain
OS88_STACK_384     equ 4        ; the largest, asked for on purpose: a deep
                                ; interpreter, or anything that talks to
                                ; ETHER.DRV, whose socket verbs cost ~126
                                ; bytes on YOUR stack (SPEC.md 72)

%macro OS88_HEADER 2-4
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
                            ;     offset with BP = the callback it wants,
                            ;     which is why your paint/key/click procs are
                            ;     ordinary near procs ending in `ret` and you
                            ;     never write `retf` (SPEC.md 20.1/20.2)
    %if %0 >= 4
    db %4                   ; +15 the worker's stack class (SPEC.md 8.7) - an
    %else                   ;     OS88_STACK_* above. This byte padded the
    db 0                    ;     dispatcher to a word for three format
    %endif                  ;     versions and was always 0, which is why it
                            ;     could be given a meaning without moving the
                            ;     header version: 0 IS the old behaviour
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
; OS88_PREFER name, vga_w, vga_h, herc_w, herc_h, cga_w, cga_h
;
; The 12-byte table OSAPI_WM_PREFER wants (SPEC.md 11.100.1), laid out so the
; three sizes read as three sizes rather than as six words. Put it anywhere in
; your image that outlives the window, and pass its offset in SI:
;
;       OS88_PREFER my_sizes, 360,300,  718,303,  638,300
;       ...
;       mov si, my_sizes
;       call OSAPI_WM_PREFER
;
; A pair of ZEROS means "nothing to say about that adapter" and leaves your
; template standing there.
;
; YOU DO NOT HAVE TO KNOW HOW TALL AN ADAPTER'S DESKTOP IS. A preference is
; still clamped by the screen exactly as a template is, so the honest way to
; say "as wide as that card, and as tall as it will give me" is a real width
; and a GENEROUS height - 638x300 on a CGA comes back 638x155, and the 300
; never had to be right. What the preference buys you is the WIDTH, which is
; almost never clamped, and the freedom to be a different SHAPE on a screen
; that is not 640x480.
; -----------------------------------------------------------------------------
%macro OS88_PREFER 7
%1:
    dw %2, %3                   ; VID_VGA   640x480  (also VID_EGA 640x350 -
    dw %4, %5                   ; VID_HERC  720x348   the kernel takes this row
    dw %6, %7                   ; VID_CGA   640x200   for an EGA, SPEC.md 39.8)
%endmacro

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
        %error "OS88_MENUSET: at most MENU_APPMAX (5) menus fit in the bar"
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
        %error "package too big: image + bss must fit in APP_MAX_SIZE"
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
                                                ; flags. EVERY ADAPTER: this
                                                ; was a no-op on VGA until
                                                ; mode 12h's 8-pixels-to-a-
                                                ; byte planes were measured
                                                ; and gained MORE than mono
                                                ; (SPEC.md 11.94), so it now
                                                ; costs 8px drag steps
                                                ; everywhere instead of being
                                                ; free on the machine you
                                                ; probably develop on.
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

%define OSAPI_FSX_CAPS      KERNEL_SEG:0x02C0   ; in BX = the window to ask
                                                ; ABOUT (0 = whatever is
                                                ; frontmost); out AX =
                                                ; bitmask (bit n = FSXM id n
                                                ; settable on the display
                                                ; that window is on), DL =
                                                ; that display's VID_* kind.
                                                ; Any context, lock held or
                                                ; not. Grey your mode menu
                                                ; with this (SPEC.md 47) -
                                                ; fsx_mode refuses with the
                                                ; same bit, one predicate for
                                                ; both.
                                                ;
                                                ; PASS YOUR WINDOW, AND ASK
                                                ; AGAIN WHEN THE ANSWER IS
                                                ; USED. On a two-display
                                                ; machine (SPEC.md 39.18.1)
                                                ; this is a question about a
                                                ; DISPLAY, and a window moves
                                                ; between them: an answer
                                                ; banked in your entry proc
                                                ; describes the window you
                                                ; were LAUNCHED FROM, because
                                                ; yours does not exist yet.
                                                ; SPEC.md 39.18.2.

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
                                                ; CF=1: bad id, or not on
                                                ; this adapter.

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
                                                ; Draw, wait, repeat.

%define OSAPI_FSX_PAGE      KERNEL_SEG:0x04E8   ; AL = a page index; out CF=1
                                                ; refused. SHOW that page, and
                                                ; wait for the vertical retrace
                                                ; that latches it - so the
                                                ; moment it returns, the page
                                                ; you named is the one being
                                                ; scanned and any OTHER page is
                                                ; yours to draw on. That is what
                                                ; makes an erase invisible
                                                ; without a second framebuffer,
                                                ; and it is the only supported
                                                ; way to spend FSI_PAGES.
                                                ; Bracket-only, exclusive task
                                                ; only, and only in a GRAPHICS
                                                ; mode that has more than one
                                                ; page - Mode X (3) and
                                                ; Hercules (2). Everything else
                                                ; refuses, including the text
                                                ; modes, whose pages are the
                                                ; BIOS's 4KB convention rather
                                                ; than this table's stride x
                                                ; height. Preserves every
                                                ; register (SPEC.md 53.10)
%define OSAPI_FSX_SURF      KERNEL_SEG:0x03F8   ; THE RECT YOUR BRACKET OWNS
                                                ; (SPEC.md 53.7.1). No inputs;
                                                ; out CF=0 with AX = x, BX =
                                                ; y, CX = w, DX = h, in the
                                                ; coordinates the drawing
                                                ; slots take. CF=1 outside a
                                                ; bracket, registers
                                                ; untouched.
                                                ;
                                                ; ASK IT INSTEAD OF ASSUMING
                                                ; (0,0) AND OSAPI_VIDEO'S
                                                ; SIZE, which is what every
                                                ; same-mode bracket here used
                                                ; to do. A same-mode bracket
                                                ; (SPEC.md 53.7) does not
                                                ; collapse a two-display
                                                ; desktop (39.18.3), so the
                                                ; coordinates are still the
                                                ; whole virtual desktop's and
                                                ; (0,0) is the PRIMARY - your
                                                ; fullscreen then lands on
                                                ; the monitor you are not on
                                                ; and your own goes stale.
                                                ; This answers your display's
                                                ; own origin and extent.
                                                ;
                                                ; It is (0,0,w,h) on every
                                                ; one-display machine and
                                                ; after any OSAPI_FSX_MODE
                                                ; call, so it costs a
                                                ; single-display machine
                                                ; nothing but the call. If
                                                ; you take the SPEC.md 11.2
                                                ; surface as well (Missile
                                                ; Command does) your window
                                                ; record already carries this
                                                ; rect and you need neither.
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
%define OSAPI_WM_BAND       KERNEL_SEG:0x03B8   ; BX = window ptr, AL = edge
                                                ; (0 left, 1 right, 2 top,
                                                ; 3 bottom), CX = the band's
                                                ; extent in pixels measured in
                                                ; from that edge, 0..255;
                                                ; CX = 0 retires that edge.
                                                ; ONE CALL PER EDGE and all
                                                ; four may be named - the part
                                                ; you KEEP is then the content
                                                ; inset by them, which is a
                                                ; rectangle whatever you ask
                                                ; for (SPEC.md 11.96.11.1).
                                                ; "CACHE THESE BANDS OF MY
                                                ; CONTENT AND TELL ME ABOUT
                                                ; THE REST" (SPEC.md
                                                ; 11.96.11): the raise cache
                                                ; then banks the bands alone
                                                ; and OSAPI_WM_DAMAGE hands you
                                                ; the content INSET by them, so
                                                ; furniture you redraw by
                                                ; DRAWING it comes back as a
                                                ; blit. Requires
                                                ; OSAPI_WM_OWNBG - without it
                                                ; the kernel's white fill would
                                                ; wipe the band it just put
                                                ; back - and it is what makes
                                                ; OSAPI_WM_SAVEU affordable on
                                                ; a window whose whole content
                                                ; is too big to bank.
                                                ; CF = 0 taken, CF = 1 REFUSED,
                                                ; and you MUST read it: promise
                                                ; WF_SAVEU after a refusal and
                                                ; you have asked for the
                                                ; whole-content cache you were
                                                ; trying to avoid
; --- the pointer's picture, over YOUR window only (SPEC.md 7.2) -------------
; AL is one of these. They all share the kernel's 8x12 cursor cell, which is
; why a shape costs the machine nothing: every walk in the cursor renderer is
; bounded by that cell, so a bigger picture would be paid on every gfx lock
; hold on the machine rather than only where you asked for it.
OSAPI_CUR_ARROW equ 0           ; the default, and what W_FLAGS arrives holding
OSAPI_CUR_CROSS equ 1           ; the aiming crosshair, hot spot at its centre
%define OSAPI_WM_CURSOR     KERNEL_SEG:0x03D0   ; BX = your window ptr, AL =
                                                ; OSAPI_CUR_*. The pointer wears
                                                ; it over your window's CONTENT
                                                ; and nowhere else - the desktop,
                                                ; other windows, and your own
                                                ; title bar and its boxes stay
                                                ; the arrow, because chrome is
                                                ; the kernel's to draw and to
                                                ; click. Set it once, at window
                                                ; creation; it is remembered in
                                                ; the record and dies with it.
                                                ; It does NOT draw and does not
                                                ; need the lock: the pointer may
                                                ; not be over you, and only the
                                                ; kernel knows - the swap lands
                                                ; on the next UI pass. NOT worth
                                                ; having inside an fsx bracket
                                                ; (SPEC.md 53): the held lock
                                                ; keeps the pointer off the
                                                ; screen there, so an exclusive
                                                ; app still draws its own
%define OSAPI_WM_DAMAGE     KERNEL_SEG:0x03B0   ; BX = your window ptr, called
                                                ; from inside your own W_PAINT.
                                                ; CF=1 = draw the WHOLE content
                                                ; and AX/BX/CX/DX are it; CF=0 =
                                                ; draw AX/BX/CX/DX and no more,
                                                ; absolute and inclusive, and it
                                                ; may be EMPTY (x1 > x2) meaning
                                                ; draw NOTHING. An empty rect is
                                                ; safe to hand any primitive -
                                                ; they read it as nothing to do
                                                ; - so forgetting to test cannot
                                                ; paint anything wrong, only
                                                ; waste the answer. Requires
                                                ; OSAPI_WM_OWNBG: without it the
                                                ; kernel has already whitened
                                                ; your content and the only
                                                ; correct answer is "whole"
                                                ; (SPEC.md 11.90.2)
%define OSAPI_WM_OWNBG      KERNEL_SEG:0x03A8   ; BX = window ptr, AL = 0 clear
                                                ; / non-0 set. "I paint EVERY
                                                ; PIXEL of my content myself",
                                                ; so the kernel's white fill in
                                                ; front of W_PAINT is skipped
                                                ; (SPEC.md 11.90.1). Worth
                                                ; having for the FLASH before
                                                ; the milliseconds: a window
                                                ; whose paint takes seconds is
                                                ; a white hole for that long
                                                ; otherwise. Set it and leave a
                                                ; pixel unwritten and you show
                                                ; whatever was there before -
                                                ; after a move, another
                                                ; window's
OSAPI_SAVEU_ON   equ 1           ; OSAPI_WM_SAVEU's AL, bit 0: the promise
OSAPI_SAVEU_1BPP equ 2           ; ...and bit 1: "and my content is two
                                 ; colours". OR them: AL = 3 is both
%define OSAPI_WM_SAVEU      KERNEL_SEG:0x0378   ; BX = window ptr, AL = 0 clear
                                                 ; / OSAPI_SAVEU_* set. A
                                                 ; PROMISE: this window's
                                                 ; content does not change
                                                 ; while it is not drawing, so
                                                 ; a raise may put its old
                                                 ; pixels back rather than call
                                                 ; W_PAINT (SPEC.md 11.96.1).
                                                 ; Costs nothing if refused;
                                                 ; wrong if the promise is.
                                                 ; OSAPI_SAVEU_1BPP is the
                                                 ; SECOND half of it (SPEC.md
                                                 ; 11.96.17): every pixel of
                                                 ; your content is colour 0 or
                                                 ; colour 15, so the cache
                                                 ; holds ONE plane instead of
                                                 ; four - a quarter of the
                                                 ; memory AND a quarter of the
                                                 ; blit, which is what gets a
                                                 ; big window under the 64KB
                                                 ; ceiling on a VGA. A 50%
                                                 ; dither counts: gfx_fill_gray
                                                 ; is 15 and 0, not a grey.
                                                 ; CDGRAY is NOT two colours,
                                                 ; and SPEC.md 47 rule 1's
                                                 ; disabled pen is CDGRAY on a
                                                 ; colour adapter. RE-STATED
                                                 ; every call: pass 1 after 3
                                                 ; and you have withdrawn it
%define OSAPI_TOAST         KERNEL_SEG:0x0380   ; ES:SI = a NUL line, CX =
                                 ; ticks to live (0 = ~3s at 18.2 Hz). Says it
                                 ; as an inverse-video strip at the right end
                                 ; of the MENU BAR and takes it down on its
                                 ; own (SPEC.md 59) - so it costs your window
                                 ; no pixels, survives a repaint without your
                                 ; help, and cannot leave your incremental
                                 ; drawing disagreeing with your W_PAINT.
                                 ; An EMPTY string retires whatever is up, so
                                 ; taking a message down early is one call and
                                 ; needs no flag of your own. ES:SI rather
                                 ; than your DS, for OSAPI_CLIP_PUT's reason:
                                 ; the text is often in a heap claim. The
                                 ; string is COPIED, so it may be scratch and
                                 ; your app may close while it is still up.
                                 ; Context: ANY - it stages and returns, and
                                 ; the UI task draws. So a worker may call it,
                                 ; and so may a callback holding the gfx lock.
                                 ; CF = 1 refused. Preserves all but the flags
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
; --- SPEC.md 18.9.3: the batch bracket ---------------------------------------
; "The interface is frozen and the disk is the same disk." Between these two,
; a FLOPPY may reuse its banked BPB instead of re-reading LBA 0 at every volume
; switch - which on a copy or an install is one call and one revolution per
; switch, and was 41 of one install's 199 (SPEC.md 18.94.2).
;
; It NESTS, and it does not have to be closed: any gfx_unlock ends it, which is
; every way out of a locked run of kernel code. So a caller cannot leave one
; open across a moment when the user could reach the drive - and equally, a
; caller that unlocks in the middle of its own operation gets no benefit from
; the half after, which is the trade that makes the rule checkable.
%define OSAPI_BATCH_BEGIN   KERNEL_SEG:0x0388   ; no arguments, no answer
%define OSAPI_BATCH_END     KERNEL_SEG:0x0390   ; ...and the other end

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

%define OSAPI_FILE_RMDIR    KERNEL_SEG:0x0498   ; SI = a NUL 8.3 name in your
                                 ; current directory, AL = 0 remove it only if
                                 ; it is EMPTY / non-zero remove it AND
                                 ; EVERYTHING UNDER IT (SPEC.md 18.6). out
                                 ; CF=0 AX=0, else AX = FERR_* (FERR_NOENT no
                                 ; such folder; FERR_PROT covers BOTH "it is
                                 ; not empty and you asked for the strict
                                 ; form" AND a read-only, hidden or system
                                 ; entry - SPEC.md 18.6 gives one code to both
                                 ; and a caller CANNOT tell them apart here;
                                 ; FERR_IO for a first cluster outside
                                 ; [2, the volume's last], which is
                                 ; corruption).
                                 ;
                                 ; **AL = 0 IS THE ONE TO REACH FOR**, and the
                                 ; flag exists because the two callers want
                                 ; opposite things: FTP's RMD is specified to
                                 ; FAIL on a non-empty directory, so a server
                                 ; that quietly emptied one would be destroying
                                 ; data the client believed it was protecting -
                                 ; while an `rm -r` in a shell means the tree
                                 ; and says so. There is no undo on this
                                 ; system, which is why the recursive form is a
                                 ; deliberate second argument rather than the
                                 ; default.
                                 ;
                                 ; The recursive walk is ITERATIVE and uses the
                                 ; disk's own '..' links rather than a stack, so
                                 ; it costs no depth on your 384-byte worker
                                 ; slice - but it is a FILE SLOT, so it is
                                 ; UI-task/window-callback context like every
                                 ; other one, and a deep tree is a long freeze.
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
                                 ;
                                 ; **ON A REDIRECTED VOLUME IT NOW TELLS THE
                                 ; DRIVER**, which it did not and should have
                                 ; (SPEC.md 62.9.1). A DRVC_FILE driver keeps
                                 ; its own idea of where it stands - FSV_STAT
                                 ; resolves a name in the CURRENT folder - so
                                 ; writing the kernel's word behind its back
                                 ; left the two disagreeing and every name you
                                 ; then asked about was looked up in the folder
                                 ; you came FROM, presented as FERR_NOENT for a
                                 ; file that is plainly listed.
                                 ;
                                 ; THE PRICE IS THE CLOBBER SET, and it is only
                                 ; a price on a redirected volume: the call can
                                 ; now reach FSV_CHDIR, and a driver's verb owns
                                 ; every register the FSV ABI does not reserve.
                                 ; BX and DX are still preserved by the kernel
                                 ; and AX is still the answer; **CX, SI and DI
                                 ; are the driver's** on a DVK_FILE volume and
                                 ; unchanged on every other. Bank them if you
                                 ; hold anything across this call - the same
                                 ; rule the file cells have always had.

%define OSAPI_FILE_GOTO_QM  KERNEL_SEG:0x0460   ; DX = a folder's first cluster
                                 ; (0 = the root), BL = the volume. out exactly
                                 ; as OSAPI_FILE_GOTO_Q: CF=0 and AX=0, else
                                 ; AX = FERR_* and nothing moved.
                                 ;
                                 ; GOTO_Q's quiet stand AND the instance MOVES
                                 ; WITH IT (SPEC.md 74.1, 19.2.1): the same
                                 ; word-or-quiet-mount, and then the folder is
                                 ; recorded as where your instance now stands,
                                 ; so the file cell you call next resolves
                                 ; THERE. GOTO_Q alone is undone by that next
                                 ; cell, which first re-stands the machine in
                                 ; your instance's folder - right for a copy
                                 ; loop that wants to come home, wrong for a
                                 ; program whose working folder changes on
                                 ; every call (a CP/M drive is a folder). Same
                                 ; caveat as GOTO_Q: the global listing is
                                 ; left empty and owed, so it is not the one to
                                 ; call before SHOWING a folder. Bank where you
                                 ; were with OSAPI_FILE_HERE first if you mean
                                 ; to come back.
%define OSAPI_REBOOT        KERNEL_SEG:0x0368   ; AL = 0 the ordinary restart /
                                 ; non-0 restart WITHOUT touching a disk. No
                                 ; answer, every register AND THE FLAGS
                                 ; preserved. POSTS a restart (SPEC.md 20.10):
                                 ; the System menu's
                                 ; own Restart, but performed by ui_task at the
                                 ; top of its next pass rather than here. You
                                 ; are holding the gfx lock and that path takes
                                 ; it, so an inline restart would deadlock with
                                 ; the machine already half torn down.
                                 ;
                                 ; So this RETURNS, and your callback should
                                 ; finish normally afterwards - draw what it
                                 ; was going to draw, return to the kernel.
                                 ; The restart happens a moment later, after
                                 ; the Control Panel's settings are flushed and
                                 ; every driver is detached.
                                 ;
                                 ; PASS AL NON-0 IF YOU HAVE JUST ASKED THE
                                 ; USER TO REMOVE A FLOPPY. That flush is a
                                 ; mount of A:, a write and a coherence
                                 ; remount, and against an empty drive it is
                                 ; seconds of frozen UI under a stopped cursor
                                 ; and then a toast telling them to put back
                                 ; the disk you told them to take out. The
                                 ; kernel cannot work that out for itself; you
                                 ; are the one who knows. AL = 0 otherwise -
                                 ; the settings are worth saving, and the
                                 ; Control Panel may be open and unsaved

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

; --- the two cells a PACKAGE may not call (SPEC.md 19.6.1) --------------------
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
;
; ...and its other half. OSAPI_FILE_APPEND refuses a hidden + system file the
; same way every write does, so without this one a system file could be MADE
; by the cell above and never FINISHED: a file larger than the caller's buffer
; is moved as a run of appends (SPEC.md 18.4.4), and the first of those met the
; two bits the create had just stamped. Same fence, same arguments and the same
; precondition as OSAPI_FILE_APPEND - the file's current size must be a whole
; number of clusters. It cannot CREATE anything, an append needing an entry
; that is already there, so SPEC.md 19.6's rule is untouched by it.
%define OSAPI_FILE_APPEND_SYS KERNEL_SEG:0x03A0 ; SI = NUL 8.3 name, ES:BX =
                                 ; bytes, CX = count; out as
                                 ; OSAPI_FILE_APPEND. DRIVERS ONLY.

; --- back to the application slots: window callbacks (SPEC.md 13.7/11.93) -----
%define OSAPI_WM_ONMOUSEUP  KERNEL_SEG:0x01F0   ; BX = win ptr, AX = a near proc
                                 ; in YOUR segment (0 clears it): the RELEASE
                                 ; half of a content click (SPEC.md 13.7).
                                 ; Call it from your entry proc after
                                 ; OSAPI_WM_CREATE, exactly like
                                 ; OSAPI_WM_ONSIZE - W_ONMOUSEUP is not a
                                 ; template word.
                                 ;
                                 ; The kernel calls it with CX = x, DX = y,
                                 ; SI = your window, in W_ONCLICK's environment
                                 ; exactly: the UI task, under the gfx lock,
                                 ; billed to your instance. You may draw.
                                 ;
                                 ; TWO RULES, and both are the point of it:
                                 ;   - it arrives ONLY if your W_ONCLICK ran
                                 ;     for the matching press. A press that
                                 ;     merely raised your window, or landed on
                                 ;     the chrome, or was swallowed by a modal
                                 ;     dialog, produces nothing.
                                 ;   - it arrives EVEN IF the release landed
                                 ;     outside your window. CX/DX are SCREEN
                                 ;     coordinates and may be outside your
                                 ;     content box, or negative once you
                                 ;     subtract your origin. That is what lets
                                 ;     you un-draw a pressed state and decline
                                 ;     to act, which is the whole feature -
                                 ;     range-test it yourself.
                                 ;
                                 ; The kernel guarantees DELIVERY; you decide
                                 ; IDENTITY. It has no idea what your controls
                                 ; are and does not try to guess.
                                 ;
                                 ; Do NOT also poll OSAPI_MOUSE in a tracking
                                 ; loop from the same W_ONCLICK: that loop
                                 ; consumes its own release. Pick one.
%define OSAPI_WM_ONRCLICK   KERNEL_SEG:0x0490   ; BX = win ptr, AX = a near proc
                                 ; in YOUR segment (0 clears it): the RIGHT
                                 ; button, pressed in your content
                                 ; (SPEC.md 13.11). Call it from your entry
                                 ; proc after OSAPI_WM_CREATE, like
                                 ; OSAPI_WM_ONWAKE - it is a side table, not a
                                 ; template word.
                                 ;
                                 ; The kernel calls it with CX = x, DX = y,
                                 ; SI = your window, in W_ONCLICK's environment
                                 ; exactly: the UI task, under the gfx lock,
                                 ; billed to your instance. You may draw.
                                 ;
                                 ; THREE RULES, and they are the whole of it:
                                 ;   - PRESS EDGE ONLY. There is no release
                                 ;     half and no EVT_RUP to build one from
                                 ;     (SPEC.md 9): the right button is a
                                 ;     single-shot verb here, not a gesture.
                                 ;     W_ONMOUSEUP and W_ONDRAG belong to the
                                 ;     LEFT button and are not armed by this.
                                 ;   - FRONTMOST ONLY. A right press on a
                                 ;     background window RAISES it and stops,
                                 ;     exactly as it did before this slot
                                 ;     existed; the press that raises you is
                                 ;     not delivered.
                                 ;   - CONTENT ONLY. The chrome - title bar,
                                 ;     close, minimize and grow boxes - has no
                                 ;     right-button path at all, and the menu
                                 ;     bar is skipped whole. CX/DX are SCREEN
                                 ;     coordinates: subtract your content
                                 ;     origin (OSAPI_WM_CONTENT) as you do in
                                 ;     W_ONCLICK.
                                 ;
                                 ; Taking it means taking it WHOLE: a window
                                 ; with a handler installed never sees the
                                 ; kernel's own right-button behaviour, and a
                                 ; window without one is unchanged in every
                                 ; way. Nothing here is a context menu - there
                                 ; is no API slot through which a package can
                                 ; supply popup items (SPEC.md 12.4) - so what
                                 ; you do with the press is your own drawing.
%define OSAPI_WM_ONRESIZE   KERNEL_SEG:0x03D8   ; BX = win ptr, AX = a near proc
                                 ; in YOUR segment (0 clears it): "your content
                                 ; box CHANGED, and you did not ask"
                                 ; (SPEC.md 11.98). Install it from your entry
                                 ; proc after OSAPI_WM_CREATE, like
                                 ; OSAPI_WM_ONSIZE - it is not a template word.
                                 ;
                                 ; THE OTHER HALF OF OSAPI_WM_ONSIZE, and they
                                 ; are not interchangeable. That one is asked
                                 ; BEFORE a resize and takes an answer; this one
                                 ; is told AFTER, and there is nothing to
                                 ; answer. It covers the case the negotiator
                                 ; cannot see at all: the user changed ADAPTER
                                 ; under you (SPEC.md 39.11.2 - a 640x480 VGA
                                 ; becomes a 640x200 CGA and every window on the
                                 ; screen is re-fitted at once), or your window
                                 ; was dragged across the seam onto a shorter
                                 ; display (39.16.3). Nobody proposed anything;
                                 ; the box simply moved.
                                 ;
                                 ; Called SI = your window, CX = the NEW content
                                 ; width, DX = the NEW content height, on the UI
                                 ; task with the gfx lock HELD - and you MUST
                                 ; NOT DRAW. A full repaint follows immediately,
                                 ; so this is your chance to be laid out
                                 ; correctly before your W_PAINT runs rather
                                 ; than a frame later.
                                 ;
                                 ; WHO NEEDS IT: any app that works out a layout
                                 ; ONCE, from the geometry, and keeps the answer
                                 ; - rows that fit, columns, a cell size. Ask
                                 ; OSAPI_WM_GEOM in W_PAINT instead and you need
                                 ; none of this. An app that draws through the
                                 ; bottom of its own frame after an adapter
                                 ; change is the symptom this exists for.
%define OSAPI_WM_ONWAKE     KERNEL_SEG:0x0458   ; BX = win ptr, AX = a near proc
                                 ; in YOUR segment (0 clears it): your WAKE
                                 ; handler (SPEC.md 74.1). Install it from your
                                 ; entry proc after OSAPI_WM_CREATE, like
                                 ; OSAPI_WM_ONRESIZE - a side table, not a
                                 ; template word, cleared when the window dies.
                                 ;
                                 ; Called SI = your window, on the UI task,
                                 ; billed to your instance, WITHOUT THE GFX
                                 ; LOCK - the one callback that is not under
                                 ; it. So it may call the file slots (it is
                                 ; the UI task, which is their contract), and
                                 ; it may take the lock itself with
                                 ; OSAPI_GFX_LOCK for a burst it can state and
                                 ; release it - and it may NOT draw before it
                                 ; has. It is a KICK, not a message: nothing
                                 ; is delivered with it, one arrives per
                                 ; OSAPI_WM_WAKE below (or fewer - see there),
                                 ; and a stale one after a slot was reused is
                                 ; possible, so a handler must be indifferent
                                 ; to being called with nothing to do.
                                 ;
                                 ; WHAT IT IS FOR: a long computation with I/O
                                 ; that has to live on the UI task because
                                 ; file slots do - RunCPM's Z80 runs in slices
                                 ; from here (SPEC.md 71), each slice re-posting
                                 ; the next, and a program waiting on a key
                                 ; costs nothing until W_ONKEY kicks.
                                 ;
                                 ; **AND IT IS THE LAUNCH DOCUMENT'S HANDOVER**
                                 ; (SPEC.md 54.10). If your package was started
                                 ; by a double-click on a DOCUMENT, the kernel
                                 ; calls this handler ONCE, itself, after your
                                 ; window is on the glass - so the shape of an
                                 ; associated app is: read OSAPI_ARG_FILE in
                                 ; your entry proc and BANK it (the slot is
                                 ; read-and-clear and is disarmed the moment
                                 ; the load returns), then open it from here,
                                 ; where there is something for the user to
                                 ; look at while the floppy turns. Take
                                 ; OSAPI_GFX_LOCK for that burst: everything
                                 ; that draws needs it, and OSAPI_TOAST only
                                 ; reaches the glass BEFORE a long operation
                                 ; for a caller that holds it (SPEC.md 59.4).
                                 ; A package that installs no handler is
                                 ; unaffected - the kernel calls nothing.
                                 ;
                                 ; This slot PRESERVES THE FLAGS, which matters
                                 ; because you install it from your entry proc:
                                 ; the CF you owe the loader is still riding in
                                 ; them there (SPEC.md 54.10.2).
%define OSAPI_WM_WAKE       KERNEL_SEG:0x0450   ; BX = win ptr (a window of
                                 ; YOURS). Post an EVT_WAKE for it (SPEC.md
                                 ; 10, 74.1): the UI task pops it in order with
                                 ; the mouse events and calls the handler
                                 ; above. Any context - ISR-safe and worker-
                                 ; safe, no lock needed. out CF=0 a wake is
                                 ; queued for that window - posted now, or one
                                 ; was ALREADY waiting, which is the same
                                 ; promise: the kernel keeps at most one queued
                                 ; wake per window, so kicking from every
                                 ; callback (paint, key, click, the handler
                                 ; itself) is free and can never fill the
                                 ; 16-record ring. CF=1 the ring was full of
                                 ; other events and nothing was posted - kick
                                 ; again from your next callback. Every
                                 ; register preserved.
; --- CLOSING: the negotiation, and the alert that usually follows ------------
; SPEC.md 75. Every close in this system - the close box, the app-name menu's
; Close, the dock tile's context menu - used to be final the instant the user
; asked for it, so an application with unsaved work had no moment at which to
; say anything about it. These three are that moment.
%define OSAPI_WM_ONCLOSE    KERNEL_SEG:0x0468   ; BX = win ptr, AX = a near proc
                                 ; in YOUR segment (0 clears it): your CLOSE
                                 ; NEGOTIATOR (SPEC.md 75.1). Install it from
                                 ; your entry proc after OSAPI_WM_CREATE, like
                                 ; OSAPI_WM_ONWAKE - a side table, not a
                                 ; template word, cleared when the window dies.
                                 ;
                                 ; Called SI = your window, on the UI task,
                                 ; GFX LOCK HELD - W_ONCLICK's environment
                                 ; exactly, so it may draw, may call the file
                                 ; slots, must not take the lock, and must not
                                 ; take a long time about it. It is called for
                                 ; EVERY way of closing you, which is the
                                 ; point: there is one door and this is in
                                 ; front of it.
                                 ;
                                 ; ANSWER IN CF. CF = 0 lets the close happen
                                 ; and is what a window with no negotiator
                                 ; says. CF = 1 REFUSES it: nothing at all
                                 ; happens - your window is not even hidden -
                                 ; and YOU NOW OWE THE USER A WAY OUT. Put up
                                 ; an alert (os88ui.inc's os88ui_ask) and close
                                 ; yourself with OSAPI_WM_CLOSE when it is
                                 ; answered. A refusal with nothing behind it
                                 ; is a window that cannot be closed, and real
                                 ; mode has no way to take that back.
                                 ;
                                 ; A negotiator is never asked about itself
                                 ; while it is running, so raising a dialog
                                 ; from inside one cannot recurse.
%define OSAPI_WM_CLOSE      KERNEL_SEG:0x0470   ; BX = a window of YOURS: close
                                 ; it (SPEC.md 75.2). This is how a negotiator
                                 ; that refused finishes the job once the user
                                 ; has answered.
                                 ;
                                 ; IT RETURNS, and your window is gone a moment
                                 ; later - the close is DEFERRED to the next UI
                                 ; pass, because you are standing in the very
                                 ; segment it is about to free and the far
                                 ; return would run your dispatcher out of
                                 ; memory the heap has already handed back. So
                                 ; call it and RETURN: do not draw afterwards,
                                 ; do not assume anything about when it lands,
                                 ; and do not call it twice.
                                 ;
                                 ; Your negotiator is NOT asked again - this is
                                 ; the answer to the question it asked. Every
                                 ; register preserved; the gfx lock may be held
                                 ; or not.
; THE ALERT ITSELF IS NOT A SLOT (SPEC.md 75.3): a windowed dialog with
; buttons has a floor of about 800 bytes wherever it lives, and it does not
; belong in a kernel budget the whole machine pays for. It is SOURCE, in
; `apps/os88ui.inc` behind `%define OS88UI_ALERT` - `os88ui_ask`, with its
; own window, its own buttons and its own completion, costing your image 607
; bytes and every package that does not ask for it nothing. See that file's
; header.
%define OSAPI_WM_PREFER     KERNEL_SEG:0x0478   ; BX = win ptr, SI = the offset
                                 ; IN YOUR OWN SEGMENT of a 12-byte table: three
                                 ; (w, h) FRAME sizes, in VID_VGA / VID_HERC /
                                 ; VID_CGA order - which is 0, 1, 2, so the
                                 ; adapter indexes it directly. SI = 0 withdraws
                                 ; it. "This is how big I want to be on each of
                                 ; the three screens" (SPEC.md 11.100.1).
                                 ;
                                 ; A (0, 0) pair means NOTHING TO SAY about that
                                 ; adapter - your template stands there - so a
                                 ; window that only cares about the CGA declares
                                 ; two zero pairs and one real one.
                                 ;
                                 ; It registers AND APPLIES, so calling it from
                                 ; your entry proc right after OSAPI_WM_CREATE is
                                 ; how you open at the right size; and it
                                 ; PRESERVES THE FLAGS, so the CF you owe the
                                 ; loader survives it. Use OS88_PREFER (below) to
                                 ; lay the table out rather than counting words.
                                 ;
                                 ; THE TABLE STAYS IN YOUR IMAGE and the kernel
                                 ; keeps only the offset, so it must live
                                 ; somewhere that outlives the window - .text or
                                 ; your bss, never the stack. It may be REWRITTEN
                                 ; whenever you like: an app whose size is
                                 ; genuinely computed fills all three entries at
                                 ; entry and then declares it, which is what
                                 ; apps/solitaire and apps/paint do.
                                 ;
                                 ; The kernel applies it at three moments: here,
                                 ; on an adapter change (SPEC.md 39.11.2), and -
                                 ; unless your window is WF_SIZABLE - when it is
                                 ; dragged wholly onto a display of a different
                                 ; kind. You are told through OSAPI_WM_ONRESIZE
                                 ; each time, and the box you get is not always
                                 ; the one you asked for: the display clamps it
                                 ; down and OSAPI_WM_MINSIZE lifts it back, so
                                 ; read OSAPI_WM_GEOM rather than assuming.

%define OSAPI_WM_MINSIZE    KERNEL_SEG:0x0480   ; BX = win ptr, CX = minimum
                                 ; outer WIDTH, DX = minimum outer HEIGHT;
                                 ; CX = DX = 0 withdraws. "Below this my layout
                                 ; stops working - do not cut me past it, even
                                 ; to fit a screen" (SPEC.md 11.100.2). Registers
                                 ; AND applies, FLAGS PRESERVED, same as
                                 ; OSAPI_WM_PREFER above.
                                 ;
                                 ; Every path that REDUCES a size honours it -
                                 ; the create-time fit, an adapter change, a
                                 ; drag onto a smaller display, the grow box and
                                 ; OSAPI_WM_RESIZE. Without one you get the
                                 ; system floor, WMIN_W x WMIN_H.
                                 ;
                                 ; IT CANNOT COST YOU YOUR TITLE BAR. The
                                 ; minimum is itself capped at the display's own
                                 ; extent, so asking for more rows than the
                                 ; adapter has gets you the adapter; what it CAN
                                 ; do is leave you hanging over the dock, which
                                 ; is OSAPI_WM_KEEPH's behaviour said as a
                                 ; number, and content below the last row a
                                 ; display has is drawn nowhere and takes no
                                 ; clicks. Ask for what your layout needs, not
                                 ; for what you would like.

%define OSAPI_WM_DISPLAY    KERNEL_SEG:0x0488   ; OSAPI_VIDEO FOR THE DISPLAY THIS
                                 ; WINDOW IS ON (SPEC.md 39.16.4). BX = your
                                 ; window. Out: AX = width, BX = height, CX =
                                 ; the first row the dock owns, SI = the first
                                 ; row the desktop band has, DL = 0 VGA / 1
                                 ; Hercules / 2 CGA, DH = bits per pixel. DI
                                 ; preserved; no lock needed, any context.
                                 ;
                                 ; **OSAPI_VIDEO ANSWERS ABOUT THE PRIMARY**
                                 ; (SPEC.md 39.2.1) and is right to - but on a
                                 ; machine with two cards your window can be
                                 ; dragged onto the other one, and then every
                                 ; fact you took from OSAPI_VIDEO is about a
                                 ; screen you are not on. If you register an
                                 ; OSAPI_WM_ONRESIZE handler to re-derive your
                                 ; layout, ask THIS inside it: that handler
                                 ; fires on a drag across the seam as well as
                                 ; on an adapter switch, and only this cell can
                                 ; tell you where you ended up.
                                 ;
                                 ; SI IS NOT ALWAYS MBAR_H. Only the primary
                                 ; has a menu bar and a dock; a secondary's
                                 ; whole height is desktop, so its band is rows
                                 ; 0..CX-1. The band is CX - SI on either, and
                                 ; that arithmetic is right on every machine.
                                 ;
                                 ; Identical to OSAPI_VIDEO on a one-display
                                 ; machine, so there is nothing to test first.
;
%define OSAPI_WM_KEEPH      KERNEL_SEG:0x03E0   ; BX = win ptr, AL = 0 clear /
                                 ; non-0 set: "my layout is FIXED - if I will
                                 ; not fit the desktop band, hang me over the
                                 ; DOCK rather than shorten me" (SPEC.md 11.93).
                                 ; Call it from your entry proc right after
                                 ; OSAPI_WM_CREATE; it re-fits from the size you
                                 ; asked for, and preserves the FLAGS, so the CF
                                 ; you owe the loader survives it.
                                 ;
                                 ; WHY IT IS OPT-IN. A template is authored for
                                 ; 640x480 and a CGA's desktop band is 155 rows,
                                 ; so a taller window is clamped to fit above the
                                 ; dock. For anything that reflows - a list, a
                                 ; text view, an app that re-derives through
                                 ; OSAPI_WM_ONRESIZE - that is exactly right.
                                 ; For a FIXED layout it is not a clamp but a
                                 ; LOSS: you go on drawing all of your content,
                                 ; so the rows below your frame are still on the
                                 ; glass and simply belong to no window - the
                                 ; kernel hit-tests the record, so a click there
                                 ; reaches the dock instead of you. Visible and
                                 ; unplayable is the symptom.
                                 ;
                                 ; The kernel cannot tell the two apart, which
                                 ; is why you say so. You still do not get more
                                 ; than the SCREEN: the floor becomes the
                                 ; display's own bottom, not infinity, so a
                                 ; template taller than the whole adapter is
                                 ; still cut - and on a machine where you fit
                                 ; the band, this changes nothing at all.
%define OSAPI_BOOT_TICKS    KERNEL_SEG:0x02F8   ; out AX = how long this machine
                                 ; took to boot, in SYSTEM TICKS (18.2065 Hz,
                                 ; 54.925 ms each): the boot sector's first
                                 ; instruction to the first desktop frame
                                 ; being on the glass. It spans the kernel
                                 ; read, the video probe, the mount and every
                                 ; driver SYSTEM.CFG asked for, which is most
                                 ; of what a boot IS on a floppy machine -
                                 ; ~24 ms per 512 bytes delivered, and 199 ms per
                                 ; int 13h CALL for one sector (384 for a whole
                                 ; track) - the CALL is the unit that matters
                                 ; (PERFORMANCE.md Part 2, Set 24).
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
