; =============================================================================
; os8088 - apps/word/scrtf.inc
;
; RTF IN AND OUT (SPEC.md 68.8) - Word's other portable format, and the one
; that is pure text transform: Opus/RTFOUT.C writes it and Opus/RTFIN.C reads
; it, neither touching Windows. Both directions live here.
;
; The extension decides which format a save writes: a name ending .RTF gets
; this, everything else gets the Word file (SPEC.md 68.4). On the way in the
; CONTENT decides - a file beginning '{\rtf' is RTF whatever it is called -
; because a file that arrives from elsewhere may be named anything.
; =============================================================================

SC_RTFDEPTH  equ 16             ; group nesting. Word's own reader has a
                                ; limit too; past this the import stops with
                                ; a toast and keeps what it had
SC_RTFCW     equ 20             ; the longest control word this reads

; =============================================================================
; THE WRITER
; =============================================================================

; -----------------------------------------------------------------------------
; sc_rtfimg - assemble the RTF text into the staging claim
; in:  ES = the staging claim, DS = the package
; out: CF=0 and [sc_dend] = the byte count; CF=1 = it did not fit.
;      Clobbers AX BX CX DX SI DI.
; -----------------------------------------------------------------------------
sc_rtfimg:
    cld
    xor di, di
    mov byte [sc_rfull], 0
    mov si, sc_r_head
    call sc_res
    xor bx, bx                      ; BX = the character index
    mov byte [sc_rat], 0            ; the attributes currently OPEN
    mov byte [sc_rnew], 1           ; a paragraph is about to start
.step:
    cmp byte [sc_rnew], 0
    je .body
    mov byte [sc_rnew], 0
    call sc_rpap                    ; \pard plus this paragraph's own
    mov byte [sc_rat], 0            ; controls; \plain closed every run
.body:
    cmp bx, [sc_len]
    jae .tail
    push ds
    PKG_LDS sc_dseg
    mov al, [bx]
    pop ds
    cmp al, 13
    je .para
    cmp al, SC_PICCH                ; a picture is its own group and wears no
    je .pict                        ; character formatting (88.5)
    push ax
    push ds
    PKG_LDS sc_cseg
    mov al, [bx]
    pop ds
    call sc_rdress                  ; open/close what this character wears
    pop ax
    call sc_rch
    inc bx
    jmp short .step
.pict:
    call sc_rpict
    inc bx
    jmp short .step
.para:
    mov si, sc_r_par
    call sc_res
    inc bx
    mov byte [sc_rnew], 1
    jmp short .step
.tail:
    mov si, sc_r_par                ; the tail paragraph's own \par: a Word
    call sc_res                     ; file's text ends with a mark and so
    mov si, sc_r_tail               ; does RTF's (SPEC.md 68.4)
    call sc_res
    mov [sc_dend], di
    cmp byte [sc_rfull], 0
    jne .no
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; sc_rpict - emit the picture the SC_PICCH at BX names, as an RTF \pict group
; in:  BX = the character index; ES = the staging claim, DS = the package
; out: preserves all but DI and the flags - sc_re advancing DI IS the emission,
;      which sc_res and sc_rnum already say of themselves. A character naming
;      no picture emits NOTHING, which is the same thing the screen does with
;      it (sc_rowhc).
;
; THIS IS THE ONE PLACE SCRIBE WRITES SOMETHING WORD DOES NOT, and RTF is the
; format where that is safe rather than reckless (SPEC.md 94.5). RTF is text
; and its own rule is that a reader skips a destination it does not know, so
; the worst a foreign reader can do with this group is drop it and keep the
; document. The same picture in a .DOC would be a binary field that a reader
; does not skip but BELIEVES.
;
; It is not an invented dialect either - every control word here is RTF's own:
;
;   \pict           the destination
;   \wbitmap 0      a Windows bitmap, bmType 0
;   \wbmplanes 1    ONE plane of...
;   \wbmbitspixel 4 ...4-bit pixels: os8088's packed 4bpp exactly, two to a
;                   byte with the high nibble leftmost, so a load is a copy
;                   and not a conversion
;   \wbmwidthbytes  the stride, carried and never recomputed - the lesson
;                   .PCX's BytesPerLine taught (SPEC.md 93)
;   \picw \pich     pixels
;   \picwgoal \pichgoal   twips, at 15 per pixel = 96dpi, the Windows
;                   logical inch. Display size, not storage size.
;
; NO PALETTE, AND THAT IS CORRECT RATHER THAN LAZY. A \wbitmap is a
; DEVICE-dependent bitmap: its indices mean whatever the device's colours are.
; os8088's sixteen are fixed (SPEC.md 6.3), so an index IS a colour here and a
; palette would be restating the device. \dibitmap is the device-INdependent
; form that carries one, and it costs a BITMAPINFO and a second layout for a
; picture that is always displayed on the machine that wrote it.
;
; THE DATA IS HEX, at two characters a byte. That is twice the size of RTF's
; \bin and it keeps SPEC.md 68.8's "pure text transform" property, which is
; worth more than the bytes: a \bin run can contain 0x7D and any reader that
; is not counting exactly right sees a group end in the middle of a picture.
; The cost is a real ceiling - the staging claim is SC_DOCCAP KB addressed by
; a 16-bit DI - so about 30KB of picture across a document. sc_re already
; stops on a full claim and sc_rtfimg already turns that into "it did not
; fit", so the limit refuses rather than truncates.
; =============================================================================
sc_rpict:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    PKG_LDS sc_cseg            ; the CHP byte IS the picture index
    mov al, [bx]                    ; (SPEC.md 94.9)
    pop ds
    xor ah, ah
    call far [sc_v_picrec]; -> BX = the table slot
    jc .out                         ; names no picture: emit nothing
    mov ax, [bx]
    mov [sc_rpw], ax
    mov ax, [bx+2]
    mov [sc_rph], ax
    mov ax, [bx+4]
    mov [sc_rps], ax
    mov ax, [bx+6]
    mov [sc_rpg], ax
    or ax, ax                       ; a slot with no segment is a picture that
    jz .out                         ; was freed; it draws nothing either
    cmp word [sc_rph], 2184         ; 2184 * 15 = 32,760 TWIPS, AND sc_rnum
    jbe .hok                        ; EMITS SIGNED. Nothing bounds a picture's
    mov byte [sc_rfull], 1          ; HEIGHT - OS88IMG_WMAX bounds the width -
    jmp .out                        ; so a 2x40,959 .PIX decodes, is claimed,
