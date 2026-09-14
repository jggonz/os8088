' LANDER.BAS - a Lunar Lander built around real physics.
'
' Physics: gravity pulls the craft down every tick; the thrusters add the
' opposite acceleration while you have fuel; momentum carries you (you drift).
' Land on the flat pad slowly and roughly level to win.
'
' The 8088 in an XT has NO floating-point unit and Turbo Basic's single-
' precision math is software-emulated and slow, so all motion is done in
' FIXED-POINT integers: positions and velocities are scaled by 256 and stored
' in LONG (&) variables, integrated with plain integer add. Only ONE small
' sprite moves; it flies over a black sky, so erasing it is just a black
' redraw - no full-screen repaint per frame. That is what keeps it fast.
'
' Controls:  Up arrow / Space = main thruster   Left/Right = side thrusters
'            ESC = quit.   After a landing or crash, any key replays.

GRAV& = 6 : THR& = 20 : SIDE& = 11
LVY& = 540 : LVX& = 470
DIM TERR%(320)
PADX1% = 0 : PADX2% = 0 : PADY% = 0

' Score + level, shown top-of-screen via a tiny 3x5 digit font (graphics mode
' has no text). Points are awarded for each successful landing.
SCORE& = 0 : LEVEL& = 0
DIM DIGIT%(9, 4)
FOR DG% = 0 TO 9 : FOR DR% = 0 TO 4 : READ DIGIT%(DG%, DR%) : NEXT : NEXT
DATA 7,5,5,5,7
DATA 2,6,2,2,7
DATA 7,1,7,4,7
DATA 7,1,7,1,7
DATA 5,5,7,1,1
DATA 7,4,7,1,7
DATA 7,4,7,5,7
DATA 7,1,2,2,2
DATA 7,5,7,5,7
DATA 7,5,7,1,7

RANDOMIZE TIMER
SCREEN 7

