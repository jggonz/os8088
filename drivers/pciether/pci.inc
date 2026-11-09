; =============================================================================
; os8088 - pci.inc: the PCI layer of PCIETHER.DRV (SPEC.md 125.3)
;
; Configuration space is reached through the PCI BIOS, `int 1Ah` AH=B1h, with
; 16-bit registers only. The image stays `cpu 8086`.
;
; HOW A kern_mega DRIVER REACHES int 1Ah (SPEC.md 125.8 item 1). It just
; issues it. kernel/mega.inc installs sixteen five-byte stubs for the BIOS's
; software services 10h-1Fh (`mega_ss`); each is `push vector / jmp mega_svc`,
; and mega_svc REFLECTS the call to real mode with every IRQ the BIOS does not
; own masked, runs the BIOS's own vector there, and comes back with the
; registers and flags the BIOS left. HDD.DRV's `int 13h`, VESA.DRV's
; `int 10h`, XMEM.DRV's `int 15h` and the kernel's clock `int 1Ah` all go
; through it. Nothing here passes a pointer, so DS and ES - which mega_svc
; turns into real-mode segments - are not read by the BIOS. On a 386 the
; reflection is a CR0.PE clear and set; on a 286 it is a CPU reset through the
; BIOS, which is why pci_probe asks OSAPI_CPU_INFO first and a 286 never makes
; the call: no 286 board has PCI. It costs a few hundred microseconds a call
; and runs at attach only.
;
; An includer defines `pce_tab`: rows of {vendor, device, name, vectors}, a
; zero vendor ends it. pci_probe walks it.
; =============================================================================

PCI_INT         equ 0x1A
PCI_PCISIG      equ 0x4350      ; DX after B101h: the low word of EDX = 'PCI '

PCI_VENDOR      equ 0x00
PCI_COMMAND     equ 0x04
PCI_BAR0        equ 0x10
PCIC_IO         equ 0x0001      ; command: I/O Space
PCIC_MASTER     equ 0x0004      ; command: Bus Master

; -----------------------------------------------------------------------------
; pci_bios - one PCI BIOS call. in: AX = the B1xxh function, and its own
; registers (SPEC.md 125.3's table). out: CF=0 it answered SUCCESSFUL (AH = 0);
; CF=1 the BIOS refused - CF set, or AH a non-zero return code. AH is spent,
; and whatever the function itself writes (B101h: AL, BX, CX, EDX, EDI)
; -----------------------------------------------------------------------------
pci_bios:
    int PCI_INT
    jc  .r
    neg ah                      ; CF = (AH != 0): every code but SUCCESSFUL
.r:
    ret

; -----------------------------------------------------------------------------
; pci_probe - walk pce_tab, take the first card the BIOS finds
; out: CF=0 and BX = bus/devfn, AX = its I/O base, SI = the table row;
;      CF=1 = none (or not a 386, or no PCI BIOS)
; clobbers: CX, DI (pce_find's caller, eth_attach, banks both); DX is KEPT
;
; THE CPU GATE FIRST: the PCI BIOS is asked only when OSAPI_CPU_INFO answers a
; 386 or better - no board below one carries PCI, and on kern_mega a 286's
; reflection of int 1Ah would be a CPU reset through the BIOS (125.8 item 1).
; Then B101h, whose 'PC' in DX is the BIOS's signature; a 2.1 BIOS also
; writes EDI there (SeaBIOS does), which is DI and so spent.
;
; The card is enabled only once a row matched AND its BAR0 is an I/O BAR, so a
; device we cannot drive is left exactly as the BIOS set it up.
; -----------------------------------------------------------------------------
pci_probe:
    push dx
    call OSAPI_CPU_INFO         ; AL = the CPU (every other register kept)
    cmp al, CPU_386
    jb  .none
    mov ax, 0xB101              ; present?
    call pci_bios
    jc  .none
    cmp dx, PCI_PCISIG
    jne .none
    mov di, pce_tab
.row:
    mov dx, [di]                ; vendor
    or  dx, dx
    jz  .none
    mov cx, [di+2]              ; device
    xor si, si                  ; the first of its kind
    mov ax, 0xB102
    call pci_bios               ; BX = bus/devfn
    jc  .next
    mov si, di                  ; the row, banked: DI names a register now
    mov di, PCI_BAR0
    mov ax, 0xB109
    call pci_bios               ; CX = BAR0
    jc  .back
    test cl, PCIC_IO            ; bit 0: an I/O BAR (the same bit as the
    jz  .back                   ; Command register's I/O Space)
    and cl, 0xFC                ; the base
    push cx
    mov di, PCI_COMMAND
    mov ax, 0xB109
    call pci_bios               ; CX = the Command register...
    jc  .base
    or  cl, PCIC_IO | PCIC_MASTER ; ...I/O Space and Bus Master on, the rest
    mov ax, 0xB10C              ; as the BIOS left it
    call pci_bios
.base:
    pop ax                      ; AX = the base (POP keeps CF)
    jnc .out
.back:
    mov di, si
.next:
    add di, PCE_ROW
    jmp short .row
.none:
    stc
.out:
    pop dx
    ret
