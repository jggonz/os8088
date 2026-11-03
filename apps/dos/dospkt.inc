; =============================================================================
; os8088 - apps/dos/dospkt.inc
;
; THE PACKET DRIVER (SPEC.md 96.23) - the part of it that runs once a wire is
; found. ONE SOURCE, TWO SHAPES (SPEC.md 96.23.11):
;   - `%include`d by apps/dos/dos.asm in a build with no part table
;     (DOS_NETINLINE: build/dos.o88, the DOSTRACE build), where it is the
;     host's own code over the host's own bss, as it always was;
;   - `%include`d by apps/dos/dosnetp.asm (PN_ROOT), part 4 of DOS.O88, where
;     CS = DS = the network claim and every `dos_pkt_*` cell is the claim's.
; What DECIDES and CLAIMS - dos_pkt_want, dos_pkt_bufs, dos_pkt_reloc,
; dos_pkt_rloc - stays in dos.asm in both.
; =============================================================================

dos_pkt_name: db 'os8088 ETHER', 0

; -----------------------------------------------------------------------------
; dos_pkt_entry - THE VECTOR (SPEC.md 96.23.2)
;
; The first three bytes are a jump and the signature starts at offset 3, which
; is the whole of how a client finds this. A short jump would be two and put
; the signature one byte early, so the assertion below is not decoration - it
; is the one thing in this file a reader cannot check by eye.
; -----------------------------------------------------------------------------
dos_pkt_entry:
    jmp strict near dos_pkt_go
dos_pkt_sig:
    db 'PKT DRVR', 0
%if dos_pkt_sig - dos_pkt_entry != 3
 %error "the PKT DRVR signature must begin at offset 3 of the handler (Crynwr)"
%endif

; -----------------------------------------------------------------------------
; The gate, in dos_int21's shape and for its reasons (SPEC.md 96.7.1): entered
; on the CLIENT's stack with the client's segment registers, so the first thing
; it does is reach its own data through CS, and the carry flag it returns is
; the one in the FLAGS image the `int` pushed.
;
;   [bp]=DS [bp+2]=BP [bp+4]=IP [bp+6]=CS [bp+8]=FLAGS
;   [bp-2]=SI [bp-4]=DI [bp-6]=ES
;
; SI, DI and ES are banked BELOW bp and restored from there, so a handler that
; leaves the stack at any depth still returns the client's registers - and
; `driver_info` writes its DS:SI answer into [bp] and [bp-2] rather than into
; the live registers, which is the same trick AH=35h uses one section up.
; -----------------------------------------------------------------------------
dos_pkt_go:
    sti
    push bp
    push ds
    mov bp, sp
    push si
    push di
    push es
    push ds                         ; **THE CLIENT'S DS, BANKED FROM THE
    push cs                         ; REGISTER** (SPEC.md 96.23.9) - the
    pop ds                          ; version that read it back out of [bp]
    pop word [dos_pkt_cds]          ; answered our OWN segment, and `[bp]` is
                                    ; SS-relative so it was never going to be
                                    ; checkable by eye. This pops the value
                                    ; pushed one instruction earlier, with DS
                                    ; already ours so the store lands here

    call dos_pkt_poll               ; **DRAIN FIRST** (SPEC.md 96.23.4): a
                                    ; client in a send loop receives as it
                                    ; sends, without waiting for a tick

    cmp ah, 1
    je .info
    cmp ah, 2
    je .access
    cmp ah, 3
    je .release
    cmp ah, 4
    je .send
    cmp ah, 5
    je .term
    cmp ah, 6
    je .getaddr
    cmp ah, 20
    je .setmode
    cmp ah, 21
    je .getmode
    cmp ah, 24
    je .stats
    mov dh, PKE_BADCMD              ; a client asking for a feature it can
    jmp .err                        ; live without expects exactly this

; --- AH=1 driver_info --------------------------------------------------------
; out BX=version CH=class DX=type CL=number DS:SI=name AL=functionality
.info:
    mov bx, PKT_VERSION
    mov ch, PKT_CLASS
    mov dx, PKT_TYPE
    mov cl, 0                       ; interface number
    mov al, PKT_FUNC
    mov word [bp-2], dos_pkt_name   ; the BANKED SI and DS, not the live ones:
    mov [bp], cs                    ; the exit path restores both from here
    jmp .ok

