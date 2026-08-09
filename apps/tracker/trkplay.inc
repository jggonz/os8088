; =============================================================================
; os8088 - apps/tracker/trkplay.inc
;
; The Tracker's replayer third (SPEC.md 45): ProTracker MOD loader/validator,
; the 4-channel replayer, and the 8-bit mono mixer. Prefix mp_. Included by
; tracker.asm AFTER the TRKB/TRKW/TRKBUF bss macros; all state is package
; bss declared through them at the end of this file.
;
; Register contract (pinned): near procs, DS = ES = package segment on
; entry; DS and SS are always preserved, every other register may be
; clobbered unless a proc documents an output. Every proc except mp_insname
; (which returns ES:SI into the blob by contract) leaves ES = DS.
;
; THE BLOB IS HOSTILE (SPEC.md 18's rule, applied to a file a stranger
; wrote): mp_load validates per the 13-point checklist in the research
; before anything is trusted - magic, song length, the pattern count
; derived from ALL 128 order bytes bounded against the real 32-bit file
; size, per-sample extents clamped to the bytes actually present, loop
; fix-ups, volume clamps. At play time every period is clamped to
; [113..856] BEFORE the DIV (the clamp IS the #DE guard) and every sample
; fetch is bounded by a precomputed limit.
;
; Addressing a blob that exceeds 64KB (BEVERLY.MOD is 116,085 bytes) on an
; 8086: nothing ever offsets more than 64KB from one base. The header is
; within blobseg's first 1084 bytes; pattern p lives at segment
; blobseg + 67 + 64*p, offset 12 (1084 + 1024*p = ((67 + 64*p) << 4) + 12);
; each sample gets a normalized base seg = blobseg + (start >> 4),
; off = start & 15, with its play length capped at MP_MAXSMP bytes so
; off + position (+ one mix step) can never wrap a 16-bit offset.
;
; Timing (research-ft2-reference): tick rate = BPM*2/5 Hz, speed = ticks
; per row; mp_gen renders [mp_spt] = mixrate*5/(2*BPM) samples per tick.
; The tick model counts UP: tick 0 reads the row and runs tick-0 effects,
; ticks 1..speed-1 run the per-tick effects, the row advances when the
; count wraps. Mixing is per-channel accumulate into a 16-bit chunk
; buffer through the 65x256 volume table (built at load), then one
; conversion pass: out = 128 + (sum >> 2).
; =============================================================================

MP_MAXSMP  equ 60000            ; per-sample play-length cap, bytes: keeps
                                ; off + pos + step*chunk < 65536 in the mixer
                                ; even at a 4000 Hz mix rate (step int <= 7)
MP_CHUNK   equ 512              ; mix chunk cap, samples (mp_mixbuf = 2x)

; --- the position stamps (SPEC.md 45.15): what the LISTENER is hearing ---------
; Every mp_* readout above is the position being MIXED, and the mixer runs a
; whole ring buffer ahead of the DAC - up to 16KB, which at XT mode's 5,500 Hz
; is nearly three seconds. So the row a stamp names is recorded against the
; STREAM BYTE its first sample lands on, and the display asks which stamp the
; card has reached rather than which row the mixer is on.
MP_ST_N    equ 64               ; stamps: 64 rows of history, ~1.19*BPM/speed
MP_ST_MASK equ MP_ST_N - 1      ; rows fit in a full ring at the XT rate, so
                                ; this covers everything down to speed 3 at
                                ; BPM 125. Faster than that the ring laps and
                                ; the display degrades TOWARDS the mixer -
                                ; never behind the music, only less late
MP_ST_SIZE equ 16               ; ...and a power of two, so the index is shifts
MP_SP_POS  equ 0                ; w: stream byte this row's first sample is at
MP_SP_SPOS equ 2                ; b: order position
MP_SP_PAT  equ 3                ; b: pattern
MP_SP_ROW  equ 4                ; b: row
MP_SP_BPM  equ 5                ; b: tempo, so a tempo change is heard and
MP_SP_SPD  equ 6                ; b: ...ticks per row, shown at the same moment
MP_SP_VOL  equ 8                ; 4b: the channel volumes, so the VU needles
                                ; move with the note rather than ahead of it
MP_SP_HIT  equ 12               ; 4b: which channels STRUCK a note in this row
                                ; (SPEC.md 45.12.1). The volumes above cannot
                                ; say it - a MOD's volume column changes only
                                ; when a volume command does, so a channel
                                ; holding one value is indistinguishable from
                                ; a silent one. Fits because MP_ST_SIZE is 16
                                ; and VOL ends at 11

; --- channel record (bss, MP_CHSZ bytes each, 4 of them) -----------------------
MP_PER    equ 0                 ; w: real period (persistent)
MP_OPER   equ 2                 ; w: output period (vibrato/arp write this)
MP_TARG   equ 4                 ; w: 3xx tone-porta target
MP_SEG    equ 6                 ; w: playing sample's base segment
MP_OFF    equ 8                 ; w: ...base offset (0..15)
MP_PLEN   equ 10                ; w: play length, bytes (loop end / one-shot end)
MP_LLEN   equ 12                ; w: loop length, bytes (0 = one-shot)
MP_POSI   equ 14                ; w: position = MP_OFF + byte index (int part)
MP_POSF   equ 16                ; w: position fraction (16.16 low word)
MP_STEPI  equ 18                ; w: step int
MP_STEPF  equ 20                ; w: step fraction
MP_VOL    equ 22                ; b: real volume 0..64
MP_OVOL   equ 23                ; b: output volume (tremolo writes this)
MP_ACT    equ 24                ; b: 1 = sample playing
MP_SMP    equ 25                ; b: current instrument 1..31 (0 = none)
MP_EFF    equ 26                ; b: this row's effect
MP_PARM   equ 27                ; b: ...and param
MP_VIBP   equ 28                ; b: vibrato table position (signed byte)
MP_VIBC   equ 29                ; b: vibrato speed<<4 | depth (sticky nibbles)
MP_TREP   equ 30                ; b: tremolo position
MP_TREC   equ 31                ; b: tremolo speed<<4 | depth
MP_PSPD   equ 32                ; b: 3xx porta speed (sticky)
MP_OFFM   equ 33                ; b: 9xx offset memory
MP_LROW   equ 34                ; b: E6x loop start row
MP_LCNT   equ 35                ; b: E6x loop counter
MP_DPER   equ 36                ; w: EDx delayed cell's period
MP_DSMP   equ 38                ; b: ...and sample
MP_CHSZ   equ 40

; --- sample table entry (bss, 12 bytes x 31) -----------------------------------
MS_SEG    equ 0                 ; w: normalized base segment
MS_OFF    equ 2                 ; w: base offset 0..15
MS_PLEN   equ 4                 ; w: play length, bytes (0 = unplayable)
MS_LLEN   equ 6                 ; w: loop length, bytes (0 = one-shot)
MS_LSTART equ 8                 ; w: loop start, bytes (kept for reference)
MS_VOL    equ 10                ; b: header volume, clamped 0..64
MS_FINE   equ 11                ; b: finetune nibble (stored, ignored in v1)
MS_SZ     equ 12

; =============================================================================
; mp_init - once, from the entry proc. The loader zeroed the bss; only the
; nonzero defaults are set here.
; =============================================================================
mp_init:
    mov byte [mp_speed], 6
    mov byte [mp_bpm], 125
    mov word [mp_mixrate], 11000
    ret

; =============================================================================
; mp_load - parse + validate the blob at [mp_blobseg] (32-bit length in
; [mp_bloblen_hi]:[mp_bloblen_lo]), build the sample table and the volume
; tables, set mp_loaded = 1.
; out: CF=0 loaded; CF=1 AX = offset of a NUL error string. ES = DS.
; =============================================================================
mp_load:
    mov byte [mp_loaded], 0
    ; -- 1: L >= 1084 ------------------------------------------------------
    cmp word [mp_bloblen_hi], 0
    jne .lenok
    cmp word [mp_bloblen_lo], 1084
    jae .lenok
    jmp .e_notmod
.lenok:
    mov es, [mp_blobseg]
    ; -- 3: magic at 1080 in {M.K., M!K!, 4CHN, FLT4} ------------------------
    mov ax, [es:1080]
    mov dx, [es:1082]
    cmp ax, 0x2E4D              ; 'M.'
    jne .m2
    cmp dx, 0x2E4B              ; 'K.'
    je .magic
.m2:
    cmp ax, 0x214D              ; 'M!'
    jne .m3
    cmp dx, 0x214B              ; 'K!'
    je .magic
.m3:
    cmp ax, 0x4334              ; '4C'
    jne .m4
    cmp dx, 0x4E48              ; 'HN'
    je .magic
.m4:
    cmp ax, 0x4C46              ; 'FL'
    jne .e_notmod
    cmp dx, 0x3454              ; 'T4'
    je .magic
.e_notmod:
    push ds
    pop es
    mov ax, mpe_notmod
    stc
    ret
.magic:
    ; -- title: 20 hostile bytes -> mp_title, printable, NUL-terminated ------
    xor si, si
    mov di, mp_title
    mov cx, 20
.tl:
    mov al, [es:si]
    cmp al, 32
    jb .tdone                   ; NUL (or any control byte) ends the title
    cmp al, 126
    jbe .tkeep
    mov al, '.'
.tkeep:
    mov [di], al
    inc si
    inc di
    loop .tl
.tdone:
    mov byte [di], 0
    ; -- 4/5: song length 1..128, restart < songlen else 0 -------------------
    mov al, [es:950]
    or al, al
    jz .e_song
    cmp al, 128
    jbe .slok
.e_song:
    push ds
    pop es
    mov ax, mpe_song
    stc
    ret
.slok:
    mov [mp_songlen], al
    mov ah, [es:951]
    cmp ah, al
    jb .rsok
    xor ah, ah
.rsok:
    mov [mp_restart], ah
    ; -- 6: copy the order table, P = max over ALL 128 entries + 1 -----------
    mov si, 952
    mov di, mp_orders
    mov cx, 128
    xor ah, ah                  ; AH = running max
.ol:
    mov al, [es:si]
    mov [di], al
    cmp al, ah
    jbe .onx
    mov ah, al
.onx:
    inc si
    inc di
    loop .ol
    mov al, ah
    xor ah, ah
    inc ax                      ; AX = P (1..256)
    mov [mp_t1], ax
    ; extent = 1084 + 1024*P (32-bit), must be <= L
    mov dx, ax
    mov cl, 10
    shl ax, cl                  ; low word of P<<10
    mov cl, 6
    shr dx, cl                  ; high word of P<<10
    add ax, 1084
    adc dx, 0                   ; DX:AX = pattern-block end = sample start
    cmp dx, [mp_bloblen_hi]
    ja .e_trunc
    jb .extok
    cmp ax, [mp_bloblen_lo]
    jbe .extok
.e_trunc:
    push ds
    pop es
    mov ax, mpe_trunc
    stc
    ret
.extok:
    mov [mp_run_lo], ax         ; 32-bit running sample start offset
    mov [mp_run_hi], dx
    mov ax, [mp_t1]
    mov [mp_npat], al           ; P <= 126 after the extent check (file cap)
    ; -- 7..10: the 31 sample headers -> mp_smptab ---------------------------
    mov word [mp_t1], 0         ; sample index 0..30
.sm:
    mov ax, [mp_t1]
    mov bx, ax
    mov cl, 5
    shl ax, cl                  ; *32
    shl bx, 1                   ; *2
    sub ax, bx                  ; *30
    add ax, 20
    mov si, ax                  ; SI = header offset in blob
    ; DI = table entry
    mov ax, [mp_t1]
    mov bx, ax
    mov cl, 3
    shl ax, cl                  ; *8
    shl bx, 1
    shl bx, 1                   ; *4
    add ax, bx                  ; *12
    add ax, mp_smptab
    mov di, ax
    ; length in words, big-endian -> DX:AX = bytes (17-bit)
    mov ax, [es:si+22]
    xchg al, ah
    xor dx, dx
    shl ax, 1
    adc dx, 0                   ; DX:AX = stated byte length
    mov [mp_t2], ax             ; stated length lo
    mov [mp_t3], dx             ; ...hi
    ; finetune + volume
    mov al, [es:si+24]
    and al, 0x0F
    mov [di+MS_FINE], al
    mov al, [es:si+25]
    cmp al, 64
    jbe .vok
    mov al, 64
.vok:
    mov [di+MS_VOL], al
    ; loop start / length, words BE -> bytes, each capped at 0xFFFF
    mov ax, [es:si+26]
    xchg al, ah
    shl ax, 1
    jnc .lsok
    mov ax, 0xFFFF              ; >64K start: lands >= plen -> loop disabled
.lsok:
    mov [mp_t4], ax             ; ls bytes
    mov ax, [es:si+28]
    xchg al, ah
    shl ax, 1
    jnc .llok
    mov ax, 0xFFFF
.llok:
    mov [mp_t5], ax             ; ll bytes
    ; normalized base from the running 32-bit start
    mov ax, [mp_run_lo]
    mov dx, [mp_run_hi]
    mov bx, ax
    and bx, 15
    mov [di+MS_OFF], bx
    mov cx, 4
.sh4:
    shr dx, 1
    rcr ax, 1
    loop .sh4                   ; AX = start >> 4 (hi bits beyond 20 only on
    add ax, [mp_blobseg]        ; hostile files whose samples are unplayable)
    mov [di+MS_SEG], ax
    ; effective length = min(stated, bytes remaining in file, MP_MAXSMP)
    mov ax, [mp_bloblen_lo]
    mov dx, [mp_bloblen_hi]
    sub ax, [mp_run_lo]
    sbb dx, [mp_run_hi]
    jc .zavail                  ; start already past EOF: nothing here
    or dx, dx
    jz .avlo
    mov ax, 0xFFFF              ; >= 64K available: the cap below decides
.avlo:
    jmp .haveav
.zavail:
    xor ax, ax
.haveav:
    ; AX = available (capped 0xFFFF); stated in mp_t3:mp_t2
    cmp word [mp_t3], 0
    jne .stbig
    cmp ax, [mp_t2]
    jbe .avwins
    mov ax, [mp_t2]
    jmp .lenset
.stbig:
    jmp .lenset                 ; stated >= 64K: available (in AX) wins
.avwins:
.lenset:
    cmp ax, MP_MAXSMP
    jbe .capok
    mov ax, MP_MAXSMP
.capok:
    mov [mp_t6], ax             ; elen
    ; advance the running start by the STATED length (the file layout)
    mov ax, [mp_t2]
    mov dx, [mp_t3]
    add [mp_run_lo], ax
    adc [mp_run_hi], dx
    ; loop fix-ups on elen
    mov ax, [mp_t4]             ; ls
    mov bx, [mp_t5]             ; ll
    mov cx, [mp_t6]             ; elen
    cmp ax, cx
    jae .oneshot                ; loop starts past the data: no loop
    add ax, bx                  ; ls + ll (17-bit possible)
    jc .trim
    cmp ax, cx
    jbe .lfit
.trim:
    mov bx, cx
    sub bx, [mp_t4]             ; ll = elen - ls
    mov ax, cx                  ; loop end = elen
.lfit:
    or bx, bx
    jz .oneshot                 ; zero loop length: one-shot over full elen
    cmp ax, 2                   ; AX = loop end (ls+ll, or elen after .trim)
    jbe .oneshot                ; ft2 rule (ft2_load_mod.c:308): looping iff
                                ; loopStart + loopLength > 2 bytes - a
                                ; 1-word loop at a nonzero start DOES loop
    mov [di+MS_PLEN], ax        ; play length = loop end
    mov [di+MS_LLEN], bx
    mov ax, [mp_t4]
    mov [di+MS_LSTART], ax
    jmp .snext
.oneshot:
    mov ax, [mp_t6]
    mov [di+MS_PLEN], ax
    mov word [di+MS_LLEN], 0
    mov word [di+MS_LSTART], 0
.snext:
    inc word [mp_t1]
    cmp word [mp_t1], 31
    jae .smdone
    jmp .sm
.smdone:
    push ds
    pop es
    call mp_vtbuild             ; the 65x256 volume table, in the current
                                ; mode's format (SPEC.md 45.9)
    ; -- done ----------------------------------------------------------------
    mov byte [mp_songpos], 0
    mov byte [mp_row], 0
    mov bx, [mp_orders]         ; BL = orders[0]
    mov al, bl
    mov [mp_pattern], al
    mov byte [mp_loaded], 1
    clc
    ret

; -----------------------------------------------------------------------------
; mp_vtbuild - the 65x256 volume table for the current mode (SPEC.md 45.9):
;   default: vt[vol][b] = (int8)b * vol >> 6 (+/-127; 16-bit accumulate,
;            out = 128 + (sum >> 2))
;   XT:      vt[vol][b] = (int8)b * vol >> 8 (+/-31; four channels sum in
;            a byte around the 0x80 bias - the output stage pre-scaled away)
; clobbers AX/BX/CX/DI
; -----------------------------------------------------------------------------
mp_vtbuild:
    mov di, mp_voltab
    mov byte [mp_tvol], 0
    mov cl, 6
    cmp byte [mp_xt], 0
    je .vt_v
    mov cl, 8
.vt_v:
    xor bx, bx                  ; BL = source byte 0..255
.vt_b:
    mov al, bl
    imul byte [mp_tvol]         ; AX = (int8)b * vol
    sar ax, cl
    mov [di], al
    inc di
    inc bl
    jnz .vt_b
    inc byte [mp_tvol]
    cmp byte [mp_tvol], 65
    jb .vt_v
    ret

; -----------------------------------------------------------------------------
; mp_setxt - set XT mode (AL = 0/1; SPEC.md 45.9) and rebuild the volume
; table to the mode's format when a module is loaded. UI context, playback
; stopped by the caller: the 16,640-imul rebuild is a deliberate sub-second
; freeze on the machines the mode exists for. Clobbers AX/BX/CX/DI.
; -----------------------------------------------------------------------------
mp_setxt:
    mov [mp_xt], al
    cmp byte [mp_loaded], 0
    je .out
    call mp_vtbuild
.out:
    ret

; =============================================================================
; mp_start - reset the replayer and start playing
; in: AL = 0 play song from position 0 / 1 loop the current pattern
;          / 2 RESUME where a stop parked it (SPEC.md 45.17)
;
; MODE 2 IS WHAT MAKES PAUSE A PAUSE. trk_play_stop already parks songpos, the
; pattern and the row at the row the LISTENER last heard - it has to, or
; stopping jumps the display three seconds forward (SPEC.md 45.15) - and every
; play path then threw that away and started at position 0. Resume keeps all
; three, and keeps the SPEED and TEMPO with them: an Fxx earlier in the pattern
; is not on the row being resumed, so re-reading the row cannot put it back and
; a resumed song would run at 125 BPM until the next one.
;
; It is not a third [mp_ploop] value. That byte is a FLAG - two readers test it
; non-zero for "never leave this pattern" - so a 2 stored there would resume
; into a pattern loop, which is the shape of bug that assembles and sounds
; nearly right.
; =============================================================================
mp_start:
    cmp byte [mp_loaded], 0
    jne .ok
    ret
.ok:
    mov ah, al                  ; AH = the mode, kept across the wipe below
    cmp al, 2
    jne .mode
    xor al, al                  ; resume is not pattern loop - see the header
.mode:
    mov [mp_ploop], al
    or al, al
    jnz .keep
    cmp ah, 2
    je .keep                    ; ...and resume keeps the position too
    mov byte [mp_songpos], 0
.keep:
    ; zero the four channel records
    push ax
    push ds
    pop es
    cld
    mov di, mp_chans
    mov cx, (MP_CHSZ * 4) / 2
    xor ax, ax
    rep stosw
    pop ax
    cmp ah, 2
    je .tempo                   ; resume: the row, the speed and the tempo are
    mov byte [mp_row], 0        ; the ones that were playing
    mov byte [mp_speed], 6
    mov byte [mp_bpm], 125
.tempo:
    mov byte [mp_tick], 0
    mov byte [mp_patdel], 0
    mov byte [mp_patdel2], 0
    mov byte [mp_pbreak], 0
    mov byte [mp_pbrow], 0
    mov byte [mp_posjmp], 0
    mov bl, [mp_songpos]
    xor bh, bh
    and bx, 127                 ; load-bearing, not hygiene: a Bxx row holds
                                ; songpos at param-1 (0xFF when the clamped
                                ; target is 0) for its remaining ticks, and
                                ; P pressed in that window reaches here -
                                ; unmasked it indexes 127 bytes past
                                ; mp_orders. Every orders byte names a
                                ; validated pattern (mp_load's P is the max
                                ; over all 128), so the mask is safe
    mov al, [mp_orders + bx]
    mov [mp_pattern], al
    call mp_calc_spt
    mov ax, [mp_spt]
    mov [mp_tickrem], ax
    call mp_readrow             ; row 0 tick 0 happens NOW; the first
    call mp_chupd               ; mp_gen samples already carry it
    mov byte [mp_playing], 1
    ret

; =============================================================================
; mp_stop
; =============================================================================
mp_stop:
    mov byte [mp_playing], 0
    xor ax, ax
    mov [mp_chvol], ax
    mov [mp_chvol + 2], ax
    mov [mp_chper], ax
    mov [mp_chper + 2], ax
    mov [mp_chper + 4], ax
    mov [mp_chper + 6], ax
    ret

; =============================================================================
; mp_setpos - AL = signed delta to song position, clamped to 0..songlen-1
; =============================================================================
mp_setpos:
    cmp byte [mp_loaded], 0
    je .out
    mov ah, [mp_songpos]
    add ah, al                  ; signed byte math; clamp BOTH ends below
    js .wrap
    cmp ah, [mp_songlen]
    jb .have
.top:
    mov ah, [mp_songlen]
    dec ah
    jmp .have
.wrap:
    or al, al                   ; AL survives the add: sign set by a positive
    jns .top                    ; delta means 127+1 overflowed (songlen=128 -
    xor ah, ah                  ; the one legal length that can) - clamp to
                                ; songlen-1, not position 0
.have:
    mov [mp_songpos], ah
    mov bl, ah
    xor bh, bh
    mov al, [mp_orders + bx]
    mov [mp_pattern], al
    mov byte [mp_pbreak], 0
    mov byte [mp_posjmp], 0
    mov byte [mp_pbrow], 0
    cmp byte [mp_playing], 0
    jne .out                    ; playing: the row chain continues there
    mov byte [mp_row], 0
.out:
    ret

; =============================================================================
; mp_mutetog - AL = channel 0..3
; =============================================================================
mp_mutetog:
    cmp al, 3
    ja .out
    mov bl, al
    xor bh, bh
    xor byte [mp_mute + bx], 1
.out:
    ret

; =============================================================================
; The position stamps (SPEC.md 45.15). Three routines, and the whole point of
; them is that mp_row is the row being MIXED: trk_feed keeps the ring topped
; up to TRK_RING - TRK_HALF, so on a 5,500 Hz XT mode stream the mixer is
; 2.2-3.0 s ahead of the card. Every readout that quotes mp_* is that far
; ahead of the music - measured, not modelled: press Enter and screendump
; 0.4 s later and the grid is already on row 21.
;
; A stamp is written where a row's first sample lands in the stream, and
; mp_at_pos answers with the stamp the DAC is inside. The app supplies
; [mp_stampbase] - the stream position mp_outbuf's first byte occupies - and
; that is the only thing the replayer needs to know about the stream.
; =============================================================================

; -----------------------------------------------------------------------------
; mp_stamp_at - record the current replayer state at stream position AX
; -----------------------------------------------------------------------------
mp_stamp_at:
    push ax
    push bx
    mov byte [mp_rowadv], 0
    mov bl, [mp_stw]
    and bl, MP_ST_MASK          ; the cursors are FREE-RUNNING bytes and are
    xor bh, bh                  ; masked here rather than stored masked: the
    shl bx, 1                   ; writer is trk_feed's lock-free worker and
    shl bx, 1                   ; the reader is the UI task, so both advances
    shl bx, 1                   ; have to be one indivisible `inc` - a
    shl bx, 1                   ; read-mask-store pair can rewind the other
    add bx, mp_stamps           ; task's (8086: shift one bit at a time)
    mov [bx + MP_SP_POS], ax
    mov al, [mp_pendpos]        ; the position as it was BEFORE this row's
    mov [bx + MP_SP_SPOS], al   ; effects ran - a Bxx parks the live one
    mov al, [mp_pendpat]
    mov [bx + MP_SP_PAT], al
    mov al, [mp_row]
    mov [bx + MP_SP_ROW], al
    mov al, [mp_bpm]
    mov [bx + MP_SP_BPM], al
    mov al, [mp_speed]
    mov [bx + MP_SP_SPD], al
    mov ax, [mp_chvol]
    mov [bx + MP_SP_VOL], ax
    mov ax, [mp_chvol + 2]
    mov [bx + MP_SP_VOL + 2], ax
    mov ax, [mp_hit]            ; ...and the strikes since the last stamp,
    mov [bx + MP_SP_HIT], ax    ; READ AND CLEARED: a strike belongs to the
    mov word [mp_hit], 0        ; row that made it and to no later one
    mov ax, [mp_hit + 2]
    mov [bx + MP_SP_HIT + 2], ax
    mov word [mp_hit + 2], 0
    inc byte [mp_stw]
    mov al, [mp_stw]
    sub al, [mp_str]            ; the ring is full: the next stamp overwrites
    cmp al, MP_ST_N             ; the oldest, so the reader moves with it. The
    jb .out                     ; display then runs LESS late than the music,
    inc byte [mp_str]           ; never early against it (SPEC.md 45.15)
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mp_stamp - the same, at the position mp_gen's cursor has reached. Called
;            from inside mp_gen, where a row has just begun.
; -----------------------------------------------------------------------------
mp_stamp:
    push ax
    mov ax, [mp_outp]
    sub ax, mp_outbuf           ; bytes into this buffer...
    add ax, [mp_stampbase]      ; ...and where the buffer starts in the stream
    call mp_stamp_at
    pop ax
    ret

; -----------------------------------------------------------------------------
; mp_stclear - drop the history and seed it with the state at [mp_stampbase].
;              Called by the app when a stream's byte counter restarts.
; -----------------------------------------------------------------------------
mp_stclear:
    push ax
    mov byte [mp_stw], 0
    mov byte [mp_str], 0
    mov word [mp_hit], 0            ; strikes belong to the stream that made
    mov word [mp_hit + 2], 0        ; them: a restart must not carry one over
    mov al, [mp_songpos]        ; no row has begun since mp_start, so the
    mov [mp_pendpos], al        ; live position IS the pending one
    mov al, [mp_pattern]
    mov [mp_pendpat], al
    mov ax, [mp_stampbase]
    call mp_stamp_at            ; ...which leaves stw = 1, str = 0: one stamp
    pop ax                      ; held, and it is the one being played
    ret

; -----------------------------------------------------------------------------
; mp_at_pos - in AX = the stream byte the card has reached (the driver's
;             free-running consumed count)
; out: SI = the stamp being heard. The read cursor only ever moves forward,
;      so this is O(rows crossed since the last frame) - normally 0 or 1.
;
; The comparison is the kernel's own wrap-safe idiom (sch_isr's wake scan):
; both counters are free-running 16-bit, the lead is bounded by the 16KB ring
; so the true difference can never reach 32768, and THE SIGN OF THE RESULT
; decides. It has to be `js` and not `jg`: jg is a signed compare of the two
; OPERANDS and so honours the overflow flag, which is precisely what modular
; arithmetic must ignore. With jg the reader stopped dead whenever consumed
; sat in the upper half of the range and the stamp in the lower - six seconds
; out of every twelve at the XT rate - and the display froze there and then
; jumped, which is what a field report of "the scroll updates every couple of
; seconds" looks like from the inside.
; -----------------------------------------------------------------------------
mp_at_pos:
    push ax
    push bx
    push cx
