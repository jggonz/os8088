/* ============================================================================
 * os8088 - apps/lemmings/lemmings.c
 *
 * LEMMINGS (SPEC.md 116) - Lemmings (DMA Design / Psygnosis, 1991, the DOS
 * release) as an os8088 C package. This file is the translation unit's ROOT:
 * the constants, the state, the prototypes, the callbacks the shim declares,
 * the empty menu set and the #includes of the rest, in dependency order.
 *
 * ONE TRANSLATION UNIT (SPEC.md 73.1): `nasm -f bin` has no notion of an
 * external symbol, so a C package is one compilation split by CONCERN into
 * #included .c files. MAKE CANNOT SEE THROUGH A #include - every file below is
 * a written prerequisite of $(BUILD)/lemmings.raw.asm in the Makefile, and
 * without that an edit here leaves a stale .o88 that reads exactly like a
 * change having done nothing (LESSONS.md 9).
 *
 * WHAT WAVE 1 IS. The desktop window is a LAUNCHER and nothing else (SPEC.md
 * 114): the four ratings, the level names of the chosen rating PAGED to the live
 * window height, the original's preview fields beside them, Play, four state
 * rows that give SPEC.md 47's greying a control to sit on, and the standard
 * About card. Play shows the level's own preview screen - the original's seven
 * lines in the original's wording - and wave 5 moves exactly that content
 * inside SPEC.md 53's bracket, into the second foreign mode each adapter needs
 * to hold its 640x350 surface. The raster the level itself needs arrives in
 * wave 2 (lemblit.inc, lemmask.inc, lemfont.inc), the game in wave 3, the whole
 * level set in wave 4.
 *
 * WHERE THE WORDS COME FROM. The level names, the preview fields, the rating
 * and style names and every sentence that greys a control are read off the
 * CONVERTED BAND FILES (SPEC.md 116.3, docs/lemband-format.md). This program
 * owns SIX literals and no more, and they are listed at their declaration
 * below; that is what SPEC.md 116.6's resident budget rests on, together with
 * the wave-1 overlay split.
 *
 * ATTRIBUTION. Lemmings is (C) 1991 DMA Design / Psygnosis. Every table, string
 * and offset derived from the original or from the three reimplementations that
 * document it names its source at the line that carries it; the full list is in
 * README.TXT beside the package, and the About card names the two principals
 * and points at it (SPEC.md 116.2). Nothing GPL or LGPL is copied.
 *
 * THE FOUR RULES (apps/cc/os88.h, SPEC.md 73.5, docs/C-TOOLCHAIN.md): every
 * addressable object is static - the address of an automatic is a stack offset
 * resolved through DS, SS != DS here, and tools/cc8086.py refuses it; no struct
 * goes by value, because the copy is a string instruction and ES is the
 * kernel's; no long, float, double, bit-field or anonymous union; and frames
 * stay under 96 bytes.
 *
 * LEMS_GREY_SAVE, the sentence behind the Save Progress row, says what the
 * row is greyed ON - this disk's SYSTEM/APPDATA did not answer (SPEC.md 19.9,
 * lemovl.c's ovl_progress_read) - and names the live CD as the one case of it
 * that cannot be cured. Waves 1-3 carried a sentence about the live CD alone,
 * one case narrower than the predicate; wave 4 broadened it in
 * tools/os88lem.py's table (SPEC.md 116.8).
 * ==========================================================================*/

#include "os88.h"
#include "lemstr.h"                 /* the generated LEMS_* ids - build/ */

/* --- the window, authored against the 640x480 reference (SPEC.md 39) --------
 * A reference, not a promise: wm_create clamps this onto the live screen and
 * every layout number is read back from os88_wm_geom() rather than assumed. On
 * a CGA the content box is 200 - MBAR_H 20 - DOCK_H 24 - TITLE_H 18 - 1 = 137
 * pixels, and the level list PAGES into whatever it gets (LESSONS.md 8: the
 * Font list ran off the bottom of a 200-line screen because it was sized from
 * its LENGTH). */
#define LEM_WIN_X    40
#define LEM_WIN_Y    40
#define LEM_WIN_W   512
#define LEM_WIN_H   208
#define LEM_MIN_W   288
#define LEM_MIN_H   120

/* --- the shape of the converted data (docs/lemband-format.md) -------------- */
#define LEM_RATINGS      4
#define LEM_PERRAT      30
#define LEM_ENTSZ       64          /* one level entry; the stride is a SHIFT */
#define LEM_RATBUF    2048          /* LEMR<n>.LEM is exactly this on every
                                     * disk and every geometry, so one rating's
                                     * bss is a constant */
#define LEM_STRBUF    5120          /* LEMSTR.LEM is 4,608 - the table
                                     * padded to 512 - and is read WHOLE; it
                                     * is the same on every geometry
                                     * (tools/os88lem.py). Wave 4 broadened
                                     * the Save Progress fact past the pad and
                                     * raised this to 5,632 - 512 bytes of bss
                                     * for 80 of text - and its review put it
                                     * back by trimming the sentence instead.
                                     *
                                     * ONE 512-STEP ABOVE THE BAND, WHICH IS
                                     * EXACTLY WHAT THE REFUSAL NEEDS. The
                                     * converter pads to 512 and lem_str_load()
                                     * REFUSES a read that FILLED the buffer -
                                     * a band that filled it may have been
                                     * truncated, and a truncated directory
                                     * indexes strings that are not there - so
                                     * the buffer has to clear the band by a
                                     * whole step or the next size the band can
                                     * take is the size that refuses. AT WAVE
                                     * 6 THE TABLE IS 4,538 BYTES, PADDED TO
                                     * 4,608, AND THE BAND THAT REFUSES IS
                                     * 5,120: the headroom is 70 bytes OF
                                     * TABLE - what 4,608 has left before the
                                     * pad rolls over - and NOT a whole step
                                     * (wave 5 left it at 2, 4,606; wave 6's
                                     * three corrections - GREY_CODE's last
                                     * sentence, GREY_EGA's "out", CREDIT4's
                                     * "(zlib)" - took 68 off). The 71st byte
                                     * of new text costs +512 bss: raising this
                                     * constant and tools/os88lem.py's own
                                     * LEM_STRBUF is ONE edit, made together,
                                     * and the converter's assertion is what
                                     * says so by name. TRIM BEFORE ADDING.
                                     * A buffer of 4,096 was what this had, and
                                     * wave 1's own two per-adapter Mode facts
                                     * plus the Psygnosis copyright took the
                                     * band from 3,584 to 4,096 - the very step
                                     * that would have taken lem_str_load() to
                                     * 0, lem_data_load() to 0, and every disk
                                     * in every geometry to a launcher with no
                                     * level names on it and the sentence "The
                                     * converted level data is not in this
                                     * folder.", which names the wrong cause:
                                     * the bands are all there.
                                     *
                                     * IT WAS 8,192 AND THE HEADROOM WAS NOT
                                     * THIS FILE'S TO SPEND. bss is the cheap
                                     * half of this budget, but wave 1 measured
                                     * 14,866 bytes of it against SPEC.md 116.6's
                                     * plan of ~14,000 for the WHOLE program,
                                     * with the lemming pool, the object
                                     * instances, the terrain list, the sprite
                                     * save-under, the dirty spans and the
                                     * trigger buckets - ~6,000 further bytes -
                                     * still to come. Four kilobytes of
                                     * deliberate headroom on a line that had
                                     * none to give is 3,072 bytes back for the
                                     * rasters, and the ceiling stays real: one
                                     * whole 512-step of band growth, which is
                                     * ~15 more rows of the size of the ones
                                     * wave 1 added. tools/os88lem.py asserts the
                                     * SAME 5,120 beside build_string_band(), so
                                     * the day it is reached the CONVERTER fails
                                     * by name on the host rather than the
                                     * shipped package failing on the glass. */

#define LEM_BANK_STYLE   0          /* which half of LEMMAN's cost table */
#define LEM_BANK_SPEC    1

/* --- the world, and the raster's own numbers (SPEC.md 116.3.1, 116.4) --------
 * 1,584 x 160, which is lemmings_3ds's and Lemmix's figure and not
 * Lemmings.ts's 1,600 (lemtool/REPORT.md measured all 120 levels). The C says
 * WHAT to draw and lemblit.inc/lemmask.inc touch the pixels, so these three are
 * the only raster numbers this side needs - and each of them is stated once
 * more, as an `equ`, in the assembly that owns it. */
#define LEM_WORLD_W   1584
#define LEM_WORLD_H    160
#define LEM_RKIND_VGA    0          /* lb_kind in lemblit.inc */
#define LEM_RKIND_CGA    1
#define LEM_RKIND_HERC   2

/* --- the glass shadow (LESSONS.md 6: design it in wave 1, not as polish) ----
 * One character and one attribute per cell. 64 columns is 512 pixels, which is
 * the window this launcher is authored at; a wider window is laid out inside
 * 64 cells rather than growing two 1,280-byte arrays, because bss is cheap but
 * it is not free and SPEC.md 116.6's budget is 81% spent before this file
 * starts. The stride is a power of two so `row * stride` is a SHIFT and never
 * an imul the gate has to find a dead scratch register for (LESSONS.md 3). */
#define LEM_SH_COLS     64
#define LEM_SH_ROWS     20
#define LEM_SH_SHIFT     6          /* 1 << 6 == LEM_SH_COLS */

/* The cell attributes. Two are a font_run ink/paper pair; LEM_AT_GREY is the
 * DISABLED PEN, which is a colour AND a flag, and on the two 1bpp adapters the
 * flag is the entire difference (SPEC.md 47.2, 39.4).
 *
 * LEM_AT_TEXT IS ZERO AND THAT IS LOAD-BEARING. The kernel white-fills the
 * WHOLE content before W_PAINT unless a window took WF_OWNBG (kernel/wm.inc's
 * own comment at the interlock, SPEC.md 11.90.2), so after a paint the glass is
 * exactly "spaces, black on white" - which is what lem_sh_blank() seeds the
 * shadow with, and which is why a full repaint draws only the cells that carry
 * something. A blank cell costs nothing at all rather than ~900 us. */
#define LEM_AT_TEXT     0           /* black on white - and the background */
#define LEM_AT_SEL      1           /* white on black - the selection */
#define LEM_AT_GREY     2           /* the disabled pen */

/* THERE IS NO LIGHT-GREY ATTRIBUTE, and there was: the tab strip drew its
 * three unselected ratings as black on OS88_LGRAY, which looks like a tab on a
 * VGA and is THREE SOLID BLACK RECTANGLES on CGA and Hercules - the paper
 * rounds to black there, and so does the ink, so the words were not on the
 * glass at all (build/port-shots/wave1-cga-launcher.png, before this). SPEC.md
 * 39.4 says to look at a drawing change on a 1bpp adapter before calling it
 * done, and this is what that looks like when you do. The selection is inverse
 * video, which is the distinction that has to survive, and it survives on all
 * three adapters; an unselected tab is ordinary text. */

/* The launcher's rows that are not level rows. Their order is their id. */
#define LEM_ROW_MODE    0
#define LEM_ROW_MUSIC   1
#define LEM_ROW_CODE    2
#define LEM_ROW_SAVE    3
#define LEM_STATE_ROWS  4

/* The first list row. Row 0 is the tab strip and row 1 is the gap under it. */
#define LEM_LISTTOP     2

/* The three screens this wave has. */
#define LEM_SC_LIST     0
#define LEM_SC_PREVIEW  1           /* the level's own preview, wave 5 moves it
                                     * inside the bracket (SPEC.md 116.4.3) */
#define LEM_SC_FACT     2           /* the sentence behind a greyed control */
#define LEM_SC_POST     3           /* the level's RESULT - the original's
                                     * postview text, in the window in wave 3
                                     * as the preview is (wave 5 moves both
                                     * into the bracket, SPEC.md 116.4.3) */

/* ------------------------------------------------------------------------- */
/* Prototypes for everything. They are here rather than scattered because the
 * HOST HARNESS compiles this same C with clang, which is stricter about
 * prototypes than SmallerC is: `call to undeclared function` and `static
 * declaration follows non-static declaration` are both errors there and neither
 * is one here (LESSONS.md 3). */

/* lemtab.c */
static int  lem_state_fact(int row);
static int  lem_state_label(int row);
static int  ovl_style_name(int style);

/* lemtext.c */
static int  lem_u16(const unsigned char *p, int off);
static int  ovl_u8(const unsigned char *p, int off);
static void ovl_u32dec(const unsigned char *p, int off, char *dst, unsigned cap);
static const char *lem_str(int id);
static int  lem_str_load(void);
static void ovl_arg_reset(void);
static void ovl_arg_add(const char *s);
static char *ovl_arg_slot(void);
static void ovl_arg_num(unsigned v);
static void ovl_arg_pct(unsigned num, unsigned den);
static void ovl_arg_u32(const unsigned char *buf, int off);
static void ovl_sfmt(char *dst, unsigned cap, const char *fmt);
static void ovl_para_fmt(const char *fmt);
static void lem_setn(char *dst, unsigned cap, unsigned v);
static void lem_fit(char *dst, unsigned cap, const char *src, int cols);
static const char *lem_trim(const char *s);
static void ovl_para_reset(void);
static void ovl_para_add(const char *s);
static void ovl_para_nl(void);
static void lem_wrap(void);
static const char *lem_wrap_line(int i);

/* lemload.c */
static int  ovl_man_u16(int off);
static int  ovl_man_u8(int off);
static const char *ovl_geom(void);
static int  ovl_cost_row(int kind, int n);
static int  ovl_cost_clus(int kind, int n);
static int  ovl_cost_parts(int kind, int n);
static const char *ovl_bandname(int kind, int n);
static const unsigned char *lem_entry(int row);
static int  lem_ent_u8(int row, int off);
static int  lem_ent_u16(int row, int off);
static const char *lem_ent_name(int row);
static int  lem_ent_here(int row);
static int  lem_rating_load(int rating);
static int  lem_data_load(void);
static void ovl_far(unsigned base, int lo, int hi);
static int  ovl_pk(unsigned seg, unsigned off);
static int  ovl_pk16(unsigned seg, unsigned off);
static int  ovl_claim_all(void);
static void lem_free_all(void);
static int  ovl_need_kb(void);
static int  ovl_parts_read(int nparts, unsigned seg, unsigned kb);
static int  ovl_rec_read(int level);
static void ovl_stem_gr(int n);
static void ovl_stem_main(void);
static int  ovl_main_read(void);
static int  ovl_bank_read(int style);
static int  ovl_spec_read(int spec);
static void ovl_stem_sp(int n);
static int  ovl_item(int id);
static void ovl_rat_name(void);

