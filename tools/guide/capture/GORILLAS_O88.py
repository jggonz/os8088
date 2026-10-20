"""Gorillas' screenshots, for tools/guide/manuals/GORILLAS_O88.md.

Run with --apps build/games360.img: Gorillas is at the root of the games
disk. The game's state is read out of its own memory with the probe
tests/gorillas.py uses, so each step waits for the screen it needs rather
than for a guessed time. One player against the computer.
"""
import os
import random
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "..", "tests"))
import gorillas as grt  # noqa: E402

AIM, FLIGHT, POINT, MATCH, SPLASH, SETUP, CHOICE, DANCE, RESULTS = range(9)


def capture(ui, shot):
    ui.path("B:/GORILLAS.O88")
    w = ui.window("Gorillas")
    p = grt.Probe(ui, grt.offsets())
    m = ui.m
    ui.mo.to(620, 460)

    def wait(pred, secs=40):
        for _ in range(int(secs / 0.1)):
            if pred():
                return True
            os88marty.guest_sleep(m, 0.1)
        return False

    def key(name):
        m.key(name)
        os88marty.guest_sleep(m, 0.15)

    def digits(s):
        for ch in s:
            key("Digit" + ch)

    # The opening screen, with its marquee running.
    wait(lambda: p.b("state") == SPLASH)
    os88marty.guest_sleep(m, 2.5)
    shot("gorillas-intro", window="Gorillas", pad=10, settle=False)

    # Setup: one player, a name, first to 3, the default gravity.
    key("Space")
    wait(lambda: p.b("state") == SETUP)
    digits("1")
    key("Enter")
    wait(lambda: p.b("setupfield") == 1)
    m.type_text("Kong")
    key("Enter")
    wait(lambda: p.b("setupfield") == 2)
    key("Enter")                        # the computer keeps its name
    wait(lambda: p.b("setupfield") == 3)
    key("Enter")                        # first to 3
    wait(lambda: p.b("setupfield") == 4)
    os88marty.guest_sleep(m, 0.5)
    shot("gorillas-setup", window="Gorillas", pad=10, settle=False)
    key("Enter")                        # gravity 9.8
    wait(lambda: p.b("setupfield") == 5)
    key("Enter")                        # music: yes
    wait(lambda: p.b("setupfield") == 6)
    key("Enter")                        # aim guides: no
    wait(lambda: p.b("state") == CHOICE)
    key("KeyP")
    wait(lambda: p.b("state") == AIM and p.b("turn") == 0)
    os88marty.guest_sleep(m, 1.0)

    # Aim the first throw.
    rnd = random.Random(8088)
    digits("62")
    key("Enter")
    digits("58")
    os88marty.guest_sleep(m, 0.5)
    shot("gorillas-aim", window="Gorillas", pad=10, settle=False)
    key("Enter")
    wait(lambda: p.b("state") == FLIGHT)
    os88marty.guest_sleep(m, 0.7)
    shot("gorillas-flight", window="Gorillas", pad=10, settle=False)

    # Keep throwing until somebody scores.
    scored = False
    for _ in range(14):
        wait(lambda: p.b("state") != FLIGHT, 30)
        if p.b("state") in (POINT, MATCH):
            scored = True
            break
        wait(lambda: (p.b("state") == AIM and p.b("turn") == 0)
             or p.b("state") in (POINT, MATCH), 60)
        if p.b("state") in (POINT, MATCH):
            scored = True
            break
        os88marty.guest_sleep(m, 0.5)
        digits(str(rnd.randint(45, 70)))
        key("Enter")
        digits(str(rnd.randint(45, 85)))
        key("Enter")
        wait(lambda: p.b("state") == FLIGHT, 5)
    if scored:
        os88marty.guest_sleep(m, 2.0)
        shot("gorillas-point", window="Gorillas", pad=10, settle=False)
        wait(lambda: p.b("state") == AIM, 60)
        for _ in range(20):
            if p.b("state") == AIM:
                break
            key("Enter")
            os88marty.guest_sleep(m, 1.0)

    # Full screen: the whole display, in the original game's colours.
    wait(lambda: p.b("state") == AIM, 60)
    os88marty.guest_sleep(m, 1.0)
    key("KeyF")
    os88marty.guest_sleep(m, 4.0)
    shot("gorillas-full", settle=False)
    key("Escape")
    os88marty.guest_sleep(m, 2.0)
