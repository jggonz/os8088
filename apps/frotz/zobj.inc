; =============================================================================
; os8088 - apps/frotz/zobj.inc
;
; The object tree: attributes, the parent/sibling/child links, and the
; property tables (Z-Machine Standard 12). Every @test_attr, @insert_obj,
; @get_prop and @jin in the story lands here.
;
; ONE FACT SHAPES THIS WHOLE FILE: the object table lives in DYNAMIC MEMORY
; (Standard 12.1), and so does every property table (12.4). Dynamic memory
; ends at [zf_static] and the Standard caps it below 64KB - so every address
; this module forms is SIXTEEN BITS, and it uses zm_gb16/zm_gw16/zm_pb16/
; zm_pw16 throughout rather than the 20-bit DX:AX pair. That is not an
; optimisation taken on faith: it is what "in dynamic memory" means, and it
; is why the three ABI entry points that hand a story address back to a
; caller (zo_addr, zo_name, zo_proplen) can always answer with DX = 0.
;
; THE SECOND DECISION: every layout number here is derived from [zf_ver] and
; from nothing else. There are six of them - entry size, attribute bytes,
; the offset of the tree past the defaults, the defaults count, the width of
; an object number, and the property-size encoding - and they change together
; at v4 or not at all. [zf_objsz] caches the same 9/14 that zo_isv3 decides;
; taking five numbers from the version and the sixth from a cache is exactly
; how the two drift apart on the story where it matters. One test settles all
; six (Standard 12.3.1 vs 12.3.2, 12.4.1 vs 12.4.2).
;
; [zf_objtab] is the header's word at $0a - the address of the WHOLE object
; table, property defaults first (12.1, 12.2). The defaults are therefore at
; [zf_objtab] and the tree at [zf_objtab] + 62 (v1-3) or + 126 (v4+).
;
; BOUNDS ARE DELIBERATE, NOT DEFENSIVE HABIT. Object 0 is not an object - it
; is the Standard's word for "nothing" (12.3) - and stories ask about it all
; the time, because get_parent of a top-level object answers 0 and the next
; opcode passes that straight back in. Every entry point here answers 0 / does
; nothing for object 0, for an object number the version cannot express, and
; for an entry that would fall outside dynamic memory. It never reads or
; writes outside the table to find that out. The Standard's own remark that
; Sherlock sets attribute 48 is the same problem one level down, and gets the
; same answer: out of range, do nothing, do not scribble on the next object.
;
; Well-foundedness (12.5) is the GAME's responsibility and the interpreter is
; not required to check it. This module does not check it - but it does not
; hang on a story that got it wrong either: the one unbounded walk (the child
; chain in zo_remove) carries a counter, because a sibling cycle would
; otherwise spin the worker task forever with the gfx lock free and no way
; back to the desktop.
; =============================================================================

; --- private state, from this module's own bss range (zbss.inc part 10) ------
; zo_remove needs to hold three object numbers across five calls and the
; worker's stack is 384 bytes shared with the tick, mouse and sound IRQs
; (docs/plans/completed/FROTZ-PLAN.md 4) - so they live here rather than on SP. Nothing is
; re-entrant: the VM is one task and zo_insert's call to zo_remove finishes
; before zo_insert reads any of them back.
%define zo_obj    (os88_image_end + ZOBJ_SLACK + 0)   ; word: the object moving
%define zo_par    (os88_image_end + ZOBJ_SLACK + 2)   ; word: its parent
%define zo_sib    (os88_image_end + ZOBJ_SLACK + 4)   ; word: its sibling

; -----------------------------------------------------------------------------
; zo_isv3 - which of the two object layouts this story uses
; in:  nothing
; out: CF = 1 for v1-3 (9-byte entries, 32 attributes, 1-byte object numbers,
;      31 property defaults, one-byte property sizes); CF = 0 for v4+
;      (14-byte entries, 48 attributes, 2-byte numbers, 63 defaults, the
;      two-byte size form). Every register preserved.
;
; Six numbers, one test, called from everything. See the banner.
; -----------------------------------------------------------------------------
zo_isv3:
    cmp byte [zf_ver], 4                ; CF = 1 exactly when the version is 1-3
    ret

