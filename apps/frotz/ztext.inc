; =============================================================================
; os8088 - apps/frotz/ztext.inc
;
; Z-strings in, ZSCII out, and the four output streams the story can select.
; Everything the Standard's section 3 describes lives here and nowhere else:
; the 5-bit Z-character packing, the three alphabets and their two completely
; different shift rules, abbreviations, the 10-bit ZSCII escape, the v5+
; custom alphabet table, dictionary encoding (3.7) and the ZSCII-to-glyph
; mapping (3.8). Section 7.1's output streams are here too, because a stream
; is a decision about where a character goes and zt_putc is the only routine
; that ever makes it.
;
; THREE DECISIONS SHAPE THIS FILE:
;
;  - **The decoder is a flat loop over an explicit cursor, not a recursion.**
;    An abbreviation (Standard 3.3) is itself a Z-string, and the obvious
;    implementation calls the decoder again. The worker's stack is 256 bytes
;    total and shared with the tick, mouse and sound IRQs (docs/FROTZ-PLAN.md
;    4), so this decodes out of a cursor held in bss - the word being consumed,
;    which Z-character of it is next, the address of the next word, and the
;    two alphabet registers - and an abbreviation SAVES that cursor into one
;    second copy and points the same loop at the abbreviation. Standard 3.3.1
;    promises an abbreviation never contains an abbreviation, so one saved
;    copy is the whole of it, and zt_lvl enforces the promise rather than
;    trusting it: a malformed story gets a dropped abbreviation, not a stack
;    that eats the scheduler's canary.
;
;  - **Only zmem.inc forms a word out of story bytes.** Z-machine words are
;    big-endian and the 8086 is not; the decoder reads through zm_gw/zm_gw16
;    and never touches ES, which belongs to the PC while the VM runs
;    (docs/FROTZ-PLAN.md 3.1).
;
;  - **A ZSCII character the 8x8 font cannot draw is TRANSLITERATED, not
;    dropped.** The kernel font is glyphs 32..126 and there is no second font
;    (SPEC.md 6): a character outside that range draws as blank while the pen
;    still advances, so a French or German story would print as prose full of
;    holes. Standard 3.8.5.4.1 allows textual equivalents, so ZSCII 155-251
;    map to their unaccented ASCII (e-acute -> "e"), and the seventeen whose
;    equivalent is genuinely two letters (a-diaeresis -> "ae", sz -> "ss",
;    thorn -> "th") print BOTH - one dropped letter per word is the
;    difference between "Kase" and "Kaese".
;
; Prefix: zt_.
; =============================================================================

; --- bss, from ZTEXT_SLACK only (apps/frotz/zbss.inc part 10) ----------------
; The decoder cursor is state rather than registers for the reason above, and
; the stream-3 stack is here rather than on SP for the same one: 16 nested
; tables is 64 bytes of table addresses and counts that must not live on a
; 256-byte stack.
%define zt_s1off  (os88_image_end + ZTEXT_SLACK + 0)   ; byte: stream 1 OFF
                                                       ;   (so all-zero = the
                                                       ;   screen is selected)
%define zt_s2on   (os88_image_end + ZTEXT_SLACK + 1)   ; byte: transcript on
%define zt_s4on   (os88_image_end + ZTEXT_SLACK + 2)   ; byte: command capture
%define zt_alph   (os88_image_end + ZTEXT_SLACK + 3)   ; byte: alphabet for the
                                                       ;   NEXT Z-character
%define zt_lock   (os88_image_end + ZTEXT_SLACK + 4)   ; byte: locked alphabet
                                                       ;   (always 0 in v3+)
%define zt_mode   (os88_image_end + ZTEXT_SLACK + 5)   ; byte: 0 normal,
                                                       ;   1 abbreviation index,
                                                       ;   2 escape high 5 bits,
                                                       ;   3 escape low 5 bits
%define zt_lvl    (os88_image_end + ZTEXT_SLACK + 6)   ; byte: 1 = inside an
                                                       ;   abbreviation
%define zt_abase  (os88_image_end + ZTEXT_SLACK + 7)   ; byte: which bank, 1..3
%define zt_ci     (os88_image_end + ZTEXT_SLACK + 8)   ; byte: next Z-char in
                                                       ;   zt_cw, 3 = exhausted
