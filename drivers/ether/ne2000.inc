; =============================================================================
; The 8390 - NE1000 and NE2000, one core (SPEC.md 72.2)
;
; National Semiconductor's DP8390 is the chip in both, and the difference
; between the cards is the width of the bus around it: an NE1000 is 8-bit with
; 8KB of buffer, an NE2000 is 16-bit with 16KB. **This driver drives both as
; 8-BIT**, and that is not a simplification to be improved on later - it is
; SPEC.md 52's rung-1 lesson in a second place. An 8088's `in ax, dx` is TWO
; 8-BIT BUS CYCLES AT THE SAME PORT: the high half of every word is lost, and
; it assembles, runs, and delivers a frame with every other byte wrong.
; Byte-wide transfers are what the target machine can do, so byte-wide is what
; this does, on every card and every CPU.
;
; --- THE REGISTER FILE IS PAGED, AND THAT IS THE COMMONEST BUG --------------
; Sixteen ports at the card's base select one of four pages through CR's top
; two bits. PSTART, RCR and ISR are page 0; PAR0..5 and CURR are page 1. A
; routine that leaves the card on page 1 and returns to one that expects page
; 0 reads CURR as BNRY and the ring walks backwards. **Every routine here
; leaves the card on PAGE 0**, and the two that need page 1 put it back before
; they return.
;
; --- READ AND WRITE ARE DIFFERENT REGISTERS AT ONE PORT ---------------------
; Page 0 offset 0x0C is RSR when you read it and RCR when you write it; 0x01
; is CLDA0 read and PSTART write. So the names below come in pairs and the
; write-side name is the one to reach for when emitting a value. Using the
; read-side name for a write assembles perfectly and configures nothing.
;
; --- THE RING IS THE CARD'S, AND WE ONLY HOLD ONE POINTER -------------------
; Received frames land in a circular buffer of 256-byte pages between PSTART
; and PSTOP. CURR is where the CARD will write next; BNRY is the last page WE
; have read. The ring is EMPTY when they meet, which is why BNRY trails
; [eth_next] by one page and never catches up: a BNRY that equals CURR is a
; ring the card believes is full, and it stops receiving without saying so.
;
; **AND A FRAME CAN STRADDLE PSTOP.** The card's LOCAL DMA wraps at the ring
; end; its REMOTE DMA - the window we read through - does not. A frame that
; began three pages below PSTOP is read as one linear run straight off the end
; of packet memory, which returns rubbish for the tail and says nothing. Every
; read here is split at PSTOP by ne_ring_read.
;
; --- NO INTERRUPT, AND THAT IS DELIBERATE (SPEC.md 72.2.1) ------------------
; The ring is drained by POLLING, from inside the socket verbs a package
; already calls every tick. An ISR would have to run at IF=0 on a machine
; whose kernel is not re-entrant, for a card that can hold ten frames while it
; waits; what it would buy is latency this application cannot use, and what it
; would cost is the one class of bug this tree has no way to reproduce.
; =============================================================================

; --- page 0, READ side -------------------------------------------------------
NE_CR       equ 0x00            ; command, on every page
NE_CLDA0    equ 0x01
NE_CLDA1    equ 0x02
NE_BNRY     equ 0x03            ; read AND write
NE_TSR      equ 0x04
NE_NCR      equ 0x05
NE_FIFO     equ 0x06
NE_ISR      equ 0x07            ; read AND write-1-to-clear
NE_CRDA0    equ 0x08
NE_CRDA1    equ 0x09
NE_RSR      equ 0x0C
NE_CNTR0    equ 0x0D
NE_CNTR1    equ 0x0E
NE_CNTR2    equ 0x0F

; --- page 0, WRITE side (same ports, different registers) --------------------
NE_PSTARTR  equ 0x01
NE_PSTOPR   equ 0x02
NE_TPSR     equ 0x04            ; transmit page start
NE_TBCR0    equ 0x05            ; transmit byte count
NE_TBCR1    equ 0x06
NE_RSAR0    equ 0x08            ; remote DMA start address
NE_RSAR1    equ 0x09
NE_RBCR0    equ 0x0A            ; remote DMA byte count (read AND write)
NE_RBCR1    equ 0x0B
NE_RCR      equ 0x0C            ; receive configuration
NE_TCR      equ 0x0D            ; transmit configuration
NE_DCR      equ 0x0E            ; data configuration
NE_IMR      equ 0x0F            ; interrupt mask

