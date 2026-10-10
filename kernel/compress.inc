; =============================================================================
; compress.inc - THE LZB COMPRESSOR (SPEC.md 20.15), an on-demand module
; =============================================================================
; The one thing in this tree that COMPRESSES on the 8088. Everything else here
; decodes: the loader, the file read, the boot blob. This is the file manager's
; Compress verb (SPEC.md 22.22) AND its Uncompress (22.23): both verbs, whole,
; and nothing else calls it.
;
; **IT IS A MODULE FOR THE REASON EVERY MODULE IS ONE** (SPEC.md 2.8): a verb
; is a deliberate act somebody waits for, and a machine that never uses it
; should carry none of it. `mod_need` claims the image, the verb runs, and
; `mod_drop` gives the whole of it back. The resident half is a `mod_tab` row,
; a file name, one `mod_fp` block and files.inc's one thunk.
;
; **IT IS A HASH-CHAIN MATCHER, DEPTH 16, OVER A 16,384-BYTE WINDOW**, and that
; is a correction to docs/plans/O88-COMPRESSION-PLAN.md 13.11.0 rather than what it
; recommended. That measurement rejected chains because a `prev` array is two
; bytes per input byte - 128KB for a 64KB file - which is true of a chain over
; the WHOLE file and not of one over a window: 13.11.0's own 12.3.1 had already
; said so, and the arithmetic is 2 x 16,384 = 32KB whatever the file is. What
; the two parses actually measure at, over the seven files of 13.11.0:
;
;   one candidate per slot, lazy    74.5%      (13.11.0's recommendation)
;   chains, depth 16, no lookahead  68.9%      (this)
;   the host's shortest-path parse  63.9%      (os88lz.lzb_compress)
;
; and against the LZ4 those files ship as, this is **3.9 to 10.3 points
; better** where 13.11.0's parse was 0.8 to 6.9. THE LOOKAHEAD IS GONE WITH IT:
; at equal work a deeper chain beats a shallower one with a lookahead on every
; budget measured, so `lazy` is dominated rather than merely marginal.
;
; **THE TABLES ARE THE CALLER'S BLOCK**, 8KB of `head` and 2 x window of
; `prev`, and that is not a register trick - it is where the memory belongs. A
; module's image is its FILE (mod_check: `the image IS the file`), so a table
; inside it would be 8KB of zeros on the system disk, written by the build and
; read off the floppy at every use, to be overwritten by `rep stosw` before the
; first byte is hashed. The verb claims one block and slices it, and the WINDOW
; is its dial: it drops until the claim fits, so a 128KB machine runs the same
; encoder over less history rather than a different one.

CMZ_HBITS   equ 12              ; head[] - 4,096 entries, 8KB, at the block's
CMZ_HSIZE   equ 1 << CMZ_HBITS  ; offset 0
CMZ_PREV    equ CMZ_HSIZE * 2   ; ...and prev[] immediately after it, two bytes
                                ; per WINDOW position (not per input byte -
                                ; that is the whole correction above)
CMZ_NIL     equ 0xFFFF          ; an empty head slot

CMZ_DEPTH   equ 16              ; chain candidates tried per position. The
                                ; curve flattens here: depth 8 is 0.29 points
                                ; worse for 23% less work and depth 24 is 0.07
                                ; better for 12% more

CMZ_MIN     equ 3               ; the shortest match this encoder will take.
                                ; LZB's FORMAT minimum is 2 (LZB_MIN in
                                ; os88lz.py) and the host's parse uses it, but
                                ; a two-byte match needs a two-byte hash
CMZ_SLACK   equ 16              ; how far past [cmz_lim] one pass can write.
                                ; The worst pass is a match: one tag bit, two
                                ; gammas of at most 30 and 16 bits, and one raw
                                ; byte - 47 tag bits is 7 tag bytes, plus the
                                ; raw one is 8. Sixteen is that doubled, and it
                                ; is what lets `.next` bound the WRITE once a
                                ; pass instead of at every store

CMZ_MAXM    equ 0x3FF0          ; the longest match this encoder emits. The
                                ; FORMAT has no such limit; the slide does -
                                ; SI is under 32KB at every probe, and this
                                ; keeps every byte a probe or a hash reads
                                ; under 48KB, which is the streamed window
CMZ_SBUF    equ 0xC000          ; ...that window, 48KB (SPEC.md 22.22.5)
CMZ_OBUFK   equ 33              ; the streamed OUTPUT window, KB: 32KB and
                                ; the slack one symbol writes past it, and the
                                ; paragraph the 'CZ' header sits in
CMZ_STRK    equ CMZ_SBUF/1024 + CMZ_OBUFK + CMZ_PREV/1024 ; ...and the claim
                                ; less prev[] - 89KB

CMZ_PSTEP   equ 512             ; source bytes between progress reports - the
                                ; widget's own unit (SPEC.md 12.8.1), so a
                                ; report is a compare until the bar's pixel
                                ; extent actually moves
CMZ_XPROG   equ apic_fpg_stepb          ; OSAPI_FS_PROG. The cloner's idiom (SPEC.md
                                ; 18.99.4): an OSAPI_SLOT is
                                ; `push ds / push cs / pop ds / call <body> /
                                ; pop ds / retf`, which is a far-callable
                                ; wrapper around a near proc that fixes DS
                                ; ITSELF - so this module can report progress
                                ; with the SOURCE in DS and needs neither a
                                ; cw_ shim nor a callback in its own ABI

CMZ_XTOAST  equ apic_toast_show          ; OSAPI_TOAST, and it is what lets this
                                ; image carry its own VERDICT STRINGS: the
                                ; slot takes ES:SI rather than DS:SI (the text
                                ; is often in a heap claim, and a module image
                                ; IS one), so five refusals that used to be
                                ; 89 bytes of kernel `.text` are 89 bytes of
                                ; this file instead

CMZ_WMAX    equ 16384           ; the compressor's window, largest first: 2
CMZ_WMIN    equ 1024            ; bytes of prev[] each (SPEC.md 20.15.1)
CMZFP       equ CLFP + 4        ; the cloner's SECOND entry (SPEC.md 20.15.3):
                                ; this whole file is part of CLONE.DRV's
                                ; image, so it costs no mod_tab row, no file
                                ; name and no mod_fp block of its own - one
                                ; far-pointer slot, in a block that had six
                                ; spare

; CMZ_TRACK 1/0 - let the pointer track through the parse, or stop it (SPEC.md
; 22.22.6). It is SPEC.md 7.4's [cur_inxfer] and 7.4's argument: while it is
; set and this task holds the gfx lock, the mouse ISR draws the arrow itself,
; which is safe exactly as long as nothing on this task is inside a drawing
; primitive. The parse draws NOTHING - so the flag is up across it and down
; around every progress step, every re-arm and every read and write (which
; bracket their own int 13h and clear it after). Through DS because the byte
; is kernel `.text` and the parse's DS is the source; `NOCURDISK=1` compiles
; it away with the kernel's own two macros.
;
; A CALL and not the store inline: fifteen sites at 14 bytes each were 210
; bytes of this image, and cmz_trk0/cmz_trk1 below are 22 with their word.
; No site is per byte - the parse's own is per CMZ_PSTEP of source, around a
; far call into the progress widget - and every register and flag still
; comes back, the store being `mov`s and pushes alone.
; THE STREAMED COMPRESS IS kern_big's ALONE (SPEC.md 22.22.5): its ending
; truncates through OSAPI_FILE_WRITE_AT, a door kern_small has not got
; (18.4.7.4), so cmz_route sent every file WHOLE there and cmz_mode was never
; anything but 0. Everything only the stream reaches - the route itself,
; cmz_stream, the source and output windows' disk halves, the pass's reads and
; writes and its unwind - is assembled on kern_big alone, not carried as dead
; code in kern_small's image.
%ifdef KERN_BIG
%define CMZ_STREAM
%endif

%macro CMZ_TRACK 1
%ifndef NOCURDISK
    call cmz_trk%1
%endif
%endmacro

section .modl                   ; the cloner's image (SPEC.md 2.8, 18.99) -
                                ; clone.inc has already laid the header, and
                                ; kernel.asm's modl_end comes after this

%ifndef NOCURDISK
; -----------------------------------------------------------------------------
; cmz_trk0 / cmz_trk1 - CMZ_TRACK's body: [cur_inxfer] = 0 / 1, through a
; DS of KERNEL_SEG fetched from this image (8086 has no `push imm`, and a
; segment register loads from memory without a scratch register). Every
; register and every flag preserved.
; -----------------------------------------------------------------------------
cmz_trk0:
    push ax
    mov al, 0                   ; `mov` and not `xor`: the flags come back
    jmp short cmz_trkc
cmz_trk1:
    push ax
    mov al, 1
cmz_trkc:
    push ds
    mov ds, [cs:cmz_kseg]
    mov [cur_inxfer], al
    pop ds
    pop ax
    ret
cmz_kseg:   dw KERNEL_SEG
%endif

; -----------------------------------------------------------------------------
; cmz_pack - LZB-compress one buffer (SPEC.md 20.15.1)
; in:  AX = the SOURCE segment, the source at AX:0000
;      DX = the OUTPUT segment, the output at DX:0000, room for SI:CX bytes
;      BX = a scratch segment: CMZ_PREV + 2*(DI+1) bytes at BX:0000
;      DI = the window's MASK - a power of two less one, 1,023 to 16,383
;      SI:CX = the source's length, 32 bits (SPEC.md 20.15.4)
;      [cs:cmz_mode] = 0 WHOLE: the source is all in memory and the stream is
;         left whole at DX:0. 1 is STREAMED (SPEC.md 22.22.5): the source is
;         fm_onam, read through a CMZ_SBUF window at AX:0, and the output a
;         33KB window at DX:0 written to fm_ebuf as it slides, 'CZ' header
;         (already at DX:-8) first - and at the end taken back to the cut,
;         truncated if it had to be, given the tail and its T word
; out: CF=0, DX:AX = the packed length (which is < SI:CX): a whole stream, T
;         word and raw tail included (SPEC.md 20.13.7). [cs:cmz_ksa] is the
;         cut in the source and [cs:cmz_tl] the tail's length
;      CF=1, AX = 0: it did not get smaller, and the output is undefined
;         (STREAMED, the container counts, and the new file is the caller's
;         to delete)
;      CF=1, AX = 1: the raw tail would be over 64KB, which the T word cannot
;         count - the file's last 64KB do not compress
;      CF=1, AX = 2: a streamed read or write failed, FERR_* in [cs:cmz_err]
; clobbers: AX BX CX DX SI DI, flags. DS, ES and BP come back.
;
; SEGMENTS RATHER THAN POINTERS, because the far pointer this is reached
; through lives in `mod_fp` - kernel `.bss` - so a caller whose DS was already
; the source could not have made the call at all.
;
; THE BAIL IS THE ONLY BOUND THE OUTPUT NEEDS. A stream that reaches the
; source's length has already lost - the caller would store the file plain - so
; the write pointer is tested against the input's length and nothing else, and
; the caller's buffer is exactly that big.
;
; **NEITHER SIDE IS ONE SEGMENT** (SPEC.md 20.15.4). SI and DI stay 16-bit
; offsets, and each is SLID back into the low half of its segment when it
; passes 32KB, with the segment moved up to match: the source by a multiple of
; 16KB, so that `position & mask` - the prev[] slot - is the same number
; before and after for every window this verb can choose; the output by
; exactly 16KB. What a slide costs is one pass over head[] and prev[], taking
; the same constant off every position in them - once per 16KB of input,
; against a parse that costs ~1,600 cycles a BYTE. Everything that is a
; length rather than an offset - the source's end, the bail limit, the lead
; the cut is chosen by - is carried 32 bits wide and re-armed as a 16-bit
; compare after each slide, so the loop tests exactly what it tested before.
; -----------------------------------------------------------------------------
cmz_pack:
    push ds
    push es
    push bp
%ifdef CMZ_STREAM
    mov [cs:cmz_sp], sp         ; where a failed streamed read or write
%endif                          ; unwinds to, from however deep (cmz_ioerr)
    mov bp, ax                  ; the source segment, out of AX's way: the
    cld                         ; head clear below wants AX for the fill
    mov [cs:cmz_mask], di
    mov [cs:cmz_tseg], bx
    mov [cs:cmz_oseg], dx
    mov [cs:cmz_sseg], ax

    or si, si
    jnz .long
    cmp cx, CMZ_SLACK
    jbe .no                     ; nothing this short compresses, and the limit
.long:                          ; below would wrap
    ; --- the 32-bit state, ONE BLOCK in this order (asserted at the data):
    ;     the whole source, for the verdict at the end; its END from the
    ;     source's current segment - the same number until the first slide;
    ;     where the parse STOPS, the same end again; the bail, from the
    ;     output's current segment; then (source base - output base), 0 until
    ;     something slides; the two bases themselves, which turn an offset
    ;     into a position in the file; and the cut - nothing has beaten a
    ;     lead of 0 yet, so the whole file would be tail, its stream end at 2.
    ;     Stored with stosw through ES = CS: ES and DI are reloaded for the
    ;     head clear below and SI is zeroed for the parse, so all three are
    ;     free - and DX is NOT, it is the output segment until `mov es, dx`
    push cs
    pop es
    mov di, cmz_nabs
    mov ax, cx
    stosw                       ; cmz_nabs
    xchg ax, si
    stosw
    xchg ax, si
    stosw                       ; cmz_rn
    xchg ax, si
    stosw
    xchg ax, si
    stosw                       ; cmz_rstop
    xchg ax, si
    stosw
    xchg ax, si
    sub ax, CMZ_SLACK
    sbb si, 0
    stosw                       ; cmz_rlim
    xchg ax, si
    stosw
    xor ax, ax
    mov cx, (cmz_koa - cmz_dl) / 2
    rep stosw                   ; cmz_dl, cmz_sbase, cmz_obase, cmz_mx, cmz_ksa
    inc ax
    inc ax
    stosw                       ; cmz_koa = 2
    dec ax
    dec ax
    stosw

    ; --- head[] starts EMPTY, and prev[] DOES NOT NEED TO -------------------
    ; A head slot left from the last file names a position in a buffer that is
    ; not this one, and nothing below would reject it - the match is verified
    ; byte for byte, so the stream stays correct, but the parse is worse for no
    ; reason. Clearing costs 4,096 stores once.
    ;
    ; prev[] is a different question and the answer is that it is SAFE stale.
    ; An entry is only read for a candidate the window test has already
    ; accepted, and prev[p] can only have been overwritten by a position
    ; p + k*window - which, being below SI, puts p itself outside the window.
    ; So every entry this walk reads was written by an insert of its own
    ; position, and 32KB of stores are not made. A slide leaves that true: it
    ; moves every entry by the same amount, garbage included.
    mov es, bx
    xor di, di
    mov cx, CMZ_HSIZE
    mov ax, CMZ_NIL
    rep stosw

    mov ds, bp                  ; the source...
    mov es, dx                  ; ...and the output
    mov byte [cs:cmz_tbit], 0   ; no tag byte open yet
    mov word [cs:cmz_nextp], CMZ_PSTEP
%ifdef CMZ_STREAM
    cmp byte [cs:cmz_mode], 0
    je .armed
    xor ax, ax                  ; STREAMED: the window's first CMZ_SBUF bytes
    mov [cs:cmz_sfill], ax      ; off the disk, from offset 0
    mov [cs:cmz_sfill+2], ax
    mov [cs:cmz_scur+RSQ_GEN], ax   ; ...and a new file: its cursor unseeded
    xor bx, bx
    mov cx, CMZ_SBUF
    call cmz_sfetch
.armed:
%endif
    call cmz_sarm
    call cmz_oarm
    CMZ_TRACK 1                 ; the pointer moves from here to the end
    xor si, si                  ; the read cursor...
    mov di, 2                   ; ...and the write cursor, past the T word
                                ; that .done fills in

.next:
    ; --- THE CUT, found as the stream is written (SPEC.md 20.13.7) ----------
    ; A decoder expanding in place reads from R and writes from 0, so it needs
    ; R >= (produced - consumed) at every instant. THIS is every instant that
    ; matters: inside a symbol the decoder consumes its bytes and only then
    ; produces, so (produced - consumed) peaks at the symbol's END - which is
    ; here, with SI what the decoder will have produced and DI what it will
    ; have consumed. The stream is cut at the FIRST such peak and the source
    ; from there on is stored raw, which is what makes R = U - P enough with
    ; no margin at all - and it makes the file SMALLER, the symbols it drops
    ; having been net-expanding by exactly what the margin used to be.
    ; Strictly greater and never negative, because os88lz.lzb_compress_machine
    ; mirrors this compare and tests/lzcomp.py compares the streams. 32 bits,
    ; because the two cursors are offsets from segments that slide apart.
    mov ax, si
    sub ax, di
    sbb dx, dx                  ; DX:AX = SI - DI, sign-extended...
    add ax, [cs:cmz_dl]
    adc dx, [cs:cmz_dl+2]       ; ...plus the bases' difference: the lead
    js .nomx                    ; still behind: the T word and the first tag
    cmp dx, [cs:cmz_mx+2]       ; byte precede the first literal, so this is
    jb .nomx                    ; negative early on
    ja .newmx
    cmp ax, [cs:cmz_mx]
    jbe .nomx
.newmx:
    mov [cs:cmz_mx], ax
    mov [cs:cmz_mx+2], dx
    mov ax, si                  ; the cut, as POSITIONS IN THE FILE: either
    xor dx, dx                  ; side may slide past it before the end, and
    add ax, [cs:cmz_sbase]      ; a streamed window no longer holds it at all
    adc dx, [cs:cmz_sbase+2]
    mov [cs:cmz_ksa], ax
    mov [cs:cmz_ksa+2], dx
    mov ax, di
    xor dx, dx
    add ax, [cs:cmz_obase]
    adc dx, [cs:cmz_obase+2]
    mov [cs:cmz_koa], ax
    mov [cs:cmz_koa+2], dx
.nomx:
    cmp si, [cs:cmz_nextp]      ; ...and the bar, on the same boundary
    jb .noprog
    call cmz_prog
    add word [cs:cmz_nextp], CMZ_PSTEP
    jnc .noprog
    mov word [cs:cmz_nextp], 0xFFFF ; a 64KB source runs the counter off the
