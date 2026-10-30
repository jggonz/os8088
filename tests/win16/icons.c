/* icons.c - WIN16 fixture (SPEC.md 124.18.4, docs/plans/WIN16-PLAN.md S12):
 * the icon calls. Its window draws the five system icons and its own with
 * DrawIcon over two strips of colour and white, so what is see-through shows
 * what it was drawn on: the third system icon straddles the strips' edge.
 * Keys open a message box with each icon - h hand, q question, e
 * exclamation, a asterisk, n none - and d the About dialog, whose SS_ICON
 * is the program's own. Built by `make win16fixtures` with Open Watcom.
 */
#include <windows.h>
#include "icons.h"

static HINSTANCE hi;
static HICON own;
static LPCSTR sys[5];

BOOL __export CALLBACK AboutProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        EndDialog(dlg, wp);
        return TRUE;
    }
    return FALSE;
}

static void box(HWND w, UINT icon, LPCSTR what)
{
    MessageBox(w, what, "Icons", MB_OK | icon);
}

LRESULT __export CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HDC dc;
    RECT r;
    HBRUSH b;
    FARPROC fp;
    int i;

    switch (msg) {
    case WM_PAINT:
        dc = BeginPaint(w, &ps);
        SetRect(&r, 0, 0, 100, 48);
        b = CreateSolidBrush(RGB(0, 128, 128));
        FillRect(dc, &r, b);
        DeleteObject(b);
        SetRect(&r, 100, 0, 240, 48);
        b = CreateSolidBrush(RGB(255, 255, 0));
        FillRect(dc, &r, b);
        DeleteObject(b);
        for (i = 0; i < 5; i++)
            DrawIcon(dc, 8 + i * 40, 8, LoadIcon(NULL, sys[i]));
        DrawIcon(dc, 8, 64, own);
        DrawIcon(dc, 200, 64, own);
        EndPaint(w, &ps);
        return 0;
    case WM_CHAR:
        switch (wp) {
        case 'h': box(w, MB_ICONHAND, "The hand: a stop sign."); break;
        case 'q': box(w, MB_ICONQUESTION, "The question mark."); break;
        case 'e': box(w, MB_ICONEXCLAMATION, "The exclamation mark."); break;
        case 'a': box(w, MB_ICONASTERISK, "The asterisk: information."); break;
        case 'n': box(w, 0, "No icon at all."); break;
        case 'd':
            fp = MakeProcInstance((FARPROC)AboutProc, hi);
            DialogBox(hi, MAKEINTRESOURCE(IDD_ABOUT), w, (DLGPROC)fp);
            FreeProcInstance(fp);
            break;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND w;

    hi = inst;
    sys[0] = IDI_APPLICATION;
    sys[1] = IDI_HAND;
    sys[2] = IDI_QUESTION;
    sys[3] = IDI_EXCLAMATION;
    sys[4] = IDI_ASTERISK;
    own = LoadIcon(inst, "DIAMOND");
    if (!prev) {
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = inst;
        wc.hIcon = own;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "W16Icons";
        if (!RegisterClass(&wc))
            return 0;
    }
    w = CreateWindow("W16Icons", "Icons", WS_OVERLAPPEDWINDOW,
                     40, 40, 260, 140, NULL, NULL, inst, NULL);
    ShowWindow(w, show);
    UpdateWindow(w);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
