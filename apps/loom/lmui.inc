; =============================================================================
; os8088 - apps/loom/lmui.inc      LOOM's hand-written cores
;
; Part of LOOM, the in-OS IDE for the Weave family (WEAVE-SPEC 1.2).
; %included by apps/loom/loom.asm AFTER the compiled C (the C names these
; labels), AFTER os88ui.inc (this file calls into it) and AFTER
; apps/weave/wblob.inc (whose header carries the calling convention and the
; segment doctrine all of these obey - read it first; nothing is repeated
; here).
;
; WHAT IS IN HERE AND WHY EACH ONE IS NOT C.
;
;   THE ALERT and THE SCROLL BAR are here because a C file may not name an
;   nasm label. apps/weave/wui.inc's _wd_ask and apps/weave/wdraw.inc's
;   _wd_sbar/_wd_sbmove/_wd_sbhit are the shape, verbatim, and the alert's
;   completion is an ordinary near proc for SPEC.md 20's reason: a package
;   author never writes a `retf`.
;
;   THE LINE SCANNER and THE TWO MOVERS are here because they are PER-BYTE
;   LOOPS OVER A CLAIM. os88_peek() is a near call per byte - os88.h itself
;   prices it as "fine for a header, wrong for a copy" - and a 6KB source
;   scanned for its line breaks through it is ~68 ms of call overhead before
;   one pixel is drawn, on a machine where a whole row of text costs 71 ms
;   (CLAUDE.md's table). In assembly the same pass is a few hundred
;   microseconds. SPEC.md 73.11 is the general rule and this is what it looks
;   like when the data is not even in our segment.
;
; THIS FILE DOES NOT DUPLICATE apps/weave/wblob.inc. w_b/w_w/w_pb/w_pw/w_copy/
; w_pcopy are %included from there and used unchanged, which is WEAVE-SPEC
; 1.2's sharing rule and SPEC.md 20.5.1's only mechanism. What is below is
; what that file has no reason to carry: a claim-to-claim copy, a claim fill,
; a mover that handles overlap, and a scanner for a text format WEAVE never
; sees.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. A routine answering in AL clears AH itself - without that the C `int`
; carries a stale high byte, which is a plausible value and never the right
; one (apps/cword/cwtype.inc's rule 2).
;
; SS != DS, so [bp+n] addresses the STACK segment. That is right for arguments
; and wrong for anything of ours - and it is also what makes the line scanner
; below able to re-read its own `off` argument after DS has been pointed at a
; claim, which is the one place in this file the distinction is load-bearing
; rather than merely respected.
; =============================================================================

section .text

; =============================================================================
; THE ALERT (SPEC.md 75.3, os88ui.inc's OS88UI_ALERT)
; =============================================================================

; -----------------------------------------------------------------------------
; int lm_ask(const char *msg, void *win, int set)
; out: 0 it is up, -1 refused - one is already up and has been RAISED (which is
;      what that engine has instead of modality), or the window table is full.
;
; The message is a near pointer in OUR segment and is NOT COPIED: os88ui.inc
; holds the pointer and letters from it when the alert paints, so the buffer
; has to outlive the dialog. apps/loom/loom.c's lm_amsg[] is a file-scope
; array for exactly that reason.
;
; THE CALLER MUST HAVE THE GFX LOCK, which every callback does and which
; SPEC.md 75.1's negotiator does too - that is the whole reason the close
; guard can raise one.
; -----------------------------------------------------------------------------
_lm_ask:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    mov si, [bp+4]                  ; the message
    mov bx, [bp+6]                  ; our window
    mov ax, [bp+8]                  ; the button set
    mov di, lm_askdone
    call os88ui_ask
    mov ax, 0                       ; MOV: CF is the answer, and `mov` does
    jnc .ok                         ; not touch it
    dec ax
.ok:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; lm_askdone - the alert's completion (os88ui.inc's contract).
; in:  AL = the 0-based button, or OS88UI_ACANCEL for a DISMISSED alert - its
;      close box, its minimize box or Esc. SI = our window. The gfx lock is
;      HELD and the alert is already destroyed.
;
; HANDLE THE CANCEL, and here it is not a nicety: the close guard's three
; buttons are Save / Discard / Cancel, and a DISMISSED alert must mean Cancel
; and not Save. It is handed to the C as -1, which lm_alertdone() folds onto
; the Cancel arm at its one `if`.
; -----------------------------------------------------------------------------
lm_askdone:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    xor ah, ah
    cmp al, OS88UI_ACANCEL
    jne .have
    mov ax, -1
.have:
    push ax                         ; arg 1: int button (-1 = dismissed)
    call _lm_alertdone              ; C: void lm_alertdone(int)
    add sp, 2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; THE SCROLL BAR (SPEC.md 13.10, os88ui.inc's OS88UI_SCROLL)
;
; `blk` is os88ui.inc's seven-word block, which apps/loom/lmed.c fills:
;
;     +0 x1  +2 y1  +4 x2  +6 y2   the bar, ABSOLUTE and INCLUSIVE
;     +8 total   +10 fit   +12 pos
;
; Everything else - the arrow cells, the track, the thumb's height and its
; travel - is derived INSIDE os88ui.inc so a painter and a hit-tester cannot
; hold different opinions about it. lm_sbmove is the scroll: the thumb
; translates and the frame, the rules, the arrows and the untouched track stay
; exactly where they are, which is three drawing calls against the bar's
; sixteen.
; =============================================================================

; void lm_sbar(int *blk) - the whole bar, ~16 calls
_lm_sbar:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    call os88ui_sbar
    pop bx
    pop bp
    ret

; void lm_sbmove(int *blk, int oldpos) - the thumb only, 3 calls. The block
; already holds the NEW pos; os88ui_sbmove wants the old one in AX.
_lm_sbmove:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov ax, [bp+6]
    call os88ui_sbmove
    pop bx
    pop bp
    ret

; int lm_sbhit(int *blk, int x, int y) - OS88UI_SB*, 0 drawing calls. A point
; outside the bar answers OS88UI_SBNONE, so the caller may hand it a click
; that landed anywhere in the content box without a test of its own.
_lm_sbhit:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov bx, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call os88ui_sbhit
    xor ah, ah                      ; a shim answering in AL must clear AH
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; =============================================================================
; THE CLAIM MOVERS
;
; The three claims LOOM holds - the sources, the compilers' scratch workspace
; and the output image - are all outside the package's segment (they have to
; be: 24KB, 50KB and 62KB against SPEC.md 20.1's 61,440 for the whole
; program). apps/weave/wblob.inc moves bytes BETWEEN a claim and our segment;
; these three move them WITHIN one claim and BETWEEN two, which is what an
; editor's insert, a Pack's staging and lm_ofill's section padding need and
; what a bundle reader never does.
; =============================================================================

; -----------------------------------------------------------------------------
; void lm_move(unsigned seg, unsigned dst, unsigned src, unsigned n)
;
; n bytes within ONE claim, correct when the ranges overlap in either
; direction - which is the whole point, because an editor's insert is
; `move(caret+1, caret, tail)` and a delete is the same call the other way.
; apps/cword/cwmove.inc is the shape; the difference is that both ends are in
; a claim rather than in our own segment, so DS AND ES are loaded and both are
; put back.
;
; THE DIRECTION IS CHOSEN THE WAY EVERY memmove CHOOSES IT: ascending when the
; destination is BELOW the source and descending when it is above, so the
; bytes already written are never the bytes still to be read.
;
; DF IS CLEARED ON THE WAY OUT, unconditionally. The descending path sets it,
; and every other piece of code in this system - the kernel's ISRs included -
; assumes DF=0 (CLAUDE.md's register discipline). Leaving it set is the kind
; of defect that shows up somewhere else entirely.
; cc8086:allow - both segments are loaded on purpose here and both restored.
; -----------------------------------------------------------------------------
_lm_move:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    mov ax, [bp+4]                  ; SS-relative, so it is read before DS
    mov di, [bp+6]                  ; moves - and BP/SS are untouched below
    mov si, [bp+8]
    mov cx, [bp+10]
    jcxz .done                      ; 0 bytes: the descending path would
                                    ; compute a bogus tail address first
    mov ds, ax
    mov es, ax                      ; both ends are the same claim
    cmp di, si
    jbe .up                         ; dst <= src: ascending is the ONLY safe
                                    ; direction when they overlap that way
    add si, cx                      ; descending: the last byte of each range
    dec si
    add di, cx
    dec di
    std
    rep movsb
    cld                             ; DF=0 is this machine's standing state
    jmp short .done
.up:
    cld
    rep movsb
.done:
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lm_scopy(unsigned dseg, unsigned doff, unsigned sseg, unsigned soff,
;               unsigned n)
;
; n bytes from one claim into another - the scratch workspace into the output
; image, which is loom.h's lm_ocopyw() and the only cross-claim traffic this
; program has. The two claims never overlap (they are two separate
; os88_mem_claim answers), so this is a plain ascending copy.
; cc8086:allow - as above.
; -----------------------------------------------------------------------------
_lm_scopy:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    mov ax, [bp+4]                  ; the destination claim
    mov di, [bp+6]
    mov dx, [bp+8]                  ; the source claim
    mov si, [bp+10]
    mov cx, [bp+12]
    mov es, ax
    mov ds, dx
    cld
    rep movsb
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lm_sfill(unsigned seg, unsigned off, unsigned v, unsigned n)
;
; n copies of one byte into a claim: WEAVE-SPEC 2.1's "padding bytes between
; sections are 0x00", written by the bundle writer, and the scratch
; workspace's regions zeroed before a Pack. A C loop over lm_wpb() would be a
; near call per byte over as much as 50KB.
; cc8086:allow - ES is loaded for one `stosb` run and restored.
; -----------------------------------------------------------------------------
_lm_sfill:
    push bp
    mov bp, sp
    push es
    push di
    mov ax, [bp+4]
    mov di, [bp+6]
    mov dx, [bp+8]
    mov cx, [bp+10]
    mov es, ax
    mov al, dl
    cld
    rep stosb
    pop di
    pop es
    pop bp
    ret

; =============================================================================
; THE GLASS SHADOW'S COMPARATOR
; =============================================================================

; -----------------------------------------------------------------------------
; unsigned lm_shdiff(unsigned seg, unsigned off, const char *row, unsigned n)
;
; Compare the n bytes of `row` (OURS) against the n bytes at seg:off (the
; shadow, IN A CLAIM) and answer the changed SPAN: 0xFFFF if every byte is
; equal, otherwise (last << 8) | first, both 0-based column indices.
;
; WHY THE SHADOW IS IN A CLAIM AT ALL. It is 88 x 52 = 4,576 bytes - one cell
; a screen position - and as C bss it was 7.5% of SPEC.md 20.1's whole 61,440
; for image AND bss together, in a package whose compilers need every byte of
; the rest. So it lives at the end of the source claim (apps/loom/lmed.c's
; LM_SHBASE), which costs nothing resident and one routine here.
;
; WHY THAT ROUTINE HAD TO BE THIS ONE. The C could reach the shadow through
; apps/weave/wblob.inc's w_b() - but that is a cdecl call PER CELL, ~20 us on
; the target, and this compare runs over every column of every row of every
; repaint: 88 cells x 24 rows x 20 us is 42 ms added to a paint, on a machine
; where the whole point of the shadow is to save exactly that kind of
; millisecond. In assembly the loop is four instructions a byte and the whole
; pane compares in about a millisecond.
;
; TWO ANSWERS IN ONE WORD because a C function has one return value and this
; is the one place where taking an out-parameter would mean `&local` - which
; SS != DS makes meaningless and tools/cc8086.py refuses outright (SPEC.md
; 73.5). n is at most 96 here (lmed.c's LM_SH_STRIDE), so both indices fit a
; byte with 0xFF left over to mean "no difference" - which is what the 0xFFFF
; answer is made of, rather than a sentinel that could collide with a real
; span.
;
; DS STAYS OURS and ES becomes the claim: `row` is a near pointer in our own
; segment and the shadow is not, so the two operands live in different
; segments and the compare reads one of each. That is also why this cannot be
; a `repe cmpsb` - that instruction reads DS:SI and ES:DI, which IS the right
; pair, but it stops at the first difference and this needs the LAST one too.
; cc8086:allow - ES is loaded on purpose for the compare and restored.
; -----------------------------------------------------------------------------
_lm_shdiff:
    push bp
    mov bp, sp
    push es
    push si
    push di
    push bx
    mov dx, [bp+4]                  ; the claim
    mov di, [bp+6]                  ; the shadow row's offset in it
    mov si, [bp+8]                  ; the built row, in OUR segment
    mov cx, [bp+10]                 ; how many cells
    mov es, dx
    mov dx, 0FFFFh                  ; DL = first, DH = last, 0xFF = none yet
    xor bx, bx
    jcxz .done
.next:
    mov al, [bx+si]                 ; [bx+si], not [si+bx]: the 8086's base +
    cmp al, [es:bx+di]              ; index modes are bx/bp with si/di
    je .same
    cmp dl, 0FFh
    jne .havef
    mov dl, bl                      ; the FIRST changed column
.havef:
    mov dh, bl                      ; ...and the last one so far
.same:
    inc bx
    cmp bx, cx
    jb .next
.done:
    mov ax, 0FFFFh                  ; nothing changed
    cmp dl, 0FFh
    je .out
    mov al, dl
    mov ah, dh
.out:
    pop bx
    pop di
    pop si
    pop es
    pop bp
    ret

; =============================================================================
; THE LINE SCANNER
; =============================================================================

; -----------------------------------------------------------------------------
; unsigned lm_lines(unsigned seg, unsigned off, unsigned n, int *tab,
;                   unsigned max)
;
; Fill `tab` with the offset of the first byte of every line in the n bytes at
; seg:off, RELATIVE TO off, and answer how many there are. A line ends at 0x0A;
; the first line starts at 0 always, so an empty source is one line and a
; source ending in a newline has a final empty line whose start is n. Nothing
; is written past `max` entries and the answer is then exactly `max`, which is
; how apps/loom/lmed.c knows to refuse the file rather than truncate it
; (loom.h's LM_MAXLINE).
;
; THE TABLE IS OURS AND THE TEXT IS NOT, which is why this loads both segment
; registers: DS points at the claim for the scan and ES at our own segment for
; the stores. `off` is re-read out of [bp+6] AFTER DS has moved - legal
; because BP addresses SS, which is neither of them (CLAUDE.md's hard rule,
; used deliberately rather than merely obeyed).
;
; NO `scasb`. A `repne scasb` would find the next 0x0A in one instruction, but
; it scans ES:DI and ES is where the TABLE is; pointing ES at the claim would
; mean re-loading it per line. The byte loop below is 4 instructions a byte
; against `scasb`'s ~15 clocks plus the loop's own re-arm, and on the 8088 the
; fetch floor (max(clocks, 4.34 x instruction bytes), PERFORMANCE.md Part 2)
; makes the difference smaller than the segment shuffling would cost.
; cc8086:allow - both segments loaded on purpose, both restored, DF left clear.
; -----------------------------------------------------------------------------
_lm_lines:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    push bx
    mov dx, [bp+4]                  ; the claim (read before DS moves)
    mov si, [bp+6]                  ; off
    mov cx, [bp+8]                  ; n
    mov di, [bp+10]                 ; tab, in OUR segment
    mov bx, [bp+12]                 ; max
    push ds
    pop es                          ; ES = ours: the table
    mov ds, dx                      ; DS = the claim: the text
    cld
    xor dx, dx                      ; DX = how many starts recorded
    or bx, bx
    jz .done                        ; a table with no room answers 0
    mov word [es:di], 0             ; line 1 starts at 0, always
    add di, 2
    inc dx
    jcxz .done
.next:
    mov al, [si]
    inc si
    cmp al, 0x0A
    jne .cont
    cmp dx, bx
    jae .done                       ; the table is full: stop, and the answer
                                    ; is `max` exactly - lmed.c refuses on it
    mov ax, si
    sub ax, [bp+6]                  ; SS-relative: `off` survived the DS move
    mov [es:di], ax
    add di, 2
    inc dx
.cont:
    loop .next
.done:
    mov ax, dx
    pop bx
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret

section .text
