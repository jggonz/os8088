; =============================================================================
; os8088 - apps/dos/dosload.asm
;
; **DOS'S LOADER - the IMAGE of DOS.O88, and the whole of what the kernel
; launches** (SPEC.md 96.44.4, 20.12.10). It reads the windowed DOS out of
; part 0, tells it where `kern_dos` sits in the file, and asks the kernel to
; treat it as the program. Then its region is freed and it is gone: what runs
; is `apps/dos/dos.asm`, at part 0, with a window and an instance and no idea
; any of this happened.
;
; **WHY IT EXISTS IS A NUMBER AND THE NUMBER IS A LAUNCH TIME.**
; `tools/os88pkg.py` refuses `--compress` beside parts - a part's offset is
; measured from the start of the file and the table that holds it is INSIDE
; the image, so compressing the image and laying out its parts are circular -
; and 96.40.3 measured what that cost when arm 3 first shipped: the image went
; RAW, `DOS.O88` 26,723 -> 57,272 bytes, and the same click took **+932 ms
; (35%)** because the launch read 21 more sectors. Twelve soak rows went red
; at once, none of them for a reason of its own.
;
; The four-piece shape (docs/plans/KERN-DOS-PLAN.md 4.1.3.1) is the way out and
; it is not a workaround: **only the IMAGE has to be raw, and the image can be
; a kilobyte.** Everything heavy becomes a part, and a part may be `OP_COMP`.
;
; **KERN_DOS IS A LAZY PART, AND IT HAS TO BE** - not a style choice.  `op_load`
; claims and reads every EAGER part into one carve, and `op_size` refuses a
; carve of 64KB or more; part 0 unpacks to ~45KB and `kern_dos` to ~29KB, so
; an eager pair would be refused outright before a sector was read. It is also
; the wrong thing to want: nothing ever `op_fetch`es this row, because 4.1.1's
; whole argument is that the handoff walks the part's bytes into EXTENTS while
; the file layer is alive and a stub reads them with `int 13h` - by then the
; heap has been given away and there is nowhere to `op_load` to. So what the
; windowed DOS needs off this row is three numbers, and the loader copies
; them into the DOS's own bss before it disappears.
;
; **`OP_COMP` BESIDE `OP_LAZY` WAS REFUSED AND IS NOT ANY MORE** (SPEC.md
; 20.12.7.4). The refusal was right about the mechanism - one `zkb` word, two
; meanings - and backwards about the conclusion: every lazy row in this tree
; is a compressed stream, and each had grown a packer and an expander outside
; the standard to get there. The word is shared in TIME now - the packed
; length until the fetch, the segment after - and this row never fetches at
; all, so the second meaning never arrives. The stub needs no other figure:
; `kds_expand` takes a byte count and runs to the end of the stream, and
; `KDS_ULEN` is carried and never read.
;
; IT MUST NOT CREATE A WINDOW (SPEC.md 20.12.10.6): its region is about to be
; freed, so a window whose `W_SEG` named it would far-call a dead claim on its
; first repaint. The windowed DOS's window is the one the user sees.
; =============================================================================
cpu 8086
bits 16

%include "os88api.inc"

    ; OS88_HIGHMEM: the loader's one claim of its own is dsl_w16's one-cluster
    ; probe buffer (SPEC.md 124.3.1), reached only through ES for READ_AT and
    ; freed - no paragraph arithmetic - and assembled only with DOS_WIN16,
    ; which kern_mega never defines (KERN-MEGA-PLAN 14.51, tests/megahigh.txt).
    OS88_HEADER 'DOS', dsl_entry, 3 | OS88_F_GLYPH | OS88_F_PARTS, OS88_STACK_DEFAULT | OS88_HIGHMEM

%include "dosicon.inc"          ; ...and the SAME icon, association block and
                                ; document glyph part 0 carries: all read out
                                ; of the IMAGE - the Disk window draws this
                                ; one before the launch, and `assoc.inc` reads
                                ; the three extensions off it to decide that a
                                ; .COM is ours at all

%include "doscall.inc"              ; CORE_ORG - where the core has to land
%define OP_NO_LAZYOK                ; ...and no op_lazyok: the core is fetched
                                    ; ONCE, here, and op_fetch's own toast is
                                    ; the answer - there is no menu item to
                                    ; grey ahead of it (SPEC.md 20.12.9.2)
%include "os88parts.inc"

