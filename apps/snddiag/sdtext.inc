; =============================================================================
; os8088 - apps/snddiag/sdtext.inc - the three pages, as text (SPEC.md 127.4)
;
; sd_build writes the current page into sd_txt, SD_ROWS rows of SD_COLS
; characters padded with spaces; sd_refresh draws the rows that differ from
; what is on the glass. Every row is a fixed width, so an opaque run repaints
; its whole cell and nothing is drawn twice (SPEC.md 6.1).
; =============================================================================

%macro PUTS 1                       ; a literal at DI
    jmp %%go
%%s: db %1, 0
%%go:
    push si
    mov si, %%s
    call sd_puts
    pop si
%endmacro

%macro ROW 1                        ; start row %1
    mov al, %1
    call sd_row
%endmacro

%macro COL 1                        ; DI = this row's column %1
    mov ax, %1
    call sd_col
%endmacro

; sd_row - AL = row: DI = its start, the row blanked and NUL-ended
sd_row:
    push ax
    push cx
    xor ah, ah
    mov cx, SD_LW
    mul cx
    add ax, sd_txt
    mov di, ax
    mov [sd_rowp], ax
    mov cx, SD_COLS
    mov al, ' '
    rep stosb
    xor al, al
    stosb
    mov di, [sd_rowp]
    pop cx
    pop ax
    ret

sd_col:
    add ax, [sd_rowp]
    mov di, ax
    ret

; sd_puts - SI = NUL string -> DI (no NUL); stops at the row's end
sd_puts:
    push ax
    push si
    push bx
    mov bx, [sd_rowp]
    add bx, SD_COLS
.c:
    lodsb
    or al, al
    jz .d
    cmp di, bx
    jae .d
    stosb
    jmp .c
.d:
    pop bx
    pop si
    pop ax
    ret

sd_hex2:                            ; AL -> two hex digits at DI
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .n
    mov al, ah
    and al, 0x0F
    call .n
    pop cx
    pop ax
    ret
.n:
    add al, '0'
    cmp al, '9'
    jbe .s
    add al, 'A' - '9' - 1
.s:
    stosb
    ret

sd_hex4:                            ; AX -> four hex digits
    xchg al, ah
    call sd_hex2
    xchg al, ah
    jmp sd_hex2

sd_hex3:                            ; AX -> three hex digits (a port)
    push ax
    mov al, ah
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .s
    add al, 'A' - '9' - 1
.s:
    stosb
    pop ax
    jmp sd_hex2

sd_dec:                             ; AX -> decimal at DI, no NUL left
    call dec_u16
    mov byte [di], ' '
    ret

sd_dec32:                           ; DX:AX -> decimal
    call dec_u32
    mov byte [di], ' '
    ret

sd_fld:                             ; AX in CX columns, right-aligned
    push cx
    call dec_fld
    pop cx
    ret

sd_yn:                              ; AL = 0 -> 'no', else 'yes'
    or al, al
    jz .n
    PUTS 'yes'
    ret
.n:
    PUTS 'no'
    ret

; sd_build - the current page into sd_txt
sd_build:
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
    xor al, al                      ; every row blank first
.blank:
    call sd_row
    inc ax
    cmp al, SD_ROWS
    jb .blank
    call sd_header
    cmp byte [sd_page], 1
    je .p2
    ja .p3
    call sd_page1
    jmp .done
.p2:
    call sd_page2
    jmp .done
.p3:
    call sd_page3
.done:
    ROW SD_ROWS - 1
    PUTS 'Keys: 1 2 3 pages  P play test  R rate  V D8h on/off  Esc quit'
    COL 64
    PUTS 'Photograph me'
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

sd_header:
    ROW 0
    PUTS ' os8088 Sound Debug  build '
    mov ax, BUILD_NUM
    call sd_dec
    COL 37
    mov bl, 0
    call .tab
    PUTS '1 Detect'
    call .end
    inc bx
    call .tab
    PUTS '2 Play test'
    call .end
    inc bx
    call .tab
    PUTS '3 Registers'
    call .end
    ret
