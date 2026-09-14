' WORD.BAS - Graphical WYSIWYG word processor (SCREEN 7)
' Carries its own 8x8 bitmap font (PRINT is a no-op in graphics mode).
' Font convention: each glyph row byte is MSB-left (bit 7 = leftmost pixel).

' ---- geometry ----
PGX% = 40 : PGY% = 16 : TW% = 28 : TR% = 21

' ---- data model: one flat array of ASCII codes; 13 = hard paragraph break ----
MAXCH% = 1200
DIM CH%(MAXCH%)
DLEN% = 0 : CUR% = 0

' ---- reflow output ----
DIM ROWSTART%(TR% + 2)
NROWS% = 0 : CROW% = 0 : CCOL% = 0 : DESCOL% = -1

' ---- font ----
DIM GLY%(127, 7)
DIM MASK%(7)

' ---- flags ----
QUIT% = 0 : REDRAW% = 0 : BLINK% = 0 : FRAME% = 0

SCREEN 7
CALL InitMasks
CALL LoadFont
CALL SeedDemo
CALL DrawChrome
CALL Reflow
CALL DrawText
CALL DrawStatus
BLINK% = 1 : CALL DrawCaret

DO
  ' Keep FRAME% bounded so the free-running blink counter never overflows
  ' a 16-bit integer (% maxes at 32767) during a long idle session.
  FRAME% = (FRAME% + 1) AND 4095
  IF FRAME% = 0 THEN
    BLINK% = 1 - BLINK%
    CALL DrawCaret
  END IF
  K$ = INKEY$
  IF K$ <> "" THEN
    CALL HandleKey(K$)
    IF REDRAW% = 1 THEN
      CALL Reflow
      CALL DrawText
    END IF
    CALL DrawStatus
    BLINK% = 1
    CALL DrawCaret
  END IF
LOOP UNTIL QUIT% = 1

SCREEN 0
CLS
END

SUB InitMasks
  SHARED MASK%()
  MASK%(0) = 128 : MASK%(1) = 64 : MASK%(2) = 32 : MASK%(3) = 16
  MASK%(4) = 8 : MASK%(5) = 4 : MASK%(6) = 2 : MASK%(7) = 1
END SUB

SUB LoadFont
  SHARED GLY%()
  FOR I% = 0 TO 127
    FOR R% = 0 TO 7
      GLY%(I%, R%) = 0
    NEXT
  NEXT
  RESTORE FontData
  READ N%
  FOR G% = 1 TO N%
    READ A%
    FOR R% = 0 TO 7
      READ B%
      GLY%(A%, R%) = B%
    NEXT
  NEXT
END SUB

SUB DrawGlyph(GC%, COL%, ROW%, INK%, PAP%)
  SHARED GLY%(), MASK%(), PGX%, PGY%
  PX% = PGX% + COL% * 8
  PY% = PGY% + ROW% * 8
  LINE (PX%, PY%)-(PX% + 7, PY% + 7), PAP%, BF
  IF GC% < 32 OR GC% > 126 THEN EXIT SUB
  FOR R% = 0 TO 7
    B% = GLY%(GC%, R%)
    IF B% <> 0 THEN
      X0% = -1
      FOR BIT% = 0 TO 7
        ONP% = 0
        IF (B% AND MASK%(BIT%)) <> 0 THEN ONP% = 1
        IF ONP% = 1 AND X0% < 0 THEN X0% = BIT%
        IF ONP% = 0 AND X0% >= 0 THEN
          LINE (PX% + X0%, PY% + R%)-(PX% + BIT% - 1, PY% + R%), INK%, BF
          X0% = -1
        END IF
      NEXT
      IF X0% >= 0 THEN LINE (PX% + X0%, PY% + R%)-(PX% + 7, PY% + R%), INK%, BF
    END IF
  NEXT
END SUB