DOS_PART_PKG  equ 0             ; windowed DOS: a whole .o88 image (20.12.10)
%ifdef DOS_MEGA
DOS_PART_CORE equ 1             ; ...the INT 21h core, which goes INSIDE it
DOS_PART_KD   equ 2             ; ...and kern_dos, which nothing here reads
DOS_PART_KDL  equ 3             ; ...and kern_mega's LOW kern_dos (SPEC.md
DOS_PART_HELP equ 4             ; 96.40.9.2), which nothing here reads either
%else
DOS_PART_WIN  equ 1             ; ...and its WINDOW, the tail of the same
                                ; assembly (SPEC.md 96.44.19): EAGER and
                                ; adjacent, so the carve lays it at the
                                ; offset it was assembled at and a windowed
                                ; launch reads both in one run - and a
                                ; headless one never claims it at all
DOS_PART_CORE equ 2             ; ...the INT 21h core, which goes INSIDE it
DOS_PART_KD   equ 3             ; ...and kern_dos, which nothing here reads
DOS_PART_HELP equ 4             ; ...and HELP's text, which the HOST reads
DOS_PART_NET  equ 5             ; with op_self (DOS-PARTS-PLAN 1.1) - and the
                                ; NETWORK, the same way (SPEC.md 96.23.11)
; --- the Setup page's part, APPENDED (SPEC.md 96.32.2.3) -------------------
DOS_PART_SETUP equ 6            ; ...and the SETUP PAGE's code, the same way
                                ; again: read when Setup opens. kern_mega's
                                ; DOS.O88 keeps the page inline (its host is
                                ; above 1MB), so it has no such row
%endif
; --- DOS_PART_DVW: the windowed layer (SPEC.md 96.54.9) ---------------------
%ifndef DOS_MEGA                ; ...and the WINDOW, the same way again: the
DOS_PART_DVW  equ DOS_PART_SETUP + 1  ; layer of SPEC.md 96.54, read at the first
%endif                          ; Run in Window. kern_mega's DOS has none
; --- end DOS_PART_DVW ---------------------------------------------------------

; --- the handoff, at the head of the DOS's bss (SPEC.md 20.12.10.2) ---------
; ONE PACKAGE, TWO SOURCES: `apps/dos/dos.asm` declares these and this file is
; the other end of them. The kernel is not involved and has no opinion; it
; does not zero a part, which is the whole of what makes this work.
;
; IT IS AN `OP_ROW` AND NOT A STRUCT OF OUR OWN, deliberately: the DOS already
; read `[dos_kdrow + OP_R_OFF]` out of the part table when the table was in
; its own image, so copying the row VERBATIM leaves all four of its read sites
; spelled exactly as they were and moves only the base.
DSLH_KDROW   equ 0              ; OP_ROW bytes, copied as they lie
DSLH_COREROW equ OP_ROW         ; ...and the CORE's beside it (SPEC.md 96.44.5)
%ifndef DOS_MEGA
DSLH_SIZE    equ OP_ROW * 2
%else
DSLH_KDLROW  equ OP_ROW * 2     ; ...and kern_mega's low kern_dos (96.40.9.2)
DSLH_SIZE    equ OP_ROW * 3
%endif
%ifdef DOS_WIN16                ; ...and on the kernels that carry WIN16, the
DSLH_DOC     equ DSLH_SIZE      ; DOCUMENT and its checked byte (SPEC.md
DSLH_W16K    equ DSLH_DOC + 16  ; 124.3.1), written straight after the core's
DSLH_HLPROW  equ DSLH_W16K + 1  ; row by the copy that left DI there - and
%else                           ; HELP's row LAST (DOS-PARTS-PLAN 1.1), its
DSLH_HLPROW  equ DSLH_SIZE      ; kind byte the stream's format
%endif
%ifndef DOS_MEGA
DSLH_NETROW  equ DSLH_HLPROW + OP_ROW   ; ...and the network part's after it
                                        ; (SPEC.md 96.23.11), the same shape
DSLH_SETROW  equ DSLH_NETROW + OP_ROW   ; ...and the Setup page's after THAT
%endif                                  ; (SPEC.md 96.32.2.3)
; --- DSLH_DVWROW --------------------------------------------------------------
%ifndef DOS_MEGA
DSLH_DVWROW  equ DSLH_SETROW + OP_ROW   ; ...and the window's (SPEC.md 96.54.9)
DSLH_HEAD    equ DSLH_DVWROW + OP_ROW   ; ...and the headless byte (96.44.20)
%endif
; --- end DSLH_DVWROW ----------------------------------------------------------

LD_H_IMG    equ 8               ; ...and the two header fields this file reads
LD_H_BSS    equ 10              ; them at, which are the FORMAT's and not ours

