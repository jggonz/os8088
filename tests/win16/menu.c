/* menu.c - WIN16 fixture (SPEC.md 124, docs/plans/WIN16-PLAN.md S5): a menu
 * resource with popups, a separator, a CHECKED item, a GRAYED item and a
 * tab-separated shortcut, plus an accelerator table. Choosing a colour moves
 * the check mark (CheckMenuItem) and fills the window with it; F2 (the
 * accelerator) and File > New count "new"s. This is where S5's in-window
 * menu bar and its check glyph (D8) are compared against Windows'.
 */
#include <windows.h>
#include <stdio.h>
#include "menu.h"

static int colour = IDM_RED, news;

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    RECT r;
    HDC dc;
    HBRUSH b;
    char buf[32];

    switch (msg) {
    case WM_COMMAND:
        switch (wp) {
        case IDM_NEW:
            news++;
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        case IDM_RED: case IDM_GREEN: case IDM_BLUE:
            CheckMenuItem(GetMenu(hwnd), colour, MF_UNCHECKED);
            colour = wp;
            CheckMenuItem(GetMenu(hwnd), colour, MF_CHECKED);
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }
        return 0;
    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &r);
        r.top = 30;
        b = CreateSolidBrush(colour == IDM_RED ? RGB(255, 0, 0) :
                             colour == IDM_GREEN ? RGB(0, 255, 0) : RGB(0, 0, 255));
        FillRect(dc, &r, b);
        DeleteObject(b);
        TextOut(dc, 8, 8, buf, sprintf(buf, "new %d", news));
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
    HACCEL acc;
    MSG m;

    if (!hPrev) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = hInst;
        wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.lpszMenuName = "W16MENU";
        wc.lpszClassName = "W16MENU";
        if (!RegisterClass(&wc))
            return 0;
    }
    acc = LoadAccelerators(hInst, "W16ACCEL");
    hwnd = CreateWindow("W16MENU", "WIN16 menu", WS_OVERLAPPEDWINDOW,
                        40, 40, 260, 160, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessage(&m, NULL, 0, 0)) {
        if (!TranslateAccelerator(hwnd, acc, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    return m.wParam;
}
