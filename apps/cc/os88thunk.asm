; =============================================================================
; os8088 - apps/cc/os88thunk.asm
;
; The bridge between the C ABI and the os8088 API (SPEC.md 73.3). Every
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
; AX/BX/CX/DX = x1/y1/x2/y2 and `OSAPI_FONT_STR_XPARENT` wants SI = a string. A C
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
;            precisely that distinction (73.5); nothing below ever needs one.
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
;       LEFT by the caller. The CALLER cleans (SPEC.md 73.3).
; out:  AX = the C return value. void functions return nothing and AX is junk.
;       A slot whose only answer is CF becomes `int`: 0 success, -1 refused.
; preserves: BP, DS, SS:SP and DF. NOTHING ELSE - SmallerC has no
;       callee-saved register (73.3), so AX, BX, CX, DX, SI, DI, ES and FLAGS
;       are all fair game and the compiler expects them to be gone - which is
;       why no routine below saves SI or DI. (They used to, "for convenience":
;       two bytes and ~19 cycles a call that nothing on the other side reads.)
;       A one-argument routine reads its argument as `mov bx, sp` /
;       `[ss:bx+2]` instead of building a frame: a byte shorter and ~20 cycles
;       cheaper than push bp / mov bp, sp / pop bp, and the override is
;       not optional - [bx] addresses DS, and the argument is on SS.
; DF:   every OSAPI slot leaves it clear and nothing here sets it, so the
;       callback rule (DF clear on return, SPEC.md 20.2) is met by the
;       trampolines in crt0.asm without this file's help. The string
;       instructions the helpers at the foot use (rep stosb, rep movsb,
;       repne scasb, lodsb/stosb) all run forward on that guarantee, and each
;       loads ES from DS on the line before - ES is the kernel's (73.5.1).
; flags:one idiom recurs: a CF a slot answers with becomes the C value with
;       `sbb ax, ax` - 0 when CF is clear, -1 when it is set, in two bytes and
;       three cycles where `mov ax, 0 / jnc / dec ax` was six bytes and a
;       branch. `neg` after it gives 0 / 1, and `not` / `and r` gives "0, or
;       the register". Nothing between the slot and the SBB may write FLAGS.
;
; The register contract of the slot itself is os88api.inc's, and the comment
; on each routine below names it rather than repeating its prose.
;
; -----------------------------------------------------------------------------
; ONLY WHAT THE C CALLS IS ASSEMBLED (SPEC.md 73.9.1)
; -----------------------------------------------------------------------------
; `nasm -f bin` has no dead-code elimination, and this file used to put every
; one of its thunks into every C package: ~3KB, of which ccsmoke calls 13
; routines. So every thunk below sits inside `%ifdef CC_U_<its label>`, and
; tools/cc8086.py writes one `%define CC_U_<name>` at the head of the compiled
; C for every symbol the C names and does not define - SmallerC's own
; `extern` list, which is exact. crt0.asm includes this file at CC_IMAGE_END,
; AFTER the compiled C, which is the whole of why the defines are in scope by
; the time a gate is read, and defines CC_THUNK_GATED to say so.
;
; INCLUDED ANY OTHER WAY - before the compiled C, the way apps/loom/lmpvmod.asm
; does - there is no use set yet, so CC_THUNK_ALL is set and every gate opens:
; that build assembles exactly the file it always did, and declares the bss
; below itself, as it always has.
;
; A thunk called only from HAND-WRITTEN assembly is the one use this cannot
; see, and it fails loudly rather than silently - nasm names the symbol as
; not defined - and the shim keeps it with `%define CC_U__os88_<name>`.
; A NEW THUNK goes inside `CC_GATE <label>` / `%ifdef CC_U_<label>` like its
; neighbours (the CC_T_* macros do it for themselves); one written outside a
; gate is still correct, it is just carried by every package again.
; =============================================================================

%ifndef CC_THUNK_GATED
  %define CC_THUNK_ALL
%endif

; CC_GATE label - in CC_THUNK_ALL mode, declare `label` used. Written in front
; of every gate, because nasm's %ifdef cannot OR two names together.
%macro CC_GATE 1
%ifdef CC_THUNK_ALL
  %define CC_U_%1
%endif
%endmacro

; THE THUNKS OTHER THUNKS REACH, which the C's use set cannot know about.
%ifdef CC_U__os88_itoa
  %define CC_U__os88_utoa           ; os88_itoa is a sign and a call to it
%endif
%ifdef CC_U__os88_file_copy
  %define CC_U__os88_fcp            ; both names are one body (SPEC.md 22.24)
%endif
%ifdef CC_U__os88_file_move
  %define CC_U__os88_fcp
%endif

; -----------------------------------------------------------------------------
; The five shapes that repeat, as macros. A macro is used only where the whole
; body is mechanical - load registers from the frame, call, return - so that
; the interesting routines further down stand out as interesting. Each
; invocation still carries its own contract comment; the macro supplies the
; three or four instructions, never the reason.
; -----------------------------------------------------------------------------

; CC_T_VOID label, slot            void f(void)
%macro CC_T_VOID 2
CC_GATE %1
%ifdef CC_U_%1
%1:
    call %2
    ret
%endif
%endmacro

; CC_T_AX label, slot              int f(void), answered in AX by the slot
%macro CC_T_AX 2
CC_GATE %1
%ifdef CC_U_%1
%1:
    call %2
    ret
%endif
%endmacro

; CC_T_REG label, slot, reg        int f(void), answered in `reg`
%macro CC_T_REG 3
CC_GATE %1
%ifdef CC_U_%1
%1:
    call %2
    mov ax, %3
    ret
%endif
%endmacro

; CC_T_A1 label, slot, reg         void f(int a) -> reg = a
%macro CC_T_A1 3
CC_GATE %1
%ifdef CC_U_%1
%1:
    mov bx, sp                      ; no frame: one argument
    mov %3, [ss:bx+2]
    call %2
    ret
%endif
%endmacro

; CC_T_WIN label, slot             void f(void *win) -> BX = win
%macro CC_T_WIN 2
CC_GATE %1
%ifdef CC_U_%1
%1:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    call %2
    ret
%endif
%endmacro

; CC_T_WINF label, slot            void f(void *win, int on) -> BX, AL
%macro CC_T_WINF 2
CC_GATE %1
%ifdef CC_U_%1
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
%endif
%endmacro

; CC_T_RECT label, slot            void f(x1,y1,x2,y2) -> AX,BX,CX,DX
%macro CC_T_RECT 2
CC_GATE %1
%ifdef CC_U_%1
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
%endif
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
CC_GATE _os88_gfx_pixel
%ifdef CC_U__os88_gfx_pixel
_os88_gfx_pixel:
    push bp
    mov bp, sp
    mov cx, [bp+4]
    mov dx, [bp+6]
    call OSAPI_GFX_PIXEL
    pop bp
    ret
%endif

; void os88_gfx_points(const void *pts, int n) - SI = the array, CX = n.
; SPEC.md 5.6.9: n records of two ints each, x then y, IN OUR SEGMENT - so the
; C passes a plain pointer to a static `int[]` and there is no segment to think
; about, os88_gfx_fill_pat's shape. It is the plot primitive: one arrival for
; a whole set of pixels the program computed, against os88_gfx_pixel's one far
; call EACH at ~640us on the field machine.
CC_GATE _os88_gfx_points
%ifdef CC_U__os88_gfx_points
_os88_gfx_points:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov cx, [bp+6]
    call OSAPI_GFX_POINTS
    pop bp
    ret
%endif

; void os88_gfx_hline(int x1, int x2, int y) - AX = x1, BX = x2, DX = y.
CC_GATE _os88_gfx_hline
%ifdef CC_U__os88_gfx_hline
_os88_gfx_hline:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_GFX_HLINE
    pop bp
    ret
%endif

; void os88_gfx_vline(int x, int y1, int y2) - AX = x, BX = y1, DX = y2.
CC_GATE _os88_gfx_vline
%ifdef CC_U__os88_gfx_vline
_os88_gfx_vline:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_GFX_VLINE
    pop bp
    ret
%endif

; The five rectangle primitives - AX/BX/CX/DX = x1/y1/x2/y2 (SPEC.md 5).
CC_T_RECT _os88_gfx_fill,      OSAPI_GFX_FILL
CC_T_RECT _os88_gfx_frame,     OSAPI_GFX_FRAME
CC_T_RECT _os88_gfx_fill_gray, OSAPI_GFX_FILL_GRAY
CC_T_RECT _os88_gfx_xor_rect,  OSAPI_GFX_XOR_RECT
CC_T_RECT _os88_gfx_xor_fill,  OSAPI_GFX_XOR_FILL

; void os88_gfx_fill_pat(int x1,int y1,int x2,int y2,const void *pat8)
; SI = 8 pattern bytes IN OUR SEGMENT (SPEC.md 20.9) - DS-relative, so the C
; passes a plain pointer to a static and there is no segment to think about.
CC_GATE _os88_gfx_fill_pat
%ifdef CC_U__os88_gfx_fill_pat
_os88_gfx_fill_pat:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    mov si, [bp+12]                 ; ours: DS, no override
    call OSAPI_GFX_FILL_PAT
    pop bp
    ret
%endif

; void os88_gfx_pen(int disabled) - SPEC.md 47. CF IS THE ARGUMENT, which is
; the one slot whose input is a flag rather than a register: every greying
; predicate in this OS already answers in CF, so the call site is a test and
; then this. Non-zero = disabled (CDGRAY plus the [gfx_dis] flag, which is the
; whole difference on the two 1bpp adapters).
CC_GATE _os88_gfx_pen
%ifdef CC_U__os88_gfx_pen
_os88_gfx_pen:
    mov bx, sp                      ; no frame: one argument
    xor ax, ax
    cmp ax, [ss:bx+2]               ; CF = (0 < arg, unsigned) = non-zero =
                                    ; disabled - no branch, no stc/clc
    call OSAPI_GFX_PEN              ; the cell preserves every register AND
    ret                             ; every flag, so CF crosses it unchanged
%endif

