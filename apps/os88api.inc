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
; SS = LOW_SEG (0x0800) - every task stack lives OUTSIDE the kernel segment,
; so SS != DS and `[bp+disp]` addresses SS, not your data. A pointer held in
; BP needs an explicit `ds:` override. That has always been true inside a
; callback (you run on the UI task's stack) and matters more now that a
; package can own a worker task of its own (OSAPI_TASK_SPAWN below), whose
; 1536-byte stack is likewise in LOW_SEG.
;
; A package is
; assembled at the APP_LOAD_OFF link base and RELOCATED at load time to a
; per-instance region - several packages (or several instances of one) can
; be resident at once, each a fully relocated copy with its own bss
; (SPEC.md 20.1). This imposes ONE coding rule (SPEC.md 20.2): a package
; address may only be embedded as a whole 16-bit word (imm16, disp16,
; dw label) - never byte-split, shifted or folded into other constants;
; os88pkg.py's reconstruction check fails the build otherwise. Every OSAPI_*
; symbol below is a fixed near-call target (4-byte jump-table slots in
; kernel.asm). Register contracts are the target routines' own (SPEC.md
; 5, 6, 8, 11, 20.4); all of them preserve every register except their
; documented outputs.
; =============================================================================

; --- environment ---------------------------------------------------------------
KERNEL_SEG   equ 0x1000     ; CS = DS for kernel and packages alike; SS is
                            ; LOW_SEG (0x0800), NOT this - see above
APP_LOAD_OFF equ 0xB000     ; where packages load (kernel segment offset)
APP_MAX_SIZE equ 0x4E00     ; image + bss budget, 0xB000..0xFDFF
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

; --- the os8088 API jump table (SPEC.md 20.3, pinned offsets) ---------------------
; Each slot is a near jmp; `call OSAPI_*` from package code is a plain near
; call into the kernel routine named in the comment.
OSAPI_GFX_LOCK      equ 0x0010   ; acquire drawing mutex + hide cursor
OSAPI_GFX_UNLOCK    equ 0x0014   ; show cursor + release mutex
OSAPI_GFX_PIXEL     equ 0x0018   ; CX=x, DX=y
OSAPI_GFX_HLINE     equ 0x001C   ; AX=x1, BX=x2, DX=y
OSAPI_GFX_VLINE     equ 0x0020   ; AX=x, BX=y1, DX=y2
OSAPI_GFX_FILL      equ 0x0024   ; AX=x1, BX=y1, CX=x2, DX=y2
OSAPI_GFX_FRAME     equ 0x0028   ; AX=x1, BX=y1, CX=x2, DX=y2
OSAPI_GFX_FILL_GRAY equ 0x002C   ; AX=x1, BX=y1, CX=x2, DX=y2 (50% dither)
OSAPI_GFX_XOR_RECT  equ 0x0030   ; AX=x1, BX=y1, CX=x2, DX=y2
OSAPI_GFX_XOR_FILL  equ 0x0034   ; AX=x1, BX=y1, CX=x2, DX=y2
OSAPI_FONT_CHAR     equ 0x0038   ; CX=x, DX=y, AL=char
OSAPI_FONT_STR      equ 0x003C   ; CX=x, DX=y, SI=NUL string
OSAPI_FONT_WIDTH    equ 0x0040   ; SI=NUL string; out AX = pixel width
OSAPI_WM_CREATE     equ 0x0044   ; SI=template; out BX = win ptr, CF on full
OSAPI_WM_SHOW       equ 0x0048   ; BX=win ptr
OSAPI_WM_HIDE       equ 0x004C   ; BX=win ptr
OSAPI_WM_FRONT      equ 0x0050   ; BX=win ptr
OSAPI_WM_CONTENT    equ 0x0054   ; BX=win ptr; out AX=content left, DX=top
OSAPI_WM_OBSCURED   equ 0x0058   ; BX=win ptr; out CF=1 if covered
OSAPI_TASK_YIELD    equ 0x005C   ; give up the time slice
OSAPI_TASK_SLEEP    equ 0x0060   ; AX = ticks to sleep (18 ~ 1 second)
OSAPI_GET_TICKS     equ 0x0064   ; out AX = [ticks]
OSAPI_SET_COLOR     equ 0x0068   ; AL -> [gfx_color]
OSAPI_MOUSE         equ 0x006C   ; out CX=mouse_x, DX=mouse_y, AL=mouse_btn
OSAPI_SRAND         equ 0x0070   ; AX -> [osapi_seed]
OSAPI_RAND          equ 0x0074   ; out AX = next pseudo-random word

; --- sound (SPEC.md 20.3/34): the whole surface exists from Phase 1; the
; --- unbuilt tiers return their documented error paths until they land.
OSAPI_SND_CAPS      equ 0x0078   ; out AX = merged caps word (SND_CAP_*),
                                 ; BL = tone route (0 = speaker, 1 = OPL2),
                                 ; DX = present-driver bits
