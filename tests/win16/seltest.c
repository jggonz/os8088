/* seltest.c - WIN16 fixture (SPEC.md 124.31.6, docs/plans/WIN16-ANY-PLAN.md
 * S22): what standard mode's KERNEL gave a program that makes its own
 * selectors - GetWinFlags' WF_PMODE, AllocSelector and its base and limit,
 * PrestoChangoSelector, the two alias calls, GlobalDosAlloc, and the DPMI
 * subset on int 31h - each checked against what the program can see for
 * itself.
 *
 * Every check that fails is named in the message box. In real mode, on any
 * kernel but kern_mega, it says so and checks nothing: those calls are
 * protected mode's.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>

UINT WINAPI AllocCStoDSAlias(UINT);

static char res[256];
static int fails;
static char probe[32] = "WIN16 seltest probe 0123456789";

static void check(int ok, const char *tag)
{
    if (!ok) {
        fails++;
        strcat(res, " ");
        strcat(res, tag);
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    DWORD flags = GetWinFlags();
    UINT ds, cs, s, a;
    DWORD base, dos;
    char __far *p;
    unsigned char __far *c;
    unsigned char __far *d;

    (void)inst; (void)prev; (void)cmd; (void)show;
    if (!(flags & WF_PMODE)) {
        sprintf(res, "seltest: real mode (flags %04lX), nothing to check", flags);
        MessageBox(0, res, "WIN16 seltest", MB_OK);
        return 0;
    }
    ds = FP_SEG((void __far *)probe);
    cs = FP_SEG((void (__far *)(void))WinMain);
    base = GetSelectorBase(ds);
    check(base != 0, "base");
    check(GetSelectorLimit(ds) >= FP_OFF((void __far *)probe) + sizeof probe - 1,
          "limit");

    s = AllocSelector(ds);
    check(s != 0, "alloc");
    if (s) {
        check(GetSelectorBase(s) == base, "copy");
        p = MK_FP(s, FP_OFF((void __far *)probe));
        check(_fmemcmp(p, probe, sizeof probe) == 0, "read");
        SetSelectorBase(s, base + 16);
        check(GetSelectorBase(s) == base + 16, "setbase");
        p = MK_FP(s, FP_OFF((void __far *)probe) - 16);
        check(_fmemcmp(p, probe, sizeof probe) == 0, "aimed");
        SetSelectorLimit(s, 0x0FFF);
        check(GetSelectorLimit(s) == 0x0FFF, "setlim");
        PrestoChangoSelector(ds, s);
        check(GetSelectorBase(s) == base, "presto");
        FreeSelector(s);
    }

    a = AllocCStoDSAlias(cs);
    check(a != 0, "csds");
    if (a) {
        c = MK_FP(cs, FP_OFF((void (__far *)(void))WinMain));
        d = MK_FP(a, FP_OFF((void (__far *)(void))WinMain));
        check(_fmemcmp(c, d, 16) == 0, "csdsread");
        FreeSelector(a);
    }
    a = AllocDStoCSAlias(ds);
    check(a != 0, "dscs");
    if (a) {
        check(GetSelectorBase(a) == base, "dscsbase");
        FreeSelector(a);
    }

    dos = GlobalDosAlloc(1024);
    check(dos != 0, "dosalloc");
    if (dos) {
        p = MK_FP(LOWORD(dos), 0);
        _fmemcpy(p, probe, sizeof probe);
        check(GetSelectorBase(LOWORD(dos)) == (DWORD)HIWORD(dos) << 4, "dosreal");
        check(_fmemcmp(p, probe, sizeof probe) == 0, "doswrite");
        GlobalDosFree(LOWORD(dos));
    }

    /* DPMI 0.9 on int 31h: the version, the increment, a descriptor made,
     * aimed at DS's base, read back and freed, and a data alias of CS */
    {
        static unsigned rax, rcx, rdx, rf, sel;
        static unsigned char desc[8];
        unsigned char __far *dp = desc;
        _asm {
            mov ax, 0400h
            int 31h
            mov rax, ax
        }
        check(rax == 0x005A, "dpmiver");
        _asm {
            mov ax, 0003h
            int 31h
            mov rax, ax
        }
        check(rax == 8, "dpmiincr");
        _asm {
            mov ax, 0006h
            mov bx, ds
            int 31h
            mov rcx, cx
            mov rdx, dx
        }
        check(((DWORD)rcx << 16 | rdx) == base, "dpmibase");
        _asm {
            mov ax, 0000h
            mov cx, 1
            int 31h
            sbb dx, dx
            mov rf, dx
            mov sel, ax
        }
        if (rf != 0 || sel == 0)
            sprintf(res + strlen(res), " dpmialloc=%04X", sel);
        fails += rf != 0 || sel == 0;
        if (rf == 0 && sel != 0) {
            rcx = HIWORD(base);
            rdx = LOWORD(base);
            _asm {
                mov ax, 0007h
                mov bx, sel
                mov cx, rcx
                mov dx, rdx
                int 31h
                sbb dx, dx
                mov rf, dx
            }
            check(rf == 0 && GetSelectorBase(sel) == base, "dpmisetb");
            _asm {
                mov ax, 0008h
                mov bx, sel
                xor cx, cx
                mov dx, 0FFFFh
                int 31h
                sbb dx, dx
                mov rf, dx
            }
            check(rf == 0 && GetSelectorLimit(sel) == 0xFFFF, "dpmisetl");
            _asm {
                push es
                push di
                les di, dp
                mov ax, 000Bh
                mov bx, sel
                int 31h
                pop di
                pop es
            }
            check(desc[2] == (unsigned char)base
                  && desc[3] == (unsigned char)(base >> 8)
                  && desc[4] == (unsigned char)(base >> 16), "dpmidesc");
            p = MK_FP(sel, FP_OFF((void __far *)probe));
            check(_fmemcmp(p, probe, sizeof probe) == 0, "dpmiread");
            _asm {
                mov ax, 0001h
                mov bx, sel
                int 31h
            }
        }
        _asm {
            mov ax, 000Ah
            mov bx, cs
            int 31h
            sbb dx, dx
            mov rf, dx
            mov sel, ax
        }
        if (rf != 0 || sel == 0)
            sprintf(res + strlen(res), " dpmialias=%04X", sel);
        fails += rf != 0 || sel == 0;
        if (rf == 0 && sel != 0) {
            c = MK_FP(cs, FP_OFF((void (__far *)(void))WinMain));
            d = MK_FP(sel, FP_OFF((void (__far *)(void))WinMain));
            check(_fmemcmp(c, d, 16) == 0, "dpmialiasread");
        }
    }

    /* where WIN16 runs: an import's far pointer is into its image, which
     * kern_mega may load above 1 MB (SPEC.md 124.31.3) */
    sprintf(res + strlen(res), "%sseltest: %d wrong, flags %04lX, WIN16 at %08lX",
            fails ? " - " : "", fails, flags,
            GetSelectorBase(FP_SEG((void (__far *)(void))GetWinFlags)));
    MessageBox(0, res, "WIN16 seltest", MB_OK);
    return 0;
}
