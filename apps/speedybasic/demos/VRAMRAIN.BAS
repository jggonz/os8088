' ===========================================================================
' VRAMRAIN.BAS - real-time "colorful rain" by poking B800 video RAM
'
' Each drop is a short vertical streak written DIRECTLY into the colour text
' buffer at &HB800 - no PRINT / no LOCATE - so 100+ drops animate smoothly. A
' drop is four cells: a bright-white head, a bright body, a dim tail, and an
' erased cell behind the tail. Speeds vary, colours are random. Any key stops.
' Runs unchanged in real Turbo Basic.
'
' Cell (row,col) on an 80-col screen is byte offset (row*80+col)*2:
'   even byte = character code, odd byte = attribute (bg*16 + fg)
' ===========================================================================
DEFINT A-Z
nd = 110                       ' number of raindrops
DIM rx(nd), ry(nd), rc(nd), rp(nd)
vidseg = &HB800
DEF SEG = vidseg
CLS
RANDOMIZE TIMER

' Scatter the drops over the screen so the first frame is already full.
FOR i = 1 TO nd
  GOSUB Respawn
  ry(i) = INT(RND * 25)
NEXT i

' --- animation loop: advance each drop, repaint only the cells that change ---
fc = 0
DO
  fc = fc + 1
  FOR i = 1 TO nd
    IF (fc MOD rp(i)) = 0 THEN
      newy = ry(i) + 1
      ' bright-white head
      IF newy <= 24 THEN
        vo = (newy * 80 + rx(i)) * 2
        POKE vo, 33 + INT(RND * 94)
        POKE vo + 1, 15
      END IF
      ' bright body, one row up
      hy = newy - 1
      IF hy >= 0 AND hy <= 24 THEN
        vo = (hy * 80 + rx(i)) * 2
        POKE vo, 33 + INT(RND * 94)
        POKE vo + 1, rc(i)
      END IF
      ' dim tail, two rows up (dark shade = bright colour - 8)
      ty = newy - 2
      IF ty >= 0 AND ty <= 24 THEN
        vo = (ty * 80 + rx(i)) * 2
        POKE vo, 33 + INT(RND * 94)
        POKE vo + 1, rc(i) - 8
      END IF
      ' erase the cell behind the tail
      ey = newy - 3
      IF ey >= 0 AND ey <= 24 THEN
        vo = (ey * 80 + rx(i)) * 2
        POKE vo, 32
        POKE vo + 1, 0
      END IF
      ry(i) = newy
      IF newy > 27 THEN GOSUB Respawn
    END IF
  NEXT i
  DELAY .03
  k$ = INKEY$
LOOP UNTIL k$ <> ""

DEF SEG
CLS
PRINT "Rain stopped."
END

' --- (re)spawn drop i at the top in a random column / colour / speed ---
Respawn:
  rx(i) = INT(RND * 80)
  ry(i) = 0
  rc(i) = 9 + INT(RND * 7)      ' bright colours 9..15
  rp(i) = 1 + INT(RND * 3)      ' move every 1, 2, or 3 frames
RETURN
