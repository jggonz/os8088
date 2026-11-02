/* shx.c - WIN16 fixture (SPEC.md 124.24.10, docs/plans/WIN16-ANY-PLAN.md
 * S19): ShellExecute and FindExecutable.
 *
 * tests/win16shx.py puts ROOT.TXT beside it and SHX.TXT in a folder, DOCS,
 * so only the folder's can open.
 * The refusals first - a verb other than "open", a file and a folder that
 * are not there - then FindExecutable on a program and on a document, and
 * last ShellExecute on DOCS\SHX.TXT by its folder, which os8088's
 * association opens. After it, a name with no folder still opens beside
 * the program: the DOS was put back where it stood.
 *
 * Every check that fails is named in the message box; "SHX OK" when none
 * does. The row then looks for the window the document opened in.
 */
#include <windows.h>
#include <shellapi.h>
#include <string.h>

static char res[256];

static void check(int ok, const char *tag)
{
    if (!ok) {
        if (res[0] == 0)
            lstrcpy(res, "SHX failed:");
        lstrcat(res, " ");
        lstrcat(res, tag);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    static char buf[144];
    static OFSTRUCT of;
    HINSTANCE r;

    r = ShellExecute(NULL, "print", "ROOT.TXT", NULL, NULL, SW_SHOWNORMAL);
    check((UINT)r == 31, "print");
    r = ShellExecute(NULL, "open", "NOFILE.TXT", NULL, NULL, SW_SHOWNORMAL);
    check((UINT)r == 2, "nofile");
    r = ShellExecute(NULL, "open", "NODIR\\X.TXT", NULL, NULL, SW_SHOWNORMAL);
    check((UINT)r == 3, "nodir");
    buf[0] = 'x';
    check((UINT)FindExecutable("SHX.EXE", NULL, buf) == 33
          && lstrcmpi(buf, "SHX.EXE") == 0, "findexe");
    buf[0] = 'x';
    check((UINT)FindExecutable("ROOT.TXT", NULL, buf) == 31 && buf[0] == 0,
          "finddoc");
    r = ShellExecute(NULL, "OPEN", "SHX.TXT", NULL, "DOCS", SW_SHOWNORMAL);
    check((UINT)r > 32, "open");
    check(OpenFile("SHX.EXE", &of, OF_EXIST) != HFILE_ERROR, "back");
    r = ShellExecute(NULL, "open", "ROOT.TXT", NULL, NULL, SW_SHOWNORMAL);
    check((UINT)r == 8, "busy");            /* the first has not started */

    MessageBox(NULL, res[0] ? res : "SHX OK", "SHX", MB_OK);
    return 0;
}
