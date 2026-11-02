/* lzt.c - WIN16 fixture (SPEC.md 124.24.8-124.24.9,
 * docs/plans/WIN16-ANY-PLAN.md S19): LZEXPAND and VER, an installer's two.
 *
 * tests/win16lz.py puts SZDD files the host packed beside it: SAMPLE.TX_,
 * SECOND.DA_, and LZT.EX_ - this program, packed. It reads SAMPLE.TX_ by
 * its packed name forward, back and past its end, copies it out expanded
 * as SAMPLE.TXT and reads the copy as a plain file; opens SECOND.DAT, which
 * is not there, and is given SECOND.DA_; reads its own version out of the
 * packed copy of itself; and has VerFindFile and VerInstallFile put
 * SECOND.DAT in the system directory.
 *
 * Every check that fails is named in the message box; "LZT OK" and the
 * bytes LZCopy wrote when none does. The row compares SAMPLE.TXT and the
 * installed SECOND.DAT with the host's expansion, byte for byte.
 */
#include <windows.h>
#include <lzexpand.h>
#include <ver.h>
#include <string.h>

static char res[512];

static void check(int ok, const char *tag)
{
    if (!ok) {
        if (res[0] == 0)
            lstrcpy(res, "LZT failed:");
        lstrcat(res, " ");
        lstrcat(res, tag);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    static OFSTRUCT of, of2;
    static char a[16], b[16], c[16], name[80], app[80], sys[80];
    static char cur[80], dest[80], tmp[80];
    static BYTE info[1024];
    static VS_FIXEDFILEINFO FAR *fix;
    static LPSTR val;
    static WORD FAR *tr;
    UINT cb, curlen, destlen, tmplen, r;
    HFILE h, hd;
    LONG len, n;
    DWORD hnd, sz, rv;
    char *p;

    /* the packed file, by its own name */
    h = LZOpenFile("SAMPLE.TX_", &of, OF_READ);
    check(h > 0, "open");
    len = LZSeek(h, 0L, 2);
    check(len > 8192, "length");
    check(LZSeek(h, 5000L, 0) == 5000L && LZRead(h, a, 16) == 16, "fwd");
    check(LZSeek(h, -4L, 1) == 5012L, "rel");
    check(LZSeek(h, 100L, 0) == 100L && LZRead(h, b, 16) == 16, "back");
    check(LZSeek(h, len + 1, 0) == LZERROR_BADVALUE, "past");
    check(LZSeek(h, 0L, 0) == 0L, "start");
    hd = LZOpenFile("SAMPLE.TXT", &of2, OF_CREATE);
    check(hd > 0, "create");
    n = LZCopy(h, hd);
    check(n == len, "copy");
    LZClose(h);
    LZClose(hd);

    check(GetExpandedName("SAMPLE.TX_", name) == 1
          && lstrcmp(name, "SAMPLE.TXT") == 0, "name1");
    check(GetExpandedName("sample.tx_", name) == 1
          && lstrcmp(name, "sample.txt") == 0, "name2");

    /* the copy, plain: LZRead is _lread on it */
    h = LZOpenFile("SAMPLE.TXT", &of, OF_READ);
    check(h > 0 && LZSeek(h, 0L, 2) == len, "plain1");
    check(LZSeek(h, 100L, 0) == 100L && LZRead(h, c, 16) == 16
          && memcmp(b, c, 16) == 0, "plain2");
    LZClose(h);

    /* a name that is not there: LZOpenFile finds it packed */
    h = LZOpenFile("SECOND.DAT", &of, OF_READ);
    check(h > 0 && LZSeek(h, 0L, 2) > 0, "mangled");
    LZClose(h);

    /* its own version, out of the packed copy of itself */
    sz = GetFileVersionInfoSize("LZT.EX_", &hnd);
    check(sz > 0 && sz <= sizeof(info)
          && sz == GetFileVersionInfoSize("LZT.EXE", &hnd), "vsize");
    check(GetFileResourceSize("SAMPLE.TXT", MAKEINTRESOURCE(16),
                              MAKEINTRESOURCE(1), &hnd) == 0, "vnotne");
    if (sz > 0 && sz <= sizeof(info)) {
        check(GetFileVersionInfo("LZT.EX_", hnd, sz, info), "vinfo");
        check(VerQueryValue(info, "\\", (void FAR * FAR *)&fix, &cb)
              && fix->dwSignature == 0xFEEF04BDL
              && fix->dwFileVersionMS == 0x00010002L
              && fix->dwFileVersionLS == 0x00030004L, "vroot");
        check(VerQueryValue(info, "\\StringFileInfo\\040904e4\\ProductName",
                            (void FAR * FAR *)&val, &cb)
              && lstrcmp(val, "W16 LZ Test") == 0, "vname");
        check(VerQueryValue(info, "\\VarFileInfo\\Translation",
                            (void FAR * FAR *)&tr, &cb)
              && cb == 4 && tr[0] == 0x0409 && tr[1] == 0x04E4, "vlang");
        check(!VerQueryValue(info, "\\StringFileInfo\\040904E4\\Nothing",
                             (void FAR * FAR *)&val, &cb), "vnone");
    }
    check(VerLanguageName(0x0409, name, sizeof(name)) == 12
          && lstrcmp(name, "U.S. English") == 0, "lang1");
    check(VerLanguageName(0x7777, name, sizeof(name)) == 16
          && lstrcmp(name, "Unknown Language") == 0, "lang2");
    check(VerLanguageName(0x0809, name, 5) == 4
          && lstrcmp(name, "U.K.") == 0, "lang3");

    /* where a file goes, and putting it there */
    GetModuleFileName(inst, app, sizeof(app));
    p = strrchr(app, '\\');
    if (p)
        p[p == app + 2 ? 1 : 0] = 0;        /* "B:\" keeps its '\' */
    GetSystemDirectory(sys, sizeof(sys));
    curlen = destlen = sizeof(cur);
    r = VerFindFile(0, "SAMPLE.TXT", sys, app, cur, &curlen, dest, &destlen);
    check(r == 0 && lstrcmpi(cur, app) == 0 && lstrcmpi(dest, app) == 0,
          "find1");
    curlen = destlen = sizeof(cur);
    r = VerFindFile(VFFF_ISSHAREDFILE, "SAMPLE.TXT", sys, app, cur, &curlen,
                    dest, &destlen);
    check(r == VFF_CURNEDEST && lstrcmpi(cur, app) == 0
          && lstrcmpi(dest, sys) == 0, "find2");
    curlen = sizeof(cur);
    destlen = 4;
    r = VerFindFile(VFFF_ISSHAREDFILE, "SAMPLE.TXT", sys, app, cur, &curlen,
                    dest, &destlen);
    check((r & VFF_BUFFTOOSMALL) && destlen == (UINT)lstrlen(sys) + 1
          && lstrlen(dest) == 3, "find3");
    tmplen = sizeof(tmp);
    rv = VerInstallFile(0, "SECOND.DA_", "SECOND.DAT", app, sys, "", tmp,
                        &tmplen);
    check(rv == 0, "install");
    curlen = destlen = sizeof(cur);
    r = VerFindFile(VFFF_ISSHAREDFILE, "SECOND.DAT", sys, app, cur, &curlen,
                    dest, &destlen);
    check(r == 0 && lstrcmpi(cur, sys) == 0, "find4");
    rv = VerInstallFile(0, "NOFILE.XX_", "NOFILE.XXX", app, sys, "", tmp,
                        &tmplen);
    check(rv == VIF_CANNOTREADSRC, "nosrc");

    if (res[0] == 0)
        wsprintf(res, "LZT OK %ld", len);
    MessageBox(NULL, res, "LZT", MB_OK);
    return 0;
}