; -----------------------------------------------------------------------------
; zo_ent16 - the byte address of an object's entry in the tree
; in:  AX = object number
; out: CF = 0 and AX = the address; CF = 1 and AX = 0 if there is no such
;      object. All other registers preserved.
;
; The three ways an object number can be wrong, and why each is refused here
; rather than by the caller:
;
;   0            The Standard's "nothing" (12.3). It reaches this routine on
;                every walk that runs off the end of a chain.
;   > 255 in v3  The links are one byte wide (12.3.1), so v1-3 cannot name it;
;                only a literal operand can, and it would index into whatever
;                follows the table.
;   past static  The entry has to lie in dynamic memory (12.1). This catches
;                both an absurd object number in v4+ (where the multiply
;                overflows a word long before) and a story whose object table
;                pointer is not one.
; -----------------------------------------------------------------------------
zo_ent16:
    push bx
    push cx
    push dx
    or ax, ax
    jz .bad                             ; object 0 is not an object
    mov cx, 14                          ; v4+: 14 bytes an entry (12.3.2)
    mov bx, 126                         ; ...past 63 default words (12.2)
    call zo_isv3
    jnc .go
    cmp ax, 255                         ; v1-3 hold an object number in a byte
    ja .bad
    mov cx, 9                           ; v1-3: 9 bytes an entry (12.3.1)
    mov bx, 62                          ; ...past 31 default words
.go:
    dec ax                              ; objects are numbered from 1 (12.3)
    mul cx                              ; DX:AX = (obj-1) * entry size
    or dx, dx
    jnz .bad                            ; over 64KB: dynamic memory cannot hold it
    add ax, bx
    jc .bad
    add ax, [zf_objtab]
    jc .bad
    mov dx, ax
    add dx, cx                          ; the LAST byte of the entry must fit too
    jc .bad
    cmp dx, [zf_static]
    ja .bad
    clc
    jmp short .out
.bad:
    xor ax, ax
    stc
.out:
    pop dx                              ; pop touches no flags: CF is the answer
    pop cx
    pop bx
    ret

; =============================================================================
; zo_addr - the public form of the above (SPEC.md 61.3)
; in:  AX = object number
; out: DX:AX = the entry's story address, or 0 if there is no such object
;
; DX is always zero. The object table is in dynamic memory (12.1) and this is
; the seam where that stops being an internal assumption and becomes something
; another module can rely on.
; =============================================================================
zo_addr:
    call zo_ent16
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; zo_rel - read one of the three tree links
; in:  AX = object number, BL = 0 parent / 1 sibling / 2 child
; out: AX = the object number found there (0 = none). Everything else kept.
;
; The links sit after the attribute flags: bytes 4/5/6 of a 9-byte entry in
; v1-3, words at 6/8/10 of a 14-byte entry in v4+ (12.3.1, 12.3.2). A missing
; object answers 0, which is the same answer the Standard gives for a link
; that is genuinely empty - the caller wants "nothing" either way.
; -----------------------------------------------------------------------------
zo_rel:
    push bx
    push cx
    push dx
    call zo_ent16
    jc .none
    mov cx, ax                          ; CX = the entry
    mov al, bl
    xor ah, ah                          ; AX = which link, 0..2
    call zo_isv3
    jc .v3
    shl ax, 1                           ; v4+: three WORDS at offset 6
    add ax, 6
    add ax, cx
    call zm_gw16
    jmp short .out
.v3:
    add ax, 4                           ; v1-3: three BYTES at offset 4
    add ax, cx
    call zm_gb16
    xor ah, ah                          ; zm_gb16 answers in AL alone
    jmp short .out
.none:
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zo_setrel - write one of the three tree links
; in:  AX = object number, BL = 0 parent / 1 sibling / 2 child, CX = value
; out: nothing; every register preserved
;
; Silently does nothing for an object that is not one - see the banner. In
; v1-3 only the low byte of CX is stored, because that is the whole field.
; The write itself goes through zm_pb16/zm_pw16, which own the dynamic-memory
; guard; this routine has already proved the entry is inside it anyway.
; -----------------------------------------------------------------------------
zo_setrel:
    push ax
    push bx
    push cx
    push dx
    call zo_ent16
    jc .out
    mov dx, ax                          ; DX = the entry
    mov al, bl
    xor ah, ah
    mov bx, cx                          ; BX/BL = the value the zm_p* want
    call zo_isv3
    jc .v3
    shl ax, 1
    add ax, 6
    add ax, dx
    call zm_pw16
    jmp short .out
