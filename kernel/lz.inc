; =============================================================================
; lz.inc - decompression (docs/plans/O88-COMPRESSION-PLAN.md, SPEC.md 20.13)
;
; ONE routine, two formats, and which one a file uses is a bit in its header.
; `tools/os88lz.py` is the REFERENCE implementation and this is the copy: a
; disagreement between them is a bug in one of them and never a judgement
; call, which is what tests/unit/t_lzfmt.py and the `lzdec` row exist to keep
; true.
;
;   LZ4  byte-oriented, min match 4. 50.6 cycles an output byte, measured on a
;        4.77 MHz 8088, against a `rep movsw` floor of 13.09. The default,
;        because it is the only candidate whose ratio pays for its own decode
;        time on the target machine (plan 1).
;   LZB  bit-oriented, aPLib-family: a tag BYTE carried in DL behind a guard
;        bit, byte-aligned literals and low offset byte, interlaced Elias
;        gamma for the length and the high offset byte. ~10 points better on
;        ratio and 207 cycles a byte - four times the floor. Built because the
;        360KB disk's RUNWAY may be worth that, and only a field A/B can say
;        (plan 12.7).
;
; **EVERY STREAM ENDS IN A RAW TAIL, AND THE TAIL IS WHAT MAKES IN-PLACE
; EXPANSION NEED NO MARGIN** (SPEC.md 20.13.7). The stream is
;
;     +0  word   T, the tail's length
;     +2         the LZ symbols
;     end-T      T raw bytes - the LAST T bytes of the output, stored as they
;                are
;
; and the encoder cuts the symbols at the FIRST point where (produced -
; consumed) peaks, storing everything after it raw. Two things follow, and
; both are arithmetic rather than policy. The file gets SMALLER: the suffix
; after that peak was net-expanding by exactly the old margin, so the cut
; saves what the margin used to cost. And a decoder reading its own source
; from `U - P` above its output can never overtake it: every prefix of the
; symbols now has (produced - consumed) at most what the whole stream has,
; which is exactly the slack the placement gives it, and the tail's source
; lands ON its destination. So there is no LZ_MARGIN any more, no sliding
; window for the caller whose buffer is exactly U, and no rule refusing to
; compress a file that lands near a kilobyte boundary - all three were the
; same problem, and the old design paid for it three times.
;
; The decoder finds the tail with the same compare it already made: where it
; tested `SI` against the source's END it tests it against `SEND - T`, and
; `SI` arriving there is the tail. The fast path pays nothing for it.
;
; **BOTH ARE BOUNDS-CHECKED AND THAT IS NOT OPTIONAL** (plan 7.3). Every byte
; off a disk is hostile (SPEC.md 19) and os88pkg.py is not a gate on a foreign
; file. What the bounds buy back is an invariant this loader already has for
; free: SPEC.md 21 step 6 writes `ceil(image/512)*512 <= need` bytes, "never a
; neighbour's region", because the COUNT is arithmetic the loader did before
; the read. A decompressor breaks exactly that - the count becomes whatever
; the stream says - so the invariant stops being structural and has to become
; a runtime check.
;
; **This module is `.cold` (SPEC.md 2.6)** and its in-kernel callers -
; loader.inc and diskw.inc - are cold too, so they reach it with a near call.
; The API cell comes in through the resident thunk.
;
; **IT TOUCHES NO KERNEL DATA AT ALL**, which is unusual enough to be the
; reason the API cell is a plain OSAPI_SLOT rather than an X cell: every other
; cell needs DS = KERNEL_SEG for its own variables and therefore needs a stub
; to get at the caller's segment, where this one wants BOTH segments to be the
; caller's and has nothing of its own to address. Its locals live in a stack
; frame, which SS addresses whatever DS is doing - and there is no `.bss` here
; at all, the window that used to be having gone with the margin.
;
; **THE TWO ARMS SHARE EVERYTHING BUT THEIR SYNTAX.** A format decides how a
; length, an offset and a literal are SPELLED; what is done with them - the
; output bound, the offset bound, the copy that may cross a segment, the match
; whose source is a segment below, the tail - is one body of code below the
; arms, entered by both. The first version carried two copies of all of it.
; =============================================================================

