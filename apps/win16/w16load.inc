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

W16_NSEGV equ 10                    ; w16l_segv's words (w16api.inc checks
%define W16_NSEGVP 10               ;   the segment equates' rows against it)

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
NE_NRESN   equ 0x20             ; the non-resident names' length...
NE_NRESOFF equ 0x2C             ; ...and their offset in the FILE, 32 bits
NE_EXETYP  equ 0x36

NEF_MULTI  equ 0x0002           ; ne_flags
NEF_PMODE  equ 0x0008
NEF_SELF   equ 0x0800
NEF_LINKERR equ 0x2000
NEF_LIB    equ 0x8000

NSF_DATA   equ 0x0001           ; segment flags
NSF_ITER   equ 0x0008
NSF_PRELOAD equ 0x0040
NSF_RELOC  equ 0x0100
NSF_DISCARD equ 0x1000

W16CL_BASE equ 0                ; the live table (SPEC.md 124.26.4), a segment:
W16CL_AGE  equ 2                ;   its paragraph (0 = not loaded), its age,
W16CL_FL   equ 4                ;   and W16CLF_*
W16CL_SIZE equ 6
W16CLF_DISC equ 1               ; discardable (124.26.1)
W16CLF_KEEP equ 2               ; ...but named by its number: never discarded
W16CLF_NEW  equ 4               ; ...its block taken at a launch, not yet filled
W16TK_SIZE equ 8                ; a thunk (124.26.2): jmp/call far, the segment,
W16TK_SEG  equ 5                ;   the entry's offset
W16TK_OFF  equ 6

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
; w16l_far - the image's one way in (w16_ldcall): near proc BX far-called.
; Every register and the flags pass through both ways.
w16l_far:
    call bx
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
%ifdef PKG_MEGA
    xor ax, ax                      ; W16SEG0's window: segment 0 as a data
    xor cx, cx                      ;   selector, 64 KB (SEG_WIN, the same
    cwd                             ;   window at every load; SPEC.md
    mov bl, SEG_WIN                 ;   124.31.10). Refused, it stays 0 and
    call OSAPI_SEG                  ;   every access traps, as before
    jc .noseg0
    mov [w16_seg0], dx
.noseg0:
%endif
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
    call w16l_hclaim                ; the tables, to the paragraph
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
%ifndef PKG_MEGA                   ; the arm runs one (SPEC.md 124.31.4)
    mov di, w16_r_pmode
    test ax, NEF_PMODE
    jnz .flagref
%else
    push ax                         ; ...and its locked blocks may move
    and al, NEF_PMODE               ;   (SPEC.md 124.31.3)
    mov [w16_pmonly], al
    pop ax
%endif
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
    call w16l_opbegin               ; ONE OPERATION from here (SPEC.md 12.8.10)

    cmp byte [w16_ntk], 0           ; nothing bound yet (SPEC.md 124.19.1),
    jne .bound                      ;   unless a task runs: the table is
    call w16_bindinit               ;   every task's (124.28.2)
.bound:
    call w16_modules                ; which of the imported modules are ours,
    call w16l_libs                  ;   and its own libraries, loaded first
    jc .refused                     ;   (SPEC.md 124.23.1): lines written
    mov es, [w16_fseg]
    mov byte [w16_cdo], 1           ; the program's own: discardable code is
                                    ;   not laid out here (SPEC.md 124.26.1)
    call w16_layout                 ; sizes and the claim; CF=1 SI = reason
    jc .refuse
    call w16_copysegs               ; CF=1 SI = reason; ES = the tables NOW
    jc .refuse                      ;   (they grew, and may have moved)
    ICALL w16_copyres                ; the resource table, kept (SPEC.md 124.10)
    jc .refuse
    call w16l_cmake                 ; the module block (124.26.4); CF=1 SI = reason
    jc .refuse
    mov word [w16_nfix], 0
    xor al, al
    call w16_relocs                 ; pass 0: check every import (CF=1: [w16_l1/2])
    jc .refused
    ICALL w16x_mode                  ; the x87: native, emulated, or neither
    jc .refuse
    call w16l_segsup                ; the discardable ones loaded at launch
    jc .refused                     ;   (124.26.3), CF=1 lines written
    mov al, 1
    call w16_relocs                 ; pass 1: apply, in the claim's segments
    jc .refused
    call w16_prologs
    call w16l_cfree                 ; the cache's work is done
    call w16_mkpsp
    call w16_frame                  ; CF=1 SI = reason
    jc .refuse
%ifndef PKG_MEGA                   ; (the arm: a 286 or better, no census)
%ifndef W16_CENSUS_ALWAYS             ; (a test build runs it on any CPU)
    cmp byte [w16_cpu186], 0        ; an 8088: the census decides
    jne .cpuok
%endif
    call w16_census                 ; CF=1: refused, lines written
    jc .refused
.cpuok:
%endif
    call w16l_keep                  ; part 5 kept for loading segments later
    jc .refuse                      ;   (124.26.3); CF=1 SI = reason
    mov es, [w16_fseg]              ; its module name, kept for
    mov si, [w16_ne]                ;   GetModuleHandle: the resident
    add si, [es:si + NE_RESTAB]     ;   names' first, 8 at most
    mov cl, [es:si]
    xor ch, ch
    cmp cx, 8
    jbe .mn
    mov cx, 8
.mn:
    mov di, w16_mname
    jcxz .mz
.mc:
    inc si
    mov al, [es:si]
    mov [di], al
    inc di
    loop .mc
.mz:
    mov byte [di], 0
    mov dx, [w16_fseg]              ; the file is no longer needed
    call OSAPI_MEM_FREE
    mov word [w16_fseg], 0
    mov byte [w16_cdo], 0
    mov al, [w16_cur]               ; the task's record (SPEC.md 124.28.1)
    mov ah, W16K_SIZE
    mul ah
    mov bx, ax
    mov ax, [w16_dgroup]
    mov [w16_task + bx + W16K_H], ax
    mov byte [w16_task + bx + W16K_ST], W16KS_RUN
    mov al, [w16_pgblk]              ; a claim (0) or a block (1) (124.28.5)
    mov [w16_task + bx + W16K_FL], al
    inc byte [w16_ntk]
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
    mov byte [w16_cdo], 0
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
    call w16l_pfree
    mov word [w16_pseg], 0
.nop:
    stc
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; w16l_pclaim - AX = PARAGRAPHS: the program's segments' claim (w16_lay1)
; -> DX, CX = the paragraphs granted; CF=1 none. The first program's and the
; second task's are claims of WIN16's own; a later one's, or one whose claim is
; refused, is a block of the global heap (SPEC.md 124.28.5). [w16_pgblk] says
; which was given (1 = a block). Both in paragraphs (SPEC.md 50.3.7).
; Clobbers AX.
w16l_pclaim:
    mov byte [w16_pgblk], 0
    cmp byte [w16_ntk], 2           ; two tasks hold a claim at most
    jae .blk
    mov cx, ax
    W16VCLAIM                       ; AX = the paragraphs GRANTED (the
    jnc .got                        ;   arm: entered in the map, 124.31.3)
    cmp byte [w16_ntk], 0
    jne .try                        ; a later task goes on to a block
    stc                             ; the first program: refused (the cmp
    ret                             ;   above cleared CF)
.got:
    xchg ax, cx
    ret
.try:
    mov ax, cx
.blk:
    mov byte [w16_pgblk], 1
    mov cx, ax                      ; a heap block is what it was asked for
    ICALL w16_kclaimp
    ret

; w16l_pfree - DX = what w16l_pclaim gave: given back
w16l_pfree:
    cmp byte [w16_pgblk], 0
    jne .blk
    W16VFREE
    ret
.blk:
    ICALL w16_kfree
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
    mov word [w16_sbase + bx], 0
    call w16l_isdisc                ; discardable code: a block of the global
    jc .nextseg                     ;   heap when it is loaded (124.26.3)
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
.nextseg:
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
    mov ax, bp                      ; PARAGRAPHS (SPEC.md 50.3.7): the image
    call w16l_pclaim                ;   pays the heap's granule, not a whole
    jnc .got                        ;   KB. CX = the paragraphs GRANTED
.huge:
    mov si, w16_r_mem
    pop bp
    stc
    ret
.got:
    mov [w16_pseg], dx
    mov ax, cx                      ; the top: base + what was granted
    add ax, dx
    mov [w16_ptop], ax
    ; zero the claim, 4KB at a time (BSS, the heap and the stack are zero)
    push es
    mov cx, bp                      ; paragraphs used
    mov bx, dx
.zero:
    W16SEG es, bx
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
    call w16l_isdisc                ; (not loaded: 0 stays)
    jc .absn
    add [w16_sbase + bx], dx
.absn:
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
%ifdef PKG_MEGA
    push cx                         ; the program's: a selector (124.31.3)
    mov cx, [w16_dgsize]
    call w16l_preach
    xor bl, bl
    ICALL w16_psel
    pop cx
%endif
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
    call w16l_isdisc                ; discardable: its bytes are read when it
    jc .recs                        ;   is loaded (124.26.3), its records now
    push es
    push di
    W16SEG es, [w16_sbase + bx]
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
.recs:
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

; w16l_opbegin - begin the load's ONE file operation (SPEC.md 12.8.10), sized
; from what a load really reads: every segment with bytes in the file that is
; not discardable code - those are read when first called, under no bar
; (SPEC.md 124.26.3) - and, on the first program, the SMALLEST System font's
; packed length, which w16_facein fetches before w16_wake ends it. A later
; task's load ends at its own w16_ldin call (w16task.inc). The tables already
; read and a library's segments are not in it: an under-estimate, which
; clamps full early, where the whole file would be an over-estimate that
; never fills. Refused (a wider operation open), the transfers report as
; always. in: ES = the tables, [w16_ne], [w16_nseg]. Keeps all
w16l_opbegin:
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, [w16_cdo]
    push ax
    mov byte [w16_cdo], 1           ; the program's own: isdisc answers
    mov di, [w16_ne]
    add di, [es:di + NE_SEGTAB]
    xor bx, bx
    xor cx, cx                      ; DX:CX = the bytes
    xor dx, dx
.seg:
    cmp word [es:di], 0             ; nothing in the file
    je .next
    call w16l_isdisc
    jc .next
    mov ax, [es:di + 2]             ; length, 0 = 64KB
    add cx, ax
    adc dx, 0
    or ax, ax
    jnz .next
    inc dx
.next:
    add di, 8
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .seg
    cmp byte [w16_ntk], 0           ; the first program: and its face
    jne .go
    add cx, [op_table + OP_T_ROWS + W16P_SYS08 * OP_ROW + OP_R_ZKB]
    adc dx, 0
.go:
    mov ax, cx
    or ax, dx
    jz .none                        ; DX:CX = 0 would be an END
    call OSAPI_FS_OP
.none:
    pop ax
    mov [w16_cdo], al
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16l_b2p - AX = bytes -> AX = paragraphs, rounded up (to 1000h for FFF1h
; and over: the add's carry is the quotient's bit 12). Keeps all but AX and
; the flags
w16l_b2p:
    add ax, 15
    rcr ax, 1                       ; (the carry is bit 16)
    shr ax, 1
    shr ax, 1
    shr ax, 1
    ret

; w16l_hclaim - AX = the tables' bytes: claimed in PARAGRAPHS (SPEC.md
; 50.3.7) -> DX, [w16_htp] = the paragraphs granted; CF=1 none. Keeps all
; but AX, CX, DX
w16l_hclaim:
    call w16l_b2p
    push bx
    xor cx, cx                      ; no page-safe head
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P                ; AX = the paragraphs GRANTED
    pop bx
    mov [w16_htp], ax
    ret

; w16l_hgrow - make the tables' claim hold AX bytes. out: CF=0 ES = it (and
; [w16_fseg]); CF=1 no memory. Keeps all but ES.
w16l_hgrow:
    push ax
    push bx
    push cx
    push dx
    call w16l_b2p                   ; AX = paragraphs (SPEC.md 50.3.7)
    cmp ax, [w16_htp]
    jbe .fits
    mov dx, [w16_fseg]
    mov bl, MEMP_REGROW
    call OSAPI_MEM_P                ; DX = its base now, AX = granted
    jc .out
    mov [w16_fseg], dx
    mov [w16_htp], ax
