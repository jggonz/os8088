; =============================================================================
; os8088 - apps/pixel/pxpart.inc
;
; PiXEL's PART ABI (SPEC.md 106.5): the header every far-called code part
; starts with, and the numbers both sides of the boundary agree on. It is
; included by the RESIDENT package (apps/pixel/pixel.asm) and by every part
; (apps/pixel/pxhelp.asm today, the decoders later), so the two cannot hold
; different opinions about where a vector is.
;
; A PART IS AN OP_SEG ROW (SPEC.md 20.12): a flat binary assembled at org 0,
; carried inside PIXEL.O88, read into a claim of its own by op_fetch when it
; is first wanted and given back by op_drop. Its CODE runs with CS = that
; claim, so it is reached by a FAR call and it answers with `retf`. Nothing
; else about it is special, and the rules below are SPEC.md 95.8's module
; rules (SCRIBE.OVL) carried over to a part:
;
;   1. ITS OWN TABLES THROUGH CS. A part's strings and constants are in the
;      part, so they are read `[cs:...]`. `push cs / pop ds` is correct ONLY
;      for the part's own data and never to get back to the package's.
;   2. THE PACKAGE THROUGH DS, OR THROUGH PXP_PKG. A vector is entered with
;      DS = the package's segment, exactly as the caller had it, so package
;      bss is `[name]` until the part repoints DS. A part that does repoint it
;      gets the package back from `[cs:PXP_PKG]`, which the resident stamps
;      before EVERY call - so a region that moved between two calls (SPEC.md
;      66) is never named stale. A part must not keep that segment anywhere
;      that outlives the call.
;   3. IT NEVER SPEAKS. No toast, no drawing, no file call: a part answers a
;      NUMBER (PXE_*) and the resident caller words it. Those are SPEC.md
;      20.12.5's ES-fenced slots and 20.6 rule 7's file slots, and a part
;      that runs on PiXEL's worker (later waves) could reach neither anyway.
;   4. IT RETURNS CF. CF = 0 success with the verb's own outputs; CF = 1 and
;      AX = a PXE_* number.
;
; The resident's half - fetch, validate, stamp, far call, drop - is px_pcall
; and px_pdrop in apps/pixel/pixel.asm, and tests/pxparts.py is its gate.
; =============================================================================

%ifndef PXPART_INC
%define PXPART_INC

; --- the header, at offset 0 of every part ------------------------------------
PXP_SIG     equ 0                   ; word: 'PX' - a part that does not start
                                    ; with it is not one of ours, and the
                                    ; resident refuses to call into it
PXP_ABI     equ 2                   ; byte: PXP_ABIVER. A part and a resident
                                    ; built from different trees disagree here
                                    ; before they disagree anywhere worse
PXP_NVEC    equ 3                   ; byte: how many vectors follow
PXP_PKG     equ 4                   ; word: THE PACKAGE'S SEGMENT, written by
                                    ; the resident immediately before every
                                    ; call (rule 2 above)
PXP_VEC     equ 6                   ; PXP_NVEC words: near offsets in the part
PXP_HDRSZ   equ 6

PXP_SIGVAL  equ 'PX'                ; as `dw 'PX'`: 'P' at +0, 'X' at +1
PXP_ABIVER  equ 4                   ; 2 since wave 3 added PXV_HEAD, 3
                                    ; since wave 4 added the flags, the
                                    ; scratch a scale, heads past the first
                                    ; and K_COLS (SPEC.md 106.19), 4 PLANS
                                    ; (106.20)

; --- the vectors, in this order in every part ---------------------------------
PXV_INIT    equ 0                   ; out AX = PXP_PROBE. Nothing else: it is
                                    ; the call the gate makes to prove the
                                    ; boundary, and a decoder's place to build
                                    ; tables it would rather not ship
PXV_DECODE  equ 1                   ; a decoder's body (SPEC.md 106.5); a part
                                    ; that is not a decoder answers PXE_NOTSUP
PXV_INFO    equ 2                   ; in ES:DI = a buffer, CX = its bytes. The
                                    ; part's text or facts, copied out
PXV_HEAD    equ 3                   ; a decoder's HEADER, on the UI task: in
                                    ; DI = the CONTEXT (below), its head at
                                    ; [PXK_HSEG]:0. SPEC.md 106.18
