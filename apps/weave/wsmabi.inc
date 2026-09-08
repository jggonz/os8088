; =============================================================================
; os8088 - apps/weave/wsmabi.inc      THE WEAVE.WSM CONTRACT (WEAVE-SPEC 1.2.2)
;
; The ONE file both assemblies %include, and the whole reason the two can be
; trusted to agree about anything: apps/weave/weave.asm builds WEAVE.O88 and
; apps/weave/wcanvas.asm builds WEAVE.WSM, and they are separate `nasm -f bin`
; jobs with no linker and no shared symbol between them (SPEC.md 73.1). Every
; number below is therefore a CONTRACT rather than an implementation detail,
; and WSM_ABI is what makes a change to one of them a refused load rather than
; a wrong answer.
;
; IT IS NOT THE CONTRACT EITHER. docs/WEAVE-SPEC.md is - sections 1.2.2,
; 6.10.3 and 6.10.4 - and this file is written from them (the os88.h/os88api.inc rule, said a
; third time). Where the two disagree, that one is right.
;
; WHY THERE IS A SECOND MODULE AT ALL, in one paragraph, because the next
; person to read this file will ask: SPEC.md 20.1 caps a package's image + bss
; at APP_MAX_SIZE = 0xF000, a kernel constant a package may not raise, and
; wave 4 closed at 60,862 of it. Everything here runs on a WORKER task, per
; frame, and SPEC.md 73.14's overlay is loaded by cc_ovneed, whose first
; instruction is `call cc_iswk` and whose answer on a worker is a refusal. So
; the overlay is not merely full, it is inapplicable, and the answer is
; CLAUDE.md's own hard rule read through SPEC.md 68.10: a second segment, and
; a RESIDENT one. WEAVE-PLAN 2.9 prices the four alternatives it beat.
; =============================================================================

; -----------------------------------------------------------------------------
; THE ABI NUMBER
;
; Bump it for ANY change to a verb, a register contract, or a layout below.
; The package assembles this value in and the module carries it at +2; a
; mismatch refuses the load with a sentence naming the file (WEAVE-SPEC 10.3).
; This is the half `.OVL`'s two size words cannot express - a contract change
; need not change a size - and it is why the stamp here is three words rather
; than two.
; -----------------------------------------------------------------------------
WSM_ABI     equ 2                   ; 2: WEAVE-SPEC 6.10.7's palette - the
                                    ; BIND block's colour word, WSMF_COLOR,
                                    ; and the sprite flags' top nibble

WSM_MAGIC   equ 0x5357              ; 'W','S' little-endian

; --- the module's header, at file offset 0 (WEAVE-SPEC 1.2.2) ----------------
WSM_H_MAGIC equ 0                   ; word: WSM_MAGIC
WSM_H_ABI   equ 2                   ; word: WSM_ABI
WSM_H_SIZE  equ 4                   ; word: the module's own byte count
WSM_H_STATE equ 6                   ; word: the state block's offset
WSM_ENTRY   equ 8                   ; ...and the far-called entry point

; --- the verbs (WEAVE-SPEC 6.10.3) -------------------------------------------
; Entered by `call far`, AL = the verb, AH = a verb-specific flag. DS is the
; caller's on entry and on return; the module switches its own and puts it
; back. ES is preserved. The answer is in AX.
WSMV_BIND   equ 0                   ; BX = canvas claim seg, CX = bundle claim
                                    ; seg, DX -> a 16-byte parameter block in
                                    ; the CALLER's DS (WSMP_*, below). out AX = 1
WSMV_SPRITE equ 1                   ; BX = sprite index, CL = a WSMF_* field,
                                    ; DX = the value, AH = 0 read / 1 write.
                                    ; out AX = the value, or 1/0 for a write
                                    ; accepted / refused (frame out of range)
WSMV_START  equ 2                   ; BX = fps 1..18, CX = the window pointer
                                    ; (the worker needs it for TASK_ALIVE and
                                    ; WM_WAKE). out AX = 1 running, 0 = fps
                                    ; outside 1..18
WSMV_STOP   equ 3                   ; out AX = 1 once the worker has stopped
WSMV_PAINT  equ 4                   ; BX = screen x, CX = screen y; the gfx
                                    ; lock is HELD by the caller and the clip
                                    ; is already armed. out AX = blits emitted
WSMV_DRAIN  equ 5                   ; DX -> a 4-word block in the caller's DS
                                    ; (comp, atom, data1, data2). out AX = 1 a
                                    ; record was written, 0 = the ring is empty