; int os88_gfx_scroll(int x1,int y1,int x2,int y2,int dy) - SPEC.md 5.5.
; SI = signed dy, positive scrolls the content UP. Returns -1 refused, and
; then NOTHING moved: fall back to a full repaint.
CC_GATE _os88_gfx_scroll
%ifdef CC_U__os88_gfx_scroll
_os88_gfx_scroll:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    mov si, [bp+12]
    call OSAPI_GFX_SCROLL
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

; int os88_gfx_blit1(const void *bits,int stride,int x,int y,int w,int rows)
; ES:SI = the 1bpp band, BP = its stride in bytes, AX/BX = x/y, CX = width,
; DX = rows (SPEC.md 5.4.2). Returns -1 REFUSED - x or w not a multiple of 8,
; rows outside 1..255, or a kern_small machine - and then nothing was drawn.
;
; BP IS AN ARGUMENT TO THE SLOT, and BP is also this file's frame pointer, so
; the stride is read out of the frame FIRST, into DI, and only then does BP
; become the stride. Reading it after `mov bp, di` would read the band's stride
; as a frame offset - the kind of mistake that shows up as a garbled blit and
; nothing else. Nothing reads the frame after the call, so the frame pointer
; is not parked across it: the `pop bp` at the end restores the CALLER's BP,
; which the push at the head saved, and that is all BP is owed.
CC_GATE _os88_gfx_blit1
%ifdef CC_U__os88_gfx_blit1
_os88_gfx_blit1:
    push bp
    mov bp, sp
    mov si, [bp+4]                  ; the band, an offset in OUR segment...
    mov di, [bp+6]                  ; ...its stride, parked until BP is free
    mov ax, [bp+8]
    mov bx, [bp+10]
    mov cx, [bp+12]
    mov dx, [bp+14]
    push ds
    pop es                          ; ES:SI - the band is ours, so ES = DS
    mov bp, di                      ; BP = stride, as the slot wants
    call OSAPI_GFX_BLIT1
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp                          ; the CALLER's BP
    ret
%endif

; void os88_gfx_blit4(const void *pix,int stride,int x,int y,int w,int h)
; ES:SI = packed 4bpp pixels (two per byte, high nibble leftmost), BP =
; stride, AX/BX = x/y, CX/DX = w/h. Same BP dance as above.
CC_GATE _os88_gfx_blit4
%ifdef CC_U__os88_gfx_blit4
_os88_gfx_blit4:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov di, [bp+6]
    mov ax, [bp+8]
    mov bx, [bp+10]
    mov cx, [bp+12]
    mov dx, [bp+14]
    push ds
    pop es                          ; ES:SI - ours
    mov bp, di
    call OSAPI_GFX_BLIT4
    pop bp                          ; the CALLER's BP
    ret
%endif

; int os88_gfx_blitp(const void *planes,int plane_step,int stride,
;                    int x,int y,int w,int rows)
; ES:SI = plane 0's first row, DI = the step to the next plane, BP = the row
; stride inside one, AX = x (A MULTIPLE OF 8), BX = y, CX = width in pixels,
; DX = rows (SPEC.md 5.4.3). Returns 0 taken, -1 REFUSED and nothing drawn.
;
; IT REFUSES MORE THAN IT ACCEPTS and the caller must read the answer: a 1bpp
; adapter, an x that is not a multiple of 8, a block off the screen, a REAL
; armed clip region, a straddle of two displays, and a kern_small kernel. The
; fallback for every one of them is os88_gfx_blit4(), which draws the same
; picture from the same packed bytes.
;
; PLANE_STEP'S BIT 15 IS THE PROBE (SPEC.md 5.4.3.2): pass `step | 0x8000` and
; every refusal is decided against the rect alone - `planes` and `stride` are
; not read and not one pixel is written - so a caller can ask "would a band
; here be taken?" BEFORE it has composed anything. That is the question a
; window on a two-card machine has no other way to ask, and a refused probe
; changes nothing, so asking again next frame is the intended use.
;
; BP IS AN ARGUMENT TO THE SLOT and BP is also this file's frame pointer, so
; the stride is the LAST thing read through the frame, by the instruction that
; overwrites BP with it. Reading anything after that reads the stride as a
; frame offset - a garbled blit, and nothing else to see.
CC_GATE _os88_gfx_blitp
%ifdef CC_U__os88_gfx_blitp
_os88_gfx_blitp:
    push bp
    mov bp, sp
    mov si, [bp+4]                  ; plane 0, an offset in OUR segment...
    mov di, [bp+6]                  ; ...the plane step, bit 15 = probe
    mov ax, [bp+10]
    mov bx, [bp+12]
    mov cx, [bp+14]
    mov dx, [bp+16]
    push ds
    pop es                          ; ES:SI - the planes are ours, so ES = DS
    mov bp, [bp+8]                  ; BP = stride, as the slot wants - the
                                    ; last read through the frame
    call OSAPI_GFX_BLITP
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp                          ; the CALLER's BP
    ret
%endif

; =============================================================================
; TEXT (SPEC.md 6). One 8x8 cell is ~900 us; a 78-cell row is ~71 ms.
; =============================================================================

; void os88_font_char_xparent(int x, int y, int ch) - CX = x, DX = y, AL = char.
CC_GATE _os88_font_char_xparent
%ifdef CC_U__os88_font_char_xparent
_os88_font_char_xparent:
    push bp
    mov bp, sp
    mov cx, [bp+4]
    mov dx, [bp+6]
    mov ax, [bp+8]
    call OSAPI_FONT_CHAR_XPARENT
    pop bp
    ret
%endif

; void os88_font_str_xparent(int x, int y, const char *s) - CX, DX, SI = NUL string
; in our segment.
CC_GATE _os88_font_str_xparent
%ifdef CC_U__os88_font_str_xparent
_os88_font_str_xparent:
    push bp
    mov bp, sp
    mov cx, [bp+4]
    mov dx, [bp+6]
    mov si, [bp+8]                  ; ours: DS
    call OSAPI_FONT_STR_XPARENT
    pop bp
    ret
%endif

; int os88_font_width(const char *s) - SI = string; out AX = pixel width.
CC_GATE _os88_font_width
%ifdef CC_U__os88_font_width
_os88_font_width:
    mov bx, sp                      ; no frame: one argument
    mov si, [ss:bx+2]
    call OSAPI_FONT_WIDTH
    ret
%endif

; void os88_font_run(int x,int y,const char *s,int ink,int paper) - SPEC.md
; 6.1: the cells' background and their glyphs in ONE pass, so the line is
; never momentarily blank and a background painter needs no clip test around
; it. CX = x, DX = y, SI = string, AL = ink, AH = paper.
CC_GATE _os88_font_run
%ifdef CC_U__os88_font_run
_os88_font_run:
    push bp
    mov bp, sp
    mov cx, [bp+4]
    mov dx, [bp+6]
    mov si, [bp+8]
    mov ax, [bp+12]                 ; paper first: it goes in the HIGH half
    mov ah, al
    mov al, [bp+10]                 ; ...then ink over the low half
    call OSAPI_FONT_RUN
    pop bp
    ret
%endif

; =============================================================================
; WINDOWS (SPEC.md 11)
; =============================================================================

; void *os88_wm_create(int x,int y,int w,int h,const char *title)
; SI = a 16-byte template (WT_*). The template is crt0.asm's `cc_tpl`, whose
; paint/onkey/onclick words are the trampolines the shim asked for and are
; already in place; this fills the five words that are the C's business.
; Returns 0 when the window table is full, which os88_main() returns straight
; on as its abort (crt0 turns a 0 into the CF the loader is owed).
CC_GATE _os88_wm_create
%ifdef CC_U__os88_wm_create
%if WT_X != 0 || WT_Y != 2 || WT_W != 4 || WT_H != 6 || WT_TITLE != 8
  %error "the WT_* template moved: os88_wm_create copies the arguments in order"
%endif
_os88_wm_create:
    push ds
    pop es                          ; ES:DI = the template, OURS...
    mov di, cc_tpl
    mov si, sp
    inc si
    inc si                          ; ...SS:SI = the five arguments, which are
    push ds                         ; WT_X, WT_Y, WT_W, WT_H and WT_TITLE in
    push ss                         ; that order - so they are one copy
    pop ds                          ; DS = the STACK, for this copy only
    mov cx, 5
    rep movsw                       ; the title is an offset in our segment,
    pop ds                          ; and the RECORD KEEPS THE POINTER, not the
                                    ; bytes - so it must outlive the window
    mov si, cc_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF = table full
    sbb ax, ax                      ; CF -> -1 / 0...
    not ax                          ; ...-> 0 / FFFF...
    and ax, bx                      ; ...-> 0 = "no window", the C's abort
                                    ; value, or the window
    ret
%endif

CC_T_WIN  _os88_wm_show,       OSAPI_WM_SHOW
CC_T_WIN  _os88_wm_hide,       OSAPI_WM_HIDE
CC_T_WIN  _os88_wm_front,      OSAPI_WM_FRONT
CC_T_WIN  _os88_wm_grow,       OSAPI_WM_GROW      ; a resizable window's own
                                                  ; repaint must END with this
CC_T_WIN  _os88_wm_destroy,    OSAPI_WM_DESTROY   ; a SECOND window of yours;
                                                  ; your own dies with the
                                                  ; instance (SPEC.md 38.1)
CC_T_WIN  _os88_wm_close,      OSAPI_WM_CLOSE     ; ...and THIS is how you
                                                  ; close your OWN (SPEC.md
                                                  ; 75.2): deferred to the next
                                                  ; UI pass, so it RETURNS and
                                                  ; you must return too. The
                                                  ; lock may be held or not

; The flag setters - BX = win, AL = 0 clear / non-zero set.
CC_T_WINF _os88_wm_sizable,    OSAPI_WM_SIZABLE
CC_T_WINF _os88_wm_snap,       OSAPI_WM_SNAP
CC_T_WINF _os88_wm_keeph,      OSAPI_WM_KEEPH
CC_T_WINF _os88_wm_ownbg,      OSAPI_WM_OWNBG
CC_T_WINF _os88_wm_saveu,      OSAPI_WM_SAVEU