.tab:
    inc di
    mov al, ' '
    cmp bl, [sd_page]
    jne .t
    mov al, '['
.t:
    stosb
    ret
.end:
    mov al, ' '
    cmp bl, [sd_page]
    jne .e
    mov al, ']'
.e:
    stosb
    ret

; --- page 1: DETECT ---------------------------------------------------------------
sd_page1:
    ROW 1
    PUTS 'Machine: CPU '
    mov al, [sd_cpu]
    mov si, sd_s_8086
    cmp al, CPU_8086
    je .cpu
    mov si, sd_s_286
    cmp al, CPU_286
    je .cpu
    mov si, sd_s_386
.cpu:
    call sd_puts
    test byte [sd_cpu+1], CPU_F_PM
    jz .rm
    PUTS ', kern_mega protected mode'
.rm:
    PUTS '   video '
    mov bl, [sd_vkind]
    cmp bl, 4
    jbe .vk
    mov bl, 4
.vk:
    xor bh, bh
    shl bx, 1
    mov si, [sd_s_vid+bx]
    call sd_puts

    ROW 2
    PUTS 'Driver:  '
    cmp byte [sd_drv], 0
    jne .drv
    PUTS 'SOUND.DRV is NOT loaded: nothing answered, or SYSTEM.CFG did not ask'
    jmp .caps
.drv:
    cmp byte [sd_diag], 0
    jne .dg
    PUTS 'SOUND.DRV loaded, NOT the debug build: no probe detail here'
    jmp .caps
.dg:
    PUTS 'SOUND.DRV loaded, debug build (record v'
    mov al, [sd_rec+SDG_O_VER]
    xor ah, ah
    call sd_dec
    PUTS ')'
.caps:
    ROW 3
    PUTS 'Caps:    '
    mov ax, [sd_caps]
    call sd_hex4
    PUTS 'h'
    mov bx, 1
    xor si, si
.cap:
    test [sd_caps], bx
    jz .capn
    inc di
    push si
    shl si, 1
    mov si, [sd_s_caps+si]
    call sd_puts
    pop si
.capn:
    inc si
    shl bx, 1
    cmp si, 12
    jb .cap

    cmp byte [sd_diag], 0
    jne .rec
    ROW 5
    PUTS 'Boot the Sound Debug disk itself for the probes: its SOUND.DRV keeps a record.'
    jmp .verdict

.rec:
    ROW 4
    PUTS 'DSP:     '
    mov ax, [sd_rec+SDG_O_BASE]
    or ax, ax
    jnz .dsp
    PUTS 'no DSP answered the reset at any base'
    jmp .scan
.dsp:
    PUTS 'base '
    call sd_hex3
    PUTS 'h  version '
    mov al, [sd_rec+SDG_O_VERHI]
    xor ah, ah
    call sd_dec
    PUTS '.'
    mov al, [sd_rec+SDG_O_VERLO]
    cmp al, 10                      ; minor as two decimal digits, the way
    jae .v2                         ; a DSP's version is written: 3.01, 4.13
    PUTS '0'
.v2:
    xor ah, ah
    call sd_dec
    PUTS '  IRQ '
    mov al, [sd_rec+SDG_O_IRQ]
    cmp al, 0xFF
    jne .irq
    PUTS 'none'
    jmp .dma
.irq:
    xor ah, ah
    call sd_dec
.dma:
    PUTS ' DMA 1 HDMA '
    mov al, [sd_rec+SDG_O_HDMA]
    or al, al
    jnz .hd
    PUTS 'none'
    jmp .auto
.hd:
    xor ah, ah
    call sd_dec
.auto:
    PUTS ' auto '
    mov al, [sd_rec+SDG_O_AUTO]
    call sd_yn
    PUTS ' tier '
    cmp byte [sd_rec+SDG_O_SBUP], 0
    jne .up
    PUTS 'DOWN'
    jmp .scan
.up:
    PUTS 'up'

