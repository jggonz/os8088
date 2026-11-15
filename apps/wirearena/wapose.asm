; wapose.asm - WireArena's POSE part (docs/plans/completed/WIREARENA-PARTS-PLAN.md
; 8.6): the figures' COLD keyframes - a flinch and the deaths, tools/
; arenagen.py's wa_pose1 - alone in an OP_ASSET at org 0, NFACE x NJ x 2
; bytes a keyframe. The hot ones (IDLE, RUN) stay in the renderer part and
; are read in place; a figure in one of these has its two facing rows copied
; into the renderer's DS by wa_kfrows (wrender.inc), through wa_posg -
; nothing else reads this part.
    cpu 8086
    bits 16
    org 0
%include "waconst.inc"
%define WA_TAB_POSE1
%include "watab.inc"
%if wa_pose1 - $$ != 0
 %error "wapose: the keyframes are at the part's offset 0 (wa_kfo)"
%endif
wa_poseend:
%if wa_poseend - $$ > 65024
 %error "wapose: past the 65,024 bytes one part may hold"
%endif
