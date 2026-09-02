; =============================================================================
; os8088 - apps/frotz/zmem.inc
;
; Story memory: the 20-bit address space, the big-endian word, and the header.
;
; ONE DECISION SHAPES THIS FILE (docs/FROTZ-PLAN.md 3): the whole story file is
; resident in one heap claim, and a story address is up to 20 bits (Standard
; 1.1.4 caps a v6-v8 file at 512KB). A claim hands back a base SEGMENT, so the
; conversion is
;
;     byte at A   ->   segment = [zf_sseg] + (A >> 4),   offset = A & 15
;
; and `A >> 4` for a 20-bit A is a 16-bit result - a shift pair and an add,
; never a 32-bit register (CLAUDE.md rule 1: 8086 only, no exception).
;
; Two fast paths carry almost all the traffic and both are visible here:
;
;   - **A < 65536 is one segment.** [zf_sseg:A] reaches it with no arithmetic
;     at all. All of dynamic memory lives there by construction (the static
;     base is a header WORD, Standard 1.1), so every loadb/storeb/property/
;     object/dictionary access takes it. That is what zm_gb16..zm_pw16 are;
;     zm_gb/zm_gw/zm_pb/zm_pw are the general forms and they simply FALL INTO
;     the 16-bit ones when DX is zero. One test, no duplicated body.
;
;   - **The PC is a live ES:SI** (zm_seek/zm_norm/zm_pcaddr). Instruction fetch
;     is `lods`, the cheapest thing the 8086 has, and ES:SI is renormalised
;     only when SI crosses 0x8000 - once per 32KB of straight-line code. That
;     is why zm_seek always lands SI in 0..15 rather than taking the <64K fast
;     path: the PC must start with the whole of a segment ahead of it.
;
; Z-machine words are BIG-ENDIAN and the 8086 is not (docs/FROTZ-PLAN.md 3).
; THIS FILE IS THE ONLY PLACE A WORD IS FORMED FROM STORY BYTES. Everything
; else calls zm_gw/zm_gw16. That is not tidiness: a byte pair assembled by hand
; at one call site out of two hundred is a bug that decodes as plausible text.
;
; WRITES ARE REFUSED ABOVE [zf_static] (Standard 1.1.2: "It is illegal for a
; game to attempt to write to static memory"). Refused, not faulted - a story
; that does it is broken, and taking the machine down over it is worse than
; dropping the byte. The one exception is the Flags 2 pair at $10/$11, the only
; header bytes the Standard marks Dyn (11.1, 11.1.2); they are let through
; unconditionally so a transcription bit cannot be lost to a pathological
; static base or to a write that arrives before zm_setup has cached one.
;
; The interpreter's OWN header writes (zm_setup, zm_hdr_screen) go straight
; through ES and are not checked - the "Int" and "Rst" columns of Standard 11.1
; are exactly the fields the interpreter must set, and it may set them wherever
; they sit.
; =============================================================================

; --- fatal codes, cached in [zf_err] and rendered by zm_errmsg ----------------
ZERR_NONE   equ 0
ZERR_VER    equ 1                   ; not a Version this interpreter knows
ZERR_BIG    equ 2                   ; longer than the claim, or than the Version
ZERR_BAD    equ 3                   ; not a story file / the header is nonsense
ZERR_MEM    equ 4                   ; a claim was refused (raised by zio.inc)
ZERR_MAX    equ 4

; --- header offsets (Standard 11.1). Byte addresses into the story. ----------
ZMH_VER     equ 0x00                ; byte: Version number
ZMH_FLAGS1  equ 0x01                ; byte: Flags 1 - meaning differs v1-3/v4+
ZMH_HIMEM   equ 0x04                ; word: base of high memory
ZMH_INITPC  equ 0x06                ; word: initial PC (v6: packed "main")
ZMH_DICT    equ 0x08                ; word: dictionary
ZMH_OBJTAB  equ 0x0A                ; word: object table (= property defaults)
ZMH_GVAR    equ 0x0C                ; word: global variables
ZMH_STATIC  equ 0x0E                ; word: base of static memory
ZMH_FLAGS2  equ 0x10                ; word: Flags 2; bit 8 is in $10, 0..7 in $11
ZMH_ABBREV  equ 0x18                ; word: abbreviations table
ZMH_FLEN    equ 0x1A                ; word: file length / 2, 4 or 8 (11.1.6)
ZMH_CKSUM   equ 0x1C                ; word: checksum of the file
ZMH_INTNUM  equ 0x1E                ; byte: interpreter number (11.1.3)
ZMH_INTVER  equ 0x1F                ; byte: interpreter version (11.1.3.1)
ZMH_SCRH    equ 0x20                ; byte: screen height in lines
ZMH_SCRW    equ 0x21                ; byte: screen width in characters
ZMH_SCRWU   equ 0x22                ; word: screen width in units (8.4.3)
ZMH_SCRHU   equ 0x24                ; word: screen height in units
ZMH_FONT26  equ 0x26                ; byte: v5 font WIDTH, v6 font HEIGHT (8.1.1)
ZMH_FONT27  equ 0x27                ; byte: v5 font HEIGHT, v6 font WIDTH
ZMH_ROFF    equ 0x28                ; word: routines offset / 8 (v6, v7)
ZMH_SOFF    equ 0x2A                ; word: static strings offset / 8 (v6, v7)
ZMH_DEFBG   equ 0x2C                ; byte: default background colour
ZMH_DEFFG   equ 0x2D                ; byte: default foreground colour
ZMH_TERM    equ 0x2E                ; word: terminating characters table
ZMH_STDREV  equ 0x32                ; byte pair: Standard revision n.m (11.1.5)
ZMH_ALPHA   equ 0x34                ; word: alphabet table, 0 = the built-in one
ZMH_HDREXT  equ 0x36                ; word: header extension table (11.1.7)

; The interpreter's identity. 6 is "IBM PC" (Standard 11.1.3) and this IS an
; IBM PC, which is the answer that section asks for. It costs one thing worth
; naming: 'Beyond Zork' picks its character-graphics font from this number, and
; the kernel font has glyphs 32..126 and nothing else (SPEC.md 6) - so ztext.inc
; approximates font 3 rather than the header lying about the machine.
ZM_INTNUM   equ 6
ZM_INTVER   equ 'F'                 ; conventionally an upper-case letter (11.1.3.1)

; Z-machine colour numbers (Standard 8.3.1). The pair the Standard names for an
; interpreter that cannot produce colours (8.3.2) is also exactly our look.
ZM_COL_BLACK equ 2
ZM_COL_WHITE equ 9

; --- our slack in zbss.inc (64 bytes at ZMEM_SLACK; 9 used) ------------------
%define zm_scw    (os88_image_end + ZMEM_SLACK + 0)   ; word: content width, px
%define zm_sch    (os88_image_end + ZMEM_SLACK + 2)   ; word: content height, px
%define zm_ckhdr  (os88_image_end + ZMEM_SLACK + 4)   ; word: header's checksum
%define zm_lsc    (os88_image_end + ZMEM_SLACK + 6)   ; byte: file-length shift
%define zm_bpp    (os88_image_end + ZMEM_SLACK + 7)   ; byte: OSAPI_VIDEO's DH

; =============================================================================
; zm_map - the 20-bit address decoder (INTERNAL)
; in:  DX:AX = story address (DX = high 4 bits)
; out: ES:BX addresses the byte
; clobbers BX, DX, ES. Preserves AX and CX, which is what lets zm_gb hand back
; a byte in AL with AH untouched.
;
; The DX=0 arm is not an optimisation of the other one, it is a different
; answer: it leaves the whole of the low 64KB reachable from one segment, so a
; word read never straddles a segment boundary and [es:bx+1] is exact.
; =============================================================================
zm_map:
    push ax
    push cx
    or dx, dx
    jnz .hi
    mov bx, ax                      ; < 64KB: one segment reaches all of it
    mov es, [zf_sseg]
    jmp short .out
.hi:
    mov bx, ax
    and bx, 15                      ; offset = A & 15
    mov cl, 4
    shr ax, cl                      ; low 12 bits of A >> 4
    mov cl, 12
    shl dx, cl                      ; the high nibbles into bits 12..15
    or ax, dx
    add ax, [zf_sseg]               ; segment = base + (A >> 4)
    mov es, ax
.out:
    pop cx
    pop ax
    ret

; =============================================================================
; zm_gb16 / zm_gw16 - the fast path: a byte or word below 64KB
; in:  AX = byte address (< 65536)
; out: AL = the byte / AX = the word, big-endian decoded
;
; [es:bx+1] cannot leave the segment for any address a Z-machine word may
; legally occupy: static memory "must end by byte address $0ffff" (Standard
; 1.1), so a word starting at $ffff is already illegal and no story has one.
; =============================================================================
zm_gb16:
    push bx
    push es
    mov es, [zf_sseg]
    mov bx, ax
    mov al, [es:bx]
    pop es
    pop bx
    ret

zm_gw16:
    push bx
    push es
    mov es, [zf_sseg]
    mov bx, ax
    mov ah, [es:bx]                 ; big-endian: high byte first
    mov al, [es:bx+1]
    pop es
    pop bx
    ret

; =============================================================================
; zm_gb / zm_gw - a byte or word anywhere in the 20-bit space
; in:  DX:AX = story address
; out: AL = the byte (AH untouched) / AX = the word
; =============================================================================
zm_gb:
    or dx, dx
    jz zm_gb16                      ; the common case, and it is a whole routine
    push bx
    push dx
    push es
    call zm_map
    mov al, [es:bx]
    pop es
    pop dx
    pop bx
    ret

zm_gw:
    or dx, dx
    jz zm_gw16
    push bx
    push dx
    push es
    call zm_map                     ; BX lands in 0..15, so BX+1 cannot wrap
    mov ah, [es:bx]
    mov al, [es:bx+1]
    pop es
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zm_wrok - may the story write this byte? (INTERNAL)
; in:  AX = byte address (< 64KB)
; out: CF = 1 -> refuse. Preserves every register.
;
; Standard 1.1.2. The Flags 2 pair is the exception, not a courtesy: bit 0 of
; $11 is transcription and the Standard requires the bit to keep telling the
; truth (11.1.2), so it is never dropped.
; -----------------------------------------------------------------------------
zm_wrok:
    cmp ax, [zf_static]
    jb .ok
    cmp ax, ZMH_FLAGS2
    je .ok
    cmp ax, ZMH_FLAGS2 + 1
    je .ok
    stc
    ret
.ok:
    clc
    ret

; =============================================================================
; zm_pb16 / zm_pw16 - write a byte or word below 64KB
; in:  AX = byte address, BL / BX = the value
; out: nothing; a write to static or high memory is dropped
; =============================================================================
zm_pb16:
    push ax
    push di
    push es
    call zm_wrok
    jc .out
    mov es, [zf_sseg]
    mov di, ax
    mov [es:di], bl
.out:
    pop es
    pop di
    pop ax
    ret

zm_pw16:
    push ax
    push di
    push es
    call zm_wrok
    jc .out
    inc ax
    call zm_wrok                    ; both halves, or neither: a word that
    dec ax                          ; DEC leaves CF alone - the answer survives
    jc .out
    mov es, [zf_sseg]
    mov di, ax
    mov [es:di], bh                 ; big-endian
    mov [es:di+1], bl
.out:
    pop es
    pop di
    pop ax
    ret

; =============================================================================
; zm_pb / zm_pw - write a byte or word anywhere
; in:  DX:AX = story address, BL / BX = the value
; out: nothing
;
; DX non-zero means the address is at or above 64KB, and dynamic memory ends
; below that by construction ([zf_static] is a header WORD) - so it is static
; or high memory and the Standard forbids the write. There is no slow path to
; write; refusing IS the slow path.
; =============================================================================
zm_pb:
    or dx, dx
    jz zm_pb16
    ret

zm_pw:
    or dx, dx
    jz zm_pw16
    ret

; =============================================================================
; zm_seek - point the live PC at a story address
; in:  DX:AX = story address
; out: ES:SI addresses it, with SI in 0..15
;
; SI is deliberately left tiny rather than taking zm_map's <64K arm: the PC
; walks forward with `lods` and wants the whole 32KB window ahead of it before
; zm_norm has to do anything.
; =============================================================================
; zm_addr sets ES:SI to an address WITHOUT claiming it is the program counter.
; Walking a Quetzal chunk or checksumming the image needs the arithmetic and
; must not disturb the PC (SPEC.md 61.3).
zm_addr:
    push ax
    push cx
    push dx
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [zf_sseg]
    mov es, ax
    pop dx
    pop cx
    pop ax
    ret

; zm_seek IS the PC mover, and it writes [zf_pcseg]/[zf_pcoff] as well as the
; registers. That memory pair is the AUTHORITY on where the program counter is
; (SPEC.md 61.3): ES is reloaded from it before every instruction, so a handler
; that clobbers ES - and every OSAPI call is free to, because the X stub hands
; the callee the caller's DS in ES - can no longer lose the program counter.
zm_seek:
    ; A SEEK OUTSIDE THE STORY IS REFUSED, not performed. Every way the PC can
    ; move - a call, a return, a jump, a branch - comes through here, so this
    ; is the one place that can name the address a mover actually asked for
    ; instead of the innocent opcode that runs afterwards at a nonsense PC.
    call zm_inimg
    jc .bad
    call zm_addr
    push ax
    mov ax, es
    mov [zf_pcseg], ax
    mov [zf_pcoff], si
    pop ax
    ret
.bad:
    mov [zx_badpc_l], ax
    mov [zx_badpc_h], dx
    mov byte [zx_badop], 0xFD       ; 0xFD = "a seek off the end of the story"
    push ax
    mov al, ZXE_BADOP
    call zx_halt
    pop ax
    ret

; zm_inimg - is DX:AX inside the story image?  out: CF=0 yes; preserves all
zm_inimg:
    cmp dx, [zf_slen_h]
    jb .yes
    ja .no
    cmp ax, [zf_slen_l]
    jb .yes
.no:
    stc
    ret
.yes:
    clc
    ret

; =============================================================================
; zm_norm - renormalise ES:SI after SI has walked past 0x8000
; in/out: ES:SI = the PC
; Preserves every other register. Called once per 32KB of straight-line code,
; which is why the test is a bare compare and not a table.
; =============================================================================
zm_norm:
    cmp si, 0x8000
    jb .out
    push ax
    sub si, 0x8000
    mov ax, es
    add ax, 0x800                   ; 0x8000 bytes = 0x800 paragraphs
    mov es, ax
    mov [zf_pcseg], ax              ; a PC mover: keep the authority current
    mov [zf_pcoff], si
    pop ax
.out:
    ret

; =============================================================================
; zm_pcaddr - the live ES:SI as a 20-bit story address
; out: DX:AX = the address
; Preserves every other register.
; =============================================================================
zm_pcaddr:
    push cx
    mov ax, es
    sub ax, [zf_sseg]               ; paragraph index, 0..0x7FFF for 512KB
    mov dx, ax
    mov cl, 12
    shr dx, cl                      ; paragraphs >> 12 = address bits 16..19
    mov cl, 4
    shl ax, cl                      ; paragraphs << 4, low 16 bits
    add ax, si
    adc dx, 0
    pop cx
    ret

; =============================================================================
; zm_unpack - a packed address to a byte address (Standard 1.2.3)
; in:  AX = packed address, BL = 0 for a routine / 1 for a string
; out: DX:AX = the byte address
;
;     2P          v1-3        4P + 8R_O   v6, v7 routines
;     4P          v4, v5      4P + 8S_O   v6, v7 print_paddr
;     8P          v8
;
; [zf_pshift] is 1/2/3 and carries the first column. The offsets are the only
; reason v7 costs anything at all over v5 - two header words and this add,
; which is why FROTZ-PLAN 2 says it comes for free.
; =============================================================================
zm_unpack:
    push cx
    xor dx, dx
    mov cl, [zf_pshift]
    or cl, cl
    jz .out                         ; no story loaded: 0 in, 0 out
.sh:
    shl ax, 1
    rcl dx, 1
    dec cl
    jnz .sh
    cmp byte [zf_ver], 6
    jb .out
    cmp byte [zf_ver], 7
    ja .out
    push bx
    push di
    or bl, bl
    jz .rout
    mov bx, [zf_soff]
    jmp short .scale
.rout:
    mov bx, [zf_roff]
.scale:
    xor di, di                      ; DI:BX = offset * 8, which needs 19 bits
    mov cl, 3
.s8:
    shl bx, 1
    rcl di, 1
    dec cl
    jnz .s8
    add ax, bx
    adc dx, di
    pop di
    pop bx
.out:
    pop cx
    ret

; =============================================================================
; zm_dynlen - the size of dynamic memory
; out: AX = [zf_static]
; =============================================================================
zm_dynlen:
    mov ax, [zf_static]
    ret

; -----------------------------------------------------------------------------
; zm_hgw - a big-endian word out of the header (INTERNAL)
; in:  BX = header offset, ES = the story segment
; out: AX = the word
; -----------------------------------------------------------------------------
zm_hgw:
    mov ah, [es:bx]
    mov al, [es:bx+1]
    ret

; =============================================================================
; zm_setup - parse and validate the header, then write our half of it back
; in:  [zf_sseg] holds the loaded story; [zf_skb] its claim in KB and
;      [zf_slen_l]/[zf_slen_h] the file's length if zi_load knew them (0 = it
;      did not, and the header's own length is taken instead)
; out: CF = 0 and AL = ZERR_NONE, every zf_* header field cached;
;      CF = 1 and AL = ZERR_* with the same code left in [zf_err]
;
; This is the only routine that decides what machine the story is running on,
; so it is also the only one that may refuse a story. It refuses rather than
; coping (SPEC.md 47): a Version we do not implement, a file longer than the
; Version permits or than the claim holds, or a header whose tables point
; outside the file are all facts, and a fact is what a refusal is allowed to
; be built on.
;
; The write-back is Standard 11.1's "Int" and "Rst" columns, and it is not
; optional - a v4+ story reads screen height out of the header to decide where
; to put its own status line, and a v5 story reads Flags 1 to decide whether to
; use colour at all. Getting this wrong does not fail, it misdraws.
; =============================================================================
zm_setup:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov bx, [zf_win]                ; DH = 4 or 1 bits per pixel, FOR THE CARD
    call OSAPI_WM_DISPLAY           ; THIS WINDOW IS ON (SPEC.md 39.16.4). The
                                    ; GRANULARITY is unchanged and deliberately
                                    ; so - Flags1 bit 0 is header state a story
                                    ; reads at startup and the Standard does not
                                    ; expect it to move under a running game
                                    ; (frotz.asm zf_onresize) - but a story
                                    ; loaded while the window already stands on
                                    ; the second card should be told about THAT
                                    ; card rather than about the primary
    mov [zm_bpp], dh

    mov es, [zf_sseg]

    ; --- Version, and the two things that follow from it alone --------------
    mov al, [es:ZMH_VER]
    mov [zf_ver], al
    or al, al
    jz .bad                         ; byte 0 of a story file is never 0
    cmp al, 8
    ja .ever
    mov ah, 1                       ; packed shift: 1 (v1-3), 2 (v4-7), 3 (v8)
    cmp al, 4
    jb .psh
    inc ah
    cmp al, 8
    jb .psh
    inc ah
.psh:
    mov [zf_pshift], ah
    mov cl, 1                       ; length divisor shift: 1, 2, 3 (11.1.6) -
    cmp al, 4                       ; NOT the same ladder: v6/v7 pack by 4 and
    jb .lsc                         ; store their length divided by 8
    mov cl, 2
    cmp al, 6
    jb .lsc
    mov cl, 3
.lsc:
    mov [zm_lsc], cl
    mov ax, 9                       ; object entry: 9 bytes v1-3, 14 from v4
    cmp byte [zf_ver], 4
    jb .osz
    mov ax, 14
.osz:
    mov [zf_objsz], ax

    ; --- the table addresses (Standard 11.1) --------------------------------
    mov bx, ZMH_HIMEM
    call zm_hgw
    mov [zf_himem], ax
    mov bx, ZMH_INITPC
    call zm_hgw
    mov [zf_initpc], ax
    mov bx, ZMH_DICT
    call zm_hgw
    mov [zf_dict], ax
    mov bx, ZMH_OBJTAB
    call zm_hgw
    mov [zf_objtab], ax
    mov [zf_propdef], ax            ; the property defaults ARE the head of the
                                    ; object table (Standard 12.1/12.2), cached
                                    ; under both names so neither reader is
                                    ; wrong; the object entries begin 62 bytes
                                    ; (v1-3) or 126 bytes (v4+) further on
    mov bx, ZMH_GVAR
    call zm_hgw
    mov [zf_gvar], ax
    mov bx, ZMH_STATIC
    call zm_hgw
    mov [zf_static], ax
    mov bx, ZMH_ABBREV
    call zm_hgw
    mov [zf_abbrev], ax
    mov bx, ZMH_CKSUM
    call zm_hgw
    mov [zm_ckhdr], ax

    cmp byte [zf_ver], 5            ; v5+ only; leaving these 0 in v1-4 is what
    jb .nov5                        ; tells ztext to use the built-in alphabet
    mov bx, ZMH_ALPHA
    call zm_hgw
    mov [zf_alpha], ax
    mov bx, ZMH_TERM
    call zm_hgw
    mov [zf_termtab], ax
    mov bx, ZMH_HDREXT
    call zm_hgw
    mov [zf_hdrext], ax
.nov5:
    cmp byte [zf_ver], 6            ; the routine/string offsets exist in v6
    jb .nov6                        ; and v7 and nowhere else (Standard 1.2.3)
    cmp byte [zf_ver], 7
    ja .nov6
    mov bx, ZMH_ROFF
    call zm_hgw
    mov [zf_roff], ax
    mov bx, ZMH_SOFF
    call zm_hgw
    mov [zf_soff], ax
.nov6:

    ; --- the length, and the two ceilings it has to clear -------------------
    mov bx, ZMH_FLEN
    call zm_hgw
    or ax, ax
    jz .lendone                     ; some early v3 files carry no length at
                                    ; all (Standard 11.1's "3+"); keep the one
                                    ; zi_load took from the directory entry
    xor dx, dx
    mov cl, [zm_lsc]
.fl:
    shl ax, 1
    rcl dx, 1
    dec cl
    jnz .fl
    mov [zf_slen_l], ax
    mov [zf_slen_h], dx
.lendone:
    mov ax, [zf_slen_l]
    mov dx, [zf_slen_h]
    mov cx, ax
    or cx, dx
    jz .nosize                      ; nobody knows the length; skip both checks
    add ax, 1023                    ; round up to whole KB, which is the unit
    adc dx, 0                       ; both the claim and the ceilings speak in
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx                       ; AX = length in KB
    mov cl, [zm_lsc]
    dec cl
    mov bx, 128                     ; Standard 1.1.4: 128KB v1-3, 256KB v4-5,
    shl bx, cl                      ; 512KB v6-8 - the same ladder as the
    cmp ax, bx                      ; length divisor, which is why it is reused
    ja .bad
    mov bx, [zf_skb]
    or bx, bx
    jz .nosize
    cmp ax, bx
    ja .ebig
.nosize:

    ; --- do the tables point inside the file? -------------------------------
    ; All four are header words, so all four are below 64KB; a file of 64KB or
    ; more therefore cannot have one outside it and the compare is skipped.
    cmp word [zf_static], 64        ; dynamic memory must hold the header
    jb .bad                         ; (Standard 1.1: at least 64 bytes)
    cmp word [zf_gvar], 64
    jb .bad
    cmp word [zf_objtab], 64
    jb .bad
    cmp word [zf_dict], 64
    jb .bad
    cmp word [zf_slen_h], 0
    jne .tabok
    mov ax, [zf_slen_l]
    or ax, ax
    jz .tabok
    cmp [zf_dict], ax
    jae .bad
    cmp [zf_objtab], ax
    jae .bad
    cmp [zf_gvar], ax
    jae .bad
    cmp [zf_static], ax
    ja .bad
.tabok:

    ; --- Flags 1 (Standard 11.1): what this interpreter can actually do -----
    mov al, [es:ZMH_FLAGS1]
    cmp byte [zf_ver], 4
    jae .f1v4
    and al, 10101111b               ; bit 4: we DO draw a status line, so the
                                    ;        "not available" bit comes down
                                    ; bit 6: the kernel font is fixed 8x8
                                    ;        (SPEC.md 6), never variable-pitch
    or al, 00100000b                ; bit 5: screen splitting, which zw_split is
    jmp short .f1done
.f1v4:
    and al, 01000010b               ; bits 6 AND 1 are the story's here; the
                                    ; rest of the byte is ours to answer.
                                    ;
                                    ; BIT 1 IS "pictures available" ONLY IN v6
                                    ; (Standard 11.1 marks it, and bit 5, v6),
                                    ; and Inform's library reads that same bit
                                    ; in EVERY version to choose between a
                                    ; score/turns status line and a clock -
                                    ; which is what `Statusline time;` sets.
                                    ; Clearing it turned 905.Z5, whose header
                                    ; arrives with it set, into a score game:
                                    ; the reference prints "Time: 9:05 am" and
                                    ; this printed "Moves: 0", from the STORY's
                                    ; own status line, on the story's own
                                    ; reading of a byte we had overwritten. The
                                    ; v1-3 path above always preserved it; this
                                    ; one did not. v6 still sets it below, and
                                    ; there it is genuinely ours.
    or al, 00011100b                ; bits 2,3,4: bold, italic and fixed-space
                                    ; are all available - as reverse video,
                                    ; underline and "no change", which is what
                                    ; one 8x8 font can honestly offer
                                    ; (docs/FROTZ-PLAN.md 5.1)
    cmp byte [zm_bpp], 4
    jne .f1nc
    or al, 00000001b                ; bit 0: colours, on VGA only. On a 1bpp
.f1nc:                              ; adapter every colour rounds to black,
                                    ; white or a dither (SPEC.md 39.4) and a
                                    ; story that colours by meaning would go
                                    ; unreadable - so we say we cannot.
    cmp byte [zf_ver], 6
    jne .f1done
    or al, 00100010b                ; bits 1,5: pictures and sound, v6 only
.f1done:
    mov [es:ZMH_FLAGS1], al
    mov [zf_flags1], al

    ; --- Flags 2: the story asks, and the interpreter clears what it cannot -
    mov al, [es:ZMH_FLAGS2 + 1]
    and al, 11111010b               ; bit 0: transcription starts off, and this
                                    ;        is the "Rst" the Standard means by
                                    ;        surviving a restart (11.1.2)
                                    ; bit 2: no screen redraw is pending yet
    cmp byte [zf_ver], 6
    je .f2w
    and al, 11010111b               ; bits 3,5: pictures and the mouse are the
                                    ; v6 window model's, and outside it nothing
                                    ; here can answer @draw_picture or
                                    ; @read_mouse. Left set in v6 for zpic.inc
                                    ; to clear if no archive is beside the story
.f2w:
    mov [es:ZMH_FLAGS2 + 1], al
    mov al, [es:ZMH_FLAGS2]
    and al, 11111110b               ; bit 8: menus. There are none.
    mov [es:ZMH_FLAGS2], al

    ; --- who we are, and which Standard we obey -----------------------------
    cmp byte [zf_ver], 4
    jb .noint
    mov byte [es:ZMH_INTNUM], ZM_INTNUM
    mov byte [es:ZMH_INTVER], ZM_INTVER
.noint:
    mov byte [es:ZMH_STDREV], 1     ; Standard 11.1.5: n and m of Revision n.m
    mov byte [es:ZMH_STDREV + 1], 1
    cmp byte [zf_ver], 5
    jb .nocol
    mov byte [es:ZMH_DEFBG], ZM_COL_WHITE
    mov byte [es:ZMH_DEFFG], ZM_COL_BLACK
.nocol:

    ; --- the screen, so the header is never garbage before zwin runs --------
    ; zw_setup computes the real grid and calls zm_hdr_screen again; this is a
    ; first answer from the live window rather than a guess, because a v4 story
    ; may read $20 before it has printed anything at all.
    mov bx, [zf_win]
    or bx, bx
    jz .defg
    call OSAPI_WM_GEOM              ; CX/DX = content w/h; CF=1 = not visible
    jnc .gotg
.defg:
    mov cx, ZF_WIN_W - 2            ; the template's own content box
    mov dx, ZF_WIN_H - (TITLE_H + 1)
.gotg:
    mov [zm_scw], cx
    mov [zm_sch], dx
    mov ax, cx
    sub ax, ZF_MARGIN * 2 + ZF_SBW
    jnc .cols
    xor ax, ax
.cols:
    mov cl, 3
    shr ax, cl                      ; whole 8px cells across
    or ax, ax
    jnz .colsok
    mov ax, 1
.colsok:
    mov bx, ax                      ; BX = columns
    mov ax, [zm_sch]
    xor dx, dx
    mov cx, ZF_LEAD
    div cx                          ; AX = rows
    or ax, ax
    jnz .rowsok
    mov ax, 1
.rowsok:
    mov cx, [zm_scw]
    mov dx, [zm_sch]
    call zm_hdr_screen

    ; --- the header extension (Standard 11.1.7) -----------------------------
    cmp byte [zf_ver], 5
    jb .done
    mov bx, [zf_hdrext]
    or bx, bx
    jz .done
    call zm_hgw                     ; word 0 = how many words follow
    cmp ax, 4
    jb .done
    mov bx, [zf_hdrext]
    add bx, 8                       ; word 4 = Flags 3
    mov byte [es:bx], 0             ; 11.1.7.4.1: every unused bit cleared, and
    mov byte [es:bx + 1], 0         ; bit 0 (transparency) is one we cannot do
    cmp ax, 6
    jb .done
    mov byte [es:bx + 2], 0         ; word 5 = true default foreground, black
    mov byte [es:bx + 3], 0
    mov byte [es:bx + 4], 0x7F      ; word 6 = true default background, white
    mov byte [es:bx + 5], 0xFF      ; ($7fff is white in sRGB, Standard 8.3.5)

.done:
    mov al, ZERR_NONE
    mov [zf_err], al
    clc
    jmp short .out
.ever:
    mov al, ZERR_VER
    jmp short .fail
.ebig:
    mov al, ZERR_BIG
    jmp short .fail
.bad:
    mov al, ZERR_BAD
.fail:
    mov [zf_err], al
    stc                             ; POP touches no flags, so CF is the answer
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; zm_hdr_screen - write the screen's dimensions into the header
; in:  AX = height in lines, BX = width in characters,
;      CX = width in units, DX = height in units
; out: nothing; preserves all registers
;
; Standard 8.4: "The interpreter may change the exact dimensions whenever it
; likes but must write the current height (in lines) and width (in characters)
; into bytes $20 and $21." So zwin.inc calls this again after every resize -
; the numbers are its, the header layout is ours, and neither module has to
; know the other's arithmetic.
;
; Units are PIXELS here (Standard 8.4.2 leaves the choice open for v5 and
; later, and v6 addresses its windows in pixels, so anything else would need
; two coordinate systems).
; =============================================================================
zm_hdr_screen:
    push ax
    push bx
    push cx
    push dx
    push es
    mov es, [zf_sseg]
    cmp byte [zf_ver], 4
    jb .out                         ; v1-3 have no screen fields at all
    cmp ax, 255
    jbe .r                          ; 255 means "infinite" (8.4.1) - clamp
    mov ax, 255                     ; below it rather than wrapping into it
.r:
    mov [es:ZMH_SCRH], al
    cmp bx, 255
    jbe .c
    mov bx, 255
.c:
    mov [es:ZMH_SCRW], bl
    cmp byte [zf_ver], 5
    jb .out
    mov [es:ZMH_SCRWU], ch          ; big-endian words
    mov [es:ZMH_SCRWU + 1], cl
    mov [es:ZMH_SCRHU], dh
    mov [es:ZMH_SCRHU + 1], dl
    mov al, CELL_W                  ; v5: $26 is width, $27 is height.
    mov ah, ZF_LEAD                 ; v6: the other way round (Standard 8.1.1)
    cmp byte [zf_ver], 6
    jne .fnt
    xchg al, ah
.fnt:
    mov [es:ZMH_FONT26], al
    mov [es:ZMH_FONT27], ah
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zm_checksum - sum the story image the way @verify wants it
; out: AX = the computed checksum, BX = the one in the header,
;      CF = 0 iff they agree
;
; Standard 11.1 / the @verify opcode: the unsigned sum mod 65536 of every byte
; from $40 to the file length. It walks the whole image, which on the target
; machine is seconds for a big v8 story - that is inherent (the sum is defined
; over every byte) and it is why @verify is the only caller.
; =============================================================================
zm_checksum:
    push cx
    push dx
    push si
    push di
    push es
    xor di, di                      ; the running sum
    mov ax, [zf_slen_l]
    mov dx, [zf_slen_h]
    mov cx, ax
    or cx, dx
    jz .done                        ; no length: nothing to verify against
    sub ax, 0x40
    sbb dx, 0                       ; DX:AX = bytes still to add
    push ax
    push dx
    mov ax, 0x40
    xor dx, dx
    call zm_addr                    ; ES:SI = byte $40, and NOT the PC
    pop dx
    pop ax
.loop:
    mov cx, ax
    or cx, dx
    jz .done
    mov cl, [es:si]
    xor ch, ch
    inc si
    add di, cx                      ; mod 65536 is the natural width here
    sub ax, 1
    sbb dx, 0
    cmp si, 0x8000
    jb .loop
    call zm_norm                    ; once per 32KB, exactly as the PC does
    jmp short .loop
.done:
    mov ax, di
    mov bx, [zm_ckhdr]
    cmp ax, bx
    je .same
    stc
    jmp short .out
.same:
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

; =============================================================================
; zm_errmsg - the sentence behind a ZERR_* code
; in:  AL = ZERR_*
; out: SI = a NUL string in our segment, ready for zw_notice or zt_puts
;
; It exists so that a refusal says WHY on the screen (SPEC.md 47). A code with
; no sentence is a code the user reads as a hang.
; =============================================================================
zm_errmsg:
    push bx
    mov bl, al
    xor bh, bh
    cmp bx, ZERR_MAX
    jbe .ok
    xor bx, bx
.ok:
    shl bx, 1
    mov si, [zm_errtab + bx]
    pop bx
    ret

zm_errtab:
    dw zm_e0, zm_e1, zm_e2, zm_e3, zm_e4
zm_e0: db 'Frotz: an internal error stopped the story.', 0
zm_e1: db 'This is a Z-machine version Frotz does not play.', 0
zm_e2: db 'This story needs more memory than this machine has.', 0
zm_e3: db 'That file is not a Z-machine story file.', 0
zm_e4: db 'Not enough free memory to open this story.', 0