.hok:                               ; and comes out as \pich-24577, which this
                                    ; reader then refuses on the way back in:
                                    ; a save that reported success with the
                                    ; picture gone. sc_dpicw's ceiling is 4369
                                    ; for the same arithmetic and twice the
                                    ; room, because a PICF's dyaGoal is an
                                    ; unsigned stosw. Refusing the SAVE and not
                                    ; the picture (47): sc_rtfimg turns
                                    ; sc_rfull into "it did not fit"
    mov si, sc_r_pict               ; '{\pict\wbitmap0\wbmplanes1'
    call sc_res                     ; '\wbmbitspixel4\wbmwidthbytes'
    mov ax, [sc_rps]
    call sc_rnum
    mov si, sc_r_picw
    call sc_res
    mov ax, [sc_rpw]
    call sc_rnum
    mov si, sc_r_pich
    call sc_res
    mov ax, [sc_rph]
    call sc_rnum
    mov si, sc_r_picwg              ; the GOAL size, in twips at 96dpi. The
    call sc_res                     ; widest picture os88img.inc will decode
    mov ax, [sc_rpw]                ; is 1280, and 1280*15 = 19200, so this
    call sc_rtwip                   ; cannot overflow the word sc_rnum signs
    mov si, sc_r_pichg
    call sc_res
    mov ax, [sc_rph]
    call sc_rtwip
    mov word [sc_rpcol], 0
    mov dx, [sc_rph]                ; DX = rows left
.row:
    cmp byte [sc_rfull], 0          ; THE CLAIM IS FULL AND THE SAVE IS ALREADY
    jne .fin                        ; REFUSED. sc_re stops storing and sets
                                    ; this; without the test the loop still
                                    ; walks every remaining picture byte at
                                    ; five near call/ret pairs each, which is
                                    ; ~2.2 s a 30KB picture on a 4.77MHz 8088,
                                    ; on the UI task, to produce nothing
    cmp word [sc_rps], 0            ; a zero stride would make `loop` 65,536
    je .fin                         ; passes off the end of the picture. Not
                                    ; reachable through either producer today;
                                    ; this is the one place that trusts a
                                    ; pictab slot without re-checking it
    or dx, dx
    jz .fin
    mov cx, [sc_rps]                ; CX = bytes in this row
    mov bx, [sc_rph]                ; the source offset: (h - dx) * stride
    sub bx, dx
    mov ax, bx
    mul word [sc_rps]               ; DX is clobbered by mul, so it is banked
    mov si, ax                      ; ...and restored below
    mov dx, [sc_rph]
    sub dx, bx
    dec dx                          ; DX = rows left AFTER this one
.byte:
    push es
    mov es, [sc_rpg]                ; the picture is a THIRD segment: ES is
    mov al, [es:si]                 ; the staging claim everywhere else in
    pop es                          ; this file, so it is borrowed per byte
    call sc_rwhex
    inc si
    inc word [sc_rpcol]
    cmp word [sc_rpcol], 64         ; wrap the hex, so the file is readable
    jb .nowrap                      ; and no line is 17,000 characters long
    mov word [sc_rpcol], 0
    mov al, 13
    call sc_re
    mov al, 10
    call sc_re
.nowrap:
    loop .byte
    jmp short .row
.fin:
    mov al, '}'
    call sc_re
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_rtwip - emit AX pixels as twips (x15) followed by a space. Preserves all
; but DI and flags. 15 twips is one pixel at the 96dpi Windows logical inch.
sc_rtwip:
    push ax
    push dx
    mov dx, 15
    mul dx                          ; DX:AX, and the high half is always 0
    call sc_rnum                    ; for a width os88img.inc would accept
    pop dx
    pop ax
    ret

; sc_rwhex - emit AL as two lowercase hex digits. Preserves all but DI and
; flags: every emitter here advances it, which is what emitting IS.
sc_rwhex:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl                      ; the HIGH nibble first, which is also the
    call sc_rwhex1                   ; leftmost pixel (SPEC.md 93)
    mov al, ah
    and al, 0x0F
    call sc_rwhex1
    pop cx
    pop ax
    ret

sc_rwhex1:
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 'a' - '0' - 10
.emit:
    call sc_re
    pop ax
    ret

; sc_rch - emit one document character, escaped. Preserves all but DI and
; flags.
sc_rch:
    push ax
    push si
    cmp al, 9
    je .tab
    cmp al, '\'
    je .esc
    cmp al, '{'
    je .esc
    cmp al, '}'
    je .esc
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    call sc_re
    jmp short .out
.esc:
    push ax
    mov al, '\'
    call sc_re
    pop ax
    call sc_re
    jmp short .out
.tab:
    mov si, sc_r_tab
    call sc_res
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rdress - emit the controls that turn AL's attributes on and the ones
;             that turn the previous run's off
; in:  AL = the attribute byte this character wears
; out: [sc_rat] = AL; preserves all registers
; -----------------------------------------------------------------------------
sc_rdress:
    push ax
    push bx
    push cx
    push si
    mov cl, al
    mov ch, [sc_rat]
    cmp cl, ch
    je .out
    mov bl, cl
    xor bl, ch                      ; BL = what changed
    test bl, SCAT_BOLD
    jz .ni
    mov si, sc_r_b
    test cl, SCAT_BOLD
    jnz .b1
    mov si, sc_r_b0
.b1:
    call sc_res
.ni:
    test bl, SCAT_ITAL
    jz .ns
    mov si, sc_r_i
    test cl, SCAT_ITAL
    jnz .i1
    mov si, sc_r_i0
.i1:
    call sc_res
.ns:
    test bl, SCAT_SCAP
    jz .nh
    mov si, sc_r_sc
    test cl, SCAT_SCAP
    jnz .s1
    mov si, sc_r_sc0
.s1:
    call sc_res
.nh:
    test bl, SCAT_HID
    jz .nu
    mov si, sc_r_v
    test cl, SCAT_HID
    jnz .h1
    mov si, sc_r_v0
.h1:
    call sc_res
.nu:
    mov al, bl                      ; the three underlines are one state, so
    and al, SCAT_UL|SCAT_WUL|SCAT_DUL   ; any change closes and reopens
    jz .done
    mov si, sc_r_ul0
    call sc_res
    mov si, sc_r_ul
    test cl, SCAT_UL
    jnz .u1
    mov si, sc_r_ulw
    test cl, SCAT_WUL
    jnz .u1
    mov si, sc_r_uld
    test cl, SCAT_DUL
    jnz .u1
    jmp short .done
