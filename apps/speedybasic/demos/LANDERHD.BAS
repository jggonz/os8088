' LANDERHD.BAS - a high-resolution Lunar Lander (SCREEN 9, EGA 640x350).
'
' A "super" remake of LANDER.BAS. Same honest fixed-point physics, but everything
' is drawn at twice the resolution with real spacecraft detail: a starfield, the
' Earth hanging in the sky, a gradient-shaded moon surface pocked with craters, a
' multi-stage lander (gold-foil descent stage, silver crew cabin, splayed landing
' legs, animated 3-colour exhaust + side-thruster jets) and a proper instrument
' panel drawn with a built-in 5x7 bitmap font.
'
' The trick that keeps it fast AND lets the craft fly over the stars: the sprite
' is NOT erased with a black box (that would punch holes in the starfield).
' Instead, before drawing the craft each frame we GET (save) the background patch
' under its footprint, and next frame we PUT it back - restoring stars, terrain,
' whatever was there. Only one small rectangle is touched per frame.
'
' Physics is integer fixed-point (scaled by 256, stored in LONG &-variables), the
' same technique the original used so it runs identically on a real 8088 in Turbo
' Basic - no floating-point in the hot loop. Frames are paced to a fixed time
' budget via TIMER so the game runs at a steady speed on any machine.
'
' Controls:  Up arrow / Space = main engine    Left / Right = side thrusters
'            ESC = quit.   After a landing or crash, any key continues.

' ---- tunable fixed-point physics (positions/velocities * 256, in LONG) ----
GRAV& = 4 : THR& = 15 : SIDE& = 9
LVY& = 950 : LVX& = 780              ' fastest survivable touchdown speeds
DIM TERR%(640)
PADX1% = 0 : PADX2% = 0 : PADY% = 0

SCORE& = 0 : LEVEL& = 0 : BEST& = 0

' ---- 5x7 bitmap font: glyphs 0-9 then A-Z (5 bits wide, 7 rows) ----
DIM FONT%(35, 6)
FOR GI% = 0 TO 35 : FOR GR% = 0 TO 6 : READ FONT%(GI%, GR%) : NEXT : NEXT
DATA 14,17,19,21,25,17,14
DATA 4,12,4,4,4,4,14
DATA 14,17,1,2,4,8,31
DATA 31,2,4,2,1,17,14
DATA 2,6,10,18,31,2,2
DATA 31,16,30,1,1,17,14
DATA 6,8,16,30,17,17,14
DATA 31,1,2,4,8,8,8
DATA 14,17,17,14,17,17,14
DATA 14,17,17,15,1,2,12
DATA 14,17,17,31,17,17,17
DATA 30,17,17,30,17,17,30
DATA 14,17,16,16,16,17,14
DATA 28,18,17,17,17,18,28
DATA 31,16,16,30,16,16,31
DATA 31,16,16,30,16,16,16
DATA 14,17,16,23,17,17,15
DATA 17,17,17,31,17,17,17
DATA 14,4,4,4,4,4,14
DATA 7,2,2,2,2,18,12
DATA 17,18,20,24,20,18,17
DATA 16,16,16,16,16,16,31
DATA 17,27,21,21,17,17,17
DATA 17,17,25,21,19,17,17
DATA 14,17,17,17,17,17,14
DATA 30,17,17,30,16,16,16
DATA 14,17,17,17,21,18,13
DATA 30,17,17,30,20,18,17
DATA 15,16,16,14,1,1,30
DATA 31,4,4,4,4,4,4
DATA 17,17,17,17,17,17,14
DATA 17,17,17,17,17,10,4
DATA 17,17,17,21,21,27,17
DATA 17,17,10,4,10,17,17
DATA 17,17,10,4,4,4,4
DATA 31,1,2,4,8,16,31