%define zt_end    (os88_image_end + ZTEXT_SLACK + 9)   ; byte: zt_cw had bit 15
%define zt_sci    (os88_image_end + ZTEXT_SLACK + 10)  ; byte: the ONE saved
%define zt_send   (os88_image_end + ZTEXT_SLACK + 11)  ; byte:   outer cursor -
%define zt_salph  (os88_image_end + ZTEXT_SLACK + 12)  ; byte:   these four and
%define zt_slock  (os88_image_end + ZTEXT_SLACK + 13)  ; byte:   the three words
%define zt_cw     (os88_image_end + ZTEXT_SLACK + 14)  ; word: the live word
%define zt_esc    (os88_image_end + ZTEXT_SLACK + 16)  ; word: escape accumulator
%define zt_al     (os88_image_end + ZTEXT_SLACK + 18)  ; word: read address, low
%define zt_ah     (os88_image_end + ZTEXT_SLACK + 20)  ; word: read address, high
%define zt_sal    (os88_image_end + ZTEXT_SLACK + 22)  ; word:   below are the
%define zt_sah    (os88_image_end + ZTEXT_SLACK + 24)  ; word:   whole of it
%define zt_scw    (os88_image_end + ZTEXT_SLACK + 26)  ; word:
%define zt_s3n    (os88_image_end + ZTEXT_SLACK + 28)  ; word: stream 3 depth
%define zt_s3tab  (os88_image_end + ZTEXT_SLACK + 30)  ; 16 words: table address
%define zt_s3cnt  (os88_image_end + ZTEXT_SLACK + 62)  ; 16 words: chars written
%define zt_nbuf   (os88_image_end + ZTEXT_SLACK + 94)  ; 8: zt_num's digits
%define zt_ebuf   (os88_image_end + ZTEXT_SLACK + 102) ; 12: zt_encode's Z-chars
%define zt_eni    (os88_image_end + ZTEXT_SLACK + 114) ; byte: how many so far
%define zt_enn    (os88_image_end + ZTEXT_SLACK + 115) ; byte: how many fit
; 116 of ZTEXT_SLACK's 128 bytes. Nothing here is touched by another module.

ZT_S3MAX    equ 16              ; Standard 7.1.2.1.1: a 17th select is an error
ZT_S3CAP    equ 32000           ; the Standard says the GAME sizes the table and
                                ; the interpreter checks nothing (7.1.2.1). That
                                ; is a promise about the story, not about the
                                ; heap: a runaway count would walk zm_pb16 out
                                ; of dynamic memory, so the count stops here.

; =============================================================================
; zt_print - print the Z-string at a story address
; in:  DX:AX = the string's byte address
; out: nothing; preserves all registers
; =============================================================================
zt_print:
    push ax
    push dx
    call zt_print_ret
    pop dx
    pop ax
    ret

; =============================================================================
; zt_print_ret - print it and say where it ended
; in:  DX:AX = the string's byte address
; out: DX:AX = the byte address just past its last word; other regs preserved
;
; @print (opcode 0OP:178) embeds its text inline and the PC resumes after it,
; so the interpreter needs the end address as much as the text.
; =============================================================================
zt_print_ret:
    push bx
    push cx
    push si
    push di
    mov [zt_al], ax
    mov [zt_ah], dx
    mov byte [zt_alph], 0       ; Standard 3.2.1: A0 is current initially
    mov byte [zt_lock], 0
    mov byte [zt_mode], 0
    mov byte [zt_lvl], 0
    mov byte [zt_ci], 3         ; nothing buffered: fetch on the first turn
    mov byte [zt_end], 0
    call zt_decloop
    mov ax, [zt_al]
    mov dx, [zt_ah]
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zt_decloop - the whole decoder: one flat loop over the cursor in bss
; in:  the cursor is set up
; out: nothing; preserves all registers
;
; The loop never calls itself. An abbreviation swaps the cursor for the
; abbreviation's own and sets zt_lvl; reaching the end bit at level 1 swaps it
; back and carries on with the very next Z-character of the outer word, which
; is what makes an abbreviation starting mid-word correct. That mid-word resume
; is the entire reason zt_ci and zt_cw are saved and not just the address.
; -----------------------------------------------------------------------------
zt_decloop:
    push ax
.next:
    cmp byte [zt_ci], 3
    jb .have
    cmp byte [zt_end], 0
    jne .strend
    call zt_fetch
.have:
    call zt_getz
    call zt_zchar
    jmp .next
.strend:
    cmp byte [zt_lvl], 0
    je .done
    mov al, [zt_sci]            ; resume the outer string
    mov [zt_ci], al
    mov al, [zt_send]
    mov [zt_end], al
    mov al, [zt_salph]
    mov [zt_alph], al
    mov al, [zt_slock]
    mov [zt_lock], al
    mov ax, [zt_scw]
    mov [zt_cw], ax
    mov ax, [zt_sal]
    mov [zt_al], ax
    mov ax, [zt_sah]
    mov [zt_ah], ax
    mov byte [zt_lvl], 0
    mov byte [zt_mode], 0       ; Standard 3.3.1: an abbreviation may not end
                                ; with an incomplete construction. If one does,
                                ; the half-built escape dies with it rather
                                ; than eating the outer string's next Z-char.
    jmp .next
