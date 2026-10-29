; =============================================================================
; os8088 - apps/win16/w16load.inc
;
; THE NE LOADER (SPEC.md 124.3). It follows winevdm's krnl386/ne_segment.c
; (NE_LoadSegment, apply_relocations, NE_FixupSegmentPrologs) and task.c
; (the start registers), case for case, with three departures that are
; real mode's:
;
;   * every segment is FIXED, in ONE claim, at a paragraph of its own - no
;     selectors, nothing moves or is discarded (WIN16-PLAN.md D3);
;   * the whole file is read into a temporary claim first, so S2 refuses a
;     program larger than OSAPI_FILE_READ's 64KB;
;   * the relocations are checked in a first pass and applied in a second,
;     so a program with a missing import is refused before any of it is
;     touched, and the refusal can say how many are missing.
;
; tools/os88ne.py reads the same structures and is the reference for every
; offset used below.
; =============================================================================

NE_FLAGS   equ 0x0C             ; the NE header, as SPEC.md 124.3 lists it
NE_AUTODATA equ 0x0E
NE_HEAP    equ 0x10
NE_STACK   equ 0x12
NE_IP      equ 0x14
NE_CS      equ 0x16
NE_SP      equ 0x18
NE_SS      equ 0x1A
NE_CSEG    equ 0x1C
NE_CMOD    equ 0x1E
NE_SEGTAB  equ 0x22
NE_MODTAB  equ 0x28
NE_IMPTAB  equ 0x2A
NE_ENTTAB  equ 0x04
NE_ENTLEN  equ 0x06
NE_ALIGN   equ 0x32
NE_EXETYP  equ 0x36

NEF_MULTI  equ 0x0002           ; ne_flags
NEF_PMODE  equ 0x0008
NEF_SELF   equ 0x0800
NEF_LINKERR equ 0x2000
NEF_LIB    equ 0x8000

NSF_DATA   equ 0x0001           ; segment flags
NSF_ITER   equ 0x0008
NSF_RELOC  equ 0x0100

; -----------------------------------------------------------------------------
; w16_load - read, check, lay out, relocate, and build the first frame
; in:  [w16_name], [w16_dir], [w16_vol]; the wake handler's context
; out: CF=0 ready to resume; CF=1 refused, [w16_l1]/[w16_l2] say why.
;      Clobbers AX, BX, CX, DX
; -----------------------------------------------------------------------------
w16_load:
    push bp
    push si
    push di
    push es
    mov dx, [w16_dir]
    mov bl, [w16_vol]
    call OSAPI_FILE_GOTO_QM
    jnc .there
    mov si, w16_r_disk
    jmp .refuse0
.there:
    mov si, w16_name                ; the size probe: no room, so FERR_BIG
    xor bx, bx                      ; with DX = the KB the read needs
    mov es, bx
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_READ
    jnc .empty
    cmp ax, FERR_BIG
    je .sized
    mov si, w16_r_read
    jmp .refuse0
.empty:
    mov si, w16_r_notwin
    jmp .refuse0
.sized:
    cmp dx, 64
    jbe .fits
    mov si, w16_r_big
    jmp .refuse0
.fits:
    mov bp, dx                      ; BP = KB
    mov ax, dx
    call OSAPI_MEM_CLAIM
    jnc .claimed
    mov si, w16_r_mem
    jmp .refuse0
.claimed:
    mov [w16_fseg], dx
    mov es, dx
    xor bx, bx
    mov ax, bp                      ; capacity = KB * 1024 as DX:CX
    mov cl, 10
    shl ax, cl
    mov dx, bp
    mov cl, 6
    shr dx, cl
    mov cx, ax
    mov si, w16_name
    call OSAPI_FILE_READ            ; DX:AX = the bytes
    jnc .read
    mov si, w16_r_read
    jmp .refuse
.read:
    or dx, dx                       ; 64KB exactly reads as DX = 1, AX = 0
    jz .size16
    mov ax, 0xFFFF
.size16:
    mov [w16_fend], ax

    ; --- MZ, then NE ---------------------------------------------------------
    cmp ax, 0x40
    jb .notwin
    cmp word [es:0], 0x5A4D         ; 'MZ'
    jne .notwin
    cmp word [es:0x18], 0x40        ; a new-format executable says so here
    jb .notwin
    mov si, [es:0x3C]
    mov ax, si
    add ax, 0x40
    jc .notwin
    cmp ax, [w16_fend]
    ja .notwin
    cmp word [es:si], 0x454E        ; 'NE'
    je .isne