WSMV_UNBIND equ 6                   ; out AX = 1. Stops, forgets the claim and
                                    ; empties the ring
WSMV_WORKER equ 7                   ; the worker's body. It does not return
                                    ; (SPEC.md 20.6 rule 2)
WSMV_PLACE  equ 8                   ; BX = the canvas's cell x in the content
                                    ; grid, CX = its cell y. out AX = 1. The
                                    ; UI task writes it at every layout; the
                                    ; worker adds it to OSAPI_WM_CONTENT's
                                    ; answer, so a dragged window is right on
                                    ; the next frame and not the next paint

; --- WSMV_BIND's parameter block, 12 bytes in the caller's DS ----------------
WSMP_W      equ 0                   ; word: W in pixels (record w x 8)
WSMP_H      equ 2                   ; word: H in pixels (record h x 8)
WSMP_WALLS  equ 4                   ; word: T=1 B=2 L=4 R=8
WSMP_TICK   equ 6                   ; word: 0 = no ontick
WSMP_NSPR   equ 8                   ; word: sprites, 0..16
WSMP_SPOFF  equ 10                  ; word: the SPRITES section's offset in
                                    ; the bundle claim
WSMP_CID    equ 12                  ; word: the canvas's own comp_id
WSMP_COLOR  equ 14                  ; word: WEAVE-SPEC 6.10.7's palette. The
                                    ; low byte is the canvas's PAPER colour,
                                    ; 0..15, which is what a SET band bit
                                    ; becomes (6.10.2's polarity, so the names
                                    ; cross over). The load path passes
                                    ; CWHITE on a 1bpp adapter whatever the
                                    ; bundle says, and that one byte is the
                                    ; whole of "colour is a VGA thing"
WSMP_SIZE   equ 16

; --- WSMV_SPRITE's field ids -------------------------------------------------
; The six of WEAVE-SPEC 6.10's surface, plus the four the load path writes.
; They are NOT atom ids: an atom is the bundle's vocabulary and this is the
; module's, and conflating the two is how a format change becomes an ABI one.
WSMF_X      equ 0
WSMF_Y      equ 1
WSMF_VX     equ 2
WSMF_VY     equ 3
WSMF_FRAME  equ 4
WSMF_SHOWN  equ 5
WSMF_NFRAME equ 6                   ; read-only: the sprite's frame count, so
                                    ; the caller can spell 10.6.1's
                                    ; `frame %d of %d.`
WSMF_DESC   equ 7                   ; write-only: DX = the SPRITES descriptor
                                    ; index, taken at load from the PK_SPRITE
                                    ; record. Sets pw/ph/wb/nframes/data offset
WSMF_COLOR  equ 8                   ; write-only: DX = 6.10.7's colour, 0..15,
                                    ; into the flags byte's top nibble. Load
                                    ; path only - `color` is deliberately NOT
                                    ; on 6.10's script surface (9.2.1), so
                                    ; a running bundle cannot invent a pen
                                    ; pair the packer never checked

; --- the canvas claim (WEAVE-SPEC 6.10.4) ------------------------------------
WSC_W       equ 0                   ; word: W in pixels
WSC_H       equ 2                   ; word: H in pixels
WSC_WALLS   equ 4                   ; word
WSC_TICK    equ 6                   ; word
WSC_NSPR    equ 8                   ; word
WSC_STRIDE  equ 10                  ; word: W/8, the buffer's bytes a row
WSC_BUF     equ 12                  ; word: the buffer's offset in this claim
WSC_CID     equ 14                  ; word: the CANVAS's own comp_id, which
                                    ; is what every event record in 3.4
                                    ; carries - a sprite's id is this + 1 +
                                    ; its index, guaranteed by the validator
                                    ; (ids are 1..n in UISTREAM order and a
                                    ; sprite record follows its canvas
                                    ; directly, 2.5)
WSC_SPR     equ 16                  ; the sprite records begin here

WSR_PX16    equ 0                   ; word signed: x in 1/16 px
WSR_PY16    equ 2                   ; word signed
WSR_X       equ 4                   ; word signed: px, = px16 >> 4
WSR_Y       equ 6                   ; word signed
WSR_VX      equ 8                   ; word signed: 1/16 px per frame
WSR_VY      equ 10                  ; word signed
WSR_PW      equ 12                  ; byte: the sprite's pixel width
WSR_PH      equ 13                  ; byte: ...and height
WSR_WB      equ 14                  ; byte: its width in BYTES
WSR_NFR     equ 15                  ; byte: its frame count, 1..8
WSR_DOFF    equ 16                  ; word: frame 0's image offset within the
                                    ; SPRITES section