.u1:
    call sc_res
.done:
    mov [sc_rat], cl
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rpap - emit \pard\plain and the controls of the paragraph starting at BX
; Preserves all registers.
; -----------------------------------------------------------------------------
sc_rpap:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    mov si, sc_r_pard
    call sc_res
    call sc_rpapidx                 ; AL = the paragraph's dictionary index
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    PKG_LDS sc_pseg
    mov ax, [si]
    mov bx, [si+2]
    mov ds, [cs:sc_pkgseg]          ; NOT `push cs / pop ds`: sc_rpap is in
                                    ; .modc, so CS is the MODULE (SPEC.md
                                    ; 94.8.3) and sc_rpp is a SCVAR - four
                                    ; bytes of somebody else's heap written
                                    ; once per paragraph. It wrote and read
                                    ; back through the SAME wrong address,
                                    ; which is why the file came out right.
                                    ; The refusal is NOT lost with it:
                                    ; sc_rtfimg's .tail emits with DS back on
                                    ; the package, so sc_re sets the package's
                                    ; sc_rfull there and "it did not fit"
                                    ; still fires
    mov [sc_rpp], ax                ; the four PAP bytes are BANKED, not held
    mov [sc_rpp+2], bx              ; in registers: SI is the string pointer
                                    ; every emit below needs
    mov al, [sc_rpp]
    and al, SCPA_ALIGN
    jz .nj                          ; left is \pard's own default
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [cs:bx+sc_r_qtab]     ; (SPEC.md 94.8.2)
    call sc_res
.nj:
    mov al, [sc_rpp+1]              ; left indent
    or al, al
    jz .nl
    xor ah, ah
    call sc_dtwip
    push ax
    mov si, sc_r_li
    call sc_res
    pop ax
    call sc_rnum
.nl:
    mov al, [sc_rpp+2]              ; first-line indent, SIGNED
    or al, al
    jz .nf
    cbw
    call sc_dtwip
    push ax
    mov si, sc_r_fi
    call sc_res
    pop ax
    call sc_rnum
.nf:
    mov al, [sc_rpp+3]              ; right indent
    or al, al
    jz .nr
    xor ah, ah
    call sc_dtwip
    push ax
    mov si, sc_r_ri
    call sc_res
    pop ax
    call sc_rnum
.nr:
    mov al, [sc_rpp]                ; line spacing: 0/1/2 -> 240/360/480
    and al, SCPA_SPACE
    jz .nsp
    shr al, 1
    shr al, 1
    xor ah, ah
    mov cx, 120
    mul cx
    add ax, 240
    push ax
    mov si, sc_r_sl
    call sc_res
    pop ax
    call sc_rnum
.nsp:
    test byte [sc_rpp], SCPA_SB     ; space before: half a line, 120 twips
    jz .nsb
    mov si, sc_r_sb
    call sc_res
    mov ax, 120
    call sc_rnum
.nsb:
                                    ; NO trailing space here: every control
                                    ; word this emits already carries the one
                                    ; that delimits it (the literal strings
                                    ; end in one, sc_rnum appends one), and a
                                    ; second space is TEXT - it arrived as a
                                    ; leading space on every paragraph
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_rpapidx - AL = the PAP index of the paragraph starting at BX. Preserves
; all other registers.
sc_rpapidx:
    push bx
    push cx
    push ds
    PKG_LD cx, sc_len
    PKG_LDS sc_dseg
.scan:
    cmp bx, cx
    jae .tail
    cmp byte [bx], 13
    je .mark
    inc bx
    jmp short .scan
.mark:
    PKG_LDS sc_cseg
    mov al, [bx]
    jmp short .out
.tail:
    PKG_LD al, sc_pap_tail
.out:
    pop ds
    pop cx
    pop bx
    ret

; sc_re - emit AL into the staging claim at ES:DI. Sets [sc_rfull] and stops
; storing once the claim is full. Preserves all registers but DI.
sc_re:
    cmp di, SC_DOCCAP * 1024 - 2
    jae .full
    mov [es:di], al
    inc di
    ret
.full:
    mov byte [sc_rfull], 1
    ret

; sc_res - emit the NUL-terminated CS string at SI. Preserves all but DI.
sc_res:
    push ax
    push si
.step:
    mov al, [cs:si]
    or al, al
    jz .out
    inc si
    call sc_re
    jmp short .step
.out:
    pop si
    pop ax
    ret

; sc_rnum - emit signed AX in decimal, then one space. Preserves all but DI.
sc_rnum:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    or ax, ax
    jge .pos
    neg ax
    push ax
    mov al, '-'
    call sc_re
    pop ax
.pos:
    mov cx, 0
    mov bx, 10
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    call sc_re
    loop .emit
    mov al, ' '
    call sc_re
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the writer's strings ----------------------------------------------------
sc_r_head: db '{\rtf1\ansi\deff0{\fonttbl{\f0\fmodern Courier New;}}', 13, 10
           db '{\stylesheet{\snext0 Normal;}}', 13, 10, 0
sc_r_tail: db '}', 13, 10, 0
sc_r_pict:  db '{\pict\wbitmap0\wbmplanes1\wbmbitspixel4\wbmwidthbytes', 0
sc_r_picw:  db '\picw', 0
sc_r_pich:  db '\pich', 0
sc_r_picwg: db '\picwgoal', 0
sc_r_pichg: db '\pichgoal', 0
sc_r_pard: db '\pard\plain ', 0
sc_r_par:  db '\par', 13, 10, 0
sc_r_tab:  db '\tab ', 0
sc_r_b:    db '\b ', 0
sc_r_b0:   db '\b0 ', 0
sc_r_i:    db '\i ', 0
sc_r_i0:   db '\i0 ', 0
sc_r_sc:   db '\scaps ', 0
sc_r_sc0:  db '\scaps0 ', 0
sc_r_v:    db '\v ', 0
sc_r_v0:   db '\v0 ', 0
sc_r_ul:   db '\ul ', 0
sc_r_ulw:  db '\ulw ', 0
sc_r_uld:  db '\uldb ', 0
sc_r_ul0:  db '\ulnone ', 0
sc_r_ql:   db '\ql ', 0
sc_r_qc:   db '\qc ', 0
sc_r_qr:   db '\qr ', 0
sc_r_qj:   db '\qj ', 0
sc_r_li:   db '\li', 0
sc_r_fi:   db '\fi', 0
sc_r_ri:   db '\ri', 0
sc_r_sl:   db '\sl', 0
sc_r_sb:   db '\sb', 0
sc_r_qtab: dw sc_r_ql, sc_r_qc, sc_r_qr, sc_r_qj

