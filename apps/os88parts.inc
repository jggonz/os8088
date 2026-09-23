; =============================================================================
; apps/os88parts.inc - THE PARTS STANDARD (SPEC.md 20.12)
;
; A package that needs more than its own 64KB segment, or that wants to carry
; its data rather than trail a sidecar file, embeds this. It is ordinary
; package code, assembled into you, and it owes the kernel nothing: every call
; it makes is a slot that was already published.
;
; THE KERNEL LEARNS THREE THINGS AND NOTHING ELSE (SPEC.md 20.12.1): that a
; *.O88 over 64KB is still a package, that flags bit 2 means the file is
; longer than the image on purpose, and it tells your ENTRY PROC the name of
; the file it came out of. There is no .o88 v6, no kernel-parsed block and no
; API slot. docs/plans/completed/O88-MULTISEG-PLAN.md 1 is why - the kernel-side version of
; this was built to five waves and thrown away at 2,560 resident bytes.
;
; --- what you write ----------------------------------------------------------
;
;     OS88_HEADER 'MYAPP', my_entry, OS88_F_PARTS
;     %include "os88parts.inc"
;
;     OS88_PARTS_BEGIN 4
;       OS88_PART OP_SEG                 ; 0  code you far-call
;       OS88_PART OP_ASSET               ; 1  data you read
;       OS88_PART OP_ASSET, OP_ZERO, 8   ; 2  8KB of scratch, NO DISK AT ALL
;       OS88_PART OP_ASSET, OP_ZERO | OP_OPT, 600
;     OS88_PARTS_END                     ; 3  ...and 600KB it can do without
;
;     my_entry:
;         call op_load                   ; SI is the kernel's; call this FIRST
;         jc .refused                    ; a toast has already said why
;         mov al, 1
;         call op_seg                    ; AX = part 1's segment
;
;     ...and the bss, which is an equ chain in this SDK: OUR words come first,
;     yours follow them, and OS88_BSS is told the sum.
;
;         my_thing  equ os88_image_end + OP_BSS
;         MY_BSS    equ 40
;         OS88_BSS OP_BSS + MY_BSS
;
; --- the three rules ---------------------------------------------------------
;
; 1. CALL op_load FROM THE ENTRY PROC, FIRST. SI arrives holding an offset
;    into the KERNEL's segment, at a 13-byte name buffer the loader reuses on
;    the next launch (SPEC.md 20.2). op_load copies it before anything else,
;    and nothing later can recover it.
;
;    ...AND BANK ES ACROSS IT IF YOU GO ON TO CALL OSAPI_ARG_FILE. ES arrives
;    holding KERNEL_SEG and that slot answers with an SI into THAT segment
;    WITHOUT reloading it - it is documented as read through the ES a package
;    proc was entered with (SPEC.md 74240). Every OSAPI slot preserves ES, so
;    an entry proc may rely on it across wm_create and about_set; op_load is
;    package code and its clobber list says ES. So `push es` / `call op_load`
;    / `pop es`, or load KERNEL_SEG yourself the way apps/cc/os88thunk.asm
;    does. It is worth stating because the failure is silent and lands four
;    routines away: the name copy reads the document's 13 bytes out of a
;    PART's segment, and what surfaces is the file refusing to be read.
;
; 2. SEGMENT 0 OWNS THE IDENTITY (SPEC.md 20.12.5, binding). Your entry proc,
;    every window, every callback and your worker task live in the PRIMARY
;    segment. A part may not call the kernel's ES-fenced slots -
;    OSAPI_MEM_CLAIM and its relatives, OSAPI_MEM_FREE, OSAPI_TASK_SPAWN,
;    OSAPI_FSX_ENTER - because ES is stamped from the caller's DS and the
;    kernel reads it as WHO IS ASKING. The primary claims and passes a
;    segment down.
;
; 3. UI TASK ONLY. op_load reads a disk and claims memory, and SPEC.md 20.6
;    rule 7 forbids a worker both. The entry proc and every window callback
;    qualify; your worker does not.
;
; --- what it costs -----------------------------------------------------------
;
; ONE claim for every part that shares the carve, because MEM_OWNER_MAX is 8
; claims per owner word (SPEC.md 50) and a package's own claims carry its
; segment. And one OSAPI_FILE_READ_AT per 32KB of it, not one per part - each
; int 13h is ~400 ms on the target machine (PERFORMANCE.md), so what is
; counted is calls.
;
; Everything the kernel-side loader would have done it does here instead, and
; it does it AFTER the kernel has already read your image - so the size
; question is answered with NO DISK READ AT ALL and a refusal is instant.
; =============================================================================