.noprog:                        ; end; parking it here costs one extra report
                                ; where a wrap would report at every position
    cmp si, [cs:cmz_nchk]
    jae .send                   ; the end - or 32KB, and a slide
    cmp di, [cs:cmz_lim]
    jae .oend                   ; the bail - or 32KB, and a slide

    call cmz_probe              ; AX = the match length, DX = its offset
    or ax, ax
    jz .lit

    ; --- the match: a 1 bit, gamma(len), gamma((off>>8)+2), off's low byte ---
    stc                         ; (os88lz.py's lzb_compress, in that order)
    call cmz_bit                ; cmz_bit and cmz_gamma both keep AX and DX,
    call cmz_gamma              ; so AX is still the length and DX the offset
    xchg ax, dx                 ; AX = the offset, DX = the length
    push ax
    mov al, ah                  ; off >> 8 - under 0x40, the offset being at
    cbw                         ; most the window's mask, so CBW zeroes AH
    inc ax
    inc ax                      ; (off >> 8) + 2, which is >= 2 by
    call cmz_gamma              ; construction - a gamma cannot code 1
    pop ax
    stosb                       ; ...and the offset's low byte, raw
    xchg ax, dx                 ; AX = the length again
    call cmz_fill               ; every position the match covers goes in
    add si, ax
    jmp .next

.lit:
    clc
    call cmz_bit
    mov al, [si]
    stosb
    mov ax, 1                   ; ...and this one position goes in, through
    call cmz_fill               ; the same routine: a literal is a match of
    inc si                      ; length one as far as the table cares
    jmp .next

.send:
    or si, si
    jns .done                   ; under 32KB: this IS the end
    call cmz_sslide
    jmp .next
.oend:
    cmp word [cs:cmz_rlim+2], 0
    jne .oslide
    cmp di, [cs:cmz_rlim]
    jae .no                     ; it is not going to get smaller
.oslide:
    call cmz_oslide
    jmp .next

    ; The refusals, HERE and not after the tail: .oend and all three of
    ; .done's tests reach them with a short jcc from here (size pass 8)
.tail:
    mov ax, 1
    jmp short .fail
.no:
    xor ax, ax
.fail:
    CMZ_TRACK 0                 ; ...on every way out, cmz_ioerr's included
    pop bp
    pop es
    pop ds
    stc
    ret

.done:
    ; --- the tail: the symbols end at the cut, and the source from there on
    ; --- goes across as it is, behind the T word (SPEC.md 20.13.7). A tag byte
    ; --- opened before the cut may carry bits of symbols after it: the decoder
    ; --- stops reading bits at the cut, so they are never seen
    mov ax, [cs:cmz_nabs]
    mov dx, [cs:cmz_nabs+2]
    sub ax, [cs:cmz_ksa]
    sbb dx, [cs:cmz_ksa+2]      ; T
    or dx, dx
    jnz .tail                   ; a tail the T word cannot count
%ifdef CMZ_STREAM
    mov [cs:cmz_tl], ax         ; (the streamed ending's: the whole one has CX)
%endif
    mov cx, ax
    mov ax, [cs:cmz_koa]        ; the symbols' length, T word included...
    mov dx, [cs:cmz_koa+2]
    add ax, cx
    adc dx, 0                   ; ...and the whole stream's
%ifdef CMZ_STREAM
    cmp byte [cs:cmz_mode], 0
    je .cmp
    add ax, DSK_CZ_HDR          ; STREAMED, the verb cannot look at the answer
    adc dx, 0                   ; before the file is written, so the container
.cmp:                           ; is counted here: a plain source's P is U
%endif
    cmp dx, [cs:cmz_nabs+2]
    ja .no
    jb .less
    cmp ax, [cs:cmz_nabs]
    jae .no
.less:
    push ax
    push dx
%ifdef CMZ_STREAM
    cmp byte [cs:cmz_mode], 0
    jne .sfin
%endif
    mov es, [cs:cmz_oseg]
    mov [es:0], cx              ; the T word, at the stream's first byte
    mov ax, [cs:cmz_ksa]        ; ...and the tail, from the cut to the cut,
    mov dx, [cs:cmz_ksa+2]      ; as far pointers off the two FIRST segments
    mov bx, [cs:cmz_sseg]
    call cmz_far
    mov ds, bx
    mov si, ax
    mov ax, [cs:cmz_koa]
    mov dx, [cs:cmz_koa+2]
    mov bx, [cs:cmz_oseg]
    call cmz_far
    mov es, bx
    mov di, ax
    call cmz_move               ; the tail, raw, however long
.pop:
    pop dx
    pop ax
.ok:
    CMZ_TRACK 0
    pop bp
    pop es
    pop ds
    clc
    ret

%ifdef CMZ_STREAM
    ; --- STREAMED: the file on the disk is everything the output window has
    ; --- slid past, and it may run past the cut (SPEC.md 22.22.5). Back to
    ; --- the cut on both sides, then the tail through the same two windows
.sfin:
    mov ax, [cs:cmz_koa]
    mov dx, [cs:cmz_koa+2]
    sub ax, [cs:cmz_obase]
    sbb dx, [cs:cmz_obase+2]
    jb .cutout                  ; the cut is already on the disk
    xchg di, ax                 ; ...or still in the window: DI goes back to
    jmp short .src              ; it and what was past it is simply dropped
.cutout:
    mov cx, di                  ; EVERYTHING to the disk, then back to the
    add cx, 8                   ; cluster below the cut: re-read from there,
    call cmz_oflush             ; truncate there (OSAPI_FILE_WRITE_AT with a
    mov ax, [cs:cmz_clb]        ; count of 0, 18.4.7.5), and the window
    neg ax                      ; starts again at it
    and ax, [cs:cmz_koa]
    mov dx, [cs:cmz_koa+2]
    mov di, [cs:cmz_koa]
    sub di, ax                  ; the window's DI at the cut: under a cluster
    push dx                     ; plus the header's eight
    push ax
    mov cx, [cs:cmz_clb]
    shl cx, 1                   ; two clusters' worth may be wanted back
    mov bx, dwf_dskw_read_at
    call cmz_tio
    pop ax
    pop dx
    xor cx, cx
    mov bx, dwf_dskw_write_at
    call cmz_tio                ; ...and the file ends there
.src:
    mov ax, [cs:cmz_ksa]        ; the SOURCE back to the cut too: the window
    and ax, 0xC000              ; refilled from the 16KB boundary below it
    mov dx, [cs:cmz_ksa+2]
    mov [cs:cmz_sfill], ax
    mov [cs:cmz_sfill+2], dx
    mov si, [cs:cmz_ksa]
    sub si, ax
    xor bx, bx
    mov cx, CMZ_SBUF
    call cmz_sfetch
    mov cx, [cs:cmz_tl]
.tb:                            ; the tail, a byte at a time: the source
    jcxz .tend                  ; window slides and refills under it, the
    or si, si                   ; output window slides and flushes under it
    jns .t1
    push cx
    call cmz_sslide
    pop cx
.t1:
    or di, di
    jns .t2
    push cx
    call cmz_oslide
    pop cx
.t2:
    movsb
    dec cx
    jmp short .tb
.tend:
    cmp byte [cs:cmz_wrote], 0
    jne .late
    mov ax, [cs:cmz_tl]         ; nothing written yet: the T word goes in
    mov [es:0], ax              ; with the stream's first bytes
.late:
    mov cx, di                  ; ...and the last of it, header's 8 bytes
    add cx, 8                   ; included while nothing has been written
    push word [cs:cmz_wrote]
    call cmz_oflush
    pop ax
    or al, al
    jz .popj                    ; (the `jmp .pop` below: a free trampoline)
    xor ax, ax                  ; the T word was written as 0 with the first
    xor dx, dx                  ; 16KB, so the first sector comes back, takes
    mov cx, [cs:cmz_clb]        ; it, and goes out again IN PLACE
    mov bx, dwf_dskw_read_at
    call cmz_tio
    mov ax, [cs:cmz_tl]
    mov [es:0], ax              ; ES:-8 is the file's byte 0, so ES:0 is its 8
    xor ax, ax
    mov cx, 512
    mov bx, dwf_dskw_write_at
    call cmz_tio
.popj:
    jmp .pop

; cmz_tio - one file call on the NEW file, fm_ebuf (module internal)
; in:  BX = the dwf_ shim (read_at or write_at), DX:AX = the offset, CX = the
;      count, the buffer at (ES-1):8 - the output window's first byte
; out: nothing; a failure never returns (cmz_ioerr). clobbers: AX DX, flags
cmz_tio:
    CMZ_TRACK 0
    push bx
    push si
    push ds
    push es
    mov si, es
    dec si
    mov es, si
    mov si, KERNEL_SEG
    mov ds, si
    mov si, fm_ebuf
    mov [cs:cmz_tsh], bx
    mov bx, 8
    call far [cs:cmz_tfp]
    jc cmz_ioerr
    call cmz_rearm
    pop es
    pop ds
    pop si
    pop bx
    CMZ_TRACK 1
    ret
%endif ; CMZ_STREAM

; cmz_far - DX:AX bytes past the segment BX -> BX:AX, AX 0..15 (module
;           internal). clobbers: DX, flags
cmz_far:
    push cx
    push ax
    mov cx, 4
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    add bx, ax
    pop ax
    and ax, 15
    pop cx
    ret

; -----------------------------------------------------------------------------
; cmz_move - CX bytes from DS:SI to ES:DI, wherever either offset starts
;            (module internal; the raw tail's copy)
; clobbers: AX CX SI DI DS ES, flags
;
; In pieces of at most 32KB with both pointers NORMALISED before each, so no
; `rep movsb` can carry either offset past 0xFFFF.
; -----------------------------------------------------------------------------
cmz_move:
.l:
    jcxz .d
    mov ax, si
    call .norm
    mov ds, ax
    mov si, dx
    mov ax, es
    push ds
    mov ds, ax
    mov ax, di
    call .norm
    pop ds
    mov es, ax
    mov di, dx
    mov ax, cx
    cmp ax, 0x8000
    jbe .one
    mov ax, 0x8000
.one:
    sub cx, ax
    xchg ax, cx
    rep movsb
    xchg ax, cx
    jmp short .l
.d:
    ret
.norm:                          ; DS:AX -> AX:DX, DX = 0..15
    push cx
    mov dx, ax
    and dx, 15
    mov cl, 4
    shr ax, cl
    mov cx, ds
    add ax, cx
    pop cx
    ret

; -----------------------------------------------------------------------------
; cmz_sarm / cmz_oarm - re-arm the loop's two 16-bit bounds (module internal)
; clobbers: AX, flags
;
; cmz_n is the source's end from DS, clamped to 0xFFFF - what cmz_probe and
; cmz_fill measure "bytes left" against, and a clamp is exact for both because
; neither asks past SI + CMZ_MAXM. cmz_nchk is where the parse STOPS - the end,
; clamped to 32KB: SI reaching it is either the stop or the
; moment to slide. cmz_lim is the bail from ES, clamped to 32KB the same way.
; -----------------------------------------------------------------------------
cmz_sarm:
    mov ax, 0xFFFF
    cmp word [cs:cmz_rn+2], 0
    jne .have
    mov ax, [cs:cmz_rn]
.have:
    mov [cs:cmz_n], ax
    mov ax, 0x8000
    cmp word [cs:cmz_rstop+2], 0
    jne .chk
    cmp [cs:cmz_rstop], ax
    jae .chk
    mov ax, [cs:cmz_rstop]
.chk:
    mov [cs:cmz_nchk], ax
    ret

cmz_oarm:
    mov ax, 0x8000
    cmp word [cs:cmz_rlim+2], 0
    jne .have
    cmp [cs:cmz_rlim], ax
    jae .have
    mov ax, [cs:cmz_rlim]
.have:
    mov [cs:cmz_lim], ax
    ret

; -----------------------------------------------------------------------------
; cmz_sslide - SI is past 32KB: move the source segment up (module internal)
; clobbers: AX BX CX DX, flags
;
; By S = (SI - 16KB) rounded DOWN to a multiple of 16KB, which leaves SI in
; [16KB, 32KB) and keeps `position & mask` unchanged for every window up to
; 16KB. Every table position loses S, and one that would go below zero goes
; to NIL: it was already older than `SI - mask`, the oldest the window
; accepts, so the walk would have stopped there anyway and the parse does not
; change (os88lz.lzb_compress_machine does no sliding and must agree).
; -----------------------------------------------------------------------------
cmz_sslide:
    mov ax, si
    sub ax, 0x4000
    and ax, 0xC000              ; S
    sub si, ax
    sub [cs:cmz_nextp], ax
    mov bx, cmz_rn
    call cmz_sub2               ; cmz_rn and cmz_rstop, adjacent...
    mov bx, cmz_dl
    call cmz_add2               ; ...as are cmz_dl and cmz_sbase
    mov dx, ax
    push es
    push di
    mov es, [cs:cmz_tseg]
    xor di, di                  ; head[] and prev[] are ONE run of words
    mov cx, [cs:cmz_mask]
    add cx, CMZ_HSIZE + 1
.w:
    mov ax, [es:di]
    cmp ax, CMZ_NIL
    je .keep
    sub ax, dx
    jnb .keep
    mov ax, CMZ_NIL             ; below the slide: out of the window
.keep:
    stosw
    loop .w
    pop di
    pop es
%ifdef CMZ_STREAM
    cmp byte [cs:cmz_mode], 0
    jne .stream
%endif
    mov cl, 4
    shr dx, cl
    mov ax, ds
    add ax, dx
    mov ds, ax
    jmp cmz_sarm
%ifdef CMZ_STREAM
.stream:                        ; the WINDOW stays put and the bytes move:
    push si                     ; [S, CMZ_SBUF) down to 0, and S more off the
    push di                     ; disk behind them - the same address
    push es                     ; arithmetic as a segment moving up by S
    push ds
    pop es
    mov si, dx
    xor di, di
    mov cx, CMZ_SBUF
    sub cx, dx
    shr cx, 1
    rep movsw
    pop es
    pop di
    pop si
    mov bx, CMZ_SBUF
    sub bx, dx
    mov cx, dx
    call cmz_sfetch
    jmp cmz_sarm
%endif

; cmz_oslide - DI is past 32KB: move the output 16KB on (module internal).
;              WHOLE, the segment moves up; STREAMED, the window stays put,
;              the first 16KB goes to the new file (header's 8 first) and the
;              rest moves down. The open tag byte is at most a symbol behind
;              DI, so it survives either. clobbers: AX BX, flags
cmz_oslide:
%ifdef CMZ_STREAM
    cmp byte [cs:cmz_mode], 0
    je .adj
    push cx
    mov cx, 0x4000
    call cmz_oflush             ; (ES-1):8 - the header, then the stream
.move:
    push si
    push ds
    push di
    push es
    pop ds
    mov si, 0x4000 - 8
    mov cx, di
    sub cx, si                  ; what is still live, up to DI
    mov ax, es
    dec ax
    mov es, ax
    mov di, 8                   ; ...to ES:-8, where the window begins
    rep movsb
    inc ax
    mov es, ax
    pop di
    pop ds
    pop si
    pop cx
    jmp short .base
.adj:
%endif
    mov ax, es
    add ax, 0x0400
    mov es, ax
.base:
    mov ax, 0x4000
    sub di, ax
    sub [cs:cmz_tofs], ax
    mov bx, cmz_rlim
    call cmz_sub2               ; cmz_rlim and cmz_dl, adjacent
    mov bx, cmz_obase
    call cmz_add1
    jmp cmz_oarm

; cmz_sub2 / cmz_sub1 - [cs:BX] -= AX (32 bits, AX zero-extended), for two
; adjacent dwords or one; cmz_add2 / cmz_add1 the same with +=. BX comes back
; past them (+8 / +4); every other register preserved. The slides' and the
; join's counters - none of them per byte (module internal)
cmz_sub2:
    call cmz_sub1
cmz_sub1:
    sub [cs:bx], ax
    sbb word [cs:bx+2], 0
    add bx, 4
    ret
cmz_add2:
    call cmz_add1
cmz_add1:
    add [cs:bx], ax
    adc word [cs:bx+2], 0
    add bx, 4
    ret

%ifdef CMZ_STREAM                ; the streamed pass's three, kern_big's alone
; -----------------------------------------------------------------------------
; cmz_sfetch - CX bytes of fm_onam, from [cs:cmz_sfill], to the window at BX
;              (module internal; the streamed source's only read)
; clobbers: AX, DX, flags - and a failure never returns (cmz_ioerr)
;
; OSAPI_FILE_READ_AT's rule is a whole number of CLUSTERS for both the offset
; and the count, which every call here keeps: 48KB, then 16KB or 32KB, from
; an offset that is always a multiple of 16KB - and the verb only streams on
; a volume whose cluster is 16KB or less. At the end of the file it asks for
; nothing at all, the window having no more to be filled with.
; -----------------------------------------------------------------------------
cmz_sfetch:
    mov ax, [cs:cmz_sfill]
    mov dx, [cs:cmz_sfill+2]
    cmp dx, [cs:cmz_nabs+2]
    jb .go
    ja .skip
    cmp ax, [cs:cmz_nabs]
    jae .skip
.go:
    CMZ_TRACK 0
    push si
    push ds
    push es
    mov es, [cs:cmz_sseg]
    mov si, KERNEL_SEG
    mov ds, si
    mov si, fm_onam
    CLO_RSEQ cs, cmz_scur       ; DX:AX = what arrived (SPEC.md 18.4.8.1)
    jc cmz_ioerr
    call cmz_rearm
    pop es
    pop ds
    pop si
    CMZ_TRACK 1
    add [cs:cmz_sfill], cx
    adc word [cs:cmz_sfill+2], 0
.skip:
    ret

; -----------------------------------------------------------------------------
; cmz_ioerr - a streamed read or write failed: AX = its FERR_*, and the stack
;             goes back to cmz_pack's frame from wherever this was reached.
;             HERE, between its two deepest callers: both jccs are short
cmz_ioerr:
    ; STKBALANCE-OK: abandons the stack on purpose - SP goes back to the frame
    ; cmz_pack banked at entry, whatever cmz_sfetch/cmz_oflush had pushed
    mov [cs:cmz_err], ax
    mov sp, [cs:cmz_sp]
    mov ax, 2
    jmp cmz_pack.fail

; cmz_oflush - write CX bytes from (ES-1):8 to fm_ebuf (module internal; pass
;              2's only write). The first is a WRITE, which creates the file
;              and lets dskw_czstamp see the 'CZ' header; every later one is
;              an APPEND, whose rule - the file a whole number of clusters -
;              the 16KB chunks keep until the last. On kern_big the APPEND
;              is a PLAIN WRITE_SEQ (SPEC.md 18.4.9, 22.22.5): no lookup and
;              no walk from the front per chunk. Not HELD: the cut-out path
;              reads its own output back (cmz_tio), and a read commits no
;              hold. Its WRITE_AT then makes the token stale, and the next
;              chunk is simply cold. clobbers: flags; a failure never
;              returns (cmz_ioerr)
; -----------------------------------------------------------------------------
cmz_oflush:
    CMZ_TRACK 0
    push ax
    push bx
    push dx
    push si
    push ds
    push es
    mov ax, es
    dec ax
    mov es, ax
    mov bx, 8
    xor dx, dx
    mov si, KERNEL_SEG
    mov ds, si
    mov si, fm_ebuf
    cmp byte [cs:cmz_wrote], 0
    jne .ap
    call COLD_SEG:dwf_dskw_write
    mov byte [cs:cmz_wrote], 1  ; `mov` leaves CF alone
    jmp short .chk
.ap:
    push di
    mov di, [cs:cmz_otok]
    xor al, al                  ; PLAIN
    call COLD_SEG:dwf_dskw_write_seq
    mov [cs:cmz_otok], di       ; (`mov` and `pop` leave CF alone)
    pop di
.chk:
    jc cmz_ioerr
    call cmz_rearm
    pop es
    pop ds
    pop si
    pop dx
    pop bx
    pop ax
    CMZ_TRACK 1
    ret

%endif ; CMZ_STREAM

; -----------------------------------------------------------------------------
; cmz_rearm - put the verb's own bar back after a read or a write took it
;             (module internal; DS = KERNEL_SEG). clobbers: flags
;
; Every dskw_* call arms the widget to ITS OWN length and ends it on the way
; out (SPEC.md 12.8), so without this the bar would sit empty through the
; seconds of parse between two 16KB chunks. A second fpg_begin re-scales the
; trough and leaves the chrome alone, and the steps put it back where the two
; passes had got to.
; -----------------------------------------------------------------------------
cmz_rearm:
    push ax
    push cx
    push dx
    mov cx, [cs:cmz_utot]
    mov dx, [cs:cmz_utot+2]
    call KERNEL_SEG:fpg_begin
    mov ax, [cs:cmz_done]
    mov dx, [cs:cmz_done+2]
.l:
    or dx, dx
    jnz .big
    or ax, ax
    jz .out
    call KERNEL_SEG:CMZ_XPROG
    jmp short .out
.big:
    push ax
    mov ax, 0x8000
    call KERNEL_SEG:CMZ_XPROG
    pop ax
    sub ax, 0x8000
    sbb dx, 0
    jmp short .l
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cmz_prog - one CMZ_PSTEP of the source parsed (module internal)
; clobbers: nothing, flags included - OSAPI_SLOT's push/pop pair writes no
;           flag and fpg_stepb preserves every register and the flags, which
;           is the contract dsk_xfer's per-sector loop already leans on
;
; The widget is the caller's: it armed fpg_begin with the source's length
; before the far call and gives it back afterwards. Unarmed this is a compare
; and a return inside fpg_stepb, so a caller that wants no bar passes nothing
; and pays a far call per 512 bytes of a parse that costs ~1,600 cycles a byte.
; -----------------------------------------------------------------------------
cmz_prog:
    add word [cs:cmz_done], CMZ_PSTEP   ; ...counted, for cmz_rearm
    adc word [cs:cmz_done+2], 0
    CMZ_TRACK 0                 ; the step PAINTS the trough
    push ax
    mov ax, CMZ_PSTEP
    call KERNEL_SEG:CMZ_XPROG
    pop ax
    CMZ_TRACK 1
    ret

; -----------------------------------------------------------------------------
; cmz_probe - the best match at SI, walking the chain (module internal)
; in:  DS = the source, SI = the position
; out: AX = the match length (0 = none, else >= CMZ_MIN), DX = its offset
;      when AX is not 0
; clobbers: AX BX CX DX, flags. SI, DI, DS, ES and BP come back.
;
; IT DOES NOT INSERT. The caller does, through cmz_fill, after the symbol is
; emitted - which is what makes a position findable from the NEXT one and
; keeps the chain in strictly decreasing order.
;
; The walk is hand-rolled and not `repe cmpsb`, because BOTH operands are in
; the SOURCE and that instruction compares DS:SI with ES:DI - ES is the table
; here, DI the output's write pointer, and giving either up would cost a
; segment load per candidate. An 8088's `cmpsb` is ~22 cycles an iteration
; anyway, so the trade is small and it is paid where the registers are scarce.
; -----------------------------------------------------------------------------
cmz_probe:
    push si
    push bp
    push es
    mov [cs:cmz_pos], si
    mov ax, [cs:cmz_n]
    sub ax, si
    cmp ax, CMZ_MIN
    jb .none                    ; fewer than three bytes left to match
    cmp ax, CMZ_MAXM
    jbe .most
    mov ax, CMZ_MAXM            ; ...and never past SI + 32KB (SPEC.md
.most:                          ; 20.15.4), which the slide relies on
    mov [cs:cmz_most], ax       ; ...and the most a match here could be

    mov ax, si                  ; lo = SI - mask: the oldest position still in
    sub ax, [cs:cmz_mask]       ; the window. It borrows for the whole of the
    jnc .havelo                 ; first window, where every position qualifies
    xor ax, ax
.havelo:
    mov [cs:cmz_lo], ax
    mov word [cs:cmz_best], 0

    call cmz_hash               ; BX = &head[h], a word into the block
    mov es, [cs:cmz_tseg]
    mov cx, [es:bx]             ; the newest position with this hash
    mov bp, CMZ_DEPTH
.chain:
    cmp cx, CMZ_NIL
    je .fin
    cmp cx, [cs:cmz_lo]
    jb .fin                     ; out of the window, and so is everything
                                ; behind it - the chain descends
    mov bx, cx                  ; DS:BX = the candidate
    xor ax, ax
    mov si, [cs:cmz_pos]
.walk:
    mov dl, [bx]                ; DL, which nothing in the walk holds: DX is
    cmp dl, [si]                ; the OUTPUT and is written at .fin/.none.
    jne .stop                   ; It was AL behind a push/pop of AX - two
                                ; bytes and ~27 cycles a compared byte more
    inc bx
    inc si
    inc ax
    cmp ax, [cs:cmz_most]
    jb .walk
.stop:
    cmp ax, [cs:cmz_best]
    jbe .nextc
    mov [cs:cmz_best], ax
    mov dx, [cs:cmz_pos]        ; DX, free once the walk is done: AX is
    sub dx, cx                  ; still the length, for the test below
    mov [cs:cmz_boff], dx
    cmp ax, [cs:cmz_most]
    jae .fin                    ; it reached the end of the input: nothing
.nextc:                         ; behind it in the chain can beat that
    dec bp
    jz .fin
    mov bx, cx
    and bx, [cs:cmz_mask]
    add bx, bx
    mov cx, [es:bx+CMZ_PREV]
    jmp short .chain
.fin:
    mov ax, [cs:cmz_best]
    cmp ax, CMZ_MIN
    jb .none
    mov dx, [cs:cmz_boff]
    jmp short .out
.none:
    xor ax, ax                  ; (DX is then not an offset: the one caller
.out:                           ; reads it only after a match)
    pop es
    pop bp
    pop si
    ret

; -----------------------------------------------------------------------------
; cmz_hash - the three-byte hash of the position at SI (module internal)
; in:  DS = the source, SI = the position, at least CMZ_MIN bytes left
; out: BX = &head[h] - the slot's BYTE offset into the caller's block
; clobbers: AX BX CX, flags
; -----------------------------------------------------------------------------
cmz_hash:
    mov ah, [si]                ; h = ((b0 << 8) ^ (b1 << 4) ^ b2) & (HSIZE-1):
    mov al, [si+2]              ; b0 << 8 and b2 share no bit, so the two loads
    mov bl, [si+1]              ; ARE their xor - no shift by eight (11 bytes
    xor bh, bh                  ; and ~45 cycles a position fewer than the
    mov cl, 4                   ; shape it replaced)
    shl bx, cl
    xor ax, bx
    and ax, CMZ_HSIZE - 1
    add ax, ax
    xchg bx, ax
    ret

; -----------------------------------------------------------------------------
; cmz_ins - record the position at SI in the chain (module internal)
; in:  DS = the source, ES = the table block, SI = a position with at least
;      CMZ_MIN bytes left
; clobbers: AX BX CX, flags
; -----------------------------------------------------------------------------
cmz_ins:
    call cmz_hash
    mov cx, [es:bx]
    mov [es:bx], si             ; head[h] = SI...
    mov bx, si
    and bx, [cs:cmz_mask]
    add bx, bx
    mov [es:bx+CMZ_PREV], cx    ; ...and prev[SI] = what head[h] was
    ret

; -----------------------------------------------------------------------------
; cmz_fill - record AX positions from SI (module internal)
; in:  DS = the source, SI = the first, AX = how many (a match's length, or 1
;      for a literal)
; clobbers: the flags. Every register comes back, ES included.
;
; EVERY POSITION A MATCH COVERS GOES IN, and it is worth 2.5 points of the 5.7
; this encoder beats the build's LZ4 by: nothing else would ever put them
; there, and a later position matching INTO the middle of an earlier match is
; common. It costs a hash and two stores per byte and no walk, so it is paid
; where the parse was already free.
;
; ES is swapped ONCE for the whole run rather than per position, which is the
; only reason this is a routine and not three lines at the call site.
; -----------------------------------------------------------------------------
cmz_fill:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov bp, ax
    or bp, bp
    jz .out
    mov dx, [cs:cmz_n]
    sub dx, CMZ_MIN             ; the last position a three-byte hash exists
    mov es, [cs:cmz_tseg]       ; at, and cmz_n is over CMZ_SLACK so this
.one:                           ; cannot borrow
    cmp si, dx
    ja .out                     ; ...the tail, where cmz_probe would refuse too
    call cmz_ins
    inc si
    dec bp
    jnz .one
.out:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cmz_bit - append one bit to the output, CF = its value (module internal)
; clobbers: the flags, and DI advances when a tag byte is opened
;
; TAG BYTES, MSB FIRST - os88lz.py's _BitOut, which is that way round so the
; 8086 READER can carry the tag in DL behind a guard bit and spend no register
; on a counter (kernel/lz.inc's LZ_GETBIT).
; -----------------------------------------------------------------------------
cmz_bit:
    push ax
    push cx
    jnc .zero
    call cmz_room
    dec byte [cs:cmz_tbit]
    mov cl, [cs:cmz_tbit]
    mov al, 1
    shl al, cl
    push bx
    mov bx, [cs:cmz_tofs]
    or [es:bx], al              ; ...INTO THE STREAM, where the tag byte lives:
    pop bx                      ; cmz_room wrote it 0 and only a 1 changes it,
    jmp short .out              ; so a stream that ends mid-tag is still whole
.zero:                          ; and a 0 writes nothing at all. (It was a CS
    call cmz_room               ; copy, OR'd and then stored back on EVERY
    dec byte [cs:cmz_tbit]      ; bit, 0s included.) Both slides carry the
.out:                           ; open byte with them - cmz_oslide's banner
    pop cx
    pop ax
    ret

; cmz_room - make sure a tag byte is open (module internal)
cmz_room:
    cmp byte [cs:cmz_tbit], 0
    jne .have
    mov [cs:cmz_tofs], di
    mov byte [cs:cmz_tbit], 8
    push ax
    xor al, al
    stosb                       ; ...reserved in the output, filled in above
    pop ax
.have:
    ret

; -----------------------------------------------------------------------------
; cmz_gamma - the interlaced Elias code os88lz.py writes (module internal)
; in:  AX = the value, >= 2
; clobbers: the flags, and DI through cmz_bit
;
; Bits of the value from one below the top down to 0, each followed by a
; continuation bit - 1 while more follow, 0 on the last. The top bit is
; implicit, which is why the value may not be 1.
; -----------------------------------------------------------------------------
cmz_gamma:
    push ax
    push cx
    push dx
    mov dx, ax
    mov cl, 15                  ; find the top bit
.top:
    mov ax, dx
    shr ax, cl
    test al, 1
    jnz .got
    dec cl
    jnz .top
.got:
    or cl, cl
    jz .out                     ; a value of 1: nothing after the implicit bit
    dec cl
.emit:
    mov ax, dx
    shr ax, cl
    shr ax, 1                   ; CF = bit CL of the value
    call cmz_bit
    add cl, 0xFF                ; CF = more bits follow (CL was not 0), and
    call cmz_bit                ; CL is one less: the continuation bit
    cmp cl, 0xFF
    jne .emit                   ; ...and CL wrapped exactly when that was a 0
.out:
    pop dx
    pop cx
    pop ax
    ret


; =============================================================================
; THE TWO VERBS (SPEC.md 22.22, 22.23), and they live HERE and not in files.inc
; =============================================================================
; Compress used to be 722 bytes of resident `.cold` and Uncompress 445, and
; every one of them only ever ran after `mod_need` had already fetched this
; image - so what was resident was policy nothing could reach without the
; system disk anyway. The only thing that kept Compress out here was ORDER:
; the old body sized, claimed, read and refused a package BEFORE calling
; mod_need, so a refusal could be said on a machine with no system disk in
; it. `mod_need` goes first now, and the trade is stated rather than hidden: a
; machine that cannot fetch this image says `No disk` instead of the specific
; reason, which is the truer answer to "compress this" on such a machine, and
; a refusal costs one extra 1.4KB read on a machine that CAN. Uncompress
; followed for the size, and for the same reason: expanding needs no encoder,
; but it needs a claim, a read, a write and four verdicts, and those are the
; same bytes whichever way the file is going.
;
; **DS IS KERNEL_SEG ON ENTRY** (SPEC.md 2.6 rule 2: the caller is `.cold`),
; so every kernel variable this needs - fm_onam, fm_ftype, dskw_raw - is
; reached with a plain `[label]` and only this image's OWN scratch needs
; `cs:`. What it CANNOT do is a near `call` into the kernel, so the services
; it wants go through the far shims the cloner already established:
; COLD_SEG:dwf_* for the file layer, COLD_SEG:mmf_* for the heap, and
; COLD_SEG:lzf_decomp for the decoder a package's expansion needs.
;
; THE THREE ANSWERS, and they are one register pair:
;
;   CF=0            the file changed. This image has already said so
;   CF=1, AX = 0    it refused, and has already said why in its own words
;   CF=1, AX != 0   a FERR_*, which the caller says off fm_errtab
;
; in:  DL = the command id fm_docmd entered with, times two: FMC_COMPRESS*2 or
;      FMC_UNCOMP*2. One entry and one far pointer for both verbs
; -----------------------------------------------------------------------------
cmz_verb:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; --- the selection BOTH verbs act on, and the no-op that says nothing ---
    ; fm_sel_ok's test, inline: this image is entered with DS = KERNEL_SEG, so
    ; the acting window's state block is a plain read. A folder, '..' and no
    ; selection at all are the no-selection case, silently, exactly as Rename
    ; and Delete - it costs the image fetch to find out, which the resident
    ; test it replaced did not, and that is the price of the test not being
    ; resident (SPEC.md 22.22)
    mov di, [fm_vp]
    mov ax, [di+FS_SEL]
    cmp ax, [di+FS_N]           ; 0xFFFF (no selection) also fails this
    jae .none
    call COLD_SEG:fmf_fm_stage_onam ; fm_onam + [fm_odir] + [fm_ftype]
    cmp byte [fm_odir], 0
    jne .none
    cmp word [fm_ftype], 3
    je .none
    cmp dl, FMC_UNCOMP * 2
    je .uncomp
%ifdef KERN_BIG
    cmp dl, FMC_UNCTO * 2
    je .uncto
%endif
    call cmz_compress
    jmp short .out
.uncomp:
    call cmz_uncomp
    jmp short .out
%ifdef KERN_BIG
.uncto:
    call cmz_uncto              ; SPEC.md 22.23.6
    jmp short .out
%endif
.none:
    mov byte [fm_cmdchg], FMD_NONE  ; a no-op draws nothing (SPEC.md 22.13)
    xor ax, ax                  ; ...and has nothing to say
    stc
.out:
    jmp clo_x6                 ; (the shared epilogue, below clo_cat)

; -----------------------------------------------------------------------------
; cmz_sizes - what the selected file OCCUPIES and what it BECOMES
; out: CF=0 with [cs:cmz_p] = the on-disk size, DX:AX = the unpacked size and
;      CL = its LZ_* format, or 0xFF for a file stored plain - every size 32
;      bits (SPEC.md 22.22.4); CF=1 with AX = a FERR_* - FERR_PROT for a
;      redirected volume
; clobbers: AX BX CX DX (the outputs), flags
;
; It has to be asked with the READ's own answer rather than the listing's:
; SPEC.md 19.1's staged entry carries the RAW size at offset 20, so a listing
; showing a compressed file shows what it occupies, and a verb claiming from
; that would claim a third of what the read is about to deliver. dskw_stat_x
; leaves the raw entry in dskw_raw, and the hint is three bytes of it
; (SPEC.md 20.14.1) - which is why this is a module routine and not the kernel
; routine it replaced: the entry is already in hand.
; -----------------------------------------------------------------------------
cmz_sizes:
    cmp byte [dsk_vkind], DVK_FILE
    je .nofs                    ; a REDIRECTED volume (SPEC.md 62.9): no entry
                                ; to read a hint out of, and nothing on it is
                                ; compressed
    mov si, fm_onam
    call COLD_SEG:dwf_dskw_stat ; AL = attribute, BX = cluster, DX:CX = size
    jc .out
    mov [cs:cmz_p], cx          ; ANY size: both sides of the codec cross 64KB
    mov [cs:cmz_p+2], dx        ; now (SPEC.md 20.14.5.1, 20.15.4)
    mov ax, cx
    mov cl, [dskw_raw+DSK_R_CZM]    ; the mark LESS its format (SPEC.md
    sub cl, DSK_CZ_MARK             ; 20.14.1): 0x5A and 0x5B pass
    cmp cl, LZ_LZB
    jbe .cz
    mov cl, 0xFF                ; no hint: what it occupies is what it
    clc                         ; becomes, and DX:AX is already that
.out:
    ret
.cz:
    mov ax, [dskw_raw+DSK_R_CZL]    ; U, 24 bits out of the hint
    mov dl, [dskw_raw+DSK_R_CZH]
    xor dh, dh                  ; ...and CF = 0. NOT `mov dh, 0`: the `jbe`
    ret                         ; above is taken with CF SET for an LZ4 mark,
                                ; and a `mov` carries it out - so an LZ4 file
                                ; answered a FERR whose number was its own size
.nofs:
    mov ax, FERR_PROT
    stc
    ret

; -----------------------------------------------------------------------------
; cmz_compress - File > Compress (SPEC.md 22.22)
; -----------------------------------------------------------------------------
cmz_compress:
    call cmz_sizes              ; DX:AX = U, CL = the format
    jc .ferr
    mov [cs:cmz_u], ax
    mov [cs:cmz_u+2], dx
    mov word [cs:cmz_x], 0      ; no clear prefix - a package sets its own
    cmp dx, 0x100
    jae .big                    ; U past 16MB: the directory hint that says
                                ; what a file expands to is 24 bits (SPEC.md
                                ; 20.14.1), and no claim here could hold it
    or dx, dx
    jnz .long
    cmp ax, CMZ_SLACK + 1
    jb .worse                   ; nothing this short can shrink, and cmz_pack
.long:                          ; refuses it in the same words
    cmp cl, LZ_LZB
    je .already
    CURBAR_ON                   ; the strip is drawn by menu_draw_bar, which
    mov ax, cmz_s_busy          ; would SPEND gfx_lock's promised hide and take
    call cmz_say                ; the arrow off the glass for the rest of the
    CURBAR_OFF                  ; hold - fpg's own bracket keeps it (SPEC.md
                                ; 7.4.3). Up NOW and for the whole operation
                                ; (22.22.6): OSAPI_TOAST draws at once for the
                                ; lock's holder, and the idle pass that would
                                ; expire it does not run until the verdict has
                                ; replaced it
%ifdef CMZ_STREAM
    mov byte [cs:cmz_mode], 0
    call cmz_route              ; too big to hold twice? (SPEC.md 22.22.5)
    jnc .whole
    jmp cmz_stream              ; ...then streamed, and its answer is ours
%else
    jmp short .whole            ; kern_small: always whole (CMZ_STREAM's note)
%endif
    ; The refusals with no claim held, and the FERR_* ones: HERE, in the
    ; middle, so that the tests above and the read below all reach them with
    ; a short jcc (size pass 8)
.rdisk:
    mov ax, FERR_IO             ; the volume moved under us between the
.rferr:                         ; directory read and the data read
    call cmz_drop               ; the claim is still held (flags only)
.ferr:
    stc                         ; AX = FERR_*, and the caller says it: this
    ret                         ; image has no fm_errtab of its own and does
                                ; not want one
.worse:
    mov ax, cmz_s_worse
    jmp short .say
.nomem:
    mov ax, cmz_s_nomem
    jmp short .say
.big:
    mov ax, FERR_BIG          ; 'Too large': the kernel's own (fm_errtab)
    jmp short .say
.already:
    mov ax, cmz_s_alr
.say:
    jmp cmz_jret                ; ...said here, so the caller says nothing
.whole:
    call cmz_claim              ; DX = the base, [cs:cmz_k]/[cs:cmz_w] set
    jc .nomem
    mov [cs:cmz_b], dx

    ; --- read the file whole, expanding it if it arrives compressed --------
    ; The capacity is the source region alone: a 'CZ' file expands in place
    ; inside exactly U (SPEC.md 20.14.2), and the region is a whole KB over
    call cmz_uread              ; DX:AX = what arrived - Uncompress's read,
    jc .rferr                   ; KU KB at the claim's base
    jne .rdisk                  ; the entry said one thing and the read another

    ; --- a PACKAGE keeps a clear prefix (SPEC.md 22.22.1) ------------------
    cmp word [fm_ftype], 1
    jne .notpkg
    mov es, [cs:cmz_b]
    call cmz_pkg                ; CF=1 with AX = the string; else [cs:cmz_x]
    jc .rsay
.notpkg:

    ; --- the three segments the encoder works between ----------------------
    mov cx, [cs:cmz_u]          ; SPEC.md 12.8's bar, scaled to what is
    mov dx, [cs:cmz_u+2]        ; actually parsed: cmz_pack reports every
    sub cx, [cs:cmz_x]          ; CMZ_PSTEP of it, which is the only progress
    sbb dx, 0                   ; the pack itself can give. DX:CX, the
    call KERNEL_SEG:fpg_begin   ; widget's own 32-bit scale - and every
    push dx                     ; register comes back, so the same DX:CX is
    push cx                     ; the parse's length below

    mov ax, [cs:cmz_k]
    mov cl, 6
    shl ax, cl                  ; KU KB in paragraphs
    mov dx, [cs:cmz_b]
    add dx, ax                  ; the output region's base...
    mov [cs:cmz_o], dx
    add dx, 8                   ; ...and the STREAM starts 128 BYTES INTO IT,
                                ; which is what leaves room for either header
                                ; in front of it without a move: 8 bytes for a
                                ; 'CZ' container, up to 112 for a package's
                                ; clear prefix, and both are a multiple of 16
    mov bx, dx                  ; base + (2*KU + 1)*64: head[] and prev[] -
    add bx, ax                  ; which is the stream's segment plus KU*64
    add bx, 64 - 8              ; and the 56 paragraphs left of that 64
    mov ax, [cs:cmz_x]
    mov cl, 4
    shr ax, cl                  ; the clear prefix in paragraphs - 0 for
    add ax, [cs:cmz_b]          ; everything but a package, so the source
    mov di, [cs:cmz_w]          ; segment is the claim's base
    dec di                      ; the window's MASK
    pop cx
    pop si                      ; SI:CX, the parse's 32-bit length
    call cmz_pack               ; NEAR: DX:AX = the packed length, tail and all
    jc .rpack

    cmp word [cs:cmz_x], 0
    jne .pkghdr

    ; --- the 'CZ' container, built in the 128 bytes in front of the stream --
    add ax, DSK_CZ_HDR
    adc dx, 0
    cmp dx, [cs:cmz_p+2]
    ja .rworse
    jb .smaller
    cmp ax, [cs:cmz_p]
    jae .rworse                 ; THE HEADER COUNTS: a file that packs to one
                                ; byte under what it occupies is bigger once
                                ; the container is on it, and cmz_pack was
                                ; comparing against U rather than against what
                                ; is on the disk
.smaller:
    mov [cs:cmz_z], ax
    mov [cs:cmz_z+2], dx
    push ax                     ; ...and the length kept, for the write
    push dx
    mov dx, [cs:cmz_o]
    add dx, 7                   ; the last paragraph before the stream, so the
    mov es, dx                  ; eight bytes end exactly where it begins
    call cmz_czhdr
    pop dx
    pop ax
    mov bx, 8
    jmp short .writeit
.rpack:                         ; (before .pkghdr, in the pack's jcc reach)
    or ax, ax
    jz .rworse                  ; AX = 0: it did not get smaller
    mov ax, cmz_s_tail          ; AX = 1: a raw tail over 64KB
    jmp short .rsay
.rworse:
    mov ax, cmz_s_worse
.rsay:                          ; the claim is held: back, then said - a
    call cmz_drop               ; string, or the write's FERR_* off fm_errtab
    jmp cmz_jret                ; (cmz_jsayx): the caller says nothing
.pkghdr:
    call cmz_hdr                ; ES:BX = the whole file, AX = its length -
    jc .rworse                  ; under 64KB, a package being one segment
    xor dx, dx
.writeit:
    mov cx, ax                  ; DX:CX, the write's 32-bit count
    mov si, fm_onam
    call COLD_SEG:dwf_dskw_write    ; ...and dskw_czstamp derives the directory
    jc .rsay                        ; hint from the bytes (SPEC.md 20.14.4) -
                                    ; or CLEARS it, which is what a package
                                    ; needs: a `.o88` begins 'O8' and carries
                                    ; its own
    call cmz_drop
    call cmz_pct                ; 'Compressed to 47%'
    clc
    ret


%ifdef CMZ_STREAM                ; the route and the streamed arm: kern_big's
; -----------------------------------------------------------------------------
; cmz_route - does this file go through memory WHOLE, or STREAMED?
; in:  CL = cmz_sizes' format byte, [cs:cmz_u] = U
; out: CF=1 = stream it (SPEC.md 22.22.5); CF=0 = whole, as it always was
; clobbers: AX CX DX, flags
;
; WHOLE WHEN IT FITS, because whole is one pass and streamed is two: the
; question is asked of mem_avail - the number a claim would be served, with
; nothing claimed and so nothing shed - at the full window. Streaming is only
; for what whole cannot fund, and only where it can work at all: a PLAIN file
; (a 'CZ' source expands on the way in and can only be read whole), not a
; package (one segment, by APP_MAX_SIZE), over 64KB (under it, whole always
; costs less than the 89KB of windows), on a volume whose cluster the 16KB
; refills are a multiple of, and on a heap that holds the windows at the
; smallest dial.
; -----------------------------------------------------------------------------
cmz_route:
    cmp cl, 0xFF
    jne .no
    cmp word [fm_ftype], 1
    je .no
    cmp word [cs:cmz_u+2], 0
    je .no
%ifdef KERN_SMALL
    jmp short .no               ; ...and never on kern_small: the ending's
%endif                          ; truncate is a door it has not got (18.4.7.4)
    push bx
    call cmz_ukb                ; AX = U's KB...
    push ax                     ; ...kept for the second test below
    add ax, ax
    add ax, 1 + CMZ_PREV/1024 + CMZ_WMAX/512    ; cmz_claim's full-window block
    push ax
    call COLD_SEG:mmf_osapi_mem_avail   ; AX = the largest claim, KB (and BX
    pop dx                              ; the total, which is not wanted)
    pop bx                      ; BX = U's KB
    cmp ax, dx
    jae .whole                  ; it fits whole: one pass
    cmp ax, CMZ_STRK + CMZ_WMIN/512
    jb .whole                   ; ...and neither way fits: whole's dial, and
                                ; its own `Not enough memory`
    cmp bx, ax                  ; ...nor when the RESULT could not be read
                                ; back: every reader of a 'CZ' file, and
                                ; Uncompress, claims U whole (SPEC.md
                                ; 22.23.4), so a stream past the heap is a
                                ; file nothing on this machine can open.
    ja .whole                   ; Whole's own claim refuses it, before any work
    call COLD_SEG:dwf_dskw_dfree    ; BX = sectors a cluster
    jc .whole
    cmp bx, 0x4000 / 512
    ja .whole
    mov cl, 9
    shl bx, cl
    mov [cs:cmz_clb], bx        ; ...kept, for the ending's re-read
    pop bx
    stc
    ret
.whole:
    pop bx
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; cmz_stream - Compress, for a file too big to hold twice (SPEC.md 22.22.5)
;
; ONE PASS, written as it goes to a NEW file, and the stream's cut (SPEC.md
; 20.15.2) - which is only known at the end, and after which everything is
; thrown away - is dealt with at the end: when it is still in the output
; window the bytes past it are simply dropped, and when the window has already
; written past it the file is TRUNCATED back (OSAPI_FILE_WRITE_AT with a count
; of 0, SPEC.md 18.4.7.5) and the tail sent after. Only then does the original
; go and the new file take its name. A refusal - not smaller, a tail over 64KB
; - deletes what was written; the disk needs room for the result beside the
; original, not for the original twice.
; -----------------------------------------------------------------------------
cmz_stream:
    mov ax, CMZ_STRK            ; the windows, head[] and prev[]
    call cmz_wclaim
    jnc .got
    mov ax, cmz_s_nomem
    jmp .say
.got:
    mov [cs:cmz_b], dx
    call cmz_jtmpn              ; the new file's name, where the file layer
                                ; can read it (DS:SI) - fm_ebuf, the status
                                ; line's edit buffer, idle while a menu verb
                                ; runs
    mov si, fm_ebuf             ; a name already TAKEN refuses: the write
    call COLD_SEG:dwf_dskw_stat ; below would replace it, and it may be the
    mov ax, FERR_EXIST          ; result a failed rename left there (SPEC.md
    jnc .ferr                   ; 22.22.5 step 4) - or the file compressed
    mov ax, [cs:cmz_u]          ; the bar: one pass over U
    mov dx, [cs:cmz_u+2]
    mov [cs:cmz_utot], ax
    mov [cs:cmz_utot+2], dx
    xor ax, ax
    mov [cs:cmz_done], ax
    mov [cs:cmz_done+2], ax
    mov [cs:cmz_wrote], al
    call cmz_rearm
    mov ax, [cs:cmz_b]          ; the 'CZ' header, in the paragraph in front
    add ax, CMZ_SBUF / 16       ; of the output window: the first 16KB out
    mov es, ax                  ; carries it, and dskw_czstamp reads it there.
    call cmz_czhdr              ; T goes in at the end (cmz_pack's .sfin)

    mov byte [cs:cmz_mode], 1   ; --- ONE pass, written as it goes --------
    mov ax, [cs:cmz_b]          ; the windows off the one claim, and the pack:
    mov dx, ax                  ; source window, CMZ_SBUF
    add dx, CMZ_SBUF / 16 + 1   ; output window, one paragraph in
    mov bx, ax
    add bx, (CMZ_SBUF / 1024 + CMZ_OBUFK) * 64  ; head[], then prev[]
    mov di, [cs:cmz_w]
    dec di
    mov cx, [cs:cmz_u]
    mov si, [cs:cmz_u+2]
    call cmz_pack               ; DX:AX = the file, header included
    jc .pfail
    mov [cs:cmz_z], ax
    mov [cs:cmz_z+2], dx
    mov si, fm_onam             ; --- and the new file takes the name -----
    call COLD_SEG:dwf_dskw_delete
    jc .rmtmp                   ; (a read-only original, say: nothing lost)
    mov si, fm_ebuf
    mov di, fm_onam
    call COLD_SEG:dwf_dskw_rename
    jc .ferr                    ; the result is on the disk, under the
                                ; temporary name - which the listing shows
    call cmz_drop
    call cmz_pct
    clc
    ret

.pfail:
    cmp ax, 2
    je .ioerr
    push ax                     ; a refusal at the END of the pass: whatever
    call .rmnew                 ; went to the disk goes
    pop ax
    or ax, ax
    jz .worse
    mov ax, cmz_s_tail
    jmp short .rsay
.ioerr:
    mov ax, [cs:cmz_err]
.rmtmp:
    push ax
    call .rmnew
    pop ax
.ferr:
    call cmz_drop               ; (flags only: AX, the error, survives)
    stc                         ; AX = FERR_*, said by the caller
    ret
.worse:
    mov ax, cmz_s_worse
    jmp short .rsay
.rmnew:                         ; the new file, if one was started: it is
    cmp byte [cs:cmz_wrote], 0  ; not the user's and it is not finished
    je .rmno
    push si
    mov si, fm_ebuf
    call COLD_SEG:dwf_dskw_delete
    pop si
.rmno:
    ret
.rsay:
    call cmz_drop               ; (flags only: AX, the error, survives)
.say:
    jmp cmz_jret                ; ...said, and the caller says nothing

%endif ; CMZ_STREAM

; cmz_czhdr - the 'CZ' container's eight bytes at ES:8 (SPEC.md 20.14.1): the
; magic, LZ_LZB and the reserved zero, and U from [cs:cmz_u]. Both Compress
; arms build it, the stream's in front of its output window and the whole
; one's in front of its stream. clobbers: AX, DI, flags
cmz_czhdr:
    cld
    mov di, 8
    mov ax, DSK_CZ_MAG
    stosw
    mov ax, LZ_LZB              ; ...in the low byte, the reserved 0 in the high
    stosw
    mov ax, [cs:cmz_u]
    stosw
    mov ax, [cs:cmz_u+2]
    stosw
    ret

; -----------------------------------------------------------------------------
; cmz_uncomp - File > Uncompress (SPEC.md 22.23)
;
; TWO ARMS, because there are two containers. A 'CZ' file is expanded by
; dskw_read_x itself (SPEC.md 20.14), so that arm is a read and a write with
; nothing between them. A package is not a container - it carries its
; compression in flags bits 3 and 4 with a clear prefix in front (SPEC.md
; 20.13) - so that arm reads the file as it lies and then does ld_expand's job
; a step at a time, into a SECOND claim, because `image` is not knowable until
; the file is in hand (22.23.2).
; -----------------------------------------------------------------------------
cmz_uncomp:
    xor ax, ax
    mov [cs:cmz_b], ax          ; neither claim is held yet, which is what
    mov [cs:cmz_o], ax          ; lets cmz_ufree be the ONE exit path
    call cmz_sizes              ; DX:AX = U, CL = the format
    jc .ferr
    mov [cs:cmz_u], ax
    mov [cs:cmz_u+2], dx        ; ANY size (SPEC.md 22.23.4): the read
    cmp cl, 0xFF                ; expands past 64KB and the write takes DX:CX
    je .pkg                     ; no 'CZ' mark: a package, or nothing at all

    ; --- a 'CZ' file: the READ layer is the whole verb (SPEC.md 20.14.2) ---
    call cmz_uclaim             ; U, rounded up
    jc .nomem
    mov [cs:cmz_b], dx
    call cmz_uread
    jc .rferr
    jne .rbad                   ; the hint said one thing and the read another
    xor bx, bx
    jmp .writeit

    ; The refusals, HERE in the middle so both arms reach them with a short
    ; jcc (size pass 8)
.rferr:
    call cmz_ufree              ; (flags only)
.ferr:
    stc
    ret
.rbad:
    mov ax, cmz_s_bad
    jmp short .rsay
.rplain:
    mov ax, cmz_s_plain
    jmp short .rsay
.rnomem:
    mov ax, cmz_s_nomem
.rsay:
    call cmz_ufree              ; (flags only)
    jmp short .say
.plain:
    jmp cmz_join                ; ...or a PART of a split set, which says
                                ; `Not compressed` itself when it is not one
                                ; (SPEC.md 22.23.5)
.nomem:
    mov ax, cmz_s_nomem
.say:
    jmp cmz_jret                ; ...said, and the caller says nothing

    ; --- a package (SPEC.md 20.13): read it as it lies, then expand -------
.pkg:
    cmp word [fm_ftype], 1      ; the SPEC.md 19 type word: 1 = a package, and
    jne .plain                  ; nothing else on a volume carries a stream
    or dx, dx
    jnz .plain                  ; a compressed package is under 64KB on the
                                ; disk (its image is), so a longer one is not
    cmp ax, LD_HDR_SIZE
    jb .plain
    call cmz_uclaim
    jc .nomem
    mov [cs:cmz_b], dx
    call cmz_uread              ; a package is not a container, so this is the
    jc .rferr                   ; same transparent read doing a plain one
    mov es, [cs:cmz_b]
    mov al, [es:LD_H_FLAGS]
    test al, 8
    jz .rplain                  ; ...and this is where a plain package leaves
    mov bx, [es:LD_H_IMG]       ; BX = the image, past cmz_cpre (which spends
    cmp bx, [cs:cmz_p]          ; DX alone)
    jbe .rbad                   ; an image no bigger than the file it is
                                ; packed into is a header that has drifted
    mov [cs:cmz_u], bx
    call cmz_cpre               ; DX = [cs:cmz_x] = the clear prefix - the SAME
                                ; ladder Compress used, glyph block included
                                ; (this copy once stopped at the association
                                ; block, and expanded DOS.O88 and VIDEO.O88 -
                                ; flags 0x2B - from 16 bytes short of their body)
    cmp dx, bx
    jae .rbad                   ; all prefix: there is no body to expand

    xchg ax, bx                 ; ...and a SECOND claim for the expansion,
    xor dx, dx                  ; (an image is 16 bits)
    call cmz_uclaim             ; because `image` was not knowable until the
    jc .rnomem                  ; file was in hand (SPEC.md 22.23.2)
    mov [cs:cmz_o], dx
    call cmz_upkg
    jc .rbad
    mov es, [cs:cmz_o]
    and byte [es:LD_H_FLAGS], 0xE7  ; bits 3 and 4 off: what lands on the disk
    mov ax, [cs:cmz_u]              ; is a package ld_check_hdr reads with no
    xor dx, dx                      ; decoder at all
    xor bx, bx

    ; --- ES:BX = the bytes, DX:AX = the length -----------------------------
.writeit:
    mov cx, ax                  ; DX:CX, the write's 32-bit count
    mov si, fm_onam
    call COLD_SEG:dwf_dskw_write    ; ...and dskw_czstamp CLEARS the directory
    jc .rferr                       ; hint, by the same route it sets one: it
                                    ; looks at the bytes (SPEC.md 20.14.4)
    call cmz_ufree
    mov ax, cmz_s_undone        ; a WORD and not a number: the new size is
    call cmz_say                ; already on the listing behind the toast
    clc
    ret


; -----------------------------------------------------------------------------
; cmz_uclaim - one block big enough to expand DX:AX bytes into
; in:  DX:AX = the unpacked size, under 16MB (the hint's 24 bits)
; out: CF=0 with DX = the base and [cs:cmz_k] = the block's KB; CF=1 = no room
; clobbers: CX, DX (an output), flags
;
; ONE KB OVER, for the round-up, and no more: a 'CZ' read expands in place
; inside exactly U (SPEC.md 20.14.2). NOT through mem_bytes_kb_x, whose own
; banner says a new caller must prove its input cannot wrap: `add ax, 1023`
; carries for every U from 64,513 up and answers ZERO KB. A shift and an
; increment cannot.
; -----------------------------------------------------------------------------
cmz_uclaim:
    push ax
    push bx
    call cmz_kb
    mov [cs:cmz_k], ax
    call cmz_mclaim
    pop bx
    pop ax
    ret

; cmz_mclaim - mem_claim AX KB as MEM_K_CMPR: the four claims this file
;              makes, every one of them its own. out: mem_claim's (CF, DX =
;              the base); clobbers BX, as `mov bx, MEM_K_CMPR` always did
cmz_mclaim:
    mov bx, MEM_K_CMPR
    call COLD_SEG:mmf_mem_claim
    ret

; cmz_kb - DX:AX bytes -> AX = KB, rounded UP (always one over at an exact
;          multiple, which both claims are glad of). DX:AX under 16MB, so the
;          answer is under 16,385 and cannot wrap. clobbers: CL, DX, flags
; cmz_ukb - ...of [cs:cmz_u], the source's length
cmz_ukb:
    mov ax, [cs:cmz_u]
    mov dx, [cs:cmz_u+2]
cmz_kb:
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    inc ax
    ret

; -----------------------------------------------------------------------------
; cmz_uread - read fm_onam whole into the claim just made
; in:  [cs:cmz_b] = the base, [cs:cmz_k] = its KB
; out: CF=0 with DX:AX = the bytes delivered and ES = the claim, and ZF = 1
;      when that is [cs:cmz_u] exactly - both whole reads' check; CF=1 with
;      AX = FERR_*
; clobbers: AX (the output), BX, CX, DX, ES, flags
; -----------------------------------------------------------------------------
cmz_uread:
    push si
    mov ax, 1024                ; the claim as a 32-BIT byte count: 66 KB is
    mul word [cs:cmz_k]         ; over 64K and the capacity test is 32-bit
    xchg cx, ax
    mov es, [cs:cmz_b]
    xor bx, bx                  ; a compressed file wants seg:0 and every
                                ; caller in the tree hands over a claim's base
    mov si, fm_onam
    call COLD_SEG:dwf_dskw_read ; DX:AX = what arrived
    jc .r
    cmp dx, [cs:cmz_u+2]
    jne .ok
    cmp ax, [cs:cmz_u]
.ok:
    clc                         ; (ZF is the compare's)
.r:
    pop si
    ret

; -----------------------------------------------------------------------------
; cmz_upkg - expand the package in [cs:cmz_b] into [cs:cmz_o]
; in:  [cs:cmz_u] = image, [cs:cmz_p] = the file, [cs:cmz_x] = the clear prefix
; out: CF=0 and the whole expanded file is at [cs:cmz_o]:0; CF=1 = the stream
;      was refused, or produced a length that is not `image`
; clobbers: AX BX CX DX SI DI ES, flags
;
; ld_expand (SPEC.md 20.13) between two regions rather than inside one: the
; prefix goes across verbatim, which leaves SI exactly on the compressed body,
; and the body expands into a segment `prefix/16` paragraphs along. The FORMAT
; is read back out of the SOURCE header once DS is the source segment, which
; is what saves carrying it through the copy.
; -----------------------------------------------------------------------------
cmz_upkg:
    mov cx, [cs:cmz_x]
    mov bx, [cs:cmz_p]
    sub bx, cx                  ; the body: what is left of the FILE
    mov dx, [cs:cmz_u]
    sub dx, cx                  ; ...and what it must produce
    mov si, [cs:cmz_o]
    mov ax, cx
    push cx
    mov cl, 4
    shr ax, cl                  ; the prefix in PARAGRAPHS - and it IS one
    pop cx
    add ax, si                  ; ...so this is the body's destination segment
    mov di, [cs:cmz_b]
    push ds
    push ax
    mov es, si
    mov ds, di
    xor si, si
    xor di, di
    cld
    rep movsb                   ; the prefix, verbatim - and this leaves SI on
    pop ax                      ; the compressed body, which is where the
    mov es, ax                  ; decoder wants it
    xor di, di                  ; lz_decomp_x REQUIRES this zero, and says why
    mov cx, bx
    mov al, [LD_H_FLAGS]        ; the format, out of the SOURCE header: DS is
    test al, 0x10               ; still the source segment here
    mov al, LZ_LZ4
    jz .fmt
    mov al, LZ_LZB
.fmt:
    xor bx, bx                  ; BX:DX = the expected output, 16 bits by
                                ; ld_check_hdr's own bound on an image
    call COLD_SEG:lzf_decomp
    pop ds                      ; `pop` leaves the flags alone, so the CF
    ret                         ; the decoder set is still the answer

; -----------------------------------------------------------------------------
; cmz_ufree - give back whichever of the two blocks is held
; clobbers: FLAGS ONLY
;
; Both words are zeroed on the way in and cleared as they are freed, so every
; failure path can call this without knowing how far the verb got - which is
; what lets the arms above have one exit each instead of a ladder.
; -----------------------------------------------------------------------------
cmz_ufree:
    push ax
    push bx
    push dx
    mov bx, cmz_b
    call .one
    mov bx, cmz_o
    call .one
    pop dx
    pop bx
    pop ax
    ret
.one:
    mov dx, [cs:bx]
    or dx, dx
    jz .none
    mov word [cs:bx], 0
    mov bx, MEM_K_CMPR
    call COLD_SEG:mmf_mem_free
.none:
    ret

; -----------------------------------------------------------------------------
; cmz_join - Uncompress of a PART: join its split set (SPEC.md 20.17, 22.23.5)
; in:  fm_onam = the selection, a file with no 'CZ' mark that is not a
;      compressed package
; out: cmz_verb's three answers
;
; IT STREAMS, which is the whole difference from the other two arms: one 82KB
; claim whatever the set's size - a 49KB input window the parts are read into
; 16KB at a time, and the 32KB block a compressed record expands into. Every
; block but the set's last expands to exactly 32KB (SPEC.md 20.17.2), so the
; result is a whole number of clusters before every APPEND, which is the rule
; that door enforces (18.4.4).
;
; The result goes to CMPRESS~.TMP and takes its real name at the end, so a
; join that stops half-way leaves the parts untouched and no half-file under
; that name. fm_onam is the part being read - its three digits are rewritten
; per part - and the whole selection is put back on every way out.
;
; Once the claim is held, a failure anywhere below leaves through cmz_jerr
; with AX = a FERR_* (under 16) or a string of this image to say, and the
; stack goes back to this routine's frame.
; -----------------------------------------------------------------------------
CMZ_JWIN    equ 49              ; the input window, KB: a record is at most
                                ; 32,778 bytes and a refill 16KB, so it never
                                ; holds more than 49,161
CMZ_JHP     equ 32              ; ...behind a 512-byte header, paragraphs: what
                                ; a paused Uncompress To... keeps (22.23.6).
                                ; 512 and then ROUNDED UP to 512 physically,
                                ; because every read lands in the window and
                                ; every write leaves the output block by DMA:
                                ; a sector that straddles a 64KB page is
                                ; int 13h error 09h, and only a 512-aligned
                                ; base makes that impossible (dsk_runcap caps
                                ; a RUN at the page, never one sector)
CMZ_JKB     equ CMZ_JWIN + 32 + 1   ; ...and the output block, and the header
                                ; with the rounding's slack: 512 + 496 fits 1KB
CMZ_JRD     equ 0x4000          ; one refill. READ_AT wants whole clusters at a
                                ; cluster-aligned offset, and 16KB is both for
                                ; every cluster this verb accepts
CMZ_JHDR    equ 32              ; a part's header (SPEC.md 20.17.1)
CMZ_JREC    equ 10              ; a block's (20.17.2)
CMZ_JST     equ 16              ; where the saved state sits in the claim's
                                ; header - past CL_STEP, which carries CLS_JOIN

; CMZ_GO src|dst - stand where the parts are, or where the result goes. Two
; volumes on kern_big (Uncompress To..., SPEC.md 22.23.6) and one folder for
; plain Uncompress, where the hop is free; on kern_small there is only the one
; folder, so the hop is not assembled at all.
%macro CMZ_GO 1
%ifdef KERN_BIG
    call cmz_go%1
%endif
%endmacro

; cmz_jopen - the start all three ways in share
; in:  [cmz_jsp] banked by the caller (kern_big: and the source's place)
; out: CF=0 with fm_onam a part (cmz_jisname), the claim held and its state
;      zeroed, standing in the source's folder, on a volume whose clusters
;      a 16KB take reads whole; CF=1 with AX = the refusal, NOTHING held. A
;      failure once the claim is held leaves through cmz_jerr
cmz_jopen:
    call cmz_jisname            ; BASE.NNN, or not ours
    mov ax, cmz_s_plain
    jc .r
    call cmz_jclaim
    mov ax, cmz_s_nomem
    jc .r
    CMZ_GO src
    call COLD_SEG:dwf_dskw_dfree    ; BX = sectors a cluster
    jc .err
    mov ax, cmz_s_clus
    cmp bx, CMZ_JRD / 512
    ja .err
    clc
.r:
    ret
.err:
    jmp cmz_jerr

; cmz_jhdr - a new file (fm_onam): its first 32 bytes, as a part's header
; out: CF=0 with ES:DI = the header, 'CS' version 1; CF=1 it is not one (or
;      the file is shorter). A read error never returns (cmz_jneed)
; clobbers: AX BX CX DX SI DI ES
cmz_jhdr:
    call cmz_jrst
    mov ax, CMZ_JHDR
    call cmz_jneed
    jc .r
    call cmz_jat
    cmp word [es:di], 'CS'      ; the magic...
    jne .no
    cmp word [es:di+2], 1       ; ...version 1 and the reserved zero
    je .r                       ; (equal: CF = 0)
.no:
    stc
.r:
    ret

; cmz_jhname - the name a set rejoins as, out of a part's header: 12 chars
;              at ES:DI+20, NUL-ended at AX:BX. clobbers: CX SI DI ES
cmz_jhname:
    push ds
    push es
    pop ds
    mov es, ax
    lea si, [di+20]
    mov di, bx
    mov cx, 12
    rep movsb
    mov [es:di], cl             ; CX is 0: the NUL
    pop ds
    ret

; cmz_jret   - AX = a string of this image or a FERR_*, said; the caller's
;              "it has spoken" (CF=1, AX=0). Nothing is held
; cmz_jspoke - ...the same answer with nothing said
cmz_jret:
    call cmz_jsayx
cmz_jspoke:
    xor ax, ax
    stc
    ret

; -----------------------------------------------------------------------------
; cmz_join  - plain Uncompress of a PART: the set, joined beside it
; cmz_uncto - Uncompress To... (SPEC.md 22.23.6, kern_big): is the selection a
;             part, and what is it called? Asked BEFORE the Save box, so the
;             box can open on the name the set rejoins as - and the box is
;             opened HERE, through fdf_fdlg_open, with the resident
;             fm_img_done_x as its completion proc
; in:  fm_onam = the selection, a file with no 'CZ' mark that is not a
;      compressed package
; out: cmz_verb's answers. Every refusal and every verdict is said HERE,
;      FERR_*s included (cmz_jsayx, off the kernel's own fm_errtab), so the
;      caller only ever hears CF=0 (the folder changed) or CF=1 with AX = 0
;
; ONE START for both, and for cmz_jgo below: [cmz_jto] says which (0 plain,
; 1 Uncompress To...'s join, 2 its probe), and everything from the claim on
; is the same code - the probe reads the selection's header through the
; join's own window, which is a 16KB take like the one it used to claim for
; itself.
; -----------------------------------------------------------------------------
%ifdef KERN_BIG
cmz_uncto:
    mov ax, cmz_s_jbusy
    cmp word [clo_seg], 0       ; a clone or another join: one claim word
    jne cmz_jret
    mov al, 2                   ; the probe
    jmp short cmz_jstart
%endif
cmz_join:
    xor al, al                  ; plain
cmz_jstart:
    cld
%ifdef KERN_BIG
    mov bl, al
    mov al, [disk_drive]        ; source and target are this one folder, so
    mov ah, al                  ; every hop is a word (fcp_goto's quiet arm)
    mov dx, [dsk_cwd]
    call cmz_jwhere
    call COLD_SEG:dkf_dsk_batch_begin
%else
    mov [cs:cmz_jto], al
%endif
    mov [cs:cmz_jsp], sp
    call cmz_jopen              ; a part, the claim, clusters a take reads whole
    jc cmz_jret                 ; nothing held: say why
    call cmz_jhdr               ; --- the SELECTION first: a plain file that
    jc cmz_jnotcs               ; only looks like a part is not compressed,
%ifdef KERN_BIG                 ; and must not be told part 1 is missing
    cmp byte [cs:cmz_jto], 0
    je cmz_jloop
    ; --- Uncompress To...'s probe: the name it rejoins as, the box's default
    mov ax, ds
    mov bx, fm_hdrbuf
    call cmz_jhname
    mov si, fm_onam             ; ...and the part's own, for the join to read
    mov di, clo_fnbuf           ; back: a cancelled box calls nothing, and the
    call clo_ds13               ; image is not held across it, so it waits in
    call cmz_jundo              ; the resident buffer - with the claim gone
    mov bx, [fm_cmdwin]         ; the Save box: the window the command acts on,
    mov si, fm_hdrbuf           ; the default name, and the RESIDENT completion
    mov di, fm_img_done_x       ; proc, which starts the join through
    mov al, 1                   ; CLV_SAVED with no claim live (22.23.6). CF=1
    call COLD_SEG:fdf_fdlg_open ; (a box already up) cannot happen from a
    jmp short cmz_jspoke        ; menu pick - and either way it has spoken
%else
    jmp short cmz_jloop
%endif

; cmz_jnotcs - not a part at all - or, past part 1, a part of nothing this
;              set knows. A tail, reached by a jump; the frame is banked
cmz_jnotcs:
    cmp word [cs:cmz_jk], 1
    ja cmz_jwrong
    mov ax, cmz_s_plain
    jmp cmz_jerr
cmz_jwrong:
    mov si, cmz_s_wrong
    call cmz_jmsg
    jmp cmz_jerr

; -----------------------------------------------------------------------------
; cmz_jloop - the parts, from the one after [cmz_jk] (SPEC.md 22.23.5)
; in:  the claim held, [cmz_jsp] banked, the state in cmz_jst
; out: through cmz_jend (all read), cmz_jerr (refused) or cmz_jsusp (a disk
;      to ask for) - never by falling off the end
;
; Once the claim is held a failure anywhere below leaves through cmz_jerr with
; AX = a FERR_* (under 16) or a string of this image to say, and the stack goes
; back to the frame banked in [cmz_jsp].
; -----------------------------------------------------------------------------
cmz_jloop:
.part:                          ; --- part k ------------------------------
    mov ax, [cs:cmz_jk]
    inc ax
    cmp ax, [cs:cmz_jn]
    jbe .next
    jmp cmz_jend
.next:
    mov [cs:cmz_jk], ax
    call cmz_jdig
    call cmz_jhdr
    jc cmz_jnotcs
    mov ax, [es:di+4]
    cmp ax, [cs:cmz_jk]
    jne cmz_jwrong              ; not the part its name says
    dec ax
    jnz .same
    mov ax, [es:di+6]           ; --- part 1 says what the set is ------
    dec ax
    cmp ax, 999 - 1
    ja cmz_jnotcs               ; 1..999 parts
    inc ax
    mov [cs:cmz_jn], ax
    mov ax, [es:di+8]
    mov dx, [es:di+10]
    mov [cs:cmz_jtot], ax
    mov [cs:cmz_jtot+2], dx
    mov [cs:cmz_utot], ax       ; ...and the bar is the original's size
    mov [cs:cmz_utot+2], dx
    or ax, dx
    jz cmz_jnotcs
    mov ax, [es:di+16]
    mov [cs:cmz_jid], ax
    mov ax, [es:di+18]
    mov [cs:cmz_jid+2], ax
    cmp byte [cs:cmz_jto], 0    ; Uncompress To... was TOLD the name, and has
    jne .named                  ; checked it and the temporary's already
    mov ax, cs                  ; the name it rejoins as, NUL-ended
    mov bx, cmz_jnam
    call cmz_jhname
    call cmz_jname              ; that name is free, and so is the temporary
.named:
    call cmz_rearm
    call cmz_jat
.same:                          ; --- the same set, and it starts where the
    push ds                     ; last one ended: n, total, offset and ID are
    push cs                     ; the header's bytes 6..20 in cmz_jn..cmz_jid's
    pop ds                      ; order (asserted below), and the offset is
    mov si, cmz_jn              ; cmz_jw - 0 for part 1, which is its check
    add di, 6
    mov cx, 7
    repe cmpsw                  ; REP alone, no segment override (SPEC.md 1's
    pop ds                      ; 8088 hazard)
    jne cmz_jwrong
    add word [cs:cmz_jpos], CMZ_JHDR
    sub word [cs:cmz_javl], CMZ_JHDR

.rec:                           ; --- one block ---------------------------
    mov ax, CMZ_JREC
    call cmz_jneed
    jc .bad                     ; a part with no block, or a header cut short
    call cmz_jat
    mov cx, [es:di]             ; S
    mov dx, [es:di+2]           ; N
    mov ax, [es:di+4]           ; M, and the reserved zero above it
    mov bx, cx
    dec bx
    js .bad                     ; S in 1..32,768: 0 and 32,769 up go negative
    mov bx, dx
    dec bx
    js .bad                     ; N in 1..32,768
    cmp ax, 2
    ja .bad                     ; stored, LZ4 or LZB - and the zero
    or ax, ax
    jnz .comp
    cmp cx, dx
    jne .bad                    ; a stored block is its own length
.comp:
    test word [cs:cmz_jw], 0x7FFF
    jnz .bad                    ; only the set's LAST block may be short
    mov bx, [cs:cmz_jw]
    mov si, [cs:cmz_jw+2]
    add bx, dx
    adc si, 0
    cmp si, [cs:cmz_jtot+2]
    ja .bad
    jb .fits
    cmp bx, [cs:cmz_jtot]
    ja .bad                     ; never past the total
.fits:
    mov [cs:cmz_jm], al
    mov [cs:cmz_js], cx
    mov [cs:cmz_jnn], dx
    mov ax, cx
    add ax, CMZ_JREC
    call cmz_jneed              ; the whole payload in the window
    jc .bad
    mov si, [cs:cmz_jpos]
    add si, CMZ_JREC
    mov ax, [cs:cmz_jwin]
    cmp byte [cs:cmz_jm], 0
    jne .dec
    push ds                     ; STORED: copied into the output block and
    push es                     ; written from there, NOT out of the window -
    mov ds, ax                  ; a record starts anywhere in the window, and a
    mov es, [cs:cmz_o]          ; write is DMA from a 512-aligned base or it
    xor di, di                  ; can straddle a 64KB page (CMZ_JHP's note).
    mov cx, [cs:cmz_jnn]        ; 32KB of rep movsb is ~0.1s against the
    cld                         ; floppy's seconds
    rep movsb
    pop es
    pop ds
    jmp short .out
.bad:                           ; HERE, in the middle of the eleven jumps to
    jmp cmz_jbad                ; it, so every one of them is a short jcc
.dec:
    CMZ_TRACK 1                 ; the decode draws nothing
    push ds
    mov ds, ax
    mov es, [cs:cmz_o]
    xor di, di                  ; lz_decomp_x's precondition
    xor bx, bx
    mov cx, [cs:cmz_js]
    mov dx, [cs:cmz_jnn]
    mov al, [cs:cmz_jm]
    dec al                      ; M is the LZ_* id plus one
    call COLD_SEG:lzf_decomp
    pop ds
    CMZ_TRACK 0                 ; (moves and pushes: CF is the decoder's)
    jc .bad
.out:                           ; the N bytes are at [cmz_o]:0 either way
    push ds
    mov ds, [cs:cmz_o]
    xor si, si
    mov cx, [cs:cmz_jnn]
    call cmz_jsum               ; AX = A, DX = B
    pop ds
    call cmz_jat
    cmp ax, [es:di+6]
    jne .bad
    cmp dx, [es:di+8]
    jne .bad                    ; a damaged copy
    CMZ_GO dst                  ; --- out it goes ------------------------
    mov es, [cs:cmz_o]
    xor bx, bx
    mov cx, [cs:cmz_jnn]
    mov si, fm_ebuf
    cmp byte [cs:cmz_jwrote], 0
    jne .ap
    xor dx, dx
    call COLD_SEG:dwf_dskw_write    ; the first creates the file...
    mov byte [cs:cmz_jwrote], 1 ; (`mov` leaves CF alone)
    jmp short .wchk
.ap:
%ifdef KERN_BIG
    mov di, [cs:cmz_jtok]       ; ...and every later one grows it, as ONE
    mov al, WSQF_HELD           ; HELD stream (SPEC.md 18.4.9): no lookup and
    call COLD_SEG:dwf_dskw_write_seq    ; no walk per block, and no FAT or
    mov [cs:cmz_jtok], di       ; directory write until the close below. The
                                ; result is useless until it is whole, and a
                                ; join that stops deletes it, so HELD's one
                                ; loss is nothing. A paused join reloads this
                                ; image, and a token of 0 is merely COLD
%else
    call COLD_SEG:dwf_dskw_append
%endif
.wchk:
    jnc .wok
    jmp cmz_jerr
.wok:
    mov ax, [cs:cmz_jnn]
    mov bx, cmz_jw              ; (BX is reloaded before .rec reads it)
    call cmz_add1
    mov bx, cmz_done
    call cmz_add1
    call cmz_rearm
    mov ax, [cs:cmz_js]
    add ax, CMZ_JREC
    add [cs:cmz_jpos], ax
    sub [cs:cmz_javl], ax
    mov ax, 1                   ; ...and is there more of this part?
    call cmz_jneed
    jc .pnext
    jmp .rec
.pnext:
    jmp .part

cmz_jbad:
    mov ax, cmz_s_bad
    ; ...into cmz_jerr

; cmz_jerr - leave the join from anywhere below its frame, the claim held
; in:  AX = a FERR_* (under 16) or a string of this image
cmz_jerr:
    ; STKBALANCE-OK: abandons the stack on purpose - SP goes back to the frame
    ; the join banked once its claim was held, whatever was pushed since
    mov sp, [cs:cmz_jsp]
    push ax
    call cmz_jrmtmp             ; a result half-written goes
    pop ax
    stc                         ; ...and it is a refusal
    ; ...into cmz_jexit

; cmz_jexit - the join ends, every way: the claim back, the bar ended, the
;             selection's name restored, and AX said
; in:  AX = a FERR_* (under 16, said off the kernel's own fm_errtab), a string
;      of this image, or 0 = nothing to say; CF = 1 it failed. Every way here
;      is reached with the frame [cmz_jsp] names under SP
; out: plain Uncompress (and Uncompress To's probe): cmz_verb's answers - CF=0
;      the folder changed, CF=1 with AX=0 it has spoken. Uncompress To...'s
;      join: AL = CLA_JDONE, CF = 0, with [clo_seg] empty
cmz_jexit:
    pushf
    push ax
%ifdef KERN_BIG
    cmp byte [cs:cmz_jto], 1
    jne .keep
    mov word [clo_seg], 0       ; cleared FIRST, clo_free's reason: the free
.keep:                          ; can compact. cmz_drop frees [cmz_b], which
%endif                          ; IS the claim [clo_seg] named
    call cmz_jundo
    pop ax
    or ax, ax
    jz .quiet
    call cmz_jsayx
.quiet:
%ifdef KERN_BIG
    cmp byte [cs:cmz_jto], 1
    je .to
%endif
    popf
    mov ax, 0                   ; (`mov` keeps CF)
    ret
%ifdef KERN_BIG
.to:
    popf
    mov al, CLA_JDONE
    clc
    ret
%endif

; cmz_jend - every part read: the length must be the total, and the result
; takes its name. Reached from cmz_jloop with the frame banked
cmz_jend:
    mov ax, [cs:cmz_jw]
    cmp ax, [cs:cmz_jtot]
    jne cmz_jbad
    mov ax, [cs:cmz_jw+2]
    cmp ax, [cs:cmz_jtot+2]
    jne cmz_jbad           ; short of what part 1 promised
    CMZ_GO dst
    mov si, cmz_jnam            ; the result takes its name: fm_onam,
    mov di, fm_onam             ; because the file layer reads names from DS
    call cmz_cs13
%ifdef KERN_BIG
    call cmz_jtmpn              ; THE CLOSE: the held stream's link, FAT and
    xor cx, cx                  ; size, before the rename - whose own gate
    call COLD_SEG:dwf_dskw_write_seq    ; would commit it too, but DROPS a
    jc .err                     ; commit's failure on the floor. The size is
                                ; exactly what cmz_jw counted, so a failure
                                ; here is the disk's, and leaves through
                                ; cmz_jerr like any other write
%endif
    mov byte [cs:cmz_jwrote], 0 ; a failed rename leaves the result on the
    mov si, fm_ebuf             ; disk under the temporary name, as the
    mov di, fm_onam             ; streamed Compress does (SPEC.md 22.22.5)
    call COLD_SEG:dwf_dskw_rename
    jc .err
    mov ax, cmz_s_undone        ; a WORD and not a number, and CF = 0
    jmp short cmz_jexit
.err:
    jmp cmz_jerr

; cmz_jundo - the claim back, the bar ended, the selection's name restored
; clobbers: AX CX SI DI, flags
cmz_jundo:
    call cmz_drop
cmz_jsel2on:                    ; ...the name alone, for a join that pauses
    mov si, cmz_jsel
    mov di, fm_onam
    jmp short cmz_cs13

; cmz_jrmtmp - CMPRESS~.TMP goes, if this join created it. clobbers: SI
cmz_jrmtmp:
    cmp byte [cs:cmz_jwrote], 0
    je .no
%ifdef KERN_BIG
    push ax                     ; NOT `CMZ_GO dst`: this runs INSIDE cmz_jerr,
    push bx                     ; and cmz_go's failure path IS cmz_jerr - so a
    mov al, [cs:cmz_jtgtd]      ; hop that failed here went back round with
    mov bx, [cs:cmz_jtgtc]      ; [cmz_jwrote] still set, for ever (SPEC.md
    call COLD_SEG:fcpf_fcp_goto ; 22.23.6.1). A target it cannot reach keeps
    pop bx                      ; its CMPRESS~.TMP, as a failed rename does
    pop ax
    jc .no
%endif
    call cmz_jtmpn
    mov si, fm_ebuf
    call COLD_SEG:dwf_dskw_delete
    mov byte [cs:cmz_jwrote], 0
.no:
    ret

; cmz_jtmpn - the temporary's name into fm_ebuf, where the file layer reads
; it (a prompt in between clears that buffer). clobbers: SI
cmz_jtmpn:
    mov si, cmz_s_tmpn
; cmz_jcp13 - 13 bytes from CS:SI to fm_ebuf. clobbers: SI
cmz_jcp13:
    push di
    mov di, fm_ebuf
    call cmz_cs13
    pop di
    ret

; cmz_cs13 - 13 bytes from CS:SI to DS:DI: a name of this image's out to where
;            the file layer or the file dialog reads it. clobbers: SI DI
cmz_cs13:
    push ax
    push cx
    mov cx, 13
.c:
    mov al, [cs:si]
    inc si
    mov [di], al
    inc di
    loop .c
    pop cx
    pop ax
    ret

; cmz_jname - the result's name must be free, and so must the temporary's;
;             leaves the temporary's in fm_ebuf. A failure never returns
cmz_jname:
    CMZ_GO dst
    mov si, cmz_jnam
    call .try
    mov si, cmz_s_tmpn
.try:
    call cmz_jcp13
    mov si, fm_ebuf
    call COLD_SEG:dwf_dskw_stat
    jc .absent
    mov ax, FERR_EXIST          ; it exists, and nothing is overwritten
    jmp cmz_jerr
.absent:
    cmp ax, FERR_NOENT
    jne .err                    ; a name the file layer will not take
    ret
.err:
    jmp cmz_jerr

; cmz_jisname - is fm_onam BASE.NNN? CF=1 it is not. CF=0 with [cmz_jext] =
;               its digits and cmz_jsel = the whole name. clobbers: AX CX SI DI
cmz_jisname:
    cld
    mov si, fm_onam
.dot:
    mov al, [si]                ; (DS, deliberately: os88ovlchk)
    inc si
    or al, al
    jz .no
    cmp al, '.'
    jne .dot
    mov [cs:cmz_jext], si
    mov cx, 3
.dig:
    mov al, [si]                ; (DS, deliberately: os88ovlchk)
    inc si
    sub al, '0'
    cmp al, 9
    ja .no
    loop .dig
    cmp byte [si], 0
    jne .no
    mov si, fm_onam             ; the selection, for every way out: DS:SI
    push cs                     ; to ES:DI = CS (ES is both callers' to spend
    pop es                      ; - each reloads it next)
    mov di, cmz_jsel
    mov cx, 13
    rep movsb
    clc
    ret
.no:
    stc
    ret

; cmz_jclaim - the join's one claim, and its state zeroed. CF=1 = no room.
; clobbers: AX BX DX
cmz_jclaim:
    mov ax, CMZ_JKB
    call cmz_mclaim
    jc .no
    mov [cs:cmz_b], dx
    call cmz_jslice
    xor ax, ax
    mov [cs:cmz_jwrote], al
    mov [cs:cmz_jretry], ax
    push cs                     ; ES = CS for two runs of stosw (DF = 0: the
    pop es                      ; join's entry cleared it)
    mov di, cmz_jk              ; cmz_jk = 0, cmz_jn = 1 - one part until it
    stosw                       ; says how many - and cmz_jtot and cmz_jw = 0,
    inc ax                      ; the part's header order (asserted at the
    stosw                       ; data)
    dec ax
    stosw
    stosw
    stosw
    stosw
    mov di, cmz_utot            ; ...and the bar: 1 until part 1 says what it
    inc ax                      ; is, and nothing done (cmz_done follows)
    stosw
    dec ax
    stosw
    stosw
    stosw
    clc
.no:
    ret

; cmz_jslice - the window and the output block, off [cmz_b]. clobbers: DX
cmz_jslice:
    mov dx, [cs:cmz_b]
    add dx, CMZ_JHP + 31        ; behind the header, and up to 512 bytes
    and dx, 0xFFE0              ; physically (32 paragraphs)
    mov [cs:cmz_jwin], dx
    add dx, CMZ_JWIN * 64
    mov [cs:cmz_o], dx
    ret

; cmz_jat - ES:DI = the input window at its read position ([cmz_jwin]:
;           [cmz_jpos]). clobbers: ES, DI
cmz_jat:
    mov es, [cs:cmz_jwin]
    mov di, [cs:cmz_jpos]
    ret

; -----------------------------------------------------------------------------
; cmz_jneed - make AX bytes at [cmz_jpos] present in the input window
; out: CF=0 they are; CF=1 the part ends first. A read error never returns -
;      and FERR_NOENT on a part's first read is that part MISSING: said by
;      name, or - Uncompress To... reading a floppy - asked for (22.23.6)
; clobbers: AX BX CX DX SI DI ES, flags
;
; When the window is short it is COMPACTED - what is left moved to its floor
; - and 16KB chunks are read in behind it until it is not. Every read is at a
; file offset that is a multiple of 16KB until the short one that ends the
; part, which is READ_AT's rule (SPEC.md 18.4.4).
; -----------------------------------------------------------------------------
cmz_jneed:
    cmp [cs:cmz_javl], ax
    jb .need
    ret                         ; CF = 0: they are
.need:
    push ax
    push ds
    mov cx, [cs:cmz_javl]
    mov si, [cs:cmz_jpos]
    mov es, [cs:cmz_jwin]
    push es
    pop ds
    mov di, cx                  ; what is left moves to the floor - but so that
    neg di                      ; it ENDS on 512, where the next read starts:
    and di, 511                 ; that read is DMA (CMZ_JHP's note)
    mov [cs:cmz_jpos], di
    cld
    cmp di, si
    jbe .fwd                    ; down, or nowhere: forward is safe
    add si, cx                  ; UP by under 512 - an overlap a forward copy
    dec si                      ; would eat, so it runs from the top
    add di, cx
    dec di
    std
.fwd:
    rep movsb
    cld
    pop ds
.more:
    pop ax
    push ax
    cmp [cs:cmz_javl], ax
    jae .got
    cmp byte [cs:cmz_jeof], 0
    jne .short
    CMZ_GO src
    mov es, [cs:cmz_jwin]
    mov bx, [cs:cmz_jpos]
    add bx, [cs:cmz_javl]       ; a multiple of 512, by the compaction
    mov cx, CMZ_JRD
    mov ax, [cs:cmz_jfo]
    mov dx, [cs:cmz_jfo+2]
    mov si, fm_onam
    CLO_RSEQ cs, cmz_jcur       ; DX:AX = what arrived, DX = 0 - one FAT
    jc .err                     ; link a chunk (SPEC.md 18.4.8.1)
    call cmz_rearm
    add [cs:cmz_javl], ax
    mov bx, cmz_jfo             ; (BX is this routine's to clobber)
    call cmz_add1
    cmp ax, CMZ_JRD
    je .more
    mov byte [cs:cmz_jeof], 1   ; a short take is the end of the part
    jmp short .more
.got:
    pop ax
.ok:
    clc
    ret
.short:
    pop ax
    stc
    ret
.err:
    cmp ax, FERR_NOENT
    jne .fail
    mov ax, [cs:cmz_jfo]
    or ax, [cs:cmz_jfo+2]
    jnz .gone                   ; it was there a moment ago: say the FERR
%ifdef KERN_BIG
    call cmz_jfloppy            ; a floppy that is not the target's drive:
    jc .miss                    ; ASK for it (SPEC.md 22.23.6)
    jmp cmz_jsusp
.miss:
%endif
    mov si, cmz_s_miss
    call cmz_jmsg
    jmp cmz_jerr
.gone:
    mov ax, FERR_NOENT
.fail:
    jmp cmz_jerr

; cmz_jrst - a new file: the window empty and the next read at offset 0
; clobbers: AX
cmz_jrst:                       ; clobbers: AX, CX, DI, ES (cmz_jhdr's
    push cs                     ; caller reloads all four)
    pop es
    mov di, cmz_jfo             ; cmz_jfo, cmz_jpos, cmz_javl and cmz_jeof:
    mov cx, cmz_jeof + 1 - cmz_jfo  ; one run of bytes (asserted at the data)
    xor ax, ax
    rep stosb
%ifdef KERN_BIG
    mov [cs:cmz_jcur+RSQ_GEN], ax   ; the cursor names no file (18.4.8.1)
%endif
    ret

; cmz_jdig - the three digits at [cmz_jext] := [cmz_jk], 1..999. clobbers:
;            AX BX DI
cmz_jdig:
    mov ax, [cs:cmz_jk]
    mov di, [cs:cmz_jext]
cmz_jdigax:                     ; ...AX's digits at DI, for the prompt's line
    mov bl, 10
    div bl                      ; AL = k/10 (under 100), AH = the units
    add ah, '0'
    mov [di+2], ah
    xor ah, ah
    div bl
    add ax, '00'                ; AL the hundreds, AH the tens
    mov [di], ax
    ret

; cmz_jsum - SPEC.md 20.17.2's check over DS:SI, CX = 1..32,768 bytes
; out: AX = A, DX = B. clobbers: BX CX SI, flags
;
; Two sums over little-endian words, an odd last byte zero-extended: A adds
; each word and B adds each A, so a pair of swapped words - which one sum
; would pass - moves B. ~10 cycles a byte on an 8088 against LZ4's ~50.
cmz_jsum:
    xor bx, bx
    xor dx, dx
    cld
    shr cx, 1
    pushf                       ; CF = the odd byte
    jcxz .odd
.w:
    mov ax, [si]
    inc si
    inc si
    add bx, ax
    add dx, bx
    loop .w
.odd:
    popf
    jnc .done
    mov al, [si]                ; (DS, deliberately: os88ovlchk)
    inc si
    xor ah, ah
    add bx, ax
    add dx, bx
.done:
    mov ax, bx
    ret

; cmz_jmsg - 'Missing FILEA.002': the string at CS:SI, then fm_onam, into
;            cmz_buf. out: AX = cmz_buf. clobbers: SI DI, flags
cmz_jmsg:
    push es
    push cs
    pop es
    mov di, cmz_buf
.p:
    mov al, [cs:si]
    inc si
    or al, al
    jz .n
    stosb
    jmp short .p
.n:
    mov si, fm_onam
.q:
    mov al, [si]                ; (DS, deliberately: os88ovlchk)
    inc si
    stosb
    or al, al
    jnz .q
    pop es
    mov ax, cmz_buf
    ret

%ifdef KERN_BIG
; =============================================================================
; Uncompress To... (SPEC.md 22.23.6) - kern_big's alone
; =============================================================================
; cmz_gosrc / cmz_godst - stand in the source's folder or the target's, through
; the far door CLONE.DRV already uses (fcp_goto, SPEC.md 18.99.8). Free inside
; the run's batch bracket (SPEC.md 18.9.3), and a word when the two are one
; folder. A failure never returns (cmz_jerr). clobbers: flags
cmz_gosrc:
    push ax
    push bx
    mov al, [cs:cmz_jsrcd]
    mov bx, [cs:cmz_jsrcc]
    jmp short cmz_go
cmz_godst:
    push ax
    push bx
    mov al, [cs:cmz_jtgtd]
    mov bx, [cs:cmz_jtgtc]
cmz_go:
    call COLD_SEG:fcpf_fcp_goto
    jc .err
    pop bx
    pop ax
    ret
.err:
    jmp cmz_jerr                ; (AX = FERR_*)

; cmz_jfloppy - may a missing part be ASKED for? CF=0 = yes: this is
;               Uncompress To..., the source is a BIOS floppy, and the result
;               is not being written to that same drive. clobbers: AX BX DL
cmz_jfloppy:
    cmp byte [cs:cmz_jto], 1    ; the join, not plain Uncompress or the probe
    jne .no
    mov al, [cs:cmz_jsrcd]
    cmp al, [cs:cmz_jtgtd]
    je .no                      ; swapping it would take the result out too
    mov bl, al
    jmp clo_flop                ; ...the cloner's own question
.no:
    stc
    ret

; cmz_jsusp - part [cmz_jk] is not on the floppy: keep everything in the
; claim and ask for it (SPEC.md 22.23.6). Reached from cmz_jneed, so the
; stack goes back to the frame first. out: AL = CLA_LINE, CF = 0
cmz_jsusp:
    ; STKBALANCE-OK: abandons the stack on purpose, cmz_jerr's reason
    mov sp, [cs:cmz_jsp]
    mov ax, [cs:cmz_jk]         ; Enter was pressed for THIS part and it is
    cmp ax, [cs:cmz_jretry]     ; still not there: say so, and ask again. A
    jne .quiet                  ; LATER part missing is the first time it has
    mov si, cmz_s_miss          ; been asked for, and says nothing - it was a
    call cmz_jmsg               ; flag, set by Enter and cleared only here, so
    call cmz_say                ; reading part 2 off the disk put in and
.quiet:                         ; missing part 3 said 'Missing SET.003'
    call cmz_jsave
    call cmz_jsel2on            ; the selection's name back
    call KERNEL_SEG:fpg_end     ; the bar is not up while the prompt is
    mov al, CLA_LINE
    clc
    ret

; cmz_jsave / cmz_jload - the state block, to and from the claim's header
; clobbers: CX SI DI, flags
cmz_jsave:
    push ds
    push es
    mov es, [cs:cmz_b]
    push cs
    pop ds
    mov si, cmz_jst
    mov di, CMZ_JST
    jmp short cmz_jmv
cmz_jload:
    push ds
    push es
    mov ds, [clo_seg]
    push cs
    pop es
    mov si, CMZ_JST
    mov di, cmz_jst
cmz_jmv:
    mov cx, CMZ_JSTL
    cld
    rep movsb
    pop es
    pop ds
    ret

; -----------------------------------------------------------------------------
; cmz_jgo - CLV_SAVED with no claim live: the Save box answered Uncompress
; To... (SPEC.md 22.23.6), through the resident fm_img_done_x
; in:  the TARGET is the volume and folder the box left current; the SOURCE is
;      the requester window's, [fm_vp] (fm_img_done_x re-aimed it, and the box
;      is modal, so nothing moved it); clo_fnbuf = the part, fdlg_name = the
;      result's name
; out: AL = CLA_LINE (a disk is asked for) or CLA_JDONE (it ended, and said
;      so), CF = 0
; -----------------------------------------------------------------------------
cmz_jgo:
    cld
    mov si, [fm_vp]             ; the source's drive and the target's...
    mov al, [si+FS_DRV]
    mov ah, [disk_drive]
    mov dx, [si+FS_CWD]         ; ...and the parts' folder
    mov bl, 1                   ; Uncompress To...
    call cmz_jwhere
    mov si, clo_fnbuf           ; the part's name where the file layer and
    mov di, fm_onam             ; cmz_jisname read it
    call clo_ds13
    push ds                     ; the result's name, as the box gave it -
    pop es                      ; cmz_jhname's copy, off a "header" 20 bytes
    mov di, fdlg_name - 20      ; before it
    mov ax, cs
    mov bx, cmz_jnam
    call cmz_jhname
    call COLD_SEG:dkf_dsk_batch_begin
    mov [cs:cmz_jsp], sp
    call cmz_jopen              ; a part (checked once already), the claim,
    jc .say                     ; and the SOURCE's clusters
    mov ax, [cs:cmz_b]          ; the claim IS [clo_seg], marked as ours:
    mov [clo_seg], ax           ; mode 7's keys, lines and teardown come here
    mov es, ax                  ; through clo_disp_x (SPEC.md 22.23.6)
    mov byte [es:CL_STEP], CLS_JOIN
    call cmz_jsave              ; the header is never garbage: k = 0 until a
                                ; prompt saves the part it wants
    call cmz_jname              ; the result's name free, BEFORE part 1 - which
    jmp cmz_jloop               ; may be on a disk that is not in yet
.say:                           ; nothing held
    call cmz_say
    mov al, CLA_JDONE
    clc
    ret

; -----------------------------------------------------------------------------
; cmz_jwhere - the join's places, in cmz_jst's order: AL/AH = the parts' and
;              the result's drive, DX = the parts' folder, BL = cmz_jto; the
;              result's folder is [dsk_cwd] on both callers. DF = 0.
;              clobbers: AX, DI, ES, flags
cmz_jwhere:
    push cs
    pop es
    mov di, cmz_jsrcd
    stosw                       ; cmz_jsrcd, cmz_jtgtd
    xchg ax, dx
    stosw                       ; cmz_jsrcc
    mov ax, [dsk_cwd]
    stosw                       ; cmz_jtgtc
    xchg ax, bx
    stosb                       ; cmz_jto
    ret

; cmz_jdisp - mode 7's verbs while the join owns [clo_seg] (SPEC.md 22.23.6)
; in:  DL = CLV_KEY (AL = the key), CLV_LINE (AL = which line, DI = the
;      buffer) or CLV_FREE; clo_disp_x has checked the marker
; -----------------------------------------------------------------------------
cmz_jdisp:
    cmp dl, CLV_LINE
    jne .notline
    jmp cmz_jline
.notline:
    cld
    call cmz_jload              ; the paused state - clobbers CX SI DI
    mov cx, [clo_seg]
    mov [cs:cmz_b], cx
    mov [cs:cmz_jsp], sp        ; a hop that fails lands in cmz_jerr
    cmp dl, CLV_KEY
    jne .abort                  ; CLV_FREE: the prompt was abandoned - a click
    cmp al, 13                  ; elsewhere, another prompt - and what was
    je .resume                  ; written goes with it
    cmp al, 27
    je .abort                   ; Esc: stopped, and the user said so, so
    mov al, CLA_LINE            ; nothing is said back. Any other key: the
    clc                         ; prompt stands
    ret
.abort:                         ; the temporary deleted, the claim back and
    call cmz_jrmtmp             ; nothing said: AL = CLA_JDONE, CF = 0
    xor ax, ax
    jmp cmz_jexit
.resume:                        ; --- Enter: the disk is in ---------------
    call cmz_jslice
    mov word [cs:cmz_jsrcc], 0  ; the ROOT of the disk now in the drive: a
                                ; folder is a cluster on the OLD disk
    mov ax, [cs:cmz_jk]         ; the part Enter says is in now - a miss of
    mov [cs:cmz_jretry], ax     ; THAT one is news, and of a later one is not
    call cmz_jsel2on            ; fm_onam = the part's name again...
    call cmz_jisname            ; ...and [cmz_jext] = its digits
    call cmz_jtmpn              ; the temporary's name, which the prompt's
                                ; arming cleared out of fm_ebuf
    push ds                     ; the bar, from where it got to: the total
    push es                     ; and what is written, into its scale and
    push cs                     ; its progress - both pairs adjacent, in the
    pop ds                      ; same order (asserted)
    push cs
    pop es
    mov si, cmz_jtot
    mov di, cmz_utot
    mov cx, 4
    rep movsw
    pop es
    pop ds
    dec word [cs:cmz_jk]        ; .part moves on to the one that was missing
    call COLD_SEG:dkf_dsk_batch_begin
    call cmz_rearm
    jmp cmz_jloop

; cmz_jline - CLV_LINE: 'Put FILEA.002 in A:' over 'Enter=ready  Esc=stop'
; in:  AL = 0 / 1, DI = the kernel buffer. out: DI on the NUL, CF = 0
;
; Out of the claim's HEADER and never through cmz_jload: the window can be
; repainted while a resumed join is running, and a load would put the paused
; state over the live one.
CMZ_HSEL    equ CMZ_JST + cmz_jsel - cmz_jst    ; where three of cmz_jst's
CMZ_HK      equ CMZ_JST + cmz_jk - cmz_jst      ; fields sit in the claim's
CMZ_HSRC    equ CMZ_JST + cmz_jsrcd - cmz_jst   ; header - OFFSETS in the
                                ; claim, not data of this image
cmz_jline:
    push ax
    push bx
    push si
    push es
    or al, al
    mov si, cmz_s_jkeys
    jnz .one
    mov es, [clo_seg]
    mov si, cmz_s_put
    call clo_cat
    mov si, CMZ_HSEL   ; the part's name, with the missing one's
    xor bx, bx                  ; digits: BX = where they go
.nm:
    mov al, [es:si]
    inc si
    or al, al
    jz .nmd
    mov [di], al
    inc di
    cmp al, '.'
    jne .nm
    mov bx, di
    jmp short .nm
.nmd:
    or bx, bx
    jz .nodig
    push di
    mov di, bx
    mov ax, [es:CMZ_HK]
    call cmz_jdigax
    pop di
.nodig:
    mov si, cmz_s_in
    call clo_cat
    mov al, [es:CMZ_HSRC]
    call clo_drv
    mov si, cmz_s_colon
.one:
    call clo_cat                ; ...which writes the NUL and leaves DI on it
    pop es
    pop si
    pop bx
    pop ax
    clc
    ret
%endif


; -----------------------------------------------------------------------------
; cmz_say - AX -> a NUL string in THIS image, as a toast (SPEC.md 59)
; clobbers: nothing but the flags
;
; ES:SI and not DS:SI, which is why the strings can live out here at all: the
; slot takes the segment from the caller (OSAPI_CLIP_PUT's reason - the text
; is often in a heap claim, and a module image IS one).
; -----------------------------------------------------------------------------
cmz_say:
    push ax
    push cx
    push si
    push es
    push cs
    pop es
    mov si, ax
.tail:
    xor cx, cx                  ; TOAST_TICKS, the default life
    call KERNEL_SEG:CMZ_XTOAST
    pop es
    pop si
    pop cx
    pop ax
    ret

; cmz_jsayx - AX = a FERR_* (under 16, said off the kernel's own fm_errtab)
;             or a string of this image. clobbers: nothing but the flags
cmz_jsayx:
    cmp ax, 16
    jae cmz_say
    push ax
    push cx
    push si
    push es
    push ds                     ; the kernel's own string, through DS
    pop es
    mov si, ax
    add si, si
    mov si, [fm_errtab+si]
    jmp short cmz_say.tail

; -----------------------------------------------------------------------------
; cmz_pct - 'Compressed to 47%' (SPEC.md 22.22)
; in:  [cs:cmz_z] = the packed length, [cs:cmz_p] = what the file occupied
;
; THE PERCENTAGE AND NOT THE TWO SIZES, because the sizes are on the listing
; behind the toast and the ratio is what the operation was for. Neither the
; multiply nor the divide can overflow: both halves are brought under 65,536
; first, and the quotient is under 100, by the `jae .rworse` that got us here
; - which is also why two digits is the whole of the conversion.
; -----------------------------------------------------------------------------
cmz_pct:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, cmz_buf             ; ...which FOLLOWS cmz_s_done (asserted at the
                                ; scratch), so the digits complete the sentence
                                ; in place and nothing is copied
    mov ax, [cs:cmz_z]          ; the FILE, header included: both arms store
    mov dx, [cs:cmz_z+2]        ; the whole length rather than the stream's
    mov bx, [cs:cmz_p]
    mov cx, [cs:cmz_p+2]
.fit:                           ; both halves shifted together until the old
    jcxz .fits                  ; size is one word: the ratio is what is
    shr cx, 1                   ; printed, and two digits of it survive any
    rcr bx, 1                   ; shift this can make (SPEC.md 22.22.4)
    shr dx, 1
    rcr ax, 1
    jmp short .fit
.fits:
    mov cx, 100
    mul cx                      ; DX:AX = the new size in hundredths...
    div bx                      ; ...over the old, so AX is the percentage
    cmp ax, 99
    jbe .pct
    mov ax, 99                  ; the shift can round the two to one number;
.pct:                           ; the file DID get smaller, so it says 99
    aam                         ; AH = tens, AL = units: AX is under 100
    add ax, '00'
    cmp ah, '0'
    je .ones
    mov [cs:di], ah
    inc di
.ones:
    mov [cs:di], al
    mov word [cs:di+1], '%'     ; ...and the NUL, the word's high byte
    mov ax, cmz_s_done
    call cmz_say
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cmz_claim - ONE block for the source, the output and the tables
; in:  [cs:cmz_u] = U
; out: CF=0 with DX = the base segment, [cs:cmz_k] = the source's KB and
;      [cs:cmz_w] = the window that fitted; CF=1 = not at any window size
; clobbers: DX (an output) and the flags
;
;   base + 0                 the source,  KU KB
;   base + KU*64             the output,  KU+1 KB - the odd KB is what the
;                            'CZ' header goes in front of the stream in
;   base + (2*KU + 1)*64     head[] 8KB, then prev[] at CMZ_PREV
;
; ONE CLAIM AND NOT THREE, because the three are one lifetime and because
; three claims into a fragmented heap can fail where one run would not:
; mem_claim compacts, so the run it can give is the run compaction would leave
; (SPEC.md 50.3).
;
; THE WINDOW IS THE DIAL. A machine that cannot fund 40KB of tables gets a
; shorter history and a slightly worse ratio, which is the whole difference;
; refusing outright would make the verb absent from the machine most likely to
; want it (SPEC.md 20.15.1).
; -----------------------------------------------------------------------------
cmz_claim:
    push ax
    push bx
    push cx
    call cmz_ukb                ; the source's KB, ROUNDED UP - and NOT through
                                ; mem_bytes_kb_x, whose own banner says a new
                                ; caller must prove its input cannot wrap: this
                                ; one cannot, `add ax, 1023` carries for every
    mov [cs:cmz_k], ax          ; U from 64,513 up and answers ZERO KB. A shift
                                ; and an increment is a KB more than needed
                                ; when U is an exact multiple of 1,024, which
                                ; the output region is glad of anyway
    add ax, ax                  ; the source and output regions...
    add ax, CMZ_PREV / 1024 + 1 ; ...plus head[]'s 8, and the header's KB
    call cmz_wclaim
    pop cx
    pop bx
    pop ax
    ret

; cmz_wclaim - the claim with the window in it, LARGEST WINDOW FIRST: halved
;              from CMZ_WMAX until a claim fits or CMZ_WMIN does not
; in:  AX = the claim's KB less the window's own prev[] (window/512 KB)
; out: CF=0 with DX = the base and [cs:cmz_w] = the window; CF=1 none fits
; clobbers: AX, BX, DX, flags (both arms' loop - cmz_claim's and cmz_stream's)
cmz_wclaim:
    push cx
    push si
    xchg si, ax
    mov word [cs:cmz_w], CMZ_WMAX
.try:
    mov ax, [cs:cmz_w]
    mov cl, 9
    shr ax, cl                  ; prev[] is 2 bytes a position: window/512 KB
    add ax, si
    call cmz_mclaim
    jnc .out
    shr word [cs:cmz_w], 1
    cmp word [cs:cmz_w], CMZ_WMIN
    jae .try                    ; ...and below it CF is set: the refusal
.out:
    pop si
    pop cx
    ret

; cmz_drop - give the block back, and end the bar (module internal)
; clobbers: FLAGS ONLY
;
; fpg_end as well, because three of the failure paths leave through here with
; SPEC.md 12.8's bar still scaled to a compress that is not happening. On the
; success path dskw_write_x has already ended it and a second one is a store
; of a zero over a zero.
cmz_drop:
    push ax
    push bx
    push dx
    mov bx, MEM_K_CMPR
    mov dx, [cs:cmz_b]
    call COLD_SEG:mmf_mem_free
    call KERNEL_SEG:fpg_end
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cmz_pkg - may this `.o88` be compressed, and what must stay clear?
; in:  ES = the source segment, the whole file at ES:0; [cs:cmz_u] = its size
; out: CF=0 and [cs:cmz_x] = the clear prefix; CF=1 with AX = its refusal string
; clobbers: AX (the output), flags
;
; os88pkg.py's `compress_pkg` refusals, on the machine, and they are the same
; refusals for the same reasons - a package this verb writes has to be one
; ld_check_hdr and ld_run_body already accept, and both of those shipped in
; wave 2 (SPEC.md 20.13). Nothing new is being defined here.
; -----------------------------------------------------------------------------
cmz_pkg:
    push bx
    push dx
    cmp word [cs:cmz_u+2], 0
    jne .no1                    ; over 64KB: not an image ld_check_hdr takes
    mov al, [es:LD_H_FLAGS]
    test al, 4
    jnz .no1                    ; PARTS (SPEC.md 20.12): a part's offset is
                                ; measured from the start of the FILE and the
                                ; table lives INSIDE the image, so compressing
                                ; the image and laying out its parts are
                                ; circular. os88pkg.py refuses it too, in the
                                ; same words
    test al, 0x18
    jnz .already                ; a compression bit is already set
    mov dx, [es:LD_H_IMG]
    cmp dx, [cs:cmz_u]
    jne .no1                    ; `image` is not the file: something this verb
                                ; does not understand, and 20.13.2's guard
                                ; would refuse whatever it wrote
    mov bx, dx
    add bx, [es:LD_H_BSS]
    jc .big
    cmp bx, APP_MAX_SIZE
    ja .big                     ; ...so cmz_hdr's two roundups cannot carry
    call cmz_cpre               ; DX = [cs:cmz_x] = the CLEAR prefix
    add dx, CMZ_SLACK
    cmp dx, [cs:cmz_u]
    jae .no1                    ; near enough all prefix: there is no body
    pop dx
    pop bx
    clc
    ret
.no1:
    mov ax, cmz_s_pkg
    jmp short .no
.big:
    mov ax, FERR_BIG          ; 'Too large': the kernel's own (fm_errtab)
    jmp short .no
.already:
    mov ax, cmz_s_alr
.no:
    pop dx
    pop bx
    stc
    ret

; cmz_cpre - AL = a package's LD_H_FLAGS -> DX = [cs:cmz_x] = its CLEAR prefix,
; ld_cpre's own ladder (loader.inc; SPEC.md 20.13.1): what the mount reads out
; of the first sector and must never have to decompress to get - the header,
; the icon, the association block and the document glyph (SPEC.md 54.3.2). At
; most 128, and always a multiple of 16 - which is what lets a source or a
; destination be a SEGMENT and the 'CZ' header land on a paragraph in front of
; the stream. Compress and Uncompress both ask it, so the two cannot disagree.
; clobbers: DX (the output), flags
cmz_cpre:
    mov dx, LD_HDR_SIZE
    test al, 1
    jz .noico
    add dx, 64
.noico:
    test al, 2
    jz .noasc
    add dx, 16
.noasc:
    test al, LD_HF_GLYPH
    jz .nogly
    add dx, 16
.nogly:
    mov [cs:cmz_x], dx
    ret

; -----------------------------------------------------------------------------
; cmz_hdr - a package's clear prefix, in front of the stream and re-flagged
; in:  AX = the packed body, [cs:cmz_x] = the prefix, [cs:cmz_b] = the source,
;      [cs:cmz_o] = the output region
; out: CF=0 with ES:BX = the whole file and AX = its length; CF=1 = refuse
; clobbers: AX BX CX DX SI DI ES, flags
;
; ONE REFUSAL AND A COPY, and the refusal is os88pkg.py's: THE IN-PLACE LAYOUT
; AGAINST THE REGION THE LOADER WAS GOING TO CLAIM ANYWAY. ld_run_body reads
; the file at R = roundup512(image - file) and expands downwards, so R + file
; has to fit roundup512(image + bss). When it does not, the answer is to leave
; this package alone rather than to make every launch on the machine claim a
; bigger region - a package that marginal was not going to save a sector.
; There is no margin to check any more: the stream's own tail is what makes
; image - file enough (SPEC.md 20.13.7).
; -----------------------------------------------------------------------------
cmz_hdr:
    add ax, [cs:cmz_x]          ; packed = the clear prefix + the stream
    cmp ax, [cs:cmz_u]
    jae .no                     ; not smaller than the image it replaces
    mov [cs:cmz_z], ax          ; ...and for a package the FILE is what the
    mov word [cs:cmz_z+2], 0    ; percentage is about, prefix included
    mov bx, [cs:cmz_u]
    sub bx, ax
    add bx, 511
    and bx, 0FE00h
    add bx, ax                  ; R + file
    jc .no
    mov es, [cs:cmz_b]
    mov cx, [es:LD_H_IMG]
    add cx, [es:LD_H_BSS]       ; cmz_pkg proved this is under
    add cx, 511                 ; APP_MAX_SIZE, so neither add can carry
    and cx, 0FE00h
    cmp bx, cx
    ja .no

    mov ax, [cs:cmz_x]
    mov cl, 4
    shr ax, cl                  ; the prefix in paragraphs, and it IS one
    mov bx, [cs:cmz_o]
    add bx, 8
    sub bx, ax                  ; ...so it ENDS where the stream begins
    mov es, bx
    mov cx, [cs:cmz_x]
    xor si, si
    xor di, di
    push ds                     ; DS is KERNEL_SEG everywhere else in this
    mov ds, [cs:cmz_b]          ; file (SPEC.md 2.6), so the source segment is
    cld                         ; borrowed for the length of the copy and put
    rep movsb                   ; straight back
    pop ds
    or byte [es:LD_H_FLAGS], 0x18   ; bit 3 = compressed, bit 4 = LZB
    xor bx, bx                  ; ES:BX = the whole file...
    mov ax, [cs:cmz_z]          ; ...and AX its length
    ret
.no:
    stc
    ret


; --- this image's own verdicts (SPEC.md 22.22.1) -----------------------------
; They were kernel `.text` until the verb moved out here, and `.text` is the
; segment that binds (KERN_CODE_MAX). Not one of them can be said by a machine
; that has not already loaded this file, so this is where they belong.
cmz_s_pkg:   db 'Cannot compress this one', 0
cmz_s_alr:   db 'Already compressed', 0
cmz_s_nomem: db 'Not enough memory', 0
cmz_s_worse: db 'It would not get smaller', 0
cmz_s_tail:  db "Its end won't compress", 0 ; a raw tail over 64KB (20.15.4)
cmz_s_busy:  db 'Compressing...', 0
cmz_s_tmpn:  db 'CMPRESS~.TMP', 0  ; a streamed Compress's new file (22.22.5)
CMZ_TMPL     equ $ - cmz_s_tmpn    ; ...13 with its NUL, fm_ebuf's size -
%if CMZ_TMPL != 13                 ; and cmz_jtmpn's cmz_cs13 moves exactly 13
    %error "cmz_s_tmpn must be 13 bytes with its NUL: cmz_cs13 copies 13 into fm_ebuf"
%endif
cmz_s_plain: db 'Not compressed', 0         ; ...and Uncompress's (SPEC.md 22.23.3)
cmz_s_bad:   db 'Cannot expand this one', 0
cmz_s_undone: db 'Uncompressed', 0
cmz_s_miss:  db 'Missing ', 0           ; ...and the join's (SPEC.md 22.23.5),
cmz_s_wrong: db 'Wrong part ', 0        ; which name the part
cmz_s_clus:  db 'Clusters too big', 0
%ifdef KERN_BIG
cmz_s_jbusy: db 'A disk job is running', 0  ; ...and Uncompress To's (22.23.6)
cmz_s_put:   db 'Put ', 0
cmz_s_in:    db ' in ', 0
cmz_s_colon  equ clo_s_colon   ; the cloner's own ':' - one image
cmz_s_jkeys: db 'Enter=ready  Esc=stop', 0
%endif

; --- the module's own scratch ------------------------------------------------
; A module image is loaded into a heap claim, so it is WRITABLE - which is what
; lets these live here and be reached `cs:`, with DS and ES both spent on the
; caller's buffers. A few dozen bytes; the 40KB that is NOT here is the
; caller's.
%ifdef CMZ_STREAM                ; (OUTSIDE the shared block below: a token
                                ; is a generation, not scratch, and the far
                                ; cell's segment word is not a zero)
cmz_otok:   dw 0                ; pass 2's WRITE_SEQ token (18.4.9): the
                                ; WRITE that makes each file leaves it stale
cmz_tfp:                        ; cmz_tio's far call: the shim...
cmz_tsh:    dw 0
            dw COLD_SEG         ; ...in the cold segment
%endif
cmz_utot:   dd 0                ; the bar's scale, both passes...
cmz_done:   dd 0                ; ...and how far along it
cmz_z:      dd 0                ; the verbs': what cmz_pack made of the file...
cmz_w:      dw 0                ; ...the window the claim could fund...
cmz_u:      dd 0                ; ...what a read of the file produces...
cmz_p:      dd 0                ; ...what it occupies on the disk today - all
                                ; three 32 bits (SPEC.md 22.22.4)...
cmz_b:      dw 0                ; ...the base of the claim...
cmz_o:      dw 0                ; ...the second region (a slice of that claim
                                ; for Compress, a second claim for Uncompress)
cmz_x:      dw 0                ; ...the clear prefix a package keeps (22.22.1)
cmz_k:      dw 0                ; ...and the source region's size in KB. Six
                                ; of these were kernel `.bss` while a verb was
                                ; resident; nothing outside this image reads
                                ; any of them
cmz_s_done: db 'Compressed to '  ; the success, which is a NUMBER and so is
                                ; composed: cmz_pct writes its digits into
                                ; cmz_buf, which follows, so this prefix is
                                ; never copied - and cmz_jmsg's lines start AT
                                ; cmz_buf and leave it alone
cmz_buf:    times 24 db 0       ; 'NN%' after the prefix, 'Wrong part FILEA.002'
                                ; - TOAST_MAX is 24 characters
%if cmz_buf - cmz_s_done != 14
    %error "cmz_buf must follow cmz_s_done's 14 bytes: cmz_pct completes the sentence in place"
%endif
; --- THE JOIN'S STATE, AND THE PACK'S SCRATCH UNDER IT ----------------------
; Two verbs that never run in one call: Compress uses the pack's block from
; cmz_pack's entry to its verdict and the join never touches it, the join
; (Uncompress of a split set, 22.23.5) owns its block from cmz_jclaim to its
; verdict and Compress never touches that - and every variable in either is
; written before it is read inside its own verb, the paused join's included
; (cmz_jload brings its block back out of the claim). The image is dropped
; after every verb besides. So the two blocks are ONE run of bytes: the
; join's is emitted and the pack's is laid over it (`absolute`), padded to
; whichever is longer. What both verbs read - the bar, cmz_b/o/u, cmz_buf,
; the WRITE_SEQ tokens, the far-call cell - is above and below, outside it.
cmz_ovl:
cmz_jst:                        ; --- the join (22.23.5/22.23.6): what a PAUSED
                                ; Uncompress To... keeps, in this order, in
                                ; its claim's header (cmz_jsave/cmz_jload)
cmz_jsrcd:  db 0                ; the parts' drive...
cmz_jtgtd:  db 0                ; ...and the result's
cmz_jsrcc:  dw 0                ; the parts' folder...
cmz_jtgtc:  dw 0                ; ...and the result's
cmz_jto:    db 0                ; 1 = Uncompress To..., 0 = beside the parts
cmz_jwrote: db 0                ; CMPRESS~.TMP exists
cmz_jk:     dw 0                ; the part being read, 1-based...
cmz_jn:     dw 0                ; ...of how many
cmz_jtot:   dd 0                ; the original's size...
cmz_jw:     dd 0                ; ...how much of it is written...
cmz_jid:    dd 0                ; ...and the set's ID. A part's header bytes
                                ; 6..20 in the same order - n, total, where it
                                ; starts, ID - so cmz_jloop checks all four
                                ; with one `repe cmpsw` (asserted below)
cmz_jsel:   times 13 db 0       ; the selection's name - a part's
cmz_jnam:   times 13 db 0       ; the result's
CMZ_JSTL    equ $ - cmz_jst
cmz_jretry: dw 0                ; the part Enter said is in: a miss of it is news
cmz_jext:   dw 0                ; where fm_onam's three digits are
cmz_jsp:    dw 0                ; the join's frame, for cmz_jerr and cmz_jsusp
cmz_jwin:   dw 0                ; the input window's segment, behind the header
cmz_jfo:    dd 0                ; the part's next read offset
cmz_jpos:   dw 0                ; the window: where the next record starts...
cmz_javl:   dw 0                ; ...how many bytes from there it holds...
cmz_jeof:   db 0                ; ...and whether the part has none left
%if cmz_jeof - cmz_jfo != 8
    %error "cmz_jfo, cmz_jpos, cmz_javl and cmz_jeof must be one run: cmz_jrst clears them with one rep stosb"
%endif
cmz_jm:     db 0                ; this block's M,
cmz_js:     dw 0                ; its S,
cmz_jnn:    dw 0                ; its N,
cmz_jovl_end:
absolute cmz_ovl                ; ...the pack's, over the join's
cmz_n:      resw 1                ; the source's end from DS, clamped to 0xFFFF
cmz_nchk:   resw 1                ; ...and clamped again to 32KB: end or slide
cmz_lim:    resw 1                ; the bail from ES, clamped to 32KB the same way
cmz_nabs:   resd 1                ; the whole source's length
cmz_rn:     resd 1                ; the source's end from DS, 32 bits (20.15.4)
cmz_rstop:  resd 1                ; where the parse stops, from DS
cmz_rlim:   resd 1                ; ...and the bail from ES
cmz_dl:     resd 1                ; (source base - output base), bytes
cmz_sbase:  resd 1                ; what the source has slid past, bytes...
cmz_obase:  resd 1                ; ...and the output
cmz_mx:     resd 1                ; the best (produced - consumed) so far...
cmz_ksa:    resd 1                ; ...and where it was seen: the source, as a
cmz_koa:    resd 1                ; position in the file, and the stream
%if cmz_rn-cmz_nabs != 4 || cmz_rstop-cmz_nabs != 8 || cmz_rlim-cmz_nabs != 12 || cmz_dl-cmz_nabs != 16 || cmz_koa-cmz_dl != 20
    %error "cmz_nabs..cmz_koa must be one block in cmz_pack's order: it stores them with stosw"
%endif
%if cmz_rstop - cmz_rn != 4 || cmz_sbase - cmz_dl != 4 || cmz_dl - cmz_rlim != 4
    %error "cmz_sslide/cmz_oslide step BX from one dword to the next: cmz_rn/cmz_rstop, cmz_rlim/cmz_dl and cmz_dl/cmz_sbase must be adjacent"
%endif
cmz_oseg:   resw 1                ; the output's FIRST segment: the T word's
cmz_mask:   resw 1                ; the window, less one (the caller's dial)
cmz_tseg:   resw 1                ; the caller's head[]/prev[] block
cmz_pos:    resw 1                ; cmz_probe's position, which its walk moves SI
cmz_most:   resw 1                ; ...the longest match it could return
cmz_lo:     resw 1                ; ...and the oldest position in the window
cmz_best:   resw 1                ; the best length found so far, and
cmz_boff:   resw 1                ; the offset that produced it
cmz_sseg:   resw 1                ; the source's FIRST segment
%ifdef CMZ_STREAM
cmz_mode:   resb 1                ; 0 whole, 1 and 2 the streamed passes
cmz_wrote:  resb 1                ; the streamed pass has created its file
cmz_sp:     resw 1                ; cmz_pack's frame, for cmz_ioerr
cmz_err:    resw 1                ; ...and the FERR_* it unwound with
cmz_sfill:  resd 1                ; the file offset of the next window refill
cmz_tl:     resw 1                ; the tail's length, T
cmz_clb:    resw 1                ; the volume's cluster, bytes (streamed)
%endif ; CMZ_STREAM: the streamed pass's own, read on no whole one
cmz_nextp:  resw 1                ; the SI the next progress report is owed at
cmz_tofs:   resw 1                ; where the tag byte being filled lives
cmz_tbit:   resb 1                ; ...and the bits still free in it
cmz_pend:
section .modl
%if cmz_pend > cmz_jovl_end
    times cmz_pend - cmz_jovl_end db 0
%endif
; THREE ADJACENCIES the join's code stores or compares as one (size pass 8):
%if cmz_jtgtd - cmz_jsrcd != 1 || cmz_jsrcc - cmz_jsrcd != 2 || cmz_jtgtc - cmz_jsrcd != 4 || cmz_jto - cmz_jsrcd != 6
    %error "cmz_jsrcd, cmz_jtgtd, cmz_jsrcc, cmz_jtgtc and cmz_jto must be one run: cmz_jwhere stores them with stosw/stosb"
%endif
%if cmz_jn - cmz_jk != 2 || cmz_jtot - cmz_jn != 2 || cmz_jw - cmz_jn != 6 || cmz_jid - cmz_jn != 10
    %error "cmz_jn, cmz_jtot, cmz_jw, cmz_jid must be a part header's bytes 6..20 in order: cmz_jloop checks them with one repe cmpsw"
%endif
%if cmz_done - cmz_utot != 4 || cmz_jw - cmz_jtot != 4
    %error "cmz_done must follow cmz_utot as cmz_jw follows cmz_jtot: cmz_jdisp restores the bar with one rep movsw"
%endif
%ifdef KERN_BIG
cmz_jcur:   times 16 db 0       ; the part's READ_SEQ cursor (18.4.8.1)...
cmz_scur    equ cmz_jcur        ; ...and the compressor's source's: the SAME 16
                                ; bytes, because no verb uses both - each resets
                                ; GEN before its first read (cmz_jrst, cmz_pack)
                                ; and the image is dropped after every verb
cmz_jtok:   dw 0                ; the result's WRITE_SEQ token (18.4.9).
                                ; Never reset: it is only a generation, and
                                ; a new temporary is a WRITE, which leaves
                                ; the last one stale
%endif

section .text
