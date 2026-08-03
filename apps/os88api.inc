; =============================================================================
; os8088 - apps/os88api.inc
;
; The program-side SDK for .o88 v3 packages (SPEC.md 20.5). Included by
; package sources only - never by the kernel. Provides the pinned os8088 API
; far-call constants (SPEC.md 20.3), the window record and wm_create template
; layouts (SPEC.md 11), the 16 EGA colour indices, and the packaging macros,
; so a package source can be just:
;
;   %include "os88api.inc"
;   OS88_HEADER 'NAME', entry        ; emits cpu/bits/org 0 + the 32-byte v3
;                                   ; header (optional 3rd arg = flags byte,
;                                   ; SPEC.md 20.2: bit 0 = embedded icon
;                                   ; follows; optional 4th arg = minimum
;                                   ; conventional RAM in KB, default 512)
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
; THE v3 MODEL (SPEC.md 20.1) - a package runs in ITS OWN SEGMENT. The
; binary is assembled at org 0 and loaded, unmodified, onto a paragraph
; boundary in a segment of its own, allocated from the conventional-memory
; arena (SPEC.md 2.5). At entry and in every callback CS = DS = ES = that
; segment; inside it, the near model applies exactly as it always did -
; near calls between your own routines, plain `dw label` tables, org-0
; offsets everywhere. There is NO relocation: your labels are correct
; wherever the segment lands, and every instance of your package carries
; identical offsets. The old v2 author rule ("package addresses only as
; whole 16-bit words") is gone - your offsets are plain numbers now.
;
; SS is LOW_SEG (0x0800), NOT your segment - every task stack lives in low
; memory, so SS != DS and `[bp+disp]` addresses SS, not your data. A
; pointer held in BP needs an explicit `ds:` override. That has always
; been true inside a callback (you run on the UI task's stack) and applies
; equally to your worker task (OSAPI_TASK_SPAWN below), whose 1536-byte
; stack is likewise in LOW_SEG.
;
; THE RETF RULE (binding, SPEC.md 20.5/20.7 rule 5): every routine the
; kernel calls in your package returns with `retf`, never `ret` - the
; entry proc, W_PAINT, W_ONKEY, W_ONCLICK, the AM_ONCMD menu handler and
; the file-dialog completion proc, all of them, because the kernel
; far-called you at your segment. A near `ret` there pops only IP and
; resumes at a garbage offset inside your own segment with the kernel's
; return address still on the stack. The one exception is the worker
; entry, which never returns at all (rule 2 below). Register contracts
; are otherwise unchanged from v2, and CF answers (the entry proc's
; abort, for one) survive the far mechanics - nothing on the return path
; touches flags.
;
; POINTERS ACROSS THE BOUNDARY (SPEC.md 20.3). Your DS is your own
; segment, so a DS-relative pointer argument names YOUR memory - and the
; kernel stages it across the boundary for you at the API slot. You pass
; the same registers as ever; these are the rules you feel:
;
;   - font_str / font_width SI strings: staged, truncated past 63 chars.
;   - wm_create's SI template: staged whole; its title/paint/onkey/onclick
;     words are offsets in YOUR segment (the kernel pairs them with it).
;   - the file slots' SI/DI names: staged, capped at 13 chars so an
;     over-long name is REFUSED (FERR_NAME), never silently truncated
;     into some other legal name.
;   - fdlg_open's SI default name: staged; DI is a completion OFFSET in
;     your segment, stored as a value.
;   - your menu set (OSAPI_MENU_SET): the offset is stored; the kernel
;     copies the whole set out of your segment when the bar is laid out.
;   - snd_fm verb 2's 11-byte patch, snd_stream verb 6's source and verb
;     5's destination: staged/copied through your segment.
;
;   ES-relative buffers (dskw data ES:BX, snd_play ES:SI, xmem_copy
;   ES:SI) change NOT AT ALL: set ES = DS exactly as before - that now
;   names your own segment, which is precisely what the kernel reads.
;
; WINDOW POINTERS ARE OPAQUE HANDLES (SPEC.md 11). The BX wm_create
; returns and the SI a callback receives are kernel pointers your DS
; cannot reach: NEVER dereference them. The v2 idioms `mov ax,[bx+W_W]`
; and `test word [bx+W_FLAGS], 2` are dead - ask OSAPI_WM_GEOM (below)
; for live content geometry and visibility, OSAPI_WM_CONTENT for the
; content origin.
; =============================================================================

; --- environment ---------------------------------------------------------------
KERNEL_SEG   equ 0x1000     ; the kernel's segment: the far API table lives
                            ; at KERNEL_SEG:0x0010 (SPEC.md 20.3). Your own
                            ; CS = DS = ES is YOUR segment, assigned at load
PKG_MAX_PARA equ 0x0FFF     ; region cap in paragraphs (SPEC.md 2.5): image
                            ; + bss must fit PKG_MAX_PARA*16 = 65,520 bytes
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
CLGRAY    equ 7
CDGRAY    equ 8
CLBLUE    equ 9
CLGREEN   equ 10
CLCYAN    equ 11
CLRED     equ 12
CLMAGENTA equ 13
CYELLOW   equ 14
CWHITE    equ 15

; --- the os8088 API table (SPEC.md 20.3, pinned offsets) ---------------------
; Each OSAPI_* below is a far seg:off immediate: `call OSAPI_*` assembles as
; a 5-byte ptr16:16 far call (9A off seg) into the kernel's 8-byte slot,
; which switches DS to KERNEL_SEG around the body and returns with retf -
; flags untouched either way, so CF-returning slots keep their contracts.
; CONSEQUENCE: a slot constant is no longer a number. It can appear ONLY as
; a `call` (or `jmp`) target - never in a `dw`, never in an expression.
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
%define OSAPI_FONT_STR      KERNEL_SEG:0x0068   ; CX=x, DX=y, SI=NUL string (yours;
                                                ; staged - truncated past 63 chars)
%define OSAPI_FONT_WIDTH    KERNEL_SEG:0x0070   ; SI=NUL string; out AX = pixel width
%define OSAPI_WM_CREATE     KERNEL_SEG:0x0078   ; SI=template (yours; staged); out
                                                ; BX = win ptr (OPAQUE), CF on full
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

; --- sound (SPEC.md 20.3/34): the whole surface exists from Phase 1; the
; --- unbuilt tiers return their documented error paths until they land.
%define OSAPI_SND_CAPS      KERNEL_SEG:0x00E0   ; out AX = merged caps word (SND_CAP_*),
                                                ; BL = tone route (0 = speaker, 1 =
                                                ; OPL2), DX = present-driver bits
%define OSAPI_SND_TONE      KERNEL_SEG:0x00E8   ; AX = freq Hz (0 = off), CX = duration
                                                ; ticks (0 = until off), DL = priority
                                                ; (default 0x40); out CF=1 refused,
                                                ; else AL = owner generation
%define OSAPI_SND_PLAY      KERNEL_SEG:0x00F0   ; PCM_EXCL clip (SPEC.md 34.4): ES:SI,
                                                ; CX, DX = rate; out AX = 0 ok /
                                                ; 1..5 err. ES = DS plays your own
                                                ; buffer - unchanged in v3
%define OSAPI_SND_FM        KERNEL_SEG:0x00F8   ; FM verbs (SPEC.md 34.2); verb 2's
                                                ; SI patch is yours - staged; out
                                                ; CF=1 no FM sink / refused
; NEVER call OSAPI_SND_STREAM from your worker task - any verb, staging
; included (SPEC.md 20.3/34.5). The open verbs' busy/half-duplex refusals
; rest on every caller being serialized on the UI task, and teardown is not
; fenced against a task. Poll status from a callback instead.
%define OSAPI_SND_STREAM    KERNEL_SEG:0x0100   ; PCM_BG streams + staging (SPEC.md
                                                ; 20.3): AL = verb (0 open / 1 feed /
                                                ; 2 close / 3 status / 5 read /
                                                ; 6 stage / 7 grant), AH = handle or
                                                ; sub-op; out AX = 0 ok / err /
                                                ; SND_ST_* state (verb 3). Verb 6's
                                                ; SI source and verb 5's DI dest are
                                                ; in YOUR segment - copied for you

; capability bits returned by OSAPI_SND_CAPS (SPEC.md 34.2)
SND_CAP_TONE     equ 01h
SND_CAP_FM       equ 02h
SND_CAP_PCM_BG   equ 04h
SND_CAP_PCM_EXCL equ 08h
SND_CAP_PCM_IN   equ 10h

; stream states returned by OSAPI_SND_STREAM verb 3 (SPEC.md 20.3/34.5)
SND_ST_PLAYING   equ 0
SND_ST_UNDER     equ 1           ; underrun-paused: resumable by a feed
SND_ST_ENDED     equ 2           ; stopped by the stream watchdog
SND_ST_STALE     equ 0FFFFh      ; the handle's stream is gone

; --- window management (SPEC.md 11.1/11.2) -------------------------------------
%define OSAPI_WM_SIZABLE    KERNEL_SEG:0x0108   ; BX=win ptr, AL = 0 clear / non-zero
                                                ; set WF_SIZABLE; call from the entry
                                                ; proc after OSAPI_WM_CREATE. A
                                                ; resizable window's paint/click procs
                                                ; MUST lay out from the live geometry
                                                ; OSAPI_WM_GEOM answers, every call.
%define OSAPI_FULLSCREEN    KERNEL_SEG:0x0110   ; AL = 1 enter (BX=win ptr) / 0 exit;
                                                ; caller holds the gfx lock (window
                                                ; callbacks do); out CF=1 enter refused
                                                ; (screen already owned). Exiting is
                                                ; the app's job (Esc in its onkey is
                                                ; the convention) - the menu bar is
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
; The SI/DI names are in YOUR segment and are staged by the kernel; a name
; longer than 12 chars is refused (FERR_NAME), never truncated. The data
; buffer is ES:BX (like OSAPI_SND_PLAY), so staged sound or your own bss
; both work; set ES = DS for the latter. A write stalls painters for its
; duration: never call these from a paint proc.
%define OSAPI_FILE_WRITE    KERNEL_SEG:0x0120   ; SI = NUL 8.3 name, ES:BX = bytes,
                                                ; CX = count (0 = empty file); creates
                                                ; or REPLACES. out CF=0 AX=0, else
                                                ; AX = FERR_*
%define OSAPI_FILE_READ     KERNEL_SEG:0x0128   ; SI = name, ES:BX = buffer, CX =
                                                ; capacity; out CF=0 AX = bytes read,
                                                ; else AX = FERR_* (FERR_BIG leaves
                                                ; the buffer alone)
%define OSAPI_FILE_DELETE   KERNEL_SEG:0x0130   ; SI = name; out CF=0 AX=0, else FERR_*
%define OSAPI_FILE_RENAME   KERNEL_SEG:0x0138   ; SI = old name, DI = new name; out
                                                ; as above
%define OSAPI_FILE_DFREE    KERNEL_SEG:0x0140   ; out CF=0, DX:AX = free bytes, BX =
                                                ; SECTORS per cluster; no disk I/O

; --- menus (SPEC.md 12.2) ------------------------------------------------------
; Every application owns the menu bar while its window is frontmost: the
; System (chip) menu stays put, the app's NAME is drawn to its right, and
; the app's own menus follow. Register the set from the entry proc, with
; BX = the window OSAPI_WM_CREATE just returned - the loader shows the
; window afterwards, so the first bar drawn is already yours.
%define OSAPI_MENU_SET      KERNEL_SEG:0x0148   ; BX = win ptr, SI = menu set offset
                                                ; in YOUR segment (0 = none). Draws
                                                ; nothing, takes no lock, preserves
                                                ; every register AND the flags - so
                                                ; it can sit between OSAPI_WM_CREATE
                                                ; and the entry proc's retf without
                                                ; eating the CF that retf owes the
                                                ; loader. The kernel copies your whole
                                                ; set out of your segment when the bar
                                                ; is laid out, clamped to the caps
                                                ; below (MENU_APPMAX / MENU_ITEMMAX /
                                                ; MENU_STRMAX)

; --- the Standard File dialog (SPEC.md 38) -------------------------------------
; The other half of the file slots above: they operate on a name, and this is
; how you get one from the user. The kernel puts up a modal window listing the
; mounted disk, and calls you back when a name has been chosen.
;
; It does NOT block. A window callback runs under the gfx lock on the UI task,
; so a dialog that pumped events inside your `retf` would have to release a
; lock it does not own. Instead this returns immediately - with the dialog
; already on screen, because you were holding the lock it needed - and your
; completion proc is called much later. Cancel calls nothing at all.
;
; Call it from a menu command or a key handler, never from a paint proc.
%define OSAPI_FILE_DLG      KERNEL_SEG:0x0150   ; AL = 0 Open / 1 Save, BX = your
                                                ; window ptr, DI = completion proc
                                                ; OFFSET (in your segment), SI =
                                                ; default name (NUL, <= 12, yours -
                                                ; staged) or 0. out CF=0 accepted,
                                                ; CF=1 refused (one is already up,
                                                ; or no room)
FDLG_OPEN  equ 0                 ; the two modes, in AL going in and in AL
FDLG_SAVE  equ 1                 ; coming back
;
%define OSAPI_VIDEO         KERNEL_SEG:0x0158   ; the screen this machine actually has
                                                ; (SPEC.md 39.2). No inputs. Out:
                                                ; AX = width, BX = height, CX = the
                                                ; first row the dock owns (so the
                                                ; usable desktop is rows MBAR_H ..
                                                ; CX-1), DL = 0 VGA / 1 Hercules /
                                                ; 2 CGA, DH = bits per pixel, 4 or 1.
                                                ; Callable from any context, lock
                                                ; held or not.
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
; Your completion proc is FAR-called on the UI task with the gfx lock HELD,
; after the dialog window is gone, with CS = DS = your segment and the
; chosen name delivered through ES (SPEC.md 38.6):
;
;     in   AL = the mode it ran in, SI = your window ptr, ES:DI = the chosen
;          name (NUL, <= 12 chars; ES = KERNEL_SEG - the buffer is the
;          kernel's, so read it through ES, never through DS). No register
;          need be preserved. Return with `retf`.
;
; Same rules as a menu command (SPEC.md 12.2): you may draw, you may call the
; file slots above, you must NOT take the lock, and you MUST repaint your own
; content - the kernel does not repaint for you. The name buffer belongs to
; the kernel and is valid for the duration of the call only: copy it (through
; ES) if you need it afterwards.
;
; The dialog navigates the volume, so by the time you are called the current
; directory IS the folder the user chose in, which is where the file slots
; resolve names (SPEC.md 19.2). Just pass the name straight through.

; --- your worker task (SPEC.md 20.6) -------------------------------------------
; Your package may own EXACTLY ONE background task, so the work you do
; between clicks is pre-empted instead of crammed into a callback. This is
; the only place package code runs off the UI task.
;
%define OSAPI_TASK_SPAWN    KERNEL_SEG:0x0160   ; AX = your worker's entry OFFSET (a
                                                ; plain `mov ax, my_worker` - an org-0
                                                ; offset in your own segment),
                                                ; BX = YOUR window ptr (what
                                                ; OSAPI_WM_CREATE gave you). Call it
                                                ; from a window callback, a menu
                                                ; handler or a file-dialog completion
                                                ; proc, i.e. with the gfx lock HELD -
                                                ; that is required, not merely
                                                ; allowed. BX must be YOUR OWN window
                                                ; and AX an offset inside YOUR OWN
                                                ; region: the kernel checks the entry
                                                ; against the region size of the
                                                ; instance BX belongs to, and refuses
                                                ; otherwise, so a stranger's window
                                                ; ptr buys nothing.
                                                ; out CF=1 refused and nothing was
                                                ; created (you already have a worker,
                                                ; or the 12-slot task table is full -
                                                ; a normal outcome: degrade, do not
                                                ; abort, and DO retry later, it is
                                                ; transient); else CF=0 and AL = the
                                                ; task slot. Every other register, AH
                                                ; included, is preserved. NOT callable
                                                ; from your entry proc: the loader has
                                                ; not published your instance yet, so
                                                ; it would simply refuse.
%define OSAPI_TASK_ALIVE    KERNEL_SEG:0x0168   ; BX = YOUR window ptr; the gfx lock
                                                ; must NOT be held, and it must be
                                                ; YOUR WORKER calling - never a window
                                                ; callback. Returns with every
                                                ; register AND the flags preserved
                                                ; while you are still alive. If your
                                                ; close box was clicked it NEVER
                                                ; RETURNS: the kernel destroys your
                                                ; window, frees your instance and
                                                ; releases your region (your segment
                                                ; is gone), and your task ends inside
                                                ; this call.

; --- the clip region (SPEC.md 11.3) -------------------------------------------
; What replaced the wm_obscured veto for background painters. Arm it under
; the lock and every gfx_*/font_* call you make is cut to the part of YOUR
; content nothing is covering; a window with one corner buried keeps drawing
; the rest instead of skipping the frame.
%define OSAPI_WM_CLIP_SET   KERNEL_SEG:0x0170   ; BX = YOUR window ptr; the gfx lock
                                                ; must be HELD. out CF = 1: not one
                                                ; pixel of your content is visible -
                                                ; draw nothing this frame (the answer
                                                ; wm_obscured used to give for a
                                                ; single covered pixel). CF = 0:
                                                ; clipping is armed. Preserves every
                                                ; register. It dies at your next
                                                ; OSAPI_GFX_UNLOCK, so it is one call
                                                ; per lock hold and there is nothing
                                                ; to undo.
%define OSAPI_WM_CLIP_CLEAR KERNEL_SEG:0x0178   ; disarm early, to draw unclipped
                                                ; again inside the same lock hold.
                                                ; Preserves every register.
%define OSAPI_WM_CLIP_TEST  KERNEL_SEG:0x0180   ; AX=x1, BX=y1, CX=x2, DX=y2
                                                ; (inclusive); out CF=0 the whole rect
                                                ; is drawable (also when no clip is
                                                ; armed), CF=1 it is not. Preserves
                                                ; every register. Ask this before you
                                                ; ERASE a rect you are about to draw
                                                ; text into: the gfx_* fills clip per
                                                ; pixel but font_char clips per whole
                                                ; 8x8 cell, so a wide erase followed
                                                ; by glyphs can white out a band the
                                                ; glyphs then refuse to repaint.
                                                ; Erase per cell, or skip the pair.
;
; Your worker gets DX = your instance index, DS = ES = CS = YOUR OWN
; SEGMENT, IF = 1 and the gfx lock free. The binding rules, none of them
; enforced:
;
;  1. ONE worker per instance. A second OSAPI_TASK_SPAWN just refuses.
;  2. NEVER return and NEVER exit on your own. Call OSAPI_TASK_ALIVE at
;     least once per outer-loop iteration and sleep when idle. This is the
;     sharp edge: a worker that exits by itself leaves the kernel holding a
;     dead task slot, and your close box then hides the window but never
;     frees the instance record - your region leaks for the rest of the
;     session. A worker that falls off the end and executes `ret` is worse:
;     it jumps to a near-random offset in your own segment.
;  3. NEVER hold the gfx lock across a long computation. Compute lock-free,
;     then take the lock for a short burst of drawing and release it. A
;     worker spinning under the lock wedges the whole machine, in either
;     scheduler mode, and no watchdog can break it.
;  4. gfx_lock is NOT reentrant: calling OSAPI_TASK_ALIVE while holding it
;     deadlocks - nothing ever draws again.
;  5. Under the lock, re-check that your window is still visible - call
;     OSAPI_WM_GEOM (CF=1 means hidden; the v2 idiom of testing
;     [bx+W_FLAGS] is dead, the record is kernel memory your DS cannot
;     reach) - and then call OSAPI_WM_CLIP_SET before drawing. Windows
;     move and get buried while you sleep, and the gfx_* primitives take
;     absolute screen coordinates: without one of these two answers you
;     paint over whatever is on top of you. CF = 1 means draw nothing this
;     frame; CF = 0 means draw normally and the kernel cuts every call to
;     the part of your content that shows. OSAPI_WM_OBSCURED is still
;     there and still correct, but it vetoes the whole frame for one
;     covered pixel - prefer the clip.
;  6. Your stack is 1536 bytes in LOW_SEG and SS != DS (see the header of
;     this file). No deep recursion, no big stack buffers, and BP-relative
;     operands address SS.
;  7. You may call: the gfx_* and font_* slots, OSAPI_SET_COLOR,
;     OSAPI_WM_CONTENT, OSAPI_WM_GEOM, OSAPI_WM_OBSCURED,
;     OSAPI_WM_CLIP_SET, OSAPI_WM_CLIP_CLEAR, OSAPI_WM_CLIP_TEST,
;     OSAPI_VIDEO, OSAPI_GET_TICKS, OSAPI_MOUSE, OSAPI_SRAND/RAND,
;     OSAPI_TASK_SLEEP, OSAPI_TASK_YIELD and OSAPI_TASK_ALIVE.
;     OSAPI_SET_COLOR writes ONE global pen shared by the whole machine,
;     so set it only inside the same lock hold as the drawing it colours -
;     lock-free, it repaints someone else's window in your colour. You may
;     NOT call the file slots, OSAPI_FILE_DLG, OSAPI_SND_STREAM or
;     OSAPI_SND_PLAY from a worker - those are UI-task-only and the kernel
;     cannot stop you.
;
; The kernel's own app_bounce_task is the reference implementation of all
; of this.

; --- CPU tier and memory above 1MB (SPEC.md 41) --------------------------------
; On a 286 or a 386 the machine has RAM above linear 0x0FFFFF that the 640KB
; conventional map cannot name. These five slots lend you some of it.
;
; READ THIS FIRST, because it is the rule the whole group is shaped around:
; EXTENDED MEMORY IS A DATA STORE ONLY. Real mode forms an address as
; seg<<4 + off, so the highest byte any CS:IP can name is 0x10FFEF - no code
; up there, no jump table, no callback, no second copy of your image, on any
; tier, ever. (Your own segment comes from the conventional arena below
; 640KB for exactly this reason, SPEC.md 2.5/20.7.) And you never address
; extended memory DIRECTLY at all: the 32-bit base OSAPI_XMEM_ALLOC hands
; you is an OPAQUE TOKEN, not a pointer you can build a segment from. Every
; byte you move crosses through OSAPI_XMEM_COPY, which is exactly why that
; slot carries one ABI over two transports - direct addressing needs unreal
; mode, which is tier 2 only and lives entirely inside the kernel. YOU are
; 8086 code: no 32-bit register, no FS, no GS, no `cpu 386` island of your
; own (SPEC.md 1 rule 1, no exception).
;
; BRANCH ON THE CAPS, NEVER ON THE TIER. OSAPI_XMEM_CAPS returning 0 KB is
; the only answer that means "do not try", and it is the answer on a 386
; whose A20 gate never verified just as much as on an 8088. The tier byte is
; information for a status line, not permission.
CPU_8086            equ 0        ; tier 0: 8086/8088. No A20 line, no memory
                                 ; above 1MB - the floor machine, and the
                                 ; answer you must still work on.
CPU_286             equ 1        ; tier 1: A20 + BIOS block move
CPU_386             equ 2        ; tier 2: the above, plus unreal mode
;
%define OSAPI_CPU_INFO      KERNEL_SEG:0x0188   ; no inputs; out AL = CPU_8086 /
                                                ; CPU_286 / CPU_386, AH = feature
                                                ; bits: bit 0 A20 verified open,
                                                ; bit 1 HMA claimed, bit 2 unreal
                                                ; mode armed. Preserves every register
                                                ; but AX. Any context, lock held or
                                                ; not - it reads two bytes.
%define OSAPI_XMEM_CAPS     KERNEL_SEG:0x0190   ; no inputs; out AX = extended-memory
                                                ; KB the pool can still hand out (0 =
                                                ; none, and the three slots below will
                                                ; all refuse), DX:CX = the pool's
                                                ; 32-bit linear base, BL = free
                                                ; block-table entries. Preserves BH,
                                                ; SI, DI. Any context.
%define OSAPI_XMEM_ALLOC    KERNEL_SEG:0x0198   ; DX:AX = bytes wanted, rounded up to
                                                ; 1KB; out CF=0 and DX:AX = the
                                                ; block's 32-bit linear base (opaque -
                                                ; hand it back to COPY and FREE, never
                                                ; dereference it), else CF=1 and AX =
                                                ; 0 no store / 1 no free block-table
                                                ; entry / 2 no contiguous run that
                                                ; big. The block is stamped with YOUR
                                                ; instance and force-freed when you
                                                ; close, exactly like a sound grant -
                                                ; but the table is small, so take one
                                                ; big block and subdivide it yourself
                                                ; rather than many.
