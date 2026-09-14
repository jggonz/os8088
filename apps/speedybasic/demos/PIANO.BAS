' PIANO.BAS - a graphical, playable piano (SCREEN 9, EGA 640x350).
'
'   White keys:  A S D F G H J K   ->  C D E F G A B C
'   Black keys:    W E   T Y U     ->  C# D#  F# G# A#
'
' Play live, or press a demo:  1 = Twinkle Twinkle,  2 = Mary Had a Little
' Lamb,  3 = Ode to Joy.  R replays the current song,  N clears it,  ESC quits.
' Notes scroll across the roll above the keyboard as they sound. Graphics mode
' has no text, so a tiny 5x7 bitmap font (built below) draws everything.

DIM GLYPH%(26, 6), CHARS$
DIM FREQ%(12), NM$(12)
DIM WHITEN%(7), WHITEK$(7), WHITEX%(7)
DIM BLACKN%(4), BLACKK$(4), BLACKX%(4)
DIM SONG%(500)
DIM S1%(41), S2%(25), S3%(29)

' --- 5x7 bitmap font (key labels, note names and the menu words) ---
CHARS$ = "ABCDEFGHJKSTUWY#+RNILMOP123"
FOR G% = 0 TO 26 : FOR R% = 0 TO 6 : READ GLYPH%(G%, R%) : NEXT : NEXT
DATA 14,17,17,31,17,17,17
DATA 30,17,30,17,17,17,30
DATA 15,16,16,16,16,16,15
DATA 30,17,17,17,17,17,30
DATA 31,16,16,30,16,16,31
DATA 31,16,16,30,16,16,16
DATA 15,16,16,23,17,17,14
DATA 17,17,17,31,17,17,17
DATA 7,2,2,2,18,18,12
DATA 17,18,20,24,20,18,17
DATA 15,16,16,14,1,1,30
DATA 31,4,4,4,4,4,4
DATA 17,17,17,17,17,17,14
DATA 17,17,17,21,21,27,17
DATA 17,17,10,4,4,4,4
DATA 10,10,31,10,31,10,10
DATA 0,4,4,31,4,4,0
DATA 30,17,30,20,18,17,17
DATA 17,25,21,21,19,17,17
DATA 14,4,4,4,4,4,14
DATA 16,16,16,16,16,16,31
DATA 17,27,21,21,17,17,17
DATA 14,17,17,17,17,17,14
DATA 30,17,17,30,16,16,16
DATA 4,12,4,4,4,4,14
DATA 14,17,1,2,4,8,31
DATA 30,1,1,14,1,1,30

' --- chromatic note table: C (262 Hz) up to the C an octave higher ---
FOR I% = 0 TO 12 : READ FREQ%(I%) : NEXT
DATA 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, 523
FOR I% = 0 TO 12 : READ NM$(I%) : NEXT
DATA "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C+"

' --- white keys: chromatic index, the computer key, and screen X ---
FOR I% = 0 TO 7 : READ WHITEN%(I%) : NEXT
DATA 0, 2, 4, 5, 7, 9, 11, 12
FOR I% = 0 TO 7 : READ WHITEK$(I%) : NEXT
DATA "A", "S", "D", "F", "G", "H", "J", "K"
FOR I% = 0 TO 7 : WHITEX%(I%) = 20 + I% * 78 : NEXT

' --- black keys: chromatic index, key, and X ---
FOR I% = 0 TO 4 : READ BLACKN%(I%) : NEXT
DATA 1, 3, 6, 8, 10
FOR I% = 0 TO 4 : READ BLACKK$(I%) : NEXT
DATA "W", "E", "T", "Y", "U"
FOR I% = 0 TO 4 : READ BSLOT% : BLACKX%(I%) = 20 + BSLOT% * 78 + 52 : NEXT
DATA 0, 1, 3, 4, 5

