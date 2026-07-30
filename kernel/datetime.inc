; =============================================================================
; os8088 - datetime.inc
;
; System wall clock. At boot, start from the defined no-RTC fallback
; (2026-07-04 00:00:00), then ask the BIOS for an AT-compatible hardware RTC
; date/time. Valid RTC data replaces the fallback; unsupported BIOS services
; or malformed BCD leave the software clock in place. Thereafter the PIT tick
; count advances the calendar. Control Panel edits are applied immediately to
; the software state and post dt_pending; ui_task performs the potentially
; blocking BIOS write later, outside the graphics lock.
;
; All stored fields are binary. The BIOS boundary alone converts BCD.
; =============================================================================

DT_BAR_HIT_X equ 560            ; rightmost 80px are the clock click target
DT_BAR_X     equ 568            ; "HH:MM:SS" text origin (64px wide)

DT_YEAR  equ 0
DT_MONTH equ 1
DT_DAY   equ 2
DT_HOUR  equ 3
DT_MIN   equ 4
DT_SEC   equ 5

dt_month_days:
    db 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31

dt_bar_str: db '00:00:00', 0

; -----------------------------------------------------------------------------
; dt_init - seed fallback, then load and validate a hardware RTC if present
; in/out: nothing; preserves all registers
; -----------------------------------------------------------------------------
dt_init:
    push ax
    call dt_fallback
    call dt_rtc_read
    mov ax, [ticks]
    mov [dt_last_tick], ax
    mov word [dt_acc], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; dt_fallback - install the required no-RTC date
; -----------------------------------------------------------------------------
dt_fallback:
    mov word [dt_year], 2026
    mov byte [dt_month], 7
    mov byte [dt_day], 4
    mov byte [dt_hour], 0
    mov byte [dt_minute], 0
    mov byte [dt_second], 0
    mov byte [dt_rtc_present], 0
    mov byte [dt_pending], 0
    mov word [dt_gen], 1
    mov word [dt_bar_seen], 0
    ret

; -----------------------------------------------------------------------------
; dt_update - advance the software calendar from the 18.2Hz PIT tick count
; Preserves every register. BIOS is never called.
; -----------------------------------------------------------------------------
dt_update:
    push ax
    push bx
    push cx
    push dx

    mov ax, [ticks]
    mov bx, ax
    sub ax, [dt_last_tick]      ; wrap-safe delta
    mov [dt_last_tick], bx
    or ax, ax
    jz .done

    mov dx, 10
    mul dx                      ; tenths of a tick, like the Clock app
    add ax, [dt_acc]
    adc dx, 0
    mov cx, 182                 ; 18.2 ticks per second
    div cx
    mov [dt_acc], dx
    mov cx, ax                  ; whole seconds owed
.second:
    jcxz .done
    call dt_advance_one
    inc word [dt_gen]
    loop .second

.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dt_advance_one - add one second with full calendar carry
; -----------------------------------------------------------------------------
dt_advance_one:
    inc byte [dt_second]
    cmp byte [dt_second], 60
    jb .done
    mov byte [dt_second], 0
    inc byte [dt_minute]
    cmp byte [dt_minute], 60
    jb .done
    mov byte [dt_minute], 0
    inc byte [dt_hour]
    cmp byte [dt_hour], 24
    jb .done
    mov byte [dt_hour], 0
    inc byte [dt_day]
    call dt_days
    cmp byte [dt_day], al
    jbe .done
    mov byte [dt_day], 1
    inc byte [dt_month]
    cmp byte [dt_month], 12
    jbe .done
    mov byte [dt_month], 1
    inc word [dt_year]
.done:
    ret

; -----------------------------------------------------------------------------
; dt_days - days in the current month, Gregorian leap-year rules
; out: AL = 28..31; preserves every other register
; -----------------------------------------------------------------------------
dt_days:
    push bx
    push cx
    push dx
    push si

    xor ax, ax
    mov al, [dt_month]
    dec ax
    mov si, ax
    mov cl, [dt_month_days + si]
    cmp byte [dt_month], 2
    jne .answer

    mov ax, [dt_year]
    xor dx, dx
    mov bx, 4
    div bx
    or dx, dx
    jnz .answer

    mov ax, [dt_year]
    xor dx, dx
    mov bx, 100
    div bx
    or dx, dx
    jnz .leap

    mov ax, [dt_year]
    xor dx, dx
    mov bx, 400
    div bx
    or dx, dx
    jnz .answer
.leap:
    inc cl
.answer:
    mov al, cl
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dt_adjust - change one field from the Date & Time page
; in: AL = DT_* field (0..5), BL = 0 decrement / nonzero increment
; out: nothing; preserves every register; posts dt_pending for ui_task
; -----------------------------------------------------------------------------
dt_adjust:
    push ax
    push bx
    push cx
    push dx
    push si

    mov dl, al                  ; selected field
    mov dh, bl                  ; direction
    call dt_update
    cmp dl, DT_YEAR
    je .year
    cmp dl, DT_MONTH
    je .month
    cmp dl, DT_DAY
    je .day
    cmp dl, DT_HOUR
    je .hour
    cmp dl, DT_MIN
    je .minute
    cmp dl, DT_SEC
    jne .done

