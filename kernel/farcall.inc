; =============================================================================
; os8088 - farcall.inc
;
; The far-code mechanism (SPEC.md 33). A module whose code lives in .fartext
; is assembled at vstart=0, shipped at the tail of the kernel image, and
; copied down to FAR_SEG:0000 by far_init before anything else runs. Its code
; therefore costs the kernel's 64KB window nothing at all; only the shims
; below stay behind.
;
; The contract for a far module - all of it - is:
;
;   1. DS STAYS KERNEL_SEG. Far code addresses kernel variables exactly like
;      near code does, so every `[var]`, `[si+off]` and `lodsb` keeps working
;      unchanged. That is the whole reason this is cheap.
;   2. THEREFORE ALL DATA STAYS IN .text (or .bss). Strings, tables, window
;      templates - anything reached through DS - must NOT move to .fartext,
;      because DS cannot see the far segment. Only executable code moves.
;      A module's data may hold POINTERS to its far code (see rule 5).
;   3. The kernel calls in through a FARSHIM: a 6-byte near stub in .text
;      that far-calls the real body. Window procs, kind-table entries and
;      anything else the kernel dispatches to by near pointer name the shim,
;      so no dispatch site anywhere learns that the module moved.
;   4. Far code calls back into the kernel with KCALL, which far-calls a
;      4-byte `call/retf` wrapper emitted by FARK. Neither hop touches flags
;      or registers, so a routine that returns CF (wm_obscured) or a value
;      still does. Every wrapper a module needs must appear in the FARK list
;      at the bottom of this file.
;   5. Calls BETWEEN routines of the same far module stay ordinary near calls,
;      and an indirect near call through a table of .fartext labels is fine
;      PROVIDED it executes with CS = FAR_SEG - i.e. from inside far code.
;      A near pointer means nothing without knowing which CS will run it, so
;      a table of far labels may only ever be dispatched from far code.
;   6. Far bodies reached by FARSHIM end in `retf`, not `ret`.
;
; What may NOT move: anything the boot path calls before far_init has run
; (splash.inc), any interrupt handler (the vectors are seg:off into
; KERNEL_SEG and an ISR must not depend on the copy having happened), and
; anything on a hot inner loop - each crossing is a far call, roughly three
; times a near one.
; =============================================================================

; -----------------------------------------------------------------------------
; FARK - emit a kernel-segment wrapper making routine %1 far-callable
;
; 4 bytes each, in .text. `call` + `retf` preserve every register and every
; flag, so the wrapper is transparent to the callee's own contract.
; -----------------------------------------------------------------------------
%macro FARK 1
%1_k:
    call %1
    retf
%endmacro

; -----------------------------------------------------------------------------
; KCALL - from .fartext, call kernel routine %1 (which needs a FARK wrapper)
; -----------------------------------------------------------------------------
%macro KCALL 1
    call far KERNEL_SEG:%1_k
%endmacro

; -----------------------------------------------------------------------------
; FARSHIM - emit the near stub %1 in .text for far body %2 in .fartext
;
; 6 bytes each. The stub is what the rest of the kernel calls and what window
; templates and kind tables point at.
; -----------------------------------------------------------------------------
%macro FARSHIM 2
%1:
    call far FAR_SEG:%2
    ret
%endmacro

; -----------------------------------------------------------------------------
; far_init - copy the .fartext blob down to FAR_SEG:0000
;
; MUST be the first thing kmain does. The blob is loaded at kernel_text_end,
; which is also the base of .bss (see the section layout in kernel.asm), so
; the two share those addresses in turn: the moment anything writes .bss the
; blob is gone. Nothing here touches .bss, and nothing before it runs.
;
; The word count rounds up, so an odd-sized blob copies one byte past its
; end - it reads inside the loaded image and writes into free low memory.
;
; in:       nothing (DS = KERNEL_SEG)
; out:      nothing
; clobbers: flags
; -----------------------------------------------------------------------------
far_init:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov ax, FAR_SEG
    mov es, ax
    push cs                     ; source = the kernel image; this code is in
    pop ds                      ; .text, so CS is it
    mov si, kernel_text_end
    xor di, di
    mov cx, (KFAR_SIZE + 1) / 2
    cld
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The far-callable kernel surface. One 4-byte wrapper per routine that far
; code needs; adding a KCALL target means adding it here. Kept as one list so
; the cost of the mechanism is visible in a single place.
; -----------------------------------------------------------------------------
    FARK gfx_fill               ; ctrl.inc + taskmgr.inc
    FARK gfx_vline
    FARK font_str
    FARK wm_content
    FARK sched_mode_get
    FARK gfx_pixel              ; ctrl.inc only
    FARK sched_mode_set
    FARK bb_set
    FARK gfx_frame              ; taskmgr.inc only
    FARK gfx_fill_gray
    FARK gfx_fill_pat
    FARK gfx_lock
    FARK gfx_unlock
    FARK inst_ptr
    FARK wm_obscured
    FARK task_yield             ; the fake int frame it builds takes CS from
                                ; the wrapper, i.e. KERNEL_SEG, so a far
                                ; caller resumes correctly through the retf
    FARK snd_tone_route         ; ctrl.inc (Sound page, SPEC.md 31.4)
    FARK snd_route_set
    FARK osapi_snd_play
    FARK opl_wr                 ; sndfm.inc probe + init (SPEC.md 34.2)
    FARK opl_patch
    FARK sbl_dsp_wr             ; sndsb.inc probe + IRQ discovery (34.2/34.5)
    FARK sbl_dsp_rd
    FARK osapi_snd_stream       ; ctrl.inc Sound Test grant (SPEC.md 31.4)
