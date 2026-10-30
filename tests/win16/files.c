/* files.c - WIN16 fixture (SPEC.md 124.21, docs/plans/WIN16-ANY-PLAN.md S16):
 * files, through both doors a Windows program has - KERNEL's _lopen family
 * and OpenFile, and the C runtime's own int 21h (mkdir, chdir, getcwd,
 * rename, remove, _dos_findfirst, stdio) - in a folder it makes and leaves.
 *
 * Every check that fails is named in the message box, which
 * tests/win16file.py reads; then it writes W16FILE.TXT, 250 numbered lines
 * of 40 bytes - more than one 8 KB window of the DOS - reads it back, and the
 * row reads the same bytes off the floppy image on the host.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <dos.h>

static char res[512];
static int fails;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

static int ends(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

static void line(char *buf, int i)          /* 40 bytes, CR LF included */
{
    sprintf(buf, "W16FILE line %03d of 250, %04X ........\r\n", i, i * 97u);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    static OFSTRUCT of;
    static char buf[600], path[144], tmp[144];
    static struct find_t ff;
    HFILE h;
    FILE *f;
    int i, n, ok;

    /* OpenFile's OF_PARSE: a full path, nothing opened */
    check(OpenFile("w16file.txt", &of, OF_PARSE) == 0 &&
          of.szPathName[1] == ':' && ends(of.szPathName, "\\W16FILE.TXT"),
          "parse");

    /* the C runtime's int 21h: 39h, 3Bh, 47h */
    check(mkdir("W16DIR") == 0, "mkdir");
    check(chdir("W16DIR") == 0, "chdir");
    check(getcwd(path, sizeof path) != NULL && ends(path, "\\W16DIR"), "getcwd");

    /* _lcreat and _lwrite; then a rewrite in the middle through _lopen's
     * read/write handle. (A handle that has GROWN its file writes only at
     * the end - DOS.O88's core, SPEC.md 96.11.2 and 124.21.3 - so the patch
     * is made the way the core can make it.) */
    for (i = 0; i < 300; i++)
        buf[i] = (char)('a' + i % 26);
    h = _lcreat("A.TXT", 0);
    check(h != HFILE_ERROR, "lcreat");
    check(_lwrite(h, buf, 300) == 300, "lwrite");
    check(_lclose(h) == 0, "lclose");
    h = _lopen("A.TXT", OF_READWRITE);
    check(h != HFILE_ERROR, "lopenrw");
    check(_llseek(h, 100L, 0) == 100L, "llseek");
    check(_lwrite(h, "MARK", 4) == 4, "lwrite2");
    check(_lclose(h) == 0, "lclose2");

    /* OF_EXIST, then _lopen and _lread */
    check(OpenFile("A.TXT", &of, OF_EXIST) == 1 && ends(of.szPathName,
          "\\W16DIR\\A.TXT"), "exist");
    h = _lopen("A.TXT", OF_READ);
    check(h != HFILE_ERROR, "lopen");
    check(_llseek(h, 0L, 2) == 300L, "size");
    _llseek(h, 0L, 0);
    n = _lread(h, path, 300);
    ok = n == 300 && memcmp(path + 100, "MARK", 4) == 0 &&
         memcmp(path, buf, 100) == 0 && memcmp(path + 104, buf + 104, 196) == 0;
    check(ok, "lread");
    check(_lread(h, path, 10) == 0, "eof");
    _lclose(h);

    /* 56h, and the old name is gone (OF_EXIST: nErrCode 2) */
    check(rename("A.TXT", "B.TXT") == 0, "rename");
    check(OpenFile("A.TXT", &of, OF_EXIST) == HFILE_ERROR && of.nErrCode == 2,
          "gone");

    /* 4Eh */
    check(_dos_findfirst("*.TXT", _A_NORMAL, &ff) == 0 &&
          strcmp(ff.name, "B.TXT") == 0 && ff.size == 300L, "find");

    /* GetTempFileName creates it; OF_DELETE deletes it */
    n = GetTempFileName(0, "W16", 0, tmp);
    check(n != 0 && OpenFile(tmp, &of, OF_EXIST) == 1, "temp");
    check(OpenFile(tmp, &of, OF_DELETE) == 1 &&
          OpenFile(tmp, &of, OF_EXIST) == HFILE_ERROR, "delete");

    /* stdio: 3Ch, 40h, 3Fh, 3Eh through the runtime */
    f = fopen("C.TXT", "w");
    check(f != NULL, "fopen");
    if (f) {
        fprintf(f, "stdio %d\n", 42);
        fclose(f);
        f = fopen("C.TXT", "r");
        check(f != NULL && fgets(path, sizeof path, f) != NULL &&
              strcmp(path, "stdio 42\n") == 0, "fgets");
        if (f)
            fclose(f);
    }

    /* 41h, 3Bh back up, 3Ah */
    check(remove("B.TXT") == 0 && remove("C.TXT") == 0, "remove");
    check(chdir("..") == 0, "chdir2");
    check(rmdir("W16DIR") == 0, "rmdir");

    /* the file the row reads off the image: past one window */
    h = OpenFile("W16FILE.TXT", &of, OF_CREATE | OF_WRITE);
    check(h != HFILE_ERROR, "create");
    for (i = 1; i <= 250; i++) {
        line(buf, i);
        if (_lwrite(h, buf, 40) != 40) {
            check(0, "write");
            break;
        }
    }
    _lclose(h);
    h = OpenFile("W16FILE.TXT", &of, OF_READ);
    ok = h != HFILE_ERROR;
    for (i = 1; ok && i <= 250; i++) {
        line(buf, i);
        ok = _lread(h, path, 40) == 40 && memcmp(path, buf, 40) == 0;
    }
    _lclose(h);
    check(ok, "readback");

    sprintf(buf, "files: %d failed%s", fails, res);
    MessageBox(NULL, buf, "WIN16 files", MB_OK);
    return 0;
}
