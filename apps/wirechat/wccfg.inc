; =============================================================================
; os8088 - apps/wirechat/wccfg.inc
;
; WIRECHAT.CFG (SPEC.md 113.8): SYSTEM\APPDATA on our own volume, through
; apps/os88data.inc. Lines of `key value`:
;
;   host os8088.com:80/chat/      SPEC.md 92.4's host[:port][/prefix/]
;   nick George
;   notify 1
;   friend Alin                   (up to WC_NFR of them)
;   code 0f1e...                  an owned nick's code (SPEC.md 113.2.2)
;
; Read in the first wake, written whole whenever the nick, the friends or the
; notification switch change - by the wake handler, because a file cell is the
; UI task's and not a callback's (SPEC.md 20.6 rule 7; wc_wantsave asks for
; the wake). A missing file, or a missing folder, is the defaults and nothing
; said: the first run is the ordinary case.
; =============================================================================

; --- wc_cfgdefault - os8088.com:80/chat/ (the entry proc) ---------------------
wc_cfgdefault:
    mov si, wc_def_host
    mov di, wc_host
    call wc_strcpy
    mov si, wc_def_pfx
    mov di, wc_pfx
    call wc_strcpy
    mov word [wc_port], 80
    ret

; -----------------------------------------------------------------------------
; wc_cfgload - read the file and take what it says. Preserves ES.
; -----------------------------------------------------------------------------
wc_cfgload:
    push es
    push ds
    pop es
    mov si, wc_cfgname
    mov bx, wc_cfgb
    xor dx, dx
    mov cx, WC_CFGMAX
    call od_read                        ; CF = 0, DX:AX = the bytes
    jc .out
    or dx, dx
    jnz .out
    cmp ax, WC_CFGMAX
    ja .out
    mov bx, ax
    mov byte [wc_cfgb+bx], 0
    mov si, wc_cfgb
.line:
    cmp byte [si], 0
    je .out
    call wc_cfgline                     ; SI = the next line
    jmp short .line
.out:
    call wc_menulabels
    pop es
    ret

; --- wc_cfgline - one `key value` line at SI; out SI = the next ------------
wc_cfgline:
    call wc_lineend                     ; DI = the next; this one NUL-ended
    push di
    mov bx, si                          ; CR, if the file has them, goes too
.cr:
    mov al, [bx]
    or al, al
    jz .key
    cmp al, 13
    jne .cr1
    mov byte [bx], 0
    jmp short .key
.cr1:
    inc bx
    jmp short .cr
.key:
    mov di, wc_k_host
    call wc_pfxci
    jnc .host
    mov di, wc_k_nick
    call wc_pfxci
    jnc .nick
    mov di, wc_k_notify
    call wc_pfxci
    jnc .notify
    mov di, wc_k_friend
    call wc_pfxci
    jnc .friend
    mov di, wc_k_code
    call wc_pfxci
    jnc .code
    jmp short .done
.code:
    call wc_skipsp
    mov bx, si
    mov di, wc_key
    mov cx, WC_KEYN
    call wc_ncopy
    jmp short .done
.host:
    call wc_hostparse
    jmp short .done
.nick:
    call wc_skipsp
    push si
    call wc_nickok
    pop si
    jc .done
    mov bx, si
    mov di, wc_nick
    mov cx, WC_NICKN
    call wc_ncopy
    jmp short .done
.notify:
    call wc_skipsp
    mov al, [si]
    sub al, '0'
    and al, 1
    mov [wc_notify], al
    jmp short .done
.friend:
    call wc_skipsp
    cmp byte [wc_fn], WC_NFR
    jae .done
    push si
    call wc_nickok
    pop si
    jc .done
    call wc_frfind                      ; once each
    jnc .done
    mov bx, si
    mov al, [wc_fn]
    call wc_frrec
    mov di, si
    mov cx, WC_NICKN
    call wc_ncopy
    inc byte [wc_fn]
.done:
    pop si
    ret

