/* longname.c - WIN16 fixture (SPEC.md 124.10): resources asked for by names
 * longer than the 15 characters WIN16 used to keep.
 *
 * WIN16 once copied a string name into a 16-byte buffer, so a longer one was
 * cut and then matched nothing: Microsoft Arcade's Asteroids asks for dialog
 * NUMBEROFPLAYERSDIALOG (21) on F2, DialogBoxParam answered -1 and the game
 * said "Error code = -1". The names are compared in place now. Here:
 *
 *   - DialogBox of the 21-character dialog (its procedure ends it with 7 as
 *     soon as it is made, so nobody has to press anything);
 *   - LoadMenu of a 40-character menu, two items;
 *   - LoadBitmap of a 255-character bitmap (the most a resource table's
 *     count byte can say), 16 x 16, and of "#300", a bitmap by number;
 *   - FindResource of a resource whose TYPE is 28 characters, and its data;
 *   - LoadAccelerators of a 26-character table;
 *   - FindResource in lower case (names are case blind), and the misses: a
 *     21-character name cut to 15 and one with a character too many;
 *   - GlobalAddAtom of a 40-character name twice is one atom, and
 *     GlobalFindAtom finds it (the table keeps 31 characters of it).
 *
 * Every check that fails is named in the message box, which
 * tests/win16longname.py reads; "LONGNAME OK" when none does.
 */
#include <windows.h>
#include <string.h>
#include "longname.h"

static char res[200];

static void check(int ok, const char *tag)
{
    if (!ok) {
        strcat(res, " ");
        strcat(res, tag);
    }
}

BOOL __export CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        PostMessage(dlg, WM_COMMAND, IDOK, 0);
        return TRUE;
    }
    if (msg == WM_COMMAND && wp == IDOK) {
        EndDialog(dlg, 7);
        return TRUE;
    }
    return FALSE;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    static BITMAP bm;
    static char lower[32];
    FARPROC fp;
    HMENU m;
    HBITMAP b;
    HRSRC r;
    HGLOBAL g;
    LPSTR p;
    int i, n;
    ATOM a1, a2;

    (void)prev; (void)cmd; (void)show;

    fp = MakeProcInstance((FARPROC)DlgProc, inst);
    n = DialogBox(inst, N21, NULL, (DLGPROC)fp);
    FreeProcInstance(fp);
    check(n == 7, "dlg21");

    lstrcpy(lower, N21);
    for (i = 0; lower[i]; i++)
        if (lower[i] >= 'A' && lower[i] <= 'Z')
            lower[i] += 'a' - 'A';
    check(FindResource(inst, lower, RT_DIALOG) != 0, "lower");
    lower[15] = 0;
    check(FindResource(inst, lower, RT_DIALOG) == 0, "cut15");
    check(FindResource(inst, N21 "S", RT_DIALOG) == 0, "plus1");

    m = LoadMenu(inst, N40);
    check(m != 0, "menu40");
    if (m) {
        check(GetMenuItemCount(m) == 2, "menu40items");
        DestroyMenu(m);
    }

    b = LoadBitmap(inst, N255);
    check(b != 0, "bmp255");
    if (b) {
        GetObject(b, sizeof bm, (LPSTR)&bm);
        check(bm.bmWidth == 16 && bm.bmHeight == 16, "bmp255size");
        DeleteObject(b);
    }
    b = LoadBitmap(inst, "#300");
    check(b != 0, "bmp300");
    if (b)
        DeleteObject(b);

    r = FindResource(inst, "LONGDATA", NTYPE);
    check(r != 0, "type28");
    if (r) {
        g = LoadResource(inst, r);
        p = g ? LockResource(g) : NULL;
        check(p != NULL && lstrcmp(p, "LONGNAME DATA") == 0, "type28data");
        if (g)
            FreeResource(g);
    }

    check(LoadAccelerators(inst, NACC) != 0, "acc26");

    a1 = GlobalAddAtom(N40);
    a2 = GlobalAddAtom(N40);
    check(a1 != 0 && a1 == a2, "atom40");
    check(a1 != 0 && GlobalFindAtom(N40) == a1, "findatom40");

    MessageBox(NULL, res[0] ? res : "LONGNAME OK", "WIN16 long names", MB_OK);
    return 0;
}