; =============================================================================
; THE READER
; =============================================================================

; sc_isrtfimg - does the staging claim begin '{\rtf'?
; in:  AX = the file's byte count; out: CF=0 = yes. Preserves every register.
sc_isrtfimg:
    push ax
    push si
    push es
    mov es, [sc_stgseg]
    cmp ax, 5
    jb .no
    cmp byte [es:0], '{'
    jne .no
    cmp byte [es:1], '\'
    jne .no
    cmp byte [es:2], 'r'
    jne .no
    cmp byte [es:3], 't'
    jne .no
    cmp byte [es:4], 'f'
    jne .no
    pop es
    pop si
    pop ax
    clc
    ret
.no:
    pop es
    pop si
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sc_rtfparse - read the RTF in the staging claim into the document
; in:  [sc_stgseg], AX = the file's byte count
; out: CF=0 loaded, CF=1 refused. Clobbers everything.
;
; A tokeniser over groups, control words, control symbols and text, with a
; group stack that saves and restores the character attributes so a \b's
; scope ends with its group, and a destination rule: {\* ...} and the known
; non-text destinations are skipped whole (SPEC.md 68.8).
; -----------------------------------------------------------------------------
sc_rtfparse:
    mov [sc_dfsize], ax
    mov byte [sc_dtrunc], 0
    mov ax, SC_MAXKB
    call far [sc_v_resize]
    jc .no
    mov word [sc_len], 0
    mov word [sc_papn], 1           ; a fresh dictionary: entry 0 is Normal
    push es
    mov es, [sc_pseg]
    mov word [es:0], 0
    mov word [es:2], 0
    pop es
    mov word [sc_pfb], 0
    mov word [sc_pfb+2], 0
    mov byte [sc_rat], 0
    mov word [sc_rdep], 0
    mov word [sc_rskip], 0
    mov word [sc_rpin], 0           ; no \pict open, and no claim held from a
    mov word [sc_rpseg], 0          ; parse that was refused half way
    call far [sc_v_pictfree]; the OLD document's pictures go back
                                    ; HERE and not at sc_load's commit point,
                                    ; which is past the ones this parse is
                                    ; about to build (SPEC.md 94.6)
    xor si, si                      ; SI = where we are in the file
.step:
    cmp si, [sc_dfsize]
    jae .done
    push es
    mov es, [sc_stgseg]
    mov al, [es:si]
    pop es
    inc si
    cmp al, '{'
    je .open
    cmp al, '}'
    je .close
    cmp al, '\'
    je .ctrl
    cmp al, 13
    je .step
    cmp al, 10
    je .step
    cmp word [sc_rskip], 0          ; SKIP WINS. A {\*\...} destination inside
    jne .step                       ; an open \pict is still skipped: its text
    cmp word [sc_rpin], 0           ; is not pixels, and an ODD number of hex
    je .text                        ; digits in it left a nibble in sc_rpnib
    call sc_rpbyte                  ; that shifted every byte of the picture
    jmp .step                       ; after it
.text:
    call sc_rput
    jmp .step
.open:
    mov ax, [sc_rdep]
    cmp ax, SC_RTFDEPTH
    jae .deep
    mov bx, ax
    shl bx, 1
    mov al, [sc_rat]
    mov [bx+sc_rstk], al
    inc word [sc_rdep]
    jmp .step
.close:
    cmp word [sc_rdep], 0
    je .step
    dec word [sc_rdep]
    mov bx, [sc_rdep]
    shl bx, 1
    mov al, [bx+sc_rstk]
    mov [sc_rat], al
    mov ax, [sc_rpin]               ; a \pict ends at the depth it opened at,
    or ax, ax                       ; the same test sc_rskip makes below
    jz .nopic
    dec ax                          ; ...INCLUDING the dec: without it the
    cmp [sc_rdep], ax               ; picture finalised on the closing brace of
    ja .nopic                       ; any group NESTED inside the \pict, so a
                                    ; {\*\picprop} - which Word writes - tore
                                    ; the collector down and the hex that
                                    ; followed went into the document as text
    call sc_rpfin
.nopic:
    mov ax, [sc_rskip]
    or ax, ax
    jz .step
    dec ax                          ; skipping stops when the group that
    cmp [sc_rdep], ax               ; started it closes
    ja .step
    mov word [sc_rskip], 0
    jmp .step
.ctrl:
    call sc_rtok                    ; consumes the control word or symbol
    jmp .step
.deep:
    mov byte [sc_dtrunc], 1         ; nested past what this reads: keep what
    jmp .done                 ; arrived and say so
.done:
    mov dx, [sc_rpseg]              ; a \pict whose brace never arrived - the
    or dx, dx                       ; file ran out, or .deep bailed. sc_rpfin
    jz .nopen                       ; is reached only from .close, so without
    call OSAPI_MEM_FREE             ; this the claim is orphaned: it is in no
    mov word [sc_rpseg], 0          ; pictab slot, so sc_pictfree cannot see it
    mov word [sc_rpin], 0           ; either, and the next parse's zeroing of
.nopen:                             ; sc_rpseg drops the handle rather than
    call sc_rtail                   ; freeing it
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_rput - append character AL to the document, wearing [sc_rat]
; Preserves all registers. A document that reaches its ceiling stops taking
; characters and raises [sc_dtrunc].
; -----------------------------------------------------------------------------
sc_rput:
    push ax
    push bx
    push es
    cmp al, 9                       ; \tab arrives here, and a tab is one of
    je .store                       ; the three control codes this document
    cmp al, 32                      ; model holds (SPEC.md 68.3)
    jb .out
    cmp al, 126
    ja .out
.store:
    mov bx, [sc_len]
    cmp bx, [sc_cap]
    jae .full
    mov es, [sc_dseg]
    mov [es:bx], al
    mov es, [sc_cseg]
    mov al, [sc_rat]
    mov [es:bx], al
    inc word [sc_len]
    jmp short .out
.full:
    mov byte [sc_dtrunc], 1
.out:
    pop es
    pop bx
    pop ax
    ret

