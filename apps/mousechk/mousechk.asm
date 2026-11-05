; =============================================================================
; os8088 - apps/mousechk/mousechk.asm
;
; MOUSE CHECK (SPEC.md 126): what every pointing device this system supports
; looks like to this machine, in one window - the serial ports' identify bursts
; and contest, the PS/2 controller's boot record and, on a button, a live probe
; of it, the VMware absolute pointer, the USB driver and the keyboard mouse -
; with a lamp per section and one plain verdict at the top. It exists because a
; shipped disk cannot be sent a knob kernel (SPEC.md 9.4.6 MOUDIAG): the board
; that will not find its PS/2 mouse has the disk it has.
;
; THE SHELL IS THIS FILE, ui.inc AND rows.inc (SPEC.md 126.2). The four SECTIONS
; (summary.inc, serial.inc, ps2.inc + probe.inc, other.inc) know nothing about a
; window: each is an xx_rows proc that calls rows.inc's builder API, and the
; shell rebuilds the whole list from them about every half second and redraws
; only the screen lines whose row changed. kread.inc is the ONE shipped reader
; of the kernel's debug registry that SPEC.md 57.3 allows (SPEC.md 126.4).
;
; Label prefix mc_ for the shell; sum_, ser_, p2_, oth_ for the sections.
; =============================================================================

%include "os88api.inc"
%include "buildnum.inc"             ; BUILD_NUM (SPEC.md 14.2), from $(BUILD)

    OS88_HEADER 'Mouse Check', mc_entry, 1, OS88_HIGH | OS88_HIGHMEM
    OS88_ICON16                     ; a mouse
    ; --- mask ---
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    ; --- data: the cord, the body, the two buttons ---
    dw 0x0180, 0x0180, 0x0FF0, 0x1008
    dw 0x1188, 0x1188, 0x1FF8, 0x1008
    dw 0x1008, 0x1008, 0x1008, 0x1008
    dw 0x1008, 0x0810, 0x07E0, 0x0000
    OS88_ICON16_END

; -----------------------------------------------------------------------------
; mc_entry - package entry point (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, IF = 1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, from wm_create)
; -----------------------------------------------------------------------------
mc_entry:
    mov si, mc_tpl
    call OSAPI_WM_CREATE
    jc mc_ret
    mov [mc_win], bx
    OS88_REGION_MOVABLE_NOKEEP mc_ret ; SPEC.md 66.6.1: every byte that
                                    ; outlives a pass is a static and moves
                                    ; with us, so the proc is a bare `ret`
    mov si, mc_pref                 ; a size per adapter (SPEC.md 11.100.1)
    call OSAPI_WM_PREFER
    mov al, 1
    call OSAPI_WM_SNAP              ; content origin on a multiple of 8, so
                                    ; every run is font_run's one-store path
    mov si, mc_about
    call OSAPI_ABOUT_SET
    mov ax, mc_tick                 ; the refresh (SPEC.md 13.9). One-shot, so
    call OSAPI_WM_ONTIMER           ; mc_tick arms the next; kern_small has no
    mov ax, MC_TICKS                ; timer and answers CF = 1: the Refresh
    call OSAPI_WM_TIMER             ; button is then the only refresh
    xchg ax, bx                     ; AX = the window
    mov bx, mc_btrec
    mov si, mc_onup                 ; W_ONMOUSEUP: a button fires
    mov di, mc_ondrag
    mov dx, mc_onclick              ; a press that was not a button's
    call os88ui_btninit             ; keeps every register
    xchg ax, bx                     ; BX = the window again
    call mc_kinit                   ; the build check, once (kread.inc)
    call sum_init
    call ser_init
    call p2_init
    call oth_init
    push bx
    call mc_rebuild
    pop bx
    clc
mc_ret:
    ret

; -----------------------------------------------------------------------------
; mc_rebuild - the whole list again, from the sections, in their order
; in:  ES = KERNEL_SEG, DS = package; out: nothing; clobbers everything
; -----------------------------------------------------------------------------
mc_rebuild:
    call mc_reset
    call sum_rows
    call ser_rows
    call p2_rows
    call oth_rows
    jmp mc_finish

; -----------------------------------------------------------------------------
; mc_tick - W_ONTIMER (SPEC.md 13.9): re-arm, rebuild, draw what moved
; in:  SI = window ptr; the UI task, gfx lock held, the clip NOT armed
; -----------------------------------------------------------------------------
mc_tick:
    mov bx, si
    mov ax, MC_TICKS
    call OSAPI_WM_TIMER             ; one-shot, disarmed before we were called
    call mc_rebuild
    jmp mc_after

