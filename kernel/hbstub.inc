; =============================================================================
; kernel/hbstub.inc - THE RESUME STUB'S BODY, ONCE (SPEC.md 87.5, 96.49)
;
; The ~440 bytes that are copied into the text framebuffer and jumped into:
; read the image back over conventional memory with `int 13h`, put the IVT
; back last, and enter the kernel at its wake address.
;
; **IT IS INCLUDED, NOT CALLED, AND THAT IS THE POINT.** Two hosts stage this
; blob and neither can reach the other's copy: `kernel/hiber.inc` puts it in
; `.modh` and `kerndos/kdresume.inc` puts it in `.text`, because by the time
; `kern_dos` needs a resume there is no kernel on the machine to ask. So the
; section is the INCLUDER's choice and this file declares none - which is also
; why it is a file rather than a macro: every label here is a plain one, unique
; because each host includes it exactly once, where a macro would need `%%` on
; twenty of them for no gain.
;
; Its map is `kernel/hbstage.inc` and everything it reads is a numeric offset
; in that segment, so the blob is position-independent.
; =============================================================================
%ifndef HBSTUB_INC
%define HBSTUB_INC 1

%ifdef DOSR_MARK
  %include "hbmark.inc"
%endif

hbs_stub:
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, HS_STACK
    cld
%ifdef DOSR_MARK
    mov word [HM_CUR], 2        ; **THE STUB OWNS THE CURSOR, NOT ITS STAGER.**
                                ; Two hosts stage this blob and only kd_resume
                                ; has a kdr_mark_init: on the ORDINARY resume
                                ; HM_CUR would be whatever was in the
                                ; framebuffer, and a trace that appends at a
                                ; garbage offset writes into HS_EXT or the IVT
                                ; copy - an instrument that breaks the thing it
                                ; is watching. Column 0 is skipped because that
                                ; is where kd_resume leaves its 'J'
    HBMARK 'S'                  ; the stub is running, and in THIS segment.
%endif                          ; Not before SS:SP: the macro pushes
    mov word [HS_K], 0
    mov si, HS_EXT
    mov cx, [HS_NX]
.extent:
    or cx, cx
    jnz .have
    jmp .done
.have:
%ifdef DOSR_MARK
    HBMARK 'X'                  ; one per extent of the chain
%endif
    push cx
    mov ax, [si]
    mov [HS_LBA], ax
    mov ax, [si+2]
    mov [HS_LBA+2], ax
    mov ax, [si+4]
    mov [HS_LEFT], ax