.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_fetch - pull the next 2-byte word of the current string into the cursor
; in:  [zt_al]/[zt_ah] = where to read
; out: nothing; preserves all registers
;
; Standard 3.2: bit 15 is set only on the last word, and the three Z-characters
; are bits 14-10, 9-5 and 4-0. zm_gw does the big-endian assembly - this file
; never forms a word out of story bytes itself (docs/FROTZ-PLAN.md 3.1).
; -----------------------------------------------------------------------------
zt_fetch:
    push ax
    push bx
    push dx
    mov ax, [zt_al]
    mov dx, [zt_ah]
    call zm_gw
    add word [zt_al], 2
    adc word [zt_ah], 0
    mov bx, ax
    and bh, 0x7F
    mov [zt_cw], bx
    mov bl, 0
    test ah, 0x80
    jz .st
    mov bl, 1
.st:
    mov [zt_end], bl
    mov byte [zt_ci], 0
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_getz - take the next Z-character out of the buffered word
; in:  [zt_cw], [zt_ci] < 3
; out: AL = the Z-character (0..31), AH = 0; [zt_ci] advanced
; -----------------------------------------------------------------------------
zt_getz:
    push bx
    push cx
    mov bl, [zt_ci]
    inc byte [zt_ci]
    mov cl, 2
    sub cl, bl                  ; 2, 1, 0
    mov bl, cl
    shl cl, 1
    shl cl, 1
    add cl, bl                  ; CL = 5 * that = 10, 5, 0
    mov ax, [zt_cw]
    shr ax, cl
    and ax, 31
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zt_zchar - act on one Z-character
; in:  AL = 0..31
; out: nothing; preserves all registers
;
; Standard 3.2.2 (v1/v2) and 3.2.3 (v3+) are not two spellings of one rule -
; they are different machines. In v1/v2 EVERY alphabet can be current, 2/3
; shift for one character and 4/5 shift-lock, all relative to the alphabet in
; force. In v3+ the lock is gone, A0 is always the base, and 1/2/3 became the
; abbreviation banks. Frotz's own formula covers both:
;
;     next = (lock + (zchar & 1) + 1) mod 3
;
; which for v3+ (lock permanently 0) gives 4 -> A1 and 5 -> A2 exactly.
; -----------------------------------------------------------------------------
zt_zchar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bl, [zt_mode]
    or bl, bl
    jz .normal
    cmp bl, 1
    je .abbrix
    cmp bl, 2
    je .eschi

    ; --- mode 3: the low 5 bits of a 10-bit ZSCII escape (Standard 3.4) -----
    mov byte [zt_mode], 0
    mov bl, al
    xor bh, bh
    mov ax, [zt_esc]
    mov cl, 5
    shl ax, cl
    or ax, bx
    cmp ax, 255
    jbe .escok
    mov ax, '?'                 ; Standard 3.8.1: 256..1023 are undefined, and
                                ; 3.8.5.4.3 says print a question mark for a
                                ; character with no letter-form
.escok:
    call zt_putc
    jmp .out

.eschi:
    mov byte [zt_mode], 3
    xor ah, ah
    mov [zt_esc], ax
    jmp .out

.abbrix:
    mov byte [zt_mode], 0
    call zt_abbrev
    jmp .out

.normal:
    or al, al
    jz .space                   ; Standard 3.5.1
    cmp al, 6
    jae .letter
    cmp byte [zf_ver], 3
    jae .v3
    ; --- v1/v2 --------------------------------------------------------------
    cmp al, 1
    jne .shift
    cmp byte [zf_ver], 1
    je .v1nl
    mov al, 1                   ; v2 has one abbreviation bank (Standard 3.3)
    jmp .abbstart
.v1nl:
    mov al, 13                  ; Standard 3.5.2
    call zt_putc
    jmp .resetalph
.v3:
    cmp al, 4
    jb .abbstart                ; 1/2/3 = abbreviation banks (Standard 3.3)
.shift:
    mov bl, al
    and bl, 1
    inc bl
    add bl, [zt_lock]
    cmp bl, 3
    jb .nomod
    sub bl, 3
.nomod:
    mov [zt_alph], bl
    cmp byte [zf_ver], 2
    ja .out                     ; v3+ has no shift lock (Standard 3.2.3)
    cmp al, 4
    jb .out
    mov [zt_lock], bl
    jmp .out

.abbstart:
    ; AL = the bank, 1..3. The index is the NEXT Z-character.
    cmp byte [zt_lvl], 0
    jne .resetalph              ; Standard 3.3.1 forbids a nested abbreviation;
                                ; a story that does it anyway loses the
                                ; abbreviation, not the interpreter
    mov [zt_abase], al
    mov byte [zt_mode], 1
    jmp .resetalph

.space:
    mov al, 32
    call zt_putc
    jmp .resetalph

.letter:
    ; Z-characters 6..31 index the alphabet table, EXCEPT the two A2 slots the
    ; Standard reserves - and it reserves them even when the story supplies its
    ; own alphabet table (3.5.5.1).
    mov bl, [zt_alph]
    cmp bl, 2
    jne .plain
    cmp al, 6
    jne .maybenl
    mov byte [zt_mode], 2       ; Standard 3.4: two Z-chars of ZSCII follow
    jmp .resetalph
