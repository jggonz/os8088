; =============================================================================
; os8088 1.0 - kernel
;
; A Macintosh-style GUI for the 8086: 640x480x16 (VGA mode 12h), pre-emptive
; round-robin multitasking off the PIT, serial Microsoft mouse on COM1.
;
; Runs in real mode, near model: CS = DS = KERNEL_SEG for all kernel code and
; every task, SS = LOW_SEG, ES scratch. Inter-module calls inside .text are
; near; .fartext modules are reached through the far shims in SPEC.md 33. The
; module contracts (register use, data layouts, concurrency rules) live in
; SPEC.md; that document is binding.
;
; Three fixed entry points:
;   1000:0000  cold entry (the boot sector jumps here)
;   1000:0008  boot splash tick (SPEC.md 15): far-called by the boot sector
;              after every sector it reads, while the rest of this image is
;              still coming off the floppy
;   1000:0010  os8088 API jump table (SPEC.md 20.3): 8-byte far slots at
;              pinned offsets, far-called by loaded programs (replaces the
;              retired syscall gate). Each slot switches DS to KERNEL_SEG
;              itself: a v3 package runs CS = DS = ES = its own segment
;              (SPEC.md 20.1) and reaches the kernel only through here
; =============================================================================

cpu 8086
bits 16
org 0x0000

; --- global constants (SPEC.md 3) -------------------------------------------
KERNEL_SEG  equ 0x1000
SAVE_SEG    equ 0x2000          ; save-under heap (menus), via ES; extent
                                ; pinned to 0x20000..0x2BFFF (SPEC.md 2.2) -
                                ; VIEW_SEG owns the top 16KB of the block
VGA_SEG     equ 0xA000          ; mode 12h planar framebuffer
SCREEN_W    equ 640
SCREEN_H    equ 480
ROW_BYTES   equ 80
MBAR_H      equ 20              ; menu bar height, px
TITLE_H     equ 18              ; window title bar height, px

CBLACK      equ 0
CWHITE      equ 15
CLGRAY      equ 7
CDGRAY      equ 8

; --- the clip region (SPEC.md 11.3) ------------------------------------------
; wm.inc builds it; vga12.inc, font.inc and icons.inc consume it. The layout
; is therefore a cross-module contract and lives here rather than in wm.inc,
; which NASM only reaches five includes later.
WM_CLIP_MAX equ 16              ; rects; more than this and the frame is
                                ; skipped instead, which is exactly what the
                                ; wm_obscured veto used to do
WCR_X1  equ 0                   ; one clip rect, 8 bytes, inclusive corners
WCR_Y1  equ 2
WCR_X2  equ 4
WCR_Y2  equ 6
WCR_SZ  equ 8

; loadable programs (SPEC.md 20) - v3: every package in its OWN segment,
; allocated from the conventional arena (SPEC.md 2.5). The kernel-segment
; pool at 0xB000..0xFDFF is gone; the whole 64KB window is the kernel's
; again (guards 1-2 at the end of this file).
ARENA_SEG     equ 0x6580        ; arena base paragraph - linear 0x65800,
                                ; exactly one paragraph past the back
                                ; buffer's pinned 0x40000..0x657FF extent
                                ; (BB_SEG + 4 x BB_PLANE_PARA). Deliberately
                                ; NOT a function of bb state, so packages
                                ; and bb_set can never fight over a
                                ; paragraph at run time (SPEC.md 2.5). The
                                ; top is [ld_arena_top], probed from int 12h
                                ; by loader_init; below 407KB of RAM the
                                ; arena is empty and package loads refuse
                                ; with a message, never a crash.
PKG_MAX_PARA  equ 0x0FFF        ; region cap, paragraphs (65,520 bytes):
                                ; keeps every I_SIZE*16 product inside 16
                                ; bits (SPEC.md 20.6's ownership fence
                                ; does that multiply)
OSAPI_STR_MAX equ 63            ; staged-string cap, chars, NUL excluded
                                ; (SPEC.md 20.3 marshalling)
OSAPI_NAME_MAX equ 13           ; staged 8.3-NAME cap, chars: one MORE than
                                ; a legal name's 12, ON PURPOSE - a 13+ char
                                ; name keeps at least 13 chars in the stage,
                                ; which dskw_name83/fdlg reject as illegal,
                                ; so an over-long name is REFUSED, never
                                ; silently truncated into some OTHER legal
                                ; name it happens to prefix (SPEC.md 20.3)

; --- low memory (SPEC.md 2.1) ------------------------------------------------
; Linear 0x00600..0x0FFFF is free on every machine once the boot sector has
; handed off: the BIOS data area ends at 0x004FF and the kernel image starts
; at 0x10000. Two segments carve it up, and everything either of them holds
; is one thing the kernel's own 64KB window no longer has to.
FAR_SEG     equ 0x0060          ; linear 0x00600 - far code (section .fartext)
FAT_SEG     equ 0x0300          ; linear 0x03000 - mount-time FAT snapshot
                                ; (SPEC.md 2.1/18), reached via ES ONLY,
                                ; never DS; dsk_next_clus is the one reader
DSK_FAT_SECS equ 32             ; resident FAT cap, sectors (16,384 bytes)
LOW_SEG     equ 0x0800          ; linear 0x08000 - task stacks + disk buffers
LOW_LIMIT   equ 0x8000          ; LOW_SEG:0x8000 IS KERNEL_SEG:0 - every
                                ; LOW_SEG offset must stay strictly below it
STK0_TOP    equ 0x7FFE          ; task 0's stack top (grows down towards the
                                ; top of .lowbss; the assertion at the end of
                                ; this file keeps 8KB of clearance)