; int os88_wm_obscured(void *win) - 1 = covered, hidden included (11.3.1).
CC_GATE _os88_wm_obscured
%ifdef CC_U__os88_wm_obscured
_os88_wm_obscured:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    call OSAPI_WM_OBSCURED
    sbb ax, ax                      ; CF -> 0 / -1...
    neg ax                          ; ...-> 0 / 1
    ret
%endif

; void os88_wm_content(void *win, struct os88_pt *origin)
; BX = win; out AX = content left, DX = content top. The struct is OURS, so
; the two stores are DS-relative. BX is reloaded AFTER the call because it was
; the input.
CC_GATE _os88_wm_content
%ifdef CC_U__os88_wm_content
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
%endif

; int os88_wm_geom(void *win, struct os88_size *sz) - BX = win; out CX/DX =
; content width/height, CF = not visible (-1). Prefer this to arithmetic on
; W_W/W_H: it is the same subtraction, done once, by the kernel.
CC_GATE _os88_wm_geom
%ifdef CC_U__os88_wm_geom
_os88_wm_geom:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    call OSAPI_WM_GEOM
    mov bx, [bp+6]                  ; MOV only from here to the SBB - none of
    mov [bx], cx                    ; these writes a flag
    mov [bx+2], dx
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

; void os88_wm_display(void *win, struct os88_video *v) - BX = win; out AX =
; width, BX = height, CX = the first row the dock owns, SI = the first row the
; desktop band has, DL = kind, DH = bits per pixel (SPEC.md 39.16.4).
;
; OSAPI_VIDEO ANSWERS ABOUT THE PRIMARY and is right to (SPEC.md 39.2.1) - but
; a window on a two-card machine can be dragged onto the other display, and
; then every fact taken from os88_video() is about a screen this window is not
; on. Ask this instead whenever the answer decides a LAYOUT or a DRAWING PATH,
; and ask it again after an os88_onresize(): a drag across the seam fires that
; handler too. No lock is needed and any context may ask, so a worker gating
; its own draw on the display's depth may call it.
;
; SI is an ANSWER of the slot's and is clobbered; the desktop row it carries is
; dropped, because struct os88_video has no field for it and the two callers
; that want it (SPEC.md 39.16.4) can subtract OS88_MBAR_H like everyone else.
CC_GATE _os88_wm_display
%ifdef CC_U__os88_wm_display
_os88_wm_display:
    push bp
    mov bp, sp
    mov di, [bp+6]                  ; the struct, ours
    mov bx, [bp+4]
    call OSAPI_WM_DISPLAY
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    xor ax, ax                      ; the two BYTES become two words, because
    mov al, dl                      ; a C `int` is what the struct declares
    mov [di+6], ax
    mov al, dh                      ; (AH is still 0)
    mov [di+8], ax
    pop bp
    ret
%endif

; int os88_wm_damage(void *win, struct os88_rect *r) - from inside your own
; os88_paint(). Returns 1 = draw the WHOLE content, 0 = draw exactly r, which
; may be EMPTY (x1 > x2) meaning draw nothing. Requires os88_wm_ownbg(win, 1);
; without it the kernel has already whitened your content and "whole" is the
; only correct answer (11.90.2). BX is both an input and an output word of the
; rect, so the destination is parked in DI.
CC_GATE _os88_wm_damage
%ifdef CC_U__os88_wm_damage
_os88_wm_damage:
    push bp
    mov bp, sp
    mov di, [bp+6]                  ; the rect, ours
    mov bx, [bp+4]
    call OSAPI_WM_DAMAGE            ; CF=1 whole; AX/BX/CX/DX = the rect
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    mov [di+6], dx
    sbb ax, ax                      ; CF -> 0 / -1...
    neg ax                          ; ...-> 0 / 1
    pop bp
    ret
%endif

; void os88_wm_title(void *win, const char *s) - BX = win, AX = a NUL string
; in OUR segment (or 0 = "the bytes W_TITLE already names changed"). Redraws
; the caption strip and nothing else: 17 rows, no W_PAINT.
CC_GATE _os88_wm_title
%ifdef CC_U__os88_wm_title
_os88_wm_title:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, [bp+6]
    call OSAPI_WM_TITLE
    pop bp
    ret
%endif

; void os88_wm_resize(void *win, int w, int h) - the WHOLE window, frame
; included. The gfx lock held or not - the slot takes it if you have none
; (SPEC.md 11.1.2). Call this instead of writing W_W/W_H:
; the fields are readable by contract and not writable.
CC_GATE _os88_wm_resize
%ifdef CC_U__os88_wm_resize
_os88_wm_resize:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_WM_RESIZE
    pop bp
    ret
%endif

; void os88_wm_minsize(void *win, int w, int h) - BX = win, CX/DX = the
; minimum OUTER width and height, frame included; 0, 0 withdraws it (SPEC.md
; 11.100.2). "Below this my layout stops working - do not cut me past it, even
; to fit a screen." It REGISTERS AND APPLIES, so calling it straight after
; os88_wm_create() from os88_main() is how a window opens no smaller than its
; layout needs; without one you get the system floor, WMIN_W x WMIN_H.
;
; Not CC_T_WIN and not a macro of its own: it is os88_wm_resize()'s register
; shape (BX, CX, DX) and that one is written longhand too.
;
; The kernel keeps the two NUMBERS, not a pointer to them - unlike the
; per-adapter preferred-size table of SPEC.md 11.100.1, where what the kernel
; holds is an offset into your image. So nothing here has to outlive the call,
; which is the only reason a plain (w, h) signature is honest.
;
; The floor is applied again on an adapter change and on a drag onto a smaller
; display, and the box you end up with is not always the one you asked for:
; the display clamps down, this lifts back, and it is capped at the display's
; own extent so it can never cost you your title bar. Read os88_wm_geom()
; after an os88_onresize() rather than assuming.
CC_GATE _os88_wm_minsize
%ifdef CC_U__os88_wm_minsize
_os88_wm_minsize:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_WM_MINSIZE
    pop bp
    ret
%endif

; void os88_wm_cursor(void *win, int shape) - BX = win, AL = OS88_CUR_*.
; Set it once at creation; the pointer wears it over your CONTENT only, and it
; dies with the record. Not CC_T_WINF: that macro folds any non-zero argument
; to 1 because a FLAG has two states, and this argument is an enumeration that
; will grow a third shape one day.
CC_GATE _os88_wm_cursor
%ifdef CC_U__os88_wm_cursor
_os88_wm_cursor:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, [bp+6]
    call OSAPI_WM_CURSOR
    pop bp
    ret
%endif

; The clip region (SPEC.md 11.3). It dies at the next os88_gfx_unlock().
CC_GATE _os88_wm_clip_set
%ifdef CC_U__os88_wm_clip_set
_os88_wm_clip_set:                  ; int os88_wm_clip_set(void *win)
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    call OSAPI_WM_CLIP_SET
    ; CF=1: not one pixel of your content shows - draw nothing this frame
    sbb ax, ax                      ; CF -> 0 / -1
    ret
%endif

CC_T_VOID _os88_wm_clip_clear, OSAPI_WM_CLIP_CLEAR

; int os88_wm_clip_test(x1,y1,x2,y2) - 0 = the whole rect is drawable (also
; when no clip is armed), -1 = it is not. Ask before you ERASE a rect you are
; about to letter: the fills clip per pixel and font_char clips per whole 8x8
; cell, so a wide erase can white out a band the glyphs then refuse to repaint.
CC_GATE _os88_wm_clip_test
%ifdef CC_U__os88_wm_clip_test
_os88_wm_clip_test:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    call OSAPI_WM_CLIP_TEST
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

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
CC_GATE %1
%ifdef CC_U_%1
%1:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]               ; the record's OFFSET, from our stack
    mov ax, KERNEL_SEG              ; ...and its SEGMENT, which is never ours
    mov es, ax
    mov ax, [es:bx+%2]
    ret
%endif
%endmacro

CC_T_WFIELD _os88_win_flags, W_FLAGS
CC_T_WFIELD _os88_win_x,     W_X
CC_T_WFIELD _os88_win_y,     W_Y
CC_T_WFIELD _os88_win_w,     W_W    ; the FRAME width - os88_wm_geom() is the
CC_T_WFIELD _os88_win_h,     W_H    ; content one

; int os88_win_visible(void *win) - W_FLAGS bit 1. What a worker re-checks
; under the lock before drawing (SPEC.md 20.6 rule 5), together with
; os88_wm_clip_set().
CC_GATE _os88_win_visible
%ifdef CC_U__os88_win_visible
_os88_win_visible:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    mov ax, KERNEL_SEG
    mov es, ax
    mov ax, [es:bx+W_FLAGS]
    and ax, 2
    ret
%endif

; -----------------------------------------------------------------------------
; Menus, About, and the two installable callbacks (SPEC.md 12.2, 13.7, 11.98)
;
; Each of these hands the kernel the address of a TRAMPOLINE in crt0.asm, so
; each is assembled only when the shim asked for that trampoline. A C package
; that calls one without the %define gets nasm's external-reference error
; naming the function, which is the failure naming its own cause (73.1).
; -----------------------------------------------------------------------------

%ifdef CC_HAS_MENUS
; void os88_menu_set(void *win, struct os88_menuset *set) - BX = win, SI = the
; set (0 = detach). AM_ONCMD is written here rather than by the C, because the
; C cannot name an assembly label: the set must therefore be WRITABLE, which
; means declared without `const` so it lands in .data.
CC_GATE _os88_menu_set
%ifdef CC_U__os88_menu_set
_os88_menu_set:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov si, [bp+6]
    or si, si
    jz .go
    mov word [si+AM_ONCMD], cc_oncmd   ; the set is OURS: DS-relative
.go:
    call OSAPI_MENU_SET             ; draws nothing, takes no lock, and
    pop bp                          ; preserves the FLAGS - so it is safe
    ret                             ; between wm_create and the CF the entry
                                    ; proc owes the loader
%endif
%endif

%ifdef CC_HAS_ABOUT
; void os88_about_set(void *win) - BX = win, SI = the handler. Adds
; 'About <Name>' above the 'Close' the kernel already puts in your pull-down.
CC_GATE _os88_about_set
%ifdef CC_U__os88_about_set
_os88_about_set:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    mov si, cc_about
    call OSAPI_ABOUT_SET
    ret
%endif

