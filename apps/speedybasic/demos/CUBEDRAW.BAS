' ===========================================================================
' CUBEDRAW.BAS - the SAME spinning 3-D cube, drawn with the LINE statement
'                (NO direct memory access - the high-level way)
'
' Companion to CUBEPOKE.BAS. Here every edge is a single LINE call into
' SCREEN 9 (640x350, 16-colour EGA) and the interpreter does the pixel
' plotting for us. Read the two sources side by side to see the difference
' between talking to the video hardware directly (POKE into B800) and letting
' BASIC's graphics primitives do the work. Runs unchanged in real Turbo Basic.
' Press ESC to stop.
' ===========================================================================
DIM VX(7), VY(7), VZ(7)
DIM PX(7), PY(7), QX(7), QY(7)
DIM EA(11), EB(11), EC(11)

' --- 8 unit-cube corners (floats are fine here - no bit math) ---
FOR I = 0 TO 7 : READ VX(I), VY(I), VZ(I) : NEXT I
DATA -1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1
DATA -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1

' --- 12 edges: front face yellow(14), back blue(9), connectors red(12) ---
FOR I = 0 TO 11 : READ EA(I), EB(I), EC(I) : NEXT I
DATA 0,1,14, 1,2,14, 2,3,14, 3,0,14
DATA 4,5,9, 5,6,9, 6,7,9, 7,4,9
DATA 0,4,12, 1,5,12, 2,6,12, 3,7,12

SCREEN 9 : CLS
CX = 320 : CY = 175              ' screen centre
FOC = 200 : DST = 4             ' focal length & camera distance (cube units)
AY = 0 : AX = 0 : HAVE = 0

DO
  ' --- erase last frame by redrawing its edges in colour 0 ---
  IF HAVE THEN
    FOR I = 0 TO 11
      LINE (QX(EA(I)), QY(EA(I)))-(QX(EB(I)), QY(EB(I))), 0
    NEXT I
  END IF

  ' --- rotate about Y then X, project with a perspective divide ---
  SA = SIN(AY) : CA = COS(AY)
  SB = SIN(AX) : CB = COS(AX)
  FOR I = 0 TO 7
    X = VX(I) : Y = VY(I) : Z = VZ(I)
    X1 = X * CA + Z * SA
    Z1 = Z * CA - X * SA
    Y2 = Y * CB - Z1 * SB
    Z2 = Z1 * CB + Y * SB
    W = FOC / (Z2 + DST)
    PX(I) = CX + X1 * W
    PY(I) = CY - Y2 * W
  NEXT I

  ' --- draw this frame's edges ---
  FOR I = 0 TO 11
    LINE (PX(EA(I)), PY(EA(I)))-(PX(EB(I)), PY(EB(I))), EC(I)
  NEXT I

  FOR I = 0 TO 7 : QX(I) = PX(I) : QY(I) = PY(I) : NEXT I
  HAVE = 1
  AY = AY + .06 : AX = AX + .04
  DELAY .02
  K$ = INKEY$
LOOP UNTIL K$ = CHR$(27)

SCREEN 0 : WIDTH 80 : CLS
PRINT "CUBEDRAW - the same cube via the LINE statement (no POKE). Bye!"
END
