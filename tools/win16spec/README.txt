tools/win16spec - the Win16 export tables WIN16 names imports from
(SPEC.md 124, docs/plans/WIN16-PLAN.md S1)

Each `<module>.<ext>16.spec` file lists one Windows 3.x module's exports:
ordinal, calling convention, name and argument types. tools/os88ne.py reads
them to turn `USER.1` into `MessageBox`, and S2's loader build reads them for
the refusal that names a missing import and for the argument bytes a pascal
call pops.

PROVENANCE. Copied VERBATIM, never edited, from winevdm
(github.com/otya128/winevdm) at commit
b88d7f8736d77baa5710eb0c35f81b489540c1a0:

    krnl386/krnl386.exe16.spec    KERNEL (Wine's name for it; the census
                                  reads it as KERNEL)
    user/user.exe16.spec          USER
    gdi/gdi.exe16.spec            GDI
    sound/sound.drv16.spec        SOUND
    win87em/win87em.dll16.spec    WIN87EM
    keyboard/keyboard.drv16.spec  KEYBOARD
    shell/shell.dll16.spec        SHELL
    system/system.drv16.spec      SYSTEM
    toolhelp/toolhelp.dll16.spec  TOOLHELP
    commdlg/commdlg.dll16.spec    COMMDLG
    mmsystem/mmsystem.dll16.spec  MMSYSTEM

winevdm's tables are Wine's (winehq.org, LGPL-2.1-or-later), extended.
winevdm as a whole is distributed under the GNU General Public License,
version 2; its LICENSE is the GPL-2 text. These files are therefore under
the GPL-2, which is the footing apps/win16/ takes when it lands
(GPL-2.0-or-later, WIN16-PLAN.md D7). Nothing else in tools/ is affected:
the tables are data the tools read, not code they include.

To move the pin, copy the same eleven files from a newer winevdm checkout,
change the commit above, and run `python3 tests/unit/t_win16ne.py`.
