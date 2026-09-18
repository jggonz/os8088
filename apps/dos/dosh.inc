; =============================================================================
; apps/dos/dosh.inc - THE BUILT-IN COMMANDS (SPEC.md 96.30)
; =============================================================================
; A `COMMAND.COM` that is not a file. `AH=4Bh` of a program whose name is
; COMMAND.COM does not load anything: it reads the command tail, runs one
; command out of the table below, and hands back an exit code. Nothing is
; spawned, no arena block is taken, and the caller cannot tell the difference
; from the outside - which is the whole point, because Microsoft C's system()
; is `COMSPEC` + `/c` + a string and thousands of DOS programs shell out that
; way for exactly the four or five verbs in here.
;
; MEASURED, NOT GUESSED. Prince of Persia's INSTALL.EXE carries `copy `,
; `*.* `, ` > NUL`, `del install.exe > NUL`, `/c`, `COMSPEC` and `command.com`
; in its data segment (docs/plans/DOS-EXEC-PLAN.md 15.10), so the two commands
; that had to work were fixed before a line of this was written, and the
; wildcard source and the `> NUL` are requirements rather than garnish.
;
; IT IS THE SAME INTERPRETER WAVE 7 WANTS. A prompt in a window differs from
; this in its INPUT (a typed line rather than a command tail) and its OUTPUT
; (a text pane rather than the ROM teletype), and in nothing else - so the
; parser and every verb below are built here, fullscreen, where a real program
; is the test.
;
; NO OSAPI_* FILE SLOT IS CALLED FROM THIS FILE. The back end's rule (96.4)
; is not relaxed for the shell: every file action goes through a `dos_be_*`,
; which is what keeps 14's hibernate phase a second back end rather than a
; rewrite.
; -----------------------------------------------------------------------------

; DSH_LINE, DSH_ARG and DSH_PAT are in dos.asm's constants block,
; EARLY, because the DBSS table sizes this file's buffers and `%assign` cannot
; forward-reference. dosnetabi.inc is in the tree for the same reason.

; -----------------------------------------------------------------------------
; dsh_tail - AH=4Bh of COMMAND.COM: run the command in the parameter block
; in:  ES:BX = the EXEC parameter block (SPEC.md 96.14)
; out: AL = the exit code, always CF=0 - the SHELL ran, whatever the command did
; clobbers: everything but the segment registers
;
; The tail at +2 is a FAR POINTER to a counted string: a length byte, the text,
; and a CR that is not counted. `/c` may be upper or lower case and may be
; followed by a space or not, and a tail with no `/c` at all is an interactive
; shell - which we cannot be, so it is refused rather than guessed at.
; -----------------------------------------------------------------------------
dsh_tail:
    push es
    mov ax, [es:bx+4]               ; the tail's far pointer, taken BEFORE ES
    mov dx, [es:bx+2]               ; moves off the parameter block
    mov es, ax
    mov si, dx
    mov cl, [es:si]                 ; the count byte
    xor ch, ch
    cmp cx, DSH_LINE - 1
    jbe .fits
    mov cx, DSH_LINE - 1            ; a tail longer than any DOS ever made;
.fits:                              ; truncate rather than refuse, because the
    inc si                          ; excess cannot be part of a command we run
    mov di, dsh_line
.cp:
    jcxz .cpend
    mov al, [es:si]
    cmp al, 13
    je .cpend                       ; the CR ends it wherever the count says
    mov [di], al
    inc si
    inc di
    dec cx
    jmp short .cp
.cpend:
    mov byte [di], 0
    pop es

    mov si, dsh_line
    call dsh_skipsp
    cmp byte [si], '/'
    jne .noc
    mov al, [si+1]
    or al, 0x20
    cmp al, 'c'
    jne .noc
    add si, 2
    call dsh_bank                   ; A CHILD SHELL'S DIRECTORY DIES WITH IT
    call dsh_run                    ; under DOS - `command /c cd \FOO` leaves
    push ax                         ; the caller exactly where it was - and
    call dsh_home                   ; running in-process is not a reason to
    pop ax                          ; behave differently. Wave 7's prompt calls
    ret                             ; dsh_run WITHOUT this bracket, which is
                                    ; the whole difference
.noc:
    mov al, 1                       ; no /C: an INTERACTIVE shell was asked
    ret                             ; for, and there is no window to put one
                                    ; in from inside a fullscreen bracket

; -----------------------------------------------------------------------------
; dsh_run - run ONE command line
; in:  SI = the line, ASCIZ, in our segment
; out: AL = the exit code (0 = it worked)
; clobbers: everything but the segment registers
; -----------------------------------------------------------------------------
dsh_run:
    mov byte [dsh_why], DSHW_NONE   ; ...and cleared per command, so a stale
    mov byte [dsh_quiet], 0         ; one cannot be read as this one's - which
                                    ; goes DOUBLE for the quiet flag, whose
                                    ; stale one is not a wrong diagnosis but a
                                    ; box that never speaks again (96.30.7):
                                    ; it is bss, so one `> NUL` silenced every
                                    ; dsh_say for the life of the instance,
                                    ; and a DIR then keeps printing NAMES
                                    ; while losing its sizes and its footer
    call dsh_redir
                                    ; `> NUL` off the end first, so the verb
    jc .badredir                    ; scan never sees it
    call dsh_skipsp
    mov di, dsh_verb
    call dsh_cmdword                ; ...and `DIR/W` is DIR (SPEC.md 96.33.15.2)
    cmp byte [dsh_verb], 0
    je .empty                       ; an empty command line is DOS's own no-op
    mov di, dsh_verb
    call dsh_upper

    ; --- A BARE `X:` IS A DRIVE CHANGE, and it is not a verb -----------------
    ; DOS answers it BEFORE the table, which is why `DIR` and `B:` feel like
    ; the same kind of thing and only one of them is. TWO characters exactly:
    ; `B:` changes drive and `B:FOO` does not - it names a file on B:, which
    ; dsh_word leaves in [dsh_verb] whole, so the length test IS the
    ; distinction and no lookahead is needed.
    ;
    ; From the PROMPT the change persists, because dos_con_run calls dsh_run
    ; with no dsh_bank/dsh_home bracket around it; from `command /c b:` it does
    ; not, because dsh_tail brackets it - and a child shell's drive dying with
    ; it is DOS's own behaviour (SPEC.md 96.30).
    cmp byte [dsh_verb+2], 0
    jne .notdrv
    cmp byte [dsh_verb+1], ':'
    jne .notdrv
    mov al, [dsh_verb]
    cmp al, 'A'
    jb .notdrv
    cmp al, 'Z'
    ja .notdrv
    sub al, 'A'                     ; SPEC.md 96.6's map is the identity
    call dos_vol_to                 ; ...and [dos_curdir] comes with it:
    jc .nodrive                     ; dos_drv_sel banks where we were and
    xor al, al                      ; RECALLS where that drive last stood
    ret                             ; (SPEC.md 96.6.2), so `B: / CD APPS / A: /
                                    ; B:` is back in B:\APPS and not at its root
.nodrive:
    mov si, dsh_s_nodrv
    call dsh_say
    mov byte [dsh_why], DSHW_NODRV
    mov al, 1
    ret
.notdrv:

    mov bx, dsh_tab
.look:
    mov al, [bx]
    or al, al
    jz .unknown
    push si
    mov si, bx
    inc si
    mov di, dsh_verb
    call dsh_same
    pop si
    jc .next
    mov ax, [bx+9]                  ; the handler: SI is the argument text
    jmp ax
.next:
    add bx, 11
    jmp short .look

.unknown:
    mov byte [dsh_why], DSHW_NOCMD  ; **THE MESSAGE IS THE CALLER'S NOW**
    cmp byte [dsh_exec], 0          ; (SPEC.md 96.33.3): from a `/c` shell an
    jne .noexec                     ; unknown verb is nothing else and this is
    mov si, dsh_s_bad               ; the answer, but from the PROMPT it may be
    call dsh_say                    ; a program - so the refusal is held back
    mov si, dsh_verb                ; and [dsh_why] says which arm ran
    call dsh_say
    mov si, dsh_s_crlf
    call dsh_say
.noexec:
    mov al, 1
    ret
.empty:
    xor al, al
    ret
.badredir:
    mov si, dsh_s_redir
    call dsh_say
    mov byte [dsh_why], DSHW_REDIR
    mov al, 1
    ret

; -----------------------------------------------------------------------------
; dsh_redir - cut a `>`, `>>` or `<` off the line and decide what it meant
; in:  SI = the line
; out: CF=0 and the line truncated at the operator; CF=1 = a target we cannot
;      honour, and the caller must refuse the whole command
; clobbers: AX, DI, flags
;
; NUL IS THE ONLY TARGET THAT WORKS, AND ANY OTHER IS REFUSED RATHER THAN
; IGNORED. Our verbs print almost nothing, so quietly dropping `> FILE` would
; be indistinguishable from honouring it right up until somebody read the file
; and found it missing - a wrong answer of SPEC.md 96.22's exact kind. A
; refusal with a non-zero exit code is a normal path (SPEC.md 47) and `> NUL`,
; which is what shelling-out code actually writes, costs nothing.
; -----------------------------------------------------------------------------
dsh_redir:
    push si
    push bx
    mov bx, si
.scan:
    mov al, [bx]
    or al, al
    jz .none
    cmp al, '>'
    je .cut
    cmp al, '<'
    je .cut
    inc bx
    jmp short .scan
.cut:
    mov si, bx                      ; SI walks the target, BX marks the cut
    inc si
    cmp byte [si], '>'
    jne .tgt
    inc si                          ; `>>` is an append, same question
.tgt:
    call dsh_skipsp
    mov di, dsh_rtgt
    call dsh_word
    mov di, dsh_rtgt
    call dsh_upper
    mov byte [bx], 0                ; the command is what came before it
    mov si, dsh_rtgt
    mov di, dsh_s_nul
    call dsh_same
    jc .no
    mov byte [dsh_quiet], 1         ; **HERE AND NOT AT THE `>`** (96.30.7):
                                    ; set before the target was judged, this
                                    ; flag suppressed .badredir's own refusal,
                                    ; so `TYPE > FILE.TXT` printed nothing and
                                    ; read as a command that had worked - the
                                    ; wrong KIND of answer 96.30.3 refuses a
                                    ; non-NUL target to avoid giving
.none:
    pop bx
    pop si
    clc
    ret
.no:
    pop bx
    pop si
    stc
    ret

; --- the small string half ---------------------------------------------------
; dsh_skipsp - advance SI past spaces and tabs.  clobbers: SI, flags
dsh_skipsp:
    cmp byte [si], ' '
    je .step
    cmp byte [si], 9
    jne .done
.step:
    inc si
    jmp short dsh_skipsp
.done:
    ret

; dsh_word - one whitespace-delimited token from SI into DI (DSH_ARG bounded)
; out: SI AT the terminator, the token ASCIZ at DI.  clobbers: AX, CX, SI, flags
;
; dsh_cmdword is the same token with `/` ending it as well, and SI left ON the
; slash so the switch stays in the tail the handler is given (SPEC.md
; 96.33.15.2): `DIR/W` is DIR with `/W` and `PRINCE/F` is PRINCE with `/F`,
; which is what COMMAND.COM does and what a DOS user's fingers do. It is for
; the COMMAND NAME and nothing else - DIR's own argument scan reads `/B` as a
; word and would get an empty one - so there are exactly two callers, dsh_run's
; verb and dos_con_ext's program name.
dsh_cmdword:
    push bx
    mov bl, '/'
    jmp short dsh_word_i