' --- demo songs (note indices into the chromatic table) ---
L1% = 42 : L2% = 26 : L3% = 30
FOR I% = 0 TO L1% - 1 : READ S1%(I%) : NEXT
' Twinkle Twinkle Little Star (full)
DATA 0, 0, 7, 7, 9, 9, 7, 5, 5, 4, 4, 2, 2, 0
DATA 7, 7, 5, 5, 4, 4, 2, 7, 7, 5, 5, 4, 4, 2
DATA 0, 0, 7, 7, 9, 9, 7, 5, 5, 4, 4, 2, 2, 0
FOR I% = 0 TO L2% - 1 : READ S2%(I%) : NEXT
' Mary Had a Little Lamb (full)
DATA 4, 2, 0, 2, 4, 4, 4, 2, 2, 2, 4, 7, 7
DATA 4, 2, 0, 2, 4, 4, 4, 4, 2, 2, 4, 2, 0
FOR I% = 0 TO L3% - 1 : READ S3%(I%) : NEXT
' Ode to Joy (Beethoven, main theme)
DATA 4, 4, 5, 7, 7, 5, 4, 2, 0, 0, 2, 4, 4, 2, 2
DATA 4, 4, 5, 7, 7, 5, 4, 2, 0, 0, 2, 4, 2, 0, 0

NSONG% = 0

SCREEN 9
CLS
CALL DrawMenu
CALL DrawPiano
CALL ClearRoll

DO
  DELAY 0.01
  K$ = INKEY$
  IF K$ <> "" THEN
    UK$ = UCASE$(K$)
    IF K$ = CHR$(27) THEN
      EXIT LOOP
    ELSEIF UK$ = "1" THEN
      CALL PlayDemo(1)
    ELSEIF UK$ = "2" THEN
      CALL PlayDemo(2)
    ELSEIF UK$ = "3" THEN
      CALL PlayDemo(3)
    ELSEIF UK$ = "R" THEN
      CALL Replay
    ELSEIF UK$ = "N" THEN
      CALL ClearSong
    ELSE
      NI% = -1
      FOR I% = 0 TO 7
        IF UK$ = WHITEK$(I%) THEN NI% = WHITEN%(I%)
      NEXT
      FOR I% = 0 TO 4
        IF UK$ = BLACKK$(I%) THEN NI% = BLACKN%(I%)
      NEXT
      IF NI% >= 0 THEN
        CALL AddNote(NI%)
        CALL PlayCurrent(NSONG% - 1)
      END IF
    END IF
  END IF
LOOP

SCREEN 0 : WIDTH 80 : CLS
PRINT "Thanks for playing!"
END

' === 5x7 font: draw one glyph, then a whole string ===

SUB DrawChar(PX%, PY%, GI%, COL%)
  SHARED GLYPH%()
  FOR RY% = 0 TO 6
    BITS% = GLYPH%(GI%, RY%)
    BV% = 16
    FOR CX% = 0 TO 4
      IF (BITS% AND BV%) <> 0 THEN PSET (PX% + CX%, PY% + RY%), COL%
      BV% = BV% \\ 2
    NEXT
  NEXT
END SUB

SUB DrawString(PX%, PY%, S$, COL%)
  SHARED CHARS$
  FOR I% = 1 TO LEN(S$)
    GI% = INSTR(CHARS$, MID$(S$, I%, 1)) - 1
    IF GI% >= 0 THEN CALL DrawChar(PX% + (I% - 1) * 6, PY%, GI%, COL%)
  NEXT
END SUB

' === top menu bar ===

SUB DrawMenu
  LINE (0, 12)-(639, 12), 8
  CALL DrawString(8, 3, "1 TWINKLE", 14)
  CALL DrawString(140, 3, "2 MARY", 11)
  CALL DrawString(248, 3, "3 ODE", 10)
  CALL DrawString(350, 3, "R REPLAY", 13)
  CALL DrawString(470, 3, "N NEW", 7)
  CALL DrawString(560, 3, "ESC", 4)
END SUB

' === keyboard ===

SUB DrawWhite(IDX%, HIL%)
  SHARED WHITEX%(), WHITEK$()
  X% = WHITEX%(IDX%)
  C% = 15
  IF HIL% = 1 THEN C% = 14
  LINE (X%, 240)-(X% + 72, 344), C%, BF
  LINE (X%, 240)-(X% + 72, 344), 0, B
  CALL DrawString(X% + 31, 322, WHITEK$(IDX%), 0)
END SUB

