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
;   1000:0010  os8088 API jump table (SPEC.md 20.3): 4-byte near-jmp slots at
;              pinned offsets, called by loaded programs (replaces the
;              retired syscall gate)
; =============================================================================

cpu 8086
bits 16
org 0x0000

; --- global constants (SPEC.md 3) -------------------------------------------
KERNEL_SEG  equ 0x1000
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

; loadable programs (SPEC.md 20) - the pool is its own address space now
PKG_PARA     equ 0x0F00         ; 61,440 bytes: the package pool, SPEC.md
                                ; 20.1. It was 19,968 bytes at the top of the
                                ; kernel's own segment, which is all that was
                                ; left there; a package now owns a SEGMENT,
                                ; links at org 0, and is loaded at a
                                ; paragraph boundary inside this block
APP_MAX_SIZE equ 0xF000         ; the biggest single package: the whole pool
PKG_DISP     equ 12             ; the dispatcher's fixed offset INSIDE the
                                ; .o88 header (SPEC.md 20.2): three bytes,
                                ; `call bp / retf`. Every kernel-to-package
                                ; call lands here with BP = the real target,
                                ; which is what lets every package callback
                                ; stay a near proc with a near `ret`

; =============================================================================
; The memory stack (SPEC.md 2). ONE ladder, bottom to top, and every rung is
; derived from the one below it: change a size and everything above it slides.
; There are no gaps and no fixed addresses above the kernel segment - what
; used to be four pinned constants up there (SND_SEG, SAVE_SEG, VIEW_SEG,
; BB_SEG) is now the claim heap of SPEC.md 42, handed out on demand.
;
;   0x00000  IVT + BIOS data area                     (theirs, 1,536 B)
;   0x00600  FAR_SEG   .fartext blob                  FAR_PARA paragraphs
;            FAT_SEG   mount-time FAT snapshot        FAT_PARA
;            LOW_SEG   .lowbss, then task 0's stack   up to KERNEL_SEG
;   0x10000  KERNEL_SEG  kernel image + .bss          KERN_MAX bytes
;            (the package pool rides in the same segment above the kernel
;             until SPEC.md 20's segment move; see APP_LOAD_OFF below)
;   0x20000  HEAP_SEG  the claim heap                 up to int 12h's top
; =============================================================================
FAR_SEG     equ 0x0060          ; linear 0x00600 - far code (section .fartext)
FAR_PARA    equ 0x02A0          ; 10,752 bytes; guard 5 fences the blob
FAT_SEG     equ FAR_SEG + FAR_PARA   ; mount-time FAT snapshot (SPEC.md 2.1/18),
                                ; reached via ES ONLY, never DS;
                                ; dsk_next_clus is the one reader
DSK_FAT_SECS equ 24             ; resident FAT cap, sectors (12,288 bytes).
                                ; Every geometry this OS builds fits: 360KB
                                ; = 2 sectors, 1.44MB = 9, and the 2.88MB
                                ; FAT16 test image = 23. It was 32 - four
                                ; more sectors than any self-consistent
                                ; volume can declare - and the difference
                                ; was 8KB of low memory nothing could reach
FAT_PARA    equ DSK_FAT_SECS * 32     ; 512 bytes = 32 paragraphs
LOW_SEG     equ FAT_SEG + FAT_PARA    ; .lowbss: task stacks + disk buffers
LOW_LIMIT   equ (KERNEL_SEG - LOW_SEG) * 16   ; LOW_SEG:LOW_LIMIT IS
                                ; KERNEL_SEG:0 - every LOW_SEG offset must
                                ; stay strictly below it
STK0_TOP    equ LOW_LIMIT - 2   ; task 0's stack top (grows down towards the
                                ; top of .lowbss; the assertion at the end of
                                ; this file keeps 8KB of clearance)

KERN_MAX    equ 0xB000          ; the kernel's own ceiling: image + .bss and
                                ; image + .fartext both stay below it (the
                                ; guards at the end of this file). 45,056
                                ; bytes, and the package pool starts there

; the file manager's per-window view cache (SPEC.md 2.3/22.1) - a heap claim
; per open Disk window now, not four pinned 4KB slots reserved from boot.
; What it buys is unchanged: a background file-manager window paints from
; memory, so wm_paint_all (no clip rect, on every window move) costs no
; floppy I/O. What changed is that a machine with no Disk window open pays
; nothing for it, and the Task Manager can bill the 3KB to the window.
VIEW_SLOTS  equ 4               ; max Disk windows = the kind's KD_CAP
VIEW_KB     equ 3               ; each cache: 1KB of entries + 2KB of icons

