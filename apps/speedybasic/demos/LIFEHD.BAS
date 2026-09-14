' ===========================================================================
' LIFEHD.BAS - Conway's Game of Life in high resolution (SCREEN 9, 640x350)
'
' The graphics remake of LIFE.BAS: a 91x48 soup of 7x7-pixel cells evolving
' under the classic rules (survive with 2 or 3 live neighbours, birth with
' exactly 3). Cells are painted as LINE ... BF filled boxes and only cells
' that CHANGED are repainted, so generations tick by quickly. Cells "cool"
' as they age: a birth flashes white, then fades light-cyan -> light-blue ->
' deep blue while it survives, so stable colonies glow blue under a white
' froth of new growth.
'
' The hot loop leans on a classic BASIC Life trick: relationals evaluate to
' -1 (true) / 0 (false), so the whole rule collapses into one assignment,
'   ng = -(n = 3 OR (n = 2 AND alive))
' and the population is tracked incrementally on births/deaths instead of
' being re-counted every pass - both keep the 4368-cell scan tight.
'
' The HUD (generation + population) is drawn with the same 5x7 bitmap font
' LANDERHD.BAS uses, because PRINT does not draw in graphics modes.
' Runs unchanged in real Turbo Basic. Any key stops.
' ===========================================================================
DEFINT A-Z

NR = 48 : NC = 91 : CS = 7         ' grid rows/cols, cell size in pixels
DIM g(NR + 1, NC + 1), ng(NR + 1, NC + 1), age(NR + 1, NC + 1)
' the extra 0 / NR+1 / NC+1 ring stays 0 = a permanently dead border

' ---- 5x7 bitmap font: glyphs 0-9 then A-Z (5 bits wide, 7 rows) ----
DIM FONT%(35, 6)
FOR GI = 0 TO 35 : FOR GR = 0 TO 6 : READ FONT%(GI, GR) : NEXT : NEXT
DATA 14,17,19,21,25,17,14
DATA 4,12,4,4,4,4,14
DATA 14,17,1,2,4,8,31
DATA 31,2,4,2,1,17,14
DATA 2,6,10,18,31,2,2
DATA 31,16,30,1,1,17,14
DATA 6,8,16,30,17,17,14
DATA 31,1,2,4,8,8,8
DATA 14,17,17,14,17,17,14
DATA 14,17,17,15,1,2,12
DATA 14,17,17,31,17,17,17
DATA 30,17,17,30,17,17,30
DATA 14,17,16,16,16,17,14
DATA 28,18,17,17,17,18,28
DATA 31,16,16,30,16,16,31
DATA 31,16,16,30,16,16,16
DATA 14,17,16,23,17,17,15
DATA 17,17,17,31,17,17,17
DATA 14,4,4,4,4,4,14
DATA 7,2,2,2,2,18,12
DATA 17,18,20,24,20,18,17
DATA 16,16,16,16,16,16,31
DATA 17,27,21,21,17,17,17
DATA 17,17,25,21,19,17,17
DATA 14,17,17,17,17,17,14
DATA 30,17,17,30,16,16,16
DATA 14,17,17,17,21,18,13
DATA 30,17,17,30,20,18,17
DATA 15,16,16,14,1,1,30
DATA 31,4,4,4,4,4,4
DATA 17,17,17,17,17,17,14
DATA 17,17,17,17,17,10,4
DATA 17,17,17,21,21,27,17
DATA 17,17,10,4,10,17,17
DATA 17,17,10,4,4,4,4
DATA 31,1,2,4,8,16,31

SCREEN 9
CLS
RANDOMIZE TIMER

' --- seed a random soup (about 1 cell in 3 alive) and paint it ---
alive = 0
FOR r = 1 TO NR
  FOR c = 1 TO NC
    IF RND < .32 THEN
      g(r, c) = 1 : age(r, c) = 1 : alive = alive + 1
      x = (c - 1) * CS : y = (r - 1) * CS
      LINE (x, y)-(x + CS - 1, y + CS - 1), 15, BF
    END IF
  NEXT c
NEXT r