; sound (SPEC.md 34, claimed in Phase 2)
SND_SEG     equ 0x3000          ; sound buffers: linear 0x30000..0x3FFFF, the
                                ; last free 64KB block on the 256KB floor -
                                ; reached via ES only, never DS (SPEC.md 2.2)
SND_SEG_KB  equ 64              ; what it adds to the Task Manager RAM figure
                                ; (SPEC.md 2.2/28, the KLOWFAR_KB idiom)

; the file manager's per-window view cache (SPEC.md 2.3/22.1)
; Four 4KB slots carved off the TOP of the SAVE_SEG block, which has exactly
; one user with exactly one save live at a time (menu_track's save-under) and
; a ~11KB worst case against the 48KB it keeps. What the 16KB buys is that a
; background file-manager window paints from memory: wm_paint_all has no clip
; rect and runs on every window move, so re-listing on demand would mean one
; full floppy mount per visible window per drag pass, under the gfx lock.
VIEW_SEG      equ 0x2C00        ; linear 0x2C000..0x2FFFF, reached via ES ONLY
VIEW_SLOTS    equ 4             ; = the Disk kind's KD_CAP (SPEC.md 29.3)
VIEW_SLOT_SEG equ 0x0100        ; 4,096 bytes per slot, in paragraphs
VIEW_SEG_KB   equ 16            ; Task Manager RAM figure (SPEC.md 28)

; double buffering (SPEC.md 32)
BB_SEG        equ 0x4000        ; back buffer base segment (plane 0)
BB_PLANE_PARA equ 0x960         ; paragraphs per plane (0x9600 = 480 rows x 80)
DB_MIN_KB     equ 500           ; int 12h floor: double-buffer only at >= 500KB
                                ; (a real 512KB machine reports less once the
                                ; BIOS takes its cut - 500 lets those in, and
                                ; the buffer ends at 0x657FF = 406KB anyway)

; --- CPU tiers and memory above 1MB (SPEC.md 41) -----------------------------
; None of this exists on tier 0, which is the target machine: an 8088 has no
; A20 line, nothing above linear 0x0FFFFF, and every routine keyed off these
; constants returns having touched no port. The tier is INFORMATION, not
; permission - kernel code branches on the verified feature bits and packages
; branch on the KB figure from osapi_xmem_caps (SPEC.md 41.1/41.8).
CPU_8086    equ 0               ; tier 0: 8086/8088. No A20, no HMA, no store.
                                ; The default [cpu_tier] and the fallback.
CPU_286     equ 1               ; tier 1: A20 gate + HMA, int 15h AH=88h
                                ; sizing and AH=87h block move (SPEC.md 41.5)
CPU_386     equ 2               ; tier 2: all of tier 1, plus unreal mode -
                                ; a 4GB data limit on FS/GS (SPEC.md 41.4)
HMA_SEG     equ 0xFFFF          ; the one segment above 1MB: HMA_SEG:0010 is
HMA_MIN_OFF equ 0x0010          ; linear 0x100000 (0xFFFF0 + 0x10) and
HMA_BYTES   equ 0xFFF0          ; HMA_SEG:FFFF is linear 0x10FFEF - the
                                ; highest byte real mode can name at all.
                                ; 65,520 bytes, DATA ONLY: the near model
                                ; pins CS = DS = KERNEL_SEG, so no code ever
                                ; lives up there (SPEC.md 41.3/41.9 rule 3)
XM_HMA_KB   equ 64              ; what a successful cpu_hma_claim takes off
                                ; the xm pool - the HMA is the first 64KB of
                                ; exactly the RAM AH=88h sizes (SPEC.md 2.4)
XM_MAX_BLKS equ 8               ; xm_alloc's fixed block table, entries: a
                                ; bulk store for a handful of large claims,
                                ; not a malloc (SPEC.md 41.5)

; =============================================================================
; Section layout (SPEC.md 2.1) - declared here, once, with attributes; every
; module afterwards switches with a bare `section .text` / `.bss` / `.fartext`
; / `.lowbss`. NASM's -f bin resolves the attributes at layout time, so a
; forward reference to .text below is fine.
;
;   .text     the kernel image, org 0, KERNEL_SEG
;   .fartext  far code (SPEC.md 33). Loaded from the floppy immediately after
;             .text but ASSEMBLED AT vstart=0, because it is copied down to
;             FAR_SEG:0000 by kmain before anything uses .bss. Costs the
;             kernel window nothing at run time.
;   .bss      kernel scratch, KERNEL_SEG. vfollows=.text - NOT .fartext - so
;             it deliberately OVERLAPS the far blob's landing zone: the blob
;             is copied out before .bss is touched, and .bss is uninitialised
;             by definition, so the same addresses serve both in turn.
;   .lowbss   scratch in LOW_SEG (SPEC.md 2.1): task stacks and the disk
;             buffers. vstart=0, addressed through SS or ES, never DS.
; =============================================================================
section .fartext follows=.text align=1 vstart=0
section .lowbss  nobits vstart=0
section .bss     nobits vfollows=.text valign=1
section .text

; =============================================================================
; Fixed entry points
; =============================================================================
cold_entry:
    jmp kmain

    times 0x08 - ($ - $$) db 0
    jmp near spl_tick           ; 1000:0008 - boot splash tick (SPEC.md 15)

    times 0x10 - ($ - $$) db 0  ; the table must land exactly at 0x0010

; =============================================================================
; os8088 API jump table (SPEC.md 20.3) - v3: 8-byte FAR slots
;
; Loaded programs `call far` these pinned absolute seg:off addresses (the
; OSAPI_* %defines in os88api.inc spell them); each slot is an 8-byte
; DS-switching stub, 1E 0E 1F E8 rr rr 1F CB - exactly 8 bytes, no pad:
;
;     push ds        ; the caller's segment, recoverable by a marshalling
;     push cs        ; wrapper at SS:SP+2 (SS = LOW_SEG on every task)
;     pop  ds        ; ...this pair is the `mov ds, imm` the 8086 lacks:
;     call target    ; every kernel body assumes DS = KERNEL_SEG, and the
;     pop  ds        ; table is .text, so CS here IS KERNEL_SEG
;     retf
;
; Neither `pop ds` nor `retf` touches flags, so every CF-returning slot
; (wm_create, wm_obscured, the dskw_* five, fdlg_open, inst_pkg_spawn, the
; clip trio, wm_geom, the xm_* trio) keeps its contract through the stub,
; and menu_win_set's stronger all-flags promise survives too. Register
; contracts are the target routines' own. The slot ORDER below IS the ABI
; and is preserved from v2 exactly - never reorder; only the stride changed,
; 4 -> 8, so slot i sits at 0x0010 + 8*i (SPEC.md 20.3 pins the layout).
;
; Slots that take a DS-relative POINTER (font_str/font_width SI, wm_create's
; template, the dskw_* names, fdlg_open's default name, snd_fm's patch,
; snd_stream's verb-5/6 buffers) point at kernel-side osapi_w_* marshalling
; wrappers (SPEC.md 20.3/20.4, bodies at the tail of this file): each
; recovers the caller's segment from the DS word this stub saved - at
; SS:[bp+4] once the wrapper frames - stages the operand into kernel
; scratch, and near-calls the raw routine on a plain kernel pointer.
; Kernel-internal callers keep near-calling the raw routines and never see
; any of it. ES-relative buffer contracts (dskw data ES:BX, snd_play ES:SI,
; xm_copy ES:SI) already carry their segment and need nothing.
;
; inst_pkg_alive may NEVER RETURN (SPEC.md 20.6): its teardown path ends in
; task_exit, abandoning the stub's saved caller-DS word on the dying
; worker's stack - harmless, the stack dies with the task.
; =============================================================================
%macro OSAPI_SLOT 1
    push ds
    push cs                     ; CS = KERNEL_SEG: the table lives in .text
    pop ds
    call %1                     ; near, inside the kernel
    pop ds
    retf                        ; 8 bytes exactly - no pad byte
%endmacro

osapi_table:
    OSAPI_SLOT gfx_lock          ; 0x0010
    OSAPI_SLOT gfx_unlock        ; 0x0018
    OSAPI_SLOT gfx_pixel         ; 0x0020
    OSAPI_SLOT gfx_hline         ; 0x0028
    OSAPI_SLOT gfx_vline         ; 0x0030
    OSAPI_SLOT gfx_fill          ; 0x0038
    OSAPI_SLOT gfx_frame         ; 0x0040
    OSAPI_SLOT gfx_fill_gray     ; 0x0048
    OSAPI_SLOT gfx_xor_rect      ; 0x0050
    OSAPI_SLOT gfx_xor_fill      ; 0x0058
    OSAPI_SLOT font_char         ; 0x0060
    OSAPI_SLOT osapi_w_font_str  ; 0x0068 - SI string, staged to osapi_sbuf
    OSAPI_SLOT osapi_w_font_width; 0x0070   (SPEC.md 20.3 marshalling)
    OSAPI_SLOT osapi_w_wm_create ; 0x0078 - SI template, staged whole
    OSAPI_SLOT wm_show           ; 0x0080
    OSAPI_SLOT wm_hide           ; 0x0088
    OSAPI_SLOT wm_front          ; 0x0090
    OSAPI_SLOT wm_content        ; 0x0098
    OSAPI_SLOT wm_obscured       ; 0x00A0
    OSAPI_SLOT task_yield        ; 0x00A8
    OSAPI_SLOT task_sleep        ; 0x00B0
    OSAPI_SLOT osapi_get_ticks    ; 0x00B8
    OSAPI_SLOT osapi_set_color    ; 0x00C0
    OSAPI_SLOT osapi_mouse        ; 0x00C8
    OSAPI_SLOT osapi_srand        ; 0x00D0
    OSAPI_SLOT osapi_rand         ; 0x00D8
    OSAPI_SLOT osapi_snd_caps     ; 0x00E0 - sound (SPEC.md 20.3/34): all
    OSAPI_SLOT osapi_snd_tone     ; 0x00E8   five slots ship in Phase 1;
    OSAPI_SLOT osapi_snd_play     ; 0x00F0   PLAY takes ES:SI (unchanged);
    OSAPI_SLOT osapi_w_snd_fm     ; 0x00F8   FM verb 2's patch is staged;
    OSAPI_SLOT osapi_w_snd_stream ; 0x0100   STREAM records the caller's
                                  ;          segment for verbs 5/6, whose
                                  ;          staging copy IS the boundary
    OSAPI_SLOT wm_sizable         ; 0x0108 - window features (SPEC.md 11.1)
    OSAPI_SLOT wm_fullscreen      ; 0x0110 - fullscreen (SPEC.md 11.2)
    OSAPI_SLOT wm_grow_paint      ; 0x0118 - grow-box restore after a
                                  ;          self-repaint (SPEC.md 11.1)
    OSAPI_SLOT osapi_w_dskw_write ; 0x0120 - files (SPEC.md 18.4/20.3): the
    OSAPI_SLOT osapi_w_dskw_read  ; 0x0128   dskw_* contracts ARE the ABI;
    OSAPI_SLOT osapi_w_dskw_delete; 0x0130   the SI/DI NAMES are staged to
    OSAPI_SLOT osapi_w_dskw_rename; 0x0138   osapi_nbuf/osapi_nbuf2;
    OSAPI_SLOT dskw_dfree         ; 0x0140   the ES:BX data needs nothing
    OSAPI_SLOT menu_win_set       ; 0x0148 - app menus (SPEC.md 12.2): in
                                  ;          BX = win ptr, SI = menu set
                                  ;          OFFSET in the caller's segment,
                                  ;          stored, never dereferenced here
                                  ;          (the set is copied kernel-side
                                  ;          at relayout) - no wrapper needed
    OSAPI_SLOT osapi_w_fdlg_open  ; 0x0150 - the Standard File dialog
                                  ;          (SPEC.md 38.6): in AL = mode,
                                  ;          BX = win, DI = callback OFFSET
                                  ;          in the caller's segment,
                                  ;          SI = default name, staged (or
                                  ;          0, passed through). The caller
                                  ;          holds the lock, so this shows
                                  ;          the window before it returns
    OSAPI_SLOT osapi_video        ; 0x0158 - runtime screen geometry
                                  ;          (SPEC.md 39.2): the screen is no
                                  ;          longer always 640x480, so the
                                  ;          SCREEN_* equs in os88api.inc are
                                  ;          a reference, not a promise
    OSAPI_SLOT inst_pkg_spawn     ; 0x0160 - package worker tasks (SPEC.md
    OSAPI_SLOT inst_pkg_alive     ; 0x0168   20.6): AX = entry, BX = own win
    OSAPI_SLOT wm_clip_set        ; 0x0170 - the clip region (SPEC.md 11.3):
    OSAPI_SLOT wm_clip_clear      ; 0x0178   what a worker may draw, in place
                                  ;          of the wm_obscured veto...
    OSAPI_SLOT wm_clip_test       ; 0x0180   ...and the whole-shape question,
                                  ;          for anything that erases a rect
                                  ;          and then draws glyphs into it
    OSAPI_SLOT cpu_info           ; 0x0188 - CPU tiers and memory above 1MB
    OSAPI_SLOT xm_caps            ; 0x0190   (SPEC.md 41): each body already
    OSAPI_SLOT xm_alloc           ; 0x0198   answers its SPEC.md 20.3 contract
    OSAPI_SLOT xm_free            ; 0x01A0   exactly, so the slots call
    OSAPI_SLOT xm_copy            ; 0x01A8   straight at them - no wrapper in
                                  ;          between, the dskw_* arrangement.
                                  ;          All five answer honestly on tier
                                  ;          0: CPU_8086, zero KB, and three
                                  ;          CF=1 refusals.
    OSAPI_SLOT wm_geom            ; 0x01B0 - new in v3 (SPEC.md 11/20.3):
                                  ;          in BX = win ptr; out CX/DX =
                                  ;          content w/h, CF=1 not visible -
                                  ;          the package-side replacement for
                                  ;          reading W_W/W_H/W_FLAGS out of a
                                  ;          record its DS can no longer reach
    OSAPI_SLOT cm_alloc           ; 0x01B8 - conventional memory (SPEC.md
    OSAPI_SLOT cm_free            ; 0x01C0   2.6/20.8): the same arena the
    OSAPI_SLOT cm_caps            ; 0x01C8   package's own region came out of,
                                  ;          asked for in paragraphs and given
                                  ;          back by base segment. Grants are
                                  ;          stamped with the calling instance
                                  ;          and force-freed at teardown, so a
                                  ;          package that never frees - and a
                                  ;          task-less one is never TOLD it is
                                  ;          closing - strands nothing. All
                                  ;          three answer honestly on the
                                  ;          256KB floor, where the arena is
                                  ;          empty: zeros, and a refusal.
    OSAPI_SLOT wm_resize          ; 0x01D0 - in BX = win ptr, CX/DX = frame
                                  ;          w/h; clamped (WMIN_* .. screen)
                                  ;          and re-positioned by wm_fit,
                                  ;          drawing nothing. The sanctioned
                                  ;          form of the record write a
                                  ;          content-sized app used to do by
                                  ;          hand (SPEC.md 11.1/20.3)
    OSAPI_SLOT gfx_blit4          ; 0x01D8 - packed 4bpp block (SPEC.md 5.4):
                                  ;          ES:SI = your pixels, BP = stride,
                                  ;          AX/BX = where, CX/DX = how big.
                                  ;          Run-coalesced INSIDE the kernel,
                                  ;          so a picture costs one far call
                                  ;          instead of one per run
    OSAPI_SLOT wm_about_set       ; 0x01E0 - in BX = win ptr, SI = your About
                                  ;          handler's offset (0 = none): the
                                  ;          opt-in that turns the app-name
                                  ;          label into a real bar menu
                                  ;          (SPEC.md 12.2)
osapi_table_end:                 ; 0x01E8

; build-time assertions: the table's start and span are ABI, prove them here
OSAPI_TABLE_OFF equ osapi_table - $$
OSAPI_TABLE_LEN equ osapi_table_end - osapi_table
%if OSAPI_TABLE_OFF != 0x0010
%error "os8088 API jump table must start at offset 0x0010"
%endif
%if OSAPI_TABLE_LEN != 59 * 8
%error "os8088 API jump table must be exactly 59 8-byte far slots"
%endif

; =============================================================================
; Boot (SPEC.md 15)
; =============================================================================
kmain:
    cli
    mov ax, KERNEL_SEG          ; the boot sector jumped here with its own
    mov ds, ax                  ; segments; setting ours up is our job
    mov es, ax
    mov ax, LOW_SEG             ; SS is NOT KERNEL_SEG (SPEC.md 2.1): every
    mov ss, ax                  ; task stack, task 0's included, lives in low
    mov sp, STK0_TOP            ; memory so the kernel window keeps the 16KB
    sti                         ; the stacks used to cost it
    cld

    call far_init               ; FIRST: the .fartext blob is sitting on top
                                ; of .bss until this moves it (SPEC.md 33)

    call cpu_detect             ; CPU tier + memory above 1MB (SPEC.md 41),
                                ; here and nowhere else: after far_init,
                                ; because xm_init writes .bss, and before
                                ; sched_init, because this is the last moment
                                ; at which no kernel ISR is installed - the
                                ; unreal-mode window inside xm_init runs with
                                ; CR0.PE set and a real-mode IVT, so the only
                                ; handlers that may fire in it are the BIOS's
                                ; own, and a tick lost here costs nothing
                                ; ([ticks] is zeroed by sched_init anyway).
    call cpu_a20_enable         ; ...and VERIFY it: the feature bit is set by
                                ; the wraparound probe, never by the poke
                                ; (SPEC.md 41.2). A no-op on tier 0 - an 8088
                                ; has no gate and port 0x92 belongs to
                                ; something else there.
    call xm_init                ; size the store (int 15h AH=88h, on task 0
                                ; per SPEC.md 7), claim the HMA, arm unreal
                                ; mode on tier 2, publish [xm_kb] LAST.

    call sched_init             ; pre-emption live from here on
    call evq_init
    call clk_init               ; system clock (SPEC.md 37): probe the RTC,
                                ; or fall back to the fixed date - before the
                                ; mode set, so the very first menu bar paint
                                ; already carries a valid clock
    call vid_init               ; video adapter (SPEC.md 39): probe, publish
                                ; the runtime geometry, set the mode. Re-runs
                                ; what the splash already did, which is what
                                ; wipes the loading screen.
    call bb_init                ; RAM probe + back buffer (SPEC.md 32): after
                                ; the mode set (VRAM just cleared, planes
                                ; start in sync), before the first drawing
    call font_init              ; needs int 10h, so after the mode is set
    call wm_init
    call menu_init              ; menu bar owner (SPEC.md 12): Locator, so
                                ; the first wm_paint_all already has a bar
    call inst_init              ; instance table (SPEC.md 29) - clean boot:
                                ; no app instances exist until launched
    call mouse_init             ; IRQ4 live; cursor stays hidden until shown
    call desk_init              ; count floppy drives for the desktop icons
    call dock_init              ; dock strip scratch (SPEC.md 30)
    call files_init             ; Disk module state (no window at boot)
    call loader_init            ; package loader state - also probes int 12h
                                ; for the arena top, on task 0 (SPEC.md 2.5)
    call cm_init                ; the arena's block table (SPEC.md 2.6): after
                                ; loader_init, whose [ld_arena_top] bounds it
    call tm_init                ; Task Manager total-RAM read (no window)
    call snd_init               ; sound layer (SPEC.md 34.7): saves the 61h
                                ; boot bits, stores its .bss state, publishes
                                ; snd_live LAST - snd_tick has been running
                                ; gated since sched_init hooked int 08h

    call gfx_lock
    call wm_paint_all
    call gfx_unlock
    call cursor_show

    jmp ui_task                 ; task 0 becomes the UI task; never returns

; =============================================================================
; osapi helpers (SPEC.md 20.4) - tiny accessors for loaded programs, reached
; only through the jump table. Each preserves all registers except its
; documented outputs.
; =============================================================================

; ---- osapi_get_ticks - out: AX = [ticks] -------------------------------------
osapi_get_ticks:
    mov ax, [ticks]
    ret

; ---- osapi_set_color - in: AL -> [gfx_color] ---------------------------------
osapi_set_color:
    mov [gfx_color], al
    ret

; ---- osapi_mouse - out: CX = [mouse_x], DX = [mouse_y], AL = [mouse_btn] -----
osapi_mouse:
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    mov al, [mouse_btn]
    ret

; ---- osapi_srand - in: AX -> [osapi_seed] -------------------------------------
osapi_srand:
    mov [osapi_seed], ax
    ret

; ---- osapi_rand - seed = seed*25173 + 13849; out: AX = new seed --------------
osapi_rand:
    push dx                     ; mul clobbers DX; only AX is an output
    mov ax, [osapi_seed]
    mov dx, 25173
    mul dx                      ; DX:AX = seed * 25173; keep the low word
    add ax, 13849
    mov [osapi_seed], ax
    pop dx
    ret

; ---- osapi_video - the screen the program actually got (SPEC.md 39.2) --------
; out: AX = width, BX = height, CX = first row the dock owns (so the usable
;      desktop is rows MBAR_H..CX-1), DL = 0 VGA / 1 Hercules / 2 CGA,
;      DH = bits per pixel, 4 or 1
osapi_video:
    mov ax, [vid_w]
    mov bx, [vid_h]
    mov cx, [vid_dock_y0]
    mov dl, [vid_kind]
    mov dh, 4
    cmp byte [vid_mono], 0
    je .r
    mov dh, 1
.r:
    ret

osapi_seed:  dw 0                ; PRNG state (inline data: .bss takes no init)

; -----------------------------------------------------------------------------
%include "viddet.inc"           ; video adapters (SPEC.md 39): the splash
                                ; probes and sets the mode on its first tick,
                                ; so this must be resident with it
%include "splash.inc"           ; must be resident within the image's opening
                                ; SPL_RESIDENT sectors (SPEC.md 15)
%include "farcall.inc"          ; far-code macros (SPEC.md 33): needed by
                                ; every module that lives in .fartext, so it
                                ; comes before all of them
%include "cpudet.inc"           ; CPU tiers + the A20 line (SPEC.md 41.1-41.3)
%include "xmem.inc"             ; memory above 1MB (SPEC.md 41.4/41.5): after
                                ; cpudet.inc, whose tier and feature bits it
                                ; reads. Both are AFTER splash.inc on purpose:
                                ; nothing here runs during the boot splash, so
                                ; neither may eat the SPL_RESIDENT window, and
                                ; being past it is what lets them use .bss.
%include "vga12.inc"
%include "vgabb.inc"
%include "font.inc"
%include "mouse.inc"
%include "sched.inc"
%include "events.inc"
%include "clock.inc"            ; the system clock (SPEC.md 37): after
                                ; sched.inc, whose [ticks] it advances from
%include "wm.inc"
%include "instance.inc"
%include "menu.inc"
%include "ui.inc"
%include "apps.inc"
%include "disk.inc"
%include "diskw.inc"          ; the FAT write path (SPEC.md 18.4): after
                                ; disk.inc, whose constants and layout it uses
%include "loader.inc"
%include "cmem.inc"             ; the arena's second claim map (SPEC.md 2.6/
                                ; 20.8): conventional memory a package asks
                                ; for and gives back. After instance.inc for
                                ; the record layout it enumerates, and beside
                                ; loader.inc because ld_alloc IS cm_fit now.
%include "files.inc"
%include "fdlg.inc"             ; the Standard File dialog (SPEC.md 38)
%include "icons.inc"
%include "desk.inc"
%include "dock.inc"
%include "taskmgr.inc"
%include "ctrl.inc"
%include "snd.inc"
%include "sndfm.inc"            ; the OPL2 driver (SPEC.md 34, Phase 3)
%include "sndsb.inc"            ; the Sound Blaster driver (SPEC.md 34, P4)

; =============================================================================
; osapi_w_* - the API marshalling wrappers (SPEC.md 20.3/20.4)
;
; A v3 caller's DS is its own segment, so every slot whose contract carries
; a DS-relative pointer points its table entry HERE instead of at the raw
; body. The shape is uniform:
;
;   push bp / mov bp, sp        ; on wrapper entry the stack is
;                               ;   [sp] = the slot stub's near return
;                               ;   [sp+2] = the CALLER's DS, saved by the
;                               ;            stub's opening `push ds`
;                               ; so after the frame, SS:[bp+4] = caller DS
;                               ; (BP-relative operands address SS, which
;                               ; IS the stack - SS = LOW_SEG on every task)
;   stage the operand           ; through ES = the caller's segment, into
;                               ; kernel scratch (.bss below), bounded and
;                               ; NUL-forced - a package string is hostile
;                               ; input and may not be terminated at all
;   call the raw routine        ; on a plain kernel DS pointer
;
; Kernel-internal callers never come through here: they near-call the raw
; routines with kernel pointers (font_str, wm_create and the dskw_* name
; parser are all heavily used from inside the kernel, which is exactly why
; the copies could not live in the bodies - SPEC.md 20.3). Every wrapper
; preserves all registers except the raw routine's documented outputs, and
; nothing after the raw call touches flags, so CF contracts (wm_create,
; the dskw_* five, fdlg_open, snd_fm, snd_stream) pass through.
;
; The single UI task serializes every caller (window callbacks and AM_ONCMD
; handlers are the only legal API contexts, SPEC.md 20.3), so one set of
; staging buffers suffices; a nested kernel repaint inside a raw routine
; never re-enters a wrapper, because kernel code does not call the table.
; =============================================================================

; -----------------------------------------------------------------------------
; osapi_copy_str - stage a NUL string across a segment boundary
; in:  ES:SI = source (hostile: may not be terminated), DI = kernel dest,
;      CX = cap in chars, NUL excluded (dest must hold CX+1 bytes)
; out: dest holds at most CX chars + a FORCED NUL; longer sources are
;      truncated at CX, never refused (SPEC.md 20.3); at most CX source
;      bytes are read
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
osapi_copy_str:
    push ax
    push cx
    push si
    push di
    jcxz .done
.copy:
    mov al, [es:si]
    inc si
    or al, al
    jz .done
    mov [di], al
    inc di
    loop .copy
.done:
    mov byte [di], 0            ; the forced NUL
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; osapi_copy_n - stage exactly CX bytes across a segment boundary
; in:  ES:SI = source, DI = kernel dest, CX = byte count (may be 0)
; out: the bytes copied
; clobbers: nothing (flags only)
;
; The fixed-length twin: templates, patches and icon bodies have no
; terminator to honour. Also the loader's header/icon re-stage (SPEC.md 21
; steps 6 and 9), whose source ES is the package segment.
; -----------------------------------------------------------------------------
osapi_copy_n:
    push ax
    push cx
    push si
    push di
    jcxz .done