.run:
    cmp word [HS_LEFT], 0
    je .nextext
    ; --- CHS of [HS_LBA] (hd_chs's one div pair), and the track's remainder --
    mov ax, [HS_LBA+2]
    xor dx, dx
    div word [HS_SPT]
    mov bx, ax                  ; BX:AX = track after the second div...
    mov ax, [HS_LBA]
    div word [HS_SPT]           ; ...and DX = sector - 1
    mov cx, [HS_SPT]
    sub cx, dx                  ; CX = sectors from this one to the track end
    mov [HS_N], cx
    inc dx
    push dx                     ; the sector, 1-based
    mov dx, bx
    div word [HS_HEADS]         ; AX = cylinder, DX = head
    pop bx                      ; BL = the sector
    mov dh, dl                  ; DH = head
    mov dl, [HS_UNIT]           ; DL = drive
    mov ch, al                  ; CH = cylinder low 8, CL = sector | high 2
    mov cl, ah
    ror cl, 1
    ror cl, 1
    and cl, 0xC0
    or cl, bl
    mov [HS_CHS], cx
    mov [HS_CHS+2], dx
    ; --- the run: to the track's end, the extent's end, the DMA page ---------
    mov cx, [HS_N]
    cmp cx, [HS_LEFT]
    jbe .cap1
    mov cx, [HS_LEFT]
.cap1:
    mov bx, [HS_K]
    cmp bx, 3
    jae .high
    mov cx, 1                   ; file sectors 0..2 go to 0060:0000, singly
    mov bx, 0x0060
    jmp short .go
.high:
    push cx
    mov cl, 5
    shl bx, cl                  ; ES = k * 32: linear k * 512
    mov ax, bx
    and ax, 0x0FFF
    neg ax
    add ax, 0x1000              ; paragraphs to the 64KB page's end...
    shr ax, cl                  ; ...in sectors
    pop cx
    cmp cx, ax
    jbe .go
    mov cx, ax
.go:
    mov [HS_N], cx
    mov es, bx
    xor bx, bx
    mov di, 3                   ; tries
.try:
    mov ax, [HS_N]
    mov ah, 0x02
    mov cx, [HS_CHS]
    mov dx, [HS_CHS+2]
    sti                         ; the ROM wants its interrupts on an AT
    int 0x13
    jnc .ok
%ifdef DOSR_MARK
    HBMARK '!'                  ; ...and one per int 13h that refused
%endif
    xor ah, ah
    mov dl, [HS_UNIT]
    int 0x13                    ; reset, and again
    dec di
    jnz .try
%ifdef DOSR_MARK
    jmp .fail                   ; the trace pushed it out of short range
%else
    jmp short .fail
%endif
.ok:
%ifdef DOSR_MARK
    HBMARK '.'                  ; one per int 13h that moved sectors...
    HBMARKK                     ; ...and the file sector it has reached
%endif
    mov bx, [HS_K]
    cmp bx, 2
    jae .advance
    ; --- file sector 0 or 1: the IVT, into the staging area for later -------
    push ds
    push si
    mov cl, 9
    shl bx, cl
    add bx, HS_IVT
    mov di, bx
    mov ax, cs
    mov es, ax
    mov ax, 0x0060
    mov ds, ax
    xor si, si
    mov cx, 256
    rep movsw
    pop si
    pop ds
.advance:
    mov ax, [HS_N]
    add [HS_K], ax
    add [HS_LBA], ax
    adc word [HS_LBA+2], 0
    sub [HS_LEFT], ax
    jmp .run
.nextext:
    add si, HS_XSTRIDE
    pop cx
    dec cx
    jmp .extent
.done:
%ifdef DOSR_MARK
    HBMARK 'D'                  ; the whole image is back
%endif
    cli                         ; --- the IVT goes back last, under cli ------
    xor ax, ax
    mov es, ax
    xor di, di
    mov si, HS_IVT
    mov cx, 512
    rep movsw
    mov al, [HS_MASKM]
    out 0x21, al
    cmp byte [HS_SLAVE], 0
    je .noslave
    mov al, [HS_MASKS]
    out 0xA1, al
.noslave:
%ifdef DOSR_MARK
    HBMARK 'V'                  ; the IVT and the PICs are the image's again,
%endif                          ; so this is the last instant the stub owns
    mov ax, [cs:HS_KSEG]        ; **STAGED AND NOT ASSEMBLED** (hbstage.inc):
    mov ds, ax                  ; ...and NOT SS:SP - hbm_wake builds its own
    jmp far [cs:HS_WAKE]        ; out of the memory it has just put back
.fail:
    mov ah, 0x02                ; the cursor to row 8 first: rows 0-2 of this
    xor bh, bh                  ; page ARE the stub, and the ROM's teletype
    mov dx, 0x0800              ; writes at the cursor the mode set homed. Row
    int 0x10                    ; 8 is the IVT copy, dead on this path, and
    mov si, hbs_s_fail - hbs_stub   ; three lines from there never scroll
.ch:
    mov al, [si]
    or al, al
    jz .key
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    inc si
    jmp short .ch
.key:
    xor ah, ah
    int 0x16
    int 0x19
    jmp $
hbs_s_fail: db 13, 10, 'Resume failed: the disk could not be read.', 13, 10
            db 'Press any key to restart.', 13, 10, 0
hbs_stub_end:
%if hbs_stub_end - hbs_stub > HS_CODE_MAX
  %error "hiber: the resume stub is bigger than its staging slot"
%endif

%endif                          ; HBSTUB_INC
