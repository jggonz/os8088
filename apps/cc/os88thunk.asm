; =============================================================================
; os8088 - apps/cc/os88thunk.asm
;
; The bridge between the C ABI and the os8088 API (SPEC.md 70.3). Every
; prototype in apps/cc/os88.h lands here.
;
; %include'd by apps/cc/crt0.asm, which has already emitted the section
; directives, `cpu 8086` and the package header; it is not assembled on its
; own and does not open a section of its own. Everything here is .text.
;
; -----------------------------------------------------------------------------
; WHY THIS FILE EXISTS
; -----------------------------------------------------------------------------
; An OSAPI slot is a FAR call with a REGISTER contract: `OSAPI_GFX_FILL` wants
; AX/BX/CX/DX = x1/y1/x2/y2 and `OSAPI_FONT_STR` wants SI = a string. A C
; caller has a cdecl STACK frame and no way to say "put this in DX". So each
; slot a C package can reach gets a routine here that reads the frame, loads
; the registers, makes the far call and turns the answer back into a C return
; value. Nothing else in the C half of the toolchain touches a register by
; name; get this file right and the rest is ordinary C.
;
; -----------------------------------------------------------------------------
; THE SEGMENT QUESTION, WHICH IS THE WHOLE OF THE DANGER (SPEC.md 1, 67.5)
; -----------------------------------------------------------------------------
; SS = LOW_SEG and DS = the package's own segment, always. THREE different
; kinds of address cross this file and each takes a different segment:
;
;   [bp+N]   THE ARGUMENTS, and they address SS with no override, which is
;            exactly right: `[bp+disp]` defaults to SS on an 8086 and the
;            arguments really are on the stack. This is the one file in the C
;            toolchain that writes a bp-relative operand on purpose - and it
;            is *reading a stack slot*, never taking its address.
;            tools/cc8086.py refuses a bp-relative `lea` in compiled code for
;            precisely that distinction (70.5); nothing below ever needs one.
;
;   [si], [bx+N], [label]   THE PACKAGE'S OWN DATA - a string the C passed, a
;            struct it wants filled, the runtime's own bss. DS-relative, no
;            override, because DS is ours.
;
;   [es:...] SOMEBODY ELSE'S SEGMENT, and there are exactly two:
;              * KERNEL_SEG, for the window record and for the name
;                OSAPI_ARG_FILE answers with. Those routines LOAD KERNEL_SEG
;                THEMSELVES rather than trust the ES a callback was entered
;                with, so they are correct from a worker, and after any other
;                call has clobbered ES.
;              * a heap claim's segment, for the _seg and peek/poke forms.
;            Where a slot wants ES:SI or ES:BX to mean OUR data, the thunk
;            does `push ds / pop es` first and says so on the line.
;
; Getting one of those wrong is the failure CLAUDE.md calls the worst kind: it
; assembles, it runs, and it reads or writes the wrong memory.
;
; -----------------------------------------------------------------------------
; THE THUNK CONTRACT - stated once, and every routine below obeys it
; -----------------------------------------------------------------------------
; in:   the cdecl frame. After the standard `push bp / mov bp, sp` the first
;       argument is at [bp+4] (near `call` return address at [bp+2], saved BP
;       at [bp+0]); further arguments follow at +6, +8, ... , pushed RIGHT TO
;       LEFT by the caller. The CALLER cleans (SPEC.md 70.3).
; out:  AX = the C return value. void functions return nothing and AX is junk.
;       A slot whose only answer is CF becomes `int`: 0 success, -1 refused.
; preserves: BP, DS, SS:SP and DF. NOTHING ELSE - SmallerC has no
;       callee-saved register (70.3), so AX, BX, CX, DX, SI, DI, ES and FLAGS
;       are all fair game and the compiler expects them to be gone. Where a
;       routine below pushes SI or DI it is for its own convenience.
; DF:   every OSAPI slot leaves it clear and nothing here sets it, so the
;       callback rule (DF clear on return, SPEC.md 20.2) is met by the
;       trampolines in crt0.asm without this file's help.
; flags:one detail matters and recurs: `mov ax, 0` is used instead of
;       `xor ax, ax` wherever a CF returned by a slot is tested AFTERWARDS,
;       because MOV writes no flags and XOR clears CF.
;
; The register contract of the slot itself is os88api.inc's, and the comment
; on each routine below names it rather than repeating its prose.
; =============================================================================

; -----------------------------------------------------------------------------
; The five shapes that repeat, as macros. A macro is used only where the whole
; body is mechanical - load registers from the frame, call, return - so that
; the interesting routines further down stand out as interesting. Each
; invocation still carries its own contract comment; the macro supplies the
; three or four instructions, never the reason.
; -----------------------------------------------------------------------------

; CC_T_VOID label, slot            void f(void)
%macro CC_T_VOID 2
%1:
    call %2
    ret
%endmacro

; CC_T_AX label, slot              int f(void), answered in AX by the slot
%macro CC_T_AX 2
%1:
    call %2
    ret
%endmacro

; CC_T_REG label, slot, reg        int f(void), answered in `reg`
%macro CC_T_REG 3
%1:
    call %2
    mov ax, %3
    ret
%endmacro

; CC_T_A1 label, slot, reg         void f(int a) -> reg = a
%macro CC_T_A1 3
%1:
    push bp
    mov bp, sp
    mov %3, [bp+4]
    call %2
    pop bp
    ret
%endmacro

; CC_T_WIN label, slot             void f(void *win) -> BX = win
%macro CC_T_WIN 2
%1:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    call %2
    pop bp
    ret
%endmacro

; CC_T_WINF label, slot            void f(void *win, int on) -> BX, AL
%macro CC_T_WINF 2
%1:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, [bp+6]
    or ah, al                       ; any non-zero word means "set", and the
    mov al, ah                      ; slot reads AL only: fold the high half
    call %2                         ; in so os88_wm_snap(w, 256) is not "off"
    pop bp
    ret
%endmacro

; CC_T_RECT label, slot            void f(x1,y1,x2,y2) -> AX,BX,CX,DX
%macro CC_T_RECT 2
%1:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call %2
    pop bp
    ret
%endmacro

; =============================================================================
; DRAWING (SPEC.md 5). Absolute screen coordinates, inclusive rectangles, gfx
; lock held by the caller. ~756 us each on the target machine whatever they
; draw, so what costs is the COUNT of these calls (PERFORMANCE.md).
; =============================================================================

