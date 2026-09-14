' ===========================================================================
' SNAKE.BAS - text-mode snake where the VIDEO RAM *IS* the game state
'
' There is NO board array. The 80x25 colour text buffer at &HB800 is the one
' and only copy of the playfield: walls, snake body and food exist purely as
' character bytes in video RAM. Collision detection is a single PEEK of the
' target cell BEFORE the head moves - a wall or body byte means death, the
' food byte means grow - so the screen doubles as the collision map, the
' classic trick on machines with a memory-mapped display.
'
' The ONE piece of state kept outside VRAM is a small circular INTEGER queue
' of the body cells' byte offsets, oldest first. That is QUEUE state, not
' BOARD state: the board itself never leaves the screen; the queue only
' remembers WHICH cell is the tail so it can be erased without scanning or
' shadowing the playfield (you cannot recover erase ORDER from a bitmap).
'
' Pacing comes from the BIOS 18.2 Hz tick dword at 0040:006C, PEEKed a byte
' at a time into a LONG exactly as VRAMPOKE.BAS does - frames wait for real
' elapsed ticks, not a calibrated empty delay loop. Two deliberate escape
' hatches keep the wait from ever hanging: a pending keypress ends it early
' (steering feels instant; holding a key speeds the snake up), and a large
' iteration bound bails out ONLY if the tick counter provably never moves
' (a stalled or emulated clock). Steer with the arrow keys or WASD; ESC
' quits. Runs unchanged in real Turbo Basic.
'
' Cell (row,col) on an 80-col screen is byte offset (row*80+col)*2:
'   even byte = CP437 character code, odd byte = attribute (bg*16 + fg)
' ===========================================================================
DEFINT A-Z

' The four cell bytes - everything the game "knows" is one of these:
CB = 178                       ' border wall (shaded block)
CS = 219                       ' snake body (solid block)
CF = 42                        ' food (*)
CE = 32                        ' empty (space)

DIM Q(2047)                    ' circular queue of body-cell VRAM offsets,
                               ' oldest (tail) first. 2048 slots exceed the
                               ' 22x78 playfield, so it can never overflow.
                               ' This is erase-order bookkeeping ONLY - the
                               ' board lives in video RAM and nowhere else.

TP = 2                         ' BIOS ticks per frame (2/18.2 s = ~9 fps)
WB = 25000                     ' stalled-clock guard: bail out of a frame wait
                               ' only when the tick NEVER advances (WaitTick)

VS = &HB800                    ' colour text video RAM
DEF SEG = VS
CLS
RANDOMIZE TIMER

' --- walls: rows 0 and 23, columns 0 and 79, poked straight into VRAM ---
FOR I = 0 TO 79
  VO = I * 2 : POKE VO, CB : POKE VO + 1, &H19
  VO = (23 * 80 + I) * 2 : POKE VO, CB : POKE VO + 1, &H19
NEXT I
FOR I = 1 TO 22
  VO = (I * 80) * 2 : POKE VO, CB : POKE VO + 1, &H19
  VO = (I * 80 + 79) * 2 : POKE VO, CB : POKE VO + 1, &H19
NEXT I

' --- status row (row 24): help text + live score, also poked cell-by-cell ---
MA = &H70 : GOSUB FillStatus
MS$ = " SNAKE - EAT * TO GROW - ARROWS/WASD STEER - ESC QUITS" : MC = 0 : GOSUB PokeMsg
MS$ = "SCORE:" : MC = 62 : GOSUB PokeMsg
PTS = 0 : GOSUB ShowScore

' --- the snake: three cells long, mid-screen, heading right ---
HR = 12 : HC = 20 : DR = 0 : DC = 1
QT = 0 : QH = 2
FOR I = 0 TO 2
  VO = (12 * 80 + 18 + I) * 2
  Q(I) = VO
  POKE VO, CS : POKE VO + 1, 2          ' dark green body
NEXT I
POKE Q(QH) + 1, 10                      ' bright green head

GOSUB PlaceFood

' ===========================================================================
' Main loop. GS = 0 playing, 1 crashed, 2 quit via ESC.
' ===========================================================================
GS = 0
DO
  ' --- steering: INKEY$ returns 1 char for ASCII keys, CHR$(0)+scan code
  ' for the arrows. A 180-degree reversal is ignored (you cannot back into
  ' your own neck); any other direction takes effect on this frame's move.
  K$ = INKEY$
  IF K$ <> "" THEN
    NDR = DR : NDC = DC
    IF LEN(K$) = 2 THEN
      EC = ASC(RIGHT$(K$, 1))           ' extended scan code
      IF EC = 72 THEN NDR = -1 : NDC = 0
      IF EC = 80 THEN NDR = 1 : NDC = 0
      IF EC = 75 THEN NDR = 0 : NDC = -1
      IF EC = 77 THEN NDR = 0 : NDC = 1
    ELSE
      KC = ASC(UCASE$(K$))
      IF KC = 87 THEN NDR = -1 : NDC = 0            ' W
      IF KC = 83 THEN NDR = 1 : NDC = 0             ' S
      IF KC = 65 THEN NDR = 0 : NDC = -1            ' A
      IF KC = 68 THEN NDR = 0 : NDC = 1             ' D
      IF KC = 27 THEN GS = 2                        ' ESC quits
    END IF
    IF NDR <> -DR OR NDC <> -DC THEN DR = NDR : DC = NDC
  END IF

  IF GS = 0 THEN
    ' --- collision test = ONE PEEK of the target cell. The screen IS the
    ' map: whatever byte lives there tells us everything we need to know.
    NR = HR + DR : NC = HC + DC
    VO = (NR * 80 + NC) * 2
    TC = PEEK(VO)
    IF TC = CB OR TC = CS THEN
      GS = 1                            ' wall or own body - dead
    ELSE
      IF TC = CF THEN
        PTS = PTS + 10                  ' food: score and GROW - the tail is
        GOSUB ShowScore                 ' NOT erased this frame, so the body
        GOSUB PlaceFood                 ' gets one cell longer
      ELSE
        EO = Q(QT) : QT = (QT + 1) AND 2047
        POKE EO, CE : POKE EO + 1, 7    ' normal step: erase the tail cell
      END IF
      POKE Q(QH) + 1, 2                 ' the old head dims to body colour
      QH = (QH + 1) AND 2047 : Q(QH) = VO
      POKE VO, CS : POKE VO + 1, 10     ' write the new head
      HR = NR : HC = NC
      GOSUB WaitTick
    END IF
  END IF
