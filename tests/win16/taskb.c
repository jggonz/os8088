/* taskb.c - WIN16 fixture (SPEC.md 124.28): the program TASKS.EXE starts.
 * It finds TASKS's window, says hello with what it was given (its command
 * tail, its nCmdShow and its own window), and answers the message TASKS
 * posts to its task with no window.
 */
#include <windows.h>
#include <string.h>

#define WM_BHELLO   (WM_USER + 1)
#define WM_APING    (WM_USER + 2)
#define WM_BPONG    (WM_USER + 3)

static HWND a;

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
    MSG m;
    HWND me;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "TASKBW";
    RegisterClass(&wc);
    me = CreateWindow("TASKBW", "Task B", WS_OVERLAPPEDWINDOW,
                      60, 90, 200, 80, 0, 0, inst, NULL);
    ShowWindow(me, show);

    while (*cmd == ' ')
        cmd++;
    a = FindWindow("TASKSW", NULL);
    PostMessage(a, WM_BHELLO, lstrcmp(cmd, "ping") == 0 ? 0x1234 : 0,
                MAKELONG(show, me));

    while (GetMessage(&m, 0, 0, 0)) {
        if (m.hwnd == 0 && m.message == WM_APING)
            PostMessage(a, WM_BPONG, m.wParam, m.lParam);
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