DO
  CALL BuildTerrain
  CALL DrawScene
  X& = 40 * 256 : Y& = 26 * 256 : VX& = 130 : VY& = 0
  FUEL% = 500 : OLDTH% = 0 : STATE% = 0 : FRAME% = 0
  LX% = INT(X& / 256) : LY% = INT(Y& / 256)
  CALL DrawHUD
  CALL DrawLander(LX%, LY%, 1)

  DO
    FT! = TIMER
    FRAME% = (FRAME% + 1) AND 1023
    CALL DrawLander(LX%, LY%, 0)
    IF OLDTH% = 1 THEN CALL DrawFlame(LX%, LY%, 0)

    UP% = 0 : LT% = 0 : RT% = 0
    K$ = INKEY$
    IF K$ <> "" THEN
      IF K$ = CHR$(27) THEN STATE% = 3
      IF K$ = " " THEN UP% = 1
      IF LEN(K$) = 2 AND ASC(K$) = 0 THEN
        EC% = ASC(MID$(K$, 2, 1))
        IF EC% = 72 THEN UP% = 1
        IF EC% = 75 THEN LT% = 1
        IF EC% = 77 THEN RT% = 1
      END IF
    END IF

    THRUSTING% = 0
    IF UP% = 1 AND FUEL% > 0 THEN
      VY& = VY& - THR& : FUEL% = FUEL% - 2 : THRUSTING% = 1
    END IF
    IF LT% = 1 AND FUEL% > 0 THEN
      VX& = VX& - SIDE& : FUEL% = FUEL% - 1 : THRUSTING% = 1
    END IF
    IF RT% = 1 AND FUEL% > 0 THEN
      VX& = VX& + SIDE& : FUEL% = FUEL% - 1 : THRUSTING% = 1
    END IF
    IF THRUSTING% = 1 THEN SOUND 90, 0.5

    ' --- integrate fixed-point motion ---
    VY& = VY& + GRAV&
    ' Terminal velocity (both directions). Caps speed for gameplay, guarantees
    ' the craft never tunnels through the thin ground in one tick, AND keeps the
    ' velocity magnitudes the HUD integer-divides safely inside 16 bits.
    IF VY& > 3000 THEN VY& = 3000
    IF VY& < -3000 THEN VY& = -3000
    IF VX& > 3000 THEN VX& = 3000
    IF VX& < -3000 THEN VX& = -3000
    X& = X& + VX&
    Y& = Y& + VY&
    IF X& < 0 THEN
      X& = 0 : VX& = 0
    END IF
    IF X& > 78592 THEN
      X& = 78592 : VX& = 0
    END IF
    IF Y& < 4096 THEN
      Y& = 4096
      IF VY& < 0 THEN VY& = 0
    END IF
    ' Extract screen pixels with INT and floating divide, NOT integer-divide.
    ' Real Turbo Basic integer division works on 16-bit operands, so dividing
    ' Y& (which exceeds 32767 once the craft is low on screen, around y=128)
    ' overflows: LY% turns negative, the lander is drawn off-screen and never
    ' collides. INT(Y& / 256) handles the full LONG range the same on web and TB.
    LX% = INT(X& / 256)
    LY% = INT(Y& / 256)

    ' --- ground / pad collision ---
    CXP% = LX% + 6
    IF CXP% > 319 THEN CXP% = 319
    IF CXP% < 0 THEN CXP% = 0
    GY% = TERR%(CXP%)
    IF LY% + 12 >= GY% THEN
      M& = VY&
      IF M& < 0 THEN M& = -M&
      N& = VX&
      IF N& < 0 THEN N& = -N&
      LY% = GY% - 12
      IF CXP% >= PADX1% AND CXP% <= PADX2% AND M& <= LVY& AND N& <= LVX& THEN
        STATE% = 1
      ELSE
        STATE% = 2
      END IF
    END IF

    CALL DrawHUD
    IF THRUSTING% = 1 THEN CALL DrawFlame(LX%, LY%, 1)
    CALL DrawLander(LX%, LY%, 1)
    OLDTH% = THRUSTING%
    ' CPU-INDEPENDENT FRAME PACING. Turbo Basic's DELAY has ~1/18.2s (0.055s)
    ' timer-tick granularity, so a sub-tick DELAY (e.g. 0.02) rounds to ZERO and
    ' the loop runs at raw CPU speed - tolerable on an XT, but a fast 386/486
    ' makes the craft plummet and vanish in milliseconds. Instead, measure how
    ' long this frame already took and sleep only the remainder of a fixed 0.055s
    ' budget, so the game runs at a steady ~18 fps on any machine.
    EL! = TIMER - FT!
    IF EL! < 0 THEN EL! = 0
    WT! = 0.055 - EL!
    IF WT! > 0 THEN DELAY WT!
  LOOP UNTIL STATE% <> 0

  IF STATE% = 2 THEN CALL Explode(LX%, LY%)
  IF STATE% = 3 THEN EXIT LOOP

  IF STATE% = 1 THEN
    ' WON: award points (landing bonus + leftover fuel + a softness bonus for a
    ' gentle touchdown + a per-level bonus), bump the level, and show the new
    ' score. Then play the victory tune, drain any thrust keys still held from
    ' the touchdown (so the next craft does not auto-fire), pause a beat, and
    ' roll straight into the next level - no keypress needed (the old code waited
    ' silently in graphics mode, which looked like the game had stopped).
    LEVEL& = LEVEL& + 1
    SOFT& = 540 - M&
    IF SOFT& < 0 THEN SOFT& = 0
    SCORE& = SCORE& + 100 + FUEL% + SOFT& + LEVEL& * 50
    CALL DrawStats
    PLAY "T160O2L8CEGO3CEC"
    DO
      KZ$ = INKEY$
    LOOP UNTIL KZ$ = ""
    DELAY 1
  ELSE
    ' CRASHED: wait for a keypress to retry (ESC quits).
    K$ = ""
    DO
      K$ = INKEY$
      DELAY 0.05
    LOOP UNTIL K$ <> ""
    IF K$ = CHR$(27) THEN EXIT LOOP
  END IF
LOOP

SCREEN 0
END

' ===================== terrain =====================

SUB BuildTerrain
  SHARED TERR%(), PADX1%, PADX2%, PADY%
  H% = 165
  FOR X% = 0 TO 319
    H% = H% + INT(RND * 5) - 2
    IF H% < 150 THEN H% = 150
    IF H% > 190 THEN H% = 190
    TERR%(X%) = H%
  NEXT
  ' Randomise the landing pad each level: a flat ledge somewhere across the
  ' surface, kept clear of the far edges. Its height follows the local terrain.
  PADX1% = 70 + INT(RND * 200)
  PADX2% = PADX1% + 34
  PADY% = TERR%(PADX1% + 17)
  FOR X% = PADX1% TO PADX2%
    TERR%(X%) = PADY%
  NEXT
END SUB

SUB DrawScene
  SHARED TERR%(), PADX1%, PADX2%, PADY%
  CLS
  FOR X% = 0 TO 319
    LINE (X%, TERR%(X%))-(X%, 199), 2
  NEXT
  LINE (PADX1%, PADY%)-(PADX2%, PADY%), 14
  LINE (PADX1%, PADY% - 3)-(PADX1%, PADY%), 14
  LINE (PADX2%, PADY% - 3)-(PADX2%, PADY%), 14
  ' HUD frame: three stacked gauges (fuel, descent rate, drift)
  LINE (6, 2)-(112, 24), 8, B
  LINE (6, 9)-(112, 9), 8
  LINE (6, 16)-(112, 16), 8
  CALL DrawStats
END SUB

' ===================== score / level readout (digit font) =====================

SUB DrawStats
  SHARED SCORE&, LEVEL&
  ' clear the top strip the numbers live in (sky, so a black wipe is safe)
  LINE (150, 2)-(318, 13), 0, BF
  CALL DrawNumber(180, 3, LEVEL&, 11)
  CALL DrawNumber(312, 3, SCORE&, 14)