; --- page 1 ------------------------------------------------------------------
NE_PAR0     equ 0x01            ; ...our own address, six bytes
NE_CURR     equ 0x07
NE_MAR0     equ 0x08

; --- and the two ports outside the register file -----------------------------
NE_DATA     equ 0x10            ; the remote DMA window
NE_RESET    equ 0x1F

; --- CR bits -----------------------------------------------------------------
NE_STP      equ 0x01
NE_STA      equ 0x02
NE_TXP      equ 0x04
NE_RD_READ  equ 0x08            ; remote DMA: read from the card
NE_RD_WRITE equ 0x10            ; ...write to it
NE_RD_ABORT equ 0x20            ; ...abort/complete
NE_PAGE1    equ 0x40

; --- ISR bits ----------------------------------------------------------------
NE_PRX      equ 0x01
NE_PTX      equ 0x02
NE_RXE      equ 0x04
NE_TXE      equ 0x08
NE_OVW      equ 0x10            ; the ring overflowed: ne_overflow recovers
NE_CNT      equ 0x20
NE_RDC      equ 0x40            ; remote DMA complete
NE_RST      equ 0x80

; --- the buffer layout, in 256-byte pages ------------------------------------
; The NE2000's packet memory starts at 16KB (page 0x40) and runs to 32KB
; (0x80). Six pages of transmit is one full 1514-byte frame with room to
; spare; the rest is the receive ring - 58 pages, about ten frames, which is
; what makes polling at 18.2 Hz safe (SPEC.md 72.2.1).
;
; **AN NE1000 HAS HALF OF IT**, 8KB at page 0x20..0x40, so the layout is
; picked at probe time from what the PROM said the card was. Every constant
; below is therefore a variable in [eth_pstart]/[eth_pstop]/[eth_txpg] and the
; equs are the NE2000 case they are initialised to.
NE_TXPAGE   equ 0x40
NE_TXPAGES  equ 6
NE_PSTART   equ NE_TXPAGE + NE_TXPAGES
NE_PSTOP    equ 0x80
NE1_TXPAGE  equ 0x20
NE1_PSTART  equ NE1_TXPAGE + NE_TXPAGES
NE1_PSTOP   equ 0x40
NE_FRAME    equ 1514            ; the largest Ethernet frame without the FCS
NE_MINFRAME equ 60              ; ...and the smallest one may put on the wire

; --- and the two SPINS, sized from the 4.77MHz machine (PERFORMANCE.md rule 3)
; A spin is not "as large as the counter goes": one turn of either is a port
; read behind a near call+ret, ~23 us on an 8088, so the count IS the time.
; The 8390 finishes a remote DMA of a whole frame in well under a millisecond
; and a transmit of one in ~1.2 ms plus backoff, so these are 9 ms and 92 ms -
; two orders of headroom each, and a card that has gone away costs a tenth of
; a second per frame rather than the 1.6 SECONDS `mov cx, 0` bought.
NE_RDCSPIN  equ 400             ; remote DMA complete (ne_dma_write)
NE_TXSPIN   equ 4000            ; the previous transmit is done (ne_tx)

; -----------------------------------------------------------------------------
; ne_out / ne_in - one card register
; in:  AL = the register (base-relative); ne_out also AH = the byte
; out: ne_in: AL = the byte
; clobbers: flags (ne_in also AL, which is its answer)
; -----------------------------------------------------------------------------
ne_out:
    push dx
    mov dl, al
    xor dh, dh
    add dx, [eth_base]
    push ax
    mov al, ah
    out dx, al
    pop ax
    pop dx
    ret

ne_in:
    push dx
    mov dl, al
    xor dh, dh
    add dx, [eth_base]
    in  al, dx
    pop dx
    ret

