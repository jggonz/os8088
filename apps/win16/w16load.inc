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

; =============================================================================
; PART 5, W16P_LOAD (SPEC.md 124.18.7): the loader and the census run once a
; program, so their CODE is a part, fetched by the wake handler (w16_ldin) and
; dropped when the load returns. Their tables, strings and scratch stay in the
; image in `section .text`, read through DS, which is ours on both sides; a
; call into the image is ICALL, as the dialog part's are, and the image's own
; address in a stub's far pointer is DS, never this part's CS.
; =============================================================================
section .w16ld follows=.w16fp vstart=0 align=1
w16l_base:
    jmp short w16l_go               ; +0  tools/os88ovl.py: a short jump ...
    db 'W16LOADR'                   ; +2  ...before the cut mark
w16l_go:
; w16_loadf - w16_load, far-called by w16_ldin
w16_loadf:
    call w16_load
    retf

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
    call OSAPI_FILE_DFREE           ; BX = sectors a cluster: READ_AT's unit
    jc .c512                        ;   (SPEC.md 124.10), asked once, here
    mov cl, 9
    shl bx, cl
    jnz .clus
.c512:
    mov bx, 512
.clus:
    mov [w16_clus], bx
    mov ax, W16_FCACHE * 1024       ; the cache: 8KB, or one cluster when a
    cmp bx, ax                      ;   cluster is larger (SPEC.md 124.20.1)
    jbe .csz
    mov ax, bx
.csz:
    mov [w16_csz], ax
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM
    jnc .cached
    mov si, w16_r_mem
    jmp .refuse0
.cached:
    mov [w16_cseg], dx
    mov word [w16_clen], 0

    ; --- MZ, then NE ---------------------------------------------------------
    push ds
    pop es
    mov di, w16_hdr
    xor ax, ax
    xor dx, dx
    mov cx, 0x40
    call w16l_fread
    jc .notwin
    cmp word [w16_hdr], 0x5A4D      ; 'MZ'
    jne .notwin
    cmp word [w16_hdr + 0x18], 0x40 ; a new-format executable says so here
    jb .notwin
    mov ax, [w16_hdr + 0x3C]        ; e_lfanew, 32 bits (SETUP.EXE 3.1's is
    mov dx, [w16_hdr + 0x3E]        ;   230KB into the file)
    mov [w16_soff], ax
    mov [w16_soff + 2], dx
    mov di, w16_hdr
    mov cx, 0x40
    call w16l_fread
    jc .notwin
    cmp word [w16_hdr], 0x454E      ; 'NE'
    jne .notwin
    call w16l_tabend                ; AX = the tables' extent, CF=1 too far
    jc .bigtab
    mov [w16_fend], ax
    mov [w16_hpos], ax
    inc word [w16_hpos]             ; the records start word-aligned
    and word [w16_hpos], 0xFFFE
    add ax, 1023
    mov cl, 10
    shr ax, cl
    mov [w16_hkb], ax
    call OSAPI_MEM_CLAIM
    jnc .tclaimed
    mov si, w16_r_mem
    jmp .refuse
.tclaimed:
    mov [w16_fseg], dx
    mov es, dx
    xor di, di
    mov ax, [w16_soff]
    mov dx, [w16_soff + 2]
    mov cx, [w16_fend]
    call w16l_fread
    jnc .tread
    mov si, w16_r_trunc
    jmp .refuse
.bigtab:
    mov si, w16_r_tabs
    jmp .refuse
.notwin:
    mov si, w16_r_notwin
    jmp .refuse
.tread:
    xor si, si                      ; the NE header is at the claim's 0
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

    call w16_bindinit               ; nothing bound yet (SPEC.md 124.19.1)
    call w16_layout                 ; sizes and the claim; CF=1 SI = reason
    jc .refuse
    call w16_copysegs               ; CF=1 SI = reason; ES = the tables NOW
    jc .refuse                      ;   (they grew, and may have moved)
    call w16l_cfree                 ; the cache's work is done
    ICALL w16_copyres                ; the resource table, kept (SPEC.md 124.10)
    jc .refuse
    call w16_modules                ; which of the imported modules are ours
    mov word [w16_nfix], 0
    xor al, al
    call w16_relocs                 ; pass 0: check every import (CF=1: [w16_l1/2])
    jc .refused
    ICALL w16x_mode                  ; the x87: native, emulated, or neither
    jc .refuse
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
    ICALL w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    ICALL w16_strcpy