%define OSAPI_XMEM_FREE     KERNEL_SEG:0x01A0   ; DX:AX = a base YOU own; out CF=0
                                                ; freed (and merged with the
                                                ; neighbouring free runs), CF=1 not a
                                                ; base or not yours. Preserves DX:AX.
%define OSAPI_XMEM_COPY     KERNEL_SEG:0x01A8   ; ES:SI = the conventional end, DX:AX
                                                ; = the 32-bit linear extended end,
                                                ; CX = bytes (EVEN, and <= 32768 -
                                                ; move more with two calls), DI = 0
                                                ; out to extended / 1 in from
                                                ; extended. out CF=0 done; CF=1 and
                                                ; AX = 1 no store / 2 the range is not
                                                ; inside a block you own / 3 bad
                                                ; length. ES is restored. The
                                                ; transport is not your business and
                                                ; you cannot tell which ran.
;
; CONTEXT, BINDING: these five are UI-task / window-callback only - a window
; callback, a menu handler or a file-dialog completion proc. NEVER from your
; worker task. On tier 1 the copy goes through int 15h AH=87h, and some 286
; BIOSes implement that call by RESETTING THE CPU to get back to real mode;
; taken with the gfx lock held that is not a slow frame, it is a dead
; machine. INFO and CAPS touch no BIOS and are harmless anywhere, but the
; honest rule is one rule for the whole group.

