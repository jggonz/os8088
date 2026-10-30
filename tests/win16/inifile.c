/* inifile.c - WIN16 fixture (SPEC.md 124.14, docs/plans/WIN16-PLAN.md S7):
 * the profile rules, run on Windows 3.0 and on WIN16 and compared twice -
 * the answers in the message box, and W16INI.INI itself, byte for byte.
 * It starts from no file, so run it on a disk without one.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static char out[640];

static void add(const char *s) { strcat(out, s); }

static void list(const char *b)
{
    while (*b) {
        add(b);
        add(",");
        b += strlen(b) + 1;
    }
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    static char b[128], line[96];
    static const char f[] = "W16INI.INI";
    int n;

    WritePrivateProfileString("Alpha", "One", "1", f);
    WritePrivateProfileString("Alpha", "Two", "  two  ", f);
    WritePrivateProfileString("Beta", "Neg", "-42", f);
    WritePrivateProfileString("alpha", "one", "uno", f);
    WritePrivateProfileString("Beta", "Gone", "x", f);
    WritePrivateProfileString("Beta", "Gone", NULL, f);
    WritePrivateProfileString("Gamma", "K", "v", f);
    WritePrivateProfileString("Gamma", NULL, NULL, f);
    WritePrivateProfileString("Beta", "Last", "\"q\"", f);

    n = GetPrivateProfileString("ALPHA", "ONE", "def", b, sizeof b, f);
    sprintf(line, "One=%s(%d) ", b, n); add(line);
    GetPrivateProfileString("Alpha", "Two", "def", b, sizeof b, f);
    sprintf(line, "Two=[%s] ", b); add(line);
    sprintf(line, "Neg=%d ", GetPrivateProfileInt("Beta", "Neg", 7, f)); add(line);
    sprintf(line, "None=%d ", GetPrivateProfileInt("Beta", "None", 7, f)); add(line);
    GetPrivateProfileString("Beta", "Last", "def", b, sizeof b, f);
    sprintf(line, "Last=%s\n", b); add(line);
    n = GetPrivateProfileString("Alpha", NULL, "", b, sizeof b, f);
    add("keys="); list(b);
    sprintf(line, "(%d) ", n); add(line);
    n = GetPrivateProfileString("Alpha", "One", "def", b, 3, f);
    sprintf(line, "cut=%s(%d) ", b, n); add(line);
    GetPrivateProfileString("Gamma", "K", "gone", b, sizeof b, f);
    sprintf(line, "K=%s ", b); add(line);
    n = GetPrivateProfileString("Nope", "K", NULL, b, sizeof b, f);
    sprintf(line, "nul=[%s](%d)", b, n); add(line);
    MessageBox(NULL, out, "WIN16 ini", MB_OK);
    return 0;
}
