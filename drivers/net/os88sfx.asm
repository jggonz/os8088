; =============================================================================
; os88sfx.asm - OS88NET.COM's self-extracting wrapper
;
; WHY THIS EXISTS. OS88NET.COM is the DOS end of SPEC.md 62's parallel link
; and it is the one file on either shipped floppy that does not run on os8088
; at all: it rides the apps disk so that the user HAS it, because the link is
; how files reach these disks in the first place (see APPS_DOS in the
; Makefile). It is there for TRANSPORT. At 19,333 bytes it was also 19 of a
; 360KB floppy's 354 clusters, spent on a program that machine cannot execute,
; and that disk was down to THREE clusters free. It is ten now.
;
; So the shipped OS88NET.COM is now an archive of itself. This stub is the
; front of it; tools/os88sfx.py packs the body and documents the format. Run
; on the DOS machine it was always meant for, it unpacks the real program into
; memory and enters it - so the file is smaller on the floppy and behaves, on
; the far machine, exactly as it did before. There is no extra step to explain
; to whoever is sitting at that machine: it is still `OS88NET`, and it still
; prints its banner.
;
; WHAT IT COSTS THE FAR MACHINE, and the answer is nothing it was not already
; spending. The packed body is staged at SFX_STAGE = 0x100 + SFX_RAW, which is
; the first byte PAST the image about to be written there - measured today
; that is 0x4C85, and the staged body ends at 0x79AA. os88net.asm's own
; reserved buffers already run to net_bss1 = 0x9C0D, so every machine that
; could run the unpacked program has the room to unpack this one, with 8,803
; bytes to spare. tools/os88sfx.py asserts the ceiling and says so in a
; sentence if it is ever crossed.
;
; THE THREE THINGS THAT MAKE IT CORRECT, none of them obvious:
;
;   1. The unpacker CANNOT run where it is assembled. It writes the image over
;      0x100 upwards, and 0x100 is where this file's own code sits. So the
;      entry below lifts `sfx_body` and the packed body clear of the image
;      first and jumps to the copy. Everything from sfx_body down therefore
;      executes at an address nasm knows nothing about, which is why the only
;      control flow in it is RELATIVE (near jumps and calls keep their
;      distance wherever the block lands) and the one absolute jump - back to
;      the unpacked program - goes through a register.
;
;   2. Output can never catch the input. The image ends exactly where the
;      staged body begins, so the decoder's `di` stops at the byte before the
;      first one it has yet to read. That is not a margin that wants
;      checking; it is the two ranges being disjoint.
;
;   3. `inc si` does not touch the carry flag. sfx_bit answers in CF and
;      reloads its buffer in the middle of doing so, so a refill that clobbered
;      CF would return the wrong bit every eighth call - which decodes to a
;      plausible stream that diverges further in, i.e. exactly the failure
;      that is hardest to read off a machine in somebody else's house.
;
; THE FORMAT is LZSS with interlaced Elias-gamma lengths and offsets, one
; stream interleaving bits with whole bytes. tools/os88sfx.py is its
; specification and carries the measurements behind the choice; the summary is
;
;     0 <byte>                              literal
;     1 <gamma(len-1)> <gamma(offhi+1)> <byte offlo>
;
; with len >= 2 and off = (offhi << 8) + offlo + 1, and a tag byte fetched
; only when the bit buffer runs dry - which is what keeps every literal and
; every offset byte-aligned and this decoder under a hundred bytes.
;
; Assemble: nasm -f bin -w+error -I build/ -o os88net.com os88sfx.asm
;           ...after tools/os88sfx.py has written build/os88net.lz and its
;           .lzi, because this file %includes the one and incbins the other.
; =============================================================================

    cpu 8086                    ; the far machine may be an XT, exactly as
    org 0x100                   ; os88net.asm assumes

; SFX_RAW - the unpacked size - is GENERATED, so the two ends cannot disagree
; about where the image stops and the staged body starts.
%include "os88net.lzi"

SFX_ORG     equ 0x100           ; where DOS enters a .COM, and where the
                                ; unpacked program has to land
SFX_END     equ SFX_ORG + SFX_RAW   ; one past the image's last byte
SFX_STAGE   equ SFX_END             ; ...and so the first byte the image can
                                    ; never reach. See note 2 above.