/* lemui.c */
static int  lem_rowy(int row);
static int  lem_colx(int col);
static void lem_sh_blank(void);
static void lem_layout(void *win);
static void lem_repaint(void *win, int full, int arm);
static void lem_pv_defer(void *win);
static void lem_select(void *win, int row);
static void lem_set_rating(void *win, int rating);
static int  lem_hit(int x, int y);
static int  lem_state_grey(int row);
static int  lem_play_ok(void);
static int  lem_kbd101(void);
static void lem_mark(int row);
static void lem_mark_range(int lo, int hi);
static void lem_mark_level(int lvl);
static void lem_mark_pane_rows(void);
static int  lem_blank_glass(void *win);
static void lem_show_fact(void *win, int labelid, int strid, int level);
static int  lem_set_fact(int labelid, int strid, int level);
static void lem_back(void *win);
static void lem_play(void *win);

/* lemgame.c - the per-frame step (SPEC.md 116.4.7) */
static void lem_snd_cue(int ev);
static void ovl_snd_flush(void);
static void ovl_rr_adjust(void);
static void ovl_increment(void);
static void ovl_spawn(void);
static void ovl_nuke_step(void);
static void ovl_finished_check(void);
static int  ovl_frame_begin(void);
static int  lem_frame_chunk(int n);
static void ovl_frame_end(void);
static void ovl_select_skill(int b);
static void ovl_toggle_pause(void);
static void ovl_nuke(void);
static void ovl_rr_extreme(int up);

/* lemobj.c - the objects and the frames everything is drawn from */
static void lem_far_add(unsigned n);
static void lem_far_mul(unsigned n, unsigned per);
static int  lem_anim_at(int act, int rtl, int f);
static int  lem_mask_at(int row, int f);
static void lem_trap(int i);
static void ovl_obj_step(void);
static void ovl_obj_draw(int i);
static int  ovl_obj_hits(int i, int x, int y, int w, int h);
static void ovl_obj_redraw_rect(int x, int y, int w, int h);
static void ovl_obj_all(void);

/* lemact.c - the eighteen actions, the skills, the pick */
static int  lem_hp(int x, int y, int miny);
static void lem_remove(void);
static void lem_turn(void);
static void lem_trans(int act, int turn);
static void lem_topcheck(void);
static void lem_field(int on);
static int  lem_field_over(void);
static int  lem_dig_row(int y);
static void lem_lay_brick(void);
static void lem_stencil(int row, int frame, int x, int y);
static int  lem_walk(void);
static int  lem_jump(void);
static int  lem_dig(void);
static int  lem_climb(void);
static int  lem_drown(void);
static int  lem_hoist(void);
static int  lem_build(void);
static int  lem_stopped(int obj);
static int  lem_bash(void);
static int  lem_mine(void);
static int  lem_fall(void);
static int  lem_float(void);
static int  lem_block(void);
static int  lem_ohno(void);
static int  lem_bomb(void);
static int  lem_handle(void);
static int  lem_fuse(void);
static void lem_objects(void);
static int  ovl_assign(void);
static void ovl_hittest(int cx, int cy, int right, int rec);
static int  ovl_word(int i);

/* lemdraw.c - the frame (SPEC.md 116.4) */
static int  ovl_level_load(int level);
static int  lem_compose_level(int level);
static void lem_world_clear(int vram);
static void ovl_mm_sample(void);
static int  ovl_mm_bit(int mx, int my);
static int  ovl_mm_ink(int v, int on);
static void ovl_mm_row(int my, int c0, int c1);
static void ovl_mm_col(int mx, int r0, int r1);
static void ovl_mm_draw(void);
static void ovl_view_rect(int erase_from);
static void ovl_status_template(void);
static void ovl_status_field(int col, const char *s, int n);
static void ovl_panel_draw(void);
static void ovl_frame_panel(void);
static void ovl_load_bar(int n);
static void lem_damage(int x, int y, int w, int h);
static int  ovl_mm_vr(int mx, int my);
static void ovl_mm_cell(int mx, int my);
static void ovl_mm_clear(int x, int y, int big);
static void ovl_mm_dots(void);
static void ovl_dirty_count(int b);
static void ovl_dirty_sel(int b);
static void lem_dirty_stat(void);
static void ovl_stat_time(void);
static void ovl_count_draw(int b);
static void ovl_sel_draw(int b, int on);
static void ovl_padr(char *dst, const char *s, int n);
static void ovl_stat_build(void);
static void ovl_stat_delta(void);
static void ovl_panel_live(void);
static void ovl_sprite_one(int i);
static void ovl_sprites_all(void);
static void ovl_rc_add(int b0, int b1, int r0, int r1, int near);
static void ovl_rc_px(int x, int y, int w, int h, int near);
static int  ovl_rc_clip(int k, int y0, int h);
static int  ovl_obj_due(int i);
static void ovl_rc_world(void);
static void ovl_objs_mark(void);
static void ovl_obj_vis(void);
static int  ovl_sbox(int i);
static void ovl_obox_forget(void);
static int  ovl_psb(unsigned o);
static void ovl_spray_forget(void);
static void ovl_spray_erase(void);
static void ovl_spray_rects(void);
static void ovl_vframe(int step);
static void ovl_dmg_fold(int x, int y, int w, int h);
static void ovl_mm_build(void);
static void ovl_mm_cells(void);
static int  ovl_pan_hit(int cx, int cy, int vr, int ox);
static void ovl_cursor_off(void);
static int  ovl_cur_shape(void);
static void ovl_cursor_on(void);
static void ovl_frame_first(int resume);
static void ovl_frame_draw(void);
static int  ovl_view_follow(int drew);
static void ovl_bg_redraw(int x, int y, int w, int h, int sync);
static int  ovl_rhit(int x0, int y0, int x1, int y1, int bx0, int by0,
                     int bx1, int by1);
static int  ovl_cur_hit(int cx, int cy, int drew);
static int  ovl_cmv_stepped(void);
static void lem_view_move(int dx);
static void ovl_mouse_map(void);

/* THE RASTER, and none of it is C (SPEC.md 73.11). These are the entry points
 * apps/lemmings/lemblit.inc, lemmask.inc and lemfont.inc define; they are NOT
 * static, because nasm resolves them and `nasm -f bin` has no notion of an
 * external symbol - the whole package is ONE assembly, which is what lets a C
 * declaration here and an `_lem_*` label there be the same function
 * (apps/cword/cwmove.inc is the precedent). The host harness defines the same
 * names against a model of the glass, so a drift is a COMPILE error there
 * (apps/lemmings/hosttest/os88.h). */
void lem_m_setup(unsigned maskseg);
void lem_m_clear(void);
int  lem_has_pixel(int x, int y);
void lem_set_pixel(int x, int y);
void lem_clear_pixel(int x, int y);
int  lem_probe4(int x, int y, const char *offs);
void lem_m_span(int x, int y, int w, int set);
void lem_m_apply(unsigned seg, unsigned off, int x, int y, int w, int h);
int  lem_m_dig(int x, int y, int w);
void lem_m_derive(unsigned dstseg, int kind);
void lem_o_setup(unsigned seg);
void lem_o_clear(void);
int  lem_o_get(int x, int y);
void lem_o_set(int x, int y, int v);
void lem_o_fill(int x, int y, int w, int h, int v);

void lem_r_setup(int kind, unsigned fbseg, unsigned worldseg,
                 unsigned shadowseg);
void lem_r_prep(void);
void lem_r_unprep(void);
void lem_r_pal(unsigned seg, unsigned stdoff, unsigned cusoff);
void lem_r_clearworld(int vram);
void lem_r_wsetup(unsigned p0, unsigned p3);
void lem_r_tsetup(unsigned t);
void lem_r_wpiece(unsigned seg, unsigned off, int x, int y, int w, int h,
                  int flags);
void lem_r_tload(void);
void lem_r_tmask(void);
void lem_r_derive(void);
void lem_r_scroll(int x);
void lem_r_dirty(int row, int b0, int b1);
void lem_r_undirty(void);
void lem_r_present(void);
int  lem_r_mlatch(void);
void lem_r_panel(unsigned seg, unsigned off);
void lem_r_rect(int x, int y, int w, int h, int colour, int fill);
void lem_r_batch(int on);
void lem_r_sprite(unsigned seg, unsigned off, int x, int y, int w, int h,
                  int mode);
void lem_r_srest(void);
void lem_r_sforget(void);
void lem_r_spray(unsigned seg, unsigned off, int x, int y, int b0, int b1,
                 int r0, int r1, int erase);
int  lem_r_shit(int sx0, int sy0, int sx1, int sy1);
int  lem_r_cbegin(int y0, int h, int b0, int b1);
int  lem_r_cband(int y0, int h);
void lem_r_cclip(int y0, int h, int b0, int b1);
void lem_r_clatch(void);
void lem_r_cend(void);
void lem_r_csave(void);
void lem_r_cdraw(void);
void lem_r_vstart(int x);
void lem_r_vpan(void);
void lem_r_cursor(const unsigned char *bits, int sx, int sy);
void lem_r_cplace(const unsigned char *bits, int sx, int sy);
extern unsigned char lem_sprec[];   /* lemsprite.inc's records: on VGA, the
                                     * frame's boxes (lemdraw.c lem_obox) and
                                     * after them lem_r_spray's bins */
void lem_r_bgrect(int x, int y, int w, int h, int sync);
void lem_r_show(int x, int y, int w, int h);
void lem_r_obj(unsigned seg, unsigned off, int x, int y, int w, int h,
               int flags);
void lem_r_glyph8(const unsigned char *bits, int x, int y, int h, int ink,
                  int paper);
void lem_r_panrect(unsigned seg, unsigned off, int x, int y, int w, int h);

int  lem_f_index(int ch);
void lem_f_cell(unsigned seg, unsigned off, int col, int row, int ch);
void lem_f_run(unsigned seg, unsigned off, int col, int row, const char *s,
               int n);
/* ...and the two screens' surface (wave 5, lemfont.inc's last section) */
void lem_p_setup(int kind, unsigned fbseg);
void lem_p_clear(int y0, int y1, int margins);
void lem_p_rect(int x, int y, int w, int h, int colour);
void lem_p_tile(unsigned seg, unsigned off, int y0, int y1);
void lem_p_text(unsigned seg, unsigned off, int x, int y, const char *s,
                int n);
void lem_p_mini(int y, int srow);
void lem_p_pal(unsigned mseg, unsigned moff, unsigned bseg, unsigned coff);

/* lemovl.c - everything in the overlay part (SPEC.md 73.14.2, 116.6) */
static void ovl_chrome(int screen);
static void ovl_fmt(int line, int level, char *dst, unsigned cap);
static void ovl_pvline(int line, int level, char *dst, unsigned cap,
                       int cols);
static void ovl_grey_more(int head, int on, int strid);
static void ovl_preview(int level);
static int  ovl_greyfact(int labelid, int strid, int level);
static void ovl_about(void *win);
static void ovl_data_leave(void);
static int  ovl_progress_read(void);
static int  ovl_progress_write(int rating, int level);
static int  ovl_compose(int level);
static int  ovl_spec_compose(int style);
static void ovl_types(void);
static void ovl_objects(unsigned base);
static void ovl_objmap(void);
static void ovl_game_init(int level);
static int  ovl_postview(int rating, int level, int form);
static int  ovl_scr_id(void);
static int  ovl_scr_cga(void);
static void ovl_scr_pal(void);
static void ovl_scr_text(const char *t, int n, int col, int y);
static void ovl_scr_tile(int y);
static void ovl_pv_draw(int level);
static void ovl_pv_pic(int k);
static void ovl_scr_lines(int y);
static int  ovl_po_draw(int level);

/* this file */
static int  lem_probe_mode(void *win);
static int  lem_mode_id(void);
static int  lem_kind_id(void);
static int  ovl_raster_ok(void);
static void ovl_launch(void *win);
static void ovl_leave(void);
static void lem_ask_launch(void *win);
static void ovl_post_menu(void *win);
static void lem_abandon(void);
static int  ovl_post_target(int success, int level);
static void lem_continue(void *win);
static int  ovl_key(int k);
static void ovl_panel_press(int px, int py);
static void ovl_mouse_input(int dt);
static void ovl_view_press(int x, int y, int right);
static void ovl_nuke_press(void);
static void ovl_q_apply(void);
static void ovl_play_loop(void);
static void ovl_tick_draw(int drew, int right);
static void lem_fsx_main(void);
static int  ovl_r_mode(int scr);
static int  ovl_scr_input(int post);
static int  ovl_scr_wait(int post);
static void ovl_view_start(void);
static int  ovl_pv_screen(void);
static int  ovl_po_screen(void);
static int  ovl_next_level(void);
static void ovl_session(void);
static void ovl_said_load(void);
static void ovl_said_save(void);

/* what ovl_launch() decided, before it takes the lock to act on it */
#define LEM_LR_GO     0             /* the bracket */
#define LEM_LR_FILE   1             /* a claim or a band file refused */
#define LEM_LR_MODE   2             /* os88_fsx_mode() would refuse */
#define LEM_LR_GONE   3             /* the module or the level went away */
static void lem_kick(void *win);
static void lem_abdismiss(void *win);

/* ------------------------------------------------------------------------- */
/* State. All static: see the four rules in the header above. */

static void *lem_win;
static int   lem_screen;            /* LEM_SC_* */
static int   lem_rating;            /* 0..3 */
static int   lem_sel;               /* 0..29 WITHIN the rating, never 0..119:
                                     * the launcher holds one rating at a time
                                     * and that is what makes LEMR<n>.LEM the
                                     * file (docs/lemband-format.md) */
static int   lem_top;               /* first level row shown in the list */
static int   lem_rows;              /* text rows that FIT right now */
static int   lem_cols;              /* content width in 8px cells, capped */
static int   lem_nlist;             /* level rows on the page */
static int   lem_listw;             /* the left pane's width in cells */
static int   lem_pvx, lem_pvw;      /* the preview pane's column and width */
static int   lem_playrow;           /* the Play control's row */
static int   lem_staterow;          /* the first state row */
static int   lem_playw = 8;         /* ...and the Play control's width, written
                                     * by the compose and read by the hit test
                                     * so the two cannot drift */
