/* bigbmp.c - WIN16 fixture (SPEC.md 124.25.17): a GDI bitmap past 64,000
 * bytes, as Paintbrush 3.0's band manager makes one.
 *
 * PBRUSH.DLL sizes its picture bands from GetFreeSpace: when it is at least
 * four times the picture it keeps the whole picture as ONE band bitmap,
 * CreateBitmap(640, 480) at 16 colours, 153,600 bytes in WIN16's 4 bits a
 * pixel, which WIN16 once refused (W16_BMMAX) and Paintbrush answered with
 * "Insufficient memory for this operation". Here a 640 x 480 bitmap is made,
 * selected into a memory DC and drawn on in each of the three tiles WIN16
 * keeps it in (204 rows apiece), with a stripe across the seam between the
 * first two; every pixel is read back through GetPixel, a block of the last
 * tile is BitBlt'd onto the window and read there, a scan of it is read
 * through GetDIBits and another written through SetDIBits, a StretchBlt of
 * the seam goes to the window, and the bitmap is deleted. Before that: a
 * line and a flood fill across the seam, a block from an odd column (the
 * band's path), a pattern brush made from it, and copies into a second
 * tiled bitmap whose tiles are a different height (500 x 400, 262 rows)
 * and into a monochrome one past 64,000 bytes, each read back.
 *
 * Every check that fails is named in the message box, which
 * tests/win16bigbmp.py reads; "BIGBMP OK" when none does.
 */
#include <windows.h>
#include <string.h>

#define BLACK RGB(0, 0, 0)
#define WHITE RGB(255, 255, 255)
#define RED   RGB(255, 0, 0)
#define GREEN RGB(0, 255, 0)
#define BLUE  RGB(0, 0, 255)

static char res[256];
static struct { BITMAPINFOHEADER h; RGBQUAD c[16]; } bi;
static BYTE scan[640 / 2 + 4];