PKG_SEG     equ KERNEL_SEG + (KERN_MAX / 16)  ; the pool starts where the
                                ; kernel's own budget ends - no gap
HEAP_SEG    equ PKG_SEG + PKG_PARA    ; the claim heap (SPEC.md 42): the
                                ; paragraph after the pool, up to whatever
                                ; int 12h reports. Nothing up there has a
                                ; fixed address any more

; double buffering (SPEC.md 32) - the back buffer is a heap CLAIM now, so
; there is no BB_SEG constant: bb_init asks for BB_KB and remembers what it
; got, and on a machine that cannot fund it the Control Panel says so.
BB_PLANE_PARA equ 0x960         ; paragraphs per plane (0x9600 = 480 rows x 80)
BB_KB         equ 150           ; 4 planes x 0x9600 bytes, in KB

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
; os8088 API jump table (SPEC.md 20.3)
;
; Loaded programs FAR-call these pinned absolute offsets: 8-byte cells at
; 0x0010 + 8n, each one a complete DS switch around a near call into the
; kernel routine named in the comment. The slot order below IS the ABI -
; never reorder.
;
; Why 8 bytes and a DS switch. Since SPEC.md 20.1 a package lives in its OWN
; segment, so CS and DS are the package's on the way in and must be the
; KERNEL's while kernel code runs. `push cs / pop ds` is the two-byte way to
; say that (the table is .text, so CS is KERNEL_SEG here), and it clobbers no
; register - which matters, because every register in this ABI is an argument
; to something. The cell is exactly:
;
;       push ds / push cs / pop ds / call near target / pop ds / retf
;
; POP and RETF touch no flags, so a routine's CF answer survives the return
; (menu_win_set and inst_pkg_alive contractually preserve the FLAGS word).
;
; Three cells need more than that and jump to a stub below instead:
;   X  the caller's DS must reach the kernel routine (a package pointer it
;      has to dereference) - the stub puts it in ES
;   N  the caller passes a NUL name at DS:SI while ES:BX is already spoken
;      for by a data buffer - the stub STAGES the name into kernel scratch,
;      the dsk_get_dir idiom of SPEC.md 2.1
; =============================================================================
%macro OSAPI_SLOT 1                 ; 8 bytes exactly
    push ds
    push cs
    pop ds
    call %1
    pop ds
    retf
%endmacro

%macro OSAPI_JSLOT 1                ; a cell that defers to a longer stub
    jmp near %1                     ; E9 rel16 = 3 bytes
    times 5 db 0
%endmacro