OSAPI_SND_TONE      equ 0x007C   ; AX = freq Hz (0 = off), CX = duration
                                 ; ticks (0 = until off), DL = priority
                                 ; (default 0x40); out CF=1 refused, else
                                 ; AL = owner generation
OSAPI_SND_PLAY      equ 0x0080   ; PCM_EXCL clip (SPEC.md 34.4): ES:SI, CX,
                                 ; DX = rate; out AX = 0 ok / 1..5 err
                                 ; (err 4 no-sink until Phase 2)
OSAPI_SND_FM        equ 0x0084   ; FM verbs (SPEC.md 34.2); out CF=1 no FM
                                 ; sink (always, until Phase 3)
; NEVER call OSAPI_SND_STREAM from your worker task - any verb, staging
; included (SPEC.md 20.3/34.5). The open verbs' busy/half-duplex refusals
; rest on every caller being serialized on the UI task, and teardown is not
; fenced against a task. Poll status from a callback instead.
OSAPI_SND_STREAM    equ 0x0088   ; PCM_BG streams + staging (SPEC.md 20.3):
                                 ; AL = verb (0 open / 1 feed / 2 close /
                                 ; 3 status / 6 stage / 7 grant), AH =
                                 ; handle or sub-op; out AX = 0 ok / err /
                                 ; SND_ST_* state (verb 3); recording
                                 ; (verbs 4-5) err 4 until Phase 5

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
OSAPI_WM_SIZABLE    equ 0x008C   ; BX=win ptr, AL = 0 clear / non-zero set
                                 ; WF_SIZABLE; call from the entry proc
                                 ; after OSAPI_WM_CREATE. A resizable
                                 ; window's paint/click procs MUST lay out
                                 ; from the live W_W/W_H every call.
OSAPI_FULLSCREEN    equ 0x0090   ; AL = 1 enter (BX=win ptr) / 0 exit;
                                 ; caller holds the gfx lock (window
                                 ; callbacks do); out CF=1 enter refused
                                 ; (screen already owned). Exiting is the
                                 ; app's job (Esc in its onkey is the
                                 ; convention) - the menu bar is
                                 ; unreachable while it holds the screen.
OSAPI_WM_GROW       equ 0x0094   ; BX=win ptr; caller holds the gfx lock.
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
OSAPI_FILE_WRITE    equ 0x0098   ; SI = NUL 8.3 name, ES:BX = bytes, CX =
                                 ; count (0 = empty file); creates or
                                 ; REPLACES. out CF=0 AX=0, else AX = FERR_*
OSAPI_FILE_READ     equ 0x009C   ; SI = name, ES:BX = buffer, CX = capacity;
                                 ; out CF=0 AX = bytes read, else AX =
                                 ; FERR_* (FERR_BIG leaves the buffer alone)
OSAPI_FILE_DELETE   equ 0x00A0   ; SI = name; out CF=0 AX=0, else FERR_*
OSAPI_FILE_RENAME   equ 0x00A4   ; SI = old name, DI = new name; out as above
OSAPI_FILE_DFREE    equ 0x00A8   ; out CF=0, DX:AX = free bytes, BX =
                                 ; SECTORS per cluster; no disk I/O

