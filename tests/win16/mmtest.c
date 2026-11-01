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
 * - waveOut (batch 2): three blocks of a tone queued with CALLBACK_TASK, each
 *   done in order with its MM_WOM_DONE, and the position at their end; the
 *   refusals on the way. Then sndPlaySound: that RIFF file to its end, a
 *   sound in memory looped and stopped, and the device free after.
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

#define NTONE 3000                  /* a block: 272 ms at 11,025 Hz */
#define NWAV 1000                   /* the sound in memory: 91 ms */
static unsigned char tone[NTONE];
static WAVEHDR wh[3];
static unsigned char wav[44 + NWAV];
static DWORD played;

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

static void put4(unsigned char *p, DWORD v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* the messages MM_WOM_OPEN..MM_WOM_DONE posted to this task, for up to ms */
static int wompump(HWAVEOUT hw, int *opens, int *dones, int *closes,
                   int want, DWORD ms)
{
    MSG msg;
    DWORD t0 = timeGetTime();

    while (*dones < want && timeGetTime() - t0 < ms) {
        if (!PeekMessage(&msg, 0, MM_WOM_OPEN, MM_WOM_DONE, PM_REMOVE))
            continue;
        if (msg.message == MM_WOM_OPEN)
            (*opens)++;
        else if (msg.message == MM_WOM_CLOSE) {
            (*closes)++;
            return 1;
        } else if (msg.message == MM_WOM_DONE) {
            check(msg.wParam == (WPARAM)hw
                  && (WAVEHDR FAR *)msg.lParam == (WAVEHDR FAR *)&wh[*dones],
                  "donemsg");
            (*dones)++;
        }
    }
    return *dones >= want;
}

/* waveOut and sndPlaySound (SPEC.md 124.32.3) */
static void waveout(UINT n)
{
    PCMWAVEFORMAT pf;
    HWAVEOUT hw = 0, h2;
    MMTIME mt;
    MSG msg;
    char s[80];
    DWORD t0;
    int i, k, opens = 0, dones = 0, closes = 0;
    UINT r;

    pf.wf.wFormatTag = WAVE_FORMAT_PCM;
    pf.wf.nChannels = 1;
    pf.wf.nSamplesPerSec = 11025;
    pf.wf.nAvgBytesPerSec = 11025;
    pf.wf.nBlockAlign = 1;
    pf.wBitsPerSample = 8;
    check(waveOutGetErrorText(WAVERR_BADFORMAT, s, sizeof s) == 0 && s[0],
          "woerr");
    if (!n) {
        check(waveOutOpen(&hw, 0, (LPWAVEFORMAT)&pf, 0, 0, 0)
              == MMSYSERR_BADDEVICEID, "wonone");
        check(!sndPlaySound("MMTEST.WAV", SND_SYNC | SND_NODEFAULT),
              "sndnone");
        return;
    }
    check(waveOutOpen(NULL, 0, (LPWAVEFORMAT)&pf, 0, 0, WAVE_FORMAT_QUERY)
          == 0, "query");
    pf.wf.wFormatTag = 2;
    check(waveOutOpen(&hw, 0, (LPWAVEFORMAT)&pf, 0, 0, 0) == WAVERR_BADFORMAT,
          "badfmt");
    pf.wf.wFormatTag = WAVE_FORMAT_PCM;
    r = waveOutOpen(&hw, WAVE_MAPPER, (LPWAVEFORMAT)&pf,
                    (DWORD)(UINT)GetCurrentTask(), 0, CALLBACK_TASK);
    check(r == 0 && hw != 0, "woopen");
    if (r != 0)
        return;
    check(waveOutOpen(&h2, 0, (LPWAVEFORMAT)&pf, 0, 0, 0)
          == MMSYSERR_ALLOCATED, "alloc");
    for (i = 0; i < NTONE; i++)         /* a 919 Hz square, three times */
        tone[i] = (unsigned char)(((i / 6) & 1) ? 0xC0 : 0x40);
    for (k = 0; k < 3; k++) {
        memset(&wh[k], 0, sizeof wh[k]);
        wh[k].lpData = (LPSTR)tone;
        wh[k].dwBufferLength = NTONE;
    }
    check(waveOutWrite(hw, &wh[0], sizeof(WAVEHDR)) == WAVERR_UNPREPARED,
          "unprep");
    for (k = 0; k < 3; k++)
        check(waveOutPrepareHeader(hw, &wh[k], sizeof(WAVEHDR)) == 0
              && (wh[k].dwFlags & WHDR_PREPARED), "prep");
    t0 = timeGetTime();
    for (k = 0; k < 3; k++)
        check(waveOutWrite(hw, &wh[k], sizeof(WAVEHDR)) == 0, "write");
    check(!(wh[2].dwFlags & WHDR_DONE) && (wh[2].dwFlags & WHDR_INQUEUE),
          "queued");
    check(waveOutClose(hw) == WAVERR_STILLPLAYING, "stillplay");
    check(waveOutUnprepareHeader(hw, &wh[2], sizeof(WAVEHDR))
          == WAVERR_STILLPLAYING, "unprepq");
    wompump(hw, &opens, &dones, &closes, 3, 5000);
    played = timeGetTime() - t0;
    check(opens == 1, "womopen");
    check(dones == 3, "womdone");
    for (k = 0; k < 3; k++)
        check((wh[k].dwFlags & WHDR_DONE) && !(wh[k].dwFlags & WHDR_INQUEUE),
              "done");
    /* 816 ms of sound: QEMU's SB16 takes it in about 490 ms of guest time,
     * so this asks only that a block is not done when it is written */
    check(played >= 400, "quick");
    mt.wType = TIME_BYTES;
    check(waveOutGetPosition(hw, &mt, sizeof mt) == 0 && mt.wType == TIME_BYTES
          && mt.u.cb >= 3L * NTONE, "posb");
    mt.wType = TIME_MS;
    check(waveOutGetPosition(hw, &mt, sizeof mt) == 0 && mt.u.ms >= 810
          && mt.u.ms < 1300, "posms");
    mt.wType = TIME_SMPTE;
    check(waveOutGetPosition(hw, &mt, sizeof mt) == 0 && mt.wType == TIME_BYTES,
          "possmpte");
    for (k = 0; k < 3; k++)
        check(waveOutUnprepareHeader(hw, &wh[k], sizeof(WAVEHDR)) == 0
              && !(wh[k].dwFlags & WHDR_PREPARED), "unprepare");
    check(waveOutClose(hw) == 0, "woclose");
    wompump(hw, &opens, &dones, &closes, 4, 1000);
    check(closes == 1, "womclose");

    /* sndPlaySound: the RIFF file mmio wrote, played to its end */
    t0 = timeGetTime();
    check(sndPlaySound("MMTEST.WAV", SND_SYNC | SND_NODEFAULT), "sndfile");
    check(timeGetTime() - t0 >= 80, "sndsync");     /* 91 ms of it */
    check(!sndPlaySound("NOFILE.WAV", SND_SYNC | SND_NODEFAULT), "sndnofile");
    /* ...one in memory, looped, not stopped by NOSTOP, then stopped */
    memcpy(wav, "RIFF", 4);
    put4(wav + 4, 36 + NWAV);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put4(wav + 16, 16);
    memcpy(wav + 20, &pf, 16);
    memcpy(wav + 36, "data", 4);
    put4(wav + 40, NWAV);
    memcpy(wav + 44, tone, NWAV);
    check(sndPlaySound((LPCSTR)wav, SND_MEMORY | SND_ASYNC | SND_LOOP),
          "sndloop");
    check(!sndPlaySound((LPCSTR)wav, SND_MEMORY | SND_ASYNC | SND_NOSTOP),
          "sndnostop");
    check(waveOutOpen(&h2, 0, (LPWAVEFORMAT)&pf, 0, 0, 0)
          == MMSYSERR_ALLOCATED, "sndbusy");
    t0 = timeGetTime();
    while (timeGetTime() - t0 < 300)    /* three passes and a bit */
        PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE);
    check(sndPlaySound(NULL, 0), "sndstop");
    r = waveOutOpen(&h2, 0, (LPWAVEFORMAT)&pf, 0, 0, 0);
    check(r == 0, "sndfree");
    if (r == 0)
        waveOutClose(h2);
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

    waveout(n);

    /* what a missing file says, and EXIST and DELETE */
    memset(&mi, 0, sizeof mi);
    check(mmioOpen("NOFILE.WAV", &mi, MMIO_READ) == 0
          && mi.wErrorRet == MMIOERR_CANNOTOPEN, "nofile");
    check(mmioOpen("MMTEST.WAV", NULL, MMIO_EXIST) != 0, "exist");
    check(mmioOpen("MMTEST.WAV", NULL, MMIO_DELETE) != 0, "delete");
    check(mmioOpen("MMTEST.WAV", NULL, MMIO_EXIST) == 0, "gone");

    sprintf(res + strlen(res),
            "%smmtest: %d wrong, waveOut %u, step %lu ms, played %lu ms",
            fails ? " - " : "", fails, n, best, played);
    MessageBox(0, res, "WIN16 mmtest", MB_OK);
    return 0;
}
