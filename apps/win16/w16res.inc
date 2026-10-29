; =============================================================================
; os8088 - apps/win16/w16res.inc
;
; RESOURCES, READ FROM THE FILE WHEN LOADED (SPEC.md 124.10). The loader keeps
; the NE resource table (w16_copyres) in the program's claim; a resource is
; read with OSAPI_FILE_READ_AT when LoadResource asks for it, into a moveable
; global block, and cached in its own NAMEINFO's handle and usage words, which
; is what Windows used them for. Shaped after Wine's krnl386 resource.c and
; ne_module.c's NE_FindResource.
;
; The file is RAW on the disk: DOS.O88 hands WIN16 only a file whose raw
; first cluster reads MZ and NE (SPEC.md 124.3), so READ_AT's on-disk bytes
; are the program's own.
; =============================================================================

NE_RSRCTAB equ 0x24             ; the NE header's resource table...
NE_RESTAB  equ 0x26             ; ...and the resident-names table after it

RN_OFF    equ 0                 ; a NAMEINFO, 12 bytes, in the kept table
RN_LEN    equ 2
RN_FLAGS  equ 4
RN_ID     equ 6
RN_HANDLE equ 8
RN_USAGE  equ 10
RN_SIZE   equ 12

RT_ICON        equ 3
RT_MENU        equ 4
RT_DIALOG      equ 5
RT_STRING      equ 6
RT_ACCELERATOR equ 9

; -----------------------------------------------------------------------------
; w16_rlayout - the paragraphs the kept table needs; in ES = the file
; out: AX = paragraphs (0: none), [w16_rtlen] = its bytes. Keeps the rest.
; -----------------------------------------------------------------------------
w16_rlayout:
    push si
    mov si, [w16_ne]
    mov ax, [es:si + NE_RESTAB]
    sub ax, [es:si + NE_RSRCTAB]
    jbe .none
    cmp ax, 0x8000                  ; a table that size is damage, not a table
    jae .none
    mov [w16_rtlen], ax
    add ax, 15
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    pop si
    ret
.none:
    xor ax, ax
    mov [w16_rtlen], ax
    pop si
    ret

; -----------------------------------------------------------------------------
; w16_copyres - the table into [w16_rtab], its handle and usage words zeroed.
; in: ES = the file; CF=1 SI = a reason (it runs past the file)
; -----------------------------------------------------------------------------
w16_copyres:
    mov cx, [w16_rtlen]
    jcxz .ok
    push ds
    push es
    push di
    mov si, [w16_ne]
    mov ax, [es:si + NE_RSRCTAB]
    add ax, si
    mov si, ax
    add ax, cx
    jc .bad
    cmp ax, [w16_fend]
    ja .bad
    mov ax, [w16_rtab]
    push es
    pop ds                          ; DS:SI = the file's table
    mov es, ax
    xor di, di
    cld
    rep movsb
    pop di
    pop es
    pop ds
    push es                         ; every entry's cache words to zero
    mov es, [w16_rtab]
    mov si, 2
.type:
    cmp word [es:si], 0
    je .done
    mov cx, [es:si + 2]
    add si, 8
.ent:
    jcxz .type
    mov word [es:si + RN_HANDLE], 0
    mov word [es:si + RN_USAGE], 0
    add si, RN_SIZE
    cmp si, [w16_rtlen]
    jae .done
    dec cx
    jmp short .ent
.done:
    pop es
.ok:
    clc
    ret
.bad:
    pop di
    pop es
    pop ds
    mov si, w16_r_trunc
    stc
    ret

; -----------------------------------------------------------------------------
; w16_rkey - a far type or name, DX:AX -> a key: AX = 8000h | id for an
; integer (MAKEINTRESOURCE, or a string "#123"), else AX = 0 and the name in
; DI's 16-byte buffer, upper case. Keeps the rest.
; -----------------------------------------------------------------------------
w16_rkey:
    push bx
    push cx
    push si
    push es
    or dx, dx
    jz .int
    mov es, dx
    mov si, ax
    cmp byte [es:si], '#'
    jne .str
    xor ax, ax                      ; "#123": the integer
    mov cx, 10
.dig:
    inc si
    mov bl, [es:si]
    sub bl, '0'
    cmp bl, 9
    ja .num
    push dx
    mul cx
    pop dx
    xor bh, bh
    add ax, bx
    jmp short .dig
.num:
.int:
    or ah, 0x80
    jmp short .out
.str:
    mov cx, 15
