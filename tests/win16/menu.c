/* menu.c - WIN16 fixture (SPEC.md 124.9-124.11, docs/plans/WIN16-PLAN.md S5):
 * a menu resource with popups, a NESTED popup, a separator, a CHECKED item, a
 * GRAYED item and a tab-separated shortcut; an accelerator table (F2, and
 * Ctrl+B as an ASCII key); a string table across two blocks; and the global
 * and local heaps. Choosing a colour moves the check mark (CheckMenuItem) and
 * fills the window with it; F2 and File > New count "new"s. The window shows
 * what each call answered, so Windows 3.0's screenshot is the reference.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "menu.h"

static int colour = IDM_RED, news, inits, pops;
static char s1[40], s2[40], mem[48];

static void memtest(void)
{
    HANDLE g, l;
    char far *gp;
    char *lp;
    int ok = 0;

    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 3000);
    if (g) {
        gp = GlobalLock(g);
        if (gp && gp[2999] == 0) {
            _fstrcpy(gp, "global");
            GlobalUnlock(g);
            g = GlobalReAlloc(g, 9000, GMEM_MOVEABLE);
            gp = g ? GlobalLock(g) : NULL;
            if (gp && _fstrcmp(gp, "global") == 0 && GlobalSize(g) >= 9000)
                ok |= 1;
            if (g) {
                GlobalUnlock(g);
                GlobalFree(g);
            }
        }
    }
    l = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, 200);
    if (l) {
        lp = LocalLock(l);
        if (lp && lp[199] == 0) {
            strcpy(lp, "local");
            LocalUnlock(l);
            l = LocalReAlloc(l, 600, LMEM_MOVEABLE);
            lp = l ? LocalLock(l) : NULL;
            if (lp && strcmp(lp, "local") == 0 && LocalSize(l) >= 600)
                ok |= 2;
            if (l) {
                LocalUnlock(l);
                LocalFree(l);
            }
        }
    }
    sprintf(mem, "global %s, local %s", ok & 1 ? "ok" : "BAD", ok & 2 ? "ok" : "BAD");
}

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    RECT r;
    HDC dc;
    HBRUSH b;
    char buf[48];

    switch (msg) {
    case WM_INITMENU:
        inits++;
        return 0;
    case WM_INITMENUPOPUP:
        pops++;
        return 0;
    case WM_COMMAND:
        switch (wp) {
        case IDM_NEW:
            news++;
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        case IDM_RED: case IDM_GREEN: case IDM_BLUE:
        case IDM_CYAN: case IDM_MAGENTA:
            CheckMenuItem(GetMenu(hwnd), colour, MF_UNCHECKED);
            colour = wp;
            CheckMenuItem(GetMenu(hwnd), colour, MF_CHECKED);
            break;
        }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &r);
        TextOut(dc, 4, 2, buf, sprintf(buf, "new %d  init %d  popup %d", news, inits, pops));
        TextOut(dc, 4, 18, buf, sprintf(buf, "client %d x %d", r.right, r.bottom));
        TextOut(dc, 4, 34, s1, strlen(s1));
        TextOut(dc, 4, 50, s2, strlen(s2));
        TextOut(dc, 4, 66, mem, strlen(mem));
        r.top = 86;
        b = CreateSolidBrush(colour == IDM_RED ? RGB(255, 0, 0) :
                             colour == IDM_GREEN ? RGB(0, 255, 0) :
                             colour == IDM_BLUE ? RGB(0, 0, 255) :
                             colour == IDM_CYAN ? RGB(0, 255, 255) : RGB(255, 0, 255));
        FillRect(dc, &r, b);
        DeleteObject(b);
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
    LoadString(hInst, IDS_HELLO, s1, sizeof s1);
    LoadString(hInst, IDS_SEVENTEEN, s2, 10);       /* cut to 9 and a NUL */
    memtest();
    acc = LoadAccelerators(hInst, "W16ACCEL");
    hwnd = CreateWindow("W16MENU", "WIN16 menu", WS_OVERLAPPEDWINDOW,
                        40, 40, 280, 200, NULL, NULL, hInst, NULL);
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