.scan:
    ROW 5
    PUTS 'Scan:   '
    mov cl, [sd_rec+SDG_O_SCANN]
    xor ch, ch
    jcxz .scanned
    mov si, sd_rec+SDG_O_SCAN
.sc:
    inc di
    mov ax, [si]
    call sd_hex3
    PUTS ':'
    mov al, [si+2]
    call sd_hex2
    cmp byte [si+3], 0
    jne .scbad
    PUTS '=ok'
    jmp .scn
.scbad:
    PUTS '-'
.scn:
    add si, 4
    loop .sc
.scanned:

    ROW 6
    PUTS 'IRQ:     '
    mov al, [sd_rec+SDG_O_IMETH]
    or al, al
    jnz .im
    PUTS 'not looked for (no DSP)'
    jmp .irq2
.im:
    cmp al, 1
    jne .im2
    PUTS 'the line our PicoMEM set-up chose'
    jmp .imw
.im2:
    cmp al, 2
    jne .im3
    PUTS 'read from mixer 80h = '
    mov al, [sd_rec+SDG_O_M80]
    call sd_hex2
    PUTS 'h'
    jmp .imw
.im3:
    PUTS 'F2h probe, '
    mov al, [sd_rec+SDG_O_FTRY]
    xor ah, ah
    call sd_dec
    PUTS ' tries'
    cmp byte [sd_rec+SDG_O_M80], 0
    je .imw
    PUTS ' (mixer 80h = '
    mov al, [sd_rec+SDG_O_M80]
    call sd_hex2
    PUTS 'h, not 1/2/4)'
.imw:
    PUTS ', '
    mov al, [sd_rec+SDG_O_IWIN]
    or al, al
    jnz .iw
    PUTS 'NO line fired'
    jmp .imask
.iw:
    PUTS 'IRQ '
    xor ah, ah
    call sd_dec
    PUTS ' fired'
.imask:
    PUTS '; PIC mask was '
    mov al, [sd_rec+SDG_O_IMASK]
    call sd_hex2
.irq2:
    ROW 7
    PUTS 'PIC now: mask '
    mov al, [sd_rec+SDG_O_IMR]
    call sd_hex2
    PUTS ' IRR '
    mov al, [sd_rec+SDG_O_IRR]
    call sd_hex2
    PUTS ' ISR '
    mov al, [sd_rec+SDG_O_ISRR]
    call sd_hex2
    PUTS ' slave mask '
    mov al, [sd_rec+SDG_O_IMR2]
    call sd_hex2
    cmp byte [sd_rec+SDG_O_IWIN], 5
    jne .mix
    cmp byte [sd_cpu], CPU_8086     ; the IRQ 5 test reads it on an XT only
    jne .mix
    PUTS '  int 0Dh seg '
    mov ax, [sd_rec+SDG_O_I5SEG]
    call sd_hex4

.mix:
    ROW 8
    PUTS 'Mixer:   '
    mov bl, [sd_rec+SDG_O_MIX]
    cmp bl, 3
    jbe .mk
    mov bl, 0
.mk:
    xor bh, bh
    shl bx, 1
    mov si, [sd_s_mix+bx]
    call sd_puts

    ROW 9
    PUTS 'ESS:     '
    cmp byte [sd_rec+SDG_O_MIX], 0
    je .e7
    PUTS 'mixer 40h x4 = '
    mov si, sd_m40
    mov cx, 4
.m40:
    lodsb
    call sd_hex2
    inc di
    loop .m40
    call sd_isess
    jc .notess
    PUTS '= ES18'
    mov al, [sd_m40+1]
    call sd_hex2
    PUTS ' cfg '
    mov ah, [sd_m40+2]
    mov al, [sd_m40+3]
    call sd_hex4
    PUTS 'h '
    jmp .e7
.notess:
    PUTS '(no ESS id) '
.e7:
    cmp byte [sd_rec+SDG_O_VERHI], 3
    jne .fm
    PUTS ' DSP E7h: '
    mov cl, [sd_e7n]
    or cl, cl
    jnz .e7a
    PUTS 'no answer (not ESS)'
    jmp .fm
