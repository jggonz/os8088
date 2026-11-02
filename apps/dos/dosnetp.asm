; =============================================================================
; os8088 - apps/dos/dosnetp.asm
;
; DOS's NETWORK PART (SPEC.md 96.23.11): the packet driver (dospkt.inc,
; SPEC.md 96.23) and the cable translation (dosnet.inc, SPEC.md 96.26),
; assembled on their own as part 4 of DOS.O88 - an OP_ASSET | OP_COMP |
; OP_LAZY | OP_SELF row the re-homed host reads with op_self into the head of
; the network claim, only when a run will publish a packet driver.
;
; THE CLAIM, every offset an assembly constant of this file:
;
;   0             the door below, then the code and its constant strings
;   PN_STATE_AT   the packet driver's cells (dospktabi.inc's PNC_*) - this
;                 image's own length, its tail being 16-aligned, which is how
;                 the HOST knows it from the row without a constant
;   PN_BUF        96.23.7's buffers: PKB_* rebased (PN_BUFSYMS PN_BUF)
;
; CS = DS = the claim in every routine here, so the gate's `push cs / pop ds`
; and the vector's `mov [es:si+2], cs` are right as written, and dosnet.inc's
; DN_DOOR reads the claim out of the claim. The host zeroes everything past
; the image before PN_D_INIT, and frees the claim after PN_D_SHUT.
;
; Nothing here may name a symbol of the host: it is a different assembly, and
; an unresolved name fails the build here rather than reading the wrong
; segment at run time - which is the property that makes the split safe.
; =============================================================================

%define PN_ROOT
    cpu 8086
    bits 16
    org 0

%include "os88api.inc"              ; the OSAPI_* cells and DRVC_* (no bytes)
%include "netpkg.inc"               ; NETV_* and NET_*, the driver's own header
%include "dosnetabi.inc"            ; the translation's numbers
%include "dospktabi.inc"            ; ...and the packet driver's, and the doors

; -----------------------------------------------------------------------------
; pn_door - THE ONE FAR ENTRY, at offset 0 (SPEC.md 96.23.11)
; in:  AL = PN_D_*; for PN_D_INIT also AH = the route (1 = the translation),
;      BL = the class the socket verbs answer on ([net_cls]), DX:SI = the
;      environment rows. Far-called with DS = the host's.
; out: nothing; DS preserved; AX and the flags are the door's
;
; **CS IS WRITTEN INTO [dos_pkt_bseg] FIRST**: the claim is movable between
; doors (dos_pkt_bufs to dos_pkt_start, 96.23.7.2) and every reader of the
; claim here loads that word, so the door is where it is made true again.
; -----------------------------------------------------------------------------
pn_door:
    push ds
    push cs
    pop ds
    mov [dos_pkt_bseg], cs
    cmp al, PN_D_POLL               ; the hot one, every INT 21h of a run
    je .poll
    cmp al, PN_D_START
    je .start
    cmp al, PN_D_SHUT
    je .shut
    mov [dos_pkt_xl], ah            ; --- PN_D_INIT ---
    mov [net_cls], bl
    mov [dos_pkt_eseg], dx
    mov [dos_pkt_eoff], si
    or ah, ah
    jz .out                         ; the card needs no state of the translation's
    call dn_init
    jmp short .out
.poll:
    call dos_pkt_poll
    jmp short .out
.start:
    call dos_pkt_start
    jmp short .out
.shut:
    call dn_shut                    ; every translated flow closed, and then
    call dos_pkt_rawdrop            ; the handles and the raw claim
.out:
    pop ds
    retf

%include "dospkt.inc"
%include "dosnet.inc"

    align 16, db 0                  ; the image ends on a paragraph, so its
PN_STATE_AT equ $                   ; length IS where the cells start
PN_BUF      equ PN_STATE_AT + PN_STATE

dos_pkt_vec   equ PN_STATE_AT + PNC_VEC
dos_pkt_raw   equ PN_STATE_AT + PNC_RAW
dos_pkt_busy  equ PN_STATE_AT + PNC_BSY
dos_pkt_mode  equ PN_STATE_AT + PNC_MODE
dos_pkt_xl    equ PN_STATE_AT + PNC_XL
net_cls       equ PN_STATE_AT + PNC_CLS
dos_pkt_htab  equ PN_STATE_AT + PNC_HTAB
dos_pkt_mac   equ PN_STATE_AT + PNC_MAC
dos_pkt_cvec  equ PN_STATE_AT + PNC_CVEC
dos_pkt_chand equ PN_STATE_AT + PNC_CHAND
dos_pkt_old08 equ PN_STATE_AT + PNC_OLD08
dos_pkt_sss   equ PN_STATE_AT + PNC_SSS
dos_pkt_ssp   equ PN_STATE_AT + PNC_SSP
dos_pkt_can   equ PN_STATE_AT + PNC_CAN
dos_pkt_cds   equ PN_STATE_AT + PNC_CDS
dos_pkt_bseg  equ PN_STATE_AT + PNC_BSEG
dos_pkt_eseg  equ PN_STATE_AT + PNC_ESEG
dos_pkt_eoff  equ PN_STATE_AT + PNC_EOFF
dos_pkt_stats equ PN_STATE_AT + PNC_STAT

PN_BUFSYMS PN_BUF                   ; dn_*, dos_pkt_rxs/_txs/_stk_top

DOS_ENVN    equ PN_ENVN             ; the host's rows, walked through ES
DOS_ENVBUF  equ PN_ENVBUF

; the network claim must stay inside one segment with op_self's read slack
; beside it - a negative TIMES is the assembler refusing (it emits nothing
; when the claim fits, so the image's length is unchanged)
    times -((PN_STATE_AT - pn_door) + PN_STATE + PKB_XLB > 0xF000) db 0