.ch:
    mov al, [es:si]
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 0x20
.up:
    mov [di], al
    or al, al
    jz .end
    inc si
    inc di
    loop .ch
    mov byte [di], 0
.end:
    xor ax, ax
.out:
    pop es
    pop si
    pop cx
    pop bx
    ret

; w16_rmatch - does table word AX (an rtTypeID or rnID) match key BX (with
; string SI)? ZF=1 yes. ES = the table. Keeps all.
w16_rmatch:
    push ax
    push cx
    push si
    push di
    or bx, bx
    jz .str
    cmp ax, bx
    jmp short .out
.str:
    test ah, 0x80
    jnz .no                         ; an integer never matches a string
    mov di, ax                      ; a pascal string at that offset
    cmp di, [w16_rtlen]
    jae .no
    mov cl, [es:di]
    xor ch, ch
.c:
    inc di
    jcxz .tail
    mov al, [es:di]
    cmp al, 'a'
    jb .u
    cmp al, 'z'
    ja .u
    sub al, 0x20
.u:
    cmp al, [si]
    jne .out
    inc si
    dec cx
    jmp short .c
.tail:
    cmp byte [si], 0
    jmp short .out
.no:
    or sp, sp                       ; ZF = 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_rfind - [w16_rtk]/[w16_rnk] (keys, w16_rkey's, with the strings in
; w16_rtypb/w16_rnamb) -> AX = the entry's offset in the table, 0 = none.
; Keeps the rest.
; -----------------------------------------------------------------------------
w16_rfind:
    push bx
    push cx
    push si
    push di
    push es
    xor ax, ax
    cmp word [w16_rtlen], 0
    je .out
    mov es, [w16_rtab]
    mov di, 2
.type:
    mov ax, [es:di]
    or ax, ax
    jz .out
    mov cx, [es:di + 2]
    add di, 8
    push di
    mov bx, [w16_rtk]
    mov si, w16_rtypb
    call w16_rmatch
    pop di
    jne .skip
.ent:
    jcxz .none
    mov ax, [es:di + RN_ID]
    mov bx, [w16_rnk]
    mov si, w16_rnamb
    call w16_rmatch
    je .hit
    add di, RN_SIZE
    dec cx
    jmp short .ent
.skip:
    mov ax, RN_SIZE                 ; past this type's entries
    mul cx
    add di, ax
    cmp di, [w16_rtlen]
    jb .type
.none:
    xor ax, ax
    jmp short .out
.hit:
    mov ax, di
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; w16_rfindid - AX = an integer type, BX = an integer name -> AX = the entry
w16_rfindid:
    or ah, 0x80
    or bh, 0x80
    mov [w16_rtk], ax
    mov [w16_rnk], bx
    jmp w16_rfind

; -----------------------------------------------------------------------------
; w16_rload - AX = an entry -> AX = its block's handle (loaded if it was not,
; its usage raised), 0 = it could not be read. Keeps the rest.
; -----------------------------------------------------------------------------
w16_rload:
    push bx
    push cx
    push dx
    push si
    push es
    or ax, ax
    jz .out
    mov si, ax
    mov es, [w16_rtab]
    mov ax, [es:si + RN_HANDLE]
    or ax, ax
    jz .read
    inc word [es:si + RN_USAGE]
    jmp short .out
.read:
    call w16_rspan                  ; DX:AX = the offset, CX = the length
    jc .zero
    call w16_rread                  ; AX = the handle
    or ax, ax
    jz .out
    mov es, [w16_rtab]
    mov [es:si + RN_HANDLE], ax
    mov word [es:si + RN_USAGE], 1
    push ax                         ; the block knows its entry, for discard
    mov bx, si
    call w16_gfind                  ; (it takes SI)
    mov si, bx
    mov [es:GH_OWNER], si
    or byte [es:GH_FLAGS], GHF_RES
    pop ax
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16_rspan - ES:SI = an entry -> DX:AX = its file offset, CX = its length.
; CF=1 larger than a block can hold here. Keeps the rest.
w16_rspan:
    push bx
    push es
    mov es, [w16_rtab]
    mov cl, [es:0]                  ; the alignment shift
    mov ax, [es:si + RN_LEN]
    xor dx, dx
    call .shift
    or dx, dx
    jnz .big
    cmp ax, 0xFFF0
    ja .big
    mov bx, ax
    mov ax, [es:si + RN_OFF]
    xor dx, dx
    call .shift
    mov cx, bx
    pop es
    pop bx
    clc
    ret
.big:
    pop es
    pop bx
    stc
    ret
