' ===========================================================================
' COPPER.BAS - Amiga-style "copper bars" in VGA 256-colour MODE 13h: four
'              glowing raster bars bounce up and down on sine paths, and
'              EVERY scanline of every bar is painted by the same 29-byte
'              hand-assembled x86 REP STOSB span-fill blitter as CUBEASM.BAS,
'              reached with CALL ABSOLUTE - one CALL per 320-pixel scanline.
'
' On the Amiga the "copper" coprocessor reprogrammed the background colour on
' every scanline, producing those trademark glowing horizontal bars. VGA has
' no copper, so the classic PC translation is exactly this: program a smooth
' light-dark-mirrored gradient per bar into the DAC (16 shades each), then
' blast each bar onto the screen as a stack of full-width scanline fills -
' fast enough, thanks to the machine-code blitter, to move every frame.
'
'   1. enter mode 13h via the BIOS:  REG 1,&H13 : CALL INTERRUPT &H10
'   2. program 4 x 16 DAC gradient entries (colour indices 32..95) through
'      the VGA DAC ports: OUT &H3C8 selects the index, then three OUT &H3C9
'      writes set 6-bit R,G,B and auto-increment to the next index
'   3. POKE the span blitter into a CODE%() buffer and CALL ABSOLUTE it for
'      every scanline: erase last frame's bands (black spans), then draw
'      each bar bottom-up - later bars simply paint over earlier ones
'
' The blitter bytes and the by-reference ABI are documented in CUBEASM.BAS:
'   CALL ABSOLUTE ADRC (OW, CNT, C) pushes the VARPTRs of three INTEGER
'   scalars ([BP+8]=&offset, [BP+6]=&count, [BP+4]=&colour); the routine
'   sets ES=A000 itself, REP STOSBs, and cleans the stack with RET 6. Any
'   A000 offset above 32767 MUST be folded to a signed 16-bit INTEGER first
'   or real Turbo Basic stops with "Error 429: Integer scalar variable
'   expected" (see SUB FillSpan below).  Press ESC to quit.
' ===========================================================================
DEFINT A-Z

NBAR = 4                          ' how many bars
BH = 31                           ' bar height: 16 shades mirrored light-dark
DIM SN(255)                       ' sine table: vertical bounce offsets
DIM PH(NBAR - 1), SP(NBAR - 1)    ' per-bar sine phase + phase speed
DIM PT(NBAR - 1)                  ' previous frame's bar top (-1 = not drawn)
DIM BC(NBAR - 1)                  ' first DAC index of each bar's gradient

' --- precompute the bounce path: SN(A) = 80 * SIN(2*pi*A / 256), so a bar
'     top of 84 + SN(phase) sweeps 4..164 and BH more rows always fit ---
FOR I = 0 TO 255
  SN(I) = CINT(80 * SIN(I * 6.2831853 / 256))
NEXT I

FOR BI = 0 TO NBAR - 1
  READ PH(BI), SP(BI)
  BC(BI) = 32 + BI * 16           ' gradients live at DAC 32-47, 48-63, ...
  PT(BI) = -1
NEXT BI
' start phase / speed per bar (red, blue, green, gold)
DATA 0,3, 64,4, 128,5, 192,6

' --- enter VGA mode 13h (320x200x256) through the BIOS ---
REG 1, &H13                       ' REG 1 = AX = 0013h: AH=00 set mode, AL=13h
CALL INTERRUPT &H10

' --- program the DAC gradients: 16 shades per bar at indices 32..95.
'     Shade I ramps 0..63 in the bar's hue; the DRAW loop mirrors the shades
'     around the bar's middle so each bar reads as a glowing lit metal tube ---
OUT &H3C8, 32                     ' start at colour index 32 (auto-increments)
FOR BI = 0 TO NBAR - 1
  FOR I = 0 TO 15
    V = I * 63 \\ 15              ' 6-bit intensity 0..63
    IF BI = 0 THEN R = V : G = V \\ 5 : BL = V \\ 5          ' red
    IF BI = 1 THEN R = V \\ 5 : G = V \\ 5 : BL = V          ' blue
    IF BI = 2 THEN R = V \\ 5 : G = V : BL = V \\ 5          ' green
    IF BI = 3 THEN R = V : G = V * 3 \\ 4 : BL = V \\ 8      ' gold
    OUT &H3C9, R : OUT &H3C9, G : OUT &H3C9, BL
  NEXT I
