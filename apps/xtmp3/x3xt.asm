; =============================================================================
; os8088 - apps/xtmp3/x3xt.asm
;
; XTMP3's XT BACKEND (SPEC.md 121.3, 121.4, 121.10; docs/xtmp3/XT.md): a
; far-called OP_SEG part that turns one granule record into unsigned 8-bit
; mono PCM with product tables instead of multiplications. It is BIT-EXACT to
; tools/x3simxt.py, whose docstring is the chain this file implements step
; for step; tests/xtmp3/X3XT.txt holds the hashes.
;
; Four profiles (XT-STUDY.md 5): XT Standard (16 window terms), XT Fast (8),
; XT Turbo (4, experimental) and XT Speech (16 terms at divisor 2 or 4).
; The part allocates nothing (SPEC.md 20.12.5): CAPS says how many paragraphs
; a profile wants, the primary claims them, and OPEN EXPANDS the profile's
; tables into that claim from XTGEN.BIN (tools/x3xttab.py's generator input)
; by x3xttab's exact integer recurrences, then checks a Fletcher-32 of the
; result against the generator's and refuses on a difference.
;
; THE CLAIM is laid out by tools/x3xtgen.py (the generated x3xtk.inc):
;   W0..W2  the window product rows, 512 bytes a distinct |D|, and every
;           output pair's two doubled V-code rings (128 bytes)
;   SY      ENCV, the 18 slot records (subband samples -> Lee in place ->
;           V codes -> PCM) and Lee's byte-split planes
;   H1      RQL, the 18 IMDCT rows, ENCR, the antialias tables, the lines
;   H2      IMWIN, the overlap, the IMDCT scratch
;
; Every variable lives in this part's own image and is reached through CS.
; Labels are x3x_ (SPEC.md 121).
; =============================================================================

    cpu 8086
    bits 16
    org 0

%include "x3xtk.inc"

%include "x3gran.inc"

; ---- the granule record's fields (apps/xtmp3/x3gran.inc, SPEC.md 121.3)
X3X_R_VER    equ X3G_VER
X3X_R_SRI    equ X3G_SRI
X3X_R_NCH    equ X3G_NCH
X3X_R_CH0    equ X3G_CH0
X3X_R_OK     equ X3G_C_OK
X3X_R_BT     equ X3G_C_BT
X3X_R_MIXED  equ X3G_C_MIX
X3X_R_WINSW  equ X3G_C_WS
X3X_R_NZ     equ X3G_C_NZ
X3X_R_IX     equ X3G_C_IX
X3X_R_Q      equ X3G_C_Q
X3X_R_CHSZ   equ X3G_CSZ
X3X_R_PLAN   equ X3G_PL

; ---- profiles (CAPS' AL and OPEN's X3O_PROF: X3XP_* in SPEC.md 121.3)
X3X_P_STD    equ 0
X3X_P_FAST   equ 1
X3X_P_TURBO  equ 2
X3X_P_SPEECH equ 3

; =============================================================================
; THE PART HEADER (x3gran.inc section 2): far-called through the vectors,
; DS = this part's segment
; =============================================================================
x3x_head:
    dw X3K_SIGVAL
    db X3G_ABI, X3K_XT, X3V_COUNT, 0
    dw x3x_caps                     ; X3V_CAPS
    dw x3x_open                     ; X3V_OPEN
    dw x3x_reset                    ; X3V_RESET
    dw x3x_granule                  ; X3V_GRANULE
    dw x3x_statsv                   ; X3V_STATS
    dw x3x_close                    ; X3V_CLOSE

; =============================================================================
; STATE (this part's own bytes, through CS)
; =============================================================================
x3x_isopen  db 0
x3x_prof    db 0
x3x_div     db 1
x3x_nsb     dw 32                   ; subbands reconstructed: 32 / divisor
x3x_nout    dw 32                   ; outputs a slot
x3x_segsy   dw 0
x3x_segh1   dw 0
x3x_segh2   dw 0
x3x_wseg    dw 0, 0, 0
x3x_nwseg   dw 0
x3x_leefn   dw 0
x3x_winfn   dw 0
x3x_aafn    dw 0
x3x_desc    dw 0                    ; the profile's window segment table
x3x_slot    dw 0                    ; slots decoded since RESET (mod 2^16)
x3x_prevbt  db 0, 0
x3x_clips   dw 0                    ; this granule's output clips
x3x_fmt     db 0                    ; X3O_FMT
x3x_over    db 0                    ; X3O_VER and X3O_SRI: the records
x3x_osri    db 0                    ; GRANULE takes
x3x_gain    dw 0                    ; X3O_GAIN
; STATS (x3gran.inc's X3Z_* layout, copied out)
x3x_stats:
x3x_s_gran  dd 0                    ; X3Z_GRAN
x3x_s_clip  dd 0                    ; X3Z_CLIP
x3x_s_sat   dd 0                    ; X3Z_SAT: requantisation and stereo
x3x_s_stage dw 0, 0, 0, 0, 0, 0, 0, 0  ; X3Z_STAGE: 0 requant, 1 stereo; 6 the
                                    ; V codes past +-2 FS (ENCV's slow path: a
                                    ; clamp to the code map, not in X3Z_SAT)
x3x_s_silent dd 0                   ; X3Z_SILENT
; ...and the reconstruction's own counters (tools/x3xtmarty.py reads them)
x3x_s_fast  dd 0                    ; long subbands through the factored IMDCT
x3x_s_sparse dd 0                   ; ...through the sparse rows
x3x_s_short dd 0                    ; short subbands
x3x_s_zero  dd 0                    ; all-zero subbands
x3x_s_split dd 0                    ; stereo granules hybridised per channel
x3x_s_end:
; granule scratch
x3x_rseg    dw 0                    ; the record's segment
x3x_roff    dw 0                    ; ...and offset
x3x_oseg    dw 0                    ; the PCM buffer
x3x_ooff    dw 0
x3x_ebt     db 0, 0                 ; the effective block of each channel
x3x_emx     db 0, 0
x3x_ews     db 0, 0
x3x_ver     db 0
x3x_sri     db 0
x3x_isms    db 0                    ; X3G_MS: the frame is M/S
x3x_issc    db 0                    ; X3G_ISSC: the MPEG-2 intensity scale
x3x_plan    db 0                    ; X3G_PLAN
x3x_sfbw    times 13 db 0           ; X3G_SFBS: this rate's short-band widths
x3x_mode    db 0                    ; 0 normal, 1 store YS, 2 average YS
x3x_bt      db 0                    ; the channel being reconstructed
x3x_mixed   db 0
x3x_winsw   db 0
x3x_sb      dw 0
x3x_end     dw 0
x3x_t       dw 0
x3x_nruns   dw 0
x3x_runs    times 3 * 41 dw 0       ; (count, dest byte offset, stride)
x3x_lv      times 128 dw 0          ; a code map's levels, at OPEN
x3x_gseg    dw 0                    ; XTGEN.BIN's paragraph in this part
x3x_fs1     dw 0                    ; the Fletcher sums carried
x3x_fs2     dw 0
x3x_cnt     dw 0
x3x_ptr     dw 0

; a dword counter in this part, += 1
%macro X3X_INC32 1
    add word [cs:%1], 1
    adc word [cs:%1 + 2], 0
%endmacro
; X3Z_STAGE word %1 += 1, saturating at FFFFh
%macro X3X_STG 1
    add word [cs:x3x_s_stage + 2 * %1], 1
    sbb word [cs:x3x_s_stage + 2 * %1], 0
%endmacro

; =============================================================================
; CAPS - in: AL = profile, AH = divisor, CL = output channels, ES:DI -> an
; X3C_SIZE record. out: CF = 0, the record filled; CF = 1, AX = X3E_PROFILE /
; X3E_DIV / X3E_CHAN. Keeps every register but AX.
; =============================================================================
x3x_caps:
    push bx
    push cx
    push si
    push di
    call x3x_chkpd                  ; BX = the profile's CAPS block
    jc .out
    mov ax, X3E_CHAN                ; the XT backend's output is mono
    cmp cl, 1
    stc
    jne .out
    cld
    mov byte [es:di + X3C_OFFER], 0Fh
    inc di
    mov si, bx
    mov cx, X3C_PARTKB - X3C_NSEG   ; NSEG, PARA[6], TKB: the block
.cp: mov al, [cs:si]
    stosb
    inc si
    loop .cp
    mov word [es:di], strict word (x3x_imgend - x3x_head + 1023) / 1024   ; X3C_PARTKB
    clc
.out:
    pop di
    pop si
    pop cx
    pop bx
    retf

; x3x_chkpd - AL = profile, AH = divisor: BX = its CAPS block (db nseg, 6 dw
; paragraphs, dw table KB). CF = 1, AX = X3E_PROFILE / X3E_DIV otherwise.
x3x_chkpd:
    cmp al, X3X_P_SPEECH
    ja .badp
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov bx, [cs:x3x_captab + bx]
    cmp al, X3X_P_SPEECH
    je .sp
    cmp ah, 1
    jne .badd
    clc
    ret
.sp:
    cmp ah, 2
    je .ok
    cmp ah, 4
    jne .badd
.ok:
    clc
    ret
.badp:
    mov ax, X3E_PROFILE
    stc
    ret
.badd:
    mov ax, X3E_DIV
    stc
    ret

x3x_captab dw x3x_std_caps, x3x_fast_caps, x3x_turbo_caps, x3x_speech_caps

; CLOSE - nothing to free: the primary owns every claim
x3x_close:
    mov byte [cs:x3x_isopen], 0
    clc
    retf

; STATS - in: ES:DI -> X3Z_SIZE bytes: the counters since OPEN
x3x_statsv:
    push cx
    push si
    push di
    push ds
    push cs
    pop ds
    mov si, x3x_stats
    mov cx, X3Z_SIZE / 2
    cld
    rep movsw
    pop ds
    pop di
    pop si
    pop cx
    clc
    retf

; =============================================================================
; OPEN - in: ES:DI -> an X3O_SIZE open record: profile, divisor, one output
; channel, U8 or S16, the version and rate index, the gain, and the segments
; CAPS asked for. Expands the profile's tables into them from XTGEN.BIN
; (carried inside this part) and checks them. out: CF = 0, history clear;
; CF = 1, AX = X3E_PROFILE / X3E_DIV / X3E_CHAN / X3E_FMT / X3E_MEM /
; X3E_RECORD / X3E_TABLE. Keeps every register but AX.
; =============================================================================
x3x_open:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    cld
    mov byte [cs:x3x_isopen], 0
    mov al, [es:di + X3O_PROF]
    mov ah, [es:di + X3O_DIV]
    call x3x_chkpd
    jc .out
    mov [cs:x3x_prof], al
    mov [cs:x3x_div], ah
    mov si, bx                      ; the CAPS block
    mov ax, X3E_CHAN
    cmp byte [es:di + X3O_OCH], 1
    jne .bad
    mov ax, X3E_FMT
    mov cl, [es:di + X3O_FMT]
    cmp cl, X3_FMT_U8
    ja .bad
    mov [cs:x3x_fmt], cl
    mov ax, [es:di + X3O_GAIN]
    mov [cs:x3x_gain], ax
    ; the stream: version 1, 2 or 25, rate index 0..2; XT Speech's divisor
    ; is its source's (SPEC.md 121.4): 2 on MPEG-2.5, 4 on MPEG-2
    mov ax, X3E_RECORD
    mov cl, [es:di + X3O_VER]
    mov ch, [es:di + X3O_SRI]
    cmp ch, 2
    ja .bad
    cmp cl, 1
    je .verok
    cmp cl, 2
    je .verok
    cmp cl, 25
    jne .bad
.verok:
    mov [cs:x3x_over], cl
    mov [cs:x3x_osri], ch
    cmp byte [cs:x3x_prof], X3X_P_SPEECH
    jne .divok
    mov ax, X3E_DIV
    mov ch, 2
    cmp cl, 25
    je .spd
    mov ch, 4
    cmp cl, 2
    jne .bad                        ; MPEG-1: no 4-6 kHz output there
.spd:
    cmp ch, [cs:x3x_div]
    jne .bad
.divok:
    ; the segments: NSEG of them, none zero
    mov ax, X3E_MEM
    mov cl, [cs:si]
    cmp cl, [es:di + X3O_NSEG]
    jne .bad
    xor ch, ch
    lea bx, [di + X3O_SEG]
.sg: cmp word [es:bx], 0
    je .bad
    inc bx
    inc bx
    loop .sg
    mov ax, 32
    mov cl, [cs:x3x_div]
    div cl                          ; AL = 32 / divisor
    xor ah, ah
    mov [cs:x3x_nsb], ax
    mov [cs:x3x_nout], ax
    ; the profile's routines
    mov bl, [cs:x3x_prof]
    xor bh, bh
    shl bx, 1
    mov bx, [cs:x3x_ptab + bx]      ; the profile's descriptor
    mov ax, [cs:bx]                 ; window segments
    mov [cs:x3x_nwseg], ax
    mov ax, [cs:bx + 2]
    mov [cs:x3x_desc], ax
    mov ax, [cs:bx + 4]
    mov [cs:x3x_leefn], ax
    mov ax, [cs:bx + 6]
    mov [cs:x3x_winfn], ax
    mov ax, [cs:bx + 8]
    mov [cs:x3x_aafn], ax
    cmp byte [cs:x3x_div], 4
    jne .w4
    mov word [cs:x3x_leefn], x3x_lee8
    mov word [cs:x3x_winfn], x3x_win_sp4
.w4:
    ; X3O_SEG: the window segments, then SY, H1, H2
    mov cx, [cs:x3x_nwseg]
    lea si, [di + X3O_SEG]
    xor bx, bx
.ws: mov ax, [es:si]
    mov [cs:x3x_wseg + bx], ax
    inc si
    inc si
    inc bx
    inc bx
    loop .ws
    mov ax, [es:si]
    mov [cs:x3x_segsy], ax
    mov ax, [es:si + 2]
    mov [cs:x3x_segh1], ax
    mov ax, [es:si + 4]
    mov [cs:x3x_segh2], ax
    ; XTGEN.BIN, paragraph-aligned in this image
    mov ax, cs
    add ax, strict word (x3x_gen - $$) / 16
    mov [cs:x3x_gseg], ax
    mov es, ax
    call x3x_gencheck
    mov ax, X3E_TABLE
    jc .bad
    ; expand
    mov word [cs:x3x_fs1], 0
    mov word [cs:x3x_fs2], 0
    call x3x_xwin                   ; the window rows (and their fl32)
    call x3x_xsy                    ; ENCV and Lee planes
    call x3x_xh1                    ; RQL, IMDCT rows, ENCR, antialias
    call x3x_xh2                    ; IMWIN
    ; the check
    mov bl, [cs:x3x_prof]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, X3E_TABLE
    mov cx, [cs:x3x_fltab + bx]
    cmp cx, [cs:x3x_fs1]
    jne .bad
    mov cx, [cs:x3x_fltab + bx + 2]
    cmp cx, [cs:x3x_fs2]
    jne .bad
    ; the counters since OPEN
    push cs
    pop es
    mov di, x3x_stats
    mov cx, (x3x_s_end - x3x_stats) / 2
    xor ax, ax
    rep stosw
    mov byte [cs:x3x_isopen], 1
    call x3x_doreset
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; per profile: window segments, their table, the Lee, window and antialias
; routines
x3x_ptab dw x3x_pd_std, x3x_pd_fast, x3x_pd_turbo, x3x_pd_speech
x3x_pd_std    dw X3X_STD_NWSEG, x3x_std_wsegs, x3x_lee32, x3x_win_std, x3x_aa_split
x3x_pd_fast   dw X3X_FAST_NWSEG, x3x_fast_wsegs, x3x_lee32, x3x_win_fast, x3x_aa_tab
x3x_pd_turbo  dw X3X_TURBO_NWSEG, x3x_turbo_wsegs, x3x_lee32, x3x_win_turbo, x3x_aa_tab
x3x_pd_speech dw X3X_SPEECH_NWSEG, x3x_speech_wsegs, x3x_lee16, x3x_win_sp2, x3x_aa_split
x3x_fltab dw X3X_STD_FL32_LO, X3X_STD_FL32_HI, X3X_FAST_FL32_LO, X3X_FAST_FL32_HI
          dw X3X_TURBO_FL32_LO, X3X_TURBO_FL32_HI, X3X_SPEECH_FL32_LO, X3X_SPEECH_FL32_HI
x3x_leetab dw x3x_std_lee, x3x_std_lee, x3x_std_lee, x3x_speech_lee
x3x_nleetab dw X3X_STD_NLEE, X3X_FAST_NLEE, X3X_TURBO_NLEE, X3X_SPEECH_NLEE

; x3x_gencheck - ES = XTGEN.BIN. CF = 1 when it is not the one built against.
x3x_gencheck:
    cmp word [es:0], 'X3'
    jne .bad
    cmp word [es:2], 'XT'
    jne .bad
    cmp word [es:4], X3X_GEN_VERSION
    jne .bad
    push ds
    push es
    pop ds
    xor si, si
    mov cx, X3X_GEN_BYTES / 2
    xor bx, bx
    xor dx, dx
.f: lodsw
    add bx, ax
    add dx, bx
    loop .f
    pop ds
    cmp bx, X3X_GEN_FL32_LO
    jne .bad
    cmp dx, X3X_GEN_FL32_HI
    jne .bad
    clc
    ret
.bad:
    stc
    ret

; x3x_fl32 - DS:SI, CX words: the Fletcher sums carried in x3x_fs1/fs2
x3x_fl32:
    push ax
    push bx
    push dx
    mov bx, [cs:x3x_fs1]
    mov dx, [cs:x3x_fs2]
.f: lodsw
    add bx, ax
    add dx, bx
    loop .f
.r: mov [cs:x3x_fs1], bx            ; (.r: where a profile run ends a check)
    mov [cs:x3x_fs2], dx
    pop dx
    pop bx
    pop ax
    ret

; x3x_vlevels - the V map's 128 levels into x3x_lv (s0 and the segment
; lengths from XTGEN.BIN, every step a power of two: XT-STUDY.md 8)
x3x_vlevels:
    push ds
    mov ds, [cs:x3x_gseg]
    mov di, x3x_lv
    xor ax, ax
    mov [cs:di], ax
    inc di
    inc di
    mov cl, [XG_VMAP + 1]           ; s0
    mov dx, 1
    shl dx, cl                      ; the step
    xor ch, ch
    mov cl, [XG_VMAP + 2]           ; segments
    mov si, XG_VMAP + 3
.s: push cx
    mov cl, [si]
    inc si
.l: add ax, dx
    mov [cs:di], ax
    inc di
    inc di
    loop .l
    shl dx, 1
    pop cx
    loop .s
    pop ds
    ret

; x3x_rlevels - the R map's levels into x3x_lv
x3x_rlevels:
    push ds
    mov ds, [cs:x3x_gseg]
    mov si, XG_RMAP + 2
    mov di, x3x_lv
    mov cx, 128
.l: lodsw
    mov [cs:di], ax
    inc di
    inc di
    loop .l
    pop ds
    ret

; ---------------------------------------------------------------- the rows
; x3x_xwin - every window segment: rows by x3xttab's recurrence (the
; running sum's high word, then the negation pass), rings zeroed; fl32 over
; the rows
x3x_xwin:
    mov word [cs:x3x_t], 0
.seg:
    mov bx, [cs:x3x_t]
    cmp bx, [cs:x3x_nwseg]
    jae .done
    shl bx, 1
    mov es, [cs:x3x_wseg + bx]
    shl bx, 1
    add bx, [cs:x3x_t]
    shl bx, 1                       ; 10 bytes a descriptor
    add bx, [cs:x3x_desc]
    mov cx, [cs:bx + 2]             ; rows
    mov si, [cs:bx + 8]             ; their |D| list (dd)
    push bx
    xor di, di
.row:
    push cx
    push si
    mov ax, [cs:si]                 ; ib, low word
    mov dx, [cs:si + 2]             ; ...and high
    call x3x_xrow
    pop si
    pop cx
    add si, 4
    add di, 512
    loop .row
    pop bx
    ; the rings
    push di
    mov cx, [cs:bx + 6]
    shr cx, 1
    xor ax, ax
    rep stosw
    pop di
    ; fl32 over the rows
    push ds
    push es
    pop ds
    xor si, si
    mov cx, [cs:bx + 2]
    mov ch, cl
    xor cl, cl                      ; 256 words a row
    call x3x_fl32
    pop ds
    inc word [cs:x3x_t]
    jmp short .seg
.done:
    ret

; x3x_xrow - one row at ES:DI for |D| = DX:AX (units of 2^-16)
x3x_xrow:
    push di
    mov word [es:di], 0             ; code -128: unused, 0
    mov word [es:di + 256], 0       ; code 0
    ; B = 4 ib << s0
    mov bx, ax
    mov bp, dx
    shl bx, 1
    rcl bp, 1
    shl bx, 1
    rcl bp, 1
    push ds
    mov ds, [cs:x3x_gseg]
    mov cl, [XG_VMAP + 1]
    xor ch, ch
    jcxz .s0
.sh: shl bx, 1
    rcl bp, 1
    loop .sh
.s0:
    mov ax, 8000h                   ; A = 2^15
    xor dx, dx
    add di, 258
    mov cl, [XG_VMAP + 2]
    mov si, XG_VMAP + 3
.seg:
    push cx
    mov cl, [si]
    inc si
.ent:
    add ax, bx
    adc dx, bp
    mov [es:di], dx
    inc di
    inc di
    loop .ent
    shl bx, 1                       ; the next segment's step doubles
    rcl bp, 1
    pop cx
    loop .seg
    pop ds
    pop di
    ; the negative half: T[-k] = -T[k]
    push di
    lea si, [di + 258]
    lea bx, [di + 254]
    mov cx, 127
.neg:
    mov ax, [es:si]
    neg ax
    mov [es:bx], ax
    inc si
    inc si
    dec bx
    dec bx
    loop .neg
    pop di
    ret

; ---------------------------------------------------------------- SY
x3x_xsy:
    mov es, [cs:x3x_segsy]
    ; ENCV: the walk over the V levels, nearest level, ties toward zero
    call x3x_vlevels
    mov di, X3X_SY_ENCV + 4096
    xor bx, bx                      ; k
    xor dx, dx                      ; v
.v: ; advance k while 2v > lv[k] + lv[k+1]
.adv:
    cmp bx, 127
    jae .got
    mov si, bx
    shl si, 1
    mov ax, [cs:x3x_lv + si]
    add ax, [cs:x3x_lv + si + 2]
    mov cx, dx
    shl cx, 1
    cmp cx, ax
    jbe .got
    inc bx
    jmp short .adv
.got:
    cmp dx, 4096
    jae .neg
    mov [es:di], bl                 ; ENCV[4096 + v] = k
.neg:
    or dx, dx
    jz .nx
    mov al, bl
    neg al
    mov si, X3X_SY_ENCV + 4096
    sub si, dx
    mov [es:si], al                 ; ENCV[4096 - v] = -k
.nx:
    inc di
    inc dx
    cmp dx, 4096
    jbe .v
    ; fl32 over ENCV
    push ds
    push es
    pop ds
    mov si, X3X_SY_ENCV
    mov cx, 4096
    call x3x_fl32
    pop ds
    ; Lee's planes
    mov bl, [cs:x3x_prof]
    xor bh, bh
    shl bx, 1
    mov si, [cs:x3x_leetab + bx]
    mov cx, [cs:x3x_nleetab + bx]
    mov di, X3X_SY_LEE
    push di
    push cx
.lee:
    push cx
    push si
    mov bl, [cs:si]                 ; the constant's index in LEE_C16
    xor bh, bh
    shl bx, 1
    shl bx, 1
    push ds
    mov ds, [cs:x3x_gseg]
    mov ax, [XG_LEE + bx]
    mov dx, [XG_LEE + bx + 2]
    pop ds
    call x3x_xsplit
    pop si
    pop cx
    inc si
    add di, 1024
    loop .lee
    pop cx
    pop si
    push ds
    push es
    pop ds
    mov ch, cl
    xor cl, cl
    shl cx, 1                       ; 512 words a constant
    call x3x_fl32
    pop ds
    ret

; x3x_xsplit - the four 256-byte planes of a byte-split constant c16 = DX:AX
; at ES:DI: L low, L high, H low, H high (x3xttab.split_recurrence).
; Preserves DI; clobbers AX, BX, CX, DX.
x3x_xsplit:
    mov [cs:x3x_c16], ax
    mov [cs:x3x_c16 + 2], dx
    ; L[l] = A >> 16, A = 2^15 + l c16
    mov ax, 8000h
    xor dx, dx
    xor bx, bx
.l: mov [es:bx + di], dl
    mov [es:bx + di + 256], dh
    add ax, [cs:x3x_c16]
    adc dx, [cs:x3x_c16 + 2]
    inc bx
    cmp bx, 256
    jb .l
    ; H[h] = (A >> 8) mod 2^16, A = 2^7 + h c16; H[-h] = -H[h]
    mov ax, 80h
    xor dx, dx
    xor bx, bx                      ; h
.h: mov cl, ah
    mov ch, dl                      ; CX = (A >> 8) & 0FFFFh
    cmp bx, 128
    jae .hn
    mov [es:bx + di + 512], cl
    mov [es:bx + di + 768], ch
.hn:
    or bx, bx
    jz .h1
    push bx
    neg bx
    and bx, 0FFh
    neg cx
    mov [es:bx + di + 512], cl
    mov [es:bx + di + 768], ch
    pop bx
.h1:
    add ax, [cs:x3x_c16]
    adc dx, [cs:x3x_c16 + 2]
    inc bx
    cmp bx, 128
    jbe .h
    ret

x3x_c16 dw 0, 0

; ---------------------------------------------------------------- H1
x3x_xh1:
    mov es, [cs:x3x_segh1]
    ; RQL: copied (88 rows of 16 words)
    push ds
    mov ds, [cs:x3x_gseg]
    mov si, XG_RQL
    mov di, X3X_H1_RQL
    mov cx, XG_NRQ * 16
    rep movsw
    pop ds
    push ds
    push es
    pop ds
    mov si, X3X_H1_RQL
    mov cx, XG_NRQ * 16
    call x3x_fl32
    pop ds
    ; the IMDCT rows: one mul an entry over the R map
    call x3x_rlevels
    push ds
    mov ds, [cs:x3x_gseg]
    mov si, XG_COS15
    mov di, X3X_H1_IMR
    mov cx, 18
.im:
    lodsw
    call x3x_xmulrow
    add di, 512
    loop .im
    pop ds
    push ds
    push es
    pop ds
    mov si, X3X_H1_IMR
    mov cx, 18 * 256
    call x3x_fl32
    pop ds
    ; ENCR: the walk with at(b) = 4b; then the sentinel
    mov di, X3X_H1_ENCR + 8192
    xor bx, bx                      ; k
    xor dx, dx                      ; b
.v:
.adv:
    cmp bx, 127
    jae .got
    mov si, bx
    shl si, 1
    mov ax, [cs:x3x_lv + si]
    add ax, [cs:x3x_lv + si + 2]
    mov cx, dx
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; 2 at(b) = 8b (b < 8192 here)
    cmp cx, ax
    jbe .got
    inc bx
    jmp short .adv
.got:
    cmp dx, 8192
    jae .neg
    mov [es:di], bl
.neg:
    or dx, dx
    jz .nx
    mov al, bl
    neg al
    mov si, X3X_H1_ENCR + 8192
    sub si, dx
    mov [es:si], al
.nx:
    inc di
    inc dx
    cmp dx, 8192
    jb .v
    ; b = 8192: k is 127 by then (8 x 8191 is past every midpoint)
    mov byte [es:X3X_H1_ENCR], -127
    mov word [es:X3X_H1_ENCR + 16384], 127
    push ds
    push es
    pop ds
    mov si, X3X_H1_ENCR
    mov cx, 8193
    call x3x_fl32
    pop ds
    ; the antialias: byte-split (std, speech) or code rows (fast, turbo)
    cmp word [cs:x3x_aafn], x3x_aa_tab
    je .aatab
    mov di, X3X_H1_AA
    xor si, si
    mov cx, 16
.aas:
    push cx
    push si
    push ds
    mov ds, [cs:x3x_gseg]
    mov ax, [XG_AA16 + si]
    mov dx, [XG_AA16 + si + 2]
    pop ds
    call x3x_xsplit
    pop si
    pop cx
    add si, 4
    add di, 1024
    loop .aas
    mov cx, 16 * 512
    jmp short .aafl
.aatab:
    push ds
    mov ds, [cs:x3x_gseg]
    mov si, XG_AA15
    mov di, X3X_H1_AA
    mov cx, 16
.aat:
    lodsw
    call x3x_xmulrow
    add di, 512
    loop .aat
    pop ds
    mov cx, 16 * 256
.aafl:
    push ds
    push es
    pop ds
    mov si, X3X_H1_AA
    call x3x_fl32
    pop ds
    ret

; x3x_xmulrow - ES:DI a 512-byte row of T[k] = sign(k) round(c lv[|k|] /
; 2^15), c = AX, over x3x_lv (x3xttab.mul_row). Preserves SI, DI, CX, DS.
x3x_xmulrow:
    push cx
    push si
    push di
    mov bp, ax
    mov word [es:di], 0
    xor si, si                      ; 2k
    add di, 256
.e: mov ax, [cs:x3x_lv + si]
    mul bp
    add ax, 4000h
    adc dx, 0
    shl ax, 1
    rcl dx, 1                       ; DX = (c lv + 2^14) >> 15
    mov bx, di
    add bx, si
    mov [es:bx], dx
    mov bx, di
    sub bx, si
    neg dx
    mov [es:bx], dx
    inc si
    inc si
    cmp si, 256
    jb .e
    pop di
    pop si
    pop cx
    ret

; ---------------------------------------------------------------- H2
x3x_xh2:
    mov es, [cs:x3x_segh2]
    mov di, X3X_H2_IMWIN
    xor si, si
    mov cx, 19
.w: push cx
    push si
    push ds
    mov ds, [cs:x3x_gseg]
    mov ax, [XG_WIN16 + si]
    mov dx, [XG_WIN16 + si + 2]
    pop ds
    call x3x_xsplit
    pop si
    pop cx
    add si, 4
    add di, 1024
    loop .w
    push ds
    push es
    pop ds
    mov si, X3X_H2_IMWIN
    mov cx, 19 * 512
    call x3x_fl32
    pop ds
    ret

; =============================================================================
; RESET - clears every history: the overlap, the V rings, the slot count and
; the window shape silence carries. in: nothing. out: CF = 0.
; =============================================================================
x3x_reset:
    cmp byte [cs:x3x_isopen], 0
    je .no
    call x3x_doreset
.no:
    clc
    retf

x3x_doreset:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    mov es, [cs:x3x_segh2]
    mov di, X3X_H2_OVL
    mov cx, 32 * 18
    xor ax, ax
    rep stosw
    xor bx, bx                      ; the window segment
.ring:
    cmp bx, [cs:x3x_nwseg]
    jae .rd
    mov si, bx
    shl si, 1
    mov es, [cs:x3x_wseg + si]
    mov ax, 10
    mul bx
    mov si, ax
    add si, [cs:x3x_desc]
    mov di, [cs:si + 4]             ; the rings
    mov cx, [cs:si + 6]
    shr cx, 1
    xor ax, ax
    rep stosw
    inc bx
    jmp short .ring
.rd:
    mov word [cs:x3x_slot], 0
    mov word [cs:x3x_prevbt], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; GRANULE - in: ES:SI -> the granule record (x3gran.inc), DX:DI -> the PCM:
; 576 / divisor mono samples, U8 or S16 as OPEN said. out: CF = 0, CX =
; samples written, AX = this granule's output clips (before any gain); CF =
; 1, AX = X3E_NOTOPEN / X3E_RECORD (another version or rate than OPEN's).
; Keeps every register but AX and CX.
; =============================================================================
x3x_granule:
    push bx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    cld
    mov ax, X3E_NOTOPEN
    cmp byte [cs:x3x_isopen], 0
    je .bad
    mov ax, X3E_RECORD
    mov bl, [es:si + X3G_VER]
    cmp bl, [cs:x3x_over]
    jne .bad
    mov bl, [es:si + X3G_SRI]
    cmp bl, [cs:x3x_osri]
    jne .bad
    mov bl, [es:si + X3G_NCH]
    dec bl
    cmp bl, 1
    jbe .go
.bad:
    stc
    jmp .out
.go:
    mov [cs:x3x_rseg], es
    mov [cs:x3x_roff], si
    mov [cs:x3x_oseg], dx
    mov [cs:x3x_ooff], di
    mov word [cs:x3x_clips], 0
    X3X_INC32 x3x_s_gran
    mov al, [es:si + X3G_VER]
    mov [cs:x3x_ver], al
    mov al, [es:si + X3G_SRI]
    mov [cs:x3x_sri], al
    mov al, [es:si + X3G_MS]
    mov [cs:x3x_isms], al
    mov al, [es:si + X3G_ISSC]
    mov [cs:x3x_issc], al
    mov al, [es:si + X3G_PLAN]
    cmp al, X3G_PLAN_PL
    jbe .pl
    xor al, al
.pl:
    mov [cs:x3x_plan], al
    push si
    add si, X3G_SFBS
    mov bx, x3x_sfbw
    mov cx, 13
.w: mov al, [es:si]
    mov [cs:bx], al
    inc si
    inc bx
    loop .w
    pop si
    ; X3Z_SILENT: this frame's channels decoded as silence
    cmp byte [es:si + X3G_CH0 + X3G_C_OK], 0
    jne .s0
    X3X_INC32 x3x_s_silent
.s0:
    cmp byte [es:si + X3G_NCH], 2
    jne .s1
    cmp byte [es:si + X3G_CH1 + X3G_C_OK], 0
    jne .s1
    X3X_INC32 x3x_s_silent
.s1:
    ; the effective blocks (a silent channel continues the window shape)
    xor bx, bx
    call x3x_effgi
    cmp byte [es:si + X3G_NCH], 2
    je .stereo
    mov byte [cs:x3x_plan], 0
    mov bx, 0
    call x3x_setgi
    mov bx, [cs:x3x_roff]
    add bx, X3G_CH0
    call x3x_rqmono
    mov byte [cs:x3x_mode], 0
    call x3x_hybrid
    jmp .synth
.stereo:
    mov bx, 1
    call x3x_effgi
    mov bx, [cs:x3x_roff]
    add bx, X3G_CH0
    mov di, X3X_H1_XL
    call x3x_rqplain
    mov bx, [cs:x3x_roff]
    add bx, X3G_CH1
    mov di, X3X_H1_XR
    call x3x_rqplain
    mov ax, word [cs:x3x_ebt]
    cmp al, ah
    jne .split
    mov ax, word [cs:x3x_emx]
    cmp al, ah
    jne .split
    ; the keys agree: one spectrum, one hybrid
    mov bx, 0
    call x3x_setgi
    call x3x_mix
    mov byte [cs:x3x_mode], 0
    call x3x_hybrid
    jmp .synth
.split:
    X3X_INC32 x3x_s_split
    cmp byte [cs:x3x_plan], 0
    je .noplan
    call x3x_stproc
.noplan:
    ; the split path: either channel may be nonzero up to the larger nz
    mov es, [cs:x3x_rseg]
    mov si, [cs:x3x_roff]
    xor ax, ax
    cmp byte [es:si + X3G_CH0 + X3G_C_OK], 0
    je .sz1
    mov ax, [es:si + X3G_CH0 + X3G_C_NZ]
.sz1:
    cmp byte [es:si + X3G_CH1 + X3G_C_OK], 0
    je .sz2
    cmp ax, [es:si + X3G_CH1 + X3G_C_NZ]
    jae .sz2
    mov ax, [es:si + X3G_CH1 + X3G_C_NZ]
.sz2:
    cmp ax, 576
    jbe .sz3
    mov ax, 576
.sz3:
    mov [cs:x3x_nzsplit], ax
    mov bx, 0
    call x3x_setgi
    call x3x_splitnz
    mov si, X3X_H1_XL
    call x3x_place
    mov byte [cs:x3x_mode], 1
    call x3x_hybrid
    mov bx, 1
    call x3x_setgi
    call x3x_splitnz
    mov si, X3X_H1_XR
    call x3x_place
    mov byte [cs:x3x_mode], 2
    call x3x_hybrid
.synth:
    call x3x_synth
    call x3x_pcmout
    mov ax, [cs:x3x_clips]
    add [cs:x3x_s_clip], ax
    adc word [cs:x3x_s_clip + 2], 0
    push ax
    mov ax, [cs:x3x_nout]
    mov cx, 18
    mul cx
    mov cx, ax                      ; CX = samples
    pop ax                          ; AX = clips
    clc
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    retf

; x3x_pcmout - the slot records' PCM to the caller's buffer, as U8 (the
; model's own bytes) or S16 ((u8 - 128) << 8); with X3O_GAIN, each sample as
; an int16 times gain / 256, floored and saturated (x3x_gain16), then written
x3x_pcmout:
    mov ds, [cs:x3x_segsy]
    mov es, [cs:x3x_oseg]
    mov di, [cs:x3x_ooff]
    mov si, X3X_SY_SLOT + X3X_SLOT_PCM
    mov dx, 18
    cmp byte [cs:x3x_fmt], X3_FMT_U8
    jne .any
    cmp word [cs:x3x_gain], 0
    jne .any
.u8:
    push si
    mov cx, [cs:x3x_nout]
    rep movsb
    pop si
    add si, X3X_SLOTSZ
    dec dx
    jnz .u8
    ret
.any:
    push si
    mov cx, [cs:x3x_nout]
.s: lodsb
    xor al, 80h
    mov ah, al
    xor al, al                      ; the int16: (u8 - 128) << 8
    cmp word [cs:x3x_gain], 0
    je .put
    call x3x_gain16
.put:
    cmp byte [cs:x3x_fmt], X3_FMT_U8
    je .b
    stosw
    loop .s
    jmp short .ns
.b: mov al, ah
    xor al, 80h
    stosb
    loop .s
.ns:
    pop si
    add si, X3X_SLOTSZ
    dec dx
    jnz .any
    ret

; x3x_gain16 - AX = sat((AX gain) >> 8), the shift floored. Keeps the rest.
x3x_gain16:
    push dx
    imul word [cs:x3x_gain]
    mov al, ah
    mov ah, dl                      ; bits 8..23 of the product
    or dh, dh
    jz .p
    cmp dh, 0FFh
    jne .sat
    test dl, 80h
    jnz .ok
    jmp short .sat
.p: test dl, 80h
    jz .ok
.sat:
    mov ax, 7FFFh
    or dx, dx
    jns .ok
    inc ax
.ok:
    pop dx
    ret

; x3x_effgi - BX = channel (0, 1); ES:[x3x_roff] the record. The channel's
; effective block: the record's, or for a silent channel x3sim's
; _silent_gi (a stop block after a start or short one, else normal)
x3x_effgi:
    push ax
    push si
    mov si, [cs:x3x_roff]
    or bx, bx
    jz .c0
    add si, X3X_R_CHSZ
.c0:
    cmp byte [es:si + X3X_R_CH0 + X3X_R_OK], 0
    je .silent
    mov al, [es:si + X3X_R_CH0 + X3X_R_BT]
    mov [cs:x3x_ebt + bx], al
    mov al, [es:si + X3X_R_CH0 + X3X_R_MIXED]
    mov [cs:x3x_emx + bx], al
    mov al, [es:si + X3X_R_CH0 + X3X_R_WINSW]
    mov [cs:x3x_ews + bx], al
    jmp short .set
.silent:
    mov al, [cs:x3x_prevbt + bx]
    cmp al, 1
    je .stop
    cmp al, 2
    je .stop
    xor al, al
    mov ah, 0
    jmp short .s2
.stop:
    mov al, 3
    mov ah, 1
.s2:
    mov [cs:x3x_ebt + bx], al
    mov byte [cs:x3x_emx + bx], 0
    mov [cs:x3x_ews + bx], ah
.set:
    mov al, [cs:x3x_ebt + bx]
    mov [cs:x3x_prevbt + bx], al
    pop si
    pop ax
    ret

; x3x_setgi - BX = channel: its effective block becomes the one the hybrid
; reconstructs, and its runs are built
x3x_setgi:
    mov al, [cs:x3x_ebt + bx]
    mov [cs:x3x_bt], al
    mov al, [cs:x3x_emx + bx]
    mov [cs:x3x_mixed], al
    mov al, [cs:x3x_ews + bx]
    mov [cs:x3x_winsw], al
    jmp x3x_mkruns

; x3x_mkruns - the bitstream-to-subband placement of the current block as
; runs (count, destination byte offset in a line buffer, stride): one run
; for a long block; for a short one window by window (x3sim.reorder_index:
; [3k + w] within a subband), after a mixed block's 36 long lines
x3x_mkruns:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, x3x_runs
    cmp byte [cs:x3x_bt], 2
    je .short
    mov word [cs:di], 576
    mov word [cs:di + 2], 0
    mov word [cs:di + 4], 2
    mov word [cs:x3x_nruns], 1
    jmp .done
.short:
    mov si, x3x_sfbw                ; the record's 13 short-band widths
    xor dx, dx                      ; runs
    xor bx, bx                      ; lines before this band, a window
    mov cx, 0                       ; start (36 when mixed)
    mov ax, 0                       ; first band
    cmp byte [cs:x3x_mixed], 0
    je .pure
    mov word [cs:di], 36
    mov word [cs:di + 2], 0
    mov word [cs:di + 4], 2
    add di, 6
    inc dx
    mov cx, 36
    mov ax, 3
.pure:
    add si, ax
.band:
    cmp ax, 13
    jae .end
    push ax
    mov al, [cs:si]                 ; the width
    xor ah, ah
    push cx
    ; dest of window w = 2 (start + 3 acc + w)
    push ax
    mov ax, bx
    add ax, bx
    add ax, bx                      ; 3 acc
    add ax, cx                      ; + start
    shl ax, 1
    mov cx, ax
    pop ax
    mov [cs:di], ax
    mov [cs:di + 2], cx
    mov word [cs:di + 4], 6
    add cx, 2
    mov [cs:di + 6], ax
    mov [cs:di + 8], cx
    mov word [cs:di + 10], 6
    add cx, 2
    mov [cs:di + 12], ax
    mov [cs:di + 14], cx
    mov word [cs:di + 16], 6
    add di, 18
    add dx, 3
    add bx, ax                      ; acc += width
    pop cx
    pop ax
    inc ax
    inc si
    jmp short .band
.end:
    mov [cs:x3x_nruns], dx
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ============================================================ requantisation
; x3x_rqline - CX lines of a channel: ES:SI = ix[l] (q 1,152 bytes on), DS:DI
; = the destination, DX = its stride in bytes. Each |ix| <= 15 reads RQL
; (q < -80: 0, q > 7: 32767), a larger one x3x_rqbig; the sign of ix.
; Advances SI by 2 a line and DI by DX. Clobbers AX, BX, BP; CX = 0.
x3x_rqline:
.l: mov ax, [es:si]
    or ax, ax
    jnz .nz
    mov [di], ax
.nx:
    inc si
    inc si
    add di, dx
    loop .l
    ret
.nz:
    mov bp, ax
    jns .p
    neg ax
.p: cmp ax, 15
    ja .big
    mov bx, [es:si + X3X_R_Q - X3X_R_IX]
    add bx, -X3X_RQ_G0
    cmp bx, X3X_RQ_G1 - X3X_RQ_G0
    ja .qout
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; 32 bytes a row
    shl ax, 1
    add bx, ax
    mov ax, [bx + X3X_H1_RQL]
.sg:
    or bp, bp
    jns .st
    neg ax
.st:
    mov [di], ax
    jmp short .nx
.qout:
    or bx, bx                       ; q + 80 < 0: below the table, 0
    js .zq
    call x3x_rqsat                  ; q > 7: saturated
    jmp short .sg
.zq:
    xor ax, ax
    jmp short .st
.big:
    mov bx, [es:si + X3X_R_Q - X3X_R_IX]
    call x3x_rqbig
    jmp short .sg

; x3x_rqbig - AX = |ix| > 15, BX = q: the Q13 magnitude, saturated at 32767.
; x3sim's pow43 (M, E) with F = 2^((q & 3) / 4) Q14: P = M F, s = 15 - E -
; (q >> 2); s >= 31: 0; s >= 1: (P + 2^(s-1)) >> s; s <= 0: saturated.
; Preserves everything but AX.
x3x_rqbig:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push bx                         ; q
    cmp ax, 1024
    jae .interp
    mov si, ax
    mov cl, [cs:x3x_p43e + si]
    shl si, 1
    mov ax, [cs:x3x_p43m + si]
    jmp short .me
.interp:
    mov bx, ax
    and bx, 7                       ; f
    mov si, ax
    mov cl, 3
    shr si, cl                      ; j
    mov cl, [cs:x3x_p43e + si]      ; Ej
    mov ch, [cs:x3x_p43e + si + 1]  ; Ej+1
    mov di, si
    shl di, 1
    mov ax, [cs:x3x_p43m + di + 2]
    sub ch, cl
    xchg cl, ch
    shl ax, cl                      ; Mj+1 << (Ej+1 - Ej)
    xchg cl, ch
    mov bp, [cs:x3x_p43m + di]      ; Mj
    sub ax, bp
    mul bx                          ; (Mj+1 - Mj) f
    add ax, 4
    adc dx, 0
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    add ax, bp                      ; mi
    add cl, 4                       ; ei
    cmp ax, 32768
    jb .me
    add ax, 1
    rcr ax, 1
    inc cl
.me:
    pop bx                          ; q
    xor ch, ch
    mov si, bx
    and si, 3
    shl si, 1
    mul word [cs:x3x_p43f + si]     ; DX:AX = P
    sar bx, 1
    sar bx, 1
    mov si, 15
    sub si, cx
    sub si, bx                      ; s
    cmp si, 31
    jge .zero
    cmp si, 0
    jle .sat
    mov cx, si
    dec cx                          ; s - 1: the rounding bit
    cmp cx, 16
    jae .hi
    mov bx, 1
    shl bx, cl
    add ax, bx
    adc dx, 0
    jmp short .sh
.hi:
    sub cx, 16
    mov bx, 1
    shl bx, cl
    add dx, bx
.sh:
    mov cx, si
.shr:
    shr dx, 1
    rcr ax, 1
    loop .shr
    or dx, dx
    jnz .sat
    cmp ax, 32767
    ja .sat
    jmp short .done
.zero:
    xor ax, ax
    jmp short .done
.sat:
    call x3x_rqsat
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; x3x_rqsat - AX = 32767, a requantisation saturation counted
x3x_rqsat:
    X3X_INC32 x3x_s_sat
    X3X_STG 0
    mov ax, 32767
    ret

; x3x_nsb18 - DX = 18 * nsb (lines reconstructed). Clobbers AX.
x3x_nsb18:
    mov ax, [cs:x3x_nsb]
    mov dx, 18
    mul dx
    mov dx, ax
    ret

; x3x_zero - DS:DI, CX words zeroed. Clobbers AX, CX, DI, ES.
x3x_zero:
    push ds
    pop es
    xor ax, ax
    rep stosw
    ret

; x3x_rqmono - BX = the record offset of a channel block: requantise it into
; X in subband order. Long: lines below min(nz, 18 nsb), the rest up to 18
; nsb zeroed. Short: X zeroed, then the runs while source lines remain.
x3x_rqmono:
    mov ds, [cs:x3x_segh1]
    mov es, [cs:x3x_rseg]
    cmp byte [es:bx + X3X_R_OK], 0
    jne .ok
    mov word [cs:x3x_nzx], 0
    mov di, X3X_H1_X
    mov cx, 576
    jmp x3x_zero
.ok:
    mov ax, [es:bx + X3X_R_NZ]
    cmp ax, 576
    jbe .nz
    mov ax, 576
.nz:
    mov [cs:x3x_rem], ax
    lea si, [bx + X3X_R_IX]
    cmp byte [cs:x3x_bt], 2
    je .short
    call x3x_nsb18
    mov cx, [cs:x3x_rem]
    cmp cx, dx
    jbe .c
    mov cx, dx
.c: mov [cs:x3x_nzx], cx            ; X is zero from here up
    push dx
    push cx
    mov di, X3X_H1_X
    mov dx, 2
    jcxz .z
    call x3x_rqline
.z: pop cx
    pop dx
    sub dx, cx
    mov di, cx
    shl di, 1
    add di, X3X_H1_X
    mov cx, dx
    jmp x3x_zero
.short:
    mov word [cs:x3x_nzx], 576
    push si
    mov di, X3X_H1_X
    mov cx, 576
    call x3x_zero
    mov es, [cs:x3x_rseg]
    pop si
    mov bx, x3x_runs
    mov ax, [cs:x3x_nruns]
    mov [cs:x3x_cnt], ax
.run:
    cmp word [cs:x3x_cnt], 0
    je .done
    mov cx, [cs:bx]
    cmp cx, [cs:x3x_rem]
    jbe .n
    mov cx, [cs:x3x_rem]
.n: jcxz .done
    sub [cs:x3x_rem], cx
    mov di, [cs:bx + 2]
    add di, X3X_H1_X
    mov dx, [cs:bx + 4]
    push bx
    call x3x_rqline
    pop bx
    add bx, 6
    dec word [cs:x3x_cnt]
    jmp short .run
.done:
    ret

; x3x_rqplain - BX = a channel block, DI = XL or XR: its lines in bitstream
; order (the zeros past nz written)
x3x_rqplain:
    mov ds, [cs:x3x_segh1]
    mov es, [cs:x3x_rseg]
    mov cx, 576
    cmp byte [es:bx + X3X_R_OK], 0
    je x3x_zero
    mov ax, [es:bx + X3X_R_NZ]
    cmp ax, 576
    jbe .nz
    mov ax, 576
.nz:
    mov [cs:x3x_rem], ax
    lea si, [bx + X3X_R_IX]
    mov cx, ax
    mov dx, 2
    jcxz .z
    call x3x_rqline
.z: mov cx, 576
    sub cx, [cs:x3x_rem]
    jmp x3x_zero

; ============================================================== stereo
; x3x_mix - XL, XR (bitstream order) to X (subband order) by the plan: 'ms'
; is L, 'lr' (or no plan) (L + R + 1) >> 1, intensity x3x_ismono. Long:
; lines below min(max(nzL, nzR), 18 nsb), zeros to 18 nsb; short: every run.
x3x_mix:
    mov ds, [cs:x3x_segh1]
    mov es, [cs:x3x_rseg]
    mov bx, [cs:x3x_roff]
    mov al, [cs:x3x_plan]
    mov [cs:x3x_hasplan], al
    ; max(nz) over the two channels (a silent one's lines are zeros)
    xor ax, ax
    cmp byte [es:bx + X3X_R_CH0 + X3X_R_OK], 0
    je .c1
    mov ax, [es:bx + X3X_R_CH0 + X3X_R_NZ]
.c1:
    cmp byte [es:bx + X3X_R_CH0 + X3X_R_CHSZ + X3X_R_OK], 0
    je .c2
    mov cx, [es:bx + X3X_R_CH0 + X3X_R_CHSZ + X3X_R_NZ]
    cmp cx, ax
    jbe .c2
    mov ax, cx
.c2:
    cmp ax, 576
    jbe .c3
    mov ax, 576
.c3:
    add bx, X3X_R_PLAN              ; ES:BX = plan[0]
    xor si, si                      ; line * 2
    cmp byte [cs:x3x_bt], 2
    je .short
    push ax
    call x3x_nsb18
    pop cx
    cmp cx, dx
    jbe .l1
    mov cx, dx
.l1:
    mov [cs:x3x_nzx], cx
    push dx
    push cx
    mov di, X3X_H1_X
    mov dx, 2
    jcxz .l2
    call x3x_mixline
.l2:
    pop cx
    pop dx
    sub dx, cx
    mov di, cx
    shl di, 1
    add di, X3X_H1_X
    mov cx, dx
    jmp x3x_zero
.short:
    mov word [cs:x3x_nzx], 576
    mov [cs:x3x_rem], ax
    push bx
    push si
    mov di, X3X_H1_X
    mov cx, 576
    call x3x_zero
    mov es, [cs:x3x_rseg]
    pop si
    pop bx
    mov ax, x3x_runs
    mov [cs:x3x_ptr], ax
    mov ax, [cs:x3x_nruns]
    mov [cs:x3x_cnt], ax
.run:
    cmp word [cs:x3x_cnt], 0
    je .done
    mov di, [cs:x3x_ptr]
    mov cx, [cs:di]
    cmp cx, [cs:x3x_rem]
    jbe .n
    mov cx, [cs:x3x_rem]
.n: jcxz .done
    sub [cs:x3x_rem], cx
    mov dx, [cs:di + 4]
    mov di, [cs:di + 2]
    add di, X3X_H1_X
    call x3x_mixline
    add word [cs:x3x_ptr], 6
    dec word [cs:x3x_cnt]
    jmp short .run
.done:
    ret

; x3x_mixline - CX lines: DS:SI + XL/XR the inputs (SI = 2 line), ES:BX the
; plan byte, DS:DI the destination, DX its stride. Advances SI, BX, DI.
; Clobbers AX, BP; CX = 0.
x3x_mixline:
.l: mov al, [cs:x3x_hasplan]        ; X3G_PLAN: 0 lr, 1 every line M/S,
    cmp al, X3G_PLAN_PL             ; 2 the plan line by line
    jne .pk
    mov al, [es:bx]
.pk:
    or al, al
    jz .lr
    cmp al, X3G_PL_MS
    je .ms
    push bx
    mov bl, al
    mov ax, [si + X3X_H1_XL]
    call x3x_ismono
    pop bx
    jmp short .st
.ms:
    mov ax, [si + X3X_H1_XL]
    jmp short .st
.lr:
    mov ax, [si + X3X_H1_XL]
    mov bp, [si + X3X_H1_XR]
    xor bp, ax
    sar bp, 1
    or ax, [si + X3X_H1_XR]
    sub ax, bp                      ; (L | R) - ((L ^ R) >> 1) = (L + R + 1) >> 1
.st:
    mov [di], ax
    inc bx
    inc si
    inc si
    add di, dx
    loop .l
    ret

; x3x_isk - AL = an intensity plan code (80h | pos, 40h the MPEG-2 scale):
; CX = kl, BP = kr (Q14; x3sim.is_factors_q, sqrt 2 when M/S).
; Preserves everything else.
x3x_isk:
    push ax
    push bx
    mov bl, al
    and bx, 7Fh                     ; is_pos (the code is 80h + p)
    cmp byte [cs:x3x_ver], 1
    jne .m2
    shl bx, 1
    shl bx, 1
    cmp byte [cs:x3x_isms], 0
    je .n1
    add bx, 28
.n1:
    mov cx, [cs:x3x_is1 + bx]
    mov bp, [cs:x3x_is1 + bx + 2]
    jmp short .done
.m2:
    push bx
    and bx, 1Fh
    shl bx, 1                       ; the word in a row
    cmp byte [cs:x3x_issc], 0
    je .s0
    add bx, 64
.s0:
    cmp byte [cs:x3x_isms], 0
    je .q
    add bx, 128
.q: mov cx, [cs:x3x_is2 + bx]       ; k
    mov bp, 16384
    cmp byte [cs:x3x_isms], 0
    je .one
    mov bp, X3X_SQRT2_Q14
.one:
    pop bx
    test bl, 1
    jz .even
    ; odd is_pos: (k, one)
    jmp short .done
.even:
    xchg cx, bp                     ; (one, k)
.done:
    pop bx
    pop ax
    ret

; x3x_ismono - AX = L, BL = the plan code: the mono value sat((L kl + L kr +
; 2^14) >> 15). Preserves everything but AX.
x3x_ismono:
    push bx
    push cx
    push dx
    push bp
    push ax
    mov al, bl
    call x3x_isk
    pop ax
    push ax
    imul cx
    mov cx, ax
    mov bx, dx
    pop ax
    imul bp
    add ax, cx
    adc dx, bx
    add ax, 4000h
    adc dx, 0
    shl ax, 1
    rcl dx, 1                       ; OF: bits 31 and 30 disagree, out of range
    jno .ok
    pushf
    call x3x_stsat
    popf
    mov dx, 7FFFh
    jnc .ok                         ; CF: the sign
    inc dx
.ok:
    mov ax, dx
    pop bp
    pop dx
    pop cx
    pop bx
    ret

; x3x_shk14 - DX:AX: sat((DX:AX + 2^13) >> 14) in AX. Clobbers DX.
x3x_shk14:
    add ax, 2000h
    adc dx, 0
    push cx
    mov cx, dx                      ; the sign, bit 15
    shl ax, 1
    rcl dx, 1
    jo .sat
    shl ax, 1
    rcl dx, 1
    jo .sat
    mov ax, dx
    pop cx
    ret
.sat:
    call x3x_stsat
    mov ax, 7FFFh
    or cx, cx
    jns .s
    inc ax
.s: pop cx
    ret

; x3x_stsat - a stereo saturation counted (flags not kept)
x3x_stsat:
    X3X_INC32 x3x_s_sat
    X3X_STG 1
    ret

; x3x_stproc - the split path with a plan: XL and XR stereo-processed in
; place (bitstream order): 'ms' sat(L + R), sat(L - R); intensity sat((L k +
; 2^13) >> 14) each side
x3x_stproc:
    mov ds, [cs:x3x_segh1]
    mov es, [cs:x3x_rseg]
    mov bx, [cs:x3x_roff]
    add bx, X3X_R_PLAN
    xor si, si
    mov cx, 576
.l: mov al, [cs:x3x_plan]           ; 1: every line M/S; 2: the plan's
    cmp al, X3G_PLAN_PL
    jne .pk
    mov al, [es:bx]
.pk:
    or al, al
    jz .nx
    cmp al, X3G_PL_MS
    jne .is
    mov ax, [si + X3X_H1_XL]
    mov dx, [si + X3X_H1_XR]
    mov bp, ax
    add ax, dx
    jno .a
    call x3x_stsat
    mov ax, 7FFFh
    or bp, bp
    jns .a
    inc ax
.a: sub bp, dx
    jno .b
    call x3x_stsat
    mov bp, 7FFFh
    test word [si + X3X_H1_XL], 8000h
    jz .b
    inc bp
.b: mov [si + X3X_H1_XL], ax
    mov [si + X3X_H1_XR], bp
    jmp short .nx
.is:
    push cx
    push bx
    call x3x_isk                    ; CX = kl, BP = kr
    mov ax, [si + X3X_H1_XL]
    push ax
    imul cx
    call x3x_shk14
    mov [si + X3X_H1_XL], ax
    pop ax
    imul bp
    call x3x_shk14
    mov [si + X3X_H1_XR], ax
    pop bx
    pop cx
.nx:
    inc bx
    inc si
    inc si
    loop .l
    ret

; x3x_place - DS:SI = XL or XR (bitstream order) to X by the runs
x3x_place:
    mov ds, [cs:x3x_segh1]
    mov bx, x3x_runs
    mov ax, [cs:x3x_nruns]
    mov [cs:x3x_cnt], ax
.run:
    cmp word [cs:x3x_cnt], 0
    je .done
    mov cx, [cs:bx]
    mov di, [cs:bx + 2]
    add di, X3X_H1_X
    mov dx, [cs:bx + 4]
.c: lodsw
    mov [di], ax
    add di, dx
    loop .c
    add bx, 6
    dec word [cs:x3x_cnt]
    jmp short .run
.done:
    ret

x3x_rem     dw 0
x3x_hasplan db 0
x3x_nzx     dw 576                  ; X is zero from this line up
x3x_nzsplit dw 576

; x3x_splitnz - the split path's bound for the current block: max nz for a
; long block, everything for a short one
x3x_splitnz:
    push ax
    mov ax, [cs:x3x_nzsplit]
    cmp byte [cs:x3x_bt], 2
    jne .l
    mov ax, 576
.l: mov [cs:x3x_nzx], ax
    pop ax
    ret

; =============================================================== the hybrid
; x3x_hybrid - X (subband order, H1) of the current block: antialias, then a
; subband at a time the IMDCT and its window into Y36, then by x3x_mode:
; 0 the overlap into the slot records, 1 Y36 kept in YS (the split path's
; first channel), 2 averaged with YS and then the overlap
x3x_hybrid:
    mov ds, [cs:x3x_segh1]
    xor bx, bx
    ; the antialias bound: 31 long, 1 mixed short, none short; < nsb
    mov cx, 31
    cmp byte [cs:x3x_bt], 2
    jne .aa
    xor cx, cx
    cmp byte [cs:x3x_mixed], 0
    je .aa
    inc cx
.aa:
    mov ax, [cs:x3x_nsb]
    dec ax
    cmp cx, ax
    jbe .a2
    mov cx, ax
.a2:
    ; and no boundary whose 16 lines all lie at or above nzx (zero, so the
    ; butterfly changes nothing): b < floor((nzx + 25) / 18)
    mov ax, [cs:x3x_nzx]
    add ax, 25
    mov dl, 18
    div dl
    xor ah, ah
    dec ax
    cmp cx, ax
    jbe .a3
    mov cx, ax
.a3:
    jcxz .noaa
    ; the last butterfly reaches 8 lines into the subband above it
    mov ax, 18
    mul cx
    add ax, 8
    cmp ax, [cs:x3x_nzx]
    jbe .a4
    mov [cs:x3x_nzx], ax
.a4:
    xor bx, bx
    call [cs:x3x_aafn]
.noaa:
    mov word [cs:x3x_sb], 0
.sb:
    mov ax, [cs:x3x_sb]
    cmp ax, [cs:x3x_nsb]
    jb .go
    ret
.go:
    call x3x_produce                ; CF = 1: the subband is all zero
    jc .zero
    cmp byte [cs:x3x_mode], 1
    je .keep
    cmp byte [cs:x3x_mode], 2
    jne .cons
    call x3x_avg
.cons:
    call x3x_consume
    jmp short .nx
.zero:
    cmp byte [cs:x3x_mode], 0
    jne .zy
    call x3x_consume0
    jmp short .nx
.zy:
    ; the split path: a zero subband is a zero Y36
    mov es, [cs:x3x_segh2]
    mov di, X3X_H2_Y36
    mov cx, 36
    xor ax, ax
    rep stosw
    cmp byte [cs:x3x_mode], 1
    je .keep
    call x3x_avg
    call x3x_consume
    jmp short .nx
.keep:
    call x3x_keepys
.nx:
    inc word [cs:x3x_sb]
    jmp short .sb

; x3x_produce - the subband x3x_sb of X into Y36 (H2). CF = 1 when its 18
; lines are all zero (Y36 untouched). Leaves DS = H1.
x3x_produce:
    mov ds, [cs:x3x_segh1]
    mov es, [cs:x3x_segh2]
    mov si, [cs:x3x_sb]
    shl si, 1
    shl si, 1                       ; 4 sb
    mov ax, si
    shl si, 1
    shl si, 1
    shl si, 1                       ; 32 sb
    add si, ax                      ; 36 bytes a subband
    mov ax, si
    shr ax, 1                       ; 18 sb, the subband's first line
    cmp ax, [cs:x3x_nzx]
    jb .look
    jmp .allz
.look:
    add si, X3X_H1_X
    ; nonzero lines: neg sets CF for a nonzero word
    xor cx, cx
%assign k 0
%rep 18
    mov ax, [si + 2 * k]
    neg ax
    adc cx, 0
%assign k k + 1
%endrep
    or cx, cx
    jnz .some
    jmp .allz
.some:
    ; short?
    cmp byte [cs:x3x_bt], 2
    jne .long
    cmp byte [cs:x3x_mixed], 0
    je .short
    cmp word [cs:x3x_sb], 2
    jae .short
.long:
    ; the shape: a mixed block's subbands 0 and 1 take the normal window
    mov al, [cs:x3x_bt]
    cmp byte [cs:x3x_winsw], 0
    je .shp
    cmp byte [cs:x3x_mixed], 0
    je .shp
    cmp word [cs:x3x_sb], 2
    jae .shp
    xor al, al
.shp:
    push ax
    cmp cx, X3X_FAST_MIN
    jb .sparse
    add word [cs:x3x_s_fast], 1
    adc word [cs:x3x_s_fast + 2], 0
    call x3x_imfast
    jmp short .win
.sparse:
    add word [cs:x3x_s_sparse], 1
    adc word [cs:x3x_s_sparse + 2], 0
    call x3x_imsparse
.win:
    pop ax
    mov ds, [cs:x3x_segh2]
    xor bx, bx
    cmp al, 1
    je .w1
    cmp al, 3
    je .w3
    call x3x_wlong0
    jmp short .wd
.w1:
    call x3x_wlong1
    jmp short .wd
.w3:
    call x3x_wlong3
.wd:
    mov ds, [cs:x3x_segh1]
    clc
    ret
.allz:
    add word [cs:x3x_s_zero], 1
    adc word [cs:x3x_s_zero + 2], 0
    stc
    ret
.short:
    add word [cs:x3x_s_short], 1
    adc word [cs:x3x_s_short + 2], 0
    ; Y36 = 0, then each window's 6-point IMDCT added in
    mov di, X3X_H2_Y36
    mov cx, 36
    xor ax, ax
    rep stosw
    xor dx, dx                      ; the window
.sw:
    mov di, X3X_H2_Z
    mov cx, 6
    xor ax, ax
    rep stosw
    push si
    mov bx, dx
    shl bx, 1
    add si, bx                      ; x[3k + w]
    xor di, di                      ; 2k
.sk:
    mov ax, [si]
    or ax, ax
    jz .sn
    call x3x_encr
    or ax, ax
    jz .sn
    xchg bx, ax
    call [cs:x3x_im6tab + di]
.sn:
    add si, 6
    inc di
    inc di
    cmp di, 12
    jb .sk
    pop si
    ; window it into Y36 at 6 + 6w
    push si
    push dx
    mov ds, [cs:x3x_segh2]
    mov di, dx
    shl di, 1
    shl di, 1
    add di, dx
    add di, dx
    shl di, 1                       ; 12 w
    add di, X3X_H2_Y36
    xor bx, bx
    call x3x_wshort
    mov ds, [cs:x3x_segh1]
    pop dx
    pop si
    inc dx
    cmp dx, 3
    jb .sw
.sd:
    clc
    ret

; x3x_encr - AX = a Q13 line: AX = its R code as a word (2 code). DS = H1.
; Clobbers BX.
x3x_encr:
    mov bx, ax
    sar bx, 1
    inc bx
    sar bx, 1                       ; (v + 2) >> 2, 8192 at most: the sentinel
    mov al, [bx + X3X_H1_ENCR + 8192]
    cbw
    shl ax, 1
    ret

; x3x_imsparse - DS:SI the subband's 18 lines (H1): Z = the sum of each
; nonzero line's 18 IMDCT row entries (ES = H2)
x3x_imsparse:
    push si
    mov di, X3X_H2_Z
    mov cx, 18
    xor ax, ax
    rep stosw
    xor di, di
.k: lodsw
    or ax, ax
    jz .n
    call x3x_encr
    or ax, ax
    jz .n
    xchg bx, ax
    call [cs:x3x_im18tab + di]
.n: inc di
    inc di
    cmp di, 36
    jb .k
    pop si
    ret

; x3x_imfast - DS:SI the subband's 18 lines (H1): Z = their DCT-IV by
; minimp3's factorisation (x3simxt.dct4_18_fast), imul by Q15 constants
; rounded (a c + 2^14) >> 15, halves (x + 1) >> 1, adds wrapping
X3X_CO equ X3X_H1_FW
X3X_SI equ X3X_H1_FW + 18
X3X_TT equ X3X_H1_FW + 36
x3x_imfast:
    mov ax, [si]
    neg ax
    mov [X3X_CO], ax
    mov ax, [si + 34]
    mov [X3X_SI], ax
%assign i 0
%rep 4
    mov ax, [si + 2 * (4 * i + 1)]
    mov dx, [si + 2 * (4 * i + 2)]
    mov cx, ax
    sub cx, dx
    mov [X3X_SI + 2 * (8 - 2 * i)], cx
    add ax, dx
    mov [X3X_CO + 2 * (1 + 2 * i)], ax
    mov ax, [si + 2 * (4 * i + 4)]
    mov dx, [si + 2 * (4 * i + 3)]
    mov cx, ax
    sub cx, dx
    mov [X3X_SI + 2 * (7 - 2 * i)], cx
    add ax, dx
    neg ax
    mov [X3X_CO + 2 * (2 + 2 * i)], ax
%assign i i + 1
%endrep
    mov di, X3X_CO
    call x3x_d9
    mov di, X3X_SI
    call x3x_d9
    neg word [X3X_SI + 2]
    neg word [X3X_SI + 6]
    neg word [X3X_SI + 10]
    neg word [X3X_SI + 14]
    ; z[9+i] = (co (-tw1) + si (-tw0) + 2^14) >> 15
    ; z[8-i] = (co (-tw0) + si tw1 + 2^14) >> 15
%assign i 0
%rep 9
    mov ax, [X3X_CO + 2 * i]
    imul word [cs:x3x_tw + 6 * i]
    mov cx, ax
    mov bp, dx
    mov ax, [X3X_SI + 2 * i]
    imul word [cs:x3x_tw + 6 * i + 2]
    add ax, cx
    adc dx, bp
    add ax, 4000h
    adc dx, 0
    shl ax, 1
    rcl dx, 1
    mov [es:X3X_H2_Z + 2 * (9 + i)], dx
    mov ax, [X3X_CO + 2 * i]
    imul word [cs:x3x_tw + 6 * i + 2]
    mov cx, ax
    mov bp, dx
    mov ax, [X3X_SI + 2 * i]
    imul word [cs:x3x_tw + 6 * i + 4]
    add ax, cx
    adc dx, bp
    add ax, 4000h
    adc dx, 0
    shl ax, 1
    rcl dx, 1
    mov [es:X3X_H2_Z + 2 * (8 - i)], dx
%assign i i + 1
%endrep
    ret

; imq: AX times a Q15 constant at [cs:c], rounded, into DX
%macro X3X_IMQ 1
    imul word [cs:x3x_dct9 + %1]
    add ax, 4000h
    adc dx, 0
    shl ax, 1
    rcl dx, 1
%endmacro
; half: AX = (AX + 1) >> 1
%macro X3X_HALF 0
    sar ax, 1
    adc ax, 0
%endmacro
X3X_C1 equ 0
X3X_C2 equ 2
X3X_C3 equ 4
X3X_C4 equ 6
X3X_C5 equ 8
X3X_C6 equ 10
X3X_S3 equ 12
; temporaries
X3X_T0 equ X3X_TT
X3X_T2 equ X3X_TT + 2
X3X_T4 equ X3X_TT + 4
X3X_O4 equ X3X_TT + 6
X3X_S0 equ X3X_TT + 8
X3X_S2 equ X3X_TT + 10
X3X_S4 equ X3X_TT + 12
X3X_S6 equ X3X_TT + 14
X3X_S8 equ X3X_TT + 16
X3X_S1 equ X3X_TT + 18
X3X_S3V equ X3X_TT + 20
X3X_S5 equ X3X_TT + 22
X3X_S7 equ X3X_TT + 24

; x3x_d9 - minimp3's 9-point DCT-III in place on DS:DI (x3simxt.dct3_9)
x3x_d9:
    ; even part
    mov ax, [di + 12]               ; s6
    X3X_HALF
    add ax, [di]                    ; t0 = s0 + half(s6)
    mov [X3X_T0], ax
    mov ax, [di]
    sub ax, [di + 12]
    mov [X3X_S0], ax                ; s0 = s0 - s6
    mov ax, [di + 8]
    add ax, [di + 4]
    X3X_IMQ X3X_C2
    mov [X3X_T4], dx                ; t4 = imq(s4 + s2, c2)
    mov ax, [di + 16]
    add ax, [di + 4]
    X3X_IMQ X3X_C1
    mov [X3X_T2], dx                ; t2 = imq(s8 + s2, c1)
    mov ax, [di + 8]
    sub ax, [di + 16]
    X3X_IMQ X3X_C3
    mov [X3X_S6], dx                ; s6 = imq(s4 - s8, c3)
    mov ax, [di + 8]
    add ax, [di + 16]
    sub ax, [di + 4]
    mov [X3X_S4], ax                ; s4 = s4 + s8 - s2
    X3X_HALF
    mov dx, [X3X_S0]
    sub dx, ax
    mov [X3X_S2], dx                ; s2 = s0 - half(s4)
    mov ax, [X3X_S4]
    add ax, [X3X_S0]
    mov [X3X_O4], ax                ; o4 = s4 + s0
    mov ax, [X3X_T0]
    sub ax, [X3X_T2]
    add ax, [X3X_S6]
    mov [X3X_S8], ax                ; s8 = t0 - t2 + s6
    mov ax, [X3X_T0]
    sub ax, [X3X_T4]
    add ax, [X3X_T2]
    mov [X3X_S0], ax                ; s0 = t0 - t4 + t2
    mov ax, [X3X_T0]
    add ax, [X3X_T4]
    sub ax, [X3X_S6]
    mov [X3X_S4], ax                ; s4 = t0 + t4 - s6
    ; odd part
    mov ax, [di + 6]
    X3X_IMQ X3X_S3
    mov [X3X_S3V], dx               ; s3 = imq(s3, s3)
    mov ax, [di + 10]
    add ax, [di + 2]
    X3X_IMQ X3X_C4
    mov [X3X_T0], dx                ; t0 = imq(s5 + s1, c4)
    mov ax, [di + 10]
    sub ax, [di + 14]
    X3X_IMQ X3X_C5
    mov [X3X_T4], dx                ; t4 = imq(s5 - s7, c5)
    mov ax, [di + 2]
    add ax, [di + 14]
    X3X_IMQ X3X_C6
    mov [X3X_T2], dx                ; t2 = imq(s1 + s7, c6)
    mov ax, [di + 2]
    sub ax, [di + 10]
    sub ax, [di + 14]
    X3X_IMQ X3X_S3
    mov [X3X_S1], dx                ; s1 = imq(s1 - s5 - s7, s3)
    mov ax, [X3X_T0]
    sub ax, [X3X_S3V]
    sub ax, [X3X_T2]
    mov [X3X_S5], ax                ; s5 = t0 - s3 - t2
    mov ax, [X3X_T4]
    sub ax, [X3X_S3V]
    sub ax, [X3X_T0]
    mov [X3X_S7], ax                ; s7 = t4 - s3 - t0
    mov ax, [X3X_T4]
    add ax, [X3X_S3V]
    sub ax, [X3X_T2]
    mov [X3X_S3V], ax               ; s3 = t4 + s3 - t2
    ; out
    mov ax, [X3X_S4]
    sub ax, [X3X_S7]
    mov [di], ax                    ; s4 - s7
    mov ax, [X3X_S2]
    add ax, [X3X_S1]
    mov [di + 2], ax                ; s2 + s1
    mov ax, [X3X_S0]
    sub ax, [X3X_S3V]
    mov [di + 4], ax                ; s0 - s3
    mov ax, [X3X_S8]
    add ax, [X3X_S5]
    mov [di + 6], ax                ; s8 + s5
    mov ax, [X3X_O4]
    mov [di + 8], ax                ; o4
    mov ax, [X3X_S8]
    sub ax, [X3X_S5]
    mov [di + 10], ax               ; s8 - s5
    mov ax, [X3X_S0]
    add ax, [X3X_S3V]
    mov [di + 12], ax               ; s0 + s3
    mov ax, [X3X_S2]
    sub ax, [X3X_S1]
    mov [di + 14], ax               ; s2 - s1
    mov ax, [X3X_S4]
    add ax, [X3X_S7]
    mov [di + 16], ax               ; s4 + s7
    ret

; ------------------------------------------------------------- antialias
; x3x_aa_split - CX boundaries (>= 1) of X (DS = H1, BH = 0): lo' = lo cs +
; hi ca, hi' = hi cs - lo ca by AASPL's byte-split planes
%macro X3X_SPL 1                    ; AX * the constant at plane %1 -> DX
    mov bl, ah
    mov dl, [bx + %1 + 512]
    mov dh, [bx + %1 + 768]
    mov bl, al
    add dl, [bx + %1]
    adc dh, [bx + %1 + 256]
%endmacro
x3x_aa_split:
    mov si, X3X_H1_X + 36           ; X[18]
.b:
%assign i 0
%rep 8
    mov ax, [si - 2 - 2 * i]        ; lo
    X3X_SPL X3X_H1_AA + 1024 * i
    mov di, dx                      ; lo cs
    X3X_SPL X3X_H1_AA + 1024 * (8 + i)
    mov bp, dx                      ; lo ca
    mov ax, [si + 2 * i]            ; hi
    X3X_SPL X3X_H1_AA + 1024 * (8 + i)
    add di, dx                      ; + hi ca
    X3X_SPL X3X_H1_AA + 1024 * i
    sub dx, bp                      ; hi cs - lo ca
    mov [si + 2 * i], dx
    mov [si - 2 - 2 * i], di
%assign i i + 1
%endrep
    add si, 36
    dec cx
    jz .d
    jmp .b
.d: ret

; x3x_aa_tab - the same through AATAB's code rows (fast, turbo): both inputs
; coded by ENCR
x3x_aa_tab:
    mov si, X3X_H1_X + 36
.b:
%assign i 0
%rep 8
    mov ax, [si + 2 * i]
    call x3x_encr
    mov di, ax                      ; code(hi)
    mov ax, [si - 2 - 2 * i]
    call x3x_encr
    mov bx, ax                      ; code(lo)
    mov ax, [bx + X3X_H1_AA + 512 * i + 256]
    add ax, [di + X3X_H1_AA + 512 * (8 + i) + 256]
    mov dx, [di + X3X_H1_AA + 512 * i + 256]
    sub dx, [bx + X3X_H1_AA + 512 * (8 + i) + 256]
    mov [si - 2 - 2 * i], ax
    mov [si + 2 * i], dx
%assign i i + 1
%endrep
    add si, 36
    dec cx
    jz .d
    jmp .b
.d: ret

; ---------------------------------------------------- overlap and slots
; x3x_consume - the subband x3x_sb: out = Y36[i] + overlap[i] (wrapping),
; overlap = Y36[18 + i], the odd subbands' odd slots negated, out into slot
; i's sample sb (ES = SY)
x3x_sbaddr:                         ; BX = its overlap row, DI = its slot word
    mov ax, [cs:x3x_sb]
    mov bx, ax
    shl bx, 1
    shl bx, 1                       ; 4 sb
    mov di, bx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, di                      ; 36 sb
    add bx, X3X_H2_OVL
    mov di, ax
    shl di, 1
    add di, X3X_SY_SLOT + X3X_SLOT_S
    ret

x3x_consume:
    mov ds, [cs:x3x_segh2]
    mov es, [cs:x3x_segsy]
    call x3x_sbaddr
    test byte [cs:x3x_sb], 1
    jnz .odd
%assign i 0
%rep 18
    mov ax, [X3X_H2_Y36 + 2 * i]
    add ax, [bx + 2 * i]
    mov [es:di + i * X3X_SLOTSZ], ax
    mov ax, [X3X_H2_Y36 + 36 + 2 * i]
    mov [bx + 2 * i], ax
%assign i i + 1
%endrep
    ret
.odd:
%assign i 0
%rep 18
    mov ax, [X3X_H2_Y36 + 2 * i]
    add ax, [bx + 2 * i]
%if i & 1
    neg ax
%endif
    mov [es:di + i * X3X_SLOTSZ], ax
    mov ax, [X3X_H2_Y36 + 36 + 2 * i]
    mov [bx + 2 * i], ax
%assign i i + 1
%endrep
    ret

; x3x_consume0 - an all-zero subband: out = overlap, overlap = 0
x3x_consume0:
    mov ds, [cs:x3x_segh2]
    mov es, [cs:x3x_segsy]
    call x3x_sbaddr
    xor dx, dx
    test byte [cs:x3x_sb], 1
    jnz .odd
%assign i 0
%rep 18
    mov ax, [bx + 2 * i]
    mov [es:di + i * X3X_SLOTSZ], ax
    mov [bx + 2 * i], dx
%assign i i + 1
%endrep
    ret
.odd:
%assign i 0
%rep 18
    mov ax, [bx + 2 * i]
%if i & 1
    neg ax
%endif
    mov [es:di + i * X3X_SLOTSZ], ax
    mov [bx + 2 * i], dx
%assign i i + 1
%endrep
    ret

; x3x_keepys - Y36 into YS[sb]
x3x_keepys:
    mov ds, [cs:x3x_segh2]
    push ds
    pop es
    mov ax, 72
    mul word [cs:x3x_sb]
    add ax, X3X_H2_YS
    mov di, ax
    mov si, X3X_H2_Y36
    mov cx, 36
    rep movsw
    ret

; x3x_avg - Y36 = (Y36 + YS[sb] + 1) >> 1
x3x_avg:
    mov ds, [cs:x3x_segh2]
    mov ax, 72
    mul word [cs:x3x_sb]
    add ax, X3X_H2_YS
    mov si, ax
    mov di, X3X_H2_Y36
    mov cx, 36
.l: mov ax, [di]
    mov dx, [si]
    mov bp, ax
    xor bp, dx
    sar bp, 1
    or ax, dx
    sub ax, bp
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .l
    ret

; ============================================================ synthesis
; x3x_synth - every slot: Lee's DCT in place and the V codes, then the
; window pass into the slot's PCM
x3x_synth:
    mov ds, [cs:x3x_segsy]
    xor bx, bx
    mov di, X3X_SY_SLOT
    mov cx, 18
.l: push cx
    push di
    call [cs:x3x_leefn]
    pop di
    pop cx
    add di, X3X_SLOTSZ
    loop .l
.ld:                                ; (Lee and the V codes done)
    mov es, [cs:x3x_segsy]
    mov bp, X3X_SY_SLOT
    mov word [cs:x3x_t], 18
.w: mov ax, [cs:x3x_slot]
    mov dx, ax
    neg dx
    and dx, 15
    shl dx, 1                       ; 2 ((-s) & 15)
    and ax, 1
    mov cl, 6
    shl ax, cl                      ; 64 (s & 1)
    add ax, dx
    mov di, ax
    push bp
    call [cs:x3x_winfn]
    pop bp
    inc word [cs:x3x_slot]
    add bp, X3X_SLOTSZ
    dec word [cs:x3x_t]
    jnz .w
.wd:
    ret

; x3x_vslow - a V value past ENCV's +-4096: AX = v + 4096 (wrapped). AX =
; its code word: the nearest V level, ties toward zero, from level K0 on.
; Preserves everything else.
x3x_vslow:
    push bx
    push cx
    push si
    X3X_STG 6                       ; a V code past +-2 FS
    sub ax, 4096
    mov cx, ax                      ; the sign
    or ax, ax
    jns .p
    neg ax                          ; |v| (32768 for -32768)
.p: mov bx, 127
    cmp ax, 32768
    jae .got
    shl ax, 1                       ; 2 |v|
    mov bx, X3X_VSLOW_K0
    xor si, si
.l: cmp ax, [cs:x3x_vmid2 + si]
    jbe .got
    inc bx
    inc si
    inc si
    jmp short .l
.got:
    mov ax, bx
    or cx, cx
    jns .s
    neg ax
.s: shl ax, 1
    pop si
    pop cx
    pop bx
    ret

; ========================================================= generated code
%define X3XTK_BODY
%include "x3xtk.inc"

; ======================================================== the generator input
; XTGEN.BIN (tools/x3xttab.py's gen_input, written by x3xtgen.py --xtgen),
; carried in the part: OPEN expands from it, after checking its Fletcher-32
    align 16
x3x_gen:
    incbin "XTGEN.BIN"
x3x_imgend:
    ; (build/XTGEN.BIN is the input x3xtk.inc was made with: a different one
    ; fails here, as a negative count)
    times (x3x_imgend - x3x_gen) - X3X_GEN_BYTES db 0