.maybenl:
    cmp al, 7
    jne .plain
    cmp byte [zf_ver], 1
    je .plain                   ; Standard 3.5.4: v1's A2 carries no new-line,
                                ; and everything after slot 6 shifts up one
    mov al, 13
    call zt_putc
    jmp .resetalph
.plain:
    mov ah, al
    sub ah, 6
    mov al, bl
    call zt_alpha_get
    call zt_putc
.resetalph:
    mov al, [zt_lock]           ; a single shift lasts exactly one character
    mov [zt_alph], al
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_abbrev - switch the cursor to abbreviation 32(bank-1)+index
; in:  AL = the index Z-character, [zt_abase] = the bank 1..3
; out: nothing; preserves all registers
;
; The table holds WORD addresses (Standard 3.3), so the byte address is twice
; the entry and can reach 128KB - hence DX:AX rather than a word.
; -----------------------------------------------------------------------------
zt_abbrev:
    push ax
    push bx
    push cx
    push dx
    xor ah, ah
    mov bl, [zt_abase]
    dec bl
    xor bh, bh
    mov cl, 5
    shl bx, cl                  ; 32 * (bank - 1)
    add bx, ax
    shl bx, 1                   ; two bytes per entry
    add bx, [zf_abbrev]
    mov ax, bx
    call zm_gw16                ; the abbreviations table is in static memory
    xor dx, dx
    shl ax, 1
    rcl dx, 1

    mov bl, [zt_ci]             ; bank the outer cursor - all of it, because
    mov [zt_sci], bl            ; the abbreviation can begin at the first of
    mov bl, [zt_end]            ; three Z-characters in a word and the other
    mov [zt_send], bl           ; two must still be read afterwards
    mov bl, [zt_alph]
    mov [zt_salph], bl
    mov bl, [zt_lock]
    mov [zt_slock], bl
    mov bx, [zt_cw]
    mov [zt_scw], bx
    mov bx, [zt_al]
    mov [zt_sal], bx
    mov bx, [zt_ah]
    mov [zt_sah], bx

    mov [zt_al], ax
    mov [zt_ah], dx
    mov byte [zt_lvl], 1
    mov byte [zt_ci], 3
    mov byte [zt_end], 0
    mov byte [zt_alph], 0       ; an abbreviation is a Z-string in its own
    mov byte [zt_lock], 0       ; right and starts in A0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zt_alpha_get - one cell of the alphabet table
; in:  AL = alphabet 0/1/2, AH = index 0..25 (i.e. Z-character - 6)
; out: AL = the ZSCII code; other registers preserved
;
; Standard 3.5.5: from v5 the header word at $34 may name a 78-byte table of
; the story's own, three blocks of 26. Below v5, or when that word is zero,
; the built-in rows of 3.5.3 apply - with v1's different A2 row (3.5.4).
; =============================================================================
zt_alpha_get:
    push bx
    push cx
    push dx
    cmp byte [zf_ver], 5
    jb .builtin
    cmp word [zf_alpha], 0
    je .builtin
    push ax
    mov bl, 26
    mul bl                      ; AX = alphabet * 26
    pop bx                      ; BH = the index we came in with
    mov bl, bh
    xor bh, bh
    add ax, bx
    add ax, [zf_alpha]
    call zm_gb16                ; the table lives in static memory, under 64K
    jmp .done
.builtin:
    mov bx, zt_a0
    cmp al, 1
    jb .row
    mov bx, zt_a1
    je .row
    mov bx, zt_a2
    cmp byte [zf_ver], 1
    jne .row
    mov bx, zt_a2v1
.row:
    mov dl, ah
    xor dh, dh
    add bx, dx
    mov al, [bx]
.done:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; zt_putc - one ZSCII character into whatever is selected (Standard 7.1)
; in:  AL = the ZSCII code
; out: nothing; preserves all registers
;
; Stream 3 is not one of four peers: while it is selected NOTHING reaches any
; other stream (7.1.2.2), and the others stay selected rather than being turned
; off. That single rule is why this routine tests it first and returns.
; =============================================================================
zt_putc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es                     ; ES IS THE PROGRAM COUNTER'S SEGMENT
                                ; (docs/FROTZ-PLAN.md 3.1), and this is the one
                                ; funnel from the VM into the window - so every
                                ; drawing path below reaches ES sooner or later
                                ; and putting it back HERE is what keeps that
                                ; from being every caller's problem. Without
                                ; it @print_paddr returned with ES pointing at
                                ; a font or a back buffer and the interpreter
                                ; carried on executing whatever lay at the same
                                ; offset in it, dying hundreds of instructions
                                ; later somewhere unrelated.
    cmp word [zt_s3n], 0
    jne .mem
    cmp byte [zt_s1off], 0
    jne .s2
    call zt_screen
.s2:
    cmp byte [zt_s2on], 0
    je .done
    call zi_script_putc         ; the transcript is a FILE, and a worker may
                                ; not touch one (docs/FROTZ-PLAN.md 4) - zio.inc
                                ; buffers it and the UI task writes it
    jmp .done
