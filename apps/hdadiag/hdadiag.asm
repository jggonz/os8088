; =============================================================================
; os8088 - HDADIAG: the Intel HDA driver's field instrument (SPEC.md 34.11.1)
;
; Asks the loaded HDA.DRV for everything a person debugging it off the machine
; would ask a debugger for - the PCI function, the controller's registers, the
; stream descriptor, the driver's own state, the BDL and ring as memory holds
; them, and a walk of every widget in the codec (capabilities, connections,
; amplifiers, pin defaults, converter formats) - then plays a tone through the
; native router and samples the stream engine twice while it sounds, and
; writes the lot to HDADIAG.TXT in the root of the disk it was launched from.
;
; A field machine has no debugger and no serial port anyone reads; the text
; file is what comes back from it. 8086 code throughout: the driver's two
; verbs (HDAV_INFO, HDAV_VERB) do the 386 half.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'HDA DIAG', hd_entry, 0

HD_W        equ 400
HD_H        equ 84
HD_CW       equ HD_W - 2
HD_CH       equ HD_H - TITLE_H - 1
HD_INFO_SZ  equ 0x2c0               ; HDAV_INFO's block (drivers/hda/hda.asm)
HD_TEXT_SZ  equ 26624               ; the report; ~6KB for a 40-widget codec,
                                    ; ~1.4KB a probe, seven probes
HD_TEXT_LIM equ HD_TEXT_SZ - 512    ; a node line stops here
HDAV_INFO   equ 1
HDAV_VERB   equ 2
HDAV_PROBE  equ 3
HD_TONE_HZ  equ 1000
HD_MAXNODE  equ 64

hd_entry:
    push si
    push di
    mov si, hd_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [hd_win], bx
    mov si, hd_s_wait
    call hd_set_status
    mov ax, hd_onwake
    call OSAPI_WM_ONWAKE
    call OSAPI_WM_WAKE              ; the run happens on the UI task, outside
    clc                             ; paint: file I/O is UI-task context
.out:
    pop di
    pop si
    ret

hd_onwake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call hd_run
    call hd_redraw
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_onkey:
    ret

hd_onclick:                         ; a click runs it again
    push ax
    push bx
    mov bx, [hd_win]
    call OSAPI_WM_WAKE
    pop bx
    pop ax
    ret

hd_set_status:                      ; SI = string
    push di
    mov di, hd_status
    call hd_copyz
    pop di
    ret

hd_copyz:                           ; SI -> DI, NUL included
    push ax
.c:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .c
    pop ax
    ret

; --- the window --------------------------------------------------------------
hd_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [hd_x], ax
    mov [hd_y], dx
    call hd_draw
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_redraw:
    push ax
    push bx
    push cx
    push dx
    mov bx, [hd_win]
    call OSAPI_WM_CONTENT
    mov [hd_x], ax
    mov [hd_y], dx
    mov bx, dx
    mov cx, ax
    add cx, HD_CW - 1
    add dx, HD_CH - 1
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    call hd_draw
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_draw:
    push ax
    push cx
    push dx
    push si
    mov ax, (CWHITE << 8) | CBLACK
    mov cx, [hd_x]
    add cx, 6
    mov dx, [hd_y]
    add dx, 8
    mov si, hd_s_head
    call OSAPI_FONT_RUN
    add dx, 14
    mov si, hd_status
    call OSAPI_FONT_RUN
    add dx, 14
    mov si, hd_s_again
    call OSAPI_FONT_RUN
    add dx, 14
    mov si, hd_s_hint
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop ax
    ret

; --- the run -----------------------------------------------------------------
hd_run:
    mov di, hd_text
    mov si, hd_s_banner
    call hd_puts
    call hd_info                    ; the driver's block, once, before the tone
    jc .nodrv
    call hd_fmt_info
    call hd_codec_walk
    call hd_tone_probe
    call hd_probes
    call hd_write
    ret
.nodrv:
    mov si, hd_s_nodrv
    call hd_set_status
    ret

