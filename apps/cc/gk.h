/* gk.h - the os8088 GAME KIT for C packages (SPEC.md 132): 3D widgets,
 * data-driven scenes and a music/effects player, over the band drawing core
 * (apps/breadxt/draw.inc, SPEC.md 130.6.2).
 *
 * The drawing half (gk_*) is assembly in apps/cc/gk.inc; the sound half
 * (gkm_*) is assembly in apps/cc/gkmus.inc. A package includes the core,
 * then the kit, from its shim:
 *
 *     %define BX_CORE_ONLY
 *     %include "breadxt/draw.inc"
 *     %include "cc/gk.inc"          ; drawing kit, needs the core
 *     %include "cc/gkmus.inc"       ; music and effects, needs nothing else
 *
 * and #includes this header from C. Everything here is RESIDENT code in the
 * package's own segment, so its size is the price of using it: SPEC.md 132.9
 * carries the measured bytes per routine.
 *
 * DRAWING RULES (the band core's): every gk_ call is valid only while a
 * strip is being composed (between bx_new and bx_out); it draws into the
 * band and is clipped to the strip, so a scene or a panel is simply drawn
 * whole for every strip it touches. Coordinates are screen pixels; w, h
 * are sizes; colours are EGA indices 0..15 that the core turns into 2x2
 * patterns on a 1bpp display. Fonts are art-part font IDs (a negative ID is
 * the kernel's 8x8). Nothing here writes a pixel to the glass.
 *
 * THE 3D LOOK (SPEC.md 132.3): light comes from the top left. A RAISED
 * surface has a light edge top/left and a shadow edge bottom/right inside a
 * 1 px black outline; SUNKEN is the reverse; a panel, a button and a key cap
 * may cast a dithered drop shadow down and to the right. Every colour the
 * kit uses comes from the ROLE TABLE, so a package re-themes by passing
 * its own table, and a 1bpp display gets the mono table.
 */
#ifndef OS88_GK_H
#define OS88_GK_H

/* ---- roles ------------------------------------------------------------ */
#define GK_R_OUTLINE   0   /* the 1 px outline around raised things (black) */
#define GK_R_FACE      1   /* panel / button face (light grey 7) */
#define GK_R_LIGHT     2   /* raised top-left edge (white 15) */
#define GK_R_SHADE     3   /* raised bottom-right edge (dark grey 8) */
#define GK_R_FIELD     4   /* inset fields, cells (white 15) */
#define GK_R_TEXT      5   /* text on face and field (black 0) */
#define GK_R_TITLE     6   /* title bar ground (blue 1, navy) */
#define GK_R_TITLELT   7   /* title bar top highlight line (light blue 9) */
#define GK_R_TITLETX   8   /* title text (white 15) */
#define GK_R_FOCUS     9   /* focused button face (blue 1) */
#define GK_R_FOCUSTX  10   /* focused button text (white 15) */
#define GK_R_HILITE   11   /* banners, the active row (yellow 14) */
#define GK_R_HILITETX 12   /* text on a highlight (black 0) */
#define GK_R_DISABLED 13   /* disabled text (dark grey 8) */
#define GK_R_DROP     14   /* drop shadow ink, dithered over what is below (black 0) */
#define GK_R_KEY      15   /* key-cap top (white 15) */
#define GK_ROLES      16

/* Set the role table (16 bytes, GK_R_* order; the table is read in place,
 * not copied, so keep it static). 0 restores the kit's own: the colour
 * table on a colour display, the mono one when bpp < 4. gk_init picks the
 * default and must be called once after bx_setup. */
void gk_init(int bpp);
void gk_theme(const unsigned char *roles);
int  gk_role(int role);                       /* the colour a role is now */

/* ---- surfaces ----------------------------------------------------------- */
#define GK_FLAT     0   /* outline only */
#define GK_RAISED   1   /* 1 px light/shade inside the outline */
#define GK_RAISED2  2   /* 2 px: a thick raised edge (buttons, key caps) */
#define GK_SUNKEN   3   /* 1 px shade/light, no outline (inset fields) */
#define GK_SUNKEN2  4   /* 2 px sunken, outline inside (wells, the board's centre) */
#define GK_ETCHED   5   /* a 2 px groove (group boxes, dividers) */
#define GK_ROUND    0x10 /* OR in: cut the four corner pixels (a softened edge) */
#define GK_SHADOW   0x20 /* OR in: cast GK_DROPW px of dithered shadow right and down */
#define GK_DROPW    3

/* A bevelled rect: edge style | GK_ROUND | GK_SHADOW, then the face filled
 * with `fill` (-1 = leave the inside alone). The shadow is drawn OUTSIDE
 * (x, y, w, h), so leave GK_DROPW px of room. */
void gk_box(int x, int y, int w, int h, int style, int fill);
/* a horizontal or vertical groove / ridge line (dividers) */
void gk_hline(int x, int y, int w, int raised);
void gk_vline(int x, int y, int h, int raised);
/* a dithered drop shadow only: d px right/below the rect */
void gk_shadow(int x, int y, int w, int h, int d);

/* ---- text --------------------------------------------------------------- */
#define GK_LEFT    0
#define GK_CENTRE  1
#define GK_RIGHT   2
#define GK_EMBOSS  0x10 /* OR in: a 1 px dark copy down-right first (titles on dark grounds) */
#define GK_FIT     0x20 /* OR in: cut the text to w (no ellipsis), never wider */
/* Text in a w-wide slot, aligned; answers the drawn width. colour is an
 * EGA colour (not a role). */
