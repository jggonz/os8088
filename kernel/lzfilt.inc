; =============================================================================
; lzfilt.inc - the NEAR-CALL FILTER's inverse, for every reader of the stream
;              format (SPEC.md 20.13.9)
;
; A stream whose T word is LZ_ESC is FILTERED: the real T word follows, and
; once the whole output is out every `E8` at an output position p with two
; bytes after it carries `rel + p + 3` (mod 64KB) where the code has `rel`.
; tools/os88lz.py's bcj_encode/bcj_decode are the reference; this is the
; copy, and it is ONE BODY for its four readers - kernel/lz.inc (packages,
; drivers, modules, parts, files, OSAPI_DECOMP, OS88CZ.COM), kds_expand in
; kernel/hiber.inc (the whole-machine DOS's stub), and kxl_dec in
; kerndos/kdxpack.inc (kern_mega's low kern_dos). The boot blob's kz_expand
; is not one of them: KERNEL.SYS is never filtered (SPEC.md 20.13.9.3).
;
; WHY `E8` AND NOTHING ELSE. `repne scasb` looks for ONE byte value at 15
; cycles a byte on an 8088, and a second opcode (E9, `jmp rel16`) would need a
; second pass or a byte loop three times slower, for 9% more saving measured
; over every shipped stream (SPEC.md 20.13.9.1). MEASURED on MartyPC's 5150:
; 18.3 cycles an output byte, Note Pad's 17,610 bytes and 646 calls - the
; scan's 15 and ~89 a hit.
;
; Safe to %include more than once: kernel/lz.inc includes it, and so do the
; two stubs' hosts.
; =============================================================================
%ifndef LZFILT_INC
%define LZFILT_INC

LZ_ESC      equ 0xFFFF          ; a T word of 0xFFFF is no tail: it is the
                                ; ESCAPE that says the stream is FILTERED and
                                ; the real T follows. An older kernel reads it
                                ; as a tail longer than any stream under 64KB
                                ; and REFUSES the stream - which is what makes
                                ; the filter safe to ship without a format
                                ; code. Mirrored by os88lz.py's LZ_ESC
                                ; (tests/unit/t_lzfmt.py)
LZ_E8       equ 0xE8            ; the one opcode the filter rewrites, `call
                                ; rel16` (mirrored by os88lz.py's LZ_E8)

; -----------------------------------------------------------------------------
; LZ_UNE8 [origin] - undo the filter over ES:origin .. ES:DI-1
; in:  ES:DI = one past the last byte of the output; origin = a register (not
;      AX, CX or DI) holding the output's first offset, or nothing for 0
; out: DI unchanged
; clobbers: AX, CX, flags
;
; `p + 3` comes back off every operand the encoder put it on: SCASW leaves DI
; one past the operand, which is `origin + p + 3`, so the rewrite is
; `sub [es:di-2], di` (and the origin added back when there is one). The
; operand is then stepped over exactly as the encoder stepped over it, which
; is why the walk visits the same positions both ways.
; -----------------------------------------------------------------------------
%macro LZ_UNE8 0-1
    push di                     ; the end, which is the answer
    mov cx, di
%if %0 = 1
    sub cx, %1                  ; the output's length...
%endif
    sub cx, 2                   ; ...less two: positions 0..n-3 have an
    jbe %%x                     ; operand after them
%if %0 = 1
    mov di, %1
%else
    xor di, di
%endif
    mov al, LZ_E8
%%s:
    repne scasb
    jne %%x                     ; no E8 left
    scasw                       ; DI = origin + p + 3
    sub [es:di-2], di
%if %0 = 1
    add [es:di-2], %1
%endif
    sub cx, 2                   ; ...and the operand is not scanned
    ja %%s
%%x:
    pop di
%endmacro

%endif
