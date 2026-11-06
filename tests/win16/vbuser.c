/* vbuser.c - WIN16 fixture (SPEC.md 124.25.15, docs/plans/WIN16-NEXT-PLAN.md
 * L1): the thirteen KERNEL and USER functions VBRUN300.DLL imports and
 * WIN16 lacked, each checked against a value the program can compute.
 *
 * Every one is imported by ordinal through vbuser.wlk, under a name of its
 * own, so the build needs no header that declares an undocumented call.
 *
 *  GetCurrentPDB      a PSP: its first word is INT 20h (CD 20)
 *  SetHandleCount     the table's size whatever is asked, as Windows': 10
 *                     comes back 10 or more, 500 fewer than 500
 *  GetCodeHandle      the window procedure's: nonzero; a locked global
 *                     block's: that block's handle
 *  FreeModule         W16DLL.DLL loaded twice has a count of 2, and one
 *                     FreeModule makes it 1 and answers TRUE
 *  GetClassWord/Long  style, extra bytes, hModule, icon, background brush
 *                     and window procedure, against what RegisterClass was
 *                     given and what GetClassInfo says; a bad window is 0
 *  GetUpdateRgn       after InvalidateRect(10,12,50,60): SIMPLEREGION, a
 *                     region holding (10,12) and (49,59) and not (9,12) or
 *                     (50,59); after ValidateRect: NULLREGION, and empty
 *  Begin/Defer/End    a child STATIC moved and sized: its window rectangle
 *                     is where it was sent
 *  CreateIcon         a 16x16 1-bit icon is an icon (not the cursor
 *                     LoadCursor hands out) and DestroyIcon takes it back
 *  EndMenu            with no menu loop running, nothing happens
 *  TrackPopupMenu     a popup at a screen point (part 11, 124.25.16). The
 *                     test drives it from the host: the first popup is
 *                     answered Down, Enter (WM_COMMAND 1 reaches the window,
 *                     TRUE comes back, WM_INITMENUPOPUP was sent) and the
 *                     second, right aligned, Esc (no command, TRUE again).
 *                     The third, centred, has a grey item and a submenu:
 *                     Down, Enter on the grey item (nothing), Down, Right
 *                     opens the submenu (a second WM_INITMENUPOPUP), Enter
 *                     there picks its item (WM_COMMAND 7).
 *                     A handle that is no menu and a window that is none
 *                     answer FALSE
 *
 * The MessageBox is "VBUSER OK", or "VBUSER FAIL" and each check that failed.
 */
#include <windows.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>

extern UINT    WINAPI VbuPdb(void);
extern UINT    WINAPI VbuHcnt(UINT);
extern HGLOBAL WINAPI VbuCodeh(FARPROC);
extern BOOL    WINAPI VbuFreemod(HMODULE);
extern WORD    WINAPI VbuClsw(HWND, int);
extern DWORD   WINAPI VbuClsl(HWND, int);
extern void    WINAPI VbuEndmenu(void);
extern int     WINAPI VbuUpdrgn(HWND, HRGN, BOOL);
extern HANDLE  WINAPI VbuBdef(int);
extern HANDLE  WINAPI VbuDef(HANDLE, HWND, HWND, int, int, int, int, UINT);
extern BOOL    WINAPI VbuEdef(HANDLE);
extern HICON   WINAPI VbuIcon(HINSTANCE, int, int, int, int,
                              const void FAR *, const void FAR *);
extern BOOL    WINAPI VbuTrack(HMENU, UINT, int, int, int, HWND,
                               const RECT FAR *);

#define GCW_STYLE_    (-26)
#define GCL_WNDPROC_  (-24)
#define GCW_CBWNDEX_  (-18)
#define GCW_HMODULE_  (-16)
#define GCW_HICON_    (-14)
#define GCW_HBRBACK_  (-10)

static char res[200];
static int ninit, lastcmd;
static const char cls[] = "VBUCLS";

static void check(int ok, const char *tag)
{
    if (!ok && strlen(res) + strlen(tag) + 2 <= sizeof res) {
        strcat(res, " ");
        strcat(res, tag);
    }
}

