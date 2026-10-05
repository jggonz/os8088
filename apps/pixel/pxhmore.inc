; =============================================================================
; os8088 - apps/pixel/pxhmore.inc
;
; THE NEXT HEAD (SPEC.md 106.19, 106.25), shared source of the decoder parts
; whose HEAD walks past the first one - JPEG's markers, the EXTRAS' TIFF
; directory and arrays, ICO's image and mask, IFF's chunks. It was the
; resident's px_hmore until wave 8 gave its room to the GIF that plays; a
; part's HEAD vector now loops on its own PXD_MORE: ph_more reads the head
; and the walk goes on. LINKED: [px_k], [px_hseg], [px_hcap], [px_clsz],
; [px_fname] and the in-memory file's words are the package's, through DS.
; =============================================================================

PH_HEADMAX  equ 2048                ; a head: the bytes from where it is asked

; ph_more - the NEXT head a HEAD asked for (SPEC.md 106.19): its
; marker is at file offset [px_k + PXK_HNEXT]. READ_AT reads from a cluster
; multiple, so the head is read from the cluster that holds the marker and
; the walk starts inside it (PXK_HPOS); an in-memory file's head is a
; pointer moved. Either way it holds the 2,048 bytes from the marker, or to
; the file's end. CF = 1 AX = PXD_READ
ph_more:
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, [px_k + PXK_HNEXT]
    mov dx, [px_k + PXK_HNEXT + 2]
    cmp byte [px_rflat], 0
    jne .flat
    mov cx, [px_clsz]               ; a power of two
    dec cx
    mov bx, ax
    and bx, cx                      ; BX = the marker's place in its cluster
    not cx
    and ax, cx                      ; DX:AX = the cluster's offset
    mov [px_k + PXK_HPOS], bx
    mov [px_k + PXK_HBASE], ax
    mov [px_k + PXK_HBASE + 2], dx
    push bx
    mov es, [px_hseg]
    xor bx, bx
    mov cx, [px_hcap]
    mov si, px_fname
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    pop bx
    jc .rd
    add bx, PH_HEADMAX              ; the head: what was read, to 2,048
    or dx, dx                       ; past the marker
    jnz .cap
    cmp ax, bx
    jbe .len
.cap:
    mov ax, bx
.len:
    mov [px_k + PXK_HLEN], ax
    clc
    jmp short .out
.rd:
    mov ax, PXD_READ
    stc
    jmp short .out
.flat:
    mov bx, ax
    and bx, 15                      ; the paragraph's place
    and ax, 0xFFF0
    mov [px_k + PXK_HPOS], bx
    mov [px_k + PXK_HBASE], ax
    mov [px_k + PXK_HBASE + 2], dx
    push ax                         ; the head: the in-memory file at the
    push dx                         ; marker's paragraph
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_flatseg]
    mov [px_k + PXK_HSEG], ax
    pop dx
    pop ax
    mov cx, [px_flatlen]            ; its bytes from there: to the file's
    mov si, [px_flatlen + 2]        ; end, at most 2,048 past the marker
    sub cx, ax
    sbb si, dx
    add bx, PH_HEADMAX
    or si, si
    jnz .fc
    cmp cx, bx
    jbe .fl
.fc:
    mov cx, bx
.fl:
    mov [px_k + PXK_HLEN], cx
    clc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