END SUB

SUB DrawDigit(PX%, PY%, D%, C%)
  SHARED DIGIT%()
  FOR RY% = 0 TO 4
    BITS% = DIGIT%(D%, RY%)
    IF (BITS% AND 4) <> 0 THEN LINE (PX%, PY% + RY% * 2)-(PX% + 1, PY% + RY% * 2 + 1), C%, BF
    IF (BITS% AND 2) <> 0 THEN LINE (PX% + 2, PY% + RY% * 2)-(PX% + 3, PY% + RY% * 2 + 1), C%, BF
    IF (BITS% AND 1) <> 0 THEN LINE (PX% + 4, PY% + RY% * 2)-(PX% + 5, PY% + RY% * 2 + 1), C%, BF
  NEXT
END SUB

SUB DrawNumber(PX%, PY%, V&, C%)
  SHARED DIGIT%()
  N& = V&
  IF N& < 0 THEN N& = 0
  RX% = PX%
  DO
    DG% = N& MOD 10
    CALL DrawDigit(RX%, PY%, DG%, C%)
    RX% = RX% - 8
    N& = N& \\ 10
  LOOP WHILE N& > 0
END SUB

' ===================== HUD gauges (no text: PRINT is a no-op in graphics) =====================

SUB DrawHUD
  SHARED FUEL%, VY&, VX&, LVY&, LVX&
  ' fuel (green)
  FL% = FUEL% \\ 5
  IF FL% > 100 THEN FL% = 100
  LINE (9, 4)-(110, 7), 0, BF
  IF FL% > 0 THEN LINE (9, 4)-(9 + FL%, 7), 10, BF
  ' descent rate (green, red when too fast to land)
  M& = VY&
  IF M& < 0 THEN M& = -M&
  VL% = M& \\ 22
  IF VL% > 100 THEN VL% = 100
  CL% = 10
  IF M& > LVY& THEN CL% = 12
  LINE (9, 11)-(110, 14), 0, BF
  IF VL% > 0 THEN LINE (9, 11)-(9 + VL%, 14), CL%, BF
  ' horizontal drift
  N& = VX&
  IF N& < 0 THEN N& = -N&
  XL% = N& \\ 22
  IF XL% > 100 THEN XL% = 100
  CX% = 10
  IF N& > LVX& THEN CX% = 12
  LINE (9, 18)-(110, 21), 0, BF
  IF XL% > 0 THEN LINE (9, 18)-(9 + XL%, 21), CX%, BF
END SUB

' ===================== sprite (draw with MODE 1, erase with MODE 0) =====================

SUB DrawLander(PX%, PY%, MODE%)
  IF MODE% = 1 THEN
    BC% = 7 : WC% = 11 : LC% = 15
  ELSE
    BC% = 0 : WC% = 0 : LC% = 0
  END IF
  LINE (PX% + 3, PY% + 3)-(PX% + 9, PY% + 8), BC%, BF
  LINE (PX% + 4, PY% + 1)-(PX% + 8, PY% + 3), LC%, BF
  PSET (PX% + 5, PY% + 5), WC%
  PSET (PX% + 6, PY% + 5), WC%
  PSET (PX% + 7, PY% + 5), WC%
  LINE (PX% + 3, PY% + 8)-(PX% + 1, PY% + 11), LC%
  LINE (PX% + 9, PY% + 8)-(PX% + 11, PY% + 11), LC%
  LINE (PX%, PY% + 11)-(PX% + 1, PY% + 11), LC%
  LINE (PX% + 11, PY% + 11)-(PX% + 12, PY% + 11), LC%
END SUB

SUB DrawFlame(PX%, PY%, MODE%)
  SHARED FRAME%
  IF MODE% = 0 THEN
    LINE (PX% + 4, PY% + 9)-(PX% + 8, PY% + 16), 0, BF
    EXIT SUB
  END IF
  FL% = 3
  IF (FRAME% AND 1) = 1 THEN FL% = 6
  LINE (PX% + 5, PY% + 9)-(PX% + 5, PY% + 9 + FL%), 14
  LINE (PX% + 6, PY% + 9)-(PX% + 6, PY% + 10 + FL%), 12
  LINE (PX% + 7, PY% + 9)-(PX% + 7, PY% + 9 + FL%), 14
END SUB

SUB Explode(PX%, PY%)
  CALL DrawLander(PX%, PY%, 0)
  CALL DrawFlame(PX%, PY%, 0)
  FOR RR% = 2 TO 18 STEP 2
    CIRCLE (PX% + 6, PY% + 6), RR%, 12
    SOUND 60, 0.7
    DELAY 0.03
  NEXT
  FOR RR% = 2 TO 18 STEP 2
    CIRCLE (PX% + 6, PY% + 6), RR%, 0
  NEXT
END SUB
