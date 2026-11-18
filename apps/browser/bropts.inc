; =============================================================================
; bropts.inc - the Options menu (1.8 step 7, SPEC.md 71.36)
;
; A menu of switches, each item "[x] Name" or "[ ] Name", toggled in place
; (the kernel reads a menu's items when it drops it, SPEC.md 12.2), saved at
; once in SYSTEM/APPDATA/BROWSER.CFG and the page laid out again. No dialog:
; a window of check boxes is part 0's kind of code, and a menu says the same
; in a few hundred resident bytes. The last item sets the home page, which a
; Browser opened with no document goes to.
;
; BROWSER.CFG: 'BO01', the options word, the home page's URL and a NUL.
; =============================================================================

OPT_IMG     equ 1                   ; fetch and draw pictures
OPT_IMGCLK  equ 2                   ; ...or, with that off, on a click on one
OPT_ANIM    equ 4                   ; play animated GIFs
OPT_COLOR   equ 8                   ; the page's colours
OPT_FONT    equ 16                  ; the faces (71.35)
OPT_TABLE   equ 32                  ; compose tables (71.34.2)
OPT_ULINK   equ 64                  ; underline links
OPT_COOKIE  equ 128                 ; keep and send the session cookie
OPT_CACHE   equ 256                 ; keep pictures in BRCACHE (71.27)
OPT_N       equ 9                   ; switches, then Set Home Page
BR_OCFGSZ   equ 6 + BR_UBUF         ; the file: magic, options, URL

; --- br_optinit - the window's start: the defaults, before the file -------
br_optinit:
    push ax
    mov ax, OPT_IMG | OPT_ANIM | OPT_COLOR | OPT_TABLE | OPT_ULINK \
            | OPT_COOKIE | OPT_CACHE
    cmp byte [br_fon], 0            ; br_fninit's CPU answer is the default
    je .f
    or ax, OPT_FONT
.f:
    mov [br_opts], ax
    mov word [br_ocfg + 6], 0       ; no home page
    mov byte [br_optread], 0
    call br_optapply
    pop ax
    ret

; --- br_optwake - br_onwake's first pass: the file, then the home page ------
br_optwake:
    cmp byte [br_optread], 0
    jne .ret
    mov byte [br_optread], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    mov bx, br_ocfg
    mov cx, BR_OCFGSZ
    xor dx, dx
    mov si, br_s_optfile
    call od_read
    jc .apply                       ; none: the defaults stand
    cmp ax, 6
    jb .apply
    cmp word [br_ocfg], 'BO'
    jne .apply
    cmp word [br_ocfg + 2], '01'
    jne .apply
    mov ax, [br_ocfg + 4]
    mov [br_opts], ax
    mov byte [br_ocfg + BR_OCFGSZ - 1], 0
.apply:
    call br_optapply
    cmp byte [br_argp], 0           ; a document was asked for: it, not home
    jne .out
    cmp byte [br_ocfg + 6], 0
    je .out
    mov si, br_ocfg + 6             ; THE HOME PAGE
    mov di, br_ubuf
    cld
.cp:
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .cp
    mov si, br_ubuf
    call br_go
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; --- br_optapply - the options word into what reads it, and the menu ------
br_optapply:
    push ax
    push bx
    push cx
    push si
    mov ax, [br_opts]
    xor bl, bl
    test al, OPT_FONT
    jz .f
    inc bl
.f:
    mov [br_fon], bl
    mov bl, LM_UNDER
    test al, OPT_ULINK
    jz .u
    or bl, LM_LINK
.u:
    mov [br_ulmask], bl
    mov si, br_i_opts               ; each switch's mark: "[x]" or "[ ]"
    mov cx, OPT_N
    mov bx, 1
.m:
    push si
    mov si, [si]
    mov byte [si + 1], ' '
    test ax, bx
    jz .n
    mov byte [si + 1], 'x'
.n:
    pop si
    inc si
    inc si
    shl bx, 1
    loop .m
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- br_optcmd - Options > item AL: part 0's (brcold.inc's br_cx_optcmd,
; 1.8's size pass) - a menu click, and the file write follows anyway
br_optcmd:
    mov byte [br_dl_sel], BRDL_OPTCMD
    jmp br_dl_cold

; --- br_imgclick - a click's hit is a picture's box: with Images off and
; Images on Click on, the page again with its pictures. CF=0 taken
br_imgclick:
    cmp byte [br_hiflag], 0
    je .no
    test word [br_opts], OPT_IMG
    jnz .no
    test word [br_opts], OPT_IMGCLK
    jz .no
    mov byte [br_imgforce], 2       ; the next load's pictures are fetched
    call br_okrel
    jc .no
    call br_reload
    clc
    ret
.no:
    stc
    ret

; --- br_imgallow - br_imgtab: CF=1 this page's pictures are not fetched ----
br_imgallow:
    test word [br_opts], OPT_IMG
    jnz .yes
    cmp byte [br_imgforce], 0
    jne .yes
    stc
    ret
.yes:
    clc
    ret

; --- br_imgnav - br_go: a load. A forced one keeps its pictures once --------
br_imgnav:
    cmp byte [br_imgforce], 2
    jne .clear
    mov byte [br_imgforce], 1
    ret
.clear:
    mov byte [br_imgforce], 0
    ret

br_s_optfile: db 'BROWSER.CFG', 0
br_m_opts:  db 'Options', 0
br_i_opts:  dw br_io_img, br_io_clk, br_io_anim, br_io_col, br_io_font
            dw br_io_tab, br_io_ul, br_io_ck, br_io_cache, br_io_home
            dw br_it_shortcuts
br_io_img:  db '[x] Images', 0
br_io_clk:  db '[ ] Images on Click', 0
br_io_anim: db '[x] Animations', 0
br_io_col:  db '[x] Colors', 0
br_io_font: db '[x] Fonts', 0
br_io_tab:  db '[x] Tables', 0
br_io_ul:   db '[x] Underline Links', 0
br_io_ck:   db '[x] Cookies', 0
br_io_cache: db '[x] Cache', 0
br_io_home: db 'Set Home Page', 0

; br_ultest - ZF=0 the cell at [BX+DI] is underlined (a link, if Options
; says so, or <u>). Every register and the other flags as they were
br_ultest:
    push ax
    mov al, [br_ulmask]
    test [bx+di], al
    pop ax
    ret
