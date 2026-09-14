' ===========================================================================
' CUBEASM.BAS - a SOLID, depth-shaded spinning cube in VGA 256-colour MODE
'               13h, whose faces are filled by an x86 SPAN-FILL BLITTER that
'               we hand-assemble, POKE into a CODE buffer, and reach with
'               CALL ABSOLUTE - one CALL per horizontal scanline span.
'
' This is the authentic "BASIC does the geometry, assembly does the fast fill"
' split, exactly as you would write it in real Turbo Basic:
'   1. switch to mode 13h via the BIOS:   REG 1,&H13 : CALL INTERRUPT &H10
'   2. load a 256-entry "fire" palette through the VGA DAC colour registers
'      (OUT &H3C8 selects the index, three OUT &H3C9 write 6-bit R,G,B)
'   3. hand-assemble a tiny REP STOSB span-filler, POKE its bytes into an
'      integer array (the classic CODE%() buffer), and CALL ABSOLUTE it once
'      per horizontal run of the solid faces.
'
' The 6 faces are sorted FAR->NEAR by average camera-space depth (painter's
' algorithm) and each is drawn as a FILLED convex quad - drawing back-to-front
' with overwrite removes hidden surfaces with no explicit culling. Each face is
' shaded by depth: nearer faces use hotter palette indices. Press ESC.
'
' ---------------------------------------------------------------------------
' THE BLITTER (call-absolute ABI - by REFERENCE, near CALL, callee does RET 6):
'
'   CALL ABSOLUTE ADRC (T0&, CNT, C)
'
' Real Turbo Basic's CALL ABSOLUTE passes scalar arguments BY REFERENCE: it
' pushes the 2-byte VARPTR (near offset) of each argument variable, NOT its
' value. So the routine receives POINTERS and dereferences them through DS
' (= the variable / code segment that DEF SEG selected). 3 words pushed L->R,
' so the stack frame (after PUSH BP / MOV BP,SP) is:
'       [BP+8] = &offset (-> low 16 bits of Y*320+X, an unsigned A000 offset)
'       [BP+6] = &count  (-> pixels in this span, >= 1)
'       [BP+4] = &colour (-> palette byte 0..255)
'   The routine sets ES=A000 ITSELF, loads each pointer into BX and reads the
'   word it points at into DI/CX/AL, then REP STOSB, RET 6. DEF SEG before the
'   CALL selects the CODE/variable segment (DGROUP), NOT A000.
'
'   Hand-assembled listing (mnemonic -> opcode bytes), 29 bytes:
'       PUSH BP                 ; 55
'       MOV  BP,SP              ; 8B EC
'       MOV  AX,0A000h          ; B8 00 A0
'       MOV  ES,AX              ; 8E C0
'       MOV  BX,[BP+8]          ; 8B 5E 08   BX = &offset
'       MOV  DI,[BX]            ; 8B 3F      DI = offset
'       MOV  BX,[BP+6]          ; 8B 5E 06   BX = &count
'       MOV  CX,[BX]            ; 8B 0F      CX = count
'       MOV  BX,[BP+4]          ; 8B 5E 04   BX = &colour
'       MOV  AX,[BX]            ; 8B 07      AL = colour
'       REP  STOSB              ; F3 AA
'       POP  BP                 ; 5D
'       RET  6                  ; C2 06 00
' ===========================================================================
DEFINT A-Z
DIM VX(7), VY(7), VZ(7)          ' the 8 cube corners (model space, x100)
DIM PX(7), PY(7), ZC(7)          ' projected screen points + camera-space depth
DIM FA(5), FB(5), FC(5), FD(5)   ' the 6 faces (4 corner indices each)
DIM FZ(5), ORD(5)                ' per-face average depth + far->near draw order
DIM SN(255), CS(255)             ' scaled (x32) sine / cosine tables
DIM XHIT(3)                      ' the x-intersections of one scanline (<=2 used)
DIM CRX(3), CRY(3)               ' the 4 projected corners of the face being filled

