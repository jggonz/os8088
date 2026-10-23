; =============================================================================
; os8088 - apps/video/vblk.inc
;
; THE BLOCK DECODER PART'S CONTRACT (SPEC.md 98.3.23): the header the part
; (apps/video/vblk.asm) opens with and the numbers both sides of the boundary
; agree on. Included by the player (apps/video/video.asm) and by the part, so
; the two cannot hold different opinions about where a vector is.
;
; THE PART is row 0 of VIDEO.O88's parts table (SPEC.md 20.12): a flat binary
; at org 0, OP_SEG | OP_LAZY | OP_COMP, fetched in the entry proc on every
; machine - every screen plays a one-bit file, natively or through the shadow. Its code runs with CS = its claim, its own
; tables through CS, and DS = the RECORD's segment while it walks a list - so
; it reaches nothing of the player's but through VBV_SET's callback.
;
; TWO ASSEMBLIES of one source (98.3.23.2): row 0 keeps its state at CS, and
; row 1 (-DVB_PM) writes nothing through CS, for a protected-mode kernel,
; where the player far-calls it through its code alias. One contract.
;
; EVERY VECTOR is a FAR call answered with `retf`:
;   in:  DS:SI = the list(s), ES = the surface, BP = the origin (added to
;        every address), AH = 1 to go ROUND THE TEXT BOX through the callback
;   out: SI past them; CF = 1 on a lead the part does not know (the record
;        is then given up)
;   keeps BP, DS, ES; clobbers AX, BX, CX, DX, DI
;
; THE CALLBACK (VBV_SET's CX:DX, the player's vp_bcb) writes ONE BYTE round
; the box: AL the byte, DI its address; AH the Map Mask on Mode X, the colour
; (0..15) of a VGA4 mask, nothing on LIN320 or one bit. It clobbers AX, BX, CX and DX
; and keeps the rest - kept thin because the rate hook's stack is 48 bytes
; (SPEC.md 53.2.2) and this path is the deepest a frame can take
; =============================================================================

%ifndef VBLK_INC
%define VBLK_INC

VB_SIG      equ 0                   ; word: 'VB'
VB_ABI      equ 2                   ; byte: VB_ABIVER
VB_NVEC     equ 3                   ; byte: VBV_N
VB_VEC      equ 4                   ; VBV_N words: near offsets in the part

VB_SIGVAL   equ 'VB'
VB_ABIVER   equ 2

VBV_SET     equ 0                   ; AX = the canvas's ROW STEP, BH = its
                                    ; BANKS (1, 2 or 4: row r of a block is
                                    ; r mod BH banks of 2000h on and r / BH
                                    ; steps down), BL = VBM_*, CX:DX = the
                                    ; callback, ES = THE PART'S OWN segment
                                    ; (the DATA one: the VB_PM assembly
                                    ; writes through it, 98.3.23.2). Out AX =
                                    ; VB_PROBE. At the
                                    ; bracket's door, every time: the player
                                    ; may have moved since the last - and the
                                    ; codebook is forgotten with VBM_NEWS
                                    ; (98.3.23.1). Keeps SI, DI,
                                    ; BP, DS and ES (the door holds the hook in DI)
VBV_LIN     equ 1                   ; LIN320's three block lists, after RUNL
VBV_PLN     equ 2                   ; AL = a planar lead, 20h or more: its list
VBV_ONE     equ 3                   ; ONE BIT's codebook and its tile list,
                                    ; after the ten lists (CGA and HERC)
VBV_N       equ 4

VBM_NOWM3   equ 1                   ; VGA4 on a card with no write mode 3
VBM_VGA4    equ 2                   ; the planar leads are VGA4's, not MODEX's
VBM_NEWS    equ 4                   ; a session's first bracket: no codebook yet

VB_PROBE    equ 0x5642 ^ 0x2E19     ; VBV_SET's answer

%endif
