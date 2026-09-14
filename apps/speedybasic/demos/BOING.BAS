'=====================================================================
'  BOING.BAS  -  "Amiga-style" bouncing checkered ball
'  Borland Turbo Basic 1.0 / 1.1
'---------------------------------------------------------------------
'  * SCREEN 9 = EGA 640x350, 16 colors (needs EGA w/ 256K).
'      For a 320x200 EGA card, use SCREEN 7 and halve R, XMAX, YMAX.
'  * Standard Turbo Basic / EGA color attributes:
'      4  = red          )  the checker
'      15 = bright white )
'      5  = magenta         the "Boing room" grid
'  * The sphere is built ONCE with a little checker-on-a-sphere math,
'    captured with GET, then animated with a mask-AND / ball-OR PUT
'    so the grid shows around the ball (no black box trailing it).
'  * Press ESC to quit.
'=====================================================================

SCREEN 9
CLS

'------------------------- tunables ----------------------------------
R    = 40              ' ball radius (pixels)
CELL = .5              ' checker size in radians; smaller = more squares
GRID = 40              ' background grid spacing (pixels)
XMAX = 639
YMAX = 349
'---------------------------------------------------------------------

D = 2 * R + 1          ' ball bounding-box side

DIM Ball%(4000)        ' ball image          (shrink these if you like)
DIM Mask%(4000)        ' AND-mask (black disc on white box)
DIM Bg%(4000)          ' background saved under the ball

'---------------------------------------------------------------------
' 1) Build the checkered sphere in the top-left corner
'---------------------------------------------------------------------
SX = R : SY = R
FOR ROW = 0 TO D - 1
  DY = ROW - R
  FOR COL = 0 TO D - 1
    DX = COL - R
    IF DX * DX + DY * DY <= R * R THEN
      X = DX / R
      Y = DY / R
      ZZ = 1 - X * X - Y * Y
      IF ZZ < .0025 THEN ZZ = .0025          ' clamp near the rim
      Z = SQR(ZZ)
      IF Z < .05 THEN Z = .05
      LON = ATN(X / Z)
      DEN = SQR(X * X + Z * Z)
      IF DEN < .05 THEN DEN = .05
      LAT = ATN(Y / DEN)
      CI = INT(LON / CELL) + INT(LAT / CELL)
      IF (CI AND 1) = 0 THEN C = 4 ELSE C = 15
      PSET (SX + DX, SY + DY), C
    END IF
  NEXT COL
NEXT ROW
GET (0, 0)-(D - 1, D - 1), Ball%

' mask: solid white box with a black filled disc punched out
LINE (0, 0)-(D - 1, D - 1), 15, BF
CIRCLE (SX, SY), R, 0
PAINT (SX, SY), 0, 0
GET (0, 0)-(D - 1, D - 1), Mask%

CLS

'---------------------------------------------------------------------
' 2) Draw the "Boing room" grid
'---------------------------------------------------------------------
FOR I = 0 TO XMAX STEP GRID : LINE (I, 0)-(I, YMAX), 5 : NEXT I
FOR I = 0 TO YMAX STEP GRID : LINE (0, I)-(XMAX, I), 5 : NEXT I

'---------------------------------------------------------------------
' 3) Bounce
'---------------------------------------------------------------------
PX = 100 : PY = 60          ' ball top-left corner
VX = 4   : VY = 0           ' velocity
GRAV = 1                    ' gravity added per frame

GET (PX, PY)-(PX + D - 1, PY + D - 1), Bg%
PUT (PX, PY), Mask%, AND
PUT (PX, PY), Ball%, OR

DO
  PUT (PX, PY), Bg%, PSET            ' erase ball (restore the grid)

  VY = VY + GRAV                     ' physics
  PX = PX + VX
  PY = PY + VY
  IF PX < 0          THEN PX = 0          : VX = -VX
  IF PX > XMAX - D + 1 THEN PX = XMAX - D + 1 : VX = -VX
  IF PY < 0          THEN PY = 0          : VY = -VY
  IF PY > YMAX - D + 1 THEN PY = YMAX - D + 1 : VY = -VY

  GET (PX, PY)-(PX + D - 1, PY + D - 1), Bg%
  PUT (PX, PY), Mask%, AND           ' punch a black hole in the grid
  PUT (PX, PY), Ball%, OR            ' drop the ball into it

  WAIT &H3DA, 8                      ' sync to EGA vertical retrace
  DELAY .01

  K$ = INKEY$
LOOP UNTIL K$ = CHR$(27)

SCREEN 0 : WIDTH 80 : CLS
PRINT "Boing!"
END
