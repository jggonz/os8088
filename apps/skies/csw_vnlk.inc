; =============================================================================
; os8088 - apps/skies/csw_vnlk.inc
;
; THE KHUMBU (SPEC.md 88.6.4) - the world NEPAL-VNLK stands in, and the
; sparsest of the nine on purpose. There is ONE building on it, a hangar beside
; the strip, and everything else is rock and meltwater: a shelf of flat ground
; in a glacial valley, two peaks 3.2 km off the end of the runway with a gate
; between them, and a wall of mountain 8 km beyond that closes the head of the
; valley. Metres, x east, z north, y up; THE ORIGIN IS THE MIDDLE OF THE STRIP,
; because in a world with no city there is nothing else to hang the map on.
;
; THE GATE IS THE WHOLE LOCATION and it is sized to be flown rather than looked
; at. The runway heads 060; the two peaks sit 3,200 m along that bearing and
; 1,100 m either side of it, which puts each of them 19 degrees off the nose -
; inside the 49-degree field a Hercules gets (88.3.4), so both are in the
; windscreen from the threshold with the gap between them in the middle of the
; view.
;
; THAT GAP IS HONEST, and the arithmetic is written down here because
; cs_collide is not what a mountain looks like: it tests the model's LEVEL 0
; footprint and the tallest level's height as an axis-aligned BOX
; (csflight.inc), so a peak is a rectangular no-go column the size of its base
; and not a cone. The test is an AND of both axes, so what matters is whether
; the runway's centreline is ever within 600 m of a peak in x AND in z at once.
; It is not: the left peak's box wants the line between 3,229 m and 4,614 m
; along for x and between 0 and 2,234 m for z, and those two ranges do not
; overlap; the right peak's are 1,785..3,170 and 4,166..6,566, which do not
; either. So the aeroplane flown straight out on 060 goes between them at every
; altitude, and it is the SHOULDERS that bite - the same bargain every building
; in Paris makes.
;
; The macros, the face tables, the ribbon faces, the shared edge lists and
; cs_m_hangar are all csworld.inc's.
; =============================================================================

; --- the rock ----------------------------------------------------------------
; CS_HILL is label, base wx, base wz, height, top wx, top wz. The tapers are
; deliberately gentle: a frustum that narrows fast is a lot smaller than its
; collision box at altitude, and that reads as a crash in clear air.
CS_HILL cs_m_vnlk_gate,  600, 600, 2400, 240, 240   ; the two of the gate
CS_HILL cs_m_vnlk_peak,  900, 900, 3600, 200, 200   ; the head of the valley
CS_HILL cs_m_vnlk_wall,  700, 900, 2000, 300, 400   ; ...and behind the strip
CS_HILL cs_m_vnlk_ridge, 550, 550, 1700, 220, 220   ; the valley's own walls
CS_HILL cs_m_vnlk_spur,  400, 400, 1100, 160, 160   ; spurs past the gate

; --- the Dudh Koshi, one ribbon down the lower valley ------------------------
; The river runs 1,150 m north-west of the strip and 120 m wide, from 3,200 m
; below the threshold to 1,800 m above it, and it stops there: past that it is
; in a gorge, which is the honest way to keep it out of the gate. It has to be
; kept out, and not for scenery reasons - cs_collide tests a hill's level 0
; footprint as an axis-aligned box, so a 1,200 m peak beside a 120 m river
; overlaps it long before the two touch on the map. The first draft of this
; world put four peaks in the water and tests/unit/t_csworld.py said so; the
; tightest clearance now is 282 m, at the near north-west ridge.
;
; Four stations a side is eight vertices and three quads, and the far model is
; the same four stations down the middle: past 2,600 m the whole river is three
; segments rather than eight vertices transformed at some 3,300 cycles each,
; which is 88.6.1's finding applied to a valley instead of a city.
cs_m_vnlk_riv: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0
    dw 2600, .v, cs_f_rib3, 0
.v: dw -2135, -1302                 ; one bank, four stations down the valley
    dw  -576,  -402
    dw   983,   498
    dw  2195,  1198
    dw -2195, -1198                 ; ...then the other, the same way up
    dw  -636,  -298
    dw   923,   602
    dw  2135,  1302
