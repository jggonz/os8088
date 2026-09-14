' ===========================================================================
' MANDEL.BAS - the Mandelbrot set rendered straight into VGA mode 13h video
'              memory, then brought to life with endless VGA DAC palette
'              cycling: the classic 256-colour fractal demo, in pure
'              Turbo Basic.
'
'   1. enter 320x200x256 mode 13h through the BIOS:
'         REG 1, &H13 : CALL INTERRUPT &H10
'   2. program a seamless CYCLIC rainbow gradient into DAC entries 1..255
'      (OUT &H3C8 selects the index, three OUT &H3C9 writes set 6-bit R,G,B,
'      then the index auto-increments). Entry 0 stays black - that is the
'      colour of points INSIDE the set, which never move.
'   3. render scanline by scanline with the classic escape-time iteration
'      z <- z*z + c in floating point (MAXIT = 40), mapping the iteration
'      count of escaping points onto the 1..255 gradient and POKEing each
'      pixel byte into segment A000 at an incrementally-stepped LONG offset.
'   4. when the render finishes (or a key aborts it), rotate DAC entries
'      1..255 by a step each frame - the colour bands flow endlessly through
'      the fractal without redrawing a single pixel. Press any key to leave.
'
' Two classic speed tricks keep the full render plausible on a real 8086:
'   - HORIZONTAL HALF RESOLUTION: only every 2nd column is computed; the
'     pixel byte is POKEd twice (320x200 look for 160x200 work).
'   - MIRROR SYMMETRY: the set is symmetric about the real axis, so the
'     picture is rendered from the middle OUTWARD - each computed scanline
'     is also POKEd to its mirror row. Half the rows to compute, and the
'     interesting middle of the picture appears first.
' INKEY$ is polled ONCE PER SCANLINE PAIR during the render, so a keypress
' aborts a long render and skips straight to the palette cycling.
' ===========================================================================
DEFINT A-Z
DIM R(255), G(255), B(255)       ' the cyclic gradient (6-bit DAC values)

MAXIT = 40                       ' escape-time iteration limit

' --- enter VGA mode 13h (320x200, 256 colours) through the BIOS ---
REG 1, &H13                      ' AX = 0013h: AH=00 set mode, AL=13h
CALL INTERRUPT &H10

' --- build a seamless cyclic rainbow: three sine waves 120 degrees apart.
'     The period is exactly 255 entries, so rotating 1..255 never shows a
'     seam where the gradient wraps around. ---
FOR I = 1 TO 255
  A! = (I - 1) * 6.2831853 / 255
  R(I) = INT(31.5 + 31.5 * SIN(A!))
  G(I) = INT(31.5 + 31.5 * SIN(A! + 2.0943951))
  B(I) = INT(31.5 + 31.5 * SIN(A! + 4.1887902))
NEXT I

' --- program the DAC: index 0 = black (inside the set), then 1..255 ---
OUT &H3C8, 0                     ' start at index 0; &H3C9 auto-increments
OUT &H3C9, 0 : OUT &H3C9, 0 : OUT &H3C9, 0
FOR I = 1 TO 255
  OUT &H3C9, R(I) : OUT &H3C9, G(I) : OUT &H3C9, B(I)
NEXT I

' ===========================================================================
' RENDER. View window: real axis -2.5 .. +1.0 (the whole set plus breathing
' room), imaginary axis +-1.194 centred between rows 99 and 100 so the two
' halves are exact mirrors. Scanline pairs are computed middle-first.
' ===========================================================================
SC! = 3.5 / 320                  ' real step per pixel column
DEF SEG = &HA000                 ' every POKE below lands in video memory

FOR H = 0 TO 99                  ' 100 scanline pairs, middle outward
  YR = 99 - H                    ' upper row of the pair (99 down to 0)
  CI! = (99.5 - YR) * .012      ' its imaginary part (+.006 .. +1.194)
  OT& = YR : OT& = OT& * 320     ' upper-row offset
  OB& = 199 - YR : OB& = OB& * 320   ' mirror-row offset (63680 needs LONG)
  FOR XC = 0 TO 318 STEP 2       ' every 2nd column, pixel byte POKEd twice
    CR! = -2.5 + XC * SC!
    ' --- escape-time iteration: z <- z*z + c until |z| > 2 or MAXIT ---
    ZX! = 0 : ZY! = 0 : IT = 0
    DO
      XT! = ZX! * ZX! - ZY! * ZY! + CR!
      ZY! = 2 * ZX! * ZY! + CI!
      ZX! = XT!
      IT = IT + 1
    LOOP UNTIL IT = MAXIT OR ZX! * ZX! + ZY! * ZY! > 4
    ' inside the set -> DAC 0 (black); escaped -> spread over the gradient
    IF IT = MAXIT THEN C = 0 ELSE C = 1 + ((IT * 6) MOD 255)
    POKE OT&, C : POKE OT& + 1, C      ' upper row, both columns
    POKE OB&, C : POKE OB& + 1, C      ' mirrored lower row
    OT& = OT& + 2 : OB& = OB& + 2
  NEXT XC
  K$ = INKEY$                    ' one key poll per scanline pair
  IF K$ <> "" THEN EXIT FOR      ' abort straight into the palette cycling
NEXT H

' ===========================================================================
' PALETTE CYCLING. Rotate DAC entries 1..255 by 4 positions per frame: entry
' I is reloaded from gradient slot I+PH (wrapped inside 1..255). The pixels
' never change - only the colours mapped onto them - so a full-screen
' "animation" costs 765 OUTs per frame. Any key exits.
' ===========================================================================
PH = 0
DO
  PH = PH + 4 : IF PH >= 255 THEN PH = PH - 255
  OUT &H3C8, 1                   ' burst-write entries 1..255
  FOR I = 1 TO 255
    SI = I + PH : IF SI > 255 THEN SI = SI - 255
    OUT &H3C9, R(SI) : OUT &H3C9, G(SI) : OUT &H3C9, B(SI)
  NEXT I
  DELAY .02
  K$ = INKEY$
LOOP UNTIL K$ <> ""

DEF SEG
REG 1, 3                         ' AH=00 set mode, AL=03 -> 80x25 text
CALL INTERRUPT &H10
PRINT "MANDEL - escape-time Mandelbrot POKEd into A000, DAC-cycled to life."
END
