/* xvol.c - WIN16 fixture (SPEC.md 124.21, docs/plans/WIN16-ANY-PLAN.md
 * S24): a setup's copy from its floppy to the hard disk. The program copies
 * its own file, a kilobyte a read and a kilobyte a write as LZCopy does,
 * into a folder it makes in the Windows directory - which is on another
 * volume when the program is started from a floppy and WIN16 lives on the
 * hard disk - then reads the copy back and compares it with itself. Twice:
 * into XVOLDIR and into ~XVOL.T, which is the shape of the Setup Toolkit's
 * ~MSSTFQF.T. It asks AH=36h about the Windows directory's drive as well,
 * which must answer for that drive and not for the floppy.
 *
 * Its MessageBox is "XVOL OK" or names each check that failed.
 */
#include <windows.h>
#include <string.h>
#include <direct.h>
#include <dos.h>

static char res[256];
static int fails;
static char me[144], dir[144], dst[160];
static char a[1024], b[1024];

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

static void copy(const char *sub, const char *tag)
{
    HFILE s, d;
    UINT n, m;
    long total = 0, back = 0;
    int same = 1;

    GetWindowsDirectory(dir, sizeof dir);
    lstrcat(dir, "\\");
    lstrcat(dir, sub);
    mkdir(dir);
    lstrcpy(dst, dir);
    lstrcat(dst, "\\COPY.BIN");
    s = _lopen(me, OF_READ);
    d = _lcreat(dst, 0);
    check(s != HFILE_ERROR, "open");
    if (s == HFILE_ERROR) {             /* say which file it was */
        strcat(res, " ");
        strcat(res, me);
    }
    check(d != HFILE_ERROR, "creat");
    if (s == HFILE_ERROR || d == HFILE_ERROR)
        return;
    for (;;) {                          /* read, write, read, write... */
        n = _lread(s, a, sizeof a);
        if (n == (UINT)-1) {
            check(0, "read");
            break;
        }
        if (n == 0)
            break;
        if (_lwrite(d, a, n) != n) {
            check(0, "write");
            break;
        }
        total += n;
    }
    check(_lclose(d) == 0, "close");
    _llseek(s, 0, 0);
    d = _lopen(dst, OF_READ);
    check(d != HFILE_ERROR, "reopen");
    if (d != HFILE_ERROR) {
        for (;;) {
            n = _lread(s, a, sizeof a);
            m = _lread(d, b, sizeof b);
            if (n != m || memcmp(a, b, n) != 0)
                same = 0;
            if (n == 0 || n == (UINT)-1 || m == (UINT)-1)
                break;
            back += m;
        }
        _lclose(d);
    }
    _lclose(s);
    check(same && back == total && total > 2048, tag);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    union REGS r;
    unsigned drv;

    GetModuleFileName(inst, me, sizeof me);
    GetWindowsDirectory(dir, sizeof dir);
    drv = (dir[0] | 0x20) - 'a' + 1;
    r.h.ah = 0x36;                      /* the Windows directory's drive */
    r.h.dl = (unsigned char)drv;
    intdos(&r, &r);
    check(r.x.ax != 0xFFFF, "dfree");
    if (me[0] != dir[0])                /* a floppy's 2,847 clusters, if */
        check(r.x.dx > 2880, "dfdrive");/*   it answered for the wrong one */
    copy("XVOLDIR", "plain");
    copy("~XVOL.T", "tilde");
    MessageBox(0, fails ? res : "XVOL OK", "XVOL", MB_OK);
    return 0;
}