cs_m_vnlk_rivf: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 2600, .v, 0, cs_e_road3
.v: dw -2165, -1250
    dw  -606,  -350
    dw   953,   550
    dw  2165,  1250

; =============================================================================
; The world. EIGHTEEN objects, the last six CSO_DENSE (88.13.1.3), so twelve of
; them are the Khumbu every rung below High draws and the six are the range's
; second rank.
;
; Every position below was laid out in (along the 060 runway axis,
; offset to its right) and converted once, the axis being (0.866, 0.5) and its
; right-hand normal (0.5, -0.866). The gate is at along 3,200 offset +-1,100;
; the valley walls are two pairs of ridges at +-2,700 and +-2,900; the wall
; behind the strip is 3,400 m back; the spurs are past the gate at along 5,200;
; and Everest closes the head of the valley 8 km out on the axis itself, which
; is a dead end on purpose, because the Khumbu is one.
;
; Ranges are Manhattan, as the cull measures them: the gate is 3,968 and 4,774
; from the threshold and is given 9,000 so it stays drawn while the aeroplane
; turns back for it; Everest is 10,928 and is given 14,000, which is inside
; CS_FAR's 16,000.
; =============================================================================
cs_ot_vnlk:
    CS_OBJ cs_m_vnlk_gate, 0,            3321,   647,   0,  9000,    0, cs_n_vnlk_kongde, CSO_COLLIDE | CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_gate, 0,            2221,  2553,   0,  9000,    0, cs_n_vnlk_nupla,  CSO_COLLIDE | CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_peak, 0,            6928,  4000,   0, 14000,    0, cs_n_vnlk_peak,   CSO_COLLIDE | CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_wall, 0,           -2644, -2220,   0,  8000,    0, cs_n_vnlk_ridge,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_ridge, 0,            1610, -2188,   0,  7000,    0, cs_n_vnlk_ridge,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_ridge, 0,           -1090,  2488,   0,  7000,    0, cs_n_vnlk_ridge,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_ridge, 0,            3702, -1211,   0,  7000,    0, cs_n_vnlk_ridge,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_ridge, 0,             802,  3811,   0,  7000,    0, cs_n_vnlk_ridge,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_spur, 0,            5253,  1301,   0,  8000,    0, cs_n_vnlk_ridge,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_spur, 0,            3753,  3899,   0,  8000,    0, cs_n_vnlk_ridge,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_vnlk_riv, cs_m_vnlk_rivf, -1181,   646,   0,  6000, 2600, cs_n_vnlk_river,  CSO_TERRAIN
    CS_OBJ cs_m_hangar,     0,             130,  -225,   0,  3000,    0, cs_n_vnlk_hangar, CSO_COLLIDE
    ; --- THE HIGH TIER (SPEC.md 88.13.1.3): six more, CSO_DENSE, so Detail
    ;     Level = High is the only rung that draws one and every rung below it
    ;     gives the Khumbu that always shipped.
    ;
    ;     EVERY OTHER WORLD'S TOP RUNG IS A CITY. This one has no city and
    ;     never will, so its High tier is the SECOND RANK OF THE RANGE: the
    ;     named peaks that stand behind the gate and beside Everest, which is
    ;     what the Khumbu looks like and what twelve objects could not say.
    ;     Below High the valley is its two gate peaks, seven anonymous ridges
    ;     and Everest at the head of it; at High it is a range.
    ;
    ;     IT IS ALSO THE ONE WORLD WHERE THE OCCLUSION PASS IS ON (88.13.7,
    ;     CSA_OCC below), and this is exactly the load it was built for: a
    ;     back rank standing behind a front one is the case where a nearer
    ;     mountain hides a further one wholesale, and the pass is what stops
    ;     the tier costing what its polygons would.
    ;
    ;     NONE OF THE SIX STANDS IN THE RUNWAY 06 DEPARTURE. cs_collide takes
    ;     a hill's level 0 footprint as an axis-aligned box, so what matters
    ;     is whether the 060 line is inside a box in x AND z at once; the
    ;     header above works that for the two gate peaks and the same
    ;     arithmetic is what placed these. Only Lhotse's box meets the line
    ;     and it meets it at 9.4 km, which is BEHIND Everest's - the corridor
    ;     already ends there. So the flight is the same at every rung, which
    ;     it has to be: a line a player can fly at Moderate and not at High
    ;     would be a difficulty setting wearing a performance setting's name.
    CS_OBJ cs_m_vnlk_gate, 0,            8700,  4200,   0, 15000,    0, cs_n_vnlk_lho,    CSO_COLLIDE | CSO_TERRAIN | CSO_DENSE
    CS_OBJ cs_m_vnlk_gate, 0,            8100,  2300,   0, 12500,    0, cs_n_vnlk_nup,    CSO_COLLIDE | CSO_TERRAIN | CSO_DENSE
    CS_OBJ cs_m_vnlk_gate, 0,            4700,  5400,   0, 12500,    0, cs_n_vnlk_ama,    CSO_COLLIDE | CSO_TERRAIN | CSO_DENSE
    CS_OBJ cs_m_vnlk_ridge, 0,           6500,  1200,   0, 10000,    0, cs_n_vnlk_tham,   CSO_COLLIDE | CSO_TERRAIN | CSO_DENSE
    CS_OBJ cs_m_vnlk_ridge, 0,           5100,  3950,   0, 11500,    0, cs_n_vnlk_kang,   CSO_COLLIDE | CSO_TERRAIN | CSO_DENSE
    CS_OBJ cs_m_vnlk_spur, 0,            2550,  -600,   0,  4000,    0, cs_n_vnlk_kusum,  CSO_COLLIDE | CSO_TERRAIN | CSO_DENSE