SUB DrawBarGlyph(GC%, PXA%, PYA%, INK%, PAP%)
  SHARED GLY%(), MASK%()
  LINE (PXA%, PYA%)-(PXA% + 7, PYA% + 7), PAP%, BF
  IF GC% < 32 OR GC% > 126 THEN EXIT SUB
  FOR R% = 0 TO 7
    B% = GLY%(GC%, R%)
    IF B% <> 0 THEN
      X0% = -1
      FOR BIT% = 0 TO 7
        ONP% = 0
        IF (B% AND MASK%(BIT%)) <> 0 THEN ONP% = 1
        IF ONP% = 1 AND X0% < 0 THEN X0% = BIT%
        IF ONP% = 0 AND X0% >= 0 THEN
          LINE (PXA% + X0%, PYA% + R%)-(PXA% + BIT% - 1, PYA% + R%), INK%, BF
          X0% = -1
        END IF
      NEXT
      IF X0% >= 0 THEN LINE (PXA% + X0%, PYA% + R%)-(PXA% + 7, PYA% + R%), INK%, BF
    END IF
  NEXT
END SUB

SUB DrawBarText(S$, PXA%, PYA%, INK%, PAP%)
  FOR I% = 1 TO LEN(S$)
    CALL DrawBarGlyph(ASC(MID$(S$, I%, 1)), PXA% + (I% - 1) * 8, PYA%, INK%, PAP%)
  NEXT
END SUB

SUB DrawChrome
  LINE (0, 0)-(319, 199), 7, BF
  LINE (0, 0)-(319, 7), 1, BF
  CALL DrawBarText("WORD.BAS  ESC=Quit", 0, 0, 15, 1)
  LINE (34, 12)-(289, 193), 8, BF
  LINE (32, 10)-(287, 191), 15, BF
  LINE (0, 192)-(319, 199), 1, BF
END SUB

' Reflow walks the buffer once, building ROWSTART%(r) for each wrapped row.
' During the walk RIDX% is the CURRENT 0-based row index; on exit we store the
' ROW COUNT in NROWS% ( = RIDX% + 1 ). ROWSTART% is clamped to row TR% so a long
' document never indexes past the array; rows beyond TR% are not drawn.
SUB Reflow
  SHARED CH%(), ROWSTART%(), DLEN%, CUR%, TW%, TR%, NROWS%, CROW%, CCOL%
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
      IF COL% > TW% THEN
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

SUB DrawText
  SHARED CH%(), ROWSTART%(), DLEN%, PGX%, PGY%, TW%, TR%, NROWS%, REDRAW%
  LINE (PGX%, PGY%)-(PGX% + TW% * 8 - 1, PGY% + TR% * 8 - 1), 15, BF
  RDRAW% = NROWS%
  IF RDRAW% > TR% THEN RDRAW% = TR%
  FOR R% = 0 TO RDRAW% - 1
    RS% = ROWSTART%(R%)
    IF R% < NROWS% - 1 THEN
      RE% = ROWSTART%(R% + 1)
    ELSE
      RE% = DLEN%
    END IF
    COL% = 0
    FOR J% = RS% TO RE% - 1
      C% = CH%(J%)
      IF C% <> 13 THEN
        IF COL% < TW% THEN
          CALL DrawGlyph(C%, COL%, R%, 0, 15)
          COL% = COL% + 1
        END IF
      END IF
    NEXT
  NEXT
  REDRAW% = 0
END SUB

SUB DrawStatus
  SHARED CROW%, CCOL%
  LINE (0, 192)-(319, 199), 1, BF
  T$ = "Ln " + FNStr2$(CROW% + 1) + "  Col " + FNStr2$(CCOL% + 1)
  CALL DrawBarText(T$, 4, 192, 15, 1)
END SUB