; --- menus (SPEC.md 12.2) ------------------------------------------------------
; Every application owns the menu bar while its window is frontmost: the
; System (chip) menu stays put, the app's NAME is drawn to its right, and
; the app's own menus follow. Register the set from the entry proc, with
; BX = the window OSAPI_WM_CREATE just returned - the loader shows the
; window afterwards, so the first bar drawn is already yours.
OSAPI_MENU_SET      equ 0x00AC   ; BX = win ptr, SI = menu set ptr (0 =
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
OSAPI_FILE_DLG      equ 0x00B0   ; AL = 0 Open / 1 Save, BX = your window ptr,
                                 ; DI = completion proc, SI = default name
                                 ; (NUL, <= 12) or 0. out CF=0 accepted, CF=1
                                 ; refused (one is already up, or no room)
FDLG_OPEN  equ 0                 ; the two modes, in AL going in and in AL
FDLG_SAVE  equ 1                 ; coming back
;
OSAPI_VIDEO         equ 0x00B4   ; the screen this machine actually has
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
;          name (NUL, <= 12 chars). No register need be preserved.
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
OSAPI_TASK_SPAWN    equ 0x00B8   ; AX = your worker's near entry (a plain
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
OSAPI_TASK_ALIVE    equ 0x00BC   ; BX = YOUR window ptr; the gfx lock must
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
OSAPI_WM_CLIP_SET   equ 0x00C0   ; BX = YOUR window ptr; the gfx lock must be
                                 ; HELD. out CF = 1: not one pixel of your
                                 ; content is visible - draw nothing this
                                 ; frame (the answer wm_obscured used to
                                 ; give for a single covered pixel). CF = 0:
                                 ; clipping is armed. Preserves every
                                 ; register. It dies at your next
                                 ; OSAPI_GFX_UNLOCK, so it is one call per
                                 ; lock hold and there is nothing to undo.
OSAPI_WM_CLIP_CLEAR equ 0x00C4   ; disarm early, to draw unclipped again
                                 ; inside the same lock hold. Preserves
                                 ; every register.
OSAPI_WM_CLIP_TEST  equ 0x00C8   ; AX=x1, BX=y1, CX=x2, DX=y2 (inclusive);
                                 ; out CF=0 the whole rect is drawable (also
                                 ; when no clip is armed), CF=1 it is not.
                                 ; Preserves every register. Ask this before
                                 ; you ERASE a rect you are about to draw
                                 ; text into: the gfx_* fills clip per pixel
                                 ; but font_char clips per whole 8x8 cell, so
                                 ; a wide erase followed by glyphs can white
                                 ; out a band the glyphs then refuse to
                                 ; repaint. Erase per cell, or skip the pair.
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
;  6. Your stack is 1536 bytes in LOW_SEG and SS != DS (see the header of
;     this file). No deep recursion, no big stack buffers, and BP-relative
;     operands address SS.
;  7. You may call: the gfx_* and font_* slots, OSAPI_SET_COLOR,
;     OSAPI_WM_CONTENT, OSAPI_WM_OBSCURED, OSAPI_WM_CLIP_SET,
;     OSAPI_WM_CLIP_CLEAR, OSAPI_WM_CLIP_TEST, OSAPI_VIDEO, OSAPI_GET_TICKS,
;     OSAPI_MOUSE, OSAPI_SRAND/RAND, OSAPI_TASK_SLEEP, OSAPI_TASK_YIELD and
;     OSAPI_TASK_ALIVE. OSAPI_SET_COLOR writes ONE global pen shared by the
;     whole machine, so set it only inside the same lock hold as the drawing
;     it colours - lock-free, it repaints someone else's window in your
;     colour. You may NOT call the file slots, OSAPI_FILE_DLG,
;     OSAPI_SND_STREAM or OSAPI_SND_PLAY from a worker - those are
;     UI-task-only and the kernel cannot stop you.
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
WIN_SIZE  equ 20
MAX_WIN   equ 12

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
; header (SPEC.md 20.2). Emits cpu 8086 / bits 16 / org APP_LOAD_OFF first,
; so this must be the first thing after the %include. The image-size and
; bss-size words are forward references resolved by OS88_IMAGE_END / OS88_BSS
; at the end of the source. The name is NUL-padded to exactly 16 bytes.
; The optional third parameter is the flags byte (default 0); flags bit 0
; declares an embedded icon, which must follow immediately as a
; OS88_ICON16 .. OS88_ICON16_END block.
; -----------------------------------------------------------------------------
%macro OS88_HEADER 2-3
    cpu 8086
    bits 16
%ifdef OS88_ORG
    org OS88_ORG             ; relocation-probe pass (Makefile passes
%else                       ; -DOS88_ORG=0xB800; os88pkg.py diffs the two
    org APP_LOAD_OFF        ; assemblies into the fixup table, SPEC.md 24)
%endif
    db 'O', '8'             ; +0  magic (word 0x384F)
    db 2                    ; +2  format version 2: relocatable (SPEC.md 20.2)
    %if %0 >= 3
    db %3                   ; +3  flags (bit 0 = embedded icon)
    %else
    db 0                    ; +3  flags
    %endif
    dw APP_LOAD_OFF         ; +4  link base (constant across both passes)
    dw (%2) - $$            ; +6  entry offset, image-relative
    dw OS88_IMAGE_SIZE       ; +8  image size (resident bytes; the reloc
                            ;     table os88pkg appends is excluded)
    dw OS88_BSS_SIZE         ; +10 bytes the loader zeroes after the image
    dw 0                    ; +12 relocation count - stamped by os88pkg.py
    dw 0                    ; +14 reserved
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
; The handler is near-called by the kernel UNDER THE GFX LOCK on the UI
; task, with AL = item index, AH = menu index (0-based), SI = the owning
; window, BX = the set. It obeys the W_ONCLICK rules (SPEC.md 11): it may
; draw and may call the file slots, but must never take the gfx lock or
; block. It MAY call OSAPI_TASK_SPAWN - a menu command is the natural place
; to start your worker.
;
; The count is a label DIFFERENCE, so it is identical in both relocation
; passes and os88pkg.py never mistakes it for a package address; the three
; pointer words per menu are ordinary whole-word addresses and relocate.
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