; HDAV_INFO into hd_info_buf. out CF=1 = no HDA driver published.
hd_info:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, hd_info_buf
    mov cx, HD_INFO_SZ
    mov bh, DRVC_SOUND
    mov bl, HDAV_INFO
    call OSAPI_DRV_CALL
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; A codec verb through the driver. in DX:AX = the verb; out DX:AX = the
; response, CF=1 on a timeout (DX:AX = 0). BX preserved.
hd_verb:
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov bh, DRVC_SOUND
    mov bl, HDAV_VERB
    call OSAPI_DRV_CALL
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; THE NID IS EIGHT BITS AND THE SHIFT IS FOUR, so it needs the whole of DX:
; the first build shifted it inside DL, and every node from 10 up lost its
; high nibble - the report's node 12 was node 02 again, 14 was 04, and the
; table repeated with period 16 while the pins the driver drives were the one
; thing it could not show.
; GET_PARAMETER: BL = nid, AL = the parameter -> DX:AX
hd_param:
    push cx
    xor dh, dh                      ; (nid<<20) | (0xF00<<8) | param:
    mov dl, bl                      ;   high word = nid<<4 | 0xF
    mov cl, 4                       ;   low word  = 0x00pp
    shl dx, cl
    or dl, 0x0f
    xor ah, ah
    call hd_verb
    pop cx
    ret

; A 12-bit verb with an 8-bit payload: BL = nid, CX = the verb (e.g. 0xF02),
; AL = payload -> DX:AX
hd_v12:
    push cx
    xor dh, dh                      ; (nid<<20) | (V<<8) | P:
    mov dl, bl                      ;   high word = nid<<4 | V>>8
    mov cl, 4                       ;   low word  = (V & 0xFF)<<8 | P
    shl dx, cl
    pop cx
    or dl, ch
    mov ah, cl
    call hd_verb
    ret

; A 4-bit verb with a 16-bit payload: BL = nid, CL = the verb (0xA, 0xB),
; AX = payload -> DX:AX
hd_v4:
    push cx
    xor dh, dh                      ; (nid<<20) | (V<<16) | P16:
    mov dl, bl                      ;   high word = nid<<4 | V
    mov cl, 4                       ;   low word  = P16
    shl dx, cl
    pop cx
    or dl, cl
    call hd_verb
    ret

; --- text helpers: DI is the pen throughout ------------------------------------
hd_puts:                            ; SI = NUL string
    push ax
.c:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc si
    inc di
    jmp short .c
.out:
    pop ax
    ret

hd_nl:
    mov byte [di], 13
    inc di
    mov byte [di], 10
    inc di
    ret

hd_sp:
    mov byte [di], ' '
    inc di
    ret

hd_hex8:                            ; AL
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .nyb
    mov al, ah
    and al, 0x0f
    call .nyb
    pop cx
    pop ax
    ret
.nyb:
    add al, '0'
    cmp al, '9'
    jbe .st
    add al, 'a' - '0' - 10
.st:
    mov [di], al
    inc di
    ret

hd_hex16:                           ; AX
    push ax
    mov al, ah
    call hd_hex8
    pop ax
    call hd_hex8
    ret

hd_hex32:                           ; DX:AX
    push ax
    mov ax, dx
    call hd_hex16
    pop ax
    call hd_hex16
    ret

hd_dec:                             ; AX, unsigned decimal
    push ax
    push bx
    push dx
    push si
    mov si, hd_num + 5              ; digits grow leftwards from a NUL
    mov byte [si], 0
    mov bx, 10
.div:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    or ax, ax
    jnz .div
    call hd_puts
    pop si
    pop dx
    pop bx
    pop ax
    ret

; a hex dump of CX dwords at SI, 4 per line, with an offset column
hd_dwords:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bx, bx                      ; the offset column
.line:
    call hd_sp
    mov al, bl
    call hd_hex8
    mov byte [di], ':'
    inc di
    mov ah, 4                       ; up to four on this line
.dw:
    call hd_sp
    push ax
    mov ax, [si]
    mov dx, [si+2]
    call hd_hex32
    pop ax
    add si, 4
    add bx, 4
    dec cx
    jz .eol
    dec ah
    jnz .dw
    call hd_nl
    jmp short .line
.eol:
    call hd_nl
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; a hex dump of CX bytes at SI on one line
hd_bytes:
    push ax
    push cx
    push si
