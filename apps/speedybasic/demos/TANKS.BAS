' TANKS.BAS - BALLISTIC TANKS, a Scorched-Earth artillery duel (SCREEN 9, EGA 640x350).
'
' Two tanks duel across a randomized, destructible ridge under a dawn sky. Take
' turns adjusting your barrel ANGLE and POWER, picking a WEAPON, and firing a
' shell that arcs under gravity and wind. Hits blow craters in the ground and
' damage the enemy tank. Win a round to level up and unlock nastier ammo
' (HEAVY, BERTHA, CLUSTER, NAPALM, NUKE) bought with the CASH you earn.
'
' Modes:  1 = ONE PLAYER vs CPU    2 = TWO PLAYERS
' Turn:   LEFT/RIGHT = aim angle   UP/DOWN = power   TAB = cycle weapon
'         SPACE or ENTER = fire    ESC = quit (at any prompt)
'
' Everything (all text, the tanks, the HUD) is drawn with primitives and a 5x7
' bitmap font, because SCREEN 9 graphics mode has no text output.

' ---- display / terrain ----
DIM TERR%(640)
PANY% = 232

' ---- 5x7 bitmap font: glyphs 0-9 then A-Z, plus 36=$ 37=. 38=left 39=right ----
DIM FONT%(39, 6)
FOR GI% = 0 TO 39 : FOR GR% = 0 TO 6 : READ FONT%(GI%, GR%) : NEXT : NEXT
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
DATA 4,15,20,14,5,30,4
DATA 0,0,0,0,0,12,12
DATA 2,6,14,30,14,6,2
DATA 8,12,14,15,14,12,8

' ---- tanks: parallel arrays, index 0=P1 (left/blue), 1=P2 or CPU (right/red) ----
DIM TX%(1)
DIM TY%(1)
DIM HP%(1)
DIM TFUEL%(1)
DIM SCORE&(1)
DIM CASH&(1)
DIM ANG%(1)
DIM PWR%(1)
DIM WEP%(1)
DIM TCOL%(1)

' ---- per-tank background-save buffers (an in-memory copy of the clean scene
'      under each tank, so aiming only redraws that small patch + the HUD) ----
DIM TBG0%(600)
DIM TBG1%(600)

' ---- weapon table (filled by READ; NW% entries) ----
NW% = 6
DIM WNAME$(5)
DIM WBLAST%(5)
DIM WDMG%(5)
DIM WDIG%(5)
DIM WCOST&(5)
DIM WLVL%(5)
DIM WD1$(5)
DIM WD2$(5)
DIM WD3$(5)

FOR I% = 0 TO NW% - 1
  READ WNAME$(I%), WBLAST%(I%), WDMG%(I%), WDIG%(I%), WCOST&(I%), WLVL%(I%)
  READ WD1$(I%), WD2$(I%), WD3$(I%)
NEXT
DATA "STANDARD", 20, 34, 10, 0,   1, "GOOD ALL ROUND", "MEDIUM BLAST", "THE DEFAULT SHELL"
DATA "HEAVY",    30, 50, 16, 150, 2, "BIGGER BLAST", "MORE DAMAGE", "DIGS DEEPER"
DATA "BERTHA",   44, 64, 26, 350, 3, "LARGE CRATER", "HEAVY DAMAGE", "WIDE IMPACT"
DATA "CLUSTER",  18, 30, 8,  450, 4, "SPLITS AT APEX", "THREE BOMBLETS", "SPREAD DAMAGE"
DATA "NAPALM",   34, 40, 12, 550, 5, "WIDE SHALLOW BURN", "ORANGE FLAMES", "SCORCHES GROUND"
DATA "NUKE",     64, 95, 40, 900, 6, "HUGE BLAST", "SCREEN FLASH", "DEVASTATING"

' ---- game state ----
MODE% = 0
LEVEL& = 1
WND% = 0
TURNN% = 0
QUIT% = 0
LASTHIT% = 0

RANDOMIZE TIMER
SCREEN 9

