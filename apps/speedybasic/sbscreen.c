/* Retained Turbo Basic text and graphics surfaces.  This file is included by
 * sbasic.c; it is not a separate translation unit (SPEC 73.1). */

#define SBS_COLS       80
#define SBS_ROWS       25
#define SBS_GW         320
#define SBS_GH         200
#define SBS_GSTRIDE    160
#define SBS_GKB        32

static unsigned char sbs_ch[SBS_COLS * SBS_ROWS];
static unsigned char sbs_at[SBS_COLS * SBS_ROWS];
static char sbs_run[SBS_COLS + 1];
static unsigned sbs_gseg;
static int sbs_mode;
static int sbs_lw, sbs_lh;
static int sbs_crow, sbs_ccol;
static int sbs_dirty;
static unsigned sbs_raw;
static unsigned char sbs_palette[256];
static unsigned char sbs_dac[768];
static int sbs_dac_at, sbs_palette_dirty, sbs_bios_mode = 3;
void sb_fb_map(unsigned raw, unsigned packed, unsigned char *map);
void sb_memzero(unsigned seg,unsigned bytes);
static int sbc_memory_owned(unsigned seg,unsigned off);
static void sbc_set_error(const char *s);

/* These three routines are in speedybasic.asm because compiled C cannot name
 * the ES segment of a heap claim. */
void sb_fb_clear(unsigned seg, int color);
void sb_fb_pixel(unsigned seg, int x, int y, int color);
void sb_fb_blit(unsigned seg, int x, int y);

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

static int sbs_init(void)
{
    int i;

    sbs_gseg = os88_mem_claim(SBS_GKB);
    sbs_mode = SB_MODE_TEXT;
    sbs_lw = SBS_GW;
    sbs_lh = SBS_GH;
    sbs_crow = 0;
    sbs_ccol = 0;
    for (i = 0; i < SBS_COLS * SBS_ROWS; i++) {
        sbs_ch[i] = ' ';
        sbs_at[i] = 7;
    }
    if (sbs_gseg)
        sb_fb_clear(sbs_gseg, OS88_BLACK);
    sbs_dirty = 1;
    return sbs_gseg ? 0 : -1;
}

static void sbs_host_mode(int mode, int width, int height)
{
    if (!mode && sbs_raw) { os88_mem_free(sbs_raw); sbs_raw = 0; }
    sbs_mode = mode ? SB_MODE_GRAPHICS : SB_MODE_TEXT;
    sbs_lw = width > 0 ? width : SBS_GW;
    sbs_lh = height > 0 ? height : SBS_GH;
    sbs_dirty = 1;
}

static void sbs_host_clear(int color)
{
    int i;

    if (sbs_mode == SB_MODE_GRAPHICS) {
        if (sbs_gseg)
            sb_fb_clear(sbs_gseg, color & 15);
        if (sbs_raw && !color) sb_memzero(sbs_raw,64000u);
    } else {
        for (i = 0; i < SBS_COLS * SBS_ROWS; i++) {
            sbs_ch[i] = ' ';
            sbs_at[i] = (unsigned char)(color & 0x7f);
        }
        sbs_crow = sbs_ccol = 0;
    }
    sbs_dirty = 1;
}

static void sbs_host_text_put(int row, int col, int ch, int fg, int bg)
{
    int p;
    --row;
    --col;
    if (row < 0 || row >= SBS_ROWS || col < 0 || col >= SBS_COLS)
        return;
    p = row * SBS_COLS + col;
    sbs_ch[p] = (unsigned char)ch;
    sbs_at[p] = (unsigned char)(((bg & 7) << 4) | (fg & 15));
    sbs_dirty = 1;
}

static void sbs_host_text_cursor(int row, int col)
{
    --row;
    --col;
    if (row >= 0 && row < SBS_ROWS)
        sbs_crow = row;
    if (col >= 0 && col < SBS_COLS)
        sbs_ccol = col;
}

static void sbs_plot_scaled(int x, int y, int color)
{
    int px, py;

    if (!sbs_gseg)
        return;
    px = sbs_scale_x(x);
    py = sbs_scale_y(y);
    if (px >= 0 && py >= 0) {
        if (sbs_bios_mode == 1) {
            color &= 3;
            if (sbs_raw) os88_poke(sbs_raw,(unsigned)py*320+px,color);
            color = color == 1 ? 11 : color == 2 ? 13 : color == 3 ? 15 : 0;
        } else if (sbs_bios_mode == 19) {
            if (sbs_raw) os88_poke(sbs_raw,(unsigned)py*320+px,color);
            color = sbs_palette[color & 255];
        }
        sb_fb_pixel(sbs_gseg, px, py, color & 15);
    }
}

static void sbs_host_pixel(int x, int y, int color)
{
    sbs_plot_scaled(x, y, color);
    sbs_dirty = 1;
}

static int sbs_abs(int n)
{
    return n < 0 ? -n : n;
}

static void sbs_host_line(int x1, int y1, int x2, int y2, int color)
{
    int dx, dy, sx, sy, err, e2;

    dx = sbs_abs(x2 - x1);
    dy = sbs_abs(y2 - y1);
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx - dy;
    for (;;) {
        sbs_plot_scaled(x1, y1, color);
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

static void sbs_host_sound(int hz, int ticks)
{
    os88_snd_tone(hz, ticks, 0x40);
}

static unsigned sbs_host_ticks(void)
{
    return os88_ticks();
}

static void sbs_host_changed(void)
{
    sbs_dirty = 1;
}

static void sbs_text_paint(int x, int y)
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
                sbs_run[n++] = (char)sbs_ch[row * SBS_COLS + col++];
            sbs_run[n] = 0;
            fg = at & 15;
            bg = (at >> 4) & 7;
            os88_font_run(x + (col - n) * 8, y + row * 8,
                          sbs_run, fg, bg);
        }
    }
}

