/* mmtest.c - WIN16 fixture (SPEC.md 124.32, docs/plans/WIN16-ANY-PLAN.md
 * S23 batch 1): MMSYSTEM's clock, its devices and mmio, each checked against
 * what the program can see for itself.
 *
 * - The clock: timeGetTime steps by less than a BIOS tick, and keeps to
 *   GetTickCount.
 * - The devices: waveOutGetDevCaps agrees with waveOutGetNumDevs.
 * - mmio: a RIFF WAVE file written with mmioCreateChunk and mmioAscend - an
 *   odd data chunk, so a pad byte - is read back three ways:
 *   - mmioDescend and mmioRead;
 *   - mmioGetInfo and mmioAdvance, through the buffer;
 *   - as a memory file over a copy of its bytes.
 *   Then MMIO_EXIST and MMIO_DELETE.
 *
 * Every check that fails is named in the message box. In real mode it says
 * so and checks nothing: MMSYSTEM is the protected-mode arm's.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define NDATA 1001                  /* odd: the data chunk has a pad byte */
#define NFILE (12 + 8 + 16 + 8 + NDATA + 1)

static char res[256];
static int fails;
static unsigned char pat[NDATA];
static unsigned char got[NDATA];
static unsigned char file[NFILE];
static PCMWAVEFORMAT fmt;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