' ---- sprite background-save buffer (covers craft + flame footprint) ----
DIM SAVE%(740)
SW% = 44 : SH% = 64                  ' save-box width / height
HAVE% = 0                            ' 1 once SAVE% holds a valid patch
OLDX% = 0 : OLDY% = 0

RANDOMIZE TIMER
SCREEN 9

DO
  CALL BuildTerrain
  CALL DrawScene
  X& = 110 * 256 : Y& = 46 * 256 : VX& = 170 : VY& = 0
  FUEL% = 1200 : OLDTH% = 0 : STATE% = 0 : FRAME% = 0
  HAVE% = 0
  LX% = INT(X& / 256) : LY% = INT(Y& / 256)

  DO
    FT! = TIMER
    FRAME% = (FRAME% + 1) AND 1023

    ' ---- read controls ----
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

    ' ---- apply thrust ----
    THRUSTING% = 0
    IF UP% = 1 AND FUEL% > 0 THEN
      VY& = VY& - THR& : FUEL% = FUEL% - 3 : THRUSTING% = 1
    ELSE
      UP% = 0
    END IF
    IF LT% = 1 AND FUEL% > 0 THEN
      VX& = VX& - SIDE& : FUEL% = FUEL% - 2 : THRUSTING% = 1
    ELSE
      LT% = 0
    END IF
    IF RT% = 1 AND FUEL% > 0 THEN
      VX& = VX& + SIDE& : FUEL% = FUEL% - 2 : THRUSTING% = 1
    ELSE
      RT% = 0
    END IF
    IF THRUSTING% = 1 THEN SOUND 90, 0.4

    ' ---- integrate fixed-point motion ----
    VY& = VY& + GRAV&
    IF VY& > 4200 THEN VY& = 4200
    IF VY& < -4200 THEN VY& = -4200
    IF VX& > 4200 THEN VX& = 4200
    IF VX& < -4200 THEN VX& = -4200
    X& = X& + VX&
    Y& = Y& + VY&
    IF X& < 1024 THEN
      X& = 1024 : VX& = 0
    END IF
    IF X& > 153600 THEN
      X& = 153600 : VX& = 0
    END IF
    IF Y& < 10240 THEN
      Y& = 10240
      IF VY& < 0 THEN VY& = 0
    END IF
    ' INT(/256), not 16-bit integer-divide: Y& exceeds 32767 low on screen.
    LX% = INT(X& / 256)
    LY% = INT(Y& / 256)

    ' ---- ground / pad collision (centre column of the craft) ----
    CXP% = LX% + 18
    IF CXP% > 639 THEN CXP% = 639
    IF CXP% < 0 THEN CXP% = 0
    GY% = TERR%(CXP%)
    IF LY% + 42 >= GY% THEN
      M& = VY& : IF M& < 0 THEN M& = -M&
      N& = VX& : IF N& < 0 THEN N& = -N&
      LY% = GY% - 42
      IF CXP% >= PADX1% AND CXP% <= PADX2% AND M& <= LVY& AND N& <= LVX& THEN
        STATE% = 1
      ELSE
        STATE% = 2
      END IF
    END IF

    ' ---- render: restore old patch, save new patch, draw craft ----
    IF HAVE% = 1 THEN PUT (OLDX%, OLDY%), SAVE%, PSET
    GET (LX% - 4, LY% - 3)-(LX% - 4 + SW% - 1, LY% - 3 + SH% - 1), SAVE%
    OLDX% = LX% - 4 : OLDY% = LY% - 3 : HAVE% = 1
    CALL DrawCraft(LX%, LY%)
    IF THRUSTING% = 1 THEN CALL DrawFlame(LX%, LY%, UP%, LT%, RT%)
    CALL DrawHUD
    OLDTH% = THRUSTING%

    ' ---- frame pacing to a fixed time budget ----
    EL! = TIMER - FT!
    IF EL! < 0 THEN EL! = 0
    WT! = 0.05 - EL!
    IF WT! > 0 THEN DELAY WT!
  LOOP UNTIL STATE% <> 0

  IF STATE% = 3 THEN EXIT LOOP

  IF STATE% = 1 THEN
    ' WON: bonus for leftover fuel, a soft touchdown and the level number.
    LEVEL& = LEVEL& + 1
    SOFT& = 950 - M& : IF SOFT& < 0 THEN SOFT& = 0
    SCORE& = SCORE& + 250 + FUEL% \\ 2 + SOFT& + LEVEL& * 100
    IF SCORE& > BEST& THEN BEST& = SCORE&
    CALL DrawStats
    CALL Banner("TOUCHDOWN", 10)
    PLAY "T160O2L8CEGO3CL4E"
    DO
      KZ$ = INKEY$
    LOOP UNTIL KZ$ = ""
    DELAY 1.4
  ELSE
    ' CRASHED: blow up, show the banner, wait for a key (ESC quits).
    CALL Explode(LX%, LY%)
    CALL Banner("CRASHED", 12)
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