cs_ote_vnlk:
CS_N_VNLK equ (cs_ote_vnlk - cs_ot_vnlk) / CSO_SIZE

; --- the names the crash line uses -------------------------------------------
; The two ridges that really flank Lukla, and the mountain past the head of the
; valley. The hangar has a name of its own rather than cs_n_bldg's 'A BUILDING'
; because it is the only building here and being told which one would be a joke.
cs_n_vnlk_kongde: db 'KONGDE RI', 0
cs_n_vnlk_nupla:  db 'NUPLA', 0
cs_n_vnlk_peak:   db 'EVEREST', 0
cs_n_vnlk_ridge:  db 'A RIDGE', 0
cs_n_vnlk_river:  db 'THE DUDH KOSHI', 0
cs_n_vnlk_hangar: db 'THE HANGAR', 0
cs_n_vnlk_lho:    db 'LHOTSE', 0
cs_n_vnlk_nup:    db 'NUPTSE', 0
cs_n_vnlk_ama:    db 'AMA DABLAM', 0
cs_n_vnlk_tham:   db 'THAMSERKU', 0
cs_n_vnlk_kang:   db 'KANGTEGA', 0
cs_n_vnlk_kusum:  db 'KUSUM KANGURU', 0

; --- the location ------------------------------------------------------------
; Lukla's strip is 527 m of it and this one is 600, which is the shortest in the
; tree by 400 m: the aeroplane is stood 60 m in from the near threshold as it is
; everywhere else, so the take-off run is 540 m against Paris-Issy's 940. It is
; 20 m wide, half of what Le Bourget gets. The hangar is the only thing near it
; and stands 183 m clear of the rectangle.
cs_a_vnlk:
    dw cs_s_vnlk                    ; CSA_NAME
    dw 0, 0                         ; CSA_X, CSA_Z: the strip IS the origin
    dw 0                            ; CSA_ELEV
    dw 10923                        ; CSA_HDG      060, and the gate is on it
    dw 300                          ; CSA_HLEN     a 600 m runway...
    dw 10                           ; CSA_HWID     ...20 m wide
    dw cs_s_rwy_vnlk                ; CSA_RWY
    dw -240                         ; CSA_SPAWN    60 m in from the threshold
    dw cs_ot_vnlk, CS_N_VNLK        ; CSA_OBJS, CSA_NOBJ
    dw -1008, 746                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 10923                        ; CSA_WHDG    (88.7.7) - THE DUDH KOSHI
    dw 800                          ; CSA_WLEN    half its length...
    dw 40                           ; CSA_WWID    ...and half its width
    dw cs_n_vnlk_river              ; CSA_WNAME
    dw CSA_OCC                  ; CSA_FLAGS  the gate hides the spurs
cs_s_vnlk: db 'NEPAL-VNLK', 0
cs_s_rwy_vnlk: db 'RUNWAY 06', 0
