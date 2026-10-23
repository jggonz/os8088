; =============================================================================
; os8088 - apps/wirechat/wirechat.asm
;
; WIRECHAT (SPEC.md 113): chat rooms on the wire. Four public rooms, private
; messages, a member list, friends, a status, and a flag that takes an
; abusive message down - against one server, os8088.com/chat/v1/, which is a
; Durable Object in the website's repository (os8088-web/src/chat/hub.js).
; SPEC.md 113.2 is the wire format both ends implement.
;
; THE SHAPE IS THE WIRE'S (SPEC.md 92.5), WITH A CONDUCTOR. A worker runs one
; HTTP request at a time and knows nothing about chat; the UI task's wake
; handler parses each reply into the stores, decides what the NEXT request
; is (wc_next), and paints what changed. One socket, always: a message sent
; while a poll is being held CLOSES the poll, and the answer to the message
; carries everything the poll would have (SPEC.md 113.3).
;
; THE FILES (SPEC.md 113.1):
;   wirechat.asm  this: header, geometry, the entry proc, template, menus,
;                 About, and the whole bss map
;   wcnet.inc     the worker and the HTTP/1.0 client
;   wcproto.inc   the request composer and the reply parser
;   wcview.inc    the layout and every painter
;   wcband.inc    the band, its slots, the themes and the buttons' faces
;   wcact.inc     keys, clicks, menus, popups, the timer - and the conductor
;   wccfg.inc     WIRECHAT.CFG
;   wctxt.inc     every string
; =============================================================================

%include "os88api.inc"
%include "netpkg.inc"               ; the socket verbs: THE DRIVER'S OWN
                                    ; header, as The Wire includes it - EQUs
                                    ; only, so it may sit above the header

    OS88_HEADER 'WireChat', wc_entry, OS88_F_ICON, OS88_STACK_256 | OS88_HIGH | OS88_HIGHMEM
                                ; **OS88_HIGH | OS88_HIGHMEM** (KERN-MEGA-PLAN
                                ; 13.3, 13.6): no arithmetic on our own CS or
                                ; DS, no claim at all, no vector, no re-home -
                                ; every socket buffer is in our own segment
                                ; and the driver reaches it through ES, as it
                                ; does Telnet's and The Wire's
                                ; **OS88_STACK_256**, argued as The Wire
                                ; argues its 192 (thewire.asm): ETHER.DRV's
                                ; verbs are ~126 bytes on OUR stack and the
                                ; interrupt floor is 64. The Wire's worker
                                ; also draws a status cell; this one draws
                                ; nothing at all (SPEC.md 113.4), but its
                                ; deepest chain is wc_nstep -> wc_take ->
                                ; wc_hdrline -> wc_dec32, four frames of
                                ; pushes where The Wire's is two, so it takes
                                ; the class above rather than arguing it fits
    OS88_ICON16
    ; Two speech balloons, the near one over the far: the chat glyph every
    ; reader already knows, and at 16x16 the two tails survive CGA's tall
    ; pixels where a handset or a keyboard would not (SPEC.md 26.4).
    ; --- mask ---
    dw 0x7FC0, 0xFFE0, 0xFFE0, 0xFFE0
    dw 0xFFFE, 0xFFFF, 0xFFFF, 0x7FFF
    dw 0x3FFF, 0x1FFF, 0x1FFF, 0x1FFF
    dw 0x0FFE, 0x0FFC, 0x0038, 0x0010
    ; --- data ---
    dw 0x7FC0, 0x8020, 0x8020, 0x8020
    dw 0x9FFE, 0x5001, 0x3001, 0x1001
    dw 0x1001, 0x1001, 0x1001, 0x1001
    dw 0x0FFE, 0x0038, 0x0028, 0x0010
    OS88_ICON16_END

; =============================================================================
; GEOMETRY (SPEC.md 113.5) - content-relative throughout. The content origin
; is 8-aligned (WF_SNAP, SPEC.md 11.94), so a column that is a multiple of 8
; here is a multiple of 8 on the glass, where a cell of the face is one byte
; of each of its rows (SPEC.md 113.5.3).
; =============================================================================
WC_LH       equ 10              ; a row: one cell of WireChat's face, 8 x 10
                                ; (SPEC.md 113.5.3) - row 0 air, the capitals
                                ; on 1..7, the descenders on 8..9
WC_FROWS    equ WC_LH           ; the face's rows: a cell IS a band
WC_FSTR     equ WC_SBUFN        ; wc_fband's stride: the longest composition
WC_FFIRST   equ 32              ; the face's characters, as the kernel's
WC_FLAST    equ 126
WC_STH      equ 12              ; the status bar: its rule and one band
WC_INH      equ 24              ; the input row: its rule, the box (16 rows,
                                ; WC_BOXY down), and air