DO
  QUIT% = 0
  CALL TitleScreen
  IF QUIT% = 1 THEN EXIT LOOP

  CALL Instructions
  IF QUIT% = 1 THEN EXIT LOOP

  ' ---- new game ----
  LEVEL& = 1
  SCORE&(0) = 0 : SCORE&(1) = 0 : CASH&(0) = 0 : CASH&(1) = 0
  TCOL%(0) = 1 : TCOL%(1) = 4

  ' ---- ROUND LOOP (one per level, until a tank is destroyed) ----
  DO
    TX%(0) = 90 : TX%(1) = 550
    CALL BuildTerrain
    TY%(0) = TERR%(TX%(0))
    TY%(1) = TERR%(TX%(1))
    HP%(0) = 100 : HP%(1) = 100
    TFUEL%(0) = 100 : TFUEL%(1) = 100
    ANG%(0) = 45 : ANG%(1) = 135
    PWR%(0) = 50 : PWR%(1) = 50
    WEP%(0) = 0  : WEP%(1) = 0
    WND% = INT(RND * 17) - 8
    TURNN% = 0
    CALL DrawScene

    ' ---- TURN LOOP ----
    DO
      IF TURNN% = 0 THEN
        CALL StatusStrip("PLAYER 1 FIRES", 11)
        CALL PlayerTurn(0)
      ELSE
        IF MODE% = 1 THEN
          CALL StatusStrip("CPU FIRES", 12)
          CALL DrawHUD
          CALL CpuAim(1)
          CALL DrawScene
          DELAY 0.4
          CALL FireShot(1)
        ELSE
          CALL StatusStrip("PLAYER 2 FIRES", 12)
          CALL PlayerTurn(1)
        END IF
      END IF
      IF QUIT% = 1 THEN EXIT LOOP

      IF HP%(0) <= 0 OR HP%(1) <= 0 THEN EXIT LOOP

      ' a non-fatal hit relocates the duel to a fresh battlefield
      IF LASTHIT% > 0 THEN
        CALL StatusStrip("DIRECT HIT  NEW BATTLEFIELD", 14)
        DELAY 0.7
        CALL Reposition
        CALL DrawScene
      END IF

      TURNN% = 1 - TURNN%
    LOOP

    IF QUIT% = 1 THEN EXIT LOOP

    ' ---- round resolved: award, then level up or game over ----
    IF HP%(1) <= 0 THEN
      CALL Explode(TX%(1), TY%(1) - 6, 50)
      SCORE&(0) = SCORE&(0) + 100 + LEVEL& * 50
      CASH&(0) = CASH&(0) + 200 + LEVEL& * 80
      CALL DrawScene
      IF MODE% = 1 THEN
        CALL Banner("YOU WIN", 14)
      ELSE
        CALL Banner("PLAYER 1 WINS", 14)
      END IF
    ELSE
      CALL Explode(TX%(0), TY%(0) - 6, 50)
      SCORE&(1) = SCORE&(1) + 100 + LEVEL& * 50
      CASH&(1) = CASH&(1) + 200 + LEVEL& * 80
      CALL DrawScene
      IF MODE% = 1 THEN
        CALL Banner("CPU WINS", 12)
      ELSE
        CALL Banner("PLAYER 2 WINS", 12)
      END IF
    END IF

    CALL StatusStrip("ANY KEY NEXT LEVEL  ESC QUIT", 15)
    K$ = ""
    DO
      K$ = INKEY$
      DELAY 0.05
    LOOP UNTIL K$ <> ""
    IF K$ = CHR$(27) THEN QUIT% = 1
    IF QUIT% = 1 THEN EXIT LOOP

    LEVEL& = LEVEL& + 1
  LOOP

  IF QUIT% = 1 THEN EXIT LOOP
LOOP

SCREEN 0
END

' ======================= font + primitives =======================

SUB FillCircle(CX%, CY%, RAD%, C%)
  FOR DY% = -RAD% TO RAD%
    DXW% = INT(SQR(RAD% * RAD% - DY% * DY%))
    LINE (CX% - DXW%, CY% + DY%)-(CX% + DXW%, CY% + DY%), C%
  NEXT
END SUB

' draw one glyph by table index (0-39) at PX,PY scaled by SCL
SUB DrawGlyphIdx(PX%, PY%, IDX%, SCL%, C%)
  SHARED FONT%()
  IF IDX% < 0 THEN EXIT SUB
  FOR GR% = 0 TO 6
    BITS% = FONT%(IDX%, GR%)
    BV% = 16
    FOR GC% = 0 TO 4
      IF (BITS% AND BV%) <> 0 THEN
        XX% = PX% + GC% * SCL%
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
END SUB

' draw string T$ at PX,PY scaled by SCL using the 5x7 font
SUB DrawText(PX%, PY%, T$, SCL%, C%)
  COL% = PX%
  FOR CI% = 1 TO LEN(T$)
    CC% = ASC(MID$(T$, CI%, 1))
    GIDX% = -1
    IF CC% >= 48 AND CC% <= 57 THEN GIDX% = CC% - 48
    IF CC% >= 65 AND CC% <= 90 THEN GIDX% = CC% - 55
    IF CC% >= 97 AND CC% <= 122 THEN GIDX% = CC% - 87
    IF CC% = 36 THEN GIDX% = 36
    IF CC% = 46 THEN GIDX% = 37
    IF GIDX% >= 0 THEN CALL DrawGlyphIdx(COL%, PY%, GIDX%, SCL%, C%)
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
  LINE (X1%, 90)-(X2%, 140), 0, BF
  LINE (X1%, 90)-(X2%, 140), C%, B
  LINE (X1% + 3, 93)-(X2% - 3, 137), C%, B
  CALL DrawText(320 - W% \\ 2, 105, MSG$, 3, C%)
END SUB

' ============================ terrain ============================

SUB BuildTerrain
  SHARED TERR%(), PANY%, TX%()
  H% = 150 + INT(RND * 40)
  FOR X% = 0 TO 639
    H% = H% + INT(RND * 7) - 3
    IF H% < 120 THEN H% = 120
    IF H% > PANY% - 6 THEN H% = PANY% - 6
    TERR%(X%) = H%
  NEXT
  ' smooth a little
  FOR X% = 1 TO 638
    TERR%(X%) = (TERR%(X% - 1) + TERR%(X%) + TERR%(X% + 1)) \\ 3
  NEXT
  ' carve a level pad under each tank's current position
  CALL CarvePad(TX%(0))
  CALL CarvePad(TX%(1))
END SUB

' flatten the terrain into a level ledge centred on CX so a tank sits flush
SUB CarvePad(CX%)
  SHARED TERR%()
  P0% = CX% - 20 : IF P0% < 0 THEN P0% = 0
  P1% = CX% + 20 : IF P1% > 639 THEN P1% = 639
  PYP% = TERR%(CX%)
  FOR X% = P0% TO P1%
    TERR%(X%) = PYP%
  NEXT
END SUB