; void os88_gfx_lock(void) / os88_gfx_unlock(void) - the drawing mutex plus
; the cursor (SPEC.md 7). A window callback is already inside one and must
; take neither; a worker takes it for a short burst of drawing and releases it
; (20.6 rule 3). NOT REENTRANT: os88_task_alive() under the lock deadlocks the
; machine.
CC_T_VOID _os88_gfx_lock,      OSAPI_GFX_LOCK
CC_T_VOID _os88_gfx_unlock,    OSAPI_GFX_UNLOCK

; void os88_set_color(int c) - AL = [gfx_color], ONE pen for the machine.
CC_T_A1   _os88_set_color,     OSAPI_SET_COLOR, al

; void os88_gfx_pixel(int x, int y) - CX = x, DX = y.
_os88_gfx_pixel:
    push bp
    mov bp, sp
    mov cx, [bp+4]
    mov dx, [bp+6]
    call OSAPI_GFX_PIXEL
    pop bp
    ret

; void os88_gfx_hline(int x1, int x2, int y) - AX = x1, BX = x2, DX = y.
_os88_gfx_hline:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_GFX_HLINE
    pop bp
    ret

; void os88_gfx_vline(int x, int y1, int y2) - AX = x, BX = y1, DX = y2.
_os88_gfx_vline:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_GFX_VLINE
    pop bp
    ret

; The five rectangle primitives - AX/BX/CX/DX = x1/y1/x2/y2 (SPEC.md 5).
CC_T_RECT _os88_gfx_fill,      OSAPI_GFX_FILL
CC_T_RECT _os88_gfx_frame,     OSAPI_GFX_FRAME
CC_T_RECT _os88_gfx_fill_gray, OSAPI_GFX_FILL_GRAY
CC_T_RECT _os88_gfx_xor_rect,  OSAPI_GFX_XOR_RECT
CC_T_RECT _os88_gfx_xor_fill,  OSAPI_GFX_XOR_FILL

; void os88_gfx_line(int x1,int y1,int x2,int y2,int dilate) - SPEC.md 5.6.
; SI = 0 thin / 1 dilated by a pixel either side of the minor axis, which is
; what an erase owes a line that was DRAWN in per-frame segments (5.6.5).
_os88_gfx_line:
    push bp
    mov bp, sp
    push si
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    mov si, [bp+12]
    call OSAPI_GFX_LINE
    pop si
    pop bp
    ret

; void os88_gfx_fill_pat(int x1,int y1,int x2,int y2,const void *pat8)
; SI = 8 pattern bytes IN OUR SEGMENT (SPEC.md 20.9) - DS-relative, so the C
; passes a plain pointer to a static and there is no segment to think about.
_os88_gfx_fill_pat:
    push bp
    mov bp, sp
    push si
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    mov si, [bp+12]                 ; ours: DS, no override
    call OSAPI_GFX_FILL_PAT
    pop si
    pop bp
    ret

; void os88_gfx_pen(int disabled) - SPEC.md 47. CF IS THE ARGUMENT, which is
; the one slot whose input is a flag rather than a register: every greying
; predicate in this OS already answers in CF, so the call site is a test and
; then this. Non-zero = disabled (CDGRAY plus the [gfx_dis] flag, which is the
; whole difference on the two 1bpp adapters).
_os88_gfx_pen:
    push bp
    mov bp, sp
    cmp word [bp+4], 0
    je .live
    stc                             ; disabled
    jmp short .go
.live:
    clc
.go:
    call OSAPI_GFX_PEN              ; the cell preserves every register AND
    pop bp                          ; every flag, so CF crosses it unchanged
    ret

; int os88_gfx_scroll(int x1,int y1,int x2,int y2,int dy) - SPEC.md 5.5.
; SI = signed dy, positive scrolls the content UP. Returns -1 refused, and
; then NOTHING moved: fall back to a full repaint.
_os88_gfx_scroll:
    push bp
    mov bp, sp
    push si
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    mov si, [bp+12]
    call OSAPI_GFX_SCROLL
    mov ax, 0                       ; MOV: the answer is in CF
    jnc .ok
    dec ax
.ok:
    pop si
    pop bp
    ret

; int os88_gfx_blit1(const void *bits,int stride,int x,int y,int w,int rows)
; ES:SI = the 1bpp band, BP = its stride in bytes, AX/BX = x/y, CX = width,
; DX = rows (SPEC.md 5.4.2). Returns -1 REFUSED - x or w not a multiple of 8,
; rows outside 1..255, or a kern_small machine - and then nothing was drawn.
;
; BP IS AN ARGUMENT TO THE SLOT, and BP is also this file's frame pointer, so
; the stride is read out of the frame FIRST, into DI, and the frame pointer is
; parked on the stack across the call. Reading it after `mov bp, di` would
; read the band's stride as a frame offset - the kind of mistake that shows up
; as a garbled blit and nothing else.
_os88_gfx_blit1:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+4]                  ; the band, an offset in OUR segment...
    mov di, [bp+6]                  ; ...its stride, parked until BP is free
    mov ax, [bp+8]
    mov bx, [bp+10]
    mov cx, [bp+12]
    mov dx, [bp+14]
    push ds
    pop es                          ; ES:SI - the band is ours, so ES = DS
    push bp                         ; the frame, back in a moment
    mov bp, di                      ; BP = stride, as the slot wants
    call OSAPI_GFX_BLIT1
    pop bp                          ; frame pointer restored before any [bp+N]
    mov ax, 0                       ; MOV: CF is the answer
    jnc .ok
    dec ax
.ok:
    pop di
    pop si
    pop bp
    ret

; void os88_gfx_blit4(const void *pix,int stride,int x,int y,int w,int h)
; ES:SI = packed 4bpp pixels (two per byte, high nibble leftmost), BP =
; stride, AX/BX = x/y, CX/DX = w/h. Same BP dance as above.
_os88_gfx_blit4:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+4]
    mov di, [bp+6]
    mov ax, [bp+8]
    mov bx, [bp+10]
    mov cx, [bp+12]
    mov dx, [bp+14]
    push ds
    pop es                          ; ES:SI - ours
    push bp
    mov bp, di
    call OSAPI_GFX_BLIT4
    pop bp
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; TEXT (SPEC.md 6). One 8x8 cell is ~900 us; a 78-cell row is ~71 ms.
; =============================================================================

; void os88_font_char(int x, int y, int ch) - CX = x, DX = y, AL = char.
_os88_font_char:
    push bp
    mov bp, sp
    mov cx, [bp+4]
    mov dx, [bp+6]
    mov ax, [bp+8]
    call OSAPI_FONT_CHAR
    pop bp
    ret

