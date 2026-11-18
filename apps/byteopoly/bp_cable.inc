; Byteopoly - the C bridge to apps/os88cable.inc (SPEC.md 131.5.1). Lane: link.
;
; bp_link.c reaches the UART only through the bp_ck_* functions below: near
; cdecl (SPEC.md 73.3), arguments at [bp+4]..., AX the answer; each keeps
; BP, DS and DF and may change everything else. os88cable.inc does the port
; search, the lease, the framing and the CRC; this file provides what it
; asks of an includer and a small ring the C reads frames out of.
;
; THE REGION RULE (SPEC.md 131.5.1). os88cable.inc pins our region before a
; lease and re-declares it movable with OS88_REGION_MOVABLE's own `ret` when
; the lease is let go or refused. A C package with an overlay must name
; cc_regreloc (= cc_ovbind, SPEC.md 73.14) instead, or the overlay's back
; vectors keep naming the old base after the next compaction: bpk_fixreg
; puts the declaration right after every call that can have closed it.

LK_NTYPES   equ 24                  ; bp_link.c's BPF_N
lk_lens     equ _bpl_lens           ; bp_link.c's table: ONE copy of the lengths
%include "os88cable.inc"

BPK_SLOTS   equ 4                   ; frames the ring holds (a refill brings 4 at most)
BPK_SLOT    equ 30                  ; a slot: the length, then a body (29 at most)
BPK_POLL    equ 32                  ; bytes a refill reads: at most 4 frames

section .bss
lk_blk:     resb LK_SIZE
lk_sbuf:    resb SER_BUFFER_SIZE    ; the lease's rings, in our own region
lk_crct:    resb 512
bpk_ring:   resb BPK_SLOTS * BPK_SLOT
bpk_head:   resb 1                  ; the next slot to read
bpk_n:      resb 1                  ; slots full
bpk_was:    resb 1                  ; LK_OPEN before a call
bpk_pad:    resb 1
bpk_drop:   resw 1                  ; frames the full ring dropped
section .text

; lk_take - os88cable.inc's hand-over: AL = the type, SI = a valid body (its
; length is the type's, its CRC checked). The body without its CRC goes into
; the next free slot; a full ring drops it and counts.
lk_take:
    mov bl, al
    xor bh, bh
    mov cl, [lk_lens+bx]
    sub cl, 2                       ; (lk_frame refuses a length below 3)
    cmp byte [bpk_n], BPK_SLOTS
    jb .room
    inc word [bpk_drop]
    ret
.room:
    mov al, [bpk_head]
    add al, [bpk_n]
    cmp al, BPK_SLOTS
    jb .s
    sub al, BPK_SLOTS
.s:
    mov ah, BPK_SLOT
    mul ah
    mov di, ax
    add di, bpk_ring
    push es
    push ds
    pop es
    mov al, cl
    stosb
    xor ch, ch
    cld
    rep movsb
    pop es
    inc byte [bpk_n]
    ret

; bpk_fixreg - after a call that can have let the lease go: the region
; movable again under OUR relocation proc. Keeps every register
bpk_fixreg:
    cmp byte [lk_blk+LK_OPEN], 0
    jne .out
    cmp byte [bpk_was], 0
    jne .fix
    cmp byte [lk_blk+LK_PORT], 0    ; (zeroed before the call: a lease was
    je .out                         ; tried and refused)
