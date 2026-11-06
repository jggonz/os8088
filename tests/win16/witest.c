/* witest.c - WIN16 fixture (SPEC.md 124.32.5, docs/plans/WIN16-ANY-PLAN.md
 * S23 batch 4): waveIn, checked against what the program can see.
 *
 * - The device: waveInGetDevCaps agrees with waveInGetNumDevs and offers
 *   8-bit mono at 11 kHz alone; WAVE_FORMAT_QUERY takes that and refuses
 *   22 kHz and 16 bits.
 * - An open with CALLBACK_TASK, its MM_WIM_OPEN, and the refusals while it
 *   is open: a second waveInOpen, and waveOutOpen (the card is half-duplex).
 * - Two blocks queued, the refusals on the way, a start, up to two seconds
 *   for them to fill, a stop and a reset: each block comes back done exactly
 *   once with its MM_WIM_DATA, holding no more than its length, and the
 *   position is 0 after the reset. Then the close and its MM_WIM_CLOSE, and
 *   the output device free again.
 *
 * QEMU's SB16 never sends a capture interrupt, so there nothing is recorded
 * and both blocks come back from the reset; on a card that records they
 * come back full. The box says how much was recorded either way. In real
 * mode it says so and checks nothing: MMSYSTEM is the protected-mode arm's.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define NBUF 4096                   /* 372 ms at 11,025 Hz */

static char res[256];
static int fails;
static unsigned char buf[2][NBUF];
static WAVEHDR wh[2];
static int opens, datas, closes, order;

static void check(int ok, const char *tag)
{
    if (ok)
        return;
    fails++;
    if (strlen(res) + strlen(tag) + 2 < sizeof res) {
        strcat(res, " ");
        strcat(res, tag);
    }
}

