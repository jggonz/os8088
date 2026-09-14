' ===========================================================================
' LIFE.BAS - Conway's Game of Life on the 80x25 text screen
'
' A random "soup" evolves under the classic rules: a live cell survives with
' 2 or 3 live neighbours, an empty cell is born with exactly 3. The 80x24
' playfield is drawn by POKEing cells straight into B800 text video RAM, and
' only cells that CHANGED are repainted, so generations tick by quickly.
' Births flash bright white, then settle to green. Any key stops.
' Runs unchanged in real Turbo Basic.
'
' Cell (row,col) on an 80-col screen is byte offset (row*80+col)*2:
'   even byte = character code, odd byte = attribute (bg*16 + fg)
' ===========================================================================
DEFINT A-Z
DIM g(25, 81), ng(25, 81)      ' playfield rows 1..24, cols 1..80; the extra
                               ' 0 / 25 / 81 ring stays 0 = a dead border
vidseg = &HB800
DEF SEG = vidseg
CLS
RANDOMIZE TIMER

' --- seed a random soup (about 1 cell in 3 alive) and paint it ---
FOR r = 1 TO 24
  FOR c = 1 TO 80
    IF RND < .32 THEN
      g(r, c) = 1
      vo = ((r - 1) * 80 + c - 1) * 2
      POKE vo, 219                   ' CP437 full block
      POKE vo + 1, 10                ' bright green
    END IF
  NEXT c
NEXT r

' --- generations: score neighbours, apply the rules, repaint changes ---
gen = 0
DO
  gen = gen + 1
  alive = 0
  FOR r = 1 TO 24
    FOR c = 1 TO 80
      n = g(r - 1, c - 1) + g(r - 1, c) + g(r - 1, c + 1)
      n = n + g(r, c - 1) + g(r, c + 1)
      n = n + g(r + 1, c - 1) + g(r + 1, c) + g(r + 1, c + 1)
      IF g(r, c) = 1 THEN
        IF n = 2 OR n = 3 THEN ng(r, c) = 1 ELSE ng(r, c) = 0
      ELSE
        IF n = 3 THEN ng(r, c) = 1 ELSE ng(r, c) = 0
      END IF
      alive = alive + ng(r, c)
    NEXT c
  NEXT r

  ' copy the new generation back, poking only where something changed:
  ' births flash white, survivors settle to green, deaths blank out
  FOR r = 1 TO 24
    FOR c = 1 TO 80
      vo = ((r - 1) * 80 + c - 1) * 2
      IF ng(r, c) <> g(r, c) THEN
        g(r, c) = ng(r, c)
        IF g(r, c) = 1 THEN
          POKE vo, 219
          POKE vo + 1, 15            ' newborn = bright white
        ELSE
          POKE vo, 32
          POKE vo + 1, 0
        END IF
      ELSEIF g(r, c) = 1 THEN
        POKE vo + 1, 10              ' survivor = bright green
      END IF
    NEXT c
  NEXT r

  ' --- status line on row 25 (white on blue), padded to a full row ---
  msg$ = " CONWAY'S LIFE   GEN" + STR$(gen) + "   ALIVE" + STR$(alive) + "   ANY KEY QUITS"
  IF LEN(msg$) < 80 THEN msg$ = msg$ + SPACE$(80 - LEN(msg$))
  FOR i = 1 TO 80
    vo = (24 * 80 + i - 1) * 2
    POKE vo, ASC(MID$(msg$, i, 1))
    POKE vo + 1, &H1F
  NEXT i

  DELAY .05
  k$ = INKEY$
LOOP UNTIL k$ <> ""

DEF SEG
CLS
PRINT "Life halted after"; gen; "generations."
END
