; =============================================================================
; os8088 - apps/skies/csvocab.inc
;
; THE SHARED WORLD VOCABULARY (SPEC.md 88.6, 88.10.5): the face tables every
; solid in every world uses, the edge lists a chain of stations wants, the
; anonymous models any world may stand on its outskirts, the runway's own face,
; and the two names every world shares.
;
; IT IS THE ONLY THING A WORLD FILE NAMES FROM OUTSIDE ITSELF - eleven symbols,
; counted - which is what makes a world assemble on its own at a fixed org. It
; is 564 bytes and it is loaded ONCE, at the head of the overlay; a world goes
; in after it and is swapped when the player picks another place.
; =============================================================================
; =============================================================================
; os8088 - apps/skies/csworld.inc
;
; THE SHARED WORLD VOCABULARY (SPEC.md 88.6): the model macros, the face
; tables every solid in every world uses, the anonymous models any world may
; stand on its outskirts, the runway's own face, the aeroplanes - and, at the
; foot of the file, the %includes that bring in ONE FILE PER LOCATION.
;
; A LOCATION IS A RUNWAY AND THE COUNTRY ROUND IT (SPEC.md 88.6.4). The world
; used to be one global table read straight by cs_scene; it is a pair of words
; in the location record now (CSA_OBJS, CSA_NOBJ), so nine places can stand in
; nine different countries and the picked row is what the cull walks. Metres;
; x east, z north, y up; EVERY WORLD PUTS ITS OWN ORIGIN WHERE ITS OWN LANDMARK
; IS, because nothing outside a world's own file ever reads its coordinates.
;
; A MODEL is db type, nverts (a STACK's LEVELS), nfaces, nedges, edge ink, 0;
; dw radius, verts, faces, edges. A STACK's verts are levels of (wx, h, wz),
; each standing for the four corners (+wx,h,+wz) (-wx,h,+wz) (-wx,h,-wz)
; (+wx,h,-wz), so a box is two levels and a pyramid a level and an apex. A
; FLAT model's verts are (x, z) pairs on the ground. A FACE is db n, ink,
; flags, then n indices, listed COUNTER-CLOCKWISE seen from outside; an EDGE
; is two indices.
;
; THE LIMITS A WORLD FILE MUST RESPECT, all of them from skies.asm:
;   CS_MAXV   24  vertices in one model - so at most SIX levels in a STACK
;   CS_MAXPV  10  vertices in one face after the near clip
;   CS_NVIS   32  objects that can be in ONE FRAME; the 33rd is dropped
;                 silently, so a world whose skyline is denser than that
;                 loses buildings rather than slowing down
;   CS_FAR 16000  metres: nothing further is transformed at all
; =============================================================================

cs_f_box:                           ; two levels: four walls and a roof
    CS_SIDES 0, CSI_WALL, CSI_WALL2
    CS_TOP 4, CSI_ROOF
cs_f_pyr:                           ; a level and an apex: four faces
    CS_SIDES 0, CSI_WALL, CSI_WALL2
cs_f_dome:                          ; a box with a pyramid on it: eight
    CS_SIDES 0, CSI_WALL, CSI_WALL2
    CS_SIDES 4, CSI_WALL, CSI_WALL2
cs_f_hill:                          ; a frustum in the hill's ink
    CS_SIDES 0, CSI_HILL, CSI_HILL2  ; two-tone, as a building is (88.4.4.1)
    CS_TOP 4, CSI_HILLTOP
cs_f_sacre:                         ; a box, its roof, a drum and a dome
    CS_SIDES 0, CSI_WALL, CSI_WALL2
    CS_TOP 4, CSI_ROOF
    CS_SIDES 4, CSI_WALL, CSI_WALL2
    CS_SIDES 8, CSI_WALL, CSI_WALL2

; --- water, and anything else laid on the ground as a RIBBON -----------------
; A ribbon is a FLAT model of 2n vertices: n stations down one bank, then n
; down the other, so the quads between them are (i, i+1, i+n+1, i+n). The face
; tables are the same bytes in every world and the Seine used to carry six
; copies of them; they are shared now, and CS_RIBF makes a set in any other
; ink. CSF_NOCULL because a ground polygon is drawn from either side.
cs_f_rib1: db 4, CSI_RIVER, CSF_NOCULL, 0, 1, 3, 2
cs_f_rib2: db 4, CSI_RIVER, CSF_NOCULL, 0, 1, 4, 3
           db 4, CSI_RIVER, CSF_NOCULL, 1, 2, 5, 4
cs_f_rib3: db 4, CSI_RIVER, CSF_NOCULL, 0, 1, 5, 4
           db 4, CSI_RIVER, CSF_NOCULL, 1, 2, 6, 5
           db 4, CSI_RIVER, CSF_NOCULL, 2, 3, 7, 6

; --- the edge lists a chain of stations wants, shared by every world ---------
; A far model is usually a CENTRELINE - the stations down the middle of what
; the near model fills - and these are its edges (88.6.1).
cs_e_road1: db 0,1
cs_e_road2: db 0,1, 1,2
cs_e_road3: db 0,1, 1,2, 2,3
cs_e_road4: db 0,1, 1,2, 2,3, 3,4
cs_e_road5: db 0,1, 1,2, 2,3, 3,4, 4,5

; --- the anonymous models (SPEC.md 88.13.1): the blocks and sheds a world
;     fills its edges with. They used to be CSO_FILLER, which only the top
;     rung drew; the ladder's top rung is CSO_DENSE now and these draw from
;     Moderate, which is the DEFAULT - so an anonymous block is ordinary
;     content and not an extra ----------------------------------------------
CS_BOX cs_m_hangar, 30,  12,  60
CS_BOX cs_m_shed,   40,  10,  40
CS_BOX cs_m_block,  60,  20,  60
CS_BOX cs_m_block2, 80,  20,  40
CS_BOX cs_m_small,  25,  15,  25

; --- ...and the anonymous TOWERS a High tier is built from (SPEC.md
;     88.13.1.3). The five above are 10 to 20 m: they are a shed, a warehouse
;     and a city block, and a downtown drawn out of them reads as an
;     industrial estate. These three are the rank above - a tower, the one
;     below it and the mid-rise floor between them - and every world that
;     grows a CSO_DENSE cluster wants the same three shapes, so they are
;     shared here rather than copied five times at 26 bytes each.
;
;     NYC-JFK keeps its own four (88.13.1.2) and is not moved onto these:
;     Manhattan's ladder is 55, 90, 140 and 190 m under the Woolworth's 241,
;     which is a statement about that skyline and not a generic scale.
;
;     THE RULE EVERY WORLD APPLIES TO THEM is the one Manhattan's four were
;     cut to: an anonymous tower may not out-top the location's own
;     landmarks, because a nameless block taller than the thing the player
;     came to see undoes it. 180 m clears the Shard's 306 and One Canada
;     Square's 235 in London, the Transamerica's 220 in San Francisco and
;     Downtown Miami's 196; where a world's landmark is lower than that, the
;     world uses tower2 and midrise and leaves cs_m_tower alone.
CS_BOX cs_m_tower,  22, 180,  22    ; 44 m square, about fifty storeys
CS_BOX cs_m_tower2, 30, 110,  30    ; 60 m square, the rank below it
CS_BOX cs_m_midrise,45,  55,  35    ; 90 by 70, the floor between them

; --- the runway's face and edges: its vertices are built in bss by
;     cs_airport from the LOCATION record (SPEC.md 88.6) -------------------
cs_f_rwy:  db 4, CSI_RUNWAY, CSF_NOCULL, 0, 1, 2, 3
cs_e_rwy:  db 0,1, 1,2, 2,3, 3,0, 4,5

; =============================================================================
; An object (SPEC.md 88.6): model, far model, x, z, y, range, lod, name,
; flags - twenty bytes, and the twentieth is the cull's own skip tick.
; =============================================================================

; --- the names every world shares --------------------------------------------
cs_n_bldg:   db 'A BUILDING', 0
cs_n_ground: db 'THE GROUND', 0

