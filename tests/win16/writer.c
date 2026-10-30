/* writer.c - WIN16 fixture (SPEC.md 124.18.2, docs/plans/WIN16-PLAN.md S12):
 * two programs writing WIN.INI at once. It writes WIN.INI's [W16Merge] key
 * named after its own file - WRITERA.EXE writes WRITERA=written - and then
 * waits in a message box, so a test can start two copies under two names,
 * each holding its own copy of WIN.INI, and end them one after the other.
 * Both keys must be in the file at the end: the second to end merges with
 * what the first wrote instead of writing back the copy it read.
 */
#include <windows.h>
#include <string.h>

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char path[128], key[16], *p, *dot;

    GetModuleFileName(inst, path, sizeof path);
    p = strrchr(path, '\\');
    if (p == NULL)
        p = strrchr(path, ':');
    p = p ? p + 1 : path;
    strncpy(key, p, sizeof key - 1);
    key[sizeof key - 1] = 0;
    dot = strchr(key, '.');
    if (dot)
        *dot = 0;
    WriteProfileString("W16Merge", key, "written");
    MessageBox(NULL, key, "WIN16 merge", MB_OK);
    return 0;
}
