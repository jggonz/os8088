; =============================================================================
; os8088 - apps/thewire/wrtxt.inc
;
; EVERY STRING THE WIRE SHOWS, in one file (docs/WIRE-PLAN.md 7).
;
; The BRAND TABLE at the top is fixed by the user and is not to be reworded.
; Three dots and never an ellipsis character: the machine's font is ASCII
; 0x20..0x7E and nothing above it exists to draw.
;
; **A STRING IS WRITTEN ONCE AND POINTED AT.** The window title, the menu-bar
; name and the About card's first line are one `The Wire`, and the two button
; captions ARE the two menu items. That is not byte-shaving - the brand table
; says these strings are the same string, and two spellings of a brand is
; exactly the thing that drifts.
;
; It is included from the DATA end of thewire.asm, after the code, so nothing
; here sits between the header and the icon block (which are at fixed image
; offsets, SPEC.md 20.2).
; =============================================================================

; --- the brand, verbatim (docs/WIRE-PLAN.md's table) -------------------------
wr_ttl:      db 'The Wire', 0           ; the window title, the menu-bar name
                                        ; and the About card's first line
wr_s_desc:   db 'Online Software Library', 0
wr_s_tag:    db 'Software by wire.', 0
wr_s_conn:   db 'Connecting to Wire...', 0
wr_s_load:   db 'Loading from the Wire...', 0
wr_s_avail:  db 'Available on the Wire: ', 0    ; + 'N program(s)'
wr_s_progs:  db ' programs', 0
wr_s_prog1:  db ' program', 0
wr_s_newtag: db 'NEW', 0                ; `New on the Wire`, in the 3 cells a
                                        ; 13-cell row has to spare
wr_s_nodrv:  db 'Wire connection unavailable', 0

; --- the File menu, and the two buttons, which are the same two strings ------
wr_m_file:   db 'File', 0
wr_it_refr:  db 'Refresh', 0
wr_it_run:   db 'Load Program', 0
wr_it_run0:  db MENU_DIS, 'Load Program', 0
wr_it_add:   db 'Add to Disk...', 0
wr_it_add0:  db MENU_DIS, 'Add to Disk...', 0
wr_it_close: db 'Close', 0
wr_s_run     equ wr_it_run
wr_s_add     equ wr_it_add

; --- the filter row ----------------------------------------------------------
; wr_filx entries are all MULTIPLES OF 8 and so is the glyph's width plus its
; gap, which is what puts every label on font_run's single-store path (6.1).
; The widths are the glyph, its gap and 8 cells a character - the hit test and
; the painter read the same two tables, so they cannot disagree.
wr_s_show:   db 'Show:', 0
wr_f_all:    db 'All', 0
wr_f_t0:     db '8088/8086', 0
wr_f_t1:     db '286', 0
wr_f_t2:     db '386', 0
wr_f_t3:     db '486+', 0
wr_filtxt:   dw wr_f_all, wr_f_t0, wr_f_t1, wr_f_t2, wr_f_t3
wr_filx:     dw 48, 96, 192, 240, 288
wr_filwid:   db 40, 88, 40, 40, 48

; --- tier words, indexed by WC_TIER ------------------------------------------
wr_tiers:    dw wr_f_t0, wr_f_t1, wr_f_t2, wr_f_t3
wr_s_mach:   db 'This machine: ', 0     ; + a tier word: 23 cells at the worst

; --- the detail pane's empty and no-driver states ----------------------------
; **WR_PANE ASSERTS THE WIDTH AT ASSEMBLY TIME.** Everything the catalog
; supplies is wrapped to WC_DESCW - 1 by the writer and refused if it will not
; fit (SPEC.md 88.2), but the pane's OWN sentences are written here by hand -
; and the first spelling of the four below ran thirty characters into a pane
; that holds twenty-seven, so the no-driver advice was cut off at the frame in
; the one state where the whole point is that a person can read it. A `%strlen`
; costs nothing and cannot be forgotten.
%macro WR_PANE 2
%1: db %2, 0
    %strlen %%n %2
    %if %%n > WR_DESCC
        %error "a detail-pane line is at most WR_DESCC cells wide"
    %endif
%endmacro

    WR_PANE wr_s_nopic, 'No picture'
    WR_PANE wr_s_pick,  'Pick one from the list.'
    WR_PANE wr_nodrv1,  'The Wire needs ETHER.DRV'
    WR_PANE wr_nodrv2,  'and a network card.'
    WR_PANE wr_nodrv3,  'Control Panel > Drivers'
    WR_PANE wr_nodrv4,  'turns it on. Or browse the'
    WR_PANE wr_nodrv5,  'library at os8088.com/wire'
wr_nodrvl:   dw wr_nodrv1, wr_nodrv2, wr_nodrv3, wr_nodrv4, wr_nodrv5, 0

; --- the predicate's reasons (SPEC.md 88.7, SPEC.md 47) ----------------------
; Each one says what a person could DO about it, which is the whole of why a
; greyed control carries a reason at all.
wr_r_nosel:  db 'Select a program first', 0
wr_r_nocat:  db 'The catalog is not loaded', 0
wr_r_busy:   db 'A transfer is already running', 0
wr_r_disk:   db 'Needs its files on a disk - use Add to Disk', 0
wr_r_flop:   db 'Available as a floppy from os8088.com', 0
wr_r_space:  db 'Not enough room on that disk', 0

; --- what the network can say ------------------------------------------------
wr_s_lost:   db 'The Wire stopped answering', 0
wr_s_refus:  db 'The Wire refused the connection', 0
wr_s_answ:   db 'The Wire did not answer (', 0  ; + 'NNN)'
wr_s_badcat: db 'Catalog not understood', 0
wr_s_badpic: db 'That picture is not a picture', 0
wr_s_nomem:  db 'Not enough memory for that program', 0
wr_s_toobig: db 'That program is too big for one claim', 0

; --- progress and outcome ----------------------------------------------------
wr_s_of:     db ' of ', 0
wr_s_k:      db 'K', 0
wr_s_adding: db 'Adding ', 0
wr_s_file:   db ' file', 0
wr_s_files:  db ' files', 0
wr_s_added:  db ' added to your disk', 0
wr_s_loaded: db 'Loaded ', 0
wr_s_fromw:  db ' from the Wire', 0
wr_s_wrote:  db 'Could not write ', 0
wr_s_ldfail: db 'That program would not start (', 0

; --- the About card (SPEC.md 12.2, 20.5.1) -----------------------------------
; FIVE lines: the name, the description, the tagline, the catalog's own date
; and where it comes from.
wr_ablines:  dw wr_ttl, wr_s_desc, wr_s_tag, wr_abdate, wr_ab5, 0
wr_ab5:      db 'os8088.com', 0
; ...and the one string in this file that is WRITTEN. The eight question marks
; are overwritten in place by wr_catck with WC_DATE's eight digits, so the card
; names the catalog on the glass rather than the build under it. It is in the
; IMAGE and not in the bss because a bss copy starts empty, and an About card
; with a blank line in it before the first fetch says nothing at all.
wr_abdate:   db 'Catalog: ????????', 0
WR_ABDATEO   equ 9                              ; where those eight start

; --- the request, composed by wr_req (SPEC.md 88.4) --------------------------
wr_s_get:    db 'GET ', 0
wr_s_http:   db ' HTTP/1.0', 13, 10, 'Host: ', 0
wr_s_tail:   db 13, 10, 'User-Agent: os8088 Wire 1.0', 13, 10
             db 'Connection: close', 13, 10, 13, 10, 0
wr_s_clen:   db 'content-length:', 0            ; matched case-blind
wr_p_pkg:    db 'pkg/', 0
wr_p_pic:    db 'pic/', 0
wr_f_cat:    db 'catalog.bin', 0
wr_x_o88:    db '.O88', 0
wr_x_pic:    db '.PIC', 0

; --- WIRE.CFG (SPEC.md 19.9, 88.4) -------------------------------------------
wr_cfg_name: db 'WIRE.CFG', 0
wr_d_system: db 'SYSTEM', 0
wr_d_appdat: db 'APPDATA', 0
wr_def_host: db 'os8088.com', 0
wr_def_pfx:  db '/wire/', 0