; --- AH=2 access_type --------------------------------------------------------
; in AL=if_class BX=if_type DL=if_number DS:SI=type CX=typelen ES:DI=receiver
; out AX = a handle
;
; DS:SI is the CLIENT's - our own DS is CS by now - so the ethertype is read
; through the banked [bp]. ES and DI are still the client's in the live
; registers, which is what makes the receiver pointer a plain bank.
.access:
    cmp al, PKT_CLASS
    jne .noclass
    cmp bx, PKT_TYPE                ; 0FFFFh is the spec's "any type of card"
    je .clsok
    cmp bx, 0xFFFF
    jne .notype
.clsok:
    or dl, dl                       ; one card, number 0
    jz .numok
    cmp dl, 0xFF
    jne .nonum
.numok:
    push cx
    push si
    xor ax, ax                      ; AX = the ethertype, 0 = every frame
    or cx, cx
    jz .anytype
    cmp cx, 2                       ; **A LENGTH OTHER THAN 2 IS REFUSED**
    jne .badtype                    ; rather than read short: an ethertype is
    push es                         ; two bytes and a client that passed a
    mov es, [bp]                    ; longer one means a protocol this card
    mov ah, [es:si]                 ; layer does not have (802.2 LSAPs)
    mov al, [es:si+1]
    pop es
.anytype:
    call dos_pkt_hnew               ; BX = the row, or CF=1 = full
    jc .nospace
    mov [bx+PKT_HTYPE], ax
    mov [bx+PKT_HRCVO], di
    mov ax, es
    mov [bx+PKT_HRCVS], ax
    mov byte [bx+PKT_HUSED], 1
    call dos_pkt_claim              ; **THE WIRE, ON THE FIRST HANDLE**
    jc .noclaim                     ; (SPEC.md 96.23.5) - not at bracket entry
    mov ax, bx                      ; the handle IS the row address: it is
    pop si                          ; ours to choose and this makes every
    pop cx                          ; later lookup a bounds check instead of
    jmp .ok                         ; a multiply
.badtype:
    pop si
    pop cx
    mov dh, PKE_BADTYPE
    jmp .err
.nospace:
    pop si
    pop cx
    mov dh, PKE_NOSPACE
    jmp .err
.noclaim:
    mov byte [bx+PKT_HUSED], 0      ; the row goes back: a handle that cannot
    pop si                          ; receive is worse than a refusal
    pop cx
    mov dh, PKE_NOSPACE
    jmp .err
.noclass:
    mov dh, PKE_NOCLASS
    jmp .err
.notype:
    mov dh, PKE_NOTYPE
    jmp .err
.nonum:
    mov dh, PKE_NONUM
    jmp .err

; --- AH=3 release_type -------------------------------------------------------
; in BX = the handle
.release:
    call dos_pkt_hchk
    jc .badhand
    mov byte [bx+PKT_HUSED], 0
    call dos_pkt_idle               ; the last handle takes the claim with it
    jmp .ok
.badhand:
    mov dh, PKE_BADHAND
    jmp .err

; --- AH=4 send_pkt -----------------------------------------------------------
; in DS:SI = the frame, CX = its length. The client's DS again.
.send:
    cmp cx, NET_EHSIZE
    jb .cantsend
    cmp cx, NET_FRAME
    ja .cantsend
    ; --- **THERE IS NO STAGING COPY** (SPEC.md 96.23.8) --------------------
    ; NETV_RAWTX takes the segment (SPEC.md 72.22.4), so the client's own
    ; buffer is handed over where it lies: DX = the DS the `int` pushed, SI =
    ; the offset it was called with. The driver copies into eth_txb either
    ; way, so a staging copy of ours was a SECOND copy of 1,514 bytes on a
    ; 4.77MHz machine and 1,514 bytes of claim to hold it - and it was where
    ; a whole class of segment bug lived, because it was the one place this
    ; package addressed the client's memory itself.
    mov dx, [dos_pkt_cds]           ; the CLIENT's DS, banked at the gate
    push cx                         ; ...and the LENGTH, because get_statistics
                                    ; wants it after the route has run and
                                    ; OSAPI_DRV_CALL publishes CX as the
                                    ; driver's to define
    cmp byte [dos_pkt_xl], 0
    jne .xlate                      ; the cable carries no frames (SPEC.md
                                    ; 72.22.3), so they are TRANSLATED
    mov bh, DRVC_NET
    mov bl, NETV_RAWTX
    call OSAPI_DRV_CALL
    jnc .sent
    pop cx
    jmp short .cantsend