%ifdef OS88UI_ABOUT
; -----------------------------------------------------------------------------
; void os88_about_card(void *win, const char **lines)
;   - and void os88_about_card_d(void *win, const char **lines) for a PAINT.
;
; The standard About card (SPEC.md 20.5.1.1) drawn from a C package. The line
; table is a C array of `const char *` terminated by a 0 entry, which in this
; near model IS os88ui_about's own format - a word per line, 0 ending it - so
; nothing is staged or converted.
;
; TWO ENTRIES, for the reason SPEC.md 20.5.1.1 gives: from os88_about() the
; clip has to be armed (ui_dispatch calls you with none), and from
; os88_paint() re-arming would throw that paint's damage region away.
; -----------------------------------------------------------------------------
CC_GATE _os88_about_card
%ifdef CC_U__os88_about_card
_os88_about_card:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov si, [bp+6]
    call os88ui_about               ; arms the clip, then draws
    pop bp
    ret
%endif

CC_GATE _os88_about_card_d
%ifdef CC_U__os88_about_card_d
_os88_about_card_d:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov si, [bp+6]
    call os88ui_about_d             ; a paint's region is already armed
    pop bp
    ret
%endif
%endif
%endif

%ifdef CC_HAS_ONMOUSEUP
; void os88_wm_onmouseup(void *win) - BX = win, AX = the near proc.
CC_GATE _os88_wm_onmouseup
%ifdef CC_U__os88_wm_onmouseup
_os88_wm_onmouseup:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    mov ax, cc_onmouseup
    call OSAPI_WM_ONMOUSEUP
    ret
%endif
%endif

%ifdef CC_HAS_ONRESIZE
; void os88_wm_onresize(void *win) - BX = win, AX = the near proc.
CC_GATE _os88_wm_onresize
%ifdef CC_U__os88_wm_onresize
_os88_wm_onresize:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    mov ax, cc_onresize
    call OSAPI_WM_ONRESIZE
    ret
%endif
%endif

%ifdef CC_HAS_ONTIMER
; void os88_wm_ontimer(void *win) - BX = win, AX = the near proc (SPEC.md 13.9).
; Call it AFTER os88_wm_create, once; os88_wm_timer() below is what arms it.
CC_GATE _os88_wm_ontimer
%ifdef CC_U__os88_wm_ontimer
_os88_wm_ontimer:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    mov ax, cc_ontimer
    call OSAPI_WM_ONTIMER
    ret
%endif

; int os88_wm_timer(void *win, int ticks) - BX = win, AX = ticks from now, 0
; cancels (SPEC.md 13.9). 18.2 ticks a second, the clock the system already
; keeps. 0 = armed, -1 = REFUSED and nothing armed - a kern_small kernel
; carries the slot and not the body, and the caller owes a second path
; (WEAVE-SPEC 8.2's static caret, SPEC.md 13.8.2's static down state). TEST IT:
; the refusal is the whole reason this answers anything at all.
CC_GATE _os88_wm_timer
%ifdef CC_U__os88_wm_timer
_os88_wm_timer:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, [bp+6]
    call OSAPI_WM_TIMER
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif
%endif

%ifdef CC_HAS_ONCLOSE
; void os88_wm_onclose(void *win) - BX = win, AX = the near proc (SPEC.md 75.1).
; Call it AFTER os88_wm_create, once, from os88_main(). From that moment every
; door out of your window - the close box, the app-name menu's Close, the dock
; tile's context menu - runs os88_onclose() first and takes its answer.
CC_GATE _os88_wm_onclose
%ifdef CC_U__os88_wm_onclose
_os88_wm_onclose:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    mov ax, cc_onclose
    call OSAPI_WM_ONCLOSE
    ret
%endif
%endif

%ifdef CC_HAS_ONWAKE
; void os88_wm_onwake(void *win) - BX = win, AX = the near proc (SPEC.md 74.1).
CC_GATE _os88_wm_onwake
%ifdef CC_U__os88_wm_onwake
_os88_wm_onwake:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    mov ax, cc_onwake
    call OSAPI_WM_ONWAKE
    ret
%endif

; int os88_wm_wake(void *win) - BX = win (SPEC.md 74.1). Post the kick; the
; handler above runs on the UI task, lock free, some passes later. Any
; context. 0 = a wake is queued for this window (now or already), -1 = the
; 16-record ring was full of other events and nothing was posted - kick again
; from your next callback. Under CC_HAS_ONWAKE with the installer: a wake
; with no handler dispatches to nothing, so a package without one has no use
; for it and does not pay its bytes.
CC_GATE _os88_wm_wake
%ifdef CC_U__os88_wm_wake
_os88_wm_wake:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    call OSAPI_WM_WAKE
    sbb ax, ax                      ; CF -> 0 / -1
    ret
%endif
%endif

; int os88_fullscreen(void *win, int enter) - BX = win, AL = 1 enter / 0 exit
; (SPEC.md 11.2 - the WF_FULL latch, NOT the 53 exclusive bracket). The caller
; holds the gfx lock, which a window callback does. 0 = done, -1 = enter
; refused (another window owns the screen). Exiting is the app's job - the
; menu bar is unreachable while it holds the screen - and SPEC.md 11.2.1's
; F/Esc binding is the convention, with 74.2's stated exception for a
; terminal that owns both keys. Every window callback runs with the lock
; already held, so the fold below is the same one CC_T_WINF does: any non-zero
; word means enter.
CC_GATE _os88_fullscreen
%ifdef CC_U__os88_fullscreen
_os88_fullscreen:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ax, [bp+6]
    or ah, al
    mov al, ah
    call OSAPI_FULLSCREEN
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

%ifdef CC_HAS_FSX
; =============================================================================
; FULLSCREEN EXCLUSIVE (SPEC.md 53) - the four slots a C package can hold
;
; THE BLOCK IS GATED, on the idiom this file already uses twice (CC_HAS_FDLG,
; CC_HAS_PARTS). nasm has no dead-code elimination, so four thunks nothing
; references still cost every C package that assembles this file 92 bytes of
; its 61,440 - and the package they were added for is the only one that will
; ever enter a bracket. Ungated they took LOOM to 30 bytes of spare image, and
; the next SDK addition of a dozen bytes would have failed LOOM's build with a
; message pointing at LOOM. The four prototypes and the OS88_FSX* constants
; stay unconditional in os88.h: an unreferenced prototype costs nothing, and a
; package that calls one without the %define gets nasm naming the symbol.
;
; This is a DIFFERENT thing from os88_fullscreen() above, which is SPEC.md
; 11.2's window latch: there the desktop's mode, primitives, cursor and event
; ladder all stay live and the app is merely a window the size of the screen.
; Here the app BORROWS THE MACHINE - multitasking suspended, every kernel
; drawer parked, and the video mode the app's to change - and os88_fsx_run()
; does not return until the entry proc does.
;
; NONE OF THE BRACKET'S RULES IS CHECKABLE FROM C, which is why os88.h's
; "what is not wrapped" list carried these four for as long as it did. They
; are wrapped now because APPLE2 (docs/APPLE2-SPEC.md section 13) is the
; first C package to want a foreign video mode, and the rules are stated
; there and obeyed by hand in ONE function whose header is the list:
;
;   - the entry is a plain RESIDENT C function whose address is taken, and
;     NEVER an ovl_ - tools/cc8086.py refuses that address by name, because
;     an overlay call site is a shim the loader has to resolve and the
;     bracket has parked the machine that would resolve it;
;   - after the first os88_fsx_mode() every drawing slot renders DESKTOP
;     geometry into a foreign framebuffer and is off-limits until the entry
;     returns;
;   - keys come from a polled int 16h and the mouse from OSAPI_MOUSE, with
;     the app edge-detecting the buttons: no events are dispatched in here;
;   - frames are paced with os88_fsx_wait() and NEVER os88_task_sleep(),
;     which degenerates to an immediate return with nothing else eligible;
;   - nothing may touch PIT channel 0, the sound ports or int 10h mode sets.
;
; os88_fsx_run, os88_fsx_mode and os88_fsx_wait each answer 0, or -1 on CF.
; os88_fsx_caps DOES NOT TEST CF and answers the MASK: SPEC.md 53.4 makes it
; callable from any context, lock held or not - that is the whole point of it,
; since a mode row has to be greyed per SPEC.md 47 BEFORE a bracket exists to
; refuse anything - so it has no refusal to report and "no foreign mode on
; this display" is a mask of 0. A caller that guards it with `< 0` has written
; a branch that cannot be taken.
; =============================================================================

; int os88_fsx_caps(void *win, int *kind) - BX = the window to ask ABOUT
; (SPEC.md 53.4); out AX = the FSXM_* bitmask settable on the display that
; window is on, DL = that display's VID_* kind. Any context, lock held or
; not, so a mode row can be greyed with it BEFORE the bracket is entered
; (SPEC.md 47).
;
; PASS YOUR OWN WINDOW AND ASK AGAIN WHERE THE ANSWER IS USED: on a
; two-display desktop (SPEC.md 39.18.2) this is a question about a DISPLAY
; and a window moves between them. `kind` is an OUT-PARAMETER, so it must be
; a static and never `&local` (SPEC.md 73.5): the store below is DS-relative
; and a stack offset dereferenced through the package segment is the failure
; that assembles, runs and reads the wrong memory.
CC_GATE _os88_fsx_caps
%ifdef CC_U__os88_fsx_caps
_os88_fsx_caps:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    call OSAPI_FSX_CAPS
    mov bx, [bp+6]                  ; the mask is the RETURN value, so DL is
    mov dh, 0                       ; widened into the out-parameter and AX is
    mov [bx], dx                    ; left alone
    pop bp
    ret
%endif

; int os88_fsx_run(void (*entry)(void), void *win, int flags) - AX = a near
; entry inside our own image, BX = our window, CX = FSXF_* (SPEC.md 53.1).
; DOES NOT RETURN until `entry` does. -1 = refused, and the refusals are one
; predicate each: already in a bracket, the file dialog is up, the caller is
; not task 0, or the ownership fence (the window must be a live instance of
; ours and the entry inside our image+bss).
CC_GATE _os88_fsx_run
%ifdef CC_U__os88_fsx_run
_os88_fsx_run:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    call OSAPI_FSX_RUN
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

