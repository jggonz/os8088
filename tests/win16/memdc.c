/* memdc.c - WIN16 fixture (SPEC.md 124.25.3-124.25.6,
 * docs/plans/WIN16-ANY-PLAN.md S19b): drawing into a memory DC, the way
 * Paintbrush keeps its picture.
 *
 * A 64 x 48 bitmap made compatible with the window's DC, held by a memory
 * DC, is drawn on with every call Paintbrush draws with - PatBlt, FillRect,
 * Rectangle, MoveTo/LineTo, a filled Polygon, ExtFloodFill inside a frame,
 * TextOut, SetPixel - and each is checked at pixels whose colour Windows
 * defines, read back through GetDIBits (which answers the same on Windows
 * as under WIN16, whatever either keeps a bitmap as). Then the bits go out
 * and back in by GetBitmapBits/SetBitmapBits and GetDIBits/SetDIBits; a
 * clip region and ExcludeClipRect cut a fill; and the screen is copied
 * into a bitmap and a bitmap onto the screen, with SetWindowOrg moving the
 * window's DC as Paintbrush moves it when it scrolls; and ExtFloodFill fills
 * on the window's DC, inside a frame and then across the whole client.
 *
 * Then what Paintbrush's File Open needs (SPEC.md 124.9.1, 124.25.8,
 * 124.25.9): every DC and bitmap handle even, as PBRUSH.DLL marks its own
 * virtual ones odd; a logical palette kept, read back, searched and indexed
 * by a DIB_PAL_COLORS colour table; GMEM_NOTIFY blocks given up only when
 * the GlobalNotify procedure says so, as a short heap makes room, which is
 * PBRUSH.DLL's band cache; and two hundred moveable handles at once.
 *
 * Last, what Visual Basic 3.0's runtime imports from GDI (SPEC.md 124.25.14):
 * OffsetWindowOrg, SetRectRgn, CreatePenIndirect, CreateBrushIndirect, Arc
 * (the short way, the long way and a whole ellipse), Pie, and the three
 * metafile calls that answer 0; Arc and Pie are drawn again on the window
 * itself, along its bottom edge, to be looked at on each adapter.
 *
 * And FloodFill (GDI.25), and a wide parallelogram Polygon - the shape of a
 * fast Paintbrush stroke - filled and outlined (pgfill, pgout, pgpen, ffill).
 *
 * Every check that fails is named in the message box, which
 * tests/win16memdc.py reads; "MEMDC OK" when none does.
 */
#include <windows.h>
#include <string.h>

#define W 64
#define H 48
#define STRIDE 32                       /* 64 pixels at 4 bits, to dwords */

static char res[256];
static struct { BITMAPINFOHEADER h; RGBQUAD c[16]; } bi;
static BYTE bits[STRIDE * H];
static BYTE keep[STRIDE * H * 4];       /* GetBitmapBits', whatever its form */
static HDC mem;
static HBITMAP bm, old;
static int notified;
static HGLOBAL asked;

static void check(int ok, const char *tag)
{
    if (!ok) {
        if (res[0] == 0)
            lstrcpy(res, "MEMDC failed:");
        if (lstrlen(res) < sizeof(res) - 12) {
            lstrcat(res, " ");
            lstrcat(res, tag);
        }
    }
}

/* bitmap b, held by memory DC m, as a 4bpp DIB - read with it out of the
 * DC, as Windows asks */