.e7a:
    mov al, [sd_e7]
    call sd_hex2
    cmp cl, 2
    jb .fm
    inc di
    mov al, [sd_e7+1]
    call sd_hex2
    cmp byte [sd_e7], 0x68
    jne .fm
    PUTS ' = ESS'

.fm:
    ROW 10
    PUTS 'FM: '
    mov al, [sd_rec+SDG_O_OPL]
    call sd_opl
    PUTS '  MPU-401: '
    mov ax, [sd_rec+SDG_O_MPU]
    call sd_port
    PUTS '  Covox: '
    mov ax, [sd_rec+SDG_O_CVX]
    call sd_port
    PUTS '  PicoMEM: '
    mov ax, [sd_rec+SDG_O_PMB]
    call sd_port

    ROW 11
    cmp word [sd_rec+SDG_O_BASE], 0
    je .verdict
    PUTS 'BLASTER=A'
    mov ax, [sd_rec+SDG_O_BASE]
    call sd_hex3
    cmp byte [sd_rec+SDG_O_IRQ], 0xFF
    je .bl1
    PUTS ' I'
    mov al, [sd_rec+SDG_O_IRQ]
    xor ah, ah
    call sd_dec
.bl1:
    PUTS ' D1'
    mov al, [sd_rec+SDG_O_HDMA]
    or al, al
    jz .bl2
    PUTS ' H'
    xor ah, ah
    call sd_dec
.bl2:
    mov ax, [sd_rec+SDG_O_MPU]
    or ax, ax
    jz .bl3
    PUTS ' P'
    call sd_hex3
.bl3:
    PUTS ' T'
    mov al, [sd_rec+SDG_O_VERHI]
    mov bl, '1'
    cmp al, 2
    jb .blt
    mov bl, '3'
    cmp al, 3
    jb .blt
    mov bl, '4'
    cmp al, 4
    jb .blt
    mov bl, '6'
.blt:
    mov al, bl
    stosb

.verdict:
    ROW 13
    PUTS 'Verdict: '
    call sd_verdict1
    ret

; sd_verdict1 - one line on what the detection says
sd_verdict1:
    cmp byte [sd_drv], 0
    jne .d
    PUTS 'no sound driver. Check SYSTEM.CFG asks for SOUND.DRV.'
    ret
.d:
    cmp byte [sd_diag], 0
    jne .g
    test word [sd_caps], SND_CAP_PCM_BG
    jz .nopcm
    PUTS 'a Sound Blaster tier is up (no detail: not the debug build).'
    ret
.g:
    cmp word [sd_rec+SDG_O_BASE], 0
    jne .b
    test word [sd_caps], SND_CAP_FM
    jz .none
    PUTS 'FM (AdLib) only: no Sound Blaster DSP answered at 210h-260h.'
    ret
.none:
    PUTS 'no card answered: no DSP, no OPL. Check the card is in SB mode.'
    ret
.b:
    cmp byte [sd_rec+SDG_O_IRQ], 0xFF
    jne .i
    cmp byte [sd_rec+SDG_O_IWIN], 5
    jne .noirq
    PUTS 'IRQ 5 refused: an XT disk controller owns it. Move the card off 5.'
    ret
.noirq:
    PUTS 'DSP found, but no IRQ (7,5,3,2) fired for F2h. Check IRQ setting.'
    ret
.i:
    cmp byte [sd_rec+SDG_O_SBUP], 0
    jne .u
    PUTS 'DSP and IRQ found, but the tier did not attach (reset failed).'
    ret
.u:
    PUTS 'Sound Blaster tier attached. Page 2 is the play test.'
    ret
.nopcm:
    PUTS 'no Sound Blaster tier (FM or speaker only).'
    ret

sd_opl:                             ; AL = 0 / SND_OPL2 / SND_OPL3
    cmp al, SND_OPL3
    je .3
    cmp al, SND_OPL2
    je .2
    PUTS 'none'
    ret
.2:
    PUTS 'OPL2'
    ret
.3:
    PUTS 'OPL3'
    ret