.xlate:
    ; --- the translation reads the frame in OUR segment --------------------
    ; dn_tx and everything under it use no segment override, because every
    ; other byte they touch is ours. So this is the one copy the card path
    ; does not make - 42 to 1514 bytes, against a wire that moves 3,741 a
    ; second, which is not the cost that decides anything here.
    push cx
    push si
    push di
    push es
    push ds
    mov es, [dos_pkt_bseg]          ; ES:DI is the CLAIM's staging frame and
    mov di, dos_pkt_txs             ; DS:SI the client's own buffer
    mov ds, dx
    call dos_pkt_copy               ; DS:SI -> ES:DI, CX bytes
    pop ds
    pop es
    pop di
    pop si
    pop cx
    mov si, dos_pkt_txs
    call dn_tx
.sent:
    ; --- get_statistics' OWN numbers, and they are the real ones -----------
    ; Both routes end here so the count is written once. It is the client's
    ; own frame length that is added, not the driver's padded one: a Crynwr
    ; client asked for these bytes and mTCP's PKTTOOL prints them back.
    pop cx
    add word [dos_pkt_stats+PKS_POUT], 1
    adc word [dos_pkt_stats+PKS_POUT+2], 0
    add word [dos_pkt_stats+PKS_BOUT], cx
    adc word [dos_pkt_stats+PKS_BOUT+2], 0
    jmp .ok
.cantsend:
    mov dh, PKE_CANTSEND
    jmp .err

; --- AH=5 terminate ----------------------------------------------------------
.term:
    call dos_pkt_hchk
    jc .badhand
    call dos_pkt_rawdrop            ; NOT dos_pkt_shut: the buffers are the
    jmp .ok                         ; bracket's and a program that terminates
                                    ; the driver may still open it again

; --- AH=6 get_address --------------------------------------------------------
; in BX = handle, ES:DI = a buffer, CX = its size; out CX = bytes written
.getaddr:
    call dos_pkt_hchk
    jc .badhand
    cmp cx, 6
    jb .badhand
    push si
    push di
    mov si, dos_pkt_mac             ; banked by the claim (SPEC.md 72.22), so
    mov cx, 6                       ; this costs no driver call at all
    call dos_pkt_copy             ; DS:SI -> ES:DI
    pop di
    pop si
    mov cx, 6
    jmp .ok

; --- AH=20 / 21 set and get receive mode -------------------------------------
; 3 is "every frame addressed to me, plus broadcast", which is the mode
; ne_init leaves the card in and the only one this driver can honestly offer.
.setmode:
    cmp cx, 3
    jne .badmode
    mov byte [dos_pkt_mode], 3
    jmp .ok
.badmode:
    mov dh, PKE_BADTYPE
    jmp .err
.getmode:
    xor ax, ax
    mov al, [dos_pkt_mode]
    jmp .ok

; --- AH=24 get_statistics ----------------------------------------------------
; out DS:SI = six dwords: packets in, packets out, bytes in, bytes out,
;     errors in, packets dropped.
.stats:
    mov word [bp-2], dos_pkt_stats
    mov [bp], cs
    jmp .ok

.ok:
    and word [bp+8], 0xFFFE         ; CF=0 in the RETURNED flags, not the live
    jmp short .leave                ; ones - the iret would discard those
.err:
    or word [bp+8], 1
.leave:
    ; STKBALANCE-OK: dos_int21's arrangement and its reasons (SPEC.md 96.7.1)
    ; - the frame is restored from `bp`, so this gate's promise does not rest
    ; on every handler above being balanced.
    mov si, [bp-2]
    mov di, [bp-4]
    mov es, [bp-6]
    mov sp, bp
    pop ds
    pop bp
    iret

