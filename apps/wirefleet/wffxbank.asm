; WFLETFX.GFX - WIREFLET.O88's part 0 (SPEC.md 114.11): the effect stamps in
; all three forms, data only, read through the segment op_fetch gives back.
    cpu 8086                    ; data only, but SPEC.md 1 asks every root
%define WF_FX_DATA
%include "wirefleet/art/fx.inc"