.adv:
    mov bl, [mp_stw]
    sub bl, [mp_str]            ; free-running distance, exact mod 256
    cmp bl, 1
    jbe .have                   ; str is the newest stamp there is
    mov bl, [mp_str]
    inc bl
    and bl, MP_ST_MASK
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, mp_stamps
    mov cx, [bx + MP_SP_POS]
    sub cx, ax                  ; wrap-safe: the SIGN of the result decides,
    jz .step                    ; and the card is exactly on this row
    jns .have                   ; ...positive: the row is still in the future
.step:
    inc byte [mp_str]
    jmp short .adv
.have:
    mov bl, [mp_str]
    and bl, MP_ST_MASK
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, mp_stamps
    mov si, bx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; mp_order - AL = order index -> AL = pattern number there
; =============================================================================
mp_order:
    mov bl, al
    xor bh, bh
    and bx, 127
    mov al, [mp_orders + bx]
    ret

; =============================================================================
; mp_insname - AL = instrument 1..31 -> ES:SI = its 22-byte name field in
; the blob (raw, not NUL-guaranteed), CF=1 none/invalid. CALLER restores ES.
; =============================================================================
mp_insname:
    cmp byte [mp_loaded], 0
    je .no
    or al, al
    jz .no
    cmp al, 31
    ja .no
    dec al
    xor ah, ah
    mov si, ax
    mov bx, ax
    mov cl, 5
    shl si, cl                  ; *32
    shl bx, 1                   ; *2
    sub si, bx                  ; *30
    add si, 20
    mov es, [mp_blobseg]
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; mp_cell2txt - in AL = row 0..63, AH = channel 0..3, DI = pattern number;
; fills mp_celltxt with 'C-2 01 A0F' + NUL. CF=1 no module / out of range.
; =============================================================================
mp_cell2txt:
    cmp byte [mp_loaded], 0
    je .no
    cmp al, 63
    ja .no
    cmp ah, 3
    ja .no
    mov bl, [mp_npat]
    xor bh, bh
    cmp di, bx
    jae .no
    ; ES = pattern segment, SI = cell offset
    mov bx, di
    mov cl, 6
    shl bx, cl                  ; pattern * 64 paragraphs
    add bx, 67
    add bx, [mp_blobseg]
    mov es, bx
    mov bl, al
    xor bh, bh
    mov cl, 4
    shl bx, cl                  ; row * 16
    mov si, bx
    mov bl, ah
    xor bh, bh
    shl bx, 1
    shl bx, 1                   ; ch * 4
    add si, bx
    add si, 12
    ; decode
    mov al, [es:si]             ; b0
    mov ah, [es:si+1]           ; b1
    mov bl, [es:si+2]           ; b2
    mov cl, [es:si+3]           ; b3 = param
    push ds
    pop es
    mov ch, al
    and ch, 0xF0
    mov dl, bl
    push cx
    mov cl, 4
    shr dl, cl
    pop cx
    or ch, dl
    and ch, 0x1F                ; CH = sample 0..31
    and bl, 0x0F                ; BL = effect
    and al, 0x0F
    xchg al, ah                 ; AX = period (hi nibble from b0)
    mov di, mp_celltxt
    ; --- note ---
    or ax, ax
    jnz .note
    mov byte [di], '.'
    mov byte [di+1], '.'
    mov byte [di+2], '.'
    jmp .ins