; -----------------------------------------------------------------------------
; dos_pkt_hnew / dos_pkt_hchk - the handle table
;
; **A HANDLE IS THE ROW'S OWN ADDRESS**, which is ours to choose and makes
; every later lookup a bounds check rather than a multiply. hchk is what makes
; that safe: a client handing back a number it made up is refused before it
; can index anything.
; out: hnew  CF=0 with BX = a free row, CF=1 = the table is full
;      hchk  CF=0 = BX is one of ours and in use
; -----------------------------------------------------------------------------
dos_pkt_hnew:
    push cx
    mov bx, dos_pkt_htab
    mov cx, PKT_NHAND
.l:
    cmp byte [bx+PKT_HUSED], 0
    je .got
    add bx, PKT_HSIZE
    loop .l
    pop cx
    stc
    ret
.got:
    pop cx
    clc
    ret

dos_pkt_hchk:
    push ax
    push dx
    mov ax, bx
    sub ax, dos_pkt_htab            ; below the table wraps to a huge unsigned,
    cmp ax, PKT_NHAND * PKT_HSIZE   ; so one compare catches both ends
    jae .no
    mov dl, PKT_HSIZE               ; ...and it must be ON a row boundary, not
    div dl                          ; merely inside the table: AX is under 28,
    or ah, ah                       ; so a BYTE divide, AH = the remainder.
    jnz .no                         ; (It was `div cx`, and CX is the CLIENT's
                                    ; - get_address's buffer size, read after
                                    ; this returns - which it overwrote with 7;
                                    ; SPEC.md 96.44.16)
    cmp byte [bx+PKT_HUSED], 0
    je .no
    pop dx
    pop ax
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dos_pkt_claim / dos_pkt_idle / dos_pkt_shut - the raw claim's lifetime
;
; Taken on the FIRST handle and dropped when the last one goes or the bracket
; ends (SPEC.md 96.23.5). A DOS program that never asks for a packet driver
; leaves our own stack running, which is most of them.
; -----------------------------------------------------------------------------
dos_pkt_claim:
    cmp byte [dos_pkt_raw], 0
    jne .have
    ; --- **THE CABLE HAS NO RAW CLAIM TO TAKE** (SPEC.md 96.26.4) ----------
    ; NETV_RAW is one of the three verbs NET.DRV refuses (72.22.3), so asking
    ; for it here would fail every access_type on the machine this translation
    ; exists for. There is nothing to claim: no ring, no card, and our own
    ; stack is not using a wire the DOS program can collide with.
    ;
    ; It also has to invent the STATION ADDRESS the claim would have handed
    ; back, because get_address must answer something and there is no PROM to
    ; read. Locally-administered unicast again, and one digit off the
    ; gateway's - a client that saw its own address on both ends of a frame
    ; would drop it as a loop.
    cmp byte [dos_pkt_xl], 0
    je .real
    push ax
    push cx
    push si
    push di
    mov si, dn_ourmac_c             ; **THE IMAGE COPY AND NOT THE CLAIM'S**:
    mov di, dos_pkt_mac             ; dn_ourmac is an offset into the network
    mov cx, 6                       ; claim now (SPEC.md 96.26.6) and DS here
    call dn_copy                    ; is ours, so reading it would fetch six
                                    ; bytes of our own bss. The constant is in
                                    ; the image, which is the one place both
                                    ; segments can see
    pop di
    pop si
    pop cx
    mov byte [dos_pkt_raw], 1
%ifndef PN_ROOT                     ; (a PART is pinned for the whole bracket
                                    ; already, SPEC.md 96.23.11)
    xor ax, ax                      ; ...AND THE BUFFERS ARE PINNED FROM HERE
    call dos_pkt_rloc               ; (96.23.7.2): dos_pkt_tick's stack is in
                                    ; them from this instruction on. BEFORE the
                                    ; `pop ax` and not after: this arm restores
                                    ; the caller's AX on its way out, and a
                                    ; clobber below the pop is a routine that
                                    ; has quietly stopped preserving a register
%endif
    pop ax
    clc
    ret
.real:
    push ax
    push bx
    push di
    mov di, dos_pkt_mac             ; the claim hands back the station address
    mov al, 1                       ; (SPEC.md 72.22) - a consumer that is the
    mov bh, DRVC_NET                ; stack now cannot build a frame without it
    mov bl, NETV_RAW
    call OSAPI_DRV_CALL
    jc .no
    mov byte [dos_pkt_raw], 1
