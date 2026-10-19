"""RunCPM's screenshots, for tools/guide/manuals/RUNCPM_O88.md.

Taken from the 1.44MB RunCPM disk, which carries the games and applications
as well as the master disk:

    python3 tools/guide/capture/run.py RUNCPM_O88 --machine os8088_xt_vga_144 \\
        --image build/os8088.img --apps build/runcpm.img
"""
import os88marty

TITLE = "RunCPM"


def slow(ui, s, gap=0.5):
    for ch in s:
        ui.m.type_text(ch)
        os88marty.guest_sleep(ui.m, gap)


def capture(ui, shot):
    ui.path("B:/RUNCPM.O88")
    print("menus:", ui.menus())
    ui.mo.to(620, 470)
    os88marty.guest_sleep(ui.m, 40)          # the banner and its clock estimate
    slow(ui, "DIR *.COM\n")
    os88marty.guest_sleep(ui.m, 30)
    shot("runcpm-window", window=TITLE, pad=4)

    slow(ui, "MBASIC\n")
    os88marty.guest_sleep(ui.m, 30)
    slow(ui, '10 FOR I=1 TO 5:PRINT I,I*I:NEXT\n')
    slow(ui, "RUN\n")
    os88marty.guest_sleep(ui.m, 15)
    shot("runcpm-mbasic", window=TITLE, pad=4)
    slow(ui, "SYSTEM\n")
    os88marty.guest_sleep(ui.m, 15)

    slow(ui, "USER 5\n")
    os88marty.guest_sleep(ui.m, 10)
    slow(ui, "DIR\n")
    os88marty.guest_sleep(ui.m, 20)
    shot("runcpm-user", window=TITLE, pad=4)

    slow(ui, "USER 8\n")
    os88marty.guest_sleep(ui.m, 10)
    slow(ui, "TURBO\n")
    os88marty.guest_sleep(ui.m, 60)
    shot("runcpm-turbo", window=TITLE, pad=4)