.note:
    push bx
    push cx
    call mp_pfind               ; AX period -> BX = note index 0..35
    mov ax, bx
    mov bl, 12
    div bl                      ; AL = octave, AH = semitone
    mov dl, al
    mov al, ah
    xor ah, ah
    shl ax, 1
    mov si, ax
    mov al, [mp_ntab + si]
    mov [di], al
    mov al, [mp_ntab + si + 1]
    mov [di+1], al
    mov al, dl
    add al, '1'
    mov [di+2], al
    pop cx
    pop bx
.ins:
    mov byte [di+3], ' '
    mov al, ch
    or al, al
    jnz .insn
    mov byte [di+4], '.'
    mov byte [di+5], '.'
    jmp .fx
.insn:
    push di
    add di, 4
    call mp_hexb
    pop di
.fx:
    mov byte [di+6], ' '
    or bl, bl
    jnz .fxn
    or cl, cl
    jnz .fxn
    mov byte [di+7], '.'
    mov byte [di+8], '.'
    mov byte [di+9], '.'
    jmp .done
.fxn:
    mov al, bl
    and al, 0x0F
    push bx
    mov bl, al
    xor bh, bh
    mov al, [mp_hexd + bx]
    pop bx
    mov [di+7], al
    mov al, cl
    push di
    add di, 8
    call mp_hexb
    pop di
