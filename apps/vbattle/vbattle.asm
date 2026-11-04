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
VH_VER      equ 4               ; 3
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
RH_LEN      equ 0               ; a record: its length, all of it
RH_ALEN     equ 2               ; ...its frame's sound's, at its end
RH_SIZE     equ 4
VB_SCRIPT   equ 2048            ; the script
VS_ENAME    equ 2
VS_EHP      equ 12
VS_ECLIP    equ 14
VS_EDMG     equ 15
VS_VICT     equ 17
VS_DEFT     equ 18
VS_PARTY    equ 24              ; 64 bytes a member
VM_HP       equ 8
VM_MP       equ 10
VM_CMDS     equ 12              ; 4 x 12 bytes
VK_CLIP     equ 6
VK_KIND     equ 7
VK_AMT      equ 8
VK_MP       equ 10
K_HIT       equ 0
K_HEAL      equ 1

VB_META     equ 4096            ; the first region, read whole into our bss
VB_MAXCLIP  equ 32
VB_MAXOPEN  equ 8
VB_PIECE    equ 4096            ; a read: a cluster multiple, and under a
                                ; 360 KB track (9 sectors), so a key waits at
                                ; most about a revolution for it
VB_CR       equ 49152           ; a CLIP RING: 48 KB, read 4 KB a piece, and a
VB_CGUARD   equ 16384           ; 16 KB guard past it holding a copy of its
                                ; first 16 KB, so a record (at most the guard,
                                ; VH_MAXREC) is whole at its cursor (125.4)
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

; a clip ring (vb_rg0, vb_rg1)
RG_SEG      equ 0               ; its segment: VB_CR + VB_CGUARD bytes
RG_CLIP     equ 2               ; the clip it holds, FFh none
RG_PLAY     equ 3               ; the hook plays from it
RG_FILL     equ 4               ; dword: the stream's bytes read into it
RG_FIDX     equ 8               ; ...and where the next lands
RG_VOFF     equ 10              ; the picture's cursor (the stream's offset,
RG_VIDX     equ 12              ; its low word) and its place in the ring
RG_AOFF     equ 14              ; ...and the sound's
RG_AIDX     equ 16
RG_VDONE    equ 18              ; its picture is over
RG_ADONE    equ 19              ; ...and its sound
RG_CUR      equ 20              ; its own file cursor (FSEQ_SIZE)
RG_SIZE     equ 36

; the hook's modes
VM_OFF      equ 0
VM_CLIP     equ 1               ; a clip's records, in order
VM_IDLE     equ 2               ; the idle loop
VM_HOLD     equ 3               ; an ending's last frame, held

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
; short file. Clobbers AX, BX, CX, DX; [vb_rcur] back to vb_cur
vb_read:
    push si
    push di
    push bp
    mov [vb_rseg], es
    mov [vb_rcap], cx
    add [vb_rbytes], cx             ; (what was read, and in how many calls:
    adc word [vb_rbytes+2], 0       ; a reader's own account, 125.4)
    inc word [vb_rcalls]
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
    mov si, vb_name
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
    mov es, [vb_rseg]
    mov word [vb_rcur], vb_cur      ; (the next read's own unless it says)
    pop bp
    pop di
    pop si
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
    push ds                         ; the first region, into our bss
    pop es
    mov bx, vb_meta
    mov cx, VB_META
    xor ax, ax
    xor dx, dx
    call vb_read
    mov word [vb_err], vb_s_nofile
    jc .fail1
    mov word [vb_err], vb_s_notvbt
    cmp word [vb_meta+VH_MAGIC], 'VB'
    jne .fail1
    cmp word [vb_meta+VH_MAGIC+2], 'T1'
    jne .fail1
    cmp word [vb_meta+VH_VER], 3
    jne .fail1
    mov ax, [vb_meta+VH_NCLIPS]
    or ax, ax
    jz .fail1
    cmp ax, VB_MAXCLIP
    ja .fail1
    cmp word [vb_meta+VH_NOPEN], VB_MAXOPEN
    ja .fail1
    cmp word [vb_meta+VH_MAXREC], VB_CGUARD
    ja .fail1                       ; (a record past the guard: not ours)
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
    ; it), and the two clip rings of 64 KB
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
    mov [vb_prings], ax
    add ax, 2 * (VB_CR + VB_CGUARD) / 16
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
    add dx, [vb_prings]
    mov [vb_rg0+RG_SEG], dx
    add dx, (VB_CR + VB_CGUARD) / 16
    mov [vb_rg1+RG_SEG], dx
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
    mov byte [vb_loaded], 1
    call vb_fontload
.again:
    ; the rings empty; the first opening's head read now, into ring 0
    mov bx, vb_rg0
    mov al, 0xFF
    call vb_rreset
    mov bx, vb_rg1
    mov al, 0xFF                    ; (vb_rreset spends AL)
    call vb_rreset
    cmp word [vb_meta+VH_NOPEN], 0
    je .music
    mov al, [vb_meta+VH_OPENS]
    mov bx, vb_rg0
    call vb_rreset
.head:
    mov bx, vb_rg0
    call vb_rhead                   ; CF=1: its head is in
    jc .music
    mov bx, vb_rg0
    call vb_rpiece
    mov word [vb_err], vb_s_badread
    jc .fail
    or al, al
    jnz .head
.music:
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
    mov [bx+RG_FIDX], ax
    mov [bx+RG_VOFF], ax
    mov [bx+RG_VIDX], ax
    mov [bx+RG_AOFF], ax
    mov [bx+RG_AIDX], ax
    mov [bx+RG_PLAY], al
    mov [bx+RG_VDONE], al
    mov [bx+RG_ADONE], al
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

; vb_rpiece - one 4 KB piece of ring BX's clip, at the stream's fill
; position, when it is not all read and the ring has room past what the
; hook still wants of it (its picture's cursor and its sound's). What the
; piece puts past the ring's end is copied to its start, and what it puts
; in the ring's first VB_CGUARD bytes is copied past its end, so a record
; is always whole at its cursor (125.4). Out AL = 1 a piece was read, 0
; nothing to read; CF=1 a read failed
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
    mov ax, [ds:bp+RG_FILL]         ; all of it read?
    mov dx, [ds:bp+RG_FILL+2]
    cmp dx, [si+VC_LEN+2]
    jb .some
    ja .none
    cmp ax, [si+VC_LEN]
    jae .none
.some:
    mov cx, [ds:bp+RG_FILL]         ; room: the ring less what is still
    mov ax, cx                      ; wanted past the EARLIER cursor
    sub cx, [ds:bp+RG_VOFF]
    sub ax, [ds:bp+RG_AOFF]
    cmp ax, cx
    jbe .r1
    mov cx, ax
.r1:
    mov ax, VB_CR                   ; the room, in whole pieces
    sub ax, cx
    jc .none
    and ax, 0 - VB_PIECE
    jz .none
    call vb_psize                   ; the piece: as much of it as the
    mov cx, [ds:bp+RG_FIDX]         ; moment allows, and no further than
    neg cx                          ; the guard's end: 64 KB (the ring and
    jz .p1                          ; its guard) less the index, 0 none
    cmp ax, cx
    jbe .p1
    mov ax, cx
