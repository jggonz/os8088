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

; --- faces between level %1 and the one above, and a top --------------------
; The winding is checked once, in the panel, and every box in the world uses
; these four tables.
%macro CS_SIDES 3                   ; base, the +-z ink, the +-x ink
    db 4, %2, 0, %1+1, %1+5, %1+4, %1+0     ; +z
    db 4, %2, 0, %1+3, %1+7, %1+6, %1+2     ; -z
    db 4, %3, 0, %1+0, %1+4, %1+7, %1+3     ; +x
    db 4, %3, 0, %1+2, %1+6, %1+5, %1+1     ; -x
%endmacro
%macro CS_TOP 2                     ; base (the level's first index), ink
    db 4, %2, 0, %1+3, %1+0, %1+1, %1+2
%endmacro

cs_f_box:                           ; two levels: four walls and a roof
    CS_SIDES 0, CSI_WALL, CSI_WALL2
    CS_TOP 4, CSI_ROOF
cs_f_pyr:                           ; a level and an apex: four faces
    CS_SIDES 0, CSI_WALL, CSI_WALL2
cs_f_dome:                          ; a box with a pyramid on it: eight
    CS_SIDES 0, CSI_WALL, CSI_WALL2
    CS_SIDES 4, CSI_WALL, CSI_WALL2
cs_f_hill:                          ; a frustum in the hill's ink
    CS_SIDES 0, CSI_HILL, CSI_HILL
    CS_TOP 4, CSI_HILL
cs_f_sacre:                         ; a box, its roof, a drum and a dome
    CS_SIDES 0, CSI_WALL, CSI_WALL2
    CS_TOP 4, CSI_ROOF
    CS_SIDES 4, CSI_WALL, CSI_WALL2
    CS_SIDES 8, CSI_WALL, CSI_WALL2

; --- a box, a pyramid, a dome (SPEC.md 88.5) -----------------------------------
;
; CSM_RAD IS THE MODEL'S OWN EUCLIDEAN RADIUS (SPEC.md 88.5.11), and these
; four work it out EXACTLY from the shape they are about to emit. It used to
; be `wx + wz + h / 2`, which is the bound for a model whose origin is its
; CENTRE - and the origin here is the base, so the vertical reach is h and not
; h/2. That halving put 36 of 122 models under their own radius, the Shard by
; 97 m of 306, and CSM_RAD under-stating is not a rounding error: cs_projall
; reads it to decide that an object is wholly in front of the near plane, and
; cs_edge1 then draws every edge from cs_sxv WITHOUT LOOKING AT cs_fv - so a
; vertex that was never projected this frame contributed whatever the last
; object left in its slot, unclipped, straight across the cockpit.
;
; EXACT AND NOT MERELY SOUND, because loose costs pixels. The first fix summed
; the terms - the only bound with no square root in it - and that is 4,000 for
; NEPAL's peak where 3,612 is the truth, 11% of a radius on the models where h
; dwarfs the plan. A radius is what cs_consider files an object on, so 11%
; files mountains that draw nothing, and `skiesocc` went red on it: the extra
; occluders moved the pass's verdicts and one of them hid a mountain the eye
; can see. So the macro does the square root at ASSEMBLY time.
;
; CS_RAD3 - CS_RADV = ceil(sqrt(a^2 + b^2 + c^2)), by Newton from an upper
;           bound. It converges from ABOVE, so the `%if %%n >= CS_RADV` is the
;           stop and not a step count; the four rounds after it are the
;           ceiling. tests/unit/t_csrad.py checks the answer against Python's
;           for every model, so this is arithmetic that cannot quietly rot.
%macro CS_RAD3 3
%assign %%s (%1)*(%1) + (%2)*(%2) + (%3)*(%3)
%if %%s == 0
  %assign CS_RADV 0
%else
  %assign CS_RADV (%1) + (%2) + (%3)
  %rep 24
    %assign %%n ((CS_RADV) + (%%s) / (CS_RADV)) / 2
    %if %%n >= CS_RADV
      %exitrep
    %endif
    %assign CS_RADV %%n
  %endrep
  %rep 4
    %if (CS_RADV) * (CS_RADV) < %%s
      %assign CS_RADV CS_RADV + 1
    %endif
  %endrep
%endif
%endmacro
%macro CS_BOX 4                     ; label, wx, h, wz
%1: db CSM_STACK, 2, 5, 0, CSI_LINE, 0
    CS_RAD3 %2, %3, %4              ; the top corner, which always dominates
    dw CS_RADV, %%v, cs_f_box, 0
%%v: dw %2, 0, %4
     dw %2, %3, %4
%endmacro
%macro CS_PYR 4                     ; label, wx, h, wz
%1: db CSM_STACK, 2, 4, 0, CSI_LINE, 0
    CS_RAD3 %2, 0, %4               ; a base corner...
%assign %%b CS_RADV
    CS_RAD3 0, %3, 0                ; ...or the apex
%if %%b > CS_RADV
  %assign CS_RADV %%b
%endif
    dw CS_RADV, %%v, cs_f_pyr, 0
%%v: dw %2, 0, %4
     dw 0, %3, 0
%endmacro
%macro CS_DOME 4                    ; label, w, the box's h, the apex's h
%1: db CSM_STACK, 3, 8, 0, CSI_LINE, 0
    CS_RAD3 %2, %3, %2              ; the box's top corner...
%assign %%b CS_RADV
    CS_RAD3 0, %4, 0                ; ...or the apex
%if %%b > CS_RADV
  %assign CS_RADV %%b
%endif
    dw CS_RADV, %%v, cs_f_dome, 0
%%v: dw %2, 0, %2
     dw %2, %3, %2
     dw 0, %4, 0
%endmacro
; --- a hill, a mountain, a headland: a frustum in the hill's ink --------------
%macro CS_HILL 6                    ; label, base wx, base wz, height, top wx,
%1: db CSM_STACK, 2, 5, 0, CSI_LINE, 0   ; top wz. A top of (0,0) is a peak
    CS_RAD3 %2, 0, %3               ; a base corner...
%assign %%b CS_RADV
    CS_RAD3 %5, %4, %6              ; ...or a top one
%if %%b > CS_RADV
  %assign CS_RADV %%b
%endif
    dw CS_RADV, %%v, cs_f_hill, 0
%%v: dw %2, 0, %3
     dw %5, %4, %6
%endmacro

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
%macro CS_RIBF 2                    ; label, ink: a 6-vertex ribbon's two quads
%1: db 4, %2, CSF_NOCULL, 0, 1, 4, 3
    db 4, %2, CSF_NOCULL, 1, 2, 5, 4
%endmacro

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
%macro CS_OBJ 9
    dw %1, %2, %3, %4, %5, %6, %7, %8, %9, 0   ; ...and CSO_SKIP
%endmacro

; --- the names every world shares --------------------------------------------
cs_n_bldg:   db 'A BUILDING', 0
cs_n_ground: db 'THE GROUND', 0

; =============================================================================
; The aeroplanes (SPEC.md 88.7): one row, and every constant the flight model
; reads. Angles are 65536 to the turn and rates are per tick.
;
; SPEEDS ARE 16.7 m/s AND NOT 16.8 (SPEC.md 88.7.4). The dive caps at 1.25 x
; VMAX and two sites read the speed SIGNED - the cap's own `jle` and cs_move's
; `imul` - so 16.8 put a ceiling of 102 m/s on VMAX, which is 198 knots: fine
; for a trainer and a biplane, and not an aeroplane faster than either. Half
; the resolution buys twice the range, every signed site keeps working
; untouched, and what changed instead is arithmetic: VSTALL/VROT/VMAX are
; x 128, THRUST/FRICT/BRAKE halved, and DRAGK re-derived from the new THRUST
; rather than scaled - it reads the HIGH WORD of v x v, which at 16.7 is a
; quarter of what it was, so the balance point moves by 2 and not by 1.
; =============================================================================
cs_p_c172:
    dw cs_s_c172                    ; CSP_NAME
    dw 24 * 128                     ; CSP_VSTALL   48 knots
    dw 28 * 128                     ; CSP_VROT     55 knots
    dw 78 * 128                     ; CSP_VMAX    150 knots, level at full power
    dw 16                           ; CSP_THRUST   2.2 m/s^2 at full throttle
    dw 689                          ; CSP_DRAGK    balances THRUST at VMAX
    dw 3                            ; CSP_FRICT    rolling, on the ground
    dw 20                           ; CSP_BRAKE
    dw 546                          ; CSP_ROLLR    3 degrees a tick, held
    dw 146                          ; CSP_ROLLL    0.8 a tick back to level
    dw 146                          ; CSP_PITCHR   0.8 a tick, held
    dw 0                            ; CSP_PITCHT   the nose STAYS where the stick
                                    ;              left it: a trimmed aeroplane
    dw 60                           ; CSP_TURNK    3 degrees a second at 30 of bank
    dw 2                            ; CSP_EYE      metres above the wheels
    dw 10923                        ; CSP_MAXROLL  60 degrees
    dw 7282                         ; CSP_MAXPITCH 40 degrees
    dw cs_ck_c172                   ; CSP_COCKPIT  (cspanel.inc)
    dw cs_att_trim                  ; CSP_ATT      a trainer's model (88.7.2)
    dw 0                            ; CSP_SPOOL    a piston: the throttle IS
                                    ;              the power (88.7.5)
    dw 0                            ; CSP_LAUNCH   on the runway
    dw 0                            ; CSP_FLAGS
    dw cs_art_c172                  ; CSP_ART      SIDE ON (88.10.1)
cs_s_c172:   db 'CESSNA 172', 0

; --- the second row: a PITTS SPECIAL (SPEC.md 88.7.2), an aerobatic biplane.
;     Light, quick on the controls and limited by nothing - it rolls right
;     round, loops over the top and stays where the stick leaves it, which is
;     cs_att_free's whole job. The numbers are an S-2B's: 1,150 lb empty,
;     260 hp, stalls near 52 knots, cruises 152 mph and never-exceeds 203 ---
cs_p_pitts:
    dw cs_s_pitts                   ; CSP_NAME
    dw 27 * 128                     ; CSP_VSTALL   52 knots
    dw 30 * 128                     ; CSP_VROT     58 knots
    dw 88 * 128                     ; CSP_VMAX    171 knots, level at full power
    dw 26                           ; CSP_THRUST   3.7 m/s^2: 260 hp on half
                                    ;              the Cessna's weight
    dw 880                          ; CSP_DRAGK    balances THRUST at VMAX
    dw 3                            ; CSP_FRICT    a taildragger on grass
    dw 23                           ; CSP_BRAKE
    dw 1820                         ; CSP_ROLLR   10 degrees a tick - 182 a
                                    ;              second, and the real one
                                    ;              manages 240
    dw 0                            ; CSP_ROLLL    NOTHING returns it to level
    dw 728                          ; CSP_PITCHR   4 degrees a tick: a loop in
                                    ;              about five seconds
    dw 0                            ; CSP_PITCHT   ...and the nose stays put
    dw 90                           ; CSP_TURNK    a shorter wing, a tighter
                                    ;              turn at the same bank
    dw 2                            ; CSP_EYE      metres above the wheels
    dw 32767                        ; CSP_MAXROLL  UNUSED by cs_att_free, and
    dw 16384                        ; CSP_MAXPITCH filled in with the honest
                                    ;              figures - 180 and 90
                                    ;              degrees - so a reader is
                                    ;              not told the wrong thing
    dw cs_ck_pitts                  ; CSP_COCKPIT  its own scattered panel
    dw cs_att_free                  ; CSP_ATT      the aerobatic model
    dw 0                            ; CSP_SPOOL    a piston
    dw 0                            ; CSP_LAUNCH   on the runway
    dw 0                            ; CSP_FLAGS
    dw cs_art_pitts                 ; CSP_ART      three-quarters from ahead
                                    ;              and BELOW, banked - a
                                    ;              different VIEW and not only
                                    ;              a different aeroplane
                                    ;              (88.10.1)
cs_s_pitts:  db 'PITTS SPECIAL', 0

; --- the third row: a FOUGA CM.170 MAGISTER (SPEC.md 88.7.5), the French jet
;     trainer the Patrouille de France flew nine of. It is here for one
;     MECHANIC and not for the number on the airspeed indicator: CSP_ATT is
;     cs_att_lag, so the stick sets a rate the aeroplane EASES onto, and
;     CSP_SPOOL 5 makes the thrust follow the throttle over about five
;     seconds. 715 km/h, stalls at 125, and its two Marbore turbojets are
;     0.28 of its weight in thrust ------------------------------------------
cs_p_fouga:
    dw cs_s_fouga                   ; CSP_NAME
    dw 35 * 128                     ; CSP_VSTALL   68 knots
    dw 42 * 128                     ; CSP_VROT     82 knots
    dw 190 * 128                    ; CSP_VMAX    369 knots. 1.25 x this is
                                    ;             30,400, which is the whole
                                    ;             reason 16.7 came first (88.7.4)
    dw 19                           ; CSP_THRUST   2.7 m/s^2: 0.28 t/w
    dw 138                          ; CSP_DRAGK    balances THRUST at VMAX
    dw 3                            ; CSP_FRICT
    dw 22                           ; CSP_BRAKE
    dw 1200                         ; CSP_ROLLR    6.6 degrees a tick ASKED
                                    ;              FOR; the lag decides when
                                    ;              it arrives
    dw 0                            ; CSP_ROLLL    nothing returns it to level
    dw 600                          ; CSP_PITCHR   3.3 a tick asked for
    dw 0                            ; CSP_PITCHT
    dw 40                           ; CSP_TURNK    a jet at 190 m/s turns
                                    ;              BADLY: two thirds of the
                                    ;              trainer's at the same bank
    dw 3                            ; CSP_EYE
    dw 32767                        ; CSP_MAXROLL  unused by cs_att_lag, and
    dw 16384                        ; CSP_MAXPITCH honest: it aerobats
    dw cs_ck_fouga                  ; CSP_COCKPIT  a jet's panel
    dw cs_att_lag                   ; CSP_ATT      the lagging model (88.7.5)
    dw 5                            ; CSP_SPOOL    a 32nd of the gap a tick:
                                    ;              95% of the way in 5.3 s
    dw 0                            ; CSP_LAUNCH   on the runway
    dw 0                            ; CSP_FLAGS
    dw cs_art_fouga                 ; CSP_ART      from BEHIND and above,
                                    ;              which is where a butterfly
                                    ;              tail is a butterfly tail
                                    ;              (88.10.1)
cs_s_fouga:  db 'FOUGA MAGISTER', 0

; --- the fourth row: a WASSMER WA-30 BIJAVE (SPEC.md 88.7.6), the French
;     two-seat sailplane. NO ENGINE AT ALL - CSP_THRUST is zero, so the only
;     energy it has is the height it starts with and every turn spends some.
;     CSP_LAUNCH 600 is what puts it there: the tow is released over the
;     field and the session begins in the air, because a winch launch is a
;     scripted sequence and this is not one. 28:1 at 80 km/h, which in a
;     model whose only sink is the nose attitude IS two degrees down --------
cs_p_bijave:
    dw cs_s_bijave                  ; CSP_NAME
    dw 17 * 128                     ; CSP_VSTALL   33 knots
    dw 250 * 128                    ; CSP_VROT     UNREACHABLE, deliberately:
                                    ;              a sailplane that lands has
                                    ;              landed, and no throttle can
                                    ;              rotate it again
    dw 61 * 128                     ; CSP_VMAX    119 knots, the VNE
    dw 0                            ; CSP_THRUST   NONE
    dw 1300                         ; CSP_DRAGK    balances gravity's component
                                    ;              at two degrees down and
                                    ;              22 m/s, which is 28:1
    dw 8                            ; CSP_FRICT    a skid on grass
    dw 15                           ; CSP_BRAKE    the wheel brake
    dw 546                          ; CSP_ROLLR    3 degrees a tick: a long
                                    ;              wing is slow in roll
    dw 60                           ; CSP_ROLLL    ...and comes back gently
    dw 110                          ; CSP_PITCHR   0.6 a tick
    dw 0                            ; CSP_PITCHT   trimmed: the nose stays
    dw 70                           ; CSP_TURNK
    dw 1                            ; CSP_EYE      low, reclined
    dw 10923                        ; CSP_MAXROLL  60 degrees
    dw 7282                         ; CSP_MAXPITCH 40
    dw cs_ck_bijave                 ; CSP_COCKPIT  no throttle, a variometer
    dw cs_att_trim                  ; CSP_ATT      docile, and it auto-levels
    dw 0                            ; CSP_SPOOL    there is no engine to spool
    dw 600                          ; CSP_LAUNCH   metres, tow just released
    dw 0                            ; CSP_FLAGS
    dw cs_art_bijave                ; CSP_ART      nearly PLAN, from above:
                                    ;              a sailplane's identity is
                                    ;              its span, and a side view
                                    ;              hides all of it (88.10.1)
cs_s_bijave: db 'WASSMER BIJAVE', 0

; --- the fifth row: an ICON A5 (SPEC.md 88.7.7), the light sport amphibian.
;     It fits the slow end of the envelope the other four leave empty - 95
;     knots and a 39-knot stall - and its whole point is CSPF_AMPHIB: it may
;     touch down on a location's WATER STRIP as well as its runway, and
;     where the place has water it STARTS there, hull in, which all nine of
;     them do. On the water the hull drags twice as hard as a wheel and
;     there is nothing to brake with -----------------------------------------
cs_p_a5:
    dw cs_s_a5                      ; CSP_NAME
    dw 20 * 128                     ; CSP_VSTALL   39 knots
    dw 24 * 128                     ; CSP_VROT     47 knots
    dw 49 * 128                     ; CSP_VMAX     95 knots
    dw 14                           ; CSP_THRUST   2.0 m/s^2: 100 hp Rotax, and
                                    ;              the field asked for a
                                    ;              quarter more than the 11
                                    ;              (1.5) it had - it is the
                                    ;              slowest aeroplane here and
                                    ;              was the slowest to get going
    dw 1500                         ; CSP_DRAGK    balances THRUST at VMAX -
                                    ;              RE-DERIVED with it, so the
                                    ;              A5 accelerates harder and
                                    ;              still tops out at 95 knots:
                                    ;              600 x 1500 >> 16 = 13
                                    ;              against a thrust of 14,
                                    ;              which is the same one-under
                                    ;              margin 1201/11 had
    dw 3                            ; CSP_FRICT    on land; DOUBLED on water
    dw 18                           ; CSP_BRAKE    on the wheels with B - and
                                    ;              on the WATER always, with
                                    ;              no key, because a hull is
                                    ;              a brake (88.7.7.2)
    dw 500                          ; CSP_ROLLR    2.7 degrees a tick
    dw 120                          ; CSP_ROLLL    it comes back to level on
                                    ;              its own: a spin-resistant
                                    ;              wing, and the easiest
                                    ;              aeroplane here to fly
    dw 130                          ; CSP_PITCHR   0.7 a tick
    dw 0                            ; CSP_PITCHT
    dw 65                           ; CSP_TURNK
    dw 2                            ; CSP_EYE
    dw 10923                        ; CSP_MAXROLL  60 degrees
    dw 7282                         ; CSP_MAXPITCH 40
    dw cs_ck_a5                     ; CSP_COCKPIT  one wide glass panel
    dw cs_att_trim                  ; CSP_ATT      docile, and it auto-levels
    dw 0                            ; CSP_SPOOL    a piston
    dw 0                            ; CSP_LAUNCH   ...on the WATER, which is
                                    ;              CSP_FLAGS' doing and not
                                    ;              a launch height
    dw CSPF_AMPHIB                  ; CSP_FLAGS
    dw cs_art_a5                    ; CSP_ART      from ahead and low, nose
                                    ;              to the RIGHT - the one that
                                    ;              shows a hull is a hull
                                    ;              (88.10.1)
cs_s_a5:     db 'ICON A5', 0

; =============================================================================
; THE WORLDS (SPEC.md 88.6.4) - one file each, and each file is whole: its
; models, its object table, its names and its location record, in that order,
; because CS_N_* is an equ over the table's own two labels. Nothing here reads
; anything there but the two symbols the location table in skies.asm names.
; =============================================================================
%include "csw_paris.inc"
%include "csw_lcy.inc"
%include "csw_jfk.inc"
%include "csw_sfo.inc"
%include "csw_mia.inc"
%include "csw_vnlk.inc"
%include "csw_sdu.inc"
%include "csw_spx.inc"