; int os88_fsx_mode(int id, void *fsi) - AL = FSXM_*, ES:DI = an FSI_SIZE
; block of OURS (SPEC.md 53.4). Bracket-only. The block is the caller's own
; data, so ES is loaded from DS and put back - the callback contract is
; ES = KERNEL_SEG and every routine in this file leaves it as it found it.
CC_GATE _os88_fsx_mode
%ifdef CC_U__os88_fsx_mode
_os88_fsx_mode:
    push bp
    mov bp, sp
    push es
    mov ax, [bp+4]
    mov di, [bp+6]
    push ds
    pop es                          ; ES:DI = ours, as gfx_blit4's contract is
    call OSAPI_FSX_MODE
    sbb ax, ax                      ; CF -> 0 / -1
    pop es
    pop bp
    ret
%endif

; int os88_fsx_wait(int kind) - AL = FSXW_TICK (0) / FSXW_VSYNC (1) /
; FSXW_FRAME (2), and it is also THE PRESENT (SPEC.md 53.5): the bracket
; never unlocks, so this is where a buffered frame reaches the glass. Draw,
; wait, repeat. Bracket-only, -1 outside one or on an unknown kind.
CC_GATE _os88_fsx_wait
%ifdef CC_U__os88_fsx_wait
_os88_fsx_wait:
    mov bx, sp                      ; no frame: one argument
    mov ax, [ss:bx+2]
    call OSAPI_FSX_WAIT
    sbb ax, ax                      ; CF -> 0 / -1
    ret
%endif
%endif

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
CC_GATE _os88_task_spawn
%ifdef CC_U__os88_task_spawn
_os88_task_spawn:
    mov bx, sp                      ; no frame: one argument
    mov bx, [ss:bx+2]
    mov ax, cc_worker
    call OSAPI_TASK_SPAWN
    sbb ax, ax                      ; CF -> 0 / -1
    ret
%endif
%endif

; =============================================================================
; INPUT, VIDEO, THE MACHINE
; =============================================================================

; void os88_mouse(struct os88_mouse *m) - out CX = x, DX = y, AL = buttons.
CC_GATE _os88_mouse
%ifdef CC_U__os88_mouse
_os88_mouse:
    mov bx, sp                      ; no frame: one argument
    mov di, [ss:bx+2]
    call OSAPI_MOUSE
    mov [di], cx
    mov [di+2], dx
    mov ah, 0                       ; AL is a byte; the struct field is a word
    mov [di+4], ax
    ret
%endif

CC_T_AX   _os88_evq_pending,   OSAPI_EVQ_PENDING

; int os88_key_down(int scan) - AL = a make scancode; out CF = 1 that key is
; DOWN right now, 0 it is up; the slot keeps every register (SPEC.md 9.7).
; ASKING IS WHAT ARMS IT: the first call clears and arms the map and always
; answers "up", so ask once in os88_main() and ignore that answer - asking
; first from a callback erases the make os88_onkey has already seen. It is
; ADVICE, NOT AN ORACLE: a break code lost inside a long interrupts-off window
; leaves that key reading down until it is pressed again, so bound what a
; "yes" makes you do rather than trusting it to end on its own.
CC_GATE _os88_key_down
%ifdef CC_U__os88_key_down
_os88_key_down:
    mov bx, sp                      ; no frame: one argument
    mov ax, [ss:bx+2]
    call OSAPI_KEY_DOWN
    sbb ax, ax                      ; CF -> 0 / -1...
    neg ax                          ; ...-> 0 / 1
    ret
%endif

CC_T_A1   _os88_srand,         OSAPI_SRAND, ax
CC_T_AX   _os88_rand,          OSAPI_RAND

; void os88_video(struct os88_video *v) - SPEC.md 39.2. AX = width, BX =
; height, CX = the first row the DOCK owns, DL = VID_*, DH = bits per pixel.
; ASK THIS: SCREEN_W/SCREEN_H are a reference, not a promise, and anything
; anchored to a screen edge is wrong on two adapters of three (SPEC.md 39).
CC_GATE _os88_video
%ifdef CC_U__os88_video
_os88_video:
    mov bx, sp                      ; no frame: one argument
    mov di, [ss:bx+2]
    call OSAPI_VIDEO
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    xor ax, ax                      ; the two bytes become two words, because
    mov al, dl                      ; a C `int` is what the struct declares
    mov [di+6], ax
    mov al, dh                      ; (AH is still 0)
    mov [di+8], ax
    ret
%endif

; int os88_cpu(void) - OS88_CPU_8086 / _286 / _386 (SPEC.md 41). A FACT the
; code can test rather than a guess about speed (PERFORMANCE.md rule 7).
CC_GATE _os88_cpu
%ifdef CC_U__os88_cpu
_os88_cpu:
    call OSAPI_CPU_INFO             ; AL = tier, AH = feature bits
    mov ah, 0
    ret
%endif

; =============================================================================
; SOUND - the resident PC speaker only (SPEC.md 34)
; =============================================================================

CC_T_AX   _os88_snd_caps,      OSAPI_SND_CAPS         ; SND_CAP_* bits

; int os88_snd_tone(int hz, int ticks, int prio) - AX = Hz (0 = off), CX =
; duration in ticks (0 = until off), DL = priority. Worker-safe by
; construction (34.3): the grant is stamped with the running task's instance,
; and a tone with a DURATION silences itself, so a worker need not come back.
CC_GATE _os88_snd_tone
%ifdef CC_U__os88_snd_tone
_os88_snd_tone:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call OSAPI_SND_TONE
    ; AL came back as the owner generation, which a C caller has no use for
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

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
CC_GATE _os88_ferr
%ifdef CC_U__os88_ferr
_os88_ferr:
    mov ax, [cc_ferr]
    ret
%endif

; unsigned os88_file_read(const char *name, void *buf, unsigned cap)
; SI = name, ES:BX = the buffer, DX:CX = its capacity; out DX:AX = the file's
; size. DX is 0 on the way back by construction: a file bigger than the
; capacity is refused with FERR_BIG from the DIRECTORY ENTRY, before any I/O,
; so the buffer is left alone and the 16-bit answer is exact.
CC_GATE _os88_file_read
%ifdef CC_U__os88_file_read
_os88_file_read:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov bx, [bp+6]
    push ds
    pop es                          ; ES:BX - the buffer is OURS
    mov cx, [bp+8]
    mov dx, 0                       ; the capacity's high word
    call OSAPI_FILE_READ
    sbb cx, cx                      ; -1 refused, 0 delivered
    and cx, ax                      ; CX = the FERR_*, or 0
    mov [cc_ferr], cx
    sub ax, cx                      ; AX = the count, or 0 - os88_ferr() says why
    pop bp
    ret
%endif

; unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap)
; The same call with ES = a heap claim's base segment and BX = 0 (SPEC.md
; 18.4.1: the transfer walks the SEGMENT, so this is how a package reads
; something its own 60KB region could never hold).
CC_GATE _os88_file_read_seg
%ifdef CC_U__os88_file_read_seg
_os88_file_read_seg:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov ax, [bp+6]
    mov es, ax                      ; ES = THEIR segment, not ours
    mov bx, 0
    mov cx, [bp+8]
    mov dx, 0
    call OSAPI_FILE_READ
    sbb cx, cx                      ; -1 refused, 0 delivered
    and cx, ax                      ; CX = the FERR_*, or 0
    mov [cc_ferr], cx
    sub ax, cx                      ; AX = the count, or 0 - os88_ferr() says why
    pop bp
    ret
%endif

; unsigned os88_file_read_at(const char *name, void *buf, unsigned cap,
;                            unsigned off_lo, unsigned off_hi)
; SI = name, ES:BX = the buffer, CX = its capacity in BYTES, DX:AX = the byte
; offset to read from; out DX:AX = the bytes DELIVERED (SPEC.md 18.4.4). The
; read half of the chunked pair whose write half is os88_file_append(), and
; the only way a C package can look at a file bigger than the buffer it can
; hold: os88_file_read() refuses a short buffer with FERR_BIG and reads
; nothing at all, so "show me the header and then decide" cannot be said with
; it.
;
; THE OFFSET IS TWO WORDS, NOT ONE, and that is the SDK's settled shape for a
; 32-bit value rather than a new idea: struct os88_find carries size_lo /
; size_hi and os88_onfile() is handed its size the same way (SPEC.md 73.7 - C
; has no such type here). Truncating the offset to a word would cap this at
; 64KB, which is the one file size the slot exists for. Reading a header is
; os88_file_read_at(name, buf, cluster_bytes, 0, 0).
;
; THE ANSWER, though, is one word and exact, for os88_file_read()'s reason:
; the delivery is bounded by the capacity, `cap` is an unsigned, so DX comes
; back 0 by construction and no saturation is possible (contrast
; os88_disk_free_kb(), where the 32 bits are genuinely the disk's and the
; shift down has to saturate).
;
; 0 MEANS TWO THINGS AND os88_ferr() SEPARATES THEM, exactly as it does for
; os88_file_read(): ferr 0 with 0 bytes is "the offset is at or past the end",
; which is how the loop terminates, and a non-zero ferr is a refusal. The
; offset AND the capacity must each be a whole number of CLUSTERS
; (os88_disk_cluster_sectors() * 512) or the answer is FERR_NAME - refused,
; never mis-read. The last chunk is the arithmetic's own exception: a tail
; shorter than a cluster is delivered whole.
CC_GATE _os88_file_read_at
%ifdef CC_U__os88_file_read_at
_os88_file_read_at:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov bx, [bp+6]
    push ds
    pop es                          ; ES:BX - the buffer is OURS
    mov cx, [bp+8]
    mov ax, [bp+10]                 ; DX:AX - the offset, low word first
    mov dx, [bp+12]
    call OSAPI_FILE_READ_AT
    sbb cx, cx                      ; -1 refused, 0 delivered
    and cx, ax                      ; CX = the FERR_*, or 0
    mov [cc_ferr], cx
    sub ax, cx                      ; AX = the count (DX is 0 - see above), or
                                    ; 0 bytes, and os88_ferr() says why
    pop bp
    ret
