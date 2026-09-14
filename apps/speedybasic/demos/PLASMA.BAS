' ===========================================================================
' PLASMA.BAS - old-school plasma animated ENTIRELY by VGA DAC palette
'              rotation: the frame is painted ONCE and never touched again.
'
' The classic demoscene trick: in mode 13h every pixel is just an INDEX into
' the 256-entry DAC colour table, so an image built from a smooth CYCLIC
' gradient can be animated by rewriting the 256 DAC entries with a moving
' phase offset - 768 tiny OUTs per frame instead of 64000 pixel writes.
'
'   1. enter mode 13h through the BIOS:  REG 1,&H13 : CALL INTERRUPT &H10
'   2. build a smooth cyclic palette from three phase-shifted sine ramps
'      folded to the DAC's 6-bit 0..63 range, loaded via OUT &H3C8 / &H3C9
'   3. paint ONE plasma frame: each pixel is a sum of sines of x and y,
'      folded to 0..255. SIN itself is called only 256 times to fill an
'      integer lookup table (64000 SIN calls would crawl); the row and
'      column wave contributions are then precomputed into RC()/CC() so the
'      per-pixel work is ONE add + AND mask, POKEd straight into segment
'      A000 with an incrementing LONG offset.
'   4. the animation loop rotates the palette: all 256 DAC entries are
'      rewritten each frame with a moving phase - ZERO framebuffer writes.
'
' Because both the palette and the plasma field are 256-cyclic, the AND-255
' folds are seamless: the colours wrap around with no visible edge.
' Press any key to exit back to text mode.
' ===========================================================================
DEFINT A-Z
DIM SN(255)                      ' sine table x63 (the only SIN calls made)
DIM PR(255), PG(255), PB(255)    ' base palette: 6-bit R,G,B per index
DIM RC(199)                      ' per-row plasma contribution (sines of y)
DIM CC(319)                      ' per-column plasma contribution (sines of x)

' --- 256-entry sine lookup table, scaled to -63..+63 ---
FOR A = 0 TO 255
  SN(A) = CINT(63 * SIN(A * 6.2831853 / 256))
NEXT A

' --- smooth cyclic palette: three sine ramps 120 degrees apart, 0..63 ---
FOR I = 0 TO 255
  PR(I) = (63 + SN(I)) \\ 2
  PG(I) = (63 + SN((I + 85) AND 255)) \\ 2
  PB(I) = (63 + SN((I + 170) AND 255)) \\ 2
NEXT I

' --- per-row / per-column plasma waves (sum of sines, integer only) ---
' pixel colour = (RC(y) + CC(x)) AND 255. The +512 bias keeps the sum
' positive; it vanishes in the AND fold because 512 is a multiple of 256.
FOR Y = 0 TO 199
  RC(Y) = 512 + SN((Y * 2) AND 255) + SN((Y * 5 + 128) AND 255) \\ 2
NEXT Y
FOR X = 0 TO 319
  CC(X) = SN((X * 3) AND 255) + SN((X * 7 + 64) AND 255) \\ 2
NEXT X

' --- enter VGA mode 13h (320x200, 256 colours) through the BIOS ---
REG 1, &H13                      ' AX = 0013h: AH=00 set mode, AL=13h
CALL INTERRUPT &H10

' --- load the base palette into the DAC (index port, then R,G,B triples) ---
OUT &H3C8, 0                     ' start at colour index 0 (auto-increments)
FOR I = 0 TO 255
  OUT &H3C9, PR(I) : OUT &H3C9, PG(I) : OUT &H3C9, PB(I)
NEXT I

' --- paint the plasma ONCE: 64000 POKEs, then the framebuffer is frozen ---
' The linear offset runs 0..63999, past an INTEGER's 32767 limit, so the
' running offset lives in the LONG variable OF& (POKE takes 0..65535).
DEF SEG = &HA000
OF& = 0
FOR Y = 0 TO 199
  RV = RC(Y)                     ' hoist the row wave out of the inner loop
  FOR X = 0 TO 319
    POKE OF&, (RV + CC(X)) AND 255
    OF& = OF& + 1
  NEXT X
NEXT Y

' --- animate by DAC rotation alone: no pixel is ever written again ---
PH = 0
DO
  PH = (PH + 2) AND 255          ' the moving phase offset
  OUT &H3C8, 0                   ' rewrite all 256 DAC entries in one burst
  FOR I = 0 TO 255
    J = (I + PH) AND 255
    OUT &H3C9, PR(J) : OUT &H3C9, PG(J) : OUT &H3C9, PB(J)
  NEXT I
  DELAY .02
  K$ = INKEY$
LOOP UNTIL K$ <> ""

' --- back to text mode (good TB hygiene: reset DEF SEG too) ---
DEF SEG
REG 1, 3
CALL INTERRUPT &H10
CLS
PRINT "PLASMA - one frame painted, then animated purely by DAC rotation."
END