WC_BOXY     equ 4               ; the box's top, below the input row's rule
WC_BOXH     equ 16
WC_RPW      equ 160             ; the right pane, its rule on the byte grid
WC_RCELLS   equ 19              ; ...a right band's cells, from rule + 8
WC_FLDH     equ 14              ; the field: inside the box, a cell row of air
WC_TIME     equ 6               ; a message's nick column: "hh:mm "...
WC_NICKW    equ 10              ; ...10 cells wide...
WC_TCOL     equ WC_TIME + WC_NICKW  ; ...and its text column
WC_SENDC    equ 9               ; Send: cells
WC_SRCHC    equ 11              ; the search box: the lens, a rule, 'Search...'
WC_GROWC    equ 2               ; cells the grow box takes off the status bar
WC_NOTIFYC  equ 18 + WC_GROWC   ; ...and where 'Notifications: Off' starts
WC_WRIGHT   equ 560             ; content narrower than this loses the right
WC_HTALL    equ 200             ; content this tall has the header's 2nd band

WC_FRW      equ 608             ; the frames OS88_PREFER asks for
WC_HVGA     equ 400
WC_FRWH     equ 688
WC_HHERC    equ 296
WC_HCGA     equ 150
WC_MINW     equ 320
WC_MINH     equ 147

; --- stores (SPEC.md 113.10) -------------------------------------------------
WC_NICKN    equ 13              ; a nick, 12 and the NUL
WC_KEYN     equ 37              ; an owned nick's code: a UUID, 36 and the NUL
WC_TEXTN    equ 201             ; a message, 200 and the NUL
WC_NMSG     equ 40              ; messages held for the view: the server's
                                ; history is 30 (SPEC.md 113.2.3), and 10 more
                                ; is what arrives while they are read
WC_MREC     equ 224             ; one: +0 dd seq, +4 dw UTC minute, +6 db
WC_MSEQ     equ 0               ; flags, +7 db lines (0 = not yet wrapped),
WC_MMIN     equ 4               ; +8 the nick, +21 the text; 222, rounded
WC_MFLG     equ 6
WC_MLINES   equ 7
WC_MNICK    equ 8
WC_MTEXT    equ 21
WCM_DM      equ 1               ; WC_MFLG: a private message...
WCM_MINE    equ 2               ; ...one this session sent
WCM_MENT    equ 4               ; ...one that names us (wc_mention)
WCM_DAY     equ 8               ; ...the first of its local day: a divider
                                ; above it (wc_dayflags, SPEC.md 113.5)
WC_MDAY     equ 222             ; +222 dw its UTC day, 1 = 2000-01-01, 0 =
                                ; none (SPEC.md 113.2.4)
%if WC_MDAY + 2 > WC_MREC
  %error "WC_MDAY runs past the record"
%endif
WC_NROS     equ 50              ; the roster: the server's session ceiling
WC_RREC     equ 16              ; +0 the nick, +13 room, +14 status
WC_RROOM    equ 13
WC_RST      equ 14
WC_NDM      equ 8               ; private conversations listed
WC_DREC     equ 16              ; +0 the nick, +13 unread
WC_DUNR     equ 13
WC_NFR      equ 8               ; friends (SPEC.md 113.8)
WC_NROOM    equ 4
WC_ROOMN    equ 16              ; a room's name...
WC_TOPICN   equ 48              ; ...and its topic
WC_RESPMAX  equ 6144            ; a reply's body ceiling (SPEC.md 113.3)
WC_REQMAX   equ 640             ; a request: ~130 of line and headers, the
                                ; host and prefix, the query, a 200-byte body
WC_CHUNK    equ 1024            ; one NETV_RECV, in our own segment
WC_HLINE    equ 32              ; one reply header line, clipped
WC_HOSTMAX  equ 48
WC_PFXMAX   equ 32
WC_NOTEN    equ 64              ; the notice line
WC_CFGMAX   equ 512
WC_SBUFN    equ 96              ; a composed row

; --- a band's META (wcband.inc): how a composed row is drawn -----------------
WC_NSPAN    equ 16              ; colour spans a band may have: the tabs
                                ; and the status bar are the many-coloured
WCX_LEN     equ 0               ; db the composition's cells
WCX_STY     equ 1               ; db WS_*
WCX_NSP     equ 2               ; db spans, 1..WC_NSPAN (0 = a slot owed)
WCX_SP      equ 3               ; (cell, ink, ground) x WC_NSPAN; 0xFF = the
                                ; style's
WCX_G0      equ WCX_SP + 3 * WC_NSPAN   ; (cell, glyph), cell 0xFF = none
WCX_G1      equ WCX_G0 + 2
WCX_BC      equ WCX_G1 + 2      ; db the bold's first cell...
WCX_BL      equ WCX_BC + 1      ; db ...and its length, 0 = none
WCX_UL      equ WCX_BL + 1      ; db the cell's last row's colour, 0xFF = the
                                ; ground: a section title's rule