; -----------------------------------------------------------------------------
; the keyboard (SPEC.md 126.3): every control has a letter, and the list
; scrolls on PgUp/PgDn/Home/End and Up/Down. A user with only the keyboard
; mouse has the arrows taken (SPEC.md 9.6), so the letters are the way in.
; in:  AX = the key (AH scan code, AL character), SI = window, lock held
; -----------------------------------------------------------------------------
mc_onkey:
    call mc_regeo
    cmp byte [mc_btrec+OS88UI_BT_N], 0
    jne .live
    jmp mc_abdown                   ; any key takes the About card down
.live:
    mov dl, al
    and dl, 0xDF                    ; a letter, either case
    cmp dl, 'R'
    je mc_act_refresh
    cmp dl, 'P'
    je mc_act_probe
    cmp dl, 'S'
    je mc_act_save
    cmp dl, 'C'
    je mc_act_copy
    cmp dl, 'Q'
    je mc_act_quit
    cmp al, 0x1B
    je mc_act_quit
    cmp dl, 'B'                     ; a page back; Space is a page on
    je .pgup
    cmp al, ' '
    je .pgdn
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    cmp ah, 0x49
    je .pgup
    cmp ah, 0x51
    je .pgdn
    cmp ah, 0x47
    je .home
    cmp ah, 0x4F
    je .end
    ret
.up:
    mov ax, -1
    jmp mc_scroll
.down:
    mov ax, 1
    jmp mc_scroll
.pgup:
    mov ax, [mc_fit]
    neg ax
    jmp mc_scroll
.pgdn:
    mov ax, [mc_fit]
    jmp mc_scroll
.home:
    mov ax, -MC_MAXR
    jmp mc_scroll
.end:
    mov ax, MC_MAXR
    jmp mc_scroll

; -----------------------------------------------------------------------------
; the buttons
; -----------------------------------------------------------------------------
mc_ondrag:
    mov bx, mc_btrec
    jmp os88ui_btndrag

; mc_onup - W_ONMOUSEUP: a button fires on the release
mc_onup:
    call mc_regeo
    mov bx, mc_btrec
    call os88ui_btnup               ; AX = index + 1, 0 = cancelled
    dec ax
    js .out
    cmp al, MB_REFRESH
    je mc_act_refresh
    cmp al, MB_PROBE
    je mc_act_probe
    cmp al, MB_SAVE
    je mc_act_save
    cmp al, MB_COPY
    je mc_act_copy
    jmp mc_act_quit                 ; Quit
.out:
    ret

; mc_act_refresh - Refresh: the list again now, and what moved drawn
mc_act_refresh:
    call mc_rebuild
    jmp mc_after

; mc_act_probe - Probe PS/2 (SPEC.md 126.8). A greyed control explains itself
mc_act_probe:
    call p2_canprobe                ; CF = 1: SI = the reason
    jc mc_toast
    call p2_probe
    call mc_rebuild
    jmp mc_after

mc_act_quit:
    mov bx, [mc_win]
    call OSAPI_WM_CLOSE             ; (a far call: never jmp to a slot)
    ret

; mc_toast - SI = a NUL line in our segment (SPEC.md 59); keeps ES
mc_toast:
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    ret

; -----------------------------------------------------------------------------
; the report (SPEC.md 126.3): the rows as CR LF text lines. Copy puts it on the
; clipboard (SPEC.md 55); Save Report asks where with the standard Save dialog,
; MOUSECHK.TXT already in the box, and the dialog leaves the volume on the
; folder the user chose, which is where mc_ondlg writes.
; -----------------------------------------------------------------------------
mc_act_save:
    mov al, 1                       ; FDLG_SAVE
    mov bx, [mc_win]
    mov di, mc_ondlg
    mov si, mc_dname
    call OSAPI_FILE_DLG             ; CF = 1: one is already up - that IS the
    ret                             ; answer to the command

mc_act_copy:
    push es
    call mc_mkreport
    jc .nomem
    xor si, si
    mov cx, [mc_rend]
    call OSAPI_CLIP_PUT             ; ES:SI = the text
    pushf
    call mc_freereport
    popf
    pop es
    mov si, mc_t_copied
    jnc .t
    mov si, mc_t_nocopy
.t:
    jmp mc_toast
.nomem:
    pop es
    mov si, mc_t_nomem
    jmp mc_toast