static int   lem_tabx[LEM_RATINGS];
static int   lem_tabw[LEM_RATINGS];
static int   lem_ok;                /* the band files were read */
static int   lem_savable;           /* SYSTEM/APPDATA answered */
static int   lem_prog[LEM_RATINGS]; /* furthest level reached per rating, -1 */
static int   lem_mode_ok;           /* this display can set 320x200x16 */
static int   lem_caps;              /* ...and its whole FSXM bitmask, which is
                                     * what lem_mode_id() picks the PLAYING
                                     * mode out of */
static int   lem_vidkind;           /* OS88_VID_* */
static int   lem_snd;               /* the speaker path answered */
static int   lem_abon;              /* the About card is up */
static int   lem_waked;             /* the first wake has happened */
static int   lem_progread;          /* ...and the progress file has been read */
static int   lem_ratpend;           /* a RATING LOAD is owed to os88_onwake:
                                     * LEMR<n>.LEM is a whole-file read and the
                                     * key that asks for it is delivered with
                                     * the gfx lock HELD (lemui.c
                                     * lem_set_rating) */
static int   lem_list_hold;         /* ...and while it is owed, the level rows
                                     * are left exactly as the shadow has them:
                                     * blanking them would be ~260 glyph cells
                                     * on an autorepeating key and then letter
                                     * the same rows again a moment later */
static int   lem_progwrite;         /* a progress WRITE is owed, for the same
                                     * reason one rating load is */
static int   lem_launchpend;        /* ...AND A LAUNCH, which is the largest of
                                     * the three by a factor of fifty: ~136 KB
                                     * of floppy I/O, and Enter, Space and a
                                     * Play click all arrive with the gfx lock
                                     * HELD (ovl_launch's own header) */
static int   lem_pw_rat;            /* ...and what it was asked to record, taken
                                     * at the keystroke rather than read back at
                                     * the wake, where the rating may have moved
                                     * on */
static int   lem_pw_lvl;
static int lem_playing = -1;        /* the level in hand, or -1 */
static int lem_resume;              /* this entry brings a kept level back */
static int lem_suspended;           /* a level is kept and the window is up */
static int lem_exit;                /* LEM_X_* */
/* the session's own state (wave 5, SPEC.md 116.4.3) */
static int lem_composed;            /* the level in hand is composed: T and
                                     * the mask hold it, and the preview
                                     * draws its picture without composing */
static int lem_started;             /* ...and its first frame has run, so the
                                     * bracket resumes at the LEVEL and not
                                     * at the preview */
static int lem_onscr;               /* the screens' mode is up: the load bar
                                     * goes into the picture's well */
static int lem_scr_fresh;           /* ovl_r_mode SET its mode this time (a
                                     * screen's VGA margins are then the
                                     * BIOS's to answer for: lem_p_clear) */
static int lem_live_id;             /* the FSXM_* id the bracket last set, -1
                                     * none yet (ovl_r_mode) */
static int lem_po_saved;            /* the bracket's postview recorded this
                                     * level's result */
static int lem_po_succ;             /* ...and it was a SUCCESS, so the menu
                                     * is already on the next level
                                     * (ovl_post_move, ovl_post_menu) */
static int lem_savfail;             /* a progress write failed where nothing
                                     * could be said (ovl_said_save) */
static int lem_po_ix;               /* the result text the postview chose -
                                     * the harness asserts it */
static int lem_scr_fy;              /* the last line's row on a screen - the
                                     * harness asserts the footer's */
static int   lem_post_next;         /* the level the postview's "continue"
                                     * goes to: the next on success, this one
                                     * again on failure (Lemmix
                                     * GameScreen.Postview.pas: "Press left
                                     * mouse button for next level / to retry
                                     * level") */
static int   lem_post_nrat;         /* ...and its RATING: a section's last
                                     * level leads to the next section's first
                                     * (Lemmix Styles.Base.pas:964-968,
                                     * TSection.DoAddLevelInfo), so Fun 30 won
                                     * goes on to Tricky 1 */
static int   lem_post_play;         /* the wake owes a preview of lem_post_next
                                     * once the new rating's file is read */
static int   lem_mode_end;          /* NO_MODE's ending: what happened to the
                                     * level (lemovl.c ovl_greyfact) */
static int   lem_gutc;              /* the GUTTER column between the two panes,
                                     * or -1. No composed run may cross it, so
                                     * the cell is always blank, always matches
                                     * the shadow and is NEVER drawn - which is
                                     * what lets ovl_chrome() put the vertical
                                     * rule inside it and never have it rubbed
                                     * out (lemui.c lem_brk) */
static int   lem_pv_hold;           /* compose the level rows but LEAVE the
                                     * preview pane as the shadow has it */
static int   lem_pv_due;            /* ...and a settle timer is pending for it */
static int   lem_glass_blank;       /* lem_blank_glass() has just filled the
                                     * content box, so ovl_chrome()'s erase arm
                                     * has nothing left to take down */
static int   lem_lv_said;           /* the toast ovl_leave() chose inside the
                                     * bracket, said by ovl_launch() after it */
static int   lem_askscr;            /* the screen a launch was asked from */

static struct os88_pt   lem_org;
static struct os88_size lem_sz;

/* the shadow */
static char          lem_sh[LEM_SH_COLS * LEM_SH_ROWS];
static unsigned char lem_sha[LEM_SH_COLS * LEM_SH_ROWS];
static int           lem_sh_ok;     /* it describes the glass */

/* scratch - static, and SHARED, so nothing here may be live across a call that
 * also uses it (LESSONS.md 13's shared-scratch trap, one level down) */
static char lem_line[LEM_SH_COLS + 1];
static char lem_num[8];
static char lem_num2[8];

/* the About card's line array. RESIDENT, because only CODE moves to the module
 * (SPEC.md 73.14) - and because os88_paint() redraws the card with
 * os88_about_card_d() without entering the module again. */
static const char *lem_ab[13];       /* twelve rows and the 0 that ends
                                     * them (lemovl.c ovl_about) */
/* 48 and not 40: the port line is 80 characters and halving it gives a
 * 41-character second half, which a 40-byte buffer cut to "...own data file"
 * on the glass (build/port-shots/wave1-vga-about.png, before this). 48 cells
 * is 384 pixels and the card clamps to the live content box, so it still fits
 * the narrow one. */
static char lem_ab1[48];
static char lem_ab2[48];

/* the cost counters the host harness prints on every build (LESSONS.md 6). A
 * visible redraw and a double-draw flash are two of the three defects that
 * never show in a screendump; this table is how they are seen at all. */
static int lem_c_calls;             /* drawing calls this repaint */
static int lem_c_cells;             /* glyph cells this repaint */
/* ...AND THE TWO THE FIRST TABLE DID NOT HAVE, which is how a repaint that
 * composed and scanned all twenty rows read as "3 calls, 78 cells" - a green
 * budget over ~400 ms of scanning nobody was counting (lemui.c lem_mark). A
 * COLUMN of a row is ~300-450 us of decided-not-to-draw on the target, so the
 * column count belongs beside the cell count or the next author reintroduces
 * the same defect against the same green number. */
/* THE COUNTERS ARE THE HOST HARNESS'S ALONE (wave 6's third review):
 * lemtest.c defines LEM_HOST and reads them; nothing in the package does, so
 * the package carries neither their bss nor their increments. LEM_COUNT,
 * LEM_CLEAR and LEM_ADD are the only way a counter is touched, and each is
 * nothing on the 8086 (apps/paccman's PMC_HOST is the shape). */
#ifdef LEM_HOST
#define LEM_COUNT(v) ((v)++)
#define LEM_CLEAR(v) ((v) = 0)
#define LEM_ADD(v, n) ((v) += (n))
#else
#define LEM_COUNT(v) ((void)0)
#define LEM_CLEAR(v) ((void)0)
#define LEM_ADD(v, n) ((void)0)
#endif

#ifdef LEM_HOST
static int lem_c_rows;              /* rows composed and scanned this repaint */
static int lem_c_cols;              /* ...and columns scanned in them */
#endif

/* THE SIX LITERALS THIS PROGRAM OWNS. Everything else the user reads is in
 * LEMSTR.LEM (SPEC.md 116.3, 116.6). These six cannot be:
 *   - the window TITLE has to outlive the window (os88.h's own rule) and so
 *     cannot point into a buffer anything reloads;
 *   - the no-data refusal is what is said when the BAND could not be read;
 *   - the About card's first row is the house form, "<product> for os8088",
 *     which apps/weave/weave.c also spells out rather than composing;
 *   ...and lemui.c owns three more of the same kind - the adapter and mode
 *   names on the Mode row, which are numbers about THIS BUILD and this machine
 *   rather than anything of the original's. */
/* ...and the TITLE is the band's LEMS_PROD where the band answered, which is
 * what the generated header is for: "a renamed string is a compile error here,
 * not a wrong sentence on the glass" (build/lemstr.h). This literal is the
 * FALLBACK - a disk whose band could not be read still gets a named window -
 * and lem_name below is what the window and the menu set are actually given.
 * It is safe to point them at the band: lem_strbuf is static bss, read once in
 * os88_main() and never reloaded, so it outlives the window (os88.h's rule). */
static const char lem_title[] = "Lemmings";
static const char *lem_name = lem_title;
static const char lem_prodline[] = "Lemmings for os8088";
static const char lem_nodata[] =
    "The converted level data is not in this folder.";

/* THE MENU SET IS EMPTY (SPEC.md 12.2, 73.12; apps/word's idiom). A package's
 * header name is 15 characters and is also the .OVL stem and the association,
 * so it cannot say what the product is called; an empty set whose AM_NAME is
 * the product puts "Lemmings" in the kernel bar instead of the header's
 * "LEMMINGS". A set with no menus has no item to pick, so os88_oncmd() below
 * can never be called - but CC_HAS_MENUS has to be declared anyway, because
 * os88_menu_set() is the slot that patches the trampoline in.
 *
 * NOT const: os88_menu_set() WRITES set->oncmd, and a set in .rodata makes that
 * patch silent nonsense (os88.h says so at the struct). */
static struct os88_menuset lem_menus = { lem_title, 0, 0, { { 0, 0, 0 } } };

/* --- the parts of the program, in dependency order -------------------------
 * Every one of these is a written prerequisite of $(BUILD)/lemmings.raw.asm.
 * lemovl.c is LAST because it names what the four above it define, and because
 * that is where the module boundary falls in the generated assembly. */
#include "lemtab.c"                 /* const tables and field offsets only */
#include "lemtext.c"                /* the string ACCESSOR, not the strings */
#include "lemload.c"                /* the band reader, resident half */
#include "lemgame.c"                /* the level's state and the frame step */
#include "lemobj.c"                 /* the objects, and the frames' rows */
#include "lemact.c"                 /* the eighteen actions and the skills */
#include "lemui.c"                  /* the launcher */
#include "lemdraw.c"                /* the frame (SPEC.md 116.4) */
#include "lemovl.c"                 /* everything ovl_* -> the overlay PART */

/* ============================================================================
 * The two things this file decides for itself
 * ==========================================================================*/

/* lem_probe_mode - which foreign mode this DISPLAY could play in, asked with
 * OUR window.
 *
 * kernel/fsx.inc's own comment records the one mistake here: a package that
 * asked before its window existed was told about the display it was LAUNCHED
 * from. So this runs after os88_wm_create() and takes lem_win.
 *
 * THE BIT THAT GREYS THE MODE ROW IS THE BIT os88_fsx_mode() WOULD REFUSE ON,
 * which is SPEC.md 47 rule 4's one predicate for both. An EGA answers 0x000F -
 * the CGA-compatible modes only (kernel/fsx.inc:117) - so a sixteen-colour card
 * gets four colours and the row says so with the fact. */
static int lem_probe_mode(void *win)
{
    static int kind;                /* an out-parameter, so a static (73.5) */
    int caps;

    kind = OS88_VID_VGA;
    caps = os88_fsx_caps(win, &kind);
    lem_vidkind = kind;
    lem_caps = caps;
    if (caps < 0)
        return 0;
    return (caps & (1 << OS88_FSXM_VGA0D)) != 0;
}

/* lem_mode_id / lem_kind_id / ovl_raster_ok - WHICH MODE THIS ADAPTER PLAYS IN,
 * and it is not the one the Mode row is about.
 *
 * The Mode ROW is greyed by lem_mode_ok, which asks specifically for
 * FSXM_VGA0D - the 320x200x16 mode - because what that row states is a fact
 * about COLOUR: "kernel/fsx.inc:117 fsx_capstab gives an EGA 0x000F, the
 * CGA-compatible modes only, so a sixteen-colour card gets four" (SPEC.md
 * 116.8). A card that cannot set 0Dh still PLAYS, in four colours or in
 * monochrome; greying the launch on that predicate would refuse the game on
 * every machine but a VGA.
 *
 * So there are two predicates and they answer different questions, and each is
 * the one the ACTION it guards would refuse on, which is SPEC.md 47 rule 4
 * applied twice rather than once. */
static int lem_mode_id(void)
{
    if (lem_vidkind == OS88_VID_HERC)
        return OS88_FSXM_HERC;
    if (lem_caps > 0 && (lem_caps & (1 << OS88_FSXM_VGA0D)))
        return OS88_FSXM_VGA0D;
    return OS88_FSXM_CGA320;
}

static int lem_kind_id(void)
{
    if (lem_vidkind == OS88_VID_HERC)
        return LEM_RKIND_HERC;
    if (lem_mode_id() == OS88_FSXM_VGA0D)
        return LEM_RKIND_VGA;
    return LEM_RKIND_CGA;           /* an EGA plays here too: its caps row is
                                     * 0x000F, the CGA-compatible modes only */
}

static int ovl_raster_ok(void)
{
    return lem_caps > 0 && (lem_caps & (1 << lem_mode_id())) != 0;
}

/* THE MODULE NEEDS NOTHING FROM ANY CALLBACK. It is a PART of LEMMINGS.O88
 * (CC_OVL_PART, SPEC.md 73.14.2), and an EAGER one: op_load reads it with the
 * package at launch and cc_ovinit binds it before os88_main() runs
 * (apps/cc/crt0.asm), so there is no state of this program in which an ovl_
 * call can refuse - a machine without the RAM for it refuses the LAUNCH, in
 * the kernel's own words, before a window exists. Waves 1 and 2 shipped it as
 * LEMMINGS.OVL beside the package and forced it resident on the first wake;
 * that machinery (lem_ovl, ovl_ready, a first W_PAINT that drew the preview
 * pane empty and waited) is gone with the file. What the first wake still owes
 * is the progress file's dive (os88_onwake). */