; OS88_F_PARTS lives in os88api.inc, beside the other header flags, because
; OS88_HEADER needs it and OS88_HEADER comes before this include.

; WHERE THE STANDARD'S BSS LIVES - see the chain at the foot of this file.
; An assembly package leaves this alone; apps/cc/crt0.asm defines it before
; including, because a C package's bss is a real section rather than the bytes
; past its image.
%ifndef OP_BSS_AT
    %define OP_BSS_AT os88_image_end
%endif

OP_SEG        equ 0             ; kind: code and/or data you far-call
OP_ASSET      equ 1             ; kind: data you reach through a segment

OP_XMS        equ 1             ; flags (SPEC.md 20.12.4) - later waves
OP_ZERO       equ 2
OP_OPT        equ 4
OP_LAZY       equ 8
OP_COMP       equ 16            ; ...and the part is COMPRESSED on the disk
                                ; (SPEC.md 20.12.7). `len` below stays the
                                ; UNPACKED byte count - it is what sizes the
                                ; claim, so a refusal still costs no disk -
                                ; and `zkb`, which the macro already forbids
                                ; on a file-backed row, carries the packed one
OP_MARGIN     equ 0             ; what a compressed run needs ABOVE its
                                ; unpacked bytes: NOTHING, since every stream
                                ; ends in a raw tail (SPEC.md 20.13.7) - the
                                ; name is kept because the arithmetic below
                                ; is written in terms of it and reads better
                                ; with the term in than with it silently gone

OP_ROW        equ 8             ; db kind, db flags, dw off (512-byte units),
OP_R_KIND     equ 0             ; dw len, dw zkb - off and len are os88pkg.py's
OP_R_FLAG     equ 1             ; and zkb is yours
OP_R_OFF      equ 2
OP_R_LEN      equ 4
OP_R_ZKB      equ 6             ; FOUR MEANINGS, one per flag combination, and
                                ; the flags are what say which: KB of
                                ; zero-filled scratch on an OP_ZERO row, the
                                ; PACKED length on an eager OP_COMP one, the
                                ; SEGMENT a fetched OP_LAZY part sits at - and
                                ; on an OP_COMP|OP_LAZY row the packed length
                                ; BEFORE the fetch and the segment after
                                ; (SPEC.md 20.12.7.4)
OP_FETCHED    equ 32            ; **A RUNTIME FLAG, never in the file** - set by
                                ; op_fetch and cleared by op_drop, and it is
                                ; what "is this part here?" asks. It used to be
                                ; `zkb != 0`, which was sound while zkb could
                                ; only ever be a banked segment; on an
                                ; OP_COMP|OP_LAZY row (SPEC.md 20.12.7.4) zkb
                                ; STARTS as the packed length, so a part that
                                ; had never been fetched read as already here
                                ; and op_fetch returned CF=0 having done
                                ; nothing. os88pkg.py refuses the bit in a
                                ; file, so a row always begins clear
OP_SPENT      equ 0xFFFF        ; ...and what op_drop leaves in a dropped
                                ; OP_COMP|OP_LAZY row, because the packed
                                ; length it would need to be read again is the
                                ; word the segment was banked in. 0 would mean
                                ; `not fetched` and a second op_fetch would
                                ; read a stream of ZERO bytes and answer CF=0
                                ; over a claim of rubble; this makes it refuse
                                ; and say so. It is safe as a sentinel because
                                ; it is not a segment any claim can have -
                                ; 0xFFFF:0000 is 16 bytes below the 1MB wrap
                                ; - or the PACKED byte count on an OP_COMP row,
                                ; or a fetched OP_LAZY row's segment. One word,
                                ; three meanings, and no two of them can be on
                                ; the same row: the macro below refuses every
                                ; pairing (SPEC.md 20.12.7)
OP_T_ROWS     equ 10            ; the rows start past the magic and the count
OP_T_FMT      equ 9             ; ...and the byte before them is the FORMAT the
                                ; OP_COMP rows use: 0 = LZ4, 1 = LZB, and
                                ; meaningless with no such row. It is the
                                ; table's own reserved byte, which os88pkg.py
                                ; validated as zero and now writes - the
                                ; package's own scope, so it needs nothing of
                                ; the header's and nothing of the kernel's

