/* bitmap.c - WIN16 fixture (SPEC.md 124, docs/plans/WIN16-PLAN.md S4/S8):
 * bitmap resources drawn the way WinMine draws its tiles - LoadBitmap,
 * CreateCompatibleDC, SelectObject, BitBlt with SRCCOPY - plus one
 * StretchBlt at twice the size and the monochrome twin blitted through a
 * colour DC, which is where Windows maps 1 to the background colour.
 */
#include <windows.h>

static HBITMAP colour, mono;

static void blit(HDC dc, HBITMAP bm, int x, int y, int scale)
{
    HDC m = CreateCompatibleDC(dc);
    HBITMAP old = SelectObject(m, bm);
    BITMAP b;

    GetObject(bm, sizeof b, &b);
    if (scale == 1)
        BitBlt(dc, x, y, b.bmWidth, b.bmHeight, m, 0, 0, SRCCOPY);
    else
        StretchBlt(dc, x, y, b.bmWidth * scale, b.bmHeight * scale,
                   m, 0, 0, b.bmWidth, b.bmHeight, SRCCOPY);
    SelectObject(m, old);
    DeleteDC(m);
}

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HDC dc;

    switch (msg) {
    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        blit(dc, colour, 8, 8, 1);
        blit(dc, colour, 48, 8, 2);
        blit(dc, mono, 120, 8, 1);
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

    colour = LoadBitmap(hInst, "W16COLOR");
    mono = LoadBitmap(hInst, "W16MONO");
    if (!hPrev) {
        wc.style = 0;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = hInst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(LTGRAY_BRUSH);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "W16BMP";
        if (!RegisterClass(&wc))
            return 0;
    }
    hwnd = CreateWindow("W16BMP", "WIN16 bitmap", WS_OVERLAPPEDWINDOW,
                        40, 40, 200, 130, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    DeleteObject(colour);
    DeleteObject(mono);
    return m.wParam;
}