.done:
    mov byte [di+10], 0
    clc
    ret
.no:
    stc
    ret

; mp_hexb - AL as two uppercase hex digits at DI (module-local helper)
mp_hexb:
    push ax
    push bx
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    mov bl, al
    xor bh, bh
    mov al, [mp_hexd + bx]
    mov [di], al
    mov bl, ah
    and bl, 0x0F
    mov al, [mp_hexd + bx]
    mov [di+1], al
    pop cx
    pop bx
    pop ax
    ret

; mp_pfind - AX = period -> BX = note index 0..35 (first entry <= period;
; nonstandard periods match nearest below, per the ft2 loader)
mp_pfind:
    xor bx, bx
.l:
    cmp bx, 35
    jae .out
    push bx
    shl bx, 1
    cmp ax, [mp_pertab + bx]
    pop bx
    jae .out
    inc bx
    jmp .l
.out:
    ret

; =============================================================================
; mp_gen - render CX bytes (even, <= 2048) of unsigned 8-bit mono into
; mp_outbuf and advance the replayer. Silence when not playing.
; =============================================================================
mp_gen:
    cld
    push bp                     ; mp_mixch_xt keeps the frac step in BP
    push ds
    pop es
    or cx, cx
    jnz .have
    jmp .done
.have:
    cmp cx, 2048
    jbe .cxok
    mov cx, 2048
.cxok:
    mov [mp_need], cx
    mov word [mp_outp], mp_outbuf
.big:
    mov cx, [mp_need]
    or cx, cx
    jz .done
    cmp byte [mp_playing], 0
    je .silence
    cmp byte [mp_loaded], 0
    je .silence
    ; chunk = min(need, tickrem, MP_CHUNK)
    mov ax, cx
    cmp ax, [mp_tickrem]
    jbe .c1
    mov ax, [mp_tickrem]
.c1:
    cmp ax, MP_CHUNK
    jbe .c2
    mov ax, MP_CHUNK
.c2:
    mov [mp_chunk], ax
    cmp byte [mp_xt], 0         ; XT mode mixes straight into the outbuf
    jne .xtmix                  ; (SPEC.md 45.9) - no zero, no convert
    ; zero the accumulator
    mov di, mp_mixbuf
    mov cx, ax
    xor ax, ax
    rep stosw
    ; mix the four channels
    mov word [mp_crec], mp_chans
    mov byte [mp_curc], 0
.mixc:
    call mp_mixch
    add word [mp_crec], MP_CHSZ
    inc byte [mp_curc]
    cmp byte [mp_curc], 4
    jb .mixc
    ; convert: out = 128 + (sum >> 2)
    mov si, mp_mixbuf
    mov di, [mp_outp]
    mov cx, [mp_chunk]
.cv:
    lodsw
    sar ax, 1
    sar ax, 1
    add ax, 128
    stosb
    loop .cv
.adv:
    mov [mp_outp], di
    mov ax, [mp_chunk]
    sub [mp_need], ax
    sub [mp_tickrem], ax
    jnz .big
    call mp_dotick              ; a tick boundary
    cmp byte [mp_rowadv], 0     ; ...and if a ROW began on it, stamp the
    je .norow                   ; stream byte its first sample lands on
    call mp_stamp               ; (SPEC.md 45.15); mp_stamp clears the flag
.norow:
    mov ax, [mp_spt]
    mov [mp_tickrem], ax
    jmp .big
.xtmix:
    mov byte [mp_first], 1      ; the first audible channel STORES the
    mov word [mp_crec], mp_chans; biased byte, later ones ADD (45.9)
    mov byte [mp_curc], 0
.xmc:
    call mp_mixch_xt
    add word [mp_crec], MP_CHSZ
    inc byte [mp_curc]
    cmp byte [mp_curc], 4
    jb .xmc
    cmp byte [mp_first], 0      ; nothing audible rendered: the chunk is
    je .xdone                   ; the bias alone
    mov di, [mp_outp]
    mov cx, [mp_chunk]
    mov al, 0x80
    rep stosb
.xdone:
    mov di, [mp_outp]
    add di, [mp_chunk]
    jmp .adv
.silence:
    mov di, [mp_outp]
    mov al, 0x80
    rep stosb                   ; CX = remaining need
    mov word [mp_need], 0
.done:
    push ds
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; mp_mixch - accumulate [mp_chunk] samples of channel [mp_crec] into
; mp_mixbuf (16-bit words). Advances the channel position; a muted or
; silent channel advances without fetching; an ended one-shot goes inactive.
; -----------------------------------------------------------------------------
mp_mixch:
    mov si, [mp_crec]
    cmp byte [si+MP_ACT], 0
    je .out
    ; limit + loop + step scratch
    mov ax, [si+MP_OFF]
    add ax, [si+MP_PLEN]
    mov [mp_climit], ax
    mov ax, [si+MP_LLEN]
    mov [mp_cloopl], ax
    mov ax, [si+MP_STEPI]
    mov [mp_cstepi], ax
    mov ax, [si+MP_STEPF]
    mov [mp_cstepf], ax
    ; silent? (muted channels keep their position moving)
    mov bl, [mp_curc]
    xor bh, bh
    cmp byte [mp_mute + bx], 0
    jne .silent
    mov al, [si+MP_OVOL]
    or al, al
    jz .silent
    ; BX = volume table slice
    xor ah, ah
    push cx
    mov cl, 8
    shl ax, cl
    pop cx
    add ax, mp_voltab
    mov bx, ax
    ; the inner loop
    mov es, [si+MP_SEG]
    mov dx, [si+MP_POSF]
    mov cx, [mp_chunk]
    mov di, mp_mixbuf
    push si
    mov si, [si+MP_POSI]
.mx:
    mov al, [es:si]
    xlat                        ; AL = vt[vol][sample]
    cbw
    add [di], ax
    inc di
    inc di
    add dx, [mp_cstepf]
    adc si, [mp_cstepi]
    cmp si, [mp_climit]
    jae .wrap
.mnext:
    loop .mx
.mdone:
    mov ax, si
    pop si
    mov [si+MP_POSI], ax
    mov [si+MP_POSF], dx
    push ds
    pop es
.out:
    ret
.wrap:
    mov ax, [mp_cloopl]
    or ax, ax
    jz .oend
.wl:
    sub si, ax
    cmp si, [mp_climit]
    jae .wl
    jmp .mnext
.oend:
    mov ax, si
    pop si
    mov [si+MP_POSI], ax
    mov [si+MP_POSF], dx
    mov byte [si+MP_ACT], 0     ; one-shot ended mid-chunk
    push ds
    pop es
    ret