WC_METAN    equ WCX_UL + 1
; --- the SLOTS (SPEC.md 113.5.1): a band's meta and cells as on the glass ----
WC_MAXB     equ 44              ; bands a pane may have slots for: a VGA's
                                ; tallest window has 40 (the rest draw whole)
WC_NCAP     equ 20              ; a narrow slot's cells: a right band's
WC_SLOTW    equ WC_METAN + WC_SBUFN     ; a wide slot
WC_SLOTNN   equ WC_METAN + WC_NCAP      ; a narrow one
WCSL_HDR    equ 0               ; the wide: the header's two bands...
WCSL_HDR2   equ 1
WCSL_TABS   equ 2               ; ...the tabs, the title, the notice line,
WCSL_TITLE  equ 3               ; the status bar
WCSL_NOTE   equ 4
WCSL_STAT   equ 5
WCSL_PANE   equ 6               ; ...and the message pane's rows
WC_NWIDE    equ WCSL_PANE + WC_MAXB
WCSL_RIGHT  equ WC_NWIDE        ; the narrow: the right pane's bands
WC_NSLOT    equ WCSL_RIGHT + WC_MAXB
%if WC_RCELLS > WC_NCAP
  %error "a right band does not fit a narrow slot"
%endif

; --- the worker's states (wcnet.inc) - WS_OPEN..WS_BODY is "in flight" -------
WS_IDLE     equ 0
WS_OPEN     equ 1
WS_WAIT     equ 2
WS_SEND     equ 3
WS_HEAD     equ 4
WS_BODY     equ 5
WS_DONE     equ 6
WS_FAIL     equ 7

WW_NONE     equ 0               ; the one byte between the halves
WW_DONE     equ 1
WW_FAIL     equ 2

; --- the link, as the conductor sees it (SPEC.md 113.4) -----------------------
LK_NONICK   equ 0               ; waiting for the user to pick a nick
LK_NODRV    equ 1               ; no network card: nothing will be sent
LK_JOIN     equ 2               ; a nick and no session: join next
LK_LIVE     equ 3               ; a session
LK_REST     equ 4               ; a 503: waiting [wc_wait]
LK_OFF      equ 5               ; a failure: backing off [wc_wait]
LK_KEY      equ 6               ; the nick is owned: waiting for its code

RQ_NONE     equ 0               ; the request in flight
RQ_JOIN     equ 1
RQ_POLL     equ 2
RQ_SAY      equ 3
RQ_FLAG     equ 4

MD_MSG      equ 0               ; the field's modes (SPEC.md 113.6.1)
MD_NICK     equ 1
MD_FIND     equ 2
MD_FRIEND   equ 3
MD_KEY      equ 4

ST_ONLINE   equ 0               ; a status, as the wire spells it
ST_AWAY     equ 1
ST_BUSY     equ 2
ST_GONE     equ 3               ; ...and a private partner off the roster

WV_DM       equ 0xFF            ; [wc_view]: a room 0..3, or this

; --- the buttons ---------------------------------------------------------------
WB_STATUS   equ 0               ; the header's right end: glyph, word, nick
WB_SEARCH   equ 1               ; the title's search box
WB_SEND     equ 2
WB_N        equ 3

