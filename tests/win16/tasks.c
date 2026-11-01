/* tasks.c - WIN16 fixture (SPEC.md 124.28, docs/plans/WIN16-ANY-PLAN.md
 * S21): one program starting another in the same WIN16 instance. It
 * WinExecs TASKB.EXE with a command tail and an nCmdShow, then trades
 * messages with it: TASKB posts to this program's window, this program
 * posts to TASKB's task with no window (PostAppMessage), and TASKB answers.
 *
 * Its MessageBox is "TASKS OK" or names each check that failed; the row
 * win16tasks reads it off the program's stack.
 */
#include <windows.h>
#include <string.h>

#define WM_BHELLO   (WM_USER + 1)       /* TASKB -> here: it has started */
#define WM_APING    (WM_USER + 2)       /* here -> TASKB's task, no window */
#define WM_BPONG    (WM_USER + 3)       /* TASKB -> here: the ping's answer */

static char res[256];
static int fails;
static HWND me, hb;
static HTASK tb;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

static void done(void)
{
    if (fails == 0)
        strcpy(res, "TASKS OK");
    MessageBox(me, res, "TASKS", MB_OK);
    DestroyWindow(me);
}

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_BHELLO:
        check(wp == 0x1234, "tail");
        check(LOWORD(lp) == SW_SHOWNOACTIVATE, "show");
        hb = (HWND)HIWORD(lp);
        tb = GetWindowTask(hb);
        check(tb != 0 && tb != GetCurrentTask(), "wndtask");
        check(IsTask(tb), "istask");
        check(GetWindowTask(hwnd) == GetCurrentTask(), "mytask");
        check(PostAppMessage(tb, WM_APING, 0x55, 0x66778899L), "postapp");
        return 0;
    case WM_BPONG:
        check(wp == 0x55 && lp == 0x66778899L, "pong");
        done();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    UINT h;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "TASKSW";
    RegisterClass(&wc);
    me = CreateWindow("TASKSW", "Tasks", WS_OVERLAPPEDWINDOW,
                      20, 20, 240, 120, 0, 0, inst, NULL);
    ShowWindow(me, show);

    check(GetNumTasks() == 1, "one");
    check(WinExec("NOSUCH.EXE", SW_SHOWNORMAL) == 2, "nofile");
    h = WinExec("TASKB.EXE ping", SW_SHOWNOACTIVATE);
    check(h >= 32, "winexec");
    check(GetNumTasks() == 2, "two");
    if (h < 32)
        done();

    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