/* the MM_WIM_* messages posted to this task, until want blocks or ms */
static void pump(HWAVEIN hw, int want, DWORD ms)
{
    MSG msg;
    DWORD t0 = timeGetTime();

    while (datas < want && timeGetTime() - t0 < ms) {
        if (!PeekMessage(&msg, 0, MM_WIM_OPEN, MM_WIM_DATA, PM_REMOVE))
            continue;
        if (msg.message == MM_WIM_OPEN)
            opens++;
        else if (msg.message == MM_WIM_CLOSE)
            closes++;
        else if (msg.message == MM_WIM_DATA) {
            check(msg.wParam == (WPARAM)hw
                  && (WAVEHDR FAR *)msg.lParam == (WAVEHDR FAR *)&wh[datas],
                  "datamsg");
            datas++;
        }
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    PCMWAVEFORMAT pf;
    WAVEINCAPS wic;
    HWAVEIN hw = 0, h2;
    HWAVEOUT ho;
    MMTIME mt;
    char s[80];
    DWORD rec = 0, cap = 0;
    UINT n, r, st = 99;
    int k;

    if (!(GetWinFlags() & WF_PMODE)) {
        MessageBox(0, "witest: real mode - MMSYSTEM is the arm's",
                   "WIN16 witest", MB_OK);
        return 0;
    }
    pf.wf.wFormatTag = WAVE_FORMAT_PCM;
    pf.wf.nChannels = 1;
    pf.wf.nSamplesPerSec = 11025;
    pf.wf.nAvgBytesPerSec = 11025;
    pf.wf.nBlockAlign = 1;
    pf.wBitsPerSample = 8;

    n = waveInGetNumDevs();
    r = waveInGetDevCaps(0, &wic, sizeof wic);
    check(n ? r == 0 : r == MMSYSERR_BADDEVICEID, "caps");
    if (n && r == 0)
        check(wic.dwFormats == WAVE_FORMAT_1M08 && wic.wChannels == 1,
              "fmts");
    check(waveInGetErrorText(WAVERR_UNPREPARED, s, sizeof s) == 0 && s[0],
          "errtext");
    if (!n) {
        check(waveInOpen(&hw, 0, (LPWAVEFORMAT)&pf, 0, 0, 0)
              == MMSYSERR_BADDEVICEID, "winone");
        goto done;
    }
    check(waveInOpen(NULL, 0, (LPWAVEFORMAT)&pf, 0, 0, WAVE_FORMAT_QUERY)
          == 0, "query");
    pf.wf.nSamplesPerSec = 22050;
    check(waveInOpen(NULL, 0, (LPWAVEFORMAT)&pf, 0, 0, WAVE_FORMAT_QUERY)
          == WAVERR_BADFORMAT, "q22");
    pf.wf.nSamplesPerSec = 11025;
    pf.wBitsPerSample = 16;
    check(waveInOpen(NULL, 0, (LPWAVEFORMAT)&pf, 0, 0, WAVE_FORMAT_QUERY)
          == WAVERR_BADFORMAT, "q16");
    pf.wBitsPerSample = 8;

    r = waveInOpen(&hw, WAVE_MAPPER, (LPWAVEFORMAT)&pf,
                   (DWORD)(UINT)GetCurrentTask(), 0, CALLBACK_TASK);
    check(r == 0 && hw != 0, "wiopen");
    if (r != 0)
        goto done;
    check(waveInOpen(&h2, 0, (LPWAVEFORMAT)&pf, 0, 0, 0)
          == MMSYSERR_ALLOCATED, "alloc");
    check(waveOutOpen(&ho, 0, (LPWAVEFORMAT)&pf, 0, 0, 0)
          == MMSYSERR_ALLOCATED, "duplex");
    check(waveInGetID(hw, &r) == 0 && r == 0, "id");
    for (k = 0; k < 2; k++) {
        memset(&wh[k], 0, sizeof wh[k]);
        memset(buf[k], 0x55, NBUF);
        wh[k].lpData = (LPSTR)buf[k];
        wh[k].dwBufferLength = NBUF;
        wh[k].dwBytesRecorded = 12345;
    }
    check(waveInAddBuffer(hw, &wh[0], sizeof(WAVEHDR)) == WAVERR_UNPREPARED,
          "unprep");
    for (k = 0; k < 2; k++)
        check(waveInPrepareHeader(hw, &wh[k], sizeof(WAVEHDR)) == 0
              && (wh[k].dwFlags & WHDR_PREPARED), "prep");
    for (k = 0; k < 2; k++)
        check(waveInAddBuffer(hw, &wh[k], sizeof(WAVEHDR)) == 0
              && wh[k].dwBytesRecorded == 0, "add");
    check(waveInAddBuffer(hw, &wh[1], sizeof(WAVEHDR))
          == WAVERR_STILLPLAYING, "addq");
    check((wh[1].dwFlags & WHDR_INQUEUE) && !(wh[1].dwFlags & WHDR_DONE),
          "queued");
    check(waveInClose(hw) == WAVERR_STILLPLAYING, "stillrec");
    check(waveInUnprepareHeader(hw, &wh[1], sizeof(WAVEHDR))
          == WAVERR_STILLPLAYING, "unprepq");

    st = waveInStart(hw);
    check(st == 0 || st == MMSYSERR_ERROR, "start");
    pump(hw, 2, 2000);
    mt.wType = TIME_BYTES;
    check(waveInGetPosition(hw, &mt, sizeof mt) == 0
          && mt.wType == TIME_BYTES, "pos");
    cap = mt.u.cb;
    mt.wType = TIME_MS;
    check(waveInGetPosition(hw, &mt, sizeof mt) == 0
          && mt.u.ms <= cap * 1000 / 11025 + 1, "posms");
    check(waveInStop(hw) == 0, "stop");
    check(waveInReset(hw) == 0, "reset");
    pump(hw, 2, 1000);
    check(opens == 1, "wimopen");
    check(datas == 2, "wimdata");
    for (k = 0; k < 2; k++) {
        check((wh[k].dwFlags & WHDR_DONE) && !(wh[k].dwFlags & WHDR_INQUEUE)
              && wh[k].dwBytesRecorded <= NBUF, "done");
        rec += wh[k].dwBytesRecorded;
    }
    /* a block is filled before the next one is begun */
    check(wh[1].dwBytesRecorded == 0 || wh[0].dwBytesRecorded == NBUF,
          "order");
    mt.wType = TIME_BYTES;
    check(waveInGetPosition(hw, &mt, sizeof mt) == 0 && mt.u.cb == 0,
          "pos0");
    for (k = 0; k < 2; k++)
        check(waveInUnprepareHeader(hw, &wh[k], sizeof(WAVEHDR)) == 0
              && !(wh[k].dwFlags & WHDR_PREPARED), "unprepare");
    check(waveInClose(hw) == 0, "wiclose");
    pump(hw, 3, 300);
    check(closes == 1, "wimclose");
    check(waveInStart(hw) == MMSYSERR_INVALHANDLE, "gone");
    if (waveOutGetNumDevs()) {
        r = waveOutOpen(&ho, 0, (LPWAVEFORMAT)&pf, 0, 0, 0);
        check(r == 0, "wofree");
        if (r == 0)
            waveOutClose(ho);
    }
done:
    sprintf(res + strlen(res),
            "%switest: %d wrong, waveIn %u, start %u, captured %lu, "
            "recorded %lu", fails ? " - " : "", fails, n, st, cap, rec);
    MessageBox(0, res, "WIN16 witest", MB_OK);
    return 0;
}