.p1:
    mov [vb_pn], ax
    mov ax, [si+VC_LEN]             ; and no further than the clip's last
    mov dx, [si+VC_LEN+2]           ; piece: the last clip in the bundle
    sub ax, [ds:bp+RG_FILL]         ; ends at the file's end, and a read
    sbb dx, [ds:bp+RG_FILL+2]       ; past that fails - every time, as the
    jnz .p2                         ; reader asked again (asked before it
    cmp ax, [vb_pn]                 ; is rounded: 61,441 and up laps)
    jae .p2
    add ax, VB_PIECE - 1
    and ax, 0 - VB_PIECE
    mov [vb_pn], ax
.p2:
    mov ax, [ds:bp+RG_FILL]         ; the file offset
    mov dx, [ds:bp+RG_FILL+2]
    add ax, [si+VC_OFF]
    adc dx, [si+VC_OFF+2]
    mov es, [ds:bp+RG_SEG]
    mov bx, [ds:bp+RG_FIDX]
    mov cx, [vb_pn]
    lea di, [ds:bp+RG_CUR]          ; its own cursor: a clip's reads follow
    mov [vb_rcur], di               ; on, with no walk of the chain
    call vb_read
    jc .out
    mov bx, [ds:bp+RG_FIDX]
    push ds
    mov ax, [ds:bp+RG_SEG]
    mov es, ax
    mov ds, ax
    cld
    mov cx, [cs:vb_pn]              ; past the end: to the start. (DS is
    mov ax, VB_CR                   ; the ring's: CS is ours.) Worked from
    sub ax, bx                      ; the room before the end, not from the
    sub cx, ax                      ; end's offset: a piece may end at 64 KB
    jbe .mirror                     ; exactly, which a word cannot hold
    mov si, VB_CR
    xor di, di
    rep movsb
.mirror:
    cmp bx, VB_CGUARD               ; in the first VB_CGUARD bytes: past the
    jae .mirrored                   ; end as well
    mov cx, VB_CGUARD
    sub cx, bx
    cmp cx, [cs:vb_pn]
    jbe .m1
    mov cx, [cs:vb_pn]
.m1:
    mov si, bx
    mov di, bx
    add di, VB_CR
    rep movsb
.mirrored:
    pop ds
    mov ax, VB_CR                   ; the fill index on, wrapped - from the
    sub ax, bx                      ; room before the end, for the same
    mov cx, [vb_pn]                 ; reason
    cmp cx, ax
    jb .w0
    sub cx, ax                      ; past it: the piece's overhang
    mov ax, cx
    jmp short .w
.w0:
    mov ax, bx
    add ax, cx
.w:
    mov [ds:bp+RG_FIDX], ax
    pushf                           ; the fill on: published last, and whole
    cli                             ; (the hook reads it)
    mov ax, [vb_pn]
    add [ds:bp+RG_FILL], ax
    adc word [ds:bp+RG_FILL+2], 0
    popf
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

; vb_psize - AX = the room (a multiple of VB_PIECE) cut to the piece the
; moment allows (125.4): VB_PIECE while a menu waits on a key, else up to
; VB_SPIECE - a read costs the kernel a call's overhead whatever it moves,
; and four 4 KB reads took 80 ms each where one of 16 KB is far less
vb_psize:
    cmp byte [vb_state], GS_MENU
    jne .big
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
    test ax, 1 << FSXM_CGA320
    jnz .mode
    mov word [vb_err], vb_s_nocga
    ret
.mode:
    mov al, FSXM_CGA320
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
    call vb_game_init
    call vb_play_init
    call vb_snd_open
    mov byte [vb_ready], 1          ; the hook is live from here
.loop:
    call vb_keys
    cmp byte [vb_quit], 0
    jne .leave
    call vb_game
    call vb_reader
    call vb_band
    call vb_snd_keep
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp .loop
.leave:
    mov byte [vb_ready], 0
    call vb_snd_close
    ret

; vb_palette - mode 4's palette from the bundle's byte (Video Player's
; vp_cgaset .c4, 98.1.3.3): background and intensity, the set, and mode 5's
; cyan, red and white
vb_palette:
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
    cmp al, 27
    jne .k
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

; vb_rrec - BX = a ring; out: CF=0 and SI = the record at its VIDEO cursor,
; whole in memory, CX = its length; CF=1 not read that far yet. AX, DX spent
vb_rrec:
    mov dx, [bx+RG_FILL]            ; what is read past the cursor (16 bits:
    sub dx, [bx+RG_VOFF]            ; never more than the ring)
    cmp dx, RH_SIZE
    jb .no
    mov es, [bx+RG_SEG]
    mov si, [bx+RG_VIDX]
    mov cx, [es:si+RH_LEN]
    cmp cx, dx
    ja .no
    clc
    ret
.no:
    stc
    ret

; vb_radv - BX = a ring: its video cursor past CX bytes (a record)
vb_radv:
    add [bx+RG_VOFF], cx
    mov ax, [bx+RG_VIDX]
    add ax, cx
    cmp ax, VB_CR
    jb .in
    sub ax, VB_CR
.in:
    mov [bx+RG_VIDX], ax
    ret

; vb_start_clip - the hook plays clip AL out of ring BX from the record at
; its cursor (an opening's first, or an action's second: the entry drew its
; first frame). Called from the hook, or before [vb_ready]
vb_start_clip:
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
    mov byte [vb_mode], VM_CLIP
    ret

; vb_hook - FSXF_RATE's hook (53.2.2): AX = periods since the last call,
; DS = ours, IF = 0, at most 48 bytes of stack, no kernel calls
vb_hook:
    cld
    cmp byte [vb_ready], 0
    jne .live
    ret
.live:
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
    clc
    ret
.clip:
    mov bx, [vb_cring]
    call vb_rrec
    jc .wait
    push cx
    mov dx, es
    call vb_decode
    pop cx
    mov bx, [vb_cring]
    call vb_radv
    inc word [vb_cfr]
    test word [vb_cflags], CF_OPENING
    jz .cnt
    cmp word [vb_cfr], 1            ; the LAST opening's first frame is up:
    jne .cnt                        ; the band may be drawn under it now
    mov al, [vb_cc]
    cmp al, [vb_olast]
    jne .cnt
    mov byte [vb_bandok], 1
.cnt:
    dec word [vb_cleft]
    jz .over
    clc
    ret
.over:
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
    mov es, [vb_vres]               ; the entry: frame 0's picture
    mov si, [vb_pentry]
    mov dx, es
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
    mov si, [es:si]
    mov dx, es
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
    mov di, [si+VC_ANCH]            ; (k, entry) pairs in the resident block
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
    mov ds, dx
    add si, RH_SIZE
    xor bp, bp
    call vd_native
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
    sub dx, [bx+RG_AOFF]
    cmp dx, RH_SIZE
    jb .late
    push es
    mov es, [bx+RG_SEG]
    mov si, [bx+RG_AIDX]
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

; vb_anext - the clip's sound on to its next frame: past this record. Its
; last done, the ring is given back once its picture is too; an opening's
; sound is followed by the next opening's at once, and the last opening's
; by the music (125.3)
vb_anext:
    mov word [vb_afpos], 0
    inc word [vb_aowe]              ; a frame's time is over: one record owed
.rec:
    mov bx, [vb_aring]
    mov es, [bx+RG_SEG]
    mov si, [bx+RG_AIDX]
    mov dx, [bx+RG_FILL]            ; the record read? If not, the sound's
    sub dx, [bx+RG_AOFF]            ; frames go on without it, owed, and the
    cmp dx, RH_SIZE                 ; sound passes over what it missed once
    jb .stay                        ; it is: it keeps to the picture's
    mov cx, [es:si+RH_LEN]          ; timeline and never trails it (125.3)
    cmp cx, dx
    ja .stay
    dec word [vb_aowe]
    add [bx+RG_AOFF], cx
    mov ax, si
    add ax, cx
    cmp ax, VB_CR
    jb .in
    sub ax, VB_CR
.in:
    mov [bx+RG_AIDX], ax
    dec word [vb_aleft]
    jz .over
    cmp word [vb_aowe], 0           ; frames missed: passed over too
    jne .rec
    ret
.over:
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

; vb_asound - the clip's sound booked at [vb_apat] starts now, from its
; ring's AUDIO cursor (its first record). The next opening's is noted for
; when this one ends
vb_asound:
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
    mov cx, [vb_meta+VH_NOPEN]
    jcxz .loop
    mov bx, cx
    mov al, [vb_meta+VH_OPENS+bx-1]
    mov [vb_olast], al
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
    je .quiet
    mov dl, 0xFF
    call vb_ringfor
    jc .quiet
    call vb_rhead
    jnc .read
.quiet:
    mov bx, vb_rg0                  ; 5. the playing clips, to the top
    call .play
    jc .read
    mov bx, vb_rg1
    call .play
    jc .read
    call OSAPI_GET_TICKS            ; A KEY A MOMENT AGO: nothing more yet.
    sub ax, [vb_ktick]              ; The next key would wait behind the
    cmp ax, VB_KQUIET               ; read in the BIOS buffer - Down then
    jb .out                         ; Enter are two keys a fifth of a second
    mov al, [vb_want]               ; apart. 6. the clip under the cursor
    cmp al, 0xFF
    je .enemy
    mov dl, [vb_meta+VB_SCRIPT+VS_ECLIP]
    call vb_ringfor
    jc .enemy
    call vb_rhead
    jnc .read
.enemy:
    cmp byte [vb_state], GS_INTRO   ; 7. the enemy's
    je .top
    mov al, [vb_meta+VB_SCRIPT+VS_ECLIP]
    mov dl, [vb_want]
    call vb_ringfor
    jc .top
    call vb_rhead
    jnc .read
.top:
    jmp vb_mpiece                   ; 8. the music
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
    cmp word [bx+RG_FILL+2], 0      ; and the ring is nobody's. (Its played
    jne .drop                       ; cursors left as they were, the enemy's
    cmp word [bx+RG_FILL], VB_CR    ; turn replayed the knight's clip from
    ja .drop                        ; its end: an idle screen, no reads)
    xor ax, ax
    mov [bx+RG_VOFF], ax
    mov [bx+RG_VIDX], ax
    mov [bx+RG_AOFF], ax
    mov [bx+RG_AIDX], ax
    mov [bx+RG_VDONE], al
    mov [bx+RG_ADONE], al
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
    mov ax, [bx+RG_FILL]
    sub ax, [bx+RG_VOFF]
    cmp ax, VB_CR / 2
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

