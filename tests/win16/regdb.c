/* regdb.c - WIN16 fixture (SPEC.md 124.24.6-124.24.7,
 * docs/plans/WIN16-ANY-PLAN.md S19): the registration database and OLE's
 * "not available".
 *
 * Registers a document class the way an installer does - an extension, a
 * class name, its open command - then reads it back by every route SHELL
 * has: a value by its full name and through an open key, the keys below a
 * key in order, one past the last, a delete and the key gone after it.
 * Then two OLE calls answer as a Windows with no OLE server does.
 *
 * Every check that fails is named in the message box, which
 * tests/win16reg.py reads; "REGDB OK" when none does. REG.DAT keeps what
 * is left (.w16 and w16file's name), which the row reads off the floppy.
 */
#include <windows.h>
#include <shellapi.h>
#include <ole.h>
#include <string.h>

static char res[256];

static void check(int ok, const char *tag)
{
    if (!ok) {
        if (res[0] == 0)
            lstrcpy(res, "REGDB failed:");
        lstrcat(res, " ");
        lstrcat(res, tag);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    static char buf[64];
    static LHCLIENTDOC doc;
    HKEY cls, key;
    LONG cb;

    check(RegSetValue(HKEY_CLASSES_ROOT, ".w16", REG_SZ, "w16file", 0)
          == ERROR_SUCCESS, "set1");
    check(RegSetValue(HKEY_CLASSES_ROOT, "w16file", REG_SZ, "W16 Document", 0)
          == ERROR_SUCCESS, "set2");
    check(RegCreateKey(HKEY_CLASSES_ROOT, "w16file\\shell\\open", &key)
          == ERROR_SUCCESS, "create");
    check(RegSetValue(key, "command", REG_SZ, "w16test.exe %1", 0)
          == ERROR_SUCCESS, "set3");
    check(RegCloseKey(key) == ERROR_SUCCESS, "close1");

    cb = sizeof(buf);
    check(RegQueryValue(HKEY_CLASSES_ROOT, ".w16", buf, &cb) == ERROR_SUCCESS
          && lstrcmp(buf, "w16file") == 0 && cb == 8, "query1");
    cb = sizeof(buf);
    check(RegQueryValue(HKEY_CLASSES_ROOT, "w16file\\shell\\open\\command",
                        buf, &cb) == ERROR_SUCCESS
          && lstrcmp(buf, "w16test.exe %1") == 0, "query2");
    cb = 4;                                 /* cut: three and the NUL */
    check(RegQueryValue(HKEY_CLASSES_ROOT, ".w16", buf, &cb) == ERROR_SUCCESS
          && lstrcmp(buf, "w16") == 0 && cb == 4, "query3");
    cb = sizeof(buf);
    check(RegQueryValue(HKEY_CLASSES_ROOT, "nowhere", buf, &cb)
          == ERROR_BADKEY, "query4");

    check(RegOpenKey(HKEY_CLASSES_ROOT, "w16file", &cls) == ERROR_SUCCESS,
          "open1");
    check(RegEnumKey(cls, 0, buf, sizeof(buf)) == ERROR_SUCCESS
          && lstrcmp(buf, "shell") == 0, "enum0");
    check(RegEnumKey(cls, 1, buf, sizeof(buf)) != ERROR_SUCCESS, "enum1");
    cb = sizeof(buf);
    check(RegQueryValue(cls, "shell\\open\\command", buf, &cb)
          == ERROR_SUCCESS && lstrcmp(buf, "w16test.exe %1") == 0, "query5");
    check(RegDeleteKey(cls, "shell") == ERROR_SUCCESS, "delete");
    check(RegOpenKey(cls, "shell\\open", &key) == ERROR_BADKEY, "gone");
    check(RegCloseKey(cls) == ERROR_SUCCESS, "close2");
    check(RegOpenKey(HKEY_CLASSES_ROOT, "nowhere", &key) == ERROR_BADKEY,
          "open2");

    check(OleRegisterClientDoc("W16", "DOC", 0L, &doc) == OLE_OK && doc != 0,
          "oledoc");
    check(OleQueryCreateFromClip("StdFileEditing", olerender_draw, 0)
          == OLE_ERROR_FORMAT, "oleclip");
    check(OleRevokeClientDoc(doc) == OLE_OK, "olerevoke");

    MessageBox(NULL, res[0] ? res : "REGDB OK", "REGDB", MB_OK);
    return 0;
}