.shift:                             ; DX:AX <<= CL (CL kept)
    push cx
    xor ch, ch
    jcxz .sd
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
.sd:
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16_rread - CX bytes at file offset DX:AX of the program's file into a new
; moveable block. out: AX = its handle, 0 = none (no memory, or the file
; could not be read). Keeps the rest.
;
; READ_AT reads whole clusters from a cluster boundary, so the block is first
; the span of clusters that holds the resource, the resource is moved to its
; start, and the block is cut to the resource.
; -----------------------------------------------------------------------------
w16_rread:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [w16_rlen], cx
    mov bx, [w16_clus]              ; a power of two
    dec bx
    mov [w16_rpre], ax
    and [w16_rpre], bx              ; the bytes before it in its cluster
    not bx
    and ax, bx                      ; DX:AX = the cluster it starts in
    mov [w16_roff], ax
    mov [w16_roff + 2], dx
    mov ax, [w16_rpre]              ; the span: prefix + length, to clusters
    xor dx, dx
    add ax, cx
    adc dx, 0
    mov bx, [w16_clus]
    dec bx
    add ax, bx
    adc dx, 0
    not bx
    and ax, bx
    mov [w16_rspanb], ax
    mov [w16_rspanb + 2], dx
    mov cl, GMEM_MOVEABLE
    call w16_gnew                   ; AX = the handle, DX = the data
    or ax, ax
    jz .out0
    mov [w16_rh], ax
    mov [w16_rdat], dx
    call OSAPI_FILE_HERE            ; the user's folder, to come back to
    mov [w16_rback], dx
    mov [w16_rbackv], bl
    mov dx, [w16_dir]
    mov bl, [w16_vol]
    call OSAPI_FILE_GOTO_QM
    jc .fail
    mov bp, [w16_rdat]              ; BP = the paragraph being filled
.chunk:
    mov ax, [w16_rspanb]
    or ax, [w16_rspanb + 2]
    jz .read
    mov cx, 0x8000                  ; at most 32KB a call, a cluster multiple
    cmp word [w16_rspanb + 2], 0
    jne .cx
    cmp [w16_rspanb], cx
    jae .cx
    mov cx, [w16_rspanb]
.cx:
    mov es, bp
    xor bx, bx
    mov si, w16_name
    mov ax, [w16_roff]
    mov dx, [w16_roff + 2]
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    jc .back
    or ax, dx
    jz .read                        ; the end of the file
    add [w16_roff], cx
    adc word [w16_roff + 2], 0
    sub [w16_rspanb], cx
    sbb word [w16_rspanb + 2], 0
    mov ax, cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add bp, ax
    jmp short .chunk
.read:
    mov dx, [w16_rback]
    mov bl, [w16_rbackv]
    call OSAPI_FILE_GOTO_QM
    ; the resource to the block's start
    mov ax, [w16_rpre]
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    add ax, [w16_rdat]
    mov cx, [w16_rlen]
    mov es, [w16_rdat]
    xor di, di
    push ds
    mov ds, ax
    cld
    rep movsb
    pop ds
    ; ...and the block cut to it
    mov ax, [w16_rh]
    call w16_gfind                  ; ES = the header
    mov bx, [w16_rlen]
    add bx, 15
    mov cl, 4
    shr bx, cl
    inc bx
    call w16_gcut
    mov ax, [w16_rh]
    jmp short .out
.back:
    mov dx, [w16_rback]
    mov bl, [w16_rbackv]
    call OSAPI_FILE_GOTO_QM
.fail:
    mov ax, [w16_rh]
    call w16_gfreeh
.out0:
    xor ax, ax
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16_gcut - ES = a used block's header, BX = the paragraphs it is to keep
; (header included, no more than it has): the rest becomes free. Keeps all.
w16_gcut:
    push ax
    push cx
    mov cx, [es:GH_SIZE]
    cmp bx, cx
    jae .out
    mov [es:GH_SIZE], bx
    mov ax, es
    add ax, bx
    push es
    mov es, ax
    sub cx, bx
    mov [es:GH_SIZE], cx
    mov byte [es:GH_FLAGS], GHF_USED
    mov word [es:GH_MAGIC], GH_SIG
    pop es
    call w16_gfreeblk
.out:
    pop cx
    pop ax
    ret