.silent:
    ; position += step * chunk, then wrap
    push si
    mov ax, [mp_cstepf]
    mul word [mp_chunk]         ; DX:AX = frac advance
    mov cx, dx
    pop si
    add ax, [si+MP_POSF]
    adc cx, 0
    mov [si+MP_POSF], ax
    push si
    mov ax, [mp_cstepi]
    mul word [mp_chunk]         ; DX:AX, DX = 0 (step*chunk bounded)
    pop si
    add ax, cx
    add ax, [si+MP_POSI]        ; cannot wrap: limit <= 64015, adv <= 1520
.swl:
    cmp ax, [mp_climit]
    jb .sst
    mov cx, [mp_cloopl]
    or cx, cx
    jz .soend
    sub ax, cx
    jmp .swl
.sst:
    mov [si+MP_POSI], ax
    ret
.soend:
    mov byte [si+MP_ACT], 0
    ret

; -----------------------------------------------------------------------------
; mp_mixch_xt - one channel's chunk in XT mode (SPEC.md 45.9), straight into
; the outbuf at [mp_outp]. [mp_first] = 1: this is the first audible channel,
; STORE 128 + vt[b]; 0: ADD vt[b]. The bounds check leaves the inner loop:
; one DIV cuts a conservative run ((limit-1-pos)/(stepint+1) samples cannot
; reach the limit), the run mixes unchecked, and only the boundary approach
; walks the checked/wrapping path a sample at a time. Muted/silent channels
; take mp_mixch's shared advance. BP carries the frac step as a register -
; never a memory base, so SS != DS cannot bite.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; MIXSAMP - one channel-sample of the XT mixer (PERFORMANCE.md Set 20)
;
;   %1 = 0  the STORE pass: this is the first live channel, so it writes
;   %1 = 1  the ADD pass: every channel after it accumulates
;   %2      this copy's PATCHED step immediate - a LOCAL label
;
; in:  ES:SI = the sample, DS:BX = the volume-table slice (xlat), DI = the
;      output, DX = the fractional position, BP = the fractional step.
;      Advances SI, DI and DX; leaves CX alone, so the caller owns the loop.
;
; WHY THE STEP IS AN IMMEDIATE AND NOT A MEMORY OPERAND. On an 8088 the bus is
; EIGHT BITS: every byte fetched or accessed is 4 clocks and they serialise, so
; what a loop costs is its BYTE TRAFFIC - instruction bytes and data bytes
; alike - and not its instruction count. This body was 15 instruction bytes and
; 6 data bytes = 21 = 84 clocks, and 84 x 4 channels x 5,500 Hz is 38.7% of a
; 4.77MHz machine, which is what PERFORMANCE.md Set 20 profiled. The integer
; step is LOOP-INVARIANT and cost 2 of those data bytes as
; `adc si, [mp_cstepi]`; as an immediate it is the same four instruction bytes
; and no read at all.
;
; mp_stepi_set patches every copy, once per channel per chunk - thousands of
; samples before the prefetch queue could be holding a stale one, which is the
; only thing that makes self-modifying code delicate on this CPU.
; -----------------------------------------------------------------------------
%macro MIXSAMP 2
    mov al, [es:si]
    xlat
%if %1 == 0
    add al, 0x80
    mov [di], al
%else
    add [di], al
%endif
    inc di
    add dx, bp
    db 0x81, 0xD6                   ; adc si, imm16 - the immediate is DATA,
%2: dw 0                            ; patched by mp_stepi_set
%endmacro

; -----------------------------------------------------------------------------
; mp_stepi_set - AX = the integer step, into every MIXSAMP copy
;                Preserves every register.
; -----------------------------------------------------------------------------
mp_stepi_set:                       ; STRAIGHT LINE, not a table walk: this runs
    mov [mp_mixch_xt.ps0], ax       ; once per channel per chunk and the loop it
    mov [mp_mixch_xt.ps1], ax       ; replaces profiled at 3.1% of the whole
    mov [mp_mixch_xt.ps2], ax       ; machine. `mov [imm16], ax` is the
    mov [mp_mixch_xt.ps3], ax       ; accumulator-direct form - 3 instruction
    mov [mp_mixch_xt.ps4], ax       ; bytes and a 2-byte write, 20 clocks - so
    mov [mp_mixch_xt.pa0], ax       ; ten of them are 200 against the loop's
    mov [mp_mixch_xt.pa1], ax       ; ~480, and it touches no register but AX,
    mov [mp_mixch_xt.pa2], ax       ; so the three pushes go too
    mov [mp_mixch_xt.pa3], ax
    mov [mp_mixch_xt.pa4], ax
    ret

mp_mixch_xt:
    mov si, [mp_crec]
    cmp byte [si+MP_ACT], 0
    je .ret0
    mov ax, [si+MP_OFF]
    add ax, [si+MP_PLEN]
    mov [mp_climit], ax
    mov ax, [si+MP_LLEN]
    mov [mp_cloopl], ax
    mov ax, [si+MP_STEPI]
    mov [mp_cstepi], ax
    call mp_stepi_set           ; ...and into every MIXSAMP copy, once per
    mov ax, [si+MP_STEPF]       ; channel rather than once per sample
    mov [mp_cstepf], ax
    mov bl, [mp_curc]
    xor bh, bh
    cmp byte [mp_mute + bx], 0
    jne mp_mixch.silent         ; the shared advance-only path: same scratch,
    mov al, [si+MP_OVOL]        ; same [mp_crec] in SI, ends in ret
    or al, al
    jz mp_mixch.silent
    xor ah, ah
    mov cl, 8
    shl ax, cl
    add ax, mp_voltab
    mov bx, ax                  ; BX = the volume table slice (xlat)
    mov es, [si+MP_SEG]
    mov dx, [si+MP_POSF]
    mov bp, [mp_cstepf]         ; BP = frac step, register-resident
    mov di, [mp_outp]
    mov cx, [mp_chunk]          ; CX = samples remaining
    push si
    mov si, [si+MP_POSI]
.runs:
    mov ax, [mp_climit]         ; conservative unchecked run
    dec ax
    sub ax, si
    jb .edge                    ; at/past limit-1: checked path decides
    push cx
    push dx
    xor dx, dx
    mov cx, [mp_cstepi]
    inc cx                      ; stepint+1 >= step always, so n samples
    div cx                      ; cannot reach the limit
    pop dx
    pop cx
    or ax, ax
    jz .edge
    cmp ax, cx
    jbe .runn
    mov ax, cx
.runn:
    sub cx, ax
    xchg cx, ax                 ; CX = run length, AX = remaining
    push ax
    mov ax, cx                  ; the run is walked FOUR samples at a time and
    shr cx, 1                   ; then the 0..3 that are left, which amortises
    shr cx, 1                   ; `loop` from 2 bytes a sample to half of one
    and ax, 3                   ; (PERFORMANCE.md Set 20: on an 8-bit bus the
    mov [mp_crem], ax           ; byte count IS the cost)
    cmp byte [mp_first], 0
    je .addq
    jcxz .strem
.stq:
    MIXSAMP 0, .ps0           ; store pass: out = 128 + vt[b]
    MIXSAMP 0, .ps1
    MIXSAMP 0, .ps2
    MIXSAMP 0, .ps3
    loop .stq
.strem:
    mov cx, [mp_crem]
    jcxz .rejoin
.stl:
    MIXSAMP 0, .ps4
    loop .stl
    jmp .rejoin
.addq:
    jcxz .addrem
.addq1:
    MIXSAMP 1, .pa0           ; add pass: out += vt[b]
    MIXSAMP 1, .pa1
    MIXSAMP 1, .pa2
    MIXSAMP 1, .pa3
    loop .addq1
.addrem:
    mov cx, [mp_crem]
    jcxz .rejoin
.addl:
    MIXSAMP 1, .pa4
    loop .addl
.rejoin:
    pop cx                      ; CX = remaining
    or cx, cx
    jnz .runs
    jmp .fin
.edge:
    cmp si, [mp_climit]         ; the boundary approach, one checked sample
    jae .wrap
    mov al, [es:si]
    xlat
    cmp byte [mp_first], 0
    je .eadd
    add al, 0x80
    mov [di], al
    jmp .estep
.eadd:
    add [di], al
.estep:
    inc di
    add dx, bp
    adc si, [mp_cstepi]
    dec cx                      ; one sample consumed: recut the run. NOT
    jz .fin                     ; `loop` - the unrolled bodies put .runs past
    jmp .runs                   ; its 128-byte reach
.wrap:
    mov ax, [mp_cloopl]
    or ax, ax
    jz .oend
.wl:
    sub si, ax
    cmp si, [mp_climit]
    jae .wl
    jmp .runs                   ; wrapped, nothing consumed: recut
.fin:
    mov ax, si
    pop si
    mov [si+MP_POSI], ax
    mov [si+MP_POSF], dx
    mov byte [mp_first], 0      ; this channel rendered: later ones add
    push ds
    pop es
    ret
.oend:                          ; one-shot ended mid-chunk
    push ds
    pop es                      ; BEFORE any stosb: ES was the sample seg
    cmp byte [mp_first], 0
    je .of
    mov al, 0x80                ; the store channel owes the bias under
    rep stosb                   ; the tail it never rendered
    mov byte [mp_first], 0
.of:
    pop si
    mov byte [si+MP_ACT], 0
.ret0:
    ret

; =============================================================================
; The replayer: one tick
; =============================================================================
mp_dotick:
    cmp byte [mp_playing], 0
    je .out
    inc byte [mp_tick]
    mov al, [mp_tick]
    cmp al, [mp_speed]
    jb .pertick
    mov byte [mp_tick], 0
    ; pattern delay (EEx): transfer, then hold held rows
    mov al, [mp_patdel]
    or al, al
    jz .nd
    mov [mp_patdel2], al
    mov byte [mp_patdel], 0
.nd:
    mov al, [mp_patdel2]
    or al, al
    jz .advance
    dec al
    mov [mp_patdel2], al
    or al, al
    jz .advance
    jmp .upd                    ; held row: no note re-read
