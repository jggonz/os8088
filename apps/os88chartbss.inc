; =============================================================================
; os88chartbss.inc - the ch_* working set apps/os88chart.inc runs on.
;
; DECLARED ONCE. It used to be eighty-four `equ` lines hand-copied into
; sheet.asm and chart.asm, and they had stayed in step only because nobody had
; yet added a field to one and not the other: the two blocks were the same 191
; bytes, in the same order, with the same increments, differing solely in
; where they started. That is a diff waiting to happen, and the failure it
; would cause is the quiet kind - the rasterizer reading one package's
; ch_pie_span out of the other's ch_pie_col.
;
; The caller says where the block goes and includes this:
;
;     %define CH_BSS_BASE (sh_chart_name + 13)
;     %include "os88chartbss.inc"
;     something_after equ CH_BSS_END
;
; WHY THE CALLER OWNS THE BASE. An .inc cannot declare bss in this model - a
; package's bss is one hand-chained run of `equ os88_image_end + N` (SPEC.md
; 20.1) and OS88_BSS needs the total - so the block has to sit inside the
; caller's chain wherever the caller has room. What it must NOT do is differ
; in shape, and that is what this file now guarantees.
;
; CH_BSS_END is where the caller carries on from, and CH_BSS_SIZE is the span,
; so a package that wants the block at a FIXED offset (which is what a shared
; overlay needs - see 82.16) can assert on it rather than count lines.
; =============================================================================
%ifndef CH_BSS_BASE
  %error "os88chartbss.inc: %define CH_BSS_BASE before including it"
%endif
ch_max         equ CH_BSS_BASE
ch_base        equ ch_max + 2
ch_arr         equ ch_base + 2
ch_cnt         equ ch_arr + 2
ch_idx         equ ch_cnt + 2
ch_bx1         equ ch_idx + 2
ch_by1         equ ch_bx1 + 2
ch_bx2         equ ch_by1 + 2
ch_by2         equ ch_bx2 + 2
ch_srcseg      equ ch_by2 + 2
ch_stgseg      equ ch_srcseg + 2
ch_neg         equ ch_stgseg + 2     ; stage 3.0f: 1 = some value is
                                       ; negative. Its own word now: the axis
                                       ; row is type-dependent, so ch_base
                                       ; cannot carry this as well.
ch_type        equ ch_neg + 2       ; CH_T_* - which chart to draw
ch_lx0         equ ch_type + 2      ; the current segment's endpoints and
ch_ly0         equ ch_lx0 + 2       ; the column being interpolated -
ch_lx1         equ ch_ly0 + 2       ; CALLER bss like every other ch_*
ch_ly1         equ ch_lx1 + 2       ; word, for the same DS reason
ch_lcx         equ ch_ly1 + 2

; sh_rowcol_reidx and friends (stage 2.x) - see the section comment above
; sh_rowcol_reidx itself for what each of these holds
ch_pie_px      equ ch_lcx + 2       ; --- stage 3.0f: the pie ---
ch_pie_py      equ ch_pie_px + 2
ch_pie_ex      equ ch_pie_py + 2    ; ch_ray's endpoint and its Bresenham
ch_pie_ey      equ ch_pie_ex + 2    ; state - in bss for the same DS reason
ch_pie_x       equ ch_pie_ey + 2    ; every other ch_* word is
ch_pie_y       equ ch_pie_x + 2
ch_pie_dx      equ ch_pie_y + 2
ch_pie_dy      equ ch_pie_dx + 2
ch_pie_sx      equ ch_pie_dy + 2
ch_pie_sy      equ ch_pie_sx + 2
ch_pie_err     equ ch_pie_sy + 2
ch_pie_e2      equ ch_pie_err + 2
ch_pie_tlo     equ ch_pie_e2 + 2    ; the 32-bit total and how far it was
ch_pie_thi     equ ch_pie_tlo + 2   ; shifted to fit a word
ch_pie_shift   equ ch_pie_thi + 2
ch_pie_a0      equ ch_pie_shift + 2 ; this slice's first half-degree...
ch_pie_span    equ ch_pie_a0 + 2    ; ...how many it covers...
ch_pie_a       equ ch_pie_span + 2  ; ...and the sweep's current one
ch_pie_col     equ ch_pie_a + 2
ch_pie_thick   equ ch_pie_col + 2    ; byte: this ray fills, so it is 3px
ch_pie_pen     equ ch_pie_thick + 1  ; byte: the colour ch_setpixel keeps
ch_pie_pat     equ ch_pie_pen + 1    ; byte: this slice's hatch, FF = solid
ch_tx          equ ch_pie_pat + 1   ; --- stage 3.0f: text into the canvas ---
ch_ty          equ ch_tx + 2
ch_tpen        equ ch_ty + 2
ch_tsrc        equ ch_tpen + 2        ; the string cursor, across ch_glyph
ch_tseg        equ ch_tsrc + 2        ; the GLYPH TABLE's segment, not KERNEL_SEG
ch_ttab        equ ch_tseg + 2
ch_tfirst      equ ch_ttab + 2        ; the character range the table covers
ch_tlast       equ ch_tfirst + 2
ch_tglyph      equ ch_tlast + 2       ; -> the current character's 8 rows
ch_trow        equ ch_tglyph + 2
ch_tcol        equ ch_trow + 2
ch_tpy         equ ch_tcol + 2
ch_tbits       equ ch_tpy + 2
ch_tnum        equ ch_tbits + 2       ; 16: ch_itoa_t's/ch_num_t's output -
                                      ; eight held "-32768" and nothing more,
                                      ; and a scaled label can carry a point
                                      ; and four digits, or nine trailing
                                      ; zeros (see ch_scale)
