/* window.c - WIN16 fixture (SPEC.md 124, docs/plans/WIN16-PLAN.md S3/S4):
 * a registered class, one top-level window, a paint, a timer, the mouse and
 * the keyboard. Every message S3 lists reaches WndProc here, and what it
 * paints is S4's first picture: a frame, a filled red box, a grey box, a
 * diagonal line and three lines of System-font text.
 *
 *   ticks  counts WM_TIMER (500 ms), repainting only the counter's strip
 *   click  counts WM_LBUTTONDOWN and shows where
 *   key    shows the last WM_CHAR
 */
#include <windows.h>
#include <stdio.h>

static int ticks, clicks, cx, cy;
static char last = '-';
static RECT strip = { 8, 8, 200, 60 };

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HDC dc;
    HBRUSH red;
    char buf[48];

    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 500, NULL);
        return 0;
    case WM_TIMER:
        ticks++;
        InvalidateRect(hwnd, &strip, TRUE);
        return 0;
    case WM_LBUTTONDOWN:
        clicks++;
        cx = LOWORD(lp);
        cy = HIWORD(lp);
        InvalidateRect(hwnd, &strip, TRUE);
        return 0;
    case WM_CHAR:
        last = (char)wp;
        InvalidateRect(hwnd, &strip, TRUE);
        return 0;
    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        TextOut(dc, 8, 8, buf, sprintf(buf, "ticks %d", ticks));
        TextOut(dc, 8, 24, buf, sprintf(buf, "clicks %d at %d,%d", clicks, cx, cy));
        TextOut(dc, 8, 40, buf, sprintf(buf, "key %c", last));
        Rectangle(dc, 8, 70, 120, 150);
        red = CreateSolidBrush(RGB(255, 0, 0));
        SelectObject(dc, red);
        Rectangle(dc, 16, 78, 64, 126);
        SelectObject(dc, GetStockObject(GRAY_BRUSH));
        Rectangle(dc, 70, 78, 112, 126);
        SelectObject(dc, GetStockObject(WHITE_BRUSH));
        DeleteObject(red);
        MoveTo(dc, 130, 70);
        LineTo(dc, 230, 150);
        EndPaint(hwnd, &ps);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
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

    if (!hPrev) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = hInst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "W16WIN";
        if (!RegisterClass(&wc))
            return 0;
    }
    hwnd = CreateWindow("W16WIN", "WIN16 window", WS_OVERLAPPEDWINDOW,
                        40, 40, 260, 200, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
