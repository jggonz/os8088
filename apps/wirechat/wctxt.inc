; =============================================================================
; os8088 - apps/wirechat/wctxt.inc
;
; EVERY STRING WIRECHAT DRAWS OR SENDS, and the glyphs. ASCII 0x20..0x7E only:
; the machine's font has 95 glyphs (SPEC.md 6.1), and three dots are three
; full stops.
; =============================================================================

wc_empty:       db 0

; --- the wire (SPEC.md 113.2, 113.3) --------------------------------------------
wc_s_post:      db 'POST ', 0
wc_s_v1:        db 'v1/', 0
wc_s_join:      db 'join', 0
wc_s_poll:      db 'poll', 0
wc_s_say:       db 'say', 0
wc_s_flag:      db 'flag', 0
wc_q_t:         db '?t=', 0
wc_q_jv:        db '?v=', 0
wc_q_v:         db '&v=', 0
wc_q_s:         db '&s=', 0
wc_q_r:         db '&r=', 0
wc_q_st:        db '&st=', 0
wc_q_h:         db '&h=1', 0
wc_q_m:         db '&m=', 0
wc_q_x:         db '&x=1', 0
wc_s_http:      db ' HTTP/1.0', 13, 10, 'Host: ', 0
wc_s_tail:      db 13, 10, 'User-Agent: os8088 WireChat 1.0', 13, 10
                db 'Connection: close', 13, 10
                db 'Content-Length: ', 0
wc_s_crlf2:     db 13, 10, 13, 10, 0
wc_s_clen:      db 'content-length:', 0

; --- WIRECHAT.CFG (SPEC.md 113.8) ------------------------------------------------
wc_cfgname:     db 'WIRECHAT.CFG', 0
wc_k_host:      db 'host', 0
wc_k_nick:      db 'nick', 0
wc_k_notify:    db 'notify', 0
wc_k_friend:    db 'friend', 0
wc_k_code:      db 'code', 0
wc_k_theme:     db 'theme', 0
wc_def_host:    db 'os8088.com', 0
wc_def_pfx:     db '/chat/', 0

; --- the notice line -------------------------------------------------------------
wc_s_connecting: db 'Connecting to WireChat...', 0
wc_s_picknick:  db 'Choose a nick, then press Enter', 0
wc_s_joined:    db 'Welcome to WireChat. Be kind.', 0
wc_s_nodrv:     db 'No network card - see Control Panel', 0
wc_s_sending:   db 'Still sending the last message...', 0
wc_s_badnick:   db 'A nick is 2-12 letters, digits, - or _', 0
wc_s_notfound:  db 'Not found', 0
wc_s_selfirst:  db 'Click a message first, then flag it', 0
wc_s_ownmsg:    db 'That message is your own', 0
wc_s_flagging:  db 'Flagging...', 0
wc_s_remhow:    db 'Open a friend to remove them', 0

; --- the header, the panes and the bar --------------------------------------------
wc_s_nonick:    db '(no nick yet)', 0
wc_s_offline:   db 'OFFLINE', 0
wc_s_online_w:  db 'Online', 0
wc_s_away_w:    db 'Away', 0
wc_s_busy_w:    db 'Busy', 0
wc_stwords:     dw wc_s_online_w, wc_s_away_w, wc_s_busy_w
wc_s_hon:       db 'ONLINE', 0          ; the header's words, by ST_*
wc_s_haway:     db 'AWAY', 0
wc_s_hbusy:     db 'BUSY', 0
wc_hwords:      dw wc_s_hon, wc_s_haway, wc_s_hbusy
wc_s_brand:     db 'WireChat', 0
wc_s_online:    db 'ONLINE', 0          ; the right pane's sections
wc_s_frnds:     db 'FRIENDS', 0
wc_s_direct:    db 'DIRECT', 0
wc_s_nodm:      db 'No direct messages', 0
wc_s_msgto:     db 'Message ', 0        ; the empty field's placeholder
wc_s_f1:        db 'F1 Help', 0         ; the status bar's legend
wc_s_f2:        db 'F2 Rooms', 0
wc_s_f3:        db 'F3 Friends', 0
wc_fkeys:       dw wc_s_f1, wc_s_f2, wc_s_f3
wc_s_colon:     db ': ', 0
wc_s_inrm:      db ' in #', 0
wc_s_dots:      db '...', 0
wc_s_private:   db 'Private conversation', 0
wc_s_new1:      db ' new message ', 0
wc_s_newn:      db ' new messages ', 0
wc_s_bar:       db ' | ', 0
wc_s_onl:       db ' online', 0
wc_s_notify:    db 'Notifications: ', 0
wc_s_on:        db 'On ', 0              ; as wide as Off: the spans stay put
wc_s_off:       db 'Off', 0
wc_s_ment:      db ' mentioned you in #', 0
; the day divider's words (SPEC.md 113.5): three letters each, so the name of
; a day is an index times four
wc_s_days:      db 'Sun', 0, 'Mon', 0, 'Tue', 0, 'Wed', 0, 'Thu', 0, 'Fri', 0
                db 'Sat', 0