.fix:
    push ax
    push bx
    push dx
    mov dx, ds                      ; (the data segment names the region on
    mov ax, cc_regreloc             ; every kernel, kern_mega's too)
    call OSAPI_MEM_MOVABLE
    pop dx
    pop bx
    pop ax
    mov byte [bpk_was], 0
.out:
    ret

; bpk_pre - before a call that can open or close: what bpk_fixreg compares
bpk_pre:
    mov al, [lk_blk+LK_OPEN]
    mov [bpk_was], al
    or al, al
    jnz .o
    mov [lk_blk+LK_PORT], al
.o:
    ret

; int bp_ck_open(int port) - 0 Automatic, 1 COM1, 2 COM2 (the user named it)
_bp_ck_open:
    push bp
    mov bp, sp
    push si
    push di
    call bpk_pre
    mov byte [bpk_n], 0
    mov ax, [bp+4]
    or ax, ax
    jnz .named
    call lk_disconnect
    call lk_auto
    xor ax, ax
    jmp .out
.named:
    call lk_connect
    mov ax, 0
    jnc .out
    mov ax, 15                      ; BP_EIO: LK_MSG says why
.out:
    call bpk_fixreg
    pop di
    pop si
    pop bp
    ret

; void bp_ck_close(void)
_bp_ck_close:
    push bp
    mov bp, sp
    call bpk_pre
    call lk_disconnect
    mov byte [bpk_n], 0
    call bpk_fixreg
    pop bp
    ret

; void bp_ck_tick(void) - the search moves on, what waits goes out, and the
; framer's counters reach bp_diag (crc: frames dropped; overrun: line errors
; and overflows, and frames the ring had no room for)
_bp_ck_tick:
    push bp
    mov bp, sp
    push si
    push di
    call bpk_pre
    call lk_tick
    cmp byte [lk_blk+LK_OPEN], 0
    je .shut
    call lk_flush
.shut:
    call bpk_fixreg
    mov ax, [lk_blk+LK_STBAD]
    mov [_bp_diag+0], ax
    mov ax, [lk_blk+LK_STERR]
    add ax, [bpk_drop]
    mov [_bp_diag+6], ax
    pop di
    pop si
    pop bp
    ret

; int bp_ck_heard(void) - a Byteopoly peer answered: the search stops here
_bp_ck_heard:
    mov byte [lk_blk+LK_HEARD], 1
    mov ax, 1
    ret

; int bp_ck_send(const BPByte *body, unsigned n) - BP_OK, or BP_EPENDING
; when the staging has no room (send it again later)
_bp_ck_send:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+4]
    mov cx, [bp+6]
    cld
    call lk_send
    mov ax, 0
    jnc .ok
    mov ax, 12                      ; BP_EPENDING
.ok:
    pop di
    pop si
    pop bp
    ret

; int bp_ck_recv(BPByte *body) - the next valid body (no CRC) and its
; length, 0 none. An empty ring is refilled first: one lk_poll of at most
; BPK_POLL bytes, which is at most four frames, so one refill cannot
; overflow it
_bp_ck_recv:
    push bp
    mov bp, sp
    push si
    push di
    cmp byte [bpk_n], 0
    jne .have
    mov cx, BPK_POLL
    call lk_poll
    xor ax, ax
    cmp byte [bpk_n], 0
    je .out
.have:
    mov al, [bpk_head]
    mov ah, BPK_SLOT
    mul ah
    mov si, ax
    add si, bpk_ring
    mov di, [bp+4]
    lodsb
    xor ah, ah
    mov cx, ax
    push ax
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    pop ax
    dec byte [bpk_n]
    mov bl, [bpk_head]
    inc bl
    cmp bl, BPK_SLOTS
    jb .h
    xor bl, bl
.h:
    mov [bpk_head], bl
.out:
    pop di
    pop si
    pop bp
    ret

; int bp_ck_port(void) - the port held, 0 none
_bp_ck_port:
    xor ax, ax
    cmp byte [lk_blk+LK_OPEN], 0
    je .n
    mov al, [lk_blk+LK_PORT]
.n:
    ret

; const char *bp_ck_msg(void) - the framer's own status line
_bp_ck_msg:
    mov ax, [lk_blk+LK_MSG]
    or ax, ax
    jnz .m
    mov ax, lk_m_off
.m:
    ret

; BPWord bp_ck_rand(void) - identities only, never the game's dice
_bp_ck_rand:
    call OSAPI_RAND
    ret
