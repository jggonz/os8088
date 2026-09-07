#!/usr/bin/env python3
"""Drive the desktop to NITEST.O88 and wait for it to finish.

apps/infones/hosttest/nisystest.sh's other half. A package has to be
double-clicked - there is no autostart on this OS - and a double-click is ONE
PROCESS (LESSONS.md 10): two `tools/mouse.py click` invocations decode as two
single clicks, because the launch needs two presses inside the kernel's
nine-tick window and two python starts do not fit in it.
"""
import os
import sys
import time

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
sys.path.insert(0, "tools")
sys.argv = ["mouse.py", "build/qmp.sock", "to", "0", "0"]
import mouse as M                                       # noqa: E402

SOCK = "build/qmp.sock"


def m(*a):
    M.ARGV = [SOCK] + [str(x) for x in a]
    M.main()


def dbl(x, y):
    m("to", x, y)
    M.hmp("mouse_button 1")
    time.sleep(0.08)
    M.hmp("mouse_button 0")
    time.sleep(0.10)
    M.hmp("mouse_button 1")
    time.sleep(0.08)
    M.hmp("mouse_button 0")
    time.sleep(0.6)


def shot(path):
    os.system("python3 tools/shot.py %s %s >/dev/null" % (SOCK, path))


dbl(601, 110)                   # the B: drive icon
time.sleep(3)
dbl(150, 128)                   # the NITEST folder, first row of the listing
time.sleep(4)
dbl(175, 144)                   # NITEST.O88, the first file in it
time.sleep(6)
shot("build/port-shots/nisystest-launch.png")

# The run is bounded by the PACKAGE's own frame cap (NI_TEST_CAP, 2,400
# emulated frames), so this waits rather than polls: NIRES.TXT is not readable
# from here until QEMU has been stopped and the image flushed, and the shell
# script does that after this returns. NIWAIT lets a loaded host have longer.
wait = int(os.environ.get("NIWAIT", "150"))
for i in range(wait // 10):
    time.sleep(10)
    shot("build/port-shots/nisystest.png")
print("nisysdrive: %d s waited; the shell script reads NIRES.TXT off the "
      "image" % (wait // 10 * 10))
