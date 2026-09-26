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
%macro CMZ_TRACK 1
%ifndef NOCURDISK
    push ds
    push ax
    mov ax, KERNEL_SEG
    mov ds, ax
    mov byte [cur_inxfer], %1
    pop ax
    pop ds
%endif
%endmacro

section .modl                   ; the cloner's image (SPEC.md 2.8, 18.99) -
                                ; clone.inc has already laid the header, and
                                ; kernel.asm's modl_end comes after this

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
    mov [cs:cmz_sp], sp         ; where a failed streamed read or write
                                ; unwinds to, from however deep (cmz_ioerr)
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
    mov [cs:cmz_nabs], cx       ; the whole source, for the verdict at the end
    mov [cs:cmz_nabs+2], si
    mov [cs:cmz_rn], cx         ; ...and its END, from the source's current
    mov [cs:cmz_rn+2], si       ; segment - which is the same number until
    mov [cs:cmz_rstop], cx      ; the first slide - and where the parse
    mov [cs:cmz_rstop+2], si    ; STOPS, which is the same end
    sub cx, CMZ_SLACK
    sbb si, 0
    mov [cs:cmz_rlim], cx       ; the bail, from the output's current segment
    mov [cs:cmz_rlim+2], si
    xor ax, ax
    mov [cs:cmz_dl], ax         ; (source base - output base), bytes: 0 until
    mov [cs:cmz_dl+2], ax       ; something slides...
    mov [cs:cmz_sbase], ax      ; ...and the two bases themselves, which is
    mov [cs:cmz_sbase+2], ax    ; what turns an offset into a position in
    mov [cs:cmz_obase], ax      ; the file
    mov [cs:cmz_obase+2], ax
    mov [cs:cmz_mx], ax         ; the cut: nothing has beaten a lead of 0 yet,
    mov [cs:cmz_mx+2], ax       ; so the whole file would be tail
    mov [cs:cmz_ksa], ax
    mov [cs:cmz_ksa+2], ax
    mov word [cs:cmz_koa], 2
    mov [cs:cmz_koa+2], ax

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
    cmp byte [cs:cmz_mode], 0
    je .armed
    xor ax, ax                  ; STREAMED: the window's first CMZ_SBUF bytes
    mov [cs:cmz_sfill], ax      ; off the disk, from offset 0
    mov [cs:cmz_sfill+2], ax
    xor bx, bx
    mov cx, CMZ_SBUF
    call cmz_sfetch