osapi_table:
    OSAPI_SLOT gfx_lock           ; 0x0010
    OSAPI_SLOT gfx_unlock         ; 0x0018
    OSAPI_SLOT gfx_pixel          ; 0x0020
    OSAPI_SLOT gfx_hline          ; 0x0028
    OSAPI_SLOT gfx_vline          ; 0x0030
    OSAPI_SLOT gfx_fill           ; 0x0038
    OSAPI_SLOT gfx_frame          ; 0x0040
    OSAPI_SLOT gfx_fill_gray      ; 0x0048
    OSAPI_SLOT gfx_xor_rect       ; 0x0050
    OSAPI_SLOT gfx_xor_fill       ; 0x0058
    OSAPI_SLOT font_char          ; 0x0060
    OSAPI_JSLOT api_font_str      ; 0x0068  X: the string is package data
    OSAPI_JSLOT api_font_width    ; 0x0070  X
    OSAPI_JSLOT api_wm_create     ; 0x0078  X: so is the template
    OSAPI_SLOT wm_show            ; 0x0080
    OSAPI_SLOT wm_hide            ; 0x0088
    OSAPI_SLOT wm_front           ; 0x0090
    OSAPI_SLOT wm_content         ; 0x0098
    OSAPI_SLOT wm_obscured        ; 0x00A0
    OSAPI_SLOT task_yield         ; 0x00A8
    OSAPI_SLOT task_sleep         ; 0x00B0
    OSAPI_SLOT osapi_get_ticks    ; 0x00B8
    OSAPI_SLOT osapi_set_color    ; 0x00C0
    OSAPI_SLOT osapi_mouse        ; 0x00C8
    OSAPI_SLOT osapi_srand        ; 0x00D0
    OSAPI_SLOT osapi_rand         ; 0x00D8
    OSAPI_SLOT osapi_snd_caps     ; 0x00E0 - sound (SPEC.md 34): what the PC
    OSAPI_SLOT osapi_snd_tone     ; 0x00E8   speaker can do, a tone, and a
    OSAPI_SLOT osapi_snd_play     ; 0x00F0   clip out of the caller's buffer
    OSAPI_SLOT wm_sizable         ; 0x00F8 - window features (SPEC.md 11.1)
    OSAPI_SLOT wm_fullscreen      ; 0x0100 - fullscreen (SPEC.md 11.2)
    OSAPI_SLOT wm_grow_paint      ; 0x0108 - grow-box restore (SPEC.md 11.1)
    OSAPI_JSLOT api_file_write    ; 0x0110 - files (SPEC.md 18.4/20.3): N,
    OSAPI_JSLOT api_file_read     ; 0x0118   because ES:BX is the data buffer
    OSAPI_JSLOT api_file_delete   ; 0x0120   and the name still has to cross
    OSAPI_JSLOT api_file_rename   ; 0x0128   (two names, this one)
    OSAPI_SLOT dskw_dfree         ; 0x0130
    OSAPI_SLOT menu_win_set       ; 0x0138 - app menus (SPEC.md 12.2): the
                                  ;          set's segment comes from the
                                  ;          window, so no stub is needed
    OSAPI_JSLOT api_fdlg_open     ; 0x0140 - the Standard File dialog
                                  ;          (SPEC.md 38.6): N, for the
                                  ;          default name
    OSAPI_SLOT osapi_video        ; 0x0148 - runtime screen geometry (39.2)
    OSAPI_JSLOT api_pkg_spawn     ; 0x0150 - worker tasks (SPEC.md 20.6): X,
                                  ;          the ownership fence needs to
                                  ;          know which segment is calling
    OSAPI_SLOT inst_pkg_alive     ; 0x0158
    OSAPI_SLOT wm_clip_set        ; 0x0160 - the clip region (SPEC.md 11.3)
    OSAPI_SLOT wm_clip_clear      ; 0x0168
    OSAPI_SLOT wm_clip_test       ; 0x0170
    OSAPI_JSLOT api_mem_claim     ; 0x0178 - the claim heap (SPEC.md 42.3):
    OSAPI_JSLOT api_mem_free      ; 0x0180   X, same fence as the spawn
    OSAPI_SLOT osapi_mem_avail    ; 0x0188
    OSAPI_SLOT gfx_blit4          ; 0x0190 - packed 4bpp block (SPEC.md 5.4):
                                  ;          ES:SI = source, BP = stride,
                                  ;          AX/BX = dest, CX/DX = w/h. ES is
                                  ;          the caller's own choice here, so
                                  ;          no stub is needed
    OSAPI_SLOT wm_resize          ; 0x0198 - resize a window (SPEC.md 11.1):
                                  ;          BX = win, CX = w, DX = h; lock
                                  ;          held. Retires the last liberty
                                  ;          in docs/PAINT-NOTES.md - an app
                                  ;          writing W_W/W_H itself
    OSAPI_SLOT osapi_font_glyphs  ; 0x01A0 - the kernel's 8x8 glyph table
                                  ;          (SPEC.md 6): out SI = its offset
                                  ;          in KERNEL_SEG, AL = first code,
                                  ;          AH = last, CX = bytes per glyph
    OSAPI_SLOT wm_onsize          ; 0x01A8 - install the resize negotiator
                                  ;          (SPEC.md 11.1): BX = win, AX =
                                  ;          near proc. The other half of
                                  ;          docs/PAINT-NOTES.md's resize
                                  ;          complaint - wm_resize is the app
                                  ;          asking, this is the app answering
    OSAPI_SLOT osapi_file_here    ; 0x01B0 - where the file API's names
                                  ;          resolve (SPEC.md 18.4/19.2)
    OSAPI_SLOT osapi_file_goto    ; 0x01B8 - ...and how to put it back
osapi_table_end:                  ; 0x01C0

; build-time assertions: the table's start and span are ABI, prove them here
OSAPI_TABLE_OFF equ osapi_table - $$
OSAPI_TABLE_LEN equ osapi_table_end - osapi_table
%if OSAPI_TABLE_OFF != 0x0010
%error "os8088 API jump table must start at offset 0x0010"
%endif
%if OSAPI_TABLE_LEN != 54 * 8
%error "os8088 API jump table must be exactly 54 8-byte slots"
%endif

; =============================================================================
; The stubs the X and N cells jump to (SPEC.md 20.3). Each ends in retf and
; restores every segment register it borrowed.
; =============================================================================