.fits:
    mov es, [w16_fseg]
    clc
.out:
    pop dx
    pop cx
    pop bx
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
    cmp byte [w16_rpass], 1         ; applying: a discardable segment's own are
    jne .recs                       ;   applied as it is loaded (124.26.3)
    call w16l_isdisc
    jc .nextseg
.recs:
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
    W16SEG es, [w16_sbase + bx]
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
    test dl, W16M_LIB               ; a library's: its own address (124.23.3)
    jnz .lib
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
    W16ICS dx                       ; the image's segment (this part's CS is not)
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
%ifdef PKG_MEGA
    mov dx, [w16x_fcs]              ; the copy's code half (w16x87.inc)
%else
    mov dx, [w16_fpseg]
%endif
    cmp byte [w16_fpm], W16FP_EMU
    je .apply
    mov ax, w16x_fpmath
    W16ICS dx                       ; the image's segment (this part's CS is not)
    jmp .apply
.nfp:
    cmp ax, W16_WFROW               ; __WINFLAGS: a constant, its value
    jne .ahinc
    ICALL w16k_getwinflags           ; AX (DX = 0)
    jmp .apply
.ahinc:
    mov dx, ax                      ; the segment equates: their paragraphs
    sub dx, W16_SEGROW              ;   (SPEC.md 124.27.1)
    cmp dx, W16_NSEGV
    jae .ahi
    push di
    mov di, dx
    shl di, 1
    mov dx, [cs:w16l_segv + di]
    pop di
%ifdef PKG_MEGA
    push ax                         ; the arm: a 64 KB window over it
    push bx                         ;   (SEG_WIN; SPEC.md 124.31.6), the
    push cx                         ;   same one at every fixup
    xor ax, ax
    xor cx, cx
    mov bl, SEG_WIN
    call OSAPI_SEG                  ; DX = the window
    pop cx
    pop bx
    pop ax
%endif
    jmp short .equate
.ahi:
%ifdef PKG_MEGA
    mov dx, 8                       ; __AHINCR and __AHSHIFT: a selector's
    cmp ax, W16_AHROW               ;   step (SPEC.md 124.31.6)
    je .equate
    mov dx, 3
%else
    mov dx, 0x1000                  ; __AHINCR and __AHSHIFT: their values
    cmp ax, W16_AHROW               ;   (SPEC.md 124.20.3)
    je .equate
    mov dx, 12
%endif
    cmp ax, W16_AHROW + 1
    jne .stub
.equate:
    mov ax, dx
    xor dx, dx
    jmp .apply
.stub:
    call w16_bindrow                ; the row's slot (SPEC.md 124.19.1)
    jc .nbind
    mov dx, ax                      ; the stub: our CS : w16_stubs + slot * 4
    shl ax, 1
    shl ax, 1
    add ax, w16_stubs
    W16ICS dx                       ; the image's segment (this part's CS is not)
    jmp .apply
.nbind:
    pop di
    pop si
    pop bx
    mov si, w16_r_nbind
    jmp .refusal
.lib:
    cmp byte [w16_rpass], 1         ; pass 0 checks nothing (bound, or bound
    jne .ok                         ;   missing), and pass 2 seeds nothing
    mov bl, dl
    and bl, ~W16M_LIB & 0xFF
    xor cl, cl                      ; by ordinal: CL = 0, AX = it
    mov al, [es:si + 1]             ; the kind, again
    and al, 3
    cmp al, 2
    mov ax, [es:si + 6]             ; (mov keeps the flags)
    jne .libord
    mov di, [w16_ne]                ; by name: the imported-names table
    add ax, [es:di + NE_IMPTAB]
    add ax, di
    mov di, ax
    mov cl, [es:di]
    inc di
    or cl, cl
    jz .missing
.libord:
    ICALL w16_libproc               ; -> DX:AX, CF=1 not exported
    jc .missing
    push ax                         ; a library's discardable code is a thunk:
    mov al, [es:si]                 ;   a far pointer to it may be one, a
    and al, 0x7F                    ;   segment or an offset alone may not
    cmp al, 3                       ;   (SPEC.md 124.26.11)
    pop ax
    je .apply
    call w16l_isthk
    jnc .apply
    pop di
    pop si
    pop bx
    mov si, w16_r_reloc
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
    test byte [es:NE_FLAGS], NEF_PMODE ; a call: through the thunk when the
    jnz .ownseg                     ;   segment is discardable (124.26.2);
                                    ;   a protected-mode-only program's,
                                    ;   any type, the segment's own, kept
                                    ;   (124.26.2.1)
    call w16l_tothk
    jc .apply
    jmp short .seg
.fixed:
    xor ah, ah
    mov dx, ax
    mov ax, [es:si + 6]
.ownseg:
    call w16l_keepck                ; a discardable one named so: kept (124.26.1)
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
%ifndef PKG_MEGA
    call w16c_seed                  ; DX = the segment number, AX = offset
%endif
    jmp short .ok
.segaddr:
    mov di, dx
    dec di
    shl di, 1
    mov dx, [w16_sbase + di]
%ifdef PKG_MEGA
    push ax                         ; the segment's selector (124.31.4)
    push bx
    mov bx, di
    mov ax, dx
    call w16l_ssel
    mov dx, ax
    pop bx
    pop ax
%endif
.apply:                             ; DX:AX = the target
    cmp byte [w16_rpass], 1         ; only pass 1 writes
    jne .ok
    mov cl, [es:si]                 ; the address type
    and cl, 0x7F
    mov di, [es:si + 2]             ; the first site
    mov ch, [es:si + 1]
    push es
    mov bx, [w16_rseg]
    W16SEG es, [w16_sbase + bx]
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

; the segment equates' values, in their rows' order (W16_SEGROW, w16api.inc):
; __ROMBIOS, __0000H, __0040H, __A000H, __B000H, __B800H, __C000H, __D000H,
; __E000H, __F000H
w16l_segv:                          ; (W16_NSEGV words, the file's head)
    dw 0xF000, 0x0000, 0x0040, 0xA000, 0xB000, 0xB800, 0xC000, 0xD000
    dw 0xE000, 0xF000

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
%ifdef PKG_MEGA
; w16l_ssel - the arm's selector for one of the program's segments (SPEC.md
; 124.31.4): AX = its paragraph ([w16_sbase + BX]), BX = its index * 2, ES =
; the tables -> AX = SEG_SUBC's code half for a code segment, a data selector
; for a data one, each reaching [w16_slen + BX]. Keeps every other register.
w16l_ssel:
    push cx
    push si
    mov si, [w16_ne]
    add si, [es:si + NE_SEGTAB]
    shl bx, 1
    shl bx, 1
    mov cl, 1
    test word [es:si + bx + 4], NSF_DATA
    jz .code
    xor cl, cl
.code:
    shr bx, 1
    shr bx, 1
    ICALL w16_ssel
    pop si
    pop cx
    ret

; w16l_thkcs - DX = the thunks' paragraph -> DX = the module block's code
; half there, which a program far-calls a thunk through. Keeps the rest.
w16l_thkcs:
    push ax
    push bx
    push cx
    mov ax, dx
    xor cx, cx
    mov bl, 1
    ICALL w16_psel
    mov dx, ax
    pop cx
    pop bx
    pop ax
    ret

; w16l_preach - CX = bytes (0 = 64KB) -> CX = paragraphs that reach them, 0
; meaning 64KB (w16_psel's). Keeps every other register.
w16l_preach:
    add cx, 15
    jc .all
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    ret
.all:
    xor cx, cx
    ret
%endif
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
    call w16_site4                  ; four bytes, where w16_inrange checked
    jc .bad                         ;   the first two
    mov [es:di], ax
    jmp short .far2
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
    call w16_site4
    jc .bad
    add [es:di], ax
.far2:
    mov [es:di + 2], dx
    clc
    ret

; w16_site4 - CF=1 if a far pointer at DI runs past segment [w16_rseg]:
; DI <= length - 4, 0 (64KB) giving FFFC. A segment shorter than 4 bytes is
; a paragraph of its claim, so its far pointer still lands in its own padding.
w16_site4:
    push bx
    mov bx, [w16_rseg]
    mov bx, [w16_slen + bx]
    sub bx, 4
    cmp bx, di
    pop bx
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
    mov ax, si                      ; the count and the indicator inside the
    inc ax                          ;   file, as w16c_ents bounds them
    cmp ax, [w16_fend]
    jae .done
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
    jc .done
    jmp short .bundle
.entries:
    mov ax, 3                       ; the whole entry inside the file: a
    cmp dl, 0xFF                    ; fixed one is 3 bytes, a movable 6
    jne .sz
    mov al, 6
.sz:
    add ax, si
    jc .done
    cmp ax, [w16_fend]
    ja .done
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
    cmp word [w16_pone], 0          ; one segment's only (124.26.3): its
    je .anyseg                      ;   index * 2 + 1
    push bx
    inc bx
    cmp bx, [w16_pone]
    pop bx
    jne .out
.anyseg:
    cmp word [w16_sbase + bx], 0    ; not loaded
    je .out
    mov si, [w16_ne]
    mov dx, [es:si + NE_FLAGS]
    W16SEG es, [w16_sbase + bx]
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
    W16SEG es, [w16_psp]
    mov word [es:0], 0x20CD         ; int 20h
    mov ax, [w16_ptop]
    mov [es:2], ax                  ; the paragraph past the program
    mov word [es:0x2C], 0           ; no environment
    mov byte [es:0x80], 0           ; an empty command tail
    mov byte [es:0x81], 0           ; (lpCmdLine is the text: InitTask)
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
%ifdef PKG_MEGA
    call w16l_ssel                  ; its code selector (124.31.4)
%endif
    mov [w16_scratch], ax           ; the entry's CS
    mov ax, [es:si + NE_SP]
    or ax, ax
    jnz .spck
    mov ax, [w16_lbase]             ; static data + stack: the heap's start
    jmp short .sp
.spck:
    mov dx, [w16_dgsize]            ; the header's SP must be inside DGROUP:
    dec dx                          ;   SP - 1 <= size - 1, 0 (64KB) being
    dec ax                          ;   FFFF on both sides
    cmp ax, dx
    ja .bad
    inc ax
.sp:
    and al, 0xFE
    or ax, ax
    jnz .sp2
    mov ax, 0xFFFE                  ; a full 64KB DGROUP
.sp2:
    mov [w16_stop], ax              ; the top of its stack (SPEC.md 124.26.5)
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
    W16PSP                          ; the program's ES
    mov [w16_res], ax
    mov word [w16_nargb], 0
    mov byte [w16_rflags], 0
    call w16l_lmfirst               ; LibMain owed: a stub before the entry
    pop di
    clc
    ret
.bad:
    pop di
    mov si, w16_r_entry
    stc
    ret

; w16l_lmfirst - a library owed its LibMain (SPEC.md 124.23.4): the first
; frame moved down four bytes, so the entry's IP:CS lies above it, and its
; return pointed at W16_LMROW's stub - the "call" the entry seems to make
; first. Keeps every register.
w16l_lmfirst:
    push ax
    push cx
    push si
    push di
    push es
    mov si, w16_libs
    mov cx, W16_NLIB
.find:
    cmp byte [si + W16L_COUNT], 0
    je .next
    cmp byte [si + W16L_OWED], 0
    jne .owed
.next:
    add si, W16L_SIZE
    loop .find
    jmp short .out
.owed:
    mov es, [w16_pss]
    mov si, [w16_psp_]
    lea di, [si - 4]
    mov [w16_psp_], di
    mov cx, 6                       ; DI SI DS BP: the IP and CS stay above
.mv:
    mov ax, [es:si]
    mov [es:di], ax
    inc si
    inc si
    inc di
    inc di
    loop .mv
    mov di, [w16_psp_]
    mov word [es:di + W16F_IP], w16_stubs + W16_STUBSZ * W16_LMROW
%ifdef PKG_MEGA
    W16ICS ax
    mov [es:di + W16F_CS], ax
%else
    mov [es:di + W16F_CS], ds
%endif
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
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
    mov dx, ax                      ; the stub: our CS : w16_stubs + slot * 4
    shl ax, 1
    shl ax, 1
    add ax, w16_stubs
    W16ICS dx                       ; the image's segment (this part's CS is not)
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
    W16CSEG es
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
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_bindrow - AX = an export row -> AX = its bind slot, given out and
; written on the row's first import, or one already bound to the same handler
; by an earlier load. CF=1 every slot is taken. Keeps the rest.
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
    push ax                         ; SI = the row
    mov cx, W16X_SIZE
    mul cx
    mov si, ax
    add si, w16_exports             ; XSEC-OK: read through cs:
    pop ax
    push bx                         ; a slot another task's load bound to
    mov bx, 4 * W16_NFIXED          ;   the same handler serves this row as
    mov dx, W16_NFIXED              ;   well: a fresh copy of the loader
.same:                              ;   knows no rows (SPEC.md 124.19.1)
    cmp dx, [w16_nbind]
    jae .new
    mov cl, [cs:si + W16X_ARGB]
    cmp [w16_bind + bx + W16B_ARGB], cl
    jne .next
    mov cl, [cs:si + W16X_PART]
    cmp [w16_bind + bx + W16B_PART], cl
    jne .next
    mov cx, [cs:si + W16X_PROC]
    cmp [w16_bind + bx + W16B_PROC], cx
    je .found
.next:
    add bx, W16B_SIZE
    inc dx
    jmp short .same
.found:
    pop bx
    mov [cs:w16l_rowslot + bx], dx
    jmp short .have
.new:
    pop bx
    mov dx, [w16_nbind]
    cmp dx, W16_NBIND
    jae .full
    inc word [w16_nbind]
    mov [cs:w16l_rowslot + bx], dx
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

; =============================================================================
; A PROGRAM'S OWN LIBRARIES (SPEC.md 124.23). A module WIN16 does not provide
; is <NAME>.DLL in the program's folder, loaded here before the module that
; imports it is laid out: its segments in the global heap, its imports bound,
; its prologs patched, its census taken, and what later calls read kept in a
; module block. The importer's loader state is saved around it (w16l_push).
; =============================================================================

; w16l_libs - each module of [w16_modid] that is not WIN16's, as a library:
; [w16_modid] becomes W16M_LIB + its record, or stays 0FFh when its file is
; not there. ES = the tables. out: CF=1 refused, [w16_l1]/[w16_l2] written
w16l_libs:
    push bx
    push si
    xor bx, bx
.mod:
    cmp bx, [w16_nmod]
    jae .done
    cmp byte [w16_modid + bx], 0xFF
    jne .next
    mov es, [w16_fseg]
    call w16_modname                ; ES:SI -> its pascal name
    call w16l_need                  ; AL = the record, 0FFh none; CF=1 refused
    jc .out
    cmp al, 0xFF
    je .next
    or al, W16M_LIB
    mov [w16_modid + bx], al
.next:
    inc bx
    jmp short .mod
.done:
    clc
.out:
    mov es, [w16_fseg]
    pop si
    pop bx
    ret

; w16l_need - ES:SI = a module's pascal name: a library already loaded (its
; count raised), or its file loaded now. out: CF=0 AL = the record or 0FFh
; (no such file); CF=1 refused, the lines written. Keeps the rest.
w16l_need:
    push cx
    push si
    push di
    mov di, w16_lname               ; the file: up to 8 of it, then .DLL
    mov cl, [es:si]
    xor ch, ch
    cmp cx, 8
    jbe .n
    mov cx, 8
