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
VH_VER      equ 4
VH_PAL      equ 8               ; CGA4 palette byte (98.1.3.3)
VH_ROWS     equ 9               ; the canvas's rows
VH_FPS      equ 10
VH_RATE     equ 12
VH_SPF      equ 14              ; samples a frame
VH_NCLIPS   equ 16
VH_NIDLE    equ 18
VH_NANCH    equ 20
VH_NSFX     equ 22              ; the menu's effects
VH_UI       equ 24              ; three bytes: the tick, ok and deny effects
VH_VRES     equ 30              ; dword offset, word length
VH_MUSIC    equ 36              ; dword offset (the music region)
VH_MLEN     equ 44              ; dword
VH_IDLETAB  equ 50              ; word: the idle table's offset in the block
VH_SFX      equ 56              ; dword offset, word length: the MENU's
VH_ANCH     equ 64              ; 16 words
VH_SFXTAB   equ 96              ; 16 x (dword offset, word length)
VB_CTAB     equ 512             ; the clip table, 48 bytes a clip
VC_OFF      equ 0               ; dword
VC_LEN      equ 4               ; dword (< 60 KB)
VC_NREC     equ 8
VC_FLAGS    equ 10
VC_EXIT     equ 12
VC_ENT      equ 14              ; the entry table's offset in the block
VC_CUE0     equ 16              ; byte: a first effect (1) or none (0),
                                ; then 17 the second's
VC_CUEF0    equ 18              ; word frame, then 20 the second's
VC_RECAT    equ 22              ; the records' offset in the region: the
                                ; clip's own effects come first
VC_CUEO0    equ 32              ; the first effect's offset in the region,
VC_CUEL0    equ 34              ; ...its length, then 36 and 38 the second's
CF_JOINS    equ 1
CF_FULL     equ 2
CF_ENDS     equ 4
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
VB_PIECE    equ 4096            ; a read-ahead piece: a cluster multiple, and
                                ; under a 360 KB track (9 sectors), so a key
                                ; waits at most about a revolution for it
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
VB_START    equ 24576           ; a clip may START once this much is read
                                ; (or all of it): the rest arrives as it plays
VB_DIV      equ 2048            ; the hook's PIT divisor: 582.6 Hz
VB_SPP_LO   equ 0xECB0          ; 11,025 samples / 582.6 periods in 16.16:
VB_SPP_HI   equ 0x0012          ; 18.9246 (the clock with no card)

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
; ES:BX, in pieces of at most 32 KB, through our one cursor.
; out CF=1 on an error or a short file. Clobbers AX, BX, CX, DX
vb_read:
    push si
    push di
    push bp
    mov [vb_rseg], es
    mov [vb_rcap], cx
    mov word [vb_cur+0], 0          ; seek: zero the cursor, set the offset
    mov word [vb_cur+2], 0
    mov word [vb_cur+4], 0
    mov word [vb_cur+6], 0
    mov word [vb_cur+8], 0
    mov word [vb_cur+10], 0
    mov [vb_cur+FSEQ_OFF], ax
    mov [vb_cur+FSEQ_OFF+2], dx
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
    mov di, vb_cur
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

; vb_load - what a play needs before it starts, each once a session, in
; THREE claims - an owner may hold eight (MEM_OWNER_MAX, SPEC.md 50), and a
; claim a region was one too many: the RESIDENT claim, carved into the idle
; loop's block, the effects and the music's 32 KB pages; the card's ring
; (vb_snd_open); and the CLIP CACHE (vb_cache, 125.4). Then the intro, read
; whole. CF=1 and [vb_err] on failure
vb_load:
    mov word [vb_err], 0
    cmp byte [vb_loaded], 0
    je .first
    jmp .intro
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
    cmp word [vb_meta+VH_VER], 2
    jne .fail1
    mov ax, [vb_meta+VH_NCLIPS]
    or ax, ax
    jz .fail1
    cmp ax, VB_MAXCLIP
    ja .fail1
    mov ax, [vb_meta+VH_MLEN]       ; the music: at least a piece
    or ax, [vb_meta+VH_MLEN+2]
    jz .fail1
    cmp word [vb_meta+VH_SPF], 0
    jne .sized