.b:
    call hd_sp
    mov al, [si]
    call hd_hex8
    inc si
    loop .b
    call hd_nl
    pop si
    pop cx
    pop ax
    ret

; --- the INFO block, formatted --------------------------------------------------
hd_fmt_info:
    mov si, hd_s_pci
    call hd_puts
    mov si, hd_info_buf
    mov cx, 64
    call hd_dwords
    mov si, hd_s_ctl
    call hd_puts
    mov si, hd_info_buf + 0x100
    mov cx, 32
    call hd_dwords
    mov si, hd_s_sd
    call hd_puts
    mov ax, [hd_info_buf + 0x1a0 + 4]  ; hda_sd
    call hd_hex16
    call hd_nl
    mov si, hd_info_buf + 0x180
    mov cx, 8
    call hd_dwords
    call hd_fmt_state
    mov si, hd_s_bdl
    call hd_puts
    mov si, hd_info_buf + 0x1c0
    mov cx, 16
    call hd_dwords
    mov si, hd_s_ring
    call hd_puts
    mov si, hd_info_buf + 0x200
    mov cx, 32
    call hd_bytes
    ret

hd_fmt_state:
    mov si, hd_s_state
    call hd_puts
    mov si, hd_info_buf + 0x1a0
    mov ax, [si]
    call hd_hex16                   ; raw claim
    call hd_sp
    mov ax, [si+2]
    call hd_hex16                   ; aligned segment
    call hd_sp
    mov ax, [si+4]
    call hd_hex16                   ; sd
    call hd_sp
    mov ax, [si+6]
    mov dx, [si+8]
    call hd_hex32                   ; bar
    call hd_sp
    mov ax, [si+10]
    call hd_hex16                   ; grant segment
    call hd_sp
    mov ax, [si+12]
    call hd_hex16                   ; pci command as found
    call hd_sp
    mov ax, [si+14]
    call hd_hex16                   ; up, active
    call hd_sp
    mov ax, [si+16]
    call hd_hex16                   ; tone_on, state
    call hd_sp
    mov ax, [si+18]
    call hd_dec                     ; rate
    call hd_sp
    mov ax, [si+20]
    call hd_dec                     ; total
    call hd_sp
    mov ax, [si+22]
    call hd_dec                     ; fed
    call hd_sp
    mov ax, [si+24]
    call hd_dec                     ; consumed
    call hd_sp
    mov ax, [si+26]
    mov dx, [si+28]
    call hd_hex32                   ; cr0
    call hd_sp
    mov ax, [si+30]
    call hd_hex16                   ; pins found (low byte), GPIO mask (high)
    call hd_sp
    mov ax, [si+0x90]
    call hd_hex16                   ; the SKU word the driver believed
    call hd_sp
    mov ax, [si+0x92]
    call hd_hex16                   ; COEF 0
    call hd_nl
    mov si, hd_s_pins
    call hd_puts
    mov si, hd_info_buf + 0x220     ; each pin found, nid/flag
    xor cx, cx
.pin:
    mov al, [si]
    call hd_hex8
    mov byte [di], '/'
    inc di
    mov al, [si+8]
    call hd_hex8
    call hd_sp
    inc si
    inc cx
    cmp cx, 8
    jb .pin
    call hd_nl
    ret

