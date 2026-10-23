; =============================================================================
; os8088 - apps/xtmp3/x3rep.inc
;
; THE FIELD REPORT (docs/xtmp3/FIELD.md; `make xtmp3field`). A test build
; only: assembled under -DX3REPORT into build/xtmp3r.o88, which no shipped
; disk carries. It exists for 86Box, which has no debug socket and cannot be
; sent a key from this host: a machine booted into XTMP3 by an \AUTOSTRT link
; writes what the ladder decided, and why, into SYSTEM\APPDATA\X3REPORT.TXT
; on the program's own volume, and the host reads it off the image afterwards
; (tools/x3field.py).
;
; Three moments, each called from one marked line in x3eng.inc:
;   x3r_cal0     x3l_start: the calibration's start, on pit_now
;   x3r_decided  x3l_done:  its end, the decision and the probes' figures
;   x3r_wd/we    the report written - when the decode-first dialog opens
;                (EVENT D) and when a live or finished stream has played out
;                and been closed (EVENT E).
;   x3r_tick     every engine tick: the first ERROR is written (EVENT X), its
;                text in an ERR line - a ladder that ends in an error writes
;                neither of the others Never while a stream plays: a
;                floppy write on the UI task could itself cause an underrun.
;
; The file is text, one field a line, numbers in hex:
;   X3REPORT 1                  the format
;   EVENT D|E                   which moment wrote it
;   CPU aa hh                   OSAPI_CPU_INFO's AL (CPU_*) and AH (CPU_F_*)
;   CARD cc CAPS hhhh           [x3_card], [x3_caps] (SND_CAP_*)
;   SRC rrrr CH cc              the source's rate and channels
;   CALPIT hhhhhhhh             x3l_start to x3l_done, in 838 ns PIT counts
;   MEAS m1 m2 .. m7            x3l_meas: audio s a wall s x 100, by X3Q_*
;                               from ORIGINAL; FFFF = not measured
;   CHOICE cc WANT ww           x3l_choice (FF none) and the user's override
;   MD mm QUAL qq RATE rrrr CHANS cc BITS bb FLAGS ff
;                               the mode and x3_st's output, at the write
;   UNDER uuuu PEAK pppp GRANS gggg
;   FREE0 kkkk AVAIL kkkk LARGEST kkkk
;                               the heap's free KB at launch (x3_free0), and
;                               OSAPI_MEM_AVAIL's total and largest run now
;   ERR text                    X3S_ERR's text (EVENT X), else nothing
;
; 86BOX FLUSHES A DISK IMAGE ON ITS NEXT SEEK, and a machine that is killed
; - the only way to stop one from here - loses whatever the emulator has not
; flushed. So the write is followed by a READ of the same file: the read's
; seek pushes the write out, and what is on the host's image afterwards is
; the whole report.
; =============================================================================

X3R_BUFSZ   equ 256

x3r_s_name: db 'X3REPORT.TXT', 0
x3r_s_head: db 'X3REPORT 1', 13, 10, 'EVENT ', 0
x3r_s_cpu:  db 13, 10, 'CPU ', 0
x3r_s_card: db 13, 10, 'CARD ', 0
x3r_s_caps: db ' CAPS ', 0
x3r_s_src:  db 13, 10, 'SRC ', 0
x3r_s_ch:   db ' CH ', 0
x3r_s_cal:  db 13, 10, 'CALPIT ', 0
x3r_s_meas: db 13, 10, 'MEAS', 0
x3r_s_cho:  db 13, 10, 'CHOICE ', 0
x3r_s_want: db ' WANT ', 0
x3r_s_md:   db 13, 10, 'MD ', 0
x3r_s_qual: db ' QUAL ', 0
x3r_s_rate: db ' RATE ', 0
x3r_s_chs:  db ' CHANS ', 0
x3r_s_bits: db ' BITS ', 0
x3r_s_flg:  db ' FLAGS ', 0
x3r_s_und:  db 13, 10, 'UNDER ', 0
x3r_s_peak: db ' PEAK ', 0
x3r_s_gran: db ' GRANS ', 0
x3r_s_free: db 13, 10, 'FREE0 ', 0
x3r_s_avl:  db ' AVAIL ', 0
x3r_s_lrg:  db ' LARGEST ', 0
x3r_s_err:  db 13, 10, 'ERR ', 0
x3r_s_end:  db 13, 10, 0

; x3r_cal0 - the calibration starts. keeps everything
x3r_cal0:
    push ax
    push dx
    call pit_now
    mov [x3r_t0], ax
    mov [x3r_t0+2], dx
    mov byte [x3r_once], 0
    pop dx
    pop ax
    ret

; x3r_decided - the ladder has decided: its wall time. keeps everything
x3r_decided:
    push ax
    push dx
    call pit_now
    sub ax, [x3r_t0]
    sbb dx, [x3r_t0+2]
    mov [x3r_cal], ax
    mov [x3r_cal+2], dx
    pop dx
    pop ax
    ret

; x3r_wd / x3r_we - the report, EVENT D (the decode-first dialog) or E (a
; stream played out). keep everything
x3r_wd:
    push ax
    mov al, 'D'
    call x3r_write
    pop ax
    ret