.refused:
    call w16l_cfree
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
; 64KB because SS = DS = it. It is laid out at the WHOLE 64KB when the arena
; has room, so the local heap can grow to its end, and at that size alone
; when not (SPEC.md 124.9): a segment here never moves, so DGROUP cannot be
; grown later the way Windows grew it.
; -----------------------------------------------------------------------------
w16_layout:
    mov byte [w16_dgfull], 1
    call w16_lay1
    jnc .out
    cmp si, w16_r_mem
    jne .no
    cmp byte [w16_dgfull], 0
    je .no
    mov byte [w16_dgfull], 0        ; the size the program asked for, then
    call w16_lay1
    ret
.no:
    stc
.out:
    ret

w16_lay1:
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
    mov [w16_lbase], ax             ; the heap starts here (CX=1: refused below)
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
    cmp byte [w16_dgfull], 0
    je .dgset
    xor ax, ax                      ; the whole 64KB, the heap's to grow into
    mov cx, 1
.dgset:
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
    jc .huge                        ; a megabyte of segments
    add di, 8
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .seg
    mov [w16_psp], bp               ; then the PSP-shaped block, 16 paragraphs
    add bp, 16
    jc .huge
    mov [w16_rtab], bp              ; then the kept resource table (w16res.inc)
    ICALL w16_rlayout
    add bp, ax
    jc .huge
    mov ax, bp
    add ax, 63                      ; KB, rounded up: past 64KB since S15
    rcr ax, 1                       ;   (SPEC.md 124.20.2), so the carry is
    mov cl, 5                       ;   bit 16
    shr ax, cl
    push ax
    call OSAPI_MEM_CLAIM
    pop cx                          ; CX = KB
    jnc .got
.huge:
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
    add [w16_rtab], dx
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
; w16_copysegs - each segment's bytes out of the file into its place, and its
; relocation records onto the end of the tables (SPEC.md 124.20.1)
; in:  ES = the tables; out: CF=1 SI = reason; ES = the tables, which grew and
;      may have MOVED ([w16_fseg] is current either way)
; -----------------------------------------------------------------------------
w16_copysegs:
    push bp
    mov di, [w16_ne]
    add di, [es:di + NE_SEGTAB]     ; DI = the segment table's offset in ES
    xor bx, bx
.seg:
    mov word [w16_rtoff + bx], 0
    mov ax, [es:di]                 ; sector
    or ax, ax
    jz .next                        ; nothing in the file: all zero
    call w16l_secoff                ; DX:AX = its offset in the file
    mov [w16_soff], ax
    mov [w16_soff + 2], dx
    mov cx, [es:di + 2]             ; length, 0 = 64KB
    push es
    push di
    mov es, [w16_sbase + bx]
    xor di, di
    jcxz .whole
    call w16l_fread
    jmp short .copied
.whole:
    mov cx, 0x8000                  ; 64KB is two halves: CX cannot say it
    call w16l_fread
    jc .copied
    mov ax, [w16_soff]
    mov dx, [w16_soff + 2]
    add ax, 0x8000
    adc dx, 0
    mov di, 0x8000
    mov cx, 0x8000
    call w16l_fread
.copied:
    pop di
    pop es
    jc .trunc
    mov es, [w16_fseg]
    test word [es:di + 4], NSF_RELOC
    jz .next
    mov ax, [w16_soff]              ; the records follow the bytes
    mov dx, [w16_soff + 2]
    mov cx, [es:di + 2]
    add ax, cx
    adc dx, 0
    or cx, cx
    jnz .roff
    inc dx                          ; after 64KB