/* the data chunk found and read back with mmioRead: 1 when it matches */
static int readback(HMMIO h, const char *tag)
{
    MMCKINFO riff, ck;
    char t[16];

    riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
    if (mmioDescend(h, &riff, NULL, MMIO_FINDRIFF) != 0) {
        sprintf(t, "%sriff", tag);
        check(0, t);
        return 0;
    }
    sprintf(t, "%srsize", tag);
    check(riff.cksize == NFILE - 8, t);
    ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
    if (mmioDescend(h, &ck, &riff, MMIO_FINDCHUNK) != 0) {
        sprintf(t, "%sdata", tag);
        check(0, t);
        return 0;
    }
    sprintf(t, "%sdsize", tag);
    check(ck.cksize == NDATA, t);
    memset(got, 0, sizeof got);
    sprintf(t, "%sread", tag);
    check(mmioRead(h, (HPSTR)got, NDATA) == NDATA, t);
    sprintf(t, "%sbytes", tag);
    check(memcmp(got, pat, NDATA) == 0, t);
    sprintf(t, "%sasc", tag);
    check(mmioAscend(h, &ck, 0) == 0, t);
    return (int)ck.dwDataOffset;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    DWORD flags = GetWinFlags();
    DWORD t0, t1, best = 0xFFFFFFFFL;
    TIMECAPS tc;
    WAVEOUTCAPS woc;
    MMIOINFO mi;
    MMCKINFO riff, ck;
    HMMIO h;
    char s[64];
    UINT n, r;
    int i, k, off;
    long len;

    (void)inst; (void)prev; (void)cmd; (void)show;
    if (!(flags & WF_PMODE)) {
        sprintf(res, "mmtest: real mode (flags %04lX), nothing to check", flags);
        MessageBox(0, res, "WIN16 mmtest", MB_OK);
        return 0;
    }
    for (i = 0; i < NDATA; i++)
        pat[i] = (unsigned char)(i * 7 + 3);

    /* the module and the clock */
    check(mmsystemGetVersion() == 0x030A, "ver");
    check(timeGetDevCaps(&tc, sizeof tc) == 0 && tc.wPeriodMin == 1
          && tc.wPeriodMax == 0xFFFF, "tcaps");
    for (k = 0; k < 8; k++) {           /* the smallest step it takes */
        t0 = timeGetTime();
        do t1 = timeGetTime(); while (t1 == t0);
        check(t1 > t0, "mono");
        if (t1 - t0 < best)
            best = t1 - t0;
    }
    check(best < 20, "res");
    t0 = timeGetTime();
    t1 = GetTickCount();
    check(t0 + 110 > t1 && t1 + 110 > t0, "tick");

    /* the devices */
    n = waveOutGetNumDevs();
    r = waveOutGetDevCaps(0, &woc, sizeof woc);
    if (n)
        check(r == 0 && strcmp(woc.szPname, "os8088 Sound") == 0
              && (woc.dwFormats & WAVE_FORMAT_1M08), "wocaps");
    else
        check(r == MMSYSERR_BADDEVICEID, "wocaps0");
    check(waveOutGetDevCaps(1, &woc, sizeof woc) == MMSYSERR_BADDEVICEID,
          "wodev1");
    check(mciGetErrorString(0, s, sizeof s) && s[0] != 0, "mcierr");
    check(mmioStringToFOURCC("wave", MMIO_TOUPPER)
          == mmioFOURCC('W', 'A', 'V', 'E'), "fourcc");

    /* a RIFF WAVE written */
    h = mmioOpen("MMTEST.WAV", NULL, MMIO_CREATE | MMIO_WRITE);
    check(h != 0, "create");
    if (h) {
        riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
        riff.cksize = 0;
        check(mmioCreateChunk(h, &riff, MMIO_CREATERIFF) == 0, "mkriff");
        ck.ckid = mmioFOURCC('f', 'm', 't', ' ');
        ck.cksize = sizeof fmt;
        check(mmioCreateChunk(h, &ck, 0) == 0, "mkfmt");
        fmt.wf.wFormatTag = WAVE_FORMAT_PCM;
        fmt.wf.nChannels = 1;
        fmt.wf.nSamplesPerSec = 11025;
        fmt.wf.nAvgBytesPerSec = 11025;
        fmt.wf.nBlockAlign = 1;
        fmt.wBitsPerSample = 8;
        check(mmioWrite(h, (HPSTR)&fmt, sizeof fmt) == sizeof fmt, "wfmt");
        check(mmioAscend(h, &ck, 0) == 0, "ascfmt");
        ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
        ck.cksize = 0;                  /* Ascend writes the size it got */
        check(mmioCreateChunk(h, &ck, 0) == 0, "mkdata");
        check(mmioWrite(h, (HPSTR)pat, NDATA) == NDATA, "wdata");
        check(mmioAscend(h, &ck, 0) == 0 && ck.cksize == NDATA, "ascdata");
        check(mmioAscend(h, &riff, 0) == 0 && riff.cksize == NFILE - 8,
              "ascriff");
        check(mmioClose(h, 0) == 0, "wclose");
    }

    /* read back: its length, then Descend and mmioRead */
    h = mmioOpen("MMTEST.WAV", NULL, MMIO_READ | MMIO_ALLOCBUF);
    check(h != 0, "open");
    if (h) {
        check(mmioSeek(h, 0, SEEK_END) == NFILE, "len");
        check(mmioSeek(h, 0, SEEK_SET) == 0, "rew");
        off = readback(h, "d");
        /* ...the same bytes through the buffer, with GetInfo and Advance */
        check(mmioSeek(h, off, SEEK_SET) == off, "seek");
        check(mmioGetInfo(h, &mi, 0) == 0 && mi.cchBuffer > 0, "ginfo");
        for (i = 0; i < NDATA; i++) {
            if (mi.pchNext == mi.pchEndRead) {
                if (mmioAdvance(h, &mi, MMIO_READ) != 0
                    || mi.pchNext == mi.pchEndRead)
                    break;
            }
            got[i] = *mi.pchNext++;
        }
        check(i == NDATA && memcmp(got, pat, NDATA) == 0, "adv");
        check(mmioSetInfo(h, &mi, 0) == 0, "sinfo");
        check(mmioClose(h, 0) == 0, "rclose");
    }

    /* ...and as a memory file over a copy of its bytes */
    h = mmioOpen("MMTEST.WAV", NULL, MMIO_READ);
    len = h ? mmioRead(h, (HPSTR)file, sizeof file) : -1;
    check(len == NFILE, "whole");
    if (h)
        mmioClose(h, 0);
    memset(&mi, 0, sizeof mi);
    mi.fccIOProc = FOURCC_MEM;
    mi.pchBuffer = (HPSTR)file;
    mi.cchBuffer = NFILE;
    h = mmioOpen(NULL, &mi, MMIO_READ);
    check(h != 0, "mem");
    if (h) {
        readback(h, "m");
        mmioClose(h, 0);
    }

    /* what a missing file says, and EXIST and DELETE */
    memset(&mi, 0, sizeof mi);
    check(mmioOpen("NOFILE.WAV", &mi, MMIO_READ) == 0
          && mi.wErrorRet == MMIOERR_CANNOTOPEN, "nofile");
    check(mmioOpen("MMTEST.WAV", NULL, MMIO_EXIST) != 0, "exist");
    check(mmioOpen("MMTEST.WAV", NULL, MMIO_DELETE) != 0, "delete");
    check(mmioOpen("MMTEST.WAV", NULL, MMIO_EXIST) == 0, "gone");

    sprintf(res + strlen(res), "%smmtest: %d wrong, waveOut %u, step %lu ms",
            fails ? " - " : "", fails, n, best);
    MessageBox(0, res, "WIN16 mmtest", MB_OK);
    return 0;
}