.copy:
    mov al, [es:si]
    inc si
    mov [di], al
    inc di
    loop .copy
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; ---- font_str (slot 0x0068): SI string -> osapi_sbuf -------------------------
osapi_w_font_str:
    push bp
    mov bp, sp                  ; SS:[bp+4] = the caller's DS (see banner)
    push es
    push si
    push di
    push cx
    mov es, [bp+4]
    mov di, osapi_sbuf
    mov cx, OSAPI_STR_MAX
    call osapi_copy_str
    pop cx
    pop di
    mov si, osapi_sbuf
    call font_str
    pop si
    pop es
    pop bp
    ret

; ---- font_width (slot 0x0070): SI string -> osapi_wbuf; out AX ---------------
; Its OWN stage, never osapi_sbuf, and the stage + measure run with IF=0:
; WIDTH is legal WITHOUT the gfx lock (it draws nothing), so a worker may
; race the UI task - or another worker - right here. The private buffer
; keeps a lock-free WIDTH from clobbering a preempted FONT_STR stage (the
; lock serializes the DRAW slots, not this one), and the cli window keeps
; two concurrent WIDTHs from measuring each other's strings. Bounded:
; OSAPI_STR_MAX bytes copied + measured, well under a tick (SPEC.md 20.3).
osapi_w_font_width:
    push bp
    mov bp, sp
    push es
    push si
    push di
    push cx
    mov es, [bp+4]
    mov di, osapi_wbuf
    mov cx, OSAPI_STR_MAX
    pushf
    cli
    call osapi_copy_str
    mov si, osapi_wbuf
    call font_width             ; AX = the answer (a truncated stage answers
                                ; for at most OSAPI_STR_MAX chars, the same
                                ; cap every staged DRAW is under)
    popf                        ; IF back (font_width has no flag contract)
    pop cx
    pop di
    pop si
    pop es
    pop bp
    ret

