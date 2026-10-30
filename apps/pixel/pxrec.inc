; =============================================================================
; os8088 - apps/pixel/pxrec.inc
;
; PiXEL's PICTURE RECORD and the numbers that go with it - pure equates,
; shared by the resident package and by the parts that are LINKED against
; it (SPEC.md 106.20: the SIMPLE part reads and writes the package's own
; variables by name, so the two must agree on every offset here).
; =============================================================================

%ifndef PXREC_INC
%define PXREC_INC

; --- the picture RECORD (SPEC.md 106.8): everything one picture is, so a
; cancelled open puts the previous one back by copying PXR_SZ bytes -------------
PXR_NAME    equ 0                   ; 13: NAME.EXT
PXR_VOL     equ 13                  ; byte: its drive
PXR_DIR     equ 14                  ; word: its folder's cluster
PXR_FSIZE   equ 16                  ; dword: its size, expanded
PXR_SW      equ 20                  ; word: the SOURCE's width...
PXR_SH      equ 22                  ; ...and height
PXR_FMT     equ 24                  ; byte: PXF_*
PXR_BITS    equ 25                  ; byte: bits a pixel, as the file says
PXR_PROG    equ 26                  ; byte: a progressive JPEG
PXR_FHAVE   equ 27                  ; byte: a file has been examined
PXR_FCOUNT  equ 28                  ; word: pictures in its folder
PXR_FIDX    equ 30                  ; word: its place among them
PXR_MW      equ 32                  ; word: the MASTER's width...
PXR_MH      equ 34                  ; ...and height
PXR_SCL     equ 36                  ; byte: s, the master is 1/2^s
PXR_PMODE   equ 37                  ; byte: PM_*
PXR_MSEG    equ 38                  ; word: the master's claim
PXR_MKB     equ 40                  ; word: ...and its KB
PXR_PACK    equ 42                  ; byte: PK_*
PXR_HAVE    equ 43                  ; byte: a DECODED picture is in the master
PXR_NPAL    equ 44                  ; word: palette entries a PAL source has
PXR_RF      equ 46                  ; byte: the decoder's row format (RF_*)
PXR_SERIAL  equ 47                  ; byte: bumped per picture (the thumbnail's
                                    ; key)
PXR_FAST    equ 48                  ; byte: the scale FAST OPEN held a JPEG
                                    ; to (SPEC.md 106.19), 0 none
PXR_DSCL    equ 49                  ; byte: the scale the master was DECODED
                                    ; at, which an edit does not change: a
                                    ; picture smaller than its file is never
                                    ; saved over it unasked (review-w7 F8)
PXR_KEEP    equ 50                  ; byte: the file holds what a master
                                    ; cannot - more frames, a transparent
                                    ; index, alpha: a Save over it goes
                                    ; through Save As (review-w8 N1)
PXR_SZ      equ 52

; --- a VIEW (pxview.inc): where the picture is on the screen and how it is
; sampled - the GIF part maps an animation frame's rect through the canvas's
; (SPEC.md 106.25) ------------------------------------------------------------
VW_IX       equ 0                   ; word: the picture's left on the screen
VW_IY       equ 2                   ; word: ...its top
VW_HS       equ 4                   ; dword: master pixels a screen pixel, 16.16
VW_VS       equ 8                   ; dword: master rows a screen row, 16.16
VW_DW       equ 12                  ; word: the picture's width on the screen
VW_DH       equ 14                  ; word: ...and height
VW_SZ       equ 16