' ============================ terrain ============================

SUB BuildTerrain
  SHARED TERR%(), PADX1%, PADX2%, PADY%
  H% = 282
  FOR X% = 0 TO 639
    H% = H% + INT(RND * 7) - 3
    IF H% < 240 THEN H% = 240
    IF H% > 322 THEN H% = 322
    TERR%(X%) = H%
  NEXT
  ' a flat landing ledge somewhere across the surface, clear of the edges
  PADX1% = 120 + INT(RND * 380)
  PADX2% = PADX1% + 60
  PADY% = TERR%(PADX1% + 30)
  FOR X% = PADX1% TO PADX2%
    TERR%(X%) = PADY%
  NEXT
END SUB

' ============================ scenery ============================

SUB DrawScene
  SHARED TERR%(), PADX1%, PADX2%, PADY%
  CLS
  ' --- starfield (only above the terrain, below the HUD) ---
  FOR S% = 1 TO 150
    SX% = INT(RND * 640)
    SY% = 40 + INT(RND * 210)
    IF SY% < TERR%(SX%) - 6 THEN
      RP% = INT(RND * 10)
      SC% = 8
      IF RP% > 6 THEN SC% = 7
      IF RP% > 8 THEN SC% = 15
      PSET (SX%, SY%), SC%
      IF RP% = 9 THEN
        PSET (SX% + 1, SY%), 7
        PSET (SX%, SY% + 1), 7
      END IF
    END IF
  NEXT
  CALL DrawEarth
  ' --- moon surface: dark body with a lit rim, then craters ---
  FOR X% = 0 TO 639
    GT% = TERR%(X%)
    LINE (X%, GT%)-(X%, 349), 8
    LINE (X%, GT%)-(X%, GT% + 2), 7
  NEXT
  FOR CR% = 1 TO 8
    CCX% = 30 + INT(RND * 580)
    IF CCX% < PADX1% - 24 OR CCX% > PADX2% + 24 THEN
      CCY% = TERR%(CCX%) + 12 + INT(RND * 22)
      CRAD% = 4 + INT(RND * 10)
      IF CCY% + CRAD% < 348 THEN
        CALL FillCircle(CCX%, CCY%, CRAD%, 0)
        CIRCLE (CCX%, CCY% - 1), CRAD%, 7
      END IF
    END IF
  NEXT
  CALL DrawPad
  ' --- instrument panel frame + static labels ---
  LINE (0, 0)-(639, 34), 0, BF
  LINE (0, 35)-(639, 35), 8
  LINE (0, 36)-(639, 36), 3
  CALL DrawText(6, 3, "FUEL", 1, 11)
  CALL DrawText(6, 13, "VERT", 1, 11)
  CALL DrawText(6, 23, "HORZ", 1, 11)
  CALL DrawText(300, 3, "SCORE", 1, 14)
  CALL DrawText(300, 21, "LEVEL", 1, 14)
  CALL DrawText(500, 3, "BEST", 1, 10)
  CALL DrawStats
  CALL DrawHUD