static void snapof(HDC dc, HDC m, HBITMAP b)
{
    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = W;
    bi.h.biHeight = H;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 4;
    SelectObject(m, old);
    GetDIBits(dc, b, 0, H, bits, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
    SelectObject(m, b);
}
#define snap(dc) snapof(dc, mem, bm)

/* pixel x, y (y from the top) of the last snap, as an RGB */
static COLORREF pix(int x, int y)
{
    BYTE b = bits[(H - 1 - y) * STRIDE + x / 2];
    int i = (x & 1) ? (b & 15) : (b >> 4);
    return RGB(bi.c[i].rgbRed, bi.c[i].rgbGreen, bi.c[i].rgbBlue);
}

#define BLACK RGB(0, 0, 0)
#define WHITE RGB(255, 255, 255)
#define RED   RGB(255, 0, 0)
#define GREEN RGB(0, 255, 0)
#define BLUE  RGB(0, 0, 255)

/* GlobalNotify's procedure: counts, remembers, and lets the block go */
BOOL __export CALLBACK NotifyProc(HGLOBAL h)
{
    notified++;
    asked = h;
    return TRUE;
}

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    static WNDCLASS wc;
    static POINT tri[3] = { {34, 4}, {60, 4}, {34, 30} };
    static RECT r;
    HWND hwnd;
    HDC dc;
    HBRUSH red, green, blue, ob;
    HPEN op;
    HBITMAP bm2;
    HDC mem2;
    LONG n;
    int x, y, k;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "MemDC";
    RegisterClass(&wc);
    hwnd = CreateWindow("MemDC", "MEMDC", WS_OVERLAPPEDWINDOW,
                        0, 0, 300, 160, NULL, NULL, inst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    dc = GetDC(hwnd);
    mem = CreateCompatibleDC(dc);
    bm = CreateCompatibleBitmap(dc, W, H);
    old = SelectObject(mem, bm);
    red = CreateSolidBrush(RED);
    green = CreateSolidBrush(GREEN);
    blue = CreateSolidBrush(BLUE);

    /* --- drawing into the bitmap --- */
    PatBlt(mem, 0, 0, W, H, WHITENESS);
    SetRect(&r, 4, 4, 12, 12);
    FillRect(mem, &r, GetStockObject(BLACK_BRUSH));
    ob = SelectObject(mem, red);            /* black pen, red inside */
    Rectangle(mem, 16, 4, 28, 14);
    MoveTo(mem, 2, 20);
    LineTo(mem, 30, 20);
    op = SelectObject(mem, GetStockObject(NULL_PEN));
    SelectObject(mem, blue);
    Polygon(mem, tri, 3);
    SelectObject(mem, op);
    SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, 36, 32, 60, 46);         /* a frame, then filled inside */
    SelectObject(mem, green);
    check(ExtFloodFill(mem, 48, 39, BLACK, FLOODFILLBORDER), "floodret");
    SelectObject(mem, ob);
    TextOut(mem, 2, 30, "W", 1);
    SetPixel(mem, 1, 1, RED);
    snap(dc);
    check(pix(0, 0) == WHITE && pix(63, 47) == WHITE, "patblt");
    check(pix(4, 4) == BLACK && pix(11, 11) == BLACK
          && pix(12, 12) == WHITE && pix(3, 3) == WHITE, "fillrect");
    check(pix(16, 4) == BLACK && pix(27, 13) == BLACK
          && pix(28, 14) == WHITE, "rectpen");
    check(pix(20, 8) == RED, "rectbrush");
    check(pix(2, 20) == BLACK && pix(29, 20) == BLACK
          && pix(30, 20) == WHITE, "lineto");
    check(pix(38, 8) == BLUE && pix(36, 6) == BLUE
          && pix(58, 28) == WHITE, "polygon");
    check(pix(48, 39) == GREEN && pix(37, 33) == GREEN
          && pix(36, 32) == BLACK && pix(62, 40) == WHITE, "flood");
    for (k = 0, y = 30; y < 46; y++)
        for (x = 2; x < 16; x++)
            k += pix(x, y) == BLACK;
    check(k > 0, "textout");
    check(pix(1, 1) == RED, "setpixel");

    /* --- the bits out and back --- */
    n = GetBitmapBits(bm, sizeof(keep), keep);
    check(n > 0, "getbits");
    PatBlt(mem, 0, 0, W, H, WHITENESS);
    snap(dc);
    check(pix(20, 8) == WHITE, "cleared");
    check(SetBitmapBits(bm, n, keep) == n, "setbitsret");
    snap(dc);
    check(pix(20, 8) == RED && pix(48, 39) == GREEN
          && pix(38, 8) == BLUE && pix(4, 4) == BLACK, "setbits");
    PatBlt(mem, 0, 0, W, H, BLACKNESS);
    SelectObject(mem, old);
    check(SetDIBits(dc, bm, 0, H, bits, (BITMAPINFO FAR *)&bi,
                    DIB_RGB_COLORS) == H, "setdibret");
    SelectObject(mem, bm);
    snap(dc);
    check(pix(38, 8) == BLUE && pix(0, 0) == WHITE, "setdib");

    /* --- a clip region and an excluded edge cut a fill --- */
    {
        HRGN rgn = CreateRectRgn(8, 8, 24, 24);
        SelectClipRgn(mem, rgn);
        PatBlt(mem, 0, 0, W, H, BLACKNESS);
        ExcludeClipRect(mem, 8, 8, 12, 24);
        SetRect(&r, 0, 0, W, H);
        FillRect(mem, &r, red);
        SelectClipRgn(mem, NULL);
        DeleteObject(rgn);
        snap(dc);
        check(pix(30, 10) == WHITE && pix(8, 8) == BLACK && pix(12, 8) == RED
              && pix(23, 23) == RED && pix(24, 24) == WHITE, "cliprgn");
    }

    /* --- the screen into a bitmap, and a bitmap onto the screen --- */
    SetRect(&r, 0, 0, 100, 60);
    FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
    SetRect(&r, 10, 10, 30, 20);
    FillRect(dc, &r, GetStockObject(BLACK_BRUSH));
    bm2 = CreateCompatibleBitmap(dc, W, H);
    mem2 = CreateCompatibleDC(dc);
    SelectObject(mem2, bm2);
    PatBlt(mem2, 0, 0, W, H, WHITENESS);
    SetWindowOrg(dc, 5, 5);                 /* Paintbrush's way: logical */
    BitBlt(mem2, 0, 0, 40, 30, dc, 5, 5, SRCCOPY);   /* 5, 5 is device 0, 0 */
    SetWindowOrg(dc, 0, 0);
    snapof(dc, mem2, bm2);
    check(pix(15, 15) == BLACK && pix(5, 5) == WHITE
          && pix(30, 20) == WHITE, "scr2bm");
    SetWindowOrg(dc, -40, 0);               /* device 140 is logical 100 */
    BitBlt(dc, 100, 10, W, H, mem, 0, 0, SRCCOPY);
    check(GetPixel(dc, 120, 18) == RED && GetPixel(dc, 138, 18) == BLUE
          && GetPixel(dc, 104, 14) == BLACK, "bm2scr");
    SetWindowOrg(dc, 0, 0);
    check(GetPixel(dc, 160, 18) == RED, "bm2scrdev");

    /* --- ExtFloodFill on the window's DC: a frame's inside, then the
     * whole of the client around it, the way Paintbrush's roller fills --- */
    GetClientRect(hwnd, &r);
    FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
    ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 20, 20, 60, 50);
    SelectObject(dc, green);
    check(ExtFloodFill(dc, 40, 35, WHITE, FLOODFILLSURFACE), "wfloodret");
    check(GetPixel(dc, 40, 35) == GREEN && GetPixel(dc, 21, 21) == GREEN
          && GetPixel(dc, 20, 20) == BLACK && GetPixel(dc, 70, 70) == WHITE,
          "wflood");
    SelectObject(dc, red);
    check(ExtFloodFill(dc, 150, 70, BLACK, FLOODFILLBORDER), "wfloodret2");
    check(GetPixel(dc, 150, 70) == RED && GetPixel(dc, 2, 2) == RED
          && GetPixel(dc, r.right - 1, r.bottom - 1) == RED
          && GetPixel(dc, 40, 35) == GREEN && GetPixel(dc, 20, 20) == BLACK,
          "wfloodbig");
    SelectObject(dc, ob);

    /* --- even handles: PBRUSH.DLL's own virtual DCs and bitmaps are odd --- */
    check(!((UINT)mem & 1) && !((UINT)mem2 & 1) && !((UINT)bm & 1)
          && !((UINT)bm2 & 1), "evenhandles");

    /* --- a logical palette: kept, read back, searched, and indexed by a
     * DIB_PAL_COLORS colour table through SelectPalette --- */
    {
        static struct { WORD ver, num; PALETTEENTRY e[16]; } lp;
        static struct { BITMAPINFOHEADER h; WORD ix[16]; } pb;
        static PALETTEENTRY got[16];
        static const COLORREF cols[4] = { RED, GREEN, BLUE, WHITE };
        HPALETTE hp, op;
        WORD cnt = 0;

        lp.ver = 0x300;
        lp.num = 16;
        for (k = 0; k < 16; k++) {          /* red, green, blue, white, ... */
            lp.e[k].peRed = GetRValue(cols[k & 3]);
            lp.e[k].peGreen = GetGValue(cols[k & 3]);
            lp.e[k].peBlue = GetBValue(cols[k & 3]);
        }
        hp = CreatePalette((LOGPALETTE FAR *)&lp);
        check(hp != NULL && !((UINT)hp & 1), "palmake");
        check(GetObject(hp, sizeof(cnt), &cnt) == 2 && cnt == 16, "palobj");
        check(GetPaletteEntries(hp, 0, 16, got) == 16 && got[0].peRed == 255
              && got[0].peGreen == 0 && got[2].peBlue == 255
              && got[5].peGreen == 255, "palget");
        check(GetPaletteEntries(hp, 14, 16, got) == 2
              && got[0].peBlue == 255, "palclip");
        check(GetNearestPaletteIndex(hp, RGB(0, 0, 240)) == 2, "palnear");
        op = SelectPalette(dc, hp, FALSE);
        RealizePalette(dc);
        memset(&pb, 0, sizeof(pb));
        pb.h.biSize = sizeof(BITMAPINFOHEADER);
        pb.h.biWidth = W;
        pb.h.biHeight = H;
        pb.h.biPlanes = 1;
        pb.h.biBitCount = 4;
        for (k = 0; k < 16; k++)            /* pixel k is palette entry 15 - k */
            pb.ix[k] = 15 - k;
        for (y = 0; y < H; y++)             /* left half 15 (red), right 13 (blue) */
            for (x = 0; x < STRIDE; x++)
                bits[y * STRIDE + x] = x < STRIDE / 2 ? 0xFF : 0xDD;
        SelectObject(mem, old);
        check(SetDIBits(dc, bm, 0, H, bits, (BITMAPINFO FAR *)&pb,
                        DIB_PAL_COLORS) == H, "paldibret");
        SelectObject(mem, bm);
        snap(dc);
        check(pix(5, 5) == RED && pix(50, 40) == BLUE, "paldib");
        SelectPalette(dc, op, FALSE);
        check(DeleteObject(hp), "paldel");
    }

    /* --- GMEM_NOTIFY blocks, as PBRUSH.DLL's band cache keeps them: the
     * heap runs short, the procedure is asked, and what it lets go goes --- */
    {
        static HGLOBAL hb[48];
        int nb;

        GlobalNotify((GNOTIFYPROC)NotifyProc);
        for (nb = 0; nb < 48; nb++) {
            hb[nb] = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE | GMEM_NOTIFY,
                                 16000L);
            if (hb[nb] == NULL)
                break;
        }
        check(nb == 48, "notifyroom");
        check(notified > 0 && asked != NULL
              && (GlobalFlags(asked) & GMEM_DISCARDED), "notifyasked");
        check(nb == 0 || GlobalLock(hb[nb - 1]) != NULL, "notifynewest");
        if (nb > 0)
            GlobalUnlock(hb[nb - 1]);
        for (k = 0; k < nb; k++)
            GlobalFree(hb[k]);
        GlobalNotify(NULL);
    }

    /* --- two hundred moveable handles at once --- */
    {
        static HGLOBAL hs[200];
        int nh;

        for (nh = 0; nh < 200; nh++)
            if ((hs[nh] = GlobalAlloc(GMEM_MOVEABLE, 16)) == NULL)
                break;
        check(nh == 200, "handles");
        for (k = 0; k < nh; k++)
            GlobalFree(hs[k]);
    }

    /* --- what Visual Basic 3.0's runtime imports (SPEC.md 124.25.14): the
     * origin moved by an offset, a region reset, a pen and a brush made from
     * their LOG structures, an Arc and a Pie, and the metafile calls that
     * answer 0 where no metafile exists --- */
    {
        static LOGPEN lp = { PS_SOLID, { 1, 0 }, RED };
        static LOGBRUSH lb = { BS_SOLID, BLUE, 0 };
        static HANDLETABLE ht;
        static METARECORD mr = { 3L, 0, { 0 } };
        HRGN rg;
        HPEN pn, op2;
        HBRUSH bh, ob2;
        DWORD o;
        int hit, i, j;

        SetWindowOrg(mem, 0, 0);
        o = OffsetWindowOrg(mem, 5, 7);
        check(o == 0L, "offorgret");
        check(GetWindowOrg(mem) == MAKELONG(5, 7), "offorg");
        o = OffsetWindowOrg(mem, -5, -7);
        check(o == MAKELONG(5, 7), "offorgback");
        check(GetWindowOrg(mem) == 0L, "offorg0");

        rg = CreateRectRgn(0, 0, 1, 1);
        SetRectRgn(rg, 8, 8, 24, 24);
        check(PtInRegion(rg, 10, 10) && !PtInRegion(rg, 30, 30)
              && !PtInRegion(rg, 4, 4), "setrectrgn");
        DeleteObject(rg);

        PatBlt(mem, 0, 0, W, H, WHITENESS);
        pn = CreatePenIndirect(&lp);
        check(pn != NULL, "penind");
        op2 = SelectObject(mem, pn);
        MoveTo(mem, 2, 40);
        LineTo(mem, 22, 40);
        SelectObject(mem, op2);
        DeleteObject(pn);
        lb.lbColor = BLUE;
        bh = CreateBrushIndirect(&lb);
        check(bh != NULL, "brind");
        SetRect(&r, 30, 36, 40, 44);
        FillRect(mem, &r, bh);
        DeleteObject(bh);
        lb.lbStyle = BS_HOLLOW;
        bh = CreateBrushIndirect(&lb);
        ob2 = SelectObject(mem, bh);
        op2 = SelectObject(mem, GetStockObject(NULL_PEN));
        Rectangle(mem, 30, 36, 40, 44);     /* hollow: nothing painted over */
        SelectObject(mem, op2);
        SelectObject(mem, ob2);
        DeleteObject(bh);
        snap(dc);
        check(pix(10, 40) == RED && pix(10, 38) == WHITE, "penindpix");
        check(pix(35, 40) == BLUE, "brindsolid");

        /* an Arc from 3 o'clock up to 12 o'clock: the upper right quarter of
         * the circle in the box 4..44, counter-clockwise, in the pen alone */
        PatBlt(mem, 0, 0, W, H, WHITENESS);
        SetRect(&r, 0, 0, 1, 1);
        Arc(mem, 4, 4, 44, 44, 44, 24, 24, 4);
        snap(dc);
        for (hit = 0, j = 8; j <= 11; j++)      /* (37.3, 9.7), a pixel's slack */
            for (i = 36; i <= 38; i++)
                hit += pix(i, j) == BLACK;
        check(hit > 0, "arcquarter");
        for (hit = 0, j = 36; j <= 40; j++)     /* the lower left is untouched */
            for (i = 8; i <= 12; i++)
                hit += pix(i, j) == BLACK;
        check(hit == 0, "arcother");
        check(pix(24, 24) == WHITE, "arcnofill");

        PatBlt(mem, 0, 0, W, H, WHITENESS);     /* 12 o'clock round to 3: the */
        Arc(mem, 4, 4, 44, 44, 24, 4, 44, 24);  /* long way, three quarters */
        snap(dc);
        for (hit = 0, j = 36; j <= 40; j++)
            for (i = 8; i <= 12; i++)
                hit += pix(i, j) == BLACK;
        check(hit > 0, "arclong");
        for (hit = 0, j = 8; j <= 11; j++)
            for (i = 36; i <= 38; i++)
                hit += pix(i, j) == BLACK;
        check(hit == 0, "arclongother");

        PatBlt(mem, 0, 0, W, H, WHITENESS);     /* one point twice: all of it */
        Arc(mem, 4, 4, 44, 44, 44, 24, 44, 24);
        snap(dc);
        for (hit = 0, j = 36; j <= 40; j++)
            for (i = 8; i <= 12; i++)
                hit += pix(i, j) == BLACK;
        check(hit > 0, "arcfull");

        /* a Pie of the same quarter: red inside it, black round it, and the
         * lower left left alone */
        PatBlt(mem, 0, 0, W, H, WHITENESS);
        ob2 = SelectObject(mem, red);
        Pie(mem, 4, 4, 44, 44, 44, 24, 24, 4);
        SelectObject(mem, ob2);
        snap(dc);
        check(pix(30, 16) == RED && pix(28, 20) == RED, "piefill");
        check(pix(16, 30) == WHITE && pix(40, 40) == WHITE, "pieother");
        for (hit = 0, j = 8; j <= 11; j++)
            for (i = 36; i <= 38; i++)
                hit += pix(i, j) == BLACK;
        check(hit > 0, "piearc");
        for (hit = 0, j = 10; j <= 14; j++)     /* the radius up to 12 o'clock */
            for (i = 23; i <= 25; i++)
                hit += pix(i, j) == BLACK;
        check(hit > 0, "pieradius");

        /* a ray whose reduced direction is -32768: the quarter from 9
         * o'clock down to 6 of the circle in 0..129, seen through a window
         * origin of (0, 60) - its left end, and no chord along logical row
         * 64, which the arc once drew with three quarters of the circle -
         * and a 45-degree start that once overflowed a divide */
        PatBlt(mem, 0, 0, W, H, WHITENESS);
        SetWindowOrg(mem, 0, 60);
        Arc(mem, 0, 0, 129, 129, -64, 64, 64, 200);
        SetWindowOrg(mem, 0, 0);
        snap(dc);
        for (hit = 0, j = 34; j <= 38; j++)     /* (8.6, 96), at 210 degrees */
            for (i = 7; i <= 10; i++)
                hit += pix(i, j) == BLACK;
        check(hit > 0, "arcwide");
        for (hit = 0, j = 2; j <= 6; j++)
            for (i = 24; i <= 56; i++)
                hit += pix(i, j) == BLACK;
        check(hit == 0, "arcwidechord");
        check(Arc(mem, 0, 0, 257, 257, 64, 192, 300, 0), "arcdiv");

        check(CopyMetaFile(NULL, NULL) == NULL, "copymf");
        check(!EnumMetaFile(mem, NULL, (MFENUMPROC)NULL, 0L), "enummf");
        PlayMetaFileRecord(mem, &ht, &mr, 0);
    }

    /* ...and the same Arc and Pie on the window itself, along its bottom edge
     * for a look at each adapter (the MessageBox is centred and leaves them) */
    {
        HBRUSH ob3 = SelectObject(dc, blue);

        PatBlt(dc, 4, 100, 280, 52, WHITENESS);
        Pie(dc, 8, 102, 56, 150, 56, 126, 32, 102);
        SelectObject(dc, ob3);
        Arc(dc, 100, 102, 148, 150, 124, 102, 100, 126);
        Arc(dc, 180, 102, 228, 150, 204, 126, 204, 126);
    }

    /* --- FloodFill (GDI.25) and the wide parallelogram a fast Paintbrush
     * stroke is: a horizontal top and bottom, long sides, filled in the
     * brush and outlined by the pen (SPEC.md 124.25.4, 124.25.19) --- */
    {
        static POINT pg[4] = { {10, 6}, {40, 6}, {58, 40}, {28, 40} };

        PatBlt(mem, 0, 0, W, H, WHITENESS);
        ob = SelectObject(mem, red);
        Polygon(mem, pg, 4);
        snap(dc);
        check(pix(34, 23) == RED && pix(22, 23) == RED && pix(45, 23) == RED
              && pix(30, 10) == RED && pix(44, 36) == RED, "pgfill");
        check(pix(5, 23) == WHITE && pix(55, 23) == WHITE
              && pix(5, 10) == WHITE && pix(62, 40) == WHITE, "pgout");
        check(pix(10, 6) == BLACK && pix(28, 40) == BLACK, "pgpen");
        SelectObject(mem, green);
        check(FloodFill(mem, 34, 23, BLACK), "ffret");
        snap(dc);
        check(pix(34, 23) == GREEN && pix(22, 23) == GREEN
              && pix(45, 36) == GREEN && pix(5, 23) == WHITE
              && pix(10, 6) == BLACK, "ffill");
        SelectObject(mem, ob);
    }

    SelectObject(mem2, old);
    DeleteDC(mem2);
    DeleteObject(bm2);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bm);
    DeleteObject(red);
    DeleteObject(green);
    DeleteObject(blue);
    ReleaseDC(hwnd, dc);

    MessageBox(hwnd, res[0] ? res : "MEMDC OK", "MEMDC", MB_OK);
    DestroyWindow(hwnd);
    return 0;
}
