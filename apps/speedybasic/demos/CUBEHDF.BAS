' ===========================================================================
' CUBEHDF.BAS - CUBEPOKEHD.BAS, optimised: an INLINED plotter with a single
'               INCREMENTAL offset (mode 13h is linear, so this really flies)
'
' Same VGA mode-13h depth-shaded cube as CUBEPOKEHD.BAS, but the hot loop is
' optimised the way you would to out-run LINE on real hardware:
'   * the plot is INLINED into the Bresenham loop - ONE SUB call per EDGE, not
'     one per pixel;
'   * mode 13h is a LINEAR framebuffer, so the screen offset advances by a
'     single ADD per step (+1 across, +/-320 down/up) - no Y*320 + X multiply;
'   * one POKE per pixel, no read-modify-write (one byte = one pixel).
' This is the leanest POKE inner loop possible in pure Turbo Basic. Press ESC.
' ===========================================================================
DEFINT A-Z
DIM VX(7), VY(7), VZ(7)
DIM PX(7), PY(7), ZC(7)
DIM QX(7), QY(7)
DIM EA(11), EB(11)
DIM SN(255), CS(255)

FOR I = 0 TO 7 : READ VX(I), VY(I), VZ(I) : NEXT I
DATA -100,-100,-100,  100,-100,-100,  100, 100,-100, -100, 100,-100
DATA -100,-100, 100,  100,-100, 100,  100, 100, 100, -100, 100, 100

FOR I = 0 TO 11 : READ EA(I), EB(I) : NEXT I
DATA 0,1, 1,2, 2,3, 3,0,  4,5, 5,6, 6,7, 7,4,  0,4, 1,5, 2,6, 3,7

FOR A = 0 TO 255
  SN(A) = CINT(32 * SIN(A * 6.2831853 / 256))
  CS(A) = CINT(32 * COS(A * 6.2831853 / 256))
NEXT A

REG 1, &H13                      ' REG 1 = AX = 0013h: AH=00 set mode, AL=13h
CALL INTERRUPT &H10

OUT &H3C8, 0
FOR I = 0 TO 255
  IF I < 85 THEN
    R = I * 63 \\ 85 : G = 0 : B = 0
  ELSEIF I < 170 THEN
    R = 63 : G = (I - 85) * 63 \\ 85 : B = 0
  ELSE
    R = 63 : G = 63 : B = (I - 170) * 63 \\ 85
  END IF
  OUT &H3C9, R : OUT &H3C9, G : OUT &H3C9, B
NEXT I

DEF SEG = &HA000
AY = 0 : AX = 0 : HAVE = 0

DO
  IF HAVE THEN
    FOR I = 0 TO 11
      CALL DrawLine(QX(EA(I)), QY(EA(I)), QX(EB(I)), QY(EB(I)), 0)
    NEXT I
  END IF

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

  FOR I = 0 TO 11
    AZ = (ZC(EA(I)) + ZC(EB(I))) \\ 2
    COL = 250 - (AZ + 173) * 7 \\ 10
    IF COL < 16 THEN COL = 16
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
PRINT "CUBEHDF - inlined mode-13h plotter, incremental A000 offset. Bye!"
END

' --- INLINED mode-13h line: ONE SUB call per edge, the linear screen offset
'     advanced by a single ADD per step. The cube stays on-screen, so the hot
'     loop needs no per-pixel clipping. ---
SUB DrawLine(AX0, AY0, AX1, AY1, C)
  X0 = AX0 : Y0 = AY0 : X1 = AX1 : Y1 = AY1
  DX = ABS(X1 - X0) : DY = ABS(Y1 - Y0)
  SX = -1 : IF X0 < X1 THEN SX = 1
  SY = -1 : IF Y0 < Y1 THEN SY = 1
  SYO = SY * 320                   ' offset delta for one vertical step (once)
  O& = Y0 : O& = O& * 320 + X0     ' running framebuffer offset (computed once)
  ER = DX - DY
  DO
    POKE O&, C                     ' inline plot: no per-pixel call, no multiply
    IF X0 = X1 AND Y0 = Y1 THEN EXIT SUB
    E2 = ER + ER
    IF E2 > -DY THEN ER = ER - DY : X0 = X0 + SX : O& = O& + SX
    IF E2 < DX THEN ER = ER + DX : Y0 = Y0 + SY : O& = O& + SYO
  LOOP
END SUB
