/* ctlproc.c - WIN16 fixture (SPEC.md 124.25.18): WIN16's controls have a
 * window procedure a program can call.
 *
 * What Visual Basic 3.0's runtime does, and what many C programs do:
 *   superclass  GetClassInfo("Edit") and ("Button"), register a class of
 *               our own named for it with the copied lpfnWndProc behind a
 *               procedure that forwards (CallWindowProc), CreateWindow it,
 *               and use it as the control: WM_SETTEXT, EM_SETSEL,
 *               EM_REPLACESEL, WM_GETTEXT; BM_SETCHECK, BM_GETCHECK;
 *               The superclass window keeps its class's name, and its
 *               own window words (after cbWndExtra's, which a multi-line
 *               Edit uses) are its own;
 *   swallow     a superclass whose procedure answers WM_CREATE itself and
 *               forwards the rest is still a working multi-line Edit;
 *   subclass    SetWindowLong(GWL_WNDPROC) on a real Edit, eat one key and
 *               pass the rest to the old procedure;
 *   many        seventy more classes, past the class block's first 64
 *               records: every one registers, and the first one's atom and
 *               name still find it after the block has grown.
 *
 * Every check that fails is named in the message box, which
 * tests/win16ctlproc.py reads; "CTLPROC OK" when none does.
 */
#include <windows.h>
#include <string.h>
#include "restxt.h"

static char res[240];
static FARPROC baseEdit, baseButton, oldEdit;
static int supseen, subseen, subate;

static void check(int ok, const char *tag)
{
    if (!ok) {
        strcat(res, " ");
        strcat(res, tag);
    }
}

LRESULT __export CALLBACK SupEdit(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    supseen++;
    return CallWindowProc(baseEdit, hwnd, msg, wp, lp);
}

LRESULT __export CALLBACK SupEat(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CREATE)
        return 0;
    return CallWindowProc(baseEdit, hwnd, msg, wp, lp);
}

LRESULT __export CALLBACK SupButton(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    supseen++;
    return CallWindowProc(baseButton, hwnd, msg, wp, lp);
}