' rebuild the battlefield with a fresh ridge and new tank positions: the left
' tank somewhere in the left third, the right tank in the right third, each on
' its own carved pad, plus a fresh wind. Called after a tank takes a hit so no
' two exchanges happen on the same ground.
SUB Reposition
  SHARED TX%(), TY%(), TERR%(), WND%
  TX%(0) = 60 + INT(RND * 150)
  TX%(1) = 430 + INT(RND * 150)
  CALL BuildTerrain
  TY%(0) = TERR%(TX%(0))
  TY%(1) = TERR%(TX%(1))
  WND% = INT(RND * 17) - 8
END SUB

SUB DrawSky
  ' banded dawn gradient with ordered dither between bands
  LINE (0, 0)-(639, 39), 1, BF
  LINE (0, 40)-(639, 79), 1, BF
  CALL DitherBand(68, 79, 5)
  LINE (0, 80)-(639, 119), 5, BF
  CALL DitherBand(108, 119, 4)
  LINE (0, 120)-(639, 159), 4, BF
  CALL DitherBand(148, 159, 6)
  LINE (0, 160)-(639, 199), 6, BF
  CALL DitherBand(188, 199, 14)
  LINE (0, 200)-(639, 233), 14, BF
  ' clouds
  CALL DrawCloud(110, 55)
  CALL DrawCloud(300, 42)
  CALL DrawCloud(470, 70)
  CALL DrawCloud(560, 95)
  ' distant magenta mountains
  CALL DrawHill(180, 150, 70, 5)
  CALL DrawHill(420, 162, 80, 5)
END SUB

SUB DitherBand(Y0%, Y1%, NC%)
  ' cheap scanline dither: paint every other row in the blend colour, leaving
  ' the band colour showing through between. A full-width LINE per row is ~600x
  ' faster than a per-pixel PSET sweep and reads the same at this scale.
  FOR YY% = Y0% TO Y1% STEP 2
    LINE (0, YY%)-(639, YY%), NC%
  NEXT
END SUB

SUB DrawCloud(CX%, CY%)
  CALL FillCircle(CX%, CY%, 7, 7)
  CALL FillCircle(CX% - 8, CY% + 2, 5, 7)
  CALL FillCircle(CX% + 9, CY% + 2, 5, 7)
  CALL FillCircle(CX%, CY% - 2, 4, 15)
END SUB

SUB DrawHill(CX%, TOPY%, HALF%, C%)
  FOR DX% = -HALF% TO HALF%
    YT% = TOPY% + ABS(DX%)
    LINE (CX% + DX%, YT%)-(CX% + DX%, 205), C%
  NEXT
END SUB

SUB DrawGround
  SHARED TERR%(), PANY%
  FOR X% = 0 TO 639
    GT% = TERR%(X%)
    LINE (X%, GT%)-(X%, PANY% - 1), 6
    LINE (X%, GT%)-(X%, GT% + 2), 2
    PSET (X%, GT%), 10
    IF (X% MOD 17) = 0 THEN PSET (X%, GT% + 8), 8
  NEXT
END SUB

' ============================ scene ============================

SUB DrawScene
  SHARED TERR%(), TX%(), TY%(), TCOL%(), ANG%(), HP%()
  CLS
  CALL DrawSky
  CALL DrawGround
  CALL DrawWindBox
  ' snapshot the clean background under each tank, THEN draw the tanks on top.
  ' Aiming later restores these patches instead of repainting the whole scene.
  CALL SaveBg(0)
  CALL SaveBg(1)
  CALL DrawTank(0)
  CALL DrawTank(1)
  CALL DrawHUD
END SUB

' ============================ tanks ============================

SUB DrawTank(I%)
  SHARED TX%(), TY%(), TCOL%(), ANG%(), HP%()
  CX% = TX%(I%)
  TYB% = TY%(I%)
  BC% = TCOL%(I%)
  ' tracks
  LINE (CX% - 15, TYB% - 6)-(CX% + 15, TYB% - 1), 8, BF
  FOR WX% = CX% - 12 TO CX% + 12 STEP 6
    CALL FillCircle(WX%, TYB% - 3, 2, 0)
  NEXT
  LINE (CX% - 15, TYB% - 6)-(CX% + 15, TYB% - 6), 7
  ' hull
  LINE (CX% - 12, TYB% - 12)-(CX% + 12, TYB% - 6), BC%, BF
  LINE (CX% - 12, TYB% - 12)-(CX% + 12, TYB% - 12), 15
  LINE (CX% - 12, TYB% - 12)-(CX% + 12, TYB% - 6), 8, B
  ' turret
  CALL FillCircle(CX%, TYB% - 12, 6, BC%)
  LINE (CX% - 7, TYB% - 12)-(CX% + 7, TYB% - 12), 8
  PSET (CX% - 2, TYB% - 15), 15
  ' barrel
  PVX% = CX%
  PVY% = TYB% - 13
  RAD! = ANG%(I%) * 3.14159 / 180
  TIPX% = PVX% + INT(16 * COS(RAD!))
  TIPY% = PVY% - INT(16 * SIN(RAD!))
  LINE (PVX%, PVY%)-(TIPX%, TIPY%), 7
  LINE (PVX%, PVY% - 1)-(TIPX%, TIPY% - 1), 7
  LINE (PVX%, PVY% + 1)-(TIPX%, TIPY% + 1), 7
  PSET (TIPX%, TIPY%), 15
  ' floating health number + triangle
  HC% = 10
  IF HP%(I%) <= 50 THEN HC% = 14
  IF HP%(I%) <= 20 THEN HC% = 12
  LINE (CX%, TYB% - 34)-(CX% - 5, TYB% - 26), BC%
  LINE (CX%, TYB% - 34)-(CX% + 5, TYB% - 26), BC%
  LINE (CX% - 5, TYB% - 26)-(CX% + 5, TYB% - 26), BC%
  HPV& = HP%(I%)
  CALL DrawNum(CX% + 9, TYB% - 24, HPV&, 1, HC%)