; =============================================================================
; the battle (the UI task)
; =============================================================================
vb_game_init:
    mov byte [vb_state], GS_INTRO
    mov byte [vb_turn], 0
    mov byte [vb_cursor], 0
    mov byte [vb_pend], 0xFF
    mov word [vb_pk], 0xFFFF
    mov byte [vb_want], 0xFF
    mov byte [vb_cdone], 0xFF
    mov byte [vb_quit], 0
    mov word [vb_key], 0
    mov byte [vb_mode], VM_OFF
    mov byte [vb_dirty], 0xFF
    mov ax, [vb_meta+VB_SCRIPT+VS_EHP]
    mov [vb_ehp], ax
    xor bx, bx
.m:
    mov si, bx
    mov cl, 6
    shl si, cl
    add si, vb_meta+VB_SCRIPT+VS_PARTY
    mov ax, [si+VM_HP]
    mov di, bx
    shl di, 1
    mov [vb_hp+di], ax
    mov ax, [si+VM_MP]
    mov [vb_mp+di], ax
    inc bx
    cmp bx, 2
    jb .m
    mov word [vb_msg], vb_s_begin
    ret

; vb_mrec - SI = member [vb_turn]'s script row
vb_mrec:
    mov al, [vb_turn]
    xor ah, ah
    mov cl, 6
    shl ax, cl
    add ax, vb_meta+VB_SCRIPT+VS_PARTY
    mov si, ax
    ret