; --- window geometry (SPEC.md 11, new in v3) -----------------------------------
; The replacement for every read a v2 package did on its window record: your
; DS can no longer reach kernel memory, so the kernel answers instead.
%define OSAPI_WM_GEOM       KERNEL_SEG:0x01B0   ; BX = win ptr; out CX = content
                                                ; width, DX = content height (inner
                                                ; px - the box whose origin
                                                ; OSAPI_WM_CONTENT answers), CF=1 the
                                                ; window is not visible. Preserves
                                                ; everything else. Callable from any
                                                ; context, lock held or not, like
                                                ; OSAPI_VIDEO; the answer is only
                                                ; STABLE under the lock, which is
                                                ; when painters ask.

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
WMIN_W     equ 96                ; smallest frame a resize can leave
WMIN_H     equ 64

; --- app menu set layout (SPEC.md 12.2) ----------------------------------------
; Built with the OS88_MENUSET / OS88_MENU / OS88_MENUSET_END macros below;
; these offsets are here for code that walks its OWN set at run time (your
; set lives in your segment - the kernel keeps its own copy).
AM_NAME     equ 0           ; word: NUL app name for the bar label (<= 15
                            ; chars); 0 = fall back to the instance name
AM_ONCMD    equ 2           ; word: your command handler's offset (retf-
                            ; returning, SPEC.md 12.2)