/* lem_kick - re-post the wake when an errand is still owed.
 *
 * os88_wm_wake() answers -1 "when the ring was full of other events and nothing
 * was posted: kick again from your next callback" (os88.h), and this program
 * now owes THREE things to os88_onwake() - the module, the rating file and the
 * progress write. Ignoring that answer is a rating tab that moved with a list
 * under it that never followed, or a progress file that is never written, and
 * neither says anything on the glass - and since wave 2 it owes a FOURTH, the
 * launch itself. So every callback that the user's own
 * input reaches kicks again first. It costs nothing when a wake is already
 * queued: the kernel keeps at most one per window, "so kicking from every
 * callback is free and cannot fill the 16-record event ring". */
static void lem_kick(void *win)
{
    if (lem_ratpend || lem_progwrite || lem_launchpend || !lem_waked)
        os88_wm_wake(win);
}

/* lem_abdismiss - take the About card down.
 *
 * THE CARD IS OPAQUE OVER ITS OWN RECT (apps/os88ui.inc's os88ui_about_d): it
 * overwrote the cells under it without the shadow knowing, so a repaint that
 * still trusts the shadow finds every composed run equal to it, marks nothing
 * dirty and issues ZERO drawing calls - the card stays on the glass until the
 * next expose or resize. apps/weave/weave.c's w_abdismiss is the precedent and
 * says the same thing in the same words ("the whole content: the card was
 * opaque over its own rect").
 *
 * INVALIDATING THE SHADOW IS NOT ENOUGH, AND THE GLASS IS WHERE THAT WAS
 * SETTLED. `lem_sh_ok = 0` re-letters every CELL, and a cell is 8 pixels of a
 * 9-pixel pitch (LEM_RPITCH) with LEM_PADX/PADY of 2 around the block: the
 * card's frame and its drop shadow cross the LEADING row between every pair of
 * text rows and the columns past the last whole cell, and no font_run of ours
 * ever writes those pixels. So the first build of this fix left a dotted ghost
 * of the card's border behind on a dismissal - visible in
 * build/port-shots/w2-dismiss-zoom.png, a dashed rule across the preview pane
 * and a dotted column down its right edge - with every character correctly
 * re-lettered around it.
 *
 * SO IT FILLS, WHICH IS WHAT weave's w_repaint2 DOES FOR IT AND THIS PROGRAM
 * HAS NEVER DONE. One os88_gfx_fill over the content box is 756 us, it puts
 * back exactly the white ground kernel/wm.inc's WF_OWNBG interlock lays down
 * before a W_PAINT, and it lets the shadow be SEEDED blank rather than
 * invalidated - so the repaint that follows draws the cells that carry
 * something and skips the ones that do not, which is the same 28 calls a
 * W_PAINT costs instead of every blank cell in the box. The fill and the seed
 * are one fact stated twice and must not drift.
 *
 * The clip region is armed here and lem_repaint() is told NOT to arm it again:
 * the fill has to be inside the same region as the letters, and a refusal
 * means not one pixel of us shows, so nothing is drawn and the shadow is left
 * INVALID for the expose that follows. */
static void lem_abdismiss(void *win)
{
    lem_abon = 0;
    /* THE HELPER IS SHARED WITH THE THREE SCREEN TRANSITIONS (lemui.c
     * lem_blank_glass), because a dismissed opaque card and a screen change are
     * the same situation: a shadow describing pixels that are no longer there,
     * where every stale character would otherwise be paid as a glyph cell. */
    if (!lem_blank_glass(win))
        return;
    lem_repaint(win, 1, 0);             /* 0: the region above is ours */
}

/* ============================================================================
 * The callbacks the shim declares (apps/lemmings/lemmings.asm)
 * ==========================================================================*/

void os88_paint(void *win)          /* W_PAINT: the gfx lock is ALREADY held */
{
    lem_kick(win);                  /* free when one is queued, and the only
                                     * thing that recovers a REFUSED post */

    /* NO DISK I/O HERE: W_PAINT arrives with the gfx lock HELD and must not
     * block, so the progress file is the first wake's (os88_onwake). The
     * module is not a read at all any more - it is resident since launch
     * (CC_OVL_PART), so this first paint draws the pane and the chrome too.
     *
     * The kernel has just white-filled the whole content under us, so the
     * shadow does not describe the glass any more - but it is not UNKNOWN
     * either, which is the difference that pays for itself: it describes a
     * blank box, so the repaint below draws the cells that carry something and
     * skips the ones that do not (LESSONS.md 6, and kernel/wm.inc's WF_OWNBG
     * interlock is what makes the fill a fact rather than a hope). */
    lem_sh_blank();
    lem_glass_blank = 1;            /* ...and the fill took ovl_chrome()'s two
                                     * rules down with everything else, so a
                                     * preview or postview has none to erase */
    lem_repaint(win, 1, 0);         /* 0: W_PAINT arrives with the region
                                     * ALREADY armed (SPEC.md 11.3) and it is
                                     * the ONE callback that does */
    if (lem_abon)
        os88_about_card_d(win, lem_ab);   /* _d: this paint's region is armed
                                           * already (SPEC.md 20.5.1.1) */
}

/* W_ONRESIZE (SPEC.md 11.98): the content box CHANGED and we did not ask - an
 * adapter change, or a drag onto a shorter display. WE MUST NOT DRAW HERE; a
 * full repaint follows immediately. What this is for is being laid out
 * correctly before that paint runs rather than a frame later. */
void os88_onresize(int w, int h, void *win)
{
    (void)w;
    (void)h;

    /* RE-PROBE, BECAUSE THIS CALLBACK IS DELIVERED ON A CHANGE OF ADAPTER KIND
     * (SPEC.md 39.16.3.3 and 11.98): drag the launcher from the VGA onto the
     * Hercules on vm/xt-multimon and the display answering for this window is a
     * different card with a different caps mask. lem_mode_ok was written once,
     * in os88_main(), and the Mode row then lettered "VGA / 320x200x16" and
     * refused to grey on a display where os88_fsx_mode() would refuse - which
     * is SPEC.md 47 rule 4's ONE PREDICATE FOR BOTH turning into two, and is
     * exactly the mistake kernel/fsx.inc records ("pass your own window and ask
     * AGAIN where the answer is used"). It decides and draws nothing, which is
     * what W_ONRESIZE requires; lem_probe_mode() also rewrites lem_vidkind from
     * the answering display, so lem_adapter() follows it. */
    lem_mode_ok = lem_probe_mode(win);

    lem_sh_ok = 0;
    lem_layout(win);
    /* A paragraph is wrapped to the LIVE content width, so a box that just
     * changed width has a stale wrap in it - and the row composer would then
     * hand os88_font_run() lines longer than the window and rely on lem_fit to
     * cut them, which loses the words rather than moving them. Re-wrapping here
     * is one pass over ~500 bytes and it happens before the repaint that
     * follows this callback, which is the whole reason W_ONRESIZE exists
     * (SPEC.md 11.98: be laid out correctly BEFORE that paint, and draw
     * nothing here). */
    if (lem_screen != LEM_SC_LIST)
        lem_wrap();
}

void os88_onclick(int x, int y, void *win)
{
    int hit;

    lem_kick(win);
    if (lem_abon) {
        lem_abdismiss(win);
        return;
    }
    if (lem_launchpend)
        return;                     /* a launch is owed to the wake and has
                                     * chosen its level: nothing moves under it
                                     * (lem_ask_launch) */
    if (lem_screen != LEM_SC_LIST) {
        /* The original's own footer on both screens: "Press mouse button to
         * continue" (Lemmix Base.Strings.pas SPreviewScreen_...), and on the
         * PREVIEW screen "continue" means START THE LEVEL - or bring a kept
         * one back - which is what it means in the original too. The
         * POSTVIEW's left button is the next level or the retry; the FACT
         * screen has nothing to continue into and goes back to the list. */
        lem_continue(win);
        return;
    }

    hit = lem_hit(x, y);
    if (hit == LEM_HIT_NONE)
        return;
    if (hit >= LEM_HIT_ROW) {
        lem_select(win, lem_top + (hit - LEM_HIT_ROW));
        return;
    }
    if (hit >= LEM_HIT_STATE && hit < LEM_HIT_STATE + LEM_STATE_ROWS) {
        /* A greyed control answers with the FACT that greys it (SPEC.md 47),
         * and the fact is a sentence in the band rather than in this image. */
        hit -= LEM_HIT_STATE;
        if (lem_state_grey(hit))
            lem_show_fact(win, lem_state_label(hit), lem_state_fact(hit), -1);
        return;
    }
    if (hit >= LEM_HIT_TAB && hit < LEM_HIT_TAB + LEM_RATINGS) {
        lem_set_rating(win, hit - LEM_HIT_TAB);
        return;
    }
    if (hit == LEM_HIT_PLAY)
        lem_play(win);
}

void os88_onkey(int ascii, int scan, void *win)
{
    int n;

    lem_kick(win);
    if (lem_abon) {
        lem_abdismiss(win);
        return;
    }
    if (lem_launchpend)
        return;                     /* typed ahead of a pending launch: the
                                     * level is chosen and the glass is not the
                                     * list yet (lem_ask_launch) */
    if (lem_screen != LEM_SC_LIST) {
        if (ascii == 27) {
            /* Esc is the original's "menu": a kept level or a finished one
             * is let go, and its claims with it */
            if (lem_screen == LEM_SC_POST) {
                ovl_post_menu(win);
                return;
            }
            if (lem_suspended)
                lem_abandon();
            lem_back(win);
        } else if (ascii == 13 || ascii == ' ' ||
                   ((lem_suspended || lem_screen == LEM_SC_POST) &&
                    (ascii == 'f' || ascii == 'F'))) {
            /* 'f' is SPEC.md 11.2.1's door BACK IN: for a level 'f' took out,
             * and from the postview 'f' took out of the bracket, which goes on
             * into the next level's preview there (wave 5) */
            lem_continue(win);
        }
        return;
    }

    n = lem_sel;
    if (scan == LEM_SC_UP)
        n--;
    else if (scan == LEM_SC_DOWN)
        n++;
    else if (scan == LEM_SC_PGUP)
        n -= lem_nlist;
    else if (scan == LEM_SC_PGDN)
        n += lem_nlist;
    else if (scan == LEM_SC_HOME)
        n = 0;
    else if (scan == LEM_SC_END)
        n = LEM_PERRAT - 1;
    else if (scan == LEM_SC_LEFT) {
        lem_set_rating(win, (lem_rating + LEM_RATINGS - 1) % LEM_RATINGS);
        return;
    } else if (scan == LEM_SC_RIGHT) {
        lem_set_rating(win, (lem_rating + 1) % LEM_RATINGS);
        return;
    } else if (ascii == 13 || ascii == ' ') {
        lem_play(win);
        return;
    } else {
        return;
    }

    if (n < 0)
        n = 0;
    if (n >= LEM_PERRAT)
        n = LEM_PERRAT - 1;
    lem_select(win, n);
}

/* W_ONWAKE (SPEC.md 74.1) - the one callback that runs WITHOUT the gfx lock.
 *
 * THE DISK TIME LIVES HERE AND NOWHERE ELSE. os88.h: W_ONWAKE is lock free, on
 * the UI task, "may call the file slots", and may take the lock itself around a
 * burst of drawing you can put a number on. The progress file's dive - the root
 * of this volume, then SYSTEM, then APPDATA, then back - is that disk time, so
 * it is here rather than in W_PAINT, which must not block. The burst that
 * follows it is one PARTIAL repaint: the only thing the answer can change is
 * the Save Progress row's greying and its value. */
void os88_onwake(void *win)
{
    int drew, was;

    lem_waked = 1;
    drew = 0;

    if (!lem_progread) {
        lem_progread = 1;
        lem_savable = ovl_progress_read();
        lem_mark_range(lem_staterow, lem_staterow + LEM_STATE_ROWS - 1);
        drew = 1;
        /* the saved progress is where the player starts, as the original's
         * code entry would put them (wave 4 review) - unless they have
         * already moved.
         *
         * AND IT IS A SELECTION MOVE, NOT A FULL REPAINT (wave 4's third
         * review): this wake follows the first W_PAINT, which has just drawn
         * every row, and `full` composed and scanned all of them across all
         * their columns again (~0.4 s on the target) and drew the chrome's two
         * rules over themselves. What the move changes is two level rows, the
         * Play control's level number and the preview pane - lem_select's own
         * set - and lem_repaint() turns a page itself when the saved level is
         * off the first one. */
        if (lem_sel == 0 && lem_prog[lem_rating] > 0) {
            was = lem_sel;
            lem_sel = lem_prog[lem_rating];
            lem_mark_level(was);
            lem_mark_level(lem_sel);
            lem_mark_cols(lem_playrow, 0, lem_playw - 1);
            lem_mark_pane_rows();
        }
    }

    /* THE RATING THE USER ASKED FOR, read here rather than on the key that
     * asked. LEMR<n>.LEM is a whole-file read - a directory walk, a FAT walk
     * and the data run, three int 13h calls at ~400 ms apiece on the target -
     * and Left, Right and a tab click are all delivered with the gfx lock HELD
     * (os88.h at os88_onkey / os88_onclick). Left AUTOREPEATS at ~100 ms, so
     * that was ten presses queued behind every freeze. The flag is RE-ARMED and
     * never stacked: a second rating key while one is owed simply moves
     * lem_rating, and this reads whichever one the user stopped on. */
    if (lem_ratpend) {
        lem_ratpend = 0;
        lem_rating_load(lem_rating);
        lem_sel = lem_prog[lem_rating] > 0 ? lem_prog[lem_rating] : 0;
        lem_top = 0;                /* lem_layout pages to lem_sel */
        drew = 1;
        /* every level row, the pane beside them and the Play control - and
         * NOT the chrome or the rows the rating key already drew (the tab
         * strip and the state rows, lemui.c lem_set_rating): `full` re-drew
         * both rules over themselves and scanned the whole box (wave 4's
         * third review) */
        lem_mark_range(LEM_LISTTOP, lem_playrow);
    }

    /* ...AND THE PROGRESS WRITE, which is the same defect one size down: two
     * ordinal walks to find SYSTEM and APPDATA and then a write that rewrites
     * the FAT and a directory entry, on Enter, Space or a Play click. A write
     * that did not happen is said by ovl_progress_write itself (the record
     * back to the file's, the row greyed, its fact as a toast), and its rows
     * are repainted here (wave 4 review: the status was dropped). */
    if (lem_progwrite) {
        lem_progwrite = 0;
        /* THE STATE ROWS ONLY WHERE THEY ARE ON THE GLASS (wave 4's third
         * review): the write is owed from the POSTVIEW (ovl_post_target), so
         * the screen is usually LEM_SC_POST, and marking the state rows there
         * scanned 3-4 rows of a paragraph for no pixel. The list draws the
         * greyed row itself when it comes back, in full (lem_back). */
        if (!ovl_progress_write(lem_pw_rat, lem_pw_lvl) &&
            lem_screen == LEM_SC_LIST) {
            lem_mark_range(lem_staterow, lem_staterow + LEM_STATE_ROWS - 1);
            drew = 1;
        }
        ovl_said_save();            /* LEMS_SAV_FAIL, when it failed */
    }

    /* ...AND THE POSTVIEW'S "next level" ACROSS A SECTION'S END, once the
     * next rating's file is in: its first level's preview, as Lemmix's
     * NextInfo has it (lem_continue owed it here, because the keystroke may
     * not read the file). */
    if (lem_post_play == 2 && !lem_ratpend) {
        /* ...or, from the window's postview's Esc, only SELECTED
         * (ovl_post_menu): the rating arm above has marked the rows, and the
         * repaint below draws them with this selection */
        lem_post_play = 0;
        lem_sel = lem_post_next;
        lem_top = 0;
    }
    if (lem_post_play && !lem_ratpend) {
        lem_post_play = 0;
        lem_sel = lem_post_next;
        lem_top = 0;
        os88_gfx_lock();
        if (lem_play_ok())
            lem_play(win);
        else
            lem_back(win);          /* the list says why, row by row */
        os88_gfx_unlock();
        return;
    }

    /* ...AND THE LAUNCH, which is the largest of the four by a factor of fifty
     * and the reason this callback exists at all in wave 2: LEMMAIN.LEM is
     * 60,416 bytes and a style bank up to 78,848 more, ~136 KB and ~350
     * int 13h calls at PERFORMANCE.md's ~400 ms apiece. lem_ask_launch()'s
     * header says what doing it on the keystroke cost. It is LAST because the
     * errands above are what a launch depends on, and it does not fall through to the repaint below: what follows a
     * launch is either the kernel's own W_PAINT out of the bracket or a refusal
     * that drew itself. */
    if (lem_launchpend) {
        lem_launchpend = 0;
        ovl_launch(win);
        return;
    }

    if (!drew || !lem_sh_ok)
        return;                     /* the first paint has not run yet, and it
                                     * will draw the answer itself */
    if (lem_abon)
        return;                     /* the card is up and OPAQUE over its own
                                     * rect: painting the launcher under it
                                     * would mark those cells CLEAN in a shadow
                                     * that does not describe the glass, which
                                     * is the failure lem_abdismiss() is written
                                     * about. The dismissal repaints in full and
                                     * picks all of this up */
    os88_gfx_lock();
    /* PARTIAL in every case now: the progress file's answer changes the
     * Save Progress row and moves the selection, a rating change the level
     * rows and the pane, and the first W_PAINT has already drawn the rest -
     * pane and chrome included, the module being resident since launch
     * (CC_OVL_PART). */
    lem_repaint(win, 0, 1);
    os88_gfx_unlock();
}