.roff:
    mov [w16_soff], ax
    mov [w16_soff + 2], dx
    push es
    push di
    push ds
    pop es
    mov di, w16_hdr                 ; the count, first
    mov cx, 2
    call w16l_fread
    pop di
    pop es
    jc .trunc
    mov ax, [w16_hdr]
    cmp ax, 0x1FFE                  ; more than any 64KB could hold
    ja .big
    mov cl, 3
    shl ax, cl
    add ax, 2                       ; AX = the count and the records
    mov cx, ax
    add ax, [w16_hpos]
    jc .big
    cmp ax, 0xFFF0
    ja .big
    call w16l_hgrow                 ; to AX bytes; ES = the tables, CF=1 none
    jc .mem
    push di
    mov di, [w16_hpos]
    mov [w16_rtoff + bx], di
    add [w16_hpos], cx
    mov ax, [w16_soff]
    mov dx, [w16_soff + 2]
    call w16l_fread
    pop di
    jc .trunc
.next:
    add di, 8
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .seg
    mov es, [w16_fseg]
    pop bp
    clc
    ret
.trunc:
    mov si, w16_r_trunc
    jmp short .fail
.big:
    mov si, w16_r_tabs
    jmp short .fail
.mem:
    mov si, w16_r_mem
.fail:
    mov es, [w16_fseg]
    pop bp
    stc
    ret

; w16l_secoff - AX = a sector -> DX:AX = sector << ne_align (ES = the tables)
w16l_secoff:
    push cx
    push si
    mov si, [w16_ne]
    mov cl, [es:si + NE_ALIGN]
    or cl, cl
    jnz .al
    mov cl, 9
.al:
    and cl, 15
    xor dx, dx
.sh:
    shl ax, 1
    rcl dx, 1
    dec cl
    jnz .sh
    pop si
    pop cx
    ret

; w16l_hgrow - make the tables' claim hold AX bytes. out: CF=0 ES = it (and
; [w16_fseg]); CF=1 no memory. Keeps all but ES.
w16l_hgrow:
    push ax
    push cx
    push dx
    add ax, 1023
    rcr ax, 1                       ; (the carry is bit 16)
    mov cl, 9
    shr ax, cl                      ; KB
    cmp ax, [w16_hkb]
    jbe .fits
    mov dx, [w16_fseg]
    call OSAPI_MEM_REGROW           ; DX = its base now
    jc .out
    mov [w16_fseg], dx
    mov [w16_hkb], ax
.fits:
    mov es, [w16_fseg]
    clc
.out:
    pop dx
    pop cx
    pop ax
    ret

; w16l_tabend - the NE header in [w16_hdr]: AX = how far its tables reach
; from it, CF=1 past 64KB. Each table's offset is a word from the header;
; the imported names have no length and run to the entry table (SPEC.md
; 124.20.1), so they are taken to end where the farthest other table does.
w16l_tabend:
    push bx
    push cx
    mov ax, [w16_hdr + NE_CSEG]     ; segments: 8 bytes each
    mov cl, 3
    shl ax, cl
    jc .far
    add ax, [w16_hdr + NE_SEGTAB]
    jc .far
    mov bx, [w16_hdr + NE_CMOD]     ; modules: a word each
    shl bx, 1
    jc .far
    add bx, [w16_hdr + NE_MODTAB]
    jc .far
    call .max
    mov bx, [w16_hdr + NE_ENTTAB]
    add bx, [w16_hdr + NE_ENTLEN]
    jc .far
    call .max
    mov bx, [w16_hdr + NE_RSRCTAB]
    call .max
    mov bx, [w16_hdr + NE_RESTAB]
    call .max
    mov bx, [w16_hdr + NE_IMPTAB]
    inc bx
    jz .far
    call .max
    cmp ax, 0x40                    ; at least the header itself
    jae .ok
    mov ax, 0x40
.ok:
    cmp ax, 0xF000                  ; room for the relocations behind them
    ja .far
    clc
    jmp short .out
.far:
    stc
