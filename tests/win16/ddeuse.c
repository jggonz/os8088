/* ddeuse.c - WIN16 fixture (SPEC.md 124.25.26, WIN16-NEXT `ddeml`): DDEML
 * answers as Windows 3.1's does with no DDE server running. An installer
 * (Sir Hangman's InstallShield engine) calls these in this order and ended
 * at the first, because WIN16 had no DDEML.
 *
 * 1. DdeInitialize writes a non-zero instance id and answers 0;
 * 2. DdeCreateStringHandle answers a non-zero HSZ, the same one for the
 *    same name (case aside) and another for another name; DdeQueryString
 *    gives the name back, and its length for a NULL buffer;
 *    DdeCmpStringHandles answers 0 for one handle and not 0 for two;
 * 3. DdeConnect (PROGMAN|PROGMAN) and DdeConnectList answer NULL;
 * 4. DdeGetLastError answers DMLERR_NO_CONV_ESTABLISHED (400Ah);
 * 5. DdeClientTransaction answers NULL;
 * 6. DdeNameService (DNS_REGISTER) answers non-zero;
 * 7. DdeKeepStringHandle and DdeFreeStringHandle (twice) answer TRUE;
 * 8. DdeUninitialize answers TRUE.
 *
 * Every check that fails is named in the message box, which
 * tests/win16dde.py reads; "DDEUSE OK" when none does.
 */
#include <windows.h>
#include <ddeml.h>
#include <string.h>

static char res[200];
static char buf[32];
static int fails;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

HDDEDATA CALLBACK __export ddecb(UINT t, UINT f, HCONV c, HSZ h1, HSZ h2,
                                 HDDEDATA d, DWORD x1, DWORD x2)
{
    return (HDDEDATA)0;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    DWORD id = 0;
    HSZ svc, top, low, oth;
    HCONV conv;
    PFNCALLBACK cb = (PFNCALLBACK)MakeProcInstance((FARPROC)ddecb, inst);

    check(DdeInitialize(&id, cb, APPCLASS_STANDARD, 0L) == DMLERR_NO_ERROR,
          "init");
    check(id != 0, "id");
    svc = DdeCreateStringHandle(id, "PROGMAN", CP_WINANSI);
    top = DdeCreateStringHandle(id, "PROGMAN", CP_WINANSI);
    low = DdeCreateStringHandle(id, "progman", CP_WINANSI);
    oth = DdeCreateStringHandle(id, "os8088 DDE", CP_WINANSI);
    check(svc != 0, "hsz1");
    check(top == svc && low == svc, "hsz2");
    check(oth != 0 && oth != svc, "hsz3");
    check(DdeQueryString(id, svc, NULL, 0, CP_WINANSI) == 7, "qlen");
    check(DdeQueryString(id, oth, buf, sizeof buf, CP_WINANSI) == 10
          && strcmp(buf, "os8088 DDE") == 0, "qstr");
    check(DdeQueryString(id, svc, buf, 4, CP_WINANSI) == 3
          && strcmp(buf, "PRO") == 0, "qcut");
    check(DdeCmpStringHandles(svc, low) == 0, "cmp0");
    check(DdeCmpStringHandles(svc, oth) != 0
          && DdeCmpStringHandles(svc, oth) == -DdeCmpStringHandles(oth, svc),
          "cmp1");
    conv = DdeConnect(id, svc, top, NULL);
    check(conv == 0, "connect");
    check(DdeGetLastError(id) == DMLERR_NO_CONV_ESTABLISHED, "lasterr");
    check(DdeConnectList(id, svc, top, 0, NULL) == 0, "connlist");
    check(DdeClientTransaction(NULL, 0, conv, 0, 0, XTYP_EXECUTE, 1000,
                               NULL) == 0, "transact");
    check(DdeNameService(id, svc, 0, DNS_REGISTER) != 0, "nameservice");
    check(DdeKeepStringHandle(id, svc) != 0, "keep");
    check(DdeFreeStringHandle(id, svc) != 0, "free1");
    check(DdeFreeStringHandle(id, svc) != 0, "free2");
    check(DdeFreeStringHandle(id, top) != 0, "free3");
    check(DdeFreeStringHandle(id, oth) != 0, "free4");
    check(DdeUninitialize(id) != 0, "uninit");

    MessageBox(NULL, res[0] ? res : "DDEUSE OK", "DDEUSE", MB_OK);
    return 0;
}
