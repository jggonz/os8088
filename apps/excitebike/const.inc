; EXCITEBIKE constants (SPEC.md 102).  Committed source, ours: nothing here is
; read from a ROM or a disassembly.  Wave 1 carries only the screen geometry
; and table sizes the skeleton uses; the physics tables (speed steps, heat
; rates, landing windows, lane geometry) arrive with the simulation in Wave 3
; (docs/plans/EXCITEBIKE-PLAN.md 7).

; one 25-tile-row layout on every adapter (SPEC.md 102.1): 8 rows of crowd and
; banner, 16 rows of track band, 1 HUD row - 320 x 200 pixels, 40 x 25 tiles
XB_COLS         equ 40
XB_TOP_ROWS     equ 8
XB_BAND_ROWS    equ 16
XB_WORLD_ROWS   equ XB_TOP_ROWS + XB_BAND_ROWS
XB_HUD_ROW      equ XB_WORLD_ROWS
XB_TOPW         equ 64                  ; the top picture repeats every 64 columns
XB_LANE_ROWS    equ 6                   ; collision rows: tile rows 21..16

; a race is a runway plus two laps, and the compiler refuses a lap over 797
; columns (tools/excitebike_assets.py), so this is the worst case
XB_CID_MAX      equ 43 + 2 * 797        ; 43 = EXB_RUNWAY

; where the Wave 1 placeholder starts looking along the course
XB_X0           equ 30

; the front end's window (452 x 284 content 432 x 264 art, DrMarco's shape)
XB_ART_W        equ 432
XB_ART_H        equ 264
XB_ART_HC       equ 132                 ; the compact 1-bit desktop's height

; error codes shown on the splash
XB_ERR_NONE     equ 0
XB_ERR_MODE     equ 1                   ; OSAPI_FSX_MODE refused
XB_ERR_FILES    equ 2                   ; 8BBV.GFX / 8BBC.GFX missing or damaged
XB_ERR_MEM      equ 3                   ; a heap claim was refused: the machine has too little (SPEC.md 102.8.3)

; ---- wave 2: the scroll engine (SPEC.md 102.1) ------------------------------
; VGA 0Dh video memory map, the same offsets in each of the four planes
XV_HUD          equ 0x0000              ; the HUD row: 8 lines x 40, shown by line compare
XV_PAGE0        equ 0x0200              ; three pages (plan 4.3 asks for two - 102.1 says why three)
XV_PAGE_SZ      equ 0x2500              ; 9,472 = 40 x 192 window + 1,637 columns of travel, rounded
XV_BAND         equ XV_PAGE0 + 3 * XV_PAGE_SZ   ; band dictionary: 128 bytes a column, up to 72
XV_TOP          equ XV_BAND + 72 * 128  ; top dictionary: 64 bytes a column, 64 columns
XV_BLACK        equ XV_TOP + 64 * 64    ; one byte of 0 in every plane, the source of line 192
XV_LC           equ 384                 ; line compare: 2 x 192, the scan line that starts the HUD (SPEC.md 102.1: it is the FIRST line of the split)