dsh_word:
    push bx
    xor bl, bl                      ; ...and no switch character, so the
                                    ; compare below can never fire: AL is
                                    ; already known not to be 0
dsh_word_i:
    push di
    mov cx, DSH_ARG - 1
.c:
    mov al, [si]
    or al, al
    jz .end
    cmp al, ' '
    je .end
    cmp al, 9
    je .end
    cmp al, bl
    je .end
    jcxz .skip                      ; past the buffer: consume the rest of the
    mov [di], al                    ; token but store none of it, so a long
    inc di                          ; argument truncates instead of running off
    dec cx                          ; into whatever follows
.skip:
    inc si
    jmp short .c
.end:
    mov byte [di], 0
    pop di
    pop bx
    ret

; dsh_upper - upper-case the ASCIZ string at DI in place.  clobbers: AL, flags
dsh_upper:
    push di
.c:
    mov al, [di]
    or al, al
    jz .done
    cmp al, 'a'
    jb .next
    cmp al, 'z'
    ja .next
    sub al, 0x20
    mov [di], al
.next:
    inc di
    jmp short .c
.done:
    pop di
    ret

; dsh_same - are the ASCIZ strings at SI and DI equal?
; out: CF=0 equal, CF=1 not.  clobbers: AX, flags (SI and DI preserved)
dsh_same:
    push si
    push di
.c:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .c
.yes:
    pop di
    pop si
    clc
    ret
.no:
    pop di
    pop si
    stc
    ret

; dsh_say - one ASCIZ string to the screen, unless the line was redirected
; in:  SI = the string.  clobbers: AX, SI, flags
;
; dos_tty is the ROM teletype, which is where a DOS program's own output goes
; inside this bracket - so a built-in's message lands in the same place, in the
; same colours, scrolling the same screen. That is the whole of "output" until
; wave 7 puts a pane behind it.
dsh_say:
    cmp byte [dsh_quiet], 0
    jne .out
.c:
    mov al, [si]
    or al, al
    jz .out
    call dos_tty
    inc si
    jmp short .c
.out:
    ret

; --- names and patterns ------------------------------------------------------
; dsh_to11 - an 8.3 ASCIZ name into the 11-byte padded form a match is decided
;            in, expanding `*` to `?` for the rest of its field
; in:  SI = the name, DI = an 11-byte buffer.  clobbers: AX, CX, SI, DI, flags
;
; DOS's own rule, and the reason a match is done this way round rather than by
; walking the two strings together: `?` matches a character OR the end of the
; name, which in the padded form is just "? matches a space" and needs no
; special case at all.
dsh_to11:
    push di
    mov cx, 8
    call .field                     ; the name...
    mov al, [si]
    cmp al, '.'
    jne .noext
    inc si
.noext:
    mov cx, 3                       ; ...and the extension
    call .field
    pop di
    ret
.field:
    mov al, [si]
    or al, al
    jz .pad
    cmp al, '.'
    je .pad
    cmp al, '*'
    jne .one
    inc si                          ; `*` fills what is left of THIS field with
.star:                              ; `?` and is not itself stored
    mov byte [di], '?'
    inc di
    loop .star
    jmp short .eat
.one:
    cmp al, 'a'
    jb .store
    cmp al, 'z'
    ja .store
    sub al, 0x20
.store:
    mov [di], al
    inc di
    inc si
    loop .field
.eat:
    mov al, [si]                    ; anything past the field's width is
    or al, al                       ; dropped, which is what DOS does to a
    jz .fdone                       ; ninth character
    cmp al, '.'
    je .fdone
    inc si
    jmp short .eat
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.fdone:
    ret

; dsh_match - does the padded name at SI match the padded pattern at DI?
; out: CF=0 it matches, CF=1 it does not.  clobbers: AX, CX, flags
dsh_match:
    push si
    push di
    mov cx, DSH_PAT
.c:
    mov al, [di]
    cmp al, '?'
    je .next
    cmp al, [si]
    jne .no
.next:
    inc si
    inc di
    loop .c
    pop di
    pop si
    clc
    ret
.no:
    pop di
    pop si
    stc
    ret

; --- where a name lives ------------------------------------------------------
; dsh_bank / dsh_home - the place we are standing, banked and put back
; clobbers: AX (bank), AX/BX/DX (home)
dsh_bank:
    mov al, [dos_vol]
    mov [dsh_bvol], al
    mov ax, [dos_curdir]
    mov [dsh_bclus], ax
    ret

dsh_home:
    push ax
    mov al, [dsh_bvol]
    call dos_vol_to                 ; the DRIVE first, because that is what
    mov dx, [dsh_bclus]             ; banks and recalls a per-drive directory
    mov bl, [dsh_bvol]              ; (dos_drv_bank) - going straight to the
    call dos_be_goto                ; cluster would leave that pair disagreeing
    mov [dos_curdir], dx
    pop ax
    ret

; dsh_cdto - stand in the folder the spec at SI names
; in:  SI = an ASCIZ spec in our segment
; out: CF=0 and we are standing there; CF=1 = there is no such folder, and
;      where we stand is then whatever the attempt left (the caller banks)
; clobbers: AX, BX, CX, DX, DI, flags
;
; ONE COMPONENT PAST THE ROOT, which is dos_fh_core's own limit (a separator
; anywhere past a leading one is "path not found", code 3) and not a new one
; invented here. `C:\PRINCE` resolves and `C:\A\B` does not - and that is
; exactly the reach INSTALL.EXE asks for. dos_walk_pbuf is the upgrade when
; something wants deeper, and it is isolated to this routine.
dsh_cdto:
    mov dx, si
    mov ax, ds
    mov [dos_fnseg], ax
    mov di, dos_fname
    call dos_fh_core                ; -> dos_fname, [dos_fdrv], [dos_fabs]
    jc .no
    mov al, [dos_fdrv]
    cmp al, 0xFF
    je .nodrv                       ; no letter: wherever we already are
    call dos_vol_to
    jc .no
.nodrv:
    call dos_cd_go
    jc .no
    clc
    ret
.no:
    stc
    ret

; dsh_resolve - a spec into a PLACE and a LEAF
; in:  SI = the spec, AL = 1 to try the whole spec as a folder first
; out: CF=0 with [dsh_rdrv]/[dsh_rclus] the folder and dsh_leaf the last
;      component - EMPTY when the whole spec was itself a folder; CF=1 = no
;      such folder
; clobbers: AX, BX, CX, DX, SI, DI, flags
;
; AL=1 IS HOW `copy *.* C:\PRINCE` KNOWS WHAT PRINCE IS. DOS decides a
; destination by looking: if it exists and is a directory the files keep their
; own names, and otherwise the last component is the name to write. That is a
; question about the disk rather than about the string, so it is asked of the
; disk - and asked FIRST, because `PRINCE` is a perfectly good filename too.
dsh_resolve:
    mov [dsh_asdir], al
    mov di, dsh_spec
    call dsh_cpspec
    mov byte [dsh_leaf], 0

    cmp byte [dsh_asdir], 0
    je .split
    call dsh_bank
    mov si, dsh_spec
    call dsh_cdto
    pushf                           ; ...AND THE popf GOES AFTER dsh_home, not
    call .record                    ; before it: dsh_home ends in dos_be_goto,
    call dsh_home                   ; whose CF=0 would otherwise become the
    popf                            ; answer to "was that a folder" - so every
    jnc .ok                         ; destination looked like one and every
                                    ; copy read as a copy onto itself

.split:
    mov si, dsh_spec
    xor bx, bx                      ; BX = the spec's length
.scan:
    cmp byte [si+bx], 0
    je .findsep
    inc bx
    jmp short .scan
.findsep:
    mov si, dsh_spec                ; ...then walk BACK for a separator. SI and
    add si, bx                      ; not CX: on an 8086 only BX, BP, SI and DI
.back:                              ; index, so the pointer has to be the one
    cmp si, dsh_spec                ; that moves
    jbe .colon
    dec si
    cmp byte [si], '\'
    jne .back
    inc si                          ; SI = the LEAF, always past the separator -
    jmp short .havecut              ; where the folder part ends is decided below
.colon:
    cmp bx, 2                       ; no separator: is there a drive letter?
    jb .nodir
    cmp byte [dsh_spec+1], ':'
    jne .nodir
    mov si, dsh_spec                ; "d:" is the whole folder part
    add si, 2
