/* mcitest.c - WIN16 fixture (SPEC.md 124.32.4, docs/plans/WIN16-ANY-PLAN.md
 * S23 batch 3): MMSYSTEM's MCI, by command and by string, each answer
 * checked against what the program knows of the file it made.
 *
 * - waveaudio: MCITONE.WAV, a second of a square at 11,025 Hz written here,
 *   opened by its extension with mciSendCommand and again by a string under
 *   an alias. Its length in each time format, its mode, capabilities and
 *   product; a seek to either end; a play to 500 ms with a string's notify
 *   (MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL) and one to 300 ms with MCI_WAIT,
 *   when there is a card - MCIERR_WAVE_OUTPUTSUNSUITABLE when there is not.
 *   While a play goes on, waveOutOpen is refused, and a second play of the
 *   other device too.
 * - cdaudio, when CDROM.DRV answers: the disc's tracks, a track's length and
 *   type, a time format, the mode; a play answers MCIERR_HARDWARE where the
 *   drive refuses audio (QEMU's), or plays.
 * - the refusals: an unknown verb, keyword and device, a quote left open,
 *   mciGetErrorString's sentences; close all, after which nothing is open.
 *
 * Every check that fails is named in the message box. In real mode it says
 * so and checks nothing: MMSYSTEM is the protected-mode arm's.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define RATE 11025
#define NDATA RATE                  /* one second, 8-bit mono */

static char res[256];
static int fails;
static unsigned char wav[44 + 512];
static char ans[64];
static HWND me;
static UINT notes, notewp;
static LPARAM notelp;

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

/* a string sent: its error, the answer in ans */
static DWORD say(const char *cmd)
{
    ans[0] = 0;
    return mciSendString(cmd, ans, sizeof ans, me);
}