; --- the codec walk ----------------------------------------------------------
hd_codec_walk:
    mov si, hd_s_codec
    call hd_puts
    xor bl, bl                      ; root node: vendor, revision, subnodes
    xor al, al
    call hd_param
    call hd_hex32
    call hd_sp
    mov al, 2
    call hd_param
    call hd_hex32
    call hd_sp
    mov al, 4
    call hd_param
    call hd_hex32
    call hd_nl
    mov [hd_afg], dl                ; the first function group node
    mov bl, dl
    mov si, hd_s_afg
    call hd_puts
    mov al, bl
    call hd_hex8
    call hd_sp
    mov al, 4
    call hd_param
    call hd_hex32                   ; widget start<<16 | count
    mov [hd_wstart], dl
    mov [hd_wcount], al
    call hd_sp
    mov al, 5
    call hd_param
    call hd_hex32                   ; function group type
    call hd_sp
    mov al, 0x0a
    call hd_param
    call hd_hex32                   ; PCM rates and bits
    call hd_sp
    mov al, 0x12
    call hd_param
    call hd_hex32                   ; default output amp caps
    call hd_sp
    mov al, 0x0d
    call hd_param
    call hd_hex32                   ; default input amp caps
    call hd_sp
    mov cx, 0xf05
    xor al, al
    call hd_v12
    call hd_hex32                   ; power state
    call hd_sp
    mov cx, 0xf20
    xor al, al
    call hd_v12
    call hd_hex32                   ; the codec's subsystem ID: the SKU's
    call hd_sp                      ; first home (hda_rt_init)
    mov al, 0x11
    call hd_param
    call hd_hex32                   ; GPIO capabilities
    call hd_sp
    mov cx, 0xf15
    xor al, al
    call hd_v12
    call hd_hex8                    ; GPIO data
    mov byte [di], '/'
    inc di
    mov cx, 0xf16
    xor al, al
    call hd_v12
    call hd_hex8                    ; GPIO mask
    mov byte [di], '/'
    inc di
    mov cx, 0xf17
    xor al, al
    call hd_v12
    call hd_hex8                    ; GPIO direction
    call hd_nl
    call hd_coefs

    mov bl, [hd_wstart]
    mov al, [hd_wcount]
    cmp al, HD_MAXNODE
    jbe .cnt
    mov al, HD_MAXNODE
.cnt:
    mov [hd_left], al
.node:
    cmp byte [hd_left], 0
    je .done
    cmp di, hd_text + HD_TEXT_LIM
    jae .done
    call hd_node
    inc bl
    dec byte [hd_left]
    jmp short .node
.done:
    ret

; Realtek's coefficient file behind vendor node 20, the first 32 entries:
; COEF 0's bits 7:4 are the variant hda_rt_init keys its fixes on, and 4, d,
; e, f, 10 and 17 are the ones it writes. Index by verb 500, read by C00.
hd_coefs:
    xor bl, bl
    xor al, al
    call hd_param                   ; the vendor, again: only a 10ec has one
    cmp dx, 0x10ec
    jne .out
    mov si, hd_s_coef
    call hd_puts
    mov bl, 0x20
    xor ch, ch
.c:
    mov al, ch
    push cx
    mov cx, 0x500
    call hd_v12                     ; SET_COEF_INDEX
    mov cx, 0xc00
    xor al, al
    call hd_v12                     ; GET_PROC_COEF
    pop cx
    call hd_hex16
    call hd_sp
    inc ch
    cmp ch, 32
    jb .c
    call hd_nl
    mov si, hd_s_defs
    call hd_puts
    mov si, hd_info_buf + 0x280
    xor ch, ch
.d:
    lodsw
    call hd_hex16
    call hd_sp
    inc ch
    cmp ch, 32
    jb .d
    call hd_nl
.out:
    ret

; one widget, BL = nid: one line
hd_node:
    push bx
    push cx
    mov si, hd_s_nid
    call hd_puts
    mov al, bl
    call hd_hex8
    call hd_sp
    mov al, 9
    call hd_param                   ; widget caps
    mov [hd_caps], ax
    mov [hd_caps+2], dx
    call hd_hex32
    call hd_sp
    mov al, dl                      ; type = bits 23:20 = the high word's
    mov cl, 4                       ; low byte, upper nibble
    shr al, cl
    mov [hd_type], al
    call hd_hex8.nyb
    call hd_sp
    mov cx, 0xf05
    xor al, al
    call hd_v12                     ; power state
    call hd_hex16
    ; connections
    mov si, hd_s_conn
    call hd_puts
    test byte [hd_caps+1], 1        ; bit 8: a connection list
    jz .noconn
    mov al, 0x0e
    call hd_param
    mov [hd_clen], al
    and al, 0x7f
    call hd_hex8
    mov byte [di], ':'
    inc di
    xor ch, ch                      ; index
.ce:
    mov al, [hd_clen]
    and al, 0x7f
    cmp ch, al
    jae .noconn
    push cx
    mov al, ch
    mov cx, 0xf02
    call hd_v12
    pop cx
    call hd_sp
    test byte [hd_clen], 0x80
    jnz .long
    call hd_hex8                    ; short form: 4 entries a response
    mov al, ah
    call hd_sp
    call hd_hex8
    mov al, dl
    call hd_sp
    call hd_hex8
    mov al, dh
    call hd_sp
    call hd_hex8
    add ch, 4
    jmp short .ce