x3r_we:
    push ax
    mov al, 'E'
    call x3r_write
    pop ax
    ret

x3r_tick:
    cmp byte [x3_st+X3S_STATE], X3ST_ERROR
    jne .out
    cmp byte [x3r_xdone], 0
    jne .out
    mov byte [x3r_xdone], 1
    push ax
    mov al, 'X'
    call x3r_write
    pop ax
.out:
    ret

; x3r_write - AL = the event letter: the report into SYSTEM\APPDATA and read
; back. Once a track for 'E'. UI task only. keeps everything
x3r_write:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp al, 'E'
    jne .go
    cmp byte [x3r_once], 0
    jne .out
    mov byte [x3r_once], 1
.go:
    mov di, x3r_buf
    mov si, x3r_s_head
    call x3_put
    mov [di], al
    inc di
    mov si, x3r_s_cpu
    call x3_put
    call OSAPI_CPU_INFO
    call x3c_hex2
    mov byte [di], ' '
    inc di
    mov al, ah
    call x3c_hex2
    mov si, x3r_s_card
    call x3_put
    mov al, [x3_card]
    call x3c_hex2
    mov si, x3r_s_caps
    call x3_put
    mov ax, [x3_caps]
    call x3c_hex4
    mov si, x3r_s_src
    call x3_put
    mov ax, [x3_srate]
    call x3c_hex4
    mov si, x3r_s_ch
    call x3_put
    mov al, [x3_snch]
    call x3c_hex2
    mov si, x3r_s_cal
    call x3_put
    mov ax, [x3r_cal+2]
    call x3c_hex4
    mov ax, [x3r_cal]
    call x3c_hex4
    mov si, x3r_s_meas
    call x3_put
    mov bx, 2                       ; from X3Q_ORIG
.m:
    mov byte [di], ' '
    inc di
    mov ax, [x3l_meas+bx]
    call x3c_hex4
    add bx, 2
    cmp bx, X3Q_N * 2
    jb .m
    mov si, x3r_s_cho
    call x3_put
    mov al, [x3l_choice]
    call x3c_hex2
    mov si, x3r_s_want
    call x3_put
    mov al, [x3_st+X3S_QWANT]
    call x3c_hex2
    mov si, x3r_s_md
    call x3_put
    mov al, [x3_md]
    call x3c_hex2
    mov si, x3r_s_qual
    call x3_put
    mov al, [x3_st+X3S_QUAL]
    call x3c_hex2
    mov si, x3r_s_rate
    call x3_put
    mov ax, [x3_st+X3S_RATE]
    call x3c_hex4
    mov si, x3r_s_chs
    call x3_put
    mov al, [x3_st+X3S_CHANS]
    call x3c_hex2
    mov si, x3r_s_bits
    call x3_put
    mov al, [x3_st+X3S_BITS]
    call x3c_hex2
    mov si, x3r_s_flg
    call x3_put
    mov al, [x3_st+X3S_FLAGS]
    call x3c_hex2
    mov si, x3r_s_und
    call x3_put
    mov ax, [x3_st+X3S_UNDER]
    call x3c_hex4
    mov si, x3r_s_peak
    call x3_put
    mov ax, [x3_st+X3S_PEAK]
    call x3c_hex4
    mov si, x3r_s_gran
    call x3_put
    mov ax, [x3_grans]
    call x3c_hex4
    mov si, x3r_s_free
    call x3_put
    mov ax, [x3_free0]
    call x3c_hex4
    push bx
    call OSAPI_MEM_AVAIL            ; AX = the largest run, BX = the total
    xchg ax, bx
    mov si, x3r_s_avl
    call x3_put
    call x3c_hex4
    mov si, x3r_s_lrg
    call x3_put
    mov ax, bx
    call x3c_hex4
    pop bx
    mov si, [x3_st+X3S_ERR]
    or si, si
    jz .noerr
    push si
    mov si, x3r_s_err
    call x3_put
    pop si
    mov cx, 40
    cld
.e:
    lodsb
    or al, al
    jz .noerr
    mov [di], al
    inc di
    loop .e
.noerr:
    mov si, x3r_s_end
    call x3_put
    mov cx, di
    sub cx, x3r_buf                 ; CX = the report's bytes
    call x3c_ixhome                 ; the package's own volume (as the index)
    push ds
    pop es
    mov si, x3r_s_name
    mov bx, x3r_buf
    mov di, x3r_st
    push cx
    call od_write
    pop cx
    jc .back
    mov si, x3r_s_name              ; THE READ-BACK: 86Box's flush (above)
    mov bx, x3r_buf
    xor dx, dx
    call od_read
.back:
    call x3c_ixback
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%macro X3_REPBSS 0
    X3B x3r_t0, 4                   ; the calibration's start (pit_now)
    X3B x3r_cal, 4                  ; ...and its length
    X3B x3r_once, 1                 ; the end's report is written once
    X3B x3r_xdone, 1                ; ...and an error's
    X3B x3r_st, 1                   ; od_write's state byte
    X3B x3r_buf, X3R_BUFSZ          ; the text
%endmacro