LRESULT __export CALLBACK SubEdit(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    subseen++;
    if (msg == WM_CHAR && wp == 'x') {
        subate++;
        return 0;
    }
    return CallWindowProc(oldEdit, hwnd, msg, wp, lp);
}

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
    static WNDCLASS wc, ce, cb;
    static char buf[64];
    HWND top, e, b, s, m;
    int n, base, i, bad;
    ATOM first;
    static char gname[8] = "Grow00";

    (void)prev; (void)cmd; (void)show;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "CtlProc";
    RegisterClass(&wc);
    top = CreateWindow("CtlProc", "CTLPROC", WS_OVERLAPPEDWINDOW,
                       0, 0, 300, 200, NULL, NULL, inst, NULL);

    /* the classes' own WNDCLASS */
    check(GetClassInfo(NULL, "Edit", &ce) != 0, "ciedit");
    check(GetClassInfo(NULL, "Button", &cb) != 0, "cibutton");
    check(ce.lpfnWndProc != NULL, "edproc");
    check(cb.lpfnWndProc != NULL, "btproc");
    check(ce.lpfnWndProc != cb.lpfnWndProc, "apart");
    check(GetClassInfo(NULL, "NoSuchClassAtAll", &wc) == 0, "cinone");
    baseEdit = (FARPROC)ce.lpfnWndProc;
    baseButton = (FARPROC)cb.lpfnWndProc;

    /* superclass both: our name, our procedure, their behaviour */
    base = ce.cbWndExtra;               /* the Edit's own words come first */
    ce.cbWndExtra += 2;
    ce.lpszClassName = "SupEdit";
    ce.lpfnWndProc = SupEdit;
    ce.hInstance = inst;
    check(RegisterClass(&ce) != 0, "regedit");
    ce.lpszClassName = "SupEat";
    ce.lpfnWndProc = SupEat;
    check(RegisterClass(&ce) != 0, "regeat");
    cb.lpszClassName = "SupButton";
    cb.lpfnWndProc = SupButton;
    cb.hInstance = inst;
    check(RegisterClass(&cb) != 0, "regbutton");

    e = CreateWindow("SupEdit", "", WS_CHILD | WS_VISIBLE | WS_BORDER
                     | ES_AUTOHSCROLL, 4, 4, 160, 20, top, (HMENU)1, inst, NULL);
    check(e != 0, "mkedit");
    check(supseen > 0, "supseen");
    SendMessage(e, WM_SETTEXT, 0, (LPARAM)(LPSTR)"hello");
    n = (int)SendMessage(e, WM_GETTEXT, sizeof buf, (LPARAM)(LPSTR)buf);
    check(n == 5 && strcmp(buf, "hello") == 0, "settext");
    SendMessage(e, EM_SETSEL, 0, MAKELONG(5, 5));
    SendMessage(e, EM_REPLACESEL, 0, (LPARAM)(LPSTR)" world");
    buf[0] = 0;
    SendMessage(e, WM_GETTEXT, sizeof buf, (LPARAM)(LPSTR)buf);
    check(strcmp(buf, "hello world") == 0, "replace");
    check((int)SendMessage(e, WM_GETTEXTLENGTH, 0, 0L) == 11, "length");
    buf[0] = 0;
    GetClassName(e, buf, sizeof buf);
    check(lstrcmpi(buf, "SupEdit") == 0, "clsname");

    /* a multi-line one: our word after the Edit's survives its text */
    m = CreateWindow("SupEdit", "", WS_CHILD | WS_VISIBLE | WS_BORDER
                     | ES_MULTILINE, 170, 4, 120, 60, top, (HMENU)4, inst, NULL);
    check(m != 0, "mkml");
    SetWindowWord(m, base, 0x5A5A);
    SendMessage(m, WM_SETTEXT, 0, (LPARAM)(LPSTR)"one\r\ntwo");
    SendMessage(m, WM_CHAR, 'z', 1L);
    check(GetWindowWord(m, base) == 0x5A5A, "mlword");
    buf[0] = 0;
    SendMessage(m, WM_GETTEXT, sizeof buf, (LPARAM)(LPSTR)buf);
    check(strcmp(buf, "zone\r\ntwo") == 0, "mltext");

    /* WM_CREATE answered by the superclass, never forwarded */
    m = CreateWindow("SupEat", "", WS_CHILD | WS_VISIBLE | WS_BORDER
                     | ES_MULTILINE, 170, 70, 120, 60, top, (HMENU)5, inst, NULL);
    check(m != 0, "mkeat");
    SendMessage(m, WM_SETTEXT, 0, (LPARAM)(LPSTR)"ate");
    buf[0] = 0;
    SendMessage(m, WM_GETTEXT, sizeof buf, (LPARAM)(LPSTR)buf);
    check(strcmp(buf, "ate") == 0, "eattext");

    b = CreateWindow("SupButton", "Check", WS_CHILD | WS_VISIBLE
                     | BS_CHECKBOX, 4, 30, 100, 20, top, (HMENU)2, inst, NULL);
    check(b != 0, "mkbutton");
    check((int)SendMessage(b, BM_GETCHECK, 0, 0L) == 0, "unchecked");
    SendMessage(b, BM_SETCHECK, 1, 0L);
    check((int)SendMessage(b, BM_GETCHECK, 0, 0L) == 1, "checked");
    SendMessage(b, BM_SETCHECK, 0, 0L);
    check((int)SendMessage(b, BM_GETCHECK, 0, 0L) == 0, "cleared");

    /* subclass a plain Edit */
    s = CreateWindow("Edit", "", WS_CHILD | WS_VISIBLE | WS_BORDER
                     | ES_AUTOHSCROLL, 4, 56, 160, 20, top, (HMENU)3, inst, NULL);
    check(s != 0, "mksub");
    oldEdit = (FARPROC)SetWindowLong(s, GWL_WNDPROC, (LONG)(FARPROC)SubEdit);
    check(oldEdit != NULL, "oldproc");
    check((FARPROC)GetWindowLong(s, GWL_WNDPROC) == (FARPROC)SubEdit, "getproc");
    SendMessage(s, WM_CHAR, 'a', 1L);
    SendMessage(s, WM_CHAR, 'x', 1L);
    SendMessage(s, WM_CHAR, 'b', 1L);
    buf[0] = 0;
    SendMessage(s, WM_GETTEXT, sizeof buf, (LPARAM)(LPSTR)buf);
    check(strcmp(buf, "ab") == 0, "subtext");
    check(subate == 1, "subate");
    check(subseen >= 4, "subseen");

    /* many: the table grows past its first 64 */
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = gname;
    first = 0;
    bad = 0;
    for (i = 0; i < 70; i++) {
        ATOM a;
        gname[4] = (char)('0' + i / 10);
        gname[5] = (char)('0' + i % 10);
        a = RegisterClass(&wc);
        if (a == 0)
            bad++;
        if (i == 0)
            first = a;
    }
    check(bad == 0, "many");
    check(GetClassInfo(inst, MAKEINTRESOURCE(first), &cb) != 0
          && cb.lpfnWndProc == (WNDPROC)WndProc, "atom");
    check(GetClassInfo(inst, "Grow00", &cb) != 0, "grow00");
    check(GetClassInfo(inst, "Grow69", &cb) != 0, "grow69");
    m = CreateWindow("Grow00", "", WS_CHILD, 0, 0, 8, 8, top, (HMENU)6,
                     inst, NULL);
    check(m != 0, "mkgrow");
    buf[0] = 0;
    GetClassName(m, buf, sizeof buf);
    check(lstrcmpi(buf, "Grow00") == 0, "growname");
    GetClassName(e, buf, sizeof buf);       /* moved, and still its own */
    check(lstrcmpi(buf, "SupEdit") == 0, "movedname");

    restxt(inst, "CTLPROC.TXT", res[0] ? res : "CTLPROC OK");

    MessageBox(top, res[0] ? res : "CTLPROC OK", "CTLPROC", MB_OK);
    DestroyWindow(top);
    return 0;
}