OP_CHUNK      equ 32768         ; bytes a read moves at a time: a multiple of
                                ; EVERY legal cluster size (512 bytes to 32KB,
                                ; a power of two capped at 64 sectors by BPB
                                ; rule 4) and safely inside one segment's 64KB

; -----------------------------------------------------------------------------
; OS88_PARTS_BEGIN / OS88_PART kind [, flags] / OS88_PARTS_END
;
; The table is YOUR DATA, in your image, at a label you place. os88pkg.py finds
; it by the magic below rather than at a derived offset, so it does not care
; where you put it and there is no interaction with the icon or the
; association block. `off` and `len` are laid out as zeros and filled in by the
; packer: the assembler cannot know a separately assembled module's length.
;
; IT IS IN THE IMAGE AND THAT IS THE DESIGN (SPEC.md 20.12.3). The kernel has
; already read it by the time your entry proc runs, so sizing costs no disk;
; and it is not hostile input, because it did not come off a disk on its own.
; -----------------------------------------------------------------------------
; --- WHAT THIS TABLE ASKS FOR, derived from the rows (SPEC.md 20.12.9) -------
; Five features, five flags, and a package states none of them: OS88_PART sets
; the one its own row implies, so the code that follows the table takes only
; what the table asked for. A SECOND place to declare it would be a second
; place to get it wrong - the failure this project has met often enough to
; have a gate for (tests/unit/t_mirror.py) - and there is no wording under
; which "I use compression" is truer than the OP_COMP already on the row.
%assign OP_HAS_XMS  0
%assign OP_HAS_ZERO 0
%assign OP_HAS_OPT  0
%assign OP_HAS_LAZY 0
%assign OP_HAS_COMP 0

%macro OS88_PARTS_BEGIN 1
    %assign OS88_PART_N 0
    %assign OS88_PART_WANT %1
    %if (%1) < 1
        %error "OS88_PARTS_BEGIN with no parts in it - drop the block and the flag"
    %endif
op_table:
    db 'O88PARTS'
op_t_n:
    db %1                       ; the count, and THREE things check it: this
                                ; macro's brackets count the rows, os88pkg.py
                                ; counts the --part arguments against the
                                ; filed ones, and op_size walks exactly this
                                ; many
    db 0                        ; reserved
%endmacro

%macro OS88_PART 1-3 0, 0
    %if ((%2) & OP_XMS) && ((%1) != OP_ASSET)
        %error "OP_XMS on a SEGMENT: code has to be addressable and extended memory is not"
    %endif
    %if ((%2) & OP_ZERO) && ((%3) < 1)
        %error "OP_ZERO with no KB: a scratch part of nothing is not a part"
    %endif
    %if !((%2) & OP_ZERO) && ((%3) != 0)
        %error "a KB on a FILE-BACKED part: the carve is contiguous, so a part that wants scratch beside it declares a second, OP_ZERO part"
    %endif
    %if ((%2) & OP_OPT) && !((%2) & OP_ZERO)
        %error "OP_OPT on a file-backed part: it sits inside the carved run, and dropping it would split the one read the carve exists to be"
    %endif
    %if ((%2) & OP_LAZY) && ((%2) & OP_ZERO)
        %error "OP_LAZY on a scratch part: a scratch part earns its declaration by being SIZED before a sector is read, and a lazy part is outside that sizing - so it is OSAPI_MEM_CLAIM with extra steps, and its zkb word is where a fetched part banks its segment"
    %endif
    %if ((%2) & OP_LAZY) && ((%2) & OP_OPT)
        %error "OP_LAZY with OP_OPT: lazy already means you may not get it - op_fetch refuses and op_lazyok answers in advance"
    %endif
    %if ((%2) & OP_LAZY) && ((%2) & OP_XMS)
        %error "OP_LAZY with OP_XMS: the parts above 1MB are ONE block claimed and filled at load, so a lazy member would force that block to be claimed anyway"
    %endif
    %if ((%2) & OP_COMP) && ((%2) & OP_ZERO)
        %error "OP_COMP on a scratch part: there are no file bytes to compress, and zkb is the KB it asks for"
    %endif
    ; OP_COMP with OP_LAZY IS THE COMMON CASE AND WAS REFUSED (SPEC.md
    ; 20.12.7.4). The refusal that stood here was right about the mechanism -
    ; one zkb word, two meanings - and wrong about which way to resolve it:
    ; EVERY lazy row in this tree is a compressed stream, and each one had
    ; grown a packer and an expander of its own outside the standard. The
    ; word is shared in TIME instead: zkb is the PACKED LENGTH until the part
    ; is fetched and the SEGMENT afterwards, which is the order op_fetch
    ; reads them in.
    %if ((%2) & OP_COMP) && ((%2) & OP_XMS)
        %error "OP_COMP with OP_XMS: the span above 1MB is staged through a transient conventional buffer a chunk at a time, and a stream cannot be expanded a chunk at a time - the parts up there are stored plain"
    %endif
    %if ((%2) & OP_XMS)
        %assign OP_HAS_XMS 1
    %endif
    %if ((%2) & OP_ZERO)
        %assign OP_HAS_ZERO 1
    %endif
    %if ((%2) & OP_OPT)
        %assign OP_HAS_OPT 1
    %endif
    %if ((%2) & OP_LAZY)
        %assign OP_HAS_LAZY 1
    %endif
    %if ((%2) & OP_COMP)
        %assign OP_HAS_COMP 1
    %endif
    db %1                       ; +0 kind
    db %2                       ; +1 flags
    dw 0                        ; +2 off, in 512-byte units - the packer's
    dw 0                        ; +4 len, in bytes           - the packer's
    dw %3                       ; +6 zkb, in KB              - yours
    %assign OS88_PART_N OS88_PART_N + 1
