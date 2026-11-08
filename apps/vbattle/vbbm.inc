; =============================================================================
; os8088 - apps/vbattle/vbbm.inc - BOOKMARKS: every clip's head reached in
;          one FAT link, not a walk from the file's start (SPEC.md 126.4.7)
;
; A seek with a zeroed READ_SEQ cursor walks the bundle's cluster chain from
; its first cluster: ~330 us a link on the field 5150, so a clip head 5.8 MB
; into a 6.5 MB bundle cost ~1 s to start - every time, on a PicoMEM 2 and an
; ST-225 alike (docs/reports/PICOMEM2-STUTTER-2026-11-07.md). A kernel that
; walks a MOVED cursor forward (SPEC.md 18.4.8.2) makes a copy of a cursor a
; bookmark; this keeps one for every clip head and the music, built in ONE
; forward pass at load, and vb_dread starts every seek from the best place
; it has at or before the target - a bookmark, or the stream's own cursor.
;
; A bookmark is 4 bytes - the cursor's +4 (the cluster index of its +12) and
; +6 (the cluster before it) - over one 16-byte template that carries what
; every one shares (the generation, the volume, the size). Stale - a write,
; a remount - the kernel re-seeds from the name at the target, silently:
; never wrong, only slow once. On a kernel without the forward seek (+4 left
; 0 by the first read) nothing is built and every seek is what it was.
; =============================================================================

VB_BMMAX    equ 16384               ; clusters past this: no bookmarks (a
                                    ; chain of them is short anyway)

; vb_bmbuild - the bookmarks, for the bundle [vb_namep] (+ [vb_embat]). The
; UI task, at load. Every failure leaves [vb_bmok] 0 and every seek as it
; was. Preserves every register
vb_bmbuild:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [vb_bmok], 0
    push ds
    pop es
    mov di, vb_bmk                  ; none yet: an index of 0 is "none"
    mov cx, 2 * (VB_MAXCLIP + 1)
    xor ax, ax
    cld
    rep stosw
    mov di, vb_bmc                  ; the build's own cursor, cold, and no
    mov cx, FSEQ_SIZE               ; template (they are adjacent): a build
    rep stosw                       ; that stops part way hands out nothing
    call OSAPI_FILE_DFREE           ; BX = sectors a cluster, this volume's
    jc .out
    cmp bx, VB_BMMAX / 512
    ja .out
    mov cl, 9
    shl bx, cl
    jz .out
    mov [vb_bmclb], bx
    mov ax, bx                      ; a cluster's scratch: its KB, at least 1
    mov cl, 10
    shr ax, cl
    jnz .kb
    inc ax
.kb:
    call OSAPI_MEM_CLAIM
    jc .out
    mov [vb_bmseg], dx
    xor bx, bx                      ; the targets: every clip's head, then
.next:                              ; the music (slot VB_MAXCLIP)
    cmp bx, [vb_meta+VH_NCLIPS]
    jb .clip
    cmp bx, VB_MAXCLIP
    ja .done
    mov ax, [vb_meta+VH_MUSIC]
    mov dx, [vb_meta+VH_MUSIC+2]
    mov bx, VB_MAXCLIP
    jmp short .have
.clip:
    push bx
    call vb_crec                    ; SI = its row (CL spent)
    pop bx
    mov ax, [si+VC_OFF]
    mov dx, [si+VC_OFF+2]
.have:
    add ax, [vb_embat]              ; the FILE's offset
    adc dx, [vb_embat+2]
    mov cx, [vb_bmclb]              ; down to its cluster, and the one
    neg cx                          ; before it: read that, and the cursor
    and ax, cx                      ; stands AT the head's cluster
    sub ax, [vb_bmclb]
    sbb dx, 0
    jc .skip                        ; the head is in the first cluster: no
    cmp word [vb_bmc+4], 0          ; walk to save. The cursor MOVED when
    jne .move                       ; this kernel walks forward (it says so
    mov word [vb_bmc+0], 0          ; in +4 after a read), cold otherwise
.move:
    mov [vb_bmc+FSEQ_OFF], ax
    mov [vb_bmc+FSEQ_OFF+2], dx
    push bx
    mov dx, [vb_bmseg]
    xor bx, bx
    mov cx, [vb_bmclb]
    mov si, [vb_namep]
    mov di, vb_bmc
    call OSAPI_FILE_READ_SEQ        ; ES:DI = the cursor (ES = DS here)
    pop bx
    jc .free
    cmp word [vb_bmc+4], 0          ; not a forward-walking kernel: nothing
    je .free                        ; it built can be trusted
    mov si, bx
    shl si, 1
    shl si, 1
    mov ax, [vb_bmc+4]
    mov [vb_bmk+si], ax
    mov ax, [vb_bmc+6]
    mov [vb_bmk+si+2], ax
    mov byte [vb_bmok], 1
.skip:
    inc bx
    jmp .next
.done:
    mov si, vb_bmc                  ; the template: the generation, the
    mov di, vb_bmt                  ; volume and the size, as they stand
    mov cx, FSEQ_SIZE / 2
    rep movsw
.free:
    mov dx, [vb_bmseg]
    call OSAPI_MEM_FREE
    cmp bx, VB_MAXCLIP              ; (left part way: none of it is used)
    jbe .keep
    jmp short .out
.keep:
    cmp word [vb_bmt+0], 0          ; ...the template is what .done wrote:
    jne .out                        ; without it, no bookmark is handed out
    mov byte [vb_bmok], 0
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vb_bmseek - cursor DI is about to SEEK to file offset DX:AX: make it
; start from the best place there is at or before it - itself, if it is a
; forward-walking cursor behind the target, or the nearest bookmark. CF=0
; DI is that place (its +12 the caller's to set), CF=1 none: zero it.
; Preserves every register
vb_bmseek:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp word [vb_bmclb], 0
    je .none
    div word [vb_bmclb]             ; AX = the target's cluster index (the
    mov cx, 0xFFFF                  ; offset is under 32 MB: DX < 512)
    xor bx, bx                      ; BX = the best index so far
    mov si, [di+4]                  ; ITSELF, when it walks forward and is
    or si, si                       ; not past the target
    jz .scan
    cmp si, ax
    ja .scan
    mov bx, si
    mov cx, 0xFFFE
.scan:
    cmp byte [vb_bmok], 0
    je .pick
    xor si, si
.k:
    mov dx, [vb_bmk+si]
    or dx, dx
    jz .kn
    cmp dx, ax
    ja .kn
    cmp dx, bx
    jbe .kn
    mov bx, dx
    mov cx, si
.kn:
    add si, 4
    cmp si, 4 * (VB_MAXCLIP + 1)
    jb .k
.pick:
    cmp cx, 0xFFFF
    je .none
    cmp cx, 0xFFFE
    je .ok                          ; itself, as it stands
    push di                         ; a bookmark: the template, then its two
    push es                         ; words
    push ds
    pop es
    mov si, vb_bmt
    mov dx, cx
    mov cx, FSEQ_SIZE / 2
    cld
    rep movsw
    pop es
    pop di
    mov si, dx
    mov ax, [vb_bmk+si]
    mov [di+4], ax
    mov ax, [vb_bmk+si+2]
    mov [di+6], ax
.ok:
    clc
    jmp short .out
.none:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