; ---- wm_create (slot 0x0078): the 16-byte template, staged whole -------------
; The staged copy is what wm_create's rep movsw reads, so the record's
; title/paint/onkey/onclick words END UP as caller-segment offsets exactly
; as SPEC.md 11 defines them - the stage moves the template, not its
; meaning. Out: BX + CF, untouched by the pops.
; On success the new slot's wm_wseg is re-stamped from KERNEL_SEG to the
; CALLER's segment (SPEC.md 11): that stamp is what lets an unowned
; package window - one the loader has not (or never will have) bound -
; dispatch far at its creator instead of near at a kernel offset, and
; what the teardown sweep (wm_destroy_seg) matches.
osapi_w_wm_create:
    push bp
    mov bp, sp
    push es
    push si
    push di
    push cx
    mov es, [bp+4]
    mov di, osapi_tbuf
    mov cx, 16
    call osapi_copy_n
    pop cx
    pop di
    mov si, osapi_tbuf
    call wm_create              ; out: BX = window ptr, CF
    jc .done
    push ax
    push si
    call wm_ptr2idx             ; AL = window index (BX is wm_create's own
    mov si, ax                  ; answer: aligned by construction)
    shl si, 1
    mov ax, [bp+4]              ; the caller's segment
    mov [wm_wseg+si], ax
    pop si
    pop ax
    clc                         ; wm_create's success answer, restated (the
                                ; div/shl above left flags behind)