LZ_LZ4      equ 0               ; format ids, as stored in a header's format
LZ_LZB      equ 1               ; field. Mirrored by os88lz.py's LZ4/LZB and
                                ; guarded by tests/unit/t_mirror.py

; Which decoders this kernel carries (SPEC.md 20.13.6). **BOTH IS THE DEFAULT
; AND SHIPS**, and it needs no -D at all: `make COMPRESS=lz4` and
; `COMPRESS=lzb` are the single-format A/Bs and are the ones that name a
; symbol. That direction is deliberate and not a preference - a tool that
; re-assembles this kernel for a symbol map (tools/os88sym.py) passes no
; defines, so whatever the source does with NONE has to be what `make` builds,
; or every symbol lookup in the tree refuses a kernel that is perfectly fine.
%ifndef LZ_HAVE_LZ4
  %ifndef LZ_HAVE_LZB
    %define LZ_HAVE_LZ4         ; neither asked for: BOTH, and the dispatch
    %define LZ_HAVE_LZB         ; between them, which is what ships
  %endif
%endif

section .cold

; -----------------------------------------------------------------------------
; lz_decomp_x - expand one compressed block
;
; in:  DS:SI = the compressed bytes, T word first
;      ES:0  = the destination, and DI = 0. **DI MUST BE ZERO**: the offset
;              bound below reads "no match may reach back further than we have
;              produced" only when production started at 0. Every caller in the
;              tree hands over a claim's base, so this costs nothing and saves
;              an instruction in the hottest test in the routine.
;      CX    = compressed bytes. **UNDER 64KB, and SI + CX may not wrap** -
;              refused, see below
;      BX:DX = the EXACT expected output length, 32 bits (not a capacity:
;              every caller knows it - the loader from `image`, a part from its
;              table row, a file from its own header - and a stream that
;              produces a different number is a corrupt one)
;      AL    = LZ_LZ4 or LZ_LZB
; out: CF=0 and ES:DI is one past the last byte written. CF=1 = refused, and
;      nothing past the declared length was written.
; clobbers: AX BX CX DX SI DI and the flags. DS and ES come back the caller's.
; -----------------------------------------------------------------------------
; **THE OUTPUT CROSSES 64KB AND THE INPUT DOES NOT**, and the asymmetry is
; deliberate rather than an omission (SPEC.md 20.14.5). The output has to:
; BEVERLY.MOD is 116,085 bytes and compressing it is the single biggest win in
; the tree - 42,169 bytes, 36.3%, which is 145 sectors of a floppy. Splitting
; the stream into segment-sized blocks instead would have cost 18,766 of those
; bytes, 44% of the whole win, because a MOD's sample data matches back tens of
; KB into itself and a block boundary throws that history away.
;
; The input does not, because it does not have to and the check is free: a
; stream over 64KB means a file that compressed BADLY, which os88lz.py stores
; plain instead. `add cx, si / jc` is the whole enforcement, and what it buys
; is that the tail bound stays a plain 16-bit offset in one segment - it is
; tested once per symbol and is the hottest compare here.
;
; The locals are PUSHED rather than carved with `sub sp`, and the frame is
; unwound with `pop` rather than `mov sp, bp`, so the routine is push/pop
; balanced on every path and tools/stkbalance.py can walk it.
LZ_F_REM    equ 0               ; the frame, once BP is set: output bytes still
LZ_F_REMH   equ 2               ; to produce, 32 bits...
LZ_F_TSI    equ 4               ; ...where the raw tail begins: SEND - T, and
                                ; the ONLY source bound the arms test...
LZ_F_T      equ 6               ; ...the tail's length...
LZ_F_BUMP   equ 8               ; ...and 1 once the destination has crossed a
                                ; 64KB boundary, which is exactly the moment
                                ; "produced" passes what a 16-bit match offset
                                ; can reach - so the offset bound stops being a
                                ; compare at all (SPEC.md 20.14.5)