; sc_rputm - append a paragraph mark carrying the current PAP candidate.
; Preserves all registers.
sc_rputm:
    push ax
    push bx
    push es
    call far [sc_v_papfind]; AL = the dictionary index (CF=1 = full,
    jc .plain                       ; and Normal is the honest fallback)
    jmp short .have
.plain:
    xor al, al
.have:
    mov bx, [sc_len]
    cmp bx, [sc_cap]
    jae .full
    push ax
    mov es, [sc_dseg]
    mov byte [es:bx], 13
    pop ax
    mov es, [sc_cseg]
    mov [es:bx], al                 ; the mark's CHP byte IS the paragraph's
                                    ; dictionary index (SPEC.md 68.3)
    inc word [sc_len]
    jmp short .out
.full:
    mov byte [sc_dtrunc], 1
.out:
    pop es
    pop bx
    pop ax
    ret

; sc_rtail - the tail paragraph's format, for sc_ldpost. Preserves all.
sc_rtail:
    push ax
    call far [sc_v_papfind]
    jnc .have
    xor al, al
.have:
    mov [sc_dtail], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rtok - read the control word or symbol at SI and act on it
; in:  SI = one past the backslash
; out: SI advanced past it (and past one trailing space); preserves all others
; -----------------------------------------------------------------------------
sc_rtok:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov es, [sc_stgseg]
    cmp si, [sc_dfsize]
    jae .out
    mov al, [es:si]
    call sc_risal
    jc .symbol
    xor di, di                      ; --- a control word --------------------
.name:
    cmp si, [sc_dfsize]
    jae .named
    mov al, [es:si]
    call sc_risal
    jc .named
    cmp di, SC_RTFCW - 1
    jae .nostore
    mov [di+sc_rcw], al
    inc di
.nostore:
    inc si
    jmp short .name
.named:
    mov byte [di+sc_rcw], 0
    xor cx, cx                      ; CX = the parameter, DX = it has one
    xor dx, dx
    mov bx, 1
    cmp si, [sc_dfsize]
    jae .noparam
    mov al, [es:si]
    cmp al, '-'
    jne .digits
    mov bx, -1
    inc si
.digits:
    cmp si, [sc_dfsize]
    jae .noparam
    mov al, [es:si]
    cmp al, '0'
    jb .noparam
    cmp al, '9'
    ja .noparam
    mov dx, 1
    sub al, '0'
    xor ah, ah
    push ax
    mov ax, cx
    push bx
    push dx                         ; DX says the control word HAD a
    mov bx, 10                      ; parameter, and a 16-bit mul writes
    mul bx                          ; DX:AX - losing it reads \b0 as \b
    pop dx
    pop bx
    mov cx, ax
    pop ax
    add cx, ax
    inc si
    jmp short .digits
.noparam:
    or bx, bx
    jge .signed
    neg cx
.signed:
    cmp si, [sc_dfsize]             ; exactly one trailing space belongs to
    jae .act                        ; the control word
    cmp byte [es:si], ' '
    jne .act
    inc si
.act:
    call sc_rword
    jmp short .out
.symbol:
    inc si                          ; --- a control symbol ------------------
    cmp al, '*'
    je .star
    cmp al, 0x27                    ; \'hh - a hex character
    je .hex
    cmp word [sc_rskip], 0
    jne .out
    cmp al, '\'
    je .lit
    cmp al, '{'
    je .lit
    cmp al, '}'
    je .lit
    cmp al, '~'
    jne .out
    mov al, ' '
    call sc_rput
    jmp short .out
.lit:
    call sc_rput
    jmp short .out
.star:
    mov ax, [sc_rdep]               ; \* names the group a skipper
    mov [sc_rskip], ax
    jmp short .out
.hex:
    xor ax, ax
    cmp si, [sc_dfsize]
    jae .out
    mov al, [es:si]
    call sc_rhex
    jc .out
    mov ah, al
    inc si
    cmp si, [sc_dfsize]
    jae .out
    mov al, [es:si]
    call sc_rhex
    jc .out
    inc si
    push cx
    mov cl, 4
    shl ah, cl
    pop cx
    or al, ah
    cmp word [sc_rskip], 0
    jne .out
    call sc_rput
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_risal - CF=0 if AL is an ASCII letter. Preserves all registers.
sc_risal:
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .no
    clc
    ret
.up:
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    ja .no
    clc
    ret
.no:
    stc
    ret

; sc_rhex - AL = the value of hex digit AL; CF=1 = it is not one.
sc_rhex:
    cmp al, '0'
    jb .no
    cmp al, '9'
    ja .a
    sub al, '0'
    clc
    ret
.a:
    or al, 32
    cmp al, 'a'
    jb .no
    cmp al, 'f'
    ja .no
    sub al, 'a' - 10
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_rword - act on the control word at [sc_rcw] with parameter CX (DX = it
;            had one)
; Preserves all registers.
;
; An unrecognised control word is IGNORED - not its group. \* is what says
; 'skip the group', and honouring that distinction is what stops an RTF file
; from arriving empty (SPEC.md 68.8).
; -----------------------------------------------------------------------------
sc_rword:
    push ax
    push bx
    push si
    push di
    mov bx, sc_r_tbl
.look:
    mov si, [cs:bx]                 ; THE TABLE AND THE NAMES IT POINTS AT are
    or si, si                       ; the module's own image, so they are read
    jz .none                        ; through CS and not DS - the package's
    mov di, sc_rcw                  ; segment no longer holds them (88.8.2)
    call sc_rstreq
    jc .next
    mov al, [cs:bx+2]               ; the action
    jmp short .do
.next:
    add bx, 3
    jmp short .look
.none:
    pop di
    pop si
    pop bx
    pop ax
    ret
.do:
    cmp al, SCR_SKIP
    jne .n1
    cmp word [sc_rskip], 0          ; a destination this reader has no use
    jne .fin                        ; for: skip to its closing brace
    mov si, [sc_rdep]
    mov [sc_rskip], si
    jmp short .fin
.n1:
    cmp word [sc_rskip], 0
    jne .fin                        ; inside a skipped group nothing acts
    cmp al, SCR_PAR
    jne .n2
    call sc_rputm
    jmp short .fin
.n2:
    cmp al, SCR_TAB
    jne .n3
    mov al, 9
    call sc_rput
    jmp short .fin
.n3:
    cmp al, SCR_PLAIN
    jne .n4
    mov byte [sc_rat], 0
    jmp short .fin
