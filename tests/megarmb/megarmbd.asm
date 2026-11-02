; =============================================================================
; os8088 - tests/megarmb/megarmbd.asm
;
; MEGARMBD, a TEST instrument (nothing under tests/ ships): kern_mega's
; REAL-MODE BRACKET ON AN EXTENDED DESKTOP (SPEC.md 53.2.3's W7 paragraph,
; docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W7). With EXTD.DRV loaded the bracket
; used to fall back to protected mode (FSXR_EXTD); it runs real now, EXTD.DRV
; from a low copy, and a fullscreen bracket may set a mode on EITHER display.
; Its results are in its own bss for tests/megarmbext.py (86Box, which has no
; port E9 but reads guest memory). OS88_HIGH, so its region is above 1MB and
; the bracket runs a COPY of it below as well (W1).
;
; Its window's timer starts it once. Two legs, each OSAPI_FSX_RUN with
; FSXF_REAL and SI = FSXR_REAL:
;
;   0  CH = 1, display 0 (the primary, a VGA): OSAPI_FSX_MODE FSXM_VGA13 -
;      EXTD.DRV's EXTF_KIND and EXTF_ENTER, run in real mode - its whole
;      64,000-byte framebuffer filled with colour MD_C13, then MD_LOOK ticks
;      of halting for the harness to look at both monitors
;   1  CH = 2, display 1 (the secondary, a Hercules): FSXM_HERC, its 32 KB
;      page filled with FFh, the same wait
;
; Each leg's record (MD_RSZ bytes): cf (FFh refused), the SI it came back
; with, OSAPI_CPU_INFO's AH inside, OSAPI_FSX_MODE's CF (FFh refused), the
; FSI block's segment and width, a mark, and `phase` - 1 while the entry
; holds, 2 once it returned - which is what the harness waits on.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARMBD', md_entry, 0, OS88_STACK_192 | OS88_HIGH

MD_MARK     equ 0xD15C
MD_WAIT     equ 36                  ; the timer: two seconds after launch
MD_LOOK     equ 110                 ; each leg's six seconds inside
MD_C13      equ 0x0C                ; mode 13h's colour 12: light red

md_entry:
    mov si, md_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [md_win], bx
    mov ax, md_tick
    call OSAPI_WM_ONTIMER
    mov ax, MD_WAIT
    call OSAPI_WM_TIMER
    clc
.out:
    ret

md_paint:
    ret

; the timer: under the gfx lock, a window callback - the bracket's context
md_tick:
    cmp byte [md_ran], 0
    je .go
    ret
.go:
    mov byte [md_ran], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov di, md_r0
    mov byte [md_id], FSXM_VGA13
    mov cx, FSXF_REAL + 0x0100      ; CH = 1: display 0
    call md_run
    mov di, md_r1
    mov byte [md_id], FSXM_HERC
    mov cx, FSXF_REAL + 0x0200      ; CH = 2: display 1
    call md_run
    mov byte [md_fin], 1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; md_run - CX = the flags and the display, DI = the leg's record
md_run:
    mov [md_cur], di
    mov bx, [md_win]
    mov ax, md_main
    xor si, si                      ; FSXR_REAL in
    xor bp, bp                      ; no FSXL_ list
    call OSAPI_FSX_RUN
    sbb dl, dl
    mov di, [md_cur]
    mov [di+MDR_CF], dl
    mov ax, si
    mov [di+MDR_WHY], al
    mov byte [di+MDR_PHASE], 2
    ret

; the entry: the mode, the fill, the wait
md_main:
    mov bx, [md_cur]
    call OSAPI_CPU_INFO
    mov [bx+MDR_FEAT], ah
    mov al, [md_id]
    push ds
    pop es
    mov di, md_fsi
    call OSAPI_FSX_MODE
    sbb al, al
    mov [bx+MDR_MCF], al
    jnz .wait
    mov ax, [md_fsi+FSI_SEG]
    mov [bx+MDR_SEG], ax
    mov ax, [md_fsi+FSI_W]
    mov [bx+MDR_W], ax
    mov es, [md_fsi+FSI_SEG]
    xor di, di
    cld
    cmp byte [md_id], FSXM_HERC
    je .herc
    mov ax, MD_C13 * 0x0101
    mov cx, 32000                   ; 320 x 200 bytes
    rep stosw
    jmp short .wait
.herc:
    mov ax, 0xFFFF
    mov cx, 16384                   ; the 32 KB page: four banks of 90-byte rows
    rep stosw
.wait:
    mov word [bx+MDR_MARK], MD_MARK
    mov byte [bx+MDR_PHASE], 1
    call OSAPI_GET_TICKS
    mov si, ax
.w:
    sti
    hlt
    call OSAPI_GET_TICKS
    sub ax, si
    cmp ax, MD_LOOK
    jb .w
    ret

md_tpl:
    dw 8, 300, 200, 40
    dw md_ttl, md_paint, 0, 0
md_ttl:     db 'MegaRMB two displays', 0

; THE RESULT BLOCK, in the bss at os88_image_end (the .bin header's image
; size), which tests/megarmbext.py reads
    OS88_BSS MD_RESN + FSI_SIZE
    OS88_IMAGE_END

MDR_CF      equ 0                   ; the leg's record: cf
MDR_WHY     equ 1                   ; ...SI out
MDR_FEAT    equ 2                   ; ...CPU_INFO's AH inside
MDR_MCF     equ 3                   ; ...FSX_MODE's CF
MDR_SEG     equ 4                   ; ...FSI_SEG
MDR_W       equ 6                   ; ...FSI_W
MDR_MARK    equ 8                   ; ...the mark
MDR_PHASE   equ 10                  ; ...1 inside, 2 returned
MD_RSZ      equ 12

md_res      equ os88_image_end
md_fin      equ md_res + 0          ; byte: both legs done
md_ran      equ md_res + 1
md_id       equ md_res + 2
md_win      equ md_res + 4
md_cur      equ md_res + 6
md_r0       equ md_res + 8
md_r1       equ md_r0 + MD_RSZ
md_fsi      equ md_r1 + MD_RSZ
MD_RESN     equ md_fsi - md_res