; X: ES = the caller's DS, so the kernel routine can reach package data
%macro OSAPI_XSTUB 2
%1:
    push ds                     ; the caller's DS...
    push es                     ; ...and its ES
    push ds
    pop es                      ; ES = the caller's DS
    push cs
    pop ds                      ; DS = KERNEL_SEG
    call %2
    pop es
    pop ds
    retf
%endmacro

    OSAPI_XSTUB api_font_str,   font_str_x
    OSAPI_XSTUB api_font_width, font_width_x
    OSAPI_XSTUB api_wm_create,  wm_create
    OSAPI_XSTUB api_pkg_spawn,  inst_pkg_spawn
    OSAPI_XSTUB api_mem_claim,  osapi_mem_claim
    OSAPI_XSTUB api_mem_free,   osapi_mem_free

; N: the name at the caller's DS:SI is staged into kernel scratch first,
; because ES:BX belongs to the caller's data buffer and cannot carry it
%macro OSAPI_NSTUB 2
%1:
    push ds
    push si
    push di
    push es
    push cs
    pop es                      ; ES = KERNEL for the copy destination
    mov di, api_name
    call api_copyname           ; caller DS:SI -> ES:DI, at most 13 bytes
    pop es                      ; the caller's ES back: it is the buffer
    pop di                      ; and its DI, which fdlg_open needs as an
                                ; input (the completion proc's offset)
    push cs
    pop ds                      ; DS = KERNEL
    mov si, api_name
    call %2
    pop si
    pop ds
    retf
%endmacro

    OSAPI_NSTUB api_file_write,  dskw_write
    OSAPI_NSTUB api_file_read,   dskw_read
    OSAPI_NSTUB api_file_delete, dskw_delete
    OSAPI_NSTUB api_fdlg_open,   fdlg_open

; ...and the two-name case, which needs DI as well and so is written out
api_file_rename:
    push ds
    push si
    push di
    push es
    push cs
    pop es                      ; ES = KERNEL
    push di                     ; bank the new-name pointer across the first
    mov di, api_name            ; copy, which needs DI itself
    call api_copyname           ; old name
    pop si                      ; SI = the caller's DI = the new name
    mov di, api_name2
    call api_copyname           ; new name
    pop es
    push cs
    pop ds                      ; DS = KERNEL
    mov si, api_name
    mov di, api_name2
    call dskw_rename
    pop di
    pop si
    pop ds
    retf

; -----------------------------------------------------------------------------
; api_copyname - stage a NUL 8.3 name across the segment boundary
; in:  DS:SI = the caller's name, ES:DI = kernel scratch (13 bytes)
; out: nothing (all registers preserved); the copy is NUL-terminated even if
;      the source was not - a package cannot make this run on
; -----------------------------------------------------------------------------
api_copyname:
    push ax
    push cx
    push si
    push di
    cld
    mov cx, 13
.c:
    lodsb
    stosb
    or al, al
    jz .done
    loop .c
    mov byte [es:di-1], 0
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

api_name:   times 13 db 0       ; staged names (.text, not .bss: the file
api_name2:  times 13 db 0       ; slots are reachable before anything clears
                                ; .bss, and -f bin clears nothing)

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
    call mem_init               ; the claim heap (SPEC.md 42): int 12h, the
                                ; empty map. FIRST of the memory users -
                                ; every claim below goes through it
    call bb_init                ; back buffer (SPEC.md 32): can this ADAPTER
                                ; double-buffer? The memory question is asked
                                ; of the heap when the buffer is armed
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
    call loader_init            ; package loader state
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

; ---- osapi_font_glyphs - the kernel's own 8x8 font (SPEC.md 6/20.3) ---------
; out: SI = font_glyphs (an offset in KERNEL_SEG - read it through ES, which
;      is KERNEL_SEG on entry to every callback), AL = FONT_FIRST, AH =
;      FONT_LAST, CX = 8 bytes per glyph, row 0 first, bit 7 leftmost.
;
; For an app that draws text into its OWN pixels rather than onto the screen
; (apps/paint's text tool). Before this it re-probed the ROM font through
; int 10h and carried the kernel's F000:FA6E fallback - 40 lines to arrive
; at a table the kernel already had.
osapi_font_glyphs:
    mov si, font_glyphs
    mov al, FONT_FIRST
    mov ah, FONT_LAST
    mov cx, 8
    ret

; ---- osapi_file_here / osapi_file_goto - the volume's location (SPEC.md 19.2)
;
; The file API resolves every name in the volume's CURRENT directory, which is
; one global word shared by every window and by the Standard File dialog
; (SPEC.md 18.4/19.2). That is fine while a save is happening - the dialog
; leaves the volume in the folder the user picked, so the write lands there -
; and wrong the moment the app wants to write to the SAME PLACE again later,
; because any Disk window navigating anywhere has moved it since.
;
; So an app that means "where I saved last time" has to say so:
;
;   osapi_file_here   out DX = the current directory's first cluster (0 = the
;                     root), BL = the drive (0 = A:, 1 = B:). No disk I/O.
;   osapi_file_goto   in  DX = a cluster from osapi_file_here, BL = its drive;
;                     out CF=1 the volume could not be listed there and is
;                     back at the root with the write gate shut. This is a
;                     REMOUNT (SPEC.md 19.2) - real floppy I/O, UI-task
;                     context only, exactly like the other file slots.
;
; Storing the pair beside the file name is what makes an app's "Save" mean
; the same file its "Save As" chose.
; -----------------------------------------------------------------------------
osapi_file_here:
    mov dx, [dsk_cwd]
    mov bl, [disk_drive]
    ret

osapi_file_goto:
    push ax
    mov ax, dx
    mov dl, bl
    call dsk_chdir              ; CF = the mount failed; it has already put
    pop ax                      ; the volume back at the root
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
%include "vga12.inc"
%include "vgabb.inc"
%include "font.inc"
%include "mouse.inc"
%include "sched.inc"
%include "events.inc"
%include "clock.inc"            ; the system clock (SPEC.md 37): after
                                ; sched.inc, whose [ticks] it advances from
%include "wm.inc"
%include "memory.inc"           ; the claim heap (SPEC.md 42): after
                                ; instance.inc, whose records own the claims
%include "instance.inc"
%include "menu.inc"
%include "ui.inc"
%include "apps.inc"
%include "disk.inc"
%include "diskw.inc"          ; the FAT write path (SPEC.md 18.4): after
                                ; disk.inc, whose constants and layout it uses
%include "loader.inc"
%include "files.inc"
%include "fdlg.inc"             ; the Standard File dialog (SPEC.md 38)
%include "icons.inc"
%include "desk.inc"
%include "dock.inc"
%include "taskmgr.inc"
%include "ctrl.inc"
%include "snd.inc"              ; the sound layer (SPEC.md 34): PC speaker

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

; 1. image + bss must stay below KERN_MAX - the package pool starts there
;    (SPEC.md 20.1).
%if KTEXT_SIZE + KBSS_SIZE > KERN_MAX
%error "kernel too big: image + bss must stay below KERN_MAX"
%endif
; 2. the far blob lands at kernel_text_end and is only copied out once kmain
;    runs, so image + far must fit in the same window on the way in.
%if KTEXT_SIZE + KFAR_SIZE > KERN_MAX
%error "kernel too big: image + fartext must stay below KERN_MAX"
%endif
; 3. .lowbss must leave task 0 at least 8KB of stack below STK0_TOP, and
;    LOW_SEG offsets can never reach LOW_LIMIT (that address is the kernel).
%if KLOW_SIZE > STK0_TOP - 8192
%error "lowbss too big: task 0's stack needs 8KB of clearance below STK0_TOP"
%endif
%if STK0_TOP >= LOW_LIMIT
%error "STK0_TOP must stay below LOW_LIMIT (LOW_SEG:LOW_LIMIT is the kernel)"
%endif
; 5. the far blob is copied to FAR_SEG and must end below FAT_SEG, where the
;    FAT snapshot begins (SPEC.md 2.1). FAR_PARA is the reservation; this is
;    what makes growing it a one-constant edit that slides everything above.
%if KFAR_SIZE > FAR_PARA * 16
%error "fartext blob would collide with FAT_SEG - raise FAR_PARA"
%endif
; 6. the menu save-under (SPEC.md 2.2/12.4) must fit the claim menu_init
;    takes for it. gfx_save costs planes x rows x (byte span + 1); the two
;    clamps in menu.inc bound both factors, and this is where they are
;    checked against the buffer they write into.
%if 4 * (MENU_POPMAX*MENU_ITEM_H + 2) * (MENU_MAXW/8 + 2) > MENU_SAVE_KB*1024
%error "menu save-under can overflow its claim - lower MENU_POPMAX/MENU_MAXW"
%endif
; 7. low memory is a ladder with no gaps (SPEC.md 2.1): the FAT snapshot must
;    end at LOW_SEG exactly, and .lowbss + task 0's stack must end at the
;    kernel segment.
%if FAT_SEG + FAT_PARA != LOW_SEG
%error "low-memory ladder has a gap: FAT_PARA does not reach LOW_SEG"
%endif