; void os88_font_str(int x, int y, const char *s) - CX, DX, SI = NUL string
; in our segment.
_os88_font_str:
    push bp
    mov bp, sp
    push si
    mov cx, [bp+4]
    mov dx, [bp+6]
    mov si, [bp+8]                  ; ours: DS
    call OSAPI_FONT_STR
    pop si
    pop bp
    ret

; int os88_font_width(const char *s) - SI = string; out AX = pixel width.
_os88_font_width:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    call OSAPI_FONT_WIDTH
    pop si
    pop bp
    ret

; void os88_font_run(int x,int y,const char *s,int ink,int paper) - SPEC.md
; 6.1: the cells' background and their glyphs in ONE pass, so the line is
; never momentarily blank and a background painter needs no clip test around
; it. CX = x, DX = y, SI = string, AL = ink, AH = paper.
_os88_font_run:
    push bp
    mov bp, sp
    push si
    mov cx, [bp+4]
    mov dx, [bp+6]
    mov si, [bp+8]
    mov ax, [bp+12]                 ; paper first: it goes in the HIGH half
    mov ah, al
    mov al, [bp+10]                 ; ...then ink over the low half
    call OSAPI_FONT_RUN
    pop si
    pop bp
    ret

; =============================================================================
; WINDOWS (SPEC.md 11)
; =============================================================================

; void *os88_wm_create(int x,int y,int w,int h,const char *title)
; SI = a 16-byte template (WT_*). The template is crt0.asm's `cc_tpl`, whose
; paint/onkey/onclick words are the trampolines the shim asked for and are
; already in place; this fills the five words that are the C's business.
; Returns 0 when the window table is full, which os88_main() returns straight
; on as its abort (crt0 turns a 0 into the CF the loader is owed).
_os88_wm_create:
    push bp
    mov bp, sp
    push si
    mov ax, [bp+4]
    mov [cc_tpl+WT_X], ax
    mov ax, [bp+6]
    mov [cc_tpl+WT_Y], ax
    mov ax, [bp+8]
    mov [cc_tpl+WT_W], ax
    mov ax, [bp+10]
    mov [cc_tpl+WT_H], ax
    mov ax, [bp+12]                 ; the title: an offset in our segment, and
    mov [cc_tpl+WT_TITLE], ax       ; the RECORD KEEPS THE POINTER, not the
                                    ; bytes - so it must outlive the window
    mov si, cc_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF = table full
    mov ax, bx
    jnc .ok
    mov ax, 0                       ; 0 = "no window", the C's abort value
.ok:
    pop si
    pop bp
    ret

CC_T_WIN  _os88_wm_show,       OSAPI_WM_SHOW
CC_T_WIN  _os88_wm_hide,       OSAPI_WM_HIDE
CC_T_WIN  _os88_wm_front,      OSAPI_WM_FRONT
CC_T_WIN  _os88_wm_grow,       OSAPI_WM_GROW      ; a resizable window's own
                                                  ; repaint must END with this
CC_T_WIN  _os88_wm_destroy,    OSAPI_WM_DESTROY   ; a SECOND window of yours;
                                                  ; your own dies with the
                                                  ; instance (SPEC.md 38.1)

; The flag setters - BX = win, AL = 0 clear / non-zero set.
CC_T_WINF _os88_wm_sizable,    OSAPI_WM_SIZABLE
CC_T_WINF _os88_wm_snap,       OSAPI_WM_SNAP
CC_T_WINF _os88_wm_keeph,      OSAPI_WM_KEEPH
CC_T_WINF _os88_wm_ownbg,      OSAPI_WM_OWNBG
CC_T_WINF _os88_wm_saveu,      OSAPI_WM_SAVEU

; int os88_wm_obscured(void *win) - 1 = covered, hidden included (11.3.1).
_os88_wm_obscured:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    call OSAPI_WM_OBSCURED
    mov ax, 0
    jnc .vis
    inc ax
.vis:
    pop bp
    ret

; void os88_wm_content(void *win, struct os88_pt *origin)
; BX = win; out AX = content left, DX = content top. The struct is OURS, so
; the two stores are DS-relative. BX is reloaded AFTER the call because it was
; the input.
_os88_wm_content:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    call OSAPI_WM_CONTENT
    mov bx, [bp+6]
    mov [bx], ax
    mov [bx+2], dx
    pop bp
    ret

; int os88_wm_geom(void *win, struct os88_size *sz) - BX = win; out CX/DX =
; content width/height, CF = not visible (-1). Prefer this to arithmetic on
; W_W/W_H: it is the same subtraction, done once, by the kernel.
_os88_wm_geom:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    call OSAPI_WM_GEOM
    mov bx, [bp+6]                  ; MOV only from here to the JNC - none of
    mov [bx], cx                    ; these writes a flag
    mov [bx+2], dx
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop bp
    ret

; int os88_wm_damage(void *win, struct os88_rect *r) - from inside your own
; os88_paint(). Returns 1 = draw the WHOLE content, 0 = draw exactly r, which
; may be EMPTY (x1 > x2) meaning draw nothing. Requires os88_wm_ownbg(win, 1);
; without it the kernel has already whitened your content and "whole" is the
; only correct answer (11.90.2). BX is both an input and an output word of the
; rect, so the destination is parked in DI.
_os88_wm_damage:
    push bp
    mov bp, sp
    push di
    mov di, [bp+6]                  ; the rect, ours
    mov bx, [bp+4]
    call OSAPI_WM_DAMAGE            ; CF=1 whole; AX/BX/CX/DX = the rect
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    mov [di+6], dx
    mov ax, 0
    jnc .part
    inc ax
.part:
    pop di
    pop bp
    ret

; void os88_wm_title(void *win, const char *s) - BX = win, AX = a NUL string
; in OUR segment (or 0 = "the bytes W_TITLE already names changed"). Redraws
; the caption strip and nothing else: 17 rows, no W_PAINT.
_os88_wm_title:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, [bp+6]
    call OSAPI_WM_TITLE
    pop bp
    ret

; void os88_wm_resize(void *win, int w, int h) - the WHOLE window, frame
; included. Do NOT hold the gfx lock. Call this instead of writing W_W/W_H:
; the fields are readable by contract and not writable.
_os88_wm_resize:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_WM_RESIZE
    pop bp
    ret

