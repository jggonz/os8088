; =============================================================================
; os8088 - rtl8139.inc: the Realtek RTL8139 backend (SPEC.md 125.4)
;
; The family: 8139, 8139A/B/C and the C+ in its normal-mode register set, PCI
; 10EC:8139. POLLED, as SPEC.md 72.2.1 - IMR is 0 and nothing is hooked. One
; receive ring with WRAP, one transmit buffer, one heap claim for both.
;
; WHAT THE SPEC SAID AND THE CODE CHANGED (SPEC.md 125.4 follows this): the
; 32-bit registers (RBSTART, RCR, TCR, TSAD, TSD) are written - and TSD read -
; as ONE dword, with the 66h operand-size prefix on an `out dx, ax`. Two word
; writes were the first reading of "16-bit only", and a model that implements
; those registers for a dword access alone (QEMU's) would never see them. The
; prefix executes on the 386+ this driver is gated to (pci_gate), so the image
; stays `cpu 8086`: the byte is data to the assembler.
; =============================================================================

RT_IDR0         equ 0x00        ; station address, six bytes
RT_MAR0         equ 0x08        ; multicast hash, eight bytes
RT_TSD0         equ 0x10        ; transmit status, four dwords
RT_TSAD0        equ 0x20        ; transmit start address, four dwords
RT_RBSTART      equ 0x30        ; receive buffer start, dword
RT_CR           equ 0x37        ; command, byte
RT_CAPR         equ 0x38        ; current address of packet read, word
RT_IMR          equ 0x3C        ; interrupt mask, word
RT_ISR          equ 0x3E        ; interrupt status, word, write-one-to-clear
RT_TCR          equ 0x40        ; transmit configuration, dword
RT_RCR          equ 0x44        ; receive configuration, dword
RT_CONFIG1      equ 0x52        ; byte

RTC_BUFE        equ 0x01        ; CR: receive buffer empty
RTC_TE          equ 0x04
RTC_RE          equ 0x08
RTC_RST         equ 0x10

RTI_OVW         equ 0x0050      ; ISR: RXOVW (bit 4) | FOVW (bit 6)

RTS_ROK         equ 0x01        ; the packet header's status: received OK
RTD_TUN         equ 0x4000      ; TSD (low word): transmit underrun
RTD_TOK         equ 0x8000      ; TSD (low word): transmit OK
RTD_TABT        equ 0x4000      ; TSD (HIGH word): transmit aborted

RT_RCR_VAL      equ 0xE78A      ; RXFTH none | MXDMA unlimited | RBLEN 8K |
                                ; WRAP | AB | APM - ours, and broadcast
RT_TCR_HI       equ 0x0300      ; IFG standard
RT_TCR_LO       equ 0x0700      ; MXDMA unlimited
RT_ERTXTH       equ 8           ; TSD bits 21:16: early transmit at 256 bytes
RT_CAPR0        equ 0xFFF0      ; CAPR with the ring empty: 0 - 16

NIC_FRAME       equ 1514        ; the largest frame the stack takes
RT_RING         equ 8192        ; RBLEN = 8K
RT_RINGMASK     equ 0x1FFC      ; a read offset: modulo the ring, dword-aligned
RT_TXOFF        equ RT_RING + 16 + 1536     ; the ring, its slack, the overrun
RT_CLAIM        equ RT_TXOFF + 1536         ; ...and one transmit buffer
RT_MINFRAME     equ 60          ; a runt is padded to this
RT_MAXLEN       equ 1536 + 4    ; a header's length (frame + CRC) past this is
                                ; not a frame: the ring has been corrupted.
                                ; Linux 8139too's bound (MAX_ETH_FRAME_SIZE),
                                ; not 1518 + 4: a VLAN-tagged full frame is
                                ; 1522 + 4, a good frame we skip, and throwing
                                ; the whole ring away for it would lose every
                                ; frame queued behind it
RT_EARLY        equ 0xFFF0      ; a header's length while the chip is still
                                ; copying the frame out of its FIFO (Linux:
                                ; "should never happen" with early receive
                                ; off, and handled anyway): not yet a frame
RT_SPIN         equ 0xFFFF      ; every wait: 65,535 reads of a PCI register -
                                ; ~16 ms on a Pentium (0.25 us a read) and
                                ; ~130 ms on a 386SX, against a reset that
                                ; takes microseconds and a 1,518-byte frame
                                ; that takes 1.2 ms at 10 Mbit. A dead card is
                                ; the only case that spends it all

%if RT_TXOFF & 3
 %error "rtl8139: the transmit buffer must be 4-byte aligned"
%endif
%if (RT_RING - 4) + 4 + RT_MAXLEN > RT_TXOFF
 %error "rtl8139: a frame whose header is the ring's last dword must end in the overrun area"
%endif

; -----------------------------------------------------------------------------
; the dword door. DX = the port. Every other register kept.
; rt_outd: BX:AX -> the dword.   rt_ind: the dword -> BX:AX.
;
; **cli ACROSS THE TOP HALF**, vmmouse.asm's vmm_atom argument: on a real-mode
; kernel (kern_big, kern_emu, and kern_mega's boot attach) sch_switch saves
; 16-bit registers only, so a tick between the `pop eax` and the `out` could
; run a task or a BIOS handler that spends EAX's top half - XMEM.DRV's movers,
; SeaBIOS's C - and the card would be handed some other dword. Two
; instructions of latency.
; -----------------------------------------------------------------------------
rt_outd:
    pushf
    cli
    push bx
    push ax
    db 0x66
    pop ax                      ; pop eax: AX the low word, BX's the high
    db 0x66
    out dx, ax                  ; out dx, eax
    popf
    ret

rt_ind:
    pushf
    cli
    db 0x66
    in ax, dx                   ; in eax, dx
    db 0x66
    push ax                     ; push eax
    pop ax
    pop bx
    popf
    ret

; -----------------------------------------------------------------------------
; rt_port - DX = [eth_base] + AL, the register's offset (every RT_* is below
; 80h, so `cbw` is its zero AH). AH = 0 after; everything else kept. A byte
; load at each of the eighteen calls where a word load was (size pass 18)
; -----------------------------------------------------------------------------
rt_port:
    cbw
    mov dx, [eth_base]
    add dx, ax
    ret

; -----------------------------------------------------------------------------
; rt_rstwait - DX = the CR port: wait, bounded, for RST to clear. Spends AL
; and CX: rt_init and rt_stop, its two callers, load both afresh after it
; -----------------------------------------------------------------------------
rt_rstwait:
    mov cx, RT_SPIN
.w:
    in  al, dx
    test al, RTC_RST
    loopnz .w                   ; ...until RST reads clear, or the bound
    ret

; -----------------------------------------------------------------------------
; rt_claim - the receive ring and the transmit buffer: ONE pinned claim
; out: CF=1 = no heap for it. [rt_seg] and the linear base [rt_lin] on success.
; Spends AX, BX, CX (PCEV_CLAIM's one caller, pce_find, may); DX is kept.
;
; Pinned (SPEC.md 66: a claim is born unmovable) and CONVENTIONAL - MEMP_CONV,
; because this image is OS88_DRV_HICLAIM and a claim with no head would
; otherwise be offered the pool above 1MB, whose blocks the pool compactor
; rebases: the card is a bus master and writes the ring behind the CPU's back,
; so the claim must stay where the address we gave it says. Its bus address is
; its linear base (OSAPI_SEG SEG_LIN) - kern_mega does not page, so linear is
; physical - and [rt_par] is that as a paragraph, for rt_es.
; -----------------------------------------------------------------------------
rt_claim:
    push dx
    mov ax, OS88_BYTES_PARA(RT_CLAIM)
    mov cx, MEMP_CONV           ; no head, and below 1MB
    mov bl, MEMP_CLAIM | MEMP_CPU
    call OSAPI_MEM_P            ; DX = the base
    jc  .out
    mov [rt_seg], dx
    mov bl, SEG_LIN
    call OSAPI_SEG              ; DX:AX = its linear base
    mov [rt_lin], ax
    mov [rt_lin+2], dx
    mov cl, 4                   ; ...and as a paragraph: below 1MB, so DX is
    shr ax, cl                  ; 0..0Fh and lands in the top nibble
    mov cl, 12
    shl dx, cl
    or  ax, dx
    mov [rt_par], ax
    mov word [rt_win], 0
    clc
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; rt_es - ES = the ring's claim, as the mode we are running in names it
; out: CF=0 and ES; CF=1 = kern_mega had no window to give (its LDT full) -
;      the caller treats it as a ring with nothing in it / a send that failed.
;      Spends AX, BX and DX - rt_rx and rt_tx, its two callers, bank them and
;      load all three afresh after it; CX (rt_tx's length) is kept.
;
; A BOOT ATTACH CLAIMS IN REAL MODE (SYSTEM.CFG's drivers load before
; mega_enter), so [rt_seg] is a PARAGRAPH - and the claim is pinned, so
; mem_xlatf tells only the kernel's half that its name became a selector
; (KERN-MEGA-MEMORY-PLAN MC7.4): a `mov es, [rt_seg]` in protected mode is a
; #GP. EMS.DRV's em_fsel is the shape: ask OSAPI_SEG's SEG_WIN for the
; physical paragraph. Every real-mode kernel answers the paragraph itself, and
; it is asked again next time; under protected mode it answers a window of
; ours, and the ring never moves, so that window is kept for good (rt_stop
; gives it back; drv_release would anyway).
; -----------------------------------------------------------------------------
rt_es:
    mov dx, [rt_win]
    or  dx, dx
    jnz .have                   ; (CF = 0: the `or`)
    push cx
    mov dx, [rt_par]
    xor ax, ax                  ; a new window
    mov cx, RT_CLAIM
    mov bl, SEG_WIN
    call OSAPI_SEG
    pop cx                      ; (POP keeps CF)
    jc  .out
    cmp dx, [rt_par]            ; (a window is an LDT selector, bit 2 set,
    je  .have                   ; and a claim's paragraph is a multiple of
                                ; 16, so the two can never be equal) real
                                ; mode: the paragraph is the answer, CF = 0
    mov [rt_win], dx            ; protected mode: a window, ours for good
    clc
.have:
    mov es, dx
.out:
    ret

; -----------------------------------------------------------------------------
; rt_init - bring the card up and start receiving (nic_init; cannot fail)
; in:  [eth_base] and the claim
; out: [eth_mac], [eth_word] = 1, the receiver and transmitter on, IMR = 0
; Spends AX, BX, CX and SI - eth_attach, nic_init's one caller, banks or
; reloads all four (ethdrv.inc's seam header); DX is kept, the attach's
; caller holding it
; -----------------------------------------------------------------------------
rt_init:
    push dx
    mov al, RT_CONFIG1
    call rt_port
    xor al, al
    out dx, al                  ; CONFIG1 = 0: powered on
    mov al, RT_CR
    call rt_port
    mov al, RTC_RST
    out dx, al
    call rt_rstwait
    mov dx, [eth_base]          ; IDR0..5: the station address
    mov si, eth_mac
    mov cx, 6
.mac:
    in  al, dx
    mov [si], al
    inc si
    inc dx
    loop .mac
    mov al, RT_MAR0
    call rt_port
    mov cx, 8
    xor al, al
.mar:                           ; no multicast: the stack has no use for it
    out dx, al
    inc dx
    loop .mar
    mov byte [eth_word], 1      ; (the ring ladder's "wide" arm: nic ring sizes)
    mov byte [rt_txn], 0
    call rt_start
    pop dx
    ret

; -----------------------------------------------------------------------------
; rt_start - point the card at the ring and turn the receiver and transmitter
; on. Shared by rt_init and the overflow recovery. Spends AX, BX and DX: both
; callers bank all three (rt_init DX, its caller the rest; rt_rx all three).
;
; The ORDER is Linux 8139too's: RCR and TCR only AFTER CR = RE|TE, because the
; chip ignores their thresholds while the receiver and transmitter are off.
; -----------------------------------------------------------------------------
rt_start:
    mov al, RT_RBSTART
    call rt_port
    mov ax, [rt_lin]
    mov bx, [rt_lin+2]
    call rt_outd                ; RBSTART = the ring
    mov al, RT_IMR
    call rt_port
    xor ax, ax
    out dx, ax                  ; IMR = 0: we poll
    mov al, RT_ISR
    call rt_port
    mov ax, 0xFFFF
    out dx, ax                  ; every cause cleared (write-one-to-clear)
    mov al, RT_CR
    call rt_port
    mov al, RTC_RE | RTC_TE
    out dx, al
    mov al, RT_RCR
    call rt_port
    xor bx, bx
    mov ax, RT_RCR_VAL
    call rt_outd
    mov al, RT_TCR
    call rt_port
    mov bx, RT_TCR_HI
    mov ax, RT_TCR_LO
    call rt_outd
    mov al, RT_CAPR
    call rt_port
    mov ax, RT_CAPR0
    out dx, ax                  ; CAPR = 0 - 16: the ring is empty
    mov word [rt_off], 0
    ret

; -----------------------------------------------------------------------------
; rt_rxreset - throw the ring away and start the receiver again (SPEC.md 72.2's
; overflow rule). The transmitter is left on. Spends AX, BX and DX (rt_rx,
; its one caller, banks them); CX is kept, being rt_rx's.
; -----------------------------------------------------------------------------
rt_rxreset:
    push cx
    mov al, RT_CR
    call rt_port
    mov al, RTC_TE              ; receiver off...
    out dx, al
    mov cx, RT_SPIN
.off:
    in  al, dx
    test al, RTC_RE             ; ...and wait for it to say so
    jz  .go
    loop .off
.go:
    pop cx
    jmp rt_start                ; ...and on again at the top of the ring

; -----------------------------------------------------------------------------
; rt_stop - silence the card, then give the claim back (nic_stop; cannot fail)
;
; The DMA stops BEFORE the memory is freed: a card left running writes into
; whatever the heap gives that memory to next. Spends AX, BX, CX and DX:
; eth_detach, nic_stop's one caller, has banked everything (os88_ent)
; -----------------------------------------------------------------------------
rt_stop:
    mov al, RT_IMR
    call rt_port
    xor ax, ax
    out dx, ax
    mov al, RT_CR
    call rt_port
    xor al, al
    out dx, al                  ; receiver and transmitter off
    mov al, RTC_RST
    out dx, al                  ; ...and the chip reset
    call rt_rstwait
    mov dx, [rt_win]            ; rt_es's window, if protected mode made one
    or  dx, dx
    jz  .nowin
    mov word [rt_win], 0
    mov bl, SEG_FREE
    call OSAPI_SEG
.nowin:
    mov dx, [rt_seg]
    or  dx, dx
    jz  .out
    mov word [rt_seg], 0
    call OSAPI_MEM_FREE         ; a boot claim named by its PARAGRAPH is not
                                ; found under protected mode (mem_find_own
                                ; compares selectors there) and is refused:
                                ; drv_release's sweep of everything this
                                ; driver owns frees it a moment later, and the
                                ; card was silenced above, so nothing writes it
.out:
    ret

; -----------------------------------------------------------------------------
; rt_rx - take ONE frame off the ring (nic_rx)
; out: CF=0 and CX = its length in eth_rxb; CF=1 = nothing (empty, a frame too
;      long for eth_rxb that was skipped, or a ring that was reset)
; Every register but CX and the flags kept.
; -----------------------------------------------------------------------------
rt_rx:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    mov al, RT_ISR
    call rt_port
    in  ax, dx
    test ax, RTI_OVW
    jz  .nov
    and ax, RTI_OVW
    out dx, ax                  ; the ring is not to be trusted after an
    jmp short .reset            ; overrun, and every frame in it is lost
.nov:
    mov al, RT_CR
    call rt_port
    in  al, dx
    test al, RTC_BUFE
    jnz .empty
    call rt_es
    jc  .empty                  ; no window to read it through: next call
    mov bx, [rt_off]
    mov ax, [es:bx]             ; the header: status...
    mov dx, [es:bx+2]           ; ...and the length, frame + 4 bytes of CRC
    cmp dx, RT_EARLY
    je  .empty                  ; still being written: the next call takes it
    test al, RTS_ROK
    jz  .reset
    cmp dx, 64
    jb  .reset
    cmp dx, RT_MAXLEN
    ja  .reset
    mov si, dx
    sub si, 4
    cmp si, NIC_FRAME
    ja  .skip                   ; a good frame the stack has no room for
    push ds
    mov ax, es                  ; the claim
    push ds
    pop es                      ; ES = ours
    mov ds, ax                  ; DS = the claim
    mov cx, si                  ; the frame's length, as tested above
    lea si, [bx+4]              ; the frame follows its header, and the
    mov di, eth_rxb             ; overrun area means it is never split
    push cx
    inc cx
    shr cx, 1
    cld
    rep movsw
    pop cx                      ; CX = the frame's length
    pop ds
    call rt_consume
    clc
    jmp short .out
.skip:
    call rt_consume
    jmp short .empty
.reset:
    inc word [eth_novw]
    call rt_rxreset
.empty:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rt_consume - BX = this packet's offset, DX = its header length: step past it
; and tell the chip. The next packet is at (off + len + 4 + 3) AND NOT 3, modulo
; the ring, and CAPR is that minus 16, the chip's documented bias. Spends AX,
; BX and DX: rt_rx, its one caller, banks all three and keeps CX, the length
; -----------------------------------------------------------------------------
rt_consume:
    add bx, dx
    add bx, 4 + 3
    and bx, RT_RINGMASK
    mov [rt_off], bx
    sub bx, 16
    mov al, RT_CAPR
    call rt_port
    mov ax, bx
    out dx, ax
    ret

; -----------------------------------------------------------------------------
; rt_tx - put CX bytes of eth_txb on the wire (nic_tx)
; out: CF=1 = the card never finished (it has gone away). All registers kept.
;
; The four TSD/TSAD slots are used in turn - the chip requires it - all
; pointing at the one buffer. The stack is stop-and-wait, so we wait for THIS
; frame; a TUN or TABT ends the wait as well as TOK, and is the wire's loss
; (TCP retransmits), not the card's absence.
; -----------------------------------------------------------------------------
rt_tx:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp cx, RT_MINFRAME
    jae .len
    mov si, cx                  ; pad with zeros, not with what was there
.pad:
    mov byte [eth_txb+si], 0
    inc si
    cmp si, RT_MINFRAME
    jb  .pad
    mov cx, si                  ; = RT_MINFRAME
.len:
    call rt_es
    jc  .fail                   ; no window to write it through
    mov si, eth_txb
    mov di, RT_TXOFF
    push cx
    inc cx
    shr cx, 1
    cld
    rep movsw
    pop cx
    mov bl, [rt_txn]
    xor bh, bh
    mov si, bx
    shl si, 1
    shl si, 1                   ; SI = 4 * the slot
    inc bl
    and bl, 3
    mov [rt_txn], bl
    mov ax, [rt_lin]            ; TSAD[n] = the buffer's bus address
    mov bx, [rt_lin+2]
    add ax, RT_TXOFF
    adc bx, 0
    push ax
    mov al, RT_TSAD0
    call rt_port
    add dx, si
    pop ax
    call rt_outd
    mov al, RT_TSD0             ; TSD[n] = ERTXTH and the size, ONE write:
    call rt_port                ; OWN clear in it is what starts the send
    add dx, si
    mov bx, RT_ERTXTH
    mov ax, cx
    call rt_outd
    inc word [eth_ntx]
    mov cx, RT_SPIN
.w:
    call rt_ind
    test ax, RTD_TOK | RTD_TUN
    jnz .done
    test bx, RTD_TABT
    jnz .done
    loop .w
.fail:
    stc
    jmp short .out
.done:
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rt_name:    db 'RTL8139', 0
rt_vtab:    dw rt_claim, rt_init, rt_rx, rt_tx, rt_stop