; vb_kcmd - SI = command [vb_cursor] of member [vb_turn]
vb_kcmd:
    call vb_mrec
    mov al, [vb_cursor]
    mov ah, 12
    mul ah
    add si, ax
    add si, VM_CMDS
    ret

; vb_ncmds - CX = the member's commands (those with a clip)
vb_ncmds:
    call vb_mrec
    add si, VM_CMDS
    xor cx, cx
.c:
    cmp byte [si+VK_CLIP], 0xFF
    je .out
    add si, 12
    inc cx
    cmp cx, 4
    jb .c
.out:
    ret

vb_game:
    mov ax, [vb_key]
    mov word [vb_key], 0
    mov bl, [vb_state]
    cmp bl, GS_INTRO
    je .intro
    cmp bl, GS_MENU
    je .menu
    cmp bl, GS_WAIT
    je .wait
    ret                             ; GS_END: nothing but Esc
.intro:
    cmp byte [vb_bandok], 0         ; THE SCENE OPENING'S first frame is up:
    je .r                           ; the band under it, the menu not yet
    cmp byte [vb_banddone], 0
    jne .i2
    mov byte [vb_banddone], 1
    call vb_band_frame
.i2:
    cmp byte [vb_mode], VM_IDLE     ; ...and it has seamed onto the loop
    jne .r
    mov byte [vb_cdone], 0xFF
    jmp vb_next_turn
.r:
    ret
.menu:
    or ax, ax
    jz .hover
    cmp ah, 0x48                    ; Up
    je .up
    cmp ah, 0x50                    ; Down
    je .down
    cmp al, 13
    je .enter
    cmp al, ' '
    je .enter
    jmp short .hover
.up:
    cmp byte [vb_cursor], 0
    je .hover
    dec byte [vb_cursor]
    jmp short .moved
.down:
    call vb_ncmds
    mov al, [vb_cursor]
    inc al
    xor ah, ah
    cmp ax, cx
    jae .hover
    mov [vb_cursor], al
.moved:
    or byte [vb_dirty], 1
    mov al, [vb_meta+VH_UI]         ; the tick
    call vb_say
.hover:
    call vb_kcmd                    ; read ahead what the cursor is on
    mov al, [si+VK_CLIP]
    mov [vb_want], al
    ret
.enter:
    call vb_kcmd
    mov al, [vb_turn]
    xor ah, ah
    mov di, ax
    shl di, 1
    mov ax, [si+VK_MP]
    cmp ax, [vb_mp+di]
    jbe .afford
    mov word [vb_msg], vb_s_nomp
    or byte [vb_dirty], 4
    mov al, [vb_meta+VH_UI+2]       ; the deny
    jmp vb_say
.afford:
    sub [vb_mp+di], ax
    push si
    mov al, [vb_meta+VH_UI+1]       ; the ok
    call vb_say
    pop si
    mov [vb_acmd], si               ; what happens when its clip is over
    mov byte [vb_actor], 0
    mov al, [si+VK_CLIP]
    mov [vb_wclip], al
    mov byte [vb_state], GS_WAIT
    mov byte [vb_cdone], 0xFF
    mov word [vb_msg], vb_mbuf
    call vb_say_cmd
    or byte [vb_dirty], 7
    mov al, [vb_wclip]
    mov [vb_pend], al               ; the hook takes it at its anchor
    ret
.wait:
    mov al, [vb_cdone]
    cmp al, [vb_wclip]
    jne .r
    mov byte [vb_cdone], 0xFF
    cmp byte [vb_actor], 0
    jne .enemy
    ; the party's action, done: its effect
    mov si, [vb_acmd]
    mov ax, [si+VK_AMT]
    cmp byte [si+VK_KIND], K_HEAL
    je .heal
    sub [vb_ehp], ax
    jae .hit
    mov word [vb_ehp], 0
.hit:
    mov di, vb_mbuf
    call vb_utoa
    mov si, vb_s_damage
    call vb_strcpy
    jmp short .said
.heal:
    xor bx, bx
.h1:
    mov si, bx
    mov cl, 6
    shl si, cl
    add si, vb_meta+VB_SCRIPT+VS_PARTY
    mov di, bx
    shl di, 1
    cmp word [vb_hp+di], 0          ; the fallen stay down
    je .h2
    mov dx, [vb_hp+di]
    add dx, ax
    cmp dx, [si+VM_HP]
    jbe .h3
    mov dx, [si+VM_HP]
.h3:
    mov [vb_hp+di], dx
.h2:
    inc bx
    cmp bx, 2
    jb .h1
    mov di, vb_mbuf
    mov si, vb_s_healed
    push ax
    call vb_strcpy
    pop ax
    call vb_utoa
    mov si, vb_s_hp
    call vb_strcpy
.said:
    mov word [vb_msg], vb_mbuf
    or byte [vb_dirty], 6
    cmp word [vb_ehp], 0
    jne vb_next_turn
    mov al, [vb_meta+VB_SCRIPT+VS_VICT]
    mov si, vb_s_victory
    jmp vb_ending
.enemy:
    ; the enemy hit [vb_target]
    mov bl, [vb_target]
    xor bh, bh
    shl bx, 1
    mov ax, [vb_meta+VB_SCRIPT+VS_EDMG]
    sub [vb_hp+bx], ax
    jae .e1
    mov word [vb_hp+bx], 0
.e1:
    mov di, vb_mbuf
    mov si, vb_meta+VB_SCRIPT+VS_ENAME
    mov cx, 10
    call vb_strncpy
    mov si, vb_s_hits
    call vb_strcpy
    mov si, [vb_target]
    and si, 0xFF
    mov cl, 6
    shl si, cl
    add si, vb_meta+VB_SCRIPT+VS_PARTY
    mov cx, 8
    call vb_strncpy
    mov al, ' '
    stosb
    mov ax, [vb_meta+VB_SCRIPT+VS_EDMG]
    call vb_utoa
    mov word [vb_msg], vb_mbuf
    or byte [vb_dirty], 6
    mov ax, [vb_hp]
    or ax, [vb_hp+2]
    jnz vb_next_turn
    mov al, [vb_meta+VB_SCRIPT+VS_DEFT]
    mov si, vb_s_defeat
    jmp vb_ending

; vb_say - menu sound AL (FFh none) for the hook to start: the foreground
; asks, the hook mixes (vb_uisound)
vb_say:
    cmp al, 0xFF
    je .out
    cmp al, [vb_meta+VH_NSFX]
    jae .out
    inc al
    mov [vb_ui], al
.out:
    ret

; vb_next_turn - the next member standing, or the enemy after the last
vb_next_turn:
    cmp byte [vb_state], GS_INTRO
    jne .on
    mov byte [vb_turn], 0xFF        ; (the first turn is member 0's)
.on:
    inc byte [vb_turn]
    cmp byte [vb_turn], 2
    jae .enemy
    mov bl, [vb_turn]
    xor bh, bh
    shl bx, 1
    cmp word [vb_hp+bx], 0
    je .on                          ; down: skipped
    mov byte [vb_state], GS_MENU
    mov byte [vb_cursor], 0
    or byte [vb_dirty], 3
    ret
.enemy:
    mov byte [vb_turn], 0xFF        ; the party's turns start again after it
    mov bl, [vb_tnext]              ; the target: alternate, the standing
    xor bh, bh
    and bl, 1
    mov di, bx
    shl di, 1
    cmp word [vb_hp+di], 0
    jne .t
    xor bl, 1
.t:
    mov [vb_target], bl
    inc byte [vb_tnext]
    mov byte [vb_actor], 1
    mov al, [vb_meta+VB_SCRIPT+VS_ECLIP]
    mov [vb_wclip], al
    mov byte [vb_state], GS_WAIT
    mov byte [vb_cdone], 0xFF
    or byte [vb_dirty], 3
    mov [vb_pend], al
    mov byte [vb_want], 0xFF
    ret

; vb_ending - AL = the ending's clip, SI = its line
vb_ending:
    mov [vb_msg], si
    mov [vb_wclip], al
    mov byte [vb_state], GS_END
    mov byte [vb_turn], 0xFF
    mov byte [vb_want], 0xFF
    or byte [vb_dirty], 7
    mov [vb_pend], al
    ret

; vb_say_cmd - the message: "ARI: Bolt"
vb_say_cmd:
    push si
    mov di, vb_mbuf
    call vb_mrec
    mov cx, 8
    call vb_strncpy
    mov al, ':'
    stosb
    mov al, ' '
    stosb
    pop si
    mov cx, 6
    call vb_strncpy
    mov byte [di], 0
    ret

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

; =============================================================================
; the band (rows 142-199): drawn straight into the framebuffer, only what
; changed. [vb_dirty] bit 0 the menu, 1 the party, 2 the message line
; =============================================================================
BAND_Y      equ 142
ROW_MENU    equ 148             ; the menu's first line; 10 apart
ROW_MSG     equ 189
COL_MENU    equ 1
COL_PARTY   equ 18

vb_band_frame:
    ; the band's ground black, and its rules cyan: a row of 1-1-1-1
    mov es, [vb_vseg]
    cld
    mov dx, BAND_Y
.clr:
    call vb_rowaddr
    mov cx, 40
    xor ax, ax
    cmp dx, BAND_Y + 1
    je .rule
    cmp dx, ROW_MSG - 4
    jne .st
.rule:
    mov ax, 0x5555
.st:
    rep stosw
    inc dx
    cmp dx, 200
    jb .clr
    ; the divider between the menu and the party: column 16 (pixel 128)
    mov dx, BAND_Y + 3
.v:
    call vb_rowaddr
    add di, 32
    mov byte [es:di], 0x40
    inc dx
    cmp dx, ROW_MSG - 4
    jb .v
    mov byte [vb_dirty], 0xFF
    ret

; vb_rowaddr - DI = the address of row DX's first byte (CGA's two banks)
vb_rowaddr:
    mov di, dx
    shr di, 1
    mov ax, 80
    push dx
    mul di
    pop dx
    mov di, ax
    test dl, 1
    jz .e
    add di, 0x2000
.e:
    ret

vb_band:
    cmp byte [vb_banddone], 0       ; the openings draw the whole screen
    jne .live
    ret
.live:
    mov al, [vb_dirty]
    or al, al
    jnz .go
    ret
.go:
    mov byte [vb_dirty], 0
    test al, 1
    jz .p
    push ax
    call vb_draw_menu
    pop ax
.p:
    test al, 2
    jz .m
    push ax
    call vb_draw_party
    pop ax
.m:
    test al, 4
    jz .out
    call vb_draw_msg
.out:
    ret

; vb_draw_menu - the member's commands, the cursor's inverse
vb_draw_menu:
    xor bp, bp                      ; the line
.l:
    push ds
    pop es
    mov di, vb_lbuf
    mov cx, 14
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov byte [vb_tc], 3             ; white on black
    cmp byte [vb_state], GS_MENU
    jne .put
    call vb_mrec
    mov ax, bp
    mov ah, 12
    mul ah
    add si, ax
    add si, VM_CMDS
    cmp byte [si+VK_CLIP], 0xFF
    je .put
    push si
    mov di, vb_lbuf+1
    mov cx, 6
    call vb_strncpy
    mov byte [di], ' '
    pop si
    mov ax, [si+VK_MP]
    or ax, ax
    jz .nomp
    mov di, vb_lbuf+9
    call vb_utoa
    mov word [di], 'MP'
.nomp:
    mov byte [vb_lbuf+14], 0
    mov ax, bp
    cmp al, [vb_cursor]
    jne .put
    mov byte [vb_tc], 0x30          ; the cursor: black on white
.put:
    mov ax, bp
    mov cl, 10
    mul cl
    add ax, ROW_MENU
    mov dx, ax
    mov cx, COL_MENU
    mov si, vb_lbuf
    mov bh, [vb_tc]
    call vb_text
    inc bp
    cmp bp, 4
    jb .l
    ret

; vb_draw_party - the enemy's name, then each member: name, HP/max, MP. The
; member whose turn it is is drawn inverse: the selection is the band's
vb_draw_party:
    mov si, vb_meta+VB_SCRIPT+VS_ENAME
    mov di, vb_lbuf
    mov cx, 10
    call vb_strncpy
    mov dx, ROW_MENU
    mov cx, COL_PARTY
    mov si, vb_lbuf
    mov bh, 2                       ; red
    call vb_text
    mov si, vb_s_head
    mov dx, ROW_MENU + 10
    mov cx, COL_PARTY
    mov bh, 1                       ; cyan
    call vb_text
    xor bp, bp
.m:
    push ds
    pop es
    mov di, vb_lbuf                 ; 22 cells: name 6, HP 4/4, MP 4
    mov cx, 22
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov si, bp
    mov cl, 6
    shl si, cl
    add si, vb_meta+VB_SCRIPT+VS_PARTY
    push si
    mov di, vb_lbuf
    mov cx, 6
    call vb_strncpy
    mov byte [di], ' '
    pop si
    mov bx, bp
    shl bx, 1
    mov ax, [vb_hp+bx]
    mov di, vb_lbuf+7
    push si
    call vb_rjust4
    pop si
    mov byte [vb_lbuf+11], '/'
    mov ax, [si+VM_HP]
    mov di, vb_lbuf+12
    call vb_utoa                    ; (the most, left-justified)
    mov byte [di], ' '

    mov bx, bp
    shl bx, 1
    mov ax, [vb_mp+bx]
    mov di, vb_lbuf+18
    call vb_rjust4
    mov byte [vb_lbuf+22], 0
    mov byte [vb_tc], 3
    mov bx, bp
    shl bx, 1
    cmp word [vb_hp+bx], 0
    jne .up
    mov byte [vb_tc], 2             ; down: red
    jmp short .put
.up:
    mov ax, bp
    cmp al, [vb_turn]
    jne .put
    cmp byte [vb_state], GS_MENU
    jne .put
    mov byte [vb_tc], 0x30
.put:
    mov ax, bp
    mov cl, 10
    mul cl
    add ax, ROW_MENU + 20
    mov dx, ax
    mov cx, COL_PARTY
    mov si, vb_lbuf
    mov bh, [vb_tc]
    call vb_text
    inc bp
    cmp bp, 2
    jb .m
    ret

; vb_rjust4 - AX right-justified in four cells at DS:DI
vb_rjust4:
    push di
    mov di, vb_nbuf
    call vb_utoa
    mov cx, di
    sub cx, vb_nbuf                 ; its digits
    pop di
    mov ax, 4
    sub ax, cx
    jbe .cp
    add di, ax