wc_s_months:    db 'Jan', 0, 'Feb', 0, 'Mar', 0, 'Apr', 0, 'May', 0, 'Jun', 0
                db 'Jul', 0, 'Aug', 0, 'Sep', 0, 'Oct', 0, 'Nov', 0, 'Dec', 0
; the status bar's word for each LK_*, in that order
wc_lk_nonick:   db 'Choose a nick', 0
wc_lk_nodrv:    db 'No network card', 0
wc_lk_join:     db 'Connecting...', 0
wc_lk_live:     db 'Connected', 0
wc_lk_rest:     db 'Resting - trying again later', 0
wc_lk_off:      db 'Offline - retrying in ', 0
wc_lk_key:      db 'Enter the code for this nick', 0
wc_linkwords:   dw wc_lk_nonick, wc_lk_nodrv, wc_lk_join, wc_lk_live
                dw wc_lk_rest, wc_lk_off, wc_lk_key
%if LK_NONICK != 0 || LK_NODRV != 1 || LK_JOIN != 2 || LK_LIVE != 3 || LK_REST != 4 || LK_OFF != 5 || LK_KEY != 6
  %error "wc_linkwords is indexed by LK_*"
%endif

; --- the field's prompts, by MD_* ------------------------------------------------
wc_p_msg:       db '>', 0
wc_p_nick:      db 'Nick:', 0
wc_p_find:      db 'Find:', 0
wc_p_friend:    db 'Friend:', 0
wc_p_key:       db 'Code:', 0
wc_prompts:     dw wc_p_msg, wc_p_nick, wc_p_find, wc_p_friend, wc_p_key
%if MD_MSG != 0 || MD_NICK != 1 || MD_FIND != 2 || MD_FRIEND != 3 || MD_KEY != 4
  %error "wc_prompts is indexed by MD_*"
%endif

; --- the emoticons (SPEC.md 113.6.2): ASCII, because the font is -----------------
wc_e0:          db ':)', 0
wc_e1:          db ':(', 0
wc_e2:          db ';)', 0
wc_e3:          db ':D', 0
wc_e4:          db ':P', 0
wc_e5:          db '<3', 0
wc_e6:          db '^_^', 0
wc_e7:          db 'o_O', 0
wc_emos:        dw wc_e0, wc_e1, wc_e2, wc_e3, wc_e4, wc_e5, wc_e6, wc_e7
wc_emoink:      db CBROWN, CBLUE, CMAGENTA, CGREEN      ; Wired on a colour
                db CRED, CLRED, CCYAN, CDGRAY           ; display (wc_tint)
%if $ - wc_emoink != PP_EMON
  %error "an ink for each emoticon offered"
%endif

; --- the buttons' captions (wcband.inc paints them) ---------------------------
wc_b_search:    db 'Search...', 0
wc_b_sendc:     db '  Send   ', 0       ; WC_SENDC cells: the block's width
%if $ - wc_b_sendc - 1 != WC_SENDC
  %error "Send's caption is as wide as Send"
%endif

; --- the alerts (at most OS88UI_AMAX = 34 characters each, composed) ------------
wc_s_askflag:   db 'Flag this message as abusive?', 0
wc_s_askadd1:   db 'Add ', 0
wc_s_askadd2:   db ' as a friend?', 0
wc_s_askrem1:   db 'Remove ', 0
wc_s_askrem2:   db ' from friends?', 0

; --- the menus (SPEC.md 113.6.4) ------------------------------------------------
wc_m_chat:      db 'Chat', 0
wc_i_chat:      dw wc_it_nick, wc_it_flag, wc_it_notoff, wc_it_emo, wc_it_reconn
wc_it_nick:     db 'Change Nick...', 0
wc_it_flag:     db 'Flag Message...', 0
wc_it_notoff:   db 'Notifications Off', 0   ; what the item DOES: shown while
wc_it_noton:    db 'Notifications On', 0    ; they are on, and the other while not
wc_it_emo:      db 'Emoticons...', 0
wc_it_reconn:   db 'Reconnect', 0
wc_m_rooms:     db 'Rooms', 0
wc_i_rooms:     dw wc_r0, wc_r1, wc_r2, wc_r3
wc_r0:          db 'lobby', 0           ; the server's names (hub.js ROOMS); a
wc_r1:          db 'hardware', 0        ; menu cannot wait for the L lines
wc_r2:          db 'development', 0
wc_r3:          db 'games', 0
wc_m_stat:      db 'Status', 0
wc_i_stat:      dw wc_s_online_w, wc_s_away_w, wc_s_busy_w
wc_m_frnd:      db 'Friends', 0
wc_i_frnd:      dw wc_it_addf, wc_it_remf
wc_it_addf:     db 'Add Friend...', 0
wc_it_remf:     db 'Remove Friend', 0
wc_m_thm:       db 'Theme', 0           ; composed in wc_thmi (wc_menulabels):
wc_i_thm:       dw wc_thmi, wc_thmi + 14, wc_thmi + 28, wc_thmi + 42
                dw wc_thmi + 56