; mc_ondlg - the dialog's completion (SPEC.md 38.6): AL = mode, SI = our
;            window, DI = the chosen name in the kernel's buffer (ES = it).
;            The dialog is gone and the volume stands on the chosen folder
mc_ondlg:
    mov bx, mc_name
    mov cx, 12
.n:
    mov al, [es:di]
    mov [bx], al
    inc di
    inc bx
    loop .n
    mov byte [bx], 0
    cmp byte [mc_name], 0
    je .done                        ; no name: nothing to save
    call mc_mkreport
    jc .nomem
    xor bx, bx                      ; ES:BX = the text
    xor dx, dx
    mov cx, [mc_rend]               ; DX:CX = its bytes
    mov si, mc_name
    call OSAPI_FILE_WRITE
    pushf
    call mc_freereport
    popf
    jc .fail
    mov di, mc_run                 ; "Saved " + the name
    mov si, mc_t_saved
    call mc_scpy
    mov si, mc_name
    call mc_scpy
    mov si, mc_run
    jmp short .say
.fail:
    mov si, mc_t_nosave
    jmp short .say
.nomem:
    mov si, mc_t_nomem
.say:
    push ds
    pop es                          ; (mc_mkreport left ES on the claim)
    xor cx, cx
    call OSAPI_TOAST
.done:
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [mc_win]
    jmp mc_repaint                  ; the callback must repaint (SPEC.md 38.6)

; mc_scpy - append the NUL string at SI to DS:DI, leaving DI on its NUL
mc_scpy:
    mov al, [si]
    mov [di], al
    or al, al
    jz .z
    inc si
    inc di
    jmp short mc_scpy
.z:
    ret

; -----------------------------------------------------------------------------
; mc_mkreport - the list as it is NOW, as text, in a transient claim
; out: CF = 0: ES = the claim, [mc_rseg] its segment, [mc_rend] the length;
;      CF = 1: no memory, ES kept
; -----------------------------------------------------------------------------
mc_mkreport:
    call mc_rebuild                 ; ES = KERNEL_SEG for the sections
    mov ax, [mc_nrows]
    mov cx, 70                      ; a row, its prefix and CR LF, rounded up
    mul cx
    add ax, 512 + 1023              ; ...and the head
    mov cl, 10
    shr ax, cl                      ; AX = KB
    call OSAPI_MEM_CLAIM            ; DX = the segment
    jc .out
    mov [mc_rseg], dx
    mov es, dx
    mov word [mc_rend], 0
    cld
    ; --- the head ---------------------------------------------------------
    mov si, mc_r_title
    call mc_emit
    mov di, mc_run
    mov ax, BUILD_NUM
    call dec_u16
    mov si, mc_run
    call mc_emit
    call mc_crlf
    mov di, mc_run                 ; "Date 2026-11-05 12:34"
    mov si, mc_r_date
    call mc_scpy
    call OSAPI_CLK_GET              ; CX year, DH month, DL day, BH:BL hh:mm
    push bx
    push dx
    mov ax, cx
    mov cl, 100
    div cl                          ; AL = the century, AH = the rest
    push ax
    call mc_two
    pop ax
    mov al, ah
    call mc_two
    mov byte [di], '-'
    inc di
    pop dx
    push dx
    mov al, dh
    call mc_two
    mov byte [di], '-'
    inc di
    pop dx
    mov al, dl
    call mc_two
    mov byte [di], ' '
    inc di
    pop bx
    push bx
    mov al, bh
    call mc_two
    mov byte [di], ':'
    inc di
    pop bx
    mov al, bl
    call mc_two
    mov si, mc_run
    call mc_emit
    call mc_crlf
    mov si, mc_r_kid                ; "Kernel id 9E9D 928A 1789, ..."
    call mc_emit
    mov ax, [mc_kid]
    call mc_ehex16
    mov al, ' '
    call mc_echar
    mov ax, [mc_kid+2]
    call mc_ehex16
    mov al, ' '
    call mc_echar
    mov ax, [mc_kid+4]
    call mc_ehex16
    mov si, mc_r_kok
    cmp byte [mc_kok], 0
    jne .k
    mov si, mc_r_knok
.k:
    call mc_emit
    call mc_crlf
    ; --- the rows ---------------------------------------------------------
    mov word [mc_ri], 0
