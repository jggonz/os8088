/* motest.c - WIN16 fixture (SPEC.md 124.32.7, docs/plans/WIN16-ANY-PLAN.md
 * S23 batch 4): midiOut over SOUND.DRV's MPU-401.
 *
 * - Always: midiOutGetErrorText for a MIDI code and an undefined one, and
 *   a call on a handle that is not open answers MMSYSERR_INVALHANDLE.
 * - With no interface (QEMU has none): no device, and caps and an open
 *   answer MMSYSERR_BADDEVICEID.
 * - With one: the caps name it and give all sixteen channels; an open with
 *   CALLBACK_FUNCTION sends MOM_OPEN with its dwInstance, and a second open
 *   answers MMSYSERR_ALLOCATED; a note under running status; a long message
 *   unprepared answers MIDIERR_UNPREPARED, prepared it is sent, MHDR_DONE
 *   and MOM_DONE with the header; midiOutGetID, midiOutReset and the close's
 *   MOM_CLOSE.
 *
 * Every check that fails is named in the message box. In real mode it says
 * so and checks nothing: MMSYSTEM is the protected-mode arm's.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

static char res[256];
static int fails;
static volatile int mopen, mdone, mclose, bad;
static volatile DWORD mhdr;
static char gmon[] = { (char)0xF0, 0x7E, 0x7F, 0x09, 0x01, (char)0xF7 };
static MIDIHDR mh;

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

void FAR PASCAL _export mcb(HMIDIOUT h, UINT msg, DWORD user, DWORD d1,
                            DWORD d2)
{
    if (user != 0xC0DEL || d2)
        bad++;
    if (msg == MM_MOM_OPEN)
        mopen++;
    else if (msg == MM_MOM_DONE) {
        mdone++;
        mhdr = d1;
    } else if (msg == MM_MOM_CLOSE)
        mclose++;
}

/* PeekMessage for ms, or until *until reaches want */
static void peek(DWORD ms, volatile int *until, int want)
{
    MSG msg;
    DWORD t0 = timeGetTime();

    while (timeGetTime() - t0 < ms && (!until || *until < want))
        PeekMessage(&msg, 0, 0, 0, PM_REMOVE);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    MIDIOUTCAPS mc;
    HMIDIOUT h, h2;
    char txt[64];
    UINT devs, r, id;

    if (!(GetWinFlags() & WF_PMODE)) {
        MessageBox(0, "motest: real mode - MMSYSTEM is the arm's",
                   "WIN16 motest", MB_OK);
        return 0;
    }
    check(midiOutGetErrorText(MIDIERR_UNPREPARED, txt, sizeof txt) == 0 &&
          strcmp(txt, "The MIDI header was not prepared.") == 0, "errtext");
    check(midiOutGetErrorText(200, txt, sizeof txt) == MMSYSERR_BADERRNUM,
          "errbad");
    check(midiOutShortMsg((HMIDIOUT)0x1234, 0x403C90L) ==
          MMSYSERR_INVALHANDLE, "badh");
    check(midiOutClose((HMIDIOUT)0) == MMSYSERR_INVALHANDLE, "closebad");
    check(midiOutGetVolume((HMIDIOUT)0, NULL) == MMSYSERR_NOTSUPPORTED,
          "volume");

    devs = midiOutGetNumDevs();
    check(devs <= 1, "numdevs");
    if (!devs) {
        check(midiOutGetDevCaps(0, &mc, sizeof mc) == MMSYSERR_BADDEVICEID,
              "capsnone");
        check(midiOutOpen(&h, 0, 0, 0, 0) == MMSYSERR_BADDEVICEID,
              "opennone");
    } else {
        check(midiOutGetDevCaps(0, &mc, sizeof mc) == 0 &&
              strcmp(mc.szPname, "os8088 MPU-401") == 0 &&
              mc.wTechnology == MOD_MIDIPORT && mc.wChannelMask == 0xFFFF,
              "caps");
        check(midiOutGetDevCaps(1, &mc, sizeof mc) == MMSYSERR_BADDEVICEID,
              "caps1");
        r = midiOutOpen(&h, 0, (DWORD)(FARPROC)mcb, 0xC0DEL,
                        CALLBACK_FUNCTION);
        check(r == 0, "open");
        if (r == 0) {
            check(midiOutOpen(&h2, 0, 0, 0, 0) == MMSYSERR_ALLOCATED,
                  "open2");
            peek(500, &mopen, 1);
            check(mopen == 1, "mopen");
            /* middle C on, its off under running status, a program */
            check(midiOutShortMsg(h, 0x403C90L) == 0, "noteon");
            peek(200, NULL, 0);
            check(midiOutShortMsg(h, 0x003CL) == 0, "running");
            check(midiOutShortMsg(h, 0x0BC0L) == 0, "program");
            mh.lpData = gmon;
            mh.dwBufferLength = sizeof gmon;
            mh.dwFlags = 0;
            check(midiOutLongMsg(h, &mh, sizeof mh) == MIDIERR_UNPREPARED,
                  "unprep");
            check(midiOutPrepareHeader(h, &mh, sizeof mh) == 0 &&
                  (mh.dwFlags & MHDR_PREPARED), "prep");
            check(midiOutLongMsg(h, &mh, sizeof mh) == 0 &&
                  (mh.dwFlags & MHDR_DONE), "long");
            peek(500, &mdone, 1);
            check(mdone == 1 && mhdr == (DWORD)(LPMIDIHDR)&mh, "mdone");
            check(midiOutUnprepareHeader(h, &mh, sizeof mh) == 0 &&
                  !(mh.dwFlags & MHDR_PREPARED), "unprepare");
            id = 99;
            check(midiOutGetID(h, &id) == 0 && id == 0, "getid");
            check(midiOutReset(h) == 0, "reset");
            check(midiOutClose(h) == 0, "close");
            peek(500, &mclose, 1);
            check(mclose == 1, "mclose");
            check(midiOutShortMsg(h, 0x403C90L) == MMSYSERR_INVALHANDLE,
                  "closed");
            check(bad == 0, "args");
        }
    }

    sprintf(res + strlen(res), "%smotest: %d wrong, midiOut %u, %d/%d/%d",
            fails ? " - " : "", fails, devs, mopen, mdone, mclose);
    MessageBox(0, res, "WIN16 motest", MB_OK);
    return 0;
}
