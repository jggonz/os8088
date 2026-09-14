/* Retained Turbo Basic text and graphics surfaces.  This file is included by
 * sbasic.c; it is not a separate translation unit (SPEC 73.1). */

#define SBS_COLS       80
#define SBS_ROWS       25
#define SBS_GW         320
#define SBS_GH         200
#define SBS_GSTRIDE    160
#define SBS_GKB        32
#define SBS_MAP_OFF  32000u
#define SBS_PLANES      4
#define SBS_PLANE_KB   32

static unsigned char sbs_ch[SBS_COLS * SBS_ROWS];
static unsigned char sbs_at[SBS_COLS * SBS_ROWS];
static unsigned sbs_gseg;
static int sbs_gkb;
static unsigned sbs_pseg[SBS_PLANES];
static int sbs_plane_kb;
static unsigned char sbs_dac_rgb[3];
static unsigned char sbs_ega_map[16];
static unsigned char sbs_band[800];
static int sbs_screen;
static int sbs_dac_index, sbs_dac_part;
static int sbs_mode;
static int sbs_lw, sbs_lh;
static int sbs_dirty;
static unsigned sbs_vpoke_count;
static unsigned sbs_gget_count, sbs_gput_count;
static unsigned sbs_arg[6];

/* These three routines are in speedybasic.asm because compiled C cannot name
 * the ES segment of a heap claim. */
void sb_fb_clear(unsigned seg, int color);
void sb_fb_pixel(unsigned seg, int x, int y, int color);
void sb_fb_blit(unsigned seg, int x, int y);
void sb_plane_clear(unsigned seg, int set, int kb);
void sb_byte_clear(unsigned seg, int value);
void sb_plane_pixel(unsigned seg, unsigned off, int bit, int set);
int sb_plane_point(unsigned seg, unsigned off, int bit);
void sb_byte_set(unsigned seg, unsigned off, int value);
int sb_byte_get(unsigned seg, unsigned off);
int sb_map_get(unsigned seg, int index);
void sb_map_set(unsigned seg, int index, int value);
void sb_fb_map13(unsigned dst, unsigned lo, unsigned hi);
void sb_fb_blit9(unsigned scratch, unsigned p0, unsigned p1, unsigned p2,
                 unsigned p3, int x, int y);

static const unsigned char sbs_dos_rgb[48] = {
     0, 0, 0,  0, 0,42,  0,42, 0,  0,42,42,
    42, 0, 0, 42, 0,42, 42,21, 0, 42,42,42,
    21,21,21, 21,21,63, 21,63,21, 21,63,63,
    63,21,21, 63,21,63, 63,63,21, 63,63,63
};

static int ovl_sbs_abs(int n)
{
    return n < 0 ? -n : n;
}

static unsigned sbs_rowoff(int y)
{
    if (sbs_lw == 640)
        return (unsigned)((y << 6) + (y << 4));
    return (unsigned)((y << 5) + (y << 3));
}

