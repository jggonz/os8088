/* tmtest.c - WIN16 fixture (SPEC.md 124.32.6, docs/plans/WIN16-ANY-PLAN.md
 * S23 batch 4): timeSetEvent and CALLBACK_FUNCTION, the calls MMSYSTEM owes a
 * program and makes where it waits for a message.
 *
 * - The refusals: a delay of 0, a NULL procedure, and timeKillEvent of an ID
 *   that is not an event.
 * - A periodic event at 50 ms and a one-shot one at 150 ms, met in a
 *   PeekMessage loop: each call has its own ID, uMsg 0 and its dwUser; the
 *   one-shot one comes once and its ID dies with it.
 * - The same periodic event met while the program waits in GetMessage: its
 *   third call posts the message that ends the wait (a call already due
 *   when it returns is made first).
 * - Killed, it calls no more.
 * - With a wave device, waveOutOpen with CALLBACK_FUNCTION: WOM_OPEN, a
 *   block's WOM_DONE with the WAVEHDR, and WOM_CLOSE, each with the open's
 *   dwInstance; and waveInOpen's WIM_OPEN, two WIM_DATA and WIM_CLOSE.
 *
 * Every check that fails is named in the message box. In real mode it says
 * so and checks nothing: MMSYSTEM is the protected-mode arm's.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define NTONE 2205                  /* 200 ms at 11,025 Hz */

static char res[256];
static int fails;
static UINT idp, ido, nwait;
static volatile int np, no, bad, nested, inproc;
static volatile int wopen, wdone, wclose, iopen, idata, iclose;
static volatile DWORD whdr;
static unsigned char tone[NTONE];
static WAVEHDR wh[2];

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

void FAR PASCAL _export pcb(UINT id, UINT msg, DWORD user, DWORD d1, DWORD d2)
{
    if (inproc)
        nested++;
    if (id != idp || msg != 0 || user != 0x12345678L || d1 || d2)
        bad++;
    np++;
    if (nwait && np == nwait)
        PostAppMessage(GetCurrentTask(), WM_USER + 1, 0, 0);
}

void FAR PASCAL _export ocb(UINT id, UINT msg, DWORD user, DWORD d1, DWORD d2)
{
    if (id != ido || msg != 0 || user != 0xABCDL || d1 || d2)
        bad++;
    no++;
}

void FAR PASCAL _export wcb(HWAVEOUT h, UINT msg, DWORD user, DWORD d1,
                            DWORD d2)
{
    if (user != 0xBEEFL || d2)
        bad++;
    if (msg == MM_WOM_OPEN)
        wopen++;
    else if (msg == MM_WOM_DONE) {
        wdone++;
        whdr = d1;
    } else if (msg == MM_WOM_CLOSE)
        wclose++;
}

void FAR PASCAL _export icb(HWAVEIN h, UINT msg, DWORD user, DWORD d1,
                            DWORD d2)
{
    if (user != 0xFEEDL || d2)
        bad++;
    if (msg == MM_WIM_OPEN)
        iopen++;
    else if (msg == MM_WIM_DATA)
        idata++;
    else if (msg == MM_WIM_CLOSE)
        iclose++;
}

/* PeekMessage for ms, or until *until reaches want */
static void peek(DWORD ms, volatile int *until, int want)
{
    MSG msg;
    DWORD t0 = timeGetTime();

    while (timeGetTime() - t0 < ms && (!until || *until < want))
        PeekMessage(&msg, 0, 0, 0, PM_REMOVE);
}