wc_thnames:     dw wc_tn0, wc_tn1, wc_tn2, wc_tn3, wc_tn4
wc_tn0:         db 'Wired', 0           ; a mark, a space and the name, at
wc_tn1:         db 'Mono-Dark', 0       ; most 14 with the grey byte
wc_tn2:         db 'Blue', 0
wc_tn3:         db 'Green', 0
wc_tn4:         db 'Amber', 0

; --- the About card -------------------------------------------------------------------
wc_ab1:         db 'WireChat for os8088', 0
wc_ab2:         db 'Chat rooms on the wire', 0
wc_ab3:         db 0
wc_ab4:         db 'Server: os8088.com/chat', 0
wc_ab5:         db 'Two flags remove a message', 0

; --- F1's card: the keys (SPEC.md 113.6.3) -----------------------------------
wc_h1:          db 'WireChat keys', 0
wc_h2:          db 'Enter send, Esc clear', 0
wc_h3:          db 'Tab next room, or @nick', 0
wc_h4:          db 'PgUp PgDn Up Down scroll', 0
wc_h5:          db 'F2 next room, F3 friend', 0
wc_h6:          db 'Right-click: flag, friend', 0
wc_h7:          db 'Theme menu: five looks', 0

; =============================================================================
; THE FACE (SPEC.md 113.5.3): 95 characters, 32..126, of WC_FROWS rows each,
; bit 7 the leftmost and 1 = ink - apps/wirechat/wcface.t88's regular style,
; written by tools/os88face.py --bitmap. wcband.inc's wc_fput sets it.
; =============================================================================
wc_font:
    incbin "wcface.bin"
%if $ - wc_font != (WC_FLAST - WC_FFIRST + 1) * WC_FROWS
  %error "wcface.bin is not 95 characters of WC_FROWS rows: is wcface.t88's rows WC_LH?"
%endif

; =============================================================================
; THE GLYPHS (wcview.inc's GL_* order): eight rows of eight pixels each, drawn
; over a mask of the whole cell (wcband.inc's wc_glrec builds the record) -
; so a glyph is its own ground and leaves nothing of the space it is drawn on
; (SPEC.md 25.6's record). Each has a ROLE, which is its ink in every theme.
; =============================================================================
wc_glyphs:
    db 0x00, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x00  ; GL_ONLINE: a square
    db 0x3C, 0x42, 0x91, 0x91, 0x9D, 0x81, 0x42, 0x3C  ; GL_AWAY: a clock
    db 0x00, 0x7E, 0x42, 0x7E, 0x7E, 0x42, 0x7E, 0x00  ; GL_BUSY: barred
    db 0x00, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x7E, 0x00  ; GL_GONE: hollow
    db 0x00, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x00  ; GL_LKOK: a lamp
    db 0x00, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x00  ; GL_LKWARN
    db 0x00, 0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x00  ; GL_LKBAD
    db 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00  ; GL_LKOFF: a ring
    db 0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00  ; GL_ACT: a diamond
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  ; GL_BLANK
    db 0x00, 0x66, 0x66, 0x00, 0xFF, 0xFF, 0xFF, 0x00  ; GL_PEOPLE: two
    db 0x00, 0xFF, 0xC3, 0xA5, 0x99, 0x81, 0xFF, 0x00  ; GL_MAIL: an envelope
    db 0x78, 0x84, 0x84, 0x84, 0x78, 0x0C, 0x06, 0x03  ; GL_LENS
    db 0x00, 0x18, 0x3C, 0x7E, 0xFF, 0x00, 0x00, 0x00  ; GL_SBUP
    db 0x00, 0x00, 0x00, 0xFF, 0x7E, 0x3C, 0x18, 0x00  ; GL_SBDN
    db 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C  ; GL_SBTHM
    db 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18  ; GL_SBTRK
    db 0x3C, 0x7E, 0xCF, 0xDF, 0xFF, 0xFF, 0x7E, 0x3C  ; GL_LDLIT: lit, a glint
    db 0x3C, 0x72, 0xF1, 0xF1, 0xF1, 0xF1, 0x72, 0x3C  ; GL_LDHALF: half lit
    db 0x3C, 0x42, 0xA5, 0x99, 0x99, 0xA5, 0x42, 0x3C  ; GL_LDX: crossed
    db 0x3C, 0x42, 0x81, 0x81, 0x81, 0x81, 0x42, 0x3C  ; GL_LDOFF: out
wc_glrole:
    db R_OK, R_WARN, R_BAD, R_DIM                       ; the statuses
    db R_OK, R_WARN, R_BAD, R_DIM                       ; the link lamp
    db R_WARN, R_BG                                     ; activity, blank
    db R_ACC, R_ACC, R_ACC                              ; people, mail, lens
    db R_ACC, R_ACC, R_ACC, R_DIM                       ; the scroll column
    db R_FG, R_FG, R_FG, R_FG                           ; the LEDs (the span's)
%if wc_glrole - wc_glyphs != 8 * GL_N || $ - wc_glrole != GL_N
  %error "wc_glyphs and wc_glrole are GL_* order"
%endif