.r:
    mov bx, [mc_ri]
    cmp bx, [mc_nrows]
    jae .done
    shl bx, 1
    mov si, [mc_roff+bx]
    mov al, [si]                    ; kind
    mov ah, [si+1]                  ; lamp
    add si, 2
    or al, al
    jnz .plain
    call mc_crlf                    ; a heading: a blank line, then
    push si
    mov al, '['
    call mc_echar
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov si, [mc_lnames+bx]
    call mc_emit
    mov si, mc_r_close
    call mc_emit
    pop si
    jmp short .txt
.plain:
    push si
    mov si, mc_r_indent
    call mc_emit
    pop si
.txt:
    call mc_emit
    call mc_crlf
    inc word [mc_ri]
    jmp short .r
.done:
    clc
.out:
    ret

; mc_freereport - give the claim back; keeps the flags
mc_freereport:
    pushf
    push dx
    mov dx, [mc_rseg]
    call OSAPI_MEM_FREE
    pop dx
    popf
    ret

; mc_emit - the NUL string at DS:SI onto the report at ES:[mc_rend]
mc_emit:
    push ax
    push si
    push di
    mov di, [mc_rend]
.l:
    lodsb
    or al, al
    jz .d
    stosb
    jmp short .l
.d:
    mov [mc_rend], di
    pop di
    pop si
    pop ax
    ret

mc_echar:
    push di
    mov di, [mc_rend]
    stosb
    mov [mc_rend], di
    pop di
    ret

mc_crlf:
    push ax
    mov al, 13
    call mc_echar
    mov al, 10
    call mc_echar
    pop ax
    ret

; mc_ehex16 - AX as four hex digits onto the report
mc_ehex16:
    push ax
    push cx
    push dx
    mov dx, ax
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol dx, cl                      ; the next nibble to the bottom
    pop cx
    mov al, dl
    call mc_enib
    loop .d
    pop dx
    pop cx
    pop ax
    ret

mc_enib:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 'A' - '9' - 1
.p:
    jmp mc_echar

; mc_two - AL (0..99) as two digits at DS:DI, NUL after; DI on the NUL
mc_two:
    push ax
    aam                             ; AH = tens, AL = units
    add ax, 0x3030
    xchg al, ah
    mov [di], ax
    inc di
    inc di
    mov byte [di], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_onclick - a press that was not a button's: the About card's dismissal or
;              the list's scroll bar
; in:  CX = x, DX = y (screen), SI = window; gfx lock held
; out: CF = 1 the press was ours
; -----------------------------------------------------------------------------
mc_onclick:
    call mc_regeo
    cmp byte [mc_btrec+OS88UI_BT_N], 0
    jne .live
    call mc_abdown
    stc
    ret
.live:
    cmp byte [mc_laid], 0
    je .no
    mov bx, mc_sb
    call os88ui_sbhit               ; AL = the part
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .dn
    cmp al, OS88UI_SBPGUP
    je .pu
    cmp al, OS88UI_SBPGDN
    je .pd
.no:
    clc
    ret
.up:
    mov ax, -1
    jmp short .go
.dn:
    mov ax, 1
    jmp short .go
.pu:
    mov ax, [mc_fit]
    neg ax
    jmp short .go
.pd:
    mov ax, [mc_fit]
.go:
    call mc_scroll
    stc
    ret

; =============================================================================
; 'About Mouse Check' - the standard card (SPEC.md 12.2, 20.5.1). While it is up
; the button record says it has NO buttons, Timer's trick (SPEC.md 111.6): a
; press anywhere reaches mc_onclick and takes it down.
; =============================================================================
mc_about:
    mov byte [mc_btrec+OS88UI_BT_N], 0
    mov bx, si
    mov si, mc_ablines
    jmp os88ui_about

mc_abdown:
    mov byte [mc_btrec+OS88UI_BT_N], MC_NB
    mov si, [mc_win]
    jmp mc_repaint

; --- data ----------------------------------------------------------------------
mc_tpl:     dw 7, 22, 626, 436
            dw mc_ttl, mc_paint, mc_onkey, 0
    OS88_PREFER mc_pref, 626, 436, 712, 330, 632, 300

mc_ttl      equ 16                  ; the header's name field: 'Mouse Check'