.v3:
    add ax, 4
    add ax, dx
    call zm_pb16
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zo_parent / zo_sibling / zo_child
; in:  AX = object number
; out: AX = the object number there, 0 = none; everything else preserved
;
; @get_parent has no branch; @get_sibling and @get_child branch on non-zero
; (Standard 15), and that is zexec's business, not this module's.
; =============================================================================
zo_parent:
    push bx
    mov bl, 0
    call zo_rel
    pop bx
    ret

zo_sibling:
    push bx
    mov bl, 1
    call zo_rel
    pop bx
    ret

zo_child:
    push bx
    mov bl, 2
    call zo_rel
    pop bx
    ret

; -----------------------------------------------------------------------------
; zo_abit - locate one attribute flag
; in:  AX = object number, BL = attribute number
; out: CF = 0 and AX = the byte's address, DL = the bit's mask;
;      CF = 1 = there is nothing to do. AX and DX are the only registers
;      touched.
;
; Attributes are stored TOPMOST BIT FIRST (12.3.1): attribute 0 is bit 7 of
; the first byte, attribute 31 is bit 0 of the fourth. v4+ has 48 of them in
; six bytes (12.3.2).
;
; An attribute past the end is refused rather than clamped. The Standard's own
; remark records that Infocom's Sherlock sets and clears attribute 48, one
; past its last - clamping would let it flip a real flag, and letting the
; arithmetic run would put the bit in the NEXT OBJECT'S parent field.
; -----------------------------------------------------------------------------
zo_abit:
    push bx
    push cx
    mov cl, 32                          ; 32 attribute flags in v1-3
    call zo_isv3
    jc .n
    mov cl, 48                          ; 48 in v4+
.n:
    cmp bl, cl
    jae .bad
    call zo_ent16                       ; AX = the entry; the flags start there
    jc .bad
    mov dl, 0x80
    mov cl, bl
    and cl, 7
    shr dl, cl                          ; attribute 0 is the TOP bit
    xor bh, bh
    mov cl, 3
    shr bx, cl                          ; BX = attr >> 3 = which flag byte
    add ax, bx
    clc
    jmp short .out
.bad:
    stc
.out:
    pop cx
    pop bx
    ret

; =============================================================================
; zo_attr - @test_attr
; in:  AX = object number, BL = attribute number
; out: CF = the bit; every register preserved
;
; A missing object or attribute answers CLEAR. The Standard has no value for
; "attribute of nothing", and false is the answer that lets a story's own
; guard clauses work rather than the one that makes them fire.
; =============================================================================
zo_attr:
    push ax
    push dx
    call zo_abit
    jc .no
    call zm_gb16
    test al, dl
    jz .no
    pop dx
    pop ax
    stc
    ret
.no:
    pop dx
    pop ax
    clc
    ret

; =============================================================================
; zo_attr_set / zo_attr_clr - @set_attr, @clear_attr
; in:  AX = object number, BL = attribute number
; out: nothing; every register preserved
;
; One body: the two differ by a single OR against an AND, and a second copy
; of the read-modify-write is a second place to get the top-bit-first order
; wrong.
; =============================================================================
zo_attr_set:
    push cx
    mov cl, 1
    jmp short zo_attrw
zo_attr_clr:
    push cx
    xor cl, cl
zo_attrw:
    push ax
    push bx
    push dx
    call zo_abit                        ; AX = the byte, DL = the mask
    jc .out
    push ax
    call zm_gb16
    or cl, cl
    jz .clr
    or al, dl
    jmp short .st
.clr:
    not dl                              ; NOT leaves the flags alone
    and al, dl
.st:
    mov bl, al                          ; zm_pb16 takes the value in BL
    pop ax
    call zm_pb16
.out:
    pop dx
    pop bx
    pop ax
    pop cx
    ret

