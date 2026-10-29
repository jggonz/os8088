/* gdicard.c - WIN16 fixture (SPEC.md 124.7, 124.8.1, docs/plans/WIN16-PLAN.md S4):
 * the S4 TEST CARD. Run on Windows 3.0 first, so its screenshot is what WIN16
 * is held to, on each display.
 *
 *   lines 1-4  GetTextMetrics, GetTextExtent, GetDeviceCaps, GetSystemMetrics
 *              and GetSysColor, as numbers, a line apart by tmHeight
 *   keys       RoundRect with a coloured pen and SetTextColor text, the way
 *              CALC draws its keypad
 *   boxes      Rectangle, FillRect from a GetSysColor brush, a WinMine-style
 *              bevel in MoveTo/LineTo with grey and white pens, an R2_NOT line
 *              across a black box, and a row of SetPixel dots
 *   DIBs       SetDIBitsToDevice: a 4bpp DIB whole, a piece of it (WinMine's
 *              way of drawing one cell out of a strip), and a 1bpp DIB whose
 *              two colours are red and yellow
 *   extent     a hollow Rectangle one pixel outside a string's GetTextExtent
 *   colours    every GetSysColor, seven a line (the last line's two past
 *              COLOR_BTNTEXT are not colours and print whatever answers)
 *   metrics    every GetSystemMetrics, SM_CXSCREEN to SM_CYMINTRACK
 */
#include <windows.h>
#include <stdio.h>

static struct { BITMAPINFOHEADER h; RGBQUAD c[16]; } bi4 = {
    { sizeof(BITMAPINFOHEADER), 32, 16, 1, 4, BI_RGB, 0, 0, 0, 16, 0 },
    { {0,0,0,0}, {0,0,128,0}, {0,128,0,0}, {0,128,128,0},
      {128,0,0,0}, {128,0,128,0}, {128,128,0,0}, {192,192,192,0},
      {128,128,128,0}, {0,0,255,0}, {0,255,0,0}, {0,255,255,0},
      {255,0,0,0}, {255,0,255,0}, {255,255,0,0}, {255,255,255,0} }
};
static struct { BITMAPINFOHEADER h; RGBQUAD c[2]; } bi1 = {
    { sizeof(BITMAPINFOHEADER), 16, 16, 1, 1, BI_RGB, 0, 0, 0, 2, 0 },
    { {0,0,255,0}, {0,255,255,0} }      /* 0 red, 1 yellow (blue, green, red) */
};
static BYTE bits4[16 * 16], bits1[16 * 4];

static void dibs(void)
{
    int x, y;

    for (y = 0; y < 16; y++)            /* row 0 is the BOTTOM of a DIB */
        for (x = 0; x < 16; x++) {
            int c = y < 8 ? 15 - x : x; /* the top half runs the other way */
            bits4[y * 16 + x] = (BYTE)((c << 4) | c);
        }
    for (y = 0; y < 16; y++) {          /* a yellow arrow on red, pointing up */
        unsigned v = y < 8 ? ((1u << (2 * y + 2)) - 1) << (7 - y) : 0x03C0;
        bits1[(15 - y) * 4] = (BYTE)(v >> 8);
        bits1[(15 - y) * 4 + 1] = (BYTE)v;
    }
}