END SUB

SUB DrawEarth
  CALL FillCircle(566, 72, 30, 1)
  CALL FillCircle(557, 64, 9, 2)
  CALL FillCircle(578, 78, 7, 2)
  CALL FillCircle(563, 84, 4, 2)
  CALL FillCircle(566, 56, 5, 15)
  CIRCLE (566, 72), 30, 9
  CIRCLE (566, 72), 31, 9
END SUB

SUB DrawPad
  SHARED PADX1%, PADX2%, PADY%
  LINE (PADX1%, PADY%)-(PADX2%, PADY% + 3), 7, BF
  LINE (PADX1%, PADY%)-(PADX2%, PADY%), 15
  LINE (PADX1% + 6, PADY% + 3)-(PADX1% + 10, PADY% + 20), 8
  LINE (PADX2% - 6, PADY% + 3)-(PADX2% - 10, PADY% + 20), 8
  LINE (PADX1% + 22, PADY% + 3)-(PADX1% + 22, PADY% + 18), 8
  LINE (PADX2% - 22, PADY% + 3)-(PADX2% - 22, PADY% + 18), 8
  LINE (PADX1%, PADY% - 8)-(PADX1%, PADY%), 7
  LINE (PADX2%, PADY% - 8)-(PADX2%, PADY%), 7
  CALL FillCircle(PADX1%, PADY% - 9, 2, 14)
  CALL FillCircle(PADX2%, PADY% - 9, 2, 14)
  FOR TKX% = PADX1% + 6 TO PADX2% - 6 STEP 10
    LINE (TKX%, PADY% + 1)-(TKX% + 4, PADY% + 1), 14
  NEXT
END SUB

' filled disk via horizontal scanlines (CIRCLE does not fill, PAINT is slower)
SUB FillCircle(CX%, CY%, RAD%, C%)
  FOR DY% = -RAD% TO RAD%
    DXW% = INT(SQR(RAD% * RAD% - DY% * DY%))
    LINE (CX% - DXW%, CY% + DY%)-(CX% + DXW%, CY% + DY%), C%
  NEXT
END SUB

' ============================ instruments ============================

SUB DrawHUD
  SHARED FUEL%, VY&, VX&, LVY&, LVX&
  ' fuel (green, red when nearly empty)
  FB% = FUEL% \\ 7 : IF FB% > 184 THEN FB% = 184
  FC% = 10 : IF FUEL% < 220 THEN FC% = 12
  LINE (56, 4)-(240, 9), 0, BF
  IF FB% > 0 THEN LINE (56, 4)-(56 + FB%, 9), FC%, BF
  ' descent rate (red when too fast to land)
  M& = VY& : IF M& < 0 THEN M& = -M&
  VB% = M& \\ 24 : IF VB% > 184 THEN VB% = 184
  CL% = 10 : IF M& > LVY& THEN CL% = 12
  LINE (56, 14)-(240, 19), 0, BF
  IF VB% > 0 THEN LINE (56, 14)-(56 + VB%, 19), CL%, BF
  ' horizontal drift
  N& = VX& : IF N& < 0 THEN N& = -N&
  HB% = N& \\ 24 : IF HB% > 184 THEN HB% = 184
  CL2% = 10 : IF N& > LVX& THEN CL2% = 12
  LINE (56, 24)-(240, 29), 0, BF
  IF HB% > 0 THEN LINE (56, 24)-(56 + HB%, 29), CL2%, BF
END SUB

SUB DrawStats
  SHARED SCORE&, LEVEL&, BEST&
  LINE (360, 2)-(470, 33), 0, BF
  CALL DrawNum(462, 3, SCORE&, 2, 15)
  CALL DrawNum(420, 20, LEVEL&, 2, 15)
  LINE (560, 2)-(634, 16), 0, BF
  CALL DrawNum(632, 3, BEST&, 2, 14)