%endif

; int os88_file_write(const char *name, const void *buf, unsigned count)
; SI = name, ES:BX = the bytes, DX:CX = the count (0 = an empty file).
; Creates or REPLACES. 0 = written, -1 = see os88_ferr(). A write stalls every
; painter for its duration: never from os88_paint().
CC_GATE _os88_file_write
%ifdef CC_U__os88_file_write
_os88_file_write:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov bx, [bp+6]
    push ds
    pop es                          ; ES:BX - ours
    mov cx, [bp+8]
    mov dx, 0
    call OSAPI_FILE_WRITE
    sbb cx, cx                      ; -1 refused, 0 done
    and ax, cx                      ; AX = the FERR_*, or 0
    mov [cc_ferr], ax
    xchg ax, cx                     ; the answer: 0, or -1 (see os88_ferr())
    pop bp
    ret
%endif

; int os88_file_write_seg(const char *name, unsigned seg, unsigned count)
CC_GATE _os88_file_write_seg
%ifdef CC_U__os88_file_write_seg
_os88_file_write_seg:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov ax, [bp+6]
    mov es, ax                      ; ES = the claim's segment
    mov bx, 0
    mov cx, [bp+8]
    mov dx, 0
    call OSAPI_FILE_WRITE
    sbb cx, cx                      ; -1 refused, 0 done
    and ax, cx                      ; AX = the FERR_*, or 0
    mov [cc_ferr], ax
    xchg ax, cx                     ; the answer: 0, or -1 (see os88_ferr())
    pop bp
    ret
%endif

; int os88_file_append(const char *name, const void *buf, unsigned count)
; SPEC.md 18.4.4. The file must already exist and its size must be a whole
; number of CLUSTERS - create it with os88_file_write() and the first chunk,
; then append the rest in multiples of os88_disk_cluster_sectors() * 512.
CC_GATE _os88_file_append
%ifdef CC_U__os88_file_append
_os88_file_append:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov bx, [bp+6]
    push ds
    pop es
    mov cx, [bp+8]
    call OSAPI_FILE_APPEND
    sbb cx, cx                      ; -1 refused, 0 done
    and ax, cx                      ; AX = the FERR_*, or 0
    mov [cc_ferr], ax
    xchg ax, cx                     ; the answer: 0, or -1 (see os88_ferr())
    pop bp
    ret
%endif

; int os88_file_delete(const char *name) / os88_file_mkdir(const char *name)
; SI = a NUL 8.3 name in our segment.
%macro CC_T_FNAME1 2                ; label, slot
CC_GATE %1
%ifdef CC_U_%1
%1:
    mov bx, sp                      ; no frame: one argument
    mov si, [ss:bx+2]
    call %2
    sbb cx, cx                      ; -1 refused, 0 done
    and ax, cx                      ; AX = the FERR_*, or 0
    mov [cc_ferr], ax
    xchg ax, cx                     ; the answer: 0, or -1 (see os88_ferr())
    ret
%endif
%endmacro

CC_T_FNAME1 _os88_file_delete, OSAPI_FILE_DELETE
CC_T_FNAME1 _os88_file_mkdir,  OSAPI_FILE_MKDIR

; int os88_file_rename(const char *oldname, const char *newname)
; SI = old, DI = new.
CC_GATE _os88_file_rename
%ifdef CC_U__os88_file_rename
_os88_file_rename:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov di, [bp+6]
    call OSAPI_FILE_RENAME
    sbb cx, cx                      ; -1 refused, 0 done
    and ax, cx                      ; AX = the FERR_*, or 0
    mov [cc_ferr], ax
    xchg ax, cx                     ; the answer: 0, or -1 (see os88_ferr())
    pop bp
    ret
%endif

; int os88_file_copy(const char *name, const struct os88_place *from,
;                    const struct os88_place *to)
; int os88_file_move(const char *name, const struct os88_place *from,
;                    const struct os88_place *to)
; ONE thunk over one cell (SPEC.md 22.24): the two C names differ in the verb
; byte and nothing else. The file manager's own engine, which is why this is
; a thunk and not a loop: it streams through a buffer it claims, it DELETES A
; PARTIAL DESTINATION if anything fails - the half a hand-rolled copy in C
; gets wrong - and a move on one volume rewrites the directory entry and
; reads no data. A struct os88_place is exactly what os88_file_here() fills.
; Two answers, like every other file call: 0, or -1 with os88_ferr() set. The
; kernel's success answer is AX = 0 with CF clear, so one store serves both.
CC_GATE _os88_file_move
%ifdef CC_U__os88_file_move
_os88_file_move:
    mov ah, OSAPI_FCP_MOVE          ; the verb, parked in AH until AL is free
    jmp short _os88_fcp
%endif
CC_GATE _os88_file_copy
%ifdef CC_U__os88_file_copy
_os88_file_copy:
    mov ah, OSAPI_FCP_COPY
%endif
CC_GATE _os88_fcp
%ifdef CC_U__os88_fcp
_os88_fcp:
    push bp
    mov bp, sp
    mov si, [bp+6]                  ; the source place...
    mov dx, [si]                    ; ...its folder's first cluster...
    mov bl, [si+2]                  ; ...and its drive
    mov si, [bp+8]                  ; the destination's
    mov cx, [si]
    mov bh, [si+2]
    mov si, [bp+4]                  ; the name LAST: SI carried the places
    mov al, ah
    call OSAPI_FILE_COPY
    mov [cc_ferr], ax               ; 0 on success, the FERR_* otherwise - and
    sbb ax, ax                      ; `mov` leaves CF alone: 0, or -1
    pop bp
    ret
%endif

; int os88_file_find(int ordinal, struct os88_find *f) - SPEC.md 19.7.1.
; CX = the ordinal, ES:DI = an OSAPI_FIND_SZ buffer of ours. Returns the NEXT
; ordinal, or -1 at the end. By ordinal and not by cursor on purpose: every
; step between two of these walks directories itself.
CC_GATE _os88_file_find
%ifdef CC_U__os88_file_find
_os88_file_find:
    push bp
    mov bp, sp
    mov cx, [bp+4]
    mov di, [bp+6]
    push ds
    pop es                          ; ES:DI - the buffer is ours
    call OSAPI_FILE_FIND
    sbb dx, dx                      ; -1 at the end, 0 found
    and ax, dx                      ; FERR_NOENT at the end of the listing, or 0
    mov [cc_ferr], ax
    or dx, cx                       ; -1, or CX = the next ordinal
    xchg ax, dx
    pop bp
    ret
%endif

; unsigned os88_disk_free_kb(void) - OSAPI_FILE_DFREE answers DX:AX bytes and
; BX = SECTORS per cluster, with no disk I/O. C has no 32-bit type (73.7), so
; the shift happens here: ten single-bit shifts through the carry, which is
; the 8086's only way to move a 32-bit value and is 20 instructions once.
; Saturates at 65,535 KB rather than wrapping - a 2GB partition really can
; have more free than a word can hold, and a wrapped answer would read as a
; nearly-full disk.
CC_GATE _os88_disk_free_kb
%ifdef CC_U__os88_disk_free_kb
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
    xor ax, ax
    ret
%endif

; int os88_disk_cluster_sectors(void) - BX from the same slot. A chunked copy
; is a multiple of this AT BOTH ENDS: two volumes need not agree (18.4.4).
CC_GATE _os88_disk_cluster_sectors
%ifdef CC_U__os88_disk_cluster_sectors
_os88_disk_cluster_sectors:
    call OSAPI_FILE_DFREE
    sbb ax, ax                      ; CF -> -1 / 0...
    not ax
    and ax, bx                      ; ...-> 0, or BX
    ret
%endif

; void os88_file_here(struct os88_place *p) - out DX = your instance's current
; directory cluster (0 = root), BL = its drive. No disk I/O.
CC_GATE _os88_file_here
%ifdef CC_U__os88_file_here
_os88_file_here:
    mov bx, sp                      ; no frame: one argument
    mov di, [ss:bx+2]
    call OSAPI_FILE_HERE
    mov [di], dx
    mov bh, 0
    mov [di+2], bx
    ret
%endif

; int os88_file_goto(struct os88_place *p) - DX = the cluster, BL = the drive.
; A REMOUNT: real floppy I/O, UI-task only. -1 = it could not be listed, and
; the volume is back at its root with the write gate shut.
CC_GATE _os88_file_goto
%ifdef CC_U__os88_file_goto
_os88_file_goto:
    mov bx, sp                      ; no frame: one argument
    mov di, [ss:bx+2]
    mov dx, [di]
    mov bx, [di+2]
    call OSAPI_FILE_GOTO
    sbb ax, ax                      ; CF -> 0 / -1
    ret
%endif

; int os88_file_goto_q_mark(unsigned clus, int vol) - DX = the folder's first
; cluster (0 = the root), BL = the volume (SPEC.md 74.1, 19.2.2). GOTO_Q's
; quiet stand - a word inside the current volume, a quiet mount across one -
; AND the instance moves with it, so the file call you make next resolves
; there instead of being re-stood in the folder you were launched from. 0
; moved, -1 refused (os88_ferr() says why). Bank the place you were with
; os88_file_here() first if you mean to come back; a launch folder is one
; struct os88_place, and so is a CP/M drive.
CC_GATE _os88_file_goto_q_mark
%ifdef CC_U__os88_file_goto_q_mark
_os88_file_goto_q_mark:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov bx, [bp+6]
    call OSAPI_FILE_GOTO_QM
    mov [cc_ferr], ax               ; the FERR_* on refusal, 0 on success -
                                    ; the same word every file thunk writes
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

; int os88_vol_sys(void) - BL = the volume this machine BOOTED from (SPEC.md
; 19.7): A: on a floppy machine, the installed partition on one that boots
; from its hard disk. No disk I/O and it cannot fail. BH is cleared here
; rather than assumed: the slot preserves every other register, which means
; BH comes back holding whatever this routine happened to leave in it.
CC_GATE _os88_vol_sys
%ifdef CC_U__os88_vol_sys
_os88_vol_sys:
    call OSAPI_VOL_SYS
    mov ax, bx
    mov ah, 0
    ret
%endif