SUB DrawCaret
  SHARED CROW%, CCOL%, PGX%, PGY%, TR%, BLINK%
  IF CROW% >= TR% THEN EXIT SUB
  CX% = PGX% + CCOL% * 8
  CY% = PGY% + CROW% * 8
  IF BLINK% = 1 THEN
    LINE (CX%, CY%)-(CX%, CY% + 7), 0, BF
  ELSE
    LINE (CX%, CY%)-(CX%, CY% + 7), 15, BF
  END IF
END SUB

SUB InsAt(CODE%)
  SHARED CH%(), DLEN%, CUR%, MAXCH%, REDRAW%
  IF DLEN% >= MAXCH% THEN EXIT SUB
  FOR J% = DLEN% TO CUR% + 1 STEP -1
    CH%(J%) = CH%(J% - 1)
  NEXT
  CH%(CUR%) = CODE%
  DLEN% = DLEN% + 1
  CUR% = CUR% + 1
  REDRAW% = 1
END SUB

SUB DelAt(PP%)
  SHARED CH%(), DLEN%, REDRAW%
  IF PP% < 0 OR PP% >= DLEN% THEN EXIT SUB
  FOR J% = PP% TO DLEN% - 2
    CH%(J%) = CH%(J% + 1)
  NEXT
  DLEN% = DLEN% - 1
  REDRAW% = 1
END SUB

SUB MoveVert(DIR%)
  SHARED CH%(), ROWSTART%(), DLEN%, CUR%, NROWS%, CROW%, CCOL%, DESCOL%, REDRAW%
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
  REDRAW% = 1
END SUB

SUB MoveHome
  SHARED ROWSTART%(), CUR%, CROW%, DESCOL%, REDRAW%
  CALL Reflow
  CUR% = ROWSTART%(CROW%)
  DESCOL% = -1
  REDRAW% = 1
END SUB

SUB MoveEnd
  SHARED CH%(), ROWSTART%(), DLEN%, CUR%, NROWS%, CROW%, DESCOL%, REDRAW%
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
  REDRAW% = 1
END SUB

SUB HandleKey(K$)
  SHARED CUR%, DLEN%, REDRAW%, DESCOL%, QUIT%
  IF LEN(K$) = 2 AND ASC(K$) = 0 THEN
    EC% = ASC(MID$(K$, 2, 1))
    SELECT CASE EC%
      CASE 75
        IF CUR% > 0 THEN CUR% = CUR% - 1
        REDRAW% = 1 : DESCOL% = -1
      CASE 77
        IF CUR% < DLEN% THEN CUR% = CUR% + 1
        REDRAW% = 1 : DESCOL% = -1
      CASE 72
        CALL MoveVert(-1)
      CASE 80
        CALL MoveVert(1)
      CASE 71
        CALL MoveHome
      CASE 79
        CALL MoveEnd
      CASE 83
        IF CUR% < DLEN% THEN CALL DelAt(CUR%)
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
        END IF
      CASE 13
        CALL InsAt(13)
      CASE 9
        CALL InsAt(32)
        CALL InsAt(32)
      CASE 32 TO 126
        CALL InsAt(C%)
    END SELECT
    DESCOL% = -1
  END IF
END SUB

SUB SeedDemo
  SHARED CUR%
  S$ = "The quick brown fox."
  FOR I% = 1 TO LEN(S$)
    CALL InsAt(ASC(MID$(S$, I%, 1)))
  NEXT
  CUR% = 0
END SUB

DEF FNStr2$(N%)
  T$ = STR$(N%)
  IF LEFT$(T$, 1) = " " THEN T$ = MID$(T$, 2)
  FNStr2$ = T$
END DEF