PXV_PLANS   equ 4                   ; a decoder's PLANS: [px_plan] for the
                                    ; picture's palette (106.20), on either
                                    ; task; a part that is not one answers
                                    ; PXE_NOTSUP
PXV_N       equ 5

PXP_PROBE   equ 0x5850 ^ 0x1D06     ; PXV_INIT's answer, which no register a
                                    ; caller set up could already hold

; --- the answers -----------------------------------------------------------------
PXE_OK      equ 0
PXE_NOTSUP  equ 1                   ; this part has no such verb
PXE_ROOM    equ 2                   ; the caller's buffer is too small
PXE_PART    equ 3                   ; THE RESIDENT'S: the part could not be
                                    ; fetched. op_fetch has already said why
PXE_BADPART equ 4                   ; THE RESIDENT'S: it was fetched and is
                                    ; not a PiXEL part of this ABI - its
                                    ; signature, version or vector count is
                                    ; wrong. Nothing has said so yet

; --- PXPART_HEAD init, decode, info, head, plans ------------------------------
; The header a part opens with. Assemble the part with `org 0`.
%macro PXPART_HEAD 5
    dw PXP_SIGVAL               ; +0
    db PXP_ABIVER               ; +2
    db PXV_N                    ; +3
    dw 0                        ; +4 PXP_PKG, the resident's to write
    dw %1                       ; +6 PXV_INIT
    dw %2                       ; +8 PXV_DECODE
    dw %3                       ; +10 PXV_INFO
    dw %4                       ; +12 PXV_HEAD
    dw %5                       ; +14 PXV_PLANS
%endmacro