.done:
    pop si
    pop es
    pop bp
    ret

; ---- dskw_write (slot 0x0120): SI name staged; ES:BX data untouched ----------
; ES is BOTH things here: the caller's DS (fetched from the stack) for the
; name read, then the caller's own ES - saved first thing, restored from
; SS:[bp-2] - because ES:BX is the DATA argument and must arrive intact.
osapi_w_dskw_write:
    push bp
    mov bp, sp
    push es                     ; the caller's ES = the data segment: [bp-2]
    push si
    push di
    push cx
    mov es, [bp+4]
    mov di, osapi_nbuf
    mov cx, OSAPI_NAME_MAX
    call osapi_copy_str
    pop cx
    pop di
    mov es, [bp-2]              ; the data segment back (ES:BX contract)
    mov si, osapi_nbuf
    call dskw_write             ; out: CF + AX
    pop si
    pop es
    pop bp
    ret

; ---- dskw_read (slot 0x0128): same shape as dskw_write -----------------------
osapi_w_dskw_read:
    push bp
    mov bp, sp
    push es
    push si
    push di
    push cx
    mov es, [bp+4]
    mov di, osapi_nbuf
    mov cx, OSAPI_NAME_MAX
    call osapi_copy_str
    pop cx
    pop di
    mov es, [bp-2]              ; ES:BX = the caller's destination buffer
    mov si, osapi_nbuf
    call dskw_read              ; out: CF + AX
    pop si
    pop es
    pop bp
    ret

