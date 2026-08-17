; =============================================================================
; os8088 - apps/cword/cwtype.inc     apps/os88type.inc, wearing a C calling
;                                    convention (SPEC.md 67.12.2)
;
; %included by apps/cword/cword.asm, after the compiled C and before
; CC_IMAGE_END. It is the second piece of assembly in this package and it
; exists for one reason: the type library (SPEC.md 6.3/6.5) is hand-written
; assembly with a register contract, and SmallerC calls cdecl. Every routine
; below is a six-to-twenty line adapter - arguments off the stack into
; registers, the answer into AX - and there is no logic in any of them. Where
; an adapter had to make a decision it is written down at the routine.
;
; THREE THINGS THIS FILE IS RESPONSIBLE FOR AND THE C IS NOT:
;
;   1. ES. The library takes a caller's text in ES:SI (SPEC.md 20.3's X stubs)
;      and a C program's strings are in DS - the package segment, because that
;      is the only segment a C program can name at all (SPEC.md 67.5.1). So the
;      shims that pass text load ES = DS and PUT IT BACK: ES arrives as
;      KERNEL_SEG on every callback and the kernel's own code expects it that
;      way (SPEC.md 20.4). This is cwmove.inc's rule in a second place.
;
;   2. The 8-bit answers. ty_getrows, ty_advof and ty_famn all answer in AL and
;      leave AH alone; a C `int` is the whole of AX, so every one of them is
;      followed by `xor ah, ah` here. Without it a function returning a face's
;      row count answers a number with the caller's last high byte in it, which
;      is a plausible value and never the right one.
;
;   3. The CARRY. The library reports refusal in CF - no face slot, a bad file,
;      a blit the kernel does not carry (SPEC.md 6.5) - and C has no carry. The
;      convention here is the one SPEC.md 67.14 already uses for the overlay's
;      doors: ZERO MEANS IT DID NOT HAPPEN. Nothing in the C tests a status
;      code, so nothing in the C can forget which way round it was.
;
; THE WHOLE LIBRARY IS RESIDENT, and that is a decision rather than an
; oversight. ty_putn, ty_flush and ty_advof ARE the redraw path, so they could
; never have gone to CWORD.OVL (SPEC.md 67.14 splits by frequency); ty_scan and
; ty_open could have, and moving them would have meant a `%ifdef` through
; apps/os88type.inc - a file apps/word and tests/facetest also include - to
; sort its own labels into two sections. The room came from this program's own
; once-per-command code instead (SPEC.md 67.12.2), which is code this package
; owns, and the side effect is worth having: THE FONT LIST WORKS ON A DISK WITH
; NO CWORD.OVL ON IT.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; void cw_ty_init(void) - face 0, and it is always open (SPEC.md 6.5)
;
; Called once from os88_main(). After it, every measuring and drawing call
; below answers for the kernel's 8x8 cell, so the C has no "is a face open"
; branch anywhere in it - which is the whole point of face 0 existing.
; -----------------------------------------------------------------------------
_cw_ty_init:
    call ty_init
    ret

; -----------------------------------------------------------------------------
; int cw_ty_rows(void)  - the current face's cell height
; int cw_ty_lead(void)  - ...and its leading
;
; The two the layout needs, and only those two. apps/os88type.inc also publishes
; the baseline row and the family count; neither is shimmed here, because a shim
; with no C behind it is code that ships in a package with under a kilobyte left
; and can never be called. cw_ty_scan() answers the count on its way out.
; -----------------------------------------------------------------------------
_cw_ty_rows:
    call ty_getrows
    xor ah, ah
    ret

_cw_ty_lead:
    call ty_getlead
    xor ah, ah
    ret

; -----------------------------------------------------------------------------
; int cw_ty_adv(int ch) - the advance of one character, in pixels
;
; THE HOTTEST CALL IN THIS FILE. The wrap sums it per character of every word
; it measures and the row walk takes it per character drawn (SPEC.md 65.13), so
; it is six instructions and no frame beyond the one cdecl forces.
; -----------------------------------------------------------------------------
_cw_ty_adv:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    call ty_advof
    xor ah, ah
    pop bp
    ret

; =============================================================================
; THE THREE PER-CHARACTER WALKS, WHICH ARE WHY THIS FILE IS NOT FOUR SHIMS
;
; SPEC.md 67.11 is binding here and it is the reason these exist: "a C package
; composes and decides; anything that touches pixels, cells or bytes per
; iteration is a hand-written proc that C calls once." A proportional layout is
; exactly that shape - the wrap sums an advance per character of every word it
; measures, the row walk records a pen per cell, and a click divides one glyph
; in half - and written in C each of them is a cdecl call, a frame and a return
; PER CHARACTER on a machine where the whole row costs 25 ms.
;
; So the C calls one of these ONCE A ROW and never once a character. Two of the
; three are apps/os88type.inc's own (ty_fit and ty_hit, which exist for this);
; the third is twelve instructions.
; =============================================================================

