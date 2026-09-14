#ifndef SPEEDYBASIC_SBASIC_H
#define SPEEDYBASIC_SBASIC_H

/* The resident seam between the Turbo Basic core and the os8088 shell.
 * SmallerC has near pointers only, so this table and every function it names
 * live in the primary package segment.  The core keeps the pointer; the shell
 * therefore gives it a static table from os88_main(). */

#define SB_STATE_IDLE     0
#define SB_STATE_READY    1
#define SB_STATE_RUNNING  2
#define SB_STATE_WAITING  3
#define SB_STATE_DONE     4
#define SB_STATE_ERROR    5
#define SB_STATE_STOPPED  6

#define SB_MODE_TEXT      0
#define SB_MODE_GRAPHICS  1

#define SB_SLICE_OPS     256
#define SB_SOURCE_KB      32
#define SB_SOURCE_MAX     32767u
#define SB_ERROR_MAX      80

struct sb_host {
    /* Graphics modes retain a 320x200 colour surface.  WIDTH/HEIGHT are the
     * Turbo Basic logical coordinates and the native host maps them to it. */
    /* MODE is the BASIC SCREEN number (0, 1, 7, 9 or 13). */
    void (*mode)(int mode, int width, int height);
    void (*clear)(int color);
    void (*text_put)(int row, int col, int ch, int fg, int bg);
    void (*text_cursor)(int row, int col);
    void (*text_scroll)(void);
    void (*pixel)(int x, int y, int color);
    void (*line)(int x1, int y1, int x2, int y2, int color);
    int  (*point)(int x, int y);
    void (*circle)(int x, int y, int radius, int color);
    void (*paint)(int x, int y, int color, int border);
    unsigned (*gfx_get)(int x1, int y1, int x2, int y2,
                        unsigned segment, unsigned offset);
    void (*gfx_put)(int x, int y, unsigned segment, unsigned offset, int op);
    int  (*video_peek)(unsigned segment, unsigned offset);
    void (*video_poke)(unsigned segment, unsigned offset, int value);
    void (*port_out)(unsigned port, int value);
    void (*palette)(int index, int r6, int g6, int b6);
    void (*sound)(int hz, int ticks);
    unsigned (*ticks)(void);
    void (*changed)(void);
};

#define SB_PUT_PSET    0
#define SB_PUT_PRESET  1
#define SB_PUT_XOR     2
#define SB_PUT_OR      3
#define SB_PUT_AND     4

/* `sb_init` and both load calls return 0 on success, -1 on error.  A load
 * parses/compiles SOURCE synchronously and leaves READY or ERROR.
 * `sb_run_slice` starts READY programs and executes at most BUDGET language
 * operations.  It returns an SB_STATE_* and never draws after returning.
 * WAITING means input/time is outstanding; a later key or timer wake resumes.
 * The shell may call `sb_key` for every press, including while stopped, so the
 * core owns Turbo Basic's INKEY$/INPUT translation rather than the UI. */
int  sb_init(struct sb_host *host);
int  sb_load(const char *source, unsigned length);
int  sb_load_seg(unsigned segment, unsigned length);
int  sb_run_slice(int budget);
void sb_key(int ascii, int scan);
void sb_stop(void);
void sb_reset(void);

/* Read-only session information for the status row and editor highlight. */
int         sb_state(void);
int         sb_line(void);
const char *sb_error(void);

/* Far runtime store implemented by sbmem.inc.  Numeric cells reserve a
 * signed 32-bit pair even while the current evaluator consumes the low word.
 * String cells reuse that pair as pool offset/length. */
#define SBM_CELL_CAP 16384u
#define SBM_STR_CAP  32767u
#define SBM_KIND_BYTES 2048u
int      sbm_init(void);
void     sbm_reset(void);
int      sbm_make(unsigned count);
int      sbm_get(unsigned index);
void     sbm_set(unsigned index, int value);
unsigned sbm_get_lo(unsigned index);
int      sbm_get_hi(unsigned index);
/* Returns -1 without changing the cell if lazy high storage cannot be grown. */
int      sbm_set_pair(unsigned index, unsigned lo, int hi);
int      sbm_kind_get(unsigned index);
void     sbm_kind_set(unsigned index, int kind);
/* Graphics GET/PUT receives this segment and byte offset `cell_index << 1`. */
unsigned sbm_lo_segment(void);
int      sbm_str_set(unsigned index, const char *source, unsigned length);
unsigned sbm_str_len(unsigned index);
int      sbm_str_copy(unsigned index, char *dest, unsigned capacity);
int      sbm_str_char(unsigned index, unsigned position);

#endif