FontData:
    DATA 74
    DATA 32,0,0,0,0,0,0,0,0
    DATA 33,24,60,60,24,24,0,24,0
    DATA 34,108,108,0,0,0,0,0,0
    DATA 39,48,48,96,0,0,0,0,0
    DATA 40,24,48,96,96,96,48,24,0
    DATA 41,96,48,24,24,24,48,96,0
    DATA 44,0,0,0,0,0,112,112,96
    DATA 45,0,0,0,252,0,0,0,0
    DATA 46,0,0,0,0,0,112,112,0
    DATA 48,124,198,206,222,246,230,124,0
    DATA 49,48,112,48,48,48,48,252,0
    DATA 50,120,204,12,56,96,204,252,0
    DATA 51,120,204,12,56,12,204,120,0
    DATA 52,28,60,108,204,254,12,30,0
    DATA 53,252,192,248,12,12,204,120,0
    DATA 54,56,96,192,248,204,204,120,0
    DATA 55,252,204,12,24,48,48,48,0
    DATA 56,120,204,204,120,204,204,120,0
    DATA 57,120,204,204,124,12,24,112,0
    DATA 58,0,112,112,0,112,112,0,0
    DATA 59,0,112,112,0,112,112,96,0
    DATA 63,120,204,12,24,48,0,48,0
    DATA 65,48,120,204,204,252,204,204,0
    DATA 66,252,102,102,124,102,102,252,0
    DATA 67,60,102,192,192,192,102,60,0
    DATA 68,248,108,102,102,102,108,248,0
    DATA 69,254,98,104,120,104,98,254,0
    DATA 70,254,98,104,120,104,96,240,0
    DATA 71,60,102,192,192,206,102,62,0
    DATA 72,204,204,204,252,204,204,204,0
    DATA 73,120,48,48,48,48,48,120,0
    DATA 74,30,12,12,12,204,204,120,0
    DATA 75,230,102,108,120,108,102,230,0
    DATA 76,240,96,96,96,98,102,254,0
    DATA 77,198,238,254,254,214,198,198,0
    DATA 78,198,230,246,222,206,198,198,0
    DATA 79,56,108,198,198,198,108,56,0
    DATA 80,252,102,102,124,96,96,240,0
    DATA 81,120,204,204,204,220,120,28,0
    DATA 82,252,102,102,124,108,102,230,0
    DATA 83,120,204,224,112,28,204,120,0
    DATA 84,252,180,48,48,48,48,120,0
    DATA 85,204,204,204,204,204,204,252,0
    DATA 86,204,204,204,204,204,120,48,0
    DATA 87,198,198,198,214,254,238,198,0
    DATA 88,198,198,108,56,56,108,198,0
    DATA 89,204,204,204,120,48,48,120,0
    DATA 90,254,198,140,24,50,102,254,0
    DATA 97,0,0,120,12,124,204,118,0
    DATA 98,224,96,96,124,102,102,220,0
    DATA 99,0,0,120,204,192,204,120,0
    DATA 100,28,12,12,124,204,204,118,0
    DATA 101,0,0,120,204,252,192,120,0
    DATA 102,56,108,96,240,96,96,240,0
    DATA 103,0,0,118,204,204,124,12,248
    DATA 104,224,96,108,118,102,102,230,0
    DATA 105,48,0,112,48,48,48,120,0
    DATA 106,12,0,12,12,12,204,204,120
    DATA 107,224,96,102,108,120,108,230,0
    DATA 108,112,48,48,48,48,48,120,0
    DATA 109,0,0,204,254,254,214,198,0
    DATA 110,0,0,248,204,204,204,204,0
    DATA 111,0,0,120,204,204,204,120,0
    DATA 112,0,0,220,102,102,124,96,240
    DATA 113,0,0,118,204,204,124,12,30
    DATA 114,0,0,220,118,102,96,240,0
    DATA 115,0,0,124,192,120,12,248,0
    DATA 116,16,48,124,48,48,52,24,0
    DATA 117,0,0,204,204,204,204,118,0
    DATA 118,0,0,204,204,204,120,48,0
    DATA 119,0,0,198,214,254,254,108,0
    DATA 120,0,0,198,108,56,108,198,0
    DATA 121,0,0,204,204,204,124,12,248
    DATA 122,0,0,252,152,48,100,252,0