/* W_ONTIMER (SPEC.md 13.9) - ONE-SHOT, and it exists for exactly one thing: the
 * preview pane SETTLES.
 *
 * An arrow key changes the selection, which changes two level rows AND all
 * seven preview lines beside them - and the pane is ~140 of the ~180 glyph
 * cells a selection move costs, which is ~126 ms of the ~168 on a 4.77 MHz 8088
 * (PERFORMANCE.md's ~900 us a cell). Held down, the machine falls further
 * behind on every repeat, which is INPUT OVERRUN, the third of the three
 * defects PERFORMANCE.md Part 1 says an emulator cannot show you. So the list
 * rows are drawn on the keystroke and the pane is armed for LEM_PV_SETTLE ticks
 * and re-armed by each further key: one pane repaint after the arrow stops,
 * instead of one per repeat. This callback runs in W_ONCLICK's environment -
 * UI task, gfx lock HELD - so it may draw, and it arms its own clip region
 * because the kernel arms one for W_PAINT and for nothing else. */
void os88_ontimer(void *win)
{
    lem_kick(win);
    if (!lem_pv_due)
        return;                     /* a stale shot: be indifferent to it */
    lem_pv_due = 0;
    if (lem_abon || lem_screen != LEM_SC_LIST)
        return;
    /* THE PANE'S OWN HALF OF ITS OWN ROWS AND NOTHING ELSE (lemui.c
     * lem_mark_pane): the level rows went down on the keystroke that armed
     * this shot, list half and all, so marking those rows WHOLE would scan 252
     * columns - 76-113 ms - to draw nothing at all. */
    lem_mark_pane_rows();
    lem_repaint(win, 0, 1);
}

void os88_oncmd(int item, int menu, void *win)
{
    /* Unreachable by construction: the registered set has no menus. It exists
     * so that its AM_NAME names the product in the kernel bar. */
    (void)item;
    (void)menu;
    (void)win;
}

void os88_about(void *win)
{
    lem_abon = 1;
    ovl_about(win);                 /* the plain form: os88_about() arrives with
                                     * NO clip region armed (SPEC.md 20.5.1.1) */
}

/* ============================================================================
 * THE EXCLUSIVE BRACKET (SPEC.md 53.1, 116.5)
 *
 * os88_fsx_run() calls this and does not return until it rets. While it runs
 * the scheduler passes only this task, the video mode is ours, and EVERY
 * KERNEL DRAWING SLOT IS REFUSED - os88_gfx_*, os88_font_*, the window slots
 * and a kernel TOAST, which would paint desktop geometry into a foreign mode.
 * So everything that can refuse has already happened in ovl_launch(), outside,
 * where a refusal is a sentence in a window - the claims, the banks AND the
 * level's record, which wave 2 read in here (finding (1) of its last review).
 *
 * THE LOOP TURNS ONCE A TICK AND THE WORLD STEPS WHEN IT IS DUE. Each pass
 * drains EVERY key the BIOS holds (finding (3) of that review: one key a frame
 * was a backlog on the two cards whose scrolling frame is 178 ms, and Esc sat
 * behind it), samples the mouse, moves the view, steps at most LEM_STEP_MAX
 * lemmings of the current game frame, and repaints the pointer - so pointer
 * latency is one tick whatever a frame of the world costs, and a frame of
 * sixty lemmings on an XT simply takes several passes (SPEC.md 116.7 point 3).
 * The game frame is due 17 times a second (Lemmix's clock) by an accumulator
 * of ticks; a frame that ran late is NOT repaid in a burst, so a slow machine
 * plays slower and never in fits - decision 7.
 *
 * It is a plain RESIDENT function whose address os88_fsx_run() is handed -
 * never an ovl_, which tools/cc8086.py refuses by name, because the bracket
 * has parked the machine that would load the module (os88.h, SPEC.md 73.14).
 * The module is resident long before (it is a PART of the package, eager -
 * SPEC.md 73.14.2), so the ovl_ calls made from in here are four-byte no-op
 * guards and a far call.
 * ==========================================================================*/

/* The scan codes this loop reads. os88_fsx_key() answers (scan << 8) | ascii
 * and uses the ENHANCED BIOS pair where the BIOS says it has one, which is why
 * F11 and F12 arrive on an AT and are absent on an 83-key XT - a fact the Mode
 * row states rather than a defect (SPEC.md 116.8). The map is Lemmix's
 * src/GameScreen.Player.pas:519-531 (F1-F12) and :476-487 (Ctrl+F1/F2). */
#define LEM_K_ESC    0x01
#define LEM_K_LEFT   0x4B
#define LEM_K_RIGHT  0x4D
#define LEM_K_F1     0x3B
#define LEM_K_F2     0x3C
#define LEM_K_F3     0x3D
#define LEM_K_F10    0x44
#define LEM_K_F11    0x85
#define LEM_K_F12    0x86
#define LEM_K_CF1    0x5E
#define LEM_K_CF2    0x5F
#define LEM_K_CLEFT  0x73           /* Ctrl+Left / Ctrl+Right: int 16h's own */
#define LEM_K_CRIGHT 0x74           /* codes for the pair, on both BIOS pairs */
/* ...and the MAKE codes the kernel's key map is indexed by (os88_key_down),
 * which is how a held F1 is told from a released one: int 16h has presses
 * and no releases */
#define LEM_M_F1     0x3B
#define LEM_M_F2     0x3C
#define LEM_M_F11    0x57
#define LEM_M_F12    0x58
#define LEM_M_CTRL   0x1D

/* LEMMIX'S THREE ADDITIONS TO THE KEYBOARD, carried and named as such
 * (SPEC.md 116.10): Left / Right scroll the view and Ctrl+Left / Ctrl+Right
 * scroll it one pixel (GameScreen.Player.pas:493-509 and :548-560, KeyDown
 * sets KeyBoardScroll and CheckScroll moves SCROLL_PIXELS_NORMAL = 8, or
 * SCROLL_PIXELS_PRECISE = 1 with Ctrl, every INTERVAL_SCROLL = 58 ms until
 * KeyUp), and Ctrl+F1 / Ctrl+F2 set the release rate to its extreme while
 * paused (:475, "#EL 2008-04-28 ... *only* when pausing!"). None of them is
 * the 1991 game's, whose keys are F1-F12; each displaces nothing. */
#define LEM_SCROLL_STEP  8          /* world pixels a TICK, of a HELD arrow
                                     * or of the pointer against the edge:
                                     * Lemmix's SCROLL_PIXELS_NORMAL, every
                                     * INTERVAL_SCROLL = 58 ms
                                     * (GameScreen.Player.pas:251, :37), so
                                     * 8 x 18.2 = 146 px a second against
                                     * Lemmix's ~138. Eight is also a whole
                                     * snap on every card - two CGA bytes, one
                                     * Hercules byte - so Hercules moves on
                                     * every step. The step is multiplied by
                                     * the ticks the pass took
                                     * (ovl_mouse_input, ovl_arrow_hold), up to
                                     * the four ticks ovl_play_loop clamps dt
                                     * to - a 178 ms shadow-scrolling pass is
                                     * ~3.2 ticks and moves 24 px. A shadow
                                     * scroll is priced per COMPOSE, not per
                                     * pixel, so a late pass moving further is
                                     * free. (Wave 3 took 4, the CGA's snap,
                                     * which was half Lemmix's speed and a
                                     * number from neither reference.) */
#define LEM_SCROLL_MAX  (8 * LEM_SCROLL_STEP)   /* a tick's whole fold */

/* THE SUPERLEMMING (LVL 0x001E, wave 4): the level runs fast, at LEMMIX'S
 * ratio, because the base credit of 170 a tick IS Lemmix's 58 ms frame:
 * INTERVAL_FRAME_SUPERLEMMING 20 against INTERVAL_FRAME 58
 * (GameScreen.Player.pas:34-35, read at :849-851 and :1046-1049), so the
 * frame credit a tick is 170 x 58 / 20 = 493 - up to three game frames a
 * pass, and on an XT still one frame however long it takes. (Wave 4 first
 * took lemmings_3ds's 71/28 = 431 on top of Lemmix's base, two references
 * mixed, and said Lemmix never read its constant; review.) No level of the
 * 120 sets the flag (measured, tools/os88lem.py); it is carried so the
 * record's every field means what it says. */
#define LEM_SUPER_CREDIT 493
#define LEM_SUPER_MAX      3
#define LEM_DBLKEY       5          /* ticks, F12 twice: Lemmix's
                                     * INTERVAL_NUKE_KEY, 250 ms
                                     * (GameScreen.Player.pas:38, :453) */
#define LEM_DBLBTN       9          /* ticks, the panel's mushroom clicked
                                     * twice: Lemmix asks the OS for a double
                                     * click (ssDouble, Player.pas:928-930),
                                     * 500 ms by default, which is also this
                                     * kernel's own double-click window
                                     * (UI_TDBLT, DESK_DBLT and FM_DBLCLK are
                                     * all 9 ticks - SPEC.md 11.95) */

#define LEM_X_FAIL     0            /* why the bracket returned: the compose
                                     * refused (a band read) */
#define LEM_X_SUSPEND  1            /* 'f': the level is kept */
#define LEM_X_DONE     2            /* the play loop's verdict, or Esc - and,
                                     * leaving the bracket, "the postview in
                                     * the WINDOW": 'f' on the postview, or
                                     * its mode refused */
#define LEM_X_MODE     3            /* os88_fsx_mode() answered no */
#define LEM_X_MENU     4            /* wave 5: the original's "menu" - Esc on
                                     * either screen, the right button on the
                                     * postview - back to the level list */
#define LEM_X_LOAD     5            /* ...the NEXT level refused to load
                                     * inside the bracket (lem_load_err) */
#define LEM_X_GONE     6            /* ...or is not on this disk */

/* what one of the two screens was answered with (ovl_scr_input) */
#define LEM_SI_GO      0            /* the left button, Enter, Space */
#define LEM_SI_MENU    1            /* Esc; the right button on the postview */
#define LEM_SI_DOOR    2            /* 'f' / 'F' - SPEC.md 11.2.1's door */
#define LEM_SI_NOMODE  3            /* the screens' mode was refused */
#define LEM_SI_FAIL    4            /* the compose refused */

static int lem_rrkey;               /* the make code holding the rate, or 0 */
static int lem_rrbtn;               /* ...or the panel button holding it */
static int lem_mbtn;                /* the mouse buttons last tick */
static int lem_mmdrag;              /* a left press began in the minimap */
static unsigned lem_nuket;          /* the last nuke press, in ticks */
static int lem_f11up, lem_f12up;    /* the key was seen UP since its last press */
static int lem_arkey;               /* the arrow's make code held, or 0 */
static int lem_arsign;              /* ...its direction, -1 or 1 */
static int lem_arnew;               /* ...pressed THIS pass: one step owed
                                     * even if it is already up again */
static int lem_dx;                  /* this tick's scroll, folded from every
                                     * key and the pointer - a static, because
                                     * an automatic's address is refused
                                     * (SPEC.md 73.5) */

/* ovl_key - one key out of the BIOS. LEM_X_* to leave, or -1; lem_dx is the
 * scroll this tick so far. */