sd_port:                            ; AX = a port, 0 = none
    or ax, ax
    jnz .p
    PUTS 'none'
    ret
.p:
    call sd_hex3
    PUTS 'h'
    ret

; --- page 2: PLAY TEST ---------------------------------------------------------------
sd_page2:
    ROW 1
    PUTS 'Tone '
    mov bl, [sd_ratei]
    xor bh, bh
    shl bx, 1
    mov ax, [sd_rates+bx]
    call sd_dec
    PUTS ' Hz 8-bit mono, ring 16384, 10 s.  '
    mov al, [sd_pstate]
    cmp al, SD_PS_RUN
    jne .s1
    PUTS 'PLAYING, second '
    mov al, [sd_prow]
    xor ah, ah
    inc ax
    call sd_dec
    jmp .rg
.s1:
    cmp al, SD_PS_DONE
    jne .s2
    PUTS 'Done.'
    jmp .rg
.s2:
    cmp al, SD_PS_FAIL
    jne .s3
    PUTS 'REFUSED at '
    mov al, [sd_pfail]
    stosb
    PUTS ', code '
    mov ax, [sd_perr]
    call sd_dec
    jmp .rg
.s3:
    cmp al, SD_PS_NOTIMER
    jne .s4
    PUTS 'No timer on this kernel.'
    jmp .rg
.s4:
    PUTS 'Starting...'

.rg:
    cmp byte [sd_diag], 0
    je .cols
    cmp word [sd_rec+SDG_O_OPENS], 0
    je .cols
    ROW 2
    PUTS 'Regime: '
    cmp byte [sd_rec+SDG_O_WIDE], 0
    je .r1
    PUTS 'DSP4 41h rate'
    jmp .r3
.r1:
    cmp byte [sd_rec+SDG_O_HISP], 0
    je .r2
    PUTS 'high-speed 90h, TC '
    jmp .rtc
.r2:
    PUTS 'normal 1Ch, TC '
.rtc:
    mov al, [sd_rec+SDG_O_TC]
    call sd_hex2
.r3:
    PUTS '  block '
    mov ax, [sd_rec+SDG_O_HALF]
    call sd_dec
    cmp byte [sd_rec+SDG_O_DIRECT], 0
    je .r4
    PUTS ' direct (card plays the ring)'
    jmp .r5
.r4:
    PUTS ' double-buffered'
.r5:
    PUTS ' ack '
    mov ax, [sd_rec+SDG_O_ACKP]
    call sd_hex3

.cols:
    ROW 3
    PUTS 'sec  bytes/s  IRQs  D8h  mix22 mix04  60h 62h 64h  stream'
    xor bx, bx                      ; BX = the row
.tr:
    cmp bl, [sd_prow]
    jae .total
    mov al, bl
    add al, 4
    call sd_row
    mov ax, SD_ROWSZ
    mul bx
    mov si, ax
    add si, sd_rows
    mov ax, bx
    inc ax
    mov cx, 3
    call sd_fld
    mov ax, [si+SR_BYTES]
    mov cx, 9
    call sd_fld
    COL 12
    cmp byte [sd_diag], 0
    je .noi
    mov ax, [si+SR_IRQ]
    mov cx, 6
    call sd_fld
    jmp .d8
.noi:
    PUTS '    --'
.d8:
    COL 20
    mov al, [si+SR_D8F]
    or al, al
    jnz .d8a
    PUTS '--'
    jmp .mx
.d8a:
    cmp al, 1
    je .d8b
    PUTS 'na'                       ; asked, and the DSP did not answer
    jmp .mx
.d8b:
    mov al, [si+SR_D8]
    call sd_hex2
.mx:
    push bx
    xor bx, bx
.mxl:
    mov al, [sd_mxcol+bx]
    xor ah, ah
.mxc:
    call sd_col
    cmp byte [si+SR_MIXF], 0
    je .mxn
    cmp bl, 2
    jb .mxv
    call sd_isess
    jc .mxn
.mxv:
    mov al, [si+SR_MIX+bx]
    call sd_hex2
    jmp .mxe