; --- THE HISTOGRAM'S TAIL, past a master's pixels in its own claim (SPEC.md
; 106.20): 256 dword counts, then 256 dword bins (and the count's three byte
; planes while it runs) - the picture's own, so they move and are put back
; with it. The decoder parts fold them into the Histogram's statistics
; (pxplan.inc's pl_stats, 106.25) ---------------------------------------------
PXH_HIST    equ 0
PXH_BINS    equ 1024

; --- A CLAIM'S WINDOWS (KERN-MEGA-MEMORY-PLAN MC7): a master is reached by a
; byte offset, and under kern_mega's protected mode every claim is ONE LDT
; selector whose limit is 64KB - "the claim's segment + paragraphs" names
; some other descriptor. So a claim walked past 64KB is reached through a
; TABLE of OSAPI_SEG SEG_SUB selectors made once, one per 32KB: window k is
; based k x 32KB in and reaches 64KB (or the claim's end), so a row of a
; master (8,192 bytes at most) starting anywhere in its window ends inside
; it. On every real-mode kernel SEG_SUB answers the sum, claim + 800h x k,
; so the table is the same arithmetic made once - and a lookup is cheaper
; than the shifts it replaced. Window k of an offset O is O >> 15, the
; offset in it O & 7FFFh. A table names the WORDS holding its claim and its
; KB, and is rebuilt when that claim is not the one it was made of (its
; KEY); a real-mode move re-bases it (px_mreloc), a protected-mode one is a
; rebase that changes no selector. A master is held to PX_MWN windows: 1MB,
; more than any real-mode kernel can give one -----------------------------------
PX_MWN      equ 32                  ; windows a table holds: 32 x 32KB
MWT_KEY     equ 0                   ; word: the claim the table is OF; FFFFh
                                    ; made of nothing yet (ask again)
MWT_PSEG    equ 2                   ; word: where that claim's segment is
MWT_PKB     equ 4                   ; word: ...and its KB
MWT_WIN     equ 6                   ; PX_MWN words: the windows
MWT_SZ      equ MWT_WIN + 2 * PX_MWN
MWT_NONE    equ 0xFFFF              ; (no selector and no heap segment)

; PXSUB - DX = a segment or selector of ours, AX = paragraphs into it: DX =
; a DATA selector based there, to 64KB or the claim's end (OSAPI_SEG's
; SEG_SUB; DX + AX on every real-mode kernel). The same window asked again
; answers the same selector, so asking per OPEN costs one LDT entry and not
; one a call. Refused (no LDT entry free): DX = 0, the null selector, so a
; use is a #GP the kernel closes us for - never somebody else's memory.
; Every other register kept (the flags are not)
%macro PXSUB 0
    push ax
    push bx
    push cx
    mov bl, SEG_SUB
    xor cx, cx
    call OSAPI_SEG
    jnc %%ok
    xor dx, dx
%%ok:
    pop cx
    pop bx
    pop ax
%endmacro

; PXMW_LOOK - the lookup itself: DX:AX = a byte offset into the claim the
; table at DS:BX describes (its KEY already checked): AX = the segment of
; its window, DX = the offset in it. SI clobbered
%macro PXMW_LOOK 0
    shl ax, 1
    rcl dx, 1                       ; DX = O >> 15, the window...
    shr ax, 1                       ; ...AX = O & 7FFFh, the offset in it
    mov si, dx
    shl si, 1
    mov dx, ax
    mov ax, [bx + MWT_WIN + si]
%endmacro

PM_PAL      equ 0                   ; SPEC.md 106.8's three modes
PM_CUBE     equ 1
PM_GREY     equ 2
; (RF_* and PK_* are apps/pixel/pxpart.inc's: a part hands them over too)


; --- the formats PiXEL names (SPEC.md 106.6) -----------------------------------
PXF_NONE    equ 0
PXF_JPEG    equ 1
PXF_PNG     equ 2
PXF_GIF     equ 3
PXF_BMP     equ 4
PXF_PCX     equ 5
PXF_TIFF    equ 6
PXF_TGA     equ 7
PXF_PIX     equ 8
PXF_PNM     equ 9
PXF_ICO     equ 10
PXF_LBM     equ 11
PXF_MAC     equ 12
PXF_PACKED  equ 13                  ; a 'CZ' wrapper (SPEC.md 20.14)
PXF_N       equ 14

DF_BMP      equ 0                   ; the row decoders
DF_BMPRLE   equ 1
DF_PCX      equ 2
DF_TGA      equ 3
DF_PNMA     equ 4                   ; P1-P3, text
DF_PNMR     equ 5                   ; P4-P6, binary
DF_PIX      equ 6
DF_PART     equ 7                   ; a decoder PART (SPEC.md 106.18)

%endif
