; =============================================================================
; os8088 - apps/skies/cswmac.inc
;
; THE WORLD-BUILDING MACROS (SPEC.md 88.6), and nothing else: they EMIT NOTHING
; until a world invokes one, which is what lets the same file be read by two
; assemblies that share no address - the program's, and each world part's
; (88.10.5). They were interleaved with the tables in csworld.inc; a macro
; definition has no position, so gathering them here changes not one byte.
; =============================================================================

; --- faces between level %1 and the one above, and a top --------------------
; The winding is checked once, in the panel, and every box in the world uses
; these four tables.
%macro CS_SIDES 3                   ; base, the +-z ink, the +-x ink
    db 4, %2, CSF_PZ, %1+1, %1+5, %1+4, %1+0     ; +z
    db 4, %2, CSF_MZ, %1+3, %1+7, %1+6, %1+2     ; -z
    db 4, %3, CSF_PX, %1+0, %1+4, %1+7, %1+3     ; +x
    db 4, %3, CSF_MX, %1+2, %1+6, %1+5, %1+1     ; -x
%endmacro

%macro CS_TOP 2                     ; base (the level's first index), ink
    db 4, %2, CSF_TOP, %1+3, %1+0, %1+1, %1+2
%endmacro


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

%macro CS_RIBF 2                    ; label, ink: a 6-vertex ribbon's two quads
%1: db 4, %2, CSF_NOCULL, 0, 1, 4, 3
    db 4, %2, CSF_NOCULL, 1, 2, 5, 4
%endmacro

%macro CS_OBJ 9
    dw %1, %2, %3, %4, %5, %6, %7, %8, %9, 0   ; ...and CSO_SKIP
%endmacro