END SUB

SUB SettleTank(I%)
  SHARED TX%(), TY%(), TERR%(), HP%()
  NEWY% = TERR%(TX%(I%))
  DROP% = NEWY% - TY%(I%)
  IF DROP% > 0 THEN
    TY%(I%) = NEWY%
    IF DROP% > 14 THEN HP%(I%) = HP%(I%) - (DROP% - 14)
    IF HP%(I%) < 0 THEN HP%(I%) = 0
  END IF
END SUB

' --- in-memory background patch under a tank (covers hull, barrel sweep and
'     the floating health marker), saved/restored with GET/PUT ---
SUB SaveBg(I%)
  SHARED TX%(), TY%(), TBG0%(), TBG1%()
  CX% = TX%(I%)
  TYB% = TY%(I%)
  IF I% = 0 THEN
    GET (CX% - 20, TYB% - 36)-(CX% + 29, TYB% + 2), TBG0%
  ELSE
    GET (CX% - 20, TYB% - 36)-(CX% + 29, TYB% + 2), TBG1%
  END IF
END SUB

SUB RestoreBg(I%)
  SHARED TX%(), TY%(), TBG0%(), TBG1%()
  CX% = TX%(I%)
  TYB% = TY%(I%)
  IF I% = 0 THEN
    PUT (CX% - 20, TYB% - 36), TBG0%, PSET
  ELSE
    PUT (CX% - 20, TYB% - 36), TBG1%, PSET
  END IF
END SUB

' fast aim update: wipe the old tank by restoring the saved patch, then redraw
' just this tank (new barrel angle) and the HUD - no full-scene repaint.
SUB AimTank(I%)
  CALL RestoreBg(I%)
  CALL DrawTank(I%)
  CALL DrawAimReadout
END SUB

' ============================ HUD ============================

SUB DrawPanel(X1%, X2%, T$, C%)
  SHARED PANY%
  LINE (X1% + 1, PANY% + 4)-(X2% - 1, 332), 0, BF
  LINE (X1%, PANY% + 2)-(X2%, 333), C%, B
  CALL DrawText(X1% + 6, PANY% + 6, T$, 1, C%)
END SUB

SUB DrawBar(X%, Y%, W%, PCT%, C%)
  LINE (X%, Y%)-(X% + W%, Y% + 6), 8, B
  FL% = W% * PCT% \\ 100
  IF FL% > 0 THEN LINE (X% + 1, Y% + 1)-(X% + FL%, Y% + 5), C%, BF
END SUB

SUB DrawTankIcon(X%, Y%, C%)
  LINE (X%, Y% + 6)-(X% + 22, Y% + 10), 8, BF
  LINE (X% + 3, Y%)-(X% + 19, Y% + 6), C%, BF
  CALL FillCircle(X% + 11, Y%, 4, C%)
  LINE (X% + 11, Y% - 1)-(X% + 22, Y% - 6), 7
END SUB

SUB DrawHUD
  SHARED TX%(), TY%(), HP%(), TFUEL%(), SCORE&(), CASH&(), ANG%(), PWR%(), WEP%(), TURNN%
  SHARED WNAME$(), WD1$(), WD2$(), WD3$(), TCOL%(), LEVEL&, MODE%, PANY%
  ' clear HUD region
  LINE (0, PANY%)-(639, 334), 0, BF
  LINE (0, PANY%)-(639, PANY%), 7
  ' ---- LEFT panel: PLAYER 1 ----
  CALL DrawPanel(4, 210, "PLAYER 1", 9)
  CALL PlayerPanel(0, 10)
  ' ---- RIGHT panel: PLAYER 2 / CPU ----
  IF MODE% = 1 THEN
    CALL DrawPanel(430, 636, "CPU", 12)
  ELSE
    CALL DrawPanel(430, 636, "PLAYER 2", 12)
  END IF
  CALL PlayerPanel(1, 436)
  ' ---- CENTER panel ----
  IF TURNN% = 0 THEN
    CALL DrawPanel(216, 424, "YOUR TURN", 14)
  ELSE
    IF MODE% = 1 THEN
      CALL DrawPanel(216, 424, "CPU TURN", 14)
    ELSE
      CALL DrawPanel(216, 424, "YOUR TURN", 14)
    END IF
  END IF
  CALL DrawText(222, 256, "ANGLE", 1, 11)
  ANG& = ANG%(TURNN%)
  CALL DrawNum(330, 256, ANG&, 1, 15)
  CALL DrawText(222, 268, "POWER", 1, 11)
  PWR& = PWR%(TURNN%)
  CALL DrawNum(330, 268, PWR&, 1, 15)
  ' segmented power bar
  LINE (222, 278)-(418, 286), 8, B
  FOR SEG% = 0 TO 19
    SX% = 224 + SEG% * 10
    SC% = 8
    IF SEG% < PWR%(TURNN%) \\ 5 THEN
      SC% = 10
      IF SEG% >= 10 THEN SC% = 14
      IF SEG% >= 15 THEN SC% = 12
    END IF
    LINE (SX%, 279)-(SX% + 8, 285), SC%, BF
  NEXT
  ' weapon name + description box
  CALL DrawText(222, 292, "WEAPON", 1, 11)
  WI% = WEP%(TURNN%)
  CALL DrawText(300, 292, WNAME$(WI%), 1, 14)
  LINE (222, 302)-(418, 330), 8, B
  CALL FillCircle(232, 314, 3, 14)
  LINE (228, 314)-(232, 320), 7, BF
  CALL DrawText(244, 305, WD1$(WI%), 1, 10)
  CALL DrawText(244, 314, WD2$(WI%), 1, 10)
  CALL DrawText(244, 323, WD3$(WI%), 1, 10)
