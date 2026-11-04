/* props.c - WIN16 fixture (SPEC.md 124.25.13): window properties, and the
 * other calls Microsoft Arcade's games import - WaitMessage, CloseWindow.
 *
 * SetProp, GetProp and RemoveProp on two windows, with a string name (in
 * another case to find it) and an atom name (GlobalAddAtom's, and a
 * MAKEINTATOM-style integer); a second SetProp replaces the HANDLE; the two
 * windows' properties of one name are apart; EnumProps counts and sums them,
 * gives a string-named one's name as a string and an integer atom's as the
 * atom, answers the callback's last answer (0 when it stopped) and -1 for a
 * window with none; RemoveProp answers the HANDLE and the next GetProp 0; a
 * destroyed window's properties are gone (a new window in its slot starts
 * with none, and EnumProps over it calls nothing); and WaitMessage returns at
 * once with a message queued, and with none waits for the timer's.
 *
 * Every check that fails is named in the message box, which
 * tests/win16props.py reads; "PROPS OK" when none does.
 */
#include <windows.h>
#include <string.h>

static char res[200];
static int enumn;
static unsigned enumsum;
static int stopafter;
static int enumstr, enumatom;

static void check(int ok, const char *tag)
{
    if (!ok) {
        strcat(res, " ");
        strcat(res, tag);
    }
}

int __export CALLBACK EnumProc(HWND hwnd, LPCSTR name, HANDLE h)
{
    (void)hwnd;
    if (HIWORD((DWORD)name) != 0 && lstrcmpi(name, "spin") == 0)
        enumstr++;
    if (HIWORD((DWORD)name) == 0 && LOWORD((DWORD)name) == 0x1234)
        enumatom++;
    enumn++;
    enumsum += (unsigned)h;
    return stopafter == 0 || enumn < stopafter;
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
    static WNDCLASS wc;
    HWND a, b, c;
    ATOM at;
    MSG m;

    (void)prev; (void)cmd; (void)show;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "Props";
    RegisterClass(&wc);
    a = CreateWindow("Props", "PROPS", WS_OVERLAPPEDWINDOW,
                     0, 0, 200, 100, NULL, NULL, inst, NULL);
    b = CreateWindow("Props", "B", WS_CHILD, 0, 0, 20, 20, a, NULL, inst, NULL);

    /* strings */
    check(GetProp(a, "Spin") == 0, "empty");
    check(SetProp(a, "Spin", (HANDLE)0x1111), "set");
    check(GetProp(a, "Spin") == (HANDLE)0x1111, "get");
    check(GetProp(a, "SPIN") == (HANDLE)0x1111, "case");
    check(GetProp(b, "Spin") == 0, "apart");
    check(SetProp(b, "Spin", (HANDLE)0x2222), "setb");
    check(GetProp(a, "Spin") == (HANDLE)0x1111
          && GetProp(b, "Spin") == (HANDLE)0x2222, "twowin");
    check(SetProp(a, "spin", (HANDLE)0x3333)
          && GetProp(a, "Spin") == (HANDLE)0x3333, "replace");
    check(GetProp(a, "Spun") == 0, "other");

    /* atoms */
    at = GlobalAddAtom("PropAtom");
    check(at != 0, "atom");
    check(SetProp(a, MAKEINTATOM(at), (HANDLE)0x4444), "seta");
    check(GetProp(a, MAKEINTATOM(at)) == (HANDLE)0x4444, "geta");
    check(GetProp(a, "PropAtom") == (HANDLE)0x4444, "atomstr");
    check(SetProp(a, MAKEINTATOM(0x1234), (HANDLE)0x5555)
          && GetProp(a, MAKEINTATOM(0x1234)) == (HANDLE)0x5555, "atomint");

    /* enumeration: a has three string/atom props + 0x1234 = three */
    enumn = 0; enumsum = 0; stopafter = 0; enumstr = 0; enumatom = 0;
    check(EnumProps(a, (PROPENUMPROC)EnumProc) == 1, "enumret");
    check(enumn == 3 && enumsum == 0x3333u + 0x4444u + 0x5555u, "enum");
    check(enumstr == 1, "enumstr");
    check(enumatom == 1, "enumatom");
    enumn = 0; enumsum = 0; stopafter = 2;
    check(EnumProps(a, (PROPENUMPROC)EnumProc) == 0, "enumstopret");
    check(enumn == 2, "enumstop");
    enumn = 0; stopafter = 0;
    EnumProps(b, (PROPENUMPROC)EnumProc);
    check(enumn == 1, "enumb");

    /* removal */
    check(RemoveProp(a, "Spin") == (HANDLE)0x3333, "remove");
    check(GetProp(a, "Spin") == 0, "removed");
    check(RemoveProp(a, "Spin") == 0, "again");
    check(GetProp(b, "Spin") == (HANDLE)0x2222, "bkept");
    check(SetProp((HWND)0x0003, "Spin", (HANDLE)1) == 0, "badwnd");

    /* a destroyed window's properties go with it */
    c = CreateWindow("Props", "C", WS_CHILD, 0, 0, 20, 20, a, NULL, inst, NULL);
    SetProp(c, "Spin", (HANDLE)0x6666);
    DestroyWindow(c);
    c = CreateWindow("Props", "C", WS_CHILD, 0, 0, 20, 20, a, NULL, inst, NULL);
    check(GetProp(c, "Spin") == 0, "gone");
    enumn = 0;
    check(EnumProps(c, (PROPENUMPROC)EnumProc) == -1, "enumnone");
    check(enumn == 0, "gonenum");
    DestroyWindow(c);

    /* the rest of Arcade's imports */
    PostMessage(a, WM_USER, 0, 0L);
    WaitMessage();                  /* one is there: back at once */
    check(PeekMessage(&m, NULL, 0, 0, PM_NOREMOVE), "wait");
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
        DispatchMessage(&m);
    SetTimer(a, 1, 100, NULL);      /* none: it waits for the timer's */
    WaitMessage();
    check(PeekMessage(&m, NULL, 0, 0, PM_REMOVE)
          && m.message == WM_TIMER, "waittimer");
    KillTimer(a, 1);
    CloseWindow(a);
    check(GetProp(a, "PropAtom") == (HANDLE)0x4444, "afterclose");

    MessageBox(a, res[0] ? res : "PROPS OK", "PROPS", MB_OK);
    DestroyWindow(a);
    return 0;
}