ch_e10         equ ch_tnum + 16     ; the series' decimal exponent (82.13)
ch_sc_seg      equ ch_e10 + 2       ; ch_scale's own scratch
ch_sc_src      equ ch_sc_seg + 2
ch_sc_dst      equ ch_sc_src + 2
ch_sc_cnt      equ ch_sc_dst + 2
ch_dbl         equ ch_sc_cnt + 2    ; 8: the value being converted...
ch_dmax        equ ch_dbl + 8       ; 8: ...and the largest seen
ch_title       equ ch_dmax + 8      ; -> the chart's title, or 0 for none
ch_legy        equ ch_title + 2     ; the legend row being drawn...
ch_legr        equ ch_legy + 2      ; ...and the swatch row inside it
ch_arr2        equ ch_legr + 2       ; --- the SECOND series (82.8) ---
ch_cnt2        equ ch_arr2 + 2      ; 0 = there is no second series
ch_srcseg2     equ ch_cnt2 + 2
ch_max2        equ ch_srcseg2 + 2   ; its own scale, independent of the first
ch_mkx         equ ch_max2 + 2      ; ch_mark's centre
ch_mky         equ ch_mkx + 2
ch_scx         equ ch_mky + 2       ; a scatter point's x, across the y maths
ch_cbx         equ ch_scx + 2       ; a combination point...
ch_cby         equ ch_cbx + 2
ch_lcy         equ ch_cby + 2       ; ...and the previous one's y
ch_l2x         equ ch_lcy + 2       ; ch_line2's Bresenham state
ch_l2y         equ ch_l2x + 2
ch_l2ex        equ ch_l2y + 2
ch_l2ey        equ ch_l2ex + 2
ch_l2dx        equ ch_l2ey + 2
ch_l2dy        equ ch_l2dx + 2
ch_l2sx        equ ch_l2dy + 2
ch_l2sy        equ ch_l2sx + 2
ch_l2err       equ ch_l2sy + 2
ch_l2e2        equ ch_l2err + 2

; --- the vectors out (82.16) -------------------------------------------------
; SPEC.md 68.10 rule 1: a call from a module to resident code cannot be near,
; so it goes through a dword of (offset, segment) that the caller stamps at
; load. THESE LIVE IN THE ch_* BLOCK because the module reads them through the
; caller's DS like every other ch_* word - so they need the same fixed offset,
; and the block already has one.
;
; Each points at a SHIM, never at the routine: every routine in this tree is a
; near proc ending in `ret`, so far-calling one pops the offset, leaves the
; segment on the stack and returns into nothing.
;
; ch_v_aiszero is FUSED: the module's one reference to fp's own bss was
; `mov bx, fp_am0` before `call fp_iszero`, and an address of the caller's bss
; is exactly what a shared module cannot know. The shim supplies it, and the
; module is left with no knowledge of where fp keeps anything.
ch_v_first     equ ch_l2e2 + 2
ch_v_unpacka   equ ch_v_first + 0
ch_v_unpackb   equ ch_v_unpacka + 4
ch_v_a2i       equ ch_v_unpackb + 4
ch_v_cmpab     equ ch_v_a2i + 4
ch_v_round     equ ch_v_cmpab + 4
ch_v_scale10   equ ch_v_round + 4
ch_v_aiszero   equ ch_v_scale10 + 4
ch_v_dec_s16   equ ch_v_aiszero + 4   ; os88dec.inc's two: resident in the
ch_v_dec_u16   equ ch_v_dec_s16 + 4   ; includer, far-called from the module
ch_v_end       equ ch_v_dec_u16 + 4

; --- the loader's own state (82.16) ------------------------------------------
; Package-side, not module-side: the module never reads these. They sit in the
; shared block anyway so the two packages cannot disagree about them either.
ch_ovseg       equ ch_v_end          ; word: CHART.OVL's claim, 0 = not loaded
ch_ovfar       equ ch_ovseg + 2      ; 4: the (offset, segment) ch_ovcall
                                     ; far-calls - offset is always 0
ch_ovdir       equ ch_ovfar + 4      ; word } the folder this package was
ch_ovdrv       equ ch_ovdir + 2      ; byte } LAUNCHED from, banked at entry
ch_ovwdr       equ ch_ovdrv + 1      ; byte } ...and where the USER had the
ch_ovwas       equ ch_ovwdr + 1      ; word } volume, put back after the load
ch_ovfail      equ ch_ovwas + 2      ; byte: the load failed - not retried
                                     ; (a word's room, so what follows keeps
                                     ; its parity)

CH_BSS_END     equ ch_ovfail + 2     ; where the caller's own chain resumes
CH_BSS_SIZE    equ CH_BSS_END - CH_BSS_BASE
