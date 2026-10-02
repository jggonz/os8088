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
VP_RL       equ 16384               ; the audio ring (SPEC.md 98.3.1)...
VP_RLCODE   equ 2                   ; ...4096 << 2
VP_BLOCK    equ 2048                ; the card's block: one interrupt each
VP_BLKBPS   equ 11000               ; ...halved while the sound is slower
                                    ; than this many bytes a second (vp_sblk)
VP_SPK      equ 2                   ; [vp_snd]: the SPEAKER plays it (98.3.15)
VP_SPKMAX   equ 8000                ; the fastest PCM an 8088 plays through
                                    ; the speaker (SPEC.md 34.11.4)
VP_AMAX     equ 4                   ; frames of audio a hook call puts in
VP_SKIPMAX  equ 8                   ; shadow copies a play behind may skip
VP_SLOTP    equ VP_CHUNK / 16       ; ...in paragraphs
VP_KMAX     equ 8                   ; a LIVE play's most slots (a power of two)
VP_KBIG     equ 15                  ; ...and any other's (SPEC.md 98.3)
VP_COLS     equ 35                  ; the info panel's text columns
%ifdef VP_DIAG
VP_LINES    equ 12                  ; (the last four the heap's and the
                                    ; reader's: VPDIAG=1, a field diagnostic)
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
V88F_KNOWN  equ V88F_RESIDENT | V88F_LOOPREC | V88F_REPEAT | V88F_LIVE \
                | V88F_RUNS | V88F_SPKPWM | V88F_SPKMUL
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
R_POSTER    equ 14
R_SP0       equ 16
R_SP0N      equ 20
R_SPMAX     equ 22
R_PAL       equ 32                  ; VGA8: the palette's offset (98.1.1)
R_RSCALE    equ 36                  ; ...and its row scale, 0/1 or 2
R_FLIP      equ 37                  ; MODEX: 2 = two pages, flipped (98.3.8)
VP_PAGE     equ 19200               ; a Mode X page, in plane bytes
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
    mov di, vp_name                 ; ES:SI is the kernel's: copy it out now
    mov cx, 12
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .cpd
    loop .cp
    mov byte [di], 0
.cpd:
    mov [vp_argdir], dx
    mov [vp_argvol], bl
    mov byte [vp_argpend], 1
.nodoc:
    OS88_ALTENTER_ARM               ; the key-state map, for Alt+Enter
    call OSAPI_CPU_INFO             ; a 286 loads keys as the thumb is
    mov [vp_tier], al               ; dragged (98.4.2)
    mov word [vp_wb], 80            ; THE WINDOW'S SIZE is the layout's, for a
    mov word [vp_pwb], 80           ; 640 x 200 video until one is open
    mov word [vp_h], 200
    mov word [vp_ph], 200
    call vp_layfit
    mov ax, [vp_lcw]
    add ax, 2
    mov [vp_tpl+4], ax
    mov ax, [vp_lch]
    add ax, TITLE_H + 1
    mov [vp_tpl+6], ax
    mov si, vp_tpl
    call OSAPI_WM_CREATE
    jc .fail
    mov [vp_win], bx
    push ax                         ; A GREY GROUND on a sixteen-colour
    push bx                         ; desktop (98.4.8): the window paints
    push cx                         ; every pixel of its content itself, so
    push dx                         ; the kernel's white fill in front of
    call OSAPI_VIDEO                ; W_PAINT is not a flash of white under
    cmp dh, 1                       ; the grey (SPEC.md 11.90.1). The DEPTH
    pop dx                          ; is the question, as the SDK says: VGA
    pop cx                          ; and EGA both
    pop bx
    jbe .ngrey
    mov byte [vp_grey], 1
    mov al, 1
    call OSAPI_WM_OWNBG
.ngrey:
    pop ax
    mov al, 1                       ; A FIXED LAYOUT (SPEC.md 11.93): its
    call OSAPI_WM_KEEPH             ; height hangs over the dock rather than
                                    ; be cut - CGA's band is too short for the
                                    ; picture, the bar and the buttons (98.4.1)
    OS88_REGION_MOVABLE             ; SPEC.md 66.6.1.1. A play pins it anyway:
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
    call vp_mkdtab
    mov si, vp_menus
    call OSAPI_MENU_SET
    mov si, vp_about
    call OSAPI_ABOUT_SET
    mov ax, vp_onwake
    call OSAPI_WM_ONWAKE
    mov ax, vp_ontimer              ; the hold's loading (98.3.18); refused
    call OSAPI_WM_ONTIMER           ; on kern_small, which has no XMS anyway
    mov word [vp_msg], vp_s_none
    call vp_fmt
    mov bx, [vp_win]
    call OSAPI_WM_WAKE              ; the document, and a settled first paint,
    mov bx, [vp_win]                ; from the wake (SPEC.md 54.10)
    clc
    ret
.fail:
    stc
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
    push ax
    call vp_abdismiss
    jc .out
    cmp ax, KEY_ALTENTER            ; Alt+Enter and F: full screen, PAUSED -
    je .fs                          ; it plays when Space says so (98.3.6)
    cmp ah, KSC_LEFT
    je .prev
    cmp ah, KSC_RIGHT
    je .next
    cmp al, 27                      ; Esc: a waiting session stops, where it
    je .esc                         ; got to kept
    cmp al, 13                      ; Enter, Space or P plays
    je .play
    cmp al, ' '
    je .play
    or al, 0x20
    cmp al, 'p'
    je .play
    cmp al, 'f'
    je .fs
    cmp al, 'r'
    je .rep
    cmp al, 's'
    je .spk
    cmp al, 'm'
    je .spk
    cmp al, 'i'
    jne .out
    call vp_cardtog
    jmp short .out
.spk:
    call vp_mutetog                 ; M or S: MUTE (98.3.17)
    jmp short .out
.rep:
    call vp_reptog
    jmp short .out
.esc:
    xor al, al
    call vp_stopfor
    jmp short .out
.fs:
    call vp_fsenter
    jmp short .out
.play:
    call vp_play
    jmp short .out
.prev:
    mov ax, -1
    jmp short .step
.next:
    mov ax, 1
.step:
    call vp_step
.out:
    pop ax
    ret

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
    mov bx, [vp_win]
    call OSAPI_WM_WAKE
    pop bx
.out:
    ret

; W_ONCLICK: the rects where the window IS, then the library, which hands a
; press on no button to vp_onclick (SPEC.md 20.5.1.3.3)
vp_clickw:
    call vp_abdismiss
    jc .out
    call vp_track
    call vp_clip
    jmp os88ui_btnclick
.out:
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
    call OSAPI_GET_TICKS
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
    call OSAPI_GET_TICKS
    sub ax, [vp_dtk]
    cmp ax, VP_DRAGT
    pop ax
    jb .out
    call vp_seekto                  ; (the thumb stays under the pointer)
    call OSAPI_GET_TICKS            ; ...and the interval runs from the load's
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
    mov cx, [vp_lbw]
    sub cx, VP_THW
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
    jz .open
    dec ax
    jz .prev
    dec ax
    jz .play
    dec ax
    jz .next
    dec ax
    jz .rep
    dec ax
    jz .mute
    call vp_cardtog
    jmp short .out
.mute:
    call vp_mutetog
    jmp short .out
.rep:
    call vp_reptog
    jmp short .out
.next:
    mov ax, 1
    jmp short .step
.prev:
    mov ax, -1
.step:
    call vp_step
    jmp short .out
.play:
    call vp_play
    jmp short .out
.open:
    call vp_opendlg
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
    call vp_fmt
    jmp short .paint
.fail:
    mov word [vp_msg], vp_s_kbad
    call vp_fmt
.paint:
    mov bx, [vp_win]
    call OSAPI_WM_CLIP_SET
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
    mov bx, [vp_win]
    call OSAPI_WM_CLIP_SET
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
    mov di, vp_name
    mov cx, 12
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .cpd
    loop .cp
    mov byte [di], 0
.cpd:
    call vp_open                    ; the dialog left us standing in its
    pop di                          ; folder (SPEC.md 19.2.1)
    pop si
    mov bx, [vp_win]                ; the layout and the paint: the wake's
    call OSAPI_WM_WAKE
    pop dx
    pop cx
    pop bx
    pop ax
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
    mov byte [vp_ok], 0
    mov byte [vp_played], 0
    mov byte [vp_loaded], 0
%ifdef VP_DIAG
    mov word [vp_mfre0], 0          ; (no play of this file's to report)
%endif
    call vp_pfree                   ; the last file's poster, and its keys
    call vp_rfree                   ; ...and its loaded block
    call vp_xfree                   ; ...and its hold in XMS (98.3.18)
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
    call OSAPI_MEM_CLAIM
    jc .mem
    mov [vp_tmp], dx
    mov es, dx
    xor bx, bx
    mov cx, [vp_clsec]
    mov ax, cx
    mov cl, 9
    shl ax, cl
    mov cx, ax                      ; CX = a cluster in bytes
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
    ; THE RENDITION (98.1.7), the best that plays here: 3 its layout the
    ; DESKTOP's own - the window with no shadow, and the full screen in its
    ; own mode; 2 a mode of its own on this display, full screen (a VGA has
    ; CGA's); 1 through the shadow. The first of the best, else the first -
    ; whose refusal is then the one the card gives
    call vp_dinfo
    mov byte [vp_rbest], 0
    mov byte [vp_rscore], 0
    mov byte [vp_rend], 0
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
    call OSAPI_MEM_FREE
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_parse - ES:0 = the header. CF=1 with [vp_msg] set when it refuses
vp_parse:
    mov word [vp_nkeys], 0          ; no key in hand until the keyframes
    mov word [vp_msg], vp_s_notv88  ; pass: a refusal anywhere leaves none
    cmp word [es:0], 'V8'
    jne .bad
    cmp word [es:2], '8' + (0x1A << 8)
    jne .bad
    mov word [vp_msg], vp_s_ver
    cmp word [es:4], 1              ; version 1, and no flag this player
    jne .bad                        ; does not know (98.1.1.2)
    test word [es:V88_FLAGS], ~V88F_KNOWN
    jnz .bad
    mov word [vp_msg], vp_s_bad
    cmp word [es:V88_FRAMES+2], 0   ; this player counts frames in a word
    jne .long
    mov ax, [es:V88_FRAMES]
    or ax, ax
    jz .bad
    mov [vp_frames], ax
    mov ax, [es:V88_RATE]
    or ax, ax
    jz .bad
    mov [vp_rate], ax
    mov ax, [es:V88_SPF]
    or ax, ax
    jz .bad
    mov [vp_spf], ax
    mov al, [es:V88_AUDIO]          ; none, PCM8 with a byte a sample, or
    cmp al, 2                       ; ADPCM4 with a byte two samples - which
    ja .bad                         ; needs an even count (SPEC.md 98.1.1)
    mov [vp_audio], al
    mov byte [vp_spkpwm], 0         ; SPEAKER COUNTS (98.1.1.3): PCM8's, the
    test byte [es:V88_FLAGS], V88F_SPKPWM   ; table already the encoder's
    jz .ncnt
    cmp al, 1
    jne .bad
    mov byte [vp_spkpwm], 1
.ncnt:
    mov byte [vp_spkp], 1           ; PULSES A SAMPLE (98.1.1.3.1): the
    test byte [es:V88_FLAGS], V88F_SPKMUL   ; counts made for N / pulses,
    jz .np1                         ; which only a speaker file carries
    cmp byte [vp_spkpwm], 0
    je .bad
    mov ah, [es:V88_SPKP]
    cmp ah, 2
    jb .bad
    cmp ah, 4
    ja .bad
    mov [vp_spkp], ah
.np1:
    xor bx, bx
    or al, al
    jz .aud
    mov bx, [vp_spf]
    cmp al, 1
    je .aud
    shr bx, 1
    jc .bad
.aud:
    cmp bx, [es:V88_ABYTES]
    jne .bad
    mov [vp_abytes], bx
    mov al, [es:V88_NREND]
    dec al
    cmp al, 3
    ja .bad
    inc ax
    mov [vp_nrend], al
    cmp [vp_rend], al               ; THE RENDITION vp_open asked for, its
    jae .bad                        ; slot at DI (98.1.7)
    mov al, [vp_rend]
    mov ah, 64
    mul ah
    add ax, V88_REND
    mov di, ax
    mov ax, [es:V88_PITDIV]
    cmp ax, FSX_RATE_MIN
    jb .bad
    mov [vp_pitdiv], ax
    mov al, [es:V88_RING]           ; the ring the stream assumes: 0, or
    mov [vp_rneed], al              ; slots (a power of two, 98.1.1)
    mov al, [es:V88_PITPER]
    or al, al
    jz .bad
    mov [vp_pitper0], al
    mov al, [es:di+R_PIXFMT]
    dec al
    cmp al, PF_TEXT
    ja .bad
    mov [vp_pixfmt], al
    mov bl, [es:di+R_LAYOUT]  ; 1..8, and the canvas inside it
    dec bl
    cmp bl, LAY_TEXT
    ja .bad
    mov [vp_layout], bl
    mov word [vp_palo], 0
    mov word [vp_palo+2], 0
    cmp al, PF_VGA8                 ; VGA8 is LIN320's and MODEX's, and
    je .v8                          ; they take nothing else
    cmp al, PF_C512                 ; C512 (98.1.3.5): the text screen as
    je .c512                        ; it is, and its card byte
    cmp al, PF_TEXT                 ; TEXT (98.1.3.6): the text screen not
    je .text                        ; retimed, and its colour byte
    cmp al, PF_CGA4                 ; CGA IN COLOUR (98.1.3.3): mode 4 on
    jb .nc                          ; CGA's layout with its palette byte,
    ja .c16                         ; and the text hack on its own
    or bl, bl                       ; (LAY_CGA)
    jnz .bad
    mov ah, [es:di+R_CGAPAL]
    test ah, 0x80
    jnz .bad
    test ah, 0x40                   ; mode 5's palette names no other
    jz .cp
    test ah, 0x20
    jnz .bad
.cp:
    mov [vp_cgapal], ah
    jmp .lay
.c16:
    cmp bl, LAY_C160
    jne .bad
    jmp .lay
.text:
    cmp bl, LAY_TEXT
    jne .bad
    mov ah, [es:di+R_CGAPAL]        ; 0 mono, 1 colour
    cmp ah, 1
    ja .bad
    mov [vp_cgapal], ah
    test byte [es:di+R_WB], 3       ; whole cells, an even number of them
    jnz .bad                        ; (the poster's two a byte)
    jmp short .lay
.c512:
    cmp bl, LAY_TXT
    jne .bad
    mov ah, [es:di+R_CGAPAL]        ; 0 old, 1 new, 2 both: named, and
    cmp ah, 2                       ; nothing else depends on it
    ja .bad
    mov [vp_cgapal], ah
    test byte [es:di+R_WB], 3       ; whole cells, an even number of
    jnz .bad                        ; them (the poster's two a byte)
    jmp short .lay
.nc:
    cmp al, PF_VGA4                 ; VGA4 is LIN80's planes (98.1.3.2)
    jne .v1
    cmp bl, LAY_LIN80
    jne .bad
    jmp short .lay
.v1:
    cmp bl, LAY_LIN320
    jae .bad
    jmp .lay
.v8:
    cmp bl, LAY_LIN320
    jb .bad
    cmp bl, LAY_MODEX               ; ...LIN320's and MODEX's, and no other
    ja .bad
    mov ax, [es:di+R_PAL]     ; the palette, on a sector
    or ax, ax
    jz .bad
    test ax, 511
    jnz .bad
    mov [vp_palo], ax
    mov ax, [es:di+R_PAL+2]
    mov [vp_palo+2], ax
.lay:
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1                       ; BX = layout * 6
    mov ax, [es:di+R_WB]
    or ax, ax
    jz .bad
    cmp ax, [vp_laytab+bx+2]        ; stride
    ja .bad
    mov [vp_wb], ax
    mov ax, [es:di+R_H]
    or ax, ax
    jz .bad
    cmp ax, [vp_laytab+bx+4]        ; rows
    ja .bad
    mov [vp_h], ax
    ; PLANES (98.1.3.1, 98.1.3.2): Mode X's four byte-planes, or VGA4's four
    ; bit-planes, and how far apart a RAM image keeps them
    mov byte [vp_planar], 0
    cmp byte [vp_layout], LAY_MODEX
    jne .pl2
    mov byte [vp_planar], 1
    mov word [vp_plsp], VP_MXPL
    jmp short .pl3
.pl2:
    cmp byte [vp_pixfmt], PF_VGA4
    jne .pl3
    mov byte [vp_planar], 2
    mov cx, 80                      ; h rows of 80 bytes, in paragraphs
    mul cx
    add ax, 15
    mov cl, 4
    shr ax, cl
    mov [vp_plsp], ax
.pl3:
    ; THE ROW SCALE (98.2.4): each row shown twice by the CRTC, VGA8 only
    mov byte [vp_rs], 0
    mov al, [es:di+R_RSCALE]
    cmp al, 1
    jbe .rs1
    cmp al, 2
    jne .bad
    cmp byte [vp_pixfmt], PF_VGA8
    jne .bad
    mov byte [vp_rs], 1
.rs1:
    mov byte [vp_flip], 0           ; PAGE FLIPPING (98.3.8): Mode X's own
    mov al, [es:di+R_FLIP]
    cmp al, 1
    jbe .fl1
    cmp al, 2
    jne .bad
    cmp byte [vp_layout], LAY_MODEX
    jne .bad
    mov byte [vp_flip], 1
.fl1:
    mov ax, [vp_h]                  ; ...and the rows the picture SHOWS,
    mov cl, [vp_rs]                 ; which is what the Preview is made at
    shl ax, cl
    cmp byte [vp_pixfmt], PF_TEXT   ; (TEXT's poster is four rows a cell,
    jne .ph                         ; 98.4.6)
    shl ax, 1
    shl ax, 1
.ph:
    mov [vp_ph], ax
    ; THE PREVIEW'S WIDTH (98.4): a one-bit canvas's own, or for VGA8 the
    ; luma of its keyframe dithered to one bit, a byte per eight pixels -
    ; so what sizes and places the poster reads this and not [vp_wb]
    mov cx, [vp_wb]
    cmp byte [vp_pixfmt], PF_CGA4   ; CGA IN COLOUR: four pixels a byte, or
    jb .pnc                         ; two made two wide - a poster byte is
    shr cx, 1                       ; two canvas bytes (98.4.6)
    jc .bad
    cmp byte [vp_pixfmt], PF_C512   ; ...C512's, two ATTRIBUTES: four bytes
    je .p512                        ; - and TEXT's, two CELLS, four pixels
    cmp byte [vp_pixfmt], PF_TEXT   ; each
    jne .pgeo
.p512:
    shr cx, 1
    jc .bad
    jmp short .pgeo
.pnc:
    cmp byte [vp_pixfmt], PF_VGA8
    jne .pgeo
    cmp byte [vp_layout], LAY_MODEX ; MODEX: a plane byte is four pixels,
    jne .pl320                      ; so two of them make the poster's one
    shr cx, 1
    jc .bad
    jmp short .pgeo
.pl320:
    test cl, 7                      ; ...eight pixels to its byte
    jnz .bad
    shr cx, 1
    shr cx, 1
    shr cx, 1
.pgeo:
    mov [vp_pwb], cx
    mov byte [vp_resid], 0
    test byte [es:V88_FLAGS], V88F_RESIDENT
    jz .strm
    call vp_pblock                  ; RESIDENT (98.1.7): its blocks, no chain
    jc .bad
    jmp short .keys
.strm:
    mov ax, [es:di+R_SP0]
    test ax, 511
    jnz .bad
    mov [vp_sp0], ax
    mov ax, [es:di+R_SP0+2]
    mov [vp_sp0+2], ax
    mov ax, [es:di+R_SP0N]
    dec ax
    cmp ax, 63
    ja .bad
    inc ax
    mov [vp_sp0n], ax
    mov ax, [es:di+R_SPMAX]
    dec ax
    cmp ax, 63
    ja .bad
.keys:
    mov byte [vp_flive], 0          ; LIVE (98.3.10): a resident file's -
    test byte [es:V88_FLAGS], V88F_LIVE ; or a streamed one's, played Live
    jz .nlv                         ; once it is held in XMS (98.3.18.1) -
    mov byte [vp_flive], 1          ; and the screen each rendition is for
.nlv:
    mov byte [vp_fruns], 0          ; ...and its records' blit runs (98.1.3.4),
    test byte [es:V88_FLAGS], V88F_RUNS ; a live file's alone
    jz .nrn
    cmp byte [vp_flive], 0
    je .bad
    mov byte [vp_fruns], 1
.nrn:
    mov al, [es:di+R_TARGET]
    cmp al, 3
    ja .bad
    mov [vp_target], al
    mov ax, [es:di+R_SLEN]    ; the stream's bytes, for its KB/s
    mov [vp_slen], ax
    mov ax, [es:di+R_SLEN+2]
    mov [vp_slen+2], ax
    ; --- THE KEYFRAMES (98.1.3): a table on a sector, 16,383 at most, a
    ;     poster inside it. A record too big for one read turns the Preview
    ;     and the seek off, and the file still plays from the start
    mov ax, [es:di+R_NKEYS]         ; (vp_nkeys is 0 since the entry)
    cmp ax, 16383
    ja .bad
    mov bx, [es:di+R_POSTER]
    cmp bx, 0xFFFF
    je .pok
    cmp bx, ax
    jae .bad
.pok:
    mov [vp_poster], bx
    or ax, ax
    jz .nokeys
    mov bx, [es:di+R_KTAB]
    test bx, 511
    jnz .bad
    mov [vp_ktab], bx
    mov bx, [es:di+R_KTAB+2]
    mov [vp_ktab+2], bx
    mov bx, [es:di+R_KMAX]
    cmp bx, 7                       ; an empty planar key is 7 bytes
    jb .bad
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
    xor al, al
    test byte [es:V88_FLAGS], V88F_REPEAT
    jz .rp
    inc ax
.rp:
    mov [vp_rep], al
    mov byte [vp_lkind], 0
    cmp byte [vp_resid], 0          ; (resident with no seam: the block from
    jne .lk                         ; its start, on black - no key to read)
    cmp word [vp_nkeys], 0
    je .lk
    mov byte [vp_lkind], 2
.lk:
    test byte [es:V88_FLAGS], V88F_LOOPREC
    jz .nolp
    mov word [vp_msg], vp_s_bad
    cmp word [es:V88_LOOP+LP_L+2], 0
    jne .bad
    mov ax, [es:V88_LOOP+LP_L]      ; L, and a frame after it to go on at
    inc ax
    jz .bad
    cmp ax, [vp_frames]
    jae .bad
    dec ax
    mov [vp_lL], ax
    cmp byte [vp_resid], 0          ; RESIDENT: the seam is each block's last
    je .lstr                        ; record, found when it is loaded
    mov byte [vp_lkind], 1
    jmp .nolp
.lstr:
    mov ax, [es:V88_LOOP+LP_OFF]
    mov [vp_loff], ax
    mov ax, [es:V88_LOOP+LP_OFF+2]
    mov [vp_loff+2], ax
    mov ax, [es:V88_LOOP+LP_SP]
    test ax, 511
    jnz .bad
    mov [vp_lsp], ax
    mov ax, [es:V88_LOOP+LP_SP+2]
    mov [vp_lsp+2], ax
    mov al, [es:V88_LOOP+LP_SECS]
    dec al
    cmp al, 63
    ja .bad
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
    cmp byte [vp_flip], 0           ; ...no longer than a flipped play's copy
    je .lfl                         ; of the last record (98.3.8)...
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
    push ds                         ; the title, NUL-terminated within its 48
    push es
    push ds
    push es
    pop ds
    pop es
    mov si, V88_TITLE
    mov di, vp_title
    mov cx, VP_COLS
.t:
    lodsb
    stosb
    or al, al
    jz .td
    loop .t
    mov byte [es:di], 0
.td:
    pop es
    pop ds
    clc
    ret
.long:
    mov word [vp_msg], vp_s_long
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

; vp_canplay - can THIS display play it (SPEC.md 98.3, 98.3.2)? In its own
; layout's mode if the display has it; else through the SHADOW, in the first
; other layout whose mode it has and whose screen holds the canvas
vp_canplay:
    mov byte [vp_shadow], 0
    mov bx, [vp_win]
    call OSAPI_FSX_CAPS             ; AX = the modes this window's display has
    mov [vp_caps], ax
    mov al, [vp_layout]
    cmp byte [vp_pixfmt], PF_CGA4   ; CGA IN COLOUR (98.3.12): its own
    jb .std                         ; modes, full screen
    je .c4
    cmp byte [vp_pixfmt], PF_C512   ; C512 (98.3.12.2): a CGA's COMPOSITE
    je .c512                        ; colours, which nothing else has
    cmp byte [vp_pixfmt], PF_TEXT   ; TEXT (98.3.16): any adapter's text
    je .text                        ; screen, in colour any but a mono one
    cmp dl, VID_CGA                 ; C160: a CGA's text mode retimed, or a
    je .c16                         ; VGA's - an EGA's 350 lines hold no
    cmp dl, VID_VGA                 ; hundred rows of the cell
    jne .cno
.c16:
    call vp_try                     ; TEXT80, and 80 x 100 holds the canvas
    jc .cno
    mov byte [vp_shadow], 1         ; ...always THROUGH THE SHADOW: the
    jmp short .ok                   ; screen's stride is 160, a byte of two
.c4:
    test byte [vp_caps], 1 << FSXM_CGA320
    jnz .ok
.cno:
    mov word [vp_msg], vp_s_nocol
    ret
.c512:
    cmp dl, VID_CGA
    jne .c5no
    call vp_try                     ; TEXT80, and the screen's own layout:
    jnc .ok                         ; NATIVE, no shadow (98.1.3.5)
.c5no:
    mov word [vp_msg], vp_s_nocmp
    stc
    ret
.text:
    cmp byte [vp_cgapal], 0         ; COLOUR: an MDA or a Hercules draws
    je .t1                          ; the attributes as something else
    cmp dl, VID_HERC
    je .tno
.t1:
    call vp_try                     ; TEXT80, and the screen's own layout:
    jnc .ok                         ; NATIVE, no shadow
.tno:
    mov word [vp_msg], vp_s_notxt
    stc
    ret
.std:
    call vp_try
    jnc .ok
    cmp byte [vp_pixfmt], PF_VGA8   ; colour has no one-bit screen to be
    jae .none                       ; copied onto
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
    mov [vp_msg], ax
    ret
.shadow:
    mov byte [vp_shadow], 1
.ok:
    mov [vp_tlay], al
    mov bl, al                      ; the mode to take
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1
    mov al, [vp_laytab+bx]
    cmp byte [vp_pixfmt], PF_CGA4   ; (mode 4 is CGA's layout at two bits a
    jne .md                         ; pixel)
    mov al, FSXM_CGA320
.md:
    mov [vp_mode], al
    mov byte [vp_ok], 1
    call vp_mdet                    ; MUTED, if this machine may not play it
    mov al, [vp_tlay]               ; THE FULL SCREEN's, kept: a bracket in the
    mov [vp_fslay], al              ; window sets its own (98.3.7)
    mov al, [vp_mode]
    mov [vp_fsmode], al
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
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1                       ; BX = layout * 6
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
    call OSAPI_MEM_CLAIM_HI         ; while the window is open, and from the
                                    ; bottom it lay under a resident block,
                                    ; which could then never move down past
                                    ; it (98.1.7.4). No room for a picture:
    jc .nop                         ; the box is black and the key is picked
    mov [vp_pseg], dx               ; all the same - a play needs only its
                                    ; entry
.nop:
    mov ax, [vp_kbkb]               ; the record, a cluster either side
    call OSAPI_MEM_CLAIM
    jc .nobuf
    mov [vp_rdseg], dx
    mov ax, bx
    call vp_kent
    jc .nent
    cmp word [vp_pseg], 0
    je .free
    push bx                         ; (the decode takes every register)
    call vp_kpic
    pop bx
    jc .nent
    mov [vp_dkey], bx
    mov ax, [vp_ps]
    mov [vp_pscale], ax
    call vp_pmov                    ; (movable once it is made: 98.3.19.2)
    jmp short .free
.nent:
    call vp_pfree
.free:
    mov dx, [vp_rdseg]
    call OSAPI_MEM_FREE
    jmp short .out
.nobuf:
    call vp_pfree
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_pmov - the poster's claim MOVABLE (SPEC.md 98.3.19.2): it is read at
; each paint through [vp_pseg] and by nothing at interrupt time, so it is
; declared once it is made - not at its claim, which the decode's own
; claims follow. Preserves all
vp_pmov:
    push ax
    push dx
    mov dx, [vp_pseg]
    or dx, dx
    jz .out
    mov ax, vp_smove
    call OSAPI_MEM_MOVABLE
.out:
    pop dx
    pop ax
    ret

; vp_pfree - the poster's claim, if there is one
vp_pfree:
    push dx
    mov dx, [vp_pseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
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
    cmp byte [vp_ke+KE_SECS], 64    ; ...and a super-packet after it
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
    call OSAPI_MEM_CLAIM
    jc .no
    mov [vp_kshd], dx
    mov es, dx
    call vp_zero
    mov ax, [vp_ke+KE_OFF]
    mov dx, [vp_ke+KE_OFF+2]
    mov cx, [vp_ke+KE_LEN]
    call vp_rdat
    jc .free
    mov dx, [vp_rdseg]
    mov ax, si
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    add si, 6                       ; past len, y0, y1
    push ds
    mov ds, dx
    call vp_decram                  ; ES = the shadow, from its address 0
    pop ds
    call vp_mkpic
    clc
.free:
    pushf
    mov dx, [vp_kshd]
    call OSAPI_MEM_FREE
    popf
.no:
    ret

; vp_mkpic - the canvas in [vp_kshd] (the file's layout) into [vp_pseg] at
; the layout's scale: its own size, a half or a quarter (98.4)
vp_mkpic:
    push ax
    push cx
    push dx
    cmp byte [vp_pixfmt], PF_VGA4   ; 16 COLOURS: packed for OSAPI_GFX_BLIT4,
    jne .n4                         ; every [vp_ps]-th pixel (98.4.5)
    call vp_v4pack
    jmp .rows4
.n4:
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
    mov ax, [vp_pwb]
    mov cl, 3
    shl ax, cl
    cmp word [vp_ps], 1
    je .v8own
    mov ax, [vp_pseg]
    mov [vh_sseg], ax
    mov [vh_dseg], ax
    mov byte [vh_lay], 0xFF
    call vp_half
    mov ax, [vp_pwb]
    shl ax, 1
    shl ax, 1
    cmp word [vp_ps], 2
    je .sized
    mov ax, [vh_wb]
    mov [vh_sstr], ax
    call vp_half
    mov ax, [vp_pwb]
    shl ax, 1
    jmp short .sized
.v8own:
    mov [vp_ppx], ax
    mov ax, [vp_pwb]
    mov [vp_pbw], ax
    mov ax, [vp_ph]
    jmp short .rows
.mono:
    cmp word [vp_ps], 1             ; AT ITS OWN SIZE: the rows out of the
    jne .half                       ; file's layout, dense
    call vp_linear
    mov ax, [vp_wb]
    mov [vp_pbw], ax
    mov cl, 3
    shl ax, cl
    mov [vp_ppx], ax
    mov ax, [vp_h]
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
    mov ax, [vp_wb]                 ; ...its width half the canvas's
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
    mov ax, [vp_wb]
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
    call OSAPI_MEM_CLAIM_HI         ; (from the top, as vp_loadkey's is)
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
    mov bx, dx                      ; the row's four thresholds
    and bx, 3
    shl bx, 1
    shl bx, 1
    mov ax, [vp_bayer4+bx]
    mov [vp_rthr], ax
    mov ax, [vp_bayer4+bx+2]
    mov [vp_rthr+2], ax
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
    mov bl, al
    xor bh, bh
    mov al, [vp_lum+bx]
    mov bx, 4                       ; the column: a canvas byte starts on a
    sub bx, cx                      ; multiple of four poster pixels
    cmp [vp_rthr+bx], al            ; CF = threshold < luma: lit
    pop bx
    rcl ah, 1
    loop .px
    pop cx
    loop .sb
    push ds
    push ax
    mov ax, [vp_pseg]
    mov ds, ax
    pop ax
    mov [di], ah
    pop ds
    inc di
    pop cx
    loop .ob
    inc dx
    jmp .row
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    call OSAPI_MEM_CLAIM
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
    call OSAPI_MEM_FREE
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    call OSAPI_VIDEO                ; DL = the adapter
    mov dh, dl
    cmp byte [vp_pixfmt], PF_TEXT
    je .text
    cmp byte [vp_pixfmt], PF_C512
    je .c512
    cmp byte [vp_pixfmt], PF_C160
    je .c16
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
.c16:
    cmp dh, VID_CGA
    jne .v16
    mov dx, 0x3D8                   ; video off while the 6845 is retimed
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
    call vp_c16fill
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.c512:                              ; C512 (98.3.12.2), a CGA's alone: C160's
    mov dx, 0x3D8                   ; retime, every cell black, and the
    mov al, 0x01                    ; COLOUR BURST - video off meanwhile
    out dx, al
    mov si, vp_c16crt
    mov cx, 6
    mov dx, 0x3D4
.c5crt:
    lodsw
    out dx, al
    inc dx
    mov al, ah
    out dx, al
    dec dx
    loop .c5crt
    mov es, [vp_vseg]               ; a keyframe's canvas of zeroes is black:
    xor di, di                      ; character 0 on attribute 0
    xor ax, ax
    mov cx, 8000
    cld
    rep stosw
    mov dx, 0x3D8                   ; 80 columns, video on, blink off, and the
    mov al, 0x09                    ; black-and-white bit clear: burst on
    out dx, al
    inc dx                          ; 3D9h: border 6 - IBM's CGA makes the
    mov al, 0x06                    ; 80-column burst from the border's
    out dx, al                      ; output, and a black one has none
    jmp .out

.text:                              ; TEXT (98.3.16): the mode as the bracket
    mov bl, dh                      ; set it, the cursor off - the 6845's
    mov dx, 0x3D4                   ; register 0Ah on the card's own port -
    cmp bl, VID_HERC                ; and every cell 0 on 0, black. Colour
    jne .tcur                       ; (not on a mono card, vp_canplay) turns
    mov dl, 0xB4                    ; blink off, for sixteen backgrounds.
.tcur:                              ; (BL is the adapter: DX is the ports')
    mov ax, 0x200A
    out dx, ax
    mov es, [vp_vseg]
    xor di, di
    xor ax, ax
    mov cx, 2000
    cld
    rep stosw
    cmp byte [vp_cgapal], 0
    je .tout
    cmp bl, VID_CGA
    jne .tvga
    mov dx, 0x3D8                   ; 80 columns, video on, BLINK OFF
    mov al, 0x09
    out dx, al
    inc dx                          ; 3D9h: a black border
    xor al, al
    out dx, al
    jmp short .tout
.tvga:
    mov ax, 0x1003                  ; an EGA's or a VGA's blink off: the
    xor bx, bx                      ; BIOS (as C160's is)
    int 0x10
.tout:
    jmp .out

vp_c16fill:                         ; every cell 0DEh on black
    mov es, [vp_vseg]
    xor di, di
    mov ax, 0x00DE
    mov cx, 8000
    cld
    rep stosw
    ret

; vp_rdpal - a VGA8 file's palette (98.1.1) into vp_pal, and the luma the
; Preview dithers by into vp_lum: (77 r + 150 g + 29 b) >> 8 of the DAC's
; six bits, then x 17 + 32 >> 6 for 0..16 against a 4 x 4 Bayer cell -
; tools/os88vid.py's vga8_lum16. CF=1 with [vp_msg] set: unreadable, or a
; value past the DAC's 63. Nothing for any other format
vp_rdpal:
    cmp byte [vp_pixfmt], PF_VGA8
    je .go
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
    call OSAPI_MEM_CLAIM
    jc .mem
    mov [vp_rdseg], dx
    mov ax, [vp_palo]
    mov dx, [vp_palo+2]
    mov cx, 768
    call vp_rdat
    jc .io
    push ds
    pop es
    mov di, vp_pal
    mov cx, 768
    push ds
    mov ds, [vp_rdseg]
    cld
    rep movsb
    pop ds
    mov si, vp_pal                  ; every value a six-bit one
    mov cx, 768
.chk:
    lodsb
    cmp al, 63
    ja .bad
    loop .chk
    mov si, vp_pal
    mov di, vp_lum
    mov cx, 256
.lum:
    push cx
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
    mov [di], al
    inc di
    pop cx
    loop .lum
    mov dx, [vp_rdseg]
    call OSAPI_MEM_FREE
    clc
    jmp short .out
.bad:
    mov word [vp_msg], vp_s_bad
    jmp short .fr
.io:
    mov word [vp_msg], vp_s_io
.fr:
    mov dx, [vp_rdseg]
    call OSAPI_MEM_FREE
    stc
    jmp short .out
.mem:
    mov word [vp_msg], vp_s_mem
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
    mov dx, 0x3C8
    xor al, al
    out dx, al
    inc dx
    mov si, vp_pal
    mov cx, 768
    cld
.l:
    lodsb
    out dx, al
    loop .l
    pop si
    pop dx
    pop cx
    pop ax
.out:
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
    inc al                          ; n + 1 lines a row, twice: 2n + 1
    or al, ah
    out dx, al
    pop dx
    pop ax
.out:
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
    cmp byte [vp_layout], LAY_MODEX
    jne .lin
    jmp .mx
.lin:
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
    mov bx, dx                      ; the row's four thresholds, where BX
    and bx, 3                       ; can index them
    shl bx, 1
    shl bx, 1
    mov ax, [vp_bayer4+bx]
    mov [vp_rthr], ax
    mov ax, [vp_bayer4+bx+2]
    mov [vp_rthr+2], ax
    mov cx, [vp_pwb]
.byte:
    push cx
    xor ah, ah                      ; AH = the byte, built from the left
    mov cx, 8
.pix:
    mov bl, [es:si]
    inc si
    xor bh, bh
    mov al, [vp_lum+bx]
    mov bx, si                      ; x & 3 of the pixel just read
    dec bx
    and bx, 3
    cmp [vp_rthr+bx], al            ; CF = threshold < luma: lit
    rcl ah, 1
    loop .pix
    push ds
    push ax
    mov ax, [vp_pseg]
    mov ds, ax
    pop ax
    mov [di], ah
    pop ds
    inc di
    pop cx
    loop .byte
    inc dx
    jmp short .row
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
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
    mov bx, dx
    and bx, 3
    shl bx, 1
    shl bx, 1
    mov ax, [vp_bayer4+bx]
    mov [vp_rthr], ax
    mov ax, [vp_bayer4+bx+2]
    mov [vp_rthr+2], ax
    mov cx, [vp_pwb]
.mbyte:
    push cx
    xor ah, ah
    xor cx, cx                      ; CX = the pixel, 0..7
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
    push ax
    mov ax, [vp_pseg]
    mov ds, ax
    pop ax
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
    mov cx, ax
    pop ax
    pop bx
    pop dx
    ret

; vp_v4pack - the VGA4 canvas in [vp_kshd] (four bit-planes [vp_plsp]
; apart, row y at y x 80) as OSAPI_GFX_BLIT4's packed nibbles in [vp_pseg],
; the left pixel high, every [vp_ps]-th pixel of every [vp_ps]-th row -
; tools/os88vid.py's vga4_pack. Sets vp_pbw, vp_ppx, vp_prows. Preserves all
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
    xor si, si                      ; SI = the source row
.row:
    cmp si, [vp_h]
    jae .done
    mov ax, 80
    mul si
    mov bp, ax                      ; BP = the row in each plane
    mov word [vp_v4x], 0
    mov byte [vp_v4n], 0
    mov word [vp_v4xb], 0xFFFF
.pix:
    mov ax, [vp_v4x]
    cmp ax, [vp_v4w]
    jae .eol
    mov bx, ax
    shr bx, 1
    shr bx, 1
    shr bx, 1
    cmp bx, [vp_v4xb]
    je .have
    mov [vp_v4xb], bx               ; a new source byte: its four planes
    add bx, bp
    push es
    push si
    xor si, si
.ld:
    mov es, [vp_mxseg+si]
    mov al, [es:bx]
    shr si, 1
    mov [vp_v4c+si], al
    shl si, 1
    inc si
    inc si
    cmp si, 8
    jb .ld
    pop si
    pop es
.have:
    mov cl, [vp_v4x]
    and cl, 7
    xor ah, ah
    mov bx, 3                       ; plane 3 first, so AH = b3 b2 b1 b0
.bit:
    mov al, [vp_v4c+bx]
    shl al, cl
    shl al, 1                       ; CF = this pixel's bit
    rcl ah, 1
    dec bx
    jns .bit
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
    mov ax, [vp_ps]
    add [vp_v4x], ax
    jmp short .pix
.eol:
    test byte [vp_v4n], 1           ; a row ending on its high nibble
    jz .nr
    inc di
.nr:
    add si, [vp_ps]
    jmp .row
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
vp_rdat:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bx, [vp_clb]
    dec bx                          ; BX = a cluster's mask, throughout
    mov si, ax
    and si, bx                      ; SI = into its cluster
    sub ax, si                      ; DX:AX = that cluster's start
    add cx, si                      ; the bytes owed from there, 17 bits
    mov [vp_rdend], cx
    mov word [vp_rdend+2], 0
    adc word [vp_rdend+2], 0
    jnz .disk                       ; past 64 KB: the disk's
    cmp cx, 0xFFFF                  ; (vp_xrdat rounds CX up to even)
    je .disk
    call vp_xrdat                   ; HELD (98.3.18): a copy out of XMS
    jnc .ok
.disk:
    mov es, [vp_rdseg]
.l:
    mov di, [vp_clb]
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
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
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

; vp_spankb - CX = bytes at any offset: AX = the KB vp_rdat's read of them
; can take - CX and a cluster less a byte, rounded up to whole clusters
; (the offset's own place in its cluster is the most it can be moved on)
vp_spankb:
    push bx
    push cx
    push dx
    mov bx, [vp_clb]
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
    cmp byte [vp_ok], 1
    jne .out
    cmp byte [vp_resid], 0          ; RESIDENT: read whole into its blocks
    jne .out
    call OSAPI_XMEM_CAPS            ; no pool (every 8088): nothing more is
    or ax, ax                       ; asked, not even the directory
    jz .out
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
    call OSAPI_XMEM_CAPS            ; AX = the KB the pool has (BL, DX:CX
    cmp ax, si                      ; its other answers)
    jb .out
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
    xor ax, ax
    mov [vp_xhave], ax
    mov [vp_xhave+2], ax
    mov [vp_xfull], al
    mov byte [vp_xon], 1
    mov bx, [vp_win]
    inc ax                          ; the first chunk on the next tick
    call OSAPI_WM_TIMER
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_xfree - the hold, if there is one
vp_xfree:
    cmp byte [vp_xon], 0
    je .z
    push ax
    push dx
    mov byte [vp_xon], 0
    mov ax, [vp_xbase]
    mov dx, [vp_xbase+2]
    call OSAPI_XMEM_FREE
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
    stc
    ret
.ok:
    clc
    ret

; vp_xstep - the next chunk into the hold, through a claim of its own, on its
; own cursor. out CF=1 nothing is left to load (or the hold is gone)
vp_xstep:
    cmp byte [vp_xon], 0
    je .done
    cmp byte [vp_xfull], 0
    jne .done
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, VP_CHUNK / 1024
    call OSAPI_MEM_CLAIM
    jc .later                       ; no room now: the next tick asks again
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
    xor bx, bx                      ; DX:BX = the claim
    mov cx, VP_CHUNK
    push ds
    pop es
    mov di, vp_xcur
    mov si, vp_name
    call OSAPI_FILE_READ_SEQ        ; AX = the bytes (DX 0: one chunk)
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
    call vp_xcopy
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
.done:
    stc
    ret

; --- W_ONTIMER: SI = the window, lock held. The hold's next chunk, and the
; card's line that counts it. The next is armed as far off as this one took,
; so the loading has half the machine and the desktop the other half
vp_ontimer:
    push ax
    push bx
    push dx
    call OSAPI_GET_TICKS
    mov dx, ax
    call vp_xstep
    jc .said                        ; nothing left, or no hold: no next
    call OSAPI_GET_TICKS
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
    mov bx, [vp_win]
    call OSAPI_WM_CLIP_SET
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
    or bh, bh
    jnz .odd
    cmp dh, 0                       ; even rows: left over 0, right over 2
    jbe .e1
    or al, 2
.e1:
    cmp dl, 2
    jbe .st
    or al, 1
    jmp short .st
.odd:
    cmp dh, 3                       ; odd rows: left over 3, right over 1
    jbe .o1
    or al, 2
.o1:
    cmp dl, 1
    jbe .st
    or al, 1
.st:
    mov [vp_dtab+bx], al
    inc bx
    cmp bx, 512
    jb .l
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    mov ax, [vh_sseg]
    push ds
    mov ds, ax
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
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    cmp byte [vp_lsess], 0          ; LIVE -> the full screen: the worker
    je .rn                          ; stops at the frame it drew, and the
    mov byte [vp_lrun], 0           ; bracket goes on from it, playing if it
    mov byte [vp_autop], 0          ; was - the card paused with it, and
    cmp byte [vp_upause], 0         ; resumed by the bracket
    jne .lp
    call vp_upaus
    mov byte [vp_autop], 1
.lp:
    mov byte [vp_sfirst], 0
.rn:
    mov byte [vp_wantwin], 0
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
    mov byte [vp_startp], 0
    mov byte [vp_livem], 1
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
    mov byte [vp_lrun], 0           ; and the card, if any, where it is
    mov byte [vp_bpause], 0
    call vp_lbtn
    jmp short .out
.res:
    call vp_lgo
    call vp_lbtn
.out:
    pop ax
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
    mov byte [vp_lsess], 1
    mov byte [vp_ldrain], 0
    mov byte [vp_shadow], 1
    call vp_bandall
    cmp word [vp_krec], 0xFFFF      ; the key, onto the black
    je .nk
    mov si, [vp_krec]
    mov dx, [vp_ring]
    mov ax, si
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    call vp_decrec
    call vp_bandall
.nk:
    call vp_lprime                  ; A STREAM: the ring filled, over the key
    mov byte [vp_sfirst], 0
    mov byte [vp_ready], 1
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
    call vp_lbtn                    ; Play turned to Pause BEFORE the card:
%ifdef VP_LIVESND                   ; a card started first plays on through
    cmp byte [vp_snd], 0            ; this draw while the worker waits on
    je .ns                          ; the lock, and the start is then 0 to
    call vp_acur                    ; 3 frames late by where in a tick the
    call vp_sopen                   ; key landed (98.3.10.1) - the card,
.ns:                                ; started on the key's frame with the
%endif                              ; picture, the last thing done
    call OSAPI_GET_TICKS
    mov [vp_t0], ax
    mov [vp_ltk], ax                ; ...and the worker's clock from it
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
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
    mov byte [vp_upause], 0
    mov byte [vp_bpause], 1
    mov byte [vp_winm], 0
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
    call OSAPI_GET_TICKS
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
    mov byte [vp_lrun], 0           ; the box repainted from the shadow
    mov byte [vp_bpause], 0         ; FIRST, and only then the play resumed:
    mov byte [vp_shadow], 1         ; a repaint holds the lock, and a card
    mov ax, [vp_keep]               ; resumed before it plays on while the
    mov [vp_shseg], ax              ; worker waits (98.3.10.1)
    call vp_fmt
    mov si, [vp_win]
    call vp_repaint
    cmp byte [vp_autop], 0
    je .out
    mov byte [vp_autop], 0
    call vp_lgo
    call vp_lbtn                    ; (Pause, not Play, on its button)
.out:
    pop si
    ret

; vp_lbtn - the buttons again, Play or Pause as it now is. Lock held
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
    call vp_pbar
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
    or dx, dx
    jz .a
    call OSAPI_MEM_MOVABLE
.a:
    mov dx, [vp_rablk]
    or dx, dx
    jz .z
    call OSAPI_MEM_MOVABLE
.z:
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
    call OSAPI_MEM_CLAIM
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
    call OSAPI_MEM_FREE
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
    add ax, [vp_clb]
    adc dx, 0
    cmp byte [bx+BK_PACK], 0
    jne .pk
    add ax, [vp_clb]
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
    mov si, [vp_clb]
    dec si
    and si, [bx+BK_OFF]             ; SI = into its cluster
    xor di, di
    mov cx, [bx+BK_PACKED]
    add cx, si
    adc di, 0
    mov ax, [vp_clb]
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
    call OSAPI_MEM_AVAIL            ; AX = the largest a claim is served
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
    mov byte [di], 0
    pop cx
    pop ax
    mov di, vp_mbuf
    mov si, vp_s_needs
    call vp_puts
    push ax
    mov ax, cx
    xor dx, dx
    xor bl, bl
    call vp_putn
    mov si, vp_s_kbnd
    call vp_puts
    pop ax
    xor dx, dx
    call vp_putn
    mov si, vp_s_kbfree
    call vp_puts
    mov byte [di], 0
    mov word [vp_msg], vp_mbuf
    stc
    jmp short .out
.yes:
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_ldstored - BX = a STORED block's fields, [vp_ldseg] its claim: read in
; whole clusters from the one under its start, 32 KB a call (vp_rdat's
; bound), each piece where it belongs - then the whole moved down by the
; start's place in its cluster, a forward copy (the source is above the
; destination) stepped 32 KB at a time across the segments. Any size under
; 1 MB (98.1.7.1). CF=1 the disk failed. Clobbers AX, CX, DX, SI, DI
vp_ldstored:
    push bx
    push bp
    mov ax, [bx+BK_OFF]
    mov dx, [bx+BK_OFF+2]
    mov si, [vp_clb]
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
    mov cx, [vp_lsrem]
    or cx, [vp_lsrem+2]
    jz .move
    mov cx, 0x8000                  ; 32 KB, or what is left
    cmp word [vp_lsrem+2], 0
    jne .c
    cmp word [vp_lsrem], cx
    jae .c
    mov cx, [vp_lsrem]
.c:
    mov [vp_rdseg], bp
    call vp_rdat                    ; (a cluster boundary: SI comes back 0)
    jc .out
    add ax, cx                      ; on by the piece
    adc dx, 0
    sub [vp_lsrem], cx
    sbb word [vp_lsrem+2], 0
    add bp, 0x0800
    jmp short .piece
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
    mov cx, 0x8000
    cmp word [vp_lsrem+2], 0
    jne .mc
    cmp [vp_lsrem], cx
    jae .mc
    mov cx, [vp_lsrem]
.mc:
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
    cmp byte [vp_flip], 0           ; ...no longer than a flipped play's
    je .mf                          ; copy of the last record (98.3.8)
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
    mov al, [vp_layout]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    add bx, ax
    shl bx, 1                       ; BX = layout * 6
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
    ja .bad
    call .lists
    jc .bad
    jmp short .sub
.one:
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
.lists:                             ; the ten lists at SI. CF=1 not
    mov bx, 1                       ; the six poke lists: BX-byte changes
.pl:
    call .poke
    jc .lr
    inc bx
    cmp bx, 6
    jbe .pl
    call .slice
    jc .lr
    call .run
    jc .lr
    mov bx, 1                       ; SLICEL: a word's length, and the bytes
    call .long
    jc .lr
    xor bx, bx                      ; RUNL: a word's length, and one value
    call .long
.lr:
    ret
.poke:                              ; one list of BX-byte changes
    xor ax, ax
    cmp si, bp
    jae .no
    lodsb                           ; the count, 0 the list's end
    or al, al
    jz .r                           ; (CF=0)
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
    jc .no
    cmp ax, bp
    ja .no
    lodsw
    add di, ax
    jc .no
    xor ax, ax
.pe:
    lodsb
    add di, ax
    jc .no
    add si, bx
    loop .pe
    cmp di, dx
    jbe .poke
    jmp .no
.pabs:
    and al, 0x7F                    ; ABSOLUTE: an address each - and a
    jz .no                          ; count of 0 the decoder takes as 524,288
    mov cx, ax
    mov al, bl
    inc ax
    inc ax
    mul cl                          ; c(n + 2) bytes
    add ax, si
    jc .no
    cmp ax, bp
    ja .no
    mov di, dx
    sub di, bx                      ; DI = the highest start a change may have
    jc .no
.pa:
    lodsw
    cmp ax, di
    ja .no
    add si, bx
    loop .pa
    jmp .poke
.slice:                             ; SLICE: a skip, len8, the bytes
    xor ax, ax
    cmp si, bp
    jae .no
    lodsb
    or al, al
    jz .r
    mov cx, ax
    call .addr
    jc .r
.sle:
    lodsb
    add di, ax
    jc .no
    lodsb
    add di, ax
    jc .no
    add si, ax
    cmp si, bp
    ja .no
    loop .sle
    cmp di, dx
    jbe .slice
    jmp .no
.run:                               ; RUN: a skip, len8, the value
    xor ax, ax
    cmp si, bp
    jae .no
    lodsb
    or al, al
    jz .r
    mov cx, ax
    call .addr
    jc .r
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
    xor ax, ax                      ; len16, the bytes or the value
    cmp si, bp
    jae .no
    lodsb
    or al, al
    jz .r
    mov cx, ax
    call .addr
    jc .r
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
    mov ax, [vp_lL]
    cmp byte [vp_wkind], 1
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
    call OSAPI_MEM_FREE
.a:
    mov dx, [vp_rablk]
    or dx, dx
    jz .z
    call OSAPI_MEM_FREE
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
%endif                              ; yet (a RESIDENT play has no ring)
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
    cmp byte [vp_pixfmt], PF_VGA8
    jb .k0ok
.k0sel:
    cmp word [vp_nkeys], 0
    je .k0ok
    cmp ax, [vp_kload]
    je .k0ok
    push ax
    mov ax, [vp_kekb]               ; vp_rdat's buffer for the ENTRY alone:
    call OSAPI_MEM_CLAIM            ; a refusal leaves the old start
    jc .k0no
    mov [vp_rdseg], dx
    mov ax, [vp_sel]
    call vp_kent                    ; [vp_ke] and [vp_kload] = the key's
    mov dx, [vp_rdseg]
    call OSAPI_MEM_FREE
.k0no:
    pop ax
.k0ok:
    ; --- the sound (SPEC.md 98.3.1): a card, audio in the file, and the ring
    ;     the card will read in place, page-safe and never moved (MC_DMA)
    xor ax, ax
    mov [vp_aseg], ax
    mov [vp_keep], ax
    mov [vp_ring], ax
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
    mov ax, [vp_plsp]               ; is the bound, as a keyframe's is
    mov cx, ax                      ; (VP_MXSHD): plane 3's base + 64 KB, and
    shl ax, 1                       ; a 16-bit write from any plane stays in
    add ax, cx                      ; it
    add ax, 63
    mov cl, 6
    shr ax, cl
    add ax, 64
    jmp short .kc
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
    mov byte [vp_nokeep], 0
    mov byte [vp_pcv], 0            ; (the poster holds no frame of this yet)
    call vp_pcanv                   ; THE POSTER IS THE KEEPER (98.3.19.3):
    jc .kel                         ; at the video's own size it IS the
    mov byte [vp_nokeep], 2         ; canvas, one-bit or VGA4, and a play
    xor dx, dx                      ; that draws onto the screen needs no
    jmp .kok                        ; second copy of it
.kel:
    call vp_kelig                   ; A KEEPER THIS PLAY CAN DO WITHOUT
    jc .kreq                        ; (98.3.19): claimed, and given back if
    push ax                         ; the ring cannot reach the header's
    call OSAPI_MEM_CLAIM_HI         ; slots beside it
    pop ax
    jc .knone
    call vp_kroom
    jnc .kok
    call OSAPI_MEM_FREE
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
    call OSAPI_MEM_CLAIM_HI         ; FROM THE TOP, streamed or RESIDENT
    jnc .kok                        ; (98.1.7.4, 98.3): pinned for a bracket
                                    ; (98.3.19.2), and claimed from the bottom
                                    ; it landed on the kernel's caches and
                                    ; walled them off from the ring - 75 KB of
                                    ; Mode X keeper left the ring the 313 KB
                                    ; above it, not the 381 KB the machine had
    mov word [vp_msg], vp_s_mem
    jmp .fail
.kok:
    mov [vp_keep], dx
    cmp byte [vp_flip], 0           ; PAGE FLIPPING: the last record's copy
    je .nopv                        ; (98.3.8)
    mov ax, VP_PREVKB               ; (from the top too, for the same
    call OSAPI_MEM_CLAIM_HI         ; reason)
    jnc .pv
    mov word [vp_msg], vp_s_mem
    jmp .fail
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
    jb .ring
.rkc:
    cmp ax, [vp_kload]
    jne .ring
    mov ax, [vp_kbkb]
    call OSAPI_MEM_CLAIM
    jnc .ring
    mov word [vp_msg], vp_s_mem
    jmp .fail
.strm:
    ; --- the ring: K slots and the mirror (SPEC.md 98.3). IN THE BRACKET,
    ;     as many as the machine has, 2..[vp_kmax]: every slot past the
    ;     header's ring is headroom the encode never counted on. LIVE plays
    ;     on the desktop, so it keeps the old rule, a power of two to VP_KMAX
    call OSAPI_MEM_AVAIL            ; AX = the largest free run, KB
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
    mov dl, VP_RL / 1024 + 1
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
    dec ax                          ; less the mirror
    mov cx, dx
    ret
.kfit:
    cmp cx, 2
    jae .kok2
    mov word [vp_msg], vp_s_mem
    jmp .fail
.kok2:
    mov [vp_k], cx
    mov byte [vp_rshort], 0         ; A RING SHORT OF THE STREAM'S (98.1.1):
    cmp cl, [vp_rneed]              ; it plays, and a burst may pause it -
    jae .rok                        ; which the full screen says once
    mov byte [vp_rshort], 1
.rok:
    mov ax, cx
    inc ax
    mov cl, 5
    shl ax, cl                      ; (K + 1) x 32 KB
    call OSAPI_MEM_CLAIM
    jnc .ring
    mov word [vp_msg], vp_s_mem
    jmp .fail
.ring:
    call vp_spos                    ; where it starts, the reader, the hook
    jnc .clk0                       ; (vp_spos, which a seek runs too)
    jmp .fail
.clk0:
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
    cmp byte [vp_spkp], 1           ; door's K is a multiple of the pulses
    je .clk2                        ; and the kernel's entry is a sample's
    push ax                         ; first - within a sample of the
    push bx                         ; file's, as the door's own rounding
    mov al, [cs:os88spk_n]          ; is
    mul byte [vp_spkp]              ; AX = N, the counts a sample
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
    mov byte [vp_sess], 1
    mov byte [vp_sfirst], 1
    clc
    jmp short .out
.fail:
    call vp_sfree
    call vp_fmt
    call vp_repaint
    cmp byte [vp_lcard], 0          ; A PLAY REFUSED AT THE START: the card
    jne .fc                         ; comes out, as for one that fails on
    mov byte [vp_card], 1           ; its way (vp_sstop) - it is what says
    mov byte [vp_relay], 1          ; why, and the owner found the reason
    push bx                         ; hidden behind a closed card
    mov bx, [vp_win]
    call OSAPI_WM_WAKE
    pop bx
.fc:
    stc
.out:
    pushf                           ; ONE POST A PRESS (98.3.19.1): the
    cmp byte [vp_cppost], 0         ; play the wake starts again may not
    jne .cpk                        ; post a second time, and anything else
    mov byte [vp_cpq], 0            ; ends the press
.cpk:
    mov byte [vp_cppost], 0
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
; read into the ring (vp_spos). 0 at the least. Preserves all but AX, flags
vp_kres:
    cmp byte [vp_audio], 0
    je .a
    cmp word [vp_aseg], 0
    jne .a
    sub ax, VP_RL / 1024 + 1
    jc .z
.a:
    sub ax, [vp_kekb]
    jnc .r
.z:
    xor ax, ax
.r:
    ret

; vp_kwant - AX = the slots this play wants: the header's ring (98.1.1),
; and 2 where it says nothing. Preserves all but AX
vp_kwant:
    mov al, [vp_rneed]
    xor ah, ah
    cmp al, 2
    jae .r
    mov al, 2
.r:
    ret

; vp_kelig - CF=0 this play may go WITHOUT its keeper (SPEC.md 98.3.19):
; the keeper is only the canvas's image here - not a shadow the decoder
; writes, not a Live or RESIDENT play's, no second page to flip to - and the
; play is in the FULL SCREEN, where nothing but a swap reads it back. In the
; window the keeper is how the canvas crosses every bracket. Preserves all
vp_kelig:
    cmp byte [vp_kneed], 0          ; (the decoder writes INTO it)
    jne .no
    cmp byte [vp_resid], 0
    jne .no
    cmp byte [vp_livem], 0
    jne .no
    cmp byte [vp_flip], 0
    jne .no
    cmp byte [vp_fsshd], 0
    jne .no
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
    cmp word [vp_ps], 1
    jne .no
    cmp word [vp_pscale], 1
    jne .no
    cmp byte [vp_kneed], 0
    jne .no
    cmp byte [vp_resid], 0
    jne .no
    cmp byte [vp_livem], 0
    jne .no
    cmp byte [vp_flip], 0
    jne .no
    cmp byte [vp_fsshd], 0
    jne .no
    cmp byte [vp_pixfmt], PF_VGA4   ; 16 colours, on mode 12h's planes
    je .yes
    cmp byte [vp_pixfmt], PF_VGA8   ; ...or one bit, in the desktop's own
    jae .no                         ; layout - another's is the window's
    push ax                         ; shadow (98.3.2), which the keeper IS
    mov al, [vp_layout]
    cmp al, [vp_dlay]
    pop ax
    jne .no
.yes:
    clc
    ret
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
    call OSAPI_MEM_AVAIL
    call vp_kres
    mov cl, 5
    shr ax, cl                      ; ...in 32 KB slots
    mov bx, ax
    call vp_kwant
    inc ax                          ; (and the mirror)
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
    inc ax
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
    add ax, VP_RL / 1024 + 1
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
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop cx
    pop bx
    pop ax
    stc
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
    mov dx, [vp_ring]
    call .d
    mov dx, [vp_keep]
    call .d
    mov dx, [vp_prevseg]
    call .d
    pop dx
    ret
.d:
    or dx, dx
    jz .n
    push ax
    call OSAPI_MEM_MOVABLE
    pop ax
.n:
    ret

; vp_smove - THE SESSION'S AND THE POSTER'S RELOCATION PROC (SPEC.md 66.3,
; 98.3.19.2): BX = the old base, DX = the new. Every word naming one of
; those claims holds its BASE - every other segment is derived at its use -
; so a word equal to the old base is the one to move. Clobbers AX, SI
vp_smove:
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

vp_smtab:     dw vp_ring, vp_keep, vp_prevseg, vp_shseg, vp_pseg, vp_kshd, 0

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
    mov word [vp_krec], 0xFFFF
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
    cmp byte [vp_pixfmt], PF_VGA8   ; key 0 too for colour: its frame 0 may
    jb .start                       ; be too big for one record, and the key
.kpick:                             ; is the whole picture
    cmp ax, [vp_kload]
    jne .start
    mov [vp_rdseg], dx
    mov ax, [vp_ke+KE_OFF]
    mov dx, [vp_ke+KE_OFF+2]
    mov cx, [vp_ke+KE_LEN]
    call vp_rdat
    jnc .kin
    mov word [vp_msg], vp_s_kbad
    stc
    ret
.kin:
    cmp byte [vp_kneed], 0          ; the key decodes into a keeper that is
    je .kok                         ; only the canvas (98.1.7.3), so its
    mov dx, [vp_rdseg]              ; writes are checked as the block's were
    call vp_rbnd
    jnc .kok
    mov word [vp_msg], vp_s_kbad
    ret
.kok:
    mov [vp_krec], si
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
    ; --- the reader: from the cluster boundary under the stream's start
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
    ; --- the hook's state: before the first super-packet
    xor ax, ax
    mov [vp_lc], ax
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
    mov ax, [vp_ssec]
    mov [vp_psec], ax               ; the first super-packet, not yet entered
    cmp byte [vp_resid], 0          ; RESIDENT: the cursor on the block
    je .out
    call vp_rcur
.out:
    clc
    ret

; vp_sfree - the session's claims, whichever it holds
vp_sfree:
    push dx
    mov dx, [vp_ring]
    call .f
    mov dx, [vp_keep]
    call .f
    mov dx, [vp_aseg]
    call .f
    mov dx, [vp_prevseg]
    call .f
    xor dx, dx
    mov [vp_prevseg], dx
    mov [vp_ring], dx
    mov [vp_keep], dx
    mov [vp_shseg], dx
    mov [vp_aseg], dx
    mov [vp_kkb], dl
    pop dx
    ret
.f:
    or dx, dx
    jz .n
    call OSAPI_MEM_FREE
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
    mov byte [vp_winm], 1           ; IN THE WINDOW: Play is Pause while it
    mov byte [vp_bpause], 1         ; plays, drawn before the bracket takes
    call vp_clip                    ; the screen
    call vp_buttons
    call vp_boxxy                   ; A DRAG moves the window's pixels by any
    mov ax, [vp_py]                 ; number of rows and the play puts the
    sub ax, [vp_cy0]                ; picture on a bank: where they differ,
    cmp ax, [vp_ppoff]              ; the box is painted again first, or
    je .go                          ; the rows between are left behind (the
    call vp_pposter                 ; owner's report)
.go:
    mov byte [vp_exitr], VPX_STOP
    mov bx, [vp_win]
    mov ax, vp_main
    mov cx, FSXF_RATE
    mov dx, [vp_pdiv]
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
    mov byte [vp_lrun], 0           ; (the worker, if live, is held off by the
    mov byte [vp_lsess], 0          ; lock we hold, and idles from here)
    mov byte [vp_lend], 0           ; (a Live end's wake still queued must
    mov byte [vp_ldrain], 0         ; not stop the NEXT session)
    cmp byte [vp_sess], 0
    je .out
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
    mov byte [vp_card], 1
    mov byte [vp_relay], 1
    mov bx, [vp_win]
    call OSAPI_WM_WAKE
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
    cmp byte [vp_nowin], 0
    jne .no
    cmp byte [vp_pixfmt], PF_VGA8   ; 256 colours: full screen only (98.3.7)
    je .no
    cmp byte [vp_pixfmt], PF_CGA4   ; ...and CGA's colours, whose modes the
    jae .no                         ; one-bit desktop is not (98.3.12)
    cmp word [vp_ps], 1
    jne .no
    mov bx, [vp_win]
    call OSAPI_WM_OBSCURED          ; covered, or hidden
    jc .no
    call vp_boxxy
    mov ax, [vp_pdw]                ; the whole width, on its byte
    cmp ax, [vp_lpw]
    jne .no
    call OSAPI_VIDEO                ; AX, BX = the screen
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
    cmp dx, bx
    ja .no
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

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
    call OSAPI_MEM_CLAIM
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
    mov dx, [vp_rdseg]
    call OSAPI_MEM_FREE
    mov ax, [vp_kat_i]
    clc
    jmp short .out
.bad:
    mov dx, [vp_rdseg]
    call OSAPI_MEM_FREE
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
    mov byte [vp_pfresh], 0         ; a pause from before this bracket - the
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
    mov byte [vp_err], 1
    mov word [vp_errmsg], vp_s_refused
    ret
.mode:
    mov ax, [vp_fsi+FSI_SEG]
    mov [vp_vseg], ax
    call vp_dac                     ; VGA8: the file's 256 colours
    call vp_crtc                    ; ...and each row twice, if it asks
    call vp_cgaset                  ; ...or CGA's colours (98.3.12)
    ; the origin: centred, the row on a bank (SPEC.md 98.1.2), on the
    ; screen's layout - the file's own, or the shadow's target (98.3.2)
    mov bl, [vp_tlay]
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1
    mov ax, [vp_laytab+bx+4]        ; rows...
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
    mov word [vp_fcap], 2           ; through the shadow the decode is cheap
    cmp byte [vp_shadow], 0         ; and the copy is not: more frames a call,
    je .fc                          ; one copy after them
    mov word [vp_fcap], 8
.fc:
    cmp byte [vp_flip], 0           ; FLIPPING: a frame a call, so a flip has
    je .fc1                         ; a period to latch before the next draw
    mov word [vp_fcap], 1           ; goes into the page it replaces
.fc1:
    call vo_setup                   ; the full screen's text (98.3.13)
    mov word [vp_dy0], 0xFFFF
    mov word [vp_dy1], 0
    call OSAPI_MOUSE                ; the buttons as they are: a CLICK is a
    mov [vp_mbtn], al               ; press after this
    mov word [vp_wtk], 0xFFFF       ; the thumb: asked at the first frame
    xor ax, ax                      ; PAGES (98.3.8): page 0 on the glass,
    mov [vp_poff], ax               ; the other drawn next, and no record
    mov [vp_foff], ax               ; owed to it yet
    mov [vp_prevn], ax
    cmp byte [vp_flip], 0
    je .pg
    mov word [vp_poff], VP_PAGE
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
    mov byte [vp_sfirst], 0
    mov byte [vp_kblk], 1           ; (from frame 0: black after the fill)
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
    je .fill
    mov si, [vp_krec]
    mov dx, [vp_ring]
    mov ax, si
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    push dx
    mov ax, si
    add ax, [vp_ke+KE_LEN]
    push ax                         ; the record's end
    call vp_decboth                 ; SI past its lists
    pop ax
    pop dx
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
    je .fill
    call vp_blit
.fill:                              ; fill the ring before the first frame: a
    call vp_fill                    ; stream that fits is read whole
    jnc .fill
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
    mov byte [vp_err], 1
    mov word [vp_errmsg], vp_s_badsp
    ret
.skn:
    loop .sk
.sk0:
    cmp byte [vp_kblk], 0           ; FROM FRAME 0: the black the first frame
    je .sk0b                        ; is drawn on, now that it is next
    mov byte [vp_kblk], 0
    call vp_kput
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
    call vo_update                  ; ("Paused" again, if Space paused it)
    jmp short .loop
.snd:
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
    cmp ax, [vp_frames]             ; - unless it repeats (98.3.9)
    jae .drain
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
    call vp_skeep                   ; EVERY pass, not only an idle one: after
                                    ; an underrun the reader is catching up,
                                    ; so a chunk arrives each pass and the
                                    ; card sat silent until the whole ring
                                    ; was full again rather than until one
                                    ; block was queued (98.3.1)
    call vp_fill
    jnc .loop                       ; a chunk arrived: poll, and try again
    call vp_wthumb                 ; (in the window, the thumb moves)
    mov al, FSXW_FRAME              ; nothing to read yet: give the period
    call OSAPI_FSX_WAIT             ; to the hook
    jmp short .loop
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
    pushf
    cli
    mov byte [vp_ready], 0
    popf
    call vo_off                     ; the text off: the canvas is read back
    call vp_kget                    ; the canvas off the screen, for the next
    mov byte [vp_bpause], 0         ; surface and the box
    cmp byte [vp_exitr], VPX_DESK
    je .pic
    cmp byte [vp_winm], 0           ; the window -> the full screen: the
    jne .ret                        ; desktop between them is not the point
.pic:
    call vp_sesspic                 ; THE FRAME IN THE BOX, for the repaint
    call vp_fmt                     ; the bracket's exit makes (§53.6)
    ret
.stop:
    mov byte [vp_exitr], VPX_STOP
    pushf
    cli
    mov byte [vp_ready], 0
    popf
    call vo_off
    call vp_dtcalc                  ; the time it played - NOW, and not after
.ret:                               ; the bracket's exit repaints the desktop
    ret

; vp_dtcalc - [vp_dt], the ticks the session PLAYED: since its first frame,
; less every pause - the one running now included
vp_dtcalc:
    push ax
    cmp byte [vp_upause], 0
    je .np
    call OSAPI_GET_TICKS
    sub ax, [vp_ptk0]
    add [vp_ptk], ax
    call OSAPI_GET_TICKS
    mov [vp_ptk0], ax
.np:
    call OSAPI_GET_TICKS
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

; vp_skkey - AL = +1 Right, FFh Left. Preserves all
vp_skkey:
    push ax
    push bx
    push dx
    cmp byte [vo_skp], 0
    jne .more
    mov byte [vo_skp], 1            ; THE FIRST PRESS: from the frame on the
    mov word [vp_skn], 0            ; glass, the play paused
    mov bx, [vp_done]
    or bx, bx
    jz .z
    dec bx
.z:
    mov [vp_skb], bx
    mov byte [vp_skwas], 0
    cmp byte [vp_upause], 0
    jne .more
    mov byte [vp_skwas], 1
    push ax
    call vp_upaus
    pop ax
.more:
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
    call OSAPI_GET_TICKS
    mov [vp_sktk], ax
    call vp_sktext
    pop dx
    pop bx
    pop ax
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
    call OSAPI_GET_TICKS
    mov dx, ax
    mov al, [vp_skwt]
    xor ah, ah
    sub dx, ax                      ; ...and its wait already over
    mov [vp_sktk], dx
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
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    mov al, VOK_NOKEY
    call vo_toastk
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
    call OSAPI_MEM_CLAIM
    jc .no
    mov [vp_ring], dx
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
    mov byte [vp_err], 1
    mov byte [vp_end], 1
    mov byte [vp_skwas], 0
    jmp short .no
.ok:
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
    mov byte [vp_shadow], 0         ; the file's own layout: decoded onto the
    cmp al, [vp_layout]             ; desktop in place; another's: through the
    je .n                           ; shadow, and copied (98.3.2)
    mov byte [vp_shadow], 1
.n:
    call vp_track
    call vp_boxxy
    mov ax, [vp_py]
    mov [vp_ty0], ax
    mov ax, [vp_px]
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [vp_tx0], ax
    mov ax, [vp_ty0]
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
    cmp byte [vp_flip], 0           ; ...onto both pages when flipping
    je .one
    mov word [vp_kpo], VP_PAGE
    call vp_kmove
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
    jae .no                         ; size IS the canvas's bytes)
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
    mov bx, vp_wvq
    mov si, [bx+2]
    inc si                          ; SI = the first row inside
.row:
    mov ax, [vp_wvq + 6]
    cmp si, ax
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
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    div word [vp_abytes]            ; and two more
    add ax, 2
    mov [vp_acap], ax
    mov es, [vp_aseg]
    xor ax, ax
    mov [es:VP_RL+SND_EXT_TOTAL], ax
    mov [es:VP_RL+SND_EXT_CONS], ax
    mov bl, SND_OPENF_RING + SND_OPENF_EXT + (VP_RLCODE << SND_OPENF_RLSH)
    or bl, [vp_bflg]                ; ...and its block (34.5.3)
    cmp byte [vp_audio], 2
    jne .pf
    or bl, SND_OPENF_ADPCM4 + SND_OPENF_FORCE   ; ADPCM4 (FORCE: on a DSP
                                    ; 4.xx it was the user's call to unmute,
                                    ; 98.3.17): stream byte 0 is the card's
    mov al, [vp_aref]               ; reference - 80h, or a keyframe's
    mov [es:0], al                  ; (98.3.5) - and the silence is a
    mov word [vp_atot], 1           ; nibble of no change
    mov word [vp_a0], 1
    mov byte [vp_afn], 0
.pf:
    mov [vp_sflag], bl
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
    jc .fail
    mov byte [vp_sopn], 1
    ret
.card:
    xor al, al                      ; verb 0: open, the ring in our claim
    mov ah, [vp_sflag]
    xor si, si
    mov di, [vp_aseg]
    mov cx, [vp_atot]
    mov dx, [vp_rate]
    call OSAPI_SND_STREAM
    or al, al
    jnz .fail
    mov [vp_hand], ah
    mov byte [vp_sopn], 1
    ret
.fail:
    mov byte [vp_snd], 0            ; the card said no: a silent play
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
    mov byte [vp_mwhy], 0
    call OSAPI_SND_CAPS
    cmp byte [vp_audio], 2
    jne .p8
    test ax, SND_CAP_PCM_BG         ; ADPCM4 on the card that answers 4.xx
    jz .set
    test ax, SND_CAP_ADPCM4Q
    jz .set
    mov byte [vp_mwhy], 1
    jmp short .set
.p8:
    cmp byte [vp_audio], 1          ; PCM8 through an 8088's speaker, faster
    jne .set                        ; than it keeps up with (34.11.4)
    test ax, SND_CAP_PCM_BG
    jnz .set
    cmp byte [vp_tier], CPU_8086
    jne .set
    cmp byte [vp_spkp], 1           ; (two pulses a sample are ~96% of an
    ja .fast                        ; 8088: 34.11.7.1)
    cmp word [vp_rate], VP_SPKMAX
    jbe .set
.fast:
    mov byte [vp_mwhy], 2
.set:
    mov al, [vp_mwhy]
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
    mov byte [vp_snd], 0            ; (the hook reads it: before the free)
    mov byte [vp_sdefer], 0         ; ...and a card deferred to the first
    mov word [vp_owed], 0           ; Space is not started on a freed ring
    xor dx, dx
    xchg dx, [vp_aseg]
    or dx, dx
    jz .p
    call OSAPI_MEM_FREE
.p:
    pop dx
    pop ax
.ret:
    ret

; vp_sndprep - the sound this play will have (SPEC.md 98.3.1, 98.3.15): its
; ring claimed and [vp_snd] = 1 the card's stream, VP_SPK the speaker - or
; 0 none: no audio, MUTED, told to be silent, or no room. The card is not
; opened here. Clobbers AX, CX, DX
vp_sndprep:
    mov byte [vp_snd], 0
    cmp byte [vp_audio], 0
    je .ret
    cmp byte [vp_nosnd], 0
    jne .ret
    cmp byte [vp_mute], 0           ; MUTED: nothing claimed, nothing run
    jne .ret
    push bx
    call OSAPI_SND_CAPS
    pop bx
    test ax, SND_CAP_PCM_BG
    jz .spk
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
    mov ax, VP_RL / 1024 + 1        ; (RL + 272 of it: the table follows
    call OSAPI_MEM_CLAIM            ; the control words)
    jc .ret
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
    mov di, vp_fam                  ; 5150 (98.2.15.1)
    mov ax, 0x0100 | SPKFX_PRE_DIFF
    call os88spkfx_init
    pop di
    pop ax
.shp:
    ret
.spkno:
    mov dx, [vp_aseg]
    call OSAPI_MEM_FREE
    mov word [vp_aseg], 0
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
    mov byte [vo_skp], 1
    mov word [vp_skn], 0
    mov byte [vp_skhere], 1
    mov bx, [vp_done]
    or bx, bx
    jz .z
    dec bx
.z:
    mov [vp_skb], bx
    mov byte [vp_skwas], 0
    cmp byte [vp_upause], 0         ; PAUSED FIRST, while there is no sound
    jne .pz                         ; for the pause to touch
    mov byte [vp_skwas], 1
    call vp_upaus
.pz:
    call OSAPI_GET_TICKS            ; ...and due at once
    mov dl, [vp_skwt]
    xor dh, dh
    sub ax, dx
    mov [vp_sktk], ax
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
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_spkinfo - the info line's word on the sound (98.3.15, 98.3.17), at DI:
; MUTED and why, or with no card, the speaker. Preserves all
vp_spkinfo:
    cmp byte [vp_audio], 0
    je .r
    push ax
    push bx
    push dx
    push si
    mov si, vp_s_muted
    cmp byte [vp_mute], 0
    je .on
    cmp byte [vp_mwhy], 1           ; the card may not decode it
    jne .m2
    mov si, vp_s_mdsp
.m2:
    cmp byte [vp_mwhy], 2           ; ...or too fast for this one's speaker
    jne .p                          ; (34.11.4)
    mov si, vp_s_spkfast
    jmp short .p
.on:
    call OSAPI_SND_CAPS
    test ax, SND_CAP_PCM_BG
    jnz .o                          ; a card plays it
    cmp byte [vp_audio], 1
    jne .o
    mov si, vp_s_spkon
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
    call OSAPI_SND_STREAM           ; (a far cell: called, never jumped to)
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
; driver that takes SND_OPENF_BLKSH the largest that is still at most a
; block of 11 kHz PCM8 (~0.19 s) - so 5.5 kHz ADPCM4 plays in 512-byte
; blocks and the reader keeps 6 frames of stream ahead of the picture, not
; 20. [vp_blk] the bytes, [vp_bflg] the open flag's bits. Clobbers AX, CX
vp_sblk:
    mov word [vp_blk], VP_BLOCK
    mov byte [vp_bflg], 0
    cmp byte [vp_snd], 1            ; the card's only
    jne .r
    push bx
    push dx
    call OSAPI_SND_CAPS
    pop dx
    pop bx
    test al, SND_CAP_EXTBLK
    jz .r                           ; an older driver: 2,048, and no bits
    mov ax, [vp_rate]               ; AX = the bytes a second
    cmp byte [vp_audio], 2
    jne .b
    shr ax, 1                       ; (ADPCM4: two samples a byte)
.b:
    xor cx, cx
.l:
    cmp ax, VP_BLKBPS
    jae .d
    cmp cl, 3
    je .d
    inc cx
    shl ax, 1
    jmp short .l
.d:
    shr word [vp_blk], cl
    ror cl, 1                       ; the code into bits 6-7
    ror cl, 1
    mov [vp_bflg], cl
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
    call OSAPI_GET_TICKS
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
    call OSAPI_SND_STREAM
.out:
    ret
.resume:
    call OSAPI_GET_TICKS
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
    call OSAPI_SND_STREAM
.go:
    mov byte [vp_pfresh], 0         ; (a pause no call saw: nothing pending)
    mov byte [vp_upause], 0
    ret

; -----------------------------------------------------------------------------
; vp_fill - read the next chunk into its slot, if the hook has left it
; out: CF=0 one arrived; CF=1 none could be read now (ring full, or the end)
; -----------------------------------------------------------------------------
vp_fill:
    cmp byte [vp_resid], 0          ; RESIDENT: nothing to read (98.1.7)
    jne .none
    cmp byte [vp_eof], 0
    jne .eof
    mov ax, [vp_lc]                 ; the chunk to read
    mov bx, [vp_pc]                 ; the hook's super-packet's chunk: its slot
    add bx, [vp_k]                  ; and every one after it are still live
    cmp ax, bx
    jae .none
    push ax
    call vp_slot
    xchg bx, ax                     ; its slot
    pop ax
    mov cl, 11
    shl bx, cl
    add bx, [vp_ring]
    mov dx, bx                      ; DX:BX = the slot, ES:DI = the cursor
    xor bx, bx
    call vp_xfill                   ; HELD (98.3.18): a copy, AX = the bytes
    jnc .got
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
    mov byte [vp_err], 1
    mov word [vp_errmsg], vp_s_io
    mov byte [vp_end], 1
    stc
    ret
.dsk:
    mov cx, ax                      ; ...and behind the hold, if that is where
    mov ax, bx                      ; the hold ends
    mov dx, di
    call vp_xput
    mov ax, cx
.got:
    cmp ax, VP_CHUNK                ; ZF=0: the stream's last, short chunk -
    pushf                           ; said after [vp_lc] has it (vp_nextw)
    mov ax, [vp_lc]
    call vp_slot
    or ax, ax                       ; slot 0?
    jnz .pub
    call vp_mneed                   ; slot 0 is copied to the MIRROR, so a
    jcxz .pub                       ; super-packet starting in slot K-1 runs
    push ds                         ; on into contiguous memory - as much of
    mov ax, [vp_ring]               ; it as that super-packet runs on into
    mov bx, [vp_k]
    mov dx, cx
    mov cl, 11
    shl bx, cl
    add bx, ax
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
    cmp cx, 64
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
    mov ah, cl                      ; its end: sectors x 512...
    xor al, al
    shl ah, 1
    add bx, ax                      ; ...on from its start
    cmp bx, VP_CHUNK
    jb .nc
    sub bx, VP_CHUNK
    inc dx
.nc:
    mov cx, di                      ; the next one's sectors, at its start
    cmp dx, [vp_lc]
    jb .w
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
    mov bx, [vp_pc]
    add bx, [vp_k]
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
    mov ax, [vp_lc]
    mov [vp_wsc], ax
    mov [vp_wso], si
    jmp short .cont
.k0:
    mov byte [vp_wkind], 0
.cont:
    ; the reader from the cluster under that super-packet, as vp_sstart's
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
    cmp byte [vp_ready], 0
    je .ret
    cmp byte [vp_upause], 0         ; paused (98.3.4): the periods are not
    jne .paused                     ; the play's
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
    jcxz .ret
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
    xor al, al
    cmp [vp_owed], bx
    jb .copy
    inc ax
.copy:
    call vp_blitck                  ; the shadow's band, once for them all
    call vo_post                    ; ...and the text back over it
    cli
.ret:
    ret
.paused:                            ; ...EXCEPT THE PAUSE'S FIRST CALL'S: AX
    cmp byte [vp_pfresh], 0         ; is every period since the last call,
    je .ret                         ; and those PLAYED - through the shadow a
    mov byte [vp_pfresh], 0         ; call is a whole copy long, so Space
    add [vp_pers], ax               ; lands between two calls with 5 or 6
    cmp byte [vp_snd], 0            ; periods pending, which were dropped and
    jne .ret                        ; the picture ran that far behind the
    add [vp_owed], ax               ; clock (98.3.4). Owed, they are drawn on
    ret                             ; the resume's first call
.snd:                               ; --- the card's clock (SPEC.md 98.3.1)
    call vp_adue                    ; CX = frames due, [vp_due] set
    jcxz .top
    cmp cx, [vp_fcap]
    jbe .n2
    inc word [vp_late]              ; more behind the sound than a call draws
    mov cx, [vp_fcap]
.n2:
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
    xor al, al                      ; still frames due after them: behind
    mov bx, [vp_due]
    cmp bx, [vp_vseq]
    jbe .tc
    inc ax
.tc:
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
    cmp dx, [vp_abytes]             ; a quotient past 16 bits: long over
    jae .big
    div word [vp_abytes]            ; AX = frames wholly played
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
    cmp byte [vp_rep], 0            ; is what keeps the chunks under it, and
    jne .sq                         ; the video may not overtake it - which
    cmp ax, [vp_frames]             ; is why the header's count is the end
    jae .theend                     ; here, the chain's 0 never being reached
.sq:                                ; (repeating, the seam is next: 98.3.9)
    mov ax, [vp_vseq]
    cmp ax, [vp_aseq]
    jae .no
.go:
    mov bx, vp_pc
    call vp_next                    ; DX:SI = the record, CX = its len
    jnc .dec
    or al, al
    jz .stall
    cmp al, 1
    je .theend
    cmp al, 2
    je .badsp
    jmp short .badrec
.dec:
    inc word [vp_vseq]
    cmp byte [vp_nseam], 0
    jne .seam
    cmp byte [vp_flip], 0
    jne .flip
    call vp_decrec
    inc word [vp_done]
    clc
    ret
.flip:
    call vp_flipdec
    inc word [vp_done]
    clc
    ret
.seam:                              ; THE SEAM (98.3.9): the lap's join -
    cmp byte [vp_wkind], 1          ; the file's record, a frame like any
    jne .skey                       ; other...
    cmp byte [vp_flip], 0
    jne .sflip
    call vp_decrec
    jmp short .sdone
.sflip:
    call vp_flipdec
    jmp short .sdone
.skey:                              ; ...or keyframe 0, or nothing, over a
    push dx                         ; cleared canvas: a keyframe is decoded
    push si                         ; onto black (98.1.3)
    call vp_cclear
    pop si
    pop dx
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
.stall:
    inc word [vp_stall]
    stc
    ret
.theend:
    mov byte [vp_end], 1
    stc
    ret
.badsp:
    mov word [vp_errmsg], vp_s_badsp
    jmp short .err
.badrec:
    mov word [vp_errmsg], vp_s_badrec
.err:
    mov byte [vp_err], 1
    mov byte [vp_end], 1
    stc
    ret

; vp_flipdec - DX:SI = a record, played with PAGE FLIPPING (98.3.8): the
; back page holds the frame before last, so the last record is decoded into
; it again and then this one, which brings it to this frame; this record is
; kept for the other page's turn; and the CRTC is pointed at the page - it
; latches the start address at the next retrace, so nothing waits, and the
; next draw, a frame period later, is into the page that was showing.
; clobbers AX, BX, CX, DX, SI, DI, BP, ES
vp_flipdec:
    push dx
    push si
    cmp word [vp_prevn], 0
    je .cur
    mov dx, [vp_prevseg]
    xor si, si
    call vp_decrec
.cur:
    pop si
    pop dx
    push dx
    push si
    call vp_decrec
    pop si
    pop dx
    push ds                         ; the record, for the other page
    mov es, [vp_prevseg]
    xor di, di
    mov ds, dx
    mov cx, [si]
    mov bx, cx
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    pop ds
    mov [vp_prevn], bx
    call vo_flipon                  ; the text on it before it is shown
    mov ax, [vp_poff]               ; show it...
    call vp_show
    mov [vp_foff], ax
    xor word [vp_poff], VP_PAGE     ; ...and draw the other next
    ret

; vp_show - AX = a page's offset: the CRTC's start address (3D4h 0Ch/0Dh),
; in Mode X's bytes. Latched at the next retrace. Preserves all
vp_show:
    push ax
    push bx
    push dx
    mov bx, ax
    mov dx, 0x3D4
    mov al, 0x0C
    mov ah, bh
    out dx, ax
    mov al, 0x0D
    mov ah, bl
    out dx, ax
    pop dx
    pop bx
    pop ax
    ret

; vp_decboth - DX:SI = a record decoded into BOTH pages when flipping (a
; keyframe: the stream after it takes either), else as vp_decrec. SI past
; its lists, as vp_decrec leaves it
vp_decboth:
    cmp byte [vp_flip], 0
    je vp_decrec
    push dx
    push si
    mov word [vp_poff], 0
    call vp_decrec
    pop si
    pop dx
    mov word [vp_poff], VP_PAGE
    call vp_decrec
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
    mov ds, dx
    call vd_c160
    pop ds
    ret
.c16s:
    call vp_c16sync                 ; (the shadow the reference again)
.shd:
    push ds                         ; SHADOW (98.3.2): into the file's own
    mov ds, dx                      ; image at its own address 0, and the rows
    mov ax, [si+2]                  ; the record writes added to the band the
    mov cx, [si+4]                  ; next copy covers
    pop ds
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
    mov ds, dx
    call vd_native
    call vp_lrget
    pop ds
    ret
.shp:
    add si, 6
    push ds
    mov ds, dx
    call vp_decram                  ; (SI past the sub-records' 0)
    cmp byte [cs:vp_lrun], 0
    je .shq
    cmp byte [cs:vp_fruns], 0
    je .shq
    call vp_lrget
.shq:
    pop ds
    ret
.native:
    mov es, [vp_vseg]
    mov bp, [vp_org]
    add bp, [vp_poff]               ; the page being drawn (98.3.8), or 0
.go2:
    add si, 6
    push ds
    mov ds, dx
    cmp byte [cs:vp_planar], 0
    jne .mx
    cmp byte [cs:vo_clip], 0        ; the full screen's text up: round it
    jne .clip                       ; (98.3.13.1)
    call vd_native                  ; the lists, onto the adapter
    pop ds
    ret
.clip:
    mov byte [cs:vc_mask], 1
    call vd_clip
    pop ds
    ret
.mx:                                ; MODEX (98.1.3.1): each sub-record's Map
    lodsb                           ; Mask, then its lists - four pixels a
    or al, al                       ; store under 0Fh, a plane's under the
    jz .mxd                         ; rest
    mov ah, al
    mov [cs:vc_mask], al
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    push bp
    cmp byte [cs:vo_clip], 0
    jne .mxc
    call vd_native
    pop bp
    jmp short .mx
.mxc:
    call vd_clip
    pop bp
    jmp short .mx
.mxd:
    mov ax, 0x0F02                  ; all four planes again: the desktop's
    mov dx, 0x3C4                   ; own drawing, and a one-bit file's
    out dx, ax                      ; decode, assume it (98.1.3.2)
    pop ds
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
    call vd_native                  ; SI past them, the last time counting
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
    mov dh, cl                      ; its bytes (<= 32768): the sectors, 64
    xor dl, dl                      ; at most, x 512
    shl dh, 1
    add dx, [di+VC_PO]              ; where it ends, from its chunk's start
    mov ax, [di+VC_PC]
    cmp dx, VP_CHUNK
    jbe .one
    inc ax                          ; ...in the next chunk
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
    mov ds, dx
    lodsw
    mov cx, ax                      ; frames
    lodsw                           ; next
    pop ds
    jcxz .bsp                       ; no frames, or a next past 64 sectors
    cmp ax, 64
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
    mov bx, [di+VC_PO]
    add bx, [di+VC_ROFS]
    cmp bx, VP_CHUNK
    jb .ra
    sub bx, VP_CHUNK
    inc ax
.ra:
    call vp_addr                    ; DX:SI = the record
    push ds
    mov ds, dx
    mov cx, [si]                    ; its len...
    pop ds
    mov ah, [di+VC_PSEC]            ; ...against what is left of the
    xor al, al                      ; super-packet (x 512)
    shl ah, 1
    sub ax, [di+VC_ROFS]
    cmp cx, ax
    ja .brec
    cmp byte [vp_flip], 0           ; ...and, flipping, against the copy
    je .rfl                         ; vp_flipdec keeps of it (98.3.8), as
    cmp cx, VP_PREVKB * 1024        ; the seam's is
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
    mov ah, [di+VC_PSEC]            ; ITS LAST FRAME: step to the next one's
    xor al, al                      ; start now - the reader may reuse this
    shl ah, 1                       ; one's chunks from here, and waiting for
    add ax, [di+VC_PO]              ; the hook to ENTER the next one was a
    cmp ax, VP_CHUNK                ; deadlock (a super-packet that needs a
    jb .same                        ; chunk the reader may not read until
    sub ax, VP_CHUNK                ; this one is left)
    inc word [di+VC_PC]
.same:
    mov [di+VC_PO], ax
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
    div word [vp_k]
    xchg ax, dx
    pop dx
    ret

; vp_addr - AX = a chunk, BX = an offset in it -> DX:SI, a far pointer
; (clobbers AX, BX, CX)
vp_addr:
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
    add ax, [vp_abytes]
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
    xor dx, dx
.sput:
    mov cx, [vp_abytes]
    call vp_aput
    mov ax, [vp_wL]
    inc ax
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

; vp_aput - CX bytes from DX:SI into the ring at [vp_atot], wrapping, and the
; new total published to the card; DX = 0 writes [vp_afn] silence instead.
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
.cm:
    push ds
    mov ds, dx
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    pop ds
    ret
.fill:
    mov al, [vp_afn]
    cld
    rep stosb
    ret


%define VD_C160                     ; ...and its C160 twin (98.3.12.1)
%include "video/vdec.inc"
%include "video/vosd.inc"           ; the full screen's text (98.3.13)
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
    call OSAPI_VIDEO                ; AX = width, BX = height
    sub ax, 10                      ; the widest content: frame x 7, 1 border
    mov [vp_lcwm], ax
    sub bx, MBAR_H + TITLE_H + 1    ; the tallest, over the dock
    mov [vp_lchm], bx
    mov ax, [vp_ps]
    mov [vp_lops], ax
    call vp_dinfo                   ; (the desktop's banks, for COMPACT)
    mov si, 1                       ; SI = the scale tried: 1, 2, 4
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
    mov byte [vp_lbin], 0
    jmp short .set
.b:
    mov byte [vp_lbin], 1
.set:
    mov [vp_ps], si
    mov bx, [vp_lpw]                ; the box: the picture's width, and never
    cmp bx, VP_MINBW                ; less than the button row wants
    jae .bw
    mov bx, VP_MINBW
.bw:
    mov [vp_lbw], bx
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
    mov byte [vp_lcard], 1
    call vp_laybw
    add ax, VP_BOXX + 8 + 7
    and ax, 0xFFF8
    add ax, VP_CARDW + 4
    cmp ax, [vp_lcwm]
    ja .no
    mov [vp_lcw], ax
    mov ax, [vp_lph]
    add ax, VP_BSLACK + VP_BOXY + VP_BGAP + VP_BARH + 5
    cmp ax, VP_CARDHB
    jae .h
    mov ax, VP_CARDHB
.h:
    cmp ax, [vp_lchm]
    ja .no
    mov [vp_lch], ax
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; vp_laynorm / vp_laycomp - the box's slack and the bar's gaps for vp_layA:
; the layout's own, or COMPACT (98.4.1.1) - a CGA's 161 rows over the dock
; held a 320 x 112 picture and its bar but not the row too, by seven, so
; the logo (98.3.11) put its buttons in a card as wide as the screen. The
; slack is only what the desktop's banks need (1 on a CGA, 0 on a VGA, 3 on
; a Hercules: vp_boxxy's rounding) and the gaps are VP_CGAP. Preserves all
vp_laynorm:
    mov word [vp_lslk], VP_BSLACK
    mov word [vp_lgap], VP_BGAP
    ret

vp_laycomp:
    push ax
    push bx
    push cx
    mov bl, [vp_dlay]               ; the banks, as vp_boxxy asks for them
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1
    mov al, [vp_laytab+bx+1]
    xor ah, ah
    dec ax
    mov [vp_lslk], ax
    mov word [vp_lgap], VP_CGAP
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
    or ax, OS88UI_LATCH
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
vp_ptext:
    push ax
    push bx
    push cx
    push dx
    push si
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
    cmp bx, VP_LINES
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
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    mov ax, [vp_pseg]               ; THE PICTURE: the poster, or a LIVE play's
    mov [vp_qseg], ax               ; shadow - the frame it is on, at its own
    mov ax, [vp_pbw]                ; size (98.3.10)
    mov [vp_qbw], ax
    mov ax, [vp_prows]
    mov [vp_qrows], ax
    cmp byte [vp_lsess], 0
    je .q
    mov ax, [vp_shseg]
    mov [vp_qseg], ax
    mov word [vp_qbw], 80
    mov ax, [vp_h]
    mov [vp_qrows], ax
.q:
    cmp word [vp_qseg], 0
    je .black
    mov ax, [vp_by2]                ; rows past the box - a picture made at
    sub ax, [vp_py]                 ; another scale, between a relayout and
    inc ax                          ; its reload - are not drawn
    cmp ax, [vp_qrows]
    jbe .r
    mov ax, [vp_qrows]
.r:
    mov [vp_pdh], ax
    ; THE FRAME HUGS THE PICTURE (98.3.7): its row is on a bank, so up to
    ; three of the box's rows are spare, above it and below. Inside the
    ; frame they read as black bars round a picture that has none (the
    ; owner's report), so they go OUTSIDE it, in the window's ground -
    ; white, or grey where the ground is (98.4.8)
    mov al, CWHITE
    call OSAPI_SET_COLOR
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
    call OSAPI_SET_COLOR
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
    cmp byte [vp_pixfmt], PF_VGA4   ; 16 colours, as they are (98.4.5)
    jne .b1
    cmp byte [vp_lsess], 0          ; ...and a Live play's are its shadow's
    je .b4                          ; four planes, the repaint's clip walked
    mov byte [vp_v4p], 1            ; (98.3.10.4)
    call vp_v4blit
    jmp .out
.b4:
    call OSAPI_GFX_BLIT4
    jmp .out
.b1:
    call vp_blitb
    jnc .out
    mov ax, [vp_px]                 ; refused: black where it would be
    mov bx, [vp_py]
    mov cx, ax
    add cx, [vp_pdw]
    dec cx
    mov dx, bx
    add dx, [vp_pdh]
    dec dx
    call vp_fillne
    jmp short .out
.black:
    mov al, CBLACK                  ; no picture: the box, framed, black
    call OSAPI_SET_COLOR
    mov ax, [vp_bx1]
    dec ax
    mov bx, [vp_by1]
    dec bx
    mov cx, [vp_bx2]
    inc cx
    mov dx, [vp_by2]
    inc dx
    call OSAPI_GFX_FRAME
    mov ax, [vp_bx1]
    mov bx, [vp_by1]
    mov cx, [vp_bx2]
    mov dx, [vp_by2]
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

; vp_blitb - OSAPI_GFX_BLIT1's arguments, any number of rows: it takes 255
; at a time. CF = 1 a call refused. Preserves all
vp_blitb:
    cmp byte [vp_planar], 2         ; a VGA4 shadow's planes (98.3.10.4)
    jne .b1
    jmp vp_v4blit
.b1:
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
VP_V4BUF    equ 1024                ; the repack's buffer: VGA8's palette and
vp_v4buf    equ vp_pal              ; lumas, which a VGA4 file never has

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
    call OSAPI_SET_COLOR
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
    mov ax, di
    sub ax, vp_gh
    mov cl, 3
    shr ax, cl
    mov [vp_ghn], ax
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    call vp_unfilled
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
    mov ax, di
    sub ax, vp_gh
    mov cl, 3
    shr ax, cl
    mov [vp_ghn], ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call vp_unfilled
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    mov dx, [vp_cy0]
    add dx, VP_CARDHB - 1
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
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    call OSAPI_WM_CLIP_SET
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
    xor dx, dx
    xor bl, bl
    call vp_putn
    mov al, 'x'
    call vp_putc
    mov ax, [vp_ph]
    cmp byte [vp_pixfmt], PF_TEXT
    jne .pxh
    mov ax, [vp_h]
.pxh:
    xor dx, dx
    call vp_putn
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
    call vp_spkinfo
    ; 4: where Play starts - or, a session waiting, where it is paused
    mov di, vp_lines + 4 * VP_LINE
    cmp byte [vp_sess], 0
    je .nos
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
    mov cx, [vp_nkeys]
    jcxz .kl
    mov si, vp_s_start
    cmp word [vp_sel], 0
    jne .key
    call vp_puts
    mov ax, cx
    xor dx, dx
    xor bl, bl
    call vp_putn
    jmp short .msg
.key:
    mov si, vp_s_fromk
    call vp_puts
    mov ax, [vp_sel]
    inc ax
    xor dx, dx
    xor bl, bl
    call vp_putn
    mov si, vp_s_of
    call vp_puts
    mov ax, cx
    xor dx, dx
    call vp_putn
    mov si, vp_s_comma
    call vp_puts
    mov ax, [vp_ke+KE_K]            ; its time: the screen after frame k
    call vp_putt
    jmp short .msg
.kl:
    call vp_puts
.msg:
    ; 5: what Play does, or why not
    mov di, vp_lines + 5 * VP_LINE
    mov si, [vp_msg]
    call vp_puts
    cmp byte [vp_played], 0
    jne .res
    call vp_xsay                    ; 6: the hold, until a play's figures
    jmp .mem
.res:
    ; 6: frames drawn, stalls, pauses
    mov di, vp_lines + 6 * VP_LINE
    mov si, vp_s_drew
    call vp_puts
    mov ax, [vp_done]
    sub ax, [vp_base]
    xor dx, dx
    xor bl, bl
    call vp_putn
    mov al, '/'                     ; (' of ' ran a whole play's line past
    call vp_putc                    ; the card: its pauses fell off the end)
    mov ax, [vp_frames]
    sub ax, [vp_base]
    xor dx, dx
    call vp_putn
    mov si, vp_s_stall
    call vp_puts
    mov ax, [vp_stall]
    xor dx, dx
    call vp_putn
    cmp byte [vp_snd], 0
    je .nop
    mov si, vp_s_pause
    call vp_puts
    mov ax, [vp_pause]
    xor dx, dx
    call vp_putn
.nop:
    ; 7: late, and the time against the file's
    mov di, vp_lines + 7 * VP_LINE
    mov si, vp_s_late
    call vp_puts
    mov ax, [vp_late]
    xor dx, dx
    xor bl, bl
    call vp_putn
    mov si, vp_s_ticks
    call vp_puts
    mov ax, [vp_dt]
    xor dx, dx
    call vp_putn
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
    ; 8 and 9: THE HEAP (98.3), once a play has started - what Play found,
    ; and what the ring was sized from and took
    cmp word [vp_mfre0], 0
    je .out
    mov di, vp_lines + 8 * VP_LINE
    mov si, vp_s_heap               ; Heap 405K run, 471K free at Play
    call vp_puts
    xor dx, dx
    xor bl, bl
    mov ax, [vp_mrun0]
    call vp_putn
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
    pop es
    pop di
    pop si
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
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    inc cl
    cmp bh, cl
    jbe .d
.o:
    pop ax
    add al, '0'
    call vp_putc
    dec bh
    jnz .o
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_div32 - DX:AX /= CX; CX = the remainder
vp_div32:
    push bx
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div cx
    xchg ax, bx
    div cx
    mov cx, dx
    mov dx, bx
    pop bx
    ret

; vp_mul32 - DX:AX *= CX (the product must fit 32 bits)
vp_mul32:
    push bx
    mov bx, dx
    mul cx
    push dx
    push ax
    mov ax, bx
    mul cx
    pop bx                          ; the low product's low word
    pop dx                          ; ...and high
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
    db 1, 10
    times 10 dw 0FFFFh
    times 10 dw 00E70h
vp_i_rep:                           ; two arrows round: Repeat (98.3.9)
    db 1, 10
    times 10 dw 0FFFFh
    dw 00020h, 01FF0h, 03FF8h, 03030h, 03020h
    dw 0040Ch, 00C0Ch, 01FFCh, 00FF8h, 00400h
vp_i_mute:                          ; a speaker, struck: Mute (98.3.17)
    db 1, 10
    times 10 dw 0FFFFh
    dw 00100h, 00304h, 03F88h, 03F90h, 03FA0h
    dw 03F90h, 03F88h, 00304h, 00100h, 00000h
vp_i_info:                          ; an i: the info card
    db 1, 10
    times 10 dw 0FFFFh
    dw 00180h, 00180h, 00000h, 00380h, 00180h
    dw 00180h, 00180h, 00180h, 003C0h, 00000h
vp_i_open:                          ; an eject
    db 1, 10
    times 10 dw 0FFFFh
    dw 00180h, 003C0h, 007E0h, 00FF0h, 01FF8h
    dw 03FFCh, 00000h, 03FFCh, 03FFCh, 00000h
vp_i_prev:                          ; |<<
    db 1, 10
    times 10 dw 0FFFFh
    dw 03084h, 0318Ch, 0339Ch, 037BCh, 03FFCh
    dw 03FFCh, 037BCh, 0339Ch, 0318Ch, 03084h
vp_i_play:                          ; >
    db 1, 10
    times 10 dw 0FFFFh
    dw 00C00h, 00F00h, 00FC0h, 00FF0h, 00FFCh
    dw 00FFCh, 00FF0h, 00FC0h, 00F00h, 00C00h
vp_i_next:                          ; >>|
    db 1, 10
    times 10 dw 0FFFFh
    dw 0210Ch, 0318Ch, 039CCh, 03DECh, 03FFCh
    dw 03FFCh, 03DECh, 039CCh, 0318Ch, 0210Ch

vp_ablines:   dw vp_ab1, vp_ab2, vp_ab3, vp_ab4, vp_ab2, vp_ab6, 0
vp_ab1:       db 'Video Player for os8088', 0
vp_ab2:       db 0
vp_ab3:       db 'After XDC, by Jim Leonard', 0
vp_ab4:       db '(MobyGamer), MIT', 0
vp_ab6:       db 'Contributed by Elendilon', 0

; per layout (SPEC.md 98.1.2): FSXM id, banks, stride, rows
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
vp_v4c:       db 0, 0, 0, 0             ; vp_v4pack: a source byte's planes,
vp_v4x:       dw 0                      ; the pixel, the canvas's width,
vp_v4w:       dw 0
vp_v4xb:      dw 0                      ; the byte cached, and which nibble
vp_v4n:       db 0
vp_planar:    db 0                      ; 1 Mode X's planes, 2 VGA4's bits
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
vp_s_nofile:  db '(none) - File > Open...', 0
vp_s_none:    db 'Open a .V88 to play it', 0
vp_s_ready:   db 'Space plays and pauses; Esc stops', 0
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
vp_s_novga8:  db '256 colours: a VGA, full screen', 0
vp_s_nocol:   db 'CGA colour: needs a CGA or VGA', 0
vp_s_nocmp:   db 'Composite CGA colour: needs a CGA', 0
vp_s_colfs:   db 'CGA colour: plays full screen', 0
vp_s_notxt:   db 'Colour text: needs CGA, EGA or VGA', 0
vp_s_cpcga:   db 'Made for CGA: plays via a copy', 0
vp_s_cpherc:  db 'Made for Herc: plays via a copy', 0
vp_s_cpvga:   db 'Made for VGA: plays via a copy', 0
vp_s_fps:     db ' fps  ', 0
vp_s_kbs:     db ' KB/s, ', 0
vp_audnames:  dw vp_s_silent, vp_s_pcm8, vp_s_adpcm
vp_s_silent:  db 'silent', 0
vp_s_pcm8:    db 'sound PCM8', 0
vp_s_adpcm:   db 'sound ADPCM4', 0
vp_s_spkon:   db ', speaker', 0
vp_s_muted:   db ', muted', 0
vp_s_mdsp:    db ', muted: DSP 4', 0         ; (35 columns: 16 left here)
vp_s_spkfast: db ', muted: fast', 0
vp_s_nokeys:  db 'No keyframes: plays from the start', 0
vp_s_start:   db 'From the start; keys ', 0
vp_s_fromk:   db 'From key ', 0
vp_s_comma:   db ', at ', 0
vp_s_kbad:    db 'That keyframe could not be read', 0
vp_s_pausedat: db 'Paused at ', 0
vp_s_drew:    db 'Drew ', 0
vp_s_of:      db ' of ', 0
vp_s_xin:     db 'Into XMS: ', 0
vp_s_xheld:   db 'Held in XMS: ', 0
vp_s_kb:      db ' KB', 0
vp_s_stall:   db ', stalls ', 0
vp_s_pause:   db ', pauses ', 0
vp_s_late:    db 'Late ', 0
vp_s_room:    db 'Making room for the play...', 0
%ifdef VP_DIAG
vp_s_heap:    db 'Heap ', 0
vp_s_krun:    db 'K run, ', 0
vp_s_kfree:   db 'K free at Play', 0
vp_s_ring:    db 'Ring ', 0
vp_s_kk:      db 'K; ', 0
vp_s_keep:    db 'keep ', 0
vp_s_snd:     db ' snd ', 0
vp_s_lead:    db 'Lead ', 0
vp_s_atf:     db ' at f', 0
vp_s_hgap:    db '; hook gap ', 0
vp_s_dry:     db 'Dry ', 0
vp_s_most:    db ' stream; most ', 0
vp_s_tf:      db 't f', 0
%endif
vp_s_ticks:   db ', ', 0
vp_s_want:    db ' ticks of ', 0

; --- state ------------------------------------------------------------------------
vp_kmax:      dw VP_KBIG            ; the ring's most slots (a test may lower it)
vp_rneed:     db 0                  ; the slots the stream's bursts assume
vp_rshort:    db 0                  ; ...and this play has fewer: say so once
vp_stopat:    dw 0xFFFF             ; the gate's hold: stop before this frame
vp_win:       dw 0
vp_msg:       dw 0
vp_errmsg:    dw 0
vp_argdir:    dw 0
vp_argvol:    db 0
vp_argpend:   db 0
vp_abon:      db 0
vp_ok:        db 0
vp_loaded:    db 0
vp_played:    db 0
vp_mode:      db 0
vp_pwb:       dw 0                  ; the Preview's bytes a row (98.4)...
vp_ph:        dw 0                  ; ...and its rows: the canvas's, shown
vp_rs:        db 0                  ; ...this many times over, as a shift
vp_flip:      db 0                  ; Mode X page flipping (98.3.8)...
vp_poff:      dw 0                  ; ...the page being drawn...
vp_foff:      dw 0                  ; ...the one on the glass...
vp_kpo:       dw 0                  ; ...the one vp_kmove uses...
vp_prevseg:   dw 0                  ; ...and the last record's copy, its
vp_prevn:     dw 0                  ; bytes (0: none owed)
vp_palo:      dd 0                  ; VGA8: the palette's offset
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
vp_wb:        dw 0
vp_h:         dw 0
vp_sp0:       dw 0, 0
vp_sp0n:      dw 0
vp_clsec:     dw 0
vp_tmp:       dw 0
; the play
vp_ring:      dw 0
vp_k:         dw 1                  ; the ring's slots (1 before any play: vp_slot answers 0)
vp_vseg:      dw 0
vp_org:       dw 0
vp_t0:        dw 0
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
vp_tqg:       dw 0                  ; the machine's glyphs: segment,
vp_tqo:       dw 0                  ; offset,
vp_tqf:       db 0                  ; first code,
vp_tql:       db 0                  ; last,
vp_tqok:      db 0                  ; and whether there are any
vp_tscr:      dw 0                  ; vp_tmono's scratch: its tables
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
vx_n:         db 0                  ; ...else these runs: y0, y1, x0, x1
vx_run:       times VX_MAX * 4 db 0
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
vp_unmq:      db 0                  ; unmuted in the window: play on, synced
vp_skhere:    db 0                  ; a seek to the frame on the glass
vp_wvr:       dw 0                  ; vp_winv's button: its rect...
vp_wvq:       dw 0, 0, 0, 0         ; ...copied
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
vp_qseg:      dw 0                  ; vp_pposter's picture: its segment,
vp_qbw:       dw 0                  ; stride and rows
vp_qrows:     dw 0
vp_rend:      db 0                  ; the rendition vp_parse reads (98.1.7)
vp_nrend:     db 0                  ; ...of this many
vp_rbest:     db 0                  ; ...the best so far, and its score
vp_rscore:    db 0
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
vp_ldkb:      dw 0                  ; vp_ldblk's claim, KB, and segment
vp_ldseg:     dw 0
vp_vseq:      dw 0                  ; frames drawn, every lap counted - the
vp_aseq:      dw 0                  ; card's clock - and audio frames queued
vp_afr0:      dw 0                  ; the frame the audio cursor was set at
; the Preview (SPEC.md 98.4)
vp_cx0:       dw 0                  ; the content's origin, as vp_track saw it
vp_cy0:       dw 0
vp_clb:       dw 0                  ; a cluster, bytes
vp_slen:      dw 0, 0               ; the stream's bytes
vp_nkeys:     dw 0                  ; keyframes (0: no Preview, no seek)
vp_ktab:      dw 0, 0
vp_poster:    dw 0                  ; the header's poster, FFFFh none
vp_kmaxb:     dw 0                  ; the largest keyframe record
vp_kbkb:      dw 0                  ; ...and the claim that reads one, KB
vp_kekb:      dw 0                  ; ...and one that reads a table entry, KB
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
vp_tx1:       dw 0                  ; the bar's inside, and the thumb
vp_tx2:       dw 0
vp_tx:        dw 0
vp_pdh:       dw 0                  ; the picture's rows drawn
vp_brem:      dw 0                  ; vp_blitb's rows left
vp_dkey:      dw 0xFFFF             ; the key the picture is, FFFFh none
vp_kblk:      db 0                  ; a first bracket's black, owed (98.3.7.1)
vp_pscale:    dw 0                  ; ...and the scale it was made at
; the layout (98.4.1), vp_layfit's
vp_card:      db 0                  ; the user's: the info card out
vp_lcard:     db 0                  ; ...and whether it is (layout B forces it)
vp_lbin:      db 0                  ; 1: the buttons are in the card
vp_relay:     db 0                  ; the wake owes a layout and a resize
vp_ps:        dw 2                  ; the picture's scale: 1, 2 or 4
vp_lops:      dw 0
vp_lpw:       dw 0                  ; the picture at it, pixels and rows
vp_lph:       dw 0
vp_lbw:       dw 0                  ; the box: its width and height
vp_lbh:       dw 0
vp_lslk:      dw VP_BSLACK          ; the layout's slack and gaps (vp_laynorm,
vp_lgap:      dw VP_BGAP            ; vp_laycomp)
vp_lbary:     dw 0                  ; the bar's frame top, the button row's
vp_lbty:      dw 0
vp_lcardx:    dw 0                  ; the card's x
vp_lcw:       dw 0                  ; the content's size
vp_lch:       dw 0
vp_lcwm:      dw 0                  ; ...and the most the screen allows
vp_lchm:      dw 0
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
vp_ranok:     db 0                  ; the bracket ran: the position to keep
              db 0
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
vp_wnx:       dw 0                  ; vp_wmove: the block's new x...
vp_wox:       dw 0                  ; ...and its old one
vp_wmn:       db 0                  ; the bytes the move touches...
vp_wmt:       times 4 dw 0, 0       ; ...each its offset, mask, white bits
vh_sseg:      dw 0                  ; vp_half's image...
vh_lay:       db 0                  ; ...its layout, or FFh linear...
              db 0
vh_sstr:      dw 0                  ; ...at this stride
vh_wb:        dw 0
vh_h:         dw 0
vh_dseg:      dw 0
vh_owb:       dw 0
vh_y:         dw 0
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
vp_skwt:      db VP_SKWAIT          ; the wait after the last press, ticks
vp_grey:      db 0                  ; THE GREY GROUND (98.4.8) is on
vp_gb:        dw 0, 0, 0, 0         ; ...the rect
vp_ghn:       dw 0                  ; ...how many holes
vp_gh:        times 16 * 4 dw 0     ; ...and the holes: x1, y1, x2, y2 each
vp_gy:        dw 0                  ; ...the band
vp_gy2:       dw 0
vp_gx1:       dw 0                  ; ...and the hole a gap ends at
vp_gx2:       dw 0
                                    ; (tests/vidfskeys.py holds a seek here)
vp_skbuf:     times VO_MAXC + 1 db 0
vp_aref:      db 0                  ; ADPCM4's reference byte, this play
vp_lsin:      dw 0                  ; vp_ldstored: the start in its cluster
vp_lsrem:     dw 0, 0               ; ...and the bytes still to read or move
vp_mbuf:      times VP_MBUF db 0    ; vp_fits's "Needs n KB..."
vp_v4p:       db 0                  ; LIVE IN COLOUR (98.3.10.4): BLITP may,
vp_v4ax:      dw 0                  ; the clip down; vp_v4blit's repack:
vp_v4by:      dw 0                  ; x, y, width,
vp_v4cw:      dw 0
vp_v4rl:      dw 0                  ; rows left,
vp_v4nb:      dw 0                  ; bytes a row,
vp_v4bs:      dw 0                  ; the buffer's stride,
vp_v4rc:      dw 0                  ; rows it holds,
vp_v4k:       dw 0                  ; rows this chunk,
vp_v4ps:      dw 0                  ; and the plane step
vp_cbnd:      dw 0                  ; the canvas's bound in its layout, and
vp_kkb:       db 0                  ; the keeper's KB, 0 none (98.1.7.3)
vp_kneed:     db 0                  ; this session decodes INTO the keeper,
vp_rchk:      db 0                  ; ...and the block's writes are checked
vp_ptk0:      dw 0                  ; the tick it paused at
vp_ptk:       dw 0                  ; ticks paused, this play
vp_fsi:       times FSI_SIZE db 0
vp_cur:       times FSEQ_SIZE db 0
vp_xcur:      times FSEQ_SIZE db 0  ; THE HOLD (98.3.18): its loader's cursor,
vp_xbase:     dd 0                  ; the block's token,
vp_xcap:      dd 0                  ; its bytes (a KB over the file),
vp_xhave:     dd 0                  ; the bytes that have arrived,
vp_xon:       db 0                  ; there is one,
vp_lwant:     db 0                  ; a live stream's worker wants a feed
vp_bone:      db 0                  ; vp_buttons: this one only (0 all)
vp_xfull:     db 0                  ; ...and all of the file is in it

%include "os88alt.inc"              ; Alt+Enter in the bracket (SPEC.md 11.2.1.1)
%define OS88UI_ABOUT                ; the standard About card (SPEC.md 20.5.1)
%define OS88UI_BIMG                 ; ...and buttons with PICTURES, drawn with
%define OS88UI_NOGLYPH              ; no pixel written twice (SPEC.md 13.8.9),
%include "os88ui.inc"               ; and no check box or radio at all

; --- bss: zeroed by the loader, and no bytes of the file ----------------------
vp_dtab       equ os88_image_end    ; the half-scaler's two tables (vp_mkdtab)
vp_lines      equ vp_dtab + 512     ; the info panel's text
vp_pal        equ vp_lines + VP_LINES * VP_LINE ; VGA8's palette (98.1.1)
vp_lum        equ vp_pal + 768      ; ...and each entry's luma, 0..16
vo_sav        equ vp_lum + 256      ; what the full screen's text covers, a
                                    ; save a page (98.3.13)
vc_pg         equ vo_sav + 2 * VO_SAV ; ...and a 256-byte page -> the first
                                    ; of its rows at or after it (98.3.13.1)
vp_fam        equ vc_pg + 256       ; the speaker shaper's level family
    OS88_BSS 512 + VP_LINES * VP_LINE + 768 + 256 + 2 * VO_SAV + 256 + \
             SPKFX_NLEV * 256
    OS88_IMAGE_END
