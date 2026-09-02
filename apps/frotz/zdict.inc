; =============================================================================
; os8088 - apps/frotz/zdict.inc
;
; The dictionary and the tokeniser: Standard 13 in full, plus the lexical half
; of @read and @tokenise (Standard 15). This module owns exactly two questions -
; "where do the words in this line begin and end" and "is this word in that
; dictionary" - and it answers them without touching the screen, a file or a
; kernel slot, so it is callable from the worker with nothing saved.
;
; THREE DECISIONS SHAPE IT:
;
;  - **The dictionary header is parsed once into a cache, not per word.** A
;    turn's line is a dozen words and every one of them would otherwise re-read
;    the separator count, the entry length and the entry count out of story
;    memory. zd_setdict does it once per zd_tokenise call; zd_lookup reloads it
;    only when the caller has moved to a different table.
;
;  - **Every address here is 16 bits.** Standard 1.1: static memory "must end
;    by the last byte of the story file or by byte address $0ffff, whichever is
;    lower", so the dictionary lives below 64KB by construction, and the text
;    and parse buffers are in dynamic memory which is lower still. That lets
;    the whole module use zmem.inc's zm_*16 fast path - no DX, no 20-bit
;    arithmetic, and the inner compare loop is one call per byte.
;
;  - **The story's own dictionary is binary-chopped; a user dictionary handed
;    to @tokenise is not.** Standard 13.5 requires the story's entries to be in
;    numerical order of their encoded text, and Standard 15 (@tokenise) says a
;    user table may be unsorted and signals it by giving the entry count as -n.
;    The sign of that word is the whole test: it is why a game that renames
;    things in play works, and why a 2,000-word game dictionary costs eleven
;    compares instead of two thousand. On the target machine (a near call plus
;    ret is 11us, and a chop compares at most 6 bytes) that is the difference
;    between a parse you notice and one you do not.
;
; Nothing here draws, so nothing here is priced in PERFORMANCE.md's redraw
; budget - but the tokeniser sits between the player pressing RETURN and the
; first character of the reply, so it is on the latency path all the same.
; =============================================================================

; --- private bss, out of ZDICT_SLACK (192 bytes; 56 used) ---------------------
ZD_WMAX     equ 16                  ; characters of a word we bother to copy.
                                    ; Standard 3.7 truncates the encoding at 6
                                    ; or 9 Z-characters, and no ZSCII code
                                    ; encodes to less than one Z-character, so
                                    ; nine would do; sixteen is free and leaves
                                    ; room for a story's own alphabet table to
                                    ; be less efficient than the built-in one.

%define zd_wbuf   (os88_image_end + ZDICT_SLACK + 0)   ; ZD_WMAX: the word, in
                                                       ; our segment, lowered
%define zd_enc    (os88_image_end + ZDICT_SLACK + 16)  ; 6: its dictionary form
%define zd_dbase  (os88_image_end + ZDICT_SLACK + 22)  ; word: the cached table
%define zd_seps   (os88_image_end + ZDICT_SLACK + 24)  ; word: its separators
%define zd_nsep   (os88_image_end + ZDICT_SLACK + 26)  ; word: how many
%define zd_esize  (os88_image_end + ZDICT_SLACK + 28)  ; word: bytes per entry
%define zd_ecnt   (os88_image_end + ZDICT_SLACK + 30)  ; word: entries, clamped
%define zd_ebase  (os88_image_end + ZDICT_SLACK + 32)  ; word: the first entry
%define zd_ecmp   (os88_image_end + ZDICT_SLACK + 34)  ; word: bytes of encoded
                                                       ;       text to compare