; -----------------------------------------------------------------------------
; ne_dma_read - AX bytes from card address BX into DS:DI
; in:  AX = count, BX = the card's own address, DI = where to put them
; out: DI advanced past the bytes
; clobbers: flags
;
; A remote DMA read: point RSAR at the card address, RBCR at the count, then
; read the data window that many times. **BYTE AT A TIME**, per the header.
; -----------------------------------------------------------------------------
ne_dma_read:
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    mov ax, (NE_STA | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov al, NE_RBCR0
    mov ah, cl
    call ne_out
    mov al, NE_RBCR1
    mov ah, ch
    call ne_out
    mov al, NE_RSAR0
    mov ah, bl
    call ne_out
    mov al, NE_RSAR1
    mov ah, bh
    call ne_out
    mov ax, (NE_STA | NE_RD_READ) << 8 | NE_CR
    call ne_out
    mov dx, [eth_base]
    add dx, NE_DATA
    jcxz .done
.byte:
    in  al, dx
    mov [di], al
    inc di
    loop .byte
.done:
    mov ax, NE_RDC << 8 | NE_ISR        ; the DMA is complete: say so, or the
    call ne_out                         ; next one starts on a stale flag
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_dma_write - AX bytes from DS:SI to card address BX
; clobbers: flags. SI advances.
; -----------------------------------------------------------------------------
ne_dma_write:
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    mov ax, (NE_STA | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov ax, NE_RDC << 8 | NE_ISR
    call ne_out
    mov al, NE_RBCR0
    mov ah, cl
    call ne_out
    mov al, NE_RBCR1
    mov ah, ch
    call ne_out
    mov al, NE_RSAR0
    mov ah, bl
    call ne_out
    mov al, NE_RSAR1
    mov ah, bh
    call ne_out
    mov ax, (NE_STA | NE_RD_WRITE) << 8 | NE_CR
    call ne_out
    mov dx, [eth_base]
    add dx, NE_DATA
    jcxz .wait
.byte:
    mov al, [si]
    inc si
    out dx, al
    loop .byte
.wait:
    mov cx, NE_RDCSPIN          ; bounded: a card that never raises RDC is one
.rdc:                           ; that has gone away, and this must not hang
    mov al, NE_ISR
    call ne_in
    test al, NE_RDC
    jnz .done
    loop .rdc
.done:
    mov ax, NE_RDC << 8 | NE_ISR
    call ne_out
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_probe - is there an 8390 at [eth_base]?
; out: CF=0 and the MAC in eth_mac, [eth_word] set; CF=1 = nothing like one
;
; **A RESET AND THEN THE PROM**, and the test is that the PROM's first six
; bytes are a plausible station address rather than that any single register
; reads back. A dead ISA port floats to 0xFF and a populated one that is not
; an 8390 can still hold a byte, so `wrote 0x21 and read 0x21` is a test an
; empty slot passes about half the time on real hardware.
;
; The card's WIDTH comes out of the same read and costs nothing: an NE2000
; asked for 32 bytes in 8-bit mode answers with each PROM byte TWICE, because
; the card is putting a 16-bit word on an 8-bit bus. So if every adjacent pair
; agrees it is a 16-bit card and the address is bytes 0,2,4,..; if any pair
; differs it is an NE1000 and the address is bytes 0..5. That is Linux's
; ne.c's test and it needs no signature byte, which matters because the
; signature is the one thing the clones disagree about.
; -----------------------------------------------------------------------------
ne_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [eth_base]
    add dx, NE_RESET
    in  al, dx                  ; the reset port: reading it and writing the
    out dx, al                  ; value back is the documented reset
    mov cx, 8000
.settle:
    mov al, NE_ISR              ; ...bounded, because a port that never sets
    call ne_in                  ; RST is exactly what an empty slot looks like
    test al, NE_RST
    jnz .up
    loop .settle
    jmp .none
.up:
    mov ax, (NE_STP | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov ax, 0x48 << 8 | NE_DCR          ; BYTE-WIDE (see the header)
    call ne_out
    mov ax, 0x00 << 8 | NE_RBCR0
    call ne_out
    mov ax, 0x00 << 8 | NE_RBCR1
    call ne_out
    mov ax, 0x20 << 8 | NE_RCR          ; monitor: accept nothing yet
    call ne_out
    mov ax, 0x02 << 8 | NE_TCR          ; internal loopback while we set up
    call ne_out

    mov ax, 32                          ; the PROM: 32 bytes at card address 0
    mov bx, 0
    mov di, eth_prom
    call ne_dma_read

    mov byte [eth_word], 1              ; ...16-bit until a pair disagrees
    mov si, eth_prom
    mov cx, 16
.pair:
    mov al, [si]
    cmp al, [si+1]
    je  .same
    mov byte [eth_word], 0
.same:
    add si, 2
    loop .pair

    mov si, eth_prom                    ; the address, doubled or not
    mov di, eth_mac
    mov cx, 6
.mac:
    mov al, [si]
    mov [di], al
    inc si
    cmp byte [eth_word], 0
    je  .m1
    inc si
.m1:
    inc di
    loop .mac

    ; --- is it a plausible station address? ---------------------------------
    ; All zero and all ones are what a floating bus reads as, and bit 0 of the
    ; first byte is the MULTICAST bit - a card's own address never has it set,
    ; so a PROM that answers with one is not a PROM.
    test byte [eth_mac], 1
    jnz .none
    mov si, eth_mac
    mov cx, 6
    xor bl, bl                          ; the OR of every byte
    mov bh, 0                           ; ...and how many are 0xFF
.chk:
    mov al, [si]
    inc si
    or  bl, al
    cmp al, 0xFF
    jne .n1
    inc bh
.n1:
    loop .chk
    or  bl, bl
    jz  .none                           ; all zero: nothing there
    cmp bh, 6
    jne .ok
    jmp .none                           ; all ones: a floating bus
.ok:
    clc
    jmp short .out
.none:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_init - bring the card up and start receiving
; in:  [eth_mac] and [eth_word] as ne_probe left them
; out: nothing; the card is on page 0, started, IMR = 0 (we poll)
; -----------------------------------------------------------------------------
ne_init:
    push ax
    push cx
    push si

    mov al, NE_TXPAGE                   ; the ring is the card's SIZE, and an
    mov ah, NE_PSTART                   ; NE1000 has half of it
    mov ch, NE_PSTOP
    cmp byte [eth_word], 0
    jne .pages
    mov al, NE1_TXPAGE
    mov ah, NE1_PSTART
    mov ch, NE1_PSTOP
.pages:
    mov [eth_txpg], al
    mov [eth_pstart], ah
    mov [eth_pstop], ch

    mov ax, (NE_STP | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov ax, 0x48 << 8 | NE_DCR
    call ne_out
    mov ax, 0x00 << 8 | NE_RBCR0
    call ne_out
    mov ax, 0x00 << 8 | NE_RBCR1
    call ne_out
    mov ax, 0x20 << 8 | NE_RCR          ; monitor while the ring is being set
    call ne_out
    mov ax, 0x02 << 8 | NE_TCR          ; internal loopback, likewise
    call ne_out
    mov al, NE_TPSR
    mov ah, [eth_txpg]
    call ne_out
    mov al, NE_PSTARTR
    mov ah, [eth_pstart]
    call ne_out
    mov al, NE_PSTOPR
    mov ah, [eth_pstop]
    call ne_out
    mov al, NE_BNRY                     ; BNRY trails our read pointer by one
    mov ah, [eth_pstart]                ; page, and our read pointer starts one
    call ne_out                         ; page in - see the header
    mov ah, [eth_pstart]
    inc ah
    mov [eth_next], ah
    mov ax, 0xFF << 8 | NE_ISR
    call ne_out
    mov ax, 0x00 << 8 | NE_IMR          ; NO INTERRUPTS: SPEC.md 72.2.1
    call ne_out

    mov ax, (NE_PAGE1 | NE_STP | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov si, eth_mac                     ; our address, six registers
    mov cl, NE_PAR0
    mov ch, 6
.par:
    mov al, cl
    mov ah, [si]
    call ne_out
    inc si
    inc cl
    dec ch
    jnz .par
    mov cl, NE_MAR0                     ; no multicast at all: the stack has
    mov ch, 8                           ; no use for it and an open filter is
.mar:                                   ; frames we would have to drop anyway
    mov al, cl
    mov ah, 0
    call ne_out
    inc cl
    dec ch
    jnz .mar
    mov al, NE_CURR
    mov ah, [eth_next]
    call ne_out

    mov ax, (NE_STA | NE_RD_ABORT) << 8 | NE_CR     ; page 0 again, and GO
    call ne_out
    mov ax, 0x00 << 8 | NE_TCR          ; out of loopback
    call ne_out
    mov ax, 0x04 << 8 | NE_RCR          ; ours and broadcast; no promiscuous,
    call ne_out                         ; no multicast
    mov ax, 0xFF << 8 | NE_ISR
    call ne_out
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_stop - silence the card (DRVV_DETACH's half, and it cannot fail)
; -----------------------------------------------------------------------------
ne_stop:
    push ax
    mov ax, (NE_STP | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov ax, 0x00 << 8 | NE_IMR
    call ne_out
    mov ax, 0xFF << 8 | NE_ISR
    call ne_out
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_curr - what page the CARD will write next (page 1's CURR)
; out: AL = the page. The card is on page 0 again.
; -----------------------------------------------------------------------------
ne_curr:
    push bx
    mov ax, (NE_PAGE1 | NE_STA | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov al, NE_CURR
    call ne_in
    mov bl, al
    mov ax, (NE_STA | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov al, bl
    pop bx
    ret

; -----------------------------------------------------------------------------
; ne_ring_read - AX bytes from ring page BH offset BL into DS:DI, WRAPPING
; in:  AX = count, BH = page, BL = byte offset in it, DI = destination
; out: DI advanced
;
; This is the split the header names. The card's remote DMA walks straight
; past PSTOP; the frame it is carrying does not.
; -----------------------------------------------------------------------------
ne_ring_read:
    push ax
    push bx
    push cx
    push dx
    mov dx, bx                  ; DX = the card address
    mov cx, ax                  ; CX = what is left to read
    mov bh, [eth_pstop]         ; BX = the byte just past the ring
    xor bl, bl
    sub bx, dx                  ; BX = bytes to the end of the ring
    cmp cx, bx
    jbe .one
    push bx                     ; the head: up to PSTOP
    mov ax, bx
    mov bx, dx
    call ne_dma_read
    pop bx
    sub cx, bx                  ; ...and the tail, from PSTART
    mov ax, cx
    mov bh, [eth_pstart]
    xor bl, bl
    call ne_dma_read
    jmp short .out
.one:
    mov ax, cx
    mov bx, dx
    call ne_dma_read
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_rx - take ONE frame off the ring
; out: CF=0 and CX = its length in eth_rxb; CF=1 = the ring is empty
;
; The four-byte header the card prepends is status / next page / length, and
; the length INCLUDES those four bytes. A length outside 64..1518 or a next
; page outside the ring means the ring has been corrupted - which happens for
; real when a receive overruns - and the answer is to reset it rather than to
; follow the pointer.
; -----------------------------------------------------------------------------
ne_rx:
    push ax
    push bx
    push dx
    push di

    mov al, NE_ISR              ; an overrun is recovered before anything else:
    call ne_in                  ; the ring's pointers cannot be trusted after
    test al, NE_OVW             ; one, and every frame in it is lost anyway
    jz  .nov
    call ne_overflow
    jmp .empty
.nov:
    mov ax, (NE_PRX | NE_RXE) << 8 | NE_ISR
    call ne_out

    call ne_curr
    cmp al, [eth_next]
    je  .empty

    mov bh, [eth_next]          ; the four-byte header
    xor bl, bl
    mov ax, 4
    mov di, eth_rxh
    call ne_ring_read

    mov al, [eth_rxh+1]         ; the next page must be inside the ring
    cmp al, [eth_pstart]
    jb  .bad
    cmp al, [eth_pstop]
    jae .bad
    mov dx, [eth_rxh+2]         ; the length, including the four
    cmp dx, 64
    jb  .bad
    cmp dx, NE_FRAME + 4
    ja  .bad

    sub dx, 4
    mov bh, [eth_next]          ; ...and the body, four bytes in
    mov bl, 4
    mov ax, dx
    mov di, eth_rxb
    call ne_ring_read

    mov al, [eth_rxh+1]         ; consume it: our pointer forward, and BNRY
    mov [eth_next], al          ; one page behind it
    dec al
    cmp al, [eth_pstart]
    jae .bnry
    mov al, [eth_pstop]
    dec al
.bnry:
    mov ah, al
    mov al, NE_BNRY
    call ne_out

    mov cx, dx
    clc
    jmp short .out
.bad:
    call ne_overflow            ; the same recovery: the ring is not coherent
.empty:
    stc
.out:
    pop di
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_overflow - the documented ring-overflow recovery
;
; Everything in the ring is thrown away, which is the honest outcome: the
; frames are interleaved with the ones the card could not fit and there is no
; way to tell where one ends. TCP retransmits; ARP and DNS retry.
;
; The order is the datasheet's and none of it is optional: stop, wait for the
; stop to take, clear the remote byte count, put the card in loopback so
; nothing new arrives while the pointers move, restart, reset the ring, take
; it out of loopback.
; -----------------------------------------------------------------------------
ne_overflow:
    push ax
    push cx
    mov ax, (NE_STP | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov cx, 2000
.stopped:
    mov al, NE_ISR
    call ne_in
    test al, NE_RST
    jnz .go
    loop .stopped
.go:
    mov ax, 0x00 << 8 | NE_RBCR0
    call ne_out
    mov ax, 0x00 << 8 | NE_RBCR1
    call ne_out
    mov ax, 0x02 << 8 | NE_TCR              ; loopback while the ring moves
    call ne_out
    mov ax, (NE_STA | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov al, NE_BNRY
    mov ah, [eth_pstart]
    call ne_out
    mov ah, [eth_pstart]
    inc ah
    mov [eth_next], ah
    mov ax, (NE_PAGE1 | NE_STA | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov al, NE_CURR
    mov ah, [eth_next]
    call ne_out
    mov ax, (NE_STA | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    mov ax, NE_OVW << 8 | NE_ISR
    call ne_out
    mov ax, 0x00 << 8 | NE_TCR
    call ne_out
    inc word [eth_novw]
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_tx - put CX bytes of eth_txb on the wire
; out: CF=1 = the card never finished (it has gone away)
;
; A frame shorter than 60 bytes is padded, because the card will not send a
; runt and the pad is what an ARP request needs. The wait is for the previous
; transmit rather than this one: nothing here needs the completion, and
; waiting for it costs a millisecond of a machine that has 209 of them a tick.
; -----------------------------------------------------------------------------
ne_tx:
    push ax
    push bx
    push cx
    push si
    cmp cx, NE_MINFRAME
    jae .len
    mov si, eth_txb             ; pad with zeros, not with what was there
    add si, cx
    mov ax, cx
    mov cx, NE_MINFRAME
    sub cx, ax
    xor al, al
.pad:
    mov [si], al
    inc si
    loop .pad
    mov cx, NE_MINFRAME
.len:
    push cx
    mov bx, NE_TXSPIN           ; the PREVIOUS transmit must be done first
.busy:
    mov al, NE_CR
    call ne_in
    test al, NE_TXP
    jz  .free
    dec bx
    jnz .busy
    pop cx
    stc
    jmp short .out
.free:
    mov si, eth_txb
    mov bh, [eth_txpg]
    xor bl, bl
    mov ax, cx
    call ne_dma_write
    pop cx
    mov al, NE_TPSR
    mov ah, [eth_txpg]
    call ne_out
    mov al, NE_TBCR0
    mov ah, cl
    call ne_out
    mov al, NE_TBCR1
    mov ah, ch
    call ne_out
    mov ax, (NE_STA | NE_TXP | NE_RD_ABORT) << 8 | NE_CR
    call ne_out
    inc word [eth_ntx]
    clc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret
