; =============================================================================
; os8088 - apps/arcade/arcade.asm - ARCADE, an arcade-board emulator
; (docs/ARCADE-SPEC.md; the plan is docs/plans/ARCADE-EMU-PLAN.md)
;
; The first board is Midway's 8080 B&W board running Space Invaders. Its CPU
; is not interpreted: apps/arcade/arcaot.py translated the whole ROM to 8086
; code before the package was built, and that code is part 0 of this file.
; The package hosts it: it delivers the board's two interrupts on the PC's
; own clock, reads the keys, and draws what the game wrote to VRAM.
;
; THE GAME RUNS FULL SCREEN, inside OSAPI_FSX_RUN (SPEC.md 53): VGA 640x480
; with the cabinet's colour overlay, Hercules 720x348, or CGA 640x200. The
; window is the launcher and says which keys do what. Leaving with Esc keeps
; the machine where it stood, so Enter goes back into the same game.
;
; THE TIME (ARCADE-SPEC 5): RST 1 and RST 2 alternate every half frame by
; pit_now, not by counting 8080 cycles. Space Invaders keeps its game time in
; those two handlers and spins in its main loop between them, so a host too
; slow for the whole loop still runs the GAME at full speed - it just goes
; round the main loop fewer times a frame (MEASURED: 3.85 times at full
; speed, tools/arcsim.py stats).
; =============================================================================

%include "os88api.inc"
%include "os88mega.inc"                 ; the shipped build refuses under
                                        ;   CPU_F_PM: the code part is run
                                        ;   with a far jump into its segment
%include "arcdefs.inc"

    OS88_HEADER 'ARCADE', ar_entry, OS88_F_ICON | OS88_F_PARTS, OS88_HIGH | OS88_HIGHMEM
                                        ; (on kern_mega the arm's region and
                                        ;   its claims of 64KB or less may be
                                        ;   above 1MB: ARCADE-MEGA-PLAN)

; --- embedded 16x16 icon: an invader -----------------------------------------
    OS88_ICON16
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0x0000, 0x0000, 0x0820, 0x0440, 0x0FE0, 0x1BB0, 0x3FF8, 0x2FE8
    dw 0x2828, 0x06C0, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    OS88_ICON16_END

%ifdef PKG_MEGA
%define OP_WANT_CSEG                ; op_cseg: a board's code part is far-
%endif                              ;   called through its CODE selector
%define OP_ONMOVE ar_partsmv          ; the parts' claims MOVE (SPEC.md
                                        ;   20.12.12): ar_partsmv re-reads them
%include "os88parts.inc"

%ifdef ARC_PACMAN
    OS88_PARTS_BEGIN 15
%else
    OS88_PARTS_BEGIN 13
%endif
      OS88_PART OP_ASSET, OP_ZERO, ARC_VIEW_KB      ; 0 the view, shared
      ; NO ROM IS HERE (docs/plans/ARCADE-RELEASE-PLAN.md 2.1): a translated
      ; board ships its RECIPE - its code with the ROM's bytes as holes and
      ; the list that fills them - and its PC table, which is addresses; the
      ; ROM is the user's <SET>.ROM, read at the pick (ar_romload). EVERY
      ; part is LAZY (ARCADE-SPEC 1.2): read when Game picks its board and
      ; dropped when another is picked
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 1 Space Invaders: recipe
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 2   aux: the PC table
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 3 Space Invaders Part II
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 4
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 5 Lunar Rescue
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 6
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 7 Gun Fight
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 8
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 9 Sea Wolf
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 10
      OS88_PART OP_ASSET, OP_LAZY                   ; 11 Q*bert: the N88 core
%ifdef ARC_PACMAN
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 12 Pac-Man: recipe
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; 13   aux
%endif
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP         ; last: the 8080 boards'
                                                    ;   sounds (arcsfx.py), read
                                                    ;   for a stream only
    OS88_PARTS_END

%ifdef ARC_PACMAN                   ; the parts named in code
AR_P_INVSFX equ 14
%else
AR_P_INVSFX equ 12
%endif
AR_P_NONE   equ 0xFF                ; BD_AUX: the board builds its own

AR_CONT_W   equ 300
AR_CONT_H   equ 101
AR_HALF     equ 10020               ; PIT counts in half a frame: the board's
                                    ;   59.54 Hz (MAME mw8080bw: 4.992 MHz,
                                    ;   320 x 262), two interrupts a frame
AR_RUNLO    equ 2400                ; a run under 2 ms doubles the budget,
AR_RUNHI    equ 7200                ;   over 6 ms halves it (ARCADE-SPEC 5.2):
                                    ;   each run costs the host a PIT read and
                                    ;   the register save, so fewer is faster
AR_BUDMAX   equ 0x4000            ; (weights: roughly 8080 instructions)
AR_CELLS    equ 896                 ; 28 x 32 cells of 8 rows x 1 byte

; A BOARD (ARCADE-SPEC 5.3): what the host loop asks of each. The loop is the
; same for every board; these are the differences.
BD_TITLE    equ 0                   ; dw its name
BD_CODE     equ 2                   ; db its code part, its aux part (or
BD_AUX      equ 3                   ;   AR_P_NONE: BD_BUILD makes ar_aseg)
BD_ROM      equ 4                   ; db (unused: the ROM is a file now)
BD_FLAGS    equ 5                   ; db BDF_*
BDF_HOLD    equ 1                   ; a masked interrupt HOLDS the board's
                                    ;   clock until it is taken (Pac-Man);
                                    ;   else the frames go on and it waits
                                    ;   (ar_pend) for the guest's EI
BD_ROMLEN   equ 6                   ; dw the program bytes the view starts with
BD_RESET    equ 8                   ; dw near: the view's own cells, once copied
BD_SETUP    equ 10                  ; dw near: in the bracket - the mode, the
                                    ;   tables, the first picture. CF=1 refused
BD_IRQ      equ 12                  ; dw near: an interrupt is due - deliver it
                                    ;   (CF=0) or say it is masked (CF=1)
BD_FRAME    equ 14                  ; dw near: a frame's picture, sound, keys
BD_PERIOD   equ 16                  ; dw PIT counts between interrupts
BD_PERFRAME equ 18                  ; db interrupts a frame
BD_LATE     equ 19                  ; db frames behind before one is dropped
                                    ;   (0: two - the boards whose picture
                                    ;   always fits a frame)
                                    ; THE SOUND (ARCADE-SPEC 8), a route each:
BD_SPK      equ 20                  ; dw near: a frame's speaker tone
BD_SBTICK   equ 22                  ; dw near: a frame's sound state, before
                                    ;   the stream is mixed
BD_MIX      equ 24                  ; dw near: WRITE CX samples at ES:DI (0 =
                                    ;   the board has no stream)
BD_SBHZ     equ 26                  ; dw the stream's rate
BD_SBPREP   equ 28                  ; dw near: before the bracket, what the
                                    ;   stream needs (0 = nothing). CF=1 none
BD_FMPAT    equ 30                  ; dw the FM patches: a count, register/value
                                    ;   pairs (0 = the board has no FM)
BD_FMF      equ 32                  ; dw near: a frame's FM channels
BD_ON       equ 34                  ; dw near: its parts just read - what else
                                    ;   it holds while picked. CF=1 refused
BD_OFF      equ 36                  ; dw near: ...given back before they go
BD_X        equ 38                  ; dw an 8080 board's wiring (X_*), or 0
BD_ROMF     equ 40                  ; dw its <SET>.ROM's name (8.3, NUL)
BD_BUILD    equ 42                  ; dw near: the ROM image just read (at
                                    ;   ar_rimg), what the board makes of it
                                    ;   - the recipe filled, its tables - and
                                    ;   ar_rseg/ar_aseg set. CF=1 refused
BD_SIZE     equ 44

; AN 8080 BOARD'S WIRING (ARCADE-SPEC 5.3): Space Invaders and Taito's
; colour boards are one board with different ports, colours and sounds
X_IN0       equ 0                   ; db IN 0, IN 1, IN 2 at rest (IN 1's bit 0
X_IN1       equ 1                   ;   is the coin: a press flips it)
X_IN2       equ 2
X_COLOUR    equ 3                   ; db 1: a colour map PROM at 5000h/5400h of
                                    ;   the ROM part, a colour a cell (VGA)
X_CUES      equ 4                   ; dw the speaker's cues (ar_cues' shape)
X_LOOP      equ 6                   ; dw the bit (OUT 3 low, OUT 5 high) a held
                                    ;   sound loops on
X_CBITS     equ 8                   ; dw the stream's cues (dw bit, db clip)
X_CMASK     equ 10                  ; dw ...the bits in them
X_LCLIP     equ 12                  ; db the clip the held bit loops
X_FSTEP     equ 14                  ; dw FM: struck notes (dw bit, dw Hz)
X_FNOISE    equ 16                  ; dw FM: noises (dw bit, dw Hz, db level,
                                    ;   db frames a step)
X_FTONE     equ 18                  ; dw FM: note sequences (dw bit, dw notes)
X_LAND      equ 13                  ; db 1: a LANDSCAPE board - a VRAM byte is
                                    ;   8 pixels ACROSS, its bit 0 leftmost
                                    ;   (Gun Fight, Sea Wolf: MAME ROT0)
X_KEYS      equ 20                  ; dw near: a frame's held keys into the ports
X_SDEC      equ 22                  ; dw near: AX = the sound latches as the bit
                                    ;   word the sound layers read (OUT 3's bits
                                    ;   low, OUT 5's high, bit 5 the amplifier)
X_LAMPS     equ 24                  ; dw near: a frame's cabinet lamps, or 0

; =============================================================================
; ar_entry - in: DS = ours, ES = KERNEL_SEG. out: BX = window, CF = 0
; =============================================================================
ar_entry:
    OS88_MEGA_REFUSE
    push es
    call op_load                    ; the three parts; a refusal has toasted
    pop es
    jc .out
    mov al, 0
    call op_seg
    mov [ar_vseg], ax
    push es                         ; the view's signature from the start (a
    mov es, ax                      ;   pick's reset writes it again): no game
    mov word [es:V_SIG], 'AR'       ;   is read at open now (ARCADE-SPEC 11.1),
    mov word [es:V_SIG+2], 'CV'     ;   and the rows find the view by it
    pop es
    mov word [ar_bd], 0
    mov word [ar_win], 0            ; (af_look asks: no window yet)
    mov word [ar_rimgc], 0
    mov word [ar_zclaim], 0
    mov word [ar_iclaim], 0
    mov byte [ar_datst], 0
    mov al, OP_CARVE                ; the carve (the view) MOVABLE: ar_partsmv
    call op_movable                 ;   asks for ar_vseg again (AL is the
                                    ;   part: after the signature's ES = AX)
    call OSAPI_FILE_HERE            ; ARCADE's own folder and disk: for
    mov [ar_homedrv], bl            ;   ARCADE.DAT, and to come back to
    mov [ar_homecl], dx             ;   after the chooser has moved us
    call ar_crcinit
    call ar_placesload
    call af_init                    ; the face: its theme, layout and size
    call af_look                    ;   (ARCADE-SPEC 11) - and no game read:
    mov si, ar_tpl                  ;   opening is a LOOK (11.1), made BEFORE
    call OSAPI_WM_CREATE            ;   the region is movable: a file call
    jc .out                         ;   inside the entry may compact the heap
    OS88_REGION_MOVABLE
    OS88_ALTENTER_ARM               ; arms the key-state map OSAPI_KEY_DOWN reads
    mov [ar_win], bx
    mov si, ar_menus
    call OSAPI_MENU_SET
    mov bx, [ar_win]
    mov al, 1                       ; every pixel of the content is ours
    call OSAPI_WM_OWNBG
    call af_vgacheck                ; (the display's modes: the window's own)
    mov bx, [ar_win]
    mov si, ar_about
    call OSAPI_ABOUT_SET
    clc
.out:
    ret
; ar_toast - SI = a NUL line in our segment. Every register kept.
ar_toast:
    push ax
    push cx
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop cx
    pop ax
    ret

; =============================================================================
; ar_select - make BX the board, on the desktop: the board picked before
; given back and its parts dropped, BX's parts read (op_fetch - it may toast),
; its code part's header checked, its own claims made (BD_ON), its segments
; into ar_core, ar_aseg, ar_rseg, and the board powered on. CF=1: BX could
; not be had, a toast said why, and NO board is picked (ar_bd = 0). Every
; register but BX kept.
; =============================================================================
ar_select:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    push bx
    call ar_unselect
    pop bx
    push bx
    mov al, [bx+BD_CODE]
    call op_fetch
    jc .no
    pop bx
    push bx
    mov al, [bx+BD_AUX]
    cmp al, AR_P_NONE
    je .noaux
    call op_fetch
    jc .no
.noaux:
    pop bx
    push bx
    mov al, [bx+BD_CODE]
    call op_seg
    mov es, ax
    cmp word [es:ARC_HDR+AH_MAGIC], ARC_MAGIC
    jne .bad
    call ar_romload                 ; the user's <SET>.ROM (it may toast)
    jc .no
    pop bx
    push bx
    call [bx+BD_ON]
    jc .no
    pop bx
    push bx
    mov ax, [bx+BD_X]
    mov [ar_bx], ax
    mov al, [bx+BD_CODE]
    call op_seg
    mov es, ax
    mov ax, [es:ARC_HDR+AH_ENTER]
    mov [ar_core], ax
%ifdef PKG_MEGA
    mov al, [bx+BD_CODE]            ; the far call's: the part's CODE half,
    call op_cseg                    ;   never a fault per run through its data
%else                               ;   one (ARCADE-MEGA-PLAN M3)
    mov ax, es
%endif
    mov [ar_core+2], ax
    mov al, [bx+BD_AUX]
    cmp al, AR_P_NONE
    je .built
    call op_seg
    mov [ar_aseg], ax
.built:
    mov ax, [ar_rimg]               ; the ROM part IS the image, unless the
    mov [ar_rseg], ax               ;   board makes one of its own
    call [bx+BD_BUILD]
    jnc .made
    call [bx+BD_OFF]
    jmp short .no
.made:
    pop bx
    mov [ar_bd], bx
    mov al, [bx+BD_CODE]            ; picked: its parts MOVABLE now, banked in
    call op_movable                 ;   words ar_partsmv re-reads
    mov al, [bx+BD_AUX]
    cmp al, AR_P_NONE
    je .aux0
    call op_movable
.aux0:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    call ar_reset
    clc
    ret
.bad:
    mov si, ar_s_bad
    call ar_toast
.no:
    pop bx
    call ar_dropparts               ; whatever of BX's had come in
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret

; ar_unselect - the board picked, given back: its own claims (BD_OFF), then
; its parts and its ROM. Nothing picked after. Clobbers AX, BX
ar_unselect:
    mov bx, [ar_bd]
    or bx, bx
    jz .r
    mov word [ar_bd], 0
    call [bx+BD_OFF]
    call ar_dropparts
.r:
    ret

; ar_dropparts - board BX's parts dropped (op_drop: a part not here is a
; no-op) and the ROM image's claim freed. Kept: BX
ar_dropparts:
    mov al, [bx+BD_CODE]
    call op_drop
    mov al, [bx+BD_AUX]
    cmp al, AR_P_NONE
    je ar_freeimg
    call op_drop
    ; fall through
; ar_freeimg - the ROM image's claim, if there is one, given back. Kept: BX
ar_freeimg:
    push dx
    xor dx, dx
    xchg dx, [ar_rimgc]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
.r:
    pop dx
    ret

; =============================================================================
; THE USER'S ROM (ARCADE-SPEC 1.4). ar_romload - board BX's <SET>.ROM read
; whole into a claim of its own, from ARCADE's current folder: probed for its
; size first (OSAPI_FILE_READ with no room answers FERR_BIG and the KB it
; needs, unpacked), then read, then held to its header - 'ARCROM', version
; 1, the set's name, a length - and to the CRC32 the header carries, every
; byte of the image. out CF=0: ar_rimgc the claim, ar_rimg the image's
; segment (the claim + 2: the header is 32 bytes); CF=1: nothing kept, and
; a toast said why unless ar_qsel. Kept: BX
; =============================================================================
ar_romload:
    push bx
    mov [ar_lbd], bx
    call OSAPI_FILE_HERE            ; where ARCADE stands: tried first, and
    mov [ar_rhome], dx              ;   come back to after every place
    mov [ar_rhomed], bl
    call ar_romtry
    jnc .got
    or al, al                       ; wrong, or no memory: said at once
    jnz .say
    mov si, ar_places + 1           ; not here: each place an import wrote to
    mov cl, [ar_places]             ;   (ARCADE.DAT), in turn
    xor ch, ch
.place:
    jcxz .none
    push cx
    push si
    mov bl, [si]
    mov dx, [si+1]
    call OSAPI_FILE_GOTO_QM
    jc .next
    call ar_romtry
    pushf
    push ax
    mov dx, [ar_rhome]
    mov bl, [ar_rhomed]
    call OSAPI_FILE_GOTO_QM
    pop ax
    popf
    pop si
    pop cx
    jnc .got
    or al, al
    jnz .say
    add si, 3
    loop .place
    jmp short .none
.next:
    pop si
    pop cx
    add si, 3
    loop .place
.none:
    xor al, al
.say:
    cmp byte [ar_qsel], 0
    jne .quiet
    mov si, ar_s_romno
    cmp al, 1
    jb .where
    mov si, ar_s_romis
    je .tell
    mov si, ar_s_romram
    jmp short .tell
.where:
    push si                         ; not found: on a floppy it was made on,
    mov si, [ar_lbd]                ;   perhaps - "QBERT.ROM: disk in B:"
    mov si, [si+BD_ROMF]
    call ar_setdrv
    pop si
    jc .tell
    cmp al, 2                       ; (a floppy: a hard disk is always in)
    jae .tell
    add al, 'A'
    mov [ar_s_romdl], al
    mov si, ar_s_romdisk
.tell:
    push si                         ; the file's name, then why
    mov di, ar_msg
    push ds
    pop es
    mov si, [ar_lbd]
    mov si, [si+BD_ROMF]
.cpn:
    lodsb
    stosb
    or al, al
    jnz .cpn
    dec di
    pop si
.cp:
    lodsb
    stosb
    or al, al
    jnz .cp
    mov si, ar_msg
    call ar_toast
.quiet:
    pop bx
    stc
    ret
.got:
    pop bx
    clc
    ret

; ar_romtry - ar_romload's read, in the folder the instance stands in: out
; CF=0 the image is in (ar_rimg, ar_rimgc); CF=1 AL = 0 not here, 1 not the
; set it says, 2 no memory - nothing kept, nothing said
ar_romtry:
    mov bx, [ar_lbd]
    mov si, [bx+BD_ROMF]
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    call OSAPI_FILE_READ            ; the size probe
    jnc .wrong                      ; (an empty file)
    cmp ax, FERR_BIG
    jne .missing
    mov ax, dx                      ; the KB it needs, and one over
    inc ax
    push ax
    call OSAPI_MEM_CLAIM
    pop cx
    jc .nomem
    mov [ar_rimgc], dx
    mov es, dx
    mov ax, cx                      ; DX:CX = the claim in bytes
    mov dx, cx
    mov cl, 6
    shr dx, cl
    mov cl, 10
    shl ax, cl
    mov cx, ax
    xor bx, bx
    mov si, [ar_lbd]
    mov si, [si+BD_ROMF]
    call OSAPI_FILE_READ
    jc .wrongf
    sub ax, 32                      ; DX:AX = the image's bytes, as read
    sbb dx, 0
    jc .wrongf
    cmp word [es:0], 'AR'           ; the header
    jne .wrongf
    cmp word [es:2], 'CR'
    jne .wrongf
    cmp word [es:4], 'OM'
    jne .wrongf
    cmp byte [es:6], 1
    jne .wrongf
    push ax                         ; (the bytes read, past the name)
    mov si, [ar_lbd]                ; the set's name: the file's, to its dot
    mov si, [si+BD_ROMF]
    mov di, 8
    mov cx, 8
.name:
    lodsb
    cmp al, '.'
    jne .ch
    xor al, al
    dec si
.ch:
    cmp al, [es:di]
    jne .wrongn
    inc di
    loop .name
    pop ax
    mov si, [ar_lbd]                ; the length is the SET's (arcsets.inc),
    mov si, [si+BD_ROMF]            ;   not merely the header's: a file whose
    call ar_setfind                 ;   CRC agrees with a short image of its
    jc .wrongf                      ;   own would have the board read past
    mov cx, [bx+13]                 ;   its claim (ar_fill, BD_BUILD)
    cmp cx, [es:16]
    jne .wrongf
    mov cx, [bx+15]
    cmp cx, [es:18]
    jne .wrongf
    mov cx, [es:16]                 ; the length: 1 to 128KB (Q*bert's with
    mov bx, [es:18]                 ;   its sound chips is 68KB), all read
    cmp bx, 1
    ja .wrongf
.len:
    cmp dx, bx
    jb .wrongf
    ja .lenok
    cmp ax, cx
    jb .wrongf
.lenok:
    mov dx, es                      ; the image: two paragraphs in, past the
    mov ax, 2                       ;   header (a selector of its own on
    call ar_sub                     ;   kern_mega: ar_sub)
    mov [ar_rimg], dx
    push word [es:20]               ; the CRC32 the import checked it against
    push word [es:22]
    mov bx, [es:18]
    mov dx, es
    mov ax, 2
    call ar_crcimg                  ; DX:AX over the image, BX:CX bytes
    pop bx
    pop cx
    cmp ax, cx
    jne .wrongf
    cmp dx, bx
    jne .wrongf
    mov dx, [ar_rimgc]              ; read and checked: MOVABLE from here on
    mov ax, ar_rimgmv               ;   (SPEC.md 66.3 rule 5's order)
    call OSAPI_MEM_MOVABLE
    call OSAPI_FILE_HERE            ; (the image is in: where it was, for its
    mov [ar_rfdrv], bl              ;   picture - ARCADE-SPEC 11.4)
    mov [ar_rfcl], dx
    clc
    ret
.wrongn:
    pop ax
.wrongf:
    call ar_freeimg
.wrong:
    mov al, 1
    stc
    ret
.nomem:
    mov al, 2
    stc
    ret
.missing:
    xor al, al
    stc
    ret

; ar_rimgmv - the ROM image's claim moved (SPEC.md 66; never on kern_mega,
; where a move is a rebase): BX = was, DX = now. ar_rimg is two paragraphs
; into it, and ar_rseg IS ar_rimg on a board that builds no ROM part of its
; own; everything else derives from these at each use
ar_rimgmv:
    mov [ar_rimgc], dx
    mov bx, [ar_rimg]
    mov ax, 2
    call ar_sub
    mov [ar_rimg], dx
    cmp [ar_rseg], bx
    jne .r
    mov [ar_rseg], dx
.r:
    ret

; ar_partsmv - OP_ONMOVE (SPEC.md 20.12.12): the carve or a fetched part
; moved, and op_seg answers its new place already. Every segment banked from
; it is asked again: the view, the picked board's code part (op_cseg's code
; half in the arm) and its aux - and Q*bert's yields, which name the N88 part
; inside G. Nothing is far-called under a claim: a run claims nothing
ar_partsmv:
    xor al, al
    call op_seg
    mov [ar_vseg], ax
    mov bx, [ar_bd]
    or bx, bx
    jz .r
    mov al, [bx+BD_CODE]
%ifdef PKG_MEGA
    call op_cseg
%else
    call op_seg
%endif
    mov [ar_core+2], ax
    mov al, [bx+BD_AUX]
    cmp al, AR_P_NONE
    je .noaux
    call op_seg
    mov [ar_aseg], ax
.noaux:
    cmp bx, ar_bd_qb
    jne .r
    jmp qb_yields                   ; (the N88 part's new segment, in G)
.r:
    ret

; ar_crc32 - DX:AX = the CRC-32 (zlib's) of CX bytes at DS:SI, CX = 0 being
; 65,536. The table is ar_crctab, made once by ar_crcinit. Clobbers BX, CX, SI
ar_crc32:
    mov ax, 0xFFFF
    mov dx, ax
    call ar_crc32r
    not ax
    not dx
    ret

; ar_crcimg - DX:AX = the CRC-32 (zlib's) of an image of BX:CX bytes (at most
; 128KB) at paragraph AX of the claim DX, 64KB at a time through ar_sub.
; Clobbers BX, CX, SI
ar_crcimg:
    push ds
    push dx                         ; (the claim and the image's paragraph,
    push ax                         ;   for the second 64KB)
    call ar_sub                     ; DS = the image's first 64KB
    mov ds, dx
    xor si, si
    mov ax, 0xFFFF
    mov dx, ax
    or bx, bx
    jz .one
    push cx                         ; the first 64KB, then the rest
    xor cx, cx
    call ar_crc32r
    pop cx
    pop bx                          ; (the paragraph)
    pop si                          ; (the claim)
    push dx
    push ax
    mov dx, si
    lea ax, [bx+0x1000]
    call ar_sub                     ; DS = past its first 64KB
    mov ds, dx
    pop ax
    pop dx
    xor si, si
    jcxz .done
    call ar_crc32r
    jmp short .done
.one:
    add sp, 4
    call ar_crc32r
.done:
    not ax
    not dx
    pop ds
    ret

; ar_sub - DX = a claim of ours (or our own segment), AX = a paragraph offset
; into it: DX = a segment based there, for as long as the claim lives
; (docs/plans/ARCADE-MEGA-PLAN.md M1). The one door every LASTING derived
; segment goes through: on kern_mega a claim is an LDT selector and the sum
; names another descriptor, so the arm asks OSAPI_SEG SEG_SUB, whose window
; moves and goes with its claim (asked again, the same selector; every
; real-mode kernel answers the sum). Refused (no LDT entry), DX is kept: the
; damage stays in our own claim, never a sum on a selector (SPEC.md
; 50.6.11). The shipped build is the sum. Kept: all else
ar_sub:
%ifdef PKG_MEGA
    push ax
    push bx
    push cx
    mov bl, SEG_SUB
    xor cx, cx                      ; (to its end, 64KB at most)
    call OSAPI_SEG
    pop cx
    pop bx
    pop ax
%else
    add dx, ax
%endif
    ret

; ar_at - ar_sub for a WALK: DX = a claim, AX = a paragraph offset, DI = the
; word holding this walk's window (0 before the first): DX = the window aimed
; there - on kern_mega OSAPI_SEG SEG_AT, ONE descriptor re-aimed per step
; however far the walk goes (a 448KB zip a chip at a time); refused there
; (CF=1) on a real-mode kernel, where the sum is ours. Valid until the next
; ar_at on the same word, and over a claim that does not move meanwhile.
; Kept: all else
ar_at:
%ifdef PKG_MEGA
    push ax
    push bx
    push cx
    push dx
    mov bl, SEG_AT
    mov cx, [di]
    call OSAPI_SEG
    jc .sum
    or dx, dx                       ; (no LDT entry free: the claim kept)
    jz .keep
    mov [di], dx
    add sp, 2
    pop cx
    pop bx
    pop ax
    ret
.keep:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.sum:
    pop dx
    pop cx
    pop bx
    pop ax
%endif
    add dx, ax
    ret

; ar_crc32r - the CRC's register DX:AX run over CX bytes at DS:SI (0 =
; 65,536): no first or last inversion. Clobbers BX, CX, SI
ar_crc32r:
.b:
    mov bl, [si]
    inc si
    xor bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov al, ah                      ; crc >> 8
    mov ah, dl
    mov dl, dh
    xor dh, dh
    xor ax, [cs:ar_crctab+bx]
    xor dx, [cs:ar_crctab+bx+2]
    loop .b
    ret

; ar_crcinit - the table: entry n is n run 8 times through the polynomial
; EDB88320h (reflected)
ar_crcinit:
    xor bx, bx
    mov di, ar_crctab
.n:
    mov ax, bx
    xor dx, dx
    mov cx, 8
.k:
    shr dx, 1
    rcr ax, 1
    jnc .z
    xor ax, 0x8320
    xor dx, 0xEDB8
.z:
    loop .k
    mov [di], ax
    mov [di+2], dx
    add di, 4
    inc bx
    cmp bx, 256
    jb .n
    ret

; ar_fill - the code part's RECIPE filled (docs/plans/ARCADE-RELEASE-PLAN.md
; 2.1; arcaot.py's encode_fill): at AH_END a count, then each hole as the step
; from the one before (offset +1..255, address -128..127), or 0 and both as
; words; each takes the ROM image's byte at its address. A translated
; board's BD_BUILD. BX = the board. CF=0. Kept: BX
ar_fill:
    push bx
    mov al, [bx+BD_CODE]
    call op_seg
    mov es, ax
    mov si, [es:ARC_HDR+AH_END]
    mov cx, [es:si]
    add si, 2
    xor di, di                      ; DI = the hole, DX = its ROM address
    xor dx, dx
    push ds
    mov ds, [ar_rimg]
    jcxz .done
.e:
    mov al, [es:si]
    or al, al
    jnz .step
    mov di, [es:si+1]
    mov dx, [es:si+3]
    add si, 5
    jmp short .put
.step:
    xor ah, ah
    add di, ax
    mov al, [es:si+1]
    cbw
    add dx, ax
    add si, 2
.put:
    mov bx, dx
    mov al, [bx]
    mov [es:di], al
    loop .e
.done:
    pop ds
    pop bx
    clc
    ret

; ar_nop - a board hook with nothing to do (CF=0)
ar_nop:
    clc
    ret

; =============================================================================
; ar_reset - power the board on: the view cleared, the program copied in from
; the ROM part, the guest at 0000 with interrupts off, the write list empty,
; and then the board's own cells (BD_RESET). Every register kept.
; =============================================================================
ar_reset:
    cmp word [ar_bd], 0
    jne ar_reset1
    ret
ar_reset1:
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    push es
    mov bx, [ar_bd]
    mov cx, [bx+BD_ROMLEN]
    mov es, [ar_vseg]
    xor di, di
    push cx
    mov cx, (ARC_VIEW_KB * 1024) / 2
    xor ax, ax
    cld
    rep stosw
    pop cx
    mov ds, [ar_rseg]
    xor si, si
    xor di, di
    rep movsb
    pop es
    pop ds
    push es
    mov es, [ar_vseg]
    mov word [es:ST_AF], 0x0200     ; F: bit 1 is always 1 on an 8080 (and
                                    ;   on a Z80 it is N, which is not kept)
    mov word [es:V_SIG], 'AR'
    mov word [es:V_SIG+2], 'CV'
    cmp byte [bx+BD_AUX], AR_P_NONE ; a translated board's write list, in
    je .nolist                      ;   its AUX part: Q*bert's ar_aseg is its
    mov es, [ar_aseg]               ;   40KB sprite part, C000h past its end
    mov word [es:AUX_LPTR], AUX_LIST ;  (kern_mega's limit faulted the store;
.nolist:                            ;   real mode wrote the heap beyond)
    pop es
    mov byte [ar_halted], 0
    call [bx+BD_RESET]
    call ar_dsapply                 ; the game's switches (12.3)
    mov word [ar_hsfr], 0           ; ...and its best to come back (12.4)
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; inv_reset - Space Invaders' cells: ES = the view
inv_reset:
    push es
    push si
    mov es, [ar_vseg]
    mov word [es:ST_SP], 0x2400     ; (the ROM sets its own at once)
    mov si, [ar_bx]                 ; the ports at rest: the DIPs at their
    mov al, [si+X_IN0]              ;   defaults (3 bases, coin info shown)
    mov [es:V_IN0], al
    mov al, [si+X_IN1]
    mov [es:V_IN1], al
    mov al, [si+X_IN2]
    mov [es:V_IN2], al
    mov byte [ar_cstate], 0xFF      ; the colours to be decided afresh
    mov word [gf_aim], 3            ; the levers at rest: the guns level,
    mov word [gf_aim+2], 3          ;   the periscope ahead
    mov word [sw_scope], 15
    pop si
    pop es
    ret

; =============================================================================
; The window: the launcher
; =============================================================================
ar_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call af_paint                   ; the cabinet (arcface.inc)
    pop bp
    pop di
    pop si
    push si
    cmp byte [ar_abon], 0
    je .out
    mov bx, si
    mov si, ar_ablines
    call os88ui_about_d
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ar_onkey:                           ; AX = the key, SI = the window
    push si
    cmp byte [af_setm], 0           ; the card is SETTINGS: its keys first
    je .list
    push ax
    call af_setkey
    pop ax
    jnc .out
.list:
    cmp al, 13
    je .play
    cmp al, ' '
    je .play
    call af_key
    jmp short .out
.play:
    call af_play
.out:
    pop si
    ret

ar_onclick:
    push si
    call ar_abdismiss
    jc .out
    call af_click
.out:
    pop si
    ret

ar_oncmd:                           ; Game: Play (0), Reset (1), Add ROM
    push si                         ;   sets... (2), and a game each after
    push ax
    call ar_abdismiss
    pop ax
    cmp al, 1
    jb .play
    jne .more
    call ar_reset
    jmp short .out
.more:
    cmp al, 2
    jne .board
    call ar_import
    jmp short .out
.board:
    sub al, 3                       ; the game: selected in the list, and
    mov [af_sel], al                ;   played
    mov si, [ar_win]
    call ar_repaint
.play:
    call af_play
.out:
    pop si
    ret

ar_about:
    mov byte [ar_abon], 1
    mov bx, si
    mov si, ar_ablines
    jmp os88ui_about

ar_abdismiss:
    mov bx, ar_abon
    mov ax, ar_repaint
    jmp os88ui_abdismiss

ar_repaint:                         ; SI = the window: the whole face
    call ar_paint
    ret

; =============================================================================
; ar_play - into the full-screen bracket, and the toast it brings back
; =============================================================================
ar_play:
    mov word [ar_err], 0
    cmp word [ar_bd], 0             ; no board in memory (a pick that could
    jne .board                      ;   not be read, and nor could the last)
    mov si, ar_s_nobd
    jmp ar_toast
.board:
    call ar_snd_prep                ; on the desktop: a part read, a toast
    mov byte [af_snapw], 0
    mov byte [af_dsdirty], 0
    call ar_ovinit                  ; (the font, for the box over the game)
    mov ax, ar_fsx
    mov bx, [ar_win]
    xor cx, cx
    call OSAPI_FSX_RUN
    jnc .ran
    mov si, ar_s_nofsx
    jmp ar_toast
.ran:
    call af_snapsave                ; (on the desktop: a file may be written)
    cmp byte [af_dsdirty], 0        ; a best kept: ARCADE.DAT
    je .kept
    mov byte [af_dsdirty], 0
    call ar_datwrite
.kept:
    mov si, [ar_err]
    or si, si
    jz .out
    jmp ar_toast
.out:
    ret

; ar_fsx - the bracket's proc: SI = our window, DS = ours, ES = KERNEL_SEG
ar_fsx:
    mov bx, [ar_bd]
    call [bx+BD_SETUP]
    jc .out
    call ar_snd_open
    call ar_loop
    call ar_snd_close
    call ar_hssave                  ; its best, kept (12.4)
    call af_snap                    ; the game's picture, if it has none yet
.out:
    ret

; inv_setup - Space Invaders' picture: VGA, Hercules or CGA. SI = the window
inv_setup:
    mov bx, si
    call OSAPI_FSX_CAPS
    mov dl, FSXM_VGA12
    test ax, 1 << FSXM_VGA12
    jnz .mode
    mov dl, FSXM_HERC
    test ax, 1 << FSXM_HERC
    jnz .mode
    mov dl, FSXM_CGA640
    test ax, 1 << FSXM_CGA640
    jnz .mode
    mov word [ar_err], ar_s_nomode
    stc
    ret
.mode:
    mov al, dl
    push es
    push ds
    pop es
    mov di, ar_fsi
    call OSAPI_FSX_MODE
    pop es
    jnc .set
    mov word [ar_err], ar_s_nomode
    ret
.set:
    call ar_vinit
    call ar_dirtyall
    call ar_render
    clc
    ret

; =============================================================================
; ar_loop - run the board until Esc (ARCADE-SPEC 5)
; =============================================================================
ar_loop:
    mov byte [ar_quit], 0
    call ar_door                    ; nothing held or typed comes in (12.1)
    mov bx, [ar_bd]
    mov ax, [bx+BD_PERIOD]
    mov [ar_period], ax
    mov cl, [bx+BD_PERFRAME]        ; two FRAMES behind: drop, never race
    shl cl, 1                       ;   (Invaders' period is half a frame) -
    cmp byte [bx+BD_LATE], 0        ;   or the board's own allowance, in
    je .late                        ;   frames (BD_LATE)
    mov al, [bx+BD_LATE]
    mul byte [bx+BD_PERFRAME]
    mov cl, al
    mov ax, [bx+BD_PERIOD]
.late:
    mov ch, 0
    mul cx
    mov [ar_late], ax               ; (32 bits: Q*bert's is half a second)
    mov [ar_late+2], dx
    call pit_now
    add ax, [ar_period]
    adc dx, 0
    mov [ar_due], ax
    mov [ar_due+2], dx
    mov byte [ar_which], 1
    mov byte [ar_irqn], 0
    mov byte [ar_pend], 0
    mov word [ar_budget], 128
    call pit_now                    ; a run is timed from the end of the one
    mov [ar_t0], ax                 ;   before it: one PIT read a run
    mov [ar_t0+2], dx
.run:
    cmp byte [ar_halted], 0         ; HLT waits for the interrupt
    jne .clock
    mov es, [ar_vseg]
    mov ax, [ar_budget]
    mov [es:ST_BUD], ax
    push ds
    mov ax, es
    mov dx, [ar_aseg]
    call far [ar_core]
    pop ds
    sti
    mov es, [ar_vseg]
    inc word [es:V_NRUN]
    mov ax, [ar_budget]
    mov [es:V_NBUD], ax
    mov al, [es:ST_WHY]
    cmp al, ARCX_MISS
    je .miss
    cmp al, ARCX_HALT
    jne .budget
    mov byte [ar_halted], 1         ; a run a HLT (or a spin loop) ended says
    mov es, [ar_vseg]               ;   no more than an EI's about the budget
    mov byte [es:ST_EIX], 0         ;   (ARCADE-SPEC 3.7): not adapted
    jmp .clock
.budget:
    mov es, [ar_vseg]               ; a run EI cut short says nothing about
    mov al, 0                       ;   how long a budget takes
    xchg al, [es:ST_EIX]
    or al, al
    jnz .clock
    call pit_now                    ; the budget follows the run's length,
    push ax                         ;   in 32 bits: a run past 55 ms wraps a
    push dx                         ;   word to a small, plausible number
    sub ax, [ar_t0]
    sbb dx, [ar_t0+2]
    jnz .long
    cmp ax, AR_RUNLO
    jae .notshort
    cmp word [ar_budget], AR_BUDMAX
    jae .adapted
    shl word [ar_budget], 1
    jmp short .adapted
.notshort:
    cmp ax, AR_RUNHI
    jbe .adapted
.long:
    cmp word [ar_budget], 2
    jbe .adapted
    shr word [ar_budget], 1
.adapted:
    pop dx
    pop ax
    mov [ar_t0], ax
    mov [ar_t0+2], dx
    jmp short .due
.clock:
    call pit_now
    mov [ar_t0], ax
    mov [ar_t0+2], dx
.due:
    cmp byte [ar_pend], 0           ; an interrupt the guest had masked: in
    je .nopend                      ;   the moment it can be - its EI ended
    push ax                         ;   the run (ST_PEND) - and not at the
    push dx                         ;   next period (ARCADE-SPEC 5.1)
    mov bx, [ar_bd]
    call [bx+BD_IRQ]
    pop dx
    pop ax
    jc .nopend
    mov byte [ar_pend], 0
    mov byte [ar_halted], 0
.nopend:
                                    ; now - due, signed
    sub ax, [ar_due]
    sbb dx, [ar_due+2]
    js .run
    cmp dx, [ar_late+2]             ; behind: catch up by dropping, never
    ja .resync                      ;   by racing
    jb .deliver
    cmp ax, [ar_late]
    jb .deliver
.resync:
    mov es, [ar_vseg]
    inc word [es:V_NDROP]
    call pit_now
    mov [ar_due], ax
    mov [ar_due+2], dx
.deliver:
    mov bx, [ar_bd]                 ; the board's interrupt: in, or masked
    call [bx+BD_IRQ]                ;   and waiting (the EI that unmasks it
    mov byte [ar_halted], 0         ;   ends a run, ST_PEND): then .due
    jnc .taken                      ;   delivers it, while the frame's clock
    mov bx, [ar_bd]                 ;   goes on - or, BDF_HOLD, with the
    test byte [bx+BD_FLAGS], BDF_HOLD   ; clock held till it is taken: Pac-
    jnz .run                        ;   Man's start-up masks it through a
    mov byte [ar_pend], 1           ;   RAM test that rewrites the screen,
.taken:                             ;   and a frame drawn each period there
                                    ;   crawled at 3 a second. Held for the
                                    ;   8080 boards, Gun Fight and Sea Wolf
                                    ;   ran at 40 (ARCADE-SPEC 5.1)
    mov ax, [ar_period]
    add [ar_due], ax
    adc word [ar_due+2], 0
    inc byte [ar_irqn]
    mov bx, [ar_bd]
    mov al, [ar_irqn]
    cmp al, [bx+BD_PERFRAME]        ; the frame's last: picture, sound, keys
    jb .run
    mov byte [ar_irqn], 0
    mov es, [ar_vseg]
    inc word [es:V_NFRAME]
    call ar_typed
    cmp byte [ar_hold], 0           ; P or F1: held (12.2), and the clock
    je .nohold                      ;   taken up again from now after
    call ar_holdit
    call pit_now
    add ax, [ar_period]
    adc dx, 0
    mov [ar_due], ax
    mov [ar_due+2], dx
.nohold:
    call ar_hsput                   ; a kept best put back (12.4)
    mov bx, [ar_bd]
    call [bx+BD_FRAME]
    call ar_snd_frame
    cmp byte [ar_quit], 0
    je .run
    jmp ar_quiet
.miss:
    mov ax, [es:ST_PC]
    mov di, ar_s_miss + 19
    call ar_hex4
    mov word [ar_err], ar_s_miss
    jmp ar_quiet

; inv_irq - Space Invaders' two interrupts, RST 1 and RST 2 in turn: CF=1
; while the 8080 has them masked
inv_irq:
    mov es, [ar_vseg]
    mov byte [es:ST_PEND], 1
    cmp byte [es:ST_INTE], 0
    je .masked
    mov byte [es:ST_INTE], 0
    mov byte [es:ST_PEND], 0
    mov bx, [es:ST_SP]              ; RST n: the PC onto the 8080's stack
    sub bx, 2
    mov [es:ST_SP], bx
    mov ax, [es:ST_PC]
    mov [es:bx], ax
    mov al, [ar_which]
    mov ah, 0
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [es:ST_PC], ax
    xor byte [ar_which], 3
    clc
    ret
.masked:
    stc
    ret

; inv_frame - after RST 2: the picture, the sound, the controls
inv_frame:
    cmp byte [ar_cmode], 0
    je .draw
    mov es, [ar_vseg]               ; a colour board: OUT 5 bit 5 picks the
    mov al, [es:V_SND5]             ;   colour map, OUT 3 bit 2 turns the
    and al, 0x20                    ;   whole screen red; a change redraws
    mov ah, [es:V_SND3]             ;   every cell (MAME 8080bw)
    and ah, 0x04
    or al, ah
    cmp al, [ar_cstate]
    je .draw
    mov [ar_cstate], al
    mov [ar_cred], ah
    mov word [ar_cbase], 0x5000
    test al, 0x20
    jz .map
    mov word [ar_cbase], 0x5400
.map:
    call ar_dirtyall
.draw:
    call ar_render
    call ar_lamps
    mov si, [ar_bx]                 ; the held keys, the board's way
    jmp [si+X_KEYS]

; ar_hex4 - AX as four hex digits at DS:DI
ar_hex4:
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .w
    add al, 7
.w:
    mov [di], al
    inc di
    pop ax
    loop .d
    ret

; =============================================================================
; ar_typed - once a frame, every board: what was TYPED. Esc leaves; a coin
; or a start typed between two frames is still seen - it holds its logical
; bit (AR_COIN, AR_START1, AR_START2) in ar_pbits for AR_PULSE frames - and
; nothing typed here reaches the desktop afterwards.
; =============================================================================
AR_COIN     equ 1
AR_START1   equ 2
AR_START2   equ 4
AR_PULSE    equ 5                   ; frames a typed coin or start is held

ar_typed:
    cmp byte [ar_pleft], 0
    je .drain
    dec byte [ar_pleft]
    jnz .drain
    mov byte [ar_pbits], 0
.drain:
    mov ah, 1
    int 0x16
    jz .out
    xor ah, ah
    int 0x16
    cmp al, 27
    jne .notesc
    mov byte [ar_quit], 1
.notesc:
    or al, al                       ; F1: the card over the picture, held
    jnz .asc                        ;   (ARCADE-SPEC 12.2)
    cmp ah, 0x3B
    jne .drain
    mov byte [ar_hold], 2
    jmp .drain
.asc:
    or al, 0x20                     ; (letters to lower case)
    cmp al, 'p'                     ; P: held
    jne .notp
    cmp byte [ar_hold], 0
    jne .drain
    mov byte [ar_hold], 1
    jmp .drain
.notp:
    mov si, ar_typemap
.t:
    mov cx, [si]
    or cx, cx
    jz .drain
    add si, 2
    cmp al, cl
    jne .t
    or [ar_pbits], ch
    mov byte [ar_pleft], AR_PULSE
    jmp .drain
.out:
    ret

ar_typemap:                         ; a character typed, its logical bit
    db '5', AR_COIN
    db 'c', AR_COIN
    db '1', AR_START1
    db '2', AR_START2
    dw 0

; =============================================================================
; ar_keys - Space Invaders' controls into IN 1 and IN 2, once a frame
; =============================================================================
ar_keys8080:
    mov si, [ar_bx]
    mov bl, [si+X_IN1]              ; IN 1 at rest
    mov al, [ar_pbits]
    test al, AR_COIN
    jz .nocoin
    xor bl, 0x01                    ; the coin: active high on Space
.nocoin:                            ;   Invaders as we run it, low on Taito's
    test al, AR_START1
    jz .no1
    or bl, 0x04
.no1:
    test al, AR_START2
    jz .no2
    or bl, 0x02
.no2:
    mov si, ar_keymap
.k:
    lodsw
    or al, al
    jz .done
    call OSAPI_KEY_DOWN
    jnc .k
    or bl, ah
    jmp .k
.done:
    mov es, [ar_vseg]
    mov [es:V_IN1], bl
    mov al, bl                      ; IN 2: one player's controls serve both
    and al, 0x70                    ;   (an upright cabinet's are shared)
    mov ah, [es:V_IN2]
    and ah, 0x8F
    or al, ah
    mov [es:V_IN2], al
    ret

ar_keymap:                          ; scancode, IN 1 bit: the HELD keys,
    db KSC_SPACE, 0x10              ;   asked by level once a frame. Each is
    db KSC_LEFT, 0x20               ;   a far call, so only these: a coin, a
    db KSC_RIGHT, 0x40              ;   start and Esc are TYPED (ar_typed),
    db 0, 0                         ;   and nine calls a frame were 9% of an
                                    ;   8088 in play with the IRQs (MEASURED)

; =============================================================================
; THE PICTURE (ARCADE-SPEC 4)
;
; The 8080 writes VRAM as 224 rows of 32 bytes; the cabinet's monitor is
; turned, so a byte is 8 pixels down one screen COLUMN. A cell is 8 rows of
; one byte: turning it is an 8x8 bit transpose, and gives 8 screen bytes, one
; per screen row, each doubled across to 16 pixels for the aspect. ar_ytab
; says where each of the 256 screen rows goes on this adapter.
; =============================================================================

; ar_vinit - the row table, the doubling table and, on VGA, the overlay
ar_vinit:
    push ds
    pop es
    call ar_dblinit
    mov ax, [ar_fsi+FSI_SEG]
    mov [ar_vidseg], ax
    mov word [ar_celf], ar_cell
    mov si, [ar_bx]
    cmp byte [si+X_LAND], 0
    je .turned
    jmp ar_vinitl                   ; Gun Fight, Sea Wolf (arcland.inc)
.turned:
    xor bx, bx                      ; BX = Y
    mov di, ar_ytab
.row:
    mov al, [ar_fsi+FSI_MODE]
    cmp al, FSXM_VGA12
    je .vga
    cmp al, FSXM_HERC
    je .herc
    mov ax, bx                      ; CGA: 256 rows into 200, merged inside
    mov cl, 3                       ;   a cell so no two cells share a row
    shr ax, cl                      ; AX = q
    mov dx, ax
    mov cl, 6
    mul cl                          ; q x 6
    mov cx, dx
    shr cx, 1
    shr cx, 1
    add ax, cx                      ; + the 7-row cells before q
    mov si, bx
    and si, 7
    and dl, 3
    cmp dl, 3
    jne .six
    add si, 8
.six:
    add al, [ar_cgamap+si]
    adc ah, 0
    mov dx, ax
    shr ax, 1
    mov cl, 80
    mul cl
    test dl, 1
    jz .cgaeven
    add ax, 0x2000
.cgaeven:
    add ax, 12
    stosw
    mov ax, 0xFFFF
    stosw
    jmp .next
.herc:
    mov ax, bx
    add ax, 46
    mov dx, ax
    shr ax, 1
    shr ax, 1
    mov cl, 90
    mul cl
    and dx, 3
    mov cl, 13
    shl dx, cl
    add ax, dx
    add ax, 17
    stosw
    mov ax, 0xFFFF
    stosw
    jmp .next
.vga:
    mov ax, bx                      ; 8 rows into 15: seven doubled, one not
    mov cl, 3
    shr ax, cl
    mov cl, 15
    mul cl
    mov si, bx
    and si, 7
    shl si, 1
    add ax, si
    mov cx, 80
    mul cx
    add ax, 12
    stosw
    cmp si, 14
    je .single
    add ax, 80
    stosw
    jmp .next
.single:
    mov ax, 0xFFFF
    stosw
.next:
    inc bx
    cmp bx, 256
    jb .row
    mov word [ar_put], ar_put_cga
    mov al, [ar_fsi+FSI_MODE]
    cmp al, FSXM_HERC
    jne .notherc
    mov word [ar_put], ar_put_herc
.notherc:
    mov byte [ar_cmode], 0
    cmp al, FSXM_VGA12
    jne .out
    mov word [ar_put], ar_put_vga
    mov si, [ar_bx]
    cmp byte [si+X_COLOUR], 0
    je ar_overlay
    jmp ar_colours                  ; a colour board: no cellophane
.out:
    ret

; ar_colours - a Taito colour board on VGA (ARCADE-SPEC 4.3): the DAC's
; first eight entries the board's 3-bit RBG colours (bit 0 red, 1 blue,
; 2 green), all three planes written, and set/reset left to zero the planes
; a cell's colour has not got - one write a byte, as on Space Invaders
ar_colours:
    xor bx, bx
.c:
    mov dx, 0x3C8                   ; pixel value BL's DAC register, through
    mov al, [ar_acmap+bx]           ;   mode 12h's attribute palette (6 is
    out dx, al                      ;   14h there, EGA's brown)
    inc dx
    mov al, 0                       ; red
    test bl, 1
    jz .r
    mov al, 63
.r:
    out dx, al
    mov al, 0                       ; green
    test bl, 4
    jz .g
    mov al, 63
.g:
    out dx, al
    mov al, 0                       ; blue
    test bl, 2
    jz .b
    mov al, 63
.b:
    out dx, al
    inc bx
    cmp bx, 8
    jb .c
    mov dx, 0x3C4
    mov ax, 0x0702                  ; the map mask: planes 0-2
    out dx, ax
    mov dx, 0x3CE
    mov ax, 0x0000                  ; set/reset's value: 0
    out dx, ax
    mov ax, 0x0701                  ; ...enabled on all three until a cell
    out dx, ax                      ;   says its colour
    mov byte [ar_cesr], 7
    mov byte [ar_cmode], 1
    ret

; ar_dblinit - the doubling table: a byte's 8 pixels as 16, left byte first
ar_dblinit:
    xor bx, bx
.dbl:
    mov al, bl
    call ar_dbl4hi
    mov dl, ah
    mov al, bl
    mov cl, 4
    shl al, cl
    call ar_dbl4hi
    mov dh, ah
    mov si, bx
    shl si, 1
    mov [ar_dbl+si], dx
    inc bl
    jnz .dbl
    xor bx, bx                      ; ...and ar_dblr: bit 0 leftmost (the
.rev:                               ;   landscape boards'), the byte reversed
    mov al, bl
    mov cx, 8
.rb:
    shr al, 1
    rcl ah, 1
    loop .rb
    mov al, ah
    mov ah, 0
    mov si, ax
    shl si, 1
    mov dx, [ar_dbl+si]
    mov si, bx
    shl si, 1
    mov [ar_dblr+si], dx
    inc bl
    jnz .rev
    ret

; ar_dbl4hi - AH = AL's top four pixels, each doubled
ar_dbl4hi:
    mov cx, 4
    xor ah, ah
.b:
    shl al, 1
    pushf
    rcl ah, 1
    popf
    rcl ah, 1
    loop .b
    ret

; ar_overlay - the cabinet's cellophane, in planes 1 and 2, once: the game
; writes plane 0 only and the DAC colours each band (ARCADE-SPEC 4.3)
ar_overlay:
    mov dx, 0x3C8
    xor al, al
    out dx, al
    inc dx
    mov si, ar_pal
    mov cx, 6 * 3
.pal:
    lodsb
    out dx, al
    loop .pal
    mov es, [ar_vidseg]
    mov si, ar_bands
.band:
    lodsb                           ; plane mask, 0 = done
    or al, al
    jz .done
    mov ah, al
    mov dx, 0x3C4
    mov al, 2
    out dx, ax
    lodsw
    mov bx, ax                      ; first Y
    lodsw
    mov bp, ax                      ; last Y + 1
    lodsw
    mov [ar_band], ax               ; first byte, count
.y:
    push si
    mov si, bx
    shl si, 1
    shl si, 1
    mov di, [ar_ytab+si]
    call .fill
    mov di, [ar_ytab+si+2]
    cmp di, 0xFFFF
    je .ny
    call .fill
.ny:
    pop si
    inc bx
    cmp bx, bp
    jb .y
    jmp .band
.fill:
    mov al, [ar_band]
    mov ah, 0
    add di, ax
    mov cl, [ar_band+1]
    mov ch, 0
    mov al, 0xFF
    rep stosb
    ret
.done:
    mov dx, 0x3C4                   ; the game's plane from here on
    mov ax, 0x0102
    out dx, ax
    ret

ar_pal:     db 0,0,0, 63,63,63, 0,0,0, 63,16,16, 0,0,0, 16,63,16
; a pixel value's DAC register in mode 12h: the BIOS's attribute palette,
; EGA's, which sends 6 to 14h and 8-15 to 38h-3Fh
ar_acmap:   db 0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
ar_bands:   db 2
            dw 32, 64
            db 0, 56                ; red over the saucer's rows
            db 4
            dw 184, 240
            db 0, 56                ; green over the shields and the base
            db 4
            dw 240, 256
            db 4, 30                ; ...and over the spare bases, not the
            db 0                    ;   credits

ar_cgamap:  db 0,1,1,2,3,4,4,5, 0,1,2,3,3,4,5,6

; ar_dirtyall - every cell onto the write list, as if each had just changed:
; a fresh screen after a mode set. Its flags set, so the core adds none twice
ar_dirtyall:
    mov es, [ar_aseg]
    cld
    mov di, AUX_LIST
    mov ax, 0x2400
.band:
    mov cx, 32
.col:
    stosw
    mov bx, ax
    mov byte [es:bx+AUX_FLAGS], 1
    inc ax
    loop .col
    add ax, 0x100 - 32
    cmp ah, 0x40
    jb .band
    mov [es:AUX_LPTR], di
    ret

; =============================================================================
; ar_render - draw every cell on the write list, once
;
; The core lists a cell the first time a store CHANGES one of its bytes (the
; translated store compares first: 79% of VRAM stores in play change nothing,
; tools/arcsim.py), and its flag - the byte at the key's own offset in aux -
; keeps it off the list again until it is drawn here. So the list is the
; cells that changed, each once, at most 896 of them.
; =============================================================================
ar_render:
    mov si, AUX_LIST
.cells:
    mov es, [ar_aseg]
    cmp si, [es:AUX_LPTR]
    jae .drawn
    mov bx, [es:si]
    mov byte [es:bx+AUX_FLAGS], 0
    push si
    mov es, [ar_vseg]
    inc word [es:V_NCELL]
    call [ar_celf]
    pop si
    add si, 2
    jmp .cells
.drawn:
    mov word [es:AUX_LPTR], AUX_LIST
    ret

; ar_cell - draw the cell whose key is BX: its first byte's 8080 address,
; 2400h + band x 256 + column. Clobbers all but DS.
ar_cell:
    cmp byte [ar_cmode], 0
    je .mono
    mov al, bh                      ; a colour board: the cell's colour, from
    sub al, 0x20                    ;   the map at (H - 20h) x 32 + column:
                                    ;   MAME indexes it from RAM's start, and
                                    ;   2000-23FFh is never shown
    mov ah, 0
    mov cl, 5
    shl ax, cl
    or al, bl
    add ax, [ar_cbase]
    push ds
    mov si, ax
    mov ds, [ar_rseg]
    mov al, [si]
    pop ds
    and al, 7
    cmp byte [ar_cred], 0           ; the screen turned red: every cell
    je .col
    mov al, 1
.col:
    not al                          ; set/reset ZEROES the planes the colour
    and al, 7                       ;   has not got
    cmp al, [ar_cesr]
    je .mono
    mov [ar_cesr], al
    mov ah, al
    mov al, 1
    mov dx, 0x3CE
    out dx, ax
.mono:
    mov si, bx
    mov al, bh
    sub al, 0x24
    mov ah, 0
    shl ax, 1
    mov bp, ax                      ; two screen bytes per cell across
    mov al, bl                      ; the top screen row: Y = 248 - 8c
    mov [ar_ccol], al
    mov ah, 0
    mov cl, 3
    shl ax, cl
    neg ax
    add ax, 248
    shl ax, 1
    shl ax, 1
    add ax, ar_ytab
    push ax
    push ds
    mov ds, [ar_vseg]
    mov al, [si]
    mov ah, [si+32]
    mov bl, [si+64]
    mov bh, [si+96]
    mov cl, [si+128]
    mov ch, [si+160]
    mov dl, [si+192]
    mov dh, [si+224]
    pop ds
    pop si
%rep 8                              ; out[b] = bit b of rows 0..7, row 0 at
    shr al, 1                       ;   the left; pushed b = 0 first, so the
    rcl di, 1                       ;   pops come top row first
    shr ah, 1
    rcl di, 1
    shr bl, 1
    rcl di, 1
    shr bh, 1
    rcl di, 1
    shr cl, 1
    rcl di, 1
    shr ch, 1
    rcl di, 1
    shr dl, 1
    rcl di, 1
    shr dh, 1
    rcl di, 1
    push di
%endrep
    mov es, [ar_vidseg]
    jmp [ar_put]

; ar_put_* - pop the cell's 8 rows, top first, and put each doubled across.
; SI = its first row's ytab entry, BP = the cell's byte offset across.
ar_put_vga:                         ; 8 rows into 15: the last is single
    ; STKBALANCE-OK: jumped to from ar_cell, which pushed the eight rows it pops
%rep 7
    pop bx
    xor bh, bh
    shl bx, 1
    mov ax, [ar_dbl+bx]
    mov di, [si]
    mov [es:di+bp], ax
    mov [es:di+bp+80], ax
    add si, 4
%endrep
    pop bx
    xor bh, bh
    shl bx, 1
    mov ax, [ar_dbl+bx]
    mov di, [si]
    mov [es:di+bp], ax
    ret

ar_put_herc:
    ; STKBALANCE-OK: jumped to from ar_cell, which pushed the eight rows it pops
%rep 8
    pop bx
    xor bh, bh
    shl bx, 1
    mov ax, [ar_dbl+bx]
    mov di, [si]
    mov [es:di+bp], ax
    add si, 4
%endrep
    ret

; CGA: each cell's 8 rows into 6 - or 7 in a column that is a multiple of
; four - merged by OR; ar_cgamap is the same two patterns, and they are
; unrolled here because the per-row tests were the 5150's largest host cost
; in play (MEASURED, MartyPC)
%macro AR_CROW 0                    ; the next row, doubled, into AX
    pop bx
    xor bh, bh
    shl bx, 1
    mov ax, [ar_dbl+bx]
%endmacro
%macro AR_CPUT 1                    ; AX onto row %1 of the cell
    mov di, [si+(%1)*4]
    mov [es:di+bp], ax
%endmacro
ar_put_cga:
    ; STKBALANCE-OK: jumped to from ar_cell, which pushed the eight rows it pops
    test byte [ar_ccol], 3
    jz .seven
    AR_CROW                         ; 0 1+2 3 4 5+6 7
    AR_CPUT 0
    AR_CROW
    mov dx, ax
    AR_CROW
    or ax, dx
    AR_CPUT 1
    AR_CROW
    AR_CPUT 3
    AR_CROW
    AR_CPUT 4
    AR_CROW
    mov dx, ax
    AR_CROW
    or ax, dx
    AR_CPUT 5
    AR_CROW
    AR_CPUT 7
    ret
.seven:
    ; STKBALANCE-OK: the same eight rows, the seven-row pattern
    AR_CROW                         ; 0 1 2 3+4 5 6 7
    AR_CPUT 0
    AR_CROW
    AR_CPUT 1
    AR_CROW
    AR_CPUT 2
    AR_CROW
    mov dx, ax
    AR_CROW
    or ax, dx
    AR_CPUT 3
    AR_CROW
    AR_CPUT 5
    AR_CROW
    AR_CPUT 6
    AR_CROW
    AR_CPUT 7
    ret

; =============================================================================
; THE SOUND (ARCADE-SPEC 4.5): the board's discrete circuits as speaker tones
;
; OUT 3 and OUT 5 are latches whose bits start and hold the circuits. Once a
; frame the rising bits pick a cue, by priority, and a held saucer warbles
; under everything. Through OSAPI_SND_TONE, the tone route WireBall uses
; inside its own bracket, called only when the pitch changes. OUT 3 bit 5 is
; the amplifier: the attract mode turns it off, and so does this.
; =============================================================================
; ar_sbits - AX = the board's sound bits (X_SDEC). ar_sdec8080: Space
; Invaders' and Taito's, OUT 3 low and OUT 5 high as they are
ar_sbits:
    mov si, [ar_bx]
    jmp [si+X_SDEC]
ar_sdec8080:
    mov es, [ar_vseg]
    mov al, [es:V_SND3]
    mov ah, [es:V_SND5]
    ret

; ar_lamps - a frame's cabinet lamps, where the board has any (X_LAMPS)
ar_lamps:
    mov si, [ar_bx]
    mov ax, [si+X_LAMPS]
    or ax, ax
    jz .r
    jmp ax
.r:
    ret

ar_sound:
    call ar_sbits                   ; AX: the board's sound bits (X_SDEC)
    mov bx, [ar_sold]
    mov [ar_sold], ax
    not bx
    and bx, ax                      ; BX = the bits that rose this frame
    test al, 0x20
    jz ar_quiet                     ; the amplifier is off
    mov si, [ar_bx]
    mov si, [si+X_CUES]
    mov [ar_cfirst], si
.cue:
    lodsw                           ; AX = the bit (OUT 3 low, OUT 5 high)
    or ax, ax
    jz .play
    mov dx, ax
    lodsw                           ; AX = the cue's notes
    test bx, dx
    jz .cue
    mov cx, si
    sub cx, [ar_cfirst]             ; CX = its rank: lower is louder
    cmp word [ar_cseq], 0
    je .start
    cmp cx, [ar_crank]
    ja .cue
.start:
    mov [ar_cseq], ax
    mov [ar_crank], cx
    mov byte [ar_cwait], 0
.play:
    mov si, [ar_cseq]
    or si, si
    jz .ufo
    cmp byte [ar_cwait], 0
    je .note
    dec byte [ar_cwait]
    jmp .hold
.note:
    mov ax, [si]
    or ax, ax
    jz .done
    mov cl, [si+2]
    mov [ar_cwait], cl
    add si, 3
    mov [ar_cseq], si
    jmp ar_tone
.done:
    mov word [ar_cseq], 0
.ufo:
    mov si, [ar_bx]                 ; the held sound (the saucer): two
    mov ax, [si+X_LOOP]             ;   pitches
    test [ar_sold], ax
    jz ar_quiet
    inc byte [ar_warb]
    mov ax, 700
    test byte [ar_warb], 4
    jz ar_tone
    mov ax, 900
    jmp ar_tone
.hold:
    ret

ar_quiet:
    xor ax, ax
; ar_tone - AX = Hz (0 = off), only when it changes
ar_tone:
    cmp ax, [ar_hz]
    je .out
    mov [ar_hz], ax
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
.out:
    ret

ar_cues:                            ; the bit, the notes: by priority
    dw 0x0004, ar_n_die             ; OUT 3 bit 2: the base is hit
    dw 0x1000, ar_n_ufohit          ; OUT 5 bit 4: the saucer is hit
    dw 0x0010, ar_n_extra           ; OUT 3 bit 4: an extra base
    dw 0x0008, ar_n_invhit          ; OUT 3 bit 3: an invader is hit
    dw 0x0002, ar_n_shot            ; OUT 3 bit 1: a shot
    dw 0x0100, ar_n_f1              ; OUT 5 bits 0-3: the fleet's four steps
    dw 0x0200, ar_n_f2
    dw 0x0400, ar_n_f3
    dw 0x0800, ar_n_f4
    dw 0
ar_n_die:    dw 220
             db 4
             dw 180
             db 4
             dw 150
             db 4
             dw 120
             db 4
             dw 200
             db 4
             dw 160
             db 4
             dw 130
             db 4
             dw 100
             db 4
             dw 170
             db 4
             dw 140
             db 4
             dw 110
             db 4
             dw 90
             db 6
             dw 0
ar_n_ufohit: dw 1200
             db 3
             dw 1000
             db 3
             dw 1200
             db 3
             dw 900
             db 3
             dw 1100
             db 3
             dw 800
             db 3
             dw 0
ar_n_extra:  dw 1000
             db 4
             dw 1300
             db 4
             dw 1000
             db 4
             dw 1300
             db 4
             dw 0
ar_n_invhit: dw 900
             db 2
             dw 700
             db 2
             dw 500
             db 2
             dw 400
             db 2
             dw 0
ar_n_shot:   dw 1600
             db 1
             dw 1300
             db 1
             dw 1000
             db 1
             dw 800
             db 1
             dw 600
             db 1
             dw 0
ar_n_f1:     dw 110
             db 4
             dw 0
ar_n_f2:     dw 98
             db 4
             dw 0
ar_n_f3:     dw 87
             db 4
             dw 0
ar_n_f4:     dw 82
             db 4
             dw 0

lr_cues:                            ; Lunar Rescue (MAME 8080bw_a.cpp's
    dw 0x0004, ar_n_die             ;   lrescue_sh_port_1_w/_2_w): the death,
    dw 0x0008, ar_n_invhit          ;   an alien hit, the bonus ship, a shot,
    dw 0x0010, ar_n_extra           ;   the thrust, the footsteps and the
    dw 0x0002, ar_n_shot            ;   count of the men saved
    dw 0x0001, lr_n_thrust
    dw 0x0400, lr_n_count
    dw 0x0100, lr_n_stephi
    dw 0x0200, lr_n_steplo
    dw 0
lr_n_thrust: dw 150
             db 2
             dw 120
             db 2
             dw 0
lr_n_count:  dw 1500
             db 2
             dw 0
lr_n_stephi: dw 660
             db 2
             dw 0
lr_n_steplo: dw 440
             db 2
             dw 0

; ar_shift - shared by the portrait boards' composers (Pac-Man's, Q*bert's):
; AX = a row's 16 pixels (bit 15 the left one): DX, AX = the
; box's left and right words with them at pixel ar_sh, in MEMORY order
; (the left pixel's byte first). Two shifts by CL, no loop: a count of 16
; empties a word on every 86 (the 286 and up mask the count to 31)
ar_shift:
    mov cx, [ar_sh]
    or cx, cx
    js .left
    cmp cx, 16
    jae .right
    mov dx, ax                      ; left = M >> n, right = M << (16 - n)
    shr dx, cl
    neg cl
    add cl, 16
    shl ax, cl
    jmp short .done
.right:
    sub cx, 16
    shr ax, cl
    xor dx, dx
    jmp short .done
.left:
    neg cx
    shl ax, cl
    mov dx, ax
    xor ax, ax
.done:
    xchg dh, dl                     ; to memory order
    xchg ah, al
    ret

%include "os88pit.inc"
%include "sndpkg.inc"                   ; SOUND.DRV's FM door (arcsnd.inc)
%include "arcsnd.inc"
%include "arcland.inc"
%include "arcqb.inc"
%include "arcimp.inc"
%include "arcface.inc"
%include "arcopt.inc"
%include "arc6502.inc"
%include "arcqbs.inc"
%include "arcart.inc"
%include "arcsets.inc"                 ; (generated: getarcrom.py --asm-sets)
%define INF_DSWS                    ; (nothing stored through CS: M2)
%include "os88inflate.inc"
%define OD_TVOL ar_homedrv            ; ARCADE.DAT on ARCADE's own disk
%include "os88data.inc"
%include "qbpatch.inc"                ; (generated: arcqbgfx.py)
%ifdef ARC_PACMAN
%include "arcpm.inc"
%endif

; =============================================================================
; data
; =============================================================================
; --- the boards (ARCADE-SPEC 5.3) ---------------------------------------------
ar_boards:
    dw ar_bd_inv, ar_bd_pt2, ar_bd_lr, ar_bd_gf, ar_bd_sw, ar_bd_qb
%ifdef ARC_PACMAN
    dw ar_bd_pm
%endif
    dw 0
%macro AR_8080BOARD 7               ; name, title, code, aux, ROM bytes,
%1:                                 ;   wiring, the <SET>.ROM's name
    dw %2
    db %3, %4, 0xFF, 0
    dw %5, inv_reset, inv_setup, inv_irq, inv_frame
    dw 10020                        ; half of the board's 59.54 Hz (MAME
    db 2, 0                         ;   mw8080bw: 4.992 MHz, 320 x 262)
    dw ar_sound, inv_sbtick, inv_mix, INV_SBHZ, inv_sbprep
    dw inv_fmpat, inv_fmframe, ar_nop, ar_nop
    dw %6, %7, ar_fill
%endmacro
    AR_8080BOARD ar_bd_inv, ar_s_inv, 1, 2, 0x2000, ar_x_inv, ar_f_inv
    AR_8080BOARD ar_bd_pt2, ar_s_pt2, 3, 4, 0x5000, ar_x_pt2, ar_f_pt2
    AR_8080BOARD ar_bd_lr, ar_s_lr, 5, 6, 0x5000, ar_x_lr, ar_f_lr
    AR_8080BOARD ar_bd_gf, ar_s_gf, 7, 8, 0x1000, ar_x_gf, ar_f_gf
    AR_8080BOARD ar_bd_sw, ar_s_sw, 9, 10, 0x1000, ar_x_sw, ar_f_sw
ar_bd_qb:                           ; Gottlieb's Q*bert, its 8088 native
    dw ar_s_qb                      ;   (ARCADE-SPEC 10)
    db 11, AR_P_NONE, 0xFF, 0
    dw 0, qb_reset, qb_setup, qb_irq, qb_frame
    dw 19427                        ; the board's 61.42 Hz NMI (MAME gottlieb:
    db 1, QB_LATE                   ;   5 MHz pixels, 318 x 256), one a frame
    dw ar_nop, qbs_tick, qbs_mix, QBS_HZ, qbs_prep  ; its sound board, live on a
                                    ;   286 into the stream (ARCADE-SPEC 13)
    dw 0, ar_nop, qb_on, qb_off, 0
    dw ar_f_qb, qb_build
%ifdef ARC_PACMAN
ar_bd_pm:
    dw ar_s_pm
    db 12, 13, 0xFF, BDF_HOLD
    dw 0x4000, pm_reset, pm_setup, pm_irq, pm_frame
    dw 19688                        ; the board's 60.61 Hz (MAME pacman.cpp:
    db 1, 0                         ;   6.144 MHz, 384 x 264), one interrupt
    dw pm_sound, pm_sbtick, pm_mix, PM_SBHZ, 0
    dw pm_fmpat, pm_fmframe, pm_on, pm_off, 0
    dw ar_f_pm, pm_build
%endif
ar_f_inv:   db 'INVADERS.ROM', 0    ; each board's <SET>.ROM (ARCADE-SPEC 1.4)
ar_f_pt2:   db 'INVADPT2.ROM', 0
ar_f_lr:    db 'LRESCUE.ROM', 0
ar_f_gf:    db 'GUNFIGHT.ROM', 0
ar_f_sw:    db 'SEAWOLF.ROM', 0
ar_f_qb:    db 'QBERT.ROM', 0
ar_f_pm:    db 'PACMAN.ROM', 0

; the wiring (X_*): Space Invaders as we have always run it; Taito's two as
; MAME 8080bw has them (ARCADE-SPEC 5.3, 8.3)
ar_x_inv:   db 0x00, 0x08, 0x00, 0
            dw ar_cues, 0x0001, inv_cbits, 0x1F1E
            db 0, 0
            dw inv_fsteps, inv_fnoise, inv_ftones, ar_keys8080, ar_sdec8080, 0
ar_x_pt2:   db 0xF4, 0x81, 0x00, 1
            dw ar_cues, 0x0001, inv_cbits, 0x1F1E
            db 0, 0
            dw inv_fsteps, inv_fnoise, inv_ftones, ar_keys8080, ar_sdec8080, 0
ar_x_lr:    db 0xFF, 0x89, 0x00, 1
            dw lr_cues, 0x1000, lr_cbits, 0x071F
            db 14, 0
            dw lr_fsteps, lr_fnoise, lr_ftones, ar_keys8080, ar_sdec8080, 0
ar_x_gf:    db 0x30, 0x30, 0x10, 0  ; Gun Fight: the guns aimed level, a
            dw gf_cues, 0, gf_cbits, 0x000F   ;   70-second game
            db 0, 1
            dw ar_none, gf_fnoise, ar_none, gf_keys, gf_sdec, 0
ar_x_sw:    db 0x40, 0x60, 0x00, 0  ; Sea Wolf: 70 seconds, extended at 4000
            dw sw_cues, 0, sw_cbits, 0x001F
            db 0, 1
            dw sw_fsteps, sw_fnoise, ar_none, sw_keys, sw_sdec, sw_lamps
ar_none:    dw 0

ar_tpl:
    dw 60, 40, AR_CONT_W+2, AR_CONT_H+TITLE_H+1
    dw ar_title, ar_paint, ar_onkey, ar_onclick
    OS88_MENUSET ar_menus, ar_title, ar_oncmd
%ifdef ARC_PACMAN
        OS88_MENU ar_m_game, ar_i_game, 10
%else
        OS88_MENU ar_m_game, ar_i_game, 9
%endif
    OS88_MENUSET_END ar_menus
ar_i_game:  dw ar_s_play, ar_s_reset, ar_s_addrom, ar_s_inv, ar_s_pt2, ar_s_lr, ar_s_gf, ar_s_sw, ar_s_qb
%ifdef ARC_PACMAN
            dw ar_s_pm
%endif
ar_s_inv:   db 'Space Invaders', 0
ar_s_pt2:   db 'Space Invaders Part II', 0
ar_s_lr:    db 'Lunar Rescue', 0
ar_s_gf:    db 'Gun Fight', 0
ar_s_sw:    db 'Sea Wolf', 0
ar_s_qb:    db 'Q*bert', 0
ar_s_none:  db '(none)', 0
TOASTSTR ar_s_nobd, 'Arcade: pick a game'
ar_s_pm:    db 'Pac-Man', 0
ar_m_game:  db 'Game', 0
ar_s_play:  db 'Play', 0
ar_s_reset: db 'Reset', 0
ar_s_addrom: db 'Add ROM sets...', 0
ar_title:   db 'Arcade', 0

ar_lines:   dw ar_l1, ar_l2, ar_l3, ar_l4, ar_l5, ar_l6, ar_l7, ar_l8, 0
ar_l1:      db 'Game: ', 0                  ; + the board's name, painted after
ar_l2:      db 0
ar_l3:      db 'Enter or click: play full screen', 0
ar_l4:      db '5 or C: coin    1, 2: players', 0
ar_l5:      db 'Arrows: move    Space: fire', 0
ar_l6:      db 'Esc: back here, the game held', 0
ar_l7:      db 0
ar_l8:      db 'Game > Reset: power the board off', 0
TOASTSTR ar_s_vgaonly, 'Pac-Man needs a VGA'
TOASTSTR ar_s_qbvga, 'Q*bert needs a VGA'

ar_ablines: dw ar_ab1, ar_ab2, ar_ab3, ar_ab4, 0
ar_ab1:     db 'Arcade for os8088', 0
ar_ab2:     db 'An 8080 board, translated', 0
ar_ab3:     db 'to 8086 ahead of time', 0
ar_ab4:     db 'Space Invaders (C) Taito', 0

TOASTSTR ar_s_bad, 'Arcade: code part bad'
ar_s_romno: db ': not found', 0
ar_s_romdisk: db ': disk in '
ar_s_romdl: db 'B:', 0  ; after the <SET>.ROM's name: at most 12
ar_s_romis: db ': wrong set', 0  ;   and 11, the toast bar's 24 (TOAST_MAX)
ar_s_romram: db ': no memory', 0
TOASTSTR ar_s_nomem, 'Arcade: no memory'
TOASTSTR ar_s_nofsx, 'Arcade: no full screen'
TOASTSTR ar_s_nomode, 'Arcade: no screen mode'
TOASTSTR ar_s_miss, 'Arcade: no code at ????'

%define OS88UI_ABOUT
%define OS88UI_NOBTN
%define OS88UI_ABDISMISS
%include "os88ui.inc"

; =============================================================================
; bss: the parts standard's first, then ours
; =============================================================================
%assign AR_BSS 0
%macro AR_VAR 2
%1 equ os88_image_end + OP_BSS + AR_BSS
%assign AR_BSS AR_BSS + %2
%endmacro
AR_VAR ar_core, 4                   ; dw arc_enter, dw the code part
AR_VAR ar_sh, 2                     ; ar_shift: pixels into the box
AR_VAR ar_rimg, 2                   ; the user's ROM image (ar_romload)
AR_VAR ar_rimgc, 2                  ;   ...and its claim, 0 when none
AR_VAR ar_lbd, 2                    ;   the board it is read for
AR_VAR ar_qsel, 1                   ; 1: ar_select toasts nothing (at entry)
AR_VAR ar_msg, 48                   ; a toast's line, made
AR_VAR ar_crctab, 1024              ; CRC-32's table (ar_crcinit)
AR_VAR INF_WS, INF_WS_SZ            ; os88inflate.inc's work space
AR_VAR ar_homedrv, 1                ; the drive ARCADE started from
AR_VAR ar_homecl, 2                 ;   ...and the folder
AR_VAR ar_rhome, 2                  ; ar_romload: where it stood
AR_VAR ar_rhomed, 1
AR_VAR ar_places, 1 + AR_NPLACE * 3 ; ARCADE.DAT: places a .ROM was made,
AR_VAR ar_sdrv, AR_NSET             ;   and each set's drive at its import
                                    ;   (0xFF none) - one record, so contiguous
AR_VAR af_dsv, AR_NSET              ;   each game's switches (12.3, 0FFh none)
AR_VAR ar_hsv, AR_NSET * AR_HSB     ;   each game's best (12.4)
AR_DATSZ    equ 1 + AR_NPLACE * 3 + AR_NSET * (2 + AR_HSB)
AR_VAR ar_datst, 1                  ;   ...and os88data's state byte for it
AR_VAR ar_impname, 13               ; the import (arcimp.inc): the zip,
AR_VAR ar_zsize, 4                  ;   its size,
AR_VAR ar_zkb, 2                    ;   its claim's KB, its claim,
AR_VAR ar_zclaim, 2
AR_VAR ar_cdseg, 2                  ;   its central directory: where,
AR_VAR ar_cdoff, 2
AR_VAR ar_cdend, 2                  ;   where it ends, how many entries
AR_VAR ar_cdn, 2
AR_VAR ar_impbd, 2                  ;   the board to pick again after
AR_VAR ar_nimp, 1                   ;   sets made, and sets refused
AR_VAR ar_nwrong, 1
AR_VAR ar_iset, 2                   ;   the set being made, its claim,
AR_VAR ar_iclaim, 2
AR_VAR ar_ifound, 1                 ;   chips found, and each one's method
AR_VAR ar_imeth, 2                  ;   and packed size
AR_VAR ar_ipack, 2
AR_VAR ar_z2, 16                    ;   a PARENT zip (ar_zsize..ar_cdn's shape)
AR_VAR ar_zon, 1                    ;   1: the parent's is the one in ar_zsize
AR_VAR ar_iloose, 1                 ;   1: loose chips in a folder, no zip
AR_VAR ar_iok, AR_NSET              ;   each set: whole in the zip(s)
AR_VAR ar_iop, 1                    ;   1: a file-progress operation is ours
AR_VAR ar_iidx, 1                   ;   the set's index in ar_sets
AR_VAR af_theme, 2                  ; the face (arcface.inc): its role table,
AR_VAR af_lay, 2                    ;   its layout, the pictures' claim,
AR_VAR af_pclaim, 2
AR_VAR af_sel, 1                    ;   the selection, the games, how many
AR_VAR af_n, 1                      ;   are READY, the display's FSX modes,
AR_VAR af_nready, 1
AR_VAR af_caps, 2
AR_VAR af_state, AF_MAXG            ;   each game's state and picture
AR_VAR af_pics, AF_MAXG
AR_VAR af_ox, 2                     ;   the content origin
AR_VAR af_oy, 2
AR_VAR af_fx1, 2                    ;   a fill's corners
AR_VAR af_fy1, 2
AR_VAR af_fx2, 2
AR_VAR af_fy2, 2
AR_VAR af_bst, 2                    ;   the band: its stride, rows, bytes
AR_VAR af_bh, 2
AR_VAR af_band, AF_BANDSZ
AR_VAR af_gseg, 2                   ;   the system font
AR_VAR af_goff, 2
AR_VAR af_grng, 2
AR_VAR af_pr, 2                     ;   cells of the drawing routines
AR_VAR af_ps, 2
AR_VAR af_pen, 2
AR_VAR af_ri, 2
AR_VAR af_rpen, 2
AR_VAR af_rst, 1
AR_VAR af_py, 2
AR_VAR af_px, 2
AR_VAR af_bi, 2
AR_VAR af_bw, 2
AR_VAR af_ch, 1
AR_VAR af_cy, 2
AR_VAR af_cg, 2
AR_VAR af_snapw, 1                  ;   a picture taken, to be written
AR_VAR af_sgame, 2                  ;   ...of this game
AR_VAR af_acc, 96                   ;   a screen row, a bit a lit pixel
AR_VAR af_pl, 4 * 96                ;   ...its four planes
AR_VAR af_blk, 16                   ;   the colour numbers black on the glass
AR_VAR af_nblk, 2
AR_VAR af_lit, 16                   ;   ...and each one: 1 lit
AR_VAR af_xs, AF_PICW * 2           ;   the columns sampled
AR_VAR af_x0, 2
AR_VAR af_xn, 2
AR_VAR ar_hold, 1                   ; 1 paused, 2 the card shown (12.2)
AR_VAR ar_glyph, 96 * 8             ;   the font, 32..127, for the box
AR_VAR ar_ovn, 2                    ;   the box's lines, its rows, where
AR_VAR ar_ovl, 24
AR_VAR ar_ovh, 2
AR_VAR ar_ovx, 2
AR_VAR ar_ovy, 2
AR_VAR ar_hsfr, 2                   ; frames since a power-on (12.4)
AR_VAR af_setm, 1                   ; the card is SETTINGS (12.3): which one,
AR_VAR af_dsi, 1                    ;   the byte, a setting's bits, the
AR_VAR af_dsb, 1                    ;   choice set, how many, the turn's way
AR_VAR af_dsm, 1
AR_VAR af_dsc, 1
AR_VAR af_dsk, 1
AR_VAR af_dsdir, 1
AR_VAR af_dsdirty, 1
AR_VAR af_dsvals, 8
AR_VAR m6_a, 1                      ; the 6502 (arc6502.inc): its registers,
AR_VAR m6_x, 1
AR_VAR m6_y, 1
AR_VAR m6_s, 1
AR_VAR m6_p, 1
AR_VAR m6_irq, 1                    ;   its IRQ line, PC, memory, the debt of
AR_VAR m6_pc, 2                     ;   its last run, opcodes it does not know,
AR_VAR m6_seg, 2                    ;   scratch, and its cycles
AR_VAR m6_debt, 2
AR_VAR m6_bad, 2
AR_VAR m6_tgt, 2
AR_VAR m6_left, 2
AR_VAR m6_cyc, 4
AR_VAR m6_c0, 4
AR_VAR qbs_on, 2                    ; Q*bert's sound board (arcqbs.inc): on,
AR_VAR qbs_seg, 2                   ;   its 6503's memory, its power-on's
AR_VAR qbs_left, 4                  ;   cycles, the cycle fraction, the DAC
AR_VAR qbs_frac, 2
AR_VAR qbs_dac, 1
AR_VAR qr_state, 0                  ;   ...and its RIOT, one block (qr_reset)
AR_VAR qr_pa, 1
AR_VAR qr_pb, 1
AR_VAR qr_ddra, 1
AR_VAR qr_ora, 1
AR_VAR qr_ddrb, 1
AR_VAR qr_orb, 1
AR_VAR qr_tval, 1
AR_VAR qr_tsh, 1
AR_VAR qr_tie, 1
AR_VAR qr_tflag, 1
AR_VAR qr_pa7f, 1
AR_VAR qr_pa7ie, 1
AR_VAR qr_pa7pos, 1
AR_VAR qr_tat, 4
QR_SIZE     equ 17
AR_VAR qr_sah, 1
AR_VAR ar_rfdrv, 1                  ; where the .ROM was found (its picture's
AR_VAR ar_rfcl, 2                   ;   place)
AR_VAR qb_gseg, 2                   ; Q*bert (arcqb.inc): G, the game's 64KB
AR_VAR qb_gcs, 2                    ;   ...and G as CODE (qb_gcode)
AR_VAR qb_rclaim, 2                 ;   the parts qb_build made
AR_VAR qb_sclaim, 2
AR_VAR qbb_mask, 1                  ;   ...and its loops' cells
AR_VAR qbb_bit, 2
AR_VAR qb_n88, 2                    ;   the N88 core's segment, its yield
AR_VAR qb_n88y, 2
AR_VAR qb_palok, 1                  ;   qb_pal holds what the DAC has
AR_VAR qb_pri, 1                    ;   the background's priority, as drawn
AR_VAR qb_pal, 32                   ;   the palette as last loaded
AR_VAR qb_cur, QB_NSPR * QBS_SIZE   ;   the sprites now, and as drawn
AR_VAR qb_old, QB_NSPR * QBS_SIZE
AR_VAR qb_vcopy, 960                ;   the tiles as drawn
AR_VAR qb_dirty, 64                 ;   a box (4 x 4 tiles) to draw again
AR_VAR qb_dnext, 1                  ;   ...where the next frame looks first
AR_VAR qb_ox, 2                     ;   a sprite's old box, for the union
AR_VAR qb_oy, 2
AR_VAR qb_ox1, 2
AR_VAR qb_oy1, 2
AR_VAR qb_ctx, 2                    ;   the box being composed, in tiles
AR_VAR qb_cty, 2
AR_VAR qb_ctw, 2
AR_VAR qb_cth, 2
AR_VAR qb_bx0, 2                    ;   ...in pixels
AR_VAR qb_bx1, 2
AR_VAR qb_by0, 2
AR_VAR qb_by1, 2
AR_VAR qb_s, 2                      ;   the sprite being laid over it
AR_VAR qb_sy, 2
AR_VAR qb_iy1, 2
AR_VAR qb_sbase, 2
AR_VAR qb_o, 4                      ;   NOT its row's opaque mask, placed
AR_VAR qb_rows, 2
AR_VAR qb_yp, 2
AR_VAR qb_shseg, 2                  ;   the shadow of the glass (32KB claim)
AR_VAR qb_shr0, 2                   ;   ...the box's first byte in it, a plane
AR_VAR qb_shr, 2                    ;   ...and a row's
AR_VAR qb_pp, 2                     ;   the plane being put, in qb_pl
AR_VAR qb_skips, 1                  ;   frames the picture has waited
AR_VAR qb_yr, 2                     ;   the row's entry in qb_yoff
AR_VAR qb_act, 2 * QB_NSPR          ;   the active sprites (qb_cur offsets)
AR_VAR qb_actend, 2
AR_VAR qb_bx2, 2                    ;   the box's first byte across
AR_VAR qb_di0, 2                    ;   ...and the row's on the glass
AR_VAR qb_srok, 1                   ;   qb_srcopy holds the sprite RAM read
AR_VAR qb_srcopy, QB_NSPR * 4
AR_VAR qb_chg, 1                    ;   a byte of the row changed
AR_VAR qb_two, 1                    ;   80: the row is one screen row, 160 two
AR_VAR qb_rleft, 2                  ;   rows left to put
AR_VAR qb_yoff, 257 * 2             ;   a portrait row's screen offset
AR_VAR qb_pl, 4 * 128               ;   the box: 4 planes of 32 rows x 4
AR_VAR qb_tl, 4 * 128               ;   ...its tiles alone, for the priority
AR_VAR qb_top, 128                  ;   ...and their opaque bytes
AR_VAR ar_bd, 2                     ; the board (ar_boards)
AR_VAR ar_rseg, 2                   ; its ROM part
AR_VAR ar_period, 2                 ; its PIT counts between interrupts
AR_VAR ar_pend, 1                   ; an interrupt waits on the guest's EI
AR_VAR ar_late, 4                   ; ...and how far behind is dropped
AR_VAR ar_irqn, 1                   ; interrupts so far this frame
AR_VAR ar_vseg, 2
AR_VAR ar_aseg, 2
AR_VAR ar_vidseg, 2
AR_VAR ar_win, 2
AR_VAR ar_abon, 1
AR_VAR ar_quit, 1
AR_VAR ar_halted, 1
AR_VAR ar_which, 1
AR_VAR ar_err, 2
AR_VAR ar_due, 4
AR_VAR ar_t0, 4
AR_VAR ar_band, 2
AR_VAR ar_ccol, 1                   ; the cell's column, for ar_put_cga
AR_VAR ar_put, 2                    ; ar_put_vga, _herc or _cga
AR_VAR ar_budget, 2
AR_VAR ar_col, 2
AR_VAR ar_xoff, 2
AR_VAR ar_sold, 2                   ; the sound latches last frame
AR_VAR ar_cseq, 2                   ; the cue playing: its next note
AR_VAR ar_crank, 2
AR_VAR ar_cwait, 1
AR_VAR ar_warb, 1
AR_VAR ar_hz, 2                     ; the pitch sounding now
AR_VAR ar_pbits, 1                  ; typed coin/start bits being held
AR_VAR ar_pleft, 1                  ;   ...and for how many more frames
AR_VAR ar_fsi, FSI_SIZE
AR_VAR ar_bx, 2                     ; the 8080 board's wiring (X_*)
AR_VAR ar_cfirst, 2                 ;   ...its speaker cues' first
AR_VAR ar_cmode, 1                  ; a colour board on VGA: its cells' colours
AR_VAR ar_cbase, 2                  ;   ...the colour map in the ROM part
AR_VAR ar_cred, 1                   ;   ...the screen turned red
AR_VAR ar_cstate, 1                 ;   ...the two, as last drawn
AR_VAR ar_cesr, 1                   ;   ...set/reset's enable, as last sent
AR_VAR ar_sroute, 1                 ; THE SOUND (ARCADE-SPEC 8): its route
AR_VAR ar_sbok, 1                   ;   the stream's needs are here
AR_VAR ar_swhy, 1                   ;   ...the last step that declined one
AR_VAR ar_scaps, 2
AR_VAR ar_sbseg, 2                  ;   the stream's ring, and its handle
AR_VAR ar_sbhand, 1
AR_VAR ar_sbtot, 2                  ;   ...its total, free-running
AR_VAR ar_sbhz, 2                   ;   ...and its rate
AR_VAR ar_sbcons, 2                 ;   the consumed count last frame, and
AR_VAR ar_sbstill, 1                ;     how many frames it has stood still
AR_VAR ar_sbold, 2                  ;   the board's sound bits last frame
AR_VAR ar_fmsh, AR_FMCH * 3         ;   FM: each channel's B0h, A0h, carrier TL
AR_VAR ar_fmn, 2                    ;   ...and the frame's batch
AR_VAR ar_fmbuf, AR_FMMAX * 2
AR_VAR inv_v, INV_NV * INV_V_SIZE   ; Space Invaders: the voices
AR_VAR inv_mn, 2
AR_VAR inv_md, 2
AR_VAR inv_mseg, 2
AR_VAR inv_mstep, 1                 ;   1, or 2: every other sample
AR_VAR inv_act, 6                   ;   the voices a chunk sums
AR_VAR inv_frise, 2                 ;   ...on FM
AR_VAR inv_fheld, 2
AR_VAR inv_fwarb, 1
AR_VAR inv_fnz, 1
AR_VAR inv_fnzr, 1
AR_VAR inv_fnzc, 1
AR_VAR inv_fnzs, 1
AR_VAR inv_fnzhz, 2
AR_VAR inv_fseq, 2
AR_VAR inv_fwait, 1
AR_VAR ar_dbl, 512
AR_VAR ar_dblr, 512                 ; ...bit 0 leftmost (landscape boards)
AR_VAR ar_celf, 2                   ; the cell drawer: turned or landscape
AR_VAR ar_ccnt, 1
AR_VAR gf_aim, 4                    ; Gun Fight's levers: notch, frames to go
AR_VAR sw_scope, 2                  ; Sea Wolf's periscope: position, frames
AR_VAR sw_lamp, 1                   ;   ...and its lamps as last drawn
AR_VAR gf_odd, 1                    ; Gun Fight's keys: this frame or the next
AR_VAR ar_ytab, 1024
%ifdef ARC_PACMAN
AR_VAR pm_yoff, 288 * 2             ; Pac-Man: a portrait row's screen offset
AR_VAR pm_pl, 4 * 128               ;   the composing box: 4 planes of 32
                                    ;   rows of 4 bytes
AR_VAR pm_sel, 16                   ;   a palette's plane selectors
AR_VAR pm_selw, 32                  ;   a sprite's selectors as words
AR_VAR pm_opqw, 8                   ;   ...and its opacity
AR_VAR pm_sm, 16                    ;   its row's masks, shifted into place
AR_VAR pm_m, 8                      ;   a sprite row's four value masks
AR_VAR pm_o, 4                      ;   ...NOT its opaque mask, placed
AR_VAR pm_ox, 2                     ;   a sprite's old box, for the union
AR_VAR pm_oy, 2
AR_VAR pm_ox1, 2
AR_VAR pm_oy1, 2
AR_VAR pm_cur, 8 * PMS_SIZE         ;   the sprites now, and as last drawn
AR_VAR pm_old, 8 * PMS_SIZE
AR_VAR pm_ctx, 2                    ;   the box, in tiles
AR_VAR pm_cty, 2
AR_VAR pm_ctw, 2
AR_VAR pm_cth, 2
AR_VAR pm_bx0, 2                    ;   ...and in pixels
AR_VAR pm_bx1, 2
AR_VAR pm_by0, 2
AR_VAR pm_by1, 2
AR_VAR pm_iy0, 2
AR_VAR pm_iy1, 2
AR_VAR pm_s, 2
AR_VAR pm_sy, 2
AR_VAR pm_sbase, 2
AR_VAR pm_flip, 1
AR_VAR pm_rows, 2
AR_VAR pm_rclaim, 2                 ;   the ROM part pm_build made
AR_VAR pm_shseg, 2                  ;   the shadow of the glass (part 7)
AR_VAR pm_tvalid, 28 * 36           ;   a tile's cached planes are good
AR_VAR pm_t, 2
AR_VAR pm_tx, 2
AR_VAR pm_ty, 2
AR_VAR pm_vstep, 6                  ;   the WSG: each voice's step, phase,
AR_VAR pm_vph, 6                    ;     and its table's key
AR_VAR pm_vkey, 3
AR_VAR pm_wvol, 1
AR_VAR pm_first, 1                  ;     the first sounding voice writes
AR_VAR pm_mn, 2
AR_VAR pm_md, 2
AR_VAR pm_wtp, 2                    ;     ...where the tables start
AR_VAR pm_wtabs, 3 * 256 + 255      ;     three, on a 256-byte page
%endif
AR_VAR qbs_chips, 1                 ; 1: the pick kept Q*bert's sound chips (qbs_want)
    OS88_BSS OP_BSS + AR_BSS
    OS88_IMAGE_END