static void key(HDC dc, int x, int y, COLORREF c, const char *s)
{
    HPEN pen = CreatePen(PS_SOLID, 1, c), old;
    DWORD e;

    old = SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    RoundRect(dc, x, y, x + 40, y + 24, 8, 8);
    SelectObject(dc, old);
    DeleteObject(pen);
    SetTextColor(dc, c);
    e = GetTextExtent(dc, s, lstrlen(s));
    TextOut(dc, x + (40 - LOWORD(e)) / 2, y + (24 - HIWORD(e)) / 2, s, lstrlen(s));
    SetTextColor(dc, RGB(0, 0, 0));
}

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    TEXTMETRIC tm;
    HDC dc;
    HPEN grey, white;
    HBRUSH b;
    RECT r;
    DWORD e;
    char buf[64];
    int h, y0, y1, i;

    switch (msg) {
    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        GetTextMetrics(dc, &tm);
        h = tm.tmHeight;
        TextOut(dc, 4, 4, buf, sprintf(buf, "tm %d %d %d %d %d %d %d %d",
                tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading,
                tm.tmExternalLeading, tm.tmAveCharWidth, tm.tmMaxCharWidth,
                tm.tmWeight));
        e = GetTextExtent(dc, "Win Mine 2.01", 13);
        TextOut(dc, 4, 4 + h, buf, sprintf(buf, "ext %d %d caps %d %d %d %d %d",
                LOWORD(e), HIWORD(e), GetDeviceCaps(dc, HORZRES),
                GetDeviceCaps(dc, VERTRES), GetDeviceCaps(dc, BITSPIXEL),
                GetDeviceCaps(dc, PLANES), GetDeviceCaps(dc, NUMCOLORS)));
        TextOut(dc, 4, 4 + 2 * h, buf, sprintf(buf, "asp %d %d %d dpi %d %d sm %d %d %d %d",
                GetDeviceCaps(dc, ASPECTX), GetDeviceCaps(dc, ASPECTY),
                GetDeviceCaps(dc, ASPECTXY), GetDeviceCaps(dc, LOGPIXELSX),
                GetDeviceCaps(dc, LOGPIXELSY), GetSystemMetrics(SM_CYCAPTION),
                GetSystemMetrics(SM_CXBORDER), GetSystemMetrics(SM_CYMENU),
                GetSystemMetrics(SM_CXVSCROLL)));
        TextOut(dc, 4, 4 + 3 * h, buf, sprintf(buf, "sm %d %d %d %d %d col %06lx %06lx %06lx",
                GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                GetSystemMetrics(SM_CXFRAME), GetSystemMetrics(SM_CYFRAME),
                GetSystemMetrics(SM_CXICON), GetSysColor(COLOR_WINDOW),
                GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNSHADOW)));
        y0 = 8 + 4 * h;
        key(dc, 8, y0, RGB(255, 0, 0), "CE");
        key(dc, 56, y0, RGB(0, 0, 255), "7");
        SelectObject(dc, GetStockObject(GRAY_BRUSH));
        Rectangle(dc, 104, y0, 144, y0 + 24);
        SelectObject(dc, GetStockObject(WHITE_BRUSH));
        SetRect(&r, 152, y0, 192, y0 + 24);
        b = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
        FillRect(dc, &r, b);
        DeleteObject(b);
        grey = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
        white = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        SelectObject(dc, GetStockObject(LTGRAY_BRUSH));
        SelectObject(dc, GetStockObject(NULL_PEN));
        Rectangle(dc, 200, y0, 233, y0 + 25);
        SelectObject(dc, grey);
        for (i = 0; i < 2; i++) {       /* sunken, two pixels: WinMine's well */
            MoveTo(dc, 200 + i, y0 + 23 - i);
            LineTo(dc, 200 + i, y0 + i);
            LineTo(dc, 231 - i, y0 + i);
        }
        SelectObject(dc, white);
        for (i = 0; i < 2; i++) {
            MoveTo(dc, 201 + i, y0 + 23 - i);
            LineTo(dc, 231 - i, y0 + 23 - i);
            LineTo(dc, 231 - i, y0 + i);
        }
        SelectObject(dc, GetStockObject(BLACK_PEN));
        DeleteObject(grey);
        DeleteObject(white);
        SelectObject(dc, GetStockObject(BLACK_BRUSH));
        Rectangle(dc, 240, y0, 280, y0 + 24);
        SetROP2(dc, R2_NOT);
        MoveTo(dc, 236, y0 + 2);
        LineTo(dc, 284, y0 + 22);
        SetROP2(dc, R2_COPYPEN);
        for (i = 0; i < 20; i++)
            SetPixel(dc, 288 + 2 * i, y0 + 12, RGB(255, 0, 0));
        y1 = y0 + 32;
        SetDIBitsToDevice(dc, 8, y1, 32, 16, 0, 0, 0, 16, bits4,
                          (BITMAPINFO FAR *)&bi4, DIB_RGB_COLORS);
        SetDIBitsToDevice(dc, 48, y1, 8, 8, 8, 4, 0, 16, bits4,
                          (BITMAPINFO FAR *)&bi4, DIB_RGB_COLORS);
        SetDIBitsToDevice(dc, 64, y1, 16, 16, 0, 0, 0, 16, bits1,
                          (BITMAPINFO FAR *)&bi1, DIB_RGB_COLORS);
        e = GetTextExtent(dc, "Extent", 6);
        TextOut(dc, 96, y1, "Extent", 6);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 95, y1 - 1, 97 + LOWORD(e), y1 + 1 + HIWORD(e));
        SelectObject(dc, GetStockObject(WHITE_BRUSH));
        for (i = 0; i < 19; i += 7)     /* every COLOR_*, seven a line */
            TextOut(dc, 4, y1 + 24 + (i / 7) * h, buf, sprintf(buf,
                    "c%d %06lx %06lx %06lx %06lx %06lx %06lx %06lx", i,
                    GetSysColor(i), GetSysColor(i + 1), GetSysColor(i + 2),
                    GetSysColor(i + 3), GetSysColor(i + 4), GetSysColor(i + 5),
                    GetSysColor(i + 6)));
        for (i = 0; i < 36; i += 12)    /* every SM_*, twelve a line */
            TextOut(dc, 4, y1 + 24 + (3 + i / 12) * h, buf, sprintf(buf,
                    "s%d %d %d %d %d %d %d %d %d %d %d %d %d", i,
                    GetSystemMetrics(i), GetSystemMetrics(i + 1),
                    GetSystemMetrics(i + 2), GetSystemMetrics(i + 3),
                    GetSystemMetrics(i + 4), GetSystemMetrics(i + 5),
                    GetSystemMetrics(i + 6), GetSystemMetrics(i + 7),
                    GetSystemMetrics(i + 8), GetSystemMetrics(i + 9),
                    GetSystemMetrics(i + 10), GetSystemMetrics(i + 11)));
        EndPaint(hwnd, &ps);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    HWND hwnd;
    MSG m;

    dibs();
    if (!hPrev) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = hInst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "W16GDI";
        if (!RegisterClass(&wc))
            return 0;
    }
    hwnd = CreateWindow("W16GDI", "WIN16 GDI card", WS_OVERLAPPEDWINDOW,
                        8, 8, 448, 260, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