static int ovl_key(int k)
{
    int scan = (k >> 8) & 0xFF;
    int ascii = k & 0xFF;
    unsigned now;

    if (scan == LEM_K_ESC) {
        lem_finished = 1;           /* Lemmix: "if Key = VK_ESCAPE then
                                     * Game.Finish" (:463) */
        return LEM_X_DONE;
    }
    if (ascii == 'f' || ascii == 'F')
        return LEM_X_SUSPEND;       /* SPEC.md 11.2.1's fullscreen door, a
                                     * BINDING reservation in both directions */
    if (scan == LEM_K_LEFT || scan == LEM_K_CLEFT) {
        /* KeyDown sets KeyBoardScroll and the TICK scrolls (ovl_arrow_hold):
         * a typematic repeat only says the key is still down */
        lem_arkey = LEM_K_LEFT;
        lem_arsign = -1;
        lem_arnew = 1;
    } else if (scan == LEM_K_RIGHT || scan == LEM_K_CRIGHT) {
        lem_arkey = LEM_K_RIGHT;
        lem_arsign = 1;
        lem_arnew = 1;
    } else if (scan == LEM_K_F1) {
        lem_rrdir = -1;
        lem_rrkey = LEM_M_F1;
    } else if (scan == LEM_K_F2) {
        lem_rrdir = 1;
        lem_rrkey = LEM_M_F2;
    } else if (scan == LEM_K_CF1) {
        ovl_rr_extreme(0);
    } else if (scan == LEM_K_CF2) {
        ovl_rr_extreme(1);
    } else if (scan >= LEM_K_F3 && scan <= LEM_K_F10) {
        ovl_select_skill(LB_CLIMBER + (scan - LEM_K_F3));
    } else if (scan == LEM_K_F11) {
        if (lem_f11up)              /* "if fKeyIsDown < 2": a typematic
                                     * repeat does not toggle again */
            ovl_toggle_pause();
        lem_f11up = 0;
    } else if (scan == LEM_K_F12) {
        /* "double keypress needed to prevent accidently nuking" (:449) */
        if (lem_f12up) {
            now = os88_ticks();
            if (now - lem_nuket <= LEM_DBLKEY)
                ovl_nuke_press();
            lem_nuket = now;
        }
        lem_f12up = 0;
    }
    return -1;
}

/* ovl_panel_press - a left press on the panel at (px, py): the minimap, or
 * one of the twelve buttons (Game.SkillPanel.pas ImgMouseDown, and
 * GameScreen.Player.pas ToolBar_MouseDown with Lemmix's default
 * SkillButtonsEnabledWhenPaused). */
static void ovl_panel_press(int px, int py)
{
    int b;
    unsigned now;

    /* LEMMIX'S BOXES, RIGHT AND BOTTOM EXCLUSIVE. The minimap is
     * DosMiniMapCorners (208,18)-(311,37) under TRect.Contains, so x 208..310
     * and y 18..36 - one short of the 104x20 it is DRAWN at. A button is
     * Rect(1,16,15,38) + 16n under PtInRectEx (Game.SkillPanel.pas:89), so
     * x 16n+1..16n+14 and y 16..37. */
    if (px >= LEM_MM_X && px < LEM_MM_X + LEM_MM_W - 1 && py >= LEM_MM_Y &&
        py < LEM_MM_Y + LEM_MM_H - 1) {
        lem_mmdrag = 1;
        return;
    }
    if (px < 1 || py < 16 || py >= 38)
        return;
    b = (px - 1) >> 4;
    if (b >= LB_COUNT || px >= b * 16 + 15)
        return;
    switch (b) {
    case LB_SLOWER:
        lem_rrdir = -1;
        lem_rrbtn = 1;
        break;
    case LB_FASTER:
        lem_rrdir = 1;
        lem_rrbtn = 1;
        break;
    case LB_PAUSE:
        ovl_toggle_pause();
        break;
    case LB_NUKE:
        now = os88_ticks();
        if (now - lem_nuket <= LEM_DBLBTN)  /* "nuke requires doubleclick" */
            ovl_nuke_press();
        lem_nuket = now;
        break;
    default:
        ovl_select_skill(b);
        break;
    }
}

/* ovl_view_press - a left press in the view at world (x, y): the hit test and
 * the assignment, AT A FRAME BOUNDARY. While a frame is part-stepped
 * (lem_infr, more than LEM_STEP_MAX lemmings out) some lemmings have moved this
 * frame and some have not, and Lemmix only ever assigns BETWEEN frames - so the
 * press is queued and ovl_frame_begin() applies it before the next frame steps
 * a lemming (SPEC.md 116.7 point 3). The nuke takes the same road. */
static int lem_qhit, lem_qx, lem_qy, lem_qr, lem_qnuke;

static void ovl_view_press(int x, int y, int right)
{
    if (lem_infr) {
        lem_qhit = 1;
        lem_qx = x;
        lem_qy = y;
        lem_qr = right;
        return;
    }
    ovl_hittest(x, y, right, 1);
    ovl_assign();
}

static void ovl_nuke_press(void)
{
    if (lem_infr)
        lem_qnuke = 1;
    else
        ovl_nuke();
}

/* ovl_q_apply - the queue, at the frame boundary (ovl_frame_begin). */
static void ovl_q_apply(void)
{
    if (lem_qhit) {
        lem_qhit = 0;
        ovl_hittest(lem_qx, lem_qy, lem_qr, 1);
        ovl_assign();
    }
    if (lem_qnuke) {
        lem_qnuke = 0;
        ovl_nuke();
    }
}

/* ovl_mouse_input - the pointer, the buttons' edges and the edge scroll. `dt`
 * is the ticks this pass took. */
static void ovl_mouse_input(int dt)
{
    static struct os88_mouse m;
    int left, right, press, btn;

    os88_mouse(&m);
    ovl_mouse_map();
    /* THE BUTTONS ARE THE LEVEL NOW OR'D WITH WHAT THE LAST LONG DRAW SAW
     * (lemblit.inc lb_msample): a click that went down and up inside a 178 ms
     * shadow scroll is a press here and a release on the next pass */
    btn = m.btn | lem_r_mlatch();
    left = btn & 1;
    right = (btn >> 1) & 1;
    press = left && !(lem_mbtn & 1);
    if (!left && (lem_mbtn & 1)) {
        if (lem_rrbtn) {            /* ToolBar_MouseUp: BtnStopChangingRR */
            lem_rrbtn = 0;
            if (!lem_rrkey)
                lem_rrdir = 0;
        }
        lem_mmdrag = 0;
    }
    lem_mbtn = btn;
    if (press) {
        if (lem_my >= LEM_VIEWH_PX) {
            ovl_panel_press(lem_mx, lem_my - LEM_VIEWH_PX);
        } else if (!lem_paused) {
            /* Img_MouseDown: the assignment happens ON THE PRESS, at the game
             * cursor point, with the right button's state for the priority */
            ovl_view_press(lem_view + lem_mx - 3, lem_my + 2, right);
        }
    }
    if (lem_mmdrag && left) {
        /* ToolBar_MinimapClick: the view CENTRED on the cell, clamped */
        if (lem_mx >= LEM_MM_X && lem_mx < LEM_MM_X + LEM_MM_W) {
            lem_view = (lem_mx - LEM_MM_X) * LEM_MM_XSTEP - LEM_VIEW_W / 2;
            lem_view_move(0);
        }
        return;
    }
    /* THE EDGE COLUMN OF THE GAME IMAGE AND NOTHING ELSE (Lemmix
     * GameScreen.Player.pas:657-666, X <= 0 / X >= Img.Width - 1, and its
     * Form_MouseMove net at :1156-1168 wants Y inside Img): never from the
     * panel, whose Slower button is x 1..15, and never from a band a lemming
     * at the world's edge could not be aimed through */
    if (lem_my < LEM_VIEWH_PX) {
        if (lem_mx <= 0)
            lem_dx -= LEM_SCROLL_STEP * dt;
        else if (lem_mx >= LEM_VIEW_W - 1)
            lem_dx += LEM_SCROLL_STEP * dt;
    }
}

/* ovl_arrow_hold - a held arrow scrolls every tick until it reads UP, the
 * way F1/F2 are driven (int 16h has presses and no releases, so the kernel's
 * key map says when it let go). Ctrl held is Lemmix's precise step. */
static void ovl_arrow_hold(int dt)
{
    int step;

    if (!lem_arkey)
        return;
    if (!lem_arnew && !os88_key_down(lem_arkey)) {
        lem_arkey = 0;
        return;
    }
    lem_arnew = 0;
    /* THE PRECISE STEP IS THE CARD'S OWN SMALLEST PAN: one pixel on VGA,
     * the snap on the other two (4 on CGA, 8 on Hercules, lemblit.inc .snap).
     * A one-pixel step there left lem_view off the snap the glass is drawn
     * at, and the hit test, the margin and the pointer's lemming all read the
     * raw view - so a skill could go to the lemming up to 7 pixels beside the
     * one drawn under the pointer (wave 4 review). */
    step = LEM_SCROLL_STEP;
    if (os88_key_down(LEM_M_CTRL))
        step = lem_kind_id() == LEM_RKIND_VGA ? 1
             : lem_kind_id() == LEM_RKIND_CGA ? 4 : 8;
    lem_dx += lem_arsign * step * dt;
}

/* ovl_play_loop - the loop the header describes. */
static void ovl_play_loop(void)
{
    unsigned now, last;
    int dt, acc, k, r, drew;

    acc = 0;
    last = os88_ticks();
    lem_rrkey = 0;
    lem_rrbtn = 0;
    lem_mmdrag = 0;
    lem_f11up = 0;
    lem_f12up = 0;
    lem_arkey = 0;
    lem_arnew = 0;
    lem_mbtn = 0xFF;                /* a button already down is not a press */
    lem_exit = -1;
    for (;;) {
        os88_fsx_wait(OS88_FSXW_TICK);
        now = os88_ticks();
        dt = (int)(now - last);
        last = now;
        if (dt < 0 || dt > 4)
            dt = 4;

        /* --- input: EVERY key, then the mouse ---------------------------- */
        lem_dx = 0;
        while ((k = os88_fsx_key(0)) != 0) {
            r = ovl_key(k);
            if (r >= 0) {
                lem_exit = r;
                break;
            }
        }
        if (lem_exit >= 0)
            break;
        ovl_mouse_input(dt);
        ovl_arrow_hold(dt);
        if (lem_rrkey && !os88_key_down(lem_rrkey)) {
            lem_rrkey = 0;          /* Form_KeyUp: BtnStopChangingReleaseRate */
            if (!lem_rrbtn)
                lem_rrdir = 0;
        }
        if (!os88_key_down(LEM_M_F11))
            lem_f11up = 1;
        if (!os88_key_down(LEM_M_F12))
            lem_f12up = 1;
        if (lem_dx > LEM_SCROLL_MAX)
            lem_dx = LEM_SCROLL_MAX;
        if (lem_dx < -LEM_SCROLL_MAX)
            lem_dx = -LEM_SCROLL_MAX;
        if (lem_dx)
            lem_view_move(lem_dx);

        /* --- the world: one game frame, a chunk at a time ---------------- */
        drew = 0;
        acc += dt * (lem_super ? LEM_SUPER_CREDIT : 170);
                                    /* 17 frames in 18.2 ticks: 170 a tick */
        for (k = 0; k < (lem_super ? LEM_SUPER_MAX : 1); k++) {
            if (!lem_infr && acc >= 182) {
                acc -= 182;
                if (acc > (lem_super ? LEM_SUPER_MAX * 182 : 182))
                    acc = lem_super ? LEM_SUPER_MAX * 182 : 182;
                                    /* late is late: no burst to catch up */
                if (!ovl_frame_begin() && lem_finished)
                    break;
            }
            if (lem_infr && lem_frame_chunk(LEM_STEP_MAX)) {
                ovl_frame_end();
                drew = 1;
            }
            if (lem_infr || acc < 182)
                break;              /* a frame still being stepped, or none
                                     * due: the next pass */
        }
        if (lem_finished && !drew && !lem_infr) {
            lem_exit = LEM_X_DONE;
            break;
        }

        /* --- the glass ---------------------------------------------------- */
        ovl_tick_draw(drew, (lem_mbtn >> 1) & 1);
        ovl_snd_flush();
        if (lem_finished) {
            lem_exit = LEM_X_DONE;
            break;
        }
    }
    ovl_cursor_off();
}

/* ============================================================================
 * WAVE 5: THE SESSION - preview -> play -> postview -> next / retry / menu,
 * all inside ONE bracket (SPEC.md 116.4.3, docs/LEMMINGS-PORT-PLAN.md wave 5)
 *
 * Lemmix's screen order (GameScreen.Preview.pas, .Player.pas, .Postview.pas)
 * on the original's buttons: the preview's left button or Enter plays and Esc
 * goes to the menu; the postview's left button or Enter is the next level on
 * a success and the same one again on a failure, its right button and Esc are
 * the menu. The menu here is the launcher window, which IS this port's menu
 * screen (SPEC.md 116.8). 'f' is SPEC.md 11.2.1's door on every screen: out
 * of the preview it keeps the level, out of play it keeps the level, out of
 * the postview it shows the result in the window.
 *
 * THE MODE IS SET AROUND EACH SCREEN, and a refusal is a normal path rather
 * than an error (lem_r_mode in the plan; ovl_r_mode here, because the change
 * itself is the kernel's os88_fsx_mode and needs no assembly): no screens'
 * mode means no preview - the level composes behind the wave-2 loading panel
 * and plays - and the postview goes to the window, where it was until now.
 * ==========================================================================*/
static int lem_sbtn;                /* the buttons as the screens last saw them */

/* ovl_r_mode - set the screens' mode (scr) or the level's, and bind the
 * raster to it. 0 = os88_fsx_mode() refused, and nothing was changed. */