FOR I = 0 TO 7 : READ VX(I), VY(I), VZ(I) : NEXT I
DATA -100,-100,-100,  100,-100,-100,  100, 100,-100, -100, 100,-100
DATA -100,-100, 100,  100,-100, 100,  100, 100, 100, -100, 100, 100

' --- 6 faces, each 4 corner indices wound consistently ---
' (NB: an apostrophe inside a DATA line is NOT a comment - it becomes part of
'  the last value and breaks READ, in this IDE and in real Turbo Basic alike.
'  So the face labels live on their own ' lines, never trailing the DATA.)
FOR I = 0 TO 5 : READ FA(I), FB(I), FC(I), FD(I) : NEXT I
' back face (z = -100)
DATA 0,1,2,3
' front face (z = +100)
DATA 4,5,6,7
' bottom
DATA 0,1,5,4
' top
DATA 3,2,6,7
' left
DATA 0,3,7,4
' right
DATA 1,2,6,5

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

' ===========================================================================
' Load the span-fill blitter into a CODE%() buffer (the classic TB idiom).
' NB = number of code bytes; the DATA below is the exact 29-byte listing.
' ===========================================================================
NB = 29
DIM CODE%((NB + 1) \\ 2)         ' enough INTEGER cells to hold NB bytes
SEGC = VARSEG(CODE%(0))
ADRC = VARPTR(CODE%(0))
DEF SEG = SEGC                   ' POKE the bytes into the CODE buffer's segment
FOR I = 0 TO NB - 1
  READ BYT
  POKE ADRC + I, BYT
NEXT I
' ---- BLITTER BYTES (the 29-byte hand-assembled REP STOSB span-filler) ----
DATA &H55, &H8B, &HEC, &HB8, &H00, &HA0, &H8E, &HC0
DATA &H8B, &H5E, &H08, &H8B, &H3F, &H8B, &H5E, &H06
DATA &H8B, &H0F, &H8B, &H5E, &H04, &H8B, &H07, &HF3
DATA &HAA, &H5D, &HC2, &H06, &H00

AY2 = 0 : AX2 = 0

DO
  ' --- rotate every corner about Y then X; keep the camera-space depth ZC ---
  FOR I = 0 TO 7
    X = VX(I) : Y = VY(I) : Z = VZ(I)
    X1 = (X * CS(AY2) + Z * SN(AY2)) \\ 32
    Z1 = (Z * CS(AY2) - X * SN(AY2)) \\ 32
    Y2 = (Y * CS(AX2) - Z1 * SN(AX2)) \\ 32
    Z2 = (Z1 * CS(AX2) + Y * SN(AX2)) \\ 32
    ZC(I) = Z2
    D = Z2 + 400
    PX(I) = 160 + (X1 * 110) \\ D
    PY(I) = 100 - (Y2 * 110) \\ D
  NEXT I

  ' --- average camera-space depth per face, then sort faces FAR->NEAR ---
  FOR I = 0 TO 5
    FZ(I) = (ZC(FA(I)) + ZC(FB(I)) + ZC(FC(I)) + ZC(FD(I))) \\ 4
    ORD(I) = I
  NEXT I
  FOR J = 0 TO 4
    FOR K = 0 TO 4 - J
      IF FZ(ORD(K)) > FZ(ORD(K + 1)) THEN
        M = ORD(K) : ORD(K) = ORD(K + 1) : ORD(K + 1) = M
      END IF
    NEXT K
  NEXT J

  ' --- clear the screen (one span per row through the SAME blitter) ---
  FOR YY = 0 TO 199
    OF& = YY : OF& = OF& * 320
    LO& = OF& AND &HFFFF
    CALL FillSpan(LO&, 320, 0)
  NEXT YY

  ' --- draw every face back-to-front; overwrite removes hidden surfaces ---
  FOR N = 0 TO 5
    M = ORD(N)
    ' shade by depth: nearer face (larger FZ) -> hotter palette index
    COL = 250 - (FZ(M) + 173) * 7 \\ 10
    IF COL < 16 THEN COL = 16
    IF COL > 255 THEN COL = 255
    CALL FillQuad(M, COL)
  NEXT N

  AY2 = (AY2 + 3) AND 255
  AX2 = (AX2 + 2) AND 255
  DELAY .02
  K$ = INKEY$