lz_decomp_x:
    push ds                     ; both segments are the caller's and both come
    push bp                     ; back: the match copies below swap DS to ES
    cld
    or di, di
    jnz .early                  ; the precondition, checked rather than assumed
    add cx, si                  ; the source end
    jc .early                   ; SI + CX wrapped: not a source this routine
                                ; will read, and a bound that wrapped is a
                                ; bound that silently means nothing
    xchg ax, bp                 ; the format, out of the way of the T read
    lodsw                       ; T: how much of the output is stored raw
    sub cx, ax                  ; ...so this is where the symbols END
    jc .early                   ; a tail longer than the stream
    cmp cx, si
    jb .early                   ; ...or one that begins inside its own T word
    push di                     ; LZ_F_BUMP = 0 - nothing has crossed yet
    push ax                     ; LZ_F_T
    push cx                     ; LZ_F_TSI
    push bx                     ; ...the expected output, high word...
    push dx                     ; ...and low
    xchg ax, bp                 ; AL = the format again
    mov bp, sp                  ; SS, not DS - which is the point of a frame
    ; EVERY ARM CHECKS AL, including the single-format builds. The cell exists
    ; in both kernels and is never %ifdef'd - SPEC.md 20.8 rule 4: a slot that
    ; is present in one build and absent in another is an ABI that depends on a
    ; knob - so a build that does not carry a format must REFUSE it rather than
    ; run the one it does have on it. The first version fell through.
    cmp al, LZ_LZB
    ja .refuse                  ; neither format
%ifdef LZ_HAVE_LZB
    je .lzb
%else
    je .refuse                  ; ...or one this build does not carry
%endif
%ifdef LZ_HAVE_LZ4
    call lz_lz4
    jmp short .out
%else
    jmp short .refuse
%endif
%ifdef LZ_HAVE_LZB
.lzb:
    call lz_lzb
    jmp short .out
%endif
.refuse:
    stc
.out:
    pop cx                      ; drop the five locals. `pop` and not `add sp`,
    pop dx                      ; and none of them touches the flags, so the CF
    pop bx                      ; the arm answered survives the unwind
    pop si
    pop di
    pop bp
    pop ds
    ret
.early:                         ; refused before the frame was built, so the
    stc                         ; unwind is the shorter one
    pop bp
    pop ds
    ret

; -----------------------------------------------------------------------------
; lz_take - claim CX bytes of the declared output (module-internal)
; out: CF=1 = there are not that many left, and nothing has been written
; clobbers: the flags only
;
; The 32-bit form of what used to be `DI + CX against the end`, and it replaces
; that test rather than adding to it: the end cannot be an offset once the
; output crosses a segment, so what is tracked is what is LEFT.
; -----------------------------------------------------------------------------
lz_take:
    sub [bp+LZ_F_REM], cx
    sbb word [bp+LZ_F_REMH], 0
    ret                         ; CF is the answer, and `ret` leaves it alone

; -----------------------------------------------------------------------------
; lz_copy - CX bytes from DS:SI to ES:DI, which lz_take has already claimed
;           (module-internal)
; out: CX = 0, SI/DI advanced, and ES or DS bumped if the copy left its segment
; clobbers: AX, CX, SI, DI, DS, ES, flags
;
; The fast path is the two carries and a `rep movsb`; a copy that would leave
; either segment goes byte by byte through lz_cross instead, which is ONCE PER
; 64KB OF OUTPUT and so is not worth an instruction of the fast path. It is
; every copy the arms make: a literal run, a match, the tail.
; -----------------------------------------------------------------------------
lz_copy:
    mov ax, di
    add ax, cx
    jc lz_cross                 ; the destination would wrap...
    mov ax, si
    add ax, cx
    jc lz_cross                 ; ...or the source would
    rep movsb
    ret