END SUB

' Cheap per-keystroke HUD update: while aiming, only ANGLE, POWER and the power
' bar change, so repaint just those (the labels, borders, both side panels and
' the weapon box stay put). This is the BOING lesson - touch only what moves -
' applied to the HUD, so an aim keypress costs ~50 ops instead of a full ~1500.
SUB DrawAimReadout
  SHARED ANG%(), PWR%(), TURNN%
  ' clear only the two number fields (labels to the left are untouched)
  LINE (305, 254)-(360, 277), 0, BF
  ANG& = ANG%(TURNN%)
  CALL DrawNum(330, 256, ANG&, 1, 15)
  PWR& = PWR%(TURNN%)
  CALL DrawNum(330, 268, PWR&, 1, 15)
  ' segmented power bar (inside the static border drawn by DrawHUD)
  FOR SEG% = 0 TO 19
    SX% = 224 + SEG% * 10
    SC% = 8
    IF SEG% < PWR%(TURNN%) \\ 5 THEN
      SC% = 10
      IF SEG% >= 10 THEN SC% = 14
      IF SEG% >= 15 THEN SC% = 12
    END IF
    LINE (SX%, 279)-(SX% + 8, 285), SC%, BF
  NEXT
END SUB

SUB PlayerPanel(I%, XB%)
  SHARED HP%(), TFUEL%(), SCORE&(), CASH&(), TCOL%()
  HC% = 9
  IF I% = 1 THEN HC% = 12
  CALL DrawText(XB%, 254, "HEALTH", 1, 7)
  CALL DrawBar(XB%, 264, 140, HP%(I%), HC%)
  CALL DrawText(XB%, 276, "FUEL", 1, 7)
  CALL DrawBar(XB%, 286, 140, TFUEL%(I%), 6)
  CALL DrawTankIcon(XB% + 150, 270, TCOL%(I%))
  CALL DrawText(XB%, 304, "SCORE", 1, 7)
  CALL DrawNum(XB% + 196, 304, SCORE&(I%), 1, 15)
  CALL DrawText(XB%, 316, "CASH", 1, 7)
  CALL DrawText(XB% + 92, 316, "$", 1, 10)
  CALL DrawNum(XB% + 196, 316, CASH&(I%), 1, 10)
END SUB

SUB DrawWindBox
  SHARED WND%
  LINE (268, 8)-(372, 36), 0, BF
  LINE (268, 8)-(372, 36), 15, B
  LINE (271, 11)-(369, 33), 8, B
  CALL DrawText(284, 13, "WIND", 1, 14)
  IF WND% < 0 THEN CALL DrawGlyphIdx(290, 24, 38, 1, 15)
  IF WND% > 0 THEN CALL DrawGlyphIdx(290, 24, 39, 1, 15)
  AW& = ABS(WND%)
  CALL DrawNum(360, 24, AW&, 1, 15)
END SUB

SUB StatusStrip(MSG$, C%)
  LINE (0, 335)-(639, 349), 1, BF
  CALL DrawText(320 - LEN(MSG$) * 3, 338, MSG$, 1, C%)
END SUB

' ============================ title ============================

SUB TitleScreen
  SHARED MODE%, QUIT%
  CLS
  ' a dim dawn backdrop
  LINE (0, 0)-(639, 60), 1, BF
  LINE (0, 60)-(639, 150), 5, BF
  LINE (0, 150)-(639, 230), 4, BF
  LINE (0, 230)-(639, 349), 6, BF
  ' big gold title (with brown drop shadow), two lines
  CALL DrawText(320 - 81 + 2, 32, "BALLISTIC", 3, 6)
  CALL DrawText(320 - 81, 30, "BALLISTIC", 3, 14)
  CALL DrawText(320 - 45 + 2, 82, "TANKS", 3, 6)
  CALL DrawText(320 - 45, 80, "TANKS", 3, 14)
  CALL DrawText(320 - 14 * 3, 130, "ARTILLERY DUEL", 1, 7)
  CALL DrawText(200, 170, "1 ONE PLAYER VS CPU", 2, 11)
  CALL DrawText(200, 200, "2 TWO PLAYERS", 2, 11)
  CALL DrawText(200, 240, "ESC QUIT", 2, 12)
  CALL DrawText(320 - 21 * 3, 274, "HOW TO PLAY SHOWN NEXT", 1, 10)
  ' flavour tanks
  CALL DrawText(60, 300, "P1", 1, 9)
  CALL DrawText(560, 300, "P2", 1, 12)
  CALL FlavourTank(80, 320, 9)
  CALL FlavourTank(560, 320, 4)
  DO
    K$ = INKEY$
    DELAY 0.02
    IF K$ <> "" THEN
      IF K$ = CHR$(27) THEN QUIT% = 1 : EXIT SUB
      IF K$ = "1" THEN MODE% = 1 : EXIT SUB
      IF K$ = "2" THEN MODE% = 2 : EXIT SUB
    END IF
  LOOP