NEXT BI

' ===========================================================================
' Load the span-fill blitter into a CODE%() buffer (the classic TB idiom).
' Same 29-byte listing as CUBEASM.BAS: PUSH BP / MOV BP,SP / ES=A000 /
' deref the three pointer args into DI,CX,AL / REP STOSB / POP BP / RET 6.
' ===========================================================================
NB = 29
DIM CODE%((NB + 1) \\ 2)          ' enough INTEGER cells to hold NB bytes
SEGC = VARSEG(CODE%(0))
ADRC = VARPTR(CODE%(0))
DEF SEG = SEGC                    ' POKE the bytes into the CODE buffer's segment
FOR I = 0 TO NB - 1
  READ BYT
  POKE ADRC + I, BYT
NEXT I
' ---- BLITTER BYTES (the 29-byte hand-assembled REP STOSB span-filler) ----
DATA &H55, &H8B, &HEC, &HB8, &H00, &HA0, &H8E, &HC0
DATA &H8B, &H5E, &H08, &H8B, &H3F, &H8B, &H5E, &H06
DATA &H8B, &H0F, &H8B, &H5E, &H04, &H8B, &H07, &HF3
DATA &HAA, &H5D, &HC2, &H06, &H00

' --- clear the whole screen once (one 320-pixel span per row, colour 0) ---
FOR YY = 0 TO 199
  OF& = YY : OF& = OF& * 320
  LO& = OF& AND &HFFFF
  CALL FillSpan(LO&, 320, 0)
NEXT YY

DO
  ' --- erase LAST frame's bands first (full-width black spans), so bars
  '     never leave trails; on the first frame there is nothing to erase ---
  FOR BI = 0 TO NBAR - 1
    IF PT(BI) >= 0 THEN
      FOR J = 0 TO BH - 1
        OF& = PT(BI) + J : OF& = OF& * 320
        LO& = OF& AND &HFFFF
        CALL FillSpan(LO&, 320, 0)
      NEXT J
    END IF
  NEXT BI

  ' --- draw every bar bottom-up; overlapping is fine, later bars paint
  '     over earlier ones just like real copper lists resolved by priority ---
  FOR BI = 0 TO NBAR - 1
    TP = 84 + SN(PH(BI))          ' this frame's bar top (4..164)
    PT(BI) = TP
    FOR J = BH - 1 TO 0 STEP -1
      SH = 15 - ABS(J - 15)       ' shade 0..15..0: brightest at the centre
      C = BC(BI) + SH
      OF& = TP + J : OF& = OF& * 320
      LO& = OF& AND &HFFFF
      CALL FillSpan(LO&, 320, C)
    NEXT J
    PH(BI) = (PH(BI) + SP(BI)) AND 255
  NEXT BI

  DELAY .02
  K$ = INKEY$
LOOP UNTIL K$ = CHR$(27)

DEF SEG
REG 1, 3                          ' AH=00 set mode, AL=03h: back to 80x25 text
CALL INTERRUPT &H10
PRINT "COPPER - Amiga-style raster bars, span-filled via CALL ABSOLUTE."
END

' ---------------------------------------------------------------------------
' Fill CNT pixels of palette colour C starting at linear A000 offset T0& by
' calling the hand-assembled blitter. CALL ABSOLUTE arguments must be INTEGER
' scalar variables in real Turbo Basic (a LONG raises "Error 429: Integer
' scalar variable expected"), so the 0..63999 offset is folded to its exact
' low 16 bits as a signed INTEGER - the unsigned offset REP STOSB wants.
' DEF SEG selects the CODE segment; the blitter sets ES=A000 itself.
' ---------------------------------------------------------------------------
SUB FillSpan(T0&, CNT, C)
  SHARED SEGC, ADRC
  IF CNT < 1 THEN EXIT SUB
  OW = T0&
  IF T0& > 32767 THEN OW = T0& - 65536
  DEF SEG = SEGC                              ' point the CALL at the CODE bytes
  CALL ABSOLUTE ADRC (OW, CNT, C)            ' &offset%, &count%, &colour% (by ref)
END SUB