WSR_OX      equ 18                  ; word signed: x as last COMPOSED
WSR_OY      equ 20                  ; word signed
WSR_FLAGS   equ 22                  ; byte: WSRF_* in bits 0-3, and the
                                    ; sprite's COLOUR in bits 4-7 (6.10.7).
                                    ; Every site that writes this byte already
                                    ; writes through a mask, which is why the
                                    ; colour fits here and not in a 25th byte
WSR_FRAME   equ 23                  ; byte: frame | (last composed frame << 4)
WSR_SIZE    equ 24

WSRF_SHOWN  equ 1
WSRF_SCORED equ 2
WSRF_WASSH  equ 4                   ; shown as last composed
WSRF_DIRTY  equ 8                   ; needs its bands marked this frame
WSRF_STMASK equ 0x0F                ; ...and the colour is the other nibble
WSRF_COLSH  equ 4

WSM_MAXSPR  equ 16                  ; 2.11's own cap
WSM_MAXBAND equ 20                  ; 160 px / 8, 3.3's tallest canvas
WSM_MAXCOL  equ 40                  ; 320 px / 8, 3.3's widest - one byte a
                                    ; column of 6.10.7's colour map

; --- the module's state block, at WSM_H_STATE ---------------------------------
; The package reads and writes it with wblob.inc's w_b/w_w/w_pb/w_pw, which is
; why every field below is at a pinned offset and why there is no verb for
; most of them: the accessors already exist and cost the resident image
; nothing.
WSS_RUN     equ 0                   ; byte: 1 = the frame loop runs
WSS_ACK     equ 1                   ; byte: the worker's echo of RUN. STOP
                                    ; spins on it, so the UI task never frees
                                    ; a claim a frame is still composing into
WSS_SLEEP   equ 2                   ; word: ticks between frames
WSS_PHASE   equ 4                   ; word
WSS_FRAME   equ 6                   ; word: the frame counter (6.10.1)
WSS_CVSEG   equ 8                   ; word: the canvas claim, 0 = unbound
WSS_WIN     equ 10                  ; word: our window pointer (KERNEL_SEG)
WSS_BSEG    equ 12                  ; word: the bundle claim
WSS_SPOFF   equ 14                  ; word: the SPRITES section's offset
WSS_NSPR    equ 16                  ; word
WSS_BLITS   equ 18                  ; word: GFX_BLIT1 calls made - the
WSS_FRAMES  equ 20                  ; word: ...and frames composed. tests/
                                    ; weavegame reads the quotient against
                                    ; WEAVE-SPEC 14's 2-4
WSS_OVF     equ 22                  ; word: records the staging ring could not
                                    ; take. INPUT OVERRUN is one of the three
                                    ; defects no emulator shows (CLAUDE.md),
                                    ; so it is counted rather than hoped about
WSS_TICKP   equ 24                  ; byte: an ontick is staged (4.9 rule 3's
                                    ; collapse, as a flag rather than a scan)
WSS_RHEAD   equ 25                  ; byte: the UI task's index. ONE WRITER
WSS_RTAIL   equ 26                  ; byte: the worker's. ONE WRITER, and that
                                    ; is the whole of the handshake - a count
                                    ; would have two and need a lock a worker
                                    ; may not take (SPEC.md 20.6 rule 3)
WSS_CX      equ 27                  ; word: the canvas's cell x
WSS_CY      equ 29                  ; word: ...and cell y
WSS_KEYS    equ 31                  ; 5 bytes: last frame's key bitmap
WSS_DIRTY   equ 36                  ; WSM_MAXBAND bytes: the dirty bands
WSS_CONT    equ 56                  ; 15 bytes: the contact bitmap, one bit a
                                    ; pair over 16 sprites (120 pairs)
WSS_RING    equ 71                  ; WSM_RING x 6 bytes
WSS_PEN     equ WSS_RING + WSM_RING * WSM_REC
                                    ; word: 6.10.7's pen. Low byte the PAPER
                                    ; colour, high byte 1 when this canvas
                                    ; draws in colour at all - and appended
                                    ; AFTER the ring rather than beside its
                                    ; neighbours, so not one existing offset
                                    ; moves and the package's w_b/w_w reads
                                    ; are the same bytes they were
WSS_SIZE    equ WSS_PEN + 2

WSM_RING    equ 32                  ; slots; 31 usable, head == tail is empty
WSM_REC     equ 6                   ; comp byte, atom byte, data1 word, data2

; The polled key set (WEAVE-SPEC 6.10.5): 26 letters, 10 digits, space.
WSM_NKEY    equ 37