.fail1:
    jmp .fail
.sized:
    ; the resident claim's three parts - the block, the menu's effects and
    ; the music's ring with its guard - each rounded to 4 KB, in paragraphs
    mov cx, [vb_meta+VH_VRES+4]
    call vb_round
    mov cl, 4
    shr ax, cl
    mov [vb_psfx], ax               ; the effects start after the block
    mov cx, [vb_meta+VH_SFX+4]
    call vb_round
    mov cl, 4
    shr ax, cl
    add ax, [vb_psfx]
    mov [vb_pmus], ax               ; ...and the music's ring after them
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
    jmp short .claim
.ring:
    add ax, (VB_MR + VB_MPIECE) / 16
.claim:
    call vb_p2kb
    mov word [vb_err], vb_s_nomem
    call OSAPI_MEM_CLAIM
    jc .fail1
    mov [vb_vres], dx
    mov ax, dx
    add ax, [vb_psfx]
    mov [vb_sfxseg], ax
    add dx, [vb_pmus]
    mov [vb_mring], dx
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
    jcxz .mdone
    call vb_round
    mov cx, ax
    mov es, [vb_sfxseg]
    xor bx, bx
    mov ax, [vb_meta+VH_SFX]
    mov dx, [vb_meta+VH_SFX+2]
    call vb_read
    jc .fail1
.mdone:
    call vb_cache                   ; the clip cache's claim, and its carving
    mov word [vb_err], vb_s_nomem
    jc .fail
    mov byte [vb_loaded], 1
    call vb_fontload
.intro:
    ; the intro (the first FULL clip), whole: it plays as the bracket opens
    mov byte [vb_intro], 0xFF
    xor bx, bx
.find:
    cmp bx, [vb_meta+VH_NCLIPS]
    jae .ok
    call vb_crec
    test word [si+VC_FLAGS], CF_FULL
    jnz .got
    inc bx
    jmp .find
.got:
    mov [vb_intro], bl
    mov byte [vb_ask], 1
    call vb_ensure                  ; read all of it now
    mov word [vb_err], vb_s_badread
    jc .fail
.ok:
    ; the music from its start, its ring filled: the intro plays over it
    xor ax, ax
    mov [vb_mfill], ax
    mov [vb_mtake], ax
    mov [vb_mrd], ax
    mov [vb_mrd+2], ax
    mov [vb_munder], ax
    cmp byte [vb_mres], 0
    je .mfill
    cmp byte [vb_mread], 0          ; RESIDENT: read once a session
    jne .mok
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
.mok:
    mov word [vb_err], 0
    clc
    ret
.mfill:
    call vb_mpiece
    mov word [vb_err], vb_s_badread
    jc .fail
    or al, al
    jnz .mfill
    mov word [vb_err], 0
    clc
    ret
.fail:
    stc
    ret

; vb_mpiece - one 4 KB piece of the music into its ring, when the ring has
; room for it (SPEC.md 125.4). The file is read at the music's own offset
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
    mov ax, [vb_mrd]
    mov dx, [vb_mrd+2]
    add ax, [vb_meta+VH_MUSIC]
    adc dx, [vb_meta+VH_MUSIC+2]
    mov es, [vb_mring]
    mov bx, [vb_mfill]
    and bx, VB_MR - 1
    push bx
    mov cx, VB_MPIECE
    call vb_read
    pop bx
    jc .out
    mov ax, [vb_meta+VH_MLEN]       ; what of it is music: the rest of it,
    mov dx, [vb_meta+VH_MLEN+2]     ; at most the piece
    sub ax, [vb_mrd]
    sbb dx, [vb_mrd+2]
    or dx, dx
    jnz .four
    cmp ax, VB_MPIECE
    jbe .v
.four:
    mov ax, VB_MPIECE
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