.long:
    call hd_hex16                   ; long form: 2 entries a response
    call hd_sp
    mov ax, dx
    call hd_hex16
    add ch, 2
    jmp short .ce
.noconn:
    ; output amplifier
    test byte [hd_caps], 4
    jz .noout
    mov si, hd_s_aout
    call hd_puts
    mov al, 0x12
    call hd_param
    call hd_hex32
    call hd_sp
    mov cl, 0x0b
    mov ax, 0xa000                  ; output, left
    call hd_v4
    call hd_hex16
    call hd_sp
    mov cl, 0x0b
    mov ax, 0x8000                  ; output, right
    call hd_v4
    call hd_hex16
.noout:
    ; input amplifiers, the first four indices
    test byte [hd_caps], 2
    jz .noin
    mov si, hd_s_ain
    call hd_puts
    mov al, 0x0d
    call hd_param
    call hd_hex32
    xor ch, ch
.ai:
    call hd_sp
    mov cl, 0x0b
    mov al, ch
    mov ah, 0x20                    ; input, left, index
    call hd_v4
    call hd_hex16
    mov byte [di], '/'
    inc di
    mov cl, 0x0b
    mov al, ch
    xor ah, ah                      ; input, right, index
    call hd_v4
    call hd_hex16
    inc ch
    cmp ch, 4
    jb .ai
.noin:
    mov al, [hd_type]
    cmp al, 4                       ; pin complex
    jne .notpin
    mov si, hd_s_pin
    call hd_puts
    mov cx, 0xf01
    xor al, al
    call hd_v12                     ; connection select
    call hd_hex8
    call hd_sp
    mov al, 0x0c
    call hd_param                   ; pin caps
    call hd_hex32
    call hd_sp
    mov cx, 0xf1c
    xor al, al
    call hd_v12                     ; configuration default
    call hd_hex32
    call hd_sp
    mov cx, 0xf07
    xor al, al
    call hd_v12                     ; pin widget control
    call hd_hex8
    call hd_sp
    mov cx, 0xf0c
    xor al, al
    call hd_v12                     ; EAPD/BTL
    call hd_hex8
    call hd_sp
    mov cx, 0xf09
    xor al, al
    call hd_v12                     ; pin sense: bit 31 presence
    call hd_hex32
    jmp short .eol
