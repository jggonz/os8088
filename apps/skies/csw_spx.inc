; =============================================================================
; os8088 - apps/skies/csw_spx.inc
;
; GIZA (SPEC.md 88.6.4) - the world CAIRO-SPX stands in. Metres, x east,
; z north, y up; THE ORIGIN IS THE APEX OF THE GREAT PYRAMID.
;
; This is the CHEAPEST recognisable skyline in the set, and that is the reason
; it is in it. Three pyramids are three CS_PYR models - four faces and eight
; vertices each - and nowhere else on earth is identifiable from a silhouette
; at that price. So this world is the FLOOR of the band rather than a place
; near the middle of it: a machine that struggles over Paris should still fly
; here. What is saved on faces is spent on RANGE instead, which is the right
; place to spend it, because the pyramids really are visible from a long way
; off and a four-face model at fifteen kilometres costs eight transforms.
;
; The three stand in their real diagonal, running south-west from Khufu at the
; origin: Khafre 502 m away on a bearing of 223, Menkaure 441 m beyond it on
; much the same line at 213. Khafre is the shorter and looks the taller
; because it stands on higher ground, and the plateau's rise is NOT modelled -
; the ground here is desert and a frustum under all three would be a large flat
; face fighting them in the painter's order for no picture at all. The rise is
; the object record's CSO_Y instead, which is what that field is for: Khafre
; is up 10 m and Menkaure 6 more, so Khafre's apex stands 7 m above Khufu's
; from any angle. Ten metres under a 230 m pyramid is under a pixel at any
; range it is drawn from, so nothing reads as floating.
;
; The Nile is eight kilometres east of the pyramids in reality and its west
; bank is 3.7 km east of Khufu here, because the water and the pyramids have
; to be in the same picture: it runs south to north in two ribbons of three
; quads, joined station for station, each with its centreline as its far model
; (88.6.1). Cairo is on the far bank, six blocks and a road, which is
; somewhere to fly to and the only thing in this world that is not sand.
;
; The macros, the face tables, the shared filler models and the shared edge
; lists this file uses are csworld.inc's.
;
; THE ORDER IN THIS FILE IS FIXED: models, then the object table, then the
; names, then the location record - because CS_N_SPX is an equ over the
; table's own two labels and NASM cannot see them from above.
; =============================================================================

; --- the models --------------------------------------------------------------
; The pyramids at their real sizes. They are not scaled down: 230 m across is
; a city block, and being that big from that far away is the whole character
; of the place. A CS_PYR's four side faces degenerate to triangles at the
; apex, so the model is eight vertices and four faces however large it is.
CS_PYR cs_m_spx_khufu,  115, 147, 115    ; 230 m base, 147 m tall
CS_PYR cs_m_spx_khafre, 108, 144, 108    ; 215 m base, 144 m tall
CS_PYR cs_m_spx_menk,    53,  65,  53    ; 105 m base,  65 m tall
CS_PYR cs_m_spx_queen,   16,  28,  16    ; one of the three little ones south
                                         ; of Menkaure, 32 m across

; The Sphinx is 73 m long east to west, 19 m across and 20 m tall, which makes
; it a LOW BOX and nothing more. It gets its own model and its own name all
; the same, rather than being folded into the filler, because it is the second
; thing anybody recognises here and a crash into it should say so.
CS_BOX cs_m_spx_sphinx,  37,  20,  10
CS_BOX cs_m_spx_vtemp,   22,  13,  22    ; the valley temple beside it: 45 m
                                         ; square, granite, and the foot of
                                         ; the causeway

; The Cairo Tower, 187 m on the far bank. It is the one thing over there worth
; navigating by, so it is slender, tall and drawn from twelve kilometres.
CS_BOX cs_m_spx_ctwr,    12, 187,  12

; A MASTABA: 44 m by 24 and 9 m tall, flat-topped, and there are hundreds of
; them at Giza in two ruled grids either side of Khufu. It is the shape the
; plateau actually has from the air and the High tier is built out of it
; (SPEC.md 88.13.1.3) - which is the right thing for THIS world to spend its
; top rung on, because a tomb field is 26 bytes of model and a tower is a
; skyline Giza has not got.
CS_BOX cs_m_spx_mast,    22,   9,  12