.n4:
    cmp al, SCR_PARD
    jne .n5
    mov word [sc_pfb], 0
    mov word [sc_pfb+2], 0
    jmp short .fin
.n5:
    cmp al, SCR_PICT
    jb .n6
    cmp al, SCR_PICPL
    ja .n6
    call sc_rword3                  ; the picture actions
    jmp short .fin
.n6:
    call sc_rword2                  ; the attribute, alignment and indent
                                    ; actions - split out because the block
                                    ; above ran out of short jumps
.fin:
    pop di
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; THE PICTURE READER (SPEC.md 94.6). \pict used to be a skipped destination
; and is now a collected one. The shape mirrors sc_rskip exactly: [sc_rpin]
; holds the depth the group opened at, .step routes text to the collector
; while it is set, and .close finalises when that group's brace arrives.
;
; A GROUP THIS READER CANNOT USE IS DISCARDED AT THE CLOSING BRACE, which is
; the same outcome skipping always gave. That is deliberately the only refusal
; path: deciding mid-stream that a \wmetafile is not for us would mean a
; second state to get wrong, and the bytes are bounded by the claim anyway.
; sc_rpfin validates and frees; nothing reaches the document unless every
; parameter agrees.
; =============================================================================

; sc_rword3 - the picture actions. in: AL = the action, CX = the parameter,
; DX = it had one. Preserves all registers.
sc_rword3:
    push ax
    cmp al, SCR_PICT
    jne .param
    mov ax, [sc_rdep]               ; the depth the group opened at, exactly
    mov [sc_rpin], ax               ; as SCR_SKIP records it
    push dx                         ; a \pict reaching here with one already
    push es                         ; open re-arms the collector, and the claim
    mov dx, [sc_rpseg]              ; the last one was filling then has no
    or dx, dx                       ; holder at all. ES is banked because
    jz .nofree                      ; MEM_FREE is an X stub and overwrites it
    call OSAPI_MEM_FREE
.nofree:
    pop es
    pop dx
    mov word [sc_rpseg], 0
    mov word [sc_rpoff], 0
    mov byte [sc_rpnib], 0
    mov word [sc_rppl], 1           ; RTF's own defaults for a \pict
    mov word [sc_rpbp], 1
    mov word [sc_rpwb], 0
    mov word [sc_rpiw], 0
    mov word [sc_rpih], 0
    jmp short .out
.param:
    or dx, dx                       ; a picture parameter with no number is
    jz .out                         ; not one
    cmp al, SCR_PICWB
    jne .n1
    mov [sc_rpwb], cx
    jmp short .out
.n1:
    cmp al, SCR_PICW
    jne .n2
    mov [sc_rpiw], cx
    jmp short .out
.n2:
    cmp al, SCR_PICH
    jne .n3
    mov [sc_rpih], cx
    jmp short .out
.n3:
    cmp al, SCR_PICBP
    jne .n4
    mov [sc_rpbp], cx
    jmp short .out
.n4:
    mov [sc_rppl], cx
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rpbyte - AL is a character inside a \pict group. Preserves all registers.
;
; Hex digits pair into bytes; everything else - the spaces and the line breaks
; the writer puts in to keep the file readable - is ignored. The claim is taken
; on the FIRST data byte and not at \pict, because its size comes from
; \wbmwidthbytes and \pich and those are control words that arrive first.
; -----------------------------------------------------------------------------
sc_rpbyte:
    push ax
    push bx
    push cx
    push dx
    push es
    call sc_rhex                    ; the READER's hex-digit decoder, which
    jc .out                         ; predates this and is exactly right
    mov ah, al
    cmp byte [sc_rpnib], 0
    jne .low
    mov [sc_rpacc], ah              ; the HIGH nibble, held for its partner
    mov byte [sc_rpnib], 1
    jmp short .out
.low:
    mov byte [sc_rpnib], 0
    mov al, [sc_rpacc]
    mov cl, 4
    shl al, cl
    or al, ah
    cmp word [sc_rpseg], 0
    jne .have
    call sc_rpclaim                 ; first byte: size it and take it
    jc .out
.have:
    mov bx, [sc_rpoff]
    cmp bx, [sc_rpcap]
    jae .out                        ; more data than the header described:
    mov es, [sc_rpseg]              ; keep the picture it described and drop
    mov [es:bx], al                 ; the rest, rather than run off the claim
    inc word [sc_rpoff]
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_rpclaim - size the picture from its header and claim it.
; out: CF=0 and [sc_rpseg]/[sc_rpcap] set; CF=1 = refused (and rpseg stays 0).
; Preserves all but the flags.
sc_rpclaim:
    push ax
    push bx
    push cx
    push dx
    cmp word [sc_rppl], 1           ; ONE plane of FOUR-bit pixels is the only
    jne .no                         ; arrangement this writes and the only one
    cmp word [sc_rpbp], 4           ; it reads: 88.5 says why
    jne .no
    mov ax, [sc_rpwb]
    or ax, ax
    jz .no
    mov cx, [sc_rpiw]
    or cx, cx
    jz .no
    cmp cx, OS88IMG_WMAX            ; the same width ceiling os88img.inc has,
    ja .no                          ; so a picture that arrives by this route
    inc cx                          ; cannot be one the decoder would refuse
    shr cx, 1
    cmp ax, cx
    jb .no                          ; a stride under (w+1)/2 cannot hold it
    mov cx, [sc_rpih]
    or cx, cx
    jz .no
    mul cx                          ; DX:AX = the whole picture
    or dx, dx
    jnz .no
    mov [sc_rpcap], ax
    add ax, 1023
    jc .no
    mov cl, 10
    shr ax, cl
    or ax, ax
    jnz .kbok
    mov ax, 1