XB_HUDCOL       equ 10                  ; the HUD text starts at this tile column...
XB_HUDLEN       equ 20                  ; ...and is this many cells (160 pixels)
XB_MAXBIKES     equ 6                   ; the rider, its shadow and dust, then three opponents (wave 4)
XB_BIKE_SIZE    equ 8
XB_POSE_W       equ 24
XB_POSE_H       equ 24
XB_PHASES_V     equ 4                   ; VGA: x mod 8 in {0,2,4,6}
XB_PHASES_C     equ 2                   ; CGA: x mod 4 in {0,2}
XB_STEP_HI      equ 1                   ; game steps a sub-tick = 60.0988 / 54.6205
XB_STEP_LO      equ 6575                ;   = 1 + 6575/65536 = 1.10033 (16.16, split so it fits a word)
XB_STEPK        equ 845                 ; steps = counts x 845 / 2^24: 1.10033 / 21,845 (the sub-tick's PIT counts)
XB_STEPS_MAX    equ 8                   ; a slow machine slows the game, it never spirals
XB_TSPEED       equ 0x0366              ; the scripted scroll speed: 3.398 px a step (8.8)
XB_SUB          equ 21845               ; PIT counts in one FASTTICK sub-tick
XB_QUIET        equ 64                  ; quiet frames before the governor gives a sub-tick back
XB_HUDDIG0      equ 5                   ; the HUD's clock digits are cells 5..11
XB_HUDDIG1      equ 12

; ---- wave 3: the simulation (SPEC.md 102.3) ------------------------------------
; the numbers tools/exbsim.py's PHYS holds, mirrored: tests/excitebike_ref.py compares them
; name for name, so a change to one that is not made to the other fails a row.
XP_SPD_A          equ 0x0320
XP_SPD_B          equ 0x0340
XP_SPD_MAX        equ 0x0466
XP_SPD_MUD        equ 192
XP_SPD_ROUGH      equ 0x0180
XP_ACC_A          equ 24
XP_ACC_B          equ 63
XP_DEC            equ 0x38
XP_DEC_AIR        equ 28
XP_DEC_AIR_L      equ 60
XP_DEC_ROUGH      equ 32
XP_DEC_MUD        equ 64
XP_DEC_CRASH      equ 48
XP_DEC_STALL      equ 32
XP_KICK           equ 128
XP_GRAV           equ 52
XP_GRAV_L         equ 24
XP_TEMP_MIN       equ 0x0800
XP_TEMP_A         equ 0x1100
XP_TEMP_MAX       equ 0x2000
XP_HEAT_B         equ 15
XP_HEAT_A         equ 7
XP_COOL           equ 11
XP_HEAT_MUD       equ 15
XP_STALL_STEPS    equ 0x0102
XP_CRASH_STEPS    equ 72
XP_WHEELIE        equ 13
XP_SQUASH         equ 4
XP_LANE_MIN       equ 7
XP_LANE_MAX       equ 57
XP_PITCH_MAX      equ 4
XP_PITCH_MIN      equ -3
XP_PT_GROUND      equ 5
XP_PT_AIR         equ 3
XP_RISE_FLAT      equ 0x0400
XP_FALL_FLAT      equ 0x0400
XP_HHI_CLEAR      equ 0x0C00
XP_HLO_CLEAR      equ 0x0300
XP_A_MAX          equ 0x6000
XP_TAKEOFF_NUM    equ 3
XP_BIKE_X         equ 88
XP_FEET0          equ 118
XP_CLOCK16        equ 1664                ; thousandths of a cs a step
XP_SHADOW_MIN     equ 0x1800
XP_FINISH_BACK    equ 3
XP_COL_AHEAD      equ 12
XP_RIDER_SLOT   equ 10                  ; the player's rider colour: palette slot 10 (red)

XM_RIDE         equ 0                   ; the modes of the rider
XM_AIR          equ 1
XM_CRASH        equ 2
XM_STALL        equ 3

INP_U           equ 0x01                ; the input byte: Up (lane away), Down (lane near),
INP_D           equ 0x02                ;   Left (nose up), Right (nose down), throttle, turbo
INP_L           equ 0x04
INP_R           equ 0x08
INP_A           equ 0x10
INP_B           equ 0x20

XEV_CRASH       equ 0x01                ; what happened in the last steps (xm_ev), for the sound
XEV_LAND        equ 0x02                ;   wave 5 reads; the frame loop clears it after the frame
XEV_TAKEOFF     equ 0x04
XEV_BUMP        equ 0x08
XEV_LAP         equ 0x10
XEV_FINISH      equ 0x20
XEV_OVERHEAT    equ 0x40
XEV_BOUNCE      equ 0x80

XB_TSTEPS       equ 256                 ; the harness's script and trace chunk (steps)

; ---- wave 4: race flow, opponents, front end (SPEC.md 102.4) --------------------
XB_NAI_MAX      equ 3                   ; opponents in Selection B
XB_NAI_CGA      equ 2                   ; ...and on the CGA, whose boxes are written to a 60-clock-a-word card (SPEC.md 102.4)
XB_REC_AI       equ 3                   ; the first opponent's bike record (0 rider, 1 shadow, 2 dust)
XB_COUNTDOWN    equ 180                 ; steps of READY 3..1 / GO before a race (3 s at 60.1 Hz)
XB_GO_STEPS     equ 45                  ; the GO! stays this long into the race
XB_FLASH_STEPS  equ 150                 ; the lap flash
XB_FIN_STEPS    equ 200                 ; steps the finished rider coasts before the results
XB_IDLE_ATTRACT equ 560                 ; idle title frames before the attract demo
XB_ATTRACT_MAX  equ 2600                ; steps of an attract race, at most (43 s)
XB_NTRACKS_MAX  equ 6                   ; the five courses and the optional EXBTRACK.DAT

; opponent respawn (pixels from the rider): ahead beyond XA_FAR is put behind the
; screen's left edge, behind beyond XA_LOST is put ahead of its right edge
XA_FAR          equ 260
XA_LOST         equ 100
XA_BEHIND       equ 110
XA_AHEAD        equ 215
XA_RUBBER       equ 110                 ; ahead of the rider by this many pixels: no turbo
XA_CATCHUP      equ 20                  ; behind the rider by this many: full turbo whatever its skill
XA_SPAWNCOOL    equ 90                  ; steps between two respawns of one opponent

; a collision: riders within XA_HITX pixels along, XA_HITL lanes and XA_HITH (8.8) of height
XA_HITX         equ 14
XA_HITL         equ 5
XA_HITH         equ 0x1000
XA_HARD         equ 0x0140              ; a closing speed above this crashes the rear bike

; ranks (SPEC.md 102.4): the time against the course's par, in hundredths
XB_RANK_1ST     equ 0
XB_RANK_2ND     equ 1
XB_RANK_3RD     equ 2
XB_RANK_NQ      equ 3
XB_WIN2         equ 400                 ; 2nd place: slower than par by less than this
XB_WIN3         equ 800                 ; 3rd place: slower than par by less than this
XB_WSHRINK2     equ 50                  ; each repeat of the last course takes this off the 2nd window
XB_WSHRINK3     equ 100                 ; ...and this off the 3rd

; the states of the game (xb_state, for the test harness to read)
XS_TITLE        equ 0
XS_MODE         equ 1
XS_TRACK        equ 2
XS_RACE         equ 3
XS_RESULT       equ 4
XS_OVER         equ 5
XS_BEST         equ 6
XS_ATTRACT      equ 7

; how a race ended (xb_result)
XR_ESC          equ 0
XR_FINISH       equ 1
XR_ALT          equ 2
XR_KEY          equ 3                   ; a key ended the attract demo
XR_DONE         equ 4                   ; the attract race ran out

; ---- wave 6: Hercules (SPEC.md 102.7) -----------------------------------------------
; 720 x 348 mono, four banks 2000h apart, 90 bytes a row of four scan lines.  A game pixel is
; TWO card pixels wide, so a tile column is 16 pixels = 2 bytes = the CGA's, and the picture is
; 45 columns (360 game pixels) wide: the row pitch IS the window.
XH_COLS         equ 45
XH_HUDCOL       equ 12                  ; the HUD text's first cell: (45 - 20) / 2, rounded down
XH_ROWS         equ 50                  ; a bank's rows of the 200-line field: 200 / 4
XH_R6           equ XH_ROWS             ; CRTC R6, the rows displayed (the desktop mode's is 87)
XB_EDGE         equ 22                  ; pixels of a 24-pixel bike that may hang off an edge of the picture: 2 still show
XH_R7           equ 68                  ; CRTC R7, the vertical sync position: puts the 50 rows mid-tube
