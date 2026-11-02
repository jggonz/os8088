/* mdi.c - WIN16 fixture (SPEC.md 124.27, docs/plans/WIN16-ANY-PLAN.md
 * S20b): the Multiple Document Interface, the way File Manager and Program
 * Manager use it.
 *
 * A frame with a menu bar and a Window menu makes an MDICLIENT and three
 * children in it by WM_MDICREATE, then:
 *
 *   - each child is the client's, the last made is the active one, and the
 *     Window menu lists them after a separator;
 *   - WM_MDITILE lays them side by side inside the client, none over
 *     another, covering it, and each hears WM_SIZE;
 *   - WM_MDICASCADE staggers them, one size, the active one last and
 *     lowest;
 *   - SC_MINIMIZE makes one an icon at the bottom of the client, and a tile
 *     then stops above the icons' row; WM_MDIICONARRANGE keeps it there,
 *     and WM_MDIRESTORE brings it back;
 *   - WM_MDIMAXIMIZE fills the client with one, merges its system menu and
 *     a restore box into the frame's bar, and WM_MDIGETACTIVE says so;
 *     WM_MDIRESTORE puts it back where it was and the bar as it was;
 *   - WM_MDINEXT activates another;
 *   - WM_MDIDESTROY closes them one at a time, the Window menu shrinking
 *     with them, until there is no active child.
 *
 * Every check that fails is named in the message box, which
 * tests/win16mdi.py reads; "MDI OK" when none does.
 */
#include <windows.h>
#include <string.h>

#define NKID 3
#define IDM_FIRST 100

static char res[256];
static HWND frame, client, kid[NKID];
static HMENU bar, wmenu;
static int sized[NKID];

static void check(int ok, const char *tag)
{
    if (!ok) {
        if (res[0] == 0)
            lstrcpy(res, "MDI failed:");
        if (lstrlen(res) < sizeof(res) - 12) {
            lstrcat(res, " ");
            lstrcat(res, tag);
        }
    }
}

/* everything posted, dispatched - a move's WM_SIZE is posted (124.27.5) */
static void pump(void)
{
    MSG m;

    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        if (!TranslateMDISysAccel(client, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
}

/* a child's window rectangle in the client's coordinates */
static void place(HWND w, RECT *r)
{
    POINT a, b;

    GetWindowRect(w, r);
    a.x = r->left;
    a.y = r->top;
    b.x = r->right;
    b.y = r->bottom;
    ScreenToClient(client, &a);
    ScreenToClient(client, &b);
    r->left = a.x;
    r->top = a.y;
    r->right = b.x;
    r->bottom = b.y;
}

static int overlap(RECT *a, RECT *b)
{
    return a->left < b->right && b->left < a->right &&
           a->top < b->bottom && b->top < a->bottom;
}

static int inside(RECT *a, RECT *c)
{
    return a->left >= c->left && a->top >= c->top &&
           a->right <= c->right && a->bottom <= c->bottom;
}

static long area(RECT *r)
{
    return (long)(r->right - r->left) * (r->bottom - r->top);
}

static int which(HWND w)
{
    int i;

    for (i = 0; i < NKID; i++)
        if (kid[i] == w)
            return i;
    return -1;
}

LRESULT __export CALLBACK KidProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SIZE) {
        int i = which(hwnd);
        if (i >= 0)
            sized[i]++;
    }
    return DefMDIChildProc(hwnd, msg, wp, lp);
}

LRESULT __export CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CREATE) {
        static CLIENTCREATESTRUCT cc;
        cc.hWindowMenu = wmenu;
        cc.idFirstChild = IDM_FIRST;
        client = CreateWindow("MDICLIENT", NULL,
                              WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE,
                              0, 0, 0, 0, hwnd, (HMENU)1,
                              ((LPCREATESTRUCT)lp)->hInstance,
                              (LPSTR)&cc);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefFrameProc(hwnd, client, msg, wp, lp);
}

