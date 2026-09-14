' WORDXT.BAS - a word processor tuned to run fast on a 4.77 MHz XT.
' Same document model as WORD.BAS, but it renders in TEXT mode (SCREEN 0) so
' the video card's character ROM draws every glyph in hardware - hundreds of
' times faster than the software bitmap font WORD.BAS draws in graphics mode.
' Speed tricks: (1) text mode, (2) the real blinking hardware cursor instead of
' a software blink loop, (3) only the CHANGED line is repainted on a keystroke;
' the whole page is repainted only when the line count changes or it scrolls.

' ---- geometry (all in screen character cells) ----
LM% = 4 : PW% = 72 : TEXTTOP% = 3 : VIS% = 21 : SROW% = 25
TR% = 1300

' ---- data model: flat array of ASCII codes; 13 = hard paragraph break ----
MAXCH% = 1200
DIM CH%(MAXCH%)
DLEN% = 0 : CUR% = 0

' ---- reflow output (one ROWSTART per wrapped visual row) ----
DIM ROWSTART%(TR% + 2)
NROWS% = 0 : CROW% = 0 : CCOL% = 0 : DESCOL% = -1

' ---- view / flags ----
TOP% = 0 : QUIT% = 0 : EDITED% = 0 : FULL% = 0

SCREEN 0
COLOR 7, 0
CLS
CALL SeedDemo
CALL Reflow
CALL DrawTitle
CALL RenderAll
CALL DrawStatus
CALL PlaceCaret

DO
  K$ = INKEY$
  IF K$ <> "" THEN CALL HandleKey(K$)
LOOP UNTIL QUIT% = 1

LOCATE SROW%, 1
COLOR 15, 1
PRINT LEFT$(" WORDXT - session ended." + SPACE$(80), 79);
COLOR 7, 0
LOCATE SROW% - 1, 1
END

' ============================ rendering ============================

SUB DrawTitle
  SHARED PW%
  LOCATE 1, 1
  COLOR 15, 1
  PRINT LEFT$(" WORDXT.BAS  Arrows/Home/End move   Enter=new para   ESC=quit" + SPACE$(80), 79);
  COLOR 7, 0
END SUB

SUB DrawStatus
  SHARED CROW%, CCOL%, DLEN%, SROW%
  LOCATE SROW%, 1
  COLOR 15, 1
  S$ = " Ln " + FNS2$(CROW% + 1) + "   Col " + FNS2$(CCOL% + 1) + "   " + FNS2$(DLEN%) + " chars"
  PRINT LEFT$(S$ + SPACE$(80), 79);
  COLOR 7, 0
END SUB

' Build and print one VISUAL row (0-based) at its screen line. Pads to PW% so
' old characters are cleared. Always one PRINT - this is the fast path.
SUB DrawRow(VR%)
  SHARED CH%(), ROWSTART%(), DLEN%, NROWS%, TOP%, VIS%, TEXTTOP%, LM%, PW%
  IF VR% < TOP% THEN EXIT SUB
  IF VR% > TOP% + VIS% - 1 THEN EXIT SUB
  SR% = TEXTTOP% + (VR% - TOP%)
  LOCATE SR%, LM%
  COLOR 7, 0
  IF VR% >= NROWS% THEN
    PRINT SPACE$(PW%);
    EXIT SUB
  END IF
  RS% = ROWSTART%(VR%)
  IF VR% < NROWS% - 1 THEN
    RE% = ROWSTART%(VR% + 1)
  ELSE
    RE% = DLEN%
  END IF
  L$ = ""
  FOR J% = RS% TO RE% - 1
    C% = CH%(J%)
    IF C% <> 13 THEN L$ = L$ + CHR$(C%)
  NEXT
  IF LEN(L$) > PW% THEN L$ = LEFT$(L$, PW%)
  PRINT L$ + SPACE$(PW% - LEN(L$));
END SUB

SUB RenderAll
  SHARED TOP%, VIS%
  FOR VR% = TOP% TO TOP% + VIS% - 1
    CALL DrawRow(VR%)
  NEXT
END SUB