.mxn:
    PUTS '--'
.mxe:
    inc bx
    cmp bl, SD_NMX
    jb .mxl
    pop bx
    COL 51
    mov al, [si+SR_STATE]
    call sd_stname
    inc bx
    jmp .tr

.total:
    ROW 14
    cmp byte [sd_prow], 0
    je .verd
    PUTS 'Total '
    mov ax, [sd_psum]
    mov dx, [sd_psum+2]
    call sd_dec32
    PUTS ' bytes'
    cmp byte [sd_diag], 0
    je .verd
    PUTS '  IRQs '
    mov ax, [sd_rec+SDG_O_ISR]
    sub ax, [sd_pisr0]
    call sd_dec
    PUTS ' under '
    mov ax, [sd_rec+SDG_O_UNDER]
    call sd_dec
    PUTS ' watchdog '
    mov ax, [sd_rec+SDG_O_WDOG]
    call sd_dec
    PUTS ' spurious '
    mov ax, [sd_rec+SDG_O_SPUR]
    call sd_dec
    PUTS ' DSP t/o '
    mov ax, [sd_rec+SDG_O_DTMO]
    call sd_dec
.verd:
    ROW 15
    cmp byte [sd_pstate], SD_PS_DONE
    jne .ret
    PUTS 'Verdict: '
    call sd_verdict2
.ret:
    ret

sd_stname:                          ; AL = SND_ST_*
    cmp al, SND_ST_PLAYING
    jne .1
    PUTS 'playing'
    ret
.1:
    cmp al, SND_ST_UNDER
    jne .2
    PUTS 'UNDERRUN'
    ret
.2:
    cmp al, SND_ST_ENDED
    jne .3
    PUTS 'ENDED (no IRQ)'
    ret
.3:
    PUTS 'gone'
    ret

; sd_verdict2 - the first thing that stopped, in the order it can stop:
; the DMA, the block IRQ, the DSP's voice, a mixer register
sd_verdict2:
    push bx
    push cx
    push si
    mov si, sd_rows + SD_ROWSZ      ; from second 2 (the first is the start)
    mov cx, 2
.byt:
    cmp word [si+SR_BYTES], 0
    je .dmastop
    add si, SD_ROWSZ
    inc cx
    cmp cx, SD_SECS
    jbe .byt
    cmp byte [sd_diag], 0
    je .ok
    mov si, sd_rows + SD_ROWSZ
    mov cx, 2
.irq:
    cmp word [si+SR_IRQ], 0
    je .irqstop
    add si, SD_ROWSZ
    inc cx
    cmp cx, SD_SECS
    jbe .irq
    mov si, sd_rows
    mov cx, 2
.v:
    cmp byte [si+SR_D8F], 1         ; the voice: on, then off
    jne .vn
    cmp byte [si+SR_D8], 0
    je .vn
    cmp byte [si+SD_ROWSZ+SR_D8F], 1
    jne .vn
    cmp byte [si+SD_ROWSZ+SR_D8], 0
    je .voff
.vn:
    add si, SD_ROWSZ
    inc cx
    cmp cx, SD_SECS
    jbe .v
    mov si, sd_rows
    mov cx, 2
.m:
    xor bx, bx
.mb:
    mov al, [si+SR_MIX+bx]
    cmp al, [si+SD_ROWSZ+SR_MIX+bx]
    jne .mchg
    inc bx
    cmp bx, SD_NMX
    jb .mb
    add si, SD_ROWSZ
    inc cx
    cmp cx, SD_SECS
    jbe .m
.ok:
    mov si, sd_rows                 ; was the voice ever READ? (D8h answers
    mov cx, SD_SECS                 ; nothing on some cards, and V turns it
.okv:                               ; off) - say only what was measured
    cmp byte [si+SR_D8F], 1
    je .okvoice
    add si, SD_ROWSZ
    loop .okv
    PUTS 'DMA, IRQs, mixer steady 10 s (voice unread). Tone stopped? Say when.'
    jmp .out