AM_COUNT    equ 4           ; word: menu count, 0..MENU_APPMAX
AM_LIST     equ 6           ; AM_COUNT entries of:
AMENU_TITLE equ 0           ; word: NUL menu title (the bar cell's text)
AMENU_ITEMS equ 2           ; word: array of near ptrs to NUL item strings
AMENU_NITEM equ 4           ; word: item count
AMENU_ENTSZ equ 6
MENU_APPMAX equ 4           ; menus the bar can host per app; extras are
                            ; silently dropped by the kernel's copy pass
MENU_ITEMMAX equ 8          ; items per menu the kernel keeps; extras are
                            ; silently dropped (SPEC.md 12.2)
MENU_STRMAX  equ 19         ; chars per name/title/item string the kernel
                            ; keeps; longer strings are silently truncated

; --- window record layout (SPEC.md 11, 20 bytes) -------------------------------
; AUTHORING REFERENCE ONLY. Since v3 a window pointer is an OPAQUE HANDLE:
; the record lives in kernel memory your DS cannot reach, so these offsets
; must NEVER be dereferenced from package code - `mov ax,[bx+W_W]` reads
; your own segment's garbage, not the window. Ask OSAPI_WM_GEOM for live
; content geometry + visibility and OSAPI_WM_CONTENT for the content
; origin. The proc words are offsets in YOUR segment; the kernel pairs
; them with it at every dispatch and your procs return with `retf`.
W_FLAGS   equ 0             ; word: bit0 = used, bit1 = visible, bit2 =
                            ; resizable, bit3 = fullscreen
W_X       equ 2             ; word: frame left (screen coords)
W_Y       equ 4             ; word: frame top (y >= MBAR_H - except under
                            ; WF_FULL, SPEC.md 11.2)
W_W       equ 6             ; word: frame width, outer
W_H       equ 8             ; word: frame height, includes title bar
W_TITLE   equ 10            ; word: title string offset (in the owner's seg)
W_PAINT   equ 12            ; word: paint proc offset; SI = win ptr
W_ONKEY   equ 14            ; word: key proc offset or 0; AL=ascii, AH=scan
W_ONCLICK equ 16            ; word: click proc offset or 0; CX=x, DX=y
W_MENUS   equ 18            ; word: app menu set offset, 0 = none. NOT a
                            ; template word - set it with OSAPI_MENU_SET
WIN_SIZE  equ 20
MAX_WIN   equ 12

; --- wm_create template layout (SPEC.md 11, 16 bytes, 8 words) -----------------
; The template is YOURS (the kernel stages it out of your segment); its
; title/paint/onkey/onclick words are org-0 offsets in your segment.
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
; OS88_HEADER 'NAME', entry [, flags [, needs_kb]] - open the package and
; emit the 32-byte v3 header (SPEC.md 20.2). Emits cpu 8086 / bits 16 /
; org 0 first, so this must be the first thing after the %include. The
; image-size and bss-size words are forward references resolved by
; OS88_IMAGE_END / OS88_BSS at the end of the source. The name is
; NUL-padded to exactly 16 bytes. The optional third parameter is the
; flags byte (default 0); flags bit 0 declares an embedded icon, which
; must follow immediately as a OS88_ICON16 .. OS88_ICON16_END block. The
; optional fourth parameter is the minimum conventional RAM in KB
; (default 500): a machine below it refuses the load with the precise
; "Needs nnnK" instead of "Out of memory" - it is a statement about the
; MACHINE, not a promise of fit (the arena can still be full). 500, not
; 512, for the same reason the double-buffer floor is (SPEC.md 32): the
; check compares the RAW int 12h KB, and a real 512KB machine reports
; less once its BIOS/EBDA takes a cut - a 512 default would refuse the
; smallest machine class the arena exists to serve.
; -----------------------------------------------------------------------------
%macro OS88_HEADER 2-4
    cpu 8086
    bits 16
    org 0                   ; v3: own segment, no link base, no relocation
                            ; (SPEC.md 20.1/20.2)
    db 'O', '8'             ; +0  magic (word 0x384F)
    db 3                    ; +2  format version 3: org 0, own segment
    %if %0 >= 3
    db %3                   ; +3  flags (bit 0 = embedded icon)
    %else
    db 0                    ; +3  flags
    %endif
    dw 0                    ; +4  zero (was the v2 link base)
    dw (%2) - $$            ; +6  entry offset, image-relative - which at
                            ;     org 0 IS the plain segment offset
    dw OS88_IMAGE_SIZE       ; +8  image size = the file size, exactly
    dw OS88_BSS_SIZE         ; +10 bytes the loader zeroes after the image
    dw 0                    ; +12 zero (was the v2 relocation count)
    %if %0 >= 4
    dw %4                   ; +14 memory requirement, KB (SPEC.md 20.2)
    %else
    dw 500                  ; +14 memory requirement: 500KB default - the
                            ;     "512KB machine" class as the RAW int 12h
                            ;     reports it after the BIOS cut (the
                            ;     SPEC.md 32 double-buffer-floor precedent)
    %endif
    %strlen %%len %1
    %if %%len > 15
        %error "OS88_HEADER: program name must be at most 15 characters (the 16-byte field is NUL-padded)"
    %endif
    db %1                   ; +16 program name...
    times 16 - %%len db 0   ;     ...NUL-padded to 16 bytes
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
; The kernel COPIES the whole set out of your segment when the bar is laid
; out (SPEC.md 12.2), applying the pinned caps: at most MENU_APPMAX menus,
; MENU_ITEMMAX items per menu, MENU_STRMAX chars per string - overflow is
; a silent clamp, never an error. The pointer words are org-0 offsets in
; your own segment, which is exactly what the copy expects.
;
; The handler is FAR-called by the kernel UNDER THE GFX LOCK on the UI
; task, with CS = DS = ES = your segment, AL = item index, AH = menu index
; (0-based), SI = the owning window (opaque), BX = your set's offset. It
; obeys the W_ONCLICK rules (SPEC.md 11): it may draw and may call the
; file slots, but must never take the gfx lock or block, and it returns
; with `retf` (SPEC.md 20.5). It MAY call OSAPI_TASK_SPAWN - a menu
; command is the natural place to start your worker.
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
; one-segment budget at assembly time (SPEC.md 20.1).
; -----------------------------------------------------------------------------
%macro OS88_IMAGE_END 0
    %define OS88__HAVE_END
os88_image_end:
OS88_IMAGE_SIZE equ os88_image_end - $$
    %ifndef OS88__HAVE_BSS
OS88_BSS_SIZE equ 0
    %endif
    %if (OS88_IMAGE_SIZE + OS88_BSS_SIZE) > (PKG_MAX_PARA * 16)
        %error "package too big: image + bss must fit one segment (PKG_MAX_PARA*16 = 65,520 bytes)"
    %endif
%endmacro