/* the answer is s */
static void sayis(const char *cmd, const char *s, const char *tag)
{
    DWORD e = say(cmd);

    if (e != 0 || strcmp(ans, s) != 0) {
        fails++;
        sprintf(res + strlen(res), " %s=%lu'%s'", tag, e, ans);
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == MM_MCINOTIFY) {
        notes++;
        notewp = w;
        notelp = l;
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

/* messages pumped until a notice comes or ms pass */
static void pump(DWORD ms)
{
    MSG msg;
    DWORD t0 = timeGetTime();
    UINT n0 = notes;

    while (notes == n0 && timeGetTime() - t0 < ms) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

static int mkwav(void)
{
    HFILE f;
    unsigned char buf[512];
    unsigned i, k;
    UINT ok = 1;

    memcpy(wav, "RIFF", 4);
    *(DWORD *)(wav + 4) = 36 + NDATA;
    memcpy(wav + 8, "WAVEfmt ", 8);
    *(DWORD *)(wav + 16) = 16;
    *(WORD *)(wav + 20) = WAVE_FORMAT_PCM;
    *(WORD *)(wav + 22) = 1;
    *(DWORD *)(wav + 24) = RATE;
    *(DWORD *)(wav + 28) = RATE;
    *(WORD *)(wav + 32) = 1;
    *(WORD *)(wav + 34) = 8;
    memcpy(wav + 36, "data", 4);
    *(DWORD *)(wav + 40) = NDATA;
    f = _lcreat("MCITONE.WAV", 0);
    if (f == HFILE_ERROR)
        return 0;
    ok &= _lwrite(f, (LPCSTR)wav, 44) == 44;
    for (i = 0; i < NDATA; i += sizeof buf) {
        for (k = 0; k < sizeof buf; k++)        /* 919 Hz: 6 high, 6 low */
            buf[k] = ((i + k) / 6) & 1 ? 0xA0 : 0x60;
        k = NDATA - i < sizeof buf ? NDATA - i : sizeof buf;
        ok &= _lwrite(f, (LPCSTR)buf, k) == k;
    }
    _lclose(f);
    return ok;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    DWORD flags = GetWinFlags();
    WNDCLASS wc;
    MCI_OPEN_PARMS op;
    MCI_STATUS_PARMS st;
    MCI_SET_PARMS se;
    MCI_PLAY_PARMS pl;
    MCI_GENERIC_PARMS ge;
    MCI_GETDEVCAPS_PARMS gc;
    UINT id1 = 0, id2 = 0, cdid = 0, hw;
    DWORD e, t0, waited = 0;
    int card, cd = 0;
    char s[200];

    (void)prev; (void)cmd; (void)show;
    if (!(flags & WF_PMODE)) {
        sprintf(res, "mcitest: real mode (flags %04lX), nothing to check", flags);
        MessageBox(0, res, "WIN16 mcitest", MB_OK);
        return 0;
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = "mcitest";
    RegisterClass(&wc);
    me = CreateWindow("mcitest", "mcitest", WS_OVERLAPPED, 0, 0, 10, 10,
                      0, 0, inst, NULL);
    check(me != 0, "wnd");
    card = waveOutGetNumDevs() != 0;
    check(mkwav(), "mkwav");

    /* --- waveaudio by command: opened by its extension */
    memset(&op, 0, sizeof op);
    op.lpstrElementName = "MCITONE.WAV";
    e = mciSendCommand(0, MCI_OPEN, MCI_OPEN_ELEMENT, (DWORD)(LPVOID)&op);
    check(e == 0 && op.wDeviceID != 0, "open");
    id1 = op.wDeviceID;
    st.dwItem = MCI_STATUS_LENGTH;
    e = mciSendCommand(id1, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)(LPVOID)&st);
    check(e == 0 && st.dwReturn >= 999 && st.dwReturn <= 1001, "lenms");
    st.dwItem = MCI_STATUS_MODE;
    e = mciSendCommand(id1, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)(LPVOID)&st);
    check(e == 0 && st.dwReturn == MCI_MODE_STOP, "mode");
    se.dwTimeFormat = MCI_FORMAT_BYTES;
    e = mciSendCommand(id1, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD)(LPVOID)&se);
    st.dwItem = MCI_STATUS_LENGTH;
    e |= mciSendCommand(id1, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)(LPVOID)&st);
    check(e == 0 && st.dwReturn == NDATA, "lenbytes");
    se.dwTimeFormat = MCI_FORMAT_TMSF;
    check(mciSendCommand(id1, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD)(LPVOID)&se)
          == MCIERR_BAD_TIME_FORMAT, "badtf");
    gc.dwItem = MCI_GETDEVCAPS_DEVICE_TYPE;
    e = mciSendCommand(id1, MCI_GETDEVCAPS, MCI_GETDEVCAPS_ITEM, (DWORD)(LPVOID)&gc);
    check(e == 0 && gc.dwReturn == MCI_DEVTYPE_WAVEFORM_AUDIO, "devtype");
    st.dwItem = 0x4003;                 /* MCI_WAVE_STATUS_SAMPLESPERSEC */
    e = mciSendCommand(id1, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)(LPVOID)&st);
    check(e == 0 && st.dwReturn == RATE, "rate");
    check(mciSendCommand(id1 + 7, MCI_STOP, 0, 0) == MCIERR_INVALID_DEVICE_ID,
          "badid");

    /* --- the same file by string, under an alias */
    sayis("open mcitone.wav alias tone", "2", "sopen");
    id2 = mciGetDeviceID("tone");
    check(id2 != 0 && id2 != id1, "getid");
    check(mciGetDeviceID("TONE") == id2, "getidcase");
    sayis("status tone length", "1000", "slen");
    sayis("status tone mode", "stopped", "smode");
    sayis("status tone time format", "milliseconds", "stf");
    sayis("set tone time format samples", "", "sset");
    sayis("status tone length", "11025", "slensmp");
    sayis("set tone time format ms", "", "sset2");
    sayis("capability tone can play", "true", "scap");
    sayis("capability tone can eject", "false", "scap2");
    sayis("capability tone device type", "waveaudio", "scap3");
    sayis("info tone product", "Waveform Audio", "sinfo");
    sayis("seek tone to end", "", "sseek");
    sayis("status tone position", "1000", "spos");
    sayis("seek tone to start", "", "sseek2");
    sayis("status tone position", "0", "spos2");
    sayis("status tone channels", "1", "sch");
    check(say("open mcitone.wav alias tone") == MCIERR_DUPLICATE_ALIAS, "dupalias");

    /* --- plays */
    if (card) {
        t0 = timeGetTime();
        e = say("play tone from 0 to 500 notify");
        check(e == 0, "splay");
        {
            HWAVEOUT hwo;
            PCMWAVEFORMAT pf;
            pf.wf.wFormatTag = WAVE_FORMAT_PCM;
            pf.wf.nChannels = 1;
            pf.wf.nSamplesPerSec = RATE;
            pf.wf.nAvgBytesPerSec = RATE;
            pf.wf.nBlockAlign = 1;
            pf.wBitsPerSample = 8;
            check(waveOutOpen(&hwo, 0, (LPWAVEFORMAT)&pf, 0, 0, 0)
                  == MMSYSERR_ALLOCATED, "woinuse");
        }
        sayis("status tone mode", "playing", "splaying");
        pl.dwFrom = 0;
        check(mciSendCommand(id1, MCI_PLAY, MCI_FROM, (DWORD)(LPVOID)&pl)
              == MCIERR_WAVE_OUTPUTSINUSE, "inuse");
        pump(5000);
        check(notes == 1 && notewp == MCI_NOTIFY_SUCCESSFUL
              && LOWORD(notelp) == id2, "notify");
        check(timeGetTime() - t0 >= 200, "quick");
        sayis("status tone mode", "stopped", "sstopped");
        sayis("status tone position", "500", "spos3");
        se.dwTimeFormat = MCI_FORMAT_MILLISECONDS;
        mciSendCommand(id1, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD)(LPVOID)&se);
        pl.dwFrom = 0;
        pl.dwTo = 300;
        t0 = timeGetTime();
        e = mciSendCommand(id1, MCI_PLAY, MCI_FROM | MCI_TO | MCI_WAIT,
                           (DWORD)(LPVOID)&pl);
        waited = timeGetTime() - t0;
        check(e == 0 && waited >= 120, "wait");
        st.dwItem = MCI_STATUS_POSITION;
        e = mciSendCommand(id1, MCI_STATUS, MCI_STATUS_ITEM, (DWORD)(LPVOID)&st);
        check(e == 0 && st.dwReturn == 300, "waitpos");
        /* a play stopped: its notice aborted */
        notes = 0;
        e = say("play tone notify");
        sayis("stop tone", "", "sstop");
        pump(200);
        check(notes == 1 && notewp == MCI_NOTIFY_ABORTED, "abort");
    } else {
        check(say("play tone") == MCIERR_WAVE_OUTPUTSUNSUITABLE, "nocard");
    }

    /* --- cdaudio, when there is a drive */
    e = say("open cdaudio alias cd");
    if (e == 0) {
        cd = 1;
        cdid = mciGetDeviceID("cd");
        say("status cd media present");
        if (strcmp(ans, "true") == 0) {
            cd = 2;
            check(say("status cd number of tracks") == 0 && ans[0] >= '1', "cdn");
            sayis("status cd time format", "msf", "cdtf");
            sayis("set cd time format tmsf", "", "cdset");
            check(say("status cd length track 1") == 0 && strlen(ans) == 8
                  && ans[2] == ':' && ans[5] == ':', "cdlen");
            check(say("status cd type track 1") == 0
                  && (strcmp(ans, "audio") == 0 || strcmp(ans, "other") == 0),
                  "cdtype");
            check(say("status cd position") == 0 && strlen(ans) == 11, "cdpos");
            e = say("play cd from 2");
            if (e == 0) {
                cd = 3;
                say("stop cd");
            } else
                check(e == MCIERR_HARDWARE, "cdplay");
            sayis("status cd mode", "stopped", "cdmode");
        }
        st.dwItem = MCI_STATUS_READY;
        check(mciSendCommand(cdid, MCI_STATUS, MCI_STATUS_ITEM,
                             (DWORD)(LPVOID)&st) == 0, "cdready");
        sayis("close cd", "", "cdclose");
    } else
        check(e == MCIERR_DEVICE_NOT_INSTALLED, "cdopen");

    /* --- the refusals */
    check(say("frob tone") == MCIERR_UNRECOGNIZED_COMMAND, "ecmd");
    check(say("status tone frobnicate") == MCIERR_UNRECOGNIZED_KEYWORD, "ekey");
    check(say("play nosuch") == MCIERR_INVALID_DEVICE_NAME, "edev");
    check(say("play") == MCIERR_MISSING_DEVICE_NAME, "enodev");
    check(say("open \"mci tone.wav") == MCIERR_NO_CLOSING_QUOTE, "equote");
    check(say("play tone from x") == MCIERR_BAD_INTEGER, "eint");
    check(mciGetErrorString(MCIERR_INVALID_DEVICE_NAME, s, sizeof s)
          && strstr(s, "not open") != NULL, "estr");
    check(!mciGetErrorString(999, s, sizeof s), "estr2");
    sayis("sysinfo all quantity open", "2", "sysopen");
    check(say("sysinfo all quantity") == 0 && ans[0] >= '1', "sysq");

    /* --- close all */
    ge.dwCallback = 0;
    check(mciSendCommand(MCI_ALL_DEVICE_ID, MCI_CLOSE, 0, (DWORD)(LPVOID)&ge) == 0,
          "closeall");
    check(mciGetDeviceID("tone") == 0, "closed");
    check(mciSendCommand(id1, MCI_STOP, 0, 0) == MCIERR_INVALID_DEVICE_ID, "closed1");
    hw = waveOutGetNumDevs();
    (void)hw;
    OpenFile("MCITONE.WAV", (LPOFSTRUCT)wav, OF_DELETE);

    sprintf(res + strlen(res), "%smcitest: %d wrong, wave %d, cd %d, waited %lu ms",
            fails ? " - " : "", fails, card, cd, waited);
    MessageBox(0, res, "WIN16 mcitest", MB_OK);
    return 0;
}