.mem:
    call zt_memput
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_screen - stream 1: a ZSCII character onto the glass
; in:  AL = the ZSCII code
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zt_screen:
    push ax
    push bx
    mov ah, al                  ; keep the raw code: the two-letter table is
                                ; indexed by it, not by the transliteration
    call zt_zscii_out
    or al, al
    jz .done                    ; nothing this font could stand for
    call zt_screen1
    mov al, ah
    call zt_pair                ; Standard 3.8.5.4.1's textual equivalents that
    or al, al                   ; are genuinely two letters
    jz .done
    call zt_screen1
.done:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_screen1 - one drawable byte to the window layer that owns this version
; in:  AL = a byte in 32..126, or 13 for a new-line
; out: nothing; preserves all registers
;
; v6 replaces the whole window model rather than varying it (SPEC.md 61), so
; the choice is made here once instead of inside every caller.
; -----------------------------------------------------------------------------
zt_screen1:
    push ax
%ifdef ZHARNESS
    call zh_screenc                 ; the teletype (apps/frotz/zharness.inc)
%endif
    cmp byte [zf_ver], 6
    je .v6
    cmp al, 13
    je .nl
    call zw_putc
    jmp .done
.nl:
    call zw_newline
    jmp .done
.v6:
    call zw6_putc
.done:
    pop ax
    ret

; =============================================================================
; zt_zscii_out - ZSCII to a byte the kernel's 8x8 font can draw
; in:  AL = the ZSCII code
; out: AL = 32..126, or 13 for a new-line, or 0 for "print nothing"
;      other registers preserved
;
; SPEC.md 6: the font is ROM glyphs 32..126 and there is no other. A character
; outside that range draws blank AND STILL ADVANCES THE PEN, so dropping the
; extra characters would not even leave a tidy gap - it would leave prose full
; of holes with the spacing of the accented text.
;
; Standard 3.8.5.4.1 requires a representation of every Latin-1 character and
; explicitly allows textual equivalents, so the default Unicode translation
; table (3.8.5.3, Table 1) is folded to its unaccented ASCII here. 224..251
; are defined codes with no default mapping - a story that uses them has given
; a Unicode translation table this interpreter has no letter-forms for, and
; 3.8.5.4.3 says the answer is a question mark.
; =============================================================================
zt_zscii_out:
    push bx
    cmp al, 32
    jb .low
    cmp al, 126
    jbe .done
    cmp al, 155
    jb .none
    cmp al, 223
    ja .high
    mov bl, al
    xor bh, bh
    add bx, zt_xtra
    mov al, [bx-155]
    jmp .done
.low:
    cmp al, 13
    je .done                    ; new-line is real output (Standard 3.8)
    cmp al, 9
    je .sp                      ; tab (v6)
    cmp al, 11
    je .sp                      ; sentence space (v6)
.none:
    xor al, al
    jmp .done
.sp:
    mov al, 32
    jmp .done
.high:
    cmp al, 251
    ja .none                    ; 252..254 are input-only, 255 is undefined
    mov al, '?'
.done:
    pop bx
    ret

; -----------------------------------------------------------------------------
; zt_pair - the second letter of a two-letter textual equivalent
; in:  AL = the ZSCII code
; out: AL = the letter, or 0; other registers preserved
; -----------------------------------------------------------------------------
zt_pair:
    push bx
    mov bx, zt_pairs
.loop:
    cmp byte [bx], 0
    je .none
    cmp [bx], al
    je .hit
    inc bx
    inc bx
    jmp .loop
.hit:
    mov al, [bx+1]
    jmp .done
.none:
    xor al, al
.done:
    pop bx
    ret

; =============================================================================
; zt_puts - an interpreter message
; in:  SI = a NUL-terminated string in OUR segment
; out: nothing; preserves all registers
;
; It goes STRAIGHT to the screen and to no other stream. An interpreter message
; is not the story's output: putting "[Press RETURN to choose a save file.]"
; into a stream 3 table would corrupt the game's own array, and putting it in
; the transcript would make the transcript a lie.
; =============================================================================
zt_puts:
    push ax
    push si
.loop:
    mov al, [si]
    inc si
    or al, al
    jz .done
    call zt_screen
    jmp .loop
.done:
    pop si
    pop ax
    ret

; =============================================================================
; zt_num - a signed 16-bit number in decimal
; in:  AX = the number
; out: nothing; preserves all registers
;
; -32768 needs no special case and that is worth saying, because it usually
; does: `neg 0x8000` leaves 0x8000, and the division below is UNSIGNED, which
; reads 0x8000 as 32768. The sign is printed before the negate, so the digits
; come out right.
; =============================================================================
zt_num:
    push ax
    push bx
    push cx
    push dx
    push di
    or ax, ax
    jns .pos
    push ax
    mov al, '-'
    call zt_putc
    pop ax
    neg ax