; w16_rdiscard - AX = an entry: CF=0 it was loaded and unused, and its block
; is freed; CF=1 it stays. Keeps all.
w16_rdiscard:
    push ax
    push si
    push es
    mov si, ax
    mov es, [w16_rtab]
    cmp word [es:si + RN_USAGE], 0
    jne .keep
    mov ax, [es:si + RN_HANDLE]
    or ax, ax
    jz .keep
    mov word [es:si + RN_HANDLE], 0
    call w16_gfreeh
    clc
    jmp short .out
.keep:
    stc
.out:
    pop es
    pop si
    pop ax
    ret

; w16_rentry - AX = a resource block's handle -> SI = its entry, CF=1 not a
; loaded resource. Keeps the rest.
w16_rentry:
    push es
    call w16_gfind
    jc .out
    cmp byte [w16_gdisc], 0
    jne .no
    test byte [es:GH_FLAGS], GHF_RES
    jz .no
    mov si, [es:GH_OWNER]
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    ret

; =============================================================================
; As Windows exports them
; =============================================================================

; FindResource (KERNEL.60) - args: +0 lpType, +4 lpName, +8 hInst
w16k_findres:
    push di
    call w16_args
    push word [es:bx]
    push word [es:bx + 2]
    mov ax, [es:bx + 4]
    mov dx, [es:bx + 6]
    mov di, w16_rnamb
    call w16_rkey
    mov [w16_rnk], ax
    pop dx
    pop ax
    mov di, w16_rtypb
    call w16_rkey
    mov [w16_rtk], ax
    call w16_rfind
    pop di
    ret

; LoadResource (KERNEL.61) - args: +0 hRsrc, +2 hInst
w16k_loadres:
    call w16_args
    mov ax, [es:bx]
    jmp w16_rload

; LockResource (KERNEL.62) - args: +0 hResData: GlobalLock
w16k_lockres:
    jmp w16k_glock

; FreeResource (KERNEL.63) - args: +0 hResData. AX = 0 freed. The block stays
; cached until memory is short (SPEC.md 124.9)
w16k_freeres:
    push si
    push es
    call w16_args
    mov ax, [es:bx]
    call w16_rentry
    jc .no
    mov es, [w16_rtab]
    cmp word [es:si + RN_USAGE], 0
    je .z
    dec word [es:si + RN_USAGE]
.z:
    xor ax, ax
.no:
    pop es
    pop si
    ret

; SizeofResource (KERNEL.65) - args: +0 hRsrc, +2 hInst. DX:AX = its length
w16k_sizeres:
    push si
    call w16_args
    mov si, [es:bx]
    xor ax, ax
    xor dx, dx
    or si, si
    jz .out
    mov es, [w16_rtab]
    mov ax, [es:si + RN_LEN]
    mov cl, [es:0]
    xor ch, ch
    jcxz .out
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
.out:
    pop si
    ret

; LoadString (USER.176) - args: +0 cchMax, +2 lpBuffer, +6 wID, +8 hInst.
; AX = the bytes copied. The block stays loaded: a program asks for strings
; one at a time, and each read would be a disk revolution or two
w16u_loadstr:
    push si
    push di
    call w16_args
    mov cx, [es:bx]
    mov di, [es:bx + 2]
    mov dx, [es:bx + 4]
    mov [w16_rdst], di
    mov [w16_rdst + 2], dx
    mov [w16_rmax], cx
    mov ax, [es:bx + 6]
    mov [w16_rsid], ax
    mov bx, ax                      ; block id / 16 + 1
    mov cl, 4
    shr bx, cl
    inc bx
    mov ax, RT_STRING
    call w16_rfindid
    call w16_rload
    or ax, ax
    jz .none
    call w16_gfind
    jc .none
    mov ax, es
    inc ax
    mov es, ax                      ; ES:SI = the block's 16 strings
    xor si, si
    mov cx, [w16_rsid]
    and cx, 15
.skip:
    jcxz .here
    mov al, [es:si]
    xor ah, ah
    add si, ax
    inc si
    dec cx
    jmp short .skip
.here:
    mov cl, [es:si]
    xor ch, ch
    inc si
    mov ax, [w16_rmax]
    or ax, ax
    jz .zero                        ; no room even for the NUL
    dec ax
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    mov ax, es                      ; the block, as the source
    les di, [w16_rdst]
    push cx
    push ds
    mov ds, ax
    cld
    rep movsb
    pop ds
    mov byte [es:di], 0
    pop ax                          ; the bytes copied
    jmp short .out
.none:
    mov cx, [w16_rmax]
    jcxz .zero
    les di, [w16_rdst]
    mov byte [es:di], 0
.zero:
    xor ax, ax
.out:
    pop di
    pop si
    ret
