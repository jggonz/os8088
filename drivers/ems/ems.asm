; =============================================================================
; os8088 - drivers/ems/ems.asm
;
; EMS.DRV: EXPANDED MEMORY on a LIM EMS board's page registers (SPEC.md 107).
;
; A board has four page registers. Writing one maps a 16 KB page of the board
; into one QUARTER of a 64 KB FRAME below 1 MB, and the CPU then reads and
; writes that page as ordinary memory - so nothing here ever moves a byte. This
; driver finds the board, keeps who owns which of its pages and which quarter
; of its frame, and maps on request; the package does the rest in the frame
; itself (docs/plans/VIDEO-XMS-PLAN.md 10 is the design record, and the
; measurement that a disk read lands in the frame at the disk's own rate).
;
; ONE REGISTER FAMILY, the CONSECUTIVE one (SPEC.md 107.2): quarter q's
; register at base + q, a page's value its number. The Lo-tech 2 MB board,
; MartyPC's model of it, 86Box's "Lo-tech EMS Board" and a PicoMEM's EMS. The
; registers are WRITE-ONLY, so nothing here ever reads one: the driver knows
; what it wrote, and a quarter's holder knows what IT wrote (107.4).
;
; Reached by packages through OSAPI_DRV_CALL (BH = DRVC_EMS), and by the
; kernel for one verb, EMSV_GONE, with ES = KERNEL_SEG (xm_release_rec). The
; kernel hands this class the CALLER'S INSTANCE SLOT in BH, where every other
; class gets its own class number - so every verb puts DRVC_EMS back in BH
; before it returns, and the BX a package called with is the BX it gets.
;
; ONE KILOBYTE (kernel size pass 11). The image was 1,208 bytes and drv_load
; claims a driver in whole KB, so it took two. 256 of them were a per-page
; owner table that only repeated the handles: a handle IS one contiguous run
; (first page, length), so a page is owned exactly when it lies inside a live
; handle's run, and first fit asks the eight handles instead (em_alloc).
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'EMS', DRVC_EMS, em_entry, em_svc_end - em_svc

EM_MAXPG    equ 256             ; pages a register names (a 4 MB board)
EM_MINPG    equ 4               ; ...and fewer than a frame's four is no board
EM_ROMSIG   equ 0xAA55          ; an option ROM's first word, as a word read
EM_SLOTX    equ 0xFE            ; AN OWNER IS KEPT AS ITS SLOT XOR THIS, so 0
                                ; is "nobody" and every table is free at the
                                ; zero it loads as. Slots are 0..7 and 0xFF
                                ; (inst_caller's "no instance": the UI task, a
                                ; driver), which may hold one too - 0xFE is the
                                ; one value no caller has, and the one that
                                ; maps to 0

%if DRVV_ATTACH != 0 || DRVV_DETACH != 1
    %error "em_entry's verb test assumes ATTACH = 0 and DETACH = 1"
%endif
%if EMSV_IDENT != 0 || EMSV_CAPS != 1 || EMSV_ALLOC != 2 || EMSV_FREE != 3 || EMSV_FRAME != 4 || EMSV_UNFRAME != 5 || EMSV_MAP != 6 || EMSV_BASE != 7 || EMSV_GONE != 8
    %error "em_vtab is indexed by the EMSV_* numbers"
%endif
%if EM_MAXPG != 256
    %error "em_attach's read-back stops when CH turns 1, which is 256 pages"
%endif

; =============================================================================
; ENTRY - BX is the kernel's row pointer across attach and must come back
; (drivers/usbmouse/usbmouse.asm's header has why): attach borrows it for
; quarter 1's offset and puts it back
; =============================================================================
em_entry:
    cmp al, DRVV_DETACH
    jb em_attach
    ret                         ; DETACH, CF = 0 off the compare: nothing is
                                ; hooked and nothing is claimed - the board
                                ; keeps its contents, and a holder's next call
                                ; answers "no driver"
em_none:                        ; attach's refusal, out here where its jump
    mov al, DRVE_HW             ; stays short
    stc
    pop bx
    pop es
    ret

; -----------------------------------------------------------------------------
; DRVV_ATTACH - find the board (SPEC.md 107.2). out CF=0 SI = em_svc, or
; CF=1 AL = DRVE_HW
;
; DI = 0 and BX = 4000h for the whole walk: quarter 0's first byte and quarter
; 1's, in the frame ES names
; -----------------------------------------------------------------------------
em_attach:
    push es
    push bx
    mov al, DRVC_POINT          ; 260h is the CH375's on a Book8088 (SPEC.md
    call OSAPI_DRV_CLASSK       ; 9.12): never written while a USB mouse
    jc .any                     ; driver is up, so its place in the list is
    mov word [em_bases], 0x264  ; 264h's, asked twice. CF=1 = none loaded
.any:
    mov bx, 0x4000
    mov ax, 0xF000
    mov es, ax
.frame:                         ; the frames E000h, D000h, C000h in that order
    mov ax, es
    sub ah, 0x10
    cmp ah, 0xC0
    jb em_none
    mov es, ax
    xor di, di                  ; AN OPTION ROM anywhere in the frame (the
.rom:                           ; BIOS's own scan, a 2 KB step): somebody
    cmp word [es:di], EM_ROMSIG ; else's, and NO PORT is written for it
    je .frame
    add di, 2048
    jnz .rom                    ; ...and DI is 0 again
    mov si, em_bases
.base:
    lodsw
    xchg ax, dx                 ; DX = the base
    or dx, dx
    jz .frame
    ; --- does a paging board answer here? The two bytes this writes are put
    ; back when nothing pages, since then they may be somebody's RAM; a
    ; board's pages are this driver's to write
    mov cl, [es:di]
    mov ch, [es:bx]
    xor ax, ax                  ; page 0 -> quarter 0, page 1 -> quarter 1
    out dx, al
    inc dx
    inc ax
    out dx, al
    dec dx
    mov byte [es:di], 0x5A
    mov byte [es:bx], 0xA5
    cmp byte [es:di], 0x5A      ; two different bytes stuck...
    jne .no
    cmp byte [es:bx], 0xA5
    jne .no
    inc dx                      ; ...and page 0 into quarter 1 as well reads
    dec ax                      ; quarter 0's byte there
    out dx, al
    dec dx
    cmp byte [es:bx], 0x5A
    jne .no
    mov byte [es:bx], 0x3C      ; ...and a write through quarter 1 is seen
    cmp byte [es:di], 0x3C      ; through quarter 0: one page, two windows
    je .size
.no:
    mov [es:di], cl
    mov [es:bx], ch
    jmp short .base
    ; --- SIZED by a signature in each page's first four bytes, written from
    ; the top DOWN and read UP: a smaller board aliases, and the last write
    ; to a physical page is its own number
.size:
    mov cx, EM_MAXPG
.wr:
    mov ax, cx
    dec ax
    out dx, al
    mov [es:di], ax
    not ax
    mov [es:di+2], ax
    loop .wr
.rd:                            ; CX = 0 here
    mov ax, cx
    out dx, al
    cmp [es:di], ax
    jne .sized
    not ax
    cmp [es:di+2], ax
    jne .sized
    inc cx
    or ch, ch                   ; CX < EM_MAXPG
    jz .rd
.sized:
    cmp cx, EM_MINPG
    jb .base
    mov [em_port], dx
    mov [em_frame], es
    mov [em_npg], cx
    xor ax, ax                  ; pages 0..3 into the four quarters, so the
.map:                           ; frame reads as the board and nothing else
    out dx, al
    inc ax
    inc dx
    cmp al, 4
    jb .map                     ; CF = 0 falling out
    mov si, em_svc
    pop bx
    pop es
    ret

; =============================================================================
; THE PACKAGE DOOR (DSV_PKGCALL, SPEC.md 107.3)
; in: BL = the verb, BH = the CALLER'S INSTANCE SLOT, ES = its segment (the
; kernel's for EMSV_GONE), AX/CX/DX the verb's. out: CF, and the answers in
; AX, CX, DX, SI; BX comes back with BH = DRVC_EMS, DI is never written.
; Every verb runs with interrupts off: a package's worker and its UI task may
; both be in here, and the tables are a handful of bytes each.
;
; DI IS SAVED HERE, ONCE, so a verb may use it freely; each verb keeps any
; other register it does not answer in. A verb sees BH = the caller's slot XOR
; EM_SLOTX, the form the tables keep, and enters with CF = 0.
; =============================================================================
em_pkg:
    pushf
    cli
    push di
    xor bh, EM_SLOTX
    mov di, bx
    cmp bl, EMSV_GONE
    jbe .verb
    mov di, EMSV_GONE + 1       ; a verb this driver does not have: em_bad
.verb:
    and di, 0x00FF
    shl di, 1                   ; CF = 0
    call [em_vtab + di]         ; CF = the verb's answer...
    pop di
    rcl bh, 1                   ; ...carried across popf in BH's low bit,
    popf                        ; which puts the caller's IF back
    rcr bh, 1
    mov bh, DRVC_EMS            ; (writes no flag)
    ret

em_vtab:
    dw em_ident, em_caps, em_alloc, em_free, em_fr, em_unfr, em_map, em_base
    dw em_gone, em_bad

em_ident:
    mov ax, 'EM'
    ret

; CAPS: AX = pages free, CX = the board's, DX = the frame, SI = free quarters
em_caps:
    mov cx, [em_npg]
    mov ax, cx
    mov di, 2 * EMS_NHND - 2
.c:                             ; free = the board less every handle's run (a
    sub ax, [em_hlen + di]      ; free handle's length is 0)
    dec di
    dec di
    jns .c
    mov dx, [em_frame]
    xor si, si                  ; the free quarters, bit q for quarter q
    mov di, 3
.q:
    cmp byte [em_qown + di], 1  ; CF = 1: nobody holds it
    rcl si, 1
    dec di
    jns .q
    ret                         ; CF = 0: the bit `rcl` last shifted out of SI

; FREE: AX = a handle of yours
em_free:
    call em_hnd
    jc em_zap.out
; em_zap - DI = a handle's index: it is free, and so are its pages. AX = 0,
; CF = 0. The base and owner go with the length, because em_alloc's overlap
; test passes over a free handle only when its base is 0 too
em_zap:
    xor ax, ax
    mov [em_hlen + di], ax
    mov [em_hbo + di], ax
.out:
    ret

; BASE: AX = a handle of yours -> AX = its first page, CX = its length
em_base:
    call em_hnd
    jc .out
    mov cx, [em_hlen + di]
    mov al, [em_hbo + di]
    mov ah, 0
.out:
    ret

; ALLOC: AX = pages -> AX = a handle. FIRST FIT, one contiguous run, so a
; handle is (its first page, its length) and an interrupt can map from that
; alone (SPEC.md 107.4).
;
; THE FIT IS ASKED OF THE HANDLES. CX is the run's END, its start 0 first; a
; live handle that overlaps it moves the start to that handle's end, and the
; walk begins again. That finds the LOWEST start with room - a start inside a
; handle that overlaps the run is overlapped by it too, so nothing is skipped,
; and CX only grows - which is the run the page-table scan it replaces found:
; 0, or one past an owned page, which is the end of some handle
em_alloc:
    or ax, ax
    jz em_bad
    push cx
    push dx
    push si
    cmp ax, [em_npg]
    ja .room
    xor di, di                  ; a free handle: length 0
.h:
    cmp word [em_hlen + di], 0
    je .got
    inc di
    inc di
    cmp di, 2 * EMS_NHND
    jb .h
    mov ax, EMSE_HND
    jmp short .fail
.got:
    mov cx, ax                  ; the run is [CX - AX, CX)
.again:
    mov si, 2 * EMS_NHND - 2
.ov:
    mov dl, [em_hbo + si]
    mov dh, 0                   ; DX = that handle's first page
    cmp dx, cx
    jae .nx                     ; it starts at or past the run's end
    add dx, [em_hlen + si]      ; its end...
    add dx, ax                  ; ...and the run's, were the run to start there
    cmp dx, cx
    jbe .nx                     ; it ends at or before the run's start
    mov cx, dx                  ; overlapped: start at its end instead
    jmp short .again
.nx:
    dec si
    dec si
    jns .ov
    cmp cx, [em_npg]
    ja .room
    mov [em_hlen + di], ax
    sub cx, ax                  ; CL = the first page (a page fits a byte)
    mov ch, bh                  ; CH = the owner
    mov [em_hbo + di], cx
    xchg ax, di
    shr ax, 1                   ; CF = 0: the index is even
    inc ax                      ; AX = the handle
    jmp short .out
.room:
    mov ax, EMSE_ROOM
.fail:
    stc
.out:
    pop si
    pop dx
    pop cx
    ret

; em_hnd - AX = a handle: CF=0 it is live and the caller's, DI = its index
; (2 x (handle - 1)); CF=1 AX = EMSE_BAD, which is em_bad below
em_hnd:
    mov di, ax
    dec di
    cmp di, EMS_NHND
    jae em_bad
    shl di, 1
    cmp word [em_hlen + di], 0
    je em_bad
    cmp [em_hbo + 1 + di], bh
    je em_ret                   ; CF = 0 off the equal compare
em_bad:
    mov ax, EMSE_BAD            ; not yours, out of range, or a verb this
    stc                         ; driver does not have
em_ret:
    ret

; MAP: AL = a quarter you hold, DX = a handle of yours, CX = a page of it
em_map:
    cmp al, 3
    ja em_bad
    cbw                         ; AH = 0
    mov di, ax
    cmp [em_qown + di], bh
    jne em_bad
    push dx
    xchg ax, dx                 ; AX = the handle, DX = the quarter...
    add dx, [em_port]           ; ...and so its register
    call em_hnd
    jc .out
    cmp cx, [em_hlen + di]
    cmc                         ; CF = 1: past the handle's end
    mov ax, EMSE_BAD            ; (writes no flag)
    jc .out
    mov al, [em_hbo + di]
    add al, cl                  ; the board's page, which is below the board's
    out dx, al                  ; count: no carry, CF = 0
.out:
    pop dx
    ret

; GONE: the KERNEL's (ES = KERNEL_SEG), AL = an instance slot that is being
; torn down - every handle and quarter of its freed. EVERY REGISTER BUT AX IS
; KEPT: xm_release_rec banks only AX and BX round it (SPEC.md 107.1). It ends
; in UNFRAME's walk, with every quarter asked
em_gone:
    mov di, es
    cmp di, KERNEL_SEG
    jne em_bad                  ; a package cannot send it: its calls arrive
    xor al, EM_SLOTX            ; with its own segment in ES
    mov bh, al                  ; (em_pkg puts DRVC_EMS back)
    mov di, 2 * EMS_NHND - 2
.h:
    cmp [em_hbo + 1 + di], bh
    jne .n
    call em_zap
.n:
    dec di
    dec di
    jns .h
    mov al, 0x0F                ; ...and into UNFRAME
; UNFRAME: AL = quarters: the ones the caller holds are free again
em_unfr:
    and al, 0x0F
    mov di, em_qown
.u:
    shr al, 1
    jnc .n
    cmp [di], bh
    jne .n
    mov byte [di], 0
.n:
    inc di
    test al, al                 ; CF = 0 on the way out
    jnz .u
    ret

; FRAME: AL = quarters -> all of them the caller's, or none (EMSE_BUSY).
; out DX = the frame, CX = quarter 0's register, SI = the step, AL = 0
em_fr:
    and al, 0x0F
    jz em_bad
    mov di, 3
.chk:                           ; CL: bit q set for a quarter ANOTHER holds
    mov ah, [em_qown + di]
    cmp ah, bh
    je .f                       ; ours already, CF = 0
    cmp ah, 1                   ; CF = 1 free...
    cmc                         ; ...so CF = 1 now: somebody else's
.f:
    rcl cl, 1
    dec di
    jns .chk
    test cl, al                 ; (CL's top half is whatever it held: AL has
    jnz .busy                   ; no bits there)
    mov ah, al                  ; ...so take them all
    mov di, em_qown - 1
.t:
    inc di
    shr ah, 1                   ; CF = this quarter, ZF = none after it
    jnc .n
    mov [di], bh
.n:
    jnz .t
    mov dx, [em_frame]
    mov cx, [em_port]
    mov si, 1                   ; the CONSECUTIVE family: base + q
    xor ax, ax                  ; ...and a page's value is its number. CF = 0
    ret
.busy:
    mov ax, EMSE_BUSY
    stc
    ret

em_bases:   dw 0x260, 0x264, 0x268, 0x26C ; the bases tried in each frame: the
            dw 0x288, 0               ; Lo-tech's, and a PicoMEM's default

; =============================================================================
; THE SERVICE TABLE IS ONE CELL. DRVC_EMS keeps no copy of a driver's table:
; drv_publish reads [SI + DSV_PKGCALL] alone, the moment attach returns, into
; drv_fptr7's offset half (kernel/driver.inc, SPEC.md 107.1) - so em_svc is
; whatever lies DSV_PKGCALL bytes ahead of the door, and the 34 bytes there
; are code, never read as cells.
; =============================================================================
em_door:    dw em_pkg           ; DSV_PKGCALL
em_svc      equ em_door - DSV_PKGCALL
em_svc_end  equ em_door + 2     ; (OS88_DRIVER's +15 table length: DSV_SIZE)
%if em_door - $$ < DSV_PKGCALL
    %error "em_door must lie DSV_PKGCALL bytes into the image at least"
%endif

; =============================================================================
; State. Zero-only below OS88_STATE, so it costs the file nothing
; =============================================================================
    OS88_STATE
em_port:    dw 0                ; quarter 0's register
em_frame:   dw 0                ; the frame's segment
em_npg:     dw 0                ; the board's pages
em_qown:    times 4 db 0        ; per quarter: its holder (XOR EM_SLOTX), 0 =
                                ; nobody
em_hlen:    times EMS_NHND dw 0 ; per handle (1..8, index 2 x (h - 1)): its
                                ; length in pages, 0 = a free handle
em_hbo:     times EMS_NHND dw 0 ; ...its first page (low byte) and its owner
                                ; (high byte, the slot XOR EM_SLOTX)

    OS88_DRV_END