%endmacro

; The table's own closing checks, WITHOUT the code - which the C SDK needs
; separately, because its table is in `.data` and its code must be in `.text`.
; An assembly package uses OS88_PARTS_END and never sees this.
%macro OS88_PARTS_END_TABLE 0
    %if OS88_PART_N < 1
        %error "OS88_PARTS_BEGIN with no parts between the brackets - drop the block and the flag"
    %endif
    %if OS88_PART_N != OS88_PART_WANT
        %error "OS88_PARTS_BEGIN was told a count that is not the number of parts between the brackets"
    %endif
    %if ($ - op_table) != OP_T_ROWS + OP_ROW * OS88_PART_N
        %error "the part table is 10 bytes plus 8 per part - write nothing else between the brackets"
    %endif
%endmacro

%macro OS88_PARTS_END 0
    OS88_PARTS_END_TABLE
    OS88_PARTS_CODE
%endmacro

; -----------------------------------------------------------------------------
; OS88_PARTS_CODE - the standard's code, gated on what YOUR TABLE asked for
;
; OS88_PARTS_END calls this for you, so an assembly package writes nothing it
; did not write before. It is a macro of its own because the C SDK has to put
; the table in `.data` and the code in `.text` (apps/cc/crt0.asm), so the two
; halves of `END` have to be separable there.
;
; IT MUST COME AFTER THE TABLE, and that is the point of the arrangement: the
; OP_HAS_* flags are derived from the rows, so code emitted at the `%include`
; - which is where it used to be - cannot be gated on them.
; -----------------------------------------------------------------------------
%macro OS88_PARTS_CODE 0
%include "os88partsbody.inc"
%endmacro

; --- our bss, and YOURS FOLLOWS IT ------------------------------------------
; This SDK's bss is an equ chain off os88_image_end (SPEC.md 20.5), so the
; standard takes the first OP_BSS bytes of it and a package bases its own at
; os88_image_end + OP_BSS. OS88_BSS is told the sum.
;
; OP_BSS_AT IS WHERE THAT CHAIN STARTS, and it is a %define so that a C
; PACKAGE can move it (SPEC.md 73.2). An assembly package's bss really is the
; bytes past its image, so os88_image_end is the base and this is invisible.
; A C package has no such label: SmallerC emits four sections and the loader
; zeroes a real `.bss` between cc_bss_beg and cc_bss_end, so apps/cc/crt0.asm
; reserves OP_BSS bytes IN THAT SECTION and points this at them. One symbol
; moves and the whole chain follows, which is the only reason the standard
; needed no second version for C.
op_name     equ OP_BSS_AT + 0       ; the file we came out of, NUL-term
op_first    equ OP_BSS_AT + 13      ; the carved run's first sector
op_secs     equ OP_BSS_AT + 15      ; ...its length in sectors, 0 = none
op_kb       equ OP_BSS_AT + 17      ; ...in KB
op_bytes    equ OP_BSS_AT + 19      ; ...and in bytes
op_clb      equ OP_BSS_AT + 21      ; this volume's bytes per cluster
op_off      equ OP_BSS_AT + 23      ; the cluster-aligned read offset (32)
op_slack    equ OP_BSS_AT + 27      ; ...how far the run is past it
op_cap      equ OP_BSS_AT + 29      ; the capacity that read asks for
op_base     equ OP_BSS_AT + 31      ; the claim it lands in
op_reqkb    equ OP_BSS_AT + 33      ; the run plus the REQUIRED scratch...
op_allkb    equ OP_BSS_AT + 35      ; ...and plus the optional too
op_runkb    equ OP_BSS_AT + 37      ; KB from the base to the scratch area
op_optok    equ OP_BSS_AT + 39      ; byte: were the optional parts
                                         ; granted? A package that greys a row
                                         ; reads this rather than testing
                                         ; op_seg against zero, because zero
                                         ; is also what a table it mis-declared
                                         ; would answer (SPEC.md 47)