.kbok:
    call OSAPI_MEM_CLAIM            ; out DX = the base segment
    jc .no
    mov [sc_rpseg], dx
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rpfin - the \pict group has closed. Register it, or give it back.
; Preserves all registers.
;
; A picture whose data ran short is refused WHOLE rather than padded: half a
; picture drawn as if it were a picture is worse than the document arriving
; without it, and 47 makes refusal an ordinary path.
; -----------------------------------------------------------------------------
sc_rpfin:
    push ax
    push bx
    push cx
    push dx
    push di
    mov word [sc_rpin], 0
    mov byte [sc_rpnib], 0
    cmp word [sc_rpseg], 0          ; never claimed: nothing we could use
    je .out
    mov ax, [sc_rpoff]
    cmp ax, [sc_rpcap]
    jb .drop                        ; short of what the header promised
    mov ax, [sc_rpwb]               ; ...AND THE HEADER MAY HAVE BEEN REWRITTEN
    mul word [sc_rpih]              ; since sc_rpclaim sized the claim: \picw,
    or dx, dx                       ; \pich and \wbmwidthbytes are accepted for
    jnz .drop                       ; the rest of the group and only the CLAIM
    cmp ax, [sc_rpcap]              ; is fixed. A record describing 1280x200
    jne .drop                       ; over a 1KB claim is 127KB of somebody
    mov cx, [sc_rpiw]               ; else's heap blitted into the document, so
    or cx, cx                       ; the record must describe the bytes we
    jz .drop                        ; actually hold - every test sc_rpclaim
    cmp cx, OS88IMG_WMAX            ; made, made again on the final values
    ja .drop
    inc cx
    shr cx, 1
    cmp [sc_rpwb], cx
    jb .drop
    mov ax, [sc_npic]
    cmp ax, SC_PICMAX               ; a document may hold SC_PICMAX (88.9);
    jae .drop                       ; the rest arrive as nothing
    mov cl, 3
    shl ax, cl                      ; * SC_PICREC
    add ax, sc_pictab
    mov di, ax
    mov ax, [sc_rpiw]
    mov [di], ax
    mov ax, [sc_rpih]
    mov [di+2], ax
    mov ax, [sc_rpwb]
    mov [di+4], ax
    mov ax, [sc_rpseg]
    mov [di+6], ax
    mov ax, [sc_npic]               ; the CHP byte IS the index (88.9), and
    inc word [sc_npic]              ; the character carries no other format.
    call sc_rputp                   ; AL is that index and sc_rputp wants it
    jnc .out                        ; there - the character it writes is
    dec word [sc_npic]              ; SC_PICCH, which is the routine's own
    mov byte [sc_dtrunc], 1         ; business and not an argument to it.
    jmp short .drop                 ; CF=1 is a FULL document: no character
                                    ; names the picture, so the slot and the
                                    ; claim go back and the load says it did
                                    ; not all fit (47) rather than reporting
                                    ; a picture nothing can draw
.drop:
    mov dx, [sc_rpseg]
    call OSAPI_MEM_FREE
.out:
    mov word [sc_rpseg], 0
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_rputp - put SC_PICCH in the document with AL as its CHP byte, which is
; the picture index rather than an attribute set. Preserves all registers.
; out: CF=1 = the document was full and nothing was written.
sc_rputp:
    push ax
    push bx
    push es
    mov bx, [sc_len]
    cmp bx, [sc_cap]
    jae .full                       ; the same ceiling every other character
    mov ah, al                      ; meets; sc_rput's own refusal
    mov es, [sc_dseg]
    mov byte [es:bx], SC_PICCH
    mov es, [sc_cseg]
    mov [es:bx], ah
    inc word [sc_len]
    clc
    jmp short .out
.full:
    stc                             ; the caller owns a claim and a pictab slot
.out:                               ; on the strength of this character

    pop es
    pop bx
    pop ax
    ret

; sc_rword2 - the rest of the actions, split out because the first block
; above ran out of short jumps. in: AL = the action, CX = the parameter,
; DX = it had one, BX = the table row. Preserves all registers.
sc_rword2:
    push ax
    push bx
    push si
    cmp al, SCR_BOLD
    jb .npos
    cmp al, SCR_HID
    ja .npos
    sub al, SCR_BOLD                ; the four flag attributes, in order
    xor ah, ah
    mov si, ax
    mov ah, [cs:si+sc_r_bits]     ; CS: this table travels WITH
    or dx, dx                       ; no parameter means ON; a 0 parameter
    jz .on                          ; means OFF
    or cx, cx
    jz .off
.on:
    or [sc_rat], ah
    jmp .out
.off:
    not ah
    and [sc_rat], ah
    jmp .out
.npos:
    cmp al, SCR_UL
    jb .nul
    cmp al, SCR_ULNONE
    ja .nul
    and byte [sc_rat], ~(SCAT_UL|SCAT_WUL|SCAT_DUL) & 0xFF
    cmp al, SCR_ULNONE
    je .out
    or dx, dx
    jz .ulon
    or cx, cx
    jz .out
.ulon:
    sub al, SCR_UL
    xor ah, ah
    mov si, ax
    mov al, [cs:si+sc_r_ulbits]   ; the code into SCRIBE.OVL
    or [sc_rat], al
    jmp short .out
.nul:
    cmp al, SCR_QL
    jb .nq
    cmp al, SCR_QJ
    ja .nq
    sub al, SCR_QL
    and byte [sc_pfb], ~SCPA_ALIGN & 0xFF
    or [sc_pfb], al
    jmp short .out
.nq:
    cmp al, SCR_LI
    je .li
    cmp al, SCR_FI
    je .fi
    cmp al, SCR_RI
    je .ri
    cmp al, SCR_SL
    je .sl
    cmp al, SCR_SB
    je .sb
    jmp short .out
.li:
    call sc_rcell
    mov [sc_pfb+1], al
    jmp short .out
.fi:
    call sc_rcell
    mov [sc_pfb+2], al
    jmp short .out
.ri:
    call sc_rcell
    mov [sc_pfb+3], al
    jmp short .out
.sl:
    mov ax, cx
    or ax, ax
    jge .slp
    neg ax                          ; \sl-240 is 'exactly 240'
.slp:
    mov ah, 0
    cmp ax, 300
    jb .sl0
    mov ah, 1
    cmp ax, 420
    jb .sl0
    mov ah, 2
.sl0:
    shl ah, 1
    shl ah, 1
    and byte [sc_pfb], ~SCPA_SPACE & 0xFF
    or [sc_pfb], ah
    jmp short .out
.sb:
    or cx, cx
    jz .out
    or byte [sc_pfb], SCPA_SB
.out:
    pop si
    pop bx
    pop ax
    ret

; sc_rcell - AL = parameter CX in twips as signed cells, clamped. Preserves
; all registers but AX.
sc_rcell:
    push bx
    push cx
    push dx
    mov ax, cx
    cwd
    mov cx, SC_TWIPCELL
    idiv cx
    cmp ax, SC_INDMAX
    jle .lo
    mov ax, SC_INDMAX