; void os88_wm_cursor(void *win, int shape) - BX = win, AL = OS88_CUR_*.
; Set it once at creation; the pointer wears it over your CONTENT only, and it
; dies with the record. Not CC_T_WINF: that macro folds any non-zero argument
; to 1 because a FLAG has two states, and this argument is an enumeration that
; will grow a third shape one day.
_os88_wm_cursor:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, [bp+6]
    call OSAPI_WM_CURSOR
    pop bp
    ret

; The clip region (SPEC.md 11.3). It dies at the next os88_gfx_unlock().
_os88_wm_clip_set:                  ; int os88_wm_clip_set(void *win)
    push bp
    mov bp, sp
    mov bx, [bp+4]
    call OSAPI_WM_CLIP_SET
    mov ax, 0                       ; CF=1: not one pixel of your content
    jnc .ok                         ; shows - draw nothing this frame
    dec ax
.ok:
    pop bp
    ret

CC_T_VOID _os88_wm_clip_clear, OSAPI_WM_CLIP_CLEAR

; int os88_wm_clip_test(x1,y1,x2,y2) - 0 = the whole rect is drawable (also
; when no clip is armed), -1 = it is not. Ask before you ERASE a rect you are
; about to letter: the fills clip per pixel and font_char clips per whole 8x8
; cell, so a wide erase can white out a band the glyphs then refuse to repaint.
_os88_wm_clip_test:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call OSAPI_WM_CLIP_TEST
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop bp
    ret

CC_T_REG  _os88_wm_top,        OSAPI_WM_TOP,     bx   ; frontmost VISIBLE
CC_T_REG  _os88_menu_owner,    OSAPI_MENU_OWNER, bx   ; who owns the BAR

; -----------------------------------------------------------------------------
; Reading the window record (SPEC.md 11, 20.5)
;
; THE RECORD LIVES IN KERNEL_SEG, and C cannot write a segment override, so
; these six do it. Each loads KERNEL_SEG itself instead of using the ES the
; callback was entered with: ES is only KERNEL_SEG until the first thunk that
; needs ES for something else (any _seg form, os88_toast, os88_gfx_blit1),
; and a worker never had it at all. Two extra instructions buys a routine
; that is correct in every context, which is the trade os88api.inc's own
; warning - "without the override you are reading your own image at that
; offset, which assembles cleanly and runs wrong" - is about.
;
; in:  [bp+4] = the window pointer os88_wm_create() returned
; out: AX = the field. Clobbers BX and ES.
; -----------------------------------------------------------------------------
%macro CC_T_WFIELD 2                ; label, W_* offset
%1:
    push bp
    mov bp, sp
    mov bx, [bp+4]                  ; the record's OFFSET, from our stack
    mov ax, KERNEL_SEG              ; ...and its SEGMENT, which is never ours
    mov es, ax
    mov ax, [es:bx+%2]
    pop bp
    ret
%endmacro

CC_T_WFIELD _os88_win_flags, W_FLAGS
CC_T_WFIELD _os88_win_x,     W_X
CC_T_WFIELD _os88_win_y,     W_Y
CC_T_WFIELD _os88_win_w,     W_W    ; the FRAME width - os88_wm_geom() is the
CC_T_WFIELD _os88_win_h,     W_H    ; content one

; int os88_win_visible(void *win) - W_FLAGS bit 1. What a worker re-checks
; under the lock before drawing (SPEC.md 20.6 rule 5), together with
; os88_wm_clip_set().
_os88_win_visible:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, KERNEL_SEG
    mov es, ax
    mov ax, [es:bx+W_FLAGS]
    and ax, 2
    pop bp
    ret

; -----------------------------------------------------------------------------
; Menus, About, and the two installable callbacks (SPEC.md 12.2, 13.7, 11.98)
;
; Each of these hands the kernel the address of a TRAMPOLINE in crt0.asm, so
; each is assembled only when the shim asked for that trampoline. A C package
; that calls one without the %define gets nasm's external-reference error
; naming the function, which is the failure naming its own cause (70.1).
; -----------------------------------------------------------------------------

%ifdef CC_HAS_MENUS
; void os88_menu_set(void *win, struct os88_menuset *set) - BX = win, SI = the
; set (0 = detach). AM_ONCMD is written here rather than by the C, because the
; C cannot name an assembly label: the set must therefore be WRITABLE, which
; means declared without `const` so it lands in .data.
_os88_menu_set:
    push bp
    mov bp, sp
    push si
    mov bx, [bp+4]
    mov si, [bp+6]
    or si, si
    jz .go
    mov word [si+AM_ONCMD], cc_oncmd   ; the set is OURS: DS-relative
.go:
    call OSAPI_MENU_SET             ; draws nothing, takes no lock, and
    pop si                          ; preserves the FLAGS - so it is safe
    pop bp                          ; between wm_create and the CF the entry
    ret                             ; proc owes the loader
%endif

%ifdef CC_HAS_ABOUT
; void os88_about_set(void *win) - BX = win, SI = the handler. Adds
; 'About <Name>' above the 'Close' the kernel already puts in your pull-down.
_os88_about_set:
    push bp
    mov bp, sp
    push si
    mov bx, [bp+4]
    mov si, cc_about
    call OSAPI_ABOUT_SET
    pop si
    pop bp
    ret
%endif

%ifdef CC_HAS_ONMOUSEUP
; void os88_wm_onmouseup(void *win) - BX = win, AX = the near proc.
_os88_wm_onmouseup:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, cc_onmouseup
    call OSAPI_WM_ONMOUSEUP
    pop bp
    ret
%endif

%ifdef CC_HAS_ONRESIZE
; void os88_wm_onresize(void *win) - BX = win, AX = the near proc.
_os88_wm_onresize:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, cc_onresize
    call OSAPI_WM_ONRESIZE
    pop bp
    ret
%endif

%ifdef CC_HAS_ONWAKE
; void os88_wm_onwake(void *win) - BX = win, AX = the near proc (SPEC.md 71.1).
_os88_wm_onwake:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, cc_onwake
    call OSAPI_WM_ONWAKE
    pop bp
    ret

; int os88_wm_wake(void *win) - BX = win (SPEC.md 71.1). Post the kick; the
; handler above runs on the UI task, lock free, some passes later. Any
; context. 0 = a wake is queued for this window (now or already), -1 = the
; 16-record ring was full of other events and nothing was posted - kick again
; from your next callback. Under CC_HAS_ONWAKE with the installer: a wake
; with no handler dispatches to nothing, so a package without one has no use
; for it and does not pay its bytes.
_os88_wm_wake:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    call OSAPI_WM_WAKE
    mov ax, 0                       ; MOV: CF is the answer
    jnc .ok
    dec ax