END SUB

' ---- how-to-play screen, shown once after a mode is chosen ----
SUB Instructions
  SHARED QUIT%
  CLS
  LINE (0, 0)-(639, 349), 1, BF
  LINE (24, 12)-(615, 338), 14, B
  LINE (27, 15)-(612, 335), 14, B
  CALL DrawText(320 - 11 * 6, 24, "HOW TO PLAY", 2, 14)
  YY% = 58
  CALL DrawText(46, YY%, "BALLISTIC TANKS IS A TURN BASED ARTILLERY DUEL", 1, 15)
  YY% = YY% + 10
  CALL DrawText(46, YY%, "BLAST THE ENEMY TANK BEFORE IT FLATTENS YOU", 1, 15)
  YY% = YY% + 20
  CALL DrawText(46, YY%, "CONTROLS", 1, 11)
  YY% = YY% + 12
  CALL DrawText(60, YY%, "LEFT RIGHT     AIM THE BARREL ANGLE", 1, 10)
  YY% = YY% + 10
  CALL DrawText(60, YY%, "UP DOWN        ADJUST FIRING POWER", 1, 10)
  YY% = YY% + 10
  CALL DrawText(60, YY%, "TAB            CYCLE TO THE NEXT WEAPON", 1, 10)
  YY% = YY% + 10
  CALL DrawText(60, YY%, "COMMA PERIOD   DRIVE THE TANK  USES FUEL", 1, 10)
  YY% = YY% + 10
  CALL DrawText(60, YY%, "SPACE OR ENTER FIRE", 1, 10)
  YY% = YY% + 10
  CALL DrawText(60, YY%, "ESC            QUIT", 1, 10)
  YY% = YY% + 20
  CALL DrawText(46, YY%, "WIND PUSHES YOUR SHELL  WATCH THE WIND BOX", 1, 15)
  YY% = YY% + 10
  CALL DrawText(46, YY%, "HITS BLOW CRATERS IN THE GROUND AND HURT TANKS", 1, 15)
  YY% = YY% + 10
  CALL DrawText(46, YY%, "WIN A ROUND TO LEVEL UP AND UNLOCK STRONGER AMMO", 1, 15)
  YY% = YY% + 10
  CALL DrawText(60, YY%, "HEAVY  BERTHA  CLUSTER  NAPALM  NUKE", 1, 14)
  YY% = YY% + 10
  CALL DrawText(46, YY%, "EARN CASH FROM HITS TO BUY THE BIGGER WEAPONS", 1, 15)
  YY% = YY% + 24
  CALL DrawText(320 - 22 * 3, YY%, "PRESS ANY KEY TO START", 1, 14)
  K$ = ""
  DO
    K$ = INKEY$
    DELAY 0.03
  LOOP UNTIL K$ <> ""
  IF K$ = CHR$(27) THEN QUIT% = 1
END SUB

SUB FlavourTank(CX%, TYB%, BC%)
  LINE (CX% - 15, TYB% - 6)-(CX% + 15, TYB% - 1), 8, BF
  LINE (CX% - 12, TYB% - 12)-(CX% + 12, TYB% - 6), BC%, BF
  CALL FillCircle(CX%, TYB% - 12, 6, BC%)
  LINE (CX%, TYB% - 13)-(CX% + 16, TYB% - 22), 7
END SUB

' ============================ firing ============================

SUB FireShot(I%)
  SHARED TX%(), TY%(), ANG%(), PWR%(), WEP%(), WND%, TERR%(), HP%(), QUIT%, PANY%
  SHARED WBLAST%(), WDMG%(), WDIG%(), TCOL%(), SCORE&(), CASH&(), LASTHIT%
  LASTHIT% = 0
  J% = 1 - I%
  WI% = WEP%(I%)
  RAD! = ANG%(I%) * 3.14159 / 180
  PVX% = TX%(I%)
  PVY% = TY%(I%) - 13
  SPD! = PWR%(I%) * 0.16
  VXS! = SPD! * COS(RAD!)
  VYS! = -SPD! * SIN(RAD!)
  PXS! = PVX% + 16 * COS(RAD!) + VXS!
  PYS! = PVY% - 16 * SIN(RAD!) + VYS!
  GACC! = 0.35
  TF% = 0
  HIT% = 0
  IX% = INT(PXS!)
  IY% = INT(PYS!)
  DO
    FT! = TIMER
    VXS! = VXS! + WND% * 0.01
    VYS! = VYS! + GACC!
    PXS! = PXS! + VXS!
    PYS! = PYS! + VYS!
    IX% = INT(PXS!)
    IY% = INT(PYS!)
    TF% = TF% + 1
    IF (TF% AND 1) = 0 AND IY% >= 0 AND IX% >= 0 AND IX% <= 639 THEN PSET (IX%, IY%), 15

    ' ESC quits mid-flight
    K$ = INKEY$
    IF K$ = CHR$(27) THEN QUIT% = 1 : EXIT SUB

    ' off field -> dud miss
    IF IX% < 0 OR IX% > 639 THEN
      CALL StatusStrip("MISSED OFF FIELD", 7)
      EXIT SUB
    END IF
    ' direct tank hit
    IF ABS(IX% - TX%(J%)) <= 13 AND ABS(IY% - (TY%(J%) - 6)) <= 10 THEN
      HIT% = 1 : EXIT LOOP
    END IF
    ' terrain hit
    IF IY% >= 0 THEN
      IF IY% >= TERR%(IX%) THEN HIT% = 1 : EXIT LOOP
    END IF
    ' safety cap
    IF TF% > 1200 THEN EXIT SUB

    EL! = TIMER - FT!
    IF EL! < 0 THEN EL! = 0
    WT! = 0.012 - EL!
    IF WT! > 0 THEN DELAY WT!
  LOOP

  IF HIT% = 1 THEN
    BX% = IX%
    BY% = IY%
    IF BY% > PANY% - 1 THEN BY% = PANY% - 1
    BR% = WBLAST%(WI%)
    ' weapon special: NUKE flash, NAPALM flames
    IF WI% = 5 THEN
      LINE (0, 0)-(639, PANY% - 1), 15, BF
      DELAY 0.05
    END IF
    CALL Explode(BX%, BY%, BR%)
    IF WI% = 4 THEN CALL Napalm(BX%, BY%, BR%)
    CALL DigCrater(BX%, BR%, WDIG%(WI%))
    ' cluster: two extra small craters either side
    IF WI% = 3 THEN
      CLX% = BX% - 24
      IF CLX% < 0 THEN CLX% = 0
      CALL Explode(CLX%, TERR%(CLX%), 14)
      CALL DigCrater(CLX%, 14, WDIG%(WI%))
      CRX% = BX% + 24
      IF CRX% > 639 THEN CRX% = 639
      CALL Explode(CRX%, TERR%(CRX%), 14)
      CALL DigCrater(CRX%, 14, WDIG%(WI%))
    END IF
    LASTHIT% = 0
    CALL DamageTank(0, BX%, BY%, BR%, WDMG%(WI%))
    CALL DamageTank(1, BX%, BY%, BR%, WDMG%(WI%))
    DEALT% = LASTHIT%
    CALL SettleTank(0)
    CALL SettleTank(1)
    CALL DrawScene
    IF DEALT% > 0 THEN
      SCORE&(I%) = SCORE&(I%) + DEALT% * 10
      CASH&(I%) = CASH&(I%) + DEALT% * 5
      CALL StatusStrip("DIRECT HIT", 14)
    ELSE
      CALL StatusStrip("MISS", 7)
    END IF
    DELAY 0.5
  END IF
