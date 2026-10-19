; =============================================================================
; osrom.asm - the code that rides in os8088's ROM (docs/plans/ROM-PLAN.md 1.3)
;
; The ROM window is the five spare 8KB sockets of an IBM 5150, U28-U32, which
; decode F4000-FDFFF. tools/os88rom.py lays the window out as ONE option ROM:
;
;   F4000  55 AA 40  jmp near rom_init  'OS'  fmt  kind  dd rom_patch  bal1 0
;          (16 bytes; ROM_FMT 2 - the kernel's doorbell calls the dword at +10)
;   F4010  the payload: the kernel's `.cold` (segment F400 - `.cold`'s
;          offsets start at 0x10, kernel.asm's COLD_VSTART), or the
;          socket-check pattern
;     ...  0xFF fill
;   TAIL   THIS FILE, assembled at the offset the tool chose (TAIL_AT)
;   FDFFF  bal2
;
; Two facts out of the BIOS listings decide the shape (ROM-PLAN 1.3):
;  - The option-ROM scan calls rom_init AFTER POST has pointed int 18h at
;    F600:0000, on 10/27/82 and on GLaBIOS alike, so rom_init can point it
;    here instead. Nothing has to sit at F600:0000, which lets `.cold` run
;    straight through it.
;  - The header declares 32KB (0x40), not 40. The 10/27/82 BASIC check runs
;    from wherever the scan pointer stopped until FE00 and tests before it
;    compares, so a 40KB declaration leaves the pointer on FE00 and the loop
;    wraps to segment 0 and checksums all of RAM. 32KB leaves it on FC00: one
;    module, FC000-FDFFF, which bal2 balances. bal1 balances the declared
;    32KB for the option-ROM check itself.
;
; It is assembled TWICE by the tool - once at TAIL_AT = 0 to learn its length,
; then at the real offset - because every near label in it is an offset into
; the F400 segment.
;
; CONTRACT at rom_init (an option-ROM init, both BIOSes): CS = F400, IP = 3
; through the header's jmp, far-called, interrupts in whatever state the BIOS
; left them. It preserves every register and returns with retf. At rom_stub
; (int 18h): whatever the BIOS had; it never returns.
; =============================================================================
cpu 8086
bits 16

%ifndef TAIL_AT
  %define TAIL_AT 0
%endif
%ifndef BUILD_STR
  %define BUILD_STR '?'
%endif

ROM_KIND_SOCK   equ 1           ; the socket-check ROM: a pattern, no OS
ROM_KIND_KERNEL equ 2           ; os8088's `.cold` and what adapts a kernel
                                ; to it (ROM-PLAN 3.4)
ROM_KIND_HMA    equ 3           ; ...and the same adapter for the HMA: no
                                ; option ROM, no `.cold` (the kernel's own is
                                ; copied up), a file XMEM.DRV reads and calls
                                ; (docs/plans/HMA-PLAN.md)
%ifndef ROM_KIND
  %define ROM_KIND ROM_KIND_SOCK
%endif

ROM_SIZE        equ 40960       ; F4000-FDFFF
ROM_SEG         equ 0xF400      ; U28's base, the header's paragraph...
                                ; (...and ROM_COLDSEG, the segment `.cold`
                                ; runs at, is after the tables below: it
                                ; depends on the kernel's COLD_VSTART)
ROMF_SIZE       equ 0           ; rom_modfix's functions: kernel/kernel.asm's
ROMF_COPY       equ 1           ; equates of the same names, which the kernel
ROMF_PKG        equ 2           ; loads into AL. FIX is the last of the
ROMF_FIX        equ 3           ; four: the kernel's rom_none tests for it
ROMF_HPLACE     equ 4           ; ...and the HMA image's (docs/plans/HMA-PLAN.md
ROMF_HCLAIM     equ 5           ; 5): the drivers and the blocks under `.cold`.
                                ; (6 was HFREE: HCLAIM with CX = 0 now)
ROMF_HDROP      equ 7           ; A ROM is never asked any of them - the
ROMF_HRESET     equ 8           ; kernel's drv_hma_door asks only the HMA's
ROMF_HTAB       equ 9           ; (...and a hibernate asks where hb_tab is)
ROM_MODHB       equ 4           ; MOD_H_BUILD: the build word of a module's
                                ; header and of mod_hdrt (RT_HDRT), which
                                ; kernel/mod.inc pins at +4 with an %error
SOCK_SIZE       equ 8192        ; one socket, U28 = 0 ... U32 = 4
SOCK_N          equ 5

    org TAIL_AT

; --- the identity block, FIRST in the tail ------------------------------------
; The header's jmp to rom_init lands 32 bytes past it, which is how a tool
; finds it. Format 1 also pointed the header at it and the kernel's doorbell
; read +24 here; format 2 put that far pointer in the header itself, so this
; block is for tools (ROM-PLAN 3.4.3). os88rom.py writes the fields it knows
; only after assembly (the payload's length and place) into the zeros left
; for them.
rom_id:
    db 'OS88ROM', 0             ; +0  a second signature, past the payload
    db ROM_KIND                 ; +8
    db 0                        ; +9  reserved
%if ROM_KIND == ROM_KIND_HMA
    dw TAIL_AT                  ; +10 where this tail sits in the HMA, which
                                ; XMEM.DRV subtracts to find the CS to call
    dw hb_tab                   ; +12 the block table, for a reader (tests/
                                ; hmadrv.py): HB_N records of base, bytes,
                                ; owner, base 0 = free
    dw rom_tail_end - TAIL_AT   ; +14 the file's length
%else
    dw 0                        ; +10 payload offset in the window (tool)
    dw 0                        ; +12 payload length (tool)
    dw 0                        ; +14 tail length (tool)
%endif
    times 8 db 0                ; +16 the KEY (tool; kernel ROMs only)
    dw rom_patch                ; +24 the patcher's entry, which the tool
                                ; copies into the header's +10
%if ROM_KIND == ROM_KIND_HMA
    dw rom_res                  ; +26 where the half that stays begins: the
%else                           ; tool places the file so it is a paragraph
    dw 0                        ; +26 reserved      past `.cold`
%endif
    times 4 db 0                ; +28 reserved
%if $ - rom_id != 32
  %error "rom_id must be 32 bytes - the kernel's probe reads it at fixed offsets"
%endif

%if ROM_KIND != ROM_KIND_HMA     ; an HMA image is a file: no POST, no int 18h
; --- rom_init - called by the BIOS's option-ROM scan, during POST -------------
rom_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    pushf
    xor ax, ax
    mov ds, ax
    cli                         ; the vector is two words; nothing may take
    mov word [0x18*4], rom_stub ; int 18h between them
    mov [0x18*4+2], cs
    popf
    push cs
    pop ds
    mov si, rom_s_post
    call rom_puts
%if ROM_KIND == ROM_KIND_SOCK
    call rom_sock_check
%endif
    mov si, rom_s_crlf
    call rom_puts
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; --- rom_stub - int 18h, which a BIOS raises when there is nothing to boot ---
; The request's "insert disk and press any key, then reboot": int 19h runs the
; BIOS's bootstrap again, and a bootstrap that fails comes back here.
rom_stub:
    sti
    push cs
    pop ds
.again:
    mov si, rom_s_stub
    call rom_puts
    xor ax, ax                  ; wait for a key
    int 0x16
    mov si, rom_s_crlf
    call rom_puts
    int 0x19
    jmp short .again            ; int 19h is not meant to return

; --- rom_puts - DS:SI = an asciz string, through the BIOS teletype ------------
rom_puts:
    push ax
    push bx
.next:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp short .next
.done:
    pop bx
    pop ax
    ret

%if ROM_KIND == ROM_KIND_SOCK    ; ...and its one caller is the socket check:
                                ; a kernel ROM carried it unreached (kernel
                                ; size pass 12, 13 bytes of the window)
; --- rom_putc - AL through the BIOS teletype; preserves every register ------
rom_putc:
    push ax
    push bx
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    pop bx
    pop ax
    ret
%endif

%endif

%if ROM_KIND == ROM_KIND_SOCK
; --- rom_sock_check - prove every socket reads back what was burned ----------
; The payload is the pattern f(o) = (lo(o) + 3*hi(o) + 0x5A) mod 256 over the
; window offset o (tools/os88rom.py, sock_byte). It moves with BOTH halves of
; the address, so a stuck or swapped address line, a chip select wired to the
; wrong socket (the high byte carries the socket), or a byte the board drops
; all read as a mismatch IN THE SOCKET THAT HAS IT. The header, the int 18h
; landing at F6000 and this tail are not pattern, and are skipped.
SKIP_A0 equ 0
SKIP_A1 equ 16                  ; the header
SKIP_B0 equ 0x2000              ; F6000: a far jump to rom_stub, for a BIOS
SKIP_B1 equ 0x2005              ; that never runs rom_init (the 1981 ones)
rom_sock_check:
    xor bx, bx                  ; BX = the offset under test
    xor dx, dx                  ; DL = bad sockets, one bit each
.loop:
    cmp bx, SKIP_A1
    jb .skip
    cmp bx, SKIP_B0
    jb .test
    cmp bx, SKIP_B1
    jb .skip
.test:
    mov al, bh
    add al, al
    add al, bh
    add al, bl
    add al, 0x5A
    cmp al, [cs:bx]
    je .skip
    mov cl, bh                  ; the socket is the offset's top three bits
    shr cl, 1                   ; (8192 = 2^13, so bits 13-15 of BX = bits
    shr cl, 1                   ; 5-7 of BH)
    shr cl, 1
    shr cl, 1
    shr cl, 1
    mov al, 1
    shl al, cl
    or dl, al
.skip:
    inc bx
    cmp bx, strict word TAIL_AT ; the pattern ends where this code starts
    jb .loop
    mov cl, 0                   ; ...and the verdict, one socket at a time
.say:
    mov si, rom_s_sock          ; " U"
    call rom_puts
    mov al, '8'                 ; U28 + n: "28" "29" "30" "31" "32"
    add al, cl
    mov ch, '2'
    cmp al, '9'
    jbe .digits
    sub al, 10
    mov ch, '3'
.digits:
    xchg al, ch
    call rom_putc
    mov al, ch
    call rom_putc
    mov si, rom_s_ok
    mov al, 1
    shl al, cl
    test dl, al
    jz .tell
    mov si, rom_s_bad
.tell:
    call rom_puts
    inc cl
    cmp cl, SOCK_N
    jb .say
    ret
%endif

%if ROM_KIND >= ROM_KIND_KERNEL
; =============================================================================
; THE KERNEL'S ADAPTER (docs/plans/ROM-PLAN.md 3.4.3)
;
; A ROM_COLD kernel is assembled against its own RAM rung - every far
; reference to `.cold` names COLD_RAM - and is expanded whole by stage 2,
; `.cold` included. Then the boot overlay's first instruction rings rom_patch.
; This ROM was cut from ONE
; build and carries, in the tables tools/os88rom.py generated (ROMTAB), every
; place that build names `.cold`'s segment; it decides whether the kernel in
; RAM IS that build, and only then re-points those words at its own `.cold`.
;
; "Is that build" is three tests, every one before a byte is written:
;   1. this ROM's `.cold` and the one just expanded are the same bytes - the
;      strongest test there is, and free in a kernel that expands it anyway;
;   2. a hash over `.text`, the build number's words left out, so a ROM cut
;      before a commit that touched nothing else survives it - while a `.text`
;      routine that changed its contract without moving does not;
;   3. every word on the lists still says COLD_SEG (COLD_RAM - 1: `.cold`'s
;      offsets start at 0x10).
; A NO from any of them is CF = 1 and an untouched kernel: a wrong ROM is no
; ROM. tools/os88rom.py is the other half and its --selfcheck is the gate.
;
; THE TABLES (ROMTAB, generated):
;   RT_KSEG RT_FATSEG RT_COLDRAM  the kernel's segments, as built; COLD_RAM
;   RT_COLDSEG                    is the rung and COLD_SEG, one paragraph
;                                 under it, what every far reference names
;   RT_COLDLEN                    `.cold`'s length; ours is at CS:0x10
;   RT_HASH                       the `.text` hash; rt_spans its word spans
;   rt_text / rt_win / rt_blob    the far references, by the segment each is
;                                 an offset into: `.text`, the window half
;                                 `.ovl` at FAT_SEG, and the blob `.boot2`,
;                                 whose segment is [spl_fseg]
;   rt_vgab, RT_VGABRAM           kern_big's `.vgabuf` words, and the rung
;                                 they name, moved down to RT_COLDRAM
;   rt_mods                       per mod_tab ROW: its list and its count (the
;                                 settings core's row shares CTRL.DRV's)
;   rt_img                        per ROW: the image this ROM holds (its offset
;                                 in the window and its bytes), 0 = none
;   RT_PKGOFF RT_PKGLEN           the one package it may hold, and its name,
;   rt_pkgname                    which ROMF_PKG matches (SPEC.md 2.10.5)
;   RT_MFP                        rom_mfp's offset in the kernel's `.text`
;   RT_FSEG                       spl_fseg's, where the blob's segment is
; =============================================================================
%include ROMTAB

%if ROM_KIND == ROM_KIND_HMA
ROM_COLDSEG     equ 0xFFFF      ; the segment `.cold` runs at: the one segment
                                ; that reaches above 1MB, FFFF:x being linear
                                ; 0xFFFF0 + x - so `.cold` is copied to its own
                                ; offsets, RT_COLDVST up, the top of the HMA
%else
ROM_COLDSEG     equ ROM_SEG - (RT_COLDVST - 0x10) / 16
                                ; ...and in ROM: whatever puts `.cold`'s first
                                ; offset (kernel.asm's COLD_VSTART) on F4010,
                                ; the paragraph after the header
%endif

RT_INSTALL_DATA                 ; the INSTALLER's tables: the hash spans and
                                ; the kernel's far-reference lists, read once
                                ; by rom_patch and never again

; --- rom_patch - adopt the kernel expanded at RT_KSEG, or say no -------------
; in:  nothing. A ROM: far-called through the header's +10 by the boot
;      overlay's doorbell in kmain_o, before a byte of `.cold` has run. The
;      HMA image: far-called by XMEM.DRV's attach, from the claim it read this
;      file into, with CS = that claim less TAIL_AT/16 so every label here is
;      right - late in the boot, after the mount (docs/plans/HMA-PLAN.md)
; out: CF = 0 adopted: every listed word names ROM_COLDSEG and the kernel's
;      rom_mfp names rom_modfix. CF = 1: nothing anywhere was written
; preserves: every register (the doorbell saves only what it touches); leaves
;      the direction flag clear
rom_patch:
    push ax                     ; EVERY REGISTER KEPT, here and not in the
    push bx                     ; kernel's doorbell: these bytes are ROM and
    push cx                     ; cost nobody RAM, the doorbell's are the
    push dx                     ; boot overlay's, which kern_big has filled
    push si
    push di
    push bp
    push ds
    push es
    cld
    mov ax, RT_KSEG             ; BP = the blob, for rt_blob: [spl_fseg], which
    mov ds, ax                  ; stage 2 (or the VBR) published before the
    mov bp, [RT_FSEG]           ; kernel was entered - read here so the
                                ; doorbell need not carry it
%if ROM_KIND == ROM_KIND_KERNEL
    ; --- 1. `.cold`, byte for byte -------------------------------------------
    push cs
    pop ds
    mov si, 0x10                ; ours: the paragraph after the header
    mov ax, RT_COLDRAM
    mov es, ax
    xor di, di
    mov cx, RT_COLDLEN / 2
    repe cmpsw
    jne .no
%if RT_COLDLEN & 1
    cmpsb
    jne .no
%endif
%endif
    ; --- 2. `.text`, hashed over its spans -----------------------------------
    mov ax, RT_KSEG
    mov ds, ax
    xor dx, dx
    mov bx, rt_spans
.span:
    cmp bx, rt_spans_end
    jae .hashed
    mov si, [cs:bx]             ; start, even
    mov cx, [cs:bx+2]           ; words
    add bx, 4
    jcxz .span
.word:
    lodsw
    rol dx, 1
    xor dx, ax
    loop .word
    jmp short .span
.hashed:
    cmp dx, RT_HASH
    jne .no
    ; --- 3. every listed word names what it should - and only then, 4. ------
    mov bx, RT_COLDSEG          ; BX = RT_COLDSEG: look (what is there is
    call rp_walk                ; what each word gets back)
    jc .no
%if ROM_KIND == ROM_KIND_HMA
    ; --- HMA: no module may be HELD. This runs after the mount, and a module a
    ; boot had already read would carry far references the lists below never
    ; reach - so its claim is refused rather than guessed at. None is held at
    ; this point in any boot measured, which is what makes the refusal free
    mov ax, RT_KSEG
    mov es, ax
    mov di, RT_MODTAB
    mov cx, RT_NMODS
.held:
    cmp word [es:di], 0
    jne .no
    add di, RT_MODRSZ
    loop .held
    ; --- ...and `.cold` and this tail go up, and are read back --------------
    ; A20 is XMEM.DRV's to have verified and the HMA its to have claimed; the
    ; read-back is what says the bytes are THERE, before anything points at
    ; them. `.cold` comes from its rung, which nothing has written: stage 2
    ; read it and no byte of it is data (os88ovlchk's rule)
    mov ax, ROM_COLDSEG
    mov es, ax
    mov ax, RT_COLDRAM
    mov ds, ax
    xor si, si
    mov di, RT_COLDVST          ; `.cold`'s own offsets: FFFF:x is 0xFFFF0 + x
    mov cx, RT_COLDLEN
    rep movsb
    push cs
    pop ds
    mov si, rom_res             ; ...and the half of this file that stays
    mov di, si                  ; (rom_res, above)
    mov cx, rom_tail_end - rom_res
    rep movsb
    mov si, rom_res             ; ...back, the tail first: DS is still ours
    mov di, si
    mov cx, rom_tail_end - rom_res
    repe cmpsb
    jne .no
    mov ax, RT_COLDRAM
    mov ds, ax
    xor si, si
    mov di, RT_COLDVST
    mov cx, RT_COLDLEN
    repe cmpsb
    jne .no
%endif
    mov bx, ROM_COLDSEG         ; BX = ROM_COLDSEG: write
    call rp_walk
    mov ax, RT_KSEG             ; ...and the door mod_need far-calls
    mov es, ax
%if ROM_KIND == ROM_KIND_HMA
    ; --- ...and every module SLOT, which no list can reach: mod_init_x filled
    ; them at boot with `.cold`'s CS (COLDSEG_TO, the one store of it at run
    ; time), pointing every entry of every module not held at mod_gone. A ROM
    ; adopts before that runs and so never sees it; this runs after, and a
    ; slot left naming the old rung calls into the heap the first time a
    ; module is asked for. With no module held (above) every slot says
    ; mod_gone, so the seg word is the whole of what moves
    mov di, RT_MODFP + 2
    mov cx, RT_NSLOT
    jcxz .slotted
.slot:
    cmp word [es:di], RT_COLDSEG
    jne .snext
    mov word [es:di], ROM_COLDSEG
.snext:
    add di, 4
    loop .slot
.slotted:
%endif
    mov word [es:RT_MFP], rom_modfix
%if ROM_KIND == ROM_KIND_HMA
    mov word [es:RT_MFP+2], ROM_COLDSEG ; the COPY's, not this claim's
%else
    mov [es:RT_MFP+2], cs
%endif
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; rp_walk - the four lists, looked at (BX = RT_COLDSEG, CF = 1 at the first
; word that is not COLD_SEG) or written (BX = ROM_COLDSEG). BP = the blob.
rp_walk:
    mov ax, RT_KSEG
    mov si, rt_text
    mov cx, RT_NTEXT
    call rp_list
    jc .out
%if ROM_KIND == ROM_KIND_KERNEL     ; the window half is FAT data by the time
    mov ax, RT_FATSEG               ; the HMA image runs (after the mount), and
    mov si, rt_win                  ; nothing calls it again: not a word of it
    mov cx, RT_NWIN                 ; may be touched
    call rp_list
    jc .out
%endif
    mov ax, bp
    mov si, rt_blob
    mov cx, RT_NBLOB
    call rp_list
    jc .out
    ; ...and the planar decoder's buffers (kern_big): RT_NVGAB words in
    ; `.text` that name the `.vgabuf` rung move to the BOTTOM of the dead cold
    ; rung, which is where mem_floor_ax leaves them (ROM-PLAN 3.4.2)
    mov ax, RT_KSEG
    mov es, ax
    mov si, rt_vgab
    mov cx, RT_NVGAB
    jcxz .vok
.vnext:
    cs lodsw                    ; (DF = 0: rom_patch's cld)
    xchg ax, di
    cmp bx, RT_COLDSEG
    jne .vwrite
    cmp word [es:di], RT_VGABRAM
    jne .vbad
    loop .vnext
    jmp short .vok
.vwrite:
    mov word [es:di], RT_COLDRAM
    loop .vnext
.vok:
    clc
.out:
    ret
.vbad:
    stc
    ret

%if ROM_KIND == ROM_KIND_HMA
; --- WHAT STAYS IN THE HMA -----------------------------------------------------
; Everything above is the installer and its lists, run once from XMEM.DRV's
; claim and never again; everything from here to rom_tail_end is the module
; door, the pool and their tables, and is ALL rom_patch copies up behind
; `.cold`. tools/os88rom.py assembles this file so that this line lands on the
; paragraph after `.cold` - the installer's half then sits at offsets `.cold`
; occupies in the HMA, which is fine: it never runs from there.
    align 16, db 0
rom_res:
%endif
; rp_list - AX = the segment, CS:SI = CX offsets, BX = what each word gets:
; RT_COLDSEG to look (a word that says it is given it back, which writes
; nothing that was not there) or ROM_COLDSEG to write. Either way CF = 1 at
; the first word that does not say COLD_SEG - which on a write cannot happen,
; the look having just read every one of them, and each list being a set of
; distinct offsets (tools/os88rom.py walks the diff two bytes at a time).
; DX = no limit. Clobbers AX, CX, DI, SI (past the list), ES = AX. One body
; for both passes rather than a branch per word (kernel size pass 12, -14)
rp_list:
    mov es, ax
    mov dx, 0xFFFF
; ...and the module door's way in, with DX = the LAST offset a word may start
; at, plus one: the bytes it read less one, so a word at or past DX is not in
; this claim (the settings core reads a PART, SPEC.md 2.8.7). DF = 0
rp_list_lim:
    jcxz .ok
.next:
    cs lodsw
    xchg ax, di
    cmp di, dx
    jae .skip
    cmp word [es:di], RT_COLDSEG
    jne .bad
    mov [es:di], bx
.skip:
    loop .next
.ok:
    clc
    ret
.bad:
    stc
    ret

; --- rom_modfix - the door mod_need and the Task Manager item call ----------
; (ROM-PLAN 3.4.4, 4.3; SPEC.md 2.10.4, 2.10.5). AL picks, kernel.asm's ROMF_*:
;   0 SIZE  BX = a mod_tab ROW -> CF = 0, AX = the bytes of that row's image
;           this ROM holds; CF = 1 it holds none (the disk is asked instead)
;   1 COPY  BX = a row, ES = its claim, CX = bytes -> the image's first CX
;           bytes to ES:0, AX = CX, DX = 0, CF = 0 (dskw_read_x's answer);
;           CF = 1 it holds none, EVERY register kept - mod_need asks COPY
;           without a test and reads the disk with the same SI, CX and ES
;   3 FIX   BX = a row, BP = the bytes its claim holds -> every listed far
;           reference to `.cold` re-pointed at ROM_COLDSEG; CF = 1 = not this
;           ROM's module (a word does not say COLD_SEG) and nothing written -
;           mod_need then refuses it. ES is the claim's, out of the row
;           ([bx], MODR_SEG = 0), and is NOT kept. The ROM's own copies are
;           kept AS THE BUILD CUT THEM, so a copied module is fixed like a
;           read one. BX and BP, not ES and CX: mod_need holds both already,
;           and loading the other two cost it 5 bytes (kernel size pass 12)
;   2 PKG   DS:SI = a package's 8.3 name (the kernel's) -> the package STARTED
;           out of this ROM: ldf_ld_pkg_start run on ES:DI = its image here,
;           DX:CX = its length, so CF/AL are that far entry's answer and the
;           registers are what it leaves; CF = 1 none by that name, every
;           register kept
; preserves: every register but the outputs named and the flags. A ROW and
; not an id because mod_need has no register left holding the id - and the
; settings core (MOD_SETS) is a row of its own over CTRL.DRV's image, which
; the tool's tables already resolve, so nothing here knows it exists.
rom_modfix:
%if ROM_KIND == ROM_KIND_HMA
    cmp al, ROMF_HPLACE
    jae rom_hma
%endif
    cmp al, ROMF_PKG
    je rom_pkg
    push bx
    push si
    sub bx, RT_MODTAB           ; the row's offset into mod_tab - a row
                                ; mod_row gave mod_need, in the kernel this
                                ; ROM adopted: it is not re-checked here
%if RT_MODRSZ != 4
  %error "rt_mods and rt_img are indexed by row*4 and mod_tab's stride is not 4"
%endif
    cmp al, ROMF_FIX
    je .fix
    mov si, [cs:rt_img+bx]      ; the image's offset in this ROM, 0 = none
    or si, si
    jz .no
    push ax                     ; ...and none unless it is THIS kernel's
    mov ax, [cs:si+ROM_MODHB]   ; build: rom_patch adopts a kernel that
    cmp ax, [RT_HDRT+ROM_MODHB] ; differs only in BUILD_NUM, and mod_check
    pop ax                      ; would refuse the copy with no disk tried.
    jne .no                     ; SIZE and COPY still agree, and the disk is
                                ; read as if this ROM held nothing
    cmp al, ROMF_COPY
    je .copy
    mov ax, [cs:rt_img+bx+2]    ; SIZE: the bytes, out in AX
    jmp short .yes
.copy:
    push cx
    push di
    push ds
    push cs
    pop ds
    cld
    xor di, di
    rep movsb                   ; CX = the bytes asked for (mod_need's BP)
    pop ds
    pop di
    pop cx
    mov ax, cx                  ; the read's answer: all of it, DX:AX. CX
    cwd                         ; is under MOD_MAX_KB, so DX = 0
.yes:
    clc
    jmp short .out
.fix:
    push ax
    push cx
    push dx
    push di
    mov es, [bx + RT_MODTAB]    ; the row's MODR_SEG (+0), DS = the kernel's
    mov dx, bp                  ; DX = the limit for rp_list_lim: the bytes
    dec dx                      ; it holds, less one (BP > 0: a header)
    mov si, [cs:rt_mods+bx]     ; the list...
    mov cx, [cs:rt_mods+bx+2]   ; ...and its count
    push si
    push cx
    cld
    mov bx, RT_COLDSEG          ; look
    call rp_list_lim
    pop cx
    pop si
    jc .fixed
    mov bx, ROM_COLDSEG         ; ...then write: CF = 0
    call rp_list_lim
.fixed:
    pop di
    pop dx
    pop cx
    pop ax
    jmp short .out
.no:
    stc
.out:
    pop si
    pop bx
    retf

; rom_pkg - ROMF_PKG: the one package this ROM may carry, LAUNCHED, if it is
; the one ui_sys_open is opening: the kernel's [ui_desc] (RT_UIDESC) names a
; descriptor whose first word (UO_FILE, ui.inc checks it is 0) is the 8.3
; name. CF and AL are then ldf_ld_pkg_start's answer for it; CF = 1 with SI
; = that name and every other register kept when this ROM holds no package
; of that name. The kernel used to load SI itself (kernel size pass 12, -6)
rom_pkg:
%if RT_PKGLEN
%if ROM_COLDSEG != ROM_SEG
  %error "rom_pkg jumps NEAR into `.cold`, which runs at ROM_COLDSEG - it must be this tail's own segment"
%endif
    push ax
    mov si, [RT_UIDESC]         ; DS = the kernel's: the descriptor...
    mov si, [si]                ; ...and its name
    push si
    push di
    mov di, rt_pkgname
.cmp:
    lodsb
    cmp al, [cs:di]
    jne .no
    inc di
    or al, al
    jnz .cmp
    pop di
    pop si
    pop ax
    push cs
    pop es
    mov di, RT_PKGOFF            ; the image, in the payload ahead of this tail
    mov cx, RT_PKGLEN
    xor dx, dx
    ; ...AND LAUNCH IT: ldf_ld_pkg_start is `.cold`, and `.cold` is this
    ; segment (kern_small's COLD_VSTART is 0x10, so ROM_COLDSEG is F400 -
    ; asserted above), so a near jump with the caller's far frame still on the
    ; stack is that far entry called by the kernel, and its retf answers the
    ; kernel's `call far [rom_mfp]`. SI is the name again (popped above),
    ; DS the kernel's. The kernel's ui_sys_open spends 7 bytes less on it
    ; (kernel size pass 12). NEAR, pinned: the tool assembles this file twice
    ; at two origins and the tail's length must not move between them
    jmp near RT_LDFPKG
.no:
    pop di
    pop si
    pop ax
%endif
    stc
    retf
%if ROM_KIND == ROM_KIND_HMA
; =============================================================================
; THE REST OF THE HMA (docs/plans/HMA-PLAN.md 5)
;
; `.cold` and this tail fill the HMA from RT_COLDVST up. Under them, from 0010
; to RT_COLDVST, is a pool of BLOCKS: a driver image the kernel has just
; loaded (ROMF_HPLACE) and a driver's own buffer (ROMF_HCLAIM, the kernel's
; OSAPI_MEM_HMA). Every block is addressed the same way - a segment of its own
; and offsets from HB_ORG - because nothing above 1MB is reachable at offset 0:
; FFFF:x is linear 0xFFFF0 + x, so a block at base b is C:HB_ORG with
; C = 0xFFFF - (HB_ORG - b)/16, which puts every C in F200-F7xx. That is the
; reason the pool is UNDER `.cold`: above it, C would be FBxx-FFxx, which is
; the heap's owner tags, and a claim a driver made would read as purgeable.
;
; The record is three words - base (0 = free), bytes, owner - and the owner of
; a placed driver image is its own segment, so ROMF_HDROP with a driver's
; segment frees the image and every buffer it held in one sweep, which is what
; drv_release does. Bottom-up first fit, paragraphs: the pool holds a handful
; of blocks and a fragment costs a walk of eight records.
;
; A DRIVER IMAGE IS RELOCATED, not re-read. The build assembles each driver
; this file knows twice, at org 0 and at org HB_ORG, and the difference is a
; list of bytes that gain HB_ORG's high byte - kept here as rt_hdrv, keyed by
; a hash of the org-0 image the kernel loads. A driver this tail has no table
; for, or whose bytes are not the build's, is refused and stays in the heap.
; =============================================================================
HB_N        equ 8
HB_ORG      equ RT_DRVORG
HB_MAX      equ 0x10000 - HB_ORG    ; a block's largest: its offsets end at FFFF
HB_LO       equ 0x0010
HB_HI       equ RT_COLDVST
%if HB_ORG & 0xFF
  %error "HB_ORG must have a zero low byte: a relocation is one byte, the high"
%endif
%if HB_HI > HB_ORG
  %error "the pool reaches past HB_ORG, where a block's segment would wrap"
%endif

; rom_hma - the HMA's functions, AL = ROMF_HPLACE .. ROMF_HTAB
;   HPLACE  ES = the claim holding an org-0 driver image the kernel has just
;           checked and zeroed the bss of, CX = image + bss bytes -> CF = 0,
;           AX = the segment it runs at in the HMA now (its header at AX:HB_ORG).
;           CF = 1: no table for it, no room, or the copy did not read back -
;           and the claim is untouched either way
;   HCLAIM  CX = bytes (1..HB_MAX), BX = the owner -> CF = 0, DX = segment,
;           AX = HB_ORG, the block's first offset. CF = 1 no room. CX = 0
;           gives one BACK instead: DX = a block's segment, BX = its owner ->
;           CF = 0 freed; CF = 1 it is not a block, or not BX's, or BX's own
;           image (OSAPI_MEM_HMA's two forms, so the kernel calls once)
;   HDROP   DX = an owner -> every block it owns, its image included. CF = 0
;           when DX is a segment in the pool (F000 up: that image ran here),
;           CF = 1 when it is the heap's - drv_release's "is there a claim
;           to free as well", answered where the segment's meaning is known
;   HRESET  every block. CF = 0 (a resume whose image carried no pool)
;   HTAB    -> CF = 0, DX:AX = hb_tab and CX = its bytes: a hibernate copies
;           it and every live block into its image, and the wake copies them
;           back (SPEC.md 87.4, 87.6). A block's own bytes are FFFF:base
; preserves: every register but AX and the outputs named
rom_hma:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    cld
    cmp al, ROMF_HPLACE
    je hb_place
    mov si, hb_tab              ; (hb_free's walk, reached from hb_claim)
    cmp al, ROMF_HTAB
    je hb_htab
    cmp al, ROMF_HCLAIM
    je hb_claim
    cmp al, ROMF_HDROP
    je hb_drop
    cmp al, ROMF_HRESET
    jne hb_no
    push cs                     ; HRESET: the whole table zero, which is
    pop es                      ; every record free (ES, DI and CX are in the
    mov di, si                  ; frame; AX is the door's to clobber)
    mov cx, HB_N * 3
    xor ax, ax
    rep stosw
    jmp hb_yes

hb_htab:                        ; HTAB: SI = hb_tab, which is AX out; DX
    mov bp, sp                  ; and CX are the saved frame's, as hb_claim
    mov [bp+10], cs             ; writes its DX
    mov word [bp+12], HB_N * 6
    mov ax, si
    jmp hb_yesax

hb_free:                        ; SI = hb_tab
    cmp word [cs:si], 0
    je .next
    cmp [cs:si+4], bx           ; the owner's...
    jne .next
    mov ax, [cs:si]
    call hb_seg                 ; ...block at DX...
    cmp ax, dx
    jne .next
    cmp ax, bx                  ; ...and not its own image
    je hb_no
    mov word [cs:si], 0
    jmp hb_yes
.next:
    add si, 6
    cmp si, hb_tab_end
    jb hb_free
    jmp hb_no

hb_drop:                        ; SI = hb_tab
    cmp [cs:si+4], dx
    jne .next
    mov word [cs:si], 0
.next:
    add si, 6
    cmp si, hb_tab_end
    jb hb_drop
    cmp dh, 0xF0                ; CF = 1: a heap owner (header above)
    jmp hb_out

hb_claim:
    jcxz hb_free                ; CX = 0: DX's block back (SI = hb_tab)
    cmp cx, HB_MAX
    ja hb_no
    call hb_alloc               ; AX = base, DI = its record
    jc hb_no
    call hb_seg                 ; AX = the segment...
    mov bp, sp
    mov [bp+10], ax             ; ...into the saved DX, which the epilogue
    mov ax, HB_ORG              ; restores
hb_yesax:                       ; CF = 0 with AX the answer - which is
hb_yes:                         ; hb_yes's exit exactly: AX is in no frame
    clc                         ; slot, so the one epilogue below returns it
    jmp hb_out                  ; (it was a second copy of that epilogue)
hb_no:
    stc
hb_out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; hb_place - ROMF_HPLACE. ES = the claim. The bytes it copies, image + bss,
; are the table's: the hash covers the header's bss byte, so the image that
; matches is the one the table was built from and its length is known
hb_place:
    mov bp, [es:8]              ; the IMAGE's bytes (the header's +8), what the
    xor si, si                  ; hash is over: the bss is zeros either way
    xor dx, dx
    mov bx, bp
    shr bx, 1
    jz .odd
.word:
    mov ax, [es:si]
    add si, 2
    rol dx, 1
    xor dx, ax
    dec bx
    jnz .word
.odd:
    test bp, 1
    jz .hashed
    mov al, [es:si]
    xor ah, ah
    rol dx, 1
    xor dx, ax
.hashed:
    mov si, rt_hdrv             ; THE TABLE whose image this is, by hash and
.find:                          ; both lengths, or none
    cmp si, rt_hdrv_end
    jae hb_no
    cmp [cs:si], dx
    jne .fnext
    cmp [cs:si+2], bp
    je .found
.fnext:
    add si, 10
    jmp .find
.found:
    mov cx, [cs:si+4]           ; CX = image + bss: what is copied
    xor bx, bx                  ; no owner yet: it is the image's own segment,
    push si                     ; known once the base is
    call hb_alloc
    pop si                      ; (the table: hb_alloc walks with SI)
    jc hb_no
    push di                     ; the record
    push si                     ; the table
    push es
    pop ds                      ; DS:0 = the image...
    mov bx, ax                  ; BX = the base
    mov di, ax
    mov ax, 0xFFFF
    mov es, ax                  ; ...to FFFF:base
    xor si, si
    push cx
    rep movsb
    pop cx
    xor si, si                  ; ...and READ BACK before anything is changed
    mov di, bx                  ; or anything points at it: CF = 1 here and
    repe cmpsb                  ; the claim is still the image that runs
    pop si
    pop di
    jne .bad
    mov cx, [cs:si+8]           ; THE RELOCATION: CX bytes of the stream gain
    mov si, [cs:si+6]           ; HB_ORG's high byte, each a delta from the
    mov bp, bx                  ; last (0 = an absolute word follows)
    jcxz .placed
.rel:
    mov al, [cs:si]
    inc si
    or al, al
    jz .abs
    xor ah, ah
    add bp, ax
    jmp .add
.abs:
    mov bp, [cs:si]
    add si, 2
    add bp, bx
.add:
    add byte [es:bp], HB_ORG >> 8
    loop .rel
.placed:
    mov ax, bx
    call hb_seg                 ; AX = the segment it runs at
    mov [cs:di+4], ax           ; ...which owns it
    jmp hb_yesax
.bad:
    mov word [cs:di], 0         ; the block back: nothing was installed
    jmp hb_no

; hb_alloc - CX = bytes, BX = owner -> CF = 0, AX = base, DI = its record;
; CF = 1 no record or no room. Lowest fit, paragraph-rounded. Clobbers DX, SI
hb_alloc:
    push cx
    add cx, 15
    jc .no
    and cl, 0xF0
    mov di, hb_tab              ; a free record...
.rec:
    cmp word [cs:di], 0
    je .room
    add di, 6
    cmp di, hb_tab_end
    jb .rec
    jmp .no
.room:                          ; ...and the lowest base that overlaps nothing
    mov ax, HB_LO
.scan:
    mov si, hb_tab
.each:
    cmp word [cs:si], 0
    je .next
    mov dx, [cs:si]
    add dx, [cs:si+2]           ; DX = that block's end
    cmp ax, dx
    jae .next                   ; we start past it
    mov dx, ax
    add dx, cx                  ; DX = our end
    cmp [cs:si], dx
    jae .next                   ; it starts past us
    mov ax, [cs:si]             ; overlap: try just past it, and look again
    add ax, [cs:si+2]
    jmp .scan
.next:
    add si, 6
    cmp si, hb_tab_end
    jb .each
    mov dx, ax
    add dx, cx
    jc .no
    cmp dx, HB_HI
    ja .no
    mov [cs:di], ax
    mov [cs:di+2], cx
    mov [cs:di+4], bx
    pop cx
    clc
    ret
.no:
    pop cx
    stc
    ret

; hb_seg - AX = a base -> AX = the segment whose HB_ORG it is. Flags.
hb_seg:
    push cx
    mov cl, 4
    shr ax, cl
    add ax, 0xFFFF - (HB_ORG >> 4)
    pop cx
    ret

hb_tab:     times HB_N * 6 db 0
hb_tab_end:
%endif
RT_RES_DATA                     ; ...and the door's own tables: the modules'
                                ; lists and images, the package's name, and
                                ; the HMA's drivers' relocations
%else
; A socket ROM carries a refusing stub so the identity block's pointer is never
; a wild one.
rom_patch:
    stc
    retf
%endif

; --- the strings ---------------------------------------------------------------
%if ROM_KIND == ROM_KIND_SOCK
rom_s_post: db 'os8088 ROM socket check, build ', BUILD_STR, ':', 0
rom_s_sock: db ' U', 0
rom_s_ok:   db ' ok', 0
rom_s_bad:  db ' BAD', 0
rom_s_stub: db 13, 10, 'os8088 socket-check ROM: no system here. '
            db 'Insert a system disk and press any key.', 0
%elif ROM_KIND == ROM_KIND_KERNEL
rom_s_post: db 'os8088 ROM build ', BUILD_STR, 0
rom_s_stub: db 13, 10, 'os8088 ROM build ', BUILD_STR, ': '
            db 'insert a system disk and press any key.', 0
%endif
%if ROM_KIND != ROM_KIND_HMA
rom_s_crlf: db 13, 10, 0
%endif

rom_tail_end:
