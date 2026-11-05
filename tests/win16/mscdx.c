/* mscdx.c - WIN16 fixture (SPEC.md 124.33.3, docs/plans/WIN16-NEXT-PLAN.md
 * Q3): what a CD-ROM title's installer asks MSCDEX through int 2Fh 15xxh,
 * each answer checked against what the multiplex promises.
 *
 * With a CD drive attached (CDROM.DRV, SPEC.md 119): 1500h counts one drive
 * and names it, 150Bh agrees for that letter and not the next, 150Ch says
 * 2.10, 150Dh writes the drive's NUMBER (0 = A:) in one byte and no more,
 * 1510h is refused (CF, AX = 1), and GetDriveType of the letter is
 * DRIVE_REMOTE. With none: 1500h leaves BX = 0, and 150Bh, 150Ch, 150Dh and
 * 1510h change nothing.
 *
 * The MessageBox is "MSCDX OK n" (n = the CD's letter index, 0 = A:), or
 * "MSCDX NONE" when no drive answered and the no-drive rules held, or
 * "MSCDX FAIL" and each check that failed.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static char res[160];
static unsigned char buf[8];
static unsigned rax, rbx, rcx, rcf;

static void check(int ok, const char *tag)
{
    if (!ok) {
        strcat(res, " ");
        strcat(res, tag);
    }
}

static void list(void)
{
    buf[0] = 0xFF;
    buf[1] = 0xFF;
    _asm {
        push es
        push bx
        mov ax, ds
        mov es, ax
        lea bx, buf
        mov ax, 150Dh
        int 2Fh
        pop bx
        pop es
    }
}

static void ask(unsigned ax, unsigned bx, unsigned cx)
{
    rax = ax; rbx = bx; rcx = cx;
    _asm {
        push es
        mov ax, rax
        mov bx, rbx
        mov cx, rcx
        int 2Fh
        sbb dx, dx
        mov rcf, dx
        mov rax, ax
        mov rbx, bx
        mov rcx, cx
        pop es
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    unsigned n, first;

    (void)inst; (void)prev; (void)cmd; (void)show;
    res[0] = 0;
    ask(0x1500, 0, 0);
    n = rbx;
    first = rcx;
    if (n == 0) {
        check(rcx == 0, "none:cx");
        ask(0x150B, 0, 2);
        check(rax == 0x150B && rbx == 0 && rcx == 2, "none:150B");
        ask(0x150C, 0, 0);
        check(rax == 0x150C && rbx == 0, "none:150C");
        list();
        check(buf[0] == 0xFF && buf[1] == 0xFF, "none:150D");
        ask(0x1510, 0, 3);
        check(rax == 0x1510 && rbx == 0 && rcx == 3, "none:1510");
        if (res[0])
            strcpy(res, "MSCDX FAIL");
        else
            strcpy(res, "MSCDX NONE");
        MessageBox(0, res, "WIN16 mscdx", MB_OK);
        return 0;
    }
    check(n == 1, "count");
    ask(0x150B, 0, first);
    check(rbx == 0xADAD && rax != 0, "150B:cd");
    ask(0x150B, 0, first + 1);
    check(rbx == 0xADAD && rax == 0, "150B:other");
    ask(0x150C, 0, 0);
    check(rbx == 0x020A, "150C");
    list();
    check(buf[0] == first && buf[1] == 0xFF, "150D");
    ask(0x1510, 0, first);
    check(rcf != 0 && rax == 1, "1510");
    check(GetDriveType(first) == DRIVE_REMOTE, "drivetype");
    if (res[0]) {
        memmove(res + 11, res, strlen(res) + 1);
        memcpy(res, "MSCDX FAIL:", 11);
    } else {
        sprintf(res, "MSCDX OK %u", first);
    }
    MessageBox(0, res, "WIN16 mscdx", MB_OK);
    return 0;
}