%ifdef CC_HAS_FDLG
; int os88_file_dlg(int mode, void *win, const char *defname) - SPEC.md 38.
; AL = FDLG_OPEN/SAVE, BX = your window, DI = the completion trampoline, SI =
; a default name or 0. It does NOT block: it returns with the dialog already
; on screen (you were holding the lock it needed) and calls os88_onfile() much
; later. Cancel calls nothing at all. -1 = refused: one is already up.
CC_GATE _os88_file_dlg
%ifdef CC_U__os88_file_dlg
_os88_file_dlg:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov si, [bp+8]
    mov di, cc_onfile
    call OSAPI_FILE_DLG
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif
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
CC_GATE _os88_arg_file
%ifdef CC_U__os88_arg_file
_os88_arg_file:
    push bp
    mov bp, sp
    call OSAPI_ARG_FILE             ; CF=1 = launched empty, the ordinary case
    sbb ax, ax                      ; -1 empty, 0 a document - and ZF says
    jnz .out                        ; which
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
    xor ax, ax                      ; 0: here is a document (AL was the copy's)
.out:
    pop bp
    ret
%endif

; int os88_assoc_set(const char *ext, const char *stem) - SPEC.md 54.5.
; ES:SI -> 3 extension bytes then 8 stem bytes, both SPACE-PADDED. The padding
; is done here into the runtime's own 11-byte buffer, because getting it wrong
; in C - a NUL where a space belongs - claims an extension nobody will ever
; type. Declaring the association in the package header instead (54.6) needs
; no call at all, but that is an assembly-side declaration.
CC_GATE _os88_assoc_set
%ifdef CC_U__os88_assoc_set
_os88_assoc_set:
    push bp
    mov bp, sp
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
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

%ifdef CC_U__os88_assoc_set
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
%endif

CC_T_VOID _os88_batch_begin,   OSAPI_BATCH_BEGIN   ; SPEC.md 18.9.3: nests,
CC_T_VOID _os88_batch_end,     OSAPI_BATCH_END     ; and any unlock ends it

; =============================================================================
; MEMORY BEYOND THE REGION (SPEC.md 50.3)
; =============================================================================

; unsigned os88_mem_claim(int kb) - AX = KB; out DX = the base segment, CF =
; refused. You are identified by the segment you run in, so there is nothing
; to pass and nothing to forge, and it works from os88_main() - which is where
; an app sizes itself. The kernel frees your claims when your instance dies.
CC_GATE _os88_mem_claim
%ifdef CC_U__os88_mem_claim
_os88_mem_claim:
    mov bx, sp                      ; no frame: one argument
    mov ax, [ss:bx+2]
    call OSAPI_MEM_CLAIM
    sbb ax, ax                      ; CF -> -1 / 0...
    not ax
    and ax, dx                      ; ...-> 0 = refused, and nothing was
                                    ; claimed; or the segment
    ret
%endif

; int os88_mem_free(unsigned seg) - DX = a segment you were given.
CC_GATE _os88_mem_free
%ifdef CC_U__os88_mem_free
_os88_mem_free:
    mov bx, sp                      ; no frame: one argument
    mov dx, [ss:bx+2]
    call OSAPI_MEM_FREE
    sbb ax, ax                      ; CF -> 0 / -1
    ret
%endif

%ifdef CC_HAS_ONMOVE
; int os88_mem_movable(unsigned seg, int on) - DX = the claim, AX = the near
; proc or 0 (SPEC.md 66.2). `on` non-zero declares it movable and names
; cc_onmove above as the handler; 0 PINS it again, which is a legal call and
; the other half of SPEC.md 66.5.7.1's pin/unpin pair - pin a buffer for the
; length of a file call and declare it again after.
;
; 0 = the kernel took it, -1 = refused. TAKE THE ANSWER: mem_movable's fence
; is "yours, or not at all", so a segment you do not hold matches nothing,
; writes no MC_RLOC and returns CF=1 - and from inside the package that is
; indistinguishable from success (SPEC.md 66.5.6.2 is what that cost once).
CC_GATE _os88_mem_movable
%ifdef CC_U__os88_mem_movable
_os88_mem_movable:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    xor ax, ax
    cmp word [bp+6], 0
    je .set
    mov ax, cc_onmove
.set:
    call OSAPI_MEM_MOVABLE
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif
%endif

%ifdef CC_HAS_WORKER
; int os88_task_restartable(int on) - AX = cc_worker or 0 (SPEC.md 66.6.2).
;
; THE OFFSET IS NOT YOURS TO CHOOSE, and that is deliberate. The kernel
; re-enters a restarted worker with a fresh frame - DS = CS = the new segment,
; every register zeroed but DX - and the only entry in a C package that is
; correct under those conditions is `cc_worker` itself, which banks its own SP
; and pushes its own argument before calling os88_worker(). So this takes a
; flag and names the entry; a C author cannot get it wrong by taking the
; address of the wrong function, which is a mistake this SDK cannot detect
; (SPEC.md 73's rule against taking the address of an overlay function is the
; same shape one layer along).
;
; 0 = the kernel took it, -1 = refused (you are not a live package instance).
CC_GATE _os88_task_restartable
%ifdef CC_U__os88_task_restartable
_os88_task_restartable:
    push bp
    mov bp, sp
    xor ax, ax
    cmp word [bp+4], 0
    je .set
    mov ax, cc_worker
.set:
    call OSAPI_TASK_RESTARTABLE
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif
%endif

; unsigned os88_mem_regrow(unsigned seg, int kb) - DX = the claim, AX = the
; new size in KB; out DX = the claim's base NOW. ALWAYS TAKE THE ANSWER: a
; grow that had to move leaves your old segment pointing at memory that is no
; longer yours. 0 = refused, and the old claim is untouched at its old base.
CC_GATE _os88_mem_regrow
%ifdef CC_U__os88_mem_regrow
_os88_mem_regrow:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov ax, [bp+6]
    call OSAPI_MEM_REGROW
    sbb ax, ax                      ; CF -> -1 / 0...
    not ax
    and ax, dx                      ; ...-> 0 refused, or the base NOW
    pop bp
    ret
%endif

CC_T_AX   _os88_mem_largest_kb, OSAPI_MEM_AVAIL         ; AX = largest run
CC_T_REG  _os88_mem_total_kb,   OSAPI_MEM_AVAIL, bx     ; BX = total free
                                                        ; Size yourself from
                                                        ; these, never int 12h

; int os88_peek(unsigned seg, unsigned off) - one byte out of a claim.
; The other half of "C cannot write a segment override". A byte at a time is
; a near call each: right for a header, wrong for a copy (read bulk with
; os88_file_read_seg, or blit straight out of the claim with os88_gfx_blit4,
; which takes a segment of its own).
CC_GATE _os88_peek
%ifdef CC_U__os88_peek
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
%endif

; void os88_poke(unsigned seg, unsigned off, int value) - one byte in.
CC_GATE _os88_poke
%ifdef CC_U__os88_poke
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
%endif

; =============================================================================
; THE CLIPBOARD (SPEC.md 55) - one text buffer for the whole machine, which
; outlives the app that filled it. All three take no lock and do no I/O, so
; they are legal from a worker as well as from a callback.
; =============================================================================

; int os88_clip_put(const void *text, unsigned len) - ES:SI, CX. len 0 EMPTIES
; the clipboard and is not an error. -1 = refused (over CLIP_MAXKB, or the
; heap could not fund it), and then the clipboard is EMPTY rather than stale.
CC_GATE _os88_clip_put
%ifdef CC_U__os88_clip_put
_os88_clip_put:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov cx, [bp+6]
    push ds
    pop es                          ; ES:SI - the text is ours
    call OSAPI_CLIP_PUT
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

; int os88_clip_get(void *buf, unsigned cap) - ES:DI, CX; out AX = the WHOLE
; length, CX = what was copied. This returns CX, the bytes you actually got;
; ask os88_clip_size() first and grow your buffer, which on a machine with a
; claim heap is the difference between a paste that works and one that
; half-arrives.
CC_GATE _os88_clip_get
%ifdef CC_U__os88_clip_get
_os88_clip_get:
    push bp
    mov bp, sp
    mov di, [bp+4]
    mov cx, [bp+6]
    push ds
    pop es                          ; ES:DI - the buffer is ours
    call OSAPI_CLIP_GET
    mov ax, cx
    pop bp
    ret
%endif

; int os88_clip_put_seg(unsigned seg, unsigned off, unsigned len) - the same
; slot with the text in a CLAIM rather than in your own segment, the way
; os88_file_write_seg is os88_file_write's. It is what lets a package whose
; clipboard staging area is 1KB or 2KB keep those bytes out of its bss: claim,
; fill, put, free. ES is loaded from the argument and not from DS.
CC_GATE _os88_clip_put_seg
%ifdef CC_U__os88_clip_put_seg
_os88_clip_put_seg:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov es, ax                      ; ES:SI - THEIR segment, a heap claim
    mov si, [bp+6]
    mov cx, [bp+8]
    call OSAPI_CLIP_PUT
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

; int os88_clip_get_seg(unsigned seg, unsigned off, unsigned cap) - and the
; read half, out AX = the bytes actually copied.
CC_GATE _os88_clip_get_seg
%ifdef CC_U__os88_clip_get_seg
_os88_clip_get_seg:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov es, ax                      ; ES:DI - THEIR segment, a heap claim
    mov di, [bp+6]
    mov cx, [bp+8]
    call OSAPI_CLIP_GET
    mov ax, cx
    pop bp
    ret
%endif

; int os88_clip_size(void) - -1 = empty.
CC_GATE _os88_clip_size
%ifdef CC_U__os88_clip_size
_os88_clip_size:
    call OSAPI_CLIP_SIZE
    sbb cx, cx                      ; -1 empty, 0 not...
    or ax, cx                       ; ...so AX = -1, or the size
    ret
%endif

; int os88_toast(const char *text, int ticks) - SPEC.md 59. ES:SI = a NUL
; line, CX = ticks to live (0 = ~3s). An inverse-video strip at the right end
; of the MENU BAR: it costs your window no pixels, survives a repaint without
; your help, and cannot leave your incremental drawing disagreeing with your
; paint proc. The string is COPIED, so it may be scratch. Any context.
CC_GATE _os88_toast
%ifdef CC_U__os88_toast
_os88_toast:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov cx, [bp+6]
    push ds
    pop es                          ; ES:SI - ours
    call OSAPI_TOAST
    sbb ax, ax                      ; CF -> 0 / -1
    pop bp
    ret
%endif

; =============================================================================
; THE RUNTIME'S OWN HELPERS
;
; There is no C library on this floppy (SPEC.md 73.9: no printf family - it is
; thousands of bytes for the one conversion a program wanted). These six are
; the whole of it, and every one works on OUR segment only. They use the
; string instructions, which compiled C may not (73.5.1: movs/stos address
; ES:DI and ES is the kernel's) - so each one that writes through ES:DI loads
; ES from DS on the line before, and that pair is the whole of what makes it
; safe. They were a byte at a time through [si]/[di] once, on the reasoning
; that it was the version that cannot overwrite the operating system; it was
; also bigger and three times slower, and `push ds / pop es` closes the same
; hole for two bytes. Forward only, DF clear (the callback rule, above).
; =============================================================================

; void os88_memset(void *p, int c, unsigned n)
CC_GATE _os88_memset
%ifdef CC_U__os88_memset
_os88_memset:
    push bp
    mov bp, sp
    mov di, [bp+4]
    mov ax, [bp+6]
    mov cx, [bp+8]
    push ds
    pop es                          ; ES:DI - ours, stated
    rep stosb                       ; n = 0 stores nothing
    pop bp
    ret
%endif

; void os88_memcpy(void *dst, const void *src, unsigned n)
; Both ends are DS. Forward only, a byte at a time, so an overlapping copy is
; safe only when dst is below src - which is the direction a text editor
; deletes in - and does exactly what the byte loop it replaced did when it is
; not.
CC_GATE _os88_memcpy
%ifdef CC_U__os88_memcpy
_os88_memcpy:
    push bp
    mov bp, sp
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    push ds
    pop es                          ; ES:DI - ours, stated
    rep movsb                       ; n = 0 copies nothing
    pop bp
    ret
%endif

; unsigned os88_strlen(const char *s)
CC_GATE _os88_strlen
%ifdef CC_U__os88_strlen
_os88_strlen:
    mov bx, sp                      ; no frame: one argument
    mov si, [ss:bx+2]
    mov bx, si
.l:
    lodsb                           ; DS:SI - reads only, so ES is not needed
    or al, al
    jnz .l
    xchg ax, si                     ; one past the NUL...
    sub ax, bx
    dec ax                          ; ...less the start, less the NUL
    ret
%endif

; void os88_strcpy(char *dst, const char *src, unsigned cap)
; ALWAYS NUL-terminates and never writes more than cap bytes, cap including
; the NUL. cap 0 writes nothing at all.
CC_GATE _os88_strcpy
%ifdef CC_U__os88_strcpy
_os88_strcpy:
    push bp
    mov bp, sp
    mov di, [bp+4]
    mov si, [bp+6]
    mov cx, [bp+8]
    jcxz .done
    push ds
    pop es                          ; ES:DI - ours, stated
    dec cx                          ; leave room for the terminator
    jcxz .term
.l:
    lodsb
    or al, al
    jz .term
    stosb
    loop .l
.term:
    mov byte [di], 0
.done:
    pop bp
    ret
%endif

; char *os88_utoa(unsigned v, char *dst6) - decimal, no padding, returns dst.
; The buffer needs 6 bytes: 65535 plus the NUL. The digits come off the
; division in reverse, so they go on the STACK and come back off in order -
; at most five words, which is the kind of frame the 384-byte worker stack can
; afford (SPEC.md 73.8).
CC_GATE _os88_utoa
%ifdef CC_U__os88_utoa
_os88_utoa:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov di, [bp+6]
    mov si, di                      ; the answer, kept for the return
    push ds
    pop es                          ; ES:DI - ours, stated
    xor cx, cx
    mov bx, 10
.div:
    xor dx, dx                      ; DX:AX / 10 -> AX quotient, DX remainder
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    stosb
    loop .emit
    mov byte [di], 0
    mov ax, si
    pop bp
    ret
%endif

; char *os88_itoa(int v, char *dst7) - signed; the buffer needs 7 bytes for
; -32768 and the NUL. `neg` on -32768 gives 0x8000 back, which os88_utoa reads
; as 32768 - so the edge case prints correctly rather than needing a branch.
CC_GATE _os88_itoa
%ifdef CC_U__os88_itoa
_os88_itoa:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    push bx                         ; the answer
    or ax, ax
    jns .pos
    mov byte [bx], '-'
    inc bx
    neg ax
.pos:
    push bx                         ; os88_utoa(ax, bx) - cdecl, right to left
    push ax
    call _os88_utoa
    add sp, 4                       ; the CALLER cleans (SPEC.md 73.3)
    pop ax                          ; the original destination
    pop bp
    ret
%endif

%ifdef CC_HAS_PARTS
; =============================================================================
; THE PARTS STANDARD (SPEC.md 20.12), for C
;
; op_load has already run - cc_entry calls it before any C does, because the
; kernel's name pointer arrives in SI and nothing later can recover it. What
; is left is asking where the parts went, and fetching or dropping the lazy
; ones.
;
; **os88_part_lin is NOT here, and that is a decision.** An OP_XMS part's base
; is a 32-bit LINEAR address, and this C dialect has no `long` (SPEC.md 73,
; docs/C-TOOLCHAIN.md) - so it cannot be returned, and a pair of unsigned
; halves would hand a C author two numbers it has no arithmetic for. A C
; package that wants extended memory takes it through os88_xmem_* and owns the
; addressing itself; OP_XMS in a C package's table is refused by nothing, but
; op_seg answers 0 for it and there is no way to reach it from here. Say so in
; the shim rather than declaring one.
; =============================================================================

; unsigned os88_part_seg(int i) - part i's base segment; 0 = it is not here.
; A lazy part answers 0 until os88_part_fetch(i), and 0 again after a drop.
CC_GATE _os88_part_seg
%ifdef CC_U__os88_part_seg
_os88_part_seg:
    mov bx, sp                      ; no frame: one argument
    mov ax, [ss:bx+2]
    call op_seg
    ret
%endif

; THE THREE LAZY THUNKS ARE GATED ON THE TABLE (SPEC.md 20.12.9) as well as on
; the use set. They used to be a macro CC_PARTS_END emitted, because this file
; was included BEFORE the package's own table, so OP_HAS_LAZY was still 0 here
; and a gate written here would have compiled them out of a package that
; really does declare a lazy row. It is included at CC_IMAGE_END now (see the
; head of this file), after the table, so the gate can sit where the thunks
; are. Ungated they were the only thing in the tree still referencing
; op_fetch, op_drop and op_lazyok. CC_THUNK_ALL mode has no table in front of
; it, so it has none of the three.
%ifdef CC_THUNK_GATED
%if OP_HAS_FETCH                    ; a lazy row the STANDARD fetches - one
                                    ; marked OP_SELF has no op_fetch to thunk
                                    ; to (SPEC.md 20.12.9.1)
; int os88_part_fetch(int i) - claim and read a lazy part NOW. 0 = it is here
; (including "already was"), -1 = it could not be had and a toast has said why.
%ifdef CC_U__os88_part_fetch
_os88_part_fetch:
    mov bx, sp                      ; no frame: one argument
    mov ax, [ss:bx+2]
    call op_fetch
    sbb ax, ax                      ; CF -> 0 / -1
    ret
%endif

; void os88_part_drop(int i) - give a fetched lazy part back. No disk, no
; failure path, and dropping one that is not here is a no-op.
%ifdef CC_U__os88_part_drop
_os88_part_drop:
    mov bx, sp                      ; no frame: one argument
    mov ax, [ss:bx+2]
    call op_drop
    ret
%endif

; int os88_part_lazyok(int i) - would part i FIT right now? 1 = yes.
; A question, asked without claiming, for SPEC.md 47's rule that a package
; greys a fact rather than a guess. It is true of THIS INSTANT only.
%ifdef CC_U__os88_part_lazyok
_os88_part_lazyok:
    mov bx, sp                      ; no frame: one argument
    mov ax, [ss:bx+2]
    call op_lazyok
    sbb ax, ax                      ; CF=1 (no) -> -1, CF=0 (yes) -> 0...
    inc ax                          ; ...-> 0 / 1
    ret
%endif
%endif  ; OP_HAS_FETCH
%endif  ; CC_THUNK_GATED

; int os88_part_optok(void) - were the OP_OPT scratch parts granted?
; All or none (SPEC.md 20.12.4), so it is one answer for the whole table.
CC_GATE _os88_part_optok
%ifdef CC_U__os88_part_optok
_os88_part_optok:
    mov al, [op_optok]
    xor ah, ah
    ret
%endif
%endif  ; CC_HAS_PARTS

%ifdef CC_U__os88_assoc_set
; the NUL cc_padcopy parks on once its source has run out (SPEC.md 20.2: an
; ordinary byte of the image, addressed DS-relative like everything else).
cc_nul: db 0
%endif

; -----------------------------------------------------------------------------
; THE STATE ONLY SOME THUNKS USE, reserved only when one of them is assembled.
; In CC_THUNK_ALL mode the includer declares these itself (apps/loom/lmpvmod.asm
; does), exactly as it always has, so they are not declared twice.
; -----------------------------------------------------------------------------
%ifdef CC_THUNK_GATED
%assign cc__ferr 0
%ifdef CC_U__os88_ferr
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_read
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_read_seg
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_read_at
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_write
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_write_seg
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_append
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_delete
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_mkdir
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_rename
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_fcp
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_find
  %assign cc__ferr 1
%endif
%ifdef CC_U__os88_file_goto_q_mark
  %assign cc__ferr 1
%endif
section .bss
%if cc__ferr
cc_ferr:    resw 1                  ; the FERR_* of the last file call, read
%endif                              ; back by os88_ferr()
%ifdef CC_U__os88_assoc_set
cc_assoc:   resb 11                 ; os88_assoc_set()'s space-padded block:
%endif                              ; 3 extension bytes then 8 stem bytes
section .text
%endif