LOOP UNTIL K$ = CHR$(27)

DEF SEG
SCREEN 0 : WIDTH 80 : CLS
PRINT "CUBEASM - solid painter's cube, faces span-filled via CALL ABSOLUTE."
END

' ---------------------------------------------------------------------------
' Fill one convex quad (face index FI, palette colour C) scanline by scanline.
' For each y, intersect the 4 edges with that scanline, take min/max x, and
' emit ONE span fill. Args copied; the SHARED arrays are read, never mutated.
' ---------------------------------------------------------------------------
SUB FillQuad(FI, C)
  SHARED PX(), PY(), FA(), FB(), FC(), FD(), XHIT(), CRX(), CRY()
  CRX(0) = PX(FA(FI)) : CRY(0) = PY(FA(FI))
  CRX(1) = PX(FB(FI)) : CRY(1) = PY(FB(FI))
  CRX(2) = PX(FC(FI)) : CRY(2) = PY(FC(FI))
  CRX(3) = PX(FD(FI)) : CRY(3) = PY(FD(FI))

  YT = CRY(0) : YB = CRY(0)
  FOR J = 1 TO 3
    IF CRY(J) < YT THEN YT = CRY(J)
    IF CRY(J) > YB THEN YB = CRY(J)
  NEXT J
  IF YT < 0 THEN YT = 0
  IF YB > 199 THEN YB = 199

  FOR YY = YT TO YB
    NX = 0
    FOR J = 0 TO 3
      P0 = J : P1 = (J + 1) AND 3
      YA = CRY(P0) : YB2 = CRY(P1)
      XA = CRX(P0) : XB = CRX(P1)
      ' does scanline YY cross edge (P0->P1)?  half-open test avoids dup hits
      IF (YA <= YY AND YB2 > YY) OR (YB2 <= YY AND YA > YY) THEN
        XI = XA + (XB - XA) * (YY - YA) \\ (YB2 - YA)
        IF NX <= 3 THEN XHIT(NX) = XI : NX = NX + 1
      END IF
    NEXT J
    IF NX >= 2 THEN
      XL = XHIT(0) : XR = XHIT(0)
      FOR J = 1 TO NX - 1
        IF XHIT(J) < XL THEN XL = XHIT(J)
        IF XHIT(J) > XR THEN XR = XHIT(J)
      NEXT J
      IF XL < 0 THEN XL = 0
      IF XR > 319 THEN XR = 319
      IF XR >= XL THEN
        OF& = YY : OF& = OF& * 320 + XL
        LO& = OF& AND &HFFFF
        CNT = XR - XL + 1
        CALL FillSpan(LO&, CNT, C)
      END IF
    END IF
  NEXT YY
END SUB

' ---------------------------------------------------------------------------
' Fill CNT pixels of palette colour C starting at linear A000 offset T0& by
' calling the hand-assembled blitter. DEF SEG selects the CODE segment (the
' bytes live there); the blitter sets ES=A000 itself, so its STOSB stores
' paint pixels. The offset is passed as its low 16 bits (0..63999 fits).
' ---------------------------------------------------------------------------
SUB FillSpan(T0&, CNT, C)
  SHARED SEGC, ADRC
  IF CNT < 1 THEN EXIT SUB
  ' CALL ABSOLUTE arguments must be INTEGER scalar variables in real Turbo Basic
  ' (a LONG raises "Error 429: Integer scalar variable expected"), so fold the
  ' 0..63999 offset into a signed 16-bit INTEGER - its exact low 16 bits, which
  ' is the unsigned offset the blitter's REP STOSB wants. CNT and C are already
  ' INTEGER (DEFINT A-Z).
  OW = T0&
  IF T0& > 32767 THEN OW = T0& - 65536
  DEF SEG = SEGC                              ' point the CALL at the CODE bytes
  CALL ABSOLUTE ADRC (OW, CNT, C)            ' &offset%, &count%, &colour% (by ref)
END SUB