; -----------------------------------------------------------------------------
; zo_ptab - an object's property table
; in:  AX = object number
; out: CF = 0 and AX = the table's byte address; CF = 1 and AX = 0 if there
;      is no such object or the pointer is not one. Everything else preserved.
;
; The pointer is the last field of the entry - byte 7 of 9 in v1-3, the word
; at 12 of 14 in v4+ (12.3.1, 12.3.2). A game may legally repoint it in play
; (12.4), so it is read every time and never cached.
;
; Address 0 is refused because that is the story header, not a property table.
; It is what a zeroed or half-built entry looks like, and walking a property
; list out of the header would answer plausible nonsense for pages.
; -----------------------------------------------------------------------------
zo_ptab:
    push bx
    call zo_ent16
    jc .bad
    mov bx, 12                          ; v4+: the word at offset 12
    call zo_isv3
    jnc .n
    mov bl, 7                           ; v1-3: the word at offset 7
.n:
    add ax, bx
    call zm_gw16
    or ax, ax
    jz .bad
    clc
    jmp short .out
.bad:
    xor ax, ax
    stc
.out:
    pop bx
    ret

; =============================================================================
; zo_name - an object's short name
; in:  AX = object number
; out: DX:AX = the Z-string's address, or 0 if there is no such object
;
; The property table opens with a text-length byte and then the name itself
; (12.4), so the string starts one byte in. The length byte counts WORDS and
; is not needed to print - the Z-string's own top bit terminates it - which is
; why this hands back an address and lets zt_print do the rest.
; =============================================================================
zo_name:
    call zo_ptab
    jc .none
    inc ax                              ; past the text-length byte
.none:
    xor dx, dx                          ; dynamic memory: the high half is 0
    ret

; -----------------------------------------------------------------------------
; zo_pfirst - the first property's size byte
; in:  AX = object number
; out: CF = 0 and AX = the address of the first size byte; CF = 1 and AX = 0.
;      Everything else preserved.
;
; = table + 1 + 2 * text-length. The name is "some even number of bytes" and
; the header byte counts them in pairs (12.4).
; -----------------------------------------------------------------------------
zo_pfirst:
    push bx
    push cx
    call zo_ptab
    jc .bad
    mov bx, ax
    call zm_gb16                        ; AL = the name's length, in WORDS
    mov cl, al
    xor ch, ch
    shl cx, 1
    lea ax, [bx+1]
    add ax, cx
    clc
    jmp short .out
.bad:
    xor ax, ax
    stc
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zo_psize - decode one property's size-and-number header
; in:  AX = the address of the first size byte
; out: AL = the property number, 0 = the end of the list
;      CX = the data length in bytes
;      BX = the address of the data itself
;      AH is clobbered; everything else preserved.
;
; The two encodings (12.4.1, 12.4.2) are the largest single difference between
; the versions inside this file:
;
;   v1-3   one byte, 32*(length-1) + number. Length is 1..8, number 1..31, and
;          a size byte of 0 ends the list - which is why a multiple of 32 is
;          otherwise illegal.
;   v4+    number in the bottom 6 bits. Bit 7 clear: one byte, and bit 6 says
;          length 1 or 2 (12.4.2.2). Bit 7 set: a SECOND byte carries the
;          length in its bottom 6 bits, and a length of 0 there means 64
;          (12.4.2.1.1 - Inform compiles such properties).
;
; Bit 6 of that second byte is undetermined: Infocom's files set it, Inform's
; clear it, so it is masked off rather than tested.
; -----------------------------------------------------------------------------
zo_psize:
    mov bx, ax
    call zm_gb16                        ; AL = the first size byte
    or al, al
    jz .end
    call zo_isv3
    jnc .v4
    ; --- v1-3: 32 * (length - 1) + number --------------------------------
    mov ah, al
    mov cl, 5
    shr ah, cl
    inc ah                              ; AH = the length, 1..8
    mov cl, ah
    xor ch, ch
    and al, 31                          ; AL = the number, 1..31
    inc bx                              ; the data follows the one size byte
    ret
    ; --- v4+ -------------------------------------------------------------