SUB DrawBlack(IDX%, HIL%)
  SHARED BLACKX%(), BLACKK$()
  X% = BLACKX%(IDX%)
  C% = 8
  IF HIL% = 1 THEN C% = 12
  LINE (X%, 240)-(X% + 28, 304), C%, BF
  LINE (X%, 240)-(X% + 28, 304), 7, B
  CALL DrawString(X% + 12, 288, BLACKK$(IDX%), 15)
END SUB

SUB DrawBlacks
  FOR I% = 0 TO 4
    CALL DrawBlack(I%, 0)
  NEXT
END SUB

SUB DrawPiano
  FOR I% = 0 TO 7
    CALL DrawWhite(I%, 0)
  NEXT
  CALL DrawBlacks
END SUB

' === scrolling note roll ===

SUB ClearRoll
  LINE (0, 14)-(639, 232), 0, BF
END SUB

' Redraw the roll as a scrolling piano roll: the note just played (index K%) sits
' at a fixed "play head" near the right and older notes trail off to the left, so
' the whole roll slides left as each new note sounds and old ones scroll away.
SUB DrawRollUpTo(K%)
  SHARED SONG%(), NM$()
  CALL ClearRoll
  LO% = K% - 17
  IF LO% < 0 THEN LO% = 0
  FOR J% = LO% TO K%
    PX% = 580 - (K% - J%) * 34
    IF PX% >= 0 THEN
      NI% = SONG%(J%)
      LY% = 220 - NI% * 14
      COL% = (NI% MOD 6) + 9
      LINE (PX%, LY%)-(PX% + 28, LY% + 12), COL%, BF
      CALL DrawString(PX% + 4, LY% - 9, NM$(NI%), 15)
    END IF
  NEXT
END SUB

' === recording ===

SUB AddNote(NI%)
  SHARED SONG%(), NSONG%
  IF NSONG% < 500 THEN
    SONG%(NSONG%) = NI%
    NSONG% = NSONG% + 1
  END IF
END SUB

SUB ClearSong
  SHARED NSONG%
  NSONG% = 0
  CALL ClearRoll
END SUB

' === play one recorded note: scroll the roll to it, light its key, sound it ===

SUB PlayCurrent(K%)
  SHARED SONG%(), FREQ%(), WHITEN%(), BLACKN%()
  IF K% < 0 THEN EXIT SUB
  CALL DrawRollUpTo(K%)
  NI% = SONG%(K%)
  WI% = -1
  BI% = -1
  FOR I% = 0 TO 7
    IF WHITEN%(I%) = NI% THEN WI% = I%
  NEXT
  FOR I% = 0 TO 4
    IF BLACKN%(I%) = NI% THEN BI% = I%
  NEXT
  IF WI% >= 0 THEN
    CALL DrawWhite(WI%, 1)
    CALL DrawBlacks
  END IF
  IF BI% >= 0 THEN CALL DrawBlack(BI%, 1)
  SOUND FREQ%(NI%), 4
  IF WI% >= 0 THEN
    CALL DrawWhite(WI%, 0)
    CALL DrawBlacks
  END IF
  IF BI% >= 0 THEN CALL DrawBlack(BI%, 0)
END SUB

' === replay the current song, and the three demo songs ===

SUB Replay
  SHARED NSONG%
  FOR K% = 0 TO NSONG% - 1
    CALL PlayCurrent(K%)
    DELAY 0.06
  NEXT
END SUB

SUB PlayDemo(NUM%)
  SHARED S1%(), S2%(), S3%(), L1%, L2%, L3%, NSONG%
  CALL ClearSong
  IF NUM% = 1 THEN
    FOR J% = 0 TO L1% - 1
      CALL AddNote(S1%(J%))
      CALL PlayCurrent(NSONG% - 1)
      DELAY 0.06
    NEXT
  ELSEIF NUM% = 2 THEN
    FOR J% = 0 TO L2% - 1
      CALL AddNote(S2%(J%))
      CALL PlayCurrent(NSONG% - 1)
      DELAY 0.06
    NEXT
  ELSE
    FOR J% = 0 TO L3% - 1
      CALL AddNote(S3%(J%))
      CALL PlayCurrent(NSONG% - 1)
      DELAY 0.06
    NEXT
  END IF
END SUB
