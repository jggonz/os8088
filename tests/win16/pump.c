/* pump.c - WIN16 fixture (SPEC.md 124.8, docs/plans/WIN16-PLAN.md S3): the
 * rest of S3's message pump that window.c does not reach.
 *
 *   start   created WS_MINIMIZE, then MoveWindow - WinMine's start
 *   idle    a PeekMessage loop that counts the turns it found nothing; on
 *           Windows 3.x and here alike, an empty PeekMessage YIELDS
 *   size    WM_SIZE's client size beside GetClientRect's
 *   m / s   MoveWindow / SetWindowPos to a new size
 *   p       PostMessage(WM_USER + 1) to itself: "posted" counts them
 *   sent    SendMessage(WM_USER) at start; the window answers 7
 *   press   SetCapture on the button, ReleaseCapture on its release
 *   ms      GetTickCount since WM_CREATE, once a second (SetTimer 1000)
 */
#include <windows.h>
#include <stdio.h>

static int wcx, wcy, posted, sent, captured;
static long idle;
static DWORD born;
static RECT strip = { 8, 8, 280, 104 };

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HDC dc;
    RECT rc;
    char buf[48];

    switch (msg) {
    case WM_CREATE:
        born = GetTickCount();
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    case WM_SIZE:
        wcx = LOWORD(lp);
        wcy = HIWORD(lp);
        return 0;
    case WM_USER:
        return 7;
    case WM_USER + 1:
        posted++;
        InvalidateRect(hwnd, &strip, TRUE);
        return 0;
    case WM_TIMER:
        InvalidateRect(hwnd, &strip, TRUE);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        captured = 1;
        InvalidateRect(hwnd, &strip, TRUE);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        captured = 0;
        InvalidateRect(hwnd, &strip, TRUE);
        return 0;
    case WM_CHAR:
        if (wp == 'm')
            MoveWindow(hwnd, 60, 60, 260, 140, TRUE);
        else if (wp == 's')
            SetWindowPos(hwnd, 0, 0, 0, 320, 180, SWP_NOMOVE | SWP_NOZORDER);
        else if (wp == 'p')
            PostMessage(hwnd, WM_USER + 1, 0, 0);
        return 0;
    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &rc);
        TextOut(dc, 8, 8, buf, sprintf(buf, "size %d x %d client %d x %d",
                                       wcx, wcy, rc.right, rc.bottom));
        TextOut(dc, 8, 24, buf, sprintf(buf, "idle %ld", idle));
        TextOut(dc, 8, 40, buf, sprintf(buf, "sent %d posted %d", sent, posted));
        TextOut(dc, 8, 56, buf, sprintf(buf, "capture %d", captured));
        TextOut(dc, 8, 72, buf, sprintf(buf, "ms %lu", GetTickCount() - born));
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
        wc.lpszClassName = "W16PUMP";
        if (!RegisterClass(&wc))
            return 0;
    }
    hwnd = CreateWindow("W16PUMP", "WIN16 pump",
                        WS_OVERLAPPEDWINDOW | WS_MINIMIZE,
                        40, 40, 200, 100, NULL, NULL, hInst, NULL);
    MoveWindow(hwnd, 40, 40, 300, 160, FALSE);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    sent = (int)SendMessage(hwnd, WM_USER, 0, 0);
    UpdateWindow(hwnd);
    for (;;) {
        if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT)
                break;
            TranslateMessage(&m);
            DispatchMessage(&m);
        } else {
            idle++;
        }
    }
    return m.wParam;
}