; -----------------------------------------------------------------------------
; int cw_ty_fit(const char *s, int n, int px) - how much of a run fits?
; out: the number of CHARACTERS that fit in px pixels, 0..n
;
; The measure the word wrap is built on. It counts characters and never bytes,
; so this program's own break rules - at a paragraph mark, at the last space
; that fits, hard at the margin for a word longer than the line - sit on top of
; it unchanged, and are the same rules in both faces.
; -----------------------------------------------------------------------------
_cw_ty_fit:
    push bp
    mov bp, sp
    push cx
    push si
    push es
    push ds
    pop es
    mov si, [bp+4]
    mov cx, [bp+6]
    mov ax, [bp+8]
    call ty_fit                     ; out CX = characters, AX = their width
    mov ax, cx
    pop es
    pop si
    pop cx
    pop bp
    ret

; -----------------------------------------------------------------------------
; int cw_ty_hit(const char *s, int n, int px) - which character is under px?
; out: the character index, 0..n
;
; ty_fit's mirror, and the half-cell of the fixed face becomes a HALF-ADVANCE:
; a click past the middle of a glyph belongs to the next character, so the
; caret lands on the nearer edge of a 4-pixel `i` and of an 8-pixel `m` alike.
; -----------------------------------------------------------------------------
_cw_ty_hit:
    push bp
    mov bp, sp
    push cx
    push si
    push es
    push ds
    pop es
    mov si, [bp+4]
    mov cx, [bp+6]
    mov ax, [bp+8]
    call ty_hit                     ; out CX = the index
    mov ax, cx
    pop es
    pop si
    pop cx
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cw_ty_pen(const char *s, int n, int *px) - the row's own pixel map
;
; px[i] = the pen character i starts at, relative to the row's left edge, and
; px[n] = the pen AFTER the last one - one slot past the end, so that a span's
; right edge is a lookup rather than a special case (SPEC.md 65.13). It is the
; only new per-row state a chosen face needs, and in the fixed face the C never
; reads it at all.
;
; ONE CALL A ROW. This is the accumulate walk of SPEC.md 65.13 with the cell
; index COUNTED rather than derived from the pen: there is no divisor in a
; proportional face, which is the one thing the fixed-pitch arithmetic could
; not be carried over.
; -----------------------------------------------------------------------------
_cw_ty_pen:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [bp+4]                  ; the text - ours, so DS reaches it
    mov cx, [bp+6]                  ; its length
    mov di, [bp+8]                  ; the map
    xor dx, dx                      ; the pen
.c:
    mov [di], dx
    inc di
    inc di
    jcxz .out                       ; ...having written the slot past the end
    mov al, [si]
    inc si
    call ty_advof
    xor ah, ah
    add dx, ax
    dec cx
    jmp short .c
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; int cw_ty_use(int handle, int style) - make a face and style current
; out: 1 = it is, 0 = no such face or that style is absent and NOTHING changed
; -----------------------------------------------------------------------------
_cw_ty_use:
    push bp
    mov bp, sp
    mov al, [bp+4]                  ; handle
    mov ah, [bp+6]                  ; style
    call ty_use
    mov ax, 1
    jnc .out
    xor ax, ax
.out:
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cw_ty_cache(void) - the pre-shifted glyph table for the current face
;
; 95 glyphs x 4 pen phases x the face's rows, each a word already shifted and
; already complemented, so the compose becomes three instructions with no shift
; in it: ~160 ms once - a third of one int 13h - and 2.01x off every line drawn
; afterwards (SPEC.md 6.5.2, PERFORMANCE.md Set 62).
;
; ITS REFUSAL IS NOT AN ERROR AND THE CALLER DOES NOT SEE IT. The table is a
; 12 KB heap claim, so a small machine can decline it; ty_putn then takes the
; general path, which draws THE SAME PIXELS more slowly. There is nothing for a
; caller to do about that and nothing to tell the user, so this answers
; nothing - the one place in this file where a carry is dropped on purpose.
; -----------------------------------------------------------------------------
_cw_ty_cache:
    call ty_cache
    ret

; -----------------------------------------------------------------------------
; void cw_ty_band(int rows) - clear the band to paper
; -----------------------------------------------------------------------------
_cw_ty_band:
    push bp
    mov bp, sp
    push dx
    mov dx, [bp+4]
    call ty_band
    pop dx
    pop bp
    ret

; -----------------------------------------------------------------------------
; int cw_ty_putn(int pen, const char *s, int n) - set a run into the band
; in:  pen = pixels from the band's left edge
; out: the pen after the run
;
; The text is OURS, so ES = DS for the call and back afterwards - rule 1 at the
; head of this file.
; -----------------------------------------------------------------------------
_cw_ty_putn:
    push bp
    mov bp, sp
    push cx
    push si
    push es
    mov ax, [bp+4]                  ; the pen
    mov si, [bp+6]                  ; the text
    mov cx, [bp+8]                  ; its length
    push ds
    pop es
    call ty_putn
    pop es
    pop si
    pop cx
    pop bp
    ret