.notpin:
    cmp al, 1
    ja .eol                         ; 0 = output converter, 1 = input converter
    mov si, hd_s_conv
    call hd_puts
    mov al, 0x0a
    call hd_param                   ; the widget's own PCM rates and bits
    call hd_hex32                   ; (0 = the function group's)
    call hd_sp
    mov cl, 0x0a
    xor ax, ax
    call hd_v4                      ; converter format
    call hd_hex16
    call hd_sp
    mov cx, 0xf06
    xor al, al
    call hd_v12                     ; stream and channel
    call hd_hex8
.eol:
    call hd_nl
    pop cx
    pop bx
    ret

; --- the tone probe --------------------------------------------------------
; A one-second tone through the router - the Sound page's Test button, exactly
; - and the stream descriptor, the driver's counters and the ring's head read
; twice while it sounds. If LPIB moves between the two, the engine is running;
; the ring's bytes say what it is reading.
hd_tone_probe:
    mov si, hd_s_tone
    call hd_puts
    mov ax, HD_TONE_HZ
    mov cx, 18
    mov dl, 0x40
    call OSAPI_SND_TONE
    jnc .on
    mov si, hd_s_refused
    call hd_puts
    ret
.on:
    mov ax, 4
    call OSAPI_TASK_SLEEP
    call hd_sample
    mov ax, 5
    call OSAPI_TASK_SLEEP
    call hd_sample
    mov ax, 12
    call OSAPI_TASK_SLEEP           ; ...and let it finish before the write
    ret

hd_sample:
    call hd_info
    jc .out
    mov si, hd_s_sample
    call hd_puts
    mov si, hd_info_buf + 0x180
    mov cx, 8
    call hd_dwords
    call hd_fmt_state
    call hd_fmt_engine
    mov si, hd_s_ring
    call hd_puts
    mov si, hd_info_buf + 0x200
    mov cx, 32
    call hd_bytes
    mov si, hd_s_dac                ; ...and the DAC as the codec has it NOW,
    call hd_puts                    ; while the stream runs: format, stream
    mov bl, 2                       ; and channel, power, both amplifiers
    mov cl, 0x0a
    xor ax, ax
    call hd_v4
    call hd_hex16
    call hd_sp
    mov cx, 0xf06
    xor al, al
    call hd_v12
    call hd_hex8
    call hd_sp
    mov cx, 0xf05
    xor al, al
    call hd_v12
    call hd_hex16
    call hd_sp
    mov cl, 0x0b
    mov ax, 0xa000
    call hd_v4
    call hd_hex16
    call hd_sp
    mov cl, 0x0b
    mov ax, 0x8000
    call hd_v4
    call hd_hex16
    call hd_nl
    mov si, hd_s_check              ; ...and what a probe may have changed
    call hd_puts
    mov bl, 0x0c
    mov cl, 0x0b
    mov ax, 0x2000                  ; input, left, index 0
    call hd_v4
    call hd_hex16
    mov byte [di], '/'
    inc di
    mov cl, 0x0b
    xor ax, ax                      ; input, right, index 0
    call hd_v4
    call hd_hex16
    call hd_sp
    mov bl, [hd_info_buf + 0x220]   ; the first pin the driver found
    mov cx, 0xf0c
    xor al, al
    call hd_v12
    call hd_hex8
    mov byte [di], '/'
    inc di
    mov cx, 0xf07
    xor al, al
    call hd_v12
    call hd_hex8
    call hd_sp
    mov bl, 1                       ; the function group's power state...
    mov cx, 0xf05
    xor al, al
    call hd_v12
    call hd_hex16
    call hd_sp
    mov bl, 2                       ; ...the DAC's...
    mov cx, 0xf05
    xor al, al
    call hd_v12
    call hd_hex16
    call hd_sp
    mov bl, [hd_info_buf + 0x220]   ; ...and the first pin's
    mov cx, 0xf05
    xor al, al
    call hd_v12
    call hd_hex16
    call hd_sp
    mov bl, 0x20
    mov al, 4
    call hd_coef_one
    mov al, 0x0d
    call hd_coef_one
    mov al, 0x17
    call hd_coef_one
    call hd_nl
.out:
    ret

hd_coef_one:                        ; BL = 20, AL = the index: one coefficient
    push cx
    mov cx, 0x500
    call hd_v12
    mov cx, 0xc00
    xor al, al
    call hd_v12
    pop cx
    call hd_hex16
    call hd_sp
    ret

; The engine as three clocks and a check: WALCLK (24 MHz, the controller's
; own), LPIB (what the engine says it fetched), and the DMA position buffer's
; entry for our stream - written by the DEVICE into the driver's claim, so a
; value that tracks LPIB proves the controller and the CPU mean the same
; memory. Then how many of the ring's bytes are not a square wave's.
hd_fmt_engine:
    mov si, hd_s_engine
    call hd_puts
    mov si, hd_info_buf
    mov ax, [si+0x130]
    mov dx, [si+0x132]
    call hd_hex32                   ; WALCLK
    call hd_sp
    mov ax, [si+0x184]
    mov dx, [si+0x186]
    call hd_hex32                   ; LPIB
    call hd_sp
    mov bx, [si+0x1a4]              ; our stream descriptor's offset...
    sub bx, 0x80
    mov cl, 2
    shr bx, cl                      ; ...is index * 0x20; the entry is index * 8
    and bx, 0x38                    ; (report 3 read index * 4: another stream's)
    mov ax, [si+0x240+bx]
    mov dx, [si+0x242+bx]
    call hd_hex32                   ; the position the device wrote
    call hd_sp
    mov ax, [si+0x234]
    call hd_dec                     ; foreign bytes in the ring
    call hd_nl
    mov si, hd_s_start
    call hd_puts
    mov si, hd_info_buf + 0x236
    mov ax, [si]
    call hd_hex16                   ; SRST polls to 1
    call hd_sp
    mov ax, [si+2]
    call hd_hex16                   ; ...and to 0
    call hd_sp
    mov ax, [si+4]
    call hd_hex16                   ; pattern, control byte after RUN
    call hd_sp
    mov ax, [si+6]
    call hd_hex16                   ; LPIB a settle after RUN
    call hd_sp
    mov ax, [si+8]
    call hd_hex16                   ; the format the start wrote
    call hd_nl
    ret

; --- the ear's experiment ----------------------------------------------------
; Six probes on the driver's own stream, each one second with a silent
; second after it, and two samples of the engine while each sounds: the ring
; all zeros, then a 1 kHz square at -6 dB with one thing done before each
; start - nothing, a power cycle of the function group with the state
; polled, a codec function reset and full re-initialisation, a link reset
; and full re-initialisation, the speaker pin off so only the headphone jack
; drives. What is heard at each - nothing, a tone, or static - is the
; reading; the file says what the engine was fetching meanwhile, what the
; start's handshake saw, and reads back the power states.
hd_probes:
    mov si, hd_s_probes
    call hd_puts
    xor al, al
    mov si, hd_s_p0
    call hd_probe_one
    mov al, 1
    mov si, hd_s_p1
    call hd_probe_one
    mov al, 2
    mov si, hd_s_p2
    call hd_probe_one
    mov al, 3
    mov si, hd_s_p3
    call hd_probe_one
    mov al, 4
    mov si, hd_s_p4
    call hd_probe_one
    mov al, 5
    mov si, hd_s_p5
    call hd_probe_one
    ret

hd_probe_one:                       ; AL = the pattern, SI = its label
    push ax
    call hd_puts
    pop ax
    push ax
    mov bh, DRVC_SOUND
    mov bl, HDAV_PROBE
    call OSAPI_DRV_CALL
    pop bx                          ; (the pattern, for nothing)
    jnc .on
    mov si, hd_s_refused
    call hd_puts
    ret
.on:
    mov ax, 4
    call OSAPI_TASK_SLEEP
    call hd_sample                  ; twice, so LPIB moving is a delta INSIDE
    mov ax, 5                       ; the probe and not a guess across two
    call OSAPI_TASK_SLEEP
    call hd_sample
    mov ax, 9
    call OSAPI_TASK_SLEEP
    mov al, 0xff
    mov bh, DRVC_SOUND
    mov bl, HDAV_PROBE
    call OSAPI_DRV_CALL             ; stop
    mov ax, 18
    call OSAPI_TASK_SLEEP           ; the silent second between
    ret

; --- the file -------------------------------------------------------------------
hd_write:
    mov cx, di
    sub cx, hd_text                 ; the report's length
    mov [hd_len], cx
    call OSAPI_FILE_HERE            ; BL = the volume we were launched from
    xor dx, dx                      ; ...and its root
    call OSAPI_FILE_GOTO_QM
    jc .err
    push ds
    pop es
    mov si, hd_s_fname
    mov bx, hd_text
    mov cx, [hd_len]
    xor dx, dx
    call OSAPI_FILE_WRITE
    jc .err
    mov di, hd_status
    mov si, hd_s_wrote
    call hd_puts
    mov ax, [hd_len]
    call hd_dec
    mov si, hd_s_bytes
    call hd_copyz
    ret
.err:
    push ax
    mov di, hd_status
    mov si, hd_s_werr
    call hd_puts
    pop ax
    call hd_dec
    mov byte [di], 0
    ret

hd_tpl:
    dw 60, 40, HD_W, HD_H
    dw hd_title, hd_paint, hd_onkey, hd_onclick

hd_title:    db 'HDA Diagnostic', 0
hd_s_head:   db 'Intel HDA driver report', 0
hd_s_wait:   db 'Collecting...', 0
hd_s_again:  db 'Click in the window to run it again.', 0
hd_s_hint:   db 'The file is in the root of the boot disk.', 0
hd_s_nodrv:  db 'HDA.DRV is not loaded (Control Panel > Drivers)', 0
hd_s_wrote:  db 'Wrote HDADIAG.TXT, ', 0
hd_s_bytes:  db ' bytes', 0
hd_s_werr:   db 'Could not write HDADIAG.TXT: error ', 0
hd_s_fname:  db 'HDADIAG.TXT', 0
hd_s_banner: db 'os8088 HDADIAG 8 - Intel HDA driver report', 13, 10, 0
hd_s_pci:    db 'PCI 00:1b.0 configuration space (dwords):', 13, 10, 0
hd_s_ctl:    db 'controller registers 00-7c:', 13, 10, 0
hd_s_sd:     db 'stream descriptor at ', 0
hd_s_state:  db 'driver: raw seg sd bar grant pcicmd up/act tone/st rate total fed cons cr0 pins/gpio sku coef0:', 13, 10, ' ', 0
hd_s_pins:   db 'driver pins nid/flag (1 = headphone):', 13, 10, ' ', 0
hd_s_coef:   db 'coef 20 (00-1f):', 13, 10, ' ', 0
hd_s_bdl:    db 'BDL:', 13, 10, 0
hd_s_ring:   db 'ring head:', 0
hd_s_codec:  db 'codec 0: vendor rev subnodes:', 13, 10, ' ', 0
hd_s_afg:    db 'afg nid subnodes type pcm outamp inamp power ssid gpiocaps data/mask/dir:', 13, 10, ' ', 0
hd_s_nid:    db 'nid ', 0
hd_s_conn:   db ' conn ', 0
hd_s_aout:   db ' aout ', 0
hd_s_ain:    db ' ain ', 0
hd_s_pin:    db ' pin sel caps cfg ctl eapd sense ', 0
hd_s_conv:   db ' conv pcm fmt strm ', 0
hd_s_tone:   db 'tone probe (1000 Hz, 1 s, through the router):', 13, 10, 0
hd_s_refused: db ' refused', 13, 10, 0
hd_s_sample: db 'sample: stream descriptor', 13, 10, 0
hd_s_dac:    db 'dac 02 now: fmt strm pwr ampL ampR: ', 0
hd_s_engine: db 'engine: walclk lpib devpos foreign: ', 0
hd_s_probes: db 'probes on the driver stream, 1 s each, a silent second between:', 13, 10, 0
hd_s_p0:     db 'probe 0: ring all zero, engine running', 13, 10, 0
hd_s_p1:     db 'probe 1: 1 kHz square, nothing changed (the control)', 13, 10, 0
hd_s_p2:     db 'probe 2: 1 kHz square after AFG D3 -> D0, state polled, long waits', 13, 10, 0
hd_s_p3:     db 'probe 3: 1 kHz square after a codec function reset and full re-init', 13, 10, 0
hd_s_p4:     db 'probe 4: 1 kHz square after a link reset and full re-init (second attach)', 13, 10, 0
hd_s_p5:     db 'probe 5: 1 kHz square, speaker pin control 0 - headphone jack only', 13, 10, 0
hd_s_check:  db 'check: 0c in0 L/R, pin0 eapd ctl, afg ps, dac ps, pin0 ps, coef 04 0d 17: ', 0
hd_s_defs:   db 'coef 20 as the link reset left them (before the driver wrote):', 13, 10, ' ', 0
hd_s_start:  db 'last start: srst->1 srst->0 pat/ctl lpib+settle fmt: ', 0

HD_BSS_OWN equ 2 + 2 + 2 + 2 + 4 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 64 + 6
    OS88_BSS HD_BSS_OWN + HD_INFO_SZ + HD_TEXT_SZ
    OS88_IMAGE_END

hd_win       equ os88_image_end + 0
hd_x         equ os88_image_end + 2
hd_y         equ os88_image_end + 4
hd_len       equ os88_image_end + 6
hd_caps      equ os88_image_end + 8     ; dword
hd_afg       equ os88_image_end + 12
hd_wstart    equ os88_image_end + 13
hd_wcount    equ os88_image_end + 14
hd_left      equ os88_image_end + 15
hd_type      equ os88_image_end + 16
hd_clen      equ os88_image_end + 17
hd_pad       equ os88_image_end + 18
hd_status    equ os88_image_end + 19    ; 64 bytes
hd_num       equ os88_image_end + 83    ; hd_dec's 6-byte scratch
hd_info_buf  equ os88_image_end + HD_BSS_OWN
hd_text      equ hd_info_buf + HD_INFO_SZ