.pos:
    mov di, zt_nbuf
    xor cx, cx
    mov bx, 10
.dig:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    inc di
    inc cx
    or ax, ax
    jnz .dig
.emit:
    dec di
    mov al, [di]
    call zt_putc
    loop .emit
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Output stream selection (Standard 7.1, 7.4)
; =============================================================================
; zt_stream - @output_stream
; in:  AL = the stream number, signed: 1..4 selects, -1..-4 deselects
;      BX = the table's byte address (a stream 3 SELECT only)
; out: nothing; preserves all registers
;
; Stream 2 keeps Flags 2 bit 0 in step whichever way it was changed, because
; 'A Mind Forever Voyaging' reads it back (7.4).
; =============================================================================
zt_stream:
    push ax
    push bx
    push cx
    push dx
    push di
    or al, al
    jz .out
    js .off
    cmp al, 1
    je .on1
    cmp al, 2
    je .on2
    cmp al, 3
    je .on3
    cmp al, 4
    jne .out
    mov byte [zt_s4on], 1
    jmp .out
.on1:
    mov byte [zt_s1off], 0
    jmp .out
.on2:
    mov byte [zt_s2on], 1
    call zt_flag2
    jmp .out
.on3:
    mov cx, [zt_s3n]
    cmp cx, ZT_S3MAX
    jae .out                    ; 7.1.2.1.1 calls a 17th select a fatal error;
                                ; ignoring it loses text, halting loses the game
    mov di, cx
    shl di, 1
    mov ax, di
    add di, zt_s3tab
    mov [di], bx
    mov di, ax
    add di, zt_s3cnt
    mov word [di], 0
    inc word [zt_s3n]
    jmp .out
.off:
    neg al
    cmp al, 1
    je .off1
    cmp al, 2
    je .off2
    cmp al, 3
    je .off3
    cmp al, 4
    jne .out
    mov byte [zt_s4on], 0
    jmp .out
.off1:
    mov byte [zt_s1off], 1
    jmp .out
.off2:
    mov byte [zt_s2on], 0
    call zt_flag2
    jmp .out
.off3:
    cmp word [zt_s3n], 0
    je .out
    call zt_s3close
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_s3close - finish the innermost stream 3 table (Standard 7.1.2.1)
; in:  [zt_s3n] > 0
; out: nothing; preserves all registers
;
; The length word is written only NOW: while the stream is selected the table's
; contents are unspecified and the game may not read them, which is exactly
; what lets the count live in bss instead of in the story's array.
; -----------------------------------------------------------------------------
zt_s3close:
    push ax
    push bx
    push cx
    push di
    mov di, [zt_s3n]
    dec di
    mov [zt_s3n], di
    shl di, 1
    mov bx, di
    add bx, zt_s3cnt
    mov cx, [bx]
    mov bx, di
    add bx, zt_s3tab
    mov ax, [bx]
    mov bx, cx
    call zm_pw16
    cmp byte [zf_ver], 6
    jne .done
    mov ax, cx                  ; v6 also stores the total printing width at
    shl ax, 1                   ; header $30, and our font is a fixed CELL_W
    shl ax, 1
    shl ax, 1
    mov bx, ax
    mov ax, 0x30
    call zm_pw16
.done:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_memput - stream 3: one ZSCII byte into the innermost table
; in:  AL = the ZSCII code
; out: nothing; preserves all registers
;
; New-lines arrive here as ZSCII 13 and are stored as-is (7.1.2.2.1).
; -----------------------------------------------------------------------------
zt_memput:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, ax                  ; SI's low byte carries the character while AX
                                ; is needed for the address
    mov bx, [zt_s3n]
    dec bx
    shl bx, 1
    mov dx, bx
    add bx, zt_s3cnt
    mov cx, [bx]
    cmp cx, ZT_S3CAP
    jae .out
    inc word [bx]
    mov bx, dx
    add bx, zt_s3tab
    mov ax, [bx]
    add ax, cx
    inc ax
    inc ax                      ; past the length word the close will write
    mov bx, si
    call zm_pb16
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_flag2 - keep Flags 2 bit 0 equal to stream 2's state (Standard 7.4)
; in:  [zt_s2on]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zt_flag2:
    push ax
    push bx
    mov ax, 0x10
    call zm_gw16
    mov bx, ax
    and bl, 0xFE
    cmp byte [zt_s2on], 0
    je .put
    or bl, 1
.put:
    mov ax, 0x10
    call zm_pw16
    pop bx
    pop ax
    ret

; =============================================================================
; zt_selected - is a stream on?
; in:  AL = 1..4
; out: CF = 1 selected, CF = 0 not; all registers preserved
;
; zio.inc asks about stream 4 before echoing a command, and zexec.inc asks
; about stream 2 when the story writes Flags 2 by hand.
; =============================================================================
zt_selected:
    push ax
    xor ah, ah
    cmp al, 1
    jne .n1
    mov al, [zt_s1off]
    xor al, 1
    jmp .ans