sfx_entry:
    times ($$ - sfx_entry) db 0 ; os88net.asm's assertion, for os88net.asm's
                                ; reason: DOS enters at the FIRST BYTE of the
                                ; file, so anything that gets in front of this
                                ; label is what runs. It emits nothing and
                                ; fails the build with a negative TIMES if it
                                ; is ever untrue

    push ds                     ; DOS hands a .COM DS = ES = the PSP, and both
    pop  es                     ; movsb below want ES. Two bytes to stop
                                ; depending on it

    ; --- lift the unpacker and the packed body clear of the image ----------
    ; Backwards, because the destination is above the source. They do not
    ; overlap at today's sizes - the whole file is shorter than the image it
    ; expands to - but a copy that is correct only while that holds is a copy
    ; that breaks the day the program grows, silently, in the field.
    mov  cx, sfx_end - sfx_body
    mov  si, sfx_end - 1
    mov  di, SFX_STAGE + (sfx_end - sfx_body) - 1
    std
    rep  movsb
    cld

    mov  si, SFX_STAGE + (sfx_data - sfx_body)  ; the staged packed body
    mov  di, SFX_ORG                            ; the image, over us
    xor  bx, bx                                 ; an empty bit buffer: the
                                                ; first sfx_bit refills it
    jmp  SFX_STAGE              ; ...into the copy. Relative, and taken from
                                ; the one place that IS at its link address

; =============================================================================
; Everything below runs at SFX_STAGE and never at the address nasm gave it.
; Relative control flow only, and no `[label]` anywhere - the registers set up
; above are the entire state.
; =============================================================================
sfx_body:
.main:
    call sfx_bit
    jc   .match
    movsb                       ; a literal, straight through
    jmp  short .more
.match:
    call sfx_gamma              ; ax = len - 1
    xchg ax, dx                 ; ...banked, because the offset needs ax
    call sfx_gamma              ; ax = offhi + 1
    dec  ax
    mov  ah, al                 ; ah = offhi (<= 255: the packer checks)
    lodsb                       ; al = offlo
    inc  ax                     ; ax = the offset back from di
    mov  cx, dx
    inc  cx                     ; cx = the length...
    mov  dx, SFX_END            ; ...clamped to what the image still owes, so
    sub  dx, di                 ; a token off a truncated or damaged file
    cmp  dx, cx                 ; cannot carry di over SFX_END and into the
    jae  .len                   ; staged decompressor running this very copy
    mov  cx, dx                 ; (the packer's round-trip check is host-side;
.len:                           ; jae, because t_sfx's model has jb/jae/jz/jnz)
    push si                     ; the input stream, while si does the copy
    mov  si, di
    sub  si, ax
    rep  movsb                  ; byte at a time, so a source that reaches
    pop  si                     ; forward into this copy's own output works -
                                ; which is how one token covers a whole run
.more:
    cmp  di, SFX_END
    jb   .main

    ; --- and into the program, which is now where DOS would have put it ----
    mov  ax, SFX_ORG            ; through a register: a near jmp here would be
    jmp  ax                     ; relative to an address we are not at

; --- sfx_gamma - one interlaced Elias-gamma value, >= 1 ----------------------
; ax = 1; while (bit) ax = ax*2 + bit. The continuation bit comes FIRST, which
; is what lets the value 1 cost a single zero bit - and 1 is the commonest
; thing this decodes, being every match whose offset is under 257.
sfx_gamma:
    mov  ax, 1
.g:
    call sfx_bit
    jnc  .done
    call sfx_bit
    adc  ax, ax                 ; ax + ax + CF, in one instruction
    jmp  short .g
.done:
    ret

; --- sfx_bit - the next bit, in CF ------------------------------------------
; bx is the buffer: a byte with a 1 above it as a sentinel, so the shift that
; empties it also announces that it is empty and no counter is needed. The
; carry from that shift is meaningless and is thrown away by the refill, which
; then takes the real bit. bx = 0 on the way in therefore means "empty", which
; is why the entry above can prime it with `xor bx, bx`.
sfx_bit:
    shr  bx, 1
    jnz  .r
    mov  bl, [si]
    inc  si                     ; NOT `add si, 1` - inc leaves CF alone, and
    mov  bh, 1                  ; the shift below is what sets it
    shr  bx, 1
.r:
    ret

sfx_data:
    incbin "os88net.lz"         ; the packed body, from tools/os88sfx.py
sfx_end:

%if (sfx_end - sfx_data) != SFX_PACK
  %error "os88sfx: the payload is not the size its .lzi says - a stale build/os88net.lz beside a fresh .lzi, or the reverse"
%endif
