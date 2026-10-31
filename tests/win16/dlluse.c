/* dlluse.c - WIN16 fixture (SPEC.md 124.23, docs/plans/WIN16-ANY-PLAN.md
 * S18): a program with libraries of its own. It imports W16DLL.DLL by name
 * (DllBump) and by ordinal (DllCount, DllStarted - tests/win16/dlluse.wlk),
 * reaches it again through GetModuleHandle and GetProcAddress, then loads
 * W16DLL2.DLL, which imports W16DLL as well, and frees it.
 *
 * Its MessageBox is "DLLUSE OK" or names each check that failed; the row
 * win16dll reads it off the program's stack.
 */
#include <windows.h>
#include <string.h>

int FAR PASCAL DllBump(int by);
int FAR PASCAL DllCount(void);
int FAR PASCAL DllStarted(void);

typedef int (FAR PASCAL *BUMPPROC)(int);
typedef int (FAR PASCAL *VOIDPROC)(void);

static char res[256];
static int fails;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    HINSTANCE h, h2, h3;
    BUMPPROC bump;
    VOIDPROC count, two;
    char path[80];
    int n;

    check(DllStarted() == 0x5A, "libmain");
    check(DllBump(1) == 1, "byname");
    check(DllCount() == 1, "byord");

    h = GetModuleHandle("W16DLL");
    check(h != 0, "modh");
    check(GetModuleHandle("W16DLL.DLL") == h, "modhdll");
    bump = (BUMPPROC)GetProcAddress(h, "DllBump");
    check(bump != NULL && bump(2) == 3, "procname");
    count = (VOIDPROC)GetProcAddress(h, MAKEINTRESOURCE(3));
    check(count != NULL && count() == 3, "procord");
    check(GetProcAddress(h, "NoSuchProc") == NULL, "procnone");
    n = GetModuleFileName(h, path, sizeof path);
    check(n > 10 && strcmp(path + n - 10, "W16DLL.DLL") == 0, "filename");
    check(GetModuleUsage(h) == 1, "usage1");

    h2 = LoadLibrary("W16DLL2.DLL");
    check(h2 >= 32, "load2");
    if (h2 >= 32) {
        check(GetModuleUsage(h) == 2, "usage2");
        two = (VOIDPROC)GetProcAddress(h2, "DLL2BUMP");
        check(two != NULL && two() == 103, "shared");
        check(DllCount() == 103, "onedgroup");
        h3 = LoadLibrary("w16dll");
        check(h3 == h, "again");
        check(GetModuleUsage(h) == 3, "usage3");
        FreeLibrary(h3);
        FreeLibrary(h2);
        check(DllCount() == 1103, "wep");
        check(GetModuleUsage(h) == 1, "usagefree");
    }
    check(LoadLibrary("NOSUCH.DLL") == 2, "nofile");

    if (fails == 0)
        strcpy(res, "DLLUSE OK");
    MessageBox(0, res, "DLLUSE", MB_OK);
    return 0;
}