.n:
    jcxz .ext
.c:
    inc si
    mov al, [es:si]
    cmp al, 'a'
    jb .u
    cmp al, 'z'
    ja .u
    sub al, 0x20
.u:
    mov [di], al
    inc di
    loop .c
.ext:
    mov word [di], '.D'
    mov word [di + 2], 'LL'
    mov byte [di + 4], 0
    ICALL w16_libfind               ; AL = the record, CF=1 none loaded
    jc .load
    clc
    jmp short .out
.load:
    call w16l_ldlib
.out:
    pop di
    pop si
    pop cx
    ret

; w16l_ldlib - [w16_lname]'s library, loaded (SPEC.md 124.23). The folder
; is current and the cluster cache claimed. out: CF=0 AL = its record, or
; 0FFh no such file; CF=1 refused with the lines written (line 2 the
; library's file). Clobbers AX; keeps the rest.
w16l_ldlib:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [w16_ldep], W16_LDEPTH
    jb .deep
    mov si, w16_r_ldeep
    jmp .refuse0
.deep:
    call w16l_push                  ; the importer's state, aside
    mov byte [w16_cdo], 0           ; its code discardable but where the
%ifndef PKG_MEGA                    ;   census loads it all anyway: an 8088
%ifdef W16_CENSUS_ALWAYS            ;   (SPEC.md 124.26.11)
    jmp short .cdo
%else
    cmp byte [w16_cpu186], 0
    je .cdo
%endif
%endif
    inc byte [w16_cdo]
.cdo:
    mov si, w16_lname
    mov di, w16_name
    ICALL w16_strcpy
    mov word [w16_fseg], 0
    mov word [w16l_code], 0
    mov word [w16_dgroup], 0
    push ds
    pop es
    mov di, w16_hdr                 ; MZ, then NE (w16_load's own checks)
    xor ax, ax
    xor dx, dx
    mov cx, 0x40
    call w16l_fread
    jnc .mz
    call w16l_pop                   ; no such file: its imports bind missing
    mov al, 0xFF
    clc
    jmp .ret
.mz:
    cmp word [w16_hdr], 0x5A4D
    jne .notwin
    cmp word [w16_hdr + 0x18], 0x40
    jb .notwin
    mov ax, [w16_hdr + 0x3C]
    mov dx, [w16_hdr + 0x3E]
    mov [w16_soff], ax
    mov [w16_soff + 2], dx
    mov di, w16_hdr
    mov cx, 0x40
    call w16l_fread
    jc .notwin
    cmp word [w16_hdr], 0x454E
    jne .notwin
    call w16l_tabend
    jnc .tabs
    mov si, w16_r_tabs
    jmp .refuse
.notwin:
    mov si, w16_r_notlib
    jmp .refuse
.tabs:
    mov [w16_fend], ax
    mov [w16_hpos], ax
    inc word [w16_hpos]
    and word [w16_hpos], 0xFFFE
    call w16l_hclaim                ; the tables, to the paragraph
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
.tread:
    xor si, si
    mov [w16_ne], si
    mov ax, [es:si + NE_FLAGS]
    mov di, w16_r_notlib
    test ax, NEF_LIB
    jz .flagref
    mov di, w16_r_self
    test ax, NEF_SELF
    jnz .flagref
    mov di, w16_r_linkerr
    test ax, NEF_LINKERR
    jnz .flagref
%ifndef PKG_MEGA                   ; the arm runs one (SPEC.md 124.31.4)
    mov di, w16_r_pmode
    test ax, NEF_PMODE
    jnz .flagref
%endif
    mov al, [es:si + NE_EXETYP]
    cmp al, 2
    je .exeok
    or al, al
    jz .exeok
    mov di, w16_r_os2
.flagref:
    mov si, di
    jmp .refuse
.exeok:
    mov ax, [es:si + NE_CSEG]       ; a library may have no segment at all
    cmp ax, W16_MAXSEG              ;   (a resource library's)
    jbe .segok
    mov si, w16_r_segs
    jmp .refuse
.segok:
    mov [w16_nseg], ax
    mov ax, [es:si + NE_AUTODATA]   ; ...or no DGROUP
    cmp ax, [w16_nseg]
    jbe .dataok
    mov si, w16_r_nodata
    jmp .refuse
.dataok:
    mov ax, [es:si + NE_CMOD]
    cmp ax, 16
    jbe .modok
    mov si, w16_r_mods
    jmp .refuse
.modok:
    mov [w16_nmod], ax
    call w16_modules
    mov es, [w16_fseg]
    call w16l_laylib                ; CF=1 SI = reason
    jc .refuse
    call w16_copysegs
    jc .refuse
    call w16l_cmake                 ; its module block, if it has discardable
    jc .refuse                      ;   code (SPEC.md 124.26.11)
    call w16l_mrec                  ; registered BEFORE its libraries load, so
    jc .refuse                      ;   one that imports it back finds it, as
    mov [w16l_lrec], al             ;   Windows' module table did (124.23.2)
    call w16l_stash                 ; its segment tables, aside meanwhile
    jc .refmem
    call w16l_libs                  ; ITS libraries
    jc .refusedr
    call w16l_unstash
    call w16l_relibs                ; ...named in its module block now
    mov es, [w16_fseg]
    mov word [w16_nfix], 0
    xor al, al
    call w16_relocs
    jc .refusedr
    cmp word [w16_nfix], 0          ; floating point: decided once, by the
    je .nofp                        ;   first module that has any (124.5)
    ICALL w16x_mode
    jc .refuser
.nofp:
    call w16l_segsup                ; its discardable code loaded at launch
    jc .refusedr                    ;   (124.26.3), CF=1 lines written
    mov al, 1
    call w16_relocs
    jc .refusedr
    call w16_prologs
%ifndef PKG_MEGA
%ifndef W16_CENSUS_ALWAYS
    cmp byte [w16_cpu186], 0
    jne .cpuok
%endif
    call w16_census
    jc .refusedr
.cpuok:
%endif
    mov al, [w16l_lrec]
    push ax
    mov dx, [w16_fseg]
    call OSAPI_MEM_FREE
    call w16l_pop
    pop ax
    clc
    jmp short .ret
.refuse0:
    mov di, w16_l1                  ; (nothing pushed: the importer's name)
    ICALL w16_strcpy
    mov si, w16_lname
    mov di, w16_l2
    ICALL w16_strcpy
    stc
    jmp short .ret
.refmem:
    mov si, w16_r_mem
.refuser:
    call w16l_unreg                 ; registered: its record and block go too
.refuse:
    mov di, w16_l1
    ICALL w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    ICALL w16_strcpy
    jmp short .refused
.refusedr:
    call w16l_unreg
.refused:
    call w16l_undo                  ; its blocks and its tables back
    call w16l_pop
    stc
.ret:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16l_rtlib - [w16_lname]'s library loaded while the program runs
; (LoadLibrary, SPEC.md 124.23.5), by w16_ldlibin's w16_ldcall: the program's
; folder made current and the cluster cache claimed for it, the user's folder
; put back after. out: as w16l_ldlib's
w16l_rtlib:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov word [cs:w16l_rowslot + 2 * W16_XDDP], W16_DDPROW ; XSEC-OK: (a fresh part)
    call OSAPI_FILE_HERE
    push dx
    push bx
    mov dx, [w16_dir]
    mov bl, [w16_vol]
    call OSAPI_FILE_GOTO_QM
    jc .fail
    mov ax, [w16_csz]
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [w16_cseg], dx
    mov word [w16_clen], 0
    call w16l_ldlib
    pushf
    jc .nokeep
    push si                         ; its discardable code loads through
    call w16l_keep                  ;   the kept part (124.26.11); without
    pop si                          ;   it, each load fetches the part
.nokeep:
    call w16l_cfree
    popf
    jmp short .back
.fail:
    stc
.back:
    pop bx
    pop dx
    pushf
    push ax
    call OSAPI_FILE_GOTO_QM
    pop ax
    popf
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16l_undo - a library refused part way: its tables, code and DGROUP given
; back, and its module block. Keeps all.
w16l_undo:
    push ax
    push dx
    call w16l_cfreeall              ; its module block and loaded code
    xor dx, dx
    xchg dx, [w16_fseg]
    or dx, dx
    jz .t
    call OSAPI_MEM_FREE
.t:
    xor ax, ax
    xchg ax, [w16l_code]
    or ax, ax
    jz .c
    ICALL w16_gfreeh
.c:
    xor ax, ax
    xchg ax, [w16_dgroup]
    or ax, ax
    jz .d
%ifdef PKG_MEGA
    ICALL w16_pin                   ; (a selector by now, or still WIN16's)
%endif
    ICALL w16_gfreeh
.d:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16l_laylib - a library's segments into the global heap (SPEC.md 124.23.2):
; DGROUP a block of its own (minalloc + stack + heap), every other segment
; one block, each at a paragraph of its own. in: ES = the tables
; out: [w16_sbase], [w16_slen], [w16_dgroup] (0 = none), [w16l_code];
; CF=1 SI = reason
; -----------------------------------------------------------------------------
w16l_laylib:
    push bp
    mov si, [w16_ne]
    mov di, si
    add di, [es:si + NE_SEGTAB]
    xor bp, bp                      ; BP = the code block's paragraphs
    xor bx, bx
    mov word [w16l_dgb], 0
    mov word [w16l_dgb + 2], 0
.seg:
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jae .sized
    test word [es:di + 4], NSF_ITER
    jz .noiter
    mov si, w16_r_iter
    jmp .fail
.noiter:
    xor cx, cx                      ; CX:AX = max(length, minalloc), w16_lay1's
    mov dx, [es:di + 6]
    or dx, dx
    jnz .minset
    inc cx
.minset:
    mov ax, dx
    cmp word [es:di], 0
    je .one
    cmp word [es:di + 2], 0
    jne .len16
    xor ax, ax
    mov cx, 1
    jmp short .one
.len16:
    or cx, cx
    jnz .one
    cmp [es:di + 2], ax
    jbe .one
    mov ax, [es:di + 2]
.one:
    mov si, [w16_ne]
    mov dx, bx
    shr dx, 1
    inc dx
    cmp dx, [es:si + NE_AUTODATA]
    jne .code
    add ax, [es:si + NE_STACK]      ; DGROUP: its stack (0 in a library) and
    adc cx, 0                       ;   its local heap
    cmp word [es:si + NE_HEAP], 0
    je .noheap
    add ax, [es:si + NE_HEAP]
    adc cx, 0
    add ax, W16_LHROOM              ; ...and the room Windows let it grow
    adc cx, 0                       ;   into (SPEC.md 124.25.2)
.noheap:
    cmp cx, 1
    ja .dgbig
    jb .dgok
    or ax, ax
    jz .dgok
.dgbig:
    mov si, w16_r_dgbig
    jmp .fail
.dgok:
    mov [w16_slen + bx], ax
    mov [w16l_dgb], ax
    mov [w16l_dgb + 2], cx
    mov word [w16_sbase + bx], 0xFFFF   ; placed below
    jmp short .nextseg
.code:
    mov [w16_slen + bx], ax
    call w16l_isdisc                ; discardable: no room, loaded as the
    jc .nextseg                     ;   program's are (SPEC.md 124.26.11)
    mov [w16_sbase + bx], bp        ; relative, until the block is made
    add ax, 15
    adc cx, 0
    shr cx, 1
    rcr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add bp, ax
    jc .mem
.nextseg:
    add di, 8
    inc bx
    inc bx
    jmp .seg
.sized:
    or bp, bp                       ; the code block
    jz .nocode
    cmp bp, 0xFE00
    jae .mem
    mov ax, bp
    xor dx, dx
    mov cx, 4
.p2b:
    shl ax, 1
    rcl dx, 1
    loop .p2b
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew                  ; AX = the handle, DX = the segment
    or ax, ax
    jz .mem
    mov [w16l_code], dx
.nocode:
    mov ax, [w16l_dgb]
    mov dx, [w16l_dgb + 2]
    mov cx, ax
    or cx, dx
    jz .nodg
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew
    or ax, ax
    jz .mem
    mov [w16_dgroup], dx
    mov ax, [w16l_dgb]
    mov [w16_dgsize], ax
.nodg:
    xor bx, bx                      ; absolute paragraphs
.abs:
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jae .done
    mov ax, [w16_dgroup]
    cmp word [w16_sbase + bx], 0xFFFF
    je .put
    xor ax, ax                      ; (a discardable one: 0, not loaded)
    call w16l_isdisc
    jc .put
    mov ax, [w16_sbase + bx]
    add ax, [w16l_code]
.put:
    mov [w16_sbase + bx], ax
    inc bx
    inc bx
    jmp short .abs
.done:
%ifdef PKG_MEGA
    mov ax, [w16_dgroup]            ; a library's DGROUP: its block's selector,
    or ax, ax                       ;   as GlobalLock answers it (124.31.3)
    jz .nodsel
    ICALL w16_pblk
    mov [w16_dgroup], ax
.nodsel:
%endif
    pop bp
    clc
    ret
.mem:
    mov si, w16_r_mem
.fail:
    pop bp
    stc
    ret

; -----------------------------------------------------------------------------
; w16l_mrec - the library loaded: its module block made and its record
; written (SPEC.md 124.23.2). in: ES = the tables. out: CF=0 AL = the
; record; CF=1 SI = reason. Keeps the rest but ES.
; -----------------------------------------------------------------------------
w16l_mrec:
    push bx
    push cx
    push dx
    push di
    push bp
    mov si, w16_libs                ; a free record
    xor bp, bp
.free:
    cmp byte [si + W16L_COUNT], 0
    je .rec
    add si, W16L_SIZE
    inc bp
    cmp bp, W16_NLIB
    jb .free
    mov si, w16_r_nlib
    jmp .fail
.rec:
    mov [w16l_rp], si
    push es                         ; the record, cleared before it is filled
    push ds
    pop es
    mov di, si
    mov cx, W16L_SIZE / 2
    xor ax, ax
    cld
    rep stosw
    pop es
    mov bx, [w16_ne]                ; what the block copies, measured first
    mov ax, [es:bx + NE_ENTTAB]
    add ax, bx
    mov [w16l_entp], ax
    mov ax, [es:bx + NE_ENTLEN]
    mov [w16l_entn], ax
    mov ax, [es:bx + NE_NRESN]
    mov [w16l_nresn], ax
    mov ax, [es:bx + NE_IP]
    mov [w16l_ip], ax
    mov ax, [es:bx + NE_CS]
    mov [w16l_cs], ax
    mov di, bx                      ; the resident names, walked to their 0
    add di, [es:bx + NE_RESTAB]
    mov [w16l_resp], di
.rw:
    cmp di, [w16_fend]
    jae .rwe
    mov al, [es:di]
    or al, al
    jz .rwe
    xor ah, ah
    add di, ax
    add di, 3
    jmp short .rw
.rwe:
    inc di                          ; its 0, kept
    sub di, [w16l_resp]
    mov [w16l_resn], di
    ICALL w16_rlayout               ; its resource table's bytes (124.23.6):
    cmp word [w16_rtlen], 0x1000    ;   a block's GH_OWNER names an entry in
    jbe .rtok                       ;   12 bits, so a larger one is not kept
    mov word [w16_rtlen], 0
.rtok:
    mov ax, [w16_nseg]              ; header, segments, entries, names
    shl ax, 1
    add ax, W16LM_SEGS
    add ax, [w16l_entn]
    jc .mem
    add ax, di
    jc .mem
    add ax, [w16l_nresn]
    jc .mem
    add ax, 15                      ; ...then the resource table, on a
    jc .mem                         ;   paragraph of its own
    and al, 0xF0
    push ax
    add ax, [w16_rtlen]
    pop cx
    jc .mem
    xor dx, dx
    push cx
    mov cl, GMEM_ZEROINIT
    ICALL w16_gnew
    pop cx
    or ax, ax
    jz .mem
    mov si, [w16l_rp]
    mov [si + W16L_MREC], dx
    W16SEG es, dx                      ; ES = the block from here
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [es:W16LM_RTAB], cx
    mov ax, [w16_rtlen]
    mov [es:W16LM_RTLEN], ax
    mov ax, [w16_nseg]
    mov [es:W16LM_NSEG], ax
    mov ax, [w16l_ip]
    mov [es:W16LM_IP], ax
    mov ax, [w16l_cs]
    mov [es:W16LM_CS], ax
    call w16l_mlibs                 ; (none yet: w16l_relibs, after them)
    push bx                         ; its discardable code (124.26.11): the
    push dx                         ;   six words, where its file is, and
    mov si, w16_cmb                 ;   its thunks' code segment
    mov di, W16LM_CD
    mov cx, 6
    cld
    rep movsw
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the volume
    mov [es:W16LM_DIR], dx
    mov [es:W16LM_VOL], bl
    xor dx, dx
    cmp word [w16_cmb], 0
    je .notcs
    mov dx, [w16_cthk]
%ifdef PKG_MEGA
    call w16l_thkcs
%endif
.notcs:
    mov [es:W16LM_TCS], dx
    pop dx
    pop bx
    mov di, W16LM_SEGS              ; each segment's paragraph
    mov si, w16_sbase
    mov cx, [w16_nseg]
    cld
%ifdef PKG_MEGA
    push bx                         ; ...as the program names it: DGROUP's
    mov bx, [w16_dgroup]            ;   selector, a code segment's code half
    mov ax, bx                      ;   (SPEC.md 124.31.4)
    or ax, ax
    jz .sgo
    ICALL w16_pvirt
.sgo:
    xchg ax, bx                     ; AX = the selector, BX = its paragraph
.sone:
    jcxz .sdone
    cmp [si], bx
    je .sput
    push ax
    push cx
    mov ax, [si]
    or ax, ax                       ; (a discardable one: 0, 124.26.11)
    jz .szero
    mov cl, 1
    ICALL w16_ssel
.szero:
    stosw
    pop cx
    pop ax
    jmp short .snext
.sput:
    stosw
.snext:
    inc si
    inc si
    dec cx
    jmp short .sone
.sdone:
    pop bx
%else
    rep movsw
%endif
    mov [es:W16LM_ENT], di          ; the entry table
    mov ax, [w16l_entn]
    mov [es:W16LM_ENTN], ax
    mov si, [w16l_entp]
    mov cx, ax
    call w16l_tcopy
    mov [es:W16LM_RES], di          ; the resident names
    mov si, [w16l_resp]
    mov cx, [w16l_resn]
    call w16l_tcopy
    mov cx, [w16l_nresn]            ; the non-resident ones, out of the file
    mov [es:W16LM_NRESN], cx
    mov [es:W16LM_NRES], di
    jcxz .rdone
    push es
    mov es, [w16_fseg]
    mov bx, [w16_ne]
    mov ax, [es:bx + NE_NRESOFF]
    mov dx, [es:bx + NE_NRESOFF + 2]
    pop es
    call w16l_fread
    jnc .rdone
    mov word [es:W16LM_NRESN], 0    ; unreadable: by ordinal still works
.rdone:
    cmp word [es:W16LM_RTLEN], 0    ; the resource table, its cache words
    je .nort                        ;   cleared (w16_copyres): [w16_rtab] is
    mov si, [w16l_rp]               ;   the program's, saved by w16l_push
    mov ax, [es:W16LM_RTAB]
    add ax, [si + W16L_MREC]
    mov [w16_rtab], ax
    push es
    mov es, [w16_fseg]
    ICALL w16_copyres
    pop es
    jnc .nort
    mov word [es:W16LM_RTLEN], 0    ; past the file: no resources
.nort:
    mov si, [w16l_rp]               ; the record
    mov es, [w16_fseg]              ; its name: the resident names' first
    mov bx, [w16l_resp]
    mov cl, [es:bx]
    xor ch, ch
    cmp cx, 8
    jbe .nm
    mov cx, 8
.nm:
    jcxz .named
    mov di, si
.nc:
    inc bx
    mov al, [es:bx]
    mov [di + W16L_NAME], al
    inc di
    loop .nc
.named:
    push si
    lea di, [si + W16L_FILE]
    mov si, w16_name
    ICALL w16_strcpy
    pop si
    mov byte [si + W16L_COUNT], 1
    mov ax, [w16_dgroup]
    or ax, ax
    jnz .hi
    mov ax, [si + W16L_MREC]        ; no DGROUP: the module block is its handle
%ifdef PKG_MEGA
    ICALL w16_pblk                  ; (the program's: a selector, 124.31.3)
%endif
.hi:
    mov [si + W16L_HINST], ax
    mov ax, [w16l_code]
    mov [si + W16L_CODE], ax
    mov bx, [w16_ne]
    mov ax, [es:bx + NE_HEAP]
    mov [si + W16L_HEAP], ax
    cmp word [w16l_cs], 0
    je .noinit
    mov byte [si + W16L_OWED], 1
.noinit:
    mov ax, bp                      ; AL = the record
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    clc
    ret
.mem:
    mov si, w16_r_mem
.fail:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret

; w16l_mlibs - the libraries a library imports, by record, into its module
; block at ES (0FFh: none, or not a library). Keeps all.
w16l_mlibs:
    push ax
    push bx
    xor bx, bx
.lib:
    mov al, 0xFF
    cmp bx, [w16_nmod]
    jae .lput
    mov al, [w16_modid + bx]
    test al, W16M_LIB
    jz .lnone
    and al, ~W16M_LIB & 0xFF
    jmp short .lput
.lnone:
    mov al, 0xFF
.lput:
    mov [es:W16LM_LIBS + bx], al
    inc bx
    cmp bx, 16
    jb .lib
    pop bx
    pop ax
    ret

; w16l_lrecp - SI = the record of the library being loaded ([w16l_lrec]).
; Keeps the rest.
w16l_lrecp:
    push ax
    mov al, [w16l_lrec]
    mov ah, W16L_SIZE
    mul ah
    add ax, w16_libs
    mov si, ax
    pop ax
    ret

; w16l_relibs - its libraries loaded: their records into its module block.
; Keeps all.
w16l_relibs:
    push si
    push es
    call w16l_lrecp
    W16SEG es, [si + W16L_MREC]
    call w16l_mlibs
    pop es
    pop si
    ret

; w16l_unreg - a library refused after w16l_mrec registered it: its record
; free again and its module block given back. One that imported it back
; while it loaded keeps a count on nothing; the load that asked fails.
; Keeps all.
w16l_unreg:
    push ax
    push si
    call w16l_lrecp
    mov byte [si + W16L_COUNT], 0
    xor ax, ax
    xchg ax, [si + W16L_MREC]
    or ax, ax
    jz .out
    ICALL w16_gfreeh
.out:
    pop si
    pop ax
    ret

; w16l_stash / w16l_unstash - its segment tables ([w16_sbase], [w16_slen],
; [w16_rtoff], [w16_nseg] of each) aside at the end of its own tables while
; its libraries load - each library's load lays its own out in them - and
; back. w16l_stash: CF=1 no memory; ES = the tables. Keep the rest.
w16l_stash:
    push ax
    push cx
    push si
    push di
    mov cx, [w16_nseg]
    shl cx, 1
    mov ax, [w16_hpos]
    mov [w16l_stoff], ax
    add ax, cx
    jc .out
    add ax, cx
    jc .out
    add ax, cx
    jc .out
    call w16l_hgrow
    jc .out
    mov [w16_hpos], ax
    mov di, [w16l_stoff]
    cld
    mov si, w16_sbase
    push cx
    rep movsb
    pop cx
    mov si, w16_slen
    push cx
    rep movsb
    pop cx
    mov si, w16_rtoff
    rep movsb
    clc
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

w16l_unstash:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    mov cx, [w16_nseg]
    shl cx, 1
    mov si, [w16l_stoff]
    mov ax, [w16_fseg]
    push ds
    pop es
    mov ds, ax
    cld
    mov di, w16_sbase
    push cx
    rep movsb
    pop cx
    mov di, w16_slen
    push cx
    rep movsb
    pop cx
    mov di, w16_rtoff
    rep movsb
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16l_tcopy - CX bytes from the tables at SI to ES:DI, DI past them.
; Keeps the rest but SI.
w16l_tcopy:
    jcxz .out
    push ds
    mov ds, [w16_fseg]
    cld
    rep movsb
    pop ds
.out:
    ret

; -----------------------------------------------------------------------------
; w16l_push / w16l_pop - one module's loader state, aside while a library it
; imports loads, and back (SPEC.md 124.23.1). The cluster cache is shared and
; forgotten at each. Keep every register.
; -----------------------------------------------------------------------------
w16l_push:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov al, [w16_ldep]
    inc byte [w16_ldep]
    mov ah, W16_LSVSZ
    mul ah
    add ax, w16_lsave
    mov di, ax
    mov bx, w16l_svtab              ; XSEC-OK: read through cs:
.item:
    mov si, [cs:bx]
    or si, si
    jz .done
    mov cx, [cs:bx + 2]
    cld
    rep movsb
    add bx, 4
    jmp short .item
.done:
    mov word [w16_clen], 0
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

w16l_pop:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    dec byte [w16_ldep]
    mov al, [w16_ldep]
    mov ah, W16_LSVSZ
    mul ah
    add ax, w16_lsave
    mov si, ax
    mov bx, w16l_svtab              ; XSEC-OK: read through cs:
.item:
    mov di, [cs:bx]
    or di, di
    jz .done
    mov cx, [cs:bx + 2]
    cld
    rep movsb
    add bx, 4
    jmp short .item
.done:
    mov word [w16_clen], 0
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

w16l_svtab:                         ; what one module's load holds, address
    dw w16_fseg, 2,  w16_hpos, 2,  w16_htp, 2,  w16_ne, 2,  w16_nseg, 2
    dw w16_nmod, 2,  w16_modid, 32,  w16_fend, 2,  w16_name, 14
    dw w16_dgroup, 2,  w16_dgsize, 2,  w16_lbase, 2,  w16_pseg, 2
    dw w16_psp, 2,  w16_ptop, 2,  w16_dgfull, 1,  w16_rtab, 2
    dw w16_rtlen, 2,  w16_heap, 2,  w16_nfix, 2,  w16_soff, 4
    dw w16l_code, 2,  w16l_lrec, 1,  w16l_stoff, 2
    dw w16_cmb, 13                  ; its module block's six words and w16_cdo
    dw 0                            ;   (SPEC.md 124.26.11)
%assign w16l_svn 2+2+2+2+2 + 2+32+2+14 + 2+2+2+2 + 2+2+1+2 + 2+2+2+4 + 2+1+2 + 13
%if w16_cdo - w16_cmb != 12
  %error "w16l_svtab saves w16_cmb..w16_cdo as one range"
%endif
%if w16l_svn > W16_LSVSZ
  %error "w16l_svtab holds more than W16_LSVSZ"
%endif

; =============================================================================
; DISCARDABLE CODE (SPEC.md 124.26). A program's discardable code segments are
; blocks of the global heap, reached through one thunk an entry, loaded at
; launch or when a thunk is first hit, and discarded when memory is short
; unless a word of the program's live stack names them. What a later load
; needs is in the module block: the NE tables, room for one segment's
; relocation records, the live table and the thunks.
; =============================================================================

; w16l_isdisc - BX = a segment's index * 2: CF=1 a discardable code segment
; of the module being loaded (SPEC.md 124.26.1, 124.26.11). ES = the tables.
; Keeps all.
w16l_isdisc:
    cmp byte [w16_cdo], 0           ; not counted: every one is fixed
    je .no
    push ax
    push si
    mov si, [w16_ne]
    test word [es:si + NE_FLAGS], NEF_LIB
    jz .prog
    mov ax, bx                      ; a library's entry point's is fixed
    shr ax, 1                       ;   (SPEC.md 124.26.11)
    inc ax
    cmp ax, [es:si + NE_CS]
    je .not
.prog:
    mov ax, [es:si + NE_SEGTAB]
    add si, ax
    add si, bx
    add si, bx
    add si, bx
    add si, bx                      ; + index * 8
    mov ax, [es:si + 4]
    test ax, NSF_DATA | NSF_ITER
    jnz .not
    test ax, NSF_DISCARD
    jz .not
    cmp word [es:si], 0             ; bytes in the file
    je .not
    cmp word [es:si + 2], 0         ; under 64 KB, both
    je .not
    cmp word [es:si + 6], 0
    je .not
    pop si
    pop ax
    stc
    ret
.not:
    pop si
    pop ax
.no:
    clc
    ret

; w16l_lent - DX = a segment's number: ES:SI -> its live entry; CF=1 none (no
; module block, or out of range). Keeps the rest.
w16l_lent:
    cmp word [w16_cmb], 0
    je .none
    or dx, dx
    jz .none
    cmp dx, [w16_cnseg]
    ja .none
    mov si, dx
    dec si
    shl si, 1
    add si, dx
    dec si                          ; (DX - 1) * 3 ...
    shl si, 1                       ; ...* 2 = * W16CL_SIZE
    W16SEG es, [w16_clive]
    clc
    ret
.none:
    stc
    ret

; w16l_isdlive - DX = a segment's number: CF=1 a discardable one, by the live
; table. Keeps all.
w16l_isdlive:
    push si
    push es
    call w16l_lent
    jc .no
    test byte [es:si + W16CL_FL], W16CLF_DISC
    jz .no
    pop es
    pop si
    stc
    ret
.no:
    pop es
    pop si
    clc
    ret

; w16l_tothk - in w16_reloc1: DX = the segment a moveable entry is in. One
; that is discardable, while the program's relocations are applied, is reached
; through its thunk (SPEC.md 124.26.2): CF=1 and DX:AX = the thunk. ES:SI ->
; the record. Keeps the rest.
w16l_tothk:
    cmp byte [w16_cdo], 0
    je .no
    cmp byte [w16_rpass], 1
    jne .no
    call w16l_isdlive
    jnc .no
    mov ax, [es:si + 6]             ; the ordinal: thunk (ordinal - 1) * 8
    dec ax
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov dx, [w16_cthk]
%ifdef PKG_MEGA
    call w16l_thkcs                 ; the thunks' code half (124.31.4)
%endif
    stc
    ret
.no:
    clc
    ret

; w16l_keepck - in w16_reloc1's check pass: DX = a segment named by its NUMBER
; from segment [w16_rseg]. A discardable one named so from another segment is
; kept: loaded at launch and never discarded (SPEC.md 124.26.1). A library's
; is kept when it names itself, too: that number escapes as a callback, a
; dialog procedure handed to CreateDialog (124.26.11). Keeps all.
w16l_keepck:
    cmp byte [w16_cdo], 0
    je .out
    cmp byte [w16_rpass], 0
    jne .out
    call w16l_isdlive
    jnc .out
    push ax
    mov ax, [w16_rseg]
    shr ax, 1
    inc ax
    cmp ax, dx
    pop ax
    jne .keep
    push si                         ; itself: a program's is applied again
    mov si, [w16_ne]                ;   at each load, a library's kept
    test word [es:si + NE_FLAGS], NEF_LIB
    pop si
    jz .out
.keep:
    push si
    push es
    call w16l_lent
    or byte [es:si + W16CL_FL], W16CLF_KEEP
    pop es
    pop si
.out:
    ret

; w16l_nord - ES = the tables: AX = how many ordinals the entry table has.
; Keeps the rest.
w16l_nord:
    push cx
    push si
    mov si, [w16_ne]
    add si, [es:si + NE_ENTTAB]
    xor ax, ax
.bundle:
    xor cx, cx
    mov cl, [es:si]
    jcxz .done
    add ax, cx
    cmp byte [es:si + 1], 0         ; skipped ordinals: no entries
    je .next
    cmp byte [es:si + 1], 0xFF
    jne .three
    shl cx, 1                       ; moveable: 6 bytes an entry
.three:
    add si, cx                      ; fixed and constant: 3
    add si, cx
    add si, cx
.next:
    add si, 2
    jmp short .bundle
.done:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16l_cmake - the module block (SPEC.md 124.26.4), when the program or a
; library (124.26.11) has a discardable segment: the tables copied, the live
; table and the thunks written, its six words in w16_cmb..w16_cnord (0 none). in: ES = the tables, every segment's records read (w16_copysegs);
; out: CF=1 SI = reason. Keeps ES.
; -----------------------------------------------------------------------------
w16l_cmake:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    push es
    push ds
    pop es
    mov di, w16_cmb                 ; the six words, none yet
    mov cx, 6
    xor ax, ax
    cld
    rep stosw
    pop es
    mov di, [w16_ne]                ; a library's load leaves the program's
    test word [es:di + NE_FLAGS], NEF_LIB ; budget alone (124.26.11)
    jnz .nobud
    mov word [w16_cused], 0
%ifdef W16_CODEKB
    mov word [w16_cbud], W16_CODEKB * 64    ; a test's budget (124.26.6)
%else
    mov word [w16_cbud], 0xFFFF
%endif
.nobud:
    xor bx, bx                      ; BX = index * 2
    xor bp, bp                      ; BP = the most records a discardable
    xor dx, dx                      ;   segment has; DX = how many there are
.count:
    call w16l_isdisc
    jnc .cnext
    inc dx
    mov si, [w16_rtoff + bx]
    or si, si
    jz .cnext
    mov ax, [es:si]
    cmp ax, bp
    jbe .cnext
    mov bp, ax
.cnext:
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .count
    or dx, dx
    jnz .some
    jmp .ok                         ; nothing discardable: no block
.some:
    call w16l_nord
    mov [w16_cnord], ax
    mov ax, [w16_nseg]
    mov [w16_cnseg], ax
    mov ax, [w16_fend]              ; the tables, word-aligned, then the room
    inc ax
    and al, 0xFE
    mov [w16_crel], ax
    mov cx, bp
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add ax, cx
    jc .big
    add ax, 2 + 15                  ; its count word; the live table on a
    jc .big                         ;   paragraph
    and al, 0xF0
    mov bx, ax                      ; BX = the live table's offset
    mov cx, [w16_nseg]
    mov dx, cx
    shl cx, 1
    add cx, dx
    shl cx, 1                       ; * W16CL_SIZE
    add ax, cx
    add ax, 15
    jc .big
    and al, 0xF0
    mov di, ax                      ; DI = the thunks'
    mov cx, [w16_cnord]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add ax, cx
    jc .big
    call w16l_b2p                   ; PARAGRAPHS (SPEC.md 50.3.7)
    ICALL w16_kclaimp               ; DX = the block
    jnc .have
    mov si, w16_r_mem
    jmp short .fail
.big:
    mov si, w16_r_tabs
.fail:
    stc
    jmp short .out
.have:
    mov [w16_cmb], dx
    mov cl, 4
    mov ax, bx
    shr ax, cl
    add ax, dx
    mov [w16_clive], ax
    mov ax, di
    shr ax, cl
    add ax, dx
    mov [w16_cthk], ax
    push es                         ; the live table and the thunks, zeroed
    W16SEG es, dx
    mov cx, [w16_cnord]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, di
    sub cx, bx
    mov di, bx
    xor al, al
    cld
    rep stosb
    pop es
    mov cx, [w16_fend]              ; the tables, as they were read
    push ds
    push es
    push es
%ifdef PKG_MEGA
    W16SEG es, dx                   ; (DS still ours for the thunk)
%endif
    pop ds
%ifndef PKG_MEGA
    mov es, dx
%endif
    xor si, si
    xor di, di
    rep movsb
    pop es
    pop ds
    call w16l_lfill
    call w16l_tinit
.ok:
    clc
.out:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16l_lfill - the live table from the layout: each segment's paragraph in
; the claim (0 for a discardable one) and whether it is discardable. ES = the
; tables. Keeps all.
w16l_lfill:
    push ax
    push bx
    push dx
    push di
    xor bx, bx
    xor di, di
.seg:
    mov ax, [w16_sbase + bx]
    xor dl, dl
    call w16l_isdisc
    jnc .flags
    mov dl, W16CLF_DISC
.flags:
    push es
    W16SEG es, [w16_clive]
    mov [es:di + W16CL_BASE], ax
    mov [es:di + W16CL_FL], dl
    pop es
    add di, W16CL_SIZE
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .seg
    pop di
    pop dx
    pop bx
    pop ax
    ret

; w16l_tinit - a thunk for every entry in a discardable segment, pointing at
; w16_thkhit (SPEC.md 124.26.2): a moveable one's, and a FIXED bundle's too,
; whose indicator is the segment's number - MSCUISTF.DLL exports all 22 of
; its entries so, from discardable segments (124.26.11). ES = the tables.
; Keeps all.
w16l_tinit:
    push ax
    push cx
    push dx
    push si
    push di
    mov si, [w16_ne]
    add si, [es:si + NE_ENTTAB]
    xor di, di                      ; DI = the bundle's first thunk
.bundle:
    xor cx, cx
    mov cl, [es:si]
    jcxz .done
    mov dl, [es:si + 1]
    add si, 2
    cmp dl, 0xFF
    je .entry
    or dl, dl                       ; skipped ordinals: no entries
    jnz .fixed
    mov ax, cx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add di, ax
    jmp short .bundle
.fixed:                             ; a fixed bundle: flags, offset, its
    cmp dl, 0xFE                    ;   segment the indicator (a constant's
    je .fnext                       ;   0FEh names none)
    xor dh, dh
    call w16l_isdlive
    jnc .fnext
    mov ax, [es:si + 1]             ; its offset
    call w16l_thk1
.fnext:
    add si, 3
    add di, W16TK_SIZE
    loop .fixed
    jmp short .bundle
.entry:
    mov dl, [es:si + 3]             ; its segment
    xor dh, dh
    call w16l_isdlive
    jnc .next
    mov ax, [es:si + 4]             ; its offset
    call w16l_thk1
.next:
    add si, 6
    add di, W16TK_SIZE
    loop .entry
    jmp short .bundle
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16l_thk1 - thunk DI of the module block: segment DL's entry at offset AX,
; calling w16_thkhit until the segment is loaded. Keeps all.
w16l_thk1:
    push es
    W16SEG es, [w16_cthk]
    mov byte [es:di], 0x9A          ; call far w16_thkhit
    mov word [es:di + 1], w16_thkhit
%ifdef PKG_MEGA
    push ax
    W16ICS ax
    mov [es:di + 3], ax             ; (the image's code half)
    pop ax
%else
    mov [es:di + 3], ds             ; (the image's segment)
%endif
    mov [es:di + W16TK_SEG], dl
    mov [es:di + W16TK_OFF], ax
    pop es
    ret

; w16l_thkset - AX = a segment's number, DX = its paragraph, or 0: each of its
; thunks jumps to it, or calls w16_thkhit (SPEC.md 124.26.2). Keeps all.
w16l_thkset:
    push cx
    push di
    push es
%ifdef PKG_MEGA
    or dx, dx                       ; the segment's code selector, which each
    jz .sel                         ;   thunk's jmp far names (124.31.4)
    push ax
    mov ax, dx
    mov cl, 1
    ICALL w16_ssel
    mov dx, ax
    pop ax
.sel:
%endif
    W16SEG es, [w16_cthk]
    xor di, di
    mov cx, [w16_cnord]
    jcxz .out
.thunk:
    cmp byte [es:di], 0             ; no thunk at this ordinal
    je .next
    cmp [es:di + W16TK_SEG], al
    jne .next
    or dx, dx
    jz .gone
    mov byte [es:di], 0xEA          ; jmp far the entry
    push word [es:di + W16TK_OFF]
    pop word [es:di + 1]
    mov [es:di + 3], dx
    jmp short .next
.gone:
    mov byte [es:di], 0x9A          ; call far w16_thkhit
    mov word [es:di + 1], w16_thkhit
%ifdef PKG_MEGA
    push ax
    W16ICS ax
    mov [es:di + 3], ax
    pop ax
%else
    mov [es:di + 3], ds
%endif
.next:
    add di, W16TK_SIZE
    loop .thunk
.out:
    pop es
    pop di
    pop cx
    ret

; w16l_segtab - BX = a segment's index * 2: ES:DI -> its row of the segment
; table, and AX = its size, the larger of its length and minalloc (both under
; 64 KB for a discardable one). ES = the tables. Keeps the rest.
w16l_segtab:
    mov di, [w16_ne]
    add di, [es:di + NE_SEGTAB]
    add di, bx
    add di, bx
    add di, bx
    add di, bx
    mov ax, [es:di + 2]
    cmp ax, [es:di + 6]
    jae .ret
    mov ax, [es:di + 6]
.ret:
    ret

; -----------------------------------------------------------------------------
; w16l_segalloc - a block for discardable segment DX, from the top of an arena
; (SPEC.md 124.26.3): its paragraph in the live table and [w16_sbase], its
; size in [w16_slen], its age new. ES = the tables. out: CF=1 no memory, and
; nothing changed. Keeps all.
; -----------------------------------------------------------------------------
w16l_segalloc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, dx
    dec bx
    shl bx, 1
    call w16l_segtab                ; AX = its size
    mov [w16_slen + bx], ax
    mov si, [w16_ne]                ; a library's: its record + 1 in the
    test word [es:si + NE_FLAGS], NEF_LIB ; owner's high byte (124.26.11)
    jz .own
    mov dh, [w16l_lrec]
    inc dh
.own:
    push dx
    xor dx, dx
    mov cl, W16G_HIGH
    ICALL w16_gnew                  ; AX = the handle: its segment, as DX
    pop cx                          ; CX = its number, CH its owner's
    or ax, ax
    jz .none
    mov [w16_sbase + bx], dx
    dec ax
    W16SEG es, ax                      ; its header: code, and which
    or byte [es:GH_FLAGS], GHF_CODE
    mov [es:GH_OWNER], cx
    or ch, ch                       ; the budget is the program's code's
    jnz .nobud
    mov ax, [es:GH_SIZE]
    add [w16_cused], ax
.nobud:
    xor ch, ch
    xchg dx, cx                     ; DX = the number, CX = the paragraph
    call w16l_lent
    mov [es:si + W16CL_BASE], cx
    or byte [es:si + W16CL_FL], W16CLF_NEW ; not to go before it is filled
    inc word [w16_gclock]
    mov ax, [w16_gclock]
    mov [es:si + W16CL_AGE], ax
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16l_segfill - discardable segment DX's block filled (SPEC.md 124.26.3): its
; bytes read and the rest zeroed, its relocation records read into the module
; block and applied, its exported prologs patched and its thunks pointed at
; it. [w16_sbase] must be every segment's paragraph. The cluster cache is
; [w16_cseg]. out: CF=1 refused, [w16_l1]/[w16_l2] say why. Keeps all.
; -----------------------------------------------------------------------------
w16l_segfill:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bp, dx                      ; BP = its number
    mov bx, dx
    dec bx
    shl bx, 1                       ; BX = index * 2
    W16SEG es, [w16_cmb]
    call w16l_segtab                ; ES:DI -> its row
    mov ax, [es:di]
    call w16l_secoff                ; DX:AX = its bytes in the file
    mov cx, [es:di + 2]             ; its length
    add ax, cx
    adc dx, 0
    mov [w16_soff], ax              ; ...and where its records are
    mov [w16_soff + 2], dx
    sub ax, cx
    sbb dx, 0
    push di
    push es
    W16SEG es, [w16_sbase + bx]
    xor di, di
    call w16l_fread
    mov cx, [w16_slen + bx]
    pop es
    pop si                          ; SI -> its row
    jc .trunc
    push es                         ; the rest of it zeroed
    mov di, [es:si + 2]
    sub cx, di
    W16SEG es, [w16_sbase + bx]
    xor al, al
    cld
    rep stosb
    pop es
    test word [es:si + 4], NSF_RELOC
    jz .prolog
    mov ax, [w16_soff]              ; the count, then the records
    mov dx, [w16_soff + 2]
    mov di, [w16_crel]
    mov cx, 2
    call w16l_fread
    jc .trunc
    mov cx, [es:di]
    push cx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    jz .read
    mov ax, di                      ; the room, sized at launch for the most
    add ax, 2                       ;   a discardable segment has
    add ax, cx
    jc .toomany
    mov dx, [w16_clive]
    sub dx, [w16_cmb]
    push cx
    mov cl, 4
    shl dx, cl
    pop cx
    cmp ax, dx
    ja .toomany
    mov ax, [w16_soff]
    mov dx, [w16_soff + 2]
    add ax, 2
    adc dx, 0
    add di, 2
    call w16l_fread
.read:
    pop cx                          ; the count
    jc .trunc
    mov si, [w16_crel]
    add si, 2
    push word [w16_rpass]
    mov byte [w16_rpass], 1
.rec:
    jcxz .applied
    push cx
    push bp
    call w16_reloc1                 ; CF=1: a refusal, written (keeps no BP)
    pop bp
    pop cx
    jc .rfail
    add si, 8
    loop .rec
.applied:
    pop word [w16_rpass]
.prolog:
    mov ax, bx                      ; its exported prologs only
    inc ax
    mov [w16_pone], ax
    push bx
    call w16_prologs                ; (which keeps no BX)
    pop bx
    mov word [w16_pone], 0
    mov ax, bp                      ; its thunks, at it
    mov dx, [w16_sbase + bx]
    call w16l_thkset
    mov dx, bp                      ; filled: it may go now
    call w16l_lent
    and byte [es:si + W16CL_FL], ~W16CLF_NEW
    clc
    jmp short .out
.rfail:
    pop word [w16_rpass]
    stc
    jmp short .out
.toomany:
    pop cx
.trunc:
    mov si, w16_r_trunc
    mov di, w16_l1
    ICALL w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    ICALL w16_strcpy
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16l_segsup - the discardable segments loaded at launch (SPEC.md 124.26.3):
; the entry point's, every PRELOAD and every kept one - all of them on a CPU
; the census decides for. A block for each first, then each filled, so a kept
; segment's paragraph is there for every relocation that names it. A PRELOAD
; one that does not fit waits for its first call. ES = the tables.
; out: CF=1 refused, [w16_l1]/[w16_l2] written. Keeps ES.
; -----------------------------------------------------------------------------
w16l_segsup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [w16_cmb], 0
    jne .some
    jmp .ok
.some:
    mov cl, 1                       ; CL = 1: every one (the census)
%ifndef W16_CENSUS_ALWAYS
    cmp byte [w16_cpu186], 0
    je .blocks
    xor cl, cl
%endif
.blocks:
    xor bx, bx
.alloc:
    mov dx, bx
    shr dx, 1
    inc dx                          ; DX = its number
    call w16l_isdlive
    jnc .anext
    or cl, cl
    jnz .must
    push es
    push si
    call w16l_lent
    test byte [es:si + W16CL_FL], W16CLF_KEEP
    pop si
    pop es
    jnz .must
    mov si, [w16_ne]
    cmp dx, [es:si + NE_CS]         ; the entry point's
    je .must
    cmp byte [w16_ntk], 0           ; a later task: PRELOAD waits too, for
    jne .anext                      ;   its code can free only its own (124.28.5)
    call w16l_segtab
    test word [es:di + 4], NSF_PRELOAD
    jz .anext
    call w16l_segalloc              ; a PRELOAD one if it fits
    jmp short .anext
.must:
    call w16l_segalloc
    jc .nomem
.anext:
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .alloc
    xor bx, bx                      ; then each one that has a block, filled
.fill:
    mov dx, bx
    shr dx, 1
    inc dx
    call w16l_isdlive
    jnc .fnext
    cmp word [w16_sbase + bx], 0
    je .fnext
    call w16l_segfill
    jc .out
.fnext:
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .fill
.ok:
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.nomem:
    mov si, w16_r_mem
    mov di, w16_l1
    ICALL w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    ICALL w16_strcpy
    stc
    jmp short .out

; -----------------------------------------------------------------------------
; w16l_segin - discardable segment AX of the module whose thunks are at code
; segment DX loaded while the program runs (SPEC.md 124.26.3), for
; w16_segld: the program's, or a library's (124.26.11), whose segment is
; loaded with the program's loader state set aside and its own in place.
; out: CF=0 DX = its paragraph; CF=1 not loaded, [w16_l1]/[w16_l2] say why.
; Keeps all but DX.
; -----------------------------------------------------------------------------
w16l_segin:
    push bx
    call w16l_whose                 ; BX = 0 the program's, else a record
    or bx, bx
    jnz .lib
    pop bx
    jmp w16l_segin1
.lib:
    push cx
    mov cx, [w16_dir]               ; the program's folder: the library's
    push cx                         ;   own stands in for it
    mov cl, [w16_vol]
    push cx
    call w16l_push
    call w16l_libin
    call w16l_segin1
    pushf
    call w16l_pop
    popf
    pop cx
    mov [w16_vol], cl
    pop cx
    mov [w16_dir], cx
    pop cx
    pop bx
    ret

; w16l_whose - DX = the code segment a thunk was called through -> BX = 0
; the program's thunks (or no module's), else the record of the library
; whose they are (SPEC.md 124.26.11). Keeps the rest.
w16l_whose:
    push ax
    push es
    mov ax, dx
%ifdef PKG_MEGA
    ICALL w16_pvirt                 ; AX = its paragraph: a code half's is
%endif                              ;   its data half's
    xor bx, bx
    cmp ax, [w16_cthk]
    je .out
.lib:
    call w16l_nextmod
    jc .out                         ; (BX = 0)
    W16SEG es, [bx + W16L_MREC]
    cmp ax, [es:W16LM_CD + 6]       ; its w16_cthk
    jne .lib
.out:
    pop es
    pop ax
    ret

; w16l_libin - BX = a library's record: in the program's place, what
; loading one of its segments reads - its six words, its file's name and
; folder, its DGROUP, its record and its module references (SPEC.md
; 124.26.11). Keeps all.
w16l_libin:
    push ax
    push cx
    push si
    push di
    push es
    W16SEG es, [bx + W16L_MREC]
    mov si, W16LM_CD
    mov di, w16_cmb
    mov cx, 6
.c:
    mov ax, [es:si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .c
    mov ax, [w16_crel]              ; the tables' extent, which the entry and
    mov [w16_fend], ax              ;   name walks are bounded by (w16_crel:
                                    ;   their copy's length, made even)
    mov ax, [es:W16LM_DIR]
    mov [w16_dir], ax
    mov al, [es:W16LM_VOL]
    mov [w16_vol], al
    lea si, [bx + W16L_FILE]
    mov di, w16_name
    ICALL w16_strcpy
    mov ax, [bx + W16L_HINST]
    mov [w16_dgroup], ax
    mov ax, bx
    sub ax, w16_libs
    mov cl, 5                       ; / W16L_SIZE
    shr ax, cl
    mov [w16l_lrec], al
    W16SEG es, [w16_cmb]            ; its NE tables, at the block's 0
    mov word [w16_ne], 0
    mov ax, [es:NE_CMOD]
    mov [w16_nmod], ax
    push bx
    call w16_modules
    pop bx
    push bx                         ; ...and those that are libraries, as
    W16SEG es, [bx + W16L_MREC]     ;   its load bound them (w16l_mlibs)
    xor bx, bx
.lib:
    cmp bx, [w16_nmod]
    jae .libs
    mov al, [es:W16LM_LIBS + bx]
    cmp al, 0xFF
    je .lnext
    or al, W16M_LIB
    mov [w16_modid + bx], al
.lnext:
    inc bx
    jmp short .lib
.libs:
    pop bx
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16l_segin1 - w16l_segin's load, of the module whose six words are in
; place: its folder made current and a cluster cache taken for it, the
; budget kept (124.26.6), a block, and it filled. out: CF=0 DX = its
; paragraph; CF=1 not loaded, [w16_l1]/[w16_l2] say why. Keeps all but DX.
; -----------------------------------------------------------------------------
w16l_segin1:
    push ax
    push bx
    push cx
    push si
    push di
    push bp
    push es
    mov dx, ax
    call w16l_lent                  ; ES:SI -> its live entry
    jc .bad
    mov ax, [es:si + W16CL_BASE]
    or ax, ax
    jz .load
    mov dx, ax                      ; already here
    clc
    jmp .ret
.bad:
    mov si, w16_r_reloc
    jmp .lines
.load:
    mov bp, dx                      ; BP = its number
    mov byte [w16_cdo], 1
    mov word [w16_ne], 0
    mov cx, [w16_cnseg]             ; the program's, whatever a library's
    mov [w16_nseg], cx              ;   load left in the loader's scratch
    xor bx, bx
    xor si, si
.sbase:
    mov ax, [es:si + W16CL_BASE]
    mov [w16_sbase + bx], ax
    add si, W16CL_SIZE
    inc bx
    inc bx
    loop .sbase
    call OSAPI_FILE_HERE            ; where the user is, to be put back
    push dx
    push bx
    mov dx, [w16_dir]
    mov bl, [w16_vol]
    call OSAPI_FILE_GOTO_QM
    mov si, w16_r_disk
    jc .back
    W16SEG es, [w16_cmb]
    mov bx, bp
    dec bx
    shl bx, 1
    call w16l_segtab                ; AX = its size
    add ax, 15
    mov cl, 4
    shr ax, cl
    inc ax                          ; AX = the paragraphs its block will take
.budget:                            ; the least recently used ones not in
    mov cx, [w16_cused]             ;   use, out until it fits (124.26.6)
    add cx, ax
    jc .over
    cmp cx, [w16_cbud]
    jbe .fits
.over:
    mov dx, 0xFFFF
    call w16l_codeold
    jnc .budget
.fits:
    mov dx, bp                      ; its block first: the cluster cache is
    call w16l_segalloc              ;   for a moment, and smaller will do
    mov si, w16_r_code
    jc .back
    call w16l_segkeep               ; not discarded for its own cache
    push word [w16_csz]
    mov ax, [w16_csz]
.claim:
    push ax                         ; the cluster cache, a block of WIN16's:
    call w16l_b2p                   ;   W16_FCACHE KB, or one cluster when
    ICALL w16_kclaimp               ;   that cannot be had (124.26.3)
    pop ax
    jnc .cached
    cmp ax, [w16_clus]
    je .nocache
    mov ax, [w16_clus]
    mov [w16_csz], ax
    jmp short .claim
.nocache:
    pop word [w16_csz]
    mov dx, bp
    call w16l_segkeep
    call w16l_segdrop
    mov si, w16_r_code
    jmp short .back
.cached:
    W16TOSEL dx                     ; (w16_cseg holds a selector: 124.31.3)
    mov [w16_cseg], dx
    mov word [w16_clen], 0
    mov dx, bp
    call w16l_segkeep
    W16SEG es, [w16_cmb]
    call w16l_segfill
    pop word [w16_csz]              ; (the flags kept)
    mov si, 1                       ; SI = 1: refused, the lines written
    jc .drop
    xor si, si                      ; SI = 0: loaded
    inc word [w16_cnload]
    jmp short .uncache
.drop:
    call w16l_segdrop
.uncache:
    xor dx, dx
    xchg dx, [w16_cseg]
    W16TOVIRT dx
    ICALL w16_kfree
.back:
    pop bx                          ; the user's folder back
    pop dx
    call OSAPI_FILE_GOTO_QM
    mov byte [w16_cdo], 0
    cmp si, 1
    ja .lines                       ; SI = line 1
    je .failed
    mov dx, bp
    call w16l_lent
    mov dx, [es:si + W16CL_BASE]
    clc
    jmp short .ret
.lines:                             ; SI = line 1, the program's name line 2
    mov di, w16_l1
    ICALL w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    ICALL w16_strcpy
.failed:
    stc
.ret:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; w16l_segdrop - discardable segment DX's block given back and its thunks
; pointed at w16_thkhit: it is not loaded. Keeps all.
w16l_segdrop:
    push ax
    push bx
    push si
    push es
    mov ax, dx
    push dx
    xor dx, dx
    call w16l_thkset
    pop dx
    call w16l_lent
    xor ax, ax
    xchg ax, [es:si + W16CL_BASE]
    or ax, ax
    jz .out
    dec ax                          ; its header
    W16SEG es, ax
    cmp byte [es:GH_OWNER + 1], 0   ; the budget is the program's code's
    jne .free                       ;   (SPEC.md 124.26.11)
    mov bx, [es:GH_SIZE]
    sub [w16_cused], bx
.free:
    ICALL w16_gfreeblk
.out:
    pop es
    pop si
    pop bx
    pop ax
    ret

; w16l_segkeep - discardable segment DX's W16CLF_KEEP flipped: set while its
; block is new and its cluster cache is claimed, which may discard code
; (w16l_codeold passes a kept one over), clear again after. Keeps all.
w16l_segkeep:
    push si
    push es
    call w16l_lent
    xor byte [es:si + W16CL_FL], W16CLF_KEEP
    pop es
    pop si
    ret

; w16l_cfreeall - the module block of the module whose six words are in
; w16_cmb..w16_cnord given back, with every discardable segment of it that
; is loaded: a library's end, or its load refused (SPEC.md 124.26.11).
; [w16_cmb] = 0 after. Keeps all.
w16l_cfreeall:
    push dx
    cmp word [w16_cmb], 0
    je .out
    mov dx, [w16_cnseg]
.seg:
    call w16l_isdlive               ; each discardable one: its block, if it
    jnc .next                       ;   has one, and its thunks
    call w16l_segdrop
.next:
    dec dx
    jnz .seg
    xor dx, dx
    xchg dx, [w16_cmb]
    ICALL w16_kfree
.out:
    pop dx
    ret

; w16l_isthk - BL = a library's record index, DX = a segment an import of it
; was bound to: CF=1 it is the library's thunks' (SPEC.md 124.26.11). Keeps
; all.
w16l_isthk:
    push ax
    push bx
    push es
    mov al, bl
    mov ah, W16L_SIZE
    mul ah
    add ax, w16_libs
    mov bx, ax
    W16SEG es, [bx + W16L_MREC]
    cmp word [es:W16LM_TCS], 0
    je .no
    cmp dx, [es:W16LM_TCS]
    jne .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop es
    pop bx
    pop ax
    ret

; w16l_libfree - SI = a library's record, at its end (w16l_drop): its module
; block and its loaded discardable code given back (SPEC.md 124.26.11).
; Keeps all.
w16l_libfree:
    push bx
    mov bx, si
    call w16l_cdx
    call w16l_cfreeall
    call w16l_cdx                   ; (its six words 0 now)
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16l_codeold - DX = an age: the least recently used discardable code
; segment older than it, the program's or a library's, that no live stack
; names, discarded (SPEC.md 124.26.5, 124.26.11). One that is named is in
; use: it is aged as new and the next is tried. A program's is named by its
; own stack; a library's by any task's. CF=1 none went. Keeps all.
; -----------------------------------------------------------------------------
w16l_codeold:
    push ax
    push bx
    push cx
    push si
    push di
    push bp
    push es
    mov cx, 2 * W16_MAXSEG          ; at most so many are found in use
.try:
    mov di, 0xFFFF                  ; DI = the oldest's entry, BP its age,
    mov bp, dx                      ;   SI its module (0 the program's, else
    mov si, di                      ;   a library's record; FFFFh none)
    xor bx, bx                      ; BX = the module looked at
.mod:
    call w16l_cdx                   ; its six words in place...
    call w16l_oldest
    call w16l_cdx                   ; ...and back (the flags kept)
    jnc .nmod
    mov si, bx
.nmod:
    call w16l_nextmod
    jnc .mod
    cmp si, 0xFFFF
    je .none
    mov bx, si
    call w16l_cdx                   ; the oldest's module, in place
    W16SEG es, [w16_clive]
    call w16l_ulenset               ; what a return offset into it can be
    mov ax, [es:di + W16CL_BASE]
%ifdef PKG_MEGA
    push cx                         ; the stack names its code selector
    mov cl, 1                       ;   (124.31.4)
    ICALL w16_ssel
    pop cx
%endif
    call w16l_inuse
    je .used
    or bx, bx                       ; a library's: any task's stack
    jz .drop
    call w16l_pkuse
    jne .drop
.used:
    inc word [w16_gclock]           ; in use: new, and the next one
    mov ax, [w16_gclock]
    mov [es:di + W16CL_AGE], ax
    call w16l_cdx
    loop .try
.none:
    stc
    jmp short .out
.drop:
    push dx
    push bx
    mov ax, di                      ; its number: the entry / 6 + 1
    xor dx, dx
    mov bx, W16CL_SIZE
    div bx
    inc ax
    mov dx, ax
    call w16l_segdrop
    inc word [w16_cndrop]
    pop bx
    pop dx
    call w16l_cdx
    clc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; w16l_oldest - in the module whose six words are in place: a loaded
; discardable segment, neither kept nor new, older than BP. CF=1 the oldest
; of them: DI = its live entry, BP = its age. CF=0 none, DI and BP kept.
; Keeps the rest.
w16l_oldest:
    push ax
    push cx
    push si
    push es
    xor ah, ah                      ; AH = 1 once one is found
    cmp word [w16_cmb], 0
    je .out
    W16SEG es, [w16_clive]
    xor si, si
    mov cx, [w16_cnseg]
.seg:
    mov al, [es:si + W16CL_FL]
    and al, W16CLF_DISC | W16CLF_KEEP | W16CLF_NEW
    cmp al, W16CLF_DISC
    jne .next
    cmp word [es:si + W16CL_BASE], 0
    je .next
    cmp [es:si + W16CL_AGE], bp
    jae .next
    mov bp, [es:si + W16CL_AGE]
    mov di, si
    mov ah, 1
.next:
    add si, W16CL_SIZE
    loop .seg
.out:
    shr ah, 1                       ; CF = found
    pop es
    pop si
    pop cx
    pop ax
    ret

; w16l_cdx - BX = a library's record: its module block's six words
; exchanged with w16_cmb..w16_cnord, so the routines that work on "the"
; module work on it; again puts them back. BX = 0: the program's, in place
; already. Keeps every register and the flags (SPEC.md 124.26.11).
w16l_cdx:
    pushf
    or bx, bx
    jz .out
    push ax
    push cx
    push si
    push es
    W16SEG es, [bx + W16L_MREC]
    mov si, W16LM_CD
    mov cx, 6
.x:
    mov ax, [es:si]
    xchg ax, [si + w16_cmb - W16LM_CD]
    mov [es:si], ax
    inc si
    inc si
    loop .x
    pop es
    pop si
    pop cx
    pop ax
.out:
    popf
    ret

; w16l_nextmod - BX = 0 or a library's record -> BX = the next record whose
; library has a module block, CF=0; CF=1 none, BX = 0. Keeps the rest.
w16l_nextmod:
    push es
    or bx, bx
    jnz .adv
    mov bx, w16_libs - W16L_SIZE
.adv:
    add bx, W16L_SIZE
    cmp bx, w16_libs + W16_NLIB * W16L_SIZE
    jae .none
    cmp byte [bx + W16L_COUNT], 0
    je .adv
    W16SEG es, [bx + W16L_MREC]
    cmp word [es:W16LM_CD], 0
    je .adv
    clc
    pop es
    ret
.none:
    xor bx, bx
    stc
    pop es
    ret

; w16l_pkuse - AX = what a stack would hold for a library's segment: ZF=1 a
; PARKED task's live stack returns into it (w16l_scanw), or a parked task has no stack
; yet (SPEC.md 124.26.11, 124.28.2: its stack words read in its context
; block). Keeps all.
w16l_pkuse:
    push bx
    push cx
    push dx
    push di
    push es
    mov bx, w16_task
    mov dx, W16_MAXTASK
.t:
    cmp byte [bx + W16K_ST], W16KS_FREE
    je .n
    cmp word [bx + W16K_CTX], 0     ; the current task's: scanned already
    je .n
    mov es, [bx + W16K_CTX]
    mov cx, [es:w16_stop_bo]
    or cx, cx
    jz .out                         ; no stack yet: in use (ZF=1)
    mov di, [es:w16_pss_bo + w16_psp_ - w16_pss]
    mov es, [es:w16_pss_bo]
    sub cx, di
    jbe .n
    shr cx, 1
    jz .n
    call w16l_scanw
    je .out
.n:
    add bx, W16K_SIZE
    dec dx
    jnz .t
    inc dx                          ; ZF=0: none names it
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    ret

; w16l_scanw - ES:DI = a live stack's words, CX of them, AX = a segment:
; ZF=1 one of them is a far return address into it, the segment with the
; word below it an offset inside [w16l_ulen] (SPEC.md 124.26.5). Any word
; equal to it was the test before: a saved BP or an offset that happened to
; equal a selector held in-use code at a time Mozart's last bitmap needed
; the run it split. Clobbers CX, DI.
w16l_scanw:
    cld
.s:
    repne scasw
    jne .out                        ; none (ZF=0)
    push ax
    mov ax, [es:di - 4]             ; the return offset: 1 up to the length
    dec ax
    cmp ax, [w16l_ulen]
    pop ax
    jb .hit
    jcxz .none
    jmp short .s
.none:
    or sp, sp                       ; ZF=0
    ret
.hit:
    cmp ax, ax                      ; ZF=1
.out:
    ret

; w16l_ulenset - ES:DI = a live entry of the module in place: [w16l_ulen]
; = its segment's length, from the NE segment table (0 = 64KB). Keeps all.
w16l_ulenset:
    push ax
    push dx
    push si
    push es
    mov ax, di
    xor dx, dx
    mov si, W16CL_SIZE
    div si                          ; AX = the segment's index
    W16SEG es, [w16_cmb]            ; its NE tables, at the block's 0
    mov si, [es:NE_SEGTAB]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add si, ax
    mov ax, [es:si + 6]             ; its allocation (0 = 64KB)
    dec ax
    mov [w16l_ulen], ax             ; less one, as the scan compares
    pop es
    pop si
    pop dx
    pop ax
    ret

; w16l_inuse - AX = a paragraph: ZF=1 the program's live stack returns into
; it (w16l_scanw), from the waiting call's frame to the top (SPEC.md
; 124.26.5) - or the
; program has no stack yet: its own launch, which uses every segment it
; loads, so none of them goes for the next (124.26.3). Keeps all.
w16l_inuse:
    push cx
    push di
    push es
    cmp word [w16_stop], 0
    je .out
    mov es, [w16_pss]
    mov di, [w16_psp_]
    mov cx, [w16_stop]
    sub cx, di
    jbe .no
    shr cx, 1
    jz .no
    call w16l_scanw
    jmp short .out
.no:
    xor cx, cx
    inc cx                          ; ZF = 0
.out:
    pop es
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16l_keep - part 5 kept when the program has discardable code (SPEC.md
; 124.26.3): copied into a block of the global heap with the bind table it
; built, where w16_ldcall far-calls it from now on. out: CF=1 SI = reason
; -----------------------------------------------------------------------------
w16l_keep:
    cmp word [w16_ldk], 0           ; one copy serves every task (124.28.2)
    jne .ok
    cmp word [w16_cmb], 0
    jne .need
    push bx                         ; ...or a library has a module block
    xor bx, bx                      ;   (SPEC.md 124.26.11)
    call w16l_nextmod
    pop bx
    jc .ok
.need:
    push ax
    push cx
    push dx
    push di
    push es
    mov ax, (w16l_end - w16l_base + 15) / 16  ; PARAGRAPHS (SPEC.md 50.3.7)
    ICALL w16_kclaimp
    jc .mem
    W16SEG es, dx
    xor si, si
    xor di, di
    mov cx, (w16l_end - w16l_base + 1) / 2
    push ds
    push cs                         ; XSEC-OK: this part's own bytes, read
    pop ds
    cld
    rep movsw
    pop ds
    mov [w16_ldk], dx
    clc
    jmp short .out
.mem:
    mov si, w16_r_mem
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret
.ok:
    clc
    ret

section .text
; --- the loader's own bss, beside the package's -----------------------------
; (declared here, in the image, rather than in w16_* bss: they are scratch
; for one load and a few bytes)
w16l_ulen:  dw 0                    ; w16l_scanw's bound, less one
w16_fend:   dw 0                    ; the NE tables' extent from the header,
                                    ; = the tables claim's last valid offset + 1
w16l_code:  dw 0                    ; a library's code block (w16l_laylib)
w16l_dgb:   dd 0                    ; ...its DGROUP's bytes
w16l_rp:    dw 0                    ; ...its record, being written
w16l_lrec:  db 0                    ; ...its record, once registered
w16l_stoff: dw 0                    ; ...its segment tables, aside (stash)
w16l_entp:  dw 0                    ; ...its entry table and names, measured
w16l_entn:  dw 0
w16l_resp:  dw 0
w16l_resn:  dw 0
w16l_nresn: dw 0
w16l_ip:    dw 0                    ; ...and ne_csip
w16l_cs:    dw 0
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
w16_r_code:    db 'Not enough memory for its code.', 0
w16_r_lib:     db 'A Windows library, not a program.', 0
w16_r_self:    db 'It loads itself: not supported.', 0
w16_r_linkerr: db 'Its linker reported errors.', 0
%ifndef PKG_MEGA
w16_r_pmode:   db 'It needs protected mode.', 0
%endif
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
w16_r_notlib:  db 'Not a Windows library.', 0
w16_r_ldeep:   db 'Its libraries import too deeply.', 0
w16_r_nlib:    db 'It needs more libraries than WIN16 holds (8).', 0