END SUB

SUB Explode(CX%, CY%, RAD%)
  FOR RR% = 3 TO RAD% STEP 3
    CC% = 14 : IF (RR% AND 4) = 0 THEN CC% = 12
    IF RR% > RAD% * 3 \\ 4 THEN CC% = 4
    CIRCLE (CX%, CY%), RR%, CC%
    IF RR% > 6 THEN CIRCLE (CX%, CY%), RR% - 4, 14
    SOUND 70 + RR%, 0.3
    DELAY 0.015
  NEXT
  CALL FillCircle(CX%, CY%, RAD% \\ 2, 4)
END SUB

SUB Napalm(CX%, CY%, RAD%)
  FOR P% = 1 TO 40
    AA! = RND * 6.2832
    DD% = INT(RND * RAD%)
    FX% = CX% + INT(DD% * COS(AA!))
    FY% = CY% + INT(DD% * SIN(AA!))
    FC% = 12
    RR% = INT(RND * 3)
    IF RR% = 1 THEN FC% = 6
    IF RR% = 2 THEN FC% = 14
    IF FX% >= 0 AND FX% <= 639 AND FY% >= 0 AND FY% <= 348 THEN PSET (FX%, FY%), FC%
  NEXT
END SUB

SUB DigCrater(CX%, RAD%, DEPTH%)
  SHARED TERR%(), PANY%
  FOR X% = CX% - RAD% TO CX% + RAD%
    IF X% >= 0 AND X% <= 639 THEN
      DXC% = X% - CX%
      T& = DEPTH%
      T& = (T& * DXC% * DXC%) \\ (RAD% * RAD%)
      DD% = DEPTH% - T&
      IF DD% > 0 THEN
        NEWY% = TERR%(X%) + DD%
        IF NEWY% > PANY% - 1 THEN NEWY% = PANY% - 1
        TERR%(X%) = NEWY%
      END IF
    END IF
  NEXT
END SUB

SUB DamageTank(I%, CX%, CY%, RAD%, DMG%)
  SHARED TX%(), TY%(), HP%(), LASTHIT%
  DXD! = TX%(I%) - CX%
  DYD! = (TY%(I%) - 6) - CY%
  DIST! = SQR(DXD! * DXD! + DYD! * DYD!)
  IF DIST! <= RAD% THEN
    FRAC! = 1.0 - DIST! / RAD%
    HURT% = INT(DMG% * FRAC!)
    IF HURT% > 0 THEN
      HP%(I%) = HP%(I%) - HURT%
      IF HP%(I%) < 0 THEN HP%(I%) = 0
      LASTHIT% = HURT%
    END IF
  END IF
END SUB

' ============================ CPU AI ============================

