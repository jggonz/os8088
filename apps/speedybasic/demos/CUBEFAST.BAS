' ===========================================================================
' CUBEFAST.BAS - CUBEPOKE.BAS, optimised: an INLINED line plotter with
'                INCREMENTAL framebuffer addressing (no per-pixel SUB call)
'
' Same SCREEN 1 CGA cube poked into B800 as CUBEPOKE.BAS, but the inner pixel
' loop is hand-optimised the way you would on real hardware to chase LINE:
'   * the plot is INLINED into the Bresenham loop, so there is ONE SUB call per
'     EDGE instead of one per PIXEL (the big win);
'   * the byte offset is tracked INCREMENTALLY (add a stride per step) instead
'     of recomputing (Y\\2)*80 + X\\4 for every pixel;
'   * the 2-bit pixel field index is stepped, not recomputed.
' This still needs PEEK+POKE per pixel (CGA packs 4 pixels per byte), so LINE's
' assembly inner loop may still edge it out - but it leaves CUBEPOKE.BAS far
' behind. Press ESC to stop.
' ===========================================================================
DEFINT A-Z
DIM VX(7), VY(7), VZ(7)
DIM PX(7), PY(7)
DIM QX(7), QY(7)
DIM EA(11), EB(11), EC(11)
DIM SN(255), CS(255)
DIM MUL(3)
MUL(0) = 64 : MUL(1) = 16 : MUL(2) = 4 : MUL(3) = 1

FOR I = 0 TO 7 : READ VX(I), VY(I), VZ(I) : NEXT I
DATA -100,-100,-100,  100,-100,-100,  100, 100,-100, -100, 100,-100
DATA -100,-100, 100,  100,-100, 100,  100, 100, 100, -100, 100, 100

FOR I = 0 TO 11 : READ EA(I), EB(I), EC(I) : NEXT I
DATA 0,1,3, 1,2,3, 2,3,3, 3,0,3
DATA 4,5,1, 5,6,1, 6,7,1, 7,4,1
DATA 0,4,2, 1,5,2, 2,6,2, 3,7,2

FOR A = 0 TO 255
  SN(A) = CINT(32 * SIN(A * 6.2831853 / 256))
  CS(A) = CINT(32 * COS(A * 6.2831853 / 256))
NEXT A

SCREEN 1 : CLS
DEF SEG = &HB800
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
    D = Z2 + 400
    PX(I) = 160 + (X1 * 110) \\ D
    PY(I) = 100 - (Y2 * 110) \\ D
  NEXT I

  FOR I = 0 TO 11
    CALL DrawLine(PX(EA(I)), PY(EA(I)), PX(EB(I)), PY(EB(I)), EC(I))
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
PRINT "CUBEFAST - inlined CGA plotter, incremental B800 addressing. Bye!"
END

' --- INLINED CGA line: ONE SUB call per edge, incremental byte offset (FO) +
'     bank (BNK) + 2-bit field index (P); PEEK/POKE read-modify-write because 4
'     pixels share a byte. The cube is sized to stay fully on-screen, so the hot
'     loop needs no per-pixel clipping. ---
SUB DrawLine(AX0, AY0, AX1, AY1, C)
  SHARED MUL()
  X0 = AX0 : Y0 = AY0 : X1 = AX1 : Y1 = AY1
  DX = ABS(X1 - X0) : DY = ABS(Y1 - Y0)
  SX = -1 : IF X0 < X1 THEN SX = 1
  SY = -1 : IF Y0 < Y1 THEN SY = 1
  BNK = Y0 AND 1
  FO = (Y0 \\ 2) * 80 + (X0 \\ 4)
  IF BNK THEN FO = FO + &H2000
  P = X0 AND 3
  ER = DX - DY
  DO
    M = MUL(P)
    POKE FO, (PEEK(FO) AND (255 - 3 * M)) OR (C * M)
    IF X0 = X1 AND Y0 = Y1 THEN EXIT SUB
    E2 = ER + ER
    IF E2 > -DY THEN
      ER = ER - DY : X0 = X0 + SX : P = P + SX
      IF P > 3 THEN P = 0 : FO = FO + 1
      IF P < 0 THEN P = 3 : FO = FO - 1
    END IF
    IF E2 < DX THEN
      ER = ER + DX : Y0 = Y0 + SY
      IF SY = 1 THEN
        IF BNK = 0 THEN
          FO = FO + &H2000 : BNK = 1            ' even->odd: same row pair, +bank
        ELSE
          FO = FO + 80 - &H2000 : BNK = 0       ' odd->even: next row pair, -bank
        END IF
      ELSE
        IF BNK = 0 THEN
          FO = FO - 80 + &H2000 : BNK = 1       ' even->odd up: prev row pair
        ELSE
          FO = FO - &H2000 : BNK = 0            ' odd->even up: same row pair
        END IF
      END IF
    END IF
  LOOP
END SUB