.okvoice:
    PUTS 'DMA, IRQs, voice and mixer steady 10 s. Tone stopped? Say when.'
    jmp .out
.dmastop:
    PUTS 'the DMA STOPPED in second '
    mov ax, cx
    call sd_dec
    cmp word [sd_rec+SDG_O_WDOG], 0
    je .out
    PUTS ' - block IRQs ceased, the watchdog ended it'
    jmp .out
.irqstop:
    PUTS 'block IRQs STOPPED in second '
    mov ax, cx
    call sd_dec
    PUTS ' while the DMA ran on'
    jmp .out
.voff:
    PUTS 'the DSP voice went OFF (D8h=00) in second '
    mov ax, cx
    call sd_dec
    PUTS ' as DMA ran'
    jmp .out
.mchg:
    PUTS 'mixer register '
    mov al, [sd_mxregs+bx]
    call sd_hex2
    PUTS 'h changed in second '
    mov ax, cx
    call sd_dec
.out:
    pop si
    pop cx
    pop bx
    ret

; --- page 3: REGISTERS ---------------------------------------------------------------
sd_page3:
    cmp byte [sd_diag], 0
    jne .g
    ROW 2
    PUTS 'Nothing to show: this SOUND.DRV is not the debug build.'
    ret
.g:
    ROW 1
    PUTS 'Mixer registers 01h-7Fh, hex:'
    cmp byte [sd_mixok], 0
    jne .mx
    PUTS ' (no mixer)'
    jmp .log
.mx:
    xor bx, bx
.mr:
    mov al, bl
    mov cl, 4
    shr al, cl
    add al, 2
    call sd_row
    mov al, bl
    call sd_hex2
    PUTS ':'
.mc:
    inc di
    mov al, [sd_mixd+bx]
    or bx, bx
    jnz .mv
    PUTS '--'                       ; 00h is the reset register: never read
    jmp .mn
.mv:
    call sd_hex2
.mn:
    inc bx
    test bl, 0x0F
    jnz .mc
    cmp bx, 128
    jb .mr
.log:
    ROW 10
    PUTS 'DSP writes, oldest first (! = timed out):'
    mov bl, [sd_rec+SDG_O_DLI]      ; the oldest is the next slot
    xor bh, bh
    xor cx, cx
.lg:
    cmp cl, 0
    jne .l2
    ROW 11
.l2:
    cmp cl, 16
    jne .le
    ROW 12
.le:
    push bx
    and bx, SDG_LOGN - 1
    shl bx, 1
    mov al, [sd_rec+SDG_O_DLOG+bx]
    call sd_hex2
    mov al, ' '
    cmp byte [sd_rec+SDG_O_DLOG+bx+1], 0
    je .lok
    mov al, '!'
.lok:
    stosb
    inc di
    pop bx
    inc bx
    inc cx
    cmp cx, SDG_LOGN
    jb .lg

    ROW 13
    PUTS 'act '
    mov si, sd_rec+SDG_O_ACT
    mov cx, 12
    mov bx, sd_s_live
.sl:
    lodsb
    call sd_hex2
    push si
    mov si, [bx]
    call sd_puts
    pop si
    add bx, 2
    loop .sl
    ROW 14
    PUTS 'half '
    mov ax, [sd_rec+SDG_O_HALF]
    call sd_hex4
    PUTS ' rate '
    mov ax, [sd_rec+SDG_O_RATE]
    call sd_dec
    PUTS ' total '
    mov ax, [sd_rec+SDG_O_TOTAL]
    call sd_hex4
    PUTS ' fed '
    mov ax, [sd_rec+SDG_O_FED]
    call sd_hex4
    PUTS ' cons '
    mov ax, [sd_rec+SDG_O_CONS]
    call sd_hex4
    PUTS ' wd '
    mov ax, [sd_rec+SDG_O_WD]
    call sd_dec
    PUTS '/'
    mov ax, [sd_rec+SDG_O_WDRST]
    call sd_dec
    PUTS ' DMA '
    mov al, [sd_rec+SDG_O_XPAGE]
    call sd_hex2
    PUTS ':'
    mov ax, [sd_rec+SDG_O_XOFF]
    call sd_hex4
    PUTS '+'
    mov ax, [sd_rec+SDG_O_XCNT]
    call sd_hex4
    ROW 15
    PUTS 'ISR entries '
    mov ax, [sd_rec+SDG_O_ISR]
    call sd_dec
    PUTS ' last ack '
    mov al, [sd_rec+SDG_O_ACK]
    call sd_hex2
    PUTS '  spurious '
    mov ax, [sd_rec+SDG_O_SPUR]
    call sd_dec
    PUTS ' opens '
    mov ax, [sd_rec+SDG_O_OPENS]
    call sd_dec
    PUTS ' PIC mask '
    mov al, [sd_rec+SDG_O_IMR]
    call sd_hex2
    PUTS ' irr '
    mov al, [sd_rec+SDG_O_IRR]
    call sd_hex2
    ret