; vb_csize - AX = clip BX's room in paragraphs (its stream rounded to 4 KB).
; SI = its row; CX spent
vb_csize:
    call vb_crec
    mov cx, [si+VC_LEN]
    call vb_round
    mov cl, 4
    shr ax, cl
    ret

; vb_cache - the clip cache: ONE claim. Every clip in a place of its own
; when they all fit (the demo's case); otherwise as many slots, each the
; largest clip's size, as memory gives - eight at most, three at least (the
; clip playing, the one pending and the one under the cursor). CF=1 when
; not even three
vb_cache:
    xor bx, bx                      ; the total and the largest, paragraphs
    xor di, di
    mov word [vb_slotp], 0
.t:
    cmp bx, [vb_meta+VH_NCLIPS]
    jae .sum
    call vb_csize
    add di, ax
    jc .slots                       ; (past 1 MB: the slots, then)
    cmp ax, [vb_slotp]
    jbe .tn
    mov [vb_slotp], ax
.tn:
    inc bx
    jmp .t
.sum:
    mov ax, di
    call vb_p2kb
    call OSAPI_MEM_CLAIM
    jc .slots
    mov [vb_cbase], dx              ; EVERY CLIP IN ITS PLACE
    mov byte [vb_nslot], 0
    xor bx, bx
.place:
    cmp bx, [vb_meta+VH_NCLIPS]
    jae .ok
    mov di, bx
    shl di, 1
    mov [vb_cseg+di], dx
    mov word [vb_cload+di], 0
    push dx
    call vb_csize
    pop dx
    add dx, ax
    inc bx
    jmp .place
.ok:
    clc
    ret
.slots:
    mov byte [vb_nslot], 8          ; the most slots, then fewer
.sl:
    mov al, [vb_nslot]
    xor ah, ah
    mul word [vb_slotp]             ; DX:AX paragraphs
    or dx, dx
    jnz .less
    call vb_p2kb
    call OSAPI_MEM_CLAIM
    jnc .have
.less:
    dec byte [vb_nslot]
    cmp byte [vb_nslot], 3
    jae .sl
    mov byte [vb_nslot], 0
    stc
    ret
.have:
    mov [vb_cbase], dx
    xor bx, bx                      ; every slot empty, no clip placed
.e:
    mov byte [vb_slot+bx], 0xFF
    inc bx
    cmp bx, 8
    jb .e
    xor bx, bx
.z:
    mov word [vb_cseg+bx], 0
    mov word [vb_cload+bx], 0
    add bx, 2
    cmp bx, VB_MAXCLIP * 2
    jb .z
    clc
    ret

; vb_crec - SI = clip BX's row in the clip table. BX kept
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

; vb_ensure - clip BX read whole (the UI task, before the bracket).
; CF=1 when no memory
vb_ensure:
.go:
    call vb_crec
    mov ax, [vb_cload+bx]
    cmp ax, [si+VC_LEN]
    jae .ok
    push bx
    call vb_piece
    pop bx
    jc .out
    jmp .go
.ok:
    clc
.out:
    ret

