' ===========================================================================
' CUBEPOKEHD.BAS - a depth-shaded spinning cube in VGA 256-colour MODE 13h
'
' This is the "HD" companion to CUBEPOKE.BAS. Mode 13h is the VGA programmer's
' favourite: 320x200 with 256 colours and a DEAD-SIMPLE framebuffer at segment
' &HA000 - one byte per pixel, laid out linearly (offset = Y*320 + X), with NO
' bit-packing and NO odd/even banks. So a pixel is literally one POKE.
'
' The whole thing is done the authentic Turbo-Basic-on-VGA way, so it runs
' unchanged in real Turbo Basic:
'   1. switch to mode 13h via the BIOS:   REG 1,&H13 : CALL INTERRUPT &H10
'   2. load a 256-entry "fire" palette through the VGA DAC colour registers
'      (OUT &H3C8 selects the index, three OUT &H3C9 write 6-bit R,G,B)
'   3. DEF SEG=&HA000 and POKE colour bytes straight at the screen
'
' Each wireframe edge is shaded by depth: corners nearer the camera are drawn
' with hotter palette indices (white/yellow), far corners with dark red - so the
' cube has real 3-D shading using the 256-colour palette. Press ESC to stop.
' ===========================================================================
DEFINT A-Z
DIM VX(7), VY(7), VZ(7)          ' the 8 cube corners (model space, x100)
DIM PX(7), PY(7), ZC(7)          ' projected points + camera-space depth
DIM QX(7), QY(7)                 ' last frame's points (so we can erase them)
DIM EA(11), EB(11)               ' the 12 edges (corner A, corner B)
DIM SN(255), CS(255)             ' scaled (x32) sine / cosine tables

FOR I = 0 TO 7 : READ VX(I), VY(I), VZ(I) : NEXT I
DATA -100,-100,-100,  100,-100,-100,  100, 100,-100, -100, 100,-100
DATA -100,-100, 100,  100,-100, 100,  100, 100, 100, -100, 100, 100

FOR I = 0 TO 11 : READ EA(I), EB(I) : NEXT I
DATA 0,1, 1,2, 2,3, 3,0,  4,5, 5,6, 6,7, 7,4,  0,4, 1,5, 2,6, 3,7

FOR A = 0 TO 255
  SN(A) = CINT(32 * SIN(A * 6.2831853 / 256))
  CS(A) = CINT(32 * COS(A * 6.2831853 / 256))
NEXT A

' --- enter VGA mode 13h (320x200x256) through the BIOS ---
REG 1, &H13                      ' REG 1 = AX = 0013h: AH=00 set mode, AL=13h
CALL INTERRUPT &H10

' --- load a black -> red -> yellow -> white "fire" palette into the DAC ---
OUT &H3C8, 0                     ' start writing at colour index 0
FOR I = 0 TO 255
  IF I < 85 THEN
    R = I * 63 \\ 85 : G = 0 : B = 0
  ELSEIF I < 170 THEN
    R = 63 : G = (I - 85) * 63 \\ 85 : B = 0
  ELSE
    R = 63 : G = 63 : B = (I - 170) * 63 \\ 85
  END IF
  OUT &H3C9, R : OUT &H3C9, G : OUT &H3C9, B   ' 6-bit R,G,B (auto-increments)
NEXT I

DEF SEG = &HA000                 ' point POKE at the mode-13h framebuffer
AY = 0 : AX = 0 : HAVE = 0

DO
  ' --- erase last frame's edges (redraw them in colour 0 = black) ---
  IF HAVE THEN
    FOR I = 0 TO 11
      CALL DrawLine(QX(EA(I)), QY(EA(I)), QX(EB(I)), QY(EB(I)), 0)
    NEXT I
  END IF

  ' --- rotate every corner about Y then X; keep the camera-space depth ZC ---
  FOR I = 0 TO 7
    X = VX(I) : Y = VY(I) : Z = VZ(I)
    X1 = (X * CS(AY) + Z * SN(AY)) \\ 32
    Z1 = (Z * CS(AY) - X * SN(AY)) \\ 32
    Y2 = (Y * CS(AX) - Z1 * SN(AX)) \\ 32
    Z2 = (Z1 * CS(AX) + Y * SN(AX)) \\ 32
    ZC(I) = Z2
    D = Z2 + 400
    PX(I) = 160 + (X1 * 110) \\ D
    PY(I) = 100 - (Y2 * 110) \\ D
  NEXT I

  ' --- draw this frame's edges, shaded by depth (near = hot, far = dark) ---
  FOR I = 0 TO 11
    AZ = (ZC(EA(I)) + ZC(EB(I))) \\ 2          ' average depth of the edge
    COL = 250 - (AZ + 173) * 7 \\ 10           ' nearer -> higher palette index
    IF COL < 16 THEN COL = 16                  ' (*7\\10 keeps the product < 32767)
    IF COL > 255 THEN COL = 255
    CALL DrawLine(PX(EA(I)), PY(EA(I)), PX(EB(I)), PY(EB(I)), COL)
  NEXT I

  FOR I = 0 TO 7 : QX(I) = PX(I) : QY(I) = PY(I) : NEXT I
  HAVE = 1
  AY = (AY + 3) AND 255
  AX = (AX + 2) AND 255
  DELAY .02
  K$ = INKEY$
LOOP UNTIL K$ = CHR$(27)

DEF SEG
SCREEN 0 : WIDTH 80 : CLS
PRINT "CUBEPOKEHD - mode 13h, fire palette via the DAC, pixels POKEd to A000."
END

' --- plot one mode-13h pixel: a single POKE, no read-modify-write needed ---
SUB PSet13(X, Y, C)
  IF X < 0 OR X > 319 OR Y < 0 OR Y > 199 THEN EXIT SUB
  O& = Y                                       ' long offset (Y*320 overflows INT)
  O& = O& * 320 + X
  POKE O&, C
END SUB

' --- integer Bresenham line through PSet13 (args copied so the by-reference
'     array elements passed in are never mutated) ---
SUB DrawLine(AX0, AY0, AX1, AY1, C)
  X0 = AX0 : Y0 = AY0 : X1 = AX1 : Y1 = AY1
  DX = ABS(X1 - X0) : DY = ABS(Y1 - Y0)
  SX = -1 : IF X0 < X1 THEN SX = 1
  SY = -1 : IF Y0 < Y1 THEN SY = 1
  ER = DX - DY
  DO
    CALL PSet13(X0, Y0, C)
    IF X0 = X1 AND Y0 = Y1 THEN EXIT SUB
    E2 = ER + ER
    IF E2 > -DY THEN ER = ER - DY : X0 = X0 + SX
    IF E2 < DX THEN ER = ER + DX : Y0 = Y0 + SY
  LOOP
END SUB