; --- the Nile: two ribbons of three quads, joined station for station -------
; ONE model used TWICE, at two origins 450 m apart in x and 6,000 in z, which
; is exactly the lean of the centreline: the south piece's last stations and
; the north piece's first fall on the same three points, so the banks meet
; with no step in them. Four stations a bank, 500 m of water between the
; banks, 6 km of river in each piece.
cs_m_spx_nile: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0
    dw 3040, .v, cs_f_rib3, 0
.v: dw -475, -3000                  ; the west bank, south to north...
    dw -325, -1000
    dw -175,  1000
    dw  -25,  3000
    dw   25, -3000                  ; ...and the east bank, the same way
    dw  175, -1000
    dw  325,  1000
    dw  475,  3000
cs_m_spx_nilef: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 3010, .v, 0, cs_e_road3      ; the centreline, beyond 3,000 m: four
.v: dw -225, -3000                  ; vertices and three segments where the
    dw  -75, -1000                  ; near model is eight vertices and three
    dw   75,  1000                  ; polygons for a band of dither
    dw  225,  3000

; --- the causeway, and the corniche on the far bank -------------------------
; Both are chains of stations in the road ink with no faces at all, which is
; what Paris' axis and ring road are. A four-vertex edge model IS what a far
; model would be, so neither carries one.
;
; Khafre's causeway runs 600 m from the valley temple beside the Sphinx up to
; the pyramid's east face, west and a little north. It is the line that makes
; the Sphinx and the second pyramid one monument rather than two objects.
cs_m_spx_cause: db CSM_FLAT, 4, 0, 3, CSI_MARK, 0
    dw 310, .v, 0, cs_e_road3
.v: dw  300, -33                    ; the valley temple end, at (360, -448)
    dw  100, -10
    dw -100,  13
    dw -290,  35                    ; ...and Khafre's east face, (-230, -380)

; The corniche, 125 m in from the east bank and following it: without a line
; to stand against, six blocks on the far side of the water are six blocks in
; a desert rather than a city front.
cs_m_spx_corn: db CSM_FLAT, 4, 0, 3, CSI_MARK, 0
    dw 3010, .v, 0, cs_e_road3
.v: dw -230, -3000
    dw  -80, -1000
    dw   70,  1000
    dw  220,  3000