; -----------------------------------------------------------------------------
; dsl_row - copy part AL's table row to ES:DI (SPEC.md 96.44.5)
; in:  AL = the part, ES:DI = where it goes
; out: nothing; DI past the row
; clobbers: AX, CX, SI, DI, flags
; -----------------------------------------------------------------------------
dsl_row:
    call op_row                     ; SI -> the table row, AX preserved
; --- dsl_cpy: the same copy from a row we already hold ----------------------
; in:  DS:SI = the row, ES:DI = where it goes
; **BYTE BY BYTE and not `movsw`**: ES is the destination and DS is ours, so
; the string form would want both set, and the stash below is read with ES
; pointing at the windowed DOS.
dsl_cpy:
    mov cx, OP_ROW
.b:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jnz .b
    ret

; -----------------------------------------------------------------------------
; dsl_core - put the INT 21h core into the hole reserved for it (SPEC.md 96.44.5)
; in:  DX = the windowed DOS's segment, the parts already loaded
; out: CF=0; CF=1 = it could not be had, and op_fetch has said why
; clobbers: AX, BX, CX, SI, DI, ES, flags. DX preserved.
;
; **op_load CANNOT PUT IT THERE**, which is why this proc exists. The carve
; lays its parts out one after another, and the core has to land at CORE_ORG
; *inside* the windowed DOS's own segment - the one address both hosts agree
; on, which is what makes every `call dos_load` in the DOS a near call into the
; table (apps/dos/doscall.inc). So the core is a LAZY part: op_fetch claims it, reads
; it and expands it somewhere of its own, this copies it into place, and
; op_drop gives the claim straight back.
;
; THE PAIRING IS WHAT SPEC.md 20.12.7.4 HAD TO UNREFUSE. A lazy row could not
; be OP_COMP until that section, so the choice here would have been a raw
; 14.5KB part or a packer of this package's own - which is the third time a
; package would have written one.
;
; THE COPY IS ~41 ms on a 4.77MHz 8088 (`rep movsw` at 13.3 cycles a byte,
; PERFORMANCE.md Set 117.2) and it happens once, at a launch that is already
; reading 40KB off a floppy. The core's bss needs no zeroing: it is inside the
; windowed DOS's own image, and op_unpack has zero-filled the slot round the
; part's two streams (OP_ZFILL, SPEC.md 96.44.17) - zeros it does not ship.
; -----------------------------------------------------------------------------
dsl_core:
    mov al, DOS_PART_CORE
    call op_fetch                   ; claims, reads and expands it (20.12.4)
    jc .no
    mov al, DOS_PART_CORE
    call op_row                     ; SI -> the row, for its unpacked length
    mov cx, [si+OP_R_LEN]
    inc cx
    shr cx, 1                       ; CX = words, rounded up
    mov al, DOS_PART_CORE
    call op_seg                     ; AX = where op_fetch put it
    or ax, ax
    jz .no
    push ax
    ; **THE DOS'S SEGMENT IS READ HERE AND NOT BY THE CALLER**, because
    ; op_fetch CLAIMS - and a claim may compact, which moves the carve the DOS
    ; is sitting in (SPEC.md 66.4). Read before the fetch, it is a segment the
    ; heap has since moved out from under, and the copy below lands on whoever
    ; owns those paragraphs now. It cost a launch that reported SUCCESS and put
    ; up no window, with 14.5KB written into the middle of nothing.
    mov al, DOS_PART_PKG
    call op_seg
    or ax, ax
    jz .nopop
    mov es, ax                      ; ES:DI = the hole, at the one offset both
    mov di, CORE_ORG                ; hosts agree on
    pop ax
    push ds
    mov ds, ax                      ; DS:SI = the core, where op_fetch put it
    xor si, si
    cld
    rep movsw
    pop ds
    mov al, DOS_PART_CORE
    call op_drop                    ; ...and the claim goes straight back
    clc
    ret
.nopop:
    pop ax
.no:
    stc
    ret
%ifdef DOS_WIN16
; -----------------------------------------------------------------------------
; dsl_w16 - a Windows program, recognised before any of DOS is read
;           (SPEC.md 124.3.1)
; in:  ES = KERNEL_SEG (the entry contract); the instance stands where the
;      package was launched from
; out: CF=1 = it was an NE, and a running WIN16 took it (w16j_give, SPEC.md
;      124.28.12) or WIN16.O88's launch is POSTED: refuse, and the kernel
;      says nothing (SPEC.md 21.5.4). CF=0 = anything else: dsl_doc
;      holds the document (or zeros) and dsl_w16k says whether it was read,
;      and the instance stands where it stood
; clobbers: AX, BX, CX, DX, SI, DI, ES, BP, flags
;
; THE TEST IS dosw16.inc's, BYTE FOR BYTE, one launch earlier: an MZ whose
; relocation table starts at 40h or later carries e_lfanew at 3Ch, and a word
; `NE` there is the new executable. The host keeps its own copy for an NE
; typed at the prompt, named in Run or reached through a .LNK - the loader
; sees the link, not the program - and this one costs the host nothing it did
; not already pay: the read it makes is the read the host's wake no longer
; makes (dsl_w16k), so an ordinary .EXE pays ONE header read either way.
;
; OSAPI_ARG_FILE IS READ-AND-CLEAR, which is why the document is banked here
; and handed on into the host's bss beside the two rows: after this call the
; kernel has nothing left for the host to ask.
; -----------------------------------------------------------------------------
dsl_w16:
    call OSAPI_ARG_FILE             ; SI -> the name in KERNEL_SEG, DX:BL
    jc .no                          ; launched empty: nothing to hand on
    mov di, dsl_doc
    mov cx, 13
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [dsl_doc + 12], 0
    mov [dsl_doc + 13], dx
    mov [dsl_doc + 15], bl
    push ds
    pop es                          ; ES is ours from here
    push bx
    push dx
    call OSAPI_FILE_HERE            ; where the package was launched from:
    mov [dsl_home], dx              ; op_load reads DOS.O88 by name THERE
    mov [dsl_home + 2], bl
    pop dx
    pop bx
    call OSAPI_FILE_GOTO_QM         ; stand where the document is
    jc .home
    call OSAPI_FILE_DFREE           ; BX = sectors per cluster: READ_AT wants
    jc .home                        ; offset and capacity in whole clusters
    cmp bx, 64
    ja .home                        ; a 64KB cluster: no 16-bit CX names it
    mov ax, bx
    inc ax
    shr ax, 1                       ; KB, at least one
    MEMKB_CLAIM                     ; DX = the block
    jc .home
    mov bp, dx
    mov cl, 9
    shl bx, cl                      ; BX = the cluster in bytes
    call dsl_opsz                   ; ONE BAR FOR THE LAUNCH BEGINS HERE, so
    add cx, bx                      ; this read is in it (SPEC.md 96.44.5.5):
    adc dx, 0                       ; the parts and this cluster. Begun after
    call dsl_opgo                   ; it, the cluster was a fill of its own
    mov di, bx                      ; DI = it, for the second read
    mov cx, bx                      ; --- the first cluster -------------------
    mov es, bp
    xor bx, bx
    xor ax, ax
    xor dx, dx
    mov si, dsl_doc
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    jc .free
    mov byte [dsl_w16k], 1          ; READ: whatever it is, the host's wake
    mov cx, ax                      ; need not read it again. CX = delivered
    cmp ax, 0x40
    jb .free
%ifndef DOS_MEGA
    ; --- A SHORTCUT SAVED `Close when done` (SPEC.md 96.44.20) ------------
    ; The Shell Link header's own ShowCommand, at a fixed offset in the
    ; cluster this read already holds: 7, SW_SHOWMINNOACTIVE, is no window,
    ; so part 1 - the window - is never claimed. The magic and the CLSID's
    ; first word say it is a link at all (the host's dos_lnk_parse checks the
    ; rest, and a link it refuses is a refusal the host reports headless).
    ; THE DWORD AT 3Ch IS BOTH FORMS' FIELD - a link's ShowCommand, an MZ's
    ; e_lfanew - and both want its high word zero, so that is read ONCE
    cmp word [es:0x3E], 0           ; ShowCommand's / e_lfanew's high word
    jne .free
    mov ax, [es:0x3C]
    cmp word [es:0], 0x004C         ; HeaderSize
    jne .notlnk
    cmp word [es:4], 0x1401         ; LinkCLSID {00021401-...}
    jne .free
    cmp ax, 7                       ; ShowCommand = SW_SHOWMINNOACTIVE
    jne .free
    inc byte [dsl_head]
    jmp short .free
.notlnk:
    cmp word [es:0], 0x5A4D         ; 'MZ'
    jne .free
    cmp word [es:0x18], 0x40        ; ...with room for a new header's pointer
    jb .free
%else
    cmp word [es:0], 0x5A4D         ; 'MZ'
    jne .free
    cmp word [es:0x18], 0x40        ; ...with room for a new header's pointer
    jb .free
    cmp word [es:0x3E], 0           ; e_lfanew's high word: inside 64KB
    jne .free
    mov ax, [es:0x3C]
%endif
    test al, 1                      ; even, so the word cannot straddle the
    jnz .free                       ; cluster's end
    mov dx, di
    dec dx                          ; a power of two, so a mask
    mov si, ax
    and si, dx                      ; SI = e_lfanew within its cluster
    not dx
    and ax, dx                      ; AX = that cluster's own offset
    jz .have                        ; the first: already here
    push si                         ; --- ...or the one holding e_lfanew ----
    mov cx, di
    xor bx, bx
    xor dx, dx
    mov si, dsl_doc
    call OSAPI_FILE_READ_AT
    pop si
    jc .free
    mov cx, ax                      ; CX = delivered, as above
.have:
    lea ax, [si + 2]
    cmp cx, ax                      ; delivered past the signature?
    jb .free
    cmp word [es:si], 0x454E        ; 'NE'
    jne .free
    mov si, dsl_doc                 ; --- IT IS ONE ---------------------------
    call w16j_give                  ; a running WIN16 takes it (SPEC.md
    pushf                           ;   124.28.12) - the host's own arm's
    mov dx, bp                      ;   routine, in the claim this probe
    call OSAPI_MEM_FREE             ;   already holds
    popf
    jc .done                        ; taken: refused, as silently as a post
    push ds
    pop es
    mov si, dsl_w16pkg              ; WIN16's stem names the slot (124.3),
    mov di, dsl_doc                 ; the program its document, DX:CX = 0:
    xor cx, cx                      ; from an entry proc this is a POST, and
    xor dx, dx                      ; the kernel launches it once this one
    call OSAPI_PKG_START            ; has unwound (SPEC.md 21.5.4)
    jnc .done                       ; CF=1: nothing opens it - DOS runs the
    jmp short .home                 ; stub, as the host's own arm would
.free:
    mov dx, bp
    call OSAPI_MEM_FREE
.home:
    mov dx, [dsl_home]              ; back where op_load will read DOS.O88
    mov bl, [dsl_home + 2]
    call OSAPI_FILE_GOTO_QM
.no:
    clc
    ret
.done:
    stc
    ret

dsl_w16pkg: db 'WIN16.O88', 0

%include "win16/w16join.inc"        ; the join box (SPEC.md 124.28.12)...
%include "win16/w16jgive.inc"       ; ...and w16j_give, the host's own copy
%endif

; -----------------------------------------------------------------------------
; dsl_entry - the package entry proc (SPEC.md 20.2)
; in:  SI = the launched file's name in KERNEL_SEG, ES = KERNEL_SEG
; out: CF=0 and BX = 0 (no window of ours); CF=1 = the launch is torn down
; -----------------------------------------------------------------------------
%ifdef DOS_MEGA
    db 'KMEG'                       ; the mark kernel/megados.inc's mega_pkgok
                                    ; looks for (KERN-MEGA-DOS-PLAN 12.1 item 4)
%endif
dsl_entry:
%ifdef DOS_MEGA
    ; **A kern_mega DOS.O88 ON ANY OTHER KERNEL REFUSES** (docs/plans/
    ; KERN-MEGA-DOS-PLAN.md 12.1 item 4): its DOS has no windowed arm and its
    ; only route parks the machine above 1MB, which only kern_mega can do.
    ; Asked first, so nothing is read before the answer.
    call OSAPI_CPU_INFO             ; AH = CPU_F_*; every register but AX kept
    test ah, CPU_F_PM
    jnz dsl_pmok
    push es
    push ds
    pop es
    mov si, dsl_s_notpm
    xor cx, cx
    call OSAPI_TOAST
    pop es
    stc
    ret
TOASTSTR dsl_s_notpm, "DOS.O88 is kern_mega's"
dsl_pmok:
%endif
%ifdef DOS_WIN16
    ; --- A WINDOWS PROGRAM IS WIN16'S, AND IT IS ASKED HERE (SPEC.md 124.3.1)
    ; Before a sector of part 0 is read: an NE is POSTED to WIN16.O88 and
    ; this launch refuses, so no carve is claimed, no window is made and
    ; nothing of DOS is ever on the glass. Anything else comes back CF=0 with
    ; the document banked for the host, and the launch goes on as it was.
    push si
    push es
    call dsl_w16
    pop es
    pop si
    jc .no                          ; posted: refused, and SILENT (21.5.4) -
                                    ; .out ends the bar dsl_w16 began
%endif
    ; ONE OPERATION FOR THE LAUNCH (SPEC.md 12.8.10, 96.44.5.5): the DOS's
    ; carve and the core's fetch, their PACKED lengths - what is read - as one
    ; bar. op_load's and op_fetch's own begins are refused as nesting. A
    ; document's probe (dsl_w16) has begun it already, with its cluster in it;
    ; .out, the one exit, ends it
%ifdef DOS_WIN16                ; (headless needs a LINK, and only the WIN16 arm reads one -
%ifndef DOS_MEGA                ; and never kern_mega's, whose DOS has no window part)
    ; **HEADLESS: THE WINDOW IS NEVER CLAIMED** (SPEC.md 96.44.20). The table
    ; is this image's, which is per-instance RAM, and op_size sizes the carve
    ; from the EAGER rows alone - so one bit makes part 1 a lazy row nobody
    ; fetches, the carve is part 0's length, the re-home keeps to part 0's
    ; end, and the region is born the window's size smaller. Nothing is read
    ; for it either: the carve's run ends at the last eager row
    cmp byte [dsl_head], 0
    je .win
    or byte [op_table + OP_T_ROWS + DOS_PART_WIN * OP_ROW + OP_R_FLAG], OP_LAZY
    call dsl_opend                  ; the probe's bar counted the window it
.win:                               ; will not read: begun again, below
%endif
%endif
%ifdef DOS_WIN16
    cmp byte [dsl_opok], 0
    jne .begun                      ; the probe's bar stands
%endif
    call dsl_opsz
    call dsl_opgo
.begun:
    call op_load                    ; sizes first and reads nothing if it will
    jc .no                          ; not fit (20.12); a toast has said why

    ; --- THE CORE'S ROW IS TAKEN BEFORE IT IS SPENT (SPEC.md 20.12.7.4) ----
    ; `dsl_core` below FETCHES this row and drops it again, and a dropped
    ; compressed row's `zkb` is `OP_SPENT` rather than its packed length -
    ; which is deliberate over there and fatal here, because the handoff needs
    ; that exact figure to expand the core after the heap is gone (96.44.5.4).
    ; Copied afterwards it is 0xFFFF, and the stub reads 64KB of rubble.
    push ds
    pop es
    mov di, dsl_crow
    mov al, DOS_PART_CORE
    call dsl_row

    call dsl_core                   ; the core, into the hole inside the DOS -
    jc .no                          ; and it CLAIMS, so nothing may hold the
                                    ; DOS's segment across it

    mov al, DOS_PART_PKG
    call op_seg                     ; ...read AFTER every claim this proc makes
    or ax, ax
    jz .no
    mov dx, ax                      ; DX = where the DOS is

    ; --- the row, into the head of the DOS's bss ---------------------------
    mov es, dx
    mov di, [es:LD_H_IMG]           ; the bss begins here, which the part's own
    add di, DSLH_KDROW              ; header says
    mov al, DOS_PART_KD
    call dsl_row                    ; ...kern_dos's row
    mov di, [es:LD_H_IMG]
    add di, DSLH_COREROW            ; **AND THE CORE'S**, because the stub reads
    mov si, dsl_crow                ; BOTH on the way to arm 3 (SPEC.md
    call dsl_cpy                    ; 96.44.5.4): kern_dos's image has a hole
%ifdef DOS_MEGA
    mov di, [es:LD_H_IMG]
    add di, DSLH_KDLROW             ; ...and the LOW kern_dos's, which the DOS
    mov al, DOS_PART_KDL            ; posts instead of part 2's when its HMA
    call dsl_row                    ; box is unticked (SPEC.md 96.40.9.2).
                                    ; BEFORE the document: DSLH_DOC follows
                                    ; this row on kern_mega, and the copy
                                    ; below writes at DI, which is left here
%endif
%ifdef DOS_WIN16
    mov si, dsl_doc                 ; ...AND THE DOCUMENT, which dsl_w16 took
    call dsl_cpy                    ; with OSAPI_ARG_FILE - read-and-clear, so
    call dsl_cpy                    ; the host can no longer ask (SPEC.md
    mov al, [si]                    ; 124.3.1): the 16-byte locator, name[0]
    mov [es:di], al                 ; 0 = launched empty, then the byte that
%endif                              ; says it was read and is no NE
    mov di, [es:LD_H_IMG]
    add di, DSLH_HLPROW             ; ...and HELP's text's row, which the host
    mov al, DOS_PART_HELP           ; reads for itself after we are gone
    call dsl_row                    ; (DOS-PARTS-PLAN 1.1) - its KIND byte,
    mov al, [op_table + OP_T_FMT]   ; always OP_ASSET, overwritten with the
    mov [es:di - OP_ROW], al        ; FORMAT op_self is told, so the format
                                    ; costs the host no byte of its own
%ifndef DOS_MEGA
    mov al, DOS_PART_NET            ; ...and the NETWORK part's, straight after
    call dsl_row                    ; it (DI is there: SPEC.md 96.23.11), its
    mov al, [op_table + OP_T_FMT]   ; kind byte the format the same way
    mov [es:di - OP_ROW], al
    ; --- the Setup page's row, straight after (SPEC.md 96.32.2.3) ----------
    mov al, DOS_PART_SETUP          ; DI is there again, and the kind byte is
    call dsl_row                    ; the format as for the two before it
    mov al, [op_table + OP_T_FMT]
    mov [es:di - OP_ROW], al
%endif
; --- DOS_PART_DVW's row ------------------------------------------------------
%ifndef DOS_MEGA
    mov al, DOS_PART_DVW            ; ...and the WINDOW's after that (SPEC.md
    call dsl_row                    ; 96.54.9), DI there again, the same way
    mov al, [op_table + OP_T_FMT]
    mov [es:di - OP_ROW], al
