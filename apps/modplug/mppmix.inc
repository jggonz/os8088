; =============================================================================
; os8088 - apps/modplug/mppmix.inc
;
; ModPlug Player's replayer third (SPEC.md 52): ProTracker MOD loader/
; validator, the 4-channel replayer, and the 8-bit mono mixer. Prefix mpm_.
; Included by modplug.asm AFTER the MPPB/MPPW/MPPBUF bss macros; all state is
; package bss declared through them at the end of this file.
;
; PROVENANCE, because it matters for who fixes what. ModPlugPlayer's own
; playback engine is libopenmpt - a modern C++ library pulled in by its
; CMakeLists, not source in that repository and not a thing a 4.77 MHz 8086
; runs. So this file is the tree's own proven 8086 replayer
; (apps/tracker/trkplay.inc, SPEC.md 45.5) taken as the starting point and
; then given the DSP stages ModPlugPlayer's Setup window actually exposes -
; see "The ModPlug DSP tail" below. It is an INDEPENDENT copy, deliberately
; (SPEC.md 52.1): the two players are allowed to diverge, and a replayer bug
; fixed in one is not automatically fixed in the other. Anything below that
; is not marked as a ModPlug addition is byte-identical to trkplay.inc with
; its mp_/MP_/MS_ prefixes renamed to mpm_/MPM_/MPS_.
;
; THE ModPlug DSP TAIL (SPEC.md 52.5). Four settings out of ModPlugPlayer's
; Setup > Audio and Setup > DSP pages, each landing where it is honest on
; this hardware and each costing NOTHING while it is off:
;
;   [mpm_interp] Setup > Audio > Resampling > Interpolation Mode. 0 = none
;       (the mixer's own nearest-neighbour fetch, and the default), 1 =
;       linear. Linear is a SECOND inner loop, not a flag inside the first,
;       because the first one is the hot loop: `imul r/m8` is ~80-98 cycles
;       on an 8088 and four channels at 11 kHz would spend 4.4M cycles a
;       second on interpolation alone. That is why the Setup page greys it
;       on a tier-0 machine and why Cubic and 8-tap Sinc are greyed on every
;       machine - SPEC.md 47's rule 4, a fact and not a guess.
;   [mpm_dither] Setup > Audio > Dither. Real here and nowhere else in the
;       tree: the default path mixes into a 16-bit accumulator and truncates
;       2 bits away, so 2 bits of LFSR noise added BEFORE the shift is
;       textbook rounding dither. Its own conversion loop, so the undithered
;       path keeps exactly its old cost.
;   [mpm_amiga] Setup > Player > Amiga Filter Type. 0 = unfiltered, 1 =
;       Amiga 500, 2 = Amiga 1200. A one-pole `y += (x - y) >> k` over the
;       finished chunk - k = 1 for the 500's heavy LED filter, k = 2 for the
;       1200's gentler one. Not Paula, and the Setup page says so.
;   [mpm_crush] Setup > DSP > Bit Crush, 0 = off else the bits KEPT (1..7).
;       One AND per byte, in the same tail pass as the filter.
;
; The tail runs over the finished output in mpm_post, once per mpm_gen, so a
; mode change can never split a chunk. XT mode (SPEC.md 52.7) forces every
; one of them off: they are all per-output-sample costs and XT mode exists
; to buy per-output-sample costs back.
;
; Register contract (pinned): near procs, DS = ES = package segment on
; entry; DS and SS are always preserved, every other register may be
; clobbered unless a proc documents an output. Every proc except mpm_insname
; (which returns ES:SI into the blob by contract) leaves ES = DS.
;
; THE BLOB IS HOSTILE (SPEC.md 18's rule, applied to a file a stranger
; wrote): mpm_load validates per the 13-point checklist in the research
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
; off = start & 15, with its play length capped at MPM_MAXSMP bytes so
; off + position (+ one mix step) can never wrap a 16-bit offset.
;
; Timing (research-ft2-reference): tick rate = BPM*2/5 Hz, speed = ticks
; per row; mpm_gen renders [mpm_spt] = mixrate*5/(2*BPM) samples per tick.
; The tick model counts UP: tick 0 reads the row and runs tick-0 effects,
; ticks 1..speed-1 run the per-tick effects, the row advances when the
; count wraps. Mixing is per-channel accumulate into a 16-bit chunk
; buffer through the 65x256 volume table (built at load), then one
; conversion pass: out = 128 + (sum >> 2).
; =============================================================================

MPM_MAXSMP  equ 60000            ; per-sample play-length cap, bytes: keeps
                                ; off + pos + step*chunk < 65536 in the mixer
                                ; even at a 4000 Hz mix rate (step int <= 7)
MPM_CHUNK   equ 512              ; mix chunk cap, samples (mpm_mixbuf = 2x)

; --- channel record (bss, MPM_CHSZ bytes each, 4 of them) -----------------------
MPM_PER    equ 0                 ; w: real period (persistent)
MPM_OPER   equ 2                 ; w: output period (vibrato/arp write this)
MPM_TARG   equ 4                 ; w: 3xx tone-porta target
MPM_SEG    equ 6                 ; w: playing sample's base segment
MPM_OFF    equ 8                 ; w: ...base offset (0..15)
MPM_PLEN   equ 10                ; w: play length, bytes (loop end / one-shot end)
MPM_LLEN   equ 12                ; w: loop length, bytes (0 = one-shot)
MPM_POSI   equ 14                ; w: position = MPM_OFF + byte index (int part)
MPM_POSF   equ 16                ; w: position fraction (16.16 low word)
MPM_STEPI  equ 18                ; w: step int
MPM_STEPF  equ 20                ; w: step fraction
MPM_VOL    equ 22                ; b: real volume 0..64
MPM_OVOL   equ 23                ; b: output volume (tremolo writes this)
MPM_ACT    equ 24                ; b: 1 = sample playing
MPM_SMP    equ 25                ; b: current instrument 1..31 (0 = none)
MPM_EFF    equ 26                ; b: this row's effect
MPM_PARM   equ 27                ; b: ...and param
MPM_VIBP   equ 28                ; b: vibrato table position (signed byte)
MPM_VIBC   equ 29                ; b: vibrato speed<<4 | depth (sticky nibbles)
MPM_TREP   equ 30                ; b: tremolo position
MPM_TREC   equ 31                ; b: tremolo speed<<4 | depth
MPM_PSPD   equ 32                ; b: 3xx porta speed (sticky)
MPM_OFFM   equ 33                ; b: 9xx offset memory
MPM_LROW   equ 34                ; b: E6x loop start row
MPM_LCNT   equ 35                ; b: E6x loop counter
MPM_DPER   equ 36                ; w: EDx delayed cell's period
MPM_DSMP   equ 38                ; b: ...and sample
MPM_CHSZ   equ 40

; --- sample table entry (bss, 12 bytes x 31) -----------------------------------
MPS_SEG    equ 0                 ; w: normalized base segment
MPS_OFF    equ 2                 ; w: base offset 0..15
MPS_PLEN   equ 4                 ; w: play length, bytes (0 = unplayable)
MPS_LLEN   equ 6                 ; w: loop length, bytes (0 = one-shot)
MPS_LSTART equ 8                 ; w: loop start, bytes (kept for reference)
MPS_VOL    equ 10                ; b: header volume, clamped 0..64
MPS_FINE   equ 11                ; b: finetune nibble (stored, ignored in v1)
MPS_SZ     equ 12

; =============================================================================
; mpm_init - once, from the entry proc. The loader zeroed the bss; only the
; nonzero defaults are set here.
; =============================================================================
mpm_init:
    mov byte [mpm_speed], 6
    mov byte [mpm_bpm], 125
    mov word [mpm_mixrate], 11000
    mov word [mpm_dseed], 0xACE1    ; the dither LFSR: any nonzero seed will
    mov byte [mpm_master], 64       ; do, but zero is a fixed point
    ret

; =============================================================================
; mpm_load - parse + validate the blob at [mpm_blobseg] (32-bit length in
; [mpm_bloblen_hi]:[mpm_bloblen_lo]), build the sample table and the volume
; tables, set mpm_loaded = 1.
; out: CF=0 loaded; CF=1 AX = offset of a NUL error string. ES = DS.
; =============================================================================
mpm_load:
    mov byte [mpm_loaded], 0
    ; -- 1: L >= 1084 ------------------------------------------------------
    cmp word [mpm_bloblen_hi], 0
    jne .lenok
    cmp word [mpm_bloblen_lo], 1084
    jae .lenok
    jmp .e_notmod
.lenok:
    mov es, [mpm_blobseg]
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
    mov ax, mpme_notmod
    stc
    ret
.magic:
    ; -- title: 20 hostile bytes -> mpm_title, printable, NUL-terminated ------
    xor si, si
    mov di, mpm_title
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
    mov ax, mpme_song
    stc
    ret
.slok:
    mov [mpm_songlen], al
    mov ah, [es:951]
    cmp ah, al
    jb .rsok
    xor ah, ah
.rsok:
    mov [mpm_restart], ah
    ; -- 6: copy the order table, P = max over ALL 128 entries + 1 -----------
    mov si, 952
    mov di, mpm_orders
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
    mov [mpm_t1], ax
    ; extent = 1084 + 1024*P (32-bit), must be <= L
    mov dx, ax
    mov cl, 10
    shl ax, cl                  ; low word of P<<10
    mov cl, 6
    shr dx, cl                  ; high word of P<<10
    add ax, 1084
    adc dx, 0                   ; DX:AX = pattern-block end = sample start
    cmp dx, [mpm_bloblen_hi]
    ja .e_trunc
    jb .extok
    cmp ax, [mpm_bloblen_lo]
    jbe .extok
.e_trunc:
    push ds
    pop es
    mov ax, mpme_trunc
    stc
    ret
.extok:
    mov [mpm_run_lo], ax         ; 32-bit running sample start offset
    mov [mpm_run_hi], dx
    mov ax, [mpm_t1]
    mov [mpm_npat], al           ; P <= 126 after the extent check (file cap)
    ; -- 7..10: the 31 sample headers -> mpm_smptab ---------------------------
    mov word [mpm_t1], 0         ; sample index 0..30
.sm:
    mov ax, [mpm_t1]
    mov bx, ax
    mov cl, 5
    shl ax, cl                  ; *32
    shl bx, 1                   ; *2
    sub ax, bx                  ; *30
    add ax, 20
    mov si, ax                  ; SI = header offset in blob
    ; DI = table entry
    mov ax, [mpm_t1]
    mov bx, ax
    mov cl, 3
    shl ax, cl                  ; *8
    shl bx, 1
    shl bx, 1                   ; *4
    add ax, bx                  ; *12
    add ax, mpm_smptab
    mov di, ax
    ; length in words, big-endian -> DX:AX = bytes (17-bit)
    mov ax, [es:si+22]
    xchg al, ah
    xor dx, dx
    shl ax, 1
    adc dx, 0                   ; DX:AX = stated byte length
    mov [mpm_t2], ax             ; stated length lo
    mov [mpm_t3], dx             ; ...hi
    ; finetune + volume
    mov al, [es:si+24]
    and al, 0x0F
    mov [di+MPS_FINE], al
    mov al, [es:si+25]
    cmp al, 64
    jbe .vok
    mov al, 64
.vok:
    mov [di+MPS_VOL], al
    ; loop start / length, words BE -> bytes, each capped at 0xFFFF
    mov ax, [es:si+26]
    xchg al, ah
    shl ax, 1
    jnc .lsok
    mov ax, 0xFFFF              ; >64K start: lands >= plen -> loop disabled
.lsok:
    mov [mpm_t4], ax             ; ls bytes
    mov ax, [es:si+28]
    xchg al, ah
    shl ax, 1
    jnc .llok
    mov ax, 0xFFFF
.llok:
    mov [mpm_t5], ax             ; ll bytes
    ; normalized base from the running 32-bit start
    mov ax, [mpm_run_lo]
    mov dx, [mpm_run_hi]
    mov bx, ax
    and bx, 15
    mov [di+MPS_OFF], bx
    mov cx, 4
.sh4:
    shr dx, 1
    rcr ax, 1
    loop .sh4                   ; AX = start >> 4 (hi bits beyond 20 only on
    add ax, [mpm_blobseg]        ; hostile files whose samples are unplayable)
    mov [di+MPS_SEG], ax
    ; effective length = min(stated, bytes remaining in file, MPM_MAXSMP)
    mov ax, [mpm_bloblen_lo]
    mov dx, [mpm_bloblen_hi]
    sub ax, [mpm_run_lo]
    sbb dx, [mpm_run_hi]
    jc .zavail                  ; start already past EOF: nothing here
    or dx, dx
    jz .avlo
    mov ax, 0xFFFF              ; >= 64K available: the cap below decides
.avlo:
    jmp .haveav
.zavail:
    xor ax, ax
.haveav:
    ; AX = available (capped 0xFFFF); stated in mpm_t3:mpm_t2
    cmp word [mpm_t3], 0
    jne .stbig
    cmp ax, [mpm_t2]
    jbe .avwins
    mov ax, [mpm_t2]
    jmp .lenset
.stbig:
    jmp .lenset                 ; stated >= 64K: available (in AX) wins
.avwins:
.lenset:
    cmp ax, MPM_MAXSMP
    jbe .capok
    mov ax, MPM_MAXSMP
.capok:
    mov [mpm_t6], ax             ; elen
    ; advance the running start by the STATED length (the file layout)
    mov ax, [mpm_t2]
    mov dx, [mpm_t3]
    add [mpm_run_lo], ax
    adc [mpm_run_hi], dx
    ; loop fix-ups on elen
    mov ax, [mpm_t4]             ; ls
    mov bx, [mpm_t5]             ; ll
    mov cx, [mpm_t6]             ; elen
    cmp ax, cx
    jae .oneshot                ; loop starts past the data: no loop
    add ax, bx                  ; ls + ll (17-bit possible)
    jc .trim
    cmp ax, cx
    jbe .lfit
.trim:
    mov bx, cx
    sub bx, [mpm_t4]             ; ll = elen - ls
    mov ax, cx                  ; loop end = elen
.lfit:
    or bx, bx
    jz .oneshot                 ; zero loop length: one-shot over full elen
    cmp ax, 2                   ; AX = loop end (ls+ll, or elen after .trim)
    jbe .oneshot                ; ft2 rule (ft2_load_mod.c:308): looping iff
                                ; loopStart + loopLength > 2 bytes - a
                                ; 1-word loop at a nonzero start DOES loop
    mov [di+MPS_PLEN], ax        ; play length = loop end
    mov [di+MPS_LLEN], bx
    mov ax, [mpm_t4]
    mov [di+MPS_LSTART], ax
    jmp .snext
.oneshot:
    mov ax, [mpm_t6]
    mov [di+MPS_PLEN], ax
    mov word [di+MPS_LLEN], 0
    mov word [di+MPS_LSTART], 0
.snext:
    inc word [mpm_t1]
    cmp word [mpm_t1], 31
    jae .smdone
    jmp .sm
.smdone:
    push ds
    pop es
    call mpm_vtbuild             ; the 65x256 volume table, in the current
                                ; mode's format (SPEC.md 45.9)
    ; -- done ----------------------------------------------------------------
    mov byte [mpm_songpos], 0
    mov byte [mpm_row], 0
    mov bx, [mpm_orders]         ; BL = orders[0]
    mov al, bl
    mov [mpm_pattern], al
    mov byte [mpm_loaded], 1
    clc
    ret

; -----------------------------------------------------------------------------
; mpm_vtbuild - the 65x256 volume table for the current mode (SPEC.md 45.9):
;   default: vt[vol][b] = (int8)b * vol >> 6 (+/-127; 16-bit accumulate,
;            out = 128 + (sum >> 2))
;   XT:      vt[vol][b] = (int8)b * vol >> 8 (+/-31; four channels sum in
;            a byte around the 0x80 bias - the output stage pre-scaled away)
; clobbers AX/BX/CX/DI
; -----------------------------------------------------------------------------
mpm_vtbuild:
    mov di, mpm_voltab
    mov byte [mpm_tvol], 0
    mov cl, 6
    cmp byte [mpm_xt], 0
    je .vt_v
    mov cl, 8
.vt_v:
    xor bx, bx                  ; BL = source byte 0..255
.vt_b:
    mov al, bl
    imul byte [mpm_tvol]         ; AX = (int8)b * vol
    sar ax, cl
    mov [di], al
    inc di
    inc bl
    jnz .vt_b
    inc byte [mpm_tvol]
    cmp byte [mpm_tvol], 65
    jb .vt_v
    ret

; -----------------------------------------------------------------------------
; mpm_setxt - set XT mode (AL = 0/1; SPEC.md 45.9) and rebuild the volume
; table to the mode's format when a module is loaded. UI context, playback
; stopped by the caller: the 16,640-imul rebuild is a deliberate sub-second
; freeze on the machines the mode exists for. Clobbers AX/BX/CX/DI.
; -----------------------------------------------------------------------------
mpm_setxt:
    mov [mpm_xt], al
    or al, al                       ; ModPlug (SPEC.md 52.7): XT mode forces
    jz .built                       ; the whole DSP tail off, and does it HERE
    mov byte [mpm_interp], 0        ; rather than in the Setup window, so no
    mov byte [mpm_dither], 0        ; path into XT mode - key, menu, option
    mov byte [mpm_amiga], 0         ; button or the tier-0 pre-arm - can leave
    mov byte [mpm_crush], 0         ; a per-output-sample cost switched on in
    mov word [mpm_flt], 0           ; the mode whose entire purpose is buying
.built:                             ; those cycles back
    cmp byte [mpm_loaded], 0
    je .out
    call mpm_vtbuild
.out:
    ret

; =============================================================================
; mpm_start - reset the replayer and start playing
; in: AL = 0 play song from position 0 / 1 loop the current pattern
; =============================================================================
mpm_start:
    cmp byte [mpm_loaded], 0
    jne .ok
    ret
.ok:
    mov [mpm_ploop], al
    or al, al
    jnz .keep
    mov byte [mpm_songpos], 0
.keep:
    ; zero the four channel records
    push ds
    pop es
    cld
    mov di, mpm_chans
    mov cx, (MPM_CHSZ * 4) / 2
    xor ax, ax
    rep stosw
    mov byte [mpm_row], 0
    mov byte [mpm_tick], 0
    mov byte [mpm_speed], 6
    mov byte [mpm_bpm], 125
    mov byte [mpm_patdel], 0
    mov byte [mpm_patdel2], 0
    mov byte [mpm_pbreak], 0
    mov byte [mpm_pbrow], 0
    mov byte [mpm_posjmp], 0
    mov bl, [mpm_songpos]
    xor bh, bh
    and bx, 127                 ; load-bearing, not hygiene: a Bxx row holds
                                ; songpos at param-1 (0xFF when the clamped
                                ; target is 0) for its remaining ticks, and
                                ; P pressed in that window reaches here -
                                ; unmasked it indexes 127 bytes past
                                ; mpm_orders. Every orders byte names a
                                ; validated pattern (mpm_load's P is the max
                                ; over all 128), so the mask is safe
    mov al, [mpm_orders + bx]
    mov [mpm_pattern], al
    call mpm_calc_spt
    mov ax, [mpm_spt]
    mov [mpm_tickrem], ax
    call mpm_readrow             ; row 0 tick 0 happens NOW; the first
    call mpm_chupd               ; mpm_gen samples already carry it
    mov byte [mpm_playing], 1
    ret

; =============================================================================
; mpm_stop
; =============================================================================
mpm_stop:
    mov byte [mpm_playing], 0
    xor ax, ax
    mov [mpm_chvol], ax
    mov [mpm_chvol + 2], ax
    mov [mpm_chper], ax
    mov [mpm_chper + 2], ax
    mov [mpm_chper + 4], ax
    mov [mpm_chper + 6], ax
    ret

; =============================================================================
; mpm_setpos - AL = signed delta to song position, clamped to 0..songlen-1
; =============================================================================
mpm_setpos:
    cmp byte [mpm_loaded], 0
    je .out
    mov ah, [mpm_songpos]
    add ah, al                  ; signed byte math; clamp BOTH ends below
    js .wrap
    cmp ah, [mpm_songlen]
    jb .have
.top:
    mov ah, [mpm_songlen]
    dec ah
    jmp .have
.wrap:
    or al, al                   ; AL survives the add: sign set by a positive
    jns .top                    ; delta means 127+1 overflowed (songlen=128 -
    xor ah, ah                  ; the one legal length that can) - clamp to
                                ; songlen-1, not position 0
.have:
    mov [mpm_songpos], ah
    mov bl, ah
    xor bh, bh
    mov al, [mpm_orders + bx]
    mov [mpm_pattern], al
    mov byte [mpm_pbreak], 0
    mov byte [mpm_posjmp], 0
    mov byte [mpm_pbrow], 0
    cmp byte [mpm_playing], 0
    jne .out                    ; playing: the row chain continues there
    mov byte [mpm_row], 0
.out:
    ret

; =============================================================================
; mpm_setposn - ModPlug addition: seek to an ABSOLUTE order position
; in:  AL = the position wanted; clamped to the song's own length
;
; mpm_setpos above takes a DELTA, which is what a Prev/Next key wants. The
; time scrubber wants the other thing: the user grabbed a point on a bar and
; the position is whatever that point maps to. Expressing it as a delta would
; mean the scrubber computing (target - current) from a [mpm_songpos] the
; worker is moving underneath it - the classic read-modify-write against a
; value another task owns. So the absolute set is its own entry point, and it
; is the delta path's tail with the arithmetic taken off the front.
; =============================================================================
mpm_setposn:
    cmp byte [mpm_loaded], 0
    je .out
    cmp byte [mpm_songlen], 0       ; a validated module has songlen >= 1, but
    je .out                         ; the dec below must never underflow
    mov ah, al
    cmp ah, [mpm_songlen]
    jb .have
    mov ah, [mpm_songlen]
    dec ah
.have:
    mov [mpm_songpos], ah
    mov bl, ah
    xor bh, bh
    mov al, [mpm_orders + bx]
    mov [mpm_pattern], al
    mov byte [mpm_pbreak], 0
    mov byte [mpm_posjmp], 0
    mov byte [mpm_pbrow], 0
    cmp byte [mpm_playing], 0
    jne .out                        ; playing: the row chain continues there
    mov byte [mpm_row], 0
.out:
    ret

; =============================================================================
; mpm_mutetog - AL = channel 0..3
; =============================================================================
mpm_mutetog:
    cmp al, 3
    ja .out
    mov bl, al
    xor bh, bh
    xor byte [mpm_mute + bx], 1
.out:
    ret

; =============================================================================
; mpm_order - AL = order index -> AL = pattern number there
; =============================================================================
mpm_order:
    mov bl, al
    xor bh, bh
    and bx, 127
    mov al, [mpm_orders + bx]
    ret

; =============================================================================
; mpm_insname - AL = instrument 1..31 -> ES:SI = its 22-byte name field in
; the blob (raw, not NUL-guaranteed), CF=1 none/invalid. CALLER restores ES.
; =============================================================================
mpm_insname:
    cmp byte [mpm_loaded], 0
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
    mov es, [mpm_blobseg]
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; mpm_cell2txt - in AL = row 0..63, AH = channel 0..3, DI = pattern number;
; fills mpm_celltxt with 'C-2 01 A0F' + NUL. CF=1 no module / out of range.
; =============================================================================
mpm_cell2txt:
    cmp byte [mpm_loaded], 0
    je .no
    cmp al, 63
    ja .no
    cmp ah, 3
    ja .no
    mov bl, [mpm_npat]
    xor bh, bh
    cmp di, bx
    jae .no
    ; ES = pattern segment, SI = cell offset
    mov bx, di
    mov cl, 6
    shl bx, cl                  ; pattern * 64 paragraphs
    add bx, 67
    add bx, [mpm_blobseg]
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
    mov di, mpm_celltxt
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
    call mpm_pfind               ; AX period -> BX = note index 0..35
    mov ax, bx
    mov bl, 12
    div bl                      ; AL = octave, AH = semitone
    mov dl, al
    mov al, ah
    xor ah, ah
    shl ax, 1
    mov si, ax
    mov al, [mpm_ntab + si]
    mov [di], al
    mov al, [mpm_ntab + si + 1]
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
    call mpm_hexb
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
    mov al, [mpm_hexd + bx]
    pop bx
    mov [di+7], al
    mov al, cl
    push di
    add di, 8
    call mpm_hexb
    pop di
.done:
    mov byte [di+10], 0
    clc
    ret
.no:
    stc
    ret

; mpm_hexb - AL as two uppercase hex digits at DI (module-local helper)
mpm_hexb:
    push ax
    push bx
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    mov bl, al
    xor bh, bh
    mov al, [mpm_hexd + bx]
    mov [di], al
    mov bl, ah
    and bl, 0x0F
    mov al, [mpm_hexd + bx]
    mov [di+1], al
    pop cx
    pop bx
    pop ax
    ret

; mpm_pfind - AX = period -> BX = note index 0..35 (first entry <= period;
; nonstandard periods match nearest below, per the ft2 loader)
mpm_pfind:
    xor bx, bx
.l:
    cmp bx, 35
    jae .out
    push bx
    shl bx, 1
    cmp ax, [mpm_pertab + bx]
    pop bx
    jae .out
    inc bx
    jmp .l
.out:
    ret

; =============================================================================
; mpm_gen - render CX bytes (even, <= 2048) of unsigned 8-bit mono into
; mpm_outbuf and advance the replayer. Silence when not playing.
; =============================================================================
mpm_gen:
    cld
    push bp                     ; mpm_mixch_xt keeps the frac step in BP
    push ds
    pop es
    mov word [mpm_outp], mpm_outbuf ; BEFORE the early-out: mpm_post measures
                                    ; the produced range as outp - outbuf, and
                                    ; a stale outp would have it re-filter the
                                    ; PREVIOUS call's bytes under the caller
    or cx, cx
    jnz .have
    jmp .done
.have:
    cmp cx, 2048
    jbe .cxok
    mov cx, 2048
.cxok:
    mov [mpm_need], cx
.big:
    mov cx, [mpm_need]
    or cx, cx
    jz .done
    cmp byte [mpm_playing], 0
    je .silence
    cmp byte [mpm_loaded], 0
    je .silence
    ; chunk = min(need, tickrem, MPM_CHUNK)
    mov ax, cx
    cmp ax, [mpm_tickrem]
    jbe .c1
    mov ax, [mpm_tickrem]
.c1:
    cmp ax, MPM_CHUNK
    jbe .c2
    mov ax, MPM_CHUNK
.c2:
    mov [mpm_chunk], ax
    cmp byte [mpm_xt], 0         ; XT mode mixes straight into the outbuf
    jne .xtmix                  ; (SPEC.md 45.9) - no zero, no convert
    ; zero the accumulator
    mov di, mpm_mixbuf
    mov cx, ax
    xor ax, ax
    rep stosw
    ; mix the four channels
    mov word [mpm_crec], mpm_chans
    mov byte [mpm_curc], 0
.mixc:
    call mpm_mixch
    add word [mpm_crec], MPM_CHSZ
    inc byte [mpm_curc]
    cmp byte [mpm_curc], 4
    jb .mixc
    ; convert: out = 128 + (sum >> 2)
    mov si, mpm_mixbuf
    mov di, [mpm_outp]
    mov cx, [mpm_chunk]
    cmp byte [mpm_dither], 0        ; ModPlug (SPEC.md 52.5): dither gets its
    jne .cvd                        ; OWN loop, so this one keeps its cost
.cv:
    lodsw
    sar ax, 1
    sar ax, 1
    add ax, 128
    stosb
    loop .cv
    jmp .adv
    ; --- ModPlug addition: the dithered conversion (Setup > Audio > Dither) --
    ; Two bits are about to be shifted away, so two bits of noise added first
    ; is rounding dither rather than truncation. BP holds the LFSR because the
    ; loop has no spare register and BP is already saved by mpm_gen; it is
    ; never a memory base here, so SS != DS cannot bite (SPEC.md 2.1).
.cvd:
    mov bp, [mpm_dseed]
.cvdl:
    lodsw
    mov bx, bp                      ; x16 + x14 + x13 + x11 Galois LFSR
    shr bx, 1
    jnc .cvdn
    xor bx, 0xB400
.cvdn:
    mov bp, bx
    and bx, 3                       ; 0..3, recentred to -2..+1 so the dither
    sub bx, 2                       ; adds noise and not a DC step
    add ax, bx
    sar ax, 1
    sar ax, 1
    add ax, 128
    stosb
    loop .cvdl
    mov [mpm_dseed], bp
.adv:
    mov [mpm_outp], di
    mov ax, [mpm_chunk]
    sub [mpm_need], ax
    sub [mpm_tickrem], ax
    jnz .big
    call mpm_dotick              ; a tick boundary
    mov ax, [mpm_spt]
    mov [mpm_tickrem], ax
    jmp .big
.xtmix:
    mov byte [mpm_first], 1      ; the first audible channel STORES the
    mov word [mpm_crec], mpm_chans; biased byte, later ones ADD (45.9)
    mov byte [mpm_curc], 0
.xmc:
    call mpm_mixch_xt
    add word [mpm_crec], MPM_CHSZ
    inc byte [mpm_curc]
    cmp byte [mpm_curc], 4
    jb .xmc
    cmp byte [mpm_first], 0      ; nothing audible rendered: the chunk is
    je .xdone                   ; the bias alone
    mov di, [mpm_outp]
    mov cx, [mpm_chunk]
    mov al, 0x80
    rep stosb
.xdone:
    mov di, [mpm_outp]
    add di, [mpm_chunk]
    jmp .adv
.silence:
    mov di, [mpm_outp]
    mov al, 0x80
    rep stosb                   ; CX = remaining need
    mov word [mpm_need], 0
.done:
    call mpm_post                   ; ModPlug DSP tail (SPEC.md 52.5) over the
    push ds                         ; finished chunk: Amiga filter, bit crush
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; mpm_post - the ModPlug DSP tail, over mpm_outbuf .. [mpm_outp] (SPEC.md 52.5)
;
; ModPlug addition. Runs once per mpm_gen, after every chunk of the call is
; rendered, so a setting changed between calls can never split a buffer. Both
; stages are off by default and the whole routine is then two compares.
;
; Amiga filter: one pole, y += (x - y) >> k, k = 1 (Amiga 500's LED filter) or
; 2 (Amiga 1200's gentler one). y is carried in [mpm_flt] scaled by 256 so the
; pole keeps 8 fractional bits across calls - at k = 2 an 8-bit state would
; quantise the feedback to nothing and the filter would do audibly less than
; the setting claims. It is a lowpass in the Amiga's spirit; it is NOT Paula
; emulation, and the Setup page's own text says so rather than implying it.
;
; Bit crush: [mpm_crush] is the count of bits KEPT (1..7), so the mask is
; 0xFF << (8 - n). It runs after the filter because crushing first would just
; be smoothed back out.
;
; clobbers: AX BX CX DX SI DI. ES = DS on entry and exit (mpm_gen's contract).
; -----------------------------------------------------------------------------
mpm_post:
    mov cx, [mpm_outp]
    sub cx, mpm_outbuf              ; bytes actually produced this call
    jbe .out                        ; nothing rendered: both stages are no-ops
    mov si, mpm_outbuf
    cmp byte [mpm_amiga], 0
    je .crush
    ; --- one-pole lowpass ---------------------------------------------------
    mov dl, 1                       ; DL = the shift: A500 = 1, A1200 = 2
    cmp byte [mpm_amiga], 1
    je .k
    mov dl, 2
.k:
    mov bx, [mpm_flt]               ; BX = y, in x<<8 units
    mov di, cx                      ; the count moves to DI: CL is the shift,
.fl:                                ; so CX cannot also be the counter
    mov ah, [si]                    ; AX = x << 8, no shift needed...
    xor al, al
    sub ax, 0x8000                  ; ...and then CENTRED: the output byte is
                                    ; unsigned around a 0x80 bias, so x << 8
                                    ; for x >= 128 reads NEGATIVE as a signed
                                    ; word and the sar below would run the
                                    ; filter backwards on the top half of the
                                    ; waveform. (x - 128) << 8 spans
                                    ; -32768..32512 and stays signed-correct
    sub ax, bx                      ; AX = (x << 8) - y, signed
    mov cl, dl
    sar ax, cl
    add bx, ax                      ; y += (x - y) >> k
    mov ax, bx
    add ah, 0x80                    ; back to the biased byte: y >> 8 is in
    mov [si], ah                    ; -128..127, so this lands in 0..255
    inc si
    dec di
    jnz .fl
    mov [mpm_flt], bx
    mov si, mpm_outbuf
.crush:
    mov al, [mpm_crush]
    or al, al
    jz .out
    cmp al, 7                       ; 8 kept bits is not a crush; anything
    ja .out                         ; wider than a byte is not one either
    mov cl, al                      ; mask = 0xFF << (8 - bits)
    mov ch, 8
    sub ch, cl
    mov cl, ch
    mov al, 0xFF
    shl al, cl
    mov ah, al
    mov cx, [mpm_outp]
    sub cx, mpm_outbuf
.cr:
    and [si], ah
    inc si
    loop .cr
.out:
    ret

; -----------------------------------------------------------------------------
; mpm_volslice - ModPlug addition: the volume-table slice for a channel, with
;                the master volume folded in (SPEC.md 52.3)
; in:  AL = the channel's output volume, 0..64
; out: CF=0 and BX = the slice base for (vol * master) >> 6; CF=1 the channel
;      is inaudible at this master setting and the caller takes its silent
;      path - which still ADVANCES the position, so unmuting or turning the
;      volume back up rejoins the song where it really is.
; preserves CX DX SI DI ES; clobbers AX.
;
; This is where ModPlug Player's volume slider lands, and the placement is the
; point: scaling the TABLE SELECTION costs one multiply per channel per chunk,
; where scaling samples would cost one per output sample. The table itself is
; not rebuilt - the slider moves continuously and a rebuild is 16,640 imuls.
; -----------------------------------------------------------------------------
mpm_volslice:
    or al, al
    jz .quiet
    push cx
    mul byte [mpm_master]           ; AX = vol * master, 0..4096
    mov cl, 6
    shr ax, cl                      ; ...>> 6 = the effective volume, 0..64,
    pop cx                          ; so AH is 0 from here
    or al, al
    jz .quiet                       ; a quiet channel at a low master setting
    mov ah, al                      ; AX = vol * 256, the table's slice stride
    xor al, al
    add ax, mpm_voltab
    mov bx, ax
    clc
    ret
.quiet:
    stc
    ret

; -----------------------------------------------------------------------------
; mpm_mixch - accumulate [mpm_chunk] samples of channel [mpm_crec] into
; mpm_mixbuf (16-bit words). Advances the channel position; a muted or
; silent channel advances without fetching; an ended one-shot goes inactive.
; -----------------------------------------------------------------------------
mpm_mixch:
    mov si, [mpm_crec]
    cmp byte [si+MPM_ACT], 0
    je .out
    ; limit + loop + step scratch
    mov ax, [si+MPM_OFF]
    add ax, [si+MPM_PLEN]
    mov [mpm_climit], ax
    mov ax, [si+MPM_LLEN]
    mov [mpm_cloopl], ax
    mov ax, [si+MPM_STEPI]
    mov [mpm_cstepi], ax
    mov ax, [si+MPM_STEPF]
    mov [mpm_cstepf], ax
    ; silent? (muted channels keep their position moving)
    mov bl, [mpm_curc]
    xor bh, bh
    cmp byte [mpm_mute + bx], 0
    jne .silent
    mov al, [si+MPM_OVOL]
    call mpm_volslice               ; ModPlug addition: the volume slider folds
    jc .silent                      ; in HERE (SPEC.md 52.3) - one multiply per
                                    ; channel per chunk, never one per sample
    ; the inner loop
    mov es, [si+MPM_SEG]
    mov dx, [si+MPM_POSF]
    mov cx, [mpm_chunk]
    mov di, mpm_mixbuf
    push si
    mov si, [si+MPM_POSI]
    cmp byte [mpm_interp], 0        ; ModPlug (SPEC.md 52.5): Setup > Audio >
    jne .lin                        ; Interpolation Mode picks the loop
.mx:
    mov al, [es:si]
    xlat                        ; AL = vt[vol][sample]
    cbw
    add [di], ax
    inc di
    inc di
    add dx, [mpm_cstepf]
    adc si, [mpm_cstepi]
    cmp si, [mpm_climit]
    jae .wrap
.mnext:
    loop .mx
.mdone:
    mov ax, si
    pop si
    mov [si+MPM_POSI], ax
    mov [si+MPM_POSF], dx
    push ds
    pop es
.out:
    ret
.wrap:
    mov ax, [mpm_cloopl]
    or ax, ax
    jz .oend
.wl:
    sub si, ax
    cmp si, [mpm_climit]
    jae .wl
    jmp .mnext

; --- ModPlug addition: the linearly interpolated inner loop -------------------
; s = s0 + ((s1 - s0) * frac >> 8), frac = the position fraction's high byte,
; and THEN through the volume table - interpolating the raw sample rather than
; two table lookups is both righter and one xlat cheaper.
;
; Every scratch here is memory because every register is spoken for: BX is the
; volume table's base and xlat will not take another, CX is the count, DX is
; the position fraction AND imul's high half, SI/DI/ES are the two cursors.
; BP is the one exception - it holds climit-1 so the last sample of a run can
; hold s1 = s0 instead of fetching the byte after the sample, which for the
; last sample of the last instrument is a byte outside the module's own claim.
; BP is safe to take: mpm_gen saved it, and only mpm_mixch_xt otherwise wants
; it - the other mixing path entirely.
;
; This loop is ~150 cycles a sample against the flat path's ~50, which is the
; whole reason Setup greys it on a tier-0 machine (SPEC.md 47's rule 4).
.lin:
    mov bp, [mpm_climit]
    dec bp
.mxl:
    mov ax, [es:si]                 ; AL = s0 and AH = s1 in ONE fetch
    cmp si, bp
    jb .lnx
    mov ah, al                      ; last sample of the run: hold
.lnx:
    mov [mpm_lt0], ax               ; bank the pair, then sign-extend each
    mov al, ah
    cbw
    mov [mpm_lt1], ax               ; s1
    mov al, [mpm_lt0]
    cbw
    mov [mpm_lt0], ax               ; s0 (the pair is consumed)
    mov ax, [mpm_lt1]
    sub ax, [mpm_lt0]               ; d = s1 - s0, signed, -255..255
    mov [mpm_lt1], dx               ; bank the position fraction: imul writes DX
    mov dl, dh                      ; DX = frac 0..255, positive as signed, so
    xor dh, dh                      ; imul's signed*signed is the right product
    imul dx                         ; DX:AX = d * frac
    mov al, ah                      ; ...>> 8, taken across the pair
    mov ah, dl
    mov dx, [mpm_lt1]               ; position fraction back
    add ax, [mpm_lt0]               ; s, and inside [s0, s1] so never clipping
    xlat                            ; AL = vt[vol][s]
    cbw
    add [di], ax
    inc di
    inc di
    add dx, [mpm_cstepf]
    adc si, [mpm_cstepi]
    cmp si, [mpm_climit]
    jae .wrapl
.mnextl:
    loop .mxl
    jmp .mdone
.wrapl:
    mov ax, [mpm_cloopl]
    or ax, ax
    jz .oend
.wll:
    sub si, ax
    cmp si, [mpm_climit]
    jae .wll
    jmp .mnextl
.oend:
    mov ax, si
    pop si
    mov [si+MPM_POSI], ax
    mov [si+MPM_POSF], dx
    mov byte [si+MPM_ACT], 0     ; one-shot ended mid-chunk
    push ds
    pop es
    ret
.silent:
    ; position += step * chunk, then wrap
    push si
    mov ax, [mpm_cstepf]
    mul word [mpm_chunk]         ; DX:AX = frac advance
    mov cx, dx
    pop si
    add ax, [si+MPM_POSF]
    adc cx, 0
    mov [si+MPM_POSF], ax
    push si
    mov ax, [mpm_cstepi]
    mul word [mpm_chunk]         ; DX:AX, DX = 0 (step*chunk bounded)
    pop si
    add ax, cx
    add ax, [si+MPM_POSI]        ; cannot wrap: limit <= 64015, adv <= 1520
.swl:
    cmp ax, [mpm_climit]
    jb .sst
    mov cx, [mpm_cloopl]
    or cx, cx
    jz .soend
    sub ax, cx
    jmp .swl
.sst:
    mov [si+MPM_POSI], ax
    ret
.soend:
    mov byte [si+MPM_ACT], 0
    ret

; -----------------------------------------------------------------------------
; mpm_mixch_xt - one channel's chunk in XT mode (SPEC.md 45.9), straight into
; the outbuf at [mpm_outp]. [mpm_first] = 1: this is the first audible channel,
; STORE 128 + vt[b]; 0: ADD vt[b]. The bounds check leaves the inner loop:
; one DIV cuts a conservative run ((limit-1-pos)/(stepint+1) samples cannot
; reach the limit), the run mixes unchecked, and only the boundary approach
; walks the checked/wrapping path a sample at a time. Muted/silent channels
; take mpm_mixch's shared advance. BP carries the frac step as a register -
; never a memory base, so SS != DS cannot bite.
; -----------------------------------------------------------------------------
mpm_mixch_xt:
    mov si, [mpm_crec]
    cmp byte [si+MPM_ACT], 0
    je .ret0
    mov ax, [si+MPM_OFF]
    add ax, [si+MPM_PLEN]
    mov [mpm_climit], ax
    mov ax, [si+MPM_LLEN]
    mov [mpm_cloopl], ax
    mov ax, [si+MPM_STEPI]
    mov [mpm_cstepi], ax
    mov ax, [si+MPM_STEPF]
    mov [mpm_cstepf], ax
    mov bl, [mpm_curc]
    xor bh, bh
    cmp byte [mpm_mute + bx], 0
    jne mpm_mixch.silent         ; the shared advance-only path: same scratch,
    mov al, [si+MPM_OVOL]        ; same [mpm_crec] in SI, ends in ret
    call mpm_volslice           ; the master volume, in XT mode too: it is a
    jc mpm_mixch.silent         ; per-CHUNK cost, so XT mode keeps it
    mov es, [si+MPM_SEG]
    mov dx, [si+MPM_POSF]
    mov bp, [mpm_cstepf]         ; BP = frac step, register-resident
    mov di, [mpm_outp]
    mov cx, [mpm_chunk]          ; CX = samples remaining
    push si
    mov si, [si+MPM_POSI]
.runs:
    mov ax, [mpm_climit]         ; conservative unchecked run
    dec ax
    sub ax, si
    jb .edge                    ; at/past limit-1: checked path decides
    push cx
    push dx
    xor dx, dx
    mov cx, [mpm_cstepi]
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
    cmp byte [mpm_first], 0
    je .addl
.stl:
    mov al, [es:si]             ; store pass: out = 128 + vt[b]
    xlat
    add al, 0x80
    mov [di], al
    inc di
    add dx, bp
    adc si, [mpm_cstepi]
    loop .stl
    jmp .rejoin
.addl:
    mov al, [es:si]             ; add pass: out += vt[b]
    xlat
    add [di], al
    inc di
    add dx, bp
    adc si, [mpm_cstepi]
    loop .addl
.rejoin:
    pop cx                      ; CX = remaining
    or cx, cx
    jnz .runs
    jmp .fin
.edge:
    cmp si, [mpm_climit]         ; the boundary approach, one checked sample
    jae .wrap
    mov al, [es:si]
    xlat
    cmp byte [mpm_first], 0
    je .eadd
    add al, 0x80
    mov [di], al
    jmp .estep
.eadd:
    add [di], al
.estep:
    inc di
    add dx, bp
    adc si, [mpm_cstepi]
    loop .runs                  ; one sample consumed: recut the run
    jmp .fin
.wrap:
    mov ax, [mpm_cloopl]
    or ax, ax
    jz .oend
.wl:
    sub si, ax
    cmp si, [mpm_climit]
    jae .wl
    jmp .runs                   ; wrapped, nothing consumed: recut
.fin:
    mov ax, si
    pop si
    mov [si+MPM_POSI], ax
    mov [si+MPM_POSF], dx
    mov byte [mpm_first], 0      ; this channel rendered: later ones add
    push ds
    pop es
    ret
.oend:                          ; one-shot ended mid-chunk
    push ds
    pop es                      ; BEFORE any stosb: ES was the sample seg
    cmp byte [mpm_first], 0
    je .of
    mov al, 0x80                ; the store channel owes the bias under
    rep stosb                   ; the tail it never rendered
    mov byte [mpm_first], 0
.of:
    pop si
    mov byte [si+MPM_ACT], 0
.ret0:
    ret

; =============================================================================
; The replayer: one tick
; =============================================================================
mpm_dotick:
    cmp byte [mpm_playing], 0
    je .out
    inc byte [mpm_tick]
    mov al, [mpm_tick]
    cmp al, [mpm_speed]
    jb .pertick
    mov byte [mpm_tick], 0
    ; pattern delay (EEx): transfer, then hold held rows
    mov al, [mpm_patdel]
    or al, al
    jz .nd
    mov [mpm_patdel2], al
    mov byte [mpm_patdel], 0
.nd:
    mov al, [mpm_patdel2]
    or al, al
    jz .advance
    dec al
    mov [mpm_patdel2], al
    or al, al
    jz .advance
    jmp .upd                    ; held row: no note re-read
.advance:
    call mpm_nextrow
    call mpm_readrow
    jmp .upd
.pertick:
    call mpm_tickfx
.upd:
    call mpm_chupd
.out:
    ret

; -----------------------------------------------------------------------------
; mpm_nextrow - row/position advance (the ft2 getNextPos shape)
; -----------------------------------------------------------------------------
mpm_nextrow:
    mov al, [mpm_row]
    inc al
    cmp byte [mpm_pbreak], 0
    je .nf
    mov al, [mpm_pbrow]          ; E6x: jump within this pattern
    mov byte [mpm_pbreak], 0
.nf:
    cmp byte [mpm_posjmp], 0
    jne .adv
    cmp al, 64
    jb .set
.adv:
    mov al, [mpm_pbrow]
    mov byte [mpm_pbrow], 0
    mov byte [mpm_posjmp], 0
    cmp al, 64
    jb .r0
    xor al, al
.r0:
    cmp byte [mpm_ploop], 0      ; pattern-loop mode: never leave the pattern
    jne .set
    inc byte [mpm_songpos]
    mov ah, [mpm_songpos]
    cmp ah, [mpm_songlen]
    jb .pat
    mov ah, [mpm_restart]
    mov [mpm_songpos], ah
.pat:
    push ax
    mov bl, [mpm_songpos]
    xor bh, bh
    mov al, [mpm_orders + bx]
    mov [mpm_pattern], al
    pop ax
.set:
    mov [mpm_row], al
    ret

; -----------------------------------------------------------------------------
; mpm_readrow - tick 0: read the four cells of (pattern, row), trigger notes,
; run tick-0 effects
; -----------------------------------------------------------------------------
mpm_readrow:
    ; ES = pattern segment for the whole row
    mov al, [mpm_pattern]
    cmp al, [mpm_npat]           ; defense-in-depth (the hostile-blob rule):
    jb .pok                     ; no state corruption may ever select a
    xor al, al                  ; pattern segment past the validated extent
.pok:
    xor ah, ah
    mov cl, 6
    shl ax, cl
    add ax, 67
    add ax, [mpm_blobseg]
    mov es, ax
    mov word [mpm_crec], mpm_chans
    mov byte [mpm_curc], 0
.ch:
    mov di, [mpm_crec]
    ; SI = cell
    mov al, [mpm_row]
    xor ah, ah
    mov cl, 4
    shl ax, cl
    mov si, ax
    mov al, [mpm_curc]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    add si, ax
    add si, 12
    ; decode into mpm_wper / mpm_wsmp / MPM_EFF / MPM_PARM
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
    mov [mpm_wsmp], ch
    and bl, 0x0F
    mov [di+MPM_EFF], bl
    mov [di+MPM_PARM], cl
    and al, 0x0F
    xchg al, ah
    mov [mpm_wper], ax
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
    mov ax, [mpm_wper]
    mov [di+MPM_DPER], ax
    mov al, [mpm_wsmp]
    mov [di+MPM_DSMP], al
    jmp .fx0                    ; note delay: nothing else happens at tick 0
.noted:
    ; --- instrument: set current sample + reset volume ----------------------
    mov al, [mpm_wsmp]
    or al, al
    jz .noinst
    mov [di+MPM_SMP], al
    push bx
    call mpm_smpent              ; AL 1..31 -> BX = sample table entry
    mov al, [bx+MPS_VOL]
    mov [di+MPM_VOL], al
    pop bx
.noinst:
    ; --- note ---------------------------------------------------------------
    mov ax, [mpm_wper]
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
    mov cl, [di+MPM_PARM]
    or cl, cl
    jz .offold
    mov [di+MPM_OFFM], cl
.offold:
    mov cl, [di+MPM_OFFM]
    xor ch, ch
    push ax
    mov ax, cx
    mov cl, 8
    shl ax, cl                  ; offset memory << 8 = start bytes
    mov cx, ax
    pop ax
.trig:
    call mpm_trig
    jmp .fx0
.porta:
    call mpm_pclamp              ; AX -> clamped 113..856
    mov [di+MPM_TARG], ax
.fx0:
    ; --- tick-0 effects -----------------------------------------------------
    mov bl, [di+MPM_EFF]
    mov cl, [di+MPM_PARM]
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
    mov [di+MPM_PSPD], cl
    jmp .fxdone
.f4:
    mov al, cl
    and al, 0xF0
    jz .f4y
    mov ah, [di+MPM_VIBC]
    and ah, 0x0F
    or ah, al
    mov [di+MPM_VIBC], ah
.f4y:
    mov al, cl
    and al, 0x0F
    jz .fxdone
    mov ah, [di+MPM_VIBC]
    and ah, 0xF0
    or ah, al
    mov [di+MPM_VIBC], ah
    jmp .fxdone
.f7:
    mov al, cl
    and al, 0xF0
    jz .f7y
    mov ah, [di+MPM_TREC]
    and ah, 0x0F
    or ah, al
    mov [di+MPM_TREC], ah
.f7y:
    mov al, cl
    and al, 0x0F
    jz .fxdone
    mov ah, [di+MPM_TREC]
    and ah, 0xF0
    or ah, al
    mov [di+MPM_TREC], ah
    jmp .fxdone
.fB:
    mov byte [mpm_pbrow], 0      ; Bxx position jump
    mov byte [mpm_posjmp], 1
    cmp byte [mpm_ploop], 0      ; pattern-loop mode never leaves the pattern
    jne .fxdone                 ; (mpm_nextrow's contract): Bxx restarts the
                                ; looped pattern from row 0, and the songpos
                                ; store is SKIPPED - the advance never ++s
                                ; it back, so param-1 (0xFF for a clamped
                                ; target of 0) would park there forever
    mov al, cl
    cmp al, [mpm_songlen]
    jb .bok
    xor al, al
.bok:
    dec al                      ; songpos = target - 1; the advance ++s it
    mov [mpm_songpos], al
    jmp .fxdone
.fC:
    mov al, cl                  ; Cxx set volume
    cmp al, 64
    jbe .cok
    mov al, 64
.cok:
    mov [di+MPM_VOL], al
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
    mov [mpm_pbrow], al
    mov byte [mpm_posjmp], 1
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
    mov ax, [di+MPM_PER]         ; fine porta up
    xor ch, ch
    sub ax, cx
    call mpm_pclamp
    mov [di+MPM_PER], ax
    jmp .fxdone
.e2:
    mov ax, [di+MPM_PER]         ; fine porta down
    xor ch, ch
    add ax, cx
    call mpm_pclamp
    mov [di+MPM_PER], ax
    jmp .fxdone
.e6:
    or cl, cl                   ; E60: set loop start
    jnz .e6j
    mov al, [mpm_row]
    mov [di+MPM_LROW], al
    jmp .fxdone
.e6j:
    mov al, [di+MPM_LCNT]
    or al, al
    jnz .e6c
    mov [di+MPM_LCNT], cl        ; first pass: arm the counter, jump
    jmp .e6go
.e6c:
    dec al
    mov [di+MPM_LCNT], al
    or al, al
    jz .fxdone                  ; loop spent
.e6go:
    mov al, [di+MPM_LROW]
    mov [mpm_pbrow], al
    mov byte [mpm_pbreak], 1
    jmp .fxdone
.eA:
    mov al, [di+MPM_VOL]         ; fine vol up
    add al, cl
    cmp al, 64
    jbe .eas
    mov al, 64
.eas:
    mov [di+MPM_VOL], al
    jmp .fxdone
.eB:
    mov al, [di+MPM_VOL]         ; fine vol down
    sub al, cl
    jnc .ebs
    xor al, al
.ebs:
    mov [di+MPM_VOL], al
    jmp .fxdone
.eC:
    or cl, cl                   ; EC0: cut now
    jnz .fxdone
    mov byte [di+MPM_VOL], 0
    jmp .fxdone
.eE:
    cmp byte [mpm_patdel2], 0    ; EEx pattern delay (not while held)
    jne .fxdone
    or cl, cl
    jz .fxdone
    inc cl
    mov [mpm_patdel], cl
    jmp .fxdone
.fF:
    or cl, cl                   ; F00 = stop
    jnz .fs
    mov byte [mpm_playing], 0
    jmp .fxdone
.fs:
    cmp cl, 0x20
    jae .fbpm
    mov [mpm_speed], cl
    jmp .fxdone
.fbpm:
    mov [mpm_bpm], cl
    call mpm_calc_spt
.fxdone:
    ; defaults: what the mixer hears this tick
    mov ax, [di+MPM_PER]
    mov [di+MPM_OPER], ax
    mov al, [di+MPM_VOL]
    mov [di+MPM_OVOL], al
    add word [mpm_crec], MPM_CHSZ
    inc byte [mpm_curc]
    cmp byte [mpm_curc], 4
    jae .rowdone
    jmp .ch
.rowdone:
    push ds
    pop es
    ret

; -----------------------------------------------------------------------------
; mpm_smpent - AL = sample number 1..31 -> BX = its table entry
; -----------------------------------------------------------------------------
mpm_smpent:
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
    add bx, mpm_smptab
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpm_pclamp - AX = period -> clamped to [113..856]
; -----------------------------------------------------------------------------
mpm_pclamp:
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
; mpm_trig - trigger a note on channel DI: AX = raw period, CX = sample start
; offset (9xx; 0 otherwise). Loads the channel's sample fields from its
; current instrument, resets position and the vibrato/tremolo phases.
; -----------------------------------------------------------------------------
mpm_trig:
    call mpm_pclamp
    mov [di+MPM_PER], ax
    mov byte [di+MPM_VIBP], 0
    mov byte [di+MPM_TREP], 0
    mov al, [di+MPM_SMP]
    or al, al
    jz .dead
    cmp al, 31
    ja .dead
    push bx
    call mpm_smpent
    mov ax, [bx+MPS_SEG]
    mov [di+MPM_SEG], ax
    mov ax, [bx+MPS_OFF]
    mov [di+MPM_OFF], ax
    mov ax, [bx+MPS_PLEN]
    mov [di+MPM_PLEN], ax
    mov dx, [bx+MPS_LLEN]
    mov [di+MPM_LLEN], dx
    pop bx
    or ax, ax
    jz .dead                    ; zero-length sample: silence, never indexed
    cmp cx, ax
    jae .dead                   ; 9xx start past the data: silence (no wrap)
    mov ax, [di+MPM_OFF]
    add ax, cx
    mov [di+MPM_POSI], ax
    mov word [di+MPM_POSF], 0
    mov byte [di+MPM_ACT], 1
    ret
.dead:
    mov byte [di+MPM_ACT], 0
    ret

; -----------------------------------------------------------------------------
; mpm_tickfx - ticks 1..speed-1: the per-tick effects, all four channels
; -----------------------------------------------------------------------------
mpm_tickfx:
    mov word [mpm_crec], mpm_chans
    mov byte [mpm_curc], 0
.ch:
    mov di, [mpm_crec]
    ; defaults first; the handlers below override
    mov ax, [di+MPM_PER]
    mov [di+MPM_OPER], ax
    mov al, [di+MPM_VOL]
    mov [di+MPM_OVOL], al
    mov bl, [di+MPM_EFF]
    mov cl, [di+MPM_PARM]
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
    call mpm_arp
    jmp .next
.f1:
    mov ax, [di+MPM_PER]         ; 1xx porta up
    xor ch, ch
    sub ax, cx
    call mpm_pclamp
    mov [di+MPM_PER], ax
    mov [di+MPM_OPER], ax
    jmp .next
.f2:
    mov ax, [di+MPM_PER]         ; 2xx porta down
    xor ch, ch
    add ax, cx
    call mpm_pclamp
    mov [di+MPM_PER], ax
    mov [di+MPM_OPER], ax
    jmp .next
.f3:
    call mpm_portastep
    jmp .next
.f4:
    call mpm_vibrato
    jmp .next
.f5:
    call mpm_portastep           ; 5xy = porta (memory) + volume slide xy
    call mpm_volslide
    jmp .next
.f6:
    call mpm_vibrato             ; 6xy = vibrato (memory) + volume slide xy
    call mpm_volslide
    jmp .next
.f7:
    call mpm_tremolo
    jmp .next
.fA:
    call mpm_volslide
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
    mov al, [mpm_tick]
    xor ah, ah
    div cl
    or ah, ah
    jnz .next
    mov ax, [di+MPM_PER]
    xor cx, cx
    call mpm_trig
    jmp .next
.eC:
    cmp cl, [mpm_tick]           ; ECx note cut at tick x
    jne .next
    mov byte [di+MPM_VOL], 0
    mov byte [di+MPM_OVOL], 0
    jmp .next
.eD:
    cmp cl, [mpm_tick]           ; EDx delayed trigger at tick x
    jne .next
    mov al, [di+MPM_DSMP]
    or al, al
    jz .edper
    cmp al, 31
    ja .edper
    mov [di+MPM_SMP], al
    push bx
    call mpm_smpent
    mov al, [bx+MPS_VOL]
    mov [di+MPM_VOL], al
    mov [di+MPM_OVOL], al
    pop bx
.edper:
    mov ax, [di+MPM_DPER]
    or ax, ax
    jz .next
    xor cx, cx
    call mpm_trig
    mov ax, [di+MPM_PER]
    mov [di+MPM_OPER], ax
.next:
    add word [mpm_crec], MPM_CHSZ
    inc byte [mpm_curc]
    cmp byte [mpm_curc], 4
    jae .done
    jmp .ch
.done:
    ret

; --- per-tick effect bodies (DI = channel record; may clobber AX/CX/DX/SI) -----

; Axy / 5xy / 6xy volume slide: x up (wins), else y down; clamp 0..64
mpm_volslide:
    mov al, [di+MPM_PARM]
    mov ah, al
    push cx
    mov cl, 4
    shr ah, cl
    pop cx
    and al, 0x0F
    mov dl, [di+MPM_VOL]
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
    mov [di+MPM_VOL], dl
    mov [di+MPM_OVOL], dl
    ret

; 3xx tone portamento step toward MPM_TARG
mpm_portastep:
    mov ax, [di+MPM_PER]
    mov dx, [di+MPM_TARG]
    or dx, dx
    jz .out                     ; no target yet
    mov cl, [di+MPM_PSPD]        ; load the speed BEFORE the compare: an XOR
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
    mov [di+MPM_PER], ax
    mov [di+MPM_OPER], ax
.out:
    ret

; 4xy vibrato: delta = (vibtab[(pos>>2)&31] * depth) >> 7 on OPER only
mpm_vibrato:
    mov al, [di+MPM_VIBP]
    push cx
    mov cl, 2
    shr al, cl
    pop cx
    and al, 31
    mov bl, al
    xor bh, bh
    mov al, [mpm_vibtab + bx]
    mov dl, [di+MPM_VIBC]
    and dl, 0x0F
    mul dl                      ; AX = tab * depth (<= 3825)
    mov cl, 7
    shr ax, cl                  ; delta in PT period units
    mov dx, [di+MPM_PER]
    test byte [di+MPM_VIBP], 0x80
    jnz .minus
    add dx, ax
    jmp .clip
.minus:
    sub dx, ax
.clip:
    mov ax, dx
    call mpm_pclamp
    mov [di+MPM_OPER], ax
    ; pos += speed * 4
    mov al, [di+MPM_VIBC]
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    shl al, 1
    shl al, 1
    add [di+MPM_VIBP], al
    ret

; 7xy tremolo: delta = (vibtab[(pos>>2)&31] * depth) >> 6 on OVOL only
mpm_tremolo:
    mov al, [di+MPM_TREP]
    push cx
    mov cl, 2
    shr al, cl
    pop cx
    and al, 31
    mov bl, al
    xor bh, bh
    mov al, [mpm_vibtab + bx]
    mov dl, [di+MPM_TREC]
    and dl, 0x0F
    mul dl
    mov cl, 6
    shr ax, cl                  ; delta (<= 59)
    mov dl, [di+MPM_VOL]
    xor dh, dh
    test byte [di+MPM_TREP], 0x80
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
    mov [di+MPM_OVOL], al
    mov al, [di+MPM_TREC]
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    shl al, 1
    shl al, 1
    add [di+MPM_TREP], al
    ret

; 0xy arpeggio: tick%3 = 0 base note / 1 +x semitones / 2 +y semitones
mpm_arp:
    mov al, [mpm_tick]
    xor ah, ah
    mov dl, 3
    div dl
    or ah, ah
    jz .out                     ; base tick: OPER = PER (the default)
    mov dl, [di+MPM_PARM]
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
    mov ax, [di+MPM_PER]
    call mpm_pfind               ; BX = note index of the playing period
    add bl, dl
    cmp bl, 35
    jbe .idx
    mov bl, 35
.idx:
    xor bh, bh
    shl bx, 1
    mov ax, [mpm_pertab + bx]
    mov [di+MPM_OPER], ax
.out:
    ret

; -----------------------------------------------------------------------------
; mpm_chupd - after every tick: recompute each channel's mixer step from its
; output period (3 DIVs, the research recipe - the [113..856] clamp is the
; #DE guard) and publish the UI-visible volume/period.
; -----------------------------------------------------------------------------
mpm_chupd:
    mov word [mpm_crec], mpm_chans
    mov byte [mpm_curc], 0
.ch:
    mov di, [mpm_crec]
    mov bl, [mpm_curc]
    xor bh, bh
    cmp byte [di+MPM_ACT], 0
    je .quiet
    mov ax, [di+MPM_OPER]
    call mpm_pclamp
    push bx
    shl bx, 1
    mov [mpm_chper + bx], ax     ; word slot
    pop bx
    push bx
    mov bx, ax
    mov dx, 0x0036
    mov ax, 0x1F0F              ; DX:AX = 3,546,895 (PAL clock / 2)
    div bx                      ; AX = playback Hz (<= 31388)
    xor dx, dx
    div word [mpm_mixrate]       ; AX = step int, DX = rem
    mov [di+MPM_STEPI], ax
    xor ax, ax                  ; DX:AX = rem << 16
    div word [mpm_mixrate]
    mov [di+MPM_STEPF], ax
    pop bx
    ; publish volume: 0 when muted, else the tick's output volume
    cmp byte [mpm_mute + bx], 0
    jne .quietv
    mov al, [di+MPM_OVOL]
    mov [mpm_chvol + bx], al
    jmp .next
.quiet:
    shl bx, 1
    mov word [mpm_chper + bx], 0
    shr bx, 1
.quietv:
    mov byte [mpm_chvol + bx], 0
.next:
    add word [mpm_crec], MPM_CHSZ
    inc byte [mpm_curc]
    cmp byte [mpm_curc], 4
    jb .ch
    ret

; -----------------------------------------------------------------------------
; mpm_calc_spt - samples per tick = mixrate * 5 / (2 * BPM)
; -----------------------------------------------------------------------------
mpm_calc_spt:
    mov ax, [mpm_mixrate]
    mov bx, 5
    mul bx                      ; DX:AX = mixrate * 5
    mov bl, [mpm_bpm]
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
    mov [mpm_spt], ax
    ret

; =============================================================================
; Tables
; =============================================================================

; finetune-0 ProTracker period table, 3 octaves (ft2_tables.c ptPeriods)
mpm_pertab:
    dw 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453
    dw 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226
    dw 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113

; the PT/FT2 vibrato half-sine (vibratoTab, exact)
mpm_vibtab:
    db   0,  24,  49,  74,  97, 120, 141, 161
    db 180, 197, 212, 224, 235, 244, 250, 253
    db 255, 253, 250, 244, 235, 224, 212, 197
    db 180, 161, 141, 120,  97,  74,  49,  24

mpm_ntab:    db 'C-C#D-D#E-F-F#G-G#A-A#B-'
mpm_hexd:    db '0123456789ABCDEF'

; the mpm_load verdict strings (returned by offset in AX)
mpme_notmod: db 'Not a MOD', 0
mpme_trunc:  db 'Truncated file', 0
mpme_song:   db 'Bad song data', 0

; =============================================================================
; trkplay bss (through tracker.asm's MPPB/MPPW/MPPBUF accumulator)
; =============================================================================

; --- the pinned UI-visible state (interface order) -----------------------------
    MPPB mpm_loaded                  ; a module is parsed and playable
    MPPB mpm_playing                 ; the replayer is advancing
    MPPB mpm_songpos                 ; order position
    MPPB mpm_row                     ; row 0..63
    MPPB mpm_pattern                 ; playing pattern number
    MPPB mpm_speed                   ; ticks per row
    MPPB mpm_bpm                     ; tempo
    MPPB mpm_songlen                 ; order entries played
    MPPB mpm_npat                    ; pattern count P
    MPPBUF mpm_mute, 4               ; per-channel mute flags
    MPPBUF mpm_chvol, 4              ; current output volumes 0..64 (0 = quiet)
    MPPBUF mpm_chper, 8              ; current playing periods (0 = silent)
    MPPBUF mpm_title, 21             ; module title, NUL-terminated
    MPPBUF mpm_outbuf, 2048          ; mpm_gen's rendered output
    MPPBUF mpm_celltxt, 11           ; mpm_cell2txt's 'C-2 01 A0F' + NUL
    MPPW mpm_blobseg                 ; the module blob (set by tracker.asm)
    MPPW mpm_bloblen_lo              ; ...and its 32-bit byte length
    MPPW mpm_bloblen_hi
    MPPW mpm_mixrate                 ; mixer output rate, Hz

; --- ModPlug: the DSP tail's settings and state (SPEC.md 52.5) -----------------
; Settings, written by the Setup window; state, owned by the mixer. All zero
; at load, which is every stage off - so a machine that never opens Setup pays
; nothing for any of it.
    MPPB mpm_interp                  ; 0 = none (nearest), 1 = linear
    MPPB mpm_dither                  ; 1 = dither the 16 -> 8 bit conversion
    MPPB mpm_amiga                   ; 0 = unfiltered, 1 = A500, 2 = A1200
    MPPB mpm_crush                   ; 0 = off, else output bits KEPT (1..7)
    MPPW mpm_flt                     ; the one-pole's state, x<<8 and SIGNED
    MPPW mpm_dseed                   ; the dither LFSR (nonzero: see mpm_init)
    MPPW mpm_lt0                     ; the linear loop's scratch: s0...
    MPPW mpm_lt1                     ; ...s1, then the banked position fraction
    MPPB mpm_master                  ; the volume slider, 0..64 (mpm_volslice)

; --- replayer internals --------------------------------------------------------
    MPPB mpm_restart                 ; validated restart position
    MPPB mpm_ploop                   ; 1 = pattern-loop mode (P key)
    MPPB mpm_tick                    ; tick 0..speed-1
    MPPB mpm_patdel                  ; EEx pending delay
    MPPB mpm_patdel2                 ; ...and the held-row counter
    MPPB mpm_pbreak                  ; E6x same-pattern jump flag
    MPPB mpm_pbrow                   ; break/loop target row
    MPPB mpm_posjmp                  ; Bxx/Dxx position advance flag
    MPPB mpm_curc                    ; channel loop counter
    MPPB mpm_tvol                    ; volume-table build counter
    MPPB mpm_xt                      ; XT mode (SPEC.md 45.9): 0 = default
    MPPB mpm_first                   ; XT chunk: next audible channel stores
    MPPW mpm_spt                     ; samples per tick
    MPPW mpm_tickrem                 ; samples left in the current tick
    MPPW mpm_need                    ; mpm_gen bytes remaining
    MPPW mpm_outp                    ; mpm_gen output cursor
    MPPW mpm_chunk                   ; current mix chunk, samples
    MPPW mpm_crec                    ; current channel record
    MPPW mpm_climit                  ; mixer scratch: position limit
    MPPW mpm_cloopl                  ;   loop length
    MPPW mpm_cstepi                  ;   step int
    MPPW mpm_cstepf                  ;   step fraction
    MPPW mpm_wper                    ; row decode scratch: period
    MPPB mpm_wsmp                    ;   sample
    MPPW mpm_run_lo                  ; loader: running 32-bit sample offset
    MPPW mpm_run_hi
    MPPW mpm_t1                      ; loader scratch
    MPPW mpm_t2
    MPPW mpm_t3
    MPPW mpm_t4
    MPPW mpm_t5
    MPPW mpm_t6
    MPPBUF mpm_orders, 128           ; the order table, copied out of the blob
    MPPBUF mpm_smptab, MPS_SZ * 31    ; the validated sample table
    MPPBUF mpm_chans, MPM_CHSZ * 4    ; the four channel records
    MPPBUF mpm_mixbuf, MPM_CHUNK * 2  ; 16-bit accumulator
    MPPBUF mpm_voltab, 65 * 256      ; vt[vol][byte] = (int8)byte * vol >> 6