; --- wc_skipsp - SI past spaces ----------------------------------------------
wc_skipsp:
    cmp byte [si], ' '
    jne .out
    inc si
    jmp short wc_skipsp
.out:
    ret

; -----------------------------------------------------------------------------
; wc_hostparse - `host[:port][/prefix/]` at SI (SPEC.md 92.4, The Wire's
;                wr_cfgparse): the host stops at a space, ':' or '/'; a port of
;                0 or none is 80; a prefix begins with '/' and is given a
;                trailing one
; -----------------------------------------------------------------------------
wc_hostparse:
    call wc_skipsp
    mov di, wc_host
    mov cx, WC_HOSTMAX - 1
.h:
    mov al, [si]
    cmp al, ' '
    jbe .hend
    cmp al, ':'
    je .hend
    cmp al, '/'
    je .hend
    jcxz .hskip
    mov [di], al
    inc di
    dec cx
.hskip:
    inc si
    jmp short .h
.hend:
    mov byte [di], 0
    cmp di, wc_host                     ; no host: the default's
    jne .port
    push si
    mov si, wc_def_host
    mov di, wc_host
    call wc_strcpy
    pop si
.port:
    mov word [wc_port], 80
    cmp byte [si], ':'
    jne .pfx
    inc si
    call wc_dec32
    or dx, dx
    jnz .pfx
    or ax, ax
    jz .pfx
    mov [wc_port], ax
.pfx:
    cmp byte [si], '/'
    jne .out
    mov di, wc_pfx
    mov cx, WC_PFXMAX - 2               ; room for the trailing '/' and NUL
.p:
    mov al, [si]
    cmp al, ' '
    jbe .pend
    jcxz .pskip
    mov [di], al
    inc di
    dec cx
.pskip:
    inc si
    jmp short .p
.pend:
    cmp byte [di-1], '/'
    je .pz
    mov byte [di], '/'
    inc di
.pz:
    mov byte [di], 0
.out:
    ret

; -----------------------------------------------------------------------------
; wc_cfgsave - write the file whole: host, nick, notify, friends. Preserves ES.
; -----------------------------------------------------------------------------
wc_cfgsave:
    push es
    mov di, wc_cfgb
    mov bx, wc_cfgb + WC_CFGMAX - 1
    mov si, wc_k_host
    call wc_put
    mov al, ' '
    call wc_putc
    mov si, wc_host
    call wc_put
    mov al, ':'
    call wc_putc
    mov ax, [wc_port]
    xor dx, dx
    call wc_putd32
    mov si, wc_pfx
    call wc_put
    call wc_putnl
    cmp byte [wc_nick], 0
    je .nonick
    mov si, wc_k_nick
    call wc_put
    mov al, ' '
    call wc_putc
    mov si, wc_nick
    call wc_put
    call wc_putnl
    cmp byte [wc_key], 0
    je .nonick
    mov si, wc_k_code
    call wc_put
    mov al, ' '
    call wc_putc
    mov si, wc_key
    call wc_put
    call wc_putnl
.nonick:
    mov si, wc_k_notify
    call wc_put
    mov al, ' '
    call wc_putc
    mov al, [wc_notify]
    add al, '0'
    call wc_putc
    call wc_putnl
    xor ax, ax
.fr:
    cmp al, [wc_fn]
    jae .write
    push ax
    push si
    mov si, wc_k_friend
    call wc_put
    mov al, ' '
    call wc_putc
    pop si
    pop ax
    push ax
    call wc_frrec
    call wc_put
    call wc_putnl
    pop ax
    inc ax
    jmp short .fr
.write:
    mov cx, di
    sub cx, wc_cfgb
    push ds
    pop es
    mov si, wc_cfgname
    mov bx, wc_cfgb
    mov di, wc_cfgst
    call od_write                       ; CF = 1: no APPDATA, a write-protected
    pop es                              ; disk - the settings last the session
    ret

; --- wc_putnl - CR LF, so the file reads in any editor on the disk ----------
wc_putnl:
    mov al, 13
    call wc_putc
    mov al, 10
    jmp wc_putc