%ifdef DOS_WIN16
    mov al, [dsl_head]              ; ...and LAST, the byte that says this
%else                               ; (no WIN16, no link probe: never headless)
    xor al, al
%endif
    mov [es:di], al                 ; region has no window (DSLH_HEAD, the
%endif                              ; host's [dos_head], SPEC.md 96.44.20)
; --- end DOS_PART_DVW's row ---------------------------------------------------
                                    ; where the core goes, and only this image
                                    ; knows where either one sits in the file.
                                    ; Out of the stash and not the table: the
                                    ; table's copy has been spent by now

    ; --- and the hand-over --------------------------------------------------
    ; In the KEEP form (SPEC.md 20.12.10.5): the standard works out, from the
    ; table, where the carve's last part ends, and the kernel cuts the carve
    ; there - so the read's margin past the program goes back to the heap
    ; instead of standing in the region for the session. The same figure is
    ; what bounds the part's image + bss (20.12.10.4).
    mov al, DOS_PART_PKG
    OS88_REHOME
    jc .no
    xor bx, bx                      ; NO WINDOW: ours is the region that is
    clc                             ; about to be freed (SPEC.md 20.12.10.6)
    jmp short .out
.no:
    stc                             ; ...and the kernel tears down what exists.
                                    ; op_load has already said why in a toast
.out:
    pushf
    call dsl_opend                  ; the end - refused, and harmless, when
    popf                            ; the begin was
    ret

; -----------------------------------------------------------------------------
; dsl_opsz - DX:CX = the launch's bytes: parts 0 and the core's PACKED lengths,
;            and the window's unless headless (SPEC.md 96.44.5.5)
; dsl_opgo - begin the launch's operation of DX:CX bytes; [dsl_opok] = 1 ours
; dsl_opend - end it; an end the begin did not open is refused by the kernel
;            (SPEC.md 12.8.10), so it is unconditional. CX and DX clobbered
; every other register and, but for dsl_opend, nothing else kept as they were
; -----------------------------------------------------------------------------
dsl_opsz:
    mov cx, [op_table + OP_T_ROWS + DOS_PART_PKG * OP_ROW + OP_R_ZKB]
    xor dx, dx
    add cx, [op_table + OP_T_ROWS + DOS_PART_CORE * OP_ROW + OP_R_ZKB]
    adc dx, dx
%ifdef DOS_WIN16
%ifndef DOS_MEGA                    ; (kern_mega's DOS has no window part)
    cmp byte [dsl_head], 0
    jne .r                          ; ...so the bar counts only a window it
    add cx, [op_table + OP_T_ROWS + DOS_PART_WIN * OP_ROW + OP_R_ZKB]
    adc dx, 0                       ; will claim, read in the same run
.r:
%endif
%endif
    ret
dsl_opgo:
    call OSAPI_FS_OP                ; every register kept
%ifdef DOS_WIN16
    jc .r
    inc byte [dsl_opok]
.r:
%endif
    ret
dsl_opend:
%ifdef DOS_WIN16
    mov byte [dsl_opok], 0
%endif
    xor cx, cx
    mov dx, cx
    call OSAPI_FS_OP
    ret

; --- the table, and the standard's own code after it (SPEC.md 20.12.3) ------
%ifndef DOS_MEGA
    OS88_PARTS_BEGIN 8              ; ...the seventh the Setup page's, the
                                    ; eighth the windowed layer's (96.54.9)
%else
    OS88_PARTS_BEGIN 5
%endif
      OS88_PART OP_SEG,   OP_COMP | OP_ZFILL
                                    ; 0 THE DOS: a whole .o88 image, its bss
                                    ;   inside it because the kernel does not
                                    ;   zero a part (20.12.10). OP_COMP is
                                    ;   the whole point of this file, and
                                    ;   OP_ZFILL is what keeps the zeros OFF
                                    ;   it: the core's slot and the bss are
                                    ;   26,109 of these 55,316 bytes, and
                                    ;   op_unpack fills them rather than
                                    ;   decoding them (SPEC.md 96.44.17)
%ifndef DOS_MEGA
      OS88_PART OP_SEG,   OP_COMP | OP_ZFILL
                                    ; 1 THE DOS's WINDOW, the tail of part 0's
                                    ;   own assembly (SPEC.md 96.44.19): the
                                    ;   console, the prompt, the controls, the
                                    ;   paint and their bss. Eager, so the
                                    ;   carve lays it at CS:roundup16(part 0)
                                    ;   - where it was assembled - and reads
                                    ;   both in one run
%endif
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY
                                    ; 1 (2) THE INT 21h CORE, lazy because it does
                                    ;   not belong in the carve - dsl_core
                                    ;   copies it into the hole at CORE_ORG
                                    ;   inside part 0 and drops it again
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY
                                    ; 2 (3) kern_dos. LAZY because op_size would
                                    ;   refuse the pair eagerly (the header
                                    ;   above), and never fetched at all
                                    ;   because the handoff reads it by extent
                                    ;   list with the heap already gone - what
                                    ;   the DOS wants off this row is three
                                    ;   numbers. COMP because it is 29KB of a
                                    ;   360KB system disk otherwise, and the
                                    ;   pairing is what SPEC.md 20.12.7.4 had
                                    ;   to unrefuse
%ifdef DOS_MEGA
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY
                                    ; 3 kern_mega's LOW kern_dos (SPEC.md
                                    ;   96.40.9.2), part 2's twin at 0060:0000
%endif
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY | OP_SELF
                                    ; 4 HELP's text, read by
                                    ;   the re-homed host's op_self when HELP
                                    ; is typed (DOS-PARTS-PLAN 1.1)
%ifndef DOS_MEGA
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY | OP_SELF
                                    ; 5 DOS's NETWORK - the packet driver and
                                    ;   the cable translation - read by the
                                    ;   host's op_self into the network claim
                                    ;   when a run will publish a packet driver
                                    ;   (SPEC.md 96.23.11). kern_mega's has none
; --- the Setup page's part (SPEC.md 96.32.2.3) ------------------------------
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY | OP_SELF
                                    ; 6 the SETUP PAGE's code - its painters,
                                    ;   placers and press handlers and the
                                    ;   .LNK writer - read by the host's
                                    ;   op_self when Setup opens and freed
                                    ;   when it closes. kern_mega's has none:
                                    ;   its page is inline
%endif
; --- DOS_PART_DVW ------------------------------------------------------------
%ifndef DOS_MEGA
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY | OP_SELF
                                    ; 7 DOS's WINDOWED LAYER (SPEC.md 96.54.9),
                                    ;   read by the host's op_self into a claim
                                    ;   of its own at the first Run in Window.
                                    ;   kern_mega's has none
%endif
; --- end DOS_PART_DVW ---------------------------------------------------------
    OS88_PARTS_END

    OS88_BSS OP_BSS + DSL_BSS
    OS88_IMAGE_END

dsl_crow equ os88_image_end + OP_BSS    ; the CORE row, banked before the fetch
%ifndef DOS_WIN16                       ; that spends it
%ifdef DOS_MEGA
DSL_BSS  equ OP_ROW
%else
dsl_head equ dsl_crow + OP_ROW          ; never set without dsl_w16, which is
                                        ; what reads a link: 0, the zeroed bss
DSL_BSS  equ OP_ROW + 1
%endif
%else
dsl_doc  equ dsl_crow + OP_ROW          ; the document's 16-byte locator, as
                                        ; OSAPI_ARG_FILE answered (SPEC.md
                                        ; 21.5.3) - zero when launched empty,
                                        ; the kernel having zeroed this bss
dsl_w16k equ dsl_doc + 16               ; ...1 = read, and it is no NE: the
                                        ; host's wake need not read it again.
                                        ; ADJACENT: the hand-on copies 17
dsl_home equ dsl_w16k + 1               ; the folder we came from: dir, vol
dsl_head equ dsl_home + 3               ; 1 = a link saved Close when done
dsl_opok equ dsl_head + 1               ; 1 = the launch's bar is ours (96.44.5.5)
DSL_BSS  equ OP_ROW + 16 + 1 + 3 + 1 + 1
%endif