; =============================================================================
; The world. THIRTY objects, the last ten CSO_DENSE (88.13.1.3), so twenty of
; them are the Giza every rung below High draws.
; Twenty objects, and NINE is the most that survive the range test
; and the cull's cone together, from any of six viewpoints walked by hand:
; nine off the runway looking west, nine over the Nile looking back at Giza,
; nine over Cairo, four over Khufu looking east. That is 68 vertices and 35
; faces in the worst of them, against Paris' twelve objects over the city, and
; it is the whole reason this location is in the set.
;
; The three pyramids carry ranges of 12,000 and 16,000 with NO far model, and
; that is deliberate rather than an oversight of 88.6.1's rule: the rule is
; about vertices, and a CS_PYR is eight of them and four faces at any range,
; which is already what a far model would be built to be. CS_FAR is 16,000 on
; each axis before the Manhattan sum is taken, so a range of 16,000 draws
; Khufu from anywhere a coordinate in this world can reach.
; =============================================================================
cs_ot_spx:
    ; --- Giza: the diagonal, south-west from the origin --------------------
    CS_OBJ cs_m_spx_khufu,  0,        0,     0,   0, 16000,    0, cs_n_spx_khufu,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_spx_khafre, 0,     -340,  -370,  10, 16000,    0, cs_n_spx_khafre, CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_spx_menk,   0,     -580,  -740,  16, 12000,    0, cs_n_spx_menk,   CSO_COLLIDE | CSO_POI
    ; the three queens' pyramids in a row, their centres 140 m south of
    ; Menkaure's and 71 m clear of its base. They are 32 m across, which is
    ; what makes them the smallest thing here: a machine that has to lose
    ; something loses them and loses nothing it flies by.
    CS_OBJ cs_m_spx_queen,  0,     -690,  -880,  16,  4000,    0, cs_n_spx_queen,  CSO_COLLIDE
    CS_OBJ cs_m_spx_queen,  0,     -625,  -880,  16,  4000,    0, cs_n_spx_queen,  CSO_COLLIDE
    CS_OBJ cs_m_spx_queen,  0,     -560,  -880,  16,  4000,    0, cs_n_spx_queen,  CSO_COLLIDE
    ; --- the Sphinx, 700 m east of Khafre and on its latitude --------------
    CS_OBJ cs_m_spx_sphinx, 0,      360,  -370,   0,  6000,    0, cs_n_spx_sphinx, CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_spx_vtemp,  0,      400,  -460,   0,  4000,    0, cs_n_spx_vtemp,  CSO_COLLIDE
    CS_OBJ cs_m_spx_cause,  0,       60,  -415,   0,  4000,    0, cs_n_spx_cause,  CSO_ROAD
    ; --- the field itself, 2.5 km east ------------------------------------
    ; Both stand 188 m and 238 m clear of the runway rectangle, south of it,
    ; which is well outside 88.6's 150 m.
    CS_OBJ cs_m_hangar,     0,     2900,  -450,   0,  3000,    0, cs_n_bldg,       CSO_COLLIDE
    CS_OBJ cs_m_shed,       0,     2600,  -480,   0,  3000,    0, cs_n_bldg,       CSO_COLLIDE
    ; --- the Nile: the same ribbon twice, meeting at z = 0 -----------------
    CS_OBJ cs_m_spx_nile, cs_m_spx_nilef, 3800, -3000, 0, 7000, 3000, cs_n_spx_nile, CSO_TERRAIN
    CS_OBJ cs_m_spx_nile, cs_m_spx_nilef, 4250,  3000, 0, 7000, 3000, cs_n_spx_nile, CSO_TERRAIN
    ; --- Cairo on the east bank -------------------------------------------
    ; The tower is 187 m and reaches twelve kilometres; the blocks behind it
    ; are 120 m or so across and 20 m tall, which is a pixel or two at 4,500 m
    ; and nothing at all beyond it. Cutting them there is what keeps the two
    ; halves of this world out of one frame: from over the pyramids the whole
    ; of Cairo except the tower is out of range, and from the far bank Giza is
    ; the three pyramids and nothing else.
    CS_OBJ cs_m_spx_ctwr,   0,     4700,   400,   0, 12000,    0, cs_n_spx_ctwr,   CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_spx_corn,   0,     4400,     0,   0,  5000,    0, cs_n_spx_corn,   CSO_ROAD
    CS_OBJ cs_m_block,      0,     4900, -1300,   0,  4500,    0, cs_n_bldg,       CSO_COLLIDE
    CS_OBJ cs_m_block2,     0,     5150,  -600,   0,  4500,    0, cs_n_bldg,       CSO_COLLIDE
    CS_OBJ cs_m_block,      0,     4750,  1200,   0,  4500,    0, cs_n_bldg,       CSO_COLLIDE
    CS_OBJ cs_m_block2,     0,     5150,  2100,   0,  4500,    0, cs_n_bldg,       CSO_COLLIDE
    CS_OBJ cs_m_small,      0,     5000,  1900,   0,  3500,    0, cs_n_bldg,       CSO_COLLIDE
    ; --- THE HIGH TIER (SPEC.md 88.13.1.3): ten more, CSO_DENSE, so Detail
    ;     Level = High is the only rung that draws one and every rung below it
    ;     gives the Giza that always shipped.
    ;
    ;     THIS WORLD IS THE FLOOR OF THE SET and its High tier does not
    ;     change that. What every other location's top rung adds is towers;
    ;     Giza has none and inventing some would be the one thing this file
    ;     is written not to do. What the plateau really has beside the three
    ;     pyramids is the CEMETERIES - two ruled grids of flat-topped mastabas
    ;     east and west of Khufu, which is the texture an aerial photograph of
    ;     Giza is mostly made of - so that is the tier: seven tombs 9 m tall,
    ;     in a 90 m grid, four in the Eastern Cemetery and three in the
    ;     Western. They take 3,000 m like a shed: a 9 m box is nothing at
    ;     4,500 and the field is something the aeroplane arrives OVER.
    ;
    ;     Three more thicken Cairo on the east bank, where the departure
    ;     starts and which is behind the aeroplane for the whole flight, so
    ;     they take the 4,500 the blocks beside them already have. That
    ;     4,500 is load-bearing and not a habit: it is what keeps the two
    ;     halves of this world out of one frame.
    CS_OBJ cs_m_spx_mast,   0,      210,    90,   0,  3000,    0, cs_n_spx_mast,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_spx_mast,   0,      210,   230,   0,  3000,    0, cs_n_spx_mast,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_spx_mast,   0,      300,    90,   0,  3000,    0, cs_n_spx_mast,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_spx_mast,   0,      300,   230,   0,  3000,    0, cs_n_spx_mast,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_spx_mast,   0,     -250,   150,   0,  3000,    0, cs_n_spx_mast,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_spx_mast,   0,     -250,   290,   0,  3000,    0, cs_n_spx_mast,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_spx_mast,   0,     -340,   150,   0,  3000,    0, cs_n_spx_mast,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,     0,     4900,  -100,   0,  4500,    0, cs_n_spx_cairo,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,    0,     5250,   700,   0,  4500,    0, cs_n_spx_cairo,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,     0,     4850,  1650,   0,  4500,    0, cs_n_spx_cairo,  CSO_COLLIDE | CSO_DENSE