.v4:
    mov ah, al
    and al, 63                          ; the number is the bottom 6 bits
    inc bx
    test ah, 0x80
    jnz .two
    mov cx, 1
    test ah, 0x40                       ; bit 6: clear = 1 byte, set = 2
    jz .out1
    mov cx, 2
.out1:
    ret
.two:
    push ax
    mov ax, bx                          ; BX already points at the second byte
    call zm_gb16
    and al, 63                          ; bit 6 is undetermined; bit 7 always set
    mov cl, al
    xor ch, ch
    or cx, cx
    jnz .nz
    mov cx, 64                          ; a stored 0 means 64
.nz:
    pop ax
    inc bx                              ; the data follows BOTH size bytes
    ret
.end:
    xor al, al
    xor cx, cx
    ret

; =============================================================================
; zo_proplen - @get_prop_len
; in:  DX:AX = the address of a property's DATA
; out: AX = its length in bytes; everything else preserved
;
; This is the one routine that reads the size field BACKWARDS, and the reason
; the Standard insists the second size byte has its top bit set: given only
; the address of the data, the byte before it is either a v1-3 size byte, a
; v4+ single size byte (top bit clear), or a v4+ SECOND size byte (top bit
; set), and the three are told apart by that bit alone.
;
; @get_prop_len 0 must answer 0. Some Infocom games and files from old Inform
; versions require it (Standard 15), so it is a contract, not a guard.
;
; DX is ignored: property tables are in dynamic memory (12.4), so the address
; is sixteen bits and the caller's high half is zero by construction.
; =============================================================================
zo_proplen:
    push bx
    push cx
    push dx
    or ax, ax
    jz .zero
    dec ax
    call zm_gb16                        ; AL = the size byte before the data
    call zo_isv3
    jnc .v4
    mov cl, 5
    shr al, cl
    xor ah, ah
    inc ax                              ; 32*(length-1) + number
    jmp short .out
.v4:
    test al, 0x80
    jz .one
    and al, 63                          ; the second size byte carries the length
    xor ah, ah
    or ax, ax
    jnz .out
    mov ax, 64                          ; 0 there means 64
    jmp short .out
.one:
    test al, 0x40                       ; MOV does not touch the flags
    mov ax, 1
    jz .out
    mov ax, 2
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; zo_propaddr - @get_prop_addr
; in:  AX = object number, BL = property number
; out: AX = the byte address of the property's data, 0 = the object has not
;      got it; everything else preserved
;
; Properties are listed in DESCENDING numerical order and the Standard says
; that order is essential rather than conventional (12.4). This walk uses it:
; the moment a number smaller than the one wanted appears, the property is
; absent and the rest of the list cannot bring it back.
;
; The walk stops on a carry rather than on a bound. A property list that has
; been corrupted terminates at the first zero byte, and each step advances by
; at least two bytes, so it cannot loop; the only way out of dynamic memory is
; to wrap 16 bits, and that is what the carry catches. Bounding by [zf_static]
; instead would refuse any story that put a property table above it, which the
; Standard does not forbid a file from doing by accident and which no
; interpreter refuses today.
; =============================================================================
zo_propaddr:
    push bx
    push cx
    push dx
    mov dl, bl                          ; DL = the property wanted
    call zo_pfirst
    jc .none
.loop:
    call zo_psize                       ; AL = number, CX = length, BX = data
    or al, al
    jz .none                            ; the end of the list
    cmp al, dl
    je .hit
    jb .none                            ; descending order: we are past it
    mov ax, bx
    add ax, cx                          ; the next size byte
    jnc .loop
    jmp short .none
.hit:
    mov ax, bx
    jmp short .out