.notwin:
    mov si, w16_r_notwin
    jmp .refuse
.isne:
    mov [w16_ne], si
    mov ax, [es:si + NE_FLAGS]
    mov di, w16_r_lib
    test ax, NEF_LIB
    jnz .flagref
    mov di, w16_r_self
    test ax, NEF_SELF
    jnz .flagref
    mov di, w16_r_linkerr
    test ax, NEF_LINKERR
    jnz .flagref
    mov di, w16_r_pmode
    test ax, NEF_PMODE
    jnz .flagref
    mov al, [es:si + NE_EXETYP]
    cmp al, 2                       ; Windows, or 0 (unknown - early linkers)
    je .exeok
    or al, al
    jz .exeok
    mov di, w16_r_os2
.flagref:
    mov si, di
    jmp .refuse
.exeok:
    mov ax, [es:si + NE_CSEG]
    or ax, ax
    jz .notwin
    cmp ax, W16_MAXSEG
    jbe .segok
    mov si, w16_r_segs
    jmp .refuse
.segok:
    mov [w16_nseg], ax
    mov ax, [es:si + NE_AUTODATA]
    or ax, ax
    jz .nodata
    cmp ax, [w16_nseg]
    jbe .dataok
.nodata:
    mov si, w16_r_nodata
    jmp .refuse
.dataok:
    cmp ax, [es:si + NE_SS]         ; the stack must be DGROUP (SPEC.md 124.2)
    jne .nodata
    mov ax, [es:si + NE_CMOD]
    cmp ax, 16
    jbe .modok
    mov si, w16_r_mods
    jmp .refuse
.modok:
    mov [w16_nmod], ax

    call w16_layout                 ; sizes and the claim; CF=1 SI = reason
    jc .refuse
    call w16_copysegs               ; CF=1 SI = reason
    jc .refuse
    call w16_modules                ; which of the imported modules are ours
    xor al, al
    call w16_relocs                 ; pass 0: check every import (CF=1: [w16_l1/2])
    jc .refused
    mov al, 1
    call w16_relocs                 ; pass 1: apply
    jc .refused
    call w16_prologs
    call w16_mkpsp
    call w16_frame                  ; CF=1 SI = reason
    jc .refuse
%ifndef W16_CENSUS_ALWAYS             ; (a test build runs it on any CPU)
    cmp byte [w16_cpu186], 0        ; an 8088: the census decides
    jne .cpuok
%endif
    call w16_census                 ; CF=1: refused, lines written
    jc .refused
.cpuok:
    mov dx, [w16_fseg]              ; the file is no longer needed
    call OSAPI_MEM_FREE
    mov word [w16_fseg], 0
    clc
    jmp short .out

.refuse0:                           ; SI = a line-1 reason, before any claim
.refuse:
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    call w16_strcpy
.refused:
    mov dx, [w16_fseg]
    or dx, dx
    jz .nof
    call OSAPI_MEM_FREE
    mov word [w16_fseg], 0
.nof:
    mov dx, [w16_pseg]
    or dx, dx
    jz .nop
    call OSAPI_MEM_FREE
    mov word [w16_pseg], 0