; =============================================================================
; A DECODER PART (SPEC.md 106.10, 106.18): the answers it gives, the row
; formats it hands the emitter, the packings the Info panel names - shared
; with the resident's simple formats, so both sides mean the same number
; =============================================================================
PXD_OK      equ 0                   ; the answers (pixelsim's PXD_*)
PXD_HEAD    equ 1
PXD_DIMS    equ 2
PXD_DEPTH   equ 3
PXD_PACK    equ 4
PXD_TRUNC   equ 5
PXD_DATA    equ 6
PXD_MEM     equ 7
PXD_READ    equ 8
PXD_ORIENT  equ 9
PXD_NOTYET  equ 10
PXD_BIG     equ 11
PXD_NOFILE  equ 12                  ; the resident's: not in this folder
PXD_NOTPIC  equ 13                  ; neither its bytes nor its name are a
                                    ; picture's
PXD_NREASON equ 14
PXD_MORE    equ 0x80                ; not a refusal: HEAD wants the head
                                    ; read again at [PXK_HNEXT] (106.19) -
                                    ; the part's own read since 106.25
                                    ; (pxhmore.inc), never the resident's
PXD_TAIL    equ 0x81                ; RETIRED (106.25): the SIMPLE part reads
                                    ; a PCX's tail itself; the number is not
                                    ; given to anything else
PXD_LINK    equ 0x82                ; the SIMPLE part was linked against
                                    ; another build of the package (106.20)
PXD_REDIR   equ 0x83                ; not a refusal: the EXTRAS part's ICO
                                    ; HEAD found a PNG inside it, moved the
                                    ; head down to it and set [px_sbase]:
                                    ; the PNG part reads it (106.25)
PXD_ABORT   equ 0xFF                ; not a refusal: the user said stop

RF_IDX      equ 0                   ; what a decoder hands the emitter
RF_GREY     equ 1
RF_RGB      equ 2

PK_NONE     equ 0                   ; the Info panel's Packing
PK_RLE      equ 1
PK_RLE8     equ 2
PK_RLE4     equ 3
PK_BITF     equ 4
PK_LZW      equ 5
PK_LZWI     equ 6                   ; ...interlaced
PK_DEFL     equ 7
PK_DEFLI    equ 8                   ; ...Adam7
PK_JPEG     equ 9                   ; baseline (or extended) DCT
PK_JPEGP    equ 10                  ; progressive DCT
PK_N        equ 11

PX_DIMMAX   equ 8192                ; refused above, before any arithmetic
PXP_BG      equ 85                  ; THE VIEW BACKGROUND a transparent pixel
                                    ; shows, each channel: the canvas's own
                                    ; dark grey (SPEC.md 106.18)

; --- THE CONTEXT: one block of the package's bss, its offset in DI on HEAD
; and DECODE. "in" is the resident's to fill, "out" the part's --------------
PXK_HSEG    equ 0                   ; word in  HEAD: the head's segment...
PXK_HLEN    equ 2                   ; word in  HEAD: ...and its bytes
PXK_SW      equ 4                   ; word out HEAD: the source's width...
PXK_SH      equ 6                   ; word out HEAD: ...and height
PXK_RF      equ 8                   ; byte out HEAD: RF_*
PXK_BITS    equ 9                   ; byte out HEAD, DECODE: bits a pixel
PXK_PACK    equ 10                  ; byte out HEAD, DECODE: PK_*
PXK_RBLK    equ 11                  ; byte out DECODE: source rows a block
                                    ; sums below 1/1, 0 = all 2^s of them
PXK_NPAL    equ 12                  ; word out HEAD, DECODE: palette entries
PXK_DPARA   equ 14                  ; word out HEAD: decoder scratch, paras
PXK_SCL     equ 16                  ; byte in  DECODE: the scale s
PXK_MW      equ 18                  ; word in  DECODE: the master's width...
PXK_MH      equ 20                  ; word in  DECODE: ...and height
PXK_RSEG    equ 22                  ; word in  DECODE: the ROW BUFFER (106.9)
PXK_DSEG    equ 24                  ; word in  DECODE: the decoder SCRATCH
PXK_SPAL    equ 26                  ; word in  DECODE: [px_spal]'s offset
PXK_SVC     equ 28                  ; PXS_N words in: the services' offsets
PXK_FLAGS   equ 42                  ; byte out HEAD: PXKF_*
PXK_HCNT    equ 43                  ; byte in/out HEAD: heads read so far (the
                                    ; resident zeroes it before the first);
                                    ; the SIMPLE part's PCX tail: 1 read, 2
                                    ; none (SPEC.md 106.20)
PXK_HPOS    equ 44                  ; word in  HEAD: where the walk starts in
                                    ; the head, 0 on the first call
PXK_HBASE   equ 46                  ; dword in HEAD: the file offset of the
                                    ; head's byte 0
PXK_HNEXT   equ 50                  ; dword out HEAD, with PXD_MORE: the file
                                    ; offset the next head is to start at
PXK_FSZ     equ 54                  ; dword in HEAD, DECODE: the file's size
PXK_DSC     equ 58                  ; 4 words out HEAD (PXKF_DSC): decoder
                                    ; scratch, paragraphs, at scale 0..3;
                                    ; FFFFh = not at that scale
PXK_PRIV    equ 66                  ; 16 bytes: the PART's own, HEAD to
                                    ; DECODE - the resident never reads them
PXK_SZ      equ 82

PXKF_UP     equ 1                   ; rows arrive bottom-up ([px_dtop] 0)
PXKF_DCT    equ 2                   ; rows arrive AT THE MASTER'S SCALE, a
                                    ; master row each: no box filter
PXKF_DSC    equ 4                   ; PXK_DSC answers the scratch per scale
PXKF_PROG   equ 8                   ; a progressive picture (the Info panel)
PXKF_LATE   equ 16                  ; no row is complete before the last:
                                    ; nothing to paint as it arrives

; --- THE SERVICES (SPEC.md 106.18): far thunks of the package, a part calls
; `call far` on PXK_SVC + 2 x one of these and the package's segment, and
; each sets its own DS -------------------------------------------------------
PXS_NEXT    equ 0                   ; out CF = 1 the end; else ES:SI, CX bytes
PXS_EMIT    equ 1                   ; in AX = source row, BX = rows complete
                                    ; or FFFFh, DX = progress or FFFFh.
                                    ; out CF = 1 AX = PXD_ABORT
PXS_SCAT    equ 2                   ; in AX = row, CL = x0, CH = dx, BX, DX
PXS_TICK    equ 3                   ; in AX = progress. out CF/AX as EMIT
PXS_PAL     equ 4                   ; the palette is known. out CF/AX
PXS_COLS    equ 5                   ; ES:SI = BX master rows of DX pixels,
                                    ; top-left at row AX column CX; DI =
                                    ; rows complete or FFFFh (106.19)
PXS_RING    equ 6                   ; px_rnext: the worker's ring window on
                                    ; (the SIMPLE part's, SPEC.md 106.20)
PXS_N       equ 7

%endif