static int ovl_sbs_nearest(int r, int g, int b)
{
    int i, d, best, bestd;
    best = 0; bestd = 32767;
    for (i = 0; i < 16; i++) {
        d = ovl_sbs_abs(r - sbs_dos_rgb[i * 3]);
        d += ovl_sbs_abs(g - sbs_dos_rgb[i * 3 + 1]);
        d += ovl_sbs_abs(b - sbs_dos_rgb[i * 3 + 2]);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

static int sbs_scale_x(int x)
{
    if (sbs_lw <= 0 || x < 0 || x >= sbs_lw)
        return -1;
    if (sbs_lw == 640)
        return x >> 1;
    if (sbs_lw <= SBS_GW)
        return x;
    return x / ((sbs_lw + SBS_GW - 1) / SBS_GW);
}

static int sbs_scale_y(int y)
{
    if (sbs_lh <= 0 || y < 0 || y >= sbs_lh)
        return -1;
    if (sbs_lh == 350)
        return (y * 4) / 7;
    if (sbs_lh == 400)
        return y >> 1;
    if (sbs_lh == 480)
        return (y * 5) / 12;
    if (sbs_lh <= SBS_GH)
        return y;
    return y / ((sbs_lh + SBS_GH - 1) / SBS_GH);
}

static int ovl_sbs_init(void)
{
    int i;

    sbs_gseg = 0;
    sbs_gkb = 0;
    for (i = 0; i < SBS_PLANES; i++) sbs_pseg[i] = 0;
    sbs_plane_kb = 0;
    sbs_mode = SB_MODE_TEXT;
    sbs_lw = SBS_GW;
    sbs_lh = SBS_GH;
    for (i = 0; i < SBS_COLS * SBS_ROWS; i++) {
        sbs_ch[i] = ' ';
        sbs_at[i] = 7;
    }
    sbs_screen = 0;
    sbs_dac_index = sbs_dac_part = 0;
    for (i = 0; i < 16; i++) sbs_ega_map[i] = (unsigned char)i;
    sbs_dirty = 1;
    sbs_vpoke_count = sbs_gget_count = sbs_gput_count = 0;
    return 0;
}

static void ovl_sbs_release_graphics(void)
{
    int i;

    if (sbs_gseg) os88_mem_free(sbs_gseg);
    sbs_gseg = 0;
    sbs_gkb = 0;
    for (i = 0; i < SBS_PLANES; i++) {
        if (sbs_pseg[i]) os88_mem_free(sbs_pseg[i]);
        sbs_pseg[i] = 0;
    }
    sbs_plane_kb = 0;
    sbs_screen = 0;
    sbs_mode = SB_MODE_TEXT;
    sbs_dirty = 1;
    sbs_vpoke_count = sbs_gget_count = sbs_gput_count = 0;
}

/* Text sessions pay no graphics heap. Claims are taken on the first SCREEN
 * statement, after the app has launched and the file manager can be closed.
 * They remain owned by the instance so later mode switches retain pixels. */
static int ovl_sbs_claim_graphics(int mode)
{
    int i, planes, kb, gkb;

    /* EGA 640x350 uses 28,000 bytes per bitplane; claiming the rounded 28K
     * preserves every pixel and leaves enough conventional memory for the
     * interpreter's 16K-cell numeric store on a 640K machine. */
    kb = mode == 9 ? 28 : (mode == 13 ? SBS_PLANE_KB : 8);
    gkb = mode == 9 ? 0 : SBS_GKB;
    planes = mode == 13 ? 2 : SBS_PLANES;
    if (sbs_plane_kb && sbs_plane_kb != kb) {
        for (i = 0; i < SBS_PLANES; i++) {
            if (sbs_pseg[i]) os88_mem_free(sbs_pseg[i]);
            sbs_pseg[i] = 0;
        }
        sbs_plane_kb = 0;
    }
    if (sbs_gseg && sbs_gkb != gkb) {
        os88_mem_free(sbs_gseg); sbs_gseg = 0; sbs_gkb = 0;
    }
    if (!sbs_gseg && gkb) {
        sbs_gseg = os88_mem_claim(gkb);
        if (sbs_gseg) sbs_gkb = gkb;
    }
    for (i = 0; i < planes; i++)
        if (!sbs_pseg[i]) sbs_pseg[i] = os88_mem_claim(kb);
    if ((mode != 9 && !sbs_gseg) || !sbs_pseg[0] || !sbs_pseg[1] ||
        (planes == SBS_PLANES && (!sbs_pseg[2] || !sbs_pseg[3]))) {
        os88_toast("Speedy: graphics memory unavailable.", 0);
        return -1;
    }
    sbs_plane_kb = kb;
    if (mode == 9) {
        for (i = 0; i < 16; i++) sbs_ega_map[i] = (unsigned char)i;
    } else {
        for (i = 0; i < 256; i++) sb_map_set(sbs_gseg, i, i & 15);
        sb_fb_clear(sbs_gseg, OS88_BLACK);
    }
    for (i = 0; i < planes; i++) sb_plane_clear(sbs_pseg[i], 0, kb);
    return 0;
}

static void ovl_sbs_mode(int mode, int width, int height)
{
    if (mode && ovl_sbs_claim_graphics(mode) < 0) return;
    sbs_screen = mode;
    sbs_mode = mode ? SB_MODE_GRAPHICS : SB_MODE_TEXT;
    sbs_lw = width > 0 ? width : SBS_GW;
    sbs_lh = height > 0 ? height : SBS_GH;
    sbs_dirty = 1;
}

static void sbs_host_mode(int mode, int width, int height)
{
    ovl_sbs_mode(mode, width, height);
}

static void ovl_sbs_clear(int color)
{
    int i;

    if (sbs_mode == SB_MODE_GRAPHICS) {
        if (sbs_gseg && sbs_screen != 9)
            sb_fb_clear(sbs_gseg, sb_map_get(sbs_gseg, color));
        if (sbs_screen == 13) {
            sb_byte_clear(sbs_pseg[0], color & 255);
            sb_byte_clear(sbs_pseg[1], color & 255);
        } else {
            for (i = 0; i < SBS_PLANES; i++)
                sb_plane_clear(sbs_pseg[i], (color >> i) & 1,
                               sbs_plane_kb);
        }
    } else {
        for (i = 0; i < SBS_COLS * SBS_ROWS; i++) {
            sbs_ch[i] = ' ';
            sbs_at[i] = 7;
        }
    }
    sbs_dirty = 1;
}

static void sbs_host_clear(int color)
{
    ovl_sbs_clear(color);
}

static void sbs_host_text_put(int row, int col, int ch, int fg, int bg)
{
    int p;

    row--;                              /* Turbo Basic LOCATE is one-based */
    col--;
    if (row < 0 || row >= SBS_ROWS || col < 0 || col >= SBS_COLS)
        return;
    p = row * SBS_COLS + col;
    sbs_ch[p] = (unsigned char)ch;
    sbs_at[p] = (unsigned char)(((bg & 7) << 4) | (fg & 15));
    sbs_dirty = 1;
}

static void sbs_host_text_cursor(int row, int col)
{
    row--;
    col--;
    (void)row;
    (void)col;
}

static void ovl_sbs_text_scroll(void)
{
    int row, col, dst, src;

    for (row = 1; row < SBS_ROWS; row++) {
        src = (row << 6) + (row << 4);
        dst = src - SBS_COLS;
        for (col = 0; col < SBS_COLS; col++) {
            sbs_ch[dst + col] = sbs_ch[src + col];
            sbs_at[dst + col] = sbs_at[src + col];
        }
    }
    dst = ((SBS_ROWS - 1) << 6) + ((SBS_ROWS - 1) << 4);
    for (col = 0; col < SBS_COLS; col++) {
        sbs_ch[dst + col] = ' ';
        sbs_at[dst + col] = 7;
    }
    sbs_dirty = 1;
}

static void sbs_host_text_scroll(void)
{
    ovl_sbs_text_scroll();
}

static void sbs_plot_scaled(int x, int y, int color)
{
    int px, py;

    if (!sbs_gseg || sbs_screen == 9)
        return;
    px = sbs_scale_x(x);
    py = sbs_scale_y(y);
    if (px >= 0 && py >= 0)
        sb_fb_pixel(sbs_gseg, px, py, sb_map_get(sbs_gseg, color));
}

static void sbs_host_pixel(int x, int y, int color)
{
    unsigned off;
    int bit, i;

    if (x < 0 || x >= sbs_lw || y < 0 || y >= sbs_lh) return;
    if (sbs_screen == 13) {
        off = (unsigned)y;
        off = (off << 8) + (off << 6) + (unsigned)x;
        if (off & 0x8000) sb_byte_set(sbs_pseg[1], off & 0x7fff, color);
        else sb_byte_set(sbs_pseg[0], off, color);
    } else {
        off = sbs_rowoff(y) + (unsigned)(x >> 3);
        bit = 7 - (x & 7);
        for (i = 0; i < SBS_PLANES; i++)
            sb_plane_pixel(sbs_pseg[i], off, bit, (color >> i) & 1);
    }
    sbs_plot_scaled(x, y, color);
    sbs_dirty = 1;
}

static int ovl_sbs_point(int x, int y)
{
    unsigned off;
    int bit, i, color;
    if (x < 0 || x >= sbs_lw || y < 0 || y >= sbs_lh) return -1;
    if (sbs_screen == 13) {
        off = (unsigned)y;
        off = (off << 8) + (off << 6) + (unsigned)x;
        if (off & 0x8000) return sb_byte_get(sbs_pseg[1], off & 0x7fff);
        return sb_byte_get(sbs_pseg[0], off);
    }
    off = sbs_rowoff(y) + (unsigned)(x >> 3);
    bit = 7 - (x & 7); color = 0;
    for (i = 0; i < SBS_PLANES; i++)
        if (sb_plane_point(sbs_pseg[i], off, bit)) color |= 1 << i;
    return color;
}

static int sbs_host_point(int x, int y)
{
    return ovl_sbs_point(x, y);
}

static void ovl_sbs_line(void)
{
    int x1, y1, x2, y2, color, dx, dy, sx, sy, err, e2;

    x1 = (int)sbs_arg[0]; y1 = (int)sbs_arg[1];
    x2 = (int)sbs_arg[2]; y2 = (int)sbs_arg[3]; color = (int)sbs_arg[4];

    dx = ovl_sbs_abs(x2 - x1);
    dy = ovl_sbs_abs(y2 - y1);
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx - dy;
    for (;;) {
        sbs_host_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2)
            break;
        e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
    sbs_dirty = 1;
}

static void sbs_host_line(int x1, int y1, int x2, int y2, int color)
{
    sbs_arg[0] = (unsigned)x1; sbs_arg[1] = (unsigned)y1;
    sbs_arg[2] = (unsigned)x2; sbs_arg[3] = (unsigned)y2;
    sbs_arg[4] = (unsigned)color;
    ovl_sbs_line();
}

static void ovl_sbs_circle(int cx, int cy, int radius, int color)
{
    int x, y, d;
    if (radius < 0) return;
    x = radius; y = 0; d = 1 - radius;
    while (x >= y) {
        sbs_host_pixel(cx + x, cy + y, color);
        sbs_host_pixel(cx + y, cy + x, color);
        sbs_host_pixel(cx - y, cy + x, color);
        sbs_host_pixel(cx - x, cy + y, color);
        sbs_host_pixel(cx - x, cy - y, color);
        sbs_host_pixel(cx - y, cy - x, color);
        sbs_host_pixel(cx + y, cy - x, color);
        sbs_host_pixel(cx + x, cy - y, color);
        y++;
        if (d < 0) d += (y << 1) + 1;
        else { x--; d += ((y - x) << 1) + 1; }
    }
}

static void sbs_host_circle(int cx, int cy, int radius, int color)
{
    ovl_sbs_circle(cx, cy, radius, color);
}

/* Boundary fill with alternating raster passes.  It owns no resident queue,
 * which matters on the 8088 package budget, and preserves exact logical
 * SCREEN 9 pixels. */
static void ovl_sbs_paint(int sx, int sy, int color, int border)
{
    int x, y, changed, old, touch;
    old = ovl_sbs_point(sx, sy);
    if (old < 0 || old == color || old == border) return;
    do {
        changed = 0;
        for (y = 0; y < sbs_lh; y++)
            for (x = 0; x < sbs_lw; x++) {
                if (ovl_sbs_point(x, y) != old) continue;
                touch = x == sx && y == sy;
                if (!touch && x > 0)
                    touch = ovl_sbs_point(x - 1, y) == color;
                if (!touch && y > 0)
                    touch = ovl_sbs_point(x, y - 1) == color;
                if (!touch && x + 1 < sbs_lw)
                    touch = ovl_sbs_point(x + 1, y) == color;
                if (!touch && y + 1 < sbs_lh)
                    touch = ovl_sbs_point(x, y + 1) == color;
                if (touch) {
                    sbs_host_pixel(x, y, color); changed = 1;
                }
            }
    } while (changed);
}

static void sbs_host_paint(int sx, int sy, int color, int border)
{
    ovl_sbs_paint(sx, sy, color, border);
}

static unsigned ovl_sbs_gfx_get(void)
{
    int x, y, w, h, x1, y1, x2, y2;
    unsigned seg, off;
    x1 = (int)sbs_arg[0]; y1 = (int)sbs_arg[1];
    x2 = (int)sbs_arg[2]; y2 = (int)sbs_arg[3];
    seg = sbs_arg[4]; off = sbs_arg[5];
    if (x2 < x1 || y2 < y1) return 0;
    w = x2 - x1 + 1; h = y2 - y1 + 1;
    os88_poke(seg, off++, w & 255); os88_poke(seg, off++, (w >> 8) & 255);
    os88_poke(seg, off++, h & 255); os88_poke(seg, off++, (h >> 8) & 255);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            os88_poke(seg, off++, ovl_sbs_point(x1 + x, y1 + y));
    return (unsigned)(w * h + 4);
}

static unsigned sbs_host_gfx_get(int x1, int y1, int x2, int y2,
                                 unsigned seg, unsigned off)
{
    sbs_gget_count++;
    sbs_arg[0] = (unsigned)x1; sbs_arg[1] = (unsigned)y1;
    sbs_arg[2] = (unsigned)x2; sbs_arg[3] = (unsigned)y2;
    sbs_arg[4] = seg; sbs_arg[5] = off;
    return ovl_sbs_gfx_get();
}

static void ovl_sbs_gfx_put(void)
{
    int x, y, w, h, src, dst, x0, y0, op;
    unsigned seg, off;
    x0 = (int)sbs_arg[0]; y0 = (int)sbs_arg[1];
    seg = sbs_arg[2]; off = sbs_arg[3]; op = (int)sbs_arg[4];
    w = os88_peek(seg, off++); w |= os88_peek(seg, off++) << 8;
    h = os88_peek(seg, off++); h |= os88_peek(seg, off++) << 8;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        src = os88_peek(seg, off++); dst = ovl_sbs_point(x0 + x, y0 + y);
        if (op == SB_PUT_PRESET) src = ~src;
        else if (op == SB_PUT_XOR) src ^= dst;
        else if (op == SB_PUT_OR) src |= dst;
        else if (op == SB_PUT_AND) src &= dst;
        sbs_host_pixel(x0 + x, y0 + y, src);
    }
}

static void sbs_host_gfx_put(int x, int y, unsigned seg, unsigned off, int op)
{
    sbs_gput_count++;
    sbs_arg[0] = (unsigned)x; sbs_arg[1] = (unsigned)y;
    sbs_arg[2] = seg; sbs_arg[3] = off; sbs_arg[4] = (unsigned)op;
    ovl_sbs_gfx_put();
}

static int ovl_sbs_video_peek(unsigned seg, unsigned off)
{
    int row, col, shift, v;
    unsigned local;
    if (seg == 0xb800 && sbs_screen == 0 && off < 4000) {
        if (off & 1) return sbs_at[off >> 1];
        return sbs_ch[off >> 1];
    }
    if (seg == 0xb800 && sbs_screen == 1 && off < 16384) {
        local = off & 8191;
        if (local >= 8000) return 0;
        row = (off >= 8192 ? 1 : 0) + (local / 80) * 2;
        col = (local % 80) * 4; v = 0;
        for (shift = 6; shift >= 0; shift -= 2)
            v |= (ovl_sbs_point(col++, row) & 3) << shift;
        return v;
    }
    if (seg == 0xa000 && sbs_screen == 13) {
        if (off & 0x8000) return sb_byte_get(sbs_pseg[1], off & 0x7fff);
        return sb_byte_get(sbs_pseg[0], off);
    }
    return 0;
}

static int sbs_host_video_peek(unsigned seg, unsigned off)
{
    return ovl_sbs_video_peek(seg, off);
}

static void ovl_sbs_video_poke(unsigned seg, unsigned off, int value)
{
    int row, col, shift;
    unsigned local;
    value &= 255;
    if (seg == 0xb800 && sbs_screen == 0 && off < 4000) {
        if (off & 1) sbs_at[off >> 1] = (unsigned char)value;
        else sbs_ch[off >> 1] = (unsigned char)value;
        sbs_dirty = 1; return;
    }
    if (seg == 0xb800 && sbs_screen == 1 && off < 16384) {
        local = off & 8191;
        if (local >= 8000) return;
        row = (off >= 8192 ? 1 : 0) + (local / 80) * 2;
        col = (local % 80) * 4;
        for (shift = 6; shift >= 0; shift -= 2)
            sbs_host_pixel(col++, row, (value >> shift) & 3);
    } else if (seg == 0xa000 && sbs_screen == 13) {
        if (off & 0x8000) sb_byte_set(sbs_pseg[1], off & 0x7fff, value);
        else sb_byte_set(sbs_pseg[0], off, value);
        row = off / 320; col = off % 320;
        sbs_plot_scaled(col, row, value); sbs_dirty = 1;
    }
}

static void sbs_host_video_poke(unsigned seg, unsigned off, int value)
{
    sbs_vpoke_count++;
    ovl_sbs_video_poke(seg, off, value);
}

static void ovl_sbs_palette(int index, int r, int g, int b)
{
    index &= 255; r &= 63; g &= 63; b &= 63;
    if (sbs_screen == 9)
        sbs_ega_map[index & 15] = (unsigned char)ovl_sbs_nearest(r, g, b);
    else if (sbs_gseg)
        sb_map_set(sbs_gseg, index, ovl_sbs_nearest(r, g, b));
    sbs_dirty = 1;
}

static void sbs_host_palette(int index, int r, int g, int b)
{
    ovl_sbs_palette(index, r, g, b);
}

static void ovl_sbs_port_out(unsigned port, int value)
{
    if (port == 0x3c8) { sbs_dac_index = value & 255; sbs_dac_part = 0; }
    else if (port == 0x3c9) {
        sbs_dac_rgb[sbs_dac_part] = (unsigned char)(value & 63);
        sbs_dac_part++;
        if (sbs_dac_part == 3) {
            ovl_sbs_palette(sbs_dac_index,
                sbs_dac_rgb[0], sbs_dac_rgb[1], sbs_dac_rgb[2]);
            sbs_dac_part = 0; sbs_dac_index = (sbs_dac_index + 1) & 255;
        }
    }
}

static void sbs_host_port_out(unsigned port, int value)
{
    ovl_sbs_port_out(port, value);
}

static void ovl_sbs_sound(int hz, int ticks)
{
    os88_snd_tone(hz, ticks, 0x40);
}

static void sbs_host_sound(int hz, int ticks)
{
    ovl_sbs_sound(hz, ticks);
}

static unsigned ovl_sbs_ticks(void)
{
    return os88_ticks();
}

static unsigned sbs_host_ticks(void)
{
    return ovl_sbs_ticks();
}

static void sbs_host_changed(void)
{
    /* Parser progress and variable assignments do not change the retained
     * display. Text/graphics callbacks mark their own writes dirty. */
}

static void ovl_sbs_text_paint(int x, int y)
{
    int row, col, end, p, at, fg, bg, n;

    for (row = 0; row < SBS_ROWS; row++) {
        col = 0;
        while (col < SBS_COLS) {
            p = row * SBS_COLS + col;
            at = sbs_at[p];
            end = col + 1;
            while (end < SBS_COLS && sbs_at[row * SBS_COLS + end] == at)
                end++;
            n = 0;
            while (col < end)
                sb_ui_text[n++] = (char)sbs_ch[row * SBS_COLS + col++];
            sb_ui_text[n] = 0;
            fg = at & 15;
            bg = (at >> 4) & 7;
            os88_font_run(x + (col - n) * 8, y + row * 8,
                          sb_ui_text, fg, bg);
        }
    }
}

static void ovl_sbs_paint_surface(int x, int y, int w, int h)
{
    int ox, oy;

    os88_set_color(OS88_BLACK);
    os88_gfx_fill(x, y, x + w - 1, y + h - 1);
    if (sbs_mode == SB_MODE_GRAPHICS) {
        if (!sbs_gseg && sbs_screen != 9) {
            os88_font_run(x + 8, y + 8, "Graphics needs 32K free.",
                          OS88_WHITE, OS88_BLACK);
            return;
        }
        ox = x + (w - SBS_GW) / 2;
        oy = y + (h - SBS_GH) / 2;
        if (ox < x) ox = x;
        if (oy < y) oy = y;
        if (sbs_screen == 9)
            sb_fb_blit9(sbs_gseg, sbs_pseg[0], sbs_pseg[1],
                        sbs_pseg[2], sbs_pseg[3], ox, oy);
        else if (sbs_screen == 13)
            sb_fb_map13(sbs_gseg, sbs_pseg[0], sbs_pseg[1]);
        if (sbs_screen != 9) sb_fb_blit(sbs_gseg, ox, oy);
    } else {
        ox = x + (w - SBS_COLS * 8) / 2;
        oy = y + (h - SBS_ROWS * 8) / 2;
        if (ox < x) ox = x;
        if (oy < y) oy = y;
        ovl_sbs_text_paint(ox, oy);
    }
    sbs_dirty = 0;
}