.nop:
    stc
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; w16_layout - every segment's size and paragraph, then the ONE claim
; in:  ES = the file, [w16_ne]; out: CF=0 [w16_pseg], [w16_sbase], [w16_slen],
;      [w16_dgroup], [w16_dgsize], [w16_psp], [w16_ptop]; CF=1 SI = reason
; DGROUP is minalloc + stack + heap (Wine's NE_CreateSegment), and must fit
; 64KB because SS = DS = it.
; -----------------------------------------------------------------------------
w16_layout:
    push bp
    mov si, [w16_ne]
    mov di, si
    add di, [es:si + NE_SEGTAB]     ; DI -> the segment table
    xor bp, bp                      ; BP = paragraphs so far
    xor bx, bx                      ; BX = segment index * 2
.seg:
    mov ax, [es:di + 2]             ; length in the file (0 = 64KB with data)
    mov dx, [es:di + 6]             ; minalloc (0 = 64KB)
    test word [es:di + 4], NSF_ITER
    jz .noiter
    mov si, w16_r_iter
    pop bp
    stc
    ret
.noiter:
    ; size = max(length, minalloc) as a 17-bit DX:AX, 0 meaning 64KB in each
    xor cx, cx                      ; CX = the high bit
    or dx, dx
    jnz .minset
    inc cx                          ; minalloc 0: 64KB
.minset:
    mov ax, dx
    cmp word [es:di], 0             ; no data in the file: minalloc alone
    je .sized
    cmp word [es:di + 2], 0
    jne .len16
    xor ax, ax                      ; length 0 with data: 64KB
    mov cx, 1
    jmp short .sized
.len16:
    or cx, cx
    jnz .sized                      ; minalloc is already 64KB
    cmp [es:di + 2], ax
    jbe .sized
    mov ax, [es:di + 2]
.sized:
    ; DGROUP? add the stack and the heap
    mov si, [w16_ne]
    mov dx, bx
    shr dx, 1
    inc dx
    cmp dx, [es:si + NE_AUTODATA]
    jne .notdg
    add ax, [es:si + NE_STACK]
    adc cx, 0
    add ax, [es:si + NE_HEAP]
    adc cx, 0
    cmp cx, 1                       ; more than 64KB
    ja .dgbig
    jb .dgok
    or ax, ax
    jz .dgok                        ; exactly 64KB
.dgbig:
    mov si, w16_r_dgbig
    pop bp
    stc
    ret
.dgok:
    mov [w16_dgsize], ax            ; 0 = 64KB
.notdg:
    mov [w16_slen + bx], ax         ; 0 = 64KB
    mov [w16_sbase + bx], bp        ; RELATIVE until the claim is made
    ; paragraphs = ceil(CX:AX / 16)
    add ax, 15
    adc cx, 0
    shr cx, 1
    rcr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1                       ; (CX is 0 after one shift)
    add bp, ax
    add di, 8
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .seg
    mov [w16_psp], bp               ; then the PSP-shaped block, 16 paragraphs
    add bp, 16
    mov ax, bp
    add ax, 63                      ; KB, rounded up
    mov cl, 6
    shr ax, cl
    push ax
    call OSAPI_MEM_CLAIM
    pop cx                          ; CX = KB
    jnc .got
    mov si, w16_r_mem
    pop bp
    stc
    ret
.got:
    mov [w16_pseg], dx
    mov ax, cx                      ; the top: base + KB * 64
    mov cl, 6
    shl ax, cl
    add ax, dx
    mov [w16_ptop], ax
    ; zero the claim, 4KB at a time (BSS, the heap and the stack are zero)
    push es
    mov cx, bp                      ; paragraphs used
    mov bx, dx
.zero:
    mov es, bx
    xor di, di
    xor ax, ax
    mov dx, cx
    cmp dx, 0x100
    jbe .last
    mov dx, 0x100
.last:
    push cx
    mov cx, dx
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; words = paragraphs * 8
    cld
    rep stosw
    pop cx
    add bx, dx
    sub cx, dx
    jnz .zero
    pop es
    ; absolute paragraphs
    mov dx, [w16_pseg]
    xor bx, bx
.abs:
    add [w16_sbase + bx], dx
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .abs
    add [w16_psp], dx
    mov si, [w16_ne]
    mov bx, [es:si + NE_AUTODATA]
    dec bx
    shl bx, 1
    mov ax, [w16_sbase + bx]
    mov [w16_dgroup], ax
    pop bp
    clc
    ret

; -----------------------------------------------------------------------------
; w16_copysegs - each segment's bytes out of the file into its place
; in:  ES = the file; out: CF=1 SI = reason (a segment past the end)
; -----------------------------------------------------------------------------
w16_copysegs:
    push ds
    push es
    mov si, [w16_ne]
    mov cl, [es:si + NE_ALIGN]
    or cl, cl
    jnz .al
    mov cl, 9
.al:
    cmp cl, 15
    ja .bad
    mov di, si
    add di, [es:si + NE_SEGTAB]
    xor bx, bx
.seg:
    mov ax, [es:di]                 ; sector
    or ax, ax
    jz .next                        ; nothing in the file: all zero
    mov dx, [es:di + 2]             ; length
    or dx, dx
    jz .bad                         ; 64KB of data cannot be in a 64KB file
    xor si, si                      ; SI:AX = sector << align, as 32 bits
.sh:
    shl ax, 1
    rcl si, 1
    dec cl
    jnz .sh
    or si, si
    jnz .bad
    mov si, ax
    add ax, dx
    jc .bad
    cmp ax, [w16_fend]
    ja .bad
    push cx
    push di
    push ds
    push es
    mov cx, dx
    mov ax, [w16_sbase + bx]
    push es
    pop ds                          ; DS:SI -> the file's bytes
    mov es, ax
    xor di, di
    cld
    rep movsb
    pop es
    pop ds
    pop di
    pop cx
    mov si, [w16_ne]                ; the shift count again for the next one
    mov cl, [es:si + NE_ALIGN]
    or cl, cl
    jnz .next
    mov cl, 9
.next:
    add di, 8
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .seg
    pop es
    pop ds
    clc
    ret
.bad:
    pop es
    pop ds
    mov si, w16_r_trunc
    stc
    ret

; -----------------------------------------------------------------------------
; w16_modules - [w16_modid]: each imported module's W16M_*, or 0xFF
; in:  ES = the file
; -----------------------------------------------------------------------------
w16_modules:
    push si
    push di
    xor bx, bx
.mod:
    cmp bx, [w16_nmod]
    jae .done
    call w16_modname                ; ES:SI -> the pascal name
    call w16_modfind                ; AL = W16M_* or 0xFF
    mov [w16_modid + bx], al
    inc bx
    jmp short .mod
.done:
    pop di
    pop si
    ret

; w16_modname - ES:SI -> module BX's pascal name in the imported-names table
w16_modname:
    push ax
    push bx
    mov si, [w16_ne]
    mov ax, [es:si + NE_IMPTAB]
    add ax, si
    shl bx, 1
    add bx, si
    add bx, [es:si + NE_MODTAB]
    mov si, [es:bx]
    add si, ax
    pop bx
    pop ax
    ret

; w16_modfind - ES:SI = a pascal name; out AL = its W16M_* or 0xFF
w16_modfind:
    push bx
    push cx
    push di
    mov di, w16_modnames
    xor bx, bx
.try:
    mov cl, [di]
    or cl, cl
    jz .none
    cmp cl, [es:si]
    jne .skip
    push si
    push di
    xor ch, ch
.cmp:
    inc si
    inc di
    mov al, [es:si]
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 0x20
.up:
    cmp al, [di]
    jne .ne
    loop .cmp
    pop di
    pop si
    mov al, bl
    jmp short .out
.ne:
    pop di
    pop si
.skip:
    mov cl, [di]
    xor ch, ch
    add di, cx
    inc di
    inc bx
    jmp short .try
.none:
    mov al, 0xFF
.out:
    pop di
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16_relocs - every segment's relocation records
; in:  AL = 0 check (count what is missing), 1 apply, 2 SEED the census
;      (w16cens.inc: every internal far pointer and offset names code);
;      ES = the file
; out: CF=1 refused: [w16_l1]/[w16_l2] written
; -----------------------------------------------------------------------------
w16_relocs:
    push bp
    push si
    push di
    mov [w16_rpass], al
    mov word [w16_miss], 0
    mov si, [w16_ne]
    mov di, si
    add di, [es:si + NE_SEGTAB]
    xor bx, bx                      ; BX = segment index * 2
.seg:
    test word [es:di + 4], NSF_RELOC
    jz .nextseg
    mov ax, [es:di]
    or ax, ax
    jz .nextseg
    call w16_secoff                 ; AX = the segment's file offset
    add ax, [es:di + 2]             ; ...its relocation table follows the data
    jc .trunc
    mov si, ax
    add ax, 2
    jc .trunc
    cmp ax, [w16_fend]
    ja .trunc
    mov cx, [es:si]                 ; the record count
    mov ax, cx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    jc .trunc
    add ax, si
    jc .trunc
    add ax, 2
    jc .trunc
    cmp ax, [w16_fend]
    ja .trunc
    add si, 2
    jcxz .nextseg
.rec:
    push cx
    call w16_reloc1                 ; ES:SI -> the record, BX = the segment
    pop cx
    jc .fail
    add si, 8
    loop .rec
.nextseg:
    add di, 8
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .seg
    cmp word [w16_miss], 0          ; pass 0: anything missing is the refusal
    je .ok
    call w16_missmsg
.fail:
    stc
    jmp short .out
.trunc:
    mov si, w16_r_trunc
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    call w16_strcpy
    stc
    jmp short .out
.ok:
    clc
.out:
    pop di
    pop si
    pop bp
    ret

; w16_secoff - AX = sector -> AX = sector << align (the file is < 64KB, so
; anything that does not fit 16 bits is past its end: AX = 0xFFFF then)
w16_secoff:
    push cx
    push dx
    push si
    mov si, [w16_ne]
    mov cl, [es:si + NE_ALIGN]
    or cl, cl
    jnz .al
    mov cl, 9
.al:
    xor dx, dx
.sh:
    shl ax, 1
    rcl dx, 1
    dec cl
    jnz .sh
    or dx, dx
    jz .ok
    mov ax, 0xFFFF
.ok:
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16_reloc1 - one record (winevdm's apply_relocations, one iteration)
; in:  ES:SI -> {addr type, kind, offset, target1, target2}; BX = segment * 2;
;      [w16_rpass]. out: CF=1 a refusal written (pass 1 only - pass 0 counts)
; -----------------------------------------------------------------------------
w16_reloc1:
    push bx
    push si
    push di
    mov [w16_rseg], bx
    mov al, [es:si + 1]
    and al, 3
    cmp al, 3                       ; an OS fixup: the x87's (SPEC.md 124.5),
    jne .notos                      ; left as native code for now
    jmp .ok
.notos:
    cmp al, 0
    je .internal
    mov dx, [es:si + 4]             ; target1 = the module, 1-based
    dec dx
    cmp dx, [w16_nmod]
    jae .missing
    mov di, dx
    mov dl, [w16_modid + di]
    cmp dl, 0xFF
    je .missing
    cmp al, 2                       ; by name: none of ours is named
    je .missing
    mov ax, [es:si + 6]             ; the ordinal
    call w16_findexp                ; DL = module, AX = ordinal -> AX = row
    jc .missing
    mov dx, ax                      ; the stub: our CS : w16_stubs + row * 6
    shl ax, 1
    add ax, dx
    shl ax, 1
    add ax, w16_stubs
    mov dx, cs
    jmp short .apply
.missing:
    cmp word [w16_miss], 0
    jne .count
    mov ax, [es:si + 4]             ; remember the first: module and target
    mov [w16_mmod], ax
    mov ax, [es:si + 6]
    mov [w16_mord], ax
    mov al, [es:si + 1]
    mov [w16_mkind], al
.count:
    inc word [w16_miss]
    jmp .ok
.internal:
    mov al, [es:si + 4]             ; target1's low byte: a segment, or 0xFF
    cmp al, 0xFF
    jne .fixed
    mov ax, [es:si + 6]             ; a movable entry: its ordinal
    call w16_entrypt                ; -> DX = segment number, AX = offset
    jc .bad
    jmp short .seg
.fixed:
    xor ah, ah
    mov dx, ax
    mov ax, [es:si + 6]
.seg:
    or dx, dx
    jz .bad
    cmp dx, [w16_nseg]
    ja .bad
    cmp byte [w16_rpass], 2         ; the census's seeds: os88ne.py takes
    jne .segaddr                    ; RA_FARPTR (3) and RA_OFFSET (5)
    mov cl, [es:si]
    and cl, 0x7F
    cmp cl, 3
    je .seedit
    cmp cl, 5
    jne .ok
.seedit:
    call w16c_seed                  ; DX = the segment number, AX = offset
    jmp short .ok
.segaddr:
    mov di, dx
    dec di
    shl di, 1
    mov dx, [w16_sbase + di]
.apply:                             ; DX:AX = the target
    cmp byte [w16_rpass], 1         ; only pass 1 writes
    jne .ok
    mov cl, [es:si]                 ; the address type
    and cl, 0x7F
    mov di, [es:si + 2]             ; the first site
    mov ch, [es:si + 1]
    push es
    mov bx, [w16_rseg]
    mov es, [w16_sbase + bx]
    call w16_inrange                ; the FIRST site must be in the segment
    jc .applied
    test ch, 4                      ; additive: that one site, added
    jz .chain
    call w16_site
    jmp short .applied
.chain:                             ; a chain: each site holds the next, read
    mov bp, 0x4000                  ; BEFORE it is written; 0xFFFF ends it,
.link:                              ; and so - silently, as in winevdm - does
    mov bx, [es:di]                 ; a next site outside the segment. BP
    call w16_site                   ; bounds a chain that loops
    jc .applied
    cmp bx, di
    je .chained
    mov di, bx
    cmp di, 0xFFFF
    je .chained
    call w16_inrange
    jc .chained
    dec bp
    jnz .link
.chained:
    clc
.applied:
    pop es
    jc .bad
.ok:
    clc
    pop di
    pop si
    pop bx
    ret
.bad:
    pop di
    pop si
    pop bx
    mov si, w16_r_reloc
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    call w16_strcpy
    stc
    ret

; w16_inrange - CF=1 if a WORD at DI is not inside segment [w16_rseg]
w16_inrange:
    push bx
    mov bx, [w16_rseg]
    mov bx, [w16_slen + bx]
    or bx, bx
    jz .big                         ; 64KB: any offset but the very last
    dec bx
    cmp di, bx                      ; DI + 2 <= length
    jae .out                        ; (CF=0 here would be wrong: jae is CF=0)
    pop bx
    clc
    ret
.big:
    cmp di, 0xFFFF
    je .out
    pop bx
    clc
    ret
.out:
    pop bx
    stc
    ret

; w16_site - write DX:AX at ES:DI by address type CL; CH bit 2 = additive.
; out CF=1 an address type WIN16 does not know
w16_site:
    test ch, 4
    jnz .add
    cmp cl, 0                       ; low byte
    je .lob
    cmp cl, 2                       ; segment
    je .sel
    cmp cl, 3                       ; far pointer
    je .far
    cmp cl, 5                       ; offset
    je .off
.bad:
    stc
    ret
.lob:
    mov [es:di], al
    clc
    ret
.sel:
    mov [es:di], dx
    clc
    ret
.far:
    mov [es:di], ax
    mov [es:di + 2], dx
    clc
    ret
.off:
    mov [es:di], ax
    clc
    ret
.add:
    cmp cl, 0
    je .alob
    cmp cl, 2
    je .asel
    cmp cl, 3
    je .afar
    cmp cl, 5
    jne .bad
    add [es:di], ax
    clc
    ret
.alob:
    add [es:di], al
    clc
    ret
.asel:
    cmp word [es:di], 0             ; Borland's additive selector at zero
    jne .aok
    mov [es:di], dx
.aok:
    clc
    ret
.afar:
    add [es:di], ax
    mov [es:di + 2], dx
    clc
    ret

; -----------------------------------------------------------------------------
; w16_entrypt - entry ordinal AX -> DX = segment number, AX = offset
; out: CF=1 no such entry, or a constant. ES = the file. The entry table is
; bundles: a count, an indicator (0 = skip that many ordinals, 0xFF =
; movable, 6 bytes an entry: flags, int 3Fh, segment, offset; 0xFE = a
; constant; else the fixed segment, 3 bytes an entry: flags, offset).
; -----------------------------------------------------------------------------
w16_entrypt:
    push bx
    push cx
    push si
    push di
    push bp
    mov si, [w16_ne]
    mov bx, si
    add si, [es:bx + NE_ENTTAB]
    mov bx, ax                      ; BX = the ordinal wanted
    mov cx, 1                       ; CX = this bundle's first ordinal
.bundle:
    xor ax, ax
    mov al, [es:si]                 ; AX = count
    or al, al
    jz .none
    mov dl, [es:si + 1]             ; DL = indicator
    add si, 2
    or dl, dl
    jnz .live
    add cx, ax
    jmp short .bundle
.live:
    mov di, 3
    cmp dl, 0xFF
    jne .size
    mov di, 6
.size:
    mov bp, bx
    sub bp, cx                      ; the index in this bundle
    jb .none
    cmp bp, ax
    jb .here
    add cx, ax                      ; past it: skip count * size bytes
    push dx
    mul di
    pop dx
    add si, ax
    jmp short .bundle
.here:
    mov ax, bp
    push dx
    mul di
    pop dx
    add si, ax                      ; SI -> the entry
    cmp dl, 0xFF
    je .movable
    cmp dl, 0xFE
    je .none                        ; a constant names no code
    mov ax, [es:si + 1]             ; fixed: flags, offset; the segment is DL
    xor dh, dh
    jmp short .got
.movable:
    mov dl, [es:si + 3]
    xor dh, dh
    mov ax, [es:si + 4]
.got:
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    clc
    ret
.none:
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; w16_prologs - patch the exported prologs (winevdm's NE_FixupSegmentPrologs)
; in:  ES = the file
; An exported far function compiled for Windows begins `push ds / pop ax /
; nop` (1E 58 90) or `mov ax, ds / nop` (8C D8 90), then `inc bp / push bp /
; mov bp, sp / push ds / mov ds, ax`. Windows' loader rewrote the first three
; bytes: to NOPs in a program with instance data, so the DS a caller sets in
; AX survives into the function (WIN16 sets AX = DGROUP when it calls one -
; S3); to `mov ax, DGROUP` where the entry is flagged as shared data.
; -----------------------------------------------------------------------------
w16_prologs:
    push si
    push di
    push bp
    mov si, [w16_ne]
    mov bx, si
    add si, [es:bx + NE_ENTTAB]
.bundle:
    xor cx, cx
    mov cl, [es:si]                 ; count
    jcxz .done
    mov dl, [es:si + 1]             ; indicator
    add si, 2
    or dl, dl
    jz .bundle                      ; skipped ordinals carry no entries
    cmp dl, 0xFE
    jne .entries
    mov ax, 3                       ; constants: 3 bytes each, nothing to do
    mul cx
    add si, ax
    jmp short .bundle
.entries:
    mov al, [es:si]                 ; flags
    cmp dl, 0xFF
    je .mov
    mov bl, dl                      ; fixed: the indicator is the segment
    mov di, [es:si + 1]
    add si, 3
    jmp short .one
.mov:
    mov bl, [es:si + 3]
    mov di, [es:si + 4]
    add si, 6
.one:
    call w16_prolog1                ; AL = flags, BL = segment, DI = offset
    loop .entries
    jmp short .bundle
.done:
    pop bp
    pop di
    pop si
    ret

; w16_prolog1 - one entry: AL = its flags, BL = its segment number, DI = offset
w16_prolog1:
    push ax
    push bx
    push dx
    push si
    push es
    xor bh, bh
    or bl, bl
    jz .out
    cmp bx, [w16_nseg]
    ja .out
    dec bx
    shl bx, 1
    mov [w16_rseg], bx
    push ax
    push es
    mov si, [w16_ne]                ; code segments only
    mov dx, si
    add dx, [es:si + NE_SEGTAB]
    mov si, bx
    shl si, 1
    shl si, 1
    add si, dx
    test word [es:si + 4], NSF_DATA
    pop es
    pop ax                          ; (pop keeps the flags)
    jnz .out
    mov si, [w16_ne]
    mov dx, [es:si + NE_FLAGS]
    mov es, [w16_sbase + bx]
    push di
    inc di                          ; three bytes must fit
    call w16_inrange
    pop di
    jc .out
    cmp byte [es:di + 2], 0x90
    jne .out
    cmp word [es:di], 0x581E        ; push ds / pop ax
    jne .movds
    mov word [es:di], 0xD88C        ; -> mov ax, ds
.movds:
    cmp word [es:di], 0xD88C
    jne .out
    test al, 2                      ; shared data: mov ax, DGROUP
    jz .inst
    mov byte [es:di], 0xB8
    mov bx, [w16_dgroup]
    mov [es:di + 1], bx
    jmp short .out
.inst:
    test dx, NEF_MULTI              ; instance data and exported: the caller's
    jz .out                         ; AX stands
    test al, 1
    jz .out
    mov word [es:di], 0x9090
.out:
    pop es
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_mkpsp - the PSP-shaped block the program's ES points at (SPEC.md 124.3)
; -----------------------------------------------------------------------------
w16_mkpsp:
    push es
    mov es, [w16_psp]
    mov word [es:0], 0x20CD         ; int 20h
    mov ax, [w16_ptop]
    mov [es:2], ax                  ; the paragraph past the program
    mov word [es:0x2C], 0           ; no environment
    mov byte [es:0x80], 0           ; an empty command tail
    mov byte [es:0x81], 0x0D
    pop es
    ret

; -----------------------------------------------------------------------------
; w16_frame - the first frame: a resume that "finishes" a call into the entry
; in:  ES = the file; out: CF=1 SI = reason
; The start registers are Wine's TASK_CallToStart: BX = the stack size, CX =
; the heap size, DI = hInstance, SI = hPrevInstance (0), ES = the PSP, DS =
; SS = DGROUP, and SP from the header or just past the static data and the
; stack (NE_InitProcess).
; -----------------------------------------------------------------------------
w16_frame:
    push di
    mov si, [w16_ne]
    mov bx, [es:si + NE_CS]
    or bx, bx
    jz .bad
    cmp bx, [w16_nseg]
    ja .bad
    dec bx
    shl bx, 1
    mov ax, [w16_sbase + bx]
    mov [w16_scratch], ax           ; the entry's CS
    mov ax, [es:si + NE_SP]
    or ax, ax
    jnz .sp
    mov ax, [w16_dgsize]            ; static data + stack = DGROUP - the heap
    sub ax, [es:si + NE_HEAP]
.sp:
    and al, 0xFE
    or ax, ax
    jnz .sp2
    mov ax, 0xFFFE                  ; a full 64KB DGROUP
.sp2:
    sub ax, W16F_ARGS
    jb .bad
    mov di, ax
    mov [w16_psp_], ax
    mov ax, [w16_dgroup]
    mov [w16_pss], ax
    mov bx, [es:si + NE_IP]
    mov cx, [es:si + NE_STACK]
    mov dx, [es:si + NE_HEAP]
    push es
    mov es, ax
    mov [es:di + W16F_DI], ax       ; hInstance = the DGROUP paragraph
    mov word [es:di + W16F_SI], 0   ; hPrevInstance: every instance is first
    mov [es:di + W16F_DS], ax
    mov word [es:di + W16F_BP], 0
    mov [es:di + W16F_IP], bx
    mov ax, [w16_scratch]
    mov [es:di + W16F_CS], ax
    pop es
    mov [w16_rbx], cx
    mov [w16_rcx], dx
    mov ax, [w16_psp]
    mov [w16_res], ax
    mov word [w16_nargb], 0
    mov byte [w16_rflags], 0
    pop di
    clc
    ret
.bad:
    pop di
    mov si, w16_r_entry
    stc
    ret

; -----------------------------------------------------------------------------
; w16_missmsg - the refusal for imports WIN16 lacks: "Needs USER.287 and 52
; more" / "which WIN16 does not provide yet." (SPEC.md 124.3). ES = the file.
; -----------------------------------------------------------------------------
w16_missmsg:
    push si
    push di
    mov si, w16_r_needs
    mov di, w16_l1
    call w16_strcpy
    mov bx, [w16_mmod]
    dec bx
    cmp bx, [w16_nmod]
    jae .nomod
    call w16_modname                ; ES:SI -> pascal
    call w16_pcopy
    jmp short .dot
.nomod:
    mov byte [di], '?'
    inc di
.dot:
    mov byte [di], '.'
    inc di
    cmp byte [w16_mkind], 2
    jne .ord
    mov si, [w16_ne]                ; by name: the imported-names table
    mov ax, [es:si + NE_IMPTAB]
    add ax, si
    add ax, [w16_mord]
    mov si, ax
    call w16_pcopy
    jmp short .more
.ord:
    mov ax, [w16_mord]
    call w16_putdec
.more:
    mov ax, [w16_miss]
    dec ax
    jz .end
    mov si, w16_r_and
    call w16_strcpy
    call w16_putdec
    mov si, w16_r_more
    call w16_strcpy
.end:
    mov byte [di], 0
    mov si, w16_r_notyet
    mov di, w16_l2
    call w16_strcpy
    pop di
    pop si
    ret

; w16_pcopy - ES:SI pascal string -> DI (at most 24 characters), DI past it
w16_pcopy:
    push cx
    xor cx, cx
    mov cl, [es:si]
    cmp cx, 24
    jbe .n
    mov cx, 24
.n:
    jcxz .z
.c:
    inc si
    mov al, [es:si]
    mov [di], al
    inc di
    loop .c
.z:
    mov byte [di], 0
    pop cx
    ret


; --- the loader's own bss, beside the package's -----------------------------
; (declared here, in the image, rather than in w16_* bss: they are scratch
; for one load and a few bytes)
w16_fend:   dw 0                    ; the file's last valid offset + 1
w16_rpass:  db 0
w16_mkind:  db 0
w16_rseg:   dw 0
w16_miss:   dw 0
w16_mmod:   dw 0
w16_mord:   dw 0

; --- refusals: line 1 (line 2 is the file name unless said) -----------------
w16_r_disk:    db 'Cannot reach the disk it is on.', 0
w16_r_read:    db 'Cannot read the file.', 0
w16_r_notwin:  db 'This is not a Windows program.', 0
w16_r_big:     db 'Larger than 64KB: not yet.', 0
w16_r_mem:     db 'Not enough memory to load it.', 0
w16_r_lib:     db 'A Windows library, not a program.', 0
w16_r_self:    db 'It loads itself: not supported.', 0
w16_r_linkerr: db 'Its linker reported errors.', 0
w16_r_pmode:   db 'It needs protected mode.', 0
w16_r_os2:     db 'An OS/2 or later program.', 0
w16_r_segs:    db 'Too many segments.', 0
w16_r_nodata:  db 'It has no usable data segment.', 0
w16_r_mods:    db 'It imports too many modules.', 0
w16_r_iter:    db 'An iterated segment: not supported.', 0
w16_r_dgbig:   db 'Its data segment is over 64KB.', 0
w16_r_trunc:   db 'The file is damaged.', 0
w16_r_reloc:   db 'A relocation WIN16 cannot apply.', 0
w16_r_entry:   db 'Its entry point is not in it.', 0
w16_r_needs:   db 'Needs ', 0
w16_r_and:     db ' and ', 0
w16_r_more:    db ' more', 0
w16_r_notyet:  db 'which WIN16 does not provide yet.', 0