.second:
    mov si, dt_second
    mov cl, 59
    jmp .byte
.minute:
    mov si, dt_minute
    mov cl, 59
    jmp .byte
.hour:
    mov si, dt_hour
    mov cl, 23
.byte:
    or dh, dh
    jz .byte_dec
    cmp [si], cl
    jae .byte_wrap_zero
    inc byte [si]
    jmp .changed
.byte_wrap_zero:
    mov byte [si], 0
    jmp .changed
.byte_dec:
    cmp byte [si], 0
    je .byte_wrap_max
    dec byte [si]
    jmp .changed
.byte_wrap_max:
    mov [si], cl
    jmp .changed

.year:
    or dh, dh
    jz .year_dec
    cmp word [dt_year], 2099
    jae .year_min
    inc word [dt_year]
    jmp .clamp
.year_min:
    mov word [dt_year], 1980
    jmp .clamp
.year_dec:
    cmp word [dt_year], 1980
    jbe .year_max
    dec word [dt_year]
    jmp .clamp
.year_max:
    mov word [dt_year], 2099
    jmp .clamp

.month:
    or dh, dh
    jz .month_dec
    cmp byte [dt_month], 12
    jae .month_min
    inc byte [dt_month]
    jmp .clamp
.month_min:
    mov byte [dt_month], 1
    jmp .clamp
.month_dec:
    cmp byte [dt_month], 1
    jbe .month_max
    dec byte [dt_month]
    jmp .clamp
.month_max:
    mov byte [dt_month], 12
.clamp:
    call dt_days
    cmp byte [dt_day], al
    jbe .changed
    mov [dt_day], al
    jmp .changed

.day:
    call dt_days                ; AL = current month's maximum
    or dh, dh
    jz .day_dec
    cmp [dt_day], al
    jae .day_min
    inc byte [dt_day]
    jmp .changed
.day_min:
    mov byte [dt_day], 1
    jmp .changed
.day_dec:
    cmp byte [dt_day], 1
    jbe .day_max
    dec byte [dt_day]
    jmp .changed
.day_max:
    mov [dt_day], al

.changed:
    inc word [dt_gen]
    mov byte [dt_pending], 1
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dt_bar_draw - draw the top-right HH:MM:SS field (gfx lock already held)
; -----------------------------------------------------------------------------
dt_bar_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call dt_update
    mov di, dt_bar_str
    mov al, [dt_hour]
    call dt_put2
    mov byte [di], ':'
    inc di
    mov al, [dt_minute]
    call dt_put2
    mov byte [di], ':'
    inc di
    mov al, [dt_second]
    call dt_put2
    mov byte [di], 0

    mov byte [gfx_color], CWHITE
    mov ax, DT_BAR_HIT_X
    xor bx, bx
    mov cx, SCREEN_W - 1
    mov dx, MBAR_H - 2
    call gfx_fill

    mov byte [gfx_color], CBLACK
    mov ax, DT_BAR_HIT_X
    xor bx, bx
    mov dx, MBAR_H - 2
    call gfx_vline
    mov cx, DT_BAR_X
    mov dx, MENU_TEXT_Y
    mov si, dt_bar_str
    call font_str

    mov ax, [dt_gen]
    mov [dt_bar_seen], ax

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dt_bar_refresh - update/redraw the bar only when wall-clock state changed
; Called by ui_task with no lock held; preserves every register.
; -----------------------------------------------------------------------------
dt_bar_refresh:
    push ax
    call dt_update
    mov ax, [dt_gen]
    cmp ax, [dt_bar_seen]
    je .done
    call gfx_lock
    call dt_bar_draw
    call gfx_unlock
.done:
    pop ax
    ret

; dt_put2 - AL binary 0..99 -> two ASCII digits at DS:DI; advances DI by 2
dt_put2:
    aam
    add ax, 0x3030
    mov [di], ah
    mov [di + 1], al
    add di, 2
    ret

; -----------------------------------------------------------------------------
; BIOS RTC boundary
; -----------------------------------------------------------------------------