; -----------------------------------------------------------------------------
; int cw_ty_flush(int x, int y, int w, int rows) - the band onto the glass
; in:  x and w are multiples of 8; the gfx lock is held by the caller
; out: 1 = drawn, 0 = the blit refused and NOTHING was drawn
;
; ZERO IS A REAL ANSWER ON A REAL MACHINE, not a defensive branch: a
; kern_small kernel carries the GFX_BLIT1 slot and not its body (SPEC.md 6.5),
; and the caller letters that row in the 8x8 face instead.
;
; THE `clc` IS LOAD-BEARING AND IT COST A ROUND OF TESTING TO FIND. ty_flush
; opens with `call OSAPI_GFX_PEN` / `jnc .live` / `call ty_gray`, which reads
; like a QUERY of the disabled pen and is not one: OSAPI_GFX_PEN is a SETTER
; whose argument IS the carry flag, and the cell preserves every flag, so the
; carry that comes back is the carry that went in. ty_flush therefore dithers
; the band whenever its CALLER happened to arrive with CF set - and a compiled
; C call site arrives with whatever the last instruction of the argument setup
; left there, which is neither clear nor stable. The symptom is a run of the
; document drawn as a 50% checkerboard, exactly as a greyed control is (SPEC.md
; 47), on some runs and not others: it turned up here on a run that was bold
; AND underlined and on nothing else.
;
; So the carry is set explicitly, which is what os88api.inc's own note about
; this slot tells a caller to do - "use `clc` or `stc` first for the
; unconditional cases". Document text is never a disabled control, so it is
; always `clc`. apps/word reaches the same routine from assembly and has the
; same exposure; nothing here can fix that for it.
; -----------------------------------------------------------------------------
_cw_ty_flush:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    clc                             ; see above - NOT decoration
    call ty_flush
    mov ax, 1
    jnc .out
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; int cw_ty_scan(void) - what typefaces does this MACHINE have? (SPEC.md 19.8)
; out: the number of families listed, 0 = none, or the system disk could not be
;      reached. The caller runs it ONCE whatever the answer.
; -----------------------------------------------------------------------------
_cw_ty_scan:
    call ty_scan
    call ty_famn
    xor ah, ah
    ret

; -----------------------------------------------------------------------------
; int cw_ty_open(int family) - open family i off the system disk
; out: the handle (1..3), or 0 - refused, and the caller keeps the face it had
; -----------------------------------------------------------------------------
_cw_ty_open:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    call ty_openfam
    jc .no
    xor ah, ah
    pop bp
    ret
.no:
    xor ax, ax
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cw_ty_name(int i, char *dst) - family i's display name into dst
;
; ty_famname ANSWERS IN SI, and the C wants bytes it can letter. Copying here
; rather than handing the pointer back is deliberate, and it is SPEC.md 65.13's
; own bug written down: the assembly port's first Font handler returned that
; pointer where a window record belonged and the machine followed it into a
; hard hang, reproducible on the pick and on nothing else. A pointer into the
; library's private table is not a thing this program should be able to hold.
;
; TY_DISPSZ bytes, which is that table's own stride, and the last is forced to
; NUL so a name that filled the field cannot run on into the next one.
; -----------------------------------------------------------------------------
_cw_ty_name:
    push bp
    mov bp, sp
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov ax, [bp+4]
    call ty_famname                 ; SI = the name
    mov di, [bp+6]
    mov cx, TY_DISPSZ - 1
    cld
.c:
    lodsb
    stosb
    or al, al
    jz .done
    loop .c
    mov byte [es:di], 0
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bp
    ret

; -----------------------------------------------------------------------------
; void cw_ty_close(int handle) - give a face back
;
; A face is closed on the way to opening the next one, so this and cw_ty_open
; are a pair and the claim cannot be stranded between them (SPEC.md 50.2).
; -----------------------------------------------------------------------------
_cw_ty_close:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    call ty_close
    pop bp
    ret

; =============================================================================
; The library itself, and the bss it is given (SPEC.md 6.3's TY_BSS contract).
; It goes AFTER the shims so that nothing above can fall through into it, and
; it leaves the assembler in .text, which CLAUDE.md's section discipline
; requires of every include in this tree.
; =============================================================================
%include "os88type.inc"

section .bss
cw_ty_bss:  resb TY_BSS_SIZE        ; inside .bss and before CC_IMAGE_END, so
                                    ; crt0's cc_bss_end counts it and the
                                    ; loader zeroes it with everything else
section .text
    TY_BSS cw_ty_bss