.cp:
    mov si, vb_nbuf
.c:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp .c
.out:
    ret

vb_draw_msg:
    push ds
    pop es
    mov di, vb_lbuf
    mov cx, 38
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov si, [vb_msg]
    mov di, vb_lbuf
.c:
    lodsb
    or al, al
    jz .d
    cmp di, vb_lbuf+38
    jae .d
    stosb
    jmp .c
.d:
    mov dx, ROW_MSG
    mov cx, 1
    mov si, vb_lbuf
    mov bh, 3
    call vb_text
    ret

; vb_text - DS:SI (NUL) at cell column CX, pixel row DX; BH = colours:
; the low nibble the ink (1..3), the high the ground (0..3). Each cell is
; its 8 rows of 2 bytes, ground and glyph in one store (6.1's rule)
vb_text:
    push bp
    mov [vb_tcol], cx
    mov [vb_trow], dx
    ; the expansion: a nibble of glyph bits -> a byte of four pixels
    mov al, bh
    and al, 3
    mov [vb_tink], al
    mov al, bh
    mov cl, 4
    shr al, cl
    and al, 3
    mov [vb_tgnd], al
    xor bx, bx
.x:
    mov cx, 4
    xor ah, ah
    mov dl, bl
.x1:
    shl ah, 1
    shl ah, 1
    test dl, 8
    jz .g
    or ah, [vb_tink]
    jmp short .x2
.g:
    or ah, [vb_tgnd]
.x2:
    shl dl, 1
    loop .x1
    mov [vb_texp+bx], ah
    inc bx
    cmp bx, 16
    jb .x
    mov es, [vb_vseg]
.ch:
    lodsb
    or al, al
    jz .out
    cmp [vb_tcol], word 40
    jae .out
    sub al, 32
    jae .ok
    xor al, al
.ok:
    cmp al, 94
    jbe .ok2
    xor al, al
.ok2:
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, vb_font
    mov bp, ax                      ; the glyph's 8 rows
    mov dx, [vb_trow]
    mov cx, 8
.r:
    push cx
    call vb_rowaddr
    mov ax, [vb_tcol]
    shl ax, 1
    add di, ax
    mov al, [ds:bp]
    mov ah, al
    mov cl, 4
    shr al, cl                      ; the left four pixels' bits
    and ah, 0x0F                    ; ...and the right four's
    xor bh, bh
    mov bl, al
    mov al, [vb_texp+bx]
    mov bl, ah
    mov ah, [vb_texp+bx]
    stosw
    inc bp
    inc dx
    pop cx
    loop .r
    inc word [vb_tcol]
    jmp .ch
.out:
    pop bp
    ret

; =============================================================================
; text
; =============================================================================
vb_tpl:
    dw 40, 40, 320, 72
    dw vb_title, vb_paint, vb_onkey, vb_onclick
vb_title:     db 'Video Battle', 0
vb_name:      db 'BATTLE.VBT', 0
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
vb_s_begin:   db 'The IRON BEETLE attacks!', 0
vb_s_nomp:    db 'Not enough MP', 0
vb_s_damage:  db ' damage', 0
vb_s_healed:  db 'The party heals ', 0
vb_s_hp:      db ' HP', 0
vb_s_hits:    db ' hits ', 0
vb_s_victory: db 'VICTORY!   Esc to leave', 0
vb_s_defeat:  db 'The party has fallen.   Esc to leave', 0
vb_s_head:    db '        HP          MP', 0

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
VB_VAR vb_rseg, 2
VB_VAR vb_rcap, 2
VB_VAR vb_loaded, 1
VB_VAR vb_vres, 2
VB_VAR vb_sfxseg, 2
VB_VAR vb_mring, 2
VB_VAR vb_mfill, 2
VB_VAR vb_mtake, 2
VB_VAR vb_mrd, 4
VB_VAR vb_munder, 2
VB_VAR vb_mdry, 1
VB_VAR vb_ui, 1
VB_VAR vb_mres, 1
VB_VAR vb_prings, 2
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
VB_VAR vb_actor, 1
VB_VAR vb_target, 1
VB_VAR vb_tnext, 1
VB_VAR vb_dirty, 1
VB_VAR vb_acmd, 2
VB_VAR vb_ehp, 2
VB_VAR vb_hp, 4
VB_VAR vb_mp, 4
VB_VAR vb_msg, 2
VB_VAR vb_mbuf, 48
VB_VAR vb_lbuf, 48
VB_VAR vb_nbuf, 8
VB_VAR vb_tcol, 2
VB_VAR vb_trow, 2
VB_VAR vb_tink, 1
VB_VAR vb_tc, 1
VB_VAR vb_tgnd, 1
VB_VAR vb_texp, 16
    OS88_BSS VB_BSS
    OS88_IMAGE_END
