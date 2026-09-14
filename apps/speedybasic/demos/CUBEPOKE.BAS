' ===========================================================================
' CUBEPOKE.BAS - a spinning 3-D cube plotted by writing DIRECTLY to video RAM
'
' SCREEN 1 is 320x200 CGA graphics whose framebuffer lives at segment &HB800.
' Instead of LINE / PSET we set every pixel by POKEing the packed CGA byte
' straight into that buffer - the authentic IBM-PC direct-memory idiom, and it
' runs unchanged in real Turbo Basic.
'
' CGA SCREEN-1 memory layout (16 KB at B800):
'   * two interleaved banks: EVEN scanlines at offset 0, ODD lines at &H2000
'   * 80 bytes per scanline, 4 pixels per byte, 2 bits per pixel (MSB = left)
'   so pixel (X,Y) lives in byte  (Y\\2)*80 + X\\4  (+ &H2000 when Y is odd),
'   and one of four 2-bit fields inside that byte (picked by X AND 3).
'
' All math is fixed-point INTEGER: coordinates are scaled x100 and the sine /
' cosine tables are scaled x32, so a 16-bit INTEGER never overflows. Compare
' with CUBEDRAW.BAS, the same cube drawn with the high-level LINE statement.
' Press ESC to stop.
' ===========================================================================
DEFINT A-Z                       ' integer pixel + bit math throughout
DIM VX(7), VY(7), VZ(7)          ' the 8 cube corners (model space, x100)
DIM PX(7), PY(7)                 ' this frame's projected screen points
DIM QX(7), QY(7)                 ' last frame's points (so we can erase them)
DIM EA(11), EB(11), EC(11)       ' 12 edges: corner A, corner B, colour
DIM SN(255), CS(255)             ' sine / cosine tables, 256 steps = one turn
DIM MUL(3)                       ' 2-bit field multipliers for the 4 px / byte

' --- the four pixel positions inside a CGA byte (leftmost = high bits) ---
MUL(0) = 64 : MUL(1) = 16 : MUL(2) = 4 : MUL(3) = 1

' --- 8 corners of a cube, scaled by 100 to keep the math integer ---
FOR I = 0 TO 7 : READ VX(I), VY(I), VZ(I) : NEXT I
DATA -100,-100,-100,  100,-100,-100,  100, 100,-100, -100, 100,-100
DATA -100,-100, 100,  100,-100, 100,  100, 100, 100, -100, 100, 100

' --- 12 edges: front face white(3), back cyan(1), connectors magenta(2) ---
FOR I = 0 TO 11 : READ EA(I), EB(I), EC(I) : NEXT I
DATA 0,1,3, 1,2,3, 2,3,3, 3,0,3
DATA 4,5,1, 5,6,1, 6,7,1, 7,4,1
DATA 0,4,2, 1,5,2, 2,6,2, 3,7,2

' --- build the scaled (x32) sine / cosine lookup tables once ---
FOR A = 0 TO 255
  SN(A) = CINT(32 * SIN(A * 6.2831853 / 256))
  CS(A) = CINT(32 * COS(A * 6.2831853 / 256))
NEXT A

SCREEN 1 : CLS
DEF SEG = &HB800                  ' point POKE/PEEK at the CGA framebuffer
AY = 0 : AX = 0 : HAVE = 0

DO
  ' --- erase last frame's edges (draw them again in background colour 0) ---
  IF HAVE THEN
    FOR I = 0 TO 11
      CALL DrawLine(QX(EA(I)), QY(EA(I)), QX(EB(I)), QY(EB(I)), 0)
    NEXT I
  END IF

  ' --- rotate every corner about Y then X, then project to the screen ---
  FOR I = 0 TO 7
    X = VX(I) : Y = VY(I) : Z = VZ(I)
    X1 = (X * CS(AY) + Z * SN(AY)) \\ 32        ' yaw about Y
    Z1 = (Z * CS(AY) - X * SN(AY)) \\ 32
    Y2 = (Y * CS(AX) - Z1 * SN(AX)) \\ 32       ' pitch about X
    Z2 = (Z1 * CS(AX) + Y * SN(AX)) \\ 32
    D = Z2 + 400                                ' camera distance (model units)
    PX(I) = 160 + (X1 * 110) \\ D               ' perspective divide
    PY(I) = 100 - (Y2 * 110) \\ D
  NEXT I

  ' --- draw this frame's edges in their colours ---
  FOR I = 0 TO 11
    CALL DrawLine(PX(EA(I)), PY(EA(I)), PX(EB(I)), PY(EB(I)), EC(I))
  NEXT I

  ' --- remember the points so we can erase them next time ---
  FOR I = 0 TO 7 : QX(I) = PX(I) : QY(I) = PY(I) : NEXT I
  HAVE = 1

  AY = (AY + 3) AND 255                         ' spin (AND 255 = MOD 256)
  AX = (AX + 2) AND 255
  DELAY .02
  K$ = INKEY$
LOOP UNTIL K$ = CHR$(27)

DEF SEG                                         ' restore the default segment
SCREEN 0 : WIDTH 80 : CLS
PRINT "CUBEPOKE - the cube was drawn by POKEing CGA video RAM at B800. Bye!"
END

' --- set one CGA pixel by POKEing its byte (read-modify-write one 2-bit field) -
SUB Plot(X, Y, C)
  SHARED MUL()
  IF X < 0 OR X > 319 OR Y < 0 OR Y > 199 THEN EXIT SUB
  O = (Y \\ 2) * 80 + (X \\ 4)
  IF Y AND 1 THEN O = O + &H2000              ' odd scanlines are in bank 2
  M = MUL(X AND 3)                            ' 64 / 16 / 4 / 1
  POKE O, (PEEK(O) AND (255 - 3 * M)) OR (C * M)
END SUB

' --- integer Bresenham line that plots through Plot (copies args to locals so
'     the by-reference array elements passed in are never mutated) ---
SUB DrawLine(AX0, AY0, AX1, AY1, C)
  X0 = AX0 : Y0 = AY0 : X1 = AX1 : Y1 = AY1
  DX = ABS(X1 - X0) : DY = ABS(Y1 - Y0)
  SX = -1 : IF X0 < X1 THEN SX = 1
  SY = -1 : IF Y0 < Y1 THEN SY = 1
  ER = DX - DY
  DO
    CALL Plot(X0, Y0, C)
    IF X0 = X1 AND Y0 = Y1 THEN EXIT SUB
    E2 = ER + ER
    IF E2 > -DY THEN ER = ER - DY : X0 = X0 + SX
    IF E2 < DX THEN ER = ER + DX : Y0 = Y0 + SY
  LOOP
END SUB