; --- the glyphs (wctxt.inc's wc_glyphs, in this order) --------------------------
GL_ONLINE   equ 0                       ; the status glyphs, in ST_* order
GL_AWAY     equ 1
GL_BUSY     equ 2
GL_GONE     equ 3
GL_LKOK     equ 4                       ; the link lamp
GL_LKWARN   equ 5
GL_LKBAD    equ 6
GL_LKOFF    equ 7
GL_ACT      equ 8                       ; the activity lamp
GL_BLANK    equ 9
GL_PEOPLE   equ 10                      ; ONLINE's and FRIENDS'
GL_MAIL     equ 11                      ; DIRECT's
GL_LENS     equ 12                      ; the search box's
GL_SBUP     equ 13                      ; the scroll column
GL_SBDN     equ 14
GL_SBTHM    equ 15
GL_SBTRK    equ 16
GL_LDLIT    equ 17                      ; Wired's LEDs on one bit, or on
GL_LDHALF   equ 18                      ; a ground their pictures were not
GL_LDX      equ 19                      ; drawn for (wcband.inc's wc_bglyph)
GL_LDOFF    equ 20
GL_N        equ 21

; --- the popups ----------------------------------------------------------------
PP_NONE     equ 0
PP_STATUS   equ 1
PP_EMO      equ 2
PP_EMON     equ 8               ; emoticons offered

; --- dirty regions (wc_flush) ------------------------------------------------
WCD_HDR     equ 0x0001          ; both header bands (the clock is band 2)
WCD_TABS    equ 0x0002
WCD_TITLE   equ 0x0004
WCD_PANE    equ 0x0008          ; the whole pane
WCD_TAIL    equ 0x0010          ; ...or only what was appended (the scroll)
WCD_NOTE    equ 0x0020
WCD_FIELD   equ 0x0040          ; the input row: the prompt and the field
WCD_RIGHT   equ 0x0080
WCD_STAT    equ 0x0100
WCD_ALL     equ 0x01FF
                                ; (what the left pane carried is WCD_TABS |
                                ; WCD_RIGHT now: a room's count is a tab's,
                                ; the friends and conversations the right's)

; --- the themes (SPEC.md 113.5.2), and the ROLES a theme gives a colour ------
TH_WIRED    equ 0               ; black on white, its lamps LEDs: the default
TH_DARK     equ 1               ; white on black (Mono-Dark)
TH_BLUE     equ 2
TH_GREEN    equ 3
TH_AMBER    equ 4
TH_N        equ 5
TH_NMONO    equ 2               ; 0..TH_NMONO-1 one bit can draw
TH_WIREDC   equ TH_N            ; wc_themes' last row: Wired on a colour display
TH_DEFAULT  equ TH_WIRED
R_BG        equ 0               ; the ground
R_FG        equ 1               ; text
R_DIM       equ 2               ; times, the topic, the placeholder, the track
R_ACC       equ 3               ; the rules, titles, the prompt, the notice
R_HI        equ 4               ; the highlight's ground...
R_HIFG      equ 5               ; ...and its text
R_OK        equ 6               ; Online, On, the live link
R_WARN      equ 7               ; Away, connecting, activity
R_BAD       equ 8               ; Busy, Off, a failure, a badge's ground
R_BADFG     equ 9               ; ...a badge's digits
R_MENTG     equ 10              ; a message that names us: ground...
R_MENTF     equ 11              ; ...and text
R_NICK      equ 12              ; four nick colours, 12..15
WC_NPEN     equ 16

WC_AWAYS    equ 600             ; seconds without a key or a press: auto-away
WC_HOLD     equ 70              ; seconds a request may take before it is
                                ; given up: the server holds a poll 50

; =============================================================================
; THE BSS MAP (SPEC.md 113.10). Declared HERE, ahead of the code, so its total
; is known where OS88_BSS asks for it - Desk Clock's DKV shape.
; =============================================================================
; Desk Clock counts with %assign, which wants a number the PREPROCESSOR can
; read - and half of these sizes are equ products (WC_NMSG * WC_MREC). So the
; running offset is TEXT instead, re-defined after each name, and every
; offset is an ordinary assembler expression over constants already defined.
%xdefine WCB 0
%macro WCV 2                    ; name, bytes
%1 equ os88_image_end + WCB
%xdefine WCB (WCB + (%2))
%endmacro
    ; --- the window and the drawing --------------------------------------
    WCV wc_win, 2               ; our window
    WCV wc_ox, 2                ; the content origin, this lock hold
    WCV wc_oy, 2
    WCV wc_cw, 2                ; the content box
    WCV wc_ch, 2
    WCV wc_rx1, 2               ; the right pane's rule, 0 = no right pane
    WCV wc_mx1, 2               ; the center's first and last columns
    WCV wc_mx2, 2
    WCV wc_hr, 2                ; the header's rule
    WCV wc_hy2, 2               ; the header's second band, 0 = none
    WCV wc_tby, 2               ; the tabs' band
    WCV wc_mid, 2               ; the first row under the tabs' rule
    WCV wc_ty, 2                ; the title row
    WCV wc_py1, 2               ; the pane's first band
    WCV wc_ph, 2                ; ...its height, to the notice line
    WCV wc_srows, 2             ; ...its scroll column's cells
    WCV wc_rows, 2              ; ...the bands its lines take, from wc_top
    WCV wc_gap, 2               ; ...the air under a message (wc_face)
    WCV wc_pby, 2 * (WC_MAXB + 1)   ; ...each band's top, then the end
    WCV wc_gby, 2 * (WC_MAXB + 1)   ; ...as on the glass (wc_gshift)
    WCV wc_grows, 2             ; ...and its bands there
    WCV wc_gused, 2             ; ...the rows below them that are not ground
    WCV wc_sbg, WC_MAXB         ; ...and the scroll column's glyphs there
    WCV wc_tx, 2                ; ...its text column (8-aligned)
    WCV wc_cols, 2              ; ...and how many cells wide
    WCV wc_ny, 2                ; the notice line
    WCV wc_iy, 2                ; the input row
    WCV wc_sy, 2                ; the status bar's rule
    WCV wc_dirty, 2             ; WCD_*
    WCV wc_abon, 1              ; the About card is up
    WCV wc_pop, 1               ; PP_*
    WCV wc_hired, 1             ; the worker is spawned
    WCV wc_front, 1             ; we were frontmost at the last reply
    WCV wc_poprc, 8             ; the popup's rect, absolute
    WCV wc_brects, 8 * WB_N     ; the buttons
    WCV wc_bflags, 2 * WB_N
    WCV wc_sbon, 1              ; the pane's scroll column: 1 = drawn...
    WCV wc_sbt, 2               ; ...the thumb's first band...
    WCV wc_sbn, 2               ; ...and how many
    WCV wc_tabc, WC_NROOM + 1   ; the tabs: each one's first cell, and the end
    WCV wc_fkc, 4               ; the F-key legend: each item's first cell...
    WCV wc_fkn, 1               ; ...how many are shown
    WCV wc_hrs, 2               ; the header's status: its first cell
    WCV wc_hdown, 1             ; ...pressed (wc_stbtn)
    WCV wc_clkm, 1              ; the minute the header's clock shows
    WCV wc_ln, 20               ; the field: OS88LINE_SZ, asserted at the end
    WCV wc_lbuf, WC_TEXTN       ; ...its buffer
    WCV wc_mode, 1              ; MD_*
    WCV wc_sbuf, WC_SBUFN       ; a composed row
    WCV wc_note, WC_NOTEN       ; the notice line
    ; --- the view and its messages --------------------------------------
    WCV wc_view, 1              ; 0..3 a room, WV_DM a conversation
    WCV wc_vdm, WC_NICKN        ; ...with whom
    WCV wc_hist, 1              ; the next request asks h=1
    WCV wc_mn, 1                ; messages held
    WCV wc_sel, 1               ; the selected one, 0xFF none
    WCV wc_divi, 1              ; the divider sits above this one, 0xFF none
    WCV wc_divn, 1              ; ...and counts this many
    WCV wc_top, 2               ; the pane's first line
    WCV wc_total, 2             ; ...of this many
    WCV wc_add, 2               ; lines appended since the last paint
    WCV wc_find, 1              ; the last find's hit, 0xFF none
    WCV wc_tobot, 1             ; the next pane paint opens at the end
    WCV wc_msgs, WC_NMSG * WC_MREC
    ; --- the session ----------------------------------------------------------
    WCV wc_link, 1              ; LK_*
    WCV wc_tok, 13              ; the token, NUL = none
    WCV wc_nick, WC_NICKN       ; our nick, as the server spells it
    WCV wc_key, WC_KEYN         ; its code, when it is owned; NUL = none
    WCV wc_seq, 4               ; the server's sequence, dword
    WCV wc_rver, 4              ; the roster's version, dword
    WCV wc_st, 1                ; our status, ST_*
    WCV wc_auto, 1              ; 1 = Away because we went idle
    WCV wc_stsent, 1            ; the status the server has
    WCV wc_idle, 2              ; seconds since a key or a press
    WCV wc_tzo, 2               ; local minus UTC, minutes, signed
    WCV wc_h12, 1               ; the clock's 12-hour choice
    WCV wc_rooms, WC_NROOM * (WC_ROOMN + WC_TOPICN)
    WCV wc_cnt, 2 * WC_NROOM    ; each room's counter (C)
    WCV wc_ment, WC_NROOM       ; ...and 1 = an A named us there, unread
    WCV wc_seen, 2 * WC_NROOM   ; ...as it was when last the view
    WCV wc_rn, 1                ; the roster
    WCV wc_ros, WC_NROS * WC_RREC
    WCV wc_dn, 1                ; the private conversations
    WCV wc_dms, WC_NDM * WC_DREC
    WCV wc_fn, 1                ; the friends
    WCV wc_frs, WC_NFR * WC_NICKN
    WCV wc_notify, 1            ; 1 = notifications on
    ; --- the conductor -------------------------------------------------------
    WCV wc_rq, 1                ; RQ_* in flight
    WCV wc_rqview, 1            ; ...for this view
    WCV wc_wait, 2              ; seconds before the next request
    WCV wc_back, 2              ; ...the failure back-off
    WCV wc_age, 2               ; ticks the request in flight has taken
    WCV wc_qsay, 1              ; a message is queued...
    WCV wc_qtext, WC_TEXTN      ; ...this one
    WCV wc_qflag, 4             ; a flag is queued: this seq, 0 = none
    WCV wc_qflagdm, 1           ; ...in a conversation
    WCV wc_tsub, 1              ; ticks into the current second
    WCV wc_vgen, 1              ; bumped at every view change...
    WCV wc_rqvgen, 1            ; ...and as it was when the request was made
    WCV wc_rqh, 1               ; the request in flight asked for history
    WCV wc_ercode, 2            ; the reply: 0 OK, an ER code, 0xFFFF broken
    WCV wc_erwait, 2            ; ...and its wait
    ; --- the parser's fields (wcproto.inc) --------------------------------
    WCV wc_pseq, 4              ; a record's seq
    WCV wc_pmin, 2              ; ...its UTC minute
    WCV wc_pday, 2              ; ...its UTC day, 0 = none (SPEC.md 113.2.4)
    WCV wc_proom, 1             ; ...its room
    WCV wc_pflg, 1              ; ...the store's WCM_* for it
    WCV wc_pmine, 1             ; ...1 = this session sent it
    WCV wc_pnick, 2             ; ...the nick shown with it
    WCV wc_ptext, 2             ; ...its text
    WCV wc_pother, 2            ; ...a private message's other end
    WCV wc_dbuf, 12             ; a dword in decimal
    ; --- the painters' row (wcview.inc) ------------------------------------
    WCV wc_rinv, 1              ; the row's style, WS_*
    WCV wc_rgl, 1               ; ...its glyph, 0xFF none...
    WCV wc_rgcol, 1             ; ...at this cell
    WCV wc_rgl2, 1              ; ...a second glyph, 0xFF none...
    WCV wc_rgcol2, 1            ; ...at this cell
    WCV wc_rbold, 2             ; ...its bold string, 0 none...
    WCV wc_rbcol, 1             ; ...at this cell
    WCV wc_rul, 1               ; ...its underline, 0xFF none
    WCV wc_rspn, 1              ; ...the colour spans wc_rspan collected
    WCV wc_rsps, 3 * (WC_NSPAN - 1)
    WCV wc_rband, 2             ; ...and the band's number, for its slot
    WCV wc_rrec, 2              ; the message record a pane row draws
    WCV wc_rowy, 2              ; the band being drawn
    WCV wc_rbuf, 64             ; the link's words, and 'retrying in Ns'
    WCV wc_lmc, 2               ; the status bar's lamps: their cell
    WCV wc_fresh, 1             ; 1 = a full paint has just laid the ground
    ; --- the band and the faces (wcband.inc) --------------------------------
    WCV wc_col, 1               ; 1 = the display has colour (wc_face)
    WCV wc_thm, 1               ; the theme chosen (TH_*, WIRECHAT.CFG)
    WCV wc_eth, 1               ; ...and the one drawn: a mono one on 1 bit
    WCV wc_tint, 1              ; 1 = Wired on colour: its LEDs, nicks, emoticons
    WCV wc_pens, WC_NPEN        ; its colours, by R_*
    WCV os88line_pens, 3        ; the field's (SPEC.md 83.5): ink, ground, frame
    WCV wc_glbuf, 34            ; a glyph's icon record, built to draw
    WCV wc_thmi, TH_N * 14      ; the Theme menu's items, composed
    WCV wc_meta, WC_METAN       ; the band being drawn: its meta...
    WCV wc_bx, 2                ; ...where (screen)
    WCV wc_by, 2
    WCV wc_bslot, 2             ; ...through which slot, 0 none
    WCV wc_bfirst, 2            ; ...the cells that differ, first and last
    WCV wc_blast, 2
    WCV wc_bn, 1                ; ...a span count down
    WCV wc_binkd, 1             ; ...1 = only span inks moved (wc_inkrun)
    WCV wc_bgid, 1              ; ...a glyph
    WCV wc_bps, 2               ; ...the run wc_bspans holds: its first
    WCV wc_bpe, 2               ; cell, one past its last...
    WCV wc_bpc, 2               ; ...and its ink and ground
    WCV wc_fx, 2                ; the face (SPEC.md 113.5.3): a run's x...
    WCV wc_fy, 2                ; ...its cell's top...
    WCV wc_fcnt, 2              ; ...its characters...
    WCV wc_fpen, 2              ; ...the blit's pen: set bits, clear bits
    WCV wc_fbc, 2               ; ...its bold, [first, end), signed
    WCV wc_fbe, 2
    WCV wc_ful, 1               ; ...its last row's colour, 0xFF = none
    WCV wc_fband, WC_FSTR * WC_FROWS    ; ...composed, at a fixed stride
    WCV wc_dtop, 2              ; the pane's top line AS DRAWN, 0xFFFF owed
    WCV wc_act, 1               ; the activity lamp's seconds left
    WCV wc_bv, 8                ; a button's rect, as its painter has it
    WCV wc_slw, WC_NWIDE * WC_SLOTW     ; the slots (SPEC.md 113.5.1)
    WCV wc_sln, WC_MAXB * WC_SLOTNN
    ; --- the actions (wcact.inc) ------------------------------------------
    WCV wc_rqdone, 1            ; the request a reply answered
    WCV wc_rqnote, 1            ; ...and whether it set the notice line
    WCV wc_draft, WC_TEXTN      ; the message set aside for another mode
    WCV wc_askn, WC_NICKN       ; the nick an alert asks about...
    WCV wc_askwhat, 1           ; ...and what it asks: 1 add, 2 remove, 3 flag
    WCV wc_askbuf, 40           ; ...in its words
    WCV wc_mbuf, WC_NOTEN       ; '<nick> mentioned you in #<room>'
    WCV wc_cpl, 1               ; Tab's @nick run: the typed prefix, 0 none
    WCV wc_cpi, 1               ; ...the roster record it took last
    WCV wc_cpart, 2             ; ...what was typed after the @ this time
    ; --- the worker and the HTTP client (wcnet.inc) ----------------------------
    WCV wc_state, 1             ; WS_*
    WCV wc_gen, 1               ; the generation (SPEC.md 71.11)
    WCV wc_gen0, 1
    WCV wc_hnd, 1               ; the socket, 0 = none
    WCV wc_wake, 1              ; WW_*: the one byte
    WCV wc_wake0, 1
    WCV wc_nodrv, 1             ; the last failure was "no driver"
    WCV wc_reqn, 2              ; the request's bytes (the composer's)
    WCV wc_code, 2              ; --- wc_start zeroes from here...
    WCV wc_clen, 4              ; the reply's Content-Length, dword
    WCV wc_haslen, 2            ; ...and whether it said one (a word: the
                                ; zeroed run is stored as words)
    WCV wc_got, 2               ; body bytes so far
    WCV wc_hlen, 2              ; header line bytes so far
    WCV wc_hline, 2             ; header lines so far
    WCV wc_sent, 2              ; request bytes sent
    WCV wc_rxp, 2               ; unread bytes in wc_rxb: where...
    WCV wc_rxn, 2               ; ...and how many --- ...to here
    WCV wc_host, WC_HOSTMAX
    WCV wc_port, 2
    WCV wc_pfx, WC_PFXMAX
    WCV wc_hbuf, WC_HLINE
    WCV wc_reqb, WC_REQMAX
    WCV wc_rxb, WC_CHUNK
    WCV wc_resp, WC_RESPMAX + 2 ; the body, NUL-terminated
    ; --- the configuration (wccfg.inc) -----------------------------------
    WCV wc_cfgb, WC_CFGMAX + 2
    WCV wc_cfgst, 1             ; od_write's state byte
    WCV wc_cfgld, 1             ; the file has been read
    WCV wc_save, 1              ; a write is owed
WC_BSS equ WCB

; =============================================================================
; wc_entry - the package entry point (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; The Wire's: create, declare, install - and KICK ITSELF, so the file read
; and the first request happen in the wake handler with the window already
; on the glass (SPEC.md 74.1).
; =============================================================================
wc_entry:
    mov si, wc_tpl
    call OSAPI_WM_CREATE                ; BX = window ptr, CF on table full
    jc wc_ret
    mov [wc_win], bx
    OS88_REGION_MOVABLE_NOKEEP wc_ret   ; every byte that outlives a pass is a
                                        ; static; the worker declares itself at
                                        ; its spawn (wc_hire)
    mov al, 1
    call OSAPI_WM_SNAP                  ; 8-aligned content (SPEC.md 11.94)
    mov bx, [wc_win]
    mov al, 1                           ; every pixel is ours (SPEC.md 113.5):
    call OSAPI_WM_OWNBG                 ; a dark theme never flashes white
    mov al, 1
    call OSAPI_WM_SIZABLE
    mov si, wc_sizes
    call OSAPI_WM_PREFER
    mov cx, WC_MINW
    mov dx, WC_MINH
    call OSAPI_WM_MINSIZE
    mov si, wc_menus
    call OSAPI_MENU_SET
    mov si, wc_about
    call OSAPI_ABOUT_SET
    mov bx, [wc_win]                    ; BX re-loaded before each hook: The
    mov ax, wc_onwake                   ; Wire's reason (a stale BX is
    call OSAPI_WM_ONWAKE                ; refused in silence)
    mov bx, [wc_win]
    mov ax, wc_onresize
    call OSAPI_WM_ONRESIZE
    mov bx, [wc_win]
    mov ax, wc_onrclick
    call OSAPI_WM_ONRCLICK
    mov bx, [wc_win]
    mov ax, wc_ontimer
    call OSAPI_WM_ONTIMER
    mov bx, [wc_win]
    mov ax, wc_onwheel                  ; the mouse WHEEL scrolls the pane
    call OSAPI_WM_ONWHEEL               ; (SPEC.md 13.18); a no-op on the
                                        ; 128KB kernel, flags preserved
    mov bx, [wc_win]
    mov ax, 18                          ; one second; re-armed in the handler
    call OSAPI_WM_TIMER
    mov ax, [wc_win]                    ; the buttons' gesture (SPEC.md
    mov bx, wc_btrec                    ; 20.5.1.3): the library installs the
    mov si, wc_onup                     ; press, the release and the drag, and
    mov di, wc_ondrag                   ; chains a press that is not a
    mov dx, wc_onclick                  ; button's to wc_onclick
    call os88ui_btninit
    ; --- the state a fresh window starts in ------------------------------
    mov word [wc_ln+LN_BUF], wc_lbuf    ; os88line wants these before the
    mov word [wc_ln+LN_MAX], WC_TEXTN   ; first key (telnet.asm's warning:
    mov byte [wc_ln+LN_FOCUS], 1        ; zeroed bss aims LN_BUF at the header)
    mov al, 0xFF
    mov [wc_sel], al
    mov [wc_divi], al
    mov [wc_find], al
    mov byte [wc_notify], 1
    mov byte [wc_thm], TH_DEFAULT
    mov byte [wc_hist], 1
    mov byte [wc_stsent], 0xFF          ; nothing sent: the first request says
    mov word [wc_dirty], WCD_ALL
    call wc_cfgdefault                  ; the host before the file is read
    mov bx, [wc_win]
    call OSAPI_WM_WAKE                  ; ...which the first wake does
    clc
wc_ret:
    ret

; --- the window template and the three sizes ---------------------------------
; y = 22 so the CGA frame clears the dock without the fit moving it: 22 + 150
; - 1 = 171, and the dock owns row 176.
wc_tpl:
    dw 16, 22, WC_FRW, WC_HVGA
    dw wc_ttl, wc_paint, wc_onkey, 0    ; no W_ONCLICK: os88ui_btninit's

    OS88_PREFER wc_sizes, WC_FRW, WC_HVGA,  WC_FRWH, WC_HHERC,  WC_FRW, WC_HCGA

wc_ttl      equ 16                      ; the header's name field: 'WireChat'

; --- the menus (SPEC.md 113.6.4) ---------------------------------------------
    OS88_MENUSET wc_menus, wc_ttl, wc_oncmd
        OS88_MENU wc_m_chat, wc_i_chat, 5
        OS88_MENU wc_m_rooms, wc_i_rooms, WC_NROOM
        OS88_MENU wc_m_stat, wc_i_stat, 3
        OS88_MENU wc_m_frnd, wc_i_frnd, 2
        OS88_MENU wc_m_thm, wc_i_thm, TH_N
    OS88_MENUSET_END wc_menus

WM_CHAT     equ 0                       ; menu indices, in the set's order
WM_ROOMS    equ 1
WM_STAT     equ 2
WM_FRND     equ 3
WM_THEME    equ 4

; --- the About card's lines (SPEC.md 12.2, 20.5.1) ----------------------------
wc_ablines:
    dw wc_ab1, wc_ab2, wc_ab3, wc_ab4, wc_ab5, 0
wc_hlplines:                            ; F1's card: the keys (SPEC.md 113.6.3)
    dw wc_h1, wc_h2, wc_h3, wc_h4, wc_h5, wc_h6, wc_h7, 0

; =============================================================================
; 'About WireChat' - the standard card
; =============================================================================
; wc_about - the OSAPI_ABOUT_SET handler. in: SI = our window; lock HELD
wc_about:
    mov byte [wc_abon], 1               ; 2 is F1's help card (wc_fkey)
    call wc_popdown                     ; a popup and the card cannot both be up
    mov bx, si
    mov si, wc_ablines
    jmp os88ui_about                    ; arms the clip itself; a tail call

%include "wctxt.inc"
%include "wccfg.inc"
%include "wcnet.inc"
%include "wcproto.inc"
%include "wcview.inc"
%include "wcband.inc"
%include "wcact.inc"

; the buttons' record (SPEC.md 20.5.1.3): rects and flags in the bss, filled
; by wc_layout; labels in wctxt.inc
    OS88UI_BTNREC wc_btrec, wc_brects, wc_btown, wc_bflags, WB_N

; --- wc_btn - draw button AL (its index PLUS ONE, os88ui_btn's convention) -
; The one call site, beside the record it draws from: the painters in
; wcview.inc and wcact.inc come here, so the record and its control are in
; one file (tests/btnsites.txt). Preserves every register.
wc_btn:
    push bx
    mov bx, wc_btrec
    call os88ui_btn
    pop bx
    ret

; --- the shared code --------------------------------------------------------------
%define OS88UI_ABOUT
%define OS88UI_BOWN                 ; the themes paint their own buttons
%define OS88UI_ALERT
%define OS88UI_NOGLYPH              ; no check box or radio drawn here
%include "os88ui.inc"
%define OS88LINE_PENS               ; the field in the theme (SPEC.md 83.5)
%define OS88LINE_RUN wc_frun        ; ...lettered in our face (SPEC.md 83.6):
%define OS88LINE_RUNUP 1            ; its cell starts a row above font_run's
%define OS88LINE_RUNH WC_FROWS      ; and is WC_FROWS rows deep
%include "os88line.inc"
%include "os88data.inc"
%define OS88SOCK_STATE              ; net_find asks NETV_STATE too
%include "os88sock.inc"

%if OS88LINE_SZ != 20
  %error "wc_ln is declared 20 bytes and os88line.inc's block is not"
%endif

    OS88_BSS WC_BSS
    OS88_IMAGE_END