op_xmson    equ OP_BSS_AT + 40      ; byte: is there a store to put the
                                         ; OP_XMS parts in? Asked ONCE, before
                                         ; the sizing, because the answer moves
                                         ; those parts between two totals
op_xoff     equ OP_BSS_AT + 41      ; the XMS span's first sector...
op_xsecs    equ OP_BSS_AT + 43      ; ...its length in sectors...
op_xmskb    equ OP_BSS_AT + 45      ; ...and the KB it wants up there
op_xlin     equ OP_BSS_AT + 47      ; the one block it all landed in (32)
op_xcur     equ OP_BSS_AT + 51      ; ...and where the climb is up to (32),
                                         ; which is a SECOND word and not a
                                         ; walked op_xlin: the first version
                                         ; walked the base and put it back by
                                         ; subtracting the BLOCK's size, while
                                         ; what the cursor advanced by is the
                                         ; SPAN's - so every part came out
                                         ; (block - span) low and op_xload's own
                                         ; failure path freed an address that
                                         ; was never claimed
op_xslack   equ OP_BSS_AT + 55      ; op_xload's own head slack...
op_xrd      equ OP_BSS_AT + 57      ; ...its cluster-aligned read cursor (32)
op_xleft    equ OP_BSS_AT + 61      ; ...bytes of the span still to copy up
op_xneed    equ OP_BSS_AT + 63      ; ...what the read has to cover in all
op_xcap     equ OP_BSS_AT + 65      ; ...one bufferful of it
op_xstg     equ OP_BSS_AT + 67      ; ...and the transient claim it uses
op_want     equ OP_BSS_AT + 69      ; bytes of the run that MUST still arrive,
                                    ; so a file that ends inside it refuses
                                    ; rather than filling half a claim
                                    ; (OP_BSS_AT + 71 was op_tail, the padding
                                    ; after the run's last body. op_bend
                                    ; already excludes it, so measuring it
                                    ; twice is what made a payload under 512
                                    ; bytes refuse itself - SPEC.md 20.12.7.3.
                                    ; The word is left UNNAMED rather than
                                    ; reclaimed: every offset below it would
                                    ; move, and this chain is read by
                                    ; apps/cc/crt0.asm too)

op_zn       equ OP_BSS_AT + 73      ; byte: how many rows carry OP_COMP. ZERO
                                    ; IS THE WHOLE OF THE OLD BEHAVIOUR - every
                                    ; path below tests it and takes exactly
                                    ; what it took before, so a package with no
                                    ; compressed part is unchanged to the
                                    ; instruction (SPEC.md 20.12.7)
op_usecs    equ OP_BSS_AT + 74      ; the run's UNPACKED length in sectors...
op_ubytes   equ OP_BSS_AT + 76      ; ...and in bytes. The claim is cut from
                                    ; these and the READ from op_secs/op_bytes,
                                    ; and the two are the same run measured at
                                    ; its two ends
op_rpara    equ OP_BSS_AT + 78      ; R, in paragraphs: how far up the claim the
                                    ; packed run lands so op_unpack can walk
                                    ; down onto it. 0 = it lands at the base,
                                    ; which is what it always did
op_rdseg    equ OP_BSS_AT + 80      ; ...and the segment that comes to, which is
                                    ; what op_read fills from. Its callers set
                                    ; it: op_claim to the base plus R, op_fetch
                                    ; to the claim it just made
op_zsrc     equ OP_BSS_AT + 82      ; op_unpack's read pointer, as a segment -
                                    ; BP is its write pointer and there is no
                                    ; third register left to hold this one
op_bend     equ OP_BSS_AT + 84      ; the run's EXACT byte end, where op_bytes
                                    ; is the same run rounded to whole sectors.
                                    ; op_want is cut from this one, because the
                                    ; last part's padding is the end of the
                                    ; FILE and asking for it is a short read
OP_BSS      equ 86