static void waves(void)
{
    PCMWAVEFORMAT pf;
    HWAVEOUT ho;
    HWAVEIN hi;
    UINT r;
    int i, k;

    pf.wf.wFormatTag = WAVE_FORMAT_PCM;
    pf.wf.nChannels = 1;
    pf.wf.nSamplesPerSec = 11025;
    pf.wf.nAvgBytesPerSec = 11025;
    pf.wf.nBlockAlign = 1;
    pf.wBitsPerSample = 8;
    if (waveOutGetNumDevs()) {
        for (i = 0; i < NTONE; i++)
            tone[i] = (unsigned char)(((i / 6) & 1) ? 0xC0 : 0x40);
        r = waveOutOpen(&ho, 0, (LPWAVEFORMAT)&pf, (DWORD)(FARPROC)wcb,
                        0xBEEFL, CALLBACK_FUNCTION);
        check(r == 0, "woopen");
        if (r == 0) {
            memset(&wh[0], 0, sizeof wh[0]);
            wh[0].lpData = (LPSTR)tone;
            wh[0].dwBufferLength = NTONE;
            waveOutPrepareHeader(ho, &wh[0], sizeof(WAVEHDR));
            waveOutWrite(ho, &wh[0], sizeof(WAVEHDR));
            peek(3000, &wdone, 1);
            check(wopen == 1, "wopen");
            check(wdone == 1 && whdr == (DWORD)(LPWAVEHDR)&wh[0], "wdone");
            waveOutUnprepareHeader(ho, &wh[0], sizeof(WAVEHDR));
            check(waveOutClose(ho) == 0, "woclose");
            peek(500, &wclose, 1);
            check(wclose == 1, "wclose");
        }
    }
    if (waveInGetNumDevs()) {
        r = waveInOpen(&hi, 0, (LPWAVEFORMAT)&pf, (DWORD)(FARPROC)icb,
                       0xFEEDL, CALLBACK_FUNCTION);
        check(r == 0, "wiopen");
        if (r == 0) {
            for (k = 0; k < 2; k++) {
                memset(&wh[k], 0, sizeof wh[k]);
                wh[k].lpData = (LPSTR)tone;
                wh[k].dwBufferLength = 512;
                waveInPrepareHeader(hi, &wh[k], sizeof(WAVEHDR));
                waveInAddBuffer(hi, &wh[k], sizeof(WAVEHDR));
            }
            waveInStart(hi);
            peek(300, NULL, 0);
            waveInReset(hi);
            peek(500, &idata, 2);
            check(iopen == 1, "iopen");
            check(idata == 2, "idata");
            for (k = 0; k < 2; k++)
                waveInUnprepareHeader(hi, &wh[k], sizeof(WAVEHDR));
            check(waveInClose(hi) == 0, "wiclose");
            peek(500, &iclose, 1);
            check(iclose == 1, "iclose");
        }
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    MSG msg;
    DWORD t0, waited;
    int n1;

    if (!(GetWinFlags() & WF_PMODE)) {
        MessageBox(0, "tmtest: real mode - MMSYSTEM is the arm's",
                   "WIN16 tmtest", MB_OK);
        return 0;
    }
    check(timeSetEvent(0, 10, (LPTIMECALLBACK)pcb, 0, TIME_PERIODIC) == 0,
          "delay0");
    check(timeSetEvent(50, 10, NULL, 0, TIME_PERIODIC) == 0, "nullproc");
    check(timeKillEvent(999) == TIMERR_NOCANDO, "killbad");

    idp = timeSetEvent(50, 10, (LPTIMECALLBACK)pcb, 0x12345678L,
                       TIME_PERIODIC);
    ido = timeSetEvent(150, 10, (LPTIMECALLBACK)ocb, 0xABCDL, TIME_ONESHOT);
    check(idp != 0 && ido != 0 && idp != ido, "ids");
    check(np == 0 && no == 0, "early");     /* nothing until it waits */
    peek(600, NULL, 0);
    check(np >= 6 && np <= 14, "periodic");
    check(no == 1, "oneshot");
    check(timeKillEvent(ido) == TIMERR_NOCANDO, "killfired");

    /* ...and while it waits in GetMessage: the third call ends the wait */
    n1 = np;
    nwait = np + 3;
    t0 = timeGetTime();
    while (GetMessage(&msg, 0, 0, 0))
        if (msg.message == WM_USER + 1)
            break;
    waited = timeGetTime() - t0;
    /* a call due when the third returns comes before the message */
    check(np >= n1 + 3 && np <= n1 + 5 && waited >= 100, "getmsg");
    nwait = 0;
    check(bad == 0 && nested == 0, "args");

    check(timeKillEvent(idp) == 0, "kill");
    n1 = np;
    peek(250, NULL, 0);
    check(np == n1, "killed");

    waves();
    check(bad == 0, "wargs");

    sprintf(res + strlen(res),
            "%stmtest: %d wrong, periodic %d, waited %lu ms, wave %d/%d/%d, "
            "in %d/%d/%d", fails ? " - " : "", fails, np, waited, wopen,
            wdone, wclose, iopen, idata, iclose);
    MessageBox(0, res, "WIN16 tmtest", MB_OK);
    return 0;
}
