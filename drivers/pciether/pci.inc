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
; BIOS, which is why pci_gate asks OSAPI_CPU_INFO first and a 286 never makes
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
; pci_gate - may we ask the PCI BIOS at all? (a 386 or better)
; out: CF=0 yes, CF=1 no. AX kept.
; -----------------------------------------------------------------------------
pci_gate:
    push ax
    call OSAPI_CPU_INFO
    cmp al, CPU_386             ; CF = AL below a 386
    pop ax                      ; (pop leaves the flags)
    ret

; -----------------------------------------------------------------------------
; pci_present - B101h. out: CF=0 the BIOS is there. Every register kept.
; -----------------------------------------------------------------------------
pci_present:
    push ax
    push bx
    push cx
    push dx
    mov ax, 0xB101
    int PCI_INT
    jc  .no
    or  ah, ah
    jnz .no
    cmp dx, PCI_PCISIG
    jne .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pci_find - B102h. in: DX = vendor, CX = device, SI = index
; out: CF=0 and BX = bus (BH) / device-function (BL); CF=1 = none
; -----------------------------------------------------------------------------
pci_find:
    push ax
    mov ax, 0xB102
    int PCI_INT
    jc  .no
    or  ah, ah
    jnz .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; configuration reads and writes: BX = bus/devfn, DI = the register
; pci_rdb -> CL, pci_rdw -> CX; pci_wrb takes CL, pci_wrw takes CX. CF=1 = the
; BIOS refused. Everything else kept.
; -----------------------------------------------------------------------------
pci_rdb:
    push ax
    mov ax, 0xB108
    jmp short pci_cfg
pci_rdw:
    push ax
    mov ax, 0xB109
    jmp short pci_cfg
pci_wrb:
    push ax
    mov ax, 0xB10B
    jmp short pci_cfg
pci_wrw:
    push ax
    mov ax, 0xB10C
pci_cfg:
    int PCI_INT
    jc  .no
    or  ah, ah
    jnz .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; pci_iobar0 - BX = bus/devfn. out: CF=0 and AX = BAR0's port base; CF=1 = BAR0
; is not an I/O BAR (bit 0 clear) or the BIOS refused
; -----------------------------------------------------------------------------
pci_iobar0:
    push cx
    push di
    mov di, PCI_BAR0
    call pci_rdw
    jc  .out
    test cl, 1
    jz  .no
    mov ax, cx
    and ax, 0xFFFC
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; pci_enable - BX = bus/devfn: I/O Space and Bus Master on, the rest of the
; Command register as the BIOS left it. CF=1 = the BIOS refused
; -----------------------------------------------------------------------------
pci_enable:
    push cx
    push di
    mov di, PCI_COMMAND
    call pci_rdw
    jc  .out
    or  cx, PCIC_IO | PCIC_MASTER
    call pci_wrw
.out:
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; pci_probe - walk pce_tab, take the first card the BIOS finds
; out: CF=0 and BX = bus/devfn, AX = its I/O base, SI = the table row;
;      CF=1 = none (or not a 386, or no PCI BIOS)
;
; The card is enabled only once a row matched AND its BAR0 is an I/O BAR, so a
; device we cannot drive is left exactly as the BIOS set it up.
; -----------------------------------------------------------------------------
pci_probe:
    push cx
    push dx
    push di
    call pci_gate
    jc  .none
    call pci_present
    jc  .none
    mov di, pce_tab
.row:
    mov dx, [di]                ; vendor
    or  dx, dx
    jz  .none
    mov cx, [di+2]              ; device
    xor si, si                  ; the first of its kind
    call pci_find
    jc  .next
    call pci_iobar0
    jc  .next
    call pci_enable
    jc  .next
    mov si, di
    clc
    jmp short .out
.next:
    add di, PCE_ROW
    jmp short .row
.none:
    stc
.out:
    pop di
    pop dx
    pop cx
    ret