' --- HUD chrome under the playfield (playfield is y 0..335) ---
LINE (0, 337)-(639, 337), 8
CALL DrawText(4, 340, "LIFE HD", 1, 15)
CALL DrawText(64, 340, "GEN", 1, 7)
CALL DrawText(160, 340, "POP", 1, 7)
CALL DrawText(558, 340, "ANY KEY QUITS", 1, 8)

' --- generations: score neighbours, apply the rules, repaint changes ---
gen& = 0
DO
  gen& = gen& + 1
  ' relationals are -1/0 in TB, so one assignment IS the whole Life rule
  FOR r = 1 TO NR
    FOR c = 1 TO NC
      n = g(r - 1, c - 1) + g(r - 1, c) + g(r - 1, c + 1) + g(r, c - 1) + g(r, c + 1) + g(r + 1, c - 1) + g(r + 1, c) + g(r + 1, c + 1)
      ng(r, c) = -(n = 3 OR (n = 2 AND g(r, c) = 1))
    NEXT c
  NEXT r

  ' copy the new generation back, repainting only where something changed;
  ' unchanged survivors just cool one colour step (then stay put)
  FOR r = 1 TO NR
    FOR c = 1 TO NC
      IF ng(r, c) <> g(r, c) THEN
        g(r, c) = ng(r, c)
        x = (c - 1) * CS : y = (r - 1) * CS
        IF g(r, c) = 1 THEN
          age(r, c) = 1 : alive = alive + 1    ' newborn = bright white
          LINE (x, y)-(x + CS - 1, y + CS - 1), 15, BF
        ELSE
          age(r, c) = 0 : alive = alive - 1
          LINE (x, y)-(x + CS - 1, y + CS - 1), 0, BF
        END IF
      ELSEIF g(r, c) = 1 THEN
        a = age(r, c) + 1
        IF a < 5 THEN                  ' cool: white -> 11 -> 9 -> 1, then stop
          age(r, c) = a
          k = 11
          IF a = 3 THEN k = 9
          IF a = 4 THEN k = 1
          x = (c - 1) * CS : y = (r - 1) * CS
          LINE (x, y)-(x + CS - 1, y + CS - 1), k, BF
        END IF
      END IF
    NEXT c
  NEXT r

  ' --- refresh the HUD numbers ---
  LINE (88, 340)-(150, 346), 0, BF
  CALL DrawText(88, 340, MID$(STR$(gen&), 2), 1, 15)
  LINE (184, 340)-(230, 346), 0, BF
  CALL DrawText(184, 340, MID$(STR$(alive), 2), 1, 14)

  DELAY .02
  k$ = INKEY$
LOOP UNTIL k$ <> ""

SCREEN 0 : WIDTH 80 : CLS
PRINT "Life halted after"; gen&; "generations."
END

' --- draw T$ at pixel (PX,PY) in colour C, SCL x magnification (5x7 font) ---
SUB DrawText(PX%, PY%, T$, SCL%, C%)
  SHARED FONT%()
  COL% = PX%
  FOR CI% = 1 TO LEN(T$)
    CC% = ASC(MID$(T$, CI%, 1))
    GIDX% = -1
    IF CC% >= 48 AND CC% <= 57 THEN GIDX% = CC% - 48
    IF CC% >= 65 AND CC% <= 90 THEN GIDX% = CC% - 55
    IF CC% >= 97 AND CC% <= 122 THEN GIDX% = CC% - 87
    IF GIDX% >= 0 THEN
      FOR GR% = 0 TO 6
        BITS% = FONT%(GIDX%, GR%)
        BV% = 16
        FOR GC% = 0 TO 4
          IF (BITS% AND BV%) <> 0 THEN
            XX% = COL% + GC% * SCL%
            YY% = PY% + GR% * SCL%
            IF SCL% = 1 THEN
              PSET (XX%, YY%), C%
            ELSE
              LINE (XX%, YY%)-(XX% + SCL% - 1, YY% + SCL% - 1), C%, BF
            END IF
          END IF
          BV% = BV% \\ 2
        NEXT
      NEXT
    END IF
    COL% = COL% + 6 * SCL%
  NEXT
END SUB