; ---- dskw_delete (slot 0x0130): SI name staged -------------------------------
osapi_w_dskw_delete:
    push bp
    mov bp, sp
    push es
    push si
    push di
    push cx
    mov es, [bp+4]
    mov di, osapi_nbuf
    mov cx, OSAPI_NAME_MAX
    call osapi_copy_str
    pop cx
    pop di
    mov si, osapi_nbuf
    call dskw_delete            ; out: CF + AX
    pop si
    pop es
    pop bp
    ret

; ---- dskw_rename (slot 0x0138): SI old + DI new, both staged -----------------
osapi_w_dskw_rename:
    push bp
    mov bp, sp
    push es
    push si
    push di
    push cx
    mov es, [bp+4]
    push di                     ; the caller's DI = the NEW name's offset
    mov di, osapi_nbuf
    mov cx, OSAPI_NAME_MAX
    call osapi_copy_str         ; old name -> osapi_nbuf
    pop si                      ; SI = the new name's caller offset
    mov di, osapi_nbuf2
    call osapi_copy_str         ; new name -> osapi_nbuf2 (CX preserved)
    pop cx
    mov si, osapi_nbuf
    mov di, osapi_nbuf2
    call dskw_rename            ; out: CF + AX
    pop di
    pop si
    pop es
    pop bp
    ret