cs_ote_spx:
CS_N_SPX equ (cs_ote_spx - cs_ot_spx) / CSO_SIZE

; --- the names the crash line uses -------------------------------------------
cs_n_spx_khufu:  db 'THE GREAT PYRAMID', 0
cs_n_spx_khafre: db 'THE PYRAMID OF KHAFRE', 0
cs_n_spx_menk:   db 'THE PYRAMID OF MENKAURE', 0
cs_n_spx_queen:  db 'A QUEENS PYRAMID', 0
cs_n_spx_sphinx: db 'THE SPHINX', 0
cs_n_spx_vtemp:  db 'THE VALLEY TEMPLE', 0
cs_n_spx_cause:  db 'THE CAUSEWAY', 0
cs_n_spx_nile:   db 'THE NILE', 0
cs_n_spx_ctwr:   db 'THE CAIRO TOWER', 0
cs_n_spx_corn:   db 'THE CORNICHE', 0
cs_n_spx_mast:   db 'A MASTABA', 0
cs_n_spx_cairo:  db 'CAIRO', 0

; =============================================================================
; The location (SPEC.md 88.6, 88.6.4). Cairo West is out in the desert on the
; far side of the pyramids in reality; this field is 2.5 km EAST of Khufu and
; faces WEST, because that puts the whole world in front of the aeroplane on
; the take-off run. Measured off the extended centreline, everything the
; location is named for is within ten degrees of the nose: the Sphinx 190 m
; left and 1.4 km past the far threshold, Khufu 180 m RIGHT and 1.8 km past
; it, Khafre 190 m left again at 2.1 km, Menkaure 560 m left at 2.4. The four
; of them cross the track rather than lining up along it, so they open out of
; one another as the aeroplane closes, which is how they are photographed and
; is the reason this heading was chosen. THE APPROACH IS THE LOCATION.
;
; It is also a genuine obstacle. Khufu's apex is 147 m and 1.8 km past the far
; threshold, so a go-around off 27 is a climb or a turn, and CSO_COLLIDE means
; the simulator agrees.
;
; Landing on 27 comes the other way, in over the Nile: the water's west bank
; is 411 m short of the near threshold on this latitude, so the last of the
; approach is over the river and the strip of green beside it.
; =============================================================================
cs_a_spx:
    dw cs_s_spx                     ; CSA_NAME
    dw 2500, -180                   ; CSA_X, CSA_Z: 2.5 km east of Khufu, and
    dw 0                            ; CSA_ELEV     190 m north of the Sphinx
    dw 49152                        ; CSA_HDG      270
    dw 700                          ; CSA_HLEN     a 1,400 m runway...
    dw 22                           ; CSA_HWID     ...44 m wide
    dw cs_s_rwy_spx                 ; CSA_RWY
    dw -640                         ; CSA_SPAWN    60 m in from the threshold,
                                    ;              which is the east end here
    dw cs_ot_spx, CS_N_SPX          ; CSA_OBJS, CSA_NOBJ
    dw 3950, -1000                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 0                        ; CSA_WHDG    (88.7.7) - THE NILE
    dw 1000                          ; CSA_WLEN    half its length...
    dw 100                           ; CSA_WWID    ...and half its width
    dw cs_n_spx_nile              ; CSA_WNAME
    dw 0                        ; CSA_FLAGS
cs_s_spx: db 'CAIRO-SPX', 0
cs_s_rwy_spx: db 'RUNWAY 27', 0