static HWND newkid(HINSTANCE inst, const char *title)
{
    static MDICREATESTRUCT mc;

    mc.szClass = "MdiKid";
    mc.szTitle = (LPSTR)title;
    mc.hOwner = inst;
    mc.x = CW_USEDEFAULT;
    mc.y = CW_USEDEFAULT;
    mc.cx = CW_USEDEFAULT;
    mc.cy = CW_USEDEFAULT;
    mc.style = 0;
    mc.lParam = 0;
    return (HWND)LOWORD(SendMessage(client, WM_MDICREATE, 0,
                                    (LPARAM)(LPMDICREATESTRUCT)&mc));
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    static WNDCLASS wc;
    static RECT cr, r[NKID], keep, icon;
    static char *titles[NKID] = { "One", "Two", "Three" };
    static int before[NKID];
    MSG m;
    LONG ga;
    HWND act;
    int i, j, nbar, nwin, lo;
    long sum;

    wc.lpfnWndProc = FrameProc;
    wc.hInstance = inst;
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = "MdiFrame";
    RegisterClass(&wc);
    wc.lpfnWndProc = KidProc;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "MdiKid";
    RegisterClass(&wc);

    bar = CreateMenu();
    wmenu = CreatePopupMenu();
    AppendMenu(wmenu, MF_STRING, 10, "&Tile");
    AppendMenu(wmenu, MF_STRING, 11, "&Cascade");
    AppendMenu(wmenu, MF_STRING, 12, "&Arrange Icons");
    AppendMenu(bar, MF_STRING, 20, "&File");
    AppendMenu(bar, MF_POPUP, (UINT)wmenu, "&Window");
    frame = CreateWindow("MdiFrame", "MDI", WS_OVERLAPPEDWINDOW |
                         WS_CLIPCHILDREN, CW_USEDEFAULT, 0, 460, 300,
                         NULL, bar, inst, NULL);
    ShowWindow(frame, show);
    UpdateWindow(frame);
    pump();
    check(client != NULL, "client");
    nbar = GetMenuItemCount(bar);
    nwin = GetMenuItemCount(wmenu);
    GetClientRect(client, &cr);

    /* made, the client's, the last one active, listed */
    for (i = 0; i < NKID; i++) {
        kid[i] = newkid(inst, titles[i]);
        check(kid[i] != NULL && IsWindow(kid[i]), "create");
        check(GetParent(kid[i]) == client, "parent");
    }
    pump();
    ga = SendMessage(client, WM_MDIGETACTIVE, 0, 0);
    check((HWND)LOWORD(ga) == kid[NKID - 1] && HIWORD(ga) == 0, "active");
    check(GetMenuItemCount(wmenu) == nwin + 1 + NKID, "listed");
    check(GetMenuItemID(wmenu, nwin + 1) == IDM_FIRST, "firstid");

    /* tiled: inside, apart, covering, and each told its size */
    for (i = 0; i < NKID; i++)
        before[i] = sized[i];
    SendMessage(client, WM_MDITILE, 0, 0);
    pump();
    sum = 0;
    for (i = 0; i < NKID; i++) {
        place(kid[i], &r[i]);
        check(inside(&r[i], &cr), "tile-in");
        check(sized[i] > before[i], "tile-size");
        sum += area(&r[i]);
        for (j = 0; j < i; j++)
            check(!overlap(&r[i], &r[j]), "tile-apart");
    }
    check(sum * 10 >= area(&cr) * 9, "tile-cover");

    /* cascaded: one size, staggered, the active one last and lowest */
    SendMessage(client, WM_MDICASCADE, 0, 0);
    pump();
    act = (HWND)LOWORD(SendMessage(client, WM_MDIGETACTIVE, 0, 0));
    for (i = 0; i < NKID; i++)
        place(kid[i], &r[i]);
    for (i = 0; i < NKID; i++) {
        check(r[i].right - r[i].left == r[0].right - r[0].left &&
              r[i].bottom - r[i].top == r[0].bottom - r[0].top, "casc-size");
        for (j = 0; j < i; j++)
            check(r[i].left != r[j].left && r[i].top != r[j].top,
                  "casc-stagger");
        if (kid[i] != act) {
            j = which(act);
            check(j >= 0 && r[j].left > r[i].left && r[j].top > r[i].top,
                  "casc-top");
        }
    }

    /* minimized: an icon at the bottom, under the tiles */
    SendMessage(kid[0], WM_SYSCOMMAND, SC_MINIMIZE, 0);
    pump();
    check(IsIconic(kid[0]), "min");
    place(kid[0], &icon);
    check(icon.right - icon.left == 32 && icon.bottom - icon.top == 32,
          "min-32");
    check(icon.bottom <= cr.bottom && icon.top >= cr.bottom - 80,
          "min-bottom");
    SendMessage(client, WM_MDITILE, 0, 0);
    pump();
    for (i = 1; i < NKID; i++) {
        place(kid[i], &r[i]);
        check(inside(&r[i], &cr) && r[i].bottom <= icon.top, "tile-above");
    }
    SendMessage(client, WM_MDIICONARRANGE, 0, 0);
    pump();
    place(kid[0], &icon);
    check(IsIconic(kid[0]) && icon.bottom <= cr.bottom &&
          icon.top >= cr.bottom - 80, "arrange");
    SendMessage(client, WM_MDIRESTORE, (WPARAM)kid[0], 0);
    pump();
    check(!IsIconic(kid[0]), "unmin");

    /* maximized: the client filled, the bar merged; then put back */
    place(kid[1], &keep);
    SendMessage(client, WM_MDIMAXIMIZE, (WPARAM)kid[1], 0);
    pump();
    check(IsZoomed(kid[1]), "max");
    ga = SendMessage(client, WM_MDIGETACTIVE, 0, 0);
    check((HWND)LOWORD(ga) == kid[1] && HIWORD(ga) == 1, "max-active");
    check(GetMenuItemCount(bar) == nbar + 2, "max-bar");
    GetClientRect(kid[1], &r[1]);
    check(r[1].right == cr.right && r[1].bottom == cr.bottom, "max-fill");
    SendMessage(client, WM_MDIRESTORE, (WPARAM)kid[1], 0);
    pump();
    check(!IsZoomed(kid[1]), "unmax");
    check(GetMenuItemCount(bar) == nbar, "unmax-bar");
    place(kid[1], &r[1]);
    check(EqualRect(&r[1], &keep), "unmax-place");

    /* next */
    act = (HWND)LOWORD(SendMessage(client, WM_MDIGETACTIVE, 0, 0));
    SendMessage(client, WM_MDINEXT, 0, 0);
    pump();
    ga = SendMessage(client, WM_MDIGETACTIVE, 0, 0);
    check((HWND)LOWORD(ga) != act && which((HWND)LOWORD(ga)) >= 0, "next");

    /* closed, one at a time */
    for (i = NKID - 1; i >= 0; i--) {
        SendMessage(client, WM_MDIDESTROY, (WPARAM)kid[i], 0);
        pump();
        check(!IsWindow(kid[i]), "destroy");
        lo = i ? nwin + 1 + i : nwin;
        check(GetMenuItemCount(wmenu) == lo, "unlisted");
        act = (HWND)LOWORD(SendMessage(client, WM_MDIGETACTIVE, 0, 0));
        check(i ? which(act) >= 0 && which(act) < i : act == NULL,
              "destroy-active");
    }

    MessageBox(frame, res[0] ? res : "MDI OK", "MDI", MB_OK);
    DestroyWindow(frame);
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