END SUB

' draw string T$ at PX,PY scaled by SCL using the 5x7 font
SUB DrawText(PX%, PY%, T$, SCL%, C%)
  SHARED FONT%()
  COL% = PX%
  FOR CI% = 1 TO LEN(T$)
    CC% = ASC(MID$(T$, CI%, 1))
    GIDX% = -1
    IF CC% >= 48 AND CC% <= 57 THEN GIDX% = CC% - 48
    IF CC% >= 65 AND CC% <= 90 THEN GIDX% = CC% - 55
    IF CC% >= 97 AND CC% <= 122 THEN GIDX% = CC% - 87
    IF GIDX% >= 0 THEN
      FOR GR% = 0 TO 6
        BITS% = FONT%(GIDX%, GR%)
        BV% = 16
        FOR GC% = 0 TO 4
          IF (BITS% AND BV%) <> 0 THEN
            XX% = COL% + GC% * SCL%
            YY% = PY% + GR% * SCL%
            IF SCL% = 1 THEN
              PSET (XX%, YY%), C%
            ELSE
              LINE (XX%, YY%)-(XX% + SCL% - 1, YY% + SCL% - 1), C%, BF
            END IF
          END IF
          BV% = BV% \\ 2
        NEXT
      NEXT
    END IF
    COL% = COL% + 6 * SCL%
  NEXT
END SUB

' draw V& right-aligned so its rightmost digit ends near PX
SUB DrawNum(PX%, PY%, V&, SCL%, C%)
  N& = V& : IF N& < 0 THEN N& = 0
  RX% = PX% - 5 * SCL%
  DO
    DG% = N& MOD 10
    CALL DrawText(RX%, PY%, CHR$(48 + DG%), SCL%, C%)
    RX% = RX% - 6 * SCL%
    N& = N& \\ 10
  LOOP WHILE N& > 0
END SUB

SUB Banner(MSG$, C%)
  W% = LEN(MSG$) * 18
  X1% = 320 - W% \\ 2 - 14
  X2% = 320 + W% \\ 2 + 14
  LINE (X1%, 150)-(X2%, 200), 0, BF
  LINE (X1%, 150)-(X2%, 200), C%, B
  LINE (X1% + 3, 153)-(X2% - 3, 197), C%, B
  CALL DrawText(320 - W% \\ 2, 165, MSG$, 3, C%)
END SUB

' ============================ the spacecraft ============================