.n1:
    cmp al, 2
    jne .n2
    mov al, [zt_s2on]
    jmp .ans
.n2:
    cmp al, 3
    jne .n3
    xor al, al
    cmp word [zt_s3n], 0
    je .ans
    inc al
    jmp .ans
.n3:
    cmp al, 4
    jne .no
    mov al, [zt_s4on]
.ans:
    or al, al
    jz .no
    stc
    jmp .out
.no:
    clc
.out:
    pop ax
    ret

; =============================================================================
; zt_reset - put the streams back the way a fresh game finds them
; in:  nothing
; out: nothing; preserves all registers
;
; @restart and @restore both land on a machine whose streams are whatever the
; previous story left, and an unterminated stream 3 pointing into the old
; dynamic memory is the one that bites.
; =============================================================================
zt_reset:
    mov byte [zt_s1off], 0
    mov byte [zt_s2on], 0
    mov byte [zt_s4on], 0
    mov word [zt_s3n], 0
    mov byte [zt_mode], 0
    mov byte [zt_lvl], 0
    ret

; =============================================================================
; zt_enclen - how many bytes an encoded dictionary word occupies
; in:  nothing
; out: AX = 4 (v1-3) or 6 (v4+); other registers preserved
;
; Six Z-characters in v1-3 and nine in v4+ (Standard 3.7), packed three to a
; word - so the BUFFER is 4 or 6 bytes, not 6 or 9. zdict.inc compares this
; many bytes against a dictionary entry, so the two must agree.
; =============================================================================
zt_enclen:
    mov ax, 4
    cmp byte [zf_ver], 4
    jb .out
    mov ax, 6
.out:
    ret

; =============================================================================
; zt_encode - encrypt typed text the way the dictionary stores it
; in:  SI = the text, in OUR segment; CX = its length
;      DI = the destination, zt_enclen bytes
; out: nothing; preserves all registers
;
; Standard 3.7, and every clause of it is load-bearing: lower-case first (so
; A1 is unreachable unless the story supplied its own alphabet table), no
; abbreviations, pad with Z-character 5, and truncate to exactly 6 or 9
; Z-characters LEAVING ANY MULTI-CHARACTER CONSTRUCTION INCOMPLETE rather than
; dropping it - which is why zt_emit caps per Z-character instead of the
; caller checking whether the next character will fit whole.
;
; NOT implemented: 3.7.1's v1/v2 rule that shift-LOCK 4/5 replaces the single
; shifts when two consecutive characters share an alphabet. It only changes
; the encoding of v1/v2 words carrying two adjacent punctuation or capital
; characters, and v1/v2 dictionaries are lower-case words; the cost of getting
; it wrong is one word not matching, the cost of the code is a lookahead in
; the one place this file has none.
; =============================================================================
zt_encode:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov al, 6
    cmp byte [zf_ver], 4
    jb .setn
    mov al, 9
.setn:
    mov [zt_enn], al
    mov byte [zt_eni], 0

    push cx                     ; pad first: a truncation then needs no tidying
    push di
    mov di, zt_ebuf
    mov cx, 12
    mov al, 5
.pad:
    mov [di], al
    inc di
    loop .pad
    pop di
    pop cx

.each:
    or cx, cx
    jz .pack
    mov al, [zt_eni]
    cmp al, [zt_enn]
    jae .pack
    mov al, [si]
    inc si
    dec cx
    call zt_enc_char
    jmp .each

.pack:
    ; Standard 3.2 in reverse: three Z-characters to a big-endian word, bit 15
    ; set on the last one.
    mov si, zt_ebuf
    mov dl, [zt_enn]
    xor dh, dh
.pw:
    xor ax, ax
    mov al, [si]
    mov cl, 5
    shl ax, cl
    or al, [si+1]
    shl ax, cl
    or al, [si+2]
    add si, 3
    sub dl, 3
    jnz .notlast
    or ah, 0x80
.notlast:
    mov [di], ah
    mov [di+1], al
    inc di
    inc di
    or dl, dl
    jnz .pw

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_enc_char - one character of typed text into Z-characters
; in:  AL = the character
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zt_enc_char:
    push ax
    push bx
    push cx
    push dx
    cmp al, 'A'
    jb .nolc
    cmp al, 'Z'
    ja .nolc
    add al, 32                  ; Standard 3.7: convert to lower case
.nolc:
    cmp al, ' '
    jne .find
    xor al, al                  ; Standard 3.5.1
    call zt_emit
    jmp .out
.find:
    mov ah, al
    mov al, 0
    call zt_search
    jnc .a0
    mov al, 1
    call zt_search
    jnc .a1
    mov al, 2
    call zt_search
    jnc .a2
    ; In no alphabet: a 10-bit ZSCII escape, four Z-characters (Standard 3.4).
    call zt_shift2
    mov al, 6
    call zt_emit
    mov al, ah
    mov cl, 5
    shr al, cl
    and al, 31
    call zt_emit
    mov al, ah
    and al, 31
    call zt_emit
    jmp .out