.havecut:
    push si                         ; SI is the leaf, and everything before it
    mov di, dsh_leaf                ; is the folder
    call dsh_cpz
    pop si

    ; **AND THE FOLDER PART DROPS THE SEPARATOR** (SPEC.md 96.30.8), unless it
    ; is the spec's FIRST character - which is the case the old rule was
    ; written for, where cutting before it leaves an empty string where the
    ; caller meant the root. Everywhere else it left a TRAILING one, so
    ; `BIN\ONE.TXT` handed dsh_cdto the string `BIN\` - one component and
    ; that component EMPTY, which dos_cd_go reads as `where we already are`
    ; and answers SUCCESS. The walk silently did not happen, the leaf was
    ; looked for in the wrong folder, and every verb said File not found
    ; about a file that was plainly there.
    cmp si, dsh_spec + 1            ; a leaf at +1 means the separator LEADS,
    jbe .term                       ; so keep it: the folder part is `\`
    cmp byte [si-1], '\'             ; ...and .colon arrives here too, where the
    jne .term                       ; byte before the leaf is a COLON and the
    dec si                          ; folder part `d:` is already whole
.term:
    mov byte [si], 0
    call dsh_bank
    mov si, dsh_spec
    call dsh_cdto
    pushf
    call .record
    call dsh_home
    popf
    jc .no
    jmp short .ok

.nodir:
    mov si, dsh_spec                ; no folder part at all: right here
    mov di, dsh_leaf
    call dsh_cpz
    mov al, [dos_vol]
    mov [dsh_rdrv], al
    mov ax, [dos_curdir]
    mov [dsh_rclus], ax
.ok:
    clc
    ret
.no:
    stc
    ret
.record:
    mov al, [dos_vol]
    mov [dsh_rdrv], al
    mov ax, [dos_curdir]
    mov [dsh_rclus], ax
    ret

; dsh_cpspec - SI -> dsh_spec, bounded.  clobbers: AX, CX, SI, DI, flags
dsh_cpspec:
    mov di, dsh_spec
; dsh_cpz - ASCIZ from SI to DI, DSH_ARG bounded and always terminated
dsh_cpz:
    push di
    mov cx, DSH_ARG - 1
.c:
    mov al, [si]
    or al, al
    jz .end
    jcxz .end
    mov [di], al
    inc si
    inc di
    dec cx
    jmp short .c
.end:
    mov byte [di], 0
    pop di
    ret

; =============================================================================
; THE VERBS
; =============================================================================
; dsh_two - the `<source> <destination>` argument shape every file verb has
; in:  SI = the argument text
; out: CF=0 with the source resolved into [dsh_sdrv]/[dsh_sclus] + dsh_pat (the
;      padded pattern) and the destination into [dsh_ddrv]/[dsh_dclus] +
;      dsh_dname (empty = keep each source's own name); CF=1 with AL = the exit
;      code and the message already said
; clobbers: everything but the segment registers
dsh_two:
    call dsh_skipsp
    mov di, dsh_a1
    call dsh_word
    call dsh_skipsp
    mov di, dsh_a2
    call dsh_word
    cmp byte [dsh_a1], 0
    je .nosrc
    cmp byte [dsh_a2], 0
    je .nodst

    mov si, dsh_a1                  ; the SOURCE is never "try it as a folder":
    xor al, al                      ; `copy B:` means every file on B:, and
    call dsh_resolve                ; asking the disk would make it mean the
    jc .nopath                      ; folder itself
    mov al, [dsh_rdrv]
    mov [dsh_sdrv], al
    mov ax, [dsh_rclus]
    mov [dsh_sclus], ax
    cmp byte [dsh_leaf], 0
    jne .haspat
    mov word [dsh_leaf], '*' | ('.' << 8)   ; a bare folder means everything in
    mov word [dsh_leaf+2], '*'              ; it, which is `*.*`
.haspat:
    mov si, dsh_leaf
    mov di, dsh_pat
    call dsh_to11

    mov si, dsh_a2                  ; ...and the DESTINATION always is
    mov al, 1
    call dsh_resolve
    jc .nopath
    mov al, [dsh_rdrv]
    mov [dsh_ddrv], al
    mov ax, [dsh_rclus]
    mov [dsh_dclus], ax
    mov si, dsh_leaf
    mov di, dsh_dname
    call dsh_cpz
    clc
    ret
.nosrc:
.nodst:
    mov si, dsh_s_syntax
    call dsh_say
    mov byte [dsh_why], DSHW_SYNTAX
    mov al, 1
    stc
    ret
.nopath:
    mov si, dsh_s_nopath
    call dsh_say
    mov byte [dsh_why], DSHW_NOPATH
    mov al, 3
    stc
    ret

; -----------------------------------------------------------------------------
; dsh_nth - the [dsh_skip]th name in the CURRENT folder matching dsh_pat
; out: CF=0 with dsh_fname = the name and [dsh_isdir] set; CF=1 = no more
; clobbers: AX, BX, CX, DX, SI, DI, flags
;
; BY ORDINAL AND WITH A SKIP COUNT, because OSAPI_FILE_FIND is by ordinal and
; a directory that is being WRITTEN renumbers underneath one (SPEC.md 19.7.1).
; A copy leaves the source folder alone, so it walks with the skip count
; rising; a DELETE or a MOVE takes an entry out, so it asks for the FIRST match
; every time and each pass makes progress. One loop, one parameter, and the
; destructive case cannot skip a file by counting one that has gone.
; -----------------------------------------------------------------------------
dsh_nth:
    push es
    push ds
    pop es
    xor cx, cx                      ; the ordinal
    mov dx, [dsh_skip]              ; how many matches to pass over
.step:
    mov di, dsh_fnd
    call dos_be_find
    jc .none
    push cx
    mov si, dsh_fnd                 ; +0 is the ASCIZ name
    mov di, dsh_nm11
    call dsh_to11
    mov si, dsh_nm11
    mov di, dsh_pat
    call dsh_match
    pop cx
    jc .step
    cmp word [dsh_fnd+14], OSAPI_FT_DIR
    jae .step                       ; a folder is not a file to copy, and '..'
                                    ; is never reported anyway (96.12.2)
    or dx, dx
    jz .got
    dec dx
    jmp short .step
.got:
    mov si, dsh_fnd
    mov di, dsh_fname
    call dsh_cpz
    pop es
    clc
    ret
.none:
    pop es
    stc
    ret

; -----------------------------------------------------------------------------
; dsh_c_copy - COPY, and the engine XCOPY and MOVE also run
; in:  SI = the arguments
; out: AL = the exit code
; -----------------------------------------------------------------------------
dsh_c_copy:
    mov byte [dsh_del], 0
dsh_copy_go:
    call dsh_two
    jc .out
    mov al, [dsh_sdrv]              ; ONTO ITSELF is refused, and not only
    cmp al, [dsh_ddrv]              ; because DOS refuses it: a copy INTO the
    jne .ok                         ; folder being walked adds entries, which
    mov ax, [dsh_sclus]             ; renumbers the ordinals dsh_nth is
    cmp ax, [dsh_dclus]             ; counting, and the walk would never end
    jne .ok
    cmp byte [dsh_dname], 0
    je .self                        ; same folder, no new name
.ok:
    call dsh_bank
    call dsh_bufget                 ; the DOS arena's, not the heap's
    jc .nomem
    mov word [dsh_skip], 0
    mov word [dsh_n], 0
.each:
    mov dx, [dsh_sclus]             ; stand in the source folder for the walk
    mov bl, [dsh_sdrv]
    call dos_be_goto
    jc .ioerr
    mov [dos_curdir], dx
    call dsh_nth
    jc .done

    mov si, dsh_dname               ; the destination's name: the one it was
    cmp byte [dsh_dname], 0         ; given, or each source's own
    jne .named
    mov si, dsh_fname
.named:
    mov di, dsh_cname
    call dsh_cpz

    cmp byte [dsh_del], 0
    je .justcopy

    mov si, dsh_fname               ; THE ENGINE FIRST: on one volume this
    mov bl, [dsh_sdrv]              ; rewrites the directory entry and reads no
    mov bh, [dsh_ddrv]              ; data at all, and elsewhere it copies and
    mov dx, [dsh_sclus]             ; deletes through ITS buffer (SPEC.md
    mov cx, [dsh_dclus]             ; 22.24)
    call dos_be_move
    jnc .moved
    cmp ax, FERR_FULL               ; ...which inside a bracket it cannot claim
    jne .ioerr                      ; (96.30.6): nothing was written, so stream
    call dsh_stream                 ; through OURS - the copy half of the
    jc .ioerr                       ; fallback - and then delete the source
    mov dx, [dsh_sclus]             ; ...then the source, once it is whole
    mov bl, [dsh_sdrv]
    call dos_be_goto
    jc .ioerr
    mov [dos_curdir], dx
    mov si, dsh_fname
    call dsh_delone
    jc .ioerr
.moved:
    inc word [dsh_n]
    jmp .each                       ; the source lost an entry, so the FIRST
                                    ; match is the next one
.justcopy:
    call dsh_stream
    jc .ioerr
    inc word [dsh_n]
    inc word [dsh_skip]             ; the source is untouched, so step past the
    jmp .each                       ; one just done

.done:
    call dsh_bufput
    call dsh_home
    cmp word [dsh_n], 0
    je .nofile
    call dsh_count
    xor al, al
    ret
.ioerr:
    call dsh_bufput
    call dsh_home
    mov si, dsh_s_ioerr
    call dsh_say
    mov byte [dsh_why], DSHW_IO
    mov al, 1
    ret
.nomem:
    call dsh_home
    mov si, dsh_s_nomem
    call dsh_say
    mov byte [dsh_why], DSHW_MEM
    mov al, 1
    ret
.nofile:
    mov si, dsh_s_nofile
    call dsh_say
    mov byte [dsh_why], DSHW_NOFILE
    mov al, 1
    ret
.self:
    mov si, dsh_s_self
    call dsh_say
    mov byte [dsh_why], DSHW_SELF
    mov al, 1
.out:
    ret

dsh_c_move:
    mov byte [dsh_del], 1
    jmp dsh_copy_go

dsh_c_xcopy:
    mov byte [dsh_del], 0
    jmp dsh_copy_go

; -----------------------------------------------------------------------------
; dsh_bufget / dsh_bufput - THE COPY BUFFER, OUT OF THE DOS ARENA
; out: CF=0 with [dsh_cpseg] a block and [dsh_cpkb] its KB; CF=1 = none
; clobbers: AX, BX, ES, flags
;
; NOT THE HEAP, AND NOT THE DISK CACHE - and the second of those is the
; interesting one (SPEC.md 96.30.6). A copy's payload never enters the
; read-ahead cache: SPEC.md 18.95.1 skips the fill whenever the caller's
; request already covers the whole chunk, because filling would read exactly
; what the direct path reads and evict a slot for sectors nobody asks for
; twice - which is what stops a streaming copy flushing the DIRECTORY chunks
; out from under itself. So there is no cached copy of the payload to write
; from; the bytes are DMA'd into a buffer once and written out of it once, and
; the only question is whose buffer.
;
; Inside the fsx bracket the heap is the DOS program's, every byte of it, so
; OSAPI_FILE_COPY's own claim answers FERR_FULL. The arena is where DOS itself
; would take it: a program that EXECs must free memory first - that is the DOS
; contract and tests/dosexec/parent.asm asserts it, a 4Bh before AH=4Ah
; answering 8 - so a program that shells out has, by construction, left room
; for a shell to load into. Ours loads a buffer instead of a COMMAND.COM.
; -----------------------------------------------------------------------------
; --- dsh_clkb - BX = this volume's cluster size in KB, at least 1 -----------
; A cluster is a power of two SECTORS by FAT's own rule - dos_fh_fill's mask
; already leans on it - so KB is sectors/2 with 1 as the floor, and that floor
; is exact rather than a fudge: a 512-byte cluster makes every whole KB two of
; them. Preserves everything but BX.
dsh_clkb:
    push ax
    call dos_be_dfree               ; BX = SECTORS per cluster, and this is the
    jc .guess                       ; O(clusters) call the SDK warns about, so
    or bx, bx                       ; it is asked once per BUFFER and never in
    jnz .got                        ; the chunk loop that uses one
.guess:
    mov bx, 2                       ; a volume that will not say: 1KB, which is
.got:                               ; the 360KB floppy's own and a multiple of
    inc bx                          ; every smaller one
    shr bx, 1                       ; sectors -> KB, rounding 1 sector up to 1
    pop ax
    ret

; **AND WHERE IT COMES FROM DEPENDS ON WHETHER THERE IS A PROGRAM** (96.30.7.2).
; 96.30.6 sized this against `a program that shells out`, which by the DOS
; contract has left room for a shell to load into - so the block comes out of
; the DOS ARENA, where a real COMMAND.COM would load itself. At the PROMPT
; there is no program and no arena: [dos_arena] is claimed inside dos_run and
; freed back to 0 on the way out, so dos_mcb_alloc walks a chain that starts at
; segment 0, finds no MCB signature in the IVT and refuses every time. That is
; the windowed caller 96.30.6's own last paragraph says has a heap, so at the
; prompt it takes one - the same KB, through OSAPI_MEM_CLAIM.
dsh_bufget:
    mov word [dsh_cpseg], 0
    push ax
    call dsh_clkb                   ; **AND THE SIZE IS CUT TO IT** (96.30.7)
    pop ax
    cmp ax, bx                      ; ...which is also the floor: OSAPI_FILE_
    jae .round                      ; READ_AT refuses a capacity under one
    mov ax, bx                      ; cluster outright (18.4.4)
.round:
    push bx                         ; up to the next whole cluster, by MASK:
    dec bx                          ; the cluster is a power of two, which is
    add ax, bx                      ; the same fact dos_fh_fill's own rounding
    pop bx                          ; rests on
    push bx
    neg bx
    and ax, bx
    pop bx
    cmp ax, 1024                    ; ...and a block this cannot NAME: the
    jae .none                       ; shl below is 16 bits of paragraphs
    mov byte [dsh_cpheap], 0
    cmp word [dos_arena], 0
    jne .try
    cmp word [dos_hkv + DHK_CLAIM], 0
    je .none                        ; **NO ARENA AND NO HEAP** (SPEC.md
                                    ; 96.44.6): that pair is `kern_dos`, where
                                    ; the program owns everything above the
                                    ; floor and there is nothing to claim
                                    ; from. It cannot arise there - `kd_entry`
                                    ; sets [dos_arena] before any door and
                                    ; there is no prompt to reach this without
                                    ; one - and saying so is what lets the core
                                    ; hold no `OSAPI_*` call at all
    mov byte [dsh_cpheap], 1        ; ...the prompt's arm, decided ONCE so the
.try:                               ; put below cannot free the other one
    mov [dsh_cpkb], ax
    push bx
    cmp byte [dsh_cpheap], 0
    je .arena
    call word [dos_hkv + DHK_CLAIM] ; AX = KB -> DX = the base segment
    mov ax, dx                      ; ...and this arm answers where the other
    jmp short .did                  ; one does, in AX
.arena:
    mov bx, ax
    mov cl, 6
    shl bx, cl                      ; KB -> paragraphs
    call dos_mcb_alloc
.did:
    pop bx
    jnc .got
    mov ax, [dsh_cpkb]              ; halve and retry: a machine that cannot
    shr ax, 1                       ; fund 8KB can very often fund one, and a
    push bx                         ; smaller buffer is a slower copy rather
    neg bx                          ; than no copy. BACK ONTO A CLUSTER after
    and ax, bx                      ; the halving, DOWNWARD: a request that is
    pop bx                          ; not a power of two halves to an odd
    cmp ax, bx                      ; multiple, and rounding UP here could not
    jae .try                        ; terminate. NEVER under a cluster,
.none:                              ; which is what DSH_CPMINKB's flat 1KB got
    stc                             ; wrong: on a 2KB- or 4KB-cluster volume -
    ret                             ; a hard disk - the last retry handed COPY
.got:                               ; a buffer READ_AT could not be asked for,
    mov [dsh_cpseg], ax             ; and every gate disk here is a floppy
    clc                             ; whose cluster is 512 bytes, so nothing
    ret                             ; could see it

dsh_bufput:
    push ax
    push dx
    push es
    mov ax, [dsh_cpseg]
    or ax, ax
    jz .none
    cmp byte [dsh_cpheap], 0
    je .arena
    mov dx, ax
    call word [dos_hkv + DHK_FREE]  ; ...and only ever the host that CLAIMED it:
    jmp short .gone                 ; [dsh_cpheap] was decided once, at the get
.arena:
    mov es, ax
    call dos_mcb_free
.gone:
    mov word [dsh_cpseg], 0
.none:
    pop es
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dsh_stream - ONE file, from the folder we are standing in to the destination
; in:  dsh_fname = the source's name, dsh_cname = the destination's,
;      [dsh_ddrv]/[dsh_dclus] the destination place; we are STANDING in the
;      source folder and are put back there
; out: CF=0 copied; CF=1 with AX = FERR_*
; clobbers: AX, BX, CX, DX, SI, DI, flags
;
; A HAND-ROLLED COPY, WHICH SPEC.md 22.24 WARNS ABOUT - and the half it says
; gets got wrong is written here on purpose: a destination that was created
; and then failed is DELETED, so a half-copy never survives as a plausible
; file. The other half of the warning, the redirector's FSV_COPY fast path, is
; genuinely lost; it applies to a remote-to-remote pair, which is not a thing
; a DOS program inside an fsx bracket has.
; -----------------------------------------------------------------------------
dsh_stream:
    mov word [dsh_off], 0
    mov word [dsh_off+2], 0
    mov byte [dsh_made], 0
.chunk:
    mov dx, [dsh_sclus]             ; the SOURCE, every pass: the write below
    mov bl, [dsh_sdrv]              ; stands in the destination folder
    call dos_be_goto
    jc .ioerr
    mov [dos_curdir], dx
    mov es, [dsh_cpseg]
    mov si, dsh_fname
    xor bx, bx
    mov cx, [dsh_cpkb]
    mov ax, cx
    mov cl, 10
    shl ax, cl                      ; KB -> bytes; DSH_CPKB is small enough
    mov cx, ax                      ; that this cannot reach 64KB
    mov dx, [dsh_off+2]
    mov ax, [dsh_off]
    call dos_be_rdat                ; out DX:AX = what it delivered
    jc .ioerr
    mov cx, ax
    or cx, cx
    jnz .write
    cmp word [dsh_off], 0           ; nothing delivered on the FIRST pass is an
    jne .done                       ; empty file, which still has to be created
    cmp word [dsh_off+2], 0
    jne .done
.write:
    mov [dsh_got], cx
    mov dx, [dsh_dclus]
    mov bl, [dsh_ddrv]
    call dos_be_goto
    jc .ioerr
    mov [dos_curdir], dx
    mov es, [dsh_cpseg]
    mov si, dsh_cname
    xor bx, bx
    mov cx, [dsh_got]
    cmp byte [dsh_made], 0
    jne .app
    xor dx, dx                      ; the FIRST chunk CREATES (and replaces)...
    call dos_be_write
    jc .ioerr
    mov byte [dsh_made], 1
    jmp short .step
.app:
    call dos_be_append              ; ...and every one after it extends
    jc .ioerr
.step:
    mov cx, [dsh_got]
    add [dsh_off], cx
    adc word [dsh_off+2], 0
    or cx, cx
    jnz .chunk
.done:
    clc
    ret
.ioerr:
    push ax                         ; THE UNDO: a destination that exists and
    cmp byte [dsh_made], 0          ; is short is worse than no destination at
    je .nofile                      ; all, because it looks like a file
    mov dx, [dsh_dclus]
    mov bl, [dsh_ddrv]
    call dos_be_goto
    jc .nofile
    mov [dos_curdir], dx
    mov si, dsh_cname
    call dos_be_delete
.nofile:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dsh_delone - delete the name at SI in the folder we are standing in
; out: CF from the back end.  clobbers: AX, flags
dsh_delone:
    push si
    call dos_be_delete
    pop si
    ret

; dsh_c_del - DEL and ERASE
dsh_c_del:
    call dsh_skipsp
    mov di, dsh_a1
    call dsh_word
    cmp byte [dsh_a1], 0
    je .syntax
    mov si, dsh_a1
    xor al, al
    call dsh_resolve
    jc .nopath
    cmp byte [dsh_leaf], 0
    jne .haspat
    mov word [dsh_leaf], '*' | ('.' << 8)
    mov word [dsh_leaf+2], '*'
.haspat:
    mov si, dsh_leaf
    mov di, dsh_pat
    call dsh_to11

    call dsh_bank
    mov word [dsh_skip], 0          ; ALWAYS THE FIRST MATCH: each delete takes
    mov word [dsh_n], 0             ; an entry out and renumbers the ordinals
.each:
    mov dx, [dsh_rclus]
    mov bl, [dsh_rdrv]
    call dos_be_goto
    jc .ioerr
    mov [dos_curdir], dx
    call dsh_nth
    jc .done
    mov si, dsh_fname
    call dsh_delone
    jc .ioerr
    inc word [dsh_n]
    jmp short .each
.done:
    call dsh_home
    cmp word [dsh_n], 0
    je .nofile
    xor al, al
    ret
.ioerr:
    call dsh_home
    mov si, dsh_s_ioerr
    call dsh_say
    mov byte [dsh_why], DSHW_IO
    mov al, 1
    ret
.nofile:
    mov si, dsh_s_nofile
    call dsh_say
    mov byte [dsh_why], DSHW_NOFILE
    mov al, 1
    ret
.syntax:
    mov si, dsh_s_syntax
    call dsh_say
    mov byte [dsh_why], DSHW_SYNTAX
    mov al, 1
    ret
.nopath:
    mov si, dsh_s_nopath
    call dsh_say
    mov byte [dsh_why], DSHW_NOPATH
    mov al, 3
    ret

; -----------------------------------------------------------------------------
; dsh_c_ren - REN and RENAME: both names in ONE folder, which is what DOS's own
; AH=56h does (SPEC.md 96.31) and what OSAPI_FILE_RENAME publishes. A rename
; that would move is MOVE's job and says so.
dsh_c_ren:
    call dsh_skipsp
    mov di, dsh_a1
    call dsh_word
    call dsh_skipsp
    mov di, dsh_a2
    call dsh_word
    cmp byte [dsh_a1], 0
    je .syntax
    cmp byte [dsh_a2], 0
    je .syntax
    mov si, dsh_a1
    xor al, al
    call dsh_resolve
    jc .nopath
    cmp byte [dsh_leaf], 0
    je .syntax
    mov si, dsh_leaf                ; the old name, out of the resolve
    mov di, dsh_a1
    call dsh_cpz

    call dsh_bank
    mov dx, [dsh_rclus]
    mov bl, [dsh_rdrv]
    call dos_be_goto
    jc .ioerr
    mov [dos_curdir], dx
    mov si, dsh_a1
    mov di, dsh_a2
    call dos_be_rename
    jc .ioerr
    call dsh_home
    xor al, al
    ret
.ioerr:
    call dsh_home
    mov si, dsh_s_ioerr
    call dsh_say
    mov byte [dsh_why], DSHW_IO
    mov al, 1
    ret
.syntax:
    mov si, dsh_s_syntax
    call dsh_say
    mov byte [dsh_why], DSHW_SYNTAX
    mov al, 1
    ret
.nopath:
    mov si, dsh_s_nopath
    call dsh_say
    mov byte [dsh_why], DSHW_NOPATH
    mov al, 3
    ret

; -----------------------------------------------------------------------------
; dsh_c_md / dsh_c_rd / dsh_c_cd - one folder name each
dsh_c_md:
    mov byte [dsh_dirop], 0
    jmp short dsh_dir_go
dsh_c_rd:
    mov byte [dsh_dirop], 1
    jmp short dsh_dir_go
dsh_c_cd:
    mov byte [dsh_dirop], 2
dsh_dir_go:
    call dsh_skipsp
    mov di, dsh_a1
    call dsh_word
    cmp byte [dsh_a1], 0
    je .syntax
    cmp byte [dsh_dirop], 2
    je .chdir

    mov si, dsh_a1                  ; MD and RD name a folder INSIDE another,
    xor al, al                      ; so the leaf is the new name and the place
    call dsh_resolve                ; is where it goes
    jc .nopath
    cmp byte [dsh_leaf], 0
    je .syntax
    call dsh_bank
    mov dx, [dsh_rclus]
    mov bl, [dsh_rdrv]
    call dos_be_goto
    jc .ioerr
    mov [dos_curdir], dx
    mov si, dsh_leaf
    cmp byte [dsh_dirop], 0
    jne .rm
    call dos_be_mkdir
    jmp short .fin
.rm:
    xor al, al                      ; strict: a folder with anything in it is
    call dos_be_rmdir               ; refused, which is DOS's own answer
.fin:
    jc .ioerr
    call dsh_home
    xor al, al
    ret
.chdir:
    mov si, dsh_a1                  ; ...and CD just stands there. In a /C
    call dsh_cdto                   ; shell dsh_tail puts us back afterwards,
    jc .nopath                      ; which is what a child shell's exit does
    xor al, al                      ; under DOS
    ret
.ioerr:
    call dsh_home
    mov si, dsh_s_ioerr
    call dsh_say
    mov byte [dsh_why], DSHW_IO
    mov al, 1
    ret
.syntax:
    mov si, dsh_s_syntax
    call dsh_say
    mov byte [dsh_why], DSHW_SYNTAX
    mov al, 1
    ret
.nopath:
    mov si, dsh_s_nopath
    call dsh_say
    mov byte [dsh_why], DSHW_NOPATH
    mov al, 3
    ret

; -----------------------------------------------------------------------------
; dsh_c_type - a file to the screen, through the ROM teletype (SPEC.md 96.30.7)
;
; **THE BUFFER IS THE COPY BUFFER AND THAT IS THE WHOLE FIX.** This read in
; 128-byte chunks out of a bss array, and OSAPI_FILE_READ_AT will not take a
; capacity that is not a whole number of CLUSTERS (18.4.4) - so it was refused
; on its first chunk, on every file, on every volume, and reported the refusal
; as `File creation error`. The smallest cluster this machine has is one
; 512-byte sector, so no geometry could ever have made 128 legal. dsh_bufget
; is where COPY already gets a cluster-shaped block out of the DOS arena
; (96.30.6), so there is one buffer and one sizing rule.
;
; TWO ARMS, and the second is the handle layer's own decision (96.30.7.1):
; READ_AT is RAW (20.14.3), so a 'CZ' file - which README.TXT is on every
; shipped floppy - would be typed as its wrapper. A compressed file is read
; WHOLE through OSAPI_FILE_READ, which expands on the way in, exactly as
; AH=3Dh opens one FHF_WHOLE.
; -----------------------------------------------------------------------------
dsh_c_type:
    call dsh_skipsp
    mov di, dsh_a1
    call dsh_word
    cmp byte [dsh_a1], 0
    je .syntax
    mov si, dsh_a1
    xor al, al
    call dsh_resolve
    jc .nopath
    cmp byte [dsh_leaf], 0
    je .syntax
    call dsh_bank
    mov dx, [dsh_rclus]
    mov bl, [dsh_rdrv]
    call dos_be_goto
    jc .nopath
    mov [dos_curdir], dx

    call dsh_stat                   ; --- is it there, how big, and is it
    jc .nofile                      ; COMPRESSED? DX:AX = the UNPACKED size,
    test bl, OSAPI_FIND_CZ          ; BL = the flags
    jnz .czarm

    mov ax, DSH_CPKB                ; --- PLAIN: stream it, a cluster-shaped
    call dsh_bufget                 ; chunk at a time
    jc .nomem
    mov word [dsh_off], 0
    mov word [dsh_off+2], 0
.chunk:
    push ds
    pop es                          ; dos_be_rdat takes the NAME in DS:SI and
    mov si, dsh_leaf                ; the buffer in ES:BX, so ES is the block
    mov es, [dsh_cpseg]             ; and the name is read before it moves
    mov bx, 0
    mov cx, [dsh_cpkb]
    mov ax, cx
    mov cl, 10
    shl ax, cl                      ; KB -> bytes. A cluster of 64KB would wrap
    mov cx, ax                      ; this to zero and READ_AT refuses a zero
    mov dx, [dsh_off+2]             ; capacity, so that machine gets an honest
    mov ax, [dsh_off]               ; refusal rather than a wrong read - and no
    call dos_be_rdat                ; volume this OS mounts has one
    jc .rderr
    mov cx, ax
    or cx, cx
    jz .fin                         ; at or past the end: the loop's own end
    add [dsh_off], cx
    adc word [dsh_off+2], 0
    call .put
    jc .fin                         ; ^Z
    jmp short .chunk

.czarm:
    ; --- COMPRESSED: ONE whole read, which EXPANDS (96.30.7.1) --------------
    ; The size is the UNPACKED one - it is what OSAPI_FILE_FIND reports and
    ; what DIR already prints - so the block is sized from what will ARRIVE
    ; rather than from what is on the disk. 64KB is the bound and it is the
    ; PRINTER's: the walk below indexes one segment, and a block bigger than
    ; that cannot be reached from it.
    or dx, dx
    jnz .toobig
    add ax, 1023
    jc .toobig
    mov cl, 10
    shr ax, cl                      ; bytes -> KB, rounded up
    or ax, ax
    jnz .czget
    inc ax                          ; an empty file still wants a block
.czget:
    push ax                         ; **THE HALVING IS NOT ALLOWED HERE**: a
    call dsh_bufget                 ; whole-file read into a smaller block is
    pop bx                          ; FERR_BIG, decided off the directory entry
    jc .nomem                       ; before any I/O, so a half-sized buffer
    cmp [dsh_cpkb], bx              ; buys nothing at all. The cluster rounding
    jb .nomemb                      ; may make it BIGGER, which is free
    push ds
    pop es
    mov si, dsh_leaf
    mov es, [dsh_cpseg]
    xor bx, bx
    mov ax, [dsh_cpkb]
    mov cx, 1024
    mul cx                          ; DX:AX = the capacity in BYTES, 32 bits,
    mov cx, ax                      ; which is what this slot takes
    call dos_be_read                ; out DX:AX = the UNPACKED size
    jc .rderr
    mov cx, ax
    jcxz .fin
    xor bx, bx
    call .put
.fin:
    call dsh_bufput
    call dsh_home
    xor al, al
    ret

; --- .put - CX bytes at [dsh_cpseg]:BX to the console --------------------
; out: CF=1 if a ^Z ended the file. BX walks; ES and CX ride through dos_tty,
; whose console arm brackets every SI and ES it touches (apps/os88con.inc) and
; whose ROM arm is int 10h AH=0Eh.
.put:
    jcxz .putok
.p1:
    mov al, [es:bx]
    cmp al, 26                      ; ^Z ends a text file, as it does under DOS
    je .puteof
    call dos_tty
    inc bx
    loop .p1
.putok:
    clc
    ret
.puteof:
    stc
    ret

.nomemb:
    call dsh_bufput
.nomem:
    mov si, dsh_s_nomem             ; ...and it is DOS's own sentence, which
    mov ah, DSHW_MEM                ; dsh_bufget's other caller already uses
    jmp short .bad
.toobig:
    mov si, dsh_s_toobig
    mov ah, DSHW_IO
    jmp short .bad
.rderr:
    call dsh_bufput
    mov si, dsh_s_rderr             ; **AND NOT `File creation error`**: TYPE
    mov ah, DSHW_IO                 ; creates nothing, and reporting COPY's
    jmp short .bad                  ; message here sent the first person to
.nofile:                            ; try the verb looking for a destination
    mov si, dsh_s_nofile            ; to give it (96.30.7)
    mov ah, DSHW_NOFILE
.bad:
    call dsh_home
    call dsh_say
    mov [dsh_why], ah
    mov al, 1
    ret
.syntax:
    mov si, dsh_s_syntax
    call dsh_say
    mov byte [dsh_why], DSHW_SYNTAX
    mov al, 1
    ret
.nopath:
    mov si, dsh_s_nopath
    call dsh_say
    mov byte [dsh_why], DSHW_NOPATH
    mov al, 3
    ret

; --- dsh_stat - dos_fh_stat on the leaf we are standing on -------------------
; out: CF=0 with DX:AX = the UNPACKED size and BL = the OSAPI_FIND_* flags;
;      CF=1 = no such file in this folder
;
; Through [dos_fname] because dos_fh_stat is the walker AH=3Dh opens through,
; and one walker is the point: it is what knows that a folder is not a file and
; that '..' is not either. The buffer is the handle layer's scratch for a name
; and is not held across anything.
;
; **AND THE COPY UPPER-CASES, because dos_fh_stat's compare is byte-exact**
; (SPEC.md 96.30.7.1). 8.3 names are upper case on the disk, which is why
; dos_fh_core upper-cases at the same job one layer along - and this did not,
; so `type readme.txt` answered `File not found` where `TYPE README.TXT`
; printed the file. No other verb has it: every one of them hands the name to
; the KERNEL, whose dskw_name83 upper-cases into the 8.3 field, so only the
; verb that asks a question about a name BEFORE handing it over could be
; case-sensitive at all.
dsh_stat:
    push si
    push di
    push cx
    push es
    push ds
    pop es
    mov si, dsh_leaf
    mov di, dos_fname
    mov cx, 12                      ; an 8.3 name, and the terminator after it
    cld
.cp:
    lodsb
    call dos_upc
    stosb
    or al, al
    jz .done
    loop .cp
    mov byte [es:di], 0
.done:
    pop es
    pop cx
    pop di
    pop si
    jmp dos_fh_stat

; -----------------------------------------------------------------------------
; dsh_c_dir - DIR (SPEC.md 96.33.4)
; in:  SI = the arguments
; out: AL = the exit code
;
; **THE TABLE HAD NO DIR AND THAT WAS RIGHT UNTIL THERE WAS A PROMPT.** Every
; other verb here is one a PROGRAM shells out for (SPEC.md 96.30.1 measured
; which); nothing shells out to list a directory, because a program that wants
; a listing calls `AH=4Eh`. A person at a prompt wants nothing else first.
;
; ONE LINE A FILE, DOS's own columns: the name padded to 8, the extension
; padded to 3, then `<DIR>` or the size right-aligned. No date and no time -
; the listing record carries neither (`OSAPI_FIND_SZ`'s six fields) and a
; column of blanks is worse than no column.
;
; **FOLDERS ARE LISTED AND `..` IS NOT SKIPPED**, which is the opposite of
; dsh_nth's rule and deliberate: that walker feeds COPY, where a folder is not
; a file to copy; this one is the user's picture of where they are, and `..`
; is how they know they are not at the root.
; -----------------------------------------------------------------------------
dsh_c_dir:
    mov byte [dsh_dsw], 0           ; --- THE SWITCHES FIRST (SPEC.md 96.33.9)
    call dsh_dirsw                  ; /P and /W, wherever in the tail they sit,
    jc .badsw                       ; and REMOVED from it so the parse below
                                    ; sees a path. CF=1 = one we have not got,
                                    ; which is `Invalid parameter` and not a
                                    ; file name - measured on IBM DOS 3.30,
                                    ; where /B answers exactly that
    call dsh_skipsp
    mov di, dsh_a1
    call dsh_word
    cmp byte [dsh_a1], 0
    jne .havearg
    mov word [dsh_a1], '*' | ('.' << 8)     ; a bare DIR is `*.*`, here as
    mov word [dsh_a1+2], '*'                ; everywhere else
.havearg:
    mov si, dsh_a1
    mov al, 1                       ; ...and a whole spec that IS a folder is
    call dsh_resolve                ; listed rather than matched, which is what
    jc .nopath                      ; `DIR APPS` has to mean
    cmp byte [dsh_leaf], 0
    jne .haspat
    mov word [dsh_leaf], '*' | ('.' << 8)
    mov word [dsh_leaf+2], '*'
.haspat:
    mov si, dsh_leaf
    mov di, dsh_pat
    call dsh_to11

    mov word [dsh_n], 0             ; a FRESH listing: the counters, the
    mov word [dsh_ord], 0           ; ordinal to start at, /W's column
    mov word [dsh_ln], 0
    mov byte [dsh_wcol], 0
    call dsh_sbuild                 ; ...AND THE SORT (SPEC.md 96.33.9.2), once
                                    ; for the whole listing rather than per
                                    ; page, so a /P listing sorts as ONE listing
                                    ; and the claim spans the suspension. CF=1
                                    ; is not a failure: it is directory order,
                                    ; which is what real DOS lists
    jmp dsh_dir_page                ; ...and its answer is ours (tail call)
.badsw:
    mov si, dsh_s_badparm
    call dsh_say
    mov byte [dsh_why], DSHW_BADSW
    mov al, 1
    ret
.nopath:
    mov si, dsh_s_nopath
    call dsh_say
    mov byte [dsh_why], DSHW_NOPATH
    mov al, 3
    ret

; --- dsh_dirsw - take DIR's switches out of the tail at SI ------------------
; in:  SI = the tail cursor
; out: CF=0 with [dsh_dsw] set and every `/X` REMOVED from the string in
;      place; CF=1 = a switch this box has not got, nothing else promised
; clobbers: flags (SI is preserved, and it is still the cursor)
;
; SPEC.md 96.33.9. DOS takes them ANYWHERE in the tail - `DIR /P *.COM` and
; `DIR *.COM /P` are the same command - so this is a compacting scan rather
; than a leading-token test, and the caller's own parse then sees a tail with
; nothing in it but the path.
dsh_dirsw:
    push ax
    push si
    push di
    mov di, si                      ; the write cursor trails the read one
.scan:
    mov al, [si]
    or al, al
    jz .end
    cmp al, '/'
    jne .keep
    inc si
    mov al, [si]
    call dos_upc                    ; `/p` is `/P`
    cmp al, 'P'
    jne .notp
    or byte [dsh_dsw], DSW_P
    jmp short .took
.notp:
    cmp al, 'W'
    jne .bad                        ; **AND /B IS HERE**: it is DOS 5's, the
    or byte [dsh_dsw], DSW_W        ; real 3.30 refuses it, and the refusal is
.took:                              ; measured rather than assumed
    inc si
    jmp short .scan
.keep:
    mov [di], al
    inc di
    inc si
    jmp short .scan
.bad:
    pop di
    pop si
    pop ax
    stc
    ret
.end:
    mov byte [di], 0
    pop di
    pop si
    pop ax
    clc
    ret

; --- dsh_pagemax - AX = how many lines one page of this console holds -------
; clobbers: AX (flags)
;
; **THE LIVE VIEWPORT AND NOT 24** (SPEC.md 96.33.9): [con_vrows] is 25 in the
; full-screen bracket and on VGA or Hercules, and SEVENTEEN on CGA, where
; 96.33.8's short band shows 17 rows of 25. A fixed 24 pages past the end of a
; CGA band and makes the switch useless on the one adapter whose user needs it
; most. One row is left for the `Strike a key` prompt itself.
dsh_pagemax:
%ifndef KD_BACKEND                  ; **THERE IS NO CONSOLE UNDER kern_dos**
                                    ; (SPEC.md 96.43), so the viewport is the
                                    ; program's own 25-row screen and the
                                    ; refusal below is the answer rather than
                                    ; a fallback
    mov ax, [con_vrows]
%else
    xor ax, ax
%endif
    cmp ax, 4
    jae .have                       ; never painted (a `COMMAND /c` before the
    mov ax, 25                      ; first paint): DOS's own screen, so the
.have:                              ; listing is at worst not paged
    dec ax
    ret

; --- dsh_dir_page - ONE PAGE of the listing, and where a resume comes back --
; in:  [dsh_ord] = the ordinal to start at, [dsh_pat]/[dsh_rclus]/[dsh_rdrv]
;      the walk, [dsh_dsw] the switches
; out: AL = the exit code; [dsh_more] = 1 if it stopped with more to come
;
; SPEC.md 96.33.9. **IT RE-`dos_be_goto`s EVERY PAGE** rather than holding a
; cursor across the suspension, because the ORDINAL is the only thing the
; by-name API promises to mean the same twice (SPEC.md 19.7.1) - so a floppy
; changed between two pages costs a wrong listing and never a wrong sector.
dsh_dir_page:
    mov byte [dsh_more], 0
    call dsh_bank
    mov dx, [dsh_rclus]
    mov bl, [dsh_rdrv]
    call dos_be_goto
    jc .ioerr
    mov [dos_curdir], dx
    mov cx, [dsh_ord]               ; the ordinal, banked across the pause
.each:
    cmp word [dsh_sseg], 0
    je .walk                        ; no claim: the old chained walk, in
    push cx                         ; directory order (SPEC.md 96.33.9.2)
    call dsh_stage                  ; ...otherwise straight out of RAM, sorted,
    jc .done                        ; with no FIND and so no directory re-walk.
                                    ; **CX STAYS ON THE STACK ACROSS THE `jc`**:
                                    ; every exit below pops it exactly once, and
                                    ; on this arm it is a dead ordinal - the
                                    ; cursor is [dsh_si]. A `pop` before the
                                    ; branch would leave .done popping a frame
                                    ; that is not there
    jmp short .matched              ; dsh_sbuild matched it on the way in
.walk:
    push ds
    pop es
    push cx
    mov di, dsh_fnd
    call dos_be_find                ; out CX = the NEXT ordinal
    jc .done
    pop ax                          ; ...the one we asked for, spent
    push cx
    mov si, dsh_fnd
    mov di, dsh_nm11
    call dsh_to11
    mov si, dsh_nm11
    mov di, dsh_pat
    call dsh_match
    jc .next
.matched:
    test byte [dsh_dsw], DSW_W
    jz .long
    call dsh_dirwide                ; five to a row, so four names in five
    jmp short .counted              ; cost no line at all
.long:
    call dsh_dirline
    inc word [dsh_ln]
.counted:
    inc word [dsh_n]
    test byte [dsh_dsw], DSW_P
    jz .next                        ; no /P: nothing stops
    cmp byte [dsh_exec], 0          ; **AND ONLY THE PROMPT MAY PAUSE.** This
    je .next                        ; byte is set exactly while a line typed at
                                    ; the console is running (dos_con_run), so
                                    ; it is also the answer to "is there a key
                                    ; handler that will resume me". From
                                    ; `COMMAND /c DIR /P` inside a program's
                                    ; AH=4Bh there is not, and a listing that
                                    ; suspended there would never finish and
                                    ; never return to the program
    call dsh_pagemax                ; AX = lines this console's page holds
    cmp [dsh_ln], ax
    jb .next
    pop cx                          ; the NEXT ordinal is where we resume
    mov [dsh_ord], cx
    call dsh_home
    mov byte [dsh_more], 1          ; ...and the console reads this, because
    mov si, dsh_s_strike            ; dos_con_key holds the gfx lock and a
    call dsh_say                    ; built-in may not wait in it (96.33.9)
    xor al, al
    ret
.next:
    pop cx
    jmp short .each
.done:
    pop cx
    call dsh_sfree                  ; the sort claim is the LISTING's, and this
                                    ; is where a listing ends (SPEC.md
                                    ; 96.33.9.2). A /P suspension is not an end
                                    ; and deliberately does not reach here -
                                    ; dsh_dir_stop is the abandon path and frees
                                    ; it too
    call dsh_wflush                 ; /W's part-row, if one is open
    call dsh_home
    call dsh_dirtot
    xor al, al
    ret
.ioerr:
    call dsh_sfree
    call dsh_home
    mov si, dsh_s_ioerr
    call dsh_say
    mov byte [dsh_why], DSHW_IO
    mov al, 1
    ret

; --- dsh_dir_more - RESUME a suspended listing, on the line below the prompt -
; out: as dsh_dir_page
;
; **THE NEWLINE IS HERE AND NOT IN THE PROMPT** (SPEC.md 96.33.9.1).
; `Strike a key when ready . . . ` ends in a SPACE and no CRLF, which is
; COMMAND.COM's own string and is what leaves the cursor sitting after the dots
; where the user can see it.  DOS moves down when the key ARRIVES, so a prompt
; that carried its own newline would put the cursor on the next line and wait
; there.  Without this the resumed listing overprinted the prompt's own row.
dsh_dir_more:
    push si
    mov si, dsh_s_crlf
    call dsh_say
    pop si
    jmp dsh_dir_page

; --- dsh_dir_stop - abandon a suspended listing (Esc, Ctrl-C) ---------------
; clobbers: nothing (flags)
dsh_dir_stop:
    mov byte [dsh_more], 0
    jmp dsh_sfree                   ; ...and the sort claim goes with it

; =============================================================================
; DIR'S SORT (SPEC.md 96.33.9.2)
;
; A stated departure from DOS, on the owner's line: the ABI is 3.3's and the
; SCREEN is ours. A program can observe int 21h's answers, the version AH=30h
; reports and which switches are refused; it cannot observe the order names
; appear in for a person to read.
;
; It makes DIR CHEAPER rather than dearer. OSAPI_FILE_FIND is stateless by
; ordinal and re-walks the directory per call (SPEC.md 19.7.1), so a chained
; listing already visits it O(n^2) times. This walks ONCE, keeps each matching
; entry's whole find record in a claim, sorts an array of OFFSETS into those
; records, and emits from RAM.
; =============================================================================
DSH_SREC    equ OSAPI_FIND_SZ       ; 24 - the record, kept whole so that
                                    ; dsh_dirline and dsh_dirwide read exactly
                                    ; what they read before
DSH_SMAX    equ 256                 ; ...and the cap. A 1.44MB root holds 224
DSH_SOFF    equ DSH_SMAX * DSH_SREC ; where the offset array starts
DSH_SKB     equ 7                   ; (6,144 + 512 = 6,656 bytes)

; --- dsh_sfree - give the claim back.  clobbers: nothing (flags) ------------
dsh_sfree:
    pushf
    push ax
    push dx
    mov dx, [dsh_sseg]
    or dx, dx
    jz .out                         ; ...and a host with no heap never set it,
    call word [dos_hkv + DHK_FREE]  ; so the hook cannot be reached unarmed
    mov word [dsh_sseg], 0
.out:
    mov word [dsh_sn], 0
    pop dx
    pop ax
    popf
    ret

; --- dsh_scmp - does the record indexed at SI sort BEFORE the one at DI? ----
; in:  ES = the claim, SI and DI = BYTE offsets into the index array (i*2)
; out: CF=1 = yes.  clobbers: nothing but the flags
;
; **THE RECORD SORTS ITSELF**: +0 is a NUL-terminated 13-byte DISPLAY name
; (apps/os88api.inc's own layout), so nothing is stored beside it and the
; order is the one a person reads. DOS names are upper case (SPEC.md 19), so
; there is no folding to do.
;
; It takes INDEX slots rather than record offsets and does the indirection
; itself, which is not a convenience: the caller's loop already holds i*2 and
; j*2, and the obvious spelling - dereference into AX and compare `[es:ax]` -
; does not assemble at all. The 8086 addresses memory through BX, BP, SI and
; DI and through nothing else.
;
; Byte loads with an `es:` override on BOTH operands, for dsk_sortdir's
; reason: `cmpsb` wants DS:SI and DS is OURS, not the claim's.
dsh_scmp:
    push ax
    push cx
    push si
    push di
    add si, DSH_SOFF
    mov si, [es:si]                 ; ...the two records' byte offsets
    add di, DSH_SOFF
    mov di, [es:di]
    mov cx, 13                      ; the field's width, as a fence - the NUL
.c:                                 ; below is what actually ends it
    mov al, [es:si]
    cmp al, [es:di]
    jne .ne                         ; ...and the cmp's own CF is the answer
    or al, al
    jz .same                        ; both ended here, so they are equal
    inc si
    inc di
    dec cx
    jnz .c
.same:
    clc                             ; equal is not BEFORE
.ne:
    pop di                          ; `pop` writes no flag, so the compare's
    pop si                          ; CF rides out through all four
    pop cx
    pop ax
    ret

; --- dsh_sbuild - one walk, into a sorted claim -----------------------------
; in:  [dsh_rclus]/[dsh_rdrv] the folder, [dsh_pat] the pattern
; out: CF=0 with [dsh_sseg]/[dsh_sn] live; CF=1 = list it in directory order
; clobbers: AX, BX, CX, DX, SI, DI, ES, flags
dsh_sbuild:
    call dsh_sfree                  ; a listing abandoned mid-page leaves one
    cmp word [dos_hkv + DHK_CLAIM], 0
    je .no                          ; **A HOST WITH NO HEAP TAKES THE SAME EXIT
                                    ; A REFUSAL DOES** (SPEC.md 96.44.6), which
                                    ; is why this pair is a hook rather than a
                                    ; door: under `kern_dos` a DIR lists in
                                    ; directory order, which is real DOS's and
                                    ; is the behaviour already written below
    mov ax, DSH_SKB
    call word [dos_hkv + DHK_CLAIM] ; out DX = base segment
    jc .no                          ; **REFUSED IS NOT FATAL**: fall through to
    mov [dsh_sseg], dx              ; directory order, which is real DOS's

    ; **AND IT STANDS WHERE THE LISTING IS** (SPEC.md 96.30.8). This walked
    ; with dos_be_find and no dos_be_goto in front of it, so it enumerated
    ; wherever the INSTANCE happened to stand - and dsh_c_dir calls it after
    ; dsh_resolve has already gone home, so `DIR BIN` filled the claim from
    ; the launch folder and dsh_dir_page then served ITS rows rather than
    ; walking the folder it had correctly stood in. A bare DIR was right
    ; because the two folders are the same one, which is why nothing saw it.
    ; The bracket is dsh_dir_page's own, and the two agree now.
    call dsh_bank
    mov dx, [dsh_rclus]
    mov bl, [dsh_rdrv]
    call dos_be_goto
    jc .nogo
    mov [dos_curdir], dx
    xor cx, cx                      ; the ordinal, chained
    xor bx, bx                      ; ...and the record cursor, bytes
.walk:
    push ds
    pop es
    push cx
    mov di, dsh_fnd
    call dos_be_find                ; out CX = the NEXT ordinal
    jc .built
    pop ax
    push cx
    mov si, dsh_fnd                 ; matched HERE rather than at emit, so the
    mov di, dsh_nm11                ; claim holds only what will be printed and
    call dsh_to11                   ; the cap counts the same entries the
    mov si, dsh_nm11                ; listing does
    mov di, dsh_pat
    call dsh_match
    jc .next
    cmp bx, DSH_SOFF
    jae .full                       ; more than the cap: unsorted, and 96.33.9.2
                                    ; says why that degradation is the safe one
    mov es, [dsh_sseg]              ; THE RECORD, WHOLE AND ALONE - its +0 is
    mov di, bx                      ; the display name, so the sort needs
    mov si, dsh_fnd                 ; nothing stored beside it
    mov cx, DSH_SREC
    call dsh_scopy
    mov ax, [dsh_sn]                ; ...and its offset, into the index array
    add ax, ax
    add ax, DSH_SOFF
    mov di, ax
    mov [es:di], bx
    inc word [dsh_sn]
    add bx, DSH_SREC
.next:
    pop cx
    jmp short .walk
.full:
    pop cx
.nofit:
    call dsh_sfree
.nogo:                              ; the goto failed, or the claim will not
    call dsh_home                   ; hold the listing: unsorted is the answer
.no:                                ; and dsh_dir_page stands itself up again
    stc
    ret
.built:
    pop cx
    cmp word [dsh_sn], 2
    jb .sorted                      ; 0 or 1 records are already in order
    mov es, [dsh_sseg]
    call dsh_ssort
.sorted:
    call dsh_home                   ; ...and back where we were found, because
                                    ; dsh_dir_page does its own goto and a
                                    ; caller of either may be standing anywhere
.done:
    mov word [dsh_si], 0
    clc
    ret

; --- dsh_scopy - CX bytes from DS:SI to ES:DI.  clobbers: AX, CX, SI, DI ----
; A byte loop and not `rep movsb`, for dsh_scmp's reason inverted: movsb wants
; ES:DI, which IS the claim here, and DS:SI, which IS ours - so this one could
; use it. It does not, because the routine above holds BX across the copy and
; a shared helper that clobbers differently at its two call sites is how a
; register contract rots.
dsh_scopy:
    push si
    push di
    push cx
.b:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jnz .b
    pop cx
    pop di
    pop si
    ret

; --- dsh_ssort - selection sort of the OFFSET array, by the record's name ---
; in:  ES = the claim, [dsh_sn] records
; clobbers: AX, BX, CX, DX, SI, DI, flags (BP is banked)
;
; Selection sort for dsk_sortdir's reason (SPEC.md 19.4): at most n-1
; exchanges whatever the input, where a bubble sort's worst case is n^2/2 -
; and here an exchange is two WORDS of the index rather than a 24-byte record.
;
; BP holds i*2 and is an INTEGER here, never an address: SS is not DS in this
; system (SPEC.md 1), so a `[bp+x]` would read the stack segment.
dsh_ssort:
    push bp
    mov cx, [dsh_sn]
    add cx, cx                      ; CX = n*2, the index array's length
    xor bp, bp                      ; BP = i*2
.outer:
    mov ax, bp
    add ax, 2
    cmp ax, cx
    jae .out                        ; the last element has nothing to pick from
    mov di, bp                      ; DI = the smallest seen, as an index slot
    mov si, ax                      ; SI = j*2
.inner:
    cmp si, cx
    jae .place
    call dsh_scmp
    jnc .innext                     ; not smaller: keep looking
    mov di, si
.innext:
    add si, 2
    jmp short .inner
.place:
    cmp di, bp
    je .next                        ; already the smallest: nothing to exchange
    mov si, bp
    add si, DSH_SOFF
    mov bx, di
    add bx, DSH_SOFF
    mov ax, [es:si]
    mov dx, [es:bx]
    mov [es:si], dx
    mov [es:bx], ax
.next:
    add bp, 2
    jmp short .outer
.out:
    pop bp
    ret

; --- dsh_stage - record [dsh_si] of the sorted claim, into dsh_fnd/dsh_nm11 -
; out: CF=0 staged and [dsh_si] advanced; CF=1 = the listing is done
; clobbers: AX, CX, SI, DI, ES, flags
;
; It puts the two cells dsh_dirline and dsh_dirwide already read exactly where
; they already read them, which is what makes the sort invisible to the two
; renderers.
dsh_stage:
    mov ax, [dsh_si]
    cmp ax, [dsh_sn]
    jae .end
    mov es, [dsh_sseg]
    add ax, ax
    add ax, DSH_SOFF
    mov si, ax
    mov si, [es:si]                 ; the record's byte offset
    mov di, dsh_fnd
    mov cx, DSH_SREC
    call dsh_sload
    push ds                         ; ...and dsh_nm11 is DERIVED from it, by the
    pop es                          ; same routine the unsorted arm uses, so the
    mov si, dsh_fnd                 ; two paths cannot drift
    mov di, dsh_nm11
    call dsh_to11
    inc word [dsh_si]
    clc
    ret
.end:
    stc
    ret

; --- dsh_sload - CX bytes from ES:SI to DS:DI.  clobbers: AX, CX, SI, DI ----
dsh_sload:
    push si
    push di
    push cx
.b:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .b
    pop cx
    pop di
    pop si
    ret

; --- dsh_dirline - one entry of [dsh_fnd], as DOS lays it out ----------------
; clobbers: AX, BX, CX, DX, SI, DI, flags
dsh_dirline:
    mov si, dsh_nm11                ; the padded form dsh_to11 just built, which
    mov cx, 8                       ; is the columns already - the `?` a pattern
    call dsh_field                  ; expansion leaves is not in it, dsh_to11
    mov al, ' '                     ; having been given a real name
    call dos_tty
    mov si, dsh_nm11 + 8
    mov cx, 3
    call dsh_field
    mov al, ' '
    call dos_tty
    cmp word [dsh_fnd+14], OSAPI_FT_DIR
    jb .size
    mov si, dsh_s_dir               ; `<DIR>`, and `..` gets it too
    call dsh_say
    jmp short .eol
.size:
    mov ax, [dsh_fnd+18]            ; **THE LOW WORD, and the high one is not
    mov dx, [dsh_fnd+20]            ; dropped** - dsh_num32 takes both, because
    call dsh_num32                  ; a 65KB file on a 1.44MB floppy is ordinary
.eol:
    mov si, dsh_s_crlf
    call dsh_say
    ret

; --- dsh_field - CX bytes at SI, verbatim.  clobbers: AL, CX, SI (flags) ----
; It was dsh_dirline's own local until /W wanted the same two fields.
dsh_field:
    mov al, [si]
    call dos_tty
    inc si
    loop dsh_field
    ret

; --- dsh_dirwide - one entry in /W's five columns (SPEC.md 96.33.9) ---------
; clobbers: AX, CX, SI, flags
;
; The field is MEASURED: `COMMAND  COM    ANSI     SYS    ` is 8-pad, a space,
; 3-pad and four trailing - sixteen columns, five to a row, eighty exactly.
; The fifth ends the row instead of padding, so no line is 81 characters wide
; and nothing wraps.
dsh_dirwide:
    mov si, dsh_nm11
    mov cx, 8
    call dsh_field
    mov al, ' '
    call dos_tty
    mov si, dsh_nm11 + 8
    mov cx, 3
    call dsh_field
    inc byte [dsh_wcol]
    cmp byte [dsh_wcol], 5
    jb .pad
    mov byte [dsh_wcol], 0
    mov si, dsh_s_crlf
    call dsh_say
    inc word [dsh_ln]
    ret
.pad:
    mov cx, 4
.sp:
    mov al, ' '
    call dos_tty
    loop .sp
    ret

; --- dsh_wflush - end a part-finished /W row.  clobbers: AX, SI, flags ------
dsh_wflush:
    cmp byte [dsh_wcol], 0
    je .out
    mov byte [dsh_wcol], 0
    mov si, dsh_s_crlf
    call dsh_say
    inc word [dsh_ln]
.out:
    ret

; --- dsh_dirtot - `N file(s)`, DOS's own footer -----------------------------
dsh_dirtot:
    mov si, dsh_s_crlf
    call dsh_say
    mov al, ' '                     ; **NINE COLUMNS AND NOT EIGHT.**
    call dos_tty                    ; COMMAND.COM's own format is
    mov ax, [dsh_n]                 ; `%9d File(s) %9ld bytes free`, and
    xor dx, dx                      ; dsh_num32 pads to 8 for the SIZE column,
    call dsh_num32                  ; which is right there because a space
    mov si, dsh_s_files             ; precedes it. One more space is the
    call dsh_say                    ; difference and costs no second routine
    call dsh_dfree                  ; DX:AX = bytes free, CF=1 = unknown
    jc .crlf
    push ax
    push dx
    mov al, ' '
    call dos_tty
    pop dx
    pop ax
    call dsh_num32
    mov si, dsh_s_bfree
    call dsh_say
    ret
.crlf:
    mov si, dsh_s_crlf              ; no free count: the line still ends, and
    call dsh_say                    ; `N File(s)` alone is what DOS prints for
    ret                             ; a listing it could not size

; --- dsh_dfree - DX:AX = bytes free on the volume we are standing on --------
; out: CF=0 with DX:AX the byte count; CF=1 = the volume would not answer
; clobbers: AX, BX, CX, DX, DI, ES, flags
;
; OSAPI_FILE_DFREE answers in bytes already, so this is the door and nothing
; else. It used to ask OSAPI_VOL_STAT for its record and multiply the free
; cluster count up by hand - the same FAT walk, and then the same arithmetic
; the kernel had just done on the way to a field it did not read.
dsh_dfree:
    call dos_be_dfree               ; DX:AX = free bytes, BX = sectors per
    ret                             ; cluster; CF=1 with AX = FERR_*

; --- dsh_num32 - DX:AX as decimal, right-aligned in eight columns -----------
; clobbers: AX, BX, CX, DX, SI, DI, flags
;
; A 32-BIT DIVIDE and not two 16-bit ones: `div bx` on DX:AX quotients into AX
; with the remainder in DX and needs the high half to be smaller than the
; divisor, which after the first step it always is - so the loop is the
; ordinary one and the only thing 32 bits costs is zeroing DX once.
dsh_num32:
    mov di, dsh_num + 11
    mov byte [di], 0
    mov bx, 10
.d:
    push ax
    mov ax, dx
    xor dx, dx
    div bx                          ; the HIGH half first, remainder into DX
    mov cx, ax                      ; ...which is the top of the next divide
    pop ax
    div bx                          ; DX:AX / 10 completed
    dec di
    add dl, '0'
    mov [di], dl
    mov dx, cx
    or ax, ax
    jnz .d
    or dx, dx
    jnz .d
.pad:
    mov si, dsh_num + 11
    sub si, di                      ; SI = the digits written
    mov cx, 8
    sub cx, si
    jbe .say
.sp:
    mov al, ' '
    call dos_tty
    loop .sp
.say:
    mov si, di
    call dsh_say
    ret

; -----------------------------------------------------------------------------
; dsh_c_ver / dsh_c_echo / dsh_c_rem
dsh_c_ver:
    mov si, dsh_s_ver
    call dsh_say
    xor al, al
    ret

dsh_c_echo:
    call dsh_skipsp
    call dsh_say
    mov si, dsh_s_crlf
    call dsh_say
    xor al, al
    ret

dsh_c_rem:
    xor al, al
    ret

; dsh_count - "N file(s) copied", DOS's own report
; clobbers: AX, BX, CX, DX, SI, DI, flags
dsh_count:
    mov ax, [dsh_n]
    mov di, dsh_num + 5
    mov byte [di], 0
    mov bx, 10
.d:
    xor dx, dx
    div bx
    dec di
    add dl, '0'
    mov [di], dl
    or ax, ax
    jnz .d
    mov si, di
    call dsh_say
    mov si, dsh_s_copied
    call dsh_say
    ret

; =============================================================================
; THE TABLE
; =============================================================================
; Eleven bytes a row: a marker, the verb NUL-padded to eight, and the handler.
; A marker rather than a length because the compare is dsh_same's, which needs
; the terminator and not the count; zero ends the table.
%macro DSHCMD 2
    db 0xFF
  %%n:
    db %1, 0
    times 8 - ($ - %%n) db 0
    dw %2
%endmacro

dsh_tab:
    DSHCMD 'COPY',   dsh_c_copy
    DSHCMD 'XCOPY',  dsh_c_xcopy
    DSHCMD 'MOVE',   dsh_c_move
    DSHCMD 'DEL',    dsh_c_del
    DSHCMD 'ERASE',  dsh_c_del
    DSHCMD 'REN',    dsh_c_ren
    DSHCMD 'RENAME', dsh_c_ren
    DSHCMD 'MD',     dsh_c_md
    DSHCMD 'MKDIR',  dsh_c_md
    DSHCMD 'RD',     dsh_c_rd
    DSHCMD 'RMDIR',  dsh_c_rd
    DSHCMD 'CD',     dsh_c_cd
    DSHCMD 'CHDIR',  dsh_c_cd
    DSHCMD 'TYPE',   dsh_c_type
    DSHCMD 'DIR',    dsh_c_dir
    DSHCMD 'VER',    dsh_c_ver
    DSHCMD 'ECHO',   dsh_c_echo
    DSHCMD 'REM',    dsh_c_rem
    db 0

; [dsh_why] - WHICH refusal, for a debugger and not for the program
; The exit code stays DOS's (1, or 3 for a path), because a shell's exit code
; is what a batch file tests and inventing a richer one would be a contract
; nobody asked for. But `> NUL` silences the message, so under exactly the
; conditions a program shells out in there is NOTHING to read - which cost this
; session two emulator runs to recover by other means. One byte fixes it.
; THE DSHW_* REASON CODES MOVED TO apps/dos/dos.asm (SPEC.md 96.44.5):
; the window prompt tests DSHW_NOCMD and the core sets it, so a constant
; defined HERE is a seam across the core boundary. An equate emits nothing,
; so the container is where it costs neither host anything.

; --- DIR's switches, in [dsh_dsw] (SPEC.md 96.33.9) -------------------------
; The DOS 3.30 set is these TWO and nothing else, measured: /B answers
; `Invalid parameter` on the real thing exactly as /Z does, having arrived
; with DOS 5, and this box reports 3.31.
DSW_P       equ 1               ; pause a page at a time
DSW_W       equ 2               ; five columns of names, no size and no date

dsh_s_nul:      db 'NUL', 0
dsh_s_crlf:     db 13, 10, 0
dsh_s_bad:      db 'Bad command or file name: ', 0
dsh_s_syntax:   db 'Invalid number of parameters', 13, 10, 0
dsh_s_nopath:   db 'Path not found', 13, 10, 0
dsh_s_nofile:   db 'File not found', 13, 10, 0
dsh_s_ioerr:    db 'File creation error', 13, 10, 0
dsh_s_self:     db 'File cannot be copied onto itself', 13, 10, 0
dsh_s_copied:   db ' file(s) copied', 13, 10, 0
dsh_s_files:    db ' File(s)', 0     ; **MEASURED, not remembered**: the
                                ; format in IBM DOS 3.30's COMMAND.COM is
                                ; `%9d File(s) %9ld bytes free` (offsets
                                ; 20095 and 20111), capital F and no newline
                                ; until the byte count has been printed
dsh_s_bfree:    db ' bytes free', 13, 10, 0
dsh_s_strike:   db 'Strike a key when ready . . . ', 0
                                ; ...at offset 20332, spaced dots and a
                                ; trailing space, which is what DIR /P prints
                                ; (SPEC.md 96.33.9)
dsh_s_badparm:  db 'Invalid parameter', 13, 10, 0
                                ; ...20365, and it is what the real DOS 3.30
                                ; answers `DIR /B` as well as `DIR /Z`
dsh_s_dir:      db '<DIR>', 0
dsh_s_redir:    db 'Cannot redirect to that file', 13, 10, 0
dsh_s_nomem:    db 'Insufficient memory', 13, 10, 0
dsh_s_rderr:    db 'Error reading file', 13, 10, 0
                                ; **NOT A DOS 3.30 STRING AND IT CANNOT BE**
                                ; (SPEC.md 96.30.7): the real thing answers a
                                ; TYPE that cannot read through the CRITICAL
                                ; ERROR handler - `Abort, Retry, Fail?` - and
                                ; this box has none at the prompt. What it must
                                ; not do is keep saying `File creation error`
                                ; about a verb that creates nothing
dsh_s_toobig:   db 'File too large', 13, 10, 0
                                ; ...the COMPRESSED bound alone (96.30.7.1): a
                                ; 'CZ' file has to be expanded whole and the
                                ; walk that prints it indexes one segment

dsh_s_nodrv:    db 'Invalid drive specification', 13, 10, 0
dsh_s_ver:      db 13, 10, 'os8088 DOS Version 3.31', 13, 10, 0
                                ; **NOT "box" AND NOT "MS-DOS"** - this is not
                                ; a box to the person typing at it and it is
                                ; not Microsoft's. The VERSION stays 3.31
                                ; because that is what AH=30h answers
                                ; (SPEC.md 96.7), and a machine whose VER and
                                ; whose programs disagree is worse than either

; -----------------------------------------------------------------------------
; dsh_isshell - is the AH=4Bh program name our built-in COMMAND.COM?
; in:  [bp] = the caller's DS and DX its offset - the EXEC argument untouched
; out: CF=0 it is; CF=1 it is not, and .exec carries on as before
; clobbers: flags only
;
; THE RAW NAME AND NOT THE PARSED ONE, deliberately: COMSPEC is usually a full
; path (`C:\COMMAND.COM`) on a drive that may not exist here, so parsing it
; first would refuse with "invalid drive" before anything could recognise it.
; The last component is the whole question.
; -----------------------------------------------------------------------------
dsh_isshell:
    push ax
    push cx
    push si
    push di
    push es
    mov ax, [bp]
    mov es, ax
    mov si, dx
    xor cx, cx
.len:
    cmp byte [es:si], 0
    je .end
    inc si
    inc cx
    cmp cx, 80
    jb .len
    jmp short .no                   ; longer than any COMSPEC: not ours
.end:
    or cx, cx
    jz .no
.back:
    or cx, cx
    jz .cmp
    dec si
    dec cx
    mov al, [es:si]
    cmp al, '\'
    je .fwd
    cmp al, '/'
    je .fwd
    cmp al, ':'
    je .fwd
    jmp short .back
.cmp:
    dec si                          ; .back stopped AT the separator when it
    inc si                          ; found one and one short of the start when
.fwd:                               ; it did not; .fwd is the shared step past
    inc si                          ; it, and this pair makes the no-separator
    dec si                          ; case land on the same instruction
    mov di, dsh_s_cmd
.c:
    mov al, [es:si]
    or al, al
    jz .tail
    or al, 0x20                     ; case-insensitively: COMSPEC is normally
    cmp al, [di]                    ; upper case and the literal fallback a
    jne .no                         ; program carries is normally lower
    inc si
    inc di
    jmp short .c
.tail:
    cmp byte [di], 0
    jne .no
    pop es
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret
.no:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret

dsh_s_cmd:      db 'command.com', 0
