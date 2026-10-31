; =============================================================================
; os8088 - apps/win16/w16dosh.asm
;
; WIN16, AS A HOST OF THE DOS CORE (SPEC.md 124.21.1). DOS.O88 and kern_dos
; are the core's other two hosts (SPEC.md 96.44). A host owes the core its back
; end, its hooks and its segment; this part is what WIN16 puts in that segment
; above the core's bss, and it is small because the back end is DOS.O88's own
; source (apps/dos/dosbe.inc) and WIN16 binds no hooks.
;
; `apps/dos/dos.asm` with DOS_EXTCORE and KD_BACKEND emits no bytes - neither
; the core nor DOS.O88's window - and gives this file every constant and every
; core cell's address, which are the same in every host (SPEC.md 96.44.2).
; =============================================================================
cpu 8086
bits 16

%define DOS_EXTCORE                 ; the core is not in this assembly...
%define KD_BACKEND                  ; ...and nor is DOS.O88's window
%include "doscall.inc"
%include "w16dos.inc"
%include "w16dcore.inc"             ; W16_DOSINT21 (tools/w16dcmap.py)

    org W16DH_ORG

os88_image_end equ W16DH_ORG + W16DH_MAX  ; no host cell is named: this is
DVOL_MAX equ DVOL_CAP                     ;   only so the equates assemble
%include "dos.asm"

w16dh_tab:
    jmp strict near w16dh_init      ; W16DH_INIT
w16dh_tab_i21:
    jmp strict near w16dh_i21       ; W16DH_I21
w16dh_tab_done:
    jmp strict near w16dh_done      ; W16DH_DONE
    dw 0                            ; W16DH_HSS
    dw 0                            ; W16DH_HSP
    db DOS_WKB                      ; W16DH_WKB

; -----------------------------------------------------------------------------
; w16dh_i21 - the core's int 21h, on the stack the image named
; in:  an `int 21h` frame (or a far call after pushf), the caller's registers
; The core's gate runs a call on [dos_sv_ss]:[dos_dstk], or on the stack it
; arrived on when that is already the one (a KERNEL call's handler, on the
; UI task's). The image's host stack moves with every resume, so it is
; written here at each call rather than once.
; -----------------------------------------------------------------------------
w16dh_i21:
    push ax
    mov ax, [cs:W16DH_HSS]
    mov [cs:dos_sv_ss], ax
    mov ax, [cs:W16DH_HSP]
    mov [cs:dos_dstk], ax
    pop ax
    jmp W16_DOSINT21

; -----------------------------------------------------------------------------
; w16dh_init - stand the core where the program is, once (far)
; in:  AX = the program's PSP-shaped block, BL = the volume, DX = the folder's
;      first cluster, CX = the window's segment, SI = its paragraphs
; out: CF=1 = the window is smaller than the volume's cluster
; -----------------------------------------------------------------------------
w16dh_init:
    push ds
    push ax
    push cs
    pop ds
    call dos_be_bind                ; the doors, as addresses
    mov byte [dos_onprog], 0        ; a call is never on the program's stack
    mov byte [dos_pvol], 0xFF       ; standing nowhere yet (SPEC.md 96.6.3)
    mov [dos_ldpsp], ax             ; the JFT the core keeps in step
    mov [dos_dtaseg], ax            ; the DTA at PSP:0080, as DOS sets it
    mov word [dos_dta], 0x80
    mov [dos_vol], bl
    mov [dos_curdir], dx
    mov al, bl
    call dos_drv_bank               ; ...and the drive's own folder
    mov ax, cx                      ; an arena whose top is the window, so
    sub ax, DOS_PSPP + 0x100        ;   dos_fh_setup carves exactly it; the
    mov [dos_arena], ax             ;   memory calls that would walk the rest
    mov ax, si                      ;   are never forwarded (SPEC.md 124.21.2)
    add ax, DOS_PSPP + 0x100
    mov [dos_apara], ax
    call dos_fh_setup               ; CF=1 AL = DER_MEM
    pop ax
    pop ds
    retf

; -----------------------------------------------------------------------------
; w16dh_done - the program has ended: each handle it left open closed, which
; writes what the window holds (SPEC.md 96.11) and keeps the JFT in step, as
; DOS's own exit does (dos_fh_sweep is not on the core's table). Far, on the
; UI task's stack with [W16DH_HSS] already that stack. Keeps all.
; -----------------------------------------------------------------------------
w16dh_done:
    ; STKBALANCE-OK: each `pushf` / `push cs` / `call` is an int's frame,
    ; which the core's gate takes back with its iret
    push ax
    push bx
    mov bx, DOS_FH0
.close:
    mov ah, 0x3E                    ; one not open answers 6, unharmed
    pushf
    push cs
    call w16dh_i21
    inc bx
    cmp bx, DOS_FH0 + DOS_NFH
    jb .close
    pop bx
    pop ax
    retf

%include "dosbe.inc"
    DOS_BETAB

W16DH_LEN equ $ - $$
%if W16DH_LEN > W16DH_MAX
  %error "WIN16's DOS host outgrew W16DH_MAX (apps/win16/w16dos.inc)"
%endif