.a0:
    add al, 6
    call zt_emit
    jmp .out
.a1:
    push ax
    call zt_shift1
    pop ax
    add al, 6
    call zt_emit
    jmp .out
.a2:
    push ax
    call zt_shift2
    pop ax
    add al, 6
    call zt_emit
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_search - where a character sits in one alphabet
; in:  AL = alphabet 0/1/2, AH = the ZSCII character
; out: CF = 0 and AL = the index 0..25; CF = 1 = not in this alphabet.
;      AH preserved; other registers preserved
;
; A2 slots 0 and 1 are the escape and the new-line and are never a letter, in
; the built-in table or the story's own (Standard 3.5.5.1) - so the scan skips
; them. In v1 there is no new-line slot and only slot 0 is skipped (3.5.4).
; -----------------------------------------------------------------------------
zt_search:
    push bx
    push cx
    push dx
    mov dl, al
    mov dh, ah
    xor cl, cl
    cmp dl, 2
    jne .lp
    mov cl, 1
    cmp byte [zf_ver], 1
    je .lp
    mov cl, 2
.lp:
    cmp cl, 26
    jae .miss
    mov al, dl
    mov ah, cl
    call zt_alpha_get
    cmp al, dh
    je .hit
    inc cl
    jmp .lp
.hit:
    mov al, cl
    mov ah, dh
    clc
    jmp .out
.miss:
    mov ah, dh
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zt_shift1 / zt_shift2 - emit the shift that reaches A1 / A2 for one character
; in:  nothing
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zt_shift1:
    push ax
    mov al, 4
    cmp byte [zf_ver], 3
    jae .e
    mov al, 2
.e:
    call zt_emit
    pop ax
    ret

zt_shift2:
    push ax
    mov al, 5
    cmp byte [zf_ver], 3
    jae .e
    mov al, 3
.e:
    call zt_emit
    pop ax
    ret

; -----------------------------------------------------------------------------
; zt_emit - append one Z-character to the encoding, or drop it
; in:  AL = the Z-character
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zt_emit:
    push ax
    push bx
    mov bl, [zt_eni]
    cmp bl, [zt_enn]
    jae .out
    inc byte [zt_eni]
    xor bh, bh
    add bx, zt_ebuf
    mov [bx], al
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; The alphabet rows (Standard 3.5.3 and 3.5.4), Z-characters 6..31
; =============================================================================
zt_a0:   db 'abcdefghijklmnopqrstuvwxyz'
zt_a1:   db 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
; Slot 0 reads as a space in the Standard's diagram but is the ZSCII escape and
; is never looked up here; slot 1 is the new-line.
zt_a2:   db ' ^0123456789.,!?_#', 39, '"/\-:()'
; v1 has no new-line in A2, which makes room for '<' and shifts everything up.
zt_a2v1: db ' 0123456789.,!?_#', 39, '"/\<-:()'

; =============================================================================
; ZSCII 155..223 folded onto ASCII (Standard 3.8.5.3, Table 1's own "textual
; equivalent" column, reduced to its first letter)
; =============================================================================
zt_xtra:
    db 'aouAOUs'                ; 155-161 a/o/u diaeresis, both cases, sz
    db '><'                     ; 162-163 guillemets
    db 'eiyEI'                  ; 164-168 e/i/y diaeresis, E/I diaeresis
    db 'aeiouy'                 ; 169-174 acute, lower
    db 'AEIOUY'                 ; 175-180 acute, upper
    db 'aeiou'                  ; 181-185 grave, lower
    db 'AEIOU'                  ; 186-190 grave, upper
    db 'aeiou'                  ; 191-195 circumflex, lower
    db 'AEIOU'                  ; 196-200 circumflex, upper
    db 'aAoO'                   ; 201-204 a-ring, o-slash
    db 'anoANO'                 ; 205-210 tilde
    db 'aAcC'                   ; 211-214 ae-ligature, c-cedilla
    db 'ttTT'                   ; 215-218 thorn and eth
    db 'L'                      ; 219     pound sign
    db 'oO'                     ; 220-221 oe-ligature
    db '!?'                     ; 222-223 inverted punctuation

; The seventeen whose equivalent is two letters: (ZSCII, second letter), and a
; NUL count byte to end. Printing only the first would turn German prose into
; something a reader has to decode.
zt_pairs:
    db 155, 'e',  156, 'e',  157, 'e'       ; ae oe ue
    db 158, 'e',  159, 'e',  160, 'e'       ; Ae Oe Ue
    db 161, 's'                             ; ss
    db 162, '>',  163, '<'                  ; >> <<
    db 211, 'e',  212, 'E'                  ; ae AE ligatures
    db 215, 'h',  216, 'h'                  ; th th
    db 217, 'h',  218, 'h'                  ; Th Th
    db 220, 'e',  221, 'E'                  ; oe OE ligatures
    db 0
