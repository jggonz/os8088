/* bigbmp.c - WIN16 fixture (SPEC.md 124.25.16): a GDI bitmap past 64,000
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
 * the seam goes to the window, and the bitmap is deleted.
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
    HBITMAP big, bold;
    HBRUSH red, blue;
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
    ReleaseDC(hwnd, dc);

    MessageBox(hwnd, res[0] ? res : "BIGBMP OK", "BIGBMP", MB_OK);
    DestroyWindow(hwnd);
    return 0;
}