; vb_piece - read the next piece of clip BX (the clip's number). CF=1: no
; room for it in the cache, or a read error. Only an ASKED clip ([vb_ask]
; set) may take a slot from another
vb_piece:
    push bp
    mov bp, bx
    shl bp, 1                       ; BP = the word index (DS: overrides -
    cmp word [ds:vb_cseg+bp], 0     ; BP names SS)
    jne .have
    call vb_slotfor                 ; slots only: a free one, or (asked) a
    jc .out                         ; victim's
.have:
    call vb_crec
    mov cx, [si+VC_LEN]             ; what is left, rounded, at most a piece
    call vb_round
    sub ax, [ds:vb_cload+bp]
    jbe .done
    cmp ax, VB_PIECE
    jbe .n
    mov ax, VB_PIECE
.n:
    mov cx, ax
    mov ax, [si+VC_OFF]
    mov dx, [si+VC_OFF+2]
    add ax, [ds:vb_cload+bp]
    adc dx, 0
    mov es, [ds:vb_cseg+bp]
    mov bx, [ds:vb_cload+bp]
    push cx
    call vb_read
    pop cx
    jc .out
    add [ds:vb_cload+bp], cx        ; published last: the hook reads it
.done:
    clc
.out:
    pop bp
    ret

; vb_slotfor - clip BX (BP = BX * 2) into a slot: a free one, or, when
; [vb_ask], the next whose clip is not playing, pending or under the cursor
; (round robin). CF=1 when there is none
vb_slotfor:
    cmp byte [vb_nslot], 0
    je .none                        ; (every clip has its place: not here)
    mov cl, [vb_nslot]
    xor ch, ch
    xor di, di
.free:
    cmp byte [vb_slot+di], 0xFF
    je .take
    inc di
    cmp di, cx
    jb .free
    cmp byte [vb_ask], 0            ; reading ahead unasked: no victim
    je .none
.v:                                 ; a victim, from the last one taken on
    mov di, [vb_snext]
    inc di
    mov ax, di                      ; (CX is the loop's count here)
    cmp al, [vb_nslot]
    jb .v1
    xor di, di
.v1:
    mov [vb_snext], di
    mov al, [vb_slot+di]
    cmp al, [vb_pend]
    je .vn
    cmp al, [vb_want]
    je .vn
    cmp byte [vb_mode], VM_CLIP
    jne .vq
    cmp al, [vb_cc]
    je .vn
.vq:
    mov si, vb_act                  ; nor one whose effect may still sound
    call .snd
    je .vn
    mov si, vb_q0
    call .snd
    je .vn
    mov si, vb_q1
    call .snd
    je .vn
.vok:
    xor ah, ah                      ; its clip forgets it
    push bx
    mov bx, ax
    shl bx, 1
    mov word [vb_cseg+bx], 0
    mov word [vb_cload+bx], 0
    pop bx
    jmp short .take
.vn:
    loop .v
.none:
    stc
    ret
.snd:                               ; ZF: [SI] is on and in clip AL
    cmp byte [si+QS_ON], 0
    je .sno
    cmp al, [si+QS_CLIP]
    ret
.sno:
    cmp al, 0xFF                    ; (never a clip: ZF clear)
    ret
.take:
    mov [vb_slot+di], bl
    mov ax, di                      ; its segment: base + slot * size
    mul word [vb_slotp]
    add ax, [vb_cbase]
    mov [ds:vb_cseg+bp], ax
    mov word [ds:vb_cload+bp], 0
    clc
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
    call vb_snd_open
    ; the intro plays first, from its first record
    mov bl, [vb_intro]
    cmp bl, 0xFF
    jne .intro
    mov word [vb_ifr], 0            ; no intro: the loop from frame 0's record
    mov byte [vb_mode], VM_IDLE     ; (record 0 takes the last to the first)
    dec word [vb_ifr]               ; ...so it starts at -1 + 1
    mov ax, [vb_meta+VH_NIDLE]
    add [vb_ifr], ax
    jmp short .go
.intro:
    xor bh, bh
    call vb_start_clip
.go:
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
; the clips, from the hook's side
; =============================================================================

; vb_start_clip - the hook plays clip BX from its first record (the intro,
; or after an entry record has drawn frame 0). Called with the hook not
; running: from the hook itself, or before [vb_ready]
vb_start_clip:
    mov [vb_cc], bl
    call vb_crec
    mov ax, [si+VC_NREC]
    mov [vb_cleft], ax
    mov ax, [si+VC_FLAGS]
    mov [vb_cflags], ax
    mov ax, [si+VC_EXIT]
    mov [vb_cexit], ax
    mov ax, [si+VC_RECAT]
    mov [vb_coff], ax
    shl bx, 1
    mov ax, [vb_cseg+bx]
    mov [vb_ccseg], ax
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
    mov byte [vb_hn], 2             ; at most two frames a call
.f:
    mov ax, [vb_clock]
    mov dx, [vb_clock+2]
    sub ax, [vb_due]
    sbb dx, [vb_due+2]
    jb .fed                         ; not due yet
    or dx, dx                       ; more than ~6 frames behind: forgiven,
    jnz .forgive                    ; the picture runs slow, never wrong
    cmp ax, 4410
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
    jc .fed                         ; a clip not read that far: wait
    mov ax, [vb_meta+VH_SPF]
    add [vb_due], ax
    adc word [vb_due+2], 0
    dec byte [vb_hn]
    jnz .f
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
    je .idle
    clc
    ret
.clip:
    mov bl, [vb_cc]
    xor bh, bh
    shl bx, 1
    mov dx, [vb_cload+bx]
    mov si, [vb_coff]
    mov ax, si
    add ax, 2
    jc .wait
    cmp ax, dx
    ja .wait
    mov es, [vb_ccseg]
    mov cx, [es:si]                 ; the record's length
    mov ax, si
    add ax, cx
    jc .wait
    cmp ax, dx
    ja .wait
    mov [vb_coff], ax
    mov dx, es
    call vb_decode
    inc word [vb_cfr]
    dec word [vb_cleft]
    jnz .ok
    mov al, [vb_cc]                 ; the clip is over
    mov [vb_cdone], al
    test word [vb_cflags], CF_JOINS
    jz .hold
    mov ax, [vb_cexit]              ; it seamed onto idle frame EXIT
    mov [vb_ifr], ax
    mov byte [vb_mode], VM_IDLE
    clc
    ret
.hold:
    mov byte [vb_mode], VM_HOLD
.ok:
    clc
    ret
.wait:
    stc
    ret
.idle:
    ; an action waiting: its anchor is chosen once it is ready, and the
    ; entry record is drawn in place of the frame after the anchor
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
    ; the entry: clip BL's entry table, anchor [vb_pa]
    xor bh, bh
    call vb_crec
    mov si, [si+VC_ENT]
    mov ax, [vb_pa]
    shl ax, 1
    add si, ax
    mov es, [vb_vres]
    mov si, [es:si]
    mov dx, es
    call vb_decode
    xor bh, bh
    mov bl, [vb_pend]
    call vb_start_clip
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

; vb_anchor - clip [vb_pend] waits for enough of itself to be read, then
; takes the next anchor at least two frames on, and its sound effects are
; booked at the samples their frames will be shown at
vb_anchor:
    xor bh, bh
    mov bl, [vb_pend]
    call vb_crec
    mov cx, [si+VC_RECAT]           ; its effects and VB_START of its
    add cx, VB_START                ; records, or all of it
    jc .all
    cmp cx, [si+VC_LEN]
    jbe .c
.all:
    mov cx, [si+VC_LEN]
.c:
    shl bx, 1
    cmp [vb_cload+bx], cx
    jb .out                         ; not yet
    ; the next anchor: d = (k - ifr) mod N, the smallest d >= 2
    mov word [vb_bestd], 0xFFFF
    xor di, di
.a:
    cmp di, [vb_meta+VH_NANCH]
    jae .picked
    mov bx, di
    shl bx, 1
    mov ax, [vb_meta+VH_ANCH+bx]
    sub ax, [vb_ifr]
    jae .pos
    add ax, [vb_meta+VH_NIDLE]
.pos:
    cmp ax, 2
    jae .d
    add ax, [vb_meta+VH_NIDLE]
.d:
    cmp ax, [vb_bestd]
    jae .an
    mov [vb_bestd], ax
    mov [vb_pa], di
.an:
    inc di
    jmp .a
.picked:
    cmp word [vb_bestd], 0xFFFF
    je .out
    mov bx, [vb_pa]
    shl bx, 1
    mov ax, [vb_meta+VH_ANCH+bx]
    mov [vb_pk], ax
    ; the entry frame is due at [vb_due] + d * spf (d frames from the next)
    mov ax, [vb_bestd]
    mul word [vb_meta+VH_SPF]
    add ax, [vb_due]
    adc dx, [vb_due+2]
    mov [vb_estart], ax
    mov [vb_estart+2], dx
    xor bh, bh
    mov bl, [vb_pend]
    call vb_crec
    shl bx, 1
    mov di, [vb_cseg+bx]            ; the effects are the clip's own
    cmp byte [si+VC_CUE0], 0
    je .q1
    push si
    mov bx, [si+VC_CUEO0]
    mov cx, [si+VC_CUEL0]
    mov ax, [si+VC_CUEF0]
    call .one
    pop si
.q1:
    cmp byte [si+VC_CUE0+1], 0
    je .out
    mov bx, [si+VC_CUEO0+4]
    mov cx, [si+VC_CUEL0+4]
    mov ax, [si+VC_CUEF0+2]
.one:                               ; an effect at the clip's frame AX
    mul word [vb_meta+VH_SPF]
    add ax, [vb_estart]
    adc dx, [vb_estart+2]
    mov si, ax
    mov al, [vb_pend]
    jmp vb_cue
.out:
    ret

; vb_cue - book an effect: DI:BX its bytes, CX its length, DX:SI the sample
; it starts at, AL the clip it is in (a slot holding it is not given up while
; it may sound). Into the queue's first free entry: two, and the second
; replaces the first when it starts
vb_cue:
    push di
    mov di, vb_q0
    cmp byte [di+QS_ON], 0
    je .put
    mov di, vb_q1
.put:
    mov [di+QS_AT], si
    mov [di+QS_AT+2], dx
    mov [di+QS_OFF], bx
    mov [di+QS_LEN], cx
    mov [di+QS_CLIP], al
    mov byte [di+QS_ON], 1
    pop ax
    mov [di+QS_SEG], ax
    mov di, ax                      ; (DI back: the second cue's segment)
    ret

; vb_decode - one record at DX:SI (its length word first) onto the screen
vb_decode:
    push ds
    mov es, [vb_vseg]
    mov ds, dx
    lodsw                           ; (the length; the lists follow)
    xor bp, bp
    call vd_native
    pop ds
    ret

; vb_feed - the card's ring topped up to VB_LEAD ahead of what it consumed:
; the music, looping, and the effect that is due mixed into it
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

; vb_uisound - menu sound AL - 1 (resident), NOW: it replaces the effect
; playing and is mixed straight into what the ring holds from a block past
; the card's last report up to the write position, so it is heard about a
; block from now rather than the ring's whole lead (125.3). The rest of it
; is mixed as the ring is written, as any effect is
vb_uisound:
    dec al
    xor ah, ah
    mov bx, ax                      ; its row: 6 bytes
    shl bx, 1
    add bx, ax
    shl bx, 1
    mov ax, [vb_meta+VH_SFXTAB+bx]
    mov [vb_act+QS_OFF], ax
    mov ax, [vb_meta+VH_SFXTAB+4+bx]
    mov [vb_act+QS_LEN], ax
    mov ax, [vb_sfxseg]
    mov [vb_act+QS_SEG], ax
    mov byte [vb_act+QS_CLIP], 0xFE
    mov byte [vb_act+QS_ON], 1
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
    mov ax, [vb_act+QS_LEN]
    cmp cx, ax
    jbe .n2
    mov cx, ax
.n2:
    jcxz .done
    sub [vb_act+QS_LEN], cx
    mov si, [vb_act+QS_OFF]
    add [vb_act+QS_OFF], cx
    mov dx, cx
    mov es, [vb_ring]
    push ds
    mov ds, [vb_act+QS_SEG]
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
    cmp word [vb_act+QS_LEN], 0
    jne .r
    mov byte [vb_act+QS_ON], 0
.r:
    ret

; vb_chunk - CX samples (> 0) at the write position, cut to the ring's
; wrap, the music's ring and the next effect's start
vb_chunk:
    mov di, [vb_wpos]
    and di, VB_RL - 1
    mov ax, VB_RL
    sub ax, di
    cmp cx, ax
    jbe .w
    mov cx, ax
.w:
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
    ; the music, out of its ring: what it holds, to its wrap
    mov si, [vb_mtake]
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
    mov byte [vb_mdry], 1           ; still running and the effect mixed
    jmp short .m
.mh:
    mov byte [vb_mdry], 0
    cmp cx, ax
    jbe .m
    mov cx, ax
.m:
    ; the queued effect starts at q0.at: a chunk never runs across it
    cmp byte [vb_q0+QS_ON], 0
    je .copy
    mov ax, [vb_q0+QS_AT]
    mov dx, [vb_q0+QS_AT+2]
    sub ax, [vb_wpos]
    sbb dx, [vb_wpos+2]
    jb .startq                      ; due (or past: started late, skipped in)
    jnz .copy                       ; far off
    or ax, ax
    jz .startq
    cmp cx, ax
    jbe .copy
    mov cx, ax
    jmp short .copy
.startq:
    call vb_qstart
.copy:
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
    ; the effect, added over the music (both kept in range by the writer:
    ; music 64..191, an effect -64..63, so the sum never wraps)
    cmp byte [vb_act+QS_ON], 0
    je .noeff
    mov ax, [vb_act+QS_LEN]
    cmp cx, ax
    jbe .e
    mov cx, ax
.e:
    sub [vb_act+QS_LEN], cx
    jnz .e2
    mov byte [vb_act+QS_ON], 0
.e2:
    mov si, [vb_act+QS_OFF]
    add [vb_act+QS_OFF], cx
    push ds
    mov ds, [vb_act+QS_SEG]
.mix:
    lodsb
    add [es:di], al
    inc di
    loop .mix
    pop ds
.noeff:
    pop cx
    add [vb_wpos], cx
    adc word [vb_wpos+2], 0
    cmp byte [vb_mdry], 0
    jne .tot
    add [vb_mtake], cx              ; the music's ring, given back
    cmp byte [vb_mres], 0
    je .tot
    mov ax, [vb_mtake]              ; (resident: from the top at its end)
    cmp ax, [vb_meta+VH_MLEN]
    jb .tot
    mov word [vb_mtake], 0
.tot:
    mov es, [vb_ring]
    mov ax, [vb_wpos]
    mov [es:VB_RL+SND_EXT_TOTAL], ax
    ret

; vb_qstart - the queue's first effect becomes the one that plays, skipped
; in by however far the write position is past its start; the second moves
; up. Keeps BX, CX, DI, SI
vb_qstart:
    push si
    mov ax, [vb_wpos]
    sub ax, [vb_q0+QS_AT]           ; how late (< 64 KB, or it is gone)
    mov dx, [vb_q0+QS_LEN]
    cmp ax, dx
    jae .gone
    sub dx, ax
    mov [vb_act+QS_LEN], dx
    add ax, [vb_q0+QS_OFF]
    mov [vb_act+QS_OFF], ax
    mov ax, [vb_q0+QS_SEG]
    mov [vb_act+QS_SEG], ax
    mov al, [vb_q0+QS_CLIP]
    mov [vb_act+QS_CLIP], al
    mov byte [vb_act+QS_ON], 1
    jmp short .up
.gone:
    mov byte [vb_act+QS_ON], 0
.up:
    mov si, vb_q1                   ; q0 = q1, q1 = none
    mov ax, [si+QS_AT]
    mov [vb_q0+QS_AT], ax
    mov ax, [si+QS_AT+2]
    mov [vb_q0+QS_AT+2], ax
    mov ax, [si+QS_OFF]
    mov [vb_q0+QS_OFF], ax
    mov ax, [si+QS_LEN]
    mov [vb_q0+QS_LEN], ax
    mov ax, [si+QS_SEG]
    mov [vb_q0+QS_SEG], ax
    mov al, [si+QS_CLIP]
    mov [vb_q0+QS_CLIP], al
    mov al, [si+QS_ON]
    mov [vb_q0+QS_ON], al
    mov byte [si+QS_ON], 0
    pop si
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
    mov [vb_ui], al
    mov [vb_q0+QS_ON], al
    mov [vb_q1+QS_ON], al
    mov [vb_act+QS_ON], al
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

; vb_reader - one piece of the clip most worth having: the pending one, then
; the one under the cursor, then the enemy's, then (memory allowing) any
; other. A new cursor position just changes which: what was read is kept
vb_reader:
    cmp byte [vb_mres], 0           ; (resident music: the clips have it all)
    jne .clips
    mov ax, [vb_mfill]              ; THE MUSIC FIRST while its ring is under
    sub ax, [vb_mtake]              ; half: 1.5 s of it is the margin a
    cmp ax, VB_MR / 2               ; clip's read may spend
    jae .clips
    jmp vb_mpiece
.clips:
    mov byte [vb_ask], 1            ; the asked: they may take a slot
    mov al, [vb_pend]               ; (the action chosen is read at once:
    cmp al, 0xFF                    ; its start waits on it)
    jne .try
    call OSAPI_GET_TICKS            ; A KEY A MOMENT AGO: no other clip's read
    sub ax, [vb_ktick]              ; yet. The next key would wait behind it
    cmp ax, VB_KQUIET               ; in the BIOS buffer - Down then Enter is
    jae .reads                      ; two keys a fifth of a second apart -
    ret                             ; and a read is a revolution or a copy
.reads:
    mov al, [vb_want]
    cmp al, 0xFF
    je .bg
    call .need
    jc .try
.bg:
    mov al, [vb_meta+VB_SCRIPT+VS_ECLIP]
    call .need
    jc .try
    mov byte [vb_ask], 0            ; the rest only into room that is free
    xor al, al
.scan:
    cmp al, [vb_meta+VH_NCLIPS]
    jae .top
    call .need
    jc .try
    inc al
    jmp .scan
.try:
    xor ah, ah
    mov bx, ax
    call vb_piece
    ret
.top:
    jmp vb_mpiece                   ; nothing else to read: the music's ring
.need:                              ; CF=1: clip AL is not all read yet
    push ax
    push si
    xor ah, ah
    mov bx, ax
    call vb_crec
    shl bx, 1
    mov ax, [vb_cload+bx]
    cmp ax, [si+VC_LEN]
    pop si
    pop ax
    jb .yes
    clc
    ret
.yes:
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
    cmp byte [vb_mode], VM_IDLE     ; the intro has seamed onto the loop:
    jne .r                          ; the band's rows are black, and ours
    mov byte [vb_cdone], 0xFF
    call vb_band_frame
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
    cmp byte [vb_state], GS_INTRO   ; the intro draws the whole screen
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
QS_AT   equ 0                   ; dword: the sample it starts at
QS_OFF  equ 4                   ; its offset in the effects' claim
QS_LEN  equ 6                   ; bytes left
QS_ON   equ 8
QS_SEG  equ 10                  ; the segment its bytes are in
QS_CLIP equ 12                  ; the clip it is in (FEh a menu sound)
QS_SIZE equ 14

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
VB_VAR vb_rseg, 2
VB_VAR vb_rcap, 2
VB_VAR vb_lo, 2
VB_VAR vb_hi, 2
VB_VAR vb_loaded, 1
VB_VAR vb_intro, 1
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
VB_VAR vb_cseg, VB_MAXCLIP*2
VB_VAR vb_cload, VB_MAXCLIP*2
VB_VAR vb_ask, 1
VB_VAR vb_psfx, 2
VB_VAR vb_pmus, 2
VB_VAR vb_cbase, 2
VB_VAR vb_nslot, 1
VB_VAR vb_slotp, 2
VB_VAR vb_slot, 8
VB_VAR vb_snext, 2
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
VB_VAR vb_pa, 2
VB_VAR vb_bestd, 2
VB_VAR vb_estart, 4
VB_VAR vb_ccseg, 2
VB_VAR vb_coff, 2
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
VB_VAR vb_q0, QS_SIZE
VB_VAR vb_q1, QS_SIZE
VB_VAR vb_act, QS_SIZE
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