.ok:
    pop bp
    ret
%endif

; int os88_fullscreen(void *win, int enter) - BX = win, AL = 1 enter / 0 exit
; (SPEC.md 11.2 - the WF_FULL latch, NOT the 53 exclusive bracket). The caller
; holds the gfx lock, which a window callback does. 0 = done, -1 = enter
; refused (another window owns the screen). Exiting is the app's job - the
; menu bar is unreachable while it holds the screen - and SPEC.md 11.2.1's
; F/Esc binding is the convention, with 71.2's stated exception for a
; terminal that owns both keys. Every window callback runs with the lock
; already held, so the fold below is the same one CC_T_WINF does: any non-zero
; word means enter.
_os88_fullscreen:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, [bp+6]
    or ah, al
    mov al, ah
    call OSAPI_FULLSCREEN
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop bp
    ret

; =============================================================================
; TASKS AND TIME (SPEC.md 8, 20.6)
; =============================================================================

CC_T_VOID _os88_task_yield,    OSAPI_TASK_YIELD
CC_T_A1   _os88_task_sleep,    OSAPI_TASK_SLEEP, ax   ; AX = ticks, 18 ~ 1s
CC_T_AX   _os88_ticks,         OSAPI_GET_TICKS
CC_T_AX   _os88_boot_ticks,    OSAPI_BOOT_TICKS       ; 0xFFFF = never stamped
CC_T_WIN  _os88_task_alive,    OSAPI_TASK_ALIVE       ; from the WORKER only,
                                                      ; lock NOT held; never
                                                      ; returns once closed

%ifdef CC_HAS_WORKER
; int os88_task_spawn(void *win) - AX = the worker's near entry (crt0's
; trampoline), BX = your window. From a callback with the gfx lock HELD; not
; from os88_main(), where the instance is not published yet and the kernel
; refuses. -1 = refused, which is NORMAL and transient: degrade and retry.
_os88_task_spawn:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, cc_worker
    call OSAPI_TASK_SPAWN
    mov ax, 0                       ; MOV: CF is the answer, and AL came back
    jnc .ok                         ; holding the task slot
    dec ax
.ok:
    pop bp
    ret
%endif

; =============================================================================
; INPUT, VIDEO, THE MACHINE
; =============================================================================

; void os88_mouse(struct os88_mouse *m) - out CX = x, DX = y, AL = buttons.
_os88_mouse:
    push bp
    mov bp, sp
    push di
    mov di, [bp+4]
    call OSAPI_MOUSE
    mov [di], cx
    mov [di+2], dx
    mov ah, 0                       ; AL is a byte; the struct field is a word
    mov [di+4], ax
    pop di
    pop bp
    ret

CC_T_AX   _os88_evq_pending,   OSAPI_EVQ_PENDING
CC_T_A1   _os88_srand,         OSAPI_SRAND, ax
CC_T_AX   _os88_rand,          OSAPI_RAND

; void os88_video(struct os88_video *v) - SPEC.md 39.2. AX = width, BX =
; height, CX = the first row the DOCK owns, DL = VID_*, DH = bits per pixel.
; ASK THIS: SCREEN_W/SCREEN_H are a reference, not a promise, and anything
; anchored to a screen edge is wrong on two adapters of three (SPEC.md 39).
_os88_video:
    push bp
    mov bp, sp
    push di
    mov di, [bp+4]
    call OSAPI_VIDEO
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    mov al, dl                      ; the two bytes become two words, because
    mov ah, 0                       ; a C `int` is what the struct declares
    mov [di+6], ax
    mov al, dh
    mov ah, 0
    mov [di+8], ax
    pop di
    pop bp
    ret

; int os88_cpu(void) - OS88_CPU_8086 / _286 / _386 (SPEC.md 41). A FACT the
; code can test rather than a guess about speed (PERFORMANCE.md rule 7).
_os88_cpu:
    call OSAPI_CPU_INFO             ; AL = tier, AH = feature bits
    mov ah, 0
    ret

; =============================================================================
; SOUND - the resident PC speaker only (SPEC.md 34)
; =============================================================================

CC_T_AX   _os88_snd_caps,      OSAPI_SND_CAPS         ; SND_CAP_* bits

; int os88_snd_tone(int hz, int ticks, int prio) - AX = Hz (0 = off), CX =
; duration in ticks (0 = until off), DL = priority. Worker-safe by
; construction (34.3): the grant is stamped with the running task's instance,
; and a tone with a DURATION silences itself, so a worker need not come back.
_os88_snd_tone:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_SND_TONE
    mov ax, 0                       ; AL came back as the owner generation,
    jnc .ok                         ; which a C caller has no use for
    dec ax
.ok:
    pop bp
    ret

; =============================================================================
; FILES (SPEC.md 18.4, 19)
;
; UI-TASK CONTEXT ONLY - never from your worker; the kernel cannot enforce it
; and a second concurrent caller corrupts the FAT. Every routine in this
; section writes [cc_ferr], which os88_ferr() reads back.
;
; The count is DX:CX, 32 bits. These pass DX = 0 and cap the count at 16, and
; that costs a package nothing it can reach: image plus bss cap at 60KB, and
; anything larger lives in a claim, which the _seg forms read straight into.
; =============================================================================

; int os88_ferr(void) - the FERR_* of the last file call, 0 = it worked.
_os88_ferr:
    mov ax, [cc_ferr]
    ret

; unsigned os88_file_read(const char *name, void *buf, unsigned cap)
; SI = name, ES:BX = the buffer, DX:CX = its capacity; out DX:AX = the file's
; size. DX is 0 on the way back by construction: a file bigger than the
; capacity is refused with FERR_BIG from the DIRECTORY ENTRY, before any I/O,
; so the buffer is left alone and the 16-bit answer is exact.
_os88_file_read:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov bx, [bp+6]
    push ds
    pop es                          ; ES:BX - the buffer is OURS
    mov cx, [bp+8]
    mov dx, 0                       ; the capacity's high word
    call OSAPI_FILE_READ
    jc .err
    mov word [cc_ferr], 0
    jmp short .out
.err:
    mov [cc_ferr], ax
    mov ax, 0                       ; 0 bytes; os88_ferr() says why
.out:
    pop si
    pop bp
    ret

; unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
; The same call with ES = a heap claim's base segment and BX = 0 (SPEC.md
; 18.4.1: the transfer walks the SEGMENT, so this is how a package reads
; something its own 60KB region could never hold).
_os88_file_read_seg:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov ax, [bp+6]
    mov es, ax                      ; ES = THEIR segment, not ours
    mov bx, 0
    mov cx, [bp+8]
    mov dx, 0
    call OSAPI_FILE_READ
    jc .err
    mov word [cc_ferr], 0
    jmp short .out
.err:
    mov [cc_ferr], ax
    mov ax, 0
.out:
    pop si
    pop bp
    ret

; int os88_file_write(const char *name, const void *buf, unsigned count)
; SI = name, ES:BX = the bytes, DX:CX = the count (0 = an empty file).
; Creates or REPLACES. 0 = written, -1 = see os88_ferr(). A write stalls every
; painter for its duration: never from os88_paint().
_os88_file_write:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov bx, [bp+6]
    push ds
    pop es                          ; ES:BX - ours
    mov cx, [bp+8]
    mov dx, 0
    call OSAPI_FILE_WRITE
    jc .err
    mov word [cc_ferr], 0
    mov ax, 0
    jmp short .out
.err:
    mov [cc_ferr], ax
    mov ax, -1
.out:
    pop si
    pop bp
    ret

; int os88_file_write_seg(const char *name, unsigned seg, unsigned count)
_os88_file_write_seg:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov ax, [bp+6]
    mov es, ax                      ; ES = the claim's segment
    mov bx, 0
    mov cx, [bp+8]
    mov dx, 0
    call OSAPI_FILE_WRITE
    jc .err
    mov word [cc_ferr], 0
    mov ax, 0
    jmp short .out
.err:
    mov [cc_ferr], ax
    mov ax, -1
.out:
    pop si
    pop bp
    ret

; int os88_file_append(const char *name, const void *buf, unsigned count)
; SPEC.md 18.4.4. The file must already exist and its size must be a whole
; number of CLUSTERS - create it with os88_file_write() and the first chunk,
; then append the rest in multiples of os88_disk_cluster_sectors() * 512.
_os88_file_append:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov bx, [bp+6]
    push ds
    pop es
    mov cx, [bp+8]
    call OSAPI_FILE_APPEND
    jc .err
    mov word [cc_ferr], 0
    mov ax, 0
    jmp short .out
.err:
    mov [cc_ferr], ax
    mov ax, -1
.out:
    pop si
    pop bp
    ret

; int os88_file_delete(const char *name) / os88_file_mkdir(const char *name)
; SI = a NUL 8.3 name in our segment.
%macro CC_T_FNAME1 2                ; label, slot
%1:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    call %2
    jc %%err
    mov word [cc_ferr], 0
    mov ax, 0
    jmp short %%out
%%err:
    mov [cc_ferr], ax
    mov ax, -1
%%out:
    pop si
    pop bp
    ret
%endmacro

CC_T_FNAME1 _os88_file_delete, OSAPI_FILE_DELETE
CC_T_FNAME1 _os88_file_mkdir,  OSAPI_FILE_MKDIR

; int os88_file_rename(const char *oldname, const char *newname)
; SI = old, DI = new.
_os88_file_rename:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+4]
    mov di, [bp+6]
    call OSAPI_FILE_RENAME
    jc .err
    mov word [cc_ferr], 0
    mov ax, 0
    jmp short .out
.err:
    mov [cc_ferr], ax
    mov ax, -1
.out:
    pop di
    pop si
    pop bp
    ret

; int os88_file_find(int ordinal, struct os88_find *f) - SPEC.md 19.7.1.
; CX = the ordinal, ES:DI = an OSAPI_FIND_SZ buffer of ours. Returns the NEXT
; ordinal, or -1 at the end. By ordinal and not by cursor on purpose: every
; step between two of these walks directories itself.
_os88_file_find:
    push bp
    mov bp, sp
    push di
    mov cx, [bp+4]
    mov di, [bp+6]
    push ds
    pop es                          ; ES:DI - the buffer is ours
    call OSAPI_FILE_FIND
    jc .end
    mov word [cc_ferr], 0
    mov ax, cx                      ; the next ordinal
    jmp short .out
.end:
    mov [cc_ferr], ax               ; FERR_NOENT at the end of the listing
    mov ax, -1
.out:
    pop di
    pop bp
    ret

; unsigned os88_disk_free_kb(void) - OSAPI_FILE_DFREE answers DX:AX bytes and
; BX = SECTORS per cluster, with no disk I/O. C has no 32-bit type (70.7), so
; the shift happens here: ten single-bit shifts through the carry, which is
; the 8086's only way to move a 32-bit value and is 20 instructions once.
; Saturates at 65,535 KB rather than wrapping - a 2GB partition really can
; have more free than a word can hold, and a wrapped answer would read as a
; nearly-full disk.
_os88_disk_free_kb:
    call OSAPI_FILE_DFREE
    jc .none
    mov cx, 10                      ; >> 10 = bytes to KB
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    or dx, dx
    jz .out
    mov ax, 0FFFFh                  ; more than a word of KB: saturate
.out:
    ret
.none:
    mov ax, 0
    ret

; int os88_disk_cluster_sectors(void) - BX from the same slot. A chunked copy
; is a multiple of this AT BOTH ENDS: two volumes need not agree (18.4.4).
_os88_disk_cluster_sectors:
    call OSAPI_FILE_DFREE
    mov ax, bx
    jnc .out
    mov ax, 0
.out:
    ret

; void os88_file_here(struct os88_place *p) - out DX = your instance's current
; directory cluster (0 = root), BL = its drive. No disk I/O.
_os88_file_here:
    push bp
    mov bp, sp
    push di
    mov di, [bp+4]
    call OSAPI_FILE_HERE
    mov [di], dx
    mov bh, 0
    mov [di+2], bx
    pop di
    pop bp
    ret

; int os88_file_goto(struct os88_place *p) - DX = the cluster, BL = the drive.
; A REMOUNT: real floppy I/O, UI-task only. -1 = it could not be listed, and
; the volume is back at its root with the write gate shut.
_os88_file_goto:
    push bp
    mov bp, sp
    push di
    mov di, [bp+4]
    mov dx, [di]
    mov bx, [di+2]
    call OSAPI_FILE_GOTO
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop di
    pop bp
    ret