%define zd_sorted (os88_image_end + ZDICT_SLACK + 36)  ; byte: chop, don't scan
%define zd_tbuf   (os88_image_end + ZDICT_SLACK + 38)  ; word: text buffer addr
%define zd_pbuf   (os88_image_end + ZDICT_SLACK + 40)  ; word: parse buffer
%define zd_pmax   (os88_image_end + ZDICT_SLACK + 42)  ; word: its byte 0
%define zd_pn     (os88_image_end + ZDICT_SLACK + 44)  ; word: words written
%define zd_scan   (os88_image_end + ZDICT_SLACK + 46)  ; word: the read cursor
%define zd_tend   (os88_image_end + ZDICT_SLACK + 48)  ; word: one past the text
%define zd_wstart (os88_image_end + ZDICT_SLACK + 50)  ; word: this word's addr
%define zd_wlen   (os88_image_end + ZDICT_SLACK + 52)  ; word: its length
%define zd_flag   (os88_image_end + ZDICT_SLACK + 54)  ; byte: @tokenise's flag

; =============================================================================
; zd_tokenise - lexical analysis (Standard 13.6, and @read / @tokenise in 15)
; in:  AX = text buffer address, BX = parse buffer address,
;      CX = dictionary address (0 = the story's own, from the header at $08),
;      DL = flag: nonzero means leave the slot of an unrecognised word alone
; out: nothing; preserves every register
;
; THE TEXT BUFFER IS NOT THE SAME SHAPE IN BOTH ERAS and getting it wrong is
; the classic v5 misbehaviour - the parser sees the length byte as a letter,
; every word is off by one and the game answers "I don't know the word" to
; everything the player types:
;
;   v1-v4   byte 0 = max letters - 1;  text from byte 1, NUL-terminated
;   v5+     byte 0 = max letters;      byte 1 = how many were typed;
;                                      text from byte 2, NO terminator
;
; Byte 0 bounds the walk in both eras, so a text buffer whose NUL the story
; overwrote cannot walk this loop off into the object tree.
;
; The parse buffer is one shape in both: byte 0 = the maximum number of words
; (the story's), byte 1 = the number found (ours), then four bytes per word -
; the dictionary address as a big-endian word, the letter count, and the
; position of the first letter WITHIN THE TEXT BUFFER (so 1-based in v1-4 and
; 2-based in v5+, because that is where the text starts).
;
; DL is what makes several @tokenise calls in a row additive (Standard 15):
; the slot INDEX advances for every word whether or not it was recognised, and
; only the write is skipped - otherwise a second pass would fill slot 3 with
; the answer to word 5.
; =============================================================================
zd_tokenise:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [zd_tbuf], ax
    mov [zd_pbuf], bx
    mov [zd_flag], dl

    or bx, bx                       ; Standard 15, @read: "in Versions 5 and
    jnz .haveparse                  ; later, if parse-buffer is zero then this
    jmp .done                       ; is omitted". Refused here as well as by
.haveparse:                         ; the caller, because writing a parse
                                    ; header at address 0 would land byte 1 of
                                    ; it on the story header's flags byte.

    or cx, cx                       ; 0 = "use the ordinary game dictionary"
    jnz .havedict
    mov cx, [zf_dict]
.havedict:
    mov ax, cx
    call zd_setdict

    ; --- where the text starts and where it stops ----------------------------
    mov ax, [zd_tbuf]
    call zm_gb16                    ; AL = byte 0, the story's own bound
    mov ah, 0
    mov bx, ax                      ; BX = it
    cmp byte [zf_ver], 5
    jb .v14

    mov ax, [zd_tbuf]
    inc ax
    call zm_gb16                    ; v5+: byte 1 = characters actually typed
    mov ah, 0
    cmp ax, bx                      ; a count past byte 0 is a lying story
    jbe .cok
    mov ax, bx
.cok:
    mov cx, ax
    mov ax, [zd_tbuf]
    add ax, 2
    mov [zd_scan], ax
    add ax, cx
    mov [zd_tend], ax
    jmp short .extok
.v14:
    mov ax, [zd_tbuf]               ; v1-v4: from byte 1 to the NUL, and never
    inc ax                          ; further than byte 0 allows
    mov [zd_scan], ax
    add ax, bx
    mov [zd_tend], ax
.extok:

    ; --- the parse buffer's header ------------------------------------------
    mov ax, [zd_pbuf]
    call zm_gb16
    mov ah, 0
    mov [zd_pmax], ax
    mov word [zd_pn], 0
    mov ax, [zd_pbuf]
    inc ax
    xor bx, bx
    call zm_pb16                    ; byte 1 = 0: this analysis owns the count

    ; --- the split (Standard 13.6.1) -----------------------------------------
    ; "Spaces divide up words and are otherwise ignored. Word separators also
    ; divide words, but each one of them is considered a word in its own
    ; right." So "fred,go  fishing" is four words: fred / , / go / fishing.
.scan:
    mov ax, [zd_scan]
    cmp ax, [zd_tend]
    jae .done
    call zm_gb16                    ; AL = the ZSCII code
    or al, al
    jz .done                        ; the v1-4 terminator, and a defence in v5
    cmp al, ' '
    jne .nospace
    inc word [zd_scan]
    jmp short .scan
.nospace:
    call zd_issep
    jnc .word

    mov ax, [zd_scan]               ; a separator: one character, one word
    mov [zd_wstart], ax
    mov word [zd_wlen], 1
    inc word [zd_scan]
    call zd_emit
    jnc .scan
    jmp short .done

.word:
    mov ax, [zd_scan]
    mov [zd_wstart], ax
.wscan:
    mov ax, [zd_scan]
    cmp ax, [zd_tend]
    jae .wend
    call zm_gb16
    or al, al
    jz .wend
    cmp al, ' '
    je .wend
    call zd_issep
    jc .wend                        ; the separator ENDS this word and starts
    inc word [zd_scan]              ; the next one; it is not consumed here
    jmp short .wscan
.wend:
    mov ax, [zd_scan]
    sub ax, [zd_wstart]
    mov [zd_wlen], ax
    call zd_emit
    jnc .scan
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zd_emit - encode the word at [zd_wstart]/[zd_wlen], look it up, and write its
;           four-byte block into the parse buffer
; in:  the zd_w* pair, and the header cache
; out: CF=1 = the parse buffer is full and the walk must stop; CF=0 = carry on.
;      Preserves every register.
;
; The count in byte 1 is rewritten after every word rather than once at the
; end, because a story that reads the parse buffer from an interrupt routine
; (Standard 15, @read's time/routine operands) must never see a count that
; promises blocks nobody has written yet.
; -----------------------------------------------------------------------------
zd_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, [zd_pn]
    cmp ax, [zd_pmax]               ; "it should stop before going beyond the
    jae .full                       ; maximum number of words specified"

    call zd_encword                 ; zd_enc = the dictionary form of the word
    mov di, zd_enc
    call zd_find                    ; DX:AX = the entry, 0 = not in the table
    or ax, ax
    jnz .write
    cmp byte [zd_flag], 0
    jne .count                      ; the slot keeps whatever it already held
.write:
    mov si, [zd_pn]
    mov cl, 2
    shl si, cl                      ; 4 bytes per word
    add si, [zd_pbuf]
    add si, 2                       ; SI = this word's block
    mov bx, ax
    mov ax, si
    call zm_pw16                    ; +0: the dictionary address (0 = unknown)
    mov ax, si
    add ax, 2
    mov bl, [zd_wlen]               ; a 255-byte text buffer bounds this to a
    call zm_pb16                    ; byte by construction
    mov ax, si
    add ax, 3
    mov bx, [zd_wstart]
    sub bx, [zd_tbuf]               ; +3: position within the TEXT BUFFER, so
    call zm_pb16                    ; the story can print the word back
.count:
    mov ax, [zd_pn]
    inc ax
    mov [zd_pn], ax
    mov bx, ax
    mov ax, [zd_pbuf]
    inc ax
    call zm_pb16
    clc
    jmp short .out
.full:
    stc
.out:
    pop di                          ; pop touches no flags: CF is the answer
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zd_encword - copy the current word out of story memory and encode it
; in:  [zd_wstart], [zd_wlen]
; out: zd_enc holds 4 (v1-3) or 6 (v4+) bytes of dictionary-form text;
;      preserves every register
;
; The copy exists because zt_encode reads OUR segment and the word is in the
; story's, and it lower-cases on the way past: Standard 3.7 says text being
; encrypted to match a dictionary word "should be converted to lower case".
; @read has already done that to a typed line, but @tokenise hands us whatever
; the story put in the buffer, and a capitalised word would otherwise encode
; with an A1 shift and match nothing.
; -----------------------------------------------------------------------------
zd_encword:
    push ax
    push bx
    push cx
    push si
    push di

    mov cx, [zd_wlen]
    cmp cx, ZD_WMAX
    jbe .n
    mov cx, ZD_WMAX                 ; anything past here cannot reach the 6th
.n:                                 ; or 9th Z-character anyway
    mov di, zd_wbuf
    mov bx, [zd_wstart]
    push cx
    jcxz .copied
.cp:
    mov ax, bx
    call zm_gb16
    cmp al, 'A'
    jb .store
    cmp al, 'Z'
    ja .store
    add al, 'a' - 'A'
.store:
    mov [di], al
    inc di
    inc bx
    loop .cp
.copied:
    pop cx
    mov si, zd_wbuf
    mov di, zd_enc
    call zt_encode                  ; pads with Z-character 5 and truncates to
                                    ; the version's 6 or 9 (Standard 3.7)
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zd_issep - is this ZSCII code one of the dictionary's word separators?
; in:  AL = the code; the header cache is loaded
; out: CF=1 = yes; preserves every register including AL
;
; Standard 13.2: the separators are the bytes between the count byte and the
; entry-length byte - typically full stop, comma and double-quote. A space is
; never one of them (13.2), and the caller has already taken spaces out, so
; nothing here special-cases 32.
; -----------------------------------------------------------------------------
zd_issep:
    push ax
    push bx
    push cx
    push dx
    mov dl, al                      ; zm_gb16 owns AL, so the code lives in DL
    mov cx, [zd_nsep]
    jcxz .no
    mov bx, [zd_seps]
.l:
    mov ax, bx
    call zm_gb16
    cmp al, dl
    je .yes
    inc bx
    loop .l
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zd_lookup - find an encoded word in the STORY's own dictionary
; in:  DI = a 4-byte (v1-3) or 6-byte (v4+) encoded word in OUR segment
; out: DX:AX = the entry's byte address, or 0 if there is no such entry;
;      preserves every other register
;
; The public face of the search. It re-reads the header cache only when the
; last caller was tokenising a user dictionary, which is the only way the
; cache can be describing anything else.
; =============================================================================
zd_lookup:
    push bx
    push cx
    push si
    mov ax, [zf_dict]
    or ax, ax
    jz .nostory                     ; no story loaded: there is no dictionary
    cmp ax, [zd_dbase]
    je .have
    call zd_setdict
.have:
    call zd_find
    jmp short .out
.nostory:
    xor ax, ax
    xor dx, dx
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zd_find - search the CACHED dictionary
; in:  DI = the encoded key in our segment; the cache is loaded
; out: DX:AX = the entry address, or 0; preserves every other register
;
; Sorted tables are chopped (Standard 13.5 guarantees the order and forbids
; duplicates); an unsorted user table is scanned. The chop is the reason
; @read is not something the player waits for: a 2,000-entry dictionary is 11
; compares rather than 2,000, and each compare is at most six zm_gb16 calls.
; -----------------------------------------------------------------------------
zd_find:
    push bx
    push cx
    push si

    mov cx, [zd_ecnt]
    jcxz .none
    cmp byte [zd_sorted], 0
    je .linear

    ; --- binary chop; SI = lo, CX = hi, both indices, hi exclusive -----------
    xor si, si
.bs:
    cmp si, cx
    jae .none
    mov ax, si
    add ax, cx
    rcr ax, 1                       ; (lo+hi)/2 THROUGH THE CARRY: a hostile
                                    ; entry length of 1 puts lo+hi past 65535
                                    ; and a plain shr would fold the midpoint
                                    ; back into the half already rejected
    push ax                         ; keep the midpoint index
    mul word [zd_esize]             ; index * entry length; the clamp in
    add ax, [zd_ebase]              ; zd_setdict is what makes this fit a word
    mov bx, ax
    call zd_cmpent                  ; flags = entry CMP key
    pop ax
    je .hit
    jb .lo                          ; entry sorts below the key: search above
    mov cx, ax                      ; entry sorts above it: hi = mid
    jmp short .bs
.lo:
    mov si, ax
    inc si
    jmp short .bs

.linear:
    mov bx, [zd_ebase]              ; "-n entries, unsorted" (Standard 15)
.ll:
    call zd_cmpent
    je .hit
    add bx, [zd_esize]
    loop .ll
.none:
    xor ax, ax
    xor dx, dx
    jmp short .out
.hit:
    mov ax, bx
    xor dx, dx
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zd_cmpent - compare one dictionary entry's encoded text against the key
; in:  BX = the entry's byte address, DI = the key in our segment
; out: the flags of the first differing byte, entry CMP key (so JB = the entry
;      sorts first, JE = equal); preserves every register
;
; Standard 13.5 orders entries by the encoded text read as a 32- or 48-bit
; number, most significant byte first - which is exactly a byte-by-byte
; unsigned compare from the front, so no word is ever formed here and the
; big-endian question never arises.
; -----------------------------------------------------------------------------
zd_cmpent:
    push ax
    push bx
    push cx
    push si
    mov si, di
    mov cx, [zd_ecmp]
    jcxz .eq
.l:
    mov ax, bx
    call zm_gb16
    cmp al, [si]
    jne .out
    inc bx
    inc si
    loop .l
.eq:
    xor ax, ax                      ; ZF=1, CF=0. The inc pair above already
                                    ; clobbered the flags of the last compare,
                                    ; so equality has to be re-stated here
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zd_setdict - parse a dictionary header into the cache (Standard 13.2)
; in:  AX = the table's byte address
; out: nothing; preserves every register
;
;   n  |  n separator codes  |  entry length  |  entry count  |  entries...
;  byte         n bytes            byte            2 bytes
;
; Every field is read out of a file the SDK tells us to treat as hostile, so
; three of them are clamped rather than trusted: a zero entry length would
; divide by zero below, a compare length longer than an entry would read the
; next entry's data as text, and an entry count large enough that
; base + count*length passes 65535 would wrap the chop's address arithmetic
; back into the header. None of the three can happen in a story Inform built;
; all three are one malformed byte away in a story it did not.
; -----------------------------------------------------------------------------
zd_setdict:
    push ax
    push bx
    push cx
    push dx

    mov [zd_dbase], ax
    call zm_gb16                    ; the separator count
    mov ah, 0
    mov cx, ax
    mov [zd_nsep], cx
    mov ax, [zd_dbase]
    inc ax
    mov [zd_seps], ax
    add ax, cx                      ; -> the entry-length byte
    mov bx, ax
    call zm_gb16
    mov ah, 0
    or ax, ax
    jnz .esz
    mov ax, 1
.esz:
    mov [zd_esize], ax

    mov ax, bx
    inc ax
    call zm_gw16                    ; the entry count, SIGNED: a negative one
    mov byte [zd_sorted], 1         ; means "|n| entries, unsorted" (15)
    or ax, ax
    jns .cnt
    neg ax
    mov byte [zd_sorted], 0
.cnt:
    mov [zd_ecnt], ax
    mov ax, bx
    add ax, 3
    mov [zd_ebase], ax

    ; bytes of encoded text per entry: 4 in v1-3 (6 Z-characters), 6 in v4 and
    ; later (9 Z-characters) - Standard 13.3 and 13.4
    mov ax, 4
    cmp byte [zf_ver], 4
    jb .cmpsz
    mov ax, 6
.cmpsz:
    cmp ax, [zd_esize]
    jbe .cmpok
    mov ax, [zd_esize]
.cmpok:
    mov [zd_ecmp], ax

    mov ax, 0xFFFF                  ; entries that fit below the 64KB ceiling
    sub ax, [zd_ebase]
    xor dx, dx
    div word [zd_esize]
    cmp ax, [zd_ecnt]
    jae .out
    mov [zd_ecnt], ax
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
