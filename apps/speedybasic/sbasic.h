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

#define SB_SLICE_OPS      2048
#define SB_SOURCE_KB      32
#define SB_SOURCE_MAX     32767u
#define SB_ERROR_MAX      80

struct sb_host {
    /* Graphics modes retain a 320x200 colour surface.  WIDTH/HEIGHT are the
     * Turbo Basic logical coordinates and the native host maps them to it. */
    void (*mode)(int mode, int width, int height);
    void (*clear)(int color);
    void (*text_put)(int row, int col, int ch, int fg, int bg);
    void (*text_cursor)(int row, int col);
    void (*pixel)(int x, int y, int color);
    void (*line)(int x1, int y1, int x2, int y2, int color);
    void (*sound)(int hz, int ticks);
    unsigned (*ticks)(void);
    void (*changed)(void);
};

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

#endif
