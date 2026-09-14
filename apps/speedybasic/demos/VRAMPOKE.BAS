' ===========================================================================
' VRAMPOKE.BAS - direct video-RAM screen paint (the classic B800 idiom)
'
' Every cell is set with TWO memory stores and NO PRINT/LOCATE call, so the
' whole 80x25 screen repaints far faster than 2000 LOCATE+PRINT pairs. We
' then read the live 18.2 Hz BIOS tick out of the BIOS Data Area (0040:006C)
' to prove the BDA is dynamic.  Runs unchanged in real Turbo Basic.
'
' Cell (row,col) on an 80-column screen is at byte offset (row*80+col)*2:
'   even byte = CP437 character code, odd byte = attribute
'   attribute = bg*16 + fg   (fg 0-15, bg 0-7, bit7 = blink)
' ===========================================================================
DEFINT A-Z
vidseg = &HB800                  ' color text video RAM (&HB000 for mono)
DEF SEG = vidseg
CLS

' --- fill all 2000 cells with a rolling color block pattern, fast ---
FOR row = 0 TO 24
  FOR col = 0 TO 79
    vo = (row * 80 + col) * 2
    ch = 65 + ((row + col) MOD 26)      ' rolling A..Z (CP437 code)
    ink = (1 + (col MOD 7)) * 16 + 14   ' bg = col MOD 7 (1..7), fg = yellow
    POKE vo, ch                         ' even byte = character
    POKE vo + 1, ink                    ' odd byte  = attribute
  NEXT col
NEXT row

' --- read the live BIOS 18.2 Hz tick straight out of the BDA ---
' Long math is forced by the long (&-suffixed) VARIABLES b0&..b3&, not by a
' literal suffix: real Turbo Basic has no "256&" constant (a trailing & starts an
' octal literal), so each weight multiplies a long variable instead.
DEF SEG = &H40
b0& = PEEK(&H6C) : b1& = PEEK(&H6D) : b2& = PEEK(&H6E) : b3& = PEEK(&H6F)
tk& = b0& + b1& * 256 + b2& * 65536 + b3& * 16777216
mode = PEEK(&H49)                ' 3 = 80x25 color text
cols = PEEK(&H4A) + PEEK(&H4B) * 256
DEF SEG = vidseg

' --- write a status line by poking it cell-by-cell (white on blue) ---
msg$ = " POKED 2000 CELLS DIRECTLY TO B800 - BIOS TICKS:" + STR$(tk&) + " "
FOR i = 1 TO LEN(msg$)
  vo = ((24 * 80) + (i - 1)) * 2
  POKE vo, ASC(MID$(msg$, i, 1))
  POKE vo + 1, &H1F                     ' white (15) on blue (1)
NEXT i

' --- reset the segment when done (good TB hygiene) ---
DEF SEG