; ---- fdlg_open (slot 0x0150): SI default name staged, 0 passed through -------
; DI is a completion OFFSET in the caller's segment - a value, stored by
; fdlg_open as-is (SPEC.md 38.6) - and needs nothing here.
osapi_w_fdlg_open:
    or si, si
    jnz .stage
    jmp fdlg_open               ; no default name: nothing to stage (the
                                ; tail-jmp keeps the stub's return)
.stage:
    push bp
    mov bp, sp
    push es
    push si
    push di
    push cx
    mov es, [bp+4]
    mov di, osapi_nbuf
    mov cx, OSAPI_NAME_MAX
    call osapi_copy_str
    pop cx
    pop di
    mov si, osapi_nbuf
    call fdlg_open              ; out: CF
    pop si
    pop es
    pop bp
    ret

; ---- osapi_snd_fm (slot 0x00F8): verb 2's 11-byte patch staged ---------------
osapi_w_snd_fm:
    cmp al, 2
    je .stage
    jmp osapi_snd_fm            ; only verb 2 carries a pointer: tail-jmp
                                ; keeps the stub's return for the rest
.stage:
    push bp
    mov bp, sp
    push es
    push si
    push di
    push cx
    mov es, [bp+4]
    mov di, osapi_fmbuf
    mov cx, 11
    call osapi_copy_n
    pop cx
    pop di
    mov si, osapi_fmbuf
    call osapi_snd_fm           ; out: CF
    pop si
    pop es
    pop bp
    ret

; ---- osapi_snd_stream (slot 0x0100): record the caller's segment -------------
; Verbs 5/6's staging copy IS the boundary (SPEC.md 20.3/34.6): sbl_v_read
; writes the caller's DI destination and sbl_v_stage reads the caller's SI
; source THROUGH [osapi_dseg] instead of DS. This store is the only writer,
; and those two verb bodies are the only readers - reachable exclusively
; through this wrapper, which is what stands in for a .bss hand-init
; (kernel-internal stream use is verbs 0-4/7, which carry no pointer;
; kernel code that ever needed 5/6 would set [osapi_dseg] itself first).
osapi_w_snd_stream:
    push bp
    mov bp, sp
    push ax
    mov ax, [bp+4]
    mov [osapi_dseg], ax        ; the caller's segment, for the verb bodies
    pop ax
    call osapi_snd_stream       ; out: CF + AX (pop bp touches neither)
    pop bp
    ret

section .bss
; the marshalling stages (SPEC.md 20.3/20.4) - written by the wrappers
; above (and osapi_dseg read by sndsb.inc's verb-5/6 bodies); every buffer
; is written before it is read on every path, so none needs a boot init
osapi_sbuf:  resb 64            ; OSAPI_STR_MAX chars + the forced NUL
osapi_wbuf:  resb 64            ; font_width's OWN stage (SPEC.md 20.3):
                                ; WIDTH is legal without the gfx lock, so
                                ; it may never share osapi_sbuf with a
                                ; lock-held FONT_STR in flight
osapi_nbuf:  resb 14            ; OSAPI_NAME_MAX chars + NUL (8.3 names)
osapi_nbuf2: resb 14            ; dskw_rename's second name
osapi_tbuf:  resb 16            ; wm_create's staged template
osapi_fmbuf: resb 11            ; snd_fm verb 2's staged patch
osapi_dseg:  resw 1             ; the segment snd_stream's caller passed its
                                ; verb-5/6 pointers in (see the wrapper)

section .text

; =============================================================================
; Size guards (SPEC.md 15.1). Same-section label differences bound via equ -
; a bare label in %if is a non-scalar and will not assemble, and a difference
; across two sections is not a constant at all, which is why each section
; measures itself against its own $$.
;
; kernel_text_end MUST be the last thing in .text: it is simultaneously the
; size of the image, the base of .bss and the landing address of the .fartext
; blob (see the section layout at the top of this file).
; =============================================================================
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

section .fartext
kernel_far_end:
KFAR_SIZE equ kernel_far_end - $$

section .lowbss
kernel_low_end:
KLOW_SIZE equ kernel_low_end - $$

section .bss
; modules declared their own .bss blocks; NASM accumulates them in order,
; so this lands last
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$

; What the kernel occupies outside its own segment, in KB, rounded up: the
; Task Manager's RAM figure adds it (SPEC.md 28). Forward-referenced from
; taskmgr.inc, which NASM resolves on a later pass exactly as it already
; does for the bare kernel_bss_end label.
KLOWFAR_KB equ (KLOW_SIZE + KFAR_SIZE + 1023) / 1024

; 1. image + bss must fit the kernel's 64KB segment. Guards 1 and 2 were
;    bounded by APP_LOAD_OFF (0xB000) until v3 evicted the package pool
;    (SPEC.md 20.1): the whole segment is the kernel's now, and the
;    reclaimed 19,968 bytes are what paid for the fatter far table, the
;    marshalling wrappers, the SPEC.md 12.2 menu copy and the inst_icons
;    cache. The 0x10000 bound is an assembly-time integer, not a 16-bit
;    immediate - the old "0x1FE00 unused" map row existed only for a
;    run-time comparison that died with ld_alloc's pool scan (SPEC.md 15.1).
%if KTEXT_SIZE + KBSS_SIZE > 0x10000
%error "kernel too big: image + bss must fit the 64KB segment"
%endif
; 2. the far blob lands at kernel_text_end and is only copied out once kmain
;    runs, so image + far must fit in the same window on the way in.
%if KTEXT_SIZE + KFAR_SIZE > 0x10000
%error "kernel too big: image + fartext must fit the 64KB segment"
%endif
; 3. .lowbss must leave task 0 at least 8KB of stack below STK0_TOP, and
;    LOW_SEG offsets can never reach LOW_LIMIT (that address is the kernel).
%if KLOW_SIZE > STK0_TOP - 8192
%error "lowbss too big: task 0's stack needs 8KB of clearance below STK0_TOP"
%endif
%if STK0_TOP >= LOW_LIMIT
%error "STK0_TOP must stay below LOW_LIMIT (LOW_SEG:LOW_LIMIT is the kernel)"
%endif
; 5. the far blob is copied to FAR_SEG (linear 0x00600) and must end below
;    FAT_SEG at linear 0x03000, where the FAT snapshot begins (SPEC.md 2.1):
;    0x00600 + 0x2A00 = 0x03000.
%if KFAR_SIZE > 0x2A00
%error "fartext blob would collide with FAT_SEG at linear 0x03000"
%endif