.armed:
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
    push ax                     ; (os88lz.py's lzb_compress, in that order)
    push dx
    stc
    call cmz_bit
    call cmz_gamma              ; AX is still the length
    pop dx
    push dx
    mov ax, dx
    mov cl, 8
    shr ax, cl
    add ax, 2                   ; (off >> 8) + 2, which is >= 2 by
    call cmz_gamma              ; construction - a gamma cannot code 1
    pop dx
    pop ax
    push ax
    mov al, dl                  ; ...and the offset's low byte, raw
    stosb
    pop ax
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
    mov [cs:cmz_tl], ax
    mov cx, ax
    mov ax, [cs:cmz_koa]        ; the symbols' length, T word included...
    mov dx, [cs:cmz_koa+2]
    add ax, cx
    adc dx, 0                   ; ...and the whole stream's
    cmp byte [cs:cmz_mode], 0
    je .cmp
    add ax, DSK_CZ_HDR          ; STREAMED, the verb cannot look at the answer
    adc dx, 0                   ; before the file is written, so the container
.cmp:                           ; is counted here: a plain source's P is U
    cmp dx, [cs:cmz_nabs+2]
    ja .no
    jb .less
    cmp ax, [cs:cmz_nabs]
    jae .no
.less:
    push ax
    push dx
    cmp byte [cs:cmz_mode], 0
    jne .sfin
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
    jz .pop
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
    jmp .pop
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

; cmz_ioerr - a streamed read or write failed: AX = its FERR_*, and the stack
;             goes back to cmz_pack's frame from wherever this was reached
cmz_ioerr:
    ; STKBALANCE-OK: abandons the stack on purpose - SP goes back to the frame
    ; cmz_pack banked at entry, whatever cmz_sfetch/cmz_oflush had pushed
    mov [cs:cmz_err], ax
    mov sp, [cs:cmz_sp]
    mov ax, 2
    jmp short cmz_pack.fail

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
    sub [cs:cmz_rn], ax
    sbb word [cs:cmz_rn+2], 0
    sub [cs:cmz_rstop], ax
    sbb word [cs:cmz_rstop+2], 0
    add [cs:cmz_dl], ax
    adc word [cs:cmz_dl+2], 0
    add [cs:cmz_sbase], ax
    adc word [cs:cmz_sbase+2], 0
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
    cmp byte [cs:cmz_mode], 0
    jne .stream
    mov cl, 4
    shr dx, cl
    mov ax, ds
    add ax, dx
    mov ds, ax
    jmp cmz_sarm
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

; cmz_oslide - DI is past 32KB: move the output 16KB on (module internal).
;              WHOLE, the segment moves up; STREAMED, the window stays put,
;              the first 16KB goes to the new file (header's 8 first) and the
;              rest moves down. The open tag byte is at most a symbol behind
;              DI, so it survives either. clobbers: AX, flags
cmz_oslide:
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
    mov ax, es
    add ax, 0x0400
    mov es, ax
.base:
    sub di, 0x4000
    sub word [cs:cmz_tofs], 0x4000
    sub word [cs:cmz_rlim], 0x4000
    sbb word [cs:cmz_rlim+2], 0
    sub word [cs:cmz_dl], 0x4000
    sbb word [cs:cmz_dl+2], 0
    add word [cs:cmz_obase], 0x4000
    adc word [cs:cmz_obase+2], 0
    jmp cmz_oarm

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
    call COLD_SEG:dwf_dskw_read_at  ; DX:AX = what arrived
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
; cmz_oflush - write CX bytes from (ES-1):8 to fm_ebuf (module internal; pass
;              2's only write). The first is a WRITE, which creates the file
;              and lets dskw_czstamp see the 'CZ' header; every later one is
;              an APPEND, whose rule - the file a whole number of clusters -
;              the 16KB chunks keep until the last. clobbers: flags; a
;              failure never returns (cmz_ioerr)
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
    call COLD_SEG:dwf_dskw_append
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
    push ax
    mov al, [bx]
    cmp al, [si]
    pop ax                      ; ...which leaves the flags alone, so the Jcc
    jne .stop                   ; below still reads the compare
    inc bx
    inc si
    inc ax
    cmp ax, [cs:cmz_most]
    jb .walk
.stop:
    cmp ax, [cs:cmz_best]
    jbe .nextc
    mov [cs:cmz_best], ax
    mov ax, [cs:cmz_pos]
    sub ax, cx
    mov [cs:cmz_boff], ax
    mov ax, [cs:cmz_best]
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
    xor ax, ax
    xor dx, dx
.out:
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
    mov al, [si]                ; h = ((b0 << 8) ^ (b1 << 4) ^ b2) & (HSIZE-1)
    xor ah, ah
    mov cl, 8
    shl ax, cl
    mov bl, [si+1]
    xor bh, bh
    mov cl, 4
    shl bx, cl
    xor ax, bx
    mov bl, [si+2]
    xor bh, bh
    xor ax, bx
    and ax, CMZ_HSIZE - 1
    add ax, ax
    mov bx, ax
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
    mov ax, si
    and ax, [cs:cmz_mask]
    add ax, ax
    mov bx, ax
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
    push bx
    push cx
    jc .one
    call cmz_room
    dec byte [cs:cmz_tbit]
    jmp short .store
.one:
    call cmz_room
    dec byte [cs:cmz_tbit]
    mov cl, [cs:cmz_tbit]
    mov al, 1
    shl al, cl
    or [cs:cmz_tag], al
.store:
    mov al, [cs:cmz_tag]
    mov bx, [cs:cmz_tofs]
    mov [es:bx], al             ; ...written back every time, so a stream that
    pop cx                      ; ends mid-tag is still whole
    pop bx
    pop ax
    ret

; cmz_room - make sure a tag byte is open (module internal)
cmz_room:
    cmp byte [cs:cmz_tbit], 0
    jne .have
    mov [cs:cmz_tofs], di
    mov byte [cs:cmz_tag], 0
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
    test ax, 1
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
    or cl, cl
    jz .last
    stc
    call cmz_bit
    dec cl
    jmp short .emit
.last:
    clc
    call cmz_bit
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
    call cmz_compress
    jmp short .out
.uncomp:
    call cmz_uncomp
    jmp short .out
.none:
    mov byte [fm_cmdchg], FMD_NONE  ; a no-op draws nothing (SPEC.md 22.13)
    xor ax, ax                  ; ...and has nothing to say
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

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
    mov byte [cs:cmz_mode], 0
    call cmz_route              ; too big to hold twice? (SPEC.md 22.22.5)
    jnc .whole
    jmp cmz_stream              ; ...then streamed, and its answer is ours
.whole:
    call cmz_claim              ; DX = the base, [cs:cmz_k]/[cs:cmz_w] set
    jc .nomem
    mov [cs:cmz_b], dx

    ; --- read the file whole, expanding it if it arrives compressed --------
    ; The capacity is the source region alone: a 'CZ' file expands in place
    ; inside exactly U (SPEC.md 20.14.2), and the region is a whole KB over
    mov es, dx
    xor bx, bx
    mov ax, [cs:cmz_k]
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl                  ; KU KB, as a 32-bit byte count
    mov cx, ax
    mov si, fm_onam
    call COLD_SEG:dwf_dskw_read ; DX:AX = what arrived
    jc .rferr
    cmp dx, [cs:cmz_u+2]
    jne .rdisk
    cmp ax, [cs:cmz_u]
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
    call KERNEL_SEG:fpg_begin   ; widget's own 32-bit scale

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
    add ax, ax
    add ax, 64
    add ax, [cs:cmz_b]          ; base + (2*KU + 1)*64: head[] and prev[]
    mov bx, ax
    mov ax, [cs:cmz_x]
    mov cl, 4
    shr ax, cl                  ; the clear prefix in paragraphs - 0 for
    add ax, [cs:cmz_b]          ; everything but a package, so the source
    mov di, [cs:cmz_w]          ; segment is the claim's base
    dec di                      ; the window's MASK
    mov cx, [cs:cmz_u]
    mov si, [cs:cmz_u+2]
    sub cx, [cs:cmz_x]
    sbb si, 0                   ; SI:CX, the parse's 32-bit length
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
    mov dx, [cs:cmz_o]
    add dx, 7                   ; the last paragraph before the stream, so the
    mov es, dx                  ; eight bytes end exactly where it begins
    mov word [es:8], DSK_CZ_MAG
    mov byte [es:10], LZ_LZB
    mov byte [es:11], 0
    mov cx, [cs:cmz_u]
    mov [es:12], cx
    mov cx, [cs:cmz_u+2]
    mov [es:14], cx             ; U, 32 bits
    mov ax, [cs:cmz_z]          ; ...and the length back, DX having been
    mov dx, [cs:cmz_z+2]        ; spent on the header's segment
    mov bx, 8
    jmp short .writeit
.pkghdr:
    call cmz_hdr                ; ES:BX = the whole file, AX = its length -
    jc .rworse                  ; under 64KB, a package being one segment
    xor dx, dx
.writeit:
    mov cx, ax                  ; DX:CX, the write's 32-bit count
    mov si, fm_onam
    call COLD_SEG:dwf_dskw_write    ; ...and dskw_czstamp derives the directory
    jc .rferr                       ; hint from the bytes (SPEC.md 20.14.4) -
                                    ; or CLEARS it, which is what a package
                                    ; needs: a `.o88` begins 'O8' and carries
                                    ; its own
    call cmz_drop
    call cmz_pct                ; 'Compressed to 47%'
    clc
    ret

.rdisk:
    mov ax, FERR_IO             ; the volume moved under us between the
.rferr:                         ; directory read and the data read
    push ax                     ; the claim is still held
    call cmz_drop
    pop ax
.ferr:
    stc                         ; AX = FERR_*, and the caller says it: this
    ret                         ; image has no fm_errtab of its own and does
                                ; not want one
.rpack:
    or ax, ax
    jz .rworse                  ; AX = 0: it did not get smaller
    mov ax, cmz_s_tail          ; AX = 1: a raw tail over 64KB
    jmp short .rsay
.rworse:
    call cmz_drop
.worse:
    mov ax, cmz_s_worse
    jmp short .say
.rsay:                          ; cmz_pkg's refusal: the claim is held
    push ax
    call cmz_drop
    pop ax
    jmp short .say
.nomem:
    mov ax, cmz_s_nomem
    jmp short .say
.big:
    mov ax, cmz_s_big
    jmp short .say
.already:
    mov ax, cmz_s_alr
.say:
    call cmz_say
    xor ax, ax                  ; ...said here, so the caller says nothing
    stc
    ret

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
    mov ax, [cs:cmz_u]
    mov dx, [cs:cmz_u+2]
    call cmz_kb
    add ax, ax
    add ax, 1 + CMZ_PREV/1024 + CMZ_WMAX/512    ; cmz_claim's full-window block
    push ax
    call COLD_SEG:mmf_osapi_mem_avail   ; AX = the largest claim, KB
    pop dx
    cmp ax, dx
    jae .whole                  ; it fits whole: one pass
    cmp ax, CMZ_STRK + CMZ_WMIN/512
    jb .whole                   ; ...and neither way fits: whole's dial, and
                                ; its own `Not enough memory`
    push ax
    mov ax, [cs:cmz_u]          ; ...nor when the RESULT could not be read
    mov dx, [cs:cmz_u+2]        ; back: every reader of a 'CZ' file, and
    call cmz_kb                 ; Uncompress, claims U whole (SPEC.md
    pop dx                      ; 22.23.4), so a stream past the heap is a
    cmp ax, dx                  ; file nothing on this machine can open.
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
    mov word [cs:cmz_w], CMZ_WMAX
.try:
    mov ax, [cs:cmz_w]
    mov cl, 9
    shr ax, cl
    add ax, CMZ_STRK            ; the windows, head[] and prev[]
    mov bx, MEM_K_CMPR
    call COLD_SEG:mmf_mem_claim
    jnc .got
    shr word [cs:cmz_w], 1
    cmp word [cs:cmz_w], CMZ_WMIN
    jae .try
    mov ax, cmz_s_nomem
    jmp .say
.got:
    mov [cs:cmz_b], dx
    push ds                     ; the new file's name, where the file layer
    pop es                      ; can read it (DS:SI) - fm_ebuf, the status
    mov si, cmz_s_tmpn          ; line's edit buffer, idle while a menu verb
    mov di, fm_ebuf             ; runs
    mov cx, CMZ_TMPL
.cp:
    mov al, [cs:si]
    inc si
    stosb
    loop .cp
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
    mov word [es:8], DSK_CZ_MAG ; T goes in at the end (cmz_pack's .sfin)
    mov word [es:10], LZ_LZB    ; the format, and the reserved zero
    mov ax, [cs:cmz_u]
    mov [es:12], ax
    mov ax, [cs:cmz_u+2]
    mov [es:14], ax

    mov byte [cs:cmz_mode], 1   ; --- ONE pass, written as it goes --------
    call .pass                  ; DX:AX = the file, header included
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

.pass:                          ; the windows off the one claim, and the pack
    mov ax, [cs:cmz_b]          ; source window, CMZ_SBUF
    mov dx, ax
    add dx, CMZ_SBUF / 16 + 1   ; output window, one paragraph in
    mov bx, ax
    add bx, (CMZ_SBUF / 1024 + CMZ_OBUFK) * 64  ; head[], then prev[]
    mov di, [cs:cmz_w]
    dec di
    mov cx, [cs:cmz_u]
    mov si, [cs:cmz_u+2]
    jmp cmz_pack                ; ...whose `ret` is .pass's

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
    push ax
    call cmz_drop
    pop ax
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
    push ax
    call cmz_drop
    pop ax
.say:
    call cmz_say
    xor ax, ax
    stc
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
    mov word [cs:cmz_b], 0      ; neither claim is held yet, which is what
    mov word [cs:cmz_o], 0      ; lets cmz_ufree be the ONE exit path
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
    cmp dx, [cs:cmz_u+2]
    jne .rbad
    cmp ax, [cs:cmz_u]
    jne .rbad                   ; the hint said one thing and the read another
    xor bx, bx
    jmp .writeit

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
    mov dx, [es:LD_H_IMG]
    cmp dx, [cs:cmz_p]
    jbe .rbad                   ; an image no bigger than the file it is
                                ; packed into is a header that has drifted
    mov [cs:cmz_u], dx
    mov dx, LD_HDR_SIZE         ; ld_cpre's own ladder (SPEC.md 20.13.1) - the
    test al, 1                  ; header, the icon, the association block. At
    jz .noico                   ; most 112 and always a multiple of 16, which
    add dx, 64                  ; is what lets the body's destination be a
.noico:                         ; SEGMENT below
    test al, 2
    jz .noasc
    add dx, 16
.noasc:
    mov [cs:cmz_x], dx
    cmp dx, [cs:cmz_u]
    jae .rbad                   ; all prefix: there is no body to expand

    mov ax, [cs:cmz_u]          ; ...and a SECOND claim for the expansion,
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

.rferr:
    push ax
    call cmz_ufree
    pop ax
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
    push ax
    call cmz_ufree
    pop ax
    jmp short .say
.plain:
    mov ax, cmz_s_plain
    jmp short .say
.nomem:
    mov ax, cmz_s_nomem
.say:
    call cmz_say
    xor ax, ax
    stc
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
    mov bx, MEM_K_CMPR
    call COLD_SEG:mmf_mem_claim
    pop bx
    pop ax
    ret

; cmz_kb - DX:AX bytes -> AX = KB, rounded UP (always one over at an exact
;          multiple, which both claims are glad of). DX:AX under 16MB, so the
;          answer is under 16,385 and cannot wrap. clobbers: CL, DX, flags
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
; out: CF=0 with DX:AX = the bytes delivered and ES = the claim; CF=1 with
;      AX = FERR_*
; clobbers: AX (the output), BX, CX, DX, ES, flags
; -----------------------------------------------------------------------------
cmz_uread:
    push si
    mov ax, [cs:cmz_k]
    mov dx, ax
    mov cl, 10
    shl ax, cl                  ; the claim as a 32-BIT byte count: 66 KB is
    mov cl, 6                   ; over 64K and the capacity test is 32-bit
    shr dx, cl
    mov cx, ax
    mov es, [cs:cmz_b]
    xor bx, bx                  ; a compressed file wants seg:0 and every
                                ; caller in the tree hands over a claim's base
    mov si, fm_onam
    call COLD_SEG:dwf_dskw_read ; DX:AX = what arrived
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
    xor cx, cx                  ; TOAST_TICKS, the default life
    call KERNEL_SEG:CMZ_XTOAST
    pop es
    pop si
    pop cx
    pop ax
    ret

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
    push si
    push di
    mov si, cmz_s_done
    mov di, cmz_buf
    mov cx, CMZ_SDONE
.cp:
    mov al, [cs:si]             ; cs: BOTH WAYS: DS is the kernel's here
    inc si
    mov [cs:di], al
    inc di
    loop .cp
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
    xor dx, dx
    mov cx, 10
    div cx                      ; AL = tens, DL = units
    or al, al
    jz .ones
    add al, '0'
    mov [cs:di], al
    inc di
.ones:
    add dl, '0'
    mov [cs:di], dl
    mov byte [cs:di+1], '%'
    mov byte [cs:di+2], 0
    mov ax, cmz_buf
    call cmz_say
    pop di
    pop si
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
    mov ax, [cs:cmz_u]          ; the source's KB, ROUNDED UP - and NOT through
    mov dx, [cs:cmz_u+2]        ; mem_bytes_kb_x, whose own banner says a new
    call cmz_kb                 ; caller must prove its input cannot wrap: this
                                ; one cannot, `add ax, 1023` carries for every
    mov [cs:cmz_k], ax          ; U from 64,513 up and answers ZERO KB. A shift
                                ; and an increment is a KB more than needed
                                ; when U is an exact multiple of 1,024, which
                                ; the output region is glad of anyway
    mov word [cs:cmz_w], CMZ_WMAX
.try:
    mov ax, [cs:cmz_w]
    mov cl, 9
    shr ax, cl                  ; prev[] is 2 bytes a position: window/512 KB
    add ax, CMZ_PREV / 1024     ; ...plus head[]'s 8
    add ax, [cs:cmz_k]
    add ax, [cs:cmz_k]
    inc ax
    mov bx, MEM_K_CMPR
    call COLD_SEG:mmf_mem_claim
    jnc .out
    shr word [cs:cmz_w], 1
    cmp word [cs:cmz_w], CMZ_WMIN
    jae .try
    stc
.out:
    pop cx
    pop bx
    pop ax
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
    mov dx, LD_HDR_SIZE         ; the CLEAR prefix, ld_cpre's own ladder: what
    test al, 1                  ; the mount reads out of the first sector and
    jz .noico                   ; must never have to decompress to get
    add dx, 64                  ; (SPEC.md 20.13.1). At most 128, and always a
.noico:                         ; multiple of 16 - which is what lets the
    test al, 2                  ; source be a SEGMENT below and the header
    jz .noasc                   ; land on a paragraph in front of the stream
    add dx, 16
.noasc:
    test al, LD_HF_GLYPH        ; ...and the document glyph (SPEC.md 54.3.2)
    jz .nogly
    add dx, 16
.nogly:
    mov [cs:cmz_x], dx
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
    mov ax, cmz_s_big
    jmp short .no
.already:
    mov ax, cmz_s_alr
.no:
    pop dx
    pop bx
    stc
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
    mov cx, [cs:cmz_x]
    mov ax, [cs:cmz_b]
    mov es, bx
    xor si, si
    xor di, di
    push ds                     ; DS is KERNEL_SEG everywhere else in this
    mov ds, ax                  ; file (SPEC.md 2.6), so the source segment is
    cld                         ; borrowed for the length of the copy and put
    rep movsb                   ; straight back
    pop ds
    or byte [es:LD_H_FLAGS], 0x18   ; bit 3 = compressed, bit 4 = LZB
    xor bx, bx                  ; ES:BX = the whole file...
    mov ax, [cs:cmz_z]          ; ...and AX its length
    clc
    ret
.no:
    stc
    ret


; --- this image's own verdicts (SPEC.md 22.22.1) -----------------------------
; They were kernel `.text` until the verb moved out here, and `.text` is the
; segment that binds (KERN_CODE_MAX). Not one of them can be said by a machine
; that has not already loaded this file, so this is where they belong.
cmz_s_pkg:   db 'Cannot compress this one', 0
cmz_s_big:   db 'Too large', 0
cmz_s_alr:   db 'Already compressed', 0
cmz_s_nomem: db 'Not enough memory', 0
cmz_s_worse: db 'It would not get smaller', 0
cmz_s_tail:  db "Its end won't compress", 0 ; a raw tail over 64KB (20.15.4)
cmz_s_busy:  db 'Compressing...', 0
cmz_s_tmpn:  db 'CMPRESS~.TMP', 0  ; a streamed Compress's new file (22.22.5)
CMZ_TMPL     equ $ - cmz_s_tmpn    ; ...13 with its NUL, fm_ebuf's size
cmz_s_plain: db 'Not compressed', 0         ; ...and Uncompress's (SPEC.md 22.23.3)
cmz_s_bad:   db 'Cannot expand this one', 0
cmz_s_undone: db 'Uncompressed', 0
cmz_s_done:  db 'Compressed to '   ; ...and the success, which is a NUMBER and
CMZ_SDONE    equ $ - cmz_s_done    ; so is composed rather than picked

; --- the module's own scratch ------------------------------------------------
; A module image is loaded into a heap claim, so it is WRITABLE - which is what
; lets these live here and be reached `cs:`, with DS and ES both spent on the
; caller's buffers. A few dozen bytes; the 40KB that is NOT here is the
; caller's.
cmz_n:      dw 0                ; the source's end from DS, clamped to 0xFFFF
cmz_nchk:   dw 0                ; ...and clamped again to 32KB: end or slide
cmz_lim:    dw 0                ; the bail from ES, clamped to 32KB the same way
cmz_rn:     dd 0                ; the source's end from DS, 32 bits (20.15.4)
cmz_rlim:   dd 0                ; ...and the bail from ES
cmz_nabs:   dd 0                ; the whole source's length
cmz_dl:     dd 0                ; (source base - output base), bytes
cmz_oseg:   dw 0                ; the output's FIRST segment: the T word's
cmz_mask:   dw 0                ; the window, less one (the caller's dial)
cmz_tseg:   dw 0                ; the caller's head[]/prev[] block
cmz_pos:    dw 0                ; cmz_probe's position, which its walk moves SI
cmz_most:   dw 0                ; ...the longest match it could return
cmz_lo:     dw 0                ; ...and the oldest position in the window
cmz_best:   dw 0                ; the best length found so far, and
cmz_boff:   dw 0                ; the offset that produced it
cmz_mx:     dd 0                ; the best (produced - consumed) so far...
cmz_ksa:    dd 0                ; ...and where it was seen: the source, as a
cmz_koa:    dd 0                ; position in the file, and the stream
cmz_sbase:  dd 0                ; what the source has slid past, bytes...
cmz_obase:  dd 0                ; ...and the output
cmz_rstop:  dd 0                ; where the parse stops, from DS
cmz_sseg:   dw 0                ; the source's FIRST segment
cmz_mode:   db 0                ; 0 whole, 1 and 2 the streamed passes
cmz_wrote:  db 0                ; the streamed pass has created its file
cmz_sp:     dw 0                ; cmz_pack's frame, for cmz_ioerr
cmz_err:    dw 0                ; ...and the FERR_* it unwound with
cmz_sfill:  dd 0                ; the file offset of the next window refill
cmz_tl:     dw 0                ; the tail's length, T
cmz_clb:    dw 0                ; the volume's cluster, bytes (streamed)
cmz_tfp:                        ; cmz_tio's far call: the shim...
cmz_tsh:    dw 0
            dw COLD_SEG         ; ...in the cold segment
cmz_utot:   dd 0                ; the bar's scale, both passes...
cmz_done:   dd 0                ; ...and how far along it
cmz_nextp:  dw 0                ; the SI the next progress report is owed at
cmz_tofs:   dw 0                ; where the tag byte being filled lives
cmz_tag:    db 0                ; ...its value so far
cmz_tbit:   db 0                ; ...and the bits still free in it
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
cmz_buf:    times 20 db 0       ; 'Compressed to NN%' - TOAST_MAX is 24

section .text
