; =============================================================================
; os8088 - apps/vbattle/vbattle.asm
;
; VIDEO BATTLE (SPEC.md 125): a turn-based battle played as JOINED VIDEO
; CLIPS. A .VBT bundle (tools/os88vbt.py) holds an idle loop, the action
; clips, the music, the sound effects and the battle's script; this package
; plays it full screen in CGA mode 4 on a CGA, an EGA or a VGA:
;
;   - the clips are the Video Player's frame records (SPEC.md 98.1.3),
;     decoded by apps/video/vdec.inc, ONE copy shared as source;
;   - the battle canvas is the top 142 rows; the BAND under it (rows
;     142-199) is the interface, drawn here, straight into the framebuffer;
;   - every join is EXACT: an action starts from an ANCHOR frame of the idle
;     loop through an entry record made against that frame's screen, and its
;     last record is a seam back onto the loop (SPEC.md 125.3);
;   - the sound is PCM at the bundle's rate on a Sound Blaster (SPEC.md
;     34.5.3's external ring): the music loops, one effect at a time is
;     mixed into it, and the card is the clock the pictures keep to.
;
; THREE PIECES, and where each runs (the Video Player's own shape, 98.3):
;   vb_hook    the FSXF_RATE hook - IRQ0 at 582 Hz, IF=0, 48 bytes of stack.
;              It draws the frames that are due from memory (the idle loop,
;              resident; a clip, as far as it has been read), keeps the
;              card's ring topped up, and starts each action at its anchor.
;   vb_main    the bracket's foreground - the UI task. It reads the keys,
;              runs the battle, draws the band, and READS AHEAD: the clip
;              under the menu's cursor is read while the player decides, a
;              piece at a time, and a new cursor position drops that read
;              and starts the new one (what was read is kept).
;   the rest   the window: a title, Enter to play, and what went wrong.
; =============================================================================

%include "os88api.inc"
    OS88_HEADER 'Video Battle', vb_entry, 1
    OS88_ICON16
    ; a sword over a shield: one solid shape on every adapter (SPEC.md 39.4)
    dw 0x0000, 0x0003, 0x0007, 0x000E, 0x001C, 0x3FB8, 0x7FF0, 0x7FE0
    dw 0x7FC0, 0x7FE0, 0x3FF0, 0x3FB8, 0x1F1C, 0x0E0E, 0x0407, 0x0003
    dw 0x0007, 0x000F, 0x001F, 0x003F, 0x7FFE, 0xFFFC, 0xFFF8, 0xFFF0
    dw 0xFFF0, 0xFFF8, 0xFFFC, 0x7FFE, 0x3FBF, 0x1F1F, 0x0E0F, 0x0407
    OS88_ICON16_END

; --- the bundle's layout (SPEC.md 125.2; tools/os88vbt.py is the writer) ------
VH_MAGIC    equ 0               ; 'VBT1'
VH_VER      equ 4               ; 4
VH_PAL      equ 8               ; CGA4 palette byte (98.1.3.3)
VH_FPS      equ 10
VH_RATE     equ 12
VH_SPF      equ 14              ; samples a frame
VH_NCLIPS   equ 16
VH_NIDLE    equ 18
VH_NOPEN    equ 20              ; the openings
VH_NSFX     equ 22              ; the menu's effects
VH_UI       equ 24              ; five bytes: move, open, close, take, deny
VH_VRES     equ 30              ; dword offset, word length
VH_MUSIC    equ 36              ; dword offset (the music region)
VH_MLEN     equ 44              ; dword
VH_MUSAT    equ 48              ; dword: the sample the music comes in at
VH_SFX      equ 56              ; dword offset, word length: the MENU's
VH_IDLETAB  equ 62              ; word: the idle table's offset in the block
VH_BAND     equ 64              ; bytes: the band's cell, row, cells, rows
VH_MAXREC   equ 76              ; word: the largest record
VH_OPENS    equ 80              ; 8 bytes: the openings' clips, in turn
VH_METALEN  equ 326             ; word: the first region's length, 8 KB to
                                ; 16 KB (the rules' 4 KB at a time)
VH_IMG      equ 320             ; dword offset, word length: the band's
                                ; pictures (125.5)
VH_ENT      equ 88              ; dword offset, dword length: the ENTRIES
                                ; (125.2), each named by its paragraph
VH_SFXTAB   equ 96              ; 16 x (dword offset, word length)
VB_CTAB     equ 512             ; the clip table, 48 bytes a clip
VC_OFF      equ 0               ; dword: the stream's offset
VC_LEN      equ 4               ; dword: its length
VC_NREC     equ 8
VC_FLAGS    equ 10
VC_EXIT     equ 12
VC_ANCH     equ 14              ; its anchors in the block: (frame, entry)
VC_NANCH    equ 16
CF_JOINS    equ 1
CF_FULL     equ 2
CF_ENDS     equ 4
CF_OWNSND   equ 8               ; its sound is played instead of the music
CF_OPENING  equ 16
CF_LOOP     equ 32              ; a title or end loop (125.3.1)
CF_ENTER    equ 64              ; opening 0, entered at the title's frames
VH_TITLE    equ 330             ; the title loop's clip, FFh none
VH_LEAVE    equ 331             ; FFh: the title is left by a fade
VH_ENDS     equ 332             ; victory, defeat: (how, end loop) bytes
VH_FSTEPS   equ 336             ; a fade's steps, 1..16
VH_KIND     equ 337             ; the adapter: VK_CGA4, VK_VGA4, VK_HERC
VH_VPAL     equ 352             ; VGA4's sixteen (r, g, b), six bits each
VK_CGA4     equ 0
VK_VGA4     equ 1
VK_HERC     equ 2
VB_HORG     equ 5               ; Hercules: the 640-wide canvas at byte 5
VH_IDLE     equ 338             ; dword offset, dword length: the IDLE loop's
                                ; records, by paragraph (format 7)
EH_HOLD     equ 0
EH_FADE     equ 1
EH_JOIN     equ 2
EH_AGAIN    equ 3
EH_AGAINJ   equ 4               ; (after a once-clip: JOIN the title, the
                                ; play again with no fade - 125.3.1)
VH_ETHEN    equ 346             ; victory, defeat: what follows an end clip
                                ; that plays ONCE (EH_HOLD/AGAIN/AGAINJ)
RH_LEN      equ 0               ; a record: its length, all of it
RH_ALEN     equ 2               ; ...its frame's sound's, at its end
RH_SIZE     equ 4

VB_META     equ 16384           ; the first region, read whole into our bss:
                                ; the header, the clip table, the RULES
VB_MAXCLIP  equ 64
VB_MAXOPEN  equ 8
VB_PIECE    equ 4096            ; a read: a cluster multiple, and under a
                                ; 360 KB track (9 sectors), so a key waits at
                                ; most about a revolution for it
VB_CRMIN    equ 48              ; a CLIP RING: as many KB as the machine has
                                ; (125.4), split two ways - and at least this
VB_RKEEP    equ 8               ; KB left free when the rings are sized
VB_CGUARD   equ 16384           ; 16 KB guard past it: the tail of the record
                                ; that runs past the end, copied, so a record
                                ; (at most the guard, VH_MAXREC) is whole at
                                ; its cursor (125.4)
VB_SPIECE   equ 32768           ; the most a read moves while no menu waits
                                ; on a key: as much as the ring has room for,
                                ; up to this (125.4)
VB_HEAD     equ 40960           ; a clip may START once this much is read
                                ; (or all of it): the rest streams as it plays
VB_MR       equ 32768           ; the music's ring (3 s at 11 kHz), read
                                ; 4 KB a piece into a 4 KB guard past its end
VB_MPIECE   equ 4096
VB_MRES     equ 61440           ; music this short is RESIDENT instead: read
                                ; once and looped out of memory, so a floppy
                                ; spends its reads on the clips (125.4)
VB_RMAX     equ 32768           ; vb_read's most in one call
VB_RL       equ 16384           ; the card's ring, code 2
VB_RLCODE   equ 2
VB_LEAD     equ 4096            ; how far the ring runs ahead of the card:
                                ; four of its blocks, at most - two when the
                                ; driver gives the stream 512-byte blocks
VB_BLKCODE  equ 2               ; ...which it does with SND_CAP_EXTBLK: 2,048
                                ; >> 2 (SND_OPENF_BLKSH, SPEC.md 34.5.3)
VB_KQUIET   equ 6               ; ticks after a key before a clip is read
VB_FORGIVE  equ 11025           ; a picture this far behind its sound is let
                                ; go late (a second at 11 kHz)
VB_DIV      equ 2048            ; the hook's PIT divisor: 582.6 Hz
VB_SPP_LO   equ 0xECB0          ; 11,025 samples / 582.6 periods in 16.16:
VB_SPP_HI   equ 0x0012          ; 18.9246 (the clock with no card)

; a clip ring (vb_rg0, vb_rg1). A place in it is a SEGMENT and an offset
; of 0..15 (vb_cadv): a ring is as big as the machine allows, so a 16-bit
; index cannot hold it, and a stream's offsets are dwords
RG_SEG      equ 0               ; its segment, its guard VB_CGUARD past its end
RG_CLIP     equ 2               ; the clip it holds, FFh none
RG_PLAY     equ 3               ; the hook plays from it
RG_FILL     equ 4               ; dword: the stream's bytes read into it
RG_FSEG     equ 8               ; ...and where the next lands (a piece is 4 KB
                                ; whole, so a segment alone)
RG_VOFF     equ 10              ; dword: the picture's cursor, in the stream
RG_VSEG     equ 14              ; ...and its place in the ring: segment,
RG_VOFS     equ 16              ; offset
RG_AOFF     equ 18              ; the sound's cursor, the same way
RG_ASEG     equ 22
RG_AOFS     equ 24
RG_VDONE    equ 26              ; its picture is over
RG_ADONE    equ 27              ; ...and its sound
RG_CUR      equ 28              ; its own file cursor (FSEQ_SIZE)
RG_POFF     equ 44              ; dword: the next record not yet walked,
RG_PSEG     equ 48              ; and its place
RG_POFS     equ 50
RG_END      equ 52              ; the segment of its end (its guard's start)
RG_PARA     equ 54              ; its size: paragraphs...
RG_BYTES    equ 56              ; ...and bytes (a dword)
RG_FOFF     equ 60              ; dword: the next read's place in the stream
                                ; - the fill's, but for a loop read ROUND,
                                ; which goes back to its lap (format 7)
RG_LCSET    equ 64              ; ...the cursor at its lap's start is kept
RG_LCUR     equ 66              ; ...here: a lap read again is not a seek
RG_INP      equ RG_LCUR + FSEQ_SIZE ; IN PLACE (vbinp.inc): its clip is read
                                ; where the hold has it, nothing copied
RG_HB       equ RG_INP + 2      ; ...dword: the clip's first byte in the block
RG_SIZE     equ RG_HB + 4
VC_LAP      equ 18              ; a loop's lap: dword offset in its stream,
VC_LAPK     equ 22              ; ...and its length in 4 KB

; VB_STRM ring, row - ZF=0 (jnz) when clip ROW in ring RING is a loop too big
; to be held in its ring, which the reader reads round. AX, DX spent
%macro VB_STRM 2
    test word [%2+VC_FLAGS], CF_LOOP
    jz %%out
    mov ax, 0                       ; (IN PLACE: held, the hold its ring)
    cmp byte [%1+RG_INP], 0
    jne %%no
    mov ax, [%2+VC_LEN]
    mov dx, [%2+VC_LEN+2]
    add ax, VB_PIECE
    adc dx, 0
    cmp dx, [%1+RG_BYTES+2]
    jne %%cmp
    cmp ax, [%1+RG_BYTES]
%%cmp:
    mov ax, 0                       ; (flags kept)
    jbe %%no
    inc ax
%%no:
    or ax, ax
%%out:
%endmacro

; VB_DSDX - in place of `mov ds, dx` after a `push ds`, where DX may be
; VB_XH: the record in place in XMS (vbinp.inc), whose borrow loads DS's
; hidden base - flags pushed, IF = 0, until VB_DSEND. Video Player's VP_DSDX
; with ONE pending address ([vb_xpc]): the battle names a record and reads
; it at once, with nothing between
VB_XH       equ 0xFFFF
%macro VB_DSDX 0
    cmp dx, VB_XH
    jne %%n
    pushf
    cli
    call vb_xbeg
    jmp short %%d
%%n:
    mov ds, dx
%%d:
%endmacro
; VB_DSEND - before the `pop ds` that ends a VB_DSDX. CLOBBERS FLAGS
%macro VB_DSEND 0
    cmp byte [cs:vb_xact], 0
    je %%n
    call vb_xfin
    popf
%%n:
%endmacro

; the hook's modes
VM_OFF      equ 0
VM_CLIP     equ 1               ; a clip's records, in order
VM_IDLE     equ 2               ; the idle loop
VM_HOLD     equ 3               ; an ending's last frame, held
VM_FADE     equ 4               ; the frame on the glass faded (the UI task's)

; the battle's states
GS_INTRO    equ 0
GS_MENU     equ 1
GS_WAIT     equ 2               ; an action pending or playing
GS_END      equ 3

; =============================================================================
; the entry proc and the window
; =============================================================================
vb_entry:
    mov word [vb_rcur], vb_cur
    mov si, vb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [vb_win], bx
    OS88_REGION_MOVABLE
    mov word [vb_status], vb_s_ready
    mov bx, [vb_win]
    clc
.out:
    ret

; vb_paint - W_PAINT: four lines of text, the rest of the box filled once
vb_paint:
    push si
    mov bx, [vb_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [vb_px], ax
    mov [vb_py], dx
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    mov [vb_pw], cx
    mov [vb_ph], dx
    mov ax, cx
    mov cl, 3
    shr ax, cl
    cmp ax, 60
    jbe .c
    mov ax, 60
.c:
    mov [vb_pcols], ax
    xor bp, bp                      ; the line
.line:
    mov bx, bp
    mov si, [vb_lines+bx]
    or si, si
    jnz .have
    mov si, [vb_status]
.have:
    mov di, vb_pbuf                 ; padded to the window's width: the
    mov cx, [vb_pcols]              ; cells cover the row, once
.cp:
    lodsb
    or al, al
    jnz .st
    dec si
    mov al, ' '
.st:
    mov [di], al
    inc di
    loop .cp
    mov byte [di], 0
    mov cx, [vb_px]
    mov ax, bp
    shr ax, 1
    mov dx, 10
    mul dx
    add ax, 2
    add ax, [vb_py]
    mov dx, ax
    mov si, vb_pbuf
    mov ax, CBLACK + CWHITE * 256
    cmp bp, 0
    jne .ink
    mov ax, CWHITE + CBLACK * 256   ; the title, inverse
.ink:
    call OSAPI_FONT_RUN
    add bp, 2
    cmp bp, 8
    jb .line
    ; what the cells do not cover: the gaps between lines, the bottom and
    ; the right edge - each pixel once
    mov al, CWHITE
    call OSAPI_SET_COLOR
    xor bp, bp
.gap:
    mov ax, bp
    mov dx, 10
    mul dx
    mov bx, ax                      ; y1 = 10i (+0, +1), or 10i + 10 before
    add bx, [vb_py]
    mov dx, bx
    inc dx                          ; rows 10i, 10i+1: above line i
    mov ax, [vb_px]
    mov cx, ax
    add cx, [vb_pw]
    dec cx
    call OSAPI_GFX_FILL
    inc bp
    cmp bp, 4
    jb .gap
    mov bx, [vb_py]                 ; below the last line
    add bx, 40
    mov dx, [vb_py]
    add dx, [vb_ph]
    dec dx
    cmp dx, bx
    jl .right
    mov ax, [vb_px]
    mov cx, ax
    add cx, [vb_pw]
    dec cx
    call OSAPI_GFX_FILL
.right:
    mov ax, [vb_pcols]              ; right of the cells, rows 2..41
    mov cl, 3
    shl ax, cl
    cmp ax, [vb_pw]
    jae .done
    add ax, [vb_px]
    mov cx, [vb_px]
    add cx, [vb_pw]
    dec cx
    mov bx, [vb_py]
    mov dx, bx
    add bx, 2
    add dx, 39
    call OSAPI_GFX_FILL
.done:
    pop si
    cld
    ret

; vb_onkey - Enter plays; anything else is the window's
vb_onkey:
    cmp al, 13
    je vb_play
    cmp al, ' '
    je vb_play
    ret
vb_onclick:
    push si
    call vb_play
    pop si
    ret

; vb_play - load what the play needs before it starts (the window says so),
; then the full screen. From a UI callback, the gfx lock held (53.1)
vb_play:
    push si
    mov word [vb_status], vb_s_load
    mov si, [vb_win]
    call vb_paint
    call vb_load
    jc .say
    mov ax, vb_main
    mov bx, [vb_win]
    mov cx, FSXF_RATE
    mov dx, VB_DIV
    mov di, vb_hook
    call OSAPI_FSX_RUN
    jnc .back
    mov word [vb_err], vb_s_norate
.say:
    mov ax, [vb_err]
    mov [vb_status], ax
    jmp short .paint
.back:
    mov ax, [vb_err]                ; the play's own word, or ready
    or ax, ax
    jnz .s2
    mov ax, vb_s_again
.s2:
    mov [vb_status], ax
.paint:
    mov si, [vb_win]
    call vb_paint
    pop si
    ret

; =============================================================================
; loading (the UI task)
; =============================================================================

; vb_read - read CX bytes (a cluster multiple) at file offset DX:AX into
; ES:BX, in pieces of at most 32 KB, through the cursor [vb_rcur] names
; (vb_cur unless a stream's own: the music's, a ring's). A cursor already
; at DX:AX goes on from there; any other is SEEKED, which costs the kernel
; a walk of the file's chain from its start (FILE_READ_SEQ, 18.4.8) - the
; reason each stream keeps its own (125.4). out CF=1 on an error or a
; short file. Clobbers AX, BX, CX, DX; [vb_rcur] back to vb_cur. Out of
; the HOLD in extended memory when it has all of the bytes (vbhold.inc)
vb_read:
    cmp byte [vb_hon], 0
    je vb_dread
    cmp byte [vb_hbyp], 0           ; (the hold's own fill: the disk)
    jne vb_dread
    call vb_hread
    jc vb_dread
    ret
; vb_dread - vb_read from the disk
vb_dread:
    push si
    push di
    push bp
    mov [vb_rseg], es
    mov [vb_rcap], cx
    add [vb_rbytes], cx             ; (what was read, and in how many calls:
    adc word [vb_rbytes+2], 0       ; a reader's own account, 125.4)
    inc word [vb_rcalls]
    pushf
    cli
    mov di, [vb_hper]               ; (the periods it takes)
    mov [vb_rp0], di
    mov di, [vb_hper+2]
    mov [vb_rp0+2], di
    popf
    mov di, [vb_rcur]
    cmp ax, [di+FSEQ_OFF]           ; where it is already: no seek
    jne .seek
    cmp dx, [di+FSEQ_OFF+2]
    je .at
.seek:
    xor cx, cx                      ; seek: zero the cursor, set the offset
    mov [di+0], cx
    mov [di+2], cx
    mov [di+4], cx
    mov [di+6], cx
    mov [di+8], cx
    mov [di+10], cx
    mov [di+FSEQ_OFF], ax
    mov [di+FSEQ_OFF+2], dx
.at:
    mov bp, bx
.more:
    mov cx, [vb_rcap]
    jcxz .ok
    cmp cx, VB_RMAX
    jbe .n
    mov cx, VB_RMAX
.n:
    mov dx, [vb_rseg]
    mov bx, bp
    push ds
    pop es
    mov di, [vb_rcur]
    mov si, [vb_namep]
    push cx
    call OSAPI_FILE_READ_SEQ
    pop cx
    jc .bad
    or dx, dx
    jnz .bad
    or ax, ax
    jz .short                       ; the end, early
    cmp ax, cx
    jbe .got
    mov ax, cx
.got:
    add bp, ax
    sub [vb_rcap], ax
    jmp .more
.short:
    cmp word [vb_rcap], 0
    je .ok
.bad:
    stc
    jmp short .out
.ok:
    clc
.out:
    pushf
    cli
    mov ax, [vb_hper]
    mov dx, [vb_hper+2]
    sub ax, [vb_rp0]
    sbb dx, [vb_rp0+2]
    add [vb_rper], ax
    adc [vb_rper+2], dx
    popf
    mov es, [vb_rseg]
    mov word [vb_rcur], vb_cur      ; (the next read's own unless it says)
    pop bp
    pop di
    pop si
    ret

; vb_tmark - the periods since the last mark added to the dword at SI
; (a diagnostic: where the foreground's time goes, 125.7). AX, DX spent
vb_tmark:
    pushf
    cli
    mov ax, [vb_hper]
    mov dx, [vb_hper+2]
    xchg ax, [vb_tlast]
    xchg dx, [vb_tlast+2]
    neg ax
    adc dx, 0
    neg dx
    add ax, [vb_tlast]
    adc dx, [vb_tlast+2]
    add [si], ax
    adc [si+2], dx
    popf
    ret

; vb_entpara - AX = the paragraphs of the region whose (offset, length)
; dwords are at header offset DI (the entries', the idle loop's), rounded
; to 4 KB. DX spent
vb_entpara:
    mov ax, [vb_meta+di+4]
    mov dx, [vb_meta+di+6]
    add ax, 4095
    adc dx, 0
    and ax, 0xF000
    mov cx, 4
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    ret

; vb_entload - the region at header offset DI (the entries, the idle loop)
; into its part of the resident claim at segment AX, 32 KB a read, each
; read's segment on by its length. CF=1 a read failed
vb_entload:
    mov [vb_enext], ax
    call vb_entpara
    mov [vb_eleft], ax              ; paragraphs to go
    mov ax, [vb_meta+di]
    mov dx, [vb_meta+di+2]
.r:
    mov cx, [vb_eleft]
    jcxz .ok
    cmp cx, 32768 / 16
    jbe .n
    mov cx, 32768 / 16
.n:
    sub [vb_eleft], cx
    push ax
    push dx
    push cx
    shl cx, 1                       ; paragraphs to bytes (at most 32 KB)
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov es, [vb_enext]
    xor bx, bx
    call vb_read
    pop cx
    pop dx
    pop ax
    jc .out
    add [vb_enext], cx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add ax, cx
    adc dx, 0
    jmp short .r
.ok:
    clc
.out:
    ret

; vb_round - AX = CX rounded up to 4 KB (a cluster multiple, 125.2)
vb_round:
    mov ax, cx
    add ax, 4095
    jnc .ok
    mov ax, 0xF000                  ; (a region is at most 60 KB)
    ret
.ok:
    and ax, 0xF000
    ret

; vb_kb - AX = DX:AX bytes in KB, rounded up (a claim's size). CX spent
vb_kb:
    add ax, 1023
    adc dx, 0
    mov cx, 10
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    ret

; vb_p2kb - AX = AX paragraphs in KB, rounded up. CX, DX spent
vb_p2kb:
    xor dx, dx
    mov cx, 4
.k:
    shl ax, 1
    rcl dx, 1
    loop .k
    jmp vb_kb

; vb_load - what a play needs before it starts, in TWO claims - an owner may
; hold eight (MEM_OWNER_MAX, SPEC.md 50): the RESIDENT claim, carved into
; the idle loop's block, the menu's effects, the music's ring (or all of a
; short music) and the two CLIP RINGS; and the card's ring (vb_snd_open).
; Then the first opening's head and the music's ring are read. CF=1 and
; [vb_err] on failure
vb_load:
    mov word [vb_err], 0
    cmp byte [vb_loaded], 0
    je .first
    jmp .again
.first:
    ; WHICH BUNDLE: this adapter's own rendition first (VBATTLE-MEMORY-PLAN
    ; 9) - BATTLEV.VBT on a VGA, BATTLEH.VBT on a Hercules - and on a VGA
    ; or anything else the CGA4 one, BATTLE.VBT, in mode 4
    mov word [vb_namep], vb_name
    mov bx, [vb_win]
    call OSAPI_FSX_CAPS             ; AX = modes, DL = VID_*
    mov si, vb_namev
    cmp dl, VID_VGA
    jne .nh
    test ax, 1 << FSXM_VGA0D
    jnz .try
.nh:
    mov si, vb_nameh
    cmp dl, VID_HERC
    jne .hdr
.try:
    mov [vb_namep], si
    call .head
    jnc .meta0
    mov word [vb_namep], vb_name    ; (not there: the CGA4 one)
.hdr:
    call .head
    mov word [vb_err], vb_s_nofile
    jc .fail1
    jmp short .meta0
.head:
    push ds                         ; the first region, into our bss: its
    pop es                          ; first 4 KB (the header and the clip
    mov bx, vb_meta                 ; table), then the rest the header says
    mov cx, 4096                    ; it has (the rules, 125.2)
    xor ax, ax
    xor dx, dx
    jmp vb_read
.meta0:
    mov cx, [vb_meta+VH_METALEN]
    cmp cx, VB_META
    ja .fail1
    sub cx, 4096
    jbe .meta
    push ds
    pop es
    mov bx, vb_meta+4096
    mov ax, 4096
    xor dx, dx
    call vb_read
    jc .fail1
.meta:
    mov word [vb_err], vb_s_notvbt
    cmp word [vb_meta+VH_MAGIC], 'VB'
    jne .fail1
    cmp word [vb_meta+VH_MAGIC+2], 'T1'
    jne .fail1
    cmp word [vb_meta+VH_VER], 7
    jne .fail1
    mov ax, [vb_meta+VH_NCLIPS]
    or ax, ax
    jz .fail1
    cmp ax, VB_MAXCLIP
    ja .fail1
    cmp word [vb_meta+VH_NOPEN], VB_MAXOPEN
    ja .fail1
    mov ax, [vb_meta+VH_MAXREC]     ; the rings' GUARD: 16 KB, or 32 for a
    mov byte [vb_cgkb], VB_CGUARD / 1024    ; rendition whose whole-screen
    cmp ax, VB_CGUARD               ; record is more (VGA's planes, Hercules')
    jbe .g16
    mov byte [vb_cgkb], 2 * VB_CGUARD / 1024
    cmp ax, 2 * VB_CGUARD
    ja .fail1                       ; (a record past the guard: not ours)
.g16:
    mov ax, [vb_meta+VH_MLEN]       ; the music: at least a piece
    or ax, [vb_meta+VH_MLEN+2]
    jz .fail1
    cmp word [vb_meta+VH_SPF], 0
    jne .sized
.fail1:
    jmp .fail
.sized:
    ; the resident claim's parts, each rounded to 4 KB, in paragraphs: the
    ; block, the menu's effects, the music (its ring and guard, or all of
    ; it), the entries. The clip rings are claimed per play, in the bracket
    ; (vb_rclaim)
    mov cx, [vb_meta+VH_VRES+4]
    call vb_round
    mov cl, 4
    shr ax, cl
    mov [vb_psfx], ax
    mov cx, [vb_meta+VH_SFX+4]
    call vb_round
    mov cl, 4
    shr ax, cl
    add ax, [vb_psfx]
    mov [vb_pmus], ax
    mov byte [vb_mres], 0
    cmp word [vb_meta+VH_MLEN+2], 0 ; short enough to be resident?
    jne .ring
    cmp word [vb_meta+VH_MLEN], VB_MRES
    ja .ring
    mov byte [vb_mres], 1
    mov cx, [vb_meta+VH_MLEN]
    push ax
    call vb_round
    mov cl, 4
    shr ax, cl
    pop cx
    add ax, cx
    jmp short .rings
.ring:
    add ax, (VB_MR + VB_MPIECE) / 16
.rings:
    mov [vb_pent], ax               ; the entries: as many 4 KB as they
    mov di, VH_ENT                  ; take, in paragraphs
    call vb_entpara
    add ax, [vb_pent]
    jc .fail1
    mov [vb_pidl], ax               ; the idle loop's records, the same way
    mov di, VH_IDLE                 ; (format 7)
    call vb_entpara
    add ax, [vb_pidl]
    jc .fail1
    mov [vb_pimg], ax               ; the band's pictures (125.5)
    mov cx, [vb_meta+VH_IMG+4]
    call vb_round
    mov cl, 4
    shr ax, cl
    add ax, [vb_pimg]
    mov [vb_pbband], ax             ; ...and the band's two buffers, its
    add ax, 2 * 16384 / 16          ; ground and its composition, 16 KB each
    call vb_p2kb
    mov word [vb_err], vb_s_nomem
    call OSAPI_MEM_CLAIM
    jc .fail1
    mov [vb_vres], dx
    mov ax, dx
    add ax, [vb_psfx]
    mov [vb_sfxseg], ax
    mov ax, dx
    add ax, [vb_pmus]
    mov [vb_mring], ax
    mov ax, dx
    add ax, [vb_pent]
    mov [vb_eseg], ax
    mov ax, dx
    add ax, [vb_pidl]
    mov [vb_idseg], ax
    mov ax, dx
    add ax, [vb_pimg]
    mov [vb_iseg], ax
    mov ax, dx
    add ax, [vb_pbband]
    mov [vb_bseg], ax
    add ax, 16384 / 16
    mov [vb_cseg], ax
    call vb_cardclaim               ; the card's ring, before the rings are
                                    ; sized out of what is left
    ; the block and the menu's effects
    mov word [vb_err], vb_s_badread
    mov es, [vb_vres]
    xor bx, bx
    mov cx, [vb_meta+VH_VRES+4]
    call vb_round
    mov cx, ax
    mov ax, [vb_meta+VH_VRES]
    mov dx, [vb_meta+VH_VRES+2]
    call vb_read
    jc .fail1
    mov cx, [vb_meta+VH_SFX+4]
    jcxz .read
    call vb_round
    mov cx, ax
    mov es, [vb_sfxseg]
    xor bx, bx
    mov ax, [vb_meta+VH_SFX]
    mov dx, [vb_meta+VH_SFX+2]
    call vb_read
    jc .fail1
.read:
    mov di, VH_ENT                  ; the entries, 32 KB a read
    mov ax, [vb_eseg]
    call vb_entload
    jc .fail1
    mov di, VH_IDLE                 ; the idle loop
    mov ax, [vb_idseg]
    call vb_entload
    jc .fail1
    mov cx, [vb_meta+VH_IMG+4]      ; the band's pictures
    jcxz .noimg
    call vb_round
    mov cx, ax
    mov es, [vb_iseg]
    xor bx, bx
    mov ax, [vb_meta+VH_IMG]
    mov dx, [vb_meta+VH_IMG+2]
    call vb_read
    jc .fail1
.noimg:
    call vb_hopen                   ; the hold, where there is a pool
    mov byte [vb_loaded], 1
    call vb_fontload
.again:
    ; the music from where it comes in, its ring filled: it plays when the
    ; openings are over
    xor ax, ax
    mov [vb_mfill], ax
    mov [vb_mtake], ax
    mov [vb_munder], ax
    mov ax, [vb_meta+VH_MUSAT]
    mov dx, [vb_meta+VH_MUSAT+2]
    and ax, 0xF000                  ; (a ring reads from a cluster boundary,
    mov [vb_mrd], ax                ; and is taken from the join itself)
    mov [vb_mrd+2], dx
    mov ax, [vb_meta+VH_MUSAT]
    and ax, 0x0FFF
    mov [vb_mskip], ax
    cmp byte [vb_mres], 0
    je .mfill
    mov ax, [vb_meta+VH_MUSAT]      ; RESIDENT: the join is an offset
    mov [vb_mtake], ax
    cmp byte [vb_mread], 0          ; read once a session
    jne .ok
    mov es, [vb_mring]
    xor bx, bx
    mov cx, [vb_meta+VH_MLEN]
    call vb_round
    mov cx, ax
    mov ax, [vb_meta+VH_MUSIC]
    mov dx, [vb_meta+VH_MUSIC+2]
    call vb_read
    mov word [vb_err], vb_s_badread
    jc .fail
    mov byte [vb_mread], 1
    jmp short .ok
.mfill:
    call vb_mpiece
    mov word [vb_err], vb_s_badread
    jc .fail
    or al, al
    jnz .mfill
    mov ax, [vb_mskip]              ; the join: the ring's first bytes taken
    mov [vb_mtake], ax
.ok:
    mov word [vb_err], 0
    clc
    ret
.fail:
    stc
    ret

; vb_crec - SI = clip BX's row in the clip table, 48 bytes. BX kept
vb_crec:
    push bx
    mov si, bx
    mov cl, 4
    shl si, cl                      ; 16 x clip
    mov bx, si
    shl bx, 1                       ; + 32 x clip
    add si, bx
    add si, vb_meta+VB_CTAB
    pop bx
    ret

; vb_rreset - ring BX given to clip AL (FFh: none), empty, from the stream's
; first byte. Only a ring the hook is not playing from
vb_rreset:
    mov [bx+RG_CLIP], al
    xor ax, ax
    mov [bx+RG_FILL], ax
    mov [bx+RG_FILL+2], ax
    mov [bx+RG_VOFF], ax
    mov [bx+RG_VOFF+2], ax
    mov [bx+RG_VOFS], ax
    mov [bx+RG_AOFF], ax
    mov [bx+RG_AOFF+2], ax
    mov [bx+RG_AOFS], ax
    mov [bx+RG_POFF], ax
    mov [bx+RG_POFF+2], ax
    mov [bx+RG_POFS], ax
    mov [bx+RG_FOFF], ax
    mov [bx+RG_FOFF+2], ax
    mov [bx+RG_LCSET], al
    mov [bx+RG_PLAY], al
    mov [bx+RG_VDONE], al
    mov [bx+RG_ADONE], al
    mov ax, [bx+RG_SEG]
    mov [bx+RG_FSEG], ax
    mov [bx+RG_VSEG], ax
    mov [bx+RG_ASEG], ax
    mov [bx+RG_PSEG], ax
    jmp vb_iring                    ; (in place, when the hold may be read so)

; vb_cadv - ring BX's place at [bx+di] (a segment, then an offset 0..15) on
; by CX bytes, wrapped at the ring's end: a record that ran past it was
; whole in the guard, and its tail is at the ring's start. AX, CX, DX
; spent. From the hook too: no kernel call, two bytes of stack
vb_cadv:
    mov ax, [bx+di+2]
    add ax, cx
    mov dx, ax
    and ax, 15
    mov [bx+di+2], ax
    mov cl, 4
    shr dx, cl
    add dx, [bx+di]
    cmp dx, [bx+RG_END]
    jb .in
    sub dx, [bx+RG_PARA]
.in:
    mov [bx+di], dx
    ret

; vb_held - DX = what ring BX holds read past the stream offset at [bx+di]
; (a dword), FFFFh for that or more. AX spent
vb_held:
    mov dx, [bx+RG_FILL]
    mov ax, [bx+RG_FILL+2]
    sub dx, [bx+di]
    sbb ax, [bx+di+2]
    jz .r
    mov dx, 0xFFFF
.r:
    ret

; vb_cardclaim - the card's ring, page-safe, from the top, when there is a
; card that takes a background stream (vb_snd_open uses it). Claimed once
vb_cardclaim:
    cmp word [vb_ring], 0
    jne .out
    call OSAPI_SND_CAPS
    test ax, SND_CAP_PCM_BG
    jz .out
    mov ax, VB_RL / 1024 + 1
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA_HI
    jc .out
    mov [vb_ring], dx
.out:
    ret

; vb_rclaim - the two CLIP RINGS, as big as the machine allows: the largest
; run less VB_RKEEP, split two ways, each less its guard and cut to whole
; 4 KB (125.4). Nothing else can claim while the bracket runs, and they are
; given back as it ends (vb_rfree). CF=1 and [vb_err] when there is not
; room for VB_CRMIN each
vb_rclaim:
    call OSAPI_MEM_AVAIL            ; AX = the largest run, KB
    sub ax, VB_RKEEP
    jc .no
    shr ax, 1
    xor dh, dh
    mov dl, [vb_cgkb]
    sub ax, dx
    jc .no
    and ax, 0xFFFC
    cmp ax, VB_CRMIN
    jb .no
    mov [vb_rkb], ax
    add ax, dx
    shl ax, 1
    call OSAPI_MEM_CLAIM
    jc .no
    mov [vb_rclm], dx
    mov bx, vb_rg0
    call .set
    mov ax, [vb_rkb]
    add al, [vb_cgkb]
    adc ah, 0
    mov cl, 6
    shl ax, cl
    add dx, ax
    mov bx, vb_rg1
    call .set
    clc
    ret
.no:
    mov word [vb_err], vb_s_nomem
    stc
    ret
.set:                               ; ring BX at segment DX
    mov [bx+RG_SEG], dx
    mov ax, [vb_rkb]
    mov cl, 6
    shl ax, cl
    mov [bx+RG_PARA], ax
    add ax, dx
    mov [bx+RG_END], ax
    mov ax, [vb_rkb]
    mov cl, 10
    push dx
    xor dx, dx
.k:
    shl ax, 1
    rcl dx, 1
    dec cl
    jnz .k
    mov [bx+RG_BYTES], ax
    mov [bx+RG_BYTES+2], dx
    pop dx
    mov al, 0xFF
    call vb_rreset
    ret

; vb_rfree - the clip rings given back
vb_rfree:
    xor dx, dx
    xchg dx, [vb_rclm]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
.out:
    ret

; vb_rhead - CF=1 when ring BX holds its clip's head: VB_HEAD of it, or all
vb_rhead:
    push bx
    mov al, [bx+RG_CLIP]
    xor ah, ah
    mov bx, ax
    call vb_crec
    pop bx
    cmp word [bx+RG_FILL+2], 0
    jne .yes
    mov ax, VB_HEAD
    cmp word [si+VC_LEN+2], 0
    jne .h
    cmp ax, [si+VC_LEN]
    jbe .h
    mov ax, [si+VC_LEN]
.h:
    cmp [bx+RG_FILL], ax
    jae .yes
    clc
    ret
.yes:
    stc
    ret

; vb_rpiece - one piece of ring BX's clip, at the stream's fill position,
; when it is not all read and the ring has room past what the hook still
; wants of it (its picture's cursor and its sound's). A piece ends at the
; ring's end at the most. The records it completes are walked, and the one
; that runs past the end has its tail copied past it, into the guard, so a
; record is always whole at its cursor (125.4) - that tail and nothing
; else. Out AL = 1 a piece was read, 0 nothing to read; CF=1 a read failed
vb_rpiece:
    push si
    push di
    push bp
    mov bp, bx
    mov al, [ds:bp+RG_CLIP]
    cmp al, 0xFF
    je .none
    xor ah, ah
    mov bx, ax
    call vb_crec
    VB_STRM ds:bp, si               ; a loop read round is never all read
    jnz .some
    mov ax, [ds:bp+RG_FILL]         ; all of it read?
    mov dx, [ds:bp+RG_FILL+2]
    cmp dx, [si+VC_LEN+2]
    jb .some
    ja .none
    cmp ax, [si+VC_LEN]
    jae .none
.some:
    mov ax, [ds:bp+RG_VOFF]         ; room: the ring less what is still
    mov dx, [ds:bp+RG_VOFF+2]       ; wanted past the EARLIER cursor
    cmp dx, [ds:bp+RG_AOFF+2]
    jb .e1
    ja .e2
    cmp ax, [ds:bp+RG_AOFF]
    jbe .e1
.e2:
    mov ax, [ds:bp+RG_AOFF]
    mov dx, [ds:bp+RG_AOFF+2]
.e1:
    add ax, [ds:bp+RG_BYTES]        ; the ring's bytes, less the fill past
    adc dx, [ds:bp+RG_BYTES+2]      ; that cursor
    sub ax, [ds:bp+RG_FILL]
    sbb dx, [ds:bp+RG_FILL+2]
    jb .none
    or dx, dx
    jz .r1
    mov ax, 0xF000                  ; (64 KB and more: more than a piece)
.r1:
    and ax, 0 - VB_PIECE            ; in whole pieces
    jz .none
    call vb_psize                   ; the piece: as much of it as the
    mov dx, [ds:bp+RG_END]          ; moment allows, and no further than
    sub dx, [ds:bp+RG_FSEG]         ; the ring's end: nothing is read into
    cmp dx, 0x1000                  ; the guard, so nothing is copied out
    jae .p1
    mov cl, 4
    shl dx, cl
    cmp ax, dx
    jbe .p1
    mov ax, dx
.p1:
    mov [vb_pn], ax
    mov ax, [si+VC_LEN]             ; and no further than the clip's last
    mov dx, [si+VC_LEN+2]           ; piece: the last clip in the bundle
    sub ax, [ds:bp+RG_FOFF]         ; ends at the file's end, and a read
    sbb dx, [ds:bp+RG_FOFF+2]       ; past that fails - every time, as the
    jnz .p2                         ; reader asked again (asked before it
    cmp ax, [vb_pn]                 ; is rounded: 61,441 and up laps)
    jae .p2
    add ax, VB_PIECE - 1
    and ax, 0 - VB_PIECE
    mov [vb_pn], ax
.p2:
    VB_STRM ds:bp, si               ; A LOOP READ ROUND, at its lap's start
    jz .p3                          ; for the first time: its cursor kept,
    cmp byte [ds:bp+RG_LCSET], 0    ; to go round with and no seek
    jne .p3
    mov ax, [ds:bp+RG_FOFF]
    cmp ax, [si+VC_LAP]
    jne .p3
    mov ax, [ds:bp+RG_FOFF+2]
    cmp ax, [si+VC_LAP+2]
    jne .p3
    push si
    push di
    lea si, [ds:bp+RG_CUR]
    lea di, [ds:bp+RG_LCUR]
    call vb_ccopy
    pop di
    pop si
    mov byte [ds:bp+RG_LCSET], 1
.p3:
    mov ax, [ds:bp+RG_FOFF]         ; the file offset
    mov dx, [ds:bp+RG_FOFF+2]
    add ax, [si+VC_OFF]
    adc dx, [si+VC_OFF+2]
    mov es, [ds:bp+RG_FSEG]
    xor bx, bx
    mov cx, [vb_pn]
    lea di, [ds:bp+RG_CUR]          ; its own cursor: a clip's reads follow
    mov [vb_rcur], di               ; on, with no walk of the chain
    call vb_read
    jc .out
    mov si, vb_tpre
    call vb_tmark
    mov ax, [vb_pn]                 ; the fill's place on: the piece ends
    mov cl, 4                       ; at the ring's end at the most
    shr ax, cl
    add ax, [ds:bp+RG_FSEG]
    cmp ax, [ds:bp+RG_END]
    jb .w
    mov ax, [ds:bp+RG_SEG]
.w:
    mov [ds:bp+RG_FSEG], ax
.parse:                             ; THE RECORDS NOW WHOLE, walked: one
    mov ax, [ds:bp+RG_FILL]         ; that runs past the ring's end has its
    mov dx, [ds:bp+RG_FILL+2]       ; tail copied past it, into the guard,
    add ax, [vb_pn]                 ; and nothing else is (125.4)
    adc dx, 0
    sub ax, [ds:bp+RG_POFF]         ; what is read of the record there
    sbb dx, [ds:bp+RG_POFF+2]
    jz .a1
    mov ax, 0xFFFF
.a1:
    mov cx, ax
    cmp cx, RH_SIZE
    jb .pstop
    mov es, [ds:bp+RG_PSEG]         ; its length, its second byte at the
    mov si, [ds:bp+RG_POFS]         ; ring's start when the first is the
    mov al, [es:si+RH_LEN]          ; ring's last
    mov dx, [ds:bp+RG_END]
    dec dx
    cmp dx, [ds:bp+RG_PSEG]
    jne .h0
    cmp si, 15
    jne .h0
    mov es, [ds:bp+RG_SEG]
    mov ah, [es:0]
    jmp short .h1
.h0:
    mov ah, [es:si+RH_LEN+1]
.h1:
    cmp ax, RH_SIZE                 ; (a length that cannot be: left for
    jb .pstop                       ; the hook to refuse)
    cmp cx, ax
    jb .pstop                       ; not all of it read yet
    mov dx, [ds:bp+RG_END]          ; past the end? (A record is the guard
    sub dx, [ds:bp+RG_PSEG]         ; at most: farther off than that, no)
    cmp dx, 0x802
    jae .pin
    mov cl, 4
    shl dx, cl                      ; the bytes from its segment to the end
    mov cx, si
    add cx, ax
    sub cx, dx                      ; ...its tail past it
    jbe .pin
    call vb_gcopy
.pin:
    add [ds:bp+RG_POFF], ax
    adc word [ds:bp+RG_POFF+2], 0
    mov cx, ax
    mov bx, bp
    mov di, RG_PSEG
    call vb_cadv
    jmp .parse
.pstop:
    mov dx, [ds:bp+RG_END]          ; a header that runs past the end: its
    sub dx, [ds:bp+RG_PSEG]         ; bytes there now, so the hook reads
    cmp dx, 1                       ; the right length (and waits)
    jne .pdone
    cmp word [ds:bp+RG_POFS], 16 - RH_SIZE
    jbe .pdone
    mov cx, RH_SIZE
    call vb_gcopy
.pdone:
    mov ax, [vb_pn]                 ; the next read's place: on, or for a
    add [ds:bp+RG_FOFF], ax         ; loop read round at its lap's end back
    adc word [ds:bp+RG_FOFF+2], 0   ; to the lap, its cursor with it
    mov al, [ds:bp+RG_CLIP]
    xor ah, ah
    mov bx, ax
    call vb_crec
    VB_STRM ds:bp, si
    jz .pf
    mov ax, [ds:bp+RG_FOFF]
    mov dx, [ds:bp+RG_FOFF+2]
    cmp dx, [si+VC_LEN+2]
    jb .pf
    ja .pw
    cmp ax, [si+VC_LEN]
    jb .pf
.pw:
    mov ax, [si+VC_LAP]
    mov [ds:bp+RG_FOFF], ax
    mov ax, [si+VC_LAP+2]
    mov [ds:bp+RG_FOFF+2], ax
    push di
    lea si, [ds:bp+RG_LCUR]
    lea di, [ds:bp+RG_CUR]
    call vb_ccopy
    pop di
.pf:
    pushf                           ; the fill on: published last, and whole
    cli                             ; (the hook reads it)
    mov ax, [vb_pn]
    add [ds:bp+RG_FILL], ax
    adc word [ds:bp+RG_FILL+2], 0
    popf
    mov si, vb_tcp
    call vb_tmark
    mov al, 1
    clc
    jmp short .out
.none:
    xor al, al
    clc
.out:
    mov bx, bp
    pop bp
    pop di
    pop si
    ret

; vb_ccopy - a file cursor (FSEQ_SIZE bytes) DS:SI to DS:DI. AX, CX spent
vb_ccopy:
    push es
    push ds
    pop es
    mov cx, FSEQ_SIZE / 2
    cld
    rep movsw
    pop es
    ret

; vb_gcopy - CX bytes from the start of ring DS:BP to past its end: a
; record's tail into the guard (vb_rpiece). AX, BX, DX kept
vb_gcopy:
    push si
    push di
    push ds
    push es
    add [vb_ncopy], cx              ; (a diagnostic: bytes the reader copies)
    adc word [vb_ncopy+2], 0
    mov es, [ds:bp+RG_END]
    mov ds, [ds:bp+RG_SEG]
    xor si, si
    xor di, di
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    pop es
    pop ds
    pop di
    pop si
    ret

; vb_psize - AX = the room (a multiple of VB_PIECE) cut to the piece the
; moment allows (125.4): VB_PIECE while a menu waits on a key, else up to
; VB_SPIECE - a read costs the kernel a call's overhead whatever it moves,
; and four 4 KB reads took 80 ms each where one of 16 KB is far less
vb_psize:
    cmp byte [vb_state], GS_MENU
    je .small
    cmp byte [vb_state], GS_TARGET
    jne .big
.small:
    mov ax, VB_PIECE
    ret
.big:
    cmp ax, VB_SPIECE
    jbe .ok
    mov ax, VB_SPIECE
.ok:
    ret

; vb_ringfor - ring BX for clip AL: the one holding it, or else one given
; to it - a ring the hook is not playing from, and not holding the action
; pending or clip DL (the reader's next most wanted). CF=1 none free
vb_ringfor:
    mov bx, vb_rg0
    cmp al, [bx+RG_CLIP]
    je .have
    mov bx, vb_rg1
    cmp al, [bx+RG_CLIP]
    je .have
    mov bx, vb_rg0
    call .free
    jnc .give
    mov bx, vb_rg1
    call .free
    jc .out
.give:
    call vb_rreset
.have:
    clc
.out:
    ret
.free:                              ; CF=1 ring BX is not to be given away
    cmp byte [bx+RG_PLAY], 0
    jne .busy
    mov ah, [bx+RG_CLIP]
    cmp ah, 0xFF
    je .ok
    cmp ah, [vb_pend]
    je .busy
    cmp ah, dl
    je .busy
.ok:
    clc
    ret
.busy:
    stc
    ret

; vb_mpiece - one 4 KB piece of the music into its ring, when the ring has
; room for it (125.4). The file is read at the music's own offset
; [vb_mrd], always a cluster multiple; the piece lands at the ring's fill
; position, and what it puts past the ring's end (into the guard) is copied
; to its start. The last piece of the music carries less than 4 KB of it, and
; the music goes on from its first byte: it loops. Out AL = 1 a piece was
; read, 0 the ring is full; CF=1 a read failed. AX..DX, ES spent
; vb_mtopup - the music's ring read FULL (piece after piece, a read that
; fails ending it), ahead of a long stretch the reader cannot run in
vb_mtopup:
    mov cx, VB_MR / VB_MPIECE + 1
.r:
    push cx
    call vb_mpiece
    pop cx
    jc .out
    or al, al
    jz .out
    loop .r
.out:
    ret

vb_mpiece:
    cmp byte [vb_mres], 0           ; (resident music is never read again)
    je .stream
    xor al, al
    clc
    ret
.stream:
    mov ax, [vb_mfill]              ; what the ring holds (16 bits: it is
    sub ax, [vb_mtake]              ; never more than VB_MR)
    cmp ax, VB_MR - VB_MPIECE
    jbe .room
    xor al, al
    clc
    ret
.room:
    push si
    push di
    neg ax                          ; the room, in whole pieces, cut to the
    add ax, VB_MR                   ; moment's piece and to the ring's wrap
    and ax, 0 - VB_MPIECE           ; (the guard takes one piece past it)
    call vb_psize
    mov bx, [vb_mfill]
    and bx, VB_MR - 1
    mov cx, VB_MR + VB_MPIECE
    sub cx, bx
    and cx, 0 - VB_MPIECE
    cmp ax, cx
    jbe .rs
    mov ax, cx
.rs:
    mov [vb_mpn], ax
    mov ax, [vb_mrd]                ; past the music's end: no more than it
    mov dx, [vb_meta+VH_MLEN]       ; (and the next piece reads its top)
    mov cx, [vb_meta+VH_MLEN+2]
    sub dx, ax
    sbb cx, [vb_mrd+2]
    jnz .rl
    cmp dx, [vb_mpn]                ; (asked first: rounding 61,441 and up
    jae .rl                         ; laps 16 bits to a piece of nothing)
    add dx, VB_MPIECE - 1
    and dx, 0 - VB_MPIECE
    mov [vb_mpn], dx
.rl:
    mov ax, [vb_mrd]
    mov dx, [vb_mrd+2]
    add ax, [vb_meta+VH_MUSIC]
    adc dx, [vb_meta+VH_MUSIC+2]
    mov es, [vb_mring]
    push bx
    mov cx, [vb_mpn]
    mov word [vb_rcur], vb_curm     ; its own cursor
    call vb_read
    pop bx
    jc .out
    mov ax, [vb_meta+VH_MLEN]       ; what of it is music: the rest of it,
    mov dx, [vb_meta+VH_MLEN+2]     ; at most the piece
    sub ax, [vb_mrd]
    sbb dx, [vb_mrd+2]
    or dx, dx
    jnz .four
    cmp ax, [vb_mpn]
    jbe .v
.four:
    mov ax, [vb_mpn]
.v:
    mov cx, bx                      ; past the ring's end: to its start
    add cx, ax
    sub cx, VB_MR
    jbe .in
    push ds
    mov dx, [vb_mring]
    mov es, dx
    mov ds, dx
    mov si, VB_MR
    xor di, di
    cld
    add [cs:vb_ncopy], cx           ; (a diagnostic: bytes the reader copies)
    adc word [cs:vb_ncopy+2], 0
    rep movsb
    pop ds
.in:
    add [vb_mrd], ax
    adc word [vb_mrd+2], 0
    mov dx, [vb_mrd]                ; the music's end: from the top
    cmp dx, [vb_meta+VH_MLEN]
    jne .pub
    mov dx, [vb_mrd+2]
    cmp dx, [vb_meta+VH_MLEN+2]
    jne .pub
    mov word [vb_mrd], 0
    mov word [vb_mrd+2], 0
.pub:
    add [vb_mfill], ax              ; published last: the hook reads it
    mov al, 1
    clc
.out:
    pop di
    pop si
    ret

; vb_fontload - the kernel's 8x8 glyphs into our bss, ' '..'~' (the
; desktop's drawing slots are not ours in the bracket, 53.7)
vb_fontload:
    push ds
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL = first, AH = last
    mov [vb_ffirst], al
    mov [vb_flast], ah
    mov es, dx
    mov di, vb_font
    mov bx, 32
.ch:
    mov cx, 8
    cmp bl, [vb_ffirst]
    jb .blank
    cmp bl, [vb_flast]
    ja .blank
    push si
    mov ax, bx
    sub al, [vb_ffirst]
    mov cl, 3
    shl ax, cl
    add si, ax
    mov cx, 8
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    pop si
    jmp short .next
.blank:
    mov byte [di], 0
    inc di
    loop .blank
.next:
    inc bx
    cmp bx, 127
    jb .ch
    pop ds
    ret

; =============================================================================
; the bracket's foreground (vb_main) - SI = window, DS = ours, ES = kernel
; =============================================================================
vb_main:
    mov word [vb_err], 0
    mov bx, [vb_win]
    call OSAPI_FSX_CAPS             ; AX = modes, DL = VID_*
    mov [vb_vid], dl
    mov cl, [vb_meta+VH_KIND]       ; the mode is the bundle's adapter's
    mov [vb_kind], cl
    mov ch, FSXM_VGA0D
    cmp cl, VK_VGA4
    je .mk
    mov ch, FSXM_HERC
    cmp cl, VK_HERC
    je .mk
    mov ch, FSXM_CGA320
.mk:
    mov dx, ax
    mov cl, ch
    mov ax, 1
    shl ax, cl
    test dx, ax
    jnz .mode
    mov word [vb_err], vb_s_nocga
    cmp byte [vb_kind], VK_HERC
    jne .mx
    mov word [vb_err], vb_s_noherc
.mx:
    ret
.mode:
    mov al, ch
    push ds
    pop es
    mov di, vb_fsi
    call OSAPI_FSX_MODE
    jnc .set
    mov word [vb_err], vb_s_nocga
    ret
.set:
    mov ax, [vb_fsi+FSI_SEG]
    mov [vb_vseg], ax
    call vb_palette
    cmp byte [vb_kind], VK_CGA4     ; the band's present, for this adapter
    je .bt
    call vb_bptab
.bt:
    call vb_rclaim                  ; the clip rings, all there is (125.4)
    jc .out
    call vb_rstart                  ; ...and the first opening's head
    jc .rout
    call vb_game_init
    call vb_play_init
    call vb_snd_open
    mov byte [vb_ready], 1          ; the hook is live from here
.loop:
    call vb_keys
    cmp byte [vb_quit], 0
    jne .leave
    call vb_game
    call vb_fadetick                ; a fade's next step, and what follows it
    cmp byte [vb_awant], 0          ; the play again with no fade: a
    je .naw                         ; once-clip joined the title (125.3.1)
    mov byte [vb_awant], 0
    call vb_again
.naw:
    mov si, vb_tg
    call vb_tmark
    mov ax, [vb_rcalls]
    push ax
    call vb_reader
    pop ax
    cmp ax, [vb_rcalls]
    jne .rd
    inc word [vb_nidle]             ; (a pass that read nothing)
.rd:
    mov si, vb_tr
    call vb_tmark
    call vb_band
    call vb_snd_keep
    mov si, vb_tb
    call vb_tmark
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    mov si, vb_tw
    call vb_tmark
    jmp .loop
.leave:
    mov byte [vb_ready], 0
    call vb_snd_close
.rout:
    call vb_rfree
.out:
    ret

; vb_rstart - the rings empty, and the first opening's head read into ring
; 0 before the play starts. CF=1 and [vb_err] when a read fails
vb_rstart:
    mov al, [vb_meta+VH_TITLE]      ; a TITLE plays first, out of ring 1 -
    mov bx, vb_rg1                  ; opening 0 is read into ring 0 while it
    cmp al, 0xFF                    ; goes round (125.3.1)
    jne .r
    cmp word [vb_meta+VH_NOPEN], 0
    je .ok
    mov al, [vb_meta+VH_OPENS]
    mov bx, vb_rg0
.r:
    mov [vb_rsring], bx
    call vb_rreset
.head:
    mov bx, [vb_rsring]
    call vb_rhead                   ; CF=1: its head is in
    jc .ok
    mov bx, [vb_rsring]
    call vb_rpiece
    jc .bad
    or al, al
    jnz .head
.ok:
    clc
    ret
.bad:
    mov word [vb_err], vb_s_badread
    stc
    ret

; vb_palette - mode 4's palette from the bundle's byte (Video Player's
; vp_cgaset .c4, 98.1.3.3): background and intensity, the set, and mode 5's
; cyan, red and white
vb_palette:
    cmp byte [vb_kind], VK_HERC     ; (one bit: no palette)
    jne .v
    ret
.v:
    cmp byte [vb_kind], VK_VGA4
    jne .cga
    push ds                         ; VGA4's own sixteen: the attribute
    pop es                          ; controller's sixteen to DAC 0..15 in
    mov dx, vb_actab                ; order, and those sixteen the bundle's
    mov ax, 0x1002
    int 0x10
    push ds
    pop es
    mov dx, vb_meta+VH_VPAL
    xor bx, bx
    mov cx, 16
    mov ax, 0x1012
    int 0x10
    ret
.cga:
    mov ah, 0x0B
    xor bh, bh
    mov bl, [vb_meta+VH_PAL]
    and bl, 0x1F
    int 0x10
    mov ah, 0x0B
    mov bh, 1
    mov bl, [vb_meta+VH_PAL]
    mov cl, 5
    shr bl, cl
    or bl, bl
    jz .s
    mov bl, 1
.s:
    int 0x10
    test byte [vb_meta+VH_PAL], 0x40
    jz .out
    cmp byte [vb_vid], VID_CGA
    jne .ev
    mov dx, 0x3D8
    mov al, 0x0E
    out dx, al
    ret
.ev:
    mov ax, 0x1000
    mov bx, 0x0402
    test byte [vb_meta+VH_PAL], 0x10
    jz .e1
    mov bh, 0x14
.e1:
    int 0x10
.out:
    ret

; vb_keys - Esc leaves; the arrows and Enter are the menu's
vb_keys:
    mov ah, 1
    int 0x16
    jz .none
    xor ah, ah
    int 0x16
    cmp al, 27                      ; Esc: back out of a sub-menu, the
    jne .k                          ; side menu or a target; otherwise leave
    cmp byte [vb_state], GS_TARGET
    je .k
    cmp byte [vb_state], GS_MENU
    jne .quit
    cmp byte [vb_pmenu], NONE
    jne .k
.quit:
    mov byte [vb_quit], 1
    ret
.k:
    mov [vb_key], ax
    push ax
    call OSAPI_GET_TICKS            ; when: the reader waits a moment on it
    mov [vb_ktick], ax
    pop ax
.none:
    ret

; =============================================================================
; the clips, from the hook's side (SPEC.md 125.3)
; =============================================================================

; vb_rrec - BX = a ring; out: CF=0 and DX:SI = the record at its VIDEO
; cursor, whole in memory (DX a segment - or, in place on XMS, VB_XH: a
; handle VB_DSDX borrows, vbinp.inc), CX = its length; CF=1 not read that
; far yet. AX spent, and ES
vb_rrec:
    push di
    mov di, RG_VOFF                 ; what is read past the cursor
    call vb_held
    pop di
    cmp dx, RH_SIZE
    jb .no
    cmp byte [bx+RG_INP], 0
    jne .inp
    mov es, [bx+RG_VSEG]
    mov si, [bx+RG_VOFS]
    mov cx, [es:si+RH_LEN]
    cmp cx, dx
    ja .no
    mov dx, es
    clc
    ret
.inp:                               ; IN PLACE: all of it is there
    mov ax, [bx+RG_VOFF]
    mov dx, [bx+RG_VOFF+2]
    call vb_iat
    call vb_ilen
    clc
    ret
.no:
    stc
    ret

; vb_radv - BX = a ring: its video cursor past CX bytes (a record). AX, CX,
; DX spent
vb_radv:
    add [bx+RG_VOFF], cx
    adc word [bx+RG_VOFF+2], 0
    push di
    mov di, RG_VSEG
    call vb_cadv
    pop di
    ret

; vb_start_clip - the hook plays clip AL out of ring BX from the record at
; its cursor (an opening's first, or an action's second: the entry drew its
; first frame). AL and BX kept. Called from the hook, or before
; [vb_ready], or from the UI task with interrupts off
vb_start_clip:
    push ax                         ; (AL kept: callers book its sound by it)
    mov [vb_cc], al
    mov [vb_cring], bx
    mov byte [bx+RG_PLAY], 1
    push bx
    xor ah, ah
    mov bx, ax
    call vb_crec
    pop bx
    mov ax, [si+VC_NREC]
    mov [vb_cleft], ax
    mov ax, [si+VC_FLAGS]
    mov [vb_cflags], ax
    mov ax, [si+VC_EXIT]
    mov [vb_cexit], ax
    test word [vb_cflags], CF_LOOP
    jz .nl
    VB_STRM bx, si                  ; held in its ring, or read round
    mov byte [vb_lstream], 0
    jz .ls
    mov byte [vb_lstream], 1
.ls:
    mov ax, [si+VC_NREC]            ; A LOOP: its lap is records 1..n, from
    dec ax                          ; record 0's end in its ring (it fits
    mov [vb_lnrec], ax              ; whole, from the ring's start)
    mov [vb_lring], bx
    cmp byte [bx+RG_INP], 0
    jne .lin
    push es
    mov es, [bx+RG_SEG]
    mov ax, [es:RH_LEN]
    pop es
    jmp short .lb
.lin:                               ; (in place: record 0's own length)
    xor ax, ax
    xor dx, dx
    call vb_iat
    call vb_ilen
    mov ax, cx
.lb:
    mov [vb_lbase], ax
.nl:
    mov byte [vb_mode], VM_CLIP
    pop ax
    ret

; vb_lrewind - the loop in ring BX back to its lap's start: the picture's
; cursor at record 1, its frame 0 on the glass (the seam drew it). The hook's
vb_lrewind:
    cmp byte [vb_lstream], 0        ; (READ ROUND: the cursor goes on, the
    jne .on                         ; ring holds the next lap after this)
    push di
    mov di, RG_VOFF
    call vb_lcur
    pop di
.on:
    mov ax, [vb_lnrec]
    mov [vb_cleft], ax
    mov word [vb_cfr], 1
    ret

; vb_lcur - ring BX's cursor at [bx+di] (a dword, then its segment and
; offset) put at record 1 of the loop it holds. AX, CX, DX spent
vb_lcur:
    mov ax, [vb_lbase]
    mov [bx+di], ax
    mov word [bx+di+2], 0
    mov dx, ax
    and ax, 15
    mov [bx+di+6], ax
    mov cl, 4
    shr dx, cl
    add dx, [bx+RG_SEG]
    mov [bx+di+4], dx
    ret

; vb_hook - FSXF_RATE's hook (53.2.2): AX = periods since the last call,
; DS = ours, IF = 0, at most 48 bytes of stack, no kernel calls
vb_hook:
    cld
    cmp byte [vb_ready], 0
    jne .live
    ret
.live:
    add [vb_hper], ax               ; (the periods, counted: 125.7's account
    adc word [vb_hper+2], 0         ; of where the reader's time goes; and
    mov dx, ax                      ; the ones that passed while the hook
    dec dx                          ; ran last, near enough)
    add [vb_hbusy], dx
    adc word [vb_hbusy+2], 0
    cmp byte [vb_snd], 0
    je .pit
    ; --- the card's clock: the bytes it has consumed, plus the periods
    ; since its last block, capped below a block (98.3.1's vp_adue)
    mov es, [vb_ring]
    mov bx, [es:VB_RL+SND_EXT_CONS]
    mov dx, bx
    sub bx, [vb_cons]
    jz .same
    mov [vb_cons], dx
    add [vb_cons32], bx
    adc word [vb_cons32+2], 0
    mov word [vb_pers], 0
    jmp short .est
.same:
    add [vb_pers], ax
.est:
    mov ax, [vb_pers]
    cmp ax, [vb_pcap]               ; less than a block's periods: 100 for
    jbe .m                          ; 2,048 bytes, 26 for 512
    mov ax, [vb_pcap]
.m:
    mov cx, 19
    mul cx
    add ax, [vb_cons32]
    mov dx, [vb_cons32+2]
    adc dx, 0
    cmp dx, [vb_clock+2]            ; never backwards
    jb .frames
    ja .newc
    cmp ax, [vb_clock]
    jbe .frames
.newc:
    mov [vb_clock], ax
    mov [vb_clock+2], dx
    jmp short .frames
.pit:
    mov cx, ax                      ; no card: the PIT's periods
.p1:
    add word [vb_frac], VB_SPP_LO
    adc word [vb_clock], VB_SPP_HI
    adc word [vb_clock+2], 0
    loop .p1
.frames:
    mov byte [vb_hn], 2             ; at most two frames a call: behind, the
.f:                                 ; picture catches up on the sound
    mov ax, [vb_clock]
    mov dx, [vb_clock+2]
    sub ax, [vb_due]
    sbb dx, [vb_due+2]
    jb .fed                         ; not due yet
    or dx, dx                       ; more than a second behind: forgiven,
    jnz .forgive                    ; and the picture runs late from here
    cmp ax, VB_FORGIVE
    jb .one
.forgive:
    inc word [vb_late]
    mov ax, [vb_clock]
    mov dx, [vb_clock+2]
    mov [vb_due], ax
    mov [vb_due+2], dx
.one:
    sti                             ; a disk's or the card's interrupt is not
    call vb_frame                   ; held behind a frame (53.2.2)
    cli
    jc .stall                       ; a clip not read that far: wait
    mov ax, [vb_meta+VH_SPF]
    add [vb_due], ax
    adc word [vb_due+2], 0
    dec byte [vb_hn]
    jnz .f
    jmp short .fed
.stall:
    inc word [vb_stall]
.fed:
    call vb_feed
    ret

; vb_frame - draw the next frame of whatever is playing. CF=1: the clip's
; next record is not in memory yet (nothing drawn)
vb_frame:
    mov al, [vb_mode]
    cmp al, VM_CLIP
    je .clip
    cmp al, VM_IDLE
    jne .none
    jmp .idle
.none:
    cmp al, VM_FADE                 ; A FADE: the UI task draws it, a step a
    jne .nh                         ; frame of this clock
    inc word [vb_fticks]
    clc
    ret
.nh:
    cmp al, VM_HOLD                 ; held: an end loop to start?
    jne .nn
    cmp byte [vb_estart], 0
    je .nn
    call vb_eloop_go
.nn:
    clc
    ret
.clip:
    test word [vb_cflags], CF_LOOP  ; THE TITLE, left by a clip: entered at
    jz .c0                          ; the anchor chosen, in place of the
    cmp byte [vb_lwant], 0          ; frame after it
    je .c0
    cmp byte [vb_tplay], 0
    je .c0
    cmp word [vb_lk], 0xFFFF
    jne .lk
    call vb_lanchor
    jmp short .c0
.lk:
    mov ax, [vb_cfr]
    dec ax
    cmp ax, [vb_lk]
    jne .c0
    jmp vb_leave_go
.c0:
    mov bx, [vb_cring]
    call vb_rrec
    jc .wait
    push cx
    call vb_decode
    pop cx
    mov bx, [vb_cring]
    call vb_radv
    inc word [vb_cfr]
    test word [vb_cflags], CF_OPENING
    jz .cnt
    cmp word [vb_cfr], 1            ; an opening WITH THE MENU (not CF_FULL)
    jne .cnt                        ; has its first frame up, or the LAST
    test word [vb_cflags], CF_FULL  ; opening has: the band may be drawn
    jz .bok                         ; under it now (125.3: a full-screen
    mov al, [vb_cc]                 ; opening owns the band's rows, and
    cmp al, [vb_olast]              ; every opening was one before format
    jne .cnt                        ; 7's per-opening choice)
.bok:
    mov byte [vb_bandok], 1
.cnt:
    dec word [vb_cleft]
    jz .over
    clc
    ret
.over:
    test word [vb_cflags], CF_LOOP  ; a loop's lap is over: round again
    jz .ov
    mov bx, [vb_cring]
    call vb_lrewind
    clc
    ret
.ov:
    mov al, [vb_cc]                 ; the clip is over
    mov [vb_cdone], al
    mov bx, [vb_cring]
    mov byte [bx+RG_VDONE], 1       ; (its ring is free once its sound is)
    test word [vb_cflags], CF_JOINS
    jz .next
    mov ax, [vb_cexit]              ; it seamed onto idle frame EXIT
    mov [vb_ifr], ax
    mov byte [vb_mode], VM_IDLE
    clc
    ret
.next:
    test word [vb_cflags], CF_OPENING
    jz .hold
    mov al, [vb_onext]              ; AN OPENING: the next one follows on the
    cmp al, 0xFF                    ; very next frame, from its own ring
    je .hold
    mov bx, [vb_oring]
    call vb_start_clip
    mov word [vb_cfr], 0
    call vb_onextset                ; ...and the one after it, if any
    clc
    ret
.hold:
    mov byte [vb_mode], VM_HOLD
    test word [vb_cflags], CF_ENDS  ; AN ENDING: how it goes on (125.3.1)
    jz .hx
    mov al, [vb_ehow]
    cmp al, EH_JOIN                 ; it seamed onto its end loop's frame 0
    jne .h1
    mov byte [vb_estart], 2
    jmp short .hx
.h1:
    cmp al, EH_FADE
    jne .h2
    mov bl, [vb_eloop]              ; the canvas fades, then its end clip -
    xor bh, bh                      ; or the WHOLE SCREEN, before one that
    call vb_crec                    ; is full screen (125.3.1)
    mov byte [vb_fwant], 2
    test word [si+VC_FLAGS], CF_FULL
    jz .hx
    mov byte [vb_fwant], 4
    jmp short .hx
.h2:
    cmp al, EH_AGAIN
    jne .h3
    mov byte [vb_fwant], 3          ; the screen fades, and the play again
    jmp short .hx
.h3:
    cmp al, EH_AGAINJ               ; a once-clip that JOINED the title: the
    jne .hx                         ; play again at once, over its seam (the
    mov byte [vb_awant], 1          ; UI task's)
.hx:
    clc
    ret
.wait:
    stc
    ret
.idle:
    ; an action waiting: its anchor is chosen once its head is read, and
    ; the entry record is drawn in place of the frame after the anchor
    mov bl, [vb_pend]
    cmp bl, 0xFF
    je .loop
    cmp word [vb_pk], 0xFFFF
    jne .anchored
    call vb_anchor
    jmp short .loop
.anchored:
    mov ax, [vb_ifr]
    cmp ax, [vb_pk]
    jne .loop
    mov dx, [vb_eseg]               ; the entry: frame 0's picture, at
    add dx, [vb_pentry]             ; its paragraph in the entries
    xor si, si
    call vb_decode
    mov bx, [vb_pring]              ; frame 0's record carries its sound
    call vb_rrec                    ; alone: past it
    call vb_radv
    mov al, [vb_pend]
    mov bx, [vb_pring]
    call vb_start_clip
    dec word [vb_cleft]
    mov word [vb_cfr], 1
    mov byte [vb_pend], 0xFF
    mov word [vb_pk], 0xFFFF
    clc
    ret
.loop:
    mov ax, [vb_ifr]                ; the loop's next frame
    inc ax
    cmp ax, [vb_meta+VH_NIDLE]
    jb .i1
    xor ax, ax
.i1:
    mov [vb_ifr], ax
    mov si, ax
    shl si, 1
    add si, [vb_meta+VH_IDLETAB]
    mov es, [vb_vres]
    mov dx, [es:si]                 ; its record's paragraph (format 7)
    add dx, [vb_idseg]
    xor si, si
    call vb_decode
    clc
    ret

; vb_onextset - [vb_onext] and [vb_oring]: the opening after the one now
; playing, and the ring it is read into (the one not playing). Called from
; the hook as an opening starts, and before the bracket
vb_onextset:
    mov byte [vb_onext], 0xFF
    mov al, [vb_oidx]
    inc al
    mov [vb_oidx], al
    xor ah, ah
    cmp ax, [vb_meta+VH_NOPEN]
    jae .out
    call vb_oring_of
    mov [vb_onext], al
    mov [vb_oring], bx
.out:
    ret

; vb_oring_of - opening AX (its place in turn): AL = its clip, BX = its ring
; - ring 0 for the first, third..., ring 1 for the second... so that one
; plays while the next is read
vb_oring_of:
    mov bx, ax
    mov al, [vb_meta+VH_OPENS+bx]
    test bl, 1
    mov bx, vb_rg0
    jz .r
    mov bx, vb_rg1
.r:
    ret

; vb_anchor - clip [vb_pend] waits for its head in a ring, then takes the
; next of ITS anchors far enough on that its sound starts ahead of the
; ring's write position: the lead's frames and one more (125.3)
vb_anchor:
    xor bh, bh
    mov bl, [vb_pend]
    call vb_crec
    mov bx, vb_rg0                  ; its ring
    mov al, [vb_pend]
    cmp al, [bx+RG_CLIP]
    je .ring
    mov bx, vb_rg1
    cmp al, [bx+RG_CLIP]
    je .ring
    ret                             ; not given one yet
.ring:
    cmp byte [bx+RG_PLAY], 0        ; (a ring still sounding: not yet)
    je .free
    ret
.free:
    mov cx, VB_HEAD                 ; its head, or all of it
    cmp word [si+VC_LEN+2], 0
    jne .h
    cmp cx, [si+VC_LEN]
    jbe .h
    mov cx, [si+VC_LEN]
.h:
    cmp word [bx+RG_FILL+2], 0
    jne .read
    cmp [bx+RG_FILL], cx
    jae .read
    ret                             ; not read that far yet
.read:
    mov [vb_pring], bx
    ; the nearest anchor at least [vb_dmin] frames on: d = (k - ifr) mod N
    mov word [vb_bestd], 0xFFFF
    mov di, [si+VC_ANCH]            ; (k, entry's paragraph) pairs in the
                                    ; resident block
    mov cx, [si+VC_NANCH]
    jcxz .out
    mov es, [vb_vres]
.a:
    mov ax, [es:di]
    sub ax, [vb_ifr]
    jae .pos
    add ax, [vb_meta+VH_NIDLE]
.pos:
    cmp ax, [vb_dmin]
    jae .d
    add ax, [vb_meta+VH_NIDLE]
.d:
    cmp ax, [vb_bestd]
    jae .an
    mov [vb_bestd], ax
    mov dx, [es:di]
    mov [vb_pk], dx
    mov dx, [es:di+2]
    mov [vb_pentry], dx
.an:
    add di, 4
    loop .a
    cmp word [vb_bestd], 0xFFFF
    je .out
    mov byte [bx+RG_PLAY], 1        ; the reader streams it from here
    ; its sound starts with frame 0, due at [vb_due] + d * spf
    mov ax, [vb_bestd]
    mul word [vb_meta+VH_SPF]
    add ax, [vb_due]
    adc dx, [vb_due+2]
    mov [vb_apat], ax
    mov [vb_apat+2], dx
    mov [vb_apring], bx
    mov al, [vb_pend]
    mov [vb_apclip], al
.out:
    ret

; vb_decode - one record at DX:SI (its header first) onto the screen
vb_decode:
    push ds
    mov es, [vb_vseg]
    VB_DSDX                         ; (DX a handle in place on XMS: a borrow)
    add si, RH_SIZE
    xor bp, bp
    cmp byte [cs:vb_kind], VK_VGA4  ; (DS is the record's: CS is ours)
    je .planar
    cmp byte [cs:vb_kind], VK_HERC
    jne .n
    mov bp, VB_HORG
.n:
    call vd_native
    VB_DSEND
    pop ds
    ret
.planar:                            ; VGA4: sub-records, each its Map Mask
    lodsb                           ; and ten lists, a 0 after the last
    or al, al
    jz .pd
    mov ah, al
    mov al, 2
    mov dx, 0x3C4
    out dx, ax
    xor bp, bp
    call vd_native
    jmp short .planar
.pd:
    mov ax, 0x0F02                  ; every plane again
    mov dx, 0x3C4
    out dx, ax
    VB_DSEND
    pop ds
    ret

; =============================================================================
; the sound, from the hook's side: the card's ring topped up out of the
; music's ring and the clip's own sound (SPEC.md 125.3)
; =============================================================================

; vb_feed - the card's ring topped up to [vb_lead] ahead of what it consumed
vb_feed:
    cmp byte [vb_snd], 0
    jne .ui
    ret
.ui:
    mov al, [vb_ui]                 ; a menu sound asked for
    or al, al
    jz .on
    mov byte [vb_ui], 0
    call vb_uisound
.on:
    mov ax, [vb_wpos]               ; ahead = wpos - cons32 (< 64 KB)
    sub ax, [vb_cons32]
    cmp ax, [vb_lead]
    jb .room
    ret
.room:
    mov cx, [vb_lead]
    sub cx, ax
    cmp cx, 32
    jb .done
    cmp cx, 512                     ; a call's share
    jbe .c1
    mov cx, 512
.c1:
    call vb_chunk
    jmp .on
.done:
    ret

; vb_uisound - menu sound AL - 1 (resident), NOW: it replaces the menu sound
; playing and is mixed straight into what the ring holds from a block past
; the card's last report up to the write position, so it is heard about a
; block from now rather than the ring's whole lead (125.3). The rest of it
; is mixed as the ring is written
vb_uisound:
    dec al
    xor ah, ah
    mov bx, ax                      ; its row: 6 bytes
    shl bx, 1
    add bx, ax
    shl bx, 1
    mov ax, [vb_meta+VH_SFXTAB+bx]
    mov [vb_uoff], ax
    mov ax, [vb_meta+VH_SFXTAB+4+bx]
    mov [vb_ulen], ax
    mov di, [vb_cons32]             ; from a block past the card's report
    add di, [vb_blk]
    mov cx, [vb_wpos]
    sub cx, di
    jbe .out                        ; (nothing queued past it)
    cmp cx, VB_LEAD
    ja .out
.piece:
    jcxz .out
    push cx
    and di, VB_RL - 1               ; to the ring's wrap
    mov ax, VB_RL
    sub ax, di
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    mov ax, [vb_ulen]
    cmp cx, ax
    jbe .n2
    mov cx, ax
.n2:
    jcxz .done
    sub [vb_ulen], cx
    mov si, [vb_uoff]
    add [vb_uoff], cx
    mov dx, cx
    mov es, [vb_ring]
    push ds
    mov ds, [vb_sfxseg]
.mix:
    lodsb
    add [es:di], al
    inc di
    loop .mix
    pop ds
    pop cx
    sub cx, dx
    jmp .piece
.done:
    pop cx
.out:
    ret

; vb_chunk - CX samples (> 0) at the write position: the music (or, under a
; clip's OWN sound, nothing of it), the clip's sound over it, the menu's
; sound over that. Cut to the card ring's wrap, the music ring's, the next
; frame of the clip's sound, and the start of a clip's sound to come
vb_chunk:
    mov di, [vb_wpos]
    and di, VB_RL - 1
    mov ax, VB_RL
    sub ax, di
    cmp cx, ax
    jbe .w
    mov cx, ax
.w:
    ; a clip's sound to come: the chunk stops at its first sample, and it
    ; starts there (or at once, if the write position is past it)
    cmp word [vb_apring], 0
    je .cur
    mov ax, [vb_apat]
    mov dx, [vb_apat+2]
    sub ax, [vb_wpos]
    sbb dx, [vb_wpos+2]
    jb .go                          ; (past: late, it starts now)
    jnz .cur
    or ax, ax
    jz .go
    cmp cx, ax
    jbe .cur
    mov cx, ax
    jmp short .cur
.go:
    call vb_asound
.cur:
    ; the clip's sound playing: to the end of its frame
    mov byte [vb_aown], 0
    cmp word [vb_aring], 0
    je .mus
    mov ax, [vb_meta+VH_SPF]
    sub ax, [vb_afpos]
    cmp cx, ax
    jbe .fc
    mov cx, ax
.fc:
    test word [vb_aflags], CF_OWNSND
    jz .mus
    mov byte [vb_aown], 1           ; OWN: no music under it
    mov byte [vb_mdry], 1
    jmp short .m
.mus:
    cmp byte [vb_mon], 0            ; (the music not in yet: silence)
    jne .mon
    mov byte [vb_mdry], 1
    jmp short .m
.mon:
    cmp byte [vb_mres], 0
    je .wring
    mov si, [vb_mtake]              ; RESIDENT: to the music's own end
    mov ax, [vb_meta+VH_MLEN]
    sub ax, si
    cmp cx, ax
    jbe .wr
    mov cx, ax
.wr:
    mov byte [vb_mdry], 0
    jmp short .m
.wring:
    mov si, [vb_mtake]              ; the music, out of its ring
    and si, VB_MR - 1
    mov ax, VB_MR
    sub ax, si
    cmp cx, ax
    jbe .mw
    mov cx, ax
.mw:
    mov ax, [vb_mfill]
    sub ax, [vb_mtake]
    jnz .mh
    inc word [vb_munder]            ; THE RING IS DRY: silence, the clock
    mov byte [vb_mdry], 1           ; still running
    jmp short .m
.mh:
    mov byte [vb_mdry], 0
    cmp cx, ax
    jbe .m
    mov cx, ax
.m:
    push cx
    push ds
    mov es, [vb_ring]
    push di
    push cx
    cmp byte [vb_mdry], 0
    jne .sil
    mov ds, [vb_mring]
    rep movsb
    jmp short .cpd
.sil:
    mov al, 128
    rep stosb
.cpd:
    pop cx
    pop di
    pop ds
    ; the clip's sound: this frame's bytes, as far as the frame has them
    cmp word [vb_aring], 0
    je .ui
    call vb_aframe
.ui:
    cmp byte [vb_fvol], 0           ; a fade: the sound comes down with it
    je .ui0
    call vb_atten
.ui0:
    ; the menu's sound, over all of it
    cmp word [vb_ulen], 0
    je .noui
    mov ax, [vb_ulen]
    cmp cx, ax
    jbe .u
    mov cx, ax
.u:
    sub [vb_ulen], cx
    mov si, [vb_uoff]
    add [vb_uoff], cx
    push ds
    push di
    mov ds, [vb_sfxseg]
.umix:
    lodsb
    add [es:di], al
    inc di
    loop .umix
    pop di
    pop ds
.noui:
    pop cx
    add [vb_wpos], cx
    adc word [vb_wpos+2], 0
    cmp byte [vb_mdry], 0
    jne .clip
    add [vb_mtake], cx              ; the music's ring, given back
    cmp byte [vb_mres], 0
    je .clip
    mov ax, [vb_mtake]              ; (resident: from the top at its end)
    cmp ax, [vb_meta+VH_MLEN]
    jb .clip
    mov word [vb_mtake], 0
.clip:
    cmp word [vb_aring], 0          ; the clip's frame, on by CX samples
    je .tot
    add [vb_afpos], cx
    mov ax, [vb_afpos]
    cmp ax, [vb_meta+VH_SPF]
    jb .tot
    call vb_anext
.tot:
    mov es, [vb_ring]
    mov ax, [vb_wpos]
    mov [es:VB_RL+SND_EXT_TOTAL], ax
    ret

; vb_aframe - the clip's sound for the chunk at ES:DI, CX samples, in the
; frame at [vb_afpos]: added over the music (MIX), or written in its place
; (OWN, the music's silence already there). Keeps CX, DI, ES
vb_aframe:
    push cx
    push di
    cmp word [vb_aowe], 0           ; a frame owed: the record at the cursor
    jne .out                        ; is a missed one, and plays as silence
    mov bx, [vb_aring]
    mov dx, [bx+RG_FILL]            ; this frame's record read?
    mov ax, [bx+RG_FILL+2]
    sub dx, [bx+RG_AOFF]
    sbb ax, [bx+RG_AOFF+2]
    jz .h
    mov dx, 0xFFFF
.h:
    cmp dx, RH_SIZE
    jb .late
    cmp byte [bx+RG_INP], 0
    jne .inp
    push es
    mov es, [bx+RG_ASEG]
    mov si, [bx+RG_AOFS]
    mov ax, [es:si+RH_LEN]
    cmp ax, dx
    ja .late1
    mov dx, [es:si+RH_ALEN]         ; the frame's sound: DX bytes, at the
    add si, ax                      ; record's end
    sub si, dx
    mov ax, [vb_afpos]
    sub dx, ax                      ; what is left of it
    jbe .none1
    add si, ax
    cmp cx, dx
    jbe .n
    mov cx, dx
.n:
    mov ax, es
    pop es
    push ds
    mov ds, ax
    cmp byte [cs:vb_aown], 0        ; (DS is the ring's: CS is ours)
    jne .own
.mix:
    lodsb
    add [es:di], al
    inc di
    loop .mix
    pop ds
    jmp short .out
.own:
    rep movsb
    pop ds
    jmp short .out
.late1:
    pop es
.late:
    inc word [vb_alate]
    jmp short .out
.none1:
    pop es
.out:
    pop di
    pop cx
    ret
.inp:                               ; IN PLACE: the record where the hold
    mov ax, [bx+RG_AOFF]            ; has it (all of it is there), DS it
    mov dx, [bx+RG_AOFF+2]          ; through a borrow on XMS - the hook's
    call vb_iat                     ; interrupts are off already here
    push ds
    VB_DSDX
    mov ax, [si+RH_LEN]
    mov dx, [si+RH_ALEN]
    add si, ax
    sub si, dx
    mov ax, [cs:vb_afpos]
    sub dx, ax
    jbe .idone
    add si, ax
    cmp cx, dx
    jbe .in
    mov cx, dx
.in:
    cmp byte [cs:vb_aown], 0
    jne .iown
.imix:
    lodsb
    add [es:di], al
    inc di
    loop .imix
    jmp short .idone
.iown:
    rep movsb
.idone:
    VB_DSEND
    pop ds
    jmp short .out

; vb_anext - the clip's sound on to its next frame: past this record. Its
; last done, the ring is given back once its picture is too; an opening's
; sound is followed by the next opening's at once, and the last opening's
; by the music (125.3)
vb_anext:
    mov word [vb_afpos], 0
    inc word [vb_aowe]              ; a frame's time is over: one record owed
.rec:
    mov bx, [vb_aring]
    mov es, [bx+RG_ASEG]
    mov si, [bx+RG_AOFS]
    mov dx, [bx+RG_FILL]            ; the record read? If not, the sound's
    mov ax, [bx+RG_FILL+2]          ; frames go on without it, owed, and the
    sub dx, [bx+RG_AOFF]            ; sound passes over what it missed once
    sbb ax, [bx+RG_AOFF+2]
    jz .h
    mov dx, 0xFFFF
.h:
    cmp dx, RH_SIZE
    jb .stay                        ; it is: it keeps to the picture's
    cmp byte [bx+RG_INP], 0         ; timeline and never trails it (125.3)
    jne .inp
    mov cx, [es:si+RH_LEN]
.ilen:
    cmp cx, dx
    ja .stay
    dec word [vb_aowe]
    add [bx+RG_AOFF], cx
    adc word [bx+RG_AOFF+2], 0
    push di
    mov di, RG_ASEG
    call vb_cadv
    pop di
    dec word [vb_aleft]
    jz .over
    cmp word [vb_aowe], 0           ; frames missed: passed over too
    jne .rec
    ret
.over:
    test word [vb_aflags], CF_LOOP  ; A LOOP still on the glass: its sound
    jz .ao                          ; goes round with it
    cmp byte [bx+RG_VDONE], 0
    jne .ao
    cmp byte [vb_lstream], 0
    jne .as
    push di
    mov di, RG_AOFF
    call vb_lcur
    pop di
.as:
    mov ax, [vb_lnrec]
    mov [vb_aleft], ax
    ret
.ao:
    ; its sound is over
    mov byte [bx+RG_ADONE], 1
    mov word [vb_aring], 0
    test word [vb_aflags], CF_OPENING
    jz .out
    mov al, [vb_aonext]             ; the next opening's sound, from here
    cmp al, 0xFF
    je .music
    mov bx, [vb_aoring]
    mov [vb_apring], bx
    mov [vb_apclip], al
    mov ax, [vb_wpos]
    mov dx, [vb_wpos+2]
    mov [vb_apat], ax
    mov [vb_apat+2], dx
    call vb_asound
    ret
.music:
    mov byte [vb_mon], 1            ; the last: the music comes in
.out:
    ret
.stay:
    cmp word [vb_aowe], 1           ; (this frame's own wait is not late:
    jbe .st                         ; only a frame that passed with none)
    inc word [vb_alate]
.st:
    ret
.inp:                               ; (in place: its length where the hold
    push dx                         ; has it)
    mov ax, [bx+RG_AOFF]
    mov dx, [bx+RG_AOFF+2]
    call vb_iat
    call vb_ilen
    pop dx
    jmp .ilen

; vb_asound - the clip's sound booked at [vb_apat] starts now, from its
; ring's AUDIO cursor (its first record). The next opening's is noted for
; when this one ends
vb_asound:
    mov bx, [vb_aring]              ; a loop's sound given way to the next
    or bx, bx                       ; (the title's, as its way out starts)
    jz .a0
    mov byte [bx+RG_ADONE], 1
.a0:
    mov bx, [vb_apring]
    mov word [vb_apring], 0
    mov [vb_aring], bx
    mov word [vb_afpos], 0
    mov word [vb_aowe], 0
    push bx
    mov al, [vb_apclip]
    xor ah, ah
    mov bx, ax
    call vb_crec
    pop bx
    mov ax, [si+VC_NREC]
    mov [vb_aleft], ax
    mov ax, [si+VC_FLAGS]
    mov [vb_aflags], ax
    mov byte [vb_aonext], 0xFF
    cmp byte [vb_askip], 0          ; an end loop JOINED: its frame 0 is up
    je .k0                          ; already, so its sound starts at 1
    mov byte [vb_askip], 0
    push ax
    push di
    push si
    mov cx, [vb_lbase]
    add [bx+RG_AOFF], cx
    adc word [bx+RG_AOFF+2], 0
    mov di, RG_ASEG
    call vb_cadv
    dec word [vb_aleft]
    pop si
    pop di
    pop ax
.k0:
    test ax, CF_OPENING
    jz .out
    xor bx, bx                      ; AN OPENING: the one after it in turn,
.f:                                 ; for its sound to follow at once (the
    cmp bx, [vb_meta+VH_NOPEN]      ; sound runs ahead of the picture, so
    jae .out                        ; the picture's own [vb_onext] is not
    mov al, [vb_meta+VH_OPENS+bx]   ; yet this one's next)
    inc bx
    cmp al, [vb_apclip]
    jne .f
    cmp bx, [vb_meta+VH_NOPEN]
    jae .out
    mov ax, bx
    call vb_oring_of
    mov [vb_aonext], al
    mov [vb_aoring], bx
.out:
    ret

; =============================================================================
; the card (the UI task)
; =============================================================================
vb_snd_open:
    mov byte [vb_snd], 0
    xor ax, ax                      ; every clock from 0
    mov [vb_cons], ax
    mov [vb_cons32], ax
    mov [vb_cons32+2], ax
    mov [vb_pers], ax
    mov [vb_clock], ax
    mov [vb_clock+2], ax
    mov [vb_frac], ax
    mov [vb_due], ax
    mov [vb_due+2], ax
    mov [vb_wpos], ax
    mov [vb_wpos+2], ax
    mov [vb_late], ax
    mov word [vb_dmin], 2           ; (no card: two frames on)
    call OSAPI_SND_CAPS
    test ax, SND_CAP_PCM_BG
    jz .out
    mov word [vb_blk], 2048         ; the card's block, and what follows
    mov word [vb_lead], VB_LEAD     ; from it: the lead, and the clock's cap
    mov word [vb_pcap], 100
    mov byte [vb_oflag], SND_OPENF_RING + SND_OPENF_EXT + (VB_RLCODE << SND_OPENF_RLSH)
    test ax, SND_CAP_EXTBLK
    jz .blk
    mov word [vb_blk], 2048 >> VB_BLKCODE
    mov word [vb_lead], VB_LEAD / 2
    mov word [vb_pcap], 26
    or byte [vb_oflag], VB_BLKCODE << SND_OPENF_BLKSH
.blk:
    mov ax, [vb_lead]               ; an action's sound must start ahead of
    xor dx, dx                      ; the ring's write position: its anchor
    div word [vb_meta+VH_SPF]       ; is the lead's frames and two on
    add ax, 2
    mov [vb_dmin], ax
    cmp word [vb_ring], 0
    jne .have
    mov ax, VB_RL / 1024 + 1
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA_HI
    jc .out
    mov [vb_ring], dx
.have:
    mov es, [vb_ring]
    xor ax, ax
    mov [es:VB_RL+SND_EXT_TOTAL], ax
    mov [es:VB_RL+SND_EXT_CONS], ax
    mov byte [vb_snd], 1            ; (vb_feed's own test)
    call vb_feed                    ; the first VB_LEAD bytes
    mov byte [vb_snd], 0
    xor al, al                      ; verb 0: open, an external ring
    mov ah, [vb_oflag]
    xor si, si
    mov di, [vb_ring]
    mov cx, [vb_wpos]
    mov dx, [vb_meta+VH_RATE]
    call OSAPI_SND_STREAM
    jc .out
    or al, al
    jnz .out
    mov [vb_hand], ah
    mov byte [vb_snd], 1
.out:
    ret

; vb_play_init - the play's own state, before the hook is live: the first
; opening playing from ring 0 with its sound booked at sample 0, the next
; noted; or, with no opening, the idle loop and the music at once
vb_play_init:
    xor ax, ax
    mov [vb_aring], ax
    mov [vb_apring], ax
    mov [vb_ulen], ax
    mov [vb_alate], ax
    mov [vb_stall], ax
    mov [vb_ui], al
    mov [vb_bandok], al
    mov [vb_banddone], al
    mov [vb_mon], al
    mov word [vb_cfr], 0
    mov byte [vb_onext], 0xFF
    mov byte [vb_olast], 0xFF
    mov [vb_tplay], al              ; (125.3.1's: no title, fade, loop yet)
    mov [vb_lwant], al
    mov [vb_estart], al
    mov [vb_eplay], al
    mov [vb_fon], al
    mov [vb_fwant], al
    mov [vb_fvol], al
    mov [vb_askip], al
    mov word [vb_lk], 0xFFFF
    mov byte [vb_eloop], 0xFF
    mov byte [vb_ehow], EH_HOLD
    mov al, [vb_meta+VH_FSTEPS]     ; a fade's steps, 1..16
    dec al
    and al, 15
    inc al
    mov [vb_fsteps], al
    mov cx, [vb_meta+VH_NOPEN]
    jcxz .loop
    mov bx, cx
    mov al, [vb_meta+VH_OPENS+bx-1]
    mov [vb_olast], al
    mov al, [vb_meta+VH_TITLE]      ; A TITLE: it goes round out of ring 1
    cmp al, 0xFF                    ; while opening 0 is read into ring 0
    je .open
    mov bx, vb_rg1
    call vb_start_clip
    mov byte [vb_tplay], 1
    mov [vb_apring], bx
    mov [vb_apclip], al
    xor ax, ax
    mov [vb_apat], ax
    mov [vb_apat+2], ax
    mov byte [vb_oidx], 0xFF
    call vb_onextset                ; (the next is opening 0)
    ret
.open:
    mov byte [vb_oidx], 0
    mov al, [vb_meta+VH_OPENS]
    mov bx, vb_rg0
    call vb_start_clip
    mov [vb_apring], bx             ; its sound from the very first sample
    mov [vb_apclip], al
    xor ax, ax
    mov [vb_apat], ax
    mov [vb_apat+2], ax
    call vb_onextset                ; (opening 0 plays: the next is 1)
    ret
.loop:
    mov ax, [vb_meta+VH_NIDLE]      ; no opening: the loop from its frame 0
    dec ax                          ; (its record 0 takes the last frame to
    mov [vb_ifr], ax                ; the first), the music at once, the
    mov byte [vb_mode], VM_IDLE     ; band too
    mov byte [vb_mon], 1
    mov byte [vb_bandok], 1
    ret

; vb_snd_keep - an underrun resumes the card once a block is queued
vb_snd_keep:
    cmp byte [vb_snd], 0
    je .out
    inc byte [vb_kn]
    test byte [vb_kn], 7
    jnz .out
    mov al, 3
    mov ah, [vb_hand]
    call OSAPI_SND_STREAM
    cmp ax, 2                       ; ENDED: the play goes on silent
    je .dead
    cmp ax, 1                       ; UNDER
    jne .out
    mov al, 1
    mov ah, [vb_hand]
    mov cx, [vb_wpos]
    call OSAPI_SND_STREAM
.out:
    ret
.dead:
    mov byte [vb_snd], 0
    ret

vb_snd_close:
    cmp byte [vb_snd], 0
    je .out
    mov byte [vb_snd], 0
    mov al, 2
    mov ah, [vb_hand]
    call OSAPI_SND_STREAM
.out:
    ret

; =============================================================================
; reading ahead (the UI task, in the bracket)
; =============================================================================

; vb_reader - one piece of what is most worth reading (125.4):
;   1. the music, while its ring holds under half;
;   2. a clip playing, while its ring holds under half;
;   3. the next opening's head;
;   4. the action pending's head;
;   5. a clip playing, to its ring's top: what is on the glass first;
;   -  after a key, nothing more for a moment (VB_KQUIET);
;   6. the head of the clip under the cursor;
;   7. the head of the enemy's;
;   8. the music, to its ring's top.
; A ring whose clip has played and sounded is given back first
vb_reader:
    mov bx, vb_rg0
    call .done
    mov bx, vb_rg1
    call .done
    cmp byte [vb_mres], 0           ; (resident music: the clips have it all)
    jne .clips
    mov ax, [vb_mfill]
    sub ax, [vb_mtake]
    cmp ax, VB_MR / 2
    jae .clips
    jmp vb_mpiece
.clips:
    mov bx, vb_rg0                  ; 2. streaming
    call .low
    jc .read
    mov bx, vb_rg1
    call .low
    jc .read
    cmp byte [vb_onext], 0xFF       ; 3. the next opening
    je .pend
    mov bx, [vb_oring]
    mov al, [vb_onext]
    cmp al, [bx+RG_CLIP]
    je .ohave
    cmp byte [bx+RG_PLAY], 0        ; (its ring still the last opening's)
    jne .pend
    call vb_rreset
.ohave:
    call vb_rhead
    jnc .read
.pend:
    mov al, [vb_pend]               ; 4. the action pending
    cmp al, 0xFF
    je .eloop
    mov dl, 0xFF
    call vb_ringfor
    jc .eloop
    call vb_rhead
    jnc .read
.eloop:
    cmp byte [vb_state], GS_END     ; 4b. an ending's END loop (125.3.1)
    jne .quiet
    mov al, [vb_eloop]
    cmp al, 0xFF
    je .quiet
    cmp byte [vb_eplay], 0
    jne .quiet
    mov dl, [vb_pend]
    call vb_ringfor
    jc .quiet
    call vb_rhead
    jnc .read
.quiet:
    mov bx, vb_rg0                  ; 5. the playing clips, to the top - a
    call .play                      ; ring with no room passes the turn on
    jnc .q1                         ; (a loop read round is never all read,
    call vb_rpiece                  ; and would keep it from the music and
    jc .rerr                        ; the hold's fill)
    or al, al
    jnz .rok
.q1:
    mov bx, vb_rg1
    call .play
    jnc .q2
    call vb_rpiece
    jc .rerr
    or al, al
    jnz .rok
.q2:
    call OSAPI_GET_TICKS            ; A KEY A MOMENT AGO: nothing more yet.
    sub ax, [vb_ktick]              ; The next key would wait behind the
    cmp ax, VB_KQUIET               ; read in the BIOS buffer - Down then
    jb .out                         ; Enter are two keys a fifth of a second
    mov al, [vb_want]               ; apart. 6. the clip under the cursor
    cmp al, 0xFF
    je .enemy
    mov dl, [vb_eclip]
    call vb_ringfor
    jc .enemy
    call vb_rhead
    jnc .read
.enemy:
    cmp byte [vb_state], GS_INTRO   ; 7. the enemy's (not before the battle,
    je .top                         ; nor after it: an end loop's ring)
    cmp byte [vb_state], GS_END
    je .top
    mov al, [vb_eclip]
    cmp al, 0xFF
    je .top
    mov dl, [vb_want]
    call vb_ringfor
    jc .top
    call vb_rhead
    jnc .read
.top:
    call vb_mpiece                  ; 8. the music
    jc .rerr
    or al, al
    jnz .rok
    jmp vb_hstep                    ; 9. the HOLD's fill (VBATTLE-MEMORY 4)
.rok:
    clc
    ret
.rerr:
    stc
    ret
.read:
    jmp vb_rpiece
.out:
    ret
.done:                              ; ring BX played and sounded: given back
    cmp byte [bx+RG_PLAY], 0
    je .dn
    cmp byte [bx+RG_VDONE], 0
    je .dn
    cmp byte [vb_snd], 0            ; (no card: no sound to wait for)
    je .free
    cmp byte [bx+RG_ADONE], 0
    je .dn
.free:                              ; and its cursors back at the clip's
    pushf                           ; top: a clip that fit the ring whole
    cli                             ; plays again from it unread; one that
    mov byte [bx+RG_PLAY], 0        ; did not has lost its head to its tail,
    cmp byte [bx+RG_INP], 0         ; (in place: the hold has it all)
    jne .keep
    mov ax, [bx+RG_FILL+2]          ; and the ring is nobody's. (Its played
    cmp ax, [bx+RG_BYTES+2]         ; cursors left as they were, the enemy's
    jb .keep                        ; turn replayed the knight's clip from
    ja .drop                        ; its end: an idle screen, no reads)
    mov ax, [bx+RG_FILL]
    cmp ax, [bx+RG_BYTES]
    ja .drop
.keep:
    xor ax, ax
    mov [bx+RG_VOFF], ax
    mov [bx+RG_VOFF+2], ax
    mov [bx+RG_VOFS], ax
    mov [bx+RG_AOFF], ax
    mov [bx+RG_AOFF+2], ax
    mov [bx+RG_AOFS], ax
    mov [bx+RG_VDONE], al
    mov [bx+RG_ADONE], al
    mov ax, [bx+RG_SEG]
    mov [bx+RG_VSEG], ax
    mov [bx+RG_ASEG], ax
    popf
    ret
.drop:
    mov al, 0xFF
    call vb_rreset
    popf
.dn:
    ret
.low:                               ; CF=1 ring BX is playing and under half
    call .play
    jnc .lno
    mov ax, [bx+RG_FILL]            ; (twice what it holds, against all
    mov dx, [bx+RG_FILL+2]          ; of the ring)
    sub ax, [bx+RG_VOFF]
    sbb dx, [bx+RG_VOFF+2]
    shl ax, 1
    rcl dx, 1
    cmp dx, [bx+RG_BYTES+2]
    jb .lyes
    ja .lno
    cmp ax, [bx+RG_BYTES]
    jb .lyes
.lno:
    clc
    ret
.lyes:
    stc
    ret
.play:                              ; CF=1 ring BX is playing, not all read
    cmp byte [bx+RG_PLAY], 0
    je .pno
    cmp byte [bx+RG_VDONE], 0
    jne .pno
    push bx
    mov al, [bx+RG_CLIP]
    xor ah, ah
    mov bx, ax
    call vb_crec
    pop bx
    VB_STRM bx, si                  ; (a loop read round: always more)
    jnz .pyes
    mov ax, [bx+RG_FILL]
    mov dx, [bx+RG_FILL+2]
    cmp dx, [si+VC_LEN+2]
    jb .pyes
    ja .pno
    cmp ax, [si+VC_LEN]
    jb .pyes
.pno:
    clc
    ret
.pyes:
    stc
    ret

%include "vbattle/vbrules.inc"
%include "vbattle/vbgame.inc"
%include "vbattle/vbloop.inc"
%include "vbattle/vbhold.inc"
%include "vbattle/vbinp.inc"

; vb_strncpy - at most CX bytes of DS:SI to DS:DI, trailing blanks dropped
vb_strncpy:
    push ds
    pop es
    mov bx, di
.c:
    lodsb
    or al, al
    jz .end
    stosb
    cmp al, ' '
    je .sp
    mov bx, di
.sp:
    loop .c
.end:
    mov di, bx
    mov byte [di], 0
    ret

; vb_strcpy - DS:SI (NUL) to DS:DI, DI left on the NUL
vb_strcpy:
    push ds
    pop es
.c:
    lodsb
    stosb
    or al, al
    jnz .c
    dec di
    ret

; vb_utoa - AX in decimal at DS:DI, DI left past it. AX, DX, CX, BX spent
vb_utoa:
    push ds
    pop es
    mov bx, 10
    xor cx, cx
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.o:
    pop ax
    add al, '0'
    stosb
    loop .o
    mov byte [di], 0
    ret

%include "vbattle/vbband.inc"

; =============================================================================
; text
; =============================================================================
vb_tpl:
    dw 40, 40, 320, 72
    dw vb_title, vb_paint, vb_onkey, vb_onclick
vb_title:     db 'Video Battle', 0
vb_actab:     db 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0
vb_name:      db 'BATTLE.VBT', 0
vb_namev:     db 'BATTLEV.VBT', 0     ; the VGA rendition's (format 7)
vb_nameh:     db 'BATTLEH.VBT', 0     ; ...and Hercules'
vb_s_noherc:  db ' Needs BATTLEH.VBT on a Hercules', 0
vb_lines:     dw vb_s_t1, vb_s_t2, 0, vb_s_t4
vb_s_t1:      db ' VIDEO BATTLE', 0
vb_s_t2:      db ' Enter or click: play (Esc leaves)', 0
vb_s_t4:      db ' Needs BATTLE.VBT beside it', 0
vb_s_ready:   db ' Ready', 0
vb_s_load:    db ' Loading...', 0
vb_s_again:   db ' Enter or click to play again', 0
vb_s_nofile:  db ' BATTLE.VBT not found', 0
vb_s_notvbt:  db ' BATTLE.VBT is not a VBT1 file', 0
vb_s_nomem:   db ' Not enough memory', 0
vb_s_badread: db ' BATTLE.VBT: a read failed', 0
vb_s_norate:  db ' Needs the full kernel (FSXF_RATE)', 0
vb_s_nocga:   db ' Needs CGA, EGA or VGA', 0
vb_s_appears: db ' appears!', 0
vb_s_who:     db 'Who?', 0
vb_s_cannot:  db 'Cannot now', 0
vb_s_miss:    db 'MISS', 0
vb_s_escaped: db 'Escaped!   Esc to leave', 0
vb_s_nomp:    db 'Not enough MP', 0
vb_s_damage:  db ' damage', 0
vb_s_healed:  db 'The party heals ', 0
vb_s_hp:      db ' HP', 0
vb_s_hits:    db ' hits ', 0
vb_s_victory: db 'VICTORY!   Esc to leave', 0
vb_s_defeat:  db 'The party has fallen.   Esc to leave', 0
vb_s_victl:   db 'VICTORY!   Enter: again   Esc: leave', 0
vb_s_defl:    db 'The party has fallen.   Enter: again', 0
vb_s_victa:   db 'VICTORY!', 0
vb_s_defa:    db 'The party has fallen.', 0
vb_s_head:    db '        HP          MP', 0

%define VD_YIELD                    ; (the yield sites, live in place: vbinp)
%include "video/vdec.inc"

; =============================================================================
; bss
; =============================================================================
%assign VB_BSS 0
%macro VB_VAR 2
%1 equ os88_image_end+VB_BSS
%assign VB_BSS VB_BSS+%2
%endmacro
VB_VAR vb_meta, VB_META
VB_VAR vb_win, 2
VB_VAR vb_status, 2
VB_VAR vb_err, 2
VB_VAR vb_px, 2
VB_VAR vb_py, 2
VB_VAR vb_pw, 2
VB_VAR vb_ph, 2
VB_VAR vb_pcols, 2
VB_VAR vb_pbuf, 64
VB_VAR vb_cur, FSEQ_SIZE
VB_VAR vb_curm, FSEQ_SIZE
VB_VAR vb_rcur, 2
VB_VAR vb_pn, 2
VB_VAR vb_mpn, 2
VB_VAR vb_rbytes, 4
VB_VAR vb_rcalls, 2
VB_VAR vb_hper, 4                   ; hook periods, all told
VB_VAR vb_rper, 4                   ; ...of them inside vb_read
VB_VAR vb_rp0, 4
VB_VAR vb_tlast, 4
VB_VAR vb_tg, 4                     ; keys and the game
VB_VAR vb_tr, 4                     ; the reader
VB_VAR vb_tb, 4                     ; the band and the card
VB_VAR vb_tw, 4                     ; the wait
VB_VAR vb_nidle, 2
VB_VAR vb_tpre, 4
VB_VAR vb_tcp, 4
VB_VAR vb_ncopy, 4
VB_VAR vb_rkb, 2                    ; each clip ring's KB, this play
VB_VAR vb_rclm, 2                   ; ...their claim
VB_VAR vb_hbusy, 4
VB_VAR vb_rseg, 2
VB_VAR vb_rcap, 2
VB_VAR vb_loaded, 1
VB_VAR vb_vres, 2
VB_VAR vb_sfxseg, 2
VB_VAR vb_mring, 2
VB_VAR vb_mfill, 2
VB_VAR vb_mlow, 2                   ; ...its lowest as a band rebuild ends
VB_VAR vb_mtake, 2
VB_VAR vb_mrd, 4
VB_VAR vb_munder, 2
VB_VAR vb_mdry, 1
VB_VAR vb_ui, 1
VB_VAR vb_mres, 1
VB_VAR vb_mskip, 2
VB_VAR vb_rg0, RG_SIZE
VB_VAR vb_rg1, RG_SIZE
VB_VAR vb_cring, 2
VB_VAR vb_pring, 2
VB_VAR vb_pentry, 2
VB_VAR vb_dmin, 2
VB_VAR vb_stall, 2
VB_VAR vb_alate, 2
VB_VAR vb_aring, 2
VB_VAR vb_aleft, 2
VB_VAR vb_aowe, 2
VB_VAR vb_afpos, 2
VB_VAR vb_aflags, 2
VB_VAR vb_aown, 1
VB_VAR vb_apring, 2
VB_VAR vb_apclip, 1
VB_VAR vb_apat, 4
VB_VAR vb_aonext, 1
VB_VAR vb_aoring, 2
VB_VAR vb_mon, 1
VB_VAR vb_onext, 1
VB_VAR vb_oring, 2
VB_VAR vb_oidx, 1
VB_VAR vb_olast, 1
VB_VAR vb_bandok, 1
VB_VAR vb_banddone, 1
VB_VAR vb_uoff, 2
VB_VAR vb_ulen, 2
VB_VAR vb_ktick, 2
VB_VAR vb_blk, 2
VB_VAR vb_lead, 2
VB_VAR vb_pcap, 2
VB_VAR vb_oflag, 1
VB_VAR vb_mread, 1
VB_VAR vb_ring, 2
VB_VAR vb_hand, 1
VB_VAR vb_snd, 1
VB_VAR vb_kn, 1
VB_VAR vb_psfx, 2
VB_VAR vb_pmus, 2
VB_VAR vb_pent, 2                   ; the entries' place in the claim
VB_VAR vb_eseg, 2                   ; ...their segment
VB_VAR vb_eleft, 2
VB_VAR vb_enext, 2
VB_VAR vb_fsi, FSI_SIZE
VB_VAR vb_vseg, 2
VB_VAR vb_vid, 1
VB_VAR vb_ffirst, 1
VB_VAR vb_flast, 1
VB_VAR vb_font, 95*8
; the hook's
VB_VAR vb_ready, 1
VB_VAR vb_mode, 1
VB_VAR vb_hn, 1
VB_VAR vb_cc, 1
VB_VAR vb_cdone, 1
VB_VAR vb_pend, 1
VB_VAR vb_ifr, 2
VB_VAR vb_pk, 2
VB_VAR vb_bestd, 2
VB_VAR vb_cleft, 2
VB_VAR vb_cfr, 2
VB_VAR vb_cflags, 2
VB_VAR vb_cexit, 2
VB_VAR vb_cons, 2
VB_VAR vb_cons32, 4
VB_VAR vb_pers, 2
VB_VAR vb_clock, 4
VB_VAR vb_frac, 2
VB_VAR vb_due, 4
VB_VAR vb_late, 2
VB_VAR vb_wpos, 4
; the battle's
VB_VAR vb_quit, 1
VB_VAR vb_key, 2
VB_VAR vb_state, 1
VB_VAR vb_turn, 1
VB_VAR vb_cursor, 1
VB_VAR vb_want, 1
VB_VAR vb_wclip, 1
VB_VAR vb_dirty, 1
VB_VAR vb_menu, 1                   ; the open menu
VB_VAR vb_pmenu, 1                  ; ...the one it was opened from (FFh none)
VB_VAR vb_pcur, 1                   ; ...and its cursor there
VB_VAR vb_mact, 1                   ; the action being aimed
VB_VAR vb_tcur, 1                   ; ...at which character
VB_VAR vb_mrec, 2
VB_VAR vb_eclip, 1                  ; the enemy's likely clip (the read-ahead)
VB_VAR vb_eact, 1
VB_VAR vb_tinkp, 1
VB_VAR vb_tbg, 1
VB_VAR vb_iseg, 2                   ; the band's pictures
VB_VAR vb_bseg, 2                   ; the band's ground
VB_VAR vb_cseg, 2                   ; ...and its composition
VB_VAR vb_tcomp, 1                  ; text into the composition
VB_VAR vb_pimg, 2
VB_VAR vb_pbband, 2
; the battle's state, zeroed at its start (vb_game_init)
VB_VAR vb_tplay, 1                  ; the title goes round (125.3.1)
VB_VAR vb_lwant, 1                  ; ...and is to be left by its clip
VB_VAR vb_lk, 2                     ; the title frame it is left at, FFFFh
VB_VAR vb_lent, 2                   ; ...and its entry's paragraph
VB_VAR vb_lnow, 2
VB_VAR vb_lnrec, 2                  ; a loop's lap: records 1..n
VB_VAR vb_lbase, 2                  ; ...from record 0's end in its ring
VB_VAR vb_lring, 2                  ; ...which ring
VB_VAR vb_eloop, 1                  ; the ending's end loop, FFh
VB_VAR vb_ethen, 1                  ; ...what follows a once-clip (EH_*)
VB_VAR vb_awant, 1                  ; ...the play again, no fade, wanted
VB_VAR vb_ehow, 1                   ; ...how the ending goes on (EH_*)
VB_VAR vb_estart, 1                 ; 1: start it from 0, 2: from 1 (joined)
VB_VAR vb_eplay, 1                  ; it goes round
VB_VAR vb_askip, 1                  ; its sound from record 1 (joined)
VB_VAR vb_fwant, 1                  ; a fade wanted: what follows it
VB_VAR vb_fon, 1                    ; a fade under way
VB_VAR vb_fnext, 1                  ; 1 opening 0, 2 the end loop, 3 again
VB_VAR vb_fstep, 1
VB_VAR vb_fsteps, 1
VB_VAR vb_fvol, 1                   ; the sound's shift, 4 silent
VB_VAR vb_frows, 2
VB_VAR vb_fticks, 2                 ; the hook's frames since it began
VB_VAR vb_rsring, 2
VB_VAR vb_cgkb, 1
VB_VAR vb_hon, 1                    ; the HOLD is on (vbhold.inc)
VB_VAR vb_hbyp, 1                   ; ...its fill reading the disk
VB_VAR vb_hrn, 1                    ; ...its ranges
VB_VAR vb_hi, 1                     ; ...the one filling
VB_VAR vb_hfull, 1                  ; ...all of it there
VB_VAR vb_hpool, 2                  ; ...the pool, in 4 KBs
VB_VAR vb_hkb4, 2                   ; ...the block, in 4 KBs
VB_VAR vb_hk, 2
VB_VAR vb_hbnc, 2                   ; ...the fill's bounce
VB_VAR vb_hcopies, 2                ; ...reads it answered
VB_VAR vb_hcap, 2                   ; ...a gate's cap on the pool, 4 KBs
VB_VAR vb_htype, 1                  ; ...its store: 1 XMS, 2 EMS
VB_VAR vb_ehnd, 2                   ; ...EMS: the handle
VB_VAR vb_efseg, 2                  ; ...and the frame's segment
VB_VAR vb_eq, 1                     ; ...the quarters it holds: 0Fh or 3
VB_VAR vb_eport, 2                  ; ...the recipe (107.4): quarter 0's
VB_VAR vb_estep, 2                  ; register, the step to the next,
VB_VAR vb_eor, 1                    ; the bits ORed into a page,
VB_VAR vb_ebase, 2                  ; and the run's first page
VB_VAR vb_eslow, 1                  ; the board's speed: 0 RAM's, 1 to 4x, 2
VB_VAR vb_er10, 2                   ; ...slower; its time in RAM's tenths
VB_VAR vb_eforce, 1                 ; ...a test's answer, plus one (a poke)
VB_VAR vb_etseg, 2                  ; ...vb_erate's segment
VB_VAR vb_hinp, 1                   ; IN PLACE from here (vbinp.inc)
VB_VAR vb_noinp, 1                  ; ...never: the A/B gate (a test's poke)
VB_VAR vb_tier, 1                   ; ...XMS's tier: a 286 or a 386
VB_VAR vb_xpc, 4                    ; ...VB_XH's linear address
VB_VAR vb_xact, 1                   ; ...a borrow is on
VB_VAR vb_xif, 1                    ; ...and its caller had IF (2) or not
VB_VAR vb_icalls, 2                 ; ...records reached in place (a count)
VB_VAR vb_iyc, 2                    ; ...yields inside a borrow (a count)
VB_VAR vb_irings, 2                 ; ...rings given a clip in place
VB_VAR vb_htok, 4                   ; ...the block's token
VB_VAR vb_hcur, FSEQ_SIZE           ; ...the fill's own file cursor
VB_VAR vb_htab, 28 * 8              ; ...the ranges (HT_*)
VB_VAR vb_kind, 1                   ; the bundle's adapter (VK_*)
VB_VAR vb_tcol0, 2                  ; a text's first cell (vb_text)
VB_VAR vb_bpc0, 2                   ; vb_bpres: the first cell...
VB_VAR vb_bpcn, 2                   ; ...and how many
VB_VAR vb_vptab, 256                ; VGA4: a band byte's planes 0 and 1
VB_VAR vb_hptab, 512                ; Hercules: its even and odd rows
VB_VAR vb_namep, 2                  ; the bundle's name: its own rendition's                   ; the rings' guard, KB: 16 or 32
VB_VAR vb_pidl, 2                   ; the idle loop's part of the claim
VB_VAR vb_idseg, 2                  ; ...and its segment
VB_VAR vb_lstream, 1                ; the loop playing STREAMS (too big to
                                    ; hold in its ring: format 7)
VB_VAR vb_zstart, 0
VB_VAR vb_rng, 2
VB_VAR vb_hp, 8
VB_VAR vb_mp, 8
VB_VAR vb_lim, 4
VB_VAR vb_guard, 4
VB_VAR vb_charge, 4
VB_VAR vb_mods, 16
VB_VAR vb_modt, 16
VB_VAR vb_ehp, 2
VB_VAR vb_emp, 2
VB_VAR vb_eguard, 1
VB_VAR vb_echarge, 1
VB_VAR vb_emods, 4
VB_VAR vb_emodt, 4
VB_VAR vb_stance, 1
VB_VAR vb_sleft, 1
VB_VAR vb_sret, 1
VB_VAR vb_efol, 1                   ; a follow-up owed (125.6.7), NONE none
VB_VAR vb_eftgt, 1                  ; ...the target it keeps
VB_VAR vb_qfol, 1                   ; ...this turn's, taken at its start
VB_VAR vb_sturns, 1
VB_VAR vb_tnext, 1
VB_VAR vb_last, 1
VB_VAR vb_items, 16
VB_VAR vb_over, 1
VB_VAR vb_zend, 0
; the plan (vbrules.inc's vb_resolve)
VB_VAR vb_qa, 1
VB_VAR vb_qw, 1
VB_VAR vb_qchosen, 1
VB_VAR vb_qresp, 1
VB_VAR vb_qpick, 2
VB_VAR vb_qact, 2
VB_VAR vb_qn, 1
VB_VAR vb_qt, 4
VB_VAR vb_qhit, 4
VB_VAR vb_qcrit, 4
VB_VAR vb_qdmg, 8
VB_VAR vb_qheal, 8
VB_VAR vb_qfirst, 1
VB_VAR vb_qeff, 2
VB_VAR vb_qtot, 2
VB_VAR vb_qc1, 1
VB_VAR vb_qchg, 1
VB_VAR vb_qsum, 2
VB_VAR vb_qnopp, 1
VB_VAR vb_qnhit, 1
VB_VAR vb_qanycrit, 1
VB_VAR vb_qallg, 1
VB_VAR vb_qout, 1
VB_VAR vb_qsingle, 2
VB_VAR vb_qclip, 1
VB_VAR vb_qpk, 2
VB_VAR vb_qcur, 1
VB_VAR vb_qhealed, 1
VB_VAR vb_qrule, 2
VB_VAR vb_q32, 4
VB_VAR vb_racted, 1
VB_VAR vb_ractor, 1
VB_VAR vb_rflags, 1
VB_VAR vb_dvar, 2
VB_VAR vb_dpow, 2
VB_VAR vb_datk, 2
VB_VAR vb_ddef, 2
VB_VAR vb_msg, 2
VB_VAR vb_mbuf, 48
VB_VAR vb_lbuf, 48
VB_VAR vb_lay, 20                   ; the band's layout (vbband.inc, 125.5.1)
VB_VAR vb_plo, 1                    ; ...the party line's first column
VB_VAR vb_phi, 1                    ; ...and its end
VB_VAR vb_chrec, 2                  ; ...a character's record, composing
VB_VAR vb_nbuf, 8
VB_VAR vb_tcol, 2
VB_VAR vb_trow, 2
VB_VAR vb_tink, 1
VB_VAR vb_tc, 1
VB_VAR vb_tgnd, 1
VB_VAR vb_texp, 16
    OS88_BSS VB_BSS
    OS88_IMAGE_END
