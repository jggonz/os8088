/* pmdde.c - WIN16 fixture (SPEC.md 124.33.2, docs/plans/WIN16-ANY-PLAN.md
 * S24): a setup program's last act, talking to Program Manager over DDE.
 * WM_DDE_INITIATE to HWND_BROADCAST for PROGMAN|PROGMAN must be answered
 * inside the send by a window whose class is "Progman". Then four blocks of
 * commands, each posted as WM_DDE_EXECUTE and acknowledged with
 * WM_DDE_ACK:
 *   1. [CreateGroup(PMDDE Test)][ShowGroup(PMDDE Test,1)]
 *      [AddItem(B:\PMDDE.EXE,PM Test)]
 *      [AddItem(NOTEPAD.EXE B:\README.TXT,"Read Me")]   - positive;
 *   2. [Bogus(1)]                                        - negative;
 *   3. [DeleteItem(Read Me)]                             - positive;
 *   4. [CreateGroup(PMDDE Gone)][DeleteGroup(PMDDE Gone)] - positive.
 * Then a WM_DDE_REQUEST, which is acknowledged negatively, and
 * WM_DDE_TERMINATE both ways. After its MessageBox it calls
 * ExitWindows(EW_RESTARTWINDOWS), as a setup does, which ends it.
 *
 * What it leaves on WIN16's volume, B: (the row win16pmdde reads it back):
 * SYSTEM\APPDATA\WIN16\GROUPS\PMDDETES\PMTEST.LNK naming B:\PMDDE.EXE, no
 * README.LNK, no PMDDEGON folder, and a desktop shortcut to PMDDETES.
 *
 * Its MessageBox is "PMDDE OK" or names each check that failed.
 */
#include <windows.h>
#include <dde.h>
#include <string.h>

static char res[256];
static int fails, step, acks;
static HWND me, server;
static ATOM apm, agroups;
static HGLOBAL hcmd;

static const char *blocks[] = {
    "[CreateGroup(PMDDE Test)][ShowGroup(PMDDE Test,1)]\r\n"
    "[AddItem(B:\\PMDDE.EXE,PM Test)]"
    "[AddItem(NOTEPAD.EXE B:\\README.TXT,\"Read Me\")]",
    "[Bogus(1)]",
    "[DeleteItem(Read Me)]",
    " [CreateGroup(PMDDE Gone)] [DeleteGroup( PMDDE Gone )]"
};
static const char *tags[] = { "exec1", "bogus", "delitem", "delgroup" };

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

static void execute(HWND hwnd, const char *s)
{
    LPSTR p;

    hcmd = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, lstrlen(s) + 1);
    p = GlobalLock(hcmd);
    lstrcpy(p, s);
    GlobalUnlock(hcmd);
    check(PostMessage(server, WM_DDE_EXECUTE, (WPARAM)hwnd,
                      MAKELONG(0, hcmd)), "post");
}

static void done(void)
{
    KillTimer(me, 1);
    MessageBox(me, fails ? res : "PMDDE OK", "PMDDE", MB_OK);
    /* a setup's last word: WIN16 ends it, with a toast that no restart is
     * needed (SPEC.md 124.33.3) */
    ExitWindows(MAKELONG(EW_RESTARTWINDOWS, 0), 0);
    DestroyWindow(me);
}

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DDE_ACK:
        if (step == 0) {                /* the INITIATE's, inside the send */
            acks++;
            server = (HWND)wp;
            check(LOWORD(lp) == apm && HIWORD(lp) == apm, "ackatoms");
            return 0;
        }
        check((HWND)wp == server, "ackfrom");
        if (step <= 4) {                /* an EXECUTE's */
            int ok = (LOWORD(lp) & 0x8000) != 0;

            check(step == 2 ? !ok : ok, tags[step - 1]);
            check((HGLOBAL)HIWORD(lp) == hcmd, "hcmd");
            GlobalFree(hcmd);
            if (++step <= 4)
                execute(hwnd, blocks[step - 1]);
            else
                check(PostMessage(server, WM_DDE_REQUEST, (WPARAM)hwnd,
                                  MAKELONG(CF_TEXT, agroups)), "request");
            return 0;
        }
        if (step == 5) {                /* the REQUEST's: not served */
            check((LOWORD(lp) & 0x8000) == 0 && HIWORD(lp) == agroups,
                  "reqack");
            step = 6;
            PostMessage(server, WM_DDE_TERMINATE, (WPARAM)hwnd, 0);
        }
        return 0;
    case WM_DDE_TERMINATE:
        check(step == 6 && (HWND)wp == server, "terminate");
        done();
        return 0;
    case WM_TIMER:                      /* ten seconds and no answer */
        check(0, "timeout");
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
    char cls[16];

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "PMDDEW";
    RegisterClass(&wc);
    me = CreateWindow("PMDDEW", "PMDDE", WS_OVERLAPPEDWINDOW,
                      20, 20, 240, 120, 0, 0, inst, NULL);
    ShowWindow(me, show);

    apm = GlobalAddAtom("PROGMAN");
    agroups = GlobalAddAtom("Groups");
    SendMessage(HWND_BROADCAST, WM_DDE_INITIATE, (WPARAM)me,
                MAKELONG(apm, apm));
    check(acks == 1 && server != 0, "init");
    if (server) {
        check(IsWindow(server), "iswindow");
        check(GetClassName(server, cls, sizeof cls) == 7
              && lstrcmp(cls, "Progman") == 0, "class");
        step = 1;
        SetTimer(me, 1, 10000, NULL);
        execute(me, blocks[0]);
    } else
        done();

    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