static int ovl_r_mode(int scr)
{
    static char fsi[OS88_FSI_SIZE];  /* SPEC.md 53.4's block, by byte offset */
    unsigned seg;
    int id, kind;

    id = scr ? ovl_scr_id() : lem_mode_id();
    if (id < 0)
        return 0;
    /* EVERY MODE SET HERE CLEARS ITS PAGE: the BIOS's on a VGA and a CGA, and
     * on a Hercules - which plays in the very mode its screens are drawn in -
     * FSXM_HERC's vid_setmode, whose 32 KB wipe runs on every call
     * (kernel/fsx.inc .herc), so the screen's pixels never stay around the
     * game's box. A SCREEN SKIPS THE SET WHEN ITS MODE IS ALREADY UP: one
     * that follows a screen (postview -> the next level's preview), and on a
     * Hercules the postview after PLAY too - the same FSXM_HERC, where the set
     * was a black frame between the last game frame and the result and a
     * 32 KB wipe of a surface the tile covers anyway. Nothing in FSXM_HERC is
     * ever written outside x 40..679 (the game's box and the screens' surface
     * share the letterbox, lemblit.inc LEM_HERCX and lemfont.inc LEM_PHERCX),
     * and the bracket's first set wiped those margins, so the tile and the
     * well's clear cover everything there is (lemfont.inc lem_p_clear). PLAY
     * always sets its mode: on a Hercules that wipe is what takes the tile
     * from around the game's box. The fsi block is static and still
     * describes this id, the last set being this id's. */
    lem_scr_fresh = !(scr && lem_live_id == id);
    if (lem_scr_fresh && os88_fsx_mode(id, fsi) != 0)
        return 0;
    lem_live_id = id;
    seg = (unsigned)(unsigned char)fsi[OS88_FSI_SEG]
        | (((unsigned)(unsigned char)fsi[OS88_FSI_SEG + 1]) << 8);
    kind = lem_kind_id();
    lem_onscr = scr;
    lem_r_setup(kind, seg, lem_cl_world, lem_cl_shadow);
    lem_r_tsetup(lem_cl_t);
    lem_m_setup(lem_cl_mask);
    lem_o_setup(lem_cl_rec);
    if (scr) {
        lem_p_setup(id == OS88_FSXM_VGA12 ? LEM_RKIND_VGA
                    : id == OS88_FSXM_HERC ? LEM_RKIND_HERC : LEM_RKIND_CGA,
                    seg);
        return 1;
    }
    lem_r_prep();
    if (kind == LEM_RKIND_VGA)
        lem_r_pal(lem_cl_bank, LEM_GR_STDPAL, LEM_GR_CUSTOM);
    return 1;
}

/* ovl_scr_input - one look at the keys and the buttons. LEM_SI_*, or -1. A
 * button is a PRESS only on its edge, and a screen starts with lem_sbtn at
 * 0xFF, so the click that ended the screen before cannot end this one. */
static int ovl_scr_input(int post)
{
    static struct os88_mouse m;
    int k, r;

    while ((k = os88_fsx_key(0)) != 0) {
        if (((k >> 8) & 0xFF) == LEM_K_ESC)
            return LEM_SI_MENU;
        k &= 0xFF;
        if (k == 'f' || k == 'F')
            return LEM_SI_DOOR;
        if (k == 13)
            return LEM_SI_GO;       /* Return only: Form_KeyDown on both
                                     * screens (Preview.pas :184-193,
                                     * Postview.pas) binds no Space */
    }
    os88_mouse(&m);
    r = -1;
    if ((m.btn & 1) && !(lem_sbtn & 1))
        r = LEM_SI_GO;
    else if (post && (m.btn & 2) && !(lem_sbtn & 2))
        r = LEM_SI_MENU;            /* "Press right mouse button for menu" -
                                     * the preview has no right button
                                     * (Form_MouseDown: mbLeft only) */
    lem_sbtn = m.btn;
    return r;
}

static int ovl_scr_wait(int post)
{
    int r;

    for (;;) {
        os88_fsx_wait(OS88_FSXW_TICK);
        r = ovl_scr_input(post);
        if (r >= 0)
            return r;
    }
}

/* ovl_view_start - the level's first view: THE LVL RECORD'S 0x0018 IS THE
 * VIEW'S LEFT EDGE, NOT ITS CENTRE (Lemmix GameScreen.Player.pas:857,
 * lemmings_3ds import_level.c:510-527), clamped and then rounded to a
 * multiple of eight, in that order. */
static void ovl_view_start(void)
{
    lem_view = 0;
    lem_view_move(lem_ent_u16(lem_playing, LEM_E_STARTX));
    lem_view = (lem_view + 4) & ~7;
}

/* ovl_pv_screen - the PREVIEW: the screen first, so the level's number, name
 * and figures are on the glass for the whole compose; the compose (its bar in
 * the picture's well, lemdraw.c ovl_load_bar); the picture, a row at a time
 * with the input asked every eighth row, so a player who has read enough
 * does not wait for the rest; then the button. LEM_SI_*. */
static int ovl_pv_screen(void)
{
    int k, h, r;

    if (!ovl_r_mode(1))
        return LEM_SI_NOMODE;
    /* THE KEYS GET THE BUTTONS' EDGE GUARD: what is in the BIOS buffer now
     * was typed at the screen before - a typematic Esc or Enter still queued
     * behind the one that ended it - and must not answer this one. Keys typed
     * during the compose below DO count (type-ahead). */
    while (os88_fsx_key(0) != 0)
        ;
    if (!lem_composed)
        lem_world_clear(0);         /* T and the mask in RAM only: the world's
                                     * VRAM rows are mode 0Dh's, which in mode
                                     * 12h are this screen's rows 35 on - and
                                     * the play mode's set clears them and
                                     * ovl_frame_first copies T over them */
    lem_sbtn = 0xFF;
    ovl_pv_draw(lem_playing);
    if (!lem_composed) {
        lem_load_n = 0;
        if (!lem_compose_level(lem_playing))
            return LEM_SI_FAIL;
        lem_composed = 1;
        ovl_scr_pal();              /* a special level's colours are the
                                     * picture's now (ovl_spec_compose) */
    }
    h = ovl_scr_cga() ? LEM_PV_PICHC : LEM_PV_PICH;
    for (k = 0; k < h; k++) {
        ovl_pv_pic(k);
        if ((k & 7) == 7 && (r = ovl_scr_input(0)) >= 0)
            return r;
    }
    return ovl_scr_wait(0);
}

/* ovl_po_screen - the POSTVIEW, and the result RECORDED while it is read: the
 * progress write is file I/O, which SPEC.md 53.1 allows mid-bracket, and a
 * write that fails is said by the window afterwards (lem_savfail), because
 * nothing can be said in here. LEM_SI_*. */
static int ovl_po_screen(void)
{
    int success;

    if (!ovl_r_mode(1))
        return LEM_SI_NOMODE;
    lem_sbtn = 0xFF;
    success = ovl_po_draw(lem_playing);
    lem_po_saved = 1;
    lem_po_succ = success;
    if (ovl_post_target(success, lem_playing)) {
        lem_progwrite = 0;
        ovl_progress_write(lem_pw_rat, lem_pw_lvl);
    }
    /* ...and the key that ENDED PLAY does not answer the postview: the play
     * loop stops draining at its first exit key, and a held Esc's typematic
     * repeats queue behind it while the result is drawn (several hundred ms
     * on the XT) - one of them read here was the menu before the result had
     * been seen. Scan 01 has no E0 prefix: this is not SPEC.md 9.7's
     * doubled-key class, and real hardware takes it too. */
    while (os88_fsx_key(0) != 0)
        ;
    return ovl_scr_wait(1);
}

/* ovl_post_move - the postview's target becomes "the game's" level, read
 * INSIDE the bracket where it crosses a section (SPEC.md 53.1: the file API
 * is legal here). -1 = moved; else LEM_X_LOAD, said by the window.
 *
 * BOTH OF THE POSTVIEW'S BUTTONS MOVE IT, because Lemmix moves it before the
 * screen is even drawn: GameScreen.Postview.pas GetScreenText runs `if
 * r.Success then App.CurrentLevelInfo := NextInfo`, so the MENU's next Play
 * opens the next level too - and after Mayhem 30, whose NextInfo is nil, the
 * preview's BuildScreen falls back to FirstLevel (GameScreen.Preview.pas
 * :89-90), which ovl_post_target() has already made Fun 1. A failure moves
 * nothing: its target is the level it was. */
static int ovl_post_move(void)
{
    if (lem_post_nrat != lem_rating) {
        lem_rating = lem_post_nrat; /* Fun 30 won is Tricky 1 (ovl_post_target) */
        if (!lem_rating_load(lem_rating)) {
            lem_load_err = LEM_LE_BANK;
            ovl_rat_name();
            return LEM_X_LOAD;
        }
    }
    lem_sel = lem_post_next;
    return -1;
}

/* ovl_next_level - the postview's left button: the next level, or the same
 * one again, read and claimed INSIDE the bracket. -1 = it is in hand and the
 * session goes on to its preview; otherwise the LEM_X_* the window says why
 * under. */
static int ovl_next_level(void)
{
    int r;

    if ((r = ovl_post_move()) >= 0)
        return r;
    if (!lem_play_ok())
        return LEM_X_GONE;          /* greyed on this disk: the window shows
                                     * its fact */
    if (!ovl_level_load(lem_sel))
        return LEM_X_LOAD;
    lem_playing = lem_sel;
    lem_composed = 0;
    lem_started = 0;
    lem_po_saved = 0;
    lem_po_succ = 0;
    ovl_view_start();
    return -1;
}

/* ovl_session - the loop the header describes. */
static void ovl_session(void)
{
    int r, play;

    play = lem_resume && lem_started;
    for (;;) {
        if (!play) {
            r = ovl_pv_screen();
            if (r == LEM_SI_MENU) {
                lem_exit = LEM_X_MENU;
                return;
            }
            if (r == LEM_SI_DOOR) {
                lem_exit = LEM_X_SUSPEND;
                return;
            }
            if (r == LEM_SI_FAIL) {
                lem_exit = LEM_X_FAIL;
                return;
            }
        }
        play = 0;
        if (!ovl_r_mode(0)) {
            lem_exit = LEM_X_MODE;  /* the same bit that greys the Mode row -
                                     * and its OWN fact, never NO_BANK's */
            return;
        }
        if (!lem_started) {
            /* THE PANEL AND A WORD BEFORE THE WAIT (SPEC.md 116.4.5): with no
             * screens' mode the level composes HERE, as it did before wave 5,
             * and after a preview the minimap's sampling is the wait */
            if (!lem_composed)
                lem_world_clear(1);
            ovl_frame_panel();
            if (!lem_composed) {
                if (!lem_compose_level(lem_playing)) {
                    lem_r_unprep();
                    lem_exit = LEM_X_FAIL;
                    return;
                }
                lem_composed = 1;
            }
        }
        lem_lvl_ok = 1;
        ovl_frame_first(lem_started);
        lem_started = 1;
        ovl_play_loop();
        lem_lvl_ok = 0;
        lem_r_sforget();
        lem_r_unprep();
        if (lem_exit != LEM_X_DONE)
            return;                 /* 'f': the level is kept */

        r = ovl_po_screen();
        if (r == LEM_SI_NOMODE || r == LEM_SI_DOOR)
            return;                 /* LEM_X_DONE: the postview in the window */
        if (r == LEM_SI_MENU) {
            /* the menu is on the NEXT level after a success (ovl_post_move) */
            lem_exit = LEM_X_MENU;
            if (lem_po_succ && (r = ovl_post_move()) >= 0)
                lem_exit = r;
            return;
        }
        r = ovl_next_level();
        if (r >= 0) {
            lem_exit = r;
            return;
        }
    }
}

static void lem_fsx_main(void)
{
    lem_exit = LEM_X_FAIL;
    lem_onscr = 0;                  /* the bracket opens on the desktop's
                                     * mode, whatever the last one left in */
    lem_live_id = -1;
    if (lem_playing >= 0)
        ovl_session();
    ovl_leave();                    /* the window's screen, chosen before the
                                     * exit's paint draws it */
}

/* lem_ask_launch - the keystroke's whole share of a launch: a flag.
 *
 * IT DRAWS NOTHING AND IT READS NOTHING: the disk I/O of a launch is ~136 KB
 * and tens of seconds on the target, and os88.h pins W_ONCLICK and W_ONKEY as
 * "the UI task, the gfx lock HELD ... must not take long". os88_onwake() is
 * the one lock-free callback (SPEC.md 74.1), and the launch is its errand. */
/* lem_continue - "continue" on whichever screen is up (see os88_onclick). */
static void lem_continue(void *win)
{
    if (lem_screen == LEM_SC_PREVIEW) {
        lem_ask_launch(win);
    } else if (lem_screen == LEM_SC_POST) {
        if (lem_post_nrat != lem_rating) {
            /* the NEXT SECTION: its file is read on the wake (no keystroke
             * touches a file, SPEC.md 116.6), and the wake opens its first
             * level's preview once it is in */
            lem_rating = lem_post_nrat;
            lem_ratpend = 1;
            lem_post_play = 1;
            os88_wm_wake(win);
            return;
        }
        lem_sel = lem_post_next;
        if (lem_sel < lem_top || lem_sel >= lem_top + lem_nlist)
            lem_top = lem_sel;
        lem_play(win);
    } else {
        lem_back(win);
    }
}

/* ovl_post_menu - Esc on the WINDOW's postview: the menu, ON THE NEXT LEVEL
 * after a success, as the bracket's own postview leaves it (ovl_post_move has
 * Lemmix's reference). A keystroke may not read a file (SPEC.md 116.6), so a
 * section's end is owed to the wake, which selects the next section's level
 * without playing it (lem_post_play = 2); until it lands the level rows are
 * held blank rather than lettered with the section being left. */
static void ovl_post_menu(void *win)
{
    lem_abandon();
    if (lem_po_succ) {
        if (lem_post_nrat != lem_rating) {
            lem_rating = lem_post_nrat;
            lem_ratpend = 1;
            lem_post_play = 2;
            lem_list_hold = 1;
            lem_back(win);
            lem_list_hold = 0;
            os88_wm_wake(win);
            return;
        }
        lem_sel = lem_post_next;    /* lem_layout() pages lem_top to it */
    }
    lem_back(win);
}

static void lem_ask_launch(void *win)
{
    /* THE SCREEN IS LEFT AS IT IS, AND SO IS THE SHADOW: both still describe
     * the glass, which is whatever the launch was asked from (wave 6, wave 5's
     * third review). This used to claim the LIST here - lem_screen = LIST and,
     * from a window preview or postview, lem_sh_ok = 0 - while the glass still
     * showed the paragraph; ui_task dispatches one key and then one event a
     * pass, so a typed-ahead Down could reach os88_onkey before the wake and
     * repaint the list from an invalid shadow (every cell lettered, blanks
     * included, ~1.1 s on the XT) and move lem_sel before ovl_launch read it:
     * a launch of a level the player did not choose. Now every arm of
     * ovl_launch chooses the screen it leaves on (its refusal arm switches to
     * the list with lem_back when lem_askscr is not the list), and while the
     * launch is pending os88_onkey and os88_onclick take nothing. */
    lem_askscr = lem_screen;
    lem_launchpend = 1;
    lem_kick(win);
}

