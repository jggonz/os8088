; =============================================================================
; os8088 - apps/video/video.asm
;
; VIDEO PLAYER (SPEC.md 98.3): a .V88 file (SPEC.md 98.1) played fullscreen
; at its own frame rate, the decode inside the timer interrupt the way XDC
; (MobyGamer's, MIT, (c) 2014 Jim Leonard) does it, the file read behind it
; through OSAPI_FILE_READ_SEQ. Wave 3 of docs/plans/VIDEO-PLAN.md: SILENT,
; paced by FSXF_RATE (SPEC.md 53.2.2), on a surface of the file's own layout.
;
; THREE PIECES, and where each runs:
;   vp_hook    the FSXF_RATE hook - IRQ0, IF=0 on entry, 48 bytes of stack.
;              It draws the frames that are due, straight into the adapter,
;              from super-packets the reader has already put in memory.
;   vp_main    the bracket's foreground - the UI task, the machine frozen
;              round it. It reads the file into the ring in 32 KB chunks and
;              polls for Esc.
;   the rest   the window: the file's numbers, P to play, and what the last
;              play cost.
;
; THE RING (SPEC.md 98.3). K slots of 32 KB (a power of two only in a LIVE
; play; elsewhere as many as the machine has), and a MIRROR
; slot after them: every chunk that lands in slot 0 is copied there too, so a
; super-packet (<= 32 KB) that starts in slot K-1 runs on into the mirror and
; is contiguous. Positions are (chunk, offset) pairs; chunk c lives in slot
; c mod K (vp_slot). The reader may fill chunk c only when c < (the hook's
; super-packet's chunk) + K; the hook may enter a super-packet only when
; every chunk it touches has been loaded. [vp_lc], the chunks loaded, is the
; ONE word both sides read, and the reader writes it last.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Video Player', vp_entry, 0x23  ; icon, assoc, doc glyph

; --- the icon (SPEC.md 20.2): a play button --------------------------------
; A solid disc with a play triangle cut out of it, so it reads as one shape
; on every adapter - no grey, no one-pixel stroke (SPEC.md 39.4). The mask
; is the disc dilated one pixel, a white underlay round it.
;
;   .....######.....    the data; the mask is every pixel but the corners
;   ..##########..
;   .####.#######.      (rows 0-2 and 13-15 shown trimmed)
;   .####..######.
;   #####....#####
;   #####.....####
;   #####.......##
    OS88_ICON16
    dw 0x3FFC, 0x7FFE, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x7FFE, 0x3FFC
    dw 0x07E0, 0x1FF8, 0x3FFC, 0x7BFE, 0x79FE, 0xF87F, 0xF83F, 0xF80F
    dw 0xF80F, 0xF83F, 0xF87F, 0x79FE, 0x7BFE, 0x3FFC, 0x1FF8, 0x07E0
    OS88_ICON16_END

    OS88_ASSOC16                    ; SPEC.md 54.6: double-click a .V88
    db 1
    OS88_ASSOC_EXT 'V88'
    OS88_ASSOC16_END

; --- what a .V88 wears (SPEC.md 54.3.2) -------------------------------------
; The disc reduced by majority would be a solid blob with the triangle gone,
; so a document wears the triangle alone, black on its page.
    OS88_DOCGLYPH8
    db 0x40                         ; .#......
    db 0x70                         ; .###....
    db 0x7C                         ; .#####..
    db 0x7E                         ; .######.
    db 0x7C                         ; .#####..
    db 0x70                         ; .###....
    db 0x40                         ; .#......
    db 0x00                         ; ........
    OS88_DOCGLYPH8_END

VP_CHUNK    equ 32768               ; a ring slot, and a READ_SEQ call
VP_XBMIN    equ 128                 ; THE BANK (98.3.18.2): the least worth
                                    ; taking, KB - and the WHOLE pool is taken,
                                    ; none left to others (98.3.18.2.1)
VP_EFAST    equ 15                  ; THE EMS FRAME'S SPEED (98.3.18.8), in
VP_ESLOW    equ 40                  ; tenths of RAM's time: EMS first and in
                                    ; place to 1.5x, a bank with copies to 4x
VP_RL       equ 16384               ; the audio ring (SPEC.md 98.3.1)...
VP_RLCODE   equ 2                   ; ...4096 << 2
VP_BLOCK    equ 2048                ; the card's block: one interrupt each
VP_BLKDIV   equ 40                  ; ...halved while longer than 1/40 s
VP_HALFHI   equ 22222               ; (PCM8 past this keeps it: vp_sblk)
VP_SPK      equ 2                   ; [vp_snd]: the SPEAKER plays it (98.3.15)
VP_SPKMAX   equ 8000                ; the fastest PCM an 8088 plays through
                                    ; the speaker (SPEC.md 34.11.4)
VP_AMAX     equ 4                   ; frames of audio a hook call puts in
VP_SKIPMAX  equ 8                   ; shadow copies a play behind may skip
VP_MSYNC    equ 18                  ; ticks a play waits after a REAL mode
                                    ; change, for a monitor to re-sync
                                    ; (98.3.20): ~1 s, counted from the mode
                                    ; set, so a disk fill longer than that
                                    ; pays nothing
VP_SLOTP    equ VP_CHUNK / 16       ; ...in paragraphs
VP_KMAX     equ 8                   ; a LIVE play's most slots (a power of two)
VP_KBIG     equ 15                  ; ...and any other's (SPEC.md 98.3)
VP_COLS     equ 35                  ; the info panel's text columns
%ifdef VP_DIAG
VP_LINES    equ 13                  ; (the last five the heap's, the reader's
                                    ; and the layer's tests: VPDIAG=1, a
                                    ; field diagnostic)
%else
VP_LINES    equ 8
%endif
VP_LINESB   equ 8                   ; ...and all a card with its buttons in
VP_LINE     equ VP_COLS + 1
VP_LPITCH   equ 11                  ; ...and its line pitch
; --- the window (SPEC.md 98.4.1): content-relative, every x a byte's. What
;     depends on the file and the screen is vp_layfit's, in the vp_l* words
VP_BOXX     equ 8                   ; the picture box's inside, top left
VP_BOXY     equ 6
VP_MINBW    equ 256                 ; ...never narrower than the button row
VP_BARH     equ 10                  ; the scrub bar, frame included
VP_THW      equ 8                   ; the thumb's width
VP_CARDW    equ 280                 ; the info card: VP_COLS cells
VP_TXTY     equ 6
VP_CARDH    equ VP_TXTY + VP_LINES * VP_LPITCH + 4
VP_CARDBY   equ 96                  ; the buttons in the card, under its text
VP_CARDHB   equ VP_CARDBY + 20 + 4
VP_BTW      equ 28                  ; a button (Tracker's transport, SPEC.md
VP_BTH      equ 20                  ; 45), and their pitch
VP_BTP      equ 32
VP_NB       equ 4                   ; Open, previous key, Play, next key...
VP_NBTN     equ VP_NB + 3           ; ...Repeat, Mute, and the info card's
VP_CURW     equ 8                   ; a stream cursor, in words (vp_next)
VC_PC       equ 0                   ; ...its fields: the chunk, the offset,
VC_PO       equ 2                   ; the super-packet's sectors, the next
VC_PSEC     equ 4                   ; one's, frames left in it, the record's
VC_NSEC     equ 6                   ; offset, records to step over, and the
VC_FLEFT    equ 8                   ; seams taken - [vp_pc]'s order
VC_ROFS     equ 10
VC_SKIP     equ 12
VC_GEN      equ 14
VP_LCAP     equ 4                   ; a live pass's most frames (98.3.10)
VP_DRAGT    equ 9                   ; ticks between loads mid-drag, 286 up
VP_BSLACK   equ 3                   ; the box's rows under the picture: its
                                    ; top goes down to a bank (98.3.7)
VP_BGAP     equ 6                   ; ...and the rows from the box to the bar,
VP_CGAP     equ 3                   ; and the bar to the buttons - or, COMPACT
                                    ; (98.4.1.1), these, the slack the
                                    ; desktop's banks need and no more
VPX_STOP    equ 1                   ; how a bracket ended (98.3.7): stopped,
VPX_SWAP    equ 2                   ; swapped between window and full screen,
VPX_DESK    equ 3                   ; back to the desktop, paused - never 0,
                                    ; which is vp_poll's "nothing" 
%if VPX_STOP != 1
    %error "vp_main's .pfesc makes VPX_STOP with an inc from 0"
%endif
VP_KMAXREC  equ 61440               ; the largest keyframe record we will read:
                                    ; a VGA8 one is up to a canvas (98.1.3),
                                    ; and what bounds it is the read - the
                                    ; record's clusters at the worst offset
                                    ; in one 64 KB claim (vp_spankb), which
                                    ; vp_parse works out

; --- the file (SPEC.md 98.1.1) -------------------------------------------------
V88_FRAMES  equ 8
V88_RATE    equ 12
V88_SPF     equ 14
V88_AUDIO   equ 16
V88_NREND   equ 17
V88_ABYTES  equ 18
V88_PITDIV  equ 20
V88_PITPER  equ 22
V88_RING    equ 23                  ; the ring the stream assumes, slots
V88_TITLE   equ 32
V88_FLAGS   equ 6
V88F_RESIDENT equ 1                 ; the flags: every rendition one BLOCK,
V88F_LOOPREC equ 2                  ; read whole (98.1.7); a seam record
V88F_REPEAT equ 4                   ; (98.1.1.2); Repeat on at the start
V88F_LIVE   equ 8                   ; ...may play on the live desktop (98.3.10)
V88F_RUNS   equ 16                  ; ...its frame records carry BLIT RUNS
V88F_SPKPWM equ 32                  ; ...its PCM8 is the SPEAKER's counts
V88F_SPKMUL equ 64                  ; ...made for PULSES A SAMPLE past one
V88_SPKP    equ 24                  ; (98.1.1.3.1): how many, 2..4
V88F_AHEAD  equ 128                 ; SOUND AHEAD (98.1.8): record r carries
V88_AHEAD   equ 25                  ; frame r + A's sound, A here; the START's
V88_LEAD0   equ 464                 ; lead (frames 0..A-1) a dword offset here
VP_SPBIG    equ 127                 ; BIGSP's super-packet, sectors (98.1.4.1):
                                    ; 63.5 KB, so one from a sector's start
                                    ; is a segment's
VP_ALMAX    equ 8000                ; A x abytes at most: staged in the sound
                                    ; ring's tail, clear of what vp_aput fills
V88F_BIGSP  equ 256                 ; SUPER-PACKETS PAST 32 KB (98.1.4.1):
                                    ; up to VP_SPBIG sectors, a stream's only
V88F_KLEADS equ 512                 ; THE KEYS' LEADS APART (98.1.8.1): key i's
V88_KLEADS  equ 468                 ; lead at this dword + i x A x abytes,
                                    ; not its entry's tail
V88_XBANK   equ 472                 ; THE BANK (98.3.18.3): the XMS its encode
V88_XPRE    equ 474                 ; assumed, KB, and the prefill it banked
V88_YBASE   equ 500                 ; THE BASE'S YARDSTICK (98.3.18.9)
V88_LYORD   equ 502                 ; THE LAYER BEFORE THE BANK (98.1.9.1),
V88_LYNEED  equ 504                 ; ...while the bank holds this KB
V88_LYBKS   equ 506                 ; THE BASE'S DISK, KB/s idle (98.1.9.6)
V88_LYIDL   equ 508                 ; ...and the layer machine's
V88_BENCHL  equ 510                 ; THE BENCH (98.1.9.13) on the layer's
R_BENCH     equ 62                  ; ...and in the rendition, on the base's
VP_BNSPAN   equ 192                 ; every bench write inside this: the least
                                    ; off-screen gap (CGA's, after bank 0)
VP_BNMAX    equ 10496               ; ...and the record's most bytes (os88vid's
                                    ; bench_record, any kind: t_bench asserts)
V88_LAYER   equ 476                 ; THE LAYER (98.1.9): u32 its first
                                    ; super-packet, u16 64, u16 KB/s, u16 KB,
                                    ; u16 prefill, u32 its key table, u16 the
                                    ; longest record - all 0 for none
VP_LYMAX    equ 4                   ; the layer's slots at most
VP_LYTMIN   equ 100                 ; ...and the least yardstick the bench
                                    ; can judge a CPU by, 10 us (98.1.9.12)
VP_LCONT    equ 0x8040              ; the header's word for a CONTINUOUS layer
                                    ; (98.1.9.8): 32 KB chunks, bit 15 the
                                    ; kind - the padded super-packets of
                                    ; before it said 64, and play the base
                                    ; on, KB - no flag, 0 none
VP_KLRING   equ 96                  ; a key's read and its lead's, KB: what the
                                    ; least ring holds, two slots and a mirror
V88F_SCREEN equ 1024                ; A SCREEN OF ITS OWN (98.1.3.2.1): its
                                    ; rendition's R_SCREEN is not 0
V88F_PALEV  equ 2048                ; A PALETTE A SCENE (98.1.3.2.3): a table
R_PEV       equ 56                  ; at this dword of the slot, its entries
R_PEVN      equ 60                  ; this word - u32 the frame a scene starts
PEV_LEN     equ 52                  ; at, 48 bytes its sixteen
VP_PEVMAX   equ 255
V88F_SUB2   equ 4096                ; A PLANAR FILE'S SUB-RECORDS (98.1.3.2.4)
                                    ; carry a LIST MAP after the lead - only
                                    ; the lists it names are there - and on
                                    ; VGA4 a lead of 10h + c is ONE COLOUR c,
                                    ; stored in write mode 3
V88F_PAGE   equ 8192                ; PAGE-AHEAD (98.1.3.7): rendition byte
                                    ; 37 is 3, and a record's y0 bit 15 is a
                                    ; FLIP, its y1 bit 15 a hidden-page
                                    ; record after its lists
V88F_KPARTS equ 16384               ; KEYS IN PARTS (98.1.3.8): a key past
                                    ; one read is several key records back
                                    ; to back, one read each - a part's y0
                                    ; bit 15 says another follows, its last
                                    ; two bytes how long that one's read is
V88F_KPOOL  equ 32768               ; THE PAGE POOL (98.1.3.8.1): a page a
                                    ; key names, stored once
REF_LEN     equ 12                  ; ...a reference: len y0 y1 at(32) n
VP_KREFS    equ 7                   ; ...a key's references, at most (N-1)
V88F_KNOWN  equ V88F_RESIDENT | V88F_LOOPREC | V88F_REPEAT | V88F_LIVE \
                | V88F_RUNS | V88F_SPKPWM | V88F_SPKMUL | V88F_AHEAD \
                | V88F_BIGSP | V88F_KLEADS | V88F_PALEV | V88F_SUB2 \
                | V88F_PAGE | V88F_KPARTS | V88F_SCREEN | V88F_KPOOL
R_TARGET    equ 53                  ; LIVE: the screen a rendition was drawn
                                    ; for - 1 CGA, 2 Hercules, 3 VGA/EGA
V88_AUDBLK  equ 176                 ; RESIDENT: the audio block's offset,
R_BLOCK     equ 40                  ; packed and unpacked bytes and packing -
BK_OFF      equ 0                   ; and a rendition's picture block's, at
BK_PACKED   equ 4                   ; R_BLOCK in its slot
BK_UNPACKED equ 8
BK_PACK     equ 12
V88_LOOP    equ 448                 ; the loop block: L, the seam's offset and
LP_L        equ 0                   ; length, the super-packet of frame L+1 -
LP_OFF      equ 4                   ; its sectors, the records before frame
LP_LEN      equ 8                   ; L+1 there, its offset
LP_SECS     equ 10
LP_IDX      equ 11
LP_SP       equ 12
V88_REND    equ 192                 ; rendition 0
R_PIXFMT    equ 0
R_LAYOUT    equ 1
R_WB        equ 2
R_H         equ 4
R_KTAB      equ 8
R_NKEYS     equ 12
R_THUMB     equ 40                  ; DESKTOP POSTERS (98.1.3.11): a stream's
R_THUMBR    equ 56                  ; slot bytes 40..45, a resident file's
                                    ; 56..61 - the table's offset, the
                                    ; scale, the flags:
TH_CUSTOM   equ 1                   ; a custom picture after the keys'
TH_KEYS     equ 2                   ; a picture a key
TH_COLOUR   equ 4                   ; each in the desktop's sixteen
TH_MONO     equ 8                   ; ...and/or in black and white
TH_SIZE     equ 16                  ; the scale is the Preview's own
R_POSTER    equ 14
R_SP0       equ 16
R_SP0N      equ 20
R_SPMAX     equ 22
R_PAL       equ 32                  ; VGA8: the palette's offset (98.1.1)
R_RSCALE    equ 36                  ; ...and its row scale, 0/1 or 2
R_FLIP      equ 37                  ; MODEX: 2 = two pages, flipped (98.3.8)
R_SCREEN    equ 38                  ; VGA4: the screen it plays on (98.1.3.2.1)
R_PAGES     equ 39                  ; byte 37 = 4: the pages, 3..8 (98.1.3.7.1)
VP_HELD     equ 0xFFFE              ; vp_prevn after a HELD record (98.1.3.9)
ROW_MASK    equ 0x0FFF              ; ...an N-page record's rows, its page and
                                    ; flags above them
COPY_LEN    equ 6                   ; a COPY's header alone (98.1.3.7.2)
VP_NSCR     equ 7                   ; ...of vp_scrtab's
VP_SCRN     equ 5                   ; ...from which they are NARROW: rows 40
                                    ; plane bytes apart (98.1.3.2.5)
VP_PAGE     equ 19200               ; a Mode X page, in plane bytes
VP_PAGE200  equ 16000               ; ...and at 320 x 200 (98.1.3.1.1)
VP_LCW      equ 2                   ; a latch-copied page byte, in record
                                    ; bytes' decode: ESTIMATED (98.3.8.1)
VP_MBUF     equ 40                  ; vp_fits's line, and its NUL
VP_PREVKB   equ 31                  ; the last record's copy: REC_MAX + slack
PF_VGA8     equ 2                   ; [vp_pixfmt] is the format less one
PF_VGA4     equ 3                   ; ...16 colours on mode 12h's planes
; LIVE WITH SOUND (SPEC.md 98.3.10.1) is built in unless NOLIVESND=1 builds
; it out (-DVP_NOLIVESND): the A/B, and the way to ship without it
%ifndef VP_NOLIVESND
%define VP_LIVESND
%endif
PF_CGA4     equ 4                   ; ...CGA in colour: mode 4 (98.1.3.3)
PF_C160     equ 5                   ; ...and 160 x 100 x 16, the text hack
LAY_C160    equ 5                   ; ...its layout: the attributes, packed
PF_C512     equ 6                   ; ...and the hack's COMPOSITE colours,
LAY_TXT     equ 6                   ; on the text screen as it is (98.1.3.5)
PF_TEXT     equ 7                   ; ...and TEXT: the 80 x 25 text screen of
LAY_TEXT    equ 7                   ; any adapter, not retimed (98.1.3.6)
R_CGAPAL    equ 54                  ; CGA4: the palette byte (98.1.3.3)
LAY_LIN320  equ 3                   ; ...and [vp_layout] the layout less one
LAY_LIN80   equ 2
LAY_MODEX   equ 4
LAY_HERC    equ 1                   ; (HERC: page-ahead's second page, 98.3.8.3)
VP_HPAGE    equ 0x8000              ; ...at B800h, 3BFh bit 1 allowing it
VP_MXPL     equ 0x4B0               ; a MODEX plane image, 19,200 bytes, in
                                    ; paragraphs: plane p of a RAM copy is
                                    ; at + p x this (98.1.3.1)
VP_MXSHD    equ 121                 ; ...and the claim a keyframe decodes
                                    ; into: plane 3's base + 64 KB, so a
                                    ; 16-bit write from any plane stays in
                                    ; it (98.1.6)
R_SLEN      equ 24
R_KMAX      equ 30
; a keyframe table entry (SPEC.md 98.1.3), as vp_ke holds it
KE_K        equ 0
KE_OFF      equ 4
KE_LEN      equ 8
KE_SP       equ 10
KE_SECS     equ 14
KE_IDX      equ 15

; =============================================================================
vp_entry:
    call OSAPI_ARG_FILE             ; CF = 1: launched with no document
    jc .nodoc
    call vp_cpname                  ; ES:SI is the kernel's: copy it out now
    mov [vp_argdir], dx
    mov [vp_argvol], bl
    mov byte [vp_argpend], 1
.nodoc:
    OS88_ALTENTER_ARM               ; the key-state map, for Alt+Enter
    call OSAPI_CPU_INFO             ; a 286 loads keys as the thumb is
    mov [vp_tier], al               ; dragged (98.4.2)
    call vp_layfit                  ; THE WINDOW'S SIZE is the layout's, for a
                                    ; 640 x 200 video until one is open (the
                                    ; image's vp_wb, vp_pwb, vp_h and vp_ph)
    mov ax, [vp_lcw]
    inc ax
    inc ax
    mov [vp_tpl+4], ax
    mov ax, [vp_lch]
    add ax, TITLE_H + 1
    mov [vp_tpl+6], ax
    mov si, vp_tpl
    call OSAPI_WM_CREATE
    jnc .made                       ; (refused: CF = 1 back to the loader)
    ret
.made:
    mov [vp_win], bx
    push ax                         ; A GREY GROUND on a sixteen-colour
    push bx                         ; desktop (98.4.8): the window paints
    push cx                         ; every pixel of its content itself, so
    push dx                         ; the kernel's white fill in front of
    call vp_video                   ; W_PAINT is not a flash of white under
    cmp dh, 1                       ; the grey (SPEC.md 11.90.1). The DEPTH
    pop dx                          ; is the question, as the SDK says: VGA
    pop cx                          ; and EGA both
    pop bx
    jbe .ngrey
    mov al, 1
    mov [vp_grey], al
    call OSAPI_WM_OWNBG
.ngrey:
    pop ax
    mov al, 1                       ; A FIXED LAYOUT (SPEC.md 11.93): its
    call OSAPI_WM_KEEPH             ; height hangs over the dock rather than
                                    ; be cut - CGA's band is too short for the
                                    ; picture, the bar and the buttons (98.4.1)
    OS88_REGION_MOVABLE vp_noreloc  ; SPEC.md 66.6.1.1. A play pins it anyway:
                                    ; the bracket's own frame is on the stack
    push bx                         ; THE BUTTONS' GESTURE (SPEC.md 20.5.1.3):
    mov ax, bx                      ; press inverts, release fires, a slide
    mov bx, vp_btns                 ; off cancels
    mov si, vp_onup
    mov di, vp_ondrag
    mov dx, vp_onclick              ; ...a press on no button: the scrub bar
    call os88ui_btninit
    pop bx
    mov ax, vp_clickw               ; ...and the press reaches US first, so the
    call OSAPI_WM_ONCLICK           ; rects are where the window is NOW
    mov si, vp_menus
    call OSAPI_MENU_SET
    mov si, vp_about
    call OSAPI_ABOUT_SET
    mov ax, vp_onwake
    call OSAPI_WM_ONWAKE
    mov ax, vp_ontimer              ; the hold's loading (98.3.18); refused
    call OSAPI_WM_ONTIMER           ; on kern_small, which has no XMS anyway
    mov ax, vp_onclose              ; THE CLOSE ENDS THE SESSION FIRST
    call OSAPI_WM_ONCLOSE           ; (98.3.18.10)
    mov ax, vp_onresize             ; ...and a landing on another display
    call OSAPI_WM_ONRESIZE          ; asks again (98.3.21)
    call vp_fmt                     ; ([vp_msg]: the image's vp_s_none)
    call vp_wakew                   ; the document, and a settled first paint,
    mov bx, [vp_win]                ; from the wake (SPEC.md 54.10)
    clc
    ret

; W_ONCLICK: the rects where the window IS, then the library, which hands a
; press on no button to vp_onclick (SPEC.md 20.5.1.3.3). It stands HERE, in
; front of vp_onwake, for that one's address: a Live play's chunk loop runs
; in vp_onwake, and an odd block ahead of it keeps it at the parity it had
vp_clickw:
    call vp_abdismiss
    jc .out
    call vp_track
    call vp_clip
    jmp os88ui_btnclick
.out:
    ret

; --- W_ONWAKE: SI = the window --------------------------------------------------
; The document named at launch; then a layout owed (a file opened, the card
; toggled) - the picture made again if its scale moved, the caption made for
; the width to come (98.4.3), and the window resized to it. The resize is
; made UNDER our lock: the slot takes one itself now when a caller has none
; (SPEC.md 11.1.2), and a kernel from before that drew the grown window over
; the pointer with no hide promised
vp_onwake:
    push ax
    push bx
    push cx
    push dx
    cmp byte [vp_lwant], 0          ; A LIVE STREAM'S CHUNKS (98.3.18.1),
    je .nlw                         ; asked for by the worker: copied out of
    mov byte [vp_lwant], 0          ; the hold a chunk at a time, each under
.lf:                                ; the lock - the worker is a TASK, not
    call OSAPI_GFX_LOCK             ; the bracket's ISR hook, and pre-empted
    call vp_lfeed1                  ; between vp_nextw's advance and the
    sbb al, al                      ; decode its slot would be refilled under
    call OSAPI_GFX_UNLOCK           ; it; so it waits one copy at most
    or al, al
    jz .lf
    mov al, [vp_lend]               ; ...AND NOTHING ELSE: a wake that was
    or al, [vp_cpgo]                ; only the feed paints nothing. It fell
    or al, [vp_argpend]             ; through to vp_repaint, the whole
    or al, [vp_relay]               ; window a chunk at a time - and every
    jnz .nlw                        ; button under a window went blank with
    jmp .out                        ; it (98.3.18.1.1)
.nlw:
    call OSAPI_GFX_LOCK
    cmp byte [vp_lend], 0           ; a LIVE play's end, found by the worker:
    je .nle                         ; finished here, on the UI task
    mov byte [vp_lend], 0
    push si
    mov si, [vp_win]
    xor al, al
    call vp_stopfor
    pop si
.nle:
    mov al, [vp_cpgo]               ; A PLAY THAT POSTED A COMPACTION
    or al, al                       ; (98.3.19.1), started again now that
    jz .ncp                         ; it has run - with the room it made
    mov byte [vp_cpgo], 0
    mov word [vp_msg], vp_s_ready
    push si
    mov si, [vp_win]
    cmp al, 2
    je .cpf
    call vp_play
    jmp short .cpd
.cpf:
    call vp_fsenter
.cpd:
    pop si
.ncp:
    cmp byte [vp_argpend], 0
    je .lay
    mov byte [vp_argpend], 0
    mov dx, [vp_argdir]             ; where the document is (SPEC.md 54.5)
    mov bl, [vp_argvol]
    call OSAPI_FILE_GOTO_QM         ; ...and the INSTANCE with it (74.1):
    call vp_open                    ; GOTO_Q moved the machine alone, and the
                                    ; first file cell put it back in the
                                    ; folder VIDEO.O88 was loaded from - so a
                                    ; document anywhere else read as a disk
                                    ; error (the owner's C:\APPS and F:)
.lay:
    cmp byte [vp_relay], 0
    je .paint
    mov byte [vp_relay], 0
    call vp_layfit
    or ax, ax
    jz .size
    mov ax, [vp_dkey]               ; the picture at the new scale: the
    cmp ax, 0xFFFF                  ; session's frame, or a key's
    je .size
    cmp ax, 0xFFFE
    jne .key
    call vp_sesspic
    jmp short .size
.key:
    call vp_loadkey
    call vp_fmt
.size:
    mov cx, [vp_lcw]
    add cx, 2
    call vp_mkcap                   ; for the width it is about to have
    mov bx, [vp_win]
    call OSAPI_WM_GEOM              ; CX, DX = the content now
    cmp cx, [vp_lcw]
    jne .resize
    cmp dx, [vp_lch]
    jne .resize
    xor ax, ax                      ; the same size, maybe a new file: the
    call OSAPI_WM_TITLE             ; caption's strip and nothing else
.paint:
    call vp_repaint
    jmp short .unl
.resize:
    mov cx, [vp_lcw]
    add cx, 2
    mov dx, [vp_lch]
    add dx, TITLE_H + 1
    call OSAPI_WM_RESIZE            ; ...which draws the caption made above
    call OSAPI_WM_GEOM
    inc cx
    inc cx
    mov ax, [vp_lcw]
    inc ax
    inc ax
    cmp cx, ax
    jae .unl
    call vp_mkcap                   ; clamped below what we asked for: made
    xor ax, ax                      ; again for the width it got
    call OSAPI_WM_TITLE
.unl:
    call OSAPI_GFX_UNLOCK
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_mkcap - the caption for a window CX pixels wide, into vp_cap, which the
; record's W_TITLE names (SPEC.md 98.4.3): 'Video Player - <title>', else
; 'Video - <title>', else the title cut to fit - n characters fit when
; 8n <= CX - 56. The title is the header's, or the file's name without one
vp_mkcap:
    push ax
    push bx
    push cx
    push si
    push di
    mov ax, cx
    sub ax, 56
    jnc .w
    xor ax, ax
.w:
    mov cl, 3
    shr ax, cl                      ; AX = the characters the bar has room for
    mov di, vp_cap
    mov si, vp_ttl
    cmp byte [vp_loaded], 0
    je .last                        ; nothing open: the name alone
    mov si, vp_title
    cmp byte [si], 0
    jne .len
    mov si, vp_name
.len:
    mov bx, si
    mov cx, 15                      ; 'Video Player - '
.l:
    cmp byte [bx], 0
    je .ld
    inc bx
    inc cx
    jmp short .l
.ld:
    mov bx, vp_pfx1
    cmp cx, ax
    jbe .pre
    mov bx, vp_pfx2
    sub cx, 7                       ; 'Video - '
    cmp cx, ax
    ja .last
.pre:
    xchg si, bx
    call .cpy                       ; the prefix, then the title
    mov si, bx
.last:
    call .cpy
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
.cpy:                               ; SI to DI while AX lasts
    or ax, ax
    jz .cd
    mov cl, [si]
    or cl, cl
    jz .cd
    mov [di], cl
    inc si
    inc di
    dec ax
    jmp short .cpy
.cd:
    ret

; --- the menu, the keys and the buttons (SPEC.md 98.4) ---------------------------
; vp_reptog - Repeat on or off (98.3.9), and its button to match. It may
; change while a play runs: the reader and both cursors read it as they
; reach the file's end
vp_reptog:
    xor byte [vp_rep], 1
    call vp_track
    call vp_clip
    mov byte [vp_bone], 5           ; ITS button, not all seven: on an 8088
    jmp vp_buttons                  ; a row of framed pictures held a LIVE
                                    ; pass off two ticks (vidlivesndl)

; vp_cardtog - the info card out or in: a new layout, and the window resized
; to it, from the wake - OSAPI_WM_RESIZE may not be called under the lock
vp_cardtog:
    cmp byte [vp_lbin], 0           ; the buttons live in it: it stays
    jne .out
    xor byte [vp_card], 1
    mov byte [vp_relay], 1
    push bx
    call vp_wakew
    pop bx
.out:
    ret

vp_oncmd:                           ; AL = item, AH = menu, SI = window
    call vp_abdismiss
    cmp al, 1
    jb vp_opendlg
    je .play
    cmp al, 2
    je .fs
    cmp al, 5
    je vp_cardtog
    cmp al, 6
    je vp_reptog
    cmp al, 7
    je vp_mutetog
    sub al, 3                       ; 3, 4: the key before, the key after
    mov ax, -1
    je .s
    mov ax, 1
.s:
    jmp vp_step
.fs:
    jmp vp_fsenter
.play:
    jmp vp_play

vp_opendlg:
    mov al, 2                       ; a new file: the session goes
    call vp_stopfor
    mov al, FDLG_OPEN
    mov bx, [vp_win]
    mov di, vp_onfile
    xor si, si
    call OSAPI_FILE_DLG
    ret

vp_onkey:                           ; AL = ascii, AH = scan, SI = window
    push ax                         ; - each key the menu's item, run as the
    push bx                         ; menu runs it (vp_oncmd)
    call vp_abdismiss
    jc .out
    mov bl, 2                       ; Alt+Enter and F: full screen, PAUSED -
    cmp ax, KEY_ALTENTER            ; it plays when Space says so (98.3.6)
    je .cmd
    mov bl, 3                       ; Left and Right: the key before, after
    cmp ah, KSC_LEFT
    je .cmd
    inc bx
    cmp ah, KSC_RIGHT
    je .cmd
    cmp al, 27                      ; Esc: a waiting session stops, where it
    je .esc                         ; got to kept
    mov bl, 1                       ; Enter, Space or P plays
    cmp al, 13
    je .cmd
    cmp al, ' '
    je .cmd
    or al, 0x20
    cmp al, 'p'
    je .cmd
    inc bx
    cmp al, 'f'
    je .cmd
    mov bl, 6                       ; R: Repeat
    cmp al, 'r'
    je .cmd
    inc bx                          ; M or S: MUTE (98.3.17)
    cmp al, 's'
    je .cmd
    cmp al, 'm'
    je .cmd
    mov bl, 5                       ; I: the info card
    cmp al, 'i'
    jne .out
.cmd:
    mov al, bl
    call vp_oncmd
    jmp short .out
.esc:
    xor al, al
    call vp_stopfor
.out:
    pop bx
    pop ax
    ret

; a press on no button: on the scrub bar it takes the THUMB (98.4.2), which
; follows the pointer until the release picks the key under it
vp_onclick:                         ; CX = x, DX = y (screen), SI = window
    push ax
    push cx
    push dx
    cmp word [vp_nkeys], 0
    je .out
    mov ax, dx
    sub ax, [vp_cy0]
    sub ax, [vp_lbary]
    cmp ax, VP_BARH
    jae .out
    mov ax, cx
    sub ax, [vp_cx0]
    sub ax, VP_BOXX
    cmp ax, [vp_lbw]
    jae .out
    mov byte [vp_drag], 1
    call vp_dragx
    call vp_ticks
    mov [vp_dtk], ax
    call vp_pbar
.out:
    pop dx
    pop cx
    pop ax
    ret

; W_ONDRAG: the thumb follows the pointer; a button follows the press. On a
; 286 or better the picture follows too, a load at most every VP_DRAGT ticks
; (a key's load is ~1.3 s on an 8088, which a drag cannot wait for there -
; it loads on the release)
vp_ondrag:
    call vp_clip
    cmp byte [vp_drag], 0
    jne .thumb
    push bx
    mov bx, vp_btns
    call os88ui_btndrag
    pop bx
    ret
.thumb:
    push ax
    push dx
    mov dx, [vp_tpos]
    call vp_dragx                   ; AX = the key under it
    cmp dx, [vp_tpos]
    je .still
    call vp_pbar
.still:
    cmp byte [vp_tier], CPU_286
    jb .out
    cmp ax, [vp_sel]
    je .out
    push ax
    call vp_ticks
    sub ax, [vp_dtk]
    cmp ax, VP_DRAGT
    pop ax
    jb .out
    call vp_seekto                  ; (the thumb stays under the pointer)
    call vp_ticks                   ; ...and the interval runs from the load's
    mov [vp_dtk], ax                ; END, so a slow disk is not asked again
.out:                               ; the moment it answers
    pop dx
    pop ax
    ret

; vp_dragx - CX = the pointer's x: [vp_tpos] the thumb under it, AX = the key
; whose share of the bar that is. Preserves the rest
vp_dragx:
    push cx
    push dx
    mov ax, cx
    sub ax, [vp_cx0]
    sub ax, VP_BOXX                 ; along the bar, clamped to it
    jns .p
    xor ax, ax
.p:
    mov cx, [vp_lbw]
    dec cx
    cmp ax, cx
    jbe .q
    mov ax, cx
.q:
    push ax
    sub ax, VP_THW / 2              ; the thumb centred on it, inside the bar
    jns .t
    xor ax, ax
.t:
    sub cx, VP_THW - 1              ; (CX = [vp_lbw] - 1 above)
    cmp ax, cx
    jbe .u
    mov ax, cx
.u:
    mov [vp_tpos], ax
    pop ax
    mul word [vp_nkeys]
    div word [vp_lbw]
    pop dx
    pop cx
    ret

vp_onup:                            ; W_ONMOUSEUP: the button FIRES here
    push ax
    push bx
    push si
    call vp_track
    call vp_clip
    mov si, [vp_win]
    cmp byte [vp_drag], 0
    je .btn
    call vp_dragx                   ; THE THUMB'S RELEASE: the key under it
    mov byte [vp_drag], 0
    cmp ax, [vp_sel]
    jne .seek
    cmp byte [vp_sess], 0           ; ...the key picked, but a SESSION is
    je .snap                        ; somewhere else: the pick is news. A
.seek:                              ; looping play started from key 0 had no
    call vp_seekto                  ; way back to it (the owner's report)
    jmp short .out
.snap:
    call vp_pbar                    ; ...the one already picked: it snaps back
    jmp short .out
.btn:
    mov bx, vp_btns
    call os88ui_btnup               ; AX = the button, index + 1, or 0
    dec ax
    js .out
    xchg ax, bx                     ; ...run as its menu item (vp_oncmd)
    mov al, [vp_bcmd+bx]
    call vp_oncmd
.out:
    pop si
    pop bx
    pop ax
    ret

; vp_step - AX = -1 or +1: the key before or after the one picked
vp_step:
    push ax
    push ax                         ; a session steps from where it IS: the
    mov al, 1                       ; key at or before it, then this one
    call vp_stopfor
    pop ax
    add ax, [vp_sel]
    js .out
    call vp_seekto
.out:
    pop ax
    ret

; vp_seekto - AX = a keyframe: the play starts there (98.3.5), and the box
; shows it. Lock held
vp_seekto:
    push ax
    push bx
    cmp ax, [vp_nkeys]
    jae .out
    push ax
    mov al, 2                       ; a key picked outright: the session goes
    call vp_stopfor
    pop ax
    call vp_loadkey
    cmp ax, [vp_kload]              ; its entry is what the play needs; a
    jne .fail                       ; picture that would not fit is only black
    mov [vp_sel], ax
    jmp short .f
.fail:
    mov word [vp_msg], vp_s_kbad
.f:
    call vp_fmt
.paint:
    call vp_clipw
    jc .out
    call vp_track
    call vp_pposter
    call vp_pbar
    mov bx, 4                       ; the lines a key changes, and the buttons
    call vp_ptext
    call vp_buttons
.out:
    pop bx
    pop ax
    ret

vp_clip:
    push bx
    call vp_clipw
    pop bx
    ret

; --- the Open dialog's completion: ES:DI = the name, SI = the window ------------
vp_onfile:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, di
    call vp_cpname
    call vp_open                    ; the dialog left us standing in its
    pop di                          ; folder (SPEC.md 19.2.1)
    pop si
    call vp_wakew                   ; the layout and the paint: the wake's
    jmp vp_ld4

; vp_cpname - ES:SI = a file's name, NUL-terminated within 13: into vp_name.
; vp_cpn - ...or CX characters of it to DI, the NUL after them if it has
; none there (vp_parse's title). Clobbers AL, CX, SI, DI
vp_cpname:
    mov di, vp_name
    mov cx, 12
vp_cpn:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .cpd
    loop vp_cpn
    mov [di], cl                    ; (CX = 0)
.cpd:
    ret

; =============================================================================
; vp_open - read and check the header of [vp_name] (SPEC.md 98.1.6)
; out: [vp_ok] = 1 playable here; [vp_msg] says why not. Gfx lock held.
; =============================================================================
vp_open:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor ax, ax
    mov [vp_ok], al
    mov [vp_played], al
    mov [vp_lbk], al                ; (a resident play banks nothing)
    mov [vp_lygot], ax              ; (nor reads a layer)
    mov [vp_lymis], ax
    mov [vp_lyskp], ax
    mov [vp_loaded], al
%ifdef VP_DIAG
    mov word [vp_mfre0], 0          ; (no play of this file's to report: the
                                    ; imm16 form keeps VP_DIAG's play code at
                                    ; the parity the plain build's has)
%endif
    call vp_pfree                   ; the last file's poster, and its keys
    call vp_pevfree                 ; ...and its palettes a scene
    call vp_rfree                   ; ...and its loaded block
    call vp_xfree                   ; ...and its hold in XMS (98.3.18)
    push di
    mov di, vp_rdcur                ; ...and vp_rdat's cursor, which names no
    call os88_rseq_new              ; file and would trust the last one's
    pop di
    xor ax, ax
    mov [vp_nkeys], ax
    mov [vp_kekb], ax               ; a keyless file reserves no entry read
    mov [vp_nrend], al
    mov [vp_sel], ax
    dec ax
    mov [vp_kload], ax
    call OSAPI_FILE_DFREE           ; BX = sectors a cluster
    jc .io
    mov [vp_clsec], bx
    mov ax, bx
    mov cl, 9
    shl ax, cl
    mov [vp_clb], ax                ; ...and bytes
    mov ax, bx
    inc ax
    shr ax, 1                       ; ...in KB, for one cluster's claim
    call vp_mclaim
    jc .mem
    mov [vp_tmp], dx
    mov es, dx
    xor bx, bx
    mov cx, [vp_clb]                ; CX = a cluster in bytes
    xor ax, ax
    xor dx, dx
    mov si, vp_name
    call OSAPI_FILE_READ_AT         ; the header is the file's first sector
    jc .iofree
    or dx, dx
    jnz .have
    cmp ax, 512
    jb .short
.have:
    ; THE READ'S GRANULE (98.1.7.6): what vp_rdat reads in - a SECTOR where the
    ; kernel's READ_AT takes one (SPEC.md 18.4.4.2), else the cluster. Asked
    ; of a hard disk's volume alone, whose clusters are 2 KB and up: a key
    ; read in 32 KB clusters is the record and up to two of them, so a 60 KB
    ; key was past one 64 KB read and the file had no seek
    mov ax, [vp_clb]
    mov [vp_rgb], ax
    cmp ax, 1024
    jbe .gr
    mov bx, 512                     ; (the header's second sector, over the
    mov cx, bx                      ; copy of it the read above left there)
    mov ax, bx
    xor dx, dx
    call OSAPI_FILE_READ_AT         ; CF=1: a kernel that wants clusters
    jc .gr
    mov word [vp_rgb], 512
.gr:
    ; THE RENDITION (98.1.7), the best that plays here: 3 its layout the
    ; DESKTOP's own - the window with no shadow, and the full screen in its
    ; own mode; 2 a mode of its own on this display, full screen (a VGA has
    ; CGA's); 1 through the shadow. The first of the best, else the first -
    ; whose refusal is then the one the card gives
    call vp_dinfo
    xor ax, ax
    mov [vp_rbest], al
    mov [vp_rscore], al
    mov [vp_rend], al
.rl:
    call vp_parse
    jc .rn
    mov byte [vp_ok], 0
    call vp_canplay
    cmp byte [vp_ok], 1
    jne .rn
    mov ah, 1
    mov al, [vp_layout]             ; (a live rendition names its screen)
    cmp byte [vp_target], 0
    je .rt
    mov al, [vp_target]
    dec al
    cmp al, [vp_dlay]
    jne .rs
    mov ah, 3
    jmp short .rs
.rt:
    cmp byte [vp_shadow], 0
    jne .rs
    inc ah
    cmp al, [vp_dlay]
    jne .rs
    inc ah
.rs:
    cmp ah, [vp_rscore]
    jbe .rn
    mov [vp_rscore], ah
    mov al, [vp_rend]
    mov [vp_rbest], al
.rn:
    inc byte [vp_rend]
    mov al, [vp_rend]
    cmp al, [vp_nrend]
    jb .rl
    mov al, [vp_rbest]
.rch:
    mov [vp_rend], al
    mov byte [vp_ok], 0
    call vp_parse
    jc .nk
    call vp_rdpal                   ; VGA8's palette, and its luma
    jc .nk
    mov byte [vp_loaded], 1
    call vp_canplay
    call vp_xopen                   ; a streamed file into XMS, if it fits
    jmp short .free
.nk:
    mov word [vp_nkeys], 0          ; not loaded: no key to seek or show
.free:
    mov dx, [vp_tmp]
    call vp_mfree
.said:
    mov byte [vp_relay], 1          ; THE LAYOUT for this video (98.4.1),
    cmp byte [vp_ok], 0             ; owed to the wake - with the card out if
    jne .lf                         ; the file will not play here, since the
    mov byte [vp_card], 1           ; card is what says why. A file that could
.lf:                                ; not even be READ says why there too: its
    cmp byte [vp_loaded], 0         ; reason sat behind a closed card (the
    je .out                         ; owner's report, a .V88 on F:)
    call vp_layfit
    mov ax, [vp_poster]             ; THE POSTER (98.4): the header's keyframe,
    cmp ax, [vp_nkeys]              ; at the layout's scale - and only once the
    jae .out                        ; header's claim is gone, so the poster's
    mov byte [vp_thcp], 1           ; (the custom one, if there is: 98.1.3.11)
    call vp_loadkey                 ; sits under what is freed
    jmp short .out
.short:
    mov word [vp_msg], vp_s_notv88
    jmp short .free
.iofree:
    mov word [vp_msg], vp_s_io
    jmp short .free
.io:
    mov word [vp_msg], vp_s_io
    jmp short .said
.mem:
    mov word [vp_msg], vp_s_mem
    jmp short .said
.out:
    call vp_fmt
    jmp vp_ld7

; vp_hd32 - ES:SI = a dword of the header: into [BX], AX = its low word, and
; ZF = 1 when that is on a sector (511 clear) - vp_parse's offsets, stored
; before they are judged: a refusal leaves nothing of the file loaded
vp_hd32:
    mov ax, [es:si+2]
    mov [bx+2], ax
    mov ax, [es:si]
    mov [bx], ax
    test ax, 511
    ret

; vp_pbad - vp_parse's refusal, CF=1, where its first checks reach it
; with a short jump. vp_parse has no frame, so every refusal in it is
; stc/ret, and copies of it (.b0 to .b7) sit where the rest reach one
vp_pbad:
    stc
    ret

; vp_parse - ES:0 = the header. CF=1 with [vp_msg] set when it refuses
vp_parse:
    xor ax, ax
    mov [vp_nkeys], ax              ; no key in hand until the keyframes
    mov byte [vp_nkwhy], VOK_NOKEY  ; (and none in the file, until they are)
    mov word [vp_msg], vp_s_notv88  ; pass: a refusal anywhere leaves none
    cmp word [es:0], 'V8'
    jne vp_pbad
    cmp word [es:2], '8' + (0x1A << 8)
    jne vp_pbad
    mov word [vp_msg], vp_s_ver
    cmp word [es:4], 1              ; version 1, and no flag this player
    jne vp_pbad                        ; does not know (98.1.1.2)
    test word [es:V88_FLAGS], ~V88F_KNOWN & 0xFFFF
    jnz vp_pbad
    mov word [vp_msg], vp_s_bad
    test byte [es:V88_FLAGS+1], V88F_KPARTS >> 8    ; (parts: never in a
    jz .nkp                         ; resident block or a Live file, whose
    test byte [es:V88_FLAGS], V88F_RESIDENT | V88F_LIVE  ; keys are whole)
    jnz vp_pbad
.nkp:
    mov ax, 64                      ; A SUPER-PACKET's sectors at most, and
    mov cx, 1                       ; the ring's mirror slots: 64 and one -
    test byte [es:V88_FLAGS+1], V88F_BIGSP >> 8     ; or BIGSP's 127, which
    jz .nbig                        ; can touch three chunks, and two
    test byte [es:V88_FLAGS], V88F_RESIDENT | V88F_LIVE     ; (98.1.4.1)
    jnz vp_pbad                        ; (a stream's, played in the bracket)
    mov al, VP_SPBIG
    inc cx
.nbig:
    mov [vp_spcap], ax
    mov [vp_msl], cx
    cmp word [es:V88_FRAMES+2], 0   ; this player counts frames in a word
    jne .long
    mov ax, [es:V88_FRAMES]
    or ax, ax
    jz .b0
    mov [vp_frames], ax
    mov ax, [es:V88_RATE]
    or ax, ax
    jz .b0
    mov [vp_rate], ax
    mov ax, [es:V88_SPF]
    or ax, ax
    jz .b0
    mov [vp_spf], ax
    mov al, [es:V88_AUDIO]          ; none, PCM8 with a byte a sample, or
    cmp al, 2                       ; ADPCM4 with a byte two samples - which
    ja .b0                         ; needs an even count (SPEC.md 98.1.1)
    mov [vp_audio], al
    xor ah, ah                      ; SPEAKER COUNTS (98.1.1.3): PCM8's, the
    test byte [es:V88_FLAGS], V88F_SPKPWM   ; table already the encoder's
    jz .ncnt
    cmp al, 1
    jne .b0
    mov ah, al
.ncnt:
    mov [vp_spkpwm], ah
    mov byte [vp_spkp], 1           ; PULSES A SAMPLE (98.1.1.3.1): the
    test byte [es:V88_FLAGS], V88F_SPKMUL   ; counts made for N / pulses,
    jz .np1                         ; which only a speaker file carries
    or ah, ah
    jz .b0
    mov ah, [es:V88_SPKP]
    cmp ah, 2
    jb .b0
    cmp ah, 4
    ja .b0
    mov [vp_spkp], ah
    jmp short .np1
.long:
    mov word [vp_msg], vp_s_long
.b0:                                ; (.bad, again)
    stc
    ret
.np1:
    xor bx, bx
    or al, al
    jz .aud
    mov bx, [vp_spf]
    cmp al, 1
    je .aud
    shr bx, 1
    jc .b0
.aud:
    cmp bx, [es:V88_ABYTES]
    jne .b0
    mov [vp_abytes], bx
    mov al, [es:V88_NREND]
    dec al
    cmp al, 3
    ja .b0
    inc ax
    mov [vp_nrend], al
    cmp [vp_rend], al               ; THE RENDITION vp_open asked for, its
    jae .b0                        ; slot at DI (98.1.7)
    mov al, [vp_rend]
    mov ah, 64
    mul ah
    add ax, V88_REND
    mov di, ax
    mov ax, [es:V88_PITDIV]
    cmp ax, FSX_RATE_MIN
    jb .b0
    mov [vp_pitdiv], ax
    mov al, [es:V88_RING]           ; the ring the stream assumes: 0, or
    mov [vp_rneed], al              ; slots (2 to VP_KBIG, 98.1.1)
    mov al, [es:V88_PITPER]
    or al, al
    jz .b0
    mov [vp_pitper0], al
    mov al, [es:di+R_PIXFMT]
    dec al
    cmp al, PF_TEXT
    ja .b0
    mov [vp_pixfmt], al
    mov bl, [es:di+R_LAYOUT]  ; 1..8, and the canvas inside it
    dec bl
    cmp bl, LAY_TEXT
    ja .b0
    mov [vp_layout], bl
    xor cx, cx                      ; (CX is free until .lay's)
    mov [vp_palo], cx
    mov [vp_palo+2], cx
    mov [vp_pevn], cx
    mov word [vp_lstr], 80          ; (LIN80's own rows, but a NARROW screen's)
    mov [vp_screen], cl             ; A SCREEN OF ITS OWN (98.1.3.2.1): a
    mov [vp_mx200], cl              ; VGA4 file's, or MODEX's 320 x 200
    mov ah, [es:di+R_SCREEN]        ; (98.1.3.1.1), and never one a
    or ah, ah                       ; resident or a Live file plays on
    jz .scr0
    test byte [es:V88_FLAGS+1], V88F_SCREEN >> 8    ; (announced, so a
    jz .b1                          ; this one refuses it at open)
    cmp al, PF_VGA4
    je .scrv
    cmp bl, LAY_MODEX
    jne .b1
    cmp ah, 1
    jne .b1
    test byte [es:V88_FLAGS], V88F_RESIDENT | V88F_LIVE
    jnz .b1
    mov [vp_mx200], ah
    jmp short .scr0
.scrv:
    cmp ah, VP_NSCR
    jae .b1
    test byte [es:V88_FLAGS], V88F_RESIDENT | V88F_LIVE
    jnz .b1
    mov [vp_screen], ah
    cmp ah, VP_SCRN                 ; NARROW (98.1.3.2.5): its rows 40 apart,
    jb .scr0                        ; in the canvas's memory image as on the
    mov word [vp_lstr], 40          ; glass - vp_rowaddr's and every page's
.scr0:
    cmp al, PF_C512                 ; C512 (98.1.3.5): the text screen as
    je .c512                        ; it is, and its card byte
    cmp al, PF_TEXT                 ; TEXT (98.1.3.6): the text screen not
    je .text                        ; retimed, and its colour byte
    cmp al, PF_CGA4                 ; CGA IN COLOUR (98.1.3.3): mode 4 on
    jb .nc                          ; CGA's layout with its palette byte,
    ja .c16                         ; and the text hack on its own
    or bl, bl                       ; (LAY_CGA)
    jnz .b1
    mov ah, [es:di+R_CGAPAL]
    test ah, 0x80
    jnz .b1
    test ah, 0x40                   ; mode 5's palette names no other
    jz .cp
    test ah, 0x20
    jnz .b1
.cp:
    mov [vp_cgapal], ah
    jmp .lay
.b1:                                ; (.bad, again)
    stc
    ret
.c16:
    cmp bl, LAY_C160
    jne .b1
    jmp .lay
.text:
    mov cx, LAY_TEXT | (1 << 8)     ; TEXT: its colour byte, 0 mono, 1 colour
    jmp short .tx
.c512:
    mov cx, LAY_TXT | (2 << 8)      ; C512: 0 old, 1 new, 2 both - named, and
.tx:                                ; nothing else depends on it
    cmp bl, cl                      ; (CL the layout, CH the byte's most)
    jne .b1
    mov ah, [es:di+R_CGAPAL]
    cmp ah, ch
    ja .b1
    mov [vp_cgapal], ah
    test byte [es:di+R_WB], 3       ; whole cells, an even number of them
    jnz .b1                         ; (the poster's two a byte)
    jmp .lay
.nc:
    cmp al, PF_VGA4                 ; VGA4 is LIN80's planes (98.1.3.2)
    jne .v1
    cmp bl, LAY_LIN80
    jne .b2
    mov ax, [es:di+R_PAL]           ; ...and its OWN sixteen, or the EGA's
    or ax, [es:di+R_PAL+2]          ; (98.1.3.2.1) - on any screen, full
    jz .lay                         ; screen only (98.1.3.2.2), so never in
    test byte [es:V88_FLAGS], V88F_RESIDENT | V88F_LIVE ; a file that plays
    jnz .b2                         ; on the desktop
    call .pal                       ; ...on a sector
    jnz .b2
    call .pev                       ; ...and a palette a scene's table
    jc .b2
    jmp short .lay
.v1:
    cmp al, PF_VGA8                 ; VGA8 is LIN320's and MODEX's, and
    je .v8                          ; they take nothing else
    cmp bl, LAY_LIN320
    jae .b2
    jmp .lay
.b2:                                ; (.bad, again)
    stc
    ret
.pev:                               ; A PALETTE A SCENE (98.1.3.2.3): where its
    test byte [es:V88_FLAGS+1], V88F_PALEV >> 8     ; table is, read with
    jz .pev0                        ; the palette (vp_rdpal); a loop's seam
    mov ax, [es:di+R_PEVN]          ; takes frame L's (vp_frame .seam)
    or ax, ax
    jz .pevb
    cmp ax, VP_PEVMAX
    ja .pevb
    mov [vp_pevn], ax
    mov ax, [es:di+R_PEV]
    mov [vp_pevo], ax
    test ax, 511                    ; (on a sector)
    jnz .pevb
    mov ax, [es:di+R_PEV+2]
    mov [vp_pevo+2], ax
.pev0:
    clc
    ret
.pevb:
    stc
    ret
.pal:                               ; the palette's offset into vp_palo: AX
    push bx                         ; its low word, ZF = 1 on a sector
    lea si, [di+R_PAL]
    mov bx, vp_palo
    call vp_hd32
    pop bx
    ret
.v8:
    cmp bl, LAY_LIN320
    jb .b2
    cmp bl, LAY_MODEX               ; ...LIN320's and MODEX's, and no other
    ja .b2
    call .pal                       ; the palette, on a sector
    jnz .b2
    or ax, ax
    jz .b2
.lay:
    test byte [es:V88_FLAGS+1], V88F_PALEV >> 8     ; (a PALEV flag on a file
    jz .lay0                        ; that took no table: not a VGA4 one with
    cmp word [vp_pevn], 0           ; its own sixteen)
    je .b2
.lay0:
    call vp_lay6                    ; BX = layout x 6
    mov ax, [es:di+R_WB]
    or ax, ax
    jz .b2
    cmp ax, [vp_laytab+bx+2]        ; stride
    ja .b2
    mov [vp_wb], ax
    mov ax, [es:di+R_H]
    or ax, ax
    jz .b2
    cmp ax, [vp_laytab+bx+4]        ; rows
    ja .b3
    mov [vp_h], ax
    mov word [vp_page], VP_PAGE     ; (Mode X's page; a screen's below)
    cmp byte [vp_mx200], 0          ; (MODEX AT 320 x 200, 98.1.3.1.1: four
    je .m240                        ; pages of 16,000, the canvas in 200
    mov word [vp_page], VP_PAGE200  ; rows)
    cmp ax, 200
    ja .b3
.m240:
    cmp byte [vp_layout], LAY_HERC  ; (a Hercules's: page 1 at B800h)
    jne .hpg
    mov word [vp_page], VP_HPAGE
.hpg:
    mov bl, [vp_screen]             ; ...and a SCREEN's rows and plane bytes
    or bl, bl                       ; across: the canvas inside them, and
    jz .scr1                        ; two pages of its rows in a plane
    xor bh, bh
    shl bx, 1
    shl bx, 1
    cmp ax, [vp_scrtab+bx]
    ja .b3
    mov ax, [vp_scrtab+bx]
    mov [vp_scrows], ax
    mul word [vp_lstr]
    mov [vp_page], ax
    mov ax, [vp_scrtab+bx+2]
    mov [vp_scwb], ax
    cmp ax, [vp_wb]
    jb .b3
.scr1:
    mov ax, [vp_h]
    ; PLANES (98.1.3.1, 98.1.3.2): Mode X's four byte-planes, or VGA4's four
    ; bit-planes, and how far apart a RAM image keeps them
    xor bx, bx                      ; (BL the kind, from here to .pl3)
    cmp byte [vp_layout], LAY_MODEX
    jne .pl2
    inc bx
    mov word [vp_plsp], VP_MXPL
    jmp short .pl3
.b3:                                ; (.bad, again)
    stc
    ret
.pl2:
    cmp byte [vp_pixfmt], PF_VGA4
    jne .pl3
    mov bl, 2
    mul word [vp_lstr]              ; h rows of 80 bytes (40), in paragraphs
    add ax, 15
    mov cl, 4
    shr ax, cl
    mov [vp_plsp], ax
.pl3:
    mov [vp_planar], bl
    xor ax, ax                      ; SUB2 (98.1.3.2.4): a planar rendition's
    test byte [es:V88_FLAGS+1], V88F_SUB2 >> 8      ; alone - a one-bit
    jz .s2n                         ; one's record is one set of lists, and
    or bl, bl                       ; a Live file carries both kinds under
    jz .s2n                         ; the one header
    inc ax
.s2n:
    mov [vp_sub2], al
    mov dl, [vp_pixfmt]             ; (the format, from here to .pgeo)
    ; THE ROW SCALE (98.2.4): each row shown twice by the CRTC, VGA8 only
    xor ax, ax
    mov [vp_rs], al
    mov [vp_flip], al               ; (PAGE FLIPPING's, below: none yet)
    mov [vp_npg], al
    mov [vp_fpg], al
    mov [vp_own], al
    mov al, [es:di+R_RSCALE]
    cmp al, 1
    jbe .rs1
    cmp al, 2
    jne .b3
    cmp dl, PF_VGA8
    jne .b3
    mov byte [vp_rs], 1
.rs1:
    mov al, [es:di+R_FLIP]          ; PAGE FLIPPING (98.3.8): Mode X's own
    cmp al, 7                       ; (EACH RECORD AGAINST ITS OWN PAGE,
    jne .fp7o                       ; VIDEO-SEEN-PLAN 3b: byte 37 6's file
    test byte [es:V88_FLAGS], V88F_LOOPREC  ; whose pages are never brought
    jnz .b3                         ; up - so no loop's seam, which joins
    mov byte [vp_own], 1            ; the pages of two laps)
    dec ax
.fp7o:
    cmp al, 6                       ; (HELD RECORDS, 98.1.3.9: a flipped
    jne .fp6h                       ; file, vp_flipdec's to read)
    mov al, 2
.fp6h:
    cmp al, 5                       ; FLIPPED AND BUILT AHEAD (98.1.3.7.2):
    jne .fp7                        ; byte 37's 4 with every record flipped,
    cmp word [vp_lstr], 40          ; never on a NARROW screen
    je .b3
    mov byte [vp_fpg], 1
    mov al, 4
.fp7:
    cmp al, 4                       ; ON N PAGES (98.1.3.7.1): byte 37's 3
    jne .fp4                        ; from here on, with byte 39's N, 3..8,
    cmp byte [vp_layout], LAY_HERC  ; all in a plane's 64 KB - and never a
    je .b3                          ; Hercules's, which has two
    mov al, [es:di+R_PAGES]
    cmp al, 3
    jb .b3b
    cmp al, 8
    ja .b3b
    mov [vp_npg], al
    push dx
    cbw
    mul word [vp_page]              ; (N x a page: 64 KB at most - DX:AX
    or ax, ax                       ; 1:0000 the most)
    jnz .fp5
    dec dx
.fp5:
    or dx, dx
    pop dx
    jnz .b3b
    mov al, 3
.fp4:
    mov ah, [es:V88_FLAGS+1]        ; PAGE-AHEAD (98.1.3.7): byte 37 is 3
    and ah, V88F_PAGE >> 8          ; with the flag, and only with it
    cmp al, 3
    jne .fpn
    or ah, ah
    jz .b3b
    jmp short .fp2
.b3b:                               ; (.bad, again)
    stc
    ret
.fpn:
    or ah, ah
    jnz .b3b
    cmp al, 1
    jbe .fl1
    cmp al, 2
    jne .b3b
.fp2:
    cmp al, 3                       ; PAGE-AHEAD is a stream's, played in the
    jne .fp3                        ; bracket from a key: never RESIDENT, LIVE
    test byte [es:V88_FLAGS], V88F_RESIDENT | V88F_LOOPREC | V88F_LIVE
    jnz .b3b                        ; or looped (98.1.3.7) - its seam would
.fp3:                               ; copy into a keep page-ahead never claims
    cmp al, 2                       ; (FLIPPED every frame: never NARROW,
    jne .fp6                        ; whose pages are page-ahead's, 98.1.3.2.5)
    cmp word [vp_lstr], 40
    je .b3b
.fp6:
    cmp byte [vp_screen], 0         ; (a VGA4 file on a screen of its own:
    jne .fl0                        ; two pages in a plane, 98.1.3.2.1)
    cmp byte [vp_layout], LAY_MODEX
    je .fl0
    cmp al, 3                       ; ...or page-ahead on a HERCULES, its
    jne .b3b                        ; second page at B800h (98.3.8.3)
    cmp byte [vp_layout], LAY_HERC
    jne .b3b
.fl0:
    sub al, 1                       ; 1 flipped, 2 page-ahead
    mov [vp_flip], al
.fl1:
    mov ax, [vp_h]                  ; ...and the rows the picture SHOWS,
    mov cl, [vp_rs]                 ; which is what the Preview is made at
    shl ax, cl
    cmp dl, PF_TEXT                 ; (TEXT's poster is four rows a cell,
    jne .ph                         ; 98.4.6)
    shl ax, 1
    shl ax, 1
.ph:
    mov [vp_ph], ax
    ; THE PREVIEW'S WIDTH (98.4): a one-bit canvas's own, or for VGA8 the
    ; luma of its keyframe dithered to one bit, a byte per eight pixels -
    ; so what sizes and places the poster reads this and not [vp_wb]
    mov cx, [vp_wb]
    cmp dl, PF_CGA4                 ; CGA IN COLOUR: four pixels a byte, or
    jb .pnc                         ; two made two wide - a poster byte is
    shr cx, 1                       ; two canvas bytes (98.4.6)
    jc .b4
    cmp dl, PF_C512                 ; ...C512's, two ATTRIBUTES: four bytes
    je .p512                        ; - and TEXT's, two CELLS, four pixels
    cmp dl, PF_TEXT                 ; each
    jne .pgeo
.p512:
    shr cx, 1
    jc .b4
    jmp short .pgeo
.pnc:
    cmp dl, PF_VGA8
    jne .pgeo
    cmp byte [vp_layout], LAY_MODEX ; MODEX: a plane byte is four pixels,
    jne .pl320                      ; so two of them make the poster's one
    shr cx, 1
    jc .b4
    jmp short .pgeo
.pl320:
    test cl, 7                      ; ...eight pixels to its byte
    jnz .b4
    shr cx, 1
    shr cx, 1
    shr cx, 1
.pgeo:
    mov [vp_pwb], cx
    mov byte [vp_resid], 0
    test byte [es:V88_FLAGS], V88F_RESIDENT
    jz .strm
    call vp_pblock                  ; RESIDENT (98.1.7): its blocks, no chain
    jc .b4
    jmp short .keys
.b4:                                ; (.bad, again)
    stc
    ret
.strm:
    lea si, [di+R_SP0]              ; the first super-packet, on a sector
    mov bx, vp_sp0
    call vp_hd32
    jnz .b4
    mov ax, [es:di+R_SP0N]
    dec ax
    cmp ax, [vp_spcap]
    jae .b4
    inc ax
    mov [vp_sp0n], ax
    mov ax, [es:di+R_SPMAX]
    dec ax
    cmp ax, [vp_spcap]
    jae .b4
.keys:
    xor ax, ax                      ; SOUND AHEAD (98.1.8): A, the bytes of a
    mov [vp_ahead], al              ; lead, and where the start's is - never
    mov [vp_alb], ax                ; with RESIDENT, LIVE or no sound, and at
    mov [vp_klead], ax              ; most VP_ALMAX bytes of it - and where
    mov [vp_klead+2], ax            ; the keys' are, apart (98.1.8.1)
    mov [vp_fruns], al
    test byte [es:V88_FLAGS], V88F_LIVE ; LIVE (98.3.10): a resident file's -
    jz .nlv                         ; or a streamed one's, played Live once
    inc ax                          ; it is held in XMS (98.3.18.1) - and the
.nlv:                               ; screen each rendition is for
    mov [vp_flive], al
    test byte [es:V88_FLAGS], V88F_RUNS ; ...and its records' blit runs
    jz .nrn                         ; (98.1.3.4), a live file's alone
    or al, al
    jz .b5
    mov [vp_fruns], al
.nrn:
    mov al, [es:V88_AHEAD]          ; A, which only AHEAD has
    test byte [es:V88_FLAGS], V88F_AHEAD
    jnz .ah
    or al, al
    jnz .b5
    test byte [es:V88_FLAGS+1], V88F_KLEADS >> 8
    jnz .b5
    jmp short .nah
.b5:                                ; (.bad, again)
    stc
    ret
.ah:
    or al, al
    jz .b5
    mov ah, [vp_resid]
    or ah, [vp_flive]
    jnz .b5
    cmp byte [vp_audio], 0
    je .b5
    mov [vp_ahead], al
    push dx
    xor ah, ah
    mul word [vp_abytes]
    or dx, dx
    pop dx
    jnz .b5
    cmp ax, VP_ALMAX
    ja .b5
    mov [vp_alb], ax
    mov si, V88_LEAD0
    mov bx, vp_lead0
    call vp_hd32
    test byte [es:V88_FLAGS+1], V88F_KLEADS >> 8
    jz .nah
    mov si, V88_KLEADS              ; (a sector or more in: 0 is "inline")
    mov bx, vp_klead
    call vp_hd32
    cmp word [vp_klead+2], 0
    jne .nah
    cmp ax, 512
    jb .b5
.nah:
    mov al, [es:di+R_TARGET]
    cmp al, 3
    ja .b5
    mov [vp_target], al
    mov ax, [es:V88_XBANK]          ; THE BANK IT ASKS FOR (98.3.18.3): in
    mov [vp_hxb], ax                ; the header's zero tail, so a file made
    mov ax, [es:V88_XPRE]           ; before says nothing and a player made
    mov [vp_hxp], ax                ; before reads neither
    mov ax, [es:V88_YBASE]          ; THE BASE'S YARDSTICK (98.3.18.9): key
    mov [vp_kytb], ax               ; 0's decode on the machine it was made
                                    ; for, 10 us (0 none) - kept, as the
                                    ; layer's two are (vp_lywtab): vp_bnsel
                                    ; picks them or the bench's at a session
    mov ax, [es:di+R_BENCH]         ; THE BENCH (98.1.9.13): the player's
    mov [vp_bnb], ax                ; own record modelled on the machine the
    mov [vp_bnb2], ax               ; file was made for, and on the layer's;
    mov ax, [es:V88_BENCHL]         ; 0 none
    mov [vp_bnl], ax
    call vp_lyparse                 ; THE LAYER (98.1.9), or none
    call vp_thparse                 ; THE DESKTOP POSTERS (98.1.3.11), or none
    lea si, [di+R_SLEN]             ; the stream's bytes, for its KB/s
    mov bx, vp_slen
    call vp_hd32
    ; --- THE KEYFRAMES (98.1.3): a table on a sector, 16,383 at most, a
    ;     poster inside it. A record too big for one read turns the Preview
    ;     and the seek off, and the file still plays from the start
    mov ax, [es:di+R_NKEYS]         ; (vp_nkeys is 0 since the entry)
    cmp ax, 16383
    ja .b6
    mov bx, [es:di+R_POSTER]
    cmp bx, 0xFFFF
    je .pok
    cmp bx, ax
    jae .b6
.pok:
    mov [vp_poster], bx
    or ax, ax                       ; WHY THERE IS NO SEEK, if there is none:
                                    ; the file has no keys ([vp_nkwhy] is
                                    ; VOK_NOKEY since the entry)...
    jz .nokeys
    mov byte [vp_nkwhy], VOK_BIGKEY ; ...or has them, past one read here
    push ax
    lea si, [di+R_KTAB]             ; the table, on a sector
    mov bx, vp_ktab
    call vp_hd32
    pop ax
    jnz .b6
    mov bx, [es:di+R_KMAX]
    cmp bx, 7                       ; an empty planar key is 7 bytes
    jb .b6
    cmp bx, VP_KMAXREC
    ja .nokeys
    mov [vp_kmaxb], bx
    push ax                         ; its read: the record's clusters at the
    mov cx, bx                      ; worst offset, whole KB - ONE 64 KB claim
    call vp_spankb                  ; at most, which a ring's two slots hold.
    mov bx, ax                      ; A 32 KB cluster's is 64 KB for a record
    pop ax                          ; up to 32 KB and a byte
    cmp bx, 64
    ja .nokeys
    mov [vp_kbkb], bx
    push ax                         ; THE KEYS' LEADS APART (98.1.8.1): a
    mov ax, [vp_klead]              ; seek reads the lead into the ring past
    or ax, [vp_klead+2]             ; the key's read, so the two must fit
    jz .kl0                         ; the least ring - or the file plays
    mov cx, [vp_alb]                ; from the start and does not seek, as
    call vp_spankb                  ; a key past one read does
    add ax, [vp_kbkb]
    cmp ax, VP_KLRING
    jbe .kl0
    pop ax
    jmp .nokeys
.b6:                                ; (.bad, again)
    stc
    ret
.kl0:
    pop ax
    mov cx, 16                      ; ...and a TABLE ENTRY's, which is all
    push ax                         ; a seek's claim reads (vp_kent): the
    call vp_spankb                  ; ring keeps back this and not a record's
    mov [vp_kekb], ax               ; (SPEC.md 98.3)
    pop ax
    mov [vp_nkeys], ax
.nokeys:
    ; --- REPEAT (98.3.9): on if the file asks, and how a lap joins the next -
    ;     its seam record (1), keyframe 0 over a cleared canvas (2), or the
    ;     cleared canvas and the stream from its start (0)
    xor ax, ax
    test byte [es:V88_FLAGS], V88F_REPEAT
    jz .rp
    inc ax
.rp:
    mov [vp_rep], al
    mov [vp_lkind], ah
    cmp byte [vp_resid], 0          ; (resident with no seam: the block from
    jne .lk                         ; its start, on black - no key to read)
    cmp word [vp_nkeys], 0
    je .lk
    mov byte [vp_lkind], 2
.lk:
    mov si, V88_TITLE               ; the title, NUL-terminated within its 48
    mov di, vp_title
    mov cx, VP_COLS
    call vp_cpn
    test byte [es:V88_FLAGS], V88F_LOOPREC
    jz .lok
    mov word [vp_msg], vp_s_bad
    cmp word [es:V88_LOOP+LP_L+2], 0
    jne .b7
    mov ax, [es:V88_LOOP+LP_L]      ; L, and a frame after it to go on at
    inc ax
    jz .b7
    cmp ax, [vp_frames]
    jae .b7
    dec ax
    mov [vp_lL], ax
    cmp byte [vp_resid], 0          ; RESIDENT: the seam is each block's last
    je .lstr                        ; record, found when it is loaded
    mov byte [vp_lkind], 1
.lok:                               ; (.nolp, again: a short jump from the
    clc                             ; test above; the title is copied ahead
    ret                             ; of the loop block, which only refuses)
.b7:                                ; (.bad, again: a short jump from here)
    stc
    ret
.lstr:
    mov si, V88_LOOP+LP_OFF
    mov bx, vp_loff
    call vp_hd32
    mov si, V88_LOOP+LP_SP          ; (its super-packet on a sector)
    mov bx, vp_lsp
    call vp_hd32
    jnz .b7
    mov al, [es:V88_LOOP+LP_SECS]
    dec al
    cmp al, [vp_spcap]
    jae .b7
    inc ax
    mov [vp_lsecs], al
    mov al, [es:V88_LOOP+LP_IDX]
    mov [vp_lidx], al
    mov ax, [es:V88_LOOP+LP_LEN]    ; the seam: no shorter than an empty
    mov bx, 16                      ; frame...
    cmp byte [vp_planar], 0
    je .lmin
    mov bx, 7
.lmin:
    add bx, [vp_abytes]
    cmp ax, bx
    jb .bad
    mov [vp_llen], ax
    cmp byte [vp_own], 0            ; (none kept against its own page)
    jne .lfl
    cmp byte [vp_flip], 1           ; ...no longer than a flipped play's copy
    jne .lfl                        ; of the last record (98.3.8) - but a
    cmp word [vp_msl], 1            ; BIGSP file's longer one is copied off
    jne .lfl                        ; the glass instead (98.3.8.1)...
    cmp ax, VP_PREVKB * 1024
    ja .nolp
.lfl:
    mov cx, ax                      ; ...and one read: its clusters at the
    call vp_spankb                  ; worst offset, whole KB, as a keyframe's
    cmp ax, 64
    ja .nolp
    mov [vp_lkb], ax
    mov byte [vp_lkind], 1
.nolp:
    clc
    ret
.bad:
    stc
    ret

; vp_pblock - ES:0 = the header, DI = the rendition's slot: a RESIDENT
; file's blocks (98.1.7) - the chain's fields zero, the picture block no
; more than 60 KB packed and 128 KB less a paragraph unpacked, the audio
; block exactly the frames' audio (PCM8, or ADPCM4 - 98.1.7.2), packings the kernel names.
; CF=1 not sound
vp_pblock:
    push ax
    mov byte [vp_resid], 1
    mov ax, [es:di+R_SP0]
    or ax, [es:di+R_SP0+2]
    or ax, [es:di+R_SP0N]
    or ax, [es:di+R_SPMAX]
    jnz .bad
    push si
    lea si, [di+R_BLOCK]
    mov bx, vp_bk
    call vp_pbk
    pop si
    jc .bad
    cmp word [vp_bk+BK_UNPACKED], 16
    jb .bad
    xor ax, ax
    mov [vp_abk+BK_UNPACKED], ax
    mov [vp_abk+BK_UNPACKED+2], ax
    cmp byte [vp_audio], 0
    je .ok
    push si
    mov si, V88_AUDBLK
    mov bx, vp_abk
    call vp_pbk
    pop si
    jc .bad
    mov ax, [vp_frames]             ; ...and it is the frames' sound exactly
    mul word [vp_abytes]
    cmp ax, [vp_abk+BK_UNPACKED]
    jne .bad
    cmp dx, [vp_abk+BK_UNPACKED+2]
    jne .bad
.ok:
    pop ax
    clc
    ret
.bad:
    pop ax
    stc
    ret

; vp_pbk - ES:SI = a block's four fields, into [BX]: a packing of 0
; (stored), 1 (LZ4) or 2 (LZB). PACKED, it reads in under 60 KB and unpacks
; to under 128 KB (OSAPI_DECOMP's input is one segment); STORED, it is what
; it unpacks to and ANY size under 1 MB - vp_ldblk reads it in pieces, and
; the machine's memory is the bound (98.1.7.1). CF=1 not sound
vp_pbk:
    push ax
    push cx
    push di
    push ds
    push es
    push ds
    push es
    pop ds
    pop es
    mov di, bx
    mov cx, 13
    cld
    rep movsb
    pop es
    pop ds
    pop di
    mov al, [bx+BK_PACK]
    cmp al, 2
    ja .bad
    or al, al
    jnz .pk
    mov ax, [bx+BK_UNPACKED]        ; STORED: it is what it unpacks to...
    cmp ax, [bx+BK_PACKED]
    jne .bad
    mov ax, [bx+BK_UNPACKED+2]
    cmp ax, [bx+BK_PACKED+2]
    jne .bad
    cmp ax, 0x0F                    ; ...under 1 MB
    ja .bad
    jmp short .ok
.pk:
    cmp word [bx+BK_PACKED+2], 0
    jne .bad
    cmp word [bx+BK_PACKED], 61440
    ja .bad
    cmp word [bx+BK_UNPACKED+2], 1
    ja .bad
    jb .ok
    cmp word [bx+BK_UNPACKED], 0xFFF0
    ja .bad
.ok:
    pop cx
    pop ax
    clc
    ret
.bad:
    pop cx
    pop ax
    stc
    ret

; vp_canplay - can it play HERE, and if not here, ON ANOTHER DISPLAY (SPEC.md
; 98.3.21)? The window's display first, as every play before; refused there,
; each display of an extended desktop in turn, OSAPI_FSX_CAPS with BX = 1, 2,
; ... until it says there is none (SPEC.md 53.1.2) - the first that can is
; [vp_fsdisp], and the play goes full screen on it, the window staying where
; it is. Nothing can: the window's own refusal is the one shown.
; PLAYED HERE ONLY THROUGH THE SHADOW, a display that plays it in its OWN
; mode is better (98.3.21.1): the same walk, wanting no shadow - a CGA file
; on a Hercules goes to the CGA rather than through the copy - except a LIVE
; file, which is made for the desktop it is on
vp_canplay:
    mov byte [vp_fsdisp], 0
    mov bx, [vp_win]
    call vp_canp1
    mov ah, [vp_ok]                 ; AH = 1: it plays here, so the walk
    cmp ah, 1                       ; wants a display that plays it NATIVE
    jne .walk
    cmp byte [vp_shadow], 0         ; ...native here: nothing is better
    je .out
    cmp byte [vp_flive], 0          ; ...a Live file: the desktop's
    jne .out
    mov byte [vp_ok], 0
.walk:
    push word [vp_msg]              ; (the window's refusal, if none can)
    push ax
    mov bx, 1
.d:
    push bx
    call OSAPI_FSX_CAPS             ; CF=1: no display BX-1, the walk's end;
    mov [vp_fskind], dl             ; DL its kind, for what the card says
    pop bx
    jc .none
    push bx
    call vp_canp1
    pop bx
    pop ax
    push ax
    cmp byte [vp_ok], 1
    jne .next
    cmp ah, 1                       ; wanting it native: a shadow is no
    jne .got                        ; better than the window's own
    cmp byte [vp_shadow], 0
    je .got
    mov byte [vp_ok], 0
.next:
    inc bx
    jmp short .d
.got:
    mov [vp_fsdisp], bl             ; CH for OSAPI_FSX_RUN
    pop ax                          ; (the two banked words)
    pop ax
    mov bl, [vp_fskind]
    and bx, 3
    shl bx, 1
    mov ax, [vp_s_ontab+bx]         ; "Plays full screen on the CGA"
    mov [vp_msg], ax
    ret
.none:
    pop ax
    pop word [vp_msg]
    cmp ah, 1                       ; nothing plays it better: as it plays
    jne .out                        ; HERE, the window's answer made again
    mov bx, [vp_win]
    call vp_canp1
.out:
    ret

; vp_recheck - Play and F ask again (SPEC.md 98.3.21): the window may have
; been dragged to another display since the file was opened, and the answer
; is about the display. A changed answer is shown before the play goes on
vp_recheck:
    call vp_reask
    je .out
    push si
    call vp_fmt
    mov si, [vp_win]
    call vp_repaint
    pop si
.out:
    ret

; vp_onresize - W_ONRESIZE: the window landed on another display and its box
; changed with it (SPEC.md 39.16.3.3) - the same question asked again. It
; MUST NOT DRAW (SPEC.md 11.98): the card's lines are made here, and the
; repaint that follows draws them
vp_onresize:
    call vp_reask
    je .out
    call vp_fmt
.out:
    ret

; vp_reask - vp_canplay again, between sessions only: ZF=1 the answer is
; what it was (or nothing was asked)
vp_reask:
    cmp byte [vp_loaded], 0
    je .out
    cmp byte [vp_sess], 0
    jne .same
    cmp byte [vp_lsess], 0
    jne .same
    push ax
    push bx
    push cx
    push dx
    mov al, [vp_ok]
    mov ah, [vp_fsdisp]
    push ax
    push word [vp_mute]             ; THE MUTE IS NOT THE DISPLAY'S: vp_canp1
    mov byte [vp_ok], 0             ; runs vp_mdet, which re-derives it from
    call vp_canplay                 ; the machine and forgets an M pressed
    pop word [vp_mute]              ; since the load - Play then ran a file
    pop ax                          ; the user had unmuted SILENT (98.3.17).
    cmp al, [vp_ok]                 ; (vp_mwhy, the byte after, comes back
                                    ; with it: the same machine, the same why)
    jne .ch
    cmp ah, [vp_fsdisp]
.ch:
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret
.same:
    cmp al, al
    ret

; vp_canp1 - can THIS display play it (SPEC.md 98.3, 98.3.2)? BX = the window
; to ask about, or a display (OSAPI_FSX_CAPS' BX). In its own layout's mode
; if the display has it; else through the SHADOW, in the first other layout
; whose mode it has and whose screen holds the canvas
vp_canp1:
    call OSAPI_FSX_CAPS             ; AX = the modes that display has
    mov [vp_caps], ax
    xor cx, cx
    mov [vp_shadow], cl
    test al, 1 << FSXM_VGA12        ; WRITE MODE 3 (98.1.3.2.4) is a VGA's:
    jz .m3n                         ; an EGA has modes 0 to 2 and no 12h,
    inc cx                          ; and its colour sub-records are stored
.m3n:                               ; a plane at a time (vp_m3pl)
    mov [vp_m3ok], cl
    mov al, [vp_layout]
    mov ah, [vp_pixfmt]             ; (AH = the format: vp_try keeps it)
    cmp ah, PF_CGA4                 ; CGA IN COLOUR (98.3.12): its own
    jb .std                         ; modes, full screen
    je .c4
    cmp ah, PF_C512                 ; C512 (98.3.12.2): a CGA's COMPOSITE
    je .c512                        ; colours, which nothing else has
    cmp ah, PF_TEXT                 ; TEXT (98.3.16): any adapter's text
    je .text                        ; screen, in colour any but a mono one
    cmp dl, VID_CGA                 ; C160: a CGA's text mode retimed, or a
    je .c16                         ; VGA's - an EGA's 350 lines hold no
    cmp dl, VID_VGA                 ; hundred rows of the cell
    jne .cno
.c16:
    call vp_try                     ; TEXT80, and 80 x 100 holds the canvas
    jc .cno
    mov byte [vp_shadow], 1         ; ...always THROUGH THE SHADOW: the
.okj:                               ; screen's stride is 160, a byte of two
    jmp .ok
.c4:
    test byte [vp_caps], 1 << FSXM_CGA320
    jnz .okj
.cno:                               ; (a refusal is [vp_msg] and [vp_ok] 0:
    mov ax, vp_s_nocol              ; no caller reads the carry)
.say:
    mov [vp_msg], ax
    ret
.c512:
    cmp dl, VID_CGA
    jne .c5no
    call vp_try                     ; TEXT80, and the screen's own layout:
    jnc .ok                         ; NATIVE, no shadow (98.1.3.5)
.c5no:
    mov ax, vp_s_nocmp
    jmp short .say
.text:
    cmp byte [vp_cgapal], 0         ; COLOUR: an MDA or a Hercules draws
    je .t1                          ; the attributes as something else
    cmp dl, VID_HERC
    je .tno
.t1:
    call vp_try                     ; TEXT80, and the screen's own layout:
    jnc .ok                         ; NATIVE, no shadow
.tno:
    mov ax, vp_s_notxt
    jmp short .say
.std:
    mov bl, [vp_screen]             ; A SCREEN OF ITS OWN (98.1.3.2.1): its
    or bl, bl                       ; mode on a VGA, never the shadow -
    jz .std1                        ; 0Dh's for the 320-wide two, 12h's for
    mov cx, 1 << FSXM_VGA0D         ; the rest, each then retimed - and the
    cmp bl, 2                       ; NARROW two are 320 wide (98.1.3.2.5)
    jbe .sc
    cmp bl, VP_SCRN
    jae .sc
    mov cx, 1 << FSXM_VGA12
.sc:
    test [vp_caps], cx
    jnz .ok
    mov ax, vp_s_novga
    jmp short .say
.std1:
    call vp_try
    jnc .ok
    cmp ah, PF_VGA8                 ; colour has no one-bit screen to be
    jae .none                       ; copied onto
    cmp byte [vp_flip], 2           ; ...and page-ahead's second page is the
    je .none                        ; adapter's own (98.3.8.3)
    mov al, 2                       ; LIN80, HERC, CGA: the roomiest first
.l:
    cmp al, [vp_layout]
    je .n
    call vp_try
    jnc .shadow
.n:
    dec al
    jns .l
.none:
    mov bl, [vp_layout]             ; "made for <layout>": nothing here holds
    xor bh, bh                      ; it
    shl bx, 1
    mov ax, [vp_laynotab+bx]
    jmp short .say
.shadow:
    inc byte [vp_shadow]            ; (0 since the entry: 1)
.ok:
    mov [vp_tlay], al
    mov [vp_fslay], al              ; THE FULL SCREEN's, kept: a bracket in the
    mov bl, al                      ; window sets its own (98.3.7) - the mode
    call vp_lay6                    ; to take: BX = layout x 6
    mov al, [vp_laytab+bx]
    cmp byte [vp_pixfmt], PF_CGA4   ; (mode 4 is CGA's layout at two bits a
    jne .md                         ; pixel)
    mov al, FSXM_CGA320
.md:
    mov bl, [vp_screen]             ; (a screen of its own: 0Dh or 12h)
    or bl, bl
    jz .md1
    mov al, FSXM_VGA0D
    cmp bl, 2
    jbe .md1
    cmp bl, VP_SCRN
    jae .md1
    mov al, FSXM_VGA12
.md1:
    mov [vp_mode], al
    mov [vp_fsmode], al
    mov byte [vp_ok], 1
    call vp_mdet                    ; MUTED, if this machine may not play it
    mov al, [vp_shadow]
    mov [vp_fsshd], al
    mov word [vp_msg], vp_s_ready
    cmp byte [vp_shadow], 0
    je .out
    mov bl, [vp_layout]
    cmp byte [vp_target], 0         ; a RENDITION names the screen it was
    je .cp                          ; drawn for (98.1.7), and its layout is
    mov bl, [vp_target]             ; lin80's whichever that was - so the
    dec bl                          ; layout said "VGA" of OS8088.V88's CGA
    cmp bl, [vp_dlay]               ; one. Made for this screen, the copy is
    je .out                         ; the player's business and not news
.cp:
    xor bh, bh
    shl bx, 1
    mov ax, [vp_laycptab+bx]
    mov [vp_msg], ax
.out:
    ret

; vp_try - AL = a layout: CF=0 its mode is on this display and its screen
; holds the canvas. Preserves AL
vp_try:
    push ax
    mov bl, al
    call vp_lay6                    ; BX = layout x 6
    mov cl, [vp_laytab+bx]
    mov ax, [vp_caps]
    shr ax, cl
    test al, 1
    jz .no
    mov ax, [vp_wb]
    cmp ax, [vp_laytab+bx+2]        ; stride
    ja .no
    push cx
    mov ax, [vp_h]                  ; the rows it SHOWS: the canvas's, each
    mov cl, [vp_rs]                 ; shown twice by a row scale (not
    shl ax, cl                      ; [vp_ph], which is the POSTER's - four
    pop cx                          ; a row for TEXT)
    cmp ax, [vp_laytab+bx+4]        ; rows
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; vp_lay6 - BL = a layout: BX = it x 6, its vp_laytab row, and AX = the
; layout. Preserves the rest
vp_lay6:
    xor bh, bh
    mov al, 6
    mul bl
    xchg ax, bx
    ret

; vp_rowaddr - AX = a row, BL = a layout -> AX = the row's first byte in that
; layout's memory image (SPEC.md 98.1.2). Preserves CX, DX, SI, DI
vp_rowaddr:
    push cx
    push dx
    xor bh, bh
    mov cx, bx
    shl bx, 1
    add bx, cx
    shl bx, 1                       ; BX = layout * 6
    mov cl, [vp_laytab+bx+1]        ; banks: 1, 2 or 4
    xor ch, ch
    dec cx
    mov dx, ax
    and dx, cx                      ; DX = the bank
    inc cx
.s:
    shr cx, 1
    jz .sd
    shr ax, 1                       ; AX = the row within its bank
    jmp short .s
.sd:
    push dx
    mul word [vp_laytab+bx+2]       ; ...times the stride
    pop dx
    mov cl, 13
    shl dx, cl                      ; + the bank x 8192
    add ax, dx
    pop dx
    pop cx
    ret

; =============================================================================
; THE KEYFRAMES (SPEC.md 98.1.3, 98.4): a table entry, and its picture halved
; into the poster the window shows
; =============================================================================
; vp_loadkey - keyframe AX: its entry into vp_ke ([vp_kload] = AX when it is
; there), its record decoded into a 64 KB shadow and halved into the poster
; ([vp_pseg] = 0 when there is none). Lock held. Preserves all
vp_loadkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bx, ax
    call vp_pfree
    call vp_psize                   ; THE PICTURE, from the TOP: it is held
    call vp_mclhi                   ; while the window is open, and from the
                                    ; bottom it lay under a resident block,
                                    ; which could then never move down past
                                    ; it (98.1.7.4). No room for a picture:
    jc .nop                         ; the box is black and the key is picked
    mov [vp_pseg], dx               ; all the same - a play needs only its
                                    ; entry
.nop:
    mov ax, [vp_kbkb]               ; the record, a cluster either side
    call vp_mclaim
    jc .nobuf
    mov [vp_rdseg], dx
    mov ax, bx
    call vp_kent
    jc .nent
    cmp word [vp_pevseg], 0         ; A PALETTE A SCENE (98.1.3.2.3): the
    je .kx                          ; picture in its own scene's colours,
    mov ax, [vp_ke+KE_K]            ; each the nearest of the desktop's
    call vp_palat
    call vp_v4xlat
.kx:
    cmp word [vp_pseg], 0
    je .free
    cmp word [vp_thn], 0            ; A DESKTOP POSTER (98.1.3.11): the key's
    je .kp                          ; own, or at open the custom one - read
    mov ax, bx                      ; and blitted, not decoded. The custom
    test byte [vp_thfl], TH_CUSTOM  ; one stays for the same key made again
    jz .tk                          ; at another scale (the layout's)
    cmp byte [vp_thcp], 0
    jne .tc
    cmp byte [vp_dcust], 0
    je .tk
    cmp bx, [vp_dkey]
    jne .tk
.tc:
    mov ax, [vp_nkeys]
.tk:
    push ax
    push bx
    call vp_thpic
    pop bx
    pop ax
    jc .kp
    xor cl, cl                      ; (the custom one is drawn: kept so)
    cmp ax, [vp_nkeys]
    jne .tcd
    inc cx
.tcd:
    mov [vp_dcust], cl
    jmp short .made
.kp:
    mov byte [vp_dcust], 0
    push bx                         ; (the decode takes every register)
    call vp_kpic
    pop bx
    jc .nent
.made:
    mov [vp_dkey], bx
    mov ax, [vp_ps]
    mov [vp_pscale], ax
    call vp_pmov                    ; (movable once it is made: 98.3.19.2)
    jmp short .free
.nent:
    call vp_pfree
.free:
    call vp_rdfree
    jmp short .out
.nobuf:
    call vp_pfree
.out:
    mov byte [vp_thcp], 0
    jmp vp_ld8

; vp_thparse - ES:DI = the rendition's slot: its DESKTOP POSTERS (98.1.3.11)
; into vp_thtab..vp_thnk, and the Preview's least scale into [vp_thmin] - or
; [vp_thn] 0 for none, and for fields that are not this file's (a hostile
; file, or a stream in the first layout, whose 46..51 are not zero: its keys
; are decoded, as before). Preserves all
vp_thparse:
    push ax
    push bx
    push cx
    push si
    xor ax, ax
    mov [vp_thn], ax
    mov byte [vp_thmin], 1
    mov si, R_THUMBR
    test byte [es:V88_FLAGS], V88F_RESIDENT
    jnz .at
    mov si, R_THUMB
    mov ax, [es:di+R_THUMB+6]       ; (the first layout's twelve bytes)
    or ax, [es:di+R_THUMB+8]
    or ax, [es:di+R_THUMB+10]
    jnz .r
.at:
    add si, di
    mov al, [es:si+4]               ; the scale: 1, 2 or 4...
    mov cl, 0
    cmp al, 1
    je .sc
    inc cx
    cmp al, 2
    je .sc
    inc cx
    cmp al, 4
    jne .r
.sc:
    mov [vp_thsc], al
    mov [vp_thsh], cl
    mov bl, [es:si+5]               ; ...the flags
    test bl, 0xE0
    jnz .r
    mov [vp_thfl], bl
    test bl, TH_SIZE                ; THE PREVIEW'S SIZE: never larger
    jz .ns
    mov [vp_thmin], al
.ns:
    mov ah, 0                       ; ...the sets: one or two
    test bl, TH_COLOUR
    jz .m
    inc ah
.m:
    test bl, TH_MONO
    jz .k
    inc ah
.k:
    mov [vp_thnk], ah
    xor bh, bh                      ; ...the pictures: a key's each, and a
    and bl, TH_CUSTOM               ; custom one
    test byte [vp_thfl], TH_KEYS
    jz .np
    add bx, [es:di+R_NKEYS]
.np:
    or bx, bx
    jz .r
    cmp ah, 1
    jb .r
    je .n1
    shl bx, 1
.n1:
    mov ax, [vp_pwb]                ; the Preview's picture at the scale:
    shl ax, 1                       ; its own size's over it, the rows
    shl ax, 1                       ; rounding up
    shl ax, 1
    shr ax, cl
    mov [vp_thw], ax
    mov al, [vp_thsc]
    xor ah, ah
    dec ax
    add ax, [vp_ph]
    shr ax, cl
    mov [vp_thh], ax
    mov ax, [es:si]
    mov [vp_thtab], ax
    mov ax, [es:si+2]
    mov [vp_thtab+2], ax
    or ax, [vp_thtab]
    jz .r
    mov [vp_thn], bx
.r:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; vp_thpic - AX = a desktop poster (98.1.3.11) - a key, or [vp_nkeys] for the
; custom one - at [vp_ps] the stored scale, twice it or four times: its entry
; (the set this desktop shows best: black and white on one bit when there is
; one, colour on a VGA's) and bytes read through [vp_rdseg], unpacked into
; [vp_pseg] - or into a claim of their own and halved from there, once or
; twice (vp_dphalf, vp_half). CF=1 not made: a picture or a scale it does
; not have, a read that failed, or an entry that is not sound, and the key is
; then decoded as before. Clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_thpic:
    mov dl, [vp_thfl]
    cmp ax, [vp_nkeys]              ; THE PICTURE: a key's, or the custom one
    jb .key                         ; after them - or alone
    test dl, TH_CUSTOM
    jz .no0
    test dl, TH_KEYS
    jnz .ix
    xor ax, ax
    jmp short .ix
.key:
    test dl, TH_KEYS
    jnz .ix
.no0:
    jmp .no
.ix:
    cmp byte [vp_thnk], 1
    je .one
    shl ax, 1
.one:
    call vp_dinfo                   ; THE SET: on a VGA's desktop colour if
    xor bl, bl                      ; it is there, on one bit black and white
    cmp byte [vp_dlay], 2           ; if it is - else the other
    je .wc
    test dl, TH_MONO
    jz .kd
    inc bx
    test dl, TH_COLOUR
    jz .kd
    inc ax                          ; (each picture's colour entry first)
    jmp short .kd
.wc:
    test dl, TH_COLOUR
    jnz .kd
    inc bx
.kd:
    mov [vp_thkm], bl
    mov bl, [vp_thsc]               ; THE SCALE: the stored one, or a half or
    xor bh, bh                      ; a quarter of it
    xor cx, cx
.sc:
    cmp bx, [vp_ps]
    je .sok
    shl bx, 1
    inc cx
    cmp cl, 2
    jbe .sc
    jmp .no
.sok:
    mov [vp_thhv], cl
    mov dx, ax                      ; DX:AX = the entry's offset, 8 x AX
    mov cl, 3
    shl ax, cl
    mov cl, 13
    shr dx, cl
    add ax, [vp_thtab]
    adc dx, [vp_thtab+2]
    mov cx, 8
    call vp_rdat
    jc .no1
    push ds
    mov ds, [vp_rdseg]
    mov ax, [si]
    mov dx, [si+2]
    mov cx, [si+4]
    mov bx, [si+6]
    pop ds
    mov [vp_the], ax
    mov [vp_the+2], dx
    mov [vp_the+4], cx
    mov [vp_the+6], bx
    cmp bh, 0                       ; (its pad byte)
    jne .no1
    cmp bl, 1                       ; PK_NONE or PK_LZ4
    ja .no1
    call vp_thrb                    ; THE PICTURE'S BYTES: its rows at the
    mul word [vp_thh]               ; stored scale
    or dx, dx
    jnz .no1
    mov [vp_thu], ax
    or bl, bl                       ; (stored: exactly that)
    jnz .pk
    cmp cx, ax
    jne .no1
.pk:
    or cx, cx
    jnz .pk2
.no1:
    jmp .no
.pk2:
    call vp_spankb                  ; ...and the read inside the buffer - a
    cmp ax, [vp_kbkb]               ; bigger one than a key's record needs,
    jbe .rdok                       ; claimed before that one is let go
    call vp_mclaim
    jc .no1
    xchg dx, [vp_rdseg]
    call vp_mfree
.rdok:
    mov ax, [vp_pseg]               ; WHERE IT GOES: the poster, or a claim
    cmp byte [vp_thhv], 0           ; of its own to halve from
    je .dst
    mov ax, [vp_thu]
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call vp_mclaim
    jc .no1
    mov ax, dx
.dst:
    mov [vp_thseg], ax
    mov ax, [vp_the]
    mov dx, [vp_the+2]
    mov cx, [vp_the+4]
    call vp_rdat
    jc .fail
    mov ax, si                      ; (as a segment of its own)
    mov cl, 4
    shr ax, cl
    add ax, [vp_rdseg]
    and si, 15
    mov cx, [vp_the+4]
    mov es, [vp_thseg]
    xor di, di
    cmp byte [vp_the+6], 0
    jne .lz
    push ds
    mov ds, ax
    cld
    rep movsb
    pop ds
    jmp short .made
.lz:
    mov dx, [vp_thu]
    xor bx, bx
    push ds
    mov ds, ax
    mov al, 0                       ; OSAPI_LZ_LZ4
    call OSAPI_DECOMP
    pop ds
    jc .fail
.made:
    mov ax, [vp_thw]                ; THE BOX'S PICTURE: the size it is
    mov [vp_thow], ax               ; drawn at, the halvings followed
    mov ax, [vp_thh]
    mov [vp_thoh], ax
    cmp byte [vp_thkm], 0
    jne .mono
    cmp byte [vp_thhv], 0           ; COLOUR: halved by a pixel of each cell
    je .c4                          ; turning with its place - the first of
    mov ax, [vp_pseg]               ; two in place where it was read, since
    cmp byte [vp_thhv], 1           ; the poster's claim is the LAST one's
    je .c1                          ; size, then into the poster
    mov ax, [vp_thseg]
.c1:
    mov [vp_thdst], ax
    call vp_dphalf
    call vp_thhalf
    cmp byte [vp_thhv], 1
    je .cfree
    push word [vp_thw]
    push word [vp_thh]
    mov ax, [vp_thow]
    mov [vp_thw], ax
    mov ax, [vp_thoh]
    mov [vp_thh], ax
    mov ax, [vp_pseg]
    mov [vp_thdst], ax
    call vp_dphalf
    call vp_thhalf
    pop word [vp_thh]
    pop word [vp_thw]
.cfree:
    mov dx, [vp_thseg]
    call vp_mfree
.c4:
    mov ax, [vp_thow]               ; (OSAPI_GFX_BLIT4's rows)
    inc ax
    shr ax, 1
    mov [vp_pbw], ax
    mov byte [vp_p4], 1
    jmp short .dims
.mono:
    mov ax, [vp_thw]                ; BLACK AND WHITE: OSAPI_GFX_BLIT1's
    add ax, 7                       ; rows - halved as a decoded poster is
    shr ax, 1                       ; (vp_half), once into the poster and
    shr ax, 1                       ; once more in place there
    shr ax, 1
    mov [vp_pbw], ax
    mov byte [vp_p4], 0
    cmp byte [vp_thhv], 0
    je .dims
    mov [vh_wb], ax
    mov [vh_sstr], ax
    mov ax, [vp_thh]
    mov [vh_h], ax
    mov ax, [vp_thseg]
    mov [vh_sseg], ax
    cmp byte [vp_thhv], 1           ; (the first of two in place, as
    je .m1                          ; colour's)
    mov [vh_dseg], ax
    mov byte [vh_lay], 0xFF
    call vp_mkdtab
    call vp_half
    call vp_thhalf
    mov ax, [vh_wb]
    mov [vh_sstr], ax
.m1:
    mov ax, [vp_pseg]
    mov [vh_dseg], ax
    mov byte [vh_lay], 0xFF
    call vp_mkdtab
    call vp_half
    call vp_thhalf
.mfree:
    mov ax, [vh_wb]
    mov [vp_pbw], ax
    mov dx, [vp_thseg]
    call vp_mfree
.dims:
    mov ax, [vp_thoh]
    mov [vp_prows], ax
    mov ax, [vp_thow]
    mov [vp_ppx], ax
    mov word [vp_pskip], 0
    mov byte [vp_pthm], 1           ; (a stored picture, not the key's own)
    inc word [vp_ploads]
    clc
    ret
.fail:
    mov ax, [vp_thseg]
    cmp ax, [vp_pseg]
    je .no
    mov dx, ax
    call vp_mfree
.no:
    stc
    ret

; vp_thrb - AX = a stored desktop poster's bytes a row, in the set
; [vp_thkm] says: two pixels a byte, or eight. Preserves the rest
vp_thrb:
    mov ax, [vp_thw]
    cmp byte [vp_thkm], 0
    jne .m
    inc ax
    shr ax, 1
    ret
.m:
    add ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    ret

; vp_thhalf - [vp_thow] x [vp_thoh] halved as the halvings do: the columns
; down, the rows up. Preserves all
vp_thhalf:
    shr word [vp_thow], 1
    inc word [vp_thoh]
    shr word [vp_thoh], 1
    ret

; vp_dphalf - the desktop poster in [vp_thseg] ([vp_thw] x [vp_thh], packed)
; halved into [vp_thdst], which may be [vp_thseg] itself - every write lands
; at or behind every read left: one pixel of each 2 x 2 cell, its row the output
; column's parity and its column the row's - os88vid.py's packed_half.
; Clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_dphalf:
    mov ax, [vp_thw]
    inc ax
    shr ax, 1
    mov [vp_thsb], ax               ; the source's bytes a row
    mov es, [vp_thdst]
    xor di, di
    xor bp, bp                      ; BP = the output row
.row:
    mov ax, [vp_thh]
    inc ax
    shr ax, 1
    cmp bp, ax
    jae .done
    xor cx, cx                      ; CX = the output column
    mov byte [vp_v4n], 0
.pix:
    mov ax, [vp_thw]
    shr ax, 1
    cmp cx, ax
    jae .eol
    mov ax, bp                      ; the row: 2y + (x & 1), the last at most
    shl ax, 1
    mov bx, cx
    and bx, 1
    add ax, bx
    cmp ax, [vp_thh]
    jb .sy
    mov ax, [vp_thh]
    dec ax
.sy:
    mul word [vp_thsb]
    mov si, ax
    mov bx, cx                      ; the column: 2x + (y & 1)
    shl bx, 1
    mov ax, bp
    and ax, 1
    add bx, ax
    mov ax, bx
    shr ax, 1
    add si, ax
    push ds
    mov ds, [vp_thseg]
    mov al, [si]
    pop ds
    test bl, 1
    jnz .lo4
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo4:
    and al, 15
    test byte [vp_v4n], 1
    jnz .lo
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    mov [es:di], al
    jmp short .nx
.lo:
    or [es:di], al
    inc di
.nx:
    xor byte [vp_v4n], 1
    inc cx
    jmp short .pix
.eol:
    test byte [vp_v4n], 1
    jz .nr
    inc di
.nr:
    inc bp
    jmp .row
.done:
    ret

; vp_pevload - A PALETTE A SCENE (98.1.3.2.3): the table at [vp_pevo] into a
; claim of its own, [vp_pevseg], checked - entry 0 at frame 0 in the
; header's sixteen (vp_pal), the frames ascending and inside the file, every
; value six-bit. It is MOVABLE between brackets and pinned for one, with the
; session's claims (vp_smov): the hook reads it. CF=1 [vp_msg] says why.
; Clobbers AX, BX, CX, DX, SI, DI, ES
vp_pevload:
    mov ax, [vp_pevn]
    mov cx, PEV_LEN
    mul cx
    mov [vp_pevb], ax
    add ax, 1023                    ; its own claim, whole KB
    mov cl, 10
    shr ax, cl
    call vp_mclaim
    jc .mem
    mov [vp_pevseg], dx
    mov cx, [vp_pevb]               ; ...and the read's, a cluster either side
    call vp_spankb
    call vp_mclaim
    jc .mem1
    push word [vp_rdseg]
    mov [vp_rdseg], dx
    mov ax, [vp_pevo]
    mov dx, [vp_pevo+2]
    mov cx, [vp_pevb]
    call vp_rdat                    ; SI = where it landed (CX kept)
    jc .io
    mov es, [vp_pevseg]
    xor di, di
    push ds
    mov ds, [vp_rdseg]
    cld
    rep movsb
    pop ds
    call vp_rdfree                  ; (the read's claim, done with)
    pop word [vp_rdseg]
    jmp short .chk
.io:                                ; (the refusals, where the claims above
    call vp_rdfree                  ; reach them with a short jump)
    pop word [vp_rdseg]
    mov ax, vp_s_io
    jmp short .fail
.badv:
    pop cx
.bad:
    mov ax, vp_s_bad
    jmp short .fail
.mem1:
    mov ax, vp_s_mem
.fail:
    call vp_pevfree
.say:
    mov [vp_msg], ax
    stc
    ret
.mem:
    mov ax, vp_s_mem
    jmp short .say
.chk:
    mov es, [vp_pevseg]             ; THE CHECKS (98.1.6: a hostile file)
    xor si, si
    mov cx, [vp_pevn]
    xor dx, dx
.e:
    cmp word [es:si+2], 0           ; a frame this player counts in a word,
    jne .bad                        ; inside the file
    mov ax, [es:si]
    cmp ax, [vp_frames]
    jae .bad
    or si, si
    jnz .asc
    or ax, ax                       ; entry 0 at frame 0...
    jnz .bad
    jmp short .v0
.asc:
    cmp ax, dx                      ; ...and every other after the one before
    jbe .bad
.v0:
    mov dx, ax
    push cx
    mov cx, 48
    lea bx, [si+4]
.v:
    cmp byte [es:bx], 63
    ja .badv
    inc bx
    loop .v
    pop cx
    add si, PEV_LEN
    loop .e
    mov si, vp_pal                  ; ...and entry 0's sixteen the header's
    mov di, 4
    mov cl, 48                      ; (CX is 0 after the loop)
    repe cmpsb                      ; (DF clear since the copy)
    jne .bad
    mov [vp_pevp], cx               ; (0: every byte compared)
    mov dx, [vp_pevseg]
    mov ax, vp_smove
    call vp_mmov
    clc
    ret

; vp_pevfree - the palettes a scene's claim and VGA8's palette's (vp_rdpal),
; each if there is one: a file's, which the next one's open and a refused
; load give back. Preserves all
vp_pevfree:
    push si
    mov si, vp_pevseg
    call .f
    mov si, vp_p8seg
    call .f
    pop si
    ret
.f:
    push dx
    xor dx, dx
    xchg dx, [si]
    or dx, dx
    jz .out
    call vp_mfree
.out:
    pop dx
    ret

; vp_palat - AX = a frame: with a palette a scene (98.1.3.2.3), the sixteen
; the screen after it is shown in - the last entry at or before it - into
; vp_pal, and the next entry to load the first after it. Nothing without
; one. Preserves all
vp_palat:
    cmp word [vp_pevseg], 0
    je .ret
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov bx, ax
    mov es, [vp_pevseg]
    mov cx, [vp_pevn]
    xor si, si
    xor di, di
.l:
    cmp [es:si], bx
    ja .d
    mov di, si
    add si, PEV_LEN
    loop .l
.d:
    mov [vp_pevp], si
    lea si, [di+4]
    mov di, vp_pal
    mov cx, 48
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.ret:
    ret

; vp_palev - the frame about to be decoded, [vp_done]: when a scene starts
; there (98.1.3.2.3), its sixteen into vp_pal and the DAC BEFORE the record
; is drawn - the cut is drawn in its own colours, and for the moment the
; draw takes the last scene's picture is under them, which is the cut
; itself (VIDEO-PALETTE-PLAN 1). No wait for a retrace. Preserves all
vp_palev:
    cmp word [vp_pevseg], 0
    je .ret
    push ax
    push cx
    push dx
    push si
    push es
    mov si, [vp_pevp]
    cmp si, [vp_pevb]
    jae .out
    mov es, [vp_pevseg]
    mov ax, [vp_done]
    cmp [es:si], ax
    ja .out
    mov ax, [es:si]
    call vp_palat                   ; (that entry's sixteen, the cursor on)
    call vp_paldac
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop ax
.ret:
    ret

; vp_palevw - vp_palev for a frame just FLIPPED to (98.3.8): when a scene
; starts there, the retrace that latches the new page waited for first, so
; the sixteen go in with the page and not over the last scene's picture.
; Preserves all
vp_palevw:
    cmp word [vp_pevseg], 0
    je .ret
    push si
    push es
    mov si, [vp_pevp]
    cmp si, [vp_pevb]
    jae .no
    mov es, [vp_pevseg]
    push ax
    mov ax, [vp_done]
    cmp [es:si], ax
    pop ax
    ja .no
    call vp_vwait
    pop es
    pop si
    jmp vp_palev
.no:
    pop es
    pop si
.ret:
    ret

; vp_paldac - with a palette a scene, vp_pal's sixteen into the DAC: a
; scene's start, or a seek's, the screen black. Preserves all
vp_paldac:
    cmp word [vp_pevseg], 0
    je .ret
    push ax
    push cx
    push dx
    push si
    mov cx, 48
    call vp_dacw
    pop si
    pop dx
    pop cx
    pop ax
.ret:
    ret

; vp_pmov - the poster's claim MOVABLE (SPEC.md 98.3.19.2): it is read at
; each paint through [vp_pseg] and by nothing at interrupt time, so it is
; declared once it is made - not at its claim, which the decode's own
; claims follow. Preserves all
vp_pmov:
    push ax
    push dx
    mov dx, [vp_pseg]
    mov ax, vp_smove
    call vp_mmovz
    pop dx
    pop ax
    ret

; vp_pfree - the poster's claim, if there is one
vp_pfree:
    push dx
    mov dx, [vp_pseg]
    or dx, dx
    jz .out
    call vp_mfree
    mov word [vp_pseg], 0
.out:
    mov word [vp_dkey], 0xFFFF
    pop dx
    ret

; vp_kent - AX = a keyframe: its 16-byte entry into vp_ke, through
; [vp_rdseg], and checked. CF=1 it could not be read or is not sound
vp_kent:
    mov bx, ax
    mov dx, ax                      ; DX:AX = the entry's offset, 16 x AX
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    add ax, [vp_ktab]
    adc dx, [vp_ktab+2]
    mov cx, 16
    call vp_rdat
    jc .bad
    mov word [vp_kload], 0xFFFF     ; vp_ke is overwritten: no key's until
    push ds                         ; this one is checked
    pop es
    mov di, vp_ke
    mov ax, [vp_rdseg]
    push ds
    mov ds, ax
    mov cx, 8
    cld
    rep movsw
    pop ds
    cmp word [vp_ke+KE_K+2], 0      ; a frame of the file's...
    jne .bad
    mov ax, [vp_ke+KE_K]
    cmp ax, [vp_frames]
    jae .bad
    mov ax, [vp_ke+KE_LEN]          ; ...a record no longer than the header
    cmp ax, 7                       ; said the largest is (and no shorter
    jb .bad                         ; than an empty one: 7 bytes planar,
    cmp byte [vp_planar], 0         ; 16 one-bit - BX is the key, kept)...
    jne .kmin
    cmp ax, 16
    jb .bad
.kmin:
    cmp ax, [vp_kmaxb]
    ja .bad
    mov al, [vp_ke+KE_SECS]         ; ...and a super-packet after it
    cmp al, [vp_spcap]
    ja .bad
    test word [vp_ke+KE_SP], 511
    jnz .bad
    mov [vp_kload], bx
    clc
    ret
.bad:
    stc
    ret

; vp_kpic - the record vp_ke names, decoded from black into a 64 KB shadow
; (98.1.6: the claim bounds a write the lists were not checked for) and
; halved into [vp_pseg] - twice for a canvas bigger than CGA's. CF=1 not
vp_kpic:
    mov ax, 64
    cmp byte [vp_planar], 0
    je .kc
    call vp_plshd                   ; planes: the last one's base + 64 KB
.kc:
    call vp_mclaim
    jc .no
    mov [vp_kshd], dx
    mov es, dx
    call vp_zero
    call vp_kfirst                  ; the key's first read
    jc .free
.part:
    mov dx, [vp_rdseg]
    mov ax, si
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    call vp_khdr                    ; (98.1.3.8: another part after it?)
    jc .free
    add si, 6                       ; past len, y0, y1
    push ds
    mov ds, dx
    call vp_decram                  ; ES = the shadow, from its address 0
    pop ds
    cmp byte [vp_kmore], 0
    je .made
    call vp_knrd                    ; ...and the next, over it
    jc .free
    jmp short .part
.made:
    call vp_mkpic
    clc
.free:
    pushf
    mov dx, [vp_kshd]
    call vp_mfree
    popf
.no:
    ret

; vp_kfirst - the key in [vp_ke]: its FIRST READ (its record, or its first
; part, 98.1.3.8) into [vp_rdseg], SI where it landed, and [vp_kfo] and
; [vp_kpn] where it is and how long. CF=1 it could not be read
vp_kfirst:
    mov ax, [vp_ke+KE_OFF]
    mov dx, [vp_ke+KE_OFF+2]
    mov cx, [vp_ke+KE_LEN]
    mov [vp_kfo], ax
    mov [vp_kfo+2], dx
    mov [vp_kpn], cx
    jmp vp_rdat

; vp_khdr - DX:SI = a key's read just landed, normalised (98.1.3.8): a PART
; with another after it - its y0's bit 15 - has the bit taken off in memory,
; so it decodes as any record, [vp_kmore] 1, [vp_knext] its last two bytes,
; the next read's length, and [vp_kfo] moved past it to where that read
; starts. Else [vp_kmore] 0. CF=1 a part whose length is not its read's, or
; is less than a header and a list's (a hostile file: a part of 0 moved the
; walk on by nothing, and re-read itself forever). Preserves all
vp_khdr:
    push ax
    push bx
    push ds
    mov byte [cs:vp_kmore], 0
    mov ds, dx
    test byte [si+3], 0x80
    jz .r
    mov ax, [si]                    ; this part's length: its READ's, and
    cmp ax, [cs:vp_kpn]             ; REC_HDR + 3 at least, as os88vid.py's
    jne .bad                        ; reader asks (98.1.3.8)
    cmp ax, 9
    jb .bad
    and byte [si+3], 0x7F
    mov bx, si
    add bx, ax
    mov bx, [bx-2]                  ; ...and the next read's, its last word
    mov [cs:vp_knext], bx
    add [cs:vp_kfo], ax
    adc word [cs:vp_kfo+2], 0
    mov byte [cs:vp_kmore], 1
    clc
.r:
    pop ds
    pop bx
    pop ax
    ret
.bad:
    stc
    jmp short .r

; vp_knrd - the next part's read (vp_khdr found one) into [vp_rdseg], SI
; where it landed, [vp_kpn] its length - CF=1 if it could not be read, or
; is not one the claim a key's read takes can hold (a hostile file)
vp_knrd:
    mov cx, [vp_knext]
    cmp cx, 7
    jb .bad
    cmp cx, [vp_kmaxb]
    ja .bad
    mov [vp_kpn], cx
    mov ax, [vp_kfo]
    mov dx, [vp_kfo+2]
    jmp vp_rdat
.bad:
    stc
    ret

; vp_mkpic - the canvas in [vp_kshd] (the file's layout) into [vp_pseg] at
; the layout's scale: its own size, a half or a quarter (98.4)
vp_mkpic:
    push ax
    push cx
    push dx
    mov byte [vp_pthm], 0           ; (the key's own picture, 98.1.3.11)
    mov byte [vp_p4], 0
    cmp byte [vp_pixfmt], PF_VGA4   ; 16 COLOURS: packed for OSAPI_GFX_BLIT4,
    jne .n4                         ; every [vp_ps]-th pixel (98.4.5)
    inc byte [vp_p4]
    call vp_v4pack
    jmp .rows4
.n4:
    call vp_mkdtab                  ; (its tables share the box's save, made
                                    ; at each picture: VGA4's needs none)
    cmp byte [vp_pixfmt], PF_VGA8   ; 256 COLOURS: the luma dithered to one
    jb .mono                        ; bit, dense at its own size, and halved
    je .v8                          ; in place from there - and CGA's
    call vp_cmono                   ; colours the same way (98.4.6)
    jmp short .v8p
.v8:
    call vp_v8mono
.v8p:
    mov ax, [vp_pwb]
    mov [vh_sstr], ax
    mov [vh_wb], ax
    mov ax, [vp_ph]
    mov [vh_h], ax
    cmp word [vp_ps], 1
    je .own
    mov ax, [vp_pseg]
    mov [vh_sseg], ax
    mov [vh_dseg], ax
    mov byte [vh_lay], 0xFF
    call vp_half
    jmp short .h2                   ; (the second pass is the one-bit one's)
.mono:
    cmp word [vp_ps], 1             ; AT ITS OWN SIZE: the rows out of the
    jne .half                       ; file's layout, dense
    call vp_linear
.own:                               ; (a one-bit canvas's vp_pwb and vp_ph
    mov ax, [vp_pwb]                ; are its vp_wb and vp_h: vp_parse)
    mov [vp_pbw], ax
    mov cl, 3
    shl ax, cl
    mov [vp_ppx], ax
    mov ax, [vp_ph]
    jmp short .rows
.half:
    mov ax, [vp_kshd]               ; HALVED: out of the file's layout...
    mov [vh_sseg], ax
    mov al, [vp_layout]
    mov [vh_lay], al
    mov ax, [vp_wb]
    mov [vh_wb], ax
    mov ax, [vp_h]
    mov [vh_h], ax
    mov ax, [vp_pseg]
    mov [vh_dseg], ax
    call vp_half
.h2:
    mov ax, [vp_pwb]                ; ...its width half the canvas's
    shl ax, 1
    shl ax, 1
    cmp word [vp_ps], 2
    je .sized
    mov ax, [vp_pseg]               ; ...or a QUARTER, the second pass in
    mov [vh_sseg], ax               ; place: its rows are dense now
    mov byte [vh_lay], 0xFF
    mov ax, [vh_wb]
    mov [vh_sstr], ax
    call vp_half
    mov ax, [vp_pwb]
    shl ax, 1
.sized:
    mov [vp_ppx], ax
    mov ax, [vh_wb]
    mov [vp_pbw], ax
    mov ax, [vh_h]
.rows:
    mov [vp_prows], ax
.rows4:
    mov word [vp_pskip], 0
    inc word [vp_ploads]
    pop dx
    pop cx
    pop ax
    ret

; vp_sesspic - the session's frame (the keeper) in the box (98.3.7): what a
; bracket paused back to the desktop shows, and a new layout remakes
vp_sesspic:
    push ax
    push dx
    mov ax, [vp_pscale]
    cmp ax, [vp_ps]
    je .same
    cmp byte [vp_nokeep], 2         ; THE POSTER IS THE CANVAS (98.3.19.3):
    jne .rs                         ; at another scale it no longer can be,
    xor al, al                      ; so the session stops at the key at or
    call vp_stopfor                 ; before where it is, as a full screen
    jmp short .out                  ; without a keeper does
.rs:
    call vp_pfree                   ; a claim for another scale
.same:
    cmp word [vp_pseg], 0
    jne .have
    call vp_psize
    call vp_mclhi                   ; (from the top, as vp_loadkey's is)
    jc .out
    mov [vp_pseg], dx
.have:
    mov ax, [vp_keep]
    or ax, ax
    jnz .hk
    cmp byte [vp_pcv], 0            ; the poster HOLDS the frame (vp_pcget)
    je .out
    mov word [vp_dkey], 0xFFFE
    jmp short .out
.hk:
    mov [vp_kshd], ax
    call vp_mkpic
    call vp_pmov
    mov ax, [vp_ps]
    mov [vp_pscale], ax
    mov word [vp_dkey], 0xFFFE
.out:
    pop dx
    pop ax
    ret

; vp_psize - AX = the picture's claim at the layout's scale, KB: the canvas,
; or its first half (a quarter is the second pass, in place)
vp_psize:
    push cx
    push dx
    cmp byte [vp_pixfmt], PF_VGA4   ; packed nibbles at the scale
    jne .n4
    call vp_v4dims                  ; AX = bytes a row, CX = rows
    jmp short .sz
.n4:
    mov ax, [vp_pwb]
    mov cx, [vp_ph]
    cmp byte [vp_pixfmt], PF_VGA8   ; made whole, then halved in place (and
    jae .sz                         ; CGA's colours: 98.4.6)
    cmp word [vp_ps], 1
    je .sz
    inc ax
    shr ax, 1
    inc cx
    shr cx, 1
.sz:
    mul cx
    call vp_bkb
    cmp word [vp_thn], 0            ; ...or a DESKTOP POSTER's (98.1.3.11),
    je .r                           ; whichever is more: packed two pixels
    push ax                         ; a byte at the scale, and a byte more a
    mov ax, [vp_pwb]                ; row (BLIT1's halving rounds up)
    shl ax, 1
    shl ax, 1
    shl ax, 1
    xor dx, dx
    div word [vp_ps]
    inc ax
    shr ax, 1
    inc ax
    push ax
    mov ax, [vp_ph]
    add ax, [vp_ps]
    dec ax
    xor dx, dx
    div word [vp_ps]
    pop cx
    mul cx
    call vp_bkb
    pop cx
    cmp ax, cx
    jae .r
    mov ax, cx
.r:
    pop dx
    pop cx
    ret

; vp_bkb - DX:AX bytes -> AX KB, rounded up. Clobbers CX, DX
vp_bkb:
    add ax, 1023
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    ret

; vp_linear - the canvas out of the shadow ([vp_kshd], the file's layout)
; into [vp_pseg], a row after the other at [vp_wb] bytes
vp_linear:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov es, [vp_pseg]
    xor di, di
    xor dx, dx                      ; DX = the row
.r:
    cmp dx, [vp_h]
    jae .done
    mov ax, dx
    mov bl, [vp_layout]
    call vp_rowaddr
    mov si, ax
    mov cx, [vp_wb]
    mov ax, [vp_kshd]
    push ds
    mov ds, ax
    cld
    rep movsb
    pop ds
    inc dx
    jmp short .r
.done:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_cmono - a CGA4 or C160 keyframe in [vp_kshd] (the file's layout) into
; the one-bit poster in [vp_pseg], dense at [vp_pwb] a row (98.4.6): each
; pixel's colour's luma - the sixteen's, 0..16, as 98.4.4 makes them - lit
; when it beats the 4 x 4 Bayer cell over it. A CGA4 byte is four pixels,
; the leftmost in bits 7-6; a C160 byte two, the LEFT high, each made two
; wide so the picture keeps its shape. tools/os88vid.py's cga4_mono and
; c160_mono are the reference
vp_cmono:
    cmp byte [vp_pixfmt], PF_TEXT   ; TEXT's cells are glyphs: its own
    jne .c                          ; (vp_tmono)
    jmp vp_tmono
.c:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, 15                      ; the lumas: C160's sixteen as they are,
.lc:                                ; CGA4's four through its palette byte
    mov al, [vp_c16lum+bx]
    mov [vp_lum+bx], al
    dec bx
    jns .lc
    cmp byte [vp_pixfmt], PF_CGA4
    jne .go
    mov al, [vp_cgapal]
    mov bl, al                      ; value 0: the background
    and bx, 15
    mov ah, [vp_c16lum+bx]
    mov [vp_lum], ah
    mov bl, 6                       ; the set: mode 5's, or bit 5's
    test al, 0x40
    jnz .set
    mov bl, 3
    test al, 0x20
    jnz .set
    xor bl, bl
.set:
    xor bh, bh
    mov ah, 0                       ; ...and intensity, +8
    test al, 0x10
    jz .ni
    mov ah, 8
.ni:
    xor si, si
.fg:
    mov al, [vp_c4sets+bx]
    add al, ah
    push bx
    mov bl, al
    xor bh, bh
    mov al, [vp_c16lum+bx]
    pop bx
    mov [vp_lum+1+si], al
    inc bx
    inc si
    cmp si, 3
    jb .fg
.go:
    mov es, [vp_kshd]
    xor di, di                      ; DI = the output byte
    xor dx, dx                      ; DX = the row
.row:
    cmp dx, [vp_ph]
    jae .done
    mov ax, dx
    mov bl, [vp_layout]
    call vp_rowaddr
    mov si, ax                      ; ES:SI = the row's first byte
    call vp_rthrs                   ; the row's four thresholds
    mov cx, [vp_pwb]
.ob:
    push cx
    xor ah, ah                      ; AH = the byte, built from the left
    mov cx, 2                       ; ...out of two canvas bytes
.sb:
    push cx
    cmp byte [vp_pixfmt], PF_C512   ; C512: the ATTRIBUTE, past the cell's
    jne .sb1                        ; character (98.4.6)
    inc si
.sb1:
    mov bl, [es:si]
    inc si
    mov cx, 4                       ; four poster pixels each
.px:
    cmp byte [vp_pixfmt], PF_C160
    jae .p16
    rol bl, 1
    rol bl, 1
    mov al, bl
    and al, 3
    jmp short .lv
.p16:
    mov al, bl                      ; pixels 0 and 1 the high nibble, 2 and
    cmp cl, 2                       ; 3 the low
    jbe .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 15
.lv:
    push bx
    mov bx, vp_lum
    xlat
    mov bx, 4                       ; the column: a canvas byte starts on a
    sub bx, cx                      ; multiple of four poster pixels
    cmp [vp_rthr+bx], al            ; CF = threshold < luma: lit
    pop bx
    rcl ah, 1
    loop .px
    pop cx
    loop .sb
    push ds
    mov ds, [vp_pseg]
    mov [di], ah
    pop ds
    inc di
    pop cx
    loop .ob
    inc dx
    jmp .row
.done:
    jmp vp_ld7

; vp_tmono - TEXT's poster (98.4.6): every cell FOUR pixels wide and four
; rows tall, each quarter lit when its glyph quadrant's share of the way
; from the attribute's background luma to its foreground's beats the 4 x 4
; Bayer cell. The glyphs are the MACHINE's (OSAPI_FONT_GLYPHS) for the
; codes it has, and a table for the shades and blocks; anything else is
; half lit. tools/os88vid.py's text_mono is the reference. Preserves all.
;
; THE WORK IS PER CELL HALF, NOT PER POSTER ROW (98.4.6.1): a cell's four
; poster rows share two halves, and a half's two quadrant lumas are the
; same for both of its rows - only the thresholds differ. So each half is
; mixed ONCE into a row of lumas (vp_thalf) and each poster row is only the
; compare (vp_temit); the quadrant counts come off a table of every code,
; built once (vp_tquad, 512 calls). It was 2,700 cycles a cell a poster
; row, 4.6 s for an 80 x 25 canvas on a 5150, all of it in the three
; routines this replaced. The tables are a 2 KB scratch claim, freed at the
; end; refused, the poster is black
VP_TQ0      equ 0                   ; the scratch: half 0's quadrants, a word
VP_TQ1      equ 512                 ; a code (L, R); half 1's;
VP_TLUM     equ 1024                ; the sixteen lumas;
VP_TLB      equ 1040                ; a half-row's lumas, L and R a cell;
VP_TKEY     equ 1024 + 16 + 160     ; and the cell whose lumas were last
VP_TSCRKB   equ 2
vp_tmono:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, VP_TSCRKB
    call vp_mclaim
    jnc .have
    mov ax, [vp_pwb]                ; NO ROOM: the poster black
    mul word [vp_ph]
    mov cx, ax
    mov es, [vp_pseg]
    xor di, di
    xor al, al
    cld
    rep stosb
    jmp short .out
.have:
    mov [vp_tscr], dx
    call vp_tqbuild
    xor dx, dx                      ; DX = the poster's row
    xor di, di                      ; DI = its first byte, in [vp_pseg]
.row:
    cmp dx, [vp_ph]
    jae .fin
    test dl, 1                      ; a half's first row: its lumas
    jnz .emit
    call vp_thalf
.emit:
    call vp_temit
    inc dx
    jmp short .row
.fin:
    mov dx, [vp_tscr]
    call vp_mfree
.out:
    jmp vp_ld8

; vp_tqbuild - the scratch's tables: every code's two halves' quadrant
; counts (vp_tquad, the machine's glyph or the blocks' table), and the
; sixteen lumas. Clobbers AX, BX, CX, SI, DI, ES
vp_tqbuild:
    push dx
    mov byte [vp_tqok], 0
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL..AH, CX bytes a glyph
    cmp cx, 8
    jne .nf
    mov [vp_tqg], dx
    mov [vp_tqo], si
    mov [vp_tqf], ax                ; (the first, and the last)
    mov byte [vp_tqok], 1
.nf:
    mov es, [vp_tscr]
    cld
    mov si, vp_c16lum
    mov di, VP_TLUM
    mov cx, 8
    rep movsw
    xor di, di                      ; every code half lit, as vp_tquad says
    mov ax, 0x0808                  ; of a code it knows nothing of...
    mov cx, 512
    rep stosw
    mov si, vp_tquadt               ; ...the blocks' table over that...
    mov cx, VP_TQN
.t:
    mov bl, [si]
    xor bh, bh
    shl bx, 1
    mov ax, [si+1]
    mov [es:VP_TQ0+bx], ax
    mov ax, [si+3]
    mov [es:VP_TQ1+bx], ax
    add si, 5
    loop .t
    cmp byte [vp_tqok], 0           ; ...and the machine's glyphs over both,
    je .d                           ; the order vp_tquad asks in
    mov bl, [vp_tqf]
.c:
    xor bh, bh
    mov di, bx
    shl di, 1
    xor al, al
    call vp_tquad
    mov [es:VP_TQ0+di], cx
    mov al, 1
    call vp_tquad
    mov [es:VP_TQ1+di], cx
    cmp bl, [vp_tql]
    je .d
    inc bl
    jmp short .c
.d:
    pop dx
    ret

; vp_thalf - DX = a poster row, the first of a cell half's two: that half's
; two quadrant lumas for every cell of the row, into the scratch's VP_TLB -
; (bg x (16 - n) + fg x n) >> 4, as bg + ((fg - bg) x n >> 4) or fg + ((bg
; - fg) x (16 - n) >> 4), whichever difference is not negative, which is
; the same number exactly. Preserves DX, DI
vp_thalf:
    push dx
    push di
    mov ax, dx
    shr ax, 1
    shr ax, 1
    mov bl, LAY_TEXT
    call vp_rowaddr
    mov si, ax                      ; SI = the cell row, in the canvas
    mov bp, VP_TQ0
    test dl, 2
    jz .h0
    mov bp, VP_TQ1
.h0:
    mov cx, [vp_pwb]
    shl cx, 1                       ; CX = the cells
    mov es, [vp_tscr]
    mov di, VP_TLB
    push ds
    mov ds, [vp_kshd]
    cld
    mov ax, [si]                    ; (a key the first cell is not)
    not ax
    mov [es:VP_TKEY], ax
    jmp short .c
.nx:
    loop .c                         ; (the body is past a short loop's reach)
    jmp .dd
.c:
    lodsw                           ; AL = the character, AH its attribute
    cmp ax, [es:VP_TKEY]            ; THE CELL BEFORE AGAIN - a flat area's
    jne .new                        ; run of spaces or blocks: its lumas
    mov ax, [es:di-2]
    stosw
    jmp short .nx
.new:
    mov [es:VP_TKEY], ax
    mov bl, ah
    and bx, 15
    mov dl, [es:VP_TLUM+bx]         ; DL = the foreground's luma
    mov bl, ah
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov dh, [es:VP_TLUM+bx]         ; DH = the background's
    mov bl, al
    xor bh, bh
    shl bx, 1
    add bx, bp
    mov bx, [es:bx]                 ; BL, BH = the quadrants' lit dots
    cmp dl, dh
    jb .dn
    sub dl, dh                      ; fg >= bg: bg + (fg - bg) x n >> 4
    mov al, bl
    mul dl
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add al, dh
    stosb
    mov al, bh
    mul dl
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add al, dh
    stosb
    jmp .nx
.dn:
    sub dh, dl                      ; fg < bg: fg + (bg - fg) x (16 - n) >> 4
    mov al, 16
    sub al, bl
    mul dh
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add al, dl
    stosb
    mov al, 16
    sub al, bh
    mul dh
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add al, dl
    stosb
    jmp .nx
.dd:
    pop ds
    pop di
    pop dx
    ret

; vp_temit - DX = a poster row: its bytes from VP_TLB's lumas against the
; row's four Bayer thresholds, two cells a byte, into [vp_pseg] at DI - and
; DI past them. Preserves DX
vp_temit:
    push dx
    mov bx, dx
    and bx, 3
    shl bx, 1
    shl bx, 1
    mov dx, [vp_bayer4+bx+2]        ; DL, DH = the right quadrant's pixels'
    mov bx, [vp_bayer4+bx]          ; BL, BH = the left's
    mov cx, [vp_pwb]
    mov es, [vp_pseg]
    push ds
    mov ds, [vp_tscr]
    mov si, VP_TLB
    cld
.b:
    lodsw                           ; the first cell: AL its left luma, AH
    cmp bl, al                      ; its right; CF = threshold < luma: lit
    rcl bp, 1
    cmp bh, al
    rcl bp, 1
    cmp dl, ah
    rcl bp, 1
    cmp dh, ah
    rcl bp, 1
    lodsw                           ; ...and the second
    cmp bl, al
    rcl bp, 1
    cmp bh, al
    rcl bp, 1
    cmp dl, ah
    rcl bp, 1
    cmp dh, ah
    rcl bp, 1
    xchg ax, bp
    stosb
    loop .b
    pop ds
    pop dx
    ret

; vp_tquad - BL = a character, AL = 0 its top half, 1 its bottom -> CL, CH
; = the lit dots of that half's left and right 4 x 4 quadrants, 0..16.
; Preserves all but CX
vp_tquad:
    push ax
    push bx
    push dx
    push si
    push es
    cmp byte [vp_tqok], 0           ; the machine's glyph, if it has one
    je .tab
    cmp bl, [vp_tqf]
    jb .tab
    cmp bl, [vp_tql]
    ja .tab
    mov dl, al
    mov al, bl
    sub al, [vp_tqf]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    or dl, dl
    jz .q0
    add ax, 4
.q0:
    add ax, [vp_tqo]
    mov si, ax
    mov es, [vp_tqg]
    xor cx, cx
    mov dh, 4
.qr:
    mov al, [es:si]                 ; a row's two nibbles, counted
    inc si
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    add cl, [vp_nib+bx]
    mov bl, al
    and bl, 15
    add ch, [vp_nib+bx]
    dec dh
    jnz .qr
    jmp short .out
.tab:
    mov si, vp_tquadt               ; ...else the shades' and blocks' table
    mov dh, VP_TQN
.tl:
    cmp bl, [si]
    je .tf
    add si, 5
    dec dh
    jnz .tl
    mov cx, 0x0808                  ; anything else: half lit
    jmp short .out
.tf:
    mov cx, [si+1]
    or al, al
    jz .out
    mov cx, [si+3]
.out:
    pop es
    pop si
    pop dx
    pop bx
    pop ax
    ret

; vp_cgaset - CGA's colours, once the bracket has set its mode (98.3.12).
; CGA4: the palette byte through int 10h AH=0Bh - background and intensity,
; then the set - and mode 5's third set, which a CGA gets from 3D8h's
; black-and-white bit and an EGA or VGA from its palette register 2 made
; red. C160: the text mode retimed to a hundred rows of two scan lines on a
; CGA (SPEC.md 88.15.2's six writes) or of four on a VGA, blink off so the
; background is sixteen colours, the cursor off, and every cell the right
; half block, 0DEh, on black. Preserves all
vp_cgaset:
    cmp byte [vp_pixfmt], PF_CGA4
    jae .go
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vp_video                   ; DL = the adapter
    mov dh, dl
    mov al, [vp_pixfmt]
    cmp al, PF_C160
    jb .c4                          ; (CGA4)
    je .c16
    cmp al, PF_TEXT
    je .text
                                    ; C512 (98.3.12.2), a CGA's alone: C160's
    call vp_c16tim                  ; retime, every cell black, and the
                                    ; COLOUR BURST - video off meanwhile
    xor ax, ax                      ; a keyframe's canvas of zeroes is black:
    call vp_cfill8                  ; character 0 on attribute 0
    mov dx, 0x3D8                   ; 80 columns, video on, blink off, and the
    mov al, 0x09                    ; black-and-white bit clear: burst on
    out dx, al
    inc dx                          ; 3D9h: border 6 - IBM's CGA makes the
    mov al, 0x06                    ; 80-column burst from the border's
    out dx, al                      ; output, and a black one has none
    jmp short .out

.text:                              ; TEXT (98.3.16): the mode as the bracket
    mov bl, dh                      ; set it, the cursor off - the 6845's
    mov dx, 0x3D4                   ; register 0Ah on the card's own port -
    cmp bl, VID_HERC                ; and every cell 0 on 0, black. Colour
    jne .tcur                       ; (not on a mono card, vp_canplay) turns
    mov dl, 0xB4                    ; blink off, for sixteen backgrounds.
.tcur:                              ; (BL is the adapter: DX is the ports')
    mov ax, 0x200A
    out dx, ax
    xor ax, ax
    mov cx, 2000
    call vp_cfill
    cmp byte [vp_cgapal], 0
    je .out
    cmp bl, VID_CGA
    je .blk                         ; (C160's: blink off, a black border)
    mov ax, 0x1003                  ; an EGA's or a VGA's blink off: the
    xor bx, bx                      ; BIOS (as C160's is)
    int 0x10
    jmp short .out

.c16:
    cmp dh, VID_CGA
    jne .v16
    call vp_c16tim                  ; video off while the 6845 is retimed
    call vp_c16fill
.blk:
    mov dx, 0x3D8                   ; 80 columns, video on, BLINK OFF
    mov al, 0x09
    out dx, al
    inc dx                          ; 3D9h: a black border
    xor al, al
    out dx, al
    jmp short .out
.v16:
    mov dx, 0x3D4                   ; max scan line: 3, so 400 lines hold
    mov al, 9                       ; 100 rows (the other bits kept)
    out dx, al
    inc dx
    in al, dx
    and al, 0xE0
    or al, 3
    out dx, al
    dec dx
    mov ax, 0x200A                  ; the cursor off
    out dx, ax
    mov ax, 0x1003                  ; blink off: sixteen backgrounds
    xor bx, bx
    int 0x10
    call vp_c16fill
.out:
    jmp vp_ld7

.c4:
    mov ah, 0x0B                    ; background and intensity
    xor bh, bh
    mov bl, [vp_cgapal]
    and bl, 0x1F
    int 0x10
    mov ah, 0x0B                    ; the set: bit 5's, and mode 5's is set
    mov bh, 1                       ; 1 with its magenta made red below
    mov bl, [vp_cgapal]
    mov cl, 5
    shr bl, cl
    or bl, bl
    jz .set
    mov bl, 1
.set:
    int 0x10
    test byte [vp_cgapal], 0x40     ; MODE 5's cyan, red and white
    jz .out
    cmp dh, VID_CGA
    jne .ev
    mov dx, 0x3D8
    mov al, 0x0E                    ; 320 x 200, black-and-white bit, video on
    out dx, al
    jmp short .out
.ev:
    mov ax, 0x1000                  ; palette register 2: magenta -> red, in
    mov bx, 0x0402                  ; the 200-line codes, intensity 10h
    test byte [vp_cgapal], 0x10
    jz .ev1
    mov bh, 0x14
.ev1:
    int 0x10
    jmp short .out

vp_c16fill:                         ; every cell 0DEh on black
    mov ax, 0x00DE
vp_cfill8:                          ; ...or AX, the hack's 8,000 cells
    mov cx, 8000
vp_cfill:                           ; ...or CX words of AX, from [vp_vseg]:0
    mov es, [vp_vseg]
    xor di, di
    cld
    rep stosw
    ret

; vp_c16tim - the 6845 retimed for the text hack (SPEC.md 88.15.2's six
; writes), video off meanwhile. Clobbers AX, CX, DX, SI
vp_c16tim:
    mov dx, 0x3D8
    mov al, 0x01
    out dx, al
    mov si, vp_c16crt
    mov cx, 6
    mov dx, 0x3D4
.crt:
    lodsw
    out dx, al
    inc dx
    mov al, ah
    out dx, al
    dec dx
    loop .crt
    ret

; vp_rdpal - a VGA8 file's palette (98.1.1) into its claim, [vp_p8seg], and
; its first sixteen into vp_pal - the luma the Preview dithers by is made
; from it at each picture (vp_mklum); a VGA4 file's own sixteen into vp_pal.
; CF=1 with [vp_msg] set: unreadable, a value past the DAC's 63, or no heap
; for the claim. Nothing for any other format. The last file's claim is
; gone already: vp_open's vp_pevfree
vp_rdpal:
    mov byte [vo_ink4], 15          ; (the EGA's white, until a palette of
    cmp byte [vp_pixfmt], PF_VGA8   ; its own says another: vp_inkset)
    je .go
    cmp byte [vp_pixfmt], PF_VGA4   ; ...or a VGA4 file's own sixteen
    jne .none                       ; (98.1.3.2.1), when it has them
    mov ax, [vp_palo]               ; (AX: vp_open's one call keeps none)
    or ax, [vp_palo+2]
    jnz .go
    push bx                         ; (the EGA's own: the poster as it is)
    xor bx, bx
.idn:
    mov [vp_v4xl+bx], bl
    inc bx
    cmp bx, 16
    jb .idn
    pop bx
.none:
    clc
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov cx, 768                     ; 768 bytes' clusters, whole KB (two
    call vp_spankb                  ; 32 KB clusters' sum was 0 in a word)
    call vp_mclaim
    jc .mem
    mov [vp_rdseg], dx
    mov ax, [vp_palo]
    mov dx, [vp_palo+2]
    mov cx, 768
    call vp_rdat
    jc .io
    mov es, [vp_rdseg]              ; every value a six-bit one, where it
    mov di, si                      ; landed
    mov cx, 768
    mov al, 63
    cld
.chk:
    scasb
    jb .bad
    loop .chk
    push ds                         ; THE SIXTEEN into vp_pal: a VGA4 file's
    pop es                          ; own, or the first of VGA8's
    mov di, vp_pal
    mov cl, 48
    push ds
    mov ds, [vp_rdseg]
    rep movsb
    pop ds
    cmp byte [vp_pixfmt], PF_VGA8   ; VGA8's 256 into a claim of their own
    jne .p16                        ; (vp_p8seg): what the bracket's DAC and
    mov ax, 1                       ; the picture's lumas read, and nothing
    call vp_mclhi                   ; at interrupt time - so it is movable
    jc .mfr                         ; (vp_smove), and from the top, being
    mov [vp_p8seg], dx              ; the file's for as long as it is open
    mov ax, vp_smove
    call vp_mmov
    mov es, dx
    xor di, di
    sub si, 48
    mov cx, 768 / 2
    push ds
    mov ds, [vp_rdseg]
    rep movsw
    pop ds
.p16:
    cmp word [vp_pevn], 0           ; A PALETTE A SCENE (98.1.3.2.3): the
    je .lum0                        ; table, beside the palette it starts at
    call vp_pevload
    jc .fr
.lum0:
    cmp byte [vp_pixfmt], PF_VGA4
    jne .fin
    call vp_v4xlat
.fin:
    clc
    jmp short .fr0
.bad:
    mov word [vp_msg], vp_s_bad
    jmp short .fr
.mfr:
    mov word [vp_msg], vp_s_mem
    jmp short .fr
.io:
    mov word [vp_msg], vp_s_io
.fr:
    stc
.fr0:
    pushf                           ; (the read's claim, either way)
    call vp_rdfree
    popf
    jmp short .out
.mem:
    mov word [vp_msg], vp_s_mem
    stc
.out:
    jmp vp_ld7

; vp_v4xlat - a VGA4 file's own sixteen (vp_pal) as the desktop's: each
; pixel value's nearest of the EGA's sixteen in RGB, the first of equals,
; into vp_v4xl - what the Preview's poster is drawn in (98.4.5),
; tools/os88vid.py's vga4_xlat. Preserves all
vp_v4xlat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, vp_pal
    xor di, di                      ; DI = the pixel value
.px:
    mov bp, 0xFFFF                  ; BP = the best distance, DL its colour
    xor dl, dl
    xor bx, bx                      ; BX = the EGA colour x 3
.k:
    xor cx, cx
    push si
    push bx
    mov dh, 3
.ch:
    mov al, [si]
    sub al, [vp_std16+bx]
    imul al                         ; (six bits: |d| <= 63, d^2 < 4,000)
    add cx, ax
    inc si
    inc bx
    dec dh
    jnz .ch
    pop bx
    pop si
    cmp cx, bp
    jae .nk
    mov bp, cx
    mov ax, bx
    mov dh, 3
    div dh
    mov dl, al
.nk:
    add bx, 3
    cmp bx, 48
    jb .k
    mov [vp_v4xl+di], dl
    add si, 3
    inc di
    cmp di, 16
    jb .px
    jmp vp_ld7b

; vp_dac - VGA8: the file's 256 colours into the DAC, once the bracket has
; set 13h (the mode set loads the BIOS's own, and the bracket's restore
; sets the desktop's back). Preserves all
vp_dac:
    cmp byte [vp_pixfmt], PF_VGA8
    jne .out
    push ax
    push cx
    push dx
    push si
    push ds
    mov dx, 0x3C8                   ; out of its claim (vp_rdpal): vp_dacw's
    xor al, al                      ; loop, which reads vp_pal and goes on to
    out dx, al                      ; the sixteen's ink
    inc dx
    mov cx, 768
    xor si, si
    mov ds, [vp_p8seg]
    cld
.l:
    lodsb
    out dx, al
    loop .l
    pop ds
    pop si
    pop dx
    pop cx
    pop ax
.out:
    ret

; vp_dacw - CX bytes of vp_pal into the DAC from its entry 0. Clobbers AL,
; CX, DX, SI
vp_dacw:
    mov dx, 0x3C8
    xor al, al
    out dx, al
    inc dx
    mov si, vp_pal
    cld
.l:
    lodsb
    out dx, al
    loop .l
    cmp byte [vp_pixfmt], PF_VGA4   ; (sixteen: the text's ink, vp_inkset)
    jne .r
    jmp vp_inkset
.r:
    ret

; vp_inkset - THE TEXT'S INK on sixteen colours (98.3.13): the brightest of
; vp_pal's sixteen, by 30 R + 59 G + 11 B, into [vo_ink4] - a file's own
; sixteen are in no order (a scene's are placed for its plane codes), so
; entry 15, the EGA's white, was any colour, and on the owner's GasGas the
; Buffering line was a dark grey on black. Clobbers AL, CX, DX, SI
vp_inkset:
    push ax
    push bx
    mov si, vp_pal
    xor bx, bx                      ; BL = the entry, BH = the best's
    xor dx, dx                      ; DX = its brightness
.e:
    lodsb
    mov ah, 30
    mul ah
    mov cx, ax
    lodsb
    mov ah, 59
    mul ah
    add cx, ax
    lodsb
    mov ah, 11
    mul ah
    add cx, ax
    cmp cx, dx
    jb .n
    mov dx, cx                      ; (ties to the later: the EGA's 15)
    mov bh, bl
.n:
    inc bl
    cmp bl, 16
    jb .e
    mov [vo_ink4], bh
    pop bx
    pop ax
    ret

; vp_scrset - a VGA4 file's SCREEN OF ITS OWN (98.1.3.2.1), once the bracket
; has set its mode (0Dh or 12h): rows 80 plane bytes apart whatever the
; width (the CRTC's Offset, 40 words: 0Dh's 20 doubled, 12h's own), the
; vertical timing a table names - 0Dh's 400 lines made 480 for 320 x 240,
; 12h's 480 made mode 10h's 350 or 400 for the 640-wide two, the horizontal
; untouched - and the file's own sixteen, the Attribute Controller's
; palette made the identity so a pixel's value IS its DAC entry. The
; bracket's restore sets the desktop's mode, and all of this with it.
; ON 640 x 480 (screen 0, 98.1.3.2.2) it is the sixteen alone: 12h is the
; desktop's mode and its timing is the file's.
; Preserves all
vp_scrset:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [vp_screen]
    or al, [vp_mx200]               ; (MODEX AT 320 x 200: 13h's 400 lines,
    jnz .crt                        ; 98.1.3.1.1)
    cmp byte [vp_pixfmt], PF_VGA4   ; (screen 0: a VGA4 file's own sixteen,
    je .pal                         ; when it has them)
    jmp short .done
.crt:
    mov dx, 0x3D4
    mov al, 0x11                    ; CR0-7 unprotected (CR11 bit 7)
    out dx, al
    inc dx
    in al, dx
    and al, 0x7F
    out dx, al
    dec dx
    mov ax, 0x2813                  ; the Offset: 40 words, 80 bytes a row
    cmp word [vp_lstr], 40          ; (...or NARROW's 20, 0Dh's own, 40
    jne .off                        ; bytes a row: 98.1.3.2.5)
    mov ah, 0x14
.off:
    out dx, ax
    mov al, [vp_screen]
    cbw
    xchg ax, bx
    shl bx, 1
    mov si, [vp_vtimes+bx]
    cmp byte [vp_mx200], 0
    je .vt0
    mov si, vp_vt200m
.vt0:
    or si, si
    jz .pal
    mov dx, 0x3C4                   ; the sync polarities, under a
    mov ax, 0x0100                  ; synchronous reset of the sequencer
    out dx, ax
    mov dx, 0x3C2
    cld
    lodsb
    out dx, al
    mov dx, 0x3C4
    mov ax, 0x0300
    out dx, ax
    mov dx, 0x3D4
.vt:
    lodsw                           ; AL = the register, AH = its value, the
    or al, al                       ; list ending in 0 (CR11 last, which
    jz .pal                         ; protects CR0-7 again)
    out dx, ax
    jmp short .vt
.pal:
    mov ax, [vp_palo]               ; ITS OWN SIXTEEN, or the EGA's that the
    or ax, [vp_palo+2]              ; mode set loaded
    jz .done

    pushf
    cli
    mov dx, 0x3DA                   ; the AC's flip-flop to its index
    in al, dx
    mov dx, 0x3C0
    xor al, al
.ac:
    out dx, al                      ; palette register AL = AL, the display
    out dx, al                      ; off until the 20h below
    inc ax
    cmp al, 16
    jb .ac
    mov al, 0x20
    out dx, al
    popf
    mov cx, 48                      ; ...and its sixteen into the DAC
    call vp_dacw
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_crtc - a row scale of 2 (98.2.4): the CRTC's Maximum Scan Line (3D4h
; index 9) shows each row twice as many scan lines - 13h's and Mode X's 1
; (two lines a row) becomes 3 - so the mode's rows halve and the picture
; stays the screen's size. The bracket's restore sets the mode, and it,
; back. Preserves all
vp_crtc:
    cmp byte [vp_rs], 0
    je .out
    push ax
    push dx
    mov dx, 0x3D4
    mov al, 9
    out dx, al
    inc dx
    in al, dx
    mov ah, al
    and ah, 0xE0
    and al, 0x1F
    shl al, 1
    inc ax                          ; n + 1 lines a row, twice: 2n + 1 (AL
                                    ; <= 3Eh: no carry into AH)
    or al, ah
    out dx, al
    pop dx
    pop ax
.out:
    ret

; vp_mklum - vp_pal's 256 lumas, 0..16, into vp_lum: what vp_v8mono dithers,
; (77 r + 150 g + 29 b) >> 8 of the DAC's six bits, then x 17 + 32 >> 6 for
; 0..16 against a 4 x 4 Bayer cell - tools/os88vid.py's vga8_lum16. Made at
; every picture, its table sharing the box's save, out of the palette's own
; claim (vp_rdpal). Clobbers AX, BX, CX, SI, DI, BP
vp_mklum:
    push ds
    push es
    push ds
    pop es
    mov di, vp_lum
    mov ds, [vp_p8seg]
    xor si, si
    mov bp, 256
    cld
.l:
    lodsb
    mov bl, 77
    mul bl
    mov bx, ax
    lodsb
    mov cl, 150
    mul cl
    add bx, ax
    lodsb
    mov cl, 29
    mul cl
    add ax, bx
    mov al, ah                      ; >> 8: 0..63
    mov cl, 17
    mul cl
    add ax, 32
    mov cl, 6
    shr ax, cl                      ; 0..16
    stosb
    dec bp
    jnz .l
    pop es
    pop ds
    ret

; vp_rthrs - DX = a poster row: its four thresholds, the 4 x 4 Bayer cell's
; row DX mod 4, into vp_rthr - every one-bit dither's (vp_cmono, both of
; vp_v8mono's). Clobbers AX, BX
vp_rthrs:
    mov bx, dx
    and bx, 3
    shl bx, 1
    shl bx, 1
    mov ax, [vp_bayer4+bx]
    mov [vp_rthr], ax
    mov ax, [vp_bayer4+bx+2]
    mov [vp_rthr+2], ax
    ret

; vp_v8mono - the VGA8 canvas in [vp_kshd] (LIN320: row y at y x 320) as a
; one-bit one in [vp_pseg], dense at [vp_pwb] bytes a row: a pixel is lit
; when its luma beats the 4 x 4 Bayer cell over it (tools/os88vid.py's
; vga8_mono, bit for bit). Preserves all
vp_v8mono:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call vp_mklum
    cmp byte [vp_layout], LAY_MODEX
    je .mx
    mov es, [vp_kshd]
    xor di, di                      ; DI = the output byte
    xor dx, dx                      ; DX = the row
.row:
    cmp dx, [vp_ph]
    jae .done
    mov ax, dx                      ; the canvas row this output row shows
    mov cl, [vp_rs]                 ; (each twice with a row scale of 2)
    shr ax, cl
    push dx
    mov cx, 320
    mul cx
    pop dx
    mov si, ax                      ; ES:SI = the row's first pixel
    call vp_rthrs                   ; the row's four thresholds, where BX
                                    ; can index them
    mov cx, [vp_pwb]
.byte:
    push cx
    mov cx, 8                       ; AH = the byte, built from the left:
                                    ; eight rcl's leave nothing of what it was
.pix:
    es lodsb                        ; (DF clear: vp_mklum)
    mov bx, vp_lum
    xlat
    mov bx, si                      ; x & 3 of the pixel just read
    dec bx
    and bx, 3
    cmp [vp_rthr+bx], al            ; CF = threshold < luma: lit
    rcl ah, 1
    loop .pix
    push ds
    mov ds, [vp_pseg]
    mov [di], ah
    pop ds
    inc di
    pop cx
    loop .byte
    inc dx
    jmp short .row
.done:
    jmp vp_ld8
.mx:                                ; MODEX: pixel x of a row is plane x mod
    mov ax, [vp_kshd]               ; 4, byte x div 4 - so an output byte's
    mov bx, vp_mxseg                ; eight are two bytes of each plane
    mov cx, 4
.seg:
    mov [bx], ax
    add ax, VP_MXPL
    inc bx
    inc bx
    loop .seg
    xor di, di
    xor dx, dx
.mrow:
    cmp dx, [vp_ph]
    jae .done
    mov ax, dx
    mov cl, [vp_rs]
    shr ax, cl
    push dx
    mov cx, 80
    mul cx
    pop dx
    mov bp, ax                      ; BP = the row in each plane
    call vp_rthrs
    mov cx, [vp_pwb]
.mbyte:
    push cx
    xor cx, cx                      ; CX = the pixel, 0..7 (AH: eight rcl's)
.mpix:
    mov bx, cx
    and bx, 3
    shl bx, 1
    mov es, [vp_mxseg+bx]
    mov bx, cx
    shr bx, 1
    shr bx, 1
    add bx, bp
    mov bl, [es:bx]
    xor bh, bh
    mov al, [vp_lum+bx]
    mov bx, cx
    and bx, 3
    cmp [vp_rthr+bx], al            ; CF = threshold < luma: lit
    rcl ah, 1
    inc cx
    cmp cx, 8
    jb .mpix
    push ds
    mov ds, [vp_pseg]
    mov [di], ah
    pop ds
    inc di
    inc bp
    inc bp
    pop cx
    loop .mbyte
    inc dx
    jmp short .mrow

; vp_plshd - AX = the KB a planar keyframe decodes into: three planes'
; spacing and 64 KB past the last one's base, so a 16-bit write from any
; plane is inside the claim (98.1.6)
vp_plshd:
    push cx
    mov ax, [vp_plsp]
    mov cx, ax
    shl ax, 1
    add ax, cx                      ; 3 x plsp paragraphs
    add ax, 4096 + 63               ; + 64 KB, rounded up to a KB
    mov cl, 6
    shr ax, cl
    pop cx
    ret

; vp_v4dims - AX = a VGA4 poster's bytes a row at [vp_ps], CX = its rows:
; every ps-th pixel of every ps-th row, two to the byte. Preserves the rest
vp_v4dims:
    push dx
    push bx
    mov bx, [vp_ps]
    mov ax, [vp_wb]
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; the canvas's pixels
    xor dx, dx
    div bx
    inc ax
    shr ax, 1
    push ax
    mov ax, [vp_h]
    add ax, bx
    dec ax
    xor dx, dx
    div bx
    pop cx
    xchg ax, cx
    pop bx
    pop dx
    ret

; vp_v4pack - the VGA4 canvas in [vp_kshd] (four bit-planes [vp_plsp]
; apart, row y at y x [vp_lstr]) as OSAPI_GFX_BLIT4's packed nibbles in [vp_pseg],
; the left pixel high, one pixel of each [vp_ps]-square cell, which one
; turning with the output's place - tools/os88vid.py's vga4_pack. Sets vp_pbw, vp_ppx, vp_prows. Preserves all
vp_v4pack:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, [vp_kshd]
    mov bx, vp_mxseg
    mov cx, 4
.seg:
    mov [bx], ax
    add ax, [vp_plsp]
    inc bx
    inc bx
    loop .seg
    call vp_v4dims
    mov [vp_pbw], ax
    mov [vp_prows], cx
    mov ax, [vp_wb]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [vp_v4w], ax
    xor dx, dx
    div word [vp_ps]
    mov [vp_ppx], ax
    mov es, [vp_pseg]
    xor di, di
    mov [vp_v4y], di                ; the output row
    dec di
    mov [vp_v4kb], di               ; (no source byte cached yet)
    mov [vp_v4kb+2], di
    mov [vp_v4kb+4], di
    mov [vp_v4kb+6], di
    inc di
.row:
    mov ax, [vp_v4y]
    cmp ax, [vp_prows]
    jae .done
    mov bx, [vp_ps]                 ; WHICH PIXEL OF THE CELL turns with the
    dec bx                          ; output's place (98.4.5): its column the
    and ax, bx                      ; row's mod the scale, on by the scale a
    mov [vp_v4cx], ax               ; pixel...
    mov ax, [vp_v4y]
    mul word [vp_ps]                ; ...its row the column's mod the scale:
    mov cx, [vp_ps]                 ; the row's ps source rows' offsets, once
    xor bx, bx                      ; a row and not a MUL a pixel
.ro:
    push ax
    cmp ax, [vp_h]                  ; (the last cell's rows past the canvas:
    jb .sy                          ; its last)
    mov ax, [vp_h]
    dec ax
.sy:
    mul word [vp_lstr]              ; (80, or a NARROW screen's 40)
    mov [vp_v4ro+bx], ax
    pop ax
    inc ax
    inc bx
    inc bx
    loop .ro
    xor ax, ax
    mov [vp_v4x], ax
    mov [vp_v4n], al
.pix:
    mov ax, [vp_v4x]
    cmp ax, [vp_ppx]
    jae .eol
    mov bx, [vp_ps]                 ; ...so a checkerboard stays one where
    dec bx                          ; the cell's corner took a single colour
    and bx, ax                      ; of it every time
    shl bx, 1                       ; BX = the row's word in vp_v4ro/vp_v4kb
    mov si, bx
    shl si, 1                       ; SI = its four planes in vp_v4c
    mov ax, [vp_v4cx]
    mov cl, al
    and cl, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add ax, [vp_v4ro+bx]            ; AX = its byte in each plane
    cmp ax, [vp_v4kb+bx]            ; THE BYTE CACHED, one for each of the
    je .have                        ; row's source rows
    mov [vp_v4kb+bx], ax            ; a new source byte: its four planes
    mov bp, ax
    push es
    xor bx, bx
.ld:
    mov es, [vp_mxseg+bx]
    mov al, [es:bp]
    mov [vp_v4c+si], al
    inc si
    inc bx
    inc bx
    cmp bx, 8
    jb .ld
    pop es
    sub si, 4
.have:
    xor ah, ah
    mov bx, 3                       ; plane 3 first, so AH = b3 b2 b1 b0
.bit:
    mov al, [vp_v4c+si+bx]
    shl al, cl
    shl al, 1                       ; CF = this pixel's bit
    rcl ah, 1
    dec bx
    jns .bit
    mov bl, ah                      ; (a file of its own colours, drawn in
    xor bh, bh                      ; the desktop's nearest: vp_v4xlat)
    mov ah, [vp_v4xl+bx]
    test byte [vp_v4n], 1
    jnz .lo
    mov cl, 4
    shl ah, cl
    mov [es:di], ah
    jmp short .nx
.lo:
    or [es:di], ah
    inc di
.nx:
    xor byte [vp_v4n], 1
    inc word [vp_v4x]
    mov ax, [vp_ps]
    add [vp_v4cx], ax
    jmp .pix
.eol:
    test byte [vp_v4n], 1           ; a row ending on its high nibble
    jz .nr
    inc di
.nr:
    inc word [vp_v4y]
    jmp .row
.done:
    jmp vp_ld8

; vp_zero - ES:0: the file's layout's memory image, black - or, where the
; keeper is smaller ([vp_kkb], a resident play's canvas, 98.1.7.3), as much
; of it as the keeper holds
vp_zero:
    push ax
    push bx
    push cx
    push di
    cmp byte [vp_planar], 0         ; four planes, [vp_plsp] apart: past
    je .one                         ; one segment, so a plane at a time
    push es
    push dx
    mov dx, 4
.zp:
    xor ax, ax
    xor di, di
    mov cx, [vp_plsp]
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; paragraphs x 8 = words
    cld
    rep stosw
    mov ax, es
    add ax, [vp_plsp]
    mov es, ax
    dec dx
    jnz .zp
    pop dx
    pop es
    jmp short .out
.one:
    mov bl, [vp_layout]
    xor bh, bh
    mov ch, [vp_laykb+bx]           ; KB x 512 = words
    mov al, [vp_kkb]
    or al, al
    jz .z
    cmp al, ch
    jae .z
    mov ch, al
.z:
    shl ch, 1
    xor cl, cl
    xor di, di
    xor ax, ax
    cld
    rep stosw
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; vp_rdat - DX:AX = a file offset, CX = bytes wanted: the clusters under
; them read into [vp_rdseg]:0 - READ_AT takes whole clusters, and one call
; moves at most 64 KB less a cluster (a word of them), so a span wider than
; that - a 32 KB cluster's, on a volume near 2 GB - is read in two or three.
; The buffer holds SI + CX rounded up to a cluster, which vp_spankb bounds.
; out: CF=0 SI = where the offset landed; CF=1 the disk failed, or the file
; stops short of them
;
; THE DISK ARM IS os88_rseq (apps/os88rseq.inc, SPEC.md 18.4.8.1) ON ITS OWN
; CURSOR, [vp_rdcur], and not OSAPI_FILE_READ_AT: READ_AT re-walks the chain
; from the front on every call, so vp_ldstored's 32 KB pieces of a block up to
; 1 MB walked a chain that grew with every piece - and so did this routine's
; own two or three calls for a wide span. A read that carries on from where
; the last one stopped walks one link a cluster; any other offset re-seeds the
; cursor, which is the walk READ_AT made anyway, so no caller pays more. The
; cursor is NOT the stream's: vp_cur, vp_xcur and vp_lycur belong to the ring,
; the hold and the layers, and a block can load while any of them is live.
; It names no file, so vp_open starts it cold for every file. The HELD arm
; (vp_xrdat) still comes first - a copy out of XMS beats any disk read - and
; on kern_small the helper makes READ_AT's own call
vp_rdat:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bx, [vp_rgb]
    dec bx                          ; BX = a cluster's mask, throughout
    mov si, ax
    and si, bx                      ; SI = into its cluster
    sub ax, si                      ; DX:AX = that cluster's start
    add cx, si                      ; the bytes owed from there, 17 bits
    mov [vp_rdend], cx
    sbb di, di                      ; (the carry, as 0 or FFFFh...
    neg di                          ; ...as 0 or 1)
    mov [vp_rdend+2], di
    jnz .disk                       ; past 64 KB: the disk's
    inc cx                          ; (vp_xrdat rounds CX up to even: FFFFh
    jz .disk                        ; is the disk's)
    dec cx
    call vp_xrdat                   ; HELD (98.3.18): a copy out of XMS
    jnc .ok
.disk:
    mov es, [vp_rdseg]
.l:
    mov di, [vp_rgb]
    neg di                          ; DI = a call's most, whole clusters
    mov cx, di                      ; CX = what this one must deliver
    cmp word [vp_rdend+2], 0
    jne .rd
    cmp [vp_rdend], di
    ja .rd
    mov cx, [vp_rdend]              ; ...the rest, in one call
    mov di, cx
    add di, bx
    not bx
    and di, bx
    not bx
.rd:
    push ax
    push dx
    push si
    push bx
    push cx
    mov cx, di
    xor bx, bx
    mov si, vp_name
    push di
    mov di, vp_rdcur
    call os88_rseq                  ; DX:AX = the bytes delivered
    pop di
    pop cx
    pop bx
    pop si
    jc .badp
    or dx, dx
    jnz .next
    cmp ax, cx
    jb .badp
.next:
    pop dx
    pop ax
    add ax, di                      ; the next call's offset...
    adc dx, 0
    sub [vp_rdend], di              ; ...and what is still owed
    sbb word [vp_rdend+2], 0
    jc .ok
    mov cx, [vp_rdend]
    or cx, [vp_rdend+2]
    jz .ok
    mov cl, 4                       ; ...into the buffer past this call's
    shr di, cl
    mov cx, es
    add cx, di
    mov es, cx
    jmp short .l
.badp:
    pop dx
    pop ax
.bad:
    stc
    jmp short .out
.ok:
    clc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "os88rseq.inc"            ; os88_rseq, vp_rdat's disk arm

; vp_spankb - CX = bytes at any offset: AX = the KB vp_rdat's read of them
; can take - CX and a cluster less a byte, rounded up to whole clusters
; (the offset's own place in its cluster is the most it can be moved on)
vp_spankb:
    push bx
    push cx
    push dx
    mov bx, [vp_rgb]
    dec bx
    xor dx, dx
    mov ax, cx
    add ax, bx
    adc dx, 0
    add ax, bx
    adc dx, 0
    not bx
    and ax, bx                      ; (a cluster divides 64 KB: DX stands)
    add ax, 1023
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; THE FILE HELD IN XMS (SPEC.md 98.3.18). A streamed file that fits the pool
; is given one block its size at open, and the block fills from the front: a
; chunk at a time on the window's timer while the window is idle, and behind
; the stream while it plays. Every read that falls inside what has arrived -
; a chunk of the stream, a key, a seam, a seek - is a copy out of it; the rest
; is the disk's, as it always was. [vp_xhave] is the bytes held, from 0
; -----------------------------------------------------------------------------
; vp_xopen - the hold for the file just opened, if it takes one. The size is
; the directory's: a read into no buffer answers FERR_BIG with the KB it
; would need, before any data I/O (SPEC.md 20.14.6.3)
vp_xopen:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call vp_xfree
    xor ax, ax
    mov [vp_lysplt], al             ; (this file's pool not split yet)
    mov [vp_lydv], al               ; (...nor its disk timed, 98.1.9.3)
    mov [vp_lycv], al               ; (...nor its CPU on key 0, 98.1.9.4)
    cmp byte [vp_ok], 1
    je .ok                          ; (jne .out, written as nasm emitted it:
    jmp near .out                   ; it sat on a short jump's edge and took
.ok:                                ; either form as code above it moved)
    cmp byte [vp_resid], 0          ; RESIDENT: read whole into its blocks
    jne .out
    call vp_xcaps                   ; no pool (every 8088): nothing more is
    or ax, ax                       ; asked, not even the directory - unless
    jnz .sz                         ; EXPANDED MEMORY answers (98.3.18.6)
    call vp_eprobe
    jc .out
.sz:
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov si, vp_name
    call OSAPI_FILE_READ            ; ES:BX = nowhere, 0 bytes
    jnc .out                        ; (an empty file)
    cmp ax, FERR_BIG
    jne .out
    or dx, dx
    jz .out
    mov si, dx                      ; SI = its KB, one over
    call vp_epref                   ; EMS where it costs less than the
    jnc .ems                        ; pool's copies (98.3.18.15)
    call vp_xcaps                   ; AX = the KB the pool has (BL, DX:CX
    or ax, ax                       ; its other answers)
    jz .ems
    cmp ax, si
    jb .bank
    mov ax, si
    mov cl, 10
    shl ax, cl
    mov dx, si
    mov cl, 6
    shr dx, cl                      ; DX:AX = KB x 1024
    mov [vp_xcap], ax
    mov [vp_xcap+2], dx
    call OSAPI_XMEM_ALLOC
    jc .out
    mov [vp_xbase], ax
    mov [vp_xbase+2], dx
%ifdef VP_XSPTEST
    call vp_xspeed                  ; (a TEST BUILD: tests/vidxspeed.py reads
%endif                              ; [vp_xr10] - only EMS asks otherwise)
    cmp si, XG_NEED / 1024          ; THE GAP, once an instance, while the
    jb .xg                          ; hold is empty (os88xgap.inc)
    call vp_xgm
.xg:
    xor ax, ax
    mov [vp_xhave], ax
    mov [vp_xhave+2], ax
    mov [vp_xfull], al
    inc ax                          ; the first chunk on the next tick
    mov [vp_xon], al
    mov bx, [vp_win]
    call OSAPI_WM_TIMER
.out:
    call vp_lyxopen                 ; ...and THE LAYER's, from what is left
    pop es                          ; (98.1.9)
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.bank:                              ; THE FILE DOES NOT FIT: a BANK, a FIFO
                                    ; of chunks ahead of the ring (VIDEO-XMS-
                                    ; PLAN 4): the WHOLE pool (98.3.18.2.1)
    cmp word [vp_lybkb], 0          ; - and with a LAYER's bank asked
    je .bnl                         ; for besides, no more than the base's
    mov cx, [vp_hxb]                ; own ask, so the layer's has the rest
    jcxz .bnl                       ; (98.1.9)
    add cx, 31
    and cl, 0xE0
    cmp ax, cx
    jbe .bnl
    mov ax, cx
.bnl:
    call vp_balloc
    jmp short .out
.ems:                               ; NO POOL, AN EMS BOARD (98.3.18.6): the
    call vp_espeed                  ; bank in its pages, sized to the file -
    cmp byte [vp_eslow], 2          ; unless the board is so slow that the
    je .eno                         ; copies would cost more than the depth
    call vp_eopen                   ; buys (98.3.18.8)
.eno:
    jmp .out

; vp_balloc - AX = KB: the BASE's bank, that much in whole 32 KB slots or
; the most of it XMS has in one run, VP_XBMIN at least - [vp_xbank] and
; [vp_xon] 1, nothing of the file held. CF=1 none. Clobbers AX, CX, DX
vp_balloc:
    and al, 0xE0                    ; in whole 32 KB slots
.btry:
    cmp ax, VP_XBMIN
    jb .no
    push ax
    mov dx, ax
    mov cl, 6
    shr dx, cl
    mov cl, 10
    shl ax, cl                      ; DX:AX = its bytes
    call OSAPI_XMEM_ALLOC
    pop cx
    jnc .bgot
    shr cx, 1                       ; no run that long: half, in slots
    and cl, 0xE0
    mov ax, cx
    jmp short .btry
.bgot:
    mov [vp_xbase], ax
    mov [vp_xbase+2], dx
    call vp_xgm                     ; THE GAP, while the bank is empty
    mov ax, cx
    mov cl, 5
    shr ax, cl
    mov [vp_xbn], ax                ; its slots
    xor ax, ax                      ; nothing of the FILE is held: vp_xin
    mov [vp_xhave], ax              ; and vp_canlive answer no, whatever
    mov [vp_xhave+2], ax            ; the last file's hold left here
    mov [vp_xfull], al
    call vp_bflush                  ; (AX kept)
    inc ax
    mov [vp_xbank], al
    mov [vp_xon], al                ; (and NO timer: it fills while a play
    clc                             ; reads, VIDEO-XMS-PLAN 4.1)
    ret
.no:
    stc
    ret

; vp_lycarve - DX = the ring's claim, just taken; [vp_k] its K slots: THE
; LAYER PLACED (98.1.9.10). Where [vp_lyfl] says there is one, its slots -
; as many as the ring's HEADROOM past the header's ring lends, VP_LYMAX
; halving to two, and two lent of the header's ring only where the base has
; an XMS bank to have them back in at the split ([vp_lycmp], KB) - and its
; bank's bounce are the ring claim's TOP: the ring K-L slots, its mirror and
; the base bank's bounce after them as ever, the layer's mirror the claim's
; slack ([vp_lymx]; a whole slot where that was refused). Only where the
; ring keeps four. A NO gives them back (vp_lyunc). IN PLACE the ring is not
; one: the layer's own claim, from what the heap has after it (vp_lyown).
; Not after the disk has said no (vp_lyopen says that). Every register kept
vp_lycarve:
    mov byte [vp_lyb], 0
    mov word [vp_lycmp], 0          ; (0 on every way out but a loan's)
    cmp byte [vp_lyfl], 0
    je .r0
    cmp byte [vp_lydv], 2           ; (the disk said no already)
    je .r0
    cmp byte [vp_einp], 0           ; IN PLACE: the board's, the bank's or
    jne .own                        ; the hold's - the ring's claim is a
    cmp byte [vp_xip], 0            ; key's, and the heap past it is free
    je .rg
.own:
    jmp vp_lyown
.r0:
    ret
.rg:
    push ax
    push bx
    push cx
    push dx
    mov dx, VP_LYMAX                ; THE MOST the ring's HEADROOM lends,
.most:                              ; halved to two, and two only lent of
    call .l                         ; the header's ring. BX = L
    mov ax, [vp_k]
    sub ax, bx
    jb .less
    cmp ax, 4
    jb .less
    cmp al, [vp_rneed]
    jae .lent                       ; (all of it headroom)
.less:
    shr dx, 1
    cmp dx, 2
    ja .most
    mov dx, 2
    call .l
    mov ax, [vp_k]
    sub ax, bx                      ; K' = K - L
    jb .no
    cmp ax, 4
    jb .no                          ; (a ring of four at the least)
.lent:
    mov [vp_lyns], dx
    pop dx
    mov cl, [vp_rneed]
    xor ch, ch
    sub cx, ax                      ; CX = what K' is short of the header's
    jbe .c                          ; ring: lent from the base's bucket, so
    call vp_xmsbk                   ; paid back in its XMS bank - and with
    jz .no2                         ; none to pay it back in, none lent
    push ax
    mov ax, cx
    mov cl, 5
    shl ax, cl
    mov [vp_lycmp], ax              ; ...in KB
    pop ax
.c:
    mov cx, [vp_k]
    mov [vp_lyk0], cx
    mov [vp_k], ax
    mov [vp_kr], ax
    mov [vp_lyb], bl
    add ax, [vp_msl]                ; past the mirror...
    call vp_xmsbk                   ; ...and the base bank's bounce
    jz .nb
    cmp byte [vp_livem], 0
    jne .nb
    inc ax
.nb:
    mov cl, 11
    shl ax, cl                      ; x 2,048 paragraphs a slot
    add ax, dx
    mov [vp_lyseg], ax
    jmp short .x
.no:                                ; NO ROOM: the card's 'N KB short' is
    pop dx                          ; what the ring needed besides - L more
    mov ax, bx                      ; than its least four
    add ax, 4
    sub ax, [vp_k]
    jmp short .sht
.no2:                               ; ...or, nothing to pay a loan back in,
    mov ax, bx                      ; L more than the header's ring (DX
                                    ; popped at .lent)
    add al, [vp_rneed]
    adc ah, 0
    sub ax, [vp_k]
.sht:
    mov cl, 5
    shl ax, cl
    mov [vp_lysht], ax
.x:
    pop cx
    pop bx
    pop ax
.r:
    ret
.l:                                 ; BX = L: DX slots, the bounce, and
    mov bx, dx                      ; the mirror's slot where the claim's
    cmp byte [vp_lybnc], 0          ; slack could not have it
    je .l1
    inc bx
.l1:
    cmp byte [vp_lymx], 0
    jne .l2
    inc bx
.l2:
    ret

; vp_lyown - IN PLACE (vp_lycarve): the layer's own claim, AFTER the ring -
; whose claim is a key's, the stream being read in the board, the bank or
; the hold - from what the heap has left: VP_LYMAX slots, halving to two,
; its mirror and its bank's bounce. None: [vp_lysht] by how much. Every
; register kept
vp_lyown:
    push ax
    push bx
    push cx
    push dx
    mov dx, VP_LYMAX
.try:
    call vp_mavail
    call vp_kres                    ; AX = what is left, the seek's entry
    mov bx, ax                      ; and a muted play's sound kept back
    mov ax, dx
    add al, [vp_lybnc]
    mov cl, 5
    shl ax, cl
    add ax, [vp_lymkb]              ; [slots][mirror][the bank's bounce]
    cmp bx, ax
    jb .less
    push dx                         ; (DX is the SLOTS: vp_mclaim answers
    call vp_mclaim                  ; the claim in it, which was stored as
    mov cx, dx                      ; the count - [vp_lyns] read 15072, a
    pop dx                          ; segment, and the layer in place read
    jc .less                        ; nothing: tests/vidlybank.py)
    mov [vp_lyseg], cx
    jmp short .out
.less:
    sub ax, bx
    mov [vp_lysht], ax              ; (short by this, KB: the card's)
    shr dx, 1
    cmp dx, 2
    jae .try
    jmp short .x
.out:
    mov [vp_lyns], dx
    mov word [vp_lysht], 0
.x:
    jmp vp_ld4

; vp_lyunc - THE FLIP GIVEN BACK (98.1.9.4): the layer not read after all -
; the CPU's verdict or the disk's - so its slots are the ring's again, K as
; the machine had them. Only while every chunk read sits where K puts it
; too (the session's chunks count from 0, so up to K' read), the mirror
; copied up to K's end for the chunks it mirrors. Every register kept
vp_lyunc:
    cmp byte [vp_lyb], 0
    je .r
    cmp byte [vp_lyon], 0
    jne .r
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    push es
    mov ax, [vp_lc]
    cmp ax, [vp_k]
    ja .x                           ; (the ring wrapped: it stays as it is)
    xor ax, ax
    mov [vp_lyb], al
    mov [vp_lyseg], ax
    mov [vp_lycmp], ax
    mov ax, [vp_lyk0]
    mov [vp_k], ax
    mov [vp_kr], ax
    xor bx, bx
.m:
    cmp bx, [vp_msl]
    jae .x
    cmp bx, [vp_lc]
    jae .x                          ; (not read: mirrored when it is)
    mov ax, bx
    mov cl, 11
    shl ax, cl
    add ax, [vp_ring]
    mov si, ax                      ; (the slot's segment)
    mov ax, bx
    add ax, [vp_k]
    shl ax, cl
    add ax, [vp_ring]
    mov es, ax
    xor di, di
    mov ds, si
    xor si, si
    mov cx, VP_CHUNK / 2
    cld
    rep movsw
    push cs                         ; (DS back: ours is our CS)
    pop ds
    inc bx
    jmp short .m
.x:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.r:
    ret

; vp_lyearly - the ring's fill, a chunk read: a FLIP's disk verdict as soon
; as the fill can give one (three chunks), and its slots given back on a
; no - while the ring has not reached them. Every register kept
vp_lyearly:
    cmp byte [vp_lyb], 0
    je .r
    cmp byte [vp_dton], 0
    je .r
    cmp word [vp_dtn], 3
    jb .r
    push ax
    push bx
    push cx
    push dx
    call vp_lydisk
    call vp_lyunc
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; vp_lyxqset - [vp_lyxq]: this session copies through the layer's bank - one
; it has, a bounce claimed for it, and not under the speaker's clock, whose
; own interrupts every XMS copy holds off (98.3.18.5). Every register kept
vp_lyxqset:
    mov byte [vp_lyxq], 0
    cmp word [vp_lyxn], 0
    je .r
    cmp byte [vp_lybnc], 0
    je .r
    cmp byte [vp_snd], VP_SPK
    je .r
    mov byte [vp_lyxq], 1
.r:
    ret

; vp_dtime - vp_fill's disk chunk just read, AX its bytes: while the ring is
; filled before a session's first frame ([vp_dton]), the hook's clock at its
; end - the first chunk's is where the span starts (its read holds the seek
; from the header), each whole one after it moves the end. Every register
; kept
vp_dtime:
    cmp byte [vp_dton], 0
    je .r
    cmp ax, VP_CHUNK                ; (the stream's short last: not a rate)
    jne .r
    push ax
    push dx
    call vp_dstamp                  ; DX:AX = the bracket's clock, PIT counts
    cmp word [vp_dtn], 0
    jne .e
    mov [vp_dt0], ax
    mov [vp_dt0+2], dx
.e:
    mov [vp_dt1], ax
    mov [vp_dt1+2], dx
    inc word [vp_dtn]
    pop dx
    pop ax
.r:
    ret

; vp_dstamp - DX:AX = THE BRACKET'S CLOCK in PIT counts (98.1.9.4): the
; hook's periods x [vp_pdiv], and how far into the period the counter is -
; channel 0 counts down from [vp_pdiv] once a period (mode 2, sch_pit0), so
; 0.84 us where the periods alone were a whole frame's. An IRQ0 already
; raised and not yet taken (the PIC's IRR) is a period the hook has not
; counted - when the latched count is past half, the reload came before the
; latch. Every other register kept
vp_dstamp:
    push bx
    push cx
    pushf
    cli
    xor al, al
    out 0x43, al                    ; (latch channel 0)
    in al, 0x40
    mov cl, al
    in al, 0x40
    mov ch, al                      ; CX = the count, down from [vp_pdiv]
    mov al, 0x0A                    ; (OCW3: read the IRR)
    out 0x20, al
    in al, 0x20
    mov bl, al
    mov ax, [vp_hper]
    popf
    cmp byte [vp_snd], VP_SPK       ; (UNDER THE SPEAKER the counter counts
    jne .nsp                        ; a sample, not a period: whole periods)
    mov cx, [vp_pdiv]
    jmp short .np
.nsp:
    test bl, 1
    jz .np
    mov dx, [vp_pdiv]
    shr dx, 1
    cmp cx, dx
    jbe .np
    inc ax                          ; (raised, not yet counted)
.np:
    mul word [vp_pdiv]              ; DX:AX = the periods' counts...
    mov bx, [vp_pdiv]
    sub bx, cx                      ; ...and this one's so far
    add ax, bx
    adc dx, 0
    pop cx
    pop bx
    ret

; vp_dtneed - CF=0: the ring's fill is being timed for THE DISK's verdict
; and none is kept yet - a file with both disks and the layer's the faster,
; the layer still on - so the chunk comes off the disk even where the XMS
; hold has it (98.1.9.7): a file loaded into the hold before Play otherwise
; filled the ring without one disk read and was never timed. Not past a
; bank's head - its chunks are the stream's next, and a read around them
; would leave the bank behind the cursor. Every register kept
vp_dtneed:
    cmp byte [vp_dton], 0
    je .no
    cmp byte [vp_lydv], 0
    jne .no
    push ax
    mov ax, [vp_lybks]
    or ax, ax
    jz .nop
    cmp [vp_lykbs], ax
    jbe .nop
    cmp byte [vp_xbank], 0
    je .yes
    cmp word [vp_xcnt], 0
    jne .nop
.yes:
    pop ax
    clc
    ret
.nop:
    pop ax
.no:
    stc
    ret

; vp_lydisk - the ring just filled (98.1.9.3): THE DISK's verdict on the
; layer, where the CPU's could not tell the machines apart - a file with the
; base's disk (V88_LYBKS) under the layer's, the layer still on, and two
; whole chunks timed past the first. This machine's KB/s below the mean of
; the two: the layer is not read. Once a file, kept ([vp_lydv]): a later
; session's ring may come out of the bank. Clobbers AX, BX, CX, DX
vp_lydisk:
    mov byte [vp_dton], 0
    cmp byte [vp_lyon], 0
    je .r
    mov al, [vp_lydv]
    cmp al, 1
    je .r                           ; (said yes already)
    cmp al, 2
    je .off                         ; (said no already)
    mov bx, [vp_lybks]
    or bx, bx
    jz .cpu                         ; (no base's disk in the file)
    mov cx, [vp_lykbs]
    cmp cx, bx
    jbe .cpu                        ; (the layer's disk no faster)
    mov ax, [vp_dtn]
    sub ax, 3
    jnb .tm                         ; (fewer than two timed: no verdict)
.none:                              ; NO VERDICT IS THE BASE (98.1.9.7): a
    mov al, 3                       ; machine nothing has shown can carry
    jmp short .offw                 ; the layer does not read it - kept for
                                    ; this session only, so a later one that
                                    ; can time may yet say yes
.cpu:                               ; NO DISK TEST IN THE FILE: the CPU's
    cmp word [vp_lytl], VP_LYTMIN   ; yes, or nothing - timed now, or kept
    jb .none                        ; from key 0 (98.1.9.7, 98.1.9.12)
    cmp word [vp_lytm], 0
    jne .r
    cmp byte [vp_lycv], 1
    jne .none
.r:
    ret
.off:
    mov al, 1                       ; (the disk's no: [vp_lywhy] 1)
.offw:
    mov byte [vp_lyon], 0
    mov byte [vp_lyoff], 1
    mov [vp_lywhy], al
    ret
.yes:
    mov byte [vp_lydv], 1
    ret
.tm:
    add ax, 2                       ; AX = the chunks timed
    push bx
    push cx
    mov cl, 5
    shl ax, cl                      ; ...KB
    push ax
    mov ax, [vp_dt1]
    mov dx, [vp_dt1+2]
    sub ax, [vp_dt0]                ; the PIT counts they took
    sbb dx, [vp_dt0+2]
    mov bl, ah                      ; >> 8: units of 214.5 us, 14 s at most
    mov bh, dl
    pop ax                          ; (the KB)
    or dh, dh
    jnz .slow
    or bx, bx
    jz .fast
    mov cx, 4661                    ; KB x 4661 / the units = KB a second
    mul cx
    cmp dx, bx
    jae .fast
    div bx
    jmp short .kbs
.slow:
    xor ax, ax
    jmp short .kbs
.fast:
    mov ax, 0xFFFF
.kbs:
    pop cx
    pop bx
    mov [vp_lydkb], ax
    add bx, cx
    rcr bx, 1                       ; THE LINE: the mean of the two disks,
    mov dx, cx                      ; or 15/16 of the layer's where that is
    shr dx, 1                       ; more - a two-pass layer spends the
    shr dx, 1                       ; whole of that machine's disk, so a disk
    shr dx, 1                       ; much under it starves the base
    shr dx, 1
    neg dx
    add dx, cx
    cmp dx, bx
    jbe .ln
    mov bx, dx
.ln:
    mov [vp_lydln], bx
    cmp ax, bx
    jae .yes
    mov byte [vp_lydv], 2
    jmp short .off

; vp_lysplit - a file's first session, the CPU just timed (vp_lybench): the
; pool SPLIT for the machine this is (98.1.9.2). Both banks were taken at
; the open, before anything knew - the base's to its ask, the layer's from
; what was left. THE BASE'S MACHINE (the layer not read) gives the layer's
; back and takes the whole pool (98.1.9.11); THE LAYER'S keeps of the base's only what it needs to carry the base
; ([vp_lyneed], 0: none) and gives the layer the rest, to its ask. Once a
; file, at its first session, before any prefill - the banks are empty -
; and only when the CPU WAS timed: untimed, the base's bank is not risked.
; Not on EMS, whose bank is the base's alone. Clobbers AX, BX, CX, DX
vp_lysplit:
    cmp byte [vp_lysplt], 0
    je .go
.r0:
    ret
.go:
    mov byte [vp_lysplt], 1
    cmp byte [vp_xems], 0
    jne .r0
    cmp word [vp_lybkb], 0          ; (no layer bank asked: nothing to split)
    je .r0
    cmp byte [vp_lydv], 2           ; THE DISK said no (98.1.9.3): this IS
    je .base                        ; the base's machine, timed or not
    cmp byte [vp_lywhy], 3          ; ...or nothing said yes (98.1.9.7): the
    je .base                        ; base keeps its bank
    cmp word [vp_lytm], 0           ; NOT TIMED (a one-bit file from its
    je .r0                          ; first frame has no key to time): no
    cmp byte [vp_lyon], 0           ; knowing - the base keeps its bank
    jne .lay
.base:
    call vp_lyxfree                 ; THE BASE'S MACHINE: no layer read -
    cmp byte [vp_xbank], 0          ; and the base's bank the WHOLE pool, as
    je .r                           ; a layerless file's is (98.3.18.2.1):
    call vp_xcaps                   ; the open capped it at its ask so the
    and al, 0xE0                    ; layer could have the rest (98.1.9.11).
    or ax, ax                       ; Empty yet - the split is before any
    jz .r                           ; prefill - so let go and taken again
    mov bx, [vp_xbn]                ; that much bigger; where the pool will
    mov cl, 5                       ; not give that in one run, the size it
    shl bx, cl                      ; was, whose run was just let go
    add ax, bx
    jc .r
    push bx
    push ax
    call vp_xfree
    pop ax
    call vp_balloc
    pop bx                          ; (BX = the KB it had)
    cmp byte [vp_xbank], 0
    je .back
    mov ax, [vp_xbn]
    mov cl, 5
    shl ax, cl
    cmp ax, bx
    jae .r
    call vp_xfree
.back:
    mov ax, bx
    jmp vp_balloc
.lay:
    cmp byte [vp_xbank], 0          ; (held whole, or no bank: the layer's
    je .ly                          ; is already from what is left)
    mov ax, [vp_xbn]
    mov cl, 5
    shl ax, cl                      ; the base's bank, KB
    mov dx, [vp_lyneed]             ; ...what it needs, and what its ring
    add dx, [vp_lycmp]              ; lent the layer (98.1.9.4) - a bank
    cmp word [vp_lycmp], 0          ; it can have: the least is VP_XBMIN, so
    je .nc                          ; a smaller pay-back is that much
    cmp dx, VP_XBMIN
    jae .nc
    mov dx, VP_XBMIN
.nc:
    cmp ax, dx
    jbe .ly                         ; (no more than it needs)
    call vp_xfree                   ; ...down to what it needs: let go, and
    mov ax, dx                      ; taken again that size - or none
    or ax, ax
    jz .ly
    call vp_balloc
.ly:
    call vp_lyxfree                 ; THE LAYER'S, from what is left now
    call vp_lyxopen
    call vp_lyxqset                 ; ...and used from THIS session on
.r:
    ret

; vp_xfree - the hold, if there is one
vp_xfree:
    call vp_lyxfree
    cmp byte [vp_xon], 0
    je .z
    push ax
    push dx
    mov byte [vp_xon], 0
    mov byte [vp_xbank], 0
    cmp byte [vp_xems], 0
    jne .e
    mov ax, [vp_xbase]
    mov dx, [vp_xbase+2]
    call OSAPI_XMEM_FREE
    jmp short .x
.e:
    mov byte [vp_xems], 0           ; EMS: the pages and the two quarters
    push bx                         ; (EMSV_GONE would take both at the
    mov ax, [vp_ehnd]               ; close; another file's open may want
    mov bx, (DRVC_EMS << 8) | EMSV_FREE ; them now)
    call OSAPI_DRV_CALL
    mov al, [vp_eq]
    mov bx, (DRVC_EMS << 8) | EMSV_UNFRAME
    call OSAPI_DRV_CALL
    pop bx
.x:
    pop dx
    pop ax
.z:
    ret

; vp_xcopy - ES:SI = conventional, DX:AX = an offset in the FILE, CX = bytes
; (even, <= 32 KB), DI = 0 up into the hold / 1 down out of it. CF=1: the
; copy was refused, and the hold is dropped - the disk serves from then on
vp_xcopy:
    push ax
    push dx
    add ax, [vp_xbase]
    adc dx, [vp_xbase+2]
    call OSAPI_XMEM_COPY
    pop dx
    pop ax
    jnc .ok
    call vp_xfree
    stc
.ok:
    ret

; vp_xin - DX:AX = an offset, CX = bytes past it: CF=0 every one of them is
; held. Every register kept
vp_xin:
    cmp byte [vp_xon], 0
    je .no
    push ax
    push dx
    add ax, cx
    adc dx, 0
    cmp dx, [vp_xhave+2]
    jne .c
    cmp ax, [vp_xhave]
.c:
    pop dx
    pop ax
    ja .no
    clc
    ret
.no:
    stc
    ret

; vp_xrdat - vp_rdat's range, DX:AX = its cluster's offset and CX = the bytes
; from there, copied to [vp_rdseg]:0 if all of it is held. CF=1: it is not,
; and the disk reads it. Every register kept
vp_xrdat:
    call vp_xin
    jc .r
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    inc cx
    and cl, 0xFE                    ; even: the byte past is in the block
    mov es, [vp_rdseg]              ; (a KB over the file) and the claim
    xor si, si
    mov di, 1
.pc:
    mov bx, cx                      ; at most 32 KB a copy
    cmp bx, VP_CHUNK
    jbe .p1
    mov bx, VP_CHUNK
.p1:
    push cx
    mov cx, bx
    call vp_xcopy
    pop cx
    jc .out
    add si, bx
    add ax, bx
    adc dx, 0
    sub cx, bx
    jnz .pc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; vp_xfill - vp_fill's chunk out of the hold, DX = its slot: at the stream's
; cursor, a whole chunk held or the file's last. out CF=0 AX = the bytes and
; the cursor past them - zeroed but for its place, so a read from the disk
; after it seeds again from the name (18.4.8); CF=1 the disk's. Every other
; register kept
vp_xfill:
    cmp byte [vp_snd], VP_SPK       ; THE SPEAKER'S CLOCK (vp_xquiet): the
    jne .nq                         ; disk serves, the bank handed back -
    cmp byte [vp_xems], 0           ; unless it is EMS, whose copy is a
    je vp_xquiet                    ; rep movsw with interrupts ON
.nq:
    cmp byte [vp_xbank], 0          ; A BANK (VIDEO-XMS-PLAN 4.1): its head
    jne vp_bfill                    ; chunk, if it has one
    cmp byte [vp_xon], 0
    je .no
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, dx
    mov ax, [vp_cur+FSEQ_OFF]
    mov dx, [vp_cur+FSEQ_OFF+2]
    mov cx, VP_CHUNK
    call vp_xin
    jnc .n                          ; a whole chunk
    cmp byte [vp_xfull], 0
    je .nop                         ; not there yet: the disk's
    mov cx, [vp_xhave]              ; the file's last: what is left of it,
    mov bx, [vp_xhave+2]            ; under a chunk (or none, at the end)
    sub cx, ax
    sbb bx, dx
    jnc .n
    xor cx, cx
.n:
    jcxz .adv
    push cx
    inc cx
    and cl, 0xFE
    xor si, si
    mov di, 1
    call vp_xcopy
    pop cx
    jc .nop
.adv:
    push ds
    pop es
    mov di, vp_cur
    xor ax, ax
    push cx
    mov cx, FSEQ_OFF / 2
    cld
    rep stosw
    pop cx
    add [vp_cur+FSEQ_OFF], cx
    adc word [vp_cur+FSEQ_OFF+2], 0
    mov ax, cx
    clc
    jmp short .out
.nop:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.no:
    stc
    ret

; vp_xsay - the card's line 6 while nothing has played: what the hold has
vp_xsay:
    cmp byte [vp_xon], 0
    je .r
    mov di, vp_lines + 6 * VP_LINE
    cmp byte [vp_xbank], 0
    je .hold
    mov si, vp_s_xbank              ; 'XMS bank: 3136 KB', or with a session
    cmp byte [vp_xems], 0           ; banking, 'XMS bank: 320 of 3136 KB' -
    je .xs                          ; or 'EMS bank: ...'
    mov si, vp_s_ebank
.xs:
    mov ax, [vp_xcnt]
    or ax, ax
    jz .bz
    mov cl, 5
    shl ax, cl
    call vp_psw
    mov si, vp_s_of
.bz:
    mov ax, [vp_xbn]                ; (after the name, or ' of ')
    mov cl, 5
    shl ax, cl
    call vp_psw
    jmp short .u
.hold:
    mov si, vp_s_xheld
    cmp byte [vp_xfull], 0
    jne .h
    mov si, vp_s_xin
.h:
    call vp_puts
    mov ax, [vp_xhave]
    mov dx, [vp_xhave+2]
    call .kb
    cmp byte [vp_xfull], 0
    jne .u
    mov si, vp_s_of
    call vp_puts
    mov ax, [vp_xcap]
    mov dx, [vp_xcap+2]
    call .kb
.u:
    mov si, vp_s_kb
    call vp_puts
.r:
    ret
.kb:                                ; DX:AX bytes, in KB
    mov cx, 1024
    call vp_div32
    xor bl, bl
    jmp vp_putn

; vp_xput - a chunk the stream just read, DX:AX = its file offset, CX = its
; bytes, ES = its slot at 0: whatever of it lies at the front of the hold
; goes up behind what is there. Every register kept
vp_xput:
    cmp byte [vp_xbank], 0          ; (a bank keeps nothing behind the play)
    jne .r
    cmp byte [vp_snd], VP_SPK       ; (nor does a copy run under the speaker's
    je .r                           ; clock: vp_xquiet)
    cmp byte [vp_xon], 0
    je .r
    cmp byte [vp_xfull], 0
    jne .r
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [vp_xhave]              ; DI:BX = where the hold ends
    mov di, [vp_xhave+2]
    sub bx, ax                      ; ...less the chunk's start: how far into
    sbb di, dx                      ; the chunk that is
    jc .out                         ; the hold ends before it: a gap
    jnz .out                        ; ...or long after it
    cmp bx, cx
    ja .out                         ; past its end: nothing new
    mov si, bx                      ; SI = the first new byte in the slot
    push cx
    sub cx, bx                      ; CX = the new bytes
    call vp_xend                    ; DX:AX = the hold's end, checked
    jc .popo
    jcxz .tail
    push cx
    inc cx
    and cl, 0xFE
    xor di, di
    call vp_xcopy
    pop cx
    jc .popo
    add [vp_xhave], cx
    adc word [vp_xhave+2], 0
.tail:
    pop cx
    cmp cx, VP_CHUNK                ; a short chunk is the file's end
    je .out
    mov byte [vp_xfull], 1
    jmp short .out
.popo:
    pop cx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; vp_xend - CX bytes about to go up at the hold's end: DX:AX = that end, and
; CF=1 (the hold dropped) if they would not fit the block - the file grew
; since it was sized
vp_xend:
    mov ax, [vp_xhave]
    mov dx, [vp_xhave+2]
    push ax
    push dx
    add ax, cx
    adc dx, 0
    add ax, 1                       ; (the even copy's byte past)
    adc dx, 0
    cmp dx, [vp_xcap+2]
    jne .c
    cmp ax, [vp_xcap]
.c:
    pop dx
    pop ax
    jbe .ok
    call vp_xfree
.stc:                               ; (vp_xstep's "nothing left", too)
    stc
    ret
.ok:
    clc
    ret

; vp_xstep - the next chunk into the hold, through a claim of its own, on its
; own cursor. out CF=1 nothing is left to load (or the hold is gone)
vp_xstep:
    cmp byte [vp_xbank], 0          ; a bank fills while a play reads, not on
    jne vp_xend.stc                 ; the timer
    cmp byte [vp_xon], 0
    je vp_xend.stc
    cmp byte [vp_xfull], 0
    jne vp_xend.stc
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, VP_CHUNK / 1024
    call vp_xgpad                   ; (+ THE GAP's slack, BX)
    call OSAPI_MEM_CLAIM
    jnc .got
    sub ax, bx                      ; ...or without it
    xor bx, bx
    call OSAPI_MEM_CLAIM
    jc .later                       ; no room now: the next tick asks again
.got:
    mov [vp_xsclm], dx
    or bx, bx
    jz .nal
    mov ax, [vp_xbase]              ; the chunk goes up to the hold's end:
    add ax, [vp_xhave]              ; the read starts that far from it
    call vp_xgal
.nal:
    push dx
    mov ax, [vp_xhave]              ; the cursor where the hold ends - the
    mov bx, [vp_xhave+2]            ; stream's chunks may have moved it
    cmp ax, [vp_xcur+FSEQ_OFF]
    jne .seed
    cmp bx, [vp_xcur+FSEQ_OFF+2]
    je .rd
.seed:
    push ds
    pop es
    mov di, vp_xcur
    push ax
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    pop ax
    mov [vp_xcur+FSEQ_OFF], ax
    mov [vp_xcur+FSEQ_OFF+2], bx
.rd:
    mov ax, [vp_xbase]              ; THE TARGET (98.3.18.15): the hold's
    mov bx, [vp_xbase+2]            ; end, and the room it has from there
    add ax, [vp_xhave]
    adc bx, [vp_xhave+2]
    mov [vp_htg], ax
    mov [vp_htg+2], bx
    mov cx, [vp_xcap]
    mov bx, [vp_xcap+2]
    sub cx, [vp_xhave]
    sbb bx, [vp_xhave+2]
    jz .room
    mov cx, VP_CHUNK
.room:
    call vp_harm
    xor bx, bx                      ; DX:BX = the claim
    mov cx, VP_CHUNK
    push ds
    pop es
    mov di, vp_xcur
    mov si, vp_name
    call OSAPI_FILE_READ_SEQ        ; AX = the bytes (DX 0: one chunk)
    call vp_hdis                    ; (flags kept)
    pop dx
    jc .drop                        ; the disk failed: the stream will say so
    mov es, dx
    push dx
    mov cx, ax
    call vp_xend                    ; DX:AX = where it goes
    jc .free
    jcxz .end
    xor si, si
    xor di, di
    push cx
    inc cx
    and cl, 0xFE
    call vp_xcopym                  ; (less what HDD.DRV put there)
    pop cx
    jc .free
    add [vp_xhave], cx
    adc word [vp_xhave+2], 0
.end:
    cmp cx, VP_CHUNK
    je .free
    mov byte [vp_xfull], 1          ; a short chunk: the file's end
.free:
    pop dx
    jmp short .fr
.drop:
    call vp_xfree
.fr:
    mov dx, [vp_xsclm]              ; (the claim, not where the read began)
    call OSAPI_MEM_FREE
.later:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
    nop                             ; (never run: what follows at the parity
                                    ; it had - tools/os88rdiff.py --hot)

; =============================================================================
; THE BANK (docs/plans/VIDEO-XMS-PLAN.md 4): a file BIGGER than the pool. The
; block is a FIFO of [vp_xbn] 32 KB slots holding the stream's chunks AHEAD
; of the ring, in order: [vp_xcnt] of them from slot [vp_xhs], the head being
; the chunk the ring takes next, at file offset [vp_xlo].
;
; Two cursors, and they are HANDED OVER, never sought again: while the bank
; is empty the stream's cursor [vp_cur] reads the disk straight into the ring
; as it always did; the bank starts by COPYING it into [vp_xcur] and reads on
; from there, and when the bank drains the copy goes back. A re-seed walks
; the chain from the file's front (18.4.8), which once a play is a cost the
; whole-file hold could pay and a bank, which empties and refills many times,
; could not. The one re-seed is the failure path: a copy refused, the block
; dropped, and the stream's cursor sought to the head's offset.
; =============================================================================

; =============================================================================
; THE BANK IN EXPANDED MEMORY (SPEC.md 98.3.18.6, VIDEO-XMS-PLAN 10.3's E2):
; with no XMS pool and EMS.DRV answering, the bank's slots are pairs of
; 16 KB pages of one handle, and a slot is reached through the frame's first
; two quarters, held for the file's life. A fill READS INTO the mapped pages
; - the disk's read is the whole of it - and a drain is one rep movsw out of
; the frame with interrupts on, where XMS reads into a bounce, copies up and
; copies down with them off. Everything else - the FIFO, the cursors, the
; prefill, the pause, the seek - is the XMS bank's, unchanged
; =============================================================================

; vp_eprobe - CF=0: EMS.DRV answers and the frame's quarters 0 and 1 are
; free. Every register kept
vp_eprobe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di                         ; (vp_ld6's rung, the even one)
    mov bl, EMSV_CAPS
    call vp_ems                     ; AX free, CX on the board, DX the frame,
    jc .out                         ; SI the quarters nobody holds
    and si, 3
    cmp si, 3
    je .out                         ; (CF=0: JE was on equal)
    stc
.out:
    jmp vp_ld6

; vp_epref - CF=0: take EMS though there is an XMS pool - EMS.DRV answers
; (it is only loaded when somebody ticked it, which is the request) and the
; board costs the play less than the pool's copies would (98.3.18.15). EMS
; in place copies nothing; its price is the frame's reads, [vp_er10] tenths
; of RAM's a KB, so (er10 - 10) tenths over RAM. The pool's price is its
; copies: a KB copied is [vp_xr10] tenths of RAM's read (vp_xspeed), once a
; banked byte where the decode goes in place (a 286 whose LOADALL has not
; failed this instance, or a 386 - 98.3.18.14) and twice where it copies.
; EMS wins when its price is the smaller. A board within VP_EFAST of RAM wins
; on every tier without the pool being timed: no copy is that cheap. It
; used to be a TIER test - EMS below a 386, XMS from one - written while a
; 386 always copied twice and on a copy rate nobody had measured. Every
; register kept
vp_epref:
    push ax
    push bx
    push cx
    push dx
    call vp_eprobe
    jc .no
    call vp_xcaps                   ; no pool (every 8088): EMS is all there
    or ax, ax                       ; is, and nothing is timed
    jz .yes
    call vp_espeed
    cmp byte [vp_eslow], 0          ; as fast as RAM: EMS, nothing to weigh
    je .yes
    cmp byte [vp_eslow], 2          ; too slow to bank in at all (98.3.18.8):
    je .no                          ; the pool, whatever it costs
    call vp_xspeed                  ; AX = an XMS KB copied, RAM tenths
    jc .no                          ; (not timed: the pool, as before)
    cmp byte [vp_tier], CPU_386     ; in place: ONE copy a banked byte
    jae .one
    cmp byte [vp_xchk], 2           ; (a 286 whose LOADALL failed: two)
    jne .one
    shl ax, 1
.one:
    mov bx, [vp_er10]
    sub bx, 10                      ; EMS's price over RAM's
    jbe .yes
    cmp bx, ax
    jae .no
.yes:
    clc
    jmp short .r
.no:
    stc
.r:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_xspeed - AX = the pool's copy of a KB in tenths of RAM's read of one
; ([vp_xr10], measured once an instance: FFFFh until then). Taken with
; OSAPI_XMEM_COPY itself - VP_XSPKB KB up out of our own image into a block
; claimed for the purpose and freed - against vp_erate's 1 KB `rep lodsw`
; passes over our own segment, two ticks each from a tick's edge, so a
; BIOS's reset, LOADALL's borrow or a 386's base load is priced as what it
; costs a play here and not as an instruction count. CF = 1 not measured
; (no block): the caller keeps the pool. Every other register kept
VP_XSPKB    equ 8                   ; KB a timed copy: a play copies 32 at a
                                    ; time, so a call's fixed part must not
                                    ; be the measurement
vp_xspeed:
    mov ax, [vp_xr10]
    cmp ax, 0xFFFF
    je .t
    clc
    ret
.t:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, VP_XSPKB * 1024
    xor dx, dx
    call OSAPI_XMEM_ALLOC           ; DX:AX = the block
    jc .out
    push ax
    push dx
    call OSAPI_GET_TICKS
    mov bx, ax
.w:
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .w
    mov bx, ax
    xor cx, cx                      ; CX = the copies
.l:
    pop dx
    pop ax
    push ax
    push dx
    push cx
    push ds
    pop es
    xor si, si                      ; (our image: an UP copy only reads it)
    xor di, di
    mov cx, VP_XSPKB * 1024
    call OSAPI_XMEM_COPY
    pop cx
    jc .fr
    inc cx
    call OSAPI_GET_TICKS
    sub ax, bx
    cmp ax, 2
    jb .l
    push cx
    mov ax, ds
    call vp_erate                   ; BX = RAM's 1 KB passes, two ticks
    pop cx
    mov ax, 10
    mul bx                          ; DX:AX = RAM's x 10...
    mov bx, cx
    mov cl, 3                       ; (VP_XSPKB = 8: KB x copies = copies x 8)
    shl bx, cl
    jz .fr
    cmp dx, bx
    jae .fr
    div bx                          ; AX = a KB's copy in RAM's tenths
    mov [vp_xr10], ax
.fr:
    pop dx
    pop ax
    pushf
    call OSAPI_XMEM_FREE
    popf
.out:
    mov ax, [vp_xr10]
    cmp ax, 0xFFFF
    cmc                             ; CF = 1 while unmeasured
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%if VP_XSPKB != 8
%error "vp_xspeed's shl by 3 is VP_XSPKB = 8"
%endif

; vp_espeed - [vp_eslow] (FFh until measured, once an instance): how the
; EMS frame READS against this package's own RAM, 1 KB `rep lodsw` passes
; counted over two ticks each - 0 within VP_EFAST tenths of RAM's time, 1
; within VP_ESLOW, 2 slower; [vp_er10] the ratio in tenths. A PicoMEM v1's
; PSRAM answers with 8 wait states and reads several times slower than a
; 286's RAM, where a Lo-tech board on an XT is RAM's speed. No board: 0.
; Every register kept
vp_espeed:
    cmp byte [vp_eslow], 0xFF
    jne .r
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [vp_eslow], 0
    mov bl, EMSV_CAPS
    call vp_ems                     ; DX = the frame
    jc .out
    push dx
    mov ax, ds
    call vp_erate
    pop ax
    push bx                         ; RAM's passes
    call vp_erate                   ; BX = the frame's
    pop ax
    mov dx, 10
    mul dx                          ; DX:AX = RAM's x 10
    or bx, bx
    jz .slow
    cmp dx, bx
    jae .slow
    div bx                          ; AX = the frame's time in RAM's tenths
    mov [vp_er10], ax
    cmp ax, VP_EFAST
    jbe .out
    mov byte [vp_eslow], 1
    cmp ax, VP_ESLOW
    jbe .out
.slow:
    mov byte [vp_eslow], 2
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; vp_erate - AX = a segment: BX = the 1 KB reads of it in two ticks, from
; a tick's edge. Clobbers AX, CX, DX, SI
vp_erate:
    mov [vp_etseg], ax
    call OSAPI_GET_TICKS
    mov dx, ax
.w:
    call OSAPI_GET_TICKS
    cmp ax, dx
    je .w
    mov dx, ax
    xor bx, bx
.l:
    mov ax, [vp_etseg]
    push ds
    mov ds, ax
    xor si, si
    mov cx, 512
    cld
    rep lodsw                       ; (one prefix: an interrupt resumes it)
    pop ds
    inc bx
    call OSAPI_GET_TICKS
    sub ax, dx
    cmp ax, 2
    jb .l
    ret

; vp_eopen - SI = the file's KB, one over: the bank in EMS - the board's
; free pages (98.3.18.2.1), no more than the file in whole slots, at
; least VP_XBMIN's; and the frame's quarters 0 and 1 for its window.
; Clobbers AX, BX, CX, DX
vp_eopen:
    push si
    mov bl, EMSV_CAPS
    call vp_ems
    jc .out0
    pop si                          ; (CAPS answered the quarters in SI)
    push si
    mov dx, si                      ; THE FILE in whole slots, one over
    shr dx, 1
    shr dx, 1
    shr dx, 1
    shr dx, 1
    add dx, 2
    and dl, 0xFE
    cmp ax, dx
    jbe .t
    mov ax, dx
.t:
    and al, 0xFE                    ; whole slots: pages two at a time
.try:
    cmp ax, VP_XBMIN / 16
    jb .out0
    push ax
    mov bl, EMSV_ALLOC
    call vp_ems                     ; AX = the handle
    pop cx
    jnc .got
    shr cx, 1                       ; no run that long: half, in slots
    and cl, 0xFE
    mov ax, cx
    jmp short .try
.out0:                              ; (.out, where the tries reach it)
    pop si
    ret
.got:
    mov [vp_ehnd], ax
    push cx
    mov al, 0x0F                    ; ALL FOUR quarters, for the decode in
    mov [vp_eq], al                 ; place (98.3.18.7) - else the two the
    mov bl, EMSV_FRAME              ; hybrid needs
    call vp_ems
    jnc .fr
    mov al, 3                       ; (BX as it went: SPEC.md 107.1)
    mov [vp_eq], al
    call vp_ems                     ; DX = the frame's segment
.fr:
    jc .frx                         ; THE RECIPE (SPEC.md 107.4) before CX is
    mov [vp_eport], cx              ; the pages again: quarter 0's register,
    mov [vp_estep], si              ; the step to the next, the bits ORed
    mov [vp_eor], al                ; into a page's value - what the hook
.frx:                               ; maps with, an ISR that may call nothing
    pop cx                          ; (MOV and POP leave CF as FRAME left it)
    jc .nofr
    mov [vp_eseg], dx
    push cx
    mov ax, [vp_ehnd]
    mov bl, EMSV_BASE
    call vp_ems                     ; AX = its first page on the board
    mov [vp_ebase], ax
    pop cx
    shr cx, 1
    mov [vp_xbn], cx                ; its slots
    xor ax, ax
    mov [vp_xhave], ax
    mov [vp_xhave+2], ax
    mov [vp_xfull], al
    call vp_bflush                  ; (AX kept)
    inc ax
    mov [vp_xems], al
    mov [vp_xbank], al
    mov [vp_xon], al
.out:
    pop si
    ret
.nofr:                              ; (taken since the probe: the pages back)
    pop si
    mov ax, [vp_ehnd]
    mov bl, EMSV_FREE
    jmp vp_ems                      ; (...and its ret is ours)

; vp_emap - AX = a slot: its two pages mapped into quarters 0 and 1. CF=1
; the board refused. Every register kept
vp_emap:
    push ax
    push bx
    push cx
    push dx
    shl ax, 1
    mov cx, ax                      ; CX = the slot's first page
    mov dx, [vp_ehnd]
    xor al, al
    mov bx, (DRVC_EMS << 8) | EMSV_MAP
    call OSAPI_DRV_CALL
    jc .out
    inc cx
    mov al, 1
    mov bx, (DRVC_EMS << 8) | EMSV_MAP
    call OSAPI_DRV_CALL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE DECODE IN PLACE (SPEC.md 98.3.18.7, VIDEO-XMS-PLAN 10.3's E3): with an
; EMS bank of all four quarters, the ring IS the board's pages and nothing is
; copied at all. The stream's chunk c lives in slot c mod [vp_kr] - a pair of
; pages - and the hook reads it THROUGH THE FRAME: vp_addr maps the three
; pages from the one under its pointer into quarters 0-2 with its own OUTs
; (the driver's recipe; the hook is an ISR and may call nothing), so a
; super-packet or a record of up to 32 KB from anywhere in a page is
; contiguous there, wherever its pages are - no mirror. The READER has
; quarter 3, a register the hook never writes, and reads into it 16 KB at a
; time. The conventional ring keeps its least, two slots and a mirror, for a
; key's record. Not taken for BIGSP (five pages), sound ahead, Repeat, a
; cluster over 16 KB or Live, which keep the hybrid (98.3.18.6)
; =============================================================================

; vp_einq - at a session's start, with CX = the slots the ring would take:
; CF=0 it plays IN PLACE - [vp_einp] 1, [vp_kr] the board's slots, the
; hybrid's FIFO off and empty. CF=1 it does not. Every register kept
vp_einq:
    mov byte [vp_einp], 0
    push ax
    mov al, [vp_noinp]              ; (a gate's: the hybrid, for the A/B)
    or al, [vp_eslow]               ; (a slow board: the decode would read
                                    ; every record through it, 98.3.18.8)
    or al, [vp_ahead]               ; (sound ahead: its lead is the ring's)
    or al, [vp_livem]
    or al, [vp_rep]                 ; (Repeat: its seam is the ring's)
    jnz .no
    cmp byte [vp_xems], 0
    je .no
    cmp byte [vp_xon], 0
    je .no
    cmp byte [vp_eq], 0x0F
    jne .no
    cmp word [vp_msl], 1            ; (BIGSP: five pages)
    jne .no
    mov ax, [vp_clb]                ; (the reader's 16 KB a call)
    dec ax
    cmp ax, EMS_PAGE - 1
    ja .no
    mov ax, [vp_xbn]
    cmp word [vp_ekr], 0            ; (a gate's cap: a ring that WRAPS, the
    je .kr                          ; reader filling while the hook reads)
    cmp ax, [vp_ekr]
    jbe .kr
    mov ax, [vp_ekr]
.kr:
    mov [vp_kr], ax
    xor ax, ax
    mov [vp_xbank], al              ; (the FIFO's slots are the ring's now)
    inc ax
    mov [vp_einp], al
    call vp_bflush
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; =============================================================================
; THE FILE HELD IN XMS, DECODED IN PLACE (SPEC.md 98.3.18.11; docs/plans/
; LOADALL-PLAN.md wave L1)
;
; On a 286, a session whose file is held WHOLE in XMS (98.3.18) reads every
; record where it lies instead of copying it down: the 286's LOADALL loads
; DS's hidden base with the record's linear address (apps/os88la.inc), so
; DS:SI reaches above 1 MB with no copy and no reset. What makes it fit this
; player with so little changed:
;
;   - THE RING'S BOOKKEEPING STAYS. vp_fill still "reads" chunk after chunk
;     into slots, the hook still waits for [vp_lc] - but a read copies
;     nothing: it notes the file offset the chunk starts at against its slot
;     ([vp_xslo]), and vp_addr turns (chunk, offset) into the hold's base +
;     that + offset. A record across two chunks is contiguous in the hold, so
;     there is no mirror; the conventional ring keeps the least a key's read
;     needs, exactly as 98.3.18.7's EMS decode does.
;   - vp_addr's DX IS A HANDLE (VP_XSEL + 0..15) naming the linear address it
;     computed, and every `mov ds, dx` a ring pointer reaches is VP_DSDX -
;     a LOADALL there, at the instruction that loaded DS, so nothing before it
;     sees a borrowed DS. VP_DSEND, before the matching `pop ds`, puts the
;     API cells back. Each pair is ONE borrow inside its own pushf/cli/popf.
;   - A HIDDEN BASE DIES AT ANY SEGMENT LOAD, every ISR loads DS, so the
;     decode inside a borrow runs with interrupts off and YIELDS (vd_yield,
;     at vdec.inc's VD_YCK sites, every VP_XYB record bytes): the cells back,
;     a moment with interrupts as the borrow found them, a fresh LOADALL at
;     the same SI. The sites are jumps over themselves outside a session in
;     place (vd_ypatch), which is what an 8088 pays for them.
;
; Not taken - the copy plays as before - on anything but a 286 whose LOADALL
; checked out (la_probe, and 16 bytes of the hold read in place against
; OSAPI_XMEM_COPY: a gate a BIOS closed reads 1 MB low and fails it), and
; for the speaker's clock (98.3.18.5), a bank, EMS, Live, RESIDENT, the
; shadow, sound ahead and Repeat - 98.3.18.7's own list, for its reasons.
;
; LA_SIM IS A TEST BUILD, NEVER SHIPPED (tests/vidxip.py): no emulator here
; runs a 286's LOADALL, so on QEMU's 386 a borrow COPIES the window out of
; XMS into a 64 KB shadow through a flat FS and points DS at that - every
; path above runs, the instruction alone is stood in for.
; =============================================================================
VP_XSEL     equ 0xFFF0              ; vp_xaddr's handles: FFF0h + 0..15
VP_XYB      equ 2048                ; record bytes between yields: ~5 ms on
                                    ; the Victory 286 at its worst rate

; VP_DSDX - in place of `mov ds, dx` after a `push ds`, where DX may be a
; ring pointer. A handle borrows (flags pushed, IF = 0, until VP_DSEND)
%macro VP_DSDX 0
    cmp dx, VP_XSEL
    jb %%n
    pushf
    cli
    call vp_xbeg
    jmp short %%d
%%n:
    mov ds, dx
%%d:
%endmacro
; VP_DSEND - before the `pop ds` that ends a VP_DSDX: a borrow's cells back
; and its flags. CLOBBERS FLAGS on either path
%macro VP_DSEND 0
    cmp byte [cs:vp_xact], 0
    je %%n
    call vp_xfin
    popf
%%n:
%endmacro

; vp_xipq - may this session decode in place? CF = 0 and [vp_xip] 1, [vp_kr]
; the slots the reader may run ahead, the yield sites live; CF = 1 not
vp_xipq:
    mov byte [vp_xip], 0
%ifdef VP_NOXIP
    stc
    ret
%else
    cmp byte [vp_xon], 0
    je .no
    cmp byte [vp_xfull], 0          ; (the whole file: the hook never waits
    je .no                          ; on a read)
    mov al, [vp_xbank]
    or al, [vp_xems]
    or al, [vp_livem]               ; (SOUND AHEAD plays: its lead is read
                                    ; into the ring, which nothing fills here)
    or al, [vp_rep]                 ; (Repeat: its seam is the ring's)
    or al, [vp_resid]
    or al, [vp_shadow]
    jnz .no
    cmp byte [vp_snd], VP_SPK       ; (the speaker's clock: 98.3.18.5)
    je .no
    mov al, [vp_noinp]              ; (a gate's: the copying hold, the A/B)
%ifndef LA_SIM
    cmp byte [vp_tier], CPU_8086    ; a 286 by LOADALL, a 386 by a loaded
    jne .tier                       ; base (98.3.18.14)
    inc ax
.tier:
%endif
    or al, al
    jnz .no
    call vp_xok
    jc .no
    mov word [vp_kr], VP_KBIG
    mov al, 1
    mov [vp_xip], al
    call vd_ypatch                  ; the yield sites live
    clc
    ret
.no:
    stc
    ret
%endif

; vp_xipend - a session's end: the sites jumps over themselves again
vp_xipend:
    cmp byte [vp_xip], 0
    je .r
    mov byte [vp_xip], 0
    push ax
    xor al, al
    call vd_ypatch
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; THE XMS BANK, DECODED IN PLACE (SPEC.md 98.3.18.13; docs/plans/LOADALL-PLAN.md
; wave L4). 98.3.18.7's shape on XMS: the ring's slots ARE the bank's -
; chunk c in bank slot c mod [vp_kr] - and [vp_einp] 2 takes every branch
; the EMS decode in place takes (the prefill, the pause's read-on, no Repeat,
; the session's end), with three of its own: the READ goes through a bounce
; (vp_xbread: the disk into the ring's first slot, which the key's decode has
; finished with, then copied UP - LOADALL-PLAN L2 is what would read straight
; into the bank), the ADDRESS is a handle (vp_xbaddr: 98.3.18.11's borrows
; and yields, unchanged), and the WRAP needs a mirror where EMS mapped three
; pages - the bank's last slot, which holds what a super-packet starting in
; slot K-1 runs on into of slot 0 (vp_mneed), as the conventional ring's
; mirror does. What goes is the bank's DRAIN: every chunk's copy DOWN.
; -----------------------------------------------------------------------------

; vp_xbipq - may this session read its XMS bank in place? CF = 0 and
; [vp_einp] 2, [vp_kr] the bank's slots less its mirror (or a gate's cap,
; [vp_ekr]), the FIFO off and the yield sites live; CF = 1 not. Clobbers AX
vp_xbipq:
%ifdef VP_NOXIP
    stc
    ret
%else
    mov al, [vp_noinp]              ; (a gate's: the copying bank, the A/B)
    or al, [vp_xems]                ; (an XMS bank: EMS has its own, above)
    or al, [vp_livem]
    or al, [vp_rep]                 ; (Repeat: its seam is the ring's)
    or al, [vp_shadow]              ; (its decoders have no yield sites)
    jnz .no
    cmp byte [vp_xbank], 0
    je .no
    cmp byte [vp_snd], VP_SPK       ; (the speaker's clock: 98.3.18.5)
    je .no
    mov ax, [vp_msl]                ; (the ring's least, the mirror's slot
    shl ax, 1                       ; past it: 3, and BIGSP's 5 - three
    inc ax                          ; slots a super-packet reads before it
    cmp [vp_xbn], ax                ; is entered, and a mirror of two,
    jb .no                          ; 98.1.4.1)
%ifndef LA_SIM
    cmp byte [vp_tier], CPU_8086    ; a 286 by LOADALL, a 386 by a loaded
    je .no                          ; base (98.3.18.14)
%endif
    call vp_xok
    jc .no
    mov ax, [vp_xbn]
    sub ax, [vp_msl]                ; the last slot or two is the mirror
    cmp word [vp_ekr], 0
    je .kr
    cmp ax, [vp_ekr]
    jbe .kr
    mov ax, [vp_ekr]
.kr:
    push ax                         ; (a gate's cap under the ring's least
    sub ax, [vp_msl]                ; is refused: the copying bank plays)
    pop ax
    jbe .no
    mov [vp_kr], ax
    xor ax, ax
    mov [vp_xbank], al              ; (the FIFO's slots are the ring's now)
    mov byte [vp_einp], 2
    call vp_bflush                  ; (AX kept)
    inc ax
    call vd_ypatch                  ; the yield sites live
    clc
    ret
.no:
    stc
    ret
%endif

; vp_xbaddr - vp_addr in the XMS bank: AX = a chunk, BX = an offset in it ->
; DX = a handle, SI its offset (vp_xmk). Clobbers AX, BX, CX
vp_xbaddr:
    call vp_slot
    xor dx, dx
    shr ax, 1
    rcr dx, 1
    xchg ax, dx                     ; DX:AX = the slot x 32 KB
    add ax, bx
    adc dx, 0
    add ax, [vp_xbase]
    adc dx, [vp_xbase + 2]
    jmp vp_xmk

; vp_xbread - BX = a slot: the stream's next chunk into it - read into the
; BOUNCE, the ring's first slot, and copied up; slot 0's also to the mirror,
; as much as a super-packet before it runs on into. out CF=0 AX = the
; bytes; CF=1 the disk or the copy failed. Every other register kept
vp_xbread:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bx
    xor cx, cx                      ; SLOT 0 IS NOT ARMED: its mirror is
    cmp bx, [vp_msl]                ; copied up from the bounce as well -
    jb .h0                          ; nor slot 1, BIGSP's second (98.1.4.1)
    mov ax, bx
    call vp_bslot                   ; DX:AX = the slot x 32 KB
    add ax, [vp_xbase]
    adc dx, [vp_xbase + 2]
    mov [vp_htg], ax
    mov [vp_htg + 2], dx
    mov cx, VP_CHUNK
.h0:
    mov dx, [vp_ring]
    call vp_harm                    ; (98.3.18.15)
    mov dx, [vp_ring]               ; DX:BX = the bounce (placed at THE GAP
    xor bx, bx                      ; from the bank, 98.3.18.12)
    mov cx, VP_CHUNK
    push ds
    pop es
    mov di, vp_cur
    mov si, vp_name
%ifdef VP_RDLOG
    push word [vp_cur + FSEQ_OFF + 2]   ; (where it starts, for the log)
    push word [vp_cur + FSEQ_OFF]
%endif
    call OSAPI_FILE_READ_SEQ        ; AX = the bytes (DX 0: a chunk)
    call vp_hdis                    ; (flags kept)
%ifdef VP_RDLOG
    pop bx                          ; (DI:BX where it started; BX is the
    pop di                          ; slot again just below)
    jc .nolog
    push es
    mov es, [vp_ring]
    call vp_prlog                   ; (98.3.22.2: the bounce, as it landed)
    pop es
    clc
.nolog:
%endif
    pop bx
    jc .out
%ifdef VP_RDSLOW
    ; A TEST BUILD'S SLOW READER (tests/vidxip.py --arm bankdry): VP_RDSLOW
    ; BIOS ticks after every chunk, so a bank in place runs DRY the way a
    ; PicoMEM at 14 wait states does - the shipped player never has it
    push ax
    push es
    xor ax, ax
    mov es, ax
    mov ax, [es:0x46C]
    add ax, VP_RDSLOW
.slow:
    sti
    cmp ax, [es:0x46C]
    jns .slow
    pop es
    pop ax
%endif
    mov si, ax
    or ax, ax
    jz .ok
    mov cx, ax
    mov ax, bx
    call vp_xbup                    ; into its slot
    jc .out
    cmp bx, [vp_msl]
    jae .ok
    call vp_mneed                   ; slot 0 - and 1, BIGSP's: THE MIRROR,
    jcxz .ok                        ; the bank's last slot or two, K on
    mov ax, [vp_kr]
    add ax, bx
    call vp_xbup
    jc .out
.ok:
    mov ax, si
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; vp_xbup - AX = a bank slot, CX = bytes: the bounce's first CX (rounded up
; to even: both have the room) copied up into it. CF=1 refused. Every
; register kept
vp_xbup:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    xor dx, dx
    shr ax, 1
    rcr dx, 1
    xchg ax, dx                     ; DX:AX = the slot x 32 KB
    add ax, [vp_xbase]
    adc dx, [vp_xbase + 2]
    inc cx
    and cl, 0xFE
    mov es, [vp_ring]
    call vp_xupm                    ; UP, less what HDD.DRV put there
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; THE FILL STRAIGHT INTO XMS (SPEC.md 98.3.18.15, 52.1.4; docs/plans/
; VIDEO-386-PLAN.md section D). A fill - the hold's (vp_xstep), the copying
; bank's (vp_bstep), the bank in place's (vp_xbread) - reads a chunk into a
; conventional buffer and copies it UP. On a 386 whose disk is HDD.DRV's
; rung 1, HDD.DRV can be ARMED for that one buffer: a sector it would put
; there goes to the chunk's XMS address instead, and its bit is set in a
; map handed back at DISARM. The copy up then skips every sector the map
; has (vp_xupm), which on such a disk is all of them - the up copy, the one
; LOADALL-PLAN's L2 was for, is gone. Everything else - a sector the
; kernel's cache served, a disk on rung 0 or a floppy, a 286, no HDD.DRV -
; leaves its bit clear and is copied as before, so the result is exact
; whichever road each sector took.
; =============================================================================

; vp_harm - a fill's read is next: DX = its buffer (offset 0), CX = the
; bytes its target has room for (0: do not arm), [vp_htg] = the XMS linear
; address the buffer's first byte goes to. HDD.DRV armed where it can
; ([vp_harmd] 1). [vp_hmapv] is 0 from here whatever happens, so a copy up
; after a read nobody armed copies the lot. Every register kept
vp_harm:
    mov byte [vp_hmapv], 0
    mov byte [vp_harmd], 0
%ifndef VP_RDLOG                    ; (its log reads the buffer as it landed)
%ifndef VP_NOHARM                   ; (a gate's A/B: tests/vidharm.py)
    cmp byte [vp_hnone], 0          ; (the door said no for good: no HDD.DRV,
    jne .r                          ; or below a 386 - asked once)
    push ax
    push bx
    push cx
    push si
    push di
    and cx, 0xFE00                  ; whole sectors...
    cmp cx, VP_CHUNK                ; ...a chunk's at most
    jbe .n
    mov cx, VP_CHUNK
.n:
    jcxz .p
    mov si, [vp_htg]
    mov di, [vp_htg + 2]
    mov bx, (DRVC_DISK << 8) | HDV_ARM
    call OSAPI_DRV_CALL
    jnc .ok
    cmp ax, 1                       ; 0 no door, 1 no 386: for good; 2 the
    ja .p                           ; gate, 3 the arguments: this read only
    mov byte [vp_hnone], 1
    jmp short .p
.ok:
    mov byte [vp_harmd], 1
.p:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
%endif
%endif
.r:
    ret

; vp_hdis - the fill's read is over: HDD.DRV disarmed and its map in
; [vp_hmap], [vp_hmapv] 1. Every register and the FLAGS kept (it sits
; between a READ_SEQ and the jc that tests it)
vp_hdis:
    pushf
    cmp byte [vp_harmd], 0
    je .r
    push ax
    push bx
    push di
    mov byte [vp_harmd], 0
    mov di, vp_hmap                 ; (ES:DI: the X stub makes ES ours)
    mov bx, (DRVC_DISK << 8) | HDV_DISARM
    call OSAPI_DRV_CALL
    jc .x
    mov byte [vp_hmapv], 1
.x:
    pop di
    pop bx
    pop ax
.r:
    popf
    ret

; vp_xcopym - vp_xcopy's UP copy (DX:AX an offset in the block, ES:0, CX
; bytes), less what HDD.DRV put there already (vp_xupm). Every register kept
vp_xcopym:
    push ax
    push dx
    add ax, [vp_xbase]
    adc dx, [vp_xbase+2]
    call vp_xupm
    pop dx
    pop ax
    jnc .ok
    call vp_xfree
    stc
.ok:
    ret

; vp_xupm - ES:0 copied UP to DX:AX (linear), CX bytes (even), by
; OSAPI_XMEM_COPY - less every sector [vp_hmap] says is there already, when
; [vp_hmapv]; each one counted in [vp_hsk]. CF = 1 a copy refused. Every
; register kept
vp_xupm:
    push si
    push di
    xor si, si
    xor di, di                      ; UP
    cmp byte [vp_hmapv], 0
    jne .map
    call OSAPI_XMEM_COPY            ; (the whole of it: nothing was armed)
    pop di
    pop si
    ret
.map:
    push ax
    push bx
    push cx
    push dx
    push bp
    mov bp, cx                      ; BP = the end
.next:                              ; SI = the next sector's offset
    cmp si, bp
    jae .done
    call .bit
    jz .run
    add si, 512                     ; in XMS already
    add word [vp_hsk], 1
    adc word [vp_hsk + 2], 0
    jmp short .next
.run:
    mov bx, si                      ; BX = a run of sectors to copy
.ext:
    add si, 512
    cmp si, bp
    jae .end
    call .bit
    jz .ext
    jmp short .cp
.end:
    mov si, bp
.cp:
    push ax
    push dx
    push si
    mov cx, si
    sub cx, bx                      ; (even: both ends are)
    add ax, bx
    adc dx, 0
    mov si, bx
    xor di, di
    call OSAPI_XMEM_COPY
    pop si
    pop dx
    pop ax
    jnc .next
    jmp short .out
.done:
    clc
.out:
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    pop di
    pop si
    ret
.bit:                               ; ZF = 1: SI's sector is NOT in XMS
    push bx
    push cx
    mov bx, si
    mov cl, 9
    shr bx, cl                      ; the sector...
    mov cl, bl
    and cl, 7
    shr bx, 1
    shr bx, 1
    shr bx, 1                       ; ...its byte of the map
    mov bl, [vp_hmap + bx]
    shr bl, cl
    test bl, 1
    pop cx
    pop bx
    ret

%ifndef VP_NOXIP
; vp_xok - is in place safe on THIS boot? Once an instance ([vp_xchk] 1 yes,
; 2 no): LOADALL probed with INT 6 hooked, then the hold's first 16 bytes
; read in place against OSAPI_XMEM_COPY's. CF = 1 not. Clobbers AX
vp_xok:
    cmp byte [vp_xchk], 1
    jne .chk
    ret                             ; (CF = 0: the compare was equal)
.chk:
    cmp byte [vp_xchk], 0
    jne .bad
    mov byte [vp_xchk], 2
%ifndef LA_SIM
    cmp byte [vp_tier], CPU_386     ; a 386 loads a base (98.3.18.14): the
    jae .noprobe                    ; compare below is its whole check
    mov byte [la_csacc], 0x93
    call la_capture
    call la_probe
    jc .bad
.noprobe:
%else
    push dx
    mov ax, 64
    call OSAPI_MEM_CLAIM            ; the shadow a simulated borrow fills
    mov [vp_simseg], dx
    pop dx
    jc .bad
%endif
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov si, vp_xsig
    mov ax, [vp_xbase]
    mov dx, [vp_xbase + 2]
    mov cx, 16
    mov di, 1
    call OSAPI_XMEM_COPY            ; the reference, by the driver
    jc .bad2
    mov ax, [vp_xbase]
    mov dx, [vp_xbase + 2]
    call vp_xmk                     ; DX = a handle, SI = 0
    mov di, vp_xsig
    mov cx, 8
    push ds
    VP_DSDX                         ; ...and in place
    repe cmpsw
    lahf
    mov [cs:vp_xzf], ah
    VP_DSEND
    pop ds
    test byte [vp_xzf], 0x40        ; ZF: every word the same
    jz .bad2
    dec byte [vp_xchk]              ; 2 -> 1 (CF = 0 since the test)
    jmp short .pop
.bad2:
    stc
.pop:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.bad:
    stc
    ret
%endif

; vp_xaddr - vp_addr in place: AX = a chunk, BX = an offset in it -> DX = a
; handle, SI = the offset within its paragraph. Clobbers AX, BX, CX
vp_xaddr:
    call vp_slot
    mov si, ax
    shl si, 1
    shl si, 1
    mov ax, [vp_xslo + si]          ; where the chunk starts in the file...
    mov dx, [vp_xslo + si + 2]
    add ax, bx
    adc dx, 0
    add ax, [vp_xbase]              ; ...and so in the hold
    adc dx, [vp_xbase + 2]
; vp_xmk - DX:AX = a linear address -> DX = a handle naming its paragraph,
; SI = its offset in it. Clobbers AX, BX
vp_xmk:
    mov si, ax
    and si, 15
    and ax, 0xFFF0
    mov bl, [cs:vp_xidx]
    inc bl
    and bl, 15
    mov [cs:vp_xidx], bl
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov [cs:vp_xpend + bx], ax
    mov [cs:vp_xpend + bx + 2], dx
    shr bx, 1
    shr bx, 1
    lea dx, [bx - 0x10]              ; = VP_XSEL + BX, mod 64 KB
    ret

; vp_xbeg - VP_DSDX's borrow: DX = a handle, IF = 0 and the caller's flags
; just pushed. DS = the window from here, every general register and ES as
; they were
vp_xbeg:
    push ax
    push bx
    push bp
    mov bp, sp
    mov al, [ss:bp + 9]             ; the flags VP_DSDX pushed: IF (bit 9) is
    and al, 2                       ; whether a yield may open the window
    mov [cs:vp_xif], al
    mov bx, dx
    sub bx, VP_XSEL
    shl bx, 1
    shl bx, 1
    mov ax, [cs:vp_xpend + bx]
    mov [cs:vp_xpc], ax
    mov ax, [cs:vp_xpend + bx + 2]
    mov [cs:vp_xpc + 2], ax
    mov byte [cs:vp_xact], 1
    pop bp
    pop bx
    pop ax
    call vd_ylset
; vp_xld - the borrow itself: DS's base [vp_xpc], ES's whatever ES is. IF = 0.
; Every general register and ES preserved
vp_xld:
%ifndef LA_SIM
    cmp byte [cs:vp_tier], CPU_386
    jae vp_ubld                     ; a 386: a loaded base (98.3.18.14)
    push ax
    push dx
    mov ax, [cs:vp_xpc]
    mov [cs:la_dsc], ax
    mov ax, [cs:vp_xpc + 2]
    mov [cs:la_dsc + 2], ax
    mov ax, cs
    mov [cs:la_dsv], ax             ; (a cache that did not take reads the
    mov ax, es                      ; package's own bytes: a wrong picture)
    mov [cs:la_esv], ax
    call la_lin
    mov [cs:la_esc], ax
    mov [cs:la_esc + 2], dx
    pop dx
    pop ax
    push cs
    pop ds
    jmp la_borrow                   ; ...and its ret is ours
%else
    jmp vp_simld
%endif

; vp_xfin - VP_DSEND's: the cells back, DS = ours, ES as it was. IF = 0
vp_xfin:
    push es
    call vp_xrst
    pop es
    mov byte [vp_xact], 0
    mov word [vd_ylim], 0xFFFF
    ret

; vp_xrst - the borrow's end, either tier's: DS = ours again (a 286's API
; cells put back first, la_restore). IF = 0. ES may be anything after it: the
; callers keep their own. Every general register kept
vp_xrst:
%ifndef LA_SIM
    cmp byte [cs:vp_tier], CPU_386
    jb la_restore                   ; (...and its ret is ours)
%endif
    push cs                         ; a 386's base, or LA_SIM's shadow: one
    pop ds                          ; real-mode load and it is gone
    ret

%ifndef LA_SIM
; vp_ubld - vp_xld on a 386 (SPEC.md 98.3.18.14, 41.15): DS's hidden base
; loaded with [vp_xpc] through os88ub.inc's moment of protected mode - the
; same window LOADALL gives a 286, without the API table borrowed. IF = 0.
; Every general register and ES preserved
cpu 386
vp_ubld:
    push eax
    push cs
    pop ds                          ; (ub_set's rule: DS = our image)
    mov eax, [vp_xpc]
    call ub_set
    pop eax
    ret
cpu 8086
%endif

; vd_ylset - [vd_ylim] = SI + VP_XYB, or FFFFh where that wraps. Preserves
; all but flags
vd_ylset:
    push ax
    mov ax, si
    add ax, VP_XYB
    jnc .l
    mov ax, 0xFFFF
.l:
    mov [cs:vd_ylim], ax
    pop ax
    ret

; vd_yield - vdec.inc's VD_YCK, SI past [vd_ylim]: inside a borrow whose
; caller had interrupts on, the cells back, a moment with them on, and the
; same window borrowed again. THE REGION'S STATE IS KEPT ON THE STACK across
; the moment: an interrupt there may run the hook, whose own borrows reuse
; it. Every register preserved; DS the window again on the way out
vd_yield:
    cmp byte [cs:vp_xact], 0
    je .lim
    cmp byte [cs:vp_xif], 0
    je .lim                         ; (borrowed with interrupts off: none)
    push word [cs:vp_xpc]
    push word [cs:vp_xpc + 2]
    push es
    call vp_xrst
    pop es
    inc word [cs:vp_simyc]          ; (tests/vidxip.py counts: every build
                                    ; since a 386 goes in place on QEMU)
    sti                             ; an interrupt owed since the borrow is
    nop                             ; taken here
    nop
    cli
    pop word [cs:vp_xpc + 2]
    pop word [cs:vp_xpc]
    mov byte [cs:vp_xact], 1
    mov byte [cs:vp_xif], 2
    call vp_xld
.lim:
    jmp vd_ylset

%ifdef LA_SIM
; vp_simld - LA_SIM's borrow (a test build): the 64 KB from [vp_xpc] copied
; into [vp_simseg] through a flat FS armed here, and DS that. A 386 only
cpu 386
vp_simld:
    push eax
    push ecx
    push esi
    push edi
    push es
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, vp_simgdt
    mov [cs:vp_simgdt + 2], eax
    lgdt [cs:vp_simgdt]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short .p1
.p1:
    mov ax, 8
    mov fs, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp short .p2
.p2:
    mov esi, [cs:vp_xpc]
    mov es, [cs:vp_simseg]
    xor edi, edi
    mov ecx, 65520 / 4
.c:
    mov eax, [fs:esi]
    mov [es:di], eax
    add esi, 4
    add di, 4
    loop .c
    pop es
    pop edi
    pop esi
    pop ecx
    pop eax
    mov ds, [cs:vp_simseg]
    ret
cpu 8086
vp_simgdt:
    dw 15
    dd 0
    dw 0
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
%endif


; vp_xfill1 - vp_fill's read, held in place: BX = the chunk's slot. The file
; offset the chunk starts at is noted against the slot, the cursor moved past
; it as vp_xfill moves it, and AX = its bytes - a whole chunk, or what is
; left of the file - on into vp_fill's publishing (no mirror)
vp_xfill1:
    shl bx, 1
    shl bx, 1
    mov ax, [vp_cur + FSEQ_OFF]
    mov dx, [vp_cur + FSEQ_OFF + 2]
    mov [vp_xslo + bx], ax
    mov [vp_xslo + bx + 2], dx
    mov cx, [vp_xhave]              ; what is left of the file from here
    mov bx, [vp_xhave + 2]
    sub cx, ax
    sbb bx, dx
    jc .zero
    or bx, bx
    jnz .whole
    cmp cx, VP_CHUNK
    jbe .have
.whole:
    mov cx, VP_CHUNK
    jmp short .have
.zero:
    xor cx, cx
.have:
    push es
    push ds
    pop es
    mov di, vp_cur                  ; the cursor zeroed but for its offset,
    xor ax, ax                      ; and moved on: the SDK's own seek, as
    push cx                         ; vp_xfill leaves it
    mov cx, FSEQ_OFF / 2
    cld
    rep stosw
    pop cx
    pop es
    add [vp_cur + FSEQ_OFF], cx
    adc word [vp_cur + FSEQ_OFF + 2], 0
    mov ax, cx
    jmp ..@vp_fillgot
; vp_xfillt - vp_xfill1 with THE DISK's verdict owed (98.1.9.7): the chunk
; is READ as well - into the ring's first conventional slot, which in place
; holds nothing past the key already drawn, timed (vp_dtime) and thrown away,
; the hold having it - so a layered file held in place is timed on its first
; play as a copying one is, where it used to be never timed and so never
; layered ("Layer off: not timed", tests/vidlybank.py). Three chunks timed
; and the verdict is taken (vp_lydisk) - two past the first, its own rule -
; so the rest of the ring is only noted, not read. BX = the chunk's slot.
; On into vp_fill's publishing with AX = the bytes, as vp_xfill1; a read
; that fails is only noted, as vp_xfill1 would have
vp_xfillt:
    push bx                         ; the chunk's slot
    push word [vp_cur+FSEQ_OFF+2]   ; where it starts in the file
    push word [vp_cur+FSEQ_OFF]
    mov dx, [vp_ring]               ; DX:BX = the scratch
    xor bx, bx
    mov cx, VP_CHUNK
    push ds
    pop es
    mov di, vp_cur
    mov si, vp_name
    call OSAPI_FILE_READ_SEQ        ; AX = the bytes, the cursor moved on
    pop si
    pop di                          ; DI:SI = where it started
    pop bx
    jnc .ok
    mov [vp_cur+FSEQ_OFF], si       ; (failed: the cursor put back, and only
    mov [vp_cur+FSEQ_OFF+2], di     ; noted - vp_xfill1 re-seeks it)
    jmp vp_xfill1
.ok:
    shl bx, 1
    shl bx, 1
    mov [vp_xslo + bx], si          ; noted, as vp_xfill1 notes it
    mov [vp_xslo + bx + 2], di
    call vp_dtime                   ; (the fill timed, 98.1.9.3)
    cmp word [vp_dtn], 3
    jb .got
    push ax
    call vp_lydisk                  ; ...and its verdict, now
    pop ax
.got:
    jmp ..@vp_fillgot

%ifndef LA_SIM
%include "os88la.inc"               ; the borrow (SPEC.md 41.13)
%define UB_DS                       ; ...and a 386's: DS's base alone
%include "os88ub.inc"               ; (SPEC.md 41.15, 98.3.18.14)
    UB_DATA
%endif

; -----------------------------------------------------------------------------
; THE GAP (os88xgap.inc, docs/reports/XMS-GAP-VICTORY286-2026-10-15.md): on a
; page-interleaved board a copy between conventional memory and XMS runs up
; to 1.8x faster with its two ends half the board's period apart. Measured
; once an instance against the first block taken; the ring - every slot,
; the bank's bounce and a key's read 32 KB on from its start - and the
; hold's own read claim then start that far from the XMS they copy against.
; -----------------------------------------------------------------------------
%ifdef VP_DIAG
%define XG_TIMES                    ; (each gap's count, for the trailer)
%endif
%include "os88xgap.inc"

; vp_xgm - the gap measured against [vp_xbase] (empty yet), once an
; instance: [vp_xgap] = G, or 0. Every register kept
vp_xgm:
    cmp byte [vp_xgok], 0
    jne .r
    push ax
    push dx
    mov ax, [vp_xbase]
    mov dx, [vp_xbase+2]
    call xg_gap
    jc .no
    mov [vp_xgap], ax
    mov byte [vp_xgok], 1
.no:
    pop dx
    pop ax
.r:
    ret

; vp_xgpad - AX = a claim's KB: + the gap's slack, 2G in KB, where the claim
; will copy against the hold or the bank (not EMS, not in place); BX = the
; slack added, 0 none. Every other register kept
vp_xgpad:
    xor bx, bx
    cmp word [vp_xgap], 0
    je .r
    cmp byte [vp_xon], 0
    je .r
    cmp byte [vp_xems], 0
    jne .r
    cmp byte [vp_xip], 0
    jne .r
    mov bx, [vp_xgap]               ; 2G bytes, in whole KB
    add bx, 511
    push cx
    mov cl, 9
    shr bx, cl
    pop cx
    add ax, bx
.r:
    ret

; vp_xgal - DX = a claim, AX = the low word of the XMS address its start
; copies against: DX moved up into the claim's slack so that (DX x 16 - AX)
; mod 2G = G. Clobbers AX
vp_xgal:
    push bx
    push cx
    mov bx, [vp_xgap]
    add ax, bx                      ; X + G
    push dx
    mov cl, 4
    shl dx, cl                      ; C
    sub ax, dx                      ; X + G - C
    shl bx, 1
    dec bx
    and ax, bx                      ; mod 2G
    shr ax, cl                      ; paragraphs (G >= 512, C x 16 whole)
    pop dx
    add dx, ax
    pop cx
    pop bx
    ret

; vp_bkind - [vp_lbk] = the bank this session plays through, for the card
; after it (vp_bksay): 0 none, 1 XMS, 2 EMS, 3 EMS in place, 4 the file held
; whole in XMS, 5 an XMS bank the speaker keeps idle (vp_xquiet), 6 none
; because the EMS board is too slow (98.3.18.8). Every register kept
vp_bkind:
    push ax
    xor al, al
    cmp byte [vp_xon], 0
    jne .on
    cmp byte [vp_eslow], 2          ; (a board too slow to bank in, and no
    jne .s                          ; pool: 98.3.18.8)
    mov al, 6
    jmp short .s
.on:
    mov al, 8                       ; (the XMS bank in place: 98.3.18.13)
    cmp byte [vp_einp], 2
    je .s
    mov al, 3
    cmp byte [vp_einp], 0
    jne .s
    mov al, 7                       ; (held, decoded in place: 98.3.18.11)
    cmp byte [vp_xip], 0
    jne .s
    mov al, 4
    cmp byte [vp_xbank], 0
    je .s
    mov al, 2
    cmp byte [vp_xems], 0
    jne .s
    mov al, 5
    cmp byte [vp_snd], VP_SPK
    je .s
    mov al, 1
.s:
    mov [vp_lbk], al
    pop ax
    ret

; vp_bksay - DI = a card line: what the last play banked in, and how much
vp_bksay:
    mov al, [vp_lbk]
    cbw
    xchg ax, bx
    shl bx, 1
    mov si, [vp_bknames + bx]
    cmp bl, 2 * 7                   ; in place (7, 8) on a 386: its own
    jb .nm                          ; words (98.3.18.14)
    cmp byte [vp_tier], CPU_386
    jb .nm
    mov si, [vp_bknames + bx + 4]
.nm:
    call vp_puts
    dec bx                          ; BX = 2 x the kind - 2: 0 to 4 for the
    dec bx                          ; three that have a size, 1 to 3 - and
    cmp bx, 2 * 8 - 2               ; 8, the XMS bank in place
    je .sz
    cmp bx, 4
    ja .r
.sz:
    mov ax, [vp_xbn]
    mov cl, 5
    shl ax, cl
    call vp_putw
    mov si, vp_s_kb
    call vp_puts
.r:
    ret

; vp_eend - the session is over: in place no more - the board's slots the
; hybrid's bank again, emptied. Every register kept
vp_eend:
    cmp byte [vp_einp], 0
    je .r
    push ax
    xor ax, ax
    cmp byte [vp_einp], 2           ; (the XMS bank's: the yield sites off)
    jne .e
    call vd_ypatch                  ; (AX kept)
.e:
    mov [vp_einp], al
    mov byte [vp_xbank], 1
    call vp_bflush
    mov ax, [vp_k]
    mov [vp_kr], ax
    pop ax
.r:
    ret

; vp_eread - BX = a slot: the stream's next chunk into its two pages through
; quarter 3, 16 KB a call. out CF=0 AX = the bytes; CF=1 a disk error. Every
; other register kept
vp_eread:
    cmp byte [vp_einp], 2           ; (the XMS bank's, through the bounce)
    je vp_xbread
    push bx
    push cx
    push dx
    push si
    push di
    push es
    shl bx, 1                       ; its first page
    call .half
    jc .out
    cmp ax, EMS_PAGE
    jne .ok                         ; short: the file's end
    push ax
    inc bx
    call .half
    pop cx
    jc .out
    add ax, cx
.ok:
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.half:                              ; BX = a page: mapped, and read into
    push bx
    mov cx, bx
    mov dx, [vp_ehnd]
    mov al, 3
    mov bx, (DRVC_EMS << 8) | EMSV_MAP
    call OSAPI_DRV_CALL
    jc .hx
    mov dx, [vp_eseg]
    add dx, (3 * EMS_PAGE) >> 4     ; quarter 3
    xor bx, bx
    mov cx, EMS_PAGE
    push ds
    pop es
    mov di, vp_cur
    mov si, vp_name
    call OSAPI_FILE_READ_SEQ        ; DX:AX = the bytes
.hx:
    pop bx
    ret

; vp_eaddr - vp_addr in place: AX = a chunk, BX = an offset in it -> DX:SI
; through the frame, the three pages from the one under it mapped into
; quarters 0-2 by their registers. Clobbers AX, BX, CX
vp_eaddr:
    cmp byte [vp_einp], 2           ; (the XMS bank's, by LOADALL)
    je vp_xbaddr
    call vp_slot
    shl ax, 1                       ; the slot's first page...
    mov cx, bx
    push cx
    mov cl, 14
    shr bx, cl
    add ax, bx                      ; ...and the one under the offset
    mov bx, [vp_kr]
    shl bx, 1                       ; BX = the ring's pages
    mov dx, [vp_eport]
    call .q
    inc ax
    add dx, [vp_estep]
    call .q
    inc ax
    add dx, [vp_estep]
    call .q
    pop bx
    and bx, EMS_PAGE - 1
    mov si, bx
    mov cl, 4
    shr bx, cl
    mov dx, [vp_eseg]
    add dx, bx
    and si, 15
    ret
.q:                                 ; AX = a page (wrapped here), DX = its
    cmp ax, bx                      ; quarter's register
    jb .qo
    sub ax, bx
.qo:
    push ax
    add ax, [vp_ebase]
    or al, [vp_eor]
    out dx, al
    pop ax
    ret

; the prefill's three questions, of whichever bank it fills: AX = the slots
; filled (vp_pfc); a chunk more, CF=1 none (vp_pfst); ZF=0 the file's end is
; in (vp_pfeof)
vp_pfc:
    cmp byte [vp_pfly], 0           ; THE LAYER's prefill (vp_lypre): its
    je .b                           ; slots and its bank
    mov ax, [vp_lyrd]
    sub ax, [vp_lyuse]
    add ax, [vp_lyxc]
    ret
.b:
    mov ax, [vp_xcnt]
    cmp byte [vp_einp], 0
    je .r
    mov ax, [vp_lc]
.r:
    ret

vp_pfst:
    cmp byte [vp_einp], 0
    je vp_bstep
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp                         ; (vp_ld8's rung, the even one)
    push es
    call vp_fill
    jmp vp_ld8

vp_pfcap:                           ; CX = the most it can fill: the bank's
    mov cx, [vp_xbn]                ; slots, or in place the RING's, which a
    cmp byte [vp_einp], 0           ; gate's cap may hold under them
    je .r
    mov cx, [vp_kr]
.r:
    ret

vp_pfeof:
    cmp byte [vp_einp], 0
    je .x
    cmp byte [vp_eof], 0
    ret
.x:
    cmp byte [vp_xbeof], 0
    ret

; vp_xquiet - NO XMS COPY UNDER THE SPEAKER'S CLOCK (98.3.18.5): its clock
; is its own interrupts, a sample each, and every transport holds them off
; for a copy - int 15h's block move for the whole 32 KB on a 286, ~11 ms or
; ~240 samples at 22 kHz, and the 386's for each KB of it - so a copy is
; time the play loses without a frame ever reading late: the owner's 286,
; banking every chunk, played in slow motion and said nothing. So the disk
; serves, as with no pool: whatever the bank held is handed back to the
; stream's cursor, sought once to the head's offset. Answers vp_xfill's CF=1,
; every other register kept
vp_xquiet:
    cmp byte [vp_xbank], 0
    je .r
    cmp word [vp_xcnt], 0
    je .r
    call vp_bseed
.r:
    stc
    ret

; vp_bflush - empty the bank (a seek, a Repeat's seam, a new block: the
; stream's cursor is about to be seeded somewhere else). Every register kept
vp_bflush:
    mov word [vp_xcnt], 0
    mov word [vp_xhs], 0
    mov byte [vp_xbeof], 0
    ret

; vp_bslot - AX = a slot -> DX:AX = its offset in the block. Clobbers nothing
; else
vp_bslot:
    mov dx, ax
    shr dx, 1                       ; slot x 32768: the high word is slot / 2
    and ax, 1
    ror ax, 1                       ; ...and the low word bit 15 its odd bit
    ret

; vp_bcur - the cursor at DS:SI copied over the one at DS:DI. Every register
; kept
vp_bcur:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov cx, FSEQ_SIZE / 2
    cld
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    ret

; vp_bfill - vp_xfill's arm for a bank: DX = the ring slot (BX = 0). out
; CF=0 AX = the bytes (32 KB, or the file's last chunk's), the head moved
; on; CF=1 the bank has nothing, and the disk reads - from the stream's
; cursor, handed back the moment the bank emptied. Every other register kept
vp_bfill:
    cmp word [vp_xcnt], 0
    jne .have
    stc
    ret
.have:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, dx
    mov cx, VP_CHUNK                ; ITS LENGTH: 32 KB, but the last chunk
    cmp word [vp_xcnt], 1           ; when the bank's cursor met the end
    jne .len
    cmp byte [vp_xbeof], 0
    je .len
    mov cx, [vp_xtl]
.len:
    cmp byte [vp_xems], 0           ; EMS (98.3.18.6): the head's two pages
    je .xd                          ; mapped, and copied out of the frame
    mov ax, [vp_xhs]                ; with interrupts on - one copy, where
    call vp_emap                    ; XMS takes two and holds them off
    jc .elost
    push cx
    push ds
    inc cx
    shr cx, 1
    xor si, si
    xor di, di
    mov ds, [vp_eseg]
    cld
    rep movsw
    pop ds
    pop cx
    jmp short .moved
.elost:
    call vp_xfree                   ; (the board refused: the disk serves,
    jmp short .lost                 ; as for a copy XMS refused)
.xd:
    push cx
    mov ax, [vp_xhs]
    call vp_bslot                   ; DX:AX = the head's slot
    inc cx
    and cl, 0xFE                    ; (an even copy: the slot has the room)
    xor si, si
    mov di, 1                       ; DOWN
    call vp_xcopy
    pop cx
    jc .lost
.moved:
    mov ax, [vp_xhs]                ; the head moves on
    inc ax
    cmp ax, [vp_xbn]
    jb .h
    xor ax, ax
.h:
    mov [vp_xhs], ax
    add [vp_xlo], cx
    adc word [vp_xlo+2], 0
    dec word [vp_xcnt]
    jnz .ok
    mov si, vp_xcur                 ; EMPTY: the bank's cursor is where the
    mov di, vp_cur                  ; stream goes on from - handed back, no
    call vp_bcur                    ; walk
.ok:
    mov ax, cx
    clc
    jmp short .out
.lost:                              ; THE COPY WAS REFUSED and vp_xcopy has
    call vp_bseed                   ; dropped the block: the disk serves from
    stc                             ; the head's offset, sought once
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; vp_bseed - the stream's cursor sought to the bank's head: [vp_cur] zeroed
; but for its place, so the next READ_SEQ seeds from the name (18.4.8).
; Every register kept
vp_bseed:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, vp_cur
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    mov ax, [vp_xlo]
    mov [vp_cur+FSEQ_OFF], ax
    mov ax, [vp_xlo+2]
    mov [vp_cur+FSEQ_OFF+2], ax
    call vp_bflush
    pop es
    pop di
    pop cx
    pop ax
    ret

; vp_bstep - THE BANK FILLS: one chunk off the disk at the bank's tail,
; through the bounce slot after the ring's mirror, while the ring has no
; room. out CF=0 a chunk went up; CF=1 nothing to do - no bank, a full one,
; or the file's end. The bracket's reader calls it where it would otherwise
; wait a period, so it fills only from time the ring did not want. UI task
; (OSAPI_XMEM_COPY's rule, SPEC.md 41.8). Every register kept
vp_bstep:
    cmp byte [vp_xbank], 0
    je .no
    cmp byte [vp_snd], VP_SPK       ; (vp_xquiet: no copy under the speaker,
    jne .sq                         ; EMS's aside)
    cmp byte [vp_xems], 0
    je .no
.sq:
    cmp byte [vp_xon], 0
    je .no
    cmp byte [vp_xbeof], 0
    jne .no
    cmp byte [vp_eof], 0
    jne .no
    push ax
    mov ax, [vp_xcnt]
    cmp ax, [vp_xbn]
    pop ax
    jb .go
.no:
    stc
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [vp_xcnt], 0           ; STARTING: the bank's cursor is the
    jne .rd                         ; stream's, and its head the ring's next
    mov si, vp_cur
    mov di, vp_xcur
    call vp_bcur
    mov ax, [vp_cur+FSEQ_OFF]
    mov [vp_xlo], ax
    mov ax, [vp_cur+FSEQ_OFF+2]
    mov [vp_xlo+2], ax
.rd:
    cmp byte [vp_xems], 0           ; EMS (98.3.18.6): the tail's two pages
    je .xb                          ; mapped, and READ INTO - no bounce, no
    call .tail                      ; copy up: a disk's read is the fill
    call vp_emap
    jc .elost
    mov dx, [vp_eseg]
    call .read                      ; DX:AX = the bytes
    jc .stop
    or ax, ax
    jz .end
    mov cx, ax
    jmp short .up
.elost:                             ; (the board refused: as XMS's refusal -
    call vp_xfree                   ; the block gone, the stream's cursor
    jmp .lost                       ; sought to the head)
.tail:                              ; AX = the bank's tail slot
    mov ax, [vp_xhs]
    add ax, [vp_xcnt]
    cmp ax, [vp_xbn]
    jb .t
    sub ax, [vp_xbn]
.t:
    ret
.read:                              ; DX:0 <- the stream's next chunk
    xor bx, bx
    mov cx, VP_CHUNK
    push ds
    pop es
    mov di, vp_xcur
    mov si, vp_name
    call OSAPI_FILE_READ_SEQ        ; (a far cell: no tail jump)
    ret
.xb:
    call .tail
    call vp_bslot                   ; DX:AX = the tail's slot, in the block
    add ax, [vp_xbase]
    adc dx, [vp_xbase + 2]
    mov [vp_htg], ax
    mov [vp_htg + 2], dx
    call vp_bbnc                    ; DX = the bounce slot
    mov cx, VP_CHUNK
    call vp_harm                    ; (98.3.18.15)
    call .read                      ; DX:AX = the bytes (DX 0: a chunk)
    call vp_hdis                    ; (flags kept)
    jc .stop                        ; a disk error: the stream will meet it
    or ax, ax
    jz .end                         ; the file ended on a chunk's boundary
    push ax
    call vp_bbnc                    ; (DX:AX was the count: the bounce again)
    mov es, dx                      ; the bounce, up to the tail's slot
    call .tail
    call vp_bslot
    pop cx
    push cx
    inc cx
    and cl, 0xFE
    xor si, si
    xor di, di                      ; UP
    call vp_xcopym
    pop cx
    jc .lost
.up:
    inc word [vp_xcnt]
    cmp cx, VP_CHUNK
    je .ok
.eof:
    mov [vp_xtl], cx                ; a short chunk: the file's last
    mov byte [vp_xbeof], 1
.ok:
    clc
    jmp short .out
.end:                               ; the end on a chunk's boundary: the
    mov cx, VP_CHUNK                ; last chunk banked was a whole one
    jmp short .eof
.stop:                              ; (no more banking this play: what is
    mov word [vp_xtl], VP_CHUNK     ; banked is whole chunks, and the
    mov byte [vp_xbeof], 1          ; stream meets the error after them)
    stc
    jmp short .out
.lost:                              ; the copy refused: the block is gone,
    call vp_bseed                   ; and with it what it held
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_pstep - A PAUSE BANKS (98.3.18.4): a session paused on the desktop
; reads its bank on, a chunk a timer tick, so the time a user spends paused
; is time the play has in hand. CF=1 nothing to do. UI task (the timer's)
vp_pstep:
    cmp byte [vp_sess], 0
    je .no
    cmp byte [vp_lsess], 0
    jne .no
    cmp byte [vp_ready], 0          ; (a bracket running reads its own)
    jne .no
    cmp byte [vp_einp], 0           ; (in place, the ring is the bank)
    jne vp_fill
    jmp vp_bstep
.no:
    stc
    ret

; vp_parm - the bracket is over with the session still on: if it banks,
; the window's timer carries the bank on from the next tick
vp_parm:
    cmp byte [vp_sess], 0
    je .out
    cmp byte [vp_einp], 0
    jne .arm
    cmp byte [vp_xbank], 0
    je .out
.arm:
    push ax
    push bx
    mov ax, 1
    mov bx, [vp_win]
    call OSAPI_WM_TIMER
    pop bx
    pop ax
.out:
    ret

; vp_bseek - A SEEK INTO THE BANK (98.3.18.4): the stream's new start,
; [vp_ssp], inside what the bank holds - the chunks before it dropped, the
; head the ring's chunk 0 and BX how far into it the start is; the cursors
; are not touched, so the disk is not either. CF=1 it is not (behind the
; bank's head, past what it holds, or a cluster over a chunk): the caller
; flushes and seeds the reader as ever. Clobbers AX, CX, DX
vp_bseek:
    cmp byte [vp_xbank], 0
    je .no
    cmp byte [vp_xon], 0
    je .no
    cmp word [vp_xcnt], 0
    je .no
    mov ax, [vp_clb]                ; (a cluster no bigger than a chunk)
    dec ax
    cmp ax, VP_CHUNK - 1
    ja .no
    mov ax, [vp_ssp]
    mov dx, [vp_ssp+2]
    sub ax, [vp_xlo]
    sbb dx, [vp_xlo+2]
    jb .no                          ; behind the head: the ring's, or gone
    test dh, 0x80                   ; (DX >= 8000h)
    jnz .no
    mov bx, ax
    and bx, VP_CHUNK - 1            ; BX = into its chunk
    shl ax, 1                       ; (bit 15 to the carry: AX is spent)
    mov cx, dx
    rcl cx, 1                       ; CX = the chunks before it
    cmp cx, [vp_xcnt]
    jae .no
    sub [vp_xcnt], cx               ; DROPPED: the head moves on by CX
    mov ax, cx
    shr ax, 1
    add [vp_xlo+2], ax
    test cl, 1
    jz .lo
    add word [vp_xlo], 0x8000
    adc word [vp_xlo+2], 0
.lo:
    mov ax, [vp_xhs]
    add ax, cx
.m:
    cmp ax, [vp_xbn]
    jb .h
    sub ax, [vp_xbn]
    jmp short .m
.h:
    mov [vp_xhs], ax
    inc word [vp_bskn]              ; (a gate's count, tests/vidbank.py)
    clc
    ret
.no:
    stc
    ret

; vp_bbnc - DX = the bounce slot: the claim's last, after the ring's K slots
; and its mirror (vp_sstart claims it with them when there is a bank). From
; [vp_ring] at its use, the claim being movable between brackets (98.3.19.2).
; Every other register kept
vp_bbnc:
    push ax
    push cx
    mov ax, [vp_k]
    add ax, [vp_msl]
    mov cl, 11
    shl ax, cl                      ; x 2,048 paragraphs a slot
    add ax, [vp_ring]
    mov dx, ax
    pop cx
    pop ax
    ret

; vp_bpre - THE PREFILL (SPEC.md 98.3.18.3): before a session's first frame,
; the bank filled to what the file asks - [vp_hxp] KB, the bytes its encode
; banked on, 0 meaning 10 s at the stream's mean and FFFFh the whole bank -
; saying
; `Buffering 37% ~24s` in the full screen's box as it goes. Space starts the
; play with what is in; Esc cancels: out CF=1. The ring is full and the hook
; idle throughout. Clobbers AX, BX, CX, DX, SI, DI
vp_bpre:
    cmp byte [vp_einp], 0           ; IN PLACE the ring is the bank: what is
    jne .ip                         ; filled is the ring's (vp_pfc, vp_pfst)
    cmp byte [vp_xbank], 0
    je .dz
    cmp byte [vp_snd], VP_SPK       ; (vp_xquiet: nothing to fill it for,
    jne .sq                         ; EMS's aside)
    cmp byte [vp_xems], 0
    je .dz
.sq:
.ip:
    cmp byte [vp_xon], 0
    je .dz
    call vp_pfcap                   ; HOW FAR: the whole bank, or the ask
    mov ax, [vp_hxp]                ; - the KB the encode banked on before
    cmp ax, 0xFFFF                  ; its first frame
    je .n
    xor dx, dx
    or ax, ax
    jnz .kb
    mov ax, [vp_slen]               ; none asked: 10 s at the stream's mean,
    mov dx, [vp_slen+2]
    mov cx, [vp_frames]
    jcxz .dz
    call vp_div32
    mov cx, [vp_rate]
    call vp_mul32
    mov cx, [vp_spf]
    jcxz .dz
    call vp_div32
    mov cx, 1024
    call vp_div32                   ; AX = KB a second (DX 0: < 64 MB/s)
    mov cx, 10
    mul cx                          ; DX:AX = the KB the ask is
    jmp short .kb
.dz:
    jmp .done
.kb:
    add ax, 31
    adc dx, 0
    mov cx, 32
    call vp_div32                   ; ...in slots
    call vp_pfcap
    or dx, dx
    jnz .n
    cmp ax, cx
    jae .n
    mov cx, ax
.n:
    jcxz .dz
    mov [vp_pfn], cx
    call vp_ticks
    mov [vp_pft0], ax
.lp:
    mov ah, 1                       ; a key: Space plays now, Esc cancels,
    int 0x16                        ; anything else is spent
    jz .nk
    xor ah, ah
    int 0x16
    cmp al, 27
    je .esc
    cmp al, ' '
    je .done
.nk:
    cmp byte [vp_pfly], 0           ; THE LAYER's (vp_lypre, which runs this
    je .bk                          ; loop with [vp_pfly] 1): to its ask, or
    call vp_pfc                     ; its end
    cmp ax, [vp_pfn]
    jae .full
    cmp byte [vp_lyeof], 0          ; (the layer read to its end: all there
    jne .full                       ; is to fill)
    call vp_pfsay
    call vp_lyread
    jmp short .st
.bk:
    call vp_pfeof                   ; THE FILE'S END BANKED: the rest of it
    je .ns                          ; is all there is to fill, so that is
    call vp_pfc                     ; the 100% (a short file, or a bank as
    or ax, ax                       ; big as what the ring left)
    jz .ns
    cmp ax, [vp_pfn]
    jae .ns
    mov [vp_pfn], ax
.ns:
    call vp_pfsay
    call vp_pfc
    cmp ax, [vp_pfn]
    jae .full
    call vp_pfst                    ; a chunk up, or nothing more to bank
.st:
    jnc .lp
.full:
    cmp byte [vp_pfwait], 0         ; (the gate's hold, tests/vidbank.py and
    jne .lp                         ; tests/vidlybank.py)
.done:
    clc
.x:
    pushf                           ; (CF the answer, whatever vo_show does)
    xor ax, ax
    mov [vp_pfly], al
    mov si, vo_s_none
    call vo_show
    popf
    ret
.esc:
    stc
    jmp short .x

; vp_bshort - a file whose encode banked on XMS ([vp_hxb] KB, 98.3.18.3)
; and a machine whose bank is smaller - or none, the file not held whole
; either: [vp_rshort], so the full screen says Low memory once. Preserves all
vp_bshort:
    push ax
    mov ax, [vp_hxb]
    cmp byte [vp_lysplt], 0         ; THE LAYER'S MACHINE (98.1.9.2): once
    je .ask                         ; the pool is split and the layer read,
    cmp byte [vp_lyon], 0           ; the base's bank is what that machine
    jne .lyn                        ; needs ([vp_lyneed]) and not the file's
    cmp word [vp_lyxn], 0           ; ask - its own split's doing, and no
    je .ask                         ; shortage. [vp_lyon] is a SEEK's: a
.lyn:                               ; session start runs this before
    mov ax, [vp_lyneed]             ; vp_lyopen, so there the layer's bank
.ask:                               ; says it (98.1.9.10) (the owner: Low memory on the
    or ax, ax                       ; second play, never on the first)
    jz .out
    cmp byte [vp_xon], 0
    je .short
    cmp byte [vp_xbank], 0
    je .out                         ; held whole: better than any bank
    push cx
    mov cl, 5
    push ax
    mov ax, [vp_xbn]
    shl ax, cl                      ; the bank's KB
    pop cx
    cmp ax, cx
    pop cx
    jae .out
.short:
    mov byte [vp_rshort], 1
.out:
    pop ax
    ret

; vp_pfsay - the prefill's line: `Buffering NN%`, and `~Ns` (or `~Nm`) left
; at the rate the fill has read at so far, into the box - or in the window,
; whose box the full screen's text is not (98.3.13), THE THUMB as its meter:
; it crosses the bar as the bank fills, and the first frame puts it back.
; Clobbers AX, BX, CX, DX, SI, DI
vp_pfsay:
    cmp byte [vp_winm], 0
    je .fs
    call vp_pfc
    cmp ax, [vp_pfn]
    jb .wp
    mov ax, [vp_pfn]
.wp:
    mov cx, [vp_lbw]
    sub cx, VP_THW
    mul cx
    div word [vp_pfn]               ; AX = the thumb's x in the bar
    cmp ax, [vp_wtx]
    je .wr
    mov bx, [vp_wtx]
    mov [vp_wtx], ax
    add ax, [vp_tx1]
    add bx, [vp_tx1]
    call vp_wmove
    mov word [vp_wtk], 0xFFFF       ; (the first frame asks it again)
.wr:
    ret
.fs:
    mov di, vp_pfbuf
    mov si, vp_s_buf
.c:
    lodsb
    or al, al
    jz .pc
    mov [di], al
    inc di
    jmp short .c
.pc:
    call vp_pfc                     ; per cent of the ask
    mov bx, [vp_pfn]
    cmp ax, bx
    jb .pp
    mov ax, bx
.pp:
    mov cx, 100
    mul cx
    div bx
    call vp_pfnum
    mov byte [di], '%'
    inc di
    call vp_pfc                     ; THE TIME LEFT: ticks so far x slots
    mov cx, ax
    jcxz .end                       ; left / slots so far, once there is a
    mov ax, [vp_pfn]                ; rate to say it from
    sub ax, cx
    jbe .end
    push ax
    call vp_ticks
    sub ax, [vp_pft0]
    pop bx
    mul bx
    push ax
    call vp_pfc
    mov cx, ax
    pop ax
    call vp_div32                   ; DX:AX = ticks left
    mov cx, 10
    call vp_mul32
    mov cx, 182
    call vp_div32                   ; ...seconds
    mov bl, 's'
    or dx, dx
    jnz .m
    cmp ax, 100
    jb .s
.m:
    mov cx, 60
    call vp_div32
    mov bl, 'm'
    or dx, dx
    jnz .big
    cmp ax, 100
    jb .s
.big:
    mov ax, 99
.s:
    mov word [di], ' ~'
    inc di
    inc di
    call vp_pfnum
    mov [di], bl
    inc di
.end:
    mov cl, [vo_cmax]               ; ONE WIDTH ALL THROUGH: padded to the
    cmp cl, VO_MAXC                 ; widest the box takes here, so a change
    jbe .pw                         ; is drawn over the last (vo_post)
    mov cl, VO_MAXC
.pw:
    xor ch, ch
    add cx, vp_pfbuf
.pad:
    cmp di, cx
    jae .pz
    mov byte [di], ' '
    inc di
    jmp short .pad
.pz:
    mov byte [di], 0
    mov si, vp_pfbuf                ; DRAWN ONLY WHEN IT CHANGED: every pass
    cmp byte [vo_kind], VOK_BUF     ; of the loop asks, and a box drawn again
    jne .new                        ; is a box off the glass between (98.3.13)
    mov di, vo_text
.cmp:
    lodsb
    cmp al, [di]
    jne .new
    inc di
    or al, al
    jnz .cmp
    ret
.new:
    mov si, vp_pfbuf
    mov ax, VOK_BUF | 0x100         ; (drawn again: the same kind, moved)
    jmp vo_show

; vp_pfnum - AX (< 1000) in decimal at [DI], DI past it. Clobbers AX, CX, DX
vp_pfnum:
    xor cx, cx
.d:
    ; STKBALANCE-LOOP: a digit pushed a turn and the second loop pops them; the count is in CX
    xor dx, dx
    push bx
    mov bx, 10
    div bx
    pop bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.o:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .o
    ret

; --- W_ONTIMER: SI = the window, lock held. The hold's next chunk, and the
; card's line that counts it. The next is armed as far off as this one took,
; so the loading has half the machine and the desktop the other half
vp_ontimer:
    push ax
    push bx
    push dx
    call vp_ticks
    mov dx, ax
    call vp_xstep
    jnc .tm
    call vp_pstep                   ; ...or A PAUSED SESSION'S BANK, a chunk
    jc .said                        ; nothing left, or no hold: no next
.tm:
    call vp_ticks
    sub ax, dx
    jnz .arm
    inc ax
.arm:
    mov bx, [vp_win]
    call OSAPI_WM_TIMER
.said:
    cmp byte [vp_played], 0         ; the card's line 6, until a play's
    jne .out                        ; figures take it
    call vp_fmt
    call vp_clipw
    jc .out
    call vp_track
    mov bx, 6
    call vp_ptext
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE HALF-SCALER (SPEC.md 98.4). Two rows of the source, a nibble of each at
; a time, index a 256-byte table that answers two output pixels: each lit
; when the four source pixels under it hold MORE lit ones than its threshold,
; 2x2 ordered dither - [0 2 / 3 1] by output row and column parity - so a
; grey stays a grey and a line one pixel wide survives. tools/os88vid.py's
; thumb_half is the reference; a table per row parity, made at start
; -----------------------------------------------------------------------------
vp_mkdtab:
    push ax
    push bx
    push cx
    push dx
    xor bx, bx                      ; BH = the row's parity, BL = the index
.l:
    mov al, bl
    xor dx, dx                      ; DH = the left pixel's count, DL the right's
    mov cx, 8
.b:
    shl al, 1                       ; bits 7,6 and 3,2 are the left pixel's,
    jnc .z                          ; 5,4 and 1,0 the right's: bit CX-1
    mov ah, cl
    dec ah
    test ah, 2
    jz .r
    inc dh
    jmp short .z
.r:
    inc dl
.z:
    loop .b
    xor al, al
    mov cx, 0x0002                  ; even rows: left over 0, right over 2
    or bh, bh
    jz .t
    mov cx, 0x0301                  ; odd rows: left over 3, right over 1
.t:
    cmp dh, ch
    jbe .t1
    or al, 2
.t1:
    cmp dl, cl
    jbe .st
    or al, 1
.st:
    mov [vp_dtab+bx], al
    inc bx
    cmp bx, 512
    jb .l
    jmp vp_ld4

; two output pixels a nibble: AL = the upper row's byte, AH = the lower's,
; BX = this row's table, CL = 4 -> AL = four output pixels, bits 3..0.
; DX clobbered
%macro VH_NIB 0
    mov dx, ax
    and al, 0xF0
    shr ah, cl
    or al, ah                       ; upper's high nibble, lower's high
    cs xlatb
    mov ah, al
    mov al, dl
    shl al, cl
    and dh, 0x0F
    or al, dh                       ; ...and the low nibbles
    cs xlatb
    shl ah, 1
    shl ah, 1
    or al, ah
%endmacro

; vp_half - [vh_sseg]'s image, [vh_wb] bytes x [vh_h] rows - in the layout
; [vh_lay], or linear at [vh_sstr] when that is FFh - halved into [vh_dseg]:0,
; dense. out: [vh_wb], [vh_h] are the half's. The destination may be the
; source when it is linear: every write lands at or behind every read left
vp_half:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, [vh_wb]
    inc ax
    shr ax, 1
    mov [vh_owb], ax
    mov es, [vh_dseg]
    xor di, di
    mov word [vh_y], 0
.row:
    mov ax, [vh_y]
    shl ax, 1
    cmp ax, [vh_h]
    jae .done
    push ax
    call vh_addr
    mov si, ax                      ; SI = the upper row
    pop ax
    inc ax
    xor bp, bp
    cmp ax, [vh_h]
    jae .one                        ; an odd last row pairs with itself
    call vh_addr
    sub ax, si
    mov bp, ax                      ; BP = the lower row, from the upper
.one:
    mov bl, [vh_y]
    and bl, 1
    mov bh, bl
    xor bl, bl
    add bx, vp_dtab
    mov ch, [vh_wb]
    shr ch, 1                       ; whole source byte pairs
    mov cl, 4
    push ds
    mov ds, [vh_sseg]
    or ch, ch
    jz .last
.p:
    mov al, [si]
    mov ah, [ds:bp+si]
    inc si
    VH_NIB
    shl al, cl
    mov [es:di], al
    mov al, [si]
    mov ah, [ds:bp+si]
    inc si
    VH_NIB
    or [es:di], al
    inc di
    dec ch
    jnz .p
.last:
    test byte [cs:vh_wb], 1         ; an odd byte left: the high nibble alone
    jz .rd
    mov al, [si]
    mov ah, [ds:bp+si]
    VH_NIB
    shl al, cl
    mov [es:di], al
    inc di
.rd:
    pop ds
    inc word [vh_y]
    jmp .row
.done:
    mov ax, [vh_owb]
    mov [vh_wb], ax
    mov ax, [vh_h]
    inc ax
    shr ax, 1
    mov [vh_h], ax
    jmp vp_ld8

; vh_addr - AX = a source row -> AX = its first byte (clobbers DX)
vh_addr:
    cmp byte [vh_lay], 0xFF
    je .lin
    push bx
    mov bl, [vh_lay]
    call vp_rowaddr
    pop bx
    ret
.lin:
    mul word [vh_sstr]
    ret

; =============================================================================
; THE PLAY IS A SESSION (SPEC.md 98.3, 98.3.7): the ring, the stream's
; cursors, the card and a copy of the canvas, which brackets come and go on.
; vp_play and vp_fsenter are the user's two ways in. A bracket ends by
; STOPPING the session, by SWAPPING between the window and the full screen,
; or by pausing back to the DESKTOP, where the session waits with its frame
; in the box until Play resumes it. Gfx lock held, SI = window, throughout
; =============================================================================
vp_play:                            ; Space, P, Enter, the Play button: play,
    push ax                         ; in the window if it can host it
    call vp_recheck                 ; (the display it is on NOW: 98.3.21)
    cmp byte [vp_ok], 1
    jne .out
    cmp byte [vp_lsess], 0          ; A LIVE SESSION is paused or resumed as
    jne .lv                         ; Live, whatever vp_canlive now says (a
                                    ; hold dropped): a bracket would leave
                                    ; its worker running on the desktop
    call vp_canlive                 ; ...or LIVE, if the file may and the box
    jc .nl                          ; shows it at its own size (98.3.10)
.lv:
    call vp_lplay
    jmp short .out
.nl:
    cmp byte [vp_sess], 0
    jne .resume
    mov byte [vp_startp], 0
    mov byte [vp_cpent], 1          ; (Play may post a compaction, 98.3.19.1)
    call vp_sstart
    mov byte [vp_cpent], 0
    jc .out
    jmp short .run
.resume:
    mov byte [vp_autop], 1          ; paused on the desktop: resumed as the
.run:                               ; bracket starts
    mov byte [vp_wantwin], 1
    call vp_srun
.out:
    pop ax
    ret

vp_fsenter:                         ; F, Alt+Enter: full screen, PAUSED -
    push ax                         ; played when Space says so (98.3.6)
    call vp_recheck
    cmp byte [vp_ok], 1
    jne .out
    cmp byte [vp_sess], 0
    jne .run
    mov byte [vp_startp], 1
    mov byte [vp_cpent], 2          ; (...and so may F)
    call vp_sstart
    mov byte [vp_cpent], 0
    jc .out
.run:
    xor ax, ax
    cmp byte [vp_lsess], 0          ; LIVE -> the full screen: the worker
    je .rn                          ; stops at the frame it drew, and the
    mov [vp_lrun], al               ; bracket goes on from it, playing if it
    mov [vp_autop], al              ; was - the card paused with it, and
    cmp byte [vp_upause], 0         ; resumed by the bracket
    jne .lp
    call vp_upaus
    mov byte [vp_autop], 1
    xor ax, ax
.lp:
    mov [vp_sfirst], al
.rn:
    mov [vp_wantwin], al
    call vp_srun
.out:
    pop ax
    ret

; =============================================================================
; LIVE (SPEC.md 98.3.10): a resident file's play ON THE DESKTOP. No bracket:
; the worker decodes the frames due into a RAM shadow and blits the rows they
; wrote into the box with OSAPI_GFX_BLIT1, under the gfx lock and the
; window's clip - the whole frame inside one hold, so a UI callback, which
; holds the lock too, never meets a frame half done and may start, stop or
; hand the play to the full screen as it likes
; =============================================================================
; vp_canlive - CF=0: this file plays LIVE here - it says it may, it is a
; one-bit LIN80 rendition or a VGA4 one on a sixteen-colour desktop, and the
; box shows it whole at its own size
vp_canlive:
    cmp byte [vp_fsdisp], 0         ; ANOTHER DISPLAY's play (98.3.21) is
    jne .no                         ; full screen there, never Live here
    cmp byte [vp_flive], 0
    je .no
    cmp byte [vp_resid], 0          ; A STREAM (98.3.18.1): the whole file
    jne .rs                         ; held in XMS - a worker may not read a
    cmp byte [vp_xon], 0            ; file, and the UI task's copy out of the
    je .no                          ; hold is what feeds it. Not yet held:
                                    ; the in-window play, which fills the
                                    ; hold behind it
    cmp byte [vp_xfull], 0
    je .no
.rs:
    cmp byte [vp_pixfmt], 0         ; MONO1 (the byte is the format - 1)
    je .m1
    cmp byte [vp_pixfmt], PF_VGA4   ; ...or IN COLOUR (98.3.10.4): VGA4 where
    jne .no                         ; the desktop has sixteen colours, its
    cmp byte [vp_grey], 0           ; four planes in one segment - so one
    je .no                          ; OSAPI_GFX_BLITP reaches them all
    cmp word [vp_plsp], VP_V4PMAX
    ja .no
.m1:
    cmp byte [vp_layout], LAY_LIN80
    jne .no
    cmp word [vp_ps], 1
    jne .no
    push ax
    call vp_boxxy
    mov ax, [vp_pdw]
    cmp ax, [vp_lpw]
    pop ax
    jne .no
    clc
    ret
.no:
    stc
    ret

; vp_lplay - Play on a live file: a live session started - or, if there is
; one, paused or resumed. A session a bracket left paused here is ended and
; the live one starts from its keyframe. Lock held
vp_lplay:
    push ax
    cmp byte [vp_sess], 0
    je .start
    cmp byte [vp_lsess], 0
    jne .toggle
    xor al, al
    call vp_stopfor
.start:
    xor ax, ax
    mov [vp_startp], al
    inc ax
    mov [vp_livem], al
%ifdef VP_LIVESND
    call vp_sstart                  ; WITH SOUND (98.3.10.1): the worker is
%else                               ; the card's feeder
    mov al, [vp_nosnd]              ; LIVE is silent: the card's clock is a
    push ax                         ; bracket's (98.3.10)
    mov byte [vp_nosnd], 1
    call vp_sstart
    pop ax
    mov [vp_nosnd], al
%endif
    mov byte [vp_livem], 0
    jc .out
    call vp_lsetup
    jmp short .out
.toggle:
    mov byte [vp_bone], 3           ; (Play/Pause's button alone: the rest
    cmp byte [vp_upause], 0         ; hold the worker off for nothing)
    jne .res
    call vp_upaus                   ; PAUSE: the worker stops where it is,
    xor ax, ax                      ; and the card, if any, where it is
    mov [vp_lrun], al
    mov [vp_bpause], al
    push bx                         ; ...AND WHAT THE SHADOW HOLDS IS PUT IN
    push cx                         ; THE BOX: rows decoded and not yet
    push dx                         ; blitted are owed, and a paused worker
    push si                         ; never pays them. A Space queued behind
    push di                         ; the start's load paused the play before
    push bp                         ; the worker's first pass, and the box
    mov ax, [vp_done]               ; stayed black for good: nothing decoded
    cmp ax, [vp_base]               ; yet, so the first frame is decoded now
    jne .shown                      ; (the worker's own vp_frame, under the
    push es                         ; lock we hold and with the worker's ES =
    push ds                         ; ours, where a callback's is the
    pop es                          ; kernel's; CF = none, and the box
    call vp_frame                   ; shows what the shadow has) - and with
    pop es
    call vp_bandall                 ; the WHOLE band, not the frame's runs:
.shown:                             ; those are what it changed against the
                                    ; frame before, and the box shows black
    call vp_lblit                   ; nothing owed the blit is a compare
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    call vp_lbtn
    jmp short .l4
.res:
    call vp_lgo
    call vp_lbtn
.l4:
    call vp_lline4                  ; the card's "where", paused or playing
.out:
    pop ax
    ret

; vp_lline4 - a live session paused or resumed: the info card's line 4 again,
; "Paused at 0:05" or where the play started. Nothing else on the card moves,
; so one line is drawn and not the card (vp_fmt never ran for a Live pause,
; and the card went on saying "At the start" over a paused frame). Lock held
vp_lline4:
    push bx
    call vp_fmt
    call vp_clip
    mov bx, 4
    call vp_ptext1
    pop bx
    ret

; vp_lsetup - a live session, just started: the shadow black and the key the
; play starts at decoded into it, the clock, the worker (hired once), and
; Play turned to Pause
vp_lsetup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di                         ; (vp_ld6's rung, the even one)
    mov al, 1
    mov [vp_lsess], al
    mov [vp_shadow], al
    dec ax
    mov [vp_ldrain], al
    call vp_bandall
    cmp word [vp_krec], 0xFFFF      ; the key, onto the black
    je .nk
    mov si, [vp_krec]
    call vp_rptr
    call vp_decrec
    call vp_bandall
.nk:
    call vp_lprime                  ; A STREAM: the ring filled, over the key
    xor ax, ax
    mov [vp_sfirst], al
    inc ax
    mov [vp_ready], al
    call vp_lgo                     ; (its clock is taken again below)
    cmp byte [vp_hired], 0          ; THE WORKER, once for the instance
    jne .h
    mov ax, vp_worker
    mov bx, [vp_win]
    call OSAPI_TASK_SPAWN
    jc .no
    mov byte [vp_hired], 1
    ; restartable: it parks only inside OSAPI_TASK_ALIVE at the top of its
    ; loop, and everything that outlives a pass is a static, so a restart
    ; costs one pass (SPEC.md 66.6.2)
    OS88_WORKER_RESTARTABLE vp_worker
    push ax                         ; PARK-SAFE (SPEC.md 66.5.4, 98.1.7.4):
    mov al, 1                       ; the player takes the gfx lock in two
    call OSAPI_MEM_PARKSAFE         ; places - the worker's pass, before it
    pop ax                          ; addresses a block, and vp_onwake's entry
                                    ; - and holds nothing derived from one in
                                    ; either, so the worker may park waiting
                                    ; there. Without it a claim made from a
                                    ; window callback, which holds the lock,
                                    ; finds the worker blocked on it and every
                                    ; block pinned (66.5.3)
.h:
    call vp_lbtn3                   ; Play turned to Pause BEFORE the card:
%ifdef VP_LIVESND                   ; a card started first plays on through
    cmp byte [vp_snd], 0            ; this draw while the worker waits on
    je .ns                          ; the lock, and the start is then 0 to
    call vp_acur                    ; 3 frames late by where in a tick the
    call vp_sopen                   ; key landed (98.3.10.1) - the card,
.ns:                                ; started on the key's frame with the
%endif                              ; picture, the last thing done
    call vp_ticks
    mov [vp_t0], ax
    mov [vp_ltk], ax                ; ...and the worker's clock from it
.out:
    jmp vp_ld6
.no:
    mov word [vp_msg], vp_s_refused
    xor al, al
    call vp_stopfor
    call vp_lbtn
    jmp short .out

; vp_lgo - a live play (re)started from where it is: the shadow the keeper
; and every row owed to the box, the clock from now, and the worker on
vp_lgo:
    push ax
    push bx
    push dx
    cmp byte [vp_upause], 0         ; paused - by Space, or by the bracket it
    je .np                          ; came back from: resumed as a bracket
    call vp_upaus                   ; resumes, the card with it
.np:
    mov byte [vp_shadow], 1         ; (a bracket may have played natively)
    mov ax, [vp_keep]
    mov [vp_shseg], ax
    call vp_bandall
    xor ax, ax
    mov [vp_upause], al
    mov [vp_winm], al
    inc ax
    mov [vp_bpause], al
    mov ax, [vp_pdiv]               ; the frame's period in PIT counts: the
    mov bl, [vp_pitper]             ; divisor x the periods a frame
    xor bh, bh
    mul bx
    mov [vp_lper], ax
    mov [vp_lper+2], dx
    xor ax, ax
    mov [vp_lacc], ax
    mov [vp_lacc+2], ax
    mov [vp_lrem], ax
    call vp_ticks
    mov [vp_ltk], ax
    mov byte [vp_lrun], 1
    pop dx
    pop bx
    pop ax
    ret

; --- LIVE FROM THE HOLD (98.3.18.1): a streamed Live play's ring, filled by
; the UI task out of XMS, as a bracket's reader fills it for the hook. The
; worker only reads the ring; vp_fill publishes [vp_lc] last, so a chunk is
; never seen half copied, and a record not there yet is a stall, not a wait
; vp_lprime - a stream's ring filled, and the cursor stepped past the key's
; frame's records (the bracket's own start). Lock held
vp_lprime:
    cmp byte [vp_resid], 0
    jne .r
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
.f:
    call vp_fill
    jnc .f
    mov cx, [vp_kidx]
    jcxz .o
.sk:
    push cx
    mov bx, vp_pc
    call vp_next
    pop cx
    jc .o                           ; (the end or damage: the worker finds it)
    loop .sk
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; vp_lfeed - a live stream's ring topped up, on the UI task. Lock held
vp_lfeed:
    call vp_lfeed1
    jnc vp_lfeed
    ret

; vp_lfeed1 - one chunk of it: CF=1 when none was read (no stream, resident,
; or nothing due). Lock held - the worker reads what vp_fill writes (98.3.18.1)
vp_lfeed1:
    cmp byte [vp_lsess], 0
    je .none
    cmp byte [vp_resid], 0
    jne .none
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vp_fill
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.none:
    stc
    ret

; vp_lask - the worker, after a pass: a slot the play has left, or a seam to
; read - one wake to the UI task, and no second until it has run
vp_lask:
    cmp byte [vp_resid], 0
    jne .r
    cmp byte [vp_lwant], 0
    jne .r
    cmp byte [vp_eof], 0
    jne .e
    mov ax, [vp_pc]
    add ax, [vp_k]
    cmp [vp_lc], ax
    jae .r
    jmp short .w
.e:
    cmp byte [vp_rep], 0            ; the file's end, repeating: the next
    je .r                           ; lap's start, once the seam is taken
    mov ax, [vp_pgen]
    cmp ax, [vp_wgen]
    jne .r
.w:
    mov byte [vp_lwant], 1
    mov bx, [vp_win]
    call OSAPI_WM_WAKE
.r:
    ret

; vp_lback - a bracket handed a live session back to the desktop: live
; again, playing if it was, and the box repainted from the shadow
vp_lback:
    push si
    call vp_lfeed                   ; (a stream: its ring topped up)
    xor ax, ax                      ; the box repainted from the shadow
    mov [vp_lrun], al               ; FIRST, and only then the play resumed:
    mov [vp_bpause], al             ; a repaint holds the lock, and a card
    mov byte [vp_shadow], 1
    mov ax, [vp_keep]               ; resumed before it plays on while the
    mov [vp_shseg], ax              ; worker waits (98.3.10.1)
    call vp_fmt
    mov si, [vp_win]
    call vp_repaint
    cmp byte [vp_autop], 0
    je .out
    mov byte [vp_autop], 0
    call vp_lgo
    call vp_lbtn3                   ; (Pause, not Play, on its button)
    call vp_lline4                  ; (vp_fmt above said "Paused at")
.out:
    pop si
    ret

; vp_lbtn - the buttons again, Play or Pause as it now is. Lock held.
; vp_lbtn3 - ...Play/Pause's alone, where nothing else on the row changed:
; a start and a return from the full screen, whose row was drawn whole by
; the paint before it (98.4.9)
    nop                             ; (parity: the play's routines below)
vp_lbtn3:
    mov byte [vp_bone], 3
vp_lbtn:
    call vp_track
    call vp_clip
    jmp vp_buttons

; -----------------------------------------------------------------------------
; vp_worker - THE background task (SPEC.md 20.6): a live play's frames. A
; tick asleep, then the lock; if a live play runs, the frames its clock
; says are due decoded into the shadow and the rows they wrote blitted.
; in: DS = ES = CS = ours, the lock free. Never returns: OSAPI_TASK_ALIVE
; not coming back is the way out
; -----------------------------------------------------------------------------
vp_worker:
    mov bx, [vp_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here
    mov ax, 1
    call OSAPI_TASK_SLEEP
    call OSAPI_GFX_LOCK
    cmp byte [vp_lrun], 0
    je .idle
    call vp_lstep
    call vp_lask                    ; a stream's next chunks, from the UI task
    call OSAPI_GFX_UNLOCK
    jmp short vp_worker
.idle:
    call OSAPI_GFX_UNLOCK
    mov ax, 4                       ; nothing playing: a longer sleep - but
    cmp byte [vp_lsess], 0          ; a live session paused sleeps a tick,
    je .sl                          ; so the resume's first frame is not
    mov al, 1                       ; four ticks behind a card already
.sl:                                ; playing (98.3.10.1)
    call OSAPI_TASK_SLEEP
    jmp short vp_worker

; vp_lstep - one pass of a live play, the lock held: the ticks since the
; last pass are PIT counts owed, a frame for each period in them - at most
; VP_LCAP drawn, the rest forgiven (the picture runs slow, never wrong); then
; the rows those frames wrote, into the box; and at the file's end the play
; over, which the UI task finishes on its wake
vp_lstep:
%ifdef VP_LIVESND
    cmp byte [vp_snd], 0
    je .pit
    jmp vp_lsnd
.pit:
%endif
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [vp_ltk]
    mov [vp_ltk], bx
    add [vp_lacc+2], ax             ; a tick is 65,536 PIT counts
    xor cx, cx
.due:
    mov ax, [vp_lacc]
    mov dx, [vp_lacc+2]
    sub ax, [vp_lper]
    sbb dx, [vp_lper+2]
    jb .go
    mov [vp_lacc], ax
    mov [vp_lacc+2], dx
    inc cx
    cmp cx, 8
    jb .due
    xor ax, ax                      ; hopelessly behind: forgiven
    mov [vp_lacc], ax
    mov [vp_lacc+2], ax
.go:
    jcxz .blit
    cmp cx, VP_LCAP                 ; a pass that ran long owes more than one:
    jbe .f                          ; up to VP_LCAP decoded and one blit for
    sub cx, VP_LCAP                 ; them all, so the picture keeps its time
    add [vp_late], cx               ; and the display rate is what drops
    mov cx, VP_LCAP
.f:
    push cx
    call vp_frame
    pop cx
    jc .nf
    loop .f
    jmp short .blit
.nf:
    cmp byte [vp_end], 0            ; the end (Repeat off): over
    je .blit
    call vp_lblit
    mov byte [vp_lrun], 0
    mov byte [vp_lend], 1
    mov bx, [vp_win]
    call OSAPI_WM_WAKE
    ret
.blit:
    jmp vp_lblit

%ifdef VP_LIVESND
; vp_lsnd - vp_lstep WITH SOUND (98.3.10.1): the card is the clock, as in a
; bracket - the ticks since the last pass become the periods the hook would
; have counted, vp_adue says what is due off the card's consumed count, up
; to VP_LCAP frames are drawn, the box blitted, and the ring topped up from
; the resident audio block. Nothing here is read off a disk
vp_lsnd:
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [vp_ltk]
    mov [vp_ltk], bx
    cmp byte [vp_ldrain], 0         ; the picture done: the sound plays out
    je .run
    jmp .drain
.run:
    cmp ax, 8                       ; (a pass held off longer: the card's
    jbe .t                          ; next word resynchronises it anyway)
    mov ax, 8
.t:
    mov dx, ax                      ; ticks x 65,536 PIT counts, and what
    mov ax, [vp_lrem]               ; was left over, in periods of the
    div word [vp_pdiv]              ; file's divisor
    mov [vp_lrem], dx
    add [vp_pers], ax
    call vp_adue                    ; CX = frames due
    cmp byte [vp_rep], 0            ; every frame drawn and no Repeat: the
    jne .nl                         ; clock stops at the last, so the end is
    mov ax, [vp_done]               ; found here and not by vp_frame - the
    cmp ax, [vp_frames]             ; bracket's play loop's own check
    jb .nl
    mov byte [vp_end], 1
    jmp short .last
.nl:
    jcxz .blit
    cmp cx, VP_LCAP
    jbe .f
    sub cx, VP_LCAP
    add [vp_late], cx
    mov cx, VP_LCAP
.f:
    push cx
    call vp_frame
    pop cx
    jc .nf
    loop .f
    jmp short .blit
.nf:
    cmp byte [vp_end], 0            ; the end (Repeat off): the last frame
    je .blit                        ; on the screen, and the sound to its
.last:
    call vp_lblit                   ; last byte - the bracket's drain, a pass
    mov byte [vp_ldrain], 1         ; at a time (98.3.10.1)
    call OSAPI_GET_TICKS
    mov [vp_tdr], ax
    ret
.blit:
    call vp_lblit
.feed:
    call vp_afill                   ; the sound a few frames ahead of the card
    jmp vp_skeep                    ; ...and a card paused for want of it on
.drain:
    call vp_adue                    ; (the card's count, and syncf, taken)
    cmp byte [vp_aend], 2           ; the audio ran out early: nothing to wait
    je .over                        ; for
    mov ax, [vp_aseq]               ; Repeat turned off after the sound had
    cmp ax, [vp_vseq]               ; queued the next lap: over when the card
    jbe .df                         ; has played the frames drawn
    mov ax, [vp_syncf]
    cmp ax, [vp_vseq]
    jae .over
    jmp short .dw
.df:
    cmp byte [vp_aend], 0
    je .dw
    mov ax, [vp_alast]              ; played past the last byte of sound?
    sub ax, [vp_afinal]
    jns .over
.dw:
    call OSAPI_GET_TICKS            ; ...or two seconds: a card that has
    sub ax, [vp_tdr]                ; stopped does not hold the play
    cmp ax, 37
    jb .feed
.over:
    mov byte [vp_ldrain], 0
    mov byte [vp_lrun], 0
    mov byte [vp_lend], 1
    mov bx, [vp_win]
    call OSAPI_WM_WAKE              ; (a far cell: called, never jumped to)
    ret
%endif

; vp_lblit - the shadow's rows the frames since the last blit wrote, into the
; box at their place, through the window's clip - if the window shows and the
; About card is not over it; and the thumb, if it moved. Lock held
vp_lblit:
    mov bx, [vp_dy1]
    cmp bx, [vp_dy0]
    jbe .out
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [vp_win]
    test word [es:bx + W_FLAGS], 2
    jz .skip
    cmp byte [vp_abon], 0
    jne .skip
    call vp_track
    call vp_boxxy
    mov bx, [vp_win]
    call OSAPI_WM_CLIP_SET
    jc .skip
    mov byte [vp_v4p], 1            ; IN COLOUR (98.3.10.4): the planes as
    mov es, [vp_shseg]              ; they are, until BLITP refuses
    mov bp, 80
    cmp byte [vp_fruns], 0          ; ONLY WHAT THE FRAMES WROTE (98.3.10.2):
    je .hull                        ; the runs they carried...
    cmp byte [vp_lfull], 0
    jne .hull
    call vp_lruns
    jmp short .th
.hull:                              ; ...or the band whole: a file with none,
    mov ax, [vp_dy0]                ; or a band something else put there (a
    mul bp                          ; key, a seam, a start)
    mov si, ax
    mov ax, [vp_px]
    mov cx, [vp_pdw]
    mov bx, [vp_py]
    add bx, [vp_dy0]
    mov dx, [vp_dy1]
    sub dx, [vp_dy0]
    call vp_blitb
.th:
    call vp_thumbx                  ; the thumb, where it has got to
    cmp ax, [vp_wtx]
    je .nt
    call vp_pthumb                  ; (what it moved by, not the bar)
.nt:
    call OSAPI_WM_CLIP_CLEAR
.skip:
    pop es
    mov byte [vp_lfull], 0          ; no runs owed, and the band is empty
    mov byte [vx_n], 0              ; again
    mov word [vp_dy0], 0xFFFF
    mov word [vp_dy1], 0
.out:
    ret

; -----------------------------------------------------------------------------
; LIVE'S BLIT, NARROWED (98.3.10.2). A pass's blit was the HULL of the rows
; its frames wrote at the canvas's width - on the logo 7,173 bytes a frame
; for 232 changed, gfx_blit1 33% of a 5150 on CGA and 45% on Hercules where
; the decode was 2%. Working out the written columns at playback costs what
; it saves (VIDEO-PLAN 15.8), so the ENCODER writes them: a live file's
; frame records carry their blit runs after their lists (98.1.3.4), and a
; pass gathers its frames' runs into [vx_run], a run whose rows meet one
; already there merged into it, and blits each. [vp_lfull] is a band put
; there by anything but a frame - a key, a seam, a start - blitted whole
; -----------------------------------------------------------------------------
VX_MAX      equ 16                  ; a pass's runs, gathered

; vp_bandall - the whole canvas owed to the screen: a copy's band, and on a
; live play the blit whole. Preserves all
vp_bandall:
    push ax
    mov word [vp_dy0], 0
    mov ax, [vp_h]
    mov [vp_dy1], ax
    mov byte [vp_lfull], 1
    pop ax
    ret

; vp_lrget - DS:SI = a live record's blit runs, the lists just decoded: each
; into the pass's, checked against the canvas - one that is not, or more
; than the pass holds, and the band is blitted whole instead. DS is the
; record's. Clobbers AX, BX, CX, DX, DI
vp_lrget:
    cmp byte [cs:vp_lfull], 0       ; (whole already: nothing to gather)
    jne .ret
    cmp word [cs:vp_h], 255         ; (a run's rows are bytes)
    ja .full
    lodsb
    cmp al, 32
    ja .full
    mov cl, al
    xor ch, ch
    or cx, cx
    jnz .r
    ret
.r:
    lodsw                           ; AL = the first row, AH = the rows
    mov dx, ax
    lodsw                           ; AL = the first byte, AH = the bytes
    or dh, dh
    jz .full
    or ah, ah
    jz .full
    mov bl, dl                      ; past its last row, and byte
    add bl, dh
    jc .full
    cmp bl, [cs:vp_h]
    ja .full
    mov bh, al
    add bh, ah
    jc .full
    cmp bh, [cs:vp_wb]
    ja .full
    ; DL..BL rows, AL..BH bytes: onto a gathered run whose rows it meets
    push cx
    xor di, di
    mov cl, [cs:vx_n]
    xor ch, ch
    jcxz .add
.m:
    cmp dl, [cs:vx_run+di+1]        ; rows [DL, BL) meet [y0, y1)?
    jae .nm
    cmp bl, [cs:vx_run+di]
    jbe .nm
    cmp dl, [cs:vx_run+di]          ; the union
    jae .m1
    mov [cs:vx_run+di], dl
.m1:
    cmp bl, [cs:vx_run+di+1]
    jbe .m2
    mov [cs:vx_run+di+1], bl
.m2:
    cmp al, [cs:vx_run+di+2]
    jae .m3
    mov [cs:vx_run+di+2], al
.m3:
    cmp bh, [cs:vx_run+di+3]
    jbe .nx
    mov [cs:vx_run+di+3], bh
    jmp short .nx
.nm:
    add di, 4
    loop .m
.add:
    cmp byte [cs:vx_n], VX_MAX
    jae .fullp
    mov [cs:vx_run+di], dl
    mov [cs:vx_run+di+1], bl
    mov [cs:vx_run+di+2], al
    mov [cs:vx_run+di+3], bh
    inc byte [cs:vx_n]
.nx:
    pop cx
    dec cx
    jz .ret
    jmp .r
.ret:
    ret
.fullp:
    pop cx
.full:
    mov byte [cs:vp_lfull], 1
    ret

; vp_lruns - the pass's runs into the box: rows [y0, y1) at the bytes
; [x0, x1) of each, cut where the box shows no more. ES:0 the shadow, BP =
; 80; after vp_track and vp_boxxy, the clip set. Clobbers AX, BX, CX, DX,
; SI, DI
vp_lruns:
    xor di, di
.r:
    mov al, [vx_n]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    cmp di, ax
    jae .ret
    mov al, [vx_run+di+2]           ; its left, in pixels into the canvas...
    xor ah, ah
    mov cl, 3
    shl ax, cl
    mov cx, [vp_pdw]                ; ...and its width, cut where the box
    sub cx, ax                      ; shows no more
    jbe .nx
    mov dl, [vx_run+di+3]
    sub dl, [vx_run+di+2]
    xor dh, dh
    push cx
    mov cl, 3
    shl dx, cl
    pop cx
    cmp cx, dx
    jbe .w
    mov cx, dx
.w:
    push ax
    mov al, [vx_run+di]             ; the shadow's first byte of it
    xor ah, ah
    mul bp
    mov si, ax
    mov al, [vx_run+di+2]
    xor ah, ah
    add si, ax
    pop ax
    add ax, [vp_px]
    mov bl, [vx_run+di]
    xor bh, bh
    add bx, [vp_py]
    mov dl, [vx_run+di+1]
    sub dl, [vx_run+di]
    xor dh, dh
    call vp_blitb
.nx:
    add di, 4
    jmp short .r
.ret:
    ret

; -----------------------------------------------------------------------------
; vp_rload - a RESIDENT file's blocks (98.1.7) in memory: the rendition's
; picture block and the audio block, each read so its clusters END at the
; top of a claim of its unpacked size and a cluster - the packed stream is
; then above where it expands to, which SPEC.md 20.13.7's raw tail makes
; enough - and OSAPI_DECOMP'd down to the claim's base. Then walked: every
; record no shorter than an empty one and inside the block, the frames'
; count of them and the seam after them with LOOPREC, and where the seam
; and frame L+1 are - and, for a play that decodes into the keeper, every
; write against the canvas (98.1.7.3), once per load. Kept while the file
; is open (vp_rfree). CF=1 with [vp_msg] set
vp_rload:
    cmp word [vp_rblk], 0
    jne .have
    mov bx, vp_bk                   ; ASKED FIRST, both blocks at once: a
    call vp_bkkb                    ; refused claim sheds the caches for
    mov cx, ax                      ; nothing (98.1.7.1)
    cmp byte [vp_audio], 0
    je .ask
    mov bx, vp_abk
    call vp_bkkb
    add cx, ax
.ask:
    mov ax, cx
    call vp_fits
    jc .out
    mov bx, vp_bk
    call vp_ldblk
    jc .out
    mov [vp_rblk], ax
    cmp byte [vp_audio], 0
    je .walk
    mov bx, vp_abk
    call vp_ldblk
    jc .free                        ; (its own words)
    mov [vp_rablk], ax
.walk:
    call vp_rwalk
    jc .bad
    mov ax, vp_rmove                ; ONLY NOW movable (98.1.7.4): loaded
    call vp_rmov                    ; and walked, and the next claim may move
    jmp short .ok                   ; them
.bad:
    mov word [vp_msg], vp_s_bad
.free:
    call vp_rfree
    stc
    ret
.have:
    mov al, [vp_kneed]              ; LOADED ALREADY, for a play onto the
    cmp al, [vp_rchk]               ; screen: its writes are checked the
    jbe .ok                         ; first time one decodes into the keeper
    call vp_rwalk
    jc .bad
.ok:
    clc
.out:
    ret

; vp_rmov - AX = vp_rmove, or 0: each loaded block declared MOVABLE, or
; pinned again (98.1.7.4). A bracket pins them for its length - its hook
; reads the block at interrupt time, and no relocation proc can reach
; that. Preserves all
vp_rmov:
    push dx
    mov dx, [vp_rblk]
    call vp_mmovz
    mov dx, [vp_rablk]
    call vp_mmovz
    pop dx
    ret

; vp_rmove - THE RESIDENT BLOCKS' RELOCATION PROC (SPEC.md 66.3, 98.1.7.4):
; BX = the old base, DX = the new. Every word derived from that block - a
; segment inside its claim - moved with it; anything outside the claim is
; not the block's, whatever its name (a streamed play's cursor is a slot
; number). vp_raud derives the sound's from its base at each use, so that
; block has only the one. Clobbers AX, BX, CX, DX, SI, DI
vp_rmove:
    cld
    mov si, vp_rmtab
    mov di, vp_bk
    cmp bx, [vp_rblk]
    je .go
    mov si, vp_ramtab
    mov di, vp_abk
    cmp bx, [vp_rablk]
    jne .out
.go:
    push bx
    mov bx, di
    call vp_bkkb                    ; AX = the claim's KB
    pop bx
    mov cl, 6
    shl ax, cl
    mov cx, ax                      ; CX = its paragraphs
    sub dx, bx                      ; DX = the delta
.w:
    lodsw
    or ax, ax
    jz .out
    mov di, ax
    mov ax, [di]
    sub ax, bx
    cmp ax, cx
    jae .w                          ; not inside the block
    add [di], dx
    jmp short .w
.out:
    ret

vp_rmtab:     dw vp_rblk, vp_rbseg, vp_rlseg, vp_rsseg, vp_pc, va_pc, 0
vp_ramtab:    dw vp_rablk, 0

; vp_ldblk - BX = a block's fields: claimed, read and expanded. out CF=0 AX
; = the claim; CF=1 [vp_msg] says why and nothing is held
vp_ldblk:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vp_bkkb
    mov [vp_ldkb], ax
    call vp_mclaim
    jnc .got
    mov word [vp_msg], vp_s_mem
    jmp .err
.got:
    mov [vp_ldseg], dx
    cmp byte [bx+BK_PACK], 0        ; STORED: in pieces, straight into place
    jne .rd1
    call vp_ldstored
    jc .io
    jmp .ok
.rd1:
    call vp_bkrd                    ; DI:CX = the read, whole clusters -
    mov ax, [vp_ldkb]               ; which vp_bkkb has put inside the claim
    mov dx, 64
    mul dx
    add ax, [vp_ldseg]              ; AX = the claim's end, a paragraph
    push ax
    mov ax, cx                      ; DI:CX >> 4 = the read in paragraphs
    mov dx, di
    mov cx, 4
.rp:
    shr dx, 1
    rcr ax, 1
    loop .rp
    mov cx, ax
    pop ax
    sub ax, cx
    mov [vp_rdseg], ax
    mov ax, [bx+BK_OFF]
    mov dx, [bx+BK_OFF+2]
    mov cx, [bx+BK_PACKED]
    call vp_rdat                    ; SI = where the block landed
    jc .io
    mov ax, si                      ; ...as a segment of its own: SI and 60
    mov cl, 4                       ; KB can pass the claim's first 64 KB
    shr ax, cl                      ; when a cluster is 32 KB (98.1.7.5)
    add [vp_rdseg], ax
    and si, 15
    mov cl, [bx+BK_PACK]
    dec cl
    mov al, cl                      ; AL = OSAPI_LZ_*
    mov cx, [bx+BK_PACKED]
    mov dx, [bx+BK_UNPACKED]
    mov bx, [bx+BK_UNPACKED+2]
    push ds
    mov es, [vp_ldseg]
    xor di, di
    mov ds, [vp_rdseg]
    call OSAPI_DECOMP
    pop ds
    jc .bad
.ok:
    mov ax, [vp_ldseg]
    clc
    jmp short .out
.io:
    mov word [vp_msg], vp_s_io
    jmp short .free
.bad:
    mov word [vp_msg], vp_s_bad
.free:
    mov dx, [vp_ldseg]
    call vp_mfree
.err:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; vp_bkkb - BX = a block's fields: AX = the KB its claim takes - the
; unpacked bytes and a cluster, two when STORED (read in whole clusters from
; the one under its start). Clobbers nothing else
vp_bkkb:
    push cx
    push dx
    mov ax, [bx+BK_UNPACKED]
    mov dx, [bx+BK_UNPACKED+2]
    add ax, [vp_rgb]
    adc dx, 0
    cmp byte [bx+BK_PACK], 0
    jne .pk
    add ax, [vp_rgb]
    adc dx, 0
    jmp short .kb
.pk:                                ; PACKED: or the READ, when that is
    push di                         ; bigger, so the read that ends at the
    call vp_bkrd                    ; claim's top starts inside it - a
    cmp dx, di                      ; packing need not shrink, and the
    ja .pkd                         ; cluster under its start adds to it
    jb .rdk                         ; (98.1.7; in 32 bits, 98.1.7.5)
    cmp ax, cx
    jae .pkd
.rdk:
    mov ax, cx
    mov dx, di
.pkd:
    pop di
.kb:
    add ax, 1023
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    pop dx
    pop cx
    ret

; vp_bkrd - BX = a PACKED block's fields: DI:CX = its read - from the
; cluster under its start, in whole clusters, which is what vp_rdat reads.
; In 32 bits: 60 KB packed and a 32 KB cluster either side is past a word,
; and a 16-bit sum refused the file as damaged (98.1.7.5). Preserves all
; else
vp_bkrd:
    push ax
    push si
    mov si, [vp_rgb]
    dec si
    and si, [bx+BK_OFF]             ; SI = into its cluster
    xor di, di
    mov cx, [bx+BK_PACKED]
    add cx, si
    adc di, 0
    mov ax, [vp_rgb]
    dec ax
    add cx, ax
    adc di, 0
    not ax
    and cx, ax
    pop si
    pop ax
    ret

; vp_fits - AX = the KB the blocks take: does the machine have them, in the
; one run a claim is served from? The deliverable - caches shed and the heap
; compacted, OSAPI_MEM_AVAIL - so a claim of that many is SERVED. CF=1 no:
; [vp_msg] = "Needs n KB of memory, m KB free" (vp_mbuf), nothing claimed
; and nothing shed. Preserves all
vp_fits:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov cx, ax
    call vp_mavail                  ; AX = the largest a claim is served
    cmp cx, ax
    jbe .yes
    push ds
    pop es
    mov di, vp_mbuf                 ; the line: filled, NUL at its end, so
    push ax                         ; vp_putc writes up to it
    push cx
    mov al, ' '
    mov cx, VP_MBUF - 1
    cld
    rep stosb
    mov [di], ch                    ; (CX = 0)
    pop cx
    pop ax
    mov di, vp_mbuf
    mov si, vp_s_needs
    push ax
    mov ax, cx
    call vp_psw
    mov si, vp_s_kbnd
    pop ax
    call vp_psw
    mov si, vp_s_kbfree
    call vp_puts
    mov byte [di], 0
    mov word [vp_msg], vp_mbuf
    stc
    jmp short .out
.yes:
    clc
.out:
    jmp vp_ld7

; vp_ldstored - BX = a STORED block's fields, [vp_ldseg] its claim: read in
; whole clusters from the one under its start, 32 KB a call (vp_rdat's
; bound), each piece where it belongs and each one starting where the last
; ended, so vp_rdat's cursor walks the chain ONCE for the block rather than
; once a piece from the front (READ_AT's cost, which this paid until
; docs/plans/NAV-COST-PLAN.md's sweep) - then the whole moved down by the
; start's place in its cluster, a forward copy (the source is above the
; destination) stepped 32 KB at a time across the segments. Any size under
; 1 MB (98.1.7.1). CF=1 the disk failed. Clobbers AX, CX, DX, SI, DI
vp_ldstored:
    push bx
    push bp
    mov ax, [bx+BK_OFF]
    mov dx, [bx+BK_OFF+2]
    mov si, [vp_rgb]
    dec si
    and si, ax                      ; SI = into its cluster
    sub ax, si                      ; DX:AX = the cluster's own offset
    sbb dx, 0
    mov [vp_lsin], si
    mov cx, [bx+BK_PACKED]          ; [vp_lsrem] = the bytes from there to
    mov di, [bx+BK_PACKED+2]        ; the block's end
    add cx, si
    adc di, 0
    mov [vp_lsrem], cx
    mov [vp_lsrem+2], di
    mov bp, [vp_ldseg]              ; BP = where the next piece goes
.piece:
    call .min                       ; 32 KB, or what is left
    jcxz .move
    mov [vp_rdseg], bp
    call vp_rdat                    ; (a cluster boundary: SI comes back 0)
    jc .out
    add ax, cx                      ; on by the piece
    adc dx, 0
    sub [vp_lsrem], cx
    sbb word [vp_lsrem+2], 0
    add bp, 0x0800
    jmp short .piece
.min:                               ; CX = 32 KB, or [vp_lsrem] if that is less
    mov cx, 0x8000
    cmp word [vp_lsrem+2], 0
    jne .m0
    cmp [vp_lsrem], cx
    jae .m0
    mov cx, [vp_lsrem]
.m0:
    ret
.move:
    mov si, [vp_lsin]               ; down by SI, if it did not start on one
    or si, si
    jz .done
    mov cx, [bx+BK_PACKED]
    mov [vp_lsrem], cx
    mov cx, [bx+BK_PACKED+2]
    mov [vp_lsrem+2], cx
    mov dx, [vp_ldseg]              ; DX:0 the destination, BP:SI the source:
    mov bp, dx                      ; both on 32 KB a piece, so SI stays
.mv:
    call .min
    jcxz .done
    sub [vp_lsrem], cx
    sbb word [vp_lsrem+2], 0
    push ds
    push si
    mov es, dx
    xor di, di
    mov ds, bp
    cld
    rep movsb
    pop si
    pop ds
    add dx, 0x0800
    add bp, 0x0800
    jmp short .mv
.done:
    clc
.out:
    pop bp
    pop bx
    ret

; vp_rwalk - the loaded block's records: counted, each checked, and where
; the seam and frame L+1 are noted - and, where this session decodes into
; the keeper ([vp_kneed]), every list of every record parsed, never
; applied, against the canvas (vp_rbnd, 98.1.7.3), [vp_rchk] then set.
; CF=1 not sound
vp_rwalk:
    call vp_cbound
    mov ax, [vp_rblk]
    mov [vp_rbseg], ax
    mov word [vp_rboff], 0
    mov dx, ax                      ; DX:SI the walk; BP:DI the block's end,
    xor si, si                      ; as a paragraph and an offset
    mov ax, [vp_bk+BK_UNPACKED]
    mov bp, [vp_bk+BK_UNPACKED+2]
    mov di, ax
    and di, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl bp, cl
    or ax, bp
    add ax, dx
    mov bp, ax                      ; BP = the end's paragraph, DI its offset
    mov cx, [vp_frames]
    mov ax, 0xFFFF                  ; the index of frame L+1, or none
    cmp byte [vp_lkind], 1
    jne .n
    inc cx                          ; ...and the seam after the frames
    mov ax, [vp_lL]
    inc ax
.n:
    mov [vp_tmp], ax
    xor bx, bx                      ; BX = the record's index
.r:
    cmp bx, cx
    jae .end
    cmp bx, [vp_tmp]
    jne .n1
    mov [vp_rlseg], dx              ; frame L+1's record
    mov [vp_rloff], si
.n1:
    cmp bx, [vp_frames]
    jne .n2
    mov [vp_rsseg], dx              ; the seam's
    mov [vp_rsoff], si
.n2:
    push ds
    mov ds, dx
    mov ax, [si]
    pop ds
    cmp ax, 7                       ; no shorter than an empty record
    jb .bad
    cmp byte [vp_planar], 0
    jne .pk
    cmp ax, 16
    jb .bad
.pk:
    cmp byte [vp_kneed], 0          ; decoded into the keeper: its writes,
    je .mn                          ; inside the canvas - each plane's
    call vp_rbnd
    jc .bad
.mn:
    cmp byte [vp_own], 0            ; (none kept against its own page)
    jne .mf
    cmp byte [vp_flip], 1           ; ...no longer than a flipped play's
    jne .mf                         ; copy of the last record (98.3.8)
    cmp ax, VP_PREVKB * 1024
    ja .bad
.mf:
    add si, ax                      ; step over it, normalised
    jc .bad
    mov ax, si
    and si, 15
    push cx
    mov cl, 4
    shr ax, cl
    pop cx
    add dx, ax
    cmp dx, bp                      ; ...and still inside the block
    ja .bad
    jb .in
    cmp si, di
    ja .bad
.in:
    inc bx
    jmp short .r
.end:
    cmp dx, bp                      ; the block is its records exactly
    jne .bad
    cmp si, di
    jne .bad
    mov al, [vp_kneed]              ; (checked, if it was asked)
    or [vp_rchk], al
    clc
    ret
.bad:
    stc
    ret

; vp_cbound - AX = [vp_cbnd] = the canvas's BOUND in the file's layout
; (98.1.7.3): past the last byte of the lowest-ending row of every bank,
; each row a whole stride - which is all a reader of the image reads (the
; copy, the poster, the keeper's moves). Preserves all but AX
vp_cbound:
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, [vp_layout]
    call vp_lay6                    ; BX = layout * 6
    mov cl, [vp_laytab+bx+1]
    xor ch, ch                      ; CX = the banks: the last row of each
    mov di, [vp_laytab+bx+2]        ; is among the canvas's last CX rows
    mov si, [vp_h]
    xor dx, dx                      ; DX = the bound so far
.b:
    or si, si
    jz .d
    dec si
    mov ax, si
    mov bl, [vp_layout]
    call vp_rowaddr
    add ax, di
    cmp ax, dx
    jbe .n
    mov dx, ax
.n:
    loop .b
.d:
    mov ax, dx
    mov [vp_cbnd], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; vp_rbnd - DX:SI = a record: its ten lists (98.1.3) PARSED and never
; applied - every list inside the record's own length, every write below
; [vp_cbnd] and none wrapping the segment. What vd_native trusts, checked
; once, so that a resident play's keeper can be the canvas (98.1.7.3) and
; not the 64 KB a list's 16-bit reach needs. A poke segment's bytes and its
; writes' end follow from its count, so its entries are checked by nothing
; but the one sum; a span's are its own lengths. CF=1 not. Preserves all
vp_rbnd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    mov ds, dx
    mov bp, si
    add bp, [si]                    ; BP = the record's end
    jc .bad
    cmp bp, 0xFF00                  ; (so no step below can wrap SI)
    ja .bad
    add si, 6
    mov dx, [cs:vp_cbnd]            ; DX = the bound
    cld
    cmp byte [cs:vp_planar], 0
    je .one
.sub:                               ; PLANAR (98.1.3.1): a Map Mask and its
    cmp si, bp                      ; lists, each plane's bound the canvas's,
    jae .bad                        ; and a 0 after the last
    lodsb
    or al, al
    jz .out                         ; (CF=0)
    cmp al, 15
    jbe .lead
    cmp byte [cs:vp_sub2], 0        ; SUB2: VGA4's colour leads too
    je .bad                         ; (98.1.3.2.4)
    cmp byte [cs:vp_planar], 2
    jne .bad
    cmp al, 0x1F
    ja .bad
.lead:
    call .lmap
    jc .bad
    call .lists
    jc .bad
    jmp short .sub
.one:
    mov word [cs:vd_map], 0xFFFF
    call .lists
    jnc .out
.bad:
    stc
.out:
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.lmap:                              ; a SUB2 sub-record's LIST MAP into
    mov word [cs:vd_map], 0xFFFF    ; [vd_map]: not 0 and naming no list past
    cmp byte [cs:vp_sub2], 0        ; the tenth (98.1.3.2.4). Any other
    je .lmr                         ; file's, all ten. CF=1 not
    mov ax, si
    add ax, 2
    cmp ax, bp
    ja .lmn
    lodsw
    or ax, ax
    jz .lmn
    test ax, 0xFC00
    jnz .lmn
    mov [cs:vd_map], ax
.lmr:
    clc
    ret
.lmn:
    stc
    ret
.lists:                             ; the lists at SI that [vd_map] names.
    mov bx, 1                       ; CF=1 not. The six poke lists: BX-byte
.pl:                                ; changes
    call .gp
    jc .lr
    inc bx
    cmp bx, 6
    jbe .pl
    shr word [cs:vd_map], 1
    jnc .gs
    call .slice
    jc .lr
.gs:
    shr word [cs:vd_map], 1
    jnc .gr
    call .run
    jc .lr
.gr:
    mov bx, 1                       ; SLICEL: a word's length, and the bytes
    shr word [cs:vd_map], 1
    jnc .gl
    call .long
    jc .lr
.gl:
    xor bx, bx                      ; RUNL: a word's length, and one value
    shr word [cs:vd_map], 1
    jnc .lr                         ; (CF=0)
    call .long
.lr:
    ret
.gp:                                ; one poke list, if the map names it
    shr word [cs:vd_map], 1         ; (CF=0 if not)
    jc .poke
    ret
.poke:                              ; one list of BX-byte changes
    xor ax, ax
    cmp si, bp
    jae .n1
    lodsb                           ; the count, 0 the list's end
    or al, al
    jz .r1                           ; (CF=0)
    test al, 0x80
    jnz .pabs
    mov cx, ax                      ; SKIP-CODED: an address, then per change
    mov al, bl                      ; a skip and BX bytes - so 2 + c(n + 1)
    mul cl                          ; bytes, and the writes end at the address
    mov di, ax                      ; + c x n + the skips
    add ax, cx
    inc ax
    inc ax
    add ax, si
    jc .n1
    cmp ax, bp
    ja .n1
    lodsw
    add di, ax
    jc .n1
    xor ax, ax
.pe:
    lodsb
    add di, ax
    jc .n1
    add si, bx
    loop .pe
    cmp di, dx
    jbe .poke
.n1:                                ; (.no and .r again, where the lists
    stc                             ; above and below reach them with a
.r1:                                ; short jump)
    ret
.pabs:
    and al, 0x7F                    ; ABSOLUTE: an address each - and a
    jz .n1                          ; count of 0 the decoder takes as 524,288
    mov cx, ax
    mov al, bl
    inc ax
    inc ax
    mul cl                          ; c(n + 2) bytes
    add ax, si
    jc .n1
    cmp ax, bp
    ja .n1
    mov di, dx
    sub di, bx                      ; DI = the highest start a change may have
    jc .n1
.pa:
    lodsw
    cmp ax, di
    ja .n1
    add si, bx
    loop .pa
    jmp .poke
.slice:                             ; SLICE: a skip, len8, the bytes
    call .hd
    jbe .r1
.sle:
    lodsb
    add di, ax
    jc .n1
    lodsb
    add di, ax
    jc .n1
    add si, ax
    cmp si, bp
    ja .n1
    loop .sle
    cmp di, dx
    jbe .slice
    jmp .no
.run:                               ; RUN: a skip, len8, the value
    call .hd
    jbe .r1
.rue:
    lodsb
    add di, ax
    jc .no
    lodsb
    add di, ax
    jc .no
    inc si
    cmp si, bp
    ja .no
    loop .rue
    cmp di, dx
    jbe .run
    jmp .no
.long:                              ; SLICEL (BX 1) or RUNL (BX 0): a skip,
    call .hd                        ; len16, the bytes or the value
    jbe .r
.le:
    xor ax, ax
    lodsb
    add di, ax
    jc .no
    lodsw
    add di, ax
    jc .no
    or bx, bx
    jnz .lsl
    mov ax, 1
.lsl:
    add si, ax
    jc .no
    cmp si, bp
    ja .no
    loop .le
    cmp di, dx
    jbe .long
    jmp .no
.hd:                                ; a span list's segment head: CF=1 not
    xor ax, ax                      ; sound, else ZF=1 the list's end, or
    cmp si, bp                      ; CX = its spans and DI their address -
    jae .no                         ; so JBE to a ret leaves with either
    lodsb
    or al, al
    jz .r
    mov cx, ax
    call .addr
    jc .r
    or cx, cx                       ; (ZF=0, CF=0)
    ret
.addr:                              ; a span segment's address into DI, and
    mov ax, bp                      ; AX 0 again
    sub ax, si
    cmp ax, 2
    jb .no
    lodsw
    mov di, ax
    xor ax, ax
    ret
.no:
    stc
.r:
    ret

; vp_rcur - a RESIDENT session's cursor: [vp_base] records into the block,
; the frames left in the lap, and how the lap joins the next - the block's
; seam (kind 1) or its start again over a cleared canvas (kind 0)
vp_rcur:
    push cx
    mov dx, [vp_rbseg]
    mov si, [vp_rboff]
    mov cx, [vp_base]
    jcxz .d
.l:
    push ds
    mov ds, dx
    mov ax, [si]
    pop ds
    add si, ax
    mov ax, si
    and si, 15
    push cx
    mov cl, 4
    shr ax, cl
    pop cx
    add dx, ax
    loop .l
.d:
    mov [vp_pc], dx
    mov [vp_po], si
    mov ax, [vp_frames]
    sub ax, [vp_base]
    mov [vp_fleft], ax
    mov al, [vp_lkind]
    mov [vp_wkind], al
    cmp al, 1
    mov ax, [vp_lL]                 ; (MOV: the compare's flags stand)
    je .w
    mov ax, 0xFFFF
.w:
    mov [vp_wL], ax
    pop cx
    ret

; vp_rnext - vp_nextw for a RESIDENT play (DI = the cursor): the next
; record where it lies in
; the block (checked by vp_rwalk, once), or at the lap's end the seam - its
; record and then frame L+1's, or none and then the block's start
vp_rnext:
    cmp word [di+VC_FLEFT], 0
    jne .rec
    cmp byte [vp_rep], 0
    je .end
    mov byte [vp_nseam], 1
    cmp byte [vp_wkind], 1
    jne .k0
    mov ax, [vp_rlseg]
    mov [di+VC_PC], ax
    mov ax, [vp_rloff]
    mov [di+VC_PO], ax
    mov ax, [vp_frames]
    sub ax, [vp_wL]
    dec ax
    mov [di+VC_FLEFT], ax
    mov dx, [vp_rsseg]
    mov si, [vp_rsoff]
    push ds
    mov ds, dx
    mov cx, [si]
    pop ds
    clc
    ret
.k0:
    mov ax, [vp_rbseg]
    mov [di+VC_PC], ax
    mov ax, [vp_rboff]
    mov [di+VC_PO], ax
    mov ax, [vp_frames]
    mov [di+VC_FLEFT], ax
    xor dx, dx
    xor si, si
    xor cx, cx
    clc
    ret
.rec:
    mov dx, [di+VC_PC]
    mov si, [di+VC_PO]
    push ds
    mov ds, dx
    mov cx, [si]
    pop ds
    mov ax, si
    add ax, cx
    push cx
    mov cx, ax
    and ax, 15
    mov [di+VC_PO], ax
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    add cx, dx
    mov [di+VC_PC], cx
    pop cx
    dec word [di+VC_FLEFT]
    clc
    ret
.end:
    mov al, 1
    stc
    ret

; vp_raud - AX = a frame: DX:SI = its audio in a RESIDENT file's audio
; block. clobbers AX, CX
vp_raud:
    mul word [vp_abytes]            ; DX:AX = the byte offset
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [vp_rablk]
    mov dx, ax
    ret

; vp_rfree - the loaded blocks, if any
vp_rfree:
    push dx
    mov dx, [vp_rblk]
    or dx, dx
    jz .a
    call vp_mfree
.a:
    mov dx, [vp_rablk]
    or dx, dx
    jz .z
    call vp_mfree
.z:
    xor dx, dx
    mov [vp_rblk], dx
    mov [vp_rablk], dx
    mov [vp_rchk], dl
    pop dx
    ret

; -----------------------------------------------------------------------------
; vp_sstart - a session, at the key picked: its claims, the reader and the
; hook's state, the clock. CF=1 it could not, [vp_msg] says why
; -----------------------------------------------------------------------------
vp_sstart:
    push bx
    push cx
    push dx
    push si
    push di
    push es
%ifdef VP_DIAG
    call OSAPI_MEM_AVAIL            ; THE HEAP AS PLAY FOUND IT, for the
    mov [vp_mrun0], ax              ; card's memory lines (98.3): the
    mov [vp_mfre0], bx              ; largest claim, and all that is free
    mov word [vp_mrun], 0           ; ...and nothing from the ring's sizing
    call vp_prbeg                   ; THE PROFILE's claim, FIRST: after the
%endif                              ; ring sized itself there was no room on
                                    ; a machine with no XMS (the owner's
                                    ; Victory, 2026-10-16) - so the ring is
                                    ; the claim smaller in VIDEOD and VIDEOR,
                                    ; and a RESIDENT play has no ring
    ; --- KEY 0's ENTRY for a colour play from the start (98.3.5, 98.2.9): a
    ;     colour play starts from key 0 and not from the stream's first
    ;     record, but only once key 0's entry is the one in hand - and a
    ;     POSTER at another key put that one there instead, so the play fell
    ;     back to the first record and showed the pre-roll's paint. The entry
    ;     alone, one short read: the box keeps its poster. And ANY key
    ;     picked whose entry is not the one in hand: a stop for an unmute
    ;     (98.3.17) picks the key at or before it and loads nothing, and
    ;     vp_keyat leaves the entry after it in hand - the play went back
    ;     to frame 0
    mov ax, [vp_sel]
    or ax, ax
    jnz .k0sel
    cmp byte [vp_flip], 2           ; (PAGE-AHEAD starts at key 0 too: its
    je .k0sel                       ; hidden page is preloaded, 98.3.8.3)
    cmp byte [vp_pixfmt], PF_VGA8
    jb .k0ok
.k0sel:
    cmp word [vp_nkeys], 0
    je .k0ok
    cmp ax, [vp_kload]
    je .k0ok
    push ax
    mov ax, [vp_kekb]               ; vp_rdat's buffer for the ENTRY alone:
    call vp_mclaim                  ; a refusal leaves the old start
    jc .k0no
    mov [vp_rdseg], dx
    mov ax, [vp_sel]
    call vp_kent                    ; [vp_ke] and [vp_kload] = the key's
    call vp_rdfree
.k0no:
    pop ax
.k0ok:
    ; --- the sound (SPEC.md 98.3.1): a card, audio in the file, and the ring
    ;     the card will read in place, page-safe and never moved (MC_DMA)
    xor ax, ax
    mov [vp_aseg], ax
    mov [vp_keep], ax
    mov [vp_ring], ax
    mov [vp_rclm], ax
    call vp_sndprep
.nosnd:
    ; --- THE CANVAS KEEPER (98.3.7): the file's own layout's memory image,
    ;     black. Where a bracket decodes through the SHADOW (98.3.2) it IS the
    ;     shadow, and so 64 KB whatever the layout - a list is not checked
    ;     entry by entry and its writes reach anywhere in ES (98.1.6), so the
    ;     claim is the bound. Where every bracket decodes onto the screen it is
    ;     only the image, read back from the screen as a bracket ends. Before
    ;     the ring, which takes what is left
    call vp_dinfo
    mov byte [vp_kneed], 0
    mov ax, 64
    cmp byte [vp_planar], 0         ; four planes' image (98.1.3.1)
    je .kr
    cmp byte [vp_resid], 0          ; a LIVE one decodes INTO it (98.3.10.4):
    je .kps                         ; its planes' writes checked as a one-bit
    cmp byte [vp_livem], 0          ; canvas's are (98.1.7.3) - the four
    je .kpl                         ; planes are the claim exactly
    mov byte [vp_kneed], 1
    jmp short .kpl                  ; (checked at load: the planes exactly)
.kps:
    cmp byte [vp_livem], 0          ; A LIVE STREAM (98.3.18.1): nothing
    je .kpl                         ; checks its records ahead, so the claim
    call vp_plshd                   ; is the bound, as a keyframe's is
    jmp short .kc                   ; (VP_MXSHD): plane 3's base + 64 KB, and
                                    ; a 16-bit write from any plane stays in
                                    ; it - a planar keyframe's own claim
.kpl:
    mov ax, [vp_plsp]               ; 4 x plsp paragraphs, in KB - 64
    shl ax, 1                       ; paragraphs to the KB (it was 16: the
    shl ax, 1                       ; keeper was claimed four times over,
    add ax, 63                      ; and the ring starved or refused)
    mov cl, 6
    shr ax, cl
    jmp short .kc
.kr:
    cmp byte [vp_resid], 0          ; RESIDENT (98.1.7.3): the CANVAS, shadow
    je .kx                          ; or not. Decoded INTO (the shadow, Live)
    cmp byte [vp_fsshd], 0          ; the block's writes are checked against
    jne .kn                         ; it once (vp_rwalk) and a key's as it is
    cmp byte [vp_livem], 0          ; read (vp_spos); decoded onto the screen
    jne .kn                         ; it holds only what vp_kmove puts there.
    mov bl, [vp_layout]             ; Either way the canvas's own size is the
    cmp bl, [vp_dlay]               ; bound, and what a 64 KB shadow did not
    je .kb                          ; hold goes to the clip
.kn:
    mov byte [vp_kneed], 1
.kb:
    call vp_cbound
    add ax, 1023
    mov cl, 10
    shr ax, cl
    jmp short .kc
.kx:
    cmp byte [vp_fsshd], 0
    jne .kc
    cmp byte [vp_livem], 0          ; LIVE: the keeper is the shadow (98.3.10)
    jne .kc
    mov bl, [vp_layout]
    cmp bl, [vp_dlay]
    jne .kc
    xor bh, bh
    mov al, [vp_laykb+bx]
    xor ah, ah
.kc:
    mov [vp_kkb], al                ; (what vp_zero clears of it)
    xor dx, dx
    mov byte [vp_nokeep], 1         ; PAGE-AHEAD (98.3.8.3): no keeper - the
    cmp byte [vp_flip], 2           ; hidden page is half a frame to come,
    je .kok                         ; which no keeper holds, so out of the
    mov [vp_nokeep], dl             ; full screen it stops at the key at or
    mov [vp_pcv], dl                ; before, as a play without one does
                                    ; (the poster holds no frame of this yet)
    call vp_pcanv                   ; THE POSTER IS THE KEEPER (98.3.19.3):
    jc .kel                         ; at the video's own size it IS the
    mov byte [vp_nokeep], 2         ; canvas, one-bit or VGA4, and a play
    xor dx, dx                      ; that draws onto the screen needs no
    jmp .kok                        ; second copy of it
.kel:
    call vp_kelig                   ; A KEEPER THIS PLAY CAN DO WITHOUT
    jc .kreq                        ; (98.3.19): claimed, and given back if
    push ax                         ; the ring cannot reach the header's
    call vp_mclhi                   ; slots beside it
    pop ax
    jc .knone
    call vp_kroom
    jnc .kok
    call vp_mfree
.knone:
    call vp_cptry                   ; ...unless moving OURSELVES too would
    jnc .posted                     ; make room for it (98.3.19.1)
    mov byte [vp_nokeep], 1         ; ...and else the play goes on without
    xor dx, dx                      ; it: a swap out of the full screen
    jmp short .kok                  ; stops at the key at or before
.posted:
    call vp_sfree                   ; POSTED: nothing held while the pass
    mov word [vp_msg], vp_s_room    ; runs, and the play starts again on the
    call vp_fmt                     ; wake (vp_onwake)
    call vp_repaint
    mov byte [vp_cppost], 1
    stc
    jmp .out
.kreq:
    call vp_mclhi                   ; FROM THE TOP, streamed or RESIDENT
    jnc .kok                        ; (98.1.7.4, 98.3): pinned for a bracket
                                    ; (98.3.19.2), and claimed from the bottom
                                    ; it landed on the kernel's caches and
                                    ; walled them off from the ring - 75 KB of
                                    ; Mode X keeper left the ring the 313 KB
                                    ; above it, not the 381 KB the machine had
    jmp .memf
.kok:
    mov [vp_keep], dx
    cmp byte [vp_own], 0            ; (AGAINST ITS OWN PAGE: no copy of the
    jne .nopv                       ; last record - VIDEO-SEEN-PLAN 3b)
    cmp byte [vp_flip], 1           ; PAGE FLIPPING: the last record's copy
    je .pvc                         ; (98.3.8; page-ahead keeps none, but
    cmp byte [vp_fpg], 0            ; flipped and built ahead does)
    je .nopv
.pvc:
    mov ax, VP_PREVKB               ; (from the top too, for the same
    call vp_mclhi                   ; reason)
    jnc .pv
.pvf:
    jmp .memf
.pv:
    mov [vp_prevseg], dx
    mov dx, [vp_keep]               ; (the KEEPER is what is zeroed and is
.nopv:                              ; the shadow: the copy claimed after it is
    mov [vp_shseg], dx              ; 31 KB, and zeroing it at the keeper's
    or dx, dx                       ; (NO KEEPER: nothing to zero - and ES 0
    jz .nkz                         ; is the vector table)
    mov es, dx                      ; size ran 45 KB past it into the heap
    call vp_zero                    ; and left the keeper as the claim found
                                    ; it - onto both pages, 98.3.8)
.nkz:
    cmp byte [vp_flip], 0           ; ON TWO PAGES OR MORE, the text's
    je .ns1                         ; second save (98.3.13.2) - from the
    mov ax, (VO_SAV + 1023) / 1024  ; top, as the copy is
    call vp_mclhi
    jc .pvf
    mov [vo_s1seg], dx
.ns1:
    ; --- RESIDENT (98.1.7): no ring - the block, loaded and kept, and a
    ;     key's record read into a claim of its own if the play starts at one
    cmp byte [vp_resid], 0
    je .strm
    call vp_rload
    jnc .rk
    jmp .fail
.rk:
    xor dx, dx                      ; (no key: no claim, [vp_ring] 0)
    mov ax, [vp_sel]
    or ax, ax
    jnz .rkc
    cmp byte [vp_pixfmt], PF_VGA8
    jb .tor
.rkc:
    cmp ax, [vp_kload]
    jne .tor
    mov ax, [vp_kbkb]
    call vp_mclaim
    jnc .tor
    jmp .memf
.tor:                               ; (.ring, where the tests above reach
    jmp .ring                       ; it with a short jump)
.strm:
    call vp_lyclaim                 ; THE LAYER's slots first (98.1.9)
    ; --- the ring: K slots and the mirror (SPEC.md 98.3). IN THE BRACKET,
    ;     as many as the machine has, 2..[vp_kmax]: every slot past the
    ;     header's ring is headroom the encode never counted on. LIVE plays
    ;     on the desktop, so it keeps the old rule, a power of two to VP_KMAX
    call vp_mavail                  ; AX = the largest free run, KB
%ifdef VP_DIAG
    mov [vp_mrun], ax               ; (the card's memory lines, and what
    mov dl, [vp_kkb]                ; was claimed before it: a stop
    cmp byte [vp_nokeep], 0         ; clears both)
    je .mk0
    xor dl, dl
.mk0:
    mov [vp_mkeep], dl
    xor dx, dx
    cmp word [vp_aseg], 0
    je .ms0
    push ax
    call vp_ringkb
    mov dl, al
    pop ax
.ms0:
    mov [vp_msnd], dl
%endif
    mov cx, [vp_kmax]
    cmp byte [vp_livem], 0
    jne .klive
    call vp_kres                    ; ...less what is claimed after it
.kbig:
    call .kslots
    cmp cx, ax
    jbe .kfit
    mov cx, ax                      ; (AX is -1 with no room: CX stays)
    jmp short .kfit
.klive:
    cmp cx, VP_KMAX
    jbe .kl0
    mov cx, VP_KMAX
.kl0:
    call .kslots
.k:
    cmp cx, ax
    jbe .kfit
    shr cx, 1
    jmp short .k
.kslots:
    mov dx, cx
    mov cl, 5
    shr ax, cl                      ; ...in 32 KB slots
    sub ax, [vp_msl]                ; less the mirror
    mov cx, dx
    ret
.kfit:
    mov ax, [vp_msl]                ; two slots, or three for BIGSP: a super-
    inc ax                          ; packet's chunks are all read before it
    cmp cx, ax                      ; is entered (98.1.4.1)
    jae .kok2
    jmp .memf
.kok2:
    mov [vp_kr], cx                 ; (the ring's slots, for vp_slot)
    call vp_einq                    ; IN PLACE (98.3.18.7): the least ring for
    jnc .kin                        ; a key's read, and the board's slots as
    call vp_xbipq                   ; the stream's - or the XMS bank's, by
    jnc .kin                        ; LOADALL (98.3.18.13) - or the hold's,
    call vp_xipq                    ; decoded in place by LOADALL
    jc .knp                         ; (98.3.18.11)
.kin:
    mov cx, [vp_msl]
    inc cx
.knp:
    call vp_bkind                   ; (what the card says after it)
    mov [vp_k], cx
    mov byte [vp_rshort], 0         ; A RING SHORT OF THE STREAM'S (98.1.1):
    push cx
    mov cx, [vp_kr]
    cmp cl, [vp_rneed]              ; it plays, and a burst may pause it -
    pop cx
    jae .rok                        ; which the full screen says once
    mov byte [vp_rshort], 1
.rok:
    call vp_bshort                  ; ...and a BANK short of the file's ask
    mov ax, cx
    add ax, [vp_msl]
    call vp_xmsbk                   ; (+ the bank's bounce, vp_bbnc)
    jz .rkb
    cmp byte [vp_livem], 0
    jne .rkb
    inc ax
.rkb:
    mov cl, 5
    shl ax, cl                      ; (K + the mirror) x 32 KB
    push bx
    call vp_xgpad                   ; ...+ THE GAP's slack (os88xgap.inc)
    mov byte [vp_lymx], 0           ; ...+ THE LAYER'S MIRROR, where it is
    cmp byte [vp_lyfl], 0           ; carved from this claim's top (98.1.9.10)
    je .rmx                         ; - slack too, so the ring is never a
    cmp byte [vp_einp], 0           ; slot short for it: refused, the carve
    jne .rmx                        ; takes it a whole slot. In place the
    cmp byte [vp_xip], 0            ; ring is not one, and the layer's own
    jne .rmx                        ; claim comes after it
    add ax, [vp_lymkb]
    inc byte [vp_lymx]
.rmx:
    call vp_mclaim
    jnc .rgot
    sub ax, bx                      ; no room for the slack: without it
    xor bx, bx
    cmp byte [vp_lymx], 0
    je .rm0
    sub ax, [vp_lymkb]
    dec byte [vp_lymx]
.rm0:
    call vp_mclaim
    jnc .rgot
    pop bx
    jmp .memf
.rgot:
    mov [vp_rclm], dx               ; (what is freed, and what moves)
    or bx, bx
    jz .rpl
    mov ax, [vp_xbase]              ; the ring's start that far from the
    call vp_xgal                    ; hold's, mod 2G: every slot and chunk
.rpl:                               ; after it 32 KB on, so every copy is
    pop bx
.ring:
    call vp_lycarve                 ; ...or THE FLIP: the ring's top (98.1.9.4)
    call vp_lyopen                  ; THE LAYER's slots, if it plays one
    call vp_spos                    ; where it starts, the reader, the hook
    jnc .clk0                       ; (vp_spos, which a seek runs too)
    jmp .fail
.clk0:
%ifdef VP_DIAG
    call vp_prgo                    ; (THE PLAY'S PROFILE: the clock run)
%endif
    ; --- the clock: the file's own period, or - with the card - half of it,
    ;     the hook then reading the card's position twice a frame
    mov dx, [vp_pitdiv]
    mov al, [vp_pitper0]
    cmp byte [vp_snd], 0
    je .clk
    cmp dx, 2 * FSX_RATE_MIN
    jb .clk
    cmp al, 127
    ja .clk
    shr dx, 1
    shl al, 1
.clk:
    cmp byte [vp_snd], VP_SPK       ; PULSES A SAMPLE (34.11.7): the period
    jne .clk2                       ; a whole number of SAMPLES, so the
    cmp byte [os88spk_pp], 1        ; door's K is a multiple of the pulses
    je .clk2                        ; and the kernel's entry is a sample's
    push ax                         ; first - within a sample of the
    push bx                         ; file's, as the door's own rounding
    mov al, [cs:os88spk_n]          ; is. THE PULSES PLAYED, not the file's:
    mul byte [os88spk_pp]           ; one on a Covox (98.3.15.1). AX = N
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div bx
    mul bx
    mov dx, ax
    pop bx
    pop ax
.clk2:
    mov [vp_pitper], al
    mov [vp_pdiv], dx
    mov al, 1
    mov [vp_sess], al
    mov [vp_sfirst], al
    clc
    jmp short .out
.memf:
    mov word [vp_msg], vp_s_mem
.fail:
    call vp_sfree
    call vp_fmt
    call vp_repaint
    cmp byte [vp_lcard], 0          ; A PLAY REFUSED AT THE START: the card
    jne .fc                         ; comes out, as for one that fails on
    mov al, 1                       ; its way (vp_sstop) - it is what says
    mov [vp_card], al               ; why, and the owner found the reason
    mov [vp_relay], al
    push bx                         ; hidden behind a closed card
    call vp_wakew
    pop bx
.fc:
    stc
.out:
    pushf                           ; ONE POST A PRESS (98.3.19.1): the
    xor ax, ax                      ; play the wake starts again may not
    cmp [vp_cppost], al             ; post a second time, and anything else
    jne .cpk                        ; ends the press
    mov [vp_cpq], al
.cpk:
    mov [vp_cppost], al
    popf
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; vp_kres - AX = the largest free run, KB -> AX = what the ring may take of
; it (SPEC.md 98.3): less the SOUND's ring where the file has sound and this
; play claimed none - MUTED: M claims it in the full screen after the ring
; is up (98.3.17), and a ring that took every slot refused it - and less
; the claim a seek reads its key's ENTRY into after the ring (98.3.14),
; which would otherwise quietly refuse: the entry, not the record, which is
; read into the ring (vp_spos) - and less THE GAP's slack (98.3.18.12), the
; 2G the ring's claim takes on top of its slots, which a ring sized to every
; slot the run holds left no room for: at G = 2048 the 4 KB rarely fit what
; the rounding left, the claim fell back to an unplaced ring (vp_sstart's
; `.rmx` retry), and the Victory's copies ran at their slow gap. 0 at the
; least. Preserves all but AX, flags
vp_kres:
    cmp byte [vp_audio], 0
    je .a
    cmp word [vp_aseg], 0
    jne .a
    push bx
    mov bx, ax
    call vp_ringkb
    sub bx, ax
    xchg ax, bx
    pop bx
    jc .z
.a:
    sub ax, [vp_kekb]
    jc .z
    call vp_xmsbk                   ; ...and THE BANK'S BOUNCE, a slot after
    jz .r                           ; the mirror (vp_bbnc) - XMS's: EMS reads
                                    ; into its frame
    sub ax, VP_CHUNK / 1024
    jc .z
.r:
    push bx                         ; THE GAP's slack: BX = its KB, 0 where
    push ax                         ; the session will not place
    xor ax, ax
    call vp_xgpad
    pop ax
    sub ax, bx
    pop bx
    jnc .rr
.z:
    xor ax, ax
.rr:
    ret

; vp_xmsbk - ZF=0: the session's bank is XMS's - [vp_xbank], and not EMS's
; ([vp_xems] is 0 or 1), the one with a bounce slot (vp_bbnc). Preserves all
; but flags
vp_xmsbk:
    cmp byte [vp_xbank], 0
    je .r
    cmp byte [vp_xems], 1
.r:
    ret

; vp_lyclaim - a stream session's start, BEFORE its ring is sized: is there
; a LAYER to place (98.1.9)? [vp_lyfl] 1 and [vp_lyns] its most slots if the
; file has one this play can read - and NOTHING CLAIMED: its slots are the
; ring claim's top (vp_lycarve, 98.1.9.10), so the ring is sized exactly as
; a play with no layer's and a NO gives every slot back to it. (They were a
; claim of their own, taken first: a no left them idle for the play, and
; the claim walled the heap's caches off from the ring.) Every register kept
vp_lyclaim:
    push cx
    mov byte [vp_lyfl], 0
    cmp word [vp_lyseg], 0
    jne .out                        ; (held already)
    call vp_lywant                  ; CX = its most slots, 0 none
    jcxz .out
    mov [vp_lyns], cx
    xor cx, cx
    mov [vp_lysht], cx
    mov [vp_lybnc], cl              ; THE LAYER BANK'S BOUNCE (98.1.9.4): with
    cmp word [vp_lyxn], 0           ; a bank now, or one the SPLIT may grant -
    jne .bq                         ; the base's took the whole pool at the
    cmp byte [vp_lysplt], 0         ; open (--bank auto), and the layer's is
    jne .set                        ; taken at the split, after this claim
    cmp word [vp_lybkb], 0
    je .set
    call vp_xmsbk
    jz .set
.bq:
    mov byte [vp_lybnc], 1
.set:
    inc byte [vp_lyfl]              ; A LAYER TO PLACE: the ring's top (0
                                    ; since the entry: 1)
.out:
    pop cx
    ret

; vp_lywant - CX = the most slots the layer's read-ahead would take,
; VP_LYMAX - or 0, no layer this play can read: none in the file, flipped
; every frame (PAGE-AHEAD's is fine: its frames are drawn on the page shown,
; and the layer after them, 98.3.8.3), repeating (the seam would want the
; layer's place again), Live, or clusters past 32 KB. Preserves all but CX
vp_lywant:
    xor cx, cx
    push ax
    mov ax, [vp_lysp]
    or ax, [vp_lysp+2]
    jz .o
    cmp byte [vp_flip], 1
    je .o
    mov al, [vp_rep]
    or al, [vp_livem]
    jnz .o
    cmp word [vp_clb], VP_CHUNK
    ja .o
    mov cx, VP_LYMAX                ; THE PLAYER's choice, not the encode's:
.o:                                 ; the most it would take
    pop ax
    ret

; vp_kwant - AX = the slots this play wants: the header's ring (98.1.1),
; and never fewer than vp_sstart's .kfit takes - 2, or 3 for BIGSP (98.1.4.1),
; whose header may well say 2. Preserves all but AX
vp_kwant:
    mov ax, [vp_msl]
    inc ax
    cmp al, [vp_rneed]
    jae .r
    mov al, [vp_rneed]
.r:
    ret

; vp_kplain - ZF=1: the keeper would hold the canvas's image and nothing
; else - no decode INTO it, no RESIDENT, Live or flipped play, no shadow in
; either surface (vp_kelig's and vp_pcanv's common ground). Preserves all
; but flags
vp_kplain:
    push ax
    mov al, [vp_kneed]              ; (the decoder writes INTO it)
    or al, [vp_resid]
    or al, [vp_livem]
    or al, [vp_flip]
    or al, [vp_fsshd]
    pop ax                          ; (POP: the OR's flags stand)
    ret

; vp_kelig - CF=0 this play may go WITHOUT its keeper (SPEC.md 98.3.19):
; the keeper is only the canvas's image here - not a shadow the decoder
; writes, not a Live or RESIDENT play's, no second page to flip to - and the
; play is in the FULL SCREEN, where nothing but a swap reads it back. In the
; window the keeper is how the canvas crosses every bracket. Preserves all
vp_kelig:
    call vp_kplain
    jnz .no
    cmp byte [vp_startp], 0         ; F: the full screen, paused
    jne .yes
    call vp_track
    call vp_canwin                  ; Play: the full screen where the window
    cmc                             ; cannot host it
    ret
.yes:
    clc
    ret
.no:
    stc
    ret

; vp_pcanv - CF=0 the poster can stand in for the keeper (SPEC.md
; 98.3.19.3): it is at the video's own size - one-bit dense rows (vp_linear)
; or VGA4 packed two pixels a byte (vp_v4pack), the canvas exactly - and the
; play draws onto the screen in the file's own layout: no decode INTO a
; keeper, no RESIDENT, LIVE or flipped play, no shadow in either surface.
; Preserves all
vp_pcanv:
    cmp word [vp_pseg], 0
    je .no
    cmp byte [vp_pthm], 0           ; (a stored picture's claim and rows are
    jne .no                         ; not the canvas's: 98.1.3.11)
    cmp word [vp_ps], 1
    jne .no
    cmp word [vp_pscale], 1
    jne .no
    call vp_kplain
    jnz .no
    push ax
    cmp byte [vp_pixfmt], PF_VGA4   ; 16 colours, on mode 12h's planes -
    jne .p1                         ; but not in ITS OWN sixteen (98.1.3.2.2):
    mov ax, [vp_palo]               ; the poster is in the desktop's, and the
    or ax, [vp_palo+2]              ; screen's pixel values copied into it
    jmp short .z                    ; would show the stopped picture in the
                                    ; wrong colours (the options audit)
.p1:
    cmp byte [vp_pixfmt], PF_VGA8   ; ...or one bit, in the desktop's own
    jae .nop                        ; layout - another's is the window's
    mov al, [vp_layout]             ; shadow (98.3.2), which the keeper IS
    cmp al, [vp_dlay]
.z:
    pop ax                          ; (ZF=1: it can)
    jne .no
    clc
    ret
.nop:
    pop ax
.no:
    stc
    ret

; vp_pcput / vp_pcget - the canvas between the screen and the POSTER, where
; it stands in for the keeper (98.3.19.3): its rows in the file's layout at
; [vp_org] on the screen, dense at [vp_pbw] in [vp_pseg]. A put with no frame
; of this session in the poster yet ([vp_pcv] 0) is black, as the keeper's
; would have been; a get sets [vp_pcv]. Clobbers AX, BX, CX, DX, SI, DI, ES
vp_pcput:
    cmp byte [vp_pcv], 0
    je .blk
    cmp word [vp_pseg], 0
    jne .go
.blk:
    mov byte [vp_pcv], 0            ; (vp_kmove's own black - and with no
    xor al, al                      ; frame, it does not come back here)
    jmp vp_kmove
.go:
    xor dx, dx
.r:
    cmp dx, [vp_h]
    jae .done
    call vp_pcrow
    mov es, [vp_vseg]
    mov cx, [vp_wb]
    push ds
    cmp byte [vp_planar], 0
    jne .v4
    mov ds, [vp_pseg]
    rep movsb                       ; ONE BIT: the row as it is
    pop ds
    inc dx
    jmp short .r
.v4:
    push bp
    push dx
    mov bp, cx
    mov ds, [vp_pseg]
    pushf
    cli                             ; (the Map Mask is the hook's too)
.v4b:                               ; 16 COLOURS: 4 packed bytes -> 8 pixels'
%rep 4                              ; bit in each of the four planes, b3 of
    lodsb                           ; a pixel's nibble plane 3's
%rep 2
    shl al, 1
    rcl ch, 1
    shl al, 1
    rcl cl, 1
    shl al, 1
    rcl bh, 1
    shl al, 1
    rcl bl, 1
%endrep
%endrep
    mov dx, 0x3C4
    mov ax, 0x0102
    out dx, ax
    mov [es:di], bl
    mov ah, 2
    out dx, ax
    mov [es:di], bh
    mov ah, 4
    out dx, ax
    mov [es:di], cl
    mov ah, 8
    out dx, ax
    mov [es:di], ch
    inc di
    dec bp
    jnz .v4b
    popf
    pop dx
    pop bp
    pop ds
    inc dx
    jmp .r
.done:
    cmp byte [vp_planar], 0
    je .ret
    call vp_mxall                   ; the writes back to all four planes
.ret:
    ret

vp_pcget:
    cmp word [vp_pseg], 0           ; A POSTER THAT CANNOT HOLD THE CANVAS -
    je .lost                        ; gone, or at another scale since - and the
    cmp word [vp_pscale], 1         ; session is a keeperless one (98.3.19):
    je .ok                          ; it stops at the key on the way back
.lost:
    mov byte [vp_nokeep], 1
    mov byte [vp_pcv], 0
    ret
.ok:
    xor dx, dx
.r:
    cmp dx, [vp_h]
    jae .done
    call vp_pcrow
    xchg si, di                     ; SI the screen, DI the poster
    mov es, [vp_pseg]
    mov cx, [vp_wb]
    push ds
    cmp byte [vp_planar], 0
    jne .v4
    mov ds, [vp_vseg]
    rep movsb
    pop ds
    inc dx
    jmp short .r
.v4:
    push bp
    push dx
    mov bp, cx
    mov ds, [vp_vseg]
    pushf
    cli
.v4b:                               ; four planes' byte -> 4 packed bytes
    mov dx, 0x3CE
    mov ax, 0x0004
    out dx, ax
    mov bl, [si]
    mov ah, 1
    out dx, ax
    mov bh, [si]
    mov ah, 2
    out dx, ax
    mov cl, [si]
    mov ah, 3
    out dx, ax
    mov ch, [si]
%rep 4
%rep 2
    shl ch, 1
    rcl al, 1
    shl cl, 1
    rcl al, 1
    shl bh, 1
    rcl al, 1
    shl bl, 1
    rcl al, 1
%endrep
    stosb
%endrep
    inc si
    dec bp
    jnz .v4b
    mov ax, 0x0004                  ; the Read Map back to plane 0, where the
    out dx, ax                      ; desktop keeps it
    popf
    pop dx
    pop bp
    pop ds
    inc dx
    jmp .r
.done:
    mov byte [vp_pcv], 1
.ret:
    ret

; vp_pcrow - DX = a canvas row: DI = it on the screen, SI = it in the
; poster. Preserves all but AX, BX, SI, DI
vp_pcrow:
    mov ax, dx
    mov bl, [vp_layout]
    call vp_rowaddr
    add ax, [vp_org]
    add ax, [vp_kpo]
    mov di, ax
    push dx
    mov ax, dx
    mul word [vp_pbw]
    mov si, ax
    pop dx
    ret

; vp_kroom - DX = the keeper, just claimed: CF=0 the ring can still reach
; the header's slots beside it, and the mirror. Preserves all
vp_kroom:
    push ax
    push bx
    push cx
    call vp_mavail
    call vp_kres
    mov cl, 5
    shr ax, cl                      ; ...in 32 KB slots
    mov bx, ax
    call vp_kwant
    add ax, [vp_msl]                ; (and the mirror)
    cmp bx, ax                      ; CF = short
    pop cx
    pop bx
    pop ax
    ret

; vp_cptry - the keeper and the ring will not both fit: CF=0 a compaction
; is POSTED that will make room for both if our own region moves too
; (SPEC.md 98.3.19.1, 66.4.3), and the caller frees what it holds and
; RETURNS - the play starts again on the wake. CF=1 no: not from Play or F
; ([vp_cpent]), asked once already for this press ([vp_cpq]), or the
; what-if says even that would not do. Preserves all
vp_cptry:
    push ax
    push bx
    push cx
    cmp byte [vp_cpent], 0
    je .no
    cmp byte [vp_cpq], 0
    jne .no
    call vp_kwant                   ; the ring and its mirror...
    add ax, [vp_msl]
    mov cl, 5
    shl ax, cl
    mov bl, [vp_kkb]                ; ...and the keeper
    xor bh, bh
    add ax, bx
    add ax, [vp_kekb]               ; ...and what vp_kres keeps back
    cmp byte [vp_audio], 0
    je .w
    cmp word [vp_aseg], 0
    jne .w
    push bx
    mov bx, ax
    call vp_ringkb
    add ax, bx
    pop bx
.w:
    mov cx, ax                      ; (CX: the what-if answers in AX AND BX)
    mov ax, MEM_LVL_TOP             ; AH = MEMC_WHATIF, AL = the level
    call OSAPI_MEM_COMPACT          ; AX = the largest run if we moved too:
    cmp ax, cx                      ; a MEASUREMENT, never a promise
    jb .no
    mov bx, [vp_win]
    mov ax, (MEMC_POST << 8) | MEM_LVL_TOP  ; an ordinary claim's rank: every
    call OSAPI_MEM_COMPACT          ; cache counts as free (SPEC.md 50.6.4)
    jc .no
    mov byte [vp_cpq], 1
    mov al, [vp_cpent]
    mov [vp_cpgo], al
    clc
    jmp short .p
.no:
    stc
.p:
    pop cx
    pop bx
    pop ax
    ret

; vp_smov - AX = vp_smove, or 0: the SESSION's claims - the ring, the keeper
; and the page copy - movable between brackets, or pinned for one (SPEC.md
; 98.3.19.2). A bracket's hook reads them at interrupt time, which no
; relocation can reach (66.3); on the desktop nothing does. A LIVE session
; keeps them pinned: its worker decodes out of them on the desktop.
; Preserves all
vp_smov:
    push dx
    cmp byte [vp_lsess], 0
    je .go
    xor ax, ax
.go:
    mov dx, [vp_rclm]
    call vp_mmovz
    mov dx, [vp_keep]
    call vp_mmovz
    mov dx, [vp_prevseg]
    call vp_mmovz
    mov dx, [vp_pevseg]             ; (the hook reads the palettes a scene)
    call vp_mmovz
    mov dx, [vo_s1seg]              ; (and the text's second save)
    call vp_mmovz
    pop dx
    ret

; vp_mmovz - DX = a claim, or 0 for none: OSAPI_MEM_MOVABLE on it with AX
; as it is (the proc, or 0 to pin), and AX kept - vp_smov's and vp_rmov's
vp_mmovz:
    or dx, dx
    jz .n
    push ax
    call vp_mmov
    pop ax
.n:
    ret

; vp_smove - THE SESSION'S AND THE POSTER'S RELOCATION PROC (SPEC.md 66.3,
; 98.3.19.2): BX = the old base, DX = the new. Every word naming one of
; those claims holds its BASE - every other segment is derived at its use -
; so a word equal to the old base is the one to move. Clobbers AX, SI
vp_smove:
    cmp [vp_rclm], bx               ; THE RING may start inside its claim
    jne .s                          ; (THE GAP, os88xgap.inc): it moves by
    mov ax, [vp_ring]               ; what the claim does
    sub ax, bx
    add ax, dx
    mov [vp_ring], ax
    cmp byte [vp_lyb], 0            ; A FLIP's slots are in the ring's claim
    je .s                           ; (98.1.9.4): derived from its base, and
    mov ax, [vp_lyseg]              ; moved with it
    sub ax, bx
    add ax, dx
    mov [vp_lyseg], ax
.s:
    mov si, vp_smtab
.w:
    lodsw
    or ax, ax
    jz .out
    xchg ax, si
    cmp [si], bx
    jne .n
    mov [si], dx
.n:
    xchg ax, si
    jmp short .w
.out:
    ret

vp_smtab:     dw vp_rclm, vp_keep, vp_prevseg, vp_shseg, vp_pseg, vp_kshd
              dw vp_pevseg, vp_p8seg, vo_s1seg, 0

; -----------------------------------------------------------------------------
; vp_spos - DX = the ring (or, resident, the key's claim, or 0): the session
; at [vp_sel] - the key's record read into it, the reader from its
; super-packet, the hook's state and the clock's count. vp_sstart's, and a
; seek's in the full screen (98.3.14). CF=1 the key could not be read,
; [vp_msg] says so. Clobbers AX, BX, CX, SI, DI, ES
; -----------------------------------------------------------------------------
vp_spos:
    mov [vp_ring], dx
    ; --- WHERE IT STARTS (98.3.5): the file's first super-packet, or the
    ;     picked keyframe's - its record read into the ring, to be decoded
    ;     once the surface is up and before the ring is filled over it
    xor ax, ax
    mov [vp_base], ax
    mov [vp_kidx], ax
    mov [vp_lpar], ax               ; (a lead's paragraphs past the ring)
    mov [vp_alst], ax               ; no lead read (98.1.8), none staged,
    mov [va_pc], ax                 ; and the audio cursor before the ring
    mov [vp_afr], ax                ; (vp_lfloor reads it from the first fill,
    dec ax                          ; and its frame until vp_sopen sets it)
    mov [vp_krec], ax
    mov [vp_lsrc], ax
    mov byte [vp_aref], 0x80
    mov ax, [vp_sp0]
    mov [vp_ssp], ax
    mov ax, [vp_sp0+2]
    mov [vp_ssp+2], ax
    mov ax, [vp_sp0n]
    mov [vp_ssec], ax
    mov ax, [vp_sel]
    or ax, ax
    jnz .kpick
    cmp byte [vp_flip], 2           ; ...and for page-ahead, whose key 0
    je .kpick                       ; carries the hidden page PRELOADED
    cmp byte [vp_pixfmt], PF_VGA8   ; key 0 too for colour: its frame 0 may
    jb .tos                         ; be too big for one record, and the key
.kpick:                             ; is the whole picture
    cmp ax, [vp_kload]
    jne .tos
    mov [vp_rdseg], dx
    call vp_kfirst                  ; (its first part, 98.1.3.8: the rest
    jc .kbad                        ; are read as it is decoded)
    cmp byte [vp_kneed], 0          ; the key decodes into a keeper that is
    je .kok                         ; only the canvas (98.1.7.3), so its
    mov dx, [vp_rdseg]              ; writes are checked as the block's were
    call vp_rbnd
    jc .kbad
.kok:
    mov [vp_krec], si
    cmp byte [vp_ahead], 0          ; SOUND AHEAD: the key's lead is its
    je .knl                         ; entry's last A x abytes bytes (98.1.8)
    mov ax, [vp_klead]
    or ax, [vp_klead+2]
    jnz .kapart
    mov ax, si
    add ax, [vp_ke+KE_LEN]
    sub ax, [vp_alb]
    mov [vp_lsrc], ax
    jmp short .knl
.kbad:                              ; (the key's reads' one refusal)
    mov word [vp_msg], vp_s_kbad
    stc
    ret
.tos:                               ; (.start, where the tests above reach
    jmp short .start                ; it with a short jump)
.kapart:                            ; ...or, APART (98.1.8.1), its own read
    cmp byte [vp_snd], 0            ; into the ring past the key's - none for
    je .knl                         ; a muted play, as the start's
    push word [vp_rdseg]
    mov ax, [vp_kbkb]               ; (whole KB: a paragraph count)
    mov cl, 6
    shl ax, cl
    mov [vp_lpar], ax
    add [vp_rdseg], ax
    mov ax, [vp_kload]              ; DX:AX = the table + key x the lead
    mul word [vp_alb]
    add ax, [vp_klead]
    adc dx, [vp_klead+2]
    mov cx, [vp_alb]
    call vp_rdat
    pop word [vp_rdseg]
    jc .kbad
    mov [vp_lsrc], si
.knl:
    mov ax, [vp_ke+KE_K]
    inc ax
    mov [vp_base], ax               ; the first frame the stream draws
    mov ax, [vp_ke+KE_SP]
    mov [vp_ssp], ax
    mov ax, [vp_ke+KE_SP+2]
    mov [vp_ssp+2], ax
    mov al, [vp_ke+KE_SECS]
    xor ah, ah
    mov [vp_ssec], ax
    mov al, [vp_ke+KE_IDX]          ; ...and the records before it there
    mov [vp_kidx], ax
.start:
    cmp word [vp_krec], 0xFFFF      ; FROM THE STREAM'S START, SOUND AHEAD:
    jne .nl0                        ; the start's lead read into the ring,
    cmp byte [vp_ahead], 0          ; frames 0 .. A-1 (98.1.8)
    je .nl0
    cmp byte [vp_snd], 0            ; (a muted play takes none)
    je .nl0
    mov ax, [vp_ring]
    mov [vp_rdseg], ax
    mov ax, [vp_lead0]
    mov dx, [vp_lead0+2]
    mov cx, [vp_alb]
    call vp_rdat
    jc .kbad
    mov [vp_lsrc], si
.nl0:
    ; --- the reader: from the cluster boundary under the stream's start
    call vp_bseek                   ; ...or from INSIDE THE BANK, if it holds
    jnc .inbank                     ; the place (98.3.18.4)
    call vp_bflush                  ; (a bank holds the old place's chunks)
    mov ax, [vp_clb]
    dec ax                          ; a cluster's mask
    mov bx, [vp_ssp]
    and bx, ax                      ; BX = how far the stream is into it
    push ds
    pop es
    mov di, vp_cur
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    mov ax, [vp_ssp]
    sub ax, bx
    mov [vp_cur+FSEQ_OFF], ax
    mov ax, [vp_ssp+2]
    mov [vp_cur+FSEQ_OFF+2], ax
.inbank:
    ; --- the hook's state: before the first super-packet
    xor ax, ax
    mov [vp_lc], ax
    call vp_lyunc                   ; (a LATE no, the ring past its slots
                                    ; then: chunk 0 again, so they are its
                                    ; now - 98.1.9.10)
    mov [vp_pc], ax
    mov [vp_po], bx
    mov [vp_fleft], ax
    mov [vp_owed], ax
    mov [vp_stall], ax
    mov [vp_pause], ax              ; (this play's, as the stalls are: it
    mov [vp_ptk], ax                ; counted every play since the open)
%ifdef VP_DIAG
    mov [vp_pstrm], ax              ; THE READER'S DIAGNOSTIC (98.3): pauses
    mov [vp_pmax], ax               ; with the sound waiting on the stream,
    mov [vp_pmaxf], ax              ; the longest and where, and the least
    mov [vp_lminf], ax              ; lead the reader had over the picture
    mov [vp_astv], al
    mov [vp_pin], al
    mov word [vp_lmin], 0xFFFF
    mov [os88spk_dryn], ax          ; (the speaker's, 34.11: this play's)
    mov [os88spk_dryn+2], ax
    mov [os88spk_dryg], ax
%endif
    mov [vp_late], ax
    mov [vp_dt], ax
    mov [vp_gap], ax
    mov [vp_ready], al
    mov [vp_end], al
    mov [vp_eof], al
    mov [vp_err], al
    mov [vp_held], al
    mov [vp_upause], al
    mov [vp_sdefer], al
    mov [vp_autop], al
    mov [vp_stopq], al
    mov [vp_dtok], al
    mov [vp_cskip], ax              ; (the cursor skips nothing and has taken
    mov [vp_pgen], ax               ; no seam; none is armed - 98.3.9)
    mov [vp_wgen], ax
    mov ax, [vp_base]
    mov [vp_done], ax               ; frames before it count as drawn
    mov [vp_vseq], ax               ; ...and the clock's count starts there
    or ax, ax                       ; ...and the sixteen the screen it starts
    jz .pa0                         ; from is shown in (98.1.3.2.3): the
    dec ax                          ; key's, or frame 0's
.pa0:
    call vp_palat
    call vp_lypos                   ; ...and THE LAYER's place (98.1.9)
    mov ax, [vp_ssec]
    mov [vp_psec], ax               ; the first super-packet, not yet entered
    cmp byte [vp_resid], 0          ; RESIDENT: the cursor on the block
    je .out
    call vp_rcur
.out:
    clc
    ret

; =============================================================================
; THE LAYER (SPEC.md 98.1.9): a second stream after the base's, encoded on
; top of it for a machine with disk and CPU to spare. It is read into slots
; of its own only when the base's ring - and its bank - are full, which is
; the disk's spare time and needs no measurement of it; and it is decoded
; after the base's record for the same frame, unless the hook is behind -
; a layer record is values, so one left out leaves the base's bytes, and
; the next carries on. A machine that cannot keep it up plays the base.
; Every layer super-packet is a whole 32 KB slot on a 32 KB boundary, so a
; read is one READ_SEQ of a slot, at a cluster multiple on any volume whose
; clusters are 32 KB or less
; =============================================================================

; vp_lyparse - ES = the header: [vp_lysp] - 0 for no layer, or a layer this
; player will not read (its words wrong, a stream that is not a plain one) -
; and the layer's words with it (vp_lywtab). The base plays whatever the
; layer says. Clobbers AX
vp_lyparse:
    push bx
    push cx
    push si
    xor ax, ax
    mov [vp_lyord], al
    mov si, vp_lywtab               ; the first VP_LYW0 words: 0 for none
    mov cx, VP_LYW0
.z:
    mov bx, [si+2]
    mov [bx], ax
    add si, 4
    loop .z
    mov ax, [es:V88_LAYER]
    test ax, VP_CHUNK - 1           ; (a 32 KB boundary)
    jnz .no
    or ax, [es:V88_LAYER+2]
    jz .no
    cmp word [es:V88_LAYER+4], VP_LCONT
    jne .no                         ; (64: the padded kind, the base plays)
    cmp word [es:V88_LAYER+16], VP_CHUNK - 8
    ja .no
    cmp word [es:V88_LAYER+16], 2
    jb .no
    mov si, vp_lywtab               ; ...and all of them, the header's
    mov cx, VP_LYW
    cld
.c:
    lodsw
    xchg ax, bx
    mov ax, [es:bx]
    xchg ax, bx
    lodsw
    xchg ax, bx
    mov [bx], ax
    loop .c
    mov al, [es:V88_LYORD]          ; ...and whether it was made to be read
    mov [vp_lyord], al              ; BEFORE the base's bank refills
    mov ax, [vp_lymax]              ; THE MIRROR past the last slot, whole
    add ax, 1023                    ; KB to hold the longest record's run
    mov cl, 10                      ; on out of it (98.1.9.8)
    shr ax, cl
    mov [vp_lymkb], ax
.no:
    pop si
    pop cx
    pop bx
    ret

; the layer's words (98.1.9): the header's offset and the player's word -
; first those a file with no layer leaves 0, then the key table's and the
; KB, which a file with no layer leaves as they were
vp_lywtab:
    dw V88_LAYER, vp_lysp           ; its first super-packet
    dw V88_LAYER + 2, vp_lysp + 2
    dw V88_LAYER + 18, vp_lybkb     ; its XMS bank, KB
    dw V88_LAYER + 10, vp_lypkb     ; ...and its prefill, KB (FFFFh: all)
    dw V88_LAYER + 20, vp_kytl      ; key 0's decode modelled on the layer's
    dw V88_LAYER + 22, vp_kytlb     ; machine and on the base's, 10 us
    dw V88_LYNEED, vp_lyneed        ; the bank's KB that carries the base
    dw V88_LYBKS, vp_lybks          ; the two machines' disks with nothing
    dw V88_LYIDL, vp_lykbs          ; decoding (98.1.9.6)
    dw V88_LAYER + 16, vp_lymax     ; its longest record (98.1.9.8)
VP_LYW0     equ ($ - vp_lywtab) / 4
    dw V88_LAYER + 12, vp_lykt      ; its key table
    dw V88_LAYER + 14, vp_lykt + 2
    dw V88_LAYER + 8, vp_lykb       ; its memory, KB (the encode's floor)
VP_LYW      equ ($ - vp_lywtab) / 4

; vp_lyopen - a stream session's start, after its ring: [vp_lyon] 1 if
; vp_lyclaim claimed the layer's slots. Nothing is refused for the layer's
; want: the base plays. Every register kept
vp_lyopen:
    push ax
    push cx
    push dx
    xor ax, ax
    mov [vp_lyon], al
    mov [vp_lyoff], al
    mov [vp_lywhy], al
    mov [vp_lygot], ax              ; (this play's counts, layer or none)
    mov [vp_lymis], ax
    mov [vp_lyskp], ax
    inc ax                          ; (AX = 0 above: 1)
    cmp word [vp_lyseg], 0          ; (a seek's re-entry keeps it on)
    je .room
    mov [vp_lyon], al
    jmp short .r
.room:
    cmp byte [vp_lyfl], 0           ; NO ROOM, and none the ring could lend
    je .r                           ; (98.1.9.4): the card says by how much -
    mov [vp_lyoff], al              ; or the DISK said no, kept for the file
    inc ax                          ; (98.1.9.3), so none was placed
    mov [vp_lywhy], al
    cmp byte [vp_lydv], 2
    jne .r
    dec byte [vp_lywhy]
.r:
    pop dx
    pop cx
    pop ax
    ret

; vp_lypos - the session's start, or a seek's: the layer's cursor at the
; super-packet holding frame [vp_base] - the first, or the picked key's
; next frame, out of the layer's key table - and its slots empty. The hook
; steps over the records before [vp_done] itself. Every register kept
vp_lypos:
    cmp byte [vp_lyon], 0
    je .r
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor ax, ax
    mov [vp_lyrd], ax
    mov [vp_lyuse], ax
    mov [vp_lyo], ax                ; (the first record at the chunk's start)
    mov [vp_lyeof], al
    mov [vp_lybeh], al
    mov [vp_lygot], ax
    mov [vp_lyskp], ax
    mov [vp_lymis], ax
    mov [vp_lyxh], ax               ; (the bank empty: what it held was the
    mov [vp_lyxc], ax               ; old place's)
    mov [vp_lyxq], al
    call vp_lyxqset                 ; THE BANK this session
    push ds
    pop es
    mov di, vp_lycur
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    mov ax, [vp_lysp]
    mov dx, [vp_lysp+2]
    cmp word [vp_base], 0
    je .at
    push word [vp_rdseg]            ; (the key's record is read there)
    mov ax, [vp_lyseg]              ; FROM A KEY: its entry, read into the
    mov [vp_rdseg], ax              ; layer's first slot before anything is
    mov ax, [vp_kload]              ; (the key the ring was positioned on)
    mov cx, 8
    mul cx
    add ax, [vp_lykt]
    adc dx, [vp_lykt+2]
    call vp_rdat                    ; [vp_rdseg]:SI = the entry
    pop word [vp_rdseg]
    jc .off
    push ds
    mov ds, [vp_lyseg]
    mov ax, [si]                    ; the key's next frame's record: its
    mov dx, [si+2]                  ; offset from the layer's start
    pop ds                          ; (98.1.9.8)
    mov cx, ax
    and cx, dx
    inc cx
    jz .end                         ; (FFFFFFFFh: the key is on the last frame)
    mov cx, ax
    and cx, VP_CHUNK - 1
    mov [vp_lyo], cx                ; ...into the chunk that holds it,
    and ax, 0x10000 - VP_CHUNK      ; read from the chunk's start
    add ax, [vp_lysp]
    adc dx, [vp_lysp+2]
.at:
    mov [vp_lycur+FSEQ_OFF], ax
    mov [vp_lycur+FSEQ_OFF+2], dx
    mov ax, [vp_base]               ; ...its frame the play's first
    mov [vp_lynx], ax
    jmp short .out
.end:
    mov byte [vp_lyeof], 1
    jmp short .out
.off:
    mov byte [vp_lyon], 0           ; (unreadable: the base alone)
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; vp_lyread - THE DISK IS FREE (the ring full, the bank full or none): the
; layer's next step - a slot fed from the layer's BANK if it holds any
; (a copy, no disk), else the disk read into an empty slot, or with every
; slot full into the bank through its bounce slot. CF=0 a step was taken;
; CF=1 nothing to do. A layer that cannot be read stops; the base plays
; on. Clobbers AX, BX, CX, DX, SI, DI, ES
vp_lyread:
    cmp byte [vp_lyon], 0
    je .none
    call vp_lydrain                 ; (the bank first: it is in order)
    jnc .r
    cmp byte [vp_lyeof], 0
    jne .none
    mov ax, [vp_lyrd]
    sub ax, [vp_lyuse]
    cmp ax, [vp_lyns]
    jae .bank                       ; every slot is the hook's
    cmp word [vp_lyxc], 0
    jne .none                       ; (the bank holds the next: drained)
    mov bx, [vp_lyns]
    dec bx
    and bx, [vp_lyrd]
    push bx
    call vp_lyslseg
    call vp_lyget                   ; the disk into slot BX
    pop bx
    jc .r
    or bx, bx                       ; (slot 0: its head mirrored past the
    jnz .pub                        ; last, 98.1.9.8)
    call vp_lymir
.pub:
    inc word [vp_lyrd]              ; published LAST: the hook reads it
    clc
.r:
    ret
.bank:
    cmp byte [vp_lyxq], 0
    je .none
    mov ax, [vp_lyxc]
    cmp ax, [vp_lyxn]
    jae .none                       ; the bank full too
    call vp_lybounce                ; THE BOUNCE: past the slots and their
    call vp_lyget                   ; mirror, then up to the bank's tail
    jc .r
    mov ax, [vp_lyxh]
    add ax, [vp_lyxc]
    cmp ax, [vp_lyxn]
    jb .t
    sub ax, [vp_lyxn]
.t:
    call vp_lybounce
    mov si, bx
    xor di, di                      ; UP
    call vp_lyxcp
    jc .r
    inc word [vp_lyxc]
    clc
    ret
.none:
    stc
    ret

; vp_lybench - a session's first frame, the key just decoded: THIS CPU
; TIMED AT THE PLAY'S OWN WORK - the same record decoded again for eight
; ticks from a tick's edge, the screen unchanged by it (a key's writes are
; the picture already there), and its time a decode against key 0's,
; modelled on the layer's machine and on the base's (98.1.9). Nearer the
; base's than the layer's - past the mean of the two, or an eighth past
; the layer's where they are one machine - and the layer is not read: what
; would be decoded on top of every frame would make them late. ~0.45 s,
; and only for a file with a layer and a yardstick. [vp_lytm] its time,
; [vp_lyoff] 1 when it said no. Clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_lybench:
    xor ax, ax
    mov [vp_lytm], ax
    mov [vp_ybq], al
    call vp_bnsel                   ; THE BENCH's yardsticks, or key 0's
    cmp byte [vp_snd], VP_SPK       ; THE SPEAKER AGAINST THE BANK (98.3.18.9)
    jne .nsq                        ; - a file that asks for a bank, played
    cmp word [vp_hxb], 0            ; on a machine with no card, whose PCM8
    je .nsq                         ; was not encoded for the speaker: the
    cmp byte [vp_spkpwm], 0         ; speaker's interrupts hold the bank
    jne .nsq                        ; idle and take the CPU the encode spent
    cmp word [vp_ytb], 0            ; on the picture - and that says what it
    je .nsq                         ; was made for (an older file: as ever)
    mov byte [vp_ybq], 1
.nsq:
    mov al, 0
    cmp byte [vp_lyon], 0
    je .nly
    cmp word [vp_lytl], VP_LYTMIN   ; (A KEY TOO SMALL TO TELL A CPU BY,
    jb .nly                         ; 98.1.9.12: no CPU verdict - the disk's)
    mov al, 1
.nly:
    or al, [vp_ybq]
    jz .r0
    cmp byte [vp_bnon], 0           ; (THE BENCH needs no key: 98.1.9.13)
    jne .go
    cmp word [vp_krec], 0xFFFF
    jne .go
    cmp byte [vp_ybq], 0            ; (no key to time: the bank wins - the
    je .r0                          ; picture is what the file was made for)
    call vp_ymute
.r0:
    ret
.go:
    call vp_bnio                    ; (the bench's surface: off the screen)
    call .tk
    mov bx, ax
.w:
    call .tk                        ; (from a tick's edge)
    cmp ax, bx
    je .w
    mov [vp_lyt0], ax
    call vp_dstamp                  ; (and the bracket's clock: 98.1.9.13)
    mov [vp_bnt0], ax
    mov [vp_bnt0+2], dx
    mov word [vp_lyn], 0
.l:
    mov dx, [vp_bnseg]              ; THE BENCH (98.1.9.13), built at
    xor si, si                      ; vp_bnsel...
    cmp byte [vp_bnon], 0
    jne .ld
    mov si, [vp_krec]               ; ...or the key's record, where it was
    mov dx, [vp_ring]               ; read (inline, not vp_rptr: this loop
    mov ax, si                      ; is the timed one)
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    call vp_decboth
    jmp short .lt
.ld:
    call vp_decrec
.lt:
    inc word [vp_lyn]
    call .tk                        ; THE BIOS'S TICK, read and not called
    sub ax, [vp_lyt0]               ; for (98.1.9.4): a far call a decode
    cmp ax, 8                       ; was most of a small key's time
    jb .l
    call vp_bnio                    ; (...and the screen's again)
    cmp ax, 11
    ja .slow                        ; (a decode near a second: too slow)
    push ax                         ; TO THE PIT COUNT (98.1.9.13): whole
    call vp_dstamp                  ; ticks were up to one decode short of
    sub ax, [vp_bnt0]               ; the time eight decodes took - an
    sbb dx, [vp_bnt0+2]             ; eighth on a machine whose decode is
    pop cx                          ; a tick. The bracket's clock is 0.84 us,
    dec cx                          ; and is believed while it agrees with
    cmp dx, cx                      ; the BIOS's to a tick (65,536 counts)
    jb .tkd
    mov cx, [vp_lyn]
    call vp_div32                   ; DX:AX = a decode's counts
    mov cx, 100
    call vp_mul32
    mov cx, 1193                    ; ...x 100 / 1,193: in 10 us
    call vp_div32
    or dx, dx
    jz .tt
    mov ax, 0xFFFF
    jmp short .tt
.tkd:
    inc cx
    mov ax, cx
    mov cx, 5493                    ; ticks x 54.93 ms, in 10 us
    mul cx
    div word [vp_lyn]               ; AX = a decode's time
.tt:
    or ax, ax                       ; (1 at the least: 0 is UNTIMED, and a
    jnz .t1                         ; fast machine's small key was timed)
    inc ax
.t1:
    mov [vp_lytm], ax
    cmp byte [vp_ybq], 0            ; THE SPEAKER: kept only on a machine
    je .ly                          ; twice as fast as the one the file was
    mov cx, ax                      ; made for - roughly that machine, and
    shl cx, 1                       ; the bank has the CPU (98.3.18.9)
    jc .ym
    cmp cx, [vp_ytb]
    jb .ly
.ym:
    call vp_ymute
.ly:
    mov ax, [vp_lytm]
    cmp byte [vp_lyon], 0
    je .r
    cmp word [vp_lytl], VP_LYTMIN   ; (timed for the speaker alone: the
    jb .r                           ; layer's yardstick says nothing)
    mov bx, [vp_lytl]               ; THE LINE: the mean of the layer's
    mov cx, [vp_lytb]               ; machine's and the base's, or an eighth
    mov dx, bx                      ; over the layer's where they are one
    add dx, bx
    shr dx, 1
    shr dx, 1
    shr dx, 1                       ; DX = a quarter of the layer's...
    add dx, bx                      ; ...and it: 1.25x
    cmp cx, dx
    ja .two
    mov dx, bx                      ; ONE MACHINE: 1.125x the layer's
    shr dx, 1
    shr dx, 1
    shr dx, 1
    add dx, bx
    jmp short .cmp
.two:
    mov dx, bx
    add dx, cx
    rcr dx, 1                       ; (the mean, its carry kept)
.cmp:
    add dx, 2                       ; ...and 20 us over it: the loop's own
    jc .yes                         ; (98.1.9.4)
    cmp byte [vp_bnon], 0           ; (THE BENCH's is any start's: kept)
    jne .k0
    cmp word [vp_sel], 0            ; KEY 0 is what the yardstick is: its
    je .k0                          ; verdict kept for the file; another
    cmp byte [vp_lycv], 0           ; key's time is not key 0's, so a kept
    je .k0n                         ; verdict wins
    cmp byte [vp_lycv], 1
    je .r
    jmp short .no
.k0:
    mov byte [vp_lycv], 1
    cmp ax, dx
    jbe .r
    mov byte [vp_lycv], 2
    jmp short .no
.k0n:
    cmp ax, dx
    jbe .r
.no:
    mov byte [vp_lyon], 0
    mov byte [vp_lyoff], 1
.r:
.yes:
    ret
.tk:                                ; AX = the BIOS's tick, 0040:006C (the
    push es                         ; kernel keeps it exact at any rate)
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]
    pop es
    ret
.slow:
    cmp byte [vp_ybq], 0
    je .sl
    call vp_ymute
.sl:
    cmp byte [vp_lyon], 0           ; (a layer to turn off, or none)
    jne .no
    ret

; vp_bnsel - the session's bench, at vp_lybench's start (98.1.9.13): with
; the file's bench words and a play decoded NATIVELY in a layout with an
; off-screen gap, the player's own record built past the key's read and its
; lead ([vp_bnseg]), [vp_bnon] 1 and the yardsticks the bench's - else key
; 0's, as every file before. Clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_bnsel:
    mov byte [vp_bnon], 0
    mov si, vp_kytl                 ; key 0's yardsticks...
    call .cp
    cmp word [vp_bnb], 0
    je .r
    mov al, [vp_shadow]             ; (a shadow's image has no gap to aim
    or al, [vp_livem]               ; at: the key, as before)
    or al, [vp_resid]
    jnz .r
    cmp byte [vp_pixfmt], PF_CGA4   ; (no text screen)
    ja .r
    mov bx, 8000                    ; THE GAP the bench decodes into: CGA's
    mov al, [vp_tlay]               ; after bank 0, 192 bytes...
    cmp al, LAY_HERC
    jb .g
    mov bx, 7830                    ; ...Hercules', 87 rows of 90 in,
    je .g
    cmp al, LAY_MODEX
    ja .r
    mov bx, 0xFE00                  ; ...the VGA's past every page there is
.g:                                 ; (two of 32,000 at most)
    mov [vp_bnorg], bx
    xor ax, ax
    mov [vp_bnpof], ax
    mov [vp_bnclp], al
    mov ax, [vp_kbkb]               ; WHERE: past the key's read and a lead,
    mov cl, 6                       ; inside the ring and its mirror - which
    shl ax, cl                      ; the fill has not reached yet
    mov bx, [vp_alb]
    add bx, 15
    mov cl, 4
    shr bx, cl
    add ax, bx
    add ax, [vp_ring]
    mov [vp_bnseg], ax
    add ax, (VP_BNMAX + 15) / 16
    mov bx, [vp_k]
    add bx, [vp_msl]
    mov cl, 11
    shl bx, cl
    add bx, [vp_ring]
    cmp ax, bx
    ja .r                           ; (no room: the key's, as before)
    call vp_bngen
    mov byte [vp_bnon], 1
    mov si, vp_bnl                  ; ...or the bench's: the three in a row
.cp:
    push ds
    pop es
    mov di, vp_lytl
    mov cx, 3
    cld
    rep movsw
.r:
    ret

; vp_bnio - the bench's surface swapped in, or back (98.1.9.13): the origin
; its gap, page 0 and no text box to round. Every register kept
vp_bnio:
    cmp byte [vp_bnon], 0
    je .r
    push ax
    mov ax, [vp_bnorg]
    xchg ax, [vp_org]
    mov [vp_bnorg], ax
    mov ax, [vp_bnpof]
    xchg ax, [vp_poff]
    mov [vp_bnpof], ax
    mov al, [vp_bnclp]
    xchg al, [vo_clip]
    mov [vp_bnclp], al
    pop ax
.r:
    ret

; vp_bngen - THE BENCH RECORD (98.1.9.13) at [vp_bnseg]:0 - tools/os88vid.py's
; bench_record, byte for byte: the mix's ten lists, or a plane's sub-record
; each with a quarter of it (SUB2: its list map, and the colour sub-record),
; every write inside VP_BNSPAN and every byte 0. [vp_bnlen] its bytes and
; [vp_bnsum] the check t_bench asks for. Clobbers AX, BX, CX, DX, SI, DI,
; BP, ES
vp_bngen:
    mov es, [vp_bnseg]
    mov di, 6
    cld
    cmp byte [vp_planar], 0
    jne .pl
    mov si, vp_bnmix
    mov cl, 0
    call .lists
    jmp short .hdr
.pl:
    mov al, 1                       ; a plane at a time
.pm:
    stosb
    push ax
    call .map
    mov si, vp_bnmix
    mov cl, 2                       ; (a quarter of the mix each)
    call .lists
    pop ax
    shl al, 1
    cmp al, 16
    jb .pm
    cmp byte [vp_planar], 2
    jne .end
    cmp byte [vp_sub2], 0
    je .end
    mov al, 0x10                    ; SUB2's colour lead, colour 0
    stosb
    call .map
    mov si, vp_bncmix
    mov cl, 0
    call .lists
.end:
    xor al, al
    stosb
.hdr:
    mov [es:0], di                  ; its length, rows 0 to 1
    xor ax, ax
    mov [es:2], ax
    inc ax
    mov [es:4], ax
    mov [vp_bnlen], di
    mov cx, di
    xor si, si
    xor bx, bx
    dec ax                          ; (AH 0 for the sum, DF clear)
.s:
    rol bx, 1                       ; THE CHECK: each byte added to the word
    es lodsb                        ; rotated left one
    add bx, ax
    loop .s
    mov [vp_bnsum], bx
    ret
.map:
    cmp byte [vp_sub2], 0
    je .mr
    mov ax, 0x03FF                  ; (all ten lists)
    stosw
.mr:
    ret
.lists:                             ; SI = the mix, CL its shift: the ten
    mov dl, 0                       ; lists, each row of list k in turn
.lk:
    push si
.row:
    mov al, [si]
    cmp al, 0xFF
    je .le
    cmp al, dl
    jne .nx
    call .emit
.nx:
    add si, 5
    jmp short .row
.le:
    pop si
    xor al, al
    stosb                           ; (the list's end)
    inc dl
    cmp dl, 10
    jb .lk
    ret
.emit:                              ; the row at SI: list, bytes, changes,
    push si                         ; absolute
    push dx
    push cx
    mov bx, [si+2]
    shr bx, cl                      ; BX = its changes
    mov al, [si]
    mov [vp_bnk], al
    mov al, [si+1]
    xor ah, ah
    mov [vp_bnn], ax
    cmp byte [si+4], 0
    jne .abs
    mov ax, VP_BNSPAN               ; SKIP-CODED: one byte apart from 0, as
    sub ax, [vp_bnn]                ; many as the span holds, 127 at most
    mov cx, [vp_bnn]
    inc cx
    xor dx, dx
    div cx
    inc ax
    cmp ax, 127
    jbe .pk
    mov ax, 127
.pk:
    mov bp, ax
.sg:
    or bx, bx
    jz .done
    mov cx, bp
    cmp bx, cx
    jae .c1
    mov cx, bx
.c1:
    sub bx, cx
    mov al, cl
    stosb
    xor ax, ax
    stosw                           ; (from address 0)
.en:
    stosb                           ; the skip: 0, then 1
    call .chg
    mov al, 1
    loop .en
    jmp short .sg
.abs:
    xor bp, bp                      ; ABSOLUTE: 3 apart, back to 0 at the
.ag:                                ; span's end
    or bx, bx
    jz .done
    mov cx, 127
    cmp bx, cx
    jae .c2
    mov cx, bx
.c2:
    sub bx, cx
    mov al, cl
    or al, 0x80
    stosb
.ae:
    mov ax, bp
    stosw
    call .chg
    add bp, 3
    mov ax, bp
    add ax, [vp_bnn]
    cmp ax, VP_BNSPAN
    jbe .an
    xor bp, bp
.an:
    loop .ae
    jmp short .ag
.done:
    pop cx
    pop dx
    pop si
    ret
.chg:                               ; the change: n bytes; a slice's length
    push cx                         ; and n; a run's length and a value -
    mov cx, [vp_bnn]                ; all of them 0
    cmp byte [vp_bnk], 5
    jbe .z
    mov al, cl
    stosb
    cmp byte [vp_bnk], 6
    je .z
    mov cx, 1
.z:
    xor al, al
    rep stosb
    pop cx
    ret

; THE BENCH's MIX (98.1.9.13): list, bytes a change, changes, absolute - and
; os88vid's BENCH_MIX/BENCH_CMIX, row for row (t_bench compares the record)
vp_bnmix:
    db 0, 1
    dw 400
    db 0
    db 0, 1
    dw 120
    db 1
    db 1, 2
    dw 300
    db 0
    db 2, 3
    dw 200
    db 0
    db 3, 4
    dw 160
    db 0
    db 4, 5
    dw 100
    db 0
    db 5, 6
    dw 100
    db 0
    db 6, 16
    dw 100
    db 0
    db 6, 40
    dw 40
    db 0
    db 7, 16
    dw 120
    db 0
    db 7, 64
    dw 40
    db 0
    db 0xFF
vp_bncmix:
    db 6, 16
    dw 60
    db 0
    db 0xFF

; vp_ymute - the bench's verdict for the speaker (98.3.18.9): MUTED, the
; bank given what the speaker would have taken - [vp_mwhy] 3, which the
; card says, M unmuting as ever. Every register kept
vp_ymute:
    push ax
    mov byte [vp_mute], 1
    mov byte [vp_mwhy], 3
    call vp_sndoff
    call vp_bkind                   ; (the bank is the session's now)
    pop ax
    ret

; vp_lyslots - the layer's SLOTS filled off the disk and nothing more: the
; bank is the prefill's to fill (vp_lypre), and the ask it was made for.
; Clobbers AX, BX, CX, DX, SI, DI, ES
vp_lyslots:
    mov al, [vp_lyxq]
    push ax
    mov byte [vp_lyxq], 0
.l:
    call vp_lyread
    jnc .l
    pop ax
    mov [vp_lyxq], al
    ret

; vp_lydrain - a slot empty and the layer's bank holding its next: the
; bank's head copied down into it. CF=0 one was; CF=1 nothing to drain.
; Clobbers AX, BX, CX, DX, SI, DI, ES
vp_lydrain:
    cmp word [vp_lyxc], 0
    je .no
    mov ax, [vp_lyrd]
    sub ax, [vp_lyuse]
    cmp ax, [vp_lyns]
    jae .no
    mov bx, [vp_lyns]
    dec bx
    and bx, [vp_lyrd]
    push bx
    call vp_lyslseg
    mov si, bx
    mov ax, [vp_lyxh]
    mov di, 1                       ; DOWN
    call vp_lyxcp
    pop bx
    jc .r
    or bx, bx                       ; (slot 0: mirrored, 98.1.9.8)
    jnz .m
    call vp_lymir
.m:
    mov ax, [vp_lyxh]
    inc ax
    cmp ax, [vp_lyxn]
    jb .h
    xor ax, ax
.h:
    mov [vp_lyxh], ax
    dec word [vp_lyxc]
    inc word [vp_lyrd]              ; published LAST
    clc
.r:
    ret
.no:
    stc
    ret

; vp_lyxcp - AX = a bank slot, SI = a layer slot's SEGMENT, DI = 0 up into the bank /
; 1 down out of it: its 32 KB copied (OSAPI_XMEM_COPY). CF=1 refused, and
; the bank is dropped for the session - the disk serves the layer on from
; where the bank's head was, and what the bank held is lost to it, the
; hook stepping over those frames' records. Clobbers AX, BX, CX, DX, SI, ES
vp_lyxcp:
    mov dx, ax
    shr dx, 1                       ; slot x 32768
    and ax, 1
    ror ax, 1
    add ax, [vp_lyxb]
    adc dx, [vp_lyxb+2]
    mov es, si
    xor si, si
    mov cx, VP_CHUNK
    call OSAPI_XMEM_COPY
    push ds
    pop es
    jnc .ok
    mov byte [vp_lyxq], 0
    mov word [vp_lyxc], 0
    stc
.ok:
    ret

; vp_lyget - BX = a layer slot's SEGMENT: the layer's next 32 KB CHUNK off
; the disk into it (98.1.9.8) - the records run on across chunks, so a
; chunk is bytes and nothing in it is checked here; the hook checks each
; record as it meets it. A short chunk is the file's end: the layer's last.
; CF=1: none - and none will be read after it, the layer playing on from
; what is in. Clobbers AX, BX, CX, DX, SI, DI, ES
vp_lyget:
    mov dx, bx                      ; DX:BX = the slot
    xor bx, bx
    push ds
    pop es
    mov di, vp_lycur
    mov si, vp_name
    mov cx, VP_CHUNK
    call OSAPI_FILE_READ_SEQ
    jc .off
    or dx, dx
    jnz .got
    cmp ax, VP_CHUNK
    jae .got
    or ax, ax
    jz .off                         ; (nothing left: the end)
    mov byte [vp_lyeof], 1          ; (short: the file's end, and this the
.got:                               ; last of the layer)
    clc
    ret
.off:
    mov byte [vp_lyeof], 1          ; (unread or unreadable: no more of it
    stc                             ; is read - what the slots and the bank
    ret                             ; hold is still drawn)

; vp_lyslseg - BX = a layer slot: BX = its segment. Every other register kept
vp_lyslseg:
    push cx
    mov cl, 11
    shl bx, cl
    add bx, [vp_lyseg]
    pop cx
    ret

; vp_lybounce - BX = THE BANK'S BOUNCE: past the slots and their mirror.
; Every other register kept
vp_lybounce:
    push ax
    push cx
    mov bx, [vp_lyns]
    mov cl, 11
    shl bx, cl
    mov ax, [vp_lymkb]
    mov cl, 6
    shl ax, cl
    add bx, ax
    add bx, [vp_lyseg]
    pop cx
    pop ax
    ret

; vp_lymir - slot 0 just filled: THE MIRROR (98.1.9.8) - its head, as much
; as the longest record can run on into it, copied past the last slot, so
; a record that starts in the last slot and runs on into slot 0's chunk is
; whole in memory where the hook reads it. The hook cannot reach that
; record before slot 0 is published, and slot 0 is not filled again until
; the hook is past it, so the copy holds for as long as it is read. Every
; register kept
vp_lymir:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov ax, [vp_lyns]
    mov cl, 11
    shl ax, cl
    add ax, [vp_lyseg]
    mov es, ax                      ; ES = the mirror
    mov ax, [vp_lymkb]
    mov cl, 9
    shl ax, cl
    mov cx, ax                      ; CX = its words (16,384 at the most)
    mov ds, [vp_lyseg]              ; DS = slot 0 (the last of ours read)
    xor si, si
    xor di, di
    cld
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; vp_lyxopen - a file's open, after the base's bank: THE LAYER's XMS BANK
; out of what the pool has left - the header's ask, or all that is left
; if it is less, in whole slots, two at least. The base's bank came first,
; held to its own ask where the file asks for both. None on an EMS-only
; machine. Every register kept
vp_lyxopen:
    push ax
    push cx
    push dx
    mov word [vp_lyxn], 0
    mov ax, [vp_lysp]
    or ax, [vp_lysp+2]
    jz .out
    cmp word [vp_lybkb], 0
    je .out
    push bx
    call vp_xcaps                   ; AX = the pool's KB (and DX:CX, BL
    pop bx                          ; its other answers)
    mov cx, [vp_lybkb]
    cmp ax, cx
    jbe .t
    mov ax, cx
.t:
    mov cl, 5
    shr ax, cl                      ; ...in slots
.try:
    cmp ax, 2
    jb .out
    push ax
    mov dx, ax
    shr dx, 1                       ; DX:AX = slots x 32 KB
    and ax, 1
    ror ax, 1
    call OSAPI_XMEM_ALLOC
    pop cx
    jnc .got
    shr cx, 1                       ; (no run that long: half)
    mov ax, cx
    jmp short .try
.got:
    mov [vp_lyxb], ax
    mov [vp_lyxb+2], dx
    mov [vp_lyxn], cx
.out:
    pop dx
    pop cx
    pop ax
    ret

; vp_lyxfree - the layer's bank, if there is one. Every register kept
vp_lyxfree:
    cmp word [vp_lyxn], 0
    je .r
    push ax
    push dx
    mov word [vp_lyxn], 0
    mov byte [vp_lyxq], 0
    mov ax, [vp_lyxb]
    mov dx, [vp_lyxb+2]
    call OSAPI_XMEM_FREE
    pop dx
    pop ax
.r:
    ret

; vp_lypre - a session's first start, after the base's prefill: THE
; LAYER's, the header's KB of it (FFFFh: every slot and the bank whole)
; read before the first frame, saying `Buffering 37% ~24s` as the base's
; does - in the base's own loop (vp_bpre.n), [vp_pfly] 1 turning its
; questions to the layer. Space plays with what is in; Esc cancels: CF=1.
; Clobbers AX, BX, CX, DX, SI, DI
vp_lypre:
    cmp byte [vp_lyon], 0
    je .done
    mov ax, [vp_lypkb]
    or ax, ax
    jz .done
    mov cx, [vp_lyns]               ; HOW FAR: the slots and the bank, or
    cmp byte [vp_lyxq], 0           ; the ask, if less
    je .c
    add cx, [vp_lyxn]
.c:
    cmp ax, 0xFFFF
    je .n
    add ax, 31
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    cmp ax, cx
    jae .n
    mov cx, ax
.n:
    inc byte [vp_pfly]              ; 1 (every way out of the loop puts it
    jmp vp_bpre.n                   ; back to 0): the BASE's loop fills it
.done:
    jmp vp_bpre.done

; vp_lybh - in the hook, CX = the frames this call draws: two or more is
; BEHIND, and the layer waits for the picture to catch up. Every register
vp_lybh:
    mov byte [vp_lybeh], 0
    cmp cx, 1
    jbe .r
    mov byte [vp_lybeh], 1
.r:
    ret

; vp_lyready - in the hook, before frame [vp_done]'s base record (98.1.9):
; CF=1 when the layer is played, the hook is not behind, and that frame's
; layer record is not read yet - its chunk, or the next one it runs on into.
; The FRAME WAITS (a stall, as for a base record not in the ring), because
; drawn without it the layer's next records repair a picture they were not
; made for: each carries only what the ones before it left, so a record
; MISSED is a repair never sent again - on the owner's 486 the base's cut
; leftovers, red tail lights across a whole shot for a second, where every
; skip and miss counter said nothing had gone. The layer is read only when
; the disk is free (vp_lyread), which a waiting picture makes it. Every
; register kept
vp_lyready:
    cmp byte [vp_lyon], 0
    je .ok
    cmp byte [vp_lybeh], 0          ; (behind: vp_lydec steps over it)
    jne .ok
    push ax
    mov ax, [vp_lynx]
    cmp ax, [vp_done]               ; (ahead of the picture, or an earlier
    pop ax                          ; frame's to step over: not this one)
    jne .ok
    push ax
    push bx
    push cx
    push si
    push es
    mov ax, [vp_lyuse]              ; its chunk in...
    cmp ax, [vp_lyrd]
    jae .no
    inc ax
    mov si, [vp_lyo]
    cmp si, VP_CHUNK - 1            ; ...and its length word's,
    jb .lw
    cmp ax, [vp_lyrd]
    jae .no
.lw:
    mov bx, [vp_lyns]
    dec bx
    and bx, [vp_lyuse]
    call vp_lyslseg
    mov es, bx
    mov cx, [es:si]
    add cx, si
    jc .nx                          ; (a damaged length: vp_lydec's to stop)
    cmp cx, VP_CHUNK                ; ...and the next chunk if the record
    jbe .yes                        ; runs on into it
.nx:
    cmp ax, [vp_lyrd]
    jae .no
.yes:
    clc
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret
.no:
    stc
    jmp short .out
.ok:
    clc
    ret

; vp_lydec - in the hook, frame [vp_done]'s base record just decoded: its
; layer record decoded on top - or stepped over, the hook behind or the
; record's chunk not read yet. Records before the frame are stepped over
; too: a chunk that arrives late is caught up through, never played late.
; THE RECORDS RUN ON ACROSS CHUNKS (98.1.9.8): [vp_lyuse] is the chunk the
; next one starts in and [vp_lyo] where, and a record that runs on out of
; its chunk waits for the next one to be read - out of the LAST slot it
; runs on into the mirror (vp_lymir). Clobbers AX, BX, CX, DX, SI, DI, BP,
; ES
vp_lydec:
    cmp byte [vp_lyon], 0
    je .r
.cur:
    mov ax, [vp_lynx]
    cmp ax, [vp_done]
    ja .r                           ; (ahead of the picture: it waits)
    mov ax, [vp_lyuse]              ; its chunk, if it is in
    cmp ax, [vp_lyrd]
    jae .r
    inc ax                          ; AX = the chunk after it
    mov si, [vp_lyo]
    cmp si, VP_CHUNK - 1            ; (its length word runs on out of the
    jb .lw                          ; chunk: the next one in too)
    cmp ax, [vp_lyrd]
    jae .r
.lw:
    mov bx, [vp_lyns]
    dec bx
    and bx, [vp_lyuse]
    call vp_lyslseg
    mov es, bx
    mov cx, [es:si]                 ; the record's length
    cmp cx, 2
    jb .bad
    cmp cx, [vp_lymax]
    ja .bad
    mov bx, si
    add bx, cx                      ; (< 64 KB: 32,767 + 32,760 at most)
    cmp bx, VP_CHUNK
    jbe .in
    cmp ax, [vp_lyrd]               ; (it runs on out of the chunk: the
    jae .r                          ; next one in too)
.in:
    mov ax, [vp_lynx]
    cmp ax, [vp_done]               ; (an earlier frame's: stepped over,
    jne .miss1                      ; its chunk read too late for it)
    cmp cx, 2                       ; THIS frame's: drawn, unless it is empty
    je .adv                         ; or the hook is behind
    cmp byte [vp_lybeh], 0
    jne .late
    push cx
    mov dx, es                      ; DX:SI = the record, normalised
    mov ax, si
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    call vp_decrec
    pop cx
    inc word [vp_lygot]
    call .next
    ret
.late:
    inc word [vp_lyskp]
    jmp short .adv
.miss1:
    cmp cx, 2
    je .adv
    cmp ax, [vp_base]               ; (before the play's first frame - a
    jb .adv                         ; key's own, after a seek - is no miss)
    inc word [vp_lymis]
.adv:
    call .next
    jmp .cur
.next:                              ; past the record: CX = its length
    add [vp_lyo], cx
    inc word [vp_lynx]
    cmp word [vp_lyo], VP_CHUNK
    jb .nr
    sub word [vp_lyo], VP_CHUNK     ; (out of its chunk: that one back to
    inc word [vp_lyuse]             ; the reader)
.nr:
    ret
.r:
    ret
.bad:
    mov byte [vp_lyon], 0           ; (a record past the longest the file
    ret                             ; holds: the base alone from here)

; vp_sfree - the session's claims, whichever it holds
vp_sfree:
    push dx
    mov dx, [vp_rclm]               ; (the claim: [vp_ring] may start in it)
    call .f
    mov dx, [vp_keep]
    call .f
    mov dx, [vp_aseg]
    call .f
    mov dx, [vp_prevseg]
    call .f
    mov dx, [vo_s1seg]
    call .f
    mov dx, [vp_lyseg]
    cmp byte [vp_lyb], 0            ; (a flip's slots are the ring's)
    jne .nly
    call .f
.nly:
    push ax                         ; (AX the zero: a store of it is a byte
    xor ax, ax                      ; shorter than one of DX)
    mov [vp_lyb], al
    mov [vp_lyseg], ax
    mov [vp_lyon], al
    mov [vp_prevseg], ax
    mov [vo_s1seg], ax
    mov [vp_ring], ax
    mov [vp_rclm], ax
    mov [vp_keep], ax
    mov [vp_shseg], ax
    mov [vp_aseg], ax
    mov [vp_kkb], al
    pop ax
    pop dx
    ret
.f:
    or dx, dx
    jz .n
    call vp_mfree
.n:
    ret

; -----------------------------------------------------------------------------
; vp_srun - brackets on the session until it stops or goes back to the
; desktop. [vp_wantwin] asks for the window; the window is taken if it can
; host the play (vp_canwin) and the full screen otherwise
; -----------------------------------------------------------------------------
vp_srun:
    push ax
    push bx
    push cx
    push dx
    push di
.again:
    call vp_track
    mov byte [vp_winm], 0
    cmp byte [vp_wantwin], 0
    je .go
    call vp_canwin
    jc .go
    mov al, 1                       ; IN THE WINDOW: Play is Pause while it
    mov [vp_winm], al               ; plays, drawn before the bracket takes
    mov [vp_bpause], al             ; the screen
    call vp_clip
    call vp_buttons
    call vp_boxxy                   ; A DRAG moves the window's pixels by any
    mov ax, [vp_py]                 ; number of rows and the play puts the
    sub ax, [vp_cy0]                ; picture on a bank: where they differ,
    cmp ax, [vp_ppoff]              ; the box is painted again first, or
    je .go                          ; the rows between are left behind (the
    call vp_pposter                 ; owner's report)
.go:
    call OSAPI_WM_CLIP_CLEAR        ; THE BRACKET'S DOOR PAYS OFF THE WIDGET
                                    ; under whatever region is armed (SPEC.md
                                    ; 12.8.5.2): a play from a key reads its
                                    ; record first, in this hold, and the
                                    ; window's region above would leave the
                                    ; bar frozen behind the teardown
    mov byte [vp_exitr], VPX_STOP
    mov bx, [vp_win]
    mov ax, vp_main
    mov cl, FSXF_RATE
    mov ch, [vp_fsdisp]             ; 0 the window's display, n display n-1
    mov dx, [vp_pdiv]               ; (98.3.21, SPEC.md 53.1.2) - 0 whenever
                                    ; the window plays it, vp_canwin refusing
                                    ; another display's
    mov di, vp_hook
    push ax                         ; THE BLOCKS STAY PUT FOR THE BRACKET: its
    xor ax, ax                      ; hook reads them at interrupt time
    call vp_rmov                    ; (98.1.7.4) - and so do the session's
    call vp_smov                    ; (98.3.19.2)
    pop ax
    call OSAPI_FSX_RUN
    pushf
    push ax
    mov ax, vp_rmove
    call vp_rmov
    mov ax, vp_smove                ; ...movable again on the desktop
    call vp_smov
    pop ax
    popf
    jnc .ran
    mov word [vp_msg], vp_s_refused
    mov byte [vp_stopq], 2
    call vp_sstop
    jmp .out
.ran:
    mov al, [vp_exitr]
    cmp al, VPX_SWAP
    je .swap
    cmp al, VPX_DESK
    je .desk
    call vp_sstop                   ; STOPPED: over, and where it got to kept
    jmp .out
.swap:
    cmp byte [vp_winm], 0
    je .tow
    mov byte [vp_wantwin], 0        ; the window -> the full screen, as it was
    jmp .again
.tow:                               ; the full screen -> the window: playing
    cmp byte [vp_lsess], 0          ; (LIVE: back on the desktop, 98.3.10)
    je .tnk
    call vp_lback
    jmp .out
.tnk:
    cmp byte [vp_nokeep], 1         ; NO KEEPER (98.3.19): the canvas cannot
    jne .tw                         ; cross into the window, so the session
    mov bl, [vp_autop]              ; stops at the key at or before the frame
    xor al, al                      ; on the glass, its picture in the box -
    call vp_stopfor                 ; and plays on from that key in the
    or bl, bl                       ; window if it was playing and the
    jz .out                         ; window can host it
    call vp_track
    call vp_canwin
    jc .out
    mov byte [vp_startp], 0
    call vp_sstart
    jc .out
    mov byte [vp_wantwin], 1
    jmp .again
.desk:
    cmp byte [vp_nokeep], 1         ; NO FRAME KEPT (98.3.19): stopped at the
    jne .dk                         ; key at or before, as a swap is
    xor al, al
    call vp_stopfor
    jmp .out
.dk:
    cmp byte [vp_unmq], 0           ; UNMUTED in the window (98.3.17): the
    je .out                         ; play starts again from the key at or
    mov byte [vp_unmq], 0           ; before where it was, its sound in step,
    mov al, 1                       ; in the window again
    call vp_stopfor
    mov byte [vp_startp], 0
    call vp_sstart
    jc .out
    mov byte [vp_wantwin], 1
    jmp .again
.tw:
    cmp byte [vp_autop], 0          ; on in it if it was playing and the
    je .out                         ; window can host it, else paused there
    mov byte [vp_wantwin], 1
    call vp_track
    call vp_canwin
    jnc .again
    mov byte [vp_autop], 0          ; (the pause the swap made is the user's
    call vp_fmt                     ; now: Play resumes it)
    call vp_repaint
.out:
    call vp_parm                    ; (a session left paused banks on)
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vp_sstop - the session is over: the time it played, the card closed, its
; memory back, and - by [vp_stopq] - where the next play starts (vp_after):
; 0 kept, with its picture; 1 kept, the picture left to the caller; 2 not
; touched. Painted when 0
; -----------------------------------------------------------------------------
vp_sstop:
    push ax
    push bx
%ifdef VP_DIAG
    call vp_prend                   ; (THE PLAY'S PROFILE, saved)
%endif
    call vp_eend                    ; (in place: the bank again, emptied)
    call vp_xipend                  ; (held in place: the yield sites off)
    xor ax, ax
    mov [vp_lrun], al               ; (the worker, if live, is held off by the
    mov [vp_lsess], al              ; lock we hold, and idles from here)
    mov [vp_lend], al               ; (a Live end's wake still queued must
    mov [vp_ldrain], al             ; not stop the NEXT session)
    cmp byte [vp_sess], 0
    je short .out                   ; (short: nasm took either form)
    cmp byte [vp_dtok], 0           ; the time it played, if a bracket's end
    jne .tk                         ; did not take it already
    call vp_dtcalc
.tk:
    cmp byte [vp_sopn], 0           ; open, whether or not it is still the
    je .nc                          ; clock: verb 2, the card stops and lets
    call vp_sclose                  ; go of the ring before it is freed
.nc:
    call vp_sfree
    xor al, al
    mov [vp_sess], al
    mov [vp_bpause], al
    mov [vp_upause], al
    mov [vp_ready], al
    cmp byte [vp_sfirst], 0         ; it never ran: no position, no costs
    jne .msg
    cmp word [vp_msg], vp_s_refused
    je .aft
    mov word [vp_msg], vp_s_ready
    cmp byte [vp_err], 0
    je .aft
    mov ax, [vp_errmsg]
    mov [vp_msg], ax
.aft:
    call vp_after                   ; WHERE THE PLAY GOT TO (98.3.6) - and
    mov byte [vp_played], 1         ; only then is it over, [vp_played] being
.msg:                               ; what a gate waits on
    cmp word [vp_msg], vp_s_ready   ; A PLAY THAT COULD NOT: the card comes
    je .fmt                         ; out, since it is what says why - on the
    cmp byte [vp_lcard], 0          ; wake, which lays the window out again
    jne .fmt
    mov al, 1
    mov [vp_card], al
    mov [vp_relay], al
    call vp_wakew
.fmt:
    call vp_fmt
    cmp byte [vp_stopq], 0
    jne .out
    call vp_repaint
.out:
    mov byte [vp_stopq], 0
    pop bx
    pop ax
    ret

; vp_onclose - W_ONCLOSE (SPEC.md 75.1), SI = the window, the lock held: the
; close is let happen (CF=0), but a session left paused - an Esc out of the
; bracket, still banking from the timer - is STOPPED first, by the same
; vp_sstop a Stop takes, and the file's hold in XMS or EMS given back, so the
; teardown finds nothing of a play alive (98.3.18.10)
vp_onclose:
    push ax
    mov al, 2                       ; (the window is going: nothing painted)
    call vp_stopfor
    call vp_xfree
    pop ax
    clc
    ret

; vp_stopfor - AL = vp_sstop's mode: the session over, if there is one, for
; something the user did in the window that it cannot outlive
vp_stopfor:
    cmp byte [vp_sess], 0
    je .out
    mov [vp_stopq], al
    call vp_sstop
.out:
    ret

; -----------------------------------------------------------------------------
; vp_canwin - CF=0 the window can host the play (98.3.7): the picture at the
; video's own size, the window uncovered, and the picture's rect whole on the
; screen. [vp_nowin] (a gate's) says no
; -----------------------------------------------------------------------------
vp_canwin:
    push ax
    push bx
    push cx
    push dx
    mov al, [vp_nowin]
    or al, [vp_fsdisp]              ; ...or another display's (98.3.21)
    or al, [vp_screen]              ; ...and a VGA4 file's screen of its own
    jnz .no                         ; (98.1.3.2.1)
    cmp byte [vp_flip], 2           ; page-ahead: two pages, full screen only
    je .no                          ; (98.3.8.3)
    cmp byte [vp_pixfmt], PF_VGA8   ; 256 colours: full screen only (98.3.7)
    je .no
    cmp byte [vp_pixfmt], PF_CGA4   ; ...and CGA's colours, whose modes the
    jae .no                         ; one-bit desktop is not (98.3.12)
    mov ax, [vp_palo]               ; ...or its own sixteen on any screen
    or ax, [vp_palo+2]              ; (98.1.3.2.2): VGA8 left above, so a
    jnz .no                         ; palette here is VGA4's, and its colours
                                    ; are not the desktop's
    cmp word [vp_ps], 1
    jne .no
    mov bx, [vp_win]
    call OSAPI_WM_OBSCURED          ; covered, or hidden
    jc .no
    call vp_boxxy
    mov ax, [vp_pdw]                ; the whole width, on its byte
    cmp ax, [vp_lpw]
    jne .no
    call vp_video                   ; AX, BX = the screen
    mov cx, [vp_px]
    test cx, cx
    js .no
    add cx, [vp_lpw]
    cmp cx, ax
    ja .no
    mov dx, [vp_py]
    cmp dx, MBAR_H
    jl .no
    add dx, [vp_h]
    cmp bx, dx                      ; CF = past the screen's foot
    jmp short .out
.no:
    stc
.out:
    jmp vp_ld4

; vp_dinfo - the desktop's framebuffer, as the decoder addresses it: its
; segment [vp_dseg] and its layout [vp_dlay] (98.1.2). The desktop IS one of
; the three: CGA's mode 6, the Hercules page, and mode 12h (or an EGA's 10h,
; the same linear image 350 rows deep), each at its adapter's segment
vp_dinfo:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_VIDEO                ; DL = the adapter
    mov ax, 0xB800
    xor bl, bl
    cmp dl, VID_CGA
    je .s
    mov ax, 0xB000
    inc bl
    cmp dl, VID_HERC
    je .s
    mov ax, 0xA000
    inc bl
.s:
    mov [vp_dseg], ax
    mov [vp_dlay], bl
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_boxxy - where the picture goes in the box, on the screen: [vp_px] on a
; byte, [vp_py] on the desktop layout's bank (so the decoder's addresses
; land there unchanged, 98.1.2 - which is what the box's three rows of slack
; are for), [vp_pdw] the width the box shows. After vp_track. It asks the
; adapter itself: a box placed before the first play once rounded to the
; default layout's bank (CGA's two rows on a Hercules), and the play then
; drew up to three rows below the poster, leaving a bar (the owner's report)
vp_boxxy:
    push ax
    push bx
    push cx
    push dx
    call vp_dinfo
    mov ax, [vp_cx0]                ; the box's inside
    add ax, VP_BOXX
    mov [vp_bx1], ax
    add ax, [vp_lbw]
    dec ax
    mov [vp_bx2], ax
    mov ax, [vp_cy0]
    add ax, VP_BOXY
    mov [vp_by1], ax
    add ax, [vp_lbh]
    dec ax
    mov [vp_by2], ax
    mov ax, [vp_lbw]                ; x: centred, then up to the SCREEN's
    sub ax, [vp_lpw]                ; byte, so a window whose content is off
    shr ax, 1                       ; the grid loses up to 7 columns at the
    add ax, [vp_bx1]                ; right instead
    add ax, 7
    and ax, 0xFFF8
    mov [vp_px], ax
    mov cx, [vp_bx2]
    inc cx
    sub cx, ax
    cmp cx, [vp_lpw]
    jb .w
    mov cx, [vp_lpw]
.w:
    mov [vp_pdw], cx
    mov bl, [vp_dlay]               ; y: down to the next bank boundary
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1
    mov cl, [vp_laytab+bx+1]        ; banks: 1, 2 or 4
    xor ch, ch
    dec cx
    mov ax, [vp_by1]
    add ax, cx
    not cx
    and ax, cx
    mov [vp_py], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vp_after - a play is over: Play starts next where this one got to (98.3.6).
; Stopped part way, that is the keyframe at or before the last frame drawn;
; played to the end, the start again, with the poster back in the box. A play
; that drew nothing past where it started leaves the key as it was
; -----------------------------------------------------------------------------
vp_after:
    push ax
    cmp byte [vp_stopq], 2          ; (the caller moves it itself)
    je .out
    cmp byte [vp_err], 0
    jne .out
    cmp word [vp_nkeys], 0
    je .out
    mov ax, [vp_done]
    cmp ax, [vp_frames]
    jb .mid
    mov word [vp_sel], 0            ; THE END: from the start, and the poster
    cmp byte [vp_stopq], 0
    jne .out
    mov ax, [vp_poster]
    cmp ax, [vp_nkeys]
    jae .out
    cmp ax, [vp_dkey]
    je .out
    mov byte [vp_thcp], 1           ; (the custom poster, 98.1.3.11)
    call vp_loadkey
    jmp short .out
.mid:
    dec ax                          ; the last frame drawn
    js .out
    call vp_keyat                   ; AX = the key at or before it
    jc .out
    cmp byte [vp_stopq], 0          ; 1: the position alone - a step from it
    je .pic                         ; follows, and loads its own picture
    mov [vp_sel], ax
    jmp short .out
.pic:
    cmp ax, [vp_dkey]               ; THE BOX SHOWS WHERE PLAY STARTS NEXT: a
    jne .ld                         ; stop before the next key rounds back to
    cmp ax, [vp_kload]              ; the one it started from, and the box -
    je .sel                         ; the poster, or the paused frame - was
.ld:                                ; not that key's picture (the owner's
    call vp_loadkey                 ; field report)
    cmp ax, [vp_kload]              ; picked only if its entry is in hand,
    jne .out                        ; which is what a play from it needs
.sel:
    mov [vp_sel], ax
.out:
    pop ax
    ret

; vp_keyat - AX = a frame -> AX = the keyframe at or before it, CF=1 none
; could be read. The table is not in memory, so it is ESTIMATED - keys are
; evenly spaced, so frame x keys / frames is the one or its neighbour - and
; each guess read and stepped until it is right: one or two reads, and never
; more than a bounded number
vp_keyat:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [vp_kat_t], ax
    mul word [vp_nkeys]
    div word [vp_frames]            ; (t < frames, so the key < keys)
    mov [vp_kat_i], ax
    mov word [vp_kat_n], 24
    mov ax, [vp_kekb]               ; (it reads entries and nothing else)
    call vp_mclaim
    jc .fail
    mov [vp_rdseg], dx
.l:
    dec word [vp_kat_n]
    jz .got
    mov ax, [vp_kat_i]
    call vp_kent
    jc .bad
    mov ax, [vp_ke+KE_K]
    cmp ax, [vp_kat_t]
    jbe .up
    cmp word [vp_kat_i], 0          ; past it: the key before
    je .got
    dec word [vp_kat_i]
    jmp short .l
.up:
    mov ax, [vp_kat_i]              ; at or before it: is the next one too?
    inc ax
    cmp ax, [vp_nkeys]
    jae .got
    call vp_kent
    jc .bad
    mov ax, [vp_ke+KE_K]
    cmp ax, [vp_kat_t]
    ja .got
    inc word [vp_kat_i]
    jmp short .l
.got:
    clc
    jmp short .fr
.bad:
    stc
.fr:
    pushf                           ; (the entries' claim, either way)
    call vp_rdfree
    mov ax, [vp_kat_i]
    popf
    jmp short .out
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; vp_main - a bracket on the session (SPEC.md 53.1, 98.3.7): SI = window,
; DS = CS = ours. The surface - the window's rect on the desktop as it
; stands (a same-mode bracket, §53.7), or the full screen in its mode - the
; canvas put back onto it, and the play from where the session is. It ends
; with [vp_exitr]: STOP, SWAP (F, Alt+Enter) or DESK (Space or a click, in
; the window: back to the desktop, paused)
; =============================================================================
vp_main:
    OS88_ALTENTER_SEED              ; the Alt+Enter that got us here is held
    xor ax, ax
    mov [vp_stepq], ax              ; (no frame steps from the last bracket)
    dec ax
    mov [vp_sttgt], ax
    inc ax
    mov [vp_msync], al              ; (no mode change to wait out, yet)
    mov [vp_pfresh], al             ; a pause from before this bracket - the
                                    ; desktop's, or the last bracket's way
                                    ; out - has no periods pending in it
    cmp byte [vp_winm], 0
    je .fs
    call OSAPI_FSX_SURF             ; the display this bracket owns must be
    jc .fsw                         ; the primary, at (0,0): the decoder
    or ax, bx                       ; writes its framebuffer, and a window on
    jnz .fsw                        ; another card is not there (§53.7.1)
    call vp_wsurf
    jmp .surf
.fsw:
    mov byte [vp_winm], 0           ; ...else the full screen after all
.fs:
    mov al, [vp_fslay]              ; THE FULL SCREEN: the mode vp_canplay
    mov [vp_tlay], al               ; chose, native or through the shadow
    mov al, [vp_fsshd]
    mov [vp_shadow], al
    push ds
    pop es
    mov di, vp_fsi
    mov al, [vp_fsmode]
    call OSAPI_FSX_MODE
    jnc .mode
    mov ax, vp_s_refused
.err:                               ; (the bracket's refusals: AX = why)
    mov byte [vp_err], 1
    mov [vp_errmsg], ax
    ret
.mode:
    call vp_mchg                    ; A MONITOR'S RE-SYNC (98.3.20): when
                                    ; the mode really changed, its time
    mov ax, [vp_fsi+FSI_SEG]
    mov [vp_vseg], ax
    call vp_hpg2                    ; (a Hercules's second page, or refused)
    jnc .hp2
    mov ax, vp_s_hpage
    jmp short .err
.hp2:
    call vp_dac                     ; VGA8: the file's 256 colours
    call vp_crtc                    ; ...and each row twice, if it asks
    call vp_scrset                  ; ...or a VGA4 file's own screen
    call vp_cgaset                  ; ...or CGA's colours (98.3.12)
    ; the origin: centred, the row on a bank (SPEC.md 98.1.2), on the
    ; screen's layout - the file's own, or the shadow's target (98.3.2)
    mov bl, [vp_tlay]
    call vp_lay6                    ; BX = the layout x 6
    mov ax, [vp_laytab+bx+4]        ; rows...
    cmp byte [vp_mx200], 0          ; (...Mode X's 320 x 200's, 98.1.3.1.1,
    je .orx                         ; or a screen of its own's: 98.1.3.2.1)
    mov ax, 200
.orx:
    cmp byte [vp_screen], 0
    je .orw
    mov ax, [vp_scrows]
.orw:
    mov cl, [vp_rs]                 ; ...which a row scale halves (98.2.4)
    shr ax, cl
    sub ax, [vp_h]
    shr ax, 1                       ; y0
    xor dx, dx
    mov cl, [vp_laytab+bx+1]        ; banks
    xor ch, ch
    div cx                          ; AX = y0 / banks, rounded down
    push ax
    mul cx
    mov [vp_ty0], ax                ; ...as a row, for the shadow's copy
    pop ax
    mul word [vp_laytab+bx+2]       ; ...rows of stride
    mov cx, [vp_laytab+bx+2]
    cmp byte [vp_screen], 0         ; (...centred in the bytes it SHOWS)
    je .orc
    mov cx, [vp_scwb]
.orc:
    sub cx, [vp_wb]
    shr cx, 1                       ; x0
    mov [vp_tx0], cx
    add ax, cx
    mov [vp_org], ax
    ; COMPOSITE (SPEC.md 98.3.3): a CGACOMP file on a real CGA turns the
    ; colour burst on, which is what makes its stripes colours on a
    ; composite monitor. 3D8h is the app's past the mode set (§53.7), and the
    ; bracket's restore sets the mode, and with it the burst, back
    mov byte [vp_burst], 0
    cmp byte [vp_pixfmt], 1         ; CGACOMP (the byte is the format - 1)
    jne .surf
    cmp byte [vp_fsmode], FSXM_CGA640
    jne .surf
    call OSAPI_VIDEO                ; DL = the adapter: only a CGA has a
    cmp dl, VID_CGA                 ; composite output; an EGA's or a VGA's
    jne .surf                       ; mode 6 is RGB and 3D8h is not theirs
    mov dx, 0x3D8
    mov al, 0x1A                    ; 640x200 graphics, video on, burst ON
    out dx, al
    mov byte [vp_burst], 1
.surf:
    mov ax, 2                       ; through the shadow the decode is cheap
    cmp byte [vp_shadow], 0         ; and the copy is not: more frames a call,
    je .fc                          ; one copy after them
    mov al, 8
.fc:
    cmp byte [vp_fpg], 0            ; (flipped and built ahead: every frame
    jne .fc2                        ; flipped too)
    cmp byte [vp_flip], 1           ; FLIPPING: a frame a call, so a flip has
    jne .fc1                        ; a period to latch before the next draw
.fc2:
    mov al, 1                       ; goes into the page it replaces (page-
                                    ; ahead waits for its rare flips instead)
.fc1:
    mov [vp_fcap], ax
    call vo_setup                   ; the full screen's text (98.3.13)
    call OSAPI_MOUSE                ; the buttons as they are: a CLICK is a
    mov [vp_mbtn], al               ; press after this
    xor ax, ax                      ; PAGES (98.3.8): page 0 on the glass,
    mov [vp_dy1], ax
    mov [vp_poff], ax               ; the other drawn next, and no record
    mov [vp_foff], ax               ; owed to it yet
    mov [vp_prevn], ax
    mov [vo_sl0], ax                ; (the text's two saves on pages 0 and
    dec ax                          ; 1, until N pages move one: vo_pgsave)
    mov [vp_dy0], ax
    mov [vp_wtk], ax                ; the thumb: asked at the first frame
    mov ax, [vp_page]
    mov [vo_sl1], ax
    cmp byte [vp_flip], 1           ; (page-ahead draws on the page shown)
    jne .pg
    mov [vp_poff], ax
.pg:
    ; THE CANVAS onto this surface (98.3.7): where the session got to, black
    ; before its first frame - but NOT YET on a session's first bracket
    cmp byte [vp_sfirst], 0
    jne .sfst
    call vp_kput
    cmp byte [vp_autop], 0          ; A LATER BRACKET: on from where it was,
    je .rdy                         ; playing again if the last one's end was
    mov byte [vp_autop], 0          ; what paused it
    call vp_upaus
.rdy:
    mov byte [vp_ready], 1
    jmp .loop
.sfst:
    ; THE PICTURE STAYS UP UNTIL THE PLAY CAN DRAW OVER IT (98.3.7.1): the
    ; ring's fill is seconds off a disk, and blacking the canvas before it
    ; left the window black for all of them. From frame 0 the black waits
    ; for the fill; from a key the screen already shows, it is not needed
    ; at all - the key's writes are the bytes already there
    xor ax, ax
    mov [vp_sfirst], al
    inc ax
    mov [vp_pfok], al               ; (the bank's prefill, after the ring's)
    mov [vp_kblk], al               ; (from frame 0: black after the fill)
    cmp word [vp_krec], 0xFFFF
    je .first
    mov byte [vp_kblk], 0
    call vp_kheld
    jnc .first
    call vp_kput                    ; another picture: black, then the key
.first:
    ; THE KEYFRAME (98.3.5): the screen after frame k, decoded onto the black
    ; - or into the shadow, and copied - before the ring is filled over its
    ; record
    cmp word [vp_krec], 0xFFFF
    je .lead
    call vp_paldac                  ; (its scene's sixteen, onto the black)
.kp:
    mov si, [vp_krec]
    mov dx, [vp_ring]
    mov ax, si
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    call vp_khdr                    ; (98.1.3.8: another part after it?)
    jc .kpbad
    push dx
    mov ax, si
    add ax, [vp_kpn]
    push ax                         ; the record's end
    call vp_decboth                 ; SI past its lists
    pop ax
    pop dx
    cmp byte [vp_kmore], 0          ; KEYS IN PARTS (98.1.3.8): the next
    je .klast                       ; read over this one in the ring - the
    push word [vp_rdseg]            ; lead apart lies past a whole read's
    mov ax, [vp_ring]               ; claim, so it is not touched - and its
    mov [vp_rdseg], ax              ; writes checked as the first's were
    call vp_knrd
    pop word [vp_rdseg]
    jc .kpbad
    cmp byte [vp_kneed], 0
    je .kpok
    mov dx, [vp_ring]
    call vp_rbnd
    jc .kpbad
.kpok:
    mov [vp_krec], si
    jmp .kp
.kpbad:
    mov ax, vp_s_kbad
    jmp .err
.klast:
    cmp byte [vp_audio], 2          ; ADPCM4's keyframe carries the card's
    jne .kd                         ; REFERENCE after its lists: the sample
    cmp si, ax                      ; the stream holds at frame k+1, its
    jae .kd                         ; scale 0 there (98.1.1.1). A file made
    push ds                         ; before that has none, and starts at 80h
    mov ds, dx
    mov al, [si]
    pop ds
    mov [vp_aref], al
.kd:
    cmp byte [vp_shadow], 0
    je .lead
    call vp_blit
.lead:
    call vp_lybench                 ; THIS CPU, timed on the key: the layer
                                    ; and the speaker (98.1.9, 98.3.18.9)
    cmp byte [vp_krn], 0            ; THE PAGE POOL's pictures, the key whole
    je .kpl                         ; and benched - its read is where they
    call vp_kpool                   ; land (98.1.3.8.1)
    jnc .kpl
    ret
.kpl:
    call vp_lyunc                   ; (said no: a flip given back, 98.1.9.4)
    call vp_lstage                  ; (sound ahead: the lead out of the ring)
    mov word [vp_dtn], 0            ; THE DISK, timed on the fill (98.1.9.3)
    mov al, [vp_lyon]
    mov [vp_dton], al
.fill:                              ; fill the ring before the first frame: a
    cmp byte [vp_einp], 0           ; (IN PLACE the ring is the board's: two
    je .fl                          ; chunks are the first super-packet's,
    cmp word [vp_lc], 2             ; and the prefill or the play reads the
    jb .fl                          ; rest - and a THIRD while THE DISK's
    cmp byte [vp_dton], 0           ; verdict is owed, two timed past the
    je .fld                         ; first being its rule, 98.1.9.3; it is
    cmp word [vp_dtn], 3            ; taken at .fld)
    jae .fld
.fl:
    call vp_fill                    ; stream that fits is read whole
    jc .fld
    call vp_lyearly                 ; (a flip's verdict as soon as it can be)
    jmp short .fill
.fld:
    call vp_lydisk                  ; THE DISK's verdict on the layer...
    call vp_lyunc                   ; (...a flip given back, 98.1.9.4)
    call vp_lysplit                 ; ...and the pool split for this machine
    call vp_lyslots                 ; ...THE LAYER's slots too, before the
                                    ; first frame (98.1.9)
    cmp byte [vp_pfok], 0           ; ...and THE BANK, before a session's
    je .npf                         ; first frame (98.3.18.3) - not after a
    mov byte [vp_pfok], 0           ; seek, which wants the play now
    call vp_bpre
    jc .pfesc
    call vp_lypre                   ; ...and THE LAYER's (98.1.9)
    jnc .npf
.pfesc:
    xor ax, ax                      ; ESC while it filled: nothing played
    mov [vp_dt], ax
    inc ax                          ; (AL = 1 = VPX_STOP)
    mov [vp_dtok], al
    jmp .leave
.npf:
    mov cx, [vp_kidx]               ; ...then step over the records before
    jcxz .sk0                       ; frame k+1 in its super-packet, a len
.sk:                                ; hop each (98.1.3) - all in the ring
    push cx
    mov bx, vp_pc
    call vp_next
    pop cx
    jnc .skn
    cmp al, 1                       ; the end: a key on the last frame
    je .sk0
%ifdef VP_RDLOG
    call vp_prbad
%endif
    mov ax, vp_s_badsp
    jmp .err
.skn:
    loop .sk
.sk0:
    cmp byte [vp_kblk], 0           ; FROM FRAME 0: the black the first frame
    je .sk0b                        ; is drawn on, now that it is next
    mov byte [vp_kblk], 0
    call vp_kput
    call vp_paldac                  ; (frame 0's sixteen, onto it)
.sk0b:
    cmp byte [vp_startp], 0         ; F / Alt+Enter: IN PAUSED (98.3.6), on
    je .snd                         ; the picture where the play would start
    call vp_acur                    ; (the card, later, from HERE)
    cmp word [vp_krec], 0xFFFF      ; - the keyframe's, or else the first
    jne .held                       ; frame, drawn here with the hook idle
    mov al, [vp_snd]
    push ax
    mov byte [vp_snd], 0            ; (no card yet to keep it behind)
    call vp_frame
    pop ax
    mov [vp_snd], al
    cmp byte [vp_shadow], 0
    je .held
    call vp_blit
.held:
    mov byte [vp_msync], 0          ; (Space starts it: the user's time)
    mov byte [vp_upause], 1
    mov al, [vp_snd]                ; the card waits for the first Space
    mov [vp_sdefer], al
    call OSAPI_GET_TICKS
    mov [vp_t0], ax
    mov [vp_ptk0], ax
    call vp_rsay
    mov byte [vp_ready], 1
    cmp byte [vp_skgo], 0           ; A SEEK made while playing: on from the
    je .hv                          ; key (98.3.14)
    mov byte [vp_skgo], 0
    call vp_upaus
.hv:
    cmp word [vp_sttgt], 0xFFFF     ; A STEP BACK landed on its key: the
    je .hv1                         ; frames from there to the one wanted
    mov ax, [vp_sttgt]              ; stepped on (98.3.14.1) - the key's
    mov word [vp_sttgt], 0xFFFF     ; picture is frame [vp_done]-1
    mov bx, [vp_done]
    dec bx
    sub ax, bx
    jbe .hv1
    mov [vp_stepq], ax
.hv1:
    call vo_update                  ; ("Paused" again, if Space paused it)
    jmp short .loop
.snd:
    call vp_mwait                   ; (the monitor's re-sync, 98.3.20)
    cmp byte [vp_snd], 0
    je .go
    call vp_acur
    call vp_sopen                   ; the card, started with the picture
.go:
    call OSAPI_GET_TICKS
    mov [vp_t0], ax
    call vp_rsay                    ; (said before the first frame)
    mov byte [vp_ready], 1
.loop:
    call vp_poll                    ; AL = the way out, or 0
    or al, al
    jnz .leave
    call vp_skdue                   ; a seek whose wait is over: made, and
    jnc .lk                         ; the play from its key
    jmp .first
.lk:
    call vo_update                  ; (a toast's time)
    cmp byte [vp_end], 0
    jne .drain
    cmp byte [vp_snd], 0            ; ...or, with sound, every frame drawn:
    je .rd                          ; the clock stops at the last, so nothing
    cmp byte [vp_rep], 0            ; asks the stream for one past it (the
    jne .rd                         ; silent play still ends on the chain's
    mov ax, [vp_done]               ; own 0, which a hold can sit in front of)
    cmp ax, [vp_frames]             ; - unless it repeats (98.3.9), or is
    jb .rd                          ; PAUSED there, a frame step's last
    cmp byte [vp_upause], 0         ; (98.3.14.1): it drains once Space
    je .drain                       ; plays on
.rd:
%ifdef VP_DIAG
    cmp byte [vp_eof], 0            ; THE READER'S LEAST LEAD over the
    jne .rl                         ; picture, in chunks (98.3's diagnostic):
    mov ax, [vp_lc]                 ; loaded past the hook's super-packet,
    sub ax, [vp_pc]                 ; sampled every pass until the stream
    cmp ax, [vp_lmin]               ; has been read to its end
    jae .rl
    mov [vp_lmin], ax
    mov ax, [vp_done]
    mov [vp_lminf], ax
.rl:
%endif
    cmp word [vp_lyxc], 0           ; THE LAYER's bank, drained into a slot
    je .nld                         ; the hook let go, whatever the disk is
    call vp_lydrain                 ; doing (98.1.9)
.nld:
    call vp_skeep                   ; EVERY pass, not only an idle one: after
                                    ; an underrun the reader is catching up,
                                    ; so a chunk arrives each pass and the
                                    ; card sat silent until the whole ring
                                    ; was full again rather than until one
                                    ; block was queued (98.3.1)
    call vp_fill
    jnc .loop                       ; a chunk arrived: poll, and try again
    cmp byte [vp_lyord], 0          ; THE RING IS FULL: the LAYER next, where
    je .bk                          ; the file was made for it (98.1.9.1) and
    mov ax, [vp_xcnt]               ; the base's bank holds what this machine
    mov cl, 5                       ; needs to carry the base - the rest of
    shl ax, cl                      ; the bank is the machine's slack
    cmp ax, [vp_lyneed]
    jb .bk
    call vp_lyread
    jnc .loop
.bk:
    cmp byte [vp_bhold], 0          ; (a gate's: the bracket banks no more)
    jne .nbk
    call vp_bstep                   ; ...then the bank fills, a chunk a pass
    jnc .loop                       ; (VIDEO-XMS-PLAN 4.1)
.nbk:
    call vp_lyread                  ; THE LAYER, with the disk free (98.1.9)
    jnc .loop
    call vp_wthumb                 ; (in the window, the thumb moves)
    mov al, FSXW_FRAME              ; nothing to read yet: give the period
    call OSAPI_FSX_WAIT             ; to the hook
    jmp .loop                       ; (near in VPDIAG's longer loop)
.drain:                             ; the picture is done: the sound plays
    cmp byte [vp_snd], 0            ; out to its last byte, the hook still
    je .stop                        ; topping the ring up with its silence
    cmp byte [vp_err], 0
    jne .stop
    call OSAPI_GET_TICKS
    mov [vp_tdr], ax
.dl:
    call vp_poll                    ; any way out, now, is the end
    or al, al
    jnz .stop
    call vp_skdue                   ; ...but a seek back is not (98.3.14)
    jnc .dk
    jmp .first
.dk:
    call vo_update
    cmp byte [vo_skp], 0            ; one on its way: the end waits for it
    je .dnw
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp short .dl
.dnw:
    cmp byte [vp_aend], 2           ; the audio ran out early: nothing to wait
    je .stop                        ; for
    mov ax, [vp_aseq]               ; REPEAT TURNED OFF after the sound had
    cmp ax, [vp_vseq]               ; queued the next lap (98.3.9): the play
    jbe .df                         ; ends when the card has played the
    mov ax, [vp_syncf]              ; frames drawn, not the ones after
    cmp ax, [vp_vseq]
    jae .stop
    jmp short .dw
.df:
    cmp byte [vp_aend], 0
    je .dw
    mov ax, [vp_alast]              ; played past the last byte of sound?
    sub ax, [vp_afinal]
    jns .stop
.dw:
    call OSAPI_GET_TICKS            ; ...or two seconds: a card that has
    sub ax, [vp_tdr]                ; stopped does not hold the machine
    cmp ax, 37
    jae .stop
    call vp_skeep
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp short .dl
.leave:
    mov word [vp_stepq], 0          ; (frame steps end with the bracket)
    mov word [vp_sttgt], 0xFFFF
    cmp byte [vo_skp], 0            ; a seek not made: as it was
    je .lv
    mov byte [vo_skp], 0
    cmp byte [vp_skwas], 0
    je .lv
    push ax
    call vp_upaus
    pop ax
.lv:
    mov [vp_exitr], al
    cmp al, VPX_STOP
    je .stop
    cmp byte [vp_upause], 0         ; SWAP or DESK: the session PAUSES - the
    jne .rb                         ; card halted where it is - and a swap's
    call vp_upaus                   ; pause is resumed by the next bracket,
    cmp byte [vp_exitr], VPX_DESK   ; where Space's or a click's is the
    je .rb                          ; user's own
    mov byte [vp_autop], 1
.rb:
    call .off                       ; the text off: the canvas is read back
    call vp_kget                    ; the canvas off the screen, for the next
    mov byte [vp_bpause], 0         ; surface and the box
    cmp byte [vp_exitr], VPX_DESK
    je .pic
    cmp byte [vp_winm], 0           ; the window -> the full screen: the
    jne .ret                        ; desktop between them is not the point
.pic:
    call vp_sesspic                 ; THE FRAME IN THE BOX, for the repaint
.fmt:                               ; the bracket's exit makes (§53.6) - and
    call vp_fmt                     ; the card's text, which shares the box's
.ret:                               ; save
    ret
.stop:
    mov byte [vp_exitr], VPX_STOP
    call .off
    call vp_dtcalc                  ; the time it played - NOW, and not after
    jmp short .fmt                  ; the bracket's exit repaints the desktop
.off:                               ; the hook idle, and the text off
    pushf
    cli
    mov byte [vp_ready], 0
    popf
    jmp vo_off

; vp_mchg - the full screen's mode just set: [vp_msync] 1 and [vp_mtk] now
; where the HARDWARE mode changed - the desktop's geometry (OSAPI_VIDEO) is
; not the mode's - so a monitor re-syncs; 0 where it is the same mode
; (VGA12 over a 640 x 480 desktop, CGA640 over 640 x 200, HERC over
; 720 x 348), which no monitor sees. Every register kept
vp_mchg:
    push ax
    push bx
    push cx
    push dx
    call vp_video                   ; AX, BX = the desktop's width, height
    mov dl, [vp_fsmode]
    mov cx, 480
    cmp dl, FSXM_VGA12
    je .w640
    mov cx, 200
    cmp dl, FSXM_CGA640
    je .w640
    cmp dl, FSXM_HERC
    jne .chg
    cmp ax, 720
    jne .chg
    cmp bx, 348
    je .same
    jmp short .chg
.w640:
    cmp ax, 640
    jne .chg
    cmp bx, cx
    je .same
.chg:
    call vp_ticks
    mov [vp_mtk], ax
    mov byte [vp_msync], 1
.same:
    jmp vp_ld4

; vp_mwait - a play about to start in a bracket whose mode changed: held
; until VP_MSYNC ticks after the change, so a monitor that blanks while it
; re-syncs has a picture to show before the first frame and the card's
; first sound. A ring filled off a disk is usually longer than that and
; costs nothing here; one filled out of the XMS hold is not (the owner's
; report: the sound half a second ahead of a picture still blank). Once a
; bracket. Every register kept
vp_mwait:
    cmp byte [vp_msync], 0
    je .r
    mov byte [vp_msync], 0
    push ax
.w:
    call OSAPI_GET_TICKS
    sub ax, [vp_mtk]
    cmp ax, VP_MSYNC
    jae .x
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jnc .w
.x:
    pop ax
.r:
    ret

; vp_dtcalc - [vp_dt], the ticks the session PLAYED: since its first frame,
; less every pause - the one running now included
vp_dtcalc:
    push ax
    call vp_ticks                   ; (one reading for all three)
    cmp byte [vp_upause], 0
    je .np
    push ax
    sub ax, [vp_ptk0]
    add [vp_ptk], ax
    pop ax
    mov [vp_ptk0], ax
.np:
    sub ax, [vp_t0]
    sub ax, [vp_ptk]
    mov [vp_dt], ax
    mov byte [vp_dtok], 1
    pop ax
    ret

; vp_poll - the bracket's keys and, in the window, the mouse (SPEC.md 53.1:
; this IS the UI task, so it polls). out: AL = 0, or the way out - VPX_STOP
; for Esc, VPX_SWAP for F or Alt+Enter (off the key-state map, which int 16h
; never sees - apps/os88alt.inc), VPX_DESK for Space or a click in the
; window. Space in the full screen pauses and resumes in place
vp_poll:
    call os88alt_edge
    jc .swap
    cmp byte [vp_winm], 0
    je .key
    push cx
    push dx
    call OSAPI_MOUSE                ; AL = the buttons: a press is a click
    mov ah, [vp_mbtn]
    mov [vp_mbtn], al
    not ah
    and al, ah
    and al, 3
    jz .nclk
    mov bx, vp_brects + 4 * 8       ; ...on REPEAT it is Repeat, and the play
    cmp cx, [bx]                    ; goes on (98.3.9); anywhere else it is
    jb .dsk                         ; the desktop
    cmp cx, [bx+4]
    ja .dsk
    cmp dx, [bx+2]
    jb .dsk
    cmp dx, [bx+6]
    ja .dsk
    pop dx
    pop cx
    jmp short .rep
.dsk:
    mov bx, vp_brects + 5 * 8       ; ...on MUTE it is Mute (98.3.17)
    cmp cx, [bx]
    jb .dsk2
    cmp cx, [bx+4]
    ja .dsk2
    cmp dx, [bx+2]
    jb .dsk2
    cmp dx, [bx+6]
    ja .dsk2
    pop dx
    pop cx
    jmp .spk
.dsk2:
    pop dx
    pop cx
    jmp .desk
.nclk:
    pop dx
    pop cx
.key:
    mov ah, 1
    int 0x16
    jz .none
    xor ah, ah
    int 0x16
    or al, al                       ; an extended key: Left and Right seek
    jz .ext                         ; in the full screen (98.3.14)
    cmp al, 0xE0
    je .ext
    cmp al, 27
    je .stop
    cmp al, ' '
    je .space
    cmp al, '.'                     ; . and , - A FRAME ON or BACK, paused
    je .fwd                         ; (98.3.14.1)
    cmp al, ','
    je .bwd
    or al, 0x20
    cmp al, 'f'
    je .swap
    cmp al, 's'
    je .spk
    cmp al, 'm'
    je .spk
    cmp al, 'r'
    jne .none
.rep:
    xor byte [vp_rep], 1            ; REPEAT, mid-play: the reader and both
    cmp byte [vp_winm], 0           ; cursors read it at the file's end; in
    je .rtoast                      ; the window its button turns over - by
    mov word [vp_wvr], vp_brects + 4 * 8
    call vp_winv                    ; an XOR, which is exact here: nothing
.none:                              ; repaints inside a bracket, and the
    xor al, al                      ; exit's repaint draws it from [vp_rep]
    ret
.rtoast:
    call vo_toast_rep               ; ...and in the full screen it SAYS so
    jmp short .none                 ; (98.3.13)
.spk:                               ; M or S: MUTE, mid-play (98.3.17)
    cmp byte [vp_audio], 0
    je .none
    xor byte [vp_mute], 1
    mov al, [vp_mute]
    mov [vp_umute], al
    cmp byte [vp_winm], 0
    je .mfs
    mov word [vp_wvr], vp_brects + 5 * 8
    call vp_winv                    ; (its button turns over, as Repeat's)
    cmp byte [vp_mute], 0
    je .munw
    call vp_sndoff                  ; MUTED: now, and the play goes on
    jmp short .none
.munw:
    mov byte [vp_unmq], 1           ; UNMUTED in the window: to the desktop,
    jmp .desk                       ; where the play starts again, in step
.mfs:
    cmp byte [vp_mute], 0
    je .mfon
    call vp_sndoff                  ; ...in the full screen, now
    mov al, VOK_SNDOFF
    call vo_toastk
    jmp short .none
.mfon:
    call vp_unmfs                   ; ...and on again from the key here
    jmp short .none
.ext:
    cmp byte [vp_winm], 0
    jne .none
    cmp ah, 0x47                    ; Home: the start, now
    je .home
    mov al, 1
    cmp ah, 0x4D                    ; Right
    je .sk
    mov al, 0xFF
    cmp ah, 0x4B                    ; Left
    jne .none
.sk:
    call vp_skkey
    jmp short .none
.home:
    call vp_skhome
    jmp short .none
.fwd:
    call vp_stfwd
    jmp .none
.bwd:
    call vp_stbwd
    jmp .none
.space:
    cmp byte [vp_winm], 0
    jne .desk
    cmp byte [vo_skp], 0            ; a seek on its way resumes as it was
    jne .none
    cmp byte [vp_upause], 0         ; PAUSED says so, and the text is off
    jne .sres                       ; before the hook decodes again (98.3.13)
    call vp_upaus
    mov byte [vo_pz], 1
    call vo_update
    jmp .none
.sres:
    mov byte [vo_pz], 0
    call vo_update
    call vp_upaus
    jmp .none
.stop:
    mov al, VPX_STOP
    ret
.swap:
    mov al, VPX_SWAP
    ret
.desk:
    mov al, VPX_DESK
    ret

; =============================================================================
; SEEKING IN THE FULL SCREEN (SPEC.md 98.3.14): Left and Right pause the
; play and say where it will land, five seconds a press; half a second
; after the last press it goes there - the keyframe at or before that time,
; read, and the play started from it WITHOUT leaving the bracket, which
; would set the desktop's mode and repaint it between the two
; =============================================================================
VP_SKWAIT   equ 9                   ; ticks after the last press: 1/2 s
VP_SKMAXN   equ 99                  ; presses counted, either way

; FRAME STEPS (98.3.14.1, the owner, 2026-10-18): paused in the full
; screen, . draws the next frame and , the one before. ON is the hook's:
; [vp_stepq] frames it draws while paused, a played frame's draw each - so
; a flip, a layer or a held frame is exactly what the play would show.
; BACK is a SEEK: the key at or before the frame wanted, landed on paused,
; and the frames from it to that one stepped on - the player decodes
; nothing without drawing it, so the way there is seen, briefly. Either is
; refused playing, with a seek on its way, in the window, or at an end

; vp_stfwd - . : one frame on. Preserves all
vp_stfwd:
    cmp byte [vp_upause], 0
    je .no
    cmp byte [vo_skp], 0
    jne .no
    cmp word [vp_sttgt], 0xFFFF     ; (a step back still on its way)
    jne .no
    push ax
    pushf                           ; (the queue and the hook's counts read
    cli                             ; as one: it draws between two reads)
    mov ax, [vp_stepq]
    cmp ax, 99
    jae .x
    cmp byte [vp_rep], 0            ; AT THE END: the next drawn is [vp_done]
    jne .a                          ; and those queued, and never one past
    add ax, [vp_done]               ; the last - which ends the play
    cmp ax, [vp_frames]
    jae .x
.a:
    cmp byte [vp_snd], 0            ; ...nor past the sound queued, a started
    je .ok                          ; card being paused: the ring takes no
    cmp byte [vp_sdefer], 0         ; more, and vp_frame would refuse it
    jne .ok
    mov ax, [vp_vseq]
    add ax, [vp_stepq]
    cmp ax, [vp_aseq]
    jae .x
.ok:
    inc word [vp_stepq]
.x:
    popf
    pop ax
.no:
    ret

; vp_stbwd - , : one frame back, through a seek to the key at or before
; it. Preserves all
vp_stbwd:
    cmp byte [vp_upause], 0
    je .out
    cmp byte [vo_skp], 0
    jne .out
    cmp word [vp_stepq], 0          ; (steps on still to draw)
    jne .out
    cmp word [vp_nkeys], 0          ; (no key: nowhere to come back from)
    je .out
    push ax
    push bx
    push dx
    mov ax, [vp_done]               ; the frame on the glass is [vp_done]-1,
    sub ax, 2                       ; the one wanted the one before it
    jb .x
    push ax
    mov byte [vp_skhere], 1         ; THE KEY AT OR BEFORE IT, due at once -
    call vp_skfirst                 ; Unmute's seek (vp_unmfs), from there
    pop ax
    mov [vp_skb], ax
    mov [vp_sttgt], ax
    call vp_sknow
.x:
    pop dx
    pop bx
    pop ax
.out:
    ret

; vp_skkey - AL = +1 Right, FFh Left. Preserves all
vp_skkey:
    push ax
    push bx
    push dx
    cmp byte [vo_skp], 0
    jne .more
    call vp_skfirst                 ; THE FIRST PRESS
.more:
    mov word [vp_sttgt], 0xFFFF     ; (an arrow moves a step back's target)
    cbw
    add ax, [vp_skn]
    cmp ax, VP_SKMAXN
    jle .c1
    mov ax, VP_SKMAXN
.c1:
    cmp ax, -VP_SKMAXN
    jge .c2
    mov ax, -VP_SKMAXN
.c2:
    mov [vp_skn], ax
    call vp_ticks
    mov [vp_sktk], ax
    call vp_sktext
    pop dx
    pop bx
    pop ax
    ret

; vp_skfirst - a seek's FIRST PRESS (an arrow's, or an unmute's in the full
; screen): [vo_skp] 1, no presses yet, from the frame on the glass, the play
; paused - [vp_skwas] whether it was playing. Clobbers BX
vp_skfirst:
    mov byte [vo_skp], 1
    mov word [vp_stepq], 0          ; (no frame step through the seek, and no
    mov word [vp_sttgt], 0xFFFF     ; step back's frame but the one it sets)
    xor bx, bx
    mov [vp_skn], bx
    mov [vp_skwas], bl
    mov bx, [vp_done]
    or bx, bx
    jz .z
    dec bx
.z:
    mov [vp_skb], bx
    cmp byte [vp_upause], 0
    jne .r
    inc byte [vp_skwas]             ; (0 above: 1)
    push ax
    call vp_upaus
    pop ax
.r:
    ret

; vp_sknow - the seek on its way due at once: [vp_sktk] now less its wait.
; Clobbers AX, DX
vp_sknow:
    call vp_ticks
    mov dl, [vp_skwt]
    xor dh, dh
    sub ax, dx
    mov [vp_sktk], ax
    ret

; vp_skhome - Home: a seek to the start, due at once - the one Left makes,
; taken all the way back, with no wait for another press (98.3.14).
; Preserves all
vp_skhome:
    push ax
    push dx
    mov al, 0xFF
    call vp_skkey                   ; paused, and a seek on its way...
    mov word [vp_skn], -VP_SKMAXN   ; ...all the way back: frame 0
    call vp_sktext
    call vp_sknow                   ; ...and its wait already over
    pop dx
    pop ax
    ret

; vp_sktarget - AX = the frame the presses so far point at: five seconds of
; frames each, from the frame on the glass, inside the file
vp_sktarget:
    push bx
    push cx
    push dx
    mov ax, [vp_rate]
    mov cx, 5
    mul cx                          ; 5 x the rate, over the samples a frame
    mov bx, [vp_spf]
    mov cx, bx
    shr cx, 1
    add ax, cx
    adc dx, 0
    cmp dx, bx
    jae .big
    div bx
    jmp short .f
.big:
    mov ax, 0xFFFF
.f:
    mov cx, ax                      ; CX = five seconds of frames
    mov ax, [vp_skn]
    or ax, ax
    jns .fw
    neg ax                          ; BACK: to 0 at the least
    mul cx
    or dx, dx
    jnz .zero
    mov bx, [vp_skb]
    sub bx, ax
    jb .zero
    mov ax, bx
    jmp short .out
.zero:
    xor ax, ax
    jmp short .out
.fw:
    mul cx                          ; ON: to the last frame at the most
    or dx, dx
    jnz .last
    add ax, [vp_skb]
    jc .last
    cmp ax, [vp_frames]
    jb .out
.last:
    mov ax, [vp_frames]
    dec ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; vp_sktext - the text the box shows while the seek waits: "<< 1:05", ">>
; 2:10", or the time alone once the presses cancel out
vp_sktext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, vp_skbuf
    mov ax, [vp_skn]
    or ax, ax
    jz .t
    mov al, '>'
    jns .d
    mov al, '<'
.d:
    mov [di], al
    mov [di+1], al
    mov byte [di+2], ' '
    add di, 3
.t:
    call vp_sktarget
    call vo_time
    mov al, VOK_SEEK
    mov ah, 1
    mov si, vp_skbuf
    call vo_show
    jmp vp_ld6

; vp_skdue - the main loop's: a seek whose wait is over is made. out: CF=1
; it was - the play starts from its key (vp_main's .first); CF=0 nothing to
; do, or it came to nothing and the play is as it was. Clobbers AX, BX, CX,
; DX, SI, DI, ES
vp_skdue:
    cmp byte [vo_skp], 0
    jne .p
    clc
    ret
.p:
    call OSAPI_GET_TICKS
    sub ax, [vp_sktk]
    cmp al, [vp_skwt]               ; (ticks: under 256, and the wait is 9)
    jae .go
    clc
    ret
.go:
    mov byte [vo_skp], 0
    cmp byte [vp_skhere], 0         ; UNMUTED (98.3.17): to the key at or
    jne .here                       ; before the frame on the glass
    cmp word [vp_skn], 0            ; the presses cancelled out
    je .back
.here:
    cmp word [vp_nkeys], 0          ; a file with no keyframes has nowhere
    je .nokey                       ; to start but its first
    call vp_sktarget
    call vp_keyat                   ; AX = the key at or before it
    jc .back
    mov [vp_skk], ax
    mov ax, [vp_kekb]               ; its entry - and ON, the first key past
    call OSAPI_MEM_CLAIM            ; the frame on the glass
    jc .back
    mov [vp_rdseg], dx
    mov ax, [vp_skk]
    call vp_kent
    jc .fr
    cmp byte [vp_skhere], 0
    jne .ok
    cmp word [vp_skn], 0
    jl .ok
    mov ax, [vp_ke+KE_K]
    cmp ax, [vp_skb]
    ja .ok
    mov ax, [vp_skk]                ; (the target is before the next key:
    inc ax                          ; that key, or nothing past the last)
    cmp ax, [vp_nkeys]
    jae .fr
    mov [vp_skk], ax
    call vp_kent
    jc .fr
.ok:
    mov byte [vp_skhere], 0
    mov dx, [vp_rdseg]
    call OSAPI_MEM_FREE
    mov ax, [vp_skk]
    call vp_fseek
    jc .back
    mov al, [vp_skwas]              ; on from the key, playing if it was
    mov [vp_skgo], al
    stc
    ret
.fr:
    mov dx, [vp_rdseg]
    call OSAPI_MEM_FREE
    jmp short .back
.nokey:
    mov al, [vp_nkwhy]              ; WHY (98.3.14): the file has no keys, or
    call vo_toastk                  ; keys past one read on this volume
.back:
    cmp byte [vp_snd], 0            ; AN UNMUTE's seek come to nothing
    je .bk                          ; (98.3.17): its ring claimed and no card
    cmp byte [vp_sopn], 0           ; opened or deferred - the resume would
    jne .bk                         ; run a clock nothing plays. Silent on
    cmp byte [vp_sdefer], 0         ; the PIT instead, as a refused open is
    jne .bk
    call vp_sndoff
    mov al, VOK_SNDNX
    call vo_toastk
.bk:
    mov byte [vp_skhere], 0
    mov word [vp_sttgt], 0xFFFF     ; (a step back come to nothing: . again)
    call vo_update                  ; the seek's text off
    cmp byte [vp_skwas], 0
    je .no
    call vp_upaus                   ; ...and playing again, where it was
.no:
    clc
    ret

; vp_fseek - AX = a key whose entry is in hand ([vp_ke]): the session moved
; there, in the bracket - the card closed (the resume opens it at the key),
; the canvas black, and the reader, the hook and the clock at the key
; (vp_spos). CF=1 it could not: nothing moved, or, if the key's record could
; not be read, the play ends with the reason
vp_fseek:
    mov [vp_sel], ax
    cmp byte [vp_resid], 0          ; RESIDENT: the key's record wants a
    je .st                          ; claim of its own, if the play had none
    cmp word [vp_ring], 0
    jne .st
    mov ax, [vp_kbkb]
    call vp_mclaim
    jc .no
    mov [vp_ring], dx
    mov [vp_rclm], dx
.st:
    cmp byte [vp_sopn], 0
    je .c
    call vp_sclose                  ; the card stops, and lets go of the ring
.c:
    mov byte [vp_nseam], 0
    call vp_cclear
    mov dx, [vp_ring]
    call vp_spos
    jnc .ok
    mov ax, [vp_msg]
    mov [vp_errmsg], ax
    mov al, 1
    mov [vp_err], al
    mov [vp_end], al
    dec ax
    mov [vp_skwas], al
    jmp short .no
.ok:
    call vp_paldac                  ; (the key's sixteen, the canvas black)
    mov byte [vp_startp], 1         ; .first: paused on the key's picture,
    clc                             ; the card from there
    ret
.no:
    stc
    ret

; vp_wsurf - the window's rect as this bracket's surface: the desktop's own
; framebuffer and layout, the picture's place in the box as the origin
vp_wsurf:
    push ax
    push bx
    call vp_dinfo
    mov ax, [vp_dseg]
    mov [vp_vseg], ax
    mov al, [vp_dlay]
    mov [vp_tlay], al
    cmp al, [vp_layout]             ; the file's own layout: decoded onto the
    mov al, 0                       ; desktop in place; another's: through the
    je .n                           ; shadow, and copied (98.3.2)
    inc ax
.n:
    mov [vp_shadow], al
    call vp_track
    call vp_boxxy
    mov ax, [vp_py]
    mov [vp_ty0], ax
    push ax
    mov ax, [vp_px]
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [vp_tx0], ax
    pop ax
    mov bl, [vp_tlay]
    call vp_rowaddr
    add ax, [vp_tx0]
    mov [vp_org], ax
    pop bx
    pop ax
    ret

; vp_c16sync - C160 (98.3.12.1): the shadow is let go stale while the decode
; writes the screen, so before anything reads it - the text going up, the
; bracket ending - the canvas is read back off the screen's odd addresses.
; ~8,000 bytes once, where the copy it replaced was every dirty row every
; frame. Preserves all
vp_c16sync:
    cmp byte [vp_c16st], 0
    je .ret
    mov byte [vp_c16st], 0
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    push es
    mov es, [vp_shseg]
    mov si, [vp_org]
    shl si, 1
    inc si                          ; the canvas's first attribute
    xor di, di                      ; ...into the shadow at its own 0
    mov bx, [vp_h]
    mov ax, [vp_wb]
    mov ds, [vp_vseg]
    cld
.r:
    push si
    mov cx, ax
.c:
    movsb
    inc si
    loop .c
    pop si
    add si, 160                     ; the next row's, on screen...
    add di, 80                      ; ...and in the packed image, whose
    sub di, ax                      ; row the loop has already crossed
    dec bx
    jnz .r
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.ret:
    ret

; vp_kput / vp_kget - the canvas between the keeper and the surface (98.3.7).
; Through the shadow the keeper IS the shadow: put is a copy of all of it,
; get is nothing. Onto the screen in place, the canvas's rows at the origin
vp_kput:
    mov word [vp_kpo], 0
    cmp byte [vp_shadow], 0
    je .native
    mov word [vp_dy0], 0
    push ax
    mov ax, [vp_h]
    mov [vp_dy1], ax
    pop ax
    jmp vp_blit
.native:
    push ax
    xor al, al
    call vp_kmove
    cmp byte [vp_flip], 0           ; ...onto both pages when flipping,
    je .one                         ; or all N (98.3.8.4)
    push cx
    mov cl, [vp_npg]
    or cl, cl
    jnz .kn
    mov cl, 2
.kn:
    dec cl
.kl:
    push ax
    mov ax, [vp_kpo]
    add ax, [vp_page]
    mov [vp_kpo], ax
    pop ax
    call vp_kmove
    dec cl
    jnz .kl
    pop cx
    mov word [vp_kpo], 0
.one:
    pop ax
    ret

; vp_kheld - CF=0 when the glass already holds the key a play starts from,
; exactly where the play draws it (98.3.7.1): in the window, decoded in
; place (no shadow, no pages), a one-bit file, and the box's picture that
; key's at its own size - the poster rule places it on the play's rows, and
; vp_srun repaints it there after a drag. Preserves all
vp_kheld:
    push ax
    cmp byte [vp_winm], 0
    je .no
    cmp byte [vp_shadow], 0
    jne .no
    cmp byte [vp_flip], 0
    jne .no
    cmp byte [vp_pixfmt], PF_VGA8   ; (MONO1, CGACOMP: the poster at its own
    jae .no                         ; size IS the canvas's bytes - unless it
    cmp byte [vp_pthm], 0           ; is a stored picture, 98.1.3.11)
    jne .no
    cmp word [vp_pseg], 0
    je .no
    cmp word [vp_pscale], 1
    jne .no
    mov ax, [vp_dkey]
    cmp ax, [vp_kload]
    jne .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

vp_kget:
    cmp byte [vp_shadow], 0
    jne vp_c16sync                  ; (C160's shadow may be stale: 98.3.12.1)
    push ax
    mov ax, [vp_foff]               ; off the page on the glass
    mov [vp_kpo], ax
    mov al, 1
    call vp_kmove
    pop ax
.out:
    ret

vp_kmove:                           ; AL = 0 keeper -> screen, 1 screen -> keeper
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [vp_kdir], al
    cld
    mov ax, [vp_keep]
    or ax, ax                       ; NO KEEPER (98.3.19): a put is black on
    jnz .kh                         ; the screen, and a get has nowhere to go
    cmp byte [vp_nokeep], 2         ; ...unless the POSTER stands in for it
    jne .nk2                        ; (98.3.19.3)
    cmp byte [vp_kdir], 0
    jne .pg
    cmp byte [vp_pcv], 0            ; (no frame in it yet: black, below)
    je .nk2
    call vp_pcput
    jmp .d
.pg:
    call vp_pcget
    jmp .d
.nk2:
    cmp byte [vp_kdir], 0
    jne .d
    cmp byte [vp_planar], 0         ; (all four planes at once)
    je .zr
    call vp_mxall
.zr:
    xor dx, dx
.zl:
    cmp dx, [vp_h]
    jae .d
    mov ax, dx
    mov bl, [vp_layout]
    call vp_rowaddr                 ; AX = the row, in the canvas's layout
    mov di, ax
    add di, [vp_org]
    add di, [vp_kpo]
    mov cx, [vp_wb]
    mov es, [vp_vseg]
    xor al, al
    rep stosb
    inc dx
    jmp short .zl
.kh:
    mov [vp_kseg], ax
    cmp byte [vp_planar], 0
    je .one
    xor cx, cx                      ; PLANES: one at a time, the Map Mask
.pl:                                ; for a put and the Read Map for a get,
    call vp_mxsel                   ; into the keeper's planes plsp apart
    push cx
    call .rows
    pop cx
    mov ax, [vp_plsp]
    add [vp_kseg], ax
    inc cx
    cmp cx, 4
    jb .pl
    call vp_mxall
    jmp short .d
.one:
    call .rows
    jmp short .d
.rows:
    xor dx, dx                      ; DX = the row
.r:
    cmp dx, [vp_h]
    jae .rd
    mov ax, dx
    mov bl, [vp_layout]
    call vp_rowaddr                 ; AX = the row, in the keeper
    mov si, ax
    mov di, ax
    add di, [vp_org]                ; ...and on the screen, on the page it
    add di, [vp_kpo]                ; is moved to or from (98.3.8)
    mov cx, [vp_wb]
    mov ax, [vp_kseg]
    mov bx, [vp_vseg]
    cmp byte [vp_kdir], 0
    jne .get
    mov es, bx
    push ds
    mov ds, ax
    rep movsb
    pop ds
    jmp short .n
.get:
    xchg si, di
    mov es, ax
    push ds
    mov ds, bx
    rep movsb
    pop ds
.n:
    inc dx
    jmp short .r
.rd:
    ret
.d:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_mxsel - CL = a MODEX plane: the Map Mask (writes) and the Read Map
; (reads) both on it. vp_mxall - writes to all four again, which is what the
; decoder's 0Fh sub-records and the mode set assume. Preserve all
vp_mxsel:
    push ax
    push dx
    mov ah, 1
    shl ah, cl
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    mov ah, cl
    mov al, 4
    mov dx, 0x3CE
    out dx, ax
    pop dx
    pop ax
    ret
vp_mxall:
    push ax
    push dx
    mov ax, 0x0F02
    mov dx, 0x3C4
    out dx, ax
    pop dx
    pop ax
    ret

; vp_wthumb - in the window, the thumb follows the play (98.3.7): when its
; offset moves, the block goes from where it was to where it is, written into
; the desktop's framebuffer here - the kernel's drawing is not the bracket's to
; use - so a move is at most four bytes a row, not a bar
vp_wthumb:
    cmp byte [vp_winm], 0
    je .out
    push ax
    mov ax, [vp_done]               ; nothing to ask until a frame is drawn
    cmp ax, [vp_wtk]
    je .p
    mov [vp_wtk], ax
    call vp_thumbx
    cmp ax, [vp_wtx]
    je .p
    push bx
    mov bx, [vp_wtx]
    mov [vp_wtx], ax
    add ax, [vp_tx1]
    add bx, [vp_tx1]
    call vp_wmove
    pop bx
.p:
    pop ax
.out:
    ret

; vp_wmove - the thumb's block from screen x BX to screen x AX, over the bar's
; inside rows, on the desktop's framebuffer ([vp_dseg], laid out as
; [vp_dlay]). ONE STORE A BYTE, of its final value: the old block made white
; and the new one black in two passes put the columns they share through
; white and back, and that was the flicker (98.3.7). So the bytes either block
; touches - at most four, found once - each carry a mask (the bits that
; change) and the white bits within it, and every row writes each byte once.
; A 1 bpp desktop merges; mode 12h stores through the Bit Mask after a read
; loads the latches, the data's 1s white and 0s black. Preserves all
vp_wmove:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [vp_wnx], ax
    mov [vp_wox], bx
    mov byte [vp_wmn], 0
    mov cx, bx
    call .two                       ; the old block's bytes...
    mov cx, [vp_wnx]
    call .two                       ; ...and the new one's
    mov es, [vp_dseg]
    mov si, [vp_cy0]
    add si, [vp_lbary]
    inc si                          ; SI = the first row inside the bar
    mov cx, VP_BARH - 2
.row:
    push cx
    mov ax, si
    mov bl, [vp_dlay]
    call vp_rowaddr
    mov bp, ax                      ; BP = the row's start
    mov bx, vp_wmt
    mov cl, [vp_wmn]
    xor ch, ch
    jcxz .rn                        ; (a move always changes a bit; defensive)
.e:
    mov di, bp
    add di, [bx]
    mov ax, [bx+2]                  ; AL = the mask, AH = its white bits
    pushf                           ; A BYTE at a time with IF = 0 - its read
    cli                             ; and write (and the Bit Mask) are one step
    cmp byte [vp_dlay], 2           ; against the pointer's ISR - and no more:
                                    ; a sample ISR (SPEC.md 34.11) loses a
                                    ; pulse to every 864 cycles held
    je .ev
    not al
    and al, [es:di]
    or al, ah
    mov [es:di], al
    jmp short .en
.ev:
    mov dx, 0x3CE
    push ax
    mov ah, al
    mov al, 8                       ; the Bit Mask
    out dx, ax
    pop ax
    mov al, [es:di]                 ; the latches, all four planes
    mov [es:di], ah
.en:
    popf
    add bx, 4
    loop .e
.rn:
    pop cx
    inc si
    loop .row
    cmp byte [vp_dlay], 2           ; mode 12h: every bit writable again
    jne .d
    mov dx, 0x3CE
    mov ax, 0xFF08
    out dx, ax
.d:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.two:                               ; CX = a block's x: its two bytes
    shr cx, 1
    shr cx, 1
    shr cx, 1
    call .one
    inc cx
.one:                               ; CX = a byte: into vp_wmt, once
    mov si, vp_wmt
    mov dl, [vp_wmn]
    xor dh, dh
.dup:
    or dx, dx
    jz .new
    cmp [si], cx
    je .r
    add si, 4
    dec dx
    jmp short .dup
.new:                               ; SI = the free entry
    mov ax, [vp_wnx]
    call .bm
    mov bl, dl                      ; BL = the new block's bits (black)
    mov ax, [vp_wox]
    call .bm                        ; DL = the old one's
    mov al, bl
    not al
    and dl, al                      ; DL = the old's the new does not cover
    or bl, dl                       ; BL = every bit that changes
    jz .r
    mov [si], cx
    mov [si+2], bl
    mov [si+3], dl
    inc byte [vp_wmn]
.r:
    ret
.bm:                                ; AX = a block's x, CX = a byte: DL =
    push ax                         ; the block's bits in it
    push cx
    mov dx, ax
    shr dx, 1
    shr dx, 1
    shr dx, 1                       ; DX = the block's first byte
    and ax, 7
    xchg ax, cx                     ; CL = x & 7, AX = the byte asked about
    mov ch, 0xFF
    shr ch, cl                      ; CH = its bits in the first byte
    cmp ax, dx
    je .b0
    inc dx
    cmp ax, dx
    mov dl, 0
    jne .bo
    mov dl, ch
    not dl                          ; ...and the rest in the next
    jmp short .bo
.b0:
    mov dl, ch
.bo:
    pop cx
    pop ax
    ret

; vp_winv - the Repeat button's interior turned over on the desktop's
; framebuffer: its up look is a white ground with the picture in black and
; its down look the reverse (os88ui_bdraw), so an XOR one pixel in from the
; frame is the other. A 1 bpp desktop takes it byte by byte; mode 12h
; through the Graphics Controller's XOR function, the Bit Mask, and a read
; that loads the latches. Preserves all
vp_winv:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    pushf
    push ds                         ; the button's rect, [vp_wvr]'s
    pop es
    mov si, [vp_wvr]
    mov di, vp_wvq
    mov cx, 4
    cld
    rep movsw
    cli
    mov es, [vp_dseg]
    cmp byte [vp_dlay], 2
    jne .g
    mov dx, 0x3CE                   ; the GC's function: XOR
    mov ax, 0x1803
    out dx, ax
.g:
    mov si, [vp_wvq + 2]
    inc si                          ; SI = the first row inside
.row:
    cmp si, [vp_wvq + 6]
    jae .d
    mov ax, si
    mov bl, [vp_dlay]
    call vp_rowaddr
    mov bp, ax                      ; BP = the row's start
    mov cx, [vp_wvq]
    inc cx                          ; CX = the first x inside...
.x:
    cmp cx, [vp_wvq + 4]
    jae .nr
    mov di, cx                      ; ...its byte, and the bits from it to
    shr di, 1                       ; the byte's end or the inside's
    shr di, 1
    shr di, 1
    add di, bp
    mov al, 0xFF
    push cx
    and cl, 7
    shr al, cl                      ; AL = x's bit and those after it
    pop cx
    mov dx, cx
    or dx, 7
    inc dx                          ; DX = the next byte's first x
    mov ah, 0xFF
    mov bx, [vp_wvq + 4] ; the inside ends before x2: the bits
    cmp bx, dx                      ; from x2 on are kept
    jae .m
    push cx
    mov cl, bl
    and cl, 7
    shr ah, cl
    not ah
    pop cx
    mov dx, bx
.m:
    and al, ah
    cmp byte [vp_dlay], 2
    je .vga
    xor [es:di], al
    jmp short .nx
.vga:
    push dx
    mov ah, al
    mov al, 8                       ; the Bit Mask
    mov dx, 0x3CE
    out dx, ax
    pop dx
    mov al, [es:di]                 ; the latches
    mov byte [es:di], 0xFF          ; ...XOR'd with every plane's 1s
.nx:
    mov cx, dx
    jmp short .x
.nr:
    inc si
    jmp short .row
.d:
    cmp byte [vp_dlay], 2           ; mode 12h: a plain store, every bit
    jne .o
    mov dx, 0x3CE
    mov ax, 0x0003
    out dx, ax
    mov ax, 0xFF08
    out dx, ax
.o:
    popf
    jmp vp_ld8

; -----------------------------------------------------------------------------
; vp_sopen - the sound (SPEC.md 98.3.1): the audio cursor at the stream's
; start, the ring filled as far as it goes, then the card started on it.
; Refused, the play is silent and paced by FSXF_RATE as before
; -----------------------------------------------------------------------------
vp_sopen:
    push ds
    pop es
    xor ax, ax
    mov di, vp_szero                ; the play's sound counters, all zero
    mov cx, VP_SZERO / 2
    cld
    rep stosw
    mov ax, [vp_abase]              ; the audio cursor is vp_acur's, at the
    mov [vp_aseq], ax               ; frame the clock reads until the card's
    mov [vp_syncf], ax              ; first block says otherwise
    mov ax, [vp_afr0]
    mov [vp_afr], ax
    mov byte [vp_afn], 0x80         ; PCM8's silence
    call vp_sblk                    ; the card's block, [vp_blk]
    mov ax, [vp_blk]                ; frames the clock may run on past the
    xor dx, dx                      ; card's last word: one block's worth,
    div word [vp_rbytes]            ; and two more
    add ax, 2
    mov [vp_acap], ax
    mov es, [vp_aseg]
    xor ax, ax
    mov [es:VP_RL+SND_EXT_TOTAL], ax
    mov [es:VP_RL+SND_EXT_CONS], ax
    mov bl, SND_OPENF_RING + SND_OPENF_EXT + (VP_RLCODE << SND_OPENF_RLSH)
    or bl, [vp_bflg]                ; ...and its block (34.5.3)
    mov al, [vp_aref]               ; THE PLAYER'S DECODER (98.3.17.1) where
    xor ah, ah                      ; the card's would start: the reference
    mov [vp_sref], ax               ; byte and a scale of 0 - the state the
    mov [vp_sscl], ah               ; encoder steers to at a key
    cmp byte [vp_sadp], 0           ; ...and then the card is PCM8's, with
    jne .pf                         ; no reference byte in its ring
    cmp byte [vp_audio], 2
    jne .pf
    or bl, SND_OPENF_ADPCM4 + SND_OPENF_FORCE   ; ADPCM4 (FORCE: on a DSP
                                    ; 4.xx it was the user's call to unmute,
                                    ; 98.3.17): stream byte 0 is the card's
    mov al, [vp_aref]               ; reference - 80h, or a keyframe's
    mov [es:0], al                  ; (98.3.5) - and the silence is a
    mov ax, 1                       ; nibble of no change
    mov [vp_atot], ax
    mov [vp_a0], ax
    dec ax
    mov [vp_afn], al
.pf:
    mov [vp_sflag], bl
    mov cx, [vp_alst]               ; SOUND AHEAD (98.1.8): the lead vp_lstage
    jcxz .pfl                       ; staged in the ring's tail goes first -
    mov bl, [vp_ahead]              ; frames base .. base + A - 1, one piece
    mov si, VP_RL                   ; each (the speaker's shaper sees them as
    sub si, cx                      ; it sees a record's)
.al:
    push bx
    push si
    mov dx, [vp_aseg]
    mov cx, [vp_abytes]
    call vp_aput
    pop si
    pop bx
    add si, [vp_abytes]
    inc word [vp_afr]
    inc word [vp_aseq]
    dec bl
    jnz .al
    mov word [vp_alst], 0
.pfl:
    mov ax, [vp_atot]
    push ax
    call vp_afill
    pop ax
    cmp ax, [vp_atot]
    jne .pfl
    cmp word [vp_atot], VP_BLOCK    ; the card starts on a whole block: a clip
    jae .open                       ; shorter than one is silence past its end
    mov cx, VP_BLOCK
    sub cx, [vp_atot]
    xor dx, dx
    call vp_aput
.open:
    cmp byte [vp_snd], VP_SPK       ; THE SPEAKER: from here, in this bracket
    jne .card                       ; (98.3.15)
    call os88spk_go
    jnc .op
.fail:
    mov byte [vp_snd], 0            ; the card said no: a silent play
    ret
.card:
    xor al, al                      ; verb 0: open, the ring in our claim
    mov ah, [vp_sflag]
    xor si, si
    mov di, [vp_aseg]
    mov cx, [vp_atot]
    mov dx, [vp_rate]
    call vp_sstrm
    or al, al
    jnz .fail
    mov [vp_hand], ah
.op:
    mov byte [vp_sopn], 1
    ret

; =============================================================================
; MUTE (SPEC.md 98.3.17): a play with NO sound run at all - no ring claimed,
; no card stream, no speaker ISR - so the machine is the picture's. The
; user's to choose with M, S, the button or the menu, from anywhere; and
; the DEFAULT where this machine may not play the file's sound: ADPCM4 on a
; card whose DSP answers 4.xx (SND_CAP_ADPCM4Q - Creative's SB16 dropped the
; command, some compatibles did not), or PCM8 through an 8088's speaker past
; VP_SPKMAX. Unmuted, those are tried anyway, which is the point
; =============================================================================

; vp_mdet - a file just opened: [vp_mwhy] why its sound should default to
; silence (0 it should not), and [vp_mute] that or the user's own mute.
; Preserves all
vp_mdet:
    push ax
    push bx
    push dx
    call vp_scaps
    xor bl, bl                      ; BL = why, into [vp_mwhy] at .set
    cmp byte [vp_audio], 2
    jne .p8
    test ax, SND_CAP_PCM_BG         ; ADPCM4 on the card that answers 4.xx
    jz .set
    test ax, SND_CAP_ADPCM4Q
    jz .set
    cmp byte [vp_tier], CPU_286     ; (a 286 decodes it itself: 98.3.17.1)
    jae .set
    mov bl, 1
    jmp short .set
.p8:
    cmp byte [vp_audio], 1          ; PCM8 through an 8088's speaker, faster
    jne .set                        ; than it keeps up with (34.11.4)
    test ax, SND_CAP_PCM_BG
    jnz .set
    cmp byte [vp_tier], CPU_8086
    jne .set
    test ax, SND_CAP_LPTDAC         ; (A COVOX plays one entry a sample
    jnz .rate                       ; whatever the file's P: 98.3.15.1)
    cmp byte [vp_spkp], 1           ; (two pulses a sample are ~96% of an
    ja .fast                        ; 8088: 34.11.7.1)
.rate:
    cmp word [vp_rate], VP_SPKMAX
    jbe .set
.fast:
    mov bl, 2
.set:
    mov [vp_mwhy], bl
    mov al, bl
    or al, [vp_umute]
    jz .m
    mov al, 1
.m:
    mov [vp_mute], al
    pop dx
    pop bx
    pop ax
    ret

; vp_mutetog - M, S, the Mute button or the menu, off a bracket: MUTED from
; now, a play's sound off where it is; or unmuted, the next play has it and
; a session under way starts again from the key at or before where it is,
; its sound with it - playing if it was. Lock held
vp_mutetog:
    cmp byte [vp_ok], 1
    jne .ret
    cmp byte [vp_audio], 0          ; (nothing to hear, nothing to mute)
    je .ret
    push ax
    xor byte [vp_mute], 1
    mov al, [vp_mute]
    mov [vp_umute], al
    cmp byte [vp_sess], 0
    je .show
    or al, al
    jz .on
    call vp_sndoff
    jmp short .show
.on:
    mov ah, [vp_lrun]               ; (a LIVE play running: on again)
    push ax
    mov al, 1
    call vp_stopfor
    pop ax
    or ah, ah
    jz .show
    call vp_play
.show:
    call vp_fmt
    call vp_repaint
    pop ax
.ret:
    ret

; vp_sndoff - the sound off NOW, the play going on silent on the PIT: the
; card's stream or the speaker closed, and its ring given back. Preserves all
vp_sndoff:
    cmp byte [vp_snd], 0
    je .ret
    push ax
    push dx
    cmp byte [vp_sopn], 0
    je .c
    call vp_sclose
.c:
    xor dx, dx
    mov [vp_snd], dl                ; (the hook reads it: before the free)
    mov [vp_sdefer], dl             ; ...and a card deferred to the first
    mov [vp_owed], dx               ; Space is not started on a freed ring
    xchg dx, [vp_aseg]
    or dx, dx
    jz .p
    call vp_mfree
.p:
    pop dx
    pop ax
.ret:
    ret

; vp_sndprep - the sound this play will have (SPEC.md 98.3.1, 98.3.15): its
; ring claimed and [vp_snd] = 1 the card's stream, VP_SPK the speaker (or
; the Covox, os88spk_lpt naming its port: 98.3.15.1) - or
; 0 none: no audio, MUTED, told to be silent, or no room. The card is not
; opened here. Clobbers AX, CX, DX
vp_sndprep:
    xor ax, ax
    mov [vp_snd], al
    mov [vp_sadp], al               ; the card decodes, unless (98.3.17.1)
    mov ax, [vp_abytes]             ; ...and the ring's bytes a frame are
    mov [vp_rbytes], ax             ; the file's
    cmp byte [vp_audio], 0
    je .ret
    cmp byte [vp_nosnd], 0
    jne .ret
    cmp byte [vp_mute], 0           ; MUTED: nothing claimed, nothing run
    jne .ret
    push bx
    call vp_scaps
    pop bx
    test ax, SND_CAP_PCM_BG
    jz .spk
    cmp byte [vp_audio], 2          ; ADPCM4 on a DSP 4.xx, on a 286 or
    jne .hw                         ; better: THE PLAYER decodes it, into a
    test ax, SND_CAP_ADPCM4Q        ; PCM8 stream of two samples a file
    jz .hw                          ; byte (98.3.17.1)
    cmp byte [vp_tier], CPU_286
    jb .hw
    mov byte [vp_sadp], 1
    shl word [vp_rbytes], 1
.hw:
    mov ax, VP_RL / 1024 + 1        ; A CARD: the ring and its two control
    mov cx, ax                      ; words, page-safe and never moved
    call OSAPI_MEM_CLAIM_DMA_HI
    jc .ret                         ; no room for it: the play is silent
    mov [vp_aseg], dx
    mov byte [vp_snd], 1
.ret:
    ret
.spk:                               ; NO CARD: THE SPEAKER (98.3.15) - PCM8,
    cmp byte [vp_livem], 0          ; not Live (the desktop cannot give up
    jne .ret                        ; channel 0). The ring the card would
    cmp byte [vp_audio], 1          ; read, and the table its counts are
    jne .ret                        ; made through
    call os88spk_lptq               ; ...OR A COVOX (98.3.15.1, 34.14): DX =
    jnc .lpt                        ; its port when the Sound page's tier is
    xor dx, dx                      ; the Covox, else 0 and the speaker -
.lpt:                               ; asked per play, before os88spk_init
    call os88spk_lpt                ; builds the table for it
    call vp_ringkb                  ; (RL + 272 of it: the table follows
    call vp_mclaim                  ; the control words - and the shaper's
    jc .ret                         ; family after it, for a card's clip)
    mov [vp_aseg], dx
    push di
    mov di, dx
    mov ah, VP_RLCODE << SND_OPENF_RLSH
    mov dx, [vp_rate]
    push cx
    mov cl, [vp_spkp]               ; pulses a sample (98.1.1.3.1)
    call os88spk_init
    pop cx
    pop di
    jc .spkno                       ; a rate it cannot: silent
    mov byte [vp_snd], VP_SPK
    cmp byte [vp_spkpwm], 0         ; A CLIP MADE FOR A CARD is SHAPED for the
    jne .shp                        ; speaker here (34.11.9), as the encoder
    push ax                         ; shapes one made for it: a straight wave
    push di                         ; is the carrier and nothing else on a
    push es                         ; 5150 (98.2.15.1) - its family in the
    mov es, [vp_aseg]               ; ring's claim, so a card machine never
    mov di, VP_RL + SPKFX_RINGX     ; holds it (34.11.9)
    mov ax, 0x0100 | SPKFX_PRE_DIFF
    call os88spkfx_init
    pop es
    pop di
    pop ax
.shp:
    ret
.spkno:
    xor dx, dx
    xchg dx, [vp_aseg]
    call vp_mfree
    ret

; vp_ringkb - AX = the KB the sound ring claims, or will: RL, its control
; words and the speaker's count table - and the shaper's level family after
; them when the speaker shapes a card's clip (SPEC.md 34.11.9). Preserves all
; but AX
vp_ringkb:
    cmp byte [vp_spkpwm], 0         ; a file of counts never shapes
    jne .small
    cmp word [vp_aseg], 0
    je .ask
    cmp byte [vp_snd], VP_SPK
    jne .small
.big:
    mov ax, VP_RL / 1024 + (SPKFX_RINGEND + 1023) / 1024
    ret
.ask:                               ; not claimed yet: the speaker's, if
    cmp byte [vp_livem], 0          ; there is no card (vp_sndprep's test)
    jne .small
    push bx
    push dx
    call OSAPI_SND_CAPS
    pop dx
    pop bx
    test ax, SND_CAP_PCM_BG
    jz .big
.small:
    mov ax, VP_RL / 1024 + 1
    ret

; vp_unmfs - UNMUTED in the full screen: the sound claimed, and the play
; moved to the key at or before the frame on the glass - a seek that lands
; where it is (98.3.14) - so the card or the speaker starts in step with it.
; With no key to go to, the sound is the next play's. Preserves all
vp_unmfs:
    push ax
    push bx
    push cx
    push dx
    cmp word [vp_nkeys], 0
    je .next
    cmp byte [vo_skp], 0            ; a seek on its way lands with it
    jne .prep
    mov byte [vp_skhere], 1
    call vp_skfirst                 ; PAUSED FIRST, while there is no sound
    call vp_sknow                   ; for the pause to touch - and due at once
.prep:
    cmp byte [vp_snd], 0
    jne .say
    call vp_sndprep
    mov al, VOK_LOWMEM              ; (no room for its ring)
    cmp byte [vp_snd], 0
    je .t
.say:
    mov al, VOK_SNDON
    jmp short .t
.next:
    mov al, VOK_SNDNX
.t:
    call vo_toastk
    jmp vp_ld4

; vp_spkinfo - the info line's word on the sound (98.3.15, 98.3.17), at DI:
; MUTED and why, or with no card, the speaker - or the Covox, when the Sound
; page's tier is (98.3.15.1). Preserves all
vp_spkinfo:
    cmp byte [vp_audio], 0
    je .r
    push ax
    push bx
    push dx
    push si
    cmp byte [vp_mute], 0
    je .on
    mov al, [vp_mwhy]               ; MUTED, and why: the user, the card
    cbw                             ; that may not decode it, too fast for
    xchg ax, bx                     ; this one's speaker (34.11.4), or the
    shl bx, 1                       ; bank wanted the CPU (98.3.18.9)
    mov si, [vp_mwnames+bx]
    jmp short .p
.on:
    call vp_scaps
    test ax, SND_CAP_PCM_BG
    jnz .o                          ; a card plays it
    cmp byte [vp_audio], 1
    jne .o
    mov si, vp_s_spkon
    test ax, SND_CAP_LPTDAC         ; ...or the Covox (98.3.15.1)
    jz .p
    mov si, vp_s_cvx
.p:
    call vp_puts
.o:
    pop si
    pop dx
    pop bx
    pop ax
.r:
    ret

; vp_sclose - the sound closed, the card's stream or the speaker's
vp_sclose:
    mov byte [vp_sopn], 0
    cmp byte [vp_snd], VP_SPK
    jne .card
    jmp os88spk_stop
.card:
    mov al, 2
    mov ah, [vp_hand]
    call vp_sstrm                   ; (a far cell: called, never jumped to)
    ret

; vp_skeep - a stream the card paused for want of data resumes the moment a
; whole block is queued again (SPEC.md 34.5.2's contract: verb 1 resumes it)
; vp_acur - the audio cursor where the video's is now - past a keyframe's
; skip - and [vp_abase] the frame it is at. Before vp_sopen; and, going in
; paused (98.3.6), before the first frame is drawn, so the card starts on
; that frame's sound while that frame is on the screen
vp_acur:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, vp_pc
    mov di, va_pc
    mov cx, VP_CURW
    cld
    rep movsw
    mov cx, [vp_vseq]               ; the clock counts every lap (98.3.9)
    mov [vp_abase], cx
    mov cx, [vp_done]               ; ...and the frame the cursor is at: going
    mov [vp_afr0], cx               ; in paused, one is drawn after this
    pop es
    pop di
    pop si
    pop cx
    ret

; vp_sblk - THE CARD'S BLOCK (SPEC.md 98.3.1, 34.5.3): 2,048 bytes, or on a
; driver that takes SND_OPENF_BLKSH halved (up to three times) while it is
; longer than 25 ms of sound - 256 at 11 kHz PCM8 and at 5.5 kHz, 512 at
; 22 kHz - so the card never interrupts much more than 43 times a second
; (864 cycles each on a 4.77 MHz 8088: under 1%), and the reader need keep
; only that much sound ahead of the picture. PCM8 past 22,222 Hz keeps its
; block: the driver's is 4,096 there on some cards and 2,048 on others.
; [vp_blk] the bytes, [vp_bflg] the open flag's bits. Clobbers AX, CX
vp_sblk:
    mov word [vp_blk], VP_BLOCK
    mov byte [vp_bflg], 0
    cmp byte [vp_snd], 1            ; the card's only
    jne .r
    push bx
    push dx
    call vp_scaps
    test al, SND_CAP_EXTBLK
    jz .x                           ; an older driver: 2,048, and no bits
    mov ax, [vp_rate]               ; AX = the bytes a second
    cmp byte [vp_sadp], 0           ; (decoded by the player: PCM8's)
    jne .b0
    cmp byte [vp_audio], 2
    je .a
.b0:
    cmp ax, VP_HALFHI               ; PCM8 past 22,222 Hz: as it is
    ja .x
    jmp short .b
.a:
    shr ax, 1                       ; (ADPCM4: two samples a byte)
.b:
    xor dx, dx
    mov cx, VP_BLKDIV
    div cx                          ; AX = the bytes in 25 ms
    mov dx, VP_BLOCK
    xor cx, cx
.l:
    cmp dx, ax
    jbe .d
    cmp cl, 3
    je .d
    inc cx
    shr dx, 1
    jmp short .l
.d:
    shr word [vp_blk], cl
    ror cl, 1                       ; the code into bits 6-7
    ror cl, 1
    mov [vp_bflg], cl
.x:
    pop dx
    pop bx
.r:
    ret

vp_skeep:
    cmp byte [vp_snd], 1            ; the card's alone: the speaker takes more
    jne .out                        ; the moment it is queued, and never ends
    cmp byte [vp_upause], 0         ; a pause is not an underrun to resume
    jne .out
    mov al, 3
    mov ah, [vp_hand]
    call OSAPI_SND_STREAM           ; AX = state, DX = consumed
    cmp ax, 2                       ; ENDED: the card stopped interrupting and
    jne .und                        ; the driver's watchdog gave up on it. The
    mov byte [vp_snd], 0            ; picture goes on, silent, on the PIT -
    mov word [vp_owed], 0           ; never held for a clock that has gone
    inc word [vp_pause]
    ret
.und:
    cmp ax, 1
    jne .out
%ifdef VP_DIAG
    push dx                         ; THE DIAGNOSTIC (98.3): a pause is
    cmp byte [vp_pin], 0            ; timed from the pass that first sees
    jne .pi                         ; it, and called the STREAM's when the
    mov byte [vp_pin], 1            ; sound was waiting on an unread record
    call OSAPI_GET_TICKS            ; at any pass while it lasted
    mov [vp_pt0], ax
.pi:
    cmp byte [vp_astv], 0           ; (bit 1: the stream's, seen)
    je .pn
    or byte [vp_pin], 2
.pn:
    pop dx
%endif
    mov ax, [vp_atot]
    sub ax, dx
    cmp ax, [vp_blk]
    jb .out
    mov al, 1
    mov ah, [vp_hand]
    mov cx, [vp_atot]
    call OSAPI_SND_STREAM
    inc word [vp_pause]
%ifdef VP_DIAG
    test byte [vp_pin], 2           ; (the stream's)
    jz .pc
    inc word [vp_pstrm]
.pc:
    mov byte [vp_pin], 0
    call OSAPI_GET_TICKS
    sub ax, [vp_pt0]
    cmp ax, [vp_pmax]
    jbe .out
    mov [vp_pmax], ax
    mov ax, [vp_done]
    mov [vp_pmaxf], ax
%endif
.out:
    ret

; -----------------------------------------------------------------------------
; vp_upaus - Space (98.3.4): pause, or resume. Paused, the hook returns at
; once - nothing owed, no periods counted - so the clock stands where the
; picture stands; the card, if there is one, is halted where it is (verb 10,
; SPEC.md 34.5.4) and verb 1 starts it again. The ticks paused are not play
; time
; -----------------------------------------------------------------------------
vp_upaus:
    cmp byte [vp_upause], 0
    jne .resume
    mov byte [vp_pfresh], 1         ; the hook's next call is the pause's
    mov byte [vp_upause], 1         ; first - set BEFORE the pause, so a call
                                    ; between the two stores plays its
                                    ; periods and the flag waits for the next.
                                    ; One store each: the hook reads at IF = 0
    call vp_ticks
    mov [vp_ptk0], ax
    cmp byte [vp_snd], 0
    je .out
    cmp byte [vp_snd], VP_SPK       ; the speaker stops where it is, and costs
    jne .cp                         ; nothing while it does
    call os88spk_stop
    ret
.cp:
    mov al, SND_V_PAUSE
    mov ah, [vp_hand]
    call vp_sstrm
.out:
    ret
.resume:
    mov word [vp_stepq], 0          ; (frame steps not drawn yet: none)
    call vp_ticks
    sub ax, [vp_ptk0]
    add [vp_ptk], ax
    cmp byte [vp_sdefer], 0         ; in paused (98.3.6): the card starts now,
    je .v1                          ; at the frame the picture holds
    mov byte [vp_sdefer], 0
    call vp_sopen
    jmp short .go
.v1:
    cmp byte [vp_snd], 0
    je .go
    cmp byte [vp_snd], VP_SPK       ; the speaker, in THIS bracket - the last
    jne .v1c                        ; one's end closed it
    call os88spk_go
    jnc .go
    mov byte [vp_snd], 0            ; refused: silent, on the PIT
    mov word [vp_owed], 0
    jmp short .go
.v1c:
    mov al, 1                       ; the card first: it resumes where it
    mov ah, [vp_hand]               ; stopped, and the clock extrapolates
    mov cx, [vp_atot]               ; from its last word as before
    call vp_sstrm
.go:
    mov byte [vp_pfresh], 0         ; (a pause no call saw: nothing pending)
    mov byte [vp_upause], 0
    ret

; -----------------------------------------------------------------------------
; vp_fill - read the next chunk into its slot, if the hook has left it
; out: CF=0 one arrived; CF=1 none could be read now (ring full, or the end)
; -----------------------------------------------------------------------------
; vp_lfloor - BX = the lowest chunk a cursor still reads: the hook's, or with
; SOUND AHEAD the audio cursor's when that is lower - its record may sit up to
; A - 1 behind the picture's (98.1.8), where without it the picture never
; overtakes the audio cursor (98.3.1) and the hook's chunk is the answer.
; An audio cursor that has queued the LAST frame's sound reads nothing more
; (vp_afill pads from there) and is parked A records short of the end, so
; it no longer counts - or the picture's last A records, wider than the
; ring on a small K or a heavy ending, would never be read
vp_lfloor:
    mov bx, [vp_pc]
    cmp byte [vp_ahead], 0
    je .r
    cmp byte [vp_snd], 0
    je .r
    cmp byte [vp_rep], 0            ; (repeating, it reads on: 98.3.9)
    jne .lo
    push ax
    mov ax, [vp_afr]
    cmp ax, [vp_frames]
    pop ax
    jae .r
.lo:
    cmp bx, [va_pc]
    jbe .r
    mov bx, [va_pc]
.r:
    ret

; vp_lstage - SOUND AHEAD (98.1.8): the lead vp_spos read with the key, or
; the start's, copied out of the ring - which the stream is about to fill
; over it - into the sound ring's TAIL, where vp_sopen queues it. A muted
; play stages none. Clobbers AX, CX, SI, DI
vp_lstage:
    xor ax, ax
    mov [vp_alst], ax
    mov si, [vp_lsrc]
    dec ax
    cmp si, ax
    je .r
    mov [vp_lsrc], ax
    cmp byte [vp_snd], 0
    je .r
    mov cx, [vp_alb]
    mov [vp_alst], cx
    push ds
    push es
    mov ax, si
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    and si, 15
    mov di, VP_RL
    sub di, cx
    mov es, [vp_aseg]
    add ax, [vp_ring]
    add ax, [vp_lpar]               ; (a lead apart, 98.1.8.1)
    mov ds, ax
    cld
    rep movsb
    pop es
    pop ds
.r:
    ret

vp_fill:
    cmp byte [vp_resid], 0          ; RESIDENT: nothing to read (98.1.7)
    jne .none
    cmp byte [vp_eof], 0
    jne .eof
    mov ax, [vp_lc]                 ; the chunk to read
    call vp_lfloor                  ; the lowest chunk still read: its slot
    add bx, [vp_kr]                 ; and every one after it are still live
    cmp ax, bx
    jae .none
    push ax
    call vp_slot
    xchg bx, ax                     ; its slot
    pop ax
    cmp byte [vp_einp], 0           ; IN PLACE (98.3.18.7): into the board's
    je .conv                        ; pages, which ARE the ring
    call vp_eread
    jc .ioerr
    call vp_dtime                   ; (a disk read: timed, 98.1.9.3 - it was
    jmp .got                        ; not, so a layer was never timed here)
.conv:
    cmp byte [vp_xip], 0            ; HELD, IN PLACE (98.3.18.11): nothing
    je .cv                          ; is copied - the chunk's offset is noted
    call vp_dtneed                  ; ...unless THE DISK's verdict is owed
    jc vp_xfill1                    ; (98.1.9.7): then it is READ too, timed
    jmp vp_xfillt                   ; and thrown away (98.3.18.14)
.cv:
    mov cl, 11
    shl bx, cl
    add bx, [vp_ring]
    mov dx, bx                      ; DX:BX = the slot, ES:DI = the cursor
    xor bx, bx
    call vp_dtneed                  ; THE DISK's verdict still owed: off the
    jnc .disk                       ; disk, held or not (98.1.9.7)
    call vp_xfill                   ; HELD (98.3.18): a copy, AX = the bytes
    jnc .got
.disk:
    push dx                         ; (the slot, and where in the file it
    push word [vp_cur+FSEQ_OFF+2]   ; starts: vp_xput's)
    push word [vp_cur+FSEQ_OFF]
    mov cx, VP_CHUNK
    push ds
    pop es
    mov di, vp_cur
    mov si, vp_name
    call OSAPI_FILE_READ_SEQ
    pop bx
    pop di
    pop es
    jnc .dsk
.ioerr:
    mov byte [vp_err], 1
    mov word [vp_errmsg], vp_s_io
    mov byte [vp_end], 1
    stc
    ret
.dsk:
%ifdef VP_RDLOG
    call vp_prlog                   ; THE READ LOG (98.3.22.2): DI:BX where
%endif                              ; in the file, AX the bytes, ES:0 them
    call vp_dtime                   ; (the ring's fill timed, 98.1.9.3)
    mov cx, ax                      ; ...and behind the hold, if that is where
    mov ax, bx                      ; the hold ends
    mov dx, di
    call vp_xput
    mov ax, cx
.got:
..@vp_fillgot:
    cmp ax, VP_CHUNK                ; ZF=0: the stream's last, short chunk -
    pushf                           ; said after [vp_lc] has it (vp_nextw)
    cmp byte [vp_einp], 0           ; (in place no mirror: the hook's window
    jne .pub                        ; is three pages, wherever they are)
    cmp byte [vp_xip], 0            ; (nor held in place: the hold is the
    jne .pub                        ; file, contiguous)
    mov ax, [vp_lc]
    call vp_slot
    cmp ax, [vp_msl]                ; slot 0 - and 1, BIGSP's two mirror slots
    jae .pub                        ; (98.1.4.1)
    call vp_mneed                   ; ...is copied to the MIRROR, so a
    jcxz .pub                       ; super-packet starting in slot K-1 runs
    push ds                         ; on into contiguous memory - as much of
    mov dx, cx                      ; it as that super-packet runs on into
    mov cl, 11
    shl ax, cl
    add ax, [vp_ring]               ; the slot...
    mov bx, [vp_k]
    shl bx, cl
    add bx, ax                      ; ...and its mirror, K slots on
    mov es, bx
    mov ds, ax
    xor si, si
    xor di, di
    mov cx, dx
    inc cx
    shr cx, 1
    cld
    rep movsw
    pop ds
.pub:
    inc word [vp_lc]                ; ...and published LAST
    popf
    je .full
    mov byte [vp_eof], 1            ; ...and then the end: a hook that saw it
.full:                              ; first would call the last chunk missing
    clc
    ret
.eof:                               ; THE FILE'S END, REPEATING (98.3.9): the
    cmp byte [vp_rep], 0            ; next lap's start is read on behind it -
    je .none                        ; once the video has taken the last seam
    mov ax, [vp_pgen]               ; armed, there being one set of them
    cmp ax, [vp_wgen]
    jne .none
    jmp vp_warm
.none:
    stc
    ret

; vp_mneed - chunk [vp_lc] just read into slot 0: CX = the bytes of it the
; MIRROR needs (98.3). Only a super-packet that starts in the chunk before
; it and runs on into it reads the mirror, so the chain is walked from the
; hook's super-packet - every header on it is in a chunk already loaded -
; to the one that crosses into [vp_lc], and what it runs on is the answer:
; 0 when none does, VP_CHUNK when the walk meets the chain's end (a seam may
; follow) or anything it cannot size. It was the whole 32 KB every time, 1.8%
; of a 5150 streaming off XT-IDE with K = 8 and 8% with K = 2 (VIDEO-PLAN
; 15.8). Preserves all but CX
vp_mneed:
    push ax
    push bx
    push dx
    push si
    push di
    push bp
    mov dx, [vp_pc]                 ; DX:BX = a super-packet's start, as a
    mov bx, [vp_po]                 ; chunk and an offset into it, and CX its
    mov cx, [vp_psec]               ; sectors
    mov bp, 1024                    ; (a bound on the walk)
.w:
    cmp dx, [vp_lc]                 ; at or past the chunk just read: nothing
    jae .none                       ; before it runs on into it
    jcxz .all                       ; the chain's end
    cmp cx, [vp_spcap]
    ja .all
    dec bp
    jz .all
    push cx                         ; ITS HEADER's next: the one after it
    push dx
    push bx
    mov ax, dx
    call vp_addr                    ; DX:SI = the super-packet
    push ds
    mov ds, dx
    mov di, [si+2]
    pop ds
    pop bx
    pop dx
    pop cx
    mov ax, dx                      ; its end...
    call vp_spend
    mov dx, ax
    mov cx, di                      ; the next one's sectors, at its start
    cmp dx, [vp_lc]
    jb .w
    ja .all                         ; ...right through it (BIGSP): all of it
    mov cx, bx                      ; it started before [vp_lc] and runs BX
    jmp short .out                  ; bytes into it
.none:
    xor cx, cx
    jmp short .out
.all:
    mov cx, VP_CHUNK
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; vp_warm - arm the SEAM (98.3.9) at the ring's next chunk: the record that
; joins this lap to the next - the file's seam, or keyframe 0 - read in there
; whole, and the reader moved to the super-packet of the frame after it, as
; a play from a keyframe starts (98.3.5). CF=0 armed; CF=1 not yet (the ring
; has no room for the read), or the read failed, which ends the play
vp_warm:
    cmp byte [vp_einp], 0           ; (IN PLACE: no Repeat - its seam and
    je .w0                          ; its key are read into the conventional
    stc                             ; ring, which the board's slots are not;
    ret                             ; the play ends at the file's end)
.w0:
    mov al, [vp_lkind]
    mov [vp_wkind], al
    xor cx, cx                      ; CX = the chunks the read takes: its KB
    or al, al                       ; in 32s, rounded up - at most 64 KB, two
    jz .room
    mov cx, [vp_lkb]
    cmp al, 1
    je .kb
    mov cx, [vp_kbkb]
.kb:
    add cx, 31
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
.room:
    mov ax, [vp_lc]                 ; every chunk of it free
    add ax, cx
    call vp_lfloor
    add bx, [vp_kr]
    cmp ax, bx
    jbe .go
    stc
    ret
.go:
    push cx
    mov ax, [vp_sp0]                ; kind 0: the stream from its start, onto
    mov [vp_wpc], ax                ; black (vp_wpc and vp_wpo hold the
    mov ax, [vp_sp0+2]              ; super-packet's offset for now)
    mov [vp_wpo], ax
    mov ax, [vp_sp0n]
    mov [vp_wpsec], ax
    mov word [vp_widx], 0
    mov word [vp_wL], 0xFFFF
    cmp byte [vp_wkind], 0
    je .cont
    mov ax, [vp_lc]                 ; the read goes to the chunk's slot, and
    call vp_slot                    ; on into the next or the mirror: two
    mov cl, 11                      ; slots from any are contiguous
    shl ax, cl
    add ax, [vp_ring]
    mov [vp_rdseg], ax
    cmp byte [vp_wkind], 1
    jne .key
    mov ax, [vp_lL]                 ; THE SEAM RECORD
    mov [vp_wL], ax
    mov ax, [vp_lsp]
    mov [vp_wpc], ax
    mov ax, [vp_lsp+2]
    mov [vp_wpo], ax
    mov al, [vp_lsecs]
    xor ah, ah
    mov [vp_wpsec], ax
    mov al, [vp_lidx]
    mov [vp_widx], ax
    mov ax, [vp_loff]
    mov dx, [vp_loff+2]
    mov cx, [vp_llen]
    jmp short .rd
.key:
    xor ax, ax                      ; KEYFRAME 0: its entry, then its record
    call vp_kent
    jc .bad
    cmp byte [vp_ke+KE_SECS], 0     ; a key on the last frame has nothing
    je .k0                          ; after it: the stream's start instead
    mov ax, [vp_ke+KE_K]
    mov [vp_wL], ax
    mov ax, [vp_ke+KE_SP]
    mov [vp_wpc], ax
    mov ax, [vp_ke+KE_SP+2]
    mov [vp_wpo], ax
    mov al, [vp_ke+KE_SECS]
    xor ah, ah
    mov [vp_wpsec], ax
    mov al, [vp_ke+KE_IDX]
    mov [vp_widx], ax
    mov ax, [vp_ke+KE_OFF]
    mov dx, [vp_ke+KE_OFF+2]
    mov cx, [vp_ke+KE_LEN]
.rd:
    mov [vp_wlen], cx
    call vp_rdat                    ; SI = where it landed
    jc .bad
    cmp byte [vp_wkind], 2          ; A KEY IN PARTS (98.1.3.8) is more
    jne .rd1                        ; than the hook can decode at a join:
    push ds                         ; the lap goes on from the stream's
    mov ds, [vp_rdseg]              ; start, on black, as one with no key
    test byte [si+3], 0x80
    pop ds
    jz .rd1
    mov ax, [vp_sp0]                ; (kind 0's from the start: .key set
    mov [vp_wpc], ax                ; key 0's super-packet and frame)
    mov ax, [vp_sp0+2]
    mov [vp_wpo], ax
    mov ax, [vp_sp0n]
    mov [vp_wpsec], ax
    xor ax, ax
    mov [vp_widx], ax
    dec ax
    mov [vp_wL], ax
    jmp .k0
.rd1:
    mov ax, [vp_lc]
    mov [vp_wsc], ax
    mov [vp_wso], si
    jmp short .cont
.k0:
    mov byte [vp_wkind], 0
.cont:
    ; the reader from the cluster under that super-packet, as vp_sstart's
    call vp_bflush                  ; (the bank's chunks are the lap's end)
    mov ax, [vp_clb]
    dec ax
    mov bx, [vp_wpc]
    and bx, ax                      ; BX = how far it is into that cluster
    push ds
    pop es
    mov di, vp_cur
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    mov ax, [vp_wpc]
    sub ax, bx
    mov [vp_cur+FSEQ_OFF], ax
    mov ax, [vp_wpo]
    mov [vp_cur+FSEQ_OFF+2], ax
    pop cx
    add [vp_lc], cx                 ; past the seam's chunks
    mov ax, [vp_lc]
    mov [vp_wpc], ax                ; the cursor after it: that chunk, BX in
    mov [vp_wpo], bx
    mov byte [vp_eof], 0
    inc word [vp_wgen]              ; ...and ARMED, last
    clc
    ret
.bad:
    pop cx
    mov byte [vp_err], 1
    mov word [vp_errmsg], vp_s_kbad
    mov byte [vp_end], 1
    stc
    ret

; vp_rsay - a play whose ring is short of the stream's (98.1.1) says so in
; the full screen, once: a burst the encoder banked for may pause it.
; Preserves all
vp_rsay:
    cmp byte [vp_rshort], 0
    je .ret
    mov byte [vp_rshort], 0
    push ax
    mov al, VOK_LOWMEM
    call vo_toastk
    pop ax
.ret:
    ret

; =============================================================================
; vp_hook - FSXF_RATE's hook (SPEC.md 53.2.2, 98.3)
; in:  AX = periods since the last call; DS = ours; IF=0
; SILENT, a frame is due every [vp_pitper] periods. At most two are drawn a
; call - one due and one owed - and the rest are counted LATE and forgiven: a
; delta frame cannot be skipped, so falling behind is paid for by the picture
; running slow, never by a wrong picture.
; WITH SOUND (SPEC.md 98.3.1) the CARD is the clock: the frames due are the
; frames whose audio it has played, as of its last block interrupt, plus the
; periods since - capped at a block's worth, so a card that stops holds the
; picture too. Then the ring is topped up from the audio cursor.
; =============================================================================
vp_hook:
    cld                             ; DF is the interrupted code's (vp_nextw)
    add [vp_hper], ax               ; (the bracket's clock, 98.1.9.3)
    cmp byte [vp_ready], 0
    je .ret0
    cmp byte [vp_upause], 0         ; paused (98.3.4): the periods are not
    je .play                        ; the play's...
    cmp byte [vp_pfresh], 0         ; ...EXCEPT THE PAUSE'S FIRST CALL'S: AX
    je .step                        ; is every period since the last call,
    mov byte [vp_pfresh], 0         ; and those PLAYED - through the shadow a
    add [vp_pers], ax               ; call is a whole copy long, so Space
    cmp byte [vp_snd], 0            ; lands between two calls with 5 or 6
    jne .ret0                       ; periods pending, which were dropped and
    add [vp_owed], ax               ; the picture ran that far behind the
.ret0:                              ; clock (98.3.4). Owed, they are drawn on
    ret                             ; the resume's first call
.step:                              ; A FRAME STEP (98.3.14.1): one frame
    cmp word [vp_stepq], 0          ; drawn as a played one is, the clock
    je .ret0                        ; still paused - and with sound its audio
    dec word [vp_stepq]             ; queued, the card paused, so the frames
    cmp byte [vp_snd], 0            ; stepped are in step when it resumes
    je .st1                         ; - unless the card is not started yet
    cmp byte [vp_sdefer], 0         ; (a paused start or a seek's landing),
    jne .st1                        ; which starts at the frame on the glass
    sti                             ; (as .top calls it)
    call vp_afill
    cli
.st1:
    mov cx, 1
    jmp .n
    nop                             ; (never run: .play, every call's, on the
.play:                              ; word parity the hook had)
    add [vp_pers], ax
    cmp ax, [vp_gap]                ; the longest the hook was held off: a
    jbe .g                          ; picture late for THAT is the machine's,
    mov [vp_gap], ax                ; not the clock's
.g:
    cmp byte [vp_snd], 0
    jne .snd
    add [vp_owed], ax
    mov bl, [vp_pitper]
    xor bh, bh
    mov ax, [vp_owed]               ; CX = the frames due, in ONE divide: a
    xor dx, dx                      ; subtraction loop is a turn a frame owed,
    div bx                          ; at IF=0
    mov [vp_owed], dx
    mov cx, ax
    jcxz .ret0
    cmp cx, [vp_fcap]
    jbe .n
    sub cx, [vp_fcap]
    cmp byte [vp_shadow], 0
    jne .keep
    add [vp_late], cx               ; native: forgiven, the picture runs slow
    mov cx, [vp_fcap]
    jmp short .n
.keep:
    mov al, VP_SKIPMAX              ; SHADOW: the rest stays owed - up to
    mul byte [vp_fcap]              ; VP_SKIPMAX calls' worth, past which it
    cmp cx, ax                      ; is late as a native play's is, so a
    jbe .kp                         ; machine that never catches up does not
    sub cx, ax                      ; owe without end
    add [vp_late], cx
    mov cx, ax
.kp:
    mov ax, cx                      ; The copy is what costs, once a call
    mul bx                          ; however many frames it covers, so the
    add [vp_owed], ax               ; decode catches up and the DISPLAY rate
    mov cx, [vp_fcap]               ; is what drops
.n:
    call vp_lybh                    ; (two frames a call: the layer waits)
    sti                             ; a disk's completion is not held behind a
    push cx                         ; frame (SPEC.md 53.2.2 allows it)
    call vo_pre                     ; the text off the page it decodes into
    pop cx                          ; (98.3.13)
.f:
    push cx
    call vp_frame
    pop cx
    jc .stop
    loop .f
.stop:
    mov bl, [vp_pitper]             ; still a frame owed after them: behind
    xor bh, bh
    cmp [vp_owed], bx               ; (AL = 1 not below, 0 below: no branch)
    sbb al, al
    inc al
    call vp_blitck                  ; the shadow's band, once for them all
    call vo_post                    ; ...and the text back over it
    cli
    ret
.snd:                               ; --- the card's clock (SPEC.md 98.3.1)
    call vp_adue                    ; CX = frames due, [vp_due] set
    jcxz .top
    cmp cx, [vp_fcap]
    jbe .n2
    inc word [vp_late]              ; more behind the sound than a call draws
    mov cx, [vp_fcap]
.n2:
    call vp_lybh
    sti
    push cx
    call vo_pre
    pop cx
.f2:
    push cx
    call vp_frame
    pop cx
    jc .top
    loop .f2
.top:
    sti
    mov bx, [vp_due]                ; still frames due after them: behind
    cmp [vp_vseq], bx               ; (AL = 1 when vseq < due: no branch)
    sbb al, al
    neg al
    call vp_blitck
    call vo_post
    call vp_afill                   ; the audio a few frames ahead of the card
    cli
    ret

; vp_adue - the card's clock (SPEC.md 98.3.1): the frames the sound says are
; due, off what the card has consumed as of its last block interrupt and
; [vp_pers], the periods since - the bracket's hook counts them, and a LIVE
; play's worker (98.3.10) counts them off the ticks. out: [vp_due], and CX =
; the frames due past those drawn (0 = on time). Clobbers AX, BX, DX, ES
vp_adue:
    mov es, [vp_aseg]
    mov ax, [es:VP_RL+SND_EXT_CONS] ; what the card has consumed, as of its
    mov dx, ax                      ; last block interrupt
    sub ax, [vp_alast]
    jz .nosync
    mov [vp_alast], dx
    add [vp_aplay], ax
    adc word [vp_aplay+2], 0
    mov ax, [vp_aplay]
    mov dx, [vp_aplay+2]
    sub ax, [vp_a0]                 ; the reference byte is no frame's
    sbb dx, 0
    jc .neg
    cmp dx, [vp_rbytes]             ; a quotient past 16 bits: long over
    jae .big
    div word [vp_rbytes]            ; AX = frames wholly played
    jmp short .syn
.neg:
    xor ax, ax
    jmp short .syn
.big:
    mov ax, 0xFFF0
.syn:
    add ax, [vp_abase]              ; ...from the frame the card started at
    jnc .syb
    mov ax, 0xFFF0
.syb:
    mov [vp_syncf], ax
    mov ax, [vp_pers]
    mov [vp_syncp], ax
.nosync:
    mov ax, [vp_pers]               ; ...and the periods since, at most a
    sub ax, [vp_syncp]              ; block's worth of frames
    xor dx, dx
    mov bl, [vp_pitper]
    xor bh, bh
    div bx
    cmp ax, [vp_acap]
    jbe .cap
    mov ax, [vp_acap]
.cap:
    add ax, [vp_syncf]
    inc ax                          ; AX = the frames due, every lap counted
    cmp byte [vp_rep], 0            ; (98.3.9) - and never past this lap's
    jne .due2                       ; last, whose silence plays on after it,
    mov bx, [vp_frames]             ; unless it repeats
    sub bx, [vp_done]
    add bx, [vp_vseq]
    cmp ax, bx
    jbe .due2
    mov ax, bx
.due2:
    mov [vp_due], ax
    xor cx, cx
    sub ax, [vp_vseq]
    jbe .ret                        ; none: the picture is on time
    mov cx, ax
    cmp cx, [vp_skmax]
    jbe .ret
    mov [vp_skmax], cx
.ret:
    ret

; -----------------------------------------------------------------------------
; vp_blitck / vp_blit - the SHADOW's copy (SPEC.md 98.3.2): the canvas rows
; the frames since the last copy wrote, [vp_dy0, vp_dy1), from the file's
; layout to the screen's, a row at a time and each row re-addressed. Only the
; DISPLAY rate pays for it: the decode behind it has already run
; -----------------------------------------------------------------------------
; in: AL = 1 the play is behind. Then the copy waits and the call's time
; goes to the decode, which is what keeps the play in time - but never more
; than VP_SKIPMAX calls running, so a machine that can never catch up still
; sees its picture move
vp_blitck:
    cmp byte [vp_shadow], 0
    je .out
    mov bx, [vp_dy1]
    cmp bx, [vp_dy0]
    jbe .out
    or al, al
    jz .go
    cmp byte [vp_skipn], VP_SKIPMAX
    jae .go
    inc byte [vp_skipn]
    ret
.go:
    mov byte [vp_skipn], 0
    call vp_blit
.out:
    ret

vp_blit:
    cmp byte [vo_drawn], 0          ; the full screen's text up: the copy
    jne vp_blitr                    ; goes round it (98.3.13.1)
    mov cx, [vp_dy0]
    mov dx, [vp_dy1]
    sub dx, cx                      ; DX = the band's rows
    jbe .d
    call vp_rsfirst                 ; its first row, both ends
    mov es, [vp_vseg]
    push bp
    mov bp, [vb_s]                  ; BP = the shadow's row, BX the screen's,
    mov bx, [vb_t]                  ; AX the row's phase for the steps
    mov ax, cx
    and ax, 3
    shl ax, 1
    mov cx, [vp_wb]
    mov [vb_n], cx
    cmp byte [vp_tlay], LAY_C160
    push ds
    mov ds, [vp_shseg]
    cld
    je .c16
.r:
    mov si, bp
    mov di, bx
    mov cx, [cs:vb_n]
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    xchg ax, si                     ; the next row: a step on each side
    add bp, [cs:vb_sd+si]
    add bx, [cs:vb_td+si]
    add si, 2
    and si, 6
    xchg ax, si
    dec dx
    jnz .r
.dn:
    pop ds
    pop bp
.d:
    mov word [vp_dy0], 0xFFFF       ; the band is empty again
    mov word [vp_dy1], 0
    ret
.c16:                               ; C160 (98.3.12): each shadow byte the
    mov si, bp                      ; ATTRIBUTE of a cell, at every other
    mov di, bx                      ; address of a 160-byte row
    mov cx, [cs:vb_n]
.cb:
    movsb
    inc di
    loop .cb
    xchg ax, si
    add bp, [cs:vb_sd+si]
    add bx, [cs:vb_td+si]
    add si, 2
    and si, 6
    xchg ax, si
    dec dx
    jnz .c16
    jmp short .dn

; vp_rsfirst / vp_rsnext - THE COPY'S ROWS (98.3.2), for vp_blit and
; vp_blitr: [vb_s] the shadow's row in the file's layout, [vb_t] the
; screen's byte at the centred origin (C160's: the cell's attribute).
; vp_rowaddr's multiply and bank loop, twice a row, measured 25% of a 5150
; copying a Hercules file onto a CGA - more than the stores it placed - so
; it is taken once, for the band's first row, and each row after it is a
; STEP out of a table: the next bank's 8 KB on, or bank 0 a stride on. Every
; layout's banks divide four and the screen's origin row is a multiple of
; its banks, so ONE phase, the canvas row mod 4, indexes both tables.
; vp_rsfirst: CX = the canvas row. Preserves all
vp_rsfirst:
    push ax
    push bx
    push dx
    push si
    push di
    mov si, vb_s
    mov di, vb_sd
    mov ax, cx
    mov bl, [vp_layout]
    call .one
    mov si, vb_t
    mov di, vb_td
    mov ax, cx
    add ax, [vp_ty0]
    mov bl, [vp_tlay]
    call .one
    mov ax, [vp_tx0]
    add [vb_t], ax
    cmp byte [vp_tlay], LAY_C160    ; C160: the screen's are the cells'
    jne .x                          ; attributes, two bytes a cell
    shl word [vb_t], 1
    inc word [vb_t]
    mov si, vb_td
.dbl:
    shl word [si], 1
    add si, 2
    cmp si, vb_td + 8
    jb .dbl
.x:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret
.one:                               ; SI = the row's address, DI = its four
    push cx                         ; steps; AX = the row, BL = the layout
    push bx
    call vp_rowaddr
    mov [si], ax
    pop bx
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1                       ; (layout x 6)
    mov dl, [vp_laytab+bx+1]        ; the banks: 1, 2 or 4
    mov al, dl
    dec ax
    xor ah, ah
    mov cl, 13
    shl ax, cl
    neg ax
    add ax, [vp_laytab+bx+2]        ; AX = the last bank's step: a stride,
    dec dl                          ; less the other banks' 8 KB each
    xor bx, bx                      ; DL = the bank mask, BX the entry
    xor dh, dh                      ; DH = its row, + 1
.st:
    inc dh
    test dh, dl
    jz .w
    mov word [di+bx], 8192
    jmp short .n
.w:
    mov [di+bx], ax
.n:
    add bx, 2
    cmp bx, 8
    jb .st
    pop cx
    ret

; vp_rsnext - CX = the row just copied: [vb_s] and [vb_t] one row on.
; Preserves all
vp_rsnext:
    push ax
    push bx
    mov bx, cx
    and bx, 3
    shl bx, 1
    mov ax, [vb_sd+bx]
    add [vb_s], ax
    mov ax, [vb_td+bx]
    add [vb_t], ax
    pop bx
    pop ax
    ret

; vp_blitr - vp_blit while the full screen's text is up: each row in three
; parts, the box's columns skipped (vo_bparts). A copy with no text is the
; one rep store it always was
vp_blitr:
    mov es, [vp_vseg]
    mov dx, [vp_shseg]
    mov cx, [vp_dy0]
    cmp cx, [vp_dy1]
    jae .d
    call vp_rsfirst
.r:
    mov si, [vb_s]
    mov di, [vb_t]
    push cx
    call vo_bparts                  ; the row, round the full screen's text
    mov cx, [vb_n1]                 ; (98.3.13.1)
    call .part
    mov ax, [vb_n2]
    add si, ax
    cmp byte [vp_tlay], LAY_C160
    jne .s1
    shl ax, 1                       ; (C160: two screen bytes a cell)
.s1:
    add di, ax
    mov cx, [vb_n3]
    call .part
    pop cx
    call vp_rsnext
    inc cx
    cmp cx, [vp_dy1]
    jb .r
.d:
    jmp vp_blit.d
.part:                              ; CX units from the shadow at SI
    jcxz .pr
    push ds
    mov ds, dx
    cld
    cmp byte [cs:vp_tlay], LAY_C160
    je .pc
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    pop ds
    ret
.pc:
    movsb
    inc di
    loop .pc
    pop ds
.pr:
    ret

; vp_frame - draw the next frame. CF=1 it did not (stalled, held, ended)
; -----------------------------------------------------------------------------
; THE PLAY'S PROFILE (VPDIAG=1, docs/plans/LOADALL-PLAN.md L5): every frame's
; time on the machine, for the encoder's model to be fitted to - a profile
; priced from synthetic frames and a slack the owner found by raising --peak
; is what this replaces. PIT channel 2 runs free (mode 2, 65,536 counts,
; 0.838 us each) with the speaker's data line off, so a play with a CARD's
; sound - or none - can be timed to the count; a speaker play is not
; profiled (channel 2 is its sound). Per frame drawn, two words: from
; vp_frame's start (the layer's read and the record's fetch included) and
; from the record in hand (the draw: the decode, the flip's back page or
; page-ahead's two, the layer on top) to the frame drawn. A frame longer
; than 55 ms wraps; the host flags it. Saved as VPPROFn.DAT beside the
; player when the session stops: a 32-byte header (98.3's VPPR), then the
; words, frame 0 first.
; -----------------------------------------------------------------------------
%ifdef VP_DIAG
VP_PRH      equ 48                  ; a 48-byte header (version 2)...
%ifdef VP_RDLOG
VP_PRMAX    equ 11000               ; ...frames profiled at most (43 KB)...
VP_RLMAX    equ 2048                ; ...and READS logged at most, 8 bytes
                                    ; each (98.3.22.2) - 59 KB in all
%else
VP_PRMAX    equ 16000               ; ...frames profiled at most (62.5 KB)
VP_RLMAX    equ 0                   ; (no read log: VIDEOR's, VP_RDLOG)
%endif
%macro VP_PRDONE 0
    call vp_prmark
%endmacro
; vp_prpit - AX = channel 2's count, latched. Every other register kept
vp_prpit:
    pushf
    cli
    mov al, 0x80                    ; latch channel 2
    out 0x43, al
    in al, 0x42
    mov ah, al
    in al, 0x42
    popf
    xchg al, ah
    ret

vp_prt0:
    cmp word [vp_prseg], 0
    je .r
    push ax
    call vp_prpit
    mov [vp_prta], ax
    pop ax
.r:
    ret

vp_prt1:
    cmp word [vp_prseg], 0
    je .r
    push ax
    call vp_prpit
    mov [vp_prtb], ax
    pop ax
.r:
    ret

; vp_prmark - frame [vp_done] drawn: its two times, if it has a slot
vp_prmark:
    cmp word [vp_prseg], 0
    je .r
    push ax
    push bx
    push dx
    push es
    mov bx, [vp_done]
    cmp bx, [vp_prna]
    jae .o
    cmp bx, [vp_prn]
    jb .s
    lea ax, [bx + 1]
    mov [vp_prn], ax                ; (the most frames seen: the file's count
.s:                                 ; less any not drawn, or a seek's)
    call vp_prpit
    mov dx, [vp_prta]
    sub dx, ax                      ; a down-counter: before - after
    push dx
    mov dx, [vp_prtb]
    sub dx, ax
    shl bx, 1
    shl bx, 1
    mov es, [vp_prseg]
    mov [es:bx + VP_PRH + 2], dx    ; the draw (past the header)
    pop dx
    mov [es:bx + VP_PRH], dx        ; ...and the whole frame
.o:
    pop es
    pop dx
    pop bx
    pop ax
.r:
    ret

%ifdef VP_RDLOG
; vp_prlog - THE READ LOG (98.3.22.2): a read of AX bytes from file offset
; DI:BX landed at ES:0 - its offset, length and a sum of its whole words
; (each: the sum rotated left once, then the word added; an odd last byte
; left out) into the profile's next entry, while there is one. The host
; sums the file the same way, so a chunk the disk handed over wrong is
; named. Every register kept but the flags
vp_prlog:
    cmp word [vp_prseg], 0
    je .r
    cmp word [vp_prnl], VP_RLMAX
    jae .r
    inc word [vp_prnl]
    push cx
    push dx
    push si
    push ds
    mov cx, ax
    shr cx, 1
    push es
    pop ds
    xor si, si                      ; SI = where, DX = the sum
    xor dx, dx
    jcxz .sd
.w:
    rol dx, 1
    add dx, [si]
    inc si
    inc si
    loop .w
.sd:
    push cs
    pop ds
    push es
    mov si, [vp_prlp]
    add word [vp_prlp], 8
    mov es, [vp_prseg]
    mov [es:si], bx                 ; the offset
    mov [es:si + 2], di
    mov [es:si + 4], ax             ; the bytes
    mov [es:si + 6], dx             ; the sum
    pop es
    pop ds
    pop si
    pop dx
    pop cx
.r:
    ret

; vp_prbad - a damaged super-packet: the first one's where, into the header
; (the hook's cursor and the reader's), and the count. Every register kept
vp_prbad:
    inc word [vp_prbd]
    cmp word [vp_prbd], 1
    jne .r
    cmp word [vp_prseg], 0
    je .r
    push ax
    push di
    push es
    mov es, [vp_prseg]
    mov di, 38
    mov ax, [vp_pc + VC_PC]
    stosw
    mov ax, [vp_pc + VC_PO]
    stosw
    mov ax, [vp_pc + VC_PSEC]
    stosw
    mov ax, [vp_lc]
    stosw
    mov ax, [vp_kr]
    stosw
    pop es
    pop di
    pop ax
.r:
    ret
%endif

; vp_prbeg - a session starts: the words' claim, zeroed - before the rings
; are sized, or a machine with no XMS has no room for it after (vp_prgo
; runs the clock, or gives the claim back to the speaker)
vp_prbeg:
    push ax
    push cx
    push dx
    push di
    push es
    cmp word [vp_prseg], 0
    jne .out                        ; (a session already profiled, unsaved)
    mov word [vp_prn], 0
    mov word [vp_prnl], 0
    mov word [vp_prbd], 0
    mov ax, [vp_frames]
    cmp ax, VP_PRMAX
    jbe .n
    mov ax, VP_PRMAX
.n:
    mov [vp_prna], ax
    shl ax, 1                       ; four bytes a frame, the header and the
    shl ax, 1                       ; read log, in whole KB
%ifdef VP_RDLOG
    push ax
    add ax, VP_PRH
    mov [vp_prlp], ax               ; (the log's first entry: after them)
    pop ax
%endif
    add ax, VP_PRH + 8 * VP_RLMAX + VP_DIL + 1023
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM
    jc .out
    mov [vp_prseg], dx
    mov es, dx
    xor di, di
    mov cx, ax
    mov ax, cx                      ; KB x 512 words
    mov cl, 9
    shl ax, cl
    mov cx, ax
    xor ax, ax
    cld
    rep stosw
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; vp_prgo - the session's clock is set: channel 2 run free for the profile
; - unless the speaker plays it (98.3.15), whose clock channel 2 is: the
; claim given back. Clobbers AX
vp_prgo:
    cmp word [vp_prseg], 0
    je .r
    cmp byte [vp_snd], VP_SPK
    jne .go
    push dx
    mov dx, [vp_prseg]
    call OSAPI_MEM_FREE
    mov word [vp_prseg], 0
    pop dx
.r:
    ret
.go:
    in al, 0x61
    mov [vp_pr61], al
    and al, 0xFC                    ; the speaker's data off...
    or al, 1                        ; ...channel 2's gate on
    out 0x61, al
    mov al, 0xB4                    ; channel 2, both bytes, mode 2, binary
    out 0x43, al
    xor al, al
    out 0x42, al
    out 0x42, al                    ; 65,536: free running
    ret

; vp_prend - the session stopped: the profile written as VPPROFn.DAT (the
; first n from 1 not on the disk) and its claim given back. Every register
; kept
vp_prend:
    cmp word [vp_prseg], 0
    jne .go
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [vp_pr61]               ; the gate as it was
    out 0x61, al
    mov es, [vp_prseg]              ; THE HEADER, in the claim's first 48
    mov word [es:0], 'VP'           ; (+38 on, a damaged super-packet's
    mov word [es:2], 'PR'           ; where, vp_prbad's, already there)
    mov word [es:4], 3              ; version 3 (the card's trailer, VPDI)
    mov ax, [vp_prn]
    mov [es:6], ax
    mov ax, [vp_pitdiv]
    mov [es:8], ax
    mov al, [vp_pitper0]
    mov ah, [vp_snd]
    mov [es:10], ax
    mov al, [vp_flip]
    mov ah, [vp_einp]
    mov [es:12], ax
    mov al, [vp_xip]
    mov ah, [vp_lbk]
    mov [es:14], ax
    mov si, vp_name
    mov di, 16
    mov cx, 13
    cld
    rep movsb
    mov ax, [vp_late]
    stosw                           ; +29
    mov di, 32
    mov ax, [vp_prna]               ; +32 the slots, +34 the reads, +36 the
    stosw                           ; damaged super-packets
    mov ax, [vp_prnl]
    stosw
    mov ax, [vp_prbd]
    stosw
    ; THE CARD'S TRAILER (98.3.22.3), after the words and the reads: 'VPDI',
    ; its bytes, its words' count, then every number the info card draws
    ; and where this boot put the play (vp_ditab), the package's CS and SS
    mov di, [vp_prna]
    shl di, 1
    shl di, 1
    mov ax, [vp_prnl]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add di, ax
    add di, VP_PRH
    mov ax, 'VP'
    stosw
    mov ax, 'DI'
    stosw
    mov ax, VP_DIL
    stosw
    mov ax, VP_DIN
    stosw
    mov si, vp_ditab
    mov cx, VP_DIN - 2
.dw:
    lodsw
    xchg bx, ax
    mov ax, [bx]
    stosw
    loop .dw
    mov ax, cs
    stosw
    mov ax, ss
    stosw
    mov al, '1'
.try:
    mov [vp_prfn + 6], al
    push ax
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov si, vp_prfn
    call OSAPI_FILE_READ            ; zero bytes: is it there?
    xchg bx, ax                     ; (BX = the refusal)
    pop ax
    jnc .next                       ; (an empty one: there)
    cmp bx, FERR_NOENT              ; ONLY no such file is free: one that is
    je .free                        ; there refuses FERR_BIG (a read into no
.next:                              ; room, 20.14.6.3), and that refusal
    inc al                          ; once overwrote VPPROF1 every play
    cmp al, '9'
    jbe .try
    jmp short .drop                 ; nine already: none written
.free:
    mov cx, [vp_prna]               ; the header, every frame slot, and the
    shl cx, 1                       ; reads logged after them, and the
    shl cx, 1                       ; card's trailer
    add cx, VP_PRH + VP_DIL
    mov ax, [vp_prnl]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add cx, ax
    xor dx, dx
    mov es, [vp_prseg]
    xor bx, bx
    mov si, vp_prfn
    call OSAPI_FILE_WRITE           ; the header and the words
.drop:
    mov dx, [vp_prseg]
    call OSAPI_MEM_FREE
    mov word [vp_prseg], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%else
%macro VP_PRDONE 0
%endmacro
%endif

vp_frame:
    cmp byte [vp_end], 0
    jne .no
    mov ax, [vp_done]
    cmp ax, [vp_stopat]             ; the gate's hold (tests/vidplay.py):
    jne .snd                        ; stop BEFORE this frame, the screen being
    mov byte [vp_held], 1           ; the host's frame [vp_stopat]-1
.no:
    stc
    ret
.snd:
    cmp byte [vp_snd], 0            ; WITH SOUND, never a picture whose audio
    je .go                          ; is not in the ring yet: the audio cursor
    cmp byte [vp_sdefer], 0         ; (the card not started yet - a FRAME
    jne .go                         ; STEP, paused: it starts where they end)
    cmp byte [vp_rep], 0            ; is what keeps the chunks under it, and
    jne .sq                         ; the video may not overtake it - which
    cmp ax, [vp_frames]             ; is why the header's count is the end
    jae .theend                     ; here, the chain's 0 never being reached
.sq:                                ; (repeating, the seam is next: 98.3.9)
    mov ax, [vp_vseq]
    cmp ax, [vp_aseq]
    jae .no
.go:
%ifdef VP_DIAG
    call vp_prt0                    ; (THE PLAY'S PROFILE: the frame begins)
%endif
    call vp_lyready                 ; THE LAYER'S RECORD READ FIRST (98.1.9)
    jc .stall
    mov bx, vp_pc
    call vp_next                    ; DX:SI = the record, CX = its len
    jnc .dec
    or al, al
    jz .stall
    cmp al, 1
    je .theend
    cmp al, 2
    jne .badrec
%ifdef VP_RDLOG
    call vp_prbad
%endif
    mov word [vp_errmsg], vp_s_badsp
    jmp short .err
.badrec:
    mov word [vp_errmsg], vp_s_badrec
.err:
    mov byte [vp_err], 1
    mov byte [vp_end], 1
    stc
    ret
.stall:                             ; (the ends, here in a short jump's
    inc word [vp_stall]             ; reach of the frame's tests: each was a
    stc                             ; jump over a jump, taken every frame)
    ret
.theend:
    mov byte [vp_end], 1
    stc
    ret
.dec:
%ifdef VP_DIAG
    call vp_prt1                    ; (...and its draw)
%endif
    inc word [vp_vseq]
    cmp byte [vp_nseam], 0
    jne .seam
    cmp byte [vp_flip], 0           ; (a FLIPPED frame's sixteen go in with
    jne .flip                       ; its flip, 98.3.8.3)
    call vp_palev                   ; (a scene's sixteen, if one starts here)
.dr:
    call vp_decrec
    call vp_lydec                   ; THE LAYER on top of it (98.1.9)
    VP_PRDONE
    inc word [vp_done]
    clc
    ret
.flip:
    cmp byte [vp_flip], 2           ; PAGE-AHEAD (98.3.8.3)
    jne .fd
    cmp byte [vp_fpg], 0            ; ...FLIPPED AND BUILT AHEAD (98.3.8.5)
    jne .fpd
    call vp_pgdec
    call vp_lydec                   ; (the layer on the page shown: 98.2.1.8.3)
    VP_PRDONE
    inc word [vp_done]
    clc
    ret
.fpd:
    call vp_fpdec
    jmp short .fdw
.fd:
    call vp_flipdec
.fdw:
    call vp_palevw                  ; (in the retrace the flip latched in)
    VP_PRDONE
    inc word [vp_done]
    clc
    ret
.seam:                              ; THE SEAM (98.3.9): the lap's join -
    cmp byte [vp_wkind], 1          ; the file's record, a frame like any
    jne .skey                       ; other...
    push ax                         ; (it shows frame L: in frame L's scene's
    mov ax, [vp_wL]                 ; sixteen, 98.1.3.2.3 - before the draw,
    call vp_palat                   ; or with a flip, at it)
    pop ax
    cmp byte [vp_flip], 0
    jne .sflip
    call vp_paldac
    call vp_decrec
    jmp short .sdone
.sflip:
    call vp_flipdec
    cmp word [vp_pevseg], 0
    je .sdone
    call vp_vwait
    call vp_paldac
    jmp short .sdone
.skey:                              ; ...or keyframe 0, or nothing, over a
    push dx                         ; cleared canvas: a keyframe is decoded
    push si                         ; onto black (98.1.3)
    call vp_cclear
    pop si
    pop dx
    push ax                         ; (and in the colours of the frame it
    mov ax, [vp_wL]                 ; shows, the screen black: 98.1.3.2.3 -
    cmp byte [vp_wkind], 0          ; nothing shown, a lap from frame 0: its
    jne .skp                        ; first scene's)
    xor ax, ax
.skp:
    call vp_palat
    call vp_paldac
    pop ax
    cmp byte [vp_wkind], 0
    je .sdone
    push word [vp_poff]             ; (both pages, the one drawn next kept)
    call vp_decboth
    pop word [vp_poff]
.sdone:
    mov ax, [vp_wL]                 ; the frame it shows: the next is after it
    inc ax
    mov [vp_done], ax
    clc
    ret

; vp_flipdec - DX:SI = a record, played with PAGE FLIPPING (98.3.8): the
; back page holds the frame before last, so the last record is decoded into
; it again and then this one, which brings it to this frame; this record is
; kept for the other page's turn; and the CRTC is pointed at the page - it
; latches the start address at the next retrace, so nothing waits, and the
; next draw, a frame period later, is into the page that was showing.
; clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_flipdec:
    cmp byte [vp_own], 0            ; (each record against its own page:
    jne vp_owndec                   ; nothing owed, VIDEO-SEEN-PLAN 3b)
    push dx
    push si
    mov ax, [vp_prevn]              ; WHAT THE BACK PAGE OWES: nothing, the
    or ax, ax                       ; last record again, or its rows copied
    jz .cur                         ; off the glass (98.3.8.1)
    cmp ax, VP_HELD                 ; (nothing after a HELD record: it was
    je .cur                         ; drawn on this page, 98.1.3.9)
    cmp ax, 0xFFFF                  ; (a record past VP_PREVKB, not kept)
    je .copy
    mov ax, [vp_pry1]               ; THE CHEAPER: a row copied is 80 bytes
    sub ax, [vp_pry0]               ; at ~VP_LCW record bytes' decode each,
    jbe .redec                      ; against the record's own bytes
    mul word [vp_flcw]
    or dx, dx
    jnz .redec
    cmp ax, [vp_prevn]
    jae .redec
.copy:
    call vp_lcopy
    call vo_lfix                    ; (the glass's text is not the picture)
    jmp short .cur
.redec:
    mov dx, [vp_prevseg]
    xor si, si
    call vp_decrec
.cur:
    pop si
    pop dx
    push dx
    push si
    call vp_decrec
    mov [vp_pgend], si              ; (past its lists: vp_fpdec's chain)
    pop si
    pop dx
    push ds                         ; the record, for the other page: its
    VP_DSDX                         ; band always, and itself while it fits
    mov ax, [si+2]                  ; the copy
    mov bx, [si+4]
    mov cx, [si]
    VP_DSEND
    pop ds
    mov bp, ax                      ; (y0's bit 15: HELD, 98.1.3.9)
    and ax, ROW_MASK                ; (a page and its flags, 98.1.3.7.2)
    and bx, ROW_MASK
    cmp word [vp_prevn], VP_HELD    ; AFTER A HELD RECORD: the page on the
    jne .nh                         ; glass is two records behind, and one
    cmp ax, [vp_pry0]               ; is kept - so the rows of both, and
    jbe .h0                         ; the copy off the glass for them
    mov ax, [vp_pry0]
.h0:
    cmp bx, [vp_pry1]
    jae .h1
    mov bx, [vp_pry1]
.h1:
    mov cx, 0xFFFF
.nh:
    mov [vp_pry0], ax
    mov [vp_pry1], bx
    test bp, 0x8000                 ; A HELD RECORD (98.1.3.9): drawn on the
    jz .nhd                         ; page the glass is not showing and NOT
    cmp byte [vp_fpg], 0            ; shown - the next record goes on the
    jne .nhd                        ; same page and is - with nothing owed
    mov word [vp_prevn], VP_HELD    ; to it but its rows (bit 15 is REC_FLIP
    ret                             ; on a page that is built ahead)
.nhd:
    cmp byte [vp_fpg], 0            ; (FLIPPED AND BUILT AHEAD: its own lists
    je .kp                          ; and no more - the chain and the audio
    mov cx, [vp_pgend]              ; are never decoded again, 98.3.8.5)
    sub cx, si
.kp:
    mov bx, 0xFFFF
    cmp cx, VP_PREVKB * 1024
    ja .nokeep
    push ds
    mov es, [vp_prevseg]
    xor di, di
    VP_DSDX
    mov bx, cx
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    VP_DSEND
    pop ds
.nokeep:
    mov [vp_prevn], bx
    call vo_flipon                  ; the text on it before it is shown
    mov ax, [vp_poff]               ; show it...
    call vp_show
    mov [vp_foff], ax
    mov ax, [vp_page]               ; ...and draw the other next
    xor [vp_poff], ax
    ret

; vp_owndec - DX:SI = a record of a flipped file AGAINST ITS OWN PAGE (byte
; 37 7, VIDEO-SEEN-PLAN 3b): drawn on the back page as it stands - which is
; the frame two back, or after a HELD record the one it holds - so nothing is
; decoded again and nothing kept. Only a record with REC_UP (y1 bit 12, the
; one after a key) first takes its rows y0..y1 off the glass, as 98.3.8.1's
; copy does. Then shown, unless HELD. Clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_owndec:
    push dx
    push si
    push ds
    VP_DSDX
    mov ax, [si+2]
    mov bx, [si+4]
    VP_DSEND
    pop ds
    test bh, 0x10                   ; REC_UP: its page brought up first
    jz .dec
    and ax, ROW_MASK
    and bx, ROW_MASK
    mov [vp_pry0], ax
    mov [vp_pry1], bx
    call vp_lcopy
    call vo_lfix                    ; (the glass's text is not the picture)
.dec:
    pop si
    pop dx
    push dx
    push si
    call vp_decrec
    pop si
    pop dx
    push ds
    VP_DSDX
    mov ax, [si+2]
    VP_DSEND
    pop ds
    mov word [vp_prevn], 0
    test ah, 0x80                   ; HELD (98.1.3.9): drawn and not shown,
    jz .show                        ; the next drawn on the same page
    ret
.show:
    call vo_flipon                  ; the text on it before it is shown
    mov ax, [vp_poff]               ; show it...
    call vp_show
    mov [vp_foff], ax
    mov ax, [vp_page]               ; ...and draw the other next
    xor [vp_poff], ax
    ret

; vp_fpdec - DX:SI = a record FLIPPED AND BUILT AHEAD (98.1.3.7.2, 98.3.8.5):
; drawn on the page it names - which gets one of the text's saves and the box
; taken off it - brought up first as vp_flipdec brings a back page up unless
; it was BUILT ahead, kept for the next one's bring-up and shown; then its
; chain, hidden records and COPYs, each onto its own page - the page just
; left only once the retrace has latched the flip. Clobbers AX, BX, CX, DX,
; SI, DI, BP, ES
vp_fpdec:
    push ds
    VP_DSDX
    mov al, [si+3]                  ; y0's high byte: the page
    mov ah, [si+5]                  ; y1's: BUILT (bit 4), a chain (bit 7)
    VP_DSEND
    pop ds
    mov [vp_fphi], ah
    call vp_pgnum
    call vo_pgsave                  ; (one of the text's saves on it)
    mov [vp_poff], ax
    call vp_pgoff                   ; (the box off it, none decoded round)
    test byte [vp_fphi], 0x10       ; BUILT: drawn on as it stands, nothing
    jz .up                          ; owed to it
    mov word [vp_prevn], 0
.up:
    mov ax, [vp_foff]
    mov [vp_fpold], ax              ; (the page left: the chain's wait)
    push dx
    call vp_flipdec                 ; brought up, drawn, kept, its text on,
    pop dx                          ; shown
    call vp_pgon
    test byte [vp_fphi], 0x80
    jz .r
    mov si, [vp_pgend]
    mov byte [vp_fpw], 1
    call vp_pghid
    mov byte [vp_fpw], 0
.r:
    mov ax, [vp_foff]
    mov [vp_poff], ax
    ret

; vp_pgcopy - DX:SI = a COPY (98.1.3.7.2), [vp_poff] its destination: its
; rows copied onto it through the latches off its source, the destination's
; box taken off first, and the source's - when it carries one over the rows
; - replaced by the picture under it (vo_cfix). SI past it. Preserves DX
vp_pgcopy:
    push dx
    push ds
    mov ds, dx
    mov ax, [si+2]
    mov bx, [si+4]
    mov cl, [si+5]                  ; (y1's high byte: the source)
    pop ds
    add si, COPY_LEN
    push si
    and ax, ROW_MASK
    and bx, ROW_MASK
    push word [vp_pry0]             ; (the kept record's band, for the next
    push word [vp_pry1]             ; frame's bring-up)
    mov [vp_pry0], ax
    mov [vp_pry1], bx
    mov al, cl
    call vp_pgnum                   ; AX = the source
    push word [vp_foff]
    mov [vp_foff], ax
    push ax
    call vp_pgoff
    call vp_lcopy
    pop ax
    call vo_cfix
    call vp_pgon
    pop word [vp_foff]
    pop word [vp_pry1]
    pop word [vp_pry0]
    pop si
    pop dx
    ret

; vp_kref1 - DX:SI = a key's REFERENCE to the page pool (98.1.3.8.1),
; [vp_poff] its page: kept in vp_kref for vp_kpool, which reads it once the
; key is whole - the key's read is where the picture's would land. SI past
; it. Preserves all but SI
vp_kref1:
    push ax
    push bx
    push cx
    push di
    push ds
    mov ds, dx
    mov ax, [si+6]
    mov cx, [si+8]
    mov di, [si+10]
    pop ds
    add si, REF_LEN
    mov bl, [vp_krn]
    cmp bl, VP_KREFS
    jae .r
    inc byte [vp_krn]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov [vp_kref+bx+2], ax
    mov [vp_kref+bx+4], cx
    mov [vp_kref+bx+6], di
    mov ax, [vp_poff]
    mov [vp_kref+bx], ax
.r:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; vp_rptr - SI = an offset into the ring: DX:SI the same byte, SI < 16.
; Clobbers AX, CL
vp_rptr:
    mov dx, [vp_ring]
    mov ax, si
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    ret

; vp_kpool - THE PAGE POOL (98.1.3.8.1, 98.3.8.6), after a key: each page it
; named black and then its picture, read in parts as a key is and checked as
; a key's reads are. CF=1, with [vp_err], when one could not be read or is
; not one a key's read could be. Clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_kpool:
    xor bx, bx
.e:
    cmp bl, [vp_krn]
    jae .done
    push bx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ax, [vp_kref+bx]
    mov [vp_kpo], ax
    mov [vp_poff], ax
    xor al, al
    call vp_kmove                   ; (black: a page-ahead play has no
    mov word [vp_kpo], 0            ; keeper, 98.3.19)
    mov ax, [vp_kref+bx+2]
    mov dx, [vp_kref+bx+4]
    mov cx, [vp_kref+bx+6]
    mov [vp_kfo], ax
    mov [vp_kfo+2], dx
    mov [vp_knext], cx
.rd:
    push word [vp_rdseg]
    mov ax, [vp_ring]
    mov [vp_rdseg], ax
    call vp_knrd
    pop word [vp_rdseg]
    jc .bad
    cmp byte [vp_kneed], 0
    je .ok
    mov dx, [vp_ring]
    call vp_rbnd
    jc .bad
.ok:
    call vp_rptr
    call vp_khdr                    ; (another part after it?)
    push dx
    call vp_decrec
    pop dx
    cmp byte [vp_kmore], 0
    jne .rd
    pop bx
    inc bx
    jmp short .e
.done:
    mov byte [vp_krn], 0
    mov ax, [vp_foff]
    mov [vp_poff], ax
    clc
    ret
.bad:
    pop bx
    mov byte [vp_krn], 0
    mov byte [vp_err], 1
    mov word [vp_errmsg], vp_s_kbad
    stc
    ret

; vp_lcopy - flipping: the canvas rows [vp_pry0, vp_pry1) copied from the
; page on the glass ([vp_foff]) to the one drawn next ([vp_poff]) through the
; VGA's LATCHES - write mode 1, Map Mask 0Fh, so a byte read and written
; moves a byte of all four planes (98.3.8.1). Whole rows of 80, from the
; canvas's origin: a Mode X row is 80 plane bytes and the CRTC does the row
; scale. Clobbers AX, BX, CX, DX, SI, DI, ES
vp_lcopy:
    mov cx, [vp_pry1]
    sub cx, [vp_pry0]
    jbe .r
    mov ax, 80
    mul cx
    mov cx, ax                      ; the plane bytes
    mov ax, 80
    mul word [vp_pry0]
    add ax, [vp_org]
    mov si, ax
    mov di, ax
    add si, [vp_foff]
    add di, [vp_poff]
    mov dx, 0x3C4
    mov ax, 0x0F02                  ; every plane
    out dx, ax
    mov dx, 0x3CE
    mov al, 5                       ; the Graphics Mode register as it is...
    out dx, al
    inc dx
    in al, dx
    mov bl, al
    and al, 0xFC
    or al, 1                        ; ...in write mode 1
    out dx, al
    push ds
    mov es, [vp_vseg]
    mov ds, [vp_vseg]
    cld
    rep movsb
    pop ds
    mov al, bl                      ; (and back: DX is still 3CFh)
    out dx, al
.r:
    ret

; vp_show - AX = a page's offset: the CRTC's start address (3D4h 0Ch/0Dh),
; in Mode X's bytes. Latched at the next retrace. Preserves all
vp_show:
    push ax
    push bx
    push dx
    mov [vp_shown], ax              ; (what the CRTC was last told)
    cmp byte [vp_tlay], LAY_HERC    ; A HERCULES (98.3.8.3): not the start
    jne .crtc                       ; address but 3B8h's bit 7, graphics and
    mov dx, 0x3B8                   ; video on either way
    or ax, ax
    mov al, 0x0A
    jz .h0
    mov al, 0x8A
.h0:
    out dx, al
    jmp short .r
.crtc:
    mov bx, ax
    mov dx, 0x3D4
    mov al, 0x0C
    mov ah, bh
    out dx, ax
    mov al, 0x0D
    mov ah, bl
    out dx, ax
.r:
    pop dx
    pop bx
    pop ax
    ret

; vp_flipto - AX = a page's offset, SHOWN and on the glass when this returns
; (98.3.8.3): a VGA latches the start address at the retrace, so the write
; and then the wait; a Hercules switches its page the moment 3B8h is
; written, so the wait and then the write, inside the blank. Preserves all
vp_flipto:
    cmp byte [vp_tlay], LAY_HERC
    je .h
    call vp_show
    jmp vp_vwait
.h:
    call vp_vwait
    jmp vp_show

; vp_hpg2 - after the mode set: CF=1 when this play wants a Hercules's
; second page and the machine has none to give - a colour card at B800h too,
; which fsx_page refuses it on (SPEC.md 53.10). Else, page-ahead on a
; Hercules, the second page's decode ALLOWED (3BFh = 3) for the whole play,
; page 0 shown, and page 1 made BLACK: the mode set clears page 0 only, and
; vp_kput blacks the canvas and not the screen round it, so a smaller video
; after a bigger one showed the last play's picture there at every flip
; (98.3.8.3). The bracket's restore puts 3BFh back. Preserves all but flags
vp_hpg2:
    cmp byte [vp_flip], 2
    jne .ok
    cmp byte [vp_tlay], LAY_HERC
    jne .ok
    push ax
    push dx
    mov al, 1                       ; the kernel's own test, page 1 shown...
    call OSAPI_FSX_PAGE
    jc .no
    mov dx, 0x3BF                   ; ...and the decode left allowed, page 0
    mov al, 3                       ; on the glass
    out dx, al
    xor ax, ax
    call vp_show
    push cx                         ; ...and the whole of page 1 black, now
    push di                         ; it is hidden and its decode is allowed
    push es
    mov es, [vp_vseg]
    mov di, VP_HPAGE
    mov cx, VP_HPAGE / 2
    cld
    rep stosw                       ; (AX = 0 from the show)
    pop es
    pop di
    pop cx
    pop dx
    pop ax
.ok:
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; vp_decboth - DX:SI = a record decoded into BOTH pages when flipping (a
; keyframe: the stream after it takes either), else as vp_decrec. SI past
; its lists, as vp_decrec leaves it
vp_decboth:
    cmp byte [vp_flip], 0
    je vp_decrec
    cmp byte [vp_npg], 0
    jne vp_decn
    push dx
    push si
    mov word [vp_poff], 0
    call vp_decrec
    pop si
    pop dx
    push ax
    mov ax, [vp_page]
    mov [vp_poff], ax
    pop ax
    mov [vp_pgsi], si
    push dx
    call vp_decrec
    pop dx
    mov [vp_pgend], si
    cmp byte [vp_flip], 2           ; PAGE-AHEAD (98.3.8.3): page 0 shown,
    jne .r                          ; and the key's hidden record - after
    xor ax, ax                      ; its lists - onto page 1
    mov [vp_foff], ax
    cmp [vp_shown], ax              ; (page 1 shown, after a flip: page 0
    je .s0                          ; shown and latched first - not on every
    call vp_flipto                  ; decode of the key a bench times)
.s0:
    mov si, [cs:vp_pgsi]            ; (the record's start, vp_decrec's)
    push ds
    VP_DSDX
    mov al, [si+5]                  ; y1 bit 15: a hidden record follows
    VP_DSEND                        ; (which takes the flags: tested after)
    pop ds
    mov si, [cs:vp_pgend]
    test al, 0x80
    jz .z
    call vp_pghid
.z:
    mov word [vp_poff], 0
.r:
    ret

; vp_decn - vp_decboth ON N PAGES (98.1.3.7.1, 98.3.8.4): a KEY's lists
; onto every page, the page it names shown (and latched), and its hidden
; records each onto its own page. SI past them, as vp_decboth leaves it
vp_decn:
    mov [vp_pgsi], si
    xor ax, ax
    mov [vp_krn], al                ; (98.3.8.6: its references, none yet)
    mov [vp_prevn], ax              ; (98.3.8.5: nothing owed after a key)
    mov [vp_poff], ax
    mov al, [vp_npg]
    mov [vp_pgk], al
.all:
    push dx
    push si
    call vp_decrec
    mov [vp_pgend], si
    pop si
    pop dx
    mov ax, [vp_poff]
    add ax, [vp_page]
    mov [vp_poff], ax
    dec byte [vp_pgk]
    jnz .all
    push ds
    mov ds, dx
    mov ax, [si+4]                  ; AH: y1's high byte, a hidden record
    mov al, [si+3]                  ; AL: y0's, the page shown after it
    pop ds
    push ax
    call vp_pgnum
    call vo_pgsave                    ; (a save of the text's there)
    mov [vp_foff], ax
    mov [vp_poff], ax
    cmp [vp_shown], ax
    je .s
    call vp_flipto
.s:
    pop ax
    mov si, [vp_pgend]
    test ah, 0x80
    jz .r
    mov byte [vp_inkey], 1          ; (a key's chain: a REFERENCE, not a
    call vp_pghid                   ; copy, 98.1.3.8.1)
    mov byte [vp_inkey], 0
.r:
    ret

; vp_pgnum - AL = a record's y0 high byte -> AX = the offset of the page in
; its bits 4-6 (98.1.3.7.1): that page x [vp_page]. Preserves all but AX
vp_pgnum:
    push cx
    push dx
    mov cl, 4
    shr al, cl
    and ax, 7
    mul word [vp_page]
    pop dx
    pop cx
    ret

; vp_pgdec - DX:SI = a record, played PAGE-AHEAD (98.1.3.7, 98.3.8.3): drawn
; on the page shown, or with REC_FLIP on the other, which is then shown and
; waited for; and a hidden record after its lists into the page hidden after
; it. Clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_pgdec:
    mov [vp_pgsi], si
    push ds
    VP_DSDX
    mov al, [si+3]                  ; y0's high byte: bit 15 a FLIP
    VP_DSEND
    pop ds
    test al, 0x80
    jz .here
    cmp byte [vp_npg], 0            ; THE FLIP: onto the page it names on N
    je .x2                          ; pages (98.3.8.4), with a save of the
    call vp_pgnum                   ; text's there...
    call vo_pgsave
    jmp short .fl
.x2:
    mov ax, [vp_foff]               ; ...or the hidden page of two, the
    xor ax, [vp_page]               ; text off it and not decoded round...
.fl:
    mov [vp_poff], ax
    call vp_pgoff
    push dx
    call vp_decrec
    pop dx
    mov [vp_pgend], si
    call vp_pgon
    call vo_flipon                  ; ...the text onto it before it shows...
    mov ax, [vp_poff]
    call vp_flipto                  ; ...and shown, the retrace waited for:
    mov [vp_foff], ax               ; the next hidden record may be this
    call vp_palev                   ; call's, into the page just left - and
    mov byte [vp_pgfl], 1           ; a scene's sixteen in that same blank
    jmp short .hid
.here:
    call vp_palev                   ; (drawn in place: before, as unflipped)
    mov ax, [vp_foff]
    mov [vp_poff], ax
    push dx
    call vp_decrec
    pop dx
    mov [vp_pgend], si
.hid:
    mov si, [cs:vp_pgsi]            ; (the record's start, vp_decrec's)
    push ds
    VP_DSDX
    mov al, [si+5]                  ; y1 bit 15: a hidden record follows
    VP_DSEND                        ; (which takes the flags: tested after)
    pop ds
    mov si, [cs:vp_pgend]
    test al, 0x80
    jz .r
    call vp_pghid
.r:
    mov ax, [vp_foff]
    mov [vp_poff], ax
    cmp byte [vp_pgfl], 0           ; FLIPPED: the text is decoded round on
    je .nf                          ; the page now shown, its own box and
    mov byte [vp_pgfl], 0           ; save (vc_setup) - the last page's left
    cmp byte [vo_mode], 0           ; every store under the new one's box
    je .nf                          ; going onto the glass over the text
    call vc_setup
.nf:
    ret

; vp_pghid - DX:SI = a hidden record (98.1.3.7): into the page that is not
; on the glass, the text off it and not decoded round - or ON N PAGES
; (98.3.8.4) each of a chain into the page it names. Clobbers all but DX
vp_pghid:
    cmp byte [vp_npg], 0
    je .two
.n:
    push ds
    mov ds, dx
    mov ax, [si+4]                  ; AH: another follows this one
    mov al, [si+3]                  ; AL: its page
    pop ds
    push ax
    call vp_pgnum
    cmp byte [vp_fpw], 0            ; (FLIPPED AND BUILT AHEAD: the page just
    je .nw                          ; left is on the glass until the retrace,
    cmp ax, [vp_fpold]              ; which is waited for before it is
    jne .nw                         ; written)
    call vp_vwait
    mov byte [vp_fpw], 0
.nw:
    mov [vp_poff], ax
    pop ax
    push ax
    test al, 0x80                   ; A COPY (98.1.3.7.2)
    jz .hr
    cmp byte [vp_inkey], 0          ; ...or in a key, A REFERENCE to the page
    jne .rf                         ; pool (98.1.3.8.1)
    call vp_pgcopy
    jmp short .nx
.rf:
    call vp_kref1
    jmp short .nx
.hr:
    call vp_pgoff
    push dx
    call vp_decrec                  ; (SI past its lists: the next one)
    pop dx
    call vp_pgon
.nx:
    pop ax
    test ah, 0x80
    jnz .n
    mov ax, [vp_foff]
    mov [vp_poff], ax
    ret
.two:
    mov ax, [vp_foff]
    xor ax, [vp_page]
    mov [vp_poff], ax
    call vp_pgoff
    push dx
    call vp_decrec
    pop dx
    call vp_pgon
    mov ax, [vp_foff]
    mov [vp_poff], ax
    ret

; vp_pgoff - page-ahead: the full screen's text OFF the page [vp_poff] - the
; one about to be drawn that is not on the glass - and no decoding round it;
; vp_pgon puts the clip back. Preserves DX, SI
vp_pgoff:
    push dx
    push si
    mov al, [vo_clip]
    mov [vp_pgclip], al
    mov byte [vo_clip], 0
    mov ax, [vp_poff]               ; (vo_pre's: the page's bit in vo_drawn,
    call vo_savbit                     ; none when no save is on it)
    test [vo_drawn], bl
    jz .o
    not bl
    and [vo_drawn], bl
    not bl
    shr bl, 1
    mov al, 1
    call vo_io
.o:
    pop si
    pop dx
    ret

vp_pgon:
    push ax
    mov al, [vp_pgclip]
    mov [vo_clip], al
    pop ax
    ret

; vp_vwait - the VGA's vertical retrace BEGUN, so a start address written
; before it has latched: a retrace under way is waited out first. Bounded,
; so a card that never says cannot hang the hook. Preserves all
vp_vwait:
    push ax
    push cx
    push dx
    push bx
    mov dx, 0x3DA                   ; a VGA's 3DAh bit 3, active high - or a
    mov bx, 0x0008                  ; Hercules's 3BAh bit 7, active LOW: BH
    cmp byte [vp_tlay], LAY_HERC    ; is XORed in so one test serves both
    jne .p
    mov dl, 0xBA
    mov bx, 0x8080
.p:
    mov cx, 0xFFFF
.a:
    in al, dx
    xor al, bh
    test al, bl
    jz .b
    loop .a
.b:
    mov cx, 0xFFFF
.c:
    in al, dx
    xor al, bh
    test al, bl
    jnz .d
    loop .c
.d:
    pop bx
    pop dx
    pop cx
    pop ax
    ret

; vp_cclear - the canvas black, for a seam that is a keyframe (98.3.9): the
; keeper zeroed and, playing onto the screen, put there - on both pages when
; flipping, which then owe the next frame no record of the last. Through
; the shadow it is the shadow, and the next copy takes every row.
; clobbers AX, BX, CX, DX, SI, DI, ES
vp_cclear:
    mov byte [vp_pcv], 0            ; (the poster's frame is not the canvas now)
    cmp word [vp_keep], 0           ; (NO KEEPER, 98.3.19: vp_kput below
    je .native                      ; blacks the screen itself)
    mov es, [vp_keep]
    call vp_zero
    cmp byte [vp_shadow], 0
    je .native
    mov byte [vp_c16st], 0          ; (black, and the copy puts it there)
    call vp_bandall
    ret
.native:
    call vp_kput
    mov word [vp_prevn], 0
    mov byte [vo_drawn], 0          ; the canvas is new: what the text had
    mov byte [vo_clip], 0           ; kept is not under it now (98.3.13)
    ret

; vp_decrec - DX:SI = a record: decoded onto the screen, or into the shadow
; with the rows it writes added to the band the next copy covers (98.3.2).
; clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_decrec:
    cmp byte [vp_shadow], 0
    je .native
    cmp byte [vp_tlay], LAY_C160    ; C160 (98.3.12.1): STRAIGHT onto the
    jne .shd                        ; screen while nothing needs the shadow -
    cmp byte [vo_drawn], 0          ; no text up, and no band owed a copy -
    jne .c16s                       ; and the shadow is let go stale
    mov ax, [vp_dy0]
    cmp ax, [vp_dy1]
    jb .c16s
    mov byte [vp_c16st], 1
    mov es, [vp_vseg]
    mov bp, [vp_org]
    shl bp, 1
    inc bp
    add si, 6
    push ds
    VP_DSDX
    call vd_c160
    VP_DSEND
    pop ds
    ret
.c16s:
    call vp_c16sync                 ; (the shadow the reference again)
.shd:
    push ds                         ; SHADOW (98.3.2): into the file's own
    VP_DSDX                         ; image at its own address 0, and the rows
    mov ax, [si+2]                  ; the record writes added to the band the
    mov cx, [si+4]                  ; next copy covers
    VP_DSEND
    pop ds
    and ax, ROW_MASK                ; (a HELD record's bit, 98.1.3.9)
    cmp cx, [vp_h]
    jbe .y1
    mov cx, [vp_h]
.y1:
    cmp ax, [vp_dy0]
    jae .y0k
    mov [vp_dy0], ax
.y0k:
    cmp cx, [vp_dy1]
    jbe .y1k
    mov [vp_dy1], cx
.y1k:
    mov es, [vp_shseg]
    xor bp, bp
    cmp byte [vp_planar], 2         ; VGA4 (98.3.10.4): its sub-records into
    je .shp                         ; the four planes of a RAM image
    cmp byte [vp_lrun], 0           ; LIVE (98.3.10.2): the record's blit runs,
    je .go2                         ; after its lists, onto the pass's
    cmp byte [vp_fruns], 0
    je .go2
    add si, 6
    push ds
    VP_DSDX
    call vd_native
    call vp_lrget
    VP_DSEND
    pop ds
    ret
.shp:
    add si, 6
    push ds
    VP_DSDX
    call vp_decram                  ; (SI past the sub-records' 0)
    cmp byte [cs:vp_lrun], 0
    je .shq
    cmp byte [cs:vp_fruns], 0
    je .shq
    call vp_lrget
.shq:
    VP_DSEND
    pop ds
    ret
.native:
    mov es, [vp_vseg]
    mov bp, [vp_org]
    add bp, [vp_poff]               ; the page being drawn (98.3.8), or 0
.go2:
    add si, 6
    push ds
    VP_DSDX
    cmp byte [cs:vp_planar], 0
    jne .mx
    cmp byte [cs:vo_clip], 0        ; the full screen's text up: round it
    jne .clip                       ; (98.3.13.1)
    call vd_native                  ; the lists, onto the adapter
    VP_DSEND
    pop ds
    ret
.clip:
    mov byte [cs:vc_mask], 1
    call vd_clip
    VP_DSEND
    pop ds
    ret
.mx:                                ; PLANAR (98.1.3.1): each sub-record's Map
    lodsb                           ; Mask, then its lists - four pixels a
    or al, al                       ; store under 0Fh, a plane's under the
    jz .mxd                         ; rest
    cmp al, 0x10                    ; SUB2's COLOUR LEAD (98.1.3.2.4): one
    jae .m3                         ; colour, in write mode 3
.mxp:
    cmp byte [cs:vp_m3on], 0        ; (a plane's after a colour's the writer
    je .mxq                         ; never makes, 98.1.3.2.4 - the GC put
    call vp_m3off                   ; back for it all the same)
.mxq:
    mov ah, al
    mov [cs:vc_mask], al
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    push bp
    cmp byte [cs:vo_clip], 0
    jne .mxc
    call vp_dlist
    pop bp
    jmp short .mx
.mxc:
    call vp_clist
    pop bp
    jmp short .mx
.m3:
    cmp byte [cs:vp_planar], 2      ; (VGA4's alone: Mode X's lead is a
    jne .mxp                        ; plane mask and nothing else)
    cmp byte [cs:vp_m3ok], 0        ; NO WRITE MODE 3 (an EGA): each byte
    jne .m3w                        ; read, changed and written a plane at
    and al, 0x0F                    ; a time - round the box too, vc_m3b
    mov [cs:vp_m3c], al             ; asking the same
    push bp
    mov word [cs:vw_fn], vp_m3pl
    cmp byte [cs:vo_clip], 0
    je .m3e
    mov word [cs:vw_fn], vc_m3b
.m3e:
    call vd_walk
    pop bp
    jmp .mx
.m3w:
    call vp_m3set
    push bp
    cmp byte [cs:vo_clip], 0
    jne .m3c
    call vd_wmap
    pop bp
    jmp .mx
.m3c:
    mov word [cs:vw_fn], vc_m3b     ; round the box (98.3.13.1): a byte at a
    call vd_walk                    ; time, the box's into the save
    pop bp
    jmp .mx
.mxd:
    mov ax, 0x0F02                  ; all four planes again: the desktop's
    mov dx, 0x3C4                   ; own drawing, and a one-bit file's
    out dx, ax                      ; decode, assume it (98.1.3.2)
    cmp byte [cs:vp_m3ok], 0        ; (and the Read Map 0 vp_m3pl moved,
    jne .mxk                        ; on a card without write mode 3)
    mov ax, 0x0004
    mov dx, 0x3CE
    out dx, ax
.mxk:
    cmp byte [cs:vp_m3on], 0
    je .mxe
    call vp_m3off
.mxe:
    VP_DSEND
    pop ds
    ret

; vp_dlist / vp_clist - a planar sub-record's lists at DS:SI, onto the
; screen (vd_native's contract) or round the text box (vd_clip's): SUB2's
; behind their list map, every other file's all ten
vp_dlist:
    cmp byte [cs:vp_sub2], 0
    jne .m
    jmp vd_native
.m:
    jmp vd_nmap
vp_clist:
    cmp byte [cs:vp_sub2], 0
    jne .m
    jmp vd_clip
.m:
    jmp vd_clipm

; vp_m3set - AL = a colour lead, 10h + c (SPEC.md 98.1.3.2.4): the GC in
; read mode 1 / write mode 3 with Color Don't Care 0 - a read then answers
; FFh and loads the latches - the Bit Mask all and every plane mapped, at a
; record's first; and Set/Reset c, every time. The resting state is put back
; by vp_m3off at the record's end (vga12.inc's: mode 0, Set/Reset 0, Color
; Don't Care 0Fh), so nothing outside a record's decode ever sees it. DS any.
; Clobbers AX, DX
vp_m3set:
    and al, 0x0F
    mov [cs:vp_m3c], al
    mov ah, al
    mov dx, 0x3CE
    cmp byte [cs:vp_m3on], 0
    jne .c
    mov byte [cs:vp_m3on], 1
    push ax
    mov dx, 0x3C4
    mov ax, 0x0F02                  ; every plane
    out dx, ax
    mov dx, 0x3CE
    mov ax, 0x0B05                  ; read mode 1, write mode 3
    out dx, ax
    mov ax, 0x0007                  ; Color Don't Care: none, so a read is FFh
    out dx, ax
    mov ax, 0xFF08                  ; Bit Mask: every bit (the CPU's byte is
    out dx, ax                      ; the mask)
    pop ax
.c:
    xor al, al                      ; Set/Reset: the colour
    out dx, ax
    ret

; vp_m3off - the GC as vga12.inc leaves it. DS any. Preserves all
vp_m3off:
    push ax
    push dx
    mov byte [cs:vp_m3on], 0
    mov dx, 0x3CE
    mov ax, 0x0005                  ; read mode 0, write mode 0
    out dx, ax
    mov ax, 0x0F07                  ; Color Don't Care: every plane
    out dx, ax
    xor ax, ax                      ; Set/Reset 0
    out dx, ax
    pop dx
    pop ax
    ret

; vp_m3pl - vd_walk's routine for a colour sub-record on a card WITHOUT
; write mode 3 (an EGA): byte DI of each plane read (Read Map), its mask
; AL's bits set where [vp_m3c] has that plane's and cleared where not, and
; written back to that plane alone (Map Mask) - what write mode 3 does in
; one store, in four. The GC as vga12.inc leaves it (write mode 0, Set/Reset
; off). Clobbers AX, BX, CX, DX
vp_m3pl:
    mov bl, al
    mov bh, [cs:vp_m3c]
    mov cx, 0x0100                  ; CL = the plane, CH = its bit
.p:
    mov dx, 0x3C4
    mov al, 2
    mov ah, ch
    out dx, ax                      ; Map Mask: this plane
    mov dx, 0x3CE
    mov al, 4
    mov ah, cl
    out dx, ax                      ; Read Map: this plane
    mov al, [es:di]
    test bh, ch
    jz .c
    or al, bl
    jmp short .w
.c:
    mov ah, bl
    not ah
    and al, ah
.w:
    mov [es:di], al
    shl ch, 1
    inc cl
    cmp cl, 4
    jb .p
    ret

; vp_m3ram - vd_walk's routine for a colour sub-record into a RAM image (ES
; plane 0, four planes [vp_plsp] paragraphs apart): byte DI of each plane
; has the mask AL's bits set where [vp_m3c] has that plane's, cleared where
; not - what write mode 3 does to the card. Clobbers AX, CX, DX
vp_m3ram:
    push es
    mov dx, es
    mov ah, [cs:vp_m3c]
    mov cx, 4
.p:
    mov es, dx
    shr ah, 1
    jnc .c
    or [es:di], al
    jmp short .n
.c:
    not al
    and [es:di], al
    not al
.n:
    add dx, [cs:vp_plsp]
    loop .p
    pop es
    ret

; vp_decram - DS:SI = a record's lists, ES = a RAM image at its address 0:
; decoded there, BP = 0. A MODEX image is four planes VP_MXPL paragraphs
; apart, and a sub-record is decoded into every plane its mask names.
; clobbers AX, BX, CX, DX, SI, DI, BP
vp_decram:
    xor bp, bp
    cmp byte [cs:vp_planar], 0
    jne .mx
    jmp vd_native
.mx:
    push es
    mov bx, es
.sub:
    lodsb
    or al, al
    jz .done
    cmp byte [cs:vp_planar], 2      ; VGA4's COLOUR LEAD (98.1.3.2.4): every
    jne .pm                         ; plane's byte at once, a byte at a time
    cmp byte [cs:vp_sub2], 0
    je .pm
    cmp al, 0x10
    jb .pm
    and al, 0x0F
    mov [cs:vp_m3c], al
    mov word [cs:vw_fn], vp_m3ram
    mov es, bx
    xor bp, bp
    push bx
    call vd_walk
    pop bx
    jmp short .sub
.pm:
    mov ah, al                      ; AH = the mask, shifted as it is spent
    mov dx, bx                      ; DX = plane 0's image
    mov cx, 4
    mov di, si                      ; the lists, decoded again per plane
.p:
    shr ah, 1
    jnc .np
    push ax
    push bx
    push cx
    push dx
    push di
    mov es, dx
    mov si, di
    xor bp, bp
    call vp_dlist                   ; SI past them, the last time counting
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
.np:
    add dx, [cs:vp_plsp]
    loop .p
    jmp short .sub
.done:
    pop es
    ret

; -----------------------------------------------------------------------------
; vp_next - step a stream cursor to its next record (SPEC.md 98.3)
; in:  BX = a cursor, six words: chunk, offset, sectors, next's sectors,
;      frames left, record offset - [vp_pc] the video's, [va_pc] the audio's
; out: CF=0 DX:SI = the record, CX = its len, the cursor past it;
;      CF=1 AL = 0 not loaded yet / 1 the end / 2 bad super-packet / 3 bad
;      record, and the cursor where it was
; clobbers: AX, CX, DX, SI, DI, ES
; One stepping for both cursors, worked IN PLACE through DI (VC_*): the
; video's is what the reader keys on, so the audio's may run ahead but never
; releases anything. It was worked on a copy, in and out: 1,765 cycles a
; frame, most of the hook's own (VIDEO-PLAN 15.8)
; -----------------------------------------------------------------------------
vp_next:
    push bx
    mov di, bx
    call vp_nextw
    pop bx
    ret

vp_nextw:
    mov byte [vp_nseam], 0
    cmp byte [vp_resid], 0
    jne vp_rnext
    cmp word [di+VC_FLEFT], 0
    jne .rec
    ; --- ENTER the super-packet at (pc, po), psec sectors: all of it
    ;     loaded, or wait. (pc, po) moved here the moment the one before
    ;     it ended, so the reader is never held up by a finished one
    mov cx, [di+VC_PSEC]
    or cx, cx
    jnz .sp
    cmp byte [vp_rep], 0            ; the chain's 0: the end of the stream -
    je .end                         ; or, repeating, THE SEAM (98.3.9), once
    mov ax, [di+VC_GEN]                ; the reader has armed one this cursor
    cmp ax, [vp_wgen]               ; has not taken
    je .wait
    mov ax, [vp_wgen]
    mov [di+VC_GEN], ax
    mov ax, [vp_wpc]                ; the cursor goes on after it...
    mov [di+VC_PC], ax
    mov ax, [vp_wpo]
    mov [di+VC_PO], ax
    mov ax, [vp_wpsec]
    mov [di+VC_PSEC], ax
    mov ax, [vp_widx]               ; ...past the records before frame L+1
    mov [di+VC_SKIP], ax
    xor ax, ax
    mov [di+VC_FLEFT], ax
    mov [di+VC_ROFS], ax
    mov byte [vp_nseam], 1
    xor dx, dx                      ; ...and the record is the seam: none for
    xor si, si                      ; kind 0
    xor cx, cx
    cmp byte [vp_wkind], 0
    je .sm
    mov ax, [vp_wsc]
    mov bx, [vp_wso]
    call vp_addr
    mov cx, [vp_wlen]
.sm:
    clc
    ret
.wait:
    xor al, al
    stc
    ret
.end:
    mov al, 1
    stc
    ret
.sp:
    push bx
    mov ax, [di+VC_PC]              ; where it ends - a chunk on, or two for
    mov bx, [di+VC_PO]              ; BIGSP's (98.1.4.1) - and the last chunk
    call vp_spend                   ; it touches: the one before, on an edge
    or bx, bx
    pop bx
    jnz .one
    dec ax
.one:
    cmp ax, [vp_lc]
    jb .ld
    cmp byte [vp_eof], 0            ; not all read yet - and never will be at
    je .nr                          ; the file's end with no Repeat: a stream
    cmp byte [vp_rep], 0            ; cut short, ended with the reason and
    je .bsp                         ; not stalled on its last frame for good
.nr:
    xor al, al                      ; not all read yet
    stc
    ret
.ld:
    mov ax, [di+VC_PC]
    mov bx, [di+VC_PO]
    call vp_addr                    ; DX:SI = the super-packet
    push ds
    VP_DSDX
    lodsw
    mov cx, ax                      ; frames
    lodsw                           ; next
    VP_DSEND
    pop ds
    jcxz .bsp                       ; no frames, or a next past the cap
    cmp ax, [vp_spcap]
    jbe .spok
.bsp:
    mov al, 2
    stc
    ret
.spok:
    mov [di+VC_FLEFT], cx
    mov [di+VC_NSEC], ax
    mov word [di+VC_ROFS], 4
.rec:
    ; --- the record at [rofs] into the super-packet
    mov ax, [di+VC_PC]
    mov bx, [di+VC_ROFS]            ; (past 32 KB in a BIGSP one: its whole
    cmp bx, VP_CHUNK                ; chunks first, so the sum is a word's)
    jb .rw
    sub bx, VP_CHUNK
    inc ax
.rw:
    add bx, [di+VC_PO]
    cmp bx, VP_CHUNK
    jb .ra
    sub bx, VP_CHUNK
    inc ax
.ra:
    call vp_addr                    ; DX:SI = the record
    push ds
    VP_DSDX
    mov cx, [si]                    ; its len...
    VP_DSEND
    pop ds
    mov ah, [di+VC_PSEC]            ; ...against what is left of the
    xor al, al                      ; super-packet (x 512)
    shl ah, 1
    sub ax, [di+VC_ROFS]
    cmp cx, ax
    ja .brec
    cmp byte [vp_own], 0            ; (none kept against its own page)
    jne .rfl
    cmp byte [vp_flip], 1           ; ...and, flipping, against the copy
    jne .rfl                        ; vp_flipdec keeps of it (98.3.8), as
    cmp word [vp_msl], 1            ; the seam's is - but a BIGSP file's
    jne .rfl                        ; longer one is copied off the glass
    cmp cx, VP_PREVKB * 1024        ; instead (98.3.8.1)
    ja .brec
.rfl:
    mov ax, [vp_abytes]
    add ax, 6 + 10                  ; the header and ten lists' ends - or
    cmp byte [vp_planar], 0         ; a planar record's one 0 after its
    je .rmin                        ; sub-records (98.1.3.1): an empty
    sub ax, 9                       ; frame is seven bytes
.rmin:
    cmp cx, ax
    jae .rok
.brec:
    mov al, 3
    stc
    ret
.rok:
    add [di+VC_ROFS], cx
    dec word [di+VC_FLEFT]
    jnz .out
    push bx                         ; (CX is the record's len, and BX the
    push cx                         ; caller's: vp_next's contract)
    mov ax, [di+VC_PC]              ; ITS LAST FRAME: step to the next one's
    mov bx, [di+VC_PO]              ; start now - the reader may reuse this
    mov cx, [di+VC_PSEC]            ; one's chunks from here, and waiting for
    call vp_spend                   ; the hook to ENTER the next one was a
    mov [di+VC_PC], ax              ; deadlock (a super-packet that needs a
    mov [di+VC_PO], bx              ; chunk the reader may not read until
    pop cx                          ; this one is left)
    pop bx
    mov ax, [di+VC_NSEC]
    mov [di+VC_PSEC], ax
.out:
    cmp word [di+VC_SKIP], 0           ; a record before the frame a seam goes
    je .ret                         ; on at (98.3.9): stepped over
    dec word [di+VC_SKIP]
    jmp vp_nextw
.ret:
    clc
    ret

; vp_slot - AX = a chunk -> AX = its slot, the chunk mod K (SPEC.md 98.3:
; K is whatever the machine had room for, not a power of two). Everything
; else preserved but the flags. One `div`, 2-4 calls a frame
vp_slot:
    push dx
    xor dx, dx
    div word [vp_kr]                ; (the ring's slots: the board's in place)
    xchg ax, dx
    pop dx
    ret

; vp_addr - AX = a chunk, BX = an offset in it -> DX:SI, a far pointer
; (clobbers AX, BX, CX)
vp_addr:
    cmp byte [vp_einp], 0
    jne vp_eaddr
    cmp byte [vp_xip], 0            ; the file held, decoded in place
    jne vp_xaddr                    ; (98.3.18.11)
    call vp_slot                    ; (a slot: <= VP_KBIG)
    mov ah, al
    xor al, al
    shl ah, 1
    shl ah, 1
    shl ah, 1                       ; x 2048 paragraphs
    add ax, [vp_ring]
    mov si, bx
    mov cl, 4
    shr bx, cl
    add ax, bx
    mov dx, ax
    and si, 15
    ret

; vp_spend - AX = a super-packet's chunk, BX = its offset there (a whole
; sector), CX = its sectors -> AX:BX, where it ends the same way: an end on
; a chunk's edge is the next chunk's 0. In SECTORS, so BIGSP's 127 from
; anywhere in a chunk stay in a word (98.1.4.1). Preserves CX, DX
vp_spend:
    push cx
    push dx
    mov dx, cx
    mov cl, 9
    shr bx, cl                      ; its start, in sectors (0..63)
    add bx, dx                      ; ...and its end (..190)
    mov dx, bx
    mov cl, 6
    shr dx, cl                      ; whole chunks on
    add ax, dx
    and bx, 63
    mov cl, 9
    shl bx, cl                      ; ...and the bytes into the last
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; vp_afill - put the audio of the next frames into the ring (SPEC.md 98.3.1):
; as far as the ring has room behind what the card has not played, and the
; reader has loaded - VP_AMAX frames a call, so a hook stays short. At the
; stream's end, silence to a whole block past it, so the card's last block
; interrupt finds a full one and the tail is heard
; -----------------------------------------------------------------------------
vp_afill:
    cmp byte [vp_aend], 0
    jne .out
    mov word [vp_acnt], VP_AMAX
.l:
    cmp byte [vp_rep], 0            ; (repeating, the seam is next: 98.3.9)
    jne .l2
    mov ax, [vp_afr]
    cmp ax, [vp_frames]
    jae .pad
.l2:
    mov ax, [vp_atot]               ; room: what is queued and not played,
    sub ax, [vp_alast]              ; plus this frame, inside the ring
    add ax, [vp_rbytes]
    cmp ax, VP_RL
    ja .out
    mov bx, va_pc
    call vp_next
    jnc .rec
    or al, al
%ifdef VP_DIAG
    jnz .ae
    mov byte [vp_astv], 1           ; NOT READ YET: the sound waits on the
    ret                             ; stream (the card line's diagnostic)
.ae:
%else
    jz .out                         ; not read yet: next call
%endif
    mov byte [vp_aend], 2           ; the end early, or damage: the video
    ret                             ; says which when it gets there
.rec:
    cmp byte [vp_nseam], 0
    jne .seam
    cmp byte [vp_resid], 0          ; RESIDENT: the frame's in the audio
    je .ra                          ; block (98.1.7)
    mov ax, [vp_afr]
    call vp_raud
    jmp short .rput
.ra:
    add si, cx                      ; the audio is the record's last bytes
    sub si, [vp_abytes]
.rput:
    mov cx, [vp_abytes]
    call vp_aput
%ifdef VP_DIAG
    mov byte [vp_astv], 0
%endif
    inc word [vp_afr]
.nx:
    inc word [vp_aseq]
    dec word [vp_acnt]
    jnz .l
    ret
.seam:                              ; THE SEAM (98.3.9): frame L's audio from
    cmp byte [vp_wkind], 1          ; the seam record, or a frame of silence
    jne .sil                        ; for a keyframe's join
    cmp byte [vp_resid], 0          ; (RESIDENT: frame L's, in the block)
    je .sa
    mov ax, [vp_wL]
    call vp_raud
    jmp short .sput
.sa:
    add si, cx
    sub si, [vp_abytes]
    jmp short .sput
.sil:
    xor dx, dx                      ; (a frame of the RING's: twice the
    mov cx, [vp_rbytes]             ; file's when the player decodes, as
    jmp short .sp2                  ; vp_aput's silence is not, 98.3.17.1)
.sput:
    mov cx, [vp_abytes]
.sp2:
    call vp_aput
    mov ax, [vp_wL]
    inc ax
    mov cl, [vp_ahead]              ; (sound ahead: the frames after the
    xor ch, ch                      ; join came in the tail's records, 98.1.8)
    add ax, cx
    mov [vp_afr], ax
    jmp short .nx
.out:
    ret
.pad:
    cmp word [vp_apend], 0          ; the end: silence to a whole block past
    jne .pw                         ; the last byte, once
    mov ax, [vp_atot]
    mov [vp_afinal], ax
    mov cx, [vp_blk]                ; (the card's block: vp_sblk)
    add ax, cx
    add ax, cx
    dec ax
    neg cx
    and ax, cx
    mov [vp_apend], ax
.pw:
    mov cx, [vp_apend]
    sub cx, [vp_atot]
    jz .done
    mov ax, VP_RL                   ; as much as there is room for now
    add ax, [vp_alast]
    sub ax, [vp_atot]
    jz .out
    cmp cx, ax
    jbe .pz
    mov cx, ax
.pz:
    xor si, si                      ; DX:SI = nowhere: vp_aput fills
    xor dx, dx
    call vp_aput
    jmp short .pw
.done:
    mov byte [vp_aend], 1
    ret

; VP_ADSTEP - one ADPCM4 sample (98.3.17.1): AL = the nibble, AH = the scale,
; BP = the running sample, BH = 0, ES:DI the ring. The sample stored and
; kept in BP, AH the next scale; BL clobbered
%macro VP_ADSTEP 0
    or al, ah                       ; the index: scale | nibble
    mov bl, al
    mov al, [cs:vp_addel + bx]      ; the step, signed
    cbw
    add ax, bp
    jns %%lo
    xor ax, ax                      ; clamped at 0...
%%lo:
    cmp ax, 255
    jbe %%hi
    mov ax, 255                     ; ...and at 255
%%hi:
    mov bp, ax
    stosb
    mov ah, [cs:vp_adnsc + bx]      ; the next scale
%endmacro

; the card's tables, by index (scale | nibble): the step (DOSBox's
; scaleMap) and the scale after it (scale + adjustMap) - tools/os88vid.py's
; ADPCM4_SCALE and ADPCM4_ADJUST, which tests/vidsound.py's soft row holds
; the decode to
vp_addel:
    db 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00, 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9
    db 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1
    db 0x02, 0x06, 0x0A, 0x0E, 0x12, 0x16, 0x1A, 0x1E, 0xFE, 0xFA, 0xF6, 0xF2, 0xEE, 0xEA, 0xE6, 0xE2
    db 0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x3C, 0xFC, 0xF4, 0xEC, 0xE4, 0xDC, 0xD4, 0xCC, 0xC4
vp_adnsc:
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10
    db 0x00, 0x10, 0x10, 0x10, 0x10, 0x20, 0x20, 0x20, 0x00, 0x10, 0x10, 0x10, 0x10, 0x20, 0x20, 0x20
    db 0x10, 0x20, 0x20, 0x20, 0x20, 0x30, 0x30, 0x30, 0x10, 0x20, 0x20, 0x20, 0x20, 0x30, 0x30, 0x30
    db 0x20, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x20, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30

; vp_aput - CX bytes from DX:SI into the ring at [vp_atot], wrapping, and the
; new total published to the card; DX = 0 writes [vp_afn] silence instead.
; With [vp_sadp] (98.3.17.1) CX is the FILE's ADPCM4 bytes and the ring takes
; twice as many samples; CX of silence is still the ring's.
; clobbers AX, BX, CX, SI, DI, ES
vp_aput:
    mov es, [vp_aseg]
    cmp byte [vp_snd], VP_SPK       ; THE SPEAKER, a card's samples: the
    jne .nlev                       ; shaper decides the piece - a frame's
    cmp byte [vp_spkpwm], 0         ; audio - from its own peak before a
    jne .nlev                       ; sample of it is emitted (34.11.9)
    or dx, dx
    jz .nlev
    push ds
    mov ds, dx
    call os88spkfx_level
    pop ds
.nlev:
    cmp byte [vp_sadp], 0           ; THE PLAYER DECODES: two samples a byte
    je .nd
    or dx, dx
    jz .nd
    shl cx, 1
.nd:
    push cx                         ; the whole count
    mov di, [vp_atot]
    and di, VP_RL - 1
    mov bx, VP_RL
    sub bx, di                      ; BX = room to the ring's end
    cmp cx, bx
    jbe .one
    mov cx, bx
    call .cp                        ; to the end...
    xor di, di                      ; ...and the rest from its start
    pop cx
    push cx
    sub cx, bx
.one:
    call .cp
    pop bx
    add [vp_atot], bx
    mov ax, [vp_atot]
    mov [es:VP_RL+SND_EXT_TOTAL], ax
    ret
.cp:
    cmp byte [vp_snd], VP_SPK       ; THE SPEAKER's ring holds PWM counts:
    jne .cc                         ; each sample through the shaper (98.3.15,
    or dx, dx                       ; 34.11.9) - unless the FILE holds them
    jz .sil                         ; already (98.1.1.3), which is a copy -
    cmp byte [vp_spkpwm], 0         ; and silence os88spk_sil either way: the
    jne .cm                         ; table's middle, or a count of 1 where
    push ds                         ; the shaper has the carrier away
    mov ds, dx
    call os88spkfx_emit
    pop ds
    ret
.sil:
    mov al, [os88spk_sil]
    cld
    rep stosb
    ret
.cc:
    or dx, dx
    jz .fill
    cmp byte [vp_sadp], 0
    jne .cs
.cm:
    push ds
    VP_DSDX
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    VP_DSEND
    pop ds
    ret
.fill:
    mov al, [vp_afn]
    cmp byte [vp_sadp], 0           ; (decoded: the running sample held,
    je .fl                          ; a no-change nibble's silence)
    mov al, [vp_sref]
.fl:
    cld
    rep stosb
    ret
; .cs - ADPCM4 decoded (98.3.17.1): CX samples (even) into ES:DI from CX / 2
; bytes at DX:SI, high nibble first, the card's own tables (98.1.1.1). The
; scale is a multiple of 16 in 0..48, so scale OR nibble is the table index
; and never needs a clamp; the sample does
.cs:
    push bx                         ; (the wrap's room, above, is in BX)
    push dx
    push bp
    mov bp, [vp_sref]               ; BP = the running sample, 0..255
    mov ah, [vp_sscl]               ; AH = the scale
    xor bh, bh
    push ds
    VP_DSDX
    shr cx, 1                       ; CX = the file's bytes
    cld
.csb:
    lodsb
    mov dl, al                      ; DL = the low nibble, for after
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    VP_ADSTEP
    mov al, dl
    and al, 0x0F
    VP_ADSTEP
    loop .csb
    VP_DSEND
    pop ds
    mov [vp_sref], bp
    mov [vp_sscl], ah
    pop bp
    pop dx
    pop bx
    ret


%define VD_C160                     ; ...and its C160 twin (98.3.12.1)
%define VD_YIELD                    ; vd_yield is ours (98.3.18.11)
%include "video/vdec.inc"
%include "video/vosd.inc"           ; the full screen's text (98.3.13)
%ifdef VP_DIAG
%define OS88SPK_DRYCNT              ; (the speaker's dry stretches, counted)
%endif
%include "os88spk.inc"              ; the speaker's ring player (34.11)
%include "os88spkfx.inc"            ; ...and its shaper (34.11.9)

; =============================================================================
; the window
; =============================================================================
vp_paint:                           ; W_PAINT: SI = window, region armed
    push ax
    push bx
    push si
    call vp_track
    cmp byte [vp_grey], 0           ; the ground, where nothing else is
    je .ng                          ; (98.4.8)
    call vp_ground
.ng:
    call vp_pposter
    call vp_pbar
    xor bx, bx
    call vp_ptext
    call vp_buttons
    cmp byte [vp_abon], 0
    je .out
    mov bx, si
    mov si, vp_ablines
    call os88ui_about_d
.out:
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; THE LAYOUT (SPEC.md 98.4.1). The picture at the video's own size if the
; screen has the room, else a half, else a quarter; the scrub bar under it;
; the transport buttons centred under THAT with the info card's button at the
; right - or, where there is room for the picture and the bar but not for a
; row of buttons as well, the buttons in the info card instead, which is then
; always shown. The info card beside the picture when it is asked for, and
; its room is taken from the picture's scale if it must be. The window may
; reach down over the dock (CGA's desktop band is too short otherwise)
; =============================================================================
; vp_layfit - the layout for [vp_wb] x [vp_h] and [vp_card] on this screen,
; into the vp_l* words. out: AX = 1 the picture's scale changed. Preserves
; the rest
vp_layfit:
    push bx
    push cx
    push dx
    push si
    push di
    call vp_video                   ; AX = width, BX = height
    sub ax, 10                      ; the widest content: frame x 7, 1 border
    mov [vp_lcwm], ax
    sub bx, MBAR_H + TITLE_H + 1    ; the tallest, over the dock
    mov [vp_lchm], bx
    mov ax, [vp_ps]
    mov [vp_lops], ax
    call vp_dinfo                   ; (the desktop's banks, for COMPACT)
    mov al, [vp_thmin]              ; SI = the scale tried: 1, 2, 4 - from
    xor ah, ah                      ; the file's own choice up (98.1.3.11)
    mov si, ax
.s:
    call vp_laysize                 ; the picture's size at SI -> vp_lpw/lph
    mov cl, [vp_card]
    call vp_laynorm
    call vp_layA
    jnc .a
    call vp_laycomp                 ; COMPACT (98.4.1.1): the row under the
    call vp_layA                    ; bar still, before the card takes it
    jnc .a
    call vp_laynorm
    call vp_layB
    jnc .b
    shl si, 1
    cmp si, 4
    jbe .s
    mov si, 4                       ; nothing fits: a quarter, buttons below
    call vp_laysize
    mov cl, [vp_card]
    call vp_laynorm
    call vp_layA
.a:
    mov al, 0
    jmp short .set
.b:
    mov al, 1
.set:
    mov [vp_lbin], al
    mov [vp_ps], si
    call vp_laybw                   ; the box: the picture's width, and never
    mov [vp_lbw], ax                ; less than the button row wants
    mov bx, ax
    mov ax, [vp_lph]
    add ax, [vp_lslk]               ; the picture's row goes down to a bank
    mov [vp_lbh], ax
    add ax, VP_BOXY
    add ax, [vp_lgap]
    mov [vp_lbary], ax              ; the bar's frame, under the box
    add ax, VP_BARH
    add ax, [vp_lgap]
    mov [vp_lbty], ax               ; the button row, under the bar
    mov ax, bx                      ; the card: past the box, on a byte
    add ax, VP_BOXX + 8 + 7
    and ax, 0xFFF8
    mov [vp_lcardx], ax
    mov ax, [vp_ps]
    xor ax, [vp_lops]
    jz .same
    mov ax, 1
.same:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; vp_laysize - SI = a scale: vp_lpw, vp_lph = the picture's size at it
vp_laysize:
    push ax
    push cx
    mov ax, [vp_pwb]                ; pixels: 8 a byte, over the scale
    mov cl, 3
    shl ax, cl
    mov cx, si
.w:
    shr cx, 1
    jz .wd
    shr ax, 1
    jmp short .w
.wd:
    mov [vp_lpw], ax
    mov ax, [vp_ph]                 ; rows: halved rounding UP, as vp_half
    mov cx, si                      ; does
.h:
    shr cx, 1
    jz .hd
    inc ax
    shr ax, 1
    jmp short .h
.hd:
    mov [vp_lph], ax
    pop cx
    pop ax
    ret

; vp_layA - buttons under the bar, the card if CL says so: CF=0 it fits, the
; content size in vp_lcw/vp_lch and [vp_lcard] set
vp_layA:
    push ax
    push bx
    mov [vp_lcard], cl
    call vp_laybw                   ; AX = the box's width
    add ax, VP_BOXX + 8             ; the content without the card
    or cl, cl
    jz .w
    add ax, 7                       ; ...or with it, past the box on a byte
    and ax, 0xFFF8
    add ax, VP_CARDW + 4
.w:
    cmp ax, [vp_lcwm]
    ja .no
    mov [vp_lcw], ax
    mov ax, [vp_lgap]
    shl ax, 1
    add ax, [vp_lph]
    add ax, [vp_lslk]
    add ax, VP_BOXY + VP_BARH + VP_BTH + 5
    mov bx, VP_CARDH                ; the card's own height, when it is shown
    or cl, cl
    jz .h
.hb:                                ; (vp_layB's: BX = the card's height)
    cmp ax, bx
    jae .h
    mov ax, bx
.h:
    cmp ax, [vp_lchm]
    ja .no
    mov [vp_lch], ax
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; vp_layB - the buttons in the card, which is then shown: CF=0 it fits
vp_layB:
    push ax
    push bx
    mov byte [vp_lcard], 1
    call vp_laybw
    add ax, VP_BOXX + 8 + 7
    and ax, 0xFFF8
    add ax, VP_CARDW + 4
    cmp ax, [vp_lcwm]
    ja vp_layA.no
    mov [vp_lcw], ax
    mov ax, [vp_lph]
    add ax, VP_BSLACK + VP_BOXY + VP_BGAP + VP_BARH + 5
    mov bx, VP_CARDHB               ; ...the height, as vp_layA's with its
    jmp short vp_layA.hb            ; card shown, this card's

; vp_laynorm / vp_laycomp - the box's slack and the bar's gaps for vp_layA:
; the layout's own, or COMPACT (98.4.1.1) - a CGA's 161 rows over the dock
; held a 320 x 112 picture and its bar but not the row too, by seven, so
; the logo (98.3.11) put its buttons in a card as wide as the screen. The
; slack is only what the desktop's banks need (1 on a CGA, 0 on a VGA, 3 on
; a Hercules: vp_boxxy's rounding) and the gaps are VP_CGAP. Preserves all
vp_laynorm:
    mov word [vp_lslk], VP_BSLACK
    mov word [vp_lgap], VP_BGAP
vp_noreloc:                         ; (a bare ret: the region's relocation
    ret                             ; proc - nothing points inside us)

vp_laycomp:
    push ax
    push bx
    push cx
    mov bl, [vp_dlay]               ; the banks, as vp_boxxy asks for them
    call vp_lay6                    ; BX = layout x 6
    mov al, [vp_laytab+bx+1]
    cbw                             ; (1, 2 or 4)
    dec ax
    mov [vp_lslk], ax
    mov al, VP_CGAP                 ; (AH = 0)
    mov [vp_lgap], ax
    pop cx
    pop bx
    pop ax
    ret

vp_laybw:                           ; AX = the box's width at vp_lpw
    mov ax, [vp_lpw]
    cmp ax, VP_MINBW
    jae .o
    mov ax, VP_MINBW
.o:
    ret

; vp_track - where the window is now: the content origin, and the buttons'
; rects in screen coordinates (a window moves without a paint). Preserves all
vp_track:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, [vp_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [vp_cx0], ax
    mov [vp_cy0], dx
    push ax                         ; ...and the bar's inside, which vp_pbar
    add ax, VP_BOXX                 ; sets and vp_wthumb draws against: a
    mov [vp_tx1], ax                ; dragged window's thumb went where the
    add ax, [vp_lbw]                ; bar USED to be until the next paint
    dec ax
    mov [vp_tx2], ax
    pop ax
    mov di, vp_brects
    cmp byte [vp_lbin], 0
    jne .incard
    mov bx, [vp_lbw]                ; UNDER THE BAR: the four centred on the
    sub bx, VP_NB * VP_BTP - (VP_BTP - VP_BTW)  ; box, the card's at its
    shr bx, 1                       ; right edge
    add ax, bx
    add ax, VP_BOXX
    add dx, [vp_lbty]
    jmp short .row
.incard:
    add ax, [vp_lcardx]             ; IN THE CARD, under its text
    add dx, VP_CARDBY
.row:
    mov cx, VP_NB
.r:
    call vp_rect
    add ax, VP_BTP
    loop .r
    mov cx, ax                      ; REPEAT and MUTE: fifth and sixth in the
    cmp byte [vp_lbin], 0           ; card's row, or at the box's left edge
    je .ub                          ; under the bar, where the card's seventh
    mov ax, cx                      ; is at its right
    call vp_rect
    add ax, VP_BTP
    call vp_rect
    mov ax, VP_NB + 2
    jmp short .n
.ub:
    mov ax, [vp_cx0]
    add ax, VP_BOXX
    call vp_rect
    add ax, VP_BTP
    call vp_rect
    mov ax, [vp_cx0]
    add ax, VP_BOXX
    add ax, [vp_lbw]
    sub ax, VP_BTW
    call vp_rect
    mov ax, VP_NBTN
.n:
    cmp byte [vp_abon], 0           ; none live under the About card
    je .live
    xor ax, ax
.live:
    mov [vp_btns + OS88UI_BT_N], ax
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vp_rect:                            ; AX, DX = a button's top left -> [DI], DI += 8
    mov [di], ax
    mov [di+2], dx
    push ax
    add ax, VP_BTW - 1
    mov [di+4], ax
    mov ax, dx
    add ax, VP_BTH - 1
    mov [di+6], ax
    pop ax
    add di, 8
    ret

; vp_buttons - every button, in the state it should be in now. Lock held
vp_buttons:
    push ax
    push bx
    cmp byte [vp_abon], 0
    jne .out
    mov ax, vp_i_play               ; PLAY IS PAUSE while it plays in the
    cmp byte [vp_bpause], 0         ; window (98.3.7)
    je .pl
    mov ax, vp_i_pause
.pl:
    mov [vp_blabels+4], ax
    mov ax, OS88UI_IMG              ; Open is always live; the key buttons
    mov [vp_bflags], ax             ; stop at the ends, and Play needs a file
    mov [vp_bflags+2], ax           ; that plays here
    mov [vp_bflags+4], ax
    mov [vp_bflags+6], ax
    mov bx, ax
    cmp byte [vp_rep], 0            ; Repeat stands DOWN while it is on
    je .r                           ; (98.3.9)
    or bx, OS88UI_LATCH
.r:
    mov [vp_bflags+8], bx
    mov bx, ax                      ; MUTE stands DOWN while muted, and there
    cmp byte [vp_mute], 0           ; is nothing to mute without a file that
    je .m                           ; has sound (98.3.17)
    or bx, OS88UI_LATCH
.m:
    cmp byte [vp_ok], 1
    jne .md
    cmp byte [vp_audio], 0
    jne .mo
.md:
    or bx, OS88UI_DIS
.mo:
    mov [vp_bflags+10], bx
    cmp byte [vp_lcard], 0          ; the card's button stands DOWN while the
    je .c                           ; card is out (OS88UI_LATCH)
    or al, OS88UI_LATCH
.c:
    mov [vp_bflags+12], ax
    cmp word [vp_sel], 0
    jne .p1
    or byte [vp_bflags+2], OS88UI_DIS
.p1:
    cmp byte [vp_ok], 1
    je .p2
    or byte [vp_bflags+4], OS88UI_DIS
.p2:
    mov ax, [vp_sel]
    inc ax
    cmp ax, [vp_nkeys]
    jb .p3
    or byte [vp_bflags+6], OS88UI_DIS
.p3:
    mov bx, vp_btns
    mov al, [vp_bone]               ; one button, if a caller named it
    or al, al
    jnz .b
    inc ax
.b:
    cmp al, [vp_btns + OS88UI_BT_N]
    ja .out
    call os88ui_btn
    cmp byte [vp_bone], 0
    jne .out
    inc al
    jmp short .b
.out:
    mov byte [vp_bone], 0
    pop bx
    pop ax
    ret

; vp_ptext - the info card's lines from BX on, each an opaque run the full
; width, so nothing is erased first (PERFORMANCE.md rule 2). Nothing while
; the card is in
vp_ptext1:                          ; ...or line BX alone
    push ax
    mov ax, bx
    inc ax
    jmp short vp_ptx
vp_ptext:
    push ax
    mov ax, VP_LINES
vp_ptx:
    push bx
    push cx
    push dx
    push si
    push di                         ; (vp_ld6's rung, the even one)
    mov di, ax                      ; DI = the line to stop before
    cmp byte [vp_lcard], 0
    je .out
    mov ax, VP_LINE
    mul bx
    add ax, vp_lines
    mov si, ax
    mov al, VP_LPITCH
    mul bl
    add ax, [vp_cy0]
    add ax, VP_TXTY
    mov dx, ax
    mov cx, [vp_cx0]
    add cx, [vp_lcardx]
.l:
    cmp bx, di
    jae .out
%ifdef VP_DIAG
    cmp byte [vp_lbin], 0           ; the buttons in the card: its text ends
    je .lok                         ; above them (VP_CARDBY)
    cmp bx, VP_LINESB
    jae .out
.lok:
%endif
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    add si, VP_LINE
    add dx, VP_LPITCH
    inc bx
    jmp short .l
.out:
    jmp vp_ld6

; vp_pposter - the box and the picture in it (98.4): the picture where there
; is one, black round it, each pixel written once
vp_pposter:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    call vp_boxxy                   ; the box, and the picture's place in it
    mov ax, [vp_py]                 ; where it is drawn, from the content's
    sub ax, [vp_cy0]                ; origin: a drag moves the pixels by any
    mov [vp_ppoff], ax              ; number of rows, and the play looks
    mov dx, [vp_pseg]               ; THE PICTURE: the poster, or a LIVE play's
    mov bx, [vp_pbw]                ; shadow - the frame it is on, at its own
    mov ax, [vp_prows]              ; size (98.3.10)
    cmp byte [vp_lsess], 0
    je .q
    mov dx, [vp_shseg]
    mov bx, 80
    mov ax, [vp_h]
.q:
    mov [vp_qseg], dx
    mov [vp_qbw], bx
    mov [vp_qrows], ax
    or dx, dx
    je .black
    mov cx, [vp_by2]                ; rows past the box - a picture made at
    sub cx, [vp_py]                 ; another scale, between a relayout and
    inc cx                          ; its reload - are not drawn
    cmp ax, cx
    jbe .r
    mov ax, cx
.r:
    mov [vp_pdh], ax
    ; THE FRAME HUGS THE PICTURE (98.3.7): its row is on a bank, so up to
    ; three of the box's rows are spare, above it and below. Inside the
    ; frame they read as black bars round a picture that has none (the
    ; owner's report), so they go OUTSIDE it, in the window's ground -
    ; white, or grey where the ground is (98.4.8)
    mov al, CWHITE
    call vp_color
    mov ax, [vp_bx1]                ; above the frame
    dec ax
    mov bx, [vp_by1]
    dec bx
    mov cx, [vp_bx2]
    inc cx
    mov dx, [vp_py]
    sub dx, 2
    call vp_gndne
    mov bx, [vp_py]                 ; below it
    add bx, [vp_pdh]
    inc bx
    mov dx, [vp_by2]
    inc dx
    call vp_gndne
    mov al, CBLACK
    call vp_color
    mov ax, [vp_bx1]                ; the frame, on the picture's rows
    dec ax
    mov bx, [vp_py]
    dec bx
    mov cx, [vp_bx2]
    inc cx
    mov dx, [vp_py]
    add dx, [vp_pdh]
    call OSAPI_GFX_FRAME
    mov bx, [vp_py]                 ; left of it
    mov dx, bx
    add dx, [vp_pdh]
    dec dx
    mov cx, [vp_px]
    dec cx
    call vp_fillne
    mov ax, [vp_px]                 ; right of it
    add ax, [vp_pdw]
    mov cx, [vp_bx2]
    call vp_fillne
    mov es, [vp_qseg]
    xor si, si
    mov bp, [vp_qbw]
    mov ax, [vp_px]
    mov cx, [vp_pdw]
    mov bx, [vp_py]
    mov dx, [vp_pdh]
    cmp byte [vp_lsess], 0          ; A LIVE PLAY'S picture is its shadow:
    je .bp                          ; VGA4's four planes, the repaint's
    cmp byte [vp_pixfmt], PF_VGA4   ; clip walked (98.3.10.4)
    jne .b1
    mov byte [vp_v4p], 1
    call vp_v4blit
    jmp .out
.bp:
    cmp byte [vp_p4], 0             ; THE POSTER: 16 colours as they are
    je .b1                          ; (98.4.5, 98.1.3.11), or one bit -
    call vp_p4planes                ; as planes, the repaint's clip walked,
    jnc .out                        ; where BLITP takes them (98.4.5.1)
.b4:
    call OSAPI_GFX_BLIT4
    jmp .out
.b1:
    call vp_blit1                   ; (every register kept)
    jnc .out
    add cx, ax                      ; refused: black where it would be
    dec cx
    add dx, bx
    dec dx
    call vp_fillne
    jmp short .out
.black:
    mov al, CBLACK                  ; no picture: the box, framed, black
    call vp_color
    mov ax, [vp_bx1]
    dec ax
    mov bx, [vp_by1]
    dec bx
    mov cx, [vp_bx2]
    inc cx
    mov dx, [vp_by2]
    inc dx
    call OSAPI_GFX_FRAME            ; (a kernel cell: every register kept)
    inc ax                          ; ...and the box inside it
    inc bx
    dec cx
    dec dx
    call vp_fillne
.out:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_p4planes - THE SIXTEEN-COLOUR POSTER AS PLANES (98.4.5.1): ES:SI the
; packed poster (BLIT4's nibbles, BP bytes a row), AX = x (on a byte), BX =
; y, CX = width, DX = rows - a few rows at a time into vp_v4buf's four
; planes and out through OSAPI_GFX_BLITP with DI bit 14, which walks the
; repaint's clip. Under a region BLIT4 gives up its fast path unless the box
; lies inside ONE fragment (SPEC.md 5.4.1.4), so a window dragged over a
; Live colour file's poster took a second or two to heal a pass, a run at a
; time, where the play's shadow, through BLITP, was instant. CF=1 refused
; by the probe (a one-bit display, a straddle, kern_small) and nothing
; drawn: the caller draws it with BLIT4. Preserves all
vp_p4planes:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push di
    mov di, 0x8000                  ; THE PROBE: this rect, with the region
    call OSAPI_GFX_BLITP            ; aside - BLITP's refusals are its
    pop di                          ; whole answer, asked before any work
    jc .ret
    mov [vp_pqx], ax
    mov [vp_pqy], bx
    mov [vp_pqw], cx
    mov [vp_pqr], dx
    mov [vp_pqsrc], si
    mov [vp_pqseg], es
    mov [vp_pqst], bp
    add cx, 7                       ; a plane's bytes a row
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [vp_pqnb], cx
    shl cx, 1                       ; ...and the rows vp_v4buf holds in four
    shl cx, 1
    mov ax, VP_V4BUF
    xor dx, dx
    div cx
    mov [vp_pqk], ax
.chunk:
    mov dx, [vp_pqr]
    or dx, dx
    jz .done
    cmp dx, [vp_pqk]
    jbe .n
    mov dx, [vp_pqk]
.n:
    mov [vp_pqn], dx
    mov ax, [vp_pqnb]               ; the plane step: a chunk's plane
    mul dx
    mov [vp_pqstp], ax
    push ds
    pop es                          ; ES = the buffer's
    mov di, vp_v4buf
    mov cx, [vp_pqn]
.row:
    push cx
    push di
    mov si, [vp_pqsrc]
    mov cx, [vp_pqnb]
    mov bp, [vp_pqstp]
    push ds
    mov ds, [cs:vp_pqseg]
.col:                               ; eight pixels: four packed bytes, the
    push cx                         ; left nibble high, b3 b2 b1 b0 - into
    mov cx, 4                       ; BL BH DL DH, planes 0 to 3
.pb:
    lodsb
    shl al, 1
    rcl dh, 1
    shl al, 1
    rcl dl, 1
    shl al, 1
    rcl bh, 1
    shl al, 1
    rcl bl, 1
    shl al, 1
    rcl dh, 1
    shl al, 1
    rcl dl, 1
    shl al, 1
    rcl bh, 1
    shl al, 1
    rcl bl, 1
    loop .pb
    mov [es:di], bl
    mov [es:di+bp], bh
    add di, bp
    mov [es:di+bp], dl
    add di, bp
    mov [es:di+bp], dh
    sub di, bp
    sub di, bp
    inc di
    pop cx
    loop .col
    pop ds
    pop di
    add di, [vp_pqnb]
    mov ax, [vp_pqst]
    add [vp_pqsrc], ax
    pop cx
    loop .row
    mov ax, [vp_pqx]                ; ...and the chunk to the screen
    mov bx, [vp_pqy]
    mov cx, [vp_pqw]
    mov dx, [vp_pqn]
    mov si, vp_v4buf
    mov bp, [vp_pqnb]
    mov di, [vp_pqstp]
    or di, 0x4000                   ; (walk the clip)
    call OSAPI_GFX_BLITP
    jc .ret                         ; (the rest through BLIT4, whole)
    add [vp_pqy], dx
    sub [vp_pqr], dx
    jmp .chunk
.done:
    clc
.ret:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_blitb - OSAPI_GFX_BLIT1's arguments, any number of rows: it takes 255
; at a time. CF = 1 a call refused. Preserves all
vp_blitb:
    cmp byte [vp_planar], 2         ; a VGA4 shadow's planes (98.3.10.4)
    jne vp_blit1
    jmp vp_v4blit

; vp_blit1 - vp_blitb for a ONE-BIT picture whatever the file is: the poster,
; which in a VGA4 file is a stored black and white one (98.1.3.11), never
; the planes vp_blitb takes a VGA4 file's shadow for. Preserves all
vp_blit1:
    push ax
    push bx
    push dx
    push si
    mov [vp_brem], dx
.l:
    mov dx, [vp_brem]
    or dx, dx
    jz .out                         ; (CF = 0 from the or)
    cmp dx, 255
    jbe .n
    mov dx, 255
.n:
    sub [vp_brem], dx
    call OSAPI_GFX_BLIT1
    jc .out
    add bx, dx                      ; the next band: its rows down the screen,
    push ax                         ; its bytes along the picture
    push dx
    mov ax, dx
    mul bp
    add si, ax
    pop dx
    pop ax
    jmp short .l
.out:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; LIVE IN COLOUR (98.3.10.4): the shadow is a VGA4 canvas's four bit-planes,
; [vp_plsp] paragraphs apart in ONE segment (vp_canlive holds them to it),
; row y at y x 80 in each. They go out as they are, one OSAPI_GFX_BLITP a
; run - the bytes and nothing else, which is the card's own shape - with
; DI bit 14 asking it to WALK the window's clip (SPEC.md 5.4.3.6), so a box
; a window covers part of is drawn exactly where it shows at the same
; price. Where BLITP refuses - off the screen's side, a straddle, a one-bit
; display, a kernel without the walk - the rows are repacked into BLIT4's
; nibbles a few at a time and drawn through the clip instead
; -----------------------------------------------------------------------------
VP_V4PMAX   equ 0x3FF               ; the planes' spacing that fits a segment,
                                    ; its step in bytes clear of DI's bit 14
VP_V4BUF    equ 1024                ; the repack's buffer, in the desktop's
                                    ; scratch (vp_v4buf, past vp_dskend)

; vp_v4blit - vp_blitb for a VGA4 shadow: ES:SI = plane 0's first byte, BP =
; 80, AX = x, BX = y, CX = the width in pixels, DX = rows; the clip armed.
; One BLITP walking the clip while [vp_v4p] says it may - a refusal ends
; that for the pass - else packed through vp_v4buf with OSAPI_GFX_BLIT4.
; CF = 0. Preserves all
vp_v4blit:
    cmp byte [vp_v4p], 0
    je .pk
    push di
    push cx
    mov di, [vp_plsp]               ; DI = the plane step, in bytes, and bit
    mov cl, 4                       ; 14: walk the clip (5.4.3.6)
    shl di, cl
    or di, 0x4000
    pop cx
    call OSAPI_GFX_BLITP
    pop di
    jnc .ret
    mov byte [vp_v4p], 0            ; refused: packed, for the rest of it
.pk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [vp_v4ax], ax
    mov [vp_v4by], bx
    mov [vp_v4cw], cx
    mov [vp_v4rl], dx
    add cx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1
    or cx, cx
    jnz .nb
    jmp .done
.nb:
    mov [vp_v4nb], cx
    shl cx, 1                       ; the buffer's stride: four bytes of
    shl cx, 1                       ; nibbles a plane byte
    mov [vp_v4bs], cx
    mov ax, VP_V4BUF
    xor dx, dx
    div cx
    mov [vp_v4rc], ax               ; ...and the rows it holds
    mov ax, [vp_plsp]
    mov cl, 4
    shl ax, cl
    mov [vp_v4ps], ax
.ch:
    mov dx, [vp_v4rl]
    or dx, dx
    jz .done
    cmp dx, [vp_v4rc]
    jbe .n
    mov dx, [vp_v4rc]
.n:
    sub [vp_v4rl], dx
    push dx
    mov [vp_v4k], dx
    mov di, vp_v4buf
.row:
    push si
    mov cx, [vp_v4nb]
.col:
    push cx
    push si
    mov al, [es:si]                 ; the byte column's four planes: eight
    add si, [vp_v4ps]               ; pixels
    mov ah, [es:si]
    add si, [vp_v4ps]
    mov bx, ax                      ; BL = plane 0, BH = plane 1
    mov al, [es:si]
    add si, [vp_v4ps]
    mov ah, [es:si]
    mov cx, ax                      ; CL = plane 2, CH = plane 3
    pop si
    inc si
    mov dl, 4
.px:                                ; two pixels a byte, the left high, each
    shl ch, 1                       ; b3 b2 b1 b0
    rcl al, 1
    shl cl, 1
    rcl al, 1
    shl bh, 1
    rcl al, 1
    shl bl, 1
    rcl al, 1
    shl ch, 1
    rcl al, 1
    shl cl, 1
    rcl al, 1
    shl bh, 1
    rcl al, 1
    shl bl, 1
    rcl al, 1
    mov [di], al
    inc di
    dec dl
    jnz .px
    pop cx
    loop .col
    pop si
    add si, bp
    dec word [vp_v4k]
    jnz .row
    pop dx
    push si
    push es
    push ds
    pop es
    mov si, vp_v4buf
    mov ax, [vp_v4ax]
    mov bx, [vp_v4by]
    mov cx, [vp_v4cw]
    push bp
    mov bp, [vp_v4bs]
    call OSAPI_GFX_BLIT4
    pop bp
    pop es
    pop si
    add [vp_v4by], dx
    jmp .ch
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    clc
    ret

; vp_gndne - the window's ground over AX,BX..CX,DX, unless it is empty:
; solid grey on a sixteen-colour desktop (98.4.8), else the colour set. The
; pen is left grey
vp_gndne:
    cmp byte [vp_grey], 0
    je vp_fillne
    push ax
    mov al, CLGRAY
    call vp_color
    pop ax
    jmp vp_fillne

; -----------------------------------------------------------------------------
; THE GREY GROUND (SPEC.md 98.4.8): on a sixteen-colour desktop the window's
; content is SOLID grey where no element is - Tracker's body, CLGRAY - and the window
; paints EVERY pixel itself (WF_OWNBG), so nothing is white first and grey
; after. The elements are holes in a rect, and what they leave is filled a
; band at a time: the picture's box, the bar, the buttons and the card in
; the content; the card's text lines and its buttons in the card, whose
; gaps are white
; -----------------------------------------------------------------------------
vp_ground:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call vp_boxxy
    mov ax, [vp_cx0]                ; THE CONTENT
    mov bx, [vp_cy0]
    mov [vp_gb], ax
    mov [vp_gb+2], bx
    add ax, [vp_lcw]
    dec ax
    add bx, [vp_lch]
    dec bx
    mov [vp_gb+4], ax
    mov [vp_gb+6], bx
    mov di, vp_gh
    mov ax, [vp_bx1]                ; the box, its frame included
    dec ax
    mov bx, [vp_by1]
    dec bx
    mov cx, [vp_bx2]
    inc cx
    mov dx, [vp_by2]
    inc dx
    call vp_ghole
    mov ax, [vp_cx0]                ; the bar
    add ax, VP_BOXX - 1
    mov bx, [vp_cy0]
    add bx, [vp_lbary]
    mov cx, ax
    add cx, [vp_lbw]
    inc cx
    mov dx, bx
    add dx, VP_BARH - 1
    call vp_ghole
    cmp byte [vp_abon], 0           ; the buttons, unless the About card is
    jne .nb                         ; up and they are not drawn
    call vp_gbtns
.nb:
    cmp byte [vp_lcard], 0          ; the card
    je .fill
    call vp_gcard
    call vp_ghole
.fill:
    mov bl, CLGRAY
    call .fl
    cmp byte [vp_lcard], 0          ; THE CARD: white between its lines
    je .out
    call vp_gcard
    mov [vp_gb], ax
    mov [vp_gb+2], bx
    mov [vp_gb+4], cx
    mov [vp_gb+6], dx
    mov di, vp_gh
    mov bx, [vp_cy0]                ; each line, an opaque run 8 rows tall
    add bx, VP_TXTY
    xor si, si
.ln:
    mov ax, [vp_cx0]
    add ax, [vp_lcardx]
    mov cx, ax
    add cx, VP_CARDW - 1
    mov dx, bx
    add dx, 7
    call vp_ghole
    add bx, VP_LPITCH
    inc si
%ifdef VP_DIAG
    cmp si, VP_LINESB
    jb .ln
    cmp byte [vp_lbin], 0           ; (the buttons in the card: its text
    jne .lnd                        ; ends above them)
%endif
    cmp si, VP_LINES
    jb .ln
.lnd:
    cmp byte [vp_lbin], 0           ; ...and its buttons, if they are in it
    je .cf
    cmp byte [vp_abon], 0
    jne .cf
    call vp_gbtns
.cf:
    mov bl, CWHITE
    call .fl
.out:
    jmp vp_ld6
.fl:                                ; BL = a colour: [vp_gb] less the holes
    xchg ax, di                     ; vp_gh to DI, filled in it (DI spent:
    sub ax, vp_gh                   ; both callers set it again, or pop it)
    mov cl, 3
    shr ax, cl
    mov [vp_ghn], ax
    mov al, bl
    call vp_color
    jmp vp_unfilled

; vp_gcard - AX,BX..CX,DX = the card's rect: its lines and, if they are in
; it, its buttons, with a margin round them - on the screen
vp_gcard:
    mov ax, [vp_cx0]
    add ax, [vp_lcardx]
    sub ax, 4
    mov bx, [vp_cy0]
    add bx, VP_TXTY - 3
    mov cx, ax
    add cx, VP_CARDW + 7
    mov dx, [vp_cy0]
    add dx, VP_CARDH - 1
    cmp byte [vp_lbin], 0
    je .r
    add dx, VP_CARDHB - VP_CARDH
.r:
    ret

; vp_gbtns - every button's rect, a hole each. DI = the list's end
vp_gbtns:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, vp_brects
    mov cx, VP_NBTN
.b:
    push cx
    mov ax, [si]
    mov bx, [si+2]
    mov cx, [si+4]
    mov dx, [si+6]
    call vp_ghole
    pop cx
    add si, 8
    loop .b
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_ghole - AX,BX..CX,DX to the hole list at DI, DI past it
vp_ghole:
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    mov [di+6], dx
    add di, 8
    ret

; vp_unfilled - the part of the rect [vp_gb] no rect of the [vp_ghn] at
; vp_gh covers, filled in the colour set. A
; band at a time from the top, each ending where a hole starts or stops;
; in a band, the gaps between the holes that cross it, left to right. Every
; comparison signed: a window may hang off the screen's left or top.
; Preserves all
vp_unfilled:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [vp_gb+2]               ; AX = the band's top
.band:
    cmp ax, [vp_gb+6]
    jg .done
    mov [vp_gy], ax
    mov dx, [vp_gb+6]               ; DX = its bottom: the rect's, or the
    mov si, vp_gh                   ; row before a hole starts below it, or
    mov cx, [vp_ghn]                ; a crossing hole's last
    jcxz .yb
.ye:
    mov bx, [si+2]
    cmp bx, ax
    jle .in
    dec bx
    cmp bx, dx
    jge .nx
    mov dx, bx
    jmp short .nx
.in:
    mov bx, [si+6]
    cmp bx, ax
    jl .nx
    cmp bx, dx
    jge .nx
    mov dx, bx
.nx:
    add si, 8
    loop .ye
.yb:
    mov [vp_gy2], dx
    mov di, [vp_gb]                 ; DI = x
.xs:
    cmp di, [vp_gb+4]
    jg .bnext
    mov word [vp_gx1], 0x7FFF       ; the crossing hole not left of x with
    mov si, vp_gh                   ; the least left edge
    mov cx, [vp_ghn]
    jcxz .gap
.hl:
    mov ax, [si+2]
    cmp ax, [vp_gy]
    jg .hn
    mov ax, [si+6]
    cmp ax, [vp_gy]
    jl .hn
    mov ax, [si+4]
    cmp ax, di
    jl .hn
    mov ax, [si]
    cmp ax, [vp_gx1]
    jge .hn
    mov [vp_gx1], ax
    mov ax, [si+4]
    mov [vp_gx2], ax
.hn:
    add si, 8
    loop .hl
.gap:
    mov cx, [vp_gx1]                ; the gap: x up to that hole, or to the
    dec cx                          ; rect's right
    cmp cx, [vp_gb+4]
    jle .c
    mov cx, [vp_gb+4]
.c:
    cmp cx, di
    jl .skip
    mov ax, di
    mov bx, [vp_gy]
    mov dx, [vp_gy2]
    call OSAPI_GFX_FILL
.skip:
    cmp word [vp_gx1], 0x7FFF       ; no hole: the band is done
    je .bnext
    mov di, [vp_gx2]                ; ...else on past it
    inc di
    jmp .xs
.bnext:
    mov ax, [vp_gy2]
    inc ax
    jmp .band
.done:
    jmp vp_ld6

; vp_fillne - GFX_FILL AX,BX..CX,DX in the colour set, unless it is empty
vp_fillne:
    cmp cx, ax
    jl .no
    cmp dx, bx
    jl .no
    call OSAPI_GFX_FILL
.no:
    ret

; vp_pbar - the scrub bar (98.4): white, and the thumb black where the play
; starts - or, mid-drag, under the pointer - three fills and no pixel twice;
; grey with no keyframes to pick
vp_pbar:
    push ax
    push bx
    push cx
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [vp_cx0]
    add ax, VP_BOXX - 1
    mov bx, [vp_cy0]
    add bx, [vp_lbary]
    mov cx, ax
    add cx, [vp_lbw]
    inc cx
    mov dx, bx
    add dx, VP_BARH - 1
    call OSAPI_GFX_FRAME
    inc ax                          ; the inside
    mov [vp_tx1], ax
    inc bx
    dec cx
    mov [vp_tx2], cx
    dec dx
    cmp word [vp_nkeys], 0
    jne .live
    call OSAPI_GFX_FILL_GRAY
    jmp short .out
.live:
    call vp_thumbx                  ; AX = the thumb's x in the bar
    mov [vp_wtx], ax
    add ax, [vp_tx1]
    mov [vp_tx], ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [vp_tx1]                ; white before it...
    mov cx, [vp_tx]
    dec cx
    call vp_fillne
    mov ax, [vp_tx]                 ; ...and after it
    add ax, VP_THW
    mov cx, [vp_tx2]
    call vp_fillne
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [vp_tx]                 ; ...and the thumb
    mov cx, ax
    add cx, VP_THW - 1
    call vp_fillne
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_pthumb - a Live play's thumb moved along a bar already drawn (98.4.9):
; the columns it left white and the columns it took black, two fills where
; vp_pbar is a frame and three, and no column written twice. Lock held, the
; clip armed; old and new from [vp_wtx] and [vp_tx1], which vp_track keeps
; where the window is now
vp_pthumb:
    cmp word [vp_nkeys], 0
    je vp_pbar
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call vp_thumbx
    mov si, [vp_wtx]                ; SI = where it was, DI = where it goes,
    mov [vp_wtx], ax                ; on the screen
    mov di, [vp_tx1]
    add si, di
    add di, ax
    mov [vp_tx], di
    mov bx, [vp_cy0]                ; the bar's inside rows
    add bx, [vp_lbary]
    inc bx
    mov dx, bx
    add dx, VP_BARH - 3
    mov al, CWHITE
    call OSAPI_SET_COLOR
    cmp di, si
    je .out
    jl .left
    mov ax, si                      ; RIGHT: white from where it was to the
    mov cx, si                      ; nearer of where it goes and its old
    add cx, VP_THW                  ; right end...
    cmp cx, di
    jle .r1
    mov cx, di
.r1:
    dec cx
    call vp_fillne
    mov ax, si                      ; ...black from the farther of the two
    add ax, VP_THW                  ; to its new right end
    cmp ax, di
    jge .r2
    mov ax, di
.r2:
    mov cx, di
    add cx, VP_THW - 1
    jmp short .blk
.left:
    mov ax, di                      ; LEFT: the mirror
    add ax, VP_THW
    cmp ax, si
    jge .l1
    mov ax, si
.l1:
    mov cx, si
    add cx, VP_THW - 1
    call vp_fillne
    mov ax, di
    mov cx, di
    add cx, VP_THW
    cmp cx, si
    jle .l2
    mov cx, si
.l2:
    dec cx
.blk:
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call vp_fillne
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_thumbx - AX = the thumb's offset along the bar: under the pointer while
; it is dragged, else where the play starts, along the file's frames
vp_thumbx:
    push cx
    push dx
    mov ax, [vp_tpos]
    cmp byte [vp_drag], 0
    jne .out
    cmp byte [vp_sess], 0           ; a session: where it has got to
    je .key
    mov ax, [vp_done]
    or ax, ax
    jz .at
    dec ax
    cmp ax, [vp_frames]             ; a silent stream longer than its header
    jb .at                          ; says ends on the chain's 0: the thumb
    mov ax, [vp_frames]             ; stops at the bar's end
    dec ax
    jmp short .at
.key:
    xor ax, ax
    cmp word [vp_sel], 0
    je .out
    mov ax, [vp_ke+KE_K]
.at:
    mov cx, [vp_lbw]
    sub cx, VP_THW
    mul cx
    div word [vp_frames]
.out:
    pop dx
    pop cx
    ret

vp_repaint:                         ; SI = window, lock held, no region armed
    push bx
    mov bx, si
    call vp_clips
    jc .gone
    call vp_paint
.gone:
    pop bx
    ret

vp_about:                           ; OSAPI_ABOUT_SET's handler: SI = window
    push bx
    push si
    mov byte [vp_abon], 1
    mov bx, si
    mov si, vp_ablines
    call os88ui_about
    pop si
    pop bx
    ret

vp_abdismiss:                       ; CF=1: the click went on the card
    cmp byte [vp_abon], 0
    je .none
    mov byte [vp_abon], 0
    call vp_repaint
    stc
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; vp_fmt - the info panel's lines (SPEC.md 98.4), each padded to the width
; (opaque runs, no erase):
;   0 the file   1 its title   2 canvas, screen, fps, length
;   3 the stream's KB/s and its sound   4 where Play starts   5 the message
;   6, 7 what the last play cost
; -----------------------------------------------------------------------------
vp_fmt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, vp_lines                ; blank every line first
    mov bx, VP_LINES
.bl:
    mov al, ' '
    mov cx, VP_COLS
    rep stosb
    xor al, al
    stosb
    dec bx
    jnz .bl
    ; 0: the file
    mov di, vp_lines
    mov si, vp_s_file
    call vp_puts
    mov si, vp_name
    cmp byte [si], 0
    jne .nm
    mov si, vp_s_nofile
.nm:
    call vp_puts
    cmp byte [vp_loaded], 0
    jne .have
    jmp .msg
.have:
    ; 1: the title
    mov di, vp_lines + VP_LINE
    mov si, vp_title
    call vp_puts
    ; 2: the canvas, its screen, the rate, the length
    mov di, vp_lines + 2 * VP_LINE
    mov ax, [vp_pwb]                ; the pixels, whatever a byte holds
    mov cl, 3
    shl ax, cl
    cmp byte [vp_pixfmt], PF_C160   ; (C160's poster is two wide a pixel)
    jb .pxw
    shr ax, 1
    cmp byte [vp_pixfmt], PF_C512   ; (...and C512's is a cell's two, so
    je .px4                         ; four poster pixels a cell - and
    cmp byte [vp_pixfmt], PF_TEXT   ; TEXT's: its cells, by its rows)
    jne .pxw
.px4:
    shr ax, 1
.pxw:
    call vp_putw
    mov al, 'x'
    call vp_putc
    mov ax, [vp_ph]
    cmp byte [vp_pixfmt], PF_TEXT
    jne .pxh
    mov ax, [vp_h]
.pxh:
    call vp_putw
    mov al, ' '
    call vp_putc
    mov bl, [vp_layout]
    xor bh, bh
    shl bx, 1
    mov si, [vp_laynames+bx]
    cmp byte [vp_pixfmt], PF_CGA4
    jne .lc5
    mov si, vp_s_cga4
.lc5:
    cmp byte [vp_pixfmt], PF_TEXT   ; TEXT: mono or colour
    jne .lc6
    mov si, vp_s_txtm
    cmp byte [vp_cgapal], 0
    je .lnm
    mov si, vp_s_txtc
    jmp short .lnm
.lc6:
    cmp byte [vp_pixfmt], PF_C512   ; C512: the card it was made for, the
    jne .lnm                        ; one thing its byte 54 says (98.1.3.5)
    mov bl, [vp_cgapal]
    xor bh, bh
    shl bx, 1
    mov si, [vp_c5names+bx]
.lnm:
    call vp_puts
    mov ax, [vp_rate]               ; fps to two places: rate x 100 / spf
    mov cx, 100
    mul cx
    mov cx, [vp_spf]                ; (32 bits: a rate the parse takes can
    call vp_div32                   ; be 655 fps and more)
    mov bl, 2
    call vp_putn
    mov si, vp_s_fps
    call vp_puts
    mov ax, [vp_frames]
    call vp_putt
    ; 3: the stream's rate, and its sound
    mov di, vp_lines + 3 * VP_LINE
    mov ax, [vp_slen]               ; bytes a frame x fps / 1024: every step
    mov dx, [vp_slen+2]             ; inside 32 bits (a frame is < 64 KB)
    mov cx, [vp_frames]
    call vp_div32
    mov cx, [vp_rate]
    call vp_mul32
    mov cx, [vp_spf]
    call vp_div32
    mov cx, 1024
    call vp_div32
    xor bl, bl
    call vp_putn
    mov si, vp_s_kbs
    call vp_puts
    mov bl, [vp_audio]
    xor bh, bh
    shl bx, 1
    mov si, [vp_audnames+bx]
    call vp_puts
    cmp byte [vp_ahead], 0          ; (carried ahead of the picture, 98.1.8)
    je .nah
    mov si, vp_s_ahead
    call vp_puts
.nah:
    call vp_spkinfo
    ; 4: where Play starts - or, a session waiting, where it is paused -
    ;    or after a play that read a LAYER, what it did with it (98.1.9)
    mov di, vp_lines + 4 * VP_LINE
    cmp byte [vp_sess], 0
    jne .sess
    cmp byte [vp_played], 0
    je .nos
    cmp byte [vp_lyoff], 0          ; ...or why it read none
    je .ly4
    cmp byte [vp_lywhy], 2
    jne .lyd
    mov si, vp_s_lyrm               ; 'Layer off: no room, 41 KB short'
    mov ax, [vp_lysht]
    call vp_psw
    mov si, vp_s_lyrms
    call vp_puts
    jmp .msg
.lyd:
    cmp byte [vp_lywhy], 0
    je .lyc
    cmp byte [vp_lywhy], 3          ; 'Layer off: not timed' (98.1.9.7)
    jne .lydk
    mov si, vp_s_lynt
    call vp_puts
    jmp .msg
.lydk:
    mov si, vp_s_lydsk              ; 'Layer off: disk 437 KB/s, its 602'
    mov ax, [vp_lydkb]
    call vp_psw
    mov si, vp_s_lydits
    mov ax, [vp_lykbs]
    call vp_psw
    jmp .msg
.lyc:
    mov si, vp_s_lyoff              ; 'Layer off: key 120 ms, its 49'
    cmp byte [vp_bnon], 0
    je .lyk
    mov si, vp_s_lyofb              ; ...or 'Layer off: bench 21 ms, its 5'
.lyk:
    call vp_puts
    mov ax, [vp_lytm]
    call .ms
    mov si, vp_s_lyits
    call vp_puts
    mov ax, [vp_lytl]
    call .ms
    jmp .msg
.ms:                                ; AX in 10 us, said in ms
    push cx
    xor dx, dx
    mov cx, 100
    div cx
    pop cx
    jmp vp_putw
.ly4:
    mov ax, [vp_lygot]
    or ax, [vp_lymis]
    or ax, [vp_lyskp]
    jz .nos
    mov si, vp_s_lydrew             ; 'Layer drew 812, 21 missed, 0 late'
    mov ax, [vp_lygot]
    call vp_psw
    mov si, vp_s_lymis
    mov ax, [vp_lymis]
    call vp_psw
    mov si, vp_s_lylate
    mov ax, [vp_lyskp]
    call vp_psw
    mov si, vp_s_lylt
    call vp_puts
    jmp .msg
.sess:
    cmp byte [vp_lrun], 0           ; a LIVE play running: where it started,
    jne .nos                        ; as before it ran (its frame moves)
    mov si, vp_s_pausedat
    call vp_puts
    mov ax, [vp_done]
    or ax, ax
    jz .pz
    dec ax
.pz:
    call vp_putt
    jmp .msg
.nos:
    mov si, vp_s_nokeys
    cmp byte [vp_nkwhy], VOK_NOKEY
    je .nk
    mov si, vp_s_bigkeys
.nk:
    mov cx, [vp_nkeys]
    jcxz .kl
    mov si, vp_s_start
    cmp word [vp_sel], 0
    jne .key
    mov ax, cx
    call vp_psw
    jmp short .msg
.key:
    mov si, vp_s_fromk
    mov ax, [vp_sel]
    inc ax
    call vp_psw
    mov si, vp_s_of
    mov ax, cx
    call vp_psw
    mov si, vp_s_comma
    call vp_puts
    mov ax, [vp_ke+KE_K]            ; its time: the screen after frame k
    call vp_putt
    jmp short .msg
.kl:
    call vp_puts
.msg:
    ; 5: what Play does, or why not - or after a play with nothing to
    ;    say, what it banked in (98.3.18.6)
    mov di, vp_lines + 5 * VP_LINE
    mov si, [vp_msg]
    cmp byte [vp_played], 0
    je .msg5
    cmp si, vp_s_ready
    jne .msg5
    call vp_bksay
    jmp short .msg6
.msg5:
    call vp_puts
.msg6:
    cmp byte [vp_played], 0
    jne .res
    call vp_xsay                    ; 6: the hold, until a play's figures
    jmp .mem
.res:
    ; 6: frames drawn, stalls, pauses
    mov di, vp_lines + 6 * VP_LINE
    mov si, vp_s_drew
    mov ax, [vp_done]
    sub ax, [vp_base]
    call vp_psw
    mov al, '/'                     ; (' of ' ran a whole play's line past
    call vp_putc                    ; the card: its pauses fell off the end)
    mov ax, [vp_frames]
    sub ax, [vp_base]
    call vp_putw
    mov si, vp_s_stall
    mov ax, [vp_stall]
    call vp_psw
    cmp byte [vp_snd], 0
    je .nop
    mov si, vp_s_pause
    mov ax, [vp_pause]
    call vp_psw
.nop:
    ; 7: late, and the time against the file's
    mov di, vp_lines + 7 * VP_LINE
    mov si, vp_s_late
    mov ax, [vp_late]
    call vp_psw
    mov si, vp_s_ticks
    mov ax, [vp_dt]
    call vp_psw
    mov si, vp_s_want
    call vp_puts
    mov ax, [vp_done]               ; the ticks those frames should have
    sub ax, [vp_base]               ; taken: frames x spf / 10 x 182 / rate,
    mul word [vp_spf]               ; in that order so every step fits 32
    mov cx, 10                      ; bits (65,535 x 920 / 10 x 182 < 2^31)
    call vp_div32
    mov cx, 182
    call vp_mul32
    mov cx, [vp_rate]
    call vp_div32
    xor bl, bl
    call vp_putn
.mem:
%ifdef VP_DIAG
%ifndef VP_RDLOG                    ; (VIDEOR: the read log in its room)
    ; 12: THE LAYER'S TESTS (98.1.9.3, 98.1.9.4) - the disk as timed, the
    ; line it was held to, and key 0 timed against its model
    mov ax, [vp_lysp]
    or ax, [vp_lysp+2]
    jz .nlt
    cmp byte [vp_played], 0
    je .nlt
    mov di, vp_lines + 12 * VP_LINE
    mov si, vp_s_tdsk               ; Disk 548 KB/s, line 565; key 4/3
    call vp_puts
    mov ax, [vp_lydkb]
    call vp_putw
    mov si, vp_s_tln
    call vp_puts
    mov ax, [vp_lydln]
    call vp_putn
    mov si, vp_s_tkey
    call vp_puts
    mov ax, [vp_lytm]
    call vp_putn
    mov al, '/'
    call vp_putc
    mov ax, [vp_lytl]
    call vp_putn
.nlt:
%endif
    ; 8 and 9: THE HEAP (98.3), once a play has started - what Play found,
    ; and what the ring was sized from and took
    cmp word [vp_mfre0], 0
    je .out
    mov di, vp_lines + 8 * VP_LINE
    mov si, vp_s_heap               ; Heap 405K run, 471K free at Play
    call vp_puts
    mov ax, [vp_mrun0]
    call vp_putw
    mov si, vp_s_krun
    call vp_puts
    mov ax, [vp_mfre0]
    call vp_putn
    mov si, vp_s_kfree
    call vp_puts
    mov di, vp_lines + 9 * VP_LINE
    cmp word [vp_mrun], 0           ; Ring 10/8 of 381K; keep 75 snd 17
    je .mk
    mov si, vp_s_ring
    call vp_puts
    mov ax, [vp_k]
    call vp_putn
    mov al, '/'
    call vp_putc
    mov al, [vp_rneed]
    xor ah, ah
    call vp_putn
    mov si, vp_s_of
    call vp_puts
    mov ax, [vp_mrun]
    call vp_putn
    mov si, vp_s_kk
    call vp_puts
    jmp short .mk2
.mk:
    mov byte [vp_mkeep], 0          ; (RESIDENT: no ring, and its keeper and
    mov byte [vp_msnd], 0           ; sound are not captured - say nothing)
    jmp .out
.mk2:
    mov si, vp_s_keep
    call vp_puts
    mov al, [vp_mkeep]
    xor ah, ah
    call vp_putn
    mov si, vp_s_snd
    call vp_puts
    mov al, [vp_msnd]
    call vp_putn
    cmp byte [vp_played], 0         ; 10 and 11: THE READER (98.3), after a
    je .out                         ; play - its least lead over the picture,
    cmp word [vp_lmin], 0xFFFF      ; the hook's longest gap, and the card's
    je .out                         ; pauses: the stream's of all, the longest
    mov di, vp_lines + 10 * VP_LINE
    mov si, vp_s_lead               ; Lead 1 at f470; hook gap 3
    call vp_puts
    xor dx, dx
    mov ax, [vp_lmin]
    call vp_putn
    mov si, vp_s_atf
    call vp_puts
    mov ax, [vp_lminf]
    call vp_putn
    mov si, vp_s_hgap
    call vp_puts
    mov ax, [vp_gap]
    call vp_putn
    cmp byte [vp_msnd], 0
    je .out
    mov di, vp_lines + 11 * VP_LINE
%ifndef VP_RDLOG                    ; (VIDEOR: a card's play is the case)
    cmp byte [vp_snd], VP_SPK       ; THE SPEAKER RAN DRY: how often, and for
    jne .card                       ; how long - silence its clock did not
    mov si, vp_s_sdry               ; count, so the play ran that much slow
    call vp_puts                    ; (Spkr dry 41 times, 2.3 s)
    mov ax, [os88spk_dryg]
    call vp_putw
    mov si, vp_s_sdt
    call vp_puts
    mov ax, [vp_rate]
    xor dx, dx
    mov cx, 10
    div cx
    mov cx, ax                      ; samples a tenth of a second
    mov ax, [os88spk_dryn]
    mov dx, [os88spk_dryn+2]
    call vp_div32
    mov bl, 1                       ; (in tenths)
    call vp_putn
    mov si, vp_s_ms
    call vp_puts
    jmp .out
.card:
%endif
    mov si, vp_s_dry                ; Dry 9/11 stream; most 6t f480
    call vp_puts
    mov ax, [vp_pstrm]
    call vp_putn
    mov al, '/'
    call vp_putc
    mov ax, [vp_pause]
    call vp_putn
    mov si, vp_s_most
    call vp_puts
    mov ax, [vp_pmax]
    call vp_putn
    mov si, vp_s_tf
    call vp_puts
    mov ax, [vp_pmaxf]
    call vp_putn
%endif
.out:
; vp_ld7, vp_ld8 .. vp_ld4 - THE SHARED EPILOGUE, vp_fmt's own (it falls
; in): pop the last 7 of AX BX CX DX SI DI ES (vp_ld7), or the last 8, 7, 6
; or 4 of AX BX CX DX SI DI BP ES, in push order, and return. Cold routines
; end in `jmp vp_ldN` instead of their own pops - nothing a play runs does.
; Written out, not OS88_POPLADDER, and only the rungs something jumps to
; named: tools/stkbalance.py walks a jump into it from the depth it came with
vp_ld7:
    pop es
    jmp short vp_ld6
vp_ld8:
    pop es
vp_ld7b:
    pop bp
vp_ld6:
    pop di
    pop si
vp_ld4:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_putt - AX = frames -> DI as the time they take, m:ss
vp_putt:
    push ax
    push bx
    push cx
    push dx
    mul word [vp_spf]
    mov cx, [vp_rate]
    call vp_div32                   ; DX:AX = seconds
    mov cx, 60
    call vp_div32                   ; ...minutes, CX = the seconds over
    xor bl, bl
    call vp_putn
    mov al, ':'
    call vp_putc
    mov ax, cx
    aam                             ; AH = tens, AL = units
    add ax, '00'
    xchg al, ah
    call vp_putc
    mov al, ah
    call vp_putc
    jmp short vp_ld4

; vp_puts - SI = NUL string -> DI, stopping at the line's end (DI's own NUL)
vp_puts:
    push ax
.l:
    lodsb
    or al, al
    jz .out
    call vp_putc
    jmp short .l
.out:
    pop ax
    ret

; vp_putc - AL -> [DI] unless DI is on its line's NUL
vp_putc:
    cmp byte [di], 0
    je .full
    stosb
.full:
    ret

; vp_psw - SI = a string, AX unsigned: the string and then the number, as
; vp_puts and vp_putw - DX = 0 and BL = 0 after
vp_psw:
    call vp_puts
; vp_putw - AX unsigned, no decimals -> DI: vp_putn with DX = 0 and BL = 0,
; which it leaves so
vp_putw:
    xor dx, dx
    xor bl, bl

; vp_putn - DX:AX unsigned, BL decimals -> DI
vp_putn:
    push ax
    push bx
    push cx
    push dx
    xor bh, bh                      ; BH = digits emitted
.d:
    ; STKBALANCE-LOOP: one digit (and the point) pushed a turn and the second loop pops them; the count is in BH
    mov cx, 10
    call vp_div32                   ; DX:AX /= 10, CX = the digit
    push cx
    inc bh
    cmp bh, bl
    jne .nopt
    mov cx, '.' - '0'               ; (no push imm on an 8086)
    push cx
    inc bh
.nopt:
    mov cx, ax
    or cx, dx
    jnz .d
    or bl, bl                       ; ...and with decimals, at least one digit
    jz .o                           ; before the point: "0.05", not ".05"
    mov cl, bl
    inc cx                          ; (CL <= 3: CH is not read)
    cmp bh, cl
    jbe .d
.o:
    pop ax
    add al, '0'
    call vp_putc
    dec bh
    jnz .o
    jmp vp_ld4

; THE KERNEL CALLS COLD CODE MAKES MOST, as near thunks: a far call is five
; bytes a site and a near one three. Each is exactly the slot it names, so a
; site reads as before; none is reached from a play's per-frame path, whose
; sites keep their far calls (and pay no call more)
vp_rdfree:                          ; the record buffer [vp_rdseg] back, DX it
    mov dx, [vp_rdseg]
vp_mfree:                           ; OSAPI_MEM_FREE
    call OSAPI_MEM_FREE
    ret
vp_mclaim:                          ; OSAPI_MEM_CLAIM
    call OSAPI_MEM_CLAIM
    ret
vp_ticks:                           ; OSAPI_GET_TICKS
    call OSAPI_GET_TICKS
    ret
vp_ems:                             ; OSAPI_DRV_CALL to EMS, BL the verb
    mov bh, DRVC_EMS
    call OSAPI_DRV_CALL
    ret
vp_wakew:                           ; OSAPI_WM_WAKE, BX = the window
    mov bx, [vp_win]
    call OSAPI_WM_WAKE
    ret
vp_clipw:                           ; OSAPI_WM_CLIP_SET, BX = the window...
    mov bx, [vp_win]
vp_clips:                           ; ...or BX as it is
    call OSAPI_WM_CLIP_SET
    ret
vp_mclhi:                           ; OSAPI_MEM_CLAIM_HI
    call OSAPI_MEM_CLAIM_HI
    ret
vp_mmov:                            ; OSAPI_MEM_MOVABLE
    call OSAPI_MEM_MOVABLE
    ret
vp_mavail:                          ; OSAPI_MEM_AVAIL
    call OSAPI_MEM_AVAIL
    ret
vp_xcaps:                           ; OSAPI_XMEM_CAPS
    call OSAPI_XMEM_CAPS
    ret
vp_video:                           ; OSAPI_VIDEO
    call OSAPI_VIDEO
    ret
vp_color:                           ; OSAPI_SET_COLOR
    call OSAPI_SET_COLOR
    ret
vp_scaps:                           ; OSAPI_SND_CAPS
    call OSAPI_SND_CAPS
    ret
vp_sstrm:                           ; OSAPI_SND_STREAM
    call OSAPI_SND_STREAM
    ret

; vp_div32 - DX:AX /= CX; CX = the remainder
vp_div32:
    push bx
    xchg ax, bx                     ; (BX the low word)
    mov ax, dx
    xor dx, dx
    div cx
    xchg ax, bx
    div cx
    mov cx, dx
    mov dx, bx
    pop bx
    ret

; vp_mul32 - DX:AX *= CX (the product must fit 32 bits). Every other
; register kept
vp_mul32:
    push bx
    mov bx, dx
    mul cx                          ; DX:AX = the low word's product
    push dx
    xchg ax, bx                     ; (BX its low word)
    mul cx                          ; AX = the high word's, the low half
    pop dx
    add dx, ax
    mov ax, bx
    pop bx
    ret

; =============================================================================
; data
; =============================================================================
vp_tpl:
    dw 7, 22, 0, 0                  ; W_X = 7 mod 8: the content on a byte
                                    ; (SPEC.md 11.94). The size: vp_entry's,
                                    ; from the layout (98.4.1)
    dw vp_cap, vp_paint, vp_onkey, vp_clickw

    OS88_MENUSET vp_menus, vp_ttl, vp_oncmd
        OS88_MENU vp_m_file, vp_i_file, 8
    OS88_MENUSET_END vp_menus
vp_ttl:       db 'Video Player', 0
vp_pfx1:      db 'Video Player - ', 0
vp_pfx2:      db 'Video - ', 0
vp_cap:       db 'Video Player', 0  ; the window's caption (98.4.3): the
              times 15 + VP_COLS + 1 - 13 db 0  ; longest is pfx1 + a title
vp_m_file:    db 'File', 0
vp_i_file:    dw vp_it_open, vp_it_play, vp_it_fs, vp_it_prev, vp_it_next
              dw vp_it_info, vp_it_rep, vp_it_mute
vp_s_nofile:  db '(none) - File > '  ; ...and the menu's item is its tail
vp_it_open:   db 'Open...', 0
vp_it_play:   db 'Play (Space)', 0
vp_it_fs:     db 'Full screen (F)', 0
vp_it_prev:   db 'Previous key (Left)', 0
vp_it_next:   db 'Next key (Right)', 0
vp_it_info:   db 'Info (I)', 0
vp_it_rep:    db 'Repeat (R)', 0
vp_it_mute:   db 'Mute (M)', 0

; the buttons (SPEC.md 20.5.1.3): Tracker's transport pictures, 16 x 10
    OS88UI_BTNREC vp_btns, vp_brects, vp_blabels, vp_bflags, VP_NB
vp_blabels:   dw vp_i_open, vp_i_prev, vp_i_play, vp_i_next, vp_i_rep
              dw vp_i_mute, vp_i_info
vp_bflags:    times VP_NBTN dw OS88UI_IMG
vp_brects:    times VP_NBTN * 4 dw 0
vp_i_pause:                         ; ||
    db ICON_SOLID, 10
    times 10 dw 00E70h
vp_i_rep:                           ; two arrows round: Repeat (98.3.9)
    db ICON_SOLID, 10
    dw 00020h, 01FF0h, 03FF8h, 03030h, 03020h
    dw 0040Ch, 00C0Ch, 01FFCh, 00FF8h, 00400h
vp_i_mute:                          ; a speaker, struck: Mute (98.3.17)
    db ICON_SOLID, 10
    dw 00100h, 00304h, 03F88h, 03F90h, 03FA0h
    dw 03F90h, 03F88h, 00304h, 00100h, 00000h
vp_i_info:                          ; an i: the info card
    db ICON_SOLID, 10
    dw 00180h, 00180h, 00000h, 00380h, 00180h
    dw 00180h, 00180h, 00180h, 003C0h, 00000h
vp_i_open:                          ; an eject
    db ICON_SOLID, 10
    dw 00180h, 003C0h, 007E0h, 00FF0h, 01FF8h
    dw 03FFCh, 00000h, 03FFCh, 03FFCh, 00000h
vp_i_prev:                          ; |<<
    db ICON_SOLID, 10
    dw 03084h, 0318Ch, 0339Ch, 037BCh, 03FFCh
    dw 03FFCh, 037BCh, 0339Ch, 0318Ch, 03084h
vp_i_play:                          ; >
    db ICON_SOLID, 10
    dw 00C00h, 00F00h, 00FC0h, 00FF0h, 00FFCh
    dw 00FFCh, 00FF0h, 00FC0h, 00F00h, 00C00h
vp_i_next:                          ; >>|
    db ICON_SOLID, 10
    dw 0210Ch, 0318Ch, 039CCh, 03DECh, 03FFCh
    dw 03FFCh, 03DECh, 039CCh, 0318Ch, 0210Ch

vp_ablines:   dw vp_ab1, vp_ab2, vp_ab3, vp_ab4, vp_ab2, vp_ab6, 0
vp_ab1:       db 'Video Player for os8088', 0
vp_ab2:       db 0
vp_ab3:       db 'After XDC, by Jim Leonard', 0
vp_ab4:       db '(MobyGamer), MIT', 0
vp_ab6:       db 'Contributed by Elendilon', 0

; per layout (SPEC.md 98.1.2): FSXM id, banks, stride, rows
vp_lstr       equ vp_laytab + LAY_LIN80 * 6 + 2
                                    ; LIN80's stride: 80, or 40 while a
                                    ; NARROW file is open (98.1.3.2.5) - so
                                    ; vp_rowaddr and every stride check agree
vp_laytab:
    db FSXM_CGA640, 2
    dw 80, 200
    db FSXM_HERC, 4
    dw 90, 348
    db FSXM_VGA12, 1
    dw 80, 480
    db FSXM_VGA13, 1
    dw 320, 200
    db FSXM_MODEX, 1
    dw 80, 240
    db FSXM_TEXT80, 1               ; C160: the attributes, packed (98.1.3.3)
    dw 80, 100
    db FSXM_TEXT80, 1               ; TXT: the same screen AS IT IS, a
    dw 160, 100                     ; character and an attribute (98.1.3.5)
    db FSXM_TEXT80, 1               ; TEXT: the same, not retimed - 80 x 25
    dw 160, 25                      ; on any adapter (98.1.3.6)
vp_laynames:  dw vp_s_cga, vp_s_herc, vp_s_vga, vp_s_vga8, vp_s_modex
              dw vp_s_c160, vp_s_c512, vp_s_txtm
vp_laynotab:  dw vp_s_nocga, vp_s_noherc, vp_s_novga, vp_s_novga8
              dw vp_s_novga8, vp_s_nocol, vp_s_nocmp, vp_s_notxt
vp_laycptab:  dw vp_s_cpcga, vp_s_cpherc, vp_s_cpvga, vp_s_novga8
              dw vp_s_novga8, vp_s_colfs, vp_s_colfs, vp_s_notxt
                                        ; (TEXT is never copied: unread)
vp_laykb:     db 16, 32, 38, 63, 75     ; each layout's memory image, KB -
              db 8                      ; C160's too, 80 x 100: without it
                                        ; vp_zero read the next table's 0 and
                                        ; cleared NOTHING (98.3.14)
              db 16                     ; ...and TXT's, 160 x 100
              db 4                      ; ...and TEXT's, 160 x 25
vp_bayer4:    db 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5
vp_rthr:      db 0, 0, 0, 0             ; vp_v8mono: this row's four
vp_mxseg:     dw 0, 0, 0, 0             ; ...and a planar image's four planes
vp_v4x:       dw 0                      ; vp_v4pack: the output's pixel and
vp_v4y:       dw 0                      ; row, the canvas's width, the
vp_v4w:       dw 0                      ; source column, and which
vp_v4cx:      dw 0                      ; nibble
vp_v4n:       db 0
vp_planar:    db 0                      ; 1 Mode X's planes, 2 VGA4's bits
vp_sub2:      db 0                      ; ...with list maps and colour leads
vp_m3on:      db 0                      ; the GC is in write mode 3 (98.1.3.2.4)
vp_m3c:       db 0                      ; ...and the colour sub-record's colour
vp_m3ok:      db 0                      ; this card HAS write mode 3 (a VGA)
vp_plsp:      dw 0                      ; ...and a RAM image's spacing, paras
vp_kseg:      dw 0                      ; vp_kmove: the keeper's plane
vp_s_cga:     db 'CGA  ', 0
vp_s_herc:    db 'Herc  ', 0
vp_s_vga:     db 'VGA  ', 0
vp_s_vga8:    db 'VGA 256  ', 0
vp_s_modex:   db 'Mode X  ', 0
vp_s_c160:    db 'CGA 16  ', 0
vp_s_c512:    db 'CGA 512  ', 0
vp_c5names:   dw vp_s_c5old, vp_s_c5new, vp_s_c5both
vp_s_c5old:   db 'CGA 512 old  ', 0
vp_s_c5new:   db 'CGA 512 new  ', 0
vp_s_c5both:  db 'CGA 512 either  ', 0
vp_s_cga4:    db 'CGA 4  ', 0
vp_s_txtm:    db 'Text  ', 0
vp_s_txtc:    db 'Text colour  ', 0

vp_s_file:    db 'File: ', 0
vp_s_none:    db 'Open a .V88 to play it', 0
vp_s_ready:   db 'Space plays and pauses; Esc stops', 0
vp_bknames:   dw vp_s_bk0, vp_s_bk1, vp_s_bk2, vp_s_bk3, vp_s_bk4, vp_s_bk5
              dw vp_s_bk6, vp_s_bk7, vp_s_bk8, vp_s_bk9, vp_s_bk10
vp_s_bk0:     db 'No bank: the disk alone', 0
vp_s_bk1:     db 'Banked in XMS, ', 0
vp_s_bk2:     db 'Banked in EMS, ', 0
vp_s_bk3:     db 'EMS in place, ', 0
vp_s_bk4:     db 'Held whole in XMS', 0
vp_s_bk5:     db 'XMS bank idle: the speaker', 0
vp_s_bk6:     db 'EMS too slow: the disk alone', 0
vp_s_bk7:     db 'XMS in place (LOADALL)', 0
vp_s_bk8:     db 'XMS bank in place (LOADALL), ', 0
vp_s_bk9:     db 'XMS in place (386)', 0
vp_s_bk10:    db 'XMS bank in place (386), ', 0
vp_s_lydrew:  db 'Layer drew ', 0
vp_s_lyoff:   db 'Layer off: key ', 0
vp_s_lyofb:   db 'Layer off: bench ', 0
vp_s_lyits:   db ' ms, its ', 0
vp_s_lynt:    db 'Layer off: not timed', 0
vp_s_lydsk:   db 'Layer off: disk ', 0
vp_s_lyrm:    db 'Layer off: no room, ', 0
vp_s_lyrms:   db ' KB short', 0
vp_s_lydits:  db ' KB/s, its ', 0
vp_s_lylate:  db ' missed, ', 0
vp_s_lylt:    db ' late', 0
vp_s_notv88:  db 'Not a .V88 video', 0
vp_s_ver:     db 'A newer .V88 than this player', 0
vp_s_bad:     db 'This .V88 is damaged', 0
vp_s_long:    db 'Over 65,535 frames: too long', 0
vp_s_io:      db 'The disk could not be read', 0
vp_s_mem:     db 'Not enough memory to play it', 0
vp_s_needs:   db 'Needs ', 0
vp_s_kbnd:    db ' KB of memory, ', 0
vp_s_kbfree:  db ' KB free', 0
vp_s_refused: db 'The screen could not be taken', 0
vp_s_badsp:   db 'Stopped: a damaged super-packet', 0
vp_s_badrec:  db 'Stopped: a damaged frame', 0
vp_s_nocga:   db 'Made for CGA; not on this screen', 0
vp_s_noherc:  db 'Made for Hercules; not this screen', 0
vp_s_novga:   db 'Made for VGA; not on this screen', 0
vp_s_hpage:   db 'No Hercules page 2: a colour card', 0
vp_s_novga8:  db '256 colours: a VGA, full screen', 0
vp_s_nocol:   db 'CGA colour: needs a CGA or VGA', 0
vp_s_ontab:   dw vp_s_onvga, vp_s_onherc, vp_s_oncga, vp_s_onega
vp_s_onvga:   db 'Plays full screen on the VGA', 0
vp_s_onherc:  db 'Plays full screen on the Hercules', 0
vp_s_oncga:   db 'Plays full screen on the CGA', 0
vp_s_onega:   db 'Plays full screen on the EGA', 0
vp_s_nocmp:   db 'Composite CGA colour: needs a CGA', 0
vp_s_colfs:   db 'CGA colour: plays full screen', 0
vp_s_notxt:   db 'Colour text: needs CGA, EGA or VGA', 0
vp_s_cpcga:   db 'Made for CGA: plays via a copy', 0
vp_s_cpherc:  db 'Made for Herc: plays via a copy', 0
vp_s_cpvga:   db 'Made for VGA: plays via a copy', 0
vp_s_fps:     db ' fps  ', 0
vp_s_kbs:     db ' KB/s'            ; ...and ', ' is its tail, twice over
vp_s_lymis:
vp_s_ticks:   db ', ', 0
vp_audnames:  dw vp_s_silent, vp_s_pcm8, vp_s_adpcm
vp_mwnames:   dw vp_s_muted, vp_s_mdsp, vp_s_spkfast, vp_s_mbank ; vp_mwhy's
vp_bcmd:      db 0, 3, 1, 4, 6, 7, 5    ; the File menu's item each BUTTON runs
                                    ; (vp_onup): Open, the key before, Play,
                                    ; the key after, Repeat, Mute, Info
vp_s_silent:  db 'silent', 0
vp_s_pcm8:    db 'sound PCM8', 0
vp_s_ahead:   db ' ahead', 0
vp_s_adpcm:   db 'sound ADPCM4', 0
vp_s_spkon:   db ', speaker', 0
vp_s_cvx:     db ', Covox', 0
vp_s_muted:   db ', muted', 0
vp_s_mdsp:    db ', muted: DSP 4', 0         ; (35 columns: 16 left here)
vp_s_spkfast: db ', muted: fast', 0
vp_s_mbank:   db ', muted: the bank', 0
vp_s_nokeys:  db 'No keyframes: plays from the start', 0
vp_s_bigkeys: db 'Keys too big here: from the start', 0
vp_s_start:   db 'At the start; keys ', 0
vp_s_fromk:   db 'At key ', 0
vp_s_comma:   db ', ', 0
vp_s_kbad:    db 'That keyframe could not be read', 0
vp_s_pausedat: db 'Paused at ', 0
vp_s_drew:    db 'Drew ', 0
vp_s_xin:     db 'Into XMS: ', 0
vp_s_xheld:   db 'Held in XMS: ', 0
vp_s_xbank:   db 'XMS bank: ', 0
vp_s_ebank:   db 'EMS bank: ', 0
vp_s_kb:      db ' KB', 0
vp_s_stall:   db ', stalls ', 0
vp_s_pause:   db ', pauses ', 0
vp_s_late:    db 'Late ', 0
vp_s_room:    db 'Making room for the play...', 0
%ifdef VP_DIAG
vp_s_heap:    db 'Heap ', 0
%ifndef VP_RDLOG
vp_s_tdsk:    db 'Disk ', 0
vp_s_tln:     db ' KB/s, line ', 0
vp_s_tkey:    db '; key ', 0
%endif
vp_s_krun:    db 'K run, ', 0
vp_s_kfree:   db 'K free at Play', 0
vp_s_ring:    db 'Ring ', 0
vp_s_kk:      db 'K; ', 0
vp_s_keep:    db 'keep ', 0
vp_s_snd:     db ' snd ', 0
vp_s_lead:    db 'Lead ', 0
vp_s_atf:     db ' a'               ; ...and 't f' is its tail
vp_s_tf:      db 't f', 0
vp_s_hgap:    db '; hook gap ', 0
vp_s_dry:     db 'Dry ', 0
%ifndef VP_RDLOG
vp_s_sdry:    db 'Spkr dry ', 0
vp_s_sdt:     db ' times, ', 0
vp_s_ms:      db ' s', 0
%endif
vp_s_most:    db ' stream; most ', 0
%endif
vp_s_want:    db ' ticks'           ; ...and ' of ' is its tail
vp_s_of:      db ' of ', 0

; --- state ------------------------------------------------------------------------
vp_kmax:      dw VP_KBIG            ; the ring's most slots (a test may lower it)
vp_rneed:     db 0                  ; the slots the stream's bursts assume
vp_rshort:    db 0                  ; ...and this play has fewer: say so once
vp_stopat:    dw 0xFFFF             ; the gate's hold: stop before this frame
vp_win:       dw 0
vp_msg:       dw vp_s_none          ; (what the card says before a file)
vp_errmsg:    dw 0
vp_argdir:    dw 0
vp_argvol:    db 0
vp_argpend:   db 0
vp_abon:      db 0
vp_ok:        db 0
vp_fsdisp:    db 0                  ; 0 it plays on the window's display, n
                                    ; full screen on display n-1 (98.3.21)
vp_fskind:    db 0                  ; ...that display's VID_* kind
vp_loaded:    db 0
vp_played:    db 0
vp_mode:      db 0
vp_pwb:       dw 80                 ; the Preview's bytes a row (98.4)...
vp_ph:        dw 200                ; ...and its rows: the canvas's, shown
vp_rs:        db 0                  ; ...this many times over, as a shift
vp_flip:      db 0                  ; Mode X page flipping (98.3.8)...
vp_npg:       db 0                  ; ...page-ahead ON N PAGES, N (98.3.8.4)
vp_inkey:     db 0                  ; ...decoding a key's chain (98.3.8.6)
vp_krn:       db 0                  ; ...the page pool's references kept,
vp_kref:      times VP_KREFS * 8 db 0   ; each the page, at(32) and n
vp_fpg:       db 0                  ; ...FLIPPED AND BUILT AHEAD (98.3.8.5)
vp_fphi:      db 0                  ; ...a record's y1 high byte
vp_fpw:       db 0                  ; ...a wait owed before vp_fpold is written
vp_fpold:     dw 0                  ; ...the page the flip left
vp_mx200:     db 0                  ; MODEX at 320 x 200 (98.1.3.1.1)
vp_pgk:       db 0                  ; ...a key's pages still to draw
vp_page:      dw VP_PAGE            ; ...a page, in plane bytes (a screen's
                                    ; rows x 80 for VGA4, 98.1.3.2.1)
vp_screen:    db 0                  ; VGA4's screen (98.1.3.2.1), 0: 12h's
vp_scrows:    dw 0                  ; ...its rows, and the plane bytes of
vp_scwb:      dw 0                  ; a row it shows
vp_scrtab:    dw 480, 80            ; 0: mode 12h, the desktop's
              dw 200, 40            ; 1: 320 x 200, mode 0Dh
              dw 240, 40            ; 2: 320 x 240, 0Dh on 480 lines
              dw 350, 80            ; 3: 640 x 350, 12h on mode 10h's lines
              dw 400, 80            ; 4: 640 x 400, 12h on 400 lines
              dw 200, 40            ; 5: 320 x 200 NARROW, 0Dh as it is
              dw 240, 40            ; 6: 320 x 240 NARROW (98.1.3.2.5)
vp_std16:     db 0, 0, 0, 0, 0, 42, 0, 42, 0, 0, 42, 42, 42, 0, 0, 42
              db 0, 42, 42, 21, 0, 42, 42, 42, 21, 21, 21, 21, 21, 63, 21
              db 63, 21, 21, 63, 63, 63, 21, 21, 63, 21, 63, 63, 63, 21, 63
              db 63, 63          ; the EGA's sixteen, the DAC's six bits
vp_vtimes:    dw 0, 0, vp_vt240, vp_vt350, vp_vt400, 0, vp_vt240
                                    ; ...each screen's vertical timing, or
                                    ; none: the misc output, then CRTC
                                    ; (index, value) pairs to a 0
vp_vt240:     db 0xE3               ; 0Dh on 480 lines, each row two: Mode
              db 0x06, 0x0D, 0x07, 0x3E, 0x10, 0xEA, 0x12, 0xDF
              db 0x15, 0xE7, 0x16, 0x06, 0x11, 0xAC, 0
                                    ; X's own vertical values (FSXM_MODEX)
vp_vt200m:    db 0x63               ; MODEX on 13h's own 400 lines, each row
              db 0x06, 0xBF, 0x07, 0x1F, 0x10, 0x9C, 0x12, 0x8F
              db 0x15, 0x96, 0x16, 0xB9, 0x11, 0x8E, 0
                                    ; two (CR09's 41h, FSXM_MODEX's, kept):
                                    ; 320 x 200 unchained, four pages
vp_vt350:     db 0xA3               ; 12h on mode 10h's 350 lines
              db 0x06, 0xBF, 0x07, 0x1F, 0x09, 0x40, 0x10, 0x83
              db 0x12, 0x5D, 0x15, 0x63, 0x16, 0xBA, 0x11, 0x85, 0
vp_vt400:     db 0x63               ; 12h on 400 lines, a line a row: the
              db 0x06, 0xBF, 0x07, 0x1F, 0x09, 0x40, 0x10, 0x9C
              db 0x12, 0x8F, 0x15, 0x96, 0x16, 0xB9, 0x11, 0x8E, 0
                                    ; text mode's vertical values
vp_v4xl:      db 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
                                    ; a VGA4 pixel's colour in the desktop's
                                    ; sixteen, for the poster (98.4.5)
vp_poff:      dw 0                  ; ...the page being drawn...
vp_foff:      dw 0                  ; ...the one on the glass...
vp_pgsi:      dw 0                  ; PAGE-AHEAD (98.3.8.3): a record's start
vp_pgend:     dw 0                  ; ...and past its lists
vp_pgfl:      db 0                  ; ...a flip in this record (vc_setup after)
vp_pgclip:    db 0                  ; ...vo_clip, while the other page is drawn
vp_shown:     dw 0                  ; the start address vp_show last wrote
vp_kpo:       dw 0                  ; ...the one vp_kmove uses...
vp_prevseg:   dw 0                  ; ...and the last record's copy, its
vp_flcw:      dw 80 * VP_LCW        ; a row copied, in record bytes (a test
                                    ; may set 0: always the copy)
vp_pry0:      dw 0                  ; the last record's band, for its copy
vp_pry1:      dw 0                  ; off the glass (98.3.8.1)
vp_prevn:     dw 0                  ; bytes (0: none owed; FFFFh: not kept,
                                    ; its band copied off the glass)
vp_palo:      dd 0                  ; VGA8: the palette's offset...
vp_p8seg:     dw 0                  ; ...and its claim, 0 none (vp_rdpal)
vp_pevo:      dd 0                  ; A PALETTE A SCENE (98.1.3.2.3): the
vp_pevn:      dw 0                  ; table's offset and entries, its claim
vp_pevseg:    dw 0                  ; (0: none), its bytes, and the next
vp_pevb:      dw 0                  ; entry to load - an offset in the claim
vp_pevp:      dw 0
vp_layout:    db 0
vp_pixfmt:    db 0
vp_pitper:    db 0                  ; periods a frame, this play
vp_pitper0:   db 0                  ; ...and the file's
vp_name:      times 13 db 0
vp_title:     times VP_COLS + 1 db 0
vp_frames:    dw 0
vp_rate:      dw 0
vp_spf:       dw 0
vp_abytes:    dw 0
vp_pitdiv:    dw 0
vp_wb:        dw 80                 ; (640 x 200 until a file is open:
vp_h:         dw 200                ; vp_entry's first layout)
vp_sp0:       dw 0, 0
vp_sp0n:      dw 0
vp_clsec:     dw 0
; the play
vp_ring:      dw 0
vp_rclm:      dw 0                  ; ...the claim it starts in (THE GAP)
vp_spcap:     dw 64                 ; a super-packet's sectors at most, and the
vp_msl:       dw 1                  ; ring's mirror slots (98.1.4.1)
vp_k:         dw 1                  ; the ring's slots (1 before any play: vp_slot answers 0)
vp_vseg:      dw 0
vp_org:       dw 0
vp_t0:        dw 0
vp_mtk:       dw 0                  ; the tick the bracket's mode changed at
vp_msync:     db 0                  ; 1: a play waits out the re-sync (98.3.20)
vp_dt:        dw 0
vp_lc:        dw 0                  ; chunks loaded - the one shared word
vp_pc:        dw 0                  ; the hook's super-packet: chunk...
vp_po:        dw 0                  ; ...offset...
vp_psec:      dw 0                  ; ...sectors (0 = the end)
vp_nsec:      dw 0                  ; the one after it
vp_fleft:     dw 0                  ; frames left in it
vp_rofs:      dw 0                  ; the next record, into it
vp_cskip:     dw 0                  ; records still to step over (98.3.9)
vp_pgen:      dw 0                  ; seams this cursor has taken
vp_owed:      dw 0
vp_done:      dw 0
vp_stall:     dw 0
vp_late:      dw 0
vp_ready:     db 0
vp_end:       db 0
vp_eof:       db 0
vp_err:       db 0
vp_held:      db 0
vp_thmin:     db 1                  ; (the Preview's least scale: TH_SIZE)
vp_thnk:      db 0                  ; (the sets stored: 1 or 2)
vp_thkm:      db 0                  ; (the set read: 1 black and white)
vp_thhv:      db 0                  ; (its halvings to the layout's scale)
vp_p4:        db 0                  ; THE POSTER is packed for BLIT4 (1) or
vp_pthm:      db 0                  ; BLIT1's rows; and a stored picture (1)
vp_thow:      dw 0                  ; (the box's picture, as it is halved)
vp_thdst:     dw 0                  ; (where vp_dphalf writes)
vp_pqx:       dw 0                  ; vp_p4planes: the box's x, the next
vp_pqy:       dw 0                  ; chunk's y, the width, the rows left,
vp_pqw:       dw 0                  ; the next source row and its segment
vp_pqr:       dw 0                  ; and stride, a plane's bytes a row,
vp_pqsrc:     dw 0                  ; the rows a chunk, this chunk's rows
vp_pqseg:     dw 0                  ; and its plane step
vp_pqst:      dw 0
vp_pqnb:      dw 0
vp_pqk:       dw 0
vp_pqn:       dw 0
vp_pqstp:     dw 0
vp_thoh:      dw 0
vp_thtab:     dd 0                  ; DESKTOP POSTERS (98.1.3.11): the table,
vp_thn:       dw 0                  ; its entries (0: none), the stored
vp_thw:       dw 0                  ; width and height, the scale and its
vp_thh:       dw 0                  ; shift, the flags, a custom one wanted,
vp_thsc:      db 0                  ; an entry in hand, its unpacked bytes,
vp_thsh:      db 0                  ; where it went, and its rows' bytes
vp_thfl:      db 0
vp_thcp:      db 0
vp_dcust:     db 0                  ; ...and the box shows it
vp_the:       dw 0, 0, 0, 0
vp_thu:       dw 0
vp_thseg:     dw 0
vp_thsb:      dw 0
vp_own:       db 0                  ; byte 37 7: each record against its own
                                    ; page, none brought up (VIDEO-SEEN-PLAN 3b)
; the shadow (SPEC.md 98.3.2) and the burst (98.3.3)
vp_shadow:    db 0                  ; this file plays through the shadow
vp_burst:     db 0                  ; the colour burst was turned on
vp_cgapal:    db 0                  ; CGA4: the palette byte (98.1.3.3)
vp_c16lum:    db 0, 1, 6, 8, 3, 5, 6, 11, 6, 7, 12, 13, 9, 10, 15, 17
; TEXT's poster (vp_tmono): the codes the machine has no glyph for that the
; encoder writes - a code, then its quadrants' lit dots, top-left, top-right,
; bottom-left, bottom-right (tools/os88txtfont.py's BLOCK_QUADS)
vp_tquadt:    db 0x00, 0, 0, 0, 0
              db 0xB0, 4, 4, 4, 4
              db 0xB1, 8, 8, 8, 8
              db 0xB2, 12, 12, 12, 12
              db 0xDB, 16, 16, 16, 16
              db 0xDC, 0, 0, 16, 16
              db 0xDF, 16, 16, 0, 0
              db 0xDD, 16, 0, 16, 0
              db 0xDE, 0, 16, 0, 16
VP_TQN        equ ($ - vp_tquadt) / 5
vp_nib:       db 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
vp_tqok:      db 0                  ; the TEXT poster has the machine's
                                    ; glyphs (vp_tqg..vp_tql, the desktop's
                                    ; scratch: this byte keeps the hook's
                                    ; words below on their parity)
vp_c4sets:    db 2, 4, 6, 3, 5, 7, 3, 4, 7  ; CGA4's three sets' colours 1-3
vp_c16crt:    db 4, 127, 5, 6, 6, 100, 7, 112, 9, 1, 10, 0x20  ; SPEC.md 88.15.2
vp_tlay:      db 0                  ; the screen's layout (= [vp_layout] native)
              db 0
vp_caps:      dw 0
vp_ty0:       dw 0                  ; the canvas's top row on the screen
vp_tx0:       dw 0                  ; ...and its left byte
vp_shseg:     dw 0
vb_s:         dw 0                  ; vp_rsfirst: the shadow's row, and
vb_sd:        dw 0, 0, 0, 0         ; the step from it by the row mod 4...
vb_t:         dw 0                  ; ...and the screen's
vb_td:        dw 0, 0, 0, 0
vb_n:         dw 0                  ; vp_blit: the canvas's bytes a row
vp_fruns:     db 0                  ; the file's records carry blit runs
vp_lfull:     db 0                  ; LIVE (98.3.10.2): blit the band whole
vx_n:         db 0                  ; ...else these runs (vx_run, in the
                                    ; desktop's scratch): y0, y1, x0, x1
vp_c16st:     db 0                  ; C160: the screen, not the shadow, is
                                    ; the picture (98.3.12.1)
vp_dy0:       dw 0                  ; the band the next copy covers
vp_dy1:       dw 0
vp_fcap:      dw 0                  ; frames a hook call may draw
vp_due:       dw 0                  ; with sound: the frames due, last call
vp_skipn:     db 0                  ; shadow copies skipped in a row
              db 0
; the sound (SPEC.md 98.3.1)
vp_audio:     db 0                  ; the file's: 0 none, 1 PCM8, 2 ADPCM4
vp_nosnd:     db 0                  ; 1: play silent whatever the machine has
vp_mute:      db 0                  ; MUTED (98.3.17): no sound run at all,
vp_mwhy:      db 0                  ; ...defaulted there: 1 ADPCM4 on a DSP
                                    ; 4.xx, 2 PCM8 past VP_SPKMAX on an 8088
vp_umute:     db 0                  ; ...and the user's own last choice
%if vp_mwhy != vp_mute + 1
  %error "vp_reask keeps vp_mute and vp_mwhy as ONE word: they must be adjacent"
%endif
vp_unmq:      db 0                  ; unmuted in the window: play on, synced
vp_skhere:    db 0                  ; a seek to the frame on the glass
                                    ; over the speaker (S, 98.3.15)
vp_snd:       db 0                  ; this play has the card
vp_hand:      db 0
vp_sopn:      db 0                  ; the stream is open (and owes a close)
vp_sflag:     db 0
vp_afn:       db 0                  ; the silence byte
vp_spkpwm:    db 0                  ; the PCM8 is speaker counts (98.1.1.3)
vp_spkp:      db 1                  ; ...made for this many pulses a sample
vp_aseg:      dw 0                  ; the ring, and its control words
vp_tdr:       dw 0
vp_szero:                           ; --- zeroed at every open ---
vp_atot:      dw 0                  ; bytes queued, free-running
vp_alast:     dw 0                  ; the card's consumed count, last seen
vp_aplay:     dw 0, 0               ; ...and summed, 32 bits
vp_afr:       dw 0                  ; frames whose audio is queued
vp_apend:     dw 0                  ; the end's silence runs to here
vp_afinal:    dw 0                  ; ...and the sound itself to here
vp_syncf:     dw 0                  ; frames played at the last word
vp_syncp:     dw 0                  ; [vp_pers] then
vp_pers:      dw 0                  ; periods, free-running
vp_a0:        dw 0                  ; stream bytes before frame 0's
vp_pause:     dw 0                  ; times the card ran dry
vp_blk:       dw VP_BLOCK           ; the card's block (vp_sblk)
vp_bflg:      db 0                  ; ...as verb 0's flag bits
vp_skmax:     dw 0                  ; most frames the picture trailed
vp_gap:       dw 0                  ; most periods between two hook calls
vp_aend:      db 0                  ; 1 the end's silence queued, 2 stopped
              db 0
VP_SZERO      equ $ - vp_szero
vp_acap:      dw 0
vp_acnt:      dw 0
va_pc:        times VP_CURW dw 0    ; the audio cursor (vp_next)
; REPEAT (SPEC.md 98.3.9)
vp_rep:       db 0                  ; Repeat is on
vp_lkind:     db 0                  ; how a lap joins: 1 the seam record, 2
                                    ; keyframe 0, 0 the stream's start
vp_lL:        dw 0                  ; the seam's: the frame it shows...
vp_loff:      dw 0, 0               ; ...where it is, how long...
vp_llen:      dw 0
vp_lkb:       dw 0                  ; ...its read, in KB
vp_lsp:       dw 0, 0               ; ...and where frame L+1 is
vp_lsecs:     db 0
vp_lidx:      db 0
vp_nseam:     db 0                  ; vp_next's record was the seam
vp_wkind:     db 0                  ; THE ARMED SEAM: its kind...
vp_wgen:      dw 0                  ; ...the seams armed so far...
vp_wL:        dw 0                  ; ...the frame it shows...
vp_wsc:       dw 0                  ; ...its chunk and offset in the ring...
vp_wso:       dw 0
vp_wlen:      dw 0
vp_wpc:       dw 0                  ; ...and the cursor after it
vp_wpo:       dw 0
vp_wpsec:     dw 0
vp_widx:      dw 0
vp_flive:     db 0                  ; LIVE (98.3.10): the file may...
vp_target:    db 0                  ; ...the rendition's screen...
vp_livem:     db 0                  ; ...vp_sstart is starting a live play...
vp_lsess:     db 0                  ; ...the session IS one...
vp_lrun:      db 0                  ; ...and the worker is to play it...
vp_lend:      db 0                  ; ...or found its end...
vp_hired:     db 0                  ; ...the worker, hired...
vp_ltk:       dw 0                  ; ...the tick it last looked at...
vp_lacc:      dw 0, 0               ; ...PIT counts owed...
vp_lper:      dw 0, 0               ; ...and a frame's
vp_lrem:      dw 0                  ; ...PIT counts short of a period (98.3.10.1)
vp_ldrain:    db 0                  ; ...and the picture done, the sound not
vp_rend:      db 0                  ; the rendition vp_parse reads (98.1.7)
vp_nrend:     db 0                  ; ...of this many
vp_resid:     db 0                  ; RESIDENT: played from memory
vp_bk:        times 13 db 0         ; the rendition's block's fields...
vp_abk:       times 13 db 0         ; ...and the audio block's
vp_rblk:      dw 0                  ; the loaded block's claim, kept while the
vp_rablk:     dw 0                  ; file is open; the audio block's
vp_rbseg:     dw 0                  ; its first record, as a paragraph and
vp_rboff:     dw 0                  ; an offset under 16
vp_rsseg:     dw 0                  ; the seam record (LOOPREC)...
vp_rsoff:     dw 0
vp_rlseg:     dw 0                  ; ...and frame L+1's
vp_rloff:     dw 0
vp_vseq:      dw 0                  ; frames drawn, every lap counted - the
vp_aseq:      dw 0                  ; card's clock - and audio frames queued
vp_afr0:      dw 0                  ; the frame the audio cursor was set at
; the Preview (SPEC.md 98.4)
vp_cx0:       dw 0                  ; the content's origin, as vp_track saw it
vp_cy0:       dw 0
vp_clb:       dw 0                  ; a cluster, bytes
vp_rgb:       dw 0                  ; ...and vp_rdat's granule: it, or a sector
vp_slen:      dw 0, 0               ; the stream's bytes
vp_nkeys:     dw 0                  ; keyframes (0: no Preview, no seek)
vp_nkwhy:     db VOK_NOKEY          ; ...and why 0: none, or past one read
vp_ktab:      dw 0, 0
vp_poster:    dw 0                  ; the header's poster, FFFFh none
vp_kmaxb:     dw 0                  ; the largest keyframe record
vp_kbkb:      dw 0                  ; ...and the claim that reads one, KB
vp_kekb:      dw 0                  ; ...and one that reads a table entry, KB
vp_kfo:       dd 0                  ; KEYS IN PARTS (98.1.3.8): the read in
vp_kpn:       dw 0                  ; hand's place in the file and length,
vp_knext:     dw 0                  ; the next one's length, and whether
vp_kmore:     db 0                  ; there is a next one
%ifdef VP_DIAG
vp_mrun0:     dw 0                  ; the heap as Play found it: the largest
vp_mfre0:     dw 0                  ; claim and all that is free, KB (98.3)
vp_mrun:      dw 0                  ; ...and what the ring was sized from
vp_mkeep:     db 0                  ; ...after the keeper, KB
vp_lmin:      dw 0xFFFF             ; THE READER'S DIAGNOSTIC (98.3): the least
vp_lminf:     dw 0                  ; lead in chunks, and the frame it was at
vp_pstrm:     dw 0                  ; pauses with the sound waiting on the stream
vp_pmax:      dw 0                  ; the longest pause, ticks, and its frame
vp_pmaxf:     dw 0
vp_pt0:       dw 0                  ; ...this one's start
vp_pin:       db 0                  ; ...in one: bit 0, and bit 1 the stream's
vp_astv:      db 0                  ; the sound is waiting on an unread record
%endif
vp_nokeep:    db 0                  ; this play goes without its keeper (98.3.19):
                                    ; 1 under pressure, 2 the poster is it
vp_pcv:       db 0                  ; ...and the poster holds this session's frame
vp_cpent:     db 0                  ; vp_sstart from Play (1) or F (2): may post
vp_cpq:       db 0                  ; ...and posted already, for this press
vp_cppost:    db 0                  ; ...just now: vp_sstart returns into it
vp_cpgo:      db 0                  ; the wake starts it again: 1 Play, 2 F
%ifdef VP_DIAG
vp_msnd:      db 0                  ; ...and the card's ring, KB (VPDIAG=1)
vp_prseg:     dw 0                  ; THE PLAY'S PROFILE: its claim,
vp_prn:       dw 0                  ; ...the frames in it,
vp_prta:      dw 0                  ; ...channel 2 at a frame's start
vp_prtb:      dw 0                  ; ...and at its record
vp_pr61:      db 0                  ; ...port 61h as it was
vp_prfn:      db 'VPPROF1.DAT', 0
; THE CARD'S TRAILER's words (98.3.22.3), in this order - tools/os88vfit.py
; DI_NAMES is the other copy. A byte's word carries its neighbour above it
vp_ditab:     dw vp_mrun0, vp_mfre0, vp_k, vp_rneed, vp_mrun, vp_mkeep
              dw vp_msnd, vp_lmin, vp_lminf, vp_gap, vp_pstrm, vp_pause
              dw vp_pmax, vp_pmaxf, vp_stall, vp_late, vp_dt, vp_done
              dw vp_base, vp_frames, vp_lysp, vp_lysp + 2, vp_lydkb
              dw vp_lydln, vp_lytm, vp_lytl, vp_rate, vp_blk, vp_tier
              dw vp_rshort, vp_kr, vp_xbn, vp_ring, vp_rclm, vp_xbase
              dw vp_xbase + 2, vp_xgap, vp_xgok, xg_t0, xg_bt, xg_best
              dw xg_tt, xg_tt + 2, xg_tt + 4, xg_tt + 6, xg_tt + 8
              dw xg_tt + 10
vp_ditab_end:
VP_DIN      equ (vp_ditab_end - vp_ditab) / 2 + 2  ; ...and CS, SS
VP_DIL      equ 8 + 2 * VP_DIN
vp_prna:      dw 0                  ; ...its frame slots,
vp_prnl:      dw 0                  ; ...the reads logged,
%ifdef VP_RDLOG
vp_prlp:      dw 0                  ; ...and where the next goes
%endif
vp_prbd:      dw 0                  ; ...and damaged super-packets seen
                                    ; THE HEADER (written into the claim):
                                    ; 'VPPR', version 2; +6 frames, +8 pit
                                    ; divisor, +10 periods a frame, +11
                                    ; sound, +12 flip, +13 einp, +14 xip,
                                    ; +15 the card's bank kind, +16 the
                                    ; file's name, +29 late; +32 frame
                                    ; slots, +34 reads logged, +36 damaged
                                    ; super-packets, +38 the first one's
                                    ; chunk, +40 offset, +42 sectors, +44
                                    ; [vp_lc], +46 [vp_kr]
%endif
vp_sel:       dw 0                  ; the key Play starts at; 0 = the start
vp_kload:     dw 0xFFFF             ; the key vp_ke holds
vp_ke:        times 16 db 0         ; its table entry (98.1.3)
vp_rdseg:     dw 0                  ; vp_rdat's destination
vp_rdend:     dd 0
vp_kshd:      dw 0
vp_pseg:      dw 0                  ; the poster: its claim, 0 = none...
vp_pbw:       dw 0                  ; ...its bytes a row...
vp_ppx:       dw 0                  ; ...its width in pixels...
vp_pdw:       dw 0                  ; ...of which the box shows this many...
vp_prows:     dw 0                  ; ...the rows shown...
vp_pskip:     dw 0                  ; ...from this offset
vp_px:        dw 0                  ; where the last paint put it (the gate
vp_ppoff:     dw 0                  ; the poster's row less the content's,
                                    ; as last painted
vp_py:        dw 0                  ; reads the screen there)
vp_ploads:    dw 0                  ; posters made: the gate waits on it
vp_bx1:       dw 0                  ; the box's inside, screen
vp_by1:       dw 0
vp_bx2:       dw 0
vp_by2:       dw 0
vp_tx1:       dw 0                  ; the bar's inside
vp_tx2:       dw 0
vp_dkey:      dw 0xFFFF             ; the key the picture is, FFFFh none
vp_kblk:      db 0                  ; a first bracket's black, owed (98.3.7.1)
vp_pscale:    dw 0                  ; ...and the scale it was made at
; the layout (98.4.1), vp_layfit's
vp_card:      db 0                  ; the user's: the info card out
vp_lcard:     db 0                  ; ...and whether it is (layout B forces it)
vp_lbin:      db 0                  ; 1: the buttons are in the card
vp_relay:     db 0                  ; the wake owes a layout and a resize
vp_ps:        dw 2                  ; the picture's scale: 1, 2 or 4
vp_lpw:       dw 0                  ; the picture at it, pixels and rows
vp_lbw:       dw 0                  ; the box: its width and height
vp_lbh:       dw 0
vp_lbary:     dw 0                  ; the bar's frame top, the button row's
vp_lbty:      dw 0
vp_lcardx:    dw 0                  ; the card's x
vp_lcw:       dw 0                  ; the content's size
vp_lch:       dw 0
; the thumb's drag (98.4.2)
vp_drag:      db 0
vp_tier:      db 0                  ; OSAPI_CPU_INFO's AL
vp_tpos:      dw 0                  ; the thumb's offset along the bar
vp_dtk:       dw 0                  ; the tick the last mid-drag load ended
vp_kat_t:     dw 0                  ; vp_keyat's frame and key
vp_kat_i:     dw 0
vp_kat_n:     dw 0
; the paused start and the play's end (98.3.4, 98.3.6)
vp_startp:    db 0                  ; F: into full screen paused
vp_sdefer:    db 0                  ; ...the card started at the first Space
vp_abase:     dw 0                  ; the frame the card's stream starts at
; the session (98.3.7)
vp_sess:      db 0                  ; a play is alive, brackets or none
vp_autop:     db 0                  ; paused by a bracket's end, not the user:
                                    ; the next bracket resumes it
vp_sfirst:    db 0                  ; ...and the next bracket is its first
vp_winm:      db 0                  ; this bracket is in the window
vp_wantwin:   db 0                  ; ...and the next one asks for it
vp_exitr:     db 0                  ; VPX_*: how the bracket ended
vp_stopq:     db 0                  ; vp_sstop's mode
vp_bpause:    db 0                  ; the Play button shows Pause
vp_mbtn:      db 0                  ; the mouse's buttons, last seen
vp_fslay:     db 0                  ; the full screen's layout, mode, and
vp_fsmode:    db 0                  ; whether through the shadow
vp_fsshd:     db 0
vp_dlay:      db 0                  ; the desktop's layout (vp_dinfo)
vp_kdir:      db 0
vp_dtok:      db 0                  ; [vp_dt] is taken
vp_nowin:     db 0                  ; 1: never in the window (a gate's)
vp_dseg:      dw 0                  ; the desktop's framebuffer
vp_keep:      dw 0                  ; the canvas keeper (the shadow, or not)
vp_pdiv:      dw 0                  ; this play's PIT divisor
vp_wtk:       dw 0                  ; the frame the thumb was last asked at
vp_wtx:       dw 0                  ; ...and where it was drawn
; the play's start and its pause (98.3.4, 98.3.5)
vp_base:      dw 0                  ; the first frame the stream draws
vp_krec:      dw 0                  ; the keyframe's record in the ring, or FFFFh
vp_kidx:      dw 0                  ; records to step over after it
vp_ssp:       dw 0, 0               ; the first super-packet this play reads
vp_ssec:      dw 0
vp_upause:    db 0                  ; Space: paused
vp_pfresh:    db 0                  ; ...and the hook has not been called since
vp_skn:       dw 0                  ; SEEKING (98.3.14): presses, signed...
vp_skb:       dw 0                  ; ...from this frame...
vp_sktk:      dw 0                  ; ...the last at this tick
vp_skk:       dw 0                  ; the key it lands on
vp_skwas:     db 0                  ; it was playing
vp_skgo:      db 0                  ; ...and plays on from the key
vp_stepq:     dw 0                  ; FRAME STEPS (, and .): frames the hook
                                    ; draws while paused...
vp_sttgt:     dw 0xFFFF             ; ...and a step back's frame, the seek's
                                    ; landing stepping on to it (FFFFh none)
vp_skwt:      db VP_SKWAIT          ; the wait after the last press, ticks
vp_grey:      db 0                  ; THE GREY GROUND (98.4.8) is on
                                    ; (tests/vidfskeys.py holds a seek here)
vp_skbuf:     times VO_MAXC + 1 db 0
vp_aref:      db 0                  ; ADPCM4's reference byte, this play
vp_sadp:      db 0                  ; 1: the PLAYER decodes ADPCM4 (98.3.17.1)
vp_sscl:      db 0                  ; ...its scale, 0..48
vp_sref:      dw 0                  ; ...its running sample, 0..255
vp_rbytes:    dw 0                  ; the ring's bytes a frame: [vp_abytes],
                                    ; twice it when the player decodes
vp_ahead:     db 0                  ; SOUND AHEAD (98.1.8): A, 0 without
vp_alb:       dw 0                  ; ...A x abytes, a lead's bytes
vp_lead0:     dd 0                  ; ...the start's lead's offset
vp_klead:     dd 0                  ; ...the keys' leads' table, apart, or 0
vp_lpar:      dw 0                  ; ...a lead read past the ring's start
vp_lsrc:      dw 0xFFFF             ; ...a lead read into the ring, or FFFFh
vp_alst:      dw 0                  ; ...its bytes staged in the sound ring
vp_mbuf:      times VP_MBUF db 0    ; vp_fits's "Needs n KB..."
vp_v4p:       db 0                  ; LIVE IN COLOUR (98.3.10.4): BLITP may walk the
                                    ; clip (vp_v4blit; its repack's words are
                                    ; the desktop's scratch, vp_dsk)
vp_cbnd:      dw 0                  ; the canvas's bound in its layout, and
vp_kkb:       db 0                  ; the keeper's KB, 0 none (98.1.7.3)
vp_kneed:     db 0                  ; this session decodes INTO the keeper,
vp_ptk0:      dw 0                  ; the tick it paused at
vp_ptk:       dw 0                  ; ticks paused, this play
vp_rchk:      db 0                  ; ...and the block's writes are checked
                                    ; (after the two words: they keep the
                                    ; parity they had)
vp_cur:       times FSEQ_SIZE db 0
vp_rdcur:     times FSEQ_SIZE db 0  ; vp_rdat's own (os88_rseq), never the stream's
vp_xcur:      times FSEQ_SIZE db 0  ; THE HOLD (98.3.18): its loader's cursor,
vp_xgap:      dw 0                  ; THE GAP (os88xgap.inc): G, 0 none
vp_xgok:      db 0                  ; ...measured, this instance
vp_xsclm:     dw 0                  ; vp_xstep's claim, the read inside it
              XG_DATA
vp_xbase:     dd 0                  ; the block's token,
vp_xcap:      dd 0                  ; its bytes (a KB over the file),
vp_xhave:     dd 0                  ; the bytes that have arrived,
vp_xon:       db 0                  ; there is one,
vp_lwant:     db 0                  ; a live stream's worker wants a feed
vp_bone:      db 0                  ; vp_buttons: this one only (0 all)
vp_xfull:     db 0                  ; ...and all of the file is in it
vp_xbank:     db 0                  ; THE BANK (VIDEO-XMS-PLAN 4): the block
                                    ; is a FIFO of [vp_xbn] 32 KB slots ahead
                                    ; of the ring, not the whole file
vp_xbeof:     db 0                  ; ...its cursor has met the file's end
vp_xbn:       dw 0                  ; ...its slots
vp_xhs:       dw 0                  ; ...the head's slot
vp_xcnt:      dw 0                  ; ...the chunks in it
vp_xtl:       dw 0                  ; ...the bytes of its last, with xbeof
vp_xlo:       dd 0                  ; ...the file offset of its head
vp_xems:      db 0                  ; 1: the bank is EMS pages (98.3.18.6)
vp_einp:      db 0                  ; 1: this session decodes IN PLACE (98.3.18.7)
vp_xip:       db 0                  ; 1: the hold, decoded in place (98.3.18.11)
vp_xact:      db 0                  ; ...a borrow is open (VP_DSDX..VP_DSEND)
vp_xif:       db 0                  ; ...and its caller had IF on (2) or off
vp_xidx:      db 0                  ; ...vp_xpend's last
vp_xchk:      db 0                  ; ...this instance checked: 1 ok, 2 not
vp_xpc:       dd 0                  ; ...the open borrow's DS base, linear
vp_xpend:     times 16 dd 0         ; ...vp_xaddr's handles' bases
vp_xslo:      times VP_KBIG dd 0    ; ...each slot's chunk, its file offset
vp_simseg:    dw 0                  ; ...LA_SIM's shadow (a test build)
vp_simyc:     dw 0                  ; ...the yields that opened a window
%ifndef LA_SIM
              LA_DATA               ; apps/os88la.inc's state
%endif
vp_lbk:       db 0                  ; ...and the last play's bank (vp_bkind)
vp_eslow:     db 0xFF               ; the board's speed (vp_espeed): FFh not
                                    ; measured, 0 RAM's, 1 slow, 2 too slow
vp_er10:      dw 0                  ; ...its time, in tenths of RAM's
vp_xr10:      dw 0xFFFF             ; the pool's KB copied, in tenths of RAM's
                                    ; read (vp_xspeed): FFFFh not measured
vp_htg:       dd 0                  ; THE FILL STRAIGHT INTO XMS (98.3.18.15):
                                    ; where the armed buffer's first byte goes
vp_hmap:      times 8 db 0          ; ...HDD.DRV's map: a bit a sector there
vp_hmapv:     db 0                  ; ...the map is this read's
vp_harmd:     db 0                  ; ...HDD.DRV is armed
vp_hnone:     db 0                  ; ...the door refused for good
vp_hsk:       dd 0                  ; ...sectors not copied up, all plays
vp_noinp:     db 0                  ; 1: never in place (a gate's A/B)
vp_ekr:       dw 0                  ; the in-place ring's slots at most (a gate's)
vp_eq:        db 0                  ; the frame's quarters held: 0Fh or 3
vp_eor:       db 0                  ; ...the recipe: bits ORed into a page,
vp_eport:     dw 0                  ; quarter 0's register, the step to the
vp_estep:     dw 0                  ; next one's
vp_ebase:     dw 0                  ; ...and the handle's first page
vp_kr:        dw 1                  ; the ring's slots for vp_slot: [vp_k],
                                    ; or the board's in place
vp_ehnd:      dw 0                  ; ...their handle
vp_eseg:      dw 0                  ; ...and the frame they are seen through
vp_hxb:       dw 0                  ; THE FILE'S ASK (header 472): the bank its
                                    ; encode assumed, KB, 0 none
vp_lysp:      dd 0                  ; THE LAYER (98.1.9): its first super-
vp_lykt:      dd 0                  ; packet, 0 none; its key table; its KB
vp_lykb:      dw 0
vp_lymax:     dw 0                  ; ...its longest record, and the KB of
vp_lymkb:     dw 0                  ; the mirror that holds its run (98.1.9.8)
vp_lyon:      db 0                  ; ...this session plays it
vp_lyeof:     db 0                  ; ...its last super-packet is read
vp_lybeh:     db 0                  ; ...the hook is behind: it waits
vp_lyseg:     dw 0                  ; ...its slots' claim, [vp_lyns] of them
vp_lyns:      dw 0
vp_lyrd:      dw 0                  ; ...slots read, and the hook's done with
vp_lyuse:     dw 0                  ; (both counts, the slot the low bits)
vp_lynx:      dw 0                  ; ...the frame of the hook's next record,
                                    ; and where it is in its chunk (98.1.9.8)
vp_lyo:       dw 0
vp_lygot:     dw 0                  ; ...records drawn, and left out behind
vp_lyskp:     dw 0
vp_lymis:     dw 0                  ; ...and stepped over, read too late
vp_lycur:     times FSEQ_SIZE db 0  ; ...and its reader's cursor
vp_lyord:     db 0                  ; ...read before the base's bank (98.1.9.1)
vp_lysplt:    db 0                  ; ...and the pool split for it (98.1.9.2)
vp_lyneed:    dw 0                  ; ...while the bank holds this KB
vp_lybkb:     dw 0                  ; ...its XMS bank, KB, as the file asks,
vp_lypkb:     dw 0                  ; and its prefill (FFFFh: all)
vp_lyxb:      dd 0                  ; ...the bank: its block,
vp_lyxn:      dw 0                  ; its slots, 0 none,
vp_lyxh:      dw 0                  ; the head's slot, the slots it holds,
vp_lyxc:      dw 0
vp_lyxq:      db 0                  ; and whether this session may copy
vp_pfly:      db 0                  ; the prefill is the LAYER's (vp_lypre)
vp_lytl:      dw 0                  ; key 0's decode on the layer's machine,
vp_lytb:      dw 0                  ; and on the base's (10 us): THE BENCH
vp_ytb:       dw 0                  ; THE BASE'S YARDSTICK (98.3.18.9) - the
                                    ; three IN A ROW, as the two sets
                                    ; vp_bnsel copies them from are
vp_lytm:      dw 0                  ; ...and on this one (vp_lybench),
vp_lyt0:      dw 0                  ; from this tick, this many times
vp_lyn:       dw 0
vp_lyoff:     db 0                  ; ...and it said no
vp_lywhy:     db 0                  ; ...for the DISK, not the CPU (98.1.9.3)
vp_lykbs:     dw 0                  ; THE DISK TEST (98.1.9.3): the layer's
vp_lybks:     dw 0                  ; disk and the base's, KB/s; this one's,
vp_lydkb:     dw 0                  ; timed on the ring's fill; the verdict,
vp_lydv:      db 0                  ; 0 none, 1 yes, 2 no - once a file
vp_dton:      db 0                  ; ...the fill timed now; whole chunks,
vp_dtn:       dw 0                  ; the hook's clock after the first and
vp_dt0:       dd 0                  ; after the last (PIT counts)
vp_dt1:       dd 0
vp_lydln:     dw 0                  ; ...and the line it was held to, KB/s
vp_hper:      dw 0                  ; the hook's periods, every call
vp_lyfl:      db 0                  ; A LAYER TO PLACE (98.1.9.10) at the
vp_lyb:       db 0                  ; ring's top; its slots lent from it (L); K as
vp_lyk0:      dw 0                  ; the machine had it; KB of the header's
vp_lycmp:     dw 0                  ; ring it lent; the bounce for the layer's
vp_lybnc:     db 0                  ; bank claimed; KB short with no flip
vp_lysht:     dw 0
vp_lycv:      db 0                  ; the CPU's verdict on key 0, kept: 0, 1, 2
vp_bnl:       dw 0                  ; THE BENCH (98.1.9.13): the layer's,
vp_bnb:       dw 0                  ; the base's twice (vp_lytl, vp_lytb,
vp_bnb2:      dw 0                  ; vp_ytb's order), 10 us
vp_kytl:      dw 0                  ; ...and key 0's, as parsed: the same
vp_kytlb:     dw 0                  ; order
vp_kytb:      dw 0
vp_bnon:      db 0                  ; ...this session times the bench
vp_bnseg:     dw 0                  ; ...built here
vp_bnlen:     dw 0                  ; ...this long, its check this
vp_bnsum:     dw 0
vp_bnorg:     dw 0                  ; ...the origin, page and box swapped
vp_bnpof:     dw 0                  ; for its decode (vp_bnio)
vp_bnclp:     db 0
vp_bnn:       dw 0                  ; (vp_bngen's row: bytes, list)
vp_bnk:       db 0
vp_bnt0:      dd 0                  ; ...and the bracket's clock at its start
vp_lymx:      db 0                  ; 1: the layer's mirror is the ring's slack
vp_ybq:       db 0                  ; whether the speaker is on trial
vp_hxp:       dw 0                  ; ...and its prefill, KB (0: 10 s at its
                                    ; mean, FFFFh: the bank whole)
vp_pfok:      db 0                  ; 1: the next ring fill is a session's first
vp_pfwait:    db 0                  ; 1: the prefill holds when done (a gate's)
vp_bhold:     db 0                  ; 1: the bracket's loop banks no further,
                                    ; the prefill and a pause still do (a gate's)
vp_pfn:       dw 0                  ; the prefill's slots
vp_bskn:      dw 0                  ; seeks the bank answered (a gate's count)
vp_pft0:      dw 0                  ; ...and the tick it began
vp_pfbuf      equ vp_skbuf          ; the line, composed in the seek's own
                                    ; buffer: each is a call's, made and then
                                    ; copied into the box by vo_show, and the
                                    ; main loop builds one at a time

%include "os88alt.inc"              ; Alt+Enter in the bracket (SPEC.md 11.2.1.1)
%define OS88UI_ABOUT                ; the standard About card (SPEC.md 20.5.1)
%define OS88UI_BIMG                 ; ...and buttons with PICTURES, drawn with
%define OS88UI_NOGLYPH              ; no pixel written twice (SPEC.md 13.8.9),
%define OS88UI_BIMGONLY             ; nothing BUT pictures (no caption, ink or
%include "os88ui.inc"               ; default ring), and no check box or radio
vp_s_buf:     db 'Buffering ', 0    ; (here, past the libraries: eleven bytes
                                    ; in front of them put os88alt_edge, which
                                    ; a frame's pass calls, at an odd shift)

; --- bss: zeroed by the loader, and no bytes of the file ----------------------
vp_pal        equ os88_image_end    ; the SIXTEEN: a VGA4 file's own, or a
                                    ; scene's (VGA8's 256 are a claim's,
                                    ; [vp_p8seg]: vp_rdpal)
vo_sav        equ vp_pal + 48       ; what the full screen's text covers on
                                    ; the FIRST of its two pages (98.3.13) -
                                    ; the second's is a claim, [vo_s1seg],
                                    ; and only a play on two pages or more
                                    ; takes it (98.3.13.2)
vc_pg         equ vo_sav + VO_SAV   ; ...and a 256-byte page -> the first
                                    ; of its rows at or after it (98.3.13.1)
; ...and INSIDE THE SAVE, the desktop's two picture tables (98.4): the save
; is live only in a bracket with the box up (vo_setup clears it at every
; entry, and vo_mode is 0 in the window), the tables only while vp_mkpic
; makes a picture, which runs on the desktop side alone - so each is made
; there, every time (vp_mkdtab, vp_mklum) - and never for VGA4, which
; never needs them
vp_dtab       equ vo_sav            ; the half-scaler's two tables
vp_lum        equ vo_sav + 512      ; VGA8's lumas, 0..16 (or CGA's colours')
; ...and the info card's text, which only the desktop draws (vp_ptext) and
; only the desktop side writes (vp_fmt): a bracket's box clobbers it, so
; EVERY exit from the full screen formats it again before the bracket's
; own repaint reads it (vp_main's .pic and .stop, SPEC.md 53.6 step 4)
vp_lines      equ vo_sav + 768      ; the info panel's text
%if 768 + VP_LINES * VP_LINE > VO_SAV       ; (the tables before it)
  %error "the card's text does not fit a page's save"
%endif
; ...and PAST it, over the page table and on past both, THE DESKTOP'S
; SCRATCH - which is what sizes the bss, being longer than the save and the
; table together (98.3.13.2): words a routine
; writes before it reads them and spends inside the one call, in routines
; the full screen never runs - the ground's holes (vp_ground), the thumb's
; move and a button turned over (vp_wmove, vp_winv: the window's play
; only), Live's band and repack (vp_blitb, vp_v4blit: the window's, under
; the gfx lock), the poster's draw (vp_pposter), the half-scaler's
; arguments (vp_half, which only vp_mkpic calls) and the VGA4 packer's
; (vp_v4pack, the same), the layout's working (vp_layfit), the open's
; (vp_open, vp_rwalk: vp_sstart runs it between brackets) and the hold's
; check (vp_xok, the same). And the mode's record
; (vp_fsi), filled and read at a bracket's door BEFORE the box can be up.
; A box up clobbers every one of them under the save's and the table's
; end, and none is read across a bracket
vp_dsk        equ vp_lines + VP_LINES * VP_LINE
vp_gh         equ vp_dsk            ; the holes: x1, y1, x2, y2 each
vp_gb         equ vp_gh + 16 * 8    ; ...the rect
vp_ghn        equ vp_gb + 8         ; ...how many holes
vp_gy         equ vp_ghn + 2        ; ...the band
vp_gy2        equ vp_gy + 2
vp_gx1        equ vp_gy2 + 2        ; ...and the hole a gap ends at
vp_gx2        equ vp_gx1 + 2
vp_wmt        equ vp_gx2 + 2        ; vp_wmove: each byte's offset, mask
vp_wnx        equ vp_wmt + 16       ; and white bits; the block's new x...
vp_wox        equ vp_wnx + 2        ; ...its old one...
vp_wmn        equ vp_wox + 2        ; ...and the bytes it touches (a byte)
vp_wvr        equ vp_wmn + 1        ; vp_winv's button: its rect...
vp_wvq        equ vp_wvr + 2        ; ...copied
vp_brem       equ vp_wvq + 8        ; vp_blitb's rows left
vp_v4ax       equ vp_brem + 2       ; vp_v4blit's repack: x, y, width,
vp_v4by       equ vp_v4ax + 2
vp_v4cw       equ vp_v4by + 2
vp_v4rl       equ vp_v4cw + 2       ; rows left,
vp_v4nb       equ vp_v4rl + 2       ; bytes a row,
vp_v4bs       equ vp_v4nb + 2       ; the buffer's stride,
vp_v4rc       equ vp_v4bs + 2       ; rows it holds,
vp_v4k        equ vp_v4rc + 2       ; rows this chunk,
vp_v4ps       equ vp_v4k + 2        ; and the plane step
vp_qseg       equ vp_v4ps + 2       ; vp_pposter's picture: its segment,
vp_qbw        equ vp_qseg + 2       ; stride and rows,
vp_qrows      equ vp_qbw + 2
vp_pdh        equ vp_qrows + 2      ; ...and its rows drawn (a gate reads it)
vh_sseg       equ vp_pdh + 2        ; vp_half's image...
vh_lay        equ vh_sseg + 2       ; ...its layout, or FFh linear (a byte)...
vh_sstr       equ vh_lay + 2        ; ...at this stride
vh_wb         equ vh_sstr + 2
vh_h          equ vh_wb + 2
vh_dseg       equ vh_h + 2
vh_owb        equ vh_dseg + 2
vh_y          equ vh_owb + 2
vp_v4ro       equ vh_y + 2          ; vp_v4pack's (vp_mkpic's too): a row's
vp_v4kb       equ vp_v4ro + 8       ; source rows' offsets, the byte cached
vp_v4c        equ vp_v4kb + 8       ; for each, and that byte's four planes
vp_lops       equ vp_v4c + 16       ; vp_layfit's: the scale it started at,
vp_lph        equ vp_lops + 2       ; the picture's rows at the one tried,
vp_lslk       equ vp_lph + 2        ; the slack and gaps (vp_laynorm,
vp_lgap       equ vp_lslk + 2       ; vp_laycomp), and the most the screen
vp_lcwm       equ vp_lgap + 2       ; allows
vp_lchm       equ vp_lcwm + 2
vp_tmp        equ vp_lchm + 2       ; vp_open's header claim, vp_rwalk's mark
vp_rbest      equ vp_tmp + 2        ; vp_open: the best rendition so far, and
vp_rscore     equ vp_rbest + 1      ; its score (bytes)
vp_xsig       equ vp_rscore + 1     ; vp_xok's reference bytes...
vp_xzf        equ vp_xsig + 16      ; ...and its compare (a byte)
vp_fsi        equ vp_xzf + 1        ; the bracket's mode (OSAPI_FSX_MODE)
vp_v4buf      equ vp_fsi + FSI_SIZE ; and Live in colour's repack buffer
                                    ; (vp_v4blit), filled and blitted a few
                                    ; rows at a time in the one call
vx_run        equ vp_v4buf + VP_V4BUF ; and Live's runs, a pass's: gathered
                                    ; by the frames' decodes (vp_lrget) only
                                    ; while Live runs, blitted and spent at
                                    ; its end (vp_lblit), all of it inside the
                                    ; worker's one hold of the lock
vp_tqg        equ vx_run + VX_MAX * 4 ; vp_tmono's (the TEXT poster): the
vp_tqo        equ vp_tqg + 2        ; machine's glyphs - segment, offset,
vp_tqf        equ vp_tqo + 2        ; first code and last (a word's two
vp_tql        equ vp_tqf + 1        ; bytes, written as one) - and its
vp_tscr       equ vp_tql + 1        ; scratch claim
vp_tx         equ vp_tscr + 2       ; vp_pbar's: the thumb's x
vp_ldkb       equ vp_tx + 2         ; vp_ldblk's claim, KB, and segment
vp_ldseg      equ vp_ldkb + 2       ; (vp_sstart's, between brackets)...
vp_lsin       equ vp_ldseg + 2      ; ...vp_ldstored's start in its cluster
vp_lsrem      equ vp_lsin + 2       ; and the bytes still to read or move
vp_etseg      equ vp_lsrem + 4      ; vp_erate's segment (vp_open's)
vp_dskend     equ vp_etseg + 2
%if vp_dskend - vo_sav > VO_SAV + 256 ; (the scratch is the longer: the bss
    OS88_BSS vp_dskend - vp_pal     ; is the palette and it, the save and
%else                               ; the page table inside it)
    OS88_BSS 48 + VO_SAV + 256
%endif
    OS88_IMAGE_END
