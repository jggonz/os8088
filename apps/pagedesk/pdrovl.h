/* ============================================================================
 * os8088 - apps/pagedesk/pdrovl.h   what runs in PDREND's second code segment
 *
 * PAGEDESK-SPEC 20.22.7.9. tools/cc8086.py moves the CODE of every C function
 * whose name begins `ovl_` into .modc - the part PDRENDC, loaded with PDREND
 * and bound by PDV_REND_CODE (pdrovl.inc) - and leaves its data in PDREND's
 * DS. Renaming here, by the preprocessor, keeps every file's own spelling,
 * so a neighbour's call to one of these needs no edit.
 *
 * WHAT MAY MOVE: code entered once per VERB, per OPERATOR, per RECORD or per
 * ANNOTATION. A call into the part is a far call (~10 us on an 8088); each
 * call it makes back to resident code - pdm_*, pd_f*, dict_get, path_*, any
 * PDREND routine not on this list - goes through cc_ovthunk (~40 us). So
 * nothing per SPAN, per PIXEL, per GLYPH or per DL record of a band walk may
 * move, and nothing whose ADDRESS is taken (cc8086 refuses it).
 *
 * Each agent adds its own block, one #define a line.
 * ==========================================================================*/
#ifndef PDROVL_H
#define PDROVL_H

/* [rcore] the verbs that are not the band loop (pdrv.c) */
#define verb_thumb      ovl_verb_thumb
#define verb_textmap    ovl_verb_textmap
#define tm_start        ovl_tm_start
#define verb_init       ovl_verb_init
#define verb_fontbank   ovl_verb_fontbank
#define glyph_fresh     ovl_glyph_fresh
#define verb_begin      ovl_verb_begin
#define verb_status     ovl_verb_status
#define verb_abort      ovl_verb_abort
#define verb_slot       ovl_verb_slot
#define xc_adopt        ovl_xc_adopt
#define view_base       ovl_view_base
#define group_narrow    ovl_group_narrow
/* [rcore] a content stream's end, once a page, form or /Contents element (pdri.c) */
#define frame_end       ovl_frame_end
/* [rcore] the ladder's answers, once a pass (pdrv.c) */
#define x_resolve       ovl_x_resolve
#define x_answered      ovl_x_answered
/* [rcore] the image stash, once a band that needs both decoders (pdrv.c) */
#define ik_fill         ovl_ik_fill
#define ik_size         ovl_ik_size
#define ik_note         ovl_ik_note
#define x_rneed         ovl_x_rneed    /* once a band, and only on a codec page */
/* [rcore] an image's operator and its decoder's open (pdri.c, pdrv.c) */
#define do_image        ovl_do_image
#define image_place     ovl_image_place
#define img_codec       ovl_img_codec
#define img_xopen       ovl_img_xopen
#define do_xobject      ovl_do_xobject
#define do_form         ovl_do_form
#define inl_start       ovl_inl_start
#define interp_start    ovl_interp_start
#define page_open       ovl_page_open
/* [rcore] annotations and the edit store's records (pdra.c) */
#define annot_try       ovl_annot_try
#define annot_next      ovl_annot_next
#define ap_fit          ovl_ap_fit
#define ap_find         ovl_ap_find
#define ap_state        ovl_ap_state
#define ref_get         ovl_ref_get
#define ed_replaced     ovl_ed_replaced
#define annot_edits     ovl_annot_edits
#define ed_marks        ovl_ed_marks
#define ed_text         ovl_ed_text
#define ed_textline     ovl_ed_textline
#define font_synth      ovl_font_synth

#endif