; dt_rtc_read - BIOS int 1Ah date/time load; fallback remains on any failure
dt_rtc_read:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    stc
    mov ah, 0x04
    int 0x1A
    mov ax, KERNEL_SEG          ; BIOS calls may not be trusted to retain DS
    mov ds, ax                  ; (MOV leaves the returned carry flag intact)
    jc .fail
    mov [dt_raw_century], ch
    mov [dt_raw_year], cl
    mov [dt_raw_month], dh
    mov [dt_raw_day], dl

    stc
    mov ah, 0x02
    int 0x1A
    mov ax, KERNEL_SEG
    mov ds, ax
    jc .fail
    mov [dt_raw_hour], ch
    mov [dt_raw_minute], cl
    mov [dt_raw_second], dh

    mov al, [dt_raw_century]
    call dt_bcd2bin
    jc .fail
    cmp al, 19
    jb .fail
    cmp al, 20
    ja .fail
    mov [dt_tmp_century], al

    mov al, [dt_raw_year]
    call dt_bcd2bin
    jc .fail
    mov [dt_tmp_year], al
    xor ah, ah
    mov bx, ax
    mov al, [dt_tmp_century]
    xor ah, ah
    mov dx, 100
    mul dx
    add ax, bx
    cmp ax, 1980
    jb .fail
    cmp ax, 2099
    ja .fail
    mov [dt_year], ax

    mov al, [dt_raw_month]
    call dt_bcd2bin
    jc .fail
    cmp al, 1
    jb .fail
    cmp al, 12
    ja .fail
    mov [dt_month], al

    mov al, [dt_raw_day]
    call dt_bcd2bin
    jc .fail
    cmp al, 1
    jb .fail
    mov [dt_day], al
    call dt_days
    cmp [dt_day], al
    ja .fail

    mov al, [dt_raw_hour]
    call dt_bcd2bin
    jc .fail
    cmp al, 23
    ja .fail
    mov [dt_hour], al

    mov al, [dt_raw_minute]
    call dt_bcd2bin
    jc .fail
    cmp al, 59
    ja .fail
    mov [dt_minute], al

    mov al, [dt_raw_second]
    call dt_bcd2bin
    jc .fail
    cmp al, 59
    ja .fail
    mov [dt_second], al
    mov byte [dt_rtc_present], 1
    jmp .done

.fail:
    call dt_fallback
.done:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dt_rtc_write - write current state through BIOS if the RTC was detected
; Called by ui_task only, with no graphics lock held.
dt_rtc_write:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    mov byte [dt_pending], 0
    cmp byte [dt_rtc_present], 0
    je .done

    mov ax, [dt_year]
    xor dx, dx
    mov bx, 100
    div bx                      ; AX = century, DX = year within century
    call dt_bin2bcd
    mov [dt_raw_century], al
    mov al, dl
    call dt_bin2bcd
    mov [dt_raw_year], al
    mov al, [dt_month]
    call dt_bin2bcd
    mov [dt_raw_month], al
    mov al, [dt_day]
    call dt_bin2bcd
    mov [dt_raw_day], al

    mov ch, [dt_raw_century]
    mov cl, [dt_raw_year]
    mov dh, [dt_raw_month]
    mov dl, [dt_raw_day]
    stc
    mov ah, 0x05
    int 0x1A
    mov ax, KERNEL_SEG
    mov ds, ax
    jc .failed

    mov al, [dt_hour]
    call dt_bin2bcd
    mov [dt_raw_hour], al
    mov al, [dt_minute]
    call dt_bin2bcd
    mov [dt_raw_minute], al
    mov al, [dt_second]
    call dt_bin2bcd
    mov [dt_raw_second], al

    mov ch, [dt_raw_hour]
    mov cl, [dt_raw_minute]
    mov dh, [dt_raw_second]
    xor dl, dl                  ; standard time / no daylight flag
    stc
    mov ah, 0x03
    int 0x1A
    mov ax, KERNEL_SEG
    mov ds, ax
    jnc .done
.failed:
    mov byte [dt_rtc_present], 0
    inc word [dt_gen]           ; refresh the panel/bar status on next paint
.done:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dt_bcd2bin - AL packed BCD -> AL binary, CF=1 malformed
dt_bcd2bin:
    push bx
    push cx
    mov bl, al
    and bl, 0x0F
    cmp bl, 9
    ja .bad
    mov cl, 4
    shr al, cl
    cmp al, 9
    ja .bad
    mov bh, 10
    mul bh
    add al, bl
    clc
    jmp .out
.bad:
    stc
.out:
    pop cx
    pop bx
    ret

; dt_bin2bcd - AL binary 0..99 -> AL packed BCD
dt_bin2bcd:
    push bx
    push cx
    aam
    mov bl, al
    mov al, ah
    mov cl, 4
    shl al, cl
    or al, bl
    pop cx
    pop bx
    ret

section .bss

dt_year:        resw 1
dt_month:       resb 1
dt_day:         resb 1
dt_hour:        resb 1
dt_minute:      resb 1
dt_second:      resb 1
dt_rtc_present: resb 1
dt_pending:     resb 1
dt_last_tick:   resw 1
dt_acc:         resw 1
dt_gen:         resw 1
dt_bar_seen:    resw 1

dt_raw_century: resb 1
dt_raw_year:    resb 1
dt_raw_month:   resb 1
dt_raw_day:     resb 1
dt_raw_hour:    resb 1
dt_raw_minute:  resb 1
dt_raw_second:  resb 1
dt_tmp_century: resb 1
dt_tmp_year:    resb 1

section .text
