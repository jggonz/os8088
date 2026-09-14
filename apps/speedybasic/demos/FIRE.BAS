' ===========================================================================
' FIRE.BAS - the classic demoscene fire effect in VGA 256-colour mode 13h
'
' The three ingredients of every late-80s fire routine, in pure Turbo Basic:
'   1. enter mode 13h through the BIOS:  REG 1,&H13 : CALL INTERRUPT &H10
'   2. program a smooth 64-entry heat gradient straight into the VGA DAC
'      (OUT &H3C8 selects the index, three OUT &H3C9 write 6-bit R,G,B and
'      auto-increment) - black -> deep red -> orange -> yellow -> white, so a
'      flame value 0..63 IS its palette index; no lookup table needed.
'   3. keep a coarse "heat" field, seed random hot spots on its bottom row,
'      let heat rise by averaging the cells below (minus a fixed cooling),
'      and blast the band into the A000 linear framebuffer with POKE.
'
' RESOLUTION TRADE: the flame lives in a 160x40 INTEGER field covering the
' bottom 80 scanlines (y = 120..199), i.e. the 320x80 band downsampled 2x.
' Each cell paints a 2x2 pixel block, so one frame costs 6400 cell updates
' plus 25600 stores instead of 25600 updates plus 25600 stores at 1x1 -
' roughly a quarter of the arithmetic. Fire is naturally blurry, so the fat
' pixels read as glow, not blockiness; this is exactly the trade every real
' 8086 fire demo made.
'
' ADDRESSING: pixel (x,y) sits at A000:(y*320+x). Offsets in the band run
' 38400..63999, past 32767, so all offset arithmetic lives in LONG variables
' (and note real Turbo Basic has NO long literal suffix - a trailing & would
' start an octal literal - so the base offset is built up in B0& first). The
' paint loop never multiplies per pixel: O& walks the top pixel row of the
' cell row (+2 per cell) while P2& walks the row 320 bytes below it, and a
' single +320 at the end of each cell row hops both over the second pixel
' row just painted. Press any key to exit (restores 80x25 text mode).
' ===========================================================================
DEFINT A-Z
RANDOMIZE TIMER

DIM F(39, 159)                   ' the flame field: F(row, col), row 39 = bottom

' --- enter VGA mode 13h (320x200, 256 colours) through the BIOS ---
REG 1, &H13                      ' AX = 0013h: AH=00 set video mode, AL=13h
CALL INTERRUPT &H10

' --- program the 64-entry heat gradient into the DAC (6-bit components) ---
' 0..21  black -> deep red      (R ramps 0..63)
' 22..42 deep red -> yellow     (G ramps up under full R)
' 43..63 yellow -> white        (B ramps up under full R+G)
OUT &H3C8, 0                     ' start writing at colour index 0
FOR I = 0 TO 63
  IF I < 22 THEN
    R = I * 3 : G = 0 : B = 0
  ELSEIF I < 43 THEN
    R = 63 : G = (I - 21) * 3 : B = 0
  ELSE
    R = 63 : G = 63 : B = (I - 42) * 3
  END IF
  OUT &H3C9, R : OUT &H3C9, G : OUT &H3C9, B   ' auto-increments the index
NEXT I

B0& = 120                        ' band top scanline...
B0& = B0& * 320                  ' ...as a linear offset (38400 - needs a LONG)
DEF SEG = &HA000                 ' all POKEs below hit the mode-13h framebuffer

DO
  ' --- seed the bottom row: random hot spots with cold gaps for flicker ---
  FOR CX = 0 TO 159
    IF RND < .2 THEN
      F(39, CX) = 0              ' a cold gap - makes the flame dance
    ELSE
      F(39, CX) = 40 + INT(RND * 24)   ' 40..63 = orange .. white heat
    END IF
  NEXT CX

  ' --- propagate the heat upward. Each cell becomes the average of the three
  ' cells below it (centre weighted double), minus 1 cooling per row. Rows are
  ' walked BOTTOM-UP, so row 38 reads the fresh seed row and every row above
  ' reads the row just recomputed - the whole column updates in one frame and
  ' the flame is fully developed from frame one instead of crawling up. ---
  FOR ROW = 38 TO 0 STEP -1
    RB = ROW + 1
    FOR CX = 0 TO 159
      CL = CX - 1 : IF CL < 0 THEN CL = 159      ' wrap the edges so the
      CR = CX + 1 : IF CR > 159 THEN CR = 0      ' flame has no cold seams
      V = (F(RB, CL) + F(RB, CX) + F(RB, CX) + F(RB, CR)) \\ 4 - 1
      IF V < 0 THEN V = 0
      F(ROW, CX) = V
    NEXT CX
  NEXT ROW

  ' --- paint the band: each cell is a 2x2 block, addressed incrementally ---
  O& = B0&                       ' running offset, top pixel row of the cell row
  FOR ROW = 0 TO 39
    P2& = O& + 320               ' running offset, bottom pixel row of the block
    FOR CX = 0 TO 159
      C = F(ROW, CX)             ' heat value 0..63 = DAC gradient index
      POKE O&, C : POKE O& + 1, C
      POKE P2&, C : POKE P2& + 1, C
      O& = O& + 2 : P2& = P2& + 2
    NEXT CX
    O& = O& + 320                ' O& sits at P2&'s start; skip the painted row
  NEXT ROW

  DELAY .01                      ' let the retrace catch a full frame
  K$ = INKEY$                    ' poll once per frame; any key exits
LOOP UNTIL K$ <> ""

DEF SEG
REG 1, 3                         ' AX = 0003h: back to 80x25 colour text
CALL INTERRUPT &H10
PRINT "FIRE - 64-colour DAC gradient + heat propagation, POKEd into A000."
END
