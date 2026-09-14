' ===========================================================================
' STARS3D.BAS - a 3-D starfield flythrough in VGA 256-colour MODE 13h with
'               depth-shaded stars, each painted by POKEing one byte straight
'               into the linear A000 framebuffer (offset = y*320 + x).
'
' The classic demo, exactly as you would write it in real Turbo Basic:
'   1. switch to mode 13h via the BIOS:   REG 1,&H13 : CALL INTERRUPT &H10
'   2. program a 16-step grey ramp into DAC colour indices 16..31
'      (OUT &H3C8 selects the index, three OUT &H3C9 writes set 6-bit R,G,B)
'      so a star's palette index IS its brightness - near stars glow white,
'      far stars fade toward a dim grey
'   3. keep a couple hundred stars in INTEGER arrays; every frame ERASE each
'      star's old pixel (POKE 0 at the offset remembered in a LONG array),
'      fly it toward the viewer, perspective-project x/z and y/z onto the
'      screen, clip, shade by depth band, and POKE the new pixel
'
' Star positions come from a tiny linear congruential generator, NOT RND,
' so the flight is identical on every run - here and in real Turbo Basic.
' The LCG s = (s*29+13) MOD 1024 is chosen so its largest intermediate
' value (1023*29+13 = 29680) still fits a signed 16-bit INTEGER - integer
' ops like MOD work on INTEGERs, so a wider generator would overflow. The
' y*320+x framebuffer offset, by contrast, IS built in a LONG variable,
' because it ranges up to 63999. Press ESC to exit.
' ===========================================================================
DEFINT A-Z

NS = 199                         ' 200 stars (indices 0..NS)
SP = 6                           ' z units flown toward the viewer per frame
DIM SX(NS), SY(NS), SZ(NS)       ' star positions: x,y model space, z = depth
DIM OP&(NS)                      ' each star's previous A000 offset (-1 = none)

' --- enter VGA mode 13h (320x200x256) through the BIOS ---
REG 1, &H13                      ' REG 1 = AX = 0013h: AH=00 set mode, AL=13h
CALL INTERRUPT &H10

' --- 16-step grey ramp in DAC 16..31 (dim far grey -> bright near white) ---
OUT &H3C8, 16                    ' start writing at colour index 16
FOR I = 0 TO 15
  V = 8 + (I * 55) \\ 15          ' 6-bit grey level 8..63
  OUT &H3C9, V : OUT &H3C9, V : OUT &H3C9, V
NEXT I

' --- scatter the stars with the deterministic 16-bit-safe LCG ---
' (full period 1024: multiplier 29 = 1 mod 4, increment 13 odd)
SD = 123
FOR I = 0 TO NS
  SD = (SD * 29 + 13) MOD 1024
  SX(I) = (SD MOD 641) - 320     ' x in -320..320
  SD = (SD * 29 + 13) MOD 1024
  SY(I) = (SD MOD 401) - 200     ' y in -200..200
  SD = (SD * 29 + 13) MOD 1024
  SZ(I) = 16 + SD MOD 240        ' z spread through 16..255
  OP&(I) = -1                    ' nothing on screen yet
NEXT I

DEF SEG = &HA000                 ' every POKE below paints the framebuffer

DO
  FOR I = 0 TO NS
    ' erase the star's previous pixel (cheaper than clearing the screen)
    IF OP&(I) >= 0 THEN POKE OP&(I), 0
    ' fly toward the viewer; expired stars respawn at the far plane
    SZ(I) = SZ(I) - SP
    IF SZ(I) < 4 THEN
      SD = (SD * 29 + 13) MOD 1024
      SX(I) = (SD MOD 641) - 320
      SD = (SD * 29 + 13) MOD 1024
      SY(I) = (SD MOD 401) - 200
      SD = (SD * 29 + 13) MOD 1024
      SZ(I) = 240 + SD MOD 16
    END IF
    ' perspective projection (|x|*100 <= 32000, so it fits an INTEGER)
    PX = 160 + SX(I) * 100 \\ SZ(I)
    PY = 100 - SY(I) * 100 \\ SZ(I)
    IF PX >= 0 AND PX <= 319 AND PY >= 0 AND PY <= 199 THEN
      INK = 31 - SZ(I) \\ 16      ' depth band -> DAC 16 (far) .. 31 (near)
      OF& = PY : OF& = OF& * 320 + PX
      POKE OF&, INK
      OP&(I) = OF&
    ELSE
      OP&(I) = -1                ' off-screen: nothing to erase next frame
    END IF
  NEXT I
  DELAY .02
  K$ = INKEY$
LOOP UNTIL K$ = CHR$(27)

DEF SEG
REG 1, 3                         ' back to 80x25 text via the BIOS
CALL INTERRUPT &H10
CLS
PRINT "STARS3D - deterministic LCG starfield, POKEd into the A000 bitmap."
END