.out:
    pop cx
    pop bx
    ret
.max:
    cmp bx, ax
    jbe .m
    mov ax, bx
.m:
    ret

; -----------------------------------------------------------------------------
; w16l_fread - CX bytes (1-65535) at file offset DX:AX to ES:DI, through the
; cluster cache (SPEC.md 124.20.1). The program's folder is current.
; out: CF=1 the file ended first or would not read. Keeps all but AX, CX, DX
; (and the bytes at ES:DI).
; -----------------------------------------------------------------------------
w16l_fread:
    push bx
    push si
    push di
    push bp
    cld
.loop:
    jcxz .done
    call .hit                       ; BX = the offset in the cache, CF=1 miss
    jnc .copy
    call .fill
    jc .err
    call .hit
    jc .err                         ; past what the file holds
.copy:
    mov bp, [w16_clen]
    sub bp, bx                      ; BP = what the cache has from there
    cmp bp, cx
    jbe .n
    mov bp, cx
.n:
    push cx
    push ds
    mov si, bx
    mov cx, bp
    mov ds, [w16_cseg]
    rep movsb
    pop ds
    pop cx
    add ax, bp
    adc dx, 0
    sub cx, bp
    jmp short .loop
.done:
    clc
    jmp short .out
.err:
    stc
.out:
    pop bp
    pop di
    pop si
    pop bx
    ret
; .hit - DX:AX inside the cache? BX = its offset there, CF=0; else CF=1
.hit:
    push dx
    mov bx, ax
    sub bx, [w16_cbase]
    sbb dx, [w16_cbase + 2]
    jnz .no                         ; below it, or 64KB and more past it
    cmp bx, [w16_clen]
    jae .no
    pop dx
    clc
    ret
.no:
    pop dx
    stc
    ret
; .fill - the cache from the cluster holding DX:AX. CF=1 a read error
.fill:
    push ax
    push cx
    push dx
    push es
    push di
    mov bx, [w16_clus]
    dec bx
    not bx
    and ax, bx
    mov [w16_cbase], ax
    mov [w16_cbase + 2], dx
    mov word [w16_clen], 0
    mov es, [w16_cseg]
    xor bx, bx
    mov cx, [w16_csz]
    mov si, w16_name
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    jc .ferr
    or dx, dx
    jz .f16
    mov ax, [w16_csz]
.f16:
    mov [w16_clen], ax
    clc
.ferr:
    pop di
    pop es
    pop dx
    pop cx
    pop ax
    ret

; w16l_cfree - the cache, given back. Keeps all.
w16l_cfree:
    push dx
    xor dx, dx
    xchg dx, [w16_cseg]
    or dx, dx
    jz .none
    call OSAPI_MEM_FREE
.none:
    pop dx
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
    mov si, [w16_ne]
    mov di, si
    add di, [es:si + NE_SEGTAB]
    xor bx, bx                      ; BX = segment index * 2
.seg:
    test word [es:di + 4], NSF_RELOC
    jz .nextseg
    mov si, [w16_rtoff + bx]        ; read with the segment (SPEC.md 124.20.1)
    or si, si
    jz .nextseg
    mov cx, [es:si]                 ; the record count
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
    jmp short .ok                   ; a missing import is bound, not refused
.fail:
    stc
    jmp short .out
.trunc:
    mov si, w16_r_trunc
    mov di, w16_l1
    ICALL w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    ICALL w16_strcpy
    stc
    jmp short .out
.ok:
    clc
.out:
    pop di
    pop si
    pop bp
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
    cmp al, 3                       ; an OS fixup: the x87's (SPEC.md 124.5)
    jne .notos
    cmp byte [w16_rpass], 1
    je .osfix
    ja .os0
    inc word [w16_nfix]             ; pass 0 counts them
.os0:
    jmp .ok
.osfix:
    mov di, [es:si + 2]             ; one site, patched for this machine
    mov cl, [es:si + 4]
    push es
    mov bx, [w16_rseg]
    mov es, [w16_sbase + bx]
    ICALL w16x_osfix
    pop es
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
    cmp al, 2                       ; by name: the few names we know
    jne .byord
    call w16_byname                 ;   (w16_bynm) as the ordinals they are
    jc .missing
    jmp short .named