' Position the real (hardware) text cursor at the caret. The third LOCATE
' parameter (,1) turns the blinking cursor ON: real Turbo Basic leaves it OFF
' during RUN, so without it the caret would not flash on actual hardware. It
' blinks for free once enabled, so there is no software blink loop burning XT
' cycles.
SUB PlaceCaret
  SHARED CROW%, CCOL%, TOP%, VIS%, TEXTTOP%, LM%, PW%
  IF CROW% < TOP% THEN EXIT SUB
  IF CROW% > TOP% + VIS% - 1 THEN EXIT SUB
  SR% = TEXTTOP% + (CROW% - TOP%)
  SC% = LM% + CCOL%
  IF SC% > LM% + PW% THEN SC% = LM% + PW%
  COLOR 7, 0
  LOCATE SR%, SC%, 1
END SUB

' Scroll the window so the caret row is visible; FULL% forces a full repaint.
SUB EnsureVisible
  SHARED CROW%, TOP%, VIS%, FULL%
  IF CROW% < TOP% THEN
    TOP% = CROW% : FULL% = 1
  END IF
  IF CROW% > TOP% + VIS% - 1 THEN
    TOP% = CROW% - VIS% + 1 : FULL% = 1
  END IF
  IF TOP% < 0 THEN TOP% = 0
END SUB

' ============================ word-wrap reflow ============================

SUB Reflow
  SHARED CH%(), ROWSTART%(), DLEN%, CUR%, PW%, TR%, NROWS%, CROW%, CCOL%
  RIDX% = 0
  ROWSTART%(0) = 0
  COL% = 0
  ROWB% = 0
  LASTSP% = -1
  I% = 0
  CROW% = 0 : CCOL% = 0 : CAPT% = 0
  DO WHILE I% < DLEN%
    IF I% = CUR% THEN
      CROW% = RIDX% : CCOL% = COL% : CAPT% = 1
    END IF
    C% = CH%(I%)
    IF C% = 13 THEN
      I% = I% + 1
      IF RIDX% < TR% THEN RIDX% = RIDX% + 1
      ROWB% = I% : LASTSP% = -1 : COL% = 0
      ROWSTART%(RIDX%) = ROWB%
    ELSE
      IF C% = 32 THEN LASTSP% = I%
      COL% = COL% + 1
      I% = I% + 1
      IF COL% > PW% THEN
        IF LASTSP% >= ROWB% AND LASTSP% >= 0 THEN
          BRK% = LASTSP% + 1
        ELSE
          BRK% = I% - 1
        END IF
        IF RIDX% < TR% THEN RIDX% = RIDX% + 1
        ROWB% = BRK%
        ROWSTART%(RIDX%) = ROWB%
        I% = BRK%
        COL% = 0 : LASTSP% = -1
      END IF
    END IF
  LOOP
  IF CAPT% = 0 THEN
    CROW% = RIDX% : CCOL% = COL%
  END IF
  NROWS% = RIDX% + 1
END SUB

' ============================ editing ============================

SUB InsAt(CODE%)
  SHARED CH%(), DLEN%, CUR%, MAXCH%
  IF DLEN% >= MAXCH% THEN EXIT SUB
  FOR J% = DLEN% TO CUR% + 1 STEP -1
    CH%(J%) = CH%(J% - 1)
  NEXT
  CH%(CUR%) = CODE%
  DLEN% = DLEN% + 1
  CUR% = CUR% + 1
END SUB

SUB DelAt(PP%)
  SHARED CH%(), DLEN%
  IF PP% < 0 OR PP% >= DLEN% THEN EXIT SUB
  FOR J% = PP% TO DLEN% - 2
    CH%(J%) = CH%(J% + 1)
  NEXT
  DLEN% = DLEN% - 1
END SUB

SUB MoveVert(DIR%)
  SHARED CH%(), ROWSTART%(), DLEN%, CUR%, NROWS%, CROW%, CCOL%, DESCOL%
  CALL Reflow
  IF DESCOL% < 0 THEN DESCOL% = CCOL%
  NR% = CROW% + DIR%
  IF NR% < 0 THEN EXIT SUB
  IF NR% >= NROWS% THEN EXIT SUB
  RS% = ROWSTART%(NR%)
  IF NR% = NROWS% - 1 THEN
    RE% = DLEN%
  ELSE
    RE% = ROWSTART%(NR% + 1)
  END IF
  RLEN% = RE% - RS%
  IF RLEN% > 0 THEN
    IF CH%(RE% - 1) = 13 THEN RLEN% = RLEN% - 1
  END IF
  TC% = DESCOL%
  IF TC% > RLEN% THEN TC% = RLEN%
  CUR% = RS% + TC%
END SUB