%ifndef PN_ROOT
    xor ax, ax                      ; ...and the buffers are pinned from here
    call dos_pkt_rloc               ; too - the card route's stack is the same
%endif                              ; stack (96.23.7.2)
    pop di
    pop bx
    pop ax
.have:
    clc
    ret
.no:
    pop di
    pop bx
    pop ax
    stc
    ret

dos_pkt_idle:                       ; the LAST handle takes the claim with it
    push bx
    push cx
    mov bx, dos_pkt_htab
    mov cx, PKT_NHAND
.l:
    cmp byte [bx+PKT_HUSED], 0
    jne .busy
    add bx, PKT_HSIZE
    loop .l
    pop cx
    pop bx
    jmp dos_pkt_rawdrop             ; a TAIL JUMP and not a fall-through: the
                                    ; label between them would take the local
                                    ; names below it into its own namespace,
                                    ; which is how `.busy` stopped resolving
.busy:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_pkt_rawdrop - the handles and the raw claim, but NOT the buffers
; dos_pkt_shut    - ...and the buffers too, which only the bracket may do
;                   (dos.asm's: it frees the claim)
;
; **THE SPLIT IS THE POINT.** The first version had release_type free the
; buffer claim along with everything else, and that is unrecoverable: §96.3
; has already given the whole heap to the program, so a client that released
; a handle and asked for another got its access_type refused for ever. The
; buffers belong to the BRACKET's lifetime and the raw claim to the handles'.
; -----------------------------------------------------------------------------
dos_pkt_rawdrop:
    push ax
    push bx
    push cx
    push di
    mov cx, PKT_NHAND               ; every handle goes, whichever path came
    mov bx, dos_pkt_htab            ; here: terminate is defined to end them
.z:
    mov byte [bx+PKT_HUSED], 0
    add bx, PKT_HSIZE
    loop .z
    cmp byte [dos_pkt_raw], 0
    je .out
    cmp byte [dos_pkt_xl], 0        ; nothing was claimed on the translation
    jne .letgo                      ; path, so there is nothing to give back
    xor di, di                      ; no MAC wanted on the way out
    xor al, al                      ; release
    mov bh, DRVC_NET
    mov bl, NETV_RAW
    call OSAPI_DRV_CALL
.letgo:
    mov byte [dos_pkt_raw], 0
%ifndef PN_ROOT                     ; (a PART stays pinned to the bracket's end:
                                    ; the vector and the tick point into it)
    mov ax, dos_pkt_reloc           ; ...and nothing is standing on the private
    call dos_pkt_rloc               ; stack any more, so the block may move
                                    ; again (96.23.7.2)
%endif
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; (dos_pkt_shut is the HOST's, in dos.asm: it frees the claim this code may
; be running from, SPEC.md 96.23.11)

; -----------------------------------------------------------------------------
; dos_pkt_poll - drain the ring, up-calling the client once per frame
;
; **BP IS NOT TOUCHED**: this is called from the gate, where BP is the frame
; pointer every exit path restores the client's registers through.
;
; The budget is what stops a busy segment of network from holding a tick for
; as long as it likes, and it is the ring's own depth rather than a guess: the
; NE2000 holds about ten frames, so a drain of ten empties whatever was there
; and an eleventh would be a frame that arrived while we were working.
; -----------------------------------------------------------------------------
PKT_BUDGET  equ 10

dos_pkt_poll:
    cmp byte [dos_pkt_raw], 0
    je .out                         ; no claim, no frames - and this is the
                                    ; common case: most DOS programs are not
                                    ; network programs
    cmp byte [dos_pkt_busy], 0
    jne .out                        ; **THE TICK CAN LAND INSIDE A CALL** and
                                    ; the client's receiver is not re-entrant
                                    ; merely because ours is
    cmp word [dos_pkt_bseg], 0      ; **AND THE CLAIM GUARD IS BOTH ROUTES'**
    je .out                         ; now (SPEC.md 96.23.7): it was the card's
                                    ; alone while the translation kept its
                                    ; frame in bss, and the symptom of getting
                                    ; that wrong was an ARP that vanished -
                                    ; dn_tx had swallowed the request and
                                    ; built the reply correctly, and this test
                                    ; stopped the poll before anything came to
                                    ; collect it
    mov byte [dos_pkt_busy], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, PKT_BUDGET
.f:
    push dx                         ; the budget: DX is the segment argument now
    cmp byte [dos_pkt_xl], 0
    jne .xl
    mov di, dos_pkt_rxs
    mov cx, NET_FRAME
    mov dx, [dos_pkt_bseg]
    mov bh, DRVC_NET
    mov bl, NETV_RAWRX
    call OSAPI_DRV_CALL
    pop dx
    jc .done
    jmp short .got
.xl:
    call dn_ready                   ; ...and it is ALREADY at PKB_RX, because
    pop dx                          ; the translation BUILT it there - which
    jc .done                        ; is the one place the two routes differ
                                    ; and the only reason this branch is left
.got:                        ; the ring is empty
    cmp cx, NET_FRAME                ; the TRUE length may exceed what we asked
    ja .next                        ; for (SPEC.md 72.22.2), and a cut frame
                                    ; handed to a stack is worse than none
    cmp cx, NET_EHSIZE
    jbe .next
    push dx                         ; **THE BUDGET GOES ON THE STACK ACROSS
    call dos_pkt_deliver            ; THE UP-CALL**, because DX is not ours
    pop dx                          ; over it: dos_pkt_deliver far-calls the
                                    ; CLIENT's receiver twice, and a client
                                    ; owes us no register at all. With the
                                    ; count in DX a receiver that used it
                                    ; turned a ten-frame drain into up to
                                    ; 65,535 of them, inside a tick handler -
                                    ; the same livelock dn_pump's CL counter
                                    ; had, one layer out
.next:
    dec dx
    jnz .f
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov byte [dos_pkt_busy], 0
.out:
    ret

; -----------------------------------------------------------------------------
; dos_pkt_deliver - one frame in the claim, CX bytes, to whoever registered
;
; **THE UP-CALL IS TWO CALLS** and that is the Crynwr contract rather than a
; choice (SPEC.md 96.23.4): AX=0 asks the client for somewhere to put CX
; bytes, and AX=1 hands the same buffer back full. A client that answers 0:0
; has refused it, and the frame is gone - it is off the ring already and there
; is nowhere to put it back.
; -----------------------------------------------------------------------------
dos_pkt_deliver:
    push es                         ; **ONE SOURCE ON BOTH ROUTES NOW** - the
    mov es, [dos_pkt_bseg]          ; card writes the claim through NETV_RAWRX
    mov ax, [es:dos_pkt_rxs+PKT_ETYPE] ; and the translation BUILDS in it
    pop es                          ; (SPEC.md 96.23.7), so the two arms this
                                    ; routine had, and the [dos_pkt_xl] test
                                    ; that chose between them, are gone
    xchg al, ah                     ; the wire is big-endian and we are not
    mov bx, dos_pkt_htab
    mov si, PKT_NHAND
.l:
    cmp byte [bx+PKT_HUSED], 0
    je .next
    cmp word [bx+PKT_HTYPE], 0
    je .hit                         ; 0 = every frame, whatever its type
    cmp [bx+PKT_HTYPE], ax
    je .hit
.next:
    add bx, PKT_HSIZE
    dec si
    jnz .l
                                    ; nobody registered for it, so it is
                                    ; dropped - which is what a packet driver
                                    ; does and not an error of ours, and
                                    ; get_statistics has a FIELD for saying so
.dropped:
    add word [dos_pkt_stats+PKS_DROP], 1
    adc word [dos_pkt_stats+PKS_DROP+2], 0
    ret
.hit:
    mov ax, [bx+PKT_HRCVO]
    mov [dos_pkt_cvec], ax
    mov ax, [bx+PKT_HRCVS]
    mov [dos_pkt_cvec+2], ax
    mov [dos_pkt_chand], bx         ; the handle, which both calls carry

    push cx                         ; --- call one: where shall I put it? ---
    xor ax, ax
    push ds
    push bp                         ; the CLIENT is about to run: it owes us
    call far [dos_pkt_cvec]         ; nothing, and BP is the gate's frame
    pop bp
    pop ds
    pop cx
    mov ax, es
    or ax, di
    jz .dropped                     ; 0:0 - refused, and the frame is gone -
                                    ; it is off the ring already and there is
                                    ; nowhere to put it back, so it is a DROP
                                    ; and get_statistics counts it as one

    push cx                         ; --- the copy, the claim to theirs ---
    push di
    push es
    push ds
    mov si, dos_pkt_rxs             ; DS:SI is the claim and ES:DI the buffer
    mov ds, [dos_pkt_bseg]          ; the client just gave us
    call dos_pkt_copy
    pop ds
    pop es
    pop di
    pop cx

    mov bx, [dos_pkt_chand]         ; --- call two: here it is ---
    mov si, di                      ; DS:SI is the buffer THEY chose, which is
    mov ax, 1                       ; what the contract hands back
    push ds
    push bp
    push es
    pop ds                          ; **DS IS THE CLIENT'S FROM HERE TO THE
    call far [cs:dos_pkt_cvec]      ; POP**, so the vector is only reachable
    pop bp                          ; with an override - and reading it from
    pop ds                          ; the client's segment would be a wild
                                    ; far call into the program's own data
    add word [dos_pkt_stats+PKS_PIN], 1
    adc word [dos_pkt_stats+PKS_PIN+2], 0
    add word [dos_pkt_stats+PKS_BIN], cx
    adc word [dos_pkt_stats+PKS_BIN+2], 0
.out:
    ret

; -----------------------------------------------------------------------------
; dos_pkt_copy - DS:SI -> ES:DI, CX bytes
;
; Written out rather than `rep movsb` for the reason every package here has
; one of these: a package's ES is the kernel's on entry to a callback and its
; own only where it has just set it, so the string instructions are the one
; family whose implicit segment is worth not relying on.
; -----------------------------------------------------------------------------
dos_pkt_copy:
    push ax
    push cx
    push si
    push di
    jcxz .out
.l:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    loop .l
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_pkt_start - publish the interface, if there is a card to publish it over
;
; **NOT PUBLISHED ON A MACHINE WITH NO NIC**, and that is SPEC.md 96.15.1's
; argument for the third time in this package: a signature a client can find,
; over a card that is not there, sends it to open a handle that cannot work
; and it has no way to ask why. Its absence sends it to its own "no packet
; driver" path, which every mTCP application has and which says so.
;
; The CARD by name and not net_find: the cable answers the socket verbs too
; and refuses every raw one (SPEC.md 72.22.3), so preferring one of two is
; the wrong question here.
; -----------------------------------------------------------------------------
dos_pkt_start:
    push bx
    mov byte [dos_pkt_vec], 0
    mov byte [dos_pkt_raw], 0
    mov byte [dos_pkt_busy], 0
    mov word [dos_pkt_can], 0x5A5A  ; the canary, armed
    mov byte [dos_pkt_mode], 3      ; what the card is in, and what get_rcv_mode
                                    ; answers until somebody sets it
    cmp byte [dos_pkt_xl], 0        ; the translation needs no buffers...
    jne .go
    cmp word [dos_pkt_bseg], 0      ; ...and the card path's dos_pkt_bufs
    je .none                        ; already asked whether there is a card
                                    ; AND got the buffers for it, so that is
                                    ; both of its questions in one compare
.go:
    call dos_pkt_install
    cmp byte [dos_pkt_vec], 0
    je .none
    call dos_pkt_hook08
.none:
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_pkt_install - find a free vector and put ourselves on it (SPEC.md 96.23.2)
; out: [dos_pkt_vec] = the vector taken, or 0 if every one was occupied
;
; **SEARCHED RATHER THAN CHOSEN**, which costs four instructions and buys the
; case that actually happens: a program the user ran earlier left something at
; 60h, or the .COM being run is itself a packet driver for a card we have not
; got. A vector whose handler already answers to `PKT DRVR` is somebody's.
; -----------------------------------------------------------------------------
dos_pkt_install:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov byte [dos_pkt_vec], 0
    mov bl, PKT_VEC_LO
.v:
    mov bh, 0
    mov ax, bx
    shl ax, 1
    shl ax, 1                       ; the vector's slot: v * 4
    mov si, ax
    xor ax, ax
    mov es, ax
    mov ax, [es:si+2]               ; its segment...
    or ax, [es:si]                  ; ...and offset: a NULL vector is free
    jz .take
    call dos_pkt_issig              ; ...and so is one nobody signed
    jc .take
.next:
    inc bl
    cmp bl, PKT_VEC_HI
    jbe .v
    jmp short .out                  ; every one taken: [dos_pkt_vec] stays 0
                                    ; and dos_run reports it
.take:
    cli
    mov word [es:si], dos_pkt_entry
    mov [es:si+2], cs
    sti
    mov [dos_pkt_vec], bl
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- dos_pkt_issig - does the handler at ES:SI carry `PKT DRVR` at offset 3? --
; out: CF=1 = it does NOT (the vector is free to take)
dos_pkt_issig:
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    push es
    mov ax, [es:si+2]
    mov bx, [es:si]
    mov ds, ax
    mov si, bx
    add si, 3
    push cs
    pop es
    mov di, dos_pkt_sig
    mov cx, 8
.c:
    mov al, [si]
    cmp al, [es:di]
    jne .free
    inc si
    inc di
    loop .c
    pop es                          ; every byte matched: somebody else's
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.free:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dos_pkt_hook08 - chain INT 08h for the life of the bracket (SPEC.md 96.23.4)
;
; This is the poll that makes asynchronous delivery real: a client that calls
; us once and then waits still receives. The unhook is dos_restore_machine's,
; which puts the WHOLE IVT back.
; -----------------------------------------------------------------------------
dos_pkt_hook08:
    push ax
    push es
    xor ax, ax
    mov es, ax
    cli
    mov ax, [es:0x08*4]
    mov [dos_pkt_old08], ax
    mov ax, [es:0x08*4+2]
    mov [dos_pkt_old08+2], ax
    mov word [es:0x08*4], dos_pkt_tick
    mov [es:0x08*4+2], cs
    sti
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_pkt_tick - IRQ0, chained, with the poll on a stack of our own
;
; **THE CHAIN GOES FIRST**, so the tick reaches the kernel at the depth it
; always did and on the stack it always did - the scheduler saves SP per task
; and a private one under it is a thing nothing here has tested.
;
; **THEN THE STACK SWAPS** (SPEC.md 96.23.4.1). The poll's deepest chain is
; OSAPI_DRV_CALL into the kernel, into the driver, into ne_rx's byte-at-a-time
; DMA loop and out into the client's receiver, and without this it would land
; on whatever stack the DOS program was running on at whatever depth it had
; reached - a .COM's default being 256 bytes under its own image. `mov
; [cs:x], sp` and `mov sp, imm16` need no register, which is what makes the
; swap possible at a gate where every register is the program's.
; -----------------------------------------------------------------------------
dos_pkt_tick:
    pushf                           ; the chain, exactly as an `int` would
    call far [cs:dos_pkt_old08]     ; have entered it - and through CS, since
                                    ; DS is the interrupted program's
    push ax
    push ds
    push cs
    pop ds
    cmp byte [dos_pkt_raw], 0       ; nothing claimed: not even the swap
    je .out
    cmp byte [dos_pkt_busy], 0      ; already inside a poll somewhere below
    jne .out
    mov [dos_pkt_sss], ss
    mov [dos_pkt_ssp], sp
    mov ax, [dos_pkt_bseg]          ; **THE STACK IS IN THE NETWORK CLAIM**
                                    ; (SPEC.md 96.23.7), not in our bss - and
                                    ; [dos_pkt_raw] above is what proves there
                                    ; is one: a claim that was refused
                                    ; publishes no interface, so nothing can
                                    ; have taken a handle
    cli
    mov ss, ax                      ; **SS AND SP IN CONSECUTIVE INSTRUCTIONS**
    mov sp, dos_pkt_stk_top         ; - an 8086 masks interrupts for one
    sti                             ; instruction after a `mov ss`, which is
                                    ; exactly this pair and is why the `cli`
                                    ; is belt and braces rather than the rule
    call dos_pkt_poll
    cli
    mov ss, [dos_pkt_sss]
    mov sp, [dos_pkt_ssp]
    sti
.out:
    pop ds
    pop ax
    iret