int  gk_text(int x, int y, int w, const char *s, int colour, int font, int flags);
/* Text vertically centred in a w x h box as well */
int  gk_textbox(int x, int y, int w, int h, const char *s, int colour, int font, int flags);

/* ---- composite widgets -------------------------------------------------- */
/* A 3D panel: outline, raised edge, face, a title bar `bar` px tall (0 = no
 * bar) with the title centred in GK_R_TITLETX, embossed; style may add
 * GK_SHADOW. Answers the y of the first row under the title bar. */
int  gk_panel(int x, int y, int w, int h, int bar, const char *title, int font, int style);
/* A title bar alone (a section header inside a panel): ground GK_R_TITLE,
 * a highlight line on top, text centred (flags GK_LEFT to left-align with
 * a 6 px indent). */
void gk_titlebar(int x, int y, int w, int h, const char *s, int font, int flags);

#define GK_B_NORMAL   0
#define GK_B_FOCUS    1   /* navy face, white text */
#define GK_B_DEFAULT  2   /* an extra outline ring (Enter) */
#define GK_B_DISABLED 3   /* greyed label; on 1bpp a screen-door over it */
#define GK_B_PRESSED  4   /* sunken, label down-right 1 px */
#define GK_B_HILITE   5   /* yellow face (the selected tab, "your turn") */
/* A push button: rounded, GK_RAISED2, a shadow, the label centred and cut
 * to fit. */
void gk_button(int x, int y, int w, int h, const char *label, int font, int state);
/* A keyboard key cap: GK_R_KEY top, a thick shade on the right and bottom
 * (the cap's sides), outline, rounded; label centred on the top face. */
void gk_keycap(int x, int y, int w, int h, const char *label, int font);
/* A tab: like a button with no bottom edge; active = GK_B_HILITE face and
 * 2 px taller. */
void gk_tab(int x, int y, int w, int h, const char *label, int font, int active);
/* A check box (size x size, sunken white well; on = the tick: an art ID
 * `tick` when present, else a drawn tick) and a radio button (a drawn
 * circle, on = a filled dot). */
void gk_check(int x, int y, int size, int on, int tick);
void gk_radio(int x, int y, int size, int on);
/* A drop-down field: raised face, the label left, a down-pointing solid
 * triangle in a right-hand zone. */
void gk_drop(int x, int y, int w, int h, const char *label, int font);
/* A rounded pill/banner (yellow by default: fill = -1 means GK_R_HILITE)
 * with the text centred. */
void gk_banner(int x, int y, int w, int h, const char *s, int font, int fill);
/* A filled triangle arrow inside a w x h box: dir 0 up, 1 right, 2 down,
 * 3 left. */
void gk_arrow(int x, int y, int w, int h, int dir, int colour);
/* A sprite with the kit's dithered shadow under it (d px down-right), then
 * the sprite; answers the sprite's width (0 absent). */
int  gk_sprshadow(int x, int y, int id, int d);

/* ---- scenes (SPEC.md 132.6) -----------------------------------------------
 * A SCENE is a list of the calls above stored as DATA in an art record,
 * compiled by tools/os88gk.py from a text source, so a screen's static
 * decoration costs record bytes in an art part instead of code and string
 * literals in the package's segment. Coordinates in a scene are relative to
 * (x, y). Dynamic values come from two slot arrays the caller passes:
 * sv[n] strings ($n in the source) and iv[n] integers (%n), so one scene
 * draws "Price $0" or a button whose state is %2. A scene answers 1 drawn,
 * 0 absent (no art part, or no record: the caller draws its own fallback or
 * nothing). Either array may be 0 when the scene names no slot of it. */
int  gk_scene(int id, int x, int y, const char *const *sv, const int *iv);
/* The rect of the op tagged #tag in a scene, relative to the scene origin,
 * into r[0..3] = x, y, w, h: buttons' hit targets come from the same data
 * that draws them. Answers 1 found, 0 not. */
int  gk_scene_rect(int id, int tag, int *r);
/* A scene's own declared size (w | h << 8 is too small: two words). */
int  gk_scene_size(int id, int *wh);

/* ---- music and effects (SPEC.md 132.8): apps/cc/gkmus.inc ---------------
 * A SONG BANK and an EFFECTS BANK are art records compiled by
 * tools/os88gkmus.py. Routing: three FM voices through SOUND.DRV
 * (OSAPI_SND_FM) when a sound driver is loaded, else the PC speaker
 * (OSAPI_SND_TONE) playing the melody voice only. Call gkm_tick() from the
 * package's timer callback (once per 18.2 Hz OS tick); it never draws and
 * never blocks. */
#define GKM_NONE   0
#define GKM_FM     1
#define GKM_SPK    2
int  gkm_init(int music_id, int sfx_id);     /* answers GKM_FM / GKM_SPK / GKM_NONE */
void gkm_tick(void);
void gkm_play(int song, int loop);           /* song index in the bank; loop 1 = repeat */
void gkm_stop(void);                          /* silence the music, release FM voices */
int  gkm_playing(void);                       /* the song index, -1 none */
void gkm_sfx(int effect);                     /* an effect over (or ducking) the music */
void gkm_volume(int music_on, int sfx_on);    /* the Options toggles */
void gkm_done(void);                          /* at close: silence, release every voice */

#endif