/* ovl_post_target - where the postview's "continue" goes, and the progress
 * the result records. 1 = a progress write is owed.
 *
 * THE NEXT LEVEL, ACROSS A SECTION'S END: Lemmix links each section's last
 * level to the next section's first (Styles.Base.pas:964-968,
 * TSection.DoAddLevelInfo) and the postview's click goes to NextInfo, so Fun
 * 30 won is Tricky 1 - not Fun 30 again under a footer that says "next
 * level", which is what wave 3 shipped. Only Mayhem 30 has no next, and its
 * postview - the congratulation - says "continue" instead (ovl_postview), and
 * continues to FUN 1: Lemmix's postview sets CurrentLevelInfo to NextInfo,
 * which is nil there, and its preview's BuildScreen falls back to
 * LevelSystem.FirstLevel (GameScreen.Preview.pas:89-90). Waves 3-4 replayed
 * Mayhem 30 under the congratulation (wave 5). A failure retries. */
static int ovl_post_target(int success, int level)
{
    lem_post_nrat = lem_rating;
    lem_post_next = level;
    if (success && level + 1 < LEM_PERRAT) {
        lem_post_next = level + 1;
    } else if (success && lem_rating + 1 < LEM_RATINGS) {
        lem_post_nrat = lem_rating + 1;
        lem_post_next = 0;
    } else if (success) {
        lem_post_nrat = 0;          /* the FirstLevel fallback - which is not
                                     * a level REACHED, so nothing is written
                                     * for it */
        lem_post_next = 0;
        return 0;
    }
    if (!success || lem_post_next <= lem_prog[lem_post_nrat])
        return 0;
    /* the RESULT records progress (lem_play's own note) - for the rating the
     * next level is IN */
    lem_pw_rat = lem_post_nrat;
    lem_pw_lvl = lem_post_next;
    lem_progwrite = 1;
    return 1;
}

/* lem_abandon - a kept level, let go: its claims back to the heap. */
static void lem_abandon(void)
{
    lem_suspended = 0;
    lem_playing = -1;
    lem_free_all();
}

/* ovl_leave - THE SCREEN THE WINDOW COMES BACK ON, CHOSEN INSIDE THE BRACKET
 * (lem_fsx_main's last act) so that the fsx exit's one wm_paint_all draws it
 * and nothing is drawn after it (SPEC.md 116.4.6's "leave the bracket" row).
 * It DECIDES and draws nothing - no drawing slot is legal in here, and the
 * screens' builders only compose the paragraph. Nothing has re-laid the window
 * out since the list was drawn in it, so the paragraph is wrapped at the
 * window's own width, once.
 *
 * THE SESSION MAY HAVE MOVED ON (wave 5): the postview's left button reads the
 * next level inside the bracket, so the level launched is stale after it and
 * lem_playing is the level the bracket left with. */
static void ovl_leave(void)
{
    int level, success;

    level = lem_playing;
    if (lem_exit == LEM_X_SUSPEND) {
        lem_suspended = 1;
        ovl_preview(level);
        lem_wrap();
        lem_screen = LEM_SC_PREVIEW;
    } else if (lem_exit == LEM_X_DONE) {
        /* the postview IN THE WINDOW: 'f' took it out of the bracket, or its
         * mode was refused there. Where the bracket's postview ran it has
         * already recorded the result (lem_po_saved); where it did not, the
         * record is owed to the wake, which ovl_launch() kicks on return */
        success = ovl_postview(lem_rating, level, LEM_PO_WIN);
        lem_po_succ = success;
        lem_wrap();
        lem_screen = LEM_SC_POST;
        ovl_post_target(success, level);
        if (lem_po_saved)
            lem_progwrite = 0;
    } else if (lem_exit == LEM_X_MODE && lem_resume) {
        /* the mode was refused INSIDE the bracket, on a RESUME: the kept
         * level stays kept, as the NO_FSX arm keeps it, and the toast says so
         * (SPEC.md 47 - what happened, not a guess) */
        lem_suspended = 1;
        ovl_preview(level);
        lem_wrap();
        lem_screen = LEM_SC_PREVIEW;
        lem_lv_said = LEMS_NO_MODE_KEPT;
    } else if (lem_exit == LEM_X_MODE) {
        /* ...on a FIRST launch: the level had been read and claimed, and it
         * is let go - said in the window, as its own fact, with the ending
         * that is TRUE here ("nothing was loaded" is not) */
        lem_abandon();
        lem_screen = LEM_SC_LIST;
        lem_mode_end = LEMS_MODE_GONE;
        lem_set_fact(LEMS_NONE, LEMS_NO_MODE, -1);
    } else if (lem_exit == LEM_X_GONE) {
        /* the postview's next level is greyed on this disk: its fact, which
         * is what a click on its row would have said */
        lem_abandon();
        lem_screen = LEM_SC_LIST;
        lem_set_fact(LEMS_NONE, lem_ent_u8(lem_sel, LEM_E_MISSING), lem_sel);
    } else {
        /* LEM_X_MENU - Esc, or the postview's right button: the original's
         * menu, which is the level list, the level and its claims let go (and
         * on a success the list is ALREADY on the next level, ovl_post_move)
         * - or the compose refused, or the next level's read did, which
         * ovl_said_load() says on the way out */
        lem_abandon();
        lem_screen = LEM_SC_LIST;
    }
}

/* ovl_launch - everything that can REFUSE, and then the bracket.
 *
 * CALLED FROM os88_onwake() AND FROM NOWHERE ELSE, which is what lets the file
 * half run with the lock FREE. A kept level ('f' out of the bracket) skips the
 * whole file half: its claims, its record turned object map and its lemmings
 * are all where they were.
 *
 * WHAT THE WINDOW SHOWS AFTERWARDS is the bracket's own exit: the postview's
 * text for a finished level (wave 5 moves it into the bracket's second foreign
 * mode), the level's preview screen for a kept one - where a click, or 'f',
 * brings it back - and the list for a refusal, with the refusal toasted. */
static void ovl_launch(void *win)
{
    int ok, level;

    level = lem_suspended ? lem_playing : lem_sel;
    if (!lem_suspended && !lem_play_ok())
        ok = LEM_LR_GONE;
    else if (!ovl_raster_ok())
        ok = LEM_LR_MODE;
    else if (lem_suspended)
        ok = LEM_LR_GO;
    else
        ok = ovl_level_load(level) ? LEM_LR_GO : LEM_LR_FILE;

    os88_gfx_lock();
    if (ok == LEM_LR_GO) {
        lem_resume = lem_suspended;
        lem_suspended = 0;
        lem_playing = level;
        if (!lem_resume) {
            /* A NEW LEVEL: nothing of it is composed, nothing has run, and
             * no result of it is recorded yet (wave 5's session flags) */
            lem_composed = 0;
            lem_started = 0;
            lem_po_saved = 0;
            lem_po_succ = 0;
            ovl_view_start();
        }
        /* NO FULLSCREEN LATCH AROUND THE BRACKET (wave 6, wave 5's third
         * review). SPEC.md 11.2's latch is a WINDOW the size of the screen
         * (os88.h: "NOT the exclusive bracket"), which Missile Command needs
         * because it plays in the DESKTOP's mode inside the window's own
         * geometry; this program sets its own modes inside the bracket, so the
         * window's size is never read in there. Stacked, it cost three kernel
         * paints a Play: wm_raise white-filling the whole screen before the
         * mode set (a full-screen flash on the way in), the fsx exit's
         * wm_paint_all with the window still fullscreen (a second white flash,
         * on the way out), and os88_fullscreen(win, 0)'s whole-desktop repaint
         * - the one the glass kept. Without it the fsx exit's single
         * wm_paint_all (SPEC.md 53.6 step 4) is the only paint, and it draws
         * the screen ovl_leave() chose INSIDE the bracket, before the entry
         * returned (SPEC.md 116.4.6's "leave the bracket" row). APPLE2's
         * a2scr.c enters the bracket the same way.
         *
         * THE EXIT IS RESET BEFORE THE CALL AND THE CALL'S OWN ANSWER IS READ.
         * os88_fsx_run() answers -1 WITHOUT running the entry - already in a
         * bracket, the file dialog up, not task 0, or the ownership fence
         * (apps/cc/os88thunk.asm's contract) - and lem_exit then still held
         * the LAST session's verdict: a finished level's DONE showed the
         * postview of one never played and recorded it as reached in
         * LEMMINGS.SAV. APPLE2's a2scr.c is the shape. */
        lem_exit = LEM_X_FAIL;
        lem_lv_said = LEMS_NONE;
        if (os88_fsx_run(lem_fsx_main, win, 0) < 0) {
            if (lem_resume) {
                lem_suspended = 1;  /* a kept level stays kept */
                ovl_preview(level);
                lem_wrap();
                lem_screen = LEM_SC_PREVIEW;
            } else {
                lem_abandon();
                lem_screen = LEM_SC_LIST;
            }
            /* nothing was painted - the entry never ran - so the glass is
             * still the screen the launch was asked from: drawn again only
             * when the answer is another screen */
            if (lem_screen != lem_askscr && lem_blank_glass(win))
                lem_repaint(win, 1, 0);
            os88_gfx_unlock();
            os88_toast(lem_str(LEMS_NO_FSX), 0);
            return;
        }
        /* the fsx exit's paint has drawn ovl_leave()'s screen: nothing is
         * drawn here, and what is owed to the wake is asked for (a kick is
         * not legal inside the bracket, where ovl_leave() decided it) */
        os88_gfx_unlock();
        lem_kick(win);
        ovl_said_save();
        if (lem_lv_said != LEMS_NONE)
            os88_toast(lem_str(lem_lv_said), 0);
        else if (lem_exit == LEM_X_FAIL || lem_exit == LEM_X_LOAD)
            ovl_said_load();
        return;
    }
    if (ok == LEM_LR_MODE) {
        /* ITS OWN FACT AND NOT THE MODE ROW'S (SPEC.md 47): the action here is
         * os88_fsx_mode() answering no on the mode this adapter would have
         * played in - before anything was read, which is the one place
         * "nothing was loaded" is true. */
        lem_mode_end = LEMS_MODE_NONE;
        lem_show_fact(win, LEMS_NONE, LEMS_NO_MODE, -1);
        os88_gfx_unlock();
        return;
    }
    if (lem_suspended)
        lem_abandon();
    if (lem_askscr != LEM_SC_LIST)
        lem_back(win);              /* asked from a window preview or
                                     * postview: the list is not drawn yet */
    /* ...asked FROM the list, the list is the glass: only the toast below
     * changes, and a blank-and-repaint here was ~674 ms and a white flash on
     * every refused Play - "Need 329KB, 210KB free" being the ordinary
     * answer on a small machine */
    os88_gfx_unlock();
    if (ok == LEM_LR_FILE)
        ovl_said_load();
}

/* ovl_said_load - why a level did not load, as a toast (24 characters,
 * TOAST_MAX): "Need 329KB, 210KB free" for a claim, "LEMGR0_1.LEM missing"
 * for a band file, "LEMMAIN.LEM is too old" for a bank of another version,
 * by lem_load_err - the three refusals are different sentences (SPEC.md
 * 47). */
static void ovl_said_load(void)
{
    ovl_arg_reset();
    if (lem_load_err == LEM_LE_MEM) {
        ovl_arg_num((unsigned)ovl_need_kb());
        ovl_arg_num(os88_mem_largest_kb());
        ovl_sfmt(lem_line, sizeof(lem_line), lem_str(LEMS_NO_MEM));
    } else {
        ovl_arg_add(lem_f_scratch);   /* the name the reader left */
        ovl_sfmt(lem_line, sizeof(lem_line),
                 lem_str(lem_load_err == LEM_LE_OLD ? LEMS_OLD_BANK
                                                    : LEMS_NO_BANK));
    }
    os88_toast(lem_line, 0);
}

/* ovl_said_save - a progress write the BRACKET'S postview could not make,
 * said now that something can be said (ovl_progress_write raised the flag
 * and greyed the row; every exit above repaints the list or a screen in
 * full, which draws the greyed row). */
static void ovl_said_save(void)
{
    if (!lem_savfail)
        return;
    lem_savfail = 0;
    os88_toast(lem_str(LEMS_SAV_FAIL), 0);
}

/* --- the entry point (SPEC.md 20.2) ----------------------------------------
 * The gfx lock is NOT held here and the window does not exist yet. The FILE
 * slots ARE legal, so this is where the band files are read: the manifest, the
 * string band and the first rating. The overlay module is already resident:
 * it is part 0 of LEMMINGS.O88, read by op_load and bound by cc_ovinit before
 * this function runs (SPEC.md 73.14.2), so an ovl_ call is legal from here on
 * and nothing about it is owed to a wake.
 *
 * A REFUSAL HERE IS NOT FATAL. The window comes up and says what is missing,
 * which is SPEC.md 47's ordinary path and a great deal more use than a launch
 * that silently aborts. */
void *os88_main(void)
{
    static struct os88_video vid;
    int i;

    lem_rating = 0;
    lem_sel = 0;
    lem_top = 0;
    lem_screen = LEM_SC_LIST;
    for (i = 0; i < LEM_RATINGS; i++)
        lem_prog[i] = -1;

    lem_ok = lem_data_load();

    os88_video(&vid);
    lem_vidkind = vid.kind;
    lem_snd = (os88_snd_caps() != 0);

    lem_name = lem_ok ? lem_str(LEMS_PROD) : lem_title;
    if (lem_name[0] == 0)
        lem_name = lem_title;       /* a band that does not carry the row */
    lem_menus.name = lem_name;

    lem_win = os88_wm_create(LEM_WIN_X, LEM_WIN_Y, LEM_WIN_W, LEM_WIN_H,
                             lem_name);
    if (lem_win == 0)
        return 0;

    os88_wm_minsize(lem_win, LEM_MIN_W, LEM_MIN_H);
    os88_wm_sizable(lem_win, 1);        /* ...then lay out from the LIVE
                                         * geometry, never from these numbers */
    os88_wm_snap(lem_win, 1);           /* content origin on a multiple of 8, so
                                         * every font_run takes SPEC.md 6.1's
                                         * single-store path on 1bpp */
    os88_wm_onresize(lem_win);
    os88_wm_onwake(lem_win);
    os88_wm_ontimer(lem_win);           /* installed, armed only by a selection
                                         * move (os88_ontimer above). It may be
                                         * REFUSED on a kern_small machine,
                                         * which is why lem_pv_defer() tests
                                         * os88_wm_timer()'s answer and has a
                                         * second path (SPEC.md 13.8.2) */
    os88_menu_set(lem_win, &lem_menus);
    os88_about_set(lem_win);

    lem_mode_ok = lem_probe_mode(lem_win);

    lem_sh_ok = 0;
    os88_wm_wake(lem_win);              /* ask for the first wake: the progress
                                         * file is read there, off the gfx lock
                                         * (the module is part 0 and already
                                         * resident - SPEC.md 73.14.2) */
    return lem_win;
}