.none:
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; zo_getprop - @get_prop
; in:  AX = object number, BL = property number
; out: AX = the value, or the property default if the object has not got it;
;      everything else preserved
;
; Length 1 gives the byte, length 2 the word; a length over 2 is illegal for
; this opcode and the result is unspecified (Standard 15), so it gives the
; first word, which is what every other interpreter gives and what a story
; relying on the accident would have been written against.
;
; An object number that is not one falls through to the default, which is the
; same answer as an object that simply lacks the property. That is deliberate:
; @get_prop 0 turns up whenever a story reads a property of get_parent's
; answer without checking it first, and a default is a value the story can
; survive.
; =============================================================================
zo_getprop:
    push bx
    push cx
    push dx
    mov dl, bl                          ; keep the number for the default
    call zo_propaddr
    or ax, ax
    jz .def
    mov cx, ax                          ; CX = the data address
    push dx
    xor dx, dx                          ; zo_proplen takes a 20-bit DX:AX
    call zo_proplen
    pop dx
    xchg ax, cx                         ; AX = the address, CX = the length
    cmp cx, 1
    jne .word
    call zm_gb16
    xor ah, ah
    jmp short .out
.word:
    call zm_gw16
    jmp short .out
    ; --- the property defaults table (12.2) ------------------------------
    ; 31 words in v1-3, 63 in v4+, at the head of the object table, indexed
    ; by property number from 1. A number outside that range has no default
    ; and answers 0 rather than reading the first object's flags.
.def:
    mov cl, 31
    call zo_isv3
    jc .dn
    mov cl, 63
.dn:
    or dl, dl
    jz .zero
    cmp dl, cl
    ja .zero
    mov al, dl
    xor ah, ah
    dec ax
    shl ax, 1
    add ax, [zf_objtab]
    call zm_gw16
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; zo_putprop - @put_prop
; in:  AX = object number, BL = property number, CX = value
; out: nothing; every register preserved
;
; A length-1 property takes the LOW BYTE of the value, so storing -1 leaves
; 255 behind (Standard 15). Over 2 bytes the opcode is undefined and this
; writes the first word, matching zo_getprop's read.
;
; The Standard says an interpreter should halt with an error if the property
; does not exist. This does nothing instead. There is no error channel in this
; ABI - [zf_err] belongs to zexec, which is the module that can also say WHICH
; opcode did it - and a story that survives a lost write is better served than
; one killed at the point the interpreter noticed.
; =============================================================================
zo_putprop:
    push ax
    push bx
    push cx
    push dx
    mov dx, cx                          ; DX = the value; CX is needed below
    call zo_propaddr
    or ax, ax
    jz .out                             ; no such property: write nothing
    mov cx, ax                          ; CX = the data address
    push dx
    xor dx, dx
    call zo_proplen                     ; AX = the length
    pop dx
    mov bx, dx                          ; BX/BL = the value the zm_p* want
    xchg ax, cx                         ; AX = the address, CX = the length
    cmp cx, 1
    jne .word
    call zm_pb16
    jmp short .out
.word:
    call zm_pw16
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zo_nextprop - @get_next_prop
; in:  AX = object number, BL = property number (0 = ask for the first)
; out: AX = the next property number, 0 = the end of the list. AH is zero, so
;      the caller can store the word straight. Everything else preserved.
;
; Asking for the next property of one the object has not got is illegal and
; the Standard says an interpreter should halt if it can check efficiently.
; The check IS efficient here - the walk has to find the property anyway - but
; this answers 0 instead, for zo_putprop's reason: a lost answer beats a dead
; interpreter, and zexec owns the fatal-error path if it ever wants one.
; =============================================================================
zo_nextprop:
    push bx
    push cx
    push dx
    mov dl, bl
    call zo_pfirst
    jc .none
    or dl, dl
    jz .first                           ; 0 asks for the first property
.loop:
    call zo_psize                       ; AL = number, CX = length, BX = data
    or al, al
    jz .none
    cmp al, dl
    je .after
    jb .none                            ; descending order: it is not here
    mov ax, bx
    add ax, cx
    jnc .loop
    jmp short .none
.after:
    mov ax, bx
    add ax, cx                          ; the size byte of the one after it
    jc .none
.first:
    call zo_psize                       ; AL = its number, or 0 at the end
    jmp short .out
.none:
    xor al, al
