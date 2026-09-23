; =============================================================================
; apps/dos/dosc.inc - THE CONSOLE, AND THE PROMPT IN IT (SPEC.md 96.33)
; =============================================================================
; §96.32 cut the window to eighty columns and left the band below the top bar
; empty, with three lines of status text standing in it. This is what goes
; there: `apps/os88con.inc`'s 80x25 screen, black with white text, and a
; `COMMAND.COM`-shaped prompt typing into it.
;
; **THERE IS NO INTERPRETER HERE.** `dosh.inc` is the interpreter and has been
; since §96.30, whose own header says what this file is: *"a prompt in a window
; differs from this in its INPUT (a typed line rather than a command tail) and
; its OUTPUT (a text pane rather than the ROM teletype), and in nothing else"*.
; So what is below is an input, an output and a prompt - and every verb in
; `dsh_tab` became reachable from the keyboard without being touched.
;
; NO OSAPI_* FILE SLOT IS CALLED FROM THIS FILE either (§96.4), and the
; prompt's own path is the place that would have been the exception: it wants
; the instance's current directory, and `DBE_PATH` is the verb for it
; (§96.4.1).
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; dos_con_start - the console a fresh window opens with
; in:  nothing; the entry proc, before the first paint
; out: nothing; clobbers AX, CX, SI, DI
;
; con_open is NOT OPTIONAL (§96.33.1): the loader zeroes bss, so an unopened
; console has attribute 0 - black on black - and every character written into
; it is invisible. The banner is two lines because the second one is the part a
; user needs and the first is the part they recognise.
; -----------------------------------------------------------------------------
dos_con_start:
    ; --- WHERE THE PROMPT STARTS (SPEC.md 96.33.2) --------------------------
    ; **THE DRIVE WE WERE LAUNCHED FROM, AT ITS ROOT.** The drive is neither a
    ; constant nor a guess - OSAPI_FILE_HERE answers the instance's own volume
    ; with no disk I/O at all, and [dos_vol] is zero out of bss where zero is
    ; drive A: (SPEC.md 96.6.3's sentinel trap one file along), so an installed
    ; machine's box stood on C: and printed `A:\>`.
    ;
    ; **AND THE FOLDER IS DELIBERATELY NOT THE ONE IT ANSWERS.** That is
    ; `APPS\` - where this package's file happens to live - and a user opening
    ; a DOS box has come for a machine, not for our installation layout. A
    ; prompt that opens `A:\APPS>` is one they have to `CD \` out of before
    ; anything they type means what it looks like.
    ;
    ; Both are overwritten the moment a document arrives: dos_entry's
    ; OSAPI_ARG_FILE path sets them from the argument, and it runs after this.
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the drive
    mov [dos_vol], bl
    mov word [dos_curdir], 0        ; ...and dsh_home, below, walks us there
    call con_open
    mov si, dsh_s_ver               ; THE SHELL'S OWN `VER` LINE, which is what
    call con_say                    ; a DOS machine opens with - and sharing it
    mov si, dos_c_hint              ; means the version is stated in one place
    call con_say
    ; **AND THE PROMPT IS NOT WRITTEN HERE** (SPEC.md 96.33.2.1), which is the
    ; sentence three lines up taken seriously: a prompt is a statement about
    ; where the shell is STANDING, and on the document path this routine runs
    ; BEFORE the drive is known. `dos_entry` writes it at `.ok`, where both
    ; doors converge and `[dos_vol]` is settled either way.
    ret

; -----------------------------------------------------------------------------
; dos_prompt - `$P$G`, recomposed (SPEC.md 96.33.2)
; in:  nothing; out: nothing; every register preserved
;
; **RECOMPOSED AND NEVER HELD**, so `CD` and a drive change need nothing
; invalidated: the letter is [dos_vol] through §96.6's identity map and the
; path is OSAPI_FILE_PATH's, which is the same pair dos_envpath builds MYPATH
; out of. There is no PROMPT command and `$P$G` is not stored (§96.33.2).
;
; IT STANDS WHERE THE SHELL STANDS FIRST. A built-in brackets itself with
; dsh_bank/dsh_home, but a PROGRAM that ran has left the instance wherever its
; own file was (dos_run's quiet GOTO), so the prompt after one would name that
; folder and the next command would resolve against ours. One dsh_home puts
; both back in step.
; -----------------------------------------------------------------------------
dos_prompt:
    push ax
    push bx                         ; **BX AND DX, WHICH THE PAIR BELOW EATS.**
    push cx                         ; dsh_home's own comment says it clobbers
    push dx                         ; AX/BX/DX, and this proc is called from
    push si                         ; dos_con_start in the middle of an entry
    push di                         ; proc that goes on to hand BX to
                                    ; OSAPI_WM_ONWAKE - so the handler was
                                    ; registered against a wrong window and
                                    ; every package-posted wake did nothing:
                                    ; Run, Enter-re-run and the prompt's own
                                    ; launch all left [dos_state] at DST_READY
                                    ; for ever, with no error anywhere
    call dsh_bank                   ; ...which is where the shell's own cwd
    call dsh_home                   ; LIVES: [dos_curdir] and [dos_vol], and
                                    ; dsh_home is what makes the instance agree
    call dos_con_nl
    mov al, [dos_vol]
    add al, 'A'
    call con_write
    mov al, ':'
    call con_write
    mov di, dos_pbuf                ; the path, into the buffer dos_envpath
    mov cx, DOS_PBUF                ; uses for MYPATH - not at the same time as
    call OSAPI_FILE_PATH            ; this, which runs on the UI task between
    mov si, dos_pbuf                ; launches
    jnc .say
    mov si, dos_lnk_root            ; no answer: the root, which is where a box
.say:                               ; with nothing mounted would be standing
    call con_say
    mov al, '>'
    call con_write
    call dos_con_mark
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_con_nl - a newline, unless the cursor is already at column 0
; in:  nothing; out: nothing; every register preserved
;
; So that a prompt after a verb that ended in CR LF does not leave a blank line
; and one after a verb that printed nothing at all still gets its own.
; -----------------------------------------------------------------------------
dos_con_nl:
    push ax
    cmp word [con_cx], 0
    je .out
    mov al, 13
    call con_write
    mov al, 10
    call con_write
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_con_mark / dos_con_take - where the typed line STARTS
; in:  nothing; out: nothing (mark), CF=1 if the cursor is back at it (take)
;
; BACKSPACE MAY NOT EAT THE PROMPT, and the console has no notion of one: it is
; a screen, and `A:\>` is four cells of it like any other four. So the column
; the prompt ended on is banked, and BS stops there. It is a COLUMN and not a
; cell offset because a scroll moves the row and not the column - which is the
; only thing that can happen to the input line while it is being typed.
; -----------------------------------------------------------------------------
dos_con_mark:
    push ax
    mov ax, [con_cx]
    mov [dos_cmdx], ax
    pop ax
    ret

dos_con_take:                       ; ...AX is spent, which every caller of this
    mov ax, [con_cx]                ; one has already banked
    cmp ax, [dos_cmdx]
    ret                             ; CF=1 (below) = the caller has rubbed out
                                    ; everything it typed

; -----------------------------------------------------------------------------
; dos_con_key - a keystroke the console takes (SPEC.md 96.33.3)
; in:  AL = ascii, AH = scan, the gfx lock HELD (W_ONKEY's contract)
; out: CF=0 = taken; CF=1 = not ours, and the caller goes on looking
; clobbers: nothing but the flags
;
; **IT ECHOES AND IT DOES NOT REPAINT THE SCREEN.** con_write marks the row it
; changed and dos_con_draw spends that mark, so typing a character costs one
; row of eighty cells and not twenty-five of them - §96.19.1's measurement one
; control along, and the reason con_rows_owed exists at all (§70.8.1).
; -----------------------------------------------------------------------------
dos_con_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [dsh_more], 0          ; **A SUSPENDED `DIR /P` OWNS THE NEXT KEY**
    jne .more                       ; (SPEC.md 96.33.9), whatever it is - which
    cmp al, 13                      ; is DOS's own behaviour and is why this
    je .enter                       ; test is in front of the editor rather
    cmp al, 8                       ; than inside it
    je .bs
    cmp al, 27
    je .esc
    cmp al, ' '
    jb .no                          ; every other control, and an extended key's
    cmp al, 0x7F                    ; AL=0: not the console's, and a scan code
    je .no                          ; the caller may still want
    mov bx, [dos_cmdn]
    cmp bx, DSH_LINE - 1
    jae .full                       ; a line at its limit takes no more, and
                                    ; does not scroll one off the side either
    mov [dsh_line + bx], al
    inc bx
    mov [dos_cmdn], bx
    mov byte [dsh_line + bx], 0
    call con_write
    call dos_con_draw
.full:
    jmp short .done
.bs:
    call dos_con_take
    jbe .done                       ; at the prompt: nothing of ours to rub out
    cmp word [dos_cmdn], 0
    je .done
    dec word [dos_cmdn]
    mov bx, [dos_cmdn]
    mov byte [dsh_line + bx], 0
    mov al, 8                       ; BS, space, BS - the teletype's own erase,
    call con_write                  ; and the only one a screen with no cell
    mov al, ' '                     ; addressing has
    call con_write
    mov al, 8
    call con_write
    call dos_con_draw
    jmp short .done
.esc:
    mov word [dos_cmdn], 0          ; DOS draws a `\` and opens a new line;
    mov byte [dsh_line], 0          ; this clears the line in place, which is
    call dos_con_clrline            ; what a windowed box with a visible cursor
    jmp short .done                 ; should do. Esc in FULL SCREEN is the way
                                    ; out of it instead (§96.33.3), and that is
                                    ; tested before this proc is reached
.enter:
    call dos_con_run
    jmp short .done

; --- a paged listing is waiting: any key is its own, not the line's ----------
; Esc and Ctrl-C ABANDON it, which is what DOS's break does to a DIR; anything
; else is one more page.  dsh_dir_page prints the prompt again itself when it
; stops a second time, so nothing here knows how many pages there are.
.more:
    cmp al, 27
    je .stop
    cmp al, 3
    je .stop
    call dsh_dir_more               ; ...which leaves [dsh_more] set if there
    cmp byte [dsh_more], 0          ; is still more after this page
    jne .drew
    call dos_prompt                 ; the listing finished: a new prompt, as
    jmp short .drew                 ; dos_con_run's own tail does
.stop:
    call dsh_dir_stop
    mov si, dos_c_crlf
    call con_say
    call dos_prompt
.drew:
    call dos_con_draw
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dos_con_clrline - rub the typed line off the screen, cursor back to the mark
; in:  nothing; out: nothing; clobbers AX, CX, flags
; -----------------------------------------------------------------------------
dos_con_clrline:
    push ax
    push cx
.back:
    mov ax, [con_cx]
    cmp ax, [dos_cmdx]
    jbe .done
    mov al, 8
    call con_write
    mov al, ' '
    call con_write
    mov al, 8
    call con_write
    jmp short .back
.done:
    call dos_con_draw
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_con_run - ENTER: run the line, then a new prompt (SPEC.md 96.33.3)
; in:  the gfx lock held; out: nothing; clobbers everything but the segments
;
; **THE BUILT-IN FIRST, THE PROGRAM SECOND**, which is DOS's own order and the
; reason `dsh_exec` exists: dsh_run's `.unknown` arm prints *"Bad command or
; file name"* for a `/c` shell, because there is nothing else it could be; from
; a prompt there is, so the message is held back and [dsh_why] says which arm
; was taken.
;
; A PROGRAM GOES THROUGH dos_go, the same proc the Run button calls - so the
; path box ends up holding what was typed (§96.33.3's "update the text box"),
; the arguments field holds the tail, and the launch is the launch that has
; been tested since §96.19 rather than a second one.
; -----------------------------------------------------------------------------
dos_con_run:
    mov al, 13
    call con_write
    mov al, 10
    call con_write
    cmp word [dos_cmdn], 0          ; **THE COUNT AND NOT THE BUFFER'S FIRST
    mov word [dos_cmdn], 0          ; BYTE** - `mov` does not touch the flags,
    je .prompt                      ; so one compare answers both. dsh_line
                                    ; still holds the LAST command after it has
                                    ; run, so testing the text would make a bare
                                    ; Enter re-run it; the count is what says
                                    ; whether anything was typed. Spent either
                                    ; way, dsh_redir being about to write a NUL
                                    ; into the buffer
    mov byte [dsh_exec], 1          ; ...hold .unknown's message back
    mov si, dsh_line
    call dsh_run
    mov byte [dsh_exec], 0
    cmp byte [dsh_more], 0          ; **A SUSPENDED `DIR /P` OWES NO PROMPT**
    je .ranout                      ; (SPEC.md 96.33.9): the listing stopped
    call dos_con_draw               ; mid-page and printed `Strike a key`, and
    ret                             ; a prompt under that would be a machine
                                    ; asking two questions at once. The next
                                    ; keystroke resumes it in dos_con_key.
                                    ; **IT STILL DRAWS, THOUGH** - this arm
                                    ; returned straight out and the page the
                                    ; user is being asked to read never reached
                                    ; the glass, so the whole listing appeared
                                    ; at the end and the pause looked like a
                                    ; hang (SPEC.md 96.33.9.1)
.ranout:
    or al, al
    jz .prompt
    cmp byte [dsh_why], DSHW_NOCMD
    jne .prompt                     ; it ran and refused, and said so itself
    call dos_con_prog
    jc .nocmd
    ret                             ; it is launching: the prompt comes back
                                    ; with the exit line, from dos_con_ended
.nocmd:
    mov si, dsh_s_bad               ; ...and NOW the message dsh_run held back
    call con_say
    mov si, dsh_verb
    call con_say
    mov si, dos_c_crlf
    call con_say
.prompt:
    call dos_prompt
    call dos_con_draw
    ret

; -----------------------------------------------------------------------------
; dos_con_prog - try the typed line as a PROGRAM
; in:  [dsh_verb] = the verb, upper-cased; SI is spent
; out: CF=0 = it is launching; CF=1 = there is no such program
; clobbers: everything but the segments
;
; The line has already been through dsh_redir, so `[dsh_line]` is truncated at
; any `>` and the tail after the verb is the argument text - which is what the
; verb handlers are handed and what the arguments field wants.
; -----------------------------------------------------------------------------
dos_con_prog:
    ; --- `OPEN <document>` (SPEC.md 96.33.22) ------------------------------
    ; **INTERCEPTED HERE AND NOT A dsh_tab ROW**, which is a placement fact
    ; rather than a style choice: dsh_tab is in the DOS CORE (doscore.asm,
    ; 96.44), which has no path resolver, reaches the file system only through
    ; the twenty dos_k_* doors, and is capped by CORE_MAX - a row there stops
    ; the build with *the DOS core outgrew CORE_MAX*, and BOTH hosts reserve
    ; that space, so a byte there is a byte off each of them. The window side
    ; is where dos_path_take already lives, so the verb costs the core NOTHING.
    ;
    ; It runs BEFORE the program search, and `OPEN.COM` still wins anyway:
    ; 96.33.7's search is only reached once the verb table has declined, and
    ; this verb is not in that table.
    mov si, dsh_verb
    mov di, dos_c_open
    call dosc_ceq
    jc .notopen
    call dos_con_open
    clc
    ret
.notopen:
    mov si, dsh_verb                ; ...and HELP, the same door for the same
    mov di, dos_c_help              ; reason (SPEC.md 96.33.23)
    call dosc_ceq
    jc .nothelp
    call dos_con_help
    clc
    ret
.nothelp:
    mov byte [dos_ispkg], 0
    call dos_con_ext                ; **`PRINCE` IS `PRINCE.COM` OR `PRINCE.EXE`**
    jc .no                          ; (SPEC.md 96.33.7). It hands back dsh_a1 =
    cmp byte [dos_ispkg], 0
    jne dos_con_pkg                 ; ...and `.O88` is neither (SPEC.md 96.33.17)
    push si                         ; the name and SI = the tail, which is the
    mov si, dsh_a1                  ; split this proc wanted anyway - the NAME
    mov di, dos_path                ; as typed and not [dsh_verb], that one
    mov cx, DOS_PBUF - 1            ; being upper-cased for the table compare
.v:
    mov al, [si]
    or al, al
    je .vend
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .v
.vend:
    mov byte [di], 0
    pop si
    call dsh_skipsp                 ; ...and everything after it is the tail
    mov di, dos_args
    mov cx, DOS_ARGMAX
.a:
    mov al, [si]
    or al, al
    je .aend
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .a
.aend:
    mov byte [di], 0
    mov si, dos_pln                 ; both boxes re-measured from their text,
    call os88line_resync            ; because a field whose LN_LEN disagrees
    mov si, dos_ln                  ; with its buffer draws the old length
    call os88line_resync

    call dos_path_take              ; **A PARSE AND A WALK, NOT AN EXISTENCE
    jc .no                          ; CHECK** - it resolves the folder and takes
    call dos_path_make              ; ...and THEN the box is rewritten FULLY
                                    ; QUALIFIED (SPEC.md 96.33.10). It held the
                                    ; name as typed - `prince.EXE` off B: - and
                                    ; the box outlives the command: it is what
                                    ; Run re-launches and what Save Shortcut
                                    ; writes, so a relative name is only true
                                    ; while the box is still standing where it
                                    ; was typed. dos_path_make is the entry
                                    ; proc's own proc, which is what makes the
                                    ; two doors agree by construction rather
                                    ; than by both being written correctly -
                                    ; and THE RESYNC IS INSIDE IT NOW (96.33.10.1),
                                    ; because that sentence was true of the
                                    ; compose and false of the re-measure,
                                    ; which is exactly where the association
                                    ; door was drawing an empty box
    mov byte [dos_state], DST_READY ; the 8.3 name, and whether there is a file
    mov byte [dos_fromcon], 1       ; of that name is dos_run's answer. So the
                                    ; refusal a typo earns is DER_READ, and
                                    ; this byte is what lets dos_con_ended
                                    ; spell it the way DOS does
    mov bx, [dos_win]
    call OSAPI_WM_WAKE              ; ...and dos_wake runs it, exactly as the
                                    ; Run button's dos_go does
    ; --- AND THE REPAINT IS THE PATH BOX'S (SPEC.md 96.33.14) ---------------
    ; This was dos_swap, which is a full ground FILL and then a full PAINT -
    ; two whole-window passes, which is what a PAGE CHANGE costs and is right
    ; for one (96.20.1). A launch from the console is not a page change when
    ; the box is already on the main page: what changed is this box's text and
    ; one line of the band, and the band has its own painter.
    cmp byte [dos_fsxup], 0
    je .wind                        ; **THE CONSOLE HAS THE SCREEN** (SPEC.md
    mov byte [dos_fsxgo], 1         ; 96.33.16): the wake just posted cannot be
    clc                             ; dispatched until this task leaves the
    ret                             ; bracket, and the repaint below would put
                                    ; pixel rows into a framebuffer that is
                                    ; character cells now. So neither happens
                                    ; here - the byte brings the bracket down
                                    ; through dos_fsx_keys, and what is on the
                                    ; screen is already this line, echoed
.wind:
    mov bx, [dos_win]
    cmp byte [dos_page], DOS_PAGE_MAIN
    jne .turn                       ; a setup page really is being replaced
    call dos_bar_rects              ; ...otherwise just the control whose text
    mov si, dos_pln                 ; moved. os88line draws its own ground, so
    call os88line_draw              ; 13.14.6 is satisfied without a fill
    call dos_con_draw
    clc
    ret
.turn:
    call dos_swap
    clc
    ret
.no:
    mov byte [dos_path], 0          ; the box goes back to empty rather than
    mov byte [dos_args], 0          ; keeping a name that does not resolve
    mov si, dos_pln
    call os88line_resync
    mov si, dos_ln
    call os88line_resync
    stc
    ret

; -----------------------------------------------------------------------------
; dosc_ceq - DS:SI vs DS:DI, NUL strings, case-insensitive. CF=0 = equal
;
; Its own because dos_ceq is inside the CORE half (%ifndef DOS_EXTCORE) and
; this is the window's - the same two rooms 96.33.22 is about.
; -----------------------------------------------------------------------------
dosc_ceq:
    push ax
    push si
    push di
.c:
    mov al, [si]
    call dos_upc
    mov ah, al                  ; the TYPED character, upper-cased FIRST - the
    mov al, [di]                ; order 96.33.15.1 cost a release to get right
    call dos_upc
    cmp al, ah
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .c
.no:
    pop di
    pop si
    pop ax
    stc
    ret
.yes:
    pop di
    pop si
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dos_pgname - [dos_tname] -> the 13-byte launch cell at DI, UPPER CASE
; in:  DI -> a 13-byte cell.  out: nothing; clobbers AX, CX, SI, DI, flags
;
; **THE CASE IS OURS AND IT USED TO BE THE KERNEL'S** (SPEC.md 21.5.3.2).
; OSAPI_PKG_START's document arm compares BOTH names - the program's and the
; document's - against the association tables, which are uppercase-exact
; because they are built from FAT names, and a name that arrives from a
; KEYBOARD is not: `open readme.txt` found no association for `.txt` while
; `OPEN README.TXT` opened Note Pad. The kernel folded it there for one cycle,
; in 44 resident bytes of every machine ever booted - .cold is resident, so
; they came off the heap and off this box's own arena - on behalf of one
; caller, and the caller is this one.
;
; It comes free HERE because the walk already existed: this copy was written
; out THREE times in this file, once for the program name and twice for a
; document, and the fold is one `call dos_upc` inside the one that is left.
;
; ALWAYS TERMINATED INSIDE 13 (SPEC.md 4): dos_path_take_si refuses anything
; over twelve characters, so the forced NUL is belt and braces - and the
; bound is load-bearing at dos_pgdoc, whose bytes at +13 are the locator's
; directory cluster WORD and volume BYTE (SPEC.md 21.5.3). They are BINARY,
; the caller writes them immediately after this returns, and a fold that
; reached them would turn a cluster of 0x6C6C into 0x4C4C.
; -----------------------------------------------------------------------------
dos_pgname:
    mov si, dos_tname
    mov cx, 13
.c:
    mov al, [si]
    call dos_upc                    ; a NUL folds to itself, so the stop below
    mov [di], al                    ; needs no test of its own in front
    inc si
    inc di
    or al, al
    jz .done
    loop .c
    mov byte [di-1], 0
.done:
    ret

; -----------------------------------------------------------------------------
; dos_con_open - `OPEN <document>`: the double-click, typed (SPEC.md 96.33.22)
; in:  the line is in dsh_line; UI task under W_ONKEY, gfx lock HELD
; out: nothing; clobbers everything but the segments
;
; It is 96.33.21.1 with the program name left out, which is exactly what
; OSAPI_PKG_START's empty-name form is for (21.5.3) - so the two share the
; posted-launch machinery and differ in one byte of dos_pkgn.
; -----------------------------------------------------------------------------
dos_con_open:
    mov si, dsh_line
    call dsh_skipsp
    mov di, dsh_a1
    call dsh_cmdword                ; step over the verb itself; SI walks on
    call dsh_skipsp                 ; to what is being opened
    cmp byte [si], 0
    je .bad
    call dos_path_take_si
    jc .bad
    mov di, dos_pgdoc
    call dos_pgname
    mov ax, [dos_pgdir]
    mov [dos_pgdoc+13], ax
    mov al, [dos_pgvol]
    mov [dos_pgdoc+15], al
    mov byte [dos_pghas], 1
    mov byte [dos_pkgn], 0          ; **THE EMPTY NAME** (SPEC.md 21.5.3): the
                                    ; document's extension names the program
    mov ax, [dos_curdir]            ; ...and we stand where the PROMPT is: the
    mov [dos_pgpdir], ax            ; document carries its own dir and volume,
    mov al, [dos_vol]               ; and the kernel locates the program by
    mov [dos_pgpvol], al            ; the hint cache from wherever we are
    mov byte [dos_pkgq], 1          ; POSTED, for dos_con_pkg's reason: the
    mov bx, [dos_win]               ; slot wants the gfx lock FREE and W_ONKEY
    call OSAPI_WM_WAKE              ; holds it
    cmp byte [dos_fsxup], 0
    je .wind
    mov byte [dos_fsxgo], 1         ; the console has the screen (96.33.16)
    ret
.wind:
    call dos_con_draw
    ret
.bad:
    ; **THIS ARM IS THE NAME, NOT THE ASSOCIATION** (SPEC.md 96.33.22.1).
    ; dos_path_take_si refuses anything that is not a legal 8.3 path - over
    ; twelve characters in the name, a path ending in a separator, a buffer
    ; overrun - and `open nosuchfile.txt` is FOURTEEN, so it lands here and
    ; not at the association lookup at all. It answered `No program is
    ; associated with that file.` for one cycle, which is the same defect
    ; this section is about wearing its replacement's clothes: the thing that
    ; failed is the FILE NAME.
    mov si, dos_s_nodoc
    call con_say
    mov si, dos_c_crlf
    call con_say
    call dos_prompt
    call dos_con_draw
    ret

; -----------------------------------------------------------------------------
; dos_con_help - `HELP`: every verb, one line each (SPEC.md 96.33.23)
; in:  nothing; UI task under W_ONKEY, gfx lock HELD
; out: nothing; clobbers AX, SI
;
; WINDOW SIDE, and not a `dsh_tab` row, for `OPEN`'s reason twice over: that
; table is in the DOS CORE, which has THIRTY BYTES of CORE_MAX left and whose
; every byte is reserved by BOTH hosts - and nothing outside the console wants
; this.
;
; **AND IT DOES NOT PAGE**, which is the whole of why it is small. The text is
; cut to SIXTEEN lines because the shortest console this machine has is CGA's
; 17-row band - measured, `[con_vrows]` = 17 there and 25 on Hercules and VGA
; - and the window is not user-resizable. So sixteen lines and the prompt that
; follows them fit on every adapter, and `DIR /P`'s suspend-and-resume
; machinery is not needed at all: ~150 bytes of pager, a `Strike a key` string
; of its own and two more hooks in `dos_con_key` that this does not spend.
; -----------------------------------------------------------------------------
dos_con_help:
    mov si, dos_h_text
    call con_say
    call dos_prompt                 ; **IT OWES ONE** (96.33.17.1's rule, one
                                    ; verb along): dos_con_prog answers CF=0
                                    ; here and dos_con_run reads that as `it is
                                    ; launching, the prompt comes with the exit
                                    ; line` - and a built-in has no exit line
    call dos_con_draw
    ret

; -----------------------------------------------------------------------------
; dos_con_pkg - a `.O88` typed at the prompt: hand it to the OS (96.33.17)
; in:  [dsh_a1] = the name, as typed; reached from dos_con_prog
; out: CF=0 - it is queued, so the caller treats it as a launch
;
; **IT IS POSTED AND NOT CALLED**, for OSAPI_PKG_START's context rule (21.5):
; the slot wants the UI task with the gfx lock FREE, and this runs under
; W_ONKEY, which holds it. dos_wake is where the lock is free - the same place
; a DOS program's own launch is serviced - so the name is banked, a wake is
; posted, and dos_pkg_go does the work.
;
; THE NAME IS COPIED rather than left in [dsh_a1], which is the shell's
; scratch and the next command's to overwrite. Thirteen bytes against a
; question nobody should have to ask about the order two things happen in.
;
; [dos_path] and [dos_state] are LEFT ALONE. The path box holds what `Run`
; re-runs as a DOS program (96.32.1), and a package is not one - putting a
; `.O88` in there would arm a button that must refuse it.
; -----------------------------------------------------------------------------
dos_con_pkg:
    push si
    push di
    ; --- THE TYPED TEXT IS A PATH (SPEC.md 96.33.21) -----------------------
    ; It was copied straight into a 13-byte cell, so `B:\APPS\CALC.O88` was
    ; truncated to twelve characters of PATH, resolved against the prompt's
    ; own folder, and answered `Cannot open B:\APPS\CALC.O`. dos_path_take
    ; already gives a DOS program all three shapes and now takes the text as
    ; a parameter, so a package gets the same three for nothing.
    mov si, dsh_a1
    call dos_path_take_si
    jc .badpath
    mov ax, [dos_pgdir]             ; **BANK THE PROGRAM'S FOLDER NOW**: the
    mov [dos_pgpdir], ax            ; document's resolve below is the SAME
    mov al, [dos_pgvol]             ; routine writing the SAME two cells, and
    mov [dos_pgpvol], al            ; getting this backwards is silent - the
                                    ; launch stands in the DOCUMENT's folder
                                    ; and refuses there (SPEC.md 96.33.21.1)
    mov di, dos_pkgn
    call dos_pgname
    ; --- ...AND A DOCUMENT AFTER IT (SPEC.md 96.33.21.1) -------------------
    ; SI at entry was dos_con_ext's second output, the argument tail. The copy
    ; above spent it, so it comes back off the stack for one more pass of the
    ; same resolver - `NOTEPAD B:\MEDIA\README.TXT` resolving its document by
    ; the three shapes the program name already got.
    mov byte [dos_pghas], 0
    pop di
    pop si
    push si
    push di
    call dsh_skipsp
    cmp byte [si], 0
    je .nodoc
    call dos_path_take_si
    jc .nodoc                       ; a tail that is not a path is just a tail:
                                    ; the package launches with no document,
                                    ; which is what it did before this existed
    push si
    push di
    mov di, dos_pgdoc
    call dos_pgname
    mov ax, [dos_pgdir]             ; the locator OSAPI_ARG_FILE answers with,
    mov [dos_pgdoc+13], ax          ; in the order it answers it (SPEC.md
    mov al, [dos_pgvol]             ; 21.5.3)
    mov [dos_pgdoc+15], al
    mov byte [dos_pghas], 1
    pop di
    pop si
.nodoc:
    mov byte [dos_pkgq], 1
    mov bx, [dos_win]
    call OSAPI_WM_WAKE              ; ...and ui_task pops it with the lock free
    pop di
    pop si
    cmp byte [dos_fsxup], 0
    je .wind
    mov byte [dos_fsxgo], 1         ; **THE CONSOLE HAS THE SCREEN** (96.33.16),
    clc                             ; and a package's window cannot appear over
    ret                             ; a framebuffer that is character cells
.wind:
    call dos_con_draw               ; the echoed line, and nothing else: the
    clc                             ; window that is about to open is the
    ret                             ; feedback
.badpath:                           ; a path that does not resolve says so the
    pop di                          ; way a package that is not there does -
    pop si                          ; the text as TYPED, because a resolve that
    mov si, dos_s_nopkg             ; refused has no 8.3 name to show
    call con_say
    mov si, dsh_a1
    call con_say
    mov si, dos_c_crlf
    call con_say
    call dos_prompt
    call dos_con_draw
    clc
    ret

; -----------------------------------------------------------------------------
; dos_pkg_go - service the posted package launch (SPEC.md 96.33.17)
; in:  [dos_pkgn] = the name; UI task, gfx lock NOT held
; out: nothing
;
; Stand where the console is standing FIRST - OSAPI_PKG_START resolves in the
; calling instance's own folder (19.2.1) and dos_be_goto is GOTO_QM, which
; moves the machine AND marks the instance, so the two agree with nothing to
; keep in step.
; -----------------------------------------------------------------------------
dos_pkg_go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [dos_pkgq], 0
    ; --- DOES THE DOCUMENT EXIST? (SPEC.md 96.33.22.1) ---------------------
    ; Asked HERE and not in the console handler, because this is the
    ; lock-free context and that one runs under W_ONKEY: a directory walk is
    ; disk I/O and 74.1 is where it may happen. Asked FIRST because it is the
    ; cheap refusal - a missing file should not cost a package launch - and
    ; because the answer is the one the user most often needs: a typo in a
    ; DOCUMENT name otherwise came back talking about programs.
    cmp byte [dos_pghas], 0
    je .nocheck
    mov dx, [dos_pgdoc+13]
    mov bl, [dos_pgdoc+15]
    call dos_be_goto                ; stand where the document is said to be
    jc .nodoc
    mov si, dos_pgdoc
    mov di, dsh_pat
    call dsh_to11                   ; the same 11-byte form every other match
    mov word [dsh_skip], 0          ; in this box is decided on
    call dsh_nth
    jc .nodoc
.nocheck:
    mov dx, [dos_pgpdir]            ; where the typed PATH said, which IS the
    mov bl, [dos_pgpvol]            ; prompt's own folder when it carried no
    call dos_be_goto                ; path at all (SPEC.md 96.33.21)
    jc .bad
    mov si, dos_pkgn
    xor cx, cx                      ; **NO IMAGE: READ THE FILE** (SPEC.md
    xor dx, dx                      ; 21.5). Zero is the ordinary case and the
                                    ; image form is the Wire's alone
    push ds                         ; ...and with DX:CX zero, ES:DI is the
    pop es                          ; DOCUMENT (21.5.3) - or ES = 0, which is
    mov di, dos_pgdoc               ; that slot's one sentence for "nothing
    cmp byte [dos_pghas], 0         ; through ES:DI" on either arm
    jne .hasdoc
    xor ax, ax
    mov es, ax
.hasdoc:
    call OSAPI_PKG_START
    pushf                           ; **COME BACK** - a path may have stood the
    push ax                         ; machine on another volume, and every
    push ds                         ; later DOS call resolves where the BOX
    pop es                          ; thinks it is
    mov dx, [dos_curdir]
    mov bl, [dos_vol]
    call dos_be_goto
    pop ax
    popf
    jnc .ok                         ; its window is up - and the PROMPT still
                                    ; has to come back (SPEC.md 96.33.17.1)
    jmp short .bad                  ; **EXPLICIT, because `.nodoc` is BELOW**:
                                    ; this used to fall straight into `.bad`
                                    ; and the document check's arm was put
                                    ; between them, so every refusal the
                                    ; LAUNCH earned came back as `File not
                                    ; found` about a document that was there.
                                    ; Two bytes rather than moving the block,
                                    ; which would put the rollback below its
                                    ; own `jc`
.nodoc:
    ; **THE FILE, AND SAY SO** (SPEC.md 96.33.22.1). The machine may be
    ; standing in the document's folder now, so put it back before anything
    ; else resolves a name - .badend goes on to draw a prompt.
    mov dx, [dos_curdir]
    mov bl, [dos_vol]
    call dos_be_goto
    mov si, dos_s_nodoc
    call con_say
    mov si, dos_c_colon2
    call con_say
    mov si, dos_pgdoc
    call con_say
    jmp short .badend
.bad:
    ; **THE REFUSAL NAMES THE SHAPE OF THE PROBLEM** (SPEC.md 96.33.22), and
    ; the three shapes are different fixes: the FILE is not there (.nodoc
    ; above, and it is the commonest), nothing is ASSOCIATED with its type, or
    ; a named program could not be opened. An EMPTY name is the second - it is
    ; `OPEN`'s own arm (21.5.3), where there was never a program name to print
    ; and `Cannot open ` alone would name nothing at all.
    cmp byte [dos_pkgn], 0
    jne .badname
    mov si, dos_s_noopen
    call con_say
    jmp short .badend
.badname:
    mov si, dos_s_nopkg
    call con_say
    mov si, dos_pkgn
    call con_say
.badend:
    mov si, dos_c_crlf
    call con_say
    jmp short .prompt
.ok:
    ; --- A LAUNCH THAT WORKED OWES A PROMPT TOO (SPEC.md 96.33.17.1) -------
    ; This arm was `nothing to say`, and for a DOS program that is right: the
    ; prompt comes back with the exit line, from dos_con_ended. A PACKAGE has
    ; no exit line - it opens its own window and outlives the command - so
    ; nothing ever came back and the next thing typed landed at column 0 with
    ; no `A:\>` in front of it.
    ;
    ; **THE COLUMN IS THE TEST**, which is dos_con_ended's own rule one door
    ; along: the echoed command has already ended its line, so an
    ; unconditional CRLF would give it a blank one.
    cmp word [con_cx], 0
    je .prompt
    mov si, dos_c_crlf
    call con_say
.prompt:
    call dos_prompt
    ; --- ...AND ONLY IF WE ARE STILL ON TOP (SPEC.md 96.33.17.2) -----------
    ; **A SUCCESSFUL LAUNCH HAS JUST PUT A WINDOW OVER US.** This is not a
    ; paint callback, so no clip region is armed - the kernel arms one in
    ; front of W_PAINT and nowhere else - and a console repaint from here
    ; goes straight over whatever the package opened. Reported off the glass
    ; as Note Pad with a black band cut through it and console text down its
    ; left edge.
    ;
    ; OSAPI_WM_OBSCURED is the published gate for exactly this (SPEC.md
    ; 11.3.1) and it answers the hidden case too, so it is the whole test. A
    ; skipped draw loses NOTHING: con_say has already marked the rows, and
    ; the next real paint - which is what raising the box does - spends them.
    mov bx, [dos_win]
    call OSAPI_WM_OBSCURED
    jc .out
    ; **AND THE DRAW TAKES THE LOCK**, because dos_pkg_go is the WAKE
    ; handler's context and dos_con_draw wants it held (SPEC.md 74.1). The
    ; refusal arms have always drawn from here unlocked; sharing one epilogue
    ; fixes that at the same time as it saves the bytes.
    call OSAPI_GFX_LOCK
    call dos_con_draw
    call OSAPI_GFX_UNLOCK
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_con_ext - the typed line's PROGRAM NAME, extension and all (SPEC.md 96.33.7)
; in:  nothing; dsh_line holds the line
; out: CF=0 with dsh_a1 = the name to run and SI = the argument tail;
;      CF=1 = a bare name and no such program
; clobbers: everything but the segment registers
;
; **DOS TRIES `.COM` THEN `.EXE` THEN `.BAT`, AND THE ORDER IS THE CONTRACT** -
; a folder holding both `FOO.COM` and `FOO.EXE` runs the `.COM`. There is no
; `.BAT` here because there is no batch interpreter, and no PATH search because
; this box has no PATH: what is searched is the current directory, which is
; where somebody who typed a bare name is standing.
;
; A NAME THAT ALREADY CARRIES A DOT IS NOT SEARCHED ON - DOS does not probe
; `FOO.` or `FOO.DAT` either, and a typed extension is the user saying which
; file they mean - BUT IT IS STILL CHECKED (SPEC.md 96.33.15). COMMAND.COM's
; rule has two halves and this routine had one: no extension is a search, and
; an extension MUST be `.COM`, `.EXE` or `.BAT` or the answer is `Bad command
; or file name`. Left alone, `DOS.O88` typed in the folder the box was launched
; from loaded 30KB of package image as a `.COM` and the machine never came
; back. There is no `.BAT` here for the reason above - no batch interpreter -
; and refusing one by name beats entering it.
;
; **IT HANDS THE NAME BACK RATHER THAN REWRITING THE LINE.** Appending four
; characters in place would have DI overtake the SI still walking the tail -
; `PRINCE /F` is eleven characters of name over a tail that starts at six - so
; the caller reads dsh_a1 and the SI this leaves, which it wanted anyway.
;
; It WALKS the directory and opens nothing: dsh_nth is the same ordinal walk
; DEL and COPY use, so a name is decided by the matcher that decides every
; other one, and a probe that opened a file would leave a handle on the
; refusing path.
; -----------------------------------------------------------------------------
dos_con_ext:
    mov si, dsh_line
    call dsh_skipsp
    mov di, dsh_a1
    call dsh_cmdword                ; ...SI walks on to the argument tail, and
                                    ; stops ON a `/` so `PRINCE/F` is PRINCE
                                    ; with `/F` (SPEC.md 96.33.15.2)
    cmp byte [dsh_a1], 0
    je .no
    mov di, dsh_a1
.dot:
    mov al, [di]
    or al, al
    jz .nodot
    cmp al, '.'
    je .dotted                      ; it named one: not ours to CHANGE, and
    inc di                          ; still ours to check (SPEC.md 96.33.15)
    jmp short .dot
.dotted:
    push si                         ; **THE ARGUMENT TAIL** - SI is this
    mov si, dos_c_com               ; routine's second output and the literals
    call .isext                     ; want the register. DI is AT the dot and
    jnc .dok                        ; both strings carry theirs, so the compare
    mov si, dos_c_exe               ; starts on it
    call .isext
    jnc .dok
    ; --- ...AND A PACKAGE IS NOT A DOS PROGRAM (SPEC.md 96.33.17) ----------
    ; `.O88` used to land in the refusal below with the rest, which was right
    ; while there was nothing else to do with one - 96.33.15 exists because
    ; entering DOS.O88 as a `.COM` wedges the machine. There is something else
    ; to do with one now: OSAPI_PKG_START (21.5) launches it the way the file
    ; manager would. So this is a third ANSWER and not a fourth extension.
    mov si, dos_c_o88
    call .isext
    jnc .ispkg
.dok:
    pop si                          ; ...and `pop` writes no flag, so the CF
    jnc .out                        ; the compare answered rides through
    jmp short .no                   ; `FOO.DAT` is not a program, whether or
.ispkg:
    mov byte [dos_ispkg], 1         ; the caller branches on this, and it is a
    pop si                          ; BYTE rather than a second carry because
    clc                             ; CF already means `this is a program name`
    ret
.nodot:                             ; not it is a file
    push si                         ; the tail, across the three probes
    mov di, dos_c_com
    call .try
    jnc .found
    mov di, dos_c_exe
    call .try
    jnc .found
    ; --- ...AND `.O88` LAST, AFTER DOS'S OWN TWO (SPEC.md 96.33.7) ----------
    ; 96.33.17 gave the DOTTED door a third answer and left the bare one with
    ; two, so `CALC.O88` opened Calculator and `CALC` beside it in the same
    ; folder said `Bad command or file name` - which reads as the box not
    ; having the program rather than as the user owing an extension the `.COM`
    ; case does not ask for. A bare name IS a search; this is the third answer
    ; reaching the door that means it.
    ;
    ; LAST is the contract, for the reason `.COM` precedes `.EXE`: where both
    ; spellings exist the DOS program wins, and `FOO.O88` is the same escape
    ; hatch `FOO.EXE` already is.
    mov di, dos_c_o88
    call .try
    jc .nofile
    mov byte [dos_ispkg], 1         ; ...and the DOS arms jump PAST this store,
                                    ; so it is the `.O88` arm's alone - the
                                    ; caller branches on it to dos_con_pkg and
                                    ; the loader never sees a package image
.found:
    pop si
.out:
    clc
    ret
.nofile:
    ; --- ...AND A NAME WITH A DOCUMENT AFTER IT IS LOOKED UP (96.33.21.2) ---
    ; The search above walks the CURRENT directory and nothing else, so
    ; `NOTEPAD README.TXT` typed on `A:\` refused - and `A:\APPS\NOTEPAD.O88`
    ; is exactly where that program is. A document names its program the way
    ; an EXTENSION does, so a stem the folder does not hold is handed to the
    ; kernel to look up in the association tables instead of refused here.
    ;
    ; **ONLY WITH A DOCUMENT.** A bare word that resolves to nothing is a TYPO
    ; and `Bad command or file name` is its answer; it is the tail that turns
    ; the word into a claim about a program worth going to look for.
    pop si                          ; the tail, and the stack is BALANCED from
    push si                         ; here - `.no` is reached from the dotted
    call dsh_skipsp                 ; path too, where nothing was pushed, so
    cmp byte [si], 0                ; this arm may not leave a word behind it
    pop si                          ; (`pop` writes no flag, so the compare
    je .no                          ; above rides through it)
    mov byte [dos_ispkg], 1         ; the kernel's assoc_app_of decides, and
    clc                             ; refuses in its own words if it cannot
    ret
.no:
    stc
    ret

; --- .isext - DI = at the dot, SI = a '.XXX',0 literal; CF=0 = they match ---
; Case-insensitive on the typed side, and the TERMINATOR is compared too, so
; `.COMX` is not `.COM` (SPEC.md 96.33.15). Preserves SI and DI.
;
; **THE TYPED CHARACTER IS UPPER-CASED FIRST, AND THAT ORDER IS THE FIX**
; (96.33.15.1). It was the literal first, banked into AH across the call and
; then restored out of existence by the `pop ax` that brought AL back - so the
; compare read a byte nobody had set, the first character never matched, and
; every dotted name on the machine answered `Bad command or file name`.
; dos_upc answers in AL and touches nothing else, so the literal goes into the
; register the call has already finished with rather than across it.
.isext:
    push si
    push di
.x:
    mov al, [di]
    call dos_upc
    mov ah, al                  ; AH = the typed character, upper-cased
    mov al, [si]                ; ...and the literal's, AFTER the call
    cmp al, ah
    jne .xno
    or al, al
    jz .xyes                        ; both ended together: a match
    inc si
    inc di
    jmp short .x
.xno:
    pop di
    pop si
    stc
    ret
.xyes:
    pop di
    pop si
    clc
    ret

; --- .try - DI = an extension; CF=0 = dsh_a1 + it is a file in this folder ---
; On a hit dsh_a1 becomes the name that matched, so the caller never rebuilds it.
.try:
    push si
    push di
    mov si, dsh_a1                  ; NAME + EXT, in the other argument slot
    mov di, dsh_a2
.n:
    mov al, [si]
    or al, al
    jz .nend
    mov [di], al
    inc si
    inc di
    jmp short .n
.nend:
    pop si                          ; ...the extension
    push si
.e:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .e
    mov si, dsh_a2
    mov di, dsh_pat
    call dsh_to11
    mov word [dsh_skip], 0
    call dsh_nth                    ; the current directory, by ordinal
    jc .tno
    mov si, dsh_a2
    mov di, dsh_a1
    call dsh_cpz
    pop di
    pop si
    clc
    ret
.tno:
    pop di
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; dos_con_starting - `Starting A:\PRINCE.EXE` before the launch (SPEC.md
;                    96.33.20.1)
; in:  [dos_path], which dos_path_make filled on whichever door came in; the
;      gfx lock NOT held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; `dos_con_ended`'s opening, at the other end of the launch and for its reason:
; a console is a LOG, so what is about to happen is said once and stays said.
; The END line has named the fully qualified path since 96.33.20 and the START
; said nothing at all - so a log of three launches was three results with
; nothing recording what had been ASKED for, and a launch that never comes back
; at all (arm 1 hands the machine over) left no trace of itself whatever.
;
; THE COLUMN IS THE TEST and not `[dos_fromcon]`, exactly as it is there: a
; typed command has already echoed its own CRLF, so an unconditional one would
; give it a blank line, and the console's own cursor is the only thing that
; knows which of the two happened.
;
; It ends in a CRLF of its own, which is what puts the result on the line BELOW
; rather than glued to this one - and why `dos_con_ended`'s leading CRLF then
; correctly emits nothing.
; -----------------------------------------------------------------------------
dos_con_starting:
    cmp word [con_cx], 0
    je .fresh
    mov si, dos_c_crlf
    call con_say
.fresh:
    mov si, dos_c_start
    call con_say
    mov si, dos_path                ; ...with dos_con_ended's own fallback:
    cmp byte [si], 0                ; dos_path_make leaves the bare name when
    jne .said                       ; it cannot resolve one, and an EMPTY one
    mov si, dos_name                ; is a box that has never launched
.said:
    call con_say
    mov si, dos_c_crlf
    call con_say
    ret

; -----------------------------------------------------------------------------
; dos_con_ended - the console's line about a program that has stopped
; in:  [dos_state] is DST_RAN or DST_ERR; the gfx lock NOT held
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; It is where the three status lines §96.32's band carried went: a console is a
; LOG, so the thing that happened is said once and stays said, rather than
; being a sentence that is true until the next launch overwrites it.
; -----------------------------------------------------------------------------
dos_con_ended:
    ; --- A LINE OF ITS OWN (SPEC.md 96.33.20) ------------------------------
    ; The shell prints a prompt and leaves the cursor sitting after it, and a
    ; launch that did NOT come from the console - a Run click, a .LNK, a
    ; document - emits nothing before this. So the result was glued to the
    ; prompt it followed: `A:\>PRINCE.EXE: A handover is already under way.`
    ;
    ; **THE COLUMN IS THE TEST AND NOT `[dos_fromcon]`**: a typed command has
    ; already echoed its own CRLF, so an unconditional one would give it a
    ; blank line, and the console's own cursor is the only thing that knows
    ; which of the two happened.
    cmp word [con_cx], 0
    je .fresh
    mov si, dos_c_crlf
    call con_say
.fresh:
    cmp byte [dos_state], DST_ERR
    jne .name
    cmp byte [dos_fromcon], 0
    je .name
    cmp byte [dos_err], DER_READ
    jne .name                       ; **A TYPO IS "BAD COMMAND", NOT A DISK
    mov si, dsh_s_bad               ; FAULT** (SPEC.md 96.33.3). dos_path_take
    call con_say                    ; takes any legal 8.3 name, so a verb that
    mov si, dos_name                ; is neither a built-in nor a file gets as
    call con_say                    ; far as the load and fails there - and
    mov si, dos_c_crlf              ; "It could not be read." about a program
    call con_say                    ; the user never had is a sentence that
    jmp short .out                  ; sends them looking at the drive
.name:
    ; **THE FULLY QUALIFIED PATH** (SPEC.md 96.33.20), which is what the box's
    ; own path field holds and what Save Shortcut writes: a bare `PRINCE.EXE`
    ; in a log line is ambiguous the moment the console has stood anywhere
    ; else, and this line is most often about a program on the OTHER drive.
    ; `dos_path_make` fills it on both launch doors and leaves the bare name
    ; there when it cannot resolve one, so the fallback is for a box that has
    ; never launched at all.
    mov si, dos_path
    cmp byte [si], 0
    jne .said
    mov si, dos_name
.said:
    call con_say
    cmp byte [dos_state], DST_ERR
    je .err
    mov si, dos_c_exit
    call con_say
    call dos_fmt_exit               ; SI = the line with the code stamped in -
    mov si, dos_exitd               ; and only its digits are wanted here
    call con_say
    call dos_con_arena              ; ...AND WHAT IT WAS GIVEN (SPEC.md 96.33.19)
    mov si, dos_c_crlf
    call con_say
    jmp short .out
.err:
    mov si, dos_c_colon
    call con_say
    call dos_err_line               ; SI = the sentence for [dos_err]
    call con_say
    mov si, dos_c_crlf
    call con_say
.out:
    mov byte [dos_fromcon], 0       ; ...spent, so the NEXT launch - a Run
    call dos_prompt                 ; click, or a document - is spelled its own
    ret                             ; way

; -----------------------------------------------------------------------------
; dos_con_arena - ` (Arena: 343KB)` onto the line a finished program leaves
; in:  [dos_akb], the figure dos_run banked; out: nothing, AX/SI preserved
;
; **THE NUMBER THE PROGRAM WAS ACTUALLY GIVEN**, said once on the console's
; log line, because the alternative is reading it off a DOS program's own
; complaint - and those are in the program's units, about what is left AFTER
; it loaded itself, and only the ones that refuse say anything at all.
; `prince.EXE ended, exit code 001` with `Requires 318 KBytes… 146 KBytes is
; Available` above it takes two numbers and an assumption about the image's
; size to turn into the one figure that is this box's business (SPEC.md
; 96.25.1): what the arena was.
;
; It is the RAN path only. `[dos_akb]` is banked at `.unmount` and again at
; `.cap`, so on a launch that never reached the claim it holds either nothing
; or the pre-compaction figure - a number that reads like an answer and is a
; step in the arithmetic. The error lines say what went wrong instead.
;
; The digits come from `dos_mem_num`, which pads with LEADING BLANKS because
; the memory page stacks its two figures and `00419` reads as a different
; quantity. A log line is one line, so they are stepped over here rather than
; given a second formatter.
; -----------------------------------------------------------------------------
dos_con_arena:
    push ax
    push di
    push si
    mov si, dos_c_aren
    call con_say
    mov ax, [dos_akb]
    mov di, dos_arend
    call dos_mem_num
    mov si, dos_arend
.skip:
    cmp byte [si], ' '
    jne .say
    inc si
    jmp short .skip
.say:
    call con_say
    mov si, dos_c_akb
    call con_say
    pop si
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_con_draw - spend the marks, under a lock the CALLER holds
; in:  the gfx lock held, [dos_win] live
; out: nothing; every register preserved
;
; The geometry has to be asked for again, because nothing tells a package its
; window moved (§11.96.12) - so this is dos_con_geom and then the rows that
; owe, which on a keystroke is one.
; -----------------------------------------------------------------------------
dos_con_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [dos_fsxup], 0
    jne .out                        ; **A FOREIGN TEXT SCREEN IS UP** (SPEC.md
                                    ; 53.1): every kernel drawing slot renders
                                    ; desktop geometry into a framebuffer that
                                    ; is now character cells, so not one of
                                    ; them may be called. The keystroke still
                                    ; reaches con_write and still marks its
                                    ; row - [con_drb] is what carries the
                                    ; change across, and dos_fsx_owed spends
                                    ; it on the OTHER renderer
    mov bx, [dos_win]
    call dos_con_geom
    jc .out                         ; not visible: the marks keep, and the next
    call con_scrollpaint            ; paint spends them
    call con_rows_owed
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_con_say - a string into the console, and drawn, with the lock TAKEN
; in:  SI = a NUL string; the gfx lock NOT held
; out: nothing; every register preserved
;
; For the paths that write to the console from outside a callback - dos_run's
; own, which is the wake handler's context (§74.1).
; -----------------------------------------------------------------------------
dos_con_say:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call con_say
    call OSAPI_GFX_LOCK
    call dos_con_draw
    call OSAPI_GFX_UNLOCK
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dos_c_hint:  db 13, 10, 'Type a command, the name of a program, or help.'
             db 13, 10, 0
dos_c_start: db 'Starting ', 0   ; ...and the line that opens one (96.33.20.1)
dos_c_exit:  db ' ended, exit code ', 0
dos_c_colon: db ': ', 0
dos_c_colon2: db ' - ', 0        ; `File not found - README.TXT`
dos_c_aren:  db ' (Arena: ', 0
dos_c_akb:   db 'KB)', 0
dos_arend:   db '00000', 0    ; dos_mem_num's five, leading-BLANK padded
dos_c_com:   db '.COM', 0     ; ...and the ORDER is the contract
dos_c_exe:   db '.EXE', 0
dos_c_o88:   db '.O88', 0     ; ...and this one is not a DOS program at all
dos_s_nopkg: db 'Cannot open ', 0
dos_c_help:  db 'HELP', 0          ; SPEC.md 96.33.23. DOS 3.3 has no HELP
                                   ; either - it arrived with DOS 5 - so the
                                   ; verb is free on OPEN's own ground

; --- DHL - one help line, COUNTED (SPEC.md 96.33.23.1) ----------------------
; The count emits nothing and is what makes the sixteen-line limit a BUILD
; failure rather than a line that scrolls off a CGA in silence.
%assign DH_LINES 0
%macro DHL 1
    db %1, 13, 10
    %assign DH_LINES DH_LINES + 1
%endmacro
dos_h_text:
    DHL 'CD [path]     Change directory. CD .. goes up.'
    DHL 'COPY a b      Copy a file. Wildcards allowed.'
    DHL 'DEL name      Delete a file. ERASE is the same.'
    DHL 'DIR [/P][/W]  List this folder. /P pages it, /W is columns.'
    DHL 'ECHO text     Print the text.'
    DHL 'MD / RD name  Make or remove a directory. MKDIR/RMDIR the same.'
    DHL 'MOVE a b      Move or rename a file.'
    DHL 'OPEN file     Open a document with the program that claims it.'
    DHL 'REM text      A note. Does nothing.'
    DHL 'REN old new   Rename a file. RENAME is the same.'
    DHL 'TYPE file     Print a text file.'
    DHL 'VER           Say what this is.'
    DHL 'XCOPY a b     Copy a folder and all under it.'
    DHL 'A:            A drive letter on its own changes drive.'
    DHL 'NAME [arg]    Run a program, or open a document with it.'
    DHL "cmd > file    Send output to a file, or to NUL."
    db 0
%if DH_LINES > 16
  %error "HELP is over SIXTEEN lines: CGA's band is 17 rows ([con_vrows], measured), and the prompt that follows takes one - so a seventeenth line scrolls the first off a machine nobody tests on. Cut a line, or give HELP a pager and the part 96.33.23.2 costs."
%endif

dos_c_open:  db 'OPEN', 0          ; SPEC.md 96.33.22, and DOS 3.3 has no such
                                   ; verb, so taking it costs no compatibility
; **BOTH OF THESE REPLACED ONE STRING THAT WAS WRONG TWICE** (SPEC.md
; 96.33.22.1). It read `There is no program on this disk for that file.` and
; was the answer to BOTH failures: it named the PROGRAM when the thing missing
; was usually the FILE, and it said `on this disk` about a lookup that is not
; about a disk at all - an association names a program by STEM and
; assoc_locate then searches the volumes, so the program it points at may
; perfectly well be somewhere else.
dos_s_nodoc:  db 'File not found', 0
dos_s_noopen: db 'No program is associated with that file.', 0

; =============================================================================
; FULL SCREEN, AND ESC BACK OUT (SPEC.md 96.33.5)
; =============================================================================
; The same 80x25 buffer on the real text screen, which is the only place the
; whole of it fits: the band shows 25 rows on Hercules and VGA and 15 on CGA,
; and an 80-column program's output is 80 columns wide on all three.
;
; **THE RENDERER IS os88con.inc's AND THE BRACKET IS OURS** (SPEC.md 70.8.13),
; and the four things it holds are the four two carriers do not agree about:
;
;   the mode      FSXM_TEXT80, which every adapter here can set
;   the worker    THERE IS NONE. Telnet passes FSXF_KEEPWORKER because its
;                 worker owns the socket; this box's console is written by the
;                 UI task alone, so a kept worker would be a task with nothing
;                 to do and one more thing to be wrong about
;   the key out   ESC, and Telnet's is Ctrl+] - see below
;   the hint      ours names Esc, so the string is ours too
;
; **ESC IS OURS ONLY WHILE THE CONSOLE HAS THE SCREEN.** A DOS program reads
; Esc for its own purposes and a box that ate it could not run half the
; software it exists for - so this bracket is entered from the menu, ends
; before any program is launched, and the program's own bracket (96.2) is a
; different one with no key of ours in it at all. SPEC.md 11.2.1 asks for `F`
; in both directions and exempts an app taking typed text, which a command
; prompt is as completely as a terminal.
; -----------------------------------------------------------------------------

; --- dos_fsx - Program > Full Screen ----------------------------------------
; in:  SI = our window; UI task, gfx lock held (a callback's environment)
; out: nothing; preserves all registers
dos_fsx:
    push ax
    push bx
    push cx
    push si
    mov bx, si
    call OSAPI_FSX_CAPS             ; AX = the modes this adapter allows, DL =
    test ax, 1 << FSXM_TEXT80       ; the DISPLAY's own kind (SPEC.md 53.7.1),
    jz .out                         ; which osapi_video cannot answer for a
    mov [con_tkind], dl             ; window that is not on the primary
    mov byte [dos_fsxgo], 0         ; a FRESH bracket owes no launch (96.33.16):
                                    ; a leaked 1 would otherwise quit it on its
                                    ; first keystroke, so this makes a leak cost
                                    ; a full screen not coming back instead
    mov byte [dos_fsxup], 1         ; ...BEFORE the bracket, so nothing between
    mov ax, dos_fsx_con             ; here and the mode set can reach a drawing
    mov bx, si                      ; slot either
    xor cx, cx                      ; NO FSXF_KEEPWORKER: this box has no worker
    call OSAPI_FSX_RUN
    mov byte [dos_fsxup], 0         ; ...and again on the refusal path, where
.out:                               ; dos_fsx_con never ran
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- dos_fsx_con - the bracket's body ---------------------------------------
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours; near ret
dos_fsx_con:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; the FSI block is OURS (the gfx_blit4
    mov di, dos_fsi                 ; idiom: ES:DI is the caller's own)
    mov al, FSXM_TEXT80
    call OSAPI_FSX_MODE
    jc .done
    mov ax, [dos_fsi + FSI_SEG]     ; the ONE thing that is not a constant:
    mov [con_tseg], ax              ; B000 on Hercules and B800 on the rest
    mov byte [con_tcur], 0xFF       ; **NOT 0 AND NOT 1**: con_tx_cursor issues
                                    ; the SHAPE only on a change, and the mode
                                    ; set left the CRTC's own cursor blinking
                                    ; at 0,0 - so a byte that already agrees
                                    ; with [con_cvis] means it never places it
    call con_tx_ice                 ; ...and the blink bit, per adapter (70.8.9)
    mov byte [con_thint], 0xFF
    mov byte [dos_aedn], 1          ; SEEDED DOWN (SPEC.md 96.33.5.1): the
                                    ; Alt+Enter that got us in here is still
                                    ; held, and dos_fsx_ae's read is a LEVEL
                                    ; one - so without this the first poll
                                    ; below would read that same hold as the
                                    ; press that takes us out again. Entered
                                    ; from the MENU it costs nothing: the first
                                    ; poll finds the keys up and clears it
    call con_markall
    call dos_fsx_owed
    mov si, dos_c_leave             ; ONCE, on entry, and anything that draws
    call con_tx_hint                ; over it may (SPEC.md 70.8.7)
.loop:
    call dos_fsx_keys
    jc .done
    call dos_fsx_owed
    mov al, FSXW_FRAME              ; YIELDS while it waits, so the rest of the
    call OSAPI_FSX_WAIT             ; machine keeps its slices
    jmp short .loop
.done:
    mov byte [dos_fsxup], 0         ; ...INSIDE the bracket, so fsx_restore's
    call con_markall                ; repaint finds the box windowed and the
    pop es                          ; whole band owed, and dos_con_draw is
    pop di                          ; whole band owed
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- dos_fsx_ae - Alt+Enter, off the key-state map (SPEC.md 96.33.5.1) -------
; in:  nothing; out: CF=1 = a FRESH press, CF=0 = the same hold, or nothing
; clobbers: nothing but the flags
;
; OSAPI_KEY_DOWN is a LEVEL read (SPEC.md 9.7) - it answers "is this key down",
; and a hold looks identical on every pass - so the EDGE is this routine's to
; find and [dos_aedn] is what the last pass saw. Two far calls a frame, and
; only when the first of them says Alt is down at all.
dos_fsx_ae:
    push ax
    mov al, KSC_ALT
    call OSAPI_KEY_DOWN
    jnc .up                         ; ...and Alt alone is most of the machine's
    mov al, KSC_ENTER               ; life, so the second call is not made
    call OSAPI_KEY_DOWN
    jnc .up
    cmp byte [dos_aedn], 0          ; both down - a PRESS only if the last pass
    jne .same                       ; did not already have them, which is what
    mov byte [dos_aedn], 1          ; makes a held key one answer and not one a
    pop ax                          ; frame
    stc
    ret
.up:
    mov byte [dos_aedn], 0          ; released: the next both-down is a press
.same:
    pop ax
    clc
    ret

; --- dos_fsx_keys - the keyboard, polled. CF=1 = leave the bracket -----------
; int 16h directly, because no events are dispatched in here (SPEC.md 53.1) -
; and every other key is the console's, exactly as it is windowed. It cannot
; DRAW, though: dos_con_key ends in dos_con_draw, which is a kernel drawing
; slot and off-limits in a foreign text mode. [con_drb] is what carries the
; change across, and dos_fsx_owed below is what spends it.
dos_fsx_keys:
    push ax
    call dos_fsx_ae                 ; ALT+ENTER IS POLLED HERE AND NOT TYPED
    jc .quit                        ; (SPEC.md 96.33.5.1). int 16h below cannot
                                    ; carry it - no XT ROM enqueues the
                                    ; combination at all (9.7.1) - and the
                                    ; kernel's synthesised keystroke cannot
                                    ; reach us either, because ui_task is not
                                    ; dispatching while a bracket is up (53.1).
                                    ; It is free: this loop was already a poll
.next:
    mov ah, 1
    int 0x16
    jz .none
    xor ah, ah
    int 0x16
    cmp al, 27
    je .quit
    call dos_con_key                ; THE SAME HANDLER, and it draws nothing:
    cmp byte [dos_fsxgo], 0         ; [dos_fsxup] stands dos_con_draw down and
    jne .quit                       ; ...and a LAUNCH ends the bracket here
    jmp short .next                 ; (SPEC.md 96.33.16), exactly as Esc does
                                    ; the marks it leaves are what dos_fsx_owed
                                    ; spends. One prompt, one line editor, two
                                    ; screens
.none:
    pop ax
    clc
    ret
.quit:
    pop ax
    stc
    ret

; --- dos_fsx_back - back into the full screen after a program typed in it ----
; in:  the gfx lock NOT held (dos_run's own exit); out: nothing, all preserved
;
; SPEC.md 96.33.16. It goes through SPEC.md 96.2's protocol, because fsx_run
; requires the lock and dos_run released it two lines up: lock, run, unlock.
; It is NOT recursion - a launch typed into the RE-ENTERED bracket posts a
; wake, and a wake is dispatched by the event loop after this whole chain has
; unwound.
dos_fsx_back:
    cmp byte [dos_fsxgo], 0
    je .out
    push si
    mov byte [dos_fsxgo], 0         ; spent BEFORE the bracket, so a launch
    call OSAPI_GFX_LOCK             ; typed inside it arms a fresh one
    mov si, [dos_win]
    call dos_fsx
    call OSAPI_GFX_UNLOCK
    pop si
.out:
    ret

; --- dos_fsx_owed - the scroll, then the rows the bitmap names, then the pen -
dos_fsx_owed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    call con_tx_scroll              ; ...FIRST: the rows below are written on
    xor bx, bx                      ; top of the cells this moves
.row:
    call con_takerow
    jnc .next
    call con_tx_row
.next:
    inc bx
    cmp bx, CON_ROWS
    jb .row
    call con_tx_cursor
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE PROGRAM'S LAST SCREEN (SPEC.md 96.34)
; =============================================================================
; -----------------------------------------------------------------------------
; dos_snap - the text screen a DOS program left -> the console's own buffer
; in:  nothing; called from dos_prog_done with the program's screen STILL UP
; out: nothing; every register preserved
;
; NOT ONE BYTE OF STORAGE. con_scr is text VRAM's own layout - a character and
; an attribute, eighty columns to a row - so the program's screen and the
; console's buffer are the same structure at two addresses, and the capture is
; a `rep movsw` a row into the buffer the band already scrolls.
;
; AND NOTHING PER CHARACTER: there is no hook in dos_tty, no ring and no state.
; It reads the RESULT rather than the traffic, which is also why it catches
; int 10h and a direct write into B800 - most of what a DOS program does with a
; screen never passes through INT 21h at all (SPEC.md 96.34.1).
;
; DS IS UNKNOWN HERE. dos_prog_done is reached from the INT 21h terminate path,
; which runs on the program's stack with DS holding whatever the program had.
; SS:SP is already ours by then; DS is this proc's own to set.
; -----------------------------------------------------------------------------
dos_snap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    push cs
    pop ds
    mov ax, 0x40
    mov es, ax                      ; --- the BDA, as the PROGRAM left it -----
    mov al, [es:0x49]               ; the mode decides the segment AND the
    mov bx, 0xB800                  ; width, and refuses every graphics one
    mov cx, CON_COLS                ; (SPEC.md 96.34.2)
    cmp al, 7
    je .mono
    cmp al, 3
    ja .out                         ; 4..6 are CGA graphics and 8 and up are
    cmp al, 1                       ; EGA/VGA's - neither has text to read
    ja .have                        ; 2, 3: eighty columns of colour text
    mov cx, 40                      ; 0, 1: forty, in the left half of each row
    jmp short .have
.mono:
    mov bx, 0xB000
.have:
    mov si, [es:0x4E]               ; the ACTIVE page's own offset, so a
                                    ; program that wrote page 1 is read there
    mov di, [es:0x62]               ; ...and that page's cursor
    and di, 7
    shl di, 1
    add di, 0x50
    mov dx, [es:di]                 ; DL = the column, DH = the row
    mov ds, bx                      ; --- DS:SI is THEIRS from here ----------
    push cs
    pop es                          ; ...and ES:DI ours, which is the way
                                    ; movsw runs
    call dos_snap_any               ; AN EMPTY SCREEN IS NOT A CAPTURE (96.34.3)
    jc .out                         ; - the bracket's own mode set cleared it,
                                    ; so a program that printed nothing would
                                    ; otherwise blit twenty-five blank rows
                                    ; over the user's session
    mov di, con_scr
    mov bx, CON_ROWS
.row:
    push cx
    rep movsw                       ; the row itself...
    pop cx
    push cx
    mov ax, 0x0720                  ; ...and a forty-column mode's right half,
    neg cx                          ; blanked in the console's own default
    add cx, CON_COLS
    jcxz .rowd
    rep stosw
.rowd:
    pop cx
    dec bx
    jnz .row

    push cs                         ; --- and it is ours again ---------------
    pop ds
    mov al, dl                      ; THE CURSOR COMES ACROSS (96.34.3): the
    xor ah, ah                      ; prompt continues where the program
    cmp ax, CON_COLS                ; stopped, and the blank rows under it stay
    jb .cxok                        ; blank
    mov ax, CON_COLS - 1
.cxok:
    mov [con_cx], ax
    mov al, dh
    xor ah, ah
    cmp ax, CON_ROWS
    jb .cyok
    mov ax, CON_ROWS - 1
.cyok:
    mov [con_cy], ax
    mov byte [con_pwrap], 0         ; a captured screen promises no wrap
    mov word [con_scrl], 0          ; con_clear's reason (70.9.3): a wholesale
    call con_markall                ; replacement makes the scroll debt a LIE,
                                    ; and con_scrollpaint would spend it by
                                    ; blitting content that is not there
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- dos_snap_any - is there anything ON that screen? ------------------------
; in:  DS:SI = the page's first cell, CX = its columns
; out: CF=0 = at least one cell is neither a space nor a NUL; CF=1 = it is
;      blank and must not be copied. SI and every register preserved.
;
; A mode set fills with 20h and a zeroed buffer reads 00h, so those two are
; what "nothing" looks like; everything else is content, control codes
; included, because 00h-1Fh are CP437 faces and arrows on a screen (70.9).
dos_snap_any:
    push ax
    push cx
    push dx                         ; **DX IS THE CURSOR THIS PROC'S CALLER IS
    push si                         ; HOLDING**, and `mul` writes it: measured
    mov ax, cx                      ; as a captured screen whose cursor came
    mov cx, CON_ROWS                ; back 0,0, so the box's own `ended, exit
    mul cx                          ; code` line landed on the program's FIRST
    mov cx, ax                      ; row. A register an instruction owns
.c:
    lodsw
    cmp al, ' '
    je .next
    or al, al
    jnz .yes
.next:
    loop .c
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret
.yes:
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret

dos_c_leave: db ' Esc to leave', 0

; **THE PROMPT'S OWN CRLF** (SPEC.md 96.44.5). It was `dsh_s_crlf`, which
; lives in `apps/dos/dosh.inc` - the CORE - and is read from here six times.
; Three bytes duplicated is cheaper than any mechanism that would let a host
; reach a core address, and there is nothing to drift: it is a carriage
; return and a line feed.
dos_c_crlf: db 13, 10, 0