LOOP UNTIL GS <> 0

IF GS = 1 THEN
  MA = &H4F : GOSUB FillStatus          ' white-on-red status row
  MS$ = " GAME OVER - FINAL SCORE:" + STR$(PTS) + " - PRESS ANY KEY " : MC = 1 : GOSUB PokeMsg
  DO : K$ = INKEY$ : LOOP UNTIL K$ <> ""
END IF

DEF SEG                                 ' restore the default segment (good
END                                     ' TB hygiene); the board stays visible

' ---------------------------------------------------------------------------
' PokeMsg: write MS$ onto the status row at column MC with attribute MA.
' ---------------------------------------------------------------------------
PokeMsg:
  FOR MI = 1 TO LEN(MS$)
    MV = (24 * 80 + MC + MI - 1) * 2
    POKE MV, ASC(MID$(MS$, MI, 1))
    POKE MV + 1, MA
  NEXT MI
RETURN

' --- FillStatus: clear the whole status row to spaces with attribute MA ---
FillStatus:
  FOR MI = 0 TO 79
    MV = (24 * 80 + MI) * 2
    POKE MV, CE : POKE MV + 1, MA
  NEXT MI
RETURN

' --- ShowScore: repaint the score digits next to the SCORE: label ---
ShowScore:
  MS$ = STR$(PTS) + " " : MC = 68 : MA = &H70 : GOSUB PokeMsg
RETURN

' ---------------------------------------------------------------------------
' PlaceFood: drop food on a random EMPTY cell. Occupancy is tested by
' PEEKing the cell - the board is the screen, remember. RND is folded to
' the 22x78 playfield; a bounded number of re-rolls is followed by a
' deterministic first-free-cell scan, so even a degenerate RND stream
' (or a nearly-full board) can never loop forever. If even the scan finds
' no free cell (the snake fills the whole playfield), NO food is poked:
' overwriting a queued body cell would corrupt the VRAM board (the head
' could "eat" its own body and the tail eraser would erase the food).
' ---------------------------------------------------------------------------
PlaceFood:
  FT = 0
  DO
    FT = FT + 1
    FR = 1 + INT(RND * 22)              ' playfield rows 1..22
    FC = 1 + INT(RND * 78)              ' playfield cols 1..78
    FO = (FR * 80 + FC) * 2
  LOOP UNTIL PEEK(FO) = CE OR FT >= 25
  FF = 1
  IF PEEK(FO) <> CE THEN
    FF = 0                              ' all re-rolls hit occupied cells:
    SY = 1                              ' scan for the first free one, and
    DO WHILE SY <= 22 AND FF = 0        ' stop as soon as it is found
      SX = 1
      DO WHILE SX <= 78 AND FF = 0
        MV = (SY * 80 + SX) * 2
        IF PEEK(MV) = CE THEN FO = MV : FF = 1
        SX = SX + 1
      LOOP
      SY = SY + 1
    LOOP
  END IF
  IF FF = 1 THEN POKE FO, CF : POKE FO + 1, 12   ' light-red *
RETURN

' ---------------------------------------------------------------------------
' WaitTick: pace one frame off the BIOS 18.2 Hz tick dword at 0040:006C.
' The four bytes are PEEKed into LONG variables (real TB has no "256&"-type
' long literal - a trailing & starts an octal constant - so the &-suffixed
' VARIABLES force long math). We wait until the tick advances TP counts:
' real elapsed time, NOT a calibrated empty loop. Two extra exits keep the
' wait from hanging: a pending keypress (INSTAT is non-destructive) ends it
' early so steering feels instant (so a held, repeating key speeds the
' snake up - deliberate), and the WI& >= WB bound is armed ONLY while
' TK& = T0&, i.e. only when the tick counter has provably never moved
' (a stalled or emulated clock) - on real hardware it can never pre-empt
' genuine tick pacing, however fast the CPU. TK& < T0& handles the midnight
' rollover (BIOS zeroes the counter at 24:00). WI& is a LONG so it cannot
' overflow while waiting out the ticks.
' ---------------------------------------------------------------------------
WaitTick:
  DEF SEG = &H40
  GOSUB ReadTick
  T0& = TK&
  WI& = 0
  DO
    WI& = WI& + 1
    GOSUB ReadTick
  LOOP UNTIL TK& >= T0& + TP OR TK& < T0& OR INSTAT OR (WI& >= WB AND TK& = T0&)
  DEF SEG = VS
RETURN

' --- ReadTick: assemble the little-endian tick dword into TK& ---
ReadTick:
  B0& = PEEK(&H6C) : B1& = PEEK(&H6D) : B2& = PEEK(&H6E) : B3& = PEEK(&H6F)
  TK& = B0& + B1& * 256 + B2& * 65536 + B3& * 16777216
RETURN