SUB CpuAim(I%)
  SHARED TX%(), TY%(), ANG%(), PWR%(), WEP%(), WND%, LEVEL&, TERR%(), CASH&()
  SHARED WLVL%(), WCOST&()
  HX% = TX%(0)
  BESTERR& = 99999
  BANG% = 135
  BPOW% = 60
  FOR TANG% = 95 TO 175 STEP 5
    FOR TPOW% = 30 TO 100 STEP 10
      CALL SimShot(I%, TANG%, TPOW%, LANDX%)
      ER& = ABS(LANDX% - HX%)
      IF ER& < BESTERR& THEN
        BESTERR& = ER&
        BANG% = TANG%
        BPOW% = TPOW%
      END IF
    NEXT
  NEXT
  JIT% = 22 - INT(LEVEL&) * 3
  IF JIT% < 2 THEN JIT% = 2
  BANG% = BANG% + INT(RND * (JIT% * 2 + 1)) - JIT%
  BPOW% = BPOW% + INT(RND * (JIT% + 1)) - JIT% \\ 2
  IF BANG% < 91 THEN BANG% = 91
  IF BANG% > 179 THEN BANG% = 179
  IF BPOW% < 10 THEN BPOW% = 10
  IF BPOW% > 100 THEN BPOW% = 100
  ANG%(I%) = BANG%
  PWR%(I%) = BPOW%
  ' pick the strongest affordable, unlocked weapon
  BW% = 0
  FOR W% = 0 TO 5
    IF WLVL%(W%) <= LEVEL& AND WCOST&(W%) <= CASH&(I%) THEN BW% = W%
  NEXT
  WEP%(I%) = BW%
END SUB

' silent integrator: returns the impact column LANDX% for the given shot
SUB SimShot(I%, TANG%, TPOW%, LANDX%)
  SHARED TX%(), TY%(), WND%, TERR%()
  J% = 1 - I%
  RAD! = TANG% * 3.14159 / 180
  SPD! = TPOW% * 0.16
  VXS! = SPD! * COS(RAD!)
  VYS! = -SPD! * SIN(RAD!)
  PXS! = TX%(I%) + 16 * COS(RAD!) + VXS!
  PYS! = (TY%(I%) - 13) - 16 * SIN(RAD!) + VYS!
  GACC! = 0.35
  TF% = 0
  IX% = INT(PXS!)
  DO
    VXS! = VXS! + WND% * 0.01
    VYS! = VYS! + GACC!
    PXS! = PXS! + VXS!
    PYS! = PYS! + VYS!
    IX% = INT(PXS!)
    IY% = INT(PYS!)
    TF% = TF% + 1
    IF IX% < 0 OR IX% > 639 THEN
      LANDX% = IX%
      EXIT SUB
    END IF
    IF ABS(IX% - TX%(J%)) <= 13 AND ABS(IY% - (TY%(J%) - 6)) <= 10 THEN
      LANDX% = IX%
      EXIT SUB
    END IF
    IF IY% >= 0 THEN
      IF IY% >= TERR%(IX%) THEN
        LANDX% = IX%
        EXIT SUB
      END IF
    END IF
    IF TF% > 1200 THEN
      LANDX% = IX%
      EXIT SUB
    END IF
  LOOP
END SUB

' ============================ player turn ============================

SUB PlayerTurn(I%)
  SHARED TX%(), TY%(), ANG%(), PWR%(), WEP%(), TFUEL%(), CASH&(), TERR%()
  SHARED QUIT%, WLVL%(), WCOST&(), LEVEL&
  CALL DrawHUD
  DO
    K$ = INKEY$
    IF K$ <> "" THEN
      IF K$ = CHR$(27) THEN QUIT% = 1 : EXIT SUB
      IF K$ = " " OR K$ = CHR$(13) THEN
        IF WCOST&(WEP%(I%)) <= CASH&(I%) THEN
          CASH&(I%) = CASH&(I%) - WCOST&(WEP%(I%))
          CALL FireShot(I%)
          EXIT SUB
        END IF
      END IF
      IF K$ = CHR$(9) THEN
        CALL CycleWeapon(I%)
        CALL DrawHUD
      END IF
      IF K$ = "," AND TFUEL%(I%) > 0 THEN
        TX%(I%) = TX%(I%) - 3
        IF TX%(I%) < 30 THEN TX%(I%) = 30
        TFUEL%(I%) = TFUEL%(I%) - 3
        TY%(I%) = TERR%(TX%(I%))
        CALL DrawScene
      END IF
      IF K$ = "." AND TFUEL%(I%) > 0 THEN
        TX%(I%) = TX%(I%) + 3
        IF TX%(I%) > 610 THEN TX%(I%) = 610
        TFUEL%(I%) = TFUEL%(I%) - 3
        TY%(I%) = TERR%(TX%(I%))
        CALL DrawScene
      END IF
      IF LEN(K$) = 2 AND ASC(K$) = 0 THEN
        EC% = ASC(MID$(K$, 2, 1))
        ADLT% = 2
        IF I% = 1 THEN ADLT% = -2
        IF EC% = 75 THEN ANG%(I%) = ANG%(I%) + ADLT%
        IF EC% = 77 THEN ANG%(I%) = ANG%(I%) - ADLT%
        IF EC% = 72 THEN PWR%(I%) = PWR%(I%) + 2
        IF EC% = 80 THEN PWR%(I%) = PWR%(I%) - 2
        IF ANG%(I%) < 0 THEN ANG%(I%) = 0
        IF ANG%(I%) > 180 THEN ANG%(I%) = 180
        IF PWR%(I%) < 0 THEN PWR%(I%) = 0
        IF PWR%(I%) > 100 THEN PWR%(I%) = 100
        CALL AimTank(I%)
      END IF
    END IF
    DELAY 0.01
  LOOP
END SUB

SUB CycleWeapon(I%)
  SHARED WEP%(), WLVL%(), WCOST&(), CASH&(), LEVEL&
  W% = WEP%(I%)
  FOR TRY% = 1 TO 6
    W% = (W% + 1) MOD 6
    IF WLVL%(W%) <= LEVEL& AND WCOST&(W%) <= CASH&(I%) THEN
      WEP%(I%) = W%
      EXIT SUB
    END IF
  NEXT
END SUB