.out:
    xor ah, ah
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; zo_remove - @remove_obj
; in:  AX = object number
; out: nothing; every register preserved
;
; Detach the object from its parent. Its own children come with it (Standard
; 15) - they are reached through it, so there is nothing to do for them.
;
; THE TREE HAS NO BACK POINTERS. An object knows its parent, its first child
; and its next sibling, and nothing knows its PREVIOUS sibling (12.3.1) - so
; unlinking one from the middle of a chain means walking the parent's child
; list from the top to find who points at it. That walk is the whole cost of
; this routine and the reason zo_insert is written to call it rather than to
; repeat it.
;
; If the chain runs out without the object in it, the tree was not well
; founded (12.5). The object is still unlinked, and no chain is rewritten: a
; guessed repair here would corrupt an unrelated object's sibling list, which
; is a bug that surfaces a hundred turns later.
;
; The counter on the walk is not paranoia about the story files this ships
; with. A sibling cycle in a corrupt file would spin the VM worker forever
; with the gfx lock free - a machine that looks alive, redraws, and never
; advances the game. There are at most 65535 objects (12.3.2), so the bound
; cannot cut a walk that was going to finish.
; =============================================================================
zo_remove:
    push ax
    push bx
    push cx
    push dx
    push di
    or ax, ax
    jz .out                             ; object 0 is not an object
    mov [zo_obj], ax
    mov bl, 0
    call zo_rel                         ; AX = parent
    or ax, ax
    jz .out                             ; already parentless: nothing to unlink
    mov [zo_par], ax
    mov ax, [zo_obj]
    mov bl, 1
    call zo_rel                         ; AX = sibling: it takes the object's place
    mov [zo_sib], ax
    mov ax, [zo_par]
    mov bl, 2
    call zo_rel                         ; AX = the parent's first child
    mov dx, [zo_obj]
    cmp ax, dx
    jne .walk
    ; the object was the first child: the parent points past it
    mov ax, [zo_par]
    mov cx, [zo_sib]
    mov bl, 2
    call zo_setrel
    jmp short .clear
.walk:
    mov cx, 0xFFFF                      ; see the banner: a cycle must not hang
.wl:
    or ax, ax
    jz .clear                           ; ran out: not well founded (12.5)
    mov di, ax                          ; DI = the candidate previous sibling
    mov bl, 1
    call zo_rel
    cmp ax, dx
    je .found
    loop .wl                            ; AX is already the next candidate
    jmp short .clear
.found:
    mov ax, di
    mov cx, [zo_sib]
    mov bl, 1
    call zo_setrel                      ; sibling(previous) = sibling(object)
.clear:
    mov ax, [zo_obj]
    xor cx, cx
    mov bl, 0
    call zo_setrel                      ; parent = nothing
    mov ax, [zo_obj]
    xor cx, cx
    mov bl, 1
    call zo_setrel                      ; and a parentless object has no sibling
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zo_insert - @insert_obj
; in:  AX = object number, BX = destination object number
; out: nothing; every register preserved
;
; Move the object to become the FIRST child of the destination: after this the
; child of D is O and the sibling of O is whatever D's child used to be
; (Standard 15). O may start anywhere in the tree, including with no parent at
; all, which is why the unlink is zo_remove's job and not a special case here.
;
; The order of the three writes matters only in that the old first child is
; read before child(D) is overwritten. It is read through zo_child so that a
; destination which is not an object answers 0 and the tree stays consistent
; rather than half-written.
;
; Inserting an object into its own subtree makes a cycle, and this does not
; check for it: 12.5 puts well-foundedness on the game, and the check is a
; walk to the root on every move - one of the commonest opcodes in a turn.
; A destination of 0 does nothing at all, because "the first child of nothing"
; has no meaning the Standard gives and detaching O instead would be a guess.
; =============================================================================
zo_insert:
    push ax
    push bx
    push cx
    push dx
    or ax, ax
    jz .out
    or bx, bx
    jz .out
    mov dx, bx                          ; DX = the destination; BL is a selector
    call zo_remove                      ; unlink from the old parent first
    mov cx, ax                          ; hold the object across the read
    mov ax, dx
    call zo_child                       ; AX = the destination's old first child
    xchg ax, cx                         ; AX = the object, CX = that old child
    mov bl, 1
    call zo_setrel                      ; sibling(O) = D's old first child
    mov cx, dx
    mov bl, 0
    call zo_setrel                      ; parent(O) = D
    mov cx, ax
    mov ax, dx
    mov bl, 2
    call zo_setrel                      ; child(D) = O
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