static void sbs_paint_surface(int x, int y, int w, int h)
{
    int ox, oy;

    os88_set_color(OS88_BLACK);
    os88_gfx_fill(x, y, x + w - 1, y + h - 1);
    if (sbs_mode == SB_MODE_GRAPHICS) {
        if (!sbs_gseg) {
            os88_font_run(x + 8, y + 8, "Graphics needs 32K free.",
                          OS88_WHITE, OS88_BLACK);
            return;
        }
        ox = x + (w - SBS_GW) / 2;
        oy = y + (h - SBS_GH) / 2;
        if (ox < x) ox = x;
        if (oy < y) oy = y;
        if (sbs_palette_dirty && sbs_raw && sbs_bios_mode == 19) {
            sb_fb_map(sbs_raw, sbs_gseg, sbs_palette);
            sbs_palette_dirty = 0;
        }
        sb_fb_blit(sbs_gseg, ox, oy);
    } else {
        ox = x + (w - SBS_COLS * 8) / 2;
        oy = y + (h - SBS_ROWS * 8) / 2;
        if (ox < x) ox = x;
        if (oy < y) oy = y;
        sbs_text_paint(ox, oy);
    }
    sbs_dirty = 0;
}

/* Virtual PC video memory. Never expose the desktop's hardware segments to
 * a BASIC program: the web interpreter uses the same retained-memory model. */
static void sbs_palette_set(int index)
{
    int i,r,g,b,dr,dg,db,dist,best,which;
    static unsigned char rgb[48]={0,0,0,0,0,42,0,42,0,0,42,42,42,0,0,42,0,42,42,21,0,42,42,42,21,21,21,21,21,63,21,63,21,21,63,63,63,21,21,63,21,63,63,63,21,63,63,63};
    r=sbs_dac[index*3];g=sbs_dac[index*3+1];b=sbs_dac[index*3+2];best=32767;which=0;
    for(i=0;i<16;++i){dr=r-rgb[i*3];dg=g-rgb[i*3+1];db=b-rgb[i*3+2];dist=dr*dr+dg*dg+db*db;if(dist<best){best=dist;which=i;}}
    sbs_palette[index]=which;sbs_palette_dirty=1;sbs_dirty=1;
}
static void sbs_out(int port,int value)
{
    if(port==0x3c8){value&=255;sbs_dac_at=(value<<1)+value;}
    if(port==0x3c9){sbs_dac[sbs_dac_at]=value&63;++sbs_dac_at;if(sbs_dac_at%3==0)sbs_palette_set((sbs_dac_at-1)/3);if(sbs_dac_at>=768)sbs_dac_at=0;}
}
static void sbs_video_mode(int mode)
{
    int i;
    sbs_bios_mode=mode;
    if((mode==19||mode==1)&&!sbs_raw)sbs_raw=os88_mem_claim(63);
    if((mode==19||mode==1)&&!sbs_raw){sbc_set_error("Not enough graphics memory");return;}
    for(i=0;i<256;++i)sbs_palette[i]=i&15;
    sbs_palette_dirty=0;
    sbs_host_mode(mode==3?0:1,mode==19?320:mode==9?640:320,mode==9?350:200);
    sbs_host_clear(0);
}
static int sbs_mem_read(unsigned seg,unsigned off)
{
    int x,y,p,v;
    if(seg==0x40){if(off==0x49)return sbs_bios_mode;if(off==0x4a)return 80;if(off==0x4b)return 0;
        if(off==0x6c)return os88_ticks()&255;if(off==0x6d)return os88_ticks()>>8;return 0;}
    if(seg==0xa000)return sbs_raw&&off<64000u?os88_peek(sbs_raw,off):0;
    if(seg==0xb800||seg==0xb000){
        if(sbs_mode==0){if(off>=4000)return 0;return off&1?sbs_at[off>>1]:sbs_ch[off>>1];}
        y=(off&8191)/80*2+((off&8192)!=0);x=(off%8192)%80*4;
        if(y>=200)return 0;v=0;
        for(p=0;p<4;++p){v<<=2;v|=sbs_raw?os88_peek(sbs_raw,(unsigned)y*320+x+p)&3:0;}return v;
    }
    /* DGROUP/array claims are the only non-video memory BASIC owns. */
    return sbc_memory_owned(seg,off)?os88_peek(seg,off):0;
}
static void sbs_mem_write(unsigned seg,unsigned off,int value)
{
    int x,y,p;
    if(seg==0xa000){if(sbs_raw&&off<64000u){os88_poke(sbs_raw,off,value);sb_fb_pixel(sbs_gseg,off%320,off/320,sbs_palette[value&255]);sbs_dirty=1;}return;}
    if(seg==0xb800||seg==0xb000){
        if(sbs_mode==0){if(off<4000){if(off&1)sbs_at[off>>1]=value;else sbs_ch[off>>1]=value;sbs_dirty=1;}return;}
        if(!sbs_raw)sbs_raw=os88_mem_claim(63);
        y=(off&8191)/80*2+((off&8192)!=0);x=(off%8192)%80*4;
        if(y>=200)return;
        for(p=3;p>=0;--p){if(sbs_raw)os88_poke(sbs_raw,(unsigned)y*320+x+p,value&3);sb_fb_pixel(sbs_gseg,x+p,y,(value&3)==1?11:(value&3)==2?13:(value&3)==3?15:0);value>>=2;}
        sbs_dirty=1;return;
    }
    if(sbc_memory_owned(seg,off))os88_poke(seg,off,value);
}