.advance:
    call mp_nextrow
    mov al, [mp_songpos]        ; BEFORE the effects: a Bxx parks songpos at
    mov [mp_pendpos], al        ; target-1 for the rest of its row (mp_start's
    mov al, [mp_pattern]        ; note), and a stamp taken after mp_readrow
    mov [mp_pendpat], al        ; would carry that instead of the position
    call mp_readrow             ; this row actually belongs to
    mov byte [mp_rowadv], 1     ; mp_gen stamps it where it stands in the
    jmp .upd                    ; output (SPEC.md 45.15)
.pertick:
    call mp_tickfx
.upd:
    call mp_chupd
.out:
    ret

; -----------------------------------------------------------------------------
; mp_nextrow - row/position advance (the ft2 getNextPos shape)
; -----------------------------------------------------------------------------
mp_nextrow:
    mov al, [mp_row]
    inc al
    cmp byte [mp_pbreak], 0
    je .nf
    mov al, [mp_pbrow]          ; E6x: jump within this pattern
    mov byte [mp_pbreak], 0
.nf:
    cmp byte [mp_posjmp], 0
    jne .adv
    cmp al, 64
    jb .set
.adv:
    mov al, [mp_pbrow]
    mov byte [mp_pbrow], 0
    mov byte [mp_posjmp], 0
    cmp al, 64
    jb .r0
    xor al, al
.r0:
    cmp byte [mp_ploop], 0      ; pattern-loop mode: never leave the pattern
    jne .set
    inc byte [mp_songpos]
    mov ah, [mp_songpos]
    cmp ah, [mp_songlen]
    jb .pat
    mov ah, [mp_restart]
    mov [mp_songpos], ah
.pat:
    push ax
    mov bl, [mp_songpos]
    xor bh, bh
    mov al, [mp_orders + bx]
    mov [mp_pattern], al
    pop ax
.set:
    mov [mp_row], al
    ret

; -----------------------------------------------------------------------------
; mp_readrow - tick 0: read the four cells of (pattern, row), trigger notes,
; run tick-0 effects
; -----------------------------------------------------------------------------
mp_readrow:
    ; ES = pattern segment for the whole row
    mov al, [mp_pattern]
    cmp al, [mp_npat]           ; defense-in-depth (the hostile-blob rule):
    jb .pok                     ; no state corruption may ever select a
    xor al, al                  ; pattern segment past the validated extent
.pok:
    xor ah, ah
    mov cl, 6
    shl ax, cl
    add ax, 67
    add ax, [mp_blobseg]
    mov es, ax
    mov word [mp_crec], mp_chans
    mov byte [mp_curc], 0
.ch:
    mov di, [mp_crec]
    ; SI = cell
    mov al, [mp_row]
    xor ah, ah
    mov cl, 4
    shl ax, cl
    mov si, ax
    mov al, [mp_curc]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    add si, ax
    add si, 12
    ; decode into mp_wper / mp_wsmp / MP_EFF / MP_PARM
    mov al, [es:si]             ; b0
    mov ah, [es:si+1]
    mov bl, [es:si+2]
    mov cl, [es:si+3]
    mov ch, al
    and ch, 0xF0
    mov dl, bl
    push cx
    mov cl, 4
    shr dl, cl
    pop cx
    or ch, dl
    and ch, 0x1F
    mov [mp_wsmp], ch
    and bl, 0x0F
    mov [di+MP_EFF], bl
    mov [di+MP_PARM], cl
    and al, 0x0F
    xchg al, ah
    mov [mp_wper], ax
    ; --- EDx (x>0): store the whole cell, skip note+instrument now ---------
    cmp bl, 0x0E
    jne .noted
    mov al, cl
    and al, 0xF0
    cmp al, 0xD0
    jne .noted
    mov al, cl
    and al, 0x0F
    jz .noted                   ; ED0 = play normally
    mov ax, [mp_wper]
    mov [di+MP_DPER], ax
    mov al, [mp_wsmp]
    mov [di+MP_DSMP], al
    jmp .fx0                    ; note delay: nothing else happens at tick 0
.noted:
    ; --- instrument: set current sample + reset volume ----------------------
    mov al, [mp_wsmp]
    or al, al
    jz .noinst
    mov [di+MP_SMP], al
    push bx
    call mp_smpent              ; AL 1..31 -> BX = sample table entry
    mov al, [bx+MS_VOL]
    mov [di+MP_VOL], al
    pop bx
.noinst:
    ; --- note ---------------------------------------------------------------
    mov ax, [mp_wper]
    or ax, ax
    jz .fx0
    cmp bl, 3                   ; 3xx/5xy: set the porta target, no retrigger
    je .porta
    cmp bl, 5
    je .porta
    ; 9xx computes the start offset before the trigger
    xor cx, cx
    cmp bl, 9
    jne .trig
    mov cl, [di+MP_PARM]
    or cl, cl
    jz .offold
    mov [di+MP_OFFM], cl
.offold:
    mov cl, [di+MP_OFFM]
    xor ch, ch
    push ax
    mov ax, cx
    mov cl, 8
    shl ax, cl                  ; offset memory << 8 = start bytes
    mov cx, ax
    pop ax
.trig:
    call mp_trig
    jmp .fx0
.porta:
    call mp_pclamp              ; AX -> clamped 113..856
    mov [di+MP_TARG], ax
.fx0:
    ; --- tick-0 effects -----------------------------------------------------
    mov bl, [di+MP_EFF]
    mov cl, [di+MP_PARM]
    cmp bl, 3
    je .f3
    cmp bl, 4
    je .f4
    cmp bl, 7
    je .f7
    cmp bl, 0x0B
    je .fB
    cmp bl, 0x0C
    je .fC
    cmp bl, 0x0D
    je .fD
    cmp bl, 0x0E
    je .fE
    cmp bl, 0x0F
    je .fF
    jmp .fxdone
.f3:
    or cl, cl
    jz .fxdone
    mov [di+MP_PSPD], cl
    jmp .fxdone
.f4:
    mov al, cl
    and al, 0xF0
    jz .f4y
    mov ah, [di+MP_VIBC]
    and ah, 0x0F
    or ah, al
    mov [di+MP_VIBC], ah
.f4y:
    mov al, cl
    and al, 0x0F
    jz .fxdone
    mov ah, [di+MP_VIBC]
    and ah, 0xF0
    or ah, al
    mov [di+MP_VIBC], ah
    jmp .fxdone
.f7:
    mov al, cl
    and al, 0xF0
    jz .f7y
    mov ah, [di+MP_TREC]
    and ah, 0x0F
    or ah, al
    mov [di+MP_TREC], ah
.f7y:
    mov al, cl
    and al, 0x0F
    jz .fxdone
    mov ah, [di+MP_TREC]
    and ah, 0xF0
    or ah, al
    mov [di+MP_TREC], ah
    jmp .fxdone