mc_s_none:  db 0
mc_dname:   db 'MOUSECHK.TXT', 0
mc_t_copied: db 'Report copied', 0
mc_t_nocopy: db 'Copy failed', 0
mc_t_nomem:  db 'Out of memory', 0
mc_t_saved:  db 'Saved ', 0
mc_t_nosave: db 'Save failed', 0
mc_r_title:  db 'Mouse Check report, os8088 package build ', 0
mc_r_date:   db 'Date ', 0
mc_r_kid:    db 'Kernel id ', 0
mc_r_kok:    db ' (the build this package was made with)', 0
mc_r_knok:   db ' (NOT the build this package was made with: raw values)', 0
mc_r_close:  db '] ', 0
mc_r_indent: db '  ', 0
mc_lnames:   dw mc_n0, mc_n1, mc_n2, mc_n3, mc_n4
mc_n0:       db 'INFO', 0
mc_n1:       db 'OK', 0
mc_n2:       db 'WARN', 0
mc_n3:       db 'PROBLEM', 0
mc_n4:       db 'N/A', 0

mc_ablines:
    dw mc_ab1, mc_ab2, mc_ab3, 0
mc_ab1:     db 'Mouse Check for os8088', 0
mc_ab2:     db 'Release build ', BUILD_STR, 0
mc_ab3:     db 'Is the mouse found, and why not?', 0

%include "mousechk/rows.inc"
%include "mousechk/kread.inc"
%include "mousechk/moblk.inc"
%include "mousechk/summary.inc"
%include "mousechk/serial.inc"
%include "mousechk/ps2.inc"
%include "mousechk/probe.inc"
%include "mousechk/other.inc"
%include "mousechk/ui.inc"

%define DEC_U16
%define DEC_U32
%include "os88dec.inc"

%define OS88UI_ABOUT
%define OS88UI_SCROLL
%define OS88UI_SBUPD
%define OS88UI_NOGLYPH
%include "os88ui.inc"

; --- loader-zeroed bss (SPEC.md 21 step 5) ------------------------------------
; Small things first, the record buffer last: OS88_BSS_ALIGNED puts mc_buf on
; the first 512 boundary past the image and fills the gap in front of it with
; whatever of this list fits (SPEC.md 77.31).
    OS88_ABYTE mc_laid              ; mc_layout has run: the rects are real
    OS88_ABYTE mc_bpp               ; the display's bits per pixel
    OS88_ABYTE mc_kok               ; the kernel is the build this was made with
    OS88_ABYTE mc_pgrey             ; Probe is drawn greyed (FF) or live (00)
    OS88_ABYTE mc_rc                ; characters in the current row
    OS88_ABYTE mc_real              ; the current row is in mc_buf, not mc_scr
    OS88_ABYTE mc_ovf               ; rows were dropped this rebuild
    OS88_AWORD mc_win
    OS88_AWORD mc_cx                ; the content origin and size
    OS88_AWORD mc_cy
    OS88_AWORD mc_cw
    OS88_AWORD mc_ch
    OS88_AWORD mc_pitch             ; the line pitch
    OS88_AWORD mc_gx                ; the list's first cell
    OS88_AWORD mc_gy                ; ...and line
    OS88_AWORD mc_bty               ; the button row's top
    OS88_AWORD mc_fit               ; lines the list shows
    OS88_AWORD mc_cells             ; a line's cells
    OS88_AWORD mc_top               ; the first row shown
    OS88_AWORD mc_nrows             ; rows in the list
    OS88_AWORD mc_bp                ; the next record
    OS88_AWORD mc_rp                ; the current row's NUL
    OS88_AWORD mc_hd                ; the current section's heading lamp byte
    OS88_AWORD mc_rseg              ; the report's transient claim
    OS88_AWORD mc_rend              ; ...and its length so far
    OS88_AWORD mc_ri                ; the report's row
    OS88_ABUF  mc_kid, 6            ; the kernel's 'ID' words
    OS88_ABUF  mc_tmp, 8            ; a decimal number
    OS88_ABUF  mc_name, 14          ; the chosen report name
    OS88_ABUF  mc_lband, 8          ; a 1bpp lamp, inverted
    OS88_ABUF  mc_sb, 20            ; the scroll bar's block + what it drew
    OS88_ABUF  mc_rects, 8 * MC_NB  ; the buttons, screen, inclusive
    OS88_ABUF  mc_bflags, 2 * MC_NB
    OS88_ABUF  mc_shown, 2 * MC_MAXFIT  ; what each screen line last drew
    OS88_ABUF  mc_roff, 2 * MC_MAXR     ; each row's record
    OS88_ABUF  mc_scr, 64           ; where a dropped row is written
    OS88_ABUF  mc_run, 100         ; a run being built
    OS88_BSS_ALIGNED mc_buf, MC_BUFSZ