.byord:
    mov ax, [es:si + 6]             ; the ordinal
.named:
    call w16_findexp                ; DL = module, AX = ordinal -> AX = row
    jc .missing
    cmp ax, W16_D3ROW               ; DOS3Call: int 21h itself, no gate
    jne .nd3                        ;   (SPEC.md 124.15.3)
    mov ax, w16k_dos3call
    mov dx, ds                      ; the image's segment (this part's CS is not)
    jmp .apply
.nd3:
    cmp ax, W16_FPROW               ; __FPMATH: the emulator's, or ours
    jne .nfp
    cmp byte [w16_rpass], 0
    jne .fpbind
    inc word [w16_nfix]
    jmp .ok
.fpbind:
    xor ax, ax
    mov dx, [w16_fpseg]
    cmp byte [w16_fpm], W16FP_EMU
    je .apply
    mov ax, w16x_fpmath
    mov dx, ds                      ; the image's segment (this part's CS is not)
    jmp .apply
.nfp:
    cmp ax, W16_WFROW               ; __WINFLAGS: a constant, its value
    jne .ahinc
    ICALL w16k_getwinflags           ; AX (DX = 0)
    jmp .apply
.ahinc:
    mov dx, 0x1000                  ; __AHINCR and __AHSHIFT: their values
    cmp ax, W16_AHROW               ;   (SPEC.md 124.20.3)
    je .equate
    mov dx, 12
    cmp ax, W16_AHROW + 1
    jne .stub
.equate:
    mov ax, dx
    xor dx, dx
    jmp .apply
.stub:
    call w16_bindrow                ; the row's slot (SPEC.md 124.19.1)
    jc .nbind
    mov dx, ax                      ; the stub: our CS : w16_stubs + slot * 6
    shl ax, 1
    add ax, dx
    shl ax, 1
    add ax, w16_stubs
    mov dx, ds                      ; the image's segment (this part's CS is not)
    jmp .apply
.nbind:
    pop di
    pop si
    pop bx
    mov si, w16_r_nbind
    jmp .refusal
.missing:                           ; SPEC.md 124.12: bound to a stub that
    cmp byte [w16_rpass], 1         ;   ends the program, naming it, if it is
    jne .ok                         ;   ever CALLED
    mov ax, [es:si + 4]
    mov [w16_mmod], ax
    mov ax, [es:si + 6]
    mov [w16_mord], ax
    mov al, [es:si + 1]
    and al, 3
    mov [w16_mkind], al
    call w16_missbind               ; -> DX:AX = the stub; CF=1 the table is full
    jnc .apply
    pop di
    pop si
    pop bx
    mov si, w16_r_toomany
.refusal:
    mov di, w16_l1
    ICALL w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    ICALL w16_strcpy
    stc
    ret
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
    ICALL w16_inrange                ; the FIRST site must be in the segment
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
    ICALL w16_inrange
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
    ICALL w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    ICALL w16_strcpy
    stc
    ret

