/* profile.c - WIN16 fixture (SPEC.md 124, docs/plans/WIN16-PLAN.md S7): the
 * two profile families WinMine keeps its best times with. A private file
 * (W16TEST.INI beside the Windows directory) counts its own runs, so the
 * second run must say 2; WIN.INI's [windows] Beep is read as a string.
 */
#include <windows.h>
#include <stdio.h>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    char buf[96], beep[16], n[8];
    int runs = GetPrivateProfileInt("Fixture", "Runs", 0, "W16TEST.INI") + 1;

    sprintf(n, "%d", runs);
    WritePrivateProfileString("Fixture", "Runs", n, "W16TEST.INI");
    GetProfileString("windows", "Beep", "(none)", beep, sizeof beep);
    sprintf(buf, "W16TEST.INI says this is run %d.\nWIN.INI [windows] Beep=%s",
            runs, beep);
    MessageBox(NULL, buf, "WIN16 profile", MB_OK);
    return 0;
}
