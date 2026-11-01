/* taskb.c - WIN16 fixture (SPEC.md 124.28): the program TASKS.EXE starts.
 * It finds TASKS's window, says hello with what it was given (its command
 * tail, its nCmdShow and its own window), answers the message TASKS posts
 * to its task with no window, and answers a send from TASKS after sending
 * TASKS one of its own. TASKS closes it.
 *
 * Started with "last", it is the second TASKB and TASKS is ending: it waits
 * for TASKS to be gone and puts up the MessageBox the row reads, "TASKS OK"
 * or the checks of its own that failed.
 */
#include <windows.h>
#include <string.h>

#define WM_BHELLO   (WM_USER + 1)
#define WM_APING    (WM_USER + 2)
#define WM_BPONG    (WM_USER + 3)
#define WM_ASEND    (WM_USER + 4)
#define WM_BBACK    (WM_USER + 5)

static HWND a, me;
static int last, waits;
static char res[64];

static void lastcheck(void)
{
    int fails = 0;

    res[0] = 0;
    if (GetNumTasks() != 1) { fails++; strcat(res, " alone"); }
    if (IsWindow(a)) { fails++; strcat(res, " itswnd"); }
    if (GetWindowTask(me) != GetCurrentTask()) { fails++; strcat(res, " mine"); }
    if (fails == 0)
        strcpy(res, "TASKS OK");
    MessageBox(me, res, "TASKS", MB_OK);
    DestroyWindow(me);
}

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER && last) {
        if (GetNumTasks() == 1 || ++waits == 20) {
            KillTimer(hwnd, 1);
            lastcheck();
        }
        return 0;
    }
    if (msg == WM_ASEND)
        return wp * 6 + SendMessage(a, WM_BBACK, 0, 0);
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
    if (lstrcmp(cmd, "last") == 0) {
        last = 1;
        SetTimer(me, 1, 200, NULL);
    } else
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