; int os88_file_goto_q_mark(unsigned clus, int vol) - DX = the folder's first
; cluster (0 = the root), BL = the volume (SPEC.md 71.1, 19.2.2). GOTO_Q's
; quiet stand - a word inside the current volume, a quiet mount across one -
; AND the instance moves with it, so the file call you make next resolves
; there instead of being re-stood in the folder you were launched from. 0
; moved, -1 refused (os88_ferr() says why). Bank the place you were with
; os88_file_here() first if you mean to come back; a launch folder is one
; struct os88_place, and so is a CP/M drive.
_os88_file_goto_q_mark:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov bx, [bp+6]
    call OSAPI_FILE_GOTO_QM
    mov [cc_ferr], ax               ; the FERR_* on refusal, 0 on success -
                                    ; the same word every file thunk writes
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop bp
    ret

; int os88_vol_sys(void) - BL = the volume this machine BOOTED from (SPEC.md
; 19.7): A: on a floppy machine, the installed partition on one that boots
; from its hard disk. No disk I/O and it cannot fail. BH is cleared here
; rather than assumed: the slot preserves every other register, which means
; BH comes back holding whatever this routine happened to leave in it.
_os88_vol_sys:
    call OSAPI_VOL_SYS
    mov ax, bx
    mov ah, 0
    ret

%ifdef CC_HAS_FDLG
; int os88_file_dlg(int mode, void *win, const char *defname) - SPEC.md 38.
; AL = FDLG_OPEN/SAVE, BX = your window, DI = the completion trampoline, SI =
; a default name or 0. It does NOT block: it returns with the dialog already
; on screen (you were holding the lock it needed) and calls os88_onfile() much
; later. Cancel calls nothing at all. -1 = refused: one is already up.
_os88_file_dlg:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov si, [bp+8]
    mov di, cc_onfile
    call OSAPI_FILE_DLG
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop di
    pop si
    pop bp
    ret
%endif

; int os88_arg_file(char *name13, struct os88_place *p) - SPEC.md 54.5. The
; document this instance was launched to open, READ-AND-CLEAR: the first
; caller gets it, so a second instance can never inherit a document.
;
; The name comes back as SI -> the KERNEL's segment, which is the second place
; a package is handed a pointer it cannot dereference (the first is the window
; record). ES is loaded here rather than trusted, for the reason CC_T_WFIELD
; gives, and the 13 bytes are copied out before anything else runs: the buffer
; is the kernel's and is valid for the duration of the call only.
_os88_arg_file:
    push bp
    mov bp, sp
    push si
    push di
    call OSAPI_ARG_FILE             ; CF=1 = launched empty, the ordinary case
    jc .none
    push dx                         ; the directory cluster...
    push bx                         ; ...and BL = its volume, across the copy
    mov ax, KERNEL_SEG
    mov es, ax
    mov di, [bp+4]                  ; our 13-byte buffer: DS-relative
    mov cx, 13
.cp:
    mov al, [es:si]                 ; from the kernel...
    mov [di], al                    ; ...to us
    inc si
    inc di
    or al, al
    jz .copied
    loop .cp
    mov byte [di-1], 0              ; 13 bytes and no NUL: terminate anyway
.copied:
    pop bx
    pop dx
    mov di, [bp+6]
    mov [di], dx
    mov bh, 0
    mov [di+2], bx
    mov ax, 0
    jmp short .out
.none:
    mov ax, -1
.out:
    pop di
    pop si
    pop bp
    ret

; int os88_assoc_set(const char *ext, const char *stem) - SPEC.md 54.5.
; ES:SI -> 3 extension bytes then 8 stem bytes, both SPACE-PADDED. The padding
; is done here into the runtime's own 11-byte buffer, because getting it wrong
; in C - a NUL where a space belongs - claims an extension nobody will ever
; type. Declaring the association in the package header instead (54.6) needs
; no call at all, but that is an assembly-side declaration.
_os88_assoc_set:
    push bp
    mov bp, sp
    push si
    push di
    mov di, cc_assoc                ; ours, DS-relative throughout the fill
    mov si, [bp+4]
    mov cx, 3
    call cc_padcopy
    mov si, [bp+6]
    mov cx, 8
    call cc_padcopy
    mov si, cc_assoc
    push ds
    pop es                          ; ES:SI - the block is ours
    call OSAPI_ASSOC_SET
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop di
    pop si
    pop bp
    ret

; cc_padcopy - copy CX bytes of the NUL string at SI to DI, padding with
; spaces (module-internal; DS for both ends).
; in:  SI = source, DI = destination, CX = the fixed width
; out: DI advanced by CX; clobbers AX, CX, SI, DI
cc_padcopy:
    mov al, [si]
    or al, al
    jz .pad
    inc si
    jmp short .put
.pad:
    mov al, ' '
    mov si, cc_nul                  ; stay on a NUL for the rest of the field
.put:
    mov [di], al
    inc di
    loop cc_padcopy
    ret

CC_T_VOID _os88_batch_begin,   OSAPI_BATCH_BEGIN   ; SPEC.md 18.9.3: nests,
CC_T_VOID _os88_batch_end,     OSAPI_BATCH_END     ; and any unlock ends it

; =============================================================================
; MEMORY BEYOND THE REGION (SPEC.md 50.3)
; =============================================================================

; unsigned os88_mem_claim(int kb) - AX = KB; out DX = the base segment, CF =
; refused. You are identified by the segment you run in, so there is nothing
; to pass and nothing to forge, and it works from os88_main() - which is where
; an app sizes itself. The kernel frees your claims when your instance dies.
_os88_mem_claim:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    call OSAPI_MEM_CLAIM
    mov ax, dx
    jnc .ok
    mov ax, 0                       ; 0 = refused, and nothing was claimed
.ok:
    pop bp
    ret

; int os88_mem_free(unsigned seg) - DX = a segment you were given.
_os88_mem_free:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    call OSAPI_MEM_FREE
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop bp
    ret

; unsigned os88_mem_regrow(unsigned seg, int kb) - DX = the claim, AX = the
; new size in KB; out DX = the claim's base NOW. ALWAYS TAKE THE ANSWER: a
; grow that had to move leaves your old segment pointing at memory that is no
; longer yours. 0 = refused, and the old claim is untouched at its old base.
_os88_mem_regrow:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov ax, [bp+6]
    call OSAPI_MEM_REGROW
    mov ax, dx
    jnc .ok
    mov ax, 0
.ok:
    pop bp
    ret

CC_T_AX   _os88_mem_largest_kb, OSAPI_MEM_AVAIL         ; AX = largest run
CC_T_REG  _os88_mem_total_kb,   OSAPI_MEM_AVAIL, bx     ; BX = total free
                                                        ; Size yourself from
                                                        ; these, never int 12h