.lo:
    cmp ax, -SC_INDMAX
    jge .ok
    mov ax, -SC_INDMAX
.ok:
    pop dx
    pop cx
    pop bx
    ret

; sc_rstreq - compare the NUL string at CS:SI with the one at DS:DI.
; CF=0 = equal. Preserves all registers.
sc_rstreq:
    push ax
    push si
    push di
.step:
    mov al, [cs:si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .step
.yes:
    pop di
    pop si
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop ax
    stc
    ret

; --- the reader's control-word table -----------------------------------------
SCR_SKIP    equ 1                   ; a destination to swallow whole
SCR_PAR     equ 2
SCR_TAB     equ 3
SCR_PLAIN   equ 4
SCR_PARD    equ 5
SCR_BOLD    equ 7                   ; the four flag attributes, in sc_r_bits
SCR_ITAL    equ 8                   ; order
SCR_SCAP    equ 9
SCR_HID     equ 10
SCR_UL      equ 11                  ; the three underlines...
SCR_ULW     equ 12
SCR_ULDB    equ 13
SCR_ULNONE  equ 14                  ; ...and the one that clears them
SCR_QL      equ 15                  ; the four alignments, in jc order
SCR_QC      equ 16
SCR_QR      equ 17
SCR_QJ      equ 18
SCR_LI      equ 19
SCR_FI      equ 20
SCR_RI      equ 21
SCR_SL      equ 22
SCR_SB      equ 23
SCR_PICT    equ 24                  ; \pict - the picture destination, which
                                    ; this reader COLLECTS instead of skipping
SCR_PICWB   equ 25                  ; \wbmwidthbytes  } the four that decide
SCR_PICW    equ 26                  ; \picw           } whether the group is
SCR_PICH    equ 27                  ; \pich           } one we can use, and
SCR_PICBP   equ 28                  ; \wbmbitspixel   } what shape it is
SCR_PICPL   equ 29                  ; \wbmplanes

sc_r_bits:   db SCAT_BOLD, SCAT_ITAL, SCAT_SCAP, SCAT_HID
sc_r_ulbits: db SCAT_UL, SCAT_WUL, SCAT_DUL

sc_r_tbl:
    dw sc_rw_par
    db SCR_PAR
    dw sc_rw_line
    db SCR_PAR
    dw sc_rw_tab
    db SCR_TAB
    dw sc_rw_plain
    db SCR_PLAIN
    dw sc_rw_pard
    db SCR_PARD
    dw sc_rw_b
    db SCR_BOLD
    dw sc_rw_i
    db SCR_ITAL
    dw sc_rw_scaps
    db SCR_SCAP
    dw sc_rw_v
    db SCR_HID
    dw sc_rw_ul
    db SCR_UL
    dw sc_rw_ulw
    db SCR_ULW
    dw sc_rw_uldb
    db SCR_ULDB
    dw sc_rw_ulnone
    db SCR_ULNONE
    dw sc_rw_ql
    db SCR_QL
    dw sc_rw_qc
    db SCR_QC
    dw sc_rw_qr
    db SCR_QR
    dw sc_rw_qj
    db SCR_QJ
    dw sc_rw_li
    db SCR_LI
    dw sc_rw_fi
    db SCR_FI
    dw sc_rw_ri
    db SCR_RI
    dw sc_rw_sl
    db SCR_SL
    dw sc_rw_sb
    db SCR_SB
    dw sc_rw_fonttbl
    db SCR_SKIP
    dw sc_rw_colortbl
    db SCR_SKIP
    dw sc_rw_stylesheet
    db SCR_SKIP
    dw sc_rw_info
    db SCR_SKIP
    dw sc_rw_pict                   ; NOT SCR_SKIP any more (SPEC.md 94.6):
    db SCR_PICT                     ; a \pict this reader understands is a
    dw sc_rw_wbmwb                  ; picture, and one it does not is
    db SCR_PICWB                    ; discarded at the closing brace, which
    dw sc_rw_picw                   ; is the same outcome skipping gave
    db SCR_PICW
    dw sc_rw_pich
    db SCR_PICH
    dw sc_rw_wbmbp
    db SCR_PICBP
    dw sc_rw_wbmpl
    db SCR_PICPL
    dw sc_rw_footnote
    db SCR_SKIP
    dw sc_rw_header
    db SCR_SKIP
    dw sc_rw_footer
    db SCR_SKIP
    dw sc_rw_xe
    db SCR_SKIP
    dw sc_rw_tc
    db SCR_SKIP
    dw 0
    db 0

sc_rw_par:        db 'par', 0
sc_rw_line:       db 'line', 0
sc_rw_tab:        db 'tab', 0
sc_rw_plain:      db 'plain', 0
sc_rw_pard:       db 'pard', 0
sc_rw_b:          db 'b', 0
sc_rw_i:          db 'i', 0
sc_rw_scaps:      db 'scaps', 0
sc_rw_v:          db 'v', 0
sc_rw_ul:         db 'ul', 0
sc_rw_ulw:        db 'ulw', 0
sc_rw_uldb:       db 'uldb', 0
sc_rw_ulnone:     db 'ulnone', 0
sc_rw_ql:         db 'ql', 0
sc_rw_qc:         db 'qc', 0
sc_rw_qr:         db 'qr', 0
sc_rw_qj:         db 'qj', 0
sc_rw_li:         db 'li', 0
sc_rw_fi:         db 'fi', 0
sc_rw_ri:         db 'ri', 0
sc_rw_sl:         db 'sl', 0
sc_rw_sb:         db 'sb', 0
sc_rw_fonttbl:    db 'fonttbl', 0
sc_rw_colortbl:   db 'colortbl', 0
sc_rw_stylesheet: db 'stylesheet', 0
sc_rw_info:       db 'info', 0
sc_rw_pict:       db 'pict', 0
sc_rw_wbmwb:      db 'wbmwidthbytes', 0
sc_rw_wbmbp:      db 'wbmbitspixel', 0
sc_rw_wbmpl:      db 'wbmplanes', 0
sc_rw_picw:       db 'picw', 0      ; \picwgoal tokenises as its OWN word -
sc_rw_pich:       db 'pich', 0      ; letters then digits - so these two do
                                    ; not have to exclude it
sc_rw_footnote:   db 'footnote', 0
sc_rw_header:     db 'header', 0
sc_rw_footer:     db 'footer', 0
sc_rw_xe:         db 'xe', 0
sc_rw_tc:         db 'tc', 0