; lz_cross - the same copy, a byte at a time, bumping a segment as its offset
;            wraps (module-internal; lz_copy's slow arm)
;
; A byte loop and not the four-way minimum it used to be: the straddling
; symbol is one symbol per 64KB of output, so what this costs is a few
; hundred cycles once per segment against sixty bytes of kernel for ever.
lz_cross:
    jcxz .done
.b:
    movsb
    or di, di
    jnz .nod
    call lz_dbump               ; the destination landed exactly on the
.nod:                           ; boundary
    or si, si
    jnz .nos
    mov ax, ds                  ; ...and so did the source: 64KB on is 4,096
    add ax, 0x1000              ; paragraphs
    mov ds, ax
.nos:
    loop .b
.done:
    ret

; lz_dbump - the destination has crossed into the next segment (module
;            internal; lz_cross and LZB's literal share it)
; clobbers: AX
lz_dbump:
    mov ax, es
    add ax, 0x1000
    mov es, ax
    mov word [bp+LZ_F_BUMP], 1  ; ...and the match-offset bound stops being a
    ret                         ; compare from here on (LZ_F_BUMP)

; -----------------------------------------------------------------------------
; lz_match - copy a match of CX bytes from AX bytes back (module-internal)
; in:  AX = the offset, CX = the length; neither yet checked
; out: CF=0 done; CF=1 refused - a zero offset, one reaching below the
;      caller's buffer, or a length past the declared output
; clobbers: AX, CX, flags
;
; A match reads what has already been written, so DS becomes ES for the copy -
; and the borrow out of `DI - offset` says the source is one segment DOWN,
; where SI has already wrapped to exactly the right offset (SPEC.md 20.14.5).
; -----------------------------------------------------------------------------
lz_match:
    or ax, ax
    jz .bad                     ; offset 0 would copy from itself for ever
    cmp ax, di
    jbe .inrange                ; ...and one past what we have produced would
                                ; read BELOW the region base, which
                                ; mem_claim_hi makes a resident package's code
                                ; (SPEC.md 50.3). Once the output has crossed
    cmp word [bp+LZ_F_BUMP], 0  ; 64KB, DI is no longer that count and the
    je .bad                     ; flag is - and every 16-bit offset is inside
.inrange:                       ; the output by construction
    call lz_take
    jc .bad
    push si
    push ds
    mov si, di
    sub si, ax
    push es
    pop ds
    jnc .go                     ; `pop` leaves the flags alone
    mov ax, ds                  ; 64KB back is exactly 4,096 paragraphs, so
    sub ax, 0x1000              ; the wrapped SI needs no arithmetic of its own
    mov ds, ax
.go:
    call lz_copy                ; ...which leaves CF clear on every path: the
    pop ds                      ; two carries it tests are the last flags it
    pop si                      ; writes, and neither can be set here
    ret
.bad:
    stc
    ret

%ifdef LZ_HAVE_LZ4
; -----------------------------------------------------------------------------
; lz_lz4 - the LZ4 block format, bounded on both pointers.
; -----------------------------------------------------------------------------
lz_lz4:
.seq:
    cmp si, [bp+LZ_F_TSI]       ; the symbols end where the tail begins, and a
    jae .end                    ; stream that ran PAST that point is refused -
                                ; the compare's own flags say which, below
    lodsb                       ; the token: literal length in the high nibble,
    mov dl, al                  ; match length - 4 in the low one
    mov cl, 4
    shr al, cl
    call lz_len                 ; CX = the literal run
    jcxz .nolit
    call lz_take                ; the literals may not run past what was
    jc .bad                     ; declared, and this is where that is decided
    call lz_copy
.nolit:
    lodsw                       ; the match offset...
    mov bx, ax
    mov al, dl
    and al, 0x0F
    call lz_len                 ; ...its length...
    add cx, 4
    mov ax, bx
    call lz_match               ; ...and the copy, bounded on both
    jnc .seq
.bad:
    stc
    ret
.end:
    jne .bad                    ; SI is PAST the tail: a length ran off the end
                                ; ...and SI AT it falls into lz_tail below
%endif

; -----------------------------------------------------------------------------
; lz_tail - the raw tail, and the end of the stream (module-internal; the LZ4
;           arm falls into it and the LZB arm jumps, both with SI = LZ_F_TSI,
;           so its `ret` is the arm's)
; out: CF=0 = the stream produced exactly the declared length; CF=1 = a tail
;      past the declared output, or a stream that ended short of it
;
; The copy is `rep movsb` from the source's tail onto the output's - which in
; place is a copy of bytes onto themselves, the source having been placed
; exactly there (the module banner), and anywhere else a forward copy from
; higher to lower. Either way it is a bounded copy and nothing special.
; -----------------------------------------------------------------------------
lz_tail:
    mov cx, [bp+LZ_F_T]
    call lz_take
    jc .out                     ; ...with CF already the answer
    call lz_copy
    mov ax, [bp+LZ_F_REM]       ; a stream that ends short of the image is a
    or ax, [bp+LZ_F_REMH]       ; truncated file, not a package - and `or`
    jz .out                     ; clears CF, which is the answer when it is 0
    stc
.out:
    ret

%ifdef LZ_HAVE_LZ4
; lz_len - a token nibble AL, extended 255 at a time while it says 15 (module
;          internal; both nibbles of every token come through here)
; out: CX = the length
; clobbers: AX, CX
lz_len:
    xor ah, ah
    mov cx, ax
    cmp al, 15
    jne .done
.x:
    lodsb                       ; AH stays 0: `lodsb` loads AL alone
    add cx, ax
    cmp al, 255
    je .x
.done:
    ret
%endif

%ifdef LZ_HAVE_LZB
; -----------------------------------------------------------------------------
; lz_lzb - the bit-oriented format, bounded on both pointers.
;
; The tag byte lives in DL with a GUARD bit below it, so DL reaching zero is
; the refill signal and no register is spent on a counter - which matters,
; because every register a bit reader holds is one `rep movsb` wants.
; -----------------------------------------------------------------------------
%macro LZ_GETBIT 0
    add dl, dl
    jnz %%ok
    mov dl, [si]
    inc si                      ; `inc` leaves CF alone, by design, so the
    adc dl, dl                  ; guard shifted out above becomes the new low
%%ok:                           ; bit and CF is the byte's top bit
%endmacro

lz_lzb:
    mov dl, 0x80                ; the guard alone: the first read refills
.loop:
    cmp si, [bp+LZ_F_TSI]       ; the tail bound, as LZ4's `.seq` reads it
    jae .end
    LZ_GETBIT
    jnc .lit
    call .gamma                 ; the match length
    mov cx, ax
    call .gamma                 ; (offset >> 8) + 2
    dec ax
    dec ax
    mov ah, al
    lodsb                       ; ...and its low byte: AX is the offset
    call lz_match
    jnc .loop
.bad:
    stc
    ret
.lit:
    sub word [bp+LZ_F_REM], 1   ; lz_take's body, inline, for the one byte a
    sbb word [bp+LZ_F_REMH], 0  ; literal is here - LZB has no literal RUNS,
    jc .bad                     ; and this is the path 207 cycles a byte is
    movsb                       ; made of
    or di, di
    jnz .loop
    call lz_dbump               ; ...and one byte can land on the boundary too
    jmp short .loop
.end:
    jne .bad
    jmp lz_tail

.gamma:                         ; AX = the value, which is always >= 2
    mov ax, 1
.g:
    LZ_GETBIT
    adc ax, ax
    LZ_GETBIT
    jc .g
    ret
%endif

; -----------------------------------------------------------------------------
; lzf_decomp - the FAR entry, for the API cell (SPEC.md 2.6.1: a far entry ends
; in `retf` and is never near-called, so the two forms are two symbols).
; Everything in `.cold` reaches lz_decomp_x with a near call instead. `retf`
; leaves the flags alone, so the CF below is the answer.
; -----------------------------------------------------------------------------
lzf_decomp:             call lz_decomp_x
                    retf

section .text