.fB:
    mov byte [mp_pbrow], 0      ; Bxx position jump
    mov byte [mp_posjmp], 1
    cmp byte [mp_ploop], 0      ; pattern-loop mode never leaves the pattern
    jne .fxdone                 ; (mp_nextrow's contract): Bxx restarts the
                                ; looped pattern from row 0, and the songpos
                                ; store is SKIPPED - the advance never ++s
                                ; it back, so param-1 (0xFF for a clamped
                                ; target of 0) would park there forever
    mov al, cl
    cmp al, [mp_songlen]
    jb .bok
    xor al, al
.bok:
    dec al                      ; songpos = target - 1; the advance ++s it
    mov [mp_songpos], al
    jmp .fxdone
.fC:
    mov al, cl                  ; Cxx set volume
    cmp al, 64
    jbe .cok
    mov al, 64
.cok:
    mov [di+MP_VOL], al
    jmp .fxdone
.fD:
    mov al, cl                  ; Dxx pattern break, BCD row
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    mov ah, 10
    mul ah
    mov ah, cl
    and ah, 0x0F
    add al, ah
    cmp al, 63
    jbe .dok
    xor al, al
.dok:
    mov [mp_pbrow], al
    mov byte [mp_posjmp], 1
    jmp .fxdone
.fE:
    mov al, cl
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    and cl, 0x0F                ; CL = x, AL = sub
    cmp al, 1
    je .e1
    cmp al, 2
    je .e2
    cmp al, 6
    je .e6
    cmp al, 0x0A
    je .eA
    cmp al, 0x0B
    je .eB
    cmp al, 0x0C
    je .eC
    cmp al, 0x0E
    je .eE
    jmp .fxdone
.e1:
    mov ax, [di+MP_PER]         ; fine porta up
    xor ch, ch
    sub ax, cx
    call mp_pclamp
    mov [di+MP_PER], ax
    jmp .fxdone
.e2:
    mov ax, [di+MP_PER]         ; fine porta down
    xor ch, ch
    add ax, cx
    call mp_pclamp
    mov [di+MP_PER], ax
    jmp .fxdone
.e6:
    or cl, cl                   ; E60: set loop start
    jnz .e6j
    mov al, [mp_row]
    mov [di+MP_LROW], al
    jmp .fxdone
.e6j:
    mov al, [di+MP_LCNT]
    or al, al
    jnz .e6c
    mov [di+MP_LCNT], cl        ; first pass: arm the counter, jump
    jmp .e6go
.e6c:
    dec al
    mov [di+MP_LCNT], al
    or al, al
    jz .fxdone                  ; loop spent
.e6go:
    mov al, [di+MP_LROW]
    mov [mp_pbrow], al
    mov byte [mp_pbreak], 1
    jmp .fxdone
.eA:
    mov al, [di+MP_VOL]         ; fine vol up
    add al, cl
    cmp al, 64
    jbe .eas
    mov al, 64
.eas:
    mov [di+MP_VOL], al
    jmp .fxdone
.eB:
    mov al, [di+MP_VOL]         ; fine vol down
    sub al, cl
    jnc .ebs
    xor al, al
.ebs:
    mov [di+MP_VOL], al
    jmp .fxdone
.eC:
    or cl, cl                   ; EC0: cut now
    jnz .fxdone
    mov byte [di+MP_VOL], 0
    jmp .fxdone
.eE:
    cmp byte [mp_patdel2], 0    ; EEx pattern delay (not while held)
    jne .fxdone
    or cl, cl
    jz .fxdone
    inc cl
    mov [mp_patdel], cl
    jmp .fxdone
.fF:
    or cl, cl                   ; F00 = stop
    jnz .fs
    mov byte [mp_playing], 0
    jmp .fxdone
.fs:
    cmp cl, 0x20
    jae .fbpm
    mov [mp_speed], cl
    jmp .fxdone
.fbpm:
    mov [mp_bpm], cl
    call mp_calc_spt
.fxdone:
    ; defaults: what the mixer hears this tick
    mov ax, [di+MP_PER]
    mov [di+MP_OPER], ax
    mov al, [di+MP_VOL]
    mov [di+MP_OVOL], al
    add word [mp_crec], MP_CHSZ
    inc byte [mp_curc]
    cmp byte [mp_curc], 4
    jae .rowdone
    jmp .ch
.rowdone:
    push ds
    pop es
    ret

; -----------------------------------------------------------------------------
; mp_smpent - AL = sample number 1..31 -> BX = its table entry
; -----------------------------------------------------------------------------
mp_smpent:
    push ax
    push cx
    dec al
    xor ah, ah
    mov bx, ax
    mov cl, 3
    shl ax, cl                  ; *8
    shl bx, 1
    shl bx, 1                   ; *4
    add bx, ax                  ; *12
    add bx, mp_smptab
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mp_pclamp - AX = period -> clamped to [113..856]
; -----------------------------------------------------------------------------
mp_pclamp:
    cmp ax, 113
    jae .lo
    mov ax, 113
.lo:
    cmp ax, 856
    jbe .out
    mov ax, 856
.out:
    ret

; -----------------------------------------------------------------------------
; mp_trig - trigger a note on channel DI: AX = raw period, CX = sample start
; offset (9xx; 0 otherwise). Loads the channel's sample fields from its
; current instrument, resets position and the vibrato/tremolo phases.
; -----------------------------------------------------------------------------
mp_trig:
    call mp_pclamp
    mov [di+MP_PER], ax
    mov byte [di+MP_VIBP], 0
    mov byte [di+MP_TREP], 0
    mov al, [di+MP_SMP]
    or al, al
    jz .dead
    cmp al, 31
    ja .dead
    push bx
    call mp_smpent
    mov ax, [bx+MS_SEG]
    mov [di+MP_SEG], ax
    mov ax, [bx+MS_OFF]
    mov [di+MP_OFF], ax
    mov ax, [bx+MS_PLEN]
    mov [di+MP_PLEN], ax
    mov dx, [bx+MS_LLEN]
    mov [di+MP_LLEN], dx
    pop bx
    or ax, ax
    jz .dead                    ; zero-length sample: silence, never indexed
    cmp cx, ax
    jae .dead                   ; 9xx start past the data: silence (no wrap)
    mov ax, [di+MP_OFF]
    add ax, cx
    mov [di+MP_POSI], ax
    mov word [di+MP_POSF], 0
    mov byte [di+MP_ACT], 1
    push bx                     ; the needle's impulse (SPEC.md 45.12.1), on
    mov bl, [mp_curc]           ; the SUCCESS path only - a trigger that fell
    xor bh, bh                  ; through to .dead makes no sound and must
    mov byte [mp_hit + bx], 1   ; light nothing. Here rather than at the three
    pop bx                      ; call sites because this is the one place a
    ret                         ; note actually starts: the initial trigger,
                                ; EDx's delayed one and E9x's retrigger all
                                ; arrive through it, each inside a loop that
                                ; maintains [mp_curc]
.dead:
    mov byte [di+MP_ACT], 0
    ret

; -----------------------------------------------------------------------------
; mp_tickfx - ticks 1..speed-1: the per-tick effects, all four channels
; -----------------------------------------------------------------------------
mp_tickfx:
    mov word [mp_crec], mp_chans
    mov byte [mp_curc], 0
.ch:
    mov di, [mp_crec]
    ; defaults first; the handlers below override
    mov ax, [di+MP_PER]
    mov [di+MP_OPER], ax
    mov al, [di+MP_VOL]
    mov [di+MP_OVOL], al
    mov bl, [di+MP_EFF]
    mov cl, [di+MP_PARM]
    or bl, bl
    jz .f0
    cmp bl, 1
    je .f1
    cmp bl, 2
    je .f2
    cmp bl, 3
    je .f3
    cmp bl, 4
    je .f4
    cmp bl, 5
    je .f5
    cmp bl, 6
    je .f6
    cmp bl, 7
    je .f7
    cmp bl, 0x0A
    je .fA
    cmp bl, 0x0E
    je .fE
    jmp .next
.f0:
    or cl, cl                   ; 0xy arpeggio (000 = empty cell)
    jz .next
    call mp_arp
    jmp .next
.f1:
    mov ax, [di+MP_PER]         ; 1xx porta up
    xor ch, ch
    sub ax, cx
    call mp_pclamp
    mov [di+MP_PER], ax
    mov [di+MP_OPER], ax
    jmp .next
.f2:
    mov ax, [di+MP_PER]         ; 2xx porta down
    xor ch, ch
    add ax, cx
    call mp_pclamp
    mov [di+MP_PER], ax
    mov [di+MP_OPER], ax
    jmp .next
.f3:
    call mp_portastep
    jmp .next
.f4:
    call mp_vibrato
    jmp .next
.f5:
    call mp_portastep           ; 5xy = porta (memory) + volume slide xy
    call mp_volslide
    jmp .next
.f6:
    call mp_vibrato             ; 6xy = vibrato (memory) + volume slide xy
    call mp_volslide
    jmp .next
.f7:
    call mp_tremolo
    jmp .next
.fA:
    call mp_volslide
    jmp .next
.fE:
    mov al, cl
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    and cl, 0x0F
    cmp al, 9
    je .e9
    cmp al, 0x0C
    je .eC
    cmp al, 0x0D
    je .eD
    jmp .next
.e9:
    or cl, cl                   ; E9x retrig every x ticks
    jz .next
    mov al, [mp_tick]
    xor ah, ah
    div cl
    or ah, ah
    jnz .next
    mov ax, [di+MP_PER]
    xor cx, cx
    call mp_trig
    jmp .next
.eC:
    cmp cl, [mp_tick]           ; ECx note cut at tick x
    jne .next
    mov byte [di+MP_VOL], 0
    mov byte [di+MP_OVOL], 0
    jmp .next
.eD:
    cmp cl, [mp_tick]           ; EDx delayed trigger at tick x
    jne .next
    mov al, [di+MP_DSMP]
    or al, al
    jz .edper
    cmp al, 31
    ja .edper
    mov [di+MP_SMP], al
    push bx
    call mp_smpent
    mov al, [bx+MS_VOL]
    mov [di+MP_VOL], al
    mov [di+MP_OVOL], al
    pop bx
.edper:
    mov ax, [di+MP_DPER]
    or ax, ax
    jz .next
    xor cx, cx
    call mp_trig
    mov ax, [di+MP_PER]
    mov [di+MP_OPER], ax
.next:
    add word [mp_crec], MP_CHSZ
    inc byte [mp_curc]
    cmp byte [mp_curc], 4
    jae .done
    jmp .ch
.done:
    ret

; --- per-tick effect bodies (DI = channel record; may clobber AX/CX/DX/SI) -----

; Axy / 5xy / 6xy volume slide: x up (wins), else y down; clamp 0..64
mp_volslide:
    mov al, [di+MP_PARM]
    mov ah, al
    push cx
    mov cl, 4
    shr ah, cl
    pop cx
    and al, 0x0F
    mov dl, [di+MP_VOL]
    or ah, ah
    jz .down
    add dl, ah
    cmp dl, 64
    jbe .st
    mov dl, 64
    jmp .st
.down:
    sub dl, al
    jnc .st
    xor dl, dl
.st:
    mov [di+MP_VOL], dl
    mov [di+MP_OVOL], dl
    ret

; 3xx tone portamento step toward MP_TARG
mp_portastep:
    mov ax, [di+MP_PER]
    mov dx, [di+MP_TARG]
    or dx, dx
    jz .out                     ; no target yet
    mov cl, [di+MP_PSPD]        ; load the speed BEFORE the compare: an XOR
    xor ch, ch                  ; between cmp and jb clears CF and turns the
    cmp ax, dx                  ; slide-down branch into dead code (every
    je .out                     ; glide toward a larger period snapped)
    jb .up                      ; period below target: slide down (larger)
    sub ax, cx
    jc .snap
    cmp ax, dx
    jae .st
.snap:
    mov ax, dx
    jmp .st
.up:
    add ax, cx
    cmp ax, dx
    jbe .st
    mov ax, dx
.st:
    mov [di+MP_PER], ax
    mov [di+MP_OPER], ax
.out:
    ret

; 4xy vibrato: delta = (vibtab[(pos>>2)&31] * depth) >> 7 on OPER only
mp_vibrato:
    mov al, [di+MP_VIBP]
    push cx
    mov cl, 2
    shr al, cl
    pop cx
    and al, 31
    mov bl, al
    xor bh, bh
    mov al, [mp_vibtab + bx]
    mov dl, [di+MP_VIBC]
    and dl, 0x0F
    mul dl                      ; AX = tab * depth (<= 3825)
    mov cl, 7
    shr ax, cl                  ; delta in PT period units
    mov dx, [di+MP_PER]
    test byte [di+MP_VIBP], 0x80
    jnz .minus
    add dx, ax
    jmp .clip
.minus:
    sub dx, ax
.clip:
    mov ax, dx
    call mp_pclamp
    mov [di+MP_OPER], ax
    ; pos += speed * 4
    mov al, [di+MP_VIBC]
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    shl al, 1
    shl al, 1
    add [di+MP_VIBP], al
    ret

; 7xy tremolo: delta = (vibtab[(pos>>2)&31] * depth) >> 6 on OVOL only
mp_tremolo:
    mov al, [di+MP_TREP]
    push cx
    mov cl, 2
    shr al, cl
    pop cx
    and al, 31
    mov bl, al
    xor bh, bh
    mov al, [mp_vibtab + bx]
    mov dl, [di+MP_TREC]
    and dl, 0x0F
    mul dl
    mov cl, 6
    shr ax, cl                  ; delta (<= 59)
    mov dl, [di+MP_VOL]
    xor dh, dh
    test byte [di+MP_TREP], 0x80
    jnz .minus
    add dx, ax
    cmp dx, 64
    jbe .st
    mov dx, 64
    jmp .st
.minus:
    sub dx, ax
    jnc .st
    xor dx, dx
.st:
    mov al, dl
    mov [di+MP_OVOL], al
    mov al, [di+MP_TREC]
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    shl al, 1
    shl al, 1
    add [di+MP_TREP], al
    ret

; 0xy arpeggio: tick%3 = 0 base note / 1 +x semitones / 2 +y semitones
mp_arp:
    mov al, [mp_tick]
    xor ah, ah
    mov dl, 3
    div dl
    or ah, ah
    jz .out                     ; base tick: OPER = PER (the default)
    mov dl, [di+MP_PARM]
    cmp ah, 1
    je .hi
    and dl, 0x0F                ; +y
    jmp .have
.hi:
    push cx
    mov cl, 4
    shr dl, cl                  ; +x
    pop cx
.have:
    mov ax, [di+MP_PER]
    call mp_pfind               ; BX = note index of the playing period
    add bl, dl
    cmp bl, 35
    jbe .idx
    mov bl, 35
.idx:
    xor bh, bh
    shl bx, 1
    mov ax, [mp_pertab + bx]
    mov [di+MP_OPER], ax
.out:
    ret

; -----------------------------------------------------------------------------
; mp_chupd - after every tick: recompute each channel's mixer step from its
; output period (3 DIVs, the research recipe - the [113..856] clamp is the
; #DE guard) and publish the UI-visible volume/period.
; -----------------------------------------------------------------------------
mp_chupd:
    mov word [mp_crec], mp_chans
    mov byte [mp_curc], 0
.ch:
    mov di, [mp_crec]
    mov bl, [mp_curc]
    xor bh, bh
    cmp byte [di+MP_ACT], 0
    je .quiet
    mov ax, [di+MP_OPER]
    call mp_pclamp
    push bx
    shl bx, 1
    mov [mp_chper + bx], ax     ; word slot
    pop bx
    push bx
    mov bx, ax
    mov dx, 0x0036
    mov ax, 0x1F0F              ; DX:AX = 3,546,895 (PAL clock / 2)
    div bx                      ; AX = playback Hz (<= 31388)
    xor dx, dx
    div word [mp_mixrate]       ; AX = step int, DX = rem
    mov [di+MP_STEPI], ax
    xor ax, ax                  ; DX:AX = rem << 16
    div word [mp_mixrate]
    mov [di+MP_STEPF], ax
    pop bx
    ; publish volume: 0 when muted, else the tick's output volume
    cmp byte [mp_mute + bx], 0
    jne .quietv
    mov al, [di+MP_OVOL]
    mov [mp_chvol + bx], al
    jmp .next
.quiet:
    shl bx, 1
    mov word [mp_chper + bx], 0
    shr bx, 1
.quietv:
    mov byte [mp_chvol + bx], 0
.next:
    add word [mp_crec], MP_CHSZ
    inc byte [mp_curc]
    cmp byte [mp_curc], 4
    jb .ch
    ret

; -----------------------------------------------------------------------------
; mp_calc_spt - samples per tick = mixrate * 5 / (2 * BPM)
; -----------------------------------------------------------------------------
mp_calc_spt:
    mov ax, [mp_mixrate]
    mov bx, 5
    mul bx                      ; DX:AX = mixrate * 5
    mov bl, [mp_bpm]
    xor bh, bh
    cmp bx, 32
    jae .bok
    mov bx, 32                  ; BPM floor guards the DIV (< 32 is
.bok:                           ; unreachable by encoding, F20 is BPM 32)
    shl bx, 1
    div bx                      ; AX = samples per tick (<= 5120)
    or ax, ax
    jnz .nz
    inc ax
.nz:
    mov [mp_spt], ax
    ret

; =============================================================================
; Tables
; =============================================================================

; finetune-0 ProTracker period table, 3 octaves (ft2_tables.c ptPeriods)
mp_pertab:
    dw 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453
    dw 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226
    dw 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113

; the PT/FT2 vibrato half-sine (vibratoTab, exact)
mp_vibtab:
    db   0,  24,  49,  74,  97, 120, 141, 161
    db 180, 197, 212, 224, 235, 244, 250, 253
    db 255, 253, 250, 244, 235, 224, 212, 197
    db 180, 161, 141, 120,  97,  74,  49,  24

mp_ntab:    db 'C-C#D-D#E-F-F#G-G#A-A#B-'
mp_hexd:    db '0123456789ABCDEF'

; the mp_load verdict strings (returned by offset in AX)
mpe_notmod: db 'Not a MOD', 0
mpe_trunc:  db 'Truncated file', 0
mpe_song:   db 'Bad song data', 0

; =============================================================================
; trkplay bss (through tracker.asm's TRKB/TRKW/TRKBUF accumulator)
; =============================================================================

; --- the pinned UI-visible state (interface order) -----------------------------
    TRKB mp_loaded                  ; a module is parsed and playable
    TRKB mp_playing                 ; the replayer is advancing
    TRKB mp_songpos                 ; order position
    TRKB mp_row                     ; row 0..63
    TRKB mp_pattern                 ; playing pattern number
    TRKB mp_speed                   ; ticks per row
    TRKB mp_bpm                     ; tempo
    TRKB mp_songlen                 ; order entries played
    TRKB mp_npat                    ; pattern count P
    TRKBUF mp_mute, 4               ; per-channel mute flags
    TRKBUF mp_chvol, 4              ; current output volumes 0..64 (0 = quiet)
    TRKBUF mp_hit, 4                ; a note STRUCK on this channel since the
                                    ; last stamp (SPEC.md 45.12.1) - mp_trig
                                    ; sets, mp_stamp_at reads and clears
    TRKBUF mp_chper, 8              ; current playing periods (0 = silent)
    TRKBUF mp_title, 21             ; module title, NUL-terminated
    TRKBUF mp_outbuf, 2048          ; mp_gen's rendered output

; --- the position stamps (SPEC.md 45.15) ---------------------------------------
    TRKBUF mp_stamps, MP_ST_N * MP_ST_SIZE
    TRKB mp_stw                     ; next stamp to write
    TRKB mp_str                     ; the one being heard (only moves forward)
    TRKB mp_rowadv                  ; mp_dotick began a row; mp_gen owes a
                                    ; stamp at its current output cursor
    TRKB mp_pendpos                 ; that row's order position and pattern,
    TRKB mp_pendpat                 ; caught before its effects could park them
    TRKW mp_stampbase               ; the stream position mp_outbuf[0] holds -
                                    ; the app's, because the replayer knows
                                    ; nothing about the stream
    TRKBUF mp_celltxt, 11           ; mp_cell2txt's 'C-2 01 A0F' + NUL
    TRKW mp_blobseg                 ; the module blob (set by tracker.asm)
    TRKW mp_bloblen_lo              ; ...and its 32-bit byte length
    TRKW mp_bloblen_hi
    TRKW mp_mixrate                 ; mixer output rate, Hz

; --- replayer internals --------------------------------------------------------
    TRKB mp_restart                 ; validated restart position
    TRKB mp_ploop                   ; 1 = pattern-loop mode (P key)
    TRKB mp_tick                    ; tick 0..speed-1
    TRKB mp_patdel                  ; EEx pending delay
    TRKB mp_patdel2                 ; ...and the held-row counter
    TRKB mp_pbreak                  ; E6x same-pattern jump flag
    TRKB mp_pbrow                   ; break/loop target row
    TRKB mp_posjmp                  ; Bxx/Dxx position advance flag
    TRKB mp_curc                    ; channel loop counter
    TRKB mp_tvol                    ; volume-table build counter
    TRKB mp_xt                      ; XT mode (SPEC.md 45.9): 0 = default
    TRKB mp_first                   ; XT chunk: next audible channel stores
    TRKW mp_spt                     ; samples per tick
    TRKW mp_tickrem                 ; samples left in the current tick
    TRKW mp_need                    ; mp_gen bytes remaining
    TRKW mp_outp                    ; mp_gen output cursor
    TRKW mp_chunk                   ; current mix chunk, samples
    TRKW mp_crec                    ; current channel record
    TRKW mp_climit                  ; mixer scratch: position limit
    TRKW mp_cloopl                  ;   loop length
    TRKW mp_cstepi                  ;   step int
    TRKW mp_crem                    ;   samples left after the 4-at-a-time run
    TRKW mp_cstepf                  ;   step fraction
    TRKW mp_wper                    ; row decode scratch: period
    TRKB mp_wsmp                    ;   sample
    TRKW mp_run_lo                  ; loader: running 32-bit sample offset
    TRKW mp_run_hi
    TRKW mp_t1                      ; loader scratch
    TRKW mp_t2
    TRKW mp_t3
    TRKW mp_t4
    TRKW mp_t5
    TRKW mp_t6
    TRKBUF mp_orders, 128           ; the order table, copied out of the blob
    TRKBUF mp_smptab, MS_SZ * 31    ; the validated sample table
    TRKBUF mp_chans, MP_CHSZ * 4    ; the four channel records
    TRKBUF mp_mixbuf, MP_CHUNK * 2  ; 16-bit accumulator
    TRKBUF mp_voltab, 65 * 256      ; vt[vol][byte] = (int8)byte * vol >> 6