SUB MoveHome
  SHARED ROWSTART%(), CUR%, CROW%, DESCOL%
  CALL Reflow
  CUR% = ROWSTART%(CROW%)
  DESCOL% = -1
END SUB

SUB MoveEnd
  SHARED CH%(), ROWSTART%(), DLEN%, CUR%, NROWS%, CROW%, DESCOL%
  CALL Reflow
  IF CROW% = NROWS% - 1 OR CROW% >= NROWS% THEN
    CUR% = DLEN%
  ELSE
    RE% = ROWSTART%(CROW% + 1)
    IF RE% > 0 THEN
      IF CH%(RE% - 1) = 13 THEN RE% = RE% - 1
    END IF
    CUR% = RE%
  END IF
  DESCOL% = -1
END SUB

' One dispatch per keystroke. Edits set EDITED%; movement leaves it 0 so we skip
' the row repaint and only move the caret.
SUB HandleKey(K$)
  SHARED CUR%, DLEN%, NROWS%, CROW%, DESCOL%, QUIT%, EDITED%, FULL%
  EDITED% = 0
  OLDNR% = NROWS%
  IF LEN(K$) = 2 AND ASC(K$) = 0 THEN
    EC% = ASC(MID$(K$, 2, 1))
    SELECT CASE EC%
      CASE 75
        IF CUR% > 0 THEN CUR% = CUR% - 1
        DESCOL% = -1
      CASE 77
        IF CUR% < DLEN% THEN CUR% = CUR% + 1
        DESCOL% = -1
      CASE 72
        CALL MoveVert(-1)
      CASE 80
        CALL MoveVert(1)
      CASE 71
        CALL MoveHome
      CASE 79
        CALL MoveEnd
      CASE 83
        IF CUR% < DLEN% THEN CALL DelAt(CUR%) : EDITED% = 1
        DESCOL% = -1
    END SELECT
  ELSE
    C% = ASC(K$)
    SELECT CASE C%
      CASE 27
        QUIT% = 1
      CASE 8
        IF CUR% > 0 THEN
          CALL DelAt(CUR% - 1)
          CUR% = CUR% - 1
          EDITED% = 1
        END IF
      CASE 13
        CALL InsAt(13) : EDITED% = 1
      CASE 9
        CALL InsAt(32) : CALL InsAt(32) : EDITED% = 1
      CASE 32 TO 126
        CALL InsAt(C%) : EDITED% = 1
    END SELECT
    DESCOL% = -1
  END IF
  IF QUIT% = 1 THEN EXIT SUB
  CALL Reflow
  CALL EnsureVisible
  IF FULL% = 1 THEN
    CALL RenderAll
    FULL% = 0
  ELSEIF EDITED% = 1 THEN
    IF NROWS% <> OLDNR% THEN
      ' Line count changed (wrap / new paragraph / join) - rows from the caret
      ' down all shift, so repaint the visible window. Rows above never change.
      CALL RenderFrom(CROW%)
    ELSE
      ' In-place edit: only the caret's own line changed.
      CALL DrawRow(CROW%)
    END IF
  END IF
  CALL DrawStatus
  CALL PlaceCaret
END SUB

' Repaint visible rows from VR% (clamped to the window) to the bottom.
SUB RenderFrom(VR%)
  SHARED TOP%, VIS%
  ST% = VR%
  IF ST% < TOP% THEN ST% = TOP%
  FOR R% = ST% TO TOP% + VIS% - 1
    CALL DrawRow(R%)
  NEXT
END SUB

SUB SeedDemo
  SHARED CUR%
  S$ = "WORDXT is a fast text-mode word processor tuned for a 4.77 MHz XT."
  S$ = S$ + CHR$(13)
  S$ = S$ + "It draws text with the video character ROM instead of a software font, so typing feels instant."
  S$ = S$ + CHR$(13)
  S$ = S$ + "Word-wrap happens as you type, and only the line you changed is redrawn."
  S$ = S$ + CHR$(13) + CHR$(13)
  S$ = S$ + "Use the arrow keys, Home, End, Backspace and Del. Press ESC to quit."
  FOR I% = 1 TO LEN(S$)
    CALL InsAt(ASC(MID$(S$, I%, 1)))
  NEXT
  CUR% = 0
END SUB

DEF FNS2$(N%)
  T$ = STR$(N%)
  IF LEFT$(T$, 1) = " " THEN T$ = MID$(T$, 2)
  FNS2$ = T$
END DEF