SUB DrawCraft(PX%, PY%)
  SHARED FRAME%
  ' landing legs (behind the body)
  LINE (PX% + 8, PY% + 30)-(PX% + 1, PY% + 42), 7
  LINE (PX% + 9, PY% + 33)-(PX% + 3, PY% + 41), 8
  LINE (PX% - 2, PY% + 42)-(PX% + 4, PY% + 42), 15
  LINE (PX% + 28, PY% + 30)-(PX% + 35, PY% + 42), 7
  LINE (PX% + 27, PY% + 33)-(PX% + 33, PY% + 41), 8
  LINE (PX% + 32, PY% + 42)-(PX% + 38, PY% + 42), 15
  ' descent stage: gold-foil box
  LINE (PX% + 7, PY% + 18)-(PX% + 29, PY% + 32), 6, BF
  LINE (PX% + 10, PY% + 18)-(PX% + 10, PY% + 32), 14
  LINE (PX% + 15, PY% + 18)-(PX% + 15, PY% + 32), 14
  LINE (PX% + 21, PY% + 18)-(PX% + 21, PY% + 32), 14
  LINE (PX% + 26, PY% + 18)-(PX% + 26, PY% + 32), 14
  LINE (PX% + 7, PY% + 18)-(PX% + 29, PY% + 18), 14
  LINE (PX% + 7, PY% + 18)-(PX% + 29, PY% + 32), 8, B
  ' RCS thruster nubs
  LINE (PX% + 4, PY% + 20)-(PX% + 6, PY% + 23), 8, BF
  LINE (PX% + 30, PY% + 20)-(PX% + 32, PY% + 23), 8, BF
  ' ascent stage: silver crew cabin
  LINE (PX% + 11, PY% + 10)-(PX% + 25, PY% + 18), 7, BF
  LINE (PX% + 13, PY% + 6)-(PX% + 23, PY% + 10), 7, BF
  LINE (PX% + 11, PY% + 10)-(PX% + 25, PY% + 10), 15
  LINE (PX% + 13, PY% + 6)-(PX% + 23, PY% + 6), 15
  LINE (PX% + 11, PY% + 10)-(PX% + 25, PY% + 18), 8, B
  ' window
  CALL FillCircle(PX% + 18, PY% + 13, 3, 11)
  PSET (PX% + 17, PY% + 12), 15
  ' engine bell
  LINE (PX% + 15, PY% + 32)-(PX% + 21, PY% + 36), 8, BF
  LINE (PX% + 16, PY% + 36)-(PX% + 20, PY% + 39), 8, BF
  ' antenna + flashing beacon
  LINE (PX% + 18, PY% + 6)-(PX% + 18, PY% + 1), 7
  BCOL% = 4 : IF (FRAME% AND 2) = 0 THEN BCOL% = 12
  PSET (PX% + 18, PY% + 1), BCOL%
END SUB

SUB DrawFlame(PX%, PY%, UP%, LT%, RT%)
  SHARED FRAME%
  CX% = PX% + 18
  IF UP% = 1 THEN
    FLEN% = 12 + (FRAME% AND 3) * 2
    FOR DY% = 0 TO FLEN%
      WID% = (FLEN% - DY%) \\ 3 + 1
      LINE (CX% - WID%, PY% + 39 + DY%)-(CX% + WID%, PY% + 39 + DY%), 14
    NEXT
    FOR DY% = 0 TO FLEN% - 4
      WID% = (FLEN% - 4 - DY%) \\ 4 + 1
      LINE (CX% - WID%, PY% + 39 + DY%)-(CX% + WID%, PY% + 39 + DY%), 12
    NEXT
    FOR DY% = 0 TO FLEN% - 8
      LINE (CX% - 1, PY% + 39 + DY%)-(CX% + 1, PY% + 39 + DY%), 15
    NEXT
  END IF
  IF LT% = 1 THEN
    LINE (PX% + 31, PY% + 21)-(PX% + 39, PY% + 21), 14
    LINE (PX% + 31, PY% + 20)-(PX% + 37, PY% + 20), 12
    LINE (PX% + 31, PY% + 22)-(PX% + 37, PY% + 22), 12
  END IF
  IF RT% = 1 THEN
    LINE (PX% + 5, PY% + 21)-(PX% - 3, PY% + 21), 14
    LINE (PX% + 5, PY% + 20)-(PX% - 1, PY% + 20), 12
    LINE (PX% + 5, PY% + 22)-(PX% - 1, PY% + 22), 12
  END IF
END SUB

SUB Explode(PX%, PY%)
  SHARED SAVE%(), OLDX%, OLDY%, HAVE%
  IF HAVE% = 1 THEN PUT (OLDX%, OLDY%), SAVE%, PSET
  HAVE% = 0
  CX% = PX% + 18 : CY% = PY% + 24
  FOR RR% = 3 TO 64 STEP 3
    CC% = 14 : IF (RR% AND 4) = 0 THEN CC% = 12
    IF RR% > 44 THEN CC% = 4
    CIRCLE (CX%, CY%), RR%, CC%
    IF RR% > 6 THEN CIRCLE (CX%, CY%), RR% - 4, 14
    SOUND 60, 0.35
    DELAY 0.02
  NEXT
END SUB
