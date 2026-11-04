/* restxt.h - a WIN16 fixture's verdict as a FILE beside the program, for the
 * rows that run it under Bochs on kern_mega (tests/win16mega.py). The kern_big
 * rows read the same text off the program's stack at its MessageBox; Bochs
 * has no such reader, so the fixture also writes <NAME>.TXT (the one line the
 * MessageBox shows) before it waits there. Harmless where nobody reads it. */
#include <string.h>

static void restxt(HINSTANCE inst, const char *name, const char *text)
{
    static char rpath[144];
    char *e;
    HFILE f;

    GetModuleFileName(inst, rpath, sizeof rpath - 16);
    e = strrchr(rpath, '\\');
    strcpy(e ? e + 1 : rpath, name);
    f = _lcreat(rpath, 0);
    if (f == HFILE_ERROR)
        return;
    _lwrite(f, text, strlen(text));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}
