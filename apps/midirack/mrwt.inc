; =============================================================================
; os8088 - apps/midirack/mrwt.inc
;
; THE SOUND BLASTER WAVETABLE (SPEC.md 105.8.6). Prefix mwt_.
;
; A SAMPLE PLAYER, mixed by the CPU and played by the card: what a
; daughterboard does in its own silicon, done here in a loop of about twenty
; instructions a voice a sample. Every General MIDI program and every drum key
; has ONE 8-bit sample in MIDIRACK.BNK (tools/os88midbank.py made it from a
; SoundFont), with its loop, its root key and an envelope; a note plays it at
; the step its pitch asks for - nearest-sample, no interpolation - through a
; volume page, into a 16-bit sum the synth's own soft-clip table turns into the
; card's bytes. It is the synth's place in the machine exactly: the same card
; ring, the same worker, the same sequencer in sample time (mrout.inc's
; mrb_start opens whichever is chosen), and the same voice allocator.
;
; THE COST is the reason it is a CHOICE and not Automatic's: a voice-sample is
; ~90 cycles on an 8088 (MEASURED: three voices at 8,000 Hz make 94% of the
; rate on the 5150, four 78%), so a 4.77 MHz machine plays three voices at
; 6,000 Hz; a 286 eight at 11,025; a 386 or better sixteen.
;
; THE BANK is read WHOLE, the first time the wavetable plays, into a claim of
; its own, from the folder MIDIRack was launched from (recorded at entry:
; adding songs through the Open dialog moves the instance's folder), and is
; kept until the window closes. Its index lives in it, so nothing is copied.
; It is read and played through WINDOWS (KERN-MEGA-MEMORY-PLAN MC7): an
; OSAPI_SEG SEG_SUB every 32 KB of the claim, each 64 KB long or to the
; claim's end, made once at load (mwt_wins). On kern_mega in protected mode
; the claim is an LDT selector, where `bseg + N` names some other
; descriptor; a real-mode kernel answers SEG_SUB with bseg + N itself, so the
; same table is today's segments there and it is one path on every kernel.
; A sample starting at byte A plays from window A >> 15 at A & 7FFFh, which
; is no slower per note-on than the granule sum it replaces (mwt_place).
;
; THE MIX CLAIM holds what the inner loop reads beside the sample: 32 volume
; pages (the signed sample to its scaled value, built at open), the span's sum
; and a copy of the output table - so the per-sample loop has the sample in DS
; and everything else in ES, and the output pass the sum and the table in DS.
; =============================================================================

MWT_SPAN    equ MRY_SPAN            ; samples a span (the ring's unit)
MWT_MAXV    equ 16
MWT_PAGES   equ 32
MWT_ACC     equ MWT_PAGES * 256     ; the sum, in the mix claim...
MWT_OTAB    equ MWT_ACC + MWT_SPAN * 2  ; ...and the output table after it
MWT_VSTEP   equ MWT_OTAB + 2048     ; ...and the loop's step, protected mode
MWT_MIXKB   equ (MWT_VSTEP + 2 + 1023) / 1024
MWT_FULL    equ 127 << 8            ; an envelope at its top
MWT_NWIN    equ 20                  ; windows: a 32 KB step over a bank of
                                    ; 640 KB at most (mwt_info's ceiling)
%if MWT_MAXV > MR_MAXV
  %error "the wavetable's voices are the allocator's"
%endif

; the bank's own layout (tools/os88midbank.py)
MRK_MAGIC   equ 0                   ; 'MRBK'
MRK_NSAMP   equ 6
MRK_DOFF    equ 8
MRK_PROG    equ 16
MRK_DRUM    equ 18
MRK_SAMP    equ 20
MRE_SAMP    equ 0                   ; an entry: dw sample
MRE_ROOT    equ 2                   ; db root
MRE_FINE    equ 3                   ; db fine (1/32 semitone, signed)
MRE_ATK     equ 4                   ; db attack, decay (10 ms), sustain
MRE_DEC     equ 5                   ;    (0..127), release (10 ms)
MRE_SUS     equ 6
MRE_REL     equ 7
MRS_PARA    equ 0                   ; a sample: dw paragraph
MRS_LEN     equ 2
MRS_LPS     equ 4
MRS_LPE     equ 6                   ; 0 = one-shot
MRS_RATE    equ 8
MRS_GAIN    equ 10

    MRW mwt_bseg                    ; the bank's claim (0: not loaded)
    MRW mwt_doff                    ; ...where its sample data starts in
    MRW mwt_doffh                   ;    it, a 32-bit byte offset
    MRW mwt_lim                     ; mwt_place: the highest end a voice
                                    ; may have, in its sample's window
    MRW mwt_nwin                    ; the windows mwt_wins made, x 2
    MRBUF mwt_win, MWT_NWIN * 2     ; window w: a SEG_SUB at 2,048w paragraphs
    MRBUF mwt_wlim, MWT_NWIN * 2    ; ...and its length, FFFFh for 64 KB
    MRW mwt_nbase                   ; mwt_place: a voice's start, as an offset
    MRW mwt_xseg                    ; the mix claim
    MRW mwt_rate                    ; the output rate
    MRW mwt_pos                     ; rendered so far this span
    MRW mr_homedir                  ; the folder MIDIRack was launched from
    MRB mr_homevol
    MRW mr_bankkb                   ; the bank's size, read at entry
    MRBUF mwt_cur, FSEQ_SIZE        ; mwt_load's READ_SEQ cursor (os88_rseq)
    MRX mwt_sseg, MWT_MAXV * 2    ; per voice: the sample's segment
    MRX mwt_end, MWT_MAXV * 2     ; ...the index it stops (or loops) at
    MRX mwt_llen, MWT_MAXV * 2    ; ...its loop's length (0: one-shot)
    MRX mwt_ip, MWT_MAXV * 2      ; ...the position, whole...
    MRX mwt_fp, MWT_MAXV * 2      ; ...and fraction
    MRX mwt_is, MWT_MAXV * 2      ; ...the step, whole and fraction
    MRX mwt_fs, MWT_MAXV * 2
    MRX mwt_bslo, MWT_MAXV * 2    ; ...the sample's rate over the output's,
    MRX mwt_bshi, MWT_MAXV * 2    ;    16.16
    MRX mwt_env, MWT_MAXV * 2     ; ...the envelope, 8.8
    MRX mwt_ad, MWT_MAXV * 2      ; ...its steps a span: attack, decay,
    MRX mwt_dd, MWT_MAXV * 2      ;    release
    MRX mwt_rd, MWT_MAXV * 2
    MRX mwt_sl, MWT_MAXV * 2      ; ...the sustain level, 8.8
    MRX mwt_stage, MWT_MAXV       ; 0 silent 1 attack 2 decay 3 sustain
    MRX mwt_amp, MWT_MAXV         ;   4 release; the level 0..127
    MRX mwt_gain, MWT_MAXV        ; ...the sample's own
    MRX mwt_pg, MWT_MAXV          ; ...the volume page, 0..31
    MRX mwt_root, MWT_MAXV
    MRX mwt_fine, MWT_MAXV
    MRX mwt_drum, MWT_MAXV        ; ...1: a drum (no bend, no note-off)

mwt_vt:     dw mwt_on, mwt_off, mr_nop, mwt_ctl, mwt_bend, mwt_susup, mwt_alloff
mwt_bname:  db 'MIDIRACK.BNK', 0

; -----------------------------------------------------------------------------
; mwt_info - at entry: is MIDIRACK.BNK beside us, and how big? A read too
; small for any bank answers FERR_BIG with the KB it needs from the directory
; entry, before a byte of data is read (OSAPI_FILE_READ) - so this is free.
; And where we are, for the read that comes later
; -----------------------------------------------------------------------------
mwt_info:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the volume
    mov [mr_homedir], dx
    mov [mr_homevol], bl
    mov byte [mr_hasbank], 0
    push ds
    pop es
    mov bx, mwt_scratch
    mov cx, 16
    xor dx, dx
    mov si, mwt_bname
    call OSAPI_FILE_READ            ; CF=1 AX = FERR_BIG, DX = the KB
    jnc .out                        ; (16 bytes: not a bank)
    cmp ax, FERR_BIG
    jne .out
    cmp dx, 640
    ja .out
    mov [mr_bankkb], dx
    mov byte [mr_hasbank], 1
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    MRBUF mwt_scratch, 16

; -----------------------------------------------------------------------------
; mwt_load - the bank into its claim, once. UI task. out: CF = 1 refused,
; [mr_msg] says why
; -----------------------------------------------------------------------------
mwt_load:
    cmp word [mwt_bseg], 0
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
    push bp
    push es
    mov word [mr_msg], mr_s_nobank
    cmp byte [mr_hasbank], 0
    je .no
    mov ax, [mr_bankkb]             ; the claim: the bank's own KB - the
    call OSAPI_MEM_CLAIM            ; last chunk reads into what is left
    mov word [mr_msg], mr_s_bankmem
    jc .no
    mov [mwt_bseg], dx
    call mwt_wins                   ; its windows, before a byte is read into
    jc .free                        ; them (no LDT entry: the claim goes back)
    call OSAPI_FILE_HERE            ; where we stand, to come back to
    push dx
    push bx
    mov dx, [mr_homedir]
    mov bl, [mr_homevol]
    call OSAPI_FILE_GOTO_QM
    jc .rdfail                      ; NOTHING MOVED: a MIDIRACK.BNK read by
                                    ; name here would be whatever folder we
                                    ; stood in, so refuse as a read would
    ; IN 32 KB CHUNKS: a chunk that size is a multiple of every legal cluster
    ; (os88parts.inc's OP_CHUNK, for the same reason), and a bank is far past
    ; what one OSAPI_FILE_READ is for. RAW, so the bank ships unwrapped
    ; (SPEC.md 105.10). Through os88_rseq (apps/os88rseq.inc, SPEC.md
    ; 18.4.8.1) and not OSAPI_FILE_READ_AT: READ_AT re-walks the chain from
    ; the front on every call, so a bank of N chunks walked N(N+1)/2 chunks'
    ; worth of links; the cursor walks each link once. On kern_small the
    ; helper makes the same call as READ_AT, which is what this was
    mov es, [mwt_bseg]              ; ES = the chunk's segment
    mov si, mwt_bname
    mov di, mwt_cur
    call os88_rseq_new              ; the cursor names no file: start it cold
    xor ax, ax                      ; DX:AX = its offset in the file
    xor dx, dx
    xor bx, bx                      ; ES:BX = where the chunk lands
    mov bp, [mr_bankkb]             ; BP = the claim's KB still unread
.chunk:
    push ax
    push dx
    mov cx, 32768
    cmp bp, 32
    jae .cap
    mov cx, bp                      ; CX = BP x 1024 (BP < 32): the room
    mov ch, cl                      ; left, whole KB and so whole sectors
    mov cl, 0                       ; - kern_big's READ_AT rule (SPEC.md
    shl ch, 1                       ; 18.4.4.2); kern_small carries no card
    shl ch, 1                       ; player (105.8.6)
.cap:
    call os88_rseq                  ; DX:AX = delivered (BX CX SI DI ES kept)
    mov cx, ax
    pop dx
    pop ax
    jc .rdfail
    cmp cx, 32768
    jne .done                       ; (a short chunk is the last)
    sub bp, 32
    jz .done
    add ax, 32768
    adc dx, 0
    xor bx, 32768                   ; the second half of this 64KB, or the
    jnz .chunk                      ; first of the next - whose window is
    mov bx, dx                      ; mwt_wins's 2 x DX (DX:AX = DX x 64KB):
    shl bx, 1                       ; reloaded only when a chunk is going
    shl bx, 1                       ; there, so ES never holds a window past
    mov es, [mwt_win + bx]          ; the last
    xor bx, bx
    jmp short .chunk
.done:
    pop bx
    pop dx
    call OSAPI_FILE_GOTO_QM         ; back where we were
    mov es, [mwt_bseg]              ; a bank, and one this reader takes
    mov word [mr_msg], mr_s_notbank
    cmp word [es:MRK_MAGIC], 'MR'
    jne .free
    cmp word [es:MRK_MAGIC+2], 'BK'
    jne .free
    cmp word [es:4], 1
    jne .free
    mov ax, [es:MRK_DOFF]           ; where the data is, KEPT as an offset
    mov [mwt_doff], ax              ; (mwt_place: a 32-bit byte offset into
    mov ax, [es:MRK_DOFF+2]         ; the bank, which picks the window)
    mov [mwt_doffh], ax
    clc
    jmp short .out
.rdfail:
    pop bx
    pop dx
    call OSAPI_FILE_GOTO_QM
    mov word [mr_msg], mr_s_rderr
.free:
    mov dx, [mwt_bseg]
    call OSAPI_MEM_FREE
    mov word [mwt_bseg], 0
.no:
    stc
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

%include "os88rseq.inc"            ; os88_rseq, for mwt_load

; mwt_wins - the bank's WINDOWS (KERN-MEGA-MEMORY-PLAN MC7): window w an
; OSAPI_SEG SEG_SUB of [mwt_bseg] at 2,048w paragraphs, 64 KB or to the
; claim's end - [mwt_bseg] + 2,048w on a real-mode kernel - and the bytes it
; reaches, the most a voice in it may play to (FFFFh for a whole 64 KB, the
; claim's tail in the last ones). ceil(KB / 32) of them: seven LDT entries
; for the shipped 224 KB bank, freed with the claim. CF = 1 no LDT entry
; free (the claim's free takes the ones made). Keeps every register
mwt_wins:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si                      ; SI = the row, x 2
    xor ax, ax                      ; AX = the window's paragraph
    mov cx, [mr_bankkb]             ; CX = the claim's KB from its base
.w:
    push cx
    mov dx, [mwt_bseg]
    mov bl, SEG_SUB
    xor cx, cx
    call OSAPI_SEG
    pop cx
    jc .x
    mov [mwt_win + si], dx
    mov dx, 0FFFFh
    cmp cx, 64
    jae .lim
    mov dh, cl                      ; under 64 KB left: its bytes
    mov dl, 0
    shl dh, 1
    shl dh, 1
.lim:
    mov [mwt_wlim + si], dx
    inc si
    inc si
    add ax, 2048
    sub cx, 32
    ja .w
    clc                             ; (the last sub may have borrowed)
.x:
    mov [mwt_nwin], si
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%if MWT_NWIN * 32 < 640
  %error "MWT_NWIN windows of 32 KB do not reach mwt_info's 640 KB bank"
%endif

; mwt_pin / mwt_decl - the bank and the mix claim PINNED for a wavetable play
; (mro_play, once mwt_load has the bank) and MOVABLE again once none is open
; (mro_close, and a refused start). Between the two the mixer's voices hold
; segments DERIVED from the bank (mwt_sseg, mwt_place) across the worker's
; park, so a move then would leave them stale; outside it no voice sounds and
; mwt_open silences them all before the next. A claim not held is 0, which
; OSAPI_MEM_MOVABLE refuses harmlessly. SPEC.md 105.10.1. Keep all but flags
mwt_pin:
    push ax
    xor ax, ax
    jmp short mwt_set
mwt_decl:
    push ax
    mov ax, mwt_move
mwt_set:
    push dx
    mov dx, [mwt_bseg]
    call OSAPI_MEM_MOVABLE
    mov dx, [mwt_xseg]
    call OSAPI_MEM_MOVABLE
    pop dx
    pop ax
    ret

; mwt_move - the bank or the mix claim MOVED (SPEC.md 66.3: BX = was, DX =
; now): one word each, and nothing derived from either is live while they
; may move (mwt_pin's note) - but the bank's WINDOWS are kept from load to
; close, so they move with it: each is the bank plus a constant on the
; real-mode kernel that calls this (kern_mega's protected mode REBASES a
; claim's selectors and calls no proc)
mwt_move:
    cmp bx, [mwt_bseg]
    jne .x
    mov [mwt_bseg], dx
    push ax
    push si
    mov ax, dx
    sub ax, bx                      ; AX = the paragraph delta
    mov si, [mwt_nwin]
.w:
    dec si
    dec si
    js .wd
    add [mwt_win + si], ax
    jmp short .w
.wd:
    pop si
    pop ax
.x:
    cmp bx, [mwt_xseg]
    jne .r
    mov [mwt_xseg], dx
.r:
    ret

; mwt_free - the bank and the mix claim back (the window is closing)
mwt_free:
    push dx
    mov dx, [mwt_bseg]
    or dx, dx
    jz .x
    call OSAPI_MEM_FREE
    mov word [mwt_bseg], 0
.x:
    mov dx, [mwt_xseg]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
    mov word [mwt_xseg], 0
.r:
    pop dx
    ret

; -----------------------------------------------------------------------------
; mwt_rate_for - AX = the rate the wavetable opens at: the user's (Settings),
; else the CPU's. mwt_voices - CL = the voices the CPU affords
; -----------------------------------------------------------------------------
mwt_rate_for:
    call mr_rate_user
    jnc .r
    mov ax, 6000                    ; MEASURED on the 5150 (tests/midirack.py
    cmp byte [mr_cpu], CPU_8086     ; --arm wt): at 8 kHz two voices keep up,
    je .r                           ; three make 94% and four 78% - so 6 kHz
    mov ax, 11025                   ; and three. Past an 8088, the bank's own
.r:                                 ; rate: its samples are 11,025 Hz at most
    ret

mwt_voices:
    mov cl, 3
    cmp byte [mr_cpu], CPU_8086
    je .r
    mov cl, 8
    cmp byte [mr_cpu], CPU_286
    je .r
    mov cl, MWT_MAXV
.r:
    ret

; -----------------------------------------------------------------------------
; mwt_open - AX = the output rate, CL = voices: the mix claim (once) and its
; volume pages, the synth's output table copied in, the voice table ours.
; out: CF = 1 no memory for the mix claim
; -----------------------------------------------------------------------------
mwt_open:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ax
    push cx
    xor si, si
    call mry_open                   ; THE OUTPUT TABLE (and the sample clock
    pop cx                          ; at 0): the synth's, built for the card
    pop ax
    mov [mwt_rate], ax
    mov al, cl
    call mrv_reset
    mov word [mrq_vt], mwt_vt
    cmp word [mwt_xseg], 0
    jne .have
    mov ax, MWT_MIXKB
    call OSAPI_MEM_CLAIM
    jc .no
    mov [mwt_xseg], dx
    mov es, dx                      ; the volume pages: page p maps the signed
    xor di, di                      ; sample s to s x p / 62, rounded
    xor bx, bx                      ; BH = the page, BL = the sample
.pg:
    mov al, bl
    cbw
    mov cl, bh
    xor ch, ch
    imul cx                         ; AX = s x p
    mov cx, 62
    or ax, ax
    js .neg
    add ax, 31
    jmp short .dv
.neg:
    sub ax, 31
.dv:
    cwd
    idiv cx
    stosb
    inc bl
    jnz .pg
    inc bh
    cmp bh, MWT_PAGES
    jb .pg
.have:
    mov es, [mwt_xseg]              ; the output table, into the claim
    mov si, mry_otab
    mov di, MWT_OTAB
    mov cx, 1024
    cld
    rep movsw
    xor bx, bx
.v:
    mov byte [mwt_stage+bx], 0
    inc bx
    cmp bx, MWT_MAXV
    jb .v
    clc
    jmp short .out
.no:
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

; mwt_hush - every voice silent now
mwt_hush:
    push bx
    xor bx, bx
.v:
    mov byte [mwt_stage+bx], 0
    mov byte [mrv_st+bx], MRV_FREE
    inc bx
    cmp bx, MWT_MAXV
    jb .v
    pop bx
    ret

; -----------------------------------------------------------------------------
; mwt_on - MRO_ON: AL = channel, AH = key, CL = velocity
; -----------------------------------------------------------------------------
; mwt_place - where a voice's sample is (KERN-MEGA-MEMORY-PLAN MC7): the
; bank's data offset plus the sample's paragraph, as a byte offset A, and
; from it a WINDOW and a START - window A >> 15 of mwt_wins's, the start
; A & 7FFFh in it, and that window's length as the end a voice may have. One
; shape on every kernel (the windows are bseg + 2,048w on a real-mode one),
; and cheaper on the XT than the 128-byte granule sum it replaces, whose
; seven-step 32-bit shift was a loop. A sample past the bank (a bank that
; lies) is placed at window 0 with nothing to play: mwt_on silences it.
; in:  ES:SI -> the sample's row, BX = the voice x 2
; out: [mwt_sseg+bx], [mwt_nbase], [mwt_lim]; preserves every register
mwt_place:
    push ax
    push cx
    push dx
    push di
    mov ax, [es:si+MRS_PARA]
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl                      ; DX:AX = the paragraph, in bytes
    add ax, [mwt_doff]
    adc dx, [mwt_doffh]             ; DX:AX = A
    shl ax, 1
    rcl dx, 1                       ; DX = A >> 15, the window
    shr ax, 1                       ; AX = A & 7FFFh, the start in it
    mov di, dx
    shl di, 1
    cmp di, [mwt_nwin]
    jb .in
    xor di, di                      ; past the bank: window 0, and an end of
    mov [mwt_lim], di               ; 0 that no start is below
    jmp short .put
.in:
    mov cx, [mwt_wlim + di]
    mov [mwt_lim], cx
.put:
    mov cx, [mwt_win + di]
    mov [mwt_sseg+bx], cx
    mov [mwt_nbase], ax
    pop di
    pop dx
    pop cx
    pop ax
    ret

mwt_on:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [mwt_bseg], 0
    je .out
    mov word [mrv_want], 0xFFFF
    mov dl, cl                      ; DL = the velocity
    mov es, [mwt_bseg]
    mov di, [es:MRK_DRUM]           ; DI = the entry: the drum key's...
    mov cl, ah
    xor ch, ch
    cmp al, MR_DRUMCH
    je .ent
    mov di, [es:MRK_PROG]           ; ...or the channel's program's
    push bx
    mov bl, al
    xor bh, bh
    mov cl, [mrc_prog+bx]
    pop bx
.ent:
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add di, cx
    mov si, [es:di+MRE_SAMP]
    cmp si, 0xFFFF
    je .out                         ; nothing in the bank for it: silence
    cmp si, [es:MRK_NSAMP]
    jae .out
    mov cl, dl
    call mrv_alloc                  ; BX = the voice
    mov byte [mwt_drum+bx], 0
    cmp al, MR_DRUMCH
    jne .nd
    mov byte [mwt_drum+bx], 1
.nd:
    mov cl, [es:di+MRE_ROOT]
    mov [mwt_root+bx], cl
    mov cl, [es:di+MRE_FINE]
    mov [mwt_fine+bx], cl
    push di
    mov cx, si                      ; the sample's row: index x 12
    shl si, 1
    add si, cx
    shl si, 1
    shl si, 1
    add si, [es:MRK_SAMP]
    shl bx, 1
    call mwt_place                  ; the voice's segment, and its start
    mov cx, [es:si+MRS_LPE]
    or cx, cx
    jnz .lp
    mov word [mwt_llen+bx], 0
    mov cx, [es:si+MRS_LEN]         ; a one-shot ends at its length...
    jmp short .rt
.lp:
    sub cx, [es:si+MRS_LPS]         ; ...a loop at its end, back by its length
    mov [mwt_llen+bx], cx
    mov cx, [es:si+MRS_LPE]
.rt:
    add cx, [mwt_nbase]             ; both counted from the voice's start
    jc .clip
    cmp cx, [mwt_lim]
    jbe .endok
.clip:
    mov cx, [mwt_lim]               ; only a bank whose sample runs off it
.endok:
    cmp cx, [mwt_nbase]             ; ...or starts past it (mwt_place): no
    jbe .none                       ; byte to play, so no voice - the mixer
    mov [mwt_end+bx], cx            ; reads before it tests the end
    mov cx, [mwt_nbase]
    mov [mwt_ip+bx], cx
    mov word [mwt_fp+bx], 0
    mov cx, [es:si+MRS_RATE]        ; the base step: rate / output, 16.16
    push ax
    push dx
    mov ax, cx
    xor dx, dx
    div word [mwt_rate]             ; AX = whole, DX = remainder
    mov [mwt_bshi+bx], ax
    xor ax, ax
    div word [mwt_rate]
    mov [mwt_bslo+bx], ax
    pop dx
    pop ax
    shr bx, 1
    mov cl, [es:si+MRS_GAIN]
    mov [mwt_gain+bx], cl
    pop di
    call mwt_envset                 ; ES:DI = the entry: the envelope's steps
    push ax
    mov si, ax                      ; SI = the channel, for its volume
    and si, 0x0F
    call mwt_level                  ; DL = the velocity
    pop ax
    mov si, ax
    and si, 0x0F
    cmp byte [mwt_drum+bx], 0
    je .tune
    mov si, 0xFFFF                  ; a drum: no bend
.tune:
    call mwt_tune                   ; AH = the key
    call mwt_page
    jmp short .out
.none:
    pop di                          ; (the entry, pushed for mwt_envset)
    shr bx, 1
    mov byte [mwt_stage+bx], 0      ; the voice it was given goes back
    mov byte [mrv_st+bx], MRV_FREE
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mwt_envset - voice BX's envelope from entry ES:DI: the stage to start in and
; its steps a span. A time T in 10 ms is T x rate / 12,800 spans of 128
; samples; a step is the whole range (127 << 8) over that
; -----------------------------------------------------------------------------
mwt_envset:
    push ax
    push cx
    push dx
    push bx
    shl bx, 1
    mov al, [es:di+MRE_SUS]
    xor ah, ah
    mov ah, al
    xor al, al                      ; AX = sustain << 8
    mov [mwt_sl+bx], ax
    mov al, [es:di+MRE_ATK]
    call mwt_step
    mov [mwt_ad+bx], ax
    mov al, [es:di+MRE_DEC]
    call mwt_step
    mov [mwt_dd+bx], ax
    mov al, [es:di+MRE_REL]
    call mwt_step
    mov [mwt_rd+bx], ax
    mov word [mwt_env+bx], 0
    pop bx
    mov byte [mwt_stage+bx], 1      ; the attack...
    cmp byte [es:di+MRE_ATK], 0
    jne .out
    push bx
    shl bx, 1
    mov word [mwt_env+bx], MWT_FULL ; ...or none at all: at the top, decaying
    pop bx
    mov byte [mwt_stage+bx], 2
.out:
    pop dx
    pop cx
    pop ax
    ret

; mwt_step - AL = a time in 10 ms -> AX = the envelope's step a span
mwt_step:
    push bx
    push cx
    push dx
    xor ah, ah
    mul word [mwt_rate]             ; DX:AX = T x rate
    mov cx, 12800
    cmp dx, cx
    jae .one                        ; (no time that long)
    div cx                          ; AX = spans
    or ax, ax
    jz .one
    mov cx, ax
    mov ax, MWT_FULL
    xor dx, dx
    div cx
    or ax, ax
    jnz .r
    inc ax
    jmp short .r
.one:
    mov ax, MWT_FULL
.r:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mwt_level - voice BX's level from velocity DL, channel SI's volume and
; expression and the sample's gain: v x vol x expr / 127^2 x gain / 127
; -----------------------------------------------------------------------------
mwt_level:
    push ax
    push cx
    push dx
    mov al, dl
    mul byte [mrc_vol+si]           ; AX = v x vol
    mov cl, [mrc_expr+si]
    xor ch, ch
    mul cx                          ; DX:AX = v x vol x expr
    mov cx, 16129
    div cx                          ; AX = 0..127
    mul byte [mwt_gain+bx]          ; AX = x gain
    mov cl, 127
    div cl                          ; AL = 0..127
    mov [mwt_amp+bx], al
    pop dx
    pop cx
    pop ax
    ret

; mwt_page - voice BX's volume page from its level and its envelope
mwt_page:
    push ax
    push bx
    push cx
    mov al, [mwt_amp+bx]
    shl bx, 1
    mul byte [mwt_env+bx+1]         ; AX = amp x env (0..16,129)
    shr bx, 1
    mov cl, 9
    shr ax, cl
    mov [mwt_pg+bx], al
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mwt_tune - voice BX's step for key AH, with channel SI's bend (SI = 0xFFFF:
; none). n = (key - root) x 32 + fine + bend, in 1/32 of a semitone; the step
; is the base x 2^(n / 384): whole octaves a shift, the rest mrt_pow
; -----------------------------------------------------------------------------
mwt_tune:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, ah
    sub al, [mwt_root+bx]           ; signed: key - root
    cbw
    mov cl, 5
    sal ax, cl
    mov cl, [mwt_fine+bx]
    xchg ax, cx
    cbw
    add ax, cx                      ; AX = n
    cmp si, 0xFFFF
    je .nb
    shl si, 1
    add ax, [mrc_bend+si]
.nb:
    add ax, 384 * 16                ; positive: 16 octaves up, taken back below
    xor dx, dx
    mov cx, 384
    div cx                          ; AX = octaves + 16, DX = the rest
    mov di, ax                      ; DI = the octave, biased
    mov si, dx
    shl si, 1
    mov cx, [mrt_pow+si]            ; CX = 2^(rest/384) x 32768
    shl bx, 1
    mov ax, [mwt_bslo+bx]           ; base x CX >> 15, base 16.16 = HI:LO
    mul cx                          ; DX:AX = lo x m
    mov si, dx                      ; SI = its high word (the low is lost to
    mov ax, [mwt_bshi+bx]           ;      the shift)
    mul cx                          ; DX:AX = hi x m
    add ax, si
    adc dx, 0                       ; DX:AX = (base x m) >> 16
    shl ax, 1                       ; ...<< 1: >> 15
    rcl dx, 1
    sub di, 16                      ; the octave: a shift either way
    jz .set
    js .down
.up:
    shl ax, 1
    rcl dx, 1
    dec di
    jnz .up
    jmp short .set
.down:
    shr dx, 1
    rcr ax, 1
    inc di
    jnz .down
.set:
    cmp dx, 64                      ; (a step past 64 samples is not a note)
    jb .ok
    mov dx, 64
.ok:
    mov [mwt_is+bx], dx
    mov [mwt_fs+bx], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the rest of the voice table
; -----------------------------------------------------------------------------
mwt_off:                            ; MRO_OFF: the voice into its release - a
    push bx                         ; drum plays out whatever the file says
    call mrv_off
    jc .out
    jnz .out                        ; held by the pedal
    cmp byte [mwt_drum+bx], 0
    jne .out
    call mwt_relv
.out:
    pop bx
    ret

mwt_relv:
    cmp byte [mwt_stage+bx], 0
    je .r
    mov byte [mwt_stage+bx], 4
.r:
    ret

mwt_susup:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_HELD
    jne .n
    cmp [mrv_ch+bx], al
    jne .n
    mov byte [mrv_st+bx], MRV_REL
    call mwt_relv
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    ret

mwt_alloff:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mrv_st+bx], MRV_FREE
    je .n
    cmp al, 0xFF
    je .k
    cmp [mrv_ch+bx], al
    jne .n
.k:
    mov byte [mrv_st+bx], MRV_REL
    call mwt_relv
.n:
    inc bx
    jmp short .v
.out:
    pop bx
    ret

mwt_ctl:                            ; MRO_CTL: the channel's levels again
    push bx
    push dx
    push si
    mov si, ax
    and si, 0x0F
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mwt_stage+bx], 0
    je .n
    cmp [mrv_ch+bx], al
    jne .n
    mov dl, [mrv_vel+bx]
    call mwt_level
.n:
    inc bx
    jmp short .v
.out:
    pop si
    pop dx
    pop bx
    ret

mwt_bend:                           ; MRO_BEND: the channel's voices retuned
    push ax
    push bx
    push si
    cmp al, MR_DRUMCH
    je .out
    mov si, ax
    and si, 0x0F
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    cmp byte [mwt_stage+bx], 0
    je .n
    cmp [mrv_ch+bx], al
    jne .n
    mov ah, [mrv_key+bx]
    call mwt_tune
.n:
    inc bx
    jmp short .v
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mwt_etick - every voice's envelope a span on, and its volume page
; -----------------------------------------------------------------------------
mwt_etick:
    push ax
    push bx
    push cx
    push si
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .out
    mov al, [mwt_stage+bx]
    or al, al
    jz .n
    mov si, bx
    shl si, 1
    mov cx, [mwt_env+si]
    cmp al, 1
    jne .d
    add cx, [mwt_ad+si]             ; ATTACK, to the top
    jc .top
    cmp cx, MWT_FULL
    jb .set
.top:
    mov cx, MWT_FULL
    mov byte [mwt_stage+bx], 2
    jmp short .set
.d:
    cmp al, 2
    jne .r
    sub cx, [mwt_dd+si]             ; DECAY, to the sustain level
    jb .sus
    cmp cx, [mwt_sl+si]
    ja .set
.sus:
    mov cx, [mwt_sl+si]
    mov byte [mwt_stage+bx], 3
    or cx, cx
    jnz .set
    jmp short .end                  ; (a sustain of nothing: the note is over)
.r:
    cmp al, 4
    jne .set                        ; SUSTAIN: nothing moves
    sub cx, [mwt_rd+si]             ; RELEASE, to nothing
    ja .set
.end:
    xor cx, cx
    mov byte [mwt_stage+bx], 0
    mov byte [mrv_st+bx], MRV_FREE
.set:
    mov [mwt_env+si], cx
    call mwt_page
.n:
    inc bx
    jmp short .v
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mwt_span - one span into the ring at ES:DI, CX = the bytes to the ring's
; end: mry_span's contract and shape, the voices the wavetable's
; -----------------------------------------------------------------------------
mwt_span:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push di
    push cx
    mov es, [mwt_xseg]              ; the sum, cleared
    mov di, MWT_ACC
    mov cx, MWT_SPAN
    xor ax, ax
    cld
    rep stosw
    mov word [mwt_pos], 0
    call mwt_etick
.ev:
    cmp byte [mr_seqon], 0
    je .rest
    call mrq_due                    ; DX:AX = the next event's sample
    jc .rest
    sub ax, [mry_slo]
    sbb dx, [mry_shi]
    jb .now
    or dx, dx
    jnz .rest
    cmp ax, MWT_SPAN
    jae .rest
    jmp short .at
.now:
    xor ax, ax
.at:
    cmp ax, [mwt_pos]
    jb .play
    call mwt_render                 ; the voices up to the event
.play:
    call mrq_step
    jmp short .ev
.rest:
    mov ax, MWT_SPAN
    call mwt_render
    pop cx                          ; CX = bytes to the ring's end
    pop di
    pop es                          ; ES:DI = where the span goes
    mov si, MWT_ACC
    mov dx, [mwt_xseg]              ; DX = the sum's segment (mwt_out keeps it)
    cmp cx, MWT_SPAN
    jae .one
    push cx
    push ds
    mov ds, dx
    call mwt_out                    ; up to the ring's end...
    pop ds
    pop ax
    mov cx, MWT_SPAN
    sub cx, ax
    xor di, di                      ; ...and the rest from its start
.one:
    cmp cx, MWT_SPAN
    jbe .m
    mov cx, MWT_SPAN
.m:
    push ds
    mov ds, dx
    call mwt_out
    pop ds
    add word [mry_slo], MWT_SPAN
    adc word [mry_shi], 0
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mwt_out - CX sums at DS:SI (the mix claim) to card bytes at ES:DI
mwt_out:
    jcxz .r
    cld
.l:
    lodsw
    add ax, 1024 + MWT_OTAB         ; biased, into the table
    mov bx, ax
    mov al, [bx]
    stosb
    loop .l
.r:
    ret

; -----------------------------------------------------------------------------
; mwt_render - every sounding voice from [mwt_pos] up to sample AX
; -----------------------------------------------------------------------------
mwt_render:
    push bx
    xor bx, bx
.v:
    cmp bl, [mrv_n]
    jae .done
    cmp byte [mwt_stage+bx], 0
    je .n
    call mwt_voice
.n:
    inc bx
    jmp short .v
.done:
    mov [mwt_pos], ax
    pop bx
    ret

; -----------------------------------------------------------------------------
; mwt_voice - voice BX from [mwt_pos] up to sample AX: THE LOOP.
;
; DS = the mix claim (BH a volume page and BL the sample byte make DS:BX the
; scaled value; DI walks the sum), ES = the sample, SI.DX the position, BP
; the step's fraction - and the step's WHOLE part an immediate patched into
; the `adc`, which frees CX for `loop`. Eighteen bytes a sample, because on an
; 8088 the bytes ARE the cost (the fetch, PERFORMANCE.md Part 2): the first cut
; was 27 and played 58% of 8 kHz with four voices on the 5150.
;
; NO END TEST in it either. Each run is cut so it cannot reach the sample's
; end - a step moves the position at most (whole + 1) samples, so
; (end - pos - 1) / (whole + 1) samples are always safe - and the sample that
; might cross is made by the careful path below, which loops or ends the
; voice. A patched instruction is reached only through a jump, so it is never
; already in a prefetch queue.
; -----------------------------------------------------------------------------
mwt_voice:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [mwt_vbx], bx
    mov cx, ax                      ; CX = the samples to make
    sub cx, [mwt_pos]
    jbe .out
    mov di, [mwt_pos]               ; DI = the sum, at the span's position
    shl di, 1
    add di, MWT_ACC
    mov ah, [mwt_pg+bx]             ; AH = the page (BH, in the loop)
    shl bx, 1
    mov ax, [mwt_end+bx]
    mov [mwt_vend], ax
    mov ax, [mwt_llen+bx]
    mov [mwt_vll], ax
    mov ax, [mwt_is+bx]
.st:                                ; (PROTECTED MODE: mwt_pmset has made
    mov [cs:..@mwt_vstep+2], ax     ; this a jmp to mwt_voicep) the whole
                                    ; step, into the adc
    ; THE REST TO .done IS ASSEMBLED TWICE (105.8.6): the voice's registers
    ; (MWT_VSET, AX = the whole step), and the loop (MWT_VRUN 1 = the
    ; protected-mode copy, mwt_voicep), whose step is the patched immediate
    ; here and [MWT_VSTEP] - through DS, the mix claim - there. This copy is
    ; byte for byte the loop it always was
%macro MWT_VSET 0
    inc ax
    mov [mwt_vinc], ax              ; ...and the most a step moves
    mov si, [mwt_ip+bx]
    mov dx, [mwt_fp+bx]
    mov bp, [mwt_fs+bx]
    mov es, [mwt_sseg+bx]
    shr bx, 1
    mov bh, [mwt_pg+bx]
    push ds
    mov ds, [mwt_xseg]
%endmacro
    MWT_VSET
    jmp short .run
%macro MWT_VRUN 1
.run:
    ; how many samples are certainly short of the end?
    mov ax, [cs:mwt_vend]
    sub ax, si
    dec ax                          ; (end - pos - 1)
    push dx
    xor dx, dx
    div word [cs:mwt_vinc]          ; AX = safe samples
    pop dx
    or ax, ax
    jz .care                        ; none: the careful path
    cmp ax, cx
    jb .part
    mov ax, cx
.part:
    sub cx, ax                      ; CX = what is left after this run
    push cx
    mov cx, ax
.l:
    mov bl, [es:si]
    mov al, [bx]
    cbw
    add [di], ax
    inc di
    inc di
    add dx, bp
%if %1
    adc si, [MWT_VSTEP]             ; (the step, in the mix claim)
%else
..@mwt_vstep:
    adc si, strict word 0x1234      ; (patched: the whole step)
%endif
    loop .l
    pop cx
%if %1
    jcxz .donej
%else
    jcxz .done
%endif
    jmp short .run
.care:                              ; ONE sample, its crossing tested
    mov bl, [es:si]
    mov al, [bx]
    cbw
    add [di], ax
    inc di
    inc di
    add dx, bp
%if %1
    adc si, [MWT_VSTEP]
%else
    adc si, [cs:..@mwt_vstep+2]
%endif
    cmp si, [cs:mwt_vend]
    jb .cnext
    mov ax, [cs:mwt_vll]
    or ax, ax
%if %1
    jz .endsj
%else
    jz .ends
%endif
.wl:
    sub si, ax                      ; a loop: back by its length (a step can
    cmp si, [cs:mwt_vend]           ; carry past it more than once)
    jae .wl
.cnext:
    dec cx
    jnz .run
%endmacro
    MWT_VRUN 0
.done:
    pop ds
    mov bx, [mwt_vbx]
    shl bx, 1
    mov [mwt_ip+bx], si
    mov [mwt_fp+bx], dx
    jmp short .out
.ends:
    pop ds                          ; a one-shot played out: the voice is
    mov bx, [mwt_vbx]               ; over
    mov byte [mwt_stage+bx], 0
    mov byte [mrv_st+bx], MRV_FREE
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mwt_voicep - mwt_voice's loop for kern_mega (SPEC.md 105.8.6). CS is a
; read-only code alias there, so the patch at .st would trap into the kernel
; once a voice a span: this copy keeps the step in the mix claim at MWT_VSTEP
; and adds it from there - DS IS the mix claim in the loop - and mwt_pmset
; points .st here once, at entry, so kern_big runs mwt_voice unchanged
; -----------------------------------------------------------------------------
%ifdef PKG_MEGA                     ; (the kern_mega arm's alone, 14.66)
mwt_voicep:
    ; STKBALANCE-OK: never called - mwt_voice.st jumps here with its seven registers pushed, and .done/.ends pop them
    mov es, [mwt_xseg]              ; (ES is the sample's again in MWT_VSET)
    mov [es:MWT_VSTEP], ax
    MWT_VSET
    MWT_VRUN 1
.donej:                             ; (falling out of it, as the real-mode
    jmp mwt_voice.done              ; copy falls into .done)
.endsj:
    jmp mwt_voice.ends

; mwt_pmset - from mr_entry under CPU_F_PM (DS = ours, our writable DATA
; selector, so the code is written through it and nothing traps): .st becomes
; a jmp to the protected-mode copy. Preserves all
mwt_pmset:
    mov word [mwt_voice.st+1], mwt_voicep - (mwt_voice.st + 3)
    mov byte [mwt_voice.st], 0xE9
    ret
%endif                              ; PKG_MEGA

    MRW mwt_vend                    ; the voice the loop is on: its end...
    MRW mwt_vinc                    ; ...the most a step moves...
    MRW mwt_vbx                     ; the voice the loop is on
    MRW mwt_vll                     ; ...its loop's length