LRESULT __export CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;

    if (msg == WM_PAINT) {
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_INITMENUPOPUP) {
        ninit++;
        return 0;
    }
    if (msg == WM_COMMAND) {
        lastcmd = wp;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void pump(void)
{
    MSG m;

    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc, wc2;
    HWND hwnd, kid;
    HRGN rgn;
    RECT r, kr;
    POINT pt;
    HANDLE hd;
    HGLOBAL g;
    void FAR *gp;
    HMODULE lib, lib2;
    HICON ic;
    HMENU pop, pop3, sub;
    UINT pdb, n;
    unsigned char andb[32], xorb[32];
    int i;

    (void)prev; (void)cmd; (void)show;
    strcpy(res, "VBUSER FAIL:");

    /* KERNEL */
    pdb = VbuPdb();
    check(pdb != 0, "pdb");
    if (pdb)
        check(*(unsigned far *)MK_FP(pdb, 0) == 0x20CD, "pdbint20");
    check(VbuHcnt(10) >= 10, "hc10");
    n = VbuHcnt(500);
    check(n >= 10 && n < 500, "hc500");
    g = GlobalAlloc(GMEM_MOVEABLE, 100);
    gp = g ? GlobalLock(g) : NULL;
    check(g && gp && VbuCodeh((FARPROC)gp) == g, "codeh:block");
    if (g) {
        GlobalUnlock(g);
        GlobalFree(g);
    }
    check(VbuCodeh((FARPROC)WndProc) != 0, "codeh:proc");

    lib = LoadLibrary("W16DLL.DLL");
    lib2 = LoadLibrary("W16DLL.DLL");
    check((UINT)lib >= 32 && lib2 == lib, "lib");
    if ((UINT)lib >= 32) {
        check(GetModuleUsage(lib) == 2, "usage2");
        check(VbuFreemod(lib) == TRUE, "freemod");
        check(GetModuleUsage(lib) == 1, "usage1");
        FreeLibrary(lib);
    }

    /* USER: a class and a window */
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = (WNDPROC)WndProc;
    wc.cbWndExtra = 6;
    wc.hInstance = inst;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls;
    check(RegisterClass(&wc) != 0, "register");
    hwnd = CreateWindow(cls, "vbuser", WS_OVERLAPPEDWINDOW, 10, 10, 300, 200,
                        NULL, NULL, inst, NULL);
    check(hwnd != 0, "window");
    if (!hwnd) {
        MessageBox(0, res, "WIN16 vbuser", MB_OK);
        return 0;
    }
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    check(GetClassInfo(inst, cls, &wc2) != 0, "classinfo");
    check(VbuClsw(hwnd, GCW_STYLE_) == (CS_HREDRAW | CS_VREDRAW), "gcw:style");
    check(VbuClsw(hwnd, GCW_CBWNDEX_) == 6, "gcw:wndextra");
    check(VbuClsw(hwnd, GCW_HMODULE_) == (WORD)inst, "gcw:module");
    check(VbuClsw(hwnd, GCW_HICON_) == (WORD)wc.hIcon && wc.hIcon, "gcw:icon");
    check(VbuClsw(hwnd, GCW_HBRBACK_) == (WORD)wc2.hbrBackground, "gcw:brush");
    check(VbuClsl(hwnd, GCL_WNDPROC_) == (DWORD)(FARPROC)WndProc, "gcl:proc");
    check(VbuClsl(hwnd, GCW_STYLE_) == (CS_HREDRAW | CS_VREDRAW), "gcl:style");
    check(VbuClsw(0, GCW_STYLE_) == 0, "gcw:nownd");

    /* the update region */
    rgn = CreateRectRgn(0, 0, 0, 0);
    check(rgn != 0, "rgn");
    ValidateRect(hwnd, NULL);       /* a clean slate: whatever ShowWindow owed */
    SetRect(&r, 10, 12, 50, 60);
    InvalidateRect(hwnd, &r, FALSE);
    check(VbuUpdrgn(hwnd, rgn, FALSE) == SIMPLEREGION, "upd:simple");
    check(PtInRegion(rgn, 10, 12) && PtInRegion(rgn, 49, 59), "upd:in");
    check(!PtInRegion(rgn, 9, 12) && !PtInRegion(rgn, 50, 59)
          && !PtInRegion(rgn, 10, 60), "upd:out");
    ValidateRect(hwnd, NULL);
    check(VbuUpdrgn(hwnd, rgn, FALSE) == NULLREGION, "upd:null");
    check(!PtInRegion(rgn, 10, 12), "upd:empty");
    check(VbuUpdrgn(0, rgn, FALSE) == 0, "upd:badwnd");
    DeleteObject(rgn);

    /* DeferWindowPos is SetWindowPos, a batch of one */
    kid = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, 5, 5, 40, 30,
                       hwnd, (HMENU)1, inst, NULL);
    check(kid != 0, "kid");
    if (kid) {
        hd = VbuBdef(2);
        check(hd != 0, "defer:begin");
        hd = VbuDef(hd, kid, NULL, 10, 12, 60, 44, SWP_NOZORDER | SWP_NOACTIVATE);
        check(hd != 0, "defer:pos");
        check(VbuEdef(hd) != 0, "defer:end");
        pt.x = 0;
        pt.y = 0;
        ClientToScreen(hwnd, &pt);
        GetWindowRect(kid, &kr);
        check(kr.left - pt.x == 10 && kr.top - pt.y == 12, "defer:where");
        check(kr.right - kr.left == 60 && kr.bottom - kr.top == 44, "defer:size");
        DestroyWindow(kid);
    }

    /* CreateIcon */
    for (i = 0; i < 32; i++) {
        andb[i] = (i & 2) ? 0xFF : 0x00;
        xorb[i] = (unsigned char)(i * 7);
    }
    ic = VbuIcon(inst, 16, 16, 1, 1, andb, xorb);
    check(ic != 0 && ic != 0x3300, "icon");
    if (ic)
        check(DestroyIcon(ic) != 0, "icon:destroy");

    /* menus: EndMenu with no loop is nothing; TrackPopupMenu is shown, and
       the host (tests/win16vbuser.py) answers it: Down, Enter, then Esc */
    VbuEndmenu();
    pop = CreatePopupMenu();
    AppendMenu(pop, MF_STRING, 1, "one");
    AppendMenu(pop, MF_STRING, 2, "two");
    check(VbuTrack(0xBEEF, TPM_LEFTALIGN, 20, 20, 0, hwnd, NULL) == 0,
          "track:nomenu");
    check(VbuTrack(pop, TPM_LEFTALIGN, 20, 20, 0, NULL, NULL) == 0,
          "track:nownd");
    pt.x = 20;
    pt.y = 20;
    ClientToScreen(hwnd, &pt);
    ninit = 0;
    lastcmd = 0;
    check(VbuTrack(pop, TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL) == 1,
          "track:true");
    pump();
    check(ninit == 1, "track:init");
    check(lastcmd == 1, "track:cmd");
    lastcmd = 0;
    check(VbuTrack(pop, TPM_RIGHTALIGN | TPM_RIGHTBUTTON, pt.x + 100, pt.y,
                   0, hwnd, NULL) == 1, "track:esc");
    pump();
    check(ninit == 2, "track:init2");
    check(lastcmd == 0, "track:nocmd");
    sub = CreatePopupMenu();
    AppendMenu(sub, MF_STRING, 7, "seven");
    pop3 = CreatePopupMenu();
    AppendMenu(pop3, MF_STRING | MF_GRAYED, 3, "grey");
    AppendMenu(pop3, MF_POPUP, (UINT)sub, "sub");
    check(VbuTrack(pop3, TPM_CENTERALIGN, pt.x + 50, pt.y + 10, 0, hwnd,
                   NULL) == 1, "track:sub");
    pump();
    check(ninit == 4, "track:init3");
    check(lastcmd == 7, "track:subcmd");
    VbuEndmenu();
    DestroyMenu(pop);
    DestroyMenu(pop3);

    DestroyWindow(hwnd);
    if (strcmp(res, "VBUSER FAIL:") == 0)
        strcpy(res, "VBUSER OK");
    MessageBox(0, res, "WIN16 vbuser", MB_OK);
    return 0;
}