; sd_mixdump - the mixer's 01h..7Fh into sd_mixd (00h, the reset, never)
sd_mixdump:
    push ax
    push bx
    mov byte [sd_mixok], 0
    cmp byte [sd_diag], 0
    je .r
    cmp byte [sd_rec+SDG_O_MIX], 0
    je .r
    mov bx, 1
.n:
    mov ah, bl
    call sd_mixrd
    jc .r
    mov [sd_mixd+bx], al
    inc bx
    cmp bx, 128
    jb .n
    mov byte [sd_mixok], 1
.r:
    pop bx
    pop ax
    ret

; --- strings -----------------------------------------------------------------------
sd_mxcol:   db 26, 32, 38, 42, 46   ; page 2's mixer columns
sd_s_8086:  db '8088/8086', 0
sd_s_286:   db '286', 0
sd_s_386:   db '386 or better', 0
sd_s_vid:   dw sd_s_v0, sd_s_v1, sd_s_v2, sd_s_v3, sd_s_v4
sd_s_v0:    db 'VGA', 0
sd_s_v1:    db 'Hercules', 0
sd_s_v2:    db 'CGA', 0
sd_s_v3:    db 'EGA', 0
sd_s_v4:    db 'VESA', 0
sd_s_mix:   dw sd_s_m0, sd_s_m1, sd_s_m2, sd_s_m3
sd_s_m0:    db 'none (SB 1.x/2.0 have none)', 0
sd_s_m1:    db 'CT1335 (SB 2.0 CD)', 0
sd_s_m2:    db 'CT1345 (SB Pro, ESS compatible)', 0
sd_s_m3:    db 'CT1745 (SB16)', 0
sd_s_caps:  dw sd_c0, sd_c1, sd_c2, sd_c3, sd_c4, sd_c5, sd_c6, sd_c7
            dw sd_c8, sd_c9, sd_c10, sd_c11
sd_c0:      db 'tone', 0
sd_c1:      db 'FM', 0
sd_c2:      db 'PCM', 0
sd_c3:      db 'clip', 0
sd_c4:      db 'record', 0
sd_c5:      db 'hi-rate', 0
sd_c6:      db 'ADPCM?', 0
sd_c7:      db 'blocks', 0
sd_c8:      db 'Covox', 0
sd_c9:      db 'mixer', 0
sd_c10:     db 'stereo', 0
sd_c11:     db '16-bit', 0
sd_s_live:  dw sd_l1, sd_l2, sd_l3, sd_l4, sd_l5, sd_l6, sd_l7, sd_l8
            dw sd_l9, sd_l10, sd_l11, sd_l12
sd_l1:      db ' st ', 0
sd_l2:      db ' dir ', 0
sd_l3:      db ' dr ', 0
sd_l4:      db ' ex ', 0
sd_l5:      db ' rg ', 0
sd_l6:      db ' wd ', 0
sd_l7:      db ' hs ', 0
sd_l8:      db ' tc ', 0
sd_l9:      db ' fm ', 0
sd_l10:     db ' xp ', 0
sd_l11:     db ' pz ', 0
sd_l12:     db '', 0