static void check(int ok, const char *tag)
{
    if (!ok) {
        if (res[0] == 0)
            lstrcpy(res, "BIGBMP failed:");
        if (lstrlen(res) < sizeof(res) - 12) {
            lstrcat(res, " ");
            lstrcat(res, tag);
        }
    }
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
    WNDCLASS wc;
    HWND hwnd;
    HDC dc, bd;
    HBITMAP big, bold, two, mono, tbold;
    HBRUSH red, blue, green, pat;
    HDC td;
    HPEN rpen, open_;
    RECT rc;
    int i;

    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.lpszClassName = "BigBmp";
        RegisterClass(&wc);
    }
    hwnd = CreateWindow("BigBmp", "BIGBMP", WS_OVERLAPPEDWINDOW, 20, 20, 340,
                        200, NULL, NULL, inst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    dc = GetDC(hwnd);
    red = CreateSolidBrush(RED);
    blue = CreateSolidBrush(BLUE);
    green = CreateSolidBrush(GREEN);

    big = CreateCompatibleBitmap(dc, 640, 480);
    check(big != NULL && !((UINT)big & 1), "make");
    bd = CreateCompatibleDC(dc);
    bold = SelectObject(bd, big);
    check(bold != NULL, "select");
    PatBlt(bd, 0, 0, 640, 480, WHITENESS);
    SetRect(&rc, 10, 10, 50, 50);
    FillRect(bd, &rc, red);
    SetRect(&rc, 0, 200, 640, 211);
    FillRect(bd, &rc, red);
    SetPixel(bd, 300, 300, GREEN);
    SetRect(&rc, 100, 440, 164, 480);
    FillRect(bd, &rc, blue);
    check(GetPixel(bd, 20, 20) == RED && GetPixel(bd, 60, 20) == WHITE,
          "tile0");
    check(GetPixel(bd, 5, 203) == RED && GetPixel(bd, 5, 204) == RED
          && GetPixel(bd, 5, 199) == WHITE && GetPixel(bd, 5, 211) == WHITE
          && GetPixel(bd, 639, 210) == RED, "seam");
    check(GetPixel(bd, 300, 300) == GREEN && GetPixel(bd, 301, 300) == WHITE
          && GetPixel(bd, 300, 299) == WHITE, "tile1");
    check(GetPixel(bd, 110, 450) == BLUE && GetPixel(bd, 639, 479) == WHITE
          && GetPixel(bd, 99, 450) == WHITE && GetPixel(bd, 164, 450) == WHITE,
          "tile2");

    BitBlt(dc, 230, 2, 64, 40, bd, 100, 440, SRCCOPY);
    check(GetPixel(dc, 232, 4) == BLUE && GetPixel(dc, 290, 40) == BLUE,
          "bitblt");
    /* a source that starts in the first tile and ends in the second */
    PatBlt(dc, 4, 2, 100, 100, WHITENESS);
    BitBlt(dc, 4, 2, 60, 20, bd, 0, 195, SRCCOPY);
    check(GetPixel(dc, 10, 2 + 5) == RED && GetPixel(dc, 10, 2 + 4) == WHITE
          && GetPixel(dc, 10, 2 + 9) == RED && GetPixel(dc, 10, 2 + 15) == RED
          && GetPixel(dc, 10, 2 + 16) == WHITE, "bltseam");

    /* a line down column 400 across the first seam (rows 150..260) */
    rpen = CreatePen(PS_SOLID, 1, RED);
    open_ = SelectObject(bd, rpen);
    MoveTo(bd, 400, 150);
    LineTo(bd, 400, 261);
    SelectObject(bd, open_);
    DeleteObject(rpen);
    /* (off the stripe, rows 200..210: tile 0 above it, tile 1 below) */
    check(GetPixel(bd, 400, 150) == RED && GetPixel(bd, 400, 199) == RED
          && GetPixel(bd, 400, 211) == RED && GetPixel(bd, 400, 260) == RED
          && GetPixel(bd, 400, 261) == WHITE && GetPixel(bd, 401, 230) == WHITE,
          "line");
    /* a red box round rows 380..439 x 500..559, filled green inside: the
     * fill's reads and writes cross the second seam (row 408) */
    SetRect(&rc, 500, 380, 560, 381);
    FillRect(bd, &rc, red);
    SetRect(&rc, 500, 439, 560, 440);
    FillRect(bd, &rc, red);
    SetRect(&rc, 500, 380, 501, 440);
    FillRect(bd, &rc, red);
    SetRect(&rc, 559, 380, 560, 440);
    FillRect(bd, &rc, red);
    open_ = SelectObject(bd, green);
    ExtFloodFill(bd, 530, 390, RED, FLOODFILLBORDER);
    SelectObject(bd, open_);
    check(GetPixel(bd, 530, 390) == GREEN && GetPixel(bd, 530, 430) == GREEN
          && GetPixel(bd, 502, 438) == GREEN && GetPixel(bd, 530, 445) == WHITE
          && GetPixel(bd, 499, 400) == WHITE && GetPixel(bd, 500, 400) == RED,
          "flood");
    /* from an odd column, across the seam: the band's path to the screen */
    PatBlt(dc, 4, 2, 100, 100, WHITENESS);
    BitBlt(dc, 4, 2, 21, 20, bd, 39, 195, SRCCOPY);
    check(GetPixel(dc, 4, 2) == WHITE && GetPixel(dc, 4 + 10, 2 + 4) == WHITE
          && GetPixel(dc, 4 + 10, 2 + 5) == RED
          && GetPixel(dc, 4 + 10, 2 + 15) == RED
          && GetPixel(dc, 4 + 10, 2 + 16) == WHITE, "oddblt");
    /* a pattern brush from it: rows 0..7, in the first tile */
    pat = CreatePatternBrush(big);
    check(pat != NULL, "patbrush");
    if (pat)
        DeleteObject(pat);

    /* a second tiled bitmap, 262 rows a tile: copies between the two cross
     * both bitmaps' seams at different rows */
    two = CreateBitmap(500, 400, 1, 4, NULL);
    check(two != NULL, "make2");
    td = CreateCompatibleDC(dc);
    tbold = SelectObject(td, two);
    PatBlt(td, 0, 0, 500, 400, WHITENESS);
    BitBlt(td, 0, 190, 500, 90, bd, 0, 150, SRCCOPY);
    /* two's row r is big's r - 40: the stripe is rows 240..250 there */
    check(GetPixel(td, 5, 239) == WHITE && GetPixel(td, 5, 240) == RED
          && GetPixel(td, 5, 250) == RED && GetPixel(td, 5, 251) == WHITE
          && GetPixel(td, 400, 262) == RED && GetPixel(td, 400, 261) == RED
          && GetPixel(td, 400, 279) == RED && GetPixel(td, 401, 262) == WHITE,
          "bm2bm");
    /* and back: two's rows 250..275 (its seam at 262) onto big's 400.. */
    BitBlt(bd, 0, 400, 500, 26, td, 0, 250, SRCCOPY);
    check(GetPixel(bd, 400, 400) == RED && GetPixel(bd, 400, 425) == RED
          && GetPixel(bd, 401, 412) == WHITE, "bm2bm2");
    SelectObject(td, tbold);
    check(DeleteObject(two), "delete2");

    /* a monochrome bitmap past 64,000 bytes (WIN16 keeps it 4bpp: 102,400) */
    mono = CreateBitmap(1024, 200, 1, 1, NULL);
    check(mono != NULL, "makemono");
    tbold = SelectObject(td, mono);
    PatBlt(td, 0, 0, 1024, 200, WHITENESS);
    SetRect(&rc, 1000, 120, 1010, 140);
    FillRect(td, &rc, GetStockObject(BLACK_BRUSH));
    check(GetPixel(td, 1005, 130) == BLACK && GetPixel(td, 1005, 119) == WHITE
          && GetPixel(td, 1011, 130) == WHITE, "mono");
    SelectObject(td, tbold);
    DeleteDC(td);
    check(DeleteObject(mono), "deletemono");

    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = 640;
    bi.h.biHeight = 480;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 4;
    SelectObject(bd, bold);
    check(GetDIBits(dc, big, 479 - 450, 1, scan, (BITMAPINFO FAR *)&bi,
                    DIB_RGB_COLORS) == 1
          && bi.c[scan[50] >> 4].rgbBlue == 255
          && bi.c[scan[50] >> 4].rgbRed == 0
          && bi.c[scan[49] >> 4].rgbRed == 255, "getdib");
    /* a scan of green written into row 474 (the third tile) as Paintbrush
     * writes its bands: the header describes the whole DIB, the bits are
     * the one scan, and the colour table is as GetDIBits answered it */
    for (i = 0; i < 640 / 2; i++)
        scan[i] = 0;
    for (i = 0; i < 16; i++)
        if (bi.c[i].rgbGreen == 255 && bi.c[i].rgbRed == 0
            && bi.c[i].rgbBlue == 0)
            break;
    check(i < 16, "greenidx");
    if (i < 16) {
        int k;

        for (k = 0; k < 640 / 2; k++)
            scan[k] = (BYTE)((i << 4) | i);
        check(SetDIBits(dc, big, 479 - 474, 1, scan, (BITMAPINFO FAR *)&bi,
                        DIB_RGB_COLORS) == 1, "setdibret");
        SelectObject(bd, big);
        check(GetPixel(bd, 0, 474) == GREEN && GetPixel(bd, 639, 474) == GREEN
              && GetPixel(bd, 5, 473) == WHITE && GetPixel(bd, 5, 475) == WHITE,
              "setdib");
        SelectObject(bd, bold);
    }
    SelectObject(bd, big);
    /* the seam, stretched onto the window */
    StretchBlt(dc, 100, 100, 120, 44, bd, 0, 190, 60, 22, SRCCOPY);
    check(GetPixel(dc, 110, 100 + 2 * 12) == RED, "stretch");
    SelectObject(bd, bold);
    DeleteDC(bd);
    check(DeleteObject(big), "delete");
    DeleteObject(red);
    DeleteObject(blue);
    DeleteObject(green);
    ReleaseDC(hwnd, dc);

    MessageBox(hwnd, res[0] ? res : "BIGBMP OK", "BIGBMP", MB_OK);
    DestroyWindow(hwnd);
    return 0;
}