; int os88_peek(unsigned seg, unsigned off) - one byte out of a claim.
; The other half of "C cannot write a segment override". A byte at a time is
; a near call each: right for a header, wrong for a copy (read bulk with
; os88_file_read_seg, or blit straight out of the claim with os88_gfx_blit4,
; which takes a segment of its own).
_os88_peek:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov es, ax                      ; THEIR segment
    mov bx, [bp+6]
    mov al, [es:bx]
    mov ah, 0
    pop bp
    ret

; void os88_poke(unsigned seg, unsigned off, int value) - one byte in.
_os88_poke:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov es, ax
    mov bx, [bp+6]
    mov ax, [bp+8]
    mov [es:bx], al
    pop bp
    ret

; =============================================================================
; THE CLIPBOARD (SPEC.md 55) - one text buffer for the whole machine, which
; outlives the app that filled it. All three take no lock and do no I/O, so
; they are legal from a worker as well as from a callback.
; =============================================================================

; int os88_clip_put(const void *text, unsigned len) - ES:SI, CX. len 0 EMPTIES
; the clipboard and is not an error. -1 = refused (over CLIP_MAXKB, or the
; heap could not fund it), and then the clipboard is EMPTY rather than stale.
_os88_clip_put:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov cx, [bp+6]
    push ds
    pop es                          ; ES:SI - the text is ours
    call OSAPI_CLIP_PUT
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop si
    pop bp
    ret

; int os88_clip_get(void *buf, unsigned cap) - ES:DI, CX; out AX = the WHOLE
; length, CX = what was copied. This returns CX, the bytes you actually got;
; ask os88_clip_size() first and grow your buffer, which on a machine with a
; claim heap is the difference between a paste that works and one that
; half-arrives.
_os88_clip_get:
    push bp
    mov bp, sp
    push di
    mov di, [bp+4]
    mov cx, [bp+6]
    push ds
    pop es                          ; ES:DI - the buffer is ours
    call OSAPI_CLIP_GET
    mov ax, cx
    pop di
    pop bp
    ret

; int os88_clip_size(void) - -1 = empty.
_os88_clip_size:
    call OSAPI_CLIP_SIZE
    jnc .ok
    mov ax, -1
.ok:
    ret

; int os88_toast(const char *text, int ticks) - SPEC.md 59. ES:SI = a NUL
; line, CX = ticks to live (0 = ~3s). An inverse-video strip at the right end
; of the MENU BAR: it costs your window no pixels, survives a repaint without
; your help, and cannot leave your incremental drawing disagreeing with your
; paint proc. The string is COPIED, so it may be scratch. Any context.
_os88_toast:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov cx, [bp+6]
    push ds
    pop es                          ; ES:SI - ours
    call OSAPI_TOAST
    mov ax, 0
    jnc .ok
    dec ax
.ok:
    pop si
    pop bp
    ret

; =============================================================================
; THE RUNTIME'S OWN HELPERS
;
; There is no C library on this floppy (SPEC.md 70.9: no printf family - it is
; thousands of bytes for the one conversion a program wanted). These six are
; the whole of it, and every one is DS-ONLY: not a single string instruction,
; because movs/stos address ES:DI and ES is the kernel's (70.5.1). A byte at a
; time through [si]/[di] is slower than `rep movsb` and it is the version that
; cannot overwrite the operating system.
; =============================================================================

; void os88_memset(void *p, int c, unsigned n)
_os88_memset:
    push bp
    mov bp, sp
    push di
    mov di, [bp+4]
    mov ax, [bp+6]
    mov cx, [bp+8]
    jcxz .done
.l:
    mov [di], al
    inc di
    loop .l
.done:
    pop di
    pop bp
    ret

; void os88_memcpy(void *dst, const void *src, unsigned n)
; Both ends are DS. Forward only, so an overlapping copy is safe only when dst
; is below src - which is the direction a text editor deletes in.
_os88_memcpy:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    jcxz .done
.l:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .l
.done:
    pop di
    pop si
    pop bp
    ret

; unsigned os88_strlen(const char *s)
_os88_strlen:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    mov ax, 0
.l:
    cmp byte [si], 0
    je .done
    inc si
    inc ax
    jmp short .l
.done:
    pop si
    pop bp
    ret

; void os88_strcpy(char *dst, const char *src, unsigned cap)
; ALWAYS NUL-terminates and never writes more than cap bytes, cap including
; the NUL. cap 0 writes nothing at all.
_os88_strcpy:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    jcxz .done
    dec cx                          ; leave room for the terminator
    jcxz .term
.l:
    mov al, [si]
    or al, al
    jz .term
    mov [di], al
    inc si
    inc di
    loop .l
.term:
    mov byte [di], 0
.done:
    pop di
    pop si
    pop bp
    ret

; char *os88_utoa(unsigned v, char *dst6) - decimal, no padding, returns dst.
; The buffer needs 6 bytes: 65535 plus the NUL. The digits come off the
; division in reverse, so they go on the STACK and come back off in order -
; at most five words, which is the kind of frame the 256-byte worker stack can
; afford (SPEC.md 70.8).
_os88_utoa:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov di, [bp+6]
    mov si, di                      ; the answer, kept for the return
    mov cx, 0
    mov bx, 10
.div:
    mov dx, 0                       ; DX:AX / 10 -> AX quotient, DX remainder
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .emit
    mov byte [di], 0
    mov ax, si
    pop di
    pop si
    pop bp
    ret

; char *os88_itoa(int v, char *dst7) - signed; the buffer needs 7 bytes for
; -32768 and the NUL. `neg` on -32768 gives 0x8000 back, which os88_utoa reads
; as 32768 - so the edge case prints correctly rather than needing a branch.
_os88_itoa:
    push bp
    mov bp, sp
    push si
    mov ax, [bp+4]
    mov si, [bp+6]
    push si                         ; the answer
    mov bx, si
    or ax, ax
    jns .pos
    mov byte [bx], '-'
    inc bx
    neg ax
.pos:
    push bx                         ; os88_utoa(ax, bx) - cdecl, right to left
    push ax
    call _os88_utoa
    add sp, 4                       ; the CALLER cleans (SPEC.md 70.3)
    pop ax                          ; the original destination
    pop si
    pop bp
    ret

; the NUL cc_padcopy parks on once its source has run out (SPEC.md 20.2: an
; ordinary byte of the image, addressed DS-relative like everything else).
cc_nul: db 0