; w16_byname - an import by NAME, as the ordinal it is (SPEC.md 124.5.2:
; CALC 3.00 imports WIN87EM's __FPMATH so). in: DL = the module, ES:SI -> the
; record, ES = the file; out: AX = the ordinal, CF=1 not a name in w16_bynm
w16_byname:
    push bx
    push cx
    push si
    push di
    mov bx, [w16_ne]
    mov ax, [es:bx + NE_IMPTAB]
    add ax, bx
    jc .no
    add ax, [es:si + 6]             ; target2: the name's offset in the table
    jc .no
    mov si, ax
    inc ax
    jz .no
    cmp ax, [w16_fend]
    jae .no
    mov cl, [es:si]                 ; the whole name inside the file
    xor ch, ch
    add ax, cx
    jc .no
    cmp ax, [w16_fend]
    ja .no
    mov di, w16_bynm
.t:
    mov al, [di]
    cmp al, 0xFF
    je .no
    cmp al, dl
    jne .skip
    mov cl, [di + 3]
    cmp cl, [es:si]
    jne .skip
    xor ch, ch
    xor bx, bx
.c:
    mov al, [di + 4 + bx]
    cmp al, [es:si + 1 + bx]
    jne .skip
    inc bx
    loop .c
    mov ax, [di + 1]
    clc
    jmp short .out
.skip:
    mov cl, [di + 3]
    xor ch, ch
    add di, cx
    add di, 4
    jmp short .t
.no:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

section .text                       ; (w16_inrange is w16x87.inc's too)
w16_bynm:                           ; module, ordinal, the name (pascal)
    db W16M_WIN87EM
    dw 1
    db 8, '__FPMATH'
    db 0xFF

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

section .w16ld
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
    ICALL w16_inrange
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
    mov ax, [w16_lbase]             ; static data + stack: the heap's start
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
    mov [w16_heap], dx
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
; w16_missbind - an import WIN16 does not provide (SPEC.md 124.12): its name,
; "USER.287" or "COMMDLG.GETOPENFILENAME", into the next free slot of
; w16_mtxt unless a slot already holds it, and that slot's stub
; in:  [w16_mmod] (1-based), [w16_mord], [w16_mkind]; ES = the file
; out: DX:AX = the stub (the image's segment); CF=1 W16_NMISS are already taken
; -----------------------------------------------------------------------------
w16_missbind:
    push bx
    push cx
    push si
    push di
    mov di, w16_mbtext              ; the name, composed (at most 50), then cut
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
    jmp short .named
.ord:
    mov ax, [w16_mord]
    ICALL w16_putdec
.named:
    mov byte [di], 0
    mov byte [w16_mbtext + W16_MTXT - 1], 0
    xor bx, bx                      ; BX = the slot
.find:
    cmp bx, [w16_nmiss]
    jae .new
    mov si, w16_mbtext              ; the same name: the same stub
    ICALL w16_mslot                  ; DI -> slot BX's text
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .next
    inc si
    inc di
    or al, al
    jnz .cmp
    jmp short .stub
.next:
    inc bx
    jmp short .find
.new:
    cmp bx, W16_NMISS
    jae .full
    inc word [w16_nmiss]
    mov si, w16_mbtext
    ICALL w16_mslot
    ICALL w16_strcpy
.stub:                              ; name slot BX's bind slot (124.19.1):
    mov di, bx                      ;   given out on its first import
    shl di, 1
    mov ax, [cs:w16l_missslot + di]
    or ax, ax
    jnz .bound
    mov ax, [w16_nbind]
    cmp ax, W16_NBIND
    jae .full
    inc word [w16_nbind]
    mov [cs:w16l_missslot + di], ax
    mov di, ax
    shl di, 1
    shl di, 1
    mov byte [w16_bind + di + W16B_ARGB], 0
    mov byte [w16_bind + di + W16B_PART], W16B_MISS
    mov [w16_bind + di + W16B_PROC], bx
.bound:
    mov dx, ax                      ; the stub: our CS : w16_stubs + slot * 6
    shl ax, 1
    add ax, dx
    shl ax, 1
    add ax, w16_stubs
    mov dx, ds                      ; the image's segment (this part's CS is not)
    clc
    jmp short .out
.full:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

section .text                       ; (w16api.inc's too)
; w16_mslot - DI -> missing-import slot BX's text (W16_MTXT = 24 bytes each)
w16_mslot:
    push ax
    mov ax, bx
    shl ax, 1
    add ax, bx                      ; * 3
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; * 24
    add ax, w16_mtxt
    mov di, ax
    pop ax
    ret

section .w16ld
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


; -----------------------------------------------------------------------------
; w16_findexp - DL = module, AX = ordinal -> AX = the row; CF=1 not ours.
; The rows are this part's own (SPEC.md 124.19.1), read through CS.
; -----------------------------------------------------------------------------
w16_findexp:
    push bx
    push cx
    mov bx, w16_exports             ; XSEC-OK: read through cs:
    xor cx, cx
.row:
    cmp cx, W16_NX
    jae .none
    cmp [cs:bx + W16X_MOD], dl
    jne .next
    cmp [cs:bx + W16X_ORD], ax
    je .hit
.next:
    add bx, W16X_SIZE
    inc cx
    jmp short .row
.hit:
    mov ax, cx
    pop cx
    pop bx
    clc
    ret
.none:
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; w16_bindinit - a fresh bind table (SPEC.md 124.19.1): the image's four
; slots written, nothing of a program's bound yet. Keeps every register.
; -----------------------------------------------------------------------------
w16_bindinit:
    push ax
    push cx
    push si
    push di
    push es
    push cs
    pop es
    mov di, w16l_rowslot            ; XSEC-OK: written through ES = CS
    mov cx, W16_NX + W16_NMISS      ;   (w16l_missslot follows it)
    xor ax, ax
    rep stosw
    mov word [cs:w16l_rowslot + 2 * W16_XDDP], W16_DDPROW
    push ds
    pop es
    mov si, w16_fixed               ; XSEC-OK: read through cs:
    mov di, w16_bind
    mov cx, W16B_SIZE * W16_NFIXED
.c:
    cs lodsb
    stosb
    loop .c
    mov word [w16_nbind], W16_NFIXED
%ifdef W16_TRACE
    mov di, w16_trow                ; ES = DS here
    mov cx, W16_NBIND
    mov ax, 0xFFFF
    rep stosw
%endif
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_bindrow - AX = an export row -> AX = its bind slot, given out and
; written on the row's first import. CF=1 every slot is taken. Keeps the rest.
; -----------------------------------------------------------------------------
w16_bindrow:
    push bx
    push cx
    push dx
    push si
    mov bx, ax
    shl bx, 1
    mov dx, [cs:w16l_rowslot + bx]  ; (0 is the image's own slot, never a row's)
    or dx, dx
    jnz .have
    mov dx, [w16_nbind]
    cmp dx, W16_NBIND
    jae .full
    inc word [w16_nbind]
    mov [cs:w16l_rowslot + bx], dx
%ifdef W16_TRACE
    push bx
    mov bx, dx
    shl bx, 1
    mov [w16_trow + bx], ax
    pop bx
%endif
    mov cx, W16X_SIZE               ; the row: AX * 7
    mul cx
    mov si, ax
    add si, w16_exports             ; XSEC-OK: read through cs:
    mov bx, [cs:w16l_rowslot + bx]
    shl bx, 1
    shl bx, 1
    mov al, [cs:si + W16X_ARGB]
    mov [w16_bind + bx + W16B_ARGB], al
    mov al, [cs:si + W16X_PART]
    mov [w16_bind + bx + W16B_PART], al
    mov ax, [cs:si + W16X_PROC]
    mov [w16_bind + bx + W16B_PROC], ax
    shr bx, 1
    shr bx, 1
    mov dx, bx
.have:
    mov ax, dx
    clc
    jmp short .out
.full:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

section .text
; --- the loader's own bss, beside the package's -----------------------------
; (declared here, in the image, rather than in w16_* bss: they are scratch
; for one load and a few bytes)
w16_fend:   dw 0                    ; the file's last valid offset + 1
w16_rpass:  db 0
w16_mkind:  db 0
w16_rseg:   dw 0
w16_mmod:   dw 0
w16_mord:   dw 0

; --- refusals: line 1 (line 2 is the file name unless said) -----------------
w16_r_disk:    db 'Cannot reach the disk it is on.', 0
w16_r_read:    db 'Cannot read the file.', 0
w16_r_notwin:  db 'This is not a Windows program.', 0
w16_r_tabs:    db 'Its tables are larger than WIN16 can read.', 0
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
w16_r_toomany: db 'It calls too much WIN16 lacks.', 0
w16_r_nbind:   db 'Imports more than WIN16 can bind (384).', 0
